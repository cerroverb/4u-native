// Touch controls for phones, in the Monster Hunter style (the MHGU port's runtime/touch.cpp, MIT, made over for the
// 3DS's buttons): bronze-rimmed dark discs with cream glyphs and the game's button letter on a small badge.
//   - left thumb: a stick fixed at its place (the knob follows the thumb inside the rim); a light tilt walks, a full
//     one runs, and the thumb well past the rim dashes (R held, with the weapon sheathed: show_stick_run); lifted
//     inside the small circle with a padlock that shows above the stick (lit while the thumb is on it), the run locks
//     (Call of Duty's sprint lock) and goes on forward until the stick or the circle is touched again, the weapon comes
//     out or a menu opens. With show_stick_run 0 a Run button above the attacks runs while held
//   - right thumb: the attack pill (A on its upper half, X on the lower, both on its seam: the 3DS's X draws and
//     attacks, A attacks, gathers, carves and talks), R guard / dash beside its lower end, B evade / crouch beside it,
//     Y sheathe (or the item, sheathed) left of it; on an arc above it ZR (the kick; once a quest is complete, Skip:
//     the port's kick and skip options), ZL (the target camera: the port's target option, the touch screen's panel)
//     and L (the camera behind the hunter, or on the target; held, the lock-on: the camera keeps turning to the monster,
//     as in the newer games, until held again; a tap on its padlock too, and a controller's right stick click). START
//     (the game's menu), the touch screen's corner and the settings at the left edge
//   - in a menu of the game's the layout stays, faded so that the window reads under it, and the menu pad comes in
//     under the right thumb (the MHGU port's, the same place, look and timing): a small pad that is the d-pad (a wide
//     dead middle, one direction at a time, the knob under the thumb), L and R as tabs above it, a small SELECT below
//     it while the hunt's is hidden; the buttons its tabs cover go while it shows. The stick moves the cursor too (the
//     circle pad), and a drag anywhere else steps it, a step every so far, as a list on a phone
//   - the game's own HUD is touched where it is drawn (the hooks say where: Port::Hooks::GameState::hud). The item bar:
//     a tap uses the item (Y; with the weapon out, Y puts it away and the item is used once it is away), a swipe turns
//     the bar (the hooks take the game's own step, as the touch screen's bar does: the strip follows the finger; no L,
//     which would turn the camera too), a long press holds it open with its neighbours (and a bow's coatings) while a
//     swipe turns it; with a bow or a bowgun, up or down changes the ammo or the coating (L held with X or B). The
//     target camera's icon (the single-screen mod's, on the top screen) takes no finger: while the lock-on is on a
//     gold ring goes round it (the padlock on L turns the lock-on)
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
#include "controls.h"
#include "town.h"
#include "hooks.h"
#include "imgui.h"
#include "mh4n/host.h"
#include "mh4n/present.h"
#include "mh4n/registry.h"
#include "mh4n/runtime.h"

namespace {

const float PI = 3.14159265f;
const float ITEM_ARROW_MIN = .03f;  // (the item bar's arrows at their smallest beside a bar by the screen's edge: place_raw)
const uint64_t MS = 1000000ull;
// the aim's tap: let go within AIM_TAP (the MHGU port's), it holds the aim open
const uint64_t AIM_TAP = 180 * MS;

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
       TB_RANGED, TB_FIRE, TB_SECOND, TB_AIM, TB_RSPECIAL, TB_AMMO, TB_MDPAD, TB_ITEMARROWS,
       TB_STICKRUN, TB_GUARDSLIDE,  // (two of the controls' ways, kept with them: the stick's run, R kept as Guard slides)
       TB_COUNT };
const char *const TB_KEY[TB_COUNT] = {"attacks", "dodge", "guard", "sheathe", "start", "select", "dpad", "l", "zl", "zr",
                                      "screen", "stick", "ranged", "fire", "second", "aim", "ranged_special", "ammo",
                                      "menu_dpad", "item_arrows", "stick_run", "guard_slide"};
// (off: SELECT and the d-pad in a hunt, which MH4U's hunts don't need. The item bar has no arrows: a tap on it uses the
// item, a swipe turns it, a long press opens it)
const bool TB_DEFAULT[TB_COUNT] = {1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0};
struct Settings {
    int controls = TOUCH_AUTO;
    float camera = 1.0f;   // the camera's turn for a drag (1: about 90 degrees a screen height)
    float opacity = .8f;   // of the controls, .2 .. 1
    bool show[TB_COUNT];
    bool debug = false;    // (debug 1: what each finger lands on, in the log)
};
Settings g_set;
bool show(int b) { return b < 0 || g_set.show[b]; }
// The layout editor's panel: its middle where the player dragged it (fractions of the screen's width from the left and
// of its height from the top; below 0 its usual place, a little above the middle) and its size (EPANEL_MIN to 100 %).
// Kept in the file however the editor closes, Save or Cancel (editor_panel in touch_layout.txt: the MHGU port's the same)
struct EditPanel {
    float x = -1.0f, y = -1.0f, k = 1.0f;
    bool operator!=(const EditPanel &o) const { return x != o.x || y != o.y || k != o.k; }
};
const float EPANEL_MIN = .6f;
// (its line in the file, save_layout's and save_panel's, under this note)
const char *const EPANEL_NOTE = "# the layout editor's panel: its middle (the screen's width from the left, its height from "
                                "the top; -1 -1 its usual place), size\n";
void epanel_line(char out[80], const EditPanel &ep) { snprintf(out, 80, "editor_panel %.4f %.4f %.2f\n", ep.x, ep.y, ep.k); }
EditPanel g_epanel;

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
bool g_menu_layout;     // a menu of the game's is open (a known change of g_game.menu)
int g_menu_known = -1;  // g_game.menu at its last change
uint64_t g_menu_on_ns;  // when the last one opened (SDL ticks: the menu pad takes a finger a moment after, mpad_ready)

// ------------------------------------------------------------------ the controls and their default places
enum Ctl {
    C_ATTACK, C_DODGE, C_R, C_ZR, C_ZL, C_L, C_MENU, C_SCREEN, C_SETTINGS, C_SHEATHE, C_LAYOUT, C_HEAVY, C_LIGHT,
    C_CORNER, C_HSELECT, C_HDPAD,
    C_RFIRE, C_RSECOND, C_RAIM, C_RSPECIAL, C_RDODGE, C_RSHEATHE, C_AMMO_UP, C_AMMO_DOWN,  // (a bow's or a bowgun's)
    C_TOWN,                                                                                // (a town's: town.h)
    C_MPAD, C_MY,                      // (the game's menus: the menu pad, and Y where the layout has none)
    C_ITEML, C_ITEMR,                  // (the item bar's arrows, in a hunt)
    C_RUNLOCK,                         // (the sprint lock's circle, laid out in the editor)
    C_RUN,                             // (R while held, sheathed: show_stick_run 0)
    C_COMBO, C_COMBO_END = C_COMBO + 5,                                                    // (the player's combined buttons)
    C_STICK,                                                                               // (laid out: touch_layout.txt)
    C_ML, C_MR, C_MSELECT,  // (the menu pad's: its page tabs above it, as the shoulders, and SELECT below it; placed from it)
    C_COUNT
};
const int C_LAID = C_STICK + 1;
// The menu pad (C_MPAD), in its base's radius: a thumb within MPAD_GRAB is its, before anything else (a tap on its
// edge is a step that way, never a tab); a direction comes on past MPAD_ON and stays down to MPAD_OFF (the MHGU port's)
const float MPAD_GRAB = 1.35f, MPAD_ON = .35f, MPAD_OFF = .25f;
// its group, in screen heights at its size 1, from its centre (y up): the tabs' centres, SELECT's; how far the group
// reaches each side, below (SELECT's plate) and above, so that it stays on the screen whole. The tabs' boxes (TAB_W x
// TAB_H, a thumb's slop more) stay outside the grab ring
const float MPAD_TAB_X = .085f, MPAD_TAB_Y = .165f, MPAD_SELECT_X = -.125f, MPAD_SELECT_Y = -.075f;
const float MPAD_HALF_W = .175f, MPAD_BELOW = .125f, MPAD_ABOVE = .199f;
// It comes in as a menu opens (MPAD_IN) and goes as it closes: held MPAD_HOLD, then out in MPAD_OUT; a finger takes it
// MPAD_TAKE after the menu opened (not one that landed there as it opened, when nothing showed yet)
const uint64_t MPAD_IN = 120 * MS, MPAD_HOLD = 90 * MS, MPAD_OUT = 80 * MS, MPAD_TAKE = 30 * MS;
enum Icon { I_ATTACK, I_DODGE, I_SHIELD, I_KICK, I_TARGET_SELECT, I_CAMERA, I_MENU, I_SCREEN, I_GEAR, I_SHEATHE, I_LAYOUT,
            I_STICK, I_DPAD, I_LETTER, I_SELECT, I_SHOT, I_BOW, I_RELOAD, I_SCOPE, I_CROUCH, I_COATING, I_MELEE, I_GATHER,
            I_UP, I_DOWN, I_SKIP, I_TOWN, I_LEFT, I_RIGHT, I_RUN };

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
    // while a menu of the game's is open (one layout everywhere: the menus add what it lacks there): the menu pad under
    // the right thumb, above the attack pill's upper end (A) and right of Y, its tabs above it and SELECT at its lower
    // left, from the screen's right edge like them (the MHGU port's place, as far from the pill); Y where the sheathe
    // button is unless that shows. The pad's key changed with its place (it was a small d-pad above the stick,
    // "menu_dpad"): a layout saved before has it at the default
    {"menupad", "Menu pad", I_DPAD, "", 0, true, .21f, .65f, .08f},
    {"menu_y", "Y (menus)", I_LETTER, "Y", BTN_Y, true, .44f, .54f, .046f},
    // the item bar's arrows, one each side of the bar (place_raw; these places only mark them as not placed by the
    // player, and are where they show while no bar is known): a tap turns it an item, held it repeats
    {"item_left", "Item bar: previous", I_LEFT, "", 0, true, .64f, .085f, .052f},
    {"item_right", "Item bar: next", I_RIGHT, "", 0, true, .565f, .085f, .052f},
    // the sprint lock's circle (show_stick_run): this place only marks it as not placed by the player (it is then LOCK_AT
    // above the stick, wherever the stick is: lock_spot); placed in the editor it stays where it was put. Its size: LOCK_R
    // of the stick's radius, times its own. The MHGU port's the same
    {"run_lock", "Sprint lock", I_RUN, "", 0, false, .29f, .546f, .0483f},
    // run: R while held, with the weapon sheathed (show_stick_run 0: the stick only walks then), above the attacks at the
    // edge, where the MHGU port has it from the pill (clear of the menu pad, which the editor shows with it, and of ZR)
    {"run_button", "Run  R", I_RUN, "R", 0, true, .075f, .66f, .05f},
    // the combined buttons (shown once the settings give them buttons), in two columns left of the attacks
    {"combo1", "Combined button 1", I_LETTER, "", 0, true, .62f, .30f, .05f},
    {"combo2", "Combined button 2", I_LETTER, "", 0, true, .62f, .44f, .05f},
    {"combo3", "Combined button 3", I_LETTER, "", 0, true, .62f, .58f, .05f},
    {"combo4", "Combined button 4", I_LETTER, "", 0, true, .75f, .30f, .05f},
    {"combo5", "Combined button 5", I_LETTER, "", 0, true, .75f, .44f, .05f},
    {"combo6", "Combined button 6", I_LETTER, "", 0, true, .75f, .58f, .05f},
    {"stick", "Movement", I_STICK, "", 0, false, .29f, .27f, .115f},
    // the menu pad's (x, y unused: place_raw): the shoulders as tabs (the item box's pages, the shops'), SELECT
    {"ml", "L", I_LETTER, "L", BTN_L, true, 0, 0, .040f},
    {"mr", "R", I_LETTER, "R", BTN_R, true, 0, 0, .040f},
    {"mselect", "SELECT", I_SELECT, "SELECT", BTN_SELECT, true, 0, 0, .030f},
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
    case C_MPAD: case C_ML: case C_MR: case C_MSELECT: return TB_MDPAD;
    case C_ITEML: case C_ITEMR: return TB_ITEMARROWS;
    case C_RUNLOCK: return TB_STICKRUN;
    default: return -1;
    }
}
bool is_tab(int i) { return i == C_ML || i == C_MR; }  // (the menu pad's shoulders, drawn as tabs)
bool is_dpad(int i) { return i == C_HDPAD; }
bool of_mpad(int i) { return i == C_MPAD || is_tab(i) || i == C_MSELECT; }  // (the menu pad's group)
bool mpad_member(int i) { return is_tab(i) || (i == C_MSELECT && !show(TB_SELECT)); }  // (SELECT: while the hunt's is hidden)
bool g_town_edit;  // (the layout editor is open: the buttons that come and go show there wherever the game is)
// One layout, the player's, in the game's menus too (`menu`: one is open): they add the menu pad and its group, and Y
// where the layout has none; the item bar's arrows are a hunt's, beside the bar while it shows
// Not in the front end (the title, the choice of a save, the character's creation), as the MHGU port: its lists are
// moved with the stick, and nobody walks there (with the stick hidden the pad shows there too: nothing else would)
bool mpad_menu(bool menu) { return g_town_edit || (menu && !(g_game.front_end == 1 && show(TB_STICK))); }
bool in_mode(int i, bool menu) {
    if (i == C_TOWN) return !menu && (g_town_edit || town_available());
    // (the run button: with show_stick_run 0, in a hunt and in towns, either layout, never in a menu - R turns its pages)
    if (i == C_RUN) return !menu && !show(TB_STICKRUN);
    if (i == C_LAYOUT) return false;  // (the menu layout's button: gone, the game says when a menu is open)
    if (i == C_MPAD) return show(TB_MDPAD) && mpad_menu(menu);
    if (of_mpad(i)) return show(TB_MDPAD) && mpad_member(i) && mpad_menu(menu);
    if (i == C_MY) return !show(TB_SHEATHE) && (menu || g_town_edit);
    if (i == C_ITEML || i == C_ITEMR)
        return show(TB_ITEMARROWS) && (g_town_edit || (!menu && g_game.place == 2 && g_game.item_bar_shown != 0));
    if (i == C_RUNLOCK) return g_town_edit && show(TB_STICKRUN);  // (in a hunt: drawn and armed with the stick)
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
// Running with the stick (show_stick_run), in the stick base's radius r, as before: the thumb past RUN_ON runs (R held:
// the dash, with the weapon sheathed) and lets go under RUN_OFF. And Call of Duty Mobile's sprint lock: a small circle
// LOCK_AT above the stick (LOCK_EDGE below the screen's top at least), shown as the thumb pushes up past the rim. The thumb
// on it (within LOCK_GRAB of its middle) arms it, lit; slid out again it is only the rim's run, so passing over it does
// nothing. Lifted there, the run locks and goes on forward (the stick held full up: where the camera looks, the right
// thumb steering) until the stick or the circle is touched again, or a run can't go on (run_end). The same in the MHGU
// port's runtime/touch.cpp
const float RUN_ON = 1.6f, RUN_OFF = 1.45f, LOCK_AT = 2.4f, LOCK_R = .42f, LOCK_GRAB = .55f, LOCK_EDGE = .02f;
const float LOCK_UP = .7f;  // (the thumb's way within about 45 degrees of straight up: the circle shows)
const uint64_t RUN_X_WAIT = 400 * MS;  // (X pressed: no run's R until this long after: frame_input)
const uint64_t RUN_L_WAIT = 70 * MS;   // (a touch L in a village: no run's R until two game frames after, or this long
                                       // while the game's frames aren't known: frame_input)
// The lock circle's middle, for a stick at (bx, by) of radius r on a screen h high
ImVec2 lock_spot(float bx, float by, float r, float h) { return ImVec2(bx, std::max(by - LOCK_AT * r, LOCK_R * r + LOCK_EDGE * h)); }
// The hunter is played in a quest while the quest camera runs (Port::Hooks::CameraTurnActive: its hook within 0.3 s), or
// for up to PLAY_STALL while the game has made fewer than PLAY_FRAMES frames since: a long frame or a stall of the game's
// (a pipeline being made, a slow phone's first minutes) is no time without the hunter, and a locked run goes on through it.
// The MHGU port's playable() (runtime/touch.cpp), with the same numbers
const uint64_t PLAY_STALL = 2000 * MS;
const uint64_t PLAY_FRAMES = 3;
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
    g_epanel = EditPanel{};
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
            bool usual = x < 0 && y < 0, placed = x >= 0 && x <= 1 && y >= 0 && y <= 1;
            if (!strcmp(key, "editor_panel") && (usual || placed) && s >= EPANEL_MIN && s <= 1.0f)
                g_epanel.x = usual ? -1.0f : x, g_epanel.y = usual ? -1.0f : y, g_epanel.k = s;
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
    char line[80];
    epanel_line(line, g_epanel);
    fprintf(f, "%s%s", EPANEL_NOTE, line);
    if (g_set.debug) fprintf(f, "debug 1\n");
    fclose(f);
    fprintf(stderr, "[touch] layout saved (%s)\n", g_layout_path.c_str());
}
// The panel's line alone into the file, the rest of it as it is: Cancel keeps where the panel was put but not the
// controls' places, so that a player who never saved a layout still has none (a file with the panel only: the loader
// takes the defaults for the rest, a later version's new ones too)
void save_panel() {
    std::string was, now;
    if (FILE *f = fopen(g_layout_path.c_str(), "r")) {
        char buf[512];
        for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) was.append(buf, n);
        fclose(f);
    }
    char line[80], key[40];
    epanel_line(line, g_epanel);
    bool put = false;
    for (size_t at = 0, end; at < was.size(); at = end) {
        end = was.find('\n', at);
        end = end == std::string::npos ? was.size() : end + 1;
        std::string l = was.substr(at, end - at);
        if (sscanf(l.c_str(), "%39s", key) != 1 || strcmp(key, "editor_panel")) now += l;
        else if (!put) now += line, put = true;  // (in its place; a second one dropped)
    }
    if (!put) {  // (none yet: at the end, under its note)
        if (!now.empty() && now.back() != '\n') now += '\n';
        now += EPANEL_NOTE, now += line;
    }
    FILE *f = fopen(g_layout_path.c_str(), "w");
    if (!f) { fprintf(stderr, "[touch] could not save %s\n", g_layout_path.c_str()); return; }
    fputs(now.c_str(), f);
    fclose(f);
    fprintf(stderr, "[touch] layout editor: its panel saved alone (%s%s)\n", g_layout_path.c_str(),
            was.empty() ? ", the rest the defaults" : "");
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
    if (i == C_RUNLOCK && fabsf(l.x[i] - d.x) < 1e-4f && fabsf(l.y[i] - d.y) < 1e-4f) {  // (not placed: above the stick)
        Geo s = place_raw(l, C_STICK, w, h);
        return {lock_spot(s.c.x, s.c.y, s.r, h), LOCK_R * s.r * l.scale[i]};
    }
    // the item bar's arrows follow the bar where the game draws it (the player may have moved it): one each side of it at
    // its middle, smaller where the screen's edge leaves less room beside the bar (both the same size, at least
    // ITEM_ARROW_MIN), as the MHGU port's (its touch.cpp item_arrow); once the player places them in the editor, that
    // place is theirs. Not placed and no bar known: side by side at their usual place
    if ((i == C_ITEML || i == C_ITEMR) && fabsf(l.x[i] - d.x) < 1e-4f && fabsf(l.y[i] - d.y) < 1e-4f) {
        const float full = d.r * l.scale[i] * h;
        if (g_game.item_bar_shown == 1 && g_game.item_bar.w > 0 && g_game.item_bar.h > 0) {
            const SDL_FRect &b = g_game.item_bar;
            const float room = std::min(b.x, w - (b.x + b.w)) / 2.2f;  // (a radius and a little gap each side)
            const float r = std::max(std::min(full, room), ITEM_ARROW_MIN * h);
            const float x = i == C_ITEML ? b.x - r * 1.15f : b.x + b.w + r * 1.15f;
            return {ImVec2(std::clamp(x, r, std::max(r, w - r)), std::clamp(b.y + b.h * .5f, r, std::max(r, h - r))), r};
        }
        const float mid = w - (DEFS[C_ITEML].x + DEFS[C_ITEMR].x) * .5f * h;
        return {ImVec2(mid + (i == C_ITEML ? -1.15f : 1.15f) * full, h - d.y * h), full};
    }
    if (i == C_MPAD) {  // (its whole group on the screen: the tabs above it, SELECT below)
        float k = l.scale[i], hw = MPAD_HALF_W * k * h, up = MPAD_ABOVE * k * h, down = MPAD_BELOW * k * h;
        float x = w - l.x[i] * h, y = h - l.y[i] * h;
        return {ImVec2(std::clamp(x, hw, std::max(hw, w - hw)), std::clamp(y, up, std::max(up, h - down))), d.r * k * h};
    }
    if (of_mpad(i)) {  // above the menu pad, as a controller's shoulders, L and R; SELECT below it, at the left. Outside
        Geo g = place_raw(l, C_MPAD, w, h);  // its grab ring at any size
        float k = l.scale[C_MPAD];
        int n = i == C_ML ? 0 : i == C_MR ? 1 : 2;
        static const float ox[3] = {-MPAD_TAB_X, MPAD_TAB_X, MPAD_SELECT_X}, oy[3] = {MPAD_TAB_Y, MPAD_TAB_Y, MPAD_SELECT_Y};
        return {ImVec2(g.c.x + ox[n] * k * h, g.c.y - oy[n] * k * h), d.r * k * h};
    }
    if (i == C_CORNER) {  // (c: the centre of its bend; a larger band stays against the edges)
        float r = d.r * l.scale[i] * h, in = r * (1 + BAND_T);
        return {ImVec2(w - l.x[i] * h - in, h - l.y[i] * h - in), r};
    }
    return {ImVec2(d.right ? w - l.x[i] * h : l.x[i] * h, h - l.y[i] * h), d.r * l.scale[i] * h};
}

// the d-pad's direction under a finger, from its centre: one at a time (a menu's cursor goes one way: a thumb between
// two arms pressing both makes it jump), and the one held stays until the thumb is clearly nearer another
int dpad_part(Geo g, float x, float y, int held = 0, float on = .17f, float off = .12f) {
    float dx = x - g.c.x, dy = y - g.c.y, ax = fabsf(dx), ay = fabsf(dy);
    if (hypotf(dx, dy) < g.r * (held ? off : on)) return 0;  // (the middle: none)
    const float STICKY = 1.35f;
    bool across = held & 3 ? ax * STICKY >= ay : held & 12 ? ax >= ay * STICKY : ax >= ay;
    return across ? (dx > 0 ? 1 : 2) : (dy < 0 ? 4 : 8);  // (right, left; up, down)
}
// the menu pad's: a wider middle (a thumb resting on it presses nothing until it leans), the same arms
int mpad_part(Geo g, float x, float y, int held = 0) { return dpad_part(g, x, y, held, MPAD_ON, MPAD_OFF); }
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
    if (i == C_ATTACK) {
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
    if (i == C_ATTACK) {
        grow(add(g.c, PILL_U, g.r * PILL_HALF), g.r), grow(add(g.c, PILL_U, -g.r * PILL_HALF), g.r);
    } else if (i == C_CORNER) {
        b = {g.c.x, g.c.y, g.c.x, g.c.y, true};
        for (int k = 0; k <= 24; k++) grow(band_point(g, g.r * BAND_LEN * k / 24.0f, 0), g.r * BAND_T);
    } else if (is_tab(i)) {
        b = {g.c.x - g.r * TAB_W, g.c.y - g.r * TAB_H, g.c.x + g.r * TAB_W, g.c.y + g.r * TAB_H, true};
    } else if (i == C_MENU || i == C_HSELECT || i == C_MSELECT) {  // (their plates below them: badge())
        b.y1 = std::max(g.c.y + g.r * 1.45f, g.c.y + g.r * 1.02f + std::max(g.r * .3f, .021f * h) * .72f);
    } else if (i == C_MPAD) {  // (its group: the tabs above it, SELECT and its plate below)
        float k = l.scale[i] * h;
        b = {g.c.x - MPAD_HALF_W * k, g.c.y - MPAD_ABOVE * k, g.c.x + MPAD_HALF_W * k, g.c.y + MPAD_BELOW * k, true};
    }
    return b;
}

// A control of the hunt's under the menu pad's group (its tabs reach ZR, ZL and L at their usual places): while the pad
// shows it is neither drawn nor touched (the tabs are L and R there). Never the attacks nor B (a menu's confirm and back:
// under the pad itself the pad takes the finger first). The hunt's d-pad (show_dpad) wherever it is: the menu pad takes
// its place, so it goes as the pad comes in and comes back as it goes (4u-native #25; the same in the MHGU port)
bool under_mpad(const Layout &l, int i, float w, float h) {
    if (of_mpad(i) || i == C_SETTINGS || i == C_ATTACK || i == C_CORNER || i == C_HEAVY || i == C_LIGHT || i == C_DODGE ||
        !in_mode(C_MPAD, true))
        return false;
    if (i == C_HDPAD) return !covered(l, C_MPAD, w, h);  // (the pad under the touch screen shows nothing: the d-pad stays)
    Rect b = bounds(l, i, w, h);
    for (int m : {C_ML, C_MR, C_MSELECT}) {
        if (!mpad_member(m)) continue;
        Rect t = bounds(l, m, w, h);
        if (b.x1 > t.x0 && b.x0 < t.x1 && b.y1 > t.y0 && b.y0 < t.y1) return true;
    }
    Geo p = place(l, C_MPAD, w, h);
    return b.near(p.c, p.r);
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

bool mpad_ready() { return SDL_GetTicksNS() - g_menu_on_ns >= MPAD_TAKE; }  // (the menu open a moment: its pad takes fingers)
// the control under a finger (a little larger than its art: a thumb is wider), and the attack pill's part. In a menu of
// the game's the menu pad first: a thumb within its ring is its (the tabs and the rest after, the tabs over what is
// under them), and no seam presses two attacks at once (no menu wants X + A); in the editor, a tab or SELECT is the pad
int hit(const Layout &l, float x, float y, float w, float h, int *part, float slop = 1.22f, bool stick = false,
        bool menu = false, bool editing = false) {
    bool pad_ready = !menu || editing || mpad_ready();  // (just opened: not yet)
    if (menu && !editing && pad_ready && in_mode(C_MPAD, true) && !covered(l, C_MPAD, w, h)) {
        Geo g = place(l, C_MPAD, w, h);
        if (hypotf(x - g.c.x, y - g.c.y) < g.r * MPAD_GRAB) {
            if (part) *part = mpad_part(g, x, y);
            return C_MPAD;
        }
    }
    int best = -1;
    float bd = 1e30f;
    for (int i = 0; i < C_COUNT; i++) {
        if (!in_mode(i, menu) || (i == C_STICK && !stick) || (of_mpad(i) && (!pad_ready || (editing && i != C_MPAD)))) continue;
        if (!editing && covered(l, i, w, h)) continue;
        if (!editing && menu && under_mpad(l, i, w, h)) continue;
        Geo g = place(l, i, w, h);
        float d = i == C_ATTACK ? pill_distance(g, x, y) / (g.r * (slop - .07f))
                : i == C_CORNER ? band_distance(g, x, y, nullptr) / (g.r * BAND_T * (slop - .07f))
                : is_tab(i) ? std::max(fabsf(x - g.c.x) / (g.r * TAB_W * slop), fabsf(y - g.c.y) / (g.r * TAB_H * slop))
                            : hypotf(x - g.c.x, y - g.c.y) / (g.r * slop);
        if (d < 1 && of_mpad(i) && !editing) d *= .01f;  // (the menu pad's tabs over a button placed under them: theirs)
        if (d < 1 && d < bd) bd = d, best = i;
    }
    if (editing && in_mode(C_MPAD, false))  // (the editor: a tab or SELECT picks the menu pad, which they go with)
        for (int i : {C_ML, C_MR, C_MSELECT}) {
            Geo g = place(l, i, w, h);
            float d = is_tab(i) ? std::max(fabsf(x - g.c.x) / (g.r * TAB_W * slop), fabsf(y - g.c.y) / (g.r * TAB_H * slop))
                                : hypotf(x - g.c.x, y - g.c.y) / (g.r * slop);
            if (mpad_member(i) && d < 1 && d < bd) bd = d, best = C_MPAD;
        }
    if (best == C_ATTACK && part) {
        Geo g = place(l, C_ATTACK, w, h);
        *part = !menu ? pill_part(g, x, y) : (x - g.c.x) * PILL_U.x + (y - g.c.y) * PILL_U.y > 0 ? 0 : 1;  // (a menu: no seam)
    }
    if (best == C_CORNER && part) {
        Geo g = place(l, C_CORNER, w, h);
        *part = band_part(g, x, y);
        if (menu && *part == 2) {  // (the seam: the nearer attack)
            float s = 0;
            band_distance(g, x, y, &s);
            *part = s / (g.r * BAND_LEN) < (BAND_SEAM0 + BAND_SEAM1) * .5f ? 1 : 0;
        }
    }
    if (best == C_MPAD && part) *part = editing ? 0 : mpad_part(place(l, C_MPAD, w, h), x, y);
    if (part && !menu && g_style == STYLE_SEPARATE && in_mode(C_LIGHT, false)) {  // (between the two attack buttons: both)
        ImVec2 at;
        float size;
        if (separate_seam(l, w, h, &at, &size) && hypotf(x - at.x, y - at.y) < size * 1.5f) best = C_LIGHT, *part = 1;
    }
    if (best >= 0 && is_dpad(best) && part) *part = dpad_part(place(l, best, w, h), x, y);
    return best;
}

// ------------------------------------------------------------------ fingers (the app's main thread)
enum Kind { F_NONE, F_BUTTON, F_STICK, F_CAMERA, F_MENU, F_ITEM, F_EDIT };
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
    uint64_t keep;         // a thumb from Guard slid onto the attacks (show_guard_slide): R kept with them
    bool out0;             // the weapon was out as it landed (the aim's: a tap that drew the weapon holds nothing open)
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
    bool menu;                      // the menu's (a tap of the menu pad's group): dropped as the menu closes
    int ctl, part;                  // that tap: which control, its direction (lit while it presses)
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
    uint64_t aim_latch;  // a tap on the aim: its button held for the game until the next tap (it aims only while held)
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
    bool run;                // running: R held (the stick's locked run, or the Run button held)
    bool run_lock;           // the stick's run locked (show_stick_run): the thumb lifted on the lock circle, the
    bool run_free;           // stick stays pushed full forward (both: a lock is only ever a lifted thumb's)
    bool run_armed;          // the thumb landed, or came back inside the rim since the last lock ended (it may lock)
    bool run_over;           // the thumb on the lock circle, past the rim, a lock possible: lifted there it locks (lit)
    bool run_can;            // a lock could start now (show_stick_run, sheathed, no menu, the hunter played: the circle shows)
    uint64_t quest_cam_ns, quest_cam_frame;  // the time and the game's frame the quest camera was last seen running
    bool rim;                // the thumb past the rim runs (RUN_ON, down to RUN_OFF)
    uint64_t run_x_until;    // an attack that draws the weapon (X) was pressed: no run's R until then
    uint64_t run_l_frame, run_l_ns;  // the game's frame and the time an L of the touch controls was last pressed
    float fx, fy;            // the stick's thumb where it is (the knob stops at the rim)
    uint64_t item_flash;
    int item_dir;            // the bar's last turn (-1, 1), 0 the item used
    uint64_t use_until, use_at;  // the item to use once the weapon is away (item_use): until when, and when it went
    int use_action;              // away, with the hunter's action then (putting the weapon away)
    SDL_FingerID bar_finger;     // the finger whose long press holds the bar open
    bool bar_open;           // a long press holds the bar open (Port::Hooks::HoldItemBarOpen)
    bool bar_held;           // the hooks hold it open now (the long press, or a step of the ammo: frame_input)
    int stick_part;          // in a menu: the direction the stick is pressing (dpad_part)
    uint64_t menu_off_ns;    // when the last menu closed (the menu pad goes: MPAD_HOLD, MPAD_OUT)
    int lit_ctl = -1, lit_part;  // a quick tap of the menu pad's group pressing now (lit until lit_until)
    uint64_t lit_until;
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
    EditPanel epanel_open;   // (g_epanel as the editor opened)
    int rep_btn;             // the panel's - or + that sized it last (-1 none), when, and where it was then
    uint64_t rep_ns;
    Rect rep_at;
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

// The Run button at its default place in a layout of the corner style (saved before the button was there): inside the
// band's hollow, where next_style puts it, when the band would cover it (both are placed from the same corner: any
// screen's shape tells)
void run_clear_of_band(Layout &l) {
    if (l.style != STYLE_CORNER || l.x[C_RUN] != DEFS[C_RUN].x || l.y[C_RUN] != DEFS[C_RUN].y) return;
    const float w = 2.0f, h = 1.0f;
    Geo band = place_raw(l, C_CORNER, w, h), o = place_raw(l, C_RUN, w, h);
    if (band_distance(band, o.c.x, o.c.y, nullptr) >= band.r * BAND_T + o.r) return;
    ImVec2 at(band.c.x + .23f * band.r, band.c.y - .27f * band.r);
    l.x[C_RUN] = (w - at.x) / h, l.y[C_RUN] = (h - at.y) / h;
}

void ensure_loaded(State &s) {
    if (s.loaded) return;
    s.loaded = true, s.sel = -1;
    seq_setup(s);
    bool had = load_layout(&s.layout);
    run_clear_of_band(s.layout);
    s.saved = s.layout;
    g_style = s.layout.style;
    fprintf(stderr, "[touch] layout: %s\n", had ? g_layout_path.c_str() : "the defaults");
}

bool pulse_busy(const Pulse &p, uint64_t now) {
    return now < p.from || now < p.until || (p.started && !frames_ok(p.frame_until) && now - p.from < FRAME_WAIT);
}
// A press of its own, held `ns` (at least min_press) and two of the game's frames; `frame_until` when it began earlier
// (a finger that already held it). A tap of the menu pad's group (`ctl`, its direction `part`) is the menu's: lit while
// it presses, dropped as the menu closes
void pulse(State &s, uint64_t buttons, uint64_t ns, uint64_t frame_until = 0, int ctl = -1, int part = 0) {
    uint64_t now = SDL_GetTicksNS();
    for (Pulse &p : s.pulse)
        if (!pulse_busy(p, now)) {
            p.buttons = buttons, p.from = now, p.until = now + ns, p.frame_until = frame_until, p.started = frame_until != 0;
            p.menu = ctl >= 0, p.ctl = ctl, p.part = part;
            return;
        }
}
uint64_t pulse_buttons(State &s, uint64_t now) {
    uint64_t b = 0;
    for (Pulse &p : s.pulse) {
        if (now < p.from || !pulse_busy(p, now)) continue;
        if (!p.started) p.started = true, p.frame_until = g_frames + 2;
        b |= p.buttons;
        if (p.ctl >= 0) s.lit_ctl = p.ctl, s.lit_part = p.part, s.lit_until = now + 50 * MS;  // (lit while it presses)
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
    // (inside the rim: pushed from here into the lock circle, it may lock; every finger event, not each frame, so that a
    // quick flick counts)
    if (d < r) s.run_armed = true;
    if (d > r) x = s.bx + dx / d * r, y = s.by + dy / d * r;
    s.kx = x, s.ky = y;
}

// The stick's locked run ends (why: for the log); it locks again only for a thumb come back inside the rim, or a new one
void run_end(State &s, const char *why) {
    s.run_over = false;
    if (!s.run_lock) return;
    s.run_lock = s.run_free = s.run_armed = false;
    if (g_set.debug) fprintf(stderr, "[touch] run let go (%s)\n", why);
}
// The thumb at (x, y) is on the lock circle, past the rim (a stick placed high has the circle clamped near it)
bool on_lock_circle(const State &s, float x, float y) {
    const Geo lg = place(s.layout, C_RUNLOCK, s.w, s.h);  // (where it is: above the stick, or where the editor put it)
    float r = place(s.layout, C_STICK, s.w, s.h).r;
    return hypotf(x - s.bx, y - s.by) > r && hypotf(x - lg.c.x, y - lg.c.y) < LOCK_GRAB / LOCK_R * lg.r;
}
// A thumb from Guard keeps R as it slides (show_guard_slide): with the weapon out, no menu of the game's open
bool guard_slide(const State &s) { return show(TB_GUARDSLIDE) && g_game.weapon_drawn == 1 && !s.menu; }

Finger *finger(State &s, SDL_FingerID id) {
    for (Finger &f : s.f)
        if (f.kind != F_NONE && f.id == id) return &f;
    return nullptr;
}

uint64_t finger_buttons(const Finger &f) {
    uint64_t b = f.ctl == C_ATTACK || f.ctl == C_CORNER ? PILL_BUTTONS[f.part]
               : is_dpad(f.ctl) || f.ctl == C_MPAD ? dpad_buttons(f.part)
               : f.ctl == C_LIGHT && f.part ? PILL_BUTTONS[2]
               : is_combo(f.ctl) ? g_combo[f.ctl - C_COMBO]
               : f.btn ? f.btn
                       : DEFS[f.ctl].buttons;
    return b | f.keep;  // (a guard kept as it slid: show_guard_slide)
}

void recompute_held(State &s) {
    s.held = 0;
    for (Finger &f : s.f)
        if (f.kind == F_BUTTON && !f.done) s.held |= finger_buttons(f);
    s.held |= s.aim_latch;  // (the aim held open by a tap)
}

// ---- the layout editor: a panel in the middle (smaller, bigger, the ranged layout, the attacks' style, defaults,
// cancel, save); drag a control to move it, pinch (a second finger anywhere) or the panel's - / + to size it. The panel
// itself goes where its title strip is dragged (a grip at each end of it), and is as small as EPANEL_MIN: picked there,
// a pinch or its - / + size it, so that it leaves free what is being placed (the MHGU port's the same)
const int SEL_PANEL = -2;  // (s.sel / a finger's ctl: the editor's panel, picked by its title strip)
const uint64_t PANEL_REPEAT = 700 * MS;  // (taps on its - or + this close together: one button's, State::rep_btn)
bool grabbed(int ctl) { return ctl >= 0 || ctl == SEL_PANEL; }  // (a finger's ctl: something it moves)
void panel_dims(float w, float h, float k, float *pw, float *ph) {
    // (between the left edge's column and the right thumb's controls, where it hides none of them, on a wide screen)
    *pw = std::clamp(w - 1.04f * h, std::min(w * .9f, 1.1f * h), std::min(w * .9f, 1.8f * h)) * k, *ph = .2f * h * k;
}
// The panel, its buttons and its title strip (grab: where a finger picks and drags it), on the screen whole; its measure
// (the screen's height at 100 %, smaller with it)
float editor_panel(const EditPanel &ep, float w, float h, Rect *panel, Rect btn[E_COUNT], Rect *grab = nullptr) {
    float k = std::clamp(ep.k, EPANEL_MIN, 1.0f), u = k * h, pw, ph;
    panel_dims(w, h, k, &pw, &ph);
    float x0 = ep.x < 0 ? (w - pw) * .5f : ep.x * w - pw * .5f, y0 = ep.y < 0 ? (h - ph) * .4f : ep.y * h - ph * .5f;
    x0 = std::clamp(x0, 0.0f, std::max(0.0f, w - pw)), y0 = std::clamp(y0, 0.0f, std::max(0.0f, h - ph));
    *panel = {x0, y0, x0 + pw, y0 + ph, true};
    float gap = .015f * u, bw = (pw - gap * (float)(E_COUNT + 1)) / (float)E_COUNT, by0 = y0 + ph * .5f, by1 = y0 + ph - gap;
    if (grab) *grab = {x0, y0, x0 + pw, by0 - gap * .5f, true};
    for (int i = 0; i < E_COUNT; i++) btn[i] = {x0 + gap + i * (bw + gap), by0, x0 + gap + i * (bw + gap) + bw, by1, true};
    return u;
}
// The panel dragged by (dx, dy) pixels, kept on the screen whole
void panel_move(State &s, float dx, float dy) {
    Rect p, b[E_COUNT];
    editor_panel(g_epanel, s.w, s.h, &p, b);
    float hw = (p.x1 - p.x0) * .5f, hh = (p.y1 - p.y0) * .5f;
    g_epanel.x = std::clamp((p.x0 + p.x1) * .5f + dx, hw, std::max(hw, s.w - hw)) / s.w;
    g_epanel.y = std::clamp((p.y0 + p.y1) * .5f + dy, hh, std::max(hh, s.h - hh)) / s.h;
}
// The panel sized (whole percents, EPANEL_MIN to 100 %) about a point of it that stays where it is: the - or + under the
// finger (a few taps in a row stay on it), its middle for a pinch
void panel_resize(State &s, float to, float ax, float ay) {
    Rect p, b[E_COUNT];
    editor_panel(g_epanel, s.w, s.h, &p, b);
    float fx = (ax - p.x0) / std::max(1.0f, p.x1 - p.x0), fy = (ay - p.y0) / std::max(1.0f, p.y1 - p.y0), pw, ph;
    g_epanel.k = std::clamp(roundf(to * 100.0f) / 100.0f, EPANEL_MIN, 1.0f);
    panel_dims(s.w, s.h, g_epanel.k, &pw, &ph);
    g_epanel.x = (ax + (.5f - fx) * pw) / s.w, g_epanel.y = (ay + (.5f - fy) * ph) / s.h;
    panel_move(s, 0, 0);
}

void hud_load(State &s);
void edit_begin(State &s) {
    s.edit = true, s.sel = -1, s.pinch = false;
    g_town_edit = true;
    hud_load(s);
    g_edit_set = g_ranged != 0;  // (the layout in use; the panel's button changes it)
    s.saved = s.layout, s.epanel_open = g_epanel, s.rep_btn = -1;
    for (Finger &f : s.f) f.kind = F_NONE;
    s.stick_on = false, s.held = 0;
    run_end(s, "the editor");
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
uint64_t g_hud_moved_ns;  // (a part of the game's HUD moved in the editor then - dragged, Defaults, Cancel: touch_editor_holds)
void hud_preview(State &s, int k) {
    g_hud_moved_ns = SDL_GetTicksNS();  // (the game draws it there: touch_editor_holds)
    game_option_preview(std::string("hud_") + HUD_KEY[k], hud_value(s.hud_at[k]));
}
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
    if (g_epanel != s.epanel_open) {  // (the panel stays where it was put, and as big, Save or Cancel: Cancel its line alone)
        if (!keep) save_panel();
        fprintf(stderr, "[touch] layout editor: its panel at %.2f %.2f, %d%%\n", g_epanel.x, g_epanel.y, (int)lroundf(g_epanel.k * 100));
    }
    g_style = s.layout.style;
    g_edit_set = -1;
    s.edit = false, s.sel = -1, s.pinch = false;
    g_town_edit = false;
}
void clamp_place(State &s, int i) {
    float wmax = s.w / s.h;
    s.layout.scale[i] = std::clamp(s.layout.scale[i], i == C_CORNER ? .7f : .5f, i == C_CORNER ? 1.3f : 2.2f);
    if (i == C_MPAD) {  // (its whole group on the screen: the tabs above it, SELECT below, as place_raw draws it)
        float k = s.layout.scale[i], hw = MPAD_HALF_W * k;
        s.layout.x[i] = std::clamp(s.layout.x[i], hw, std::max(hw, wmax - hw));
        s.layout.y[i] = std::clamp(s.layout.y[i], MPAD_BELOW * k, std::max(MPAD_BELOW * k, 1.0f - MPAD_ABOVE * k));
        return;
    }
    s.layout.x[i] = std::clamp(s.layout.x[i], .03f, wmax - .03f);
    s.layout.y[i] = std::clamp(s.layout.y[i], .03f, .97f);
}
// The next way of laying out the attacks. What the corner band would cover goes inside its bend; leaving it, back to
// where it was when the editor opened
void next_style(State &s) {
    int style = (s.layout.style + 1) % STYLE_COUNT;
    s.layout.style = style, g_style = style;
    static const struct { int c; float dx, dy; } MOVED[3] = {{C_DODGE, .3f, .3f}, {C_SHEATHE, -.2f, -.2f}, {C_RUN, .23f, -.27f}};
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
        Rect panel, btn[E_COUNT], grab;
        editor_panel(g_epanel, s.w, s.h, &panel, btn, &grab);
        // (taps in a row where the panel's - or + was press it again, though the panel it sizes slid at the screen's edge
        // and put another button there)
        uint64_t now = SDL_GetTicksNS();
        int again = s.rep_btn >= 0 && now - s.rep_ns < PANEL_REPEAT && s.rep_at.in(x, y) ? s.rep_btn : -1;
        if (again < 0) s.rep_btn = -1;
        for (int i = 0; i < E_COUNT; i++)
            if (i == again || (again < 0 && btn[i].in(x, y))) {
                f->kind = F_MENU, f->ctl = i;  // (held until it lifts: nothing under it moves; the button it lights)
                haptics_tap();
                if (i == E_SMALLER || i == E_BIGGER) {
                    float by = i == E_BIGGER ? .1f : -.1f;
                    if (s.sel >= 0 && s.sel < C_COUNT) s.layout.scale[s.sel] += by, clamp_place(s, s.sel);
                    else if (s.sel == SEL_PANEL) {  // (about the button pressed: it stays under the finger)
                        panel_resize(s, g_epanel.k + by, (btn[i].x0 + btn[i].x1) * .5f, (btn[i].y0 + btn[i].y1) * .5f);
                        s.rep_btn = i, s.rep_ns = now, s.rep_at = again < 0 ? btn[i] : s.rep_at;
                    }
                } else if (i == E_SET) {
                    g_edit_set = g_edit_set == 1 ? 0 : 1;
                    if (s.sel != SEL_PANEL) s.sel = -1;
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
            if (&g != f && g.kind == F_EDIT && grabbed(g.ctl)) drag = &g;
        if (drag && drag->ctl >= ZONE_SEL) drag = nullptr;  // (a part of the HUD has no size)
        if (drag) {  // a second finger: pinch the control being dragged, or the panel
            s.pinch = true, s.pinch_d0 = std::max(1.0f, hypotf(x - drag->x, y - drag->y));
            s.pinch_s0 = drag->ctl == SEL_PANEL ? g_epanel.k : s.layout.scale[drag->ctl];
            f->kind = F_EDIT;
            return;
        }
        // the panel's title strip: the panel, picked and dragged, over what is under it. A little past its edges (not into
        // the buttons below it) it is the panel only where nothing else is: a control or a part there stays the one picked
        auto strip = [&](float m) { return x >= grab.x0 - m && x <= grab.x1 + m && y >= grab.y0 - m && y <= grab.y1; };
        if (strip(0.0f)) {
            f->kind = F_EDIT, f->ctl = SEL_PANEL;
            s.sel = SEL_PANEL;
            return;
        }
        // a control, else the panel's face (nothing moves), else a part of the game's HUD, else the panel by its title
        // strip's edge
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
        if (c < 0 && strip(.02f * s.h)) c = SEL_PANEL;
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
            int c = a && grabbed(a->ctl) ? a->ctl : b && grabbed(b->ctl) ? b->ctl : -1;
            float by = a && b ? hypotf(a->x - b->x, a->y - b->y) / s.pinch_d0 : 1.0f;
            if (a && b && c >= 0 && c < C_COUNT) s.layout.scale[c] = s.pinch_s0 * by, clamp_place(s, c);
            if (a && b && c == SEL_PANEL) {  // (about its middle)
                Rect p, bt[E_COUNT];
                editor_panel(g_epanel, s.w, s.h, &p, bt);
                panel_resize(s, s.pinch_s0 * by, (p.x0 + p.x1) * .5f, (p.y0 + p.y1) * .5f);
            }
        } else if (f->ctl == SEL_PANEL) {
            panel_move(s, dx, dy);
        } else if (f->ctl >= ZONE_SEL) {
            hud_drag(s, f->ctl - ZONE_SEL, dx, dy);
        } else if (f->ctl >= 0) {
            if (f->ctl == C_RUNLOCK && fabsf(s.layout.x[f->ctl] - DEFS[f->ctl].x) < 1e-4f &&
                fabsf(s.layout.y[f->ctl] - DEFS[f->ctl].y) < 1e-4f) {  // (following the stick: dragged, the place it had is its own)
                const Geo g = place_raw(s.layout, f->ctl, s.w, s.h);
                s.layout.x[f->ctl] = g.c.x / s.h, s.layout.y[f->ctl] = (s.h - g.c.y) / s.h;
                s.layout.scale[f->ctl] = std::clamp(g.r / (DEFS[f->ctl].r * s.h), .5f, 2.2f);
            }
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
        // the sprint lock's circle, lit while the run goes on by itself: a tap there ends it, as touching the stick does
        if (s.run_lock && !s.stick_on && on_lock_circle(s, x, y)) {
            f->kind = F_NONE;  // (the finger is no control's: its moves and lift go nowhere)
            run_end(s, "the lock circle touched");
            haptics_tap();
            return true;
        }
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
        // (the stick's ground is the stick's, but for a button's own circle: the margin round the Town and touch screen
        // buttons reaches it, with the stick made bigger or the left side made smaller beside the touch screen's corner,
        // and a thumb meant for the stick opened the town's list or swapped the screens, 4u-native #31)
        if (c >= 0 && c != C_ATTACK && c != C_CORNER && c != C_MPAD && !is_tab(c) && !s.stick_on &&
            in_mode(C_STICK, s.menu) && !covered(s.layout, C_STICK, s.w, s.h) && in_stick_zone(s, x, y, item)) {
            Geo g = place(s.layout, c, s.w, s.h);
            if (hypotf(x - g.c.x, y - g.c.y) > g.r) c = -1;
        }
        if (c >= 0) {
            f->kind = F_BUTTON, f->ctl = c, f->part = part;
            s.use_until = s.use_at = 0;  // (another control: the item waiting for the weapon to go away is given up)
            f->btn = is_ranged(c) ? ranged_buttons(c, g_game.weapon_drawn) : 0;
            f->out0 = g_game.weapon_drawn == 1;
            if (c == C_RAIM && s.aim_latch) s.aim_latch = 0, f->done = true;  // (the aim held open: this tap lets it go)
            if (c == C_AMMO_UP || c == C_AMMO_DOWN) ammo_step(s, c == C_AMMO_UP ? 1 : -1);
            if (c == C_SCREEN) touch_toggle_bottom();
            if (c == C_TOWN) town_open(false);
            if (c == C_ITEML || c == C_ITEMR) item_swipe(s, c == C_ITEMR ? 1 : -1), f->rep = now + 420 * MS;
            if (c != C_MPAD || part) haptics_tap();  // (the menu pad: as it presses a direction, not as it is touched)
        } else if (item.in(x, y)) {
            f->kind = F_ITEM;
        } else if (!s.stick_on && in_mode(C_STICK, s.menu) && !covered(s.layout, C_STICK, s.w, s.h) &&
                   in_stick_zone(s, x, y, item)) {
            f->kind = F_STICK;
            Geo g = place(s.layout, C_STICK, s.w, s.h);
            run_end(s, "the stick touched again");  // (a run going on by itself stops: this thumb drives now)
            s.stick_on = true, s.bx = g.c.x, s.by = g.c.y;
            stick_to(s, x, y);
            // (a new thumb may lock from wherever it landed, slid up into the circle again - not one that landed in it,
            // which a stick placed high has near)
            const Geo lg = place(s.layout, C_RUNLOCK, s.w, s.h);
            if (hypotf(x - lg.c.x, y - lg.c.y) >= LOCK_GRAB / LOCK_R * lg.r) s.run_armed = true;
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
        } else if (f->kind == F_CAMERA || (f->kind == F_BUTTON && (f->ctl == C_RFIRE || f->ctl == C_RAIM) && f->moved && !f->done)) {
            s.cam_dx += dx, s.cam_dy += dy;  // (a drag on the shot or the aim aims while it is held)
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
        } else if (f->kind == F_BUTTON && f->ctl == C_MPAD) {  // the menu pad: the same, past its wide middle; a click as
            int part = mpad_part(place(s.layout, C_MPAD, s.w, s.h), x, y, f->part);  // it presses a new direction
            if (part != f->part) {
                f->part = part, recompute_held(s);
                if (part) haptics_tap();
            }
        } else if (f->kind == F_BUTTON && (f->ctl == C_ATTACK || f->ctl == C_CORNER)) {  // sliding along it: another part
            int part = 0, c = hit(s.layout, x, y, s.w, s.h, &part, 1.22f, false, s.menu);
            if (c == f->ctl && part != f->part) {
                // (the corner's guard onto its attacks: R kept, show_guard_slide; back onto the guard, the guard alone)
                if (f->ctl == C_CORNER && (f->part == 3 || f->keep) && part != 3 && guard_slide(s)) f->keep = BTN_R;
                if (part == 3) f->keep = 0;
                f->part = part, recompute_held(s), haptics_tap();
            } else if (c == C_R && f->keep) {  // (a guard kept, back onto Guard: Guard again)
                f->ctl = C_R, f->part = 0, f->keep = 0, recompute_held(s), haptics_tap();
            }
        } else if (f->kind == F_BUTTON && (f->ctl == C_HEAVY || f->ctl == C_LIGHT)) {  // from one attack to the other
            int part = 0, c = hit(s.layout, x, y, s.w, s.h, &part, 1.22f, false, s.menu);
            if ((c == C_HEAVY || c == C_LIGHT) && (c != f->ctl || part != f->part)) f->ctl = c, f->part = part, recompute_held(s), haptics_tap();
            else if (c == C_R && f->keep) f->ctl = C_R, f->part = 0, f->keep = 0, recompute_held(s), haptics_tap();
        } else if (f->kind == F_BUTTON && (f->ctl == C_DODGE || f->ctl == C_R || f->ctl == C_RUN)) {  // a thumb sliding onto
            int part = 0, c = hit(s.layout, x, y, s.w, s.h, &part, 1.22f, false, s.menu);             // the attacks
            if (c == C_ATTACK || c == C_CORNER || c == C_HEAVY || c == C_LIGHT) {
                // (from Guard with the weapon out and show_guard_slide: R kept, R + X, R + A; from Run: the attack
                // alone, a draw attack, the run's R let go; from Evade: the attack)
                f->keep = f->ctl == C_R && guard_slide(s) ? BTN_R : 0;
                f->ctl = c, f->part = part, recompute_held(s);
                if (f->keep) {
                    haptics_tap();
                    if (g_set.debug) fprintf(stderr, "[touch] guard kept: R with the attacks\n");
                }
            }
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
        if (b && of_mpad(f->ctl)) pulse(s, b, left, g_frames_fed ? f->f0 + 2 : 0, f->ctl, f->ctl == C_MPAD ? f->part : 1);
        else if (b) pulse(s, b, left, g_frames_fed ? f->f0 + 2 : 0);
    }
    // The aim with the weapon out. The game aims only while its button is held (a bow, the RG406V, 2026-11-10: R pressed,
    // 0569 until it is let go, then 056C or 0573, 0565 and back to 0501, a tap as a hold): a tap, let go within AIM_TAP
    // and not slid, holds it for the game until the next tap on the aim (the MHGU port's tap opens the scope, or a bow's
    // aiming mode, which stay open by themselves), or a dodge, a sheathe, the weapon put away, a menu (frame_input); held
    // or slid it aims while the thumb is, its slide turning the aim as a drag of the view does, and letting go ends it
    if (f->kind == F_BUTTON && f->ctl == C_RAIM && !f->done && !cancel && f->btn && f->out0 && g_game.weapon_drawn == 1 && !s.menu &&
        !f->moved && now - f->t0 < AIM_TAP && e.type == SDL_EVENT_FINGER_UP)
        s.aim_latch = f->btn;
    if (f->kind == F_STICK) {
        s.stick_on = false;
        // The thumb lifted on the lock circle, armed (it came from inside the rim or landed elsewhere, and a run can
        // lock): the run locks and goes on by itself, the stick held full forward. Not when the system took the finger,
        // nor for a thumb that only passed over the circle
        bool over = s.run_can && s.run_armed && on_lock_circle(s, x, y);  // (where it lifted)
        s.run_over = false;
        if (!cancel && over && s.run_can && !s.run_lock) {
            s.run_lock = s.run_free = true;
            haptics_tap();
            if (g_set.debug) fprintf(stderr, "[touch] run locked (the thumb lifted on the lock circle)\n");
        }
    }
    if (f->kind == F_ITEM && s.bar_open && f->id == s.bar_finger) bar_close(s);
    if (f->kind == F_ITEM && tap && !f->done && !s.bar_open && fabsf(f->swipe) < .02f * s.h && fabsf(f->swipe_y) < .02f * s.h)
        item_use(s), haptics_tap();  // a tap: the item (Y)
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
    if (g_menu_layout != s.menu) {  // (a menu of the game's opened or closed: the menu pad comes or goes; fingers on what
        s.menu = g_menu_layout;     // it takes away let go, its taps and a drag's steps waiting are dropped)
        if (s.menu) g_menu_on_ns = now;
        else s.menu_off_ns = now;
        for (Finger &f : s.f)
            if (f.kind == F_BUTTON && !in_mode(f.ctl, s.menu)) f.done = true;
        if (!s.menu) {
            for (Pulse &p : s.pulse)
                if (p.menu) p = Pulse{};
            s.cursor.n = 0, s.cursor.phase = 0, s.lit_until = 0;
        }
        recompute_held(s);
        if (g_set.debug) fprintf(stderr, "[touch] game menu %s\n", s.menu ? "open" : "closed");
    }
    // a long press on the item bar holds it open (its neighbours shown, as while L is held: a swipe meanwhile turns it,
    // lifting the finger closes it); a held arrow of the bar turns it again
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
    }
    if (s.bar_open && !item_direct()) bar_close(s);  // (the bar gone: a menu, the hunt over)
    {  // the hooks hold the bar open for the long press, and for a step of the ammo or the coating (ammo_step)
        bool want = s.bar_open || s.ammo_open.phase != 0;
        if (want != s.bar_held) Port::Hooks::HoldItemBarOpen(want), s.bar_held = want;
    }
    // (the aim held open by a tap: let go with the weapon away, a menu, the editor, the other layout, a dodge or a sheathe)
    if (s.aim_latch && (g_game.weapon_drawn != 1 || s.menu || s.edit || !g_ranged || (s.held & (BTN_B | BTN_Y)))) {
        s.aim_latch = 0;
        recompute_held(s);
        if (g_set.debug) fprintf(stderr, "[touch] the aim held open let go\n");
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
                if (f.kind == F_BUTTON || f.kind == F_STICK || f.kind == F_ITEM) f.kind = F_NONE;
            if (s.bar_open) bar_close(s);
            s.stick_on = false;
            run_end(s, "the controls hidden");
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
    {  // aiming with the gyro (controls_gyro): a bow or a bowgun out, its aim's button held (a finger, a tap's hold, a
       // controller, a key), the gyroscope's turn added as a drag's, in degrees
        const uint64_t aim = ranged_buttons(C_RAIM, 1);
        const bool aiming = g_ranged && g_game.weapon_drawn == 1 && !s.menu && !s.edit && ((s.held | controls_buttons()) & aim);
        float right = 0, up = 0;
        if (controls_gyro(aiming, &right, &up) && s.h > 0 && g_set.camera > 0) {
            const float px = s.h / (CAMERA_DEGREES * g_set.camera);
            s.cam_dx += right * px, s.cam_dy -= up * px;
        }
    }
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
    // The stick's locked run ends once a run can't go on: show_stick_run turned off, a menu of the game's open, the
    // controls hidden or in the editor, the weapon out, the hunter not played (away from a town and from the quest camera:
    // a cutscene, a cart, the quest's end; not a stall of the game's: PLAY_STALL)
    bool stick_run = show(TB_STICKRUN);
    bool quest_cam = Port::Hooks::CameraTurnActive();
    if (quest_cam) s.quest_cam_ns = now, s.quest_cam_frame = g_frames;
    else quest_cam = g_frames_fed && s.quest_cam_ns && now - s.quest_cam_ns < PLAY_STALL && g_frames - s.quest_cam_frame < PLAY_FRAMES;
    bool played = g_game.place == 1 || (g_game.place == 2 && quest_cam) || g_game.place < 0;
    s.run_can = stick_run && g_game.weapon_drawn != 1 && !s.menu && played && visible && !s.edit;
    if (s.run_lock && !s.run_can)
        run_end(s, !stick_run ? "show_stick_run off" : !visible || s.edit ? "the controls hidden or edited"
                 : s.menu ? "a menu of the game's" : g_game.weapon_drawn == 1 ? "the weapon out" : "the hunter not played");
    s.run = false;
    if (visible && !s.edit) {
        b |= s.held;
        {  // (the stick is the circle pad in the game's menus too: their cursors follow it)
            s.stick_part = 0;
            bool moving = false;
            if (s.run_lock && s.h > 0) {  // (the sprint lock, the thumb out past the rim or lifted: the circle pad
                out.circle_x = 0, out.circle_y = 156;  // held full forward, where the camera looks: the right thumb steers)
                moving = true;
            } else if (s.stick_on && s.h > 0) {
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
            // Running, R held: the dash with the weapon sheathed or not known (never with it out: R is the weapon's own
            // move, and not in a menu: R turns its pages). show_stick_run: the thumb past the rim, as before (the rim),
            // and the sprint lock: the thumb lifted on the lock circle above the stick, come from inside the rim while a
            // lock can start, locks the run with a click; it goes on forward (above) until run_end. show_stick_run 0: the
            // Run button, while held. No run's R with X pressed or just after: X draws the weapon, and R with X is another
            // move (the MHGU port's #151). The same in the MHGU port
            if (s.stick_on && s.h > 0) {
                float r = place(s.layout, C_STICK, s.w, s.h).r, d = hypotf(s.fx - s.bx, s.fy - s.by);
                // (the rim, as before: well past it; r is the stick's size in the layout: a bigger stick, farther)
                s.rim = stick_run && moving && g_game.weapon_drawn != 1 && !s.menu && d > r * (s.rim ? RUN_OFF : RUN_ON);
                // (the thumb on the circle, armed: lit, and lifted there it locks, as the finger lifts; slid off it, nothing)
                bool in_lock = on_lock_circle(s, s.fx, s.fy);
                if (!s.run_can && in_lock) s.run_armed = false;  // (there while it couldn't lock - the weapon out as it
                                                                 // was sheathed: not until it has come back inside the rim)
                bool over = s.run_can && s.run_armed && in_lock;
                if (g_set.debug && over != s.run_over)
                    fprintf(stderr, "[touch] run lock %s\n", over ? "armed (the thumb on its circle)" : "no longer armed");
                s.run_over = over;
            } else {
                s.rim = s.run_over = false;
            }
            bool run_held = false;  // (the Run button)
            for (const Finger &f : s.f) run_held |= f.kind == F_BUTTON && !f.done && f.ctl == C_RUN;
            bool want = s.run_lock || s.rim || (run_held && !stick_run && g_game.weapon_drawn != 1 && !s.menu);
            if (b & BTN_X) s.run_x_until = now + RUN_X_WAIT;
            // In a village the single-screen mod opens its World Map with L and R together: no run's R while an L of the
            // touch controls is pressed (the camera button, a double tap) nor for two game frames after it, so that a dash
            // and a tap on the camera can't open the map (4u-native #31). The L and R buttons pressed together still do.
            // (not once the game's counter went back, a new run of it, as frames_ok; and a second at most, as a press
            // waits for the game's frames: FRAME_WAIT)
            if (b & BTN_L) s.run_l_frame = g_frames, s.run_l_ns = now;
            const bool town_l = g_game.place == 1 && s.run_l_ns != 0 && now - s.run_l_ns < FRAME_WAIT &&
                                (g_frames_fed ? g_frames >= s.run_l_frame && g_frames <= s.run_l_frame + 2
                                              : now - s.run_l_ns < RUN_L_WAIT);
            s.run = want && now >= s.run_x_until && !town_l;
            if (s.run) b |= BTN_R;
            static bool said_x, said_run;  // (the log: once a change, what held R said as it lets go too)
            static const char *said_why = "";
            if (g_set.debug && want && !s.run && !said_x)
                fprintf(stderr, "[touch] run: no R %s\n", town_l ? "with the touch L in a village (the map's L+R)" : "while X draws the weapon");
            said_x = want && !s.run;
            if (s.run) said_why = s.run_lock ? "the lock" : s.rim ? "the stick's rim" : "the Run button";
            if (g_set.debug && s.run != said_run) fprintf(stderr, "[touch] run: R %s (%s)\n", s.run ? "held" : "let go", said_why);
            said_run = s.run;
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
    case I_RUN: {  // a running boot with speed lines (the MHGU port's)
        ImVec2 boot[6] = {P(c, s, -.12f, -.62f), P(c, s, .22f, -.62f), P(c, s, .22f, .2f), P(c, s, .62f, .32f),
                          P(c, s, .62f, .56f), P(c, s, -.18f, .56f)};
        dl->AddConvexPolyFilled(boot, 3, col);
        dl->AddRectFilled(P(c, s, -.18f, -.62f), P(c, s, .22f, .56f), col, .06f * s);
        dl->AddRectFilled(P(c, s, -.18f, .22f), P(c, s, .62f, .56f), col, .12f * s);
        for (int i = 0; i < 3; i++)
            dl->AddLine(P(c, s, -.88f, -.36f + .3f * i), P(c, s, -.36f, -.36f + .3f * i), col, .1f * s);
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

// the d-pad's directions: right, left, up, down (the part's bits)
const float DPAD_DIR[4][2] = {{1, 0}, {-1, 0}, {0, -1}, {0, 1}};
// the direction pressed: its quarter of the well lit, and a gold arc outside the rim, which shows round the thumb
void dpad_lit(ImDrawList *dl, ImVec2 c, float r, int part, float a) {
    for (int i = 0; i < 4; i++) {
        if (!(part & (1 << i))) continue;
        float an = atan2f(DPAD_DIR[i][1], DPAD_DIR[i][0]);
        dl->PathLineTo(c);
        dl->PathArcTo(c, r * .84f, an - PI * .25f, an + PI * .25f, 14);
        dl->PathFillConvex(fade(IM_COL32(92, 62, 22, 255), a * .9f));
        dl->PathArcTo(c, r * 1.1f, an - PI * .23f, an + PI * .23f, 14);
        dl->PathStroke(fade(GLOW, a * .85f), r * .07f, ImDrawFlags_None);
        dl->PathArcTo(c, r * 1.2f, an - PI * .18f, an + PI * .18f, 12);
        dl->PathStroke(fade(GLOW, a * .3f), r * .05f, ImDrawFlags_None);
    }
}
// the d-pad: a round bronze well like the other buttons, a long cross on it with a cream arrow near each tip
void dpad(ImDrawList *dl, Geo g, int part, float a) {
    ImVec2 c = g.c;
    float r = g.r, hw = r * .3f, len = r * .8f, edge = r * .045f;  // (an arm's half width and length, its bronze edge)
    const auto &dir = DPAD_DIR;
    disc(dl, c, r, false, a);
    auto lit = [&](int i) { return (part & (1 << i)) != 0; };
    dpad_lit(dl, c, r, part, a);
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

// The menu pad: the stick's base, smaller, with four arrows for its notches (it is the d-pad); the direction pressed
// lit as on the d-pad, the knob under the thumb (kept inside the rim). The MHGU port's
void menu_pad(ImDrawList *dl, Geo g, ImVec2 knob, int part, float a, bool on) {
    ImVec2 c = g.c;
    float r = g.r;
    dl->AddCircleFilled(ImVec2(c.x, c.y + r * .07f), r * 1.05f, fade(IM_COL32(0, 0, 0, 120), a), 40);
    dl->AddCircleFilled(c, r, fade(IM_COL32(10, 7, 5, 255), a * (on ? .55f : .42f)), 48);
    dpad_lit(dl, c, r, part, a);
    dl->AddCircle(c, r, fade(BRONZE, a), 48, r * .08f);
    dl->AddCircle(c, r * .95f, fade(GOLD, a * .85f), 48, r * .03f);
    for (int i = 0; i < 4; i++) {  // (the arrows, pointing out near the rim; pressed, lit and a little farther out)
        bool lit = (part & (1 << i)) != 0;
        float dx = DPAD_DIR[i][0], dy = DPAD_DIR[i][1], px = -dy, py = dx, s = r * .14f, at = r * (lit ? .86f : .82f);
        ImVec2 tip(c.x + dx * at, c.y + dy * at), b0(tip.x - dx * s * 1.3f + px * s, tip.y - dy * s * 1.3f + py * s),
            b1(tip.x - dx * s * 1.3f - px * s, tip.y - dy * s * 1.3f - py * s);
        dl->AddTriangleFilled(tip, b0, b1, fade(lit ? GLOW : GOLD, a * (lit ? 1.0f : .85f)));
    }
    float kr = r * KNOB;
    disc(dl, knob, kr, part != 0, a);
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

// The sprint lock's circle above the stick (show_stick_run): a small disc with a padlock, open, faded in (a) as the thumb
// pushes up toward it; armed (the thumb on it: lifted there it locks), lit, the padlock still open; locked, lit, the
// padlock closed, a gold ring round it. The same in the MHGU port's runtime/touch.cpp
void lock_circle(ImDrawList *dl, ImVec2 c, float cr, float a, bool armed, bool locked) {
    if (a <= 0.0f) return;
    disc(dl, c, cr, armed || locked, a);
    padlock(dl, ImVec2(c.x, c.y - cr * .08f), cr * .74f, locked, fade(armed || locked ? GLOW : INK, a), fade(DEEP, a));
    if (locked) dl->AddCircle(c, cr * 1.18f, fade(GLOW, a * (.6f + .3f * sinf(SDL_GetTicksNS() / 1e9f * 4))), 32, cr * .1f);
}

void control(ImDrawList *dl, ImFont *font, const Layout &l, int i, float w, float h, const State &s, float a) {
    Geo g = place(l, i, w, h);
    if (i == C_RUNLOCK) {  // (the editor's: in a hunt it shows as the thumb pushes toward it, with the stick)
        lock_circle(dl, g.c, g.r, a * .8f, false, false);
        return;
    }
    if (i == C_STICK) {
        if ((s.stick_on || s.run_free) && !s.edit) {
            // (a locked run going on by itself: the knob held at the rim, forward)
            ImVec2 base(s.bx, s.by), knob = s.stick_on ? ImVec2(s.kx, s.ky) : ImVec2(s.bx, s.by - g.r);
            stick(dl, base, knob, g.r, a, true);
            // the sprint lock's circle: faded in as the thumb pushes up past the rim while a lock could start; lit with
            // the thumb on it (armed) and locked
            // (toward the circle, wherever the editor put it: above the stick unless moved)
            float v = s.run_lock || s.run_over ? 1.0f : 0.0f;
            const Geo lg = place(l, C_RUNLOCK, w, h);
            if (!s.run_lock && !s.run_over && s.run_can && s.stick_on) {
                float ux = lg.c.x - s.bx, uy = lg.c.y - s.by, ud = std::max(1.0f, hypotf(ux, uy));
                float dx = s.fx - s.bx, dy = s.fy - s.by, d = hypotf(dx, dy), up = d > 1.0f ? (dx * ux + dy * uy) / (d * ud) : 0.0f;
                float reach = std::max(1.0f, ud - lg.r - .9f * g.r);
                v = std::clamp((d - .9f * g.r) / reach, 0.0f, 1.0f) * std::clamp((up - LOCK_UP) / .15f, 0.0f, 1.0f);
            }
            lock_circle(dl, lg.c, lg.r, a * v, s.run_over, s.run_lock);
            if (s.run) {  // (running: a gold ring round the rim, and streaks behind the knob)
                dl->AddCircle(base, g.r * 1.12f, fade(GLOW, a * .85f), 48, g.r * .07f);
                float dx = knob.x - s.bx, dy = knob.y - s.by, n = std::max(1.0f, hypotf(dx, dy));
                dx /= n, dy /= n;
                for (int k = -1; k <= 1; k++) {
                    ImVec2 o(knob.x - dy * k * g.r * .22f, knob.y + dx * k * g.r * .22f);
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
    ImVec2 thumb = g.c;  // (the menu pad's knob: under its finger, inside the rim)
    for (const Finger &f : s.f)
        if (f.kind == F_BUTTON && !f.done) {
            if (f.ctl == i) mask |= i == C_ATTACK ? (f.part == 2 ? 3 : 1 << f.part)
                                  : i == C_CORNER ? (f.part == 2 ? 3 : f.part == 3 ? 4 : 1 << f.part)
                                  : is_dpad(i) || i == C_MPAD ? f.part : 1;
            if (i == C_MPAD && f.ctl == i && !s.edit) {
                float dx = f.x - g.c.x, dy = f.y - g.c.y, d = hypotf(dx, dy), m = g.r * (1 - KNOB * .5f);
                thumb = d > m ? ImVec2(g.c.x + dx / d * m, g.c.y + dy / d * m) : ImVec2(f.x, f.y);
            }
            if (i == C_HEAVY && f.ctl == C_LIGHT && f.part) mask |= 1;  // (the seam between them: both lit)
            if (f.keep & BTN_R) mask |= i == C_R ? 1 : i == C_CORNER && f.ctl == C_CORNER ? 4 : 0;  // (a guard kept)
        }
    if (of_mpad(i) && s.lit_ctl == i && SDL_GetTicksNS() < s.lit_until) mask |= i == C_MPAD ? s.lit_part : 1;  // (a tap)
    if (i == C_ATTACK) { pill(dl, font, g, mask, a); return; }
    if (i == C_MPAD) { menu_pad(dl, g, thumb, mask, a, thumb.x != g.c.x || thumb.y != g.c.y); return; }
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
    if (i == C_RUN && g_game.weapon_drawn == 1 && !s.edit) a *= .45f, mask = 0;  // (the weapon out: dimmed, it presses nothing)
    bool lit = mask != 0 || (i == C_SCREEN && g_draw_bottom);
    disc(dl, g.c, r, lit, a);
    if (i == C_RUN && s.run) dl->AddCircle(g.c, g.r * 1.16f, fade(GLOW, a * (.6f + .3f * sinf(SDL_GetTicksNS() / 1e9f * 4))), 40, g.r * .09f);
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

// A button's word on its plate: on one line, smaller where it is too long; where that would make it much smaller (a small
// panel, a narrow screen), on two lines split at the space nearest its middle, when those come out larger (never after a
// one-letter word: "Diseño:" over "a distancia", not "Diseño: a" over "distancia")
void button_text(ImDrawList *dl, ImFont *font, const Rect &b, float fs, ImU32 col, const char *t) {
    float room = (b.x1 - b.x0) * .92f, wide = font->CalcTextSizeA(fs, FLT_MAX, 0, t).x, one = wide > room ? fs * room / wide : fs;
    ImVec2 c((b.x0 + b.x1) * .5f, (b.y0 + b.y1) * .5f);
    int n = (int)strlen(t), cut = -1;
    for (int k = 2; k < n - 1; k++)
        if (t[k] == ' ' && t[k - 2] != ' ' && (cut < 0 || abs(2 * k - n) < abs(2 * cut - n))) cut = k;
    if (one < fs * .8f && cut > 0 && n < 64) {
        char first[64];
        snprintf(first, sizeof first, "%.*s", cut, t);
        const char *second = t + cut + 1;
        float w2 = std::max(font->CalcTextSizeA(fs, FLT_MAX, 0, first).x, font->CalcTextSizeA(fs, FLT_MAX, 0, second).x);
        float two = std::min(std::min(fs, fs * room / std::max(1.0f, w2)), (b.y1 - b.y0) * .44f);
        if (two > one * 1.1f) {
            text_center(dl, font, two, ImVec2(c.x, c.y - two * .52f), col, first);
            text_center(dl, font, two, ImVec2(c.x, c.y + two * .52f), col, second);
            return;
        }
    }
    text_center(dl, font, one, c, col, t);
}

// A grip of the editor's panel: four arrowheads round a cross on a dark plate with a gold edge (its title strip drags it)
void grip(ImDrawList *dl, const Rect &r, ImU32 col) {
    ImVec2 c((r.x0 + r.x1) * .5f, (r.y0 + r.y1) * .5f);
    float s = std::min(r.x1 - r.x0, r.y1 - r.y0) * .5f, a = s * .74f, hw = s * .25f, t = s * .12f;
    dl->AddRectFilled(ImVec2(r.x0, r.y0), ImVec2(r.x1, r.y1), fade(DEEP, .9f), s * .25f);
    dl->AddRect(ImVec2(r.x0, r.y0), ImVec2(r.x1, r.y1), fade(col, .75f), s * .25f, s * .08f);
    dl->AddLine(ImVec2(c.x - a * .6f, c.y), ImVec2(c.x + a * .6f, c.y), col, t);
    dl->AddLine(ImVec2(c.x, c.y - a * .6f), ImVec2(c.x, c.y + a * .6f), col, t);
    for (int q = 0; q < 4; q++) {
        ImVec2 d = q == 0 ? ImVec2(1, 0) : q == 1 ? ImVec2(-1, 0) : q == 2 ? ImVec2(0, 1) : ImVec2(0, -1), n(-d.y, d.x);
        ImVec2 tip(c.x + d.x * a, c.y + d.y * a), base(c.x + d.x * (a - hw * 1.6f), c.y + d.y * (a - hw * 1.6f));
        dl->AddTriangleFilled(tip, ImVec2(base.x + n.x * hw, base.y + n.y * hw), ImVec2(base.x - n.x * hw, base.y - n.y * hw), col);
    }
}
// The panel's grips, at both ends of its title strip (grab), in its measure u
void panel_grips(const Rect &grab, float u, Rect g[2]) {
    float gw = .07f * u, m = .012f * u;
    g[0] = {grab.x0 + m, grab.y0 + m * 1.4f, grab.x0 + m + gw, grab.y1 - m * .5f, true};
    g[1] = {grab.x1 - m - gw, g[0].y0, grab.x1 - m, g[0].y1, true};
}

void draw_editor(ImDrawList *dl, ImFont *font, ImFont *title, const State &s, float w, float h) {
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(w, h), IM_COL32(0, 0, 0, 96));
    // the game's parts the controls had better leave free: the item bar (a tap uses the item, a swipe turns it), and the
    // touch screen's corner where it last showed (the controls it covers move aside while it shows); named inside, at
    // their bottom edge, out of the controls' way
    Rect panel, btn[E_COUNT], grab;
    float u = editor_panel(g_epanel, w, h, &panel, btn, &grab);
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
        if (i == C_MPAD)  // (its tabs and SELECT with it, where it puts them)
            for (int j : {C_ML, C_MR, C_MSELECT})
                if (mpad_member(j)) control(dl, font, s.layout, j, w, h, s, 1.0f);
        Geo g = place(s.layout, i, w, h);
        float rr = i == C_ATTACK ? g.r * PILL_HALF * .75f + g.r : i == C_MPAD ? MPAD_BELOW * s.layout.scale[i] * h : g.r;
        if (i == s.sel) {
            float t = SDL_GetTicksNS() / 1e9f, pulse_a = .65f + .35f * sinf(t * 6);
            if (i == C_MPAD) {
                dl->AddCircle(g.c, g.r * MPAD_GRAB, fade(GLOW, pulse_a), 48, .006f * h);
            } else if (i == C_ATTACK) {
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
    float line = .004f * u;
    plate(dl, panel, line, 1.0f, false);
    // (the panel picked: pulsing round it, its grips lit, its size by the right one)
    bool picked = s.sel == SEL_PANEL;
    if (picked) {
        float pulse_a = .65f + .35f * sinf(SDL_GetTicksNS() / 1e9f * 6), m = .004f * h;
        dl->AddRect(ImVec2(panel.x0 - m, panel.y0 - m), ImVec2(panel.x1 + m, panel.y1 + m), fade(GLOW, pulse_a), 0, .006f * h);
    }
    Rect grips[2];
    panel_grips(grab, u, grips);
    for (const Rect &g : grips) grip(dl, g, picked ? GLOW : GOLD);
    text_center(dl, title, .036f * u, ImVec2((panel.x0 + panel.x1) * .5f, panel.y0 + .045f * u), GLOW, T("Controls and HUD"));
    if (picked) {
        char pct[16];
        snprintf(pct, sizeof pct, "%d%%", (int)lroundf(std::clamp(g_epanel.k, EPANEL_MIN, 1.0f) * 100));
        float pf = .03f * u, pw = font->CalcTextSizeA(pf, FLT_MAX, 0, pct).x;
        text_center(dl, font, pf, ImVec2(grips[1].x0 - .012f * u - pw * .5f, panel.y0 + .045f * u), GLOW, pct);
    }
    const char *say = T(picked              ? "The panel: drag its title to move it  -  pinch or use - / + for its size"
                        : s.sel >= ZONE_SEL ? "Drag it where you want it in your hunts"
                        : s.sel == C_MPAD   ? "Shows while a menu of the game's is open - drag to move, pinch or use - / + for its size"
                        : s.sel >= 0        ? "Drag to move  -  pinch or use - / + for its size"
                                            : "Tap a control or a part of the game's screen to pick it, drag it to move it");
    float ss = .022f * u, room = grips[1].x0 - grips[0].x1 - .024f * u, wide = font->CalcTextSizeA(ss, FLT_MAX, 0, say).x;
    if (wide > room) ss *= room / wide;  // (a narrow screen, a long sentence: on one line, smaller)
    text_center(dl, font, ss, ImVec2((panel.x0 + panel.x1) * .5f, panel.y0 + .08f * u), INK, say);
    static const char *const STYLE_LABEL[STYLE_COUNT] = {"Attacks: pill", "Attacks: buttons", "Attacks: corner"};
    const char *const LABEL[E_COUNT] = {"-", "+", ranged_layout() ? "Layout: ranged" : "Layout: melee", STYLE_LABEL[s.layout.style],
                                        "Defaults", "Cancel", "Save"};
    for (int i = 0; i < E_COUNT; i++) {
        bool off = ((i == E_SMALLER || i == E_BIGGER) && s.sel < 0 && !picked) || (i == E_STYLE && ranged_layout());
        bool lit = false;
        for (const Finger &f : s.f)
            if (f.kind == F_MENU && f.ctl == i) lit = true;
        plate(dl, btn[i], line * .8f, off ? .45f : 1.0f, lit);
        button_text(dl, font, btn[i], (i < 2 ? .05f : .028f) * u, fade(i == E_SAVE ? GLOW : INK, off ? .45f : 1.0f), T(LABEL[i]));
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

// The target camera's icon while the lock-on is on: a gold ring round it, pulsing while the camera follows a monster
// (the MHGU port's cue). The icon takes no finger: the padlock that turns the lock-on is L's
void target_hint(ImDrawList *dl, const Rect &z, float h, float a, uint64_t now) {
    if (!lock_on_allowed() || !Port::Hooks::LockOn()) return;
    ImVec2 c((z.x0 + z.x1) * .5f, (z.y0 + z.y1) * .5f);
    float r = std::max(z.x1 - z.x0, z.y1 - z.y0) * .5f;
    float p = Port::Hooks::LockOnFollowing() ? .6f + .3f * sinf(now / 1e9f * 4) : .45f;
    dl->AddCircle(c, r * 1.02f, fade(GLOW, a * p), 40, .005f * h);
}

// The menu pad's group: in as a menu opens (MPAD_IN), held a moment as it closes (MPAD_HOLD: the next window of a menu
// may follow), then out (MPAD_OUT): the MHGU port's timing
float g_mpad_a = 0;
void mpad_fade(const State &s, uint64_t now) {
    static uint64_t last = 0;
    const float dt = last ? (float)std::min<uint64_t>(now - last, 100 * MS) : 0;
    last = now;
    if (in_mode(C_MPAD, s.menu)) g_mpad_a = std::min(1.0f, g_mpad_a + dt / (float)MPAD_IN);
    else if (s.menu || now - s.menu_off_ns >= MPAD_HOLD || !show(TB_MDPAD)) g_mpad_a = std::max(0.0f, g_mpad_a - dt / (float)MPAD_OUT);
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
    Rect target = target_zone(s.menu);
    if (!s.visible) {  // (hidden: the settings button alone, a little fainter; the lock-on's ring, opaque as the HUD)
        if (!covered(s.layout, C_SETTINGS, w, h)) control(dl, font, s.layout, C_SETTINGS, w, h, s, a * .75f);
        if (target.ok) target_hint(dl, target, h, 1.0f, now);
        return;
    }
    mpad_fade(s, now);
    for (int i = 0; i < C_COUNT; i++) {
        if (of_mpad(i) || !in_mode(i, s.menu) || covered(s.layout, i, w, h)) continue;
        // (in a menu of the game's the player's buttons stay, faded so its window reads and taps under them; what the
        // menu pad's group covers goes as the pad comes in, and comes back as it goes)
        float k = s.menu && i != C_MY ? .5f : 1.0f;
        if (g_mpad_a > 0 && under_mpad(s.layout, i, w, h)) k *= 1 - g_mpad_a;
        if (k > .01f) control(dl, font, s.layout, i, w, h, s, a * k);
    }
    if (g_mpad_a > 0 && show(TB_MDPAD))  // (the menu pad's group, over the rest)
        for (int i : {C_MPAD, C_ML, C_MR, C_MSELECT})
            if ((i == C_MPAD || mpad_member(i)) && !covered(s.layout, i, w, h)) control(dl, font, s.layout, i, w, h, s, a * g_mpad_a);
    Rect item = item_zone(s.menu);
    if (item.ok) item_hint(dl, s, item, h, a, now);
    if (target.ok) target_hint(dl, target, h, a, now);
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
    s.run_lock = s.run_free = s.run_armed = s.run_over = false;
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

std::uint32_t touch_combo_buttons(int k) {
    ensure_loaded(g_s);
    if (k < 0 || k >= COMBOS) return 0;
    const uint64_t m = g_combo[k];  // (ZL and ZR: bits 16 and 17 here, 14 and 15 there)
    return (uint32_t)(m & 0xfff) | (m >> 16 & 1 ? 1u << 14 : 0) | (m >> 17 & 1 ? 1u << 15 : 0);
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

bool touch_editing() { return g_s.edit; }

// The layout editor holds the game (android_main), but while a part of the game's HUD is dragged the game runs EDIT_RUN
// of every EDIT_STEP: it draws the part where it goes, and where the editor frames it comes from what it draws (held,
// the part and its frame stayed where they were: the owner's Galaxy, "se rompió el mover la interfaz"). The MHGU port
// takes a step of its game every 100 ms the same way
bool touch_editor_holds() {
    if (!g_s.edit) return false;
    const uint64_t EDIT_STEP = 100 * MS, EDIT_RUN = 40 * MS, now = SDL_GetTicksNS();
    if (now - g_hud_moved_ns > 2 * EDIT_STEP) return true;  // (nothing dragged just now: held)
    return now % EDIT_STEP >= EDIT_RUN;
}

void touch_edit_begin() {
    State &s = g_s;
    ensure_loaded(s);
    if (!s.edit) edit_begin(s);
}

void touch_haptic_tap() {
    haptics_tap();
}
