// Android performance hints (perf_hint.c): one ADPF session for the threads the frame rate rests on, its target the
// frame period at the FPS cap, each presented frame's work reported to it.
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
enum { PERF_GAME, PERF_RENDER, PERF_SUBMIT };
void perf_hint_thread(int role);  // the calling thread is that one
void perf_hint_frame(void);       // a frame was presented (the presenting thread, once per frame)
void perf_hint_set_fps(float fps);  // the FPS cap (mh4u.ini's fps)
void perf_mem_report(void);       // the process's memory, to the log
#ifdef __cplusplus
}
#endif
