// mh4u-linux: MH4U on the native runtime for Linux, with the 3DS's two screens in two windows.
//
//   mh4u-linux --game DIR --code LIB [--from-dump BASE,UPDATE] [--user DIR] [--layout dual|stacked] [--fullscreen]
//              [--top-display N] [--bottom-display N] [--scale N] [-- runtime options...]
//
// DIR is a game folder (--from-dump makes it first from the decrypted game and its update), LIB the code library built from
// the player's own dump (mh4n/tools/build_game.py --dump UPDATE.cia --native). `dual` (the default) shows the top
// screen in the window "MH4U" and the touch screen in the window "MH4U Bottom": a compositor that routes windows by
// title (ROCKNIX's Sway on dual-screen handhelds) puts the second on the bottom panel. `stacked` shows both in one
// window, the top screen above the touch screen. --top-display and --bottom-display put each window, fullscreen, on
// that display (SDL's order, 0 first).
//
// The screens come as CPU pixels (RGBA8, upright: the display GPU's readback when no sink asks for shared pictures)
// and are drawn with SDL's renderer. The touch screen takes the mouse and a finger, mapped to its 320 x 240 pixels.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "hooks.h"
#include "input_live/live_input.h"
#include "services/fs/dump_source.h"
#include "mh4n/host.h"
#include "mh4n/host_thread_name.h"
#include "mh4n/options.h"
#include "mh4n/present.h"
#include "mh4n/registry.h"
#include "mh4n/runtime.h"

using mh4n::s16;
using mh4n::u16;
using mh4n::u32;
using mh4n::u64;
using mh4n::u8;

namespace {

// ---- the screens, from the display's thread to the window --------------------------------------------------

struct Picture {
    std::shared_ptr<const std::vector<u8>> pixels;  // RGBA8, w * h, top row first
    u32 w = 0, h = 0;
};

struct Screens {
    std::mutex mutex;
    u64 sequence = 0;
    Picture top, bottom;
} g_screens;

class WindowSink final : public mh4n::present::ScreenSink {
public:
    void Publish(const mh4n::present::DisplayFrame& frame) override {
        std::lock_guard lock(g_screens.mutex);
        Take(frame.top, g_screens.top);
        Take(frame.bottom, g_screens.bottom);
        ++g_screens.sequence;
    }

private:
    static void Take(const std::optional<mh4n::present::ScreenImage>& image, Picture& out) {
        if (!image || !image->IsCpu() || !image->Valid()) return;  // (only CPU pixels: no sink here asks for others)
        out.pixels = image->pixels;
        out.w = image->width;
        out.h = image->height;
    }
};
WindowSink g_sink;

std::atomic<mh4n::Runtime*> g_runtime{nullptr};
std::atomic<bool> g_game_done{false};
std::atomic<int> g_game_status{0};
bool g_said_no_display = false;

void AttachWindow(mh4n::Runtime& rt) {
    if (auto* display = mh4n::present::FindDisplay(rt.Interfaces().gpu)) {
        display->AddScreenSink(&g_sink);
    } else {
        g_said_no_display = true;
        std::fprintf(stderr, "[Error] Linux: the GPU shows no screens: nothing will be drawn\n");
    }
    g_runtime.store(&rt);
}
MH4N_REGISTER(mh4n::BootStep, "zz.linux.window", mh4n::BootStep{mh4n::Phase::Started, AttachWindow});

// ---- the windows -------------------------------------------------------------------------------------------

struct View {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* top = nullptr;
    SDL_Texture* bottom = nullptr;
    u32 top_w = 0, top_h = 0, bottom_w = 0, bottom_h = 0;
    SDL_FRect bottom_rect{};  // where the touch screen was drawn, in the window's pixels (w 0: not drawn)
};

// The largest rectangle of aspect w:h inside `area`, centred.
SDL_FRect Fit(SDL_FRect area, float w, float h) {
    if (w <= 0 || h <= 0 || area.w <= 0 || area.h <= 0) return SDL_FRect{};
    const float k = std::min(area.w / w, area.h / h);
    return SDL_FRect{area.x + (area.w - w * k) / 2, area.y + (area.h - h * k) / 2, w * k, h * k};
}

void Upload(SDL_Renderer* renderer, SDL_Texture*& texture, u32& tw, u32& th, const Picture& picture) {
    if (!picture.pixels) return;
    if (texture == nullptr || tw != picture.w || th != picture.h) {
        if (texture) SDL_DestroyTexture(texture);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                    static_cast<int>(picture.w), static_cast<int>(picture.h));
        tw = picture.w, th = picture.h;
        if (texture == nullptr) {
            std::fprintf(stderr, "[Error] Linux: SDL_CreateTexture: %s\n", SDL_GetError());
            return;
        }
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    }
    SDL_UpdateTexture(texture, nullptr, picture.pixels->data(), static_cast<int>(picture.w * 4));
}

SDL_Window* MakeWindow(const char* title, int w, int h, bool fullscreen, int display_index) {
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, w);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, h);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
    if (display_index >= 0) {
        int count = 0;
        SDL_DisplayID* displays = SDL_GetDisplays(&count);
        if (displays && display_index < count) {
            const SDL_DisplayID id = displays[display_index];
            SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(id));
            SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(id));
        } else {
            std::fprintf(stderr, "[Warning] Linux: no display %d (there are %d)\n", display_index, count);
        }
        SDL_free(displays);
    }
    if (fullscreen) SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, true);
    SDL_Window* window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (window == nullptr) std::fprintf(stderr, "[Error] Linux: SDL_CreateWindow(%s): %s\n", title, SDL_GetError());
    return window;
}

bool MakeView(View& v, const char* title, int w, int h, bool fullscreen, int display_index) {
    v.window = MakeWindow(title, w, h, fullscreen, display_index);
    if (v.window == nullptr) return false;
    v.renderer = SDL_CreateRenderer(v.window, nullptr);
    if (v.renderer == nullptr) {
        std::fprintf(stderr, "[Error] Linux: SDL_CreateRenderer(%s): %s\n", title, SDL_GetError());
        return false;
    }
    SDL_SetRenderVSync(v.renderer, 1);
    return true;
}

void DestroyView(View& v) {
    if (v.top) SDL_DestroyTexture(v.top);
    if (v.bottom) SDL_DestroyTexture(v.bottom);
    if (v.renderer) SDL_DestroyRenderer(v.renderer);
    if (v.window) SDL_DestroyWindow(v.window);
    v = View{};
}

// ---- input -------------------------------------------------------------------------------------------------

constexpr float kCircle = 156.0f;  // the Circle Pad's reach, in HID's units
constexpr float kDeadzone = 0.10f;
constexpr float kYawSpeed = 180.0f, kPitchSpeed = 110.0f, kCurve = 1.6f;  // (as the Android app's controls.cpp)

struct Stick {
    float x = 0, y = 0;  // y up, at most 1 long
};

Stick ReadStick(SDL_Gamepad* pad, SDL_GamepadAxis ax, SDL_GamepadAxis ay) {
    Stick s{SDL_GetGamepadAxis(pad, ax) / 32767.0f, -SDL_GetGamepadAxis(pad, ay) / 32767.0f};
    const float m = std::hypot(s.x, s.y);
    if (m <= kDeadzone) return {};
    const float k = std::min(1.0f, (m - kDeadzone) / (1.0f - kDeadzone)) / m;
    return {s.x * k, s.y * k};
}

s16 Units(float v) {
    return static_cast<s16>(std::lround(std::clamp(v, -1.0f, 1.0f) * kCircle));
}

struct Controls {
    SDL_Gamepad* pad = nullptr;
    u64 last_ns = 0;
    bool quit_combo = false;
};

void OpenFirstPad(Controls& c) {
    if (c.pad) return;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids && count > 0) {
        c.pad = SDL_OpenGamepad(ids[0]);
        if (c.pad) std::fprintf(stderr, "[Info] Linux: controller %s\n", SDL_GetGamepadName(c.pad));
    }
    SDL_free(ids);
}

// The game's buttons by their place, as on the 3DS: the right face button is A, the bottom one B.
mh4n::InputState ReadControls(Controls& c, float* cam_yaw, float* cam_pitch) {
    mh4n::InputState in;
    *cam_yaw = 0, *cam_pitch = 0;
    const u64 now = SDL_GetTicksNS();
    const float dt = c.last_ns ? std::min<u64>(now - c.last_ns, 50'000'000) * 1e-9f : 0.0f;
    c.last_ns = now;
    Stick left, right;
    if (c.pad) {
        struct Map {
            SDL_GamepadButton button;
            u32 bit;
        };
        static constexpr Map kMap[] = {
            {SDL_GAMEPAD_BUTTON_EAST, mh4n::kPadA},          {SDL_GAMEPAD_BUTTON_SOUTH, mh4n::kPadB},
            {SDL_GAMEPAD_BUTTON_NORTH, mh4n::kPadX},         {SDL_GAMEPAD_BUTTON_WEST, mh4n::kPadY},
            {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, mh4n::kPadL}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, mh4n::kPadR},
            {SDL_GAMEPAD_BUTTON_START, mh4n::kPadStart},     {SDL_GAMEPAD_BUTTON_BACK, mh4n::kPadSelect},
            {SDL_GAMEPAD_BUTTON_DPAD_UP, mh4n::kPadUp},      {SDL_GAMEPAD_BUTTON_DPAD_DOWN, mh4n::kPadDown},
            {SDL_GAMEPAD_BUTTON_DPAD_LEFT, mh4n::kPadLeft},  {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, mh4n::kPadRight},
        };
        for (const auto& m : kMap) {
            if (SDL_GetGamepadButton(c.pad, m.button)) in.buttons |= m.bit;
        }
        in.zl = SDL_GetGamepadAxis(c.pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000;
        in.zr = SDL_GetGamepadAxis(c.pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000;
        left = ReadStick(c.pad, SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY);
        right = ReadStick(c.pad, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY);
        c.quit_combo = SDL_GetGamepadButton(c.pad, SDL_GAMEPAD_BUTTON_BACK) &&
                       SDL_GetGamepadButton(c.pad, SDL_GAMEPAD_BUTTON_GUIDE);
    }
    // the keyboard, as the Android app's: W A S D move, I J K L the camera, the arrows the D-pad
    const bool* keys = SDL_GetKeyboardState(nullptr);
    struct Key {
        SDL_Scancode key;
        u32 bit;
    };
    static constexpr Key kKeys[] = {
        {SDL_SCANCODE_O, mh4n::kPadA},      {SDL_SCANCODE_F, mh4n::kPadA},         {SDL_SCANCODE_SPACE, mh4n::kPadB},
        {SDL_SCANCODE_U, mh4n::kPadX},      {SDL_SCANCODE_R, mh4n::kPadY},         {SDL_SCANCODE_Q, mh4n::kPadL},
        {SDL_SCANCODE_E, mh4n::kPadR},      {SDL_SCANCODE_RETURN, mh4n::kPadStart}, {SDL_SCANCODE_BACKSPACE, mh4n::kPadSelect},
        {SDL_SCANCODE_UP, mh4n::kPadUp},    {SDL_SCANCODE_DOWN, mh4n::kPadDown},   {SDL_SCANCODE_LEFT, mh4n::kPadLeft},
        {SDL_SCANCODE_RIGHT, mh4n::kPadRight},
    };
    for (const auto& k : kKeys) {
        if (keys[k.key]) in.buttons |= k.bit;
    }
    in.zl |= keys[SDL_SCANCODE_TAB];
    in.zr |= keys[SDL_SCANCODE_T];
    const float walk = keys[SDL_SCANCODE_LSHIFT] ? 0.45f : 1.0f;
    const float kx = static_cast<float>(keys[SDL_SCANCODE_D] - keys[SDL_SCANCODE_A]);
    const float ky = static_cast<float>(keys[SDL_SCANCODE_W] - keys[SDL_SCANCODE_S]);
    if (kx != 0 || ky != 0) {
        const float k = walk * (kx != 0 && ky != 0 ? 0.7071f : 1.0f);
        left = {kx * k, ky * k};
    }
    const float cx = static_cast<float>(keys[SDL_SCANCODE_L] - keys[SDL_SCANCODE_J]);
    const float cy = static_cast<float>(keys[SDL_SCANCODE_I] - keys[SDL_SCANCODE_K]);
    if (cx != 0 || cy != 0) right = {cx, cy};
    in.circle_x = Units(left.x), in.circle_y = Units(left.y);
    // the right stick turns the camera while the game's camera takes the port's turn, else it is the C-stick
    const float m = std::hypot(right.x, right.y);
    if (Port::Hooks::CameraTurnActive()) {
        if (m > 0 && dt > 0) {
            const float k = std::pow(std::min(m, 1.0f), kCurve) / m * dt;
            *cam_yaw = right.x * k * kYawSpeed;
            *cam_pitch = right.y * k * kPitchSpeed;
        }
    } else {
        in.cstick_x = Units(right.x), in.cstick_y = Units(right.y);
    }
    return in;
}

// A point of a window to the touch screen's pixels, or nothing when it is outside the drawn touch screen.
std::optional<mh4n::input_live::TouchPoint> TouchAt(const View& v, float x, float y) {
    const SDL_FRect& r = v.bottom_rect;
    if (r.w <= 0 || r.h <= 0 || x < r.x || y < r.y || x >= r.x + r.w || y >= r.y + r.h) return std::nullopt;
    const float u = std::clamp((x - r.x) / r.w, 0.0f, 0.999f), w = std::clamp((y - r.y) / r.h, 0.0f, 0.999f);
    return mh4n::input_live::TouchPoint{static_cast<u16>(u * 320), static_cast<u16>(w * 240)};
}

// ---- options -----------------------------------------------------------------------------------------------

struct Settings {
    std::string game, code, user, from_dump;
    bool stacked = false;
    bool fullscreen = false;
    int top_display = -1, bottom_display = -1;
    int scale = 3;
    std::vector<std::string> extra;
};

const char* kUsage =
    "usage: mh4u-linux --game DIR --code LIB [--from-dump BASE,UPDATE] [--user DIR] [--layout dual|stacked] [--fullscreen]\n"
    "                  [--top-display N] [--bottom-display N] [--scale N] [-- runtime options...]\n";

bool ParseArgs(int argc, char** argv, Settings& s) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&](std::string& out) {
            if (i + 1 >= argc) return false;
            out = argv[++i];
            return true;
        };
        std::string v;
        if (a == "--") {
            for (++i; i < argc; ++i) s.extra.emplace_back(argv[i]);
        } else if (a == "--game") {
            if (!value(s.game)) return false;
        } else if (a == "--code") {
            if (!value(s.code)) return false;
        } else if (a == "--from-dump") {
            if (!value(s.from_dump)) return false;
        } else if (a == "--user") {
            if (!value(s.user)) return false;
        } else if (a == "--layout") {
            if (!value(v) || (v != "dual" && v != "stacked")) return false;
            s.stacked = v == "stacked";
        } else if (a == "--fullscreen") {
            s.fullscreen = true;
        } else if (a == "--top-display") {
            if (!value(v)) return false;
            s.top_display = std::atoi(v.c_str());
            s.fullscreen = true;
        } else if (a == "--bottom-display") {
            if (!value(v)) return false;
            s.bottom_display = std::atoi(v.c_str());
            s.fullscreen = true;
        } else if (a == "--scale") {
            if (!value(v)) return false;
            s.scale = std::clamp(std::atoi(v.c_str()), 1, 10);
        } else if (a == "--help" || a == "-h") {
            return false;
        } else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return false;
        }
    }
    if (s.user.empty()) {
        const char* data = std::getenv("XDG_DATA_HOME");
        const char* home = std::getenv("HOME");
        s.user = data && *data ? std::string(data) + "/mh4u-linux" : std::string(home ? home : ".") + "/.local/share/mh4u-linux";
    }
    return !s.game.empty() && !s.code.empty();
}

}  // namespace

int main(int argc, char* argv[]) {
    Settings settings;
    if (!ParseArgs(argc, argv, settings)) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "[Error] Linux: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_CreateDirectory(settings.user.c_str());
    if (!settings.from_dump.empty()) {
        const auto comma = settings.from_dump.find(',');
        std::string error;
        if (comma == std::string::npos ||
            !mh4n::fs::dump::PrepareGameFolder(settings.from_dump.substr(0, comma), settings.from_dump.substr(comma + 1),
                                               settings.game, &error)) {
            std::fprintf(stderr, "--from-dump: %s\n", comma == std::string::npos ? "want BASE,UPDATE" : error.c_str());
            SDL_Quit();
            return 2;
        }
    }

    // the windows: the top screen's at 5:3 (the game draws to the window's shape), the touch screen's at 4:3
    View top_view, bottom_view;
    if (!MakeView(top_view, "MH4U", 1000, settings.stacked ? 1200 : 600, settings.fullscreen, settings.top_display)) {
        SDL_Quit();
        return 1;
    }
    if (!settings.stacked &&
        !MakeView(bottom_view, "MH4U Bottom", 640, 480, settings.fullscreen, settings.bottom_display)) {
        DestroyView(top_view);
        SDL_Quit();
        return 1;
    }
    int win_w = 0, win_h = 0;
    SDL_GetWindowSizeInPixels(top_view.window, &win_w, &win_h);
    if (settings.stacked) win_h /= 2;  // (the top screen's half of the window)
    Port::Hooks::SetWindowSize(static_cast<std::uint32_t>(std::max(win_w, win_h)),
                               static_cast<std::uint32_t>(std::min(win_w, win_h)));

    // the game, on its own thread
    std::vector<std::string> args = {"mh4u", settings.game, "--code", settings.code, "--user", settings.user,
                                     "--gpu", "native-display", "--input-source", "live", "--audio", "sdl",
                                     "--frames", "4000000000", "--scale", std::to_string(settings.scale),
                                     "--input-latch", "none"};
    args.insert(args.end(), settings.extra.begin(), settings.extra.end());
    std::thread game_thread([args] {
        mh4n::SetHostThreadName("mh4u-game");
        std::vector<const char*> argv2;
        for (const auto& a : args) argv2.push_back(a.c_str());
        const mh4n::Options options = mh4n::ParseOptions("runner", static_cast<int>(argv2.size()), argv2.data());
        int status = 2;
        if (options.ok()) {
            mh4n::Runtime runtime;
            status = runtime.Boot(options);
        }
        std::fprintf(stderr, "[Info] Linux: the game ended with status %d\n", status);
        g_game_status.store(status);
        g_runtime.store(nullptr);
        g_game_done.store(true);
    });

    Controls controls;
    OpenFirstPad(controls);
    std::shared_ptr<mh4n::input_live::LiveInput> live;
    mh4n::InputState last_sent;
    bool have_sent = false;
    bool touching = false;
    SDL_WindowID touch_window = 0;
    u64 shown_sequence = 0;
    bool running = true;
    bool quitting = false;

    auto view_of = [&](SDL_WindowID id) -> View* {
        if (top_view.window && SDL_GetWindowID(top_view.window) == id) return &top_view;
        if (bottom_view.window && SDL_GetWindowID(bottom_view.window) == id) return &bottom_view;
        return nullptr;
    };
    auto set_touch = [&](std::optional<mh4n::input_live::TouchPoint> p) {
        if (live) live->SetTouch(p);
    };
    auto touch_at = [&](SDL_WindowID id, float x, float y) {
        View* v = view_of(id);
        if (v == nullptr) return;
        if (auto p = TouchAt(*v, x, y)) {
            touching = true, touch_window = id;
            set_touch(p);
        } else if (touching && touch_window == id) {
            touching = false;
            set_touch(std::nullopt);
        }
    };

    while (running) {
        if (!live) {
            if (auto* rt = g_runtime.load()) live = mh4n::input_live::Find(*rt);
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            // the mouse and fingers in the renderer's pixels, where bottom_rect is (high pixel density, fingers' 0..1)
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_FINGER_DOWN ||
                e.type == SDL_EVENT_FINGER_MOTION || e.type == SDL_EVENT_FINGER_UP) {
                const SDL_WindowID id = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.windowID
                                        : (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP)
                                            ? e.button.windowID
                                            : e.tfinger.windowID;
                if (View* v = view_of(id)) SDL_ConvertEventToRenderCoordinates(v->renderer, &e);
            }
            switch (e.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                running = false, quitting = true;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (e.key.scancode == SDL_SCANCODE_F11) {
                    View* v = view_of(e.key.windowID);
                    if (v) SDL_SetWindowFullscreen(v->window, !(SDL_GetWindowFlags(v->window) & SDL_WINDOW_FULLSCREEN));
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                // R3 locks on to the monster and lets go, as the Android app's right stick click
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) Port::Hooks::SetLockOn(!Port::Hooks::LockOn());
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                OpenFirstPad(controls);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                if (controls.pad && SDL_GetGamepadID(controls.pad) == e.gdevice.which) {
                    SDL_CloseGamepad(controls.pad);
                    controls.pad = nullptr;
                    OpenFirstPad(controls);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (e.button.button == SDL_BUTTON_LEFT && e.button.which != SDL_TOUCH_MOUSEID) {
                    touch_at(e.button.windowID, e.button.x, e.button.y);
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (touching && e.motion.which != SDL_TOUCH_MOUSEID && (e.motion.state & SDL_BUTTON_LMASK)) {
                    touch_at(e.motion.windowID, e.motion.x, e.motion.y);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.button == SDL_BUTTON_LEFT && e.button.which != SDL_TOUCH_MOUSEID && touching) {
                    touching = false;
                    set_touch(std::nullopt);
                }
                break;
            case SDL_EVENT_FINGER_DOWN:
            case SDL_EVENT_FINGER_MOTION:
                if (e.type == SDL_EVENT_FINGER_DOWN || touching) touch_at(e.tfinger.windowID, e.tfinger.x, e.tfinger.y);
                break;
            case SDL_EVENT_FINGER_UP:
                if (touching) {
                    touching = false;
                    set_touch(std::nullopt);
                }
                break;
            default:
                break;
            }
        }
        if (g_game_done.load()) running = false;

        float yaw = 0, pitch = 0;
        mh4n::InputState in = ReadControls(controls, &yaw, &pitch);
        if (controls.quit_combo) running = false, quitting = true;
        Port::Hooks::SetHostTriggers(in.zl, in.zr);
        if (yaw != 0 || pitch != 0) Port::Hooks::AddCameraTurn(yaw, pitch);
        if (live && (!have_sent || !(in == last_sent))) {
            live->Set(in);
            last_sent = in, have_sent = true;
        }

        // the newest pictures
        Picture top, bottom;
        bool fresh = false;
        {
            std::lock_guard lock(g_screens.mutex);
            if (g_screens.sequence != shown_sequence) {
                shown_sequence = g_screens.sequence;
                top = g_screens.top, bottom = g_screens.bottom;
                fresh = true;
            }
        }
        if (!fresh) {
            SDL_Delay(1);
            continue;
        }
        Upload(top_view.renderer, top_view.top, top_view.top_w, top_view.top_h, top);
        View& bview = settings.stacked ? top_view : bottom_view;
        Upload(bview.renderer, bview.bottom, bview.bottom_w, bview.bottom_h, bottom);

        auto draw = [](View& v, bool with_top, bool with_bottom) {
            int w = 0, h = 0;
            SDL_GetRenderOutputSize(v.renderer, &w, &h);
            SDL_SetRenderDrawColor(v.renderer, 0, 0, 0, 255);
            SDL_RenderClear(v.renderer);
            const float fw = static_cast<float>(w), fh = static_cast<float>(h);
            const bool both = with_top && with_bottom;
            if (with_top && v.top) {
                const SDL_FRect area{0, 0, fw, both ? fh / 2 : fh};
                const SDL_FRect r = Fit(area, static_cast<float>(v.top_w), static_cast<float>(v.top_h));
                SDL_RenderTexture(v.renderer, v.top, nullptr, &r);
            }
            v.bottom_rect = SDL_FRect{};
            if (with_bottom && v.bottom) {
                const SDL_FRect area{0, both ? fh / 2 : 0, fw, both ? fh / 2 : fh};
                v.bottom_rect = Fit(area, static_cast<float>(v.bottom_w), static_cast<float>(v.bottom_h));
                SDL_RenderTexture(v.renderer, v.bottom, nullptr, &v.bottom_rect);
            }
            SDL_RenderPresent(v.renderer);
        };
        if (settings.stacked) {
            draw(top_view, true, true);
        } else {
            draw(top_view, true, false);
            draw(bottom_view, false, true);
        }
    }

    if (auto* rt = g_runtime.load()) rt->RequestExit(0, quitting ? "the window was closed" : "the game stopped");
    if (game_thread.joinable()) game_thread.join();
    live.reset();
    if (controls.pad) SDL_CloseGamepad(controls.pad);
    DestroyView(bottom_view);
    DestroyView(top_view);
    SDL_Quit();
    return g_game_status.load();
}
