// The window's presents timed to the display: each buffer carries the time it is to be shown
// (EGL_ANDROID_presentation_time), on the display's vsync grid (display_vsync.cpp's Choreographer samples), so that a
// game held to an FPS limit below the refresh rate is shown for an even number of vsyncs a frame. Queued as soon as
// drawn, the buffers of a 30 FPS game on a 60 Hz screen landed on either side of a vsync as the frames' times varied,
// and were shown for 16 and 50 ms in turn instead of 33.
//
// Only for a limit that is a whole fraction of the refresh rate (30 or 20 on 60 Hz; 60, 40 or 30 on 120 Hz). The slot
// of a buffer with a new game frame: the last one's plus the limit's whole vsyncs (n of them: 2 at 30 on 60 Hz), unless
// that is too soon for this buffer, then the first slot that is not (the slots move a period later: that frame is shown
// a vsync late, once). Too soon: less than the margin from now. The margin is what SurfaceFlinger needs: the display's
// presentation deadline (Display.getPresentationDeadlineNanos: a buffer to be shown at a vsync must be queued that long
// before it; 28.6 ms on a Moto g06 at 60 Hz, 16.7 on the Anbernic, 9.3 on the Galaxy at 120) and 4 ms more for the
// window's own drawing (2 ms more left the Anbernic at 30 FPS with a buffer a vsync late every 15 s). It was two periods
// everywhere: a period more than the Anbernic needs. A buffer that still reaches the screen later than its slot (its
// present time, EGL_ANDROID_get_frame_timestamps) a second time within 30 s raises the margin by 2 ms for good (up to
// three periods). A buffer with no new frame (the touch screen's picture, a menu) goes into the latest slot taken, or
// the first free one. The time given is half a period before the slot's vsync: SurfaceFlinger shows a buffer at the
// first vsync not before its time, so the grid may be off by up to that much.
//
// The slots never move earlier by themselves (that would show a frame for a vsync less): the game's frames move later
// instead, the phase below, so that they come just in time for them. Before, the slots moved a period earlier after 2
// to 32 s with every frame a whole period early, and kept the worst frame's lateness meanwhile: a Moto g06 at 30 FPS
// showed its frames six or seven vsyncs after they read the controls where four or five sufficed.
//
// MH4U_PRESENT_TIME=0: no time given (each buffer shown at the first vsync after it is ready, as before).
// MH4U_PRESENT_MARGIN=<us>: a fixed margin instead. MH4U_PHASE=0: the game's VBlank stays where MH4U_VBLANK_LOCK puts it
// (0: on the display's vsync), and the slots move earlier as before.
#include "present_pacing.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <android/log.h>
#include <time.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "mh4n/pacing_trace.h"

namespace {

using PresentationTime = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLnsecsANDROID);
using NextFrameId = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLuint64KHR*);
using FrameTimestamps = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLuint64KHR, EGLint, const EGLint*, EGLnsecsANDROID*);
constexpr EGLint kTimestamps = 0x3430;          // EGL_TIMESTAMPS_ANDROID
constexpr EGLint kDisplayPresentTime = 0x343A;  // EGL_DISPLAY_PRESENT_TIME_ANDROID

int64_t NowNs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}

struct Pacer {
    int64_t last_slot = 0;        // the vsync of the latest buffer given a time
    int64_t last_frame_slot = 0;  // the vsync of the latest buffer with a new game frame
    int64_t run_since = 0;        // since when every new frame came a whole period early (0: the last one did not)
    int64_t moved_at = 0;         // when the slots last moved earlier
    int64_t window = 2000000000;  // how long they must have been early for that
    bool anew = false;            // the next new frame starts the cadence again (after a hitch)
};
Pacer g_pacer;
bool g_active = false;  // the last present was paced (a limit of n >= 2 vsyncs a frame)

int64_t (*g_deadline_query)() = nullptr;

// The buffers queued lately: their frame ids (EGL_ANDROID_get_frame_timestamps), when they were queued, their slots
struct Sent {
    EGLuint64KHR id = 0;
    int64_t queued = 0;
    int64_t slot = 0;  // 0: no time given
    int64_t period = 0;
};
constexpr int kSent = 16;
Sent g_sent[kSent];
int g_sent_next = 0;

int64_t g_margin = 0;         // the margin in force (0: not worked out yet)
int64_t g_margin_period = 0;  // the period it was worked out for
int64_t g_margin_set_at = 0;  // when it was worked out
int64_t g_last_late = 0;      // when a buffer last came later than its slot

// The phase: how much later than the display's vsync the game's VBlank (and so its frames) start, moved later while
// every frame of the last seconds would have made its slot with room to spare
struct Phase {
    int64_t base = -1;    // the VBlank's offset at the start (MH4U_VBLANK_LOCK)
    int64_t shift = 0;    // added to it, modulo the period
    int64_t slack[600] = {};  // the last frames' slack, as it would be at the present shift
    int count = 0;        // frames noted (up to 600 kept)
    int since = 0;        // frames since the last move or the last frame that missed its slot
};
Phase g_phase;

}  // namespace

void present_pacing_set_deadline_query(int64_t (*query)()) { g_deadline_query = query; }

bool present_pacing_frame_pending() {
    return g_active && g_pacer.last_frame_slot > NowNs();
}

void present_pacing_before_present(bool new_frame, float fps_cap) {
    static const bool on = [] {
        const char* v = std::getenv("MH4U_PRESENT_TIME");
        return !(v != nullptr && std::strcmp(v, "0") == 0);
    }();
    static const PresentationTime set_time =
        on ? reinterpret_cast<PresentationTime>(eglGetProcAddress("eglPresentationTimeANDROID")) : nullptr;
    EGLDisplay display = eglGetCurrentDisplay();
    EGLSurface surface = eglGetCurrentSurface(EGL_DRAW);
    if (display == EGL_NO_DISPLAY || surface == EGL_NO_SURFACE) {
        return;
    }
    const int64_t now = NowNs();
    const int64_t period = static_cast<int64_t>(mh4n::pacing::g_display_period_ns.load(std::memory_order_relaxed));
    const int64_t vsync = static_cast<int64_t>(mh4n::pacing::g_display_vsync_ns.load(std::memory_order_relaxed));

    // When the buffers sent lately reached the screen (EGL_ANDROID_get_frame_timestamps): one that came later than its
    // slot raises the margin; each is marked in the pacing trace
    static NextFrameId next_id = nullptr;
    static FrameTimestamps timestamps = nullptr;
    static EGLSurface timed_surface = EGL_NO_SURFACE;
    if (surface != timed_surface) {
        timed_surface = surface;
        const char* ext = eglQueryString(display, EGL_EXTENSIONS);
        if (ext != nullptr && std::strstr(ext, "EGL_ANDROID_get_frame_timestamps") != nullptr &&
            eglSurfaceAttrib(display, surface, kTimestamps, EGL_TRUE) == EGL_TRUE) {
            next_id = reinterpret_cast<NextFrameId>(eglGetProcAddress("eglGetNextFrameIdANDROID"));
            timestamps = reinterpret_cast<FrameTimestamps>(eglGetProcAddress("eglGetFrameTimestampsANDROID"));
        } else {
            next_id = nullptr;
            timestamps = nullptr;
        }
        for (Sent& s : g_sent) s = Sent{};
    }
    if (timestamps != nullptr) {
        for (Sent& s : g_sent) {
            if (s.id == 0 || now - s.queued < 3 * std::max<int64_t>(s.period, 8000000)) continue;
            const EGLint what[] = {kDisplayPresentTime};
            EGLnsecsANDROID shown = 0;
            if (timestamps(display, surface, s.id, 1, what, &shown) != EGL_TRUE) {
                s.id = 0;
                continue;
            }
            if (shown == -2 /* EGL_TIMESTAMP_PENDING_ANDROID */ && now - s.queued < 500000000) continue;
            if (shown > 0) {
                mh4n::pacing::MarkAt(mh4n::pacing::kAppShown, static_cast<std::uint32_t>((shown - s.queued) / 1000),
                                     static_cast<std::uint64_t>(shown));
                // (not in the first 10 s of a margin, a load's time; and a second time within 10 s: a GPU's hitch alone
                // makes one buffer late whatever the margin)
                const bool late = s.slot != 0 && shown > s.slot + s.period / 2;
                const bool again = late && g_last_late != 0 && now - g_last_late < 30000000000LL;
                if (late) g_last_late = now;
                if (again && now - g_margin_set_at > 10000000000LL && g_margin != 0 && g_margin < 3 * s.period) {
                    g_last_late = 0;
                    g_margin = std::min(g_margin + 2000000, 3 * s.period);
                    mh4n::pacing::Mark(mh4n::pacing::kAppMargin, static_cast<std::uint32_t>(g_margin / 1000));
                    __android_log_print(ANDROID_LOG_INFO, "MH4U", "present: a buffer %.1f ms late, the margin is now %.1f ms",
                                        static_cast<double>(shown - s.slot) / 1e6, static_cast<double>(g_margin) / 1e6);
                }
            }
            s.id = 0;
        }
    }
    Sent* sent = nullptr;
    if (next_id != nullptr) {
        EGLuint64KHR id = 0;
        if (next_id(display, surface, &id) == EGL_TRUE) {
            sent = &g_sent[g_sent_next];
            g_sent_next = (g_sent_next + 1) % kSent;
            *sent = Sent{id, now, 0, period};
        }
    }

    g_active = false;
    if (set_time == nullptr || period <= 0 || vsync <= 0) {
        return;  // (the display's vsync not sampled yet: the buffer is shown when ready)
    }
    // the margin: the display's presentation deadline and 2 ms (MH4U_PRESENT_MARGIN=<us>: that), worked out again when
    // the display's rate changes
    static const int64_t margin_env = [] {
        const char* v = std::getenv("MH4U_PRESENT_MARGIN");
        return v != nullptr ? static_cast<int64_t>(std::atoll(v)) * 1000 : int64_t{-1};
    }();
    if (g_margin == 0 || std::llabs(period - g_margin_period) > period / 20) {
        g_margin_period = period;
        g_margin_set_at = now;
        const int64_t deadline = g_deadline_query != nullptr ? g_deadline_query() : 0;
        g_margin = margin_env >= 0 ? margin_env
                   : deadline > 0  ? std::clamp<int64_t>(deadline + 4000000, period / 2, 3 * period)
                                   : 2 * period;
        mh4n::pacing::Mark(mh4n::pacing::kAppMargin, static_cast<std::uint32_t>(g_margin / 1000));
        __android_log_print(ANDROID_LOG_INFO, "MH4U", "present: period %.2f ms, presentation deadline %.1f ms, margin %.1f ms",
                            static_cast<double>(period) / 1e6, static_cast<double>(deadline) / 1e6,
                            static_cast<double>(g_margin) / 1e6);
    }
    const int64_t margin = g_margin;
    // the vsyncs a frame takes: as the hooks pace the frames on the VBlank's grid when it is held on the display's vsync
    // (the limit's whole vsyncs, or the steady option's step), else the limit's when it divides the refresh rate (else 1:
    // as soon as ready, one a vsync at most)
    const double refresh = 1e9 / static_cast<double>(period);
    int n = 1;
    const int game_n = mh4n::pacing::g_frame_vblanks.load(std::memory_order_relaxed);
    const uint64_t vblank = mh4n::pacing::g_vblank_period_ns.load(std::memory_order_relaxed);
    if (game_n > 0 && vblank > 0 && mh4n::pacing::g_vblank_lock_ns.load(std::memory_order_relaxed) >= 0) {
        // The game's frame in the display's periods: its VBlanks at the VBlank's own period. That is the display's when
        // gsp FollowDisplay holds the VBlank on it; when the system runs this app's display at another rate (a 30 FPS
        // vote given a 30 Hz frame-rate override on a 120 Hz panel, or a panel kept at 90 or 120 Hz) the VBlank keeps its
        // own 60 Hz clock, and a 30 cap's 2 VBlanks a frame are 1 period of 33.3 ms (4 of 8.3 ms). Taken as vsyncs they
        // spaced the buffers 66.7 ms apart: 15 FPS at most, 10 to 12 with a late frame (4u-native #21). A frame that is
        // no whole number of periods (60 FPS on 90 Hz) is not paced.
        // (MH4U_PACE_VBLANKS=1: the VBlanks taken as vsyncs, as before; an A/B aid)
        static const bool as_before = [] {
            const char* v = std::getenv("MH4U_PACE_VBLANKS");
            return v != nullptr && *v == '1';
        }();
        const double periods = game_n * static_cast<double>(vblank) / static_cast<double>(period);
        const long k = std::lround(periods);
        if (as_before) n = game_n;
        else if (k >= 1 && std::fabs(periods - static_cast<double>(k)) <= 0.03 * static_cast<double>(k)) n = static_cast<int>(k);
    } else if (fps_cap > 0 && fps_cap < refresh * 0.98) {
        const int k = std::max(1, static_cast<int>(std::lround(refresh / fps_cap)));
        if (std::fabs(refresh / k - fps_cap) <= 0.02 * fps_cap) n = k;
    }
    {  // (log.txt: how the buffers are timed, when that changes; at most a line every 5 s and 40 in all)
        static int told_n = -1, lines = 0;
        static int64_t told_period = 0, told_at = 0;
        if ((n != told_n || std::llabs(period - told_period) > period / 20) && now - told_at > 5000000000LL && lines < 40) {
            told_n = n, told_period = period, told_at = now, ++lines;
            std::fprintf(stderr, "[perf] the display's vsync every %.2f ms (presentation margin %.1f ms): %s\n",
                         static_cast<double>(period) / 1e6, static_cast<double>(margin) / 1e6,
                         n >= 2 ? (std::to_string(n) + " vsyncs a game frame").c_str() : "each picture shown when ready");
        }
    }
    if (n < 2) {
        // At the refresh rate (or a limit that does not divide it) there is no cadence to keep: each buffer is shown when
        // ready. Given times there, a GPU-bound Anbernic at 49 FPS of 60 had 6 % of its buffers dropped (two taken in one
        // vsync) and more frames shown twice.
        g_pacer = Pacer{};
        return;
    }
    g_active = true;
    static const bool phase_on = [] {
        const char* v = std::getenv("MH4U_PHASE");
        return !(v != nullptr && std::strcmp(v, "0") == 0);
    }();
    // the first vsync of the grid at or after now + margin
    const int64_t t = now + margin;
    const int64_t earliest = vsync + ((t - vsync + period - 1) / period) * period;
    Pacer& p = g_pacer;
    if (p.last_slot != 0 && p.last_slot < now - 4 * period * n) {
        p = Pacer{};  // (nothing presented for a while: a new start)
    }
    int64_t slot = std::max(earliest, p.last_slot);
    bool late = false;    // (this frame came too late for the cadence's slot)
    int64_t jump = 0;     // (how much later than that slot it is shown: the slots' move)
    if (new_frame) {
        // (after a hitch, a frame more than a period late, the next frame starts the cadence again, a vsync after the
        // hitch's at the least: the hitch already broke it, and kept the slots would stay late by the hitch's whole jump
        // until the phase took it back, 30 to 50 s after a load)
        const int64_t want = p.last_frame_slot + (p.anew ? period : n * period);
        late = p.last_frame_slot != 0 && want < earliest;
        if (late) jump = earliest - want;
        p.anew = late && jump > period;
        if (p.last_frame_slot == 0 || want < earliest) {
            if (p.moved_at != 0 && now - p.moved_at < 10000000000LL) {
                p.window = std::min<int64_t>(p.window * 2, 32000000000LL);  // (moved earlier too soon)
            }
            p.moved_at = 0;
            p.run_since = 0;
        } else {
            slot = std::max(slot, want);
            if (want - earliest >= period && !phase_on) {
                if (p.run_since == 0) p.run_since = now;
                if (now - p.run_since >= p.window && slot == want) {
                    slot -= period;  // (still not before `earliest`: a whole period early)
                    p.moved_at = now;
                    p.run_since = 0;
                }
            } else {
                p.run_since = 0;
            }
            slot = std::max(slot, p.last_slot);
        }
        p.last_frame_slot = slot;
        // The phase (MH4U_PHASE=0: off). The slots keep the cadence and only ever move later, by a period, for a frame
        // that came too late for its slot; the time a frame's slot leaves to spare (its slack) is latency the player
        // feels. When every one of the last 600 frames (300 at least: 10 to 20 s at 30 FPS) had more than 2.5 ms of
        // slack, and 150 frames went by since the last move or the last frame that missed its slot (60 when the slack is
        // over a period), the game's VBlank moves later by the smallest slack less 2 ms (at most a quarter of a
        // period, three eighths of one when the slack is over a period; half of it settled 8 ms later on the Anbernic at
        // 30 FPS, 39 ms from the present to the slot against 31): its frames read the
        // controls later and reach the same slots (gsp FollowDisplay takes the VBlank there over a few VBlanks). It never
        // moves earlier: a frame that misses its slot moves the slots a period later instead (shown a vsync late once).
        // The slack kept is the smallest of 10 to 20 s, so a frame that takes longer than any of those misses its slot:
        // with the smallest of the last 2 to 5 s, a Moto g06 and the Anbernic at 30 FPS had 8 frames a minute shown a
        // vsync late. Without the phase the slots moved a period earlier only after 2 to 32 s with every frame a whole
        // period early, a short interval each time, and a Moto g06 at 30 FPS kept its slots six or seven vsyncs after
        // its frames' start where four or five sufficed.
        if (phase_on && mh4n::pacing::g_vblank_lock_ns.load(std::memory_order_relaxed) >= 0) {
            Phase& ph = g_phase;
            if (ph.base < 0) ph.base = mh4n::pacing::g_vblank_lock_ns.load(std::memory_order_relaxed);
            // A frame more than a period too late (a load's or a texture's hitch, not the frames' usual spread) is not
            // kept: the cadence starts again after it (anew), so the frames after it have the slack they had before.
            if (!(late && jump > period)) {
                ph.slack[ph.count % 600] = slot - (now + margin);
                ++ph.count;
                ph.since = late ? 0 : ph.since + 1;
            }
            const int k = std::min(ph.count, 600);
            const int64_t low = ph.count >= 300 ? *std::min_element(ph.slack, ph.slack + k) : 0;
            if (low > 2500000 && ph.since >= (low > period ? 60 : 150)) {
                // (under half a period a move: FollowDisplay takes the VBlank to the nearer of the two ways round)
                const int64_t step = std::min(low - 2000000, low > period ? period * 3 / 8 : period / 4);
                ph.shift = (ph.shift + step) % period;
                mh4n::pacing::g_vblank_lock_ns.store((ph.base + ph.shift) % period, std::memory_order_relaxed);
                mh4n::pacing::Mark(mh4n::pacing::kAppPhase, static_cast<std::uint32_t>(ph.shift / 1000));
                for (int i = 0; i < k; ++i) ph.slack[i] -= step;
                ph.since = 0;
            }
        }
    }
    p.last_slot = slot;
    if (sent != nullptr) sent->slot = slot;
    set_time(display, surface, static_cast<EGLnsecsANDROID>(slot - period / 2));
    mh4n::pacing::Mark(mh4n::pacing::kAppPresentSlot, static_cast<std::uint32_t>(std::max<int64_t>(0, slot - now) / 1000));
}
