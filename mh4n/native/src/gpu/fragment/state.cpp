// The per-draw driver of the fragment state (N2-33). Written for this package: nothing is copied from Azahar.
#include "gpu/fragment/state.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "gpu/pica/state.h"

namespace mh4n::gpu::fragment {
namespace {
// IsFragmentKeyRegister as one bit a register, in the dirty set's words (Prepare tests them a word at a time)
constexpr std::array<u64, 12> kKeyRegisterBits = [] {
    std::array<u64, 12> bits{};
    for (u32 id = freg::kViewportDepthRange; id < 0x1E0; ++id) {
        if (IsFragmentKeyRegister(id)) bits[id >> 6] |= 1ull << (id & 63);
    }
    return bits;
}();
static_assert(sizeof(pica::DirtyRegs::qwords) == sizeof(kKeyRegisterBits));
}  // namespace

FragmentState::FragmentState(const DeviceProfile& profile) : FragmentState(profile, FeatureRegistry::Global()) {}

FragmentState::FragmentState(const DeviceProfile& profile, const FeatureRegistry& features)
    : profile_(profile), features_(features) {
    std::memset(&block_, 0, sizeof(block_));
}

rhi::Result<FragmentState::Prepared> FragmentState::Prepare(const pica::PicaState& state, const UniformContext& context,
                                                             TableRings* rings) {
    Prepared out;
    ++draws_;

    // The key: re-extracted only when a register of the fragment blocks is dirty (or on the first draw).
    bool extract = first_;
    if (!extract) {
        for (size_t w = 0; w < kKeyRegisterBits.size() && !extract; ++w) {
            extract = (state.dirty.qwords[w] & kKeyRegisterBits[w]) != 0;
        }
    }
    UniformDirty dirty;
    dirty.all = first_;  // the first block holds nothing: write every member whatever the dirty bits say
    if (extract) {
        ++key_extractions_;
        const FragmentKey key = MakeKey(state, features_);
        if (StoreKey(key, block_)) {
            out.key_changed = true;
            dirty.Mark();
            needs_ = NeedsOf(key, profile_, features_);
        } else if (first_) {
            needs_ = NeedsOf(key, profile_, features_);
        }
    }

    FillUniforms(state, block_, dirty, context, features_);

    if (rings != nullptr) {
        rhi::Result<UploadSummary> uploaded = UploadTables(state, *rings, block_, dirty, features_);
        if (!uploaded.ok()) {
            return rhi::Error{uploaded.error()};
        }
        out.tables_uploaded = uploaded->uploaded;
    }

    out.block_changed = dirty.changed || first_;
    first_ = false;
    return out;
}

// ---- FragmentUniformRing -------------------------------------------------------------------------------

FragmentUniformRing::~FragmentUniformRing() = default;

rhi::Result<std::unique_ptr<FragmentUniformRing>> FragmentUniformRing::Create(rhi::Backend& backend, u64 ring_bytes) {
    std::unique_ptr<FragmentUniformRing> self(new FragmentUniformRing());
    rhi::Result<std::unique_ptr<rhi::Ring>> ring = backend.CreateRing(ring_bytes, rhi::BufferUsage::Uniform, "fragment uniforms");
    if (!ring.ok()) {
        return rhi::Error{ring.error()};
    }
    self->ring_ = std::move(*ring);
    self->alignment_ = std::max<u64>(kFragmentBlockAlignment, backend.Caps().uniform_buffer_offset_alignment);
    return self;
}

rhi::Result<FragmentUniformRing::Allocation> FragmentUniformRing::Write(const FragmentBlock& block, bool changed) {
    if (!changed && have_last_) {
        ++reuses_;
        Allocation again = last_;
        again.reused = true;
        return again;
    }
    const u64 size = (sizeof(FragmentBlock) + alignment_ - 1) / alignment_ * alignment_;
    rhi::Result<rhi::RingAllocation> alloc = ring_->Map(size, alignment_);
    if (!alloc.ok()) {
        return rhi::Error{alloc.error()};
    }
    // (the block, then zeros to the allocation's end: not zeros under the block first)
    std::memcpy(alloc->data.data(), &block, sizeof(FragmentBlock));
    if (alloc->data.size() > sizeof(FragmentBlock)) {
        std::memset(alloc->data.data() + sizeof(FragmentBlock), 0, alloc->data.size() - sizeof(FragmentBlock));
    }
    rhi::Result<void> committed = ring_->Commit(size);
    if (!committed.ok()) {
        return rhi::Error{committed.error()};
    }
    last_ = Allocation{ring_->GetBuffer().get(), alloc->offset, kFragmentBlockBytes, false};
    have_last_ = true;
    ++allocations_;
    return last_;
}

} // namespace mh4n::gpu::fragment
