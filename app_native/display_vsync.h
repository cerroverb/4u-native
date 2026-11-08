// The display's vsync for the game's emulated VBlank to follow (display_vsync.cpp).
#pragma once

// Starts the thread that samples the display's vsync (Choreographer) and holds the emulated VBlank on it, unless
// MH4U_VBLANK_LOCK=off.
void display_vsync_start();

struct SDL_Window;
// The surface's frame rate (a vote to SurfaceFlinger: the FPS cap), set now and again after the surface is made anew
// (display_frame_rate_refresh); fps 0 keeps the one set before.
void display_set_frame_rate(SDL_Window* window, float fps);
void display_frame_rate_refresh();
