// The layout policy and the owner rule of the targets module (N2-27). See impl.h for the policy.
#include <algorithm>
#include <cstdlib>
#include <span>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include "gpu/backend/targets/impl.h"

namespace mh4n::gpu::rhi::targets {

using ::plume::RenderTextureLayout;

namespace {

// The image barriers wait only with the stages that touch images: fragment shading, the attachments, transfers,
// compute (and the host). No vertex stage reads an image here (the PICA's vertex programs sample nothing), so the
// vertex and tiling work of the passes after a barrier may run while the fragment work before it finishes, which a
// tile-based GPU does in a queue of its own (on Mali the vertex/tiler and fragment jobs overlap). plume's barrier call
// makes the vertex stages wait too (its stage sets name every graphics stage), so these barriers are recorded here,
// with plume's own layout and stage bookkeeping. MH4N_IMAGE_BARRIERS=full: plume's full barriers, as before (a
// measurement aid).
bool NarrowImageBarriers() {
    static const bool narrow = [] {
        const char* v = std::getenv("MH4N_IMAGE_BARRIERS");
        return v == nullptr || std::string_view(v) != "full";
    }();
    return narrow;
}

VkImageLayout VkLayoutOf(RenderTextureLayout layout) {
    switch (layout) {
    case RenderTextureLayout::GENERAL: return VK_IMAGE_LAYOUT_GENERAL;
    case RenderTextureLayout::SHADER_READ: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case RenderTextureLayout::COLOR_WRITE: return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case RenderTextureLayout::DEPTH_WRITE: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    case RenderTextureLayout::DEPTH_READ: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    case RenderTextureLayout::COPY_SOURCE:
    case RenderTextureLayout::RESOLVE_SOURCE: return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case RenderTextureLayout::COPY_DEST:
    case RenderTextureLayout::RESOLVE_DEST: return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case RenderTextureLayout::PRESENT: return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    default: return VK_IMAGE_LAYOUT_UNDEFINED;
    }
}

// plume's toStageFlags, for the stages a texture's last barrier gave it (the source scope here).
VkPipelineStageFlags VkStagesOf(::plume::RenderBarrierStages stages, bool geometry) {
    VkPipelineStageFlags flags = 0;
    if (stages & ::plume::RenderBarrierStage::GRAPHICS) {
        flags |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        if (geometry) flags |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
    }
    if (stages & ::plume::RenderBarrierStage::COMPUTE) flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    if (stages & ::plume::RenderBarrierStage::COPY) flags |= VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT;
    return flags;
}

constexpr VkPipelineStageFlags kImageStages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                                              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                              VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT;

// One barrier for `images` into `layout`: the source is what plume's would be (every stage the textures' last barriers
// named), the destination the stages that touch images. The textures keep plume's bookkeeping: their layout, and ALL as
// the stages of this barrier (so the next barrier waits for everything, as plume's does).
void ImageBarrier(Recorder::Impl& recorder, std::span<Image::Impl* const> images, RenderTextureLayout layout) {
    ::plume::VulkanCommandList* list = recorder.vulkan_list();
    list->endActiveRenderPass();
    const bool geometry = list->queue->device->capabilities.geometryShader;
    VkPipelineStageFlags source = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    std::vector<VkImageMemoryBarrier> barriers;
    barriers.reserve(images.size());
    for (Image::Impl* image : images) {
        ::plume::VulkanTexture* texture = image->vulkan();
        if (texture->textureLayout == RenderTextureLayout::PRESENT) {
            texture->textureLayout = RenderTextureLayout::UNKNOWN;  // (as below)
        }
        VkImageMemoryBarrier b = {};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.image = texture->vk;
        b.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;  // (plume's)
        b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.oldLayout = VkLayoutOf(texture->textureLayout);
        b.newLayout = VkLayoutOf(layout);
        b.subresourceRange.levelCount = texture->desc.mipLevels;
        b.subresourceRange.layerCount = texture->desc.arraySize;
        const bool depth = (texture->desc.flags & ::plume::RenderTextureFlag::DEPTH_TARGET) != 0;
        b.subresourceRange.aspectMask =
            depth ? (VK_IMAGE_ASPECT_DEPTH_BIT | (::plume::RenderFormatIsStencil(texture->desc.format) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0))
                  : VK_IMAGE_ASPECT_COLOR_BIT;
        barriers.push_back(b);
        source |= VkStagesOf(texture->barrierStages, geometry);
        texture->textureLayout = layout;
        texture->barrierStages = ::plume::RenderBarrierStage::ALL;
        image->dirty = false;
    }
    if (barriers.empty()) return;
    vkCmdPipelineBarrier(list->vk, source, kImageStages, 0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(barriers.size()),
                         barriers.data());
}

}  // namespace

void Transition(Recorder::Impl& recorder, Image::Impl& image, RenderTextureLayout layout) {
    if (NarrowImageBarriers()) {
        Tally().transitions.fetch_add(1, std::memory_order_relaxed);
        Image::Impl* one = &image;
        ImageBarrier(recorder, std::span<Image::Impl* const>(&one, 1), layout);
        return;
    }
    ::plume::VulkanTexture* texture = image.vulkan();
    if (texture->textureLayout == RenderTextureLayout::PRESENT) {
        // After an acquire the contents of a presented image are not defined: leave PRESENT_SRC_KHR as UNDEFINED.
        texture->textureLayout = RenderTextureLayout::UNKNOWN;
    }
    ::plume::RenderTextureBarrier barrier(texture, layout);
    Tally().transitions.fetch_add(1, std::memory_order_relaxed);
    // Every stage on both sides: plume's barrier takes its source from the stages of the image's previous
    // barrier, so a full mask here keeps every later barrier a full dependency too.
    recorder.vulkan_list()->barriers(::plume::RenderBarrierStage::ALL, nullptr, 0, &barrier, 1);
    image.dirty = false;
}

void TransitionAll(Recorder::Impl& recorder, std::span<Image::Impl* const> images, RenderTextureLayout layout) {
    if (NarrowImageBarriers()) {
        Tally().transitions.fetch_add(images.size(), std::memory_order_relaxed);
        ImageBarrier(recorder, images, layout);
        return;
    }
    std::vector<::plume::RenderTextureBarrier> barriers;
    barriers.reserve(images.size());
    for (Image::Impl* image : images) {
        ::plume::VulkanTexture* texture = image->vulkan();
        if (texture->textureLayout == RenderTextureLayout::PRESENT) {
            texture->textureLayout = RenderTextureLayout::UNKNOWN;  // (as Transition)
        }
        barriers.emplace_back(texture, layout);
        image->dirty = false;
    }
    if (barriers.empty()) {
        return;
    }
    Tally().transitions.fetch_add(barriers.size(), std::memory_order_relaxed);
    recorder.vulkan_list()->barriers(::plume::RenderBarrierStage::ALL, nullptr, 0, barriers.data(),
                                     static_cast<uint32_t>(barriers.size()));
}

void Normalize(Recorder::Impl& recorder, Image::Impl& image) {
    if (image.vulkan()->textureLayout != RenderTextureLayout::GENERAL || image.dirty) {
        Transition(recorder, image, RenderTextureLayout::GENERAL);
    }
}

void Acquire(Recorder::Impl& recorder, Image::Impl& image) {
    RecorderTargetsData& data = Data(recorder);
    if (!data.hook_registered) {
        data.hook_registered = true;
        RecorderTargetsData* raw = &data;
        Recorder::Impl* self = &recorder;
        // Runs when the recorder is submitted (value > 0, after the command buffer has ended) or dropped (0).
        recorder.on_submit.push_back([raw, self](TimelineValue value) {
            if (value == 0) {
                // A recorder that was never submitted left no barrier on the GPU: the state the images were in
                // when the recorder first touched them is the true one again.
                for (const TouchedImage& t : raw->touched) {
                    ::plume::VulkanTexture* texture = t.image->vulkan();
                    texture->textureLayout = t.layout;
                    texture->barrierStages = t.stages;
                    t.image->dirty = t.dirty;
                }
            }
            for (const TouchedImage& t : raw->touched) {
                if (t.image->open_pass_in == self) t.image->open_pass_in = nullptr;
            }
            if (value > 0 && !raw->staging.empty()) {
                TargetsData& targets = Data(*self->backend);
                std::lock_guard lock(targets.staging_mutex);
                for (std::shared_ptr<Buffer>& buffer : raw->staging) {
                    targets.staging_pending.emplace_back(value, std::move(buffer));
                }
                raw->staging.clear();
            }
        });
    }
    const bool known = std::any_of(data.touched.begin(), data.touched.end(),
                                   [&](const TouchedImage& t) { return t.image == &image; });
    if (!known) {
        ::plume::VulkanTexture* texture = image.vulkan();
        data.touched.push_back(TouchedImage{&image, texture->textureLayout, texture->barrierStages, image.dirty});
    }

    // The owner rule, clause 1 and the receiving half of clause 2.
    std::lock_guard lock(image.owner_mutex);
    if (image.handover_pending) {
        if (recorder.thread == image.handover_thread) {
            image.owner = recorder.thread;
            image.handover_pending = false;
            const Wait wait = image.handover_wait;
            const bool listed = std::any_of(recorder.required_waits.begin(), recorder.required_waits.end(),
                                            [&](const Wait& w) { return w.value == wait.value && w.stage == wait.stage; });
            if (!listed) recorder.required_waits.push_back(wait);
            return;
        }
#if MH4N_RHI_CHECKS
        ReportOwnerRuleBreach(OwnerRule::ForeignThread, "a thread recorded on an image that was handed over to another thread");
#endif
        return;
    }
#if MH4N_RHI_CHECKS
    if (recorder.thread != image.owner) {
        ReportOwnerRuleBreach(OwnerRule::ForeignThread, "a thread recorded on an image it does not own");
    }
#endif
}

void Settle(Recorder::Impl& recorder) {
    RecorderTargetsData& data = Data(recorder);
    // (the prelude never holds a pass: the open one is the main list's, which goes on after EndPrelude; an image
    // operation recorded into the prelude runs before all of it)
    if (!data.pass_open || recorder.in_prelude) return;
    data.pass_open = false;
    if (recorder.in_pass) {
        recorder.vulkan_list()->setFramebuffer(nullptr);  // ends the render pass
        recorder.in_pass = false;
        core::AttachmentlessPassEnded(recorder);  // a pass with no attachments that the caller did not end (N2-12)
    }
    const TargetPass pass = data.pass;
    data.pass = TargetPass{};
    // What the pass wrote becomes visible to everything after it; the depth attachment leaves the attachment layout.
    if (pass.color != nullptr) {
        Transition(recorder, *pass.color, RenderTextureLayout::GENERAL);
        pass.color->open_pass_in = nullptr;
    }
    if (pass.depth != nullptr) {
        Transition(recorder, *pass.depth, RenderTextureLayout::GENERAL);
        pass.depth->open_pass_in = nullptr;
    }
}

} // namespace mh4n::gpu::rhi::targets
