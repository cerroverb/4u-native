// Monster health bars (1.0.1, off unless the player turns them on: Graphics & Audio > Screen and HUD): mh4n's
// HuntAids::Healths() lists each large monster alive in the quest with its health; the app draws a bar for each over the
// top screen's picture, under the touch controls, never in the game's picture, in the look of the app's own HUD (the FPS
// counter's plate: wine, with red and moss rules, the numbers in ink), each with the monster's name at its left in the
// game's language (mh4n's platform/monster_names.cpp reads them from the game's RomFS). Where they go and how big they are can be changed in Customize controls and HUD
// (touch.cpp): dragged, sized by their corner handle, a pinch or - / +, Reset.
#pragma once
#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "hooks.h"
#include "hunt_aids.h"

struct ImFont;

// One bar as it is drawn (window pixels): its plate, the groove the health fills in it, how full (0..1) and in what
// colour (green over half, gold over a quarter, red under: the FPS counter's), its opacity (a monster in another area of
// the map fainter), its numbers ("1234 / 2546") right-aligned on the plate and the monster's name at its left end in
// their size and ink (shortened with an ellipsis to at most 45% of the room left of the numbers; the groove between).
struct HuntBarDrawn {
    SDL_FRect frame{}, track{};
    float fill = 0, alpha = 1, text_size = 0, text_right = 0, text_y = 0, line = 1, name_x = 0;
    unsigned colour = 0;  // (ImU32, opaque)
    std::string text, name;
};

// Where the HUD of the game leaves room for them at their own place (window pixels, hunt_bars_layout's `left`, `under`
// and `below`): after the hunter's own health and stamina bars (the HUD's part 1, where it is drawn: hud_edges, the
// layout editor) and under the area map or the target camera when they sit in the top right corner.
struct HuntBarsRoom {
    float left = 0, under = 0, below = 0;
};
HuntBarsRoom hunt_bars_room(const SDL_FRect& top, const Port::Hooks::GameState& game, float k = 1.f);

// Where the bars go on a top screen drawn at `top` (window pixels), one under the other: plates 46% of the screen's
// height long and 3% tall (whole pixels) as they come, times `k` (the size the layout editor gave them).
// - At their own place (`px` < 0): from the top right corner, 2% from the edges. `left` is where they may start
//   (shorter to fit after it, or, when that leaves less than a third of the height, under `under` at full length) and
//   `below` is where they start at the earliest (hunt_bars_room).
// - Placed in the layout editor: their block's top right corner at (`px`, `py`), fractions of the top screen's picture
//   from its left and top edges, at full length, kept on the picture.
// `names` (UTF-8, in the order of `healths`; with `font`, which measures them): an empty one, or one with a character
// the font has no glyph for, leaves its bar without a name.
std::vector<HuntBarDrawn> hunt_bars_layout(const SDL_FRect& top, const std::vector<Port::HuntAids::Health>& healths,
                                           float left, float under, float below, float k = 1.f, float px = -1.f,
                                           float py = -1.f, const std::vector<std::string>* names = nullptr,
                                           ImFont* font = nullptr);

// A name in at most `most` pixels at `size` of `font`: whole, or cut at a character with an ellipsis ("..." where the
// font has no U+2026); empty when not even one letter fits, or the font lacks one of its characters. Its width in `w`.
std::string hunt_bars_fit_name(ImFont* font, float size, const std::string& name, float most, float* w);

// The bars' block as last drawn (window pixels: x0, y0, x1, y1, their plates), for the layout editor's frame, drag and
// corner handle; false while none shows.
bool hunt_bars_rect(float r[4]);

// The layout editor's drag (touch.cpp): the block drawn at `block` (window pixels, hunt_bars_rect's) moved by (dx, dy),
// kept on the top screen's picture whole; its new place as touch_bars_place gives it (`px`, `py`).
void hunt_bars_drag(const SDL_FRect& top, const float block[4], float dx, float dy, float* px, float* py);
// A size the editor sets (its - / +, a pinch): in twentieths, half to twice the size they come with.
float hunt_bars_size(float k);
// The size after the editor's corner handle (on the block's bottom left corner: the top right one stays) was dragged
// from (x0, y0) to (x, y), window pixels, the block drawn at `block` at size `k0` when it was picked: as much larger as
// the finger went further from the block's top right corner; hunt_bars_size's steps.
float hunt_bars_grip(float k0, const float block[4], float x0, float y0, float x, float y);

// (touch.cpp) where the layout in use has the bars (moved and sized in the layout editor, kept in the layout and its
// profile): their block's top right corner, fractions of the top screen's picture from its left and top edges, and
// their size (1 as they come); false (x, y untouched) while they are at their own place, `k` given either way.
bool touch_bars_place(float* x, float* y, float* k);

// Draws them (Dear ImGui's background list, clipped to the top screen); nothing while the option is off, out of
// quests, or while a menu of the game is open (it would sit on the menus the game draws on the top screen). In the
// layout editor with the option on, a stand-in bar where there is none, so that it can be placed out of quests.
void hunt_bars_draw(const SDL_FRect& top);
