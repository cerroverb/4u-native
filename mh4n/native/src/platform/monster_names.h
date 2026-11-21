// The large monsters' names for the app's health bars (1.0.1.1), in the language the game shows its texts in
// (Port::HuntAids::GameLanguage()): read once per language, on a thread of their own, from the game's RomFS as the game
// reads it (services/fs/game_romfs.h: the update's first, then the game's, with the mods that are on laid over them;
// romfs.bin or the player's .cci/.cia alike): the message file <language>\msg\emName_<language> (an lmd: UTF-16 texts,
// one per row of the game's monster table, which is a monster's Health::kind) in <language>/data/core_common.arc (an MT
// Framework ARC: zlib entries). The game keeps none of them in memory during a quest.
//
// The runtime starts the reading itself (a frame after monster_health_bar is on and the game's language is known: at
// the title, before any quest), so the app and the PC runner alike log "monster names: ..." once per language.
#pragma once

#include <string>
#include <vector>

namespace mh4n::fs {
struct GameRomFsFile;
}

namespace mh4n::platform::monster_names {

// The name of a monster of `kind` in the game's `language` (UTF-8); empty while they are being read, or with none.
// Any thread.
std::string Name(int language, int kind);

// The name of the map of the game's stage `stage` (its files' m<nn>: 1 the Ancestral Steppe) in `language`, as the game
// writes it on its quest lists (<language>\msg\common\mapName_<language>, in the same archive: one per stage, the first
// stage's first); read with the monsters' names. Empty while they are being read, or with none. Any thread.
std::string MapName(int language, int stage);

// Starts reading the names of `language`, once (nothing for -1 or a language the game has no texts in). Any thread.
void Prepare(int language);

// The folder of the game's texts in `language` (its table at 0x0105722c: 1 "eng", 2 "fre", 3 "spa", 4 "ger",
// 5 "ita"), nullptr for one the game has none in.
const char* Folder(int language);

// (tests) the names of `language` read now through the game's RomFS: false, and why, when they could not be.
bool Read(int language, std::vector<std::string>* names, std::string* why);

// A file of an MT Framework archive of the game's RomFS (ARC\0, version 0x13: 80-byte entries, a name of 64, its type,
// sizes and place; the data a zlib stream, or stored) by its name ("eng\msg\emName_eng"), inflated (the weapon tree's
// names read the same archive: platform/weapon_names.cpp).
bool ArcFile(const fs::GameRomFsFile& arc, const std::string& name, std::vector<unsigned char>* out);

// (tests) the texts of an lmd message file (UTF-8), in their order.
bool LmdTexts(const std::vector<unsigned char>& lmd, std::vector<std::string>* texts);

}  // namespace mh4n::platform::monster_names
