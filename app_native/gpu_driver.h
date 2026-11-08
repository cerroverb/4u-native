// The GPU driver (Settings, Graphics & Audio, GPU driver): the system's Vulkan driver, as always, unless the player
// chose one of their own for an Adreno GPU (Turnip, or another build), at their own risk. As the MHGU port does it
// (its runtime/platform/android_gpu.c and DriverActivity.java):
//   - a driver comes as a .zip with meta.json and the library, the format the emulators' driver packages use;
//     DriverActivity.java unpacks it into files/gpu_drivers/<folder>/ (and writes import.txt with the outcome)
//   - the runtime loads Vulkan through libadrenotools (third_party/libadrenotools: a private copy of the system's
//     libvulkan.so, hooked to open that driver) when one is chosen: mh4n's gpu/backend/driver_hooks.h
//   - a driver that does not work must not leave the game unable to start: a trial file is written before it is
//     opened and removed once the game has shown frames drawn with it; found at the next start, that run never got
//     there, so the choice moves to `failed` and the system's driver is used. An instance it can't make, or a
//     device with no library of its folder loaded (one that couldn't be opened leaves the system's driver in its
//     place, silently), gives it up at once
// files/gpu_drivers/: <folder>/{driver.txt, <library>.so...}, selected (its folder, then its library), failed (the name
// of the one that did not work), trial, import.txt
#pragma once
#include <string>
#include <vector>

// android_main.cpp, before the game starts: reads the choice and hands the runtime its hooks.
void gpu_driver_setup();

struct GpuDriver {
    std::string folder;   // under files/gpu_drivers
    std::string name;     // as its meta.json names it, with its version
    std::string library;  // the driver's own library in the folder
    std::string about;    // meta.json's description and author, if any
};

struct GpuDriverStatus {
    bool adreno = false;         // an Adreno GPU: a driver of one's own can be chosen
    bool made = false;           // the game's GPU device is made (gpu, driver and running are known)
    std::string gpu;             // the device's name
    std::string driver;          // the driver that runs, as it calls itself (its name, its info, the Vulkan version)
    std::string running;         // the player's driver that runs: its name; empty: the system's
    std::string running_folder;  //   its folder
    std::string chosen;          // the folder chosen for the next start (empty: the system's)
    std::string failed;          // the player's driver that did not work, at this start or the last one (its name)
};
GpuDriverStatus gpu_driver_status();

// The installed drivers, as last read (gpu_driver_reload reads the folder again)
std::vector<GpuDriver> gpu_driver_list();
void gpu_driver_reload();

// The driver for the next start: a folder of the list, or "" for the system's. False when it can't be written.
bool gpu_driver_choose(const std::string& folder);
// Deletes an installed driver (and the choice of it).
bool gpu_driver_remove(const std::string& folder);
// The failure was shown to the player: not again at the next start.
void gpu_driver_failure_seen();

// Import a driver: the phone's file picker for its .zip (GameActivity.showDriverImport); its outcome comes once, through
// gpu_driver_import_result (done false while none has come).
void gpu_driver_import();
struct GpuDriverImport {
    bool done = false, ok = false;
    std::string folder, name;  // the driver installed
    std::string error;         // why not: no_library, not_arm64, too_large or unreadable
    std::string detail;        //   and the system's own words, if any
};
GpuDriverImport gpu_driver_import_result();
