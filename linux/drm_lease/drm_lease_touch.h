// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.
//
// From virtudude's Azahar, branch drm-leasing, commit d16299140 (src/citra_qt/drm_lease/drm_lease_touch.h): the
// same class, giving the touch to a callback (0..1 in the panel's axes) instead of an EmuWindow, which mh4u-linux
// has not; the environment variables are named MH4U_ instead of AZAHAR_.

#pragma once

#include <atomic>
#include <functional>
#include <utility>
#include <optional>
#include <string>
#include <thread>

namespace drm_lease {

/// A touch point, 0..1 along the panel's own x and y (its scanout axes), or nullopt when the finger is lifted.
/// Returns whether a new touch landed on the screen (as EmuWindow::TouchPressed does); ignored for moves and lifts.
using TouchSink = std::function<bool(std::optional<std::pair<float, float>>)>;

/// Reads a touchscreen evdev device directly and feeds it to a TouchSink as
/// touch input on the panel. Used for screens driven through a DRM lease,
/// where no windowing system delivers input for the panel. The device is
/// grabbed (EVIOCGRAB) so the compositor stops seeing its events.
class DrmLeaseTouch {
public:
    /// device_path: an evdev node path, an evdev device name, or "auto" to
    /// pick the only direct-touch device (preferring one named like the
    /// bottom screen when several exist).
    DrmLeaseTouch(TouchSink sink, std::string device_path);
    ~DrmLeaseTouch();

    DrmLeaseTouch(const DrmLeaseTouch&) = delete;
    DrmLeaseTouch& operator=(const DrmLeaseTouch&) = delete;

private:
    void Run(std::stop_token token);
    int OpenDevice();
    void ReadLoop(std::stop_token token, int fd);

    TouchSink sink;
    std::string device_path;
    bool swap_xy = false;
    bool invert_x = false;
    bool invert_y = false;
    std::jthread thread;
};

} // namespace drm_lease
