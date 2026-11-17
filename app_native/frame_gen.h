// Frame generation for the top screen ([Graphics] FrameGen in mh4u-app.ini, off by default): frame_gen.cpp. One made-up
// picture between each two of the game's top-screen pictures, when the game is held to half the display's rate or less
// (30 FPS on 60 Hz: 60 shown). android_main.cpp presents it before the game's ("FRAMEGEN:"); with the setting off nothing
// here runs and the window's frames are drawn as before. Main thread only (SDL's GL context).
#pragma once

#include <SDL3/SDL.h>

struct SharedScreenView;

// The setting (settings_panel.cpp, and android_main.cpp at the start); MH4U_FRAMEGEN=1 turns it on for tests
void frame_gen_set_enabled(bool on);
bool frame_gen_enabled();

// A part of the HUD on the top screen: left, top, right, bottom as fractions of its picture (Port::Hooks' HUD parts)
struct FrameGenBox {
    float r[4];
};

// A new top-screen picture is shown in `view`. n: the vsyncs a game frame takes (present_pacing_cadence; < 2: none made).
// skip: why none is made now (a menu, the settings...; nullptr: none). Returns the picture made between the one before
// and this one, upright at the picture's own size (the window draws it where the top screen goes), and in *follow the
// vsyncs the game's picture comes after it; nullptr when none is made (warming up, skipped, not possible here).
SDL_Texture* frame_gen_make(SDL_Renderer* renderer, SharedScreenView& view, int n, const FrameGenBox* hud, int nhud,
                            const char* skip, int* follow);
// The made picture was presented (the per-minute line)
void frame_gen_presented();
// The GL context was lost: everything is made again when needed
void frame_gen_reset();
