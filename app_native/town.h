// The town's actions that only the touch screen had (town.cpp): Guild Cards, Gestures, Friends and Hunters for Hire,
// without the touch screen. A list in the Monster Hunter style opens from a touch button of its own (touch.cpp, "town",
// shown only in a town while the hunter is walked) or from a controller's button ([Controls] TownMenu = select, l3, r3
// or off; SELECT by default, whose own use in a town, the touch screen's Customize Panels, a single screen does without).
// The pick taps the touch screen where the town's panel has it, hidden or not; what it opens on the touch screen is then
// shown large in the middle until the game leaves it: the Guild Cards', Gestures' and Friends' menus have the game's own
// cursor (the controller's buttons are the game's), the Hunters for Hire panel a cursor of the app's (the stick or the
// D-pad moves it, A taps, B closes). Fingers tap the large touch screen as they would the 3DS's.
// Everything here runs on the app's main thread.
#pragma once
#include <SDL3/SDL.h>

#include <cstdint>

// android_main.cpp, each turn of the loop: the game's place (1 a town), its menu (1 a menu or a talk has the buttons, 0
// the hunter is walked, -1 not known), frame counter and the town's touch screen page (2 the map, 18 Hunters for Hire,
// another a menu's page, -1 not known: Port::Hooks::CurrentGameState)
void town_set_game(int place, int menu, std::uint32_t frame, int panel);
// The list can open now: a town, the hunter walked, nothing of this module's under way
bool town_available();
// Opens the list (by_pad: a controller's button, which then moves its cursor) and closes it
void town_open(bool by_pad);
void town_close();
// Android's Back (the key or the gesture): the list closes, the large touch screen closes (a menu all the way, with the
// game's B; the panel back to the map). True when it was the module's.
bool town_back();
bool town_list_open();
// Something of this module's is over the game (the list, an action under way, the large touch screen): the touch
// controls neither show nor take fingers
bool town_busy();
// The touch screen shown large in the middle, `rect` its place in a window of w x h pixels, `alpha` how far it is in
// (it fades and grows in, and fades out, in 130 ms)
bool town_view(int w, int h, SDL_FRect* rect, float* alpha);
// The controller is this module's (the list, the panel's cursor): controls_input gives the game nothing meanwhile
bool town_takes_pad();
// While town_takes_pad: the buttons held (mh4n::kPad*, the controller's and the keyboard's), those pressed since the last
// call (a press shorter than a turn of the loop is one too) and the left stick (-1..1, y up)
void town_pad(std::uint32_t held, std::uint32_t pressed, float stick_x, float stick_y);
// The touch the module gives the game this turn: 1 down at x, y (the 3DS's 320 x 240), 0 none, -1 not the module's
int town_touch(int* x, int* y);
// The game's buttons the module presses this turn (the large touch screen's Back: B)
std::uint32_t town_buttons();
// A finger (ui_event): true when the module took it. Fingers on the large touch screen are left to android_main.cpp.
bool town_event(const SDL_Event& e);
// Draws the list, the large touch screen's frame, its title, Back and the cursor (ui_frame, in ImGui's frame)
void town_frame(int w, int h, bool controller);
