// MH4U for PC: the [perf] lines of log.txt (pc_perf.h). The CPU figures are QueryThreadCycleTime's (TSC ticks, read
// against rdtsc: every microsecond a thread ran, a spin as much as real work; GetThreadTimes charges whole timer ticks to
// whoever runs at the tick and misses threads that wake and wait between two), summed by thread name.
#include "pc_perf.h"

#include <windows.h>
#include <intrin.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "diag.h"
#include "mh4n/host_thread_name.h"
#include "mh4n/pacing_trace.h"
#include "pc_display.h"

extern "C" float perfstats_fps_cap(void);

namespace {

// ---- the threads' names --------------------------------------------------------------------------------------------

using SetDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
using GetDescriptionFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);
SetDescriptionFn set_description = nullptr;  // (Windows 10 1607 and later)
GetDescriptionFn get_description = nullptr;

void LoadDescriptions() {
    static std::once_flag once;
    std::call_once(once, [] {
        const HMODULE k = GetModuleHandleW(L"kernel32.dll");
        if (k == nullptr) return;
        set_description = reinterpret_cast<SetDescriptionFn>(reinterpret_cast<void*>(GetProcAddress(k, "SetThreadDescription")));
        get_description = reinterpret_cast<GetDescriptionFn>(reinterpret_cast<void*>(GetProcAddress(k, "GetThreadDescription")));
    });
}

// The threads' priorities by their names, as MHGU for PC has them: those a frame waits for above normal (the game's
// thread, the renderer, the window's, the preemption timer, the vsync's), the background compiles below normal (the
// pipeline workers and their saver: a frame never waits for them, the ubershader draws meanwhile); the rest as Windows
// starts them. The owner uses the PC while he plays: a background program's work then waits for the game's, not the
// other way round. MH4U_THREAD_PRIO=0: none changed.
int PriorityFor(const char* name) {
    static const bool on = [] {
        const char* e = std::getenv("MH4U_THREAD_PRIO");
        return e == nullptr || *e != '0';
    }();
    if (!on) return THREAD_PRIORITY_ERROR_RETURN;
    static const char* const above[] = {"mh4u-game", "mh4n-render", "mh4u-window", "mh4n-timer", "mh4u-vsync"};
    static const char* const below[] = {"mh4n-pipeline", "mh4n-pipesave", "mh4n-reserve", "mh4n-check"};
    for (const char* a : above)
        if (std::strcmp(name, a) == 0) return THREAD_PRIORITY_ABOVE_NORMAL;
    for (const char* b : below)
        if (std::strcmp(name, b) == 0) return THREAD_PRIORITY_BELOW_NORMAL;
    return THREAD_PRIORITY_ERROR_RETURN;
}

// The threads a frame goes through (the game's, the renderer, the window's) on the processors of one L3 cache when the
// CPU has several (a Ryzen's CCXs or CCDs): what the game's thread writes the renderer reads next, and across two L3s it
// came from the other one's. On a Ryzen 5 3600XT (two CCXs of 3 cores) the town at "No limit" went from 229 to 245 FPS,
// p99 frame time 11.9 -> 8.3 ms (the renderer's thread 4.0 -> 3.5 ms a frame) on the first CCX; on the second it was
// worse than on the first (191 against 216 FPS with the same build): the first of the largest L3s, when it has 6
// logical processors or more. One L3 (most CPUs): nothing changes. MH4U_FRAME_L3=0: off; =last: the last L3 (a test).
KAFFINITY FrameL3Mask() {
    static const KAFFINITY mask = [] {
        const char* e = std::getenv("MH4U_FRAME_L3");
        if (e != nullptr && *e == '0') return KAFFINITY{0};
        const bool first = !(e != nullptr && std::strcmp(e, "last") == 0);
        DWORD len = 0;
        GetLogicalProcessorInformationEx(RelationCache, nullptr, &len);
        std::vector<char> buf(len);
        KAFFINITY best = 0;
        int best_n = 0, groups = 0;
        if (len != 0 && GetLogicalProcessorInformationEx(
                            RelationCache, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data()), &len)) {
            for (DWORD off = 0; off < len;) {
                const auto* p = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data() + off);
                if (p->Cache.Level == 3 && p->Cache.GroupMask.Group == 0) {
                    ++groups;
                    const int n = __builtin_popcountll(static_cast<unsigned long long>(p->Cache.GroupMask.Mask));
                    if ((first ? n > best_n : n >= best_n) && n >= 2) best = p->Cache.GroupMask.Mask, best_n = n;
                }
                off += p->Size;
            }
        }
        // (an L3 of fewer than 6 logical processors, two cores' or less: the three threads would share cores)
        return groups >= 2 && best_n >= 6 ? best : KAFFINITY{0};
    }();
    return mask;
}

void KeepOnOneL3(const char* name) {
    if (std::strcmp(name, "mh4u-game") != 0 && std::strcmp(name, "mh4n-render") != 0 && std::strcmp(name, "mh4u-window") != 0)
        return;
    if (const KAFFINITY mask = FrameL3Mask(); mask != 0) SetThreadAffinityMask(GetCurrentThread(), mask);
}

void NameThread(const char* name) {
    LoadDescriptions();
    if (name == nullptr) return;
    KeepOnOneL3(name);
    if (const int prio = PriorityFor(name); prio != THREAD_PRIORITY_ERROR_RETURN) SetThreadPriority(GetCurrentThread(), prio);
    if (set_description == nullptr) return;
    wchar_t wide[64];
    const int n = MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, 64);
    if (n > 0) set_description(GetCurrentThread(), wide);
}

std::string DescriptionOf(HANDLE thread) {
    if (get_description == nullptr) return {};
    PWSTR text = nullptr;
    std::string out;
    if (SUCCEEDED(get_description(thread, &text)) && text != nullptr) {
        char utf8[128];
        if (WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof utf8, nullptr, nullptr) > 0) out = utf8;
        LocalFree(text);
    }
    return out;
}

// ---- the game's frame times and the window's presents --------------------------------------------------------------

// 0.1 ms buckets up to 250 ms, then one for the rest
constexpr int kBuckets = 2500;
constexpr double kBucketMs = 0.1;
struct Frames {
    std::mutex mutex;
    uint32_t hist[kBuckets + 1] = {};
    uint64_t n = 0, over25 = 0, over50 = 0, frames = 0;
    double worst = 0;
    uint32_t worst_frame = 0;
    uint32_t last_frame = 0;
    uint64_t last_qpc = 0;
    // the window
    uint64_t presents = 0, last_present_qpc = 0;
    double present_worst = 0;
    uint32_t present_hist[kBuckets + 1] = {};  // (the intervals between presents, as the frame times)
    uint64_t present_n = 0;
};
Frames g;
const double g_qpc_ms = [] {  // ms per QPC tick
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return 1000.0 / static_cast<double>(f.QuadPart);
}();

uint64_t Qpc() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<uint64_t>(c.QuadPart);
}

double Percentile(const uint32_t* hist, uint64_t n, double worst, double p) {
    uint64_t want = static_cast<uint64_t>(p * static_cast<double>(n)), seen = 0;
    if (want >= n) want = n - 1;
    for (int b = 0; b <= kBuckets; b++) {
        seen += hist[b];
        if (seen > want) return b == kBuckets ? worst : (b + 0.5) * kBucketMs;
    }
    return worst;
}

// ---- the thread that writes the lines ------------------------------------------------------------------------------

struct ThreadSample {
    std::string name;
    uint64_t cycles = 0;
};

uint64_t ProcessCycles() {
    ULONG64 c = 0;
    return QueryProcessCycleTime(GetCurrentProcess(), &c) ? c : 0;
}

// Every thread of this process now: id -> its name and cycles
std::map<DWORD, ThreadSample> TakeThreads() {
    std::map<DWORD, ThreadSample> out;
    const DWORD pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    THREADENTRY32 te{};
    te.dwSize = sizeof te;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE t = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
        if (t == nullptr) continue;
        ULONG64 cycles = 0;
        if (QueryThreadCycleTime(t, &cycles)) {
            ThreadSample& s = out[te.th32ThreadID];
            s.cycles = cycles;
            s.name = DescriptionOf(t);
            if (s.name.empty()) s.name = "unnamed";
        }
        CloseHandle(t);
    }
    CloseHandle(snap);
    return out;
}

void Reporter(int seconds) {
    pc_perf_name_this_thread("mh4u-perf");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    std::map<DWORD, ThreadSample> before = TakeThreads();
    uint64_t tsc0 = __rdtsc(), qpc0 = Qpc(), proc0 = ProcessCycles();
    uint64_t gpu0 = mh4n::pacing::g_gpu_total_ns.load(std::memory_order_relaxed);
    uint64_t frames0 = 0;
    {
        std::lock_guard lock(g.mutex);
        frames0 = g.frames;
    }
    for (;;) {
        Sleep(static_cast<DWORD>(seconds) * 1000);
        // the frames
        static uint32_t hist[kBuckets + 1], phist[kBuckets + 1];
        uint64_t n = 0, over25 = 0, over50 = 0, frames = 0, presents = 0, pn = 0;
        double worst = 0, present_worst = 0;
        uint32_t worst_frame = 0;
        {
            std::lock_guard lock(g.mutex);
            std::copy(std::begin(g.hist), std::end(g.hist), hist);
            std::fill(std::begin(g.hist), std::end(g.hist), 0u);
            std::copy(std::begin(g.present_hist), std::end(g.present_hist), phist);
            std::fill(std::begin(g.present_hist), std::end(g.present_hist), 0u);
            pn = g.present_n, g.present_n = 0;
            n = g.n, over25 = g.over25, over50 = g.over50, worst = g.worst, worst_frame = g.worst_frame;
            frames = g.frames, presents = g.presents, present_worst = g.present_worst;
            g.n = g.over25 = g.over50 = 0, g.worst = 0, g.presents = 0, g.present_worst = 0;
        }
        // the threads
        std::map<DWORD, ThreadSample> now = TakeThreads();
        const uint64_t tsc1 = __rdtsc(), qpc1 = Qpc(), proc1 = ProcessCycles();
        const uint64_t gpu1 = mh4n::pacing::g_gpu_total_ns.load(std::memory_order_relaxed);
        const double secs = static_cast<double>(qpc1 - qpc0) * g_qpc_ms / 1000.0;
        const double dtsc = static_cast<double>(tsc1 - tsc0);
        const uint64_t game_frames = frames - frames0;
        if (game_frames == 0 || secs <= 0 || dtsc <= 0) {  // (the game not started, or paused: nothing to say)
            before = std::move(now), tsc0 = tsc1, qpc0 = qpc1, proc0 = proc1, gpu0 = gpu1, frames0 = frames;
            continue;
        }
        struct ByName {
            double cores = 0;
            int threads = 0;
        };
        std::map<std::string, ByName> by_name;
        for (const auto& [id, s] : now) {
            const auto it = before.find(id);
            const uint64_t was = it != before.end() ? it->second.cycles : 0;  // (a new thread: all its time is this period's)
            if (s.cycles <= was) continue;
            ByName& b = by_name[s.name];
            b.cores += static_cast<double>(s.cycles - was) / dtsc;
            b.threads++;
        }
        std::vector<std::pair<std::string, ByName>> sorted(by_name.begin(), by_name.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second.cores > b.second.cores; });
        std::string cpu;
        char item[96];
        double rest = 0;
        int shown = 0;
        for (const auto& [name, b] : sorted) {
            if (shown >= 8 || b.cores < 0.01) {
                rest += b.cores;
                continue;
            }
            std::snprintf(item, sizeof item, "%s%s", shown ? ", " : "", name.c_str());
            cpu += item;
            if (b.threads > 1) cpu += " x" + std::to_string(b.threads);
            std::snprintf(item, sizeof item, " %.2f", b.cores);
            cpu += item;
            ++shown;
        }
        if (rest >= 0.01) {
            std::snprintf(item, sizeof item, ", the rest %.2f", rest);
            cpu += item;
        }
        const double fps = static_cast<double>(game_frames) / secs;
        double game_cores = 0;
        if (const auto it = by_name.find("mh4u-game"); it != by_name.end()) game_cores = it->second.cores;
        char limit[96];
        const float cap = perfstats_fps_cap();
        const uint64_t vblank = mh4n::pacing::g_vblank_period_ns.load(std::memory_order_relaxed);
        const int n_limit = cap > 0 && cap < 999 ? std::snprintf(limit, sizeof limit, "limit %.0f", cap)
                                                 : std::snprintf(limit, sizeof limit, "no limit");
        if (vblank > 0 && n_limit > 0)  // (the emulated VBlank: the game's frames start on it)
            std::snprintf(limit + n_limit, sizeof limit - n_limit, ", the VBlank %.1f Hz", 1e9 / static_cast<double>(vblank));
        char times[200] = "";
        if (n > 0)
            std::snprintf(times, sizeof times,
                          "frame times p50 %.1f, p90 %.1f, p99 %.1f, the longest %.1f ms (frame %u); %llu over 25 ms, %llu "
                          "over 50; ",
                          Percentile(hist, n, worst, 0.5), Percentile(hist, n, worst, 0.9), Percentile(hist, n, worst, 0.99),
                          worst, worst_frame, static_cast<unsigned long long>(over25), static_cast<unsigned long long>(over50));
        char gpu[96] = "";
        if (gpu1 > gpu0 && gpu0 != 0)
            std::snprintf(gpu, sizeof gpu, "; the GPU %.2f ms a frame (%.0f%% busy)",
                          static_cast<double>(gpu1 - gpu0) / 1e6 / static_cast<double>(game_frames),
                          static_cast<double>(gpu1 - gpu0) / 1e7 / secs);
        std::fprintf(stderr,
                     "[perf] the last %.0f s: %llu game frames (%.1f FPS, %s, VSync %s); %sthe window %.1f presents/s (their "
                     "gaps p50 %.1f, p99 %.1f, the longest %.1f ms); the process %.2f cores: %s; the game thread %.2f ms a frame%s (at %s)\n",
                     secs, static_cast<unsigned long long>(game_frames), fps, limit, pc_display_vsync() ? "on" : "off", times,
                     static_cast<double>(presents) / secs, pn ? Percentile(phist, pn, present_worst, 0.5) : 0.0,
                     pn ? Percentile(phist, pn, present_worst, 0.99) : 0.0, present_worst,
                     static_cast<double>(proc1 - proc0) / dtsc,
                     cpu.c_str(), game_cores * secs * 1000.0 / static_cast<double>(game_frames), gpu, diag_clock());
        before = std::move(now), tsc0 = tsc1, qpc0 = qpc1, proc0 = proc1, gpu0 = gpu1, frames0 = frames;
    }
}

}  // namespace

void pc_perf_name_this_thread(const char* name) { NameThread(name); }

void pc_perf_name_threads() {
    mh4n::g_host_thread_namer = NameThread;
    NameThread("mh4u-window");
}

void pc_perf_game_frame(uint32_t frame) {
    const uint64_t now = Qpc();
    std::lock_guard lock(g.mutex);
    if (frame == g.last_frame) return;
    if (g.last_qpc != 0 && frame > g.last_frame) {
        g.frames += frame - g.last_frame;
        if (frame == g.last_frame + 1) {
            const double ms = static_cast<double>(now - g.last_qpc) * g_qpc_ms;
            const int b = static_cast<int>(ms / kBucketMs);
            g.hist[b < kBuckets ? b : kBuckets]++;
            g.n++;
            if (ms > g.worst) g.worst = ms, g.worst_frame = frame;
            g.over25 += ms > 25.0, g.over50 += ms > 50.0;
        }
    }
    g.last_frame = frame, g.last_qpc = now;
}

void pc_perf_present() {
    const uint64_t now = Qpc();
    std::lock_guard lock(g.mutex);
    if (g.last_present_qpc != 0) {
        const double ms = static_cast<double>(now - g.last_present_qpc) * g_qpc_ms;
        g.present_worst = std::max(g.present_worst, ms);
        const int b = static_cast<int>(ms / kBucketMs);
        g.present_hist[b < kBuckets ? b : kBuckets]++;
        g.present_n++;
    }
    g.last_present_qpc = now;
    g.presents++;
}

void pc_perf_start() {
    static std::atomic<bool> started{false};
    if (started.exchange(true)) return;
    if (const KAFFINITY mask = FrameL3Mask(); mask != 0)
        std::fprintf(stderr, "[perf] the game's, the renderer's and the window's threads on the processors of one L3 cache "
                             "(mask %llx; MH4U_FRAME_L3=0: anywhere)\n",
                     static_cast<unsigned long long>(mask));
    int seconds = 60;
    if (const char* e = std::getenv("MH4U_PERF_SECONDS"); e != nullptr && std::atoi(e) > 0) seconds = std::clamp(std::atoi(e), 2, 3600);
    std::thread(Reporter, seconds).detach();
}
