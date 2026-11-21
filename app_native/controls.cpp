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
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <vector>

#ifdef __ANDROID__
#include <jni.h>
#else
#include "pc_input.h"  // (the PC's own keybinds: pc/pc_input.cpp)
#endif

#include "app_settings.h"
#include "hooks.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "mh4n/host.h"
#include "town.h"
#include "weapon_tree_ui.h"
#include "padmap.h"
#include "settings_panel.h"
#include "touch.h"
#include "ui.h"

namespace {

using u32 = std::uint32_t;

constexpr u32 ZL_BIT = 1u << 14, ZR_BIT = 1u << 15;  // (the triggers, beside the 3DS's kPad bits)
constexpr float kMouseDegPerCount = 0.08f;  // the camera's turn for a count of the mouse at MouseSensitivity 1 (MHGU's)
constexpr float kMouseFullSpeed = 1500.0f;   // counts a second: the C-stick all the way (where the hook can't turn it)
constexpr float kMouseSmooth = 0.025f;       // (seconds: the mouse's speed averaged over them, for the C-stick)
constexpr Uint64 kWheelPressMs = 70;         // a wheel notch on a button's keybind: pressed this long
constexpr Uint64 kMinPressMs = 80;
constexpr Uint64 kChordWaitMs = 60;
constexpr Uint64 kOpenGraceMs = 500;  // a controller just opened can't take over (one whose buttons read as held)
constexpr float kCircle = 156.0f;     // the Circle Pad's and the C-stick's reach, in HID's units
constexpr float kTrigger = 8000;      // of 32767: a trigger this far is pressed
constexpr int kTriggerHalf = 16384;   // (a trigger that is a combined button's: pressed past half its travel)
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
    bool gyro_aim = false;    // [Controls] GyroAim, GyroSensitivity (controls_gyro)
    float gyro_sens = 1.0f;
    int r3 = R3_LOCKON;
    int town = TOWN_SELECT;
    int combo_pad[kCombos] = {};  // [Controls] Combo<n>Pad: kComboPadKeys' index (COMBO_PAD's button), 0 none
    Keybinds binds;               // [Keybinds]
    float mouse_sens = 1.0f;      // [Mouse] MouseSensitivity, 0.1 .. 10 (the MHGU port's)
#ifndef __ANDROID__
    bool mouse_inv_x = false, mouse_inv_y = false;  // (the PC) [Mouse] InvertX, InvertY: the camera turned the other way
#endif
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
u32 g_out_buttons = 0;  // (controls_buttons: the game's buttons from the controller and the keys, the last turn)
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
    s.gyro_aim = app_setting_bool("Controls", "GyroAim", false);
    s.gyro_sens = std::clamp(std::strtof(app_setting("Controls", "GyroSensitivity", "1.0").c_str(), nullptr), kGyroSensMin, kGyroSensMax);
    if (!(s.gyro_sens >= kGyroSensMin)) s.gyro_sens = 1.0f;  // (not a number)
    const std::string town = app_setting("Controls", "TownMenu", "select");
    s.town = !SDL_strcasecmp(town.c_str(), "l3") ? TOWN_L3 : !SDL_strcasecmp(town.c_str(), "r3") ? TOWN_R3
             : !SDL_strcasecmp(town.c_str(), "off") ? TOWN_OFF : TOWN_SELECT;
    std::string pads;
    for (int k = 0; k < kCombos; k++) {
        s.combo_pad[k] = controls_combo_pad_value(app_setting("Controls", ("Combo" + std::to_string(k + 1) + "Pad").c_str(), "None"));
        if (s.combo_pad[k]) pads += ", combined button " + std::to_string(k + 1) + " on " + controls_combo_pad_key(s.combo_pad[k]);
    }
    s.binds = controls_read_binds();
    s.mouse_sens = std::clamp(std::strtof(app_setting("Mouse", "MouseSensitivity", "1.0").c_str(), nullptr), 0.1f, 10.0f);
    if (!(s.mouse_sens >= 0.1f)) s.mouse_sens = 1.0f;  // (not a number)
#ifndef __ANDROID__
    s.mouse_inv_x = app_setting_bool("Mouse", "InvertX", false);
    s.mouse_inv_y = app_setting_bool("Mouse", "InvertY", false);
#endif
    static bool read_once = false;  // (the first read: no change to say)
    if (read_once && !(s.binds == g_set.binds)) std::fprintf(stderr, "[Info] Controls: the keybinds changed\n");
    read_once = true;
    g_set = s;
    std::fprintf(stderr, "[Info] Controls: face buttons by %s, right stick %s, camera %.2fx%s%s, R3 %s, dead zone %.0f%%%s\n",
                 s.label ? "label" : "position", s.cstick ? "as the C-stick" : "turns the camera", s.speed,
                 s.invert_x ? ", inverted across" : "", s.invert_y ? ", inverted up and down" : "", r3.c_str(),
                 s.deadzone * 100, pads.c_str());
}

// ---- the combined buttons on a controller's own buttons ([Controls] Combo<n>Pad), as the MHGU port's (its
// runtime/host_input.c): while that button is held the game gets the combined button's buttons (touch.h
// touch_combo_buttons), and the button does nothing else - not the game's button it was, nor the touch screen, the
// lock-on, the town's list, START + SELECT or the settings. One whose combined button has no buttons is left as it was
// (1.0.2: any button. A, B, X and Y by their place - SDL's east, south, north and west -, never by FaceButtons; ZL and
// ZR are axes: COMBO_ZL / COMBO_ZR, trigger_combo_bits; Raw<i> a button of the controller's own numbering, raw_combo_bits)
enum { COMBO_ZL = 17, COMBO_ZR = 18 };
const SDL_GamepadButton COMBO_PAD[] = {
    SDL_GAMEPAD_BUTTON_INVALID,        SDL_GAMEPAD_BUTTON_LEFT_STICK,     SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    SDL_GAMEPAD_BUTTON_BACK,           SDL_GAMEPAD_BUTTON_GUIDE,          SDL_GAMEPAD_BUTTON_MISC1,
    SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1,  SDL_GAMEPAD_BUTTON_LEFT_PADDLE1,   SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2,
    SDL_GAMEPAD_BUTTON_LEFT_PADDLE2,   SDL_GAMEPAD_BUTTON_TOUCHPAD,       SDL_GAMEPAD_BUTTON_EAST,
    SDL_GAMEPAD_BUTTON_SOUTH,          SDL_GAMEPAD_BUTTON_NORTH,          SDL_GAMEPAD_BUTTON_WEST,
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,  SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_INVALID,
    SDL_GAMEPAD_BUTTON_INVALID,        SDL_GAMEPAD_BUTTON_START,          SDL_GAMEPAD_BUTTON_DPAD_UP,
    SDL_GAMEPAD_BUTTON_DPAD_DOWN,      SDL_GAMEPAD_BUTTON_DPAD_LEFT,      SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
static_assert(std::size(COMBO_PAD) == std::size(kComboPadKeys), "a button for each of Combo<n>Pad's values");
// the gamepad button a Combo<n>Pad value is (INVALID: none, a trigger or a raw button)
SDL_GamepadButton combo_pad_button(int v) { return v > 0 && v < (int)std::size(COMBO_PAD) ? COMBO_PAD[v] : SDL_GAMEPAD_BUTTON_INVALID; }
// the game's buttons button b presses as a combined button's (more than one on it: all of theirs), 0 none: b is its own
u32 combo_bits(SDL_GamepadButton b) {
    u32 bits = 0;
    for (int k = 0; k < kCombos; k++)
        if (g_set.combo_pad[k] && combo_pad_button(g_set.combo_pad[k]) == b && b != SDL_GAMEPAD_BUTTON_INVALID)
            bits |= touch_combo_buttons(k);
    return bits;
}
// ...a trigger's (left: ZL), or raw button i's
u32 trigger_combo_bits(bool left) {
    u32 bits = 0;
    for (int k = 0; k < kCombos; k++)
        if (g_set.combo_pad[k] == (left ? COMBO_ZL : COMBO_ZR)) bits |= touch_combo_buttons(k);
    return bits;
}
bool trigger_taken(bool left) {
    for (int k = 0; k < kCombos; k++)
        if (g_set.combo_pad[k] == (left ? COMBO_ZL : COMBO_ZR) && touch_combo_buttons(k)) return true;
    return false;
}
u32 raw_combo_bits(int i) {
    u32 bits = 0;
    for (int k = 0; k < kCombos; k++)
        if (g_set.combo_pad[k] == kComboPadRaw + i) bits |= touch_combo_buttons(k);
    return bits;
}
// Raw button i of controller p has a name in its map (the map makes it one of the gamepad's buttons, or a trigger)
bool raw_bound(SDL_Gamepad* p, int i) {
    int n = 0;
    SDL_GamepadBinding** b = SDL_GetGamepadBindings(p, &n);
    bool bound = false;
    for (int k = 0; b && k < n; k++)
        if (b[k] && b[k]->input_type == SDL_GAMEPAD_BINDTYPE_BUTTON && b[k]->input.button == i) bound = true;
    SDL_free(b);
    return bound;
}

// ---- Aiming with the gyro ([Controls] GyroAim), as the MHGU port's (its runtime/host_input.c gyro_update): while a
// bow or a bowgun aims (touch.cpp asks), the phone's gyroscope - the controller's in use, if it has one - turns the aim
// as a drag does, added to it: the angle turned since the aim began, times GyroSensitivity. The sensor is open only
// while aiming. The phone's axes go by the display's quarter turns from the device's natural orientation (a phone's
// portrait, an Anbernic RG406V's landscape)
constexpr float kGyroDeadband = 0.01f;  // (radians a second: the gyroscope's own drift, at rest)
SDL_Sensor* g_gyro_dev = nullptr;
SDL_Gamepad* g_gyro_pad = nullptr;
bool g_gyro_on = false;
int g_gyro_turns = 0;
Uint64 g_gyro_ns = 0;
int display_quarter(SDL_DisplayOrientation o) {  // (the device turned this many quarters counterclockwise)
    return o == SDL_ORIENTATION_LANDSCAPE ? 1 : o == SDL_ORIENTATION_PORTRAIT_FLIPPED ? 2 : o == SDL_ORIENTATION_LANDSCAPE_FLIPPED ? 3 : 0;
}
void gyro_off() {
    if (g_gyro_dev) SDL_CloseSensor(g_gyro_dev), g_gyro_dev = nullptr;
    if (g_gyro_pad) SDL_SetGamepadSensorEnabled(g_gyro_pad, SDL_SENSOR_GYRO, false), g_gyro_pad = nullptr;
    if (g_gyro_on) std::fprintf(stderr, "[Info] Controls: the gyro off\n");
    g_gyro_on = false, g_gyro_ns = 0;
}

// ---- the input devices as Android lists them, in the log (GameActivity.inputReport, the MHGU port's: their sources, a
// gamepad's keys, read as a controller or not, and the gamepad keys that came from an external device that isn't, which
// SDL gave the game as a keyboard's). A controller that "isn't read" left no line at all when SDL never listed it as a
// joystick (an Ipega PG-9025 Android calls "... Keyboard", gu-native #87). At the start, a second after a device comes
// or goes (report_soon), then every 5 s only if something changed
Uint64 g_report_next = 0;
bool g_report_all = true;  // (the whole report: the start, a device came or went)
void report_soon() { g_report_all = true, g_report_next = SDL_GetTicks() + 1000; }
#ifndef __ANDROID__
// (the PC: the joysticks SDL sees, each a line, at the start and when one comes or goes)
void input_report() {
    const Uint64 now = SDL_GetTicks();
    if (!g_report_all || now < g_report_next) return;
    g_report_all = false;
    int n = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&n);
    std::fprintf(stderr, "[input] %d joystick%s\n", n, n == 1 ? "" : "s");
    for (int i = 0; ids != nullptr && i < n; i++) {
        char guid[33];
        SDL_GUIDToString(SDL_GetJoystickGUIDForID(ids[i]), guid, sizeof guid);
        const char* name = SDL_GetJoystickNameForID(ids[i]);
        std::fprintf(stderr, "[input]   %s: %s, guid %s\n", name ? name : "?",
                     SDL_IsGamepad(ids[i]) ? "a controller SDL maps" : "no map of SDL's", guid);
    }
    SDL_free(ids);
}
#else
void input_report() {
    static bool failed = false;
    const Uint64 now = SDL_GetTicks();
    if (failed || now < g_report_next) return;
    g_report_next = now + 5000;
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto act = env ? static_cast<jobject>(SDL_GetAndroidActivity()) : nullptr;
    if (!act) return;
    jclass cls = env->GetObjectClass(act);
    jmethodID m = cls ? env->GetStaticMethodID(cls, "inputReport", "(Z)Ljava/lang/String;") : nullptr;
    jstring str = nullptr;
    if (!env->ExceptionCheck() && m) str = static_cast<jstring>(env->CallStaticObjectMethod(cls, m, static_cast<jboolean>(g_report_all)));
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe(), env->ExceptionClear();
        if (!m) failed = true, std::fprintf(stderr, "[input] no input report: GameActivity.inputReport is missing\n");
        str = nullptr;
    }
    g_report_all = false;
    const char* t = str ? env->GetStringUTFChars(str, nullptr) : nullptr;
    for (const char* p = t; p && *p;) {  // (line by line, as the MHGU port's log)
        const char* nl = std::strchr(p, '\n');
        const int n = nl ? static_cast<int>(nl - p) : static_cast<int>(std::strlen(p));
        std::fprintf(stderr, "%.*s\n", n, p);
        p += n + (nl != nullptr);
    }
    if (t) env->ReleaseStringUTFChars(str, t);
    if (str) env->DeleteLocalRef(str);
    if (cls) env->DeleteLocalRef(cls);
    env->DeleteLocalRef(act);
}
#endif

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
bool g_pad_capture = false;  // (the settings' Press a controller button: controls_pad_capture)
void imgui_pad() {
    Pad* p = find_pad(g_active);
    if (g_remapped && (!p || p->id != g_remapped || !any_button(p->pad))) g_remapped = 0;
    SDL_Gamepad* want = p && p->id != padmap_joystick() && p->id != g_remapped && !g_pad_capture ? p->pad : nullptr;
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
    if (g == g_gyro_pad) g_gyro_pad = nullptr;  // (its gyro was read: gone with it)
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
                         "(Controls, Controller, Map a controller)\n",
                 name ? name : "?", SDL_GetJoystickVendorForID(id), SDL_GetJoystickProductForID(id));
    if (!name_is_pad(name)) return;  // (not a controller in the player's hands)
#ifndef __ANDROID__
    // (the PC: a device with no name of its own, "0x1532/0x022b" as SDL calls it, is a keyboard's or a mouse's extra
    // interface far more often than a controller: logged above, no notice)
    if (name != nullptr && name[0] == '0' && name[1] == 'x' && std::strchr(name, '/') != nullptr) return;
#endif
    const std::string who = name ? name : "";
    settings_panel_notice((who.empty() ? std::string("A controller") : who) +
                              ": to use it, map it in the settings: Controls, Controller, Map a controller.",
                          (who.empty() ? std::string("Un mando") : who) +
                              ": para usarlo, mapéalo en las opciones: Controles, Mando, Mapear un mando.");
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
        if (SDL_GetGamepadButton(p, sb) && !combo_bits(sb)) b |= button_bit(p, sb);  // (a combined button's: below)
    SDL_Joystick* joy = SDL_GetGamepadJoystick(p);
    for (int k = 0; k < kCombos; k++) {
        const int v = g_set.combo_pad[k];
        const SDL_GamepadButton cb = combo_pad_button(v);
        if (cb != SDL_GAMEPAD_BUTTON_INVALID && SDL_GetGamepadButton(p, cb)) b |= touch_combo_buttons(k);
        if (v == COMBO_ZL || v == COMBO_ZR)
            if (SDL_GetGamepadAxis(p, v == COMBO_ZL ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > kTriggerHalf)
                b |= touch_combo_buttons(k);
        if (v >= kComboPadRaw && v < kComboPadRaw + kComboPadRaws && joy && SDL_GetJoystickButton(joy, v - kComboPadRaw))
            b |= touch_combo_buttons(k);
    }
    if (!trigger_taken(true) && SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > kTrigger) b |= ZL_BIT;
    if (!trigger_taken(false) && SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > kTrigger) b |= ZR_BIT;
    return b;
}

// ---- the keyboard and the mouse ([Keybinds], controls.h): the game's buttons each keybind presses (0: a stick's direction
// or Walk, the item bar, a stick's click, a combined button's: their own ways below)
const u32 BIND_BITS[BINDS] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, mh4n::kPadA, mh4n::kPadB, mh4n::kPadX, mh4n::kPadY, mh4n::kPadL, mh4n::kPadR, ZL_BIT, ZR_BIT,
    mh4n::kPadStart, mh4n::kPadSelect, mh4n::kPadUp, mh4n::kPadDown, mh4n::kPadLeft, mh4n::kPadRight};
u32 g_mouse_held = 0;            // the mouse's buttons held (bit: SDL_BUTTON_*), while it is captured
Uint64 g_wheel_until[2] = {};    // a notch up, down on a button's keybind: pressed until then (SDL_GetTicks)
#ifndef __ANDROID__
// (the PC, as the MHGU port's desktop) A mouse button (or a wheel notch) on A presses it only with the weapon out, in a
// menu or a talk of the game's, or out of the towns and the quests (the title, the save's choice): by default A is F,
// O and the right click, and the right click attacks then - it never talks, opens the item box, gathers or carves by
// mistake (F does). g_mouse_a: allowed now (mouse_a_ok, each turn)
bool g_mouse_a = true;
bool is_mouse_code(int code) { return code > kKeyMouse && code <= kKeyWheelDown; }
bool mouse_a_ok() {
    const Port::Hooks::GameState g = Port::Hooks::CurrentGameState();
    if (g.weapon_drawn == 1 || g.menu == 1 || g.front_end == 1) return true;
    return g.place != Port::Hooks::GameState::PLACE_TOWN && g.place != Port::Hooks::GameState::PLACE_QUEST;
}
// A modifier key (Ctrl, Alt, the Windows key; not Shift) on a keybind that presses something presses it only when
// tapped alone: let go with no other key or mouse button pressed meanwhile, and then as long as a wheel notch. With
// another key it is a shortcut's (Alt+Tab, Alt+Enter, Ctrl+V) and presses nothing: Alt is the lock-on and Ctrl the touch
// screen by default. On a stick's direction or Walk every key is held as it is (the MHGU port's)
constexpr int kMods = SDL_SCANCODE_RGUI - SDL_SCANCODE_LCTRL + 1;
bool g_mod_held[kMods] = {}, g_mod_spent[kMods] = {};
Uint64 g_mod_until[kMods] = {};
bool is_tap_mod(int code) {
    return code >= SDL_SCANCODE_LCTRL && code <= SDL_SCANCODE_RGUI && code != SDL_SCANCODE_LSHIFT && code != SDL_SCANCODE_RSHIFT;
}
bool bind_taps(int b) { return b >= BIND_A; }  // (a button's, a click's, the item bar's...: not moving, Walk, the camera)
void mods_spend() {  // (another key or button went down: the modifiers held are a shortcut's)
    for (int i = 0; i < kMods; i++)
        if (g_mod_held[i]) g_mod_spent[i] = true;
}
// In a menu, a window or a talk of the game's (the hooks' menu), the movement keys (W A S D) are the D-pad and push no
// Circle Pad: the game's cursor moves with the D-pad in its menus (the pause menu, the pouch, the item box, a shop, the
// quest board), not with the Circle Pad, as MHGU for PC's WASD (g_keys_menu: each controls_input)
bool g_keys_menu = false;
const int kMoveBinds[4] = {BIND_MOVE_UP, BIND_MOVE_DOWN, BIND_MOVE_LEFT, BIND_MOVE_RIGHT};
const u32 kMoveDpad[4] = {mh4n::kPadUp, mh4n::kPadDown, mh4n::kPadLeft, mh4n::kPadRight};
#endif
bool code_held(int code) {
    if (code > 0 && code < SDL_SCANCODE_COUNT) return g_keys[code];
    if (code > kKeyMouse && code <= kKeyMouse + SDL_BUTTON_X2) return (g_mouse_held >> (code - kKeyMouse) & 1) != 0;
    if (code == kKeyWheelUp || code == kKeyWheelDown) return SDL_GetTicks() < g_wheel_until[code - kKeyWheelUp];
    return false;
}
bool bind_held(int b) {
#ifdef __ANDROID__
    for (int k = 0; k < kBindKeys; k++)
        if (g_set.binds.keys[b][k] && code_held(g_set.binds.keys[b][k])) return true;
    return false;
#else
    for (int k = 0; k < kBindKeys; k++) {
        const int code = g_set.binds.keys[b][k];
        if (!code) continue;
        if (b == BIND_A && !g_mouse_a && is_mouse_code(code)) continue;
        if (bind_taps(b) && is_tap_mod(code)) {
            if (SDL_GetTicks() < g_mod_until[code - SDL_SCANCODE_LCTRL]) return true;
            continue;
        }
        if (code_held(code)) return true;
    }
    return false;
#endif
}
bool bind_has(int b, int code) {
    for (int k = 0; k < kBindKeys; k++)
        if (g_set.binds.keys[b][k] == code) return true;
    return false;
}
// the right stick's click's buttons, as a controller's ([Controls] R3: the lock-on is no button, code_down)
u32 rstick_bits() { return g_set.r3 == R3_L ? mh4n::kPadL : g_set.r3 == R3_ZL ? ZL_BIT : g_set.r3 == R3_ZR ? ZR_BIT : 0; }
// what a key or a mouse button presses (several keybinds may share it)
u32 code_bits(int code) {
    u32 b = 0;
    for (int i = 0; i < BINDS; i++) {
        if (!bind_has(i, code)) continue;
#ifndef __ANDROID__
        if (i == BIND_A && !g_mouse_a && is_mouse_code(code)) continue;  // (the right click with the weapon sheathed)
        for (int k = 0; k < 4; k++)  // (a menu's: the movement keys are the D-pad)
            if (g_keys_menu && i == kMoveBinds[k]) b |= kMoveDpad[k];
#endif
        if (BIND_BITS[i]) b |= BIND_BITS[i];
        else if (i == BIND_RSTICK) b |= rstick_bits();
        else if (i >= BIND_COMBO1 && i <= BIND_COMBO15) b |= touch_combo_buttons(i - BIND_COMBO1);
    }
    return b;
}

// The keyboard types into a text (the settings' keyboard, the game's text on the phone's keyboard): its keys are not the
// game's buttons meanwhile
bool typing() {
    SDL_Window* w = SDL_GetKeyboardFocus();
    return w && SDL_TextInputActive(w);
}

u32 keys_held() {
    u32 b = 0;
    for (int i = 0; i < BINDS; i++) {
        if (!bind_held(i)) continue;
        if (BIND_BITS[i]) b |= BIND_BITS[i];
        else if (i == BIND_RSTICK) b |= rstick_bits();
        else if (i >= BIND_COMBO1 && i <= BIND_COMBO15) b |= touch_combo_buttons(i - BIND_COMBO1);
    }
#ifndef __ANDROID__
    for (int k = 0; k < 4; k++)  // (a menu's: the movement keys are the D-pad)
        if (g_keys_menu && bind_held(kMoveBinds[k])) b |= kMoveDpad[k];
#endif
    return b;
}

// (not while the settings are open: a press there is the settings', and its release must not reach the game after them)
void pressed(u32 bits) {
    const bool module = town_takes_pad() || weapon_tree_takes_pad();  // (town.h; 1.0.2: the Smithy's weapon tree)
    if (module && !ui_menu_up() && !padmap_active()) g_town_presses |= bits;  // (however short)
    if (ui_menu_up() || padmap_active() || module || settings_panel_thanks_open()) return;  // (1.0.2: the chat afloat: the game's)
#ifndef __ANDROID__
    if (touch_editing() || pc_help_open()) return;  // (the HUD editor, the list of the controls: theirs)
#endif
    const Uint64 now = SDL_GetTicks();
    for (u32 b = bits; b; b &= b - 1) g_down_at[__builtin_ctz(b)] = now;
}

// START or SELECT went down: with the other one held on the same controller, the settings open (or close)
bool chord(SDL_Gamepad* p, SDL_GamepadButton b) {
    if (b != SDL_GAMEPAD_BUTTON_START && b != SDL_GAMEPAD_BUTTON_BACK) return false;
    const SDL_GamepadButton other = b == SDL_GAMEPAD_BUTTON_START ? SDL_GAMEPAD_BUTTON_BACK : SDL_GAMEPAD_BUTTON_START;
    if (!SDL_GetGamepadButton(p, other)) return false;
    if (!ui_settings_open() && combo_bits(other)) return false;  // (SELECT a combined button's: not SELECT now)
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

Vec keys_stick(int up, int down, int left, int right, bool walk) {  // (BIND_*: a stick's four directions)
    Vec v{static_cast<float>(bind_held(right) - bind_held(left)), static_cast<float>(bind_held(up) - bind_held(down))};
    float k = walk ? kWalk : 1.0f;
    if (v.x != 0 && v.y != 0) k *= .7071f;  // (diagonals as long as the rest)
    return {v.x * k, v.y * k};
}

// ---- the test script (MH4U_TEST_INPUT)
struct Step {
    u32 start = 0, len = 0;
    int kind = 0;  // 0 buttons, 1 the Circle Pad, 2 the C-stick, 3 a touch, 4 a button of the test controller's
    u32 bits = 0;  // (4: its SDL_GamepadButton)
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
// (the log's names of the game's buttons: MH4U_PAD_DEBUG)
std::string script_names(u32 bits) {
    static const char* const names[16] = {"A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN",
                                          "R", "L", "X", "Y", "", "", "ZL", "ZR"};
    std::string t;
    for (int i = 0; i < 16; i++)
        if (bits >> i & 1) t += (t.empty() ? "" : "+") + std::string(names[i]);
    return t.empty() ? "none" : t;
}
SDL_Joystick* g_test_pad = nullptr;  // MH4U_TEST_JOYSTICK=pad: the virtual controller the script presses (PAD)

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
            } else if (kind == "PAD" && f.size() == 4 && g_test_pad &&
                       SDL_GetGamepadButtonFromString(f[2].c_str()) != SDL_GAMEPAD_BUTTON_INVALID) {
                st.kind = 4, st.bits = static_cast<u32>(SDL_GetGamepadButtonFromString(f[2].c_str()));
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
    bool pad[SDL_GAMEPAD_BUTTON_COUNT] = {};
    for (const Step& st : g_script) {
        if (frame < st.start || frame >= st.start + st.len) continue;
        if (st.kind == 4) pad[st.bits] = true;
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
    static bool pad_was[SDL_GAMEPAD_BUTTON_COUNT] = {};
    for (int b = 0; g_test_pad && b < SDL_GAMEPAD_BUTTON_COUNT; b++)
        if (pad[b] != pad_was[b]) {
            SDL_SetJoystickVirtualButton(g_test_pad, b, pad[b]);  // (its events come with the next ones)
            pad_was[b] = pad[b];
        }
    if (frame >= g_script_end) {
        g_script_over = true;
        std::fprintf(stderr, "[Info] Controls: the test script is over at frame %u\n", frame);
    }
}


// ---- a keyboard with letters and a mouse connected (GameActivity.keyboardMouse, the MHGU port's): asked at the start,
// soon after a keyboard or a mouse comes or goes (kbm_soon) and every 2 s
int g_kbm = 0;  // (1 a keyboard, 2 a mouse)
Uint64 g_kbm_next = 0;
void kbm_soon() { g_kbm_next = SDL_GetTicks() + 500; }
#ifndef __ANDROID__
// (the PC: a keyboard and a mouse are always there)
void kbm_poll() {
    if (g_kbm == 3) return;
    g_kbm = 3;
    std::fprintf(stderr, "[input] keyboard and mouse: the settings' Controls show Keybinds and the mouse's speed\n");
}
#else
void kbm_poll() {
    static bool failed = false;
    static int said = -1;
    const Uint64 now = SDL_GetTicks();
    if (failed || now < g_kbm_next) return;
    g_kbm_next = now + 2000;
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto act = env ? static_cast<jobject>(SDL_GetAndroidActivity()) : nullptr;
    if (!act) return;
    jclass cls = env->GetObjectClass(act);
    jmethodID m = cls ? env->GetStaticMethodID(cls, "keyboardMouse", "()I") : nullptr;
    int bits = 0;
    if (!env->ExceptionCheck() && m) bits = env->CallStaticIntMethod(cls, m);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe(), env->ExceptionClear();
        if (!m) failed = true, std::fprintf(stderr, "[input] GameActivity.keyboardMouse is missing: no Keybinds on the phone\n");
        bits = 0;
    }
    if (cls) env->DeleteLocalRef(cls);
    env->DeleteLocalRef(act);
    g_kbm = bits;
    if (bits != said)
        std::fprintf(stderr, "[input] keyboard %s, mouse %s%s\n", bits & 1 ? "connected" : "none", bits & 2 ? "connected" : "none",
                     bits == 3 ? ": the settings' Controls show Keybinds and the mouse's speed" : said == 3 ? ": Keybinds hidden again" : "");
    said = bits;
}
#endif

// ---- a key or a mouse button went down (not while the settings are open: pressed() says so): the buttons of the
// keybinds it is on, and what the stick's clicks and the item bar's keybinds do at once
bool code_used(int code) {
    for (int i = 0; i < BINDS; i++)
        if (bind_has(i, code)) return true;
    return false;
}
void code_down(int code) {
    pressed(code_bits(code));
#ifndef __ANDROID__
    if (touch_editing() || pc_help_open()) {  // (the list of the controls: its own key hides it)
        if (pc_help_open() && bind_has(BIND_HELP, code)) pc_bind_pressed(BIND_HELP);
        return;
    }
#endif
    if (ui_menu_up() || padmap_active() || settings_panel_thanks_open() || typing()) return;
    if (bind_has(BIND_RSTICK, code) && (weapon_tree_is_open() || weapon_tree_available())) {  // (1.0.2: the Smithy's tree)
        if (weapon_tree_is_open()) weapon_tree_close();
        else weapon_tree_open(true);
    } else if (bind_has(BIND_RSTICK, code) && g_set.r3 == R3_LOCKON) {
        touch_lock_key();  // (a controller's right stick click's)
    }
    if (bind_has(BIND_LSTICK, code)) {  // (the left stick's click: the town's list as the controller's, else the touch screen)
        if (g_set.town == TOWN_L3 && town_list_open()) town_close();
        else if (g_set.town == TOWN_L3 && town_available()) town_open(true);
        else ui_toggle_bottom();
    }
    if (bind_has(BIND_ITEM_LEFT, code)) touch_item_key(-1);
    if (bind_has(BIND_ITEM_RIGHT, code)) touch_item_key(1);
#ifndef __ANDROID__
    for (int i = BIND_HELP; i < BINDS; i++)  // (the PC's own: the controls' list, the touch screen's shortcuts)
        if (bind_has(i, code)) pc_bind_pressed(i);
#endif
}

// ---- the mouse: captured (relative mode) by a left click on the game's picture while a keyboard and a mouse are
// connected, let go by Escape, the settings, a text being typed or the app going away. A click on the touch screen or on a
// touch control is a tap there, as in 0.1.9.3 (SDL makes a finger of each left click: touch.cpp says where it landed), and
// captures nothing. Its movement turns the camera (the port's hook where the game's camera takes it, else the C-stick by
// its speed), its buttons and wheel press what [Keybinds] says, and while it is captured its clicks make no taps (the
// hidden pointer's place is nobody's). A finger's own mouse events (SDL makes one of each touch) are never the mouse's
bool g_mouse_grab = false;      // the player's choice: clicked in (true), or Escape, the app away (false)
bool g_mouse_captured = false;  // relative mode on now
bool g_mouse_refused = false;   // SDL refused relative mode: no capture this session
float g_mouse_dx = 0, g_mouse_dy = 0, g_mouse_vx = 0, g_mouse_vy = 0;
Uint64 g_mouse_moved_ms = 0;
void mouse_let_go(const char* why) {
    if (g_mouse_grab) std::fprintf(stderr, "[input] the mouse let go (%s)\n", why);
    g_mouse_grab = false;
}
void wheel_turn(int n) {  // (n notches up, > 0)
    const int code = n > 0 ? kKeyWheelUp : kKeyWheelDown;
    if (bind_has(BIND_ITEM_LEFT, code) || bind_has(BIND_ITEM_RIGHT, code)) {
        for (int k = 0; k < std::min(std::abs(n), 4); k++) touch_item_key(bind_has(BIND_ITEM_LEFT, code) ? -1 : 1);
        return;
    }
    if (!code_bits(code)) return;
    g_wheel_until[code - kKeyWheelUp] = SDL_GetTicks() + kWheelPressMs;
    pressed(code_bits(code));
}
bool mouse_event(const SDL_Event& e) {
    const SDL_MouseID which = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.which
                              : e.type == SDL_EVENT_MOUSE_WHEEL ? e.wheel.which : e.button.which;
    if (which == SDL_TOUCH_MOUSEID) return false;  // (a finger: the touch controls' and the settings')
    switch (e.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        const int b = e.button.button;
        if (b < 1 || b > SDL_BUTTON_X2) return false;
        const bool picture = b == SDL_BUTTON_LEFT && touch_mouse_click_on_picture(e.button.timestamp);
        if (g_mouse_captured) {
            g_last_pad = true;
#ifndef __ANDROID__
            mods_spend();  // (Ctrl or Alt held with a click: a shortcut's, no tap)
#endif
            g_mouse_held |= 1u << b;
            code_down(kKeyMouse + b);
            return true;
        }
#ifndef __ANDROID__
        if (weapon_tree_is_open()) return false;  // (1.0.2: a click on the Smithy's weapon tree is its finger, no capture)
#endif
        if (picture && !ui_settings_open() && !typing() && controls_keyboard_mouse() && !g_mouse_refused && !g_mouse_grab) {
            std::fprintf(stderr, "[input] capturing the mouse (a click on the game's picture; Escape lets it go)\n");
            g_mouse_grab = true;  // (the click that captures it presses nothing)
            return true;
        }
        return false;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button >= 1 && e.button.button <= SDL_BUTTON_X2) g_mouse_held &= ~(1u << e.button.button);
        return g_mouse_captured;
    case SDL_EVENT_MOUSE_MOTION:
        if (!g_mouse_captured) return false;
        g_mouse_dx += e.motion.xrel, g_mouse_dy += e.motion.yrel;
        return true;
    case SDL_EVENT_MOUSE_WHEEL: {
        if (!g_mouse_captured) return false;
        const int n = static_cast<int>(e.wheel.integer_y) * (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1);
#ifndef __ANDROID__
        if (n) mods_spend();
#endif
        if (n) wheel_turn(n);
        return true;
    }
    default:
        return false;
    }
}
// each turn of the loop: relative mode on while the mouse is the game's
void mouse_capture(bool settings_open) {
    const bool want = g_mouse_grab && !settings_open && !typing() && controls_keyboard_mouse() && !g_mouse_refused;
    if (g_mouse_grab && !controls_keyboard_mouse()) mouse_let_go("no keyboard and mouse connected");
    if (want == g_mouse_captured) return;
    SDL_Window* w = SDL_GetKeyboardFocus();
    if (!w) w = SDL_GetMouseFocus();
#ifndef __ANDROID__
    // (tests, MH4U_TEST_NO_GRAB=1: the mouse is the game's as if captured, but the real pointer is left alone - the
    // test's mouse is its script's events, and the PC's own mouse is not taken from the person at it)
    static const bool no_grab = std::getenv("MH4U_TEST_NO_GRAB") != nullptr && *std::getenv("MH4U_TEST_NO_GRAB") == '1';
    if (no_grab) {
        g_mouse_captured = want;
        std::fprintf(stderr, "[input] mouse %s (a test's: the pointer left alone)\n", want ? "captured: it turns the camera" : "free");
        g_mouse_dx = g_mouse_dy = g_mouse_vx = g_mouse_vy = 0;
        if (!want) g_mouse_held = 0;
        return;
    }
#endif
    if (!w) return;
    if (SDL_SetWindowRelativeMouseMode(w, want) || !want) {
        g_mouse_captured = want;
        std::fprintf(stderr, "[input] mouse %s\n", want ? "captured: it turns the camera" : "free");
    } else {
        std::fprintf(stderr, "[input] mouse capture refused: %s (the mouse turns no camera this session)\n", SDL_GetError());
        g_mouse_refused = true, g_mouse_grab = false;
    }
    g_mouse_dx = g_mouse_dy = g_mouse_vx = g_mouse_vy = 0;
    if (!want) g_mouse_held = 0;
}
// the mouse's movement since the last turn: the camera
void mouse_camera(bool hook, float dt, ControlsInput& out) {
#ifndef __ANDROID__
    const float dx = g_mouse_dx * (g_set.mouse_inv_x ? -1.0f : 1.0f), dy = g_mouse_dy * (g_set.mouse_inv_y ? -1.0f : 1.0f);
#else
    const float dx = g_mouse_dx, dy = g_mouse_dy;
#endif
    g_mouse_dx = g_mouse_dy = 0;
    const Uint64 now = SDL_GetTicks();
    if (dx != 0 || dy != 0) g_mouse_moved_ms = now;
    if (hook) {  // (the game's camera takes the port's turn: a fixed angle a count, as the MHGU port's hunt camera)
        const float k = kMouseDegPerCount * g_set.mouse_sens;
        out.cam_yaw += dx * k, out.cam_pitch -= dy * k;  // (the mouse up raises the camera, as a drag up does)
        g_mouse_vx = g_mouse_vy = 0;
        return;
    }
    // elsewhere the C-stick, by the mouse's speed (smoothed), unless the stick or the keys push it already
    if (dt > 0) {
        const float a = 1.0f - std::exp(-dt / kMouseSmooth);
        g_mouse_vx += (dx / dt - g_mouse_vx) * a, g_mouse_vy += (dy / dt - g_mouse_vy) * a;
    }
    if (now - g_mouse_moved_ms > 100) g_mouse_vx = g_mouse_vy = 0;
    const float x = g_mouse_vx * g_set.mouse_sens / kMouseFullSpeed, y = -g_mouse_vy * g_set.mouse_sens / kMouseFullSpeed;
    const float m = std::hypot(x, y);
    if (m < .02f || out.cstick_x != 0 || out.cstick_y != 0) return;
    const float push = (.3f + .7f * std::min(1.0f, m)) / m;  // (past the game's own dead zone, as a drag's)
    out.cstick_x = units(x * push), out.cstick_y = units(y * push);
}

// ---- the settings choosing a key (controls_capture): the next key, mouse button or wheel notch
bool g_capture = false, g_capture_mouse = false;
int g_captured = -1;
bool capture_event(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
        if (e.key.repeat) return true;
        g_captured = e.key.scancode == SDL_SCANCODE_ESCAPE || e.key.scancode == SDL_SCANCODE_AC_BACK ? 0 : e.key.scancode;
        return true;
    case SDL_EVENT_KEY_UP:
        return false;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.which == SDL_TOUCH_MOUSEID || !g_capture_mouse || e.button.button < 1 || e.button.button > SDL_BUTTON_X2)
            return false;  // (a finger, or the pointer on one of the view's rows: theirs)
        g_captured = kKeyMouse + e.button.button;
        return true;
    case SDL_EVENT_MOUSE_WHEEL:
        if (e.wheel.which == SDL_TOUCH_MOUSEID || e.wheel.integer_y == 0) return false;
        g_captured = (e.wheel.integer_y > 0) == (e.wheel.direction != SDL_MOUSEWHEEL_FLIPPED) ? kKeyWheelUp : kKeyWheelDown;
        return true;
    default:
        return false;
    }
}

// ---- [Keybinds]' names: SDL's for a key, but its own for the ones whose name has a ',' (the list's separator) or a ';'
// / '#' (a comment in the file), and for the mouse's buttons and wheel (the MHGU port's)
const struct {
    int code;
    const char* name;
} KEY_NAMES[] = {
    {SDL_SCANCODE_COMMA, "Comma"}, {SDL_SCANCODE_KP_COMMA, "Keypad Comma"}, {SDL_SCANCODE_SEMICOLON, "Semicolon"},
    {SDL_SCANCODE_KP_HASH, "Keypad Hash"}, {kKeyMouse + SDL_BUTTON_LEFT, "Mouse Left"},
    {kKeyMouse + SDL_BUTTON_RIGHT, "Mouse Right"}, {kKeyMouse + SDL_BUTTON_MIDDLE, "Mouse Middle"},
    {kKeyMouse + SDL_BUTTON_X1, "Mouse 4"}, {kKeyMouse + SDL_BUTTON_X2, "Mouse 5"}, {kKeyWheelUp, "Wheel Up"},
    {kKeyWheelDown, "Wheel Down"},
};
int key_code(std::string name) {
    while (!name.empty() && name.back() == ' ') name.pop_back();
    while (!name.empty() && name.front() == ' ') name.erase(0, 1);
    if (name.empty()) return 0;
    for (const auto& k : KEY_NAMES)
        if (!SDL_strcasecmp(k.name, name.c_str())) return k.code;
    const SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str());
    return sc != SDL_SCANCODE_UNKNOWN && !controls_key_name(sc).empty() ? static_cast<int>(sc) : 0;
}
void parse_keys(const std::string& text, int out[kBindKeys]) {
    for (int k = 0; k < kBindKeys; k++) out[k] = 0;
    if (!SDL_strcasecmp(text.c_str(), "None")) return;
    std::stringstream in(text);
    std::string one;
    int n = 0;
    while (n < kBindKeys && std::getline(in, one, ','))
        if (const int code = key_code(one)) out[n++] = code;
}

}  // namespace

void controls_init(const std::string& data_dir, std::function<void(bool, int, int)> set_touch) {
    g_set_touch = std::move(set_touch);
    if (const char* v = std::getenv("MH4U_PAD_DEBUG"); v && *v == '1') g_debug = true;
    padmap_init(data_dir);
    read_settings();
    imgui_pad();
    if (const char* v = std::getenv("MH4U_TEST_JOYSTICK"); v && !SDL_strcmp(v, "pad")) {
        // (a gamepad's every button and its sticks, no triggers: SDL maps a virtual controller by its own order)
        SDL_VirtualJoystickDesc d;
        SDL_INIT_INTERFACE(&d);
        d.type = SDL_JOYSTICK_TYPE_GAMEPAD, d.naxes = 4, d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        d.name = "Test Gamepad";
        d.vendor_id = 0x1234, d.product_id = 0x5679;
        const SDL_JoystickID id = SDL_AttachVirtualJoystick(&d);
        if (id) g_test_pad = SDL_OpenJoystick(id);
        std::fprintf(stderr, "[Info] Controls: test gamepad %s\n", g_test_pad ? "attached" : SDL_GetError());
    }
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

#ifndef __ANDROID__
int g_last_device = 1;  // (controls_last_device: 0 the mouse or a finger, 1 the keyboard, 2 a controller)
#endif

bool controls_event(const SDL_Event& e) {
#ifndef __ANDROID__
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && std::abs(e.gaxis.value) > kTakeOver))
        g_last_device = 2;
    else if (e.type == SDL_EVENT_KEY_DOWN)
        g_last_device = 1;
    else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_WHEEL || e.type == SDL_EVENT_FINGER_DOWN)
        g_last_device = 0;
#endif
    if (g_capture && capture_event(e)) return true;  // (the settings choosing a key: the next one is theirs)
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
        open_pad(e.gdevice.which);
        report_soon();
        return false;
    case SDL_EVENT_GAMEPAD_REMOVED:
        close_pad(e.gdevice.which);
        report_soon();
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
        report_soon();
        return false;
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN: {  // (a controller's raw button: one its map has no name for, 1.0.2)
        Pad* p = find_pad(e.jbutton.which);
        if (!p || p->id == padmap_joystick() || e.jbutton.button >= kComboPadRaws || raw_bound(p->pad, e.jbutton.button))
            return false;
        static std::vector<std::pair<SDL_JoystickID, int>> told;
        if (std::find(told.begin(), told.end(), std::make_pair(p->id, (int)e.jbutton.button)) == told.end()) {
            told.emplace_back(p->id, (int)e.jbutton.button);
            std::fprintf(stderr, "[Info] Controls: %s button %d pressed: no name in the controller's map (a combined button "
                                 "can take it with Press a controller button)\n",
                         SDL_GetGamepadName(p->pad) ? SDL_GetGamepadName(p->pad) : "?", (int)e.jbutton.button);
        }
        if (p->id == g_active && !ui_settings_open())
            if (const u32 combo = raw_combo_bits(e.jbutton.button)) pressed(combo);
        return false;
    }
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
        // (a combined button's: its buttons, and nothing else; in the settings it works them as before)
        if (const u32 combo = ui_settings_open() ? 0 : combo_bits(b)) {
            if (g_debug) std::fprintf(stderr, "[Info] Controls: %s is a combined button's\n", SDL_GetGamepadStringForButton(b));
            pressed(combo);
            return true;
        }
        if (chord(p->pad, b)) return true;
        // (1.0.2) in the Smithy, with a weapon shown, the right stick's click opens the weapon tree and closes it (no
        // lock-on in a town; weapon_tree_ui.h)
        if (b == SDL_GAMEPAD_BUTTON_RIGHT_STICK && !ui_settings_open() && (weapon_tree_is_open() || weapon_tree_available())) {
            if (weapon_tree_is_open()) weapon_tree_close();
            else weapon_tree_open(true);
            return true;
        }
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
            const bool left = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
            bool& was = left ? p->zl : p->zr;
            const bool taken = !ui_settings_open() && trigger_taken(left);  // (a combined button's: past half, its buttons)
            const bool now = e.gaxis.value > (taken ? kTriggerHalf : kTrigger);
            if (now && !was) pressed(taken ? trigger_combo_bits(left) : left ? ZL_BIT : ZR_BIT);
            was = now;
        }
        return false;
    }
    case SDL_EVENT_FINGER_DOWN:
        // (the finger SDL makes of a captured mouse's left click: at the hidden pointer's place, no tap; its lift and
        // moves find no finger down)
        if (e.tfinger.touchID == SDL_MOUSE_TOUCHID && g_mouse_captured) return true;
        g_last_pad = false;
        return false;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (!e.key.repeat)
            std::fprintf(stderr, "[Info] App: key %s %s\n", SDL_GetScancodeName(e.key.scancode), e.key.down ? "down" : "up");
        // (Escape with the mouse captured lets it go, and is no B then)
        if (e.key.down && !e.key.repeat && e.key.scancode == SDL_SCANCODE_ESCAPE && g_mouse_captured) {
            mouse_let_go("Escape");
            return true;
        }
        if (e.key.down && code_used(e.key.scancode)) g_last_pad = true;
        if (e.key.scancode < SDL_SCANCODE_COUNT && !(e.key.down && typing())) {
#ifndef __ANDROID__
            // (a modifier's keybinds: pressed as it is let go, if no other key or button went down meanwhile)
            const int sc = e.key.scancode;
            if (e.key.down && !e.key.repeat && !g_keys[sc]) mods_spend();
            if (is_tap_mod(sc)) {
                const int m = sc - SDL_SCANCODE_LCTRL;
                if (e.key.down && !g_keys[sc]) g_mod_held[m] = true, g_mod_spent[m] = false;
                if (!e.key.down && g_mod_held[m]) {
                    g_mod_held[m] = false;
                    if (!g_mod_spent[m] && !typing()) {
                        g_mod_until[m] = SDL_GetTicks() + kWheelPressMs;
                        code_down(sc);
                    }
                }
                g_keys[sc] = e.key.down;
                return false;
            }
#endif
            if (e.key.down && !e.key.repeat && !g_keys[e.key.scancode]) code_down(e.key.scancode);
            g_keys[e.key.scancode] = e.key.down;
        }
        return false;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_WHEEL:
        return mouse_event(e);
    case SDL_EVENT_KEYBOARD_ADDED:
    case SDL_EVENT_KEYBOARD_REMOVED:
    case SDL_EVENT_MOUSE_ADDED:
    case SDL_EVENT_MOUSE_REMOVED:
        kbm_soon();
        return false;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        std::fill(std::begin(g_keys), std::end(g_keys), false);  // (their ups will not come)
#ifndef __ANDROID__
        std::fill(std::begin(g_mod_held), std::end(g_mod_held), false);  // (Alt+Tab: no tap of Alt)
#endif
        g_mouse_held = 0;
        if (g_mouse_grab) mouse_let_go("the app in the background");
        return false;
    default:
        return false;
    }
}

// ---- the settings' Press a controller button (controls_pad_capture): what each open controller held as it began (or
// since let go of: a press again counts), and the first new press, as its Combo<n>Pad value
namespace {
struct PadSnap {
    SDL_JoystickID id;
    std::uint64_t buttons, raw;
    bool zl, zr;
};
std::vector<PadSnap> g_snap;
int g_pad_captured = -1;
PadSnap snap_of(const Pad& p) {
    PadSnap s{p.id, 0, 0, false, false};
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT && b < 64; b++)
        if (SDL_GetGamepadButton(p.pad, static_cast<SDL_GamepadButton>(b))) s.buttons |= 1ull << b;
    if (SDL_Joystick* j = SDL_GetGamepadJoystick(p.pad))
        for (int i = 0; i < kComboPadRaws && i < SDL_GetNumJoystickButtons(j); i++)
            if (SDL_GetJoystickButton(j, i)) s.raw |= 1ull << i;
    s.zl = SDL_GetGamepadAxis(p.pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > kTriggerHalf;
    s.zr = SDL_GetGamepadAxis(p.pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > kTriggerHalf;
    return s;
}
void pad_capture_poll() {
    if (!g_pad_capture || g_pad_captured >= 0) return;
    for (const Pad& p : g_pads) {
        if (p.id == padmap_joystick()) continue;
        const PadSnap now = snap_of(p);
        auto was = std::find_if(g_snap.begin(), g_snap.end(), [&](const PadSnap& s) { return s.id == p.id; });
        if (was == g_snap.end()) {  // (one connected meanwhile: what it holds now is no press)
            g_snap.push_back(now);
            continue;
        }
        const std::uint64_t fresh = now.buttons & ~was->buttons, fresh_raw = now.raw & ~was->raw;
        int v = -1;
        for (int i = 1; i < (int)std::size(COMBO_PAD) && v < 0; i++)
            if (COMBO_PAD[i] != SDL_GAMEPAD_BUTTON_INVALID && (fresh >> COMBO_PAD[i] & 1)) v = i;
        if (v < 0 && now.zl && !was->zl) v = COMBO_ZL;
        if (v < 0 && now.zr && !was->zr) v = COMBO_ZR;
        for (int i = 0; i < kComboPadRaws && v < 0; i++)
            if ((fresh_raw >> i & 1) && !raw_bound(p.pad, i)) v = kComboPadRaw + i;
        // (what was let go of is no longer held: pressed again, it counts)
        was->buttons &= now.buttons, was->raw &= now.raw, was->zl = was->zl && now.zl, was->zr = was->zr && now.zr;
        if (v >= 0) {
            g_pad_captured = v;
            std::fprintf(stderr, "[Info] Controls: %s: %s pressed for a combined button\n",
                         SDL_GetGamepadName(p.pad) ? SDL_GetGamepadName(p.pad) : "?", controls_combo_pad_key(v).c_str());
            return;
        }
    }
}
}  // namespace

ControlsInput controls_input(std::uint32_t game_frame) {
    pad_capture_poll();
    input_report();
    kbm_poll();
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
#ifndef __ANDROID__
    {  // (a menu, a window or a talk of the game's: the movement keys are the D-pad)
        const Port::Hooks::GameState g = Port::Hooks::CurrentGameState();
        const bool menu = g.menu == 1 && g.front_end != 1;
        if (menu != g_keys_menu) std::fprintf(stderr, "[input] W A S D: %s\n", menu ? "the D-pad (a menu of the game's)" : "moving");
        g_keys_menu = menu;
    }
    {  // (a mouse button on A: allowed now or not; held as it became allowed, a press from now - the weapon out)
        const bool was = g_mouse_a;
        g_mouse_a = mouse_a_ok();
        if (g_mouse_a && !was)
            for (int k = 0; k < kBindKeys; k++)
                if (is_mouse_code(g_set.binds.keys[BIND_A][k]) && code_held(g_set.binds.keys[BIND_A][k])) pressed(mh4n::kPadA);
    }
#endif
    u32 held = (pad ? pad_held(pad) : 0) | keys_held();
    const u32 chord_bits = mh4n::kPadStart | mh4n::kPadSelect;
    if (g_chord && !(held & chord_bits)) g_chord = false;
    // the settings: nothing while they are open, and what is held as they close waits to be let go
#ifndef __ANDROID__
    // (and the HUD editor, the list of the controls: the mouse free, the game nothing)
    const bool open = ui_menu_up() || padmap_active() || settings_panel_thanks_open() || touch_editing() || pc_help_open();
    // (1.0.2: the Smithy's weapon tree too frees the mouse, its clicks the panel's fingers; the keys and the controller
    // go on to it below. The chat window afloat: the game's input goes on, the mouse free for the window)
    mouse_capture(open || ui_settings_open() || weapon_tree_is_open());
#else
    const bool open = ui_menu_up() || padmap_active() || settings_panel_thanks_open();  // (1.0.2: as on the PC)
    mouse_capture(open || ui_settings_open());
#endif
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
    if (town_takes_pad() || weapon_tree_takes_pad()) {  // (1.0.2: the Smithy's weapon tree has it the same way)
        g_mouse_dx = g_mouse_dy = 0;  // (the mouse's movement meanwhile is no turn of the camera later)
        Vec st = keys_stick(BIND_MOVE_UP, BIND_MOVE_DOWN, BIND_MOVE_LEFT, BIND_MOVE_RIGHT, false);
        if (pad) st = add(st, stick(pad, SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY));
        if (weapon_tree_takes_pad()) weapon_tree_pad(held, g_town_presses, st.x, st.y);
        else town_pad(held, g_town_presses, st.x, st.y);
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
    if (g_debug) {
        static u32 said = 0;
        if (bits != said) std::fprintf(stderr, "[Info] Controls: the game's buttons %s\n", script_names(bits).c_str());
        said = bits;
    }
    out.buttons = bits & 0xfff;
    out.zl = (bits & ZL_BIT) != 0;
    out.zr = (bits & ZR_BIT) != 0;
    // the sticks: the controller's and the keys', added
    const bool walk = bind_held(BIND_WALK);
    Vec left = keys_stick(BIND_MOVE_UP, BIND_MOVE_DOWN, BIND_MOVE_LEFT, BIND_MOVE_RIGHT, walk);
#ifndef __ANDROID__
    if (g_keys_menu) left = Vec{};  // (a menu's: the movement keys pressed the D-pad instead)
#endif
    Vec right = keys_stick(BIND_CAM_UP, BIND_CAM_DOWN, BIND_CAM_LEFT, BIND_CAM_RIGHT, false);
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
    mouse_camera(hook, dt, out);
    script_input(game_frame, out);
    g_out_buttons = out.buttons;
    return out;
}

std::uint32_t controls_buttons() { return g_out_buttons; }

bool controls_last_input_pad() {
    return g_last_pad;
}

bool controls_real_pad() {
    for (const Pad& p : g_pads)
        if (p.real) return true;
    return false;
}

std::string controls_real_pad_name() {
    for (const Pad& p : g_pads)
        if (p.real) {
            const char* n = SDL_GetGamepadName(p.pad);
            return n ? n : "?";
        }
    return "";
}

bool controls_gyro(bool aiming, float* right_deg, float* up_deg) {
    *right_deg = *up_deg = 0;
    read_settings();
    if (!aiming || !g_set.gyro_aim) {
        if (g_gyro_on) gyro_off();
        return false;
    }
    if (!g_gyro_on) {  // (the aim begins: where it is now is the middle, the phone's pose with it)
        g_gyro_on = true, g_gyro_ns = 0;
        Pad* p = find_pad(g_active);
        SDL_Gamepad* pad = p && SDL_GamepadHasSensor(p->pad, SDL_SENSOR_GYRO) ? p->pad : nullptr;
        if (pad && SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_GYRO, true)) g_gyro_pad = pad;
        int n = 0;
        SDL_SensorID* ids = g_gyro_pad ? nullptr : SDL_GetSensors(&n);
        for (int i = 0; ids && i < n && !g_gyro_dev; i++)
            if (SDL_GetSensorTypeForID(ids[i]) == SDL_SENSOR_GYRO) g_gyro_dev = SDL_OpenSensor(ids[i]);
        SDL_free(ids);
        const SDL_DisplayID d = SDL_GetPrimaryDisplay();
        g_gyro_turns = (display_quarter(SDL_GetCurrentDisplayOrientation(d)) - display_quarter(SDL_GetNaturalDisplayOrientation(d)) + 4) % 4;
        std::fprintf(stderr, "[Info] Controls: the gyro on: %s (the display %d quarter turns from the device's own orientation)\n",
                     g_gyro_pad ? SDL_GetGamepadName(g_gyro_pad) : g_gyro_dev ? SDL_GetSensorName(g_gyro_dev) : "no gyroscope",
                     g_gyro_turns);
    }
    float g[3] = {0, 0, 0}, right = 0, up = 0;  // (radians a second: the aim's turn right, up)
    if (g_gyro_pad && SDL_GetGamepadSensorData(g_gyro_pad, SDL_SENSOR_GYRO, g, 3)) {
        right = -g[1], up = g[0];  // (SDL's: a controller held before you, x right, y up, z toward you)
    } else if (g_gyro_dev && SDL_GetSensorData(g_gyro_dev, g, 3)) {
        // (the device's natural axes: x right, y up, z out of the screen; the screen's up and right in them, by quarter turn)
        static const float ux[4] = {0, 1, 0, -1}, uy[4] = {1, 0, -1, 0}, rx[4] = {1, 0, -1, 0}, ry[4] = {0, -1, 0, 1};
        const int k = g_gyro_turns;
        right = -(g[0] * ux[k] + g[1] * uy[k]), up = g[0] * rx[k] + g[1] * ry[k];
    }
    if (std::fabs(right) < kGyroDeadband) right = 0;
    if (std::fabs(up) < kGyroDeadband) up = 0;
    if (const char* t = std::getenv("MH4U_GYRO_TEST"); t && *t) {  // (tests: "<right>_<up>", degrees a second)
        float tr = 0, tu = 0;
        if (std::sscanf(t, "%f%*[ _,]%f", &tr, &tu) == 2) right = tr * 0.0174533f, up = tu * 0.0174533f;
    }
    const Uint64 now = SDL_GetTicksNS();
    const float dt = g_gyro_ns ? std::min((now - g_gyro_ns) * 1e-9f, 0.1f) : 0.0f;
    g_gyro_ns = now;
    *right_deg = right * dt * 57.29578f * g_set.gyro_sens, *up_deg = up * dt * 57.29578f * g_set.gyro_sens;
    return *right_deg != 0 || *up_deg != 0;
}

SDL_JoystickID controls_active_pad() {
    return g_active;
}

bool controls_a_is_south() {
    Pad* p = find_pad(g_active);
    return p && face_bit(p->pad, SDL_GAMEPAD_BUTTON_SOUTH) == mh4n::kPadA;
}

// ---- the keyboard's and the mouse's keybinds (controls.h)
std::string controls_key_name(int code) {
    for (const auto& k : KEY_NAMES)
        if (k.code == code) return k.name;
    const char* n = code > 0 && code < SDL_SCANCODE_COUNT ? SDL_GetScancodeName(static_cast<SDL_Scancode>(code)) : "";
    return n && *n && !std::strpbrk(n, ",;#") ? n : "";
}

Keybinds controls_default_binds() {
    Keybinds b;
    auto set = [&b](int i, std::initializer_list<int> keys) {
        int k = 0;
        for (int c : keys) b.keys[i][k++] = c;
    };
    set(BIND_MOVE_UP, {SDL_SCANCODE_W}), set(BIND_MOVE_DOWN, {SDL_SCANCODE_S});
    set(BIND_MOVE_LEFT, {SDL_SCANCODE_A}), set(BIND_MOVE_RIGHT, {SDL_SCANCODE_D});
    set(BIND_WALK, {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT});
    set(BIND_CAM_UP, {SDL_SCANCODE_I}), set(BIND_CAM_DOWN, {SDL_SCANCODE_K});
    set(BIND_CAM_LEFT, {SDL_SCANCODE_J}), set(BIND_CAM_RIGHT, {SDL_SCANCODE_L});
    set(BIND_A, {SDL_SCANCODE_O, SDL_SCANCODE_F, kKeyMouse + SDL_BUTTON_RIGHT});
    set(BIND_B, {SDL_SCANCODE_SPACE, SDL_SCANCODE_ESCAPE});
    set(BIND_X, {SDL_SCANCODE_U, SDL_SCANCODE_X, kKeyMouse + SDL_BUTTON_LEFT});
    set(BIND_Y, {SDL_SCANCODE_R, SDL_SCANCODE_Y, kKeyMouse + SDL_BUTTON_MIDDLE});
    set(BIND_L, {SDL_SCANCODE_Q}), set(BIND_R, {SDL_SCANCODE_E});
    set(BIND_ZL, {SDL_SCANCODE_TAB}), set(BIND_ZR, {SDL_SCANCODE_T});
    set(BIND_START, {SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER}), set(BIND_SELECT, {SDL_SCANCODE_BACKSPACE});
    set(BIND_DPAD_UP, {SDL_SCANCODE_UP}), set(BIND_DPAD_DOWN, {SDL_SCANCODE_DOWN});
    set(BIND_DPAD_LEFT, {SDL_SCANCODE_LEFT}), set(BIND_DPAD_RIGHT, {SDL_SCANCODE_RIGHT});
    set(BIND_LSTICK, {SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL}), set(BIND_RSTICK, {SDL_SCANCODE_LALT, SDL_SCANCODE_RALT});
    set(BIND_ITEM_LEFT, {kKeyWheelUp}), set(BIND_ITEM_RIGHT, {kKeyWheelDown});
#ifndef __ANDROID__
    // (the PC's: F2 the controls' list, 1 to 4 the touch screen's item shortcuts, G the town's Gestures; the town's other
    // actions on no key: SELECT's list has them)
    set(BIND_HELP, {SDL_SCANCODE_F2});
    for (int i = 0; i < 4; i++) set(BIND_SCREEN_ITEM1 + i, {SDL_SCANCODE_1 + i});
    set(BIND_TOWN_GESTURES, {SDL_SCANCODE_G});
#endif
    return b;
}

std::string controls_keys_text(const int keys[kBindKeys]) {
    std::string t;
    for (int k = 0; k < kBindKeys; k++) {
        const std::string n = keys[k] ? controls_key_name(keys[k]) : "";
        if (!n.empty()) t += (t.empty() ? "" : ", ") + n;
    }
    return t.empty() ? "None" : t;
}

Keybinds controls_read_binds() {
    const Keybinds d = controls_default_binds();
    Keybinds b = d;
    for (int i = 0; i < BINDS; i++) {
        const std::string v = app_setting("Keybinds", kBindNames[i], "");
        if (!v.empty()) parse_keys(v, b.keys[i]);
    }
    return b;
}

bool controls_keyboard_mouse() { return g_kbm == 3; }

#ifndef __ANDROID__
bool controls_mouse_captured() { return g_mouse_captured; }
int controls_last_device() { return g_last_device; }
void controls_mouse_let_go(const char* why) { mouse_let_go(why); }
void controls_shortcut_used() { mods_spend(); }
#endif

void controls_capture(bool on, bool mouse) {
    if (on && !g_capture) g_captured = -1;
    g_capture = on, g_capture_mouse = on && mouse;
}

int controls_take_key() {
    const int c = g_captured;
    g_captured = -1;
    return c;
}

int controls_combo_pad_value(const std::string& text) {
    for (int i = 1; i < (int)std::size(kComboPadKeys); i++)
        if (!SDL_strcasecmp(text.c_str(), kComboPadKeys[i])) return i;
    if (!SDL_strncasecmp(text.c_str(), "Raw", 3) && text.size() > 3 && text.size() <= 5 &&
        text.find_first_not_of("0123456789", 3) == std::string::npos) {
        const int i = std::atoi(text.c_str() + 3);
        if (i >= 0 && i < kComboPadRaws) return kComboPadRaw + i;
    }
    return 0;
}
std::string controls_combo_pad_key(int value) {
    if (value >= kComboPadRaw && value < kComboPadRaw + kComboPadRaws) return "Raw" + std::to_string(value - kComboPadRaw);
    return value > 0 && value < (int)std::size(kComboPadKeys) ? kComboPadKeys[value] : kComboPadKeys[0];
}
void controls_pad_capture(bool on) {
    if (on == g_pad_capture) return;
    g_pad_capture = on, g_pad_captured = -1, g_snap.clear();
    if (on)
        for (const Pad& p : g_pads) g_snap.push_back(snap_of(p));
    else if (Pad* p = find_pad(g_active); p && any_button(p->pad))
        g_remapped = p->id;  // (the button pressed for it, still held: not the menu's until let go)
    imgui_pad();
    std::fprintf(stderr, "[Info] Controls: a controller button for a combined button: %s\n", on ? "waiting" : "done");
}
int controls_take_pad() {
    const int v = g_pad_captured;
    g_pad_captured = -1;
    return v;
}
