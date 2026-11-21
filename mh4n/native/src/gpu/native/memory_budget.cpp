#include "gpu/native/memory_budget.h"

#include <algorithm>

#include <fmt/format.h>

namespace mh4n::gpu::native {

u64 StepBudgetMb(u64 device_mb) {
    return device_mb == 0 ? 768 : device_mb <= 3200 ? 192 : device_mb <= 4600 ? 256 : device_mb <= 6600 ? 384 : 768;
}

namespace {
// The pack's share of a budget: what the game's own textures don't keep (the step's three eighths)
u64 HdShare(u64 texture_mb, u64 step_mb) {
    return texture_mb - step_mb / 8 * 3;
}
}  // namespace

MemoryBudgets ChooseMemoryBudgets(u64 device_mb, long available_mb, bool hd_pack, long env_texture_mb, long env_hd_mb) {
    MemoryBudgets b;
    b.step_mb = StepBudgetMb(device_mb);
    if (env_texture_mb > 0) {
        b.texture_mb = static_cast<u64>(env_texture_mb);
        b.hd_mb = env_hd_mb > 0 ? static_cast<u64>(env_hd_mb) : b.texture_mb / 8 * 5;
        b.fixed = true;
        b.why = env_hd_mb > 0 ? "MH4N_TEXTURE_BUDGET_MB and MH4N_HD_BUDGET_MB" : "MH4N_TEXTURE_BUDGET_MB";
        return b;
    }
    b.texture_mb = b.step_mb;
    if (!hd_pack) {
        b.why = fmt::format("no HD pack: 1.0.1's step for {} MB of memory", device_mb);
    } else if (available_mb < 0) {
        b.why = "an HD pack, the memory available not known: 1.0.1's step";
    } else {
        const u64 half = static_cast<u64>(available_mb) / 2;
        const u64 wanted = half > kBudgetMarginMb ? half - kBudgetMarginMb : 0;
        b.texture_mb = std::clamp(wanted, b.step_mb, std::max(b.step_mb, kBudgetCeilingMb));
        b.why = fmt::format("an HD pack: half of the {} MB available less {} MB is {} MB; at least the step ({} MB), at most "
                            "{} MB",
                            available_mb, kBudgetMarginMb, wanted, b.step_mb, kBudgetCeilingMb);
    }
    b.hd_mb = env_hd_mb > 0 ? static_cast<u64>(env_hd_mb) : HdShare(b.texture_mb, b.step_mb);
    if (env_hd_mb > 0) b.why += "; MH4N_HD_BUDGET_MB for the packs";
    return b;
}

MemoryBudgets LowerMemoryBudgets(const MemoryBudgets& now) {
    MemoryBudgets b = now;
    if (b.fixed || b.texture_mb <= b.step_mb) return b;
    b.texture_mb = std::max(b.step_mb, b.texture_mb / 2);
    b.hd_mb = std::min(b.hd_mb, HdShare(b.texture_mb, b.step_mb));
    b.why = "the system asked for memory back: halved";
    return b;
}

}  // namespace mh4n::gpu::native
