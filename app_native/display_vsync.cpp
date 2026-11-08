// The display's vsync, for the game's emulated VBlank to follow (mh4n/pacing_trace.h, gsp FollowDisplay): Choreographer's
// frame times, sampled on a thread of its own. Every quarter of a second two vsyncs in a row are taken: the second one's
// time is the phase, their distance a measure of the period (averaged; a jump of more than 5 % is a new display rate
// and starts the average again). With MH4N_PACING_TRACE set every vsync is taken and marked in the trace.
//   MH4U_VBLANK_LOCK=off      the VBlank keeps its own clock (as before)
//   MH4U_VBLANK_LOCK=<us>     the VBlank that long after the display's vsync (default 0)
// Also the surface's frame-rate vote (ANativeWindow_setFrameRate, the FPS cap): the Galaxy dropped the panel to 60 Hz and
// the app to 15 frames a second for tens of seconds at a time (MHGU too) with no vote on the surface
// (MH4U_FRAME_RATE_VOTE=0: none).
#include "display_vsync.h"

#include <android/choreographer.h>
#include <android/log.h>
#include <android/looper.h>
#include <android/native_window.h>
#include <SDL3/SDL.h>
#include <dlfcn.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "mh4n/pacing_trace.h"
#include "mh4n/host_thread_name.h"

namespace {

using Callback64 = void (*)(int64_t, void*);
using Post64 = void (*)(AChoreographer*, Callback64, void*);
using PostLong = void (*)(AChoreographer*, void (*)(long, void*), void*);

struct Sampler {
    AChoreographer* choreographer = nullptr;
    Post64 post64 = nullptr;
    PostLong post_long = nullptr;
    bool every = false;     // every vsync (the trace)
    int taken = 0;          // vsyncs taken in this sample (2: done)
    int64_t first = 0;
    double period = 0;      // the average, ns
};
Sampler g_sampler;

void Post();

void OnVsync(int64_t t) {
    Sampler& s = g_sampler;
    if (s.taken == 0) {
        s.first = t;
        s.taken = 1;
        Post();
        return;
    }
    const double d = static_cast<double>(t - s.first);
    if (d > 2e6 && d < 50e6) {  // (one period: between 20 and 500 Hz)
        if (s.period <= 0 || std::fabs(d - s.period) > 0.05 * s.period) {
            s.period = d;
        } else {
            s.period += (d - s.period) / 16;
        }
        mh4n::pacing::g_display_period_ns.store(static_cast<std::uint64_t>(std::llround(s.period)), std::memory_order_relaxed);
        mh4n::pacing::g_display_vsync_ns.store(static_cast<std::uint64_t>(t), std::memory_order_relaxed);
    }
    if (s.every) {
        mh4n::pacing::MarkAt(mh4n::pacing::kVsync, static_cast<std::uint32_t>(s.period / 1000), static_cast<std::uint64_t>(t));
        s.first = t;
        Post();
        return;
    }
    s.taken = 2;
}

void OnVsync64(int64_t t, void*) { OnVsync(t); }
void OnVsyncLong(long t, void*) { OnVsync(static_cast<int64_t>(t)); }

void Post() {
    if (g_sampler.post64 != nullptr) {
        g_sampler.post64(g_sampler.choreographer, OnVsync64, nullptr);
    } else {
        g_sampler.post_long(g_sampler.choreographer, OnVsyncLong, nullptr);
    }
}

void Run() {
    mh4n::SetHostThreadName("mh4u-vsync");
    ALooper_prepare(0);
    Sampler& s = g_sampler;
    s.choreographer = AChoreographer_getInstance();
    void* lib = dlopen("libandroid.so", RTLD_NOW);
    if (lib != nullptr) {
        s.post64 = reinterpret_cast<Post64>(dlsym(lib, "AChoreographer_postFrameCallback64"));  // (API 29)
        s.post_long = reinterpret_cast<PostLong>(dlsym(lib, "AChoreographer_postFrameCallback"));
    }
    if (s.choreographer == nullptr || (s.post64 == nullptr && s.post_long == nullptr)) {
        __android_log_print(ANDROID_LOG_WARN, "MH4U", "display vsync: no Choreographer");
        return;
    }
    s.every = mh4n::pacing::Trace::Get().On();
    for (;;) {
        s.taken = 0;
        Post();
        while (s.taken < 2) {
            ALooper_pollOnce(1000, nullptr, nullptr, nullptr);
        }
        ALooper_pollOnce(250, nullptr, nullptr, nullptr);  // (nothing else wakes this looper: a quarter-second sleep)
    }
}

}  // namespace

void display_vsync_start() {
    const char* lock = std::getenv("MH4U_VBLANK_LOCK");
    if (lock != nullptr && std::strcmp(lock, "off") == 0) {
        return;
    }
    const long offset_us = lock != nullptr ? std::strtol(lock, nullptr, 10) : 0;
    mh4n::pacing::g_vblank_lock_ns.store(static_cast<std::int64_t>(std::max(0L, offset_us)) * 1000, std::memory_order_relaxed);
    std::thread(Run).detach();
}

// ---- the surface's frame rate (a vote to SurfaceFlinger) ----
namespace {
SDL_Window* g_vote_window = nullptr;
std::atomic<float> g_vote_fps{0.0f};
}  // namespace

void display_set_frame_rate(SDL_Window* window, float fps) {
    if (window != nullptr) g_vote_window = window;
    if (fps > 0) g_vote_fps.store(fps, std::memory_order_relaxed);
    display_frame_rate_refresh();
}

void display_frame_rate_refresh() {
    const char* off = std::getenv("MH4U_FRAME_RATE_VOTE");
    if (off != nullptr && std::strcmp(off, "0") == 0) return;
    const float fps = g_vote_fps.load(std::memory_order_relaxed);
    if (g_vote_window == nullptr || fps <= 0) return;
    auto* surface = static_cast<ANativeWindow*>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(g_vote_window), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr));
    if (surface == nullptr) return;
    using WithStrategy = int32_t (*)(ANativeWindow*, float, int8_t, int8_t);  // (API 31)
    using Plain = int32_t (*)(ANativeWindow*, float, int8_t);                 // (API 30)
    static void* lib = dlopen("libandroid.so", RTLD_NOW);
    static auto with_strategy = lib ? reinterpret_cast<WithStrategy>(dlsym(lib, "ANativeWindow_setFrameRateWithChangeStrategy")) : nullptr;
    static auto plain = lib ? reinterpret_cast<Plain>(dlsym(lib, "ANativeWindow_setFrameRate")) : nullptr;
    // (FIXED_SOURCE = 1: the content is made at this rate, the display runs at it or a multiple; ALWAYS = 1)
    int32_t r = -1;
    if (with_strategy != nullptr) r = with_strategy(surface, fps, 1, 1);
    else if (plain != nullptr) r = plain(surface, fps, 1);
    __android_log_print(ANDROID_LOG_INFO, "MH4U", "surface frame rate %.0f: %d", fps, r);
    std::fprintf(stderr, "[perf] the surface asks for %.0f frames a second (%s)\n", fps, r == 0 ? "taken" : "refused");
}
