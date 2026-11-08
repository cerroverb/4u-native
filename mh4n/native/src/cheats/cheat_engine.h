// Derived from Azahar src/core/cheats/cheats.h and src/core/cheats/cheats.cpp at
// 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-2.0-or-later: both say "GPLv2 or any later version"): the
// cheat engine's list and its calls (GetCheats, AddCheat, RemoveCheat, UpdateCheat, LoadCheatFile,
// SaveCheatFile), the cheats file of a title (`cheats/<title id, 16 hex digits>.txt` in the user folder),
// and running the cheats that are on, in order, under a shared lock.
// Copyright 2018-2025 Citra Emulator Project / Azahar Emulator Project (cheats.h)
// Copyright 2018-2026 Citra Emulator Project / Azahar Emulator Project (cheats.cpp)
//
// The cheat engine (Gateway codes, gateway_cheat.h) of the running game. The boot step `started.cheats`
// (cheats_step.cpp) makes one per run, loads the title's cheats file and runs the cheats that are on once
// per game frame (Runtime::OnFrame), on the game's thread at a safe point: Azahar runs them on a timer
// event of its CPU thread, every 50,000,000 ticks (Luma3DS's interval), the port at every new frame.
//
// The app's settings change the list while the game runs (Current): a cheat turned on or off counts from
// the next frame, and Save writes the file in Azahar's format (`*citra_enabled` marks a cheat that is on),
// which Azahar reads as it is.
//
// Threads: every call may come from any thread. Run takes the list's lock shared, the others exclusive,
// for no longer than a copy of the list (Save and Import read and write their files outside it).
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include "cheats/gateway_cheat.h"
#include "mh4n/types.h"

namespace mh4n::cheats {

// <user>/cheats/<title id>.txt (Azahar's FileUtil::UserPath::CheatsDir, "{:016X}.txt").
std::string CheatFilePath(const std::string& user_folder, u64 title_id);

// What the app shows of a cheat.
struct CheatInfo {
    std::string name, comments, code;
    bool enabled = false;
    std::size_t invalid_lines = 0;  // lines that are no codes (they do nothing)
    bool touches_code = false;      // it writes into the game's code, which the port runs recompiled: no effect
};

class CheatEngine {
public:
    // `path`: the file Load reads and Save writes (empty: none, the list starts empty and is not kept).
    explicit CheatEngine(std::string path = {});

    const std::string& Path() const { return path_; }

    // The file's cheats replace the list (a file that does not exist gives an empty list). The number
    // read.
    std::size_t Load();
    // The list to the file, in Azahar's format; false (and *error) when it can't be written.
    bool Save(std::string* error = nullptr) const;

    std::vector<CheatInfo> List() const;
    std::size_t Count() const;
    std::size_t EnabledCount() const;
    // Changes when the list or a cheat's state changes (the app redraws its rows).
    u64 Revision() const;

    bool SetEnabled(std::size_t index, bool enabled);
    // The first cheat named `name`; false if there is none.
    bool SetEnabled(const std::string& name, bool enabled);
    bool Remove(std::size_t index);
    // The cheats of a file in the same format, added off: one whose name is already in the list takes its
    // place (and is off), the others go at the end; lines before any [Name] are left out. The number added or
    // replaced; -1 (and *error: unreadable, no_cheats) when the file can't be read or holds no cheat.
    int Import(const std::string& path, std::string* error = nullptr);

    // The game's code [begin, end): what TouchesCode compares with (CheatInfo::touches_code).
    void SetCodeRange(u32 begin, u32 end);

    // Each cheat that is on, once, in order (the game's thread).
    void Run(CheatTarget& target) const;

private:
    void Changed() { ++revision_; }

    const std::string path_;
    std::vector<std::shared_ptr<GatewayCheat>> cheats_list_;
    mutable std::shared_mutex cheats_list_mutex_;
    std::atomic<u64> revision_{1};
    std::atomic<u32> code_begin_{0}, code_end_{0};
};

// The engine of the game that runs now (the boot step sets it, the end of the run clears it), or null.
std::shared_ptr<CheatEngine> Current();
void SetCurrent(std::shared_ptr<CheatEngine> engine);

}  // namespace mh4n::cheats
