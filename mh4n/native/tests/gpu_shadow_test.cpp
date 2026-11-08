// Derived from Azahar src/video_core/renderer_software/sw_framebuffer.cpp (DrawShadowMapPixel and DecodeD24S8Shadow: the oracle of
// the write), src/video_core/renderer_software/sw_rasterizer.cpp (TextureColor: the shadow compare, the perspective divide of
// Shadow2D, the depth of the lookup and its bias, the texel a coordinate selects, the stencil the shadow pass takes from the green
// of the combiner output, and the depth it writes), src/video_core/renderer_software/sw_clipper.cpp (ConvertCubeCoord: the cube
// face, its coordinates and the depth), src/video_core/shader/generator/glsl_fs_shader_gen.cpp (shadowTexture and
// shadowTextureCube: the four texels and mix2, where the oracle follows the GLSL), src/video_core/pica_types.h (the float24 and
// float16 layouts F24() and F16() encode) and src/video_core/pica/regs_texturing.h, regs_framebuffer.h, regs_rasterizer.h and
// regs_lighting.h (the register positions and fields the states write) at 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6
// (GPL-3.0-or-later: glsl_fs_shader_gen.cpp says "Licensed under GPLv3 or any later version"; the other files say "Licensed under
// GPLv2 or any later version"). The rig (Tri, the ramps, Rig) is adapted from N2-11's procedural texture test in this directory
// (GPL-3.0-or-later, see its header for what it takes from Azahar).
// Copyright 2017 Citra Emulator Project (sw_framebuffer.cpp, regs_lighting.h)
// Copyright 2015-2026 Citra Emulator Project / Azahar Emulator Project (sw_rasterizer.cpp, pica_types.h)
// Copyright 2014-2024 Citra Emulator Project / Azahar Emulator Project (sw_clipper.cpp)
// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project (glsl_fs_shader_gen.cpp)
// Copyright 2017-2024 Citra Emulator Project / Azahar Emulator Project (regs_texturing.h)
// Copyright 2017-2025 Citra Emulator Project / Azahar Emulator Project (regs_framebuffer.h)
// Copyright 2017-2026 Citra Emulator Project / Azahar Emulator Project (regs_rasterizer.h)
//
// Shadows (N2-12), on whatever Vulkan device the machine has (MH4N_VULKAN_DEVICE; a case that needs one skips with a message without
// it and fails under MH4N_REQUIRE_VULKAN=1). Run it twice: as it is (pixel interlock where the device has it) and under
// MH4N_RHI_MASK=interlock (Azahar's compare-and-swap loop, the only path of the Mali GPUs and KosmicKrisp); both must pass. The
// write case prints which path it ran.
//
// THE WRITE. A shadow source (8x8) is filled with known texels, then shadow-write draws of rectangles at constant depths store into
// it; the texels read back must be the ones a CPU port of the software renderer's DrawShadowMapPixel gives, applied draw by draw.
// UpdateShadow does not commute when depth writes (stencil 0) and stencil writes of one pass overlap, and the compare-and-swap loop
// is not in primitive order, so each case puts the depth writes and the stencil writes in different passes (the order of overlapping
// writes inside a pass is then irrelevant: the minimum of the depths, or of the stencils over a fixed depth). The divided stencil is
// a float quotient truncated to a byte on both sides; the GPU's division may land one step lower, so the stencil is allowed 1.
//
// THE LOOKUPS. A target of 8x8 pixels whose TEV stage 0 is texture 0 (Replace) reads the shadow map through the unit's class
// (Shadow2D: the map's R32_UINT view; ShadowCube: six RGBA8 faces in the cube class). The expected value is the software renderer's
// compare (lit when the stored depth is not nearer than the reference) of the four texels around the coordinate, mixed as Azahar's
// GLSL mixes them (the software renderer reads the one texel the coordinate truncates to: the case "at the texel centres" checks that
// the two agree there). A pixel whose reference depth is within 256 of a texel's depth, or whose cube direction is within 1e-3 of a
// face edge, is not compared. Every draw is also rendered by the ubershader and must agree with the specialised shader (one step).
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gpu/cpuvertex/cpu_vertex.h"
#include "gpu/cpuvertex/triangle_batch.h"
#include "gpu/fbops/fixed_function.h"
#include "gpu/fragment/feature.h"
#include "gpu/fragment/generator.h"
#include "gpu/fragment/key.h"
#include "gpu/fragment/profile.h"
#include "gpu/fragment/shadow/shadow.h"
#include "gpu/fragment/state.h"
#include "gpu/fragment/tables.h"
#include "gpu/fragment/uniforms.h"
#include "gpu/native/bindings.h"
#include "gpu/native/compose.h"
#include "gpu/native/default_provider.h"
#include "gpu/native/pipeline_builder.h"
#include "gpu/pica/state.h"
#include "support/vulkan_device.h"

using namespace mh4n;
using namespace mh4n::gpu::fragment;
namespace re = mh4n::gpu::fragment::regenum;
namespace native = mh4n::gpu::native;
namespace cpuvertex = mh4n::gpu::cpuvertex;
namespace fbops = mh4n::gpu::fbops;
namespace pica = mh4n::gpu::pica;
namespace rhi = mh4n::gpu::rhi;
namespace test = mh4n::test;

namespace {

constexpr u32 kSize = 8;      // the targets and the 2D map are 8x8
constexpr u32 kFace = 4;      // the cube faces are 4x4
constexpr u32 kMaxDepth = 0xFFFFFF;

// ---- registers --------------------------------------------------------------------------------------------------------

constexpr u32 kRegDepthRange = 0x4D, kRegDepthNear = 0x4E, kRegDepthmap = 0x6D;
constexpr u32 kRegMainConfig = 0x80, kRegTexture0Size = 0x82, kRegTexture0Params = 0x83, kRegTexture0Address = 0x85;
constexpr u32 kRegCubeAddress = 0x86, kRegShadowTexture = 0x8B, kRegTexture0Format = 0x8E;
constexpr u32 kRegTev0 = 0xC0;
constexpr u32 kRegFragmentOperation = 0x100, kRegLogicOp = 0x102, kRegDepthColorMask = 0x107, kRegAllowColor = 0x113;
constexpr u32 kRegAllowDepth = 0x115, kRegFramebufferShadow = 0x130, kRegLightingDisable = 0x1C6;
constexpr u32 kPrimaryColor = 0, kTexture0 = 3, kPrevious = 15;
constexpr u32 kTypeShadow2D = 2, kTypeShadowCube = 4;

// Float24 (1 sign, 7 exponent, 16 mantissa bits) of a normal float.
u32 F24(float f) {
    if (f == 0.0f) return 0;
    u32 bits;
    std::memcpy(&bits, &f, 4);
    const u32 sign = bits >> 31;
    const int exponent = static_cast<int>((bits >> 23) & 0xFF) - 127 + 63;
    return (sign << 23) | (static_cast<u32>(exponent) << 16) | ((bits >> 7) & 0xFFFF);
}

// Binary16 of a float that has an exact one (the cases use 0.5, 1.0, 0.25, ...).
u32 F16(float f) {
    if (f == 0.0f) return 0;
    u32 bits;
    std::memcpy(&bits, &f, 4);
    const u32 sign = (bits >> 31) << 15;
    const int exponent = static_cast<int>((bits >> 23) & 0xFF) - 127 + 15;
    return sign | (static_cast<u32>(exponent) << 10) | ((bits >> 13) & 0x3FF);
}

// The base state: no lighting, no blending, colour writes on, Z-buffering with a depth scale of -1 (a primitive at PICA z gets the
// depth -z), the other TEV stages pass-through.
std::shared_ptr<pica::PicaState> BaseState() {
    auto state = std::make_shared<pica::PicaState>();
    state->regs.fill(0);
    state->dirty.SetAll();
    for (u32 base : {0xC8u, 0xD0u, 0xD8u, 0xF0u, 0xF8u}) {
        state->regs[base + 0] = 15u | (15u << 4) | (15u << 8) | (15u << 16) | (15u << 20) | (15u << 24);
    }
    state->regs[kRegLogicOp] = static_cast<u32>(re::LogicOp::Copy);
    state->regs[kRegDepthColorMask] = 0xF00;
    state->regs[kRegAllowColor] = 0xF;
    state->regs[kRegAllowDepth] = 0;
    state->regs[kRegLightingDisable] = 1;
    state->regs[kRegDepthRange] = F24(-1.0f);
    state->regs[kRegDepthNear] = 0;
    state->regs[kRegDepthmap] = 1;
    return state;
}

// TEV stage 0 = `source` for colour and alpha, Replace.
void Stage0(pica::PicaState& state, u32 source) {
    state.regs[kRegTev0 + 0] = source | (kPrevious << 4) | (kPrevious << 8) | (source << 16) | (kPrevious << 20) | (kPrevious << 24);
}

// A shadow-write state (fragment operation mode Shadow) whose stencil is the vertex colour's green.
std::shared_ptr<pica::PicaState> WriteState(float constant, float linear) {
    auto state = BaseState();
    state->regs[kRegFragmentOperation] = 3;
    state->regs[kRegFramebufferShadow] = F16(constant) | (F16(linear) << 16);
    Stage0(*state, kPrimaryColor);
    return state;
}

// A lookup state: unit 0 of the type, ClampToEdge, `orthographic`, `bias` (the 23-bit field; the reference depth loses bias << 1).
std::shared_ptr<pica::PicaState> LookupState(u32 type, bool orthographic, u32 bias) {
    auto state = BaseState();
    state->regs[kRegMainConfig] = 1;
    state->regs[kRegTexture0Params] = type << 28;
    state->regs[kRegShadowTexture] = (orthographic ? 1u : 0u) | (bias << 1);
    Stage0(*state, kTexture0);
    return state;
}

// ---- the shadow map and the oracles -------------------------------------------------------------------------------------------

struct Map {
    u32 width = kSize, height = kSize;
    std::vector<u32> texels;  // depth << 8 | stencil, image row by row (row 0 first)
    explicit Map(u32 w = kSize, u32 h = kSize, u32 fill = 0xFFFFFFFFu) : width(w), height(h), texels(static_cast<size_t>(w) * h, fill) {}
    u32& at(u32 x, u32 y) { return texels[static_cast<size_t>(y) * width + x]; }
    u32 at(u32 x, u32 y) const { return texels[static_cast<size_t>(y) * width + x]; }
    // The bytes of the RGBA8 image whose R32_UINT view holds the texels (r = bits 0-7, ..., a = bits 24-31).
    std::vector<u8> Bytes() const {
        std::vector<u8> out(texels.size() * 4);
        for (size_t i = 0; i < texels.size(); ++i) {
            for (u32 k = 0; k < 4; ++k) out[i * 4 + k] = static_cast<u8>(texels[i] >> (8 * k));
        }
        return out;
    }
};

u32 Texel(u32 depth, u32 stencil) {
    return (depth << 8) | stencil;
}

// DrawShadowMapPixel on one texel (the float of x_ and of the quotient is not rounded to binary16: Float<10, 5>::FromFloat32 keeps the
// float as it is).
u32 SwWrite(u32 texel, u32 depth, u32 stencil, float constant, float linear) {
    const u32 ref_z = texel >> 8, ref_s = texel & 0xFF;
    if (depth >= ref_z) return texel;
    if (stencil == 0) return Texel(depth, ref_s);
    const float x = static_cast<float>(depth) / static_cast<float>(ref_z);
    const float quotient = static_cast<float>(stencil) / (constant + linear * x);
    const u32 s = static_cast<u32>(std::clamp(quotient, 0.0f, 255.0f));
    return s < ref_s ? Texel(ref_z, s) : texel;
}

// The compare (TextureColor): the stencil when the stored depth is not nearer than z, else 0.
double Compare(u32 texel, int z, double& margin) {
    const int depth = static_cast<int>(texel >> 8);
    margin = std::min(margin, static_cast<double>(std::abs(depth - z)));
    return depth >= z ? static_cast<double>(texel & 0xFF) / 255.0 : 0.0;
}

// mix2 of Azahar's GLSL: t = mix(s.xy, s.zw, a.y), mix(t.x, t.y, a.x).
double Mix2(double s0, double s1, double s2, double s3, double ax, double ay) {
    const double tx = s0 + (s2 - s0) * ay, ty = s1 + (s3 - s1) * ay;
    return tx + (ty - tx) * ax;
}

// The Shadow2D lookup: the divide, the reference depth, four texels (outside the map: lit), the mix.
double Lookup2D(const Map& map, float u, float v, float w, bool orthographic, int bias, double& margin) {
    if (!orthographic) {
        u /= w;
        v /= w;
    }
    const int z = static_cast<int>(std::min(std::fabs(w), 1.0f) * static_cast<float>(kMaxDepth)) - bias;
    const float cx = static_cast<float>(map.width) * u - 0.5f, cy = static_cast<float>(map.height) * v - 0.5f;
    const float fx = std::floor(cx), fy = std::floor(cy);
    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    auto sample = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= static_cast<int>(map.width) || y >= static_cast<int>(map.height)) return 1.0;
        return Compare(map.at(static_cast<u32>(x), static_cast<u32>(y)), z, margin);
    };
    return Mix2(sample(ix, iy), sample(ix + 1, iy), sample(ix, iy + 1), sample(ix + 1, iy + 1), cx - fx, cy - fy);
}

// The software renderer's own Shadow2D lookup: the one texel the coordinate truncates to (inside the map in the cases that use it).
double SwLookup2D(const Map& map, float u, float v, float w, bool orthographic, int bias) {
    if (!orthographic) {
        u /= w;
        v /= w;
    }
    const int z = static_cast<int>(std::min(std::fabs(w), 1.0f) * static_cast<float>(kMaxDepth)) - bias;
    const int s = static_cast<int>(u * static_cast<float>(map.width)), t = static_cast<int>(v * static_cast<float>(map.height));
    double margin = 1e9;
    return Compare(map.at(static_cast<u32>(s), static_cast<u32>(t)), z, margin);
}

// The ShadowCube lookup: ConvertCubeCoord's face, coordinates and depth, then four texels of the face clamped to it, compared and
// mixed. `margin_face` is how close the direction is to a face edge.
double LookupCube(const std::array<Map, 6>& faces, float u, float v, float w, int bias, double& margin, double& margin_face) {
    const float au = std::fabs(u), av = std::fabs(v), aw = std::fabs(w);
    margin_face = std::min({std::fabs(au - av), std::fabs(au - aw), std::fabs(av - aw)});
    int face;
    float x, y, zc;
    if (au > av && au > aw) {
        face = u > 0 ? 0 : 1;
        y = u > 0 ? -v : v;
        x = -w;
        zc = u;
    } else if (av > aw) {
        face = v > 0 ? 2 : 3;
        x = v > 0 ? u : -u;
        y = w;
        zc = v;
    } else {
        face = w > 0 ? 4 : 5;
        y = w > 0 ? -v : v;
        x = u;
        zc = w;
    }
    const float s = x / zc * 0.5f + 0.5f, t = y / zc * 0.5f + 0.5f;
    const int z = static_cast<int>(std::min(std::fabs(zc), 1.0f) * static_cast<float>(kMaxDepth)) - bias;
    const Map& map = faces[face];
    const float cx = static_cast<float>(map.width) * s - 0.5f, cy = static_cast<float>(map.height) * t - 0.5f;
    const float fx = std::floor(cx), fy = std::floor(cy);
    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    auto sample = [&](int sx, int sy) {
        sx = std::clamp(sx, 0, static_cast<int>(map.width) - 1);
        sy = std::clamp(sy, 0, static_cast<int>(map.height) - 1);
        return Compare(map.at(static_cast<u32>(sx), static_cast<u32>(sy)), z, margin);
    };
    return Mix2(sample(ix, iy), sample(ix + 1, iy), sample(ix, iy + 1), sample(ix + 1, iy + 1), cx - fx, cy - fy);
}

// ---- the draws --------------------------------------------------------------------------------------------------------------

using cpuvertex::OutputVertex;

struct Tri {
    OutputVertex v[3];
};

OutputVertex Vertex(float x, float y, float z) {
    OutputVertex v{};
    v.pos[0] = x;
    v.pos[1] = y;
    v.pos[2] = z;
    v.pos[3] = 1.0f;
    v.quat[3] = 1.0f;
    for (float& channel : v.color) channel = 1.0f;
    v.tc0_w = 1.0f;
    return v;
}

// The pixels [x0, x1) x [y0, y1) of the 8x8 target (image rows; NDC y = -1 is row 0) at `depth`, with the stencil `green` (two
// triangles; their edges lie on pixel edges, away from every pixel centre).
std::vector<Tri> Rect(u32 x0, u32 y0, u32 x1, u32 y1, float depth, u32 green) {
    auto ndc = [](u32 p) { return static_cast<float>(p) * 2.0f / kSize - 1.0f; };
    OutputVertex a = Vertex(ndc(x0), ndc(y0), -depth), b = Vertex(ndc(x1), ndc(y0), -depth), c = Vertex(ndc(x1), ndc(y1), -depth),
                 d = Vertex(ndc(x0), ndc(y1), -depth);
    for (OutputVertex* v : {&a, &b, &c, &d}) {
        v->color[0] = 0.0f;
        v->color[1] = static_cast<float>(green) / 255.0f;
        v->color[2] = 0.0f;
    }
    return {Tri{{a, b, c}}, Tri{{a, c, d}}};
}

// An attribute that is a0 + ax (px + 0.5) / 8 + ay (py + 0.5) / 8 at the centre of pixel (px, py).
struct Ramp {
    double a0 = 0, ax = 0, ay = 0;
    double At(u32 px, u32 py) const { return a0 + ax * (px + 0.5) / kSize + ay * (py + 0.5) / kSize; }
};

// One triangle over the target, vertices (0, 0), (2, 0) and (0, 2) in units of the target, carrying texcoord0 = (u, v) and
// texcoord0_w = w.
std::vector<Tri> RampTriangle(const Ramp& u, const Ramp& v, const Ramp& w) {
    Tri tri{};
    const float pos[3][2] = {{-1, -1}, {3, -1}, {-1, 3}};
    const double f[3][2] = {{0, 0}, {1, 0}, {0, 1}};
    for (int i = 0; i < 3; ++i) {
        OutputVertex& vx = tri.v[i];
        vx = Vertex(pos[i][0], pos[i][1], -0.5f);
        auto value = [&](const Ramp& r) { return static_cast<float>(r.a0 + f[i][0] * 2 * r.ax + f[i][1] * 2 * r.ay); };
        vx.tc0[0] = value(u);
        vx.tc0[1] = value(v);
        vx.tc0_w = value(w);
        vx.tc1[0] = vx.tc1[1] = vx.tc2[0] = vx.tc2[1] = 0.5f;
    }
    return {tri};
}

native::VertexInputKey InputKey() {
    const rhi::VertexLayout layout = cpuvertex::MakeBatchVertexLayout();
    native::VertexInputKey key;
    key.binding_count = static_cast<u8>(layout.bindings.size());
    for (size_t i = 0; i < layout.bindings.size(); ++i) key.bindings[i].stride = layout.bindings[i].stride;
    key.attribute_count = static_cast<u8>(layout.attributes.size());
    for (size_t i = 0; i < layout.attributes.size(); ++i) {
        key.attributes[i] = {static_cast<u8>(layout.attributes[i].location), static_cast<u8>(layout.attributes[i].binding),
                             layout.attributes[i].format, layout.attributes[i].offset};
    }
    return key;
}

struct DrawSpec {
    std::shared_ptr<pica::PicaState> state;
    std::vector<Tri> tris;
    bool new_pass = false;  // end the pass before this draw (a shadow-write pass then begins again)
};

struct Rendered {
    std::vector<u8> color;  // the 8x8 target, RGBA8
    Map map;                // the shadow source after the draws
};

struct Rig {
    std::shared_ptr<rhi::Backend> backend;
    DeviceProfile profile;
    std::unique_ptr<native::PipelineBuilder> builder;
    std::unique_ptr<native::DefaultPipelineProvider> provider;
    std::unique_ptr<native::DrawComposer> composer;
    std::unique_ptr<FragmentUniformRing> uniforms;
    std::unique_ptr<TableRings> tables;
    std::unique_ptr<rhi::Ring> vertex_ring, vs_ring, staging;
    std::shared_ptr<rhi::Image> color, shadow, cube;

    Rig(std::shared_ptr<rhi::Backend> b, bool force_uber) : backend(std::move(b)) {
        profile = ProfileOf(backend->Caps());
        builder = std::make_unique<native::PipelineBuilder>(backend, FeatureRegistry::Global());
        provider = std::make_unique<native::DefaultPipelineProvider>(
            [this](const native::PipelineKey& key) { return builder->Build(key); }, native::DefaultProviderOptions{force_uber});
        auto made = native::DrawComposer::Create(backend, *provider);
        REQUIRE_MESSAGE(made.ok(), made.error());
        composer = std::move(*made);
        auto ring = FragmentUniformRing::Create(*backend);
        REQUIRE_MESSAGE(ring.ok(), ring.error());
        uniforms = std::move(*ring);
        auto table_rings = TableRings::Create(*backend);
        REQUIRE_MESSAGE(table_rings.ok(), table_rings.error());
        tables = std::move(*table_rings);
        auto vr = backend->CreateRing(1 << 20, rhi::BufferUsage::Vertex, "shadow vertices");
        auto sr = backend->CreateRing(1 << 20, rhi::BufferUsage::Uniform, "shadow vs uniforms");
        auto st = backend->CreateRing(1 << 20, rhi::BufferUsage::Transfer, "shadow staging");
        REQUIRE(vr.ok());
        REQUIRE(sr.ok());
        REQUIRE(st.ok());
        vertex_ring = std::move(*vr);
        vs_ring = std::move(*sr);
        staging = std::move(*st);
        rhi::ImageDesc desc;
        desc.format = rhi::Format::Rgba8Unorm;
        desc.width = desc.height = kSize;
        desc.name = "shadow test colour";
        auto c = backend->CreateImage(desc);
        REQUIRE_MESSAGE(c.ok(), c.error());
        color = *c;
        desc.shadow_source = true;
        desc.name = "shadow test map";
        auto s = backend->CreateImage(desc);
        REQUIRE_MESSAGE(s.ok(), s.error());
        shadow = *s;
        desc.shadow_source = false;
        desc.width = desc.height = kFace;
        desc.cube = true;
        desc.layers = 6;
        desc.name = "shadow test cube";
        auto k = backend->CreateImage(desc);
        REQUIRE_MESSAGE(k.ok(), k.error());
        cube = *k;
    }

    void Upload(rhi::Recorder& rec, rhi::Image& image, u32 layer, const Map& map) {
        const std::vector<u8> bytes = map.Bytes();
        auto alloc = staging->Map(bytes.size(), 16);
        REQUIRE_MESSAGE(alloc.ok(), alloc.error());
        std::memcpy(alloc->data.data(), bytes.data(), bytes.size());
        staging->GetBuffer()->Flush(alloc->offset, bytes.size());
        REQUIRE(staging->Commit(bytes.size()).ok());
        rhi::BufferImageRegion region;
        region.buffer_offset = alloc->offset;
        region.layer = layer;
        region.image_rect = rhi::Rect{0, 0, map.width, map.height};
        rec.CopyBufferToImage(*staging->GetBuffer(), image, std::span<const rhi::BufferImageRegion>(&region, 1));
    }

    // Uploads the map (and the cube faces when given), runs the draws in one recorder through one FragmentState, and returns the
    // colour target and the map. A lookup draw reads the map (Shadow2D) or the cube (ShadowCube) as unit 0.
    Rendered Render(const Map& map, const std::vector<DrawSpec>& specs, const std::array<Map, 6>* faces = nullptr) {
        rhi::Recorder rec = backend->BeginRecording();
        composer->BeginRecording(rec);
        uniforms->BeginRecording();
        Upload(rec, *shadow, 0, map);
        if (faces != nullptr) {
            for (u32 f = 0; f < 6; ++f) Upload(rec, *cube, f, (*faces)[f]);
        }
        rhi::ClearValue clear;
        clear.color = {0.0f, 0.0f, 0.0f, 0.0f};
        rec.ClearImage(*color, clear);
        FragmentState fragment(profile, FeatureRegistry::Global());
        bool dynamic_set = false;
        for (const DrawSpec& spec : specs) {
            auto prepared = fragment.Prepare(*spec.state, {}, tables.get());
            REQUIRE_MESSAGE(prepared.ok(), prepared.error());
            auto alloc = uniforms->Write(fragment.block(), prepared->block_changed);
            REQUIRE_MESSAGE(alloc.ok(), alloc.error());

            cpuvertex::TriangleBatch batch;
            for (const Tri& t : spec.tris) batch.AddTriangle(t.v[0], t.v[1], t.v[2]);
            auto streamed = cpuvertex::UploadBatch(*vertex_ring, batch);
            REQUIRE_MESSAGE(streamed.ok(), streamed.error());
            auto vs = vs_ring->Map(2048, std::max<u64>(256, backend->Caps().uniform_buffer_offset_alignment));
            REQUIRE_MESSAGE(vs.ok(), vs.error());
            std::memset(vs->data.data(), 0, 2048);
            REQUIRE(vs_ring->Commit(2048).ok());

            if (spec.new_pass) composer->EndPass(rec);
            if (!dynamic_set) {
                fbops::ApplyDynamicState(rec, fbops::MakeDynamicState(*spec.state), false);
                dynamic_set = true;
            }
            const FragmentKey key = fragment.key();
            native::DrawDesc d;
            d.vertex.kind = native::VertexStageKind::Trivial;
            d.vertex.input = InputKey();
            d.fragment = &key;
            d.needs = fragment.needs();
            d.state = fbops::MakeFixedFunctionState(*spec.state);
            if (d.needs.shadow_write_pass) {
                // as native_gpu.cpp draws it: no attachments, no depth test, no blending
                d.state.depth_test = d.state.depth_write = d.state.stencil_test = false;
                d.state.blend_enabled = d.state.logic_op_enabled = false;
                d.shadow_target = shadow.get();
            } else {
                d.color_format = rhi::Format::Rgba8Unorm;
                d.target.color = color.get();
                d.target.area = rhi::Rect{0, 0, kSize, kSize};
                const auto type = key.texture.texture0_type();
                if (type == re::TextureType::Shadow2D) {
                    d.units[0] = native::TextureBinding{shadow.get(), rhi::ImageView::ShadowStorage, nullptr};
                } else if (type == re::TextureType::ShadowCube) {
                    d.units[0] = native::TextureBinding{cube.get(), rhi::ImageView::Sampled, nullptr};
                }
            }
            d.vs_pica_uniforms = native::UniformRange{vs_ring->GetBuffer().get(), vs->offset, 1616};
            d.vs_uniforms = native::UniformRange{vs_ring->GetBuffer().get(), vs->offset + 1792, 32};
            d.fragment_uniforms = *alloc;
            d.tables = tables.get();
            const rhi::VertexBufferBinding binding{vertex_ring->GetBuffer().get(), streamed->offset};
            d.vertex_buffers = std::span<const rhi::VertexBufferBinding>(&binding, 1);
            d.count = streamed->vertex_count;
            d.viewport = rhi::Viewport{0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0.0f, 1.0f};
            d.scissor = rhi::Rect{0, 0, kSize, kSize};
            auto result = composer->Draw(rec, d);
            REQUIRE_MESSAGE(result.ok(), result.error());
        }
        composer->EndPass(rec);
        backend->WaitIdle(backend->Submit(std::move(rec)));
        Rendered out;
        auto pixels = backend->ReadImage(*color, 0, 0, kSize, kSize);
        REQUIRE_MESSAGE(pixels.ok(), pixels.error());
        out.color = *pixels;
        auto texels = backend->ReadImage(*shadow, 0, 0, kSize, kSize);
        REQUIRE_MESSAGE(texels.ok(), texels.error());
        for (size_t i = 0; i < out.map.texels.size(); ++i) {
            u32 t = 0;
            for (u32 k = 0; k < 4; ++k) t |= static_cast<u32>((*texels)[i * 4 + k]) << (8 * k);
            out.map.texels[i] = t;
        }
        return out;
    }
};

// The texels a list of rectangles leaves, applied in order on the CPU: {x0, y0, x1, y1, depth, green}.
struct RectSpec {
    u32 x0, y0, x1, y1;
    float depth;
    u32 green;
};
void ApplyRects(Map& map, const std::vector<RectSpec>& rects, float constant, float linear) {
    for (const RectSpec& r : rects) {
        const u32 d = static_cast<u32>(r.depth * static_cast<float>(kMaxDepth));
        for (u32 y = r.y0; y < r.y1; ++y) {
            for (u32 x = r.x0; x < r.x1; ++x) map.at(x, y) = SwWrite(map.at(x, y), d, r.green, constant, linear);
        }
    }
}
std::vector<Tri> TrisOf(const std::vector<RectSpec>& rects) {
    std::vector<Tri> out;
    for (const RectSpec& r : rects) {
        const std::vector<Tri> t = Rect(r.x0, r.y0, r.x1, r.y1, r.depth, r.green);
        out.insert(out.end(), t.begin(), t.end());
    }
    return out;
}

void CheckMap(const Map& got, const Map& want, const std::string& name) {
    int depth_off = 0, stencil_off = 0;
    std::string where;
    for (u32 y = 0; y < kSize; ++y) {
        for (u32 x = 0; x < kSize; ++x) {
            const u32 g = got.at(x, y), w = want.at(x, y);
            const int dd = std::abs(static_cast<int>(g >> 8) - static_cast<int>(w >> 8));
            const int ds = std::abs(static_cast<int>(g & 0xFF) - static_cast<int>(w & 0xFF));
            if (dd > depth_off || ds > stencil_off) {
                char text[128];
                std::snprintf(text, sizeof(text), "texel (%u,%u) is %08x, the software renderer says %08x", x, y, g, w);
                where = text;
            }
            depth_off = std::max(depth_off, dd);
            stencil_off = std::max(stencil_off, ds);
        }
    }
    CHECK_MESSAGE(depth_off == 0, name << ": " << where);
    CHECK_MESSAGE(stencil_off <= 1, name << ": " << where);
}

// Compares the lookup target with `want` (a value in [0, 1] per pixel, or a negative value: not compared) within one step, and the
// two modes with each other.
void CheckPixels(const std::vector<u8>& a, const std::vector<u8>& b, const std::vector<double>& want, const std::string& name,
                 int min_compared = 40) {
    int compared = 0, worst_a = 0, worst_b = 0, worst_ab = 0;
    std::string where_a, where_b;
    for (u32 i = 0; i < kSize * kSize; ++i) {
        for (int k = 0; k < 4; ++k) worst_ab = std::max(worst_ab, std::abs(int(a[i * 4 + k]) - int(b[i * 4 + k])));
        if (want[i] < 0) continue;
        ++compared;
        const double expected = std::round(want[i] * 255.0);
        for (int k = 0; k < 4; ++k) {
            const int da = static_cast<int>(std::fabs(a[i * 4 + k] - expected));
            const int db = static_cast<int>(std::fabs(b[i * 4 + k] - expected));
            if (da > worst_a) {
                worst_a = da;
                where_a = "pixel (" + std::to_string(i % kSize) + "," + std::to_string(i / kSize) + ") is " + std::to_string(a[i * 4 + k]) +
                          ", the oracle says " + std::to_string(expected);
            }
            if (db > worst_b) {
                worst_b = db;
                where_b = "pixel (" + std::to_string(i % kSize) + "," + std::to_string(i / kSize) + ") is " + std::to_string(b[i * 4 + k]) +
                          ", the oracle says " + std::to_string(expected);
            }
        }
    }
    CHECK_MESSAGE(compared >= min_compared, name << ": only " << compared << " pixels compared");
    CHECK_MESSAGE(worst_a <= 1, name << " [specialised]: " << where_a);
    CHECK_MESSAGE(worst_b <= 1, name << " [ubershader]: " << where_b);
    CHECK_MESSAGE(worst_ab <= 1, name << ": the specialised shader and the ubershader differ by " << worst_ab);
}

// A map with a pattern of depths and stencils: depth 0.3 (near) on a checkerboard of 2x2 blocks, 0.7 (far) elsewhere, the stencils
// 255, 200, 128 and 60 by quadrant, and two white (cleared) texels.
Map PatternMap() {
    Map map;
    const u32 stencils[4] = {255, 200, 128, 60};
    for (u32 y = 0; y < kSize; ++y) {
        for (u32 x = 0; x < kSize; ++x) {
            const bool near = ((x / 2) + (y / 2)) % 2 == 0;
            const float depth = near ? 0.3f : 0.7f;
            map.at(x, y) = Texel(static_cast<u32>(depth * kMaxDepth), stencils[(y / 4) * 2 + x / 4]);
        }
    }
    map.at(1, 6) = 0xFFFFFFFFu;
    map.at(6, 1) = 0xFFFFFFFFu;
    return map;
}

struct LookupCase {
    std::string name;
    Ramp u, v, w;
    bool orthographic = true;
    u32 bias = 0;
};

void CheckLookup2D(Rig& spec_rig, Rig& uber_rig, const Map& map, const LookupCase& c, bool software = false) {
    INFO(c.name);
    const DrawSpec draw{LookupState(kTypeShadow2D, c.orthographic, c.bias), RampTriangle(c.u, c.v, c.w)};
    const Rendered a = spec_rig.Render(map, {draw});
    const Rendered b = uber_rig.Render(map, {draw});
    std::vector<double> want(kSize * kSize, -1.0);
    for (u32 py = 0; py < kSize; ++py) {
        for (u32 px = 0; px < kSize; ++px) {
            const float u = static_cast<float>(c.u.At(px, py)), v = static_cast<float>(c.v.At(px, py)), w = static_cast<float>(c.w.At(px, py));
            double margin = 1e9;
            const double value = software ? SwLookup2D(map, u, v, w, c.orthographic, static_cast<int>(c.bias << 1))
                                          : Lookup2D(map, u, v, w, c.orthographic, static_cast<int>(c.bias << 1), margin);
            if (margin >= 256) want[py * kSize + px] = value;
        }
    }
    CheckPixels(a.color, b.color, want, c.name);
}

}  // namespace

// ---- without a device ---------------------------------------------------------------------------------------------------------

TEST_CASE("shadow/the_feature_is_no_stub_and_fills_its_needs_and_uniforms") {
    const FeatureModule* feature = FeatureRegistry::Global().Find("shadow");
    REQUIRE(feature != nullptr);
    FragmentKey write = MakeKey(*WriteState(0.5f, 1.0f));
    CHECK(write.framebuffer.shadow_rendering() == 1);
    CHECK(feature->Stubbed(write) == nullptr);
    CHECK(feature->NeedsOf(write, DeviceProfile{}).shadow_write_pass);
    FragmentKey lookup = MakeKey(*LookupState(kTypeShadow2D, true, 0));
    CHECK(feature->Stubbed(lookup) == nullptr);
    CHECK_FALSE(feature->NeedsOf(lookup, DeviceProfile{}).shadow_write_pass);

    // SyncDrawUniforms: the two float16 of 0x130 and the texture bias << 1 of 0x8B
    auto state = WriteState(0.5f, -2.0f);
    state->regs[kRegShadowTexture] = 1u | (0x123456u << 1) | 0xFF000000u;  // the high byte is not part of the field
    FragmentState fragment(DeviceProfile{});
    auto prepared = fragment.Prepare(*state, {}, nullptr);
    REQUIRE(prepared.ok());
    const FragmentUniforms& u = fragment.block().uniforms;
    CHECK(u.shadow_bias_constant == 0.5f);
    CHECK(u.shadow_bias_linear == -2.0f);
    CHECK(u.shadow_texture_bias == static_cast<int>((0x123456u & 0x7FFFFFu) << 1));
}

TEST_CASE("shadow/the_shadow_texture_of_unit_0_and_its_cube_faces_from_the_registers") {
    auto state = LookupState(kTypeShadow2D, true, 0);
    state->regs[kRegTexture0Size] = (128u << 16) | 64u;
    state->regs[kRegTexture0Address] = 0x18154F00u / 8;
    state->regs[kRegTexture0Format] = 0;
    auto shadow2d = shadow::ShadowTextureOf(state->regs.data());
    REQUIRE(shadow2d.has_value());
    CHECK_FALSE(shadow2d->cube);
    CHECK(shadow2d->width == 128);
    CHECK(shadow2d->height == 64);
    CHECK(shadow2d->paddr[0] == 0x18154F00u);

    state->regs[kRegTexture0Params] = kTypeShadowCube << 28;
    for (u32 i = 0; i < 5; ++i) state->regs[kRegCubeAddress + i] = 0x1000u * (i + 1);
    auto cube = shadow::ShadowTextureOf(state->regs.data());
    REQUIRE(cube.has_value());
    CHECK(cube->cube);
    for (u32 face = 1; face < 6; ++face) {
        CHECK(cube->paddr[face] == (((0x18154F00u / 8) & ~0x3FFFFFu) | (0x1000u * face)) * 8);
    }
    // not a shadow type, unit 0 off, address 0: none
    state->regs[kRegTexture0Params] = 0;
    CHECK_FALSE(shadow::ShadowTextureOf(state->regs.data()).has_value());
    state->regs[kRegTexture0Params] = kTypeShadow2D << 28;
    state->regs[kRegMainConfig] = 0;
    CHECK_FALSE(shadow::ShadowTextureOf(state->regs.data()).has_value());
    state->regs[kRegMainConfig] = 1;
    state->regs[kRegTexture0Address] = 0;
    CHECK_FALSE(shadow::ShadowTextureOf(state->regs.data()).has_value());
}

TEST_CASE("shadow/the_shaders_compile_in_both_forms_and_both_write_paths") {
    DeviceProfile interlock, compswap;
    interlock.interlock = true;
    const FragmentKey write = MakeKey(*WriteState(0.5f, 1.0f));
    for (const DeviceProfile* profile : {&interlock, &compswap}) {
        const Generated g = GenerateSpecialised(write, *profile, ShaderPass::ShadowWrite);
        CHECK_MESSAGE(g.ok(), g.error);
        const bool has_interlock = g.glsl.find("beginInvocationInterlock();") != std::string::npos;
        const bool has_loop = g.glsl.find("imageAtomicCompSwap") != std::string::npos;
        CHECK(has_interlock == profile->interlock);
        CHECK(has_loop == !profile->interlock);
        CHECK(g.glsl.find("vec4 shadow_tev_output = combiner_output;") != std::string::npos);
        const Generated uw = GenerateUber(ShaderPass::ShadowWrite, *profile);
        CHECK_MESSAGE(uw.ok(), uw.error);
        CHECK((uw.glsl.find("beginInvocationInterlock();") != std::string::npos) == profile->interlock);
        const Generated uc = GenerateUber(ShaderPass::Normal, *profile);
        CHECK_MESSAGE(uc.ok(), uc.error);
        CHECK(uc.glsl.find("imageAtomicCompSwap") == std::string::npos);  // the normal pass stores nothing
    }
    // the lookups: the divide only where the unit is not orthographic; a key without a shadow type defines them as lit
    const std::string ortho = GenerateSpecialisedGlsl(MakeKey(*LookupState(kTypeShadow2D, true, 0)), compswap, ShaderPass::Normal);
    const std::string persp = GenerateSpecialisedGlsl(MakeKey(*LookupState(kTypeShadow2D, false, 0)), compswap, ShaderPass::Normal);
    const std::string cube = GenerateSpecialisedGlsl(MakeKey(*LookupState(kTypeShadowCube, true, 0)), compswap, ShaderPass::Normal);
    const std::string none = GenerateSpecialisedGlsl(MakeKey(*BaseState()), compswap, ShaderPass::Normal);
    CHECK(ortho.find("uv /= w;") == std::string::npos);
    CHECK(persp.find("uv /= w;") != std::string::npos);
    CHECK(ortho.find("vec4 shadowTextureCube(vec2 uv, float w) { return vec4(1.0); }") != std::string::npos);
    CHECK(cube.find("ShadowCubeTexel") != std::string::npos);
    CHECK(cube.find("vec4 shadowTexture(vec2 uv, float w) { return vec4(1.0); }") != std::string::npos);
    CHECK(none.find("CompareShadow") == std::string::npos);
    for (const std::string* glsl : {&ortho, &persp, &cube, &none}) {
        std::vector<u32> spirv;
        std::string error;
        CompileFragmentGlslToSpirv(*glsl, spirv, error, nullptr);
        CHECK_MESSAGE(!spirv.empty(), error);
    }
    // a shadow-write pass with lighting on (the lighting term's locals and the write tail's share main)
    auto lit = WriteState(0.5f, 1.0f);
    lit->regs[kRegLightingDisable] = 0;
    const Generated g = GenerateSpecialised(MakeKey(*lit), compswap, ShaderPass::ShadowWrite);
    CHECK_MESSAGE(g.ok(), g.error);
}

// ---- on a device --------------------------------------------------------------------------------------------------------------

TEST_CASE("shadow/the_write_pass_keeps_the_nearest_depth_then_lowers_the_stencil") {
    auto backend = test::RequireBackend();
    if (!backend) return;
    Rig spec_rig(backend, false), uber_rig(backend, true);
    std::printf("shadow write path: %s\n", spec_rig.profile.interlock ? "pixel interlock" : "compare-and-swap loop (no interlock)");
    const float constant = 0.5f, linear = 1.0f;

    // Pass 1: depth writes (stencil 0), overlapping, in two draws; the map starts cleared (white) but for a far column.
    Map start;
    for (u32 y = 0; y < kSize; ++y) start.at(7, y) = Texel(static_cast<u32>(0.875f * kMaxDepth), 90);
    const std::vector<RectSpec> depth_a = {{0, 0, 8, 8, 0.75f, 0}, {1, 1, 6, 5, 0.5f, 0}, {3, 2, 8, 7, 0.625f, 0}};
    const std::vector<RectSpec> depth_b = {{2, 3, 5, 8, 0.25f, 0}, {0, 0, 3, 3, 0.875f, 0}};
    // Pass 2: stencil writes over the depths pass 1 left: nearer ones divide the stencil, farther ones change nothing, the lowest of
    // the overlapping ones stays.
    const std::vector<RectSpec> stencil_a = {{0, 0, 4, 4, 0.375f, 200}, {2, 2, 8, 6, 0.125f, 240}, {4, 0, 8, 3, 0.9375f, 10}};
    const std::vector<RectSpec> stencil_b = {{1, 5, 7, 8, 0.1875f, 120}, {0, 2, 3, 8, 0.0625f, 250}};

    Map want = start;
    ApplyRects(want, depth_a, constant, linear);
    ApplyRects(want, depth_b, constant, linear);
    const Map after_depth = want;
    ApplyRects(want, stencil_a, constant, linear);
    ApplyRects(want, stencil_b, constant, linear);

    auto state = WriteState(constant, linear);
    const std::vector<DrawSpec> depth_only = {{state, TrisOf(depth_a)}, {state, TrisOf(depth_b)}};
    std::vector<DrawSpec> both = depth_only;
    both.push_back({state, TrisOf(stencil_a), true});
    both.push_back({state, TrisOf(stencil_b)});
    for (Rig* rig : {&spec_rig, &uber_rig}) {
        const std::string mode = rig == &spec_rig ? " [specialised]" : " [ubershader]";
        CheckMap(rig->Render(start, depth_only).map, after_depth, "the depth pass" + mode);
        CheckMap(rig->Render(start, both).map, want, "the depth pass, then the stencil pass" + mode);
    }
    // the cases above reach every branch of DrawShadowMapPixel
    int divided = 0, kept = 0;
    for (u32 i = 0; i < want.texels.size(); ++i) {
        if ((want.texels[i] & 0xFF) != (after_depth.texels[i] & 0xFF)) ++divided;
        else ++kept;
    }
    CHECK(divided >= 10);
    CHECK(kept >= 10);
}

TEST_CASE("shadow/a_stencil_quotient_that_is_negative_or_too_large_is_clamped") {
    auto backend = test::RequireBackend();
    if (!backend) return;
    Rig rig(backend, false);
    for (const auto& [constant, linear, name] : {std::tuple{-1.0f, 0.5f, "a negative divisor: 0"}, std::tuple{0.25f, 0.0f, "a quotient above 255: no change"},
                                                std::tuple{2.0f, -1.0f, "a negative linear term"}}) {
        INFO(name);
        Map start(kSize, kSize, Texel(static_cast<u32>(0.75f * kMaxDepth), 250));
        const std::vector<RectSpec> rects = {{0, 0, 8, 8, 0.5f, 100}};
        Map want = start;
        ApplyRects(want, rects, constant, linear);
        CheckMap(rig.Render(start, {{WriteState(constant, linear), TrisOf(rects)}}).map, want, name);
    }
}

TEST_CASE("shadow/the_2d_lookup_compares_four_texels_and_mixes_them") {
    auto backend = test::RequireBackend();
    if (!backend) return;
    Rig spec_rig(backend, false), uber_rig(backend, true);
    const Map map = PatternMap();
    // z = 0.5: the near texels (0.3) shadow, the far ones (0.7) give their stencil
    CheckLookup2D(spec_rig, uber_rig, map, {"orthographic, inside the map", {0.03, 0.94, 0.0}, {0.02, 0.0, 0.95}, {0.5, 0.0, 0.0}});
    CheckLookup2D(spec_rig, uber_rig, map, {"orthographic, past the edges: lit", {-0.3, 1.6, 0.0}, {-0.25, 0.1, 1.5}, {0.5, 0.0, 0.0}});
    CheckLookup2D(spec_rig, uber_rig, map, {"orthographic, diagonal", {0.1, 0.5, 0.35}, {0.05, 0.3, 0.6}, {0.5, 0.0, 0.0}});
    // z from |w|: a negative w is taken as its absolute value; past 1 it is clamped (every texel of the map is nearer)
    CheckLookup2D(spec_rig, uber_rig, map, {"orthographic, w negative", {0.03, 0.94, 0.0}, {0.02, 0.0, 0.95}, {-0.5, 0.0, 0.0}});
    CheckLookup2D(spec_rig, uber_rig, map, {"orthographic, w past 1", {0.03, 0.94, 0.0}, {0.02, 0.0, 0.95}, {1.0, 0.6, 0.0}});
    // z = 0.2 + w ramp: the reference crosses the near texels' depth inside the draw
    CheckLookup2D(spec_rig, uber_rig, map, {"orthographic, z crossing 0.3", {0.03, 0.94, 0.0}, {0.02, 0.0, 0.95}, {0.2, 0.0, 0.25}});
    // the bias moves z back past the near texels: 0.5 - 0.25 = 0.25 < 0.3, so every texel is lit by its stencil
    CheckLookup2D(spec_rig, uber_rig, map,
                  {"orthographic, a bias of 0.25", {0.03, 0.94, 0.0}, {0.02, 0.0, 0.95}, {0.5, 0.0, 0.0}, true, (kMaxDepth / 4) >> 1});
}

TEST_CASE("shadow/the_2d_lookup_divides_by_w_unless_the_unit_is_orthographic") {
    auto backend = test::RequireBackend();
    if (!backend) return;
    Rig spec_rig(backend, false), uber_rig(backend, true);
    const Map map = PatternMap();
    // uv = (u, v) / w with w from 0.45 to 0.9: the coordinates are given times w
    LookupCase c{"perspective", {0.02, 0.5, 0.1}, {0.03, 0.1, 0.55}, {0.45, 0.25, 0.2}, false};
    CheckLookup2D(spec_rig, uber_rig, map, c);
    LookupCase o = c;
    o.name = "the same coordinates, orthographic: no divide";
    o.orthographic = true;
    CheckLookup2D(spec_rig, uber_rig, map, o);
}

TEST_CASE("shadow/at_the_texel_centres_the_2d_lookup_is_the_software_renderers_one_texel") {
    auto backend = test::RequireBackend();
    if (!backend) return;
    Rig spec_rig(backend, false), uber_rig(backend, true);
    const Map map = PatternMap();
    // uv = ((px + 0.5) / 8, (py + 0.5) / 8): each pixel is at the centre of its texel
    CheckLookup2D(spec_rig, uber_rig, map, {"texel centres, z 0.5", {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, {0.5, 0.0, 0.0}}, true);
    CheckLookup2D(spec_rig, uber_rig, map, {"texel centres, z 0.8", {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, {0.8, 0.0, 0.0}}, true);
}

TEST_CASE("shadow/the_cube_lookup_takes_the_software_renderers_face_and_coordinates") {
    auto backend = test::RequireBackend();
    if (!backend) return;
    Rig spec_rig(backend, false), uber_rig(backend, true);
    // six faces with different patterns: depth 0.6 or 0.9 on a per-face checker, stencils by face and texel
    std::array<Map, 6> faces;
    for (u32 f = 0; f < 6; ++f) {
        faces[f] = Map(kFace, kFace);
        for (u32 y = 0; y < kFace; ++y) {
            for (u32 x = 0; x < kFace; ++x) {
                const bool near = (x + y + f) % 2 == 0;
                faces[f].at(x, y) = Texel(static_cast<u32>((near ? 0.6f : 0.9f) * kMaxDepth), 40 + 30 * f + 5 * x + 2 * y);
            }
        }
    }
    const Map map;  // the 2D map is not read
    // the major axis 0.75 on each face's axis, the two others ramps over (-0.6, 0.6): z = 0.75 lies between the two depths
    const Ramp major{0.75, 0, 0}, minor{0.75 * -1, 0, 0};
    const Ramp across{-0.6, 1.2, 0.0}, down{-0.55, 0.0, 1.1}, slant{-0.5, 0.6, 0.45};
    struct Face {
        const char* name;
        Ramp u, v, w;
    };
    const Face cases[] = {
        {"+X", major, across, down}, {"-X", minor, down, across}, {"+Y", across, major, down},
        {"-Y", down, minor, across}, {"+Z", across, down, major},  {"-Z", slant, across, minor},
    };
    for (const Face& c : cases) {
        INFO(c.name);
        const DrawSpec draw{LookupState(kTypeShadowCube, true, 0), RampTriangle(c.u, c.v, c.w)};
        const Rendered a = spec_rig.Render(map, {draw}, &faces);
        const Rendered b = uber_rig.Render(map, {draw}, &faces);
        std::vector<double> want(kSize * kSize, -1.0);
        for (u32 py = 0; py < kSize; ++py) {
            for (u32 px = 0; px < kSize; ++px) {
                double margin = 1e9, margin_face = 1e9;
                const double value = LookupCube(faces, static_cast<float>(c.u.At(px, py)), static_cast<float>(c.v.At(px, py)),
                                                static_cast<float>(c.w.At(px, py)), 0, margin, margin_face);
                if (margin >= 256 && margin_face >= 1e-3) want[py * kSize + px] = value;
            }
        }
        CheckPixels(a.color, b.color, want, std::string("cube face ") + c.name);
    }
}

TEST_CASE("shadow/a_write_pass_then_a_lookup_of_the_map_in_one_recording") {
    auto backend = test::RequireBackend();
    if (!backend) return;
    Rig spec_rig(backend, false), uber_rig(backend, true);
    const float constant = 1.0f, linear = 0.5f;
    const Map start;  // cleared: white
    const std::vector<RectSpec> depths = {{0, 0, 5, 4, 0.25f, 0}, {3, 2, 8, 8, 0.375f, 0}};
    const std::vector<RectSpec> stencils = {{0, 0, 8, 3, 0.125f, 180}, {4, 4, 8, 8, 0.0625f, 90}};
    Map map = start;
    ApplyRects(map, depths, constant, linear);
    ApplyRects(map, stencils, constant, linear);
    auto write = WriteState(constant, linear);
    const LookupCase c{"end to end, z 0.3", {0.02, 0.96, 0.0}, {0.03, 0.0, 0.94}, {0.3, 0.0, 0.0}};
    const std::vector<DrawSpec> draws = {{write, TrisOf(depths)},
                                         {write, TrisOf(stencils), true},
                                         {LookupState(kTypeShadow2D, true, 0), RampTriangle(c.u, c.v, c.w)}};
    const Rendered a = spec_rig.Render(start, draws);
    const Rendered b = uber_rig.Render(start, draws);
    CheckMap(a.map, map, "the map the lookup read [specialised]");
    CheckMap(b.map, map, "the map the lookup read [ubershader]");
    // the lookup from the map the GPU wrote (its stencils may be one step off the CPU's)
    std::vector<double> want(kSize * kSize, -1.0);
    for (u32 py = 0; py < kSize; ++py) {
        for (u32 px = 0; px < kSize; ++px) {
            double margin = 1e9;
            const double value = Lookup2D(a.map, static_cast<float>(c.u.At(px, py)), static_cast<float>(c.v.At(px, py)),
                                          static_cast<float>(c.w.At(px, py)), true, 0, margin);
            if (margin >= 256) want[py * kSize + px] = value;
        }
    }
    CheckPixels(a.color, b.color, want, c.name);
}
