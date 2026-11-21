// MH4U for PC: the [perf] lines of log.txt, as the Android app's "[perf] the last 60 s" (perf_hint.c): every minute
// (MH4U_PERF_SECONDS=<s> for tests) the game's frames a second, their frame times (median, 90th and 99th percentile, the
// longest, how many over 25 and 50 ms), the window's presents and their gaps, the CPU each of the program's threads used
// (in cores, by thread name: mh4u-game is the game's code and the runtime's scheduler, mh4n-render the renderer,
// mh4u-window the window's thread...) and the GPU's time a frame (the renderer's timestamps). Read on a thread of its
// own; the game's threads only count.
#pragma once
#include <cstdint>

// The host threads named for Windows' tools and these lines (mh4n::SetHostThreadName -> SetThreadDescription), and the
// calling thread "mh4u-window". Before the runtime starts.
void pc_perf_name_threads();
// Names the calling thread (UTF-8, as SetHostThreadName)
void pc_perf_name_this_thread(const char* name);
// A game frame's picture came (the sink's Publish): its frame number. The interval since the previous frame is a frame
// time when the two follow each other.
void pc_perf_game_frame(uint32_t frame);
// The window presented
void pc_perf_present();
// The thread that writes the lines (once; the first line a period after)
void pc_perf_start();
