// The running game's RomFS for the host's own readers (1.0.1.1: the monster names of the health bars, which the game
// keeps in no memory during a quest): the images SelfNCCH gives the game (archive_selfncch.cpp), with the mods that
// are on laid over them (LayeredFS, built once and shared with the game's), whatever the game folder holds: romfs.bin
// and update_romfs.bin, or source.txt and the player's .cci and .cia read in place (the Android app's).
#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "fs_types.h"
#include "mh4n/types.h"

namespace mh4n::fs {

// A file of the game's RomFS.
struct GameRomFsFile {
    std::shared_ptr<FileBackend> image;  // the RomFS (its level 3, from its header) the file is in
    u64 offset = 0, size = 0;            // the file's data in it
    bool update = false;                 // in the update's RomFS (else the game's)
    // Up to `length` bytes at `at` of the file (fewer at its end); the count read. Any thread.
    std::size_t Read(u64 at, void* buffer, std::size_t length) const;
};

// The file at `path` of the game's RomFS ("eng/data/core_common.arc"), as the game finds it: the update's RomFS first,
// then the game's. False, and why, when no game runs or neither has it. Any thread; it reads the RomFS's tables.
bool OpenGameRomFsFile(const std::string& path, GameRomFsFile* file, std::string* why);

}  // namespace mh4n::fs
