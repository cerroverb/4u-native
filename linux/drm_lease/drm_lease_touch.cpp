// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.
//
// From virtudude's Azahar, branch drm-leasing, commit d16299140 (src/citra_qt/drm_lease/drm_lease_touch.cpp): the
// same code, with the touch given to a TouchSink (0..1 along the panel's axes, after the swap and inversions)
// instead of framebuffer coordinates to an EmuWindow, log calls replaced by fprintf, Azahar's u8 by uint8_t, and the
// environment variables named MH4U_ instead of AZAHAR_.

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "drm_lease_touch.h"

namespace drm_lease {

namespace {

bool HasBit(const uint8_t* bits, unsigned bit) {
    return bits[bit / 8] & (1 << (bit % 8));
}

bool IsDirectTouchDevice(int fd) {
    uint8_t props[INPUT_PROP_MAX / 8 + 1]{};
    uint8_t abs_bits[ABS_MAX / 8 + 1]{};
    if (ioctl(fd, EVIOCGPROP(sizeof(props)), props) < 0 ||
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) < 0) {
        return false;
    }
    return HasBit(props, INPUT_PROP_DIRECT) && HasBit(abs_bits, ABS_MT_POSITION_X);
}

// name set: return the direct-touch device with that evdev name. name
// empty: return the only direct-touch device; with several, a unique
// device named like the bottom screen wins, otherwise refuse to guess.
std::optional<std::string> FindDirectTouchDevice(const std::string& name) {
    std::vector<std::pair<std::string, std::string>> candidates; // path, evdev name
    DIR* dir = opendir("/dev/input");
    if (!dir) {
        return std::nullopt;
    }
    while (dirent* entry = readdir(dir)) {
        if (std::strncmp(entry->d_name, "event", 5) != 0) {
            continue;
        }
        const std::string path = std::string("/dev/input/") + entry->d_name;
        const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        char device_name[80]{};
        if (IsDirectTouchDevice(fd)) {
            ioctl(fd, EVIOCGNAME(sizeof(device_name) - 1), device_name);
            candidates.emplace_back(path, device_name);
        }
        close(fd);
    }
    closedir(dir);

    if (!name.empty()) {
        for (const auto& [path, dev_name] : candidates) {
            if (dev_name == name) {
                return path;
            }
        }
        return std::nullopt;
    }
    if (candidates.size() == 1) {
        return candidates.front().first;
    }
    std::optional<std::string> bottom;
    for (const auto& [path, dev_name] : candidates) {
        if (dev_name.find("bottom") != std::string::npos) {
            if (bottom) {
                bottom.reset();
                break;
            }
            bottom = path;
        }
    }
    if (bottom) {
        return bottom;
    }
    std::fprintf(stderr,
                 "[Error] drm-lease touch: %zu touch devices found and none is unambiguous, set "
                 "MH4U_DRM_LEASE_TOUCH explicitly\n",
                 candidates.size());
    return std::nullopt;
}

} // namespace

namespace {
bool EnvFlag(const char* name) {
    const char* value = std::getenv(name);
    return value && value[0] != '\0' && value[0] != '0';
}
} // namespace

DrmLeaseTouch::DrmLeaseTouch(TouchSink sink_, std::string device_path_)
    : sink{std::move(sink_)}, device_path{std::move(device_path_)} {
    // Digitizer orientation quirks: swap is applied before inversion.
    swap_xy = EnvFlag("MH4U_DRM_LEASE_TOUCH_SWAP_XY");
    invert_x = EnvFlag("MH4U_DRM_LEASE_TOUCH_INVERT_X");
    invert_y = EnvFlag("MH4U_DRM_LEASE_TOUCH_INVERT_Y");
    thread = std::jthread([this](std::stop_token token) { Run(token); });
}

DrmLeaseTouch::~DrmLeaseTouch() = default;

int DrmLeaseTouch::OpenDevice() {
    std::string path = device_path;
    if (path.empty() || path[0] != '/') {
        const auto found = FindDirectTouchDevice(path == "auto" ? "" : path);
        if (!found) {
            return -1;
        }
        path = *found;
    }

    const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }

    if (ioctl(fd, EVIOCGRAB, 1) < 0) {
        std::fprintf(stderr, "[Error] drm-lease touch: EVIOCGRAB on %s failed: %s\n", path.c_str(),
                     std::strerror(errno));
        close(fd);
        return -1;
    }

    char name[64]{};
    ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
    std::fprintf(stderr, "[Info] drm-lease touch: grabbed %s ('%s')\n", path.c_str(), name);
    return fd;
}

void DrmLeaseTouch::Run(std::stop_token token) {
    while (!token.stop_requested()) {
        const int fd = OpenDevice();
        if (fd < 0) {
            // Device missing or not yet powered (e.g. across suspend); retry.
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        ReadLoop(token, fd);
        ioctl(fd, EVIOCGRAB, 0);
        close(fd);
        sink(std::nullopt);
    }
}

void DrmLeaseTouch::ReadLoop(std::stop_token token, int fd) {
    input_absinfo abs_x{};
    input_absinfo abs_y{};
    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &abs_x) < 0 ||
        ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &abs_y) < 0) {
        std::fprintf(stderr, "[Error] drm-lease touch: failed to read axis ranges\n");
        return;
    }
    const float range_x = static_cast<float>(abs_x.maximum - abs_x.minimum);
    const float range_y = static_cast<float>(abs_y.maximum - abs_y.minimum);
    if (range_x <= 0.0f || range_y <= 0.0f) {
        std::fprintf(stderr, "[Error] drm-lease touch: degenerate axis ranges\n");
        return;
    }

    // Multitouch protocol B, slot 0 only: the 3DS touchscreen is single-touch.
    int slot = 0;
    int raw_x = abs_x.value;
    int raw_y = abs_y.value;
    int tracking_id = -1;
    bool touching = false;

    while (!token.stop_requested()) {
        pollfd pfd{.fd = fd, .events = POLLIN, .revents = 0};
        const int ret = poll(&pfd, 1, 500);
        if (ret < 0 && errno != EINTR) {
            return;
        }
        if (ret <= 0 || !(pfd.revents & POLLIN)) {
            if (pfd.revents & (POLLERR | POLLHUP)) {
                return;
            }
            continue;
        }

        input_event events[64];
        const ssize_t bytes = read(fd, events, sizeof(events));
        if (bytes < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            std::fprintf(stderr, "[Warning] drm-lease touch: read failed: %s\n", std::strerror(errno));
            return;
        }

        const std::size_t count = static_cast<std::size_t>(bytes) / sizeof(input_event);
        for (std::size_t i = 0; i < count; i++) {
            const input_event& ev = events[i];
            switch (ev.type) {
            case EV_ABS:
                switch (ev.code) {
                case ABS_MT_SLOT:
                    slot = ev.value;
                    break;
                case ABS_MT_TRACKING_ID:
                    if (slot == 0) {
                        tracking_id = ev.value;
                    }
                    break;
                case ABS_MT_POSITION_X:
                    if (slot == 0) {
                        raw_x = ev.value;
                    }
                    break;
                case ABS_MT_POSITION_Y:
                    if (slot == 0) {
                        raw_y = ev.value;
                    }
                    break;
                }
                break;
            case EV_SYN: {
                if (ev.code != SYN_REPORT) {
                    break;
                }
                const bool now_touching = tracking_id >= 0;
                float nx = static_cast<float>(raw_x - abs_x.minimum) / range_x;
                float ny = static_cast<float>(raw_y - abs_y.minimum) / range_y;
                if (swap_xy) {
                    std::swap(nx, ny);
                }
                if (invert_x) {
                    nx = 1.0f - nx;
                }
                if (invert_y) {
                    ny = 1.0f - ny;
                }
                if (now_touching && !touching) {
                    touching = sink(std::pair{nx, ny});
                } else if (now_touching && touching) {
                    sink(std::pair{nx, ny});
                } else if (!now_touching && touching) {
                    sink(std::nullopt);
                    touching = false;
                }
                break;
            }
            }
        }
    }
}

} // namespace drm_lease
