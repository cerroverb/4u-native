// MH4U for PC: the display's vsync for the game's emulated VBlank to follow (pc_vsync.cpp), as the Android app's
// display_vsync.h. With VSync on, a VBlank on its own clock beats against the display's (the game's 59.83 Hz against a
// 60 Hz display: a picture shown twice every few seconds, a 33 ms hitch; 144 against a 143.9 Hz one: every 10 s): held to
// the display's vsync (gsp FollowDisplay: when the FPS limit's rate is within 1 % of the display's), one picture a vsync.
// With VSync off the VBlank keeps its own clock. MH4U_VBLANK_LOCK=off: never held.
#pragma once
#include <SDL3/SDL.h>

// The window whose display is followed (the thread starts the first time)
void pc_vsync_start(SDL_Window* window);
// VSync on: the VBlank held to the display's vsync; off: its own clock
void pc_vsync_lock(bool on);
