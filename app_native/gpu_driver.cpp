// The GPU driver: the system's, or a player's own for an Adreno GPU (gpu_driver.h).
#include "gpu_driver.h"

#include <SDL3/SDL.h>
#include <android/log.h>
#include <dlfcn.h>
#include <link.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include "adrenotools/driver.h"
#include "app_jni.h"
#include "gpu/backend/driver_hooks.h"

extern "C" volatile uint64_t g_present_count;  // (android_main.cpp: the game's frames shown)

namespace {

namespace fs = std::filesystem;
namespace rhi = mh4n::gpu::rhi;

// The game's frames shown with a player's driver before it is trusted (the trial file goes): a few seconds of the logos
constexpr uint64_t kFramesToTrust = 120;
constexpr uint32_t kQualcomm = 0x5143;  // (PCI vendor id of the Adreno GPUs)

std::mutex g_mutex;  // (g_status and g_list: the panel reads them on the app's thread, the hooks write on the game's)
GpuDriverStatus g_status;
std::vector<GpuDriver> g_list;
std::string g_root;  // files/gpu_drivers

// The player's driver this start tries: set by gpu_driver_setup, before the game's thread exists
std::string g_try_folder, g_try_library, g_try_name;
bool g_opened = false;                      // (the loader was asked once: a second instance gets the same)
PFN_vkGetInstanceProcAddr g_gipa = nullptr;  // the driver's libvulkan; null: the system's

void Log(const char* format, ...) __attribute__((format(printf, 1, 2)));
void Log(const char* format, ...) {
    char text[1024];
    va_list ap;
    va_start(ap, format);
    vsnprintf(text, sizeof text, format, ap);
    va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, "MH4U", "gpu driver: %s", text);
    std::fprintf(stderr, "[Info] GPU driver: %s\n", text);  // (log.txt)
}

std::string Path(const std::string& name) { return g_root + "/" + name; }

bool Exists(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0;
}

std::vector<std::string> Lines(const std::string& path) {
    std::vector<std::string> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        out.push_back(line);
    }
    return out;
}

bool WriteText(const std::string& path, const std::string& text) {
    {
        std::ofstream out(path + ".tmp", std::ios::trunc);
        out << text;
        if (!out) return false;
    }
    return std::rename((path + ".tmp").c_str(), path.c_str()) == 0;
}

// Its name as driver.txt has it, else its folder
std::string NameOf(const std::string& folder) {
    std::vector<std::string> info = Lines(Path(folder + "/driver.txt"));
    return !info.empty() && !info[0].empty() ? info[0] : folder;
}

// The Vulkan driver the system declares (Qualcomm's say "adreno"): the GPU before the game's device is made
bool AdrenoBySystem() {
    char value[PROP_VALUE_MAX] = "";
    __system_property_get("ro.hardware.vulkan", value);
    return strcasecmp(value, "adreno") == 0;
}

// The player's driver this start is given up: its choice moves to `failed` (the settings say so), the trial ends, and the
// system's driver runs
void GiveUp(const std::string& why) {
    Log("%s did not work (%s): the system's driver instead", g_try_name.c_str(), why.c_str());
    WriteText(Path("failed"), g_try_name + "\n");
    unlink(Path("selected").c_str());
    unlink(Path("trial").c_str());
    std::lock_guard lock(g_mutex);
    g_status.failed = g_try_name;
    g_status.chosen.clear();
    g_status.running.clear();
    g_status.running_folder.clear();
}

// ---- the runtime's hooks (gpu/backend/driver_hooks.h), on the game's thread

// Where the hook libraries are: beside this one, in the app's nativeLibraryDir (extracted: extractNativeLibs)
std::string HookDir() {
    Dl_info me{};
    if (!dladdr(reinterpret_cast<void*>(&gpu_driver_setup), &me) || me.dli_fname == nullptr) return "";
    std::string dir = me.dli_fname;
    return dir.substr(0, dir.rfind('/') + 1);
}

PFN_vkGetInstanceProcAddr Loader() {
    if (g_try_folder.empty() || g_opened) return g_gipa;
    g_opened = true;
    const std::string dir = Path(g_try_folder) + "/", hooks = HookDir();
    WriteText(Path("trial"), g_try_folder + "\n");  // (removed once it has drawn: Watch)
    void* lib = adrenotools_open_libvulkan(RTLD_NOW, ADRENOTOOLS_DRIVER_CUSTOM, nullptr, hooks.c_str(), dir.c_str(),
                                           g_try_library.c_str(), nullptr, nullptr);
    if (lib == nullptr) {
        const char* error = dlerror();
        GiveUp(std::string("it could not be opened: ") + (error ? error : "no reason given"));
        return nullptr;
    }
    g_gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(lib, "vkGetInstanceProcAddr"));
    if (g_gipa == nullptr) {
        GiveUp("its Vulkan loader has no vkGetInstanceProcAddr");
        return nullptr;
    }
    Log("%s (%s) opened through libadrenotools, hooks in %s", g_try_name.c_str(), g_try_library.c_str(), hooks.c_str());
    return g_gipa;
}

void Failed(const std::string& why) {
    g_gipa = nullptr;
    GiveUp(why);
}

// Is a library of the driver's folder loaded? (by the folder's own part of the path: the linker keeps real paths,
// /data/data/..., where SDL gave /data/user/0/...)
int FoundInFolder(struct dl_phdr_info* info, size_t, void* data) {
    return info->dlpi_name != nullptr && std::strstr(info->dlpi_name, static_cast<const char*>(data)) != nullptr;
}

// The game's frames shown with the player's driver: it works, the trial ends (a thread of its own, gone once done)
void Watch(const std::string& name) {
    const std::string trial = Path("trial");
    std::thread([trial, name] {
        while (g_present_count < kFramesToTrust) std::this_thread::sleep_for(std::chrono::milliseconds(250));
        unlink(trial.c_str());
        Log("%s works: %llu frames shown with it", name.c_str(), static_cast<unsigned long long>(g_present_count));
    }).detach();
}

void DeviceMade(const rhi::MadeDevice& d) {
    char version[48];
    snprintf(version, sizeof version, "Vulkan %u.%u.%u", (d.api_version >> 22) & 0x7f, (d.api_version >> 12) & 0x3ff,
             d.api_version & 0xfff);
    std::string driver = d.driver_name;
    if (!d.driver_info.empty()) driver += (driver.empty() ? "" : ", ") + d.driver_info;
    driver += (driver.empty() ? "" : ", ") + std::string(version);
    bool custom = g_gipa != nullptr;
    if (custom) {
        // A library that can't be opened, or one named as the system's (Android's own interface has that one loaded
        // already), leaves the system's driver in its place without an error: the MHGU port saw a menu that said
        // "Adreno 840" over the system's 1.1.87 driver. So: is a library of its folder loaded?
        std::string folder = "/gpu_drivers/" + g_try_folder + "/";
        if (!dl_iterate_phdr(FoundInFolder, folder.data())) {
            GiveUp("no library of its folder is loaded: the system's driver opened in its place");
            custom = false;
        }
    }
    Log("device \"%s\", driver %s: %s", d.device_name.c_str(), driver.c_str(),
        custom ? ("the player's, " + g_try_name).c_str() : "the system's");
    {
        std::lock_guard lock(g_mutex);
        g_status.made = true;
        g_status.adreno = d.vendor_id == kQualcomm || d.device_name.find("Adreno") != std::string::npos;
        g_status.gpu = d.device_name;
        g_status.driver = driver;
        if (custom) g_status.running = g_try_name, g_status.running_folder = g_try_folder;
    }
    if (custom) Watch(g_try_name);
}

}  // namespace

void gpu_driver_setup() {
    const char* home = SDL_GetAndroidInternalStoragePath();
    g_root = std::string(home ? home : ".") + "/gpu_drivers";
    mkdir(g_root.c_str(), 0700);
    const bool adreno = AdrenoBySystem();
    {
        std::lock_guard lock(g_mutex);
        g_status.adreno = adreno;
        std::vector<std::string> failed = Lines(Path("failed"));
        if (!failed.empty()) g_status.failed = failed[0];
    }
    std::vector<std::string> selected = Lines(Path("selected"));
    if (selected.size() >= 2 && !selected[0].empty() && !selected[1].empty()) {
        g_try_name = NameOf(selected[0]);
        if (!adreno) {
            Log("%s is chosen, but this GPU is not an Adreno: the system's driver", g_try_name.c_str());
        } else if (Exists(Path("trial"))) {
            GiveUp("the last start with it never showed the game");
        } else if (!Exists(Path(selected[0] + "/" + selected[1]))) {
            GiveUp("its library " + selected[1] + " is not in its folder");
        } else {
            g_try_folder = selected[0];
            g_try_library = selected[1];
            std::lock_guard lock(g_mutex);
            g_status.chosen = g_try_folder;
            Log("%s (%s) chosen: trying it", g_try_name.c_str(), g_try_library.c_str());
        }
    } else {
        unlink(Path("trial").c_str());
    }
    gpu_driver_reload();
    rhi::DriverHooks hooks;
    hooks.loader = Loader;
    hooks.failed = Failed;
    hooks.device_made = DeviceMade;
    rhi::SetDriverHooks(std::move(hooks));
}

GpuDriverStatus gpu_driver_status() {
    std::lock_guard lock(g_mutex);
    return g_status;
}

std::vector<GpuDriver> gpu_driver_list() {
    std::lock_guard lock(g_mutex);
    return g_list;
}

void gpu_driver_reload() {
    std::vector<GpuDriver> list;
    std::error_code ec;
    for (const fs::directory_entry& e : fs::directory_iterator(g_root, ec)) {
        const std::string folder = e.path().filename().string();
        if (!e.is_directory(ec) || folder.empty() || folder[0] == '.') continue;  // (.new: an import under way)
        std::vector<std::string> info = Lines(Path(folder + "/driver.txt"));
        if (info.size() < 2 || info[1].empty() || !Exists(Path(folder + "/" + info[1]))) continue;
        list.push_back({folder, info[0].empty() ? folder : info[0], info[1], info.size() > 2 ? info[2] : ""});
    }
    std::sort(list.begin(), list.end(), [](const GpuDriver& a, const GpuDriver& b) { return a.name < b.name; });
    std::lock_guard lock(g_mutex);
    g_list = std::move(list);
}

bool gpu_driver_choose(const std::string& folder) {
    unlink(Path("trial").c_str());
    if (folder.empty()) {
        if (unlink(Path("selected").c_str()) != 0 && Exists(Path("selected"))) return false;
    } else {
        std::string library;
        for (const GpuDriver& d : gpu_driver_list())
            if (d.folder == folder) library = d.library;
        if (library.empty() || !WriteText(Path("selected"), folder + "\n" + library + "\n")) return false;
    }
    unlink(Path("failed").c_str());
    Log("%s chosen for the next start", folder.empty() ? "the system's driver" : NameOf(folder).c_str());
    std::lock_guard lock(g_mutex);
    g_status.chosen = folder;
    g_status.failed.clear();
    return true;
}

bool gpu_driver_remove(const std::string& folder) {
    if (folder.empty() || folder.find('/') != std::string::npos || folder[0] == '.') return false;
    if (gpu_driver_status().chosen == folder && !gpu_driver_choose("")) return false;
    std::error_code ec;
    fs::remove_all(Path(folder), ec);
    Log("%s removed%s", folder.c_str(), ec ? (": " + ec.message()).c_str() : "");
    gpu_driver_reload();
    return !ec;
}

void gpu_driver_failure_seen() {
    unlink(Path("failed").c_str());
}

void gpu_driver_import() {
    unlink(Path("import.txt").c_str());
    app_call_activity("showDriverImport");
}

GpuDriverImport gpu_driver_import_result() {
    GpuDriverImport r;
    const std::string path = Path("import.txt");
    if (g_root.empty() || !Exists(path)) return r;
    std::vector<std::string> lines = Lines(path);
    unlink(path.c_str());
    gpu_driver_reload();
    r.done = true;
    r.ok = !lines.empty() && lines[0] == "ok" && lines.size() >= 3;
    if (r.ok) {
        r.folder = lines[1], r.name = lines[2];
        Log("%s installed in %s", r.name.c_str(), r.folder.c_str());
    } else {
        r.error = lines.size() >= 2 && !lines[1].empty() ? lines[1] : "unreadable";
        r.detail = lines.size() >= 3 ? lines[2] : "";
        Log("import failed: %s (%s)", r.error.c_str(), r.detail.c_str());
    }
    return r;
}
