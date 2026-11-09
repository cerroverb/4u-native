// MH4U for Android: the app's native side. SDL3 gives the window, the events, the controllers and the
// sound; the game runs on the native runtime (mh4n) on a thread of its own, with the native renderer,
// and its two screens come back here as pictures that this file draws in the window: straight from the buffers the
// renderer draws them into (shared_screens.cpp, no copy through the CPU) where the device can, else as pixels.
//
// The Java side (GameActivity) writes launch.txt in the app's external files folder:
//   game=<the game folder: exheader.bin, code.bin, romfs.bin, update_romfs.bin, icon.bin>
//   code=<the game's code library, built on the player's PC>
//   user=<the user folder: saves, extdata, mh4u.ini>
// The library is copied into the app's internal storage first: Android runs no code from shared storage.
#include <filesystem>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <android/log.h>
#include <jni.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "hooks.h"
#include "app_jni.h"
#include "app_live.h"
#include "app_settings.h"
#include "content.h"
#include "controls.h"
#include "crash_screen.h"
#include "display_vsync.h"
#include "first_run.h"
#include "game_options.h"
#include "gpu_driver.h"
#include "perf_hint.h"
#include "present_pacing.h"
#include "perfstats.h"
#include "settings_panel.h"
#include "shared_screens.h"
#include "town.h"
#include "ui.h"
#include "gpu/queue/queue_gpu.h"
#include "input_live/live_input.h"
#include "mh4n/host.h"
#include "mh4n/host_thread_name.h"
#include "mh4n/log.h"
#include "mh4n/options.h"
#include "mh4n/pacing_trace.h"
#include "mh4n/present.h"
#include "mh4n/registry.h"
#include "mh4n/runtime.h"

namespace {

using mh4n::s16;
using mh4n::u16;
using mh4n::u32;
using mh4n::u64;
using mh4n::u8;

constexpr const char* kTag = "MH4U";

void Logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void Logf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, kTag, fmt, ap);
    va_end(ap);
}

// A static method of GameActivity, from the SDL thread (the one SDL gave a JNIEnv): "crashReport", "setFrameCap"...
void CallActivity(const char* method, const char* signature, ...) {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (env == nullptr || activity == nullptr) return;
    jclass cls = env->GetObjectClass(activity);
    jmethodID id = env->GetStaticMethodID(cls, method, signature);
    if (id != nullptr) {
        va_list ap;
        va_start(ap, signature);
        env->CallStaticVoidMethodV(cls, id, ap);
        va_end(ap);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
}

// A static long method of GameActivity without arguments, from the SDL thread (0 if it cannot be called)
std::int64_t CallActivityLong(const char* method) {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (env == nullptr || activity == nullptr) return 0;
    jclass cls = env->GetObjectClass(activity);
    jmethodID id = env->GetStaticMethodID(cls, method, "()J");
    std::int64_t value = 0;
    if (id != nullptr) value = env->CallStaticLongMethod(cls, id);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        value = 0;
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return value;
}

// The game's frames shown (perfstats.cpp counts them over half a second)
extern "C" {
volatile uint64_t g_present_count = 0;
}

}  // namespace

// app_jni.h: GameActivity's static void methods without arguments (the settings' Import save, Share log, ...)
void app_call_activity(const char* method) {
    CallActivity(method, "()V");
}

namespace {

// ---- the screens, from the game thread to the window ---------------------------------------------

// The newest picture of a screen: CPU pixels or a shared picture (the renderer's buffer), whichever came last.
struct ScreenPicture {
    std::shared_ptr<const std::vector<u8>> pixels;
    std::shared_ptr<const mh4n::present::SharedPicture> shared;
    mh4n::present::ImageRegion region;  // (the shared form: the screen in its buffer)
    u32 w = 0, h = 0;
};

struct Screens {
    std::mutex mutex;
    u64 sequence = 0;
    ScreenPicture top, bottom;
    u32 game_frame = 0;
};
Screens g_screens;
// Written at each publication: the main loop's wait wakes up at once for a new picture.
int g_publish_event = -1;
// The screens straight from the renderer's buffers (shared_screens.h): on when the device's EGL can, unless
// MH4U_PRESENT_CPU=1 (a measurement aid: the pixels read back and uploaded, as before).
bool g_want_shared = false;
// The touch screen's corner is hidden (the main loop's layout): the renderer may leave its draws out (ScreenHidden).
std::atomic<bool> g_touch_hidden{false};

class WindowSink final : public mh4n::present::ScreenSink {
public:
    void Publish(const mh4n::present::DisplayFrame& frame) override {
        {
            std::lock_guard lock(g_screens.mutex);
            Take(frame.top, g_screens.top);
            Take(frame.bottom, g_screens.bottom);
            ++g_screens.sequence;
            g_screens.game_frame = frame.game_frame;
        }
        if (g_publish_event >= 0) {
            const uint64_t one = 1;
            (void)!write(g_publish_event, &one, sizeof(one));
        }
    }
    bool WantsSharedPictures() const override { return g_want_shared; }
    bool ScreenHidden(u32 screen) const override { return screen == 1 && g_touch_hidden.load(std::memory_order_relaxed); }

private:
    static void Take(const std::optional<mh4n::present::ScreenImage>& image, ScreenPicture& out) {
        if (!image) return;
        if (image->IsCpu()) {
            out.pixels = image->pixels;
            out.shared.reset();
            out.w = image->width;
            out.h = image->height;
        } else if (image->IsShared() && image->Valid()) {
            out.shared = image->shared;
            out.pixels.reset();
            out.region = image->region;
            out.w = image->region.width;
            out.h = image->region.height;
        }
    }
};
WindowSink g_sink;

// Any event SDL takes in (a touch, a key, a controller, the window) wakes the main loop's wait at once (SDL calls the
// watch on the thread that adds the event): the wait need not be short for the input's sake.
bool SDLCALL WakeMainLoop(void*, SDL_Event*) {
    if (g_publish_event >= 0) {
        const uint64_t one = 1;
        (void)!write(g_publish_event, &one, sizeof(one));
    }
    return true;
}

std::atomic<mh4n::Runtime*> g_runtime{nullptr};
std::atomic<bool> g_game_done{false};
std::atomic<int> g_game_status{0};

// The display GPU is made in the `devices` phase; this step, after it, hands it the window's sink and
// keeps the runtime for the input.
void AttachWindow(mh4n::Runtime& rt) {
    if (auto* display = mh4n::present::FindDisplay(rt.Interfaces().gpu)) {
        display->AddScreenSink(&g_sink);
    } else {
        Logf("the GPU shows no screens: nothing will be drawn");
    }
    g_runtime.store(&rt);
}
MH4N_REGISTER(mh4n::BootStep, "zz.android.window", mh4n::BootStep{mh4n::Phase::Started, AttachWindow});

// ---- files ---------------------------------------------------------------------------------------

std::map<std::string, std::string> ReadKeyValues(const std::string& path) {
    std::map<std::string, std::string> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        const auto eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
        out[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return out;
}

bool FileExists(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0;
}

// Copies `from` to `to` unless `to` already has the same size and is not older.
bool CopyIfChanged(const std::string& from, const std::string& to) {
    struct stat a{}, b{};
    if (stat(from.c_str(), &a) != 0) return false;
    if (stat(to.c_str(), &b) == 0 && a.st_size == b.st_size && b.st_mtime >= a.st_mtime) return true;
    std::ifstream in(from, std::ios::binary);
    std::ofstream out(to + ".tmp", std::ios::binary | std::ios::trunc);
    out << in.rdbuf();
    out.close();
    if (!out || std::rename((to + ".tmp").c_str(), to.c_str()) != 0) return false;
    chmod(to.c_str(), 0700);
    return true;
}

// ---- layout --------------------------------------------------------------------------------------

struct Layout {
    SDL_FRect top{}, bottom{};
    bool bottom_shown = true;
    u8 bottom_alpha = 255;
};

// [Display] of mh4u-app.ini (the settings panel writes it): where the touch screen goes
struct DisplayOptions {
    enum Mode { Overlay, Stacked, Side } mode = Overlay;
    int corner = 0;        // 0 bottom right, 1 bottom left, 2 top right, 3 top left
    float size = 0.42f;    // the touch screen's height, as a share of the window's (overlay)
    u8 opacity = 255;
};

DisplayOptions ReadDisplayOptions(const std::string& ini_path) {
    DisplayOptions d;
    std::ifstream in(ini_path);
    std::string line, section;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[') {
            section = line.substr(1, line.find(']') - 1);
            continue;
        }
        const auto eq = line.find('=');
        if (section != "Display" || eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        while (!key.empty() && key.back() == ' ') key.pop_back();
        while (!value.empty() && value.front() == ' ') value.erase(0, 1);
        if (key == "Layout") d.mode = value == "stacked" ? DisplayOptions::Stacked : value == "side" ? DisplayOptions::Side : DisplayOptions::Overlay;
        else if (key == "Corner") d.corner = value == "bottom-left" ? 1 : value == "top-right" ? 2 : value == "top-left" ? 3 : 0;
        else if (key == "Size") d.size = std::clamp(std::atoi(value.c_str()), 20, 60) / 100.0f;
        else if (key == "Opacity") d.opacity = static_cast<u8>(std::clamp(std::atoi(value.c_str()), 30, 100) * 255 / 100);
    }
    return d;
}

// The top screen's shape on the phone (mh4u.ini's widescreen, as the hooks resolve it at start: match is the window's
// own), 0 for the picture's. A screen narrower than the 3DS's 5:3 (the Anbernic's 4:3) gets a picture drawn 5:3 with
// its 3D view narrowed to that shape (hooks.cpp GetProjMatrix), to be squeezed to it here: without this it was shown
// 5:3 with black bars above and below.
float g_top_aspect = 0.0f;

float TopAspectFromIni(const std::string& ini_path, int win_w, int win_h) {
    std::string value;
    for (const auto& [k, v] : ReadKeyValues(ini_path)) {
        std::string key = k;
        while (!key.empty() && key.back() == ' ') key.pop_back();
        if (key == "widescreen") value = v;
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\r')) value.pop_back();
    while (!value.empty() && value.front() == ' ') value.erase(0, 1);
    const int w = std::max(win_w, win_h), h = std::min(win_w, win_h);
    if (value == "match" || value == "auto" || value == "screen") return h > 0 ? static_cast<float>(w) / h : 0.0f;
    const auto colon = value.find(':');
    if (colon != std::string::npos) {
        const float a = std::strtof(value.c_str(), nullptr), b = std::strtof(value.c_str() + colon + 1, nullptr);
        return b > 0 ? a / b : 0.0f;
    }
    return std::max(0.0f, std::strtof(value.c_str(), nullptr));
}

// The screens in the window. Overlay: the top screen as large as the window allows, the touch screen over it in a
// corner. Stacked: one above the other, as on the 3DS (4:3 screens). Side: the touch screen beside the top one.
Layout ComputeLayout(int w, int h, u32 top_w, u32 top_h, u32 bot_w, u32 bot_h, bool bottom_shown,
                     const DisplayOptions& d) {
    Layout l;
    l.bottom_shown = bottom_shown;
    l.bottom_alpha = d.mode == DisplayOptions::Overlay ? d.opacity : 255;
    const float th = top_h ? static_cast<float>(top_h) : 240.0f;
    const float tw = g_top_aspect > 0 ? th * g_top_aspect : top_w ? static_cast<float>(top_w) : 400.0f;
    const float bw = bot_w ? static_cast<float>(bot_w) : 320.0f, bh = bot_h ? static_cast<float>(bot_h) : 240.0f;
    if (bottom_shown && d.mode == DisplayOptions::Stacked) {
        const float scale = std::min(w / std::max(tw, bw), h / (th + bh));
        const float x0 = (w - tw * scale) / 2, y0 = (h - (th + bh) * scale) / 2;
        l.top = {x0, y0, tw * scale, th * scale};
        l.bottom = {(w - bw * scale) / 2, y0 + th * scale, bw * scale, bh * scale};
        return l;
    }
    if (bottom_shown && d.mode == DisplayOptions::Side) {
        // the top screen gets what the touch screen (at the top screen's height scale, up to half of it) leaves
        const float scale = std::min(w / (tw + bw * 0.5f), h / th);
        const float bs = scale * 0.5f;
        const float x0 = (w - (tw * scale + bw * bs)) / 2;
        l.top = {x0, (h - th * scale) / 2, tw * scale, th * scale};
        l.bottom = {x0 + tw * scale, (h - bh * bs) / 2, bw * bs, bh * bs};
        return l;
    }
    const float scale = std::min(w / tw, h / th);
    l.top = {(w - tw * scale) / 2, (h - th * scale) / 2, tw * scale, th * scale};
    if (bottom_shown) {
        const float bs = (h * d.size) / bh;
        const float bwp = bw * bs, bhp = bh * bs, m = 8;
        const bool right = d.corner == 0 || d.corner == 2, below = d.corner == 0 || d.corner == 1;
        l.bottom = {right ? w - bwp - m : m, below ? h - bhp - m : m, bwp, bhp};
    }
    return l;
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
        Logf("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    const std::string internal = SDL_GetAndroidInternalStoragePath() ? SDL_GetAndroidInternalStoragePath() : "";
    // the app's own folder: the external one, or the internal one when the system has no external storage for it, as
    // GameFolderActivity.home() does (a Redmi Note 8: launch.txt was written there and never read, "No game folder at /game")
    const std::string ext = SDL_GetAndroidExternalStoragePath() ? SDL_GetAndroidExternalStoragePath() : internal;
    // the runtime logs to stderr: into log.txt in the app's external folder (Share log reads it there)
    if (!ext.empty()) {
        std::freopen((ext + "/log.txt").c_str(), "w", stderr);
        std::setvbuf(stderr, nullptr, _IOLBF, 0);
    }
    auto launch = ReadKeyValues(ext + "/launch.txt");
    // env=NAME=value NAME2=value: variables for this run (test runs over adb: MH4U_ADPF=0, MH4N_GPU_COUNTERS=1, ...)
    if (launch.count("env")) {
        std::istringstream vars(launch["env"]);
        std::string var;
        while (vars >> var) {
            const auto eq = var.find('=');
            if (eq != std::string::npos && eq > 0) setenv(var.substr(0, eq).c_str(), var.substr(eq + 1).c_str(), 1);
        }
    }
    const std::string game = launch.count("game") ? launch["game"] : ext + "/game";
    const std::string code_src = launch.count("code") ? launch["code"] : ext + "/libmh4u_code_native.so";
    const std::string user = launch.count("user") ? launch["user"] : ext + "/user";
    // settings=<folder>: the app's own options (mh4u-app.ini) of this run, a test run's (the player's are in the external
    // files folder)
    app_settings_init(launch.count("settings") ? launch["settings"] : ext);
    // the single-screen mod (a code mod that is on in the user folder's load/mods, GameFolderActivity.syncMods or the
    // settings' Mods page): the game's touch menus are on the top screen, so the touch screen's corner starts hidden
    {
        const auto mods = mh4n::mods::Enabled(mh4n::mods::Folder(user), kContentTitle);
        // (MH4U_TOUCH_START=hidden|shown in launch.txt's env=: a test run's start, whatever the library)
        const char* start = std::getenv("MH4U_TOUCH_START");
        if (start != nullptr && (std::strcmp(start, "hidden") == 0 || std::strcmp(start, "shown") == 0)) {
            ui_set_bottom_default(std::strcmp(start, "shown") == 0);
        } else if (std::any_of(mods.begin(), mods.end(), [](const mh4n::mods::Mod& m) { return m.code; })) {
            ui_set_bottom_default(false);
        }
    }
    const std::string code = internal + "/libmh4u_code_native.so";
    mkdir(user.c_str(), 0770);
    game_options_init(user);
    content_init(user);  // (the settings' Mods and Cheats pages)
    // the port's options (the hooks read <user>/mh4u.ini): the first start writes this phone's (first_run.h: a weak
    // phone in Low-end mode at 30 FPS, the others at 60; the picture as wide as the screen); afterwards the player's own
    // file only gets a key the settings show with the app's default that the app's own file lacks (first_run_fill)
    if (!FileExists(user + "/mh4u.ini")) first_run_write(user + "/mh4u.ini", first_run_probe());
    else first_run_fill(user + "/mh4u.ini");
    Logf("game %s, code %s, user %s", game.c_str(), code_src.c_str(), user.c_str());
    {
        auto ini = ReadKeyValues(user + "/mh4u.ini");
        float fps = 30;
        for (const auto& [k, v] : ini) {
            std::string key = k;
            while (!key.empty() && key.back() == ' ') key.pop_back();
            if (key == "fps") fps = static_cast<float>(std::atof(v.c_str()));
        }
        CallActivity("setFrameCap", "(F)V", static_cast<double>(fps > 0 ? fps : 30));  // (a float goes as a double)
        perf_hint_set_fps(fps > 0 ? fps : 30);
    }

    SDL_Window* window = SDL_CreateWindow("MH4U", 0, 0, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr) {
        Logf("SDL_CreateWindow: %s", SDL_GetError());
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        Logf("SDL_CreateRenderer: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    {
        float fps = 30;
        for (const auto& [k, v] : ReadKeyValues(user + "/mh4u.ini")) {
            std::string key = k;
            while (!key.empty() && key.back() == ' ') key.pop_back();
            if (key == "fps") fps = static_cast<float>(std::atof(v.c_str()));
        }
        display_set_frame_rate(window, fps > 0 ? fps : 30);
    }
    int win_w = 0, win_h = 0;
    SDL_GetRenderOutputSize(renderer, &win_w, &win_h);
    Logf("window %dx%d, renderer %s", win_w, win_h, SDL_GetRendererName(renderer));
    // The screen's own size for the shape and the 3D resolution: the window can be smaller here, before the system
    // bars are hidden (the Anbernic's came 960x678 on a 960x720 screen, and the picture kept bars above and below)
    int screen_w = win_w, screen_h = win_h;
    if (const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(window))) {
        if (mode->w > 0 && mode->h > 0) {
            screen_w = std::max(mode->w, mode->h);
            screen_h = std::min(mode->w, mode->h);
        }
    }
    Logf("screen %dx%d", screen_w, screen_h);
    g_top_aspect = TopAspectFromIni(user + "/mh4u.ini", screen_w, screen_h);
    Logf("top screen shape %.4f (0: the picture's)", g_top_aspect);
    ui_init(window, renderer, ext);
    {
        const char* cpu = std::getenv("MH4U_PRESENT_CPU");
        g_want_shared = shared_screens_init(renderer) && !(cpu != nullptr && *cpu == '1');
        g_publish_event = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (g_publish_event >= 0) SDL_AddEventWatch(WakeMainLoop, nullptr);
        Logf("screens: %s", g_want_shared ? "straight from the renderer's buffers" : "pixels read back and uploaded");
    }
    // the controllers and a keyboard (the maps of controllers the player made are kept beside mh4u-app.ini); the touch
    // screen for the input script of a test run (MH4U_TEST_INPUT)
    controls_init(ext, [](bool down, int x, int y) {
        auto* rt = g_runtime.load();
        auto live = rt ? mh4n::input_live::Find(*rt) : nullptr;
        if (!live) return;
        if (down) live->SetTouch(mh4n::input_live::TouchPoint{static_cast<u16>(x), static_cast<u16>(y)});
        else live->SetTouch(std::nullopt);
    });
    perf_hint_thread(PERF_RENDER);
    // the game's frames start on its VBlank and the VBlank follows the display's vsync (hooks.cpp UpdateFrameTiming,
    // display_vsync.cpp): one picture a vsync, without the beat of three clocks; the limiter's spin is a sleep
    // (launch.txt's env= can set MH4U_PACE=limiter, MH4U_LIMITER_SLEEP=0, MH4U_VBLANK_LOCK=off: as before)
    setenv("MH4U_PACE", "vblank", 0);
    setenv("MH4U_LIMITER_SLEEP", "1", 0);
    // the cartridge's modelled file delays at a quarter (D26, D71; fs FsState::Delay): a load's longest frame on the
    // three phones 120-153 -> 49-90 ms, the game's scenes as at 100 % on the PC runner (pacing.md, section 13); the
    // runners keep 100 %. launch.txt's env= can set MH4N_IO_LATENCY_SCALE=100: as before
    setenv("MH4N_IO_LATENCY_SCALE", "25", 0);
    display_vsync_start();
    present_pacing_set_deadline_query([] { return CallActivityLong("presentationDeadlineNs"); });
    gpu_driver_setup();  // (the driver the game's Vulkan goes through: the system's, or the player's own for an Adreno)
    crash_screen_install();  // (a crash: the error screen with Share log, instead of the app vanishing)
    app_live_start();        // (the shadows and the reverb as mh4u-app.ini has them)

    std::string problem;
    if (!FileExists(game + "/exheader.bin")) problem = "No game folder at " + game;
    else if (!CopyIfChanged(code_src, code)) problem = "No code library at " + code_src;

    // the game, on its own thread
    std::thread game_thread;
    if (problem.empty()) {
        Port::Hooks::SetWindowSize(static_cast<std::uint32_t>(std::max(screen_w, screen_h)),
                                   static_cast<std::uint32_t>(std::min(screen_w, screen_h)));
        std::vector<std::string> args = {"mh4u", game, "--code", code, "--user", user, "--gpu", "native-display",
                                         "--input-source", "live", "--audio", "sdl",
                                         // (--frames without --ms: no time limit; this frame is years away)
                                         "--frames", "4000000000"};
        // the settings that apply at start (mh4u-app.ini): the sound's volume, the 3D resolution, the renderer's thread
        std::string render_thread = "on";
        int render_percent = kRenderScaleDefault;  // (the phone's own resolution unless the player chose less)
        app_set_window_size(screen_w, screen_h);
        {
            std::ifstream app_ini(app_settings_path());
            std::string line, section;
            while (std::getline(app_ini, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                if (!line.empty() && line[0] == '[') section = line.substr(1, line.find(']') - 1);
                const auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                std::string key = line.substr(0, eq), value = line.substr(eq + 1);
                while (!key.empty() && key.back() == ' ') key.pop_back();
                while (!value.empty() && value.front() == ' ') value.erase(0, 1);
                if (section == "Audio" && key == "Volume") {
                    args.push_back("--volume");
                    args.push_back(std::to_string(std::clamp(std::atoi(value.c_str()), 0, 100)));
                }
                if (section == "Graphics" && key == "RenderScale") {  // the 3D resolution: % of the phone's, 0 the 3DS's
                    render_percent = std::clamp(std::atoi(value.c_str()), 0, 100);
                }
                if (section == "Graphics" && key == "RenderThread") {  // the renderer on a thread of its own (on|off)
                    render_thread = value == "off" ? "off" : "on";
                }
            }
        }
        int scale_times = render_scale_times(render_percent);
        // an Adreno with a Vulkan 1.1 driver misdraws the 3D picture below 720 lines (coloured bands, an Adreno 630's
        // 2018 driver, 4u-native #7; our MHGU port saw it too): 3x at least there
        {
            const std::string gpu = first_run_gpu_name();
            const uint32_t api = first_run_gpu_api();
            if (gpu.find("Adreno") != std::string::npos && api != 0 && api < ((1u << 22) | (2u << 12)) && scale_times < 3) {
                Logf("%s with a Vulkan 1.1 driver: 3D resolution 3x at least (%dx misdraws there)", gpu.c_str(), scale_times);
                scale_times = 3;
            }
        }
        args.push_back("--scale");
        args.push_back(std::to_string(scale_times));
        Logf("3D resolution %d %% of %dx%d: %dx the 3DS's", render_percent, screen_w, screen_h, render_scale_times(render_percent));
        // the game's dither pattern is a fine grid over everything above 1x: off there unless mh4u.ini says otherwise
        if (render_scale_times(render_percent) > 1) {
            bool dither_set = false;
            for (const auto& [k, v] : ReadKeyValues(user + "/mh4u.ini")) {
                std::string key = k;
                while (!key.empty() && key.back() == ' ') key.pop_back();
                dither_set |= key == "dither";
            }
            if (!dither_set) setenv("MH4U_DITHER", "0", 1);
        }
        args.push_back("--render-thread");
        args.push_back(render_thread);
        // PowerVR's shader compiler crashes building the ubershader (libusc.so, a GE8300): no background builds there, a
        // missing pipeline is built when it is first drawn (tested with --no-ubershader on a Mali: the same picture)
        // (and no list compiled on a worker at start either: its compiler also crashed building the saved list while the
        // renderer built its own, so every pipeline is built on the renderer thread alone; the driver's cache stays)
        if (first_run_gpu_name().find("PowerVR") != std::string::npos) {
            args.push_back("--no-ubershader");
            args.push_back("--no-pipeline-list");
            Logf("PowerVR: pipelines built one at a time when first drawn, no ubershader, no list at start");
        }
        // the renderer's thread joins the performance-hint session as the game's does (it carries the frame as much;
        // PERF_SUBMIT would join only after reporting frame after frame)
        mh4n::gpu::queue::SetRendererThreadStartHook([] { perf_hint_thread(PERF_GAME); });
        if (launch.count("args")) {
            std::istringstream extra(launch["args"]);
            std::string a;
            while (extra >> a) args.push_back(a);
        }
        // The controls reach the game at its next HID update (about 4 ms), not at the next boundary of a game frame
        // (--input-latch frame, the runner's default, D59: a recorded session then replays exactly). That boundary (the
        // frame counter's change) falls in the middle of a frame's work, after the game read its pad, and the game reads
        // it again only after its next wait for the VBlank: a press waited from one to two frames (33 to 67 ms at 30 FPS)
        // more than the game's own reading needs. Not with --record-input or an --input-latch of launch.txt's, nor with
        // MH4U_INPUT_LATCH=frame.
        {
            const bool given = std::any_of(args.begin(), args.end(), [](const std::string& a) {
                return a == "--input-latch" || a == "--record-input";
            });
            const char* latch = std::getenv("MH4U_INPUT_LATCH");
            if (!given) {
                args.push_back("--input-latch");
                args.push_back(latch != nullptr && std::strcmp(latch, "frame") == 0 ? "frame" : "none");
            }
        }
        game_thread = std::thread([args] {
            mh4n::SetHostThreadName("mh4u-game");
            perf_hint_thread(PERF_GAME);  // (the guest's code and the renderer both run here)
            std::vector<const char*> argv;
            for (const auto& a : args) argv.push_back(a.c_str());
            const mh4n::Options options = mh4n::ParseOptions("runner", static_cast<int>(argv.size()), argv.data());
            int status = 2;
            if (options.ok()) {
                mh4n::Runtime runtime;
                status = runtime.Boot(options);
            }
            Logf("the game ended with status %d", status);
            g_game_status.store(status);
            g_runtime.store(nullptr);
            g_game_done.store(true);
        });
    } else {
        Logf("%s", problem.c_str());
    }

    // the two screens as the window shows them (shared_screens.h): a screen whose picture did not change (the game at
    // 30 FPS, the touch screen between menus) is not taken again, and the touch screen only when it is shown
    SharedScreenView views[2];
    SharedScreenView& top_view = views[0];
    SharedScreenView& bottom_view = views[1];
    u64 shown_sequence = 0;
    u32 shown_game_frame = 0;
    bool bottom_shown = true;
    bool running = true;
    bool quitting = false;  // (the player closed the app: the game's end is no error)
    bool paused_in_background = false;  // (the game's clock stopped while the app was away)
    SDL_FingerID touch_finger = 0;
    bool touching = false;
    DisplayOptions display = ReadDisplayOptions(app_settings_path());
    Layout layout = ComputeLayout(win_w, win_h, 400, 240, 320, 240, bottom_shown, display);
    mh4n::InputState last_sent;

    auto touch_at = [&](float x, float y, bool down) {
        auto* rt = g_runtime.load();
        if (rt == nullptr) return;
        auto live = mh4n::input_live::Find(*rt);
        if (!live) return;
        if (!down) {
            live->SetTouch(std::nullopt);
            return;
        }
        const SDL_FRect& b = layout.bottom;
        const float u = (x - b.x) / b.w, v = (y - b.y) / b.h;
        live->SetTouch(mh4n::input_live::TouchPoint{static_cast<u16>(std::clamp(u, 0.0f, 0.999f) * 320),
                                                    static_cast<u16>(std::clamp(v, 0.0f, 0.999f) * 240)});
    };
    auto inside_bottom = [&](float x, float y) {
        const SDL_FRect& b = layout.bottom;
        return layout.bottom_shown && x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
    };

    int town_touch_last = -1, town_touch_x = 0, town_touch_y = 0;  // (the town's tap last given: town.h)
    // the controllers', the keyboard's and the touch controls' state, to the game when it changed
    auto send_input = [&] {
        const UiInput ui = ui_take_input();
        const ControlsInput pad = controls_input(shown_game_frame);
        mh4n::InputState in;
        in.buttons = pad.buttons | ui.buttons;
        const bool ui_circle = ui.circle_x != 0 || ui.circle_y != 0, ui_cstick = ui.cstick_x != 0 || ui.cstick_y != 0;
        in.circle_x = ui_circle ? ui.circle_x : pad.circle_x;  // (the circle pad is positive upwards)
        in.circle_y = ui_circle ? ui.circle_y : pad.circle_y;
        in.cstick_x = ui_cstick ? ui.cstick_x : pad.cstick_x;
        in.cstick_y = ui_cstick ? ui.cstick_y : pad.cstick_y;
        in.zl = pad.zl || ui.zl;
        in.zr = pad.zr || ui.zr;
        // (the port's kick, target and skip buttons see ZL and ZR even with the game's Circle Pad Pro option off)
        Port::Hooks::SetHostTriggers(in.zl, in.zr);
        const float yaw = ui.cam_yaw + pad.cam_yaw, pitch = ui.cam_pitch + pad.cam_pitch;
        if (yaw != 0 || pitch != 0) {
            Port::Hooks::AddCameraTurn(yaw, pitch);
        }
        // the town's actions (town.h): a tap on the touch screen where the town's panel has one, and the large touch
        // screen's cursor and Back; a finger on the touch screen has it first
        in.buttons |= town_buttons();
        {
            int tx = 0, ty = 0;
            const int t = touching ? -1 : town_touch(&tx, &ty);
            if (t != town_touch_last || (t == 1 && (tx != town_touch_x || ty != town_touch_y))) {
                if (auto* rt = g_runtime.load()) {
                    if (auto live = mh4n::input_live::Find(*rt)) {
                        if (t == 1) live->SetTouch(mh4n::input_live::TouchPoint{static_cast<u16>(tx), static_cast<u16>(ty)});
                        else if (town_touch_last == 1 && !touching) live->SetTouch(std::nullopt);
                    }
                }
                town_touch_last = t, town_touch_x = tx, town_touch_y = ty;
            }
        }
        if (auto* rt = g_runtime.load(); rt != nullptr && !(in == last_sent)) {
            if (auto live = mh4n::input_live::Find(*rt)) {
                live->Set(in);
                last_sent = in;
                mh4n::pacing::Mark(mh4n::pacing::kInputSet, in.buttons);  // (the pacing trace: a press's way to the screen)
            }
        }
    };
    // Presenting costs the phone (the UI drawn, the buffer queued to the system): a loop turn presents when there
    // is something to show (a new picture of the game, an input, the menu open), and at 30 a second for a moment
    // after an input (held buttons and sticks); otherwise it sends the input and waits for an event a moment.
    u64 last_present_ms = 0, last_input_ms = 0, last_frame_present_ms = 0;
    bool new_picture = false;
    bool new_frame = false;     // (the top screen's picture changed since the last present: a new game frame)
    bool bottom_dirty = false;  // (the touch screen changed and is not presented yet)
    while (running) {
        bool had_events = false;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            had_events = true;
            last_input_ms = SDL_GetTicks();
            switch (e.type) {  // (the pacing trace: when an input came, and how long SDL had it)
            case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP:
            case SDL_EVENT_GAMEPAD_AXIS_MOTION: case SDL_EVENT_JOYSTICK_BUTTON_DOWN: case SDL_EVENT_JOYSTICK_BUTTON_UP:
            case SDL_EVENT_JOYSTICK_AXIS_MOTION: case SDL_EVENT_JOYSTICK_HAT_MOTION: case SDL_EVENT_FINGER_DOWN: case SDL_EVENT_FINGER_UP:
            case SDL_EVENT_FINGER_MOTION:
                if (mh4n::pacing::Trace::Get().On()) {
                    const Uint64 t = SDL_GetTicksNS();
                    mh4n::pacing::Mark(mh4n::pacing::kAppInput,
                                       static_cast<std::uint32_t>(t > e.common.timestamp ? (t - e.common.timestamp) / 1000 : 0));
                }
                break;
            default:
                break;
            }
            // (the controllers and the keyboard: controls.cpp keeps their state, and takes START with SELECT)
            if (controls_event(e) || ui_event(e, UiScreens{layout.top, layout.bottom, layout.bottom_shown})) {
                continue;
            }
            switch (e.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_TERMINATING:
                running = false;
                quitting = true;
                break;
            case SDL_EVENT_DID_ENTER_BACKGROUND:
                // the game stops where it is while the app is away (its clock stands still: no time is made up), unless
                // the player wants it to go on ([Interface] PauseInBackground off: a hunt with others online)
                if (auto* rt = g_runtime.load(); rt != nullptr && app_setting_bool("Interface", "PauseInBackground", true)) {
                    rt->Clock().Pause();
                    paused_in_background = true;
                }
                break;
            case SDL_EVENT_DID_ENTER_FOREGROUND:
                display_frame_rate_refresh();  // (the surface may be a new one)
                if (auto* rt = g_runtime.load(); rt != nullptr && paused_in_background) rt->Clock().Resume();
                paused_in_background = false;
                break;
            case SDL_EVENT_FINGER_DOWN: {
                const float x = e.tfinger.x * win_w, y = e.tfinger.y * win_h;
                if (!touching && inside_bottom(x, y)) {
                    touching = true;
                    touch_finger = e.tfinger.fingerID;
                    touch_at(x, y, true);
                }
                break;
            }
            case SDL_EVENT_FINGER_MOTION:
                if (touching && e.tfinger.fingerID == touch_finger) {
                    touch_at(e.tfinger.x * win_w, e.tfinger.y * win_h, true);
                }
                break;
            case SDL_EVENT_FINGER_UP:
            case SDL_EVENT_FINGER_CANCELED:
                if (touching && e.tfinger.fingerID == touch_finger) {
                    touching = false;
                    touch_at(0, 0, false);
                }
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                display_frame_rate_refresh();
                SDL_GetRenderOutputSize(renderer, &win_w, &win_h);
                break;
            case SDL_EVENT_RENDER_DEVICE_RESET:
                // the GL context was lost: every texture of the screens is made again from the next picture
                shared_screens_reset(views, 2);
                shown_sequence = 0;
                Logf("the renderer's device was reset: the screens' textures are made again");
                break;
            default:
                break;
            }
        }
        if (g_game_done.load()) {
            running = false;
        }
        // the controls to the game now, before this turn draws and presents (a present can wait for a buffer of the
        // window's: the press would wait with it)
        if (had_events) send_input();

        // the newest screens (taken under the lock, shown outside it: the renderer publishes meanwhile). A shared
        // picture is drawn from the renderer's buffer, CPU pixels are uploaded.
        {
            ScreenPicture top, bottom;
            {
                std::lock_guard lock(g_screens.mutex);
                if (g_screens.sequence != shown_sequence) {
                    shown_sequence = g_screens.sequence;
                    if (g_screens.game_frame != shown_game_frame) {
                        shown_game_frame = g_screens.game_frame;
                        g_present_count = g_present_count + 1;
                    }
                }
                top = g_screens.top;
                bottom = g_screens.bottom;
            }
            const auto take = [&](SharedScreenView& view, const ScreenPicture& p) {
                if (p.shared != nullptr) {
                    if (p.shared != view.picture && shared_screens_show(view, p.shared, p.region)) new_picture = true;
                } else if (p.pixels != nullptr && p.pixels != view.uploaded) {
                    shared_screens_show_pixels(view, p.pixels, static_cast<int>(p.w), static_cast<int>(p.h));
                    new_picture = true;
                }
            };
            {
                const void* before = top_view.picture.get();
                take(top_view, top);
                if (top_view.picture.get() != before) {
                    mh4n::pacing::Mark(mh4n::pacing::kAppPicture, shown_game_frame);
                    new_frame = true;
                }
            }
            // The touch screen's new picture waits for the top screen's of the same game frame when that one is still on
            // the GPU (it is smaller and comes first): one present a game frame instead of two. On a Mali-G52 at 3x, with
            // the touch screen shown, the window presented twice a game frame and its GPU work slowed the game's frames.
            if (bottom_shown || bottom_view.texture == nullptr) {
                const bool before = new_picture;
                new_picture = false;
                take(bottom_view, bottom);
                bottom_dirty = bottom_dirty || new_picture;
                new_picture = before;
            }
            if (bottom_dirty && (new_picture || top_view.pending_count == 0)) {
                new_picture = true;
                bottom_dirty = false;
            }
        }
        SDL_FRect town_rect{};
        float town_alpha = 1;
        const bool town_large = town_view(win_w, win_h, &town_rect, &town_alpha);  // (the town's action's screen: town.h)
        bottom_shown = ui_bottom_wanted() || town_large;
        g_touch_hidden.store(!bottom_shown, std::memory_order_relaxed);
        if (settings_panel_closed_now()) {
            display = ReadDisplayOptions(app_settings_path());  // (the settings may have moved the touch screen)
        }
        layout = ComputeLayout(win_w, win_h, static_cast<u32>(top_view.width), static_cast<u32>(top_view.height),
                               static_cast<u32>(bottom_view.width), static_cast<u32>(bottom_view.height), bottom_shown, display);
        if (town_large) {
            layout.bottom = town_rect, layout.bottom_shown = true;
            layout.bottom_alpha = static_cast<u8>(std::clamp(town_alpha, 0.0f, 1.0f) * 255);
        }
        // the game's state for the touch controls (the menu or the hunt layout, no dash with the weapon out, a bow's or
        // a bowgun's layout, the item bar where it is drawn): the hooks work it out once a game frame
        {
            const Port::Hooks::GameState game = Port::Hooks::CurrentGameState();
            UiGameState state;
            state.menu = game.menu;
            state.weapon_drawn = game.weapon_drawn;
            state.ranged = game.ranged;
            state.bow_type = game.bow_type;
            state.item_bar_shown = game.item_bar_shown;
            state.top_width = game.top_width;
            state.place = game.place;
            state.action = game.action;
            const SDL_FRect& t = layout.top;  // (fractions of the top screen's picture, to the window's pixels)
            const auto to_window = [&t](const float* f) {
                return SDL_FRect{t.x + f[0] * t.w, t.y + f[1] * t.h, (f[2] - f[0]) * t.w, (f[3] - f[1]) * t.h};
            };
            if (game.item_bar_shown == 1) state.item_bar = to_window(game.item_bar);
            static_assert(UiGameState::HUD_PARTS == Port::Hooks::HUD_PART_COUNT, "the HUD's parts");
            for (int k = 0; k < UiGameState::HUD_PARTS; ++k) {
                state.hud[k].shown = game.hud[k].shown;
                state.hud[k].seen = game.hud[k].seen;
                state.hud[k].r = to_window(game.hud[k].rect);
                state.hud[k].dx = game.hud[k].offset[0], state.hud[k].dy = game.hud[k].offset[1];
            }
            ui_set_game_state(state);
            town_set_game(game.place, game.menu, game.frame, game.town_panel);
        }
        {
            const u64 now = SDL_GetTicks();
            const bool recent_input = now - last_input_ms < 400;
            const bool due = now - last_present_ms >= (recent_input ? 33u : 250u);
            // MH4U_DEBUG_NO_PRESENT=1 (a measurement aid): nothing is drawn or presented, the game runs alone
            static const bool no_present = [] {
                const char* v = std::getenv("MH4U_DEBUG_NO_PRESENT");
                return v != nullptr && *v == '1';
            }();
            // A present with nothing new from the game while its frames come (one presented in the last 40 ms, or one
            // waiting for its vsync with the FPS limit's pacing) only takes another of the window's buffers: the touch
            // controls' look goes with the next frame's present, within a frame, and their input is taken now
            // (ui_update). A window with all its buffers waiting made the present wait for one to come back, and the next
            // frame's present and the controls with it (a Moto g06 at 30 FPS: 28 ms, an Anbernic GPU-bound at 52: 16 ms).
            const bool nothing_new = !new_picture && !ui_settings_open() && problem.empty();
            const bool frames_come = now - last_frame_present_ms < 40 || present_pacing_frame_pending();
            if (no_present || (nothing_new && ((!had_events && !due) || frames_come))) {
                if (had_events && !no_present) {
                    ui_update(win_w, win_h, UiScreens{layout.top, layout.bottom, layout.bottom_shown}, controls_real_pad());
                }
                send_input();
                // (a sleep, not SDL_WaitEventTimeout: on Android that polls the controllers and the clock in a loop and
                // kept a core busy). It ends at once when a picture is published, when the renderer's GPU finishes the
                // picture a frame of the window waits for, and when an event comes (WakeMainLoop); otherwise after 4 ms
                // for a moment after an input (the touch controls' timed presses end on time) and 16 ms else (it was 3
                // ms always: some 300 wakeups a second for nothing, a core kept out of its idle state; MH4U_LOOP_WAIT=ms)
                if (no_present || g_publish_event < 0) {
                    SDL_Delay(3);
                } else {
                    int waits[3];
                    const int n = shared_screens_wait_fds(views, 2, waits, 3);
                    pollfd fds[4] = {{g_publish_event, POLLIN, 0}};
                    for (int i = 0; i < n; ++i) fds[1 + i] = pollfd{waits[i], POLLIN, 0};
                    static const int wait_ms = [] {
                        const char* v = std::getenv("MH4U_LOOP_WAIT");
                        return v != nullptr ? std::clamp(std::atoi(v), 1, 50) : 0;
                    }();
                    const int ms = wait_ms > 0 ? wait_ms : now - last_input_ms < 400 ? 4 : 16;
                    if (poll(fds, static_cast<nfds_t>(1 + n), ms) > 0 && (fds[0].revents & POLLIN) != 0) {
                        uint64_t count = 0;
                        (void)!read(g_publish_event, &count, sizeof(count));
                    }
                }
                continue;
            }
            last_present_ms = now;
            new_picture = false;
        }
        shared_screens_frame_begin();
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        // (a render target's picture is turned upright as it is drawn: shared_screens.h)
        {  // (the top screen's first row is an edge the game never draws: a strip of stale colours, a whole row at 1x
           // (4u-native #9; a third of one at 3x); drawn one 3DS row taller and that row above the window, when it is at its top)
            SDL_FRect top = layout.top;
            if (top.y < 0.5f) {
                const float row = top.h / 240.0f * 2;  // (two: the filtering blends the undrawn row into the next)
                top.y -= row;
                top.h += row;
            }
            shared_screens_draw(top_view, top, win_w, win_h);
        }
        shared_screens_check(top_view, layout.top, win_w, win_h, ext.c_str());
        if (layout.bottom_shown) {
            if (town_large) {  // (the top screen dimmed under the town's large touch screen)
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, static_cast<u8>(160 * std::clamp(town_alpha, 0.0f, 1.0f)));
                SDL_RenderFillRect(renderer, nullptr);
            }
            shared_screens_draw(bottom_view, layout.bottom, win_w, win_h, layout.bottom_alpha);
        }
        if (!problem.empty()) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_SetRenderScale(renderer, 3, 3);
            SDL_RenderDebugText(renderer, 10, 10, problem.c_str());
            SDL_SetRenderScale(renderer, 1, 1);
        }
        // MH4U_DEBUG_NO_UI=1 (a measurement aid): the screens alone, without the touch controls and the menus
        static const bool no_ui = [] { const char* v = std::getenv("MH4U_DEBUG_NO_UI"); return v != nullptr && *v == '1'; }();
        if (!no_ui) ui_frame(win_w, win_h, UiScreens{layout.top, layout.bottom, layout.bottom_shown}, controls_real_pad());
        send_input();  // (the touch controls' input of this frame now: the present may wait for a buffer)
        // A test run (launch.txt's args with a scripted input) ignores the controls: say so on the screen, or a player who
        // picks the phone up thinks the game is broken
        // (a mixed one, a script and the controls together, a short band: the controls are tested then)
        static const bool script_run = launch.count("args") && launch["args"].find("--input-source script") != std::string::npos;
        static const bool mixed_run = launch.count("args") && launch["args"].find("--input-source mixed") != std::string::npos;
        if (script_run || mixed_run) {
            const char* text = mixed_run ? (settings_spanish() ? "PRUEBA AUTOMATICA" : "AUTOMATED TEST")
                               : settings_spanish() ? "PRUEBA AUTOMATICA: los controles no responden"
                                                    : "AUTOMATED TEST: the controls are ignored";
            const float s = 2.0f, w = static_cast<float>(std::strlen(text)) * 8 * s + 24, h = 8 * s + 16;
            const SDL_FRect band{(win_w - w) / 2, win_h - h - 12.0f, w, h};
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 60, 14, 10, 220);
            SDL_RenderFillRect(renderer, &band);
            SDL_SetRenderDrawColor(renderer, 226, 182, 92, 255);
            SDL_RenderRect(renderer, &band);
            SDL_SetRenderScale(renderer, s, s);
            SDL_RenderDebugText(renderer, (band.x + 12) / s, (band.y + 8) / s, text);
            SDL_SetRenderScale(renderer, 1, 1);
        }
        {
            const u64 start = SDL_GetTicksNS();
            shared_screens_frame_end();
            present_pacing_before_present(new_frame, perfstats_fps_cap());
            if (new_frame) last_frame_present_ms = SDL_GetTicks();
            new_frame = false;
            mh4n::pacing::Mark(mh4n::pacing::kAppPresentIn);
            SDL_RenderPresent(renderer);
            mh4n::pacing::Mark(mh4n::pacing::kAppPresentOut);
            shared_screens_note_present((SDL_GetTicksNS() - start) / 1000);
        }
        shared_screens_frame_done();
        perf_hint_frame();
        perfstats_poll();

        send_input();
    }

    if (auto* rt = g_runtime.load()) {
        rt->RequestExit(0, "the app is closing");
    }
    // the screens' pictures go before the game does (the renderer's buffers are its own)
    shared_screens_shutdown(views, 2);
    {
        std::lock_guard lock(g_screens.mutex);
        g_screens.top = ScreenPicture{};
        g_screens.bottom = ScreenPicture{};
    }
    if (game_thread.joinable()) game_thread.join();
    // the game stopped by itself with an error: the screen that shares the log, in its own process
    if (!quitting && (g_game_status.load() != 0 || !problem.empty())) {
        std::fflush(stderr);
        CallActivity("crashReport", "()V");
    }
    ui_shutdown();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return g_game_status.load();
}
