// The touch screen on a panel leased from the compositor (drm-lease-v1), for a session whose compositor shows one
// output only (gamescope's Game Mode on a dual-screen handheld). The lease, the panel's mode, the link of its CRTC and
// its rotation are virtudude's Azahar's (drm_lease_window.cpp, branch drm-leasing) with cerroverb's CRTC link
// (cerroverb/azahar 71a132b3b), the VK_KHR_display surface is Azahar's CreateDrmDisplaySurface (vk_platform.cpp)
// translated to Vulkan's C API, and the pictures are drawn on it with SDL's own Vulkan renderer, which takes an
// instance and a surface made by its caller (SDL_PROP_RENDERER_CREATE_VULKAN_*), through a hidden SDL window.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <string>

#include "drm_lease_touch.h"

namespace drm_lease {

class DrmLeaseClient;

class DrmLeaseOutput {
public:
    DrmLeaseOutput();
    ~DrmLeaseOutput();

    DrmLeaseOutput(const DrmLeaseOutput&) = delete;
    DrmLeaseOutput& operator=(const DrmLeaseOutput&) = delete;

    // Leases the connector (empty: the first internal panel offered when internal_only), reads its mode and
    // rotation (rotation < 0: the panel orientation property's), makes the renderer on it, and starts reading the
    // touch device ("auto": the only direct-touch device, or the one named like the bottom screen) into `touch`.
    // `rotation` is in Azahar's sense (counter-clockwise degrees, as MH4U_DRM_LEASE_ROTATION and AZAHAR_DRM_LEASE_ROTATION).
    // False, with everything let go, when the compositor offers no lease or any step fails.
    bool Initialize(const std::string& connector_name, int rotation, const std::string& touch_device,
                    bool internal_only, TouchSink touch);

    // The compositor's events (a lease it ends); call regularly.
    void PumpEvents();

    SDL_Window* Window() const {
        return window;
    }
    SDL_Renderer* Renderer() const {
        return renderer;
    }
    // Clockwise degrees (0, 90, 180, 270) the picture is turned to stand upright on the panel.
    int Rotation() const {
        return rotation;
    }

private:
    std::unique_ptr<DrmLeaseClient> client;
    std::unique_ptr<DrmLeaseTouch> touch;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    void* instance = nullptr;  // VkInstance
    uint64_t surface = 0;      // VkSurfaceKHR
    int rotation = 0;
};

}  // namespace drm_lease
