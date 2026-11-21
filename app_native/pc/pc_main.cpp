// MH4U for PC (MH4U.exe, Windows x64): the program's main, the PC's android_main.cpp. SDL3 gives the window, the events,
// the controllers and the sound; the game runs on the native runtime (mh4n) on a thread of its own, with the native
// renderer, and its two screens come back here as pixels that this file draws in the window (pc_screens.cpp) with the
// app's interface over them (ui.cpp: the settings, the controllers and the keyboard). The game folder is chosen on the
// game folder screen first (pc_launcher.cpp); where everything else is kept: pc_app.h.
//
// Tests (the environment, as Android's launch.txt env=): MH4U_HOME and MH4U_SAVE_FOLDER (pc_app.h); MH4U_ARGS, more
// arguments for the runtime; MH4U_TEST_INPUT, controls.h's input script, whose KEY steps (f<frame>:KEY:<SDL's name of
// a key, '_' for a space>:f<frames>) are keys pressed on the keyboard as SDL gives them, and PADAXIS steps (f<frame>:PADAXIS:<SDL's name
// of a gamepad axis>:<-1..1>:f<frames>) the sticks of the test gamepad (MH4U_TEST_JOYSTICK=pad); MH4U_TEST_SHOTS, game
// frames ("f400,f2900") at which the window is saved as a PNG into MH4U_TEST_SHOT_DIR; MH4U_TEST_QUIT, the game frame
// the program closes at.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <functional>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "hooks.h"
#include "app_live.h"
#include "app_settings.h"
#include "app_version.h"
#include "cache_clear.h"
#include "colour_filter.h"
#include "common/utf8.h"
#include "content.h"
#include "controls.h"
#include "crash_screen.h"
#include "diag.h"
#include "first_run.h"
#include "frame_gen.h"
#include "fsr1.h"
#include "present_pacing.h"
#include "mh4n/pacing_trace.h"
#include "game_options.h"
#include "mods/mod_list.h"
#include "pc_app.h"
#include "pc_input.h"
#include "pc_net.h"
#include "pc_display.h"
#include "pc_hdr.h"
#include "pc_perf.h"
#include "pc_screens.h"
#include "pc_stats.h"
#include "pc_vsync.h"
#include "perf_hint.h"
#include "settings_pc.h"
#include "gpu/backend/driver_hooks.h"
#include "perfstats.h"
#include "quick_items.h"
#include "services/fs/dump_source.h"
#include "settings_panel.h"
#include "shared_screens.h"
#include "town.h"
#include "ui.h"
#include "verify_files.h"
#include "input_live/live_input.h"
#include "mh4n/host_thread_name.h"
#include "mh4n/options.h"
#include "mh4n/present.h"
#include "mh4n/registry.h"
#include "mh4n/rooms.h"
#include "mh4n/runtime.h"

// windows.h last: it defines macros (Yield() is an empty one) that would break the project's declarations above it
#include <windows.h>
#include <io.h>
#include <shellapi.h>

namespace fs = std::filesystem;

// The game's frames shown (perfstats.cpp counts them over half a second)
extern "C" {
volatile uint64_t g_present_count = 0;
}

namespace {

using mh4n::u16;
using mh4n::u32;
using mh4n::u64;
using mh4n::u8;

void Logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void Logf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::fputs("[app] ", stderr);
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
    va_end(ap);
}

// ---- folders and files ---------------------------------------------------------------------------

PcPaths g_paths;
std::string g_game_folder;  // (pc_game_folder)
bool g_restart = false;            // (the settings asked for the game folder screen, or a restart)
const wchar_t* g_restart_arg = L"";  //   its argument: --choose-folder (the game folder screen), --builder (its builder)

fs::path P(const std::string& utf8) { return mh4n::utf8::PathFromUtf8(utf8); }

std::string Env(const char* name) {
    const wchar_t* v = _wgetenv(mh4n::utf8::ToWide(name).c_str());
    return v != nullptr ? mh4n::utf8::FromWide(v) : std::string();
}

// (the CRT's environment, which the runtime's getenv reads; only when not set already, unless `force`)
void SetEnv(const char* name, const std::string& value, bool force = true) {
    if (!force && !Env(name).empty()) return;
    _wputenv_s(mh4n::utf8::ToWide(name).c_str(), mh4n::utf8::ToWide(value).c_str());
}

bool Exists(const std::string& path) {
    std::error_code ec;
    return fs::exists(P(path), ec);
}

void MakeDirs(const std::string& path) {
    std::error_code ec;
    fs::create_directories(P(path), ec);
}

std::string FirstLine(const std::string& path) {
    std::ifstream in(P(path));
    std::string line;
    std::getline(in, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\n')) line.pop_back();
    return line;
}

void WriteText(const std::string& path, const std::string& text) {
    std::ofstream out(P(path), std::ios::binary | std::ios::trunc);
    out << text;
}

std::map<std::string, std::string> ReadKeyValues(const std::string& path) {
    std::map<std::string, std::string> out;
    std::ifstream in(P(path));
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        const auto eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        while (!key.empty() && key.back() == ' ') key.pop_back();
        while (!value.empty() && value.front() == ' ') value.erase(0, 1);
        out[key] = value;
    }
    return out;
}

std::string Trimmed(std::string s) {
    while (!s.empty() && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    return s;
}

void ReloadSaves();

void FindPaths() {
    const char* base = SDL_GetBasePath();
    g_paths.exe_dir = Trimmed(base != nullptr ? base : ".");
    const std::string home = Env("MH4U_HOME");
    g_paths.home = Trimmed(home.empty() ? g_paths.exe_dir : home);
    MakeDirs(g_paths.home);
    g_paths.game_files = g_paths.home + "/game";
    g_paths.user = g_paths.home + "/user";
    ReloadSaves();
}

// The save folder (pc_app.h): MH4U_SAVE_FOLDER (a test), else save_folder.txt's (Save folder), else the game's own
void ReloadSaves() {
    const std::string appdata = Env("APPDATA");
    if (!Env("MH4U_HOME").empty()) {  // (a test's home: all in it)
        g_paths.own_saves = g_paths.home + "/user";
        g_paths.backups = g_paths.home + "/save_backups";
    } else {
        g_paths.own_saves = Trimmed(appdata.empty() ? g_paths.home : appdata) + "/MH4U";
        g_paths.backups = g_paths.own_saves + "/save_backups";
    }
    std::string saves = Env("MH4U_SAVE_FOLDER");
    if (saves.empty()) saves = FirstLine(g_paths.home + "/save_folder.txt");
    g_paths.save_folder = Trimmed(saves.empty() ? g_paths.own_saves : saves);
}

// The runtime and the app log to stderr: into log.txt in the home, the last two sessions' kept as log_prev.txt and
// log_prev2.txt (as Android's)
// Milliseconds since Windows made this process (the start's steps in the log: what a first start waits for before the
// window, the window, the game's first picture)
long long MsSinceProcessStart() {
    FILETIME created{}, exited{}, kernel{}, user{}, now{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return -1;
    GetSystemTimePreciseAsFileTime(&now);
    const auto ticks = [](const FILETIME& f) { return (static_cast<long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    return (ticks(now) - ticks(created)) / 10000;
}

void OpenLog() {
    const std::string log = g_paths.home + "/log.txt", prev = g_paths.home + "/log_prev.txt";
    std::error_code ec;
    fs::rename(P(prev), P(g_paths.home + "/log_prev2.txt"), ec);
    fs::rename(P(log), P(prev), ec);
    if (_wfreopen(mh4n::utf8::ToWide(log).c_str(), L"w", stderr) != nullptr) {
        std::setvbuf(stderr, nullptr, _IONBF, 0);
        SetStdHandle(STD_ERROR_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stderr))));
        diag_log_ready();
    }
}

// Starts MH4U.exe again (a new process: the runtime runs once in one) and lets this one end
void Relaunch(const wchar_t* arg) {
    wchar_t exe[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) return;
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
    if (*arg) cmd += L" " + std::wstring(arg);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(exe, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        if (const std::string pids = Env("MH4U_TEST_PIDS"); !pids.empty()) {  // (a test: the runner waits for it too)
            std::ofstream out(P(pids), std::ios::app);
            out << pi.dwProcessId << "\n";
        }
        Logf("started again%s%s (pid %lu)", *arg ? " " : "", mh4n::utf8::FromWide(arg).c_str(), pi.dwProcessId);
    } else {
        Logf("could not start again (error %lu)", GetLastError());
    }
}

// ---- the screens, from the game thread to the window ---------------------------------------------

struct ScreenPicture {
    std::shared_ptr<const std::vector<u8>> pixels;
    std::shared_ptr<const mh4n::present::SharedPicture> shared;  // (the renderer's buffer: pc_screens.cpp)
    mh4n::present::ImageRegion region;
    u32 w = 0, h = 0;
};

struct Screens {
    std::mutex mutex;
    u64 sequence = 0;
    ScreenPicture top, bottom;
    u32 game_frame = 0;
};
Screens g_screens;
Uint32 g_wake_event = 0;            // (an SDL event of the app's own: a picture came, the main loop's wait ends)
std::atomic<bool> g_wake_pending{false};
// The screens straight from the renderer's buffers (pc_screens.h): on when the window's OpenGL can import them
bool g_want_shared = false;
// The touch screen is hidden (0 shown, 1 hidden, 2 hidden but for the quick items' piece: android_main.cpp's)
std::atomic<int> g_touch_mode{0};
std::atomic<Uint64> g_items_hot_until{0};
std::atomic<bool> g_items_picture{false};
// Counted in the game's frames, not in VBlanks: above 60 FPS the VBlank runs at the limit's rate (1000 a second with no
// limit, several to a frame), and a few VBlanks in 20 could hold no draw of the touch screen at all. The renderer leaves
// the hidden touch screen's draws out as it gets to them, on its own thread and frames behind the game, so a frame's
// touch screen may be drawn under the switch of a later frame: its picture is kept only from the kItemsWindow-th frame
// drawn on, and the touch screen is drawn kItemsTail frames more after the last one kept (else the picture kept holds
// what the target had: the top screen's 3D, MH4U drawing both screens into one target in turns). 1.0.2 (fix102-mirror):
// the 8th of 8 in 30, as Android's (measured here, the 1st to the 4th picture after the touch screen is drawn again can
// be of a turn whose draws were left out), and mh4n's renderer no longer publishes such a turn's touch screen
constexpr int kItemsPeriod = 30, kItemsWindow = 8, kItemsTail = 3;

void Wake() {
    if (g_wake_event == 0 || g_wake_pending.exchange(true)) return;
    SDL_Event e{};
    e.type = g_wake_event;
    SDL_PushEvent(&e);
}

class WindowSink final : public mh4n::present::ScreenSink {
public:
    void Publish(const mh4n::present::DisplayFrame& frame) override {
        pc_perf_game_frame(frame.game_frame);  // ([perf] lines: pc_perf.h)
        last_frame_ = frame.game_frame;        // (ScreenHidden's count: the same thread, at each VBlank)
        {
            std::lock_guard lock(g_screens.mutex);
            Take(frame.top, g_screens.top);
            if (items_accept_) {
                Take(frame.bottom, g_screens.bottom);
                if (frame.bottom) g_items_picture.store(true, std::memory_order_relaxed);
            }
            ++g_screens.sequence;
            g_screens.game_frame = frame.game_frame;
        }
        Wake();
    }
    bool WantsSharedPictures() const override { return g_want_shared; }
    bool ScreenHidden(u32 screen) const override {
        if (screen != 1) return false;
        const int mode = g_touch_mode.load(std::memory_order_relaxed);
        const bool next = last_frame_ != counted_frame_;  // (a frame of the game's since the last VBlank)
        counted_frame_ = last_frame_;
        bool wanted = mode == 0;  // (shown: every frame; hidden: none; for the piece: kItemsWindow in kItemsPeriod)
        if (mode == 2) {
            const bool hot = SDL_GetTicks() < g_items_hot_until.load(std::memory_order_relaxed);
            if (next) ++items_frames_;
            wanted = hot || items_frames_ % kItemsPeriod < kItemsWindow;
        }
        const bool draw = wanted || items_tail_ > 0;
        if (!draw) items_streak_ = 0;
        else if (next || items_streak_ == 0) items_streak_ = std::min(items_streak_ + 1, kItemsWindow);
        items_accept_ = wanted && items_streak_ >= kItemsWindow;  // (what Publish takes of this VBlank's)
        if (items_accept_) items_tail_ = kItemsTail;
        else if (next && items_tail_ > 0) --items_tail_;
        return !draw;
    }

private:
    mutable unsigned items_frames_ = 0;
    mutable int items_streak_ = 0;
    mutable int items_tail_ = 0;
    mutable u32 last_frame_ = 0, counted_frame_ = 0;
    mutable bool items_accept_ = true;
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

std::atomic<mh4n::Runtime*> g_runtime{nullptr};
std::atomic<bool> g_game_done{false};
std::atomic<int> g_game_status{0};

void AttachWindow(mh4n::Runtime& rt) {
    if (auto* display = mh4n::present::FindDisplay(rt.Interfaces().gpu)) {
        display->AddScreenSink(&g_sink);
    } else {
        Logf("the GPU shows no screens: nothing will be drawn");
    }
    g_runtime.store(&rt);
}
MH4N_REGISTER(mh4n::BootStep, "zz.pc.window", mh4n::BootStep{mh4n::Phase::Started, AttachWindow});

// ---- layout (android_main.cpp's, the PC's default: the touch screen beside the top one) -----------

struct Layout {
    SDL_FRect top{}, bottom{};
    // The whole top picture where it would be drawn, its parts outside the window too: the game's places on it (its HUD,
    // the item bar, the damage numbers) are fractions of the whole picture. `crop`: the fraction of its width left out at
    // each side (the top screen at a narrower shape than the picture's: `top` shows its middle).
    SDL_FRect top_full{};
    float crop = 0;
    bool bottom_shown = true;
    u8 bottom_alpha = 255;
};

struct DisplayOptions {
    enum Mode { Overlay, Stacked, Side, Window } mode = Side;  // (Window: the touch screen in a window of its own)
    int corner = 0;
    float size = 0.42f;
    u8 opacity = 255;
};

DisplayOptions ReadDisplayOptions() {
    DisplayOptions d;
    const std::string layout = app_setting("Display", "Layout", "side");
    d.mode = layout == "stacked"   ? DisplayOptions::Stacked
             : layout == "overlay" ? DisplayOptions::Overlay
             : layout == "window"  ? DisplayOptions::Window
                                   : DisplayOptions::Side;
    const std::string corner = app_setting("Display", "Corner", "bottom-right");
    d.corner = corner == "bottom-left" ? 1 : corner == "top-right" ? 2 : corner == "top-left" ? 3 : 0;
    d.size = std::clamp(app_setting_int("Display", "Size", 42), 20, 60) / 100.0f;
    d.opacity = static_cast<u8>(std::clamp(app_setting_int("Display", "Opacity", 100), 30, 100) * 255 / 100);
    return d;
}

float g_top_aspect = 0.0f;
// widescreen = match: the top screen takes the shape of the window's room for it at once, whatever the window's size. The
// game draws a picture as wide as the widest shape it may take (the display's, set when it starts: its 3D view and HUD
// sizes are fixed then), and a narrower one is the middle of that picture (more of the scene above and below, as the
// 3D view keeps its height: Hor+), never narrower than the console's own 5:3 (below that, bars above and below: the
// game's menus are 400 pixels wide); the HUD's sides follow its edges (Port::Hooks::SetShownWidth).
bool g_top_match = false;
constexpr float kNativeAspect = 400.0f / 240.0f;

float TopAspectFromIni(const std::string& ini_path, int win_w, int win_h) {
    std::string value = ReadKeyValues(ini_path)["widescreen"];
    while (!value.empty() && (value.back() == ' ' || value.back() == '\r')) value.pop_back();
    const int w = std::max(win_w, win_h), h = std::min(win_w, win_h);
    if (value == "match" || value == "auto" || value == "screen") {
        g_top_match = true;
        return h > 0 ? static_cast<float>(w) / h : 0.0f;
    }
    const auto colon = value.find(':');
    if (colon != std::string::npos) {
        const float a = std::strtof(value.c_str(), nullptr), b = std::strtof(value.c_str() + colon + 1, nullptr);
        return b > 0 ? a / b : 0.0f;
    }
    return std::max(0.0f, std::strtof(value.c_str(), nullptr));
}

Layout ComputeLayout(int w, int h, u32 top_w, u32 top_h, u32 bot_w, u32 bot_h, bool bottom_shown, const DisplayOptions& d) {
    Layout l;
    l.bottom_shown = bottom_shown;
    l.bottom_alpha = d.mode == DisplayOptions::Overlay ? d.opacity : 255;
    const float th = top_h ? static_cast<float>(top_h) : 240.0f;
    const float bw = bot_w ? static_cast<float>(bot_w) : 320.0f, bh = bot_h ? static_cast<float>(bot_h) : 240.0f;
    // the picture's own shape, and the shape the top screen takes here (match: its room in this layout, filled)
    const float full = g_top_aspect > 0 ? g_top_aspect : top_w && top_h ? static_cast<float>(top_w) / top_h : kNativeAspect;
    float aspect = full;
    if (g_top_match && full > kNativeAspect && w > 0 && h > 0) {
        const float window = static_cast<float>(w) / h;
        const float room = !bottom_shown || d.mode == DisplayOptions::Overlay || d.mode == DisplayOptions::Window ? window
                           : d.mode == DisplayOptions::Stacked             ? window * (th + bh) / th
                                                                           : window - bw * 0.5f / th;  // (side by side)
        aspect = std::clamp(room, kNativeAspect, full);
    }
    const float tw = th * aspect;
    if (bottom_shown && d.mode == DisplayOptions::Stacked) {
        const float scale = std::min(w / std::max(tw, bw), h / (th + bh));
        const float x0 = (w - tw * scale) / 2, y0 = (h - (th + bh) * scale) / 2;
        l.top = {x0, y0, tw * scale, th * scale};
        l.bottom = {(w - bw * scale) / 2, y0 + th * scale, bw * scale, bh * scale};
    } else if (bottom_shown && d.mode == DisplayOptions::Side) {
        const float scale = std::min(w / (tw + bw * 0.5f), h / th);
        const float bs = scale * 0.5f;
        const float x0 = (w - (tw * scale + bw * bs)) / 2;
        l.top = {x0, (h - th * scale) / 2, tw * scale, th * scale};
        l.bottom = {x0 + tw * scale, (h - bh * bs) / 2, bw * bs, bh * bs};
    } else {
        const float scale = std::min(w / tw, h / th);
        l.top = {(w - tw * scale) / 2, (h - th * scale) / 2, tw * scale, th * scale};
        if (d.mode == DisplayOptions::Window) l.bottom_shown = false;  // (in its own window: TouchWindow)
        if (bottom_shown && d.mode != DisplayOptions::Window) {
            const float bs = (h * d.size) / bh;
            const float bwp = bw * bs, bhp = bh * bs, m = 8;
            const bool right = d.corner == 0 || d.corner == 2, below = d.corner == 0 || d.corner == 1;
            l.bottom = {right ? w - bwp - m : m, below ? h - bhp - m : m, bwp, bhp};
        }
    }
    l.top_full = l.top;
    if (aspect < full - 1e-4f) {
        l.crop = (1.0f - aspect / full) / 2;
        l.top_full.w = l.top.w * full / aspect;
        l.top_full.x = l.top.x - (l.top_full.w - l.top.w) / 2;
    }
    return l;
}

// ---- tests: keys and the test gamepad's sticks from the input script, the window saved as PNGs ------

struct KeyStep {
    u32 start = 0, len = 0;
    int kind = 0;  // 0 a key (code: its scancode), 1 an axis of the test gamepad (code: SDL_GamepadAxis, value)
    int code = 0;
    float value = 0;
    bool pressed = false;  // (down at least one turn of the loop: a turn that missed its frames - a screenshot - still presses it)
};
std::vector<KeyStep> g_key_steps;
// The mouse's steps (tests): CLICK x y button (down at the start, up after the frames; x, y fractions of the window),
// MMOVE dx dy (that many counts each game frame: the camera while it is captured), DRAG x0 y0 x1 y1 (the left button down
// at the first point, moved to the second over the frames, up there), WHEEL x y notches. The events are the ones SDL
// gives for a real mouse, a left click's finger first (SDL_MOUSE_TOUCHID), as SDL makes it of the click
struct MouseStep {
    u32 start = 0, len = 0;
    int kind = 0;  // 0 CLICK, 1 MMOVE, 2 DRAG, 3 WHEEL
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    int button = SDL_BUTTON_LEFT;
    bool down = false, done = false;
    u32 last = 0;
    std::string text;  // (TEXT <text>: typed text, '_' a space, as a keyboard's typing gives it)
};
std::vector<MouseStep> g_mouse_steps;
std::vector<u32> g_shot_frames;
std::string g_shot_dir;
u32 g_quit_frame = 0;
// MH4U_TEST_CLOCK=wall: the scripts, the shots and the quit count sixtieths of a second from the game's first frame, not
// the game's frames (a run with no frame-rate limit plays a script made at 60 FPS at its own pace); wall@N: the game's
// frames up to its frame N, sixtieths of a second from there on (with MH4U_ARGS="--set fN:fps=1000": 60 FPS on the way,
// no limit from frame N)
bool g_wall_clock = false;
u32 g_wall_from = 0;
u32 ScriptClock(u32 game_frame) {
    if (!g_wall_clock || game_frame == 0 || game_frame < g_wall_from) return game_frame;
    static Uint64 start_ns = 0;
    if (start_ns == 0) start_ns = SDL_GetTicksNS();
    return std::max(g_wall_from, 1u) + static_cast<u32>((SDL_GetTicksNS() - start_ns) * 60 / 1000000000ull);
}

void LoadTestSteps() {
    const std::string script = Env("MH4U_TEST_INPUT");
    if (!script.empty()) {
        std::ifstream in(P(script));
        std::string line;
        int keys = 0, axes = 0;
        while (std::getline(in, line)) {
            line = line.substr(0, line.find('#'));
            for (char& c : line)
                if (c == ',' || c == '\r' || c == '\t') c = ' ';
            std::istringstream words(line);
            std::string w;
            while (words >> w) {
                std::vector<std::string> f;
                std::stringstream parts(w);
                std::string part;
                while (std::getline(parts, part, ':')) f.push_back(part);
                if (f.size() < 4 || f.front()[0] != 'f' || f.back()[0] != 'f') continue;
                KeyStep st;
                st.start = static_cast<u32>(std::atol(f.front().c_str() + 1));
                st.len = static_cast<u32>(std::atol(f.back().c_str() + 1));
                if (f[1] == "KEY" && f.size() == 4) {
                    for (char& c : f[2])
                        if (c == '_') c = ' ';  // ("Left_Shift": SDL's "Left Shift")
                    const SDL_Scancode sc = SDL_GetScancodeFromName(f[2].c_str());
                    if (sc == SDL_SCANCODE_UNKNOWN) continue;
                    st.kind = 0, st.code = sc, ++keys;
                } else if ((f[1] == "CLICK" && f.size() == 6) || (f[1] == "MMOVE" && f.size() == 5) ||
                           (f[1] == "DRAG" && f.size() == 7) || (f[1] == "WHEEL" && f.size() == 6) ||
                           (f[1] == "RESIZE" && f.size() == 5) || (f[1] == "TOUCHSCREEN" && f.size() == 5) ||
                           (f[1] == "TEXT" && f.size() == 4)) {
                    MouseStep m;
                    m.start = st.start, m.len = std::max<u32>(1, st.len);
                    m.kind = f[1] == "CLICK" ? 0 : f[1] == "MMOVE" ? 1 : f[1] == "DRAG" ? 2 : f[1] == "WHEEL" ? 3 : f[1] == "RESIZE" ? 4
                             : f[1] == "TEXT" ? 6 : 5;
                    if (m.kind == 6) {
                        m.text = f[2];
                        for (char& c : m.text)
                            if (c == '_') c = ' ';
                        g_mouse_steps.push_back(m);
                        ++keys;
                        continue;
                    }
                    m.x0 = static_cast<float>(std::atof(f[2].c_str())), m.y0 = static_cast<float>(std::atof(f[3].c_str()));
                    if (m.kind == 0) m.button = f[4] == "right" ? SDL_BUTTON_RIGHT : f[4] == "middle" ? SDL_BUTTON_MIDDLE : SDL_BUTTON_LEFT;
                    if (m.kind == 2) m.x1 = static_cast<float>(std::atof(f[4].c_str())), m.y1 = static_cast<float>(std::atof(f[5].c_str()));
                    if (m.kind == 3) m.x1 = static_cast<float>(std::atof(f[4].c_str()));
                    g_mouse_steps.push_back(m);
                    ++keys;
                    continue;
                } else if (f[1] == "PADAXIS" && f.size() == 5) {
                    const SDL_GamepadAxis axis = SDL_GetGamepadAxisFromString(f[2].c_str());
                    if (axis == SDL_GAMEPAD_AXIS_INVALID) continue;
                    st.kind = 1, st.code = axis, st.value = std::clamp(static_cast<float>(std::atof(f[3].c_str())), -1.f, 1.f);
                    ++axes;
                } else {
                    continue;
                }
                g_key_steps.push_back(st);
            }
        }
        if (keys || axes) Logf("test script %s: %d key steps, %d stick steps", script.c_str(), keys, axes);
    }
    std::string shots = Env("MH4U_TEST_SHOTS");
    for (char& c : shots)
        if (c == ',') c = ' ';
    std::istringstream list(shots);
    std::string one;
    while (list >> one)
        if (one.size() > 1 && one[0] == 'f') g_shot_frames.push_back(static_cast<u32>(std::atol(one.c_str() + 1)));
    std::sort(g_shot_frames.begin(), g_shot_frames.end());
    g_shot_dir = Env("MH4U_TEST_SHOT_DIR");
    if (g_shot_dir.empty()) g_shot_dir = g_paths.home;
    if (!g_shot_frames.empty()) MakeDirs(g_shot_dir);
    {
        const std::string clock = Env("MH4U_TEST_CLOCK");
        g_wall_clock = clock.rfind("wall", 0) == 0;
        if (clock.rfind("wall@", 0) == 0) g_wall_from = static_cast<u32>(std::atol(clock.c_str() + 5));
    }
    const std::string quit = Env("MH4U_TEST_QUIT");
    if (quit.size() > 1) g_quit_frame = static_cast<u32>(std::atol(quit.c_str() + (quit[0] == 'f' ? 1 : 0)));
}

// The mouse's steps at the game frame `frame` (MouseStep)
void PushMouseButton(SDL_Window* window, float fx, float fy, int button, bool down) {
    int ww = 0, wh = 0;
    SDL_GetWindowSize(window, &ww, &wh);
    SDL_Event e{};
    if (button == SDL_BUTTON_LEFT && down) {  // (SDL's finger of a left click, before the click)
        e.type = SDL_EVENT_FINGER_DOWN;
        e.tfinger.timestamp = SDL_GetTicksNS();
        e.tfinger.touchID = SDL_MOUSE_TOUCHID, e.tfinger.fingerID = 1, e.tfinger.x = fx, e.tfinger.y = fy, e.tfinger.pressure = 1;
        e.tfinger.windowID = SDL_GetWindowID(window);
        SDL_PushEvent(&e);
    }
    e = SDL_Event{};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.timestamp = SDL_GetTicksNS();
    e.button.windowID = SDL_GetWindowID(window), e.button.which = 1, e.button.button = static_cast<Uint8>(button);
    e.button.down = down, e.button.clicks = 1, e.button.x = fx * ww, e.button.y = fy * wh;
    SDL_PushEvent(&e);
    if (button == SDL_BUTTON_LEFT && !down) {
        e = SDL_Event{};
        e.type = SDL_EVENT_FINGER_UP;
        e.tfinger.timestamp = SDL_GetTicksNS();
        e.tfinger.touchID = SDL_MOUSE_TOUCHID, e.tfinger.fingerID = 1, e.tfinger.x = fx, e.tfinger.y = fy;
        e.tfinger.windowID = SDL_GetWindowID(window);
        SDL_PushEvent(&e);
    }
}
void PushMouseMotion(SDL_Window* window, float fx, float fy, float dx, float dy, bool left_down) {
    int ww = 0, wh = 0;
    SDL_GetWindowSize(window, &ww, &wh);
    SDL_Event e{};
    if (left_down) {
        e.type = SDL_EVENT_FINGER_MOTION;
        e.tfinger.timestamp = SDL_GetTicksNS();
        e.tfinger.touchID = SDL_MOUSE_TOUCHID, e.tfinger.fingerID = 1, e.tfinger.x = fx, e.tfinger.y = fy, e.tfinger.pressure = 1;
        e.tfinger.dx = ww > 0 ? dx / ww : 0, e.tfinger.dy = wh > 0 ? dy / wh : 0;
        e.tfinger.windowID = SDL_GetWindowID(window);
        SDL_PushEvent(&e);
        e = SDL_Event{};
    }
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.timestamp = SDL_GetTicksNS();
    e.motion.windowID = SDL_GetWindowID(window), e.motion.which = 1, e.motion.state = left_down ? SDL_BUTTON_LMASK : 0;
    e.motion.x = fx * ww, e.motion.y = fy * wh, e.motion.xrel = dx, e.motion.yrel = dy;
    SDL_PushEvent(&e);
}
void RunMouseSteps(u32 frame, SDL_Window* window) {
    for (MouseStep& m : g_mouse_steps) {
        if (m.done || frame < m.start) continue;
        const u32 end = m.start + m.len;
        switch (m.kind) {
        case 0:
            if (!m.down) PushMouseButton(window, m.x0, m.y0, m.button, true), m.down = true;
            if (frame >= end) PushMouseButton(window, m.x0, m.y0, m.button, false), m.done = true;
            break;
        case 1:
            if (frame >= end) {
                m.done = true;
            } else if (frame != m.last) {
                const float n = m.last ? static_cast<float>(frame - m.last) : 1.f;
                PushMouseMotion(window, .5f, .5f, m.x0 * n, m.y0 * n, false);
                m.last = frame;
            }
            break;
        case 2: {
            int ww = 0, wh = 0;
            SDL_GetWindowSize(window, &ww, &wh);
            const float t = std::min(1.f, static_cast<float>(frame - m.start) / m.len);
            const float x = m.x0 + (m.x1 - m.x0) * t, y = m.y0 + (m.y1 - m.y0) * t;
            if (!m.down) {
                PushMouseMotion(window, m.x0, m.y0, 0, 0, false);
                PushMouseButton(window, m.x0, m.y0, SDL_BUTTON_LEFT, true), m.down = true, m.last = frame;
                break;
            }
            if (frame != m.last) {
                const float t0 = std::min(1.f, static_cast<float>(m.last - m.start) / m.len);
                PushMouseMotion(window, x, y, (x - (m.x0 + (m.x1 - m.x0) * t0)) * ww, (y - (m.y0 + (m.y1 - m.y0) * t0)) * wh, true);
                m.last = frame;
            }
            if (frame >= end) PushMouseButton(window, m.x1, m.y1, SDL_BUTTON_LEFT, false), m.done = true;
            break;
        }
        case 3: {
            int ww = 0, wh = 0;
            SDL_GetWindowSize(window, &ww, &wh);
            PushMouseMotion(window, m.x0, m.y0, 0, 0, false);
            SDL_Event e{};
            e.type = SDL_EVENT_MOUSE_WHEEL;
            e.wheel.timestamp = SDL_GetTicksNS();
            e.wheel.windowID = SDL_GetWindowID(window), e.wheel.which = 1;
            e.wheel.y = m.x1, e.wheel.integer_y = static_cast<Sint32>(m.x1), e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
            e.wheel.mouse_x = m.x0 * ww, e.wheel.mouse_y = m.y0 * wh;
            SDL_PushEvent(&e);
            m.done = true;
            break;
        }
        case 5: {  // (TOUCHSCREEN x y: a real touch screen's finger, not the mouse's, down for the frames)
            SDL_Event e{};
            e.type = !m.down ? SDL_EVENT_FINGER_DOWN : SDL_EVENT_FINGER_UP;
            if (m.down && frame < end) break;
            e.tfinger.timestamp = SDL_GetTicksNS();
            e.tfinger.touchID = 0x7e57, e.tfinger.fingerID = 7, e.tfinger.x = m.x0, e.tfinger.y = m.y0, e.tfinger.pressure = 1;
            e.tfinger.windowID = SDL_GetWindowID(window);
            SDL_PushEvent(&e);
            if (m.down) m.done = true;
            m.down = true;
            break;
        }
        case 6: {  // (TEXT: typed text, as SDL gives a keyboard's typing)
            SDL_Event e{};
            e.type = SDL_EVENT_TEXT_INPUT;
            e.text.timestamp = SDL_GetTicksNS();
            e.text.windowID = SDL_GetWindowID(window);
            e.text.text = m.text.c_str();  // (the steps live as long as the program)
            SDL_PushEvent(&e);
            m.done = true;
            break;
        }
        case 4:  // (RESIZE w h: the window's size, in its points, as a player's drag of its edge leaves it)
            SDL_SetWindowSize(window, static_cast<int>(m.x0), static_cast<int>(m.y0));
            Logf("test: the window resized to %.0fx%.0f", m.x0, m.y0);
            m.done = true;
            break;
        }
    }
}

// The key steps' presses and releases as SDL events (they go where a key of the keyboard goes), and the test gamepad's
// sticks, at the game frame `frame`
void RunTestSteps(u32 frame, SDL_Window* window) {
    if (!g_mouse_steps.empty()) RunMouseSteps(frame, window);
    if (g_key_steps.empty()) return;
    static std::map<int, bool> key_down;
    static std::map<int, float> axis_at;
    static SDL_Joystick* pad = nullptr;
    std::map<int, bool> want_keys;
    std::map<int, float> want_axes;
    for (KeyStep& st : g_key_steps) {
        const bool on = frame >= st.start && (frame < st.start + st.len || !st.pressed);
        if (on) st.pressed = true;
        if (st.kind == 0) want_keys[st.code] = want_keys[st.code] || on;
        else if (on) want_axes[st.code] = st.value;
        else want_axes.emplace(st.code, 0.0f);
    }
    for (const auto& [code, down] : want_keys) {
        if (key_down[code] == down) continue;
        key_down[code] = down;
        SDL_Event e{};
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.timestamp = SDL_GetTicksNS();
        e.key.windowID = SDL_GetWindowID(window);
        e.key.scancode = static_cast<SDL_Scancode>(code);
        e.key.key = SDL_GetKeyFromScancode(e.key.scancode, SDL_KMOD_NONE, false);
        e.key.down = down;
        SDL_PushEvent(&e);
    }
    if (want_axes.empty()) return;
    if (pad == nullptr) {  // (controls.cpp's "Test Gamepad", MH4U_TEST_JOYSTICK=pad)
        int n = 0;
        if (SDL_JoystickID* ids = SDL_GetJoysticks(&n)) {
            for (int i = 0; i < n && pad == nullptr; ++i)
                if (const char* name = SDL_GetJoystickNameForID(ids[i]); name != nullptr && !std::strcmp(name, "Test Gamepad"))
                    pad = SDL_OpenJoystick(ids[i]);
            SDL_free(ids);
        }
        if (pad == nullptr) return;
    }
    for (const auto& [axis, value] : want_axes) {
        if (axis_at.count(axis) && axis_at[axis] == value) continue;
        axis_at[axis] = value;
        SDL_SetJoystickVirtualAxis(pad, axis, static_cast<Sint16>(std::lround(value * 32767.0f)));
    }
}

// The window's picture (the whole of it: the game's screens and the overlay, as presented next) read back here, and
// written as a PNG on a thread of its own: the PNG's encoding (100 to 300 ms at 2560x1440) no longer holds the window
std::atomic<int> g_pngs_pending{0};  // (the program waits for them before it ends)
void SavePngLater(SDL_Surface* s, const std::string& path) {
    ++g_pngs_pending;
    std::thread([s, path] {
        const bool ok = SDL_SavePNG(s, path.c_str());
        SDL_DestroySurface(s);
        Logf("the window saved to %s: %s", path.c_str(), ok ? "ok" : SDL_GetError());
        --g_pngs_pending;
    }).detach();
}
void SaveShot(SDL_Renderer* renderer, u32 frame) {
    SDL_Surface* s = pc_hdr_on() ? pc_hdr_read(renderer) : SDL_RenderReadPixels(renderer, nullptr);
    if (s == nullptr) return;
    char name[64];
    std::snprintf(name, sizeof name, "/f%06u.png", frame);
    SavePngLater(s, g_shot_dir + name);
    if (pc_hdr_on()) {  // (and the HDR output's picture as presented: pc_hdr.h)
        std::snprintf(name, sizeof name, "/f%06u_hdr.png", frame);
        pc_hdr_save_output(g_shot_dir + name);
    }
}

// ---- the game folder ----------------------------------------------------------------------------

// The game's small files in the home's game/ from the player's game and update (the runtime's source.txt reads the
// RomFS in place), unless they are there from these very files already. "" or what went wrong.
std::string PrepareGameFiles(const PcGameFolder& f) {
    const auto source = ReadKeyValues(g_paths.game_files + "/source.txt");
    const auto same = [](const std::string& a, const std::string& b) {
        std::error_code ec;
        return fs::equivalent(P(a), P(b), ec) || a == b;
    };
    if (Exists(g_paths.game_files + "/code.bin") && Exists(g_paths.game_files + "/exheader.bin") && source.count("base") &&
        source.count("update") && same(source.at("base"), f.base) && same(source.at("update"), f.update))
        return "";
    MakeDirs(g_paths.game_files);
    std::string error;
    Logf("preparing the game's files from %s and %s", f.base.c_str(), f.update.c_str());
    if (!mh4n::fs::dump::PrepareGameFolder(f.base, f.update, g_paths.game_files, &error)) return error;
    return "";
}

// The touch screen in a window of its own ([Display] Layout = window): a second SDL window with an OpenGL renderer whose
// context shares the first one's objects, so the touch screen's picture (the renderer's buffer, imported once) is drawn
// there with no copy (pc_screens_draw_second); its pixels' form is uploaded into a texture of its own. The mouse in it is
// a finger on the touch screen. Closing it hides the touch screen (its button or the settings bring it back).
struct TouchWindow {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_WindowID id = 0;
    SDL_Texture* pixels = nullptr;  // (the pixels' form)
    std::shared_ptr<const std::vector<u8>> uploaded;
    int tex_w = 0, tex_h = 0;
    SDL_FRect rect{};  // the touch screen in it
    int w = 0, h = 0;
};
TouchWindow g_touch_window;

void CloseTouchWindow() {
    TouchWindow& t = g_touch_window;
    if (t.window == nullptr) return;
    int x = 0, y = 0, w = 0, h = 0;
    SDL_GetWindowPosition(t.window, &x, &y);
    SDL_GetWindowSize(t.window, &w, &h);
    app_setting_set("Display", "TouchWindow", std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(w) + "," + std::to_string(h));
    app_settings_save();
    pc_screens_set_second(nullptr);
    if (t.pixels != nullptr) SDL_DestroyTexture(t.pixels);
    SDL_DestroyRenderer(t.renderer);
    SDL_DestroyWindow(t.window);
    t = TouchWindow{};
    Logf("the touch screen's window is closed");
}

void OpenTouchWindow(SDL_Window* main) {
    TouchWindow& t = g_touch_window;
    if (t.window != nullptr) return;
    // (where it was last, else beside the game's window, at three times the touch screen's size or what fits)
    int x = 0, y = 0, w = 0, h = 0;
    if (std::sscanf(app_setting("Display", "TouchWindow").c_str(), "%d,%d,%d,%d", &x, &y, &w, &h) != 4 || w < 160 || h < 120) {
        SDL_Rect usable{0, 0, 1920, 1080};
        SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(main), &usable);
        h = std::min(720, usable.h / 2), w = h * 4 / 3;
        x = usable.x + usable.w - w - 16, y = usable.y + usable.h - h - 48;
    }
    pc_screens_make_current();
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    t.window = SDL_CreateWindow(settings_spanish() ? "MH4U: pantalla táctil" : "MH4U: touch screen", w, h,
                                SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_OPENGL);
    if (t.window != nullptr) t.renderer = SDL_CreateRenderer(t.window, "opengl");
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
    if (t.renderer == nullptr) {
        Logf("the touch screen's window: %s", SDL_GetError());
        if (t.window != nullptr) SDL_DestroyWindow(t.window);
        t = TouchWindow{};
        pc_screens_make_current();
        return;
    }
    SDL_SetWindowPosition(t.window, x, y);
    SDL_SetWindowMinimumSize(t.window, 160, 120);
    SDL_SetRenderVSync(t.renderer, 0);
    t.id = SDL_GetWindowID(t.window);
    pc_screens_set_second(t.renderer);
    Logf("the touch screen's window is open (%dx%d at %d,%d)", w, h, x, y);
}

// The touch screen into its window (after the game's window is presented)
void DrawTouchWindow(SharedScreenView& view, const std::shared_ptr<const std::vector<u8>>& pixels, int pw, int ph) {
    TouchWindow& t = g_touch_window;
    if (t.window == nullptr) return;
    SDL_GetRenderOutputSize(t.renderer, &t.w, &t.h);
    const float sw = view.width > 0 ? static_cast<float>(view.width) : 320.0f, sh = view.height > 0 ? static_cast<float>(view.height) : 240.0f;
    const float k = std::min(t.w / sw, t.h / sh);
    t.rect = SDL_FRect{(t.w - sw * k) / 2, (t.h - sh * k) / 2, sw * k, sh * k};
    SDL_SetRenderDrawColor(t.renderer, 0, 0, 0, 255);
    SDL_RenderClear(t.renderer);
    if (!pc_screens_draw_second(view, t.rect, t.w, t.h) && pixels != nullptr && pw > 0 && ph > 0) {
        if (t.pixels == nullptr || t.tex_w != pw || t.tex_h != ph) {
            if (t.pixels != nullptr) SDL_DestroyTexture(t.pixels);
            t.pixels = SDL_CreateTexture(t.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, pw, ph);
            t.tex_w = pw, t.tex_h = ph;
            if (t.pixels != nullptr) SDL_SetTextureScaleMode(t.pixels, SDL_SCALEMODE_LINEAR);
        }
        if (t.pixels != nullptr) {
            if (pixels != t.uploaded) SDL_UpdateTexture(t.pixels, nullptr, pixels->data(), pw * 4), t.uploaded = pixels;
            SDL_RenderTexture(t.renderer, t.pixels, nullptr, &t.rect);
        }
    }
    SDL_RenderPresent(t.renderer);
    pc_screens_make_current();
}

// Live resizing: while the window's edge is dragged Windows runs a loop of its own and the program's loop waits, so SDL
// sends expose events from inside it (every 10 ms or so, data1 = 1) to an event watch: the frame is drawn there, at the
// window's size of the moment (no stretched or black frames while the edge moves)
struct LiveDraw {
    SDL_WindowID window = 0;
    std::function<void()> draw;
    bool busy = false;
};
bool SDLCALL LiveResizeWatch(void* user, SDL_Event* e) {
    auto* d = static_cast<LiveDraw*>(user);
    if (e->type == SDL_EVENT_WINDOW_EXPOSED && e->window.data1 == 1 && e->window.windowID == d->window && !d->busy && d->draw) {
        d->busy = true;
        d->draw();
        d->busy = false;
    }
    return true;
}

// MH4U_TEST_DRAG=f<frame>:<steps>[:<px a step>] (tests): at that game frame a test thread changes the window's width by
// that many steps of 8 pixels (negative: narrower; SetWindowPos on the program's own window, every 40 ms: no input is
// sent anywhere), and then back; every size drawn meanwhile is saved as live_<n>.png into MH4U_TEST_SHOT_DIR (the clip of
// the resize). The drawing of a frame from inside Windows' own resize loop (LiveResizeWatch) is what a drag by hand gets.
u32 g_drag_frame = 0;
int g_drag_steps = 0, g_drag_px = 8;
std::atomic<bool> g_drag_active{false};
void LoadDragTest() {
    const std::string drag = Env("MH4U_TEST_DRAG");
    if (drag.size() > 1 && drag[0] == 'f' && std::sscanf(drag.c_str() + 1, "%u:%d:%d", &g_drag_frame, &g_drag_steps, &g_drag_px) >= 2)
        Logf("test: the window's width changed at game frame %u, %d steps", g_drag_frame, g_drag_steps);
}
void StartDragTest(SDL_Window* window) {
    HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (hwnd == nullptr) return;
    const int steps = g_drag_steps, px = g_drag_px;
    g_drag_active.store(true);
    std::thread([hwnd, steps, px] {
        RECT r{};
        GetWindowRect(hwnd, &r);
        const int w0 = r.right - r.left, h0 = r.bottom - r.top;
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 1; i <= std::abs(steps); ++i) {
                const int k = pass == 0 ? i : std::abs(steps) - i;
                SetWindowPos(hwnd, nullptr, 0, 0, w0 + (steps < 0 ? -k : k) * px, h0, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                Sleep(40);
            }
        Sleep(300);
        g_drag_active.store(false);
    }).detach();
    Logf("test: the window's width is being changed (%d steps of %d px)", steps, px);
}
void LiveShot(SDL_Renderer* renderer, int w, int h) {
    static unsigned saved = 0;
    static int last_w = 0;
    if (!g_drag_active.load() || w == last_w || saved >= 60 || (saved > 0 && std::abs(w - last_w) < 24)) return;
    last_w = w;
    if (SDL_Surface* s = pc_hdr_on() ? pc_hdr_read(renderer) : SDL_RenderReadPixels(renderer, nullptr)) {
        char name[64];
        std::snprintf(name, sizeof name, "/live_%02u.png", saved++);
        SavePngLater(s, g_shot_dir + name);
        Logf("live resize: %dx%d drawn (saved as %s)", w, h, name + 1);
    }
}

// MH4U_FPS_LOG=1 (tests): every 5 s, the game's pictures shown a second and the window's presents a second
void PresentStats(u32 game_frame) {
    static const bool on = !Env("MH4U_FPS_LOG").empty();
    static Uint64 since = 0;
    static u32 frame0 = 0;
    static unsigned presents = 0;
    if (!on) return;
    const Uint64 now = SDL_GetTicksNS();
    ++presents;
    if (since == 0) since = now, frame0 = game_frame, presents = 0;
    if (now - since < 5000000000ull) return;
    const double s = (now - since) / 1e9;
    std::fprintf(stderr, "[fps] the game %.1f pictures/s, the window %.1f presents/s (game frame %u)\n",
                 (game_frame - frame0) / s, presents / s, game_frame);
    since = now, frame0 = game_frame, presents = 0;
}

// (1.0.2) FSR 1 on the top screen as mh4u-app.ini has it ([Graphics] Upscale = fsr1, Fsr1Sharpness; fsr1.h), never with
// PS1 style (the PS1 filter, Brick), as android_main.cpp's ApplyFsr1: at the start and when the settings close (the
// panel sets its draft while it is open)
void ApplyFsr1() {
    const bool ps1 = app_setting_bool("Graphics", "Ps1Filter", false) ||
                     app_low_end_level(app_setting("Graphics", "LowEnd", "off")) == LOW_END_BRICK;
    fsr1_set(app_setting("Graphics", "Upscale", "smooth") == "fsr1" && !ps1,
             std::clamp(app_setting_int("Graphics", "Fsr1Sharpness", 80), 0, 100) / 100.0f);
}
// The top screen into `dst` with FSR 1 (fsr1_draw), the middle of the picture when a narrower window shows that
// (crop, as shared_screens_draw_part); false: not drawn (FSR 1 off, or the picture not shown larger): drawn as before
bool DrawTopFsr1(SDL_Renderer* renderer, SharedScreenView& view, float crop, const SDL_FRect& dst, int win_w, int win_h) {
    if (crop <= 0) return fsr1_draw(renderer, view, dst, win_w, win_h);
    const SDL_FRect whole = view.source;
    const int width = view.width;
    view.source = SDL_FRect{whole.x + crop * whole.w, whole.y, (1 - 2 * crop) * whole.w, whole.h};
    view.width = std::max(1, static_cast<int>(std::lround(width * (1 - 2 * crop))));
    const bool drawn = fsr1_draw(renderer, view, dst, win_w, win_h);
    view.source = whole, view.width = width;
    return drawn;
}

}  // namespace

const PcPaths& pc_paths() { return g_paths; }
std::string pc_game_folder() { return g_game_folder; }
void pc_paths_reload_saves() { ReloadSaves(); }
bool pc_restart_requested() { return g_restart; }

bool pc_action(const char* method) {
    if (method == nullptr) return false;
    const std::string m = method;
    if (m == "showGameFolder" || m == "restartApp" || m == "showBuilderNow") {  // (showBuilder asks first: pc_files.cpp)
        g_restart = true;
        g_restart_arg = m == "showBuilderNow" ? L"--builder" : m == "showGameFolder" ? L"--choose-folder" : L"";
        SDL_Event e{};
        e.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&e);
        return true;
    }
    return pc_files_action(m);  // (the log, the saves, mods and cheats, DLC, licenses: pc_files.cpp)
}

int main(int argc, char* argv[]) {
    diag_start();
    pc_perf_name_threads();  // (the threads named for the [perf] lines and Windows' tools: pc_perf.h)
    // the mouse is a finger on the touch screen, as a mouse is on Android (SDL's touch events from it)
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");
    const long long main_ms = MsSinceProcessStart();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MH4U", SDL_GetError(), nullptr);
        return 1;
    }
    FindPaths();
    for (int i = 1; i < argc; ++i)  // (the error screen after a crash: before the log is moved, it reads that session's)
        if (!std::strncmp(argv[i], "--crashed", 9)) {
            app_settings_init(g_paths.home);
            const int status = pc_crash_screen(argv[i][9] == '=' ? argv[i] + 10 : "", settings_spanish());
            SDL_Quit();
            return status;
        }
    OpenLog();
    std::fprintf(stderr, "[Info] App: MH4U for PC %s\n", MH4U_APP_VERSION);
    diag_log_env();
    Logf("home %s, saves in %s", g_paths.home.c_str(), g_paths.save_folder.c_str());
    Logf("the start: main %lld ms after the process was made, the log at %lld ms (SDL's start between)", main_ms,
         MsSinceProcessStart());
    app_settings_init(g_paths.home);
    diag_log_ini("mh4u-app.ini (the app's options; names and passwords hidden)", app_settings_path());
    // (the PC's layout until the settings change it: both screens, the touch screen beside the top one)
    if (app_setting("Display", "Layout").empty()) {
        app_setting_set("Display", "Layout", "side");
        app_settings_save();
    }
    const bool es = settings_spanish();
    pc_updates_check(g_paths.home);  // (GitHub's latest release, at most once a day: pc_updates.cpp)

    // the game folder: the one chosen before, a folder given on the command line, or the game folder screen's
    bool choose = false, builder = false;
    std::string folder = FirstLine(g_paths.home + "/game_folder.txt");
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--choose-folder")) choose = true;
        else if (!std::strcmp(argv[i], "--builder")) choose = builder = true;  // (F1, System, Game code: the builder's page)
        else if (argv[i][0] != '-') folder = argv[i];
    }
    PcGameFolder game_folder = pc_look_at_folder(folder, es);
    for (;;) {
        if (choose || !game_folder.problem.empty() || !Env("MH4U_LAUNCHER_SHOT").empty()) {
            const std::string chosen = pc_launcher(game_folder, es, builder);
            builder = false;
            if (chosen.empty()) {
                SDL_Quit();
                return 0;
            }
            game_folder = pc_look_at_folder(chosen, es);
            choose = false;
            if (!game_folder.problem.empty()) continue;
        }
        if (const std::string error = PrepareGameFiles(game_folder); !error.empty()) {
            Logf("the game's files: %s", error.c_str());
            game_folder.problem = error;
            continue;
        }
        break;
    }
    WriteText(g_paths.home + "/game_folder.txt", game_folder.folder + "\n");
    g_game_folder = game_folder.folder;
    // a change of save folder, a save or event quests imported (each waits for this start), the game folder's mods/
    for (const auto& [title, text] : pc_files_before_game(game_folder.folder)) {
        Logf("%s: %s", title.c_str(), text.c_str());
        if (Env("MH4U_TEST_ANSWER").empty()) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title.c_str(), text.c_str(), nullptr);
    }
    pc_update_notice(es);  // (a newer release, at most once a day)
    SetEnv("MH4N_SAVE_BACKUPS", g_paths.backups);  // (the runtime's own backups there too: DLC in every save)
    const std::string game = g_paths.game_files, user = g_paths.user, code = game_folder.library;
    Logf("game folder %s: game %s, update %s, code library %s (%s)", game_folder.folder.c_str(), game_folder.base.c_str(),
         game_folder.update.c_str(), code.c_str(), game_folder.library_id.c_str());

    LoadTestSteps();
    pc_perf_start();
    // The code library read ahead on a thread of its own, at low priority (as MHGU for PC's): on a hard disk its pages
    // otherwise come one at a time as the game first runs them, the first minutes' stutters; from the cache, at once
    std::thread([lib = code] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        const Uint64 t0 = SDL_GetTicks();
        FILE* f = _wfopen(mh4n::utf8::ToWide(lib).c_str(), L"rb");
        if (f == nullptr) return;
        std::vector<char> chunk(4 << 20);
        size_t total = 0, n = 0;
        while ((n = std::fread(chunk.data(), 1, chunk.size(), f)) > 0) total += n;
        std::fclose(f);
        Logf("the code library read ahead: %zu MB in %llu ms", total >> 20, static_cast<unsigned long long>(SDL_GetTicks() - t0));
    }).detach();
    LoadDragTest();
    // The pipeline cache in a place of the user's own (%LOCALAPPDATA%\MH4U\cache\pipelines), not in the program's
    // folder: a new version unpacked into a new folder starts with every pipeline already known instead of compiling
    // them all again (the first start's black screen). MH4U_PIPELINE_DIR=<folder> names another; a home of the tests'
    // own (MH4U_HOME) keeps it inside, as before.
    std::string pipeline_root = user;
    if (const std::string own = Env("MH4U_PIPELINE_DIR"); !own.empty()) {
        pipeline_root = own;
    } else if (Env("MH4U_HOME").empty()) {
        if (const std::string local = Env("LOCALAPPDATA"); !local.empty()) pipeline_root = local + "/MH4U";
    }
    MakeDirs(pipeline_root + "/cache");
    cache_clear_start(pipeline_root);  // (<root>/cache/pipelines: the settings' Clear shader cache works on it there)
    // HD texture packs: the game folder's textures/, read in place (as Android's)
    if (Env("MH4N_TEXTURE_PACKS").empty()) {
        std::error_code ec;
        if (fs::is_directory(P(game_folder.folder + "/textures"), ec)) {
            SetEnv("MH4N_TEXTURE_PACKS", game_folder.folder + "/textures");
            std::fprintf(stderr, "[Info] App: texture packs from %s/textures\n", game_folder.folder.c_str());
        }
    }
    if (app_setting("Graphics", "HdPackSize", "full") == "half") SetEnv("MH4N_HD_SCALE", "half", false);
    MakeDirs(user);
    game_options_init(user);
    // a code mod that is on (the single-screen mod): the touch screen's menus are on the top screen
    bool code_mod = false, touch_start_set = false;
    {
        const auto mods = mh4n::mods::Enabled(mh4n::mods::Folder(user), content_title_of(game));
        code_mod = std::any_of(mods.begin(), mods.end(), [](const mh4n::mods::Mod& m) { return m.code; });
        const std::string start = Env("MH4U_TOUCH_START");
        if (start == "hidden" || start == "shown") {
            ui_set_bottom_default(start == "shown");
            touch_start_set = true;
        } else if (code_mod) {
            ui_set_bottom_default(false);
        }
    }
    diag_log_ini("mh4u.ini (the port's game options)", user + "/mh4u.ini");
    content_init(user);
    content_set_game(game);
    {  // the app's own cheats (faster eating and drinking, as Android's and MHGU's), off, the first time: beside MH4U.exe
        std::ifstream own(P(g_paths.exe_dir + "/cheats/comer_rapido.txt"), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(own)), std::istreambuf_iterator<char>());
        if (!text.empty()) content_add_port_cheats(text);
    }
    verify_files_init(VerifyPaths{g_paths.home, game, code, user, g_paths.save_folder, MH4U_PC_CODE_ID});
    if (!Exists(user + "/mh4u.ini")) first_run_write(user + "/mh4u.ini", first_run_probe());
    else first_run_fill(user + "/mh4u.ini");
    {  // one screen (mh4u.ini's single_screen): the touch screen's corner starts hidden, as on Android
        std::string one = ReadKeyValues(user + "/mh4u.ini")["single_screen"];
        const bool on = one == "1" || one == "on" || one == "true" || one == "yes";
        const std::string env_one = Env("MH4U_SINGLE_SCREEN");
        const std::string run_one = env_one.empty() ? one : env_one;
        const bool run_on = run_one == "1" || run_one == "on" || run_one == "true" || run_one == "yes";
        ui_set_one_screen(code_mod || (run_on && content_title() == kContentTitle));
        if (code_mod) {
            SetEnv("MH4U_SINGLE_SCREEN", "off");
            if (on) Logf("one screen: off for this start (a code mod is on)");
        } else if (on && !touch_start_set && content_title() == kContentTitle) {
            ui_set_bottom_default(false);
        }
    }

    // the window (pc_display.h): a borderless full screen at the desktop's resolution by default, or a window of any size,
    // on the display it starts on (tests: MH4U_WINDOW_AT=x,y the display holding that desktop point, e.g. 3440,0 for a
    // second monitor right of a 3440-wide one)
    SDL_DisplayID start_display = SDL_GetPrimaryDisplay();
    if (const char* at = SDL_getenv("MH4U_WINDOW_AT"); at != nullptr && *at != 0) {
        SDL_Point p{0, 0};
        if (SDL_sscanf(at, "%d,%d", &p.x, &p.y) == 2)
            if (SDL_DisplayID d = SDL_GetDisplayForPoint(&p); d != 0) start_display = d;
    }
    // OpenGL draws the window's picture (pc_screens.cpp: the renderer's buffers with no copy, the colour filter, PS1
    // style, frame generation); SDL's default renderer (Direct3D 11) if a GL context can't be had (MH4U_RENDERER=<SDL's
    // name of one>: that one, a test)
    SDL_Window* window = pc_display_create_window("MH4U", start_display, SDL_WINDOW_OPENGL);
    Logf("the start: the window at %lld ms", MsSinceProcessStart());
    if (window == nullptr) {
        Logf("SDL_CreateWindow: %s", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MH4U", SDL_GetError(), nullptr);
        return 1;
    }
    const std::string renderer_named = Env("MH4U_RENDERER");
    SDL_Renderer* renderer = renderer_named.empty() ? SDL_CreateRenderer(window, "opengl") : nullptr;
    if (renderer == nullptr) {
        if (renderer_named.empty()) Logf("SDL's OpenGL renderer: %s: SDL's default renderer instead", SDL_GetError());
        renderer = SDL_CreateRenderer(window, renderer_named.empty() ? nullptr : renderer_named.c_str());
    }
    if (renderer == nullptr) {
        Logf("SDL_CreateRenderer: %s", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MH4U", SDL_GetError(), window);
        return 1;
    }
    SDL_SetRenderVSync(renderer, pc_display_vsync() ? 1 : 0);
    pc_set_window(window);
    pc_vsync_start(window);  // (with VSync on, the game's VBlank held to the display's vsync: pc_vsync.h)
    pc_vsync_lock(pc_display_vsync());
    perf_hint_thread(PERF_RENDER);
    int win_w = 0, win_h = 0;
    SDL_GetRenderOutputSize(renderer, &win_w, &win_h);
    Logf("window %dx%d, renderer %s, vsync %s", win_w, win_h, SDL_GetRendererName(renderer), pc_display_vsync() ? "on" : "off");
    // The screen's own size for the shape and the 3D resolution (the desktop's, as the phone's screen on Android)
    int screen_w = 1920, screen_h = 1080;
    if (const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(window))) {
        if (mode->w > 0 && mode->h > 0) screen_w = std::max(mode->w, mode->h), screen_h = std::min(mode->w, mode->h);
    }
    Logf("screen %dx%d", screen_w, screen_h);
    // (the top screen's shape for widescreen = match: the widest it may take, the display's or the window's if wider: a
    // narrower window shows the middle of that picture, ComputeLayout; its 3D resolution: the screen's)
    const bool window_wider = static_cast<float>(win_w) / std::max(win_h, 1) > static_cast<float>(screen_w) / screen_h;
    const int shape_w = std::max(window_wider ? win_w : screen_w, 1), shape_h = std::max(window_wider ? win_h : screen_h, 1);
    g_top_aspect = TopAspectFromIni(user + "/mh4u.ini", shape_w, shape_h);
    Logf("top screen shape %.4f (0: the picture's)", g_top_aspect);
    ui_init(window, renderer, g_paths.home);
    g_want_shared = shared_screens_init(renderer);
    pc_screens_set_upscale(pc_display_upscale());
    pc_hdr_start(window, renderer);  // ([Colour] HDR on and Windows' HDR on for the window's display: pc_hdr.h)
    g_wake_event = SDL_RegisterEvents(1);
    controls_init(g_paths.home, [](bool down, int x, int y) {
        auto* rt = g_runtime.load();
        auto live = rt ? mh4n::input_live::Find(*rt) : nullptr;
        if (!live) return;
        if (down) live->SetTouch(mh4n::input_live::TouchPoint{static_cast<u16>(x), static_cast<u16>(y)});
        else live->SetTouch(std::nullopt);
    });
    // the game's frames start on its VBlank, the limiter sleeps, the cartridge's delays at a quarter (as Android's;
    // MH4U_PACE=limiter, MH4U_LIMITER_SLEEP=0, MH4N_IO_LATENCY_SCALE=100 in the environment: as before)
    SetEnv("MH4U_PACE", "vblank", false);
    SetEnv("MH4U_LIMITER_SLEEP", "1", false);
    SetEnv("MH4N_IO_LATENCY_SCALE", "25", false);
    // (performance) the renderer's queue counts the game's pictures, not the emulated VBlanks: at "No limit" the VBlank
    // runs at 1000 Hz, and two VBlanks queued held the game's thread behind the renderer (mh4n QueueConfig)
    SetEnv("MH4N_RENDER_QUEUE_SWAPS", "1", false);
    // (performance) above 60 FPS the game's job workers look for work every 200 us, not every 1 ms (mh4n port hooks'
    // job_poll_us): its main thread waits on them each frame, and that millisecond held "No limit" at 250 FPS
    // (measured: town 249 -> 296-302; 50 us: 287). MH4U_JOB_POLL_US=0: the game's own millisecond
    SetEnv("MH4U_JOB_POLL_US", "200", false);
    // (performance) the renderer's rings that the GPU reads (vertices, uniforms, tables) in the host's cached memory, not
    // in the card's own across the bus (mh4n CreateRing): the renderer thread writes them at every draw (measured with
    // the line above: town 277-298 -> 293-315 FPS; the GPU's time +10 %, it is a third busy). MH4N_RING_MEMORY=device:
    // as before
    SetEnv("MH4N_RING_MEMORY", "host", false);
    // the top screen's picture drawn upright by the renderer (its render target is not shared on PC: pc_screens.cpp)
    SetEnv("MH4N_TARGET_SCREENS", "0", false);
    // the GPU the renderer runs on (Graphics & Audio > Advanced > GPU: a part of its name), and its adapter for the readout
    if (const std::string gpu = app_setting("Graphics", "Gpu"); !gpu.empty()) {
        SetEnv("MH4N_VULKAN_DEVICE", gpu, false);
        Logf("the GPU asked for: %s", gpu.c_str());
    }
    {
        mh4n::gpu::rhi::DriverHooks hooks;
        hooks.device_made = [](const mh4n::gpu::rhi::MadeDevice& d) {
            if (d.luid_valid) winstats_set_luid(d.luid);
        };
        mh4n::gpu::rhi::SetDriverHooks(std::move(hooks));
    }
    pc_stats_apply();
    crash_screen_install();
    app_live_start();
    pc_net_start();  // (Discord Rich Presence: pc_discord.h)
    frame_gen_set_enabled(app_setting_bool("Graphics", "FrameGen", false));  // ([Graphics] FrameGen: frame_gen.h)
    {
        ColourAdjust colour;
        app_colour(&colour);
        shared_screens_set_colour(colour);
        shared_screens_set_ps1(app_setting_bool("Graphics", "Ps1Filter", false),
                               app_low_end_level(app_setting("Graphics", "LowEnd", "off")) == LOW_END_BRICK);
        ApplyFsr1();  // (1.0.2: FSR 1 on the top screen, off as it comes)
    }

    // the game, on its own thread
    std::thread game_thread;
    {
        Port::Hooks::SetWindowSize(static_cast<std::uint32_t>(std::max(shape_w, shape_h)),
                                   static_cast<std::uint32_t>(std::min(shape_w, shape_h)));
        std::vector<std::string> args = {"mh4u", game, "--code", code, "--user", user, "--gpu", "native-display",
                                         "--input-source", "live", "--audio", "sdl", "--frames", "4000000000"};
        MakeDirs(g_paths.save_folder + "/sdmc");
        args.push_back("--pipeline-dir");
        args.push_back(pipeline_root + "/cache/pipelines");
        args.push_back("--saves");
        args.push_back(g_paths.save_folder + "/sdmc");
        std::string render_thread = "on";
        int render_percent = kRenderScaleDefault;
        app_set_window_size(screen_w, screen_h);
        if (const std::string volume = app_setting("Audio", "Volume"); !volume.empty()) {
            args.push_back("--volume");
            args.push_back(std::to_string(std::clamp(std::atoi(volume.c_str()), 0, 100)));
        }
        if (const std::string scale = app_setting("Graphics", "RenderScale"); !scale.empty())
            render_percent = std::clamp(std::atoi(scale.c_str()), 0, 100);
        if (app_setting("Graphics", "RenderThread", "on") == "off") render_thread = "off";
        const int scale_times = render_scale_used(render_percent, false);
        std::fprintf(stderr, "[Info] App: 3D resolution %d %% of %dx%d: %dx the 3DS's\n", render_percent, screen_w, screen_h,
                     scale_times);
        args.push_back("--scale");
        args.push_back(std::to_string(scale_times));
        if (scale_times > 1 && !ReadKeyValues(user + "/mh4u.ini").count("dither")) SetEnv("MH4U_DITHER", "0");
        args.push_back("--render-thread");
        args.push_back(render_thread);
        {
            std::istringstream extra(Env("MH4U_ARGS"));
            std::string a;
            while (extra >> a) args.push_back(a);
        }
        // The stall guard (mh4n's --stall-pause): when the PC holds the game up (a slow disk, a busy PC), the game's
        // clock stops after 100 ms instead of running on. Online, the game takes a host it hasn't heard from for about
        // a second for gone and leaves the hall: the PC's own stall made it leave a phone's hall. MH4U_ARGS may set
        // another value (0: off).
        if (std::find(args.begin(), args.end(), "--stall-pause") == args.end()) {
            args.push_back("--stall-pause");
            args.push_back("100");
        }
        // The link hold (mh4n's --link-hold): in a hall, when the host has sent nothing for half a second (a room sends
        // every frame reliably and in order, so one lost on a hub's slow link holds the rest back for about a second),
        // the game clock waits for it, so that the game doesn't take the host for gone and leave. MH4U_ARGS may set
        // another value (0: off).
        if (std::find(args.begin(), args.end(), "--link-hold") == args.end()) {
            args.push_back("--link-hold");
            args.push_back("500");
        }
        // (the screens read back, pixels: a FPS limit above 60 from the first frame stops the game at its frame 15 or
        // so, on that path only; the game starts at 60 and takes the limit once it runs, a few seconds in)
        if (!g_want_shared && Env("MH4U_FPS").empty()) {
            const float fps = std::strtof(ReadKeyValues(user + "/mh4u.ini")["fps"].c_str(), nullptr);
            if (fps > 60.5f) {
                SetEnv("MH4U_FPS", "60");
                char at[64];
                std::snprintf(at, sizeof at, "f600:fps=%g", fps);
                args.push_back("--set");
                args.push_back(at);
                Logf("the screens come as pixels: the game starts at 60 FPS and takes its limit (%g) at its frame 600", fps);
            }
        }
        const bool given = std::any_of(args.begin(), args.end(), [](const std::string& a) {
            return a == "--input-latch" || a == "--record-input";
        });
        if (!given) {
            args.push_back("--input-latch");
            args.push_back(Env("MH4U_INPUT_LATCH") == "frame" ? "frame" : "none");
        }
        std::string line;
        for (const auto& a : args) line += " " + a;
        Logf("the runtime's arguments:%s", line.c_str());
        game_thread = std::thread([args] {
            mh4n::SetHostThreadName("mh4u-game");
            perf_hint_thread(PERF_GAME);
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
            Wake();
        });
    }

    SharedScreenView views[2];
    SharedScreenView& top_view = views[0];
    top_view.top = true;
    SharedScreenView& bottom_view = views[1];
    u64 shown_sequence = 0;
    u32 shown_game_frame = 0;
    bool bottom_shown = true;
    bool running = true;
    bool quitting = false;
    bool paused_minimized = false, paused_for_menu = false;
    SDL_FingerID touch_finger = 0;
    bool touching = false, touching_items = false;
    DisplayOptions display = ReadDisplayOptions();
    Layout layout = ComputeLayout(win_w, win_h, 400, 240, 320, 240, bottom_shown, display);
    mh4n::InputState last_sent;
    size_t next_shot = 0;

    auto touch_at = [&](float x, float y, bool down) {
        auto* rt = g_runtime.load();
        if (rt == nullptr) return;
        auto live = mh4n::input_live::Find(*rt);
        if (!live) return;
        if (!down) {
            live->SetTouch(std::nullopt);
            return;
        }
        int tx = 0, ty = 0;
        if (touching_items) g_items_hot_until.store(SDL_GetTicks() + 1500, std::memory_order_relaxed);
        if (touching_items && quick_items_touch(x, y, &tx, &ty)) {
            live->SetTouch(mh4n::input_live::TouchPoint{static_cast<u16>(tx), static_cast<u16>(ty)});
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

    // The touch screen's own window's events (true: one of its own, handled)
    const auto TouchWindowEvent = [&](const SDL_Event& ev) {
        TouchWindow& t = g_touch_window;
        SDL_WindowID id = 0;
        if (ev.type >= SDL_EVENT_WINDOW_FIRST && ev.type <= SDL_EVENT_WINDOW_LAST) id = ev.window.windowID;
        else if (ev.type == SDL_EVENT_FINGER_DOWN || ev.type == SDL_EVENT_FINGER_MOTION || ev.type == SDL_EVENT_FINGER_UP ||
                 ev.type == SDL_EVENT_FINGER_CANCELED)
            id = ev.tfinger.windowID;
        else if (ev.type == SDL_EVENT_MOUSE_MOTION) id = ev.motion.windowID;
        else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev.type == SDL_EVENT_MOUSE_BUTTON_UP) id = ev.button.windowID;
        else if (ev.type == SDL_EVENT_MOUSE_WHEEL) id = ev.wheel.windowID;
        if (id == 0 || id != t.id) return false;
        if (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            if (ui_bottom_wanted()) ui_toggle_bottom();  // (the touch screen hidden: its window closes at the next frame)
            return true;
        }
        const auto send = [&](float nx, float ny, bool down) {
            auto* rt = g_runtime.load();
            auto live = rt ? mh4n::input_live::Find(*rt) : nullptr;
            if (!live) return;
            if (!down) {
                live->SetTouch(std::nullopt);
                return;
            }
            const float u = (nx * t.w - t.rect.x) / std::max(1.0f, t.rect.w), v = (ny * t.h - t.rect.y) / std::max(1.0f, t.rect.h);
            live->SetTouch(mh4n::input_live::TouchPoint{static_cast<u16>(std::clamp(u, 0.0f, 0.999f) * 320),
                                                        static_cast<u16>(std::clamp(v, 0.0f, 0.999f) * 240)});
        };
        if (ev.type == SDL_EVENT_FINGER_DOWN) {
            const float u = (ev.tfinger.x * t.w - t.rect.x) / std::max(1.0f, t.rect.w);
            const float v = (ev.tfinger.y * t.h - t.rect.y) / std::max(1.0f, t.rect.h);
            if (!touching && u >= 0 && u < 1 && v >= 0 && v < 1) {
                touching = true, touching_items = false, touch_finger = ev.tfinger.fingerID;
                send(ev.tfinger.x, ev.tfinger.y, true);
            }
        } else if (ev.type == SDL_EVENT_FINGER_MOTION) {
            if (touching && ev.tfinger.fingerID == touch_finger) send(ev.tfinger.x, ev.tfinger.y, true);
        } else if (ev.type == SDL_EVENT_FINGER_UP || ev.type == SDL_EVENT_FINGER_CANCELED) {
            if (touching && ev.tfinger.fingerID == touch_finger) touching = false, send(0, 0, false);
        }
        return true;
    };
    int town_touch_last = -1, town_touch_x = 0, town_touch_y = 0;
    auto send_input = [&] {
        const UiInput ui = ui_take_input();
        const ControlsInput pad = controls_input(ScriptClock(shown_game_frame));
        mh4n::InputState in;
        in.buttons = pad.buttons | ui.buttons;
        const bool ui_circle = ui.circle_x != 0 || ui.circle_y != 0, ui_cstick = ui.cstick_x != 0 || ui.cstick_y != 0;
        in.circle_x = ui_circle ? ui.circle_x : pad.circle_x;
        in.circle_y = ui_circle ? ui.circle_y : pad.circle_y;
        in.cstick_x = ui_cstick ? ui.cstick_x : pad.cstick_x;
        in.cstick_y = ui_cstick ? ui.cstick_y : pad.cstick_y;
        in.zl = pad.zl || ui.zl;
        in.zr = pad.zr || ui.zr;
        Port::Hooks::SetHostTriggers(in.zl, in.zr);
        const float yaw = ui.cam_yaw + pad.cam_yaw, pitch = ui.cam_pitch + pad.cam_pitch;
        if (yaw != 0 || pitch != 0) Port::Hooks::AddCameraTurn(yaw, pitch);
        in.buttons |= town_buttons();
        {
            int tx = 0, ty = 0;
            int t = touching ? -1 : town_touch(&tx, &ty);
            if (t != 1 && !touching && pc_key_touch(&tx, &ty) == 1) t = 1;  // (a key's tap: the item shortcuts, pc_input.h)
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
            }
        }
    };

    u64 last_present_ms = 0, last_input_ms = 0;
    bool new_picture = false, bottom_dirty = false, had_events = false, new_top = false;
    // One frame: the game's state, its newest pictures, the layout, the window drawn and presented (or, with nothing new
    // and not `live`, a short wait). `live`: from inside Windows' own loop while the window's edge is dragged (SDL's
    // expose events, LiveResizeWatch): drawn at the window's size of the moment, every time.
    const auto frame_once = [&](bool live) {
        if (g_game_done.load()) running = false;
        // the settings open while playing alone: the game waits ([Interface] PauseInMenu, as Android's)
        if (auto* rt = g_runtime.load()) {
            const auto room = mh4n::rooms::GetStatus().state;
            const bool alone = room != mh4n::rooms::State::Joined && room != mh4n::rooms::State::Moderator;
            static unsigned read_version = ~0u;
            static bool pause_in_menu = true;
            if (read_version != app_settings_version()) {
                read_version = app_settings_version();
                pause_in_menu = app_setting_bool("Interface", "PauseInMenu", true);
            }
            // (1.0.2: not the chat window afloat - the game goes on under it, settings_panel.h)
            const bool want = ((ui_settings_open() && !settings_panel_chat_floating() && !touch_hud_size_runs()) || touch_editor_holds() ||
                               pc_help_open()) && alone &&
                              pause_in_menu;  // (the settings' HUD size just changed: a moment of every 100 ms runs, touch.cpp)
            if (want && !paused_for_menu) {
                rt->Clock().Pause();
                paused_for_menu = true;
                if (!touch_hud_size_recent()) Logf("the game waits while the menu is open");
            } else if (!want && paused_for_menu) {
                paused_for_menu = false;
                if (!paused_minimized) rt->Clock().Resume();
                if (!touch_hud_size_recent()) Logf("the game goes on");
            }
        }
        pc_net_frame();
        pc_net_test(shown_game_frame, window);  // (tests: MH4U_TEST_NET, pc_net.h)
        RunTestSteps(ScriptClock(shown_game_frame), window);
        {  // MH4U_TEST_MENU=f<frame>:<page>[#<row>],... (tests): the settings opened at that section or page at that game
           // frame, that row picked ("save": the Save button; the input script's KEY steps do the rest, as keys of the
           // keyboard; the game goes on behind with [Interface] PauseInMenu off)
            static std::vector<std::pair<u32, std::string>> menu_steps;
            static bool menu_read = false;
            static std::string menu_page;
            static int menu_again = 0;
            if (!menu_read) {
                menu_read = true;
                std::string list = Env("MH4U_TEST_MENU");
                for (char& c : list)
                    if (c == ',') c = ' ';
                std::istringstream words(list);
                std::string w;
                while (words >> w)
                    if (w.size() > 1 && w[0] == 'f' && w.find(':') != std::string::npos)
                        menu_steps.emplace_back(static_cast<u32>(std::atol(w.c_str() + 1)), w.substr(w.find(':') + 1));
            }
            while (!menu_steps.empty() && shown_game_frame >= menu_steps.front().first && !live) {
                const std::string page = menu_steps.front().second;
                menu_steps.erase(menu_steps.begin());
                if (page == "save") {
                    pc_settings_test_save();
                    Logf("test: the settings saved");
                } else {
                    ui_open_settings();
                    menu_page = page, menu_again = 2;  // (again the next frames: the panel's own opening comes first)
                    Logf("test: the settings at %s", page.c_str());
                }
            }
            if (menu_again > 0 && !live) {
                --menu_again;
                pc_settings_test_page(menu_page);
            }
        }
        if (g_drag_frame != 0 && g_drag_frame != ~0u && shown_game_frame >= g_drag_frame && !live) {
            g_drag_frame = ~0u;  // (once)
            StartDragTest(window);
        }
        if (had_events) send_input();

        // the newest screens (taken under the lock, uploaded outside it)
        {
            ScreenPicture top, bottom;
            {
                std::lock_guard lock(g_screens.mutex);
                if (g_screens.sequence != shown_sequence) {
                    shown_sequence = g_screens.sequence;
                    if (g_screens.game_frame != shown_game_frame) {
                        shown_game_frame = g_screens.game_frame;
                        g_present_count = g_present_count + 1;
                        static Uint64 last_frame_ns = 0;
                        const Uint64 frame_ns = SDL_GetTicksNS();
                        if (last_frame_ns) diag_frame_interval(frame_ns - last_frame_ns);
                        last_frame_ns = frame_ns;
                    }
                }
                top = g_screens.top;
                bottom = g_screens.bottom;
            }
            if (top.shared != nullptr) {
                if (top.shared != top_view.picture && shared_screens_show(top_view, top.shared, top.region))
                    new_picture = new_top = true;
            } else if (top.pixels != nullptr && top.pixels != top_view.uploaded) {
                shared_screens_show_pixels(top_view, top.pixels, static_cast<int>(top.w), static_cast<int>(top.h));
                new_picture = new_top = true;
            }
            const bool want_bottom = bottom_shown || bottom_view.texture == nullptr || quick_items_needs_touch_screen();
            if (want_bottom && bottom.shared != nullptr) {
                if (bottom.shared != bottom_view.picture && shared_screens_show(bottom_view, bottom.shared, bottom.region))
                    bottom_dirty = true;
            } else if (want_bottom && bottom.pixels != nullptr && bottom.pixels != bottom_view.uploaded) {
                shared_screens_show_pixels(bottom_view, bottom.pixels, static_cast<int>(bottom.w), static_cast<int>(bottom.h));
                bottom_dirty = true;
            }
            // (the renderer's pictures its GPU had not finished: the newest one finished since)
            if (pc_screens_poll(top_view)) new_picture = new_top = true;
            if (pc_screens_poll(bottom_view)) bottom_dirty = true;
            if (bottom_dirty) new_picture = true, bottom_dirty = false;
        }
        SDL_FRect town_rect{};
        float town_alpha = 1;
        const bool town_large = town_view(win_w, win_h, &town_rect, &town_alpha);
        bottom_shown = ui_bottom_wanted() || town_large;
        if (settings_panel_closed_now()) {
            display = ReadDisplayOptions();
            pc_display_apply(window);
            SDL_SetRenderVSync(renderer, pc_display_vsync() ? 1 : 0);
            pc_vsync_lock(pc_display_vsync());
            pc_screens_set_upscale(pc_display_upscale());
            ColourAdjust colour;
            app_colour(&colour);
            shared_screens_set_colour(colour);
            shared_screens_set_ps1(app_setting_bool("Graphics", "Ps1Filter", false),
                                   app_low_end_level(app_setting("Graphics", "LowEnd", "off")) == LOW_END_BRICK);
            ApplyFsr1();
        }
        layout = ComputeLayout(win_w, win_h, static_cast<u32>(top_view.width), static_cast<u32>(top_view.height),
                               static_cast<u32>(bottom_view.width), static_cast<u32>(bottom_view.height), bottom_shown, display);
        {  // (the part of the picture shown: the HUD's sides go to its edges)
            static float told = -1;
            const float shown = 1.0f - 2.0f * layout.crop;
            if (std::fabs(shown - told) > 0.002f) {
                Port::Hooks::SetShownWidth(shown);
                told = shown;
            }
        }
        quick_items_update(win_w, win_h, bottom_shown);
        {
            static unsigned every_read = ~0u;
            static bool every_frame = false;
            if (every_read != app_settings_version())
                every_read = app_settings_version(), every_frame = app_setting_bool("Graphics", "QuickItemsEveryFrame", false);
            const int mode = bottom_shown ? 0 : quick_items_needs_touch_screen() ? (every_frame ? 0 : 2) : 1;
            if (mode == 2 && g_touch_mode.load(std::memory_order_relaxed) != 2) {
                g_items_hot_until.store(SDL_GetTicks() + 600, std::memory_order_relaxed);
                if (g_touch_mode.load(std::memory_order_relaxed) == 1) g_items_picture.store(false, std::memory_order_relaxed);
            }
            g_touch_mode.store(mode, std::memory_order_relaxed);
        }
        if (town_large) {
            layout.bottom = town_rect, layout.bottom_shown = true;
            layout.bottom_alpha = static_cast<u8>(std::clamp(town_alpha, 0.0f, 1.0f) * 255);
        }
        {
            const Port::Hooks::GameState game = Port::Hooks::CurrentGameState();
            {  // [place]: where the game is, at each change that holds a second (android_main.cpp's)
                static int last_place = -2, last_front = -2, last_weapon = -2, pending_key = 0;
                static Uint64 since_ns = 0, changed_ns = 0;
                const bool differs = game.place != last_place || game.front_end != last_front ||
                                     (game.place == Port::Hooks::GameState::PLACE_QUEST && game.weapon != last_weapon);
                const int key = (game.place + 2) * 1000 + (game.front_end + 2) * 100 + (game.weapon + 2);
                const Uint64 now_ns = SDL_GetTicksNS();
                if (!differs) changed_ns = 0;
                else if (changed_ns == 0 || key != pending_key) changed_ns = now_ns, pending_key = key;
                if (differs && now_ns - changed_ns >= 1000000000ull) {
                    changed_ns = 0;
                    const char* where = game.front_end == 1 ? "the title, a save's choice or the creation"
                                        : game.place == Port::Hooks::GameState::PLACE_TOWN ? "a town (a village or the Gathering Hall)"
                                        : game.place == Port::Hooks::GameState::PLACE_QUEST ? "a quest"
                                        : game.place == Port::Hooks::GameState::PLACE_OTHER ? "another place (a load, the Arena's gear...)"
                                                                                           : "not known (a place no rule covers)";
                    char weapon[32] = "";
                    if (game.place == Port::Hooks::GameState::PLACE_QUEST && game.weapon >= 0)
                        std::snprintf(weapon, sizeof weapon, ", weapon class %d", game.weapon);
                    std::fprintf(stderr, "[place] %s: %s%s%s (game frame %u)\n", diag_clock(), where, weapon,
                                 since_ns ? (", after " + std::to_string((now_ns - since_ns) / 1000000000ull) + " s").c_str() : "",
                                 shown_game_frame);
                    since_ns = now_ns;
                    last_place = game.place, last_front = game.front_end, last_weapon = game.weapon;
                }
            }
            UiGameState state;
            state.menu = game.menu;
            state.weapon_drawn = game.weapon_drawn;
            state.ranged = game.ranged;
            state.bow_type = game.bow_type;
            state.item_bar_shown = game.item_bar_shown;
            state.top_width = game.top_width;
            state.place = game.place;
            state.front_end = game.front_end;
            state.action = game.action;
            const SDL_FRect& t = layout.top_full;
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
                // (1.0.2: the size it is drawn at, toward its corner, and hidden: the HUD editor's sizes, as Android's)
                state.hud[k].scale = game.hud[k].scale, state.hud[k].hidden = game.hud[k].hidden;
                state.hud[k].anchor = SDL_FPoint{t.x + game.hud[k].anchor[0] * t.w, t.y + game.hud[k].anchor[1] * t.h};
            }
            ui_set_game_state(state);
            town_set_game(game.place, game.menu, game.frame, game.town_panel);
        }
        {
            // A present when there is something to show (a new picture, an input, the menu open), at 30 a second for a
            // moment after an input, else 4 a second; otherwise the input goes and the loop waits for an event a moment
            const u64 now = SDL_GetTicks();
            const bool recent_input = now - last_input_ms < 400;
            const bool due = now - last_present_ms >= (recent_input ? 33u : 250u);
            const bool nothing_new = !new_picture && !ui_settings_open();
            if (nothing_new && !had_events && !due && !live) {
                send_input();
                SDL_WaitEventTimeout(nullptr, pc_screens_pending(views, 2) ? 1 : recent_input ? 4 : 16);
                return;
            }
            last_present_ms = now;
            new_picture = false;
        }
        // FRAMEGEN ([Graphics] FrameGen, frame_gen.h, as android_main.cpp): a new top picture with VSync on and the FPS
        // limit a whole fraction of the display's refresh rate (half or less): the picture made between the last one and
        // it is presented first, the touch screen and the menus over it as over the game's; the game's is presented next
        {
            static float refresh = 0;
            static unsigned read_at = ~0u;
            if (read_at != app_settings_version() || refresh <= 0) {
                read_at = app_settings_version();
                const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
                refresh = mode != nullptr && mode->refresh_rate > 0 ? mode->refresh_rate : 60.0f;
                mh4n::pacing::g_display_period_ns.store(static_cast<u64>(1e9 / refresh), std::memory_order_relaxed);
            }
            const float cap = perfstats_fps_cap();
            const int n = cap > 0 && cap < 1000 ? static_cast<int>(std::lround(refresh / cap)) : 1;
            pc_set_cadence(pc_display_vsync() && n >= 2 && std::fabs(refresh / n - cap) <= 0.03f * cap ? n : 1);
            // (performance) VSync on and the FPS limit a whole fraction of the display's rate (60 on a 120 or 240 Hz
            // display, 30 on 60, 25 on 75): the game's VBlank at the display's own rate, held to its vsync (pc_vsync.h),
            // and a game frame every n of them (mh4n hooks: the VBlank's grid), one picture every n vsyncs; otherwise the
            // VBlank as the limit sets it (0: the 3DS's 59.83 Hz, or the limit's own rate above 60)
            // (a refresh_rate of mh4u.ini's own is left as it is)
            static std::string told_rate;
            static const bool own_rate = std::strtof(game_option_get("refresh_rate", "0").c_str(), nullptr) > 0;
            const std::string rate = own_rate ? std::string()
                                     : pc_display_vsync() && n >= 2 && std::fabs(refresh / n - cap) <= 0.01f * cap
                                         ? std::to_string(refresh)
                                         : "0";
            if (!rate.empty() && rate != told_rate && game_option_preview("refresh_rate", rate)) {
                if (!told_rate.empty() || rate != "0")
                    Logf("the game's VBlank: %s", rate == "0" ? "as the FPS limit sets it"
                                                              : ("the display's " + rate + " Hz, a frame every " +
                                                                 std::to_string(n) + " vsyncs").c_str());
                told_rate = rate;
            }
        }
        if (new_top && frame_gen_enabled() && !live) {
            const Port::Hooks::GameState game = Port::Hooks::CurrentGameState();
            const char* skip = ui_settings_open() || touch_editor_holds() ? "the settings or the layout editor are open"
                               : game.front_end == 1                       ? "the title or a save's choice"
                               : game.menu == 1                            ? "a menu or a window of the game's"
                                                                           : nullptr;
            FrameGenBox boxes[Port::Hooks::HUD_PART_COUNT];
            int nb = 0;
            for (int k = 0; k < Port::Hooks::HUD_PART_COUNT; ++k)
                if (game.hud[k].shown == 1) std::memcpy(boxes[nb++].r, game.hud[k].rect, sizeof boxes[0].r);
            int follow = 0;
            SDL_Texture* made = frame_gen_make(renderer, top_view, present_pacing_cadence(), boxes, nb, skip, &follow);
            if (made != nullptr) {
                const bool hdr = pc_hdr_begin(renderer, win_w, win_h);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                SDL_RenderClear(renderer);
                SDL_FRect top = layout.top;
                if (top.y < 0.5f) {
                    const float row = top.h / 240.0f * 2;
                    top.y -= row;
                    top.h += row;
                }
                SharedScreenView made_view;
                float fw = 0, fh = 0;
                SDL_GetTextureSize(made, &fw, &fh);
                made_view.texture = made;
                made_view.top = true;
                made_view.source = SDL_FRect{0, 0, fw, fh};
                made_view.width = static_cast<int>(fw), made_view.height = static_cast<int>(fh);
                if (DrawTopFsr1(renderer, made_view, layout.crop, top, win_w, win_h)) {
                } else if (layout.crop > 0) {
                    shared_screens_draw_part(made_view, SDL_FRect{layout.crop, 0, 1 - 2 * layout.crop, 1}, top, win_w, win_h);
                } else {
                    shared_screens_draw(made_view, top, win_w, win_h);
                }
                if (layout.bottom_shown) shared_screens_draw(bottom_view, layout.bottom, win_w, win_h, layout.bottom_alpha);
                if (hdr) pc_hdr_overlay(renderer);
                ui_frame(win_w, win_h, UiScreens{layout.top_full, layout.bottom, layout.bottom_shown}, true);
                if (hdr) pc_hdr_present(renderer, pc_display_vsync());
                else SDL_RenderPresent(renderer);
                frame_gen_presented();
            }
        }
        new_top = false;
        const bool hdr = pc_hdr_begin(renderer, win_w, win_h);  // (the HDR output: SDL draws into its pictures, pc_hdr.h)
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        {  // (the top screen's first row is an edge the game never draws: drawn one 3DS row taller, that row above the
           // window, when it is at its top, as on Android)
            SDL_FRect top = layout.top;
            if (top.y < 0.5f) {
                const float row = top.h / 240.0f * 2;
                top.y -= row;
                top.h += row;
            }
            if (DrawTopFsr1(renderer, top_view, layout.crop, top, win_w, win_h)) {  // (FSR 1: fsr1.h, 1.0.2)
            } else if (layout.crop > 0) {
                shared_screens_draw_part(top_view, SDL_FRect{layout.crop, 0, 1 - 2 * layout.crop, 1}, top, win_w, win_h);
            } else {
                shared_screens_draw(top_view, top, win_w, win_h);
            }
        }
        if (layout.bottom_shown) {
            if (town_large) {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, static_cast<u8>(160 * std::clamp(town_alpha, 0.0f, 1.0f)));
                SDL_RenderFillRect(renderer, nullptr);
            }
            shared_screens_draw(bottom_view, layout.bottom, win_w, win_h, layout.bottom_alpha);
        }
        if (g_items_picture.load(std::memory_order_relaxed)) quick_items_draw(bottom_view, win_w, win_h, display.opacity);
        static bool first_picture_told = false;
        if (top_view.texture != nullptr && !first_picture_told) {
            first_picture_told = true;
            Logf("the start: the game's first picture at %lld ms", MsSinceProcessStart());
        }
        if (top_view.texture == nullptr) {
            // (no picture of the game's yet: the runtime starting, or a first start's shaders being built) a word that
            // it is on its way, in the menus' gold on their dark wine, rather than a black window
            SDL_SetRenderDrawColor(renderer, 0x2A, 0x0A, 0x10, 255);
            SDL_RenderFillRect(renderer, nullptr);
            const char* title = "MH4U";
            const char* line = settings_spanish() ? "Iniciando el juego..." : "Starting the game...";
            const float big = std::max(2.0f, std::floor(win_h / 120.0f)), small = std::max(1.0f, std::floor(win_h / 360.0f));
            SDL_SetRenderDrawColor(renderer, 0xD4, 0xAF, 0x37, 255);
            SDL_SetRenderScale(renderer, big, big);
            SDL_RenderDebugText(renderer, (win_w / big - 8.0f * std::strlen(title)) / 2, win_h / big / 2 - 12, title);
            SDL_SetRenderScale(renderer, small, small);
            SDL_RenderDebugText(renderer, (win_w / small - 8.0f * std::strlen(line)) / 2, win_h / small / 2 + 10, line);
            SDL_SetRenderScale(renderer, 1, 1);
        }
        if (hdr) pc_hdr_overlay(renderer);  // (the overlay at the paper white, unspread)
        // (the touch controls: hidden while a controller is connected, as on Android; at Auto touch.cpp keeps them hidden
        // until a real touch screen is touched - a keyboard and a mouse play)
        ui_frame(win_w, win_h, UiScreens{layout.top_full, layout.bottom, layout.bottom_shown}, controls_real_pad());
        send_input();
        while (next_shot < g_shot_frames.size() && ScriptClock(shown_game_frame) >= g_shot_frames[next_shot]) {
            SaveShot(renderer, g_wall_clock ? g_shot_frames[next_shot] : shown_game_frame);
            ++next_shot;
        }
        LiveShot(renderer, win_w, win_h);
        mh4n::pacing::Mark(mh4n::pacing::kAppPresentIn, shown_game_frame);  // (MH4N_PACING_TRACE: the window's presents)
        if (hdr) pc_hdr_present(renderer, pc_display_vsync());
        else SDL_RenderPresent(renderer);
        mh4n::pacing::Mark(mh4n::pacing::kAppPresentOut, shown_game_frame);
        {  // (the touch screen in a window of its own: open while the layout says so and the touch screen is shown)
            const bool want = display.mode == DisplayOptions::Window && bottom_shown && !town_large;
            if (want && g_touch_window.window == nullptr) OpenTouchWindow(window);
            else if (!want && g_touch_window.window != nullptr) CloseTouchWindow();
            if (g_touch_window.window != nullptr) {
                ScreenPicture b;
                {
                    std::lock_guard lock(g_screens.mutex);
                    b = g_screens.bottom;
                }
                DrawTouchWindow(bottom_view, b.pixels, static_cast<int>(b.w), static_cast<int>(b.h));
            }
        }
        shared_screens_frame_done();
        PresentStats(shown_game_frame);
        pc_perf_present();
        perfstats_poll();
        send_input();
        if (g_quit_frame != 0 && ScriptClock(shown_game_frame) >= g_quit_frame) {
            Logf("MH4U_TEST_QUIT: game frame %u, closing", shown_game_frame);
            running = false;
            quitting = true;
        }
    };
    LiveDraw live_draw;
    live_draw.window = SDL_GetWindowID(window);
    live_draw.draw = [&] {
        SDL_GetRenderOutputSize(renderer, &win_w, &win_h);
        frame_once(true);
    };
    SDL_AddEventWatch(LiveResizeWatch, &live_draw);
    while (running) {
        had_events = false;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == g_wake_event) {
                g_wake_pending.store(false);
                continue;
            }
            had_events = true;
            last_input_ms = SDL_GetTicks();
            // F1: the settings (Android's gear or START + SELECT), and F1 again closes them; F11 or Alt+Enter: full screen
            // or a window (a modifier held with them is a shortcut's: no tap of its keybind, controls.cpp)
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                if (e.key.scancode == SDL_SCANCODE_F1) {
                    controls_shortcut_used();
                    // (1.0.2: the chat window afloat: the menu in its place, ui_open_settings)
                    if (!ui_settings_open() || settings_panel_chat_floating()) ui_open_settings();
                    else settings_panel_request_close();
                    continue;
                }
                if (e.key.scancode == SDL_SCANCODE_F11 ||
                    (e.key.scancode == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT) != 0)) {
                    controls_shortcut_used();
                    pc_display_toggle(window);
                    continue;
                }
            }
            // (the touch screen's own window: a click or a drag there is a finger on the touch screen, nothing else)
            if (g_touch_window.window != nullptr && TouchWindowEvent(e)) continue;
            if (pc_net_event(e)) continue;  // (the chat key: pc_net.h)
            if (controls_event(e) || ui_event(e, UiScreens{layout.top_full, layout.bottom, layout.bottom_shown})) continue;
            switch (e.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                running = false;
                quitting = true;
                break;
            case SDL_EVENT_WINDOW_MINIMIZED:
                // the game stops where it is while the window is minimized ([Interface] PauseInBackground, as Android's)
                if (auto* rt = g_runtime.load(); rt != nullptr && app_setting_bool("Interface", "PauseInBackground", true)) {
                    rt->Clock().Pause();
                    paused_minimized = true;
                    Logf("the window is minimized: the game waits");
                }
                break;
            case SDL_EVENT_WINDOW_RESTORED:
                if (auto* rt = g_runtime.load(); rt != nullptr && paused_minimized && !paused_for_menu) rt->Clock().Resume();
                if (paused_minimized) Logf("the window is back: the game goes on");
                paused_minimized = false;
                break;
            case SDL_EVENT_FINGER_DOWN: {
                const float x = e.tfinger.x * win_w, y = e.tfinger.y * win_h;
                if (!touching && inside_bottom(x, y)) {
                    touching = true, touching_items = false;
                    touch_finger = e.tfinger.fingerID;
                    touch_at(x, y, true);
                } else if (!touching && !touch_editing() && quick_items_hit(x, y)) {
                    touching = true, touching_items = true;
                    touch_finger = e.tfinger.fingerID;
                    touch_at(x, y, true);
                }
                break;
            }
            case SDL_EVENT_FINGER_MOTION:
                if (touching && e.tfinger.fingerID == touch_finger) touch_at(e.tfinger.x * win_w, e.tfinger.y * win_h, true);
                break;
            case SDL_EVENT_FINGER_UP:
            case SDL_EVENT_FINGER_CANCELED:
                if (touching && e.tfinger.fingerID == touch_finger) {
                    touching = false;
                    if (touching_items) g_items_hot_until.store(SDL_GetTicks() + 1500, std::memory_order_relaxed);
                    touch_at(0, 0, false);
                    touching_items = false;
                }
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                SDL_GetRenderOutputSize(renderer, &win_w, &win_h);
                // (the mouse's and the fingers' places in the new layout at once, not at the next picture)
                layout = ComputeLayout(win_w, win_h, static_cast<u32>(top_view.width), static_cast<u32>(top_view.height),
                                       static_cast<u32>(bottom_view.width), static_cast<u32>(bottom_view.height), bottom_shown,
                                       display);
                break;
            case SDL_EVENT_RENDER_DEVICE_RESET:
            case SDL_EVENT_RENDER_TARGETS_RESET:
                shared_screens_reset(views, 2);
                frame_gen_reset();
                fsr1_reset();
                shown_sequence = 0;
                Logf("the renderer's device was reset: the screens' textures are made again");
                break;
            default:
                break;
            }
        }
        frame_once(false);
    }

    SDL_RemoveEventWatch(LiveResizeWatch, &live_draw);
    if (g_touch_window.window != nullptr) {  // (kept open for the next start: its place)
        CloseTouchWindow();
        app_setting_set("Display", "Layout", "window");
        app_settings_save();
    }
    if (auto* rt = g_runtime.load()) rt->RequestExit(0, "the program is closing");
    shared_screens_shutdown(views, 2);
    {
        std::lock_guard lock(g_screens.mutex);
        g_screens.top = ScreenPicture{};
        g_screens.bottom = ScreenPicture{};
    }
    if (game_thread.joinable()) game_thread.join();
    if (!quitting && g_game_status.load() != 0) pc_crash_open("error");  // (the error screen: pc_crash.cpp)
    for (int i = 0; i < 100 && g_pngs_pending.load() > 0; ++i) SDL_Delay(30);  // (test shots still being written)
    ui_shutdown();
    pc_hdr_shutdown();
    pc_display_remember(window);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (g_restart) Relaunch(g_restart_arg);
    return g_game_status.load();
}
