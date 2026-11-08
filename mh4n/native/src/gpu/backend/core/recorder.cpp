// The recorder: commands on the calling thread's command list (N2-26).
#include <algorithm>

#include <fmt/format.h>

#include "gpu/backend/core/impl.h"
#include "mh4n/assert.h"

// Preconditions of the recording calls (a pass is open or not, a capability is on): checked with the
// backend's other checks, MH4N_RHI_CHECKS.
#if MH4N_RHI_CHECKS
#define RHI_REQUIRE(cond, ...) MH4N_ASSERT(cond, __VA_ARGS__)
#else
#define RHI_REQUIRE(cond, ...)                                                                                         \
    do {                                                                                                               \
    } while (0)
#endif

namespace mh4n::gpu::rhi {

namespace core {

::plume::RenderBarrierStages ToPlumeStages(Stage stage) {
    switch (stage) {
    case Stage::ComputeShader: return ::plume::RenderBarrierStage::COMPUTE;
    case Stage::Transfer: return ::plume::RenderBarrierStage::COPY;
    case Stage::VertexShader:
    case Stage::FragmentShader:
    case Stage::AllGraphics: break;
    }
    return ::plume::RenderBarrierStage::GRAPHICS;
}

::plume::RenderFramebuffer* ZeroAttachmentFramebuffer(Backend::Impl& backend, u32 width, u32 height) {
    std::lock_guard lock(backend.framebuffer_mutex);
    auto it = backend.framebuffers.find({width, height});
    if (it == backend.framebuffers.end()) {
        ::plume::RenderFramebufferDesc desc;
        desc.width = width;
        desc.height = height;
        std::unique_ptr<::plume::RenderFramebuffer> framebuffer = backend.device_owner->createFramebuffer(desc);
        if (framebuffer == nullptr ||
            static_cast<::plume::VulkanFramebuffer*>(framebuffer.get())->vk == VK_NULL_HANDLE) {
            return nullptr;
        }
        it = backend.framebuffers.emplace(std::make_pair(width, height), std::move(framebuffer)).first;
    }
    return it->second.get();
}

} // namespace core

namespace {

// The pipeline stage and the accesses of a Stage, for Recorder::Barrier.
struct StageMasks {
    VkPipelineStageFlags stages = 0;
    VkAccessFlags writes = 0;
    VkAccessFlags reads = 0;
};

StageMasks MasksOf(Stage stage) {
    constexpr VkAccessFlags kShaderRead = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT;
    StageMasks masks;
    switch (stage) {
    case Stage::VertexShader:
        masks.stages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
        masks.writes = VK_ACCESS_SHADER_WRITE_BIT;
        masks.reads = kShaderRead | VK_ACCESS_SHADER_WRITE_BIT;
        break;
    case Stage::FragmentShader:
        masks.stages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        masks.writes = VK_ACCESS_SHADER_WRITE_BIT;
        masks.reads = kShaderRead | VK_ACCESS_SHADER_WRITE_BIT;
        break;
    case Stage::ComputeShader:
        masks.stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        masks.writes = VK_ACCESS_SHADER_WRITE_BIT;
        masks.reads = kShaderRead | VK_ACCESS_SHADER_WRITE_BIT;
        break;
    case Stage::Transfer:
        masks.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT;
        masks.writes = VK_ACCESS_TRANSFER_WRITE_BIT;
        masks.reads = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT;
        break;
    case Stage::AllGraphics:
        masks.stages = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
        masks.writes = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        masks.reads = kShaderRead | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
                      VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        break;
    }
    return masks;
}

// The memory dependency on each side of a pass with no attachments (N2-12). Its draws store into storage images (the shadow
// write), which no layout transition of the targets module covers and which the validation layer does not see between commands
// of one command buffer (rhi-spike.md 6.9): what came before the pass (a load, a lookup that reads the map) is ordered before its
// stores, and its stores are visible to what follows (a lookup, a write-back), whatever binding set is reused. Outside a render
// pass: before BeginPass sets the framebuffer, after the pass has ended.
// Every stage that can touch such an image takes part: the fragment stage (the stores, the lookups), the tests and colour output
// (an image drawn into as a target), compute (the readback), transfer (copies, clears, uploads) and the host. The vertex stages
// never read or write one, so they are left out: the vertex work around the pass need not wait for its fragment work (on a
// tiler, the next passes' vertex and tiling work runs beside it).
void AttachmentlessPassBarrier(Recorder::Impl& r) {
    constexpr VkPipelineStageFlags kStages =
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT;
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(r.vulkan_list()->vk, kStages, kStages, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

} // namespace

void core::AttachmentlessPassEnded(Recorder::Impl& r) {
    if (r.attachmentless_pass) {
        r.attachmentless_pass = false;
        AttachmentlessPassBarrier(r);
    }
}

// ---- The class -----------------------------------------------------------------------------------

Recorder::Recorder(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Recorder::Recorder(Recorder&&) noexcept = default;
Recorder& Recorder::operator=(Recorder&&) noexcept = default;
Recorder::~Recorder() = default;

std::thread::id Recorder::Thread() const {
    return impl_->thread;
}

Recorder::Impl::~Impl() {
    if (backend == nullptr) {
        return;
    }
    // Dropped without a submission: the space and the sets it held are free at once.
    for (auto& hook : on_submit) {
        hook(0);
    }
    on_submit.clear();
    targets.reset();
    state.reset();
    if (counted) {
        std::lock_guard lock(backend->thread_mutex);
        ThreadState& state = backend->threads[thread];
        if (state.open_count > 0) {
            --state.open_count;
        }
        if (state.open == this) {
            state.open = nullptr;
        }
    }
    if (list != nullptr) {
        vulkan_list()->end(); // ends a pass the recorder left open; the list can be begun again
        std::lock_guard lock(backend->list_mutex);
        backend->free_lists.push_back(std::move(list));
    }
    if (other != nullptr) {  // the prelude, or the main list when dropped inside the prelude
        static_cast<::plume::VulkanCommandList*>(other.get())->end();
        std::lock_guard lock(backend->list_mutex);
        backend->free_lists.push_back(std::move(other));
    }
}

void Recorder::BeginPrelude() {
    Impl& r = *impl_;
    MH4N_ASSERT(!r.in_prelude, "BeginPrelude inside the prelude");
    if (!r.has_prelude) {
        r.other = core::TakeCommandList(*r.backend);
        r.other_state = Impl::ListState{};
        r.has_prelude = true;
    }
    r.SwapLists();
    r.in_prelude = true;
}

void Recorder::EndPrelude() {
    Impl& r = *impl_;
    MH4N_ASSERT(r.in_prelude, "EndPrelude without BeginPrelude");
    MH4N_ASSERT(!r.in_pass, "a pass in the prelude");
    r.SwapLists();
    r.in_prelude = false;
}

// ---- Commands ------------------------------------------------------------------------------------

void Recorder::SetPipeline(const Pipeline& pipeline) {
    Impl& r = *impl_;
    const Pipeline::Impl& p = pipeline.impl();
    RHI_REQUIRE(!p.compute || !r.in_pass, "a compute pipeline cannot be set inside a pass");
    if (!p.compute && r.pipeline == &p) {
        return;  // bound already (a pipeline stays bound in the command buffer across passes)
    }
    if (p.compute || r.pipeline == nullptr || r.pipeline->compute || !(r.pipeline->sets == p.sets)) {
        r.bound_sets = {};  // (sets are kept by a pipeline with the same set shapes only)
    }
    r.vulkan_list()->setPipeline(p.pipeline.get());
    if (p.compute) {
        r.vulkan_list()->setComputePipelineLayout(p.layout.get());
    } else {
        r.vulkan_list()->setGraphicsPipelineLayout(p.layout.get());
    }
    r.pipeline = &p;
}

void Recorder::Bind(const BindingSet& set, u32 index) {
    Impl& r = *impl_;
    RHI_REQUIRE(r.pipeline != nullptr, "Bind before SetPipeline");
    RHI_REQUIRE(index < r.pipeline->sets.size() && r.pipeline->sets[index] == set.Desc(),
                "the binding set's shape differs from the pipeline's set {}", index);
    ::plume::RenderDescriptorSet* plume_set = set.impl().set.get();
    if (r.pipeline->compute) {
        r.vulkan_list()->setComputeDescriptorSet(plume_set, index);
    } else {
        const VkDescriptorSet vk_set = static_cast<::plume::VulkanDescriptorSet*>(plume_set)->vk;
        if (index < r.bound_sets.size()) {
            Impl::BoundSet& bound = r.bound_sets[index];
            if (bound.set == vk_set && bound.offset_count == 0) {
                return;
            }
            bound = Impl::BoundSet{vk_set, {}, 0};
        }
        r.vulkan_list()->setGraphicsDescriptorSet(plume_set, index);
    }
}

void Recorder::Bind(const BindingSet& set, u32 index, std::span<const u32> dynamic_offsets) {
    Impl& r = *impl_;
    RHI_REQUIRE(r.pipeline != nullptr, "Bind before SetPipeline");
    RHI_REQUIRE(index < r.pipeline->sets.size() && r.pipeline->sets[index] == set.Desc(),
                "the binding set's shape differs from the pipeline's set {}", index);
    RHI_REQUIRE(static_cast<size_t>(std::count_if(set.Desc().slots.begin(), set.Desc().slots.end(),
                                                  [](const BindingSlot& s) {
                                                      return s.type == BindingType::UniformBufferDynamic;
                                                  })) == dynamic_offsets.size(),
                "one dynamic offset per UniformBufferDynamic slot");
    ::plume::VulkanCommandList* list = r.vulkan_list();
    const ::plume::VulkanPipelineLayout* layout =
        r.pipeline->compute ? list->activeComputePipelineLayout : list->activeGraphicsPipelineLayout;
    const VkDescriptorSet vk_set = static_cast<::plume::VulkanDescriptorSet*>(set.impl().set.get())->vk;
    if (!r.pipeline->compute && index < r.bound_sets.size() && dynamic_offsets.size() <= 4) {
        Impl::BoundSet& bound = r.bound_sets[index];
        if (bound.set == vk_set && bound.offset_count == dynamic_offsets.size() &&
            std::equal(dynamic_offsets.begin(), dynamic_offsets.end(), bound.offsets.begin())) {
            return;
        }
        bound.set = vk_set;
        bound.offset_count = static_cast<u32>(dynamic_offsets.size());
        std::copy(dynamic_offsets.begin(), dynamic_offsets.end(), bound.offsets.begin());
    }
    vkCmdBindDescriptorSets(list->vk, r.pipeline->compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
                            layout->vk, index, 1, &vk_set, static_cast<uint32_t>(dynamic_offsets.size()),
                            dynamic_offsets.data());
}

void Recorder::SetVertexBuffers(u32 first_binding, std::span<const VertexBufferBinding> buffers) {
    RHI_REQUIRE(first_binding + buffers.size() <= kMaxVertexBindings, "at most {} vertex bindings", kMaxVertexBindings);
    thread_local std::vector<::plume::RenderVertexBufferView> views;
    thread_local std::vector<::plume::RenderInputSlot> slots;
    views.clear();
    slots.clear();
    for (size_t i = 0; i < buffers.size(); ++i) {
        const VertexBufferBinding& binding = buffers[i];
        RHI_REQUIRE(binding.buffer != nullptr && HasUsage(binding.buffer->Usage(), BufferUsage::Vertex),
                    "a vertex buffer needs Vertex usage");
        const u64 size = binding.buffer->Size() - std::min(binding.offset, binding.buffer->Size());
        views.emplace_back(::plume::RenderBufferReference(binding.buffer->impl().buffer.get(), binding.offset),
                           static_cast<uint32_t>(size));
        slots.emplace_back(static_cast<uint32_t>(first_binding + i), 0u);
    }
    impl_->vulkan_list()->setVertexBuffers(first_binding, views.data(), static_cast<uint32_t>(views.size()),
                                           slots.data());
}

void Recorder::SetIndexBuffer(const Buffer& buffer, u64 offset, IndexType type) {
    RHI_REQUIRE(HasUsage(buffer.Usage(), BufferUsage::Index), "an index buffer needs Index usage");
    RHI_REQUIRE(type != IndexType::U8 || impl_->backend->caps.index_uint8,
                "8-bit indices need Capabilities::index_uint8: widen them");
    ::plume::RenderFormat format = ::plume::RenderFormat::R16_UINT;
    if (type == IndexType::U8) format = ::plume::RenderFormat::R8_UINT;
    if (type == IndexType::U32) format = ::plume::RenderFormat::R32_UINT;
    const ::plume::RenderIndexBufferView view(::plume::RenderBufferReference(buffer.impl().buffer.get(), offset),
                                              static_cast<uint32_t>(buffer.Size() - std::min(offset, buffer.Size())),
                                              format);
    impl_->vulkan_list()->setIndexBuffer(&view);
}

void Recorder::SetViewport(const Viewport& v) {
    Impl& r = *impl_;
    if (r.viewport_known && r.viewport.x == v.x && r.viewport.y == v.y && r.viewport.width == v.width &&
        r.viewport.height == v.height && r.viewport.min_depth == v.min_depth && r.viewport.max_depth == v.max_depth) {
        return;
    }
    r.viewport_known = true;
    r.viewport = v;
    const ::plume::RenderViewport viewport(v.x, v.y, v.width, v.height, v.min_depth, v.max_depth);
    impl_->vulkan_list()->setViewports(&viewport, 1);
}

void Recorder::SetScissor(const Rect& r) {
    Impl& self = *impl_;
    if (self.scissor_known && self.scissor.x == r.x && self.scissor.y == r.y && self.scissor.width == r.width &&
        self.scissor.height == r.height) {
        return;
    }
    self.scissor_known = true;
    self.scissor = r;
    const ::plume::RenderRect scissor(r.x, r.y, r.x + static_cast<s32>(r.width), r.y + static_cast<s32>(r.height));
    impl_->vulkan_list()->setScissors(&scissor, 1);
}

// MH4N_FRAG_STATS (DrawQueries, rhi.h): the draw inside a pipeline statistics query, its pass begun first (a query
// begun in a pass ends in it).
static u32 BeginDrawQuery(::plume::VulkanCommandList& list) {
    const DrawQueryHook& hook = DrawQueries();
    if (hook.pool == 0 || hook.next == nullptr) {
        return UINT32_MAX;
    }
    const u32 index = hook.next(hook.user);
    if (index == UINT32_MAX) {
        return index;
    }
    list.checkActiveRenderPass();
    vkCmdBeginQuery(list.vk, reinterpret_cast<VkQueryPool>(hook.pool), index, 0);
    return index;
}

static void EndDrawQuery(::plume::VulkanCommandList& list, u32 index) {
    if (index != UINT32_MAX) {
        vkCmdEndQuery(list.vk, reinterpret_cast<VkQueryPool>(DrawQueries().pool), index);
    }
}

void Recorder::Draw(u32 vertex_count, u32 instance_count, u32 first_vertex, u32 first_instance) {
    RHI_REQUIRE(impl_->in_pass, "Draw outside a pass");
    if (impl_->vulkan_list()->activeRenderPass == VK_NULL_HANDLE) Tally().render_passes.fetch_add(1, std::memory_order_relaxed);
    const u32 query = BeginDrawQuery(*impl_->vulkan_list());
    impl_->vulkan_list()->drawInstanced(vertex_count, instance_count, first_vertex, first_instance);
    EndDrawQuery(*impl_->vulkan_list(), query);
}

void Recorder::DrawIndexed(u32 index_count, u32 instance_count, u32 first_index, s32 vertex_offset,
                           u32 first_instance) {
    RHI_REQUIRE(impl_->in_pass, "DrawIndexed outside a pass");
    if (impl_->vulkan_list()->activeRenderPass == VK_NULL_HANDLE) Tally().render_passes.fetch_add(1, std::memory_order_relaxed);
    const u32 query = BeginDrawQuery(*impl_->vulkan_list());
    impl_->vulkan_list()->drawIndexedInstanced(index_count, instance_count, first_index, vertex_offset, first_instance);
    EndDrawQuery(*impl_->vulkan_list(), query);
}

void Recorder::Dispatch(u32 x, u32 y, u32 z) {
    RHI_REQUIRE(!impl_->in_pass, "Dispatch inside a pass");
    impl_->vulkan_list()->dispatch(x, y, z);
}

void Recorder::CopyBuffer(const Buffer& dst, u64 dst_offset, const Buffer& src, u64 src_offset, u64 size) {
    RHI_REQUIRE(!impl_->in_pass, "CopyBuffer inside a pass");
    RHI_REQUIRE(HasUsage(dst.Usage(), BufferUsage::Transfer) && HasUsage(src.Usage(), BufferUsage::Transfer),
                "CopyBuffer needs Transfer usage on both buffers");
    RHI_REQUIRE(dst.GetHeap() != Heap::Upload && src.GetHeap() != Heap::Readback,
                "a copy cannot write the Upload heap or read the Readback heap");
    RHI_REQUIRE(dst_offset + size <= dst.Size() && src_offset + size <= src.Size(), "CopyBuffer out of range");
    impl_->vulkan_list()->copyBufferRegion(dst.impl().buffer->at(dst_offset), src.impl().buffer->at(src_offset), size);
}

void Recorder::Barrier(Stage from, Stage to) {
    RHI_REQUIRE(!impl_->in_pass, "Barrier inside a pass");
    const StageMasks source = MasksOf(from);
    const StageMasks target = MasksOf(to);
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = source.writes;
    barrier.dstAccessMask = target.reads | target.writes;
    vkCmdPipelineBarrier(impl_->vulkan_list()->vk, source.stages, target.stages, 0, 1, &barrier, 0, nullptr, 0,
                         nullptr);
}

void Recorder::BeginPass(const PassDesc& pass) {
    Impl& r = *impl_;
    RHI_REQUIRE(!r.in_pass, "BeginPass inside a pass");
    RHI_REQUIRE(pass.width > 0 && pass.height > 0, "a pass needs a size");
    ::plume::RenderFramebuffer* framebuffer = core::ZeroAttachmentFramebuffer(*r.backend, pass.width, pass.height);
    MH4N_ASSERT(framebuffer != nullptr, "the framebuffer of a {}x{} pass with no attachments could not be made",
                pass.width, pass.height);
    r.vulkan_list()->setFramebuffer(nullptr);  // no render pass of plume's is active while the barrier is recorded
    AttachmentlessPassBarrier(r);
    r.attachmentless_pass = true;
    r.vulkan_list()->setFramebuffer(framebuffer);
    const ::plume::RenderViewport viewport(0.0f, 0.0f, static_cast<float>(pass.width), static_cast<float>(pass.height));
    const ::plume::RenderRect scissor(0, 0, static_cast<s32>(pass.width), static_cast<s32>(pass.height));
    r.vulkan_list()->setViewports(&viewport, 1);
    r.vulkan_list()->setScissors(&scissor, 1);
    r.viewport_known = r.scissor_known = false;
    r.in_pass = true;
}

void Recorder::EndPass() {
    Impl& r = *impl_;
    RHI_REQUIRE(r.in_pass, "EndPass outside a pass");
    r.vulkan_list()->setFramebuffer(nullptr); // ends the pass the first draw began
    r.in_pass = false;
    core::AttachmentlessPassEnded(r);
}

} // namespace mh4n::gpu::rhi
