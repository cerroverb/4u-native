// Diagnostics for the log a player shares (1.0.1), as the MHGU port's runtime/diag.h: the session's clock on the lines
// that tell where the game was and what it did, the settings in a compact block (and each change), each minute's frame
// intervals, and the lines the app's Java side gives the log (com.mh4u.port.Diag: the phone, the battery, the network's
// kind, how the last processes ended, the system's events). One log answers what used to take a second one.
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#include <map>
#include <string>
extern "C" {
#endif

// (android_main.cpp, first thing) the session's start: diag_clock counts from here
void diag_start(void);
// "h:mm:ss" since diag_start, in a buffer of the calling thread's (valid until its next call)
const char* diag_clock(void);
// The same into `buf` (no thread-local storage: for a crash's report, in a signal handler)
void diag_clock_into(char* buf, size_t size);
// (android_main.cpp) stderr is log.txt now: the Java side's lines may come in
void diag_log_ready(void);

// The game's frames shown, their intervals (the presenting loop: android_main.cpp) and once a minute (perf_hint.c's
// thread) "frame intervals p50 33.4, p90 33.6, p99 50.1, the longest 120.3 ms; 2 over 100 ms" ("" with none), which starts
// the next minute's count. Each its own thread; a lock between them, taken once a frame.
void diag_frame_interval(uint64_t ns);
void diag_frame_summary(char* out, size_t size);

// (perf_hint.c, once a minute) log.txt kept under 32 MB: past it, what came after its first 4 MB is cut, and it goes on
// from a line that says so
void diag_log_cap(void);

// The battery's temperature in tenths of a degree C as the Java side last had it (com.mh4u.port.Diag: each
// ACTION_BATTERY_CHANGED while the game runs), for the minute's line where the sysfs file can't be read; INT_MIN: not known
int diag_battery_tenths(void);

// (android_main.cpp, after launch.txt's env=) [settings] the MH4U_ and MH4N_ variables this start has (a test run's)
void diag_log_env(void);

#ifdef __cplusplus
}
// An IPv4's last two numbers as x ("192.168.x.x"), the rest as it is
std::string diag_mask(const std::string& s);
// A settings key whose value is a name, a password or a key: the log says only "(set)"
bool diag_secret_key(const std::string& key);
// [settings] <title>: every key = value of an INI file (sections as Section.Key), a few to a line, secrets hidden
void diag_log_ini(const char* title, const std::string& path);
// The INI file's values as "Section.Key" -> value (keys before any section: just "key")
std::map<std::string, std::string> diag_read_ini(const std::string& path);
// [settings] h:mm:ss <title> changed: what differs between two of those, on a line or a few
void diag_log_changes(const char* title, const std::map<std::string, std::string>& before,
                      const std::map<std::string, std::string>& after);
#endif
