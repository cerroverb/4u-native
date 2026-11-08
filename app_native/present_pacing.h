// The window's presents timed to the display (present_pacing.cpp).
#pragma once

#include <cstdint>

// Before SDL_RenderPresent, on the thread whose GL context presents: the time the next buffer is to be shown
// (EGL_ANDROID_presentation_time), on the display's vsync grid. `new_frame`: the buffer carries a new game frame (the top
// screen's picture changed); `fps_cap`: the settings' FPS limit. Off with MH4U_PRESENT_TIME=0.
void present_pacing_before_present(bool new_frame, float fps_cap);

// How the margin learns the display's presentation deadline (ns; 0 if unknown): a call to the activity, on the thread
// that presents, each time the display's rate changes.
void present_pacing_set_deadline_query(std::int64_t (*query)());

// A buffer with a new game frame is waiting for a vsync to come (the limit's pacing is on): a present with nothing new
// now would only take another of the window's buffers, and wait for one to come back.
bool present_pacing_frame_pending();
