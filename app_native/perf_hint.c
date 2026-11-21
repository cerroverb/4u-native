// Android performance hints (ADPF: Android Dynamic Performance Framework; perf_hint.h), from the MHGU port. The threads
// the frame rate rests on share one hint session whose target is the frame period at the settings' FPS, and each
// presented frame's work is reported to it: the system then picks cores and clocks for that rate (it raises the
// threads' clock floor when frames run long and lets it down when they are short), rather than its load tracking
// winding the clocks down during every frame's waits and up again too late.
//   MH4U_ADPF=0             no session
//   MH4U_ADPF_LOG=1         every 2 s: the work reported per frame (average, max), each thread's CPU time per frame
//                           (average / max), the target, the thermal headroom and status (also without a session)
//   MH4U_ADPF_TARGET=<pct>  the target as a percentage of the frame period (default 70; lower: boosted sooner, warmer)
//   MH4U_ADPF_RENEW=<s>     (1.0.2, a test) the same target sent again every s seconds (renew, below)
//
// The target is 70 % of the period, not all of it: the system winds the clocks down until the busiest thread's work
// fills the target, and with the whole period that thread had no time left over. Frames then ended on either side of a
// vsync as their work varied, and were shown for one vsync and three in turn (the Moto g06 at 30 FPS: the big cores at
// 1.35-1.6 GHz, the render thread 26-33 ms a frame, 16 stutters a minute; at 70 %: none, and less CPU time, 88 against
// 101 CPU-seconds a minute). The same holds at any FPS limit the phone can keep.
//
// The threads: the game's update thread (runtime/hle/vi.c vi_pause_game, once a frame), the presenting one
// (runtime/nvn/vulkan/device.c backend_present) and the one translating command lists (exec.c backend_execute) when it
// is another. The session is made with the first two once both are known (those still running: a thread whose CPU
// clock no longer answers has ended). A translating thread joins it with APerformanceHint_setThreads (API 34) only once
// it has submitted in SUBMIT_FRAMES frames in a row - a thread that submits a few times while loading and ends doesn't
// - as does a game or render thread that turns up later; one that ended leaves it then. The functions are looked up in
// libandroid.so at run time (API 33; the app's minimum is 28: below 33 there is no session), and the session is made
// on a thread of its own (binder calls: the frame's path only reports, which the system batches).
//
// A frame's work: the busiest session thread's CPU time since the previous present (CLOCK_THREAD_CPUTIME_ID: what it
// computed, with the time it spent blocked left out wherever that was - the guest's waits on its events (vsync, the
// other thread's work), the frame limiter's sleep, the GPU waits, vk_lock). The render thread's time from frame start to
// present less its GPU waits (vk_wait_seq) would still count the guest's own waits as work: at the FPS cap that is the
// whole frame period whatever the load, and the system would boost all the time. The busiest thread rather than the sum:
// the update thread goes on with the next frame while the render thread translates this one, so the slower of the two
// sets the rate (MH4U_ADPF_LOG shows both: their sum above the frame period at the cap says they overlap).
// Power efficiency (APerformanceHint_setPreferPowerEfficiency, API 35): on, the system may keep the threads on its
// efficient cores, which a tight frame cannot afford, but which is most of a phone's heat when frames leave time over.
// So it goes by what the frames need: on after EASY_RUNS seconds or so of frames using under half the target, off at once
// when they near it (MH4U_ADPF_EFFICIENT=0: never).
#define _GNU_SOURCE
#include <android/api-level.h>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <stdbool.h>
#include <stdint.h>

#include "diag.h"
#include "perf_hint.h"

// (MHGU's runtime gave these: here the app sets the FPS and the clock is the monotonic one)
static _Atomic float g_perf_fps = 30.0f;
void perf_hint_set_fps(float fps) { atomic_store(&g_perf_fps, fps > 0 ? fps : 30.0f); }
static float config_live_fps(void) { return atomic_load(&g_perf_fps); }
static uint64_t plat_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

#define LOG(...) fprintf(stderr, __VA_ARGS__)

// <android/performance_hint.h> (API 33-35) and <android/thermal.h> (API 30-31), called through pointers: linked
// directly, they would keep the app from loading below API 33
typedef struct APerformanceHintManager APerformanceHintManager;
typedef struct APerformanceHintSession APerformanceHintSession;
typedef struct AThermalManager AThermalManager;
static struct {
    APerformanceHintManager *(*get_manager)(void);
    APerformanceHintSession *(*create_session)(APerformanceHintManager *, const int32_t *, size_t, int64_t);
    int64_t (*preferred_rate)(APerformanceHintManager *);
    int (*update_target)(APerformanceHintSession *, int64_t);
    int (*report)(APerformanceHintSession *, int64_t);
    int (*set_threads)(APerformanceHintSession *, const pid_t *, size_t);  // (API 34)
    int (*prefer_efficiency)(APerformanceHintSession *, bool);             // (API 35)
    AThermalManager *(*thermal_manager)(void);                             // (API 30)
    int (*thermal_status)(AThermalManager *);                              // (API 30)
    float (*thermal_headroom)(AThermalManager *, int);                     // (API 31)
} fn;
static pthread_once_t load_once = PTHREAD_ONCE_INIT;

static void load(void) {
    void *lib = dlopen("libandroid.so", RTLD_NOW);  // (already loaded: the runtime links it)
    if (!lib) return;
#define SYM(f, name) fn.f = (__typeof__(fn.f))dlsym(lib, name)
    SYM(get_manager, "APerformanceHint_getManager");
    SYM(create_session, "APerformanceHint_createSession");
    SYM(preferred_rate, "APerformanceHint_getPreferredUpdateRateNanos");
    SYM(update_target, "APerformanceHint_updateTargetWorkDuration");
    SYM(report, "APerformanceHint_reportActualWorkDuration");
    SYM(set_threads, "APerformanceHint_setThreads");
    SYM(prefer_efficiency, "APerformanceHint_setPreferPowerEfficiency");
    SYM(thermal_manager, "AThermal_acquireManager");
    SYM(thermal_status, "AThermal_getCurrentThermalStatus");
    SYM(thermal_headroom, "AThermal_getThermalHeadroom");
#undef SYM
}

// ---- the threads (each names itself: perf_hint_thread)
#define MAX_THREADS 8     // (room for translating threads that come and go: entries are never reused)
#define SUBMIT_FRAMES 8   // a translating thread joins the session once it has submitted in this many frames in a row
#define CORE (1u << PERF_GAME | 1u << PERF_RENDER)  // (the roles the session is made with)
typedef struct {
    pid_t tid;
    clockid_t clock;              // its CPU time
    _Atomic unsigned roles;       // PERF_* bits (changed under lock)
    _Atomic uint32_t submitted;   // the frame (presents) it last submitted in
    // (the presenting thread's)
    bool gone;                    // its clock stopped answering: the thread ended
    bool in;                      // in the session (or the one being made)
    int streak;                   // not in it: the frames in a row it submitted in (up to SUBMIT_FRAMES)
    uint64_t last, sum, max;      // its CPU time at the last present; this log period's total and largest frame
} Thr;
static Thr thr[MAX_THREADS];
static _Atomic int nthr;  // entries complete below it
static _Atomic unsigned roles_known;
static _Atomic uint32_t presents;  // frames presented so far: the number of the one in progress
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static __thread unsigned t_roles;  // the roles this thread has given already
static __thread int t_slot = -1;   // its entry in thr (-1: none, the table was full)

void perf_hint_thread(int role) {
    unsigned bit = 1u << role;
    if (t_roles & bit) {  // (every frame after the first: a thread-local load; a translating thread notes the frame)
        if (role == PERF_SUBMIT && t_slot >= 0)
            atomic_store_explicit(&thr[t_slot].submitted, atomic_load_explicit(&presents, memory_order_relaxed),
                                  memory_order_relaxed);
        return;
    }
    t_roles |= bit;
    pid_t tid = gettid();
    uint32_t f = atomic_load_explicit(&presents, memory_order_relaxed);
    pthread_mutex_lock(&lock);
    int n = atomic_load_explicit(&nthr, memory_order_relaxed), i = 0;
    while (i < n && thr[i].tid != tid) i++;
    if (i < n) {
        atomic_fetch_or(&thr[i].roles, bit);
    } else if (n < MAX_THREADS && pthread_getcpuclockid(pthread_self(), &thr[n].clock) == 0) {
        thr[n].tid = tid;
        atomic_store(&thr[n].roles, bit);
        atomic_store_explicit(&thr[n].submitted, f - 1, memory_order_relaxed);  // (none yet)
        atomic_store_explicit(&nthr, n + 1, memory_order_release);
    } else {
        i = -1;
    }
    if (i >= 0 && role == PERF_SUBMIT) atomic_store_explicit(&thr[i].submitted, f, memory_order_relaxed);
    t_slot = i;
    atomic_fetch_or(&roles_known, bit);
    pthread_mutex_unlock(&lock);
}

static void role_name(unsigned roles, char *buf, size_t size) {
    static const char *const names[] = {"game", "render", "submit"};
    size_t o = 0;
    buf[0] = 0;
    for (int r = 0; r < 3; r++)
        if ((roles & (1u << r)) && o < size) o += (size_t)snprintf(buf + o, size - o, "%s%s", o ? "+" : "", names[r]);
}

// "game 1234, render+submit 1235" (the entries in mask)
static void thread_list(unsigned mask, char *buf, size_t size) {
    size_t o = 0;
    buf[0] = 0;
    for (int i = 0; i < MAX_THREADS && o < size; i++) {
        if (!(mask >> i & 1)) continue;
        char r[32];
        role_name(atomic_load(&thr[i].roles), r, sizeof r);
        o += (size_t)snprintf(buf + o, size - o, "%s%s %d", o ? ", " : "", r, (int)thr[i].tid);
    }
}

static bool alive(Thr *t) {  // (the presenting thread) its clock still answers: the thread has not ended
    struct timespec ts;
    if (!t->gone && clock_gettime(t->clock, &ts) != 0) t->gone = true;
    return !t->gone;
}

// ---- the session
static _Atomic(APerformanceHintSession *) session;  // (published by the maker once made)
static _Atomic int unavailable;                      // there will be none: MH4U_ADPF=0, below API 33, refused
// (the presenting thread's)
static bool making;      // the maker has been started
static int64_t target_ns;
static int pct = 70, log_on = -1;
static struct { int n; int32_t tids[MAX_THREADS]; char list[200]; } mk;  // the maker's threads (set before it starts)

static int64_t frame_target(void) {
    float fps = config_live_fps();
    if (!(fps >= 1.0f)) fps = 60.0f;  // (the settings not read yet)
    return (int64_t)(1e9 / fps * pct / 100.0);
}

static void *make_session(void *arg) {
    (void)arg;
    pthread_once(&load_once, load);
    int n = mk.n;
    APerformanceHintManager *m = NULL;
    if (!fn.get_manager || !fn.create_session || !fn.update_target || !fn.report) {
        LOG("[perf] ADPF unavailable: Android API %d (performance hint sessions are API 33)\n", android_get_device_api_level());
    } else if (!(m = fn.get_manager())) {
        LOG("[perf] ADPF unavailable: no performance hint manager\n");
    } else {
        APerformanceHintSession *s = n ? fn.create_session(m, mk.tids, (size_t)n, target_ns) : NULL;
        if (s) {
            if (fn.prefer_efficiency) fn.prefer_efficiency(s, false);
            LOG("[perf] ADPF session: %d thread%s (%s), target %.2f ms\n", n, n == 1 ? "" : "s", mk.list, target_ns / 1e6);
            atomic_store_explicit(&session, s, memory_order_release);
            return NULL;
        }
        // (-1: this device's power HAL has no hint sessions)
        LOG("[perf] ADPF unavailable: no session given for %s (Android API %d, preferred update rate %lld ns)\n", mk.list,
            android_get_device_api_level(), fn.preferred_rate ? (long long)fn.preferred_rate(m) : 0ll);
    }
    atomic_store_explicit(&unavailable, 1, memory_order_relaxed);
    return NULL;
}

// The session's threads set again (APerformanceHint_setThreads, API 34): those it has, a game or render thread new
// since, the translating threads that submitted in SUBMIT_FRAMES frames in a row; none that ended. Below API 34, or
// refused: it keeps the ones it has, and this isn't tried again.
static void regroup(APerformanceHintSession *s, int n) {
    static bool stuck;
    if (stuck) return;
    pid_t tids[MAX_THREADS];
    int k = 0;
    unsigned mask = 0;
    for (int i = 0; i < n; i++) {
        Thr *t = &thr[i];
        if ((t->in || (atomic_load(&t->roles) & CORE) || t->streak >= SUBMIT_FRAMES) && alive(t))
            tids[k++] = t->tid, mask |= 1u << i;
    }
    char list[200];
    thread_list(mask, list, sizeof list);
    int r = fn.set_threads && k ? fn.set_threads(s, tids, (size_t)k) : -1;
    if (!fn.set_threads)
        LOG("[perf] ADPF session keeps its threads: no APerformanceHint_setThreads below API 34 (for %s)\n", list);
    else if (r)
        LOG("[perf] ADPF session keeps its threads: %s not taken (%d)\n", list, r);
    else
        LOG("[perf] ADPF session: %d thread%s (%s)\n", k, k == 1 ? "" : "s", list);
    if (r) { stuck = true; return; }
    for (int i = 0; i < n; i++) thr[i].in = mask >> i & 1;
}

// MH4U_ADPF_LOG: the period's frames, reported work, each thread's CPU time per frame, thermal state
static void log_frame(uint64_t work, bool have_session) {
    static uint64_t t0, frames, work_sum, work_max;
    static AThermalManager *tm;
    static bool tm_tried;
    frames++, work_sum += work;
    if (work > work_max) work_max = work;
    uint64_t now = plat_now_ns();
    if (!t0) t0 = now;
    if (now - t0 < 2000000000ull) return;
    char line[600];
    int o = snprintf(line, sizeof line, "[perf] 2s: %llu frames, work %.2f ms avg %.2f max, target %.2f ms%s |",
                     (unsigned long long)frames, work_sum / 1e6 / frames, work_max / 1e6,
                     (have_session ? target_ns : frame_target()) / 1e6, have_session ? "" : " (no session)");
    int n = atomic_load_explicit(&nthr, memory_order_acquire);
    for (int i = 0; i < n; i++) {
        char r[32];
        role_name(atomic_load(&thr[i].roles), r, sizeof r);
        if (o < (int)sizeof line - 80)
            o += snprintf(line + o, sizeof line - o, " %s %d%s %.2f/%.2f", r, (int)thr[i].tid,
                          thr[i].gone ? " (gone)" : have_session && !thr[i].in ? " (not in session)" : "",
                          thr[i].sum / 1e6 / frames, thr[i].max / 1e6);
        thr[i].sum = thr[i].max = 0;
    }
    if (!tm_tried) {  // (thermal service: a binder call, only while this log is on)
        tm_tried = true;
        pthread_once(&load_once, load);
        if (fn.thermal_manager) tm = fn.thermal_manager();
    }
    if (tm && o < (int)sizeof line - 80) {
        float h = fn.thermal_headroom ? fn.thermal_headroom(tm, 0) : NAN;  // (1.0: throttled hard, severe status)
        o += snprintf(line + o, sizeof line - o, " | thermal headroom %.2f, status %d", h,
                      fn.thermal_status ? fn.thermal_status(tm) : -1);
    }
    LOG("%s\n", line);
    t0 = now, frames = work_sum = work_max = 0;
}

#define EASY_FRAMES 60  // (frames looked at together)
#define EASY_RUNS 5     // (of them in a row, all easy, before the efficient cores)
#define EASY_RUNS_MAX 80
// A phone whose frames are easy on the fast cores but not on the efficient ones went back and forth all the time
// (a Unisoc T606: about 30 times in one session, each a few slow frames): each time they did not last a minute, the
// next try waits twice as long
static void efficiency_follow(APerformanceHintSession *s, int64_t work) {
    static int frames, easy_runs, off = -1, need = EASY_RUNS;
    static int64_t sum, longest;
    static bool efficient;
    static uint64_t on_since;
    if (off < 0) {
        const char *e = getenv("MH4U_ADPF_EFFICIENT");
        off = !fn.prefer_efficiency || (e && !strcmp(e, "0"));
    }
    if (off) return;
    sum += work, longest = work > longest ? work : longest;
    if (++frames < EASY_FRAMES) return;
    int64_t avg = sum / frames, target = target_ns * 100 / pct;  // (the frame period: the session's target is a share of it)
    bool tight = avg > target * 7 / 10 || longest > target;
    easy_runs = avg < target / 2 && longest < target * 8 / 10 ? easy_runs + 1 : 0;
    frames = 0, sum = longest = 0;
    bool want = tight ? false : efficient || easy_runs >= need;
    if (want == efficient) return;
    efficient = want;
    fn.prefer_efficiency(s, want);
    uint64_t now = plat_now_ns();
    if (want) on_since = now, easy_runs = 0;
    else if (now - on_since < 60000000000ull) need = need * 2 > EASY_RUNS_MAX ? EASY_RUNS_MAX : need * 2;
    else need = EASY_RUNS;
    LOG("[perf] efficient cores %s (work %.1f ms of %.1f)%s\n", want ? "on" : "off", avg / 1e6, target / 1e6,
        want ? "" : need > EASY_RUNS ? ": the next try waits longer" : "");
}

// Once a minute (and at the first present): the process's memory map entries against vm.max_map_count, its resident
// memory and the system's available memory. Games that close after 5 to 15 minutes (PowerVR, a 3 GB phone) ran out of
// map entries (an mprotect said "out of memory" on a phone with 11 GB) or of memory: this shows whether either grows.
static long count_lines(const char *path, long *bytes) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    char buf[16384];
    long n = 0, total = 0;
    ssize_t r;
    while ((r = read(fd, buf, sizeof buf)) > 0) {
        total += r;
        for (ssize_t i = 0; i < r; i++) n += buf[i] == '\n';
    }
    close(fd);
    if (bytes) *bytes = total;
    return n;
}

static long kb_field(const char *path, const char *name) {
    FILE *f = fopen(path, "re");
    if (!f) return -1;
    char line[256];
    long v = -1;
    size_t len = strlen(name);
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, name, len)) { v = atol(line + len); break; }
    fclose(f);
    return v;
}

static void *mem_thread(void *arg);
static void mem_start(void) {
    pthread_t t;
    if (pthread_create(&t, NULL, mem_thread, NULL) == 0) pthread_detach(t);
}

// (the error screen's log: what the minute's line says, now; open / read only, nothing that allocates)
void perf_mem_report(void) {
    long maps = count_lines("/proc/self/maps", NULL), rss = -1;
    char buf[4096];
    int fd = open("/proc/self/status", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        ssize_t r = read(fd, buf, sizeof buf - 1);
        close(fd);
        if (r > 0) {
            buf[r] = 0;
            const char *p = strstr(buf, "VmRSS:");
            if (p) rss = atol(p + 6);
        }
    }
    fprintf(stderr, "[mem] at the error: %ld map entries, resident %ld MB\n", maps, rss / 1024);
}

// The kinds of the process's map entries (/proc/self/maps' last field: a file, a device, "[anon:...]", or nothing for plain
// anonymous memory), the `n` most numerous into `out` ("58000 /dev/pvrsrvkm, 2100 (anonymous), ..."): the next log of a
// phone whose entries keep growing names what grows.
#define MAP_KINDS 96
static void map_kinds(char *out, size_t size, int n) {
    struct { char name[48]; long count; } kinds[MAP_KINDS];
    int nk = 0;
    long other = 0;
    out[0] = 0;
    FILE *f = fopen("/proc/self/maps", "re");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        if (!strchr(line, '\n')) {  // (a longer line: the rest of it skipped)
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n') {}
        }
        // address perms offset dev inode [name]: the name begins after the fifth field
        char *p = line;
        for (int field = 0; field < 5 && *p; field++) {
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++;
        }
        char name[48];
        size_t len = strcspn(p, "\n");
        if (len == 0) snprintf(name, sizeof name, "(anonymous)");
        else snprintf(name, sizeof name, "%.*s", (int)(len < sizeof name - 1 ? len : sizeof name - 1), p);
        int i = 0;
        while (i < nk && strcmp(kinds[i].name, name) != 0) i++;
        if (i == nk) {
            if (nk == MAP_KINDS) { other++; continue; }
            snprintf(kinds[nk].name, sizeof kinds[nk].name, "%s", name);
            kinds[nk++].count = 0;
        }
        kinds[i].count++;
    }
    fclose(f);
    size_t used = 0;
    for (int k = 0; k < n; k++) {
        int best = -1;
        for (int i = 0; i < nk; i++)
            if (kinds[i].count > 0 && (best < 0 || kinds[i].count > kinds[best].count)) best = i;
        if (best < 0) break;
        used += (size_t)snprintf(out + used, used < size ? size - used : 0, "%s%ld %s", k ? ", " : "", kinds[best].count,
                                 kinds[best].name);
        kinds[best].count = 0;
        if (used >= size) break;
    }
    if (other && used < size) snprintf(out + used, size - used, ", %ld of other kinds", other);
}

// (shared_screens.cpp) the screens as pixels from now on: the renderer's shared buffers and their sync files no more
void shared_screens_stop_sharing(void);

static void mem_watch(void) {
    static long max_maps = -1, first_maps = -1, first_rss = -1, valve = -1;
    if (max_maps < 0) {
        FILE *f = fopen("/proc/sys/vm/max_map_count", "re");
        if (f) { if (fscanf(f, "%ld", &max_maps) != 1) max_maps = 0; fclose(f); }
        if (max_maps < 0) max_maps = 0;
    }
    long maps = count_lines("/proc/self/maps", NULL);
    long rss = kb_field("/proc/self/status", "VmRSS:"), avail = kb_field("/proc/meminfo", "MemAvailable:");
    if (first_maps < 0) first_maps = maps, first_rss = rss;
    const char *gpu = "";
    char of[32] = "";  // (apps may not read vm.max_map_count: 65530 is the kernel's default)
    if (max_maps > 0) snprintf(of, sizeof of, " of %ld", max_maps);
    LOG("[mem] %ld map entries%s (%+ld since the start), resident %ld MB (%+ld), the system's available %ld MB%s\n",
        maps, of, maps - first_maps, rss / 1024, (rss - first_rss) / 1024, avail / 1024, gpu);
    // Entries that keep growing (a PowerVR BXM-8-256 on driver 1.15: about two a game frame, 64,000 after ten minutes, then
    // std::bad_alloc: the process can't map more, 4u-native #31; a session elsewhere stays within a few thousand): what kind
    // grows, and past 85 % of the most the system allows (55,700 of 65,530, a minute or two before the end) the screens go
    // to the app as pixels from then on: the shared buffers handed over each frame and their sync files are what the MHGU
    // port, which plays on the same GPU without closing so, never does (it presents on its own); the next minutes' lines
    // say whether that stopped it. A last resort: the pixels cost frames (the Moto g06 at 3x, 26 FPS to 12-13).
    // MH4U_TEST_MAPS_LIMIT=<n> (a test run): that limit, and the kinds from n / 4 entries more than at the start.
    if (valve < 0) {
        const char *test = getenv("MH4U_TEST_MAPS_LIMIT");
        valve = test != NULL && atol(test) > 0 ? atol(test) : (max_maps > 0 ? max_maps : 65530) * 85 / 100;
    }
    if (maps - first_maps > (valve / 4 < 10000 ? valve / 4 : 10000)) {
        char kinds[512];
        map_kinds(kinds, sizeof kinds, 4);
        LOG("[mem] the map entries by kind: %s\n", kinds);
    }
    static bool stopped;
    if (!stopped && maps > valve && maps - first_maps > valve / 2) {
        stopped = true;
        LOG("[mem] %ld map entries and growing: the screens go to the app as pixels from now on\n", maps);
        shared_screens_stop_sharing();
    }
}

// The minute's [perf] line, after the [mem] one from the second on: the game's frames shown in that minute, the CPU time
// of the process in cores (1.00: one core all the time) and of its busiest threads, by name (the pipeline workers'
// three together), and the process's major page faults (pages read back from storage: memory short). A phone that runs
// slowly whatever the settings (4u-native #43: an Adreno 610 at 7-8 FPS at 1x as at 3x) shows here which thread had no
// time to spare - the game's (mh4u-game), the renderer's (mh4n-render), the pipeline workers' (mh4n-pipeline) - or that
// none had (a wait: the GPU, a lock), or the memory. At its end the phone's thermal status and headroom (API 30 and 31:
// a binder call each, once a minute on this thread) and the CPUs' clocks, slowest and fastest now: FPS that fall over a
// long session and come back after a pause in the menu (4u-native #50, Discord "MH4U Caida de fps", two Mali-G52 MC2
// Galaxys) is the phone slowing itself down when these drop with them, and the game when they don't.
extern volatile uint64_t g_present_count;  // (android_main.cpp: the game's frames shown)
int present_pacing_vsyncs(void);        // (present_pacing.cpp: the vsyncs a game frame at the last present, 1 when ready)
int present_pacing_game_vblanks(void);  // (the emulated VBlanks a game frame as the hooks pace it, 0 the game's clock)

// "; limit L FPS: ..." for the minute's line: the settings' limit and how its frames were paced at the minute's end (a
// player's log shows the limit they chose, 15 to 240, and whether its frames kept a cadence: the game on every n-th
// VBlank and shown for n vsyncs, or by the game's own clock and shown when ready, a limit that does not divide the screen)
static void perf_limit_state(char *out, size_t size) {
    const float cap = config_live_fps();
    const int vblanks = present_pacing_game_vblanks(), vsyncs = present_pacing_vsyncs();
    char game[48], shown[48];
    if (vblanks == 1) snprintf(game, sizeof game, "the game on every VBlank");
    else if (vblanks > 1) snprintf(game, sizeof game, "the game on every %d%s VBlank", vblanks,
                                   vblanks == 2 ? "nd" : vblanks == 3 ? "rd" : "th");
    else snprintf(game, sizeof game, "the game by its own clock");
    if (vsyncs >= 2) snprintf(shown, sizeof shown, "each frame shown for %d vsyncs", vsyncs);
    else snprintf(shown, sizeof shown, vsyncs == 1 ? "each picture shown when ready" : "nothing shown yet");
    snprintf(out, size, "; limit %.0f FPS: %s, %s", cap, game, shown);
}

// (1.0.1, diag) a sysfs number, -1 when the file can't be read (or isn't a number)
static long read_number(const char *path) {
    char buf[64];
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t r = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (r <= 0) return -1;
    buf[r] = 0;
    const char *comma = strchr(buf, ',');  // (MediaTek's GED: "idx, freq")
    const char *p = comma ? comma + 1 : buf;
    while (*p == ' ') p++;
    if ((*p < '0' || *p > '9') && *p != '-') return -1;
    return atol(p);
}
#define GPU_READ(p) read_number(p)

// (1.0.1, diag) The GPU's clock in MHz, -1 when no source here can be read: Adreno's kgsl (Hz), the kernel's gpu_clock
// (MHz: Samsung's, some Qualcomm's), MediaTek's GED (KHz, "idx, freq"), a Mali's misc device (Hz), and else the first
// devfreq device whose name says it is the GPU (mali, gpu, g3d, kgsl, pvr: Hz; a Unisoc's or a MediaTek's Mali is one).
// The source found is kept; none found is looked for again only every tenth time.
static long gpu_mhz(void) {
    static char path[160];
    static int scale;  // what turns the file's number into MHz: 1 MHz, 1000 KHz, 1000000 Hz
    static int misses;
    if (!path[0]) {
        if (misses++ % 10) return -1;
        static const struct { const char *p; int scale; } known[] = {
            {"/sys/class/kgsl/kgsl-3d0/gpuclk", 1000000}, {"/sys/kernel/gpu/gpu_clock", 1},
            {"/sys/kernel/ged/hal/current_freqency", 1000}, {"/sys/class/misc/mali0/device/cur_freq", 1000000}};
        for (size_t i = 0; i < sizeof known / sizeof known[0] && !path[0]; i++)
            if (GPU_READ(known[i].p) > 0) snprintf(path, sizeof path, "%s", known[i].p), scale = known[i].scale;
        DIR *d = path[0] ? NULL : opendir("/sys/class/devfreq");
        for (struct dirent *e; d && (e = readdir(d)) != NULL;) {
            const char *n = e->d_name;
            if (!strstr(n, "mali") && !strstr(n, "gpu") && !strstr(n, "g3d") && !strstr(n, "kgsl") && !strstr(n, "pvr")) continue;
            if (strstr(n, "bw") || strstr(n, "bus")) continue;  // (a GPU's memory bus, as Qualcomm's gpubw: not its clock)
            char p[160];
            snprintf(p, sizeof p, "/sys/class/devfreq/%s/cur_freq", n);
            if (GPU_READ(p) > 0) { snprintf(path, sizeof path, "%s", p); scale = 1000000; break; }
        }
        if (d) closedir(d);
        if (!path[0]) return -1;
    }
    long v = GPU_READ(path);
    return v > 0 ? v / scale : -1;
}

// "; thermal status S, headroom H; CPU A-B MHz" (each part only when the phone gives it) into `out`
static void perf_phone_state(char *out, size_t size) {
    static AThermalManager *tm;
    static bool tm_tried;
    out[0] = 0;
    if (!tm_tried) {
        tm_tried = true;
        pthread_once(&load_once, load);
        if (fn.thermal_manager) tm = fn.thermal_manager();
    }
    int o = 0;
    if (tm && fn.thermal_status) {
        o += snprintf(out + o, size - (size_t)o, "; thermal status %d", fn.thermal_status(tm));
        const float h = fn.thermal_headroom ? fn.thermal_headroom(tm, 0) : NAN;  // (1.0: throttled hard)
        if (!isnan(h) && o < (int)size) o += snprintf(out + o, size - (size_t)o, ", headroom %.2f", h);
    }
    long lo = 0, hi = 0;
    for (int cpu = 0; cpu < 16; cpu++) {
        char path[80], buf[32];
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        ssize_t r = read(fd, buf, sizeof buf - 1);
        close(fd);
        if (r <= 0) continue;
        buf[r] = 0;
        const long khz = atol(buf);
        if (khz <= 0) continue;
        if (lo == 0 || khz < lo) lo = khz;
        if (khz > hi) hi = khz;
    }
    if (hi > 0 && o < (int)size) o += snprintf(out + o, size - (size_t)o, "; CPU %ld-%ld MHz", lo / 1000, hi / 1000);
    // (1.0.1, diag) the battery's temperature (its sysfs file, else what the Java side last had: Diag.nativeBattery) and the
    // GPU's clock (gpu_mhz); "?" when it can't be read, never a made-up number
    if (o < (int)size) {
        long t = read_number("/sys/class/power_supply/battery/temp");
        if (t == -1 || t <= -400 || t >= 1500) t = diag_battery_tenths();  // (-1: not readable)
        if (t > -400 && t < 1500) o += snprintf(out + o, size - (size_t)o, "; battery %.1f C", t / 10.0);
        else o += snprintf(out + o, size - (size_t)o, "; battery ? C");
    }
    if (o < (int)size) {
        const long gpu = gpu_mhz();
        if (gpu > 0) o += snprintf(out + o, size - (size_t)o, "; GPU %ld MHz", gpu);
        else o += snprintf(out + o, size - (size_t)o, "; GPU ? MHz");
    }
}
#define PERF_TASKS 192
#define PERF_TOP 5
typedef struct { int32_t tid; unsigned long long ticks; } PerfTask;
static PerfTask perf_prev[PERF_TASKS];
static int perf_prev_n = -1;  // (-1: no minute yet, the first call only takes the counts)
static uint64_t perf_prev_frames, perf_prev_ns;
static unsigned long long perf_prev_ticks, perf_prev_majflt;

// utime + stime and majflt of a /proc stat file ("pid (comm) state ...": comm may hold spaces and parentheses, so the
// fields are counted from the last ')'); `name`: comm, at most 15 bytes. false: unreadable.
static bool perf_stat(const char *path, char name[16], unsigned long long *ticks, unsigned long long *majflt) {
    char buf[512];
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    ssize_t r = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (r <= 0) return false;
    buf[r] = 0;
    char *open_p = strchr(buf, '('), *close_p = strrchr(buf, ')');
    if (open_p == NULL || close_p == NULL || close_p < open_p) return false;
    size_t len = (size_t)(close_p - open_p - 1);
    if (len > 15) len = 15;
    memcpy(name, open_p + 1, len);
    name[len] = 0;
    unsigned long long maj = 0, ut = 0, st = 0;  // (fields 12, 14 and 15; the state is field 3)
    if (sscanf(close_p + 1, " %*c %*d %*d %*d %*d %*d %*u %*llu %*llu %llu %*llu %llu %llu", &maj, &ut, &st) != 3) return false;
    *ticks = ut + st;
    if (majflt) *majflt = maj;
    return true;
}

static void perf_watch(void) {
    const uint64_t now = plat_now_ns(), frames = g_present_count;
    char name[16];
    unsigned long long ticks = 0, majflt = 0;
    if (!perf_stat("/proc/self/stat", name, &ticks, &majflt)) return;
    static PerfTask now_tasks[PERF_TASKS];  // (this thread only)
    static struct { char name[16]; double ticks; int threads; } by_name[PERF_TASKS];
    int n = 0, names = 0;
    DIR *dir = opendir("/proc/self/task");
    if (dir == NULL) return;
    for (struct dirent *e; (e = readdir(dir)) != NULL && n < PERF_TASKS;) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        char path[64];
        unsigned long long t = 0;
        snprintf(path, sizeof path, "/proc/self/task/%s/stat", e->d_name);
        if (!perf_stat(path, name, &t, NULL)) continue;
        now_tasks[n].tid = (int32_t)atoi(e->d_name);
        now_tasks[n].ticks = t;
        unsigned long long before = 0;  // (a thread new since the last minute: all its time is the minute's)
        for (int i = 0; i < perf_prev_n; i++)
            if (perf_prev[i].tid == now_tasks[n].tid) { before = perf_prev[i].ticks; break; }
        ++n;
        if (perf_prev_n < 0 || t <= before) continue;
        int k = 0;
        while (k < names && strcmp(by_name[k].name, name) != 0) k++;
        if (k == names) {
            memcpy(by_name[k].name, name, sizeof name);
            by_name[k].ticks = 0;
            by_name[k].threads = 0;
            names++;
        }
        by_name[k].ticks += (double)(t - before);
        by_name[k].threads++;
    }
    closedir(dir);
    if (perf_prev_n >= 0 && now > perf_prev_ns) {
        const double secs = (double)(now - perf_prev_ns) / 1e9, hz = (double)sysconf(_SC_CLK_TCK);
        char line[512];
        char limit[128];
        perf_limit_state(limit, sizeof limit);
        int len = snprintf(line, sizeof line, "[perf] the last %.0f s: %llu game frames shown (%.1f FPS%s); the process %.2f cores",
                           secs, (unsigned long long)(frames - perf_prev_frames), (double)(frames - perf_prev_frames) / secs,
                           limit, (double)(ticks - perf_prev_ticks) / hz / secs);
        for (int shown = 0; shown < PERF_TOP && len < (int)sizeof line - 48; shown++) {
            int best = -1;
            for (int k = 0; k < names; k++)
                if (by_name[k].threads > 0 && (best < 0 || by_name[k].ticks > by_name[best].ticks)) best = k;
            if (best < 0 || by_name[best].ticks / hz / secs < 0.01) break;
            char count[16] = "";
            if (by_name[best].threads > 1) snprintf(count, sizeof count, " x%d", by_name[best].threads);
            len += snprintf(line + len, sizeof line - (size_t)len, "%s %s%s %.2f", shown == 0 ? ":" : ",", by_name[best].name,
                            count, by_name[best].ticks / hz / secs);
            by_name[best].threads = 0;  // (shown)
        }
        char phone[192], intervals[160];
        perf_phone_state(phone, sizeof phone);
        diag_frame_summary(intervals, sizeof intervals);
        LOG("%s; %llu major page faults%s; %s (at %s)\n", line, majflt - perf_prev_majflt, phone,
            *intervals ? intervals : "no frame intervals", diag_clock());
    }
    memcpy(perf_prev, now_tasks, sizeof(PerfTask) * (size_t)n);
    perf_prev_n = n;
    perf_prev_ns = now;
    perf_prev_frames = frames;
    perf_prev_ticks = ticks;
    perf_prev_majflt = majflt;
}

// The minute's [mem] line on a thread of its own: reading /proc/self/maps (thousands of lines the kernel writes out,
// under the process's memory map lock) took the presenting thread a while once a minute
// (1.0.1, diag) The build ids of the app's libraries, the player's code library and the GPU driver's, once: a crash's
// backtrace says "libmain.so+<offset>", and with the build id the offset is a function of the very build that ran (a
// player's own libmh4u_code_native.so too); a driver's id tells one build of it from another of the same version.
struct build_ids { char *line; int o, cap; };
static int build_id_cb(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size;
    struct build_ids *out = data;
    const char *name = info->dlpi_name;
    if (!name || !*name) return 0;
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    bool ours = !strcmp(base, "libmain.so") || !strcmp(base, "libSDL3.so") || strstr(base, "mh4u_code") != NULL;
    bool gpu = strstr(base, "vulkan") || strstr(base, "GLES") || strstr(base, "adreno") || strstr(base, "mali") ||
               strstr(base, "pvr") || strstr(base, "usc") || strstr(base, "glnext") || strstr(base, "mesa");
    if ((!ours && !gpu) || out->o > out->cap - 120) return 0;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_NOTE) continue;
        const char *p = (const char *)(uintptr_t)(info->dlpi_addr + ph->p_vaddr), *end = p + ph->p_memsz;
        while (p + sizeof(ElfW(Nhdr)) <= end) {
            const ElfW(Nhdr) *nh = (const ElfW(Nhdr) *)(const void *)p;
            const char *nname = p + sizeof *nh, *desc = nname + ((nh->n_namesz + 3) & ~3u);
            if (desc + nh->n_descsz > end) break;
            if (nh->n_type == NT_GNU_BUILD_ID && nh->n_namesz == 4 && !memcmp(nname, "GNU", 4)) {
                out->o += snprintf(out->line + out->o, out->cap - out->o, "%s %s ", out->o > 18 ? "," : "", base);
                for (unsigned k = 0; k < nh->n_descsz && k < 20 && out->o < out->cap - 3; k++)
                    out->o += snprintf(out->line + out->o, out->cap - out->o, "%02x", (unsigned char)desc[k]);
                return 0;
            }
            p = desc + ((nh->n_descsz + 3) & ~3u);
        }
    }
    return 0;
}

static void *mem_thread(void *arg) {
    (void)arg;
    bool ids_told = false;
    for (;;) {
        mem_watch();
        perf_watch();
        diag_log_cap();  // (log.txt under 32 MB: diag.h)
        if (!ids_told) {
            ids_told = true;
            char line[1500];
            struct build_ids out = {line, 0, (int)sizeof line};
            out.o = snprintf(line, sizeof line, "[app] build ids:");
            dl_iterate_phdr(build_id_cb, &out);
            LOG("%s\n", line);
        }
        sleep(60);
    }
    return NULL;
}

// (1.0.2, a test: MH4U_ADPF_RENEW=<seconds>, 5 or more; unset or 0 off) The same target sent to the session again every so many
// seconds. A realme (Helio G70, Android 11) had its big cores' allowed clock at 998 of 2000 MHz from the start, with no
// heat (battery 30-35 C, thermal status 0), and back at 2000 for good once the app lost the focus and got it again (the
// notification bar pulled down); the FPS fell with it (GU#262, Discord "Helio G70"). Whether asking again from the app
// does what the focus did is what this tries, on phones with a session (API 33 and later; that realme has none). The
// log's [perf] heat lines show the allowed clocks each minute.
static void renew(APerformanceHintSession *s) {
    static int every = -1;
    static uint64_t last;
    static unsigned sent, told;
    if (every < 0) {
        const char *e = getenv("MH4U_ADPF_RENEW");
        every = e ? atoi(e) : 0;
        every = every <= 0 ? 0 : every < 5 ? 5 : every;
        if (every) LOG("[perf] MH4U_ADPF_RENEW=%d: the ADPF target sent again every %d s (a test)\n", every, every);
    }
    if (!every) return;
    uint64_t now = plat_now_ns();
    if (!last) last = now;
    if (now - last < (uint64_t)every * 1000000000ull) return;
    last = now;
    if (!s) {
        if (!told++) LOG("[perf] MH4U_ADPF_RENEW: no ADPF session to send it to\n");
        return;
    }
    int r = fn.update_target(s, target_ns);
    if (r || ++sent <= 3 || sent % 20 == 0)
        LOG("[perf] ADPF target %.2f ms sent again (%u times)%s\n", target_ns / 1e6, sent, r ? ": not taken" : "");
}

void perf_hint_frame(void) {
    perf_hint_thread(PERF_RENDER);
    static pthread_once_t mem_once = PTHREAD_ONCE_INIT;
    pthread_once(&mem_once, mem_start);
    if (log_on < 0) {  // (the first present: the environment)
        const char *e = getenv("MH4U_ADPF_LOG");
        log_on = e && *e && strcmp(e, "0") != 0;
        e = getenv("MH4U_ADPF_TARGET");
        if (e && atoi(e) > 0) pct = atoi(e) < 25 ? 25 : atoi(e) > 400 ? 400 : atoi(e);
        e = getenv("MH4U_ADPF");
        if (e && !strcmp(e, "0")) {
            LOG("[perf] ADPF off (MH4U_ADPF=0)\n");
            atomic_store_explicit(&unavailable, 1, memory_order_relaxed);
        }
    }
    APerformanceHintSession *s = atomic_load_explicit(&session, memory_order_acquire);
    int n = atomic_load_explicit(&nthr, memory_order_acquire);
    // (the frame that ends here: the number the translating threads noted while it ran)
    uint32_t f = atomic_fetch_add_explicit(&presents, 1, memory_order_relaxed);
    renew(s);  // (a test, off unless given: above)
    if (!s && !making && !atomic_load_explicit(&unavailable, memory_order_relaxed) &&
        (atomic_load_explicit(&roles_known, memory_order_acquire) & CORE) == CORE) {
        // (the update thread's first frame came: the session is made with the game and render threads, those still
        // running; a translating thread joins it later, once it has shown it works in every frame)
        making = true;
        target_ns = frame_target();
        unsigned mask = 0;
        mk.n = 0;
        for (int i = 0; i < n; i++)
            if ((atomic_load(&thr[i].roles) & CORE) && alive(&thr[i]))
                thr[i].in = true, mask |= 1u << i, mk.tids[mk.n++] = thr[i].tid;
        thread_list(mask, mk.list, sizeof mk.list);
        pthread_t th;
        pthread_attr_t a;
        pthread_attr_init(&a);
        pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&th, &a, make_session, NULL) != 0)
            atomic_store_explicit(&unavailable, 1, memory_order_relaxed);
        pthread_attr_destroy(&a);
    }
    if (!s && !log_on) return;
    // the frame's work: the busiest session thread's CPU time since the last present (without a session yet: any's)
    uint64_t work = 0;
    bool again = false;  // the session's threads to be set again (regroup)
    for (int i = 0; i < n; i++) {
        Thr *t = &thr[i];
        struct timespec ts;
        if (t->gone) continue;
        if (clock_gettime(t->clock, &ts) != 0) { t->gone = true; again |= t->in; continue; }
        uint64_t c = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec, d = t->last ? c - t->last : 0;
        t->last = c;
        t->sum += d;
        if (d > t->max) t->max = d;
        if (d > work && (!s || t->in)) work = d;
        if (!s || t->in) continue;
        if (atomic_load(&t->roles) & CORE) again = true;  // (a game or render thread new since the session was made)
        else if (atomic_load_explicit(&t->submitted, memory_order_relaxed) != f) t->streak = 0;
        else if (t->streak < SUBMIT_FRAMES && ++t->streak == SUBMIT_FRAMES) again = true;  // (one of every frame's now)
    }
    if (s && work) efficiency_follow(s, (int64_t)work);
    if (s) {
        int64_t tgt = frame_target();  // (the settings' FPS changed: the target follows)
        if (tgt != target_ns) {
            int r = fn.update_target(s, tgt);
            if (r) LOG("[perf] ADPF target %.2f ms not taken (%d)\n", tgt / 1e6, r);
            else if (log_on) LOG("[perf] ADPF target %.2f ms\n", tgt / 1e6);
            target_ns = tgt;
        }
        if (again) regroup(s, n);
        if (work) fn.report(s, (int64_t)work);
    }
    if (log_on) log_frame(work, s != NULL);
}
