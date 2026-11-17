// The large monsters' names on their health bars (hunt_bars.cpp, 1.0.1): the game's own, in the language it shows its
// texts in (mh4n's Port::HuntAids::GameLanguage()), read from its files once per language: the message file
// <language>\msg\emName_<language> (an lmd: UTF-16 texts, one per row of the game's monster table, which is a
// monster's Health::kind) in <language>\data\core_common.arc of the update's RomFS (update_romfs.bin in the game folder,
// the one the game loads) or else the game's (romfs.bin). The game keeps none of them in memory during a quest.
#pragma once
#include <string>
#include <vector>

// Starts reading the names of the game's `language` on a thread of its own, the first time it is asked for: nothing for
// -1 (not known yet) or a language without a folder of texts in the game. Cheap: every frame.
void hunt_names_prepare(int language);

// The name of a monster of `kind` in the game's `language` (UTF-8); empty while they are being read, or with none.
std::string hunt_name(int language, int kind);

// The folder of the game's texts in `language` ("eng", "fre", "spa", "ger", "ita": its table at 0x0105722c), nullptr
// for one the game has none in.
const char* hunt_names_folder(int language);

// (tests) the names of `language` in the files of `game_folder`, read now: false, and why, when they could not be read.
bool hunt_names_read(const std::string& game_folder, int language, std::vector<std::string>* names, std::string* why);
