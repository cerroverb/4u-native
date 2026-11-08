// The first start's options for this phone (first_run.h).
#include "first_run.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <dlfcn.h>
#include <strings.h>
#include <unistd.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <utility>
#include <vector>

#include "app_settings.h"

namespace {

void Log(const std::string& text) { std::fprintf(stderr, "[Info] App: first start: %s\n", text.c_str()); }

// The first GPU's name, from an instance made for it alone (the system's loader; the game makes its own later)
std::string VulkanGpuName() {
    void* lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr) return "";
    std::string name;
    auto get = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(lib, "vkGetInstanceProcAddr"));
    auto create = get ? reinterpret_cast<PFN_vkCreateInstance>(get(nullptr, "vkCreateInstance")) : nullptr;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "MH4U first start";
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    if (create != nullptr && create(&info, nullptr, &instance) == VK_SUCCESS) {
        auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(get(instance, "vkEnumeratePhysicalDevices"));
        auto properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(get(instance, "vkGetPhysicalDeviceProperties"));
        auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(get(instance, "vkDestroyInstance"));
        uint32_t n = 0;
        if (enumerate != nullptr && properties != nullptr && enumerate(instance, &n, nullptr) == VK_SUCCESS && n > 0) {
            std::vector<VkPhysicalDevice> devices(n);
            if (enumerate(instance, &n, devices.data()) == VK_SUCCESS && n > 0) {
                VkPhysicalDeviceProperties p{};
                properties(devices[0], &p);
                name = p.deviceName;
            }
        }
        if (destroy != nullptr) destroy(instance, nullptr);
    }
    // (the library stays loaded: drivers do not always survive being unloaded, and the game loads it again)
    return name;
}

// The number after `prefix` in `name` (any case), or -1
int NumberAfter(const std::string& name, const char* prefix) {
    const size_t at = [&] {
        std::string lower = name;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return lower.find(prefix);
    }();
    if (at == std::string::npos) return -1;
    size_t i = at + std::char_traits<char>::length(prefix);
    while (i < name.size() && (name[i] == ' ' || name[i] == '-')) ++i;
    if (i >= name.size() || !std::isdigit(static_cast<unsigned char>(name[i]))) return -1;
    return std::atoi(name.c_str() + i);
}

}  // namespace

bool first_run_weak_gpu(const std::string& name) {
    if (name.empty()) return false;
    if (strcasestr(name.c_str(), "powervr") != nullptr) return true;  // (the GE8320 of a Helio G35, the BXM of a Helio G36)
    if (strcasestr(name.c_str(), "mali-t") != nullptr) return true;   // (Midgard)
    // Mali-G31, G51, G52, G71, G72 (Bifrost, before the G76): the G52 is the Moto g06's; a G57 (Valhall) is not weak
    if (const int g = NumberAfter(name, "mali-g"); g >= 0) return g == 31 || g == 51 || g == 52 || g == 71 || g == 72;
    // Adreno 3xx to 5xx, and the 610 to 613 of the low-end Snapdragons (6xx, 4xx)
    if (const int a = NumberAfter(name, "adreno"); a >= 0) return a < 600 || (a >= 610 && a <= 613);
    return false;
}

bool first_run_fast_cores() {
    static const bool fast = [] {
        if (const char* e = std::getenv("MH4U_FAST_CORES"); e != nullptr && (*e == '0' || *e == '1')) return *e == '1';
        std::ifstream in("/proc/cpuinfo");
        std::string line;
        int parts = 0, small = 0;
        while (std::getline(in, line)) {
            if (line.compare(0, 8, "CPU part") != 0) continue;
            const size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            const unsigned part = static_cast<unsigned>(std::strtoul(line.c_str() + colon + 1, nullptr, 16));
            ++parts;
            // A53, A35, A55, A510, A520; Kryo 2xx/3xx/4xx Silver (0x801, 0x803, 0x805)
            for (unsigned s : {0xd03u, 0xd04u, 0xd05u, 0xd46u, 0xd80u, 0x801u, 0x803u, 0x805u})
                small += part == s;
        }
        return parts == 0 || small < parts;
    }();
    return fast;
}

FirstRunDevice first_run_probe() {
    FirstRunDevice d;
    const char* gpu = std::getenv("MH4U_FIRST_RUN_GPU");
    d.gpu = gpu != nullptr ? gpu : VulkanGpuName();
    const char* ram = std::getenv("MH4U_FIRST_RUN_RAM_MB");
    d.ram_mb = ram != nullptr ? std::atoll(ram)
                              : static_cast<long long>(sysconf(_SC_PHYS_PAGES)) * sysconf(_SC_PAGE_SIZE) / (1024 * 1024);
    // "4 GB" phones report a little less than 4096 MB (the kernel and the GPU keep some): up to 4.5 GB counts as 4 GB
    const bool small = d.ram_mb > 0 && d.ram_mb <= 4608;
    const bool weak_gpu = first_run_weak_gpu(d.gpu);
    d.weak = small || weak_gpu;
    d.why = "GPU " + (d.gpu.empty() ? std::string("unknown") : d.gpu) + (weak_gpu ? " (weak)" : "") + ", " +
            std::to_string(d.ram_mb) + " MB of RAM" + (small ? " (4 GB or less)" : "");
    return d;
}

void first_run_write(const std::string& ini_path, const FirstRunDevice& device) {
    // the port's options (the hooks read <user>/mh4u.ini): the picture as wide as the screen; 60 FPS, or 30 and the screen
    // filters off on a weak phone (Low-end mode's, settings_panel.cpp low_end)
    std::ofstream ini(ini_path);
    ini << "# MH4U for Android: the port's options (the same as the PC port's mh4u.ini)\n"
           "# frame rate cap: 30 (the 3DS's) to 240\nfps = "
        << (device.weak ? low_end_fps() : 60)
        << "\n"
           "# the top screen as wide as the phone's screen\nwidescreen = match\nwide_render = 1\n"
           "# the hunting HUD's sides at the screen's edges\nhud_edges = 1\n"
           "# the kick and the target camera on ZR and ZL\nkick = ZR\ntarget = ZL\nskip = ZR\n";
    if (device.weak) {
        ini << "# Low-end mode (this phone's first start): the screen filters and the game's anti-aliasing off\n";
        for (const char* filter : kLowEndFilters) ini << filter << " = off\n";
        ini << "antialiasing = off\n";
    }
    ini.close();
    if (!device.weak) {
        Log(device.why + ": the app's own options");
        return;
    }
    // the app's: Low-end mode and the 3D resolution it sets, unless the file has them already
    bool changed = false;
    auto set = [&](const char* section, const char* key, const std::string& value) {
        if (!app_setting(section, key).empty()) return;
        app_setting_set(section, key, value);
        changed = true;
    };
    set("Graphics", "LowEnd", "on");
    set("Graphics", "RenderScale", std::to_string(kLowEndScale));
    set("Graphics", "Shadows", "off");
    std::string error;
    if (changed && !app_settings_save(&error)) Log("mh4u-app.ini: " + error);
    Log(device.why + ": Low-end mode, " + std::to_string(low_end_fps()) + " FPS, the 3D resolution at " +
        std::to_string(kLowEndScale) + " %");
}

void first_run_fill(const std::string& ini_path) {
    std::vector<std::string> lines;
    {
        std::ifstream in(ini_path);
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
    }
    if (lines.empty() || lines[0].rfind("# MH4U for Android", 0) != 0) return;
    auto has = [&](const char* key) {
        for (const std::string& raw : lines) {
            const size_t a = raw.find_first_not_of(" \t");
            if (a == std::string::npos || raw[a] == '#') continue;
            const size_t eq = raw.find('=');
            if (eq == std::string::npos) continue;
            std::string k = raw.substr(a, eq - a);
            while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
            if (k == key) return true;
        }
        return false;
    };
    static const std::pair<const char*, const char*> kDefaults[] = {{"fps", "60"}, {"widescreen", "match"}, {"kick", "ZR"}, {"target", "ZL"}};
    std::string added;
    for (const auto& [key, value] : kDefaults) {
        if (has(key)) continue;
        lines.push_back(std::string(key) + " = " + value);
        added += std::string(added.empty() ? "" : ", ") + lines.back();
    }
    if (added.empty()) return;
    std::ofstream out(ini_path, std::ios::trunc);
    for (const std::string& line : lines) out << line << '\n';
    std::fprintf(stderr, "[Info] App: mh4u.ini lacked %s (the settings' defaults): added\n", added.c_str());
}
