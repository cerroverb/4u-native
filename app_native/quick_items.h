// Quick items on the main screen (1.0.1.1, mh4u.ini quick_items, off unless the player turns it on: Graphics & Audio >
// Screen and HUD): in a quest under way, the touch screen's item shortcuts panel (the quick items the game has on the
// touch screen, in the player's panel layout: one slot, or two side by side) shown on the main screen as a piece of its
// own, the touch screen's very picture of that part, scaled. A tap there is a tap on that part of the touch screen, so
// the items are used from it. mh4n says where the panel is and when it shows (Port::Hooks::GameState's items_slot and
// items_wide: while the quest's small panels are up; not with the quest menu or a big panel on the touch screen, nor
// out of quests, nor when the player's layout has no such panel); the app leaves it out while the whole touch screen
// shows anyway (its corner, stacked or side by side). Moved and sized in Customize controls and HUD (touch.cpp: drag,
// its corner handle, a pinch or - / +, Reset), kept in the layout as quick_items x y k; there, out of quests, a
// stand-in in the app's look.
#pragma once
#include <SDL3/SDL.h>

struct SharedScreenView;

// The touch screen's panels (mh4n hooks.h: six slots of 160 x 68 under a 36-pixel bar, two columns of three)
constexpr float kQuickItemsSlotW = 160.f, kQuickItemsSlotH = 68.f, kQuickItemsTop = 36.f;

// The part of the touch screen (fractions: left, top, right, bottom) that the panel in `slot` takes, wide or not.
void quick_items_crop(int slot, bool wide, float crop[4]);

// The piece in a window w x h: as tall as `kHeight` of the window times `k` (half to twice), as wide as its shape says,
// its centre at (x, y), fractions of the window; at its own place (x < 0): the middle of the bottom edge, a little above
// it. Kept in the window whole.
SDL_FRect quick_items_layout(float w, float h, bool wide, float x, float y, float k);

// A point (x, y) of the window in the piece `rect` showing `crop`: the touch screen's point under it (320 x 240).
void quick_items_touch_point(const SDL_FRect& rect, const float crop[4], float x, float y, int* tx, int* ty);

// The layout editor (touch.cpp): the piece drawn at `block` (x0, y0, x1, y1) moved by (dx, dy), kept in the window: its
// new centre as fractions of the window; a size the editor sets, in twentieths from half to twice; the size after the
// corner handle (the bottom right corner: the centre stays) went from (x0, y0) to (x, y).
void quick_items_drag(float w, float h, const float block[4], float dx, float dy, float* px, float* py);
float quick_items_size(float k);
float quick_items_grip(float k0, const float block[4], float x0, float y0, float x, float y);

// Once a frame, before the screens are drawn (android_main.cpp): whether the piece shows now (the option on, a quest's
// panel shown by mh4n, the whole touch screen not shown, the settings closed; in the layout editor always, a stand-in
// out of quests), worked out for a window w x h. `bottom_shown`: the whole touch screen shows.
bool quick_items_update(int w, int h, bool bottom_shown);
// Whether the touch screen's picture is needed for it (shown, and not a stand-in): the renderer must draw that screen and
// the app take its pictures even with the touch screen hidden.
bool quick_items_needs_touch_screen();
// Draws it: the touch screen's part, at `alpha`, with the screens (SDL and GL, before the app's UI); the stand-in in the
// app's UI frame (ui.cpp, Dear ImGui's background list). Under the touch controls and the menus either way.
void quick_items_draw(SharedScreenView& bottom, int win_w, int win_h, Uint8 alpha);
void quick_items_draw_stand_in();
// Whether (x, y) of the window is on it (a finger there is the game's stylus, not the touch controls'), and the touch
// screen's point it stands for.
bool quick_items_hit(float x, float y);
bool quick_items_touch(float x, float y, int* tx, int* ty);
// The piece as last laid out (x0, y0, x1, y1; window pixels), for the layout editor; false while it doesn't show.
bool quick_items_rect(float r[4]);

// (touch.cpp) where the layout in use has the piece: its centre (fractions of the window) and size; false (x, y
// untouched) at its own place, k given either way.
bool touch_items_place(float* x, float* y, float* k);
