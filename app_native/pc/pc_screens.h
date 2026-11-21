// MH4U for PC: what the PC's screens add to shared_screens.h (pc_screens.cpp).
//
// The window is drawn with SDL's OpenGL renderer. The renderer's screens come as shared pictures (mh4n present.h
// SharedPicture: a Vulkan allocation exported as an NT handle, imported here with GL_EXT_memory_object_win32: no copy
// through the CPU), or as pixels where that can't be (another GPU, an old driver: MH4U_PRESENT_CPU=1 forces it). A
// picture the renderer's GPU has not finished waits in its view (no sync file on Windows): pc_screens_poll shows the
// newest finished one. The colour filter, PS1 style, Brick's sharp pixels and the picture filter are the Android app's
// programs (GLSL ES 1.00, run through the desktop GL's ES compatibility), folded into the draw that puts a screen into
// the window.
#pragma once

#include <SDL3/SDL.h>

#include "shared_screens.h"

// The renderer's buffers can be shown here (the GL context imports them and runs on the renderer's GPU). Set once the
// GL context exists (shared_screens_init) and the device's UUIDs were compared with the first picture's.
bool pc_screens_shared_possible();
// A view's newest picture the renderer's GPU has finished, shown now if it is newer than the one shown: true when the
// view changed. Each loop pass, for every view.
bool pc_screens_poll(SharedScreenView& view);
// Some view waits for an unfinished picture (the loop's wait is then short)
bool pc_screens_pending(const SharedScreenView* views, int count);
// The 3D picture's filter when it is scaled to the window ([Graphics] Upscale): 0 smooth (linear), 1 sharp pixels (each
// one a square, its edges blended over a window pixel), 2 nearest. PS1 style takes its own.
void pc_screens_set_upscale(int mode);
// A second window's renderer (the touch screen in a window of its own), its GL context sharing the first one's objects:
// a view of the first renderer drawn into it with the same programs. Null: none.
void pc_screens_set_second(SDL_Renderer* renderer);
// `view` (the first renderer's) into `dst` of the second renderer's window, `out_w` x `out_h`: queued SDL work of that
// renderer goes first. False when it can't be drawn there (pixels: the caller uploads them into that renderer instead).
bool pc_screens_draw_second(SharedScreenView& view, const SDL_FRect& dst, int out_w, int out_h);
// Makes the first renderer's GL context current again (after the second window's draw)
void pc_screens_make_current();
