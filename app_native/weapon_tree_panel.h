// The weapon tree's panel (1.0.2): the tree of the weapon the Smithy shows (weapon_tree_view.h) drawn over the game as
// the series draws its weapon trees: columns of weapons joined by lines, each weapon its class's icon in its rarity's
// colour and its name, the one the Smithy shows lit, the ones had marked (box, worn), the next ones as "?" over the
// icon's silhouette; beside it, the details of the weapon under the cursor (attack, element, affinity, defense, slots,
// rarity, sharpness, what forging or upgrading it takes). The icons are the game's own, read from the player's copy of
// the game by each port (Icons); without them, drawn ones of our own. The window keeps the apps' Monster Hunter look
// (wine-dark window, red and moss rules, bronze and gold). The same file in both ports (MH4U's app_native, MHGU's
// runtime): each app opens it, feeds it fingers and its controller, and closes it (MH4U: weapon_tree_ui.cpp; MHGU:
// weapon_tree_ui.cpp). Read-only: it shows, it never forges.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"  // (ImRect, RegisterUserTexture)
#include "weapon_tree_view.h"

namespace weapon_tree_panel {

// The weapon classes, in the order of the games' icon sheet (the cells 55 to 69 of its large icons, no 64)
enum WeaponClass {
    kGreatSword, kLongSword, kSwordAndShield, kDualBlades, kHammer, kHuntingHorn, kLance, kGunlance, kLightBowgun,
    kHeavyBowgun, kBow, kSwitchAxe, kInsectGlaive, kChargeBlade, kWeaponClasses
};

// The classes' icons, from the game's own files (its icon sheet, which the game tints by rarity): kWeaponClasses square
// cells of `cell` pixels side by side, RGBA, rows from the top. `serial`: a new picture each time it changes (0 none).
struct Icons {
    int cell = 0;
    std::vector<std::uint8_t> rgba;
    std::uint32_t serial = 0;
};

// What one opening shows: the class (names in the game's language), the view, the class's name and its icon, and the
// language of the app's own words (true: Spanish).
struct Content {
    weapon_tree::Class cls;
    weapon_tree::View view;
    std::string class_name;
    int weapon_class = -1;          // WeaponClass (-1: no icon)
    const Icons* icons = nullptr;   // the game's icons (null or empty: drawn ones)
    bool spanish = false;
};

// The panel's own state between frames (one per app).
struct State {
    int focus = -1;              // the node whose details show (the Smithy's at first)
    float scroll_x = 0, scroll_y = 0;
    float detail_scroll = 0;     // the details, when they are taller than their column
    bool placed = false;         // the first frame centres the Smithy's weapon
    // (where the last frame drew things, for the fingers)
    ImRect panel, area, close, detail;
    float detail_height = 0;
    std::vector<ImRect> nodes;
    // a finger on the panel
    bool finger = false, dragged = false, close_held = false;
    ImVec2 finger_at, finger_last;
};

// Draws the panel on `dl` over a window of w x h pixels, `a` its opening (0..1: it fades and grows in), `pad`: a
// controller is in use (its hints show, the focus has a cursor).
void Draw(ImDrawList* dl, float w, float h, float a, bool pad, const Content& c, State& s);

// A finger or the mouse (window pixels): down, moved or up. True when the panel took it; `close` is set when it asks
// to be closed (its X, or a tap outside it).
bool Finger(int kind, float x, float y, const Content& c, State& s, bool* close);
enum { kDown, kMove, kUp };

// A controller's press while it is open: the d-pad or the stick (dx, dy: -1, 0, 1; y down) moves the focus along the
// tree (left its parent, right its first child, up and down the nearest one in the next row); `close` for B.
void Move(int dx, int dy, const Content& c, State& s);

// The panel's words in the app's language (its own table: the apps' settings tables stay as they are).
const char* Words(const char* english, bool spanish);

}  // namespace weapon_tree_panel
