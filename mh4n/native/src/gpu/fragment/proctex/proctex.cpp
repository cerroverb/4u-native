// Derived from Azahar src/video_core/shader/generator/glsl_fs_shader_gen.cpp (DefineProcTexSampler, AppendProcTexShiftOffset,
// AppendProcTexClamp and AppendProcTexCombineAndMap: ProcTexLookupLUT, the noise functions, SampleProcTexColor and ProcTex),
// src/video_core/renderer_vulkan/vk_rasterizer.cpp (SyncAndUploadLUTs: the five procedural tables in the `lut_rg` and `lut_rgba`
// texel buffers, their order and the element offsets kept in the uniforms), src/video_core/pica/pica_core.h (ProcTex::ValueEntry,
// ColorEntry and ColorDifferenceEntry: the entries' bit fields and scales), src/video_core/rasterizer_accelerated.cpp and
// src/video_core/pica/dirty_regs.h (SyncDrawUniforms and CheckProctex: the noise and bias uniforms and the registers that dirty
// them), src/video_core/pica_types.h (Float<10, 5>::FromRaw: the float16 of the noise and bias registers) and
// src/video_core/pica/regs_texturing.h (the procedural texture registers) at 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6
// (GPL-3.0-or-later: glsl_fs_shader_gen.cpp and vk_rasterizer.cpp say "Licensed under GPLv3 or any later version"; pica_core.h,
// rasterizer_accelerated.cpp, dirty_regs.h, pica_types.h and regs_texturing.h each say "Licensed under GPLv2 or any later
// version").
// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project (glsl_fs_shader_gen.cpp, vk_rasterizer.cpp,
//   pica_core.h, rasterizer_accelerated.cpp)
// Copyright 2025-2026 Citra Emulator Project / Azahar Emulator Project (dirty_regs.h)
// Copyright 2015-2026 Citra Emulator Project / Azahar Emulator Project (pica_types.h)
// Copyright 2017-2024 Citra Emulator Project / Azahar Emulator Project (regs_texturing.h)
//
// The `proctex/` feature (N2-11): the procedural texture unit (texture unit 3), in both modes of the emitter.
//
// The GLSL is Azahar's DefineProcTexSampler: the coordinate (abs of texcoord0, 1 or 2), the LOD (the bias inside the log2), the
// shift offsets taken before the noise, the noise (Azahar's NoiseRand1D/2D), the clamps, the colour combiner through the colour
// map, the colour table lookup (nearest or linear, levels 0 to 7, mipmaps by LOD), and the separate alpha through the alpha map.
// `sampling/` routes `sampleTexUnit3()` here where the key enables the unit.
//
// In the specialised mode the text is Azahar's, with the key's fields as literals. In the ubershader the same expressions
// choose at run time from `cfg` (nested `?:` over the same table entries, `if` for the statements), so one table feeds both
// modes.
//
// ONE EVALUATION PER FRAGMENT: Azahar's `ProcTex()` body is `ProcTexEvaluate()` here, called once at the `Prologue` point
// (where the unit is on) into the global `proctex_value`, and `ProcTex()` returns that global. In Azahar's text every source
// that reads the unit calls the whole function again; in the ubershader each of the 36 TEV operands goes through
// `tev_source`'s switch, and the procedural code inlined at every one of them made the Mali-G57's compiler run out of memory.
// The result is the same (the function reads only the varyings, the uniforms and the tables), and the LOD's derivatives are
// taken in main's uniform control flow, before any discard.
//
// What differs from Azahar's text, on purpose:
//   - the level index of `SampleProcTexColor` is clamped to 7 (`lut_offsets[min(level, 7)]`): with the mipmap-linear filters
//     and an LOD clamped at 7, Azahar's `lod_i + 1` reads `lut_offsets[8]`, outside the array (its weight is 0 there);
//   - a filter value the PICA does not define (6, 7) is sampled as Nearest at level 0, where Azahar's generator emits no lookup
//     at all (a shader that does not compile);
//   - a combiner the PICA does not define (10 to 15) gives 0.0, as Azahar's `LOG_CRITICAL` branch does, and a clamp mode it does
//     not define (5 to 7) is ToEdge, as Azahar's default;
//   - the clamps and the combiners are expressions (`u = <expression>;`) rather than Azahar's statements, the same arithmetic.
// The software renderer (sw_proctex.cpp, read for comparison, nothing copied) differs from the GLSL in two places where the
// GLSL's choice is kept: the map lookups are clamped to [0, 1], and the mipmap levels are used (the software renderer has a TODO).
#include <array>
#include <bit>
#include <cstddef>
#include <cstring>

#include "gpu/backend/rhi.h"
#include "gpu/fragment/emitter.h"
#include "gpu/fragment/feature.h"
#include "gpu/fragment/regs.h"
#include "gpu/fragment/tables.h"
#include "gpu/pica/state.h"

namespace mh4n::gpu::fragment {
namespace {

namespace kw = keyword;

// The words of ProcTexConfig in the key.
constexpr size_t kPtRaw = kw::kProcTexRaw;
constexpr size_t kPtWidth = kw::kProcTex + offsetof(ProcTexConfig, lut_width) / sizeof(u32);
constexpr size_t kPtOffset0 = kw::kProcTex + offsetof(ProcTexConfig, lut_offset0) / sizeof(u32);
constexpr size_t kPtLod = kw::kProcTex + offsetof(ProcTexConfig, lod_min) / sizeof(u32);  // lod_min bits 0-15, lod_max 16-31
static_assert(offsetof(ProcTexConfig, lod_min) % sizeof(u32) == 0);
static_assert(offsetof(ProcTexConfig, lod_max) == offsetof(ProcTexConfig, lod_min) + 2);
static_assert(offsetof(ProcTexConfig, lut_offset3) == offsetof(ProcTexConfig, lut_offset0) + 12);

// ProcTexLutTable values (the bits of PicaState::ProcTex::table_dirty).
constexpr u32 kTableNoise = 0, kTableColorMap = 2, kTableAlphaMap = 3, kTableColor = 4, kTableColorDiff = 5;

// ProcTexClamp, ProcTexShift, ProcTexFilter.
constexpr u32 kClampMirroredRepeat = 3;
constexpr u32 kShiftOdd = 1, kShiftEven = 2;
constexpr u32 kFilterLinear = 1, kFilterNearestMipmapNearest = 2, kFilterLinearMipmapNearest = 3,
              kFilterNearestMipmapLinear = 4, kFilterLinearMipmapLinear = 5;

// ---- the tables of the expressions (Azahar's switch statements; {0} is the operand) ------------------------------------

struct Case {
    u32 value;
    const char* text;
};

// AppendProcTexClamp, as an expression of `{0}` (the default, an undefined mode, is ToEdge's).
constexpr Case kClamps[] = {
    {0, "({0} > 1.0 ? 0.0 : {0})"},                              // ToZero
    {1, "min({0}, 1.0)"},                                        // ToEdge
    {2, "fract({0})"},                                           // SymmetricalRepeat
    {3, "(int({0}) % 2 == 0 ? fract({0}) : 1.0 - fract({0}))"},  // MirroredRepeat
    {4, "({0} > 0.5 ? 1.0 : 0.0)"},                              // Pulse
};
constexpr const char* kClampDefault = "min({0}, 1.0)";

// AppendProcTexCombineAndMap's combiners, of `u` and `v`.
constexpr Case kCombiners[] = {
    {0, "u"},                                                      // U
    {1, "(u * u)"},                                                // U2
    {2, "v"},                                                      // V
    {3, "(v * v)"},                                                // V2
    {4, "((u + v) * 0.5)"},                                        // Add
    {5, "((u * u + v * v) * 0.5)"},                                // Add2
    {6, "min(sqrt(u * u + v * v), 1.0)"},                          // SqrtAdd2
    {7, "min(u, v)"},                                              // Min
    {8, "max(u, v)"},                                              // Max
    {9, "min(((u + v) * 0.5 + sqrt(u * u + v * v)) * 0.5, 1.0)"},  // RMax
};
constexpr const char* kCombinerDefault = "0.0";

// The coordinate pair (ProcTexConfig::coord; 3 is not expected and reads texcoord0, as Azahar's).
constexpr Case kCoords[] = {
    {0, "texcoord0"},
    {1, "texcoord1"},
    {2, "texcoord2"},
};
constexpr const char* kCoordDefault = "texcoord0";

// The text of the table's entry for `value`: the entry itself where the value is known, else `?:` over every entry.
template <size_t N>
std::string Pick(const Emitter& e, const Value& value, const Case (&table)[N], const char* otherwise, std::string_view operand) {
    if (value.known) {
        for (const Case& c : table) {
            if (c.value == value.literal) {
                return fmt::format(fmt::runtime(c.text), operand);
            }
        }
        return fmt::format(fmt::runtime(otherwise), operand);
    }
    std::string out = "(";
    for (const Case& c : table) {
        out += fmt::format("{} ? {} : ", e.Eq(value, c.value).glsl, fmt::format(fmt::runtime(c.text), operand));
    }
    out += fmt::format(fmt::runtime(otherwise), operand);
    out += ")";
    return out;
}

// A key field as a GLSL int (the key's ints are small and non-negative).
std::string IntText(const Value& v) {
    return v.known ? fmt::format("{}", static_cast<s32>(v.literal)) : fmt::format("int({})", v.glsl);
}

// A key field as a GLSL float.
std::string FloatText(const Value& v) {
    return v.known ? fmt::format("{}.0", v.literal) : fmt::format("float({})", v.glsl);
}

// AppendProcTexShiftOffset: the shift of one axis, from the other axis's coordinate `v`.
std::string ShiftText(const Emitter& e, const Value& shift, const Value& clamp, std::string_view v) {
    const Cond mirrored = e.Eq(clamp, kClampMirroredRepeat);
    const std::string offset =
        mirrored.known ? std::string(mirrored.value ? "1.0" : "0.5") : fmt::format("({} ? 1.0 : 0.5)", mirrored.glsl);
    const std::string odd = fmt::format("{} * float((int({}) / 2) % 2)", offset, v);
    const std::string even = fmt::format("{} * float(((int({}) + 1) / 2) % 2)", offset, v);
    if (shift.known) {
        return shift.literal == kShiftOdd ? odd : shift.literal == kShiftEven ? even : std::string("0.0");
    }
    return fmt::format("({} ? {} : {} ? {} : 0.0)", e.Eq(shift, kShiftOdd).glsl, odd, e.Eq(shift, kShiftEven).glsl, even);
}

// ---- the GLSL ---------------------------------------------------------------------------------------------------------

// LUT sampling utility. For NoiseLUT/ColorMap/AlphaMap, coord=0.0 is lut[0], coord=127.0/128.0 is lut[127] and coord=1.0 is
// lut[127]+lut_diff[127]. For other indices, the result is interpolated using value entries and difference entries.
constexpr const char* kLookupLut = R"(
float ProcTexLookupLUT(int offset, float coord) {
    coord *= 128.0;
    float index_i = clamp(floor(coord), 0.0, 127.0);
    float index_f = coord - index_i; // fract() cannot be used here because 128.0 needs to be
                                     // extracted as index_i = 127.0 and index_f = 1.0
    vec2 entry = texture_buffer_lut_rg.data[int(index_i) + offset];
    return clamp(entry.r + entry.g * index_f, 0.0, 1.0);
}
)";

// Noise utility (see Azahar's renderer_software/sw_proctex.cpp for more information about these functions).
constexpr const char* kNoise = R"(
int ProcTexNoiseRand1D(int v) {
    const int table[] = int[](0,4,10,8,4,9,7,12,5,15,13,14,11,15,2,11);
    return ((v % 9 + 2) * 3 & 0xF) ^ table[(v / 9) & 0xF];
}

float ProcTexNoiseRand2D(vec2 point) {
    const int table[] = int[](10,2,15,8,0,7,4,5,5,13,2,6,13,9,3,14);
    int u2 = ProcTexNoiseRand1D(int(point.x));
    int v2 = ProcTexNoiseRand1D(int(point.y));
    v2 += ((u2 & 3) == 1) ? 4 : 0;
    v2 ^= (u2 & 1) * 6;
    v2 += 10 + u2;
    v2 &= 0xF;
    v2 ^= table[u2];
    return -1.0 + float(v2) * (2.0/15.0);
}

float ProcTexNoiseCoef(vec2 x) {
    vec2 grid  = 9.0 * proctex_noise_f * abs(x + proctex_noise_p);
    vec2 point = floor(grid);
    vec2 frac  = grid - point;

    float g0 = ProcTexNoiseRand2D(point) * (frac.x + frac.y);
    float g1 = ProcTexNoiseRand2D(point + vec2(1.0, 0.0)) * (frac.x + frac.y - 1.0);
    float g2 = ProcTexNoiseRand2D(point + vec2(0.0, 1.0)) * (frac.x + frac.y - 1.0);
    float g3 = ProcTexNoiseRand2D(point + vec2(1.0, 1.0)) * (frac.x + frac.y - 2.0);

    float x_noise = ProcTexLookupLUT(proctex_noise_lut_offset, frac.x);
    float y_noise = ProcTexLookupLUT(proctex_noise_lut_offset, frac.y);
    float x0 = mix(g0, g1, x_noise);
    float x1 = mix(g2, g3, x_noise);
    return mix(x0, x1, y_noise);
}
)";

void DefineSampleColor(Emitter& e) {
    const Value filter = e.Field(kPtRaw, 17, 3);
    e.Raw("vec4 SampleProcTexColor(float lut_coord, int level) {\n");
    e.Line("int lut_width = {} >> level;", IntText(e.Field(kPtWidth, 0, 32)));
    // Offsets for level 4-7 seem to be hardcoded
    e.Line("int lut_offsets[8] = int[]({}, {}, {}, {}, 0xF0, 0xF8, 0xFC, 0xFE);", IntText(e.Field(kPtOffset0, 0, 32)),
           IntText(e.Field(kPtOffset0 + 1, 0, 32)), IntText(e.Field(kPtOffset0 + 2, 0, 32)),
           IntText(e.Field(kPtOffset0 + 3, 0, 32)));
    e.Raw("int lut_offset = lut_offsets[min(level, 7)];\n");
    // For the color lut, coord=0.0 is lut[offset] and coord=1.0 is lut[offset+width-1]
    e.Raw("lut_coord *= float(lut_width - 1);\n");
    const Cond linear = e.Or(e.Eq(filter, kFilterLinear),
                             e.Or(e.Eq(filter, kFilterLinearMipmapLinear), e.Eq(filter, kFilterLinearMipmapNearest)));
    e.If(
        linear,
        [&] {
            e.Raw("int lut_index_i = int(lut_coord) + lut_offset;\n"
                  "float lut_index_f = fract(lut_coord);\n"
                  "return texture_buffer_lut_rgba.data[lut_index_i + proctex_lut_offset] + "
                  "lut_index_f * texture_buffer_lut_rgba.data[lut_index_i + proctex_diff_lut_offset];\n");
        },
        [&] {
            e.Raw("lut_coord += float(lut_offset);\n"
                  "return texture_buffer_lut_rgba.data[int(round(lut_coord)) + proctex_lut_offset];\n");
        });
    e.Raw("}\n");
}

void DefineProcTex(Emitter& e) {
    const Value coord = e.Field(kPtRaw, 1, 2);
    const Value u_clamp = e.Field(kPtRaw, 3, 3);
    const Value v_clamp = e.Field(kPtRaw, 6, 3);
    const Value color_combiner = e.Field(kPtRaw, 9, 4);
    const Value alpha_combiner = e.Field(kPtRaw, 13, 4);
    const Value filter = e.Field(kPtRaw, 17, 3);
    const Value separate_alpha = e.Field(kPtRaw, 20, 1);
    const Value noise_enable = e.Field(kPtRaw, 21, 1);
    const Value u_shift = e.Field(kPtRaw, 22, 2);
    const Value v_shift = e.Field(kPtRaw, 24, 2);
    const Value lod_min = e.Field(kPtLod, 0, 16);
    const Value lod_max = e.Field(kPtLod, 16, 16);

    e.Raw("vec4 ProcTexEvaluate() {\n");
    e.Line("vec2 uv = abs({});", Pick(e, coord, kCoords, kCoordDefault, ""));

    // This LOD formula is the same as the LOD upper limit defined in OpenGL.
    // f(x, y) <= m_u + m_v + m_w
    // (See OpenGL 4.6 spec, 8.14.1 - Scale Factor and Level-of-Detail)
    // Note: this is different from the one normal 2D textures use.
    e.Raw("vec2 duv = max(abs(dFdx(uv)), abs(dFdy(uv)));\n");
    // unlike normal texture, the bias is inside the log2
    e.Line("float lod = log2(abs(float({}) * proctex_bias) * (duv.x + duv.y));", IntText(e.Field(kPtWidth, 0, 32)));
    e.Raw("if (proctex_bias == 0.0) lod = 0.0;\n");
    // clamp(lod, max(0, lod_min), min(7, lod_max)): lod_min is unsigned.
    if (lod_max.known) {
        e.Line("lod = clamp(lod, {}, {}.0);", FloatText(lod_min), lod_max.literal < 7 ? lod_max.literal : 7u);
    } else {
        e.Line("lod = clamp(lod, {}, min(7.0, {}));", FloatText(lod_min), FloatText(lod_max));
    }

    // Get shift offset before noise generation
    e.Line("float u_shift = {};", ShiftText(e, u_shift, u_clamp, "uv.y"));
    e.Line("float v_shift = {};", ShiftText(e, v_shift, v_clamp, "uv.x"));

    // Generate noise
    e.If(e.Eq(noise_enable, 1), [&] {
        e.Raw("uv += proctex_noise_a * ProcTexNoiseCoef(uv);\n"
              "uv = abs(uv);\n");
    });

    // Shift
    e.Raw("float u = uv.x + u_shift;\n"
          "float v = uv.y + v_shift;\n");

    // Clamp
    e.Line("u = {};", Pick(e, u_clamp, kClamps, kClampDefault, "u"));
    e.Line("v = {};", Pick(e, v_clamp, kClamps, kClampDefault, "v"));

    // Combine and map
    e.Line("float lut_coord = ProcTexLookupLUT(proctex_color_map_offset, {});",
           Pick(e, color_combiner, kCombiners, kCombinerDefault, ""));

    e.Raw("vec4 final_color;\n");
    const Cond mip_nearest = e.Or(e.Eq(filter, kFilterNearestMipmapNearest), e.Eq(filter, kFilterLinearMipmapNearest));
    const Cond mip_linear = e.Or(e.Eq(filter, kFilterNearestMipmapLinear), e.Eq(filter, kFilterLinearMipmapLinear));
    e.If(
        mip_nearest, [&] { e.Raw("final_color = SampleProcTexColor(lut_coord, int(round(lod)));\n"); },
        [&] {
            e.If(
                mip_linear,
                [&] {
                    e.Raw("int lod_i = int(lod);\n"
                          "float lod_f = fract(lod);\n"
                          "final_color = mix(SampleProcTexColor(lut_coord, lod_i), SampleProcTexColor(lut_coord, lod_i + 1), "
                          "lod_f);\n");
                },
                [&] { e.Raw("final_color = SampleProcTexColor(lut_coord, 0);\n"); });
        });

    e.If(
        e.Eq(separate_alpha, 1),
        [&] {
            // Note: in separate alpha mode, the alpha channel skips the color LUT look up stage. It uses the output of
            // CombineAndMap directly instead.
            e.Line("float final_alpha = ProcTexLookupLUT(proctex_alpha_map_offset, {});",
                   Pick(e, alpha_combiner, kCombiners, kCombinerDefault, ""));
            e.Raw("return vec4(final_color.xyz, final_alpha);\n");
        },
        [&] { e.Raw("return final_color;\n"); });
    e.Raw("}\n");
}

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

s32 SignExtend(u32 value, u32 bits) {
    const u32 shift = 32 - bits;
    return static_cast<s32>(value << shift) >> shift;
}

class ProcTexFeature final : public FeatureModule {
public:
    const char* Name() const override { return "proctex"; }

    // ---- the generated code ------------------------------------------------------------------------------------------

    void Emit(Point point, EmitContext& ctx) const override {
        if (ctx.emitter == nullptr) {
            return;
        }
        Emitter& e = *ctx.emitter;
        const Cond enabled = e.Eq(e.Field(kPtRaw, 0, 1), 1);
        if (point == Point::Prologue) {
            e.If(enabled, [&] { e.Raw("proctex_value = ProcTexEvaluate();\n"); });
            return;
        }
        if (point != Point::Declarations) {
            return;
        }
        // The specialised shader of a key with the unit off defines the prototype's function and nothing else (sampleTexUnit3
        // does not call it then).
        if (enabled.known && !enabled.value) {
            e.Raw("vec4 ProcTex() { return vec4(0.0); }\n");
            return;
        }
        e.Raw(kLookupLut);
        const Cond noise = e.Eq(e.Field(kPtRaw, 21, 1), 1);
        if (!noise.known || noise.value) {
            e.Raw(kNoise);
        }
        DefineSampleColor(e);
        DefineProcTex(e);
        e.Raw("vec4 proctex_value = vec4(0.0);\n"
              "vec4 ProcTex() { return proctex_value; }\n");
    }

    // ---- the draw's needs --------------------------------------------------------------------------------------------

    Needs NeedsOf(const FragmentKey& key, const DeviceProfile&) const override {
        Needs needs;
        needs.lut_rg = key.proctex.enable() != 0;
        needs.lut_rgba = key.proctex.enable() != 0;
        return needs;
    }

    // ---- uniforms ----------------------------------------------------------------------------------------------------

    // SyncDrawUniforms's "Sync proctex uniforms" (CheckProctex: any of the six registers 0xA8 to 0xAD dirty).
    void FillUniforms(const pica::PicaState& state, FragmentBlock& block, UniformDirty& dirty) const override {
        bool changed = dirty.all;
        for (u32 id = freg::kProcTex; id <= freg::kProcTexLutOffset && !changed; ++id) {
            changed = state.dirty.Test(id);
        }
        if (!changed) {
            return;
        }
        const auto& regs = state.regs;
        const u32 noise_u = regs[freg::kProcTexNoiseU];
        const u32 noise_v = regs[freg::kProcTexNoiseV];
        const u32 frequency = regs[freg::kProcTexNoiseFrequency];
        const Vec2f f{Float16ToFloat(Bits(frequency, 0, 16)), Float16ToFloat(Bits(frequency, 16, 16))};
        const Vec2f a{static_cast<float>(SignExtend(Bits(noise_u, 0, 16), 16)) / 4095.0f,
                      static_cast<float>(SignExtend(Bits(noise_v, 0, 16), 16)) / 4095.0f};
        const Vec2f p{Float16ToFloat(Bits(noise_u, 16, 16)), Float16ToFloat(Bits(noise_v, 16, 16))};
        const float bias = Float16ToFloat(Bits(regs[freg::kProcTex], 20, 8) | (Bits(regs[freg::kProcTexLut], 19, 8) << 8));

        FragmentUniforms& u = block.uniforms;
        const bool same = std::memcmp(&f, &u.proctex_noise_f, sizeof(f)) == 0 && std::memcmp(&a, &u.proctex_noise_a, sizeof(a)) == 0 &&
                          std::memcmp(&p, &u.proctex_noise_p, sizeof(p)) == 0 && std::memcmp(&bias, &u.proctex_bias, sizeof(bias)) == 0;
        if (dirty.all || !same) {
            u.proctex_noise_f = f;
            u.proctex_noise_a = a;
            u.proctex_noise_p = p;
            u.proctex_bias = bias;
            dirty.Mark();
        }
    }

    // ---- the five tables in lut_rg and lut_rgba ------------------------------------------------------------------------

    // Azahar's SyncAndUploadLUTs: the noise, colour map and alpha map tables as {value, difference} pairs (R32G32, 128 each),
    // then the colour table and the colour difference table (R32G32B32A32, 256 each), only the dirty ones (all after a wrap of
    // the ring), each offset kept in elements of its view. The session was opened for all five, so `Take` cannot run out of
    // room; if it did, the offset would be left as it was.
    void UploadTables(TableUpload& upload) const override {
        const u32 need = upload.need.proctex;
        if (need == 0) {
            return;
        }
        const pica::ProcTex& pt = upload.state.proctex;
        FragmentUniforms& u = upload.block.uniforms;

        // ProcTex::ValueEntry: value bits 0-11 (0.0.12), difference bits 12-23 (signed), both over 4095.
        auto value_table = [&](const std::array<u32, 128>& table, int& offset) {
            rhi::Result<TableSlice> slice = upload.rings.Take(TableRing::General, table.size() * sizeof(Vec2f));
            if (!slice.ok()) {
                return;
            }
            for (size_t i = 0; i < table.size(); ++i) {
                const u32 raw = table[i];
                const Vec2f entry{static_cast<float>(Bits(raw, 0, 12)) / 4095.0f,
                                  static_cast<float>(SignExtend(Bits(raw, 12, 12), 12)) / 4095.0f};
                std::memcpy(slice->data.data() + i * sizeof(Vec2f), &entry, sizeof(entry));
            }
            offset = slice->ElementOffset(sizeof(Vec2f));
            upload.dirty.Mark();
        };
        if ((need >> kTableNoise) & 1u) {
            value_table(pt.noise_table, u.proctex_noise_lut_offset);
        }
        if ((need >> kTableColorMap) & 1u) {
            value_table(pt.color_map_table, u.proctex_color_map_offset);
        }
        if ((need >> kTableAlphaMap) & 1u) {
            value_table(pt.alpha_map_table, u.proctex_alpha_map_offset);
        }

        // ProcTex::ColorEntry (r, g, b, a bytes, over 255) and ColorDifferenceEntry (signed bytes, half the difference: times
        // 2, over 255).
        auto color_table = [&](const std::array<u32, 256>& table, bool difference, int& offset) {
            rhi::Result<TableSlice> slice = upload.rings.Take(TableRing::General, table.size() * sizeof(Vec4f));
            if (!slice.ok()) {
                return;
            }
            for (size_t i = 0; i < table.size(); ++i) {
                const u32 raw = table[i];
                float c[4];
                for (u32 k = 0; k < 4; ++k) {
                    const u32 byte = Bits(raw, 8 * k, 8);
                    c[k] = difference ? static_cast<float>(SignExtend(byte, 8) * 2) / 255.0f : static_cast<float>(byte) / 255.0f;
                }
                const Vec4f entry{c[0], c[1], c[2], c[3]};
                std::memcpy(slice->data.data() + i * sizeof(Vec4f), &entry, sizeof(entry));
            }
            offset = slice->ElementOffset(sizeof(Vec4f));
            upload.dirty.Mark();
        };
        if ((need >> kTableColor) & 1u) {
            color_table(pt.color_table, false, u.proctex_lut_offset);
        }
        if ((need >> kTableColorDiff) & 1u) {
            color_table(pt.color_diff_table, true, u.proctex_diff_lut_offset);
        }
    }
};

}  // namespace

MH4N_REGISTER_FRAGMENT_FEATURE(ProcTexFeature)
}  // namespace mh4n::gpu::fragment
