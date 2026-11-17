// Where the threads that carry a frame run, and at what priority. Two switches, read once at the start (launch.txt's
// env= or the app's environment), both off until measured: off, every thread keeps the nice it inherits (SDL's thread
// is at -4, THREAD_PRIORITY_DISPLAY, and the threads it makes start there) and runs on any core the system gives it.
//   MH4N_PERF_NICE=1      the game's thread and the renderer's at nice -10 (the boost Android gives the top app's
//                         RenderThread), and the two that time them with them, the guest's preemption timer (mh4n-timer)
//                         and the vsync sampler (mh4u-vsync): today all four have the same nice, and the guest's
//                         preemption, which the timer raises, must not wait behind the game's thread. The presenting
//                         thread at -4 (as SDL leaves it). The runtime's workers (mh4n-decode, mh4n-texload, mh4n-fsio,
//                         ...) at 0 at least: one made by a frame thread would take its -10. The renderer's nice was
//                         never set: it took the creating thread's, 0 in one run on a Moto g06 and -4 in another
//   MH4N_PERF_AFFINITY=1  on a SoC with small cores (their cpu_capacity under half the largest one's: a Helio G85's six
//                         Cortex-A55 beside its two A75, a T820's four A55 beside its four A76), the game's and the
//                         renderer's threads on the other cores alone. A Moto g06 at Low-end 60 had both on the A55s in
//                         about half of the snapshots, preempted some 190 times a second, while the presenting thread
//                         sat on the A75s (threads.py, cpu_after_low)
//                     =2  and every other thread of the app on the small cores: the presenting one, the timer, the
//                         workers, the GPU driver's, Java's
// Nothing is done on a SoC without small cores (all of a Snapdragon 8 Elite's are large), with fewer than two of the
// others, or below what the system allows (the error is logged once).
// They are applied on a thread of their own (mh4u-place, nice 10) once the game's first picture is shown (the runtime's
// threads are made by then), then again every 5 s (a thread made since; a nice the system set back; the CPUs Android's
// cpuset gives a thread back when the app goes to the background and returns) and as soon as the app is back in the
// foreground. Each change after the first time is logged: it says whether something sets them back.
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include "thread_place.h"

#define LOG(...) fprintf(stderr, __VA_ARGS__)

#define NICE_FRAME (-10)   // the game's and the renderer's threads, and their timer and vsync sampler
#define NICE_PRESENT (-4)  // the presenting thread (SDL's THREAD_PRIORITY_DISPLAY)
#define PERIOD_S 5         // placed again this often
#define AGAIN_LOGS 40      // the changes after the first pass logged, at most

static int nice_on, affinity;  // MH4N_PERF_NICE, MH4N_PERF_AFFINITY
static bool active;            // either: the placing thread runs
static _Atomic pid_t role_tid[PLACE_ROLES];
static _Atomic bool shown;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv;
static bool go, poke;  // (under mu) the first picture was shown; place again now
static cpu_set_t fast, small;
static int ncpu;

static int env_int(const char *name) {
    const char *e = getenv(name);
    if (!e) return -1;
    return atoi(e);
}

// "0-5", "6-7", "0,2-3"
static void cpu_list(const cpu_set_t *set, char *out, size_t size) {
    size_t o = 0;
    out[0] = 0;
    for (int c = 0; c < ncpu && o < size; c++) {
        if (!CPU_ISSET(c, set)) continue;
        int e = c;
        while (e + 1 < ncpu && CPU_ISSET(e + 1, set)) e++;
        o += (size_t)snprintf(out + o, size - o, "%s%d", o ? "," : "", c);
        if (e > c && o < size) o += (size_t)snprintf(out + o, size - o, "-%d", e);
        c = e;
    }
}

static int read_int(const char *path) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    char buf[32];
    ssize_t r = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (r <= 0) return -1;
    buf[r] = 0;
    return atoi(buf);
}

// The small cores: by cpu_capacity (the scheduler's measure of each core, 1024 the largest) under half the largest; where
// no core gives one, by the core's part in /proc/cpuinfo (Cortex-A35, A53, A55, A510, A520). false: no small cores, or
// fewer than two others (the two frame threads would share one)
static bool find_cores(char *how, size_t size) {
    ncpu = (int)sysconf(_SC_NPROCESSORS_CONF);
    if (ncpu > CPU_SETSIZE) ncpu = CPU_SETSIZE;
    if (ncpu < 2) return snprintf(how, size, "%d core", ncpu), false;
    CPU_ZERO(&fast);
    CPU_ZERO(&small);
    int cap[CPU_SETSIZE], max = 0, min = 0x7fffffff;
    bool have = true;
    for (int c = 0; c < ncpu; c++) {
        char path[96];
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpu_capacity", c);
        cap[c] = read_int(path);
        if (cap[c] <= 0) have = false;
        if (cap[c] > max) max = cap[c];
        if (cap[c] > 0 && cap[c] < min) min = cap[c];
    }
    if (have) {
        for (int c = 0; c < ncpu; c++) CPU_SET(c, cap[c] * 2 < max ? &small : &fast);
        snprintf(how, size, "cpu_capacity %d / %d", max, min);
    } else {
        FILE *f = fopen("/proc/cpuinfo", "re");
        int cpu = -1, known = 0;
        char line[256];
        while (f && fgets(line, sizeof line, f)) {
            unsigned part;
            if (sscanf(line, "processor : %d", &cpu) == 1 || sscanf(line, "processor\t: %d", &cpu) == 1) continue;
            if (cpu >= 0 && cpu < ncpu && (sscanf(line, "CPU part : %x", &part) == 1 || sscanf(line, "CPU part\t: %x", &part) == 1)) {
                bool little = part == 0xd04 || part == 0xd03 || part == 0xd05 || part == 0xd46 || part == 0xd80;
                CPU_SET(cpu, little ? &small : &fast);
                known++;
            }
        }
        if (f) fclose(f);
        if (known < ncpu) return snprintf(how, size, "no cpu_capacity, %d of %d cores' parts known", known, ncpu), false;
        snprintf(how, size, "the cores' parts");
    }
    char a[64], b[64];
    cpu_list(&fast, a, sizeof a);
    cpu_list(&small, b, sizeof b);
    size_t o = strlen(how);
    snprintf(how + o, size - o, ": fast %s, small %s", a, CPU_COUNT(&small) ? b : "none");
    return CPU_COUNT(&small) > 0 && CPU_COUNT(&fast) >= 2;
}

static bool comm(pid_t tid, char *out, size_t size) {
    char path[64];
    snprintf(path, sizeof path, "/proc/self/task/%d/comm", (int)tid);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    ssize_t r = read(fd, out, size - 1);
    close(fd);
    if (r <= 0) return false;
    out[r] = 0;
    char *nl = strchr(out, '\n');
    if (nl) *nl = 0;
    return true;
}

// Every thread of the app once: what the switches give it, where it differs from what it has
static void place_all(bool first) {
    static int again_logs, nice_failed, cpus_failed;
    const pid_t game = atomic_load(&role_tid[PLACE_GAME]), render = atomic_load(&role_tid[PLACE_RENDER]),
                present = atomic_load(&role_tid[PLACE_PRESENT]);
    DIR *d = opendir("/proc/self/task");
    if (!d) return;
    int others = 0;  // (MH4N_PERF_AFFINITY=2: the app's other threads sent to the small cores this pass)
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const pid_t tid = (pid_t)atoi(e->d_name);
        char name[32];
        if (tid <= 0 || !comm(tid, name, sizeof name)) continue;
        const bool frame = tid == game || tid == render || (!render && !strcmp(name, "mh4n-render"));
        const bool timing = !frame && (!strcmp(name, "mh4n-timer") || !strcmp(name, "mh4u-vsync"));
        const bool presenting = !frame && tid == present;
        const bool worker = !frame && !timing && !strncmp(name, "mh4n-", 5);
        char said[160] = "";
        if (nice_on && (frame || timing || presenting || worker)) {
            errno = 0;
            const int now = getpriority(PRIO_PROCESS, (id_t)tid);
            const int want = frame || timing ? NICE_FRAME : presenting ? NICE_PRESENT : now < 0 ? 0 : now;
            if (errno == 0 && now != want) {
                if (setpriority(PRIO_PROCESS, (id_t)tid, want) == 0)
                    snprintf(said, sizeof said, "nice %d -> %d", now, want);
                else if (!nice_failed++)
                    LOG("[perf] MH4N_PERF_NICE: %s %d's nice %d not taken (%s)\n", name, (int)tid, want, strerror(errno));
            }
        }
        const cpu_set_t *want = !affinity ? NULL : frame ? &fast : affinity >= 2 ? &small : NULL;
        if (want) {
            cpu_set_t cur;
            CPU_ZERO(&cur);
            if (sched_getaffinity(tid, sizeof cur, &cur) == 0 && !CPU_EQUAL(&cur, want)) {
                char a[64], b[64];
                cpu_list(&cur, a, sizeof a);
                cpu_list(want, b, sizeof b);
                if (sched_setaffinity(tid, sizeof *want, want) == 0) {
                    if (!frame && !timing && !presenting && !worker) others++;
                    else {
                        size_t o = strlen(said);
                        snprintf(said + o, sizeof said - o, "%scpus %s -> %s", o ? ", " : "", a, b);
                    }
                } else if (!cpus_failed++) {
                    LOG("[perf] MH4N_PERF_AFFINITY: %s %d on cpus %s not taken (%s)\n", name, (int)tid, b, strerror(errno));
                }
            }
        }
        if (said[0] && (first || again_logs++ < AGAIN_LOGS))
            LOG("[perf] %s %s %d: %s\n", first ? "placed" : "placed again", name, (int)tid, said);
    }
    closedir(d);
    if (others && (first || again_logs++ < AGAIN_LOGS))
        LOG("[perf] %s: %d other threads on the small cores\n", first ? "placed" : "placed again", others);
}

static void *place_main(void *arg) {
    (void)arg;
    pthread_setname_np(pthread_self(), "mh4u-place");
    setpriority(PRIO_PROCESS, (id_t)gettid(), 10);  // (nothing waits for it)
    pthread_mutex_lock(&mu);
    while (!go) pthread_cond_wait(&cv, &mu);
    pthread_mutex_unlock(&mu);
    for (bool first = true;; first = false) {
        place_all(first);
        struct timespec until;
        clock_gettime(CLOCK_MONOTONIC, &until);
        until.tv_sec += PERIOD_S;
        pthread_mutex_lock(&mu);
        while (!poke)
            if (pthread_cond_timedwait(&cv, &mu, &until) == ETIMEDOUT) break;
        poke = false;
        pthread_mutex_unlock(&mu);
    }
    return NULL;
}

void thread_place_init(void) {
    const int n = env_int("MH4N_PERF_NICE"), a = env_int("MH4N_PERF_AFFINITY");
    nice_on = n > 0;
    affinity = a < 0 ? 0 : a > 2 ? 2 : a;
    if (n >= 0) LOG("[perf] MH4N_PERF_NICE=%d\n", nice_on);
    if (a >= 0) {
        char how[200];
        const bool ok = find_cores(how, sizeof how);
        LOG("[perf] MH4N_PERF_AFFINITY=%d (%s)%s\n", affinity, how, affinity && !ok ? ": nothing to place on this SoC" : "");
        if (!ok) affinity = 0;
    }
    active = nice_on || affinity;
    if (!active) return;
    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    pthread_cond_init(&cv, &ca);
    pthread_condattr_destroy(&ca);
    pthread_t t;
    if (pthread_create(&t, NULL, place_main, NULL) == 0) pthread_detach(t);
    else active = false;
}

void thread_place_role(int role) {
    if (role >= 0 && role < PLACE_ROLES) atomic_store(&role_tid[role], gettid());
}

void thread_place_shown(void) {
    if (!active || atomic_load_explicit(&shown, memory_order_relaxed)) return;
    atomic_store(&shown, true);
    pthread_mutex_lock(&mu);
    go = true;
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&mu);
}

void thread_place_resume(void) {
    if (!active) return;
    pthread_mutex_lock(&mu);
    poke = true;
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&mu);
}
