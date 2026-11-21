// The game clock and the two time sources (N1-06). Our own code; the only thing taken from Azahar is
// the tick rate (BASE_CLOCK_RATE_ARM11 of core/core_timing.h, a hardware constant).
#include "mh4n/clock.h"

#include <algorithm>

#include "mh4n/assert.h"

#if defined(_WIN32)
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace mh4n {

// ---- SteadyTimeSource --------------------------------------------------------------------------------

#if !defined(_WIN32)
void SetThreadWaitsCoarse() {}
#endif

s64 SteadyTimeSource::NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() -
                                                                epoch_)
        .count();
}

#if defined(_WIN32)
namespace {

// MH4N_WIN_WAIT=spin: libc++'s own timed waits (whole milliseconds, then 0 ms timeouts until the deadline)
bool LibcxxWaits() {
    static const bool spin = [] {
        const char* v = std::getenv("MH4N_WIN_WAIT");
        return v != nullptr && std::strcmp(v, "spin") == 0;
    }();
    return spin;
}

// The timer is set this early and the rest is waited for on the clock, the lock let go and a notify seen at once (its
// event polled) (MH4N_WIN_WAIT_SPIN_US, 1 ms by default). A high-resolution timer comes 0.1 to 0.4 ms late on average,
// now and then a millisecond or more on a busy PC, and a thread asleep in the kernel takes its time to answer: the
// game's thread waits for the renderer's interrupts and for the emulated VBlank (1000 a second at "No limit") several
// times a frame. Measured in the town at "No limit": 229-245 FPS with this, 216 spinning only before deadlines less
// than 1 ms away, 191 only before the VBlank's, 118 with no spin at all; libc++'s own waits made 226, with the
// preemption timer's thread spinning a whole core. That thread never spins here (SetThreadWaitsCoarse: a slice a little
// longer changes nothing).
thread_local bool t_coarse = false;

s64 SpinNs() {
    static const s64 ns = [] {
        const char* v = std::getenv("MH4N_WIN_WAIT_SPIN_US");
        return v != nullptr ? std::clamp<s64>(std::atoll(v), 0, 5000) * 1000 : 1000000;
    }();
    return t_coarse ? 0 : ns;
}

// A cv's wake-up: the event Notify sets and the timer its waiter sleeps on (one waiter a cv: the scheduler's idle wait,
// the preemption timer's). Made at the first wait or notify, kept for the process (a few handles).
struct Waker {
    HANDLE event = nullptr;
    HANDLE timer = nullptr;
};

Waker& WakerFor(const std::condition_variable& cv) {
    static std::mutex mutex;
    static std::unordered_map<const void*, Waker*> wakers;
    std::lock_guard guard(mutex);
    Waker*& w = wakers[&cv];
    if (w == nullptr) {
        w = new Waker;
        w->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
        w->timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (w->timer == nullptr) {  // (before Windows 10 1803: a plain one, the system's tick)
            w->timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        }
    }
    return *w;
}

}  // namespace

void SetThreadWaitsCoarse() {
    t_coarse = true;
}

void SteadyTimeSource::Notify(std::condition_variable& cv) {
    cv.notify_all();
    if (!LibcxxWaits()) {
        SetEvent(WakerFor(cv).event);
    }
}
#endif

bool SteadyTimeSource::WaitUntil(std::condition_variable& cv, std::unique_lock<std::mutex>& lock,
                                 std::optional<s64> deadline_ns, const std::function<bool()>& pred) {
    if (!deadline_ns) {
        cv.wait(lock, pred);
        return true;
    }
#if defined(_WIN32)
    if (!LibcxxWaits()) {
        Waker& w = WakerFor(cv);
        if (w.event != nullptr && w.timer != nullptr) {
            for (;;) {
                if (pred()) {
                    return true;
                }
                const s64 left = *deadline_ns - NowNs();
                if (left <= 0) {
                    return pred();
                }
                if (left <= SpinNs()) {
                    // the last stretch on the clock, the lock let go, the event polled for a notify (taking the
                    // caller's lock again and again here made its cache line bounce for the threads that notify)
                    lock.unlock();
                    while (NowNs() < *deadline_ns && WaitForSingleObject(w.event, 0) == WAIT_TIMEOUT) {
                        YieldProcessor();
                    }
                    lock.lock();
                    continue;
                }
                LARGE_INTEGER due;
                due.QuadPart = -std::max<s64>((left - SpinNs()) / 100, 1);  // (relative, in 100 ns)
                SetWaitableTimerEx(w.timer, &due, 0, nullptr, nullptr, nullptr, 0);
                const HANDLE handles[2] = {w.event, w.timer};
                lock.unlock();
                WaitForMultipleObjects(2, handles, FALSE, INFINITE);
                lock.lock();
            }
        }
    }
#endif
    return cv.wait_until(lock, epoch_ + std::chrono::nanoseconds(*deadline_ns), pred);
}

// ---- ManualTimeSource --------------------------------------------------------------------------------

bool ManualTimeSource::WaitUntil(std::condition_variable& cv, std::unique_lock<std::mutex>& lock,
                                 std::optional<s64> deadline_ns, const std::function<bool()>& pred) {
    if (pred()) {
        return true;
    }
    if (!deadline_ns) {
        cv.wait(lock, pred);
        return true;
    }
    // Nothing else can happen before the deadline in a simulation: skip the idle time.
    s64 now = now_.load(std::memory_order_acquire);
    while (now < *deadline_ns && !now_.compare_exchange_weak(now, *deadline_ns)) {
    }
    return pred();
}

void ManualTimeSource::Advance(s64 ns) {
    MH4N_ASSERT(ns >= 0, "ManualTimeSource::Advance({})", ns);
    now_.fetch_add(ns, std::memory_order_acq_rel);
    if (hook_) {
        hook_();
    }
}

void ManualTimeSource::Set(s64 ns) {
    MH4N_ASSERT(ns >= now_.load(), "ManualTimeSource::Set({}) goes backwards", ns);
    now_.store(ns, std::memory_order_release);
    if (hook_) {
        hook_();
    }
}

// ---- GameClock ---------------------------------------------------------------------------------------

GameClock::GameClock(TimeSource& source) : source_(source), base_host_ns_(source.NowNs()) {}

u64 GameClock::NowLocked() const {
    if (!Running()) {
        return base_ticks_;
    }
    const s64 elapsed = std::max<s64>(source_.NowNs() - base_host_ns_, 0);
    // base + elapsed: NsToTicks saturates, so does the sum.
    const u64 ticks = NsToTicks(static_cast<u64>(elapsed));
    u64 now = base_ticks_ + ticks;
    if (now < base_ticks_) {
        now = ~u64{0};
    }
    return limit_ ? std::min(now, *limit_) : now;
}

u64 GameClock::Now() const {
    std::lock_guard lock(mutex_);
    return NowLocked();
}

void GameClock::Snapshot() {
    base_ticks_ = NowLocked();
    base_host_ns_ = source_.NowNs();
}

void GameClock::Notify() {
    std::function<void()> callback;
    {
        std::lock_guard lock(mutex_);
        callback = on_change_;
    }
    if (callback) {
        callback();
    }
}

bool GameClock::SetStopped(bool paused, u32 halted) {
    if (paused == paused_ && halted == halted_) {
        return false;
    }
    const bool was_running = Running();
    if (was_running) {
        Snapshot();  // (it stops now: the reading so far is kept)
    }
    paused_ = paused;
    halted_ = halted;
    if (!was_running && Running()) {
        base_host_ns_ = source_.NowNs();  // the time it stood still is dropped
    }
    return true;
}

void GameClock::Pause() {
    bool changed;
    {
        std::lock_guard lock(mutex_);
        changed = SetStopped(true, halted_);
    }
    if (changed) {
        Notify();
    }
}

void GameClock::Resume() {
    bool changed;
    {
        std::lock_guard lock(mutex_);
        changed = SetStopped(false, halted_);
    }
    if (changed) {
        Notify();
    }
}

bool GameClock::Paused() const {
    std::lock_guard lock(mutex_);
    return paused_;
}

void GameClock::Halt(HaltBy who) {
    bool changed;
    {
        std::lock_guard lock(mutex_);
        changed = SetStopped(paused_, halted_ | who);
    }
    if (changed) {
        Notify();
    }
}

void GameClock::Unhalt(HaltBy who) {
    bool changed;
    {
        std::lock_guard lock(mutex_);
        changed = SetStopped(paused_, halted_ & ~static_cast<u32>(who));
    }
    if (changed) {
        Notify();
    }
}

u32 GameClock::Halted() const {
    std::lock_guard lock(mutex_);
    return halted_;
}

void GameClock::Hold(u64 until_tick) {
    {
        std::lock_guard lock(mutex_);
        Snapshot();  // the time that already stood still at an older limit is dropped here
        limit_ = std::max(until_tick, base_ticks_);
    }
    Notify();
}

void GameClock::ReleaseHold() {
    {
        std::lock_guard lock(mutex_);
        if (!limit_) {
            return;
        }
        Snapshot();
        limit_.reset();
    }
    Notify();
}

std::optional<u64> GameClock::HeldAt() const {
    std::lock_guard lock(mutex_);
    return limit_;
}

std::optional<s64> GameClock::HostNsFor(u64 tick) const {
    std::lock_guard lock(mutex_);
    const u64 now = NowLocked();
    if (tick <= now) {
        return source_.NowNs();
    }
    if (!Running() || (limit_ && tick > *limit_)) {
        return std::nullopt;
    }
    // base_host + the least time that covers (tick - base) ticks; the host nanoseconds are s64.
    const u64 dt_ns = TicksToNsCeil(tick - base_ticks_);
    if (dt_ns > static_cast<u64>(INT64_MAX - base_host_ns_)) {
        return std::nullopt;
    }
    return base_host_ns_ + static_cast<s64>(dt_ns);
}

void GameClock::SetChangeCallback(std::function<void()> callback) {
    std::lock_guard lock(mutex_);
    on_change_ = std::move(callback);
}

}  // namespace mh4n
