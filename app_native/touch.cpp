// Touch controls for phones, in the Monster Hunter style (the MHGU port's runtime/touch.cpp, MIT, made over for the
// 3DS's buttons): bronze-rimmed dark discs with cream glyphs and the game's button letter on a small badge.
//   - left thumb: a stick fixed at its place (the knob follows the thumb inside the rim); a light tilt walks, a full
//     one runs, and the thumb well past the rim dashes (R held, with the weapon sheathed)
//   - right thumb: the attack pill (A on its upper half, X on the lower, both on its seam: the 3DS's X draws and
//     attacks, A attacks, gathers, carves and talks), R guard / dash beside its lower end, B evade / crouch beside it,
//     Y sheathe (or the item, sheathed) left of it; on an arc above it ZR (the kick; once a quest is complete, Skip:
//     the port's kick and skip options), ZL (the target camera: the port's target option, the touch screen's panel)
//     and L (the camera behind the hunter, or on the target; held, the lock-on: the camera keeps turning to the monster,
//     as in the newer games, until held again; a controller's right stick click too). START (the game's menu), the touch
//     screen's corner and the settings at the left edge, with the button that changes between the hunt's layout and
//     the menus'
//   - in a menu of the game's, the menu layout instead: the stick stays and moves the cursor, as the d-pad (one
//     direction at a time); where the attacks are the same pill, confirming (A) on its upper half and going back (B)
//     on the lower, so the thumb stays put; X and Y small beside it, L and R as tabs above it, SELECT beside START. A
//     drag anywhere else moves the cursor, a step every so far, as a list on a phone. Until the app can tell a menu is
//     open (ui_set_game_state), the layout button chooses
//   - the game's own HUD is touched where it is drawn (the hooks say where: Port::Hooks::GameState::hud). The item bar:
//     a tap uses the item (Y; with the weapon out, Y puts it away and the item is used once it is away), a swipe turns
//     the bar (the hooks take the game's own step, as the touch screen's bar does: the strip follows the finger; no L,
//     which would turn the camera too), a long press holds it open with its neighbours (and a bow's coatings) while a
//     swipe turns it; with a bow or a bowgun, up or down changes the ammo or the coating (L held with X or B). The
//     target camera's icon (the single-screen mod's, on the top screen): a tap does what the touch screen's panel does,
//     a long press turns the lock-on on and off
//   - with a bow or a bowgun in hand, in a hunt, a layout of their own: a large button under the thumb that fires
//     (a drag on it, held, aims: the right thumb can't be on the screen and on a button at once), a smaller one that
//     reloads (a bow: its melee attack), aim, crouching fire (a bow: its coating), evade, sheathe, and arrows for the
//     ammo. Laid out on its own in the editor
//   - up to six combined buttons of the player's own (the settings' Buttons on the screen: A+X, say), each pressing
//     its buttons together; placed in the editor
//   - anywhere else: dragging turns the camera by the angle moved, as in a phone game (the port's camera hook while the
//     game's camera takes it, Port::Hooks::CameraTurnActive; elsewhere as the C-stick); a double tap centres it (L)
//   - the touch screen's corner, while it shows, is the game's: a finger there is the stylus, and the controls it would
//     cover move out from under it, the whole side's group together (above it when they fit, else beside it). Its
//     button (the 3DS's two screens) shows and hides it
// Every control can be moved and resized (the layout editor: a long press on the settings button), kept in
// touch_layout.txt in the app's folder, with which buttons show and the opacity. The editor moves the game's HUD too,
// part by part (the clock, the health and stamina bars, the sharpness, the name plates, the area's map, the target
// camera, the item bar: the hooks move the game's own layout objects; hud_<part> in mh4u.ini). Shown while no controller is
// connected (controls auto), always (on) or never (off: the settings button alone).
#include "touch.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include <atomic>

#include <jni.h>

#include "app_settings.h"
#include "game_options.h"
#include "settings_locale.h"
#include "settings_panel.h"
#include "town.h"
#include "hooks.h"
#include "imgui.h"
#include "mh4n/host.h"
#include "mh4n/present.h"
#include "mh4n/registry.h"
#include "mh4n/runtime.h"

namespace {

const float PI = 3.14159265f;
const uint64_t MS = 1000000ull;

// the game's buttons: the 3DS's own bits (mh4n::kPad*), and ZL / ZR (UiInput's zl and zr) above them
const uint64_t BTN_A = mh4n::kPadA, BTN_B = mh4n::kPadB, BTN_X = mh4n::kPadX, BTN_Y = mh4n::kPadY,
               BTN_L = mh4n::kPadL, BTN_R = mh4n::kPadR, BTN_START = mh4n::kPadStart, BTN_SELECT = mh4n::kPadSelect,
               BTN_UP = mh4n::kPadUp, BTN_DOWN = mh4n::kPadDown, BTN_LEFT = mh4n::kPadLeft,
               BTN_RIGHT = mh4n::kPadRight, BTN_ZL = 1ull << 16, BTN_ZR = 1ull << 17;

// The phone's vibration for a pressed control (com.mh4u.port.Haptics.tap, by JNI: this is SDL's thread), while
// [Controls] Haptics in mh4u-app.ini (the settings panel's Vibrate on touch) is on
void haptics_tap() {
    static unsigned version = ~0u;
    static bool on = true, failed = false;
    static jclass cls = nullptr;
    static jmethodID tap = nullptr;
    if (app_settings_version() != version)
        version = app_settings_version(), on = app_setting_bool("Controls", "Haptics", true);
    if (!on || failed) return;
    auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    if (!env) return;
    if (!cls) {  // (through the activity's class loader: FindClass from native code sees only the system's classes)
        jobject act = static_cast<jobject>(SDL_GetAndroidActivity());
        jclass act_cls = act ? env->GetObjectClass(act) : nullptr;
        jmethodID get_loader = act_cls ? env->GetMethodID(act_cls, "getClassLoader", "()Ljava/lang/ClassLoader;") : nullptr;
        jobject loader = get_loader ? env->CallObjectMethod(act, get_loader) : nullptr;
        jclass loader_cls = loader && !env->ExceptionCheck() ? env->FindClass("java/lang/ClassLoader") : nullptr;
        jmethodID load = loader_cls ? env->GetMethodID(loader_cls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;") : nullptr;
        jstring name = load ? env->NewStringUTF("com.mh4u.port.Haptics") : nullptr;
        jobject found = name ? env->CallObjectMethod(loader, load, name) : nullptr;
        if (found && !env->ExceptionCheck()) cls = static_cast<jclass>(env->NewGlobalRef(found));
        if (cls) tap = env->GetStaticMethodID(cls, "tap", "()V");
        if (env->ExceptionCheck()) env->ExceptionClear();
        for (jobject o : {found, (jobject)name, (jobject)loader_cls, loader, (jobject)act_cls, act})
            if (o) env->DeleteLocalRef(o);
        if (!cls || !tap) {
            failed = true;
            fprintf(stderr, "[touch] com.mh4u.port.Haptics not usable: no vibration\n");
            return;
        }
    }
    env->CallStaticVoidMethod(cls, tap);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

// The lock-on ([Controls] LockOn in mh4u-app.ini, the settings panel's row; on unless turned off): a long press on L
// or a controller's right stick click turns it on and off (Port::Hooks::SetLockOn: the hooks turn the quest camera)
bool lock_on_allowed() {
    static unsigned version = ~0u;
    static bool on = true;
    if (app_settings_version() != version) version = app_settings_version(), on = app_setting_bool("Controls", "LockOn", true);
    return on;
}

void lock_toggle(const char *by) {
    bool on = lock_on_allowed() && !Port::Hooks::LockOn();
    Port::Hooks::SetLockOn(on);
    fprintf(stderr, "[touch] lock-on %s (%s)\n", on ? "on" : "off", by);
}

// ------------------------------------------------------------------ settings (touch_layout.txt)
enum { TOUCH_AUTO, TOUCH_ON, TOUCH_OFF };
// the game's buttons, for showing each on the screen or not
enum { TB_ATTACKS, TB_DODGE, TB_GUARD, TB_SHEATHE, TB_START, TB_SELECT, TB_DPAD, TB_L, TB_ZL, TB_ZR, TB_SCREEN, TB_STICK,
       TB_RANGED, TB_FIRE, TB_SECOND, TB_AIM, TB_RSPECIAL, TB_AMMO, TB_MDPAD, TB_ITEMARROWS, TB_COUNT };
const char *const TB_KEY[TB_COUNT] = {"attacks", "dodge", "guard", "sheathe", "start", "select", "dpad", "l", "zl", "zr",
                                      "screen", "stick", "ranged", "fire", "second", "aim", "ranged_special", "ammo",
                                      "menu_dpad", "item_arrows"};
// (off: SELECT and the d-pad in a hunt, which MH4U's hunts don't need. The item bar has no arrows: a tap on it uses the
// item, a swipe turns it, a long press opens it)
const bool TB_DEFAULT[TB_COUNT] = {1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
struct Settings {
    int controls = TOUCH_AUTO;
    float camera = 1.0f;   // the camera's turn for a drag (1: about 90 degrees a screen height)
    float opacity = .8f;   // of the controls, .2 .. 1
    bool show[TB_COUNT];
    bool debug = false;    // (debug 1: what each finger lands on, in the log)
};
Settings g_set;
bool show(int b) { return b < 0 || g_set.show[b]; }

// The combined buttons: the game's buttons each presses ("A+X"), 0 none (touch_layout.txt combo_buttons)
const int COMBOS = 6;
uint64_t g_combo[COMBOS];
const struct { const char *name; uint64_t bit; } COMBO_NAMES[] = {
    {"A", mh4n::kPadA}, {"B", mh4n::kPadB}, {"X", mh4n::kPadX}, {"Y", mh4n::kPadY}, {"L", mh4n::kPadL},
    {"R", mh4n::kPadR}, {"ZL", 1ull << 16}, {"ZR", 1ull << 17}, {"START", mh4n::kPadStart}, {"SELECT", mh4n::kPadSelect},
    {"UP", mh4n::kPadUp}, {"DOWN", mh4n::kPadDown}, {"LEFT", mh4n::kPadLeft}, {"RIGHT", mh4n::kPadRight}};
uint64_t combo_mask(const std::string &text) {
    uint64_t m = 0;
    size_t at = 0;
    while (at <= text.size()) {
        size_t plus = text.find('+', at);
        std::string name = text.substr(at, plus == std::string::npos ? std::string::npos : plus - at);
        for (auto &c : name) c = (char)toupper((unsigned char)c);
        for (const auto &n : COMBO_NAMES)
            if (name == n.name) m |= n.bit;
        if (plus == std::string::npos) break;
        at = plus + 1;
    }
    return m;
}
std::string combo_text(uint64_t m) {
    std::string t;
    for (const auto &n : COMBO_NAMES)
        if (m & n.bit) t += (t.empty() ? "" : "+") + std::string(n.name);
    return t;
}

// The interface's language: the words the editor shows, in Spanish where the settings say (settings_locale.h)
const char *T(const char *english) { return settings_text(settings_spanish() ? 1 : 0, english); }

// what android_main.cpp knows of the game (ui_set_game_state)
UiGameState g_game;
// The parts of the game's HUD (UiGameState::hud, Port::Hooks::HudPartKey's order), and their names in the editor
enum { HP_CLOCK, HP_BARS, HP_SHARPNESS, HP_PARTY, HP_MAP, HP_TARGET, HP_ITEMS };
const char *const HUD_KEY[UiGameState::HUD_PARTS] = {"clock", "bars", "sharpness", "party", "map", "target", "items"};
const char *const HUD_NAME[UiGameState::HUD_PARTS] = {"Clock", "Health and stamina", "Sharpness", "Name plates",
                                                      "Area map", "Target camera", "Item bar"};
const int ZONE_SEL = 100;  // (the editor's s.sel and a finger's ctl: a part of the HUD, ZONE_SEL + its HP_*)
bool g_menu_layout;     // the menu layout is the one in use (the layout button's choice, or a known change)
int g_menu_known = -1;  // g_game.menu at its last change

// ------------------------------------------------------------------ the controls and their default places
enum Ctl {
    C_ATTACK, C_DODGE, C_R, C_ZR, C_ZL, C_L, C_MENU, C_SCREEN, C_SETTINGS, C_SHEATHE, C_LAYOUT, C_HEAVY, C_LIGHT,
    C_CORNER, C_HSELECT, C_HDPAD,
    C_RFIRE, C_RSECOND, C_RAIM, C_RSPECIAL, C_RDODGE, C_RSHEATHE, C_AMMO_UP, C_AMMO_DOWN,  // (a bow's or a bowgun's)
    C_TOWN,                                                                                // (a town's: town.h)
    C_MDPAD, C_MSELECT, C_MY,          // (the game's menus: the small d-pad, and SELECT and Y where the layout has none)
    C_ITEML, C_ITEMR,                  // (the item bar's arrows, in a hunt)
    C_COMBO, C_COMBO_END = C_COMBO + 5,                                                    // (the player's combined buttons)
    C_STICK,                                                                               // (laid out: touch_layout.txt)
    C_DPAD, C_MPILL, C_FX, C_FY, C_SELECT, C_ML, C_MR,  // (the menu layout's: placed from the stick, the pill, START)
    C_COUNT
};
const int C_LAID = C_STICK + 1;
enum Icon { I_ATTACK, I_DODGE, I_SHIELD, I_KICK, I_TARGET_SELECT, I_CAMERA, I_MENU, I_SCREEN, I_GEAR, I_SHEATHE, I_LAYOUT,
            I_STICK, I_DPAD, I_LETTER, I_SELECT, I_SHOT, I_BOW, I_RELOAD, I_SCOPE, I_CROUCH, I_COATING, I_MELEE, I_GATHER,
            I_UP, I_DOWN, I_SKIP, I_TOWN, I_LEFT, I_RIGHT };

struct Def {
    const char *key, *name;  // in touch_layout.txt; in the layout editor
    Icon icon;
    const char *letter;      // the game's button, on the badge
    uint64_t buttons;
    bool right;              // placed from the screen's right edge (else from the left)
    float x, y, r;           // the default centre (screen heights from that edge and from the bottom) and radius (the
                             // attack pill: half its thickness; the stick: its base)
};
// The attack pill lies along the right thumb's arc (rising to the right, 40 degrees), both halves as far from the
// screen's corner, which it leaves to the game's item bar (MH4U draws it there; the editor can move it); R (guard, dash) by
// its lower end, B (evade) right of it above the item bar; the small buttons on an arc above it; the menus in a column
// at the left edge, below the hunter's gauges
const Def DEFS[C_COUNT] = {
    {"attack", "Attack  A / X", I_ATTACK, "", 0, true, .255f, .37f, .085f},
    {"dodge", "Evade  B", I_DODGE, "B", BTN_B, true, .085f, .255f, .056f},
    {"guard", "Guard / dash  R", I_SHIELD, "R", BTN_R, true, .505f, .2f, .058f},
    {"zr", "Kick  ZR", I_KICK, "ZR", BTN_ZR, true, .075f, .78f, .045f},
    {"target", "Target  ZL", I_TARGET_SELECT, "ZL", BTN_ZL, true, .19f, .8f, .045f},
    {"camera", "Camera / lock-on  L", I_CAMERA, "L", BTN_L, true, .31f, .74f, .045f},
    {"start", "Menu  START", I_MENU, "START", BTN_START, false, .065f, .55f, .046f},
    {"screen", "Touch screen", I_SCREEN, "", 0, false, .065f, .43f, .046f},
    {"settings", "Settings", I_GEAR, "", 0, false, .065f, .67f, .04f},
    // sheathe: Y with the weapon out (sheathed, Y uses the item: the item bar's tap); left of the pill's upper half
    {"sheathe", "Sheathe  Y", I_SHEATHE, "Y", BTN_Y, true, .44f, .54f, .046f},
    {"layout", "Hunt / menus", I_LAYOUT, "", 0, false, .17f, .67f, .04f},
    // the attack buttons as two of their own (STYLE_SEPARATE), and as one band round the screen's corner, above the
    // item bar (STYLE_CORNER: x, y the band's outer edge from the screen's edges, r the radius of its middle line at the
    // bend)
    {"heavy", "Attack  A", I_ATTACK, "A", BTN_A, true, .16f, .43f, .078f},
    {"light", "Draw / attack  X", I_ATTACK, "X", BTN_X, true, .34f, .27f, .078f},
    {"corner", "Attack and guard", I_ATTACK, "", 0, true, .03f, .19f, .26f},
    // off by default (show_select, show_dpad): the game's buttons a hunt can do without
    {"select_hunt", "SELECT", I_SELECT, "SELECT", BTN_SELECT, false, .18f, .55f, .046f},
    {"dpad_hunt", "D-pad", I_DPAD, "", 0, false, .33f, .6f, .105f},
    // a bow's or a bowgun's (their buttons: ranged_buttons): the shot large under the thumb, the rest in an arc round it,
    // the evade by the corner, the ammo arrows beside them
    {"fire", "Fire / shoot", I_SHOT, "", 0, true, .215f, .36f, .115f},
    {"second", "Reload / melee", I_RELOAD, "", 0, true, .37f, .225f, .07f},
    {"aim", "Aim", I_SCOPE, "", 0, true, .415f, .39f, .07f},
    {"rspecial", "Crouching fire / coating", I_CROUCH, "", 0, true, .33f, .545f, .058f},
    {"rdodge", "Evade  B", I_DODGE, "B", BTN_B, true, .075f, .225f, .056f},
    {"rsheathe", "Sheathe  Y", I_SHEATHE, "Y", BTN_Y, true, .16f, .57f, .046f},
    {"ammo_up", "Next ammo", I_UP, "", 0, true, .56f, .4f, .05f},
    {"ammo_down", "Previous ammo", I_DOWN, "", 0, true, .56f, .23f, .05f},
    // the town's actions that only the touch screen had (Guild Cards, Gestures, Friends, Hunters for Hire: town.h), in a
    // town while the hunter is walked; below the touch screen's button
    {"town", "Town actions", I_TOWN, "", 0, false, .065f, .31f, .046f},
    // while a menu of the game's is open (one layout everywhere: the menus add what it lacks there): a small d-pad above
    // the stick, SELECT beside START unless the hunt's shows, Y where the sheathe button is unless that shows
    {"menu_dpad", "D-pad (menus)", I_DPAD, "", 0, false, .32f, .55f, .075f},
    {"menu_select", "SELECT (menus)", I_SELECT, "SELECT", BTN_SELECT, false, .18f, .55f, .042f},
    {"menu_y", "Y (menus)", I_LETTER, "Y", BTN_Y, true, .44f, .54f, .046f},
    // the item bar's arrows, left of the bar (its usual place: the top screen's bottom-right corner): a tap turns it an
    // item, held it repeats
    {"item_left", "Item bar: previous", I_LEFT, "", 0, true, .64f, .085f, .034f},
    {"item_right", "Item bar: next", I_RIGHT, "", 0, true, .565f, .085f, .034f},
    // the combined buttons (shown once the settings give them buttons), in two columns left of the attacks
    {"combo1", "Combined button 1", I_LETTER, "", 0, true, .62f, .30f, .05f},
    {"combo2", "Combined button 2", I_LETTER, "", 0, true, .62f, .44f, .05f},
    {"combo3", "Combined button 3", I_LETTER, "", 0, true, .62f, .58f, .05f},
    {"combo4", "Combined button 4", I_LETTER, "", 0, true, .75f, .30f, .05f},
    {"combo5", "Combined button 5", I_LETTER, "", 0, true, .75f, .44f, .05f},
    {"combo6", "Combined button 6", I_LETTER, "", 0, true, .75f, .58f, .05f},
    {"stick", "Movement", I_STICK, "", 0, false, .29f, .27f, .115f},
    // the menu layout's (x, y unused: place())
    {"dpad", "D-pad", I_DPAD, "", 0, false, 0, 0, .145f},
    {"menu_pill", "Confirm and back", I_ATTACK, "", 0, true, 0, 0, .085f},
    {"x", "X", I_LETTER, "X", BTN_X, true, 0, 0, .05f},
    {"y", "Y", I_LETTER, "Y", BTN_Y, true, 0, 0, .05f},
    {"select", "SELECT", I_SELECT, "SELECT", BTN_SELECT, false, 0, 0, .046f},
    {"ml", "L", I_LETTER, "L", BTN_L, true, 0, 0, .044f},  // (the shoulders: tabs in the item box, the shops)
    {"mr", "R", I_LETTER, "R", BTN_R, true, 0, 0, .044f},
};
enum { STYLE_PILL, STYLE_SEPARATE, STYLE_CORNER, STYLE_COUNT };  // how the attacks and the guard are laid out
int g_style = STYLE_PILL;    // (Layout.style)
int g_ranged = 0;            // the weapon (UiGameState.ranged) while the layout for it applies, else 0
int g_edit_set = -1;         // the editor's: which layout it shows, 0 the usual one, 1 the ranged one; -1 not open
// (a bow's or a bowgun's buttons are no menu's: in the game's menus the other set, with A, B, X and Y as they are)
bool ranged_layout() { return g_edit_set >= 0 ? g_edit_set == 1 : g_ranged != 0 && g_game.menu != 1; }
bool is_ranged(int i) { return i >= C_RFIRE && i <= C_AMMO_DOWN; }
bool is_combo(int i) { return i >= C_COMBO && i <= C_COMBO_END; }
int show_setting(int i) {  // the show_* a control follows (the settings and layout buttons have none: always there)
    switch (i) {
    case C_ATTACK: case C_HEAVY: case C_LIGHT: case C_CORNER: return TB_ATTACKS;
    case C_DODGE: case C_RDODGE: return TB_DODGE;
    case C_RFIRE: return TB_FIRE;
    case C_RSECOND: return TB_SECOND;
    case C_RAIM: return TB_AIM;
    case C_RSPECIAL: return TB_RSPECIAL;
    case C_AMMO_UP: case C_AMMO_DOWN: return TB_AMMO;
    case C_SHEATHE: case C_RSHEATHE: return TB_SHEATHE;
    case C_R: return TB_GUARD;
    case C_MENU: return TB_START;
    case C_HSELECT: return TB_SELECT;
    case C_HDPAD: return TB_DPAD;
    case C_L: return TB_L;
    case C_ZL: return TB_ZL;
    case C_ZR: return TB_ZR;
    case C_SCREEN: return TB_SCREEN;
    case C_STICK: return TB_STICK;
    case C_MDPAD: return TB_MDPAD;
    case C_ITEML: case C_ITEMR: return TB_ITEMARROWS;
    default: return -1;
    }
}
bool is_tab(int i) { return i == C_ML || i == C_MR; }  // (the menu's shoulders, drawn as tabs)
bool is_dpad(int i) { return i == C_DPAD || i == C_HDPAD || i == C_MDPAD; }
bool g_town_edit;  // (the layout editor is open: the buttons that come and go show there wherever the game is)
// One layout, the player's, in the game's menus too (`menu`: one is open): they add the small d-pad, and SELECT and Y
// where the layout has none; the item bar's arrows are a hunt's, beside the bar while it shows
bool in_mode(int i, bool menu) {
    if (i == C_TOWN) return !menu && (g_town_edit || town_available());
    if (i == C_LAYOUT || i >= C_DPAD) return false;  // (the menu layout's own: gone)
    if (i == C_MDPAD) return show(TB_MDPAD) && (menu || g_town_edit);
    if (i == C_MSELECT) return !show(TB_SELECT) && (menu || g_town_edit);
    if (i == C_MY) return !show(TB_SHEATHE) && (menu || g_town_edit);
    if (i == C_ITEML || i == C_ITEMR)
        return show(TB_ITEMARROWS) && (g_town_edit || (!menu && g_game.place == 2 && g_game.item_bar_shown != 0));
    if (i == C_SETTINGS) return true;
    if (i == C_MENU || i == C_SCREEN) return show(show_setting(i));
    if (is_combo(i)) return g_combo[i - C_COMBO] != 0;  // (the hunt's, either layout, once it has buttons)
    if (is_ranged(i) ? !ranged_layout() : ranged_layout() && (i == C_ATTACK || i == C_HEAVY || i == C_LIGHT || i == C_CORNER ||
                                                              i == C_R || i == C_DODGE || i == C_SHEATHE))
        return false;
    if (!show(show_setting(i))) return false;
    if (i == C_ATTACK) return g_style == STYLE_PILL;
    if (i == C_HEAVY || i == C_LIGHT) return g_style == STYLE_SEPARATE;
    if (i == C_CORNER) return g_style == STYLE_CORNER;
    if (i == C_R) return g_style != STYLE_CORNER;
    return true;
}
const ImVec2 PILL_U(.766f, -.643f);  // the pill's axis on the screen (y down): up and to the right
const float PILL_HALF = 1.9f;        // its half length, in its radii
const float PILL_SEAM = .17f;        // the seam's half width (both buttons), in half lengths
const float TAB_W = 1.75f, TAB_H = .85f;  // the menu's L and R tabs: half their width and height, in their radius
const uint64_t PILL_BUTTONS[4] = {BTN_A, BTN_X, BTN_X | BTN_A, BTN_R};  // the upper half, the lower, the seam (both); the
                                                                         // corner's guard
// The corner band, from its left end along the bottom, round the bend, up the right edge; in its bend's radius r:
const float BAND_T = .365f;     // its half thickness
const float BAND_LEFT = 1.15f;  // the straight part along the bottom
const float BAND_UP = .96f;     // the straight part up the side
const float BAND_LEN = BAND_LEFT + PI * .5f + BAND_UP;
// where its buttons end, in parts of its length: the guard, the light attack, the seam (both attacks), the heavy attack
const float BAND_GUARD = .30f, BAND_SEAM0 = .63f, BAND_SEAM1 = .69f;
const float KNOB = .45f;            // the stick's knob, in its base's radius
const float CAMERA_DEGREES = 92.0f; // the camera's turn for a drag the screen's height (the MHGU port's, with its mouse
                                    // camera: 1150 counts of 0.08 degrees)

// What a control of the ranged layout presses (the manual's controls). Out, a bowgun: A fires, X reloads, R aims (a
// tap the scope, held the quick aim), X + A crouching fire (a heavy bowgun); a bow: the game's Bow Controls say which
// of X and R draws (held, let go it shoots) and which aims; A its melee attack, X + A its coating. Sheathed the large
// button brings the weapon out (X: R would dash) and the smaller one does what A does then: gather, carve, talk
uint64_t ranged_buttons(int c, int drawn) {
    bool bow = g_ranged == 1, type2 = g_game.bow_type != 0, out = drawn == 1;
    switch (c) {
    case C_RFIRE: return !out ? BTN_X : !bow ? BTN_A : type2 ? BTN_R : BTN_X;
    case C_RSECOND: return !out || bow ? BTN_A : BTN_X;
    case C_RAIM: return !out || (bow && type2) ? BTN_X : BTN_R;
    case C_RSPECIAL: return BTN_X | BTN_A;
    default: return DEFS[c].buttons;
    }
}

struct Layout {
    float x[C_COUNT], y[C_COUNT], scale[C_COUNT];
    int style;  // STYLE_*
};
Layout defaults() {
    Layout l = {};
    for (int i = 0; i < C_COUNT; i++) l.x[i] = DEFS[i].x, l.y[i] = DEFS[i].y, l.scale[i] = 1.0f;  // (the menu layout's: unused)
    return l;
}
std::string g_layout_path = "touch_layout.txt";  // (the app's folder: touch_init)
const char *const CONTROLS_KEY[3] = {"auto", "on", "off"};

bool load_layout(Layout *l) {
    *l = defaults();
    for (int b = 0; b < TB_COUNT; b++) g_set.show[b] = TB_DEFAULT[b];
    for (int k = 0; k < COMBOS; k++) g_combo[k] = 0;
    FILE *f = fopen(g_layout_path.c_str(), "r");
    if (!f) return false;
    char line[160], key[40], word[40];
    float x, y, s;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        int n = sscanf(line, "%39s %f %f %f", key, &x, &y, &s);
        if (n == 4) {
            for (int i = 0; i < C_LAID; i++)
                if (!strcmp(key, DEFS[i].key) && x > 0 && x < 4 && y > 0 && y < 1 && s >= .4f && s <= 2.5f)
                    l->x[i] = x, l->y[i] = y, l->scale[i] = s;
        }
        if (n >= 2 && !strcmp(key, "attack_style") && x >= 0 && x < (float)STYLE_COUNT) l->style = (int)x;
        if (n >= 2 && !strcmp(key, "opacity")) g_set.opacity = std::clamp(x, .2f, 1.0f);
        if (n >= 2 && !strcmp(key, "camera_speed")) g_set.camera = std::clamp(x, .2f, 5.0f);
        if (n >= 2 && !strcmp(key, "debug")) g_set.debug = x != 0;
        if (n >= 2 && !strncmp(key, "show_", 5))
            for (int b = 0; b < TB_COUNT; b++)
                if (!strcmp(key + 5, TB_KEY[b])) g_set.show[b] = x != 0;
        int k;
        if (sscanf(line, "combo_buttons %d %39s", &k, word) == 2 && k >= 1 && k <= COMBOS) g_combo[k - 1] = combo_mask(word);
        if (sscanf(line, "%39s %39s", key, word) == 2 && !strcmp(key, "controls"))
            for (int m = 0; m < 3; m++)
                if (!strcmp(word, CONTROLS_KEY[m])) g_set.controls = m;
    }
    fclose(f);
    return true;
}

void save_layout(const Layout &l) {
    FILE *f = fopen(g_layout_path.c_str(), "w");
    if (!f) { fprintf(stderr, "[touch] could not save %s\n", g_layout_path.c_str()); return; }
    fprintf(f, "# MH4U's touch controls: name, centre (screen heights from its edge and from the bottom), size\n");
    for (int i = 0; i < C_LAID; i++) fprintf(f, "%s %.4f %.4f %.3f\n", DEFS[i].key, l.x[i], l.y[i], l.scale[i]);
    fprintf(f, "# the attacks: 0 the pill, 1 two buttons, 2 a band round the corner\nattack_style %d\n", l.style);
    fprintf(f, "# shown while no controller is connected (auto), always (on) or never (off: the settings button alone)\n");
    fprintf(f, "controls %s\nopacity %.2f\ncamera_speed %.2f\n", CONTROLS_KEY[g_set.controls], g_set.opacity, g_set.camera);
    fprintf(f, "# which of the game's buttons show (1) or not (0)\n");
    for (int b = 0; b < TB_COUNT; b++) fprintf(f, "show_%s %d\n", TB_KEY[b], g_set.show[b] ? 1 : 0);
    fprintf(f, "# the combined buttons: the game's buttons each presses together\n");
    for (int k = 0; k < COMBOS; k++)
        if (g_combo[k]) fprintf(f, "combo_buttons %d %s\n", k + 1, combo_text(g_combo[k]).c_str());
    if (g_set.debug) fprintf(f, "debug 1\n");
    fclose(f);
    fprintf(stderr, "[touch] layout saved (%s)\n", g_layout_path.c_str());
}

ImVec2 add(ImVec2 a, ImVec2 b, float k) { return ImVec2(a.x + b.x * k, a.y + b.y * k); }

struct Geo {
    ImVec2 c;
    float r;
};
// While the touch screen's corner shows, the controls placed from the side it is on move out from under it, all
// together so that they keep their shape: above it (smaller when they don't fit there, down to MIN_FIT), else beside
// it ([0] the left side's, [1] the right's: update_shift). Nothing moves in the layout itself
struct Fit {
    float scale = 1;
    ImVec2 anchor, offset;  // (a point scaled about the anchor, then moved by the offset)
};
Fit g_fit[2];
const float MIN_FIT = .6f;
Geo place_raw(const Layout &l, int i, float w, float h);
Geo place(const Layout &l, int i, float w, float h) {
    Geo g = place_raw(l, i, w, h);
    const Fit &f = g_fit[DEFS[i].right ? 1 : 0];
    if (f.scale == 1 && f.offset.x == 0 && f.offset.y == 0) return g;
    return {ImVec2(f.anchor.x + (g.c.x - f.anchor.x) * f.scale + f.offset.x, f.anchor.y + (g.c.y - f.anchor.y) * f.scale + f.offset.y),
            g.r * f.scale};
}
Geo place_raw(const Layout &l, int i, float w, float h) {
    const Def &d = DEFS[i];
    // the item bar's arrows follow the bar where the game draws it (the player may have moved it), left of its name
    // plate with a gap; once the player places them in the editor, that place is theirs
    if ((i == C_ITEML || i == C_ITEMR) && fabsf(l.x[i] - d.x) < 1e-4f && fabsf(l.y[i] - d.y) < 1e-4f &&
        g_game.item_bar_shown == 1 && g_game.item_bar.w > 0 && g_game.item_bar.h > 0) {
        const SDL_FRect &b = g_game.item_bar;
        const float r = d.r * l.scale[i] * h, gap = r * .5f;
        const float cy = b.y + b.h - std::min(b.h * .5f, r * 1.2f);
        const float cx = b.x - gap - r - (i == C_ITEML ? 2 * r + gap * .6f : 0);
        return {ImVec2(std::max(cx, r), std::min(cy, h - r)), r};
    }
    if (i == C_DPAD) {  // (the d-pad where the stick is, larger than it, on the screen)
        Geo g = place_raw(l, C_STICK, w, h);
        float r = d.r * l.scale[C_STICK] * h;
        g.c.x = std::max(g.c.x, r * 1.08f), g.c.y = std::min(g.c.y, h - r * 1.08f);
        return {g.c, r};
    }
    if (i == C_SELECT) {  // (beside START, as on the 3DS)
        Geo g = place_raw(l, C_MENU, w, h);
        float r = d.r * l.scale[C_MENU] * h;
        return {ImVec2(g.c.x + (g.r + r) * 1.3f, g.c.y), r};
    }
    if (i == C_CORNER) {  // (c: the centre of its bend; a larger band stays against the edges)
        float r = d.r * l.scale[i] * h, in = r * (1 + BAND_T);
        return {ImVec2(w - l.x[i] * h - in, h - l.y[i] * h - in), r};
    }
    if (i == C_MPILL) return place_raw(l, C_ATTACK, w, h);  // (the thumb stays where the attacks are)
    if (i == C_FX || i == C_FY || i == C_ML || i == C_MR) {  // round the pill, where B and R are in a hunt, and tabs above it
        Geo g = place_raw(l, C_ATTACK, w, h);
        float k = l.scale[C_ATTACK], r = d.r * k * h;
        int n = i == C_FX ? 0 : i == C_FY ? 1 : i == C_ML ? 2 : 3;
        static const float ox[4] = {.155f, -.25f, -.215f, .16f}, oy[4] = {.15f, .175f, -.13f, -.25f};
        float mx = n >= 2 ? r * 1.8f : r * 1.1f, my = r * 1.1f;  // (a pill moved to an edge: they stay on the screen)
        return {ImVec2(std::clamp(g.c.x + ox[n] * k * h, mx, w - mx), std::clamp(g.c.y + oy[n] * k * h, my, h - my)), r};
    }
    return {ImVec2(d.right ? w - l.x[i] * h : l.x[i] * h, h - l.y[i] * h), d.r * l.scale[i] * h};
}

// the d-pad's direction under a finger, from its centre: one at a time (a menu's cursor goes one way: a thumb between
// two arms pressing both makes it jump), and the one held stays until the thumb is clearly nearer another
int dpad_part(Geo g, float x, float y, int held = 0) {
    float dx = x - g.c.x, dy = y - g.c.y, ax = fabsf(dx), ay = fabsf(dy);
    if (hypotf(dx, dy) < g.r * (held ? .12f : .17f)) return 0;  // (the middle: none)
    const float STICKY = 1.35f;
    bool across = held & 3 ? ax * STICKY >= ay : held & 12 ? ax >= ay * STICKY : ax >= ay;
    return across ? (dx > 0 ? 1 : 2) : (dy < 0 ? 4 : 8);  // (right, left; up, down)
}
uint64_t dpad_buttons(int part) {
    return (part & 1 ? BTN_RIGHT : 0) | (part & 2 ? BTN_LEFT : 0) | (part & 4 ? BTN_UP : 0) | (part & 8 ? BTN_DOWN : 0);
}

// The band's middle line: the point s along it (0 its left end, BAND_LEN * r its top), off it by e (> 0 toward the outside)
ImVec2 band_point(Geo g, float s, float e) {
    float r = g.r, left = BAND_LEFT * r, bend = PI * .5f * r;
    if (s <= left) return ImVec2(g.c.x - left + s, g.c.y + r + e);
    if (s >= left + bend) return ImVec2(g.c.x + r + e, g.c.y - (s - left - bend));
    float an = PI * .5f - (s - left) / r;
    return ImVec2(g.c.x + cosf(an) * (r + e), g.c.y + sinf(an) * (r + e));
}
// How far a point is from the middle line, and how far along it (ends included: their round caps)
float band_distance(Geo g, float x, float y, float *along) {
    float r = g.r, left = BAND_LEFT * r, bend = PI * .5f * r, up = BAND_UP * r, px = x - g.c.x, py = y - g.c.y, s, d;
    if (px <= 0 && py >= 0) {  // (the straight part along the bottom)
        float t = std::clamp(px + left, 0.0f, left);
        s = t, d = hypotf(px + left - t, py - r);
    } else if (px >= 0 && py <= 0) {  // (the one up the side)
        float t = std::clamp(-py, 0.0f, up);
        s = left + bend + t, d = hypotf(px - r, -py - t);
    } else if (px > 0 && py > 0) {  // (the bend)
        float an = atan2f(py, px);
        s = left + (PI * .5f - an) * r, d = fabsf(hypotf(px, py) - r);
    } else {  // (inside the band's open side: the nearer end of the two straight parts)
        float a = hypotf(px + std::min(left, -px), py - r), b = hypotf(px - r, py + std::min(up, -py));
        s = a < b ? left + std::max(px, -left) : left + bend + std::min(up, -py), d = std::min(a, b);
    }
    if (along) *along = s;
    return d;
}
int band_part(Geo g, float x, float y) {  // PILL_BUTTONS' index
    float s = 0;
    band_distance(g, x, y, &s);
    s /= g.r * BAND_LEN;
    return s < BAND_GUARD ? 3 : s < BAND_SEAM0 ? 1 : s < BAND_SEAM1 ? 2 : 0;
}

float pill_distance(Geo g, float x, float y) {  // from the pill's axis
    float half = g.r * PILL_HALF, px = x - g.c.x, py = y - g.c.y;
    float t = std::clamp(px * PILL_U.x + py * PILL_U.y, -half, half);
    float qx = px - PILL_U.x * t, qy = py - PILL_U.y * t;
    return sqrtf(qx * qx + qy * qy);
}
int pill_part(Geo g, float x, float y) {  // 0 the upper half (A), 1 the lower (X), 2 the seam (both)
    float t = ((x - g.c.x) * PILL_U.x + (y - g.c.y) * PILL_U.y) / (g.r * PILL_HALF);
    return t > PILL_SEAM ? 0 : t < -PILL_SEAM ? 1 : 2;
}

// the point between the separate attack buttons that presses both (not when they are moved far apart)
bool separate_seam(const Layout &l, float w, float h, ImVec2 *at, float *size) {
    Geo a = place(l, C_HEAVY, w, h), b = place(l, C_LIGHT, w, h);
    float gap = hypotf(a.c.x - b.c.x, a.c.y - b.c.y) - a.r - b.r;
    if (gap > a.r * 2) return false;
    *at = ImVec2((a.c.x + b.c.x) * .5f, (a.c.y + b.c.y) * .5f);
    *size = a.r * .3f;
    return true;
}

// ------------------------------------------------------------------ the game's screens in the window
struct Rect {
    float x0, y0, x1, y1;
    bool ok;
    bool in(float x, float y) const { return ok && x >= x0 && x <= x1 && y >= y0 && y <= y1; }
    bool near(ImVec2 p, float r) const {  // (a disc at p reaches into it)
        if (!ok) return false;
        float dx = std::max({x0 - p.x, 0.0f, p.x - x1}), dy = std::max({y0 - p.y, 0.0f, p.y - y1});
        return dx * dx + dy * dy < r * r;
    }
};
Rect g_bottom;       // the touch screen's corner while it shows (window pixels); else not ok
Rect g_bottom_seen;  // where it was when it last showed (the editor outlines it)
SDL_FRect g_top;     // the top screen's picture

void set_screens(const UiScreens &sc) {
    g_top = sc.top;
    g_bottom = {sc.bottom.x, sc.bottom.y, sc.bottom.x + sc.bottom.w, sc.bottom.y + sc.bottom.h,
                sc.bottom_shown && sc.bottom.w > 0 && sc.bottom.h > 0};
    if (g_bottom.ok) g_bottom_seen = g_bottom;
}

// A control under the touch screen's corner while it shows: not drawn, and a finger there is the game's
bool covered(const Layout &l, int i, float w, float h) {
    if (!g_bottom.ok) return false;
    Geo g = place(l, i, w, h);
    if (i == C_ATTACK || i == C_MPILL) {
        for (int k = -4; k <= 4; k++)
            if (g_bottom.near(add(g.c, PILL_U, g.r * PILL_HALF * k / 4.0f), g.r)) return true;
        return false;
    }
    if (i == C_CORNER) {
        for (int k = 0; k <= 24; k++)
            if (g_bottom.near(band_point(g, g.r * BAND_LEN * k / 24.0f, 0), g.r * BAND_T)) return true;
        return false;
    }
    if (is_tab(i)) {
        Rect t = {g.c.x - g.r * TAB_W, g.c.y - g.r * TAB_H, g.c.x + g.r * TAB_W, g.c.y + g.r * TAB_H, true};
        return t.x1 > g_bottom.x0 && t.x0 < g_bottom.x1 && t.y1 > g_bottom.y0 && t.y0 < g_bottom.y1;
    }
    return g_bottom.near(g.c, g.r);
}

// What a control's art takes on the screen (its badge and START's plate included)
Rect bounds(const Layout &l, int i, float w, float h) {
    Geo g = place(l, i, w, h);
    Rect b = {g.c.x - g.r, g.c.y - g.r, g.c.x + g.r, g.c.y + g.r, true};
    auto grow = [&](ImVec2 p, float r) {
        b.x0 = std::min(b.x0, p.x - r), b.y0 = std::min(b.y0, p.y - r), b.x1 = std::max(b.x1, p.x + r), b.y1 = std::max(b.y1, p.y + r);
    };
    if (i == C_ATTACK || i == C_MPILL) {
        grow(add(g.c, PILL_U, g.r * PILL_HALF), g.r), grow(add(g.c, PILL_U, -g.r * PILL_HALF), g.r);
    } else if (i == C_CORNER) {
        b = {g.c.x, g.c.y, g.c.x, g.c.y, true};
        for (int k = 0; k <= 24; k++) grow(band_point(g, g.r * BAND_LEN * k / 24.0f, 0), g.r * BAND_T);
    } else if (is_tab(i)) {
        b = {g.c.x - g.r * TAB_W, g.c.y - g.r * TAB_H, g.c.x + g.r * TAB_W, g.c.y + g.r * TAB_H, true};
    } else if (i == C_MENU || i == C_SELECT || i == C_HSELECT) {
        b.y1 = g.c.y + g.r * 1.45f;
    }
    return b;
}

// The item bar: where android_main.cpp says it is, or (not known) its usual place, the top screen's bottom-right
// corner (MH4U's HUD, with the port's hud_edges at the picture's edge); a little larger than its art, for a thumb
Rect item_zone(bool menu) {
    Rect r = {0, 0, 0, 0, false};
    if (menu || g_game.item_bar_shown == 0) return r;
    if (g_game.item_bar_shown == 1) {
        const SDL_FRect &b = g_game.item_bar;
        if (b.w <= 0 || b.h <= 0) return r;
        float m = b.h * .15f;
        r = {b.x - m, b.y - m, b.x + b.w + m, b.y + b.h + m, true};
    } else {
        if (g_top.w <= 0 || g_top.h <= 0) return r;
        float u = g_top.h, x1 = g_top.x + g_top.w - .008f * u, y1 = g_top.y + g_top.h - .008f * u;
        r = {x1 - .35f * u, y1 - .17f * u, x1, y1, true};
    }
    if (g_bottom.ok && r.x1 > g_bottom.x0 && r.x0 < g_bottom.x1 && r.y1 > g_bottom.y0 && r.y0 < g_bottom.y1)
        r.ok = false;  // (under the touch screen's corner: that is the game's)
    return r;
}

// The target camera's icon on the top screen (the single-screen mod puts it there, Port::Hooks' "target" part): a tap
// does what a tap on the touch screen's panel does, a long press turns the lock-on on and off. A little larger than its
// art, for a thumb
Rect target_zone(bool menu) {
    const UiGameState::HudPart &p = g_game.hud[HP_TARGET];
    if (menu || !p.shown || p.r.w <= 0 || p.r.h <= 0) return {0, 0, 0, 0, false};
    float m = p.r.h * .3f;
    Rect r = {p.r.x - m, p.r.y - m, p.r.x + p.r.w + m, p.r.y + p.r.h + m, true};
    if (g_bottom.ok && r.x1 > g_bottom.x0 && r.x0 < g_bottom.x1 && r.y1 > g_bottom.y0 && r.y0 < g_bottom.y1) r.ok = false;
    return r;
}

// the control under a finger (a little larger than its art: a thumb is wider), and the attack pill's part
int hit(const Layout &l, float x, float y, float w, float h, int *part, float slop = 1.22f, bool stick = false,
        bool menu = false, bool editing = false) {
    int best = -1;
    float bd = 1e30f;
    for (int i = 0; i < C_COUNT; i++) {
        if (!in_mode(i, menu) || (i == C_STICK && !stick)) continue;
        if (!editing && covered(l, i, w, h)) continue;
        Geo g = place(l, i, w, h);
        float d = i == C_ATTACK || i == C_MPILL ? pill_distance(g, x, y) / (g.r * (slop - .07f))
                : i == C_CORNER ? band_distance(g, x, y, nullptr) / (g.r * BAND_T * (slop - .07f))
                : is_tab(i) ? std::max(fabsf(x - g.c.x) / (g.r * TAB_W * slop), fabsf(y - g.c.y) / (g.r * TAB_H * slop))
                            : hypotf(x - g.c.x, y - g.c.y) / (g.r * slop);
        if (d < 1 && d < bd) bd = d, best = i;
    }
    if (best == C_ATTACK && part) *part = pill_part(place(l, C_ATTACK, w, h), x, y);
    if (best == C_MPILL && part) {  // (no seam: nothing presses both in a menu)
        Geo g = place(l, C_MPILL, w, h);
        *part = (x - g.c.x) * PILL_U.x + (y - g.c.y) * PILL_U.y > 0 ? 0 : 1;
    }
    if (best == C_CORNER && part) *part = band_part(place(l, C_CORNER, w, h), x, y);
    if (part && !menu && g_style == STYLE_SEPARATE && in_mode(C_LIGHT, false)) {  // (between the two attack buttons: both)
        ImVec2 at;
        float size;
        if (separate_seam(l, w, h, &at, &size) && hypotf(x - at.x, y - at.y) < size * 1.5f) best = C_LIGHT, *part = 1;
    }
    if (best >= 0 && is_dpad(best) && part) *part = dpad_part(place(l, best, w, h), x, y);
    return best;
}

// ------------------------------------------------------------------ fingers (the app's main thread)
enum Kind { F_NONE, F_BUTTON, F_STICK, F_CAMERA, F_MENU, F_ITEM, F_TARGET, F_EDIT };
struct Finger {
    SDL_FingerID id;
    Kind kind;
    int ctl, part;       // the control (F_BUTTON, F_EDIT), the pill's part
    float x, y, x0, y0;  // pixels: now, where it landed
    uint64_t t0;
    float swipe, swipe_y;  // the item bar, a menu's cursor: pixels swiped since the last step
    bool moved, done;
    uint64_t btn;          // a control of the ranged layout: what it pressed when it landed (the weapon may go out under it)
    uint64_t f0;           // the game's frame when it landed (g_frames)
    uint64_t rep;          // the item bar's arrows: when a held one turns the bar again
};
// The game reads the buttons once a frame of its own, and its frames can be long (3 to 8 fps on a Moto g06 at the
// start, for now): a press it is to see lasts a while and two of its frames (its frame counter, from the display:
// FrameCounter below), or 150 ms while none has come
uint64_t g_frames;      // the game's frame counter, at this frame of the app's
bool g_frames_fed;      // (one has come)
// The display's frames (one per VBlank, from the game thread) carry the game's own frame counter: a sink of ours
// beside the window's (android_main.cpp), added the same way, before the run
std::atomic<uint32_t> g_game_frame{0};
std::atomic<bool> g_game_frame_seen{false};
class FrameCounter final : public mh4n::present::ScreenSink {
public:
    void Publish(const mh4n::present::DisplayFrame &frame) override {
        g_game_frame.store(frame.game_frame, std::memory_order_relaxed);
        g_game_frame_seen.store(true, std::memory_order_release);
    }
    // (it never looks at the screens: any form suits it, the window's shared pictures included)
    bool WantsSharedPictures() const override { return true; }
};
FrameCounter g_frame_counter;
void AttachFrameCounter(mh4n::Runtime &rt) {
    if (auto *display = mh4n::present::FindDisplay(rt.Interfaces().gpu)) display->AddScreenSink(&g_frame_counter);
}
MH4N_REGISTER(mh4n::BootStep, "zz.android.touch", mh4n::BootStep{mh4n::Phase::Started, AttachFrameCounter});
// (the frame `f` has been shown; or the counter went back, a new run of it)
bool frames_ok(uint64_t f) { return !g_frames_fed || g_frames >= f || g_frames + 2 < f; }
// A press waits for the game's frames 1 s at most: through a long load with no frames it is let go all the same, not held
const uint64_t FRAME_WAIT = 1000 * MS;
uint64_t min_press() { return g_frames_fed ? 60 * MS : 150 * MS; }
// One press of its own: a tap held long enough, the item used, the camera centred
struct Pulse {
    uint64_t buttons, from, until;  // (time: held from .. until)
    uint64_t frame_until;           // (and until the game has shown this frame; 0: two frames after it begins)
    bool started;
};
// Presses one after another, each seen by the game and let go before the next: the item bar's turn (L held with Y or
// A), the ammo's (L held with X or B), a menu's cursor moved by a drag
struct Seq {
    uint64_t hold;                    // held from the start to the end (L), or 0
    uint64_t lead, press, gap, tail;  // the least time of each phase: the hold alone first, a press, the gap after
                                      // it, the hold kept after the last one (each two of the game's frames at least)
    uint64_t q[6];                    // the presses waiting, the first one the current one
    int n;
    int phase;                        // 0 none, 1 the lead, 2 a press, 3 a gap, 4 the tail
    uint64_t t, f;                    // when the phase began (SDL_GetTicksNS, g_frames)
};
void seq_phase(Seq &q, int phase, uint64_t now) { q.phase = phase, q.t = now, q.f = g_frames; }
void seq_add(Seq &q, uint64_t buttons, uint64_t now) {
    if (q.n >= 6) return;
    q.q[q.n++] = buttons;
    if (q.phase == 0) seq_phase(q, q.hold && q.lead ? 1 : 2, now);
    else if (q.phase == 4) seq_phase(q, 2, now);  // (the gap is behind it)
}
uint64_t seq_buttons(Seq &q, uint64_t now) {  // (each frame: what it holds now)
    auto done = [&](uint64_t ns) { return now - q.t >= ns && (frames_ok(q.f + 2) || now - q.t >= FRAME_WAIT); };
    for (int guard = 0; guard < 8; guard++) {
        if (q.phase == 1 && done(q.lead)) seq_phase(q, 2, now);
        else if (q.phase == 2 && done(std::max(q.press, min_press()))) {
            for (int i = 1; i < q.n; i++) q.q[i - 1] = q.q[i];
            q.n--;
            seq_phase(q, 3, now);
        } else if (q.phase == 3 && done(q.gap)) seq_phase(q, q.n ? 2 : q.hold ? 4 : 0, now);
        else if (q.phase == 4 && done(q.tail)) seq_phase(q, 0, now);
        else break;
    }
    return q.phase ? q.hold | (q.phase == 2 && q.n ? q.q[0] : 0) : 0;
}
const uint64_t BAR_OPEN = 1ull << 40;  // (Seq::hold for the bar held open by the hooks: no button of the game's)
enum { E_SMALLER, E_BIGGER, E_SET, E_STYLE, E_RESET, E_CANCEL, E_SAVE, E_COUNT };
struct State {
    Finger f[10];
    // the editor: each part of the game's HUD where the player puts it (Port::Hooks' hud_<key>, the top screen's
    // pixels; the game shows it there while it is dragged), and where it was when the editor opened (Cancel)
    float hud_at[UiGameState::HUD_PARTS][2], hud_open[UiGameState::HUD_PARTS][2];
    float hud_drag_rest[2];  // (a drag's fraction of a pixel not given to the game yet)
    bool stick_on;
    float bx, by, kx, ky;    // the stick's base and knob, pixels
    float cam_dx, cam_dy;    // pixels dragged since the last frame
    uint64_t held;           // buttons held by fingers
    uint64_t last_ns;        // the last touch
    uint64_t tap_ns;         // the last short tap on the camera's part (a second one soon after centres it)
    uint64_t step_ns;        // when a drag may move a menu's cursor again
    float tap_x, tap_y;
    Pulse pulse[8];
    bool run;                // the thumb is past the stick's rim: dashing (R held)
    float fx, fy;            // the stick's thumb where it is (the knob stops at the rim)
    uint64_t item_flash;
    int item_dir;            // the bar's last turn (-1, 1), 0 the item used
    uint64_t use_until, use_at;  // the item to use once the weapon is away (item_use): until when, and when it went
    int use_action;              // away, with the hunter's action then (putting the weapon away)
    SDL_FingerID bar_finger;     // the finger whose long press holds the bar open
    bool bar_open;           // a long press holds the bar open (Port::Hooks::HoldItemBarOpen)
    bool bar_held;           // the hooks hold it open now (the long press, or a step of the ammo: frame_input)
    uint64_t target_flash;   // the target camera's icon tapped (its answer)
    uint64_t target_check;   // when to see whether the tap left a monster on the target camera (the lock follows)
    int stick_part;          // in a menu: the direction the stick is pressing (dpad_part)
    Seq item, ammo, cursor;  // the item bar's steps, the ammo's, a menu cursor's
    Seq ammo_open;           // the ammo's with the bar held open by the hooks (ammo_step)
    float cs_x, cs_y;        // the C-stick's push for a drag (where the camera hook isn't there), -1..1
    uint64_t cs_until, frame_ns;
    float w, h;              // the screen, pixels
    Layout layout, saved;
    bool loaded;
    bool menu;               // the menu layout is shown (at the last frame)
    bool visible, gear_only;
    bool edit;               // the layout editor
    int sel;
    bool pinch;
    float pinch_d0, pinch_s0;
};
State g_s;
SDL_Renderer *g_renderer;
bool g_bottom_touch = true;  // the touch screen's corner wanted while the touch controls show (MH4U's main menu is on it;
bool g_bottom_pad = true;    // the controls under it move aside, update_shift), and while they are hidden
// The steps' times. The item bar: L a moment alone, then Y or A. The ammo: the game takes the press only a while after L
// (the MHGU port measured 220 to 310 ms there), and L let go confirms it. The cursor: a press, and a gap the game sees
void seq_setup(State &s) {
    s.item.hold = BTN_L, s.item.lead = 100 * MS, s.item.press = 80 * MS, s.item.gap = 80 * MS, s.item.tail = 100 * MS;
    s.ammo.hold = BTN_L, s.ammo.lead = 380 * MS, s.ammo.press = 80 * MS, s.ammo.gap = 90 * MS, s.ammo.tail = 150 * MS;
    // (the bar held open by the hooks instead of L: a bit no button has stands for the hold, frame_input opens the bar)
    s.ammo_open.hold = BAR_OPEN, s.ammo_open.lead = 120 * MS, s.ammo_open.press = 80 * MS, s.ammo_open.gap = 90 * MS,
    s.ammo_open.tail = 150 * MS;
    s.cursor.hold = 0, s.cursor.lead = 0, s.cursor.press = 70 * MS, s.cursor.gap = 70 * MS, s.cursor.tail = 0;
}

void ensure_loaded(State &s) {
    if (s.loaded) return;
    s.loaded = true, s.sel = -1;
    seq_setup(s);
    bool had = load_layout(&s.layout);
    s.saved = s.layout;
    g_style = s.layout.style;
    fprintf(stderr, "[touch] layout: %s\n", had ? g_layout_path.c_str() : "the defaults");
}

bool pulse_busy(const Pulse &p, uint64_t now) {
    return now < p.from || now < p.until || (p.started && !frames_ok(p.frame_until) && now - p.from < FRAME_WAIT);
}
// A press of its own, held `ns` (at least min_press) and two of the game's frames; `frame_until` when it began earlier
// (a finger that already held it)
void pulse(State &s, uint64_t buttons, uint64_t ns, uint64_t frame_until = 0) {
    uint64_t now = SDL_GetTicksNS();
    for (Pulse &p : s.pulse)
        if (!pulse_busy(p, now)) {
            p.buttons = buttons, p.from = now, p.until = now + ns, p.frame_until = frame_until, p.started = frame_until != 0;
            return;
        }
}
uint64_t pulse_buttons(State &s, uint64_t now) {
    uint64_t b = 0;
    for (Pulse &p : s.pulse) {
        if (now < p.from || !pulse_busy(p, now)) continue;
        if (!p.started) p.started = true, p.frame_until = g_frames + 2;
        b |= p.buttons;
    }
    return b;
}
// A step of the ammo or the coating: X the next one, B the one before, while the bar is open (the game's own: L held,
// and L let go confirms it). Where the hooks know the game they hold the bar open themselves meanwhile, as L does
// (Port::Hooks::HoldItemBarOpen: the camera isn't turned behind the hunter at each step); else L is held
bool item_direct();
void ammo_step(State &s, int dir) {
    uint64_t now = SDL_GetTicksNS();
    seq_add(item_direct() ? s.ammo_open : s.ammo, dir > 0 ? BTN_X : BTN_B, now);
}
// The hooks turn the item bar themselves (Port::Hooks::ScrollItemBar: the game's own step, as the touch screen's swipe on
// the bar takes it) where the game is known: in a quest, the bar on the screen
bool item_direct() { return g_game.place == 2 && g_game.item_bar_shown == 1; }
// A step of the item bar: the hooks' (no L: L would turn the camera behind the hunter at each step), else the game's
// buttons (L held, A the next item, Y the one before: "hold L, then Y or A")
void item_step(State &s, int dir) {
    uint64_t now = SDL_GetTicksNS();
    if (item_direct()) Port::Hooks::ScrollItemBar(dir);
    else seq_add(s.item, dir > 0 ? BTN_A : BTN_Y, now);
    s.item_dir = -dir, s.item_flash = now + 200 * MS;
}
// A swipe on the bar a step's length to the right (1) or the left (-1): the strip follows the finger, as on the touch
// screen's own item bar (a swipe to the right brings the item on the left: Y's way)
void item_swipe(State &s, int finger) { item_step(s, -finger); }
// A tap on the bar uses the item (Y). With the weapon out Y puts it away first, and the item is used once it is away
// (Y again, a moment after the game says the weapon is sheathed; given up after a while, or when another control is
// pressed meanwhile)
const uint64_t USE_WAIT = 2500 * MS, USE_AFTER = 60 * MS, BAR_HOLD = 400 * MS;
// The bar's long press over
void bar_close(State &s) { s.bar_open = false; }
void item_use(State &s) {
    uint64_t now = SDL_GetTicksNS();
    pulse(s, BTN_Y, min_press());
    s.use_until = g_game.weapon_drawn == 1 ? now + USE_WAIT : 0, s.use_at = 0;
    s.item_flash = now + 200 * MS, s.item_dir = 0;
}

// The stick is fixed at its place (the layout's: the editor moves it); a thumb on its base or a little past its rim
// takes it, not on the item bar
bool in_stick_zone(const State &s, float x, float y, const Rect &item) {
    Geo g = place(s.layout, C_STICK, s.w, s.h);
    if (item.in(x, y)) return false;
    return hypotf(x - g.c.x, y - g.c.y) < std::max(1.45f * g.r, .17f * s.h);
}

void stick_to(State &s, float x, float y) {  // (the knob under the thumb, kept inside the rim)
    s.fx = x, s.fy = y;
    float dx = x - s.bx, dy = y - s.by, d = sqrtf(dx * dx + dy * dy);
    float r = place(s.layout, C_STICK, s.w, s.h).r;
    if (d > r) x = s.bx + dx / d * r, y = s.by + dy / d * r;
    s.kx = x, s.ky = y;
}

Finger *finger(State &s, SDL_FingerID id) {
    for (Finger &f : s.f)
        if (f.kind != F_NONE && f.id == id) return &f;
    return nullptr;
}

uint64_t finger_buttons(const Finger &f) {
    return f.ctl == C_ATTACK || f.ctl == C_CORNER ? PILL_BUTTONS[f.part]
         : f.ctl == C_MPILL ? (f.part ? BTN_B : BTN_A)
         : is_dpad(f.ctl) ? dpad_buttons(f.part)
         : f.ctl == C_LIGHT && f.part ? PILL_BUTTONS[2]
         : is_combo(f.ctl) ? g_combo[f.ctl - C_COMBO]
         : f.btn ? f.btn
                 : DEFS[f.ctl].buttons;
}

void recompute_held(State &s) {
    s.held = 0;
    for (Finger &f : s.f)
        if (f.kind == F_BUTTON && !f.done) s.held |= finger_buttons(f);
}

// ---- the layout editor: a panel in the middle (smaller, bigger, the ranged layout, the attacks' style, defaults,
// cancel, save); drag a control to move it, pinch (a second finger anywhere) or the panel's - / + to size it
void editor_panel(float w, float h, Rect *panel, Rect btn[E_COUNT]) {
    // (between the left edge's column and the right thumb's controls, where it hides none of them, on a wide screen)
    float pw = std::clamp(w - 1.04f * h, std::min(w * .9f, 1.1f * h), std::min(w * .9f, 1.8f * h)), ph = .2f * h;
    float x0 = (w - pw) * .5f, y0 = (h - ph) * .4f;
    *panel = {x0, y0, x0 + pw, y0 + ph, true};
    float gap = .015f * h, bw = (pw - gap * (float)(E_COUNT + 1)) / (float)E_COUNT, by0 = y0 + ph * .5f, by1 = y0 + ph - gap;
    for (int i = 0; i < E_COUNT; i++) btn[i] = {x0 + gap + i * (bw + gap), by0, x0 + gap + i * (bw + gap) + bw, by1, true};
}

void hud_load(State &s);
void edit_begin(State &s) {
    s.edit = true, s.sel = -1, s.pinch = false;
    g_town_edit = true;
    hud_load(s);
    g_edit_set = g_ranged != 0;  // (the layout in use; the panel's button changes it)
    s.saved = s.layout;
    for (Finger &f : s.f) f.kind = F_NONE;
    s.stick_on = false, s.held = 0;
    fprintf(stderr, "[touch] layout editor\n");
}
// ---- the game's HUD in the editor: its parts drag as a whole, each where the player wants it (Port::Hooks moves the
// game's own layout objects: hud_<key> in mh4u.ini, live while dragged), and stay there in every hunt
// The part where the editor shows it: its art at the game's own place (where the hooks say it is, less the offset it is
// drawn with) moved by the offset the editor has for it now (the game may show the new one a frame later)
Rect hud_rect(const State &s, int k) {
    const UiGameState::HudPart &p = g_game.hud[k];
    if (p.r.w <= 0 || p.r.h <= 0 || g_top.w <= 0 || g_top.h <= 0) return {0, 0, 0, 0, false};
    float sx = g_top.w / (float)std::max(g_game.top_width, 1), sy = g_top.h / 240.0f;
    float dx = s.edit ? (s.hud_at[k][0] - p.dx) * sx : 0, dy = s.edit ? (s.hud_at[k][1] - p.dy) * sy : 0;
    return {p.r.x + dx, p.r.y + dy, p.r.x + p.r.w + dx, p.r.y + p.r.h + dy, true};
}
// The parts the editor offers: those on the screen now or since the game started, and outside a hunt (where the hooks
// know the game) those every hunt has, and in a quest the area's map (its usual place until it is drawn: the single-screen
// mod draws it on the top screen only with the map among the touch screen's panels, and the Arena has none); not the
// target camera (the single-screen mod's) until it is seen
bool hud_offered(int k) {
    const UiGameState::HudPart &p = g_game.hud[k];
    return p.shown || p.seen || (g_game.place >= 0 && k != HP_TARGET && (k != HP_MAP || g_game.place == 2));
}
std::string hud_value(const float *v) {
    char t[48];
    snprintf(t, sizeof t, "%.0f %.0f", v[0], v[1]);
    return t;
}
void hud_preview(State &s, int k) { game_option_preview(std::string("hud_") + HUD_KEY[k], hud_value(s.hud_at[k])); }
// A part dragged by (dx, dy) window pixels: in the top screen's pixels, kept on the top screen's picture
void hud_drag(State &s, int k, float dx, float dy) {
    Rect r = hud_rect(s, k);
    if (!r.ok) return;
    float sx = g_top.w / (float)std::max(g_game.top_width, 1), sy = g_top.h / 240.0f;
    dx = std::max(g_top.x - r.x0, std::min(g_top.x + g_top.w - r.x1, dx));
    dy = std::max(g_top.y - r.y0, std::min(g_top.y + g_top.h - r.y1, dy));
    float before[2] = {s.hud_at[k][0], s.hud_at[k][1]};
    s.hud_drag_rest[0] += dx / sx, s.hud_drag_rest[1] += dy / sy;  // (whole pixels to the game; the rest kept)
    for (int a = 0; a < 2; a++) {
        float whole = truncf(s.hud_drag_rest[a]);
        s.hud_at[k][a] = std::clamp(s.hud_at[k][a] + whole, a ? -240.0f : -400.0f, a ? 240.0f : 400.0f);
        s.hud_drag_rest[a] -= whole;
    }
    if (before[0] != s.hud_at[k][0] || before[1] != s.hud_at[k][1]) hud_preview(s, k);
}
void hud_load(State &s) {
    for (int k = 0; k < UiGameState::HUD_PARTS; k++) {
        std::string v = game_option_get(std::string("hud_") + HUD_KEY[k], k == HP_ITEMS ? game_option_get("item_bar", "0 0") : "0 0");
        float x = 0, y = 0;
        sscanf(v.c_str(), "%f %f", &x, &y);
        s.hud_at[k][0] = s.hud_open[k][0] = x, s.hud_at[k][1] = s.hud_open[k][1] = y;
    }
}
// Save: each part moved to mh4u.ini (the item bar's under its older key too, which an earlier version wrote); Cancel:
// each back where it was
void hud_end(State &s, bool keep) {
    for (int k = 0; k < UiGameState::HUD_PARTS; k++) {
        if (s.hud_at[k][0] == s.hud_open[k][0] && s.hud_at[k][1] == s.hud_open[k][1]) continue;
        if (!keep) {
            game_option_preview(std::string("hud_") + HUD_KEY[k], hud_value(s.hud_open[k]));
            continue;
        }
        if (k == HP_ITEMS) game_option_set("item_bar", hud_value(s.hud_at[k]));
        game_option_set(std::string("hud_") + HUD_KEY[k], hud_value(s.hud_at[k]));
        fprintf(stderr, "[touch] HUD: %s moved to %s\n", HUD_KEY[k], hud_value(s.hud_at[k]).c_str());
    }
}

void edit_end(State &s, bool keep) {
    hud_end(s, keep);
    if (keep) save_layout(s.layout), s.saved = s.layout;
    else s.layout = s.saved;
    g_style = s.layout.style;
    g_edit_set = -1;
    s.edit = false, s.sel = -1, s.pinch = false;
    g_town_edit = false;
}
void clamp_place(State &s, int i) {
    float wmax = s.w / s.h;
    s.layout.x[i] = std::clamp(s.layout.x[i], .03f, wmax - .03f);
    s.layout.y[i] = std::clamp(s.layout.y[i], .03f, .97f);
    s.layout.scale[i] = std::clamp(s.layout.scale[i], i == C_CORNER ? .7f : .5f, i == C_CORNER ? 1.3f : 2.2f);
}
// The next way of laying out the attacks. What the corner band would cover goes inside its bend; leaving it, back to
// where it was when the editor opened
void next_style(State &s) {
    int style = (s.layout.style + 1) % STYLE_COUNT;
    s.layout.style = style, g_style = style;
    static const struct { int c; float dx, dy; } MOVED[2] = {{C_DODGE, .3f, .3f}, {C_SHEATHE, -.2f, -.2f}};
    for (const auto &m : MOVED) {
        Layout before = s.saved.style == STYLE_CORNER ? defaults() : s.saved;
        s.layout.x[m.c] = before.x[m.c], s.layout.y[m.c] = before.y[m.c];
        if (style != STYLE_CORNER) continue;
        Geo band = place(s.layout, C_CORNER, s.w, s.h), o = place(s.layout, m.c, s.w, s.h);
        if (band_distance(band, o.c.x, o.c.y, nullptr) < band.r * BAND_T + o.r) {  // (in the band's hollow)
            ImVec2 at(band.c.x + m.dx * band.r, band.c.y + m.dy * band.r);
            s.layout.x[m.c] = (s.w - at.x) / s.h, s.layout.y[m.c] = (s.h - at.y) / s.h;
            clamp_place(s, m.c);
        }
    }
    // ZR, ZL and L, on their arc above the attacks: where the band's upright part reaches them, all three in a row
    // leading left from its top, in their order (ZR nearest the edge, as on the 3DS)
    static const int ROW[3] = {C_ZR, C_ZL, C_L};
    Layout before = s.saved.style == STYLE_CORNER ? defaults() : s.saved;
    bool reached = false;
    for (int c : ROW) {
        s.layout.x[c] = before.x[c], s.layout.y[c] = before.y[c];
        if (style != STYLE_CORNER) continue;
        Geo band = place(s.layout, C_CORNER, s.w, s.h), o = place(s.layout, c, s.w, s.h);
        reached |= band_distance(band, o.c.x, o.c.y, nullptr) < band.r * BAND_T + o.r * 1.1f;
    }
    if (reached) {
        Geo band = place(s.layout, C_CORNER, s.w, s.h);
        ImVec2 top = band_point(band, band.r * BAND_LEN, 0);
        float x = top.x - band.r * BAND_T;
        for (int c : ROW) {
            float r = DEFS[c].r * s.layout.scale[c] * s.h;
            x -= r * 1.25f;
            s.layout.x[c] = (s.w - x) / s.h, s.layout.y[c] = (s.h - top.y) / s.h;
            clamp_place(s, c);
            x -= r * 1.25f;
        }
    }
    if (s.sel >= 0 && s.sel < C_COUNT && !in_mode(s.sel, false)) s.sel = -1;
}

void edit_event(State &s, const SDL_Event &e, float x, float y) {
    if (e.type == SDL_EVENT_FINGER_DOWN) {
        Finger *f = nullptr;
        for (Finger &g : s.f)
            if (g.kind == F_NONE) { f = &g; break; }
        if (!f) return;
        *f = {};
        f->id = e.tfinger.fingerID, f->x = f->x0 = x, f->y = f->y0 = y, f->ctl = -1;
        Rect panel, btn[E_COUNT];
        editor_panel(s.w, s.h, &panel, btn);
        for (int i = 0; i < E_COUNT; i++)
            if (btn[i].in(x, y)) {
                f->kind = F_MENU;  // (held until it lifts: nothing under it moves)
                haptics_tap();
                if (i == E_SMALLER || i == E_BIGGER) {
                    if (s.sel >= 0 && s.sel < C_COUNT) s.layout.scale[s.sel] += i == E_BIGGER ? .1f : -.1f, clamp_place(s, s.sel);
                } else if (i == E_SET) {
                    g_edit_set = g_edit_set == 1 ? 0 : 1, s.sel = -1;
                } else if (i == E_STYLE) {
                    if (!ranged_layout()) next_style(s);
                } else if (i == E_RESET) {
                    s.layout = defaults();
                    g_style = s.layout.style;
                    for (int k = 0; k < UiGameState::HUD_PARTS; k++)  // (the HUD's parts back at the game's places)
                        if (s.hud_at[k][0] != 0 || s.hud_at[k][1] != 0) s.hud_at[k][0] = s.hud_at[k][1] = 0, hud_preview(s, k);
                } else {
                    edit_end(s, i == E_SAVE);
                    f->kind = F_NONE;
                }
                return;
            }
        Finger *drag = nullptr;
        for (Finger &g : s.f)
            if (&g != f && g.kind == F_EDIT && g.ctl >= 0) drag = &g;
        if (drag && drag->ctl >= ZONE_SEL) drag = nullptr;  // (a part of the HUD has no size)
        if (drag) {  // a second finger: pinch the control being dragged
            s.pinch = true, s.pinch_d0 = std::max(1.0f, hypotf(x - drag->x, y - drag->y)), s.pinch_s0 = s.layout.scale[drag->ctl];
            f->kind = F_EDIT;
            return;
        }
        int c = hit(s.layout, x, y, s.w, s.h, nullptr, 1.5f, true, false, true);
        if (c >= C_LAID) c = -1;
        if (c < 0 && panel.in(x, y)) { f->kind = F_MENU; return; }
        if (c < 0) {  // (a part of the game's HUD: dragged as a whole; the smallest under the finger, a little larger)
            float best = 1e30f, m = .012f * s.h;
            for (int k = 0; k < UiGameState::HUD_PARTS; k++) {
                Rect r = hud_rect(s, k);
                if (!hud_offered(k) || !r.ok || x < r.x0 - m || x > r.x1 + m || y < r.y0 - m || y > r.y1 + m) continue;
                float area = (r.x1 - r.x0) * (r.y1 - r.y0);
                if (area < best) best = area, c = ZONE_SEL + k;
            }
            if (c >= ZONE_SEL) {
                f->kind = F_EDIT, f->ctl = c, s.sel = c, s.hud_drag_rest[0] = s.hud_drag_rest[1] = 0;
                return;
            }
        }
        f->kind = F_EDIT, f->ctl = c;
        s.sel = c;
        return;
    }
    Finger *f = finger(s, e.tfinger.fingerID);
    if (!f) return;
    if (e.type == SDL_EVENT_FINGER_MOTION) {
        float dx = x - f->x, dy = y - f->y;
        f->x = x, f->y = y;
        if (f->kind != F_EDIT) return;
        if (s.pinch) {
            Finger *a = nullptr, *b = nullptr;
            for (Finger &g : s.f)
                if (g.kind == F_EDIT) (a ? b : a) = &g;
            int c = a && a->ctl >= 0 ? a->ctl : b && b->ctl >= 0 ? b->ctl : -1;
            if (a && b && c >= 0 && c < C_COUNT) s.layout.scale[c] = s.pinch_s0 * hypotf(a->x - b->x, a->y - b->y) / s.pinch_d0, clamp_place(s, c);
        } else if (f->ctl >= ZONE_SEL) {
            hud_drag(s, f->ctl - ZONE_SEL, dx, dy);
        } else if (f->ctl >= 0) {
            s.layout.x[f->ctl] += (DEFS[f->ctl].right ? -dx : dx) / s.h;
            s.layout.y[f->ctl] -= dy / s.h;
            clamp_place(s, f->ctl);
        }
        return;
    }
    if (f->kind == F_EDIT) s.pinch = false;
    f->kind = F_NONE;
}

// g_fit for this frame: each side's controls of the layout in use, when the touch screen's corner covers one of them,
// move as one above or below it (the menus' few fit as they are), smaller when that is what it takes to fit there (the
// hunt's, above a corner at the bottom: they keep to their edge), else beside it
void update_shift(const State &s) {
    g_fit[0] = g_fit[1] = Fit{};
    if (!g_bottom.ok || s.edit || s.w <= 0 || s.h <= 0) return;
    float m = .025f * s.h, edge = m * .4f;
    for (int side = 0; side < 2; side++) {
        Rect box = {1e30f, 1e30f, -1e30f, -1e30f, true};
        bool under = false;
        for (int i = 0; i < C_COUNT; i++) {
            if (DEFS[i].right != (side == 1) || !in_mode(i, s.menu)) continue;
            if (!s.visible && i != C_SETTINGS) continue;
            if (covered(s.layout, i, s.w, s.h)) under = true;
            Rect b = bounds(s.layout, i, s.w, s.h);
            box.x0 = std::min(box.x0, b.x0), box.y0 = std::min(box.y0, b.y0), box.x1 = std::max(box.x1, b.x1), box.y1 = std::max(box.y1, b.y1);
        }
        if (!under) continue;
        Fit &f = g_fit[side];
        float hgt = std::max(1.0f, box.y1 - box.y0), ax = side ? box.x1 : box.x0;
        float above = g_bottom.y0 - m - edge, below = s.h - edge - (g_bottom.y1 + m);  // (the room each way)
        float up = box.y1 - (g_bottom.y0 - m), down = (g_bottom.y1 + m) - box.y0;
        if (box.y0 - up >= edge) f.offset = ImVec2(0, -up);
        else if (box.y1 + down <= s.h - edge) f.offset = ImVec2(0, down);
        else if (std::max(above, below) / hgt >= MIN_FIT) {
            f.scale = std::max(above, below) / hgt;
            if (above >= below) f.anchor = ImVec2(ax, box.y0), f.offset = ImVec2(0, edge - box.y0);  // (from the top edge down)
            else f.anchor = ImVec2(ax, box.y1), f.offset = ImVec2(0, s.h - edge - box.y1);
        } else {
            f.offset = ImVec2(side ? -(box.x1 - g_bottom.x0 + m) : g_bottom.x1 - box.x0 + m, 0);
        }
    }
}

void window_size(State &s) {  // (the fingers' pixels: the window's, as android_main.cpp's are)
    if (s.w > 0 && s.h > 0) return;
    int w = 0, h = 0;
    if (g_renderer) SDL_GetRenderOutputSize(g_renderer, &w, &h);
    s.w = (float)w, s.h = (float)h;
}

bool finger_event(const SDL_Event &e) {
    State &s = g_s;
    ensure_loaded(s);
    window_size(s);
    if (s.w <= 0 || s.h <= 0) return false;
    uint64_t now = SDL_GetTicksNS();
    float x = e.tfinger.x * s.w, y = e.tfinger.y * s.h;
    if (s.edit) {
        s.last_ns = now;
        edit_event(s, e, x, y);
        return true;
    }
    if (e.type == SDL_EVENT_FINGER_DOWN) {
        if (g_bottom.in(x, y)) return false;  // (the touch screen's corner: the game's stylus, android_main.cpp)
        Finger *f = nullptr;
        for (Finger &g : s.f)
            if (g.kind == F_NONE) { f = &g; break; }
        if (!f) return true;
        s.last_ns = now;
        *f = {};
        f->id = e.tfinger.fingerID, f->x = f->x0 = x, f->y = f->y0 = y, f->t0 = now, f->f0 = g_frames;
        if (!s.visible) {
            int part = 0;
            if (s.gear_only && hit(s.layout, x, y, s.w, s.h, &part, 1.22f, false, s.menu) == C_SETTINGS) {
                f->kind = F_BUTTON, f->ctl = C_SETTINGS, f->part = part;  // (a tap opens the settings, as when they show)
                haptics_tap();
                return true;
            }
            f->kind = F_CAMERA;  // (hidden: a drag still turns the camera)
            return true;
        }
        Rect item = item_zone(s.menu);
        // the padlock by L ("Lock camera"): a tap turns the lock-on on and off (the hooks keep the one state)
        if (!s.menu && lock_on_allowed() && in_mode(C_L, false) && !covered(s.layout, C_L, s.w, s.h)) {
            Geo g = place(s.layout, C_L, s.w, s.h);
            ImVec2 pc(g.c.x - g.r * .72f, g.c.y - g.r * .62f);
            if (hypotf(x - pc.x, y - pc.y) < std::max(g.r * .42f, .018f * s.h) * 1.5f) {
                f->kind = F_NONE;  // (the finger is no control's: its moves and lift go nowhere)
                lock_toggle("the padlock");
                haptics_tap();
                return true;
            }
        }
        int part = 0, c = hit(s.layout, x, y, s.w, s.h, &part, 1.22f, false, s.menu);
        if ((c == C_R || c == C_DODGE || c == C_CORNER || c == C_RDODGE || c == C_RSECOND || c == C_AMMO_UP ||
             c == C_AMMO_DOWN) && item.in(x, y))
            c = -1;  // (narrow screens: their places reach the item bar)
        if (c >= 0) {
            f->kind = F_BUTTON, f->ctl = c, f->part = part;
            s.use_until = s.use_at = 0;  // (another control: the item waiting for the weapon to go away is given up)
            f->btn = is_ranged(c) ? ranged_buttons(c, g_game.weapon_drawn) : 0;
            if (c == C_AMMO_UP || c == C_AMMO_DOWN) ammo_step(s, c == C_AMMO_UP ? 1 : -1);
            if (c == C_SCREEN) touch_toggle_bottom();
            if (c == C_TOWN) town_open(false);
            if (c == C_ITEML || c == C_ITEMR) item_swipe(s, c == C_ITEMR ? 1 : -1), f->rep = now + 420 * MS;
            if (c == C_LAYOUT) {
                g_menu_layout = !g_menu_layout;
                fprintf(stderr, "[touch] the %s layout (by hand)\n", g_menu_layout ? "menu" : "hunt");
            }
            haptics_tap();
        } else if (item.in(x, y)) {
            f->kind = F_ITEM;
        } else if (!s.stick_on && in_mode(C_STICK, s.menu) && !covered(s.layout, C_STICK, s.w, s.h) &&
                   in_stick_zone(s, x, y, item)) {
            f->kind = F_STICK;
            Geo g = place(s.layout, C_STICK, s.w, s.h);
            s.stick_on = true, s.bx = g.c.x, s.by = g.c.y;
            stick_to(s, x, y);
        } else {
            f->kind = F_CAMERA;
        }
        recompute_held(s);
        if (g_set.debug)
            fprintf(stderr, "[touch] finger %.0f,%.0f of %.0fx%.0f: kind %d control %d part %d, buttons %llx; item bar %d\n",
                    x, y, s.w, s.h, f->kind, f->ctl, f->part, (unsigned long long)s.held, item.ok);
        return true;
    }
    Finger *f = finger(s, e.tfinger.fingerID);
    if (!f) return false;  // (not one of the controls': the touch screen's, android_main.cpp's)
    s.last_ns = now;
    if (f->kind == F_MENU) {
        if (e.type != SDL_EVENT_FINGER_MOTION) f->kind = F_NONE;
        return true;
    }
    if (e.type == SDL_EVENT_FINGER_MOTION) {
        float dx = x - f->x, dy = y - f->y;
        if (hypotf(x - f->x0, y - f->y0) > .025f * s.h) f->moved = true;
        if (f->kind == F_CAMERA && s.menu && s.visible) {  // a drag moves the cursor, following the finger, a step every so far
            f->swipe += dx, f->swipe_y += dy;
            float ax = fabsf(f->swipe), ay = fabsf(f->swipe_y), step = .075f * s.h;
            if (now >= s.step_ns && s.cursor.n < 2 && std::max(ax, ay) >= step) {  // (a step or two ahead at most)
                bool across = ax > ay;
                seq_add(s.cursor, across ? (f->swipe > 0 ? BTN_RIGHT : BTN_LEFT) : (f->swipe_y > 0 ? BTN_DOWN : BTN_UP), now);
                s.step_ns = now + 120 * MS;  // (each press and the gap after it are the game's to see: s.cursor)
                f->swipe = across ? f->swipe - copysignf(step, f->swipe) : 0;
                f->swipe_y = across ? 0 : f->swipe_y - copysignf(step, f->swipe_y);
                haptics_tap();
            }
        } else if (f->kind == F_CAMERA || (f->kind == F_BUTTON && f->ctl == C_RFIRE && f->moved && !f->done)) {
            s.cam_dx += dx, s.cam_dy += dy;  // (a drag on the shot aims while it is held)
        } else if (f->kind == F_STICK) {
            stick_to(s, x, y);
        } else if (f->kind == F_ITEM) {  // a swipe turns the bar, an item every so far; up or down with a gun: the ammo
            f->swipe += dx, f->swipe_y += dy;
            float step = .055f * s.h;
            if (g_ranged && !f->done && fabsf(f->swipe_y) >= step * 1.3f && fabsf(f->swipe_y) > fabsf(f->swipe)) {
                ammo_step(s, f->swipe_y < 0 ? 1 : -1), haptics_tap();
                f->done = true;  // (one step a swipe: each takes a moment, and two would look like none)
            }
            while (!f->done && f->swipe >= step) item_swipe(s, 1), f->swipe -= step, haptics_tap();
            while (!f->done && f->swipe <= -step) item_swipe(s, -1), f->swipe += step, haptics_tap();
        } else if (f->kind == F_BUTTON && is_dpad(f->ctl)) {  // the d-pad: the direction the thumb is in now
            int part = dpad_part(place(s.layout, f->ctl, s.w, s.h), x, y, f->part);
            if (part != f->part) f->part = part, recompute_held(s), haptics_tap();
        } else if (f->kind == F_BUTTON && (f->ctl == C_ATTACK || f->ctl == C_CORNER)) {  // sliding along it: another part
            int part = 0, c = hit(s.layout, x, y, s.w, s.h, &part);
            if (c == f->ctl && part != f->part) f->part = part, recompute_held(s), haptics_tap();
        } else if (f->kind == F_BUTTON && (f->ctl == C_HEAVY || f->ctl == C_LIGHT)) {  // from one attack to the other
            int part = 0, c = hit(s.layout, x, y, s.w, s.h, &part);
            if ((c == C_HEAVY || c == C_LIGHT) && (c != f->ctl || part != f->part)) f->ctl = c, f->part = part, recompute_held(s), haptics_tap();
        } else if (f->kind == F_BUTTON && (f->ctl == C_DODGE || f->ctl == C_R)) {  // a thumb sliding onto the attacks
            int part = 0, c = hit(s.layout, x, y, s.w, s.h, &part);
            if (c == C_ATTACK || c == C_CORNER || c == C_HEAVY || c == C_LIGHT) f->ctl = c, f->part = part, recompute_held(s);
        }
        f->x = x, f->y = y;
        return true;
    }
    // up / cancelled. (Where the finger lifted counts too: a quick flick can come with no motion between its down and
    // its up, and on the item bar that was a tap: the item used instead of the bar turned)
    if (hypotf(x - f->x0, y - f->y0) > .025f * s.h) f->moved = true;
    if (f->kind == F_ITEM && e.type == SDL_EVENT_FINGER_UP && !f->done) {
        f->swipe += x - f->x, f->swipe_y += y - f->y;
        float step = .055f * s.h;
        if (g_ranged && fabsf(f->swipe_y) >= step * 1.3f && fabsf(f->swipe_y) > fabsf(f->swipe)) {
            ammo_step(s, f->swipe_y < 0 ? 1 : -1), haptics_tap();
            f->done = true;
        }
        // (the last bit of a swipe: an item more once it is most of a step, as the finger lifting ends it)
        while (!f->done && f->swipe >= step * .6f) item_swipe(s, 1), f->swipe -= step, haptics_tap();
        while (!f->done && f->swipe <= -step * .6f) item_swipe(s, -1), f->swipe += step, haptics_tap();
    }
    bool tap = !f->moved && now - f->t0 < 300 * MS && e.type == SDL_EVENT_FINGER_UP;
    bool cancel = e.type == SDL_EVENT_FINGER_CANCELED;
    if (f->kind == F_BUTTON && !f->done && !cancel && (now - f->t0 < min_press() || !frames_ok(f->f0 + 2))) {
        uint64_t b = finger_buttons(*f);  // (a quick tap is held until the game has seen it: min_press)
        uint64_t left = now - f->t0 < min_press() ? min_press() - (now - f->t0) : 0;
        if (b) pulse(s, b, left, g_frames_fed ? f->f0 + 2 : 0);
    }
    if (f->kind == F_STICK) s.stick_on = false;
    if (f->kind == F_ITEM && s.bar_open && f->id == s.bar_finger) bar_close(s);
    if (f->kind == F_ITEM && tap && !f->done && !s.bar_open && fabsf(f->swipe) < .02f * s.h && fabsf(f->swipe_y) < .02f * s.h)
        item_use(s), haptics_tap();  // a tap: the item (Y)
    // A tap on the target camera's icon (as the MHGU port's): the lock-on, the camera following the monster. Unlocked: the
    // game's target camera on its monster (a tap on its panel, unless one is chosen) and the lock on; locked: the panel's
    // tap again, the next monster, and once the target camera is off (after the last, or nothing to target: the game's
    // own answer) the lock off with it. The lock-on turned off in the settings: the panel's tap alone.
    // the settings button: a tap opens the settings (a long press opened the editor: touch_frame)
    if (f->kind == F_BUTTON && f->ctl == C_SETTINGS && !f->done && e.type == SDL_EVENT_FINGER_UP) ui_open_settings();
    if (s.menu) s.tap_ns = 0;  // (in the game's menus L turns pages: no double tap there)
    else if (f->kind == F_CAMERA && tap) {  // a double tap on the camera's part centres it (L)
        if (now - s.tap_ns < 350 * MS && hypotf(x - s.tap_x, y - s.tap_y) < .08f * s.h) pulse(s, BTN_L, min_press()), s.tap_ns = 0;
        else s.tap_ns = now, s.tap_x = x, s.tap_y = y;
    }
    f->kind = F_NONE;
    recompute_held(s);
    return true;
}

// This frame's input from the fingers, into `out`
void frame_input(State &s, bool controller, UiInput &out) {
    uint64_t now = SDL_GetTicksNS();
    if (g_game_frame_seen.load(std::memory_order_acquire)) {  // (the game's frame counter, for the presses' length)
        if (!g_frames_fed) fprintf(stderr, "[touch] the game's frames seen: taps last two of them\n");
        g_frames = g_game_frame.load(std::memory_order_relaxed), g_frames_fed = true;
        static uint64_t said_ns, said_frames;
        if (g_set.debug && now - said_ns > 5000 * MS) {
            if (said_ns) fprintf(stderr, "[touch] the game: %.1f frames a second\n", (g_frames - said_frames) / ((now - said_ns) / 1e9));
            said_ns = now, said_frames = g_frames;
        }
    }
    // the game's state: a menu known to open or close takes over from the layout button
    if (g_game.menu >= 0 && g_game.menu != g_menu_known) g_menu_layout = g_game.menu == 1;
    g_menu_known = g_game.menu;
    int ranged = show(TB_RANGED) ? std::clamp(g_game.ranged, 0, 2) : 0;
    if (ranged != g_ranged) {  // (a weapon in hand or gone: the other layout, under the same fingers)
        g_ranged = ranged;
        for (Finger &f : s.f)
            if (f.kind == F_BUTTON && !in_mode(f.ctl, s.menu)) f.done = true;
        recompute_held(s);
    }
    if (g_menu_layout != s.menu) {  // (the other layout; fingers on what it lacks let go)
        s.menu = g_menu_layout;
        for (Finger &f : s.f)
            if (f.kind == F_BUTTON && !in_mode(f.ctl, s.menu)) f.done = true;
        recompute_held(s);
    }
    // a long press on the item bar holds it open (its neighbours shown, as while L is held: a swipe meanwhile turns it,
    // lifting the finger closes it); one on the target camera's icon turns the lock-on on and off
    for (Finger &f : s.f) {
        if (f.kind == F_BUTTON && (f.ctl == C_ITEML || f.ctl == C_ITEMR) && !f.done && f.rep && now >= f.rep) {
            item_swipe(s, f.ctl == C_ITEMR ? 1 : -1);  // (held: an item every 170 ms, as a held d-pad in the game's menus)
            f.rep = now + 170 * MS;
            haptics_tap();
        }
        if (f.kind == F_ITEM && !f.done && !f.moved && !s.bar_open && item_direct() && now - f.t0 > BAR_HOLD) {
            s.bar_open = true, s.bar_finger = f.id;  // (no item used when it lifts: it is no tap)
            haptics_tap();
        }
        if (s.target_check && now >= s.target_check) {  // (no monster on the target camera after a tap: no lock)
            s.target_check = 0;
            if (Port::Hooks::LockOn() && Port::Hooks::TargetCamMonster() == 0) {
                Port::Hooks::SetLockOn(false);
                fprintf(stderr, "[touch] lock-on off (the target camera's icon: no monster on it)\n");
            }
        }
    }
    if (s.bar_open && !item_direct()) bar_close(s);  // (the bar gone: a menu, the hunt over)
    {  // the hooks hold the bar open for the long press, and for a step of the ammo or the coating (ammo_step)
        bool want = s.bar_open || s.ammo_open.phase != 0;
        if (want != s.bar_held) Port::Hooks::HoldItemBarOpen(want), s.bar_held = want;
    }
    // the item used once the weapon is away (item_use): Y again when the hunter's action after the weapon went away
    // (putting it away) gives way to another (standing, walking)
    if (s.use_until) {
        if (now > s.use_until || g_game.place != 2) {
            s.use_until = s.use_at = 0;
        } else if (g_game.weapon_drawn == 0) {
            if (!s.use_at) s.use_at = now, s.use_action = g_game.action;
            else if (g_game.action != s.use_action && now - s.use_at >= USE_AFTER) {
                pulse(s, BTN_Y, min_press());
                s.use_until = s.use_at = 0;
            }
        }
    }
    // a long press on the settings button opens the editor
    for (Finger &f : s.f) {
        if (f.kind != F_BUTTON || f.done) continue;
        if (f.ctl == C_SETTINGS && now - f.t0 > 700 * MS) {
            f.done = true;
            haptics_tap();
            edit_begin(s);
            break;
        }
        if (f.ctl == C_L && !s.menu && lock_on_allowed() && now - f.t0 > 600 * MS) {
            f.done = true;  // (L lets go: the long press was the lock-on's)
            haptics_tap();
            lock_toggle("L held");
            recompute_held(s);
            break;
        }
    }
    // the lock-on ends by itself away from the quest camera for a while (a town, the quest's end) or turned off in the
    // settings
    {
        static uint64_t away_ns;
        if (!Port::Hooks::LockOn() || Port::Hooks::CameraTurnActive()) away_ns = 0;
        else if (!away_ns) away_ns = now;
        if (Port::Hooks::LockOn() && (!lock_on_allowed() || (away_ns && now - away_ns > 10000 * MS))) {
            Port::Hooks::SetLockOn(false);
            away_ns = 0;
            fprintf(stderr, "[touch] lock-on off (%s)\n", lock_on_allowed() ? "no quest camera" : "the settings");
        }
    }
    bool visible = s.edit || g_set.controls == TOUCH_ON || (g_set.controls == TOUCH_AUTO && !controller);
    if (visible != s.visible) {
        fprintf(stderr, "[touch] controls %s (controls %s, controller %d)\n", visible ? "shown" : "hidden",
                CONTROLS_KEY[g_set.controls], (int)controller);
        if (!visible) {  // (what the fingers held lets go; a drag still turns the camera)
            for (Finger &f : s.f)
                if (f.kind == F_BUTTON || f.kind == F_STICK || f.kind == F_ITEM || f.kind == F_TARGET) f.kind = F_NONE;
            if (s.bar_open) bar_close(s);
            s.stick_on = false;
            recompute_held(s);
        }
    }
    s.visible = visible;
    s.gear_only = !visible;  // (hidden, by a controller or by off: the settings button alone stays, the way back)
    uint64_t b = pulse_buttons(s, now) | seq_buttons(s.item, now) | seq_buttons(s.ammo, now) | seq_buttons(s.cursor, now) |
                 (seq_buttons(s.ammo_open, now) & ~BAR_OPEN);
    // the camera: by the angle dragged where the port's hook turns it, else as the C-stick, pushed by the drag's speed
    float dt = s.frame_ns ? std::clamp((now - s.frame_ns) / 1e9f, .004f, .1f) : .016f;
    s.frame_ns = now;
    if (s.h > 0 && !s.edit) {
        if (Port::Hooks::CameraTurnActive()) {
            float k = CAMERA_DEGREES * g_set.camera / s.h;
            out.cam_yaw += s.cam_dx * k, out.cam_pitch -= s.cam_dy * k;
            s.cs_x = s.cs_y = 0, s.cs_until = 0;
        } else if (s.cam_dx != 0 || s.cam_dy != 0) {
            // (full at a screen height a second; the least push past the game's own dead zone)
            float vx = s.cam_dx / dt / s.h * g_set.camera, vy = -s.cam_dy / dt / s.h * g_set.camera, m = hypotf(vx, vy);
            float push = m < .02f ? 0 : .3f + .7f * std::min(1.0f, m / 1.0f);
            s.cs_x = m > 0 ? vx / m * push : 0, s.cs_y = m > 0 ? vy / m * push : 0;
            s.cs_until = now + 70 * MS;
        } else if (now > s.cs_until) {
            s.cs_x = s.cs_y = 0;
        }
        if (s.cs_x != 0 || s.cs_y != 0)
            out.cstick_x = (int16_t)std::clamp(s.cs_x * 156.0f, -156.0f, 156.0f),
            out.cstick_y = (int16_t)std::clamp(s.cs_y * 156.0f, -156.0f, 156.0f);
    }
    s.cam_dx = s.cam_dy = 0;
    if (visible && !s.edit) {
        b |= s.held;
        {  // (the stick is the circle pad in the game's menus too: their cursors follow it)
            s.stick_part = 0;
            bool moving = false;
            if (s.stick_on && s.h > 0) {
                float r = place(s.layout, C_STICK, s.w, s.h).r;
                float dx = (s.kx - s.bx) / r, dy = -(s.ky - s.by) / r;  // (screen y is down, the circle pad's up)
                float m = sqrtf(dx * dx + dy * dy);
                // a tiny dead zone (4%), and the first bit of tilt already past the game's own dead zone (about a
                // quarter): the hunter moves from the first millimetre, faster the farther; full at 90% of the rim
                if (m > .04f) {
                    float o = .28f + .72f * std::min(1.0f, (m - .04f) / .86f), t = o / m;
                    out.circle_x = (int16_t)std::clamp(dx * t * 156.0f, -156.0f, 156.0f);
                    out.circle_y = (int16_t)std::clamp(dy * t * 156.0f, -156.0f, 156.0f);
                    moving = m > .3f;
                }
            }
            // the thumb well past the stick's rim dashes: R held (with the weapon sheathed or not known; drawn, R is
            // the weapon's own move), with a little hysteresis so the edge does not flicker
            static bool was;
            s.run = false;
            if (s.stick_on && moving && s.h > 0 && g_game.weapon_drawn != 1 && !s.menu) {  // (a menu's R: a page)
                float r = place(s.layout, C_STICK, s.w, s.h).r, d = hypotf(s.fx - s.bx, s.fy - s.by);
                was = d > r * (was ? 1.45f : 1.6f);  // (r is the stick's size in the layout: a bigger stick, farther)
                s.run = was;
                if (s.run) b |= BTN_R;
            } else {
                was = false;
            }
        }
    }
    out.buttons |= (uint32_t)(b & 0xffffu);
    out.zl = out.zl || (b & BTN_ZL) != 0;
    out.zr = out.zr || (b & BTN_ZR) != 0;
}

// ------------------------------------------------------------------ drawing
const ImU32 INK = IM_COL32(244, 232, 200, 255), GOLD = IM_COL32(222, 186, 96, 255), BRONZE = IM_COL32(112, 76, 34, 255),
            DEEP = IM_COL32(28, 20, 15, 255), GLOW = IM_COL32(255, 214, 92, 255), RED = IM_COL32(176, 34, 30, 255),
            MOSS = IM_COL32(80, 142, 54, 255), WINE = IM_COL32(50, 25, 24, 255);
bool g_draw_menu, g_draw_bottom, g_draw_skip;  // (the layout button's, the touch screen button's and ZR's faces)
float g_mdpad_a = 0;  // (the menus' d-pad: how far it has faded in)

ImU32 fade(ImU32 c, float a) {
    return (c & 0x00ffffffu) | ((ImU32)std::clamp(((c >> 24) & 0xff) * a, 0.0f, 255.0f) << 24);
}
ImVec2 P(ImVec2 c, float s, float x, float y) { return ImVec2(c.x + x * s, c.y + y * s); }

// a padlock (the lock-on's mark on L): closed while on
void padlock(ImDrawList *dl, ImVec2 c, float s, bool closed, ImU32 col, ImU32 deep) {
    float sx = closed ? 0 : .28f;  // (open: the shackle swung up and to the right)
    dl->PathArcTo(P(c, s, sx, -.18f), .34f * s, PI, 2 * PI, 16);
    dl->PathStroke(col, .13f * s, 0);
    dl->AddLine(P(c, s, sx + .34f, -.18f), P(c, s, sx + .34f, closed ? .02f : -.1f), col, .13f * s);
    dl->AddLine(P(c, s, sx - .34f, -.18f), P(c, s, sx - .34f, .02f), col, .13f * s);
    dl->AddRectFilled(P(c, s, -.5f, -.02f), P(c, s, .5f, .72f), col, .1f * s);
    dl->AddCircleFilled(P(c, s, 0, .3f), .11f * s, deep, 12);
    dl->AddLine(P(c, s, 0, .3f), P(c, s, 0, .52f), deep, .08f * s);
}

void sword(ImDrawList *dl, ImVec2 c, float s, float dx, float dy, ImU32 col) {
    float n = sqrtf(dx * dx + dy * dy);
    dx /= n, dy /= n;
    float px = -dy, py = dx;
    ImVec2 b0(c.x - dx * .25f * s, c.y - dy * .25f * s), tip(c.x + dx * .9f * s, c.y + dy * .9f * s);
    ImVec2 sh(tip.x - dx * .22f * s, tip.y - dy * .22f * s);
    float w = .12f * s;
    ImVec2 blade[5] = {ImVec2(b0.x + px * w, b0.y + py * w), ImVec2(sh.x + px * w, sh.y + py * w), tip,
                       ImVec2(sh.x - px * w, sh.y - py * w), ImVec2(b0.x - px * w, b0.y - py * w)};
    dl->AddConvexPolyFilled(blade, 5, col);
    dl->AddLine(ImVec2(b0.x + px * .4f * s, b0.y + py * .4f * s), ImVec2(b0.x - px * .4f * s, b0.y - py * .4f * s), col,
                .14f * s);
    ImVec2 hilt(b0.x - dx * .42f * s, b0.y - dy * .42f * s);
    dl->AddLine(b0, hilt, col, .12f * s);
    dl->AddCircleFilled(ImVec2(hilt.x - dx * .06f * s, hilt.y - dy * .06f * s), .11f * s, col);
}

// a sword along u, its point toward +u, centred on c: the blade (and its fuller), the guard across, the grip, the pommel
void sword_along(ImDrawList *dl, ImVec2 c, ImVec2 u, float len, float bw, float guard, ImU32 col, ImU32 deep, bool fuller) {
    ImVec2 n(-u.y, u.x), base = add(c, u, -.18f * len), tip = add(c, u, .62f * len), sh = add(tip, u, -bw * 1.1f);
    ImVec2 blade[5] = {add(base, n, bw * .5f), add(sh, n, bw * .5f), tip, add(sh, n, -bw * .5f), add(base, n, -bw * .5f)};
    dl->AddConvexPolyFilled(blade, 5, col);
    if (fuller) dl->AddLine(add(base, u, bw * .3f), add(sh, u, -bw * .2f), deep, bw * .22f);
    dl->AddLine(add(base, n, guard * .5f), add(base, n, -guard * .5f), col, bw * .55f);
    ImVec2 grip = add(base, u, -.2f * len);
    dl->AddLine(base, grip, col, bw * .42f);
    dl->AddCircleFilled(add(grip, u, -bw * .2f), bw * .38f, col, 16);
}

// the 3DS's two screens, the lower one filled while the touch screen's corner shows
void two_screens(ImDrawList *dl, ImVec2 c, float s, bool on, ImU32 col, ImU32 deep) {
    dl->AddRectFilled(P(c, s, -.74f, -.8f), P(c, s, .74f, -.1f), col, .12f * s);
    dl->AddRectFilled(P(c, s, -.6f, -.68f), P(c, s, .6f, -.22f), deep, .05f * s);
    dl->AddLine(P(c, s, -.5f, -.01f), P(c, s, .5f, -.01f), col, .07f * s);  // (the hinge)
    dl->AddRectFilled(P(c, s, -.62f, .08f), P(c, s, .62f, .8f), col, .12f * s);
    if (!on) dl->AddRectFilled(P(c, s, -.48f, .2f), P(c, s, .48f, .68f), deep, .05f * s);
    else dl->AddCircleFilled(P(c, s, .2f, .44f), .1f * s, deep, 12);  // (a touch on it)
}

void icon(ImDrawList *dl, Icon ic, ImVec2 c, float r, ImU32 col, ImU32 deep) {
    float s = r * .62f;
    switch (ic) {
    case I_DODGE: {  // a roll: a curled arrow with motion lines
        float ar = .5f * s;
        dl->PathArcTo(ImVec2(c.x + .08f * s, c.y), ar, PI * .85f, PI * 2.35f, 24);
        dl->PathStroke(col, .16f * s, 0);
        float a = PI * 2.35f;
        ImVec2 e(c.x + .08f * s + cosf(a) * ar, c.y + sinf(a) * ar);
        dl->AddTriangleFilled(ImVec2(e.x - .26f * s, e.y - .16f * s), ImVec2(e.x + .12f * s, e.y - .3f * s),
                              ImVec2(e.x + .08f * s, e.y + .12f * s), col);
        for (int i = 0; i < 3; i++)
            dl->AddLine(P(c, s, -.95f, -.25f + .25f * i), P(c, s, -.62f, -.25f + .25f * i), col, .09f * s);
        break;
    }
    case I_SHIELD: {
        ImVec2 sh[5] = {P(c, s, -.55f, -.62f), P(c, s, .55f, -.62f), P(c, s, .55f, .05f), P(c, s, 0, .7f), P(c, s, -.55f, .05f)};
        dl->AddConvexPolyFilled(sh, 5, col);
        dl->AddLine(P(c, s, 0, -.45f), P(c, s, 0, .45f), deep, .1f * s);
        dl->AddLine(P(c, s, -.36f, -.12f), P(c, s, .36f, -.12f), deep, .1f * s);
        break;
    }
    case I_KICK: {  // a kick: a hunter's boot, tipped forward, its toe striking a burst
        const float ca = .883f, sa = -.469f;  // (tipped 28 degrees: the toe up and to the right)
        ImVec2 o = P(c, s, -.16f, -.06f);
        auto at = [&](float x, float y) { return P(o, s, x * ca - y * sa, x * sa + y * ca); };
        ImVec2 hit = at(.74f, .2f);  // (the impact, ahead of the toe)
        ImVec2 star[16];
        for (int i = 0; i < 16; i++) {
            float an = i * PI / 8 - PI / 2, rr = (i % 2 ? .13f : .36f) * s;
            star[i] = ImVec2(hit.x + cosf(an) * rr, hit.y + sinf(an) * rr);
        }
        for (int i = 0; i < 16; i += 2) {
            dl->AddTriangleFilled(hit, star[(i + 15) % 16], star[i], fade(col, .8f));
            dl->AddTriangleFilled(hit, star[i], star[i + 1], fade(col, .8f));
        }
        ImVec2 shaft[4] = {at(-.44f, -.78f), at(-.02f, -.78f), at(0, .06f), at(-.44f, .06f)};
        dl->AddConvexPolyFilled(shaft, 4, col);
        ImVec2 foot[6] = {at(-.44f, 0), at(.04f, -.03f), at(.42f, .1f), at(.54f, .27f), at(.47f, .42f), at(-.44f, .42f)};
        dl->AddConvexPolyFilled(foot, 6, col);
        dl->AddLine(at(-.46f, -.6f), at(0, -.6f), deep, .07f * s);    // (the cuff)
        dl->AddLine(at(-.42f, .33f), at(.47f, .33f), deep, .06f * s);  // (the sole)
        dl->AddLine(at(-.06f, -.1f), at(.3f, .08f), deep, .05f * s);   // (the lacing's edge)
        break;
    }
    case I_TOWN: {  // the town's actions: a guild card (a hunter's head and shoulders, the card's lines) with a gold stud
        dl->AddRectFilled(P(c, s, -.78f, -.52f), P(c, s, .78f, .52f), col, .1f * s);
        dl->AddRectFilled(P(c, s, -.66f, -.4f), P(c, s, .66f, .4f), deep, .06f * s);
        dl->AddCircleFilled(P(c, s, -.34f, -.08f), .15f * s, col, 14);
        dl->AddTriangleFilled(P(c, s, -.58f, .32f), P(c, s, -.34f, .02f), P(c, s, -.1f, .32f), col);
        for (int i = 0; i < 3; i++) dl->AddRectFilled(P(c, s, .02f, -.22f + i * .2f), P(c, s, .52f, -.14f + i * .2f), col, .03f * s);
        break;
    }
    case I_SKIP: {  // once a quest is complete: skip (the touch screen's Skip): two arrows and a bar
        dl->AddTriangleFilled(P(c, s, -.72f, -.5f), P(c, s, -.12f, 0), P(c, s, -.72f, .5f), col);
        dl->AddTriangleFilled(P(c, s, -.16f, -.5f), P(c, s, .44f, 0), P(c, s, -.16f, .5f), col);
        dl->AddRectFilled(P(c, s, .5f, -.5f), P(c, s, .68f, .5f), col, .04f * s);
        break;
    }
    case I_TARGET_SELECT: {  // the target camera: a reticle between two arrows (it goes from one monster to the other)
        dl->AddCircle(c, .38f * s, col, 20, .11f * s);
        dl->AddCircleFilled(c, .1f * s, col);
        for (int i = 0; i < 4; i++) {
            float a = i * PI * .5f;
            dl->AddLine(ImVec2(c.x + cosf(a) * .26f * s, c.y + sinf(a) * .26f * s),
                        ImVec2(c.x + cosf(a) * .52f * s, c.y + sinf(a) * .52f * s), col, .1f * s);
        }
        for (int d = -1; d <= 1; d += 2)
            dl->AddTriangleFilled(P(c, s, .95f * d, 0), P(c, s, .68f * d, -.24f), P(c, s, .68f * d, .24f), col);
        break;
    }
    case I_CAMERA: {  // centre the camera: a camera
        dl->AddRectFilled(P(c, s, -.66f, -.36f), P(c, s, .66f, .44f), col, .12f * s);
        dl->AddRectFilled(P(c, s, -.24f, -.54f), P(c, s, .24f, -.3f), col, .06f * s);
        dl->AddCircleFilled(P(c, s, 0, .05f), .27f * s, deep, 18);
        dl->AddCircleFilled(P(c, s, 0, .05f), .15f * s, col, 14);
        break;
    }
    case I_MENU:
        for (int i = -1; i <= 1; i++) dl->AddRectFilled(P(c, s, -.55f, i * .38f - .08f), P(c, s, .55f, i * .38f + .08f), col, .05f * s);
        break;
    case I_SCREEN:
        two_screens(dl, c, s, g_draw_bottom, col, deep);
        break;
    case I_SELECT:  // (the 3DS's SELECT: a small oval key)
        dl->AddRectFilled(P(c, s, -.5f, -.2f), P(c, s, .5f, .2f), col, .2f * s);
        dl->AddRectFilled(P(c, s, -.34f, -.07f), P(c, s, .34f, .07f), deep, .07f * s);
        break;
    case I_LAYOUT: {  // the two layouts, the one in use lit: a sword (the hunt's) and a list with its cursor (the menus')
        ImU32 hunt = g_draw_menu ? fade(col, .38f) : GLOW, menu = g_draw_menu ? GLOW : fade(col, .38f);
        sword_along(dl, P(c, s, -.42f, .02f), ImVec2(0, -1), s * 1.45f, s * .24f, s * .5f, hunt, deep, false);
        for (int i = -1; i <= 1; i++) dl->AddRectFilled(P(c, s, .2f, i * .32f - .06f), P(c, s, .78f, i * .32f + .06f), menu, .04f * s);
        dl->AddTriangleFilled(P(c, s, .02f, -.15f), P(c, s, .16f, 0), P(c, s, .02f, .15f), menu);
        dl->AddLine(P(c, s, -.02f, -.62f), P(c, s, -.02f, .62f), fade(col, .5f), .04f * s);
        break;
    }
    case I_SHEATHE: {  // a sword sliding down into its scabbard
        ImVec2 sc[4] = {P(c, s, -.22f, -.08f), P(c, s, .22f, -.08f), P(c, s, .16f, .82f), P(c, s, -.16f, .82f)};
        dl->AddConvexPolyFilled(sc, 4, col);  // (the scabbard)
        dl->AddLine(P(c, s, -.3f, .12f), P(c, s, .3f, .12f), deep, .07f * s);  // (its band)
        dl->AddRectFilled(P(c, s, -.42f, -.26f), P(c, s, .42f, -.14f), col, .04f * s);  // (the guard, at its mouth)
        dl->AddRectFilled(P(c, s, -.07f, -.62f), P(c, s, .07f, -.26f), col);  // (the grip)
        dl->AddCircleFilled(P(c, s, 0, -.68f), .1f * s, col, 14);  // (the pommel)
        for (int d = -1; d <= 1; d += 2)  // (down arrows beside it: in it goes)
            dl->AddTriangleFilled(P(c, s, .62f * d, .38f), P(c, s, .44f * d, .1f), P(c, s, .8f * d, .1f), col);
        break;
    }
    case I_SHOT: {  // a shell flying up and to the right, its trail behind it
        ImVec2 u(.7071f, -.7071f), n(.7071f, .7071f), b = add(c, u, -.12f * s), m = add(c, u, .3f * s), t = add(c, u, .78f * s);
        float w = .27f * s;
        ImVec2 body[5] = {add(b, n, w), add(m, n, w), t, add(m, n, -w), add(b, n, -w)};
        dl->AddConvexPolyFilled(body, 5, col);
        dl->AddLine(add(add(b, u, .17f * s), n, w), add(add(b, u, .17f * s), n, -w), deep, .07f * s);  // (its band)
        for (int k = -1; k <= 1; k++) {
            ImVec2 o = add(add(c, u, -.34f * s), n, k * .24f * s);
            dl->AddLine(o, add(o, u, -(k ? .34f : .54f) * s), fade(col, k ? .7f : 1.0f), .09f * s);
        }
        break;
    }
    case I_BOW: {  // a bow drawn, up and to the right, the arrow on its string
        const float at = -PI * .25f;
        ImVec2 u(.7071f, -.7071f), n(.7071f, .7071f), o = add(c, u, -.5f * s), nock = add(c, u, -.42f * s);
        float r0 = .95f * s, sweep = 1.0f;
        dl->PathArcTo(o, r0, at - sweep, at + sweep, 22);
        dl->PathStroke(col, .13f * s, 0);
        ImVec2 e0(o.x + cosf(at - sweep) * r0, o.y + sinf(at - sweep) * r0), e1(o.x + cosf(at + sweep) * r0, o.y + sinf(at + sweep) * r0);
        dl->AddLine(e0, nock, col, .05f * s);
        dl->AddLine(nock, e1, col, .05f * s);
        ImVec2 tip = add(c, u, .92f * s), sh = add(c, u, .56f * s);
        dl->AddLine(add(c, u, -.62f * s), sh, col, .08f * s);
        dl->AddTriangleFilled(tip, add(sh, n, .17f * s), add(sh, n, -.17f * s), col);
        dl->AddLine(add(add(c, u, -.62f * s), n, .13f * s), add(c, u, -.42f * s), col, .06f * s);  // (its fletching)
        dl->AddLine(add(add(c, u, -.62f * s), n, -.13f * s), add(c, u, -.42f * s), col, .06f * s);
        break;
    }
    case I_RELOAD: {  // a shell in a turning arrow
        float ar = .68f * s, a1 = PI * 1.55f;
        dl->PathArcTo(c, ar, -PI * .35f, a1, 28);
        dl->PathStroke(col, .13f * s, 0);
        ImVec2 e(c.x + cosf(a1) * ar, c.y + sinf(a1) * ar);
        dl->AddTriangleFilled(ImVec2(e.x + .34f * s, e.y + .02f * s), ImVec2(e.x - .1f * s, e.y - .3f * s), ImVec2(e.x - .1f * s, e.y + .3f * s), col);
        ImVec2 sh[5] = {P(c, s, -.17f, .34f), P(c, s, -.17f, -.1f), P(c, s, 0, -.38f), P(c, s, .17f, -.1f), P(c, s, .17f, .34f)};
        dl->AddConvexPolyFilled(sh, 5, col);
        break;
    }
    case I_SCOPE: {  // the view through a scope
        dl->AddCircle(c, .66f * s, col, 28, .13f * s);
        dl->AddCircleFilled(c, .09f * s, col, 12);
        for (int i = 0; i < 4; i++) {
            float a = i * PI * .5f;
            dl->AddLine(ImVec2(c.x + cosf(a) * .28f * s, c.y + sinf(a) * .28f * s), ImVec2(c.x + cosf(a) * .94f * s, c.y + sinf(a) * .94f * s), col, .1f * s);
        }
        break;
    }
    case I_CROUCH: {  // down on one knee: two chevrons over the ground
        for (int k = 0; k < 2; k++) {
            float y = -.5f + k * .42f;
            ImVec2 v[3] = {P(c, s, -.6f, y), P(c, s, 0, y + .42f), P(c, s, .6f, y)};
            dl->AddPolyline(v, 3, col, .17f * s, 0);
        }
        dl->AddRectFilled(P(c, s, -.7f, .5f), P(c, s, .7f, .63f), col, .05f * s);
        break;
    }
    case I_COATING: {  // a flask
        dl->AddRectFilled(P(c, s, -.17f, -.72f), P(c, s, .17f, -.2f), col, .03f * s);
        dl->AddRectFilled(P(c, s, -.27f, -.8f), P(c, s, .27f, -.66f), col, .04f * s);
        dl->AddCircleFilled(P(c, s, 0, .22f), .55f * s, col, 24);
        dl->AddCircleFilled(P(c, s, 0, .22f), .36f * s, deep, 20);
        dl->AddCircleFilled(P(c, s, 0, .3f), .24f * s, col, 16);
        break;
    }
    case I_MELEE:
        sword(dl, P(c, s, -.08f, .08f), s * 1.05f, 1, -1, col);
        break;
    case I_GATHER: {  // an open hand
        for (int i = 0; i < 4; i++) {
            float x = -.42f + i * .28f, top = i == 0 || i == 3 ? -.42f : -.66f;
            dl->AddLine(P(c, s, x, .1f), P(c, s, x, top), col, .2f * s);
            dl->AddCircleFilled(P(c, s, x, top), .1f * s, col, 10);
        }
        dl->AddLine(P(c, s, -.5f, .3f), P(c, s, -.84f, -.06f), col, .2f * s);
        dl->AddRectFilled(P(c, s, -.52f, 0), P(c, s, .52f, .66f), col, .18f * s);
        break;
    }
    case I_LEFT: case I_RIGHT: {  // the item bar's arrows: a gold arrowhead with the bar's edge behind it
        float d = ic == I_LEFT ? -1.0f : 1.0f;
        dl->AddTriangleFilled(P(c, s, d * .78f, 0), P(c, s, -d * .3f, -.72f), P(c, s, -d * .3f, .72f), col);
        dl->AddRectFilled(P(c, s, d > 0 ? -.72f : .44f, -.5f), P(c, s, d > 0 ? -.5f : .66f, .5f), fade(col, .7f), .04f * s);
        break;
    }
    case I_UP: case I_DOWN: {
        float d = ic == I_UP ? -1.0f : 1.0f;
        dl->AddTriangleFilled(P(c, s, 0, d * .78f), P(c, s, -.72f, -d * .3f), P(c, s, .72f, -d * .3f), col);
        dl->AddRectFilled(P(c, s, -.5f, d > 0 ? -.72f : .44f), P(c, s, .5f, d > 0 ? -.5f : .66f), fade(col, .7f), .04f * s);
        break;
    }
    case I_GEAR:
        dl->AddCircleFilled(c, .5f * s, col, 24);
        for (int i = 0; i < 8; i++) {
            float a = i * PI * .25f;
            dl->AddLine(ImVec2(c.x + cosf(a) * .4f * s, c.y + sinf(a) * .4f * s),
                        ImVec2(c.x + cosf(a) * .72f * s, c.y + sinf(a) * .72f * s), col, .24f * s);
        }
        dl->AddCircleFilled(c, .22f * s, deep, 16);
        break;
    default:
        break;
    }
}

// a disc: a soft shadow, the bronze ring with a gold rule and four studs, the dark face; pressed, it lights up
void disc(ImDrawList *dl, ImVec2 c, float r, bool pressed, float a) {
    dl->AddCircleFilled(ImVec2(c.x, c.y + r * .07f), r * 1.05f, fade(IM_COL32(0, 0, 0, 120), a), 40);
    dl->AddCircleFilled(c, r, fade(BRONZE, a * .95f), 40);
    dl->AddCircle(c, r * .91f, fade(GOLD, a), 40, r * .06f);
    dl->AddCircleFilled(c, r * .84f, fade(pressed ? IM_COL32(92, 62, 22, 255) : DEEP, a * (pressed ? .92f : .72f)), 40);
    dl->AddCircleFilled(ImVec2(c.x, c.y - r * .2f), r * .6f, fade(IM_COL32(255, 240, 210, 255), a * .05f), 32);
    for (int i = 0; i < 4; i++) {  // (studs on the ring)
        float an = PI * .25f + i * PI * .5f, sr = r * .955f;
        ImVec2 p(c.x + cosf(an) * sr, c.y + sinf(an) * sr);
        float d = r * .07f;
        ImVec2 q[4] = {ImVec2(p.x, p.y - d), ImVec2(p.x + d, p.y), ImVec2(p.x, p.y + d), ImVec2(p.x - d, p.y)};
        dl->AddConvexPolyFilled(q, 4, fade(GOLD, a));
    }
    if (pressed) {
        dl->AddCircle(c, r * 1.1f, fade(GLOW, a * .7f), 40, r * .08f);
        dl->AddCircle(c, r * 1.2f, fade(GLOW, a * .25f), 40, r * .06f);
    }
}

// the game's button on a small dark badge with a gold rim: round for a letter or two, a plate for START and SELECT
void badge(ImDrawList *dl, ImFont *font, ImVec2 b, float br, const char *letter, float a) {
    if (!letter[0]) return;
    size_t n = strlen(letter);
    if (n <= 2) {
        dl->AddCircleFilled(b, br, fade(IM_COL32(18, 12, 9, 255), a), 20);
        dl->AddCircle(b, br, fade(GOLD, a), 20, br * .14f);
        float fs = br * (n > 1 ? 1.0f : 1.3f);
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, letter);
        dl->AddText(font, fs, ImVec2(b.x - ts.x * .5f, b.y - ts.y * .52f), fade(INK, a), letter);
        return;
    }
    float fs = br * .92f;
    ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, letter);
    float hw = ts.x * .5f + br * .5f, hh = br * .72f;
    dl->AddRectFilled(ImVec2(b.x - hw, b.y - hh), ImVec2(b.x + hw, b.y + hh), fade(IM_COL32(18, 12, 9, 255), a), hh);
    dl->AddRect(ImVec2(b.x - hw, b.y - hh), ImVec2(b.x + hw, b.y + hh), fade(GOLD, a), hh, br * .12f, 0);
    dl->AddText(font, fs, ImVec2(b.x - ts.x * .5f, b.y - ts.y * .52f), fade(INK, a), letter);
}

// the attack pill's outline (a capsule along PILL_U), as the current path
void capsule_path(ImDrawList *dl, ImVec2 c, float half, float r) {
    float an = atan2f(PILL_U.y, PILL_U.x);
    dl->PathArcTo(add(c, PILL_U, half), r, an - PI / 2, an + PI / 2, 20);
    dl->PathArcTo(add(c, PILL_U, -half), r, an + PI / 2, an + 3 * PI / 2, 20);
}
// one half (0 upper, 1 lower) of its face, from the seam out, as the current path
void half_path(ImDrawList *dl, ImVec2 c, float half, float r, int which, float seam) {
    float an = atan2f(PILL_U.y, PILL_U.x), d = which ? -1.0f : 1.0f;
    ImVec2 n(-PILL_U.y, PILL_U.x), s0 = add(c, PILL_U, d * seam);
    dl->PathLineTo(add(s0, n, which ? r : -r));
    dl->PathArcTo(add(c, PILL_U, d * half), r, an + (which ? PI / 2 : -PI / 2), an + (which ? 3 * PI / 2 : PI / 2), 20);
    dl->PathLineTo(add(s0, n, which ? -r : r));
}

void pill_body(ImDrawList *dl, Geo g, int pressed_mask, float a) {
    float r = g.r, half = r * PILL_HALF, seam = half * PILL_SEAM;
    ImVec2 c = g.c, n(-PILL_U.y, PILL_U.x);
    capsule_path(dl, ImVec2(c.x, c.y + r * .07f), half, r * 1.05f);
    dl->PathFillConvex(fade(IM_COL32(0, 0, 0, 120), a));
    capsule_path(dl, c, half, r);
    dl->PathFillConvex(fade(BRONZE, a * .95f));
    capsule_path(dl, c, half, r * .91f);
    dl->PathStroke(fade(GOLD, a), r * .06f, ImDrawFlags_Closed);
    for (int h = 0; h < 2; h++) {
        bool on = pressed_mask & (1 << h);
        half_path(dl, c, half, r * .84f, h, seam);
        dl->PathFillConvex(fade(on ? IM_COL32(92, 62, 22, 255) : DEEP, a * (on ? .92f : .72f)));
    }
    // the seam: both buttons; a gold rule each side and a stud in the middle
    bool both = (pressed_mask & 3) == 3;
    ImVec2 q[4] = {add(add(c, PILL_U, seam), n, r * .84f), add(add(c, PILL_U, seam), n, -r * .84f),
                   add(add(c, PILL_U, -seam), n, -r * .84f), add(add(c, PILL_U, -seam), n, r * .84f)};
    dl->AddConvexPolyFilled(q, 4, fade(both ? IM_COL32(120, 84, 28, 255) : IM_COL32(52, 36, 22, 255), a * .85f));
    dl->AddLine(add(add(c, PILL_U, seam), n, r * .86f), add(add(c, PILL_U, seam), n, -r * .86f), fade(GOLD, a), r * .045f);
    dl->AddLine(add(add(c, PILL_U, -seam), n, r * .86f), add(add(c, PILL_U, -seam), n, -r * .86f), fade(GOLD, a), r * .045f);
    float d = r * .13f;
    ImVec2 st[4] = {ImVec2(c.x, c.y - d), ImVec2(c.x + d, c.y), ImVec2(c.x, c.y + d), ImVec2(c.x - d, c.y)};
    dl->AddConvexPolyFilled(st, 4, fade(both ? GLOW : GOLD, a));
    if (pressed_mask & 3) {
        capsule_path(dl, c, half, r * 1.1f);
        dl->PathStroke(fade(GLOW, a * .6f), r * .08f, ImDrawFlags_Closed);
    }
}

void pill_letters(ImDrawList *dl, ImFont *font, Geo g, const char *upper, const char *lower, float a) {
    float r = g.r, fs = r * .38f;
    ImVec2 n(-PILL_U.y, PILL_U.x);
    for (int h = 0; h < 2; h++) {
        const char *t = h ? lower : upper;
        float sgn = h ? -1.0f : 1.0f;
        ImVec2 at = add(add(g.c, PILL_U, sgn * r * 2.3f), n, sgn * r * .5f), ts = font->CalcTextSizeA(fs, FLT_MAX, 0, t);
        dl->AddText(font, fs, ImVec2(at.x - ts.x * .5f + fs * .05f, at.y - ts.y * .5f + fs * .06f), fade(IM_COL32(0, 0, 0, 255), a * .8f), t);
        dl->AddText(font, fs, ImVec2(at.x - ts.x * .5f, at.y - ts.y * .5f), fade(GOLD, a), t);
    }
}

void pill(ImDrawList *dl, ImFont *font, Geo g, int pressed_mask, float a) {
    float r = g.r;
    ImVec2 c = g.c;
    pill_body(dl, g, pressed_mask, a);
    // the icons in the middle of each half (between the seam and the round end: 1.5 radii from the centre), along the
    // pill: a great sword on the upper half (A), a small sword on the lower (X: it draws the weapon too); the game's
    // letter engraved by the outer end of each half, on the side away from the guard
    ImU32 cu = fade(pressed_mask & 1 ? GLOW : INK, a), cl = fade(pressed_mask & 2 ? GLOW : INK, a);
    sword_along(dl, add(c, PILL_U, r * 1.5f), PILL_U, r * 2.05f, r * .46f, r * .95f, cu, fade(DEEP, a), true);
    sword_along(dl, add(c, PILL_U, -r * 1.5f), PILL_U, r * 1.25f, r * .24f, r * .6f, cl, fade(DEEP, a), false);
    pill_letters(dl, font, g, "A", "X", a);
}

// The menu layout's: the same pill, a tick to confirm on its upper half and an arrow turning back on the lower
void check_mark(ImDrawList *dl, ImVec2 c, float s, ImU32 col, ImU32 deep) {
    ImVec2 p[3] = {P(c, s, -.56f, .02f), P(c, s, -.14f, .44f), P(c, s, .62f, -.46f)};
    dl->AddPolyline(p, 3, deep, s * .38f, 0);
    dl->AddPolyline(p, 3, col, s * .23f, 0);
}

void back_arrow(ImDrawList *dl, ImVec2 c, float s, ImU32 col, ImU32 deep) {
    for (int pass = 0; pass < 2; pass++) {
        float t = pass ? s * .19f : s * .34f;
        ImU32 ink = pass ? col : deep;
        dl->PathLineTo(P(c, s, -.36f, -.3f));
        dl->PathArcTo(P(c, s, .16f, .05f), s * .35f, -PI * .5f, PI * .5f, 14);
        dl->PathLineTo(P(c, s, -.3f, .4f));
        dl->PathStroke(ink, t, 0);
        float e = pass ? 0 : s * .08f;
        dl->AddTriangleFilled(ImVec2(c.x - s * .74f - e, c.y - s * .3f), ImVec2(c.x - s * .3f + e * .4f, c.y - s * .62f - e),
                              ImVec2(c.x - s * .3f + e * .4f, c.y + s * .02f + e), ink);
    }
}

void menu_pill(ImDrawList *dl, ImFont *font, Geo g, int mask, float a) {
    float r = g.r;
    pill_body(dl, g, mask, a);
    check_mark(dl, add(g.c, PILL_U, r * 1.45f), r * .72f, fade(mask & 1 ? GLOW : INK, a), fade(DEEP, a));
    back_arrow(dl, add(g.c, PILL_U, -r * 1.45f), r * .66f, fade(mask & 2 ? GLOW : INK, a), fade(DEEP, a));
    pill_letters(dl, font, g, "A", "B", a);
}

// L and R: tabs, as the game's own pages, an arrow to their side
void tab(ImDrawList *dl, ImFont *font, Geo g, bool left, const char *t, bool pressed, float a) {
    float w = g.r * TAB_W, h = g.r * TAB_H, rd = h * .55f;
    ImVec2 p0(g.c.x - w, g.c.y - h), p1(g.c.x + w, g.c.y + h), in(g.r * .13f, g.r * .13f);
    dl->AddRectFilled(ImVec2(p0.x, p0.y + g.r * .08f), ImVec2(p1.x, p1.y + g.r * .1f), fade(IM_COL32(0, 0, 0, 120), a), rd);
    dl->AddRectFilled(p0, p1, fade(BRONZE, a * .95f), rd);
    dl->AddRect(ImVec2(p0.x + in.x * .45f, p0.y + in.y * .45f), ImVec2(p1.x - in.x * .45f, p1.y - in.y * .45f), fade(pressed ? GLOW : GOLD, a), rd * .85f, g.r * .06f, 0);
    dl->AddRectFilled(ImVec2(p0.x + in.x, p0.y + in.y), ImVec2(p1.x - in.x, p1.y - in.y),
                      fade(pressed ? IM_COL32(92, 62, 22, 255) : DEEP, a * (pressed ? .92f : .72f)), rd * .7f);
    float d = left ? -1.0f : 1.0f, ax = g.c.x + d * w * .62f, ch = g.r * .34f;
    dl->AddTriangleFilled(ImVec2(ax + d * ch * .8f, g.c.y), ImVec2(ax - d * ch * .3f, g.c.y - ch), ImVec2(ax - d * ch * .3f, g.c.y + ch),
                          fade(pressed ? GLOW : GOLD, a));
    float fs = g.r * .95f;
    ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, t), at(g.c.x - d * g.r * .28f - ts.x * .5f, g.c.y - ts.y * .52f);
    dl->AddText(font, fs, ImVec2(at.x + fs * .04f, at.y + fs * .05f), fade(IM_COL32(0, 0, 0, 255), a * .7f), t);
    dl->AddText(font, fs, at, fade(pressed ? GLOW : INK, a), t);
}

// The band's outline between two points along it, f off its middle line both ways, as the current path; a round end
// where it is one of the band's own
void band_path(ImDrawList *dl, Geo g, float s0, float s1, float f, bool cap0, bool cap1) {
    const int N = 44;
    for (int i = 0; i <= N; i++) { ImVec2 p = band_point(g, s0 + (s1 - s0) * i / N, f); dl->PathLineTo(p); }
    if (cap1) dl->PathArcTo(band_point(g, s1, 0), f, 0, -PI, 14);
    for (int i = N; i >= 0; i--) { ImVec2 p = band_point(g, s0 + (s1 - s0) * i / N, -f); dl->PathLineTo(p); }
    if (cap0) dl->PathArcTo(band_point(g, s0, 0), f, -PI * .5f, -PI * 1.5f, 14);
}
// Filled as a strip of quads (no triangulation of a shape so bent, which took in its inside; no edge smoothing between
// them, which would show as lines) with round ends
void band_fill(ImDrawList *dl, Geo g, float s0, float s1, float f, ImU32 col, bool cap0, bool cap1) {
    const int N = 44;
    ImDrawListFlags keep = dl->Flags;
    dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
    for (int i = 0; i < N; i++) {
        float a = s0 + (s1 - s0) * i / N, b = s0 + (s1 - s0) * (i + 1) / N;
        dl->AddQuadFilled(band_point(g, a, f), band_point(g, b, f), band_point(g, b, -f), band_point(g, a, -f), col);
    }
    if (cap1) { dl->PathArcTo(band_point(g, s1, 0), f, 0, -PI, 16); dl->PathFillConvex(col); }
    if (cap0) { dl->PathArcTo(band_point(g, s0, 0), f, -PI * .5f, -PI * 1.5f, 16); dl->PathFillConvex(col); }
    dl->Flags = keep;
}
ImVec2 band_along(Geo g, float s) {  // its direction at s
    ImVec2 a = band_point(g, s - g.r * .02f, 0), b = band_point(g, s + g.r * .02f, 0);
    float dx = b.x - a.x, dy = b.y - a.y, n = std::max(1e-3f, hypotf(dx, dy));
    return ImVec2(dx / n, dy / n);
}
// All three in one band round the corner: the guard along the bottom, X at the bend, A up the side, the seam between
// the attacks pressing both; bits of mask: 1 A, 2 X, 4 guard
void corner_band(ImDrawList *dl, ImFont *font, Geo g, int mask, float a) {
    float t = g.r * BAND_T, len = g.r * BAND_LEN, s0 = len * BAND_GUARD, s1 = len * BAND_SEAM0, s2 = len * BAND_SEAM1, e = .84f * t;
    ImU32 lit = IM_COL32(92, 62, 22, 255);
    band_fill(dl, {ImVec2(g.c.x, g.c.y + t * .08f), g.r}, 0, len, t * 1.04f, fade(IM_COL32(0, 0, 0, 120), a), true, true);
    band_fill(dl, g, 0, len, t, fade(BRONZE, a * .95f), true, true);
    band_path(dl, g, 0, len, t, true, true);
    dl->PathStroke(fade(BRONZE, a * .95f), 1.2f, ImDrawFlags_Closed);  // (its outer edges smooth)
    band_path(dl, g, 0, len, t * .91f, true, true);
    dl->PathStroke(fade(GOLD, a), t * .06f, ImDrawFlags_Closed);
    auto face = [&](float from, float to, bool c0, bool c1, bool on) {
        band_fill(dl, g, from, to, e, fade(on ? lit : DEEP, a * (on ? .92f : .72f)), c0, c1);
    };
    face(0, s0, true, false, mask & 4);
    face(s0, s1, false, false, mask & 2);
    face(s2, len, false, true, mask & 1);
    bool both = (mask & 3) == 3;
    band_fill(dl, g, s1, s2, e, fade(both ? IM_COL32(120, 84, 28, 255) : IM_COL32(52, 36, 22, 255), a * .85f), false, false);
    for (float at : {s0, s1, s2}) dl->AddLine(band_point(g, at, t * .86f), band_point(g, at, -t * .86f), fade(GOLD, a), t * .045f);
    ImVec2 m = band_point(g, (s1 + s2) * .5f, 0);
    float d = t * .13f;
    ImVec2 st[4] = {ImVec2(m.x, m.y - d), ImVec2(m.x + d, m.y), ImVec2(m.x, m.y + d), ImVec2(m.x - d, m.y)};
    dl->AddConvexPolyFilled(st, 4, fade(both ? GLOW : GOLD, a));
    if (mask) {
        band_path(dl, g, 0, len, t * 1.1f, true, true);
        dl->PathStroke(fade(GLOW, a * .6f), t * .08f, ImDrawFlags_Closed);
    }
    ImU32 ch = fade(mask & 1 ? GLOW : INK, a), cl = fade(mask & 2 ? GLOW : INK, a), cg = fade(mask & 4 ? GLOW : INK, a);
    float sh = (s2 + len) * .5f, sl = (s0 + s1) * .5f, sg = s0 * .52f;
    sword_along(dl, band_point(g, sh, 0), band_along(g, sh), t * 2.05f, t * .46f, t * .95f, ch, fade(DEEP, a), true);
    sword_along(dl, band_point(g, sl, 0), band_along(g, sl), t * 1.25f, t * .24f, t * .6f, cl, fade(DEEP, a), false);
    icon(dl, I_SHIELD, band_point(g, sg, 0), t * .95f, cg, fade(DEEP, a));
    float fs = t * .38f;
    const struct { const char *t; ImVec2 at; } letters[3] = {
        {"A", band_point(g, sh + t * 1.0f, -t * .52f)}, {"X", band_point(g, sl - t * .95f, -t * .52f)}, {"R", band_point(g, sg - t * .95f, -t * .52f)}};
    for (const auto &l : letters) {
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, l.t);
        dl->AddText(font, fs, ImVec2(l.at.x - ts.x * .5f + fs * .05f, l.at.y - ts.y * .5f + fs * .06f), fade(IM_COL32(0, 0, 0, 255), a * .8f), l.t);
        dl->AddText(font, fs, ImVec2(l.at.x - ts.x * .5f, l.at.y - ts.y * .5f), fade(GOLD, a), l.t);
    }
}

// the d-pad: a round bronze well like the other buttons, a long cross on it with a cream arrow near each tip; the
// direction pressed lights its quarter of the well, and a gold arc outside the rim, which shows round the thumb
void dpad(ImDrawList *dl, Geo g, int part, float a) {
    ImVec2 c = g.c;
    float r = g.r, hw = r * .3f, len = r * .8f, edge = r * .045f;  // (an arm's half width and length, its bronze edge)
    static const float dir[4][2] = {{1, 0}, {-1, 0}, {0, -1}, {0, 1}};  // (right, left, up, down: the part's bits)
    disc(dl, c, r, false, a);
    auto lit = [&](int i) { return (part & (1 << i)) != 0; };
    for (int i = 0; i < 4; i++) {
        if (!lit(i)) continue;
        float an = atan2f(dir[i][1], dir[i][0]);
        dl->PathLineTo(c);
        dl->PathArcTo(c, r * .84f, an - PI * .25f, an + PI * .25f, 14);
        dl->PathFillConvex(fade(IM_COL32(92, 62, 22, 255), a * .9f));
        dl->PathArcTo(c, r * 1.1f, an - PI * .23f, an + PI * .23f, 14);
        dl->PathStroke(fade(GLOW, a * .85f), r * .07f, ImDrawFlags_None);
        dl->PathArcTo(c, r * 1.2f, an - PI * .18f, an + PI * .18f, 12);
        dl->PathStroke(fade(GLOW, a * .3f), r * .05f, ImDrawFlags_None);
    }
    // (the arms out from a square in the middle, one piece: drawn from the centre they would overlap there, and show)
    auto arm = [&](const float *d, float from, float to, float w, ImU32 col) {
        ImVec2 p0(c.x + d[0] * from - (d[1] != 0 ? w : 0), c.y + d[1] * from - (d[0] != 0 ? w : 0));
        ImVec2 p1(c.x + d[0] * to + (d[1] != 0 ? w : 0), c.y + d[1] * to + (d[0] != 0 ? w : 0));
        ImDrawFlags tip = d[0] > 0 ? ImDrawFlags_RoundCornersRight : d[0] < 0 ? ImDrawFlags_RoundCornersLeft
                        : d[1] < 0 ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersBottom;
        dl->AddRectFilled(ImVec2(std::min(p0.x, p1.x), std::min(p0.y, p1.y)), ImVec2(std::max(p0.x, p1.x), std::max(p0.y, p1.y)), col, w * .3f, tip);
    };
    const ImU32 face = fade(IM_COL32(60, 43, 28, 255), a * .95f), pressed = fade(IM_COL32(128, 88, 32, 255), a);
    float out = hw + edge;
    for (int i = 0; i < 4; i++) arm(dir[i], out, len + edge, out, fade(BRONZE, a));
    dl->AddRectFilled(ImVec2(c.x - out, c.y - out), ImVec2(c.x + out, c.y + out), fade(BRONZE, a));
    for (int i = 0; i < 4; i++) arm(dir[i], hw, len, hw, lit(i) ? pressed : face);
    dl->AddRectFilled(ImVec2(c.x - hw, c.y - hw), ImVec2(c.x + hw, c.y + hw), face);
    dl->AddCircleFilled(c, hw * .68f, fade(DEEP, a), 20);
    float d = hw * .36f;
    ImVec2 q[4] = {ImVec2(c.x, c.y - d), ImVec2(c.x + d, c.y), ImVec2(c.x, c.y + d), ImVec2(c.x - d, c.y)};
    dl->AddConvexPolyFilled(q, 4, fade(GOLD, a));
    for (int i = 0; i < 4; i++) {  // (the arrows; pressed, a little farther out)
        float dx = dir[i][0], dy = dir[i][1], px = -dy, py = dx, s = hw * .62f, at = r * (lit(i) ? .75f : .72f);
        ImVec2 tip(c.x + dx * at, c.y + dy * at), b0(tip.x - dx * s * 1.25f + px * s, tip.y - dy * s * 1.25f + py * s),
            b1(tip.x - dx * s * 1.25f - px * s, tip.y - dy * s * 1.25f - py * s);
        dl->AddTriangleFilled(tip, b0, b1, fade(lit(i) ? GLOW : INK, a));
    }
}

void stick(ImDrawList *dl, ImVec2 base, ImVec2 knob, float r, float a, bool on) {
    float kr = r * KNOB;
    dl->AddCircleFilled(base, r, fade(IM_COL32(10, 7, 5, 255), a * (on ? .42f : .26f)), 48);
    dl->AddCircle(base, r, fade(BRONZE, a), 48, r * .07f);
    dl->AddCircle(base, r * .955f, fade(GOLD, a * .85f), 48, r * .025f);
    for (int i = 0; i < 8; i++) {  // (eight notches: the directions)
        float an = i * PI * .25f;
        ImVec2 o(base.x + cosf(an) * r * .82f, base.y + sinf(an) * r * .82f);
        float d = r * (i % 2 ? .035f : .06f);
        ImVec2 q[4] = {ImVec2(o.x + cosf(an) * d * 1.6f, o.y + sinf(an) * d * 1.6f), ImVec2(o.x - sinf(an) * d, o.y + cosf(an) * d),
                       ImVec2(o.x - cosf(an) * d * 1.6f, o.y - sinf(an) * d * 1.6f), ImVec2(o.x + sinf(an) * d, o.y - cosf(an) * d)};
        dl->AddConvexPolyFilled(q, 4, fade(GOLD, a * .8f));
    }
    disc(dl, knob, kr, on, a);
    float d = kr * .32f;
    ImVec2 q[4] = {ImVec2(knob.x, knob.y - d), ImVec2(knob.x + d, knob.y), ImVec2(knob.x, knob.y + d), ImVec2(knob.x - d, knob.y)};
    dl->AddConvexPolyFilled(q, 4, fade(GOLD, a));
}

// What a control of the ranged layout shows now: the weapon in hand, the game's Bow Controls, out or sheathed (the editor,
// with no such weapon in hand, shows a bowgun's, out)
void ranged_face(int i, int drawn, Icon *ic, char *letter, size_t size) {
    bool bow = g_ranged == 1, out = drawn == 1;
    uint64_t b = ranged_buttons(i, drawn);
    *ic = i == C_RFIRE ? (bow ? I_BOW : I_SHOT)
        : i == C_RSECOND ? (!out ? I_GATHER : bow ? I_MELEE : I_RELOAD)
        : i == C_RSPECIAL ? (bow ? I_COATING : I_CROUCH)
                          : DEFS[i].icon;
    snprintf(letter, size, "%s", i == C_AMMO_UP || i == C_AMMO_DOWN ? "" : (b & BTN_X) && (b & BTN_A) ? "X+A" : b & BTN_A ? "A"
                                  : b & BTN_X ? "X" : b & BTN_R ? "R" : DEFS[i].letter);
}

void control(ImDrawList *dl, ImFont *font, const Layout &l, int i, float w, float h, const State &s, float a) {
    Geo g = place(l, i, w, h);
    if (i == C_STICK) {
        if (s.stick_on && !s.edit) {
            stick(dl, ImVec2(s.bx, s.by), ImVec2(s.kx, s.ky), g.r, a, true);
            if (s.run) {  // (dashing: a gold ring round the rim, and streaks behind the knob)
                dl->AddCircle(ImVec2(s.bx, s.by), g.r * 1.12f, fade(GLOW, a * .85f), 48, g.r * .07f);
                float dx = s.kx - s.bx, dy = s.ky - s.by, n = std::max(1.0f, hypotf(dx, dy));
                dx /= n, dy /= n;
                for (int k = -1; k <= 1; k++) {
                    ImVec2 o(s.kx - dy * k * g.r * .22f, s.ky + dx * k * g.r * .22f);
                    dl->AddLine(ImVec2(o.x - dx * g.r * .55f, o.y - dy * g.r * .55f), ImVec2(o.x - dx * g.r * .95f, o.y - dy * g.r * .95f),
                                fade(GLOW, a * (k ? .5f : .8f)), g.r * .06f);
                }
            }
        } else {
            stick(dl, g.c, g.c, g.r, a * .8f, false);
        }
        return;
    }
    int mask = 0;
    for (const Finger &f : s.f)
        if (f.kind == F_BUTTON && !f.done) {
            if (f.ctl == i) mask |= i == C_ATTACK || i == C_MPILL ? (f.part == 2 ? 3 : 1 << f.part)
                                  : i == C_CORNER ? (f.part == 2 ? 3 : f.part == 3 ? 4 : 1 << f.part)
                                  : is_dpad(i) ? f.part : 1;
            if (i == C_HEAVY && f.ctl == C_LIGHT && f.part) mask |= 1;  // (the seam between them: both lit)
        }
    if (i == C_ATTACK) { pill(dl, font, g, mask, a); return; }
    if (i == C_MPILL) { menu_pill(dl, font, g, mask, a); return; }
    if (is_tab(i)) { tab(dl, font, g, i == C_ML, DEFS[i].letter, mask != 0, a); return; }
    if (i == C_CORNER) { corner_band(dl, font, g, mask, a); return; }
    if (i == C_HEAVY || i == C_LIGHT) {  // (a great sword, a small one, as on the pill)
        float r = mask ? g.r * .95f : g.r;
        disc(dl, g.c, r, mask != 0, a);
        ImU32 col = fade(mask ? GLOW : INK, a);
        if (i == C_HEAVY) sword_along(dl, ImVec2(g.c.x - PILL_U.x * r * .08f, g.c.y - PILL_U.y * r * .08f), PILL_U, r * 1.55f, r * .36f, r * .72f, col, fade(DEEP, a), true);
        else sword_along(dl, ImVec2(g.c.x - PILL_U.x * r * .05f, g.c.y - PILL_U.y * r * .05f), PILL_U, r * 1.2f, r * .2f, r * .5f, col, fade(DEEP, a), false);
        badge(dl, font, ImVec2(g.c.x + r * .7f, g.c.y + r * .7f), r * .3f, DEFS[i].letter, a);
        ImVec2 at;
        float size;
        if (i == C_LIGHT && separate_seam(l, w, h, &at, &size)) {  // (between the two: both)
            bool both = false;
            for (const Finger &f : s.f) both |= f.kind == F_BUTTON && !f.done && f.ctl == C_LIGHT && f.part;
            float d = size * .55f;
            ImVec2 q[4] = {ImVec2(at.x, at.y - d), ImVec2(at.x + d, at.y), ImVec2(at.x, at.y + d), ImVec2(at.x - d, at.y)};
            dl->AddConvexPolyFilled(q, 4, fade(both ? GLOW : GOLD, a));
        }
        return;
    }
    if (is_dpad(i)) { dpad(dl, g, mask, a); return; }
    if (DEFS[i].icon == I_LETTER) {  // (the game's letter, large, as on the 3DS's face buttons)
        std::string own = is_combo(i) ? combo_text(g_combo[i - C_COMBO]) : "";
        const char *label = is_combo(i) ? (own.empty() ? "+" : own.c_str()) : DEFS[i].letter;
        float r = mask ? g.r * .95f : g.r, fs = r * (is_combo(i) ? .75f : i >= C_ML ? .9f : 1.05f);
        disc(dl, g.c, r, mask != 0, a);
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, label);
        if (ts.x > r * 1.55f) fs *= r * 1.55f / ts.x, ts = font->CalcTextSizeA(fs, FLT_MAX, 0, label);  // (inside the disc)
        dl->AddText(font, fs, ImVec2(g.c.x - ts.x * .5f + fs * .04f, g.c.y - ts.y * .5f + fs * .05f), fade(IM_COL32(0, 0, 0, 255), a * .7f), label);
        dl->AddText(font, fs, ImVec2(g.c.x - ts.x * .5f, g.c.y - ts.y * .52f), fade(mask ? GLOW : INK, a), label);
        return;
    }
    float r = mask ? g.r * .95f : g.r;
    bool lit = mask != 0 || (i == C_SCREEN && g_draw_bottom);
    disc(dl, g.c, r, lit, a);
    Icon ic = DEFS[i].icon;
    char letter[8];
    snprintf(letter, sizeof letter, "%s", DEFS[i].letter);
    if (i == C_TOWN) snprintf(letter, sizeof letter, "%s", T("TOWN"));  // (a plate under it, as START's: what it is)
    if (i == C_ZR && g_draw_skip) ic = I_SKIP;  // (once a quest is complete, ZR taps the touch screen's Skip)
    if (is_ranged(i)) ranged_face(i, s.edit ? 1 : g_game.weapon_drawn, &ic, letter, sizeof letter);
    icon(dl, ic, g.c, i == C_RFIRE ? r * .94f : r, fade(mask || (i == C_SCREEN && g_draw_bottom) ? GLOW : INK, a), fade(DEEP, a));
    float br = std::max(r * (i == C_RFIRE ? .22f : .3f), .021f * h);  // (the large one's as large as the others')
    if (strlen(letter) > 3) badge(dl, font, ImVec2(g.c.x, g.c.y + r * 1.02f), br, letter, a);  // (START, SELECT: a plate below)
    else badge(dl, font, ImVec2(g.c.x + r * .7f, g.c.y + r * .7f), br, letter, a);
    if (i == C_L && !s.edit && lock_on_allowed()) {  // (the lock-on: a padlock by L, a tap on it turns it; lit while on)
        bool on = Port::Hooks::LockOn();
        if (on) {
            float pulse = Port::Hooks::LockOnFollowing() ? .6f + .3f * sinf(SDL_GetTicksNS() / 1e9f * 4) : .45f;
            dl->AddCircle(g.c, r * 1.16f, fade(GLOW, a * pulse), 40, r * .09f);
        }
        padlock(dl, ImVec2(g.c.x - r * .72f, g.c.y - r * .62f), std::max(r * .42f, .018f * h), on, fade(on ? GLOW : INK, a),
                fade(DEEP, a));
    }
}

// a plate in the game's window style: wine-dark, a black edge, red and green rules top and bottom
void plate(ImDrawList *dl, Rect r, float line, float a, bool lit) {
    dl->AddRectFilled(ImVec2(r.x0, r.y0), ImVec2(r.x1, r.y1), fade(lit ? IM_COL32(96, 52, 30, 255) : WINE, a * .94f));
    dl->AddRect(ImVec2(r.x0, r.y0), ImVec2(r.x1, r.y1), fade(IM_COL32(0, 0, 0, 255), a), 0, line, 0);
    dl->AddRectFilled(ImVec2(r.x0, r.y0), ImVec2(r.x1, r.y0 + line), fade(RED, a));
    dl->AddRectFilled(ImVec2(r.x0, r.y0 + line), ImVec2(r.x1, r.y0 + line * 1.7f), fade(MOSS, a));
    dl->AddRectFilled(ImVec2(r.x0, r.y1 - line * 1.7f), ImVec2(r.x1, r.y1 - line), fade(MOSS, a));
    dl->AddRectFilled(ImVec2(r.x0, r.y1 - line), ImVec2(r.x1, r.y1), fade(RED, a));
}

void text_center(ImDrawList *dl, ImFont *font, float size, ImVec2 c, ImU32 col, const char *t, float w = 0) {
    ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0, t);
    if (w > 0) c.x = std::clamp(c.x, ts.x * .5f + size * .3f, w - ts.x * .5f - size * .3f);  // (inside the screen)
    dl->AddText(font, size, ImVec2(c.x - ts.x * .5f + size * .05f, c.y - ts.y * .5f + size * .06f), IM_COL32(0, 0, 0, 200), t);
    dl->AddText(font, size, ImVec2(c.x - ts.x * .5f, c.y - ts.y * .5f), col, t);
}

void dashed_rect(ImDrawList *dl, Rect r, ImU32 col, float t) {
    float seg = t * 5;
    for (float x = r.x0; x < r.x1; x += seg * 2) {
        dl->AddLine(ImVec2(x, r.y0), ImVec2(std::min(x + seg, r.x1), r.y0), col, t);
        dl->AddLine(ImVec2(x, r.y1), ImVec2(std::min(x + seg, r.x1), r.y1), col, t);
    }
    for (float y = r.y0; y < r.y1; y += seg * 2) {
        dl->AddLine(ImVec2(r.x0, y), ImVec2(r.x0, std::min(y + seg, r.y1)), col, t);
        dl->AddLine(ImVec2(r.x1, y), ImVec2(r.x1, std::min(y + seg, r.y1)), col, t);
    }
}

void draw_editor(ImDrawList *dl, ImFont *font, ImFont *title, const State &s, float w, float h) {
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(w, h), IM_COL32(0, 0, 0, 96));
    // the game's parts the controls had better leave free: the item bar (a tap uses the item, a swipe turns it), and the
    // touch screen's corner where it last showed (the controls it covers move aside while it shows); named inside, at
    // their bottom edge, out of the controls' way
    Rect panel, btn[E_COUNT];
    editor_panel(w, h, &panel, btn);
    Rect item = hud_rect(s, HP_ITEMS);
    if (g_bottom_seen.ok) {
        dashed_rect(dl, g_bottom_seen, IM_COL32(222, 186, 96, 120), .003f * h);
        float right = item.ok && item.x0 < g_bottom_seen.x1 ? item.x0 : g_bottom_seen.x1;
        text_center(dl, font, .022f * h, ImVec2((g_bottom_seen.x0 + right) * .5f, g_bottom_seen.y1 - .022f * h),
                    fade(INK, .75f), T("Touch screen"), w);
    }
    auto on_panel = [&](ImVec2 c, float tw, float th) {
        return c.x + tw * .5f > panel.x0 && c.x - tw * .5f < panel.x1 && c.y + th * .5f > panel.y0 && c.y - th * .5f < panel.y1;
    };
    std::vector<Rect> used;  // (the names and sizes put down so far)
    auto label_rect = [&](ImVec2 c, float tw, float th) {
        c.x = std::clamp(c.x, tw * .5f + th * .3f, w - tw * .5f - th * .3f);  // (where text_center puts it)
        return Rect{c.x - tw * .5f, c.y - th * .5f, c.x + tw * .5f, c.y + th * .5f, true};
    };
    auto clear_at = [&](ImVec2 c, float tw, float th, int self) {
        Rect r = label_rect(c, tw, th);
        if (r.y0 < 0 || r.y1 > h || on_panel(ImVec2((r.x0 + r.x1) * .5f, c.y), tw, th)) return false;
        auto overlap = [&](const Rect &b) { return r.x1 > b.x0 && r.x0 < b.x1 && r.y1 > b.y0 && r.y0 < b.y1; };
        for (const Rect &u : used)
            if (overlap(u)) return false;
        for (int j = 0; j < C_LAID; j++) {
            if (j == self || !in_mode(j, false)) continue;
            Geo o = place(s.layout, j, w, h);
            if (j == C_ATTACK || j == C_CORNER) {  // (thin and tilted or bent: the label's corners, edges and middle)
                for (int k = 0; k < 9; k++) {
                    float px = r.x0 + (k % 3) * tw * .5f, py = r.y0 + (k / 3) * th * .5f;
                    if (j == C_ATTACK ? pill_distance(o, px, py) < o.r * 1.05f : band_distance(o, px, py, nullptr) < o.r * BAND_T * 1.05f)
                        return false;
                }
                continue;
            }
            if (overlap(bounds(s.layout, j, w, h))) return false;
        }
        return true;
    };
    auto place_label = [&](const ImVec2 *cand, int n, float tw, float th, int self) {
        int pick = 0;
        for (int k = 0; k < n; k++)
            if (clear_at(cand[k], tw, th, self)) { pick = k; break; }
        used.push_back(label_rect(cand[pick], tw, th));
        return cand[pick];
    };
    // the game's HUD: each part a gold dashed frame round its art (the selected one lit and pulsing), its name on a dark
    // plate below it (above it at the screen's bottom edge); its names first, so the controls' keep clear of them
    float pulse_t = SDL_GetTicksNS() / 1e9f;
    for (int k = 0; k < UiGameState::HUD_PARTS; k++) {
        Rect r = hud_rect(s, k);
        if (!hud_offered(k) || !r.ok) continue;
        bool sel = s.sel == ZONE_SEL + k;
        float m = .006f * h;
        Rect f = {r.x0 - m, r.y0 - m, r.x1 + m, r.y1 + m, true};
        dl->AddRectFilled(ImVec2(f.x0, f.y0), ImVec2(f.x1, f.y1), sel ? IM_COL32(255, 214, 92, 40) : IM_COL32(222, 186, 96, 22));
        dashed_rect(dl, f, sel ? fade(GLOW, .65f + .35f * sinf(pulse_t * 6)) : IM_COL32(222, 186, 96, 210), (sel ? .005f : .0035f) * h);
        const char *t = T(HUD_NAME[k]);
        float fs = .022f * h, tw = font->CalcTextSizeA(fs, FLT_MAX, 0, t).x;
        ImVec2 c((f.x0 + f.x1) * .5f, f.y1 + fs * .9f);
        if (c.y + fs > h) c.y = f.y0 - fs * .9f;
        c.x = std::clamp(c.x, tw * .5f + fs * .6f, w - tw * .5f - fs * .6f);
        dl->AddRectFilled(ImVec2(c.x - tw * .5f - fs * .35f, c.y - fs * .62f), ImVec2(c.x + tw * .5f + fs * .35f, c.y + fs * .62f),
                          IM_COL32(28, 20, 15, 190), fs * .3f);
        text_center(dl, font, fs, c, sel ? GLOW : INK, t, w);
        used.push_back(label_rect(c, tw + fs * .7f, fs * 1.24f));
    }
    for (int i = 0; i < C_LAID; i++) {
        if (!in_mode(i, false)) continue;  // (the controls not in this layout stay in the file only)
        control(dl, font, s.layout, i, w, h, s, 1.0f);
        Geo g = place(s.layout, i, w, h);
        float rr = i == C_ATTACK ? g.r * PILL_HALF * .75f + g.r : g.r;
        if (i == s.sel) {
            float t = SDL_GetTicksNS() / 1e9f, pulse_a = .65f + .35f * sinf(t * 6);
            if (i == C_ATTACK) {
                capsule_path(dl, g.c, g.r * PILL_HALF, g.r * 1.22f);
                dl->PathStroke(fade(GLOW, pulse_a), .006f * h, ImDrawFlags_Closed);
            } else if (i == C_CORNER) {
                band_path(dl, g, 0, g.r * BAND_LEN, g.r * BAND_T * 1.2f, true, true);
                dl->PathStroke(fade(GLOW, pulse_a), .006f * h, ImDrawFlags_Closed);
            } else {
                dl->AddCircle(g.c, g.r * 1.25f, fade(GLOW, pulse_a), 48, .006f * h);
            }
        }
        if (i == C_CORNER) {  // (its name above its upright part, its size beside it)
            ImVec2 top = band_point(g, g.r * BAND_LEN, 0);
            float ny = top.y - g.r * BAND_T - .03f * h;
            text_center(dl, font, .024f * h, ImVec2(top.x - g.r * .22f, ny), i == s.sel ? GLOW : INK, T(DEFS[i].name), w);
            used.push_back(label_rect(ImVec2(top.x - g.r * .22f, ny), font->CalcTextSizeA(.024f * h, FLT_MAX, 0, T(DEFS[i].name)).x, .024f * h));
            if (i == s.sel) {
                char pct[16];
                snprintf(pct, sizeof pct, "%d%%", (int)lroundf(s.layout.scale[i] * 100));
                text_center(dl, font, .03f * h, ImVec2(top.x - g.r * .22f, ny - .035f * h), GLOW, pct, w);
            }
            continue;
        }
        // its name below it, above it, beside it on the screen's inner side or the outer: the first place that is clear
        // of the panel, of the other controls and of the names put down before; the selected one's size the same way
        const char *t = T(DEFS[i].name);
        float fs = .024f * h, side = DEFS[i].right ? -1.0f : 1.0f;
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, t);
        float drop = i == C_MENU || i == C_HSELECT ? g.r * .5f : 0;  // (START's and SELECT's plates below them)
        bool top_row = i == C_SETTINGS || i == C_LAYOUT;              // (that row's names side by side above it)
        ImVec2 below(g.c.x, g.c.y + rr + .03f * h + drop), above(g.c.x, g.c.y - rr - .03f * h);
        ImVec2 inner(g.c.x + side * (g.r * 1.3f + ts.x * .5f), g.c.y), outer(g.c.x - side * (g.r * 1.3f + ts.x * .5f), g.c.y);
        ImVec2 cand[4] = {top_row ? above : below, top_row ? below : above, inner, outer};
        ImVec2 at = place_label(cand, 4, ts.x, fs, i);
        text_center(dl, font, fs, at, i == s.sel ? GLOW : INK, t, w);
        if (i == s.sel) {
            char pct[16];
            snprintf(pct, sizeof pct, "%d%%", (int)lroundf(s.layout.scale[i] * 100));
            float pf = .03f * h, pw = font->CalcTextSizeA(pf, FLT_MAX, 0, pct).x;
            ImVec2 pc[4] = {at.y > g.c.y ? ImVec2(g.c.x, g.c.y - rr - .035f * h) : ImVec2(g.c.x, g.c.y + rr + .035f * h),
                            ImVec2(g.c.x + side * (g.r * 1.3f + pw * .5f), g.c.y), ImVec2(g.c.x - side * (g.r * 1.3f + pw * .5f), g.c.y),
                            ImVec2(at.x, at.y + .035f * h)};
            text_center(dl, font, pf, place_label(pc, 4, pw, pf, i), GLOW, pct, w);
        }
    }
    float line = .004f * h;
    plate(dl, panel, line, 1.0f, false);
    text_center(dl, title, .036f * h, ImVec2((panel.x0 + panel.x1) * .5f, panel.y0 + .045f * h), GLOW, T("Touch controls"));
    text_center(dl, font, .022f * h, ImVec2((panel.x0 + panel.x1) * .5f, panel.y0 + .08f * h), INK,
                T(s.sel >= ZONE_SEL ? "Drag it where you want it in your hunts"
                  : s.sel >= 0 ? "Drag to move  -  pinch or use - / + for its size"
                               : "Tap a control or a part of the game's screen to pick it, drag it to move it"));
    static const char *const STYLE_LABEL[STYLE_COUNT] = {"Attacks: pill", "Attacks: buttons", "Attacks: corner"};
    const char *const LABEL[E_COUNT] = {"-", "+", ranged_layout() ? "Layout: ranged" : "Layout: melee", STYLE_LABEL[s.layout.style],
                                        "Defaults", "Cancel", "Save"};
    for (int i = 0; i < E_COUNT; i++) {
        bool off = ((i == E_SMALLER || i == E_BIGGER) && s.sel < 0) || (i == E_STYLE && ranged_layout());
        bool lit = false;
        for (const Finger &f : s.f)
            if (f.kind == F_MENU && btn[i].in(f.x, f.y)) lit = true;
        plate(dl, btn[i], line * .8f, off ? .45f : 1.0f, lit);
        float fs = (i < 2 ? .05f : .028f) * h, room = (btn[i].x1 - btn[i].x0) * .92f, wide = font->CalcTextSizeA(fs, FLT_MAX, 0, T(LABEL[i])).x;
        if (wide > room) fs *= room / wide;  // (a narrow screen, a long word: on one line, smaller)
        text_center(dl, font, fs, ImVec2((btn[i].x0 + btn[i].x1) * .5f, (btn[i].y0 + btn[i].y1) * .5f),
                    fade(i == E_SAVE ? GLOW : INK, off ? .45f : 1.0f), T(LABEL[i]));
    }
}

// The item bar's answer to a swipe: gold chevrons over it pointing the way it turned, fading out (the bar itself is the
// game's: nothing stays drawn over it)
void item_hint(ImDrawList *dl, const State &s, const Rect &item, float h, float a, uint64_t now) {
    if (!s.item_flash || now >= s.item_flash + 150 * MS) return;
    float t = now < s.item_flash ? 1.0f : 1.0f - (now - s.item_flash) / (150.0f * MS);  // (lit, then fading)
    if (s.item_dir == 0) {  // (a tap, the item used: a gold glow round the bar)
        float r = .012f * h;
        dl->AddRect(ImVec2(item.x0, item.y0), ImVec2(item.x1, item.y1), fade(GLOW, t * .8f), r, .006f * h);
        dl->AddRect(ImVec2(item.x0 - r * .6f, item.y0 - r * .6f), ImVec2(item.x1 + r * .6f, item.y1 + r * .6f), fade(GLOW, t * .3f), r, .004f * h);
        return;
    }
    float d = (float)s.item_dir, ch = .05f * h, cx = (item.x0 + item.x1) * .5f + d * .03f * h, cy = (item.y0 + item.y1) * .5f;
    for (int k = 0; k < 2; k++) {  // (two chevrons, the leading one brighter)
        float x = cx + d * k * ch * .7f;
        ImVec2 p[3] = {ImVec2(x - d * ch * .45f, cy - ch), ImVec2(x + d * ch * .35f, cy), ImVec2(x - d * ch * .45f, cy + ch)};
        dl->AddPolyline(p, 3, fade(IM_COL32(0, 0, 0, 255), t * .6f), ch * .4f, 0);
        dl->AddPolyline(p, 3, fade(GLOW, t * (k ? 1.0f : .6f)), ch * .24f, 0);
    }
}

// The target camera's icon: a flash when tapped; with the lock-on allowed, a padlock on a dark disc at its lower left
// (open, or closed and lit while the lock-on is on: a long press on the icon turns it), and while it is on a gold ring
// round the icon, pulsing while the camera follows a monster (the MHGU port's)
void target_hint(ImDrawList *dl, const Rect &z, float h, float a, uint64_t now, uint64_t flash) {
    ImVec2 c((z.x0 + z.x1) * .5f, (z.y0 + z.y1) * .5f);
    float r = std::max(z.x1 - z.x0, z.y1 - z.y0) * .5f;
    if (now < flash) dl->AddCircle(c, r * .95f, fade(GLOW, a * (flash - now) / (250.0f * MS)), 40, .006f * h);
    if (!lock_on_allowed()) return;
    bool on = Port::Hooks::LockOn();
    if (on) {
        float p = Port::Hooks::LockOnFollowing() ? .6f + .3f * sinf(now / 1e9f * 4) : .45f;
        dl->AddCircle(c, r * 1.02f, fade(GLOW, a * p), 40, .005f * h);
    }
    ImVec2 pc(z.x0 + .004f * h, z.y1 - .004f * h);
    dl->AddCircleFilled(pc, .02f * h, fade(DEEP, a * .85f), 20);
    dl->AddCircle(pc, .02f * h, fade(GOLD, a), 20, .003f * h);
    padlock(dl, pc, .024f * h, on, fade(on ? GLOW : INK, a), fade(DEEP, a));
}

ImFont *g_title_font;  // (the editor's title: a serif, as the game's own titles; else the main font)

void draw(State &s, float w, float h) {
    ImFont *font = ImGui::GetFont();
    ImFont *title = g_title_font ? g_title_font : font;
    ImDrawList *dl = ImGui::GetBackgroundDrawList();  // (under the app's own windows: the settings, later)
    g_draw_menu = s.menu, g_draw_bottom = g_bottom.ok, g_draw_skip = Port::Hooks::SkipOffered();
    if (s.edit) {
        draw_editor(dl, font, title, s, w, h);
        return;
    }
    uint64_t now = SDL_GetTicksNS();
    float idle = s.last_ns ? (now - s.last_ns) / 1e9f : 99.0f;
    float a = g_set.opacity * (idle > 5.0f ? .65f : 1.0f);  // (dimmer after a while untouched)
    if (!s.visible) {  // (hidden: the settings button alone, a little fainter)
        if (!covered(s.layout, C_SETTINGS, w, h)) control(dl, font, s.layout, C_SETTINGS, w, h, s, a * .75f);
        return;
    }
    {  // (the menus' d-pad fades in and out, 150 ms)
        static uint64_t last = 0;
        const float dt = last ? std::min((now - last) / 1e9f, .1f) : 0;
        last = now;
        const float target = in_mode(C_MDPAD, s.menu) ? 1.0f : 0.0f;
        g_mdpad_a = target > g_mdpad_a ? std::min(target, g_mdpad_a + dt / .15f) : std::max(target, g_mdpad_a - dt / .15f);
    }
    for (int i = 0; i < C_COUNT; i++) {
        const bool on = in_mode(i, s.menu);
        if (i == C_MDPAD && (on || g_mdpad_a > 0) && !covered(s.layout, i, w, h)) control(dl, font, s.layout, i, w, h, s, a * g_mdpad_a);
        // (in a menu of the game's the player's buttons stay, faded so its window reads and taps under them; the menu's
        // own additions at full strength)
        else if (on && !covered(s.layout, i, w, h))
            control(dl, font, s.layout, i, w, h, s, s.menu && i != C_MSELECT && i != C_MY ? a * .5f : a);
    }
    Rect item = item_zone(s.menu);
    if (item.ok) item_hint(dl, s, item, h, a, now);
    Rect target = target_zone(s.menu);
    if (target.ok) target_hint(dl, target, h, a, now, s.target_flash);
}

}  // namespace

void touch_init(SDL_Renderer *renderer, const std::string &data_dir) {
    g_renderer = renderer;
    if (!data_dir.empty()) g_layout_path = data_dir + "/touch_layout.txt";
    for (int b = 0; b < TB_COUNT; b++) g_set.show[b] = TB_DEFAULT[b];
    g_s.visible = true;  // (until the first frame says)
    ImGuiIO &io = ImGui::GetIO();
    g_title_font = io.Fonts->Fonts.Size > 1 ? io.Fonts->Fonts[1] : nullptr;  // (ui.cpp: the serif second)
}

bool touch_event(const SDL_Event &e, const UiScreens &screens) {
    if (e.type != SDL_EVENT_FINGER_DOWN && e.type != SDL_EVENT_FINGER_MOTION && e.type != SDL_EVENT_FINGER_UP &&
        e.type != SDL_EVENT_FINGER_CANCELED)
        return false;
    ensure_loaded(g_s);
    window_size(g_s);
    set_screens(screens);
    update_shift(g_s);
    return finger_event(e);
}

void touch_frame(int width, int height, const UiScreens &screens, bool controller, UiInput &out) {
    State &s = g_s;
    ensure_loaded(s);
    if (width <= 0 || height <= 0) return;
    if ((s.w != width || s.h != height) && s.w > 0) {  // (the window turned or changed: the fingers start over)
        touch_reset();
        fprintf(stderr, "[touch] window %dx%d\n", width, height);
    }
    s.w = (float)width, s.h = (float)height;
    set_screens(screens);
    update_shift(s);
    frame_input(s, controller, out);
    update_shift(s);  // (the layout may have changed: the other one's controls)
    // drawn in the window's pixels; ImGui's display may count in other units (a display scale): scaled to them after
    ImGuiIO &io = ImGui::GetIO();
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    int v0 = dl->VtxBuffer.Size;
    float kx = io.DisplaySize.x > 0 ? io.DisplaySize.x / s.w : 1, ky = io.DisplaySize.y > 0 ? io.DisplaySize.y / s.h : 1;
    bool scaled = fabsf(kx - 1) > 1e-3f || fabsf(ky - 1) > 1e-3f;
    if (scaled) dl->PushClipRect(ImVec2(0, 0), ImVec2(std::max(s.w, io.DisplaySize.x), std::max(s.h, io.DisplaySize.y)));
    draw(s, s.w, s.h);
    if (scaled) {
        dl->PopClipRect();
        for (int i = v0; i < dl->VtxBuffer.Size; i++) dl->VtxBuffer[i].pos.x *= kx, dl->VtxBuffer[i].pos.y *= ky;
        static bool said;
        if (!said) said = true, fprintf(stderr, "[touch] ImGui's display %.0fx%.0f for a window of %dx%d\n", io.DisplaySize.x,
                                        io.DisplaySize.y, width, height);
    }
}

void touch_reset() {
    State &s = g_s;
    for (Finger &f : s.f) f.kind = F_NONE;
    for (Pulse &p : s.pulse) p = Pulse{};
    for (Seq *q : {&s.item, &s.ammo, &s.cursor}) q->n = 0, q->phase = 0;
    s.bar_open = false, s.ammo_open.n = 0, s.ammo_open.phase = 0;
    if (s.bar_held) Port::Hooks::HoldItemBarOpen(false), s.bar_held = false;
    s.use_until = s.use_at = 0;
    s.stick_on = false, s.held = 0, s.cam_dx = s.cam_dy = 0, s.pinch = false, s.run = false;
    s.cs_x = s.cs_y = 0;
}

void touch_set_game_state(const UiGameState &state) { g_game = state; }

bool touch_pad_button(const SDL_Event &e) {
    if (e.type != SDL_EVENT_GAMEPAD_BUTTON_DOWN || e.gbutton.button != SDL_GAMEPAD_BUTTON_RIGHT_STICK) return false;
    if (!lock_on_allowed()) return false;
    lock_toggle("right stick click");
    return true;
}

bool touch_bottom_wanted() {
    if (g_s.edit) return false;  // (the editor shows every control)
    return g_s.visible ? g_bottom_touch : g_bottom_pad;
}

void touch_set_bottom_default(bool shown) {
    g_bottom_touch = g_bottom_pad = shown;
    fprintf(stderr, "[touch] the touch screen's corner starts %s\n", shown ? "shown" : "hidden");
}

void touch_toggle_bottom() {
    bool &b = g_s.visible ? g_bottom_touch : g_bottom_pad;
    b = !b;
    fprintf(stderr, "[touch] the touch screen's corner %s\n", b ? "shown" : "hidden");
}

std::string touch_setting(const std::string &key) {
    State &s = g_s;
    ensure_loaded(s);
    char text[32];
    if (key == "controls") return CONTROLS_KEY[g_set.controls];
    if (key == "opacity") return snprintf(text, sizeof text, "%.2f", g_set.opacity), text;
    if (key == "camera_speed") return snprintf(text, sizeof text, "%.2f", g_set.camera), text;
    if (key == "attack_style") return std::to_string(s.layout.style);
    if (!key.compare(0, 5, "combo") && key.size() == 6 && key[5] >= '1' && key[5] <= '0' + COMBOS) return combo_text(g_combo[key[5] - '1']);
    if (!key.compare(0, 5, "show_"))
        for (int b = 0; b < TB_COUNT; b++)
            if (key.compare(5, std::string::npos, TB_KEY[b]) == 0) return g_set.show[b] ? "1" : "0";
    return "";
}

void touch_set_setting(const std::string &key, const std::string &value) {
    State &s = g_s;
    ensure_loaded(s);
    const float x = strtof(value.c_str(), nullptr);
    bool known = true;
    if (key == "controls") {
        for (int m = 0; m < 3; m++)
            if (value == CONTROLS_KEY[m]) g_set.controls = m;
    } else if (key == "opacity") {
        g_set.opacity = std::clamp(x, .2f, 1.0f);
    } else if (key == "camera_speed") {
        g_set.camera = std::clamp(x, .2f, 5.0f);
    } else if (key == "attack_style") {
        // the editor's way from one style to the next (what the corner band would cover moves inside its bend), which
        // takes the controls' places from where they were before: the layout as it is now
        int style = std::clamp((int)x, 0, STYLE_COUNT - 1);
        if (s.w > 0 && s.h > 0 && !s.edit) {
            s.saved = s.layout;
            for (int i = 0; i < STYLE_COUNT && s.layout.style != style; i++) next_style(s);
        }
        s.layout.style = style, g_style = style;
    } else if (!key.compare(0, 5, "combo") && key.size() == 6 && key[5] >= '1' && key[5] <= '0' + COMBOS) {
        g_combo[key[5] - '1'] = combo_mask(value);
    } else if (!key.compare(0, 5, "show_")) {
        known = false;
        for (int b = 0; b < TB_COUNT; b++)
            if (key.compare(5, std::string::npos, TB_KEY[b]) == 0) g_set.show[b] = x != 0, known = true;
    } else
        known = false;
    if (!known) return;
    if (!s.edit) s.saved = s.layout;  // (the editor, when it opens, starts from here)
    for (Finger &f : s.f)  // (a control gone from under a finger lets go of it)
        if (f.kind == F_BUTTON) f.done = true;
    recompute_held(s);
    save_layout(s.edit ? s.saved : s.layout);
}

void touch_edit_begin() {
    State &s = g_s;
    ensure_loaded(s);
    if (!s.edit) edit_begin(s);
}

void touch_haptic_tap() {
    haptics_tap();
}
