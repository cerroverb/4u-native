// The in-game settings (settings_panel.cpp, from the MHGU port's runtime/settings_panel.cpp, its phone layout): a panel
// in the Monster Hunter style over the game, with tabs at its left, rows a finger drags and taps, a picker for a row's
// choices, the picked row's explanation, Save, and confirmations before what can't be undone. A controller works it
// too: the D-pad or the stick moves, A confirms, B goes back, X saves, L and R change tabs.
// Everything here runs on the app's main thread, inside ui.cpp's ImGui frame.
#pragma once
#include <SDL3/SDL.h>

#include <string>

// Each frame, between ImGui::NewFrame and ImGui::Render (after the touch controls, so it is drawn over them). Does
// nothing while *open is false; sets it false when the player closes the panel (X, Back to the game, B / Back with
// nothing unsaved, or after the unsaved-changes question). It also draws the FPS counter while that is on (the
// Graphics & Audio tab), open or not.
void settings_panel(bool* open);

// Every SDL event, after ImGui_ImplSDL3_ProcessEvent: true while the panel is open and the event is one it works with
// (fingers, the mouse, a controller's presses and sticks, keys), which then go neither to the touch controls nor to
// the game. Releases (a button's or a key's up) are never taken, so nothing stays held in the game.
bool settings_panel_event(const SDL_Event& e, bool open);

// A step back from outside (the Android Back key is read by the panel itself): what B does.
void settings_panel_request_back();

// Closing from outside (a controller's START + SELECT, controls.cpp): what Close does, asking first when something is
// unsaved.
void settings_panel_request_close();
// The thanks card (once, after the credits the first time the app opens; again from Credits & Licenses): while it
// shows it takes the fingers, the controller and the keys (ui_event, first), and the game gets no input
bool settings_panel_thanks_open();
bool settings_panel_thanks_event(const SDL_Event& e);

// A notice over the game for a few seconds, in the menu's language (a controller that needs a map, controls.cpp).
void settings_panel_notice(const std::string& english, const std::string& spanish);

// True once after the panel closed (android_main.cpp reads mh4u-app.ini's [Display] options again then).
bool settings_panel_closed_now();

// Something is unsaved, or a confirmation is up: the panel wants to stay open.
bool settings_panel_dirty();

// The language the panel and the FPS counter use: mh4u-app.ini's [Interface] Language, else the phone's.
bool settings_spanish();
