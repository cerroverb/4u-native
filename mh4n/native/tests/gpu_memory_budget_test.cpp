// Tests for the renderer's texture memory budgets (gpu/native/memory_budget.h; 1.0.2, S9, 4u-native #67): without an HD
// pack 1.0.1's steps, with one half the memory available less the margin, never under the step and at most the ceiling,
// the variables deciding when set, and the halving when the system asks for memory back.
#include <doctest/doctest.h>

#include "gpu/native/memory_budget.h"

using namespace mh4n;
using namespace mh4n::gpu::native;

TEST_CASE("memory budget: 1.0.1's steps without an HD pack, whatever is available") {
    CHECK(StepBudgetMb(0) == 768);
    CHECK(StepBudgetMb(3000) == 192);
    CHECK(StepBudgetMb(4000) == 256);
    CHECK(StepBudgetMb(6000) == 384);
    CHECK(StepBudgetMb(16000) == 768);
    for (u64 device : {3000ull, 4000ull, 6000ull, 16000ull}) {
        const MemoryBudgets b = ChooseMemoryBudgets(device, 12000, false);
        CHECK(b.texture_mb == StepBudgetMb(device));
        CHECK(b.hd_mb == StepBudgetMb(device) / 8 * 5);  // (1.0.1's five eighths)
        CHECK(!b.fixed);
    }
}

TEST_CASE("memory budget: an HD pack follows the memory available") {
    // a 16 GB phone with 10 GB available: half less 512 MB is 4.5 GB, the ceiling 3 GB; the game keeps the step's 3/8
    MemoryBudgets b = ChooseMemoryBudgets(16000, 10000, true);
    CHECK(b.texture_mb == kBudgetCeilingMb);
    CHECK(b.hd_mb == kBudgetCeilingMb - 768 / 8 * 3);
    // an 8 GB phone with 4 GB available: 1536 MB
    b = ChooseMemoryBudgets(8000, 4096, true);
    CHECK(b.texture_mb == 1536);
    CHECK(b.hd_mb == 1536 - 288);
    // a 4 GB phone (a Moto g06) with 1.2 GB available: never under 1.0.1's step, so the same as 1.0.1
    b = ChooseMemoryBudgets(4000, 1200, true);
    CHECK(b.texture_mb == 256);
    CHECK(b.hd_mb == 160);
    // nothing known: the step
    b = ChooseMemoryBudgets(6000, -1, true);
    CHECK(b.texture_mb == 384);
    CHECK(b.hd_mb == 240);
}

TEST_CASE("memory budget: MH4N_TEXTURE_BUDGET_MB and MH4N_HD_BUDGET_MB still decide") {
    MemoryBudgets b = ChooseMemoryBudgets(16000, 10000, true, 1000);
    CHECK(b.texture_mb == 1000);
    CHECK(b.hd_mb == 625);
    CHECK(b.fixed);
    CHECK(LowerMemoryBudgets(b).texture_mb == 1000);  // (never lowered)
    b = ChooseMemoryBudgets(16000, 10000, true, 1000, 300);
    CHECK(b.hd_mb == 300);
    b = ChooseMemoryBudgets(16000, 10000, true, 0, 300);  // (only the packs' share set)
    CHECK(b.texture_mb == kBudgetCeilingMb);
    CHECK(b.hd_mb == 300);
}

TEST_CASE("memory budget: halved when the system asks for memory back, never under the step") {
    MemoryBudgets b = ChooseMemoryBudgets(16000, 10000, true);
    b = LowerMemoryBudgets(b);
    CHECK(b.texture_mb == 1536);
    CHECK(b.hd_mb == 1536 - 288);
    b = LowerMemoryBudgets(LowerMemoryBudgets(LowerMemoryBudgets(b)));
    CHECK(b.texture_mb == 768);
    CHECK(b.hd_mb == 480);
    CHECK(LowerMemoryBudgets(b).texture_mb == 768);  // (at the step: stays)
    const MemoryBudgets none = ChooseMemoryBudgets(4000, 1200, false);
    CHECK(LowerMemoryBudgets(none).texture_mb == 256);
}
