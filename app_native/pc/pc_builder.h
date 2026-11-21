// MH4U for PC: the game's code library made inside MH4U.exe (pc_builder.cpp), the Windows counterpart of the Android
// app's on-phone builder (BuilderService, BuilderTools) and the same as MHGU for PC's (runtime/pc_builder.c). The game
// folder screen (pc_launcher.cpp) shows it and works its buttons; F1, System, Game code, Build the game code opens that
// screen again (the program starts over into it).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// What a build is doing (pcb_snapshot copies it)
enum { PCB_IDLE, PCB_RUNNING, PCB_DONE, PCB_FAILED, PCB_CANCELLED };
// The stages, in order (tools/pc_build.py's names; "tools" is the downloads)
constexpr int PCB_STAGES = 5;
extern const char* const pcb_stage_names[PCB_STAGES];  // tools, extract, recompile, compile, install

struct PcbJob {
    int status = PCB_IDLE;  // PCB_*
    int stage = 0;          // 0..PCB_STAGES-1
    char detail[32] = "";   // "download:python@python.org", "unpack:zig", the build's own stage name
    int64_t done = 0, total = 0;  // how far the stage is (bytes, functions, files)
    int64_t started_ms = 0, stage_started_ms = 0, ended_ms = 0;  // (SDL_GetTicks)
    char error_stage[16] = "";  // the failure's kind: space, download, tools, dest, extract, version, mods, recompile,
                                // compile, link, install, memory, start
    char error_text[400] = "";
    char folder[1100] = "";  // the game folder of the run (UTF-8)
    int jobs = 0;            // compilers at once
};

// The tools' state and the room they need (bytes)
struct PcbInfo {
    int64_t to_download = 0;  // what the downloads still take (0: both in place)
    int64_t installed = 0;    // what the tools take once unpacked (Python and Zig)
    int64_t used = 0;         // what the builder's folder takes now (tools, work folder, downloads)
    int64_t needed = 0;       // free space a run needs now (the tools still missing, the work folder)
    int64_t free = 0;         // free space where the builder works
    bool half_done = false;   // something to Start over from: a work folder, a run cut, failed or cancelled
    bool interrupted = false; // a run was going on when the program closed (state.txt)
    bool scripts_ok = false;  // builder_tools.zip and zigcc.exe are beside MH4U.exe
    char where[600] = "";     // the builder's folder (UTF-8), for the texts
};

// The builder's folder: builder\ in the program's home (beside the settings). Once, on the main thread, after the
// program's folders are known (pc_paths), before anything else here.
void pcb_init();
void pcb_snapshot(PcbJob* out);
// The build's last lines (not its @@ ones), newest last: up to max, each up to 160 bytes; how many
int pcb_tail(char (*lines)[160], int max);
void pcb_info(PcbInfo* out);
// The code mods a build can take (Android's BuilderActivity, the same choices in the same order): the game folder's mods/
// (what the program copies to its user folder at each start), the code mods in the user folder's load/mods (those of the
// game folder and those added in the settings, as they are on), or none (always last). `what`: the mods' names.
struct PcbMods {
    std::string flag, path;  // pc_build.py's --game-mods FOLDER or --user-mods USER; "" none
    std::string what;        // the mods' names ("" none)
};
std::vector<PcbMods> pcb_mod_choices(const std::string& folder, const std::string& user);
// A build for the game folder (UTF-8): the game, its update as pc_look_at_folder found them, and the code mods chosen.
// False when one runs already or the folder is missing. It goes on in the background: pcb_snapshot says how.
bool pcb_start(const std::string& folder, const std::string& base, const std::string& update, const PcbMods& mods);
void pcb_cancel();
// Start over (as Android's): the work folder (and, with tools, the downloaded Python and Zig) set aside in one step and
// deleted on a thread of its own; state.txt goes. Never while a build runs (false). The game folder is not touched.
bool pcb_start_over(bool tools);
// Delete the tools and anything left of a build (the screen's "Delete the tools"): never while a build runs
bool pcb_delete_tools();
bool pcb_running();
// The library id this program wants (MH4U_PC_CODE_ID)
const char* pcb_wanted_id();
