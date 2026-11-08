// Derived from Azahar src/video_core/shader/generator/glsl_shader_gen.cpp, glsl_shader_gen.h,
// shader_gen.cpp, shader_gen.h, shader_uniforms.h, shader_uniforms.cpp, src/video_core/pica/shader_setup.h,
// shader_setup.cpp, output_vertex.cpp, regs_rasterizer.h and regs_shader.h at
// 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-2.0-or-later; each of those files says "Licensed under
// GPLv2 or any later version"): the configuration that identifies a generated vertex shader, the
// locations and uniform blocks of its interface, the semantic names of the output attributes, and the
// output map.
// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project (glsl_shader_gen.cpp,
//   glsl_shader_gen.h, shader_gen.cpp, shader_gen.h, shader_setup.cpp, shader_setup.h)
// Copyright 2023-2025 Citra Emulator Project / Azahar Emulator Project (shader_uniforms.h,
//   shader_uniforms.cpp)
// Copyright 2023 Citra Emulator Project (output_vertex.cpp)
// Copyright 2017-2026 Citra Emulator Project / Azahar Emulator Project (regs_rasterizer.h)
// Copyright 2017-2025 Citra Emulator Project / Azahar Emulator Project (regs_shader.h)
//
// The PICA vertex shader as SPIR-V (N2-06). `TranslateVertexShader` turns a vertex program (the
// ShaderSetup of the processor's state), the registers that say how its outputs are used and the
// conversions of its inputs into a SPIR-V vertex shader; `VertexShaderCache` compiles each distinct
// configuration once. Nothing here names an RHI type: the SPIR-V is a vector of words for
// rhi::ShaderCode, the layout of the buffers is below.
//
// ---- The interface of a translated vertex shader (N2-08, N2-16, N2-09 read this) ---------------
//
// Descriptor set 0, two uniform buffers (std140), both visible to the vertex stage:
//   binding 0  kBindingPicaUniforms  `uint b; uvec4 i[4]; vec4 f[96]` = PicaUniformData (1616 bytes):
//                                    the bool uniforms (bit n = bool n), the integer uniforms
//                                    (x, y, z, w of each) and the float uniforms as float32
//   binding 1  kBindingVertexData    `uint enable_clip1; uint flip_viewport; vec4 clip_coef`
//                                    = VertexUniformData (32 bytes)
// The test hook (`capture_outputs`, never set by the renderer) adds binding kBindingCapture, a
// storage buffer written by the shader (layout in kCapture* below).
//
// Inputs: one `layout(location = r) in {vec4|ivec4|uvec4} vs_in_typed_reg<r>` per input register r that
// the program reads (r = 0..15, the location is the register, as in vertex_loader.h's description).
// The type is the attribute's conversion: Float -> vec4, FromSint -> ivec4, FromUint -> uvec4
// (converted with `vec4(x)`, the PICA's byte and short attributes are not normalised), and an
// attribute with `alias_w` gets w = 1.0 (Azahar's interpreter does; its GLSL generator writes 0, which
// vertex_loader.h says is not to be copied). The pipeline's vertex layout must give the register a
// format of the matching class; the renderer declares all 16 locations, a location the shader does not
// read is harmless.
//
// Outputs, with `geometry_stage` false (the stage feeds the fragment stage): at Azahar's fixed
// locations (ATTRIBUTE_* in shader_gen.h; the fragment shader declares the same):
//   location 1  vec4  primary_color   = min(abs(color), 1)
//   location 2  vec2  texcoord0
//   location 3  vec2  texcoord1
//   location 4  vec2  texcoord2
//   location 5  float texcoord0_w
//   location 6  vec4  normquat
//   location 7  vec3  view
//   gl_Position       = (x, y', -z, w) after SanitizeVertex; y' = -y when flip_viewport
//   gl_ClipDistance[0..1] (only with use_clip_planes): -z (the PICA's near plane z <= 0) and
//                     dot(clip_coef, pos) when enable_clip1
// Each is taken from the output attribute the registers map to its semantic (OUTMAP, below). A
// semantic that no output attribute maps is 1.0 in every lane that uses it (not 0: Azahar's
// glsl_shader_gen.cpp `semantic()` returns "1.0", and output_vertex.cpp says "Hardware tests have
// shown that they are initialized to 1"; gpu-fragment-design.md 8 (h) says zero, which is wrong). A
// mapped attribute's lane the program does not write is what the generated code starts with:
// (0, 0, 0, 1) for the attribute.
//
// Outputs, with `geometry_stage` true (Azahar's `use_geometry_shader`, the fixed geometry shader of
// lit draws, N2-09): `layout(location = n) out vec4 vs_out_attr<n>` for n < num_outputs, nothing
// else; the geometry stage does the position and the varyings from the same semantic map.
//
// With `pull_fixup` (the lighting fix-up without a geometry stage, where one is slow or missing; written for this
// package, not Azahar's): the stage has no vertex input and reads its own vertex data from the storage buffer at set 0,
// binding kBindingPullWords (the renderer's vertex ring as words):
//   gl_InstanceIndex   the word of the draw's header there (the draw's firstInstance)
//   header word b      (b < 16) the byte offset in the buffer of vertex binding b's data
// Each input register is read from where `pull[r]` says (binding, byte offset, format; vertex v of a binding is at
// offset + v * stride, a per-instance binding at offset alone) and converted as the vertex input would. The draw is an
// indexed triangle list whose index (gl_VertexIndex) is a vertex's number in the bindings times two, plus one when its
// quaternion is negated in that triangle; the outputs are those of the stage without a geometry stage, normquat
// negated by that bit. Two compute passes before the draws make those indices (the prelude, rhi.h), for all the draws
// of a recording at once: their set 0 (native/bindings.h, FixupPassSets) holds the vertex-stage uniform ring as words
// (binding kBindingPassUniformWords), the dispatch's parameters (binding kBindingPassParams: `uvec4 pass_params`, x the
// word of the job records, y the word of the dispatch's workgroup table) and the vertex ring as words (binding
// kBindingPullWords). A workgroup's job is job record pass_params.x + kPullJobWords * table[pass_params.y + workgroup]; a
// job record (kPullJob*) is a draw's: its PicaUniformData's word in the uniform ring, its header's word, its first vertex,
// its vertex count, the word of its quaternions, the word of its index list, its corner count, the word of its indices,
// the first workgroup of each pass that is its own, its index list's form (kPullJobIndexForm: bits 0-1 the index size,
// kPullIndexNone (vertex i is i), kPullIndexU8, kPullIndexU16 or kPullIndexU32, little-endian and packed in words; bits
// 2-3 the topology, kPullTriangleList, kPullTriangleStrip or kPullTriangleFan; kPullTriangleTable set when the triangles
// drawn are those the table names), its base vertex (added to each index, a signed int) and the word of its triangle
// table (a word a triangle drawn: its number in the topology; the CPU leaves out those with two equal vertices, which
// have no area and draw nothing, nor does the geometry stage's):
//   quaternion pass (GenerateVertexGlsl's `quaternion_compute`, one dispatch a program, one invocation a vertex): loads
//     the job's PicaUniformData into shared memory (`uniforms`, as the vertex stage's block), runs a copy of the program
//     that writes only the quaternion's output attributes for the job's vertex first + i, and stores the quaternion as
//     4 words at quaternions + 4 i
//   flip pass (FlipPassGlsl, one dispatch, one invocation a corner): corner i is corner i % 3 of triangle t, i / 3 or
//     the table's entry i / 3, of the index list as the device assembles the topology for a geometry stage (a list's
//     triangle t: 3t, 3t+1, 3t+2; a strip's: t, t+1, t+2 when t is even, t, t+2, t+1 when it is odd; a fan's: t+1, t+2,
//     0), which keeps each triangle's winding; it stores the index, the corner's vertex * 2 plus 1 when the dot product
//     of its quaternion and its triangle's first vertex's is negative, at indices + i
// What the fixed geometry shader does (fragment/lighting/fixup.h), corner by corner, with the vertices of a triangle
// shaded once for each sign they take.
//
// The output map the caller needs (TranslatedVertexShader): `num_outputs`, `output_map[reg]` (the
// attribute index of output register reg, 16 when the register is not in the output mask) and
// `semantic_map[semantic]` (attribute and component, attribute 16 when nothing is mapped).
//
// ---- The cache key -----------------------------------------------------------------------------
//
// VertexShaderConfig is the key: the program and swizzle hashes (of the loaded words) and sizes, the
// entry point, the output mask (-> output map), the OUTMAP registers (the rasterizer's
// vs_output_total and the seven vs_output_attributes), the per-register input conversion and
// alias_w, and the options that change the code. A hit never compiles.
#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "gpu/pica/state.h"
#include "gpu/vertex/vertex_loader.h"
#include "mh4n/types.h"

namespace mh4n::gpu::shader {

inline constexpr u32 kDescriptorSet = 0;
inline constexpr u32 kBindingPicaUniforms = 0;
inline constexpr u32 kBindingVertexData = 1;
inline constexpr u32 kBindingCapture = 15;
inline constexpr u32 kBindingPullWords = 6;   // `pull_fixup`: the vertex data as words (native/bindings.h)
inline constexpr u32 kPullHeaderWords = 16;   // ... the words of a draw's header
inline constexpr u32 kPullComputeGroup = 64;  // ... the invocations of a workgroup of the two compute passes
inline constexpr u32 kBindingPassUniformWords = 0;  // ... their bindings besides kBindingPullWords (native/bindings.h)
inline constexpr u32 kBindingPassParams = 1;
// ... a job record: its words
inline constexpr u32 kPullJobUniforms = 0, kPullJobHeader = 1, kPullJobFirstVertex = 2, kPullJobVertices = 3,
                     kPullJobQuaternions = 4, kPullJobIndexList = 5, kPullJobCornerCount = 6, kPullJobIndices = 7,
                     kPullJobQuaternionGroup = 8, kPullJobFlipGroup = 9, kPullJobIndexForm = 10, kPullJobBaseVertex = 11,
                     kPullJobTriangleTable = 12, kPullJobWords = 16;
// ... kPullJobIndexForm's index sizes (bits 0-1), topologies (bits 2-3) and the triangle table's bit
inline constexpr u32 kPullIndexNone = 0, kPullIndexU8 = 1, kPullIndexU16 = 2, kPullIndexU32 = 3;
inline constexpr u32 kPullTriangleList = 0, kPullTriangleStrip = 1, kPullTriangleFan = 2;
inline constexpr u32 kPullTriangleTable = 16;

// Azahar's `Attributes` (shader_gen.h): the vertex stage's outputs and the fragment stage's inputs.
inline constexpr u32 kLocationPrimaryColor = 1;
inline constexpr u32 kLocationTexcoord0 = 2;
inline constexpr u32 kLocationTexcoord1 = 3;
inline constexpr u32 kLocationTexcoord2 = 4;
inline constexpr u32 kLocationTexcoord0W = 5;
inline constexpr u32 kLocationNormquat = 6;
inline constexpr u32 kLocationView = 7;

// RasterizerRegs::VSOutputAttributes::Semantic.
namespace semantic {
inline constexpr u32 kPositionX = 0, kPositionY = 1, kPositionZ = 2, kPositionW = 3;
inline constexpr u32 kQuaternionX = 4, kQuaternionY = 5, kQuaternionZ = 6, kQuaternionW = 7;
inline constexpr u32 kColorR = 8, kColorG = 9, kColorB = 10, kColorA = 11;
inline constexpr u32 kTexcoord0U = 12, kTexcoord0V = 13, kTexcoord1U = 14, kTexcoord1V = 15;
inline constexpr u32 kTexcoord0W = 16;
inline constexpr u32 kViewX = 18, kViewY = 19, kViewZ = 20;
inline constexpr u32 kTexcoord2U = 22, kTexcoord2V = 23;
inline constexpr u32 kInvalid = 31;
inline constexpr u32 kCount = 24;
} // namespace semantic

// The internal registers the key reads besides the shader block (regs.h has the shader blocks):
// RasterizerRegs at 0x40.
namespace reg {
inline constexpr u32 kClipEnable = 0x047;           // bit 0
inline constexpr u32 kClipCoef = 0x048;             // 4 registers, float24
inline constexpr u32 kVsOutputTotal = 0x04F;        // bits 0-2
inline constexpr u32 kVsOutputAttributes = 0x050;   // 7 registers: map_x 0-4, map_y 8-12, map_z 16-20, map_w 24-28
} // namespace reg

// ---- the uniform blocks ---------------------------------------------------------------------------

struct PicaUniformData {
    u32 b = 0;
    u32 pad0[3] = {};
    u32 i[4][4] = {};   // [register][x, y, z, w]
    float f[96][4] = {}; // [register][x, y, z, w]
};
static_assert(sizeof(PicaUniformData) == 1616, "std140 vs_pica_data");

struct VertexUniformData {
    u32 enable_clip1 = 0;
    u32 flip_viewport = 0;
    u32 pad[2] = {};
    float clip_coef[4] = {};
};
static_assert(sizeof(VertexUniformData) == 32, "std140 vs_data");

// VSPicaUniformData::SetFromRegs.
PicaUniformData MakePicaUniformData(const pica::ShaderSetup& setup);
// The same written straight into `out` (sizeof(PicaUniformData) bytes, e.g. a ring's mapping): no copy on the stack.
void WritePicaUniformData(const pica::ShaderSetup& setup, PicaUniformData* out);
// What Azahar's RasterizerAccelerated::SyncClipEnabled / SyncClipCoef / the viewport flip set.
VertexUniformData MakeVertexUniformData(const pica::PicaState& state, bool flip_viewport);

// ---- the test hook's storage buffer ---------------------------------------------------------------

// `capture_outputs`: the shader writes, at vec4 index gl_VertexIndex * kCaptureVec4PerVertex + k:
//   k 0-15   the output attributes vs_out_attr0 .. 15 as the program left them (zero beyond num_outputs)
//   k 16     primary_color                         k 17  (texcoord0.xy, texcoord1.xy)
//   k 18     (texcoord2.xy, texcoord0_w, 0)        k 19  normquat
//   k 20     (view.xyz, 0)                         k 21  gl_Position
//   k 22     (clip distance 0, clip distance 1, 0, 0)  (zeros without use_clip_planes)  k 23 zero
inline constexpr u32 kCaptureVec4PerVertex = 24;
inline constexpr u32 kCaptureOutputs = 0, kCapturePrimaryColor = 16, kCaptureTexcoord01 = 17,
                     kCaptureTexcoord2W = 18, kCaptureNormquat = 19, kCaptureView = 20,
                     kCapturePosition = 21, kCaptureClip = 22;

// ---- the key ---------------------------------------------------------------------------------------

struct InputRegisterConfig {
    vertex::Conversion conversion = vertex::Conversion::None;
    bool alias_w = false;
    bool operator==(const InputRegisterConfig&) const = default;
};

// `pull_fixup`: where an input register's attribute is (the vertex loader's description).
struct PullAttribute {
    u8 binding = 0;
    vertex::VertexFormat format = vertex::VertexFormat::R32G32B32A32_FLOAT;
    u8 per_instance = 0;
    u8 pad = 0;
    u16 offset = 0;
    u16 stride = 0;
    bool operator==(const PullAttribute&) const = default;
};

struct VertexShaderConfig {
    u64 program_hash = 0;   // of the loaded words
    u64 swizzle_hash = 0;
    u32 program_words = 0;  // how many words were loaded (ShaderSetup::biggest_program_size)
    u32 swizzle_words = 0;
    u32 main_offset = 0;
    u32 output_mask = 0;    // the vertex shader unit's output mask (16 bits)
    u32 outmap_total = 0;   // vs_output_total (0-7)
    std::array<u32, 7> outmap_attributes{}; // raw vs_output_attributes
    std::array<InputRegisterConfig, 16> inputs{};
    bool use_clip_planes = true;  // write gl_ClipDistance (needs shaderClipDistance)
    bool geometry_stage = false;  // outputs for the fixed geometry shader (see above)
    bool capture_outputs = false; // the test hook
    bool exact_arithmetic = true; // DecompileInput::exact_arithmetic
    bool pull_fixup = false;      // the lighting fix-up in this stage (see above); geometry_stage is then false
    std::array<PullAttribute, 16> pull{};  // pull_fixup: each input register's attribute (zero otherwise)

    bool operator==(const VertexShaderConfig&) const = default;
    u64 Hash() const;
};

// The words of a program or of the swizzle table hashed the way the key does (a 64-bit splitmix-style mix of the words and their count, not cryptographic).
u64 HashWords(const u32* words, size_t count);

struct ConfigOptions {
    bool use_clip_planes = true;
    bool geometry_stage = false;
    bool capture_outputs = false;
    bool exact_arithmetic = true;
    bool pull_fixup = false;  // needs `loaded`
};

// The key of the vertex shader a draw runs: `state` is the processor's state at the draw (its vs
// setup, shader block and OUTMAP registers), `loaded` the vertex loader's description of the draw
// (the conversion of each input register); null: every input is Float.
VertexShaderConfig MakeVertexShaderConfig(const pica::PicaState& state, const vertex::LoadedDraw* loaded,
                                          const ConfigOptions& options = {});

// ---- the result --------------------------------------------------------------------------------------

struct SemanticSlot {
    u8 attribute = 16; // 16: no output attribute maps this semantic
    u8 component = 0;
};

struct TranslatedVertexShader {
    std::vector<u32> spirv;
    // The output map of the key's registers.
    u32 num_outputs = 0;
    std::array<u8, 16> output_map{};                       // output register -> attribute, 16 = none
    std::array<SemanticSlot, semantic::kCount> semantic_map{};
    u16 used_inputs = 0;                                   // bit r: the shader reads input register r
    // Informational.
    std::vector<std::string> notes;
    bool uses_unverified_instructions = false;             // DST, DSTI or LIT (see glsl_decompiler.cpp)
    bool differs_from_interpreter = false;                 // a HINT: a shape the interpreter may run differently (see glsl_decompiler.cpp)
    std::string glsl;                                      // the source (kept: the shaders are small)
    double compile_ms = 0.0;                               // the glslang time
};

struct TranslateResult {
    std::shared_ptr<const TranslatedVertexShader> shader; // null when the program is not translated
    std::string error;                                    // then: why
    std::string error_category;                           // ... the cause in a word: no part of the program, no GLSL
    int error_word = -1;                                  // ... the offset of the word at fault, -1 when none
    bool ok() const {
        return shader != nullptr;
    }
};

// Translates and compiles. The program is read from `setup` (its first config.program_words words
// and config.swizzle_words operand descriptors); a program that is not a valid vertex program (it
// reads or jumps outside the loaded code, never ends, recurses, uses an instruction that has no
// meaning) is rejected with a message, never run. Safe to call from several threads.
TranslateResult TranslateVertexShader(const pica::ShaderSetup& setup, const VertexShaderConfig& config);

// The GLSL only (for tests and tools): "" and an error message for a program that is not translated. With
// `quaternion_compute` (a `pull_fixup` config only): the quaternion pass's compute shader instead (see above).
TranslateResult GenerateVertexGlsl(const pica::ShaderSetup& setup, const VertexShaderConfig& config,
                                   bool quaternion_compute = false);

// The flip pass's compute shader (see above): the same for every program.
std::string FlipPassGlsl();

// Compiles Vulkan GLSL (#version 450) to SPIR-V 1.5 (Vulkan 1.2), a vertex stage or a compute stage. On failure
// `error` has glslang's log.
bool CompileGlslToSpirv(const std::string& source, std::vector<u32>& spirv, std::string& error,
                        double* milliseconds = nullptr, bool compute = false);

// ---- the cache ---------------------------------------------------------------------------------------

class VertexShaderCache {
public:
    struct Stats {
        u64 lookups = 0;
        u64 hits = 0;
        u64 compiles = 0;  // misses that ran the translator (successes and failures)
        u64 failures = 0;  // compiles that produced no shader
    };

    // The shader of `config`, translating it on a miss. A program that fails is remembered as a
    // failure (the next lookup of the same key does not translate it again). Thread-safe; the
    // translation runs with the lock released, so two threads that miss on one key can both
    // translate it (the second result is dropped).
    TranslateResult Get(const pica::ShaderSetup& setup, const VertexShaderConfig& config);

    Stats stats() const;
    size_t size() const;
    void Clear();

private:
    struct KeyHash {
        size_t operator()(const VertexShaderConfig& c) const {
            return static_cast<size_t>(c.Hash());
        }
    };
    mutable std::mutex mutex_;
    std::unordered_map<VertexShaderConfig, TranslateResult, KeyHash> entries_;
    Stats stats_;
};

} // namespace mh4n::gpu::shader
