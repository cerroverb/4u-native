// Derived from Azahar src/video_core/shader/generator/glsl_fs_shader_gen.cpp (FragmentModule::WriteLighting and
// DefineLightingHelpers), src/video_core/renderer_software/sw_lighting.cpp (ComputeFragmentsColors: which light's
// two-sided bit the LUT inputs read), src/video_core/rasterizer_accelerated.cpp (SyncDrawUniforms: the light and ambient
// uniforms, LightColor), src/video_core/pica/dirty_regs.h (CheckLight, CheckLightingAmbient: the registers that dirty
// them), src/video_core/renderer_vulkan/vk_rasterizer.cpp (SyncAndUploadLUTsLF: the 24 lighting tables in lut_lf and
// their offsets), src/video_core/pica/pica_core.h (Lighting::LutEntry), src/video_core/pica/regs_lighting.h
// (LightingSampler, IsLightingSamplerSupported, LightSrc's fields) and src/video_core/pica_types.h (Float::FromRaw for
// the f16 positions and the f20 attenuation factors) at 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-3.0-or-later:
// glsl_fs_shader_gen.cpp and vk_rasterizer.cpp say "Licensed under GPLv3 or any later version"; sw_lighting.cpp,
// rasterizer_accelerated.cpp, dirty_regs.h, pica_core.h, regs_lighting.h and pica_types.h say "Licensed under GPLv2 or
// any later version").
// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project (glsl_fs_shader_gen.cpp, vk_rasterizer.cpp,
//   rasterizer_accelerated.cpp, pica_core.h)
// Copyright 2025-2026 Citra Emulator Project / Azahar Emulator Project (dirty_regs.h)
// Copyright 2015-2026 Citra Emulator Project / Azahar Emulator Project (pica_types.h)
// Copyright 2017 Citra Emulator Project (sw_lighting.cpp, regs_lighting.h)
//
// The `lighting/` feature (N2-09): the PICA's per-fragment lighting, Azahar's WriteLighting in both modes of the emitter.
//
//   - up to 8 lights (the key's slots, `LightConfig::lights`), each reading `light_src[num]`;
//   - the LUTs: distribution 0 and 1, Fresnel (last slot only, into the alpha of the primary and/or secondary colour),
//     reflection R, G, B, spot (sampler 8 + num) and distance attenuation (sampler 16 + num), each gated by its enable bit
//     and by IsLightingSamplerSupported for the configuration, with its input (NH, VH, NV, LN, SP, CP), abs mode and scale;
//   - the normal and tangent: the surface-local vectors (unit vectors, or the bump texture's perturbation as a normal map
//     with the optional z renormalisation, or as a tangent map) rotated by the interpolated `normquat`, normalised;
//   - clamp highlights, the geometric factors, the global ambient;
//   - the shadow term: `sampleTexUnit{shadow_selector}()` (inverted when asked), as Azahar. A shadow-map unit goes to
//     shadow/'s lookups, which give 1.0 until N2-12, so the shadow factor is 1.0 for them.
//
// ONE DELIBERATE DIFFERENCE from Azahar's GLSL: the abs mode of a LUT input reads the two-sided bit of the light being
// computed, as the software renderer does (sw_lighting.cpp); the GLSL generator indexes the slot array by the light's
// number (`lighting.lights[light_config.num]`), which is another slot's bit whenever a slot holds a light of another number.
//
// The specialised shader skips the whole block when no TEV stage reads the primary or the secondary fragment colour (the
// colours are then never read; nothing else of the shader depends on them).
//
// The quaternion fix-up (design 5.5) is lighting/'s too: fixup.h generates the fixed geometry stage that the composition
// wires in for lit draws of the translated vertex stage (the CPU path's draws are fixed on the CPU, cpuvertex/).
#include <bit>
#include <cstddef>
#include <cstring>
#include <string>

#include "gpu/backend/rhi.h"
#include "gpu/fragment/emitter.h"
#include "gpu/fragment/feature.h"
#include "gpu/fragment/generator.h"
#include "gpu/fragment/regs.h"
#include "gpu/fragment/tables.h"
#include "gpu/pica/state.h"

namespace mh4n::gpu::fragment {
namespace {

namespace kw = keyword;

// ---- where the lighting key's words are (LightConfig, from kw::kLighting) ----------------------------------------------

constexpr size_t LightingWord(size_t offset) {
    return kw::kLighting + offset / sizeof(u32);
}
constexpr size_t kLutD0 = LightingWord(offsetof(LightConfig, lut_d0));
constexpr size_t kLutD1 = LightingWord(offsetof(LightConfig, lut_d1));
constexpr size_t kLutSp = LightingWord(offsetof(LightConfig, lut_sp));
constexpr size_t kLutFr = LightingWord(offsetof(LightConfig, lut_fr));
constexpr size_t kLutRr = LightingWord(offsetof(LightConfig, lut_rr));
constexpr size_t kLutRg = LightingWord(offsetof(LightConfig, lut_rg));
constexpr size_t kLutRb = LightingWord(offsetof(LightConfig, lut_rb));
constexpr size_t kLights = LightingWord(offsetof(LightConfig, lights));  // two 16-bit lights per word
static_assert(offsetof(LutConfig, scale_bits) == sizeof(u32));
static_assert(sizeof(Light) == 2 && kLights + 4 == kw::kProcTex);

// LightingRegs::LightingSampler.
constexpr u32 kSamplerD0 = 0, kSamplerD1 = 1, kSamplerFresnel = 3, kSamplerRb = 4, kSamplerRg = 5, kSamplerRr = 6;
constexpr u32 kSamplerSpot = 8, kSamplerDist = 16;

// LightingRegs::LightingConfig (7 is 8), LightingBumpMode, LightingLutInput.
constexpr u32 kConfig0 = 0, kConfig1 = 1, kConfig2 = 2, kConfig3 = 3, kConfig4 = 4, kConfig5 = 5, kConfig7 = 8;
constexpr u32 kBumpNormalMap = 1, kBumpTangentMap = 2;
constexpr u32 kInputNH = 0, kInputVH = 1, kInputNV = 2, kInputLN = 3, kInputSP = 4, kInputCP = 5;

// The TEV sources that read the lighting result (TevStageConfig::Source).
constexpr u32 kSourcePrimaryFragmentColor = 1, kSourceSecondaryFragmentColor = 2;

// DefineLightingHelpers, and (ubershader) the texture unit chosen by a key field.
constexpr const char* kHelpers = R"(
float LookupLightingLUT(int lut_index, int index, float delta) {
    vec2 entry = texture_buffer_lut_lf.data[lighting_lut_offset[lut_index >> 2][lut_index & 3] + index];
    return entry.r + entry.g * delta;
}

float LookupLightingLUTUnsigned(int lut_index, float pos) {
    int index = int(clamp(floor(pos * 256.0), 0.f, 255.f));
    float delta = pos * 256.0 - float(index);
    return LookupLightingLUT(lut_index, index, delta);
}

float LookupLightingLUTSigned(int lut_index, float pos) {
    int index = int(clamp(floor(pos * 128.0), -128.f, 127.f));
    float delta = pos * 128.0 - float(index);
    if (index < 0) index += 256;
    return LookupLightingLUT(lut_index, index, delta);
}
)";

constexpr const char* kUberUnit = R"(
vec4 mh4n_lighting_unit(uint unit) {
    switch (unit) {
    case 0u: return sampleTexUnit0();
    case 1u: return sampleTexUnit1();
    case 2u: return sampleTexUnit2();
    default: return sampleTexUnit3();
    }
}
)";

// The lighting globals of WriteLighting (locals of the lighting block here).
constexpr const char* kLocals =
    "vec4 diffuse_sum = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "vec4 specular_sum = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "vec3 light_vector = vec3(0.0);\n"
    "float light_distance = 0.0;\n"
    "vec3 refl_value = vec3(0.0);\n"
    "vec3 spot_dir = vec3(0.0);\n"
    "vec3 half_vector = vec3(0.0);\n"
    "float dot_product = 0.0;\n"
    "float clamp_highlights = 1.0;\n"
    "float geo_factor = 1.0;\n";

// ---- the PICA's small floats (Pica::Float<M, E>::FromRaw) ------------------------------------------------------------

template <unsigned M, unsigned E>
float FloatFromRaw(u32 hex) {
    constexpr u32 kMantissaMask = (1u << M) - 1;
    constexpr u32 kExponentMask = (1u << E) - 1;
    constexpr s32 kBias = 127 - ((1 << (E - 1)) - 1);
    s32 exponent = static_cast<s32>((hex >> M) & kExponentMask);
    u32 mantissa = hex & kMantissaMask;
    const u32 sign = ((hex >> (E + M)) & 1u) << 31;
    u32 out;
    if (exponent == 0 && mantissa == 0) {
        out = sign;
    } else if (exponent == 0) {
        // A denormal, converted without loss (the PICA flushes them; Azahar keeps the value).
        exponent = kBias + 1;
        while ((mantissa & (1u << M)) == 0) {
            --exponent;
            mantissa <<= 1;
        }
        mantissa &= kMantissaMask;
        out = sign | (static_cast<u32>(exponent) << 23) | (mantissa << (23 - M));
    } else if (static_cast<u32>(exponent) == kExponentMask) {
        out = sign | (0xFFu << 23) | (mantissa << (23 - M));
    } else {
        out = sign | (static_cast<u32>(kBias + exponent) << 23) | (mantissa << (23 - M));
    }
    return std::bit_cast<float>(out);
}

float F16(u32 raw) {
    return FloatFromRaw<10, 5>(raw & 0xFFFFu);
}
float F20(u32 raw) {
    return FloatFromRaw<12, 7>(raw & 0xFFFFFu);
}

// LightColor::ToVec3f: 10-bit fields, 255 is 1.0.
Vec3f LightColor(u32 word) {
    return Vec3f{static_cast<float>(Bits(word, 20, 10)) / 255.0f, static_cast<float>(Bits(word, 10, 10)) / 255.0f,
                 static_cast<float>(Bits(word, 0, 10)) / 255.0f};
}

// A signed 13-bit field (fixed 1.1.11) over 2047.
float Spot(u32 word, u32 first) {
    const s32 v = static_cast<s32>(Bits(word, first, 13) << 19) >> 19;
    return static_cast<float>(v) / 2047.0f;
}

// ---- the generated code ----------------------------------------------------------------------------------------------

// IsLightingSamplerSupported(config, sampler) as a condition on the key's configuration field.
Cond Supported(const Emitter& e, const Value& config, u32 sampler) {
    switch (sampler) {
    case kSamplerD0:
        return e.Ne(config, kConfig1);
    case kSamplerD1:
        return e.And(e.And(e.Ne(config, kConfig0), e.Ne(config, kConfig1)), e.Ne(config, kConfig5));
    case kSamplerSpot:
        return e.And(e.Ne(config, kConfig2), e.Ne(config, kConfig3));
    case kSamplerFresnel:
        return e.And(e.And(e.Ne(config, kConfig0), e.Ne(config, kConfig2)), e.Ne(config, kConfig4));
    case kSamplerRr:
        return e.Ne(config, kConfig3);
    case kSamplerRg:
    case kSamplerRb:
        return e.Or(e.Or(e.Eq(config, kConfig4), e.Eq(config, kConfig5)), e.Eq(config, kConfig7));
    default:
        return Emitter::False();
    }
}

// `sampleTexUnit{unit}()` for a key field.
std::string UnitText(const Value& unit) {
    if (unit.known) {
        return fmt::format("sampleTexUnit{}()", unit.literal);
    }
    return fmt::format("mh4n_lighting_unit({})", unit.glsl);
}

// The LUT scale (LutConfig::scale_bits, a float's bits) as a GLSL float.
std::string ScaleText(const Emitter& e, size_t lut) {
    const Value bits = e.Word(lut + 1);
    if (bits.known) {
        return fmt::format("{:#}", std::bit_cast<float>(bits.literal));
    }
    return fmt::format("uintBitsToFloat({})", bits.glsl);
}

// The sampler index `base + num` as a GLSL int.
std::string SamplerText(const Value& num, u32 base) {
    if (num.known) {
        return fmt::format("{}", base + num.literal);
    }
    return fmt::format("int({}u + {})", base, num.glsl);
}

// get_lut_value: `<out> = scale * LookupLightingLUT{Unsigned,Signed}(sampler, input)` for the LUT whose key words start at
// `lut`. `two_sided` is the light's own two-sided bit (see the header).
void WriteLut(Emitter& e, std::string_view out, const std::string& sampler, size_t lut, const Value& two_sided,
              const Value& config) {
    e.Raw("{\nfloat lut_pos = 0.0;\n");
    e.Switch(e.Field(lut, 2, 3),
             {
                 {kInputNH, [&] { e.Raw("lut_pos = dot(normal, normalize(half_vector));\n"); }},
                 {kInputVH, [&] { e.Raw("lut_pos = dot(normalize(view), normalize(half_vector));\n"); }},
                 {kInputNV, [&] { e.Raw("lut_pos = dot(normal, normalize(view));\n"); }},
                 {kInputLN, [&] { e.Raw("lut_pos = dot(light_vector, normal);\n"); }},
                 {kInputSP, [&] { e.Raw("lut_pos = dot(light_vector, spot_dir);\n"); }},
                 {kInputCP,
                  [&] {
                      // CP input is only available with configuration 7. Even if the normal vector is modified by the normal
                      // map, the half angle vector is projected using the modified normal vector; the projection is not
                      // normalized before the dot product.
                      e.If(e.Eq(config, kConfig7), [&] {
                          e.Raw("lut_pos = dot(normalize(half_vector) - normal * dot(normal, normalize(half_vector)), "
                                "tangent);\n");
                      });
                  }},
             });
    const std::string scale = ScaleText(e, lut);
    e.If(
        e.Ne(e.Field(lut, 1, 1), 0),
        [&] {
            // LUT index is in the range of (0.0, 1.0)
            e.If(
                e.Ne(two_sided, 0), [&] { e.Raw("lut_pos = abs(lut_pos);\n"); },
                [&] { e.Raw("lut_pos = max(lut_pos, 0.0);\n"); });
            e.Raw(fmt::format("{} = ({} * LookupLightingLUTUnsigned({}, lut_pos));\n", out, scale, sampler));
        },
        [&] {
            // LUT index is in the range of (-1.0, 1.0)
            e.Raw(fmt::format("{} = ({} * LookupLightingLUTSigned({}, lut_pos));\n", out, scale, sampler));
        });
    e.Raw("}\n");
}

// A LUT's enable bit together with the configuration's support for its sampler.
Cond LutOn(const Emitter& e, size_t lut, const Value& config, u32 sampler) {
    return e.And(e.Ne(e.Field(lut, 0, 1), 0), Supported(e, config, sampler));
}

class LightingFeature final : public FeatureModule {
public:
    const char* Name() const override { return "lighting"; }

    // ---- the generated code ------------------------------------------------------------------------------------------

    void Emit(Point point, EmitContext& ctx) const override {
        Emitter& e = *ctx.emitter;
        if (point == Point::Declarations) {
            if (!e.specialised()) {
                e.Raw(kHelpers);
                e.Raw(kUberUnit);
            } else if (ctx.key.lighting.enable() != 0) {
                e.Raw(kHelpers);
            }
            return;
        }
        if (point != Point::Lighting) {
            return;
        }
        if (e.specialised() && !TevUsesSource(ctx.key, kSourcePrimaryFragmentColor) &&
            !TevUsesSource(ctx.key, kSourceSecondaryFragmentColor)) {
            return;  // nothing reads the result
        }
        e.If(e.Ne(e.Field(kw::kLightRaw, 0, 1), 0), [&] { WriteLighting(e); });
    }

    // WriteLighting.
    static void WriteLighting(Emitter& e) {
        const size_t raw = kw::kLightRaw;
        const Value src_num = e.Field(raw, 1, 4);
        const Value bump_mode = e.Field(raw, 5, 2);
        const Value bump_selector = e.Field(raw, 7, 2);
        const Value bump_renorm = e.Field(raw, 9, 1);
        const Value clamp_highlights = e.Field(raw, 10, 1);
        const Value config = e.Field(raw, 11, 4);
        const Value primary_alpha = e.Field(raw, 15, 1);
        const Value secondary_alpha = e.Field(raw, 16, 1);
        const Value enable_shadow = e.Field(raw, 17, 1);
        const Value shadow_primary = e.Field(raw, 18, 1);
        const Value shadow_secondary = e.Field(raw, 19, 1);
        const Value shadow_invert = e.Field(raw, 20, 1);
        const Value shadow_alpha = e.Field(raw, 21, 1);
        const Value shadow_selector = e.Field(raw, 22, 2);

        e.Raw("{\n");
        e.Raw(kLocals);

        // Compute fragment normals and tangents: the surface-local vectors (no bump mapping: unit vectors).
        e.Raw("vec3 surface_normal = vec3(0.0, 0.0, 1.0);\n"
              "vec3 surface_tangent = vec3(1.0, 0.0, 0.0);\n");
        const std::string perturbation = fmt::format("2.0 * ({}).rgb - 1.0", UnitText(bump_selector));
        e.Switch(bump_mode, {
                                {kBumpNormalMap,
                                 [&] {
                                     // Bump mapping is enabled using a normal map
                                     e.Raw(fmt::format("surface_normal = {};\n", perturbation));
                                     // Recompute Z-component of perturbation if 'renorm' is enabled, this provides a higher
                                     // precision result
                                     e.If(e.Ne(bump_renorm, 0), [&] {
                                         e.Raw("surface_normal.z = sqrt(max((1.0 - (surface_normal.x*surface_normal.x + "
                                               "surface_normal.y*surface_normal.y)), 0.0));\n");
                                     });
                                 }},
                                {kBumpTangentMap,
                                 [&] {
                                     // Bump mapping is enabled using a tangent map (its Z is not recomputed: it does not
                                     // affect the computation below)
                                     e.Raw(fmt::format("surface_tangent = {};\n", perturbation));
                                 }},
                            });

        // Rotate the surface-local normal by the interpolated normal quaternion to convert it to eyespace.
        e.Raw("vec4 normalized_normquat = normalize(normquat);\n"
              "vec3 normal = quaternion_rotate(normalized_normquat, surface_normal);\n"
              "vec3 tangent = quaternion_rotate(normalized_normquat, surface_tangent);\n");

        e.Raw("vec4 shadow = vec4(1.0);\n");
        e.If(e.Ne(enable_shadow, 0), [&] {
            e.If(
                e.Ne(shadow_invert, 0), [&] { e.Raw(fmt::format("shadow = vec4(1.0) - {};\n", UnitText(shadow_selector))); },
                [&] { e.Raw(fmt::format("shadow = {};\n", UnitText(shadow_selector))); });
        });

        // Write the code to emulate each enabled light.
        e.ForEach(src_num, 8, "light_index", [&](const std::string& index) {
            Value light;
            Cond last;
            if (e.specialised()) {
                const u32 i = static_cast<u32>(std::stoul(index));
                light = e.Field(kLights + i / 2, (i % 2) * 16, 16);
                last = e.Eq(src_num, i + 1);
            } else {
                e.Raw(fmt::format("uint light_word = {}u + ({} >> 1u);\n", kLights, index));
                e.Raw(fmt::format("uint light_raw = (cfg[light_word >> 2u][light_word & 3u] >> (({} & 1u) * 16u)) & 0xFFFFu;\n",
                                  index));
                light.glsl = "light_raw";
                last = Cond{false, false, fmt::format("({} + 1u == {})", index, src_num.glsl)};
            }
            WriteLight(e, light, last, config, clamp_highlights, primary_alpha, secondary_alpha, shadow_primary,
                       shadow_secondary);
        });

        // Apply shadow attenuation to alpha components if enabled
        e.If(e.Ne(shadow_alpha, 0), [&] {
            e.If(e.Ne(primary_alpha, 0), [&] { e.Raw("diffuse_sum.a *= shadow.a;\n"); });
            e.If(e.Ne(secondary_alpha, 0), [&] { e.Raw("specular_sum.a *= shadow.a;\n"); });
        });

        // Sum final lighting result
        e.Raw("diffuse_sum.rgb += lighting_global_ambient;\n"
              "primary_fragment_color = clamp(diffuse_sum, vec4(0.0), vec4(1.0));\n"
              "secondary_fragment_color = clamp(specular_sum, vec4(0.0), vec4(1.0));\n");
        e.Raw("}\n");
    }

    // One light of the loop: `light` is its 16-bit Light word, `last` whether it is the last slot.
    static void WriteLight(Emitter& e, const Value& light, const Cond& last, const Value& config, const Value& clamp_highlights,
                           const Value& primary_alpha, const Value& secondary_alpha, const Value& shadow_primary,
                           const Value& shadow_secondary) {
        const Value num = e.Slice(light, 0, 3);
        const Value directional = e.Slice(light, 3, 1);
        const Value two_sided = e.Slice(light, 4, 1);
        const Value dist_atten_enable = e.Slice(light, 5, 1);
        const Value spot_atten_enable = e.Slice(light, 6, 1);
        const Value geometric_factor_0 = e.Slice(light, 7, 1);
        const Value geometric_factor_1 = e.Slice(light, 8, 1);
        const Value shadow_enable = e.Slice(light, 9, 1);
        const std::string light_src = num.known ? fmt::format("light_src[{}]", num.literal)
                                                : fmt::format("light_src[{}]", num.glsl);

        // Compute light vector (directional or positional)
        e.If(
            e.Ne(directional, 0), [&] { e.Raw(fmt::format("light_vector = {}.position;\n", light_src)); },
            [&] { e.Raw(fmt::format("light_vector = {}.position + view;\n", light_src)); });
        e.Raw("light_distance = length(light_vector);\n"
              "light_vector = normalize(light_vector);\n");
        e.Raw(fmt::format("spot_dir = {}.spot_direction;\n", light_src));
        e.Raw("half_vector = normalize(view) + light_vector;\n");

        // Compute dot product of light_vector and normal, adjust if lighting is one-sided or two-sided
        e.If(
            e.Ne(two_sided, 0), [&] { e.Raw("dot_product = abs(dot(light_vector, normal));\n"); },
            [&] { e.Raw("dot_product = max(dot(light_vector, normal), 0.0);\n"); });

        // If enabled, clamp specular component if lighting result is zero
        e.If(e.Ne(clamp_highlights, 0), [&] { e.Raw("clamp_highlights = sign(dot_product);\n"); });

        // If enabled, compute spot light attenuation value
        e.Raw("float spot_atten = 1.0;\n");
        e.If(e.And(e.Ne(spot_atten_enable, 0), Supported(e, config, kSamplerSpot)),
             [&] { WriteLut(e, "spot_atten", SamplerText(num, kSamplerSpot), kLutSp, two_sided, config); });

        // If enabled, compute distance attenuation value
        e.Raw("float dist_atten = 1.0;\n");
        e.If(e.Ne(dist_atten_enable, 0), [&] {
            e.Raw(fmt::format("dist_atten = LookupLightingLUTUnsigned({}, clamp({}.dist_atten_scale * light_distance + "
                              "{}.dist_atten_bias, 0.0, 1.0));\n",
                              SamplerText(num, kSamplerDist), light_src, light_src));
        });

        e.If(e.Or(e.Ne(geometric_factor_0, 0), e.Ne(geometric_factor_1, 0)), [&] {
            e.Raw("geo_factor = dot(half_vector, half_vector);\n"
                  "geo_factor = geo_factor == 0.0 ? 0.0 : min(dot_product / geo_factor, 1.0);\n");
        });

        // Specular 0 component
        e.Raw("float d0_lut_value = 1.0;\n");
        e.If(LutOn(e, kLutD0, config, kSamplerD0),
             [&] { WriteLut(e, "d0_lut_value", fmt::format("{}", kSamplerD0), kLutD0, two_sided, config); });
        e.Raw(fmt::format("vec3 specular_0 = (d0_lut_value * {}.specular_0);\n", light_src));
        e.If(e.Ne(geometric_factor_0, 0), [&] { e.Raw("specular_0 = (specular_0 * geo_factor);\n"); });

        // If enabled, lookup ReflectRed value, otherwise, 1.0 is used
        e.If(
            LutOn(e, kLutRr, config, kSamplerRr),
            [&] { WriteLut(e, "refl_value.r", fmt::format("{}", kSamplerRr), kLutRr, two_sided, config); },
            [&] { e.Raw("refl_value.r = 1.0;\n"); });
        // If enabled, lookup ReflectGreen value, otherwise, ReflectRed value is used
        e.If(
            LutOn(e, kLutRg, config, kSamplerRg),
            [&] { WriteLut(e, "refl_value.g", fmt::format("{}", kSamplerRg), kLutRg, two_sided, config); },
            [&] { e.Raw("refl_value.g = refl_value.r;\n"); });
        // If enabled, lookup ReflectBlue value, otherwise, ReflectRed value is used
        e.If(
            LutOn(e, kLutRb, config, kSamplerRb),
            [&] { WriteLut(e, "refl_value.b", fmt::format("{}", kSamplerRb), kLutRb, two_sided, config); },
            [&] { e.Raw("refl_value.b = refl_value.r;\n"); });

        // Specular 1 component
        e.Raw("float d1_lut_value = 1.0;\n");
        e.If(LutOn(e, kLutD1, config, kSamplerD1),
             [&] { WriteLut(e, "d1_lut_value", fmt::format("{}", kSamplerD1), kLutD1, two_sided, config); });
        e.Raw(fmt::format("vec3 specular_1 = (d1_lut_value * refl_value * {}.specular_1);\n", light_src));
        e.If(e.Ne(geometric_factor_1, 0), [&] { e.Raw("specular_1 = (specular_1 * geo_factor);\n"); });

        // Fresnel: only the last entry in the light slots applies the Fresnel factor
        e.If(e.And(last, LutOn(e, kLutFr, config, kSamplerFresnel)), [&] {
            e.Raw("float fresnel_value = 1.0;\n");
            WriteLut(e, "fresnel_value", fmt::format("{}", kSamplerFresnel), kLutFr, two_sided, config);
            // Enabled for diffuse lighting alpha component
            e.If(e.Ne(primary_alpha, 0), [&] { e.Raw("diffuse_sum.a = fresnel_value;\n"); });
            // Enabled for the specular lighting alpha component
            e.If(e.Ne(secondary_alpha, 0), [&] { e.Raw("specular_sum.a = fresnel_value;\n"); });
        });

        // Compute primary fragment color (diffuse lighting) function
        e.If(
            e.And(e.Ne(shadow_primary, 0), e.Ne(shadow_enable, 0)),
            [&] {
                e.Raw(fmt::format("diffuse_sum.rgb += (({0}.diffuse * dot_product * shadow.rgb) + {0}.ambient) * dist_atten * "
                                  "spot_atten;\n",
                                  light_src));
            },
            [&] {
                e.Raw(fmt::format("diffuse_sum.rgb += (({0}.diffuse * dot_product) + {0}.ambient) * dist_atten * spot_atten;\n",
                                  light_src));
            });

        // Compute secondary fragment color (specular lighting) function
        e.If(
            e.And(e.Ne(shadow_secondary, 0), e.Ne(shadow_enable, 0)),
            [&] {
                e.Raw("specular_sum.rgb += (specular_0 + specular_1) * clamp_highlights * dist_atten * spot_atten * "
                      "shadow.rgb;\n");
            },
            [&] { e.Raw("specular_sum.rgb += (specular_0 + specular_1) * clamp_highlights * dist_atten * spot_atten;\n"); });
    }

    // ---- the draw's needs --------------------------------------------------------------------------------------------

    Needs NeedsOf(const FragmentKey& key, const DeviceProfile&) const override {
        Needs needs;
        needs.lut_lf = key.lighting.enable() != 0;
        return needs;
    }

    // ---- uniforms ----------------------------------------------------------------------------------------------------

    // SyncDrawUniforms' lighting part: the global ambient (GPUREG_LIGHTING_AMBIENT) and each light whose sixteen registers
    // have a dirty one (CheckLight).
    void FillUniforms(const pica::PicaState& state, FragmentBlock& block, UniformDirty& dirty) const override {
        const auto& regs = state.regs;
        FragmentUniforms& u = block.uniforms;
        if (dirty.all || state.dirty.Test(freg::kLightingGlobalAmbient)) {
            const Vec3f ambient = LightColor(regs[freg::kLightingGlobalAmbient]);
            if (dirty.all || std::memcmp(&ambient, &u.lighting_global_ambient, sizeof(ambient)) != 0) {
                u.lighting_global_ambient = ambient;
                dirty.Mark();
            }
        }
        for (u32 n = 0; n < 8; ++n) {
            const u32 base = freg::kLight0 + freg::kLightStride * n;
            bool light_dirty = dirty.all;
            if (!light_dirty && base % 64 + freg::kLightStride <= 64) {
                // (a light's registers in one word of the dirty set: tested together)
                const u64 field = freg::kLightStride >= 64 ? ~0ull : (1ull << freg::kLightStride) - 1;
                light_dirty = ((state.dirty.qwords[base >> 6] >> (base & 63)) & field) != 0;
            } else {
                for (u32 id = base; id < base + freg::kLightStride && !light_dirty; ++id) {
                    light_dirty = state.dirty.Test(id);
                }
            }
            if (!light_dirty) {
                continue;
            }
            LightSrc src = u.light_src[n];  // a copy: the padding stays as it is, so memcmp compares the members
            src.specular_0 = LightColor(regs[base + freg::kLightSpecular0]);
            src.specular_1 = LightColor(regs[base + freg::kLightSpecular1]);
            src.diffuse = LightColor(regs[base + freg::kLightDiffuse]);
            src.ambient = LightColor(regs[base + freg::kLightAmbient]);
            const u32 xy = regs[base + freg::kLightPositionXY];
            const u32 z = regs[base + freg::kLightPositionZ];
            src.position = Vec3f{F16(Bits(xy, 0, 16)), F16(Bits(xy, 16, 16)), F16(Bits(z, 0, 16))};
            const u32 spot_xy = regs[base + freg::kLightSpotXY];
            const u32 spot_z = regs[base + freg::kLightSpotZ];
            src.spot_direction = Vec3f{Spot(spot_xy, 0), Spot(spot_xy, 16), Spot(spot_z, 0)};
            src.dist_atten_bias = F20(regs[base + freg::kLightDistAttenBias]);
            src.dist_atten_scale = F20(regs[base + freg::kLightDistAttenScale]);
            if (dirty.all || std::memcmp(&src, &u.light_src[n], sizeof(src)) != 0) {
                u.light_src[n] = src;
                dirty.Mark();
            }
        }
    }

    // ---- the lighting tables in lut_lf --------------------------------------------------------------------------------

    // SyncAndUploadLUTsLF's lighting part: each dirty table as 256 {value, difference} pairs, its offset in elements of the
    // R32G32 view in `lighting_lut_offset[n / 4][n % 4]`.
    void UploadTables(TableUpload& upload) const override {
        u32 dirty = upload.need.lighting & pica::Lighting::kAllDirty;
        while (dirty != 0) {
            const u32 index = static_cast<u32>(std::countr_zero(dirty));
            dirty &= ~(1u << index);
            rhi::Result<TableSlice> slice = upload.rings.Take(TableRing::Lf, pica::Lighting::kEntries * sizeof(Vec2f));
            if (!slice.ok()) {
                return;
            }
            const auto& lut = upload.state.lighting.luts[index];
            for (u32 i = 0; i < pica::Lighting::kEntries; ++i) {
                const u32 raw = lut[i];
                // Lighting::LutEntry: value bits 0-11 (0.0.12), difference bits 12-22 (0.0.11) with its sign in bit 23.
                const float difference = static_cast<float>(Bits(raw, 12, 11)) / 2047.0f;
                const Vec2f entry{static_cast<float>(Bits(raw, 0, 12)) / 4095.0f, Bits(raw, 23, 1) != 0 ? -difference : difference};
                std::memcpy(slice->data.data() + i * sizeof(Vec2f), &entry, sizeof(entry));
            }
            Vec4i& offsets = upload.block.uniforms.lighting_lut_offset[index / 4];
            const int offset = slice->ElementOffset(sizeof(Vec2f));
            switch (index % 4) {
            case 0: offsets.x = offset; break;
            case 1: offsets.y = offset; break;
            case 2: offsets.z = offset; break;
            default: offsets.w = offset; break;
            }
            upload.dirty.Mark();
        }
    }
};

}  // namespace

MH4N_REGISTER_FRAGMENT_FEATURE(LightingFeature)
}  // namespace mh4n::gpu::fragment
