// Controllers and a physical keyboard for the game (controls.h).
//
// A press is seen by the game: the game reads the buttons once a frame, so a press shows for kMinPressMs at least, from
// its down event, however short it was. START and SELECT wait kChordWaitMs before the game gets them: pressed together
// (a hand never presses two at the same moment) they open the settings and the game gets neither.
#include "controls.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

#include "app_settings.h"
#include "hooks.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "mh4n/host.h"
#include "town.h"
#include "padmap.h"
#include "settings_panel.h"
#include "ui.h"

namespace {

using u32 = std::uint32_t;

constexpr u32 ZL_BIT = 1u << 14, ZR_BIT = 1u << 15;  // (the triggers, beside the 3DS's kPad bits)
constexpr Uint64 kMinPressMs = 80;
constexpr Uint64 kChordWaitMs = 60;
constexpr Uint64 kOpenGraceMs = 500;  // a controller just opened can't take over (one whose buttons read as held)
constexpr float kCircle = 156.0f;     // the Circle Pad's and the C-stick's reach, in HID's units
constexpr float kTrigger = 8000;      // of 32767: a trigger this far is pressed
constexpr int kTakeOver = 22000;      // of 32767: a stick of another controller this far makes it the one in use
constexpr float kWalk = .45f;         // Shift with W A S D: walking
// The camera at full tilt and speed 100 %, degrees a second (the game's own turn by the C-stick tops out near these)
constexpr float kYawSpeed = 180.0f, kPitchSpeed = 110.0f;
constexpr float kCurve = 1.6f;        // the tilt's power: a small tilt turns slowly, for aiming

enum { R3_LOCKON, R3_L, R3_ZL, R3_ZR, R3_OFF };
// the town's actions' list (town.h): SELECT (its own use in a town, Customize Panels, is the touch screen's), a stick's click
enum { TOWN_SELECT, TOWN_L3, TOWN_R3, TOWN_OFF };

struct Settings {
    bool label = false, cstick = false, invert_x = false, invert_y = false;
    float speed = 1.0f, deadzone = .10f;
    int r3 = R3_LOCKON;
    int town = TOWN_SELECT;
};
Settings g_set;
unsigned g_set_version = ~0u;

struct Pad {
    SDL_Gamepad* pad = nullptr;
    SDL_JoystickID id = 0;
    Uint64 opened_ms = 0;
    bool real = false;
    bool zl = false, zr = false;  // the triggers past kTrigger, as their last motion said
};
std::vector<Pad> g_pads;
SDL_JoystickID g_active = 0;     // the controller the game reads: the one a button was last pressed on
SDL_Gamepad* g_imgui_pad = nullptr;  // the one the settings read (the same, unless it is being mapped)
bool g_imgui_manual = false;         // (ImGui's backend told to read only that one)
SDL_JoystickID g_remapped = 0;       // its map changed with a button held: not the menu's until let go
bool g_scanned = false;

Uint64 g_down_at[32] = {};  // each output bit's last press (pad or key), SDL_GetTicks
bool g_keys[SDL_SCANCODE_COUNT] = {};
bool g_chord = false;  // START and SELECT were pressed together: neither is the game's until both are let go
u32 g_suppress = 0;    // held as the settings closed: not the game's until let go
bool g_was_open = false;
Uint64 g_last_ms = 0;  // the camera's last turn
std::function<void(bool, int, int)> g_set_touch;
u32 g_town_presses = 0;
bool g_last_pad = false;  // (controls_last_input_pad)  // the presses while the town's list or cursor has the controller, since controls_input
bool g_debug = false;  // MH4U_PAD_DEBUG=1: every controller's press and push in the log

void read_settings() {
    if (app_settings_version() == g_set_version) return;
    g_set_version = app_settings_version();
    Settings s;
    s.label = !SDL_strcasecmp(app_setting("Controls", "FaceButtons", "position").c_str(), "label");
    s.cstick = !SDL_strcasecmp(app_setting("Controls", "RightStick", "camera").c_str(), "cstick");
    s.speed = std::clamp(app_setting_int("Controls", "StickSpeed", 100), 25, 300) / 100.0f;
    s.invert_x = app_setting_bool("Controls", "InvertX", false);
    s.invert_y = app_setting_bool("Controls", "InvertY", false);
    const std::string r3 = app_setting("Controls", "R3", "lockon");
    s.r3 = !SDL_strcasecmp(r3.c_str(), "L") ? R3_L : !SDL_strcasecmp(r3.c_str(), "ZL") ? R3_ZL
           : !SDL_strcasecmp(r3.c_str(), "ZR") ? R3_ZR : !SDL_strcasecmp(r3.c_str(), "off") ? R3_OFF : R3_LOCKON;
    s.deadzone = std::clamp(app_setting_int("Controls", "Deadzone", 10), 0, 30) / 100.0f;
    const std::string town = app_setting("Controls", "TownMenu", "select");
    s.town = !SDL_strcasecmp(town.c_str(), "l3") ? TOWN_L3 : !SDL_strcasecmp(town.c_str(), "r3") ? TOWN_R3
             : !SDL_strcasecmp(town.c_str(), "off") ? TOWN_OFF : TOWN_SELECT;
    g_set = s;
    std::fprintf(stderr, "[Info] Controls: face buttons by %s, right stick %s, camera %.2fx%s%s, R3 %s, dead zone %.0f%%\n",
                 s.label ? "label" : "position", s.cstick ? "as the C-stick" : "turns the camera", s.speed,
                 s.invert_x ? ", inverted across" : "", s.invert_y ? ", inverted up and down" : "", r3.c_str(),
                 s.deadzone * 100);
}

// ---- which devices are controllers
// A controller in the player's hands hides the touch controls. Android calls other devices a gamepad too: the
// emulator's keyboard ("qwerty2"), virtual and uinput devices (a handheld's touch-to-button mapper), a phone's GPIO keys,
// keypads, and the media keys of a USB or Bluetooth device ("... Consumer Control").
bool name_is_pad(const char* name) {
    static const char* const not_pads[] = {"qwerty", "virtual", "uinput", "gpio", "keypad", "consumer control"};
    if (!name) return true;  // (no name: SDL still maps it as a gamepad)
    for (const char* k : not_pads)
        if (SDL_strcasestr(name, k)) return false;
    return true;
}

Pad* find_pad(SDL_JoystickID id) {
    for (Pad& p : g_pads)
        if (p.id == id) return &p;
    return nullptr;
}

void log_pad(SDL_JoystickID id, const char* what) {
    const char* name = SDL_GetGamepadNameForID(id);
    std::fprintf(stderr, "[Info] Controls: %s: %s (%04x:%04x)%s\n", what, name ? name : "?", SDL_GetGamepadVendorForID(id),
                 SDL_GetGamepadProductForID(id), padmap_custom(id) ? ", mapped by the player" : "");
}

bool any_button(SDL_Gamepad* g) {
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++)
        if (SDL_GetGamepadButton(g, static_cast<SDL_GamepadButton>(b))) return true;
    return false;
}

// The settings (ImGui's backend) read the controller the game reads, unless it is the one being mapped, or its map
// changed while a button was held (that button would read as another one: the menu waits for it to be let go)
void imgui_pad() {
    Pad* p = find_pad(g_active);
    if (g_remapped && (!p || p->id != g_remapped || !any_button(p->pad))) g_remapped = 0;
    SDL_Gamepad* want = p && p->id != padmap_joystick() && p->id != g_remapped ? p->pad : nullptr;
    if (want == g_imgui_pad && g_imgui_manual) return;
    g_imgui_pad = want, g_imgui_manual = true;
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, want ? &want : nullptr, want ? 1 : 0);
}

void open_pad(SDL_JoystickID id) {
    if (find_pad(id)) return;
    SDL_Gamepad* g = SDL_OpenGamepad(id);
    if (!g) {
        std::fprintf(stderr, "[Info] Controls: gamepad %u failed to open: %s\n", (unsigned)id, SDL_GetError());
        return;
    }
    Pad p;
    p.pad = g, p.id = id, p.opened_ms = SDL_GetTicks();
    p.real = name_is_pad(SDL_GetGamepadName(g)) || padmap_custom(id);
    g_pads.push_back(p);
    if (!g_active) g_active = id;
    log_pad(id, g_active == id ? "controller in use" : "controller open too (a button pressed on it makes it the one in use)");
    std::fprintf(stderr, "[Info] Controls: %s %s\n", SDL_GetGamepadName(g) ? SDL_GetGamepadName(g) : "?",
                 p.real ? "counts as a controller: the touch controls hide" : "is not a controller for the touch controls");
    imgui_pad();
}

void close_pad(SDL_JoystickID id) {
    auto it = std::find_if(g_pads.begin(), g_pads.end(), [id](const Pad& p) { return p.id == id; });
    if (it == g_pads.end()) return;
    const char* name = SDL_GetGamepadName(it->pad);
    std::fprintf(stderr, "[Info] Controls: controller removed%s: %s\n", g_active == id ? " (it was in use)" : "",
                 name ? name : "?");
    SDL_Gamepad* g = it->pad;
    g_pads.erase(it);
    if (g_active == id) {
        g_active = g_pads.empty() ? 0 : g_pads.front().id;
        if (g_active) log_pad(g_active, "controller in use");
    }
    imgui_pad();  // (before the gamepad goes: ImGui holds it)
    SDL_CloseGamepad(g);
}

// A joystick SDL has no gamepad map for is not read at all until the player maps it, and nothing said so: a notice
// says where to map it (Controls, Map a controller), once per controller and session
void joystick_seen(SDL_JoystickID id) {
    if (g_debug) {
        const char* name = SDL_GetJoystickNameForID(id);
        std::fprintf(stderr, "[Info] Controls: joystick %u: %s (%04x:%04x), %s\n", (unsigned)id, name ? name : "?",
                     SDL_GetJoystickVendorForID(id), SDL_GetJoystickProductForID(id), SDL_IsGamepad(id) ? "a gamepad" : "no gamepad map");
    }
    if (SDL_IsGamepad(id)) return;
    static std::vector<SDL_JoystickID> told;
    if (std::find(told.begin(), told.end(), id) != told.end()) return;
    told.push_back(id);
    const char* name = SDL_GetJoystickNameForID(id);
    std::fprintf(stderr, "[Info] Controls: joystick %s (%04x:%04x) has no gamepad map: not read until it is mapped "
                         "(Controls, Map a controller)\n",
                 name ? name : "?", SDL_GetJoystickVendorForID(id), SDL_GetJoystickProductForID(id));
    if (!name_is_pad(name)) return;  // (not a controller in the player's hands)
    const std::string who = name ? name : "";
    settings_panel_notice((who.empty() ? std::string("A controller") : who) +
                              ": to use it, map it in the settings: Controls, Map a controller.",
                          (who.empty() ? std::string("Un mando") : who) +
                              ": para usarlo, mapéalo en las opciones: Controles, Mapear un mando.");
}

void scan() {
    g_scanned = true;
    int n = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&n);
    for (int i = 0; i < n; i++) open_pad(ids[i]);
    SDL_free(ids);
    ids = SDL_GetJoysticks(&n);
    for (int i = 0; i < n; i++) joystick_seen(ids[i]);
    SDL_free(ids);
}

// ---- buttons
// Face buttons: SDL names them by their place. Position: as on the 3DS (A on the right). Label: the letter printed on
// the button, an Xbox pad's letter at a place with none (PlayStation's shapes); a mapped controller by position (it was
// mapped that way)
u32 face_bit(SDL_Gamepad* p, SDL_GamepadButton b) {
    const int i = b == SDL_GAMEPAD_BUTTON_SOUTH ? 0 : b == SDL_GAMEPAD_BUTTON_EAST ? 1 : b == SDL_GAMEPAD_BUTTON_WEST ? 2 : 3;
    static const u32 by_place[4] = {mh4n::kPadB, mh4n::kPadA, mh4n::kPadY, mh4n::kPadX};
    static const u32 xbox_at[4] = {mh4n::kPadA, mh4n::kPadB, mh4n::kPadX, mh4n::kPadY};
    if (!g_set.label || padmap_custom(SDL_GetGamepadID(p))) return by_place[i];
    switch (SDL_GetGamepadButtonLabel(p, b)) {
    case SDL_GAMEPAD_BUTTON_LABEL_A: return mh4n::kPadA;
    case SDL_GAMEPAD_BUTTON_LABEL_B: return mh4n::kPadB;
    case SDL_GAMEPAD_BUTTON_LABEL_X: return mh4n::kPadX;
    case SDL_GAMEPAD_BUTTON_LABEL_Y: return mh4n::kPadY;
    default: return xbox_at[i];
    }
}

u32 button_bit(SDL_Gamepad* p, SDL_GamepadButton b) {
    switch (b) {
    case SDL_GAMEPAD_BUTTON_SOUTH:
    case SDL_GAMEPAD_BUTTON_EAST:
    case SDL_GAMEPAD_BUTTON_WEST:
    case SDL_GAMEPAD_BUTTON_NORTH: return face_bit(p, b);
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return mh4n::kPadL;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return mh4n::kPadR;
    case SDL_GAMEPAD_BUTTON_START: return mh4n::kPadStart;
    case SDL_GAMEPAD_BUTTON_BACK: return mh4n::kPadSelect;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return mh4n::kPadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return mh4n::kPadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return mh4n::kPadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return mh4n::kPadRight;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        return g_set.r3 == R3_L ? mh4n::kPadL : g_set.r3 == R3_ZL ? ZL_BIT : g_set.r3 == R3_ZR ? ZR_BIT : 0;
    default: return 0;  // (the left stick's click shows or hides the touch screen; Guide opens the settings: ui.cpp)
    }
}

u32 pad_held(SDL_Gamepad* p) {
    static const SDL_GamepadButton buttons[] = {
        SDL_GAMEPAD_BUTTON_SOUTH,     SDL_GAMEPAD_BUTTON_EAST,      SDL_GAMEPAD_BUTTON_WEST,          SDL_GAMEPAD_BUTTON_NORTH,
        SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_BACK,
        SDL_GAMEPAD_BUTTON_DPAD_UP,   SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,     SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
        SDL_GAMEPAD_BUTTON_RIGHT_STICK};
    u32 b = 0;
    for (SDL_GamepadButton sb : buttons)
        if (SDL_GetGamepadButton(p, sb)) b |= button_bit(p, sb);
    if (SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > kTrigger) b |= ZL_BIT;
    if (SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > kTrigger) b |= ZR_BIT;
    return b;
}

// The keyboard (MHGU's keys where it has them, and the camera on I J K L)
u32 key_bit(SDL_Scancode k) {
    switch (k) {
    case SDL_SCANCODE_O:
    case SDL_SCANCODE_F: return mh4n::kPadA;
    case SDL_SCANCODE_SPACE:
    case SDL_SCANCODE_ESCAPE: return mh4n::kPadB;
    case SDL_SCANCODE_U:
    case SDL_SCANCODE_X: return mh4n::kPadX;
    case SDL_SCANCODE_R:
    case SDL_SCANCODE_Y: return mh4n::kPadY;
    case SDL_SCANCODE_Q: return mh4n::kPadL;
    case SDL_SCANCODE_E: return mh4n::kPadR;
    case SDL_SCANCODE_TAB: return ZL_BIT;
    case SDL_SCANCODE_T: return ZR_BIT;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: return mh4n::kPadStart;
    case SDL_SCANCODE_BACKSPACE: return mh4n::kPadSelect;
    case SDL_SCANCODE_UP: return mh4n::kPadUp;
    case SDL_SCANCODE_DOWN: return mh4n::kPadDown;
    case SDL_SCANCODE_LEFT: return mh4n::kPadLeft;
    case SDL_SCANCODE_RIGHT: return mh4n::kPadRight;
    default: return 0;
    }
}

// The keyboard types into a text (the settings' keyboard, the game's text on the phone's keyboard): its keys are not the
// game's buttons meanwhile
bool typing() {
    SDL_Window* w = SDL_GetKeyboardFocus();
    return w && SDL_TextInputActive(w);
}

u32 keys_held() {
    u32 b = 0;
    for (int k = 0; k < SDL_SCANCODE_COUNT; k++)
        if (g_keys[k]) b |= key_bit(static_cast<SDL_Scancode>(k));
    return b;
}

// (not while the settings are open: a press there is the settings', and its release must not reach the game after them)
void pressed(u32 bits) {
    if (town_takes_pad() && !ui_settings_open() && !padmap_active()) g_town_presses |= bits;  // (however short: town.h)
    if (ui_settings_open() || padmap_active() || town_takes_pad() || settings_panel_thanks_open()) return;
    const Uint64 now = SDL_GetTicks();
    for (u32 b = bits; b; b &= b - 1) g_down_at[__builtin_ctz(b)] = now;
}

// START or SELECT went down: with the other one held on the same controller, the settings open (or close)
bool chord(SDL_Gamepad* p, SDL_GamepadButton b) {
    if (b != SDL_GAMEPAD_BUTTON_START && b != SDL_GAMEPAD_BUTTON_BACK) return false;
    const SDL_GamepadButton other = b == SDL_GAMEPAD_BUTTON_START ? SDL_GAMEPAD_BUTTON_BACK : SDL_GAMEPAD_BUTTON_START;
    if (!SDL_GetGamepadButton(p, other)) return false;
    g_chord = true;
    g_down_at[__builtin_ctz(mh4n::kPadStart)] = g_down_at[__builtin_ctz(mh4n::kPadSelect)] = 0;  // (the first one's press)
    if (ui_settings_open()) {
        std::fprintf(stderr, "[Info] Controls: START + SELECT: the settings close\n");
        settings_panel_request_close();
    } else {
        std::fprintf(stderr, "[Info] Controls: START + SELECT: the settings\n");
        ui_open_settings();
    }
    return true;
}

// ---- sticks
struct Vec {
    float x = 0, y = 0;
};
// SDL's axes (y down) as a stick with y up, past the dead zone and scaled from its edge, at most 1 long
Vec stick(SDL_Gamepad* p, SDL_GamepadAxis ax, SDL_GamepadAxis ay) {
    Vec v{SDL_GetGamepadAxis(p, ax) / 32767.0f, -SDL_GetGamepadAxis(p, ay) / 32767.0f};
    const float m = std::hypot(v.x, v.y);
    if (m <= g_set.deadzone || m <= 0) return {};
    const float k = std::min(1.0f, (m - g_set.deadzone) / std::max(.01f, 1.0f - g_set.deadzone)) / m;
    return {v.x * k, v.y * k};
}

Vec add(Vec a, Vec b) {
    Vec v{a.x + b.x, a.y + b.y};
    const float m = std::hypot(v.x, v.y);
    if (m > 1) v.x /= m, v.y /= m;
    return v;
}

std::int16_t units(float v) {
    return static_cast<std::int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * kCircle));
}

Vec keys_stick(SDL_Scancode up, SDL_Scancode down, SDL_Scancode left, SDL_Scancode right, bool walk) {
    Vec v{static_cast<float>(g_keys[right] - g_keys[left]), static_cast<float>(g_keys[up] - g_keys[down])};
    float k = walk ? kWalk : 1.0f;
    if (v.x != 0 && v.y != 0) k *= .7071f;  // (diagonals as long as the rest)
    return {v.x * k, v.y * k};
}

// ---- the test script (MH4U_TEST_INPUT)
struct Step {
    u32 start = 0, len = 0;
    int kind = 0;  // 0 buttons, 1 the Circle Pad, 2 the C-stick, 3 a touch
    u32 bits = 0;
    float x = 0, y = 0;
};
std::vector<Step> g_script;
u32 g_script_end = 0;
bool g_script_over = true, g_script_touch = false;

u32 script_button(const std::string& name) {
    static const std::pair<const char*, u32> names[] = {
        {"A", mh4n::kPadA},         {"B", mh4n::kPadB},         {"X", mh4n::kPadX},       {"Y", mh4n::kPadY},
        {"L", mh4n::kPadL},         {"R", mh4n::kPadR},         {"ZL", ZL_BIT},           {"ZR", ZR_BIT},
        {"START", mh4n::kPadStart}, {"SELECT", mh4n::kPadSelect}, {"UP", mh4n::kPadUp},   {"DOWN", mh4n::kPadDown},
        {"LEFT", mh4n::kPadLeft},   {"RIGHT", mh4n::kPadRight}};
    for (const auto& [n, b] : names)
        if (name == n) return b;
    return 0;
}

void script_load(const char* path, u32 until) {
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr, "[Info] Controls: no test script at %s\n", path);
        return;
    }
    std::string line;
    int bad = 0;
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
            Step st;
            if (f.size() < 3 || f.front()[0] != 'f' || f.back()[0] != 'f') {
                ++bad;
                continue;
            }
            st.start = static_cast<u32>(std::atol(f.front().c_str() + 1));
            st.len = static_cast<u32>(std::atol(f.back().c_str() + 1));
            const std::string& kind = f[1];
            if ((kind == "CIRCLE" || kind == "CSTICK" || kind == "TOUCH") && f.size() == 5) {
                st.kind = kind == "CIRCLE" ? 1 : kind == "CSTICK" ? 2 : 3;
                st.x = static_cast<float>(std::atof(f[2].c_str()));
                st.y = static_cast<float>(std::atof(f[3].c_str()));
            } else if (f.size() == 3 && script_button(kind)) {
                st.bits = script_button(kind);
            } else {
                ++bad;
                continue;
            }
            if (until && st.start >= until) continue;
            if (until) st.len = std::min(st.len, until - st.start);
            g_script.push_back(st);
            g_script_end = std::max(g_script_end, st.start + st.len);
        }
    }
    g_script_over = g_script.empty();
    std::fprintf(stderr, "[Info] Controls: test script %s: %zu steps to frame %u (%d not understood), beside the controls\n",
                 path, g_script.size(), g_script_end, bad);
}

void script_input(u32 frame, ControlsInput& out) {
    if (g_script_over) return;
    bool touch = false;
    int tx = 0, ty = 0;
    for (const Step& st : g_script) {
        if (frame < st.start || frame >= st.start + st.len) continue;
        switch (st.kind) {
        case 0: out.buttons |= st.bits & 0xfff, out.zl |= (st.bits & ZL_BIT) != 0, out.zr |= (st.bits & ZR_BIT) != 0; break;
        case 1: out.circle_x = units(st.x), out.circle_y = units(st.y); break;
        case 2: out.cstick_x = units(st.x), out.cstick_y = units(st.y); break;
        case 3: touch = true, tx = static_cast<int>(st.x), ty = static_cast<int>(st.y); break;
        }
    }
    if (touch || g_script_touch) {
        if (g_set_touch) g_set_touch(touch, tx, ty);
        g_script_touch = touch;
    }
    if (frame >= g_script_end) {
        g_script_over = true;
        std::fprintf(stderr, "[Info] Controls: the test script is over at frame %u\n", frame);
    }
}

}  // namespace

void controls_init(const std::string& data_dir, std::function<void(bool, int, int)> set_touch) {
    g_set_touch = std::move(set_touch);
    if (const char* v = std::getenv("MH4U_PAD_DEBUG"); v && *v == '1') g_debug = true;
    padmap_init(data_dir);
    read_settings();
    imgui_pad();
    if (const char* script = std::getenv("MH4U_TEST_INPUT"); script && *script) {
        const char* until = std::getenv("MH4U_TEST_INPUT_UNTIL");
        script_load(script, until ? static_cast<u32>(std::atol(until)) : 0);
    }
    if (const char* v = std::getenv("MH4U_TEST_JOYSTICK"); v && *v == '1') {
        SDL_VirtualJoystickDesc d;
        SDL_INIT_INTERFACE(&d);
        d.type = SDL_JOYSTICK_TYPE_UNKNOWN, d.naxes = 2, d.nbuttons = 8;
        d.name = "Test Joystick";
        d.vendor_id = 0x1234, d.product_id = 0x5678;
        const SDL_JoystickID id = SDL_AttachVirtualJoystick(&d);
        std::fprintf(stderr, "[Info] Controls: test joystick %s\n", id ? "attached" : SDL_GetError());
    }
}

bool controls_event(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
        open_pad(e.gdevice.which);
        return false;
    case SDL_EVENT_GAMEPAD_REMOVED:
        close_pad(e.gdevice.which);
        return false;
    case SDL_EVENT_GAMEPAD_REMAPPED:  // (the player mapped it, or forgot the map: padmap.cpp)
        if (Pad* p = find_pad(e.gdevice.which)) {
            p->real = name_is_pad(SDL_GetGamepadName(p->pad)) || padmap_custom(p->id);
            log_pad(p->id, "controller's map changed");
            if (any_button(p->pad)) g_remapped = p->id, imgui_pad();
        }
        return false;
    case SDL_EVENT_JOYSTICK_ADDED:  // (also sent for gamepads, before their GAMEPAD_ADDED: joystick_seen skips those)
        joystick_seen(e.jdevice.which);
        return false;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (g_debug) std::fprintf(stderr, "[Info] Controls: %u %s up\n", (unsigned)e.gbutton.which,
                                  SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(e.gbutton.button)));
        return false;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
        g_last_pad = true;
        if (g_debug) std::fprintf(stderr, "[Info] Controls: %u %s down\n", (unsigned)e.gbutton.which,
                                  SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(e.gbutton.button)));
        Pad* p = find_pad(e.gbutton.which);
        if (!p || p->id == padmap_joystick()) return false;  // (being mapped: its presses are the map's)
        if (p->id != g_active && SDL_GetTicks() - p->opened_ms >= kOpenGraceMs) {
            g_active = p->id;
            log_pad(p->id, "controller in use (a button pressed on it)");
            imgui_pad();
        }
        if (p->id != g_active) return false;
        const auto b = static_cast<SDL_GamepadButton>(e.gbutton.button);
        if (chord(p->pad, b)) return true;
        // (a stick's click as the town's list's button: in a town it opens the list, and closes it)
        if ((b == SDL_GAMEPAD_BUTTON_LEFT_STICK && g_set.town == TOWN_L3) ||
            (b == SDL_GAMEPAD_BUTTON_RIGHT_STICK && g_set.town == TOWN_R3)) {
            if (town_list_open()) return town_close(), true;
            if (town_available() && !ui_settings_open()) return town_open(true), true;
        }
        if (b == SDL_GAMEPAD_BUTTON_LEFT_STICK && !ui_settings_open()) ui_toggle_bottom();  // (the touch screen)
        pressed(button_bit(p->pad, b));
        // (the right stick's click is the lock-on's (touch.cpp, through ui_event) unless the player gave it a button)
        return b == SDL_GAMEPAD_BUTTON_RIGHT_STICK && g_set.r3 != R3_LOCKON && !ui_settings_open();
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        Pad* p = find_pad(e.gaxis.which);
        if (g_debug && std::abs(e.gaxis.value) > 4000)
            std::fprintf(stderr, "[Info] Controls: %u %s %d\n", (unsigned)e.gaxis.which,
                         SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(e.gaxis.axis)), e.gaxis.value);
        // (a stick pushed well over on another controller makes it the one in use too: one picked up and moved first)
        if (p && p->id != g_active && p->id != padmap_joystick() && e.gaxis.axis <= SDL_GAMEPAD_AXIS_RIGHTY &&
            std::abs(e.gaxis.value) > kTakeOver && SDL_GetTicks() - p->opened_ms >= kOpenGraceMs) {
            g_active = p->id;
            log_pad(p->id, "controller in use (a stick pushed on it)");
            imgui_pad();
        }
        if (!p || p->id != g_active) return false;
        if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
            bool& was = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? p->zl : p->zr;
            const bool now = e.gaxis.value > kTrigger;
            if (now && !was) pressed(e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? ZL_BIT : ZR_BIT);
            was = now;
        }
        return false;
    }
    case SDL_EVENT_FINGER_DOWN:
        g_last_pad = false;
        return false;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (e.key.down && key_bit(e.key.scancode) != 0) g_last_pad = true;
        if (e.key.scancode < SDL_SCANCODE_COUNT && !(e.key.down && typing())) {
            if (e.key.down && !e.key.repeat && !g_keys[e.key.scancode]) pressed(key_bit(e.key.scancode));
            g_keys[e.key.scancode] = e.key.down;
        }
        if (!e.key.repeat)
            std::fprintf(stderr, "[Info] App: key %s %s\n", SDL_GetScancodeName(e.key.scancode), e.key.down ? "down" : "up");
        return false;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        std::fill(std::begin(g_keys), std::end(g_keys), false);  // (their ups will not come)
        return false;
    default:
        return false;
    }
}

ControlsInput controls_input(std::uint32_t game_frame) {
    read_settings();
    padmap_poll();
    if (!g_scanned) scan();  // (controllers there at the start send no ADDED event to a loop that began after them)
    imgui_pad();
    static bool was_typing = false;
    if (typing() && !was_typing) std::fill(std::begin(g_keys), std::end(g_keys), false);  // (keys held go to the text)
    was_typing = typing();
    ControlsInput out;
    const Uint64 now = SDL_GetTicks();
    const float dt = g_last_ms ? std::min(now - g_last_ms, Uint64{50}) / 1000.0f : 0.0f;
    g_last_ms = now;
    Pad* p = find_pad(g_active);
    SDL_Gamepad* pad = p && p->id != padmap_joystick() ? p->pad : nullptr;
    u32 held = (pad ? pad_held(pad) : 0) | keys_held();
    const u32 chord_bits = mh4n::kPadStart | mh4n::kPadSelect;
    if (g_chord && !(held & chord_bits)) g_chord = false;
    // the settings: nothing while they are open, and what is held as they close waits to be let go
    const bool open = ui_settings_open() || padmap_active() || settings_panel_thanks_open();
    if (g_was_open && !open) g_suppress = held;
    g_was_open = open;
    g_suppress &= held;
    if (open) {
        script_input(game_frame, out);
        return out;
    }
    // SELECT in a town, while the hunter is walked: the town's list (town.h), once it is clear START is not coming with it
    {
        const int sel = __builtin_ctz(mh4n::kPadSelect);
        if (g_set.town == TOWN_SELECT && g_down_at[sel] && now >= g_down_at[sel] + kChordWaitMs && !g_chord &&
            town_available()) {
            g_down_at[sel] = 0;
            g_suppress |= held & mh4n::kPadSelect;
            town_open(true);
        }
    }
    // the town's list and its panel's cursor have the controller: the game gets nothing, and what is held as they let it
    // go waits to be let go
    static bool town_had = false;
    if (town_takes_pad()) {
        Vec st = keys_stick(SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_D, false);
        if (pad) st = add(st, stick(pad, SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY));
        town_pad(held, g_town_presses, st.x, st.y);
        g_town_presses = 0;
        town_had = true;
        script_input(game_frame, out);
        return out;
    }
    if (town_had) g_suppress = held, town_had = false;
    u32 bits = 0;
    for (int i = 0; i < 16; i++) {
        const u32 bit = 1u << i;
        const Uint64 wait = bit & chord_bits ? kChordWaitMs : 0;  // (START and SELECT: the other may come)
        const Uint64 down = g_down_at[i];
        const bool shown = down && now >= down + wait;
        if (((held & bit) && shown) || (shown && now < down + wait + kMinPressMs)) bits |= bit;
    }
    bits &= ~g_suppress;
    if (g_chord) bits &= ~chord_bits;
    out.buttons = bits & 0xfff;
    out.zl = (bits & ZL_BIT) != 0;
    out.zr = (bits & ZR_BIT) != 0;
    // the sticks: the controller's and the keys', added
    const bool walk = g_keys[SDL_SCANCODE_LSHIFT] || g_keys[SDL_SCANCODE_RSHIFT];
    Vec left = keys_stick(SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_D, walk);
    Vec right = keys_stick(SDL_SCANCODE_I, SDL_SCANCODE_K, SDL_SCANCODE_J, SDL_SCANCODE_L, false);
    if (pad) {
        left = add(left, stick(pad, SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY));
        right = add(right, stick(pad, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY));
    }
    out.circle_x = units(left.x), out.circle_y = units(left.y);
    // the right stick: the port's camera turn while the game's camera takes it (a smooth turn by the tilt, with or
    // without the Circle Pad Pro), else the C-stick
    const float m = std::hypot(right.x, right.y);
    const bool hook = Port::Hooks::CameraTurnActive();
    if (g_debug && m > 0) {
        static int said = -1;
        if (said != (int)hook) std::fprintf(stderr, "[Info] Controls: the right stick %s\n", hook ? "turns the camera (the port's hook)" : "is the C-stick (no camera takes the hook's turn here)");
        said = hook;
    }
    if (!g_set.cstick && hook) {
        if (m > 0 && dt > 0) {
            const float k = std::pow(m, kCurve) / m * g_set.speed * dt;
            out.cam_yaw = right.x * k * kYawSpeed * (g_set.invert_x ? -1.0f : 1.0f);
            out.cam_pitch = right.y * k * kPitchSpeed * (g_set.invert_y ? -1.0f : 1.0f);
        }
    } else {
        out.cstick_x = units(right.x), out.cstick_y = units(right.y);
    }
    script_input(game_frame, out);
    return out;
}

bool controls_last_input_pad() {
    return g_last_pad;
}

bool controls_real_pad() {
    for (const Pad& p : g_pads)
        if (p.real) return true;
    return false;
}

SDL_JoystickID controls_active_pad() {
    return g_active;
}

bool controls_a_is_south() {
    Pad* p = find_pad(g_active);
    return p && face_bit(p->pad, SDL_GAMEPAD_BUTTON_SOUTH) == mh4n::kPadA;
}
