// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.
//
// drm_lease_output.h says where each part comes from. ReadConnectorMode, FindPropertyId, LinkConnectorToLeasedCrtc
// and ReadPanelOrientation are Azahar's (src/citra_qt/drm_lease/drm_lease_window.cpp at cerroverb/azahar 71a132b3b,
// on virtudude's drm-leasing d16299140), with fprintf for its log calls and uint32_t for u32. CreateDrmDisplaySurface
// is Azahar's (src/video_core/renderer_vulkan/vk_platform.cpp, d16299140) in Vulkan's C API; it takes the first
// device that knows the leased connector instead of Azahar's configured device index.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <vector>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <SDL3/SDL_vulkan.h>

#include "drm_lease_client.h"
#include "drm_lease_output.h"

namespace drm_lease {

namespace {

// The preferred (or first) mode of the leased connector; VK_KHR_display picks
// its mode independently, this sizes the framebuffer layout and links the CRTC.
bool ReadConnectorMode(int lease_fd, uint32_t connector_id, drmModeModeInfo& out_mode) {
    drmModeConnector* connector = drmModeGetConnector(lease_fd, connector_id);
    if (!connector || connector->count_modes < 1) {
        if (connector) {
            drmModeFreeConnector(connector);
        }
        return false;
    }

    drmModeModeInfo mode = connector->modes[0];
    for (int i = 0; i < connector->count_modes; i++) {
        if (connector->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
            mode = connector->modes[i];
            break;
        }
    }

    out_mode = mode;
    drmModeFreeConnector(connector);
    return true;
}

uint32_t FindPropertyId(int lease_fd, uint32_t object_id, uint32_t object_type, const char* name) {
    drmModeObjectProperties* props = drmModeObjectGetProperties(lease_fd, object_id, object_type);
    if (!props) {
        return 0;
    }

    uint32_t property_id = 0;
    for (uint32_t i = 0; i < props->count_props && property_id == 0; i++) {
        drmModePropertyRes* prop = drmModeGetProperty(lease_fd, props->props[i]);
        if (!prop) {
            continue;
        }
        if (std::strcmp(prop->name, name) == 0) {
            property_id = prop->prop_id;
        }
        drmModeFreeProperty(prop);
    }
    drmModeFreeObjectProperties(props);
    return property_id;
}

// An idle compositor leaves the connector unlinked, and Mesa then accepts a
// CRTC only if drmModeGetCrtc reports no framebuffer. That call reads the
// CRTC's legacy primary plane, which need not be the leased plane and may be
// scanning out for the compositor, so vkGetPhysicalDeviceSurfaceFormatsKHR can
// return no formats. Link the connector first, with the CRTC left inactive so
// that no framebuffer is needed and the panel stays off until the first present.
// The property sequence follows drm_atomic_commit in Mesa's wsi_common_display.c.
void LinkConnectorToLeasedCrtc(int lease_fd, uint32_t connector_id, drmModeModeInfo mode) {
    drmModeConnector* connector = drmModeGetConnectorCurrent(lease_fd, connector_id);
    if (!connector) {
        return;
    }
    const bool linked = connector->encoder_id != 0;
    drmModeFreeConnector(connector);
    if (linked) {
        return;
    }

    drmModeRes* resources = drmModeGetResources(lease_fd);
    if (!resources) {
        return;
    }
    const uint32_t crtc_id = resources->count_crtcs > 0 ? resources->crtcs[0] : 0;
    drmModeFreeResources(resources);
    if (crtc_id == 0) {
        std::fprintf(stderr, "[Warning] drm-lease: the lease holds no CRTC\n");
        return;
    }

    if (drmSetClientCap(lease_fd, DRM_CLIENT_CAP_ATOMIC, 1) != 0) {
        std::fprintf(stderr, "[Warning] drm-lease: atomic modesetting is unavailable\n");
        return;
    }

    const uint32_t connector_crtc =
        FindPropertyId(lease_fd, connector_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");
    const uint32_t crtc_mode = FindPropertyId(lease_fd, crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID");
    const uint32_t crtc_active = FindPropertyId(lease_fd, crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE");
    if (connector_crtc == 0 || crtc_mode == 0 || crtc_active == 0) {
        std::fprintf(stderr, "[Warning] drm-lease: missing atomic properties for the leased objects\n");
        return;
    }

    uint32_t mode_blob = 0;
    if (drmModeCreatePropertyBlob(lease_fd, &mode, sizeof(mode), &mode_blob) != 0) {
        std::fprintf(stderr, "[Warning] drm-lease: failed to create the mode blob\n");
        return;
    }

    drmModeAtomicReq* request = drmModeAtomicAlloc();
    if (request) {
        drmModeAtomicAddProperty(request, connector_id, connector_crtc, crtc_id);
        drmModeAtomicAddProperty(request, crtc_id, crtc_mode, mode_blob);
        drmModeAtomicAddProperty(request, crtc_id, crtc_active, 0);
        const int ret =
            drmModeAtomicCommit(lease_fd, request, DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr);
        if (ret != 0) {
            std::fprintf(stderr, "[Warning] drm-lease: failed to link connector %u to CRTC %u: %s\n",
                         connector_id, crtc_id, std::strerror(-ret));
        } else {
            std::fprintf(stderr, "[Info] drm-lease: linked connector %u to CRTC %u\n", connector_id, crtc_id);
        }
        drmModeAtomicFree(request);
    }
    drmModeDestroyPropertyBlob(lease_fd, mode_blob);
}

// Content rotation from the connector's "panel orientation" property, or -1
// when absent. Left/Right Side Up map per the compositor convention.
int ReadPanelOrientation(int lease_fd, uint32_t connector_id) {
    drmModeObjectProperties* props =
        drmModeObjectGetProperties(lease_fd, connector_id, DRM_MODE_OBJECT_CONNECTOR);
    if (!props) {
        return -1;
    }

    int rotation = -1;
    for (uint32_t i = 0; i < props->count_props && rotation < 0; i++) {
        drmModePropertyRes* prop = drmModeGetProperty(lease_fd, props->props[i]);
        if (!prop) {
            continue;
        }
        if (std::strcmp(prop->name, "panel orientation") == 0) {
            for (int j = 0; j < prop->count_enums; j++) {
                if (prop->enums[j].value != props->prop_values[i]) {
                    continue;
                }
                const char* name = prop->enums[j].name;
                if (std::strcmp(name, "Normal") == 0) {
                    rotation = 0;
                } else if (std::strcmp(name, "Upside Down") == 0) {
                    rotation = 180;
                } else if (std::strcmp(name, "Left Side Up") == 0) {
                    rotation = 90;
                } else if (std::strcmp(name, "Right Side Up") == 0) {
                    rotation = 270;
                }
                break;
            }
        }
        drmModeFreeProperty(prop);
    }
    drmModeFreeObjectProperties(props);
    return rotation;
}

} // namespace

namespace {

// The Vulkan entry points used here, from the loader SDL opened (SDL_Vulkan_GetVkGetInstanceProcAddr).
struct VulkanCalls {
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;
    PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties = nullptr;
    PFN_vkCreateInstance CreateInstance = nullptr;
    PFN_vkDestroyInstance DestroyInstance = nullptr;
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR GetPhysicalDeviceSurfaceSupportKHR = nullptr;
    PFN_vkGetDrmDisplayEXT GetDrmDisplayEXT = nullptr;
    PFN_vkAcquireDrmDisplayEXT AcquireDrmDisplayEXT = nullptr;
    PFN_vkGetDisplayModePropertiesKHR GetDisplayModePropertiesKHR = nullptr;
    PFN_vkGetPhysicalDeviceDisplayPropertiesKHR GetPhysicalDeviceDisplayPropertiesKHR = nullptr;
    PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR GetPhysicalDeviceDisplayPlanePropertiesKHR = nullptr;
    PFN_vkGetDisplayPlaneSupportedDisplaysKHR GetDisplayPlaneSupportedDisplaysKHR = nullptr;
    PFN_vkGetDisplayPlaneCapabilitiesKHR GetDisplayPlaneCapabilitiesKHR = nullptr;
    PFN_vkCreateDisplayPlaneSurfaceKHR CreateDisplayPlaneSurfaceKHR = nullptr;
    PFN_vkDestroySurfaceKHR DestroySurfaceKHR = nullptr;

    template <typename F>
    void Load(F& f, VkInstance instance, const char* name) {
        f = reinterpret_cast<F>(GetInstanceProcAddr(instance, name));
    }
    void LoadInstance(VkInstance instance) {
        Load(DestroyInstance, instance, "vkDestroyInstance");
        Load(EnumeratePhysicalDevices, instance, "vkEnumeratePhysicalDevices");
        Load(GetPhysicalDeviceQueueFamilyProperties, instance, "vkGetPhysicalDeviceQueueFamilyProperties");
        Load(GetPhysicalDeviceSurfaceSupportKHR, instance, "vkGetPhysicalDeviceSurfaceSupportKHR");
        Load(GetDrmDisplayEXT, instance, "vkGetDrmDisplayEXT");
        Load(AcquireDrmDisplayEXT, instance, "vkAcquireDrmDisplayEXT");
        Load(GetDisplayModePropertiesKHR, instance, "vkGetDisplayModePropertiesKHR");
        Load(GetPhysicalDeviceDisplayPropertiesKHR, instance, "vkGetPhysicalDeviceDisplayPropertiesKHR");
        Load(GetPhysicalDeviceDisplayPlanePropertiesKHR, instance, "vkGetPhysicalDeviceDisplayPlanePropertiesKHR");
        Load(GetDisplayPlaneSupportedDisplaysKHR, instance, "vkGetDisplayPlaneSupportedDisplaysKHR");
        Load(GetDisplayPlaneCapabilitiesKHR, instance, "vkGetDisplayPlaneCapabilitiesKHR");
        Load(CreateDisplayPlaneSurfaceKHR, instance, "vkCreateDisplayPlaneSurfaceKHR");
        Load(DestroySurfaceKHR, instance, "vkDestroySurfaceKHR");
    }
};

// An instance with Azahar's three display extensions (GetInstanceExtensions, enable_drm_display) and the surface
// extension, plus the two SDL's renderer enables on an instance of its own where offered (SDL_render_vulkan.c,
// VULKAN_CreateDeviceResources), which it then also uses on one it is given.
VkInstance CreateDisplayInstance(VulkanCalls& vk) {
    uint32_t count = 0;
    vk.EnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vk.EnumerateInstanceExtensionProperties(nullptr, &count, available.data());
    const auto offered = [&](const char* name) {
        for (const VkExtensionProperties& e : available) {
            if (std::strcmp(e.extensionName, name) == 0) {
                return true;
            }
        }
        return false;
    };
    std::vector<const char*> extensions;
    for (const char* name : {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_DISPLAY_EXTENSION_NAME,
                             VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME, VK_EXT_ACQUIRE_DRM_DISPLAY_EXTENSION_NAME}) {
        if (!offered(name)) {
            std::fprintf(stderr, "[Error] drm-lease: the Vulkan driver lacks %s\n", name);
            return VK_NULL_HANDLE;
        }
        extensions.push_back(name);
    }
    for (const char* name : {VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME,
                             VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME}) {
        if (offered(name)) {
            extensions.push_back(name);
        }
    }
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "mh4u-linux";
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    VkInstance instance = VK_NULL_HANDLE;
    if (const VkResult res = vk.CreateInstance(&info, nullptr, &instance); res != VK_SUCCESS) {
        std::fprintf(stderr, "[Error] drm-lease: vkCreateInstance failed: %d\n", res);
        return VK_NULL_HANDLE;
    }
    return instance;
}

// Creates a VK_KHR_display surface on a leased DRM connector. The lease fd
// comes from the frontend (drm-lease-v1); the display is acquired through
// VK_EXT_acquire_drm_display and driven directly, bypassing the window system.
VkSurfaceKHR CreateDrmDisplaySurface(const VulkanCalls& vk, VkInstance instance, int drm_lease_fd,
                                     uint32_t drm_connector_id, uint32_t drm_mode_width, uint32_t drm_mode_height,
                                     VkPhysicalDevice& out_physical_device) {
    if (!vk.GetDrmDisplayEXT || !vk.AcquireDrmDisplayEXT) {
        std::fprintf(stderr, "[Error] drm-lease: VK_EXT_acquire_drm_display is not supported by this driver\n");
        return VK_NULL_HANDLE;
    }

    uint32_t device_count = 0;
    vk.EnumeratePhysicalDevices(instance, &device_count, nullptr);
    std::vector<VkPhysicalDevice> physical_devices(device_count);
    vk.EnumeratePhysicalDevices(instance, &device_count, physical_devices.data());
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDisplayKHR display = VK_NULL_HANDLE;
    for (VkPhysicalDevice candidate : physical_devices) {
        if (vk.GetDrmDisplayEXT(candidate, drm_lease_fd, drm_connector_id, &display) == VK_SUCCESS &&
            display != VK_NULL_HANDLE) {
            physical_device = candidate;
            break;
        }
    }
    if (physical_device == VK_NULL_HANDLE) {
        std::fprintf(stderr, "[Error] drm-lease: no Vulkan device knows connector %u\n", drm_connector_id);
        return VK_NULL_HANDLE;
    }
    if (const VkResult res = vk.AcquireDrmDisplayEXT(physical_device, drm_lease_fd, display); res != VK_SUCCESS) {
        std::fprintf(stderr, "[Error] drm-lease: vkAcquireDrmDisplayEXT failed: %d\n", res);
        return VK_NULL_HANDLE;
    }

    // The frontend sized its framebuffer from the DRM preferred mode;
    // Vulkan does not order this list, so match it explicitly.
    uint32_t mode_count = 0;
    vk.GetDisplayModePropertiesKHR(physical_device, display, &mode_count, nullptr);
    std::vector<VkDisplayModePropertiesKHR> modes(mode_count);
    vk.GetDisplayModePropertiesKHR(physical_device, display, &mode_count, modes.data());
    if (modes.empty()) {
        std::fprintf(stderr, "[Error] drm-lease: Leased display has no modes\n");
        return VK_NULL_HANDLE;
    }
    const VkDisplayModePropertiesKHR* mode = nullptr;
    for (const VkDisplayModePropertiesKHR& m : modes) {
        if (m.parameters.visibleRegion.width == drm_mode_width &&
            m.parameters.visibleRegion.height == drm_mode_height) {
            mode = &m;
            break;
        }
    }
    if (mode == nullptr) {
        std::fprintf(stderr, "[Error] drm-lease: Leased display has no %ux%u mode\n", drm_mode_width, drm_mode_height);
        return VK_NULL_HANDLE;
    }

    uint32_t display_count = 0;
    vk.GetPhysicalDeviceDisplayPropertiesKHR(physical_device, &display_count, nullptr);
    std::vector<VkDisplayPropertiesKHR> displays(display_count);
    vk.GetPhysicalDeviceDisplayPropertiesKHR(physical_device, &display_count, displays.data());
    const VkDisplayPropertiesKHR* display_props = nullptr;
    for (const VkDisplayPropertiesKHR& d : displays) {
        if (d.display == display) {
            display_props = &d;
            break;
        }
    }
    if (display_props == nullptr || !(display_props->supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)) {
        std::fprintf(stderr, "[Error] drm-lease: Leased display does not support the identity transform\n");
        return VK_NULL_HANDLE;
    }

    uint32_t plane_count = 0;
    vk.GetPhysicalDeviceDisplayPlanePropertiesKHR(physical_device, &plane_count, nullptr);
    std::vector<VkDisplayPlanePropertiesKHR> planes(plane_count);
    vk.GetPhysicalDeviceDisplayPlanePropertiesKHR(physical_device, &plane_count, planes.data());
    std::optional<uint32_t> plane_index;
    for (uint32_t i = 0; i < planes.size(); i++) {
        if (planes[i].currentDisplay && planes[i].currentDisplay != display) {
            continue;
        }
        uint32_t supported_count = 0;
        vk.GetDisplayPlaneSupportedDisplaysKHR(physical_device, i, &supported_count, nullptr);
        std::vector<VkDisplayKHR> supported(supported_count);
        vk.GetDisplayPlaneSupportedDisplaysKHR(physical_device, i, &supported_count, supported.data());
        for (VkDisplayKHR d : supported) {
            if (d == display) {
                plane_index = i;
                break;
            }
        }
        if (plane_index) {
            break;
        }
    }
    if (!plane_index) {
        std::fprintf(stderr, "[Error] drm-lease: No display plane supports the leased display\n");
        return VK_NULL_HANDLE;
    }

    VkDisplayPlaneCapabilitiesKHR plane_caps{};
    vk.GetDisplayPlaneCapabilitiesKHR(physical_device, mode->displayMode, *plane_index, &plane_caps);
    VkDisplayPlaneAlphaFlagBitsKHR alpha_mode{};
    bool alpha_found = false;
    for (const VkDisplayPlaneAlphaFlagBitsKHR candidate :
         {VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR, VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR,
          VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR, VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR}) {
        if (plane_caps.supportedAlpha & candidate) {
            alpha_mode = candidate;
            alpha_found = true;
            break;
        }
    }
    if (!alpha_found) {
        std::fprintf(stderr, "[Error] drm-lease: Leased display plane advertises no alpha mode\n");
        return VK_NULL_HANDLE;
    }

    VkDisplaySurfaceCreateInfoKHR display_ci{};
    display_ci.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
    display_ci.displayMode = mode->displayMode;
    display_ci.planeIndex = *plane_index;
    display_ci.planeStackIndex = planes[*plane_index].currentStackIndex;
    display_ci.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    display_ci.globalAlpha = 1.0f;
    display_ci.alphaMode = alpha_mode;
    display_ci.imageExtent = mode->parameters.visibleRegion;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (const VkResult res = vk.CreateDisplayPlaneSurfaceKHR(instance, &display_ci, nullptr, &surface);
        res != VK_SUCCESS) {
        std::fprintf(stderr, "[Error] drm-lease: vkCreateDisplayPlaneSurfaceKHR failed: %d\n", res);
        return VK_NULL_HANDLE;
    }

    std::fprintf(stderr, "[Info] drm-lease: Created DRM display surface: connector %u mode %ux%u plane %u\n",
                 drm_connector_id, mode->parameters.visibleRegion.width, mode->parameters.visibleRegion.height,
                 *plane_index);
    out_physical_device = physical_device;
    return surface;
}

// A queue family of the device that draws and can present to the surface.
std::optional<uint32_t> FindQueueFamily(const VulkanCalls& vk, VkPhysicalDevice physical_device,
                                        VkSurfaceKHR surface) {
    uint32_t count = 0;
    vk.GetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vk.GetPhysicalDeviceQueueFamilyProperties(physical_device, &count, families.data());
    for (uint32_t i = 0; i < count; i++) {
        VkBool32 present = VK_FALSE;
        vk.GetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface, &present);
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
            return i;
        }
    }
    return std::nullopt;
}

VulkanCalls g_vk;

}  // namespace

DrmLeaseOutput::DrmLeaseOutput() = default;

DrmLeaseOutput::~DrmLeaseOutput() {
    touch.reset();
    if (renderer) {
        SDL_DestroyRenderer(renderer);  // (the instance and the surface are ours: SDL leaves them)
    }
    if (surface != 0) {
        g_vk.DestroySurfaceKHR(static_cast<VkInstance>(instance), reinterpret_cast<VkSurfaceKHR>(surface), nullptr);
    }
    if (instance) {
        g_vk.DestroyInstance(static_cast<VkInstance>(instance), nullptr);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
    client.reset();  // (the lease goes back to the compositor)
}

void DrmLeaseOutput::PumpEvents() {
    if (client) {
        client->PumpEvents();
    }
}

bool DrmLeaseOutput::Initialize(const std::string& connector_name, int rotation_, const std::string& touch_device,
                                bool internal_only, TouchSink touch_sink) {
    // the lease, its mode, its CRTC and its rotation, as Azahar's DrmLeaseWindow::Initialize
    client = std::make_unique<DrmLeaseClient>();
    if (!client->Acquire(connector_name, internal_only)) {
        client.reset();
        return false;
    }
    drmModeModeInfo mode{};
    if (!ReadConnectorMode(client->GetLeaseFd(), client->GetConnectorId(), mode)) {
        std::fprintf(stderr, "[Error] drm-lease: failed to read a mode from the leased connector\n");
        client.reset();
        return false;
    }
    LinkConnectorToLeasedCrtc(client->GetLeaseFd(), client->GetConnectorId(), mode);
    if (rotation_ < 0) {
        rotation_ = ReadPanelOrientation(client->GetLeaseFd(), client->GetConnectorId());
        if (rotation_ < 0) {
            rotation_ = 0;
            std::fprintf(stderr,
                         "[Info] drm-lease: connector reports no panel orientation, assuming unrotated; "
                         "set MH4U_DRM_LEASE_ROTATION if the output is sideways\n");
        } else {
            std::fprintf(stderr, "[Info] drm-lease: panel orientation property gives rotation %d\n", rotation_);
        }
    }
    // Azahar's rotation turns the picture counter-clockwise (the Thor's bottom panel, "Right Side Up", is 270 there and
    // stood upside down when drawn 270 degrees clockwise); SDL_RenderTextureRotated turns it clockwise
    rotation = (360 - rotation_ % 360) % 360;

    // the surface on it, drawn with SDL's Vulkan renderer through a window that is never shown
    window = SDL_CreateWindow("MH4U Bottom", mode.hdisplay, mode.vdisplay, SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN);
    if (window == nullptr) {
        std::fprintf(stderr, "[Error] drm-lease: SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
    g_vk.GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    if (g_vk.GetInstanceProcAddr == nullptr) {
        std::fprintf(stderr, "[Error] drm-lease: no Vulkan loader: %s\n", SDL_GetError());
        return false;
    }
    g_vk.Load(g_vk.EnumerateInstanceExtensionProperties, VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
    g_vk.Load(g_vk.CreateInstance, VK_NULL_HANDLE, "vkCreateInstance");
    VkInstance vk_instance = CreateDisplayInstance(g_vk);
    if (vk_instance == VK_NULL_HANDLE) {
        return false;
    }
    instance = vk_instance;
    g_vk.LoadInstance(vk_instance);
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkSurfaceKHR vk_surface = CreateDrmDisplaySurface(g_vk, vk_instance, client->GetLeaseFd(), client->GetConnectorId(),
                                                      mode.hdisplay, mode.vdisplay, physical_device);
    if (vk_surface == VK_NULL_HANDLE) {
        return false;
    }
    surface = reinterpret_cast<uint64_t>(vk_surface);
    const std::optional<uint32_t> family = FindQueueFamily(g_vk, physical_device, vk_surface);
    if (!family) {
        std::fprintf(stderr, "[Error] drm-lease: no queue family draws and presents to the leased display\n");
        return false;
    }
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_RENDERER_CREATE_NAME_STRING, "vulkan");
    SDL_SetPointerProperty(props, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, window);
    SDL_SetPointerProperty(props, SDL_PROP_RENDERER_CREATE_VULKAN_INSTANCE_POINTER, vk_instance);
    SDL_SetNumberProperty(props, SDL_PROP_RENDERER_CREATE_VULKAN_SURFACE_NUMBER, static_cast<Sint64>(surface));
    SDL_SetPointerProperty(props, SDL_PROP_RENDERER_CREATE_VULKAN_PHYSICAL_DEVICE_POINTER, physical_device);
    SDL_SetNumberProperty(props, SDL_PROP_RENDERER_CREATE_VULKAN_GRAPHICS_QUEUE_FAMILY_INDEX_NUMBER, *family);
    SDL_SetNumberProperty(props, SDL_PROP_RENDERER_CREATE_VULKAN_PRESENT_QUEUE_FAMILY_INDEX_NUMBER, *family);
    SDL_SetNumberProperty(props, SDL_PROP_RENDERER_CREATE_PRESENT_VSYNC_NUMBER, 1);
    renderer = SDL_CreateRendererWithProperties(props);
    SDL_DestroyProperties(props);
    if (renderer == nullptr) {
        std::fprintf(stderr, "[Error] drm-lease: SDL_CreateRenderer on the leased display: %s\n", SDL_GetError());
        return false;
    }

    if (!touch_device.empty()) {
        touch = std::make_unique<DrmLeaseTouch>(std::move(touch_sink), touch_device);
    }
    std::fprintf(stderr, "[Info] drm-lease: touch screen output ready, %ux%u, turned %d degrees clockwise\n",
                 mode.hdisplay, mode.vdisplay, rotation);
    return true;
}

}  // namespace drm_lease
