// The Smithy's weapon tree (1.0.2): while the game's Smithy shows a weapon (its forge's list, Upgrade's list), a touch
// button of its own (touch.cpp, "weapon_tree": it comes and goes with the Smithy's weapon, and the layout editor moves
// or hides it like the others) or a controller's right stick click opens the tree of that weapon over the game
// (weapon_tree_panel.h): the weapons the player has (box, worn), the ones before them in their line and the next step
// from each as "?". Read-only: mh4n's Port::WeaponTree reads the game's memory; nothing is forged from here. B, Back, a
// tap beside it or its X close it; the d-pad or the stick moves its cursor, a finger taps a weapon or drags the tree.
// Everything here runs on the app's main thread.
#pragma once
#include <SDL3/SDL.h>

#include <cstdint>

// The Smithy shows a weapon now (the button shows, R3 opens)
bool weapon_tree_available();
void weapon_tree_open(bool by_pad);
void weapon_tree_close();
bool weapon_tree_is_open();
// Android's Back: closes it (true when it was open)
bool weapon_tree_back();
// While open: the controller is the panel's (controls_input gives the game nothing) and so are the fingers
bool weapon_tree_takes_pad();
void weapon_tree_pad(std::uint32_t held, std::uint32_t pressed, float stick_x, float stick_y);
bool weapon_tree_event(const SDL_Event& e);
// Draws it (ui_frame, in ImGui's frame), over everything but the settings
void weapon_tree_frame(int w, int h, bool controller);
