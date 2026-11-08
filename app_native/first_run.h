// The first start's options for this phone (android_main.cpp, when the user folder has no mh4u.ini yet), which the MHGU
// port does not do: a weak phone starts in Low-end mode, as the settings' preset sets it (low_end_fps: 60 FPS with a fast
// core, else 30; each can be changed afterwards); the others as the app comes (60 FPS, the phone's own 3D resolution).
// Weak: a weak GPU (the Mali-G52 of a
// Moto g06 and its kind: first_run_weak_gpu) or 4 GB of RAM or less. Only at the first start: an existing player's
// options are never changed, and a key mh4u-app.ini already has stays as it is.
#pragma once
#include <string>

struct FirstRunDevice {
    std::string gpu;     // the Vulkan device's name ("" when none could be made)
    long long ram_mb = 0;
    bool weak = false;
    std::string why;     // in the log's words
};

// The phone, as the first start sees it: its GPU (a Vulkan instance of its own, made and destroyed here) and its RAM.
// MH4U_FIRST_RUN_GPU and MH4U_FIRST_RUN_RAM_MB (launch.txt's env=, a test run) stand in for what the phone reports.
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
