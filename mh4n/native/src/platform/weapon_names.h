// The weapons' names for the Smithy's weapon tree (1.0.2, port/src/weapon_tree.h), in the language the game shows its
// texts in: read once per language, on a thread of their own, from the game's RomFS as monster_names.h reads the
// monsters' (the update's first, then the game's, with the mods that are on laid over them): one lmd message file a
// class, <language>\msg\<class>Name_<language> (LswordName, SwordName, ... GaxeName: one text per weapon id) in
// <language>/data/core_common.arc. From the same file, the items' names (the recipes' materials: itemName_<language>) and
// the weapons' icons of the game's icon sheet (cmn_icon_GSM_NOMIP), for the tree's panel: the player's own files, read
// as the game runs (none of it is in the app).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mh4n::platform::weapon_names {

// The name of weapon `id` of equipment `type` (7 the great sword ... 20 the charge blade) in the game's `language`
// (UTF-8, trimmed); empty while they are being read, or with none. Any thread.
std::string Name(int language, int type, int id);

// The name of item `id` in the game's `language` (empty while being read, or with none). Any thread.
std::string Item(int language, int id);

// The weapons' icons in the game's `language`: one cell a class (weapon_tree_panel.h's WeaponClass order), `cell`
// pixels a side, side by side, RGBA; false while being read or with none. Any thread.
bool Icons(int language, std::vector<std::uint8_t>* rgba, int* cell);

// Starts reading the names of `language`, once (nothing for -1 or a language the game has no texts in). Any thread.
void Prepare(int language);

// (tests) the names of every class in `language` read now through the game's RomFS (by type - 7, then by id): false,
// and why, when they could not be.
bool Read(int language, std::vector<std::vector<std::string>>* names, std::string* why);

}  // namespace mh4n::platform::weapon_names
