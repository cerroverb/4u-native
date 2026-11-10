// The first start's options for this phone (android_main.cpp, when the user folder has no mh4u.ini yet), which the MHGU
// port does not do: a weak phone starts in Low-end mode, as the settings' preset sets it (low_end_fps: 60 FPS with a fast
// core, else 30; each can be changed afterwards); the others as the app comes (60 FPS, the phone's own 3D resolution).
// Weak: a weak GPU (the Mali-G52 of a
// Moto g06 and its kind: first_run_weak_gpu) or 4 GB of RAM or less. Only at the first start: an existing player's
// options are never changed, and a key mh4u-app.ini already has stays as it is.
#pragma once
#include <cstdint>
#include <string>

struct FirstRunDevice {
    std::string gpu;     // the Vulkan device's name ("" when none could be made)
    long long ram_mb = 0;
    bool weak = false;
    std::string why;     // in the log's words
};

// The phone, as the first start sees it: its GPU (a Vulkan instance of its own, made and destroyed here) and its RAM.
// MH4U_FIRST_RUN_GPU and MH4U_FIRST_RUN_RAM_MB (launch.txt's env=, a test run; '_' for a space in a name) stand in for what
// the phone reports.
FirstRunDevice first_run_probe();
// A GPU too weak for the game at the phone's own resolution and 60 FPS: Mali-G52 and the Bifrost and Midgard ones of
// its rank and below, Adreno 3xx to 5xx and 610 to 613, PowerVR. Not a Mali-G57 (an Anbernic RG406V) or an Adreno 830.
bool first_run_weak_gpu(const std::string& name);
// Whether the phone has a fast core: a CPU part in /proc/cpuinfo that is not one of the small in-order cores (Cortex-A53,
// A55, A35, A510, A520 and Qualcomm's Silver Kryo cores built on them). A phone whose kernel shows no part counts as fast.
// MH4U_FAST_CORES=0|1 (launch.txt's env=, a test run) stands in for the answer. Read once.
bool first_run_fast_cores();

// Writes the first start's mh4u.ini (the port's options) into `ini_path`, and the app's own (mh4u-app.ini, through
// app_settings) for a weak phone: Low-end mode as the settings set it. Logs what it chose and why.
void first_run_write(const std::string& ini_path, const FirstRunDevice& device);

// Every later start: a key the settings panel shows with the app's default (settings_panel.cpp defaults) but that the hooks
// read otherwise when it is missing (fps: the game's own 30; widescreen, kick and target: off) is added to the app's own
// mh4u.ini with the app's default, so that the panel shows what runs. Only a file the app wrote (its first line says so):
// the PC port's mh4u.ini or a test's is left as it is, and a key that is there is never changed.
void first_run_fill(const std::string& ini_path);

// The first Vulkan GPU's name ("" when none), from an instance made for it alone, once (the system's driver: a player's own
// isn't opened here): PowerVR runs without the ubershader. MH4U_GPU_NAME (launch.txt's env=, a test run;
// '_' for a space) stands in for it.
std::string first_run_gpu_name();
// That GPU's Vulkan version (VK_MAKE_API_VERSION; 0 when none). MH4U_GPU_API ("1.1.87", a test run) stands in for it.
uint32_t first_run_gpu_api();

// The Adreno fix ([Graphics] AdrenoFix = auto|off, the settings' "Adreno fix" row). An Adreno 630 on its 2020 system driver
// (Vulkan 1.1.87) drew coloured flashes over the 3D picture below 720 lines, and not at 3x (4u-native #7). Drivers from
// 1.1.128 on (an Adreno 610's and 619's of 2021 and 2023) are left alone: there the floor only cost speed and kept the
// player's 3D resolution from changing (4u-native #26, #27, #28), and those phones played at 1x before it with no bands.
// Applies: an Adreno on a system driver older than 1.1.128, with no driver of the player's own chosen for this start.
bool adreno_fix_applies();
// The --scale the renderer gets for `times` (the 3D resolution's): at least 3 where the fix applies and `on` (auto)
int adreno_fix_scale(int times, bool on);
