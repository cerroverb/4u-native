// MH4U for PC: the display's vsync, for the game's emulated VBlank to follow (pc_vsync.h; mh4n pacing g_display_vsync_ns,
// g_display_period_ns and g_vblank_lock_ns, gsp FollowDisplay), as the Android app's display_vsync.cpp does with
// Choreographer. The display's own scanline (D3DKMTGetScanLine, gdi32: the adapter's state) on a thread of its own: the
// moment it enters the vertical blank, looked for about four times a second where the last ones say it comes (a
// high-resolution timer to just before it, then the scanline read until it does), its time on pacing::NowNs's clock
// (QueryPerformanceCounter, as clock_gettime's CLOCK_MONOTONIC is on Windows); the period, the mean over every vblank
// since the window's monitor or its rate changed (those between two looks counted by the monitor's own rate).
// IDXGIOutput::WaitForVBlank was tried first: in the game's own process, with its window on that monitor, it came back
// with the window's presents (48 or 60 Hz on a 75 Hz monitor), not with the display's vblank.
#include "pc_vsync.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <thread>
#include <vector>

#include "mh4n/pacing_trace.h"
#include "pc_perf.h"

namespace {

std::atomic<HWND> g_hwnd{nullptr};
std::atomic<bool> g_lock{false};

// D3DKMT, declared from Microsoft's documentation of d3dkmthk.h (as pc_stats.c does: MinGW has no such header)
struct KmtOpenGdi {  // D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME
    WCHAR DeviceName[32];
    UINT hAdapter;
    LUID AdapterLuid;
    UINT VidPnSourceId;
};
struct KmtScanLine {  // D3DKMT_GETSCANLINE
    UINT hAdapter;
    UINT VidPnSourceId;
    BOOLEAN InVerticalBlank;
    UINT ScanLine;
};
struct KmtClose {  // D3DKMT_CLOSEADAPTER
    UINT hAdapter;
};
using OpenFn = LONG(APIENTRY*)(KmtOpenGdi*);
using ScanFn = LONG(APIENTRY*)(KmtScanLine*);
using CloseFn = LONG(APIENTRY*)(KmtClose*);

struct Display {
    HMONITOR monitor = nullptr;
    UINT adapter = 0, source = 0;
    double nominal_ns = 0;  // the monitor's rate as Windows has it set (its display configuration's own fraction)
};

// The monitor's adapter and source for its scanline, and its refresh rate; false when they can't be had
bool OpenDisplay(HMONITOR monitor, OpenFn open, Display& d) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof info;
    if (!GetMonitorInfoW(monitor, &info)) return false;
    KmtOpenGdi o{};
    std::memcpy(o.DeviceName, info.szDevice, sizeof o.DeviceName);
    if (open(&o) != 0) return false;
    d.monitor = monitor, d.adapter = o.hAdapter, d.source = o.VidPnSourceId, d.nominal_ns = 0;
    UINT32 np = 0, nm = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) == ERROR_SUCCESS) {
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(np);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(nm);
        if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths.data(), &nm, modes.data(), nullptr) == ERROR_SUCCESS) {
            for (UINT32 i = 0; i < np; ++i) {
                const DISPLAYCONFIG_PATH_INFO& p = paths[i];
                const DISPLAYCONFIG_RATIONAL r = p.targetInfo.refreshRate;
                if (p.sourceInfo.adapterId.LowPart == o.AdapterLuid.LowPart &&
                    p.sourceInfo.adapterId.HighPart == o.AdapterLuid.HighPart && p.sourceInfo.id == o.VidPnSourceId &&
                    r.Numerator > 0 && r.Denominator > 0)
                    d.nominal_ns = 1e9 * r.Denominator / r.Numerator;
            }
        }
    }
    return true;
}

// Reads the scanline every 50 us until it enters the vertical blank, at most until `until` (pacing::NowNs): its time,
// 0 if not seen. (Each read is a call into the graphics kernel: read back to back they held up the window's presents.)
uint64_t NextVblank(ScanFn scan, const Display& d, uint64_t until) {
    KmtScanLine s{d.adapter, d.source, 0, 0};
    if (scan(&s) != 0) return 0;
    bool was = s.InVerticalBlank != 0;
    for (;;) {
        const uint64_t t = mh4n::pacing::NowNs();
        if (scan(&s) != 0 || t > until) return 0;
        if (s.InVerticalBlank && !was) return t;
        was = s.InVerticalBlank != 0;
        while (mh4n::pacing::NowNs() < t + 50000) YieldProcessor();
    }
}

void Sampler() {
    pc_perf_name_this_thread("mh4u-vsync");  // (above normal: its wake-up is the vsync's time; pc_perf.cpp)
    const HMODULE gdi = LoadLibraryW(L"gdi32.dll");
    const auto open = reinterpret_cast<OpenFn>(
        reinterpret_cast<void*>(GetProcAddress(gdi, "D3DKMTOpenAdapterFromGdiDisplayName")));
    const auto scan = reinterpret_cast<ScanFn>(reinterpret_cast<void*>(GetProcAddress(gdi, "D3DKMTGetScanLine")));
    const auto close = reinterpret_cast<CloseFn>(reinterpret_cast<void*>(GetProcAddress(gdi, "D3DKMTCloseAdapter")));
    if (open == nullptr || scan == nullptr) {
        std::fprintf(stderr, "[perf] gdi32 has no D3DKMTGetScanLine: the game's VBlank keeps its own clock\n");
        return;
    }
    constexpr DWORD kHighResolution = 0x00000002;  // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, kHighResolution, TIMER_ALL_ACCESS);
    if (timer == nullptr) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    Display d;
    bool opened = false, told = false;
    uint64_t first = 0, last = 0, intervals = 0;
    double period = 0;
    for (;;) {
        if (!g_lock.load()) {  // (VSync off: nothing follows the display, nothing measured)
            first = last = intervals = 0, period = 0;
            Sleep(250);
            continue;
        }
        // the window's monitor (looked at every pass: the window may have moved)
        const HWND hwnd = g_hwnd.load();
        const HMONITOR monitor = hwnd != nullptr ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) : nullptr;
        if (monitor == nullptr || !opened || monitor != d.monitor) {
            if (opened && close != nullptr) {
                KmtClose c{d.adapter};
                close(&c);
            }
            opened = monitor != nullptr && OpenDisplay(monitor, open, d);
            first = last = intervals = 0, period = 0, told = false;
            if (!opened) {
                Sleep(1000);
                continue;
            }
        }
        const double ref = period > 0 ? period : d.nominal_ns;
        uint64_t t = 0;
        const uint64_t now = mh4n::pacing::NowNs();
        if (last != 0 && ref > 0) {
            // where the next vblank 2 ms from now or later should come: to 0.7 ms before it on the timer, then the scanline
            const double k = std::ceil((static_cast<double>(now - last) + 2e6) / ref);
            const uint64_t expect = last + static_cast<uint64_t>(std::llround(k * ref));
            if (timer != nullptr && expect > now + 700000) {
                LARGE_INTEGER due;
                due.QuadPart = -static_cast<LONGLONG>((expect - now - 700000) / 100);
                SetWaitableTimerEx(timer, &due, 0, nullptr, nullptr, nullptr, 0);
                WaitForSingleObject(timer, 1000);
            }
            t = NextVblank(scan, d, expect + 1500000);
        }
        if (t == 0) t = NextVblank(scan, d, mh4n::pacing::NowNs() + 50000000);  // (no prediction yet, or it missed)
        if (t == 0) {
            opened = false;  // (the adapter answers no more: opened again)
            Sleep(200);
            continue;
        }
        if (last != 0) {
            const double dt = static_cast<double>(t - last);
            const double n = ref > 0 ? std::round(dt / ref) : 1.0;
            if (n < 1 || (ref > 0 && std::fabs(dt - n * ref) > 0.1 * ref)) {
                first = t, intervals = 0;  // (a rate of its own: measured again)
            } else {
                intervals += static_cast<uint64_t>(n);
                if (intervals >= 4 || ref <= 0) period = static_cast<double>(t - first) / static_cast<double>(intervals);
            }
        } else {
            first = t;
        }
        last = t;
        if (period > 0) {
            mh4n::pacing::g_display_period_ns.store(static_cast<uint64_t>(std::llround(period)), std::memory_order_relaxed);
            mh4n::pacing::g_display_vsync_ns.store(t, std::memory_order_relaxed);
            mh4n::pacing::g_vblank_lock_ns.store(g_lock.load() ? 0 : -1, std::memory_order_relaxed);
            if (!told && intervals >= 300) {
                told = true;
                std::fprintf(stderr, "[perf] the display's vsync: %.3f Hz (its setting %.3f Hz; the game's VBlank follows it "
                                     "with VSync on)\n",
                             1e9 / period, d.nominal_ns > 0 ? 1e9 / d.nominal_ns : 0.0);
            }
        }
        Sleep(240);  // (about four looks a second)
    }
}

}  // namespace

void pc_vsync_start(SDL_Window* window) {
    const HWND hwnd = static_cast<HWND>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (hwnd == nullptr) return;
    g_hwnd.store(hwnd);
    static std::atomic<bool> started{false};
    if (started.exchange(true)) return;
    if (const char* e = std::getenv("MH4U_VBLANK_LOCK"); e != nullptr && std::string_view(e) == "off") {
        std::fprintf(stderr, "[perf] MH4U_VBLANK_LOCK=off: the game's VBlank keeps its own clock\n");
        return;
    }
    std::thread(Sampler).detach();
}

void pc_vsync_lock(bool on) {
    g_lock.store(on);
    if (!on) mh4n::pacing::g_vblank_lock_ns.store(-1, std::memory_order_relaxed);
}
