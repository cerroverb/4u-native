// A measurement aid for frame pacing (MH4N_PACING_TRACE=<file>): the moments that set when a picture reaches the
// display, from every thread that has a part in it (the game's main loop and its frame limiter, the emulated VBlank,
// the renderer's publication of a screen, the app's present and the display's vsync), each with the CLOCK_MONOTONIC
// time SurfaceFlinger's frame times are in, so the two can be laid side by side.
//
// Header only (any module can mark without a link dependency; one instance per shared object). Off: one load and a
// test per mark. On: 1 Mi entries of 16 bytes are allocated at the first mark (16 MB; the marks after they are full
// are dropped) and a thread of its own appends the finished ones to the file every 2 s, so the file is complete up
// to 2 s before the process was killed. The file is a sequence of Entry records (little endian).
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <thread>
#include "mh4n/host_thread_name.h"

namespace mh4n::pacing {

enum Kind : std::uint32_t {
    kFrameStart = 1,      // the game's main loop starts a frame (sMhMain::frame; value: the hooks' frame count)
    kLimiterIn = 2,       // the game's frame limiter (sMain::updateFrameTiming) is entered
    kLimiterOut = 3,      // ... and returns
    kVBlank = 4,          // the emulated VBlank (value: its count)
    kSwapApplied = 5,     // that VBlank latched a new top-screen buffer the game had handed over
    kPublish = 6,         // the renderer thread published the screens to the window (value: the game frame)
    kAppPicture = 7,      // the app took a new top-screen picture (value: the game frame)
    kAppPresentIn = 8,    // the app's SDL_RenderPresent starts
    kAppPresentOut = 9,   // ... and returns
    kVsync = 10,          // the display's vsync (Choreographer's frame time; value: the period in us)
    kVBlankAdjust = 11,   // the emulated VBlank moved toward the display's (value: the shift in us + 1e6)
    kSleep = 12,          // the limiter's wait done by the host (value: us)
    // the renderer thread at the end of each frame (its counters, us): its busy time so far in the frame, and the parts
    // of it that were pipelines built or waited for, texture work, waits for the GPU (PaceGpu), the draws
    kRenderFrame = 13,
    kRenderSync = 14,
    kRenderTex = 15,
    kRenderPace = 16,
    kRenderDraw = 17,
    kRenderWriteback = 18,  // render targets read back for the CPU (a wait for the GPU)
    kRenderRing = 19,       // waits for room in the vertex and uniform rings
    kRenderSkipped = 20,    // draws left out because no pipeline was built for them yet (a count)
    kRenderCpu = 21,        // the CPU path's vertex work
    kRenderMaterialize = 22,
    kRenderCompose = 23,
    kRenderVertex = 24,
    kRenderPrep = 25,
    kRenderTarget = 26,
    kRenderFallback = 27,     // draws drawn with the ubershader fallback (a count)
    kRenderAsyncBuilds = 28,  // pipelines the workers finished (a count)
    kRenderUploads = 29,      // textures uploaded (a count)
    kRenderGpu = 30,          // the GPU's time of the screens it finished since the last mark (us; timestamps)
    kQueueWait = 31,          // the game's thread waited for the renderer's queue (us): its frame limit or a sync call
    kAppPresentSlot = 32,     // the app's next buffer: how far ahead the vsync it is to be shown at is (us)
    kRenderDecode = 33,       // textures decoded (and hashed) by the cache (us)
    kRenderUpload = 34,       // textures' uploads recorded (us)
    kRenderLoad = 35,         // render targets' rows loaded from guest memory (us)
    kRenderCreate = 36,       // textures' images made (us)
    kRenderWbCpu = 37,        // write-backs because the CPU wrote or reads that memory (a count)
    kRenderWbTexture = 38,    // write-backs because a texture reads that memory (a count)
    kRenderWbGx = 39,         // write-backs because a transfer writes or reads that memory (a count)
    kRenderWbOverlap = 40,    // write-backs because another target is drawn over that memory (a count)
    kRenderWbOther = 41,      // write-backs for anything else (a target made again, a transfer's own...) (a count)
    // the input's way to the screen (the latency from a press to the picture that answers it)
    kAppInput = 42,       // the app took an input event (a key, a controller's button or stick, a finger; value: us since
                          // SDL had it)
    kInputSet = 43,       // the app handed the game's live input a new state (value: its buttons)
    kInputLatch = 44,     // the live input latched a changed state for the game (a frame's change; value: its buttons)
    kInputSample = 45,    // the HID service read a changed state from the live input (value: its buttons)
    kPadRead = 46,        // the game's sMhPad::update read the pad (value: the held word, as sPad encodes it)
    kGxCommand = 47,      // a GX command the game queued was run (value: its id: 1 a command list, 3 a display transfer)
    kPublishSwap = 48,    // the renderer published the screens (value: the top screen's buffer swaps applied so far)
    kSwapCount = 49,      // the GSP applied a top-screen buffer swap at a VBlank (value: the swaps so far)
    kAppShown = 50,       // a buffer of the app's reached the screen (marked at that time; value: us since it was queued)
    kAppMargin = 51,      // the app's present margin changed (value: us)
    kAppPhase = 52,       // the app moved the game's VBlank later against the display's vsync (value: the shift, us)
    kRenderAbPhase = 53,  // MH4N_AB's phase of the renderer's frame: 1 its optimizations on, 2 off
    kAppTopGpu = 54,      // the app's GL time drawing the top screen into the window (MH4U_GL_TIMER=1; value: ns)
    kTargetHandOver = 55, // a screen target's picture handed over (gpu3; value: 1 at a later transfer, 2 the VBlank, 3 a write, 4 its own transfer)
    kSharedFence = 56,    // a sync file made for a shared picture (value: submissions it covers past the picture's)
    kAppPending = 57,     // the app keeps a picture back until the GPU has finished it (value: 1 with a sync file, 0 without)
};

struct Entry {
    std::uint64_t ns;
    std::uint32_t kind;
    std::uint32_t value;
};

inline std::uint64_t NowNs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<std::uint64_t>(ts.tv_nsec);
}

class Trace {
public:
    static Trace& Get() {
        static Trace trace;
        return trace;
    }
    bool On() const { return on_; }
    void Mark(Kind kind, std::uint32_t value, std::uint64_t ns) {
        const std::size_t i = next_.fetch_add(1, std::memory_order_relaxed);
        if (i >= capacity_) {
            return;
        }
        entries_[i].kind = kind;
        entries_[i].value = value;
        __atomic_store_n(&entries_[i].ns, ns == 0 ? 1 : ns, __ATOMIC_RELEASE);
    }

private:
    Trace() {
        const char* path = std::getenv("MH4N_PACING_TRACE");
        if (path == nullptr || *path == 0) {
            return;
        }
        // MH4N_PACING_TRACE_MB: the buffer's size (default 16 MB: 1 Mi marks, about 6 minutes with the renderer's)
        const char* mb = std::getenv("MH4N_PACING_TRACE_MB");
        const long size_mb = mb != nullptr ? std::strtol(mb, nullptr, 10) : 16;
        capacity_ = static_cast<std::size_t>(std::clamp(size_mb, 1L, 256L)) * 1024 * 1024 / sizeof(Entry);
        file_ = std::fopen(path, "wb");
        entries_ = file_ != nullptr ? static_cast<Entry*>(std::calloc(capacity_, sizeof(Entry))) : nullptr;
        if (entries_ == nullptr) {
            return;
        }
        on_ = true;
        std::thread([this] {
            SetHostThreadName("mh4n-trace");
            Write();
        }).detach();
    }
    void Write() {
        std::size_t done = 0;
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            const std::size_t end = std::min(next_.load(std::memory_order_relaxed), capacity_);
            std::size_t ready = done;
            while (ready < end && __atomic_load_n(&entries_[ready].ns, __ATOMIC_ACQUIRE) != 0) {
                ++ready;
            }
            if (ready > done) {
                std::fwrite(entries_ + done, sizeof(Entry), ready - done, file_);
                std::fflush(file_);
                done = ready;
            }
            if (done >= capacity_) {
                std::fclose(file_);
                return;
            }
        }
    }
    bool on_ = false;
    std::size_t capacity_ = 0;
    std::FILE* file_ = nullptr;
    Entry* entries_ = nullptr;
    std::atomic<std::size_t> next_{0};
};

// The display's vsync as the app sees it (Choreographer), for the emulated VBlank to follow (gsp: FollowDisplay): the
// CLOCK_MONOTONIC time of a recent vsync and the period, 0 while not known. g_vblank_lock_ns >= 0: the VBlank is held
// that long after the display's vsync; < 0: it keeps its own clock (as on the PC).
inline std::atomic<std::uint64_t> g_display_vsync_ns{0};
inline std::atomic<std::uint64_t> g_display_period_ns{0};
inline std::atomic<std::int64_t> g_vblank_lock_ns{-1};
// The emulated VBlank as the GSP schedules it: the CLOCK_MONOTONIC time of the next one and the period (0: not known),
// for the frame pacing of the hooks (UpdateFrameTiming), which times frame starts by it instead of the game's VBlank
// counter (that one moves only when the game's interrupt thread has run, which a busy phone delays).
inline std::atomic<std::uint64_t> g_next_vblank_ns{0};
inline std::atomic<std::uint64_t> g_vblank_period_ns{0};
// The renderer's GPU time so far (ns): the sum, over every screen the GPU has finished, of the time from its first
// command to its last (two timestamps a screen, read without a wait). 0 while not known (no timestamps). The steady
// pacing of the hooks takes a frame's cost from it, as the game's own time does not show a GPU-bound phone's.
inline std::atomic<std::uint64_t> g_gpu_total_ns{0};
// The VBlanks a game frame takes now, as the hooks pace it on the VBlank's grid (2 at 30 FPS on 60 Hz; more with the
// steady option's slower steps); 0 when the frames are not on the grid. The app times its buffers by it.
inline std::atomic<int> g_frame_vblanks{0};
// The game's frame counter as the hooks count it (sMhMain::frame, kFrameStart's value), for measurement aids that start at
// a game frame (MH4N_PASS_TRACE=f<frame>,<n>); 0 before the first frame and on runners without the hooks.
inline std::atomic<std::uint32_t> g_game_frame{0};

inline void Mark(Kind kind, std::uint32_t value = 0) {
    Trace& t = Trace::Get();
    if (t.On()) {
        t.Mark(kind, value, NowNs());
    }
}

inline void MarkAt(Kind kind, std::uint32_t value, std::uint64_t ns) {
    Trace& t = Trace::Get();
    if (t.On()) {
        t.Mark(kind, value, ns);
    }
}

}  // namespace mh4n::pacing
