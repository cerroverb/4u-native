// MH4U for PC: what the Android app's Java side and its Android-only sources do, on Windows (pc.cmake builds this in their
// place). The phone's own parts have no PC counterpart and do nothing here: the performance hints and the cores' placing
// (perf_hint.c, thread_place.c), a player's own Adreno driver (gpu_driver.cpp), the display's refresh rate
// (display_vsync.cpp) and the presentation's deadline (present_pacing.cpp). Others are PC work still to come (frame
// error screen, rooms on this network); until then they say so in the log and do nothing. Frame generation is the
// Android app's frame_gen.cpp, on the window's desktop OpenGL (pc/compat/GLES3/gl31.h); its cadence comes from here.
#include <SDL3/SDL.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "app_jni.h"
#include "crash_screen.h"
#include "display_vsync.h"
#include "frame_gen.h"
#include "gpu_driver.h"
#include "perf_hint.h"
#include "pc_app.h"
#include "present_pacing.h"
#include "thread_place.h"

#include <windows.h>  // (last: its macros)

// ---- a PC with two GPUs: the drivers' own way to have this program's OpenGL on the dedicated one (the renderer's Vulkan
// takes it too, pc_settings' GPU row aside: the window's GL then imports its pictures with no copy) ----
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

// ---- pc_compat.h -----------------------------------------------------------------------------------
bool pc_replace_file(const char* from, const char* to) {
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

// ---- app_jni.h: GameActivity's methods, the settings' actions ------------------------------------
void app_call_activity(const char* method) {
    if (pc_action(method)) return;
    std::fprintf(stderr, "[app] %s: not on PC yet\n", method != nullptr ? method : "?");
}

// ---- perf_hint.h, thread_place.h: Android's performance hints and the cores' placing --------------
extern "C" {
// (perf_hint_thread, perf_hint_frame: pc_stats.c, the threads the PC readout times)
void perf_hint_set_fps(float) {}
void perf_mem_report(void) {}
void thread_place_init(void) {}
void thread_place_role(int) {}
void thread_place_shown(void) {}
void thread_place_resume(void) {}
}

// ---- gpu_driver.h: a player's own Adreno driver (Android only) -----------------------------------
void gpu_driver_setup() {}
GpuDriverStatus gpu_driver_status() { return GpuDriverStatus{}; }
std::vector<GpuDriver> gpu_driver_list() { return {}; }
void gpu_driver_reload() {}
bool gpu_driver_choose(const std::string&) { return false; }
bool gpu_driver_remove(const std::string&) { return false; }
void gpu_driver_failure_seen() {}
void gpu_driver_import() {}
GpuDriverImport gpu_driver_import_result() { return GpuDriverImport{}; }

// ---- display_vsync.h: the display's refresh rate (Android's frame rate API) ----------------------
void display_vsync_start() {}
void display_set_frame_rate(SDL_Window*, float) {}
void display_frame_rate_refresh() {}

// ---- present_pacing.h: the presentation's deadline (Android's EGL timing): SDL's vsync paces the window here ----------
void present_pacing_before_present(bool, float) {}
void present_pacing_set_deadline_query(std::int64_t (*)()) {}
bool present_pacing_frame_pending() { return false; }
extern "C" int present_pacing_vsyncs(void) { return 1; }
extern "C" int present_pacing_game_vblanks(void) { return 1; }
// (frame generation: the vsyncs a game frame takes, pc_set_cadence from the main loop: VSync on and the FPS limit a whole
// fraction of the display's refresh rate, else 1)
namespace {
std::atomic<int> g_cadence{1};
}
void pc_set_cadence(int n) { g_cadence.store(n < 1 ? 1 : n, std::memory_order_relaxed); }
int present_pacing_cadence() { return g_cadence.load(std::memory_order_relaxed); }
void present_pacing_follow(int) {}

// ---- crash_screen.h: the error screen (the PC's: pc_crash.cpp) -------------------------------------
void crash_screen_install() { pc_crash_install(); }
void crash_screen_device_lost(const char* where) {
    pc_crash_device_lost(where);
}

// (net_lan.h, rooms on this network: pc_net_lan.cpp, with Windows' sockets)
