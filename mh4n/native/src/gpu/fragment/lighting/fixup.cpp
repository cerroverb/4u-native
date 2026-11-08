// Derived from Azahar src/video_core/shader/generator/glsl_shader_gen.cpp (GetGSCommonSource, GenerateFixedGeometryShader,
// the VSUniformBlockDef text and SanitizeVertex) at 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-2.0-or-later; the file
// says "Licensed under GPLv2 or any later version"). The glslang call is written from glslang's public API, after
// fragment/spirv_compile.cpp.
// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project (glsl_shader_gen.cpp)
//
// See fixup.h.
#include "gpu/fragment/lighting/fixup.h"

#include <initializer_list>
#include <mutex>

#include <fmt/format.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

namespace mh4n::gpu::fragment::lighting {
namespace {

// RasterizerRegs::VSOutputAttributes::Semantic.
constexpr u32 kPositionX = 0, kPositionY = 1, kPositionZ = 2, kPositionW = 3;
constexpr u32 kQuaternionX = 4, kQuaternionY = 5, kQuaternionZ = 6, kQuaternionW = 7;
constexpr u32 kColorR = 8, kColorG = 9, kColorB = 10, kColorA = 11;
constexpr u32 kTexcoord0U = 12, kTexcoord0V = 13, kTexcoord1U = 14, kTexcoord1V = 15, kTexcoord0W = 16;
constexpr u32 kViewX = 18, kViewY = 19, kViewZ = 20, kTexcoord2U = 22, kTexcoord2V = 23;

// The vertex stage's second uniform block (binding 1, VertexUniformData) and SanitizeVertex, as shader/vertex_shader.cpp
// declares them.
constexpr const char* kVertexDataBlock = R"(layout (set = 0, binding = 1, std140) uniform vs_data {
    uint enable_clip1;
    uint flip_viewport;
    vec4 clip_coef;
};

const vec2 EPSILON_Z = vec2(0.000001f, -1.00001f);

vec4 SanitizeVertex(vec4 vtx_pos) {
    float ndc_z = vtx_pos.z / vtx_pos.w;
    if (ndc_z > 0.f && ndc_z < EPSILON_Z[0]) {
        vtx_pos.z = 0.f;
    }
    if (ndc_z < -1.f && ndc_z > EPSILON_Z[1]) {
        vtx_pos.z = -vtx_pos.w;
    }
    return vtx_pos;
}
)";

// The fragment stage's inputs (the locations of Azahar's ATTRIBUTE_*, shader/vertex_shader.h).
constexpr const char* kOutputs = R"(layout(location = 1) out vec4 primary_color;
layout(location = 2) out vec2 texcoord0;
layout(location = 3) out vec2 texcoord1;
layout(location = 4) out vec2 texcoord2;
layout(location = 5) out float texcoord0_w;
layout(location = 6) out vec4 normquat;
layout(location = 7) out vec3 view;
)";

void InitializeGlslang() {
    static std::once_flag once;
    std::call_once(once, [] { glslang::InitializeProcess(); });
}

}  // namespace

std::string GenerateFixupGeometryGlsl(const FixupStageInputs& in) {
    // A vertex stage with no output attribute still gets one (zero) attribute per vertex: a GLSL array cannot be empty.
    const u32 attributes = in.num_outputs == 0 ? 1 : in.num_outputs;
    std::string out = "#version 450\n\n"
                      "layout(triangles) in;\n"
                      "layout(triangle_strip, max_vertices = 3) out;\n\n";
    out += kVertexDataBlock;
    out += '\n';
    for (u32 i = 0; i < in.num_outputs; ++i) {
        out += fmt::format("layout(location = {0}) in vec4 vs_out_attr{0}[];\n", i);
    }
    out += kOutputs;
    out += fmt::format("\nstruct Vertex {{\n    vec4 attributes[{}];\n}};\n\n", attributes);

    const auto semantic = [&](u32 s) -> std::string {
        const FixupStageInputs::Slot slot = in.semantic_map[s];
        if (slot.attribute < in.num_outputs) {
            return fmt::format("vtx.attributes[{}].{}", slot.attribute, "xyzw"[slot.component & 3]);
        }
        return "1.0";
    };
    const auto vec = [&](std::initializer_list<u32> list) {
        std::string s;
        for (u32 id : list) {
            s += (s.empty() ? "" : ", ") + semantic(id);
        }
        return s;
    };

    out += "vec4 GetVertexQuaternion(Vertex vtx) {\n";
    out += "    return vec4(" + vec({kQuaternionX, kQuaternionY, kQuaternionZ, kQuaternionW}) + ");\n";
    out += "}\n\n";

    out += "void EmitVtx(Vertex vtx, bool quats_opposite) {\n";
    out += "    vec4 vtx_pos = vec4(" + vec({kPositionX, kPositionY, kPositionZ, kPositionW}) + ");\n";
    out += "    vtx_pos = SanitizeVertex(vtx_pos);\n";
    out += "    if (flip_viewport != 0u) {\n";
    out += "        vtx_pos.y = -vtx_pos.y;\n";
    out += "    }\n";
    out += "    gl_Position = vec4(vtx_pos.x, vtx_pos.y, -vtx_pos.z, vtx_pos.w);\n";
    if (in.clip_distance) {
        out += "    gl_ClipDistance[0] = -vtx_pos.z;\n";  // fixed PICA clipping plane z <= 0
        out += "    if (enable_clip1 != 0u) {\n";
        out += "        gl_ClipDistance[1] = dot(clip_coef, vtx_pos);\n";
        out += "    } else {\n";
        out += "        gl_ClipDistance[1] = 0.0;\n";
        out += "    }\n";
    }
    out += "    vec4 vtx_quat = GetVertexQuaternion(vtx);\n";
    out += "    normquat = mix(vtx_quat, -vtx_quat, bvec4(quats_opposite));\n";
    out += "    vec4 vtx_color = abs(vec4(" + vec({kColorR, kColorG, kColorB, kColorA}) + "));\n";
    // As the vertex stage clamps it (shader/vertex_shader.cpp): c < 1 ? c : 1, so a NaN gives 1.
    out += "    primary_color = mix(vec4(1.0), vtx_color, lessThan(vtx_color, vec4(1.0)));\n";
    out += "    texcoord0 = vec2(" + vec({kTexcoord0U, kTexcoord0V}) + ");\n";
    out += "    texcoord1 = vec2(" + vec({kTexcoord1U, kTexcoord1V}) + ");\n";
    out += "    texcoord0_w = " + semantic(kTexcoord0W) + ";\n";
    out += "    view = vec3(" + vec({kViewX, kViewY, kViewZ}) + ");\n";
    out += "    texcoord2 = vec2(" + vec({kTexcoord2U, kTexcoord2V}) + ");\n";
    out += "    EmitVertex();\n";
    out += "}\n";

    out += R"(
bool AreQuaternionsOpposite(vec4 qa, vec4 qb) {
    return (dot(qa, qb) < 0.0);
}

void EmitPrim(Vertex vtx0, Vertex vtx1, Vertex vtx2) {
    EmitVtx(vtx0, false);
    EmitVtx(vtx1, AreQuaternionsOpposite(GetVertexQuaternion(vtx0), GetVertexQuaternion(vtx1)));
    EmitVtx(vtx2, AreQuaternionsOpposite(GetVertexQuaternion(vtx0), GetVertexQuaternion(vtx2)));
    EndPrimitive();
}

void main() {
    Vertex prim_buffer[3];
)";
    for (u32 v = 0; v < 3; ++v) {
        out += fmt::format("    prim_buffer[{}].attributes = vec4[{}](", v, attributes);
        if (in.num_outputs == 0) {
            out += "vec4(0.0, 0.0, 0.0, 1.0)";
        }
        for (u32 i = 0; i < in.num_outputs; ++i) {
            out += fmt::format("{}vs_out_attr{}[{}]", i == 0 ? "" : ", ", i, v);
        }
        out += ");\n";
    }
    out += "    EmitPrim(prim_buffer[0], prim_buffer[1], prim_buffer[2]);\n";
    out += "}\n";
    return out;
}

std::vector<u32> GenerateFixupGeometrySpirv(const FixupStageInputs& inputs, std::string* error) {
    InitializeGlslang();
    const std::string source = GenerateFixupGeometryGlsl(inputs);
    const auto fail = [&](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return std::vector<u32>{};
    };

    glslang::TShader shader(EShLangGeometry);
    const char* strings[1] = {source.c_str()};
    shader.setStrings(strings, 1);
    // Vulkan 1.2 and SPIR-V 1.5, as the vertex and fragment stages.
    shader.setEnvInput(glslang::EShSourceGlsl, EShLangGeometry, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
    const EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(GetDefaultResources(), 450, false, messages)) {
        return fail(std::string(shader.getInfoLog()) + shader.getInfoDebugLog());
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) {
        return fail(std::string(program.getInfoLog()) + program.getInfoDebugLog());
    }
    const glslang::TIntermediate* intermediate = program.getIntermediate(EShLangGeometry);
    if (intermediate == nullptr) {
        return fail("glslang produced no geometry stage");
    }
    glslang::SpvOptions options;
    options.disableOptimizer = true;  // glslang is built without spirv-tools
    options.validate = false;
    std::vector<unsigned int> words;
    glslang::GlslangToSpv(*intermediate, words, &options);
    if (words.empty()) {
        return fail("glslang produced no SPIR-V for the geometry stage");
    }
    return std::vector<u32>(words.begin(), words.end());
}

}  // namespace mh4n::gpu::fragment::lighting
