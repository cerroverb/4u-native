// Derived from Azahar src/video_core/renderer_software/sw_proctex.cpp (ProcTex, LookupLUT, NoiseRand1D, NoiseRand2D, NoiseCoef,
// GetShiftOffset, ClampCoord and CombineAndMap: the oracle below), src/video_core/shader/generator/glsl_fs_shader_gen.cpp
// (DefineProcTexSampler: where the oracle follows the GLSL instead, the clamp of the map lookups and the mipmap levels with their
// LOD), src/common/vector_math.h (BilinearInterp: the order of the noise's interpolation), src/video_core/pica/pica_core.h
// (ProcTex::ValueEntry, ColorEntry and ColorDifferenceEntry: the entries the tables are written in) and
// src/video_core/pica/regs_texturing.h (the procedural texture registers and the TEV stage words) at
// 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-3.0-or-later: glsl_fs_shader_gen.cpp says "Licensed under GPLv3 or any later
// version"; sw_proctex.cpp, vector_math.h, pica_core.h and regs_texturing.h say "Licensed under GPLv2 or any later version").
// The rig below (Tri, Vertex, Rig, the register helpers) is adapted from N2-10's fog test in this directory (GPL-3.0-or-later, see
// its header for what it takes from Azahar).
// Copyright 2017 Citra Emulator Project (sw_proctex.cpp)
// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project (glsl_fs_shader_gen.cpp, pica_core.h)
// Copyright 2015-2025 Citra Emulator Project / Azahar Emulator Project (vector_math.h)
// Copyright 2017-2024 Citra Emulator Project / Azahar Emulator Project (regs_texturing.h)
//
// The procedural texture (N2-11), on whatever Vulkan device the machine has (MH4N_VULKAN_DEVICE; a case that needs one skips with a
// message without it and fails under MH4N_REQUIRE_VULKAN=1).
//
// Every draw is one triangle over an 8x8 target whose texture coordinates are ramps, with TEV stage 0 = the procedural slot
// (Texture3), Replace. The expected pixels come from a CPU port of the SOFTWARE renderer's ProcTex, with the GLSL's two choices
// (the map lookups clamped to [0, 1], and the mipmap levels chosen by the LOD, which the software renderer leaves as a TODO). The
// software renderer truncates the looked-up colour to a byte and the shader rounds it, so a pixel may differ by one step. A pixel
// whose coordinate lies within 1e-3 of a decision of the unit (a clamp's edge, a shift's parity, a noise cell, a nearest index or a
// level) is not compared; most of each draw is. Every draw is also rendered by the ubershader and must agree with the specialised
// shader.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
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

constexpr u32 kSize = 8;  // the targets are 8x8

// ---- registers --------------------------------------------------------------------------------------------------------

constexpr u32 kRegDepthRange = 0x4D, kRegDepthNear = 0x4E, kRegDepthmap = 0x6D;
constexpr u32 kRegMainConfig = 0x80;
constexpr u32 kRegProcTex = 0xA8, kRegNoiseU = 0xA9, kRegNoiseV = 0xAA, kRegNoiseFrequency = 0xAB, kRegLut = 0xAC, kRegLutOffset = 0xAD;
constexpr u32 kRegTev0 = 0xC0;
constexpr u32 kRegLogicOp = 0x102, kRegDepthColorMask = 0x107, kRegAllowColor = 0x113, kRegAllowDepth = 0x115;
constexpr u32 kTexture3 = 6, kPrevious = 15;

// Float24 (1 sign, 7 exponent, 16 mantissa bits) of a normal float.
u32 F24(float f) {
    if (f == 0.0f) return 0;
    u32 bits;
    std::memcpy(&bits, &f, 4);
    const u32 sign = bits >> 31;
    const int exponent = static_cast<int>((bits >> 23) & 0xFF) - 127 + 63;
    return (sign << 23) | (static_cast<u32>(exponent) << 16) | ((bits >> 7) & 0xFFFF);
}

// Binary16 of a normal float (the mantissa truncated) and back: the oracle uses the value the register holds.
u32 F16(float f) {
    if (f == 0.0f) return 0;
    u32 bits;
    std::memcpy(&bits, &f, 4);
    const u32 sign = (bits >> 31) << 15;
    const int exponent = static_cast<int>((bits >> 23) & 0xFF) - 127 + 15;
    return sign | (static_cast<u32>(exponent) << 10) | ((bits >> 13) & 0x3FF);
}
float H(u32 h) {
    if ((h & 0x7FFF) == 0) return 0.0f;
    const u32 bits = ((h >> 15) << 31) | ((((h >> 10) & 0x1F) + 127 - 15) << 23) | ((h & 0x3FF) << 13);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// ---- the procedural texture's configuration and tables ------------------------------------------------------------------

struct Config {
    u32 coord = 0;          // texcoord0, 1, 2
    u32 u_clamp = 1, v_clamp = 1;  // ToEdge
    u32 color_combiner = 0, alpha_combiner = 0;
    bool separate_alpha = false;
    bool noise = false;
    u32 u_shift = 0, v_shift = 0;
    u32 filter = 1;         // Linear
    u32 lod_min = 0, lod_max = 7;
    u32 width = 128;
    std::array<u32, 4> offsets = {0, 128, 192, 224};
    u32 bias = 0;           // binary16
    int amplitude_u = 0, amplitude_v = 0;          // fixed 1.3.12
    u32 phase_u = 0, phase_v = 0, freq_u = 0, freq_v = 0;  // binary16
};

// ProcTex::ValueEntry: value bits 0-11, difference bits 12-23 (signed).
u32 ValueEntry(int value, int difference) {
    return (static_cast<u32>(value) & 0xFFF) | ((static_cast<u32>(difference) & 0xFFF) << 12);
}
// A 128-entry table of f on [0, 1]: value[i] = f(i / 128), the difference the step to f((i + 1) / 128).
std::array<u32, 128> ValueTable(const std::function<double(double)>& f) {
    std::array<u32, 128> table{};
    for (int i = 0; i < 128; ++i) {
        const int value = static_cast<int>(std::lround(4095.0 * f(i / 128.0)));
        const int next = static_cast<int>(std::lround(4095.0 * f((i + 1) / 128.0)));
        table[i] = ValueEntry(value, next - value);
    }
    return table;
}

// The colour table: even bytes in each channel, so the half differences are exact; entry i + 1 follows entry i (256 wraps to 0).
std::array<int, 4> ColorOf(int i, int seed) {
    return {(2 * i + seed) & 0xFE, (254 - 2 * i + 3 * seed) & 0xFE, (6 * i + 5 * seed) & 0xFE, (128 + 4 * i + seed) & 0xFE};
}

struct Tables {
    std::array<u32, 128> noise{}, color_map{}, alpha_map{};
    std::array<u32, 256> color{}, diff{};
};

Tables MakeTables(int seed = 0) {
    Tables t;
    t.noise = ValueTable([](double x) { return x * x * (3.0 - 2.0 * x); });
    t.color_map = ValueTable([](double x) { return std::pow(x, 0.8); });
    t.alpha_map = ValueTable([](double x) { return 1.0 - 0.9 * x; });
    for (int i = 0; i < 256; ++i) {
        const std::array<int, 4> c = ColorOf(i, seed), n = ColorOf((i + 1) & 0xFF, seed);
        u32 color = 0, diff = 0;
        for (int k = 0; k < 4; ++k) {
            color |= static_cast<u32>(c[k]) << (8 * k);
            diff |= (static_cast<u32>((n[k] - c[k]) / 2) & 0xFF) << (8 * k);
        }
        t.color[i] = color;
        t.diff[i] = diff;
    }
    return t;
}

// ---- the oracle: sw_proctex.cpp, with the GLSL's map clamp and mipmaps --------------------------------------------------

struct Oracle {
    const Config& c;
    const Tables& t;
    double margin = 1e9;  // the smallest distance of a decision to its edge, for the pixel just computed

    void Edge(double x, double edge) { margin = std::min(margin, std::fabs(x - edge)); }
    void Integer(double x) { Edge(x, std::round(x)); }

    static float ValueOf(u32 entry) { return static_cast<float>(entry & 0xFFF) / 4095.0f; }
    static float DiffOf(u32 entry) { return static_cast<float>(static_cast<s32>(entry << 8) >> 20) / 4095.0f; }

    // LookupLUT, clamped as ProcTexLookupLUT clamps it.
    float LookupLUT(const std::array<u32, 128>& lut, float coord) const {
        coord *= 128.0f;
        const int index = std::min(static_cast<int>(coord), 127);
        const float frac = coord - static_cast<float>(index);
        return std::clamp(ValueOf(lut[index]) + frac * DiffOf(lut[index]), 0.0f, 1.0f);
    }

    static unsigned NoiseRand1D(unsigned v) {
        static constexpr std::array<unsigned, 16> table{{0, 4, 10, 8, 4, 9, 7, 12, 5, 15, 13, 14, 11, 15, 2, 11}};
        return ((v % 9 + 2) * 3 & 0xF) ^ table[(v / 9) & 0xF];
    }
    static float NoiseRand2D(unsigned x, unsigned y) {
        static constexpr std::array<unsigned, 16> table{{10, 2, 15, 8, 0, 7, 4, 5, 5, 13, 2, 6, 13, 9, 3, 14}};
        unsigned u2 = NoiseRand1D(x);
        unsigned v2 = NoiseRand1D(y);
        v2 += ((u2 & 3) == 1) ? 4 : 0;
        v2 ^= (u2 & 1) * 6;
        v2 += 10 + u2;
        v2 &= 0xF;
        v2 ^= table[u2];
        return -1.0f + static_cast<float>(v2) * 2.0f / 15.0f;
    }
    float NoiseCoef(float u, float v) {
        const float x = 9 * H(c.freq_u) * std::fabs(u + H(c.phase_u));
        const float y = 9 * H(c.freq_v) * std::fabs(v + H(c.phase_v));
        Integer(x);
        Integer(y);
        const int x_int = static_cast<int>(x), y_int = static_cast<int>(y);
        const float x_frac = x - static_cast<float>(x_int), y_frac = y - static_cast<float>(y_int);
        const float g0 = NoiseRand2D(x_int, y_int) * (x_frac + y_frac);
        const float g1 = NoiseRand2D(x_int + 1, y_int) * (x_frac + y_frac - 1);
        const float g2 = NoiseRand2D(x_int, y_int + 1) * (x_frac + y_frac - 1);
        const float g3 = NoiseRand2D(x_int + 1, y_int + 1) * (x_frac + y_frac - 2);
        const float x_noise = LookupLUT(t.noise, x_frac);
        const float y_noise = LookupLUT(t.noise, y_frac);
        const float y0 = g0 + (g1 - g0) * x_noise;  // BilinearInterp: Lerp(x00, x01, s), Lerp(x10, x11, s), Lerp(y0, y1, t)
        const float y1 = g2 + (g3 - g2) * x_noise;
        return y0 + (y1 - y0) * y_noise;
    }

    float ShiftOffset(float v, u32 mode, u32 clamp_mode) {
        const float offset = clamp_mode == 3 ? 1.0f : 0.5f;
        if (mode == 1 || mode == 2) Integer(v);
        switch (mode) {
        case 1: return offset * static_cast<float>((static_cast<int>(v) / 2) % 2);
        case 2: return offset * static_cast<float>(((static_cast<int>(v) + 1) / 2) % 2);
        default: return 0.0f;
        }
    }

    void Clamp(float& coord, u32 mode) {
        switch (mode) {
        case 0: Edge(coord, 1.0); if (coord > 1.0f) coord = 0.0f; break;
        case 1: coord = std::min(coord, 1.0f); break;
        case 2: Integer(coord); coord = coord - std::floor(coord); break;
        case 3: {
            const int integer = static_cast<int>(coord);
            const float frac = coord - static_cast<float>(integer);
            coord = (integer % 2) == 0 ? frac : (1.0f - frac);
            break;
        }
        case 4: Edge(coord, 0.5); coord = coord <= 0.5f ? 0.0f : 1.0f; break;
        default: coord = std::min(coord, 1.0f); break;
        }
    }

    float CombineAndMap(float u, float v, u32 combiner, const std::array<u32, 128>& map) const {
        float f;
        switch (combiner) {
        case 0: f = u; break;
        case 1: f = u * u; break;
        case 2: f = v; break;
        case 3: f = v * v; break;
        case 4: f = (u + v) * 0.5f; break;
        case 5: f = (u * u + v * v) * 0.5f; break;
        case 6: f = std::min(std::sqrt(u * u + v * v), 1.0f); break;
        case 7: f = std::min(u, v); break;
        case 8: f = std::max(u, v); break;
        case 9: f = std::min(((u + v) * 0.5f + std::sqrt(u * u + v * v)) * 0.5f, 1.0f); break;
        default: f = 0.0f; break;
        }
        return LookupLUT(map, f);
    }

    // The colour table at a level (bytes, as floats): SampleProcTexColor. Level 0 is the software renderer's lookup.
    std::array<float, 4> Sample(float lut_coord, int level) {
        static constexpr u32 kHigh[4] = {0xF0, 0xF8, 0xFC, 0xFE};
        const int width = static_cast<int>(c.width) >> level;
        const int offset = static_cast<int>(level < 4 ? c.offsets[level] : kHigh[std::min(level, 7) - 4]);
        const float index = lut_coord * static_cast<float>(width - 1);
        std::array<float, 4> out{};
        const bool linear = c.filter == 1 || c.filter == 3 || c.filter == 5;
        if (linear) {
            const int i = static_cast<int>(index) + offset;
            const float frac = index - std::floor(index);
            for (int k = 0; k < 4; ++k) {
                const float value = static_cast<float>((t.color[i] >> (8 * k)) & 0xFF);
                const float diff = static_cast<float>(static_cast<s32>(t.diff[i] << (24 - 8 * k)) >> 24) * 2.0f;
                out[k] = value + frac * diff;
            }
        } else {
            Edge(index - std::floor(index), 0.5);
            const int i = static_cast<int>(std::round(index + static_cast<float>(offset)));
            for (int k = 0; k < 4; ++k) out[k] = static_cast<float>((t.color[i] >> (8 * k)) & 0xFF);
        }
        return out;
    }

    // The pixel (bytes, as doubles) at coordinate (s, t) of the chosen pair; (dx, dy) are the LOD's duv.
    std::array<double, 4> Pixel(float s, float tt, float duv_x, float duv_y) {
        margin = 1e9;
        float u = std::fabs(s), v = std::fabs(tt);
        // the GLSL's LOD (the software renderer has none)
        float lod = std::log2(std::fabs(static_cast<float>(c.width) * H(c.bias)) * (duv_x + duv_y));
        if (H(c.bias) == 0.0f) lod = 0.0f;
        lod = std::clamp(lod, static_cast<float>(c.lod_min), std::min(7.0f, static_cast<float>(c.lod_max)));

        const float u_shift = ShiftOffset(v, c.u_shift, c.u_clamp);
        const float v_shift = ShiftOffset(u, c.v_shift, c.v_clamp);
        if (c.noise) {
            const float noise = NoiseCoef(u, v);
            u += noise * static_cast<float>(c.amplitude_u) / 4095.0f;
            v += noise * static_cast<float>(c.amplitude_v) / 4095.0f;
            u = std::fabs(u);
            v = std::fabs(v);
        }
        u += u_shift;
        v += v_shift;
        Clamp(u, c.u_clamp);
        Clamp(v, c.v_clamp);
        const float lut_coord = CombineAndMap(u, v, c.color_combiner, t.color_map);

        std::array<float, 4> color;
        if (c.filter == 2 || c.filter == 3) {
            Edge(lod - std::floor(lod), 0.5);
            color = Sample(lut_coord, static_cast<int>(std::round(lod)));
        } else if (c.filter == 4 || c.filter == 5) {
            const int lod_i = static_cast<int>(lod);
            const float lod_f = lod - std::floor(lod);
            const std::array<float, 4> a = Sample(lut_coord, lod_i), b = Sample(lut_coord, lod_i + 1);
            for (int k = 0; k < 4; ++k) color[k] = a[k] + (b[k] - a[k]) * lod_f;
        } else {
            color = Sample(lut_coord, 0);
        }
        std::array<double, 4> out{};
        for (int k = 0; k < 4; ++k) out[k] = std::clamp(static_cast<double>(color[k]), 0.0, 255.0);
        if (c.separate_alpha) {
            out[3] = 255.0 * CombineAndMap(u, v, c.alpha_combiner, t.alpha_map);
        }
        return out;
    }
};

// ---- the draws --------------------------------------------------------------------------------------------------------

// The coordinate at the centre of pixel (px, py): u0 + ux (px + 0.5) / 8 + uy (py + 0.5) / 8, and the same for v.
struct Ramp {
    double u0 = 0, ux = 0, uy = 0, v0 = 0, vx = 0, vy = 0;
    double U(u32 px, u32 py) const { return u0 + ux * (px + 0.5) / kSize + uy * (py + 0.5) / kSize; }
    double V(u32 px, u32 py) const { return v0 + vx * (px + 0.5) / kSize + vy * (py + 0.5) / kSize; }
};
// The pair a draw does not read: constant, and far from the ramps.
constexpr Ramp kDecoy{0.9, 0, 0, 0.1, 0, 0};

// A PICA state with stage 0 = the procedural slot (Replace), the other stages pass-through, colour writes on, no depth, no blending,
// no lighting, the procedural unit on with `c` and `tables`.
std::shared_ptr<pica::PicaState> StateOf(const Config& c, const Tables& tables) {
    auto state = std::make_shared<pica::PicaState>();
    state->regs.fill(0);
    state->dirty.SetAll();
    state->regs[kRegTev0 + 0] = kTexture3 | (kPrevious << 4) | (kPrevious << 8) | (kTexture3 << 16) | (kPrevious << 20) | (kPrevious << 24);
    for (u32 base : {0xC8u, 0xD0u, 0xD8u, 0xF0u, 0xF8u}) {
        state->regs[base + 0] = 15u | (15u << 4) | (15u << 8) | (15u << 16) | (15u << 20) | (15u << 24);
    }
    state->regs[kRegLogicOp] = static_cast<u32>(re::LogicOp::Copy);
    state->regs[kRegDepthColorMask] = 0xF00;
    state->regs[kRegAllowColor] = 0xF;
    state->regs[kRegAllowDepth] = 0;
    state->regs[0x1C6] = 1;  // lighting disabled
    state->regs[kRegDepthRange] = F24(-1.0f);
    state->regs[kRegDepthNear] = 0;
    state->regs[kRegDepthmap] = 1;

    state->regs[kRegMainConfig] = (1u << 10) | (c.coord << 8);
    state->regs[kRegProcTex] = c.u_clamp | (c.v_clamp << 3) | (c.color_combiner << 6) | (c.alpha_combiner << 10) |
                               (c.separate_alpha ? 1u << 14 : 0u) | (c.noise ? 1u << 15 : 0u) | (c.u_shift << 16) | (c.v_shift << 18) |
                               ((c.bias & 0xFF) << 20);
    state->regs[kRegNoiseU] = (static_cast<u32>(c.amplitude_u) & 0xFFFF) | (c.phase_u << 16);
    state->regs[kRegNoiseV] = (static_cast<u32>(c.amplitude_v) & 0xFFFF) | (c.phase_v << 16);
    state->regs[kRegNoiseFrequency] = c.freq_u | (c.freq_v << 16);
    state->regs[kRegLut] = c.filter | (c.lod_min << 3) | (c.lod_max << 7) | (c.width << 11) | ((c.bias >> 8) << 19);
    state->regs[kRegLutOffset] = c.offsets[0] | (c.offsets[1] << 8) | (c.offsets[2] << 16) | (c.offsets[3] << 24);
    state->proctex.noise_table = tables.noise;
    state->proctex.color_map_table = tables.color_map;
    state->proctex.alpha_map_table = tables.alpha_map;
    state->proctex.color_table = tables.color;
    state->proctex.color_diff_table = tables.diff;
    state->proctex.table_dirty = pica::ProcTex::kAllDirty;
    return state;
}

using cpuvertex::OutputVertex;

struct Tri {
    OutputVertex v[3];
};

// The triangle over the target, the chosen pair carrying `ramp` and the other two the decoy. Its vertices are (0, 0), (2, 0) and
// (0, 2) in units of the target, so an attribute is a0 + 2 fx ax + 2 fy ay there.
Tri RampTriangle(const Config& c, const Ramp& ramp) {
    Tri tri{};
    const float pos[3][2] = {{-1, -1}, {3, -1}, {-1, 3}};
    const double f[3][2] = {{0, 0}, {1, 0}, {0, 1}};
    for (int i = 0; i < 3; ++i) {
        OutputVertex& v = tri.v[i];
        v.pos[0] = pos[i][0]; v.pos[1] = pos[i][1]; v.pos[2] = -0.5f; v.pos[3] = 1.0f;
        v.quat[3] = 1.0f;
        for (float& channel : v.color) channel = 1.0f;
        v.tc0_w = 1.0f;
        float* pairs[3] = {v.tc0, v.tc1, v.tc2};
        const u32 chosen = c.coord < 3 ? c.coord : 0;  // 3 is not expected: the shader reads texcoord0
        for (u32 p = 0; p < 3; ++p) {
            const Ramp& r = p == chosen ? ramp : kDecoy;
            pairs[p][0] = static_cast<float>(r.u0 + f[i][0] * 2 * r.ux + f[i][1] * 2 * r.uy);
            pairs[p][1] = static_cast<float>(r.v0 + f[i][0] * 2 * r.vx + f[i][1] * 2 * r.vy);
        }
    }
    return tri;
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
    Tri tri;
};

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
        auto vr = backend->CreateRing(1 << 20, rhi::BufferUsage::Vertex, "proctex vertices");
        auto sr = backend->CreateRing(1 << 20, rhi::BufferUsage::Uniform, "proctex vs uniforms");
        REQUIRE(vr.ok());
        REQUIRE(sr.ok());
        vertex_ring = std::move(*vr);
        vs_ring = std::move(*sr);
        rhi::ImageDesc desc;
        desc.format = rhi::Format::Rgba8Unorm;
        desc.width = desc.height = kSize;
        desc.name = "proctex colour";
        auto image = backend->CreateImage(desc);
        REQUIRE_MESSAGE(image.ok(), image.error());
        color = *image;
    }

    // Clears the target, runs the draws in one recorder through one FragmentState (the real path: the key from the registers, the
    // uniforms, the tables), and returns the colour pixels.
    std::vector<u8> Render(const std::vector<DrawSpec>& specs) {
        rhi::Recorder rec = backend->BeginRecording();
        composer->BeginRecording(rec);
        uniforms->BeginRecording();
        FragmentState fragment(profile, FeatureRegistry::Global());
        bool first = true;
        for (const DrawSpec& spec : specs) {
            auto prepared = fragment.Prepare(*spec.state, {}, tables.get());
            REQUIRE_MESSAGE(prepared.ok(), prepared.error());
            auto alloc = uniforms->Write(fragment.block(), prepared->block_changed);
            REQUIRE_MESSAGE(alloc.ok(), alloc.error());

            cpuvertex::TriangleBatch batch;
            batch.AddTriangle(spec.tri.v[0], spec.tri.v[1], spec.tri.v[2]);
            auto streamed = cpuvertex::UploadBatch(*vertex_ring, batch);
            REQUIRE_MESSAGE(streamed.ok(), streamed.error());
            auto vs = vs_ring->Map(2048, std::max<u64>(256, backend->Caps().uniform_buffer_offset_alignment));
            REQUIRE_MESSAGE(vs.ok(), vs.error());
            std::memset(vs->data.data(), 0, 2048);
            REQUIRE(vs_ring->Commit(2048).ok());

            rhi::TargetPassDesc pass;
            pass.color = color.get();
            pass.area = rhi::Rect{0, 0, kSize, kSize};
            const fbops::DynamicState dynamic = fbops::MakeDynamicState(*spec.state);
            if (first) {
                rhi::TargetPassDesc clear_pass = pass;
                clear_pass.color_load = rhi::LoadOp::Clear;
                clear_pass.clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
                clear_pass.depth_load = rhi::LoadOp::Clear;
                clear_pass.clear_depth = 1.0f;
                composer->BeginTargetPass(rec, clear_pass);
                fbops::ApplyDynamicState(rec, dynamic, false);
                first = false;
            }
            const FragmentKey key = fragment.key();
            native::DrawDesc d;
            d.vertex.kind = native::VertexStageKind::Trivial;
            d.vertex.input = InputKey();
            d.fragment = &key;
            d.needs = fragment.needs();
            d.state = fbops::MakeFixedFunctionState(*spec.state);
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
        }
        backend->WaitIdle(backend->Submit(std::move(rec)));
        auto pixels = backend->ReadImage(*color, 0, 0, kSize, kSize);
        REQUIRE_MESSAGE(pixels.ok(), pixels.error());
        return *pixels;
    }
};

struct Case {
    std::string name;
    Config config;
    Ramp ramp;
};

// Renders the case in both modes and compares every pixel away from a decision with the oracle (within one step), and the two modes
// with each other (within one step). `tables` defaults to MakeTables().
void CheckCase(Rig& spec_rig, Rig& uber_rig, const Case& c, const Tables& tables = MakeTables()) {
    INFO(c.name);
    const DrawSpec draw{StateOf(c.config, tables), RampTriangle(c.config, c.ramp)};
    const std::vector<u8> a = spec_rig.Render({draw});
    const std::vector<u8> b = uber_rig.Render({draw});
    // duv: the derivatives of |coordinate| across a pixel (the ramps of the LOD cases stay positive)
    const float duv_x = static_cast<float>(std::max(std::fabs(c.ramp.ux), std::fabs(c.ramp.uy)) / kSize);
    const float duv_y = static_cast<float>(std::max(std::fabs(c.ramp.vx), std::fabs(c.ramp.vy)) / kSize);
    int compared = 0;
    int worst_a = 0, worst_b = 0, worst_ab = 0;
    std::string where_a, where_b;
    for (u32 py = 0; py < kSize; ++py) {
        for (u32 px = 0; px < kSize; ++px) {
            Oracle oracle{c.config, tables};
            const std::array<double, 4> want = oracle.Pixel(static_cast<float>(c.ramp.U(px, py)), static_cast<float>(c.ramp.V(px, py)), duv_x, duv_y);
            const size_t i = (static_cast<size_t>(py) * kSize + px) * 4;
            for (int k = 0; k < 4; ++k) worst_ab = std::max(worst_ab, std::abs(int(a[i + k]) - int(b[i + k])));
            if (oracle.margin < 1e-3) continue;
            ++compared;
            for (int k = 0; k < 4; ++k) {
                const int da = static_cast<int>(std::ceil(std::fabs(a[i + k] - want[k]) - 1e-3));
                const int db = static_cast<int>(std::ceil(std::fabs(b[i + k] - want[k]) - 1e-3));
                auto note = [&](int d, int& worst, std::string& where, const std::vector<u8>& p) {
                    if (d > worst) {
                        worst = d;
                        where = "pixel (" + std::to_string(px) + "," + std::to_string(py) + ") channel " + std::to_string(k) + " is " +
                                std::to_string(p[i + k]) + ", the oracle says " + std::to_string(want[k]);
                    }
                };
                note(da, worst_a, where_a, a);
                note(db, worst_b, where_b, b);
            }
        }
    }
    CHECK_MESSAGE(compared >= 48, c.name << ": only " << compared << " pixels away from a decision");
    CHECK_MESSAGE(worst_a <= 1, c.name << " [specialised]: " << where_a);
    CHECK_MESSAGE(worst_b <= 1, c.name << " [ubershader]: " << where_b);
    CHECK_MESSAGE(worst_ab <= 1, c.name << ": the specialised shader and the ubershader differ by " << worst_ab);
}

// u along x and v along y, inside (0, 1).
constexpr Ramp kUnit{0.02, 0.96, 0, 0.03, 0, 0.94};
// u along x over (0.1, 3.4), v along y over (0.2, 3.9): past the edges the clamps act on.
constexpr Ramp kWide{0.1, 3.3, 0, 0.2, 0, 3.7};
// both coordinates along both axes
constexpr Ramp kDiagonal{0.05, 0.6, 0.3, 0.07, 0.25, 0.65};

}  // namespace

// ---- without a device ---------------------------------------------------------------------------------------------------

TEST_CASE("proctex/no_draw_is_reported_stubbed_and_the_needs_name_the_two_procedural_tables") {
    const FeatureModule* feature = FeatureRegistry::Global().Find("proctex");
    REQUIRE(feature != nullptr);
    FragmentKey key = MakeKey(*StateOf(Config{}, MakeTables()));
    REQUIRE(key.proctex.enable() == 1);
    CHECK(TevUsesSource(key, kTexture3));
    CHECK(feature->Stubbed(key) == nullptr);
    const Needs needs = feature->NeedsOf(key, DeviceProfile{});
    CHECK(needs.lut_rg);
    CHECK(needs.lut_rgba);
    CHECK_FALSE(needs.lut_lf);
    key.proctex = ProcTexConfig{};
    CHECK_FALSE(feature->NeedsOf(key, DeviceProfile{}).lut_rg);
}

TEST_CASE("proctex/the_uniforms_decode_the_noise_and_bias_registers") {
    Config c;
    c.noise = true;
    c.amplitude_u = 2048;   // 0.5
    c.amplitude_v = -1024;  // -0.25
    c.phase_u = F16(0.375f);
    c.phase_v = F16(-1.5f);
    c.freq_u = F16(0.75f);
    c.freq_v = F16(2.0f);
    c.bias = F16(0.0390625f);  // 5 / 128: the low byte in 0xA8, the high byte in 0xAC
    FragmentState fragment(DeviceProfile{});
    auto prepared = fragment.Prepare(*StateOf(c, MakeTables()), {}, nullptr);
    REQUIRE(prepared.ok());
    const FragmentUniforms& u = fragment.block().uniforms;
    CHECK(u.proctex_noise_a.x == doctest::Approx(2048.0 / 4095.0));
    CHECK(u.proctex_noise_a.y == doctest::Approx(-1024.0 / 4095.0));
    CHECK(u.proctex_noise_p.x == 0.375f);
    CHECK(u.proctex_noise_p.y == -1.5f);
    CHECK(u.proctex_noise_f.x == 0.75f);
    CHECK(u.proctex_noise_f.y == 2.0f);
    CHECK(u.proctex_bias == 0.0390625f);
}

TEST_CASE("proctex/the_shaders_compile_in_both_forms_and_the_specialised_one_carries_only_what_the_key_uses") {
    DeviceProfile profile;
    std::vector<Config> configs(6);
    configs[1].noise = true;
    configs[2].filter = 5;
    configs[2].separate_alpha = true;
    configs[3].filter = 2;
    configs[3].u_shift = 1;
    configs[3].v_shift = 2;
    configs[3].u_clamp = 3;
    configs[4].filter = 6;  // not a PICA filter: Nearest at level 0
    configs[4].color_combiner = 12;  // not a PICA combiner: 0.0
    configs[4].u_clamp = 6;  // not a PICA clamp: ToEdge
    configs[5].coord = 3;  // not expected: texcoord0
    for (const Config& c : configs) {
        const FragmentKey key = MakeKey(*StateOf(c, MakeTables()));
        const Generated g = GenerateSpecialised(key, profile, ShaderPass::Normal);
        CHECK_MESSAGE(g.ok(), g.error);
    }
    const std::string plain = GenerateSpecialisedGlsl(MakeKey(*StateOf(configs[0], MakeTables())), profile, ShaderPass::Normal);
    CHECK(plain.find("ProcTexNoiseCoef") == std::string::npos);
    CHECK(plain.find("proctex_value = ProcTexEvaluate();") != std::string::npos);
    const std::string noisy = GenerateSpecialisedGlsl(MakeKey(*StateOf(configs[1], MakeTables())), profile, ShaderPass::Normal);
    CHECK(noisy.find("uv += proctex_noise_a * ProcTexNoiseCoef(uv);") != std::string::npos);
    // the unit off: the prototype's function returns zero and nothing of the unit is emitted
    auto off = StateOf(Config{}, MakeTables());
    off->regs[kRegMainConfig] = 0;
    const std::string none = GenerateSpecialisedGlsl(MakeKey(*off), profile, ShaderPass::Normal);
    CHECK(none.find("vec4 ProcTex() { return vec4(0.0); }") != std::string::npos);
    CHECK(none.find("ProcTexEvaluate") == std::string::npos);

    const Generated uber = GenerateUber(ShaderPass::Normal, profile);
    CHECK_MESSAGE(uber.ok(), uber.error);
    CHECK(uber.glsl.find("ProcTexNoiseCoef") != std::string::npos);
    const Generated uber_shadow = GenerateUber(ShaderPass::ShadowWrite, profile);
    CHECK_MESSAGE(uber_shadow.ok(), uber_shadow.error);
}

// ---- on a device -------------------------------------------------------------------------------------------------------

TEST_CASE("proctex/every_combiner_through_the_colour_map_and_the_linear_colour_table") {
    auto backend = test::RequireBackend();
    if (!backend) return;
    Rig spec_rig(backend, false), uber_rig(backend, true);
    for (u32 combiner = 0; combiner < 10; ++combiner) {
        Case c{"combiner " + std::to_string(combiner), {}, kDiagonal};
        c.config.color_combiner = combiner;
        CheckCase(spec_rig, uber_rig, c);
    }
    Case undefined{"combiner 11, not a PICA combiner: 0.0", {}, kUnit};
    undefined.config.color_combiner = 11;
    CheckCase(spec_rig, uber_rig, undefined);
}

TEST_CASE("proctex/every_clamp_mode_on_each_axis") {
    auto backend = test::RequireBackend();
    if (!backend) return;
    Rig spec_rig(backend, false), uber_rig(backend, true);
    for (u32 mode = 0; mode < 6; ++mode) {
        Case cu{"u clamp " + std::to_string(mode), {}, kWide};
        cu.config.u_clamp = mode;
        cu.config.color_combiner = 0;  // U
        CheckCase(spec_rig, uber_rig, cu);
        Case cv{"v clamp " + std::to_string(mode), {}, kWide};
        cv.config.v_clamp = mode;
        cv.config.color_combiner = 2;  // V
        CheckCase(spec_rig, uber_rig, cv);
    }
}

TEST_CASE("proctex/the_shifts_take_the_other_axis_and_the_mirrored_repeat_offset") {
    auto backend = test::RequireBackend();
    if (!backend) return;
    Rig spec_rig(backend, false), uber_rig(backend, true);
    for (u32 shift : {1u, 2u}) {
        for (u32 clamp : {2u, 3u}) {
            Case cu{"u shift " + std::to_string(shift) + ", u clamp " + std::to_string(clamp), {}, kWide};
            cu.config.u_shift = shift;
            cu.config.u_clamp = clamp;
            cu.config.color_combiner = 0;
            CheckCase(spec_rig, uber_rig, cu);
            Case cv{"v shift " + std::to_string(shift) + ", v clamp " + std::to_string(clamp), {}, kWide};
            cv.config.v_shift = shift;
            cv.config.v_clamp = clamp;
            cv.config.color_combiner = 2;
            CheckCase(spec_rig, uber_rig, cv);
        }
    }
}

TEST_CASE("proctex/separate_alpha_the_nearest_filter_and_the_coordinate_pairs") {
    auto backend = test::RequireBackend();
    if (!backend) return;
    Rig spec_rig(backend, false), uber_rig(backend, true);
    Case alpha{"separate alpha, alpha combiner Max through the alpha map", {}, kDiagonal};
    alpha.config.separate_alpha = true;
    alpha.config.color_combiner = 4;
    alpha.config.alpha_combiner = 8;
    CheckCase(spec_rig, uber_rig, alpha);

    Case nearest{"nearest, width 64 at offset 100", {}, kDiagonal};
    nearest.config.filter = 0;
    nearest.config.width = 64;
    nearest.config.offsets[0] = 100;
    nearest.config.color_combiner = 4;
    CheckCase(spec_rig, uber_rig, nearest);

    Case narrow{"linear, width 17 at offset 30", {}, kDiagonal};
    narrow.config.width = 17;
    narrow.config.offsets[0] = 30;
    narrow.config.color_combiner = 6;
    CheckCase(spec_rig, uber_rig, narrow);

    for (u32 coord : {0u, 1u, 2u, 3u}) {
        Case pair{"coordinate pair " + std::to_string(coord), {}, kDiagonal};
        pair.config.coord = coord;
        pair.config.color_combiner = 4;
        CheckCase(spec_rig, uber_rig, pair);
    }

    Case negative{"negative coordinates are taken as their absolute value", {}, Ramp{-0.95, 0.9, 0, -0.04, 0, -0.9}};
    negative.config.color_combiner = 4;
    CheckCase(spec_rig, uber_rig, negative);
}

TEST_CASE("proctex/the_noise_moves_the_coordinates_before_the_shift_and_the_clamp") {
    auto backend = test::RequireBackend();
    if (!backend) return;
    Rig spec_rig(backend, false), uber_rig(backend, true);
    Case noise{"noise", {}, kWide};
    noise.config.noise = true;
    noise.config.amplitude_u = 1000;
    noise.config.amplitude_v = -700;
    noise.config.phase_u = F16(0.125f);
    noise.config.phase_v = F16(0.25f);
    noise.config.freq_u = F16(0.5f);
    noise.config.freq_v = F16(0.75f);
    noise.config.u_clamp = 3;
    noise.config.v_clamp = 2;
    noise.config.color_combiner = 4;
    CheckCase(spec_rig, uber_rig, noise);

    Case shifted = noise;
    shifted.name = "noise with a u shift";
    shifted.config.u_shift = 1;
    shifted.config.amplitude_u = 300;
    shifted.config.freq_u = F16(0.25f);
    CheckCase(spec_rig, uber_rig, shifted);
}

TEST_CASE("proctex/the_mipmap_filters_choose_the_level_by_the_lod_and_its_clamp") {
    auto backend = test::RequireBackend();
    if (!backend) return;
    Rig spec_rig(backend, false), uber_rig(backend, true);
    // duv = (0.25, 0.25): lod = log2(128 * bias * 0.5)
    const Ramp ramp{0.02, 2.0, 0, 0.03, 0, 2.0};
    for (u32 filter : {2u, 3u, 4u, 5u}) {
        Case zero{"filter " + std::to_string(filter) + ", bias 0: level 0", {}, kDiagonal};
        zero.config.filter = filter;
        zero.config.color_combiner = 4;
        CheckCase(spec_rig, uber_rig, zero);

        Case lod{"filter " + std::to_string(filter) + ", lod log2(64 * 5 / 128) = 1.32", {}, ramp};
        lod.config.filter = filter;
        lod.config.bias = F16(0.0390625f);
        lod.config.u_clamp = 2;
        lod.config.v_clamp = 2;
        lod.config.color_combiner = 4;
        CheckCase(spec_rig, uber_rig, lod);

        Case high = lod;
        high.name = "filter " + std::to_string(filter) + ", lod log2(64 * 0.1) = 2.68, levels 2 and 3";
        high.config.bias = F16(0.1f);
        CheckCase(spec_rig, uber_rig, high);

        Case clamped = high;
        clamped.name = "filter " + std::to_string(filter) + ", lod 2.68 clamped to lod_max 1";
        clamped.config.lod_max = 1;
        CheckCase(spec_rig, uber_rig, clamped);

        Case raised = lod;
        raised.name = "filter " + std::to_string(filter) + ", lod 1.32 raised to lod_min 3";
        raised.config.lod_min = 3;
        CheckCase(spec_rig, uber_rig, raised);
    }
}

TEST_CASE("proctex/a_table_that_changes_is_uploaded_again") {
    auto backend = test::RequireBackend();
    if (!backend) return;
    Rig rig(backend, false);
    Case c{"seed 0 then seed 40", {}, kDiagonal};
    c.config.color_combiner = 4;
    const Tables first = MakeTables(0), second = MakeTables(40);
    auto state1 = StateOf(c.config, first);
    auto state2 = StateOf(c.config, second);
    state2->dirty.Reset();
    state2->proctex.table_dirty = 1u << 4 | 1u << 5;  // the colour table and its differences only
    const std::vector<u8> pixels = rig.Render({{state1, RampTriangle(c.config, c.ramp)}, {state2, RampTriangle(c.config, c.ramp)}});
    int worst = 0;
    for (u32 py = 0; py < kSize; ++py) {
        for (u32 px = 0; px < kSize; ++px) {
            Oracle oracle{c.config, second};
            const std::array<double, 4> want = oracle.Pixel(static_cast<float>(c.ramp.U(px, py)), static_cast<float>(c.ramp.V(px, py)), 0, 0);
            for (int k = 0; k < 4; ++k) {
                worst = std::max(worst, static_cast<int>(std::ceil(std::fabs(pixels[(py * kSize + px) * 4 + k] - want[k]) - 1e-3)));
            }
        }
    }
    CHECK(worst <= 1);
}
