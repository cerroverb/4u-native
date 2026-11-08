// The player's own dump read as it is, nothing extracted (the Android app): a decrypted .cci/.3ds (NCSD),
// .cxi (NCCH) or .cia (its content 0). Written for the Android port, after tools/extract_ncch.py and
// tools/blz.py (this repository's own tools).
//
// A game folder may hold `source.txt` instead of romfs.bin and update_romfs.bin:
//     base=<the base game's .cci, .3ds or .cxi>
//     update=<the update's .cia (or .cxi)>
// Both RomFS are then windows into those files (FsState). PrepareGameFolder writes such a folder: the small
// files the loader reads (exheader.bin, code.bin decompressed, icon.bin), info.txt and source.txt.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "positioned_file.h"

namespace mh4n::fs::dump {

struct Ncch {
    u64 base = 0;  // where the NCCH starts in the file
    u64 program_id = 0;
    u32 exheader_size = 0;
    u64 exefs_offset = 0, exefs_size = 0;  // relative to base
    u64 romfs_offset = 0, romfs_size = 0;
    bool no_crypto = false;
    u64 cia_title_id = 0;      // a .cia's TMD: its title id and version (0 for other files)
    u16 cia_title_version = 0;
};

// The game's NCCH in `file` (an NCSD's partition 0, a CIA's content 0, or an NCCH at 0).
bool FindNcch(const PositionedFile& file, Ncch* out, std::string* error);
// An ExeFS file ("icon", ".code") of that NCCH.
bool ReadExeFsFile(const PositionedFile& file, const Ncch& ncch, const char* name, std::vector<u8>* out,
                   std::string* error);
// Backward LZ77 (the 3DS's ExeFS .code), in place.
bool BlzDecompress(std::vector<u8>& data, std::string* error);
// The RomFS of the game's NCCH in `path`, as a file of its own.
std::shared_ptr<PositionedFile> OpenRomFs(const std::string& path, std::string* error);

// Writes the game folder for `base` (the game) and `update` (its update) into `folder`; false and a message
// a player can act on otherwise (an encrypted dump, the wrong title, a file cut short).
bool PrepareGameFolder(const std::string& base, const std::string& update, const std::string& folder,
                       std::string* error);

}  // namespace mh4n::fs::dump
