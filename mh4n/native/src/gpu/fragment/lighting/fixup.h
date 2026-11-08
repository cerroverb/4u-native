// Derived from Azahar src/video_core/shader/generator/glsl_shader_gen.cpp (GetGSCommonSource and
// GenerateFixedGeometryShader) and shader_gen.h (PicaGSConfigState's semantic map) at
// 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-2.0-or-later; both files say "Licensed under GPLv2 or any later
// version").
// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project (glsl_shader_gen.cpp, shader_gen.h)
//
// The quaternion fix-up stage of lighting/ (N2-09; docs/native/gpu-fragment-design.md 5.5): the fixed geometry shader
// that Azahar puts between a translated vertex shader and the fragment shader of a lit draw (`triangles` in,
// `triangle_strip` out, three vertices). It reads the vertex stage's output attributes (`vs_out_attr<n>`, written when
// the vertex stage is translated with `geometry_stage`, shader/vertex_shader.h), does what the vertex stage would have
// done with them (position, clip distances, primary colour, texture coordinates, view) and flips the quaternion of the
// second and third vertex when its dot product with the first vertex's is negative, so that the interpolated
// quaternion does not pass through zero between two vertices that hold the same rotation with opposite signs.
//
// The PICA does that flip in its rasteriser; Azahar's renderers do it in this stage (hardware vertex shader), on the CPU
// (software path: cpuvertex/ here) or with fragment barycentrics. The composition (native/) asks for this stage through
// its `FixupGeneratorRegistry`; the renderer registers `GenerateFixupGeometrySpirv` there. The same test can be made in
// the vertex stage instead (shader/vertex_shader.h, `pull_fixup`; the renderer's `--lighting-fixup vs`): the default on a
// Mali, whose geometry stage cost 17 ms of a Moto g06's frame, and on a device without geometry shaders.
//
// Written against the outputs of shader/vertex_shader.cpp, not Azahar's: the primary colour is clamped as the vertex
// stage clamps it (a NaN gives 1), and the clip distances are written only on a device that has them.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "mh4n/types.h"

namespace mh4n::gpu::fragment::lighting {

// What the geometry stage is generated from: the translated vertex stage's output map (native::VertexStageOutputs).
struct FixupStageInputs {
    u32 num_outputs = 0;  // the vertex stage's output attributes vs_out_attr0 .. num_outputs - 1
    struct Slot {
        u8 attribute = 16;  // 16: no output attribute maps this semantic
        u8 component = 0;
    };
    // RasterizerRegs::VSOutputAttributes::Semantic -> (attribute, component); 24 semantics.
    std::array<Slot, 24> semantic_map{};
    bool clip_distance = false;  // the device has shaderClipDistance (the vertex stage's `use_clip_planes`)
};

// The GLSL (#version 450, Vulkan) of the geometry stage.
std::string GenerateFixupGeometryGlsl(const FixupStageInputs& inputs);

// The GLSL compiled to SPIR-V with glslang; empty (and `error`, when given, holds glslang's log) on failure.
std::vector<u32> GenerateFixupGeometrySpirv(const FixupStageInputs& inputs, std::string* error = nullptr);

}  // namespace mh4n::gpu::fragment::lighting
