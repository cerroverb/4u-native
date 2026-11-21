// Damage numbers (1.0.1, off unless the player turns them on: Graphics & Audio > Screen and HUD): the port finds each
// hit's damage and its place on the top screen (mh4n port/src/hunt_aids.h), the app draws it here, over the top screen's
// picture and under the touch controls, never in the game's picture.
#pragma once
#include <SDL3/SDL.h>

#include <cstdint>
#include <vector>

// One number as it is drawn: the text's centre (window pixels), its height, its opacity (0..1) and its value; (Burst)
// how many hits it holds.
struct HuntNumberDrawn {
    float x = 0, y = 0, size = 0, alpha = 0;
    int value = 0;
    int hits = 1;
};

// The numbers' styles (1.0.2, [Graphics] DamageNumberStyle, the port's own looks): Classic as they came; Outlined, a
// thick outline and a jump in; Compact, smaller and shorter; Burst, a run of hits on one monster summed in one number
// (mh4n's damage_number_burst) that jumps at each hit, stays put and shows how many hits it holds.
enum HuntNumberStyle { kStyleClassic, kStyleOutlined, kStyleCompact, kStyleBurst };

// Where the numbers dealt in the last second go on a top screen drawn at `top` (window pixels), at `now_ns` (the steady
// clock's): each rises 8% of the screen's height while it lasts and fades in its last 40%; its text `text_height` of the
// screen's height (Medium, 7.5%: 18 of the console's 240 lines; Small 4%, Large 10%), whole pixels. Numbers off the
// picture are left out.
std::vector<HuntNumberDrawn> hunt_numbers_layout(const SDL_FRect& top, std::int64_t now_ns, float text_height = .075f,
                                                 int style = kStyleClassic);

// Draws them (Dear ImGui's background list, clipped to the top screen) at the player's size and colour ([Graphics]
// DamageNumberSize / DamageNumberColour, read every half second); nothing while the option is off.
void hunt_numbers_draw(const SDL_FRect& top);
