// The guest's SleepThread as the port may shape it. Header only, like pacing_trace.h: the kernel's SVC (svc.cpp)
// reads the pointer, the port (port/src/hooks.cpp) sets it.
//
// MH4U's job workers look for work in a loop that sleeps 1 ms between looks; at a frame rate above 60 the game's main
// thread waits on them every frame, and that millisecond (half of it on average) was a fifth of a 4 ms frame at "No
// limit". The port's job_poll_us option (off by default) shortens that one sleep.
#pragma once

#include <cstdint>

namespace mh4n {

// Null: a guest thread sleeps the time it asked for. Otherwise the time to sleep for a SleepThread called from
// `return_address` (the caller's lr) that asked for `ns`. Called on the scheduler's thread.
inline std::int64_t (*g_sleep_filter)(std::uint32_t return_address, std::int64_t ns) = nullptr;

}  // namespace mh4n
