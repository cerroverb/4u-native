// Shared images (shared_images.h): AHardwareBuffers bound to VkImages, the foreign queue family's hand-over, sync files.
// Written for this repository from the Vulkan specification (VK_ANDROID_external_memory_android_hardware_buffer,
// VK_EXT_queue_family_foreign, VK_KHR_external_fence_fd) and the NDK's AHardwareBuffer documentation.
#include "gpu/backend/shared/shared_images.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

#include <fmt/format.h>

#include "gpu/backend/core/impl.h"
#include "gpu/backend/vulkan_access.h"
#include "mh4n/log.h"

#if defined(__ANDROID__)
#include <android/hardware_buffer.h>
#include <poll.h>
#include <unistd.h>
// (the NDK's copy: the tree's Vulkan headers have no platform headers; it only declares the Android types and
// function pointer types, against the vulkan_core.h volk already included)
#include <vulkan/vulkan_android.h>
#endif

namespace mh4n::gpu::rhi {

namespace {
constexpr const char* kLog = "RHI";
[[maybe_unused]] std::atomic<u64> g_next_id{1};  // (SharedImage::Id; Android only)
}  // namespace

// ---- SyncFile ---------------------------------------------------------------------------------------------------------

SyncFile::~SyncFile() {
#if defined(__ANDROID__)
    if (fd_ >= 0) {
        close(fd_);
    }
#endif
}

int SyncFile::Dup() const {
#if defined(__ANDROID__)
    return fd_ >= 0 ? dup(fd_) : -1;
#else
    return -1;
#endif
}

bool SyncFile::Signaled() const {
#if defined(__ANDROID__)
    if (fd_ < 0) {
        return true;
    }
    pollfd p{fd_, POLLIN, 0};
    return poll(&p, 1, 0) > 0;
#else
    return true;
#endif
}

// ---- SharedImage ------------------------------------------------------------------------------------------------------

struct SharedImage::Impl {
    // The device's own state, not the Backend handle: a retired image sits in that state's list, and a hold on the
    // handle from there would keep the handle (and the device) alive for good (core/device.cpp, ReleaseWithLastHandle).
    std::shared_ptr<Backend::Impl> device_owner;
    std::shared_ptr<Image> image;  // wrapped over `vk_image` (destroyed first: its views)
    VkImage vk_image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    std::shared_ptr<void> buffer;  // the AHardwareBuffer, released by its deleter
    u32 width = 0, height = 0;
    u64 id = 0;

    ~Impl() {
        image.reset();
        if (device_owner != nullptr) {
            const VkDevice device = device_owner->device->vk;
            if (vk_image != VK_NULL_HANDLE) vkDestroyImage(device, vk_image, nullptr);
            if (memory != VK_NULL_HANDLE) vkFreeMemory(device, memory, nullptr);
        }
    }
};

SharedImage::SharedImage(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SharedImage::~SharedImage() = default;
Image& SharedImage::GetImage() { return *impl_->image; }
std::shared_ptr<void> SharedImage::BufferRef() const { return impl_->buffer; }
u32 SharedImage::Width() const { return impl_->width; }
u32 SharedImage::Height() const { return impl_->height; }
u64 SharedImage::Id() const { return impl_->id; }

// ---- SharedImages -----------------------------------------------------------------------------------------------------

struct SharedImages::Impl {
    std::shared_ptr<Backend> backend;
    VulkanHandles handles;
    VkImageUsageFlags usage = 0;
    u64 buffer_usage = 0;  // AHardwareBuffer_UsageFlags
#if defined(__ANDROID__)
    PFN_vkGetAndroidHardwareBufferPropertiesANDROID get_properties = nullptr;
#endif
    PFN_vkGetFenceFdKHR get_fence_fd = nullptr;
    VkFence fence = VK_NULL_HANDLE;  // exportable as a sync file; reset by every export
    bool fence_failed = false;

    ~Impl() {
        if (fence != VK_NULL_HANDLE) {
            vkDestroyFence(handles.device, fence, nullptr);
        }
    }
};

SharedImages::SharedImages(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SharedImages::~SharedImages() = default;

std::unique_ptr<SharedImages> SharedImages::Create(const std::shared_ptr<Backend>& backend, std::string* why) {
    const auto fail = [&](std::string text) -> std::unique_ptr<SharedImages> {
        if (why != nullptr) *why = std::move(text);
        return nullptr;
    };
#if !defined(__ANDROID__)
    (void)backend;
    return fail("shared images are made on Android only");
#else
    if (backend == nullptr) return fail("no backend");
    // A player's own driver for an Adreno (Mesa's Turnip through libadrenotools) next to the system's OpenGL ES: the two
    // would have to agree on the buffer's compressed layout, which has not been tried. MH4N_SHARED_IMAGES=1 tries it.
    constexpr u32 kDriverIdMesaTurnip = 18;  // VK_DRIVER_ID_MESA_TURNIP
    const char* force = std::getenv("MH4N_SHARED_IMAGES");
    if (backend->Caps().driver_id == kDriverIdMesaTurnip && !(force != nullptr && *force == '1')) {
        return fail("Mesa's Turnip driver: not tried with the system's OpenGL ES (MH4N_SHARED_IMAGES=1 tries it)");
    }
    const auto& enabled = backend->EnabledExtensions();
    const auto has = [&](const char* name) { return std::find(enabled.begin(), enabled.end(), name) != enabled.end(); };
    if (!has(VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME) ||
        !has(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME)) {
        return fail("the device has no VK_ANDROID_external_memory_android_hardware_buffer or VK_EXT_queue_family_foreign");
    }
    auto impl = std::make_unique<Impl>();
    impl->backend = backend;
    impl->handles = Handles(*backend);
    impl->get_properties = reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(
        vkGetDeviceProcAddr(impl->handles.device, "vkGetAndroidHardwareBufferPropertiesANDROID"));
    if (impl->get_properties == nullptr) return fail("vkGetAndroidHardwareBufferPropertiesANDROID is missing");

    // Can an R8G8B8A8 colour target in a hardware buffer be made, and with which buffer usage?
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkAndroidHardwareBufferUsageANDROID buffer_usage = {};
    bool supported = false;
    for (int attempt = 0; attempt < 2 && !supported; ++attempt) {
        if (attempt == 1) usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VkPhysicalDeviceExternalImageFormatInfo external_info = {};
        external_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
        external_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
        VkPhysicalDeviceImageFormatInfo2 info = {};
        info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
        info.pNext = &external_info;
        info.format = VK_FORMAT_R8G8B8A8_UNORM;
        info.type = VK_IMAGE_TYPE_2D;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        buffer_usage = {};
        buffer_usage.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_USAGE_ANDROID;
        VkExternalImageFormatProperties external_props = {};
        external_props.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;
        external_props.pNext = &buffer_usage;
        VkImageFormatProperties2 props = {};
        props.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
        props.pNext = &external_props;
        const VkResult result = vkGetPhysicalDeviceImageFormatProperties2(impl->handles.physical_device, &info, &props);
        supported = result == VK_SUCCESS &&
                    (external_props.externalMemoryProperties.externalMemoryFeatures &
                     VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) != 0;
    }
    if (!supported) return fail("the device cannot import an R8G8B8A8 colour target from a hardware buffer");
    impl->usage = usage;
    impl->buffer_usage = buffer_usage.androidHardwareBufferUsage | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                         AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;

    // Sync files of fences (optional: without them the reader waits on the CPU, WaitFor).
    if (has(VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME)) {
        VkPhysicalDeviceExternalFenceInfo fence_info = {};
        fence_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_FENCE_INFO;
        fence_info.handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;
        VkExternalFenceProperties fence_props = {};
        fence_props.sType = VK_STRUCTURE_TYPE_EXTERNAL_FENCE_PROPERTIES;
        vkGetPhysicalDeviceExternalFenceProperties(impl->handles.physical_device, &fence_info, &fence_props);
        impl->get_fence_fd = reinterpret_cast<PFN_vkGetFenceFdKHR>(
            vkGetDeviceProcAddr(impl->handles.device, "vkGetFenceFdKHR"));
        if ((fence_props.externalFenceFeatures & VK_EXTERNAL_FENCE_FEATURE_EXPORTABLE_BIT) != 0 &&
            impl->get_fence_fd != nullptr) {
            VkExportFenceCreateInfo export_info = {};
            export_info.sType = VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO;
            export_info.handleTypes = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;
            VkFenceCreateInfo create = {};
            create.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            create.pNext = &export_info;
            if (vkCreateFence(impl->handles.device, &create, nullptr, &impl->fence) != VK_SUCCESS) {
                impl->fence = VK_NULL_HANDLE;
            }
        }
    }
    MH4N_LOG(Info, kLog, "rhi: shared images (hardware buffers, usage {:#x}), sync files {}", impl->buffer_usage,
             impl->fence != VK_NULL_HANDLE ? "yes" : "no");
    return std::make_unique<SharedImages>(std::move(impl));
#endif
}

Result<std::shared_ptr<SharedImage>> SharedImages::Make(u32 width, u32 height) {
#if !defined(__ANDROID__)
    (void)width;
    (void)height;
    return Error{"shared images are made on Android only"};
#else
    Impl& s = *impl_;
    const VkDevice device = s.handles.device;
    AHardwareBuffer_Desc desc = {};
    desc.width = width;
    desc.height = height;
    desc.layers = 1;
    desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    desc.usage = s.buffer_usage;
    AHardwareBuffer* raw = nullptr;
    if (AHardwareBuffer_allocate(&desc, &raw) != 0 || raw == nullptr) {
        return Error{fmt::format("AHardwareBuffer_allocate {}x{} failed", width, height)};
    }
    auto image_impl = std::make_unique<SharedImage::Impl>();
    image_impl->buffer = std::shared_ptr<void>(raw, [](void* b) { AHardwareBuffer_release(static_cast<AHardwareBuffer*>(b)); });
    image_impl->device_owner = s.backend->impl().shared_from_this();
    image_impl->width = width;
    image_impl->height = height;
    image_impl->id = g_next_id.fetch_add(1);

    VkAndroidHardwareBufferFormatPropertiesANDROID format_props = {};
    format_props.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID;
    VkAndroidHardwareBufferPropertiesANDROID buffer_props = {};
    buffer_props.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
    buffer_props.pNext = &format_props;
    if (s.get_properties(device, raw, &buffer_props) != VK_SUCCESS || buffer_props.memoryTypeBits == 0) {
        return Error{"vkGetAndroidHardwareBufferPropertiesANDROID failed"};
    }
    if (format_props.format != VK_FORMAT_R8G8B8A8_UNORM) {
        return Error{fmt::format("the hardware buffer's Vulkan format is {}, not R8G8B8A8", static_cast<int>(format_props.format))};
    }

    VkExternalMemoryImageCreateInfo external = {};
    external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
    VkImageCreateInfo create = {};
    create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create.pNext = &external;
    create.imageType = VK_IMAGE_TYPE_2D;
    create.format = VK_FORMAT_R8G8B8A8_UNORM;
    create.extent = {width, height, 1};
    create.mipLevels = 1;
    create.arrayLayers = 1;
    create.samples = VK_SAMPLE_COUNT_1_BIT;
    create.tiling = VK_IMAGE_TILING_OPTIMAL;
    create.usage = s.usage;
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &create, nullptr, &image_impl->vk_image) != VK_SUCCESS) {
        image_impl->vk_image = VK_NULL_HANDLE;
        return Error{"vkCreateImage over a hardware buffer failed"};
    }

    VkImportAndroidHardwareBufferInfoANDROID import = {};
    import.sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
    import.buffer = raw;
    VkMemoryDedicatedAllocateInfo dedicated = {};
    dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated.pNext = &import;
    dedicated.image = image_impl->vk_image;
    VkPhysicalDeviceMemoryProperties memory_props = {};
    vkGetPhysicalDeviceMemoryProperties(s.handles.physical_device, &memory_props);
    u32 type = ~0u;
    for (int pass = 0; pass < 2 && type == ~0u; ++pass) {  // a device-local type first, else any the buffer allows
        for (u32 i = 0; i < memory_props.memoryTypeCount && type == ~0u; ++i) {
            const bool local = (memory_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
            if ((buffer_props.memoryTypeBits & (1u << i)) != 0 && (local || pass == 1)) {
                type = i;
            }
        }
    }
    VkMemoryAllocateInfo allocate = {};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.pNext = &dedicated;
    allocate.allocationSize = buffer_props.allocationSize;
    allocate.memoryTypeIndex = type;
    if (type == ~0u || vkAllocateMemory(device, &allocate, nullptr, &image_impl->memory) != VK_SUCCESS) {
        image_impl->memory = VK_NULL_HANDLE;
        return Error{"vkAllocateMemory importing a hardware buffer failed"};
    }
    if (vkBindImageMemory(device, image_impl->vk_image, image_impl->memory, 0) != VK_SUCCESS) {
        return Error{"vkBindImageMemory of a hardware buffer failed"};
    }
    Result<std::shared_ptr<Image>> wrapped =
        WrapExternalImage(*s.backend, image_impl->vk_image, Format::Rgba8Unorm, width, height);
    if (!wrapped.ok()) {
        return Error{wrapped.error()};
    }
    image_impl->image = *wrapped;
    return std::make_shared<SharedImage>(std::move(image_impl));
#endif
}

namespace {

void Barrier(Recorder& recorder, VkImage image, u32 src_family, u32 dst_family, VkPipelineStageFlags src_stage,
             VkAccessFlags src_access, VkPipelineStageFlags dst_stage, VkAccessFlags dst_access) {
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = dst_access;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;  // (the backend's one layout: the hand-over keeps it)
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = src_family;
    barrier.dstQueueFamilyIndex = dst_family;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(CommandBuffer(recorder), src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

}  // namespace

void SharedImages::Acquire(Recorder& recorder, SharedImage& image) {
    Barrier(recorder, image.impl().vk_image, VK_QUEUE_FAMILY_FOREIGN_EXT, impl_->handles.queue_family_index,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
}

void SharedImages::Release(Recorder& recorder, SharedImage& image) {
    Barrier(recorder, image.impl().vk_image, impl_->handles.queue_family_index, VK_QUEUE_FAMILY_FOREIGN_EXT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0);
}

bool SharedImages::CanCopy() const {
    constexpr VkImageUsageFlags kCopy = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return (impl_->usage & kCopy) == kCopy;
}

void SharedImages::AcquireAll(Recorder& recorder, SharedImage& image) {
    Barrier(recorder, image.impl().vk_image, VK_QUEUE_FAMILY_FOREIGN_EXT, impl_->handles.queue_family_index,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
}

void SharedImages::ReleaseAll(Recorder& recorder, SharedImage& image) {
    Barrier(recorder, image.impl().vk_image, impl_->handles.queue_family_index, VK_QUEUE_FAMILY_FOREIGN_EXT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0);
}

bool SharedImages::CanExportFences() const {
    return impl_->fence != VK_NULL_HANDLE && !impl_->fence_failed;
}

std::shared_ptr<SyncFile> SharedImages::ExportFence() {
    Impl& s = *impl_;
    if (s.fence == VK_NULL_HANDLE || s.fence_failed) {
        return nullptr;
    }
    // An empty submission with the fence: it signals once all the work submitted before it has finished.
    VkResult result;
    {
        QueueLock lock(*s.backend);
        result = vkQueueSubmit(s.handles.queue, 0, nullptr, s.fence);
    }
    int fd = -1;
    if (result == VK_SUCCESS) {
        VkFenceGetFdInfoKHR info = {};
        info.sType = VK_STRUCTURE_TYPE_FENCE_GET_FD_INFO_KHR;
        info.fence = s.fence;
        info.handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;
        result = s.get_fence_fd(s.handles.device, &info, &fd);  // (the export resets the fence)
    }
    if (result != VK_SUCCESS) {
        MH4N_LOG(Warning, kLog, "rhi: a sync file of a fence failed ({}): the readers wait on the CPU from now on",
                 static_cast<int>(result));
        s.fence_failed = true;
        vkWaitForFences(s.handles.device, 1, &s.fence, VK_TRUE, UINT64_MAX);
        return nullptr;
    }
    // (-1: the fence had signalled already; nothing to wait for)
    return std::make_shared<SyncFile>(fd);
}

bool SharedImages::WaitFor(Backend& backend, TimelineValue value, u64 timeout_ns) {
    Backend::Impl& impl = backend.impl();
    if (value == 0 || impl.Completed() >= value) {
        return true;
    }
    if (value > impl.last_submitted.load()) {
        return false;  // (never issued: it would never come)
    }
    if (impl.timeline != VK_NULL_HANDLE) {
        VkSemaphoreWaitInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        info.semaphoreCount = 1;
        info.pSemaphores = &impl.timeline;
        info.pValues = &value;
        return vkWaitSemaphores(impl.device->vk, &info, timeout_ns) == VK_SUCCESS;
    }
    const auto end = std::chrono::steady_clock::now() + std::chrono::nanoseconds(timeout_ns);
    while (impl.Completed() < value) {
        if (std::chrono::steady_clock::now() >= end) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(250));
    }
    return true;
}

}  // namespace mh4n::gpu::rhi
