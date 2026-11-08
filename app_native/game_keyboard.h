// The phone's own keyboard for the game's text (the Guild Card's greeting, the hunter's and the Palico's names, a
// hall's password...): while the game's keyboard is open (Port::Keyboard, mh4n port/src/swkbd_bridge.h), a panel at
// the top of the screen in the Monster Hunter style shows the text, the phone's keyboard comes up under it, and what is
// typed goes into the game's own text box. OK and Cancel press the game's buttons. "Game's keys" puts the panel away
// for the game's own keyboard (on the touch screen, as on the 3DS), and a small tab brings it back.
//
// ui.cpp calls both on the app's main thread; nothing here goes to the log but that a keyboard opened.
#pragma once
#include <SDL3/SDL.h>

// Every SDL event, after the settings panel's turn: true when the panel took it (the typed text, Backspace, Enter, the
// Back key while it shows, a finger on it). Releases are never taken. A controller stays the game's: its START is the
// game's keyboard's own OK, on the same text (the panel's goes into the game's text box as it is typed).
bool game_keyboard_event(const SDL_Event& e);
// Each frame inside ImGui's frame, after the touch controls. `covered`: the settings panel is over the game (the panel
// hides and the phone's keyboard is the settings' then).
void game_keyboard_frame(int width, int height, bool covered);
