// The Vulkan driver a host chooses. Without hooks the backend loads Vulkan through the system's loader, as it always
// did. A host that opened a driver of its own (the Android app: a player's GPU driver through libadrenotools,
// app_native/gpu_driver.cpp) sets the hooks before the first CreateInstance:
//   - loader: asked by each CreateInstance; the vkGetInstanceProcAddr to load Vulkan from (plume patch 14), or null
//     for the system's. An instance that can't be made with it, or that lists no GPU, is made again with the
//     system's loader, after `failed` has been told why: a broken driver doesn't stop the game.
//   - device_made: told by each CreateBackend, once its device is made, which GPU and which driver run.
//   - device_lost: told once, on whichever thread saw it, that the device is lost (VK_ERROR_DEVICE_LOST from a submission,
//     a fence or a wait) and where: nothing the renderer submits runs any more, and a wait for it would never end. A
//     host with no hook keeps going (the waits return at once from then on).
// Process-wide; the hooks are called on the thread that makes the instance or the device.
#pragma once

// VK_KHR_portability_subset lives among the beta declarations; the backend's own headers define this too.
#ifndef VK_ENABLE_BETA_EXTENSIONS
#define VK_ENABLE_BETA_EXTENSIONS
#endif

#include <volk.h>

#include <functional>
#include <string>

#include "mh4n/types.h"

namespace mh4n::gpu::rhi {

// The device a backend was made on, and its driver as the driver itself tells it.
struct MadeDevice {
    std::string device_name;  // VkPhysicalDeviceProperties::deviceName ("Adreno (TM) 830")
    std::string driver_name;  // VkPhysicalDeviceDriverProperties (empty without Vulkan 1.2 or VK_KHR_driver_properties)
    std::string driver_info;  //   its driverInfo (a build, a date, a Mesa version)
    u32 vendor_id = 0;
    u32 driver_id = 0;
    u32 api_version = 0;
    u32 driver_version = 0;   // VkPhysicalDeviceProperties::driverVersion, in the vendor's own encoding
    // (Windows) VkPhysicalDeviceIDProperties::deviceLUID, the adapter's in DXGI and D3DKMT: the PC app's readout opens it
    u8 luid[8] = {};
    bool luid_valid = false;
};

struct DriverHooks {
    std::function<PFN_vkGetInstanceProcAddr()> loader;
    std::function<void(const std::string& why)> failed;
    std::function<void(const MadeDevice&)> device_made;
    std::function<void(const std::string& where)> device_lost;
};

// Replaces the hooks (empty ones: the system's loader, nobody told).
void SetDriverHooks(DriverHooks hooks);

namespace core {
// For instance.cpp and device.cpp: the hooks' calls (each does nothing without its hook).
PFN_vkGetInstanceProcAddr HostLoader();
void HostLoaderFailed(const std::string& why);
void HostDeviceMade(const MadeDevice& device);
void HostDeviceLost(const std::string& where);
}  // namespace core

}  // namespace mh4n::gpu::rhi
