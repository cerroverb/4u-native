// The SPIR-V the runtime's glslang makes (gpu/shader, gpu/fragment, gpu/fragment/lighting/fixup.cpp, present): SPIR-V 1.5
// for Vulkan 1.2, or SPIR-V 1.3 for Vulkan 1.1, the newest a device below Vulkan 1.2 has to take. A 1.5 module is outside
// the spec there, and only such devices (Mali-G57 and Adreno 6xx drivers of 1.1) showed some of the players' misdraws
// (4u-native #5, #18, #21, #24); the MHGU port gives them 1.3 as well. Set once, when the device is made
// (backend/core/device.cpp), before any shader is compiled; the precompiled modules (readback, cpuvertex) have a 1.3 copy.
// MH4N_SPIRV=1.3 or 1.5 picks it on any device (a test).
#pragma once

#include <atomic>

namespace mh4n::gpu {

inline std::atomic<bool> g_spirv_1_3{false};

inline bool SpirvTarget13() {
    return g_spirv_1_3.load(std::memory_order_relaxed);
}

}  // namespace mh4n::gpu
