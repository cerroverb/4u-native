// The mods of the user folder (<user>/load/mods/), several at once, in an order the player picks: what the
// loader (the code mods) and the SelfNCCH archive (LayeredFS) apply, and what the app's Mods page shows and
// changes. Azahar reads one mods folder per title, <user>/load/mods/<title id>/; here every folder in
// load/mods is a mod:
//
//   load/mods/<name>/romfs/        files laid over the game's RomFS (LayeredFS)
//   load/mods/<name>/romfs_ext/    .stub, .ips and .bps over the game's RomFS files
//   load/mods/<name>/exefs/code.ips (code.bps, code.bin; also without exefs/): a code mod, applied to the
//                                   game's code at load. The code library is recompiled from the code,
//                                   so it must be made with the same code mods (tools/build_game.py --mods):
//                                   with others the runtime refuses it and the game does not start.
//   load/mods/0004000000126300/    Azahar's own layout: a mod like the others (named by the title id)
//
// A folder named as another title's id (16 hex digits) is not this game's and is left out; so is one whose
// name starts with '.' or '_'. load/mods/load_order.txt orders them and turns them off:
//
//   # Written by the Mods page ...        (comments)
//   SomeMod                               on, loaded first
//   # off: OtherMod                       off, keeping its place
//   0004000000126300                      on, loaded after SomeMod: where both change a file, this one wins
//
// A folder the list does not name loads after the listed ones, on, by name; a name without a folder is
// dropped. Without the file every folder is on, by name. load/mods/.from_game_folder names the mods the app
// copied from the mods folder beside the game (GameFolderActivity.syncMods), one per line.
#pragma once

#include <string>
#include <vector>

#include "../code_patch/code_patch.h"
#include "mh4n/types.h"

namespace mh4n::mods {

inline constexpr const char* kOrderFile = "load_order.txt";
inline constexpr const char* kGameFolderList = ".from_game_folder";
// The mods of the last start the code library took (load_order.txt's format, every mod named): written at each start
// that gets that far, for the app's error screen to go back to when a change of the code mods keeps the game from
// starting.
inline constexpr const char* kLastGoodFile = "load_order.ok";

struct Mod {
    std::string name;  // its folder under load/mods
    std::string path;  // the folder, with a trailing '/'
    bool on = true;
    bool romfs = false;            // romfs/ or romfs_ext/
    bool code = false;             // a code replacement or patch: the code library must be made with it
    bool from_game_folder = false;  // copied from the mods folder beside the game (the app replaces it at start)
};

// <user>/load/mods/
std::string Folder(const std::string& user_folder);

// Every mod of `program_id`'s title in `folder` (Folder()), in load order, as the list orders them.
std::vector<Mod> List(const std::string& folder, u64 program_id);
// The ones that are on, in load order.
std::vector<Mod> Enabled(const std::string& folder, u64 program_id);
// Writes load_order.txt (or `file`) for `mods`, in that order (the ones off as "# off: name"). False (and
// *error) when it can't be written.
bool SaveOrder(const std::string& folder, const std::vector<Mod>& mods, std::string* error = nullptr,
               const char* file = kOrderFile);

// The game's code with the code mods of `mods` that are on applied, in order (code_patch::ApplyFolders).
code_patch::Outcome ApplyCode(const std::vector<Mod>& mods, std::vector<u8>& code, u32 bss_pages);
// The LayeredFS layers of the mods that are on, in order: each one's romfs/ and romfs_ext/.
struct Layer {
    std::string romfs, romfs_ext;
};
std::vector<Layer> RomFsLayers(const std::vector<Mod>& mods);

// ---- the code library and the code mods --------------------------------------------------------------

// The code library the game runs (CodeLibrary's TextBase, TextSize and TextCrc32), recorded at the start.
struct LibraryId {
    bool known = false;
    u32 text_base = 0, text_size = 0, text_crc32 = 0;
};
LibraryId RunningLibrary();
// The game folder the running game was loaded from, and its program id (empty, 0: none runs).
std::string RunningGameFolder();
u64 RunningProgramId();

// Would `library` take the game's code (game_folder's exheader.bin and code.bin) with the code mods of
// `mods` that are on? Reads the files: off the interface's thread. `error`: why it can't be told (a file,
// a patch that does not apply); then `matches` is false.
struct CodeCheck {
    bool matches = false;
    std::string error;
};
CodeCheck CheckCode(const std::string& game_folder, const std::vector<Mod>& mods, const LibraryId& library);

}  // namespace mh4n::mods
