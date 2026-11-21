// Derived from Azahar src/core/hle/service/am/am.cpp, src/core/file_sys/title_metadata.cpp,
// src/core/file_sys/title_metadata.h, src/core/file_sys/signature.h and src/core/file_sys/ncch_container.cpp at
// 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-3.0-or-later: am.cpp, title_metadata.cpp and ncch_container.cpp say
// "GPLv3 or any later version"; title_metadata.h and signature.h say "GPLv2 or any later version"): where an
// installed title lives (GetMediaTitlePath and GetTitlePath: sdmc/Nintendo 3DS/<SYSTEM_ID>/<SDCARD_ID>/title/<high>/<low>/
// on the SD card, nand/<SYSTEM_ID>/title/<high>/<low>/ on the NAND), which title metadata is the title's
// (GetTitleMetadataPath: the lowest-numbered <8 hex digits>.tmd of content/), where a content is
// (GetTitleContentPath: content/<content id>.app, or content/00000000/<content id>.app when the title has more than one
// content and content 1 is optional, as DLC has), the layout of the title metadata (the signature types and sizes,
// the header's title id, type, version and content count, the content chunk records) and which NCCH can be read
// (NCCHContainer::Load: an encrypted NCCH is not supported; the RomFS is read from 0x1000 bytes into its section).
// Copyright 2015-2026 Citra Emulator Project / Azahar Emulator Project (am.cpp)
// Copyright 2017-2026 Citra Emulator Project / Azahar Emulator Project (title_metadata.cpp, ncch_container.cpp)
// Copyright 2017-2025 Citra Emulator Project / Azahar Emulator Project (title_metadata.h)
// Copyright 2024 Azahar Emulator Project (signature.h)
//
// Add-on content (DLC) titles the player brings (1.0.1). MH4U asks am:app which contents of its add-on content title
// (0004008C00126300 for the USA game) are owned and on the SD card, and reads each one's RomFS through the NCCH
// archive (0x2345678A, the title id and the SD card's media type, the content index). The runtime used to answer as
// a console with no add-on content. Now the titles found in the player's folders, in Azahar's layout, are answered as
// Azahar answers for an installed title:
//   - the SD card: the save folder (--saves) and the user folder's sdmc/, in that order;
//   - the NAND: the user folder's nand/;
//   - --dlc-folder DIR (repeatable): an Azahar user folder (DIR/sdmc/..., DIR/nand/...) or its sdmc/ folder,
//     read in place.
// The titles are looked up once, when the file system starts, and logged (one line per title, and why a content
// can't be read). Nothing is written.
//
// Departures from Azahar:
//   - a content is found by the index its chunk record holds, as the console's AM does; Azahar takes the request's
//     index as a position in the title metadata (the same for title metadata whose indices run 0, 1, 2..., as DLC's do);
//   - a content counts as downloaded (OWNERSHIP_DOWNLOADED) only when the runtime can read it: an .app that is
//     encrypted, compressed by Azahar's "compress installed CIA content" option or not an NCCH is logged and left
//     out, so the game skips it instead of failing to read it. Azahar marks any .app that exists;
//   - the .app is also looked for in the other of the two places (content/ and content/00000000/), for a folder
//     copied by hand;
//   - no ticket is read: every content counts as owned, which is Azahar's answer for a title whose ticket is not
//     installed ("to keep compatibility with older emulator builds").
#pragma once

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "mh4n/types.h"

namespace mh4n::fs {

struct FsState;

inline constexpr u32 kTidHighDlc = 0x0004008C;

// Media types (FS's MediaType) the titles live on.
inline constexpr u32 kDlcMediaNand = 0;
inline constexpr u32 kDlcMediaSdmc = 1;

// What the runtime can do with one content of a title.
enum class DlcContentState {
    Readable,     // a decrypted NCCH with a RomFS
    Missing,      // its .app is not there (a title installed in part)
    Encrypted,    // an NCCH that is still encrypted
    Compressed,   // a Z3DS file (Azahar's "compress installed CIA content")
    NotNcch,      // something else: an .app Azahar encrypted for its own console, a damaged copy
    NoRomFs,      // an NCCH without a RomFS
    CutShort,     // the file ends before its RomFS does
};
const char* DlcContentStateText(DlcContentState state);

struct DlcContent {
    u32 id = 0;      // the content id: <id>.app
    u16 index = 0;   // the content index the game asks for
    u16 type = 0;    // the chunk record's type (bit 0 encrypted, bit 14 optional)
    u64 size = 0;    // the size the title metadata gives
    std::string path;  // the .app (where it would be, if Missing)
    DlcContentState state = DlcContentState::Missing;
    u64 romfs_offset = 0;  // the readable RomFS in the .app: past the section's 0x1000-byte IVFC header
    u64 romfs_size = 0;
};

struct DlcTitle {
    u64 title_id = 0;
    u32 media = kDlcMediaSdmc;
    u16 version = 0;
    u32 title_type = 0;
    std::string folder;    // .../title/0004008c/<low>/
    std::string tmd_path;
    std::vector<DlcContent> contents;  // in the title metadata's order

    // The content whose chunk record holds `index`; null if the title has none.
    const DlcContent* Content(u16 index) const;
    std::size_t Readable() const;
};

class DlcTitles {
public:
    // Looks the titles up (FsState::Init calls it once, with the folders it knows) and logs what it found.
    void Scan(const FsState& state, const std::vector<std::string>& extra_folders);

    // The title of that id on that media type; null if none was found there.
    const DlcTitle* Find(u32 media, u64 title_id) const;
    const std::vector<DlcTitle>& All() const { return titles_; }
    // True the first time it is called for that content (what is logged once per content). Scheduler thread.
    bool FirstAsk(u64 title_id, u32 content_index) { return asked_.insert({title_id, content_index}).second; }

    // For tests: the folders of a user folder's tree that are looked at, in order.
    static std::vector<std::pair<u32, std::string>> TitleRoots(const FsState& state,
                                                               const std::vector<std::string>& extra_folders);

private:
    std::vector<DlcTitle> titles_;
    std::set<std::pair<u64, u32>> asked_;
};

// Parses a title metadata file (the bytes of a .tmd, or of a .cia's TMD section). False and a reason if it is not one.
bool ParseTitleMetadata(const std::vector<u8>& bytes, DlcTitle* title, std::string* error);

// Reads one title folder (.../title/0004008c/<low>/): its title metadata and the state of each content. False and a
// reason if it has no title metadata that can be read.
bool ReadDlcTitle(const std::string& folder, u64 title_id, u32 media, DlcTitle* title, std::string* error);

// ---- the installed DLC in every save (1.0.1, the hooks' dlc_flags: port/src/hooks.cpp DlcFlagsStep) ----

// The game's own add-on content title (0004008C and the game's low id; any add-on title while the game's id is not
// known) was found with at least one content the runtime can read.
bool HasReadableGameDlc(const FsState& state);

// The folder the app keeps its save backups in: save_backups/ beside the user folder (the app's user/ and
// save_backups/ share a folder), with its slash; empty without a user folder. MH4N_SAVE_BACKUPS names another one (the
// PC's, in its save folder: app_native/pc/pc_main.cpp).
std::string SaveBackupsFolder(const FsState& state);

// Copies the game's save, before the hooks first change it, the way the app's own backups are laid out
// (android/.../SaveFolder.java backup): SaveBackupsFolder()/<yyyy-MM-dd_HH-mm-ss>-<what>/<low>/ with data/ (the save
// data folder), 00000001.metadata and extdata/ (the game's extdata 00000000/0000<low / 0x100>/), each one that is
// there. The copy's folder; empty, and *error says why, when there is no save to copy or it could not be copied whole
// (the part copied is then removed).
std::string BackUpGameSave(const FsState& state, const std::string& what, std::string* error);

}  // namespace mh4n::fs
