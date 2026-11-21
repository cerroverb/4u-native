// The renderer's texture memory budgets (native_gpu.cpp TextureBudgetBytes, HdBudgetBytes): what the textures' images may
// take before the least recently used go, and the HD packs' share of it (hd_textures.h).
//
// 1.0.1 had fixed steps by the device's memory: up to 3 GB 192 MB, up to 4.5 GB 256 MB, up to 6.5 GB 384 MB, more 768 MB,
// five eighths of it for the packs. A 16 GB phone's pack stopped at 480 MB of its 768 (4u-native #67). From 1.0.2 (S9,
// "use the memory there is"), with an HD pack the budget follows the memory the system has available when the renderer
// starts: half of it less a margin (the game, the GPU's other memory and the system share the same RAM), never under
// 1.0.1's step for that device and at most a ceiling; the game's own textures keep the step's three eighths, as before,
// and the pack the rest. Without a pack nothing changes (the step, as 1.0.1). When the system asks for memory back
// (Android's onTrimMemory while running, or little memory available: native_gpu.cpp MemoryLow, TrimForMemory) the budget
// is halved, never under the step, for the rest of the session, and the least recently used images go down to it.
// MH4N_TEXTURE_BUDGET_MB and MH4N_HD_BUDGET_MB still decide when set (the HD share then five eighths, as 1.0.1).
#pragma once

#include <string>

#include "mh4n/types.h"

namespace mh4n::gpu::native {

inline constexpr u64 kBudgetMarginMb = 512;    // left out of the half of the memory available
inline constexpr u64 kBudgetCeilingMb = 3072;  // the most, whatever the device has

struct MemoryBudgets {
    u64 texture_mb = 0;  // all the textures' images
    u64 hd_mb = 0;       // the HD packs' share of them
    u64 step_mb = 0;     // 1.0.1's step for the device (the least the budget goes down to)
    bool fixed = false;  // MH4N_TEXTURE_BUDGET_MB decides: never lowered
    std::string why;     // for the log
};

// 1.0.1's step by the device's memory (MB; 0: not known, 768)
u64 StepBudgetMb(u64 device_mb);
// The budgets at the start. `available_mb` < 0: not known (the step). env_texture_mb / env_hd_mb > 0: the variables'.
MemoryBudgets ChooseMemoryBudgets(u64 device_mb, long available_mb, bool hd_pack, long env_texture_mb = 0, long env_hd_mb = 0);
// The system asked for memory back: halved, never under the step (a fixed one, or one at the step, stays)
MemoryBudgets LowerMemoryBudgets(const MemoryBudgets& now);

}  // namespace mh4n::gpu::native
