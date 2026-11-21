// A name for the calling host thread, for the system's own tools (top -H, /proc/<pid>/task/<tid>/comm, a profiler's
// thread list; 15 characters at most). A thread nobody names takes its creator's: on Android every thread of the runtime
// and the app showed as "SDLThread", and a per-thread measure (scripts/pacing/threads.py) could not tell the game's
// thread from the renderer's or the timer's. Linux and Android: pthread_setname_np. Elsewhere through
// g_host_thread_namer when a program sets it (the PC app: SetThreadDescription, for its per-thread [perf] lines and a
// profiler's thread list; this header stays free of windows.h), else it does nothing.
#pragma once

#if defined(__linux__) || defined(__ANDROID__)
#include <pthread.h>
#endif

namespace mh4n {

#if !defined(__linux__) && !defined(__ANDROID__)
// (set once, before the runtime starts its threads)
inline void (*g_host_thread_namer)(const char* name) = nullptr;
#endif

inline void SetHostThreadName(const char* name) {
#if defined(__linux__) || defined(__ANDROID__)
    pthread_setname_np(pthread_self(), name);
#else
    if (g_host_thread_namer != nullptr) g_host_thread_namer(name);
#endif
}

}  // namespace mh4n
