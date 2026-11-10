// Derived from Azahar src/video_core/shader/generator/glsl_shader_gen.cpp and glsl_fs_shader_gen.cpp at
// 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (labelled GPL-3.0-or-later: glsl_shader_gen.cpp itself says GPLv2+, but the SPIR-V in the
// included file is compiled from shaders/trivial.vert, which takes the varying locations from glsl_fs_shader_gen.cpp, GPLv3+).
// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project (glsl_shader_gen.cpp, glsl_fs_shader_gen.cpp)
#include "gpu/cpuvertex/trivial_vertex.h"

#include "gpu/cpuvertex/trivial_vertex_spv.inc"
#include "gpu/cpuvertex/trivial_vertex_spv_v11.inc"
#include "mh4n/spirv_target.h"

namespace mh4n::gpu::cpuvertex {

std::span<const u32> TrivialVertexShader(bool use_clip_planes) {
    if (SpirvTarget13()) {  // (SPIR-V 1.3 below Vulkan 1.2: mh4n/spirv_target.h)
        return use_clip_planes ? std::span<const u32>(spv_trivial_vertex_clip_v11)
                               : std::span<const u32>(spv_trivial_vertex_noclip_v11);
    }
    return use_clip_planes ? std::span<const u32>(spv_trivial_vertex_clip)
                           : std::span<const u32>(spv_trivial_vertex_noclip);
}

} // namespace mh4n::gpu::cpuvertex
