// Derived from Azahar src/video_core/renderer_software/sw_lighting.cpp (ComputeFragmentsColors: the reference the device cases
// compare with, ported below for draws without bump mapping and shadow), src/video_core/renderer_software/sw_rasterizer.cpp (the
// normalised interpolated quaternion it takes), src/video_core/pica/regs_lighting.h (the lighting registers, their fields, the
// LUT inputs, scales and samplers, IsLightingSamplerSupported), src/video_core/pica/pica_core.h (Lighting::LutEntry) and
// src/video_core/pica_types.h (Float::FromRaw for f16 and f20) at 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-2.0-or-later: every
// one of those files says "Licensed under GPLv2 or any later version"). The rig (Tri, Vertex, Rig) is adapted from gpu_fog_test.cpp
// in this directory (GPL-3.0-or-later, see its header).
// Copyright 2017 Citra Emulator Project (sw_lighting.cpp, regs_lighting.h)
// Copyright 2015-2026 Citra Emulator Project / Azahar Emulator Project (sw_rasterizer.cpp, pica_types.h)
// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project (pica_core.h)
//
// Fragment lighting (N2-09). Without a device: the stub is gone, the needs, the uniforms from the light registers, the shaders of
// many lighting configurations in both modes, the quaternion fix-up stage. On whatever Vulkan device the machine has
// (MH4N_VULKAN_DEVICE; skipped with a message without one): lit draws through the real path (FragmentState: the key, the uniforms,
// the 24 tables) against a CPU port of the software renderer's ComputeFragmentsColors, and the same draws in the ubershader.
//
// The software renderer truncates the lighting colours to bytes and the shader rounds them (the TEV's byteround), and the two
// compute in different float orders: the device cases allow 2 steps of 8 bits.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "gpu/cpuvertex/cpu_vertex.h"
#include "gpu/cpuvertex/triangle_batch.h"
#include "gpu/fbops/fixed_function.h"
#include "gpu/fragment/feature.h"
#include "gpu/fragment/generator.h"
#include "gpu/fragment/key.h"
#include "gpu/fragment/lighting/fixup.h"
#include "gpu/fragment/profile.h"
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

constexpr u32 kSize = 8;

// ---- registers --------------------------------------------------------------------------------------------------------

constexpr u32 kRegTev0 = 0xC0;
constexpr u32 kRegLogicOp = 0x102, kRegDepthColorMask = 0x107, kRegAllowColor = 0x113, kRegAllowDepth = 0x115;
constexpr u32 kRegLight0 = 0x140;
constexpr u32 kRegAmbient = 0x1C0, kRegMaxLight = 0x1C2, kRegConfig0 = 0x1C3, kRegConfig1 = 0x1C4, kRegDisable = 0x1C6;
constexpr u32 kRegAbs = 0x1D0, kRegInput = 0x1D1, kRegScale = 0x1D2, kRegSlots = 0x1D9;

// LightingSampler, LightingLutInput, LightingConfig.
constexpr u32 kD0 = 0, kD1 = 1, kFr = 3, kRb = 4, kRg = 5, kRr = 6, kSp = 8, kDa = 16;
constexpr u32 kNH = 0, kVH = 1, kNV = 2, kLN = 3, kSP = 4, kCP = 5;
constexpr u32 kConfig7 = 8;

// The field positions of the seven LUTs in abs_lut_input (the disable bit), lut_input and lut_scale (3 bits each), and their
// disable bits in config1 (-1: the spot LUT has none).
struct LutField {
    u32 sampler;
    u32 abs_bit;
    u32 field;
    int disable_bit;
};
constexpr LutField kLutD0{kD0, 1, 0, 16}, kLutD1{kD1, 5, 4, 17}, kLutSp{kSp, 9, 8, -1}, kLutFr{kFr, 13, 12, 19};
constexpr LutField kLutRb{kRb, 17, 16, 22}, kLutRg{kRg, 21, 20, 21}, kLutRr{kRr, 25, 24, 20};

u32 Bits(u32 word, u32 first, u32 count) {
    return (word >> first) & ((1u << count) - 1u);
}

// ---- the PICA's small floats --------------------------------------------------------------------------------------------

// Float::FromRaw.
template <unsigned M, unsigned E>
float FromRaw(u32 hex) {
    constexpr s32 bias = 127 - ((1 << (E - 1)) - 1);
    s32 exponent = static_cast<s32>((hex >> M) & ((1u << E) - 1));
    u32 mantissa = hex & ((1u << M) - 1);
    const u32 sign = ((hex >> (E + M)) & 1u) << 31;
    u32 out;
    if (exponent == 0 && mantissa == 0) {
        out = sign;
    } else if (exponent == 0) {
        exponent = bias + 1;
        while ((mantissa & (1u << M)) == 0) {
            --exponent;
            mantissa <<= 1;
        }
        mantissa &= (1u << M) - 1;
        out = sign | (static_cast<u32>(exponent) << 23) | (mantissa << (23 - M));
    } else if (static_cast<u32>(exponent) == (1u << E) - 1) {
        out = sign | (0xFFu << 23) | (mantissa << (23 - M));
    } else {
        out = sign | (static_cast<u32>(bias + exponent) << 23) | (mantissa << (23 - M));
    }
    return std::bit_cast<float>(out);
}

// The raw bits of a normal float in the PICA's M-bit mantissa, E-bit exponent format (the mantissa truncated).
template <unsigned M, unsigned E>
u32 ToRaw(float f) {
    if (f == 0.0f) return 0;
    const u32 bits = std::bit_cast<u32>(f);
    const u32 sign = bits >> 31;
    const int exponent = static_cast<int>((bits >> 23) & 0xFF) - 127 + ((1 << (E - 1)) - 1);
    REQUIRE(exponent > 0);
    REQUIRE(exponent < (1 << E) - 1);
    return (sign << (E + M)) | (static_cast<u32>(exponent) << M) | ((bits & 0x7FFFFF) >> (23 - M));
}

u32 F16(float f) { return ToRaw<10, 5>(f); }
u32 F20(float f) { return ToRaw<12, 7>(f); }

// A LightColor word from bytes (255 is 1.0).
u32 Color(u32 r, u32 g, u32 b) {
    return (r << 20) | (g << 10) | b;
}

// A 13-bit signed spot component.
u32 Spot13(int v) {
    return static_cast<u32>(v) & 0x1FFF;
}

// A lighting LUT entry: value 0.0.12 in bits 0-11, |difference| 0.0.11 in bits 12-22, its sign in bit 23.
u32 LutEntry(int value, int difference) {
    return static_cast<u32>(value) | (static_cast<u32>(std::abs(difference)) << 12) | (difference < 0 ? 1u << 23 : 0u);
}

// A table of a smooth function f on [0, 1] (index i at i / 256) or, signed, on [-1, 1) (index i at i / 128, the upper half of
// the entries holding the negative inputs): value f(x) and difference f(x + step) - f(x).
template <typename F>
std::array<u32, 256> Table(F&& f, bool signed_input) {
    std::array<u32, 256> table{};
    for (int i = 0; i < 256; ++i) {
        float x, next;
        if (signed_input) {
            const int s = i < 128 ? i : i - 256;
            x = static_cast<float>(s) / 128.0f;
            next = static_cast<float>(s + 1) / 128.0f;
        } else {
            x = static_cast<float>(i) / 256.0f;
            next = static_cast<float>(i + 1) / 256.0f;
        }
        const int value = static_cast<int>(std::lround(std::clamp(f(x), 0.0f, 1.0f) * 4095.0f));
        const int to = static_cast<int>(std::lround(std::clamp(f(next), 0.0f, 1.0f) * 4095.0f));
        const int difference = static_cast<int>(std::lround(static_cast<float>(to - value) / 4095.0f * 2047.0f));
        table[i] = LutEntry(value, std::clamp(difference, -2047, 2047));
    }
    return table;
}

// ---- the base state ------------------------------------------------------------------------------------------------------

// Stage 0 takes `source` (1: the primary fragment colour, 2: the secondary) for colour and alpha, Replace; the other stages pass
// through; colour writes on, no depth, no blending, lighting off.
std::unique_ptr<pica::PicaState> BaseState(u32 source = 1) {
    auto state = std::make_unique<pica::PicaState>();
    state->regs.fill(0);
    state->dirty.SetAll();
    state->regs[kRegTev0 + 0] = source | (15u << 4) | (15u << 8) | (source << 16) | (15u << 20) | (15u << 24);
    for (u32 base : {0xC8u, 0xD0u, 0xD8u, 0xF0u, 0xF8u}) {
        state->regs[base + 0] = 15u | (15u << 4) | (15u << 8) | (15u << 16) | (15u << 20) | (15u << 24);
    }
    state->regs[kRegLogicOp] = static_cast<u32>(re::LogicOp::Copy);
    state->regs[kRegDepthColorMask] = 0xF00;
    state->regs[kRegAllowColor] = 0xF;
    state->regs[kRegAllowDepth] = 0;
    state->regs[0x6D] = 1;  // Z-buffering
    state->regs[0x4D] = 0xBF0000;  // depth range -1.0 (float24)
    state->regs[kRegDisable] = 1;
    return state;
}

// One light's sixteen registers.
struct LightRegs {
    u32 specular_0 = 0, specular_1 = 0, diffuse = 0, ambient = 0;
    float x = 0, y = 0, z = 0;
    int spot_x = 0, spot_y = 0, spot_z = 0;
    bool directional = false, two_sided = false, geometric_0 = false, geometric_1 = false;
    float dist_bias = 0, dist_scale = 0;
};

void WriteLight(pica::PicaState& s, u32 num, const LightRegs& l) {
    const u32 base = kRegLight0 + 0x10 * num;
    s.regs[base + 0] = l.specular_0;
    s.regs[base + 1] = l.specular_1;
    s.regs[base + 2] = l.diffuse;
    s.regs[base + 3] = l.ambient;
    s.regs[base + 4] = F16(l.x) | (F16(l.y) << 16);
    s.regs[base + 5] = F16(l.z);
    s.regs[base + 6] = Spot13(l.spot_x) | (Spot13(l.spot_y) << 16);
    s.regs[base + 7] = Spot13(l.spot_z);
    s.regs[base + 9] = (l.directional ? 1u : 0u) | (l.two_sided ? 2u : 0u) | (l.geometric_0 ? 4u : 0u) | (l.geometric_1 ? 8u : 0u);
    s.regs[base + 10] = l.dist_bias == 0.0f ? 0 : F20(l.dist_bias);
    s.regs[base + 11] = l.dist_scale == 0.0f ? 0 : F20(l.dist_scale);
}

// A LUT's input, abs mode and scale (the scale field's value: 0 1x, 1 2x, 2 4x, 3 8x, 6 0.25x, 7 0.5x), enabled.
void SetLut(pica::PicaState& s, const LutField& lut, u32 input, bool abs, u32 scale) {
    s.regs[kRegInput] = (s.regs[kRegInput] & ~(7u << lut.field)) | (input << lut.field);
    s.regs[kRegScale] = (s.regs[kRegScale] & ~(7u << lut.field)) | (scale << lut.field);
    if (abs) {
        s.regs[kRegAbs] &= ~(1u << lut.abs_bit);
    } else {
        s.regs[kRegAbs] |= 1u << lut.abs_bit;
    }
    if (lut.disable_bit >= 0) {
        s.regs[kRegConfig1] &= ~(1u << lut.disable_bit);
    }
}

// Lighting on with `slots` (slot n holds light slots[n]) and configuration `config`; every LUT, distance and spot attenuation and
// shadow off (config1 all set), abs off for every input; the 24 tables filled with distinct smooth functions.
void Lit(pica::PicaState& s, std::vector<u32> slots, u32 config) {
    s.regs[kRegDisable] = 0;
    s.regs[kRegMaxLight] = static_cast<u32>(slots.size()) - 1;
    u32 enable = 0;
    for (size_t i = 0; i < slots.size(); ++i) enable |= slots[i] << (4 * i);
    s.regs[kRegSlots] = enable;
    s.regs[kRegConfig0] = config << 4;
    s.regs[kRegConfig1] = 0xFFFFFFFFu;
    s.regs[kRegAbs] = 0xFFFFFFFFu;
    for (u32 t = 0; t < 24; ++t) {
        const float k = 0.5f + 0.1f * static_cast<float>(t % 7);
        const bool signed_table = t < 8;  // the first eight are read signed by the cases that use them so
        s.lighting.luts[t] = Table([k](float x) { return 0.15f + 0.8f * std::pow(std::abs(x), k) * (x < 0 ? 0.6f : 1.0f); },
                                   signed_table);
    }
    s.lighting.lut_dirty = pica::Lighting::kAllDirty;
}

// ---- the software renderer's lighting (ComputeFragmentsColors, no bump map, no shadow) ---------------------------------------

struct V3 {
    float x = 0, y = 0, z = 0;
    V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(float k) const { return {x * k, y * k, z * k}; }
    V3 operator*(const V3& o) const { return {x * o.x, y * o.y, z * o.z}; }
};
float Dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Cross(const V3& a, const V3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Length(const V3& v) { return std::sqrt(Dot(v, v)); }
V3 Normalized(const V3& v) { return v * (1.0f / Length(v)); }
V3 LightColorOf(u32 w) { return {Bits(w, 20, 10) / 255.0f, Bits(w, 10, 10) / 255.0f, Bits(w, 0, 10) / 255.0f}; }

float ScaleOf(u32 v) {
    switch (v) {
    case 0: return 1.0f;
    case 1: return 2.0f;
    case 2: return 4.0f;
    case 3: return 8.0f;
    case 6: return 0.25f;
    case 7: return 0.5f;
    default: return 0.0f;
    }
}

bool Supported(u32 config, u32 sampler) {
    switch (sampler) {
    case kD0: return config != 1;
    case kD1: return config != 0 && config != 1 && config != 5;
    case kSp: return config != 2 && config != 3;
    case kFr: return config != 0 && config != 2 && config != 4;
    case kRr: return config != 3;
    case kRg:
    case kRb: return config == 4 || config == 5 || config == kConfig7;
    default: return false;
    }
}

float Lookup(const pica::PicaState& s, u32 lut, u32 index, float delta) {
    const u32 e = s.lighting.luts[lut][index];
    const float value = static_cast<float>(Bits(e, 0, 12)) / 4095.0f;
    const float diff = static_cast<float>(Bits(e, 12, 11)) / 2047.0f * (Bits(e, 23, 1) ? -1.0f : 1.0f);
    return value + diff * delta;
}

struct SwColors {
    std::array<int, 4> diffuse, specular;
};

SwColors SwLighting(const pica::PicaState& s, std::array<float, 4> quat, V3 view) {
    const auto& r = s.regs;
    const u32 config0 = r[kRegConfig0], config1 = r[kRegConfig1];
    const u32 config = Bits(config0, 4, 4);
    // the normalised quaternion
    const float ql = std::sqrt(quat[0] * quat[0] + quat[1] * quat[1] + quat[2] * quat[2] + quat[3] * quat[3]);
    const V3 qv{quat[0] / ql, quat[1] / ql, quat[2] / ql};
    const float qw = quat[3] / ql;
    auto rotate = [&](V3 v) { return v + Cross(qv, Cross(qv, v) + v * qw) * 2.0f; };
    const V3 normal = rotate({0, 0, 1});
    const V3 tangent = rotate({1, 0, 0});

    float diffuse_sum[4] = {0, 0, 0, 1};
    float specular_sum[4] = {0, 0, 0, 1};
    const u32 max_light = Bits(r[kRegMaxLight], 0, 3);
    for (u32 slot = 0; slot <= max_light; ++slot) {
        const u32 num = Bits(r[kRegSlots], 4 * slot, 3);
        const u32 base = kRegLight0 + 0x10 * num;
        const V3 position{FromRaw<10, 5>(Bits(r[base + 4], 0, 16)), FromRaw<10, 5>(Bits(r[base + 4], 16, 16)),
                          FromRaw<10, 5>(Bits(r[base + 5], 0, 16))};
        const u32 lconfig = r[base + 9];
        const bool directional = Bits(lconfig, 0, 1), two_sided = Bits(lconfig, 1, 1);
        const bool geo0 = Bits(lconfig, 2, 1), geo1 = Bits(lconfig, 3, 1);
        V3 light_vector = directional ? position : position + view;
        const float length = Length(light_vector);
        light_vector = light_vector * (1.0f / length);
        const V3 norm_view = Normalized(view);
        const V3 half_vector = norm_view + light_vector;

        float dist_atten = 1.0f;
        if (Bits(config1, 24 + num, 1) == 0) {
            const float scale = FromRaw<12, 7>(Bits(r[base + 11], 0, 20));
            const float bias = FromRaw<12, 7>(Bits(r[base + 10], 0, 20));
            const float loc = std::clamp(scale * length + bias, 0.0f, 1.0f);
            const u32 index = static_cast<u32>(std::clamp(std::floor(loc * 256.0f), 0.0f, 255.0f));
            dist_atten = Lookup(s, kDa + num, index, loc * 256.0f - static_cast<float>(index));
        }

        auto lut_value = [&](const LutField& lut, u32 sampler) {
            const u32 input = Bits(r[kRegInput], lut.field, 3);
            const bool abs = Bits(r[kRegAbs], lut.abs_bit, 1) == 0;
            float x = 0.0f;
            switch (input) {
            case kNH: x = Dot(normal, Normalized(half_vector)); break;
            case kVH: x = Dot(norm_view, Normalized(half_vector)); break;
            case kNV: x = Dot(normal, norm_view); break;
            case kLN: x = Dot(light_vector, normal); break;
            case kSP: {
                const auto s13 = [](u32 w, u32 first) { return static_cast<float>(static_cast<s32>(Bits(w, first, 13) << 19) >> 19) / 2047.0f; };
                x = Dot(light_vector, V3{s13(r[base + 6], 0), s13(r[base + 6], 16), s13(r[base + 7], 0)});
                break;
            }
            case kCP:
                if (config == kConfig7) {
                    const V3 h = Normalized(half_vector);
                    x = Dot(h - normal * Dot(normal, h), tangent);
                }
                break;
            }
            u32 index;
            float delta;
            if (abs) {
                x = two_sided ? std::abs(x) : std::max(x, 0.0f);
                index = static_cast<u32>(std::clamp(std::floor(x * 256.0f), 0.0f, 255.0f));
                delta = x * 256.0f - static_cast<float>(index);
            } else {
                const int si = static_cast<int>(std::clamp(std::floor(x * 128.0f), -128.0f, 127.0f));
                delta = x * 128.0f - static_cast<float>(si);
                index = static_cast<u32>(static_cast<u8>(static_cast<s8>(si)));
            }
            return ScaleOf(Bits(r[kRegScale], lut.field, 3)) * Lookup(s, sampler, index, delta);
        };
        auto enabled = [&](const LutField& lut) { return lut.disable_bit < 0 || Bits(config1, lut.disable_bit, 1) == 0; };

        float spot_atten = 1.0f;
        if (Bits(config1, 8 + num, 1) == 0 && Supported(config, kSp)) spot_atten = lut_value(kLutSp, kSp + num);
        float d0 = 1.0f;
        if (enabled(kLutD0) && Supported(config, kD0)) d0 = lut_value(kLutD0, kD0);
        V3 specular_0 = LightColorOf(r[base + 0]) * d0;
        V3 refl{1, 1, 1};
        refl.x = enabled(kLutRr) && Supported(config, kRr) ? lut_value(kLutRr, kRr) : 1.0f;
        refl.y = enabled(kLutRg) && Supported(config, kRg) ? lut_value(kLutRg, kRg) : refl.x;
        refl.z = enabled(kLutRb) && Supported(config, kRb) ? lut_value(kLutRb, kRb) : refl.x;
        float d1 = 1.0f;
        if (enabled(kLutD1) && Supported(config, kD1)) d1 = lut_value(kLutD1, kD1);
        V3 specular_1 = refl * LightColorOf(r[base + 1]) * d1;
        if (slot == max_light && enabled(kLutFr) && Supported(config, kFr)) {
            const float fr = lut_value(kLutFr, kFr);
            if (Bits(config0, 2, 1)) diffuse_sum[3] = fr;
            if (Bits(config0, 3, 1)) specular_sum[3] = fr;
        }
        float dot_product = Dot(light_vector, normal);
        dot_product = two_sided ? std::abs(dot_product) : std::max(dot_product, 0.0f);
        float clamp_highlights = 1.0f;
        if (Bits(config0, 27, 1)) clamp_highlights = dot_product == 0.0f ? 0.0f : 1.0f;
        if (geo0 || geo1) {
            float geo = Dot(half_vector, half_vector);
            geo = geo == 0.0f ? 0.0f : std::min(dot_product / geo, 1.0f);
            if (geo0) specular_0 = specular_0 * geo;
            if (geo1) specular_1 = specular_1 * geo;
        }
        const V3 diffuse = (LightColorOf(r[base + 2]) * dot_product + LightColorOf(r[base + 3])) * dist_atten * spot_atten;
        const V3 specular = (specular_0 + specular_1) * clamp_highlights * dist_atten * spot_atten;
        diffuse_sum[0] += diffuse.x; diffuse_sum[1] += diffuse.y; diffuse_sum[2] += diffuse.z;
        specular_sum[0] += specular.x; specular_sum[1] += specular.y; specular_sum[2] += specular.z;
    }
    const V3 ambient = LightColorOf(r[kRegAmbient]);
    diffuse_sum[0] += ambient.x; diffuse_sum[1] += ambient.y; diffuse_sum[2] += ambient.z;
    SwColors out;
    for (int i = 0; i < 4; ++i) {
        out.diffuse[i] = static_cast<u8>(std::clamp(diffuse_sum[i], 0.0f, 1.0f) * 255);
        out.specular[i] = static_cast<u8>(std::clamp(specular_sum[i], 0.0f, 1.0f) * 255);
    }
    return out;
}

// ---- the rig (adapted from gpu_fog_test.cpp) ------------------------------------------------------------------------------

using cpuvertex::OutputVertex;

struct Tri {
    OutputVertex v[3];
};

// A triangle over the whole target whose three vertices carry the same quaternion and view (so every pixel has them).
Tri LitTriangle(std::array<float, 4> quat, V3 view) {
    Tri t{};
    const float xy[3][2] = {{-1, -1}, {3, -1}, {-1, 3}};
    for (int i = 0; i < 3; ++i) {
        OutputVertex& v = t.v[i];
        v.pos[0] = xy[i][0]; v.pos[1] = xy[i][1]; v.pos[2] = -0.5f; v.pos[3] = 1.0f;
        for (int c = 0; c < 4; ++c) v.quat[c] = quat[c];
        for (int c = 0; c < 4; ++c) v.color[c] = 1.0f;
        v.tc0_w = 1.0f;
        v.view[0] = view.x; v.view[1] = view.y; v.view[2] = view.z;
    }
    return t;
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

struct Rig {
    std::shared_ptr<rhi::Backend> backend;
    DeviceProfile profile;
    std::unique_ptr<native::PipelineBuilder> builder;
    std::unique_ptr<native::DefaultPipelineProvider> provider;
    std::unique_ptr<native::DrawComposer> composer;
    std::unique_ptr<FragmentUniformRing> uniforms;
    std::unique_ptr<TableRings> tables;
    std::unique_ptr<rhi::Ring> vertex_ring, vs_ring;
    std::shared_ptr<rhi::Image> color;

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
        auto vr = backend->CreateRing(1 << 20, rhi::BufferUsage::Vertex, "lighting vertices");
        auto sr = backend->CreateRing(1 << 20, rhi::BufferUsage::Uniform, "lighting vs uniforms");
        REQUIRE(vr.ok());
        REQUIRE(sr.ok());
        vertex_ring = std::move(*vr);
        vs_ring = std::move(*sr);
        rhi::ImageDesc desc;
        desc.format = rhi::Format::Rgba8Unorm;
        desc.width = desc.height = kSize;
        desc.name = "lighting colour";
        auto image = backend->CreateImage(desc);
        REQUIRE_MESSAGE(image.ok(), image.error());
        color = *image;
    }

    // One draw through FragmentState; the pixel at (3, 3).
    std::array<int, 4> Render(const pica::PicaState& state, const Tri& tri) {
        rhi::Recorder rec = backend->BeginRecording();
        composer->BeginRecording(rec);
        uniforms->BeginRecording();
        FragmentState fragment(profile, FeatureRegistry::Global());
        auto prepared = fragment.Prepare(state, {}, tables.get());
        REQUIRE_MESSAGE(prepared.ok(), prepared.error());
        auto alloc = uniforms->Write(fragment.block(), prepared->block_changed);
        REQUIRE_MESSAGE(alloc.ok(), alloc.error());

        cpuvertex::TriangleBatch batch;
        batch.AddTriangle(tri.v[0], tri.v[1], tri.v[2]);
        auto streamed = cpuvertex::UploadBatch(*vertex_ring, batch);
        REQUIRE_MESSAGE(streamed.ok(), streamed.error());
        auto vs = vs_ring->Map(2048, std::max<u64>(256, backend->Caps().uniform_buffer_offset_alignment));
        REQUIRE_MESSAGE(vs.ok(), vs.error());
        std::memset(vs->data.data(), 0, 2048);
        REQUIRE(vs_ring->Commit(2048).ok());

        rhi::TargetPassDesc pass;
        pass.color = color.get();
        pass.area = rhi::Rect{0, 0, kSize, kSize};
        rhi::TargetPassDesc clear_pass = pass;
        clear_pass.color_load = rhi::LoadOp::Clear;
        clear_pass.clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
        composer->BeginTargetPass(rec, clear_pass);
        fbops::ApplyDynamicState(rec, fbops::MakeDynamicState(state), false);
        const FragmentKey key = fragment.key();
        native::DrawDesc d;
        d.vertex.kind = native::VertexStageKind::Trivial;
        d.vertex.input = InputKey();
        d.fragment = &key;
        d.needs = fragment.needs();
        d.state = fbops::MakeFixedFunctionState(state);
        d.color_format = rhi::Format::Rgba8Unorm;
        d.depth_format = rhi::Format::Undefined;
        d.target = pass;
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
        backend->WaitIdle(backend->Submit(std::move(rec)));
        auto pixels = backend->ReadImage(*color, 0, 0, kSize, kSize);
        REQUIRE_MESSAGE(pixels.ok(), pixels.error());
        const size_t i = (3 * kSize + 3) * 4;
        return {(*pixels)[i], (*pixels)[i + 1], (*pixels)[i + 2], (*pixels)[i + 3]};
    }
};

bool Near(std::array<int, 4> a, std::array<int, 4> b, int tolerance) {
    for (int c = 0; c < 4; ++c) {
        if (std::abs(a[c] - b[c]) > tolerance) return false;
    }
    return true;
}

std::string Str(std::array<int, 4> p) {
    return "(" + std::to_string(p[0]) + ", " + std::to_string(p[1]) + ", " + std::to_string(p[2]) + ", " + std::to_string(p[3]) + ")";
}

// ---- the cases ----------------------------------------------------------------------------------------------------------

struct Case {
    const char* name;
    void (*edit)(pica::PicaState&);
    std::array<float, 4> quat;
    V3 view;
};

const std::array<float, 4> kTilted = {0.2f, -0.3f, 0.1f, 0.9f};

LightRegs Positional() {
    LightRegs l;
    l.specular_0 = Color(70, 60, 50);
    l.specular_1 = Color(30, 40, 50);
    l.diffuse = Color(120, 100, 80);
    l.ambient = Color(20, 30, 40);
    l.x = 0.5f; l.y = 1.0f; l.z = 2.0f;
    l.spot_x = 300; l.spot_y = -700; l.spot_z = -1800;
    return l;
}

const Case kCases[] = {
    {"diffuse only, positional, config 0, no LUT",
     [](pica::PicaState& s) {
         Lit(s, {0}, 0);
         WriteLight(s, 0, Positional());
         s.regs[kRegAmbient] = Color(10, 12, 14);
     },
     kTilted, {0.25f, -0.5f, 1.5f}},
    {"distribution 0 (NH, abs, 2x) and reflection red (NV, signed), distance and spot attenuation, config 0",
     [](pica::PicaState& s) {
         Lit(s, {3}, 0);
         LightRegs l = Positional();
         l.dist_bias = 0.125f; l.dist_scale = 0.25f;
         WriteLight(s, 3, l);
         SetLut(s, kLutD0, kNH, true, 1);
         SetLut(s, kLutRr, kNV, false, 0);
         SetLut(s, kLutSp, kSP, true, 0);
         s.regs[kRegConfig1] &= ~(1u << (24 + 3));  // distance attenuation of light 3
         s.regs[kRegConfig1] &= ~(1u << (8 + 3));   // spot attenuation of light 3
         s.regs[kRegAmbient] = Color(5, 5, 5);
     },
     kTilted, {0.25f, -0.5f, 1.5f}},
    {"configuration 7: every LUT, CP and VH and LN inputs, Fresnel into both alphas, clamp highlights, geometric factors",
     [](pica::PicaState& s) {
         Lit(s, {1}, kConfig7);
         LightRegs l = Positional();
         l.geometric_0 = l.geometric_1 = true;
         WriteLight(s, 1, l);
         SetLut(s, kLutD0, kVH, true, 0);
         SetLut(s, kLutD1, kCP, false, 7);
         SetLut(s, kLutFr, kNV, true, 0);
         SetLut(s, kLutRr, kLN, true, 0);
         SetLut(s, kLutRg, kNH, false, 6);
         SetLut(s, kLutRb, kVH, true, 1);
         s.regs[kRegConfig0] |= (1u << 2) | (1u << 3) | (1u << 27);
     },
     kTilted, {-0.75f, 0.25f, 1.0f}},
    {"two slots holding lights 5 and 2 (one directional and two-sided), Fresnel of the last slot",
     [](pica::PicaState& s) {
         Lit(s, {5, 2}, 6);
         LightRegs a = Positional();
         a.two_sided = true;
         a.directional = true;
         a.x = -0.25f; a.y = 0.5f; a.z = -1.0f;  // behind the surface: the two-sided light still lights it
         WriteLight(s, 5, a);
         LightRegs b = Positional();
         b.diffuse = Color(60, 200, 90);
         WriteLight(s, 2, b);
         SetLut(s, kLutD0, kNH, true, 0);
         SetLut(s, kLutD1, kLN, true, 0);
         SetLut(s, kLutFr, kNV, false, 0);
         s.regs[kRegConfig0] |= (1u << 2);
         s.regs[kRegAmbient] = Color(8, 16, 24);
     },
     kTilted, {0.25f, -0.5f, 1.5f}},
};

}  // namespace

// ---- without a device ---------------------------------------------------------------------------------------------------

TEST_CASE("lighting/the_stub_is_gone_and_a_lit_draw_needs_the_lf_table_and_the_fixup") {
    auto state = BaseState();
    Lit(*state, {0}, 0);
    const FragmentKey key = MakeKey(*state);
    REQUIRE(key.lighting.enable() == 1);
    std::array<const char*, 8> names{};
    CHECK(StubbedFeatures(key, FeatureRegistry::Global(), names) == 0);
    const Needs needs = NeedsOf(key, DeviceProfile{});
    CHECK(needs.lut_lf);
    CHECK(needs.fixup_gs);
    const Needs off = NeedsOf(MakeKey(*BaseState()), DeviceProfile{});
    CHECK_FALSE(off.lut_lf);
    CHECK_FALSE(off.fixup_gs);
}

TEST_CASE("lighting/the_uniforms_follow_the_light_registers") {
    auto state = BaseState();
    Lit(*state, {6}, 0);
    LightRegs l = Positional();
    l.dist_bias = 0.125f;
    l.dist_scale = -3.0f;
    WriteLight(*state, 6, l);
    state->regs[kRegAmbient] = Color(255, 0, 51);
    FragmentBlock block{};
    UniformDirty dirty;
    dirty.all = true;
    FillUniforms(*state, block, dirty);
    CHECK(dirty.changed);
    const LightSrc& src = block.uniforms.light_src[6];
    CHECK(src.specular_0.x == doctest::Approx(70 / 255.0f));
    CHECK(src.specular_1.z == doctest::Approx(50 / 255.0f));
    CHECK(src.diffuse.y == doctest::Approx(100 / 255.0f));
    CHECK(src.ambient.x == doctest::Approx(20 / 255.0f));
    CHECK(src.position.x == 0.5f);
    CHECK(src.position.y == 1.0f);
    CHECK(src.position.z == 2.0f);
    CHECK(src.spot_direction.x == doctest::Approx(300 / 2047.0f));
    CHECK(src.spot_direction.y == doctest::Approx(-700 / 2047.0f));
    CHECK(src.spot_direction.z == doctest::Approx(-1800 / 2047.0f));
    CHECK(src.dist_atten_bias == 0.125f);
    CHECK(src.dist_atten_scale == -3.0f);
    CHECK(block.uniforms.lighting_global_ambient.x == 1.0f);
    CHECK(block.uniforms.lighting_global_ambient.y == 0.0f);
    CHECK(block.uniforms.lighting_global_ambient.z == doctest::Approx(0.2f));

    // nothing dirty: nothing written
    state->dirty.Reset();
    UniformDirty again;
    FillUniforms(*state, block, again);
    CHECK_FALSE(again.changed);
    // one light register written: that light again
    state->regs[kRegLight0 + 0x10 * 6 + 2] = Color(1, 2, 3);
    state->dirty.Set(kRegLight0 + 0x10 * 6 + 2);
    UniformDirty one;
    FillUniforms(*state, block, one);
    CHECK(one.changed);
    CHECK(block.uniforms.light_src[6].diffuse.x == doctest::Approx(1 / 255.0f));
}

TEST_CASE("lighting/the_shaders_compile_for_many_configurations_in_both_modes") {
    DeviceProfile profile;
    std::vector<FragmentKey> keys;
    for (const Case& c : kCases) {
        auto state = BaseState();
        c.edit(*state);
        keys.push_back(MakeKey(*state));
    }
    // bump modes, renormalisation, the shadow term and its options, every configuration
    for (u32 bump = 0; bump < 3; ++bump) {
        for (u32 config : {0u, 1u, 2u, 3u, 4u, 5u, 6u, kConfig7}) {
            auto state = BaseState(bump == 2 ? 2 : 1);
            Lit(*state, {0, 7}, config);
            SetLut(*state, kLutD0, kCP, bump == 1, 0);
            SetLut(*state, kLutRg, kSP, bump != 1, 3);
            state->regs[kRegConfig0] |= (bump << 28) | (1u << 22) | ((bump & 1u) << 30) | 1u | (1u << 16) | (1u << 17) |
                                        (bump == 2 ? (1u << 18) | (1u << 19) : 0u) | (2u << 24);
            state->regs[kRegConfig1] &= ~0xFFu;  // shadow on for every light
            keys.push_back(MakeKey(*state));
        }
    }
    for (const FragmentKey& key : keys) {
        const Generated g = GenerateSpecialised(key, profile, ShaderPass::Normal);
        CHECK_MESSAGE(g.ok(), g.error);
        CHECK(g.glsl.find("LookupLightingLUT") != std::string::npos);
    }
    const Generated uber = GenerateUber(ShaderPass::Normal, profile);
    CHECK_MESSAGE(uber.ok(), uber.error);
    CHECK(uber.glsl.find("light_raw") != std::string::npos);

    // nothing reads the lighting result: the specialised shader leaves the block out
    auto state = BaseState(0);  // stage 0 reads the primary colour, not a fragment colour
    Lit(*state, {0}, 0);
    const std::string unread = GenerateSpecialisedGlsl(MakeKey(*state), profile, ShaderPass::Normal);
    CHECK(unread.find("diffuse_sum") == std::string::npos);
    // and lighting off has no helpers
    CHECK(GenerateSpecialisedGlsl(MakeKey(*BaseState()), profile, ShaderPass::Normal).find("LookupLightingLUT") == std::string::npos);
}

TEST_CASE("lighting/the_fixup_geometry_stage_compiles_and_flips_against_the_first_vertex") {
    lighting::FixupStageInputs in;
    in.num_outputs = 4;
    for (u32 s = 0; s < 24; ++s) in.semantic_map[s] = {static_cast<u8>(s / 4 < 4 ? s / 4 : 16), static_cast<u8>(s % 4)};
    for (bool clip : {false, true}) {
        in.clip_distance = clip;
        std::string error;
        const std::vector<u32> spirv = lighting::GenerateFixupGeometrySpirv(in, &error);
        CHECK_MESSAGE(!spirv.empty(), error);
        const std::string glsl = lighting::GenerateFixupGeometryGlsl(in);
        CHECK((glsl.find("gl_ClipDistance") != std::string::npos) == clip);
        CHECK(glsl.find("AreQuaternionsOpposite(GetVertexQuaternion(vtx0), GetVertexQuaternion(vtx2))") != std::string::npos);
        CHECK(glsl.find("layout(location = 3) in vec4 vs_out_attr3[];") != std::string::npos);
        CHECK(glsl.find("vs_out_attr4") == std::string::npos);
    }
    lighting::FixupStageInputs empty;
    std::string error;
    CHECK_MESSAGE(!lighting::GenerateFixupGeometrySpirv(empty, &error).empty(), error);
}

// ---- on a device ----------------------------------------------------------------------------------------------------------

TEST_CASE("lighting/lit_draws_match_the_software_renderers_formulas_in_both_modes") {
    auto backend = test::RequireBackend();
    if (!backend) return;
    Rig specialised(backend, false);
    Rig uber(backend, true);
    for (const Case& c : kCases) {
        for (u32 source : {1u, 2u}) {
            const std::string name = c.name;
            CAPTURE(name);
            CAPTURE(source);
            auto state = BaseState(source);
            c.edit(*state);
            const SwColors sw = SwLighting(*state, c.quat, c.view);
            const std::array<int, 4> want = source == 1 ? sw.diffuse : sw.specular;
            const Tri tri = LitTriangle(c.quat, c.view);
            const std::array<int, 4> got = specialised.Render(*state, tri);
            CHECK_MESSAGE(Near(got, want, 2), "specialised " << Str(got) << " software " << Str(want));
            const std::array<int, 4> got_uber = uber.Render(*state, tri);
            CHECK_MESSAGE(Near(got_uber, got, 1), "ubershader " << Str(got_uber) << " specialised " << Str(got));
        }
    }
}
