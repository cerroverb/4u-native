// Touch controls for phones in the Monster Hunter style (touch.cpp, from the MHGU port's runtime/touch.cpp): a fixed
// stick on the left, the camera turned by dragging anywhere else, and the 3DS's buttons round the right thumb.
// Everything here runs on the app's main thread (ui.cpp calls it from ui_event and ui_frame).
#pragma once
#include <SDL3/SDL.h>

#include <cstdint>
#include <string>

#include "ui.h"

// Once, after ImGui's context exists: the layout file (<data_dir>/touch_layout.txt) is read on the first event or frame.
void touch_init(SDL_Renderer* renderer, const std::string& data_dir);
// A finger event: true when it is the controls' (a finger on the touch screen's corner never is: the game's).
bool touch_event(const SDL_Event& e, const UiScreens& screens);
// Each frame, inside ImGui's frame: works out this frame's input (added to `out`) and draws the controls on the
// background draw list, in ImGui's display units (`width`, `height`: the window's pixels, as the fingers are).
void touch_frame(int width, int height, const UiScreens& screens, bool controller, UiInput& out);
// Every finger gone (the app went to the background: their ups will not come).
void touch_reset();
// The touch controls' vibration for a pressed control (town.cpp's plates too)
void touch_haptic_tap();
// What android_main.cpp knows of the game (ui_set_game_state). (The game's frame counter, for how long a quick tap is
// held, touch.cpp reads itself: a screen sink of its own on the display, registered as a boot step.)
void touch_set_game_state(const UiGameState& state);
// A controller's button for the touch controls (the right stick click: the lock-on); true when it took it.
bool touch_pad_button(const SDL_Event& e);
// The touch screen's corner: wanted shown or not (one choice while the touch controls show, another while they hide).
bool touch_bottom_wanted();
void touch_toggle_bottom();
// Whether the touch screen's corner starts shown (the single-screen mod puts the game's touch menus on the top
// screen: the corner starts hidden, and the touch-screen button still brings it back).
void touch_set_bottom_default(bool shown);
// The controls' settings (the settings panel's Controls tab), by their keys in touch_layout.txt: "controls" (auto, on,
// off), "opacity" (0.20 .. 1.00), "camera_speed" (0.20 .. 5.00), "attack_style" (0 the pill, 1 two buttons, 2 the corner
// band) and "show_<button>" (1 shown, 0 hidden: show_attacks, show_dodge, ...). Empty for a key that isn't one.
std::string touch_setting(const std::string& key);
// Sets one at once and saves touch_layout.txt (a value out of its range is brought into it).
void touch_set_setting(const std::string& key, const std::string& value);
// The game's buttons combined button k (0..5, "combo<k+1>") presses, as mh4n::kPad* bits with ZL 1 << 14 and ZR 1 << 15
// (controls.cpp's): 0 none. A controller's button presses them too ([Controls] Combo<n>Pad, controls.h).
std::uint32_t touch_combo_buttons(int k);
// Opens the layout editor (the settings panel's Customize the layout; a long press on the settings button does too).
void touch_edit_begin();
