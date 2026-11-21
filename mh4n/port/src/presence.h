// Where the player is in the game, for the PC app's Discord Rich Presence (the MHGU port's runtime/hle/presence.c): the
// hunter being played (name, Hunter Rank, the weapon equipped) and, in a quest, its map and the hunter's area, the large
// monsters still alive and how many hunters are in it. Read from the game's memory a few times a second (presence.cpp,
// from hooks.cpp's frame step); nothing is written. The app turns it into words (app_native/pc/pc_discord.cpp).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct RCtx;

namespace Port::Presence {

struct Info {
    std::uint32_t serial = 0;  // goes up with each reading (0: none yet)
    bool hunter = false;       // a save is loaded: the fields below hold its hunter
    std::string name;          // the hunter's name (UTF-8)
    int hr = 0;                // Hunter Rank
    int weapon = -1;           // the weapon equipped: the game's weapon numbers (GameState::weapon's), 0 the great
                               // sword ... 14 the charge blade, 4 the heavy bowgun and 6 the light one; -1 none known
    bool quest = false;        // in a quest (sGameControl's mode 1)
    int stage = -1;            // in a quest: its map, the game's stage number (m<nn> of its files; 1 the Ancestral
                               // Steppe), from the area map the top screen draws (hooks.cpp MapLayoutSeen); -1 not known
                               // (no map drawn yet in this quest)
    int area = -1;             // in a quest: the hunter's area of the map (0 the camp)
    int quest_id = 0;          // in a quest: the quest's number (0 not known)
    int hunters = 0;           // in a quest: the hunters in it (this console's own included)
    std::vector<int> monsters; // in a quest: the large monsters alive, their rows of the game's monster table (the
                               // health bars' kinds), each once, in the game's order
    int language = -1;         // the game's language (Port::HuntAids::GameLanguage's numbers)
};

/// The last reading. Any thread.
Info Current();

/// Whether the app wants the readings (the PC app, while its Discord Rich Presence is on): off, as it starts (the
/// Android app never asks), Frame reads nothing. Any thread.
void SetWanted(bool on);

/// hooks.cpp's sMhMain::frame, after the game's frame: a reading every few frames.
void Frame(RCtx* ctx);

/// A new boot: nothing remembered of the last game.
void Reset();

/// presence_trace (tests, the runner's --set or MH4N_PRESENCE_TRACE=1): a line at each change, with the memory the
/// readings come from around them. False for another key.
bool SetOption(const std::string& key, const std::string& value);
void SetLog(void (*log)(const std::string& line));

}  // namespace Port::Presence
