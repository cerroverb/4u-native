// The host's Vulkan driver hooks (driver_hooks.h).
#include "gpu/backend/driver_hooks.h"

#include <mutex>

namespace mh4n::gpu::rhi {

namespace {

std::mutex g_mutex;
DriverHooks g_hooks;

DriverHooks Hooks() {
    std::lock_guard lock(g_mutex);
    return g_hooks;
}

} // namespace

void SetDriverHooks(DriverHooks hooks) {
    std::lock_guard lock(g_mutex);
    g_hooks = std::move(hooks);
}

namespace core {

PFN_vkGetInstanceProcAddr HostLoader() {
    const DriverHooks hooks = Hooks();
    return hooks.loader ? hooks.loader() : nullptr;
}

void HostLoaderFailed(const std::string& why) {
    const DriverHooks hooks = Hooks();
    if (hooks.failed) hooks.failed(why);
}

void HostDeviceMade(const MadeDevice& device) {
    const DriverHooks hooks = Hooks();
    if (hooks.device_made) hooks.device_made(device);
}

} // namespace core

} // namespace mh4n::gpu::rhi
