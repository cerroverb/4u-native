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
//     and L (the camera behind the hunter, or on the target), and the Lock-on button: the port's own (it presses none
//     of the game's buttons: a long press on L turned it up to 0.1.9.2, and L held lost its own uses, a bowgun's ammo
//     with L + X / B), a tap turns the lock-on - the camera keeps turning to the monster, as in the newer games - on and
//     off, as a controller's right stick click does; the MHGU port's the same. START (the game's menu), the touch
//     screen's corner and the settings at the left edge
//   - in a menu of the game's the layout stays, faded so that the window reads under it, and the menu pad comes in
//     under the right thumb (the MHGU port's, the same place, look and timing): a small pad that is the d-pad (a wide
//     dead middle, one direction at a time, the knob under the thumb), L and R as tabs above it, a small SELECT below
//     it while the hunt's is hidden; the buttons its tabs cover go while it shows. The stick moves the cursor too (the
//     circle pad), and a drag anywhere else steps it, a step every so far, as a list on a phone. With the game's
//     touch-screen menus on the top screen (single-screen mode), a control over them is drawn fainter still, so that the
//     whole menu reads (menus_area: the item box's last column under the right thumb's buttons on a 20:9 screen); it
//     works as ever and lights while touched, and nothing moves
//   - the game's own HUD is touched where it is drawn (the hooks say where: Port::Hooks::GameState::hud). The item bar:
//     a tap uses the item (Y; with the weapon out, Y puts it away and the item is used once it is away), a swipe turns
//     the bar (the hooks take the game's own step, as the touch screen's bar does: the strip follows the finger; no L,
//     which would turn the camera too), a long press holds it open with its neighbours (and a bow's coatings) while a
//     swipe turns it; with a bow or a bowgun, up or down changes the ammo or the coating (L held with X or B). The
//     target camera's icon (the single-screen mod's, on the top screen) takes no finger: while the lock-on is on a
//     gold ring goes round it (the Lock-on button turns the lock-on)
//   - with a bow or a bowgun in hand, in a hunt, a layout of their own: a large button under the thumb that fires
//     (a drag on it, held, aims: the right thumb can't be on the screen and on a button at once), a smaller one that
//     reloads (a bow: its melee attack), aim, crouching fire (a bow: its coating), evade, sheathe, and arrows for the
//     ammo. Laid out on its own in the editor
//   - up to six combined buttons of the player's own (the settings' Buttons on the screen: A+X, say), each pressing
//     its buttons together; placed in the editor
//   - anywhere else: dragging turns the camera by the angle moved, as in a phone game (the port's camera hook while the
//     game's camera takes it, Port::Hooks::CameraTurnActive; elsewhere as the C-stick). No double tap does anything
//     there (up to 0.1.9.3 one centred the camera, L: pressed by accident in a fight it turned the camera away); the
//     camera button does it. The MHGU port's the same
//   - the touch screen's corner, while it shows, is the game's: a finger there is the stylus, and the controls it would
//     cover move out from under it, the whole side's group together (above it when they fit, else beside it). Its
//     button (the 3DS's two screens) shows and hides it
// Every control can be moved and resized (the layout editor: a long press on the settings button), kept in
// touch_layout.txt in the app's folder, with which buttons show and the opacity. The editor moves the game's HUD too,
// part by part (the clock, the health and stamina bars, the sharpness, the name plates, the area's map, the target
// camera, the item bar: the hooks move the game's own layout objects; hud_<part> in mh4u.ini; 1.0.2: each also sized with
// - / + or a pinch and hidden with Hide, and a button of its own shows and hides the map). Shown while no controller is
// connected (controls auto), always (on) or never (off: the settings button alone).
// 1.0.1: any control the editor moves takes a picture of the player's, drawn alone in its place (pics_*); a long press
// on an empty spot hides every control and another shows them (hide_finger); a finger slid from one button onto another
// keeps both pressed (slide_group); the item bar's touch area is sized in the editor (Layout.item_zone); the camera's
// drag can be smoothed (camera_smooth)
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

#include <dirent.h>
#include <sys/stat.h>
#ifdef __ANDROID__
#include <jni.h>
#endif

#include "app_jni.h"  // (app_call_activity: the layout editor's Add pictures)
#include "app_settings.h"
#include "chat/chat_overlay.h"  // (the chat button: a finger on it is its own)
#include "chat/chat.h"          // (1.0.2: the chat's button in the editor, while it is on: chat_bubble_on)
#include "game_options.h"
#include "settings_locale.h"
#include "settings_panel.h"
#include "controls.h"
#include "town.h"
#include "weapon_tree_ui.h"
#include "hooks.h"
#include "imgui.h"
#include "imgui_internal.h"  // (ImGui::RegisterUserTexture: the players' pictures on the buttons)
#include "perfstats.h"       // (perfstats_rect: the FPS counter's plate, which the editor moves)
#include "hunt_bars.h"       // (hunt_bars_rect: the monsters' health bars, which the editor moves and sizes; 1.0.1)
#include "quick_items.h"     // (the quick items' piece: placed and sized in the editor like the bars; 1.0.1.1)
#include "unzip.h"           // (a profile's .zip: Import, 1.0.2)
#include "zipw.h"            // (...and Export)
#include <ctime>
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
// [Controls] Haptics in mh4u-app.ini (the settings panel's Vibrate on touch) is on. The PC has no phone to vibrate.
#ifndef __ANDROID__
void haptics_tap() {}
#else
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
#endif

#ifndef __ANDROID__
// (the PC) A real touch screen's finger has touched it (not the mouse's, SDL_MOUSE_TOUCHID): the touch controls show at
// Auto from then on, the settings button with them, as the MHGU port's desktop; until then a keyboard and a mouse play
bool g_screen_touched = false;
// (the PC) The editor opened without the touch controls on the screen (a keyboard and a mouse, a controller): the HUD's
// parts alone, the mouse drags them and its wheel sizes them, and the panel only has what they use
bool g_edit_hud = false;
const char *pc_say(const char *en, const char *es) { return settings_spanish() ? es : en; }
#endif

// The lock-on ([Controls] LockOn in mh4u-app.ini, the settings panel's row; on unless turned off): the Lock-on button
// (C_LOCKON) or a controller's right stick click turns it on and off (Port::Hooks::SetLockOn: the hooks turn the quest
// camera)
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
       TB_LOCKON,                   // (the port's Lock-on button)
       TB_FLOATSTICK,               // (the floating stick: it comes to where the thumb lands near it, off by default)
       TB_MENU_LR, TB_DPAD_MENUS,   // (the menus' ways: the menu pad's L and R tabs; the hunt's d-pad kept in menus)
       TB_SLIDEBOTH,                // (a finger slid from a button onto another keeps both pressed: slide_group, 1.0.1)
       TB_CAMRIGHT,                 // (a drag turns the camera only from the screen's right half: 1.0.2, off by default)
       TB_MAPBTN,                   // (1.0.2: the map's button, C_MAPTOG)
       TB_WEAPON_TREE,              // (1.0.2: the Smithy's weapon tree's button, weapon_tree_ui.h; the MHGU port's ShowWeaponTree)
       TB_COUNT };
const char *const TB_KEY[TB_COUNT] = {"attacks", "dodge", "guard", "sheathe", "start", "select", "dpad", "l", "zl", "zr",
                                      "screen", "stick", "ranged", "fire", "second", "aim", "ranged_special", "ammo",
                                      "menu_dpad", "item_arrows", "stick_run", "guard_slide", "lock_on", "floating_stick",
                                      "menu_lr", "dpad_menus", "slide_both", "camera_right_half", "map_button", "weapon_tree"};
// (off: SELECT and the d-pad in a hunt, which MH4U's hunts don't need, and the floating stick. The item bar has no
// arrows: a tap on it uses the item, a swipe turns it, a long press opens it. The menu pad's L and R on and the d-pad in
// menus off: as up to 0.1.9.3. A slide between buttons keeps both, the owner's call for 1.0.1. The camera from either
// half of the screen, as before. The map's button and the weapon tree's on, 1.0.2)
const bool TB_DEFAULT[TB_COUNT] = {1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1};
struct Settings {
    int controls = TOUCH_AUTO;
    float camera = 1.0f;   // the camera's turn for a drag (1: about 90 degrees a screen height)
    float smooth = 0.0f;   // the drag's turn smoothed (camera_smooth, 0 off .. 1: cam_smooth_tau), off by default
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

// The combined buttons: the game's buttons each presses ("A+X"), 0 none. 1.0.2: fifteen, each layout (each profile) with
// its own (Layout.combo, combo <n> in its file); g_combo is touch_layout.txt's combo_buttons of before (six), which a
// layout whose file has no combo lines (a profile of before) takes, and which stay in the file for an older version
// (never changed now); g_pub_combo the layout in use's (publish), for the buttons and touch_combo_buttons
const int COMBOS = 15, COMBOS_OLD = 6;
uint64_t g_combo[COMBOS_OLD];
uint64_t g_pub_combo[COMBOS];
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
// (1.0.2) A part of the HUD in the editor and the files: its offset (dx, dy: the top screen's pixels from the game's place),
// its own size (HUD_K_MIN .. HUD_K_MAX, times the HUD's size of the settings) and hidden (1), as mh4u.ini's
// hud_<key> = dx dy size hidden (hud_value: "dx dy" alone while the size is 1 and it shows, as up to 1.0.1.1)
const int HUD_V = 4;
const float HUD_K_MIN = .5f, HUD_K_MAX = 2.0f;
float g_hud_size = 1.0f;  // (the settings' HUD size, mh4u.ini hud_size: read as the editor opens)
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
    C_LOCKON,                          // (the port's Lock-on button)
    C_MAPTOG,                          // (1.0.2: shows and hides the map on the main screen: the HUD's Area map)
    C_COMBO, C_COMBO_END = C_COMBO + COMBOS - 1,                                           // (the player's combined buttons)
    // the emulator-style layout's (a profile of that kind, Layout.kind: every one presses its button of the game's and
    // nothing else, as an emulator's pad on the screen; the stick, the settings, touch screen, town and Lock-on buttons,
    // the item bar's arrows and the combined buttons are the same controls in both kinds). The MHGU port's the same
    C_EA, C_EB, C_EX, C_EY, C_EL, C_ER, C_EZL, C_EZR, C_ESTART, C_ESELECT, C_EDPAD,
    C_STICK,                                                                               // (laid out: touch_layout.txt)
    C_ML, C_MR, C_MSELECT,  // (the menu pad's: its page tabs above it, as the shoulders, and SELECT below it; placed from it)
    C_TREE,                 // (1.0.2: the Smithy's weapon tree, weapon_tree_ui.h; last, so the layouts' bits stay)
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
            I_UP, I_DOWN, I_SKIP, I_TOWN, I_LEFT, I_RIGHT, I_RUN, I_LOCKON, I_MAP, I_TREE };

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
    {"camera", "Camera  L", I_CAMERA, "L", BTN_L, true, .31f, .74f, .045f},
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
    // the lock-on (show_lock_on, with [Controls] LockOn): the port's own, it presses none of the game's buttons; a tap
    // turns the lock-on (lock_toggle). On the right thumb's arc above Y and left of L, as far from both as they are from
    // each other (the MHGU port's place, from its arc). In a quest only: elsewhere it is in the editor alone
    {"lock_on", "Lock-on", I_LOCKON, "", 0, true, .43f, .68f, .045f},
    // (1.0.2) the map's button (show_map_button, on): a tap shows or hides the map the game draws on the main screen
    // (the HUD's Area map: hud_map's hidden, as the editor's Hide). In a quest only. At the left edge's column, below the
    // touch screen's and the town's buttons, by the screen's bottom corner
    {"map_toggle", "Map: show / hide", I_MAP, "", 0, false, .065f, .19f, .046f},
    // the combined buttons (shown once the settings give them buttons), in two columns left of the attacks
    {"combo1", "Combined button 1", I_LETTER, "", 0, true, .62f, .30f, .05f},
    {"combo2", "Combined button 2", I_LETTER, "", 0, true, .62f, .44f, .05f},
    {"combo3", "Combined button 3", I_LETTER, "", 0, true, .62f, .58f, .05f},
    {"combo4", "Combined button 4", I_LETTER, "", 0, true, .75f, .30f, .05f},
    {"combo5", "Combined button 5", I_LETTER, "", 0, true, .75f, .44f, .05f},
    {"combo6", "Combined button 6", I_LETTER, "", 0, true, .75f, .58f, .05f},
    // (1.0.2) the nine more, empty until the settings give them buttons: above the first ones' two columns, then a third
    // column left of them (clear of the stick on a 4:3 screen)
    {"combo7", "Combined button 7", I_LETTER, "", 0, true, .62f, .72f, .05f},
    {"combo8", "Combined button 8", I_LETTER, "", 0, true, .75f, .72f, .05f},
    {"combo9", "Combined button 9", I_LETTER, "", 0, true, .62f, .86f, .05f},
    {"combo10", "Combined button 10", I_LETTER, "", 0, true, .75f, .86f, .05f},
    {"combo11", "Combined button 11", I_LETTER, "", 0, true, .87f, .30f, .05f},
    {"combo12", "Combined button 12", I_LETTER, "", 0, true, .87f, .44f, .05f},
    {"combo13", "Combined button 13", I_LETTER, "", 0, true, .87f, .58f, .05f},
    {"combo14", "Combined button 14", I_LETTER, "", 0, true, .87f, .72f, .05f},
    {"combo15", "Combined button 15", I_LETTER, "", 0, true, .87f, .86f, .05f},
    // the emulator-style layout: A, B, X and Y in a diamond under the right thumb (above the item bar's corner), the d-pad
    // below the stick on the left, the shoulders at the screen's edges (the inner pair above), SELECT and START inward of
    // them; a drag anywhere turns the camera (the MHGU port's places, the diamond higher, as this port's attacks)
    {"emu_a", "A", I_LETTER, "A", BTN_A, true, .14f, .42f, .058f},
    {"emu_b", "B", I_LETTER, "B", BTN_B, true, .26f, .30f, .058f},
    {"emu_x", "X", I_LETTER, "X", BTN_X, true, .26f, .54f, .058f},
    {"emu_y", "Y", I_LETTER, "Y", BTN_Y, true, .38f, .42f, .058f},
    {"emu_l", "L", I_LETTER, "L", BTN_L, false, .17f, .70f, .05f},
    {"emu_r", "R", I_LETTER, "R", BTN_R, true, .17f, .70f, .05f},
    {"emu_zl", "ZL", I_LETTER, "ZL", BTN_ZL, false, .17f, .84f, .05f},
    {"emu_zr", "ZR", I_LETTER, "ZR", BTN_ZR, true, .17f, .84f, .05f},
    {"emu_start", "START", I_MENU, "START", BTN_START, true, .42f, .70f, .046f},
    {"emu_select", "SELECT", I_SELECT, "SELECT", BTN_SELECT, false, .42f, .70f, .046f},
    {"emu_dpad", "D-pad", I_DPAD, "", 0, false, .50f, .17f, .105f},
    {"stick", "Movement", I_STICK, "", 0, false, .29f, .27f, .115f},
    // the menu pad's (x, y unused: place_raw): the shoulders as tabs (the item box's pages, the shops'), SELECT
    {"ml", "L", I_LETTER, "L", BTN_L, true, 0, 0, .040f},
    {"mr", "R", I_LETTER, "R", BTN_R, true, 0, 0, .040f},
    {"mselect", "SELECT", I_SELECT, "SELECT", BTN_SELECT, true, 0, 0, .030f},
    // (1.0.2) the Smithy's weapon tree (weapon_tree_ui.h): only while the Smithy shows a weapon, a menu of the game's
    // where the town's button never shows; in the left column, one place below the map's button (which shows in a
    // quest only), so the two never sit on each other in the editor
    {"weapon_tree", "Weapon tree", I_TREE, "", 0, false, .065f, .07f, .046f},
};
enum { STYLE_PILL, STYLE_SEPARATE, STYLE_CORNER, STYLE_COUNT };  // how the attacks and the guard are laid out
int g_style = STYLE_PILL;    // (Layout.style)
// A layout's kind (Layout.kind, layout_kind in the file): the port's own (the attack pill, the menu pad, the layout for
// bows and bowguns, the buttons the settings' Buttons on the screen show) or the emulator-style one (A, B, X, Y in a
// diamond, the shoulders, START, SELECT and the d-pad, each the game's button alone, each shown or hidden in the editor).
// Any of the ten profiles holds either (the profiles, below). The MHGU port's the same
enum { KIND_OURS, KIND_EMU, KIND_COUNT };
int g_kind = KIND_OURS;  // (Layout.kind)
uint64_t g_off;          // (Layout.off: the emulator-style controls hidden in its editor, a bit each)
bool emu_ctl(int i) { return i >= C_EA && i <= C_EDPAD; }
int g_ranged = 0;            // the weapon (UiGameState.ranged) while the layout for it applies, else 0
int g_edit_set = -1;         // the editor's: which layout it shows, 0 the usual one, 1 the ranged one; -1 not open
// (a bow's or a bowgun's buttons are no menu's: in the game's menus the other set, with A, B, X and Y as they are)
bool ranged_layout() {
    if (g_kind == KIND_EMU) return false;  // (the emulator-style layout: the same for every weapon)
    return g_edit_set >= 0 ? g_edit_set == 1 : g_ranged != 0 && g_game.menu != 1;
}
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
    case C_LOCKON: return TB_LOCKON;
    case C_MAPTOG: return TB_MAPBTN;
    case C_TREE: return TB_WEAPON_TREE;
    default: return -1;
    }
}
bool is_tab(int i) { return i == C_ML || i == C_MR; }  // (the menu pad's shoulders, drawn as tabs)
bool is_dpad(int i) { return i == C_HDPAD || i == C_EDPAD; }
bool of_mpad(int i) { return i == C_MPAD || is_tab(i) || i == C_MSELECT; }  // (the menu pad's group)
// (its L and R while show_menu_lr says so - the MHGU port's ShowMenuLR -, SELECT while the hunt's is hidden)
bool mpad_member(int i) { return (is_tab(i) && show(TB_MENU_LR)) || (i == C_MSELECT && !show(TB_SELECT)); }
bool g_town_edit;  // (the layout editor is open: the buttons that come and go show there wherever the game is)
// One layout, the player's, in the game's menus too (`menu`: one is open): they add the menu pad and its group, and Y
// where the layout has none; the item bar's arrows are a hunt's, beside the bar while it shows
// Not in the front end (the title, the choice of a save, the character's creation), as the MHGU port: its lists are
// moved with the stick, and nobody walks there (with the stick hidden the pad shows there too: nothing else would)
bool mpad_menu(bool menu) { return g_town_edit || (menu && !(g_game.front_end == 1 && show(TB_STICK))); }
// A control the emulator-style layout's editor can hide (Layout.off): any of its own but the settings button, the way back
// to the settings (the settings' Buttons on the screen hide the port's own layout's: its editor has no Hide for those)
bool hideable(int i) {
    return g_kind == KIND_EMU && i != C_SETTINGS &&
           (emu_ctl(i) || i == C_STICK || i == C_LOCKON || i == C_SCREEN || i == C_TOWN || i == C_MAPTOG || i == C_TREE ||
            is_combo(i));
}
// The emulator-style layout: its own buttons, the stick, the settings, touch screen, town and Lock-on buttons, the item
// bar's arrows and the combined buttons, each but the settings button unless its editor hid it (the editor shows those
// too, faded, for its Show); in a menu of the game's nothing changes (its d-pad is always there: no menu pad)
bool emu_mode(int i, bool menu) {
    if (i != C_SETTINGS && (g_off >> i & 1) && !g_town_edit) return false;
    if (emu_ctl(i) || i == C_SETTINGS || i == C_STICK) return true;
    if (i == C_TOWN) return !menu && (g_town_edit || town_available());
    if (i == C_TREE) return show(TB_WEAPON_TREE) && (g_town_edit || weapon_tree_available());  // (1.0.2: the Smithy's)
    if (i == C_SCREEN) return show(TB_SCREEN);
    if (i == C_ITEML || i == C_ITEMR)
        return show(TB_ITEMARROWS) && (g_town_edit || (!menu && g_game.place == 2 && g_game.item_bar_shown != 0));
    if (i == C_RUNLOCK) return g_town_edit && show(TB_STICKRUN);
    if (i == C_LOCKON) return lock_on_allowed() && show(TB_LOCKON) && (g_town_edit || g_game.place == 2);
    if (i == C_MAPTOG) return show(TB_MAPBTN) && (g_town_edit || g_game.place == 2);
    if (is_combo(i)) return g_pub_combo[i - C_COMBO] != 0;
    return false;
}
bool in_mode(int i, bool menu) {
    if (g_kind == KIND_EMU) return emu_mode(i, menu);
    if (emu_ctl(i)) return false;  // (the emulator-style layout's alone)
    if (i == C_TOWN) return !menu && (g_town_edit || town_available());
    if (i == C_TREE) return show(TB_WEAPON_TREE) && (g_town_edit || weapon_tree_available());  // (1.0.2: the Smithy's)
    // (the run button: with show_stick_run 0, in a hunt and in towns, either layout, never in a menu - R turns its pages)
    if (i == C_RUN) return !menu && !show(TB_STICKRUN);
    if (i == C_LAYOUT) return false;  // (the menu layout's button: gone, the game says when a menu is open)
    if (i == C_MPAD) return show(TB_MDPAD) && mpad_menu(menu);
    if (of_mpad(i)) return show(TB_MDPAD) && mpad_member(i) && mpad_menu(menu);
    if (i == C_MY) return !show(TB_SHEATHE) && (menu || g_town_edit);
    if (i == C_ITEML || i == C_ITEMR)
        return show(TB_ITEMARROWS) && (g_town_edit || (!menu && g_game.place == 2 && g_game.item_bar_shown != 0));
    if (i == C_RUNLOCK) return g_town_edit && show(TB_STICKRUN);  // (in a hunt: drawn and armed with the stick)
    // (the Lock-on button: with the lock-on allowed, in a quest - nothing is locked on elsewhere)
    if (i == C_LOCKON && (!lock_on_allowed() || (!g_town_edit && g_game.place != 2))) return false;
    if (i == C_MAPTOG) return show(TB_MAPBTN) && (g_town_edit || g_game.place == 2);  // (a quest's; any layout)
    if (i == C_SETTINGS) return true;
    if (i == C_MENU || i == C_SCREEN) return show(show_setting(i));
    if (is_combo(i)) return g_pub_combo[i - C_COMBO] != 0;  // (the hunt's, either layout, once it has buttons)
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
const uint64_t RUN_X_WAIT = 400 * MS;  // (X pressed: no run's R until this long after, but for X + A in a run whose R
                                       // was held this long already: frame_input)
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
// (camera_smooth: the time constant the drag's turn is given over, 12 ms to 152 ms at 1, as the MHGU port's CameraSmoothing)
inline float cam_smooth_tau(float smooth) { return .012f + .14f * smooth; }

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

const int IMAGE_NAME = 48;  // (a picture's file name, Layout.image)
// The item bar's touch area (Layout.item_zone: the editor's - / + and a pinch with the item bar picked), times its own
// size about the bar's middle (1: the bar and a margin of 15 % of its height round it, as up to 1.0)
const float ITEM_ZONE_MIN = .5f, ITEM_ZONE_MAX = 2.0f;
const int PROFILES_N = 10;  // the layout profiles (below)
const int HUD_N = UiGameState::HUD_PARTS;
struct Layout {
    float x[C_COUNT], y[C_COUNT], scale[C_COUNT];
    int style;                        // STYLE_*
    int kind;                         // KIND_*: layout_kind in the file (none: the port's own)
    int profile;                      // the profile it is, 1..PROFILES_N (profile in touch_layout.txt; none: 1)
    bool profile_line;                // (the file named it: one without was made before the profiles)
    uint64_t off;                     // the emulator-style layout's controls hidden in its editor (hidden <key>)
    uint64_t hold;                    // controls that stay pressed: a tap presses, the next lets go (hold <key>)
    float fps_x, fps_y;               // the FPS counter's middle, in the screen's width and height; < 0 its own place,
                                      // at the top (fps_counter in the file)
    float bars_x, bars_y, bars_k;     // the monsters' health bars (hunt_bars.cpp, 1.0.1): their block's top right corner
                                      // in the top screen's width and height from its left and top; < 0 their own place,
                                      // the top right corner; and their size, 1 as they came (monster_bars in the file)
    float items_x, items_y, items_k;  // the quick items' piece (quick_items.cpp, 1.0.1.1): its centre in the window's
                                      // width and height; < 0 its own place, the bottom edge's middle; its size
                                      // (quick_items in the file)
    float chat_x, chat_y, chat_k;     // (1.0.2) the chat's button (chat/chat_overlay.cpp): its centre in the window's width
                                      // and height; < 0 its own place (CHAT_HOME); its size (chat_button in the file)
    char image[C_COUNT][IMAGE_NAME];  // the player's own picture on a control: a file in the profiles' images folder
                                      // (image <key> <file>); "" none. The stick's: its base
    char knob[IMAGE_NAME];            // the stick's knob's (image_knob <file>, as the MHGU port's; 1.0.1)
    float item_zone;                  // the item bar's touch area, times its own size (item_zone k: 1 as up to 1.0)
    uint64_t combo[COMBOS];           // its combined buttons' game buttons (combo <n> <buttons> in the file, 1.0.2; "-" none)
    bool has_combos;                  // (its file had them: else it took g_combo, the combined buttons of before)
    // (a profile's file only: the game's HUD parts where it has them, hud_<key> x y, as mh4u.ini's: the layout in use
    // keeps them there)
    float hud[HUD_N][HUD_V];
    bool has_hud;
};
// The emulator-style layout's own places for the controls both kinds have (DEFS has the port's): the stick a little
// higher, over the d-pad; the settings, touch screen and town buttons in their column at the left edge, lower; the
// Lock-on button above the diamond. The MHGU port's the same where it has them
const struct { int c; float x, y; } EMU_PLACES[] = {{C_STICK, .27f, .42f}, {C_SETTINGS, .065f, .64f}, {C_LOCKON, .45f, .58f},
                                                    {C_SCREEN, .065f, .50f}, {C_TOWN, .065f, .36f}};
void def_place(int kind, int i, float *x, float *y) {
    *x = DEFS[i].x, *y = DEFS[i].y;
    if (kind == KIND_EMU)
        for (const auto &e : EMU_PLACES)
            if (e.c == i) *x = e.x, *y = e.y;
}
Layout defaults(int kind = KIND_OURS) {
    Layout l = {};
    for (int i = 0; i < C_COUNT; i++) def_place(kind, i, &l.x[i], &l.y[i]), l.scale[i] = 1.0f;  // (the menu layout's: unused)
    l.kind = kind, l.profile = 1, l.fps_x = l.fps_y = -1.0f, l.item_zone = 1.0f;
    l.bars_x = l.bars_y = -1.0f, l.bars_k = 1.0f;
    l.items_x = l.items_y = -1.0f, l.items_k = 1.0f;
    l.chat_x = l.chat_y = -1.0f, l.chat_k = 1.0f;
    for (int k = 0; k < HUD_N; k++) l.hud[k][2] = 1.0f;  // (the parts at their size, shown)
    return l;
}
// The kind and what it hides, for in_mode; the FPS counter's place, for perfstats (touch_fps_place)
float g_fps_x = -1.0f, g_fps_y = -1.0f;
float g_item_zone = 1.0f;  // (Layout.item_zone, for item_zone)
float g_bars_x = -1.0f, g_bars_y = -1.0f, g_bars_k = 1.0f;  // (the monsters' health bars' place: touch_bars_place)
float g_items_x = -1.0f, g_items_y = -1.0f, g_items_k = 1.0f;  // (the quick items' piece's: touch_items_place)
float g_chat_x = -1.0f, g_chat_y = -1.0f, g_chat_k = 1.0f;     // (1.0.2: the chat's button's: touch_chat_place)
void publish(const Layout &l) {
    for (int k = 0; k < COMBOS; k++) g_pub_combo[k] = l.combo[k];
    g_kind = l.kind, g_off = l.off, g_style = l.style, g_fps_x = l.fps_x, g_fps_y = l.fps_y, g_item_zone = l.item_zone;
    g_bars_x = l.bars_x, g_bars_y = l.bars_y, g_bars_k = l.bars_k;
    g_items_x = l.items_x, g_items_y = l.items_y, g_items_k = l.items_k;
    g_chat_x = l.chat_x, g_chat_y = l.chat_y, g_chat_k = l.chat_k;
}
std::string g_layout_path = "touch_layout.txt";  // (the app's folder: touch_init)
std::string g_data_dir = ".";                    // (the app's folder: the profiles', where no save folder is chosen)
const char *const CONTROLS_KEY[3] = {"auto", "on", "off"};

// A control by its key in the file (C_LAID: the laid-out ones); -1 none
int ctl_by_key(const char *key) {
    for (int i = 0; i < C_LAID; i++)
        if (!strcmp(key, DEFS[i].key)) return i;
    return -1;
}
// A file's text, whole (false: it can't be read)
bool read_text(const std::string &path, std::string *out) {
    out->clear();
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[1024];
    for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) out->append(buf, n);
    fclose(f);
    return true;
}

// A layout from its text (touch_layout.txt's, or a profile's: the same lines). Its kind first, whose defaults the lines
// then change. The settings in it (which buttons show, the opacity, the camera's speed, the combined buttons, the
// editor's panel) only where `settings` (touch_layout.txt: a profile's file holds the layout alone, and a copy of an
// older touch_layout.txt that has them leaves the settings as they are)
void parse_layout(const std::string &text, Layout *l, bool settings) {
    int kind = KIND_OURS;
    for (size_t at = text.find("layout_kind "); at != std::string::npos; at = text.find("layout_kind ", at + 1))
        if (at == 0 || text[at - 1] == '\n') kind = std::clamp(atoi(text.c_str() + at + 12), 0, KIND_COUNT - 1);
    *l = defaults(kind);
    if (settings) {
        for (int b = 0; b < TB_COUNT; b++) g_set.show[b] = TB_DEFAULT[b];
        for (int k = 0; k < COMBOS_OLD; k++) g_combo[k] = 0;
        g_epanel = EditPanel{};
    }
    char key[40], word[64];
    float x, y, s;
    for (size_t at = 0, end; at < text.size(); at = end + 1) {
        end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string ls = text.substr(at, end - at);
        if (!ls.empty() && ls.back() == '\r') ls.pop_back();
        const char *line = ls.c_str();
        if (line[0] == '#') continue;
        // (1.0.2: combo <n> <buttons>, "-" none: the layout's own combined buttons; an older version skips the line)
        {
            int k;
            if (sscanf(line, "combo %d %63s", &k, word) == 2 && k >= 1 && k <= COMBOS && !strncmp(line, "combo ", 6)) {
                l->combo[k - 1] = strcmp(word, "-") ? combo_mask(word) : 0, l->has_combos = true;
                continue;
            }
        }
        // (the lines of 1.0's profiles with a control's key as their word: a file read by an older version skips them)
        if (sscanf(line, "%39s %63s", key, word) == 2) {
            int c = ctl_by_key(word);
            if (!strcmp(key, "hidden") && c >= 0) { l->off |= 1ull << c; continue; }
            if (!strcmp(key, "hold") && c >= 0) { l->hold |= 1ull << c; continue; }
            if (c < 0 && !strcmp(word, DEFS[C_MSELECT].key)) c = C_MSELECT;  // (its picture alone: 1.0.2)
            if (!strcmp(key, "image") && c >= 0) {  // (image <key> <file>: the name, to the line's end)
                const char *f = strstr(line, word) + strlen(word);
                while (*f == ' ' || *f == '\t') f++;
                if (*f && !strchr(f, '/') && !strchr(f, '\\')) snprintf(l->image[c], IMAGE_NAME, "%s", f);
                continue;
            }
            if (!strcmp(key, "image_knob")) {  // (image_knob <file>: the stick's knob, 1.0.1; an older version skips it)
                const char *f = line + strlen(key);
                while (*f == ' ' || *f == '\t') f++;
                if (*f && !strchr(f, '/') && !strchr(f, '\\')) snprintf(l->knob, IMAGE_NAME, "%s", f);
                continue;
            }
        }
        int n = sscanf(line, "%39s %f %f %f", key, &x, &y, &s);
        if (n >= 2 && !strcmp(key, "profile")) {
            if (x >= 1 && x <= PROFILES_N) l->profile = (int)x, l->profile_line = true;
            continue;
        }
        if (n >= 3 && !strcmp(key, "fps_counter")) {
            bool ok = x >= 0 && x <= 1 && y >= 0 && y <= 1;
            l->fps_x = ok ? x : -1.0f, l->fps_y = ok ? y : -1.0f;
            continue;
        }
        if (n >= 3 && !strcmp(key, "chat_button")) {  // (1.0.2; an older version skips the line)
            bool ok = x >= 0 && x <= 1 && y >= 0 && y <= 1;
            l->chat_x = ok ? x : -1.0f, l->chat_y = ok ? y : -1.0f;
            l->chat_k = n >= 4 ? std::clamp(s, .6f, 2.0f) : 1.0f;
            continue;
        }
        if (n >= 3 && !strcmp(key, "quick_items")) {  // (1.0.1.1; an older version skips the line)
            bool ok = x >= 0 && x <= 1 && y >= 0 && y <= 1;
            l->items_x = ok ? x : -1.0f, l->items_y = ok ? y : -1.0f;
            l->items_k = n >= 4 ? std::clamp(s, .5f, 2.0f) : 1.0f;
            continue;
        }
        if (n >= 3 && !strcmp(key, "monster_bars")) {  // (1.0.1; an older version skips the line)
            bool ok = x >= 0 && x <= 1 && y >= 0 && y <= 1;
            l->bars_x = ok ? x : -1.0f, l->bars_y = ok ? y : -1.0f;
            l->bars_k = n >= 4 ? std::clamp(s, .5f, 2.0f) : 1.0f;
            continue;
        }
        if (n >= 3 && !strncmp(key, "hud_", 4))
            for (int k = 0; k < HUD_N; k++)
                if (!strcmp(key + 4, HUD_KEY[k])) {
                    l->hud[k][0] = std::clamp(x, -400.0f, 400.0f), l->hud[k][1] = std::clamp(y, -240.0f, 240.0f), l->has_hud = true;
                    float hk = 1.0f, hh = 0;  // (1.0.2: its size and hidden; an older file has neither)
                    if (sscanf(line, "%*s %*f %*f %f %f", &hk, &hh) < 1 || !(hk > 0)) hk = 1.0f;
                    l->hud[k][2] = std::clamp(hk, HUD_K_MIN, HUD_K_MAX), l->hud[k][3] = hh != 0 ? 1.0f : 0.0f;
                }
        if (n == 4) {
            for (int i = 0; i < C_LAID; i++)
                if (!strcmp(key, DEFS[i].key) && x > 0 && x < 4 && y > 0 && y < 1 && s >= .4f && s <= 2.5f)
                    l->x[i] = x, l->y[i] = y, l->scale[i] = s;
            bool usual = x < 0 && y < 0, placed = x >= 0 && x <= 1 && y >= 0 && y <= 1;
            if (settings && !strcmp(key, "editor_panel") && (usual || placed) && s >= EPANEL_MIN && s <= 1.0f)
                g_epanel.x = usual ? -1.0f : x, g_epanel.y = usual ? -1.0f : y, g_epanel.k = s;
        }
        if (n >= 2 && !strcmp(key, "attack_style") && x >= 0 && x < (float)STYLE_COUNT) l->style = (int)x;
        if (n >= 2 && !strcmp(key, "item_zone")) l->item_zone = std::clamp(x, ITEM_ZONE_MIN, ITEM_ZONE_MAX);
        if (!settings) continue;
        if (n >= 2 && !strcmp(key, "opacity")) g_set.opacity = std::clamp(x, .2f, 1.0f);
        if (n >= 2 && !strcmp(key, "camera_speed")) g_set.camera = std::clamp(x, .2f, 5.0f);
        if (n >= 2 && !strcmp(key, "camera_smooth")) g_set.smooth = std::clamp(x, 0.0f, 1.0f);
        if (n >= 2 && !strcmp(key, "debug")) g_set.debug = x != 0;
        if (n >= 2 && !strncmp(key, "show_", 5))
            for (int b = 0; b < TB_COUNT; b++)
                if (!strcmp(key + 5, TB_KEY[b])) g_set.show[b] = x != 0;
        int k;
        if (sscanf(line, "combo_buttons %d %63s", &k, word) == 2 && k >= 1 && k <= COMBOS_OLD) g_combo[k - 1] = combo_mask(word);
        if (sscanf(line, "%39s %63s", key, word) == 2 && !strcmp(key, "controls"))
            for (int m = 0; m < 3; m++)
                if (!strcmp(word, CONTROLS_KEY[m])) g_set.controls = m;
    }
    if (l->kind != KIND_EMU) l->off = 0;  // (the port's own layout hides by the settings' Buttons on the screen)
    if (!l->has_combos)  // (a layout of before 1.0.2: the combined buttons there were, touch_layout.txt's combo_buttons)
        for (int k = 0; k < COMBOS; k++) l->combo[k] = k < COMBOS_OLD ? g_combo[k] : 0;
}
bool load_layout(Layout *l) {
    std::string text;
    bool ok = read_text(g_layout_path, &text);
    parse_layout(ok ? text : std::string(), l, true);
    return ok;
}
bool load_profile_file(const std::string &path, Layout *l) {
    std::string text;
    if (!read_text(path, &text)) return false;
    parse_layout(text, l, false);
    return true;
}

std::string hud_value(const float *v);
// The layout's own lines (a profile's file is these alone, and the HUD's parts where hud is given)
void write_layout_lines(FILE *f, const Layout &l, const float (*hud)[HUD_V]) {
    fprintf(f, "# MH4U's touch controls: name, centre (screen heights from its edge and from the bottom), size\n");
    for (int i = 0; i < C_LAID; i++) fprintf(f, "%s %.4f %.4f %.3f\n", DEFS[i].key, l.x[i], l.y[i], l.scale[i]);
    fprintf(f, "# the attacks: 0 the pill, 1 two buttons, 2 a band round the corner\nattack_style %d\n", l.style);
    fprintf(f, "# the layout's kind (0 the port's own, 1 emulator-style), its profile (1 to %d), the FPS counter's middle (in "
               "the screen's width and height; -1 -1 its own place)\n", PROFILES_N);
    fprintf(f, "layout_kind %d\nprofile %d\nfps_counter %.4f %.4f\n", l.kind, l.profile, l.fps_x, l.fps_y);
    fprintf(f, "# the monsters' health bars: their top right corner (in the top screen's width and height from its left and "
               "top; -1 -1 their own place) and size\nmonster_bars %.4f %.4f %.2f\n", l.bars_x, l.bars_y, l.bars_k);
    fprintf(f, "# the quick items' piece: its middle (in the window's width and height; -1 -1 its own place) and size\n"
               "quick_items %.4f %.4f %.2f\n", l.items_x, l.items_y, l.items_k);
    fprintf(f, "# the chat's button: its middle (in the window's width and height; -1 -1 its own place) and size\n"
               "chat_button %.4f %.4f %.2f\n", l.chat_x, l.chat_y, l.chat_k);
    fprintf(f, "# controls hidden (emulator-style), held by a tap (hold), with the player's own picture (image <key> <file>;\n"
               "# the stick's knob: image_knob <file>), drawn alone in the control's place\n");
    for (int i = 0; i < C_LAID; i++) {
        if (l.kind == KIND_EMU && (l.off >> i & 1)) fprintf(f, "hidden %s\n", DEFS[i].key);
        if (l.hold >> i & 1) fprintf(f, "hold %s\n", DEFS[i].key);
        if (l.image[i][0]) fprintf(f, "image %s %s\n", DEFS[i].key, l.image[i]);
    }
    if (l.knob[0]) fprintf(f, "image_knob %s\n", l.knob);
    if (l.image[C_MSELECT][0]) fprintf(f, "image %s %s\n", DEFS[C_MSELECT].key, l.image[C_MSELECT]);
    fprintf(f, "# the item bar's touch area (a tap uses the item, a swipe turns the bar), times its own size\nitem_zone %.2f\n", l.item_zone);
    fprintf(f, "# the combined buttons: the game's buttons each presses together (- none)\n");
    for (int k = 0; k < COMBOS; k++) fprintf(f, "combo %d %s\n", k + 1, l.combo[k] ? combo_text(l.combo[k]).c_str() : "-");
    if (hud) {
        fprintf(f, "# the game's HUD parts: where the editor put them (the top screen's pixels from the game's place)\n");
        for (int k = 0; k < HUD_N; k++) fprintf(f, "hud_%s %s\n", HUD_KEY[k], hud_value(hud[k]).c_str());
    }
}
void save_layout(const Layout &l) {
    FILE *f = fopen(g_layout_path.c_str(), "w");
    if (!f) { fprintf(stderr, "[touch] could not save %s\n", g_layout_path.c_str()); return; }
    write_layout_lines(f, l, nullptr);
    fprintf(f, "# shown while no controller is connected (auto), always (on) or never (off: the settings button alone)\n");
    fprintf(f, "controls %s\nopacity %.2f\ncamera_speed %.2f\n", CONTROLS_KEY[g_set.controls], g_set.opacity, g_set.camera);
    fprintf(f, "# the camera's drag smoothed (0 off, 1 the most)\ncamera_smooth %.2f\n", g_set.smooth);
    fprintf(f, "# which of the game's buttons show (1) or not (0)\n");
    for (int b = 0; b < TB_COUNT; b++) fprintf(f, "show_%s %d\n", TB_KEY[b], g_set.show[b] ? 1 : 0);
    fprintf(f, "# the combined buttons of before 1.0.2 (a profile without its own takes them; 1.0.2 keeps each layout's above)\n");
    for (int k = 0; k < COMBOS_OLD; k++)
        if (g_combo[k]) fprintf(f, "combo_buttons %d %s\n", k + 1, combo_text(g_combo[k]).c_str());
    char line[80];
    epanel_line(line, g_epanel);
    fprintf(f, "%s%s", EPANEL_NOTE, line);
    if (g_set.debug) fprintf(f, "debug 1\n");
    fclose(f);
    fprintf(stderr, "[touch] layout saved (%s, profile %d)\n", g_layout_path.c_str(), l.profile);
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

// ---- the layout profiles: ten numbered slots, each a whole layout of either kind (the port's own or the emulator-style
// one: the controls, the game's HUD parts, the FPS counter's place, the buttons held by a tap, the pictures), after
// tabarong's design for MHGU (Control and HUD layout presets: numbered slots in the editor, kept in the save folder, an
// overwrite asked for twice). The MHGU port's runtime/touch.cpp the same: names, files, texts.
// Kept as profile_01.txt .. profile_10.txt (touch_layout.txt's layout lines, and the HUD's parts: hud_<key> x y) in the
// "layout profiles" folder of the save folder the player chose (Settings -> Save folder: they go with the saves to the
// player's other devices and stay when the app is uninstalled), else of the app's folder; the players' pictures in its
// images folder. touch_layout.txt stays the layout in use, with the profile it is ("profile <n>"), and mh4u.ini its HUD;
// the editor's Save writes them and the profile. A touch_layout.txt from before the profiles (no profile line) is copied
// into profile 1 as it is, byte for byte, the HUD's parts after it, the first time
const char *const PROFILES_FOLDER = "layout profiles";
// The profiles' folder; chosen: it is the save folder's (false: the app's)
std::string profiles_dir(bool *chosen = nullptr) {
    std::string t, p;
    if (read_text(g_data_dir + "/save_folder.txt", &t)) {
        p = t.substr(0, t.find_first_of("\r\n"));
        while (!p.empty() && (p.back() == ' ' || p.back() == '/')) p.pop_back();
    }
    if (chosen) *chosen = !p.empty();
    return (p.empty() ? g_data_dir : p) + "/" + PROFILES_FOLDER;
}
std::string profile_path(int n) {
    char name[32];
    snprintf(name, sizeof name, "/profile_%02d.txt", n);
    return profiles_dir() + name;
}
std::string images_dir() { return profiles_dir() + "/images"; }
bool is_file(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
bool is_folder(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
bool make_folder(const std::string &p) { return is_folder(p) || mkdir(p.c_str(), 0775) == 0 || is_folder(p); }
bool write_text(const std::string &path, const std::string &text) {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = fclose(f) == 0 && ok;
    return ok;
}
bool copy_file(const std::string &from, const std::string &to) {
    std::string t;
    return read_text(from, &t) && write_text(to, t);
}
// What a profile holds: nothing (false), else its kind
bool profile_info(int n, int *kind) {
    Layout l;
    if (!load_profile_file(profile_path(n), &l)) return false;
    *kind = l.kind;
    return true;
}
// The game's HUD parts where mh4u.ini has them now (the layout in use's)
// A part's value in mh4u.ini ("dx dy", or "dx dy size hidden"): into v[HUD_V]
void hud_parse(const std::string &text, float *v) {
    v[0] = v[1] = v[3] = 0, v[2] = 1.0f;
    float k = 1.0f, hid = 0;
    int n = sscanf(text.c_str(), "%f %f %f %f", &v[0], &v[1], &k, &hid);
    v[2] = n >= 3 && k > 0 ? std::clamp(k, HUD_K_MIN, HUD_K_MAX) : 1.0f;
    v[3] = n >= 4 && hid != 0 ? 1.0f : 0.0f;
}
void hud_now(float hud[][HUD_V]) {
    for (int k = 0; k < HUD_N; k++)
        hud_parse(game_option_get(std::string("hud_") + HUD_KEY[k], k == HP_ITEMS ? game_option_get("item_bar", "0 0") : "0 0"), hud[k]);
}
// A profile's file: the layout's lines and the HUD's parts. false: it couldn't be written
bool profile_write(int n, const Layout &l, const float (*hud)[HUD_V]) {
    if (!make_folder(profiles_dir())) return false;
    std::string path = profile_path(n);
    FILE *f = fopen(path.c_str(), "w");
    if (!f) return false;
    Layout c = l;
    c.profile = n;
    write_layout_lines(f, c, hud);
    bool ok = !ferror(f);
    ok = fclose(f) == 0 && ok;
    if (!ok) fprintf(stderr, "[touch] profiles: %s can't be written\n", path.c_str());
    return ok;
}
// (1.0.2) Before another profile takes the place of the one in use: one that has no file yet (a fresh install's profile
// 1, played with but never saved in a slot; a layout of before the profiles that couldn't be copied) is written now, so
// that coming back to it finds it as it was, not empty (and then New's defaults)
void keep_in_use(const Layout &l, const float (*hud)[HUD_V]) {
    if (is_file(profile_path(l.profile))) return;
    if (profile_write(l.profile, l, hud))
        fprintf(stderr, "[touch] profiles: profile %d had no file: the layout in use written into it before leaving it\n", l.profile);
}
// Two layouts the same, as the files would have them (the profile they are and their HUD parts left out)
bool same_layout(const Layout &a, const Layout &b) {
    for (int i = 0; i < C_COUNT; i++)
        if (a.x[i] != b.x[i] || a.y[i] != b.y[i] || a.scale[i] != b.scale[i] || strcmp(a.image[i], b.image[i])) return false;
    return a.style == b.style && a.kind == b.kind && a.off == b.off && a.hold == b.hold && a.fps_x == b.fps_x &&
           a.fps_y == b.fps_y && a.bars_x == b.bars_x && a.bars_y == b.bars_y && a.bars_k == b.bars_k &&
           a.items_x == b.items_x && a.items_y == b.items_y && a.items_k == b.items_k && a.chat_x == b.chat_x &&
           a.chat_y == b.chat_y && a.chat_k == b.chat_k && !strcmp(a.knob, b.knob) &&
           a.item_zone == b.item_zone && !memcmp(a.combo, b.combo, sizeof a.combo);
}
// (the start) A save folder chosen after the profiles were made in the app's folder: they go with it, the first time
// (copied: the app's stay as they were)
void profiles_adopt() {
    bool chosen;
    std::string dir = profiles_dir(&chosen), mine = g_data_dir + "/" + PROFILES_FOLDER;
    if (!chosen || is_folder(dir) || !is_folder(mine)) return;
    if (!make_folder(dir)) { fprintf(stderr, "[touch] profiles: the save folder's %s can't be made\n", dir.c_str()); return; }
    int n = 0;
    for (int i = 1; i <= PROFILES_N; i++) {
        char name[32];
        snprintf(name, sizeof name, "/profile_%02d.txt", i);
        if (is_file(mine + name)) n += copy_file(mine + name, dir + name);
    }
    if (DIR *d = opendir((mine + "/images").c_str())) {
        make_folder(dir + "/images");
        while (dirent *e = readdir(d))
            if (e->d_name[0] != '.') copy_file(mine + "/images/" + e->d_name, dir + "/images/" + e->d_name);
        closedir(d);
    }
    fprintf(stderr, "[touch] profiles: %d copied from the app's folder into the save folder's\n", n);
}
// (the start) A touch_layout.txt made before the profiles (no profile line): copied as it is into profile 1 (the HUD's
// parts from mh4u.ini after it) - into the first empty one where profile 1 holds another layout already (a save folder
// another device shares) -, and the profile line added at its end, the rest of it untouched. Not reachable now: the next
// start tries again
void profiles_migrate(Layout &l) {
    if (l.profile_line) return;
    std::string dir = profiles_dir(), text, other;
    if (!read_text(g_layout_path, &text)) return;
    if (!make_folder(dir)) { fprintf(stderr, "[touch] profiles: %s can't be made: the layout stays as it is for now\n", dir.c_str()); return; }
    float hud[HUD_N][HUD_V];
    hud_now(hud);
    std::string copy = text;
    if (!copy.empty() && copy.back() != '\n') copy += '\n';
    copy += "# the game's HUD parts: where the editor put them (the top screen's pixels from the game's place)\n";
    for (int k = 0; k < HUD_N; k++) copy += std::string("hud_") + HUD_KEY[k] + " " + hud_value(hud[k]) + "\n";
    int n = 1;
    if (read_text(profile_path(1), &other) && other != copy) {
        n = 0;
        for (int i = 2; i <= PROFILES_N && !n; i++)
            if (!is_file(profile_path(i))) n = i;
        if (!n) { fprintf(stderr, "[touch] profiles: all ten hold a layout: the one in use stays out of them\n"); return; }
    }
    if (!is_file(profile_path(n)) && !write_text(profile_path(n), copy)) {
        fprintf(stderr, "[touch] profiles: %s can't be written\n", profile_path(n).c_str());
        return;
    }
    char line[32];
    snprintf(line, sizeof line, "%sprofile %d\n", !text.empty() && text.back() != '\n' ? "\n" : "", n);
    if (FILE *f = fopen(g_layout_path.c_str(), "ab")) fputs(line, f), fclose(f);
    l.profile = n, l.profile_line = true;
    fprintf(stderr, "[touch] profiles: the layout in use copied as it is into profile %d (%s)\n", n, dir.c_str());
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
// (1.0.2) The settings button never moves by itself: it stays at its place, or the one the player gave it in the editor,
// whatever is open (a menu of the game's, the touch screen's corner). Up to the first 1.0.2 builds it went to the
// screen's corner with the menus on the top screen, and with its side's controls out of the corner's way, and sat on
// other buttons there (the owner's Galaxy)
Geo place(const Layout &l, int i, float w, float h) {
    Geo g = place_raw(l, i, w, h);
    if (i == C_SETTINGS) return g;
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
// The game's touch-screen menus are drawn on the top screen (single-screen mode, or a code mod's single-screen: ui.h's
// ui_set_one_screen): while one is open the controls over them are drawn faint (menus_area)
bool g_one_screen;

void set_screens(const UiScreens &sc) {
    g_top = sc.top;
    g_bottom = {sc.bottom.x, sc.bottom.y, sc.bottom.x + sc.bottom.w, sc.bottom.y + sc.bottom.h,
                sc.bottom_shown && sc.bottom.w > 0 && sc.bottom.h > 0};
    if (g_bottom.ok) g_bottom_seen = g_bottom;
}

// A control under the touch screen's corner while it shows: not drawn, and a finger there is the game's
bool covered(const Layout &l, int i, float w, float h) {
    if (!g_bottom.ok || i == C_SETTINGS) return false;  // (1.0.2: the settings button stays, over the corner if it is there)
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
    } else if (i == C_MENU || i == C_HSELECT || i == C_MSELECT || i == C_ESTART || i == C_ESELECT) {  // (their plates below them: badge())
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
// its place, so it goes as the pad comes in and comes back as it goes (4u-native #25; the same in the MHGU port); with
// show_dpad_menus it stays, wherever it is, and presses the d-pad there (the MHGU port's DpadInMenus)
bool under_mpad(const Layout &l, int i, float w, float h) {
    if (of_mpad(i) || i == C_SETTINGS || i == C_ATTACK || i == C_CORNER || i == C_HEAVY || i == C_LIGHT || i == C_DODGE ||
        !in_mode(C_MPAD, true))
        return false;
    if (i == C_HDPAD)  // (the pad under the touch screen shows nothing: the d-pad stays)
        return !show(TB_DPAD_MENUS) && !covered(l, C_MPAD, w, h);
    Rect b = bounds(l, i, w, h);
    for (int m : {C_ML, C_MR, C_MSELECT}) {
        if (!mpad_member(m)) continue;
        Rect t = bounds(l, m, w, h);
        if (b.x1 > t.x0 && b.x0 < t.x1 && b.y1 > t.y0 && b.y0 < t.y1) return true;
    }
    Geo p = place(l, C_MPAD, w, h);
    return b.near(p.c, p.r);
}

// The item bar's touch area round its art (x0 y0 x1 y1, window pixels): the art and a margin of 15 % of its height,
// times the layout's item_zone about its middle (the player's, in the editor: 6O7ET's "the potion selection space")
Rect zone_round(float x0, float y0, float x1, float y1, float k) {
    float m = (y1 - y0) * .15f, hw = ((x1 - x0) * .5f + m) * k, hh = ((y1 - y0) * .5f + m) * k;
    float cx = (x0 + x1) * .5f, cy = (y0 + y1) * .5f;
    return {cx - hw, cy - hh, cx + hw, cy + hh, true};
}
// The item bar: where android_main.cpp says it is, or (not known) its usual place, the top screen's bottom-right
// corner (MH4U's HUD, with the port's hud_edges at the picture's edge); a little larger than its art, for a thumb, and
// as large as the player made it (item_zone). core: no larger than up to 1.0 (where it takes a finger before Guard or
// Evade, on a narrow screen: the part the player added round it is theirs)
Rect item_zone(bool menu, bool core = false) {
    const float k = core ? std::min(1.0f, g_item_zone) : g_item_zone;
    Rect r = {0, 0, 0, 0, false};
    if (menu || g_game.item_bar_shown == 0) return r;
    if (g_game.item_bar_shown == 1) {
        const SDL_FRect &b = g_game.item_bar;
        if (b.w <= 0 || b.h <= 0) return r;
        r = zone_round(b.x, b.y, b.x + b.w, b.y + b.h, k);
    } else {
        if (g_top.w <= 0 || g_top.h <= 0) return r;
        float u = g_top.h, x1 = g_top.x + g_top.w - .008f * u, y1 = g_top.y + g_top.h - .008f * u;
        r = {x1 - .35f * u, y1 - .17f * u, x1, y1, true};
        if (k != 1.0f) {  // (its usual place's area: the same way, about its middle)
            float cx = (r.x0 + r.x1) * .5f, cy = (r.y0 + r.y1) * .5f, hw = (r.x1 - r.x0) * .5f * k, hh = (r.y1 - r.y0) * .5f * k;
            r = {cx - hw, cy - hh, cx + hw, cy + hh, true};
        }
    }
    if (g_bottom.ok && r.x1 > g_bottom.x0 && r.x0 < g_bottom.x1 && r.y1 > g_bottom.y0 && r.y0 < g_bottom.y1)
        r.ok = false;  // (under the touch screen's corner: that is the game's)
    return r;
}

// The target camera's icon on the top screen (the single-screen mod puts it there, Port::Hooks' "target" part), a little
// larger than its art: where the lock-on's gold ring goes round it (target_hint). It takes no finger: a tap or a long
// press on it turns no lock-on (the Lock-on button and a controller's right stick click do), a drag there turns the camera
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
enum Kind { F_NONE, F_BUTTON, F_STICK, F_CAMERA, F_MENU, F_ITEM, F_EDIT, F_CHAT };  // (F_CHAT: the chat button's)
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
    uint64_t keep;         // a thumb from Guard slid onto the attacks (show_guard_slide): R kept with them; a finger
                           // slid from a button onto another (show_slide_both): what the first pressed
    bool out0;             // the weapon was out as it landed (the aim's: a tap that drew the weapon holds nothing open)
    int from_group;        // the slides' group of the button it landed on (slide_group; -1 none: no slide keeps it)
    int kept_ctl, kept_part;  // the button whose press it keeps (show_slide_both: lit as pressed; -1 none)
    uint64_t join_ns;      // (1.0.2) a thumb slid from R onto the attacks soon after landing on R: the attack's button
                           // joins R then, R alone till then (slide_lead); 0 none waiting
    bool multi;            // another finger was on the screen while it was (no long press of it hides the controls)
    bool mouse;            // a mouse's click (SDL's finger of it: no long press of it hides the controls)
    bool cam_off;          // (1.0.2, show_camera_right_half) landed on the screen's left half, where no control is: its
                           // drag turns no camera (a missed touch by the stick); a menu's cursor and a long press as before
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
// The layout editor's panel, in two rows as the MHGU port's: what is picked (smaller, bigger, hide or show - a control of
// the emulator-style layout -, back to its default, held by a tap, a picture of the player's) and the rest (the profile,
// the melee or ranged layout, the attacks' style, defaults, cancel, save)
enum { E_SMALLER, E_BIGGER, E_HIDE, E_RESET, E_HOLD, E_IMAGE, E_PROFILES, E_SET, E_STYLE, E_DEFAULTS, E_CANCEL, E_SAVE, E_COUNT };
const int E_ROW2 = E_PROFILES;  // (the second row's first)
const int IMG_MAX = 60;         // (the pictures the editor's window lists)
struct ImgName { char n[IMAGE_NAME]; };
struct State {
    Finger f[10];
    uint64_t aim_latch;  // a tap on the aim: its button held for the game until the next tap (it aims only while held)
    // the editor: each part of the game's HUD where the player puts it (Port::Hooks' hud_<key>, the top screen's
    // pixels; the game shows it there while it is dragged), and where it was when the editor opened (Cancel)
    float hud_at[UiGameState::HUD_PARTS][HUD_V], hud_open[UiGameState::HUD_PARTS][HUD_V];  // (1.0.2: and size, hidden)
    float hud_drag_rest[2];  // (a drag's fraction of a pixel not given to the game yet)
    bool bow_aim, bow_x;     // (1.0.2) a bow's aiming mode with Bow Controls Type 2, as followed here; X held last frame
    uint64_t bow_aim_ns;     // (when X last turned it)
    bool stick_on;
    float bx, by, kx, ky;    // the stick's base and knob, pixels
    float cam_dx, cam_dy;    // pixels dragged since the last frame
    float cam_rest_x, cam_rest_y;  // (camera_smooth: pixels dragged and not given to the camera yet)
    uint64_t held;           // buttons held by fingers
    uint64_t last_ns;        // the last touch
    uint64_t step_ns;        // when a drag may move a menu's cursor again
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
    uint64_t run_t0;         // when the run's R began (held since: a sprint going on, which X + A doesn't stop)
    uint64_t run_l_frame, run_l_ns;  // the game's frame and the time an L of the touch controls was last pressed
    float fx, fy;            // the stick's thumb where it is (the knob stops at the rim)
    uint64_t item_flash;
    int item_dir;            // the bar's last turn (-1, 1), 0 the item used
    uint64_t use_until, use_at;  // the item to use once the weapon is away (item_use): until when, and when it went
    int use_action;              // away, with the hunter's action then (putting the weapon away)
    SDL_FingerID bar_finger;     // the finger whose long press holds the bar open
    bool bar_open;           // a long press holds the bar open (Port::Hooks::HoldItemBarOpen)
    bool bar_held;           // the hooks hold it open now (the long press: frame_input)
    int stick_part;          // in a menu: the direction the stick is pressing (dpad_part)
    uint64_t menu_off_ns;    // when the last menu closed (the menu pad goes: MPAD_HOLD, MPAD_OUT)
    int lit_ctl = -1, lit_part;  // a quick tap of the menu pad's group pressing now (lit until lit_until)
    uint64_t lit_until;
    Seq item, ammo, cursor;  // the item bar's steps, the ammo's, a menu cursor's
    float cs_x, cs_y;        // the C-stick's push for a drag (where the camera hook isn't there), -1..1
    uint64_t cs_until, frame_ns;
    float w, h;              // the screen, pixels
    Layout layout, saved;
    // (the layout editor: the layout as it came into it last - opened, a profile loaded, saved here or New - and its HUD
    // parts then: what differs from it is not saved anywhere yet, prof_action asks before leaving it)
    Layout base;
    float base_hud[UiGameState::HUD_PARTS][HUD_V];
    bool loaded;
    bool menu;               // the menu layout is shown (at the last frame)
    bool visible, gear_only;
    bool pad_hidden;         // (1.0.2, T10) the controls hidden by a controller (show the touch controls: Auto)...
    int pad_taps;            // ...the taps on the game since then, a few seconds apart at most, and the notice said once
    uint64_t pad_tap_ns;
    bool pad_said;
    bool user_hidden;        // the player hid every control with a long press on an empty spot (long_press_hide)
    uint64_t hide_note_until;  // its line at the top until then (the first time ever, a longer one: hide_note_first)
    bool hide_note_first;
    bool edit;               // the layout editor
    int sel;
    bool pinch;
    float pinch_d0, pinch_s0;
    EditPanel epanel_open;   // (g_epanel as the editor opened)
    int rep_btn;             // the panel's - or + that sized it last (-1 none), when, and where it was then
    uint64_t rep_ns;
    Rect rep_at;
    // the buttons held by a tap (Layout.hold): those down now, a bit each, and what each presses (as its tap had it)
    uint64_t latched;
    uint64_t latch_btn[C_COUNT];
    int open_slot;           // the profile in use as the editor opened (Layout.profile: the one it edits)
    // the profiles' window over the editor (its Profile button): the slot picked, an action asked to be tapped again
    // (PA_*, -1 none), the window's button held down (lit), what each slot holds, the line that says what happened
    bool prof_open;
    int prof_pick, prof_arm, prof_btn;
    bool prof_used[PROFILES_N];
    int prof_kind[PROFILES_N];
    char prof_say[200];
    // the pictures' window (its Image button): the files of the images folder, the page shown, its button held down
    bool img_open, img_knob;  // (img_knob: the stick's window picks its knob's picture, not its base's)
    bool img_select;          // (the menu pad's window picks SELECT's picture, not the pad's: 1.0.2)
    bool sel_knob, sel_select;  // (the editor's pick was a tap on the stick's knob, on the menu pad's SELECT: 1.0.2)
    int img_n, img_page, img_btn;
    ImgName img_names[IMG_MAX];
    uint64_t img_scan_ns;
    char note[200];          // a line under the editor's panel until note_until (a profile's action)
    uint64_t note_until;
};
State g_s;

// The game's touch-screen menus on the top screen (g_one_screen) while one is open (a menu, the title, the choice of a
// save): where the game draws them, its 400 x 240 screen at its own shape in the middle of the picture (a picture wider
// than 5:3 shows more of the scene at its sides, never more of a menu; on a narrower one, 4:3, they take it all). A
// control over them is drawn faint there (MENU_GHOST of its menu look: the menu reads under it, the item box's last
// column on a 20:9 screen under the right thumb's buttons) and works as ever, a finger on it lights it as usual: those
// menus take no tap of their own, they are used with these buttons. Nothing moves (a layout the player saved stays as
// it is); never in the editor, nor the settings button (drawn as it is: the way to the settings)
const float MENU_GHOST = .35f;
Rect menus_area() {
    if (!g_one_screen || g_s.edit || (g_game.menu != 1 && g_game.front_end != 1) || g_top.w <= 0 || g_top.h <= 0)
        return {0, 0, 0, 0, false};
    const float half = std::min(g_top.w, g_top.h * 400.0f / 240.0f) * .5f, cx = g_top.x + g_top.w * .5f;
    return {cx - half, g_top.y, cx + half, g_top.y + g_top.h, true};
}
// over them: half of its art's width or more inside (the menu pad by its own disc, not its tabs and SELECT, which are
// controls of their own here), unless a finger is on it or it is held down (latched): then it shows as it is pressed
bool over_menus(const Rect &m, const State &s, int i, float w, float h) {
    if (!m.ok || i == C_SETTINGS || (s.latched >> i & 1) || (i == C_STICK && (s.stick_on || s.run_free))) return false;
    for (const Finger &f : s.f)
        if (f.kind == F_BUTTON && !f.done && f.ctl == i) return false;
    Rect b = bounds(s.layout, i, w, h);
    if (i == C_MPAD) {
        Geo g = place(s.layout, i, w, h);
        b = {g.c.x - g.r, g.c.y - g.r, g.c.x + g.r, g.c.y + g.r, true};
    }
    const float in = std::min(b.x1, m.x1) - std::max(b.x0, m.x0);
    return b.y1 > m.y0 && b.y0 < m.y1 && in > 0 && in * 2 >= b.x1 - b.x0;
}
// ---- the touch controls hidden by the player (Naym's ask, the owner's call for 1.0.1: a long press on an empty part of
// the screen hides them all - the settings button too, for a cutscene or a picture -, another one shows them again).
// A long press is a finger that lands where no control is (the camera's ground: not on a button, the stick, the item
// bar, the chat button nor the touch screen's corner, which is the game's), stays within a few pixels (a drag turns the
// camera: it cancels) and is the only one on the screen the whole time (a thumb on the stick, a second finger: no), for
// HIDE_HOLD; a ring fills round it from HIDE_RING on. While the controls show, never over the game's menus that our
// single-screen mode puts on the top screen (menus_area: they look as if they took a tap, and a player may press on
// them), nor a mouse's click. Hidden, a drag still turns the camera, nothing else is pressed. [Controls] LongPressHide
// in mh4u-app.ini (Settings > Controls > Touch controls), on by default; the first time ever a line says how to show
// them again and where to turn it off ([Controls] HideHintSeen)
const uint64_t HIDE_HOLD = 750 * MS, HIDE_RING = 250 * MS;
bool long_press_hide() {
    static unsigned version = ~0u;
    static bool on = true;
    if (app_settings_version() != version) version = app_settings_version(), on = app_setting_bool("Controls", "LongPressHide", true);
    return on;
}
// The finger whose long press is under way (its index in s.f, -1 none) and how far it has got (0 .. 1)
int hide_finger(const State &s, uint64_t now, float *t) {
    if (s.edit || !long_press_hide()) return -1;
    for (int k = 0; k < 10; k++) {
        const Finger &f = s.f[k];
        if (f.kind != F_CAMERA || f.done || f.moved || f.multi || f.mouse) continue;
        if (!s.user_hidden && menus_area().in(f.x0, f.y0)) continue;
        *t = std::min(1.0f, (now - f.t0) / (float)HIDE_HOLD);
        return k;
    }
    return -1;
}
SDL_Renderer *g_renderer;
bool g_bottom_touch = true;  // the touch screen's corner wanted while the touch controls show (MH4U's main menu is on it;
bool g_bottom_pad = true;    // the controls under it move aside, update_shift), and while they are hidden
// The steps' times. The item bar: L a moment alone, then Y or A. The ammo: the game takes the press only a while after L
// (the MHGU port measured 220 to 310 ms there), and L let go confirms it. The cursor: a press, and a gap the game sees
void seq_setup(State &s) {
    s.item.hold = BTN_L, s.item.lead = 100 * MS, s.item.press = 80 * MS, s.item.gap = 80 * MS, s.item.tail = 100 * MS;
    s.ammo.hold = BTN_L, s.ammo.lead = 380 * MS, s.ammo.press = 80 * MS, s.ammo.gap = 90 * MS, s.ammo.tail = 150 * MS;
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

std::string hud_value(const float *v);
void ensure_loaded(State &s) {
    if (s.loaded) return;
    s.loaded = true, s.sel = -1;
    seq_setup(s);
    profiles_adopt();
    bool had = load_layout(&s.layout);
    std::string from = had ? g_layout_path : "the defaults";
    if (had) profiles_migrate(s.layout);  // (made before the profiles: into profile 1, as it is)
    // (none in use here, and a profile 1 in the save folder: the app installed again, or another device's; played with,
    // its HUD's parts too)
    else if (load_profile_file(profile_path(1), &s.layout)) {
        s.layout.profile = 1, from = "profile 1 (the profiles' folder)";
        for (int k = 0; k < HUD_N && s.layout.has_hud; k++)
            game_option_set(std::string("hud_") + HUD_KEY[k], hud_value(s.layout.hud[k]));
    }
    run_clear_of_band(s.layout);
    s.saved = s.layout;
    publish(s.layout);
    fprintf(stderr, "[touch] layout: %s, profile %d, %s\n", from.c_str(), s.layout.profile,
            s.layout.kind == KIND_EMU ? "emulator-style" : "the port's own");
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
// A step of the ammo or the coating: X the next one, B the one before, with L held (the game's own: L let go confirms
// it). Always with L: the bar held open by the hooks instead (Port::Hooks::HoldItemBarOpen) moved the game's choice and
// closed without confirming it, so the ammo went back to the one before (seen on a Moto g06 with a light bowgun in a
// quest, 0.1.9.3); L held with X or B by a player's two fingers keeps it, and doesn't turn the camera
void ammo_step(State &s, int dir) {
    uint64_t now = SDL_GetTicksNS();
    seq_add(s.ammo, dir > 0 ? BTN_X : BTN_B, now);
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
// takes it, not on the item bar. The floating stick (show_floating_stick, off by default): a thumb that lands near it,
// within FLOAT_REACH of its place (a circle round it, never the whole left half of the screen: the owner's call), brings
// the stick there, its base under the thumb; lifted, it goes back to its place. The rim's run and the sprint lock's
// circle go with it (lock_geo). The MHGU port's the same
const float FLOAT_REACH = 2.6f, FLOAT_MIN = .3f;  // (the stick's radii from its place, and at least so many screen heights)
bool floating_stick() { return show(TB_FLOATSTICK); }
float stick_reach(const Geo &g, float h) {
    return floating_stick() ? std::max(FLOAT_REACH * g.r, FLOAT_MIN * h) : std::max(1.45f * g.r, .17f * h);
}
bool in_stick_zone(const State &s, float x, float y, const Rect &item) {
    Geo g = place(s.layout, C_STICK, s.w, s.h);
    if (item.in(x, y)) return false;
    return hypotf(x - g.c.x, y - g.c.y) < stick_reach(g, s.h);
}
// The sprint lock's circle where it is now: placed in the editor, where it was put; else above the stick - the floating
// stick's while it is held or its run goes on, wherever it came
Geo lock_geo(const Layout &l, const State &s, float w, float h) {
    Geo lg = place(l, C_RUNLOCK, w, h);
    const Def &d = DEFS[C_RUNLOCK];
    bool placed = fabsf(l.x[C_RUNLOCK] - d.x) >= 1e-4f || fabsf(l.y[C_RUNLOCK] - d.y) >= 1e-4f;
    if (floating_stick() && !placed && (s.stick_on || s.run_lock) && !s.edit)
        lg.c = lock_spot(s.bx, s.by, place(l, C_STICK, w, h).r, h);
    return lg;
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
    const Geo lg = lock_geo(s.layout, s, s.w, s.h);  // (where it is: above the stick, or where the editor put it)
    float r = place(s.layout, C_STICK, s.w, s.h).r;
    return hypotf(x - s.bx, y - s.by) > r && hypotf(x - lg.c.x, y - lg.c.y) < LOCK_GRAB / LOCK_R * lg.r;
}
// A thumb from Guard keeps R as it slides (show_guard_slide): with the weapon out, no menu of the game's open
bool guard_slide(const State &s) { return show(TB_GUARDSLIDE) && g_game.weapon_drawn == 1 && !s.menu; }
// Sliding a finger from one button onto another keeps both pressed (show_slide_both, on by default: the owner's call for
// 1.0.1, as two thumbs would; the Insect Glaive's R slid onto A or X, Neroovv's R onto B): the button it leaves stays
// pressed with the one it is on, and back on the first, that alone. The buttons go in groups: the attacks are one (the
// pill's halves, the two attack buttons, the band's attacks: inside it a slide goes from one to the other as before, its
// seam pressing both), the guard another (R, and the band's guard); each face or shoulder button, combined button and
// emulator-style button its own. Not START, SELECT, the d-pads, the menu pad, the stick, the item bar's arrows, the Run
// button (slid onto the attacks it lets R go, as before), a bow's or a bowgun's fire and aim (a drag on them aims) nor
// the port's own buttons. Never in a menu of the game's: there L and R turn its pages, and the slides are as before.
// Off, the slides are as up to 1.0 (show_guard_slide keeps R from Guard alone)
int slide_group(int c, int part) {
    if (c == C_ATTACK || c == C_HEAVY || c == C_LIGHT) return C_ATTACK;
    if (c == C_CORNER) return part == 3 ? C_R : C_ATTACK;
    if (c == C_R || c == C_DODGE || c == C_ZR || c == C_ZL || c == C_L || c == C_SHEATHE || c == C_RSECOND || c == C_RSPECIAL ||
        c == C_RDODGE || c == C_RSHEATHE || is_combo(c) || (c >= C_EA && c <= C_EZR))
        return c;
    return -1;
}
bool slide_both(const State &s) { return show(TB_SLIDEBOTH) && !s.menu; }

Finger *finger(State &s, SDL_FingerID id) {
    for (Finger &f : s.f)
        if (f.kind != F_NONE && f.id == id) return &f;
    return nullptr;
}

uint64_t finger_buttons(const Finger &f) {
    if (f.join_ns) return f.keep;  // (slid from R onto the attacks: R alone until the attack joins, slide_lead)
    uint64_t b = f.ctl == C_ATTACK || f.ctl == C_CORNER ? PILL_BUTTONS[f.part]
               : is_dpad(f.ctl) || f.ctl == C_MPAD ? dpad_buttons(f.part)
               : f.ctl == C_LIGHT && f.part ? PILL_BUTTONS[2]
               : is_combo(f.ctl) ? g_pub_combo[f.ctl - C_COMBO]
               : f.btn ? f.btn
                       : DEFS[f.ctl].buttons;
    return b | f.keep;  // (a guard kept as it slid: show_guard_slide)
}

void recompute_held(State &s) {
    s.held = 0;
    for (Finger &f : s.f)
        if (f.kind == F_BUTTON && !f.done) s.held |= finger_buttons(f);
    s.held |= s.aim_latch;  // (the aim held open by a tap)
    for (int i = 0; i < C_COUNT; i++)  // (the buttons a tap holds down, Layout.hold)
        if (s.latched >> i & 1) s.held |= s.latch_btn[i];
}
// (1.0.2, 4u T24) A thumb that lands on R and slides onto the attacks with the weapon out keeps R, and the attack's
// button joins it only once R has been held SLIDE_LEAD and SLIDE_LEAD_FRAMES of the game's frames: the Insect Glaive's
// kinsect (R + X, R + A) is fired from its aim, which R raises over a few frames, and an X that came sooner was no shot
// (Bonk's video, a Realme 6i at 30 FPS: R slid onto X 100 and 150 ms after it landed did nothing, so the thumb had to
// go back onto R and onto X again; 450 ms after, it fired). A slide made later joins at once, as before; the finger
// lifted while it waits gives both at once, as before
const uint64_t SLIDE_LEAD = 300 * MS;
const uint64_t SLIDE_LEAD_FRAMES = 8;
uint64_t slide_lead(const Finger &f) {
    const uint64_t now = SDL_GetTicksNS();
    if (now - f.t0 >= SLIDE_LEAD && (!g_frames_fed || frames_ok(f.f0 + SLIDE_LEAD_FRAMES))) return 0;
    return std::max(now + MS, f.t0 + SLIDE_LEAD);
}
bool r_onto_attacks(int from, int from_part, int to) {
    const bool from_r = from == C_R || (from == C_CORNER && from_part == 3);
    return from_r && slide_group(to, 0) == C_ATTACK && g_game.weapon_drawn == 1;
}
// A finger slid to (x, y) with show_slide_both (slide_group): onto a button of another group, what it pressed as it
// left the one it landed on stays pressed with that one; back onto the first, that alone; along a group, from one of its
// parts to the next, as before
void slide_to(State &s, Finger &f, float x, float y) {
    int part = 0, c = hit(s.layout, x, y, s.w, s.h, &part, 1.22f, false, s.menu);
    int to = c >= 0 ? slide_group(c, part) : -1;
    if (to < 0 || (c == f.ctl && part == f.part)) return;  // (nothing to slide onto, or where it is)
    if (to == f.from_group) {
        f.keep = 0, f.kept_ctl = -1, f.join_ns = 0;
    } else if (slide_group(f.ctl, f.part) == f.from_group) {  // (leaving the first: its press kept)
        f.keep = finger_buttons(f), f.kept_ctl = f.ctl, f.kept_part = f.part;
        f.join_ns = r_onto_attacks(f.ctl, f.part, c) ? slide_lead(f) : 0;
        if (f.join_ns && g_set.debug) fprintf(stderr, "[touch] slid from R onto the attacks: they join R in %.0f ms\n", (f.join_ns - SDL_GetTicksNS()) / 1e6);
    }
    f.ctl = c, f.part = part, f.btn = is_ranged(c) ? ranged_buttons(c, g_game.weapon_drawn) : 0;
    recompute_held(s);
    haptics_tap();
    if (g_set.debug)
        fprintf(stderr, "[touch] slid onto %s, %s kept (buttons %llx)\n", DEFS[c].key, f.kept_ctl >= 0 ? DEFS[f.kept_ctl].key : "nothing",
                (unsigned long long)finger_buttons(f));
}

// ---- the layout editor: a panel in the middle (smaller, bigger, the ranged layout, the attacks' style, defaults,
// cancel, save); drag a control to move it, pinch (a second finger anywhere) or the panel's - / + to size it. The panel
// itself goes where its title strip is dragged (a grip at each end of it), and is as small as EPANEL_MIN: picked there,
// a pinch or its - / + size it, so that it leaves free what is being placed (the MHGU port's the same)
const int SEL_PANEL = -2;  // (s.sel / a finger's ctl: the editor's panel, picked by its title strip)
const int SEL_FPS = -3;    // (...the FPS counter's plate)
const int SEL_BARS = -4;   // (...the monsters' health bars: hunt_bars.cpp, 1.0.1)
const int SEL_BARS_GRIP = -5;  // (a finger's ctl only: the bars' corner handle, their size; s.sel stays SEL_BARS)
const int SEL_ITEMS = -6;       // (...the quick items' piece: quick_items.cpp, 1.0.1.1)
const int SEL_ITEMS_GRIP = -7;  // (a finger's ctl only: its corner handle; s.sel stays SEL_ITEMS)
const int SEL_CHAT = -8;        // (...the chat's button: chat_overlay.cpp, 1.0.2)
const uint64_t PANEL_REPEAT = 700 * MS;  // (taps on its - or + this close together: one button's, State::rep_btn)
bool grabbed(int ctl) { return ctl >= 0 || ctl == SEL_PANEL || ctl == SEL_BARS || ctl == SEL_ITEMS || ctl == SEL_CHAT; }  // (a finger's ctl: something it moves)
void panel_dims(float w, float h, float k, float *pw, float *ph) {
    // (between the left edge's column and the right thumb's controls, where it hides none of them, on a wide screen)
    *pw = std::clamp(w - 1.04f * h, std::min(w * .9f, 1.1f * h), std::min(w * .9f, 1.8f * h)) * k, *ph = .28f * h * k;
}
// The panel, its buttons and its title strip (grab: where a finger picks and drags it), on the screen whole; its measure
// (the screen's height at 100 %, smaller with it)
float editor_panel(const EditPanel &ep, float w, float h, Rect *panel, Rect btn[E_COUNT], Rect *grab = nullptr) {
    float k = std::clamp(ep.k, EPANEL_MIN, 1.0f), u = k * h, pw, ph;
    panel_dims(w, h, k, &pw, &ph);
    float x0 = ep.x < 0 ? (w - pw) * .5f : ep.x * w - pw * .5f, y0 = ep.y < 0 ? (h - ph) * .4f : ep.y * h - ph * .5f;
    x0 = std::clamp(x0, 0.0f, std::max(0.0f, w - pw)), y0 = std::clamp(y0, 0.0f, std::max(0.0f, h - ph));
    *panel = {x0, y0, x0 + pw, y0 + ph, true};
    // (two rows, as the MHGU port's: what is picked, then the rest; the emulator-style layout has no melee or ranged
    // layout and no attacks' style: their buttons are left out)
#ifndef __ANDROID__
    bool emu = g_kind == KIND_EMU;
    auto left_out = [emu](int i) {
        if (g_edit_hud && (i == E_HOLD || i == E_IMAGE || i == E_SET || i == E_STYLE)) return true;  // (the HUD's alone)
        return emu && (i == E_SET || i == E_STYLE);
    };
    int n1 = 0, j1 = 0;
    for (int i = 0; i < E_ROW2; i++) n1 += !left_out(i);
    float gap = .012f * u, bh = .082f * u, r1 = y0 + .1f * u, r2 = r1 + bh + gap, bw = (pw - gap * (n1 + 1)) / n1;
    if (grab) *grab = {x0, y0, x0 + pw, r1 - gap * .5f, true};
    for (int i = 0; i < E_ROW2; i++) {
        btn[i] = {0, 0, 0, 0, false};
        if (left_out(i)) continue;
        btn[i] = {x0 + gap + j1 * (bw + gap), r1, x0 + gap + j1 * (bw + gap) + bw, r1 + bh, true};
        j1++;
    }
    int n = 0, j = 0;
    for (int i = E_ROW2; i < E_COUNT; i++) n += !left_out(i);
    bw = (pw - gap * (n + 1)) / n;
    for (int i = E_ROW2; i < E_COUNT; i++) {
        btn[i] = {0, 0, 0, 0, false};
        if (left_out(i)) continue;
        btn[i] = {x0 + gap + j * (bw + gap), r2, x0 + gap + j * (bw + gap) + bw, r2 + bh, true};
        j++;
    }
    return u;
#else
    float gap = .012f * u, bh = .082f * u, r1 = y0 + .1f * u, r2 = r1 + bh + gap, bw = (pw - gap * (E_ROW2 + 1)) / E_ROW2;
    if (grab) *grab = {x0, y0, x0 + pw, r1 - gap * .5f, true};
    for (int i = 0; i < E_ROW2; i++) btn[i] = {x0 + gap + i * (bw + gap), r1, x0 + gap + i * (bw + gap) + bw, r1 + bh, true};
    bool emu = g_kind == KIND_EMU;
    int n = 0, j = 0;
    for (int i = E_ROW2; i < E_COUNT; i++) n += !(emu && (i == E_SET || i == E_STYLE));
    bw = (pw - gap * (n + 1)) / n;
    for (int i = E_ROW2; i < E_COUNT; i++) {
        btn[i] = {0, 0, 0, 0, false};
        if (emu && (i == E_SET || i == E_STYLE)) continue;
        btn[i] = {x0 + gap + j * (bw + gap), r2, x0 + gap + j * (bw + gap) + bw, r2 + bh, true};
        j++;
    }
#endif
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

// The buttons a tap holds down (Layout.hold) can be: those that press the game's buttons for as long as they are down -
// not the attacks' pill or band (a part each), the stick, the d-pads, the menu pad, the arrows (steps), the settings,
// touch screen, town and Lock-on buttons, nor the aim (a tap already holds it). The MHGU port's the same
bool holdable(int i) {
    if (i < 0 || i >= C_LAID) return false;
    return !(i == C_ATTACK || i == C_CORNER || i == C_STICK || is_dpad(i) || i == C_MPAD || i == C_SETTINGS || i == C_LOCKON ||
             i == C_MAPTOG ||
             i == C_ITEML || i == C_ITEMR || i == C_RUNLOCK || i == C_AMMO_UP || i == C_AMMO_DOWN || i == C_RAIM ||
             i == C_SCREEN || i == C_TOWN || i == C_LAYOUT);
}
// A picture of the player's (Layout.image) can be on any control the editor moves (1.0.1: up to 1.0 the round buttons
// alone, not the pill, the band, the stick, the d-pads, the menu pad nor the sprint lock's circle), and on the menu pad's
// SELECT (1.0.2: it goes with the pad, its picture is its own; the tabs have none)
bool image_ok(int i) { return (i >= 0 && i < C_LAID) || i == C_MSELECT; }
// A line under the editor's panel for a few seconds (what a profile's action did, once its window closed)
void edit_note(State &s, const char *text) {
    snprintf(s.note, sizeof s.note, "%s", text);
    s.note_until = SDL_GetTicksNS() + 5000 * MS;
}
// The FPS counter's plate as it was last drawn (perfstats.cpp), in window pixels: the editor's frame for it, while it
// shows
bool fps_frame(Rect *r) {
    float v[4];
    if (!perfstats_rect(v)) return false;
    *r = {v[0], v[1], v[2], v[3], true};
    return true;
}
// It dragged by (dx, dy) pixels, kept on the screen whole (from its own place the first time: where it shows)
void fps_move(State &s, float dx, float dy) {
    Rect r;
    if (!fps_frame(&r)) return;
    float hw = (r.x1 - r.x0) * .5f, hh = (r.y1 - r.y0) * .5f;
    float cx = s.layout.fps_x >= 0 ? s.layout.fps_x * s.w : (r.x0 + r.x1) * .5f, cy = s.layout.fps_y >= 0 ? s.layout.fps_y * s.h : (r.y0 + r.y1) * .5f;
    s.layout.fps_x = std::clamp(cx + dx, hw, std::max(hw, s.w - hw)) / s.w;
    s.layout.fps_y = std::clamp(cy + dy, hh, std::max(hh, s.h - hh)) / s.h;
    publish(s.layout);
}
// The monsters' health bars as they were last drawn (hunt_bars.cpp: the bars themselves, in window pixels), while they
// show: in the editor with their option on there is a stand-in bar out of quests, so that they can be placed
bool bars_frame(Rect *r) {
    float v[4];
    if (!hunt_bars_rect(v)) return false;
    *r = {v[0], v[1], v[2], v[3], true};
    return true;
}
// They dragged by (dx, dy) pixels, kept on the top screen's picture whole (from their own place the first time: where
// they show); their size in twentieths, half to twice the size they came with
void bars_move(State &s, float dx, float dy) {
    float v[4];
    if (!hunt_bars_rect(v)) return;
    hunt_bars_drag(g_top, v, dx, dy, &s.layout.bars_x, &s.layout.bars_y);
    publish(s.layout);
}
void bars_size(State &s, float k) {
    s.layout.bars_k = hunt_bars_size(k);
    publish(s.layout);
}
// Their corner handle (their size, as a pinch: the top right corner stays): a square on their frame's bottom left
// corner, where the finger that picked it started and their size and block then (hunt_bars_grip)
Rect bars_grip(const Rect &bars, float h) {
    float g = .05f * h, cx = bars.x0 - .008f * h, cy = bars.y1 + .008f * h;
    return {cx - g * .5f, cy - g * .5f, cx + g * .5f, cy + g * .5f, true};
}
struct BarsGrip {
    float k0 = 1, block[4] = {}, x0 = 0, y0 = 0;
} g_bars_grip;
// The quick items' piece as last drawn (quick_items.cpp), its move and size, as the bars'; its corner handle on its
// frame's bottom right corner (its middle stays: quick_items_grip)
bool items_frame(Rect *r) {
    float v[4];
    if (!quick_items_rect(v)) return false;
    *r = {v[0], v[1], v[2], v[3], true};
    return true;
}
void items_move(State &s, float dx, float dy) {
    float v[4];
    if (!quick_items_rect(v)) return;
    quick_items_drag(s.w, s.h, v, dx, dy, &s.layout.items_x, &s.layout.items_y);
    publish(s.layout);
}
// (1.0.2) The chat's button in the editor (the owner's: placed and sized as every button, kept with the profile): where
// chat/chat_overlay.cpp draws it - CHAT_HOME of the window until moved, CHAT_R screen heights times its size
const float CHAT_HOME_X = .11f, CHAT_HOME_Y = .30f, CHAT_R = .048f;
bool chat_bubble_on() { return chat::bubble_shown(); }  // (Chat > Options > Chat button)
Geo chat_geo(const Layout &l, float w, float h) {
    float x = l.chat_x < 0 ? CHAT_HOME_X : l.chat_x, y = l.chat_y < 0 ? CHAT_HOME_Y : l.chat_y;
    return {ImVec2(x * w, y * h), CHAT_R * l.chat_k * h};
}
void chat_move(State &s, float dx, float dy) {
    Geo g = chat_geo(s.layout, s.w, s.h);
    float x = std::clamp(g.c.x + dx, g.r, std::max(g.r, s.w - g.r)), y = std::clamp(g.c.y + dy, g.r, std::max(g.r, s.h - g.r));
    s.layout.chat_x = x / s.w, s.layout.chat_y = y / s.h;
    publish(s.layout);
}
void chat_size(State &s, float k) {
    s.layout.chat_k = std::clamp(roundf(k * 100.0f) / 100.0f, .6f, 2.0f);
    publish(s.layout);
}
void items_size(State &s, float k) {
    s.layout.items_k = quick_items_size(k);
    publish(s.layout);
}
Rect items_grip(const Rect &items, float h) {
    float g = .05f * h, cx = items.x1 + .008f * h, cy = items.y1 + .008f * h;
    return {cx - g * .5f, cy - g * .5f, cx + g * .5f, cy + g * .5f, true};
}
BarsGrip g_items_grip;
// The buttons a tap held down, let go (why: for the log): the controls hidden or edited, a profile changed; one alone where
// it is no longer on the screen (frame_input)
void unlatch(State &s, uint64_t which, const char *why) {
    if (!(s.latched & which)) return;
    for (int i = 0; i < C_COUNT; i++)
        if (which >> i & 1) s.latch_btn[i] = 0;
    s.latched &= ~which;
    fprintf(stderr, "[touch] held buttons let go (%s)\n", why);
}
void hud_load(State &s);
// (1.0.2) The map's button: the map the game draws on the main screen (the HUD's Area map) shown or hidden, as the editor's
// Hide does (hud_map's fourth number in mh4u.ini, kept). While the game hasn't drawn it there since it started (the map is
// among the player's touch panels), a line says how to have it there, and nothing changes
void map_toggle() {
    float v[HUD_V];
    hud_parse(game_option_get("hud_map", "0 0"), v);
    if (v[3] == 0 && !g_game.hud[HP_MAP].seen) {
        settings_panel_notice("The map is on your touch screen panels. To see it on the main screen, take the Map off your "
                              "panels in Customize Panels (quest menu).",
                              "El mapa está en los paneles de tu pantalla táctil. Para verlo en la pantalla principal, quita "
                              "el Mapa de tus paneles en Personalizar paneles (menú de misión).", 6);
        fprintf(stderr, "[touch] the map's button: no map on the main screen yet\n");
        return;
    }
    v[3] = v[3] != 0 ? 0.0f : 1.0f;
    game_option_set("hud_map", hud_value(v));
    fprintf(stderr, "[touch] the map's button: the map %s\n", v[3] != 0 ? "hidden" : "shown");
}
void edit_begin(State &s) {
#ifndef __ANDROID__
    g_edit_hud = !s.visible && g_set.controls != TOUCH_ON;  // (no touch controls on the screen: the HUD's parts alone)
    if (g_edit_hud) fprintf(stderr, "[touch] layout editor: the HUD's parts (no touch controls on the screen)\n");
#endif
    s.edit = true, s.sel = -1, s.pinch = false;
    s.user_hidden = false, s.hide_note_until = 0;  // (the editor shows them all, and they stay shown after it)
    g_town_edit = true;
    hud_load(s);
    g_edit_set = g_ranged != 0;  // (the layout in use; the panel's button changes it)
    s.saved = s.layout, s.epanel_open = g_epanel, s.rep_btn = -1;
    s.open_slot = s.layout.profile, s.prof_open = s.img_open = false, s.note_until = 0;
    s.base = s.layout, memcpy(s.base_hud, s.hud_at, sizeof s.base_hud);
    for (Finger &f : s.f) f.kind = F_NONE;
    s.stick_on = false, s.held = 0;
    unlatch(s, ~0ull, "the editor");
    run_end(s, "the editor");
    fprintf(stderr, "[touch] layout editor (profile %d)\n", s.layout.profile);
}
// ---- the game's HUD in the editor: its parts drag as a whole, each where the player wants it (Port::Hooks moves the
// game's own layout objects: hud_<key> in mh4u.ini, live while dragged), and stay there in every hunt
// The part where the editor shows it: its art at the game's own place (where the hooks say it is, less the offset it is
// drawn with) moved by the offset the editor has for it now (the game may show the new one a frame later)
// (1.0.2: at the size the editor has for it, toward its corner, which the hooks give: the game draws it so a frame later)
Rect hud_rect(const State &s, int k) {
    const UiGameState::HudPart &p = g_game.hud[k];
    if (p.r.w <= 0 || p.r.h <= 0 || g_top.w <= 0 || g_top.h <= 0) return {0, 0, 0, 0, false};
    float sx = g_top.w / (float)std::max(g_game.top_width, 1), sy = g_top.h / 240.0f;
    float dx = s.edit ? (s.hud_at[k][0] - p.dx) * sx : 0, dy = s.edit ? (s.hud_at[k][1] - p.dy) * sy : 0;
    float x0 = p.r.x, y0 = p.r.y, x1 = p.r.x + p.r.w, y1 = p.r.y + p.r.h;
    float q = s.edit && p.scale > 0 ? g_hud_size * s.hud_at[k][2] / p.scale : 1.0f;
    if (fabsf(q - 1.0f) > 1e-3f) {
        float ax = p.anchor.x, ay = p.anchor.y;
        x0 = ax + (x0 - ax) * q, x1 = ax + (x1 - ax) * q, y0 = ay + (y0 - ay) * q, y1 = ay + (y1 - ay) * q;
    }
    return {x0 + dx, y0 + dy, x1 + dx, y1 + dy, true};
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
    char t[64];
    if (fabsf(v[2] - 1.0f) < .005f && v[3] == 0) snprintf(t, sizeof t, "%.0f %.0f", v[0], v[1]);  // (as up to 1.0.1.1)
    else snprintf(t, sizeof t, "%.0f %.0f %.2f %d", v[0], v[1], v[2], v[3] != 0 ? 1 : 0);
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
        hud_parse(game_option_get(std::string("hud_") + HUD_KEY[k], k == HP_ITEMS ? game_option_get("item_bar", "0 0") : "0 0"), s.hud_at[k]);
        std::copy(s.hud_at[k], s.hud_at[k] + HUD_V, s.hud_open[k]);
    }
    float hs = (float)atof(game_option_get("hud_size", "100").c_str());  // (the settings' HUD size: hud_rect)
    g_hud_size = hs > 2.0f ? std::clamp(hs / 100.0f, .6f, 1.0f) : hs > 0 ? std::clamp(hs, .6f, 1.0f) : 1.0f;
}
// (1.0.2) a part's own size (- / +, a pinch) and hidden (Hide), shown by the game at once
void hud_size_set(State &s, int k, float v) {
    v = std::clamp(roundf(v * 100.0f) / 100.0f, HUD_K_MIN, HUD_K_MAX);
    if (v != s.hud_at[k][2]) s.hud_at[k][2] = v, hud_preview(s, k);
}
void hud_hide(State &s, int k) {
    s.hud_at[k][3] = s.hud_at[k][3] != 0 ? 0.0f : 1.0f;
    hud_preview(s, k);
    fprintf(stderr, "[touch] layout editor: the HUD's %s %s\n", HUD_KEY[k], s.hud_at[k][3] != 0 ? "hidden" : "shown");
}
// The part back at the game's place and size, shown
void hud_reset(State &s, int k) {
    const float none[HUD_V] = {0, 0, 1.0f, 0};
    if (!std::equal(none, none + HUD_V, s.hud_at[k])) std::copy(none, none + HUD_V, s.hud_at[k]), hud_preview(s, k);
}
// Save: each part moved to mh4u.ini (the item bar's under its older key too, which an earlier version wrote); Cancel:
// each back where it was
void hud_end(State &s, bool keep) {
    for (int k = 0; k < UiGameState::HUD_PARTS; k++) {
        if (std::equal(s.hud_at[k], s.hud_at[k] + HUD_V, s.hud_open[k])) continue;
        if (!keep) {
            game_option_preview(std::string("hud_") + HUD_KEY[k], hud_value(s.hud_open[k]));
            continue;
        }
        if (k == HP_ITEMS) game_option_set("item_bar", hud_value(s.hud_at[k]));
        game_option_set(std::string("hud_") + HUD_KEY[k], hud_value(s.hud_at[k]));
        fprintf(stderr, "[touch] HUD: %s now %s\n", HUD_KEY[k], hud_value(s.hud_at[k]).c_str());
    }
}

void edit_end(State &s, bool keep) {
    hud_end(s, keep);
    if (keep) {  // (the layout in use, and its profile's file: a save folder that can't be reached keeps only the first)
        if (s.layout.profile != s.open_slot) keep_in_use(s.saved, s.hud_open);  // (another profile in use from now on)
        s.layout.profile_line = true;
        save_layout(s.layout), s.saved = s.layout;
        if (!profile_write(s.layout.profile, s.layout, s.hud_at)) {
            const char *msg = "The layout is in use, but profile %d couldn't be saved in its folder. Is the save folder there?";
            char en[200], es[200];
            snprintf(en, sizeof en, settings_text(0, msg), s.layout.profile);
            snprintf(es, sizeof es, settings_text(1, msg), s.layout.profile);
            settings_panel_notice(en, es);
        }
    } else {
        s.layout = s.saved;
    }
    publish(s.layout);
    s.prof_open = s.img_open = false;
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

// A profile's layout is the one the editor edits now (loaded, or a new one): its HUD's parts where it has them (shown at
// once, kept at Save), the melee layout
void slot_into_editor(State &s, int n, const Layout &l) {
    s.layout = l;
    s.layout.profile = n, s.layout.profile_line = true;
    run_clear_of_band(s.layout);
    publish(s.layout);
    g_edit_set = 0;
    s.sel = -1;
    for (int k = 0; k < HUD_N; k++) {
        const float none[HUD_V] = {0, 0, 1.0f, 0};
        const float *v = l.has_hud ? l.hud[k] : none;
        if (!std::equal(v, v + HUD_V, s.hud_at[k])) std::copy(v, v + HUD_V, s.hud_at[k]), hud_preview(s, k);
    }
    fprintf(stderr, "[touch] layout editor: profile %d (%s)\n", n, s.layout.kind == KIND_EMU ? "emulator-style" : "the port's own");
}

// ---- the profiles' window, over the editor (its Profile button): the ten slots in two rows, what each holds, the one
// in use and the one being edited; a slot picked, its buttons: Load it into the editor, Save the editor's layout in it
// (the editor goes on with that one), a New layout of either kind in it, Delete it; Close. What replaces or empties a
// profile asks for a second tap, as tabarong's overwrite does. Nothing is played with until the editor's Save. The MHGU
// port's the same
enum { PA_LOAD, PA_SAVE, PA_NEW_OURS, PA_NEW_EMU, PA_DELETE, PA_CLOSE, PA_COUNT };
// The window: its frame, the slots, its buttons, and where its line goes (y)
void prof_window(float w, float h, Rect *win, Rect slot[PROFILES_N], Rect btn[PA_COUNT], float *say_y) {
    float ww = std::min(w * .96f, 1.75f * h), wh = .74f * h, x0 = (w - ww) * .5f, y0 = (h - wh) * .5f;
    *win = {x0, y0, x0 + ww, y0 + wh, true};
    float gap = .016f * h, top = y0 + .095f * h, sw = (ww - gap * 6) / 5, sh = .2f * h;
    for (int i = 0; i < PROFILES_N; i++) {
        float sx = x0 + gap + (i % 5) * (sw + gap), sy = top + (i / 5) * (sh + gap);
        slot[i] = {sx, sy, sx + sw, sy + sh, true};
    }
    *say_y = top + 2 * sh + gap + .045f * h;
    float bh = .1f * h, by0 = y0 + wh - gap - bh, bw = (ww - gap * (PA_COUNT + 1)) / (float)PA_COUNT;
    for (int i = 0; i < PA_COUNT; i++) btn[i] = {x0 + gap + i * (bw + gap), by0, x0 + gap + i * (bw + gap) + bw, by0 + bh, true};
}
void prof_refresh(State &s) {
    for (int i = 0; i < PROFILES_N; i++) s.prof_used[i] = profile_info(i + 1, &s.prof_kind[i]);
}
void prof_say(State &s, const char *english, int n) { snprintf(s.prof_say, sizeof s.prof_say, T(english), n); }
void prof_open(State &s) {
    s.prof_open = true, s.img_open = false, s.prof_pick = s.layout.profile - 1, s.prof_arm = -1, s.prof_btn = -1;
    prof_refresh(s);
    bool chosen;
    profiles_dir(&chosen);
    snprintf(s.prof_say, sizeof s.prof_say, "%s",
             T(chosen ? "Pick a profile. They are kept in your save folder, and go with it to your other devices."
                      : "Pick a profile. Save in the editor plays with the one you are editing."));
}
// The editor's layout (or its HUD parts) changed since it came into the editor: not saved anywhere yet
bool unsaved(const State &s) {
    return !same_layout(s.layout, s.base) || memcmp(s.hud_at, s.base_hud, sizeof s.base_hud) != 0;
}
void set_base(State &s) { s.base = s.layout, memcpy(s.base_hud, s.hud_at, sizeof s.base_hud); }
void prof_action(State &s, int a) {
    int n = s.prof_pick + 1;
    bool used = s.prof_used[n - 1], again = s.prof_arm == a;
    s.prof_arm = -1;
    // (1.0.2) Load or New leave the editor's layout: changes not saved anywhere are asked for first (a second tap leaves
    // them); and the profile in use, if it has no file yet, is written before another can take its place
    bool leaves = (a == PA_LOAD && used && n != s.layout.profile) || a == PA_NEW_OURS || a == PA_NEW_EMU;
    if (leaves && !again && unsaved(s)) {
        s.prof_arm = a;
        prof_say(s, "Profile %d has changes you haven't saved: tap again to leave them", s.layout.profile);
        return;
    }
    switch (a) {
    case PA_CLOSE:
        s.prof_open = false;
        break;
    case PA_LOAD: {
        Layout l;
        if (!used) prof_say(s, "Profile %d is empty: New makes a layout in it", n);
        else if (n == s.layout.profile) prof_say(s, "You are editing profile %d already", n);
        else if (!load_profile_file(profile_path(n), &l)) prof_say(s, "Profile %d can't be read. Is the save folder there?", n);
        else {
            keep_in_use(s.saved, s.hud_open);
            slot_into_editor(s, n, l), s.prof_open = false, set_base(s);
            char t[200];
            snprintf(t, sizeof t, T("Profile %d loaded: Save plays with it, Cancel goes back"), n);
            edit_note(s, t);
        }
        break;
    }
    case PA_SAVE: {
        if (used && n != s.layout.profile && !again) {
            s.prof_arm = a;
            prof_say(s, "Profile %d holds a layout: tap Save here again to replace it", n);
            break;
        }
        if (n != s.open_slot) keep_in_use(s.saved, s.hud_open);
        if (!profile_write(n, s.layout, s.hud_at)) {
            prof_say(s, "Profile %d couldn't be saved. Is the save folder there?", n);
            break;
        }
        s.layout.profile = n, s.layout.profile_line = true, set_base(s);
        prof_refresh(s);
        fprintf(stderr, "[touch] profiles: the editor's layout saved in profile %d\n", n);
        prof_say(s, "Saved in profile %d, which the editor edits now: its Save plays with it", n);
        break;
    }
    case PA_NEW_OURS:
    case PA_NEW_EMU: {
        if (used && !again) {
            s.prof_arm = a;
            prof_say(s, "Profile %d holds a layout: tap New again to start it over", n);
            break;
        }
        Layout l = defaults(a == PA_NEW_EMU ? KIND_EMU : KIND_OURS);
        l.has_hud = true, l.has_combos = true;  // (the HUD's parts at the game's places; no combined buttons)
        keep_in_use(s.saved, s.hud_open);
        slot_into_editor(s, n, l), s.prof_open = false, set_base(s);
        char t[200];
        snprintf(t, sizeof t, T("A new layout in profile %d: Save keeps it and plays with it"), n);
        edit_note(s, t);
        break;
    }
    case PA_DELETE:
        if (!used) prof_say(s, "Profile %d is empty", n);
        else if (n == s.open_slot || n == s.layout.profile) prof_say(s, "Profile %d is in use or being edited: it can't be emptied", n);
        else if (!again) s.prof_arm = a, prof_say(s, "Tap Delete again to empty profile %d", n);
        else if (remove(profile_path(n).c_str()) != 0) prof_say(s, "Profile %d couldn't be emptied", n);
        else prof_refresh(s), prof_say(s, "Profile %d emptied", n), fprintf(stderr, "[touch] profiles: profile %d emptied\n", n);
        break;
    }
}
// A finger down while the window is open: a slot picks it, a button acts (lit while held); elsewhere nothing
void prof_tap(State &s, float x, float y) {
    Rect win, slot[PROFILES_N], btn[PA_COUNT];
    float say_y;
    prof_window(s.w, s.h, &win, slot, btn, &say_y);
    for (int i = 0; i < PROFILES_N; i++)
        if (slot[i].in(x, y)) {
            if (s.prof_pick != i) s.prof_arm = -1;
            s.prof_pick = i;
            if (s.prof_used[i]) prof_say(s, s.prof_kind[i] == KIND_EMU ? "Profile %d: an emulator-style layout" : "Profile %d: a port layout", i + 1);
            else prof_say(s, "Profile %d is empty", i + 1);
            haptics_tap();
            return;
        }
    for (int i = 0; i < PA_COUNT; i++)
        if (btn[i].in(x, y)) {
            s.prof_btn = i;
            haptics_tap();
            prof_action(s, i);
            return;
        }
}

// ---- the pictures' window (the editor's Picture button, a control picked): the pictures in the profiles' images
// folder, a page of them; one tapped goes on the control. None takes it off; Add pictures opens the screen that copies
// the player's own in (PicturesActivity.java); the folder is read again while the window is open. The pictures are the
// player's own, and their responsibility. For (the stick's alone): its base's picture or its knob's. The MHGU port's the
// same
enum { IA_NONE, IA_PREV, IA_NEXT, IA_KNOB, IA_ADD, IA_CLOSE, IA_COUNT };
// The picture the window sets for control c: the stick's knob's or the menu pad's SELECT's while it picks that, else the
// control's own
char *img_target(State &s, int c) {
    return c == C_STICK && s.img_knob ? s.layout.knob : c == C_MPAD && s.img_select ? s.layout.image[C_MSELECT] : s.layout.image[c];
}
const char *img_target(const State &s, int c) { return img_target(const_cast<State &>(s), c); }
// (the window's "For:" button: the stick's base or knob, the menu pad or its SELECT while it shows)
bool img_has_part(int c) { return c == C_STICK || (c == C_MPAD && mpad_member(C_MSELECT)); }
const int IMG_PAGE = 12;  // (two rows of six)
void img_window(float w, float h, Rect *win, Rect tile[IMG_PAGE], Rect btn[IA_COUNT], float *say_y) {
    float ww = std::min(w * .96f, 1.75f * h), wh = .74f * h, x0 = (w - ww) * .5f, y0 = (h - wh) * .5f;
    *win = {x0, y0, x0 + ww, y0 + wh, true};
    float gap = .016f * h, top = y0 + .095f * h, tw = (ww - gap * 7) / 6, th = .2f * h;
    for (int i = 0; i < IMG_PAGE; i++) {
        float tx = x0 + gap + (i % 6) * (tw + gap), ty = top + (i / 6) * (th + gap);
        tile[i] = {tx, ty, tx + tw, ty + th, true};
    }
    *say_y = top + 2 * th + gap + .045f * h;
    float bh = .1f * h, by0 = y0 + wh - gap - bh, bw = (ww - gap * (IA_COUNT + 1)) / (float)IA_COUNT;
    for (int i = 0; i < IA_COUNT; i++) btn[i] = {x0 + gap + i * (bw + gap), by0, x0 + gap + i * (bw + gap) + bw, by0 + bh, true};
}
bool picture_file(const char *name) {  // (what the window lists: PNG and BMP, which SDL reads; the import makes PNG)
    size_t n = strlen(name);
    if (n < 5 || n >= (size_t)IMAGE_NAME || name[0] == '.') return false;
    const char *e = name + n - 4;
    return !SDL_strcasecmp(e, ".png") || !SDL_strcasecmp(e, ".bmp");
}
void img_scan(State &s) {
    s.img_n = 0;
    if (DIR *d = opendir(images_dir().c_str())) {
        while (dirent *e = readdir(d))
            if (picture_file(e->d_name) && s.img_n < IMG_MAX) snprintf(s.img_names[s.img_n++].n, IMAGE_NAME, "%s", e->d_name);
        closedir(d);
    }
    std::sort(s.img_names, s.img_names + s.img_n, [](const ImgName &a, const ImgName &b) { return SDL_strcasecmp(a.n, b.n) < 0; });
    int pages = std::max(1, (s.img_n + IMG_PAGE - 1) / IMG_PAGE);
    s.img_page = std::clamp(s.img_page, 0, pages - 1);
    s.img_scan_ns = SDL_GetTicksNS();
}
// (the page of the picture the control has)
void img_page_of(State &s) {
    for (int i = 0; i < s.img_n; i++)
        if (s.sel >= 0 && s.sel < C_COUNT && !strcmp(s.img_names[i].n, img_target(s, s.sel))) s.img_page = i / IMG_PAGE;
}
void img_open(State &s) {
    s.img_open = true, s.prof_open = false, s.img_page = 0, s.img_btn = -1;
    // (picked by a tap on the stick's knob or the menu pad's SELECT: that one's picture, 1.0.2)
    s.img_knob = s.sel == C_STICK && s.sel_knob, s.img_select = s.sel == C_MPAD && s.sel_select && mpad_member(C_MSELECT);
    img_scan(s);
    img_page_of(s);
}
void img_tap(State &s, float x, float y) {
    Rect win, tile[IMG_PAGE], btn[IA_COUNT];
    float say_y;
    img_window(s.w, s.h, &win, tile, btn, &say_y);
    int c = s.sel >= 0 && s.sel < C_COUNT ? s.sel : -1;
    for (int i = 0; i < IMG_PAGE; i++) {
        int k = s.img_page * IMG_PAGE + i;
        if (k < s.img_n && tile[i].in(x, y) && c >= 0) {
            snprintf(img_target(s, c), IMAGE_NAME, "%s", s.img_names[k].n);
            fprintf(stderr, "[touch] layout editor: a picture on %s%s (%s)\n", DEFS[c == C_MPAD && s.img_select ? C_MSELECT : c].key,
                    c == C_STICK && s.img_knob ? "'s knob" : "", s.img_names[k].n);
            s.img_open = false;
            haptics_tap();
            return;
        }
    }
    int pages = std::max(1, (s.img_n + IMG_PAGE - 1) / IMG_PAGE);
    for (int i = 0; i < IA_COUNT; i++) {
        if (!btn[i].in(x, y)) continue;
        s.img_btn = i;
        haptics_tap();
        if (i == IA_NONE && c >= 0) img_target(s, c)[0] = 0, s.img_open = false;
        else if (i == IA_KNOB && c == C_STICK) s.img_knob = !s.img_knob, img_page_of(s);
        else if (i == IA_KNOB && img_has_part(c)) s.img_select = !s.img_select, img_page_of(s);
        else if (i == IA_PREV) s.img_page = (s.img_page + pages - 1) % pages;
        else if (i == IA_NEXT) s.img_page = (s.img_page + 1) % pages;
        else if (i == IA_ADD) make_folder(profiles_dir()), make_folder(images_dir()), app_call_activity("importPictures");
        else if (i == IA_CLOSE) s.img_open = false;
        return;
    }
}

// The item bar's touch area at size k (Layout.item_zone: the editor's - / + and a pinch with the item bar picked), in
// hundredths, kept between ITEM_ZONE_MIN and ITEM_ZONE_MAX
void item_zone_set(State &s, float k) {
    s.layout.item_zone = std::clamp(roundf(k * 100.0f) / 100.0f, ITEM_ZONE_MIN, ITEM_ZONE_MAX);
    publish(s.layout);
}
// The panel's buttons. Reset: what is picked back to its default (a control's place and size, a part of the HUD at the
// game's place, the panel's place and size, the FPS counter at the top); Defaults: every control and part of the HUD
// (the kind's own); Hide (the emulator-style layout's controls), Hold, Picture, Profile (their windows)
void edit_button(State &s, int i, const Rect *btn, uint64_t now, int again) {
    int c = s.sel >= 0 && s.sel < C_COUNT ? s.sel : -1, part = s.sel >= ZONE_SEL ? s.sel - ZONE_SEL : -1;
    switch (i) {
    case E_SMALLER:
    case E_BIGGER: {
        float by = i == E_BIGGER ? .1f : -.1f;
        if (c >= 0) s.layout.scale[c] += by, clamp_place(s, c);
        else if (part >= 0) hud_size_set(s, part, s.hud_at[part][2] + by);  // (1.0.2: a part's size; the item bar's too)
        else if (s.sel == SEL_BARS) bars_size(s, s.layout.bars_k + by);
        else if (s.sel == SEL_ITEMS) items_size(s, s.layout.items_k + by);
        else if (s.sel == SEL_CHAT) chat_size(s, s.layout.chat_k + by);
        else if (s.sel == SEL_PANEL) {  // (about the button pressed: it stays under the finger)
            panel_resize(s, g_epanel.k + by, (btn[i].x0 + btn[i].x1) * .5f, (btn[i].y0 + btn[i].y1) * .5f);
            s.rep_btn = i, s.rep_ns = now, s.rep_at = again < 0 ? btn[i] : s.rep_at;
        }
        break;
    }
    case E_HIDE:
        if (hideable(c)) s.layout.off ^= 1ull << c, publish(s.layout);
        else if (part >= 0) hud_hide(s, part);  // (1.0.2: any part of the HUD)
        break;
    case E_RESET:
        if (c >= 0) def_place(s.layout.kind, c, &s.layout.x[c], &s.layout.y[c]), s.layout.scale[c] = 1.0f;
        else if (part >= 0) {  // (at its size, shown; and the item bar's touch area at its size)
            hud_reset(s, part);
            if (part == HP_ITEMS) item_zone_set(s, 1.0f);
        }
        else if (s.sel == SEL_PANEL) panel_resize(s, 1.0f, s.w * .5f, s.h * .5f), g_epanel = EditPanel{};
        else if (s.sel == SEL_FPS) s.layout.fps_x = s.layout.fps_y = -1.0f, publish(s.layout);
        else if (s.sel == SEL_BARS) s.layout.bars_x = s.layout.bars_y = -1.0f, s.layout.bars_k = 1.0f, publish(s.layout);
        else if (s.sel == SEL_ITEMS) s.layout.items_x = s.layout.items_y = -1.0f, s.layout.items_k = 1.0f, publish(s.layout);
        else if (s.sel == SEL_CHAT) s.layout.chat_x = s.layout.chat_y = -1.0f, s.layout.chat_k = 1.0f, publish(s.layout);
        break;
    case E_HOLD:  // (a tap holds it down, the next lets it go: S13)
        if (holdable(c)) {
            s.layout.hold ^= 1ull << c;
            fprintf(stderr, "[touch] layout editor: %s %s by a tap\n", DEFS[c].key, s.layout.hold >> c & 1 ? "held" : "no longer held");
        }
        break;
    case E_IMAGE:
        if (image_ok(c)) img_open(s);
        break;
    case E_PROFILES:
        prof_open(s);
        break;
    case E_SET:
        g_edit_set = g_edit_set == 1 ? 0 : 1;
        if (s.sel != SEL_PANEL) s.sel = -1;
        break;
    case E_STYLE:
        if (!ranged_layout()) next_style(s);
        break;
    case E_DEFAULTS: {  // (the kind's own: the profile stays the one it is)
        Layout was = s.layout;
        s.layout = defaults(was.kind), s.layout.profile = was.profile, s.layout.profile_line = was.profile_line;
        memcpy(s.layout.combo, was.combo, sizeof was.combo), s.layout.has_combos = true;  // (they stay: not a placement)
#ifndef __ANDROID__
        if (g_edit_hud) {  // (the HUD's parts alone: the touch controls, not shown, stay as they were)
            const Layout d = s.layout;
            s.layout = was;
            s.layout.fps_x = d.fps_x, s.layout.fps_y = d.fps_y, s.layout.bars_x = d.bars_x, s.layout.bars_y = d.bars_y;
            s.layout.bars_k = d.bars_k, s.layout.items_x = d.items_x, s.layout.items_y = d.items_y, s.layout.items_k = d.items_k;
            s.layout.item_zone = d.item_zone;
        }
#endif
        publish(s.layout);
        for (int k = 0; k < HUD_N; k++) hud_reset(s, k);  // (the HUD's parts back at the game's places and sizes, shown)
        break;
    }
    default:
        edit_end(s, i == E_SAVE);
    }
}

void edit_event(State &s, const SDL_Event &e, float x, float y) {
    if (e.type == SDL_EVENT_FINGER_DOWN) {
        Finger *f = nullptr;
        for (Finger &g : s.f)
            if (g.kind == F_NONE) { f = &g; break; }
        if (!f) return;
        *f = {};
        f->id = e.tfinger.fingerID, f->x = f->x0 = x, f->y = f->y0 = y, f->ctl = -1;
        if (s.prof_open || s.img_open) {  // (a window over the editor: its own, nothing under it)
            f->kind = F_MENU, f->ctl = -1;
            if (s.prof_open) prof_tap(s, x, y);
            else img_tap(s, x, y);
            return;
        }
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
                edit_button(s, i, btn, now, again);
                if (!s.edit) f->kind = F_NONE;
                return;
            }
        Finger *drag = nullptr;
        for (Finger &g : s.f)
            if (&g != f && g.kind == F_EDIT && grabbed(g.ctl)) drag = &g;
        if (drag) {  // a second finger: pinch the control being dragged, or the panel
            s.pinch = true, s.pinch_d0 = std::max(1.0f, hypotf(x - drag->x, y - drag->y));
            s.pinch_s0 = drag->ctl == SEL_PANEL ? g_epanel.k : drag->ctl == SEL_BARS ? s.layout.bars_k
                         : drag->ctl == SEL_ITEMS ? s.layout.items_k
                         : drag->ctl == SEL_CHAT ? s.layout.chat_k
                         : drag->ctl == ZONE_SEL + HP_ITEMS ? s.layout.item_zone  // (the item bar: its touch area)
                         : drag->ctl >= ZONE_SEL ? s.hud_at[drag->ctl - ZONE_SEL][2] : s.layout.scale[drag->ctl];
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
        // the quick items' corner handle (its size; as the bars' below)
        if (Rect blk; !panel.in(x, y) && items_frame(&blk) && items_grip(blk, s.h).in(x, y)) {
            g_items_grip.k0 = s.layout.items_k, g_items_grip.x0 = x, g_items_grip.y0 = y;
            g_items_grip.block[0] = blk.x0, g_items_grip.block[1] = blk.y0, g_items_grip.block[2] = blk.x1, g_items_grip.block[3] = blk.y1;
            f->kind = F_EDIT, f->ctl = SEL_ITEMS_GRIP, s.sel = SEL_ITEMS;
            return;
        }
        // the monsters' health bars' corner handle (their size, over the controls, not the panel: a small square aimed at)
        if (Rect blk; !panel.in(x, y) && bars_frame(&blk) && bars_grip(blk, s.h).in(x, y)) {
            g_bars_grip.k0 = s.layout.bars_k, g_bars_grip.x0 = x, g_bars_grip.y0 = y;
            g_bars_grip.block[0] = blk.x0, g_bars_grip.block[1] = blk.y0, g_bars_grip.block[2] = blk.x1, g_bars_grip.block[3] = blk.y1;
            f->kind = F_EDIT, f->ctl = SEL_BARS_GRIP, s.sel = SEL_BARS;
            return;
        }
        // a control, else the panel's face (nothing moves), else a part of the game's HUD, else the panel by its title
        // strip's edge
        int c = hit(s.layout, x, y, s.w, s.h, nullptr, 1.5f, true, false, true);
        if (c >= C_LAID) c = -1;
#ifndef __ANDROID__
        if (g_edit_hud) c = -1;  // (the HUD's parts alone)
#endif
        s.sel_knob = s.sel_select = false;  // (a tap on the stick's knob or on SELECT: the picture window sets theirs, 1.0.2)
        if (c == C_STICK) {
            Geo g = place(s.layout, C_STICK, s.w, s.h);
            s.sel_knob = hypotf(x - g.c.x, y - g.c.y) < g.r * KNOB * 1.2f;
        } else if (c == C_MPAD && mpad_member(C_MSELECT)) {
            Geo g = place(s.layout, C_MSELECT, s.w, s.h), p = place(s.layout, C_MPAD, s.w, s.h);
            s.sel_select = hypotf(x - g.c.x, y - g.c.y) / g.r < hypotf(x - p.c.x, y - p.c.y) / p.r;
        }
        if (c < 0 && panel.in(x, y)) { f->kind = F_MENU; return; }
        Rect fps, bars;  // (the FPS counter, while it shows: over the HUD's parts)
        if (c < 0 && fps_frame(&fps) && fps.in(x, y)) {
            f->kind = F_EDIT, f->ctl = SEL_FPS, s.sel = SEL_FPS;
            return;
        }
        if (c < 0 && chat_bubble_on()) {  // (1.0.2: the chat's button, while Chat > Options > Chat button is on)
            const Geo g = chat_geo(s.layout, s.w, s.h);
            if (hypotf(x - g.c.x, y - g.c.y) < g.r * 1.25f) {
                f->kind = F_EDIT, f->ctl = SEL_CHAT, s.sel = SEL_CHAT;
                return;
            }
        }
        Rect items;  // (the quick items' piece, or its stand-in: over the HUD's parts too, a little larger)
        if (c < 0 && items_frame(&items) && x >= items.x0 - .012f * s.h && x <= items.x1 + .012f * s.h &&
            y >= items.y0 - .012f * s.h && y <= items.y1 + .012f * s.h) {
            f->kind = F_EDIT, f->ctl = SEL_ITEMS, s.sel = SEL_ITEMS;
            return;
        }
        // (the monsters' health bars, or their stand-in: over the HUD's parts too, a little larger)
        if (c < 0 && bars_frame(&bars) && x >= bars.x0 - .012f * s.h && x <= bars.x1 + .012f * s.h &&
            y >= bars.y0 - .012f * s.h && y <= bars.y1 + .012f * s.h) {
            f->kind = F_EDIT, f->ctl = SEL_BARS, s.sel = SEL_BARS;
            return;
        }
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
            if (a && b && c == ZONE_SEL + HP_ITEMS) item_zone_set(s, s.pinch_s0 * by);
            else if (a && b && c >= ZONE_SEL) hud_size_set(s, c - ZONE_SEL, s.pinch_s0 * by);  // (1.0.2: a part's size)
            if (a && b && c == SEL_BARS) bars_size(s, s.pinch_s0 * by);
            if (a && b && c == SEL_ITEMS) items_size(s, s.pinch_s0 * by);
            if (a && b && c == SEL_CHAT) chat_size(s, s.pinch_s0 * by);
            if (a && b && c == SEL_PANEL) {  // (about its middle)
                Rect p, bt[E_COUNT];
                editor_panel(g_epanel, s.w, s.h, &p, bt);
                panel_resize(s, s.pinch_s0 * by, (p.x0 + p.x1) * .5f, (p.y0 + p.y1) * .5f);
            }
        } else if (f->ctl == SEL_PANEL) {
            panel_move(s, dx, dy);
        } else if (f->ctl == SEL_FPS) {
            fps_move(s, dx, dy);
        } else if (f->ctl == SEL_BARS) {
            bars_move(s, dx, dy);
        } else if (f->ctl == SEL_BARS_GRIP) {
            bars_size(s, hunt_bars_grip(g_bars_grip.k0, g_bars_grip.block, g_bars_grip.x0, g_bars_grip.y0, x, y));
        } else if (f->ctl == SEL_CHAT) {
            chat_move(s, dx, dy);
        } else if (f->ctl == SEL_ITEMS) {
            items_move(s, dx, dy);
        } else if (f->ctl == SEL_ITEMS_GRIP) {
            items_size(s, quick_items_grip(g_items_grip.k0, g_items_grip.block, g_items_grip.x0, g_items_grip.y0, x, y));
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
    if (f->kind == F_MENU) s.prof_btn = s.img_btn = -1;  // (a window's button no longer held)
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
            if (DEFS[i].right != (side == 1) || !in_mode(i, s.menu) || i == C_SETTINGS) continue;  // (it never moves: place)
            if (!s.visible) continue;
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

// A mouse's left click comes first as a finger (SDL_MOUSE_TOUCHID: SDL's touch made of the click, the tap it was in
// 0.1.9.3), then as the click: that finger landed on the game's picture, where no control is (the camera's ground)
// (controls.cpp captures the mouse only for such a click; one on the touch screen or a control stays a tap there)
bool g_mouse_tap_picture = false;
Uint64 g_mouse_tap_ns = 0;  // (when that finger came: the click follows at once)

// (1.0.2, T10, as the MHGU port's: the touch controls gone in the middle of a game) The controls hidden because something
// counts as a controller (show the touch controls: Auto) while the player keeps tapping the game - three taps, each within
// 4 s of the last -: once a session a notice names the device and says how to show them anyway (an earbud's or a
// keyboard's buttons the phone calls a controller hid them, with nothing on the screen to say why)
void pad_hidden_tap(State &s, uint64_t now) {
    if (!s.pad_hidden || s.pad_said || s.menu) return;
    s.pad_taps = now - s.pad_tap_ns < 4000 * MS ? s.pad_taps + 1 : 1;
    s.pad_tap_ns = now;
    if (s.pad_taps < 3) return;
    s.pad_said = true;
    std::string name = controls_real_pad_name();
    const char *msg = "%s counts as a controller, so the touch controls are hidden. To show them anyway: gear, Controls, Show the touch controls, Always.";
    char en[400], es[400];
    snprintf(en, sizeof en, settings_text(0, msg), name.empty() ? settings_text(0, "A device") : name.c_str());
    snprintf(es, sizeof es, settings_text(1, msg), name.empty() ? settings_text(1, "A device") : name.c_str());
    settings_panel_notice(en, es);
    fprintf(stderr, "[touch] taps on the game with the controls hidden by a controller (%s): the notice said why\n",
            name.empty() ? "?" : name.c_str());
}
bool finger_event(const SDL_Event &e) {
    State &s = g_s;
#ifndef __ANDROID__
    if (e.type == SDL_EVENT_FINGER_DOWN && e.tfinger.touchID != SDL_MOUSE_TOUCHID && !g_screen_touched) {
        g_screen_touched = true;
        fprintf(stderr, "[touch] a touch screen's finger: the touch controls show at Auto from now on\n");
    }
#endif
    if (e.type == SDL_EVENT_FINGER_DOWN && e.tfinger.touchID == SDL_MOUSE_TOUCHID)
        g_mouse_tap_picture = false, g_mouse_tap_ns = e.tfinger.timestamp;
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
        bool gear = false;  // (1.0.2: the settings button over the touch screen's corner takes its finger: it never moves)
        if (s.visible || s.gear_only) {
            const Geo g = place(s.layout, C_SETTINGS, s.w, s.h);
            gear = hypotf(x - g.c.x, y - g.c.y) < g.r * 1.22f;
        }
        if (!gear && g_bottom.in(x, y)) return false;  // (the touch screen's corner: the game's stylus, android_main.cpp)
        if (!gear && quick_items_hit(x, y)) return false;  // (the quick items' piece: the same, its part of the touch screen)
        Finger *f = nullptr;
        for (Finger &g : s.f)
            if (g.kind == F_NONE) { f = &g; break; }
        if (!f) return true;
        s.last_ns = now;
        *f = {};
        f->id = e.tfinger.fingerID, f->x = f->x0 = x, f->y = f->y0 = y, f->t0 = now, f->f0 = g_frames;
        f->from_group = f->kept_ctl = -1, f->mouse = e.tfinger.touchID == SDL_MOUSE_TOUCHID, f->join_ns = 0;
        f->cam_off = show(TB_CAMRIGHT) && x < s.w * .5f;  // (the camera from the right half only: S-CAM, 1.0.2)
        for (Finger &g : s.f)  // (two fingers on the screen: neither's long press hides the controls)
            if (&g != f && g.kind != F_NONE) g.multi = f->multi = true;
        if (!s.visible) {
            int part = 0;
            if (s.gear_only && hit(s.layout, x, y, s.w, s.h, &part, 1.22f, false, s.menu) == C_SETTINGS) {
                f->kind = F_BUTTON, f->ctl = C_SETTINGS, f->part = part;  // (a tap opens the settings, as when they show)
                haptics_tap();
                return true;
            }
            if (chat::bubble_down(e.tfinger.x, e.tfinger.y)) {  // (the chat button: chat/chat_overlay.cpp)
                f->kind = F_CHAT;
                return true;
            }
            pad_hidden_tap(s, now);  // (T10: hidden by a controller, the game tapped: say why, once)
            f->kind = F_CAMERA;  // (hidden: a drag still turns the camera)
            g_mouse_tap_picture = e.tfinger.touchID == SDL_MOUSE_TOUCHID;
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
        int part = 0, c = hit(s.layout, x, y, s.w, s.h, &part, 1.22f, false, s.menu);
        if ((c == C_R || c == C_DODGE || c == C_CORNER || c == C_RDODGE || c == C_RSECOND || c == C_AMMO_UP ||
             c == C_AMMO_DOWN) && item_zone(s.menu, true).in(x, y))
            c = -1;  // (narrow screens: their places reach the item bar)
        // (the stick's ground is the stick's, but for a button's own circle: the margin round the Town and touch screen
        // buttons reaches it, with the stick made bigger or the left side made smaller beside the touch screen's corner,
        // and a thumb meant for the stick opened the town's list or swapped the screens, 4u-native #31)
        if (c >= 0 && c != C_ATTACK && c != C_CORNER && c != C_MPAD && !is_tab(c) && !s.stick_on &&
            in_mode(C_STICK, s.menu) && !covered(s.layout, C_STICK, s.w, s.h) && in_stick_zone(s, x, y, item)) {
            Geo g = place(s.layout, c, s.w, s.h);
            if (hypotf(x - g.c.x, y - g.c.y) > g.r) c = -1;
        }
        // the chat button (chat/chat_overlay.cpp), where no control is: the controls keep their place over it
        if (c < 0 && chat::bubble_down(e.tfinger.x, e.tfinger.y)) {
            f->kind = F_CHAT;
            return true;
        }
        if (c >= 0) {
            f->kind = F_BUTTON, f->ctl = c, f->part = part, f->from_group = slide_group(c, part);
            s.use_until = s.use_at = 0;  // (another control: the item waiting for the weapon to go away is given up)
            f->btn = is_ranged(c) ? ranged_buttons(c, g_game.weapon_drawn) : 0;
            f->out0 = g_game.weapon_drawn == 1;
            if (c == C_RAIM && s.aim_latch) s.aim_latch = 0, f->done = true;  // (the aim held open: this tap lets it go)
            if (c == C_AMMO_UP || c == C_AMMO_DOWN) ammo_step(s, c == C_AMMO_UP ? 1 : -1);
            if (c == C_SCREEN) touch_toggle_bottom();
            if (c == C_TOWN) town_open(false);
            if (c == C_TREE) weapon_tree_open(false);  // (1.0.2: the Smithy's weapon tree)
            if (c == C_ITEML || c == C_ITEMR) item_swipe(s, c == C_ITEMR ? 1 : -1), f->rep = now + 420 * MS;
            if (c == C_LOCKON) lock_toggle("the Lock-on button");  // (as it lands; lit while held, presses nothing)
            if (c == C_MAPTOG) map_toggle();
            // (a button a tap holds down, Layout.hold: this tap holds it, what it presses now staying pressed after the
            // finger lifts, lit; the next tap lets it go, and presses nothing more)
            if ((s.layout.hold >> c & 1) && holdable(c)) {
                if (s.latched >> c & 1) {
                    unlatch(s, 1ull << c, DEFS[c].key);
                    f->done = true;
                } else {
                    s.latched |= 1ull << c, s.latch_btn[c] = finger_buttons(*f);
                    fprintf(stderr, "[touch] %s held by a tap (%llx)\n", DEFS[c].key, (unsigned long long)s.latch_btn[c]);
                }
            }
            if (c != C_MPAD || part) haptics_tap();  // (the menu pad: as it presses a direction, not as it is touched)
        } else if (item.in(x, y)) {
            f->kind = F_ITEM;
        } else if (!s.stick_on && in_mode(C_STICK, s.menu) && !covered(s.layout, C_STICK, s.w, s.h) &&
                   in_stick_zone(s, x, y, item)) {
            f->kind = F_STICK;
            Geo g = place(s.layout, C_STICK, s.w, s.h);
            run_end(s, "the stick touched again");  // (a run going on by itself stops: this thumb drives now)
            s.stick_on = true, s.bx = g.c.x, s.by = g.c.y;
            if (floating_stick()) s.bx = x, s.by = y;  // (the floating stick: its base under the thumb)
            stick_to(s, x, y);
            // (a new thumb may lock from wherever it landed, slid up into the circle again - not one that landed in it,
            // which a stick placed high has near)
            const Geo lg = lock_geo(s.layout, s, s.w, s.h);
            if (hypotf(x - lg.c.x, y - lg.c.y) >= LOCK_GRAB / LOCK_R * lg.r) s.run_armed = true;
        } else {
            f->kind = F_CAMERA;
            g_mouse_tap_picture = e.tfinger.touchID == SDL_MOUSE_TOUCHID;
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
    if (f->kind == F_CHAT) {  // (a drag moves the chat button, a tap opens the chat)
        const bool up = e.type != SDL_EVENT_FINGER_MOTION;
        chat::bubble_finger(up, e.tfinger.x, e.tfinger.y);
        if (up) f->kind = F_NONE;
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
            if (f->kind != F_CAMERA || !f->cam_off)  // (show_camera_right_half: not one that landed on the left half)
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
        } else if (f->kind == F_BUTTON && !f->done && f->from_group >= 0 && slide_both(s)) {  // onto another button: both
            slide_to(s, *f, x, y);
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
                f->join_ns = f->keep ? slide_lead(*f) : 0;  // (1.0.2: R held a moment before the attack, slide_lead)
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
    f->join_ns = 0;  // (lifted while the attack waited to join R: both, as before)
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
    // or slid it aims while the thumb is, its slide turning the aim as a drag of the view does, and letting go ends it.
    // (1.0.1.1: that R on the RG406V was the draw: its save's Bow Controls had R draw and shoot, as the PC test save's.
    // With them so (bow_type 1, read from the game now) the aim is X, which raises and lowers the aiming mode by itself:
    // a tap is a tap)
    if (f->kind == F_BUTTON && f->ctl == C_RAIM && !f->done && !cancel && f->btn && f->out0 && g_game.weapon_drawn == 1 && !s.menu &&
        !f->moved && now - f->t0 < AIM_TAP && e.type == SDL_EVENT_FINGER_UP && !(g_ranged == 1 && g_game.bow_type != 0))
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
    {  // the hooks hold the bar open for the long press
        bool want = s.bar_open;
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
    // a long press on the settings button opens the editor (L held is the game's L: the lock-on is its own button's)
    for (Finger &f : s.f) {
        if (f.kind != F_BUTTON || f.done) continue;
        if (f.ctl == C_SETTINGS && now - f.t0 > 700 * MS) {
            f.done = true;
            haptics_tap();
            edit_begin(s);
            break;
        }
    }
    // the lock-on ends by itself away from the quest camera and out of a quest for a while (a town, the quest's end) or
    // turned off in the settings. In a quest it stays: a mount, a cart, a cutscene run no quest camera (the MHGU port's
    // lock-on went off by itself after a mount, gu-native #220)
    {
        static uint64_t away_ns;
        if (!Port::Hooks::LockOn() || Port::Hooks::CameraTurnActive() || g_game.place == 2) away_ns = 0;
        else if (!away_ns) away_ns = now;
        if (Port::Hooks::LockOn() && (!lock_on_allowed() || (away_ns && now - away_ns > 10000 * MS))) {
            Port::Hooks::SetLockOn(false);
            away_ns = 0;
            fprintf(stderr, "[touch] lock-on off (%s)\n", lock_on_allowed() ? "no quest camera" : "the settings");
        }
    }
    {  // a long press on an empty spot: the controls hidden, or shown again (hide_finger)
        float t = 0;
        int k = hide_finger(s, now, &t);
        if (k >= 0 && t >= 1.0f) {
            s.f[k].done = true;  // (once a press; a drag of it afterwards still turns the camera)
            s.user_hidden = !s.user_hidden;
            haptics_tap();
            fprintf(stderr, "[touch] controls %s by a long press\n", s.user_hidden ? "hidden" : "shown again");
            s.hide_note_until = 0;
            if (s.user_hidden) {
                s.hide_note_first = !app_setting_bool("Controls", "HideHintSeen", false);
                s.hide_note_until = now + (s.hide_note_first ? 6000 : 2500) * MS;
                if (s.hide_note_first) app_setting_set("Controls", "HideHintSeen", "1"), app_settings_save();
            }
        }
    }
#ifndef __ANDROID__
    if (!g_screen_touched) controller = true;  // (the PC: Auto waits for a real touch screen's finger)
#endif
    bool visible = s.edit || (!s.user_hidden && (g_set.controls == TOUCH_ON || (g_set.controls == TOUCH_AUTO && !controller)));
    s.pad_hidden = !s.edit && !s.user_hidden && g_set.controls == TOUCH_AUTO && controller;  // (T10)
    if (visible != s.visible) {
        fprintf(stderr, "[touch] controls %s (controls %s, controller %d%s)\n", visible ? "shown" : "hidden",
                CONTROLS_KEY[g_set.controls], (int)controller, s.user_hidden ? ", hidden by a long press" : "");
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
    // (hidden, by a controller or by off: the settings button alone stays, the way back; hidden by a long press, nothing:
    // another long press is the way back)
    s.gear_only = !visible && !s.user_hidden;
#ifndef __ANDROID__
    s.gear_only = s.gear_only && g_screen_touched;  // (a keyboard and a mouse: F1 is the settings' button)
#endif
    // (the buttons a tap held down: let go with the controls hidden or edited; one alone once it is off the screen - the
    // ranged layout's as the weapon goes, Run as a menu opens -, or no longer held by a tap; the pictures' window read
    // again now and then, for the pictures being added)
    if (s.latched && (!visible || s.edit)) unlatch(s, s.latched, !visible ? "the controls hidden" : "the editor"), recompute_held(s);
    for (int i = 0; i < C_COUNT && s.latched; i++)
        if ((s.latched >> i & 1) && (!in_mode(i, s.menu) || !(s.layout.hold >> i & 1))) unlatch(s, 1ull << i, DEFS[i].key), recompute_held(s);
    if (s.edit && s.img_open && now - s.img_scan_ns > 1500 * MS) img_scan(s);
    for (Finger &f : s.f)  // (an attack slid onto from R joins it now: slide_lead)
        if (f.join_ns && f.kind == F_BUTTON && now >= f.join_ns && (!g_frames_fed || frames_ok(f.f0 + SLIDE_LEAD_FRAMES))) {
            f.join_ns = 0, recompute_held(s);
            if (g_set.debug) fprintf(stderr, "[touch] the attack joined R (%.0f ms after R)\n", (now - f.t0) / 1e6);
        }
    uint64_t b = pulse_buttons(s, now) | seq_buttons(s.item, now) | seq_buttons(s.ammo, now) | seq_buttons(s.cursor, now);
    // the camera: by the angle dragged where the port's hook turns it, else as the C-stick, pushed by the drag's speed
    float dt = s.frame_ns ? std::clamp((now - s.frame_ns) / 1e9f, .004f, .1f) : .016f;
    s.frame_ns = now;
    // (the drag's turn smoothed, camera_smooth: what the fingers drag goes to the camera over a moment, none of it lost,
    // and a turn glides to a stop after the finger does; GU#244's ask, the owner's call for 1.0.1, off by default. Not the
    // gyro's, added below as it comes)
    if (g_set.smooth > 0 && !s.edit) {
        s.cam_rest_x += s.cam_dx, s.cam_rest_y += s.cam_dy;
        const float k = 1.0f - expf(-dt / cam_smooth_tau(g_set.smooth));
        s.cam_dx = s.cam_rest_x * k, s.cam_dy = s.cam_rest_y * k;
        s.cam_rest_x -= s.cam_dx, s.cam_rest_y -= s.cam_dy;
        if (fabsf(s.cam_rest_x) < .05f && fabsf(s.cam_rest_y) < .05f)  // (the last of it at once)
            s.cam_dx += s.cam_rest_x, s.cam_dy += s.cam_rest_y, s.cam_rest_x = s.cam_rest_y = 0;
    } else {
        s.cam_rest_x = s.cam_rest_y = 0;
    }
    {  // aiming with the gyro (controls_gyro): a bow or a bowgun out, its aim's button held (a finger, a tap's hold, a
       // controller, a key), the gyroscope's turn added as a drag's, in degrees
        const uint64_t aim = ranged_buttons(C_RAIM, 1);
        // (1.0.2, S4) With the game's Bow Controls Type 2 (R draws and shoots) a bow's X raises and lowers the aiming mode
        // by itself (a tap is a tap: no button is held in it), so the aim's button alone left the gyro off there, and while
        // drawing in it. The mode is followed here: on at an X pressed with the bow out (the touch Aim, a controller's X,
        // a key's), off at the next X, and once the hunter's action is no longer one of the bow's raised ones (BOW_ACTIONS:
        // aiming, drawing, shooting; AIM_SETTLE after the X, the game's action may not show it yet). Type 1 aims while R
        // is held, its draw (X) in the aiming mode too: that kept the gyro on as it was
        static const int BOW_ACTIONS[] = {0x565, 0x569, 0x505, 0x573, 0x56c, 0x576};  // (mh4n's hooks.cpp BOW_*)
        const uint64_t AIM_SETTLE = 400 * MS;
        const bool x_held = ((s.held | controls_buttons()) & BTN_X) != 0;
        if (g_ranged == 1 && g_game.bow_type == 1 && g_game.weapon_drawn == 1 && !s.menu && !s.edit) {
            if (x_held && !s.bow_x) s.bow_aim = !s.bow_aim, s.bow_aim_ns = now;
            bool raised = false;
            for (int a : BOW_ACTIONS) raised |= g_game.action == a;
            if (s.bow_aim && !raised && now - s.bow_aim_ns > AIM_SETTLE) s.bow_aim = false;
        } else {
            s.bow_aim = false;
        }
        s.bow_x = x_held;
        const bool aiming = g_ranged && g_game.weapon_drawn == 1 && !s.menu && !s.edit &&
                            (((s.held | controls_buttons()) & aim) || s.bow_aim);
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
    bool ran = s.run;  // (the run's R in the last update that got to the end: below)
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
            // move (the MHGU port's #151: a thumb at the rim through a sheathe started R in the same update as an X + A,
            // and the Great Sword guarded from the sheath). But a run whose R was held RUN_X_WAIT already, a sprint going
            // on, keeps it as X and A come together: R + X + A, as with a controller's R held through a sprint. Sheathed,
            // that is the Charge Blade's draw into the axe (X + A alone does nothing for it), and the Great Sword's guard
            // from the sheath (X + A alone: the plain draw); from 0.1.9 to 1.0 the tap on the seam in a sprint did
            // nothing with a Charge Blade (the PC runner, a Charge Blade sheathed in a field: R + X + A, action 0589 and
            // the axe out; X + A while running, nothing). X alone still lets R go (with R or without, the same draw attack:
            // 056D). The same in the MHGU port
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
            run_held |= (s.latched >> C_RUN & 1) != 0;  // (held by a tap)
            bool want = s.run_lock || s.rim || (run_held && !stick_run && g_game.weapon_drawn != 1 && !s.menu);
            const uint64_t XA = BTN_X | BTN_A;
            bool keep = ran && now - s.run_t0 >= RUN_X_WAIT && (b & XA) == XA;
            if ((b & BTN_X) && !keep) s.run_x_until = now + RUN_X_WAIT;
            // In a village the single-screen mod opens its World Map with L and R together: no run's R while an L of the
            // touch controls is pressed (the camera button) nor for two game frames after it, so that a dash
            // and a tap on the camera can't open the map (4u-native #31). The L and R buttons pressed together still do.
            // (not once the game's counter went back, a new run of it, as frames_ok; and a second at most, as a press
            // waits for the game's frames: FRAME_WAIT)
            if (b & BTN_L) s.run_l_frame = g_frames, s.run_l_ns = now;
            const bool town_l = g_game.place == 1 && s.run_l_ns != 0 && now - s.run_l_ns < FRAME_WAIT &&
                                (g_frames_fed ? g_frames >= s.run_l_frame && g_frames <= s.run_l_frame + 2
                                              : now - s.run_l_ns < RUN_L_WAIT);
            s.run = want && (keep || now >= s.run_x_until) && !town_l;
            if (s.run && !ran) s.run_t0 = now;
            if (s.run) b |= BTN_R;
            static bool said_x, said_run, said_keep;  // (the log: once a change, what held R said as it lets go too)
            static const char *said_why = "";
            if (g_set.debug && keep && s.run && !said_keep) fprintf(stderr, "[touch] run: R kept with X + A (a sprint: the game's R + X + A)\n");
            said_keep = keep && s.run;
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
bool g_draw_map_hidden;                        // (1.0.2: the map's button's face, a slash while the map is hidden)

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
    case I_TREE: {  // (1.0.2) the weapon tree: a trunk and three branches, a node at each end (the panel's sign)
        const float t = .11f * s;
        dl->AddLine(P(c, s, -.62f, 0), P(c, s, -.08f, 0), col, t);
        for (float y : {-.5f, 0.f, .5f}) {
            dl->AddLine(P(c, s, -.08f, 0), P(c, s, -.08f, y), col, t);
            dl->AddLine(P(c, s, -.08f, y), P(c, s, .5f, y), col, t);
            dl->AddCircleFilled(P(c, s, .52f, y), .15f * s, col, 12);
        }
        dl->AddCircleFilled(P(c, s, -.62f, 0), .19f * s, col, 14);
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
    case I_LOCKON: {  // the lock-on: a reticle with a padlock in its middle (the MHGU port's the same)
        dl->AddCircle(c, .74f * s, col, 28, .1f * s);
        for (int i = 0; i < 4; i++) {
            float a = i * PI * .5f;
            dl->AddLine(ImVec2(c.x + cosf(a) * .6f * s, c.y + sinf(a) * .6f * s),
                        ImVec2(c.x + cosf(a) * .95f * s, c.y + sinf(a) * .95f * s), col, .1f * s);
        }
        padlock(dl, ImVec2(c.x, c.y - .06f * s), .62f * s, true, col, deep);
        break;
    }
    case I_MAP: {  // (1.0.2) the map: a map folded in three, a route on it and a pin; hidden, a slash across
        ImVec2 m[4][4] = {{P(c, s, -.78f, -.46f), P(c, s, -.26f, -.62f), P(c, s, -.26f, .46f), P(c, s, -.78f, .62f)},
                          {P(c, s, -.26f, -.62f), P(c, s, .26f, -.46f), P(c, s, .26f, .62f), P(c, s, -.26f, .46f)},
                          {P(c, s, .26f, -.46f), P(c, s, .78f, -.62f), P(c, s, .78f, .46f), P(c, s, .26f, .62f)}};
        for (int i = 0; i < 3; i++) dl->AddConvexPolyFilled(m[i], 4, i == 1 ? fade(col, .78f) : col);
        ImVec2 route[4] = {P(c, s, -.6f, .3f), P(c, s, -.3f, .05f), P(c, s, .05f, .2f), P(c, s, .34f, -.08f)};
        dl->AddPolyline(route, 4, deep, .07f * s, 0);
        ImVec2 pin = P(c, s, .42f, -.2f);
        dl->AddTriangleFilled(ImVec2(pin.x - .13f * s, pin.y - .02f * s), ImVec2(pin.x + .13f * s, pin.y - .02f * s),
                              ImVec2(pin.x, pin.y + .22f * s), deep);
        dl->AddCircleFilled(pin, .14f * s, deep, 14);
        dl->AddCircleFilled(pin, .055f * s, col, 10);
        if (g_draw_map_hidden) {
            dl->AddLine(P(c, s, -.82f, -.78f), P(c, s, .82f, .78f), deep, .26f * s);
            dl->AddLine(P(c, s, -.82f, -.78f), P(c, s, .82f, .78f), col, .12f * s);
        }
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

void stick_base(ImDrawList *dl, ImVec2 base, float r, float a, bool on) {
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
}
void stick_knob(ImDrawList *dl, ImVec2 knob, float kr, float a, bool on) {
    disc(dl, knob, kr, on, a);
    float d = kr * .32f;
    ImVec2 q[4] = {ImVec2(knob.x, knob.y - d), ImVec2(knob.x + d, knob.y), ImVec2(knob.x, knob.y + d), ImVec2(knob.x - d, knob.y)};
    dl->AddConvexPolyFilled(q, 4, fade(GOLD, a));
}
void stick(ImDrawList *dl, ImVec2 base, ImVec2 knob, float r, float a, bool on) {
    stick_base(dl, base, r, a, on);
    stick_knob(dl, knob, r * KNOB, a, on);
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

// ---- the players' pictures on the controls (Layout.image, S06; 1.0.1: the owner's "que solo quede el png"): any control
// the editor moves can take a picture of the player's own, at their own responsibility, and then the picture alone is
// drawn in the control's place - no disc, ring, frame, letter badge nor glow of the port's -, a press shown on the
// picture itself (a little smaller and darker; a control that is on, a little larger). The stick takes one for its base
// and one for its knob (Layout.knob). Without a picture a control looks as it always did.
// Read from the profiles' images folder the first time one is drawn (inside ImGui's frame), at most PIC_SIDE pixels a
// side, kept as ImGui textures the SDL renderer's backend uploads with the font's. A file that can't be read or isn't a
// PNG or a BMP is tried again now and then and drawn as the control's own look meanwhile. Up to PIC_CACHE at once: one not
// drawn for a while is given back (its texture destroyed by the backend, then let go of) and read again when it shows.
// (The MHGU port's overlay had a pool of 16 textures and the 17th picture closed the game, gu-native #237; this
// renderer's backend makes one SDL texture a picture, with no pool, and the cache keeps their number down all the same)
const int PIC_SIDE = 256, PIC_CACHE = 64;
const uint64_t PIC_KEEP_NS = 10000 * MS, PIC_SWAP_NS = 2000 * MS;  // (unused this long: given back; or for room)
const int PIC_FRAMES = 8;  // (nor drawn in as many of ImGui's frames)
struct PicTex {
    char name[IMAGE_NAME];  // (empty: a free slot)
    ImTextureData *tex;
    uint64_t tried_ns;  // (not read: tried again after a while, a picture being copied in)
    uint64_t used_ns;   // (drawn last: when, and ImGui's frame)
    int used_frame;
    bool said;          // (its failure logged)
};
PicTex g_pics[PIC_CACHE];
// The textures given back: destroyed by the backend at the next render (WantDestroy), then taken out of ImGui's list
// and deleted (pics_sweep)
std::vector<ImTextureData *> g_pics_retired;
void pic_drop(PicTex &p) {
    if (p.tex) {
        p.tex->WantDestroyNextFrame = true;  // (destroyed for good: SetStatus would make a texture with pixels again)
        p.tex->SetStatus(ImTextureStatus_WantDestroy);
        p.tex->UnusedFrames = 1 << 20;
        g_pics_retired.push_back(p.tex);
    }
    p = PicTex{};
}
bool pic_idle(const PicTex &p, uint64_t now, uint64_t ns) {
    return ImGui::GetFrameCount() - p.used_frame >= PIC_FRAMES && now - p.used_ns >= ns;
}
// Once a frame of the touch controls: the pictures no longer shown (another page of the editor's window, a control's
// picture changed) given back after a while, and those the backend has destroyed let go of
void pics_sweep() {
    uint64_t now = SDL_GetTicksNS();
    for (PicTex &p : g_pics)
        if (p.name[0] && pic_idle(p, now, PIC_KEEP_NS)) pic_drop(p);
    for (size_t i = 0; i < g_pics_retired.size();) {
        ImTextureData *t = g_pics_retired[i];
        if (t->Status != ImTextureStatus_Destroyed) { i++; continue; }
        if (g_set.debug) fprintf(stderr, "[touch] a picture's texture given back (%zu more waiting)\n", g_pics_retired.size() - 1);
        ImGui::UnregisterUserTexture(t);
        IM_DELETE(t);
        g_pics_retired[i] = g_pics_retired.back();
        g_pics_retired.pop_back();
    }
}
ImTextureData *pic_texture(const char *name) {
    if (!name || !name[0]) return nullptr;
    uint64_t now = SDL_GetTicksNS();
    PicTex *found = nullptr, *free_slot = nullptr;
    for (PicTex &p : g_pics) {
        if (!strcmp(p.name, name)) { found = &p; break; }
        if (!p.name[0] && !free_slot) free_slot = &p;
    }
    if (found) found->used_ns = now, found->used_frame = ImGui::GetFrameCount();
    if (found && (found->tex || now - found->tried_ns < 3000 * MS)) return found->tex;
    if (!found && !free_slot) {  // (full: the one unused longest, if it has been a while)
        PicTex *old = nullptr;
        for (PicTex &p : g_pics)
            if (pic_idle(p, now, PIC_SWAP_NS) && (!old || p.used_ns < old->used_ns)) old = &p;
        if (!old) return nullptr;
        pic_drop(*old);
        free_slot = old;
    }
    PicTex &p = found ? *found : *free_slot;
    if (!found) snprintf(p.name, sizeof p.name, "%s", name), p.used_ns = now, p.used_frame = ImGui::GetFrameCount();
    p.tex = nullptr, p.tried_ns = now;
    std::string data;
    if (!read_text(images_dir() + "/" + name, &data) || data.empty()) {
        if (!p.said) fprintf(stderr, "[touch] picture %s: not in %s\n", name, images_dir().c_str()), p.said = true;
        return nullptr;
    }
    SDL_Surface *src = SDL_LoadSurface_IO(SDL_IOFromConstMem(data.data(), data.size()), true);
    if (!src) {
        if (!p.said) fprintf(stderr, "[touch] picture %s can't be read: %s\n", name, SDL_GetError()), p.said = true;
        return nullptr;
    }
    int w = src->w, h = src->h;
    float k = std::min(1.0f, (float)PIC_SIDE / (float)std::max(w, h));
    SDL_Surface *rgba = SDL_ConvertSurface(src, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(src);
    if (rgba && k < 1.0f) {
        SDL_Surface *small = SDL_ScaleSurface(rgba, std::max(1, (int)(w * k)), std::max(1, (int)(h * k)), SDL_SCALEMODE_LINEAR);
        SDL_DestroySurface(rgba);
        rgba = small;
    }
    if (!rgba) {
        if (!p.said) fprintf(stderr, "[touch] picture %s can't be converted: %s\n", name, SDL_GetError()), p.said = true;
        return nullptr;
    }
    ImTextureData *t = IM_NEW(ImTextureData)();
    t->Create(ImTextureFormat_RGBA32, rgba->w, rgba->h);
    t->UseColors = true;
    for (int y = 0; y < rgba->h; y++) memcpy(t->Pixels + (size_t)y * rgba->w * 4, (const uint8_t *)rgba->pixels + (size_t)y * rgba->pitch, (size_t)rgba->w * 4);
    fprintf(stderr, "[touch] picture %s: %dx%d, drawn at %dx%d\n", name, w, h, rgba->w, rgba->h);
    SDL_DestroySurface(rgba);
    ImGui::RegisterUserTexture(t);
    p.tex = t;
    return t;
}
// A picture fitted in a rectangle (the editor's window: its tiles)
void pic_in_rect(ImDrawList *dl, ImTextureData *t, const Rect &b, float a) {
    float bw = b.x1 - b.x0, bh = b.y1 - b.y0, k = std::min(bw / (float)t->Width, bh / (float)t->Height);
    float w = t->Width * k, h = t->Height * k, x0 = b.x0 + (bw - w) * .5f, y0 = b.y0 + (bh - h) * .5f;
    dl->AddImage(t->GetTexRef(), ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), ImVec2(0, 0), ImVec2(1, 1), fade(IM_COL32(255, 255, 255, 255), a));
}
// The press on a picture: pressed, 8 % smaller and 25 % darker; on (a lock-on on, the touch screen shown, a run going
// on, a lock locked), pulsing a little larger (the MHGU port's the same)
const float PIC_PRESSED = .92f;
const ImU32 PIC_DARK = IM_COL32(191, 191, 191, 255);
float pic_on_scale() { return 1.0f + .06f * (.5f + .5f * sinf(SDL_GetTicksNS() / 1e9f * 4)); }
// A control's picture alone, whole, fitted in a box of half sizes (hw, hh) about c and moved by (ox, oy)
void pic_alone(ImDrawList *dl, ImTextureData *t, ImVec2 c, float hw, float hh, float a, bool pressed, bool on, float ox = 0, float oy = 0) {
    float k = std::min(hw * 2 / (float)t->Width, hh * 2 / (float)t->Height) * (pressed ? PIC_PRESSED : on ? pic_on_scale() : 1.0f);
    float w = t->Width * k * .5f, h = t->Height * k * .5f;
    c.x += ox, c.y += oy;
    dl->AddImage(t->GetTexRef(), ImVec2(c.x - w, c.y - h), ImVec2(c.x + w, c.y + h), ImVec2(0, 0), ImVec2(1, 1),
                 fade(pressed ? PIC_DARK : IM_COL32(255, 255, 255, 255), a));
}
// The attack pill's picture along the pill (its width along PILL_U, the A end up and to the right), fitted in its length
// and thickness; the half pressed (the upper A, the lower X) darker
void pic_pill(ImDrawList *dl, ImTextureData *t, Geo g, int mask, float a) {
    float len = 2 * (g.r * PILL_HALF + g.r), thick = 2 * g.r;
    float k = std::min(len / (float)t->Width, thick / (float)t->Height), hl = t->Width * k * .5f, ht = t->Height * k * .5f;
    ImVec2 u = PILL_U, n(-PILL_U.y, PILL_U.x);  // (the picture's x along u, its y down along n: turned, not mirrored)
    auto at = [&](float s, float d) { return ImVec2(g.c.x + u.x * s + n.x * d, g.c.y + u.y * s + n.y * d); };
    for (int half = 0; half < 2; half++) {  // (0 the lower, X: the picture's left half; 1 the upper, A)
        float s0 = half ? 0 : -hl, s1 = half ? hl : 0, u0 = half ? .5f : 0, u1 = half ? 1 : .5f;
        bool on = mask & (half ? 1 : 2);
        dl->AddImageQuad(t->GetTexRef(), at(s0, -ht), at(s1, -ht), at(s1, ht), at(s0, ht), ImVec2(u0, 0), ImVec2(u1, 0),
                         ImVec2(u1, 1), ImVec2(u0, 1), fade(on ? PIC_DARK : IM_COL32(255, 255, 255, 255), a));
    }
}
// A picture for control i of layout l, when it has one that can be drawn (the stick's base: its own; its knob apart)
ImTextureData *pic_of(const Layout &l, int i) { return image_ok(i) && l.image[i][0] ? pic_texture(l.image[i]) : nullptr; }
// The d-pads' and the menu pad's press on their picture: moved a little toward the direction pressed, darker
void pic_pad(ImDrawList *dl, ImTextureData *t, Geo g, int part, float a) {
    float ox = 0, oy = 0;
    for (int d = 0; d < 4; d++)
        if (part & (1 << d)) ox += DPAD_DIR[d][0] * g.r * .06f, oy += DPAD_DIR[d][1] * g.r * .06f;
    pic_alone(dl, t, g.c, g.r, g.r, a, part != 0, false, ox, oy);
}
// The mark of a button a tap holds (Layout.hold): a small padlock at its upper left, closed and lit while it is held
void hold_mark(ImDrawList *dl, Geo g, bool held, float a) {
    ImVec2 c(g.c.x - g.r * .72f, g.c.y - g.r * .72f);
    float s = std::max(g.r * .34f, 6.0f);
    dl->AddCircleFilled(c, s * .62f, fade(IM_COL32(18, 12, 9, 255), a), 20);
    dl->AddCircle(c, s * .62f, fade(held ? GLOW : GOLD, a), 20, s * .08f);
    padlock(dl, ImVec2(c.x, c.y - s * .04f), s * .62f, held, fade(held ? GLOW : INK, a), fade(DEEP, a));
}
// The sprint lock's circle, or the player's picture for it alone (armed: pressed; locked: on)
void lock_draw(ImDrawList *dl, const Layout &l, ImVec2 c, float cr, float a, bool armed, bool locked) {
    if (a <= 0.0f) return;
    if (ImTextureData *t = pic_of(l, C_RUNLOCK)) pic_alone(dl, t, c, cr, cr, a, armed && !locked, locked);
    else lock_circle(dl, c, cr, a, armed, locked);
}
// The stick with the player's pictures: its base's (Layout.image) and its knob's (Layout.knob) each alone in its place
// (the knob a little larger while it runs), or the port's own base or knob where it has none
void stick_of(ImDrawList *dl, const Layout &l, ImVec2 base, ImVec2 knob, float r, float a, bool on, bool running) {
    ImTextureData *tb = pic_of(l, C_STICK), *tk = l.knob[0] ? pic_texture(l.knob) : nullptr;
    if (!tb && !tk) {
        stick(dl, base, knob, r, a, on);
        return;
    }
    float kr = r * KNOB;
    if (tb) pic_alone(dl, tb, base, r, r, a, false, false);
    else stick_base(dl, base, r, a, on);
    if (tk) pic_alone(dl, tk, knob, kr, kr, a, false, running);
    else stick_knob(dl, knob, kr, a, on);
}
// What a control's part lights when pressed (control_face's mask): the pill's halves (both at the seam), the band's
// attacks and guard, a d-pad's or the menu pad's directions; any other control, its face
int part_mask(int i, int part) {
    return i == C_ATTACK ? (part == 2 ? 3 : 1 << part) : i == C_CORNER ? (part == 2 ? 3 : part == 3 ? 4 : 1 << part)
         : is_dpad(i) || i == C_MPAD ? part : 1;
}
void control_face(ImDrawList *dl, ImFont *font, const Layout &l, int i, float w, float h, const State &s, float a) {
    Geo g = place(l, i, w, h);
    bool held = (s.latched >> i & 1) != 0;  // (a tap holds it down: lit)
    if (i == C_RUNLOCK) {  // (the editor's: in a hunt it shows as the thumb pushes toward it, with the stick)
        lock_draw(dl, l, g.c, g.r, a * .8f, false, false);
        return;
    }
    if (i == C_STICK) {
        if ((s.stick_on || s.run_free) && !s.edit) {
            // (a locked run going on by itself: the knob held at the rim, forward)
            ImVec2 base(s.bx, s.by), knob = s.stick_on ? ImVec2(s.kx, s.ky) : ImVec2(s.bx, s.by - g.r);
            stick_of(dl, l, base, knob, g.r, a, true, s.run);
            // the sprint lock's circle: faded in as the thumb pushes up past the rim while a lock could start; lit with
            // the thumb on it (armed) and locked
            // (toward the circle, wherever the editor put it: above the stick unless moved)
            float v = s.run_lock || s.run_over ? 1.0f : 0.0f;
            const Geo lg = lock_geo(l, s, w, h);
            if (!s.run_lock && !s.run_over && s.run_can && s.stick_on) {
                float ux = lg.c.x - s.bx, uy = lg.c.y - s.by, ud = std::max(1.0f, hypotf(ux, uy));
                float dx = s.fx - s.bx, dy = s.fy - s.by, d = hypotf(dx, dy), up = d > 1.0f ? (dx * ux + dy * uy) / (d * ud) : 0.0f;
                float reach = std::max(1.0f, ud - lg.r - .9f * g.r);
                v = std::clamp((d - .9f * g.r) / reach, 0.0f, 1.0f) * std::clamp((up - LOCK_UP) / .15f, 0.0f, 1.0f);
            }
            lock_draw(dl, l, lg.c, lg.r, a * v, s.run_over, s.run_lock);
            // (running: a gold ring round the rim, and streaks behind the knob; not round nor behind a picture)
            if (s.run && !pic_of(l, C_STICK)) dl->AddCircle(base, g.r * 1.12f, fade(GLOW, a * .85f), 48, g.r * .07f);
            if (s.run && !(l.knob[0] && pic_texture(l.knob))) {
                float dx = knob.x - s.bx, dy = knob.y - s.by, n = std::max(1.0f, hypotf(dx, dy));
                dx /= n, dy /= n;
                for (int k = -1; k <= 1; k++) {
                    ImVec2 o(knob.x - dy * k * g.r * .22f, knob.y + dx * k * g.r * .22f);
                    dl->AddLine(ImVec2(o.x - dx * g.r * .55f, o.y - dy * g.r * .55f), ImVec2(o.x - dx * g.r * .95f, o.y - dy * g.r * .95f),
                                fade(GLOW, a * (k ? .5f : .8f)), g.r * .06f);
                }
            }
        } else {
            stick_of(dl, l, g.c, g.c, g.r, a * .8f, false, false);
        }
        return;
    }
    int mask = 0;
    ImVec2 thumb = g.c;  // (the menu pad's knob: under its finger, inside the rim)
    for (const Finger &f : s.f)
        if (f.kind == F_BUTTON && !f.done) {
            if (f.ctl == i) mask |= part_mask(i, f.part);
            if (i == C_MPAD && f.ctl == i && !s.edit) {
                float dx = f.x - g.c.x, dy = f.y - g.c.y, d = hypotf(dx, dy), m = g.r * (1 - KNOB * .5f);
                thumb = d > m ? ImVec2(g.c.x + dx / d * m, g.c.y + dy / d * m) : ImVec2(f.x, f.y);
            }
            if (i == C_HEAVY && f.ctl == C_LIGHT && f.part) mask |= 1;  // (the seam between them: both lit)
            if (f.kept_ctl >= 0) {  // (a button slid from, kept pressed: show_slide_both)
                if (f.kept_ctl == i) mask |= part_mask(i, f.kept_part);
                if (i == C_HEAVY && f.kept_ctl == C_LIGHT && f.kept_part) mask |= 1;
            } else if (f.keep & BTN_R) {
                mask |= i == C_R ? 1 : i == C_CORNER && f.ctl == C_CORNER ? 4 : 0;  // (a guard kept: show_guard_slide)
            }
        }
    if (of_mpad(i) && s.lit_ctl == i && SDL_GetTicksNS() < s.lit_until) mask |= i == C_MPAD ? s.lit_part : 1;  // (a tap)
    if (held && !s.edit) mask |= 1;
    if (ImTextureData *pic = pic_of(l, i)) {  // (the player's picture alone, the press on it)
        bool on = (i == C_SCREEN && g_draw_bottom) || (i == C_RUN && s.run) ||
                  (i == C_LOCKON && !s.edit && lock_on_allowed() && Port::Hooks::LockOn());
        if (i == C_RUN && g_game.weapon_drawn == 1 && !s.edit) a *= .45f, mask = 0;  // (the weapon out: dimmed, as its own)
        if (i == C_ATTACK) pic_pill(dl, pic, g, mask, a);
        else if (i == C_MPAD || is_dpad(i)) pic_pad(dl, pic, g, mask, a);
        else if (i == C_CORNER) {  // (fitted in the band's whole place round the corner)
            Rect b = bounds(l, i, w, h);
            pic_alone(dl, pic, ImVec2((b.x0 + b.x1) * .5f, (b.y0 + b.y1) * .5f), (b.x1 - b.x0) * .5f, (b.y1 - b.y0) * .5f, a, mask != 0, false);
        } else pic_alone(dl, pic, g.c, g.r, g.r, a, mask != 0, on);
        return;
    }
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
        std::string own = is_combo(i) ? combo_text(g_pub_combo[i - C_COMBO]) : "";
        const char *label = is_combo(i) ? (own.empty() ? "+" : own.c_str()) : DEFS[i].letter;
        float r = mask ? g.r * .95f : g.r, fs = r * (is_combo(i) ? .75f : i >= C_ML ? .9f : 1.05f);
        disc(dl, g.c, r, mask != 0, a);
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, label);
        if (ts.x > r * 1.55f) fs *= r * 1.55f / ts.x, ts = font->CalcTextSizeA(fs, FLT_MAX, 0, label);  // (inside the disc)
        dl->AddText(font, fs, ImVec2(g.c.x - ts.x * .5f + fs * .04f, g.c.y - ts.y * .5f + fs * .05f), fade(IM_COL32(0, 0, 0, 255), a * .7f), label);
        dl->AddText(font, fs, ImVec2(g.c.x - ts.x * .5f, g.c.y - ts.y * .52f), fade(mask ? GLOW : INK, a), label);
        return;
    }
    if (i == C_RUN && g_game.weapon_drawn == 1 && !s.edit) a *= .45f, mask = 0;  // (the weapon out: dimmed, it presses nothing)
    float r = mask ? g.r * .95f : g.r;
    bool lit = mask != 0 || (i == C_SCREEN && g_draw_bottom);
    disc(dl, g.c, r, lit, a);
    if (i == C_RUN && s.run) dl->AddCircle(g.c, g.r * 1.16f, fade(GLOW, a * (.6f + .3f * sinf(SDL_GetTicksNS() / 1e9f * 4))), 40, g.r * .09f);
    Icon ic = DEFS[i].icon;
    char letter[8];
    snprintf(letter, sizeof letter, "%s", DEFS[i].letter);
    if (i == C_TOWN) snprintf(letter, sizeof letter, "%s", T("TOWN"));  // (a plate under it, as START's: what it is)
    if (i == C_ZR && g_draw_skip) ic = I_SKIP;  // (once a quest is complete, ZR taps the touch screen's Skip)
    if (is_ranged(i)) ranged_face(i, s.edit ? 1 : g_game.weapon_drawn, &ic, letter, sizeof letter);
    // (the lock-on, as the MHGU port's: a ring round its button while it is on, pulsing while the camera follows the
    // monster, the button lit)
    bool locked = i == C_LOCKON && !s.edit && lock_on_allowed() && Port::Hooks::LockOn();
    if (locked) {
        float pulse = Port::Hooks::LockOnFollowing() ? .6f + .3f * sinf(SDL_GetTicksNS() / 1e9f * 4) : .45f;
        dl->AddCircle(g.c, r * 1.16f, fade(GLOW, a * pulse), 40, r * .09f);
    }
    icon(dl, ic, g.c, i == C_RFIRE ? r * .94f : r, fade(mask || locked || (i == C_SCREEN && g_draw_bottom) ? GLOW : INK, a),
         fade(DEEP, a));
    float br = std::max(r * (i == C_RFIRE ? .22f : .3f), .021f * h);  // (the large one's as large as the others')
    if (strlen(letter) > 3) badge(dl, font, ImVec2(g.c.x, g.c.y + r * 1.02f), br, letter, a);  // (START, SELECT: a plate below)
    else badge(dl, font, ImVec2(g.c.x + r * .7f, g.c.y + r * .7f), br, letter, a);
}

// A control, and over it the mark of a button a tap holds: in the editor, and while it is held
// (a picture of the player's: its mark in the editor alone, the picture shows it held by staying pressed)
void control(ImDrawList *dl, ImFont *font, const Layout &l, int i, float w, float h, const State &s, float a) {
    control_face(dl, font, l, i, w, h, s, a);
    bool held = (s.latched >> i & 1) != 0;
    if ((l.hold >> i & 1) && holdable(i) && (s.edit || (held && !pic_of(l, i)))) hold_mark(dl, place(l, i, w, h), held && !s.edit, a);
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

// A corner handle that sizes what it is on (the monsters' health bars'): a square with a double arrow across it, from
// its bottom left to its top right, in the grips' look
void size_grip(ImDrawList *dl, const Rect &r, ImU32 col) {
    ImVec2 c((r.x0 + r.x1) * .5f, (r.y0 + r.y1) * .5f);
    float s = std::min(r.x1 - r.x0, r.y1 - r.y0) * .5f, a = s * .7f, hw = s * .3f, t = s * .13f;
    dl->AddRectFilled(ImVec2(r.x0, r.y0), ImVec2(r.x1, r.y1), fade(DEEP, .9f), s * .25f);
    dl->AddRect(ImVec2(r.x0, r.y0), ImVec2(r.x1, r.y1), fade(col, .75f), s * .25f, s * .08f);
    const float q = .70710678f;
    for (int i = 0; i < 2; i++) {
        ImVec2 d = i ? ImVec2(-q, q) : ImVec2(q, -q), n(-d.y, d.x);
        ImVec2 tip(c.x + d.x * a, c.y + d.y * a), base(c.x + d.x * (a - hw * 1.6f), c.y + d.y * (a - hw * 1.6f));
        dl->AddLine(c, base, col, t);
        dl->AddTriangleFilled(tip, ImVec2(base.x + n.x * hw, base.y + n.y * hw), ImVec2(base.x - n.x * hw, base.y - n.y * hw), col);
    }
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

// The windows over the editor (its Profile and Picture buttons), in the game's window style: a plate, a serif title, the
// tiles, a line saying what happened or what a second tap does, the buttons (lit while held). The MHGU port's the same
void window_frame(ImDrawList *dl, ImFont *title, const Rect &win, float w, float h, const char *text) {
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(w, h), IM_COL32(0, 0, 0, 110));
    plate(dl, win, .005f * h, 1.0f, false);
    text_center(dl, title, .045f * h, ImVec2((win.x0 + win.x1) * .5f, win.y0 + .05f * h), GLOW, text);
}
void window_line(ImDrawList *dl, ImFont *font, const Rect &win, float y, float h, const char *t, ImU32 col) {
    float fs = .026f * h, room = (win.x1 - win.x0) * .94f, tw = font->CalcTextSizeA(fs, FLT_MAX, 0, t).x;
    if (tw > room) fs *= room / tw;
    text_center(dl, font, fs, ImVec2((win.x0 + win.x1) * .5f, y), col, t);
}
void window_button(ImDrawList *dl, ImFont *font, const Rect &b, float h, const char *t, bool lit, bool off, bool armed) {
    plate(dl, b, .0035f * h, off ? .45f : 1.0f, lit && !off);
    if (armed) dl->AddRect(ImVec2(b.x0 - .003f * h, b.y0 - .003f * h), ImVec2(b.x1 + .003f * h, b.y1 + .003f * h),
                           fade(RED, .65f + .35f * sinf(SDL_GetTicksNS() / 1e9f * 6)), 0, .005f * h);
    button_text(dl, font, b, .03f * h, fade(armed ? GLOW : INK, off ? .45f : 1.0f), t);
}
void draw_profiles(ImDrawList *dl, ImFont *font, ImFont *title, const State &s, float w, float h) {
    Rect win, slot[PROFILES_N], btn[PA_COUNT];
    float say_y;
    prof_window(w, h, &win, slot, btn, &say_y);
    window_frame(dl, title, win, w, h, T("Control profiles"));
    for (int i = 0; i < PROFILES_N; i++) {
        const Rect &r = slot[i];
        bool pick = i == s.prof_pick, used = s.prof_used[i], use = i + 1 == s.open_slot, editing = i + 1 == s.layout.profile;
        plate(dl, r, .0035f * h, used || editing ? 1.0f : .6f, pick);
        if (pick) dl->AddRect(ImVec2(r.x0 - .004f * h, r.y0 - .004f * h), ImVec2(r.x1 + .004f * h, r.y1 + .004f * h),
                              fade(GLOW, .65f + .35f * sinf(SDL_GetTicksNS() / 1e9f * 6)), 0, .006f * h);
        char num[8];
        snprintf(num, sizeof num, "%d", i + 1);
        float cx = (r.x0 + r.x1) * .5f, rh = r.y1 - r.y0;
        text_center(dl, font, rh * .34f, ImVec2(cx, r.y0 + rh * .3f), pick ? GLOW : used || editing ? GOLD : fade(INK, .5f), num);
        int kind = editing ? s.layout.kind : s.prof_kind[i];
        const char *what = used || editing ? T(kind == KIND_EMU ? "Emulator style" : "Ours") : T("Empty");
        float fs = rh * .13f, tw = font->CalcTextSizeA(fs, FLT_MAX, 0, what).x, room = (r.x1 - r.x0) * .9f;
        text_center(dl, font, tw > room ? fs * room / tw : fs, ImVec2(cx, r.y0 + rh * .63f), used || editing ? INK : fade(INK, .55f), what);
        const char *tag = editing ? T("Editing") : use ? T("In use") : nullptr;
        if (tag) {
            float ts = rh * .12f, tww = font->CalcTextSizeA(ts, FLT_MAX, 0, tag).x;
            if (tww > room) ts *= room / tww;
            text_center(dl, font, ts, ImVec2(cx, r.y0 + rh * .84f), GLOW, tag);
        }
    }
    window_line(dl, font, win, say_y, h, s.prof_say, s.prof_arm >= 0 ? GLOW : INK);
    const char *const LABEL[PA_COUNT] = {"Load", "Save here", "New: ours", "New: emulator style", "Delete", "Close"};
    int n = s.prof_pick + 1;
    bool used = s.prof_used[s.prof_pick];
    for (int i = 0; i < PA_COUNT; i++) {
        bool off = (i == PA_LOAD && (!used || n == s.layout.profile)) || (i == PA_DELETE && (!used || n == s.open_slot || n == s.layout.profile));
        window_button(dl, font, btn[i], h, T(LABEL[i]), s.prof_btn == i, off, s.prof_arm == i);
    }
}
void draw_pictures(ImDrawList *dl, ImFont *font, ImFont *title, const State &s, float w, float h) {
    Rect win, tile[IMG_PAGE], btn[IA_COUNT];
    float say_y;
    img_window(w, h, &win, tile, btn, &say_y);
    int c = s.sel >= 0 && s.sel < C_COUNT ? s.sel : -1;
    char head[96];
    snprintf(head, sizeof head, T("Picture for: %s"), c < 0 ? "" : c == C_STICK && s.img_knob ? T("the stick's knob")
                                                     : c == C_MPAD && s.img_select ? T("the menu pad's SELECT")
                                                     : is_combo(c) ? T("Combined button") : T(DEFS[c].name));
    window_frame(dl, title, win, w, h, head);
    for (int i = 0; i < IMG_PAGE; i++) {
        int k = s.img_page * IMG_PAGE + i;
        const Rect &r = tile[i];
        if (k >= s.img_n) {
            plate(dl, r, .0035f * h, .35f, false);
            continue;
        }
        bool on = c >= 0 && !strcmp(img_target(s, c), s.img_names[k].n);
        plate(dl, r, .0035f * h, 1.0f, on);
        Rect in = {r.x0 + .012f * h, r.y0 + .012f * h, r.x1 - .012f * h, r.y1 - .04f * h, true};
        if (ImTextureData *t = pic_texture(s.img_names[k].n)) pic_in_rect(dl, t, in, 1.0f);
        else text_center(dl, font, .024f * h, ImVec2((r.x0 + r.x1) * .5f, (in.y0 + in.y1) * .5f), fade(INK, .6f), "?");
        float fs = .018f * h, tw = font->CalcTextSizeA(fs, FLT_MAX, 0, s.img_names[k].n).x, room = (r.x1 - r.x0) * .92f;
        text_center(dl, font, tw > room ? fs * room / tw : fs, ImVec2((r.x0 + r.x1) * .5f, r.y1 - .02f * h), on ? GLOW : INK, s.img_names[k].n);
    }
    char line[200];
    int pages = std::max(1, (s.img_n + IMG_PAGE - 1) / IMG_PAGE);
    if (!s.img_n) snprintf(line, sizeof line, "%s", T("No pictures yet: Add pictures copies yours in (PNG or JPG). They are your own responsibility."));
    else snprintf(line, sizeof line, T("Page %d of %d. The pictures you add are your own responsibility."), s.img_page + 1, pages);
    window_line(dl, font, win, say_y, h, line, INK);
    const char *const LABEL[IA_COUNT] = {"No picture", "<", ">",
                                         c == C_MPAD ? (s.img_select ? "For: SELECT" : "For: pad")
                                                     : c == C_STICK && s.img_knob ? "For: knob" : "For: base",
                                         "Add pictures", "Close"};
    for (int i = 0; i < IA_COUNT; i++) {
        bool off = (i == IA_NONE && (c < 0 || !img_target(s, c)[0])) || ((i == IA_PREV || i == IA_NEXT) && pages < 2) ||
                   (i == IA_KNOB && !img_has_part(c));
        window_button(dl, font, btn[i], h, T(LABEL[i]), s.img_btn == i, off, false);
    }
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
        if (k == HP_ITEMS) {  // (the item bar's touch area round it: a tap there uses the item, a swipe turns the bar)
            Rect z = zone_round(r.x0, r.y0, r.x1, r.y1, s.layout.item_zone);
            dl->AddRectFilled(ImVec2(z.x0, z.y0), ImVec2(z.x1, z.y1), sel ? IM_COL32(120, 190, 90, 46) : IM_COL32(120, 190, 90, 26), .01f * h);
            dl->AddRect(ImVec2(z.x0, z.y0), ImVec2(z.x1, z.y1), sel ? IM_COL32(150, 220, 110, 220) : IM_COL32(120, 190, 90, 150), .01f * h,
                        (sel ? .004f : .0025f) * h);
        }
        bool hid = s.hud_at[k][3] != 0;  // (1.0.2: hidden, framed faint)
        dl->AddRectFilled(ImVec2(f.x0, f.y0), ImVec2(f.x1, f.y1), sel ? IM_COL32(255, 214, 92, 40) : IM_COL32(222, 186, 96, hid ? 10 : 22));
        dashed_rect(dl, f, sel ? fade(GLOW, .65f + .35f * sinf(pulse_t * 6)) : IM_COL32(222, 186, 96, hid ? 110 : 210), (sel ? .005f : .0035f) * h);
        char own[96];
        if (sel) snprintf(own, sizeof own, "%s%s%s  %d%%", T(HUD_NAME[k]), hid ? " " : "", hid ? T("(hidden)") : "",
                          (int)lroundf(s.hud_at[k][2] * 100));
        else snprintf(own, sizeof own, "%s%s%s", T(HUD_NAME[k]), hid ? " " : "", hid ? T("(hidden)") : "");
        const char *t = own;
        float fs = .022f * h, tw = font->CalcTextSizeA(fs, FLT_MAX, 0, t).x;
        ImVec2 c((f.x0 + f.x1) * .5f, f.y1 + fs * .9f);
        if (c.y + fs > h) c.y = f.y0 - fs * .9f;
        c.x = std::clamp(c.x, tw * .5f + fs * .6f, w - tw * .5f - fs * .6f);
        dl->AddRectFilled(ImVec2(c.x - tw * .5f - fs * .35f, c.y - fs * .62f), ImVec2(c.x + tw * .5f + fs * .35f, c.y + fs * .62f),
                          IM_COL32(28, 20, 15, 190), fs * .3f);
        text_center(dl, font, fs, c, sel ? GLOW : INK, t, w);
        used.push_back(label_rect(c, tw + fs * .7f, fs * 1.24f));
        if (k == HP_ITEMS && sel) {  // (the item bar's touch area's size, above its name: 1.0.2, its own size is in the name)
            char pct[48];
            snprintf(pct, sizeof pct, "%s %d%%", T("Touch area"), (int)lroundf(s.layout.item_zone * 100));
            float ty = c.y < f.y0 ? c.y - fs * 1.5f : std::max(f.y0 - fs * .9f, fs);
            text_center(dl, font, fs, ImVec2(c.x, ty), IM_COL32(150, 220, 110, 255), pct, w);
        }
    }
    bool hud_only = false;  // (the PC's editor without the touch controls: none of them drawn)
#ifndef __ANDROID__
    hud_only = g_edit_hud;
#endif
    // (the floating stick: how far from its place a thumb brings it, dashed)
    if (!hud_only && floating_stick() && in_mode(C_STICK, false)) {
        Geo g = place(s.layout, C_STICK, w, h);
        float reach = stick_reach(g, h);
        for (int k = 0; k < 48; k += 2)
            dl->PathArcTo(g.c, reach, k * PI / 24, (k + 1) * PI / 24, 4), dl->PathStroke(fade(GOLD, .55f), .003f * h);
        text_center(dl, font, .02f * h, ImVec2(g.c.x, std::max(g.c.y - reach - .02f * h, .02f * h)), fade(GOLD, .8f), T("Floating stick"), w);
    }
    for (int i = 0; i < C_LAID && !hud_only; i++) {
        if (!in_mode(i, false)) continue;  // (the controls not in this layout stay in the file only)
        bool hid = hideable(i) && (s.layout.off >> i & 1);  // (the emulator-style layout's, hidden: faded)
        control(dl, font, s.layout, i, w, h, s, hid ? .35f : 1.0f);
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
        char own[64];
        snprintf(own, sizeof own, "%s%s%s", T(DEFS[i].name), hid ? " " : "", hid ? T("(hidden)") : "");
        const char *t = own;
        float fs = .024f * h, side = DEFS[i].right ? -1.0f : 1.0f;
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, t);
        float drop = i == C_MENU || i == C_HSELECT || i == C_ESTART || i == C_ESELECT ? g.r * .5f : 0;  // (START's and SELECT's plates below them)
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
    Rect fps;  // (the FPS counter, while it shows: framed, named, picked like a part)
    if (fps_frame(&fps)) {
        bool sel = s.sel == SEL_FPS;
        Rect f = {fps.x0 - .006f * h, fps.y0 - .006f * h, fps.x1 + .006f * h, fps.y1 + .006f * h, true};
        dashed_rect(dl, f, sel ? fade(GLOW, .65f + .35f * sinf(SDL_GetTicksNS() / 1e9f * 6)) : IM_COL32(222, 186, 96, 210), (sel ? .005f : .0035f) * h);
        float fs = .022f * h, ny = f.y1 + fs * .8f > h ? f.y0 - fs * .8f : f.y1 + fs * .8f;
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, T("FPS counter"));
        ImVec2 c(std::clamp((f.x0 + f.x1) * .5f, ts.x * .5f + fs * .4f, w - ts.x * .5f - fs * .4f), ny);
        dl->AddRectFilled(ImVec2(c.x - ts.x * .5f - fs * .35f, c.y - fs * .62f), ImVec2(c.x + ts.x * .5f + fs * .35f, c.y + fs * .62f),
                          IM_COL32(28, 20, 15, 190), fs * .3f);
        text_center(dl, font, fs, c, sel ? GLOW : INK, T("FPS counter"), w);
    }
    if (chat_bubble_on()) {  // (1.0.2: the chat's button: a bubble in a dashed ring, named, picked like a part; its size)
        const bool sel = s.sel == SEL_CHAT;
        const Geo g = chat_geo(s.layout, w, h);
        dl->AddCircleFilled(g.c, g.r, IM_COL32(28, 20, 15, 215), 40);
        dl->AddCircle(g.c, g.r, IM_COL32(112, 76, 34, 255), 40, .004f * h);
        const float bw = g.r * .95f, bh = g.r * .62f;
        const ImVec2 a(g.c.x - bw * .5f, g.c.y - bh * .62f), b(g.c.x + bw * .5f, g.c.y + bh * .38f);
        dl->AddRectFilled(a, b, IM_COL32(244, 232, 200, 255), bh * .35f);
        dl->AddTriangleFilled(ImVec2(g.c.x - bw * .22f, b.y - 1), ImVec2(g.c.x - bw * .34f, b.y + bh * .38f), ImVec2(g.c.x + bw * .02f, b.y - 1),
                              IM_COL32(244, 232, 200, 255));
        dl->AddCircle(g.c, g.r * 1.18f, sel ? fade(GLOW, .65f + .35f * sinf(SDL_GetTicksNS() / 1e9f * 6)) : IM_COL32(222, 186, 96, 210), 40,
                      (sel ? .005f : .0035f) * h);
        char label[64];
        if (sel) snprintf(label, sizeof label, "%s  %d%%", T("Chat button"), (int)lroundf(s.layout.chat_k * 100));
        else snprintf(label, sizeof label, "%s", T("Chat button"));
        float fs = .022f * h, ny = g.c.y + g.r * 1.18f + fs * .8f > h ? g.c.y - g.r * 1.18f - fs * .8f : g.c.y + g.r * 1.18f + fs * .8f;
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, label);
        ImVec2 c(std::clamp(g.c.x, ts.x * .5f + fs * .4f, w - ts.x * .5f - fs * .4f), ny);
        dl->AddRectFilled(ImVec2(c.x - ts.x * .5f - fs * .35f, c.y - fs * .62f), ImVec2(c.x + ts.x * .5f + fs * .35f, c.y + fs * .62f),
                          IM_COL32(28, 20, 15, 190), fs * .3f);
        text_center(dl, font, fs, c, sel ? GLOW : INK, label, w);
    }
    Rect items;  // (the quick items' piece or its stand-in: framed, named, picked like a part; its size when picked)
    if (items_frame(&items)) {
        bool sel = s.sel == SEL_ITEMS;
        Rect f = {items.x0 - .008f * h, items.y0 - .008f * h, items.x1 + .008f * h, items.y1 + .008f * h, true};
        dashed_rect(dl, f, sel ? fade(GLOW, .65f + .35f * sinf(SDL_GetTicksNS() / 1e9f * 6)) : IM_COL32(222, 186, 96, 210),
                    (sel ? .005f : .0035f) * h);
        char label[64];
        if (sel) snprintf(label, sizeof label, "%s  %d%%", T("Quick items"), (int)lroundf(s.layout.items_k * 100));
        else snprintf(label, sizeof label, "%s", T("Quick items"));
        Rect hg = items_grip(items, h);  // (its corner handle: its size)
        size_grip(dl, hg, sel ? GLOW : GOLD);
        float fs = .022f * h, ny = f.y0 - fs * .8f < 0 ? f.y1 + fs * .8f : f.y0 - fs * .8f;  // (above it: it sits low)
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, label);
        ImVec2 c(std::clamp((f.x0 + f.x1) * .5f, ts.x * .5f + fs * .4f, w - ts.x * .5f - fs * .4f), ny);
        dl->AddRectFilled(ImVec2(c.x - ts.x * .5f - fs * .35f, c.y - fs * .62f), ImVec2(c.x + ts.x * .5f + fs * .35f, c.y + fs * .62f),
                          IM_COL32(28, 20, 15, 190), fs * .3f);
        text_center(dl, font, fs, c, sel ? GLOW : INK, label, w);
    }
    Rect bars;  // (the monsters' health bars or their stand-in: framed, named, picked like a part; their size when picked)
    if (bars_frame(&bars)) {
        bool sel = s.sel == SEL_BARS;
        Rect f = {bars.x0 - .008f * h, bars.y0 - .008f * h, bars.x1 + .008f * h, bars.y1 + .008f * h, true};
        dashed_rect(dl, f, sel ? fade(GLOW, .65f + .35f * sinf(SDL_GetTicksNS() / 1e9f * 6)) : IM_COL32(222, 186, 96, 210),
                    (sel ? .005f : .0035f) * h);
        char label[64];
        if (sel) snprintf(label, sizeof label, "%s  %d%%", T("Monster health bar"), (int)lroundf(s.layout.bars_k * 100));
        else snprintf(label, sizeof label, "%s", T("Monster health bar"));
        Rect hg = bars_grip(bars, h);  // (their corner handle: their size)
        size_grip(dl, hg, sel ? GLOW : GOLD);
        float fs = .022f * h, ny = f.y1 + fs * .8f > h ? f.y0 - fs * .8f : f.y1 + fs * .8f;
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, label);
        float cx = std::max((f.x0 + f.x1) * .5f, hg.x1 + ts.x * .5f + fs * .6f);  // (clear of the handle)
        ImVec2 c(std::clamp(cx, ts.x * .5f + fs * .4f, w - ts.x * .5f - fs * .4f), ny);
        dl->AddRectFilled(ImVec2(c.x - ts.x * .5f - fs * .35f, c.y - fs * .62f), ImVec2(c.x + ts.x * .5f + fs * .35f, c.y + fs * .62f),
                          IM_COL32(28, 20, 15, 190), fs * .3f);
        text_center(dl, font, fs, c, sel ? GLOW : INK, label, w);
    }
    int ctl = s.sel >= 0 && s.sel < C_COUNT ? s.sel : -1, part = s.sel >= ZONE_SEL ? s.sel - ZONE_SEL : -1;
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
    text_center(dl, title, .036f * u, ImVec2((panel.x0 + panel.x1) * .5f, panel.y0 + .045f * u), GLOW, hud_only ? "HUD" : T("Controls and HUD"));
    if (picked) {
        char pct[16];
        snprintf(pct, sizeof pct, "%d%%", (int)lroundf(std::clamp(g_epanel.k, EPANEL_MIN, 1.0f) * 100));
        float pf = .03f * u, pw = font->CalcTextSizeA(pf, FLT_MAX, 0, pct).x;
        text_center(dl, font, pf, ImVec2(grips[1].x0 - .012f * u - pw * .5f, panel.y0 + .045f * u), GLOW, pct);
    }
    const char *say = T(picked              ? "The panel: drag its title to move it  -  pinch or use - / + for its size"
                        : s.sel == SEL_FPS  ? "The FPS counter: drag it to move it  -  Reset puts it back at the top"
                        : s.sel == SEL_BARS ? "The monsters' health bar: drag to move, its corner handle, a pinch or - / + for its size  -  Reset puts it back"
                        : s.sel == SEL_ITEMS ? "Quick items: drag to move, its corner handle, a pinch or - / + for its size  -  Reset puts it back"
                        : s.sel == SEL_CHAT ? "The chat button: drag to move, a pinch or - / + for its size  -  Reset puts it back"
                        : ctl >= 0 && hideable(ctl) && (s.layout.off >> ctl & 1) ? "Hidden: Show brings it back"
                        : part >= 0 && s.hud_at[part][3] != 0 ? "Hidden: Show brings it back"
                        : s.sel == ZONE_SEL + HP_ITEMS ? "Drag it where you want it  -  - / + for its size, a pinch for the area a tap uses it in"
                        : s.sel == ZONE_SEL + HP_MAP ? "Drag it where you want it  -  to see the map here, take the Map off your panels: Customize Panels (quest menu)"
                        : s.sel >= ZONE_SEL ? "Drag it where you want it in your hunts  -  pinch or use - / + for its size"
                        : s.sel == C_MPAD   ? "Shows while a menu of the game's is open - drag to move, pinch or use - / + for its size"
                        : s.sel >= 0        ? "Drag to move  -  pinch or use - / + for its size"
                                            : "Tap a control or a part of the game's screen to pick it, drag it to move it");
#ifndef __ANDROID__
    // (the PC: the mouse's wheel where a phone pinches; Esc cancels)
    if (picked) say = pc_say("The panel: drag its title to move it  -  the wheel or - / + for its size",
                             "El panel: arrastra su título para moverlo  -  la rueda o - / + para su tamaño");
    else if (s.sel == SEL_BARS) say = pc_say("The monsters' health bar: drag to move, the wheel or - / + for its size  -  Reset puts it back",
                                             "La barra de vida de los monstruos: arrastra para moverla, la rueda o - / + para su tamaño  -  Restablecer la devuelve");
    else if (s.sel == SEL_ITEMS) say = pc_say("Quick items: drag to move, the wheel or - / + for its size  -  Reset puts it back",
                                              "Objetos rápidos: arrastra para moverlos, la rueda o - / + para su tamaño  -  Restablecer los devuelve");
    else if (part >= 0 && s.hud_at[part][3] != 0) {}  // (a hidden part of the HUD: "Hidden: Show brings it back", as above)
    else if (s.sel == ZONE_SEL + HP_ITEMS) say = pc_say("Drag it where you want it  -  - / + for its size, the wheel for the area a click uses it in",
                                                        "Arrástrala adonde la quieras  -  - / + para su tamaño, la rueda para el área donde un clic la usa");
    else if (s.sel == ZONE_SEL + HP_MAP) {}  // (the map: how to see it on the main screen, as above)
    else if (part >= 0) say = pc_say("Drag it where you want it in your hunts  -  the wheel or - / + for its size",
                                     "Arrástralo adonde lo quieras en tus cacerías  -  la rueda o - / + para su tamaño");
    else if (s.sel >= 0 && s.sel < C_COUNT) say = pc_say("Drag to move  -  the wheel or - / + for its size", "Arrastra para moverlo  -  la rueda o - / + para su tamaño");
    else if (s.sel < 0 && hud_only) say = pc_say("Click a part of the HUD to pick it, drag it to move it, the wheel sizes what has a size  -  Esc cancels",
                                                 "Haz clic en una parte del HUD para elegirla, arrástrala para moverla, la rueda cambia el tamaño de lo que lo tiene  -  Esc cancela");
    else if (s.sel < 0) say = pc_say("Click a control or a part of the game's screen to pick it, drag it to move it  -  Esc cancels",
                                     "Haz clic en un control o una parte de la pantalla del juego para elegirlo, arrástralo para moverlo  -  Esc cancela");
#endif
    float ss = .022f * u, room = grips[1].x0 - grips[0].x1 - .024f * u, wide = font->CalcTextSizeA(ss, FLT_MAX, 0, say).x;
    if (wide > room) ss *= room / wide;  // (a narrow screen, a long sentence: on one line, smaller)
    text_center(dl, font, ss, ImVec2((panel.x0 + panel.x1) * .5f, panel.y0 + .08f * u), INK, say);
    static const char *const STYLE_LABEL[STYLE_COUNT] = {"Attacks: pill", "Attacks: buttons", "Attacks: corner"};
    bool shown_hidden = (ctl >= 0 && hideable(ctl) && (s.layout.off >> ctl & 1)) || (part >= 0 && s.hud_at[part][3] != 0);
    char prof_label[64];
    snprintf(prof_label, sizeof prof_label, T(s.layout.kind == KIND_EMU ? "Profile %d: emulator style" : "Profile %d: ours"), s.layout.profile);
    const char *const LABEL[E_COUNT] = {"-", "+", shown_hidden ? "Show" : "Hide", "Reset",
                                        ctl >= 0 && (s.layout.hold >> ctl & 1) ? "Hold: On" : "Hold: Off", "Picture", prof_label,
                                        ranged_layout() ? "Layout: ranged" : "Layout: melee", STYLE_LABEL[s.layout.style],
                                        "Defaults", "Cancel", "Save"};
    for (int i = 0; i < E_COUNT; i++) {
        if (!btn[i].ok) continue;
        bool bars_sel = s.sel == SEL_BARS || s.sel == SEL_ITEMS || s.sel == SEL_CHAT;  // (the bars, the quick items, the chat's button: sized and reset like a control)
        bool off = ((i == E_SMALLER || i == E_BIGGER) && ((s.sel < 0 && !picked && !bars_sel) || s.sel == SEL_FPS)) ||
                   (i == E_RESET && s.sel < 0 && !picked && s.sel != SEL_FPS && !bars_sel) || (i == E_HIDE && !hideable(ctl) && part < 0) ||
                   (i == E_HOLD && !holdable(ctl)) || (i == E_IMAGE && !image_ok(ctl)) || (i == E_STYLE && ranged_layout());
#ifndef __ANDROID__
        if (i == E_HIDE && part >= 0) off = false;  // (the PC: a part of the game's HUD hides)
#endif
        bool lit = false;
        for (const Finger &f : s.f)
            if (f.kind == F_MENU && f.ctl == i) lit = true;
        bool on = i == E_HOLD && ctl >= 0 && (s.layout.hold >> ctl & 1) && !off;  // (the switch on: lit)
        plate(dl, btn[i], line * .8f, off ? .45f : 1.0f, lit);
        button_text(dl, font, btn[i], (i < E_HIDE ? .05f : .028f) * u, fade(i == E_SAVE || on ? GLOW : INK, off ? .45f : 1.0f),
                    i == E_PROFILES ? prof_label : T(LABEL[i]));
    }
    // (what a profile's action did, under the panel for a while: above it at the bottom edge)
    if (SDL_GetTicksNS() < s.note_until) {
        float fs = .022f * h, tw = font->CalcTextSizeA(fs, FLT_MAX, 0, s.note).x, maxw = std::max((panel.x1 - panel.x0) * .96f, w * .6f);
        if (tw > maxw) fs *= maxw / tw, tw = maxw;
        ImVec2 c((panel.x0 + panel.x1) * .5f, panel.y1 + fs * 1.1f);
        if (c.y + fs * .75f > h) c.y = panel.y0 - fs * 1.1f;
        c.x = std::clamp(c.x, tw * .5f + fs * .5f, std::max(tw * .5f + fs * .5f, w - tw * .5f - fs * .5f));
        dl->AddRectFilled(ImVec2(c.x - tw * .5f - fs * .5f, c.y - fs * .75f), ImVec2(c.x + tw * .5f + fs * .5f, c.y + fs * .75f),
                          IM_COL32(28, 20, 15, 215), fs * .3f);
        text_center(dl, font, fs, c, GOLD, s.note);
    }
    if (s.prof_open) draw_profiles(dl, font, title, s, w, h);  // (a window over it: the profiles, the pictures)
    else if (s.img_open) draw_pictures(dl, font, title, s, w, h);
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

// The long press that hides or shows the controls (hide_finger): a thin gold ring filling round the finger from HIDE_RING
// on; and the line at the top once they hide (the first time ever, where to turn it off too), fading out
void hide_cues(ImDrawList *dl, ImFont *font, const State &s, float w, float h, uint64_t now) {
    float t = 0;
    int k = hide_finger(s, now, &t);
    float from = (float)HIDE_RING / (float)HIDE_HOLD;
    if (k >= 0 && t > from) {
        float p = (t - from) / (1 - from), r = .05f * h;
        ImVec2 c(s.f[k].x, s.f[k].y - r * .2f);
        dl->AddCircle(c, r, IM_COL32(0, 0, 0, 70), 40, .007f * h);
        dl->PathArcTo(c, r, -PI * .5f, -PI * .5f + p * 2 * PI, 40);
        dl->PathStroke(fade(GOLD, .55f + .35f * p), .005f * h);
    }
    if (now >= s.hide_note_until || !s.user_hidden) return;
    float left = (s.hide_note_until - now) / 1e9f, a = std::min(1.0f, left / .5f);
    const char *one = T("Touch controls hidden: long-press an empty spot to show them again.");
    const char *two = s.hide_note_first ? T("You can turn this off in Settings \u203a Controls \u203a Touch controls.") : nullptr;
    float fs = .026f * h, tw = font->CalcTextSizeA(fs, FLT_MAX, 0, one).x;
    if (two) tw = std::max(tw, font->CalcTextSizeA(fs, FLT_MAX, 0, two).x);
    if (tw > w * .9f) fs *= w * .9f / tw, tw = w * .9f;
    float lines = two ? 2.0f : 1.0f, ph = fs * (lines * 1.25f + .6f), y0 = .1f * h;
    Rect r = {(w - tw) * .5f - fs * .6f, y0, (w + tw) * .5f + fs * .6f, y0 + ph, true};
    dl->AddRectFilled(ImVec2(r.x0, r.y0), ImVec2(r.x1, r.y1), fade(IM_COL32(28, 20, 15, 210), a), fs * .3f);
    dl->AddRect(ImVec2(r.x0, r.y0), ImVec2(r.x1, r.y1), fade(GOLD, a * .6f), fs * .3f, .002f * h);
    float cy = y0 + fs * .3f + fs * .625f;
    text_center(dl, font, fs, ImVec2(w * .5f, cy), fade(INK, a), one);
    if (two) text_center(dl, font, fs, ImVec2(w * .5f, cy + fs * 1.25f), fade(GOLD, a), two);
}

void draw(State &s, float w, float h) {
    ImFont *font = ImGui::GetFont();
    ImFont *title = g_title_font ? g_title_font : font;
    ImDrawList *dl = ImGui::GetBackgroundDrawList();  // (under the app's own windows: the settings, later)
    g_draw_menu = s.menu, g_draw_bottom = g_bottom.ok, g_draw_skip = Port::Hooks::SkipOffered();
    g_draw_map_hidden = s.edit ? s.hud_at[HP_MAP][3] != 0 : g_game.hud[HP_MAP].hidden != 0;
    pics_sweep();  // (the players' pictures no longer shown given back)
    if (s.edit) {
        draw_editor(dl, font, title, s, w, h);
        return;
    }
    uint64_t now = SDL_GetTicksNS();
    float idle = s.last_ns ? (now - s.last_ns) / 1e9f : 99.0f;
    float a = g_set.opacity * (idle > 5.0f ? .65f : 1.0f);  // (dimmer after a while untouched)
    Rect target = target_zone(s.menu);
    if (!s.visible) {  // (hidden: the settings button alone, a little fainter; the lock-on's ring, opaque as the HUD. Hidden
                       // by a long press: nothing of the port's but its cues)
        if (s.gear_only && !covered(s.layout, C_SETTINGS, w, h)) control(dl, font, s.layout, C_SETTINGS, w, h, s, a * .75f);
        if (target.ok && !s.user_hidden) target_hint(dl, target, h, 1.0f, now);
        hide_cues(dl, font, s, w, h, now);
        return;
    }
    mpad_fade(s, now);
    const Rect menus = menus_area();  // (the game's menus on the top screen: what is over them is drawn faint)
    for (int i = 0; i < C_COUNT; i++) {
        if (of_mpad(i) || !in_mode(i, s.menu) || covered(s.layout, i, w, h)) continue;
        // (in a menu of the game's the player's buttons stay, faded so its window reads and taps under them; what the
        // menu pad's group covers goes as the pad comes in, and comes back as it goes)
        float k = s.menu && i != C_MY ? .5f : 1.0f;
        if (g_mpad_a > 0 && under_mpad(s.layout, i, w, h)) k *= 1 - g_mpad_a;
        if (over_menus(menus, s, i, w, h)) k *= MENU_GHOST;
        if (k > .01f) control(dl, font, s.layout, i, w, h, s, a * k);
    }
    if (g_mpad_a > 0 && show(TB_MDPAD))  // (the menu pad's group, over the rest)
        for (int i : {C_MPAD, C_ML, C_MR, C_MSELECT})
            if ((i == C_MPAD || mpad_member(i)) && !covered(s.layout, i, w, h))
                control(dl, font, s.layout, i, w, h, s, a * g_mpad_a * (over_menus(menus, s, i, w, h) ? MENU_GHOST : 1.0f));
    Rect item = item_zone(s.menu);
    if (item.ok) item_hint(dl, s, item, h, a, now);
    if (target.ok) target_hint(dl, target, h, a, now);
    hide_cues(dl, font, s, w, h, now);
}

}  // namespace

void touch_init(SDL_Renderer *renderer, const std::string &data_dir) {
    g_renderer = renderer;
    if (!data_dir.empty()) g_layout_path = data_dir + "/touch_layout.txt", g_data_dir = data_dir;
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
#ifndef __ANDROID__
    {  // (the PC: a window resized - the fingers' and the mouse's places in its new size at once, not at the next frame)
        int w = 0, h = 0;
        if (g_renderer && SDL_GetRenderOutputSize(g_renderer, &w, &h) && w > 0 && h > 0 && (w != (int)g_s.w || h != (int)g_s.h)) {
            touch_reset();
            g_s.w = (float)w, g_s.h = (float)h;
            fprintf(stderr, "[touch] window %dx%d\n", w, h);
        }
    }
#endif
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
    s.bar_open = false;
    if (s.bar_held) Port::Hooks::HoldItemBarOpen(false), s.bar_held = false;
    s.use_until = s.use_at = 0;
    s.stick_on = false, s.held = 0, s.cam_dx = s.cam_dy = 0, s.pinch = false, s.run = false;
    s.run_lock = s.run_free = s.run_armed = s.run_over = false;
    s.cs_x = s.cs_y = 0;
    unlatch(s, ~0ull, "every finger let go of");
}

void touch_set_game_state(const UiGameState &state) { g_game = state; }

bool touch_pad_button(const SDL_Event &e) {
    if (e.type != SDL_EVENT_GAMEPAD_BUTTON_DOWN || e.gbutton.button != SDL_GAMEPAD_BUTTON_RIGHT_STICK) return false;
    if (!lock_on_allowed()) return false;
    lock_toggle("right stick click");
    return true;
}

void touch_lock_key() {
    if (lock_on_allowed()) lock_toggle("a key");
}

void touch_item_key(int dir) {
    if (g_s.edit || g_game.item_bar_shown == 0) return;  // (the bar on the screen only)
    item_step(g_s, dir);
}
bool touch_controls_shown() { return g_s.visible || g_s.gear_only; }
bool touch_game_menu() { return g_menu_layout; }

bool touch_mouse_click_on_picture(Uint64 click_ns) {
    const bool on = g_mouse_tap_picture && click_ns >= g_mouse_tap_ns && click_ns - g_mouse_tap_ns < 100 * MS;
    g_mouse_tap_picture = false;  // (one click's)
    return on;
}

bool touch_bottom_wanted() {
    if (g_s.edit) return false;  // (the editor shows every control)
    // (hidden by a long press: as while they show, so that hiding them for a cutscene doesn't bring the touch screen in)
    return g_s.visible || g_s.user_hidden ? g_bottom_touch : g_bottom_pad;
}

void touch_set_bottom_default(bool shown) {
    g_bottom_touch = g_bottom_pad = shown;
    fprintf(stderr, "[touch] the touch screen's corner starts %s\n", shown ? "shown" : "hidden");
}

void touch_set_one_screen(bool on) {
    if (on != g_one_screen) fprintf(stderr, "[touch] the game's touch menus %s\n", on ? "on the top screen" : "on the touch screen");
    g_one_screen = on;
}

void touch_toggle_bottom() {
    bool &b = g_s.visible || g_s.user_hidden ? g_bottom_touch : g_bottom_pad;
    b = !b;
    fprintf(stderr, "[touch] the touch screen's corner %s\n", b ? "shown" : "hidden");
}

// "combo<n>" (1..COMBOS): n, else 0
static int combo_key(const std::string &key) {
    if (key.compare(0, 5, "combo") || key.size() < 6 || key.size() > 7 || key.find_first_not_of("0123456789", 5) != std::string::npos)
        return 0;
    const int n = atoi(key.c_str() + 5);
    return n >= 1 && n <= COMBOS ? n : 0;
}
std::string touch_setting(const std::string &key) {
    State &s = g_s;
    ensure_loaded(s);
    char text[32];
    if (key == "controls") return CONTROLS_KEY[g_set.controls];
    if (key == "opacity") return snprintf(text, sizeof text, "%.2f", g_set.opacity), text;
    if (key == "camera_speed") return snprintf(text, sizeof text, "%.2f", g_set.camera), text;
    if (key == "camera_smooth") return snprintf(text, sizeof text, "%.2f", g_set.smooth), text;
    if (key == "attack_style") return std::to_string(s.layout.style);
    if (const int k = combo_key(key)) return combo_text(s.layout.combo[k - 1]);
    if (!key.compare(0, 5, "show_"))
        for (int b = 0; b < TB_COUNT; b++)
            if (key.compare(5, std::string::npos, TB_KEY[b]) == 0) return g_set.show[b] ? "1" : "0";
    return "";
}

std::uint32_t touch_combo_buttons(int k) {
    ensure_loaded(g_s);
    if (k < 0 || k >= COMBOS) return 0;
    const uint64_t m = g_pub_combo[k];  // (ZL and ZR: bits 16 and 17 here, 14 and 15 there)
    return (uint32_t)(m & 0xfff) | (m >> 16 & 1 ? 1u << 14 : 0) | (m >> 17 & 1 ? 1u << 15 : 0);
}

void touch_set_setting(const std::string &key, const std::string &value) {
    State &s = g_s;
    ensure_loaded(s);
    const float x = strtof(value.c_str(), nullptr);
    bool known = true, combo_changed = false;
    if (key == "controls") {
        for (int m = 0; m < 3; m++)
            if (value == CONTROLS_KEY[m]) g_set.controls = m;
    } else if (key == "opacity") {
        g_set.opacity = std::clamp(x, .2f, 1.0f);
    } else if (key == "camera_speed") {
        g_set.camera = std::clamp(x, .2f, 5.0f);
    } else if (key == "camera_smooth") {
        g_set.smooth = std::clamp(x, 0.0f, 1.0f);
    } else if (key == "attack_style") {
        // the editor's way from one style to the next (what the corner band would cover moves inside its bend), which
        // takes the controls' places from where they were before: the layout as it is now
        int style = std::clamp((int)x, 0, STYLE_COUNT - 1);
        if (s.w > 0 && s.h > 0 && !s.edit) {
            s.saved = s.layout;
            for (int i = 0; i < STYLE_COUNT && s.layout.style != style; i++) next_style(s);
        }
        s.layout.style = style, g_style = style;
    } else if (const int k = combo_key(key)) {  // (the layout in use's, and its profile's file: below)
        s.layout.combo[k - 1] = combo_mask(value), s.layout.has_combos = true, combo_changed = true;
        publish(s.layout);
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
    if (combo_changed && !s.edit) {  // (the combined buttons are the profile's: its file too, 1.0.2)
        float hud[HUD_N][HUD_V];
        hud_now(hud);
        if (!profile_write(s.layout.profile, s.layout, hud))
            fprintf(stderr, "[touch] profiles: profile %d's combined buttons couldn't be saved in its file\n", s.layout.profile);
    }
}

bool touch_editing() { return g_s.edit; }

// The layout editor holds the game (android_main), but while a part of the game's HUD is dragged the game runs EDIT_RUN
// of every EDIT_STEP: it draws the part where it goes, and where the editor frames it comes from what it draws (held,
// the part and its frame stayed where they were: the owner's Galaxy, "se rompió el mover la interfaz"). The MHGU port
// takes a step of its game every 100 ms the same way
// (1.0.2) The chat's button where the editor has it, the layout in use's (chat_app.cpp: chat_overlay.h OverlayPort.place):
// its centre, fractions of the window, and its radius, a fraction of its height
bool touch_chat_place(float *fx, float *fy, float *fr) {
    *fx = g_chat_x < 0 ? CHAT_HOME_X : g_chat_x, *fy = g_chat_y < 0 ? CHAT_HOME_Y : g_chat_y, *fr = CHAT_R * g_chat_k;
    return true;
}
bool touch_editor_holds() {
    if (!g_s.edit) return false;
    const uint64_t EDIT_STEP = 100 * MS, EDIT_RUN = 40 * MS, now = SDL_GetTicksNS();
    if (now - g_hud_moved_ns > 2 * EDIT_STEP) return true;  // (nothing dragged just now: held)
    return now % EDIT_STEP >= EDIT_RUN;
}
// (1.0.2) The settings' HUD size changed (Screen and HUD > HUD size, settings_panel.cpp): the game, held while the settings
// are open, runs EDIT_RUN of every EDIT_STEP for a moment, as for a part dragged in the editor, so its HUD shows at the
// new size at once (held, the HUD stayed as it was until the settings closed: the owner's Galaxy)
uint64_t g_hud_size_ns;
void touch_hud_size_changed() { g_hud_size_ns = SDL_GetTicksNS(); }
bool touch_hud_size_runs() {
    const uint64_t EDIT_STEP = 100 * MS, EDIT_RUN = 40 * MS, now = SDL_GetTicksNS();
    return g_hud_size_ns && now - g_hud_size_ns <= 3 * EDIT_STEP && now % EDIT_STEP < EDIT_RUN;
}
bool touch_hud_size_recent() { return g_hud_size_ns && SDL_GetTicksNS() - g_hud_size_ns <= 600 * MS; }

void touch_edit_begin() {
    State &s = g_s;
    ensure_loaded(s);
    if (!s.edit) edit_begin(s);
}

#ifndef __ANDROID__
bool touch_edit_wheel(float x, float y, int notches) {
    State &s = g_s;
    if (!s.edit) return false;
    if (!notches || s.prof_open || s.img_open || s.w <= 0 || s.h <= 0) return true;
    const float by = notches * .05f;
    Rect panel, btn[E_COUNT], grab, r;
    editor_panel(g_epanel, s.w, s.h, &panel, btn, &grab);
    // (what is under the pointer: the quick items, the monsters' bars, a control, the panel, the item bar's area; else the
    // one picked)
    int target = -1000;
    if (items_frame(&r) && r.in(x, y)) target = SEL_ITEMS;
    else if (bars_frame(&r) && r.in(x, y)) target = SEL_BARS;
    if (target == -1000 && !g_edit_hud && !panel.in(x, y)) {
        const int c = hit(s.layout, x, y, s.w, s.h, nullptr, 1.5f, true, false, true);
        if (c >= 0 && c < C_LAID) target = c;
    }
    if (target == -1000 && panel.in(x, y)) target = SEL_PANEL;
    if (target == -1000 && hud_offered(HP_ITEMS)) {
        const Rect it = hud_rect(s, HP_ITEMS);
        const Rect z = it.ok ? zone_round(it.x0, it.y0, it.x1, it.y1, s.layout.item_zone) : it;
        if (z.in(x, y)) target = ZONE_SEL + HP_ITEMS;
    }
    // (1.0.2: any other part of the game's HUD, its own size as - / + gives it; the item bar's own size is - / +'s, the
    // wheel being its area's as a phone's pinch)
    for (int k = 0; target == -1000 && k < HUD_N; k++) {
        if (k == HP_ITEMS || !hud_offered(k)) continue;
        const Rect hr = hud_rect(s, k);
        if (hr.ok && hr.in(x, y)) target = ZONE_SEL + k;
    }
    if (target == -1000) target = s.sel;
    if (target >= 0 && target < C_COUNT) s.layout.scale[target] += by, clamp_place(s, target);
    else if (target == ZONE_SEL + HP_ITEMS) item_zone_set(s, s.layout.item_zone + by);
    else if (target >= ZONE_SEL && target < ZONE_SEL + HUD_N) hud_size_set(s, target - ZONE_SEL, s.hud_at[target - ZONE_SEL][2] + by);
    else if (target == SEL_BARS) bars_size(s, s.layout.bars_k + by);
    else if (target == SEL_ITEMS) items_size(s, s.layout.items_k + by);
    else if (target == SEL_PANEL) panel_resize(s, g_epanel.k + by, x, y);
    else return true;
    s.sel = target;
    return true;
}

bool touch_edit_escape() {
    State &s = g_s;
    if (!s.edit) return false;
    if (s.prof_open || s.img_open) {  // (the window over it, first)
        s.prof_open = s.img_open = false;
        return true;
    }
    edit_end(s, false);
    fprintf(stderr, "[touch] layout editor: cancelled by Esc\n");
    return true;
}

bool touch_screen_touched() { return g_screen_touched; }
#endif

void touch_haptic_tap() {
    haptics_tap();
}

// ---- the control profiles from the settings menu (its Control profiles page: settings_panel.cpp): what a slot holds, the
// one in use, and an action on one at once, the layout in use, its HUD in mh4u.ini and the files written as the editor's
// Save writes them (not while the editor is open: false). The MHGU port's the same
namespace {
// The layout in use is now `l` (its profile's): in touch_layout.txt, its HUD's parts in mh4u.ini (where it has them),
// published; the fingers on what it no longer has let go
void profile_in_use(State &s, const Layout &l, const char *why) {
    s.layout = l, s.layout.profile_line = true;
    run_clear_of_band(s.layout);
    s.saved = s.layout;
    for (int k = 0; k < HUD_N && l.has_hud; k++) {
        game_option_set(std::string("hud_") + HUD_KEY[k], hud_value(l.hud[k]));
        if (k == HP_ITEMS) game_option_set("item_bar", hud_value(l.hud[k]));
    }
    save_layout(s.layout);
    publish(s.layout);
    touch_reset();
    fprintf(stderr, "[touch] profiles: profile %d in use (%s, %s)\n", s.layout.profile, why,
            s.layout.kind == KIND_EMU ? "emulator-style" : "the port's own");
}
// (1.0.2) Profile n is about to take the place of the one in use: that one, if it has no file yet, written first with
// its HUD as mh4u.ini has it
void keep_in_use_now(State &s, int n) {
    if (n == s.layout.profile) return;
    float hud[HUD_N][HUD_V];
    hud_now(hud);
    keep_in_use(s.layout, hud);
}
}  // namespace
// (1.0.2, T18, GU#258 in the MHGU port) The settings' Buttons on the screen rows while an emulator-style layout is in
// use: a row whose button that layout has its own of (EMU_TWINS) shows and turns that one's Hide (Layout.off, the
// editor's Hide), not the row's own setting, which is the port's layout's: nothing changes until the player taps it
namespace {
const struct { const char *key; int a, b; } EMU_TWINS[] = {
    {"attacks", C_EA, C_EX}, {"dodge", C_EB, -1}, {"guard", C_ER, -1}, {"sheathe", C_EY, -1}, {"zr", C_EZR, -1},
    {"zl", C_EZL, -1},       {"l", C_EL, -1},     {"start", C_ESTART, -1}, {"select", C_ESELECT, -1}, {"dpad", C_EDPAD, -1}};
}  // namespace
int touch_emu_shown(const std::string &key) {
    State &s = g_s;
    ensure_loaded(s);
    if (s.layout.kind != KIND_EMU || s.edit) return -1;
    for (const auto &t : EMU_TWINS)
        if (key == t.key) return (s.layout.off >> t.a & 1) && (t.b < 0 || (s.layout.off >> t.b & 1)) ? 0 : 1;
    return -1;
}
void touch_emu_show(const std::string &key, bool on) {
    State &s = g_s;
    ensure_loaded(s);
    if (s.layout.kind != KIND_EMU || s.edit) return;
    for (const auto &t : EMU_TWINS) {
        if (key != t.key) continue;
        for (int c : {t.a, t.b})
            if (c >= 0) s.layout.off = on ? s.layout.off & ~(1ull << c) : s.layout.off | 1ull << c;
        s.saved = s.layout;
        publish(s.layout);
        for (Finger &f : s.f)  // (a control gone from under a finger lets go of it)
            if (f.kind == F_BUTTON) f.done = true;
        recompute_held(s);
        save_layout(s.layout);
        float hud[HUD_N][HUD_V];
        hud_now(hud);
        profile_write(s.layout.profile, s.layout, hud);
        fprintf(stderr, "[touch] emulator-style layout: %s %s (the settings)\n", key.c_str(), on ? "shown" : "hidden");
    }
}
int touch_profile_active() {
    ensure_loaded(g_s);
    return g_s.layout.profile;
}
int touch_profile_kind(int n) {
    ensure_loaded(g_s);
    int kind;
    if (n < 1 || n > PROFILES_N) return -1;
    if (profile_info(n, &kind)) return kind;
    return n == g_s.layout.profile ? g_s.layout.kind : -1;  // (in use, its file not written yet: a layout of before)
}
bool touch_profile_use(int n) {
    State &s = g_s;
    ensure_loaded(s);
    Layout l;
    if (n < 1 || n > PROFILES_N || s.edit || !load_profile_file(profile_path(n), &l)) return false;
    l.profile = n;
    keep_in_use_now(s, n);
    profile_in_use(s, l, "the settings' Use");
    return true;
}
bool touch_profile_store(int n) {
    State &s = g_s;
    ensure_loaded(s);
    if (n < 1 || n > PROFILES_N || s.edit) return false;
    float hud[HUD_N][HUD_V];
    hud_now(hud);
    keep_in_use_now(s, n);
    if (!profile_write(n, s.layout, hud)) return false;
    Layout l = s.layout;
    l.profile = n;
    profile_in_use(s, l, "the settings' Save the current layout here");
    return true;
}
bool touch_profile_new(int n, int kind) {
    State &s = g_s;
    ensure_loaded(s);
    if (n < 1 || n > PROFILES_N || s.edit) return false;
    Layout l = defaults(kind == KIND_EMU ? KIND_EMU : KIND_OURS);
    l.profile = n, l.has_hud = true, l.has_combos = true;  // (the HUD's parts at the game's places; no combined buttons)
    keep_in_use_now(s, n);
    if (!profile_write(n, l, l.hud)) return false;
    profile_in_use(s, l, "the settings' New");
    return true;
}
// ---- (1.0.2, S3) A profile in a .zip and back: Export and Import on the settings' profile page, as the MHGU port's. The
// .zip holds mh4u_profile.txt (the profile's file, its combined buttons and HUD parts too) and images/<file> for each
// picture it names that the images folder has (never the port's own art). Read with unzip.c (stored or Deflate),
// written by zipw.c. A .zip with any other entry, a path that would go outside (.., absolute, a drive), no profile file or
// one without its layout_kind line is turned down whole; so are more than 64 entries, a profile file over 256 KB, a
// picture over 16 MB or over 128 MB in all. Its pictures come into the images folder: one of the same name and bytes is
// that picture; another of that name stays, and the new one is <name>_2 (_3...), the profile's lines changed to match
// (mhgu_profile.txt in the MHGU port's: one game's profile is no profile of the other's)
namespace {
const char *const ZIP_PROFILE = "mh4u_profile.txt", *const IMPORT_FILE = "profile_import.zip", *const EXPORT_DIR = "profile_export";
const size_t ZIP_PROFILE_MOST = 256 << 10, ZIP_PICTURE_MOST = 16 << 20;
const uint64_t ZIP_ALL_MOST = 128ull << 20;
const int ZIP_ENTRIES_MOST = 64;
std::string g_io_said;  // what the last export or import did (touch_profile_said)
int g_import_wait = 0;  // the slot the .zip the app's screen copies in goes to
void io_say(const char *english, int n = 0) {
    char t[400];
    snprintf(t, sizeof t, T(english), n);
    g_io_said = t;
}
// A picture's name in the .zip (images/<it>): one file, a picture's extension, no character a file name can't have
bool zip_picture_name(const char *f) {
    size_t n = strlen(f);
    if (!n || n >= IMAGE_NAME - 4 || f[0] == '.' || f[0] == ' ') return false;
    for (const char *p = f; *p; p++)
        if ((unsigned char)*p < 32 || strchr("/\\:*?\"<>|", *p)) return false;
    const char *dot = strrchr(f, '.');
    if (!dot) return false;
    static const char *const EXT[] = {".png", ".jpg", ".jpeg", ".webp", ".bmp"};
    for (const char *x : EXT)
        if (!SDL_strcasecmp(dot, x)) return true;
    return false;
}
// A path in the .zip that would go outside the folder it is unpacked in: absolute, a drive, .. (unzip.c reads a '\\' as '/')
bool zip_escapes(const char *name) {
    if (name[0] == '/' || strchr(name, ':')) return true;
    for (const char *p = name; *p;) {
        size_t n = strcspn(p, "/");
        if (n == 2 && p[0] == '.' && p[1] == '.') return true;
        p += n;
        if (*p) p++;
    }
    return false;
}
std::string in_data(const char *name) { return g_data_dir + "/" + name; }
}  // namespace
std::string touch_profile_zip_name(int n) {
    time_t t = time(nullptr);
    struct tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t);  // (the PC harness)
#else
    localtime_r(&t, &tm);
#endif
    char name[96];
    snprintf(name, sizeof name, "MH4U_profile_%02d_%04d-%02d-%02d.zip", n, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return name;
}
bool touch_profile_export(int n, const std::string &path) {
    State &s = g_s;
    ensure_loaded(s);
    if (n < 1 || n > PROFILES_N) return false;
    if (n == s.layout.profile && !is_file(profile_path(n))) {  // (in use with no file of its own yet: written now)
        float hud[HUD_N][HUD_V];
        hud_now(hud);
        keep_in_use(s.layout, hud);
    }
    Layout l;
    if (!load_profile_file(profile_path(n), &l)) {
        io_say("Profile %d is empty", n);
        return false;
    }
    // (its file as this version writes it: the combined buttons it takes from before, for one of before)
    std::string text, tmp = profiles_dir() + "/export.tmp";
    FILE *f = make_folder(profiles_dir()) ? fopen(tmp.c_str(), "w") : nullptr;
    bool ok = f != nullptr;
    if (f) {
        l.profile = n, l.profile_line = true;
        write_layout_lines(f, l, l.has_hud ? l.hud : nullptr);
        ok = !ferror(f), ok = fclose(f) == 0 && ok;
    }
    ok = ok && read_text(tmp, &text);
    remove(tmp.c_str());
    if (!ok) {
        io_say("It couldn't be done. Is the save folder there?");
        return false;
    }
    std::vector<std::string> names, data;
    auto picture = [&](const char *name) {
        std::string bytes;
        if (!name[0] || std::find(names.begin(), names.end(), name) != names.end()) return;
        if (!read_text(images_dir() + "/" + name, &bytes)) return;  // (named, and gone: left out)
        names.push_back(name), data.push_back(std::move(bytes));
    };
    for (int i = 0; i < C_LAID; i++) picture(l.image[i]);
    picture(l.knob);
    std::vector<std::string> zipped(names.size());
    std::vector<ZipItem> items;
    items.push_back({ZIP_PROFILE, text.data(), text.size()});
    for (size_t i = 0; i < names.size(); i++) {
        zipped[i] = "images/" + names[i];
        items.push_back({zipped[i].c_str(), data[i].data(), data[i].size()});
    }
    if (!zip_write(path.c_str(), items.data(), (int)items.size())) {
        fprintf(stderr, "[touch] profiles: profile %d not exported: %s\n", n, SDL_GetError());
        io_say("It couldn't be done. Is the save folder there?");
        return false;
    }
    const size_t slash = path.find_last_of("/\\");
    const std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    fprintf(stderr, "[touch] profiles: profile %d exported with %d picture(s) to %s\n", n, (int)names.size(), path.c_str());
    char t[400];
    snprintf(t, sizeof t, T("Exported: %s"), base.c_str());
    g_io_said = t;
    return true;
}
int touch_profile_import(int n, const std::string &path) {
    if (n < 1 || n > PROFILES_N) return IMPORT_FAILED;
    Unzip z;
    if (unzip_open(&z, path.c_str()) != UNZIP_OK) {
        unzip_close(&z);
        fprintf(stderr, "[touch] profiles: import into %d: not a .zip (%s)\n", n, path.c_str());
        io_say("That file is not a .zip, or it is damaged.");
        return IMPORT_NOT_ZIP;
    }
    int prof = -1, why = IMPORT_OK;
    uint64_t total = 0;
    std::vector<int> pics;
    if (z.count > ZIP_ENTRIES_MOST) why = IMPORT_NOT_PROFILE;
    for (int i = 0; i < z.count && why == IMPORT_OK; i++) {
        const UnzipEntry *e = &z.entries[i];
        const char *nm = e->name;
        total += e->size;
        if (zip_escapes(nm)) why = IMPORT_ESCAPES;
        else if (e->flags & 1) why = IMPORT_NOT_PROFILE;
        else if (!strcmp(nm, ZIP_PROFILE) && !e->dir && prof < 0 && e->size <= ZIP_PROFILE_MOST) prof = i;
        else if (!strcmp(nm, "images/") && e->dir) continue;
        else if (!strncmp(nm, "images/", 7) && !e->dir && zip_picture_name(nm + 7) && e->size <= ZIP_PICTURE_MOST) pics.push_back(i);
        else why = IMPORT_NOT_PROFILE;
    }
    if (why == IMPORT_OK && (prof < 0 || total > ZIP_ALL_MOST)) why = IMPORT_NOT_PROFILE;
    std::string text, dir = profiles_dir(), tmp = dir + "/import.tmp";
    if (why == IMPORT_OK) {
        if (!make_folder(dir)) why = IMPORT_FAILED;
        else if (unzip_extract(&z, prof, tmp.c_str(), nullptr, nullptr) != UNZIP_OK || !read_text(tmp, &text))
            why = IMPORT_NOT_PROFILE;
        remove(tmp.c_str());
        bool kind = false;  // (its signature: the layout_kind line every profile file has)
        for (size_t at = text.find("layout_kind "); at != std::string::npos && !kind; at = text.find("layout_kind ", at + 1))
            kind = at == 0 || text[at - 1] == '\n';
        if (why == IMPORT_OK && !kind) why = IMPORT_NOT_PROFILE;
    }
    Layout l;
    if (why == IMPORT_OK) {
        parse_layout(text, &l, false);
        std::string images = images_dir();
        if (!pics.empty() && !make_folder(images)) why = IMPORT_FAILED;
        for (size_t p = 0; p < pics.size() && why == IMPORT_OK; p++) {
            const char *name = z.entries[pics[p]].name + 7;
            std::string got, there, part = images + "/import.tmp";
            if (unzip_extract(&z, pics[p], part.c_str(), nullptr, nullptr) != UNZIP_OK || !read_text(part, &got)) {
                remove(part.c_str());
                why = IMPORT_NOT_PROFILE;
                break;
            }
            // (its name in the folder: the same picture there already, or a name nothing has)
            std::string stem = name, ext, final_name;
            size_t dot = stem.rfind('.');
            ext = stem.substr(dot), stem.resize(dot);
            for (int k = 1; k < 100 && final_name.empty(); k++) {
                std::string cand = k == 1 ? std::string(name) : stem + "_" + std::to_string(k) + ext;
                std::string at = images + "/" + cand;
                if (!is_file(at)) {
                    if (rename(part.c_str(), at.c_str()) == 0) final_name = cand;
                    else why = IMPORT_FAILED;
                    break;
                }
                if (read_text(at, &there) && there == got) final_name = cand;
            }
            remove(part.c_str());
            if (final_name.empty() && why == IMPORT_OK) why = IMPORT_FAILED;
            if (why == IMPORT_OK && final_name != name) {  // (the profile's lines with the name it has here)
                for (int i = 0; i < C_COUNT; i++)
                    if (!strcmp(l.image[i], name)) snprintf(l.image[i], IMAGE_NAME, "%s", final_name.c_str());
                if (!strcmp(l.knob, name)) snprintf(l.knob, IMAGE_NAME, "%s", final_name.c_str());
                fprintf(stderr, "[touch] profiles: import: the picture %s is %s here (another one had its name)\n", name, final_name.c_str());
            }
        }
    }
    unzip_close(&z);
    if (why == IMPORT_OK) {
        State &s = g_s;
        ensure_loaded(s);
        l.profile = n, l.profile_line = true;
        if (s.edit || !profile_write(n, l, l.has_hud ? l.hud : nullptr)) why = IMPORT_FAILED;
        else if (n == s.layout.profile) profile_in_use(s, l, "the settings' Import");  // (the one in use: replaced, as asked)
    }
    fprintf(stderr, "[touch] profiles: import into %d from %s: %s (%d picture(s))\n", n, path.c_str(),
            why == IMPORT_OK ? "done" : why == IMPORT_ESCAPES ? "turned down, a path goes outside"
            : why == IMPORT_NOT_PROFILE ? "turned down, not a control profile" : "failed", (int)pics.size());
    if (why == IMPORT_OK) io_say("Imported into profile %d", n);
    else if (why == IMPORT_ESCAPES) io_say("That .zip has files that would go outside its folder: it was not imported.");
    else if (why == IMPORT_NOT_PROFILE) io_say("That .zip is not a control profile of this game.");
    else io_say("It couldn't be done. Is the save folder there?");
    return why;
}
bool touch_profile_export_app(int n) {
    const std::string dir = in_data(EXPORT_DIR);
    make_folder(dir);
    if (DIR *d = opendir(dir.c_str())) {  // (one .zip there at a time: the one the app's screen copies out)
        while (dirent *e = readdir(d))
            if (e->d_name[0] != '.') remove((dir + "/" + e->d_name).c_str());
        closedir(d);
    }
    return touch_profile_export(n, dir + "/" + touch_profile_zip_name(n));
}
void touch_profile_import_wait(int n) {
    remove(in_data(IMPORT_FILE).c_str());
    g_import_wait = n;
}
void touch_profile_import_poll() {
    const int n = g_import_wait;
    if (!n || !is_file(in_data(IMPORT_FILE))) return;
    g_import_wait = 0;
    touch_profile_import(n, in_data(IMPORT_FILE));
    remove(in_data(IMPORT_FILE).c_str());
}
bool touch_profile_said(std::string *out) {
    if (g_io_said.empty()) return false;
    *out = g_io_said;
    g_io_said.clear();
    return true;
}
bool touch_profile_clear(int n) {
    State &s = g_s;
    ensure_loaded(s);
    if (n < 1 || n > PROFILES_N || n == s.layout.profile || s.edit) return false;
    bool ok = remove(profile_path(n).c_str()) == 0;
    if (ok) fprintf(stderr, "[touch] profiles: profile %d emptied (the settings)\n", n);
    return ok;
}

bool touch_items_place(float *x, float *y, float *k) {
    *k = std::clamp(g_items_k, .5f, 2.0f);
    if (g_items_x < 0 || g_items_y < 0) return false;
    *x = g_items_x, *y = g_items_y;
    return true;
}
bool touch_bars_place(float *x, float *y, float *k) {
    *k = std::clamp(g_bars_k, .5f, 2.0f);
    if (g_bars_x < 0 || g_bars_y < 0) return false;
    *x = g_bars_x, *y = g_bars_y;
    return true;
}
extern "C" bool touch_fps_place(float w, float h, float pw, float ph, float *x0, float *y0) {
    if (g_fps_x < 0 || g_fps_y < 0 || w <= 0 || h <= 0) return false;
    *x0 = std::clamp(g_fps_x * w - pw * .5f, 0.0f, std::max(0.0f, w - pw));
    *y0 = std::clamp(g_fps_y * h - ph * .5f, 0.0f, std::max(0.0f, h - ph));
    return true;
}
