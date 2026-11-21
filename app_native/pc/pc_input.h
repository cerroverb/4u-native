// MH4U for PC: the PC's own controls beside the shared ones (controls.cpp: the keyboard, the mouse and the controllers;
// touch.cpp: the touch controls and their editor), pc_input.cpp:
//   - the keybinds only the PC has (controls.h BIND_HELP..: [Keybinds] ControlsList, TouchItem1..4, GuildCards,
//     Gestures, Friends, HuntersForHire): the list of the controls (F2), the touch screen's item shortcuts in a quest (1 to 4: a tap on that item of the item shortcuts panel, wherever the player's panel
//     layout has it), the town's four touch-screen actions (G Gestures; the others on no key by default)
//   - the list of the controls over the game, EN/ES, from the keybinds as they are (F2, Controls > Keyboard and mouse >
//     List of the controls): the keyboard and the mouse, a controller, the touch screen; F2, Esc, a click or a
//     controller's B hides it, and the game waits meanwhile, playing alone
//   - a line at the first starts that says where the settings and the list are
// Everything here runs on the main thread.
#pragma once
#include <SDL3/SDL.h>

#include <string>

#include "imgui.h"
#include "settings_icons.h"

// controls.cpp: a key or mouse button on one of the PC's keybinds went down (not while typing, nor in the settings)
void pc_bind_pressed(int bind);
// ui.cpp's ui_event, first: the list's own events while it shows (true: taken)
bool pc_input_event(const SDL_Event& e);
// ui.cpp's ui_frame, in ImGui's frame, over the touch controls and under the settings: the list, the first starts' line
void pc_input_frame(int width, int height);
// The list of the controls over the game
bool pc_help_open();
void pc_help_show();
// pc_main.cpp's send_input: a key's tap on the touch screen this turn (1: down at x, y of its 320 x 240; 0 none)
int pc_key_touch(int* x, int* y);
// The settings' words for a PC (settings_panel.cpp's tr, en_es, say and help, the chat's): a phone said as the PC, its
// Wi-Fi as the network, a tap as a click, in English and Spanish (the game's touch screen and the touch controls keep
// their names). The same text when there is nothing to change; else the PC's, kept for the program's life
const char* pc_words(const char* text);
std::string pc_words(const std::string& text);

// The controller's buttons and the keyboard's keys drawn in the touch controls' style (the menu's guides, the town's list).
// The look the controller's buttons are drawn in for [Controls] ButtonIcons' index `setting` (0 auto: the controller in
// use's): 1 Xbox, 2 PlayStation, 3 B A Y X letters, 4 the game's own letters
int pc_icon_look(int setting);
// A face button (the 3DS's A, B, X, Y: 0..3) where the controller in use has it, in that look, its top-left at p; its width
float pc_face_glyph(ImDrawList* dl, ImFont* font, ImVec2 p, float size, int button, int look, const SettingsIcons::Ink& ink);
// A key's cap with its name, its top-left at p (dl null: only measured); its width
float pc_key_cap(ImDrawList* dl, ImFont* font, ImVec2 p, float size, const char* name, const SettingsIcons::Ink& ink);
// town.cpp's glyphs (SettingsIcons' A, B, X, Y, L, R, Dpad; c: the middle): the keyboard's key for that button when the
// keyboard was used last, a controller's button in its look; false: the 3DS's own glyph, as before
bool pc_glyph(ImDrawList* dl, ImFont* font, ImVec2 c, float size, int glyph, const SettingsIcons::Ink& ink);
