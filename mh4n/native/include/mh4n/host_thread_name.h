// A name for the calling host thread, for the system's own tools (top -H, /proc/<pid>/task/<tid>/comm, a profiler's
// thread list; 15 characters at most). A thread nobody names takes its creator's: on Android every thread of the runtime
// and the app showed as "SDLThread", and a per-thread measure (scripts/pacing/threads.py) could not tell the game's
// thread from the renderer's or the timer's. Linux and Android only; elsewhere it does nothing.
#pragma once

#if defined(__linux__) || defined(__ANDROID__)
#include <pthread.h>
#endif

namespace mh4n {

inline void SetHostThreadName(const char* name) {
#if defined(__linux__) || defined(__ANDROID__)
    pthread_setname_np(pthread_self(), name);
#else
    (void)name;
#endif
}

}  // namespace mh4n
