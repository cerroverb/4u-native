// FSR 1 for the top screen (1.0.2, S6: Delta's thread and abelhk96's 4u-native #68; [Graphics] Upscale = fsr1 in
// mh4u-app.ini, the settings' Graphics & Audio > Upscale filter, off as it comes). AMD FidelityFX Super Resolution 1.0
// (github.com/GPUOpen-Effects/FidelityFX-FSR, MIT: THIRD_PARTY_NOTICES), the MHGU port's same two passes and numbers:
// EASU scales the picture up to the size it is shown at, an edge-adaptive 12-tap filter, and RCAS sharpens the result
// (sharpness 0..1, RCAS's stops = 2 - 2 x sharpness, as FSR 3.1's Sharpness in the MHGU port).
//
// Where: the top screen, when it is shown larger than its picture (a 3D resolution below 100 %, the 3DS's 1x: the
// renderer draws whole multiples of 400x240), in the draw that would put it into the window: its picture upright into a
// render target of its own size (shared_screens_draw_upright, as frame generation does), EASU into one of the size it is
// shown at, RCAS into another, which is then drawn as a screen SDL would draw (shared_screens_draw: the colour filter goes
// on it as on the game's own). The game draws its HUD and its text into the same picture, so they are scaled with it.
// Never with PS1 style (its pixels are meant to be blocks) nor on a context without OpenGL ES 3.0 (the log says so).
// Off, or the picture shown at its size or smaller: nothing here runs and the screens are drawn as before.
// Main thread only (SDL's renderer's).
#pragma once

#include <SDL3/SDL.h>

struct SharedScreenView;

// The settings (the panel's draft while it is open, the saved ones otherwise: settings_panel.cpp)
void fsr1_set(bool on, float sharpness);
// Draws `view` into `dst` of the window with FSR 1 and true, or false (nothing drawn: draw it as before)
bool fsr1_draw(SDL_Renderer* renderer, SharedScreenView& view, const SDL_FRect& dst, int win_w, int win_h);
// The GL context was lost (SDL_EVENT_RENDER_DEVICE_RESET): every program and picture made again when needed
void fsr1_reset();
