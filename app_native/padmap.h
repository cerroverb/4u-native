// Mapping any controller, button by button (padmap.cpp, from the MHGU port's runtime/padmap.c): the settings panel's
// Controls tab, Map a controller. Everything here runs on the app's main thread (the SDL loop and the panel's frame).
#pragma once
#include <SDL3/SDL.h>

#include <string>

constexpr int PADMAP_MAX = 8;
constexpr int PADMAP_REST = -1;

// Once, after SDL_Init: the maps kept from before (<data_dir>/gamepad_mappings.txt, beside mh4u-app.ini).
void padmap_init(const std::string& data_dir);
// Each turn of the loop: the list, the mapping under way, the requests below.
void padmap_poll();
// This controller was mapped by the player (a map says which button is where: it is always read by position).
bool padmap_custom(SDL_JoystickID id);
// A controller is being mapped now: its buttons are neither the game's nor the menu's.
bool padmap_active();
// The one being mapped (0: none).
SDL_JoystickID padmap_joystick();

// The settings panel's. Each frame on its pages it calls padmap_watch(): the list is kept fresh, and leaving the pages
// by any way (Back, closing the menu) ends the mapping.
void padmap_watch();
int padmap_list(char names[][64], bool* custom, SDL_JoystickID* ids, int max);
void padmap_begin(int index);
int padmap_step();  // PADMAP_REST first, then 0 .. padmap_steps() - 1, then padmap_steps(): done
int padmap_steps();
const char* padmap_step_title(int step);  // English: the panel translates it
void padmap_continue();  // the rest: everything was moved once and let go
void padmap_skip();
void padmap_back();
void padmap_save();
void padmap_cancel();
// Back to what SDL made of it: at once when SDL had a map of its own for it before (kept with the player's), else the
// next time the game starts. True when it was at once.
bool padmap_forget(int index);
