// Diagnostics for the log a player shares (diag.h). The owner, for 1.0.1: the log should ask for more than we think we
// need, so one log answers most questions and it stops growing release after release.
#include "diag.h"

#ifdef __ANDROID__
#include <jni.h>
#endif
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace {

std::atomic<uint64_t> g_start_ns{0};
std::atomic<bool> g_log_ready{false};

uint64_t now_ns() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<uint64_t>(t.tv_sec) * 1000000000ull + static_cast<uint64_t>(t.tv_nsec);
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

// "[settings] <title>:" lines of up to ~220 characters
struct Lines {
    std::string title, line;
    int items = 0;
    void add(const std::string& item) {
        if (items && line.size() + item.size() > 220) flush();
        if (!items) line = "[settings] " + title + ":";
        line += (items ? ", " : " ") + item;
        items++;
    }
    void flush() {
        if (items) std::fprintf(stderr, "%s\n", line.c_str());
        items = 0;
    }
};

// the frames' intervals: 0.25 ms buckets up to 128 ms, then one for the rest
constexpr int kBuckets = 512;
constexpr uint64_t kBucketNs = 250000;
pthread_mutex_t g_frames_lock = PTHREAD_MUTEX_INITIALIZER;
uint32_t g_fb[kBuckets + 1];
uint64_t g_fmax, g_fn, g_fover;

double Percentile(double p) {
    uint64_t want = static_cast<uint64_t>(p * static_cast<double>(g_fn)), seen = 0;
    if (want >= g_fn) want = g_fn - 1;
    for (int b = 0; b <= kBuckets; b++) {
        seen += g_fb[b];
        if (seen > want) return b == kBuckets ? g_fmax / 1e6 : (b + 0.5) * kBucketNs / 1e6;
    }
    return g_fmax / 1e6;
}

}  // namespace

extern "C" {

void diag_start(void) {
    uint64_t zero = 0;
    g_start_ns.compare_exchange_strong(zero, now_ns());
}

void diag_clock_into(char* buf, size_t size) {
    diag_start();
    const unsigned long long s = (now_ns() - g_start_ns.load()) / 1000000000ull;
    std::snprintf(buf, size, "%llu:%02llu:%02llu", s / 3600, s / 60 % 60, s % 60);
}

const char* diag_clock(void) {
    static thread_local char buf[24];
    diag_clock_into(buf, sizeof buf);
    return buf;
}

void diag_log_ready(void) { g_log_ready.store(true); }

void diag_frame_interval(uint64_t ns) {
    pthread_mutex_lock(&g_frames_lock);
    const uint64_t b = ns / kBucketNs;
    g_fb[b < kBuckets ? b : kBuckets]++;
    if (ns > g_fmax) g_fmax = ns;
    if (ns > 100000000ull) g_fover++;
    g_fn++;
    pthread_mutex_unlock(&g_frames_lock);
}

void diag_frame_summary(char* out, size_t size) {
    pthread_mutex_lock(&g_frames_lock);
    *out = 0;
    if (g_fn)
        std::snprintf(out, size, "frame intervals p50 %.1f, p90 %.1f, p99 %.1f, the longest %.1f ms; %llu over 100 ms", Percentile(0.5),
                      Percentile(0.9), Percentile(0.99), g_fmax / 1e6, static_cast<unsigned long long>(g_fover));
    std::memset(g_fb, 0, sizeof g_fb);
    g_fmax = g_fn = g_fover = 0;
    pthread_mutex_unlock(&g_frames_lock);
}

void diag_log_cap(void) {
    constexpr long kCap = 32l << 20, kKeep = 4l << 20;
    static int cuts;
    flockfile(stderr);
    const long at = std::ftell(stderr);
    if (at > kCap) {
        std::fflush(stderr);
        if (ftruncate(fileno(stderr), kKeep) == 0 && std::fseek(stderr, kKeep, SEEK_SET) == 0)
            std::fprintf(stderr, "\n[log] (cut %d) the lines after the first 4 MB up to here were left out: the log stays under "
                                 "32 MB\n", ++cuts);
    }
    funlockfile(stderr);
}

void diag_log_env(void) {
#ifdef __ANDROID__
    extern char** environ;
#endif
    std::string line = "[settings] the environment's MH4U_ and MH4N_ variables:";
    int n = 0;
    for (char** v = environ; v != nullptr && *v != nullptr; v++)
        if (!std::strncmp(*v, "MH4U_", 5) || !std::strncmp(*v, "MH4N_", 5)) {
            if (n && line.size() > 200) {
                std::fprintf(stderr, "%s\n", line.c_str());
                line = "[settings] the environment's MH4U_ and MH4N_ variables:", n = 0;
            }
            line += std::string(n++ ? ", " : " ") + diag_mask(*v);
        }
    std::fprintf(stderr, "%s%s\n", line.c_str(), n ? "" : " none");
}

static std::atomic<int> g_battery_tenths{-2147483647 - 1};
int diag_battery_tenths(void) { return g_battery_tenths.load(std::memory_order_relaxed); }
#ifdef __ANDROID__  // (the Java side's: the PC has none)
JNIEXPORT void JNICALL Java_com_mh4u_port_Diag_nativeBattery(JNIEnv*, jclass, jint tenths) {
    g_battery_tenths.store(tenths, std::memory_order_relaxed);
}

// (1.0.1) A line from the app's Java side (com.mh4u.port.Diag) into log.txt; false: not the game's log yet
JNIEXPORT jboolean JNICALL Java_com_mh4u_port_Diag_nativeLine(JNIEnv* env, jclass, jstring text) {
    if (!g_log_ready.load() || text == nullptr) return JNI_FALSE;
    const char* s = env->GetStringUTFChars(text, nullptr);
    if (s == nullptr) return JNI_FALSE;
    std::fprintf(stderr, "%s\n", s);
    env->ReleaseStringUTFChars(text, s);
    return JNI_TRUE;
}

// The same, as an event of the session's: "[app] 0:12:34: <text>" (Diag.event)
JNIEXPORT jboolean JNICALL Java_com_mh4u_port_Diag_nativeEvent(JNIEnv* env, jclass, jstring text) {
    if (!g_log_ready.load() || text == nullptr) return JNI_FALSE;
    const char* s = env->GetStringUTFChars(text, nullptr);
    if (s == nullptr) return JNI_FALSE;
    std::fprintf(stderr, "[app] %s: %s\n", diag_clock(), s);
    env->ReleaseStringUTFChars(text, s);
    return JNI_TRUE;
}
#endif

}  // extern "C"

std::string diag_mask(const std::string& in) {
    std::string s = in;
    for (size_t p = 0; p < s.size(); p++) {
        if (!std::isdigit(static_cast<unsigned char>(s[p])) || (p > 0 && (std::isdigit(static_cast<unsigned char>(s[p - 1])) || s[p - 1] == '.')))
            continue;
        size_t q = p, third = std::string::npos;
        int parts = 0;
        for (;;) {
            int digits = 0;
            while (q < s.size() && std::isdigit(static_cast<unsigned char>(s[q])) && digits < 4) q++, digits++;
            if (digits == 0 || digits > 3) break;
            if (++parts == 4) break;
            if (q >= s.size() || s[q] != '.') break;
            q++;
            if (parts == 2) third = q;
        }
        if (parts != 4 || third == std::string::npos || (q < s.size() && (std::isdigit(static_cast<unsigned char>(s[q])) || s[q] == '.')))
            continue;
        s.replace(third, q - third, "x.x");
        p = third + 2;
    }
    return s;
}

bool diag_secret_key(const std::string& key) {
    std::string k;
    for (char c : key) k += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (k.find("lobby") != std::string::npos || k.find("address") != std::string::npos) return false;  // (masked, not hidden)
    for (const char* w : {"password", "passphrase", "secret", "token", "nickname", "name", "key"})
        if (k.find(w) != std::string::npos) return true;
    return false;
}

std::map<std::string, std::string> diag_read_ini(const std::string& path) {
    std::map<std::string, std::string> out;
    std::ifstream in(path);
    std::string raw, section;
    while (std::getline(in, raw)) {
        const std::string t = trim(raw);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        if (t[0] == '[' && t.back() == ']') {
            section = trim(t.substr(1, t.size() - 2));
            continue;
        }
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        out[section.empty() ? trim(t.substr(0, eq)) : section + "." + trim(t.substr(0, eq))] = trim(t.substr(eq + 1));
    }
    return out;
}

static std::string shown(const std::string& key, const std::string& value) {
    if (diag_secret_key(key.substr(key.rfind('.') == std::string::npos ? 0 : key.rfind('.') + 1)))
        return value.empty() ? "(none)" : "(set)";
    return diag_mask(value);
}

void diag_log_ini(const char* title, const std::string& path) {
    const auto values = diag_read_ini(path);
    Lines l{title};
    for (const auto& [k, v] : values) l.add(k + "=" + shown(k, v));
    if (!l.items) std::fprintf(stderr, "[settings] %s: none (no file yet)\n", title);
    l.flush();
}

void diag_log_changes(const char* title, const std::map<std::string, std::string>& before,
                      const std::map<std::string, std::string>& after) {
    Lines l{std::string(diag_clock()) + " " + title + " changed"};
    for (const auto& [k, v] : after) {
        const auto was = before.find(k);
        if (was == before.end()) l.add(k + " (new) " + shown(k, v));
        else if (was->second != v) l.add(k + " " + shown(k, was->second) + " -> " + shown(k, v));
    }
    for (const auto& [k, v] : before)
        if (after.find(k) == after.end()) l.add(k + " (taken out)");
    l.flush();
}
