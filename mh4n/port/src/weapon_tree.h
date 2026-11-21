// The weapon tree (1.0.2): in a town's Smithy, while it shows a weapon (the forge's list, Upgrade's list), the app
// offers a button that opens the tree of that weapon: the ones the player has (box, worn), the ones before them in
// their line, and the next step from each as "?" (weapon_tree_view.h). Read-only and with no hook: once a frame, after
// sMhMain::frame, this reads the game's memory (the town's controller for the Smithy, the save for the box and the
// weapon worn, the game's own weapon tables, which are the European executable's as Heavenfall documents them:
// weapon_tree.cpp). The code library is not changed (config/hooks.txt untouched; the addresses are in
// config/port_addresses.txt).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "weapon_tree_view.h"

struct RCtx;

namespace Port::WeaponTree {

/// What the Smithy shows now: its list's weapon under the cursor (the forge's list or Upgrade's), as an equipment type
/// (7 the great sword ... 20 the charge blade, the game's numbers) and an id of that class. `serial` grows each time
/// the weapon shown changes. Safe from any thread.
struct Smithy {
    bool shown = false;
    int type = -1, id = -1;
    int language = -1;          // the game's language (its folders' row: 1 eng, 2 fre, 3 spa, 4 ger, 5 ita)
    std::uint32_t serial = 0;
};
Smithy Current();

/// The tree of the weapon the Smithy shows (weapon_tree_view.h's Build), with each weapon's name in the game's language
/// (empty while they are being read: platform/weapon_names.h) and the player's box and worn weapon as the last frame in
/// the Smithy read them. False while the Smithy shows no weapon, or the tables are not the ones known (a mod moved
/// them, another version). Safe from any thread.
bool Tree(weapon_tree::Class* cls, weapon_tree::View* view, int* type);

/// The class's equipment types: 7 to 20 (kFirstType + 0 ... 13).
constexpr int kFirstType = 7, kClasses = 14;

/// The options (mh4u.ini, the app, the runner's --set): weapon_tree (on unless off: the Smithy's state read for the
/// app's button), weapon_tree_trace (tests: a line each time the weapon shown changes, with the tree as text) and
/// weapon_tree_states (tests: "all", the default, or the Smithy's states whose list counts, "3 8").
/// False for another key.
bool SetOption(const std::string& key, const std::string& value);
bool On();

void Reset();
void SetLog(void (*log)(const std::string& line));
/// Where the weapons' names come from (the native platform's platform/weapon_names.cpp sets it as the game starts): the
/// name of weapon `id` of equipment `type` in the game's `language` (empty while being read), the request to read a
/// language's (once), the name of item `id` (the recipes' materials), and the classes' icons from the game's icon sheet
/// (Icons). Without it the tree has no names, and the app draws its own icons.
void SetNames(std::string (*name)(int language, int type, int id), void (*prepare)(int language),
              std::string (*item)(int language, int id) = nullptr,
              bool (*icons)(int language, std::vector<std::uint8_t>* rgba, int* cell) = nullptr);
/// The classes' icons in the game's `language` (its icon sheet, read from the player's RomFS: the large icons of the
/// weapons, cells 55 to 69 but 64, in weapon_tree_panel.h's WeaponClass order), `cell` pixels a side each, side by side,
/// RGBA; false while they are being read or with none. Safe from any thread.
bool Icons(int language, std::vector<std::uint8_t>* rgba, int* cell);
/// hooks.cpp's sMhMain::frame, after the game's frame.
void Frame(RCtx* ctx);

}  // namespace Port::WeaponTree
