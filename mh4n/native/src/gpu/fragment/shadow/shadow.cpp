// Derived from Azahar src/video_core/shader/generator/glsl_fs_shader_gen.cpp (DefineShadowHelpers: EncodeShadow, UpdateShadow,
// CompareShadow, mix2, SampleShadow2D, shadowTexture, the texel arithmetic of shadowTextureCube; WriteShadow: the write tail, with
// pixel interlock or the imageAtomicCompSwap loop), src/video_core/renderer_software/sw_framebuffer.cpp (DrawShadowMapPixel and the
// shadow map's texel layout), src/video_core/renderer_software/sw_rasterizer.cpp (TextureColor: the shadow compare and the
// stencil the shadow pass takes from the combiner output), src/video_core/renderer_software/sw_clipper.cpp (ConvertCubeCoord: the
// cube face, its coordinates and the depth), src/video_core/rasterizer_accelerated.cpp and src/video_core/pica/dirty_regs.h
// (SyncDrawUniforms and CheckShadow: the three shadow uniforms and the registers that dirty them), src/video_core/pica_types.h
// (Float<10, 5>::FromRaw: the float16 of the bias registers) and src/video_core/pica/regs_texturing.h and regs_framebuffer.h (the
// shadow registers, the unit-0 registers) at 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-3.0-or-later: glsl_fs_shader_gen.cpp
// says "Licensed under GPLv3 or any later version"; sw_framebuffer.cpp, sw_rasterizer.cpp, sw_clipper.cpp,
// rasterizer_accelerated.cpp, dirty_regs.h, pica_types.h, regs_texturing.h and regs_framebuffer.h say "Licensed under GPLv2 or any
// later version"). The direction that reads one texel of a cube face at its centre, and the bytes put back together from it, are
// ours (the Vulkan cube-face table).
// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project (glsl_fs_shader_gen.cpp, rasterizer_accelerated.cpp)
// Copyright 2017 Citra Emulator Project (sw_framebuffer.cpp)
// Copyright 2015-2026 Citra Emulator Project / Azahar Emulator Project (sw_rasterizer.cpp, pica_types.h)
// Copyright 2014-2024 Citra Emulator Project / Azahar Emulator Project (sw_clipper.cpp)
// Copyright 2025-2026 Citra Emulator Project / Azahar Emulator Project (dirty_regs.h)
// Copyright 2017-2024 Citra Emulator Project / Azahar Emulator Project (regs_texturing.h)
// Copyright 2017-2025 Citra Emulator Project / Azahar Emulator Project (regs_framebuffer.h)
//
// The `shadow/` feature (N2-12): the shadow-write pass and the shadow lookups, in both modes of the emitter
// (docs/native/gpu-fragment-design.md 5.1).
//
// THE SHADOW MAP. A 32-bit texel holds a 24-bit depth and an 8-bit intensity (Azahar calls it the stencil): read through the
// shadow source's R32_UINT view it is `depth << 8 | stencil`, which is the software renderer's layout in guest memory once the
// RGBA8 target is written back (bytes 0-2 the depth, high byte first, byte 3 the stencil: DecodeD24S8Shadow).
//
// THE WRITE (fragment operation mode Shadow, the pass kind ShadowWrite: no attachments, the shadow source's R32_UINT view bound
// as `shadow_buffer`, set 2). At the Output point the tail packs `d = depth * 0xFFFFFF` and `s` (the green byte of the combiner
// output) and stores `UpdateShadow(old, d, s)`: DrawShadowMapPixel's rule, a nearer depth with s = 0 replaces the depth, a nearer
// one with s > 0 lowers the stencil to s / (constant + linear * d / ref_depth). Where the device has pixel interlock
// (`DeviceProfile::interlock`) the load and the store sit between beginInvocationInterlock and endInvocationInterlock, which
// glslang declares pixel_interlock_ordered; else Azahar's imageAtomicCompSwap loop, atomic but not in primitive order (the Mali-G52
// and G57 and KosmicKrisp have no interlock). UpdateShadow does not commute when depth writes and stencil writes of one pass
// overlap: whether the loop differs visibly from the interlock path on a game scene is `unverified` (rhi-spike.md 5.2).
//
// THE LOOKUPS. `shadowTexture` (Shadow2D) is Azahar's Vulkan text: the perspective divide unless the unit is orthographic, the
// reference depth `min(|w|, 1) * 0xFFFFFF - bias`, four texels around the coordinate fetched from `tex0_shadow` with texelFetch,
// each compared, and the four results mixed by hand (a texel outside the map is lit, 1.0); no compare sampler, no border colour.
// `shadowTextureCube` (ShadowCube) follows the software renderer, not Azahar's Vulkan code (which binds one image where six faces
// are written): ConvertCubeCoord picks the face, its coordinates and the depth, and the same four-texel compare and mix runs on
// that face, the texels clamped to it as Azahar's text clamps them. The faces are one RGBA8 cube image in the class `tex0_cube`;
// a texel is read with textureLod at the direction of its centre (exact with the nearest sampler) and its bytes give the texel
// back as the R32_UINT view would (`r | g << 8 | b << 16 | a << 24`).
//
// What differs from Azahar's text, on purpose, to give the software renderer's values (the oracle of this phase):
//   - the compare is `ref >= z` (lit when the stored depth is not nearer), the software renderer's `z_ref >= z_int` with z a
//     signed value; Azahar's `p.x <= z` shadows the texel whose depth equals z, and its `max(0, ...)` clamps z at 0;
//   - the stencil of the write is the combiner output's green byte before fog (sw_rasterizer.cpp writes the shadow before fog),
//     rounded, where Azahar truncates `combiner_output.g * 255` after its fog; the divided stencil is clamped to [0, 255] before
//     it is truncated (DrawShadowMapPixel's clamp: a negative quotient gives 0, which Azahar's uint() of a negative float leaves
//     undefined), and `linear * (d / ref)` is grouped as the software renderer groups it;
//   - the alpha test still discards in the shadow pass (core emits it before the Output point), as Azahar's text does; the software
//     renderer writes the shadow before its alpha test. The census's shadow passes have the alpha test off.
// The key is complete (N2-33): this feature reads `shadow_rendering`, `shadow_texture_orthographic` and the texture-0 type from it,
// writes `shadow_bias_constant`, `shadow_bias_linear` and `shadow_texture_bias`, and sets `Needs::shadow_write_pass` (which core
// sets too). `shadow.h` is the host side: what the renderer binds for the two lookups.
#include "gpu/fragment/shadow/shadow.h"

#include <bit>
#include <cstring>

#include "gpu/fragment/emitter.h"
#include "gpu/fragment/feature.h"
#include "gpu/fragment/regs.h"
#include "gpu/pica/state.h"

namespace mh4n::gpu::fragment {
namespace {

namespace kw = keyword;

// TextureConfig::TextureType.
constexpr u32 kShadow2D = 2, kShadowCube = 4;

// The unit-0 registers this file reads besides freg's (regs_texturing.h).
constexpr u32 kTexture0Size = freg::kTexture0 + 1;     // height 0-10, width 16-26
constexpr u32 kTexture0Address = freg::kTexture0 + 4;  // bits 0-27, in units of 8 bytes
constexpr u32 kCubeAddress = 0x86;                     // five words: the faces -X, +Y, -Y, +Z, -Z (bits 0-21)
constexpr u32 kTexture0Format = 0x8E;                  // bits 0-3

// ---- the GLSL -----------------------------------------------------------------------------------------------------------------

// DefineShadowHelpers, the write half (DecodeShadow is core's helper). The quotient is clamped and grouped as DrawShadowMapPixel
// computes it.
constexpr const char* kWriteHelpers = R"(
uint EncodeShadow(uvec2 pixel) {
    return (pixel.x << 8) | pixel.y;
}

uint UpdateShadow(uint pixel, uint d, uint s) {
    uvec2 ref = DecodeShadow(pixel);
    if (d < ref.x) {
        if (s == 0u) {
            ref.x = d;
        } else {
            s = uint(clamp(float(s) / (shadow_bias_constant + shadow_bias_linear * (float(d) / float(ref.x))), 0.0, 255.0));
            ref.y = min(s, ref.y);
        }
    }
    return EncodeShadow(ref);
}
)";

// DefineShadowHelpers, the lookup half: the compare of the software renderer and Azahar's mix.
constexpr const char* kCompareHelpers = R"(
float CompareShadow(uint pixel, int z) {
    uvec2 p = DecodeShadow(pixel);
    return int(p.x) >= z ? float(p.y) * (1.0 / 255.0) : 0.0;
}

float mix2(vec4 s, vec2 a) {
    vec2 t = mix(s.xy, s.zw, a.yy);
    return mix(t.x, t.y, a.x);
}
)";

constexpr const char* kSample2D = R"(
float SampleShadow2D(ivec2 uv, int z) {
    if (any(bvec4(lessThan(uv, ivec2(0)), greaterThanEqual(uv, textureSize(tex0_shadow, 0)))))
        return 1.0;
    return CompareShadow(texelFetch(tex0_shadow, uv, 0).x, z);
}
)";

// The body of shadowTexture after the perspective divide.
constexpr const char* kShadow2DBody = R"(
    int z = int(min(abs(w), 1.0) * float(0xFFFFFF)) - shadow_texture_bias;
    vec2 coord = vec2(textureSize(tex0_shadow, 0)) * uv - vec2(0.5);
    vec2 coord_floor = floor(coord);
    vec2 f = coord - coord_floor;
    ivec2 i = ivec2(coord_floor);
    vec4 s = vec4(
        SampleShadow2D(i              , z),
        SampleShadow2D(i + ivec2(1, 0), z),
        SampleShadow2D(i + ivec2(0, 1), z),
        SampleShadow2D(i + ivec2(1, 1), z));
    return vec4(mix2(s, f));
}
)";

// The cube: one texel of a face at its centre (the major axis is the face's, the other two inside (-1, 1)), its four bytes put back
// together; the face, its coordinates and the depth as ConvertCubeCoord gives them; Azahar's four texels clamped to the face.
constexpr const char* kShadowCubeGlsl = R"(
uint ShadowCubeTexel(int face, ivec2 texel, ivec2 size) {
    vec2 c = (vec2(texel) + vec2(0.5)) / vec2(size) * 2.0 - vec2(1.0);
    vec3 dir;
    if (face == 0) dir = vec3(1.0, -c.y, -c.x);
    else if (face == 1) dir = vec3(-1.0, -c.y, c.x);
    else if (face == 2) dir = vec3(c.x, 1.0, c.y);
    else if (face == 3) dir = vec3(c.x, -1.0, -c.y);
    else if (face == 4) dir = vec3(c.x, -c.y, 1.0);
    else dir = vec3(-c.x, -c.y, -1.0);
    uvec4 b = uvec4(round(textureLod(samplerCube(tex0_cube, samp0), dir, 0.0) * 255.0));
    return b.r | (b.g << 8) | (b.b << 16) | (b.a << 24);
}

vec4 shadowTextureCube(vec2 uv, float w) {
    float au = abs(uv.x), av = abs(uv.y), aw = abs(w);
    int face;
    vec2 xy;
    float zc;
    if (au > av && au > aw) {
        face = uv.x > 0.0 ? 0 : 1;
        xy = vec2(-w, uv.x > 0.0 ? -uv.y : uv.y);
        zc = uv.x;
    } else if (av > aw) {
        face = uv.y > 0.0 ? 2 : 3;
        xy = vec2(uv.y > 0.0 ? uv.x : -uv.x, w);
        zc = uv.y;
    } else {
        face = w > 0.0 ? 4 : 5;
        xy = vec2(uv.x, w > 0.0 ? -uv.y : uv.y);
        zc = w;
    }
    vec2 st = xy / zc * 0.5 + vec2(0.5);
    int z = int(min(abs(zc), 1.0) * float(0xFFFFFF)) - shadow_texture_bias;
    ivec2 size = textureSize(tex0_cube, 0);
    vec2 coord = vec2(size) * st - vec2(0.5);
    vec2 coord_floor = floor(coord);
    vec2 f = coord - coord_floor;
    ivec2 i00 = ivec2(coord_floor);
    ivec2 cmin = ivec2(0), cmax = size - ivec2(1, 1);
    ivec2 i10 = clamp(i00 + ivec2(1, 0), cmin, cmax);
    ivec2 i01 = clamp(i00 + ivec2(0, 1), cmin, cmax);
    ivec2 i11 = clamp(i00 + ivec2(1, 1), cmin, cmax);
    i00 = clamp(i00, cmin, cmax);
    vec4 s = vec4(
        CompareShadow(ShadowCubeTexel(face, i00, size), z),
        CompareShadow(ShadowCubeTexel(face, i10, size), z),
        CompareShadow(ShadowCubeTexel(face, i01, size), z),
        CompareShadow(ShadowCubeTexel(face, i11, size), z));
    return vec4(mix2(s, f));
}
)";

// WriteShadow: d from the depth WriteDepth made, s from the combiner output kept before fog (`shadow_tev_output`).
constexpr const char* kWriteHead = R"(
uint d = uint(clamp(depth, 0.0, 1.0) * float(0xFFFFFF));
uint s = uint(round(clamp(shadow_tev_output.g, 0.0, 1.0) * 255.0));
ivec2 image_coord = ivec2(gl_FragCoord.xy);
)";

constexpr const char* kWriteInterlock = R"(
beginInvocationInterlock();
uint old_shadow = imageLoad(shadow_buffer, image_coord).x;
uint new_shadow = UpdateShadow(old_shadow, d, s);
imageStore(shadow_buffer, image_coord, uvec4(new_shadow));
endInvocationInterlock();
)";

constexpr const char* kWriteCompSwap = R"(
uint old = imageLoad(shadow_buffer, image_coord).x;
uint new1;
uint old2;
do {
    old2 = old;
    new1 = UpdateShadow(old, d, s);
} while ((old = imageAtomicCompSwap(shadow_buffer, image_coord, old, new1)) != old2);
)";

// Pica::Float<10, 5>::FromRaw: an IEEE binary16 to a float, denormals renormalised so the value is exact.
float Float16ToFloat(u32 raw) {
    raw &= 0xFFFFu;
    const u32 sign = (raw >> 15) << 31;
    s32 exponent = static_cast<s32>((raw >> 10) & 0x1Fu);
    u32 mantissa = raw & 0x3FFu;
    u32 bits;
    if (exponent == 0 && mantissa == 0) {
        bits = sign;
    } else if (exponent == 0) {
        exponent = 127 - 15 + 1;
        while ((mantissa & 0x400u) == 0) {
            --exponent;
            mantissa <<= 1;
        }
        mantissa &= 0x3FFu;
        bits = sign | (static_cast<u32>(exponent) << 23) | (mantissa << 13);
    } else if (exponent == 0x1F) {
        bits = sign | 0x7F800000u | (mantissa << 13);
    } else {
        bits = sign | (static_cast<u32>(exponent + 127 - 15) << 23) | (mantissa << 13);
    }
    return std::bit_cast<float>(bits);
}

class ShadowFeature final : public FeatureModule {
public:
    const char* Name() const override { return "shadow"; }

    // ---- the generated code ------------------------------------------------------------------------------------------

    void Emit(Point point, EmitContext& ctx) const override {
        if (ctx.emitter == nullptr) {
            return;
        }
        Emitter& e = *ctx.emitter;
        const bool write_pass = e.pass() == ShaderPass::ShadowWrite;
        switch (point) {
        case Point::Declarations: Declare(e, write_pass); break;
        case Point::AlphaTest:
            // The combiner output the shadow is written from, before fog/ changes it at the next point.
            if (write_pass) {
                e.Raw("vec4 shadow_tev_output = combiner_output;\n");
            }
            break;
        case Point::Output:
            if (write_pass) {
                e.Raw(kWriteHead);
                e.Raw(e.profile().interlock ? kWriteInterlock : kWriteCompSwap);
            }
            break;
        default: break;
        }
    }

    // ---- the draw's needs --------------------------------------------------------------------------------------------

    Needs NeedsOf(const FragmentKey& key, const DeviceProfile&) const override {
        Needs needs;
        needs.shadow_write_pass = key.framebuffer.shadow_rendering() != 0;
        return needs;
    }

    // ---- uniforms ----------------------------------------------------------------------------------------------------

    // SyncDrawUniforms's "Sync shadow uniforms" (CheckShadow: GPUREG_FRAGOP_SHADOW or GPUREG_TEXUNIT0_SHADOW dirty).
    void FillUniforms(const pica::PicaState& state, FragmentBlock& block, UniformDirty& dirty) const override {
        if (!dirty.all && !state.dirty.Test(freg::kFramebufferShadow) && !state.dirty.Test(freg::kShadowTexture)) {
            return;
        }
        const u32 bias = state.regs[freg::kFramebufferShadow];
        const float constant = Float16ToFloat(Bits(bias, 0, 16));
        const float linear = Float16ToFloat(Bits(bias, 16, 16));
        const int texture_bias = static_cast<int>(Bits(state.regs[freg::kShadowTexture], 1, 23) << 1);
        FragmentUniforms& u = block.uniforms;
        const bool same = std::memcmp(&constant, &u.shadow_bias_constant, sizeof(float)) == 0 &&
                          std::memcmp(&linear, &u.shadow_bias_linear, sizeof(float)) == 0 && texture_bias == u.shadow_texture_bias;
        if (dirty.all || !same) {
            u.shadow_bias_constant = constant;
            u.shadow_bias_linear = linear;
            u.shadow_texture_bias = texture_bias;
            dirty.Mark();
        }
    }

private:
    // The prototype block's two functions in both passes (sampleTexUnit0 may call either), the write helpers in the write pass. The
    // specialised shader of a key whose unit 0 is not of the type defines the function as lit, which nothing calls.
    static void Declare(Emitter& e, bool write_pass) {
        e.Raw("\n// shadow/ (N2-12)\n");
        if (write_pass) {
            e.Raw(kWriteHelpers);
        }
        const Value type = e.Field(kw::kTexRaw, 0, 3);
        const Cond is_2d = e.Eq(type, kShadow2D);
        const Cond is_cube = e.Eq(type, kShadowCube);
        const bool any = !is_2d.known || is_2d.value || !is_cube.known || is_cube.value;
        if (any) {
            e.Raw(kCompareHelpers);
        }
        if (!is_2d.known || is_2d.value) {
            e.Raw(kSample2D);
            e.Raw("vec4 shadowTexture(vec2 uv, float w) {\n");
            // Shadow2D divides by w unless the unit is orthographic (TexturingRegs::shadow.orthographic).
            e.If(e.Eq(e.Field(kw::kTexRaw, 16, 1), 0), [&] { e.Raw("uv /= w;\n"); });
            e.Raw(kShadow2DBody);
        } else {
            e.Raw("vec4 shadowTexture(vec2 uv, float w) { return vec4(1.0); }\n");
        }
        if (!is_cube.known || is_cube.value) {
            e.Raw(kShadowCubeGlsl);
        } else {
            e.Raw("vec4 shadowTextureCube(vec2 uv, float w) { return vec4(1.0); }\n");
        }
    }
};

}  // namespace

MH4N_REGISTER_FRAGMENT_FEATURE(ShadowFeature)

// ---- the host side (shadow.h) --------------------------------------------------------------------------------------------------

namespace shadow {

std::optional<ShadowTexture> ShadowTextureOf(const u32* regs) {
    if (Bits(regs[freg::kMainConfig], 0, 1) == 0) {
        return std::nullopt;
    }
    const u32 type = Bits(regs[freg::kTexture0 + freg::kTextureParams], 28, 3);
    const u32 address = Bits(regs[kTexture0Address], 0, 28);
    if ((type != kShadow2D && type != kShadowCube) || address == 0) {
        return std::nullopt;
    }
    ShadowTexture out;
    out.cube = type == kShadowCube;
    out.width = Bits(regs[kTexture0Size], 16, 11);
    out.height = Bits(regs[kTexture0Size], 0, 11);
    out.format = Bits(regs[kTexture0Format], 0, 4);
    out.paddr[0] = address * 8;
    if (out.cube) {
        // GetCubePhysicalAddress: bits 22-27 of the main address are shared by all faces.
        for (u32 face = 1; face < 6; ++face) {
            out.paddr[face] = ((address & ~0x3FFFFFu) | Bits(regs[kCubeAddress + face - 1], 0, 22)) * 8;
        }
    }
    return out;
}

}  // namespace shadow
}  // namespace mh4n::gpu::fragment
