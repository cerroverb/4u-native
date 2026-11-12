// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.
//
// From virtudude's Azahar, branch drm-leasing, commit d16299140 (src/citra_qt/drm_lease/drm_lease_client.cpp): the
// same code, with Azahar's log calls replaced by fprintf, its integer types by <cstdint>'s, <algorithm> included
// for std::max, and the namespace renamed.

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <unistd.h>
#include <wayland-client.h>

#include "drm_lease_client.h"
#include "drm-lease-v1-client-protocol.h"

namespace drm_lease {

struct DrmLeaseClientCallbacks {
    static void RegistryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface,
                               uint32_t version) {
        auto* client = static_cast<DrmLeaseClient*>(data);
        if (std::strcmp(interface, wp_drm_lease_device_v1_interface.name) == 0) {
            // One global per DRM node; only the first is used.
            if (client->device) {
                std::fprintf(stderr, "[Info] drm-lease: ignoring additional lease device global\n");
                return;
            }
            client->device = static_cast<wp_drm_lease_device_v1*>(
                wl_registry_bind(registry, name, &wp_drm_lease_device_v1_interface, 1u));
        }
    }

    static void RegistryGlobalRemove(void* data, wl_registry* registry, uint32_t name) {}

    static void DeviceDrmFd(void* data, wp_drm_lease_device_v1* device, int32_t fd) {
        auto* client = static_cast<DrmLeaseClient*>(data);
        // Only needed for enumeration; the mode is read from the lease fd.
        client->enum_fd = fd;
    }

    static void DeviceConnector(void* data, wp_drm_lease_device_v1* device,
                                wp_drm_lease_connector_v1* connector) {
        auto* client = static_cast<DrmLeaseClient*>(data);
        client->connectors.push_back({.handle = connector});
        wp_drm_lease_connector_v1_add_listener(connector, &connector_listener, data);
    }

    static void DeviceDone(void* data, wp_drm_lease_device_v1* device) {
        static_cast<DrmLeaseClient*>(data)->device_done = true;
    }

    static void DeviceReleased(void* data, wp_drm_lease_device_v1* device) {}

    static DrmLeaseClient::ConnectorOffer* FindOffer(DrmLeaseClient* client,
                                                     wp_drm_lease_connector_v1* connector) {
        for (auto& offer : client->connectors) {
            if (offer.handle == connector) {
                return &offer;
            }
        }
        return nullptr;
    }

    static void ConnectorName(void* data, wp_drm_lease_connector_v1* connector, const char* name) {
        auto* client = static_cast<DrmLeaseClient*>(data);
        if (auto* offer = FindOffer(client, connector)) {
            offer->name = name;
        }
    }

    static void ConnectorDescription(void* data, wp_drm_lease_connector_v1* connector,
                                     const char* description) {
        auto* client = static_cast<DrmLeaseClient*>(data);
        if (auto* offer = FindOffer(client, connector)) {
            offer->description = description;
        }
    }

    static void ConnectorConnectorId(void* data, wp_drm_lease_connector_v1* connector,
                                     uint32_t connector_id) {
        auto* client = static_cast<DrmLeaseClient*>(data);
        if (auto* offer = FindOffer(client, connector)) {
            offer->connector_id = connector_id;
        }
    }

    static void ConnectorDone(void* data, wp_drm_lease_connector_v1* connector) {
        auto* client = static_cast<DrmLeaseClient*>(data);
        if (auto* offer = FindOffer(client, connector)) {
            offer->done = true;
        }
    }

    static void ConnectorWithdrawn(void* data, wp_drm_lease_connector_v1* connector) {
        auto* client = static_cast<DrmLeaseClient*>(data);
        if (auto* offer = FindOffer(client, connector)) {
            offer->done = false;
            offer->connector_id = 0;
        }
    }

    static void LeaseFd(void* data, wp_drm_lease_v1* lease, int32_t leased_fd) {
        static_cast<DrmLeaseClient*>(data)->lease_fd = leased_fd;
    }

    static void LeaseFinished(void* data, wp_drm_lease_v1* lease) {
        auto* client = static_cast<DrmLeaseClient*>(data);
        client->lease_finished = true;
        std::fprintf(stderr, "[Warning] DRM lease finished by compositor\n");
    }

    static constexpr wl_registry_listener registry_listener = {
        .global = RegistryGlobal,
        .global_remove = RegistryGlobalRemove,
    };

    static constexpr wp_drm_lease_device_v1_listener device_listener = {
        .drm_fd = DeviceDrmFd,
        .connector = DeviceConnector,
        .done = DeviceDone,
        .released = DeviceReleased,
    };

    static constexpr wp_drm_lease_connector_v1_listener connector_listener = {
        .name = ConnectorName,
        .description = ConnectorDescription,
        .connector_id = ConnectorConnectorId,
        .done = ConnectorDone,
        .withdrawn = ConnectorWithdrawn,
    };

    static constexpr wp_drm_lease_v1_listener lease_listener = {
        .lease_fd = LeaseFd,
        .finished = LeaseFinished,
    };
};

DrmLeaseClient::DrmLeaseClient() = default;

bool DrmLeaseClient::DispatchUntil(const std::function<bool()>& condition, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (!condition()) {
        while (wl_display_prepare_read(display) != 0) {
            if (wl_display_dispatch_pending(display) < 0) {
                return false;
            }
        }
        // Draining the queue above may have satisfied the condition.
        if (condition()) {
            wl_display_cancel_read(display);
            return true;
        }
        short poll_events = POLLIN;
        if (wl_display_flush(display) < 0) {
            if (errno != EAGAIN) {
                wl_display_cancel_read(display);
                return false;
            }
            // Incomplete flush: the request the server must answer may still
            // be buffered, so wake on writability and retry.
            poll_events |= POLLOUT;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            wl_display_cancel_read(display);
            return condition();
        }
        const int remaining = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
        pollfd pfd{.fd = wl_display_get_fd(display), .events = poll_events, .revents = 0};
        const int ret = poll(&pfd, 1, std::max(remaining, 1));
        if (ret < 0 && errno != EINTR) {
            wl_display_cancel_read(display);
            return false;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            wl_display_cancel_read(display);
            return false;
        }
        if (ret > 0 && (pfd.revents & POLLIN)) {
            if (wl_display_read_events(display) < 0) {
                return false;
            }
        } else {
            wl_display_cancel_read(display);
        }
        if (wl_display_dispatch_pending(display) < 0) {
            return false;
        }
    }
    return true;
}

void DrmLeaseClient::PumpEvents() {
    if (!display || pump_dead) {
        return;
    }
    const auto fail = [this] {
        std::fprintf(stderr, "[Warning] drm-lease: Wayland connection failed, event pump disabled\n");
        pump_dead = true;
    };
    while (wl_display_prepare_read(display) != 0) {
        if (wl_display_dispatch_pending(display) < 0) {
            return fail();
        }
    }
    if (wl_display_flush(display) < 0 && errno != EAGAIN) {
        wl_display_cancel_read(display);
        return fail();
    }
    pollfd pfd{.fd = wl_display_get_fd(display), .events = POLLIN, .revents = 0};
    const int ret = poll(&pfd, 1, 0);
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        wl_display_cancel_read(display);
        return fail();
    }
    if (ret > 0 && (pfd.revents & POLLIN)) {
        if (wl_display_read_events(display) < 0) {
            return fail();
        }
    } else {
        wl_display_cancel_read(display);
        if (ret < 0 && errno != EINTR) {
            return fail();
        }
    }
    if (wl_display_dispatch_pending(display) < 0) {
        return fail();
    }
}

DrmLeaseClient::~DrmLeaseClient() {
    if (lease) {
        wp_drm_lease_v1_destroy(lease);
    }
    for (auto& offer : connectors) {
        wp_drm_lease_connector_v1_destroy(offer.handle);
    }
    if (device) {
        // No roundtrip: after a timeout the compositor may be stalled, and
        // disconnecting releases everything server-side anyway.
        wp_drm_lease_device_v1_release(device);
        wl_display_flush(display);
        wp_drm_lease_device_v1_destroy(device);
    }
    if (registry) {
        wl_registry_destroy(registry);
    }
    if (display) {
        wl_display_disconnect(display);
    }
    if (enum_fd >= 0) {
        close(enum_fd);
    }
    if (lease_fd >= 0) {
        close(lease_fd);
    }
}

namespace {
bool IsInternalConnector(const std::string& name) {
    for (const char* prefix : {"DSI-", "eDP-", "LVDS-", "DPI-"}) {
        if (name.rfind(prefix, 0) == 0) {
            return true;
        }
    }
    return false;
}
} // namespace

bool DrmLeaseClient::Acquire(const std::string& connector_name, bool internal_only) {
    // Gamescope sessions hide WAYLAND_DISPLAY from clients so games pick
    // X11, and provide the compositor socket via GAMESCOPE_WAYLAND_DISPLAY.
    // Prefer it: with WAYLAND_DISPLAY unset, the default connection could
    // reach an outer compositor that does not lease.
    if (const char* gamescope_display = std::getenv("GAMESCOPE_WAYLAND_DISPLAY")) {
        display = wl_display_connect(gamescope_display);
    }
    if (!display) {
        display = wl_display_connect(nullptr);
    }
    if (!display) {
        std::fprintf(stderr, "[Info] drm-lease: no Wayland display, not using a leased output\n");
        return false;
    }

    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &DrmLeaseClientCallbacks::registry_listener, this);
    wl_display_roundtrip(display);

    if (!device) {
        std::fprintf(stderr, "[Info] drm-lease: compositor does not offer wp_drm_lease_device_v1\n");
        return false;
    }

    wp_drm_lease_device_v1_add_listener(device, &DrmLeaseClientCallbacks::device_listener, this);
    // Initial enumeration may arrive well after binding (e.g. while the
    // compositor regains DRM master); wait for the device done event.
    if (!DispatchUntil([this] { return device_done; }, 5000)) {
        std::fprintf(stderr, "[Info] drm-lease: lease device never finished enumeration\n");
        return false;
    }

    const ConnectorOffer* chosen = nullptr;
    for (const auto& offer : connectors) {
        if (!offer.done) {
            continue;
        }
        if (!connector_name.empty()) {
            if (offer.name == connector_name) {
                chosen = &offer;
                break;
            }
        } else if (!internal_only || IsInternalConnector(offer.name)) {
            chosen = &offer;
            break;
        }
    }
    if (!chosen) {
        std::fprintf(stderr, "[Info] drm-lease: no eligible connector offered (wanted '%s', %zu offered)\n",
                     connector_name.empty() ? "any internal" : connector_name.c_str(), connectors.size());
        return false;
    }

    // Snapshot before dispatching further: the dispatch below may
    // reallocate the offer vector, and a withdrawn event clears the offer's
    // fields without invalidating a granted lease.
    const std::string chosen_name = chosen->name;
    const uint32_t chosen_id = chosen->connector_id;
    wp_drm_lease_connector_v1* const chosen_handle = chosen->handle;
    chosen = nullptr;

    std::fprintf(stderr, "[Info] drm-lease: requesting connector '%s' (id %u)\n", chosen_name.c_str(), chosen_id);

    wp_drm_lease_request_v1* request = wp_drm_lease_device_v1_create_lease_request(device);
    wp_drm_lease_request_v1_request_connector(request, chosen_handle);
    lease = wp_drm_lease_request_v1_submit(request);
    wp_drm_lease_v1_add_listener(lease, &DrmLeaseClientCallbacks::lease_listener, this);

    if (!DispatchUntil([this] { return lease_fd >= 0 || lease_finished; }, 5000)) {
        std::fprintf(stderr, "[Error] drm-lease: no lease response from the compositor\n");
        return false;
    }

    if (lease_finished || lease_fd < 0) {
        std::fprintf(stderr, "[Error] drm-lease: compositor rejected the lease request\n");
        return false;
    }

    leased_connector_id = chosen_id;
    std::fprintf(stderr, "[Info] drm-lease: acquired lease fd %d for connector %u\n", lease_fd,
                 leased_connector_id);
    return true;
}

} // namespace drm_lease
