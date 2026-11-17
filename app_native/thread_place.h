// Where the threads that carry a frame run, and at what priority (thread_place.c): MH4N_PERF_NICE and
// MH4N_PERF_AFFINITY, read once at the start, both off until measured.
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
enum { PLACE_GAME, PLACE_RENDER, PLACE_PRESENT, PLACE_ROLES };
void thread_place_init(void);       // the switches (after launch.txt's env=), and the thread that applies them if any is on
void thread_place_role(int role);   // the calling thread is that one
void thread_place_shown(void);      // a picture of the game's was presented (the first one starts the placing)
void thread_place_resume(void);     // the app is back in the foreground: placed again now
#ifdef __cplusplus
}
#endif
