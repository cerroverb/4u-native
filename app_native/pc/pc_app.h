// MH4U for PC: the PC program's own parts (pc/*.cpp), between its main (pc_main.cpp), the game folder screen
// (pc_launcher.cpp) and what the Android app's Java side does (pc_platform.cpp).
//
// The program's folders. Its home, MH4U_HOME or else the folder MH4U.exe is in (as MHGU for PC's), holds what Android
// keeps in the app's external folder: mh4u-app.ini (the app's settings), log.txt (and the two before it), game_folder.txt
// (the game folder chosen), game/ (the small files read from the game and its update: exheader, code, icon, source.txt;
// the RomFS is read in place from the player's files) and user/ (mh4u.ini, the system's data, the caches, the DLC
// imported). The save (the console's SD card, sdmc/) is in the save folder: the game's own, %APPDATA%\MH4U (the player's
// own folder whatever folder MH4U.exe is unpacked to, as MHGU for PC's %APPDATA%\MHGURecomp), or the one chosen in the
// settings (Save folder: save_folder.txt in the home names it, as on Android); the saves an import or a change of save
// folder replaces are kept in %APPDATA%\MH4U\save_backups. Tests: with MH4U_HOME the game's own save folder is
// <home>/user and the backups <home>/save_backups; MH4U_SAVE_FOLDER stands in for a folder chosen.
#pragma once

#include <string>
#include <utility>
#include <vector>

struct PcPaths {
    std::string exe_dir;      // MH4U.exe's folder (UTF-8, no slash at the end)
    std::string home;         // the program's own files (above)
    std::string game_files;   // <home>/game
    std::string user;         // <home>/user
    std::string save_folder;  // the folder that holds sdmc/ (the game's own or the one chosen)
    std::string own_saves;    // the game's own save folder (%APPDATA%/MH4U)
    std::string backups;      // save_backups (in the game's own save folder)
};
const PcPaths& pc_paths();
// The save folder read again (after a change of save folder was applied: pc_files.cpp)
void pc_paths_reload_saves();
// The game folder the game runs from (UTF-8; "" before one is chosen)
std::string pc_game_folder();

// The code library this program runs with (pc.cmake's MH4U_PC_CODE_ID: mh4n tools/build_game.py --print-id --pc)
constexpr const char* kPcLibrary = "mh4u_code_native.dll";
constexpr const char* kPcLibraryId = "mh4u_code_native.id";

// A game folder as the player made it: the game (.3ds, .cci or .cxi) and its 1.3.0 update (.cia), both decrypted, the
// code library (mh4u_code_native.dll and its .id) and, if wanted, mods/ and textures/. What is in it, and what is wrong.
struct PcGameFolder {
    std::string folder;              // UTF-8, as chosen
    std::string base, update;        // the game and its update ("" when missing)
    std::string library;             // mh4u_code_native.dll ("" when missing)
    std::string library_id;          // its id (the one inside it, else the .id file's; "" none)
    std::string problem;             // what keeps it from being played, in the player's language ("" none)
    bool library_problem = false;    // it is only the code library: missing, or made with other tools
};
// MH4U_CODE_LIBRARY=<a mh4u_code_native.dll elsewhere> (a test) stands in for a library the folder doesn't have.
PcGameFolder pc_look_at_folder(const std::string& folder, bool spanish);
// The id a code library carries inside itself (the exported rc_library_id that tools/game_version.py writes, the same as
// its .id file's), read from the DLL's export table without loading it; "" for one made before the tools wrote it.
std::string pc_library_id_inside(const std::string& dll);

// The game folder screen (Android's GameFolderActivity, as MHGU for PC's launcher): a window of its own before the game's.
// `current`: the folder known so far ("" none) as pc_look_at_folder saw it. Returns the folder chosen, playable, or ""
// when the player left. MH4U_LAUNCHER_PICK=<folder> (a test) answers the folder dialog; MH4U_LAUNCHER_SHOT=<file.png>
// saves the screen once a folder is checked, MH4U_LAUNCHER_PLAY=1 then plays it (or leaves when it can't be played), =0
// leaves. `open_builder`: straight on the builder's page (F1, System, Game code, Build the game code: --builder).
// MH4U_LAUNCHER_SCRIPT (a test) works the screen and the builder's page as a player would (pc_launcher.cpp ScriptSteps).
std::string pc_launcher(const PcGameFolder& current, bool spanish, bool open_builder = false);

// The settings' actions GameActivity does on Android (app_jni.h's app_call_activity): true when the PC did it.
bool pc_action(const char* method);
// Those that are files and folders (pc_files.cpp: the log's folder, the saves, mods and cheats, DLC and event quests, the
// licenses, the textures' folder): true when it is one of them
bool pc_files_action(const std::string& method);
// Before the game starts, with its files ready (pc_files.cpp): a change of save folder, a save and event quests imported
// (each waits for the next start, as on Android), and the game folder's mods/ into the user folder. What the player has to
// be told: {title, text} pairs.
std::vector<std::pair<std::string, std::string>> pc_files_before_game(const std::string& game_folder);
// The update notice, as Android's (pc_updates.cpp): at the start, GitHub's latest release asked at most once a day on a
// thread of its own ([Interface] Updates); before the game, a newer one said at most once a day (View opens its page)
void pc_updates_check(const std::string& home);
void pc_update_notice(bool spanish);
// The game folder screen was asked for from the settings (Game folder): the game closes and the program starts again on it
bool pc_restart_requested();
// Frame generation's cadence (pc_platform.cpp's present_pacing_cadence): the vsyncs a game frame takes, set each frame by
// the main loop (1: none made)
void pc_set_cadence(int n);

// The error screen when the game crashes (pc_crash.cpp; crash_screen.h's on the PC): the handlers (crash_screen_install),
// a lost GPU device (crash_screen_device_lost), the game's own end with an error (pc_crash_open), each opening the screen
// as MH4U.exe --crashed=<why>, which pc_crash_screen shows for the session's log.txt
void pc_crash_install();
void pc_crash_device_lost(const char* where);
void pc_crash_open(const char* why);
int pc_crash_screen(const std::string& why, bool spanish);
// A message in the game folder screen's look (pc_launcher.cpp, the error screen's): the title in the band, the text, and
// buttons: `wide` each on a row of its own, `pair` side by side at the bottom. The one chosen (wide's in order, then
// pair's), -1 when the window was closed. shot (a test): the screen saved to that PNG after a few frames, then -1.
int pc_message_screen(const std::string& title, const std::string& text, const std::vector<std::string>& wide,
                      const std::vector<std::string>& pair, bool spanish, const char* shot = nullptr);
