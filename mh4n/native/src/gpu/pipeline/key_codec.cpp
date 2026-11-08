// Written for N2-16: nothing is copied from Azahar here.
#include "gpu/pipeline/key_codec.h"

#include <bit>
#include <cstring>

namespace mh4n::gpu::pipeline {
namespace {

using native::PipelineKey;

struct Writer {
    std::vector<u8>& out;
    void U8(u64 v) { out.push_back(static_cast<u8>(v)); }
    void U32(u32 v) {
        for (u32 i = 0; i < 4; ++i) out.push_back(static_cast<u8>(v >> (8 * i)));
    }
    void U64(u64 v) {
        U32(static_cast<u32>(v));
        U32(static_cast<u32>(v >> 32));
    }
};

struct Reader {
    std::span<const u8> bytes;
    size_t at = 0;
    bool ok = true;
    u8 U8(u32 max) {
        if (at >= bytes.size()) {
            ok = false;
            return 0;
        }
        const u8 v = bytes[at++];
        if (v > max) ok = false;
        return v;
    }
    u32 U32() {
        if (at + 4 > bytes.size()) {
            ok = false;
            return 0;
        }
        u32 v = 0;
        for (u32 i = 0; i < 4; ++i) v |= static_cast<u32>(bytes[at + i]) << (8 * i);
        at += 4;
        return v;
    }
    u64 U64() {
        const u64 lo = U32();
        return lo | (static_cast<u64>(U32()) << 32);
    }
};

// The largest value of each enumeration (a value above it is not a key of this build).
constexpr u32 kMaxFormat = 127;
constexpr u32 kMaxBlendFactor = static_cast<u32>(rhi::BlendFactor::SrcAlphaSaturate);
constexpr u32 kMaxBlendOp = static_cast<u32>(rhi::BlendOp::Max);
constexpr u32 kMaxLogicOp = static_cast<u32>(rhi::LogicOp::OrInverted);
constexpr u32 kMaxCompareOp = static_cast<u32>(rhi::CompareOp::Always);
constexpr u32 kMaxStencilOp = static_cast<u32>(rhi::StencilOp::DecrementWrap);
constexpr u32 kMaxCullMode = static_cast<u32>(rhi::CullMode::Back);
constexpr u32 kMaxFrontFace = static_cast<u32>(rhi::FrontFace::CounterClockwise);
constexpr u32 kMaxTopology = static_cast<u32>(rhi::Topology::TriangleFan);

void PutFace(Writer& w, const rhi::StencilFaceState& f) {
    w.U8(static_cast<u64>(f.fail));
    w.U8(static_cast<u64>(f.pass));
    w.U8(static_cast<u64>(f.depth_fail));
    w.U8(static_cast<u64>(f.compare));
}

rhi::StencilFaceState GetFace(Reader& r) {
    rhi::StencilFaceState f;
    f.fail = static_cast<rhi::StencilOp>(r.U8(kMaxStencilOp));
    f.pass = static_cast<rhi::StencilOp>(r.U8(kMaxStencilOp));
    f.depth_fail = static_cast<rhi::StencilOp>(r.U8(kMaxStencilOp));
    f.compare = static_cast<rhi::CompareOp>(r.U8(kMaxCompareOp));
    return f;
}

}  // namespace

void EncodeKey(const PipelineKey& key, std::vector<u8>& out) {
    Writer w{out};
    w.U8(static_cast<u64>(key.pass));
    w.U8(static_cast<u64>(key.vertex.kind));
    w.U64(key.vertex.program);
    const native::VertexInputKey& input = key.vertex.input;
    w.U8(input.binding_count);
    w.U8(input.attribute_count);
    for (u32 i = 0; i < input.binding_count && i < input.bindings.size(); ++i) {
        w.U32(input.bindings[i].stride);
        w.U8(input.bindings[i].per_instance);
    }
    for (u32 i = 0; i < input.attribute_count && i < input.attributes.size(); ++i) {
        const native::VertexInputKey::Attribute& a = input.attributes[i];
        w.U8(a.location);
        w.U8(a.binding);
        w.U8(static_cast<u64>(a.format));
        w.U32(a.offset);
    }
    w.U8(static_cast<u64>(key.fixup));
    w.U8(static_cast<u64>(key.fragment.kind));
    w.U8(static_cast<u64>(key.fragment.uber));
    if (key.fragment.kind == native::FragmentKind::Specialised) {
        const size_t at = out.size();
        out.resize(at + sizeof(fragment::FragmentKey));
        std::memcpy(out.data() + at, &key.fragment.key, sizeof(fragment::FragmentKey));
    }
    const rhi::FixedFunctionState& s = key.state;
    w.U8(s.blend_enabled);
    w.U8(static_cast<u64>(s.src_color));
    w.U8(static_cast<u64>(s.dst_color));
    w.U8(static_cast<u64>(s.color_op));
    w.U8(static_cast<u64>(s.src_alpha));
    w.U8(static_cast<u64>(s.dst_alpha));
    w.U8(static_cast<u64>(s.alpha_op));
    w.U8(s.color_write_mask);
    w.U8(s.logic_op_enabled);
    w.U8(static_cast<u64>(s.logic_op));
    w.U8(s.depth_test);
    w.U8(s.depth_write);
    w.U8(static_cast<u64>(s.depth_compare));
    w.U8(s.stencil_test);
    PutFace(w, s.stencil_front);
    PutFace(w, s.stencil_back);
    w.U8(static_cast<u64>(s.cull));
    w.U8(static_cast<u64>(s.front_face));
    w.U8(s.depth_bias_enabled);
    w.U32(std::bit_cast<u32>(s.depth_bias_constant));
    w.U32(std::bit_cast<u32>(s.depth_bias_slope));
    w.U8(static_cast<u64>(key.topology));
    w.U8(static_cast<u64>(key.color_format));
    w.U8(static_cast<u64>(key.depth_format));
}

std::vector<u8> EncodeKey(const PipelineKey& key) {
    std::vector<u8> out;
    out.reserve(80 + sizeof(fragment::FragmentKey));
    EncodeKey(key, out);
    return out;
}

std::optional<PipelineKey> DecodeKey(std::span<const u8> bytes) {
    Reader r{bytes};
    PipelineKey key;
    key.pass = static_cast<native::PassKind>(r.U8(1));
    key.vertex.kind = static_cast<native::VertexStageKind>(r.U8(2));
    key.vertex.program = r.U64();
    native::VertexInputKey& input = key.vertex.input;
    input.binding_count = r.U8(rhi::kMaxVertexBindings);
    input.attribute_count = r.U8(rhi::kMaxVertexAttributes);
    if (!r.ok) {
        return std::nullopt;
    }
    for (u32 i = 0; i < input.binding_count; ++i) {
        input.bindings[i].stride = r.U32();
        input.bindings[i].per_instance = r.U8(1);
    }
    for (u32 i = 0; i < input.attribute_count; ++i) {
        native::VertexInputKey::Attribute& a = input.attributes[i];
        a.location = r.U8(255);
        a.binding = r.U8(255);
        a.format = static_cast<rhi::Format>(r.U8(kMaxFormat));
        a.offset = r.U32();
    }
    key.fixup = static_cast<native::FixupStage>(r.U8(1));
    key.fragment.kind = static_cast<native::FragmentKind>(r.U8(1));
    key.fragment.uber = static_cast<native::UberModule>(r.U8(1));
    if (key.fragment.kind == native::FragmentKind::Specialised) {
        if (r.at + sizeof(fragment::FragmentKey) > bytes.size()) {
            return std::nullopt;
        }
        std::memcpy(&key.fragment.key, bytes.data() + r.at, sizeof(fragment::FragmentKey));
        r.at += sizeof(fragment::FragmentKey);
    }
    rhi::FixedFunctionState& s = key.state;
    s.blend_enabled = r.U8(1) != 0;
    s.src_color = static_cast<rhi::BlendFactor>(r.U8(kMaxBlendFactor));
    s.dst_color = static_cast<rhi::BlendFactor>(r.U8(kMaxBlendFactor));
    s.color_op = static_cast<rhi::BlendOp>(r.U8(kMaxBlendOp));
    s.src_alpha = static_cast<rhi::BlendFactor>(r.U8(kMaxBlendFactor));
    s.dst_alpha = static_cast<rhi::BlendFactor>(r.U8(kMaxBlendFactor));
    s.alpha_op = static_cast<rhi::BlendOp>(r.U8(kMaxBlendOp));
    s.color_write_mask = r.U8(255);
    s.logic_op_enabled = r.U8(1) != 0;
    s.logic_op = static_cast<rhi::LogicOp>(r.U8(kMaxLogicOp));
    s.depth_test = r.U8(1) != 0;
    s.depth_write = r.U8(1) != 0;
    s.depth_compare = static_cast<rhi::CompareOp>(r.U8(kMaxCompareOp));
    s.stencil_test = r.U8(1) != 0;
    s.stencil_front = GetFace(r);
    s.stencil_back = GetFace(r);
    s.cull = static_cast<rhi::CullMode>(r.U8(kMaxCullMode));
    s.front_face = static_cast<rhi::FrontFace>(r.U8(kMaxFrontFace));
    s.depth_bias_enabled = r.U8(1) != 0;
    s.depth_bias_constant = std::bit_cast<float>(r.U32());
    s.depth_bias_slope = std::bit_cast<float>(r.U32());
    key.topology = static_cast<rhi::Topology>(r.U8(kMaxTopology));
    key.color_format = static_cast<rhi::Format>(r.U8(kMaxFormat));
    key.depth_format = static_cast<rhi::Format>(r.U8(kMaxFormat));
    if (!r.ok || r.at != bytes.size()) {
        return std::nullopt;
    }
    return key;
}

}  // namespace mh4n::gpu::pipeline
