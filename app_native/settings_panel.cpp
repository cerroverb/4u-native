// The in-game settings (settings_panel.h): the MHGU port's settings menu (runtime/settings_panel.cpp, its phone layout)
// made over for MH4U's options. Nearly the whole screen: a header with the title, Back (in a submenu or a question),
// Save and Close; the tabs in a column at the left, the rows in a list a finger drags, the picked row's explanation
// beside or under them; a row's choices in a picker of their own over the menu, dimmed; - and + on a number's row; an
// On / Off switch on a switch's. The colours are the touch controls' (touch.cpp): wine-dark plates with a bronze edge
// and a gold rule, cream letters, the picked row and tab lit ochre to gold as the game's cursor, the game's red and
// green rules along the window's top and bottom and gold studs on its corners.
//
// What it changes, and where:
//   - the port's options, mh4u.ini (game_options.h): the frame rate, the HUD at the edges, the screen filters, the
//     dither pattern and the Gameplay tab's buttons apply at once; the widescreen at the next start
//   - the app's own, mh4u-app.ini (app_settings.h): the 3D resolution and the volume at the next start (android_main.cpp
//     reads them), the touch screen's corner when the menu closes, the FPS counter, the haptics and the menu's own at once
//   - the touch controls' (touch_layout.txt, touch.h touch_setting / touch_set_setting) at once, and their layout
//     editor (touch_edit_begin) once the menu has closed
//   - the GPU driver (gpu_driver.h, Graphics & Audio): the system's or a player's own for an Adreno, at the next start;
//     importing and removing one at once
//   - the network rooms (net_rooms.h, the Multiplayer tab): join, leave, host and close at once, while the game runs;
//     the nickname, the addresses and the room's options in mh4u-app.ini's [Network], at Save or when a room is joined
//     or hosted with them. The room's chat and comings and goings show over the game for a moment while it is closed.
//   - mods and cheats (content.h, the Mods and Cheats tabs): the mods' order and switches in load/mods/load_order.txt at
//     Save, read at the next start; the cheats' switches at Save, in the game from its next frame, and in the cheats
//     file. Adding and removing either is done at once.
// Nothing changes until Save; leaving with something unsaved asks first. Text is typed on the menu's own keyboard (a
// view of keys in its style, which a controller works too), on a physical keyboard, or on the phone's if asked for.
#include "display_vsync.h"
#include "settings_panel.h"

#ifdef __ANDROID__
#include <jni.h>
#else
#include "pc_display.h"  // (MH4U for PC: its own rows, pc/settings_pc.inc)
#include "pc_hdr.h"
#include "settings_pc.h"
#endif
#include <strings.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "app_jni.h"
#include "app_live.h"
#include "app_settings.h"
#include "chat/chat.h"
#include "chat/chat_crypto.h"
#include "chat/chat_overlay.h"
#include "chat/chat_text.h"
#include "chat_app.h"
#include "content.h"
#include "controls.h"
#include "first_run.h"
#include "frame_gen.h"
#include "fsr1.h"
#include "game_options.h"
#include "gpu_driver.h"
#include "second_screen.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "net_lan.h"
#include "net_rooms.h"
#include "padmap.h"
#include "perf_hint.h"
#include "perfstats.h"
#include "app_version.h"
#include "cache_clear.h"
#include "colour_filter.h"
#include "mh4n/pipeline_progress.h"
#include "settings_icons.h"
#ifndef __ANDROID__
#include "pc_input.h"  // (the PC's list of the controls: pc/pc_input.cpp)
#endif
#include "settings_locale.h"
#include "shared_screens.h"
#include "touch.h"
#include "ui.h"
#include "verify_files.h"

namespace {

// ---------------------------------------------------------------- what the menu edits
// Six sections in the column at the left, and Credits & Licenses as a smaller plate under them (MENU.md; the MHGU
// port's the same): each a page of its own with its basic rows, and pages one level down for the rest (the registry
// of pages and entries, after Row)
constexpr int PAGES = 7, PAGE_ABOUT = 6;
const char* const page_names[PAGES] = {"Controls", "Graphics & Audio", "Gameplay", "Multiplayer", "Mods & Cheats", "System",
                                       "Credits & Licenses"};
const char* const page_ids[PAGES] = {"controls", "graphics", "gameplay", "multiplayer", "mods", "system", "about"};
namespace rooms = mh4n::rooms;
namespace hubs = mh4n::hubs;
std::atomic<bool> g_chat_asked{false};  // (the chat button: the menu opens on the chat's page)
void credits_show();  // (the credits shown when the app opens: draw_credits, further down)
void thanks_show();   // (the thanks card: draw_thanks, further down)
void online_then(const std::string& server, std::function<void()> fn);  // (the notice about playing online: the same card)
// The notice about playing online (the card's, further down; the same text in MHGU for Android): %s the server
const char* const ONLINE_TEXT = "Playing online connects this device to %s. As with any server on the internet, the servers it connects to can see your IP address. You can choose another server, or none, in the menu's Multiplayer section.";
// The lobby that lists public rooms ([Network] Lobby): empty is this one, a community lobby where most of the game's
// players list their rooms (run by players, not by this project), so a later default reaches everyone who kept it;
// "Off" is none, anything else the one typed. The same short list as MHGU's hub server: the default, Custom..., Off
const char* const DEFAULT_LOBBY = "http://88.198.47.46:5000";
const char* const LOBBY_LIST[] = {DEFAULT_LOBBY};
bool lobby_off(const std::string& setting) { return !strcasecmp(setting.c_str(), "off"); }
std::string lobby_url(const std::string& setting) { return lobby_off(setting) ? "" : setting.empty() ? DEFAULT_LOBBY : setting; }
// The hub server ([Network] HubServer; mh4n/hubs.h, 1.0.1): empty is this one, the community server MHGU's players run
// (the same server software lists both games' hubs, each game and region apart), so a later default reaches everyone who
// kept it; "Off" is none, anything else the one typed. The same short list as the lobby's: the default, Custom..., Off
const char* const DEFAULT_HUB_SERVER = "hunterhubid-gu-hub.duckdns.org";
bool hub_server_off(const std::string& setting) { return !strcasecmp(setting.c_str(), "off"); }
std::string hub_server(const std::string& setting) { return hub_server_off(setting) ? "" : setting.empty() ? DEFAULT_HUB_SERVER : setting; }
// [Network] PlayWith (Multiplayer > Play online with): this app's hubs, Azahar's and Citra's rooms, or both
enum { PLAY_HUBS, PLAY_ROOMS, PLAY_BOTH };
// [Network] RecentLobby1..: how many lobbies typed and saved the Lobby row lists, the last first (MHGU's LDN_RECENT)
constexpr int kRecentLobbies = 24;
// A lobby the player typed: not the default (empty), not Off, none of LOBBY_LIST's
bool typed_lobby(const std::string& v) {
    if (v.empty() || lobby_off(v) || v.find_first_of("\r\n") != std::string::npos) return false;
    for (const char* l : LOBBY_LIST)
        if (!strcasecmp(v.c_str(), l)) return false;
    return true;
}
bool has_lobby(const std::vector<std::string>& list, const std::string& v) {
    for (const auto& l : list)
        if (!strcasecmp(l.c_str(), v.c_str())) return true;
    return false;
}
bool g_online_seen = false;           // (the notice was let go: now, or in an earlier session as the menu opens)
std::string g_online_server;          // (the server it names)
std::function<void()> g_online_then;  // (what waits for it)
const char* const THANKS_BODY = "To everyone who created this game, and to everyone who kept it alive over the years: thank you. This work is a love letter to the Monster Hunter series, with my deepest respect to the people who develop it.";
enum { THEME_HUNTER, THEME_GOLD, THEME_BLUE };
const char* const THEME_KEYS[] = {"hunter", "gold", "blue"};
const char* const LANGUAGE_KEYS[] = {"auto", "en", "es"};
#ifdef __ANDROID__
// (1.0.2: "second", the touch screen on a second display of the device, second_screen.h; offered only while there is one)
const char* const LAYOUT_KEYS[] = {"overlay", "stacked", "side", "second"};
#else
// (the PC: "window", the touch screen in its own window, which goes on any monitor: the PC's second screen)
const char* const LAYOUT_KEYS[] = {"overlay", "stacked", "side", "window"};
#endif
const char* const CORNER_KEYS[] = {"bottom-right", "bottom-left", "top-right", "top-left"};
const char* const CONTROLS_KEYS[] = {"auto", "on", "off"};
const char* const WIDE_KEYS[] = {"match", "16:9", "off"};
// [Controls] of mh4u-app.ini: a controller's (controls.h)
const char* const FACE_KEYS[] = {"position", "label"};
const char* const STICK_KEYS[] = {"camera", "cstick"};
const char* const R3_KEYS[] = {"lockon", "L", "ZL", "ZR", "off"};
const char* const TOWN_KEYS[] = {"select", "l3", "r3", "off"};  // [Controls] TownMenu (town.h)
#ifndef __ANDROID__
// (the PC) [Controls] ButtonIcons: the controller's buttons in the menu's guides, by its own look (auto: the one in use's)
const char* const ICON_KEYS[] = {"auto", "xbox", "playstation", "bayx", "game"};
#endif

// The touch controls' buttons that can be hidden (touch.cpp's TB_KEY, show_<key>), and whether each shows at first
struct TouchButton {
    const char *key, *title;
    bool on;
    int group;  // 0 hunting, 1 bows and bowguns, 2 menus and the rest, 3 a row of its own elsewhere (the floating stick:
               // Controls > Touch controls, MENU.md), not in Buttons on the screen nor in its count
};
const TouchButton TOUCH_BUTTONS[] = {
    {"attacks", "Attacks (A / X)", true, 0},        {"dodge", "Evade (B)", true, 0},
    {"guard", "Guard / dash (R)", true, 0},         {"guard_slide", "Keep guarding while sliding", false, 0},
    {"slide_both", "Slide between buttons: both pressed", true, 0},  // (1.0.1: touch.cpp slide_group)
    {"sheathe", "Sheathe (Y)", true, 0},
    {"zr", "Kick (ZR)", true, 0},                   {"zl", "Target (ZL)", true, 0},
    {"l", "Camera (L)", true, 0},                   {"lock_on", "Lock-on button", true, 0},
    {"map_button", "Map button", true, 0},  // (1.0.2: shows and hides the map on the main screen)
    {"weapon_tree", "Weapon tree button", true, 0},  // (1.0.2: the Smithy's, after the Lock-on button's as in the MHGU port)
    {"stick", "Movement stick", true, 0},
    {"floating_stick", "Floating stick", false, 3},
    {"stick_run", "Run with the stick", true, 0},
    {"select", "SELECT in hunts", false, 0},
    {"dpad", "D-pad in hunts", false, 0},           {"ranged", "Layout for bows and bowguns", true, 1},
    {"fire", "Fire / shoot", true, 1},              {"second", "Reload / melee", true, 1},
    {"aim", "Aim", true, 1},                        {"ranged_special", "Crouching fire / coating", true, 1},
    {"ammo", "Ammo arrows", true, 1},               {"start", "Game menu (START)", true, 2},
    {"screen", "Touch screen button", true, 2},
    {"menu_dpad", "Menu pad", true, 2},             {"item_arrows", "Item bar arrows", true, 0},
    {"menu_lr", "L and R in menus", true, 2},       {"dpad_menus", "D-pad in menus", false, 2},  // (the MHGU port's too)
    {"camera_right_half", "Camera on the right half only", false, 3},  // (1.0.2: Controls > Touch controls, S-CAM)
};
constexpr int TB = sizeof TOUCH_BUTTONS / sizeof *TOUCH_BUTTONS;

// The port's buttons (hooks.cpp PadBits), as the Gameplay tab offers them
const char* const BUTTON_KEYS[] = {"off", "ZR", "ZL", "R", "L", "X", "Y"};
const char* const END_KEYS[] = {"ZL+ZR", "L+R", "off"};

struct Draft {
    // mh4u.ini (game_options.h), as the file has them
    std::string fps, steady, widescreen, hud_edges, bloom, motion_blur, depth_of_field, radial_blur, haze, dither, antialiasing, fov;
    std::string hud_size;  // (1.0.2: mh4u.ini hud_size, the HUD's size in percent, 60 to 100)
    std::string kick, target, skip, skip_end, skip_end_hold;
    std::string gather_all;  // (Gameplay, Gathering: a gather gives all the point had left, mh4n gather.cpp, 1.0.1)
    std::string net_sync;    // (Multiplayer: wait for a player who stalls, mh4n netsync_native.h, 1.0.2)
    std::string qol_gather, qol_flex, qol_sharpen, qol_rewards;  // (the Cheats page's comforts: hooks.cpp Qol*)
    std::string rewards_send_all;  // (Gameplay, After a quest: hooks.cpp QolRewardsSendAll, S43)
    std::string dlc_flags;  // (DLC and event quests: the installed DLC in every save, mh4n hooks.cpp DlcFlagsStep, 1.0.1)
    std::string single_screen;  // (S45: one screen, onescreen_rows below)
    std::string effects;  // (effects: the game's particle effects, mh4n hooks.cpp EffectsApply)
    std::string monsters_on_map, damage_numbers;  // (1.0.1, the hunting aids: settings_hunt_aids.inc)
    std::string monster_health_bar;  // (1.0.1: the monsters' health bars, settings_hunt_aids.inc, hunt_bars.cpp)
    std::string quick_items;         // (1.0.1.1: the touch screen's quick items on the main screen, quick_items.cpp)
    std::string bow_aim;             // (1.0.1.1: a bow's shot to the monster as it is let go, mh4n hooks.cpp BowAimStep)
    std::string number_size = "medium", number_colour = "white";  // ([Graphics] DamageNumberSize / DamageNumberColour)
    std::string number_style = "classic";  // ([Graphics] DamageNumberStyle, 1.0.2: hunt_numbers.cpp)
    // mh4u-app.ini (app_settings.h)
    int scale = kRenderScaleDefault, volume = 100;  // scale: the 3D resolution, % of the phone's (0: the 3DS's)
    int low_end = LOW_END_OFF;
    // [Colour] (colour_filter.h, Graphics & Audio > Colour): every one 0, the screens as the game draws them
    int hue = 0, temperature = 0, tint = 0, brightness = 0, contrast = 0, saturation = 0, gamma = 0;
    bool stats = false, shadows = true, reverb = true;
    bool rec_audio = false;  // (1.0.2) [Audio] ScreenRecording: the sound through the phone's usual path (android_main.cpp)
    bool hd_half = false;  // Texture packs > Pack texture size: half ([Graphics] HdPackSize = half; MH4N_HD_SCALE, 1.0.1)
    bool frame_gen = false;  // [Graphics] FrameGen: a made-up picture between each two of the game's (frame_gen.h)
    bool ps1_filter = false;  // (1.0.1.1) [Graphics] Ps1Filter: the PS1 filter (settings_ps1.inc), off as the app comes
    // (1.0.2, S6) [Graphics] Upscale = smooth|fsr1 (fsr1.h: the top screen scaled up by FSR 1 where it is shown larger
    // than its picture; smooth as it comes) and Fsr1Sharpness = 0..100 (%, 80 as it comes)
    bool fsr1 = false;
    int fsr1_sharpness = 80;
    bool text_outline = false;  // (1.0.2, GU#243) [Graphics] TextOutline: a dark outline round the game's letters (app_live)
    bool fast_top = false;    // (1.0.2) [Graphics] FastTopScreen: the top screen scaled by the GPU's filter (shared_screens.h)
    bool adreno_fix = true;  // [Graphics] AdrenoFix: auto (first_run.h adreno_fix_applies; its row shows only there)
    bool screen_shown = true;
    int layout = 0, corner = 0, size = 42, opacity = 100;
    int layout_before = 0;  // (1.0.2) with layout 3 (on the second display): the layout before it, while there is none
    bool haptics = true;
    bool long_press_hide = true;  // [Controls] LongPressHide: a long press on an empty spot hides the touch controls (touch.cpp)
    bool lock_on = true;  // [Controls] LockOn: the Lock-on button, a pad's right stick click (touch.cpp)
    bool gyro_aim = false;  // [Controls] GyroAim, GyroSensitivity (tenths: 2..30): the gyro's aim (controls.cpp)
    int gyro_sens = 10;
    int language = 0, theme = THEME_HUNTER, menu_scale = 100;
    bool pause_background = true;
    bool updates = true;  // [Interface] Updates: the start looks for a newer release once a day (Notices.java)
    bool pause_menu = true;  // [Interface] PauseInMenu: the game waits while this menu is open, playing alone (android_main)
    // the touch controls (touch_layout.txt)
    int controls = 0, touch_opacity = 80, attack_style = 0;
    float camera = 1.0f;
    int camera_smooth = 0;  // the drag's turn smoothed, % (touch_layout.txt camera_smooth / 100; 0 off)
    std::vector<char> show = std::vector<char>(TB, 1);
    // a controller's (mh4u-app.ini's [Controls], controls.h): FACE_KEYS, STICK_KEYS, R3_KEYS by index; the camera's
    // speed and the sticks' dead zone in %
    int face = 0, right_stick = 0, stick_speed = 100, r3 = 0, deadzone = 10, town = 0;
    bool invert_x = false, invert_y = false;
    std::string combo[kCombos];  // the combined buttons: "A+X" (touch.cpp's combo1..combo15: the profile in use's)
    Keybinds binds;       // mh4u-app.ini's [Keybinds] (controls.h): the keyboard's and the mouse's
    int mouse_sens = 100; // [Mouse] MouseSensitivity, in hundredths (10..1000; the MHGU port's 0.1 to 10)
#ifndef __ANDROID__
    bool mouse_inv_x = false, mouse_inv_y = false;  // (the PC) [Mouse] InvertX, InvertY
    int button_icons = 0;                           // (the PC) [Controls] ButtonIcons, ICON_KEYS' index
#endif
    int combo_pad[kCombos] = {};  // and a controller's button for each: [Controls] Combo<n>Pad (controls.h)
    // mh4u-app.ini's [Network] (net_rooms.h): no room comes with the app; the lobby: empty the default (lobby_url); the
    // lobbies typed and saved, the last first ([Network] RecentLobby1..: kRecentLobbies of them, none of LOBBY_LIST's;
    // the same in the draft and as saved: taking one out is done at once, forget_lobby)
    std::string nick, address, password, lobby, host_port = "24872";
    std::vector<std::string> lobbies;
    int slots = 4;
    bool notices = true;
    // (1.0.1) hubs: PlayWith (PLAY_*: both as it comes, so the rooms of 1.0 stay where they were), the hub server (as the
    // lobby: empty the default) and the hub this phone creates (its room's port is HostPort's)
    int play_with = PLAY_BOTH;
    std::string hub_server, hub_name, hub_tags, hub_password;
    int hub_slots = 4;
    // the GPU driver for the next start (gpu_driver.h): its folder; empty: the system's
    std::string driver;
    // the Mods tab: the mods in load order, each on or off (load/mods/load_order.txt, content.h); the Cheats tab: each
    // cheat on or off, in the cheat engine's order (cheat_names)
    struct ModRow {
        std::string name;
        bool on = true;
        bool operator==(const ModRow&) const = default;
    };
    std::vector<ModRow> mods;
    std::vector<char> cheats;
#ifndef __ANDROID__
    PcDraft pc;  // (MH4U for PC's own: pc/settings_pc.h)
#endif
    bool operator==(const Draft&) const = default;
};
// What each mod holds, as last read (the draft keeps only their order and switches), and the cheats' names in the order
// of Draft::cheats
std::vector<mh4n::mods::Mod> g_mod_info;
std::vector<std::string> g_mods_found_off;  // (content_mods_found_off: added off from the game folder, a note says so)
std::vector<std::string> g_cheat_names;

// The options as the app comes (first_run.cpp writes the same into a new mh4u.ini, and android_main.cpp adds a key the
// app's own mh4u.ini lacks): what Reset puts back, and what a key the files lack reads as (load)
Draft defaults() {
    Draft d;
    d.fps = "60", d.widescreen = "match", d.hud_edges = "1", d.hud_size = "100";
    d.single_screen = kSingleScreenDefault;
    d.bloom = d.motion_blur = d.depth_of_field = d.radial_blur = d.haze = "on";
    d.dither = render_scale_used(d.scale, d.adreno_fix) > 1 ? "off" : "on";  // (a grid above 1x)
    d.antialiasing = "on", d.fov = "1";
    d.steady = "off";
    d.kick = "ZR", d.target = "ZL", d.skip = "ZR", d.skip_end = "ZL+ZR", d.skip_end_hold = "1.0";
    d.gather_all = "off";
    d.net_sync = "off";
    d.qol_gather = d.qol_flex = d.qol_sharpen = d.qol_rewards = "off";
    d.rewards_send_all = "off";
    d.dlc_flags = "on";
    d.effects = "on";
    d.monsters_on_map = d.damage_numbers = "off";  // (the owner's: in the app, off)
    d.monster_health_bar = "off";  // (the same)
    d.quick_items = "off";         // (the owner's: optional, off)
    d.bow_aim = "on";              // (the owner's 1.0 ask, S15: letting go of the shot aims at the monster by itself)
    for (int i = 0; i < TB; i++) d.show[i] = TOUCH_BUTTONS[i].on;
    d.binds = controls_default_binds();
    return d;
}
// The draft's colour filter as colour_filter.h has it
ColourAdjust colour_of(const Draft& d) {
    ColourAdjust a;
    a.hue = d.hue, a.temperature = d.temperature, a.tint = d.tint, a.brightness = d.brightness, a.contrast = d.contrast;
    a.saturation = d.saturation, a.gamma = d.gamma;
    return a;
}
// The field of view's factor in mh4u.ini's text (0.5 to 2, 1 when it is not a number)
float fov_of(const std::string& text) {
    const float f = (float)std::atof(text.c_str());
    return f > 0 ? std::clamp(std::round(f * 20) / 20.f, .5f, 2.f) : 1.f;
}
// The frame rate's cap in mh4u.ini's text (15 to 240 since 1.0.1, 30 before; the game's own 30 when it is not a number)
constexpr int kFpsMin = 15, kFpsMax = 240;
int fps_of(const std::string& text) {
    const int fps = (int)std::lround(std::atof(text.c_str()));
#ifndef __ANDROID__
    if (fps >= kPcFpsUnlimited) return kPcFpsUnlimited;  // (the PC's No limit)
#endif
    return fps > 0 ? std::clamp(fps, kFpsMin, kFpsMax) : 30;
}

bool is_on(const std::string& v) {
    return !strcasecmp(v.c_str(), "on") || v == "1" || !strcasecmp(v.c_str(), "true") || !strcasecmp(v.c_str(), "yes");
}
// a switch's new text in the file's own way of writing it: 1 / 0 where it had a number, else on / off
std::string onoff(const std::string& was, bool on) {
    return was == "0" || was == "1" ? (on ? "1" : "0") : (on ? "on" : "off");
}
int index_of(const char* const* keys, int n, const std::string& value) {
    for (int i = 0; i < n; i++)
        if (!strcasecmp(keys[i], value.c_str())) return i;
    return -1;
}
template <size_t N>
int index_of(const char* const (&keys)[N], const std::string& value) {
    return index_of(keys, (int)N, value);
}

// ---------------------------------------------------------------- the menu's state
// (Page: a page one level down from a section's, State::at; MENU.md)
enum class View { Main, Page, Buttons, ComboEdit, Keyboard, CloseChanges, Restart, Exit, Reset, RemoveDriver, RemoveMod, RemoveCheat,
                  Conflicts, PadList, PadMap, OpenSite, OpenDiscord, Profiles, ProfileSlot,
                  ColourPreset,  // (1.0.2: Graphics & Audio > Colour > Preset n)
                  Keybinds, KeyCapture, PadCapture, Chat, Friends, ChatOn,  // (the chat's: settings_chat.inc)
                  Verify };  // (System > Your game > Verify game files: verify_rows)
// View::Buttons opened from the Controller rows (playing with a controller): its combined buttons alone, as the MHGU
// port's where there are no touch controls
bool s_combos_alone = false;
struct Row;
void control_profile_row(std::vector<Row>& r);  // (the Controls tab's link to the control profiles: further down)
void button_images_row(std::vector<Row>& r);    // (...and to the buttons' pictures)
void keyboard_mouse_rows(std::vector<Row>& r);   // (S14: the keyboard and the mouse, further down)
struct Row {
    std::string id, title, value;
    std::string key, help;  // its option's name (the explanation: HELP), or an explanation of its own
    bool enabled = true, cold = false, action = false;
    bool header = false, current = false;  // a section's title (not a row to pick); the picker's choice it has now
    bool menu = false;                     // a tap lists its choices in the picker
    bool arrow = false;                    // an action or a submenu: an arrow at its right
    int toggle = -1;                       // On / Off (1 / 0; -1: not a switch): drawn as a switch
    bool number = false;                   // a number: - and + on the row (the controller: left / right)
    int grid = 0;                          // a key: this many to a line (the keyboard's), as a plate
    bool note = false;                     // a line of text (the room's chat, a reason): not a row to pick; its
                                           // value, when it has one, is who said it
    ImU32 ink = 0;                         // a note's colour, a row's value's (0: as the others)
    ImU32 mark = 0;                        // (a note) its diamond's colour alone, the text as the others' (0: ink's)
    bool sub = false;                      // (a note) an example under the note before it: further in, no diamond
    float progress = -1;                   // (a note) 0..1: a bar under its text, filled that far (Verify game files)
    unsigned who_ink = 0;                  // (1.0.2, a note) who said it in a colour of their own (chat::name_ink); 0: gold
    std::function<void(float)> slide;      // (1.0.2) a number with a bar under it, progress its fill: a tap or a drag on
                                           // the bar sets it there (0 to 1), as - and + step it
    std::function<void(int)> adjust;
    std::function<void()> activate;
    std::function<void()> remove;          // a picker's remembered choice: the x at its right takes it out of the list
};
// The text being typed (View::Keyboard): the keys a filter lets in, at most `capacity` bytes; commit takes it (or
// says why not, and the view stays)
enum { KB_ANY, KB_NICK, KB_ADDRESS, KB_DIGITS, KB_SECRET, KB_CHAT };
struct Keyboard {
    std::string title, hint, buffer;
    size_t cursor = 0, capacity = 255;
    int filter = KB_ANY;
    bool upper = false, symbols = false;
    std::function<bool(const std::string&, std::string&)> commit;
};
// A row on the screen last frame (the keyboard's keys: the controller moves between them as they lie)
struct Target {
    std::string id;
    ImVec2 center;
    bool enabled;
};
struct Origin {
    View view;
    std::string focus;
    float scroll;
    std::string at;  // (View::Page: which)
};
// A row's choices, in the picker over the menu: a tap picks, a tap away from it, its X or Back closes it
struct Dropdown {
    bool open = false, hovered = false;
    std::string row, title;  // the row it belongs to, and its title over the choices
    std::vector<std::string> names;
    int selected = 0;
    std::function<void(int)> set;
    std::vector<char> removable;      // the choices remembered (the lobbies typed): an x at the right of each
    std::function<void(int)> remove;  // takes one of them out of its list
    std::string help;                 // its row's help, shown over the choices by its "?"
    bool show_help = false;
};
// The sizes, from the screen's height (touch_panel, each frame): a row, a tab and a button about 0.09 of it
struct Metrics {
    float H = 720, f = 16, row = 40, pad = 8, line = 2;
};
// A finger dragging a list and the glide after it lets go
struct Drag {
    ImGuiID window = 0;
    float from = 0, scroll = 0, last = 0, velocity = 0;
    bool pressed = false, moving = false;
};
struct State {
    Draft draft, saved, boot;
    bool editing = false, boot_set = false, focus_request = true, saved_notice = false, save_request = false;
    int page = 0;              // the section (page_ids)
    View view = View::Main;
    std::string at;            // View::Page: the page one level down shown ("graphics/screen")
    std::string help_open;     // the row whose help is open under it (its "?")
    bool help_scroll = false;  // (it just opened: the list scrolls to show it)
    std::string reset_at;      // the page View::Reset puts back (a section's own: all of its pages)
    std::string focus, error;
    std::vector<Origin> origins;
    Dropdown dropdown;
    Keyboard keyboard;
    std::vector<Target> targets;
    unsigned seen_line = 0;  // the room's last line shown in the menu (no notice over the game for it)
    std::string lobby_asked;  // the lobby the Multiplayer tab asked by itself since the menu opened
    bool restore_scroll = false;
    float scroll = 0;
    int focus_scroll_frames = 0;
    bool swallow = false;  // the press under way does nothing when it lets go (it closed the picker, began a drag)
    bool typed_enter = false;  // this frame's Enter accepted the keyboard's text
    ImGuiID list = 0;
    Metrics m;
    Drag drag;
    bool closing = false;  // (Customize the touch controls: the editor opens as the menu closes)
    // the GPU driver's section: what the last import or removal came to (bad: a failure), the driver to remove (its
    // question), a failure at the start already told
    std::string driver_note;
    bool driver_note_bad = false, failure_told = false;
    std::string remove_folder, remove_name, failure_text;
    // the Mods and Cheats tabs: what the last import or removal came to; a code mod just added, whose switch waits for the
    // code library's answer; the mod or the cheat to remove (its question)
    std::string mods_note, cheats_note, try_code_mod;
    bool mods_note_bad = false, cheats_note_bad = false;
    std::string remove_mod;
    size_t remove_cheat = 0;
    // the Mods tab's comparison of what the mods change (content_mods_scan), made when the tab first shows and again after
    // a mod is added or removed, or Rescan
    std::shared_ptr<ContentModScan> mod_scan;
    std::string pad_note;  // Map a controller: what the last save or forget came to
    // A tap counts only for a press that began a moment after the menu came up: the press on the settings button that
    // opened it reaches ImGui a frame or two late (the button lies under a tab), as the Back key's does (back_keys)
    double opened_at = 0;
    bool armed = false;
    // A remembered lobby taken out of the picker (its x): the picker of that row opened again, fresh, at that place
    std::string reopen;
    int reopen_at = 0;
} s;
std::atomic<bool> request_back{false}, request_close{false}, closed_now{false}, is_dirty{false};
// (1.0.2, after tabarong's floating chat) the chat window afloat as the frame ended (chat_floating, settings_panel.h), and
// typed in; what was asked of it (SETTINGS_CHAT_*); its controls' places this frame and the last (fractions of the
// screen: a finger there is the window's, settings_panel_chat_claims)
std::atomic<bool> g_chat_afloat{false}, g_chat_typed{false};
std::atomic<int> request_chat{0};
bool g_chat_hidden;  // (afloat, hidden this frame by a menu of the game's)
std::mutex g_claim_lock;
std::vector<ImRect> g_claims, g_claims_next;
bool* g_open = nullptr;  // (the menu's: its rows that close it, this frame's)

// ---------------------------------------------------------------- the menu's places (MENU.md; the MHGU port's the same)
// A page one level down: its id ("graphics/screen"), its title (English: tr), the page its row is on and where among
// that page's entries; shown (none: always) hides its row, link fills it in (a summary as its value, a help)
struct MenuPage {
    const char *id, *title, *parent;
    int order;
    std::function<bool()> shown;
    std::function<void(Row&)> link;
};
// An entry: rows on a page at an order; reset puts their options back as they come (Reset, whether they show or not);
// shown (none: always; cheap, nothing else) leaves them out, and a page with nothing else on it
struct MenuEntry {
    const char* page;
    int order;
    std::function<void(std::vector<Row>&)> rows;
    std::function<void()> reset;
    std::function<bool()> shown;
};
std::vector<MenuPage>& menu_pages() {
    static std::vector<MenuPage> pages;
    return pages;
}
std::vector<MenuEntry>& menu_entries() {
    static std::vector<MenuEntry> entries;
    return entries;
}
bool menu_page(const char* id, const char* title, const char* parent, int order, std::function<bool()> shown = {},
               std::function<void(Row&)> link = {}) {
    menu_pages().push_back({id, title, parent, order, std::move(shown), std::move(link)});
    return true;
}
bool menu_entry(const char* page, int order, std::function<void(std::vector<Row>&)> rows, std::function<void()> reset = {},
                std::function<bool()> shown = {}) {
    menu_entries().push_back({page, order, std::move(rows), std::move(reset), std::move(shown)});
    return true;
}
#define MENU_CAT2(a, b) a##b
#define MENU_CAT(a, b) MENU_CAT2(a, b)
#define MENU_PAGE(...) [[maybe_unused]] const bool MENU_CAT(menu_page_, __LINE__) = menu_page(__VA_ARGS__)
#define MENU_ENTRY(...) [[maybe_unused]] const bool MENU_CAT(menu_entry_, __LINE__) = menu_entry(__VA_ARGS__)
bool settled() { return ImGui::GetTime() - s.opened_at > .25; }
bool taps_ok() { return s.armed && !s.swallow; }
bool g_started = false;  // (the FPS counter set from the saved options, once)
}  // namespace
extern "C" volatile uint64_t g_present_count;  // (android_main.cpp: the game's frames shown)
namespace {
std::string g_notice_en, g_notice_es;  // settings_panel_notice's, shown from g_notice_since (s; -1: none; -2: from
double g_notice_since = -1;            // the game's first pictures: one told at the start would be gone before them)
double g_notice_seconds = 8;           // (how long: settings_panel_notice's)

// ---------------------------------------------------------------- language
bool phone_spanish() {
    static int known = -1;
    if (known < 0) {
        int n = 0;
        SDL_Locale** loc = SDL_GetPreferredLocales(&n);
        known = loc && n > 0 && loc[0]->language && !SDL_strncasecmp(loc[0]->language, "es", 2);
        SDL_free(loc);
    }
    return known == 1;
}
bool spanish_for(int language) { return language == 2 || (language == 0 && phone_spanish()); }
bool spanish() { return spanish_for(s.draft.language); }
std::string help_of(const char* key);  // (an explanation by its key: HELP, below)
struct Row;
std::string help_text(const Row& r);  // (a row's: its own, or HELP's by its key)
#ifndef __ANDROID__
// (the PC: the words of a PC - a click, the PC, its network: pc_words, pc/pc_input.cpp)
const char* tr(const char* text) { return pc_words(settings_text(spanish() ? 1 : 0, text)); }
#else
const char* tr(const char* text) { return settings_text(spanish() ? 1 : 0, text); }
#endif
// (a row's own words in English and Spanish, kept beside the row: the 1.0 options' texts live with their rows, so each one
// moves as a whole when the menu is arranged again)
#ifndef __ANDROID__
const char* en_es(const char* en, const char* es) { return pc_words(spanish() ? es : en); }
#else
const char* en_es(const char* en, const char* es) { return spanish() ? es : en; }
#endif
std::string fmt(const char* text, ...) {
    char buffer[512];
    va_list args;
    va_start(args, text);
    vsnprintf(buffer, sizeof buffer, tr(text), args);
    va_end(args);
    return buffer;
}

// ---------------------------------------------------------------- the look
// Hunter, the game's own windows (the item pouch): a wine-dark panel, its rows split by lines and the picked one lit
// gold; Gold, a warm brown panel; Blue, a cool one. ([Interface] Theme; Hunter as it comes)
struct Theme {
    ImU32 panel, edge, accent, accent_bright, text, text_dim, text_off, warn, divider, popup;
    float rounding;
    bool game;  // Hunter: the game's window frame (draw_frame), lines between the rows
};
const Theme& theme() {
    static const Theme hunter = {IM_COL32(50, 25, 24, 252),   IM_COL32(0, 0, 0, 255),       IM_COL32(236, 212, 44, 255),
                                 IM_COL32(255, 232, 84, 255), IM_COL32(242, 238, 232, 255), IM_COL32(196, 176, 166, 255),
                                 IM_COL32(132, 112, 106, 255), IM_COL32(255, 96, 72, 255),   IM_COL32(96, 88, 88, 255),
                                 IM_COL32(56, 28, 27, 250),   0.f,                          true};
    static const Theme gold = {IM_COL32(34, 25, 19, 252),    IM_COL32(139, 97, 40, 255),   IM_COL32(213, 162, 79, 255),
                               IM_COL32(247, 226, 162, 255), IM_COL32(242, 216, 170, 255), IM_COL32(175, 150, 128, 255),
                               IM_COL32(124, 107, 90, 255),  IM_COL32(210, 106, 80, 255),  IM_COL32(139, 97, 40, 90),
                               IM_COL32(42, 31, 23, 252),    6.f,                          false};
    static const Theme blue = {IM_COL32(6, 10, 17, 252),     IM_COL32(110, 217, 255, 255), IM_COL32(111, 216, 255, 255),
                               IM_COL32(111, 216, 255, 255), IM_COL32(244, 247, 252, 255), IM_COL32(178, 192, 208, 255),
                               IM_COL32(132, 145, 160, 255), IM_COL32(255, 184, 145, 255), IM_COL32(52, 72, 94, 255),
                               IM_COL32(13, 20, 31, 255),    3.f,                          false};
    return s.draft.theme == THEME_GOLD ? gold : s.draft.theme == THEME_BLUE ? blue : hunter;
}
ImVec4 vec(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }
ImU32 fade(ImU32 c, float a) {
    return (c & 0x00ffffffu) | ((ImU32)std::clamp(((c >> 24) & 0xff) * a, 0.f, 255.f) << 24);
}
// The touch controls' colours (Hunter): dark plates with a bronze edge and a gold rule, the picked row and tab lit ochre
// to gold as the game's cursor; the other themes' from their own
struct Look {
    ImU32 plate, edge, rule, lit, ink, dim, glow, bar0, bar1, band;
};
Look touch_look() {
    const Theme& t = theme();
    if (t.game)
        return {IM_COL32(28, 20, 15, 235),   IM_COL32(112, 76, 34, 255),  IM_COL32(222, 186, 96, 255),
                IM_COL32(92, 62, 22, 255),   IM_COL32(244, 232, 200, 255), IM_COL32(204, 186, 160, 255),
                IM_COL32(255, 214, 92, 255), IM_COL32(206, 158, 42, 240),  IM_COL32(122, 82, 22, 225),
                IM_COL32(74, 34, 31, 255)};
    return {t.popup, t.divider | IM_COL32_A_MASK, t.accent, fade(t.accent, .35f), t.text, t.text_dim, t.accent_bright,
            fade(t.accent, .55f), fade(t.accent, .25f), t.popup};
}
const ImU32 RULE_RED = IM_COL32(176, 34, 30, 255), RULE_GREEN = IM_COL32(80, 142, 54, 255);
// The serif the game's titles look like (ui.cpp loads it second), else the letters' own
ImFont* title_font() {
    ImGuiIO& io = ImGui::GetIO();
    return io.Fonts->Fonts.Size > 1 ? io.Fonts->Fonts[1] : ImGui::GetFont();
}
void diamond(ImDrawList* dl, ImVec2 c, float d, ImU32 col) {
    ImVec2 q[4] = {ImVec2(c.x, c.y - d), ImVec2(c.x + d, c.y), ImVec2(c.x, c.y + d), ImVec2(c.x - d, c.y)};
    dl->AddConvexPolyFilled(q, 4, col);
}
// Text with a dark shadow under it (readable on the gold of the picked row, as the game's)
void shadow_text(ImDrawList* dl, ImFont* font, float size, ImVec2 p, ImU32 col, const char* text, float wrap = 0) {
    float o = std::max(1.f, size * .06f);
    dl->AddText(font, size, ImVec2(p.x + o, p.y + o), fade(IM_COL32(0, 0, 0, 170), ((col >> 24) & 0xff) / 255.f), text,
                nullptr, wrap);
    dl->AddText(font, size, p, col, text, nullptr, wrap);
}
void shadow_text(ImDrawList* dl, float size, ImVec2 p, ImU32 col, const char* text, float wrap = 0) {
    shadow_text(dl, ImGui::GetFont(), size, p, col, text, wrap);
}
// A finger's button: a dark square plate with an edge and a rule inside (the touch controls' discs, made square), gold
// studs on its corners (Hunter); lit while held, faded while it does nothing
void plate(ImDrawList* dl, const ImRect& r, float line, bool held, bool enabled) {
    const Look k = touch_look();
    float a = enabled ? 1.f : .45f, in = line * 1.8f;
    dl->AddRectFilled(ImVec2(r.Min.x, r.Min.y + line), ImVec2(r.Max.x, r.Max.y + line), fade(IM_COL32(0, 0, 0, 110), a));
    dl->AddRectFilled(r.Min, r.Max, fade(held ? k.lit : k.plate, a));
    dl->AddRect(r.Min, r.Max, fade(k.edge, a), 0, line);
    dl->AddRect(ImVec2(r.Min.x + in, r.Min.y + in), ImVec2(r.Max.x - in, r.Max.y - in), fade(k.rule, a * .7f), 0,
                std::max(1.f, line * .45f));
    if (theme().game)
        for (ImVec2 c : {r.Min, ImVec2(r.Max.x, r.Min.y), ImVec2(r.Min.x, r.Max.y), r.Max}) diamond(dl, c, line * 1.5f, fade(k.rule, a));
    if (held)
        dl->AddRect(ImVec2(r.Min.x - line, r.Min.y - line), ImVec2(r.Max.x + line, r.Max.y + line), fade(k.glow, .7f), 0, line);
}
// The glyphs on the buttons: an arrow (dir -1: back), a cross (close), - and +
void chevron(ImDrawList* dl, ImVec2 c, float r, int dir, ImU32 col, float thick) {
    dl->PathLineTo(ImVec2(c.x - dir * r * .5f, c.y - r));
    dl->PathLineTo(ImVec2(c.x + dir * r * .5f, c.y));
    dl->PathLineTo(ImVec2(c.x - dir * r * .5f, c.y + r));
    dl->PathStroke(col, thick);
}
void cross(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float thick) {
    dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, thick);
    dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), col, thick);
}
void sign(ImDrawList* dl, ImVec2 c, float r, bool plus, ImU32 col, float thick) {
    dl->AddRectFilled(ImVec2(c.x - r, c.y - thick * .5f), ImVec2(c.x + r, c.y + thick * .5f), col);
    if (plus) dl->AddRectFilled(ImVec2(c.x - thick * .5f, c.y - r), ImVec2(c.x + thick * .5f, c.y + r), col);
}
// The header's button to the project's page: GitHub's mark, a disc with the cat cut out of it, drawn here in the
// glyphs' colour (no artwork, no brand colours; the same in the MHGU port). r: the disc's radius
void github_mark(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    // (the cat's outline in the disc's radius, y down: from the right of its body, where it leaves the disc at the
    // bottom, up over the head and the ears and down the left with its arm; the disc's edge closes it from the left of
    // the body round to the right, clockwise as ImGui fills: one shape, its edges smoothed)
    static const float cat[][2] = {
        {.25f, .72f}, {.244f, .575f}, {.22f, .5f}, {.18f, .44f}, {.263f, .428f}, {.345f, .405f},
        {.422f, .375f}, {.49f, .33f}, {.55f, .265f}, {.595f, .18f}, {.625f, .077f}, {.635f, -.05f},
        {.61f, -.2f}, {.53f, -.32f}, {.552f, -.411f}, {.549f, -.49f}, {.52f, -.585f}, {.457f, -.583f},
        {.371f, -.554f}, {.245f, -.485f}, {.125f, -.51f}, {0, -.52f}, {-.125f, -.51f}, {-.245f, -.485f},
        {-.371f, -.554f}, {-.457f, -.583f}, {-.52f, -.585f}, {-.549f, -.49f}, {-.552f, -.411f}, {-.53f, -.32f},
        {-.61f, -.2f}, {-.635f, -.05f}, {-.625f, .077f}, {-.595f, .18f}, {-.55f, .265f}, {-.49f, .33f},
        {-.422f, .375f}, {-.345f, .405f}, {-.263f, .428f}, {-.18f, .44f}, {-.22f, .5f}, {-.244f, .575f},
        {-.3f, .594f}, {-.377f, .6f}, {-.46f, .573f}, {-.535f, .49f}, {-.59f, .43f}, {-.69f, .39f},
        {-.733f, .416f}, {-.6875f, .456f}, {-.624f, .525f}, {-.585f, .598f}, {-.56f, .646f}, {-.5f, .695f},
        {-.4f, .724f}, {-.25f, .715f}};
    const float bx = .25f, by = std::sqrt(1 - bx * bx);  // (where the body leaves the disc)
    dl->PathArcTo(c, r, std::atan2(by, -bx), std::atan2(by, bx) + 2 * IM_PI, 40);
    for (const auto& p : cat) dl->PathLineTo(ImVec2(c.x + p[0] * r, c.y + p[1] * r));
    dl->PathFillConcave(col);
}
// The header's button to the community's Discord: Discord's mark, the controller-shaped face with its two eyes, drawn
// like GitHub's in the glyphs' colour (no artwork, no brand colours; the same in the MHGU port). r: as GitHub's disc's
// radius; the face a little wider than that disc and shorter
void discord_mark(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    // (the face's left half in its half-width, y down: from the middle of its top along the top and down the left side
    // to the eyes' line (upper), then on round the left horn and along the mouth to the middle of its bottom (lower); the
    // right half mirrors it. ImGui fills no holes, so the face is two shapes, above and below the eyes' line, each with
    // its halves of the eyes cut out of that edge, clockwise as ImGui fills; the upper one reaches a pixel below the
    // line, so no seam shows between them)
    static const float upper[][2] = {{0, -.668f},    {-.229f, -.65f},  {-.282f, -.758f}, {-.493f, -.708f}, {-.695f, -.631f},
                                     {-.8f, -.457f}, {-.881f, -.285f}, {-.94f, -.115f},  {-.981f, .076f}};
    static const float lower[][2] = {{-.981f, .076f}, {-.999f, .28f},  {-.992f, .504f}, {-.882f, .578f}, {-.766f, .644f},
                                     {-.628f, .707f}, {-.485f, .758f}, {-.377f, .583f}, {-.548f, .502f}, {-.506f, .47f},
                                     {-.393f, .516f}, {-.259f, .555f}, {-.122f, .577f}, {0, .583f}};
    const int nu = IM_ARRAYSIZE(upper), nl = IM_ARRAYSIZE(lower);
    const float w = r * 1.08f, ex = .332f * w, ey = .076f * w, rx = .18f * w, ry = .2f * w, under = 1;
    auto at = [&](float x, float y) { return ImVec2(c.x + x * w, c.y + y * w); };
    for (int i = nu - 1; i >= 0; i--) dl->PathLineTo(at(upper[i][0], upper[i][1]));  // (up the left side and along the top,
    for (int i = 1; i < nu; i++) dl->PathLineTo(at(-upper[i][0], upper[i][1]));       // then down the right side)
    dl->PathLineTo(ImVec2(c.x - upper[nu - 1][0] * w, c.y + ey + under));
    for (float x : {ex, -ex}) {  // (back along the line, over the top of each eye)
        dl->PathLineTo(ImVec2(c.x + x + rx, c.y + ey + under));
        dl->PathEllipticalArcTo(ImVec2(c.x + x, c.y + ey), ImVec2(rx, ry), 0, 0, -IM_PI);
        dl->PathLineTo(ImVec2(c.x + x - rx, c.y + ey + under));
    }
    dl->PathLineTo(ImVec2(c.x + upper[nu - 1][0] * w, c.y + ey + under));
    dl->PathFillConcave(col);
    for (float x : {-ex, ex})  // (along the line, under each eye, then down the right side and back round the left)
        dl->PathEllipticalArcTo(ImVec2(c.x + x, c.y + ey), ImVec2(rx, ry), 0, IM_PI, 0);
    for (int i = 0; i < nl; i++) dl->PathLineTo(at(-lower[i][0], lower[i][1]));
    for (int i = nl - 2; i >= 0; i--) dl->PathLineTo(at(lower[i][0], lower[i][1]));
    dl->PathFillConcave(col);
}
// The project's official page and its community's Discord, the only links the project gives: in the phone's browser
// (SDL_OpenURL: an Android VIEW intent, which the Discord app takes if it is there), from the header's GitHub and
// Discord buttons once View::OpenSite's or View::OpenDiscord's Open says so (the browser can make Android close the
// game on a phone with little memory). With nothing to open it (a handheld or a TV box with no browser) the menu says
// so, as in the MHGU port
constexpr const char* OFFICIAL_PAGE = "https://github.com/khorne-hub/4u-native";
constexpr const char* DISCORD_INVITE = "https://discord.gg/sk5xEjEMwF";  // (an invite that never expires)
constexpr const char* NO_BROWSER = "No app here can open web pages.";
void open_link(const char* what, const char* url) {
    if (SDL_OpenURL(url)) {
        std::fprintf(stderr, "[Info] App: the %s went to the browser (%s)\n", what, url);
        if (s.error == tr(NO_BROWSER)) s.error.clear();
    } else {
        std::fprintf(stderr, "[Info] App: the %s could not be opened (%s): %s\n", what, url, SDL_GetError());
        s.error = tr(NO_BROWSER);
    }
}
void open_official_page() { open_link("official page", OFFICIAL_PAGE); }
void open_discord() { open_link("community's Discord", DISCORD_INVITE); }
// On / Off: a slot whose stud slides to the right and lights it green when on
void switch_mark(ImDrawList* dl, const ImRect& r, bool on) {
    const Look k = touch_look();
    float line = s.m.line, in = line * 1.3f, side = r.GetHeight() - in * 2;
    dl->AddRectFilled(r.Min, r.Max, on ? IM_COL32(58, 112, 40, 255) : IM_COL32(18, 12, 10, 230));
    dl->AddRect(r.Min, r.Max, on ? k.rule : k.edge, 0, line * .7f);
    float x = on ? r.Max.x - in - side : r.Min.x + in;
    ImRect knob(x, r.Min.y + in, x + side, r.Max.y - in);
    dl->AddRectFilled(knob.Min, knob.Max, on ? k.glow : IM_COL32(128, 108, 92, 255));
    dl->AddRect(knob.Min, knob.Max, IM_COL32(0, 0, 0, 170), 0, std::max(1.f, line * .4f));
    diamond(dl, knob.GetCenter(), side * .2f, on ? IM_COL32(28, 20, 15, 255) : IM_COL32(60, 48, 40, 255));
}
// The sections' marks in the column at the left (the MHGU port's the same), drawn as lines in col at c, about size
// across: a controller, an eye, two crossed swords, two hunters, the smithy's hammer, a cog and a scroll
void section_icon(ImDrawList* dl, int section, ImVec2 c, float size, ImU32 col) {
    const float w = std::max(1.5f, size * .085f), h = size * .5f;
    auto at = [&](float x, float y) { return ImVec2(c.x + x * h, c.y + y * h); };
    switch (section) {
    case 0:  // (Controls: a controller, its d-pad and two buttons)
        dl->AddRect(at(-1, -.56f), at(1, .6f), col, h * .42f, w);
        dl->AddLine(at(-.66f, 0), at(-.26f, 0), col, w);
        dl->AddLine(at(-.46f, -.2f), at(-.46f, .2f), col, w);
        dl->AddCircleFilled(at(.36f, -.1f), w * .95f, col);
        dl->AddCircleFilled(at(.6f, .12f), w * .95f, col);
        break;
    case 1: {  // (Graphics & Audio: an eye)
        const float a0 = IM_PI * 1.17f, a1 = IM_PI * 1.83f;
        dl->PathArcTo(at(0, .7f), h * 1.08f, a0, a1, 16);
        dl->PathStroke(col, w);
        dl->PathArcTo(at(0, -.7f), h * 1.08f, IM_PI * 2 - a1, IM_PI * 2 - a0, 16);
        dl->PathStroke(col, w);
        dl->AddCircle(c, h * .3f, col, 0, w);
        diamond(dl, c, h * .12f, col);
        break;
    }
    case 2:  // (Gameplay: two swords crossed, their guards and pommels)
        for (int side = -1; side <= 1; side += 2) {
            dl->AddLine(at(-.8f * side, -.8f), at(.56f * side, .56f), col, w);
            dl->AddLine(at(.3f * side, .74f), at(.74f * side, .3f), col, w);
            dl->AddCircleFilled(at(.76f * side, .76f), w * 1.1f, col);
        }
        break;
    case 3:  // (Multiplayer: two hunters side by side)
        for (float x : {-.42f, .42f}) {
            dl->AddCircle(at(x, -.42f), h * .24f, col, 0, w);
            dl->PathArcTo(at(x, .62f), h * .48f, IM_PI, IM_PI * 2, 12);
            dl->PathStroke(col, w);
        }
        break;
    case 4: {  // (Mods & Cheats: the smithy's hammer)
        ImVec2 head[5] = {at(-.86f, -.78f), at(.2f, -.78f), at(.42f, -.52f), at(.2f, -.26f), at(-.86f, -.26f)};
        dl->AddPolyline(head, 5, col, w, ImDrawFlags_Closed);
        dl->AddLine(at(-.3f, -.26f), at(.5f, .86f), col, w * 1.5f);
        break;
    }
    case 5: {  // (System: a cog)
        ImVec2 teeth[32];
        for (int i = 0; i < 32; i++) {
            float a = (i + .5f) / 32 * IM_PI * 2, r = ((i / 2) % 2 == 0) ? .82f : .58f;
            teeth[i] = at(std::cos(a) * r, std::sin(a) * r);
        }
        dl->AddPolyline(teeth, 32, col, w, ImDrawFlags_Closed);
        dl->AddCircle(c, h * .22f, col, 0, w);
        break;
    }
    default:  // (Credits & Licenses: a scroll, rolled at two corners, its lines)
        dl->AddRect(at(-.6f, -.66f), at(.6f, .66f), col, 0, w);
        dl->AddCircle(at(-.6f, -.66f), h * .16f, col, 0, w);
        dl->AddCircle(at(.6f, .66f), h * .16f, col, 0, w);
        for (int i = 0; i < 3; i++) dl->AddLine(at(-.34f, -.3f + i * .3f), at(.34f, -.3f + i * .3f), col, std::max(1.f, w * .7f));
        break;
    }
}
// A row's help, the "?" at its right: a small seal, a dark disc with a bronze rim and a gold ring, its mark in the
// titles' serif; lit while its help is open
void help_seal(ImDrawList* dl, ImVec2 c, float r, bool open, bool held) {
    const Look k = touch_look();
    float line = std::max(1.5f, s.m.line * .8f);
    dl->AddCircleFilled(ImVec2(c.x, c.y + line), r, IM_COL32(0, 0, 0, 110));
    dl->AddCircleFilled(c, r, held || open ? k.lit : k.plate);
    dl->AddCircle(c, r, k.edge, 0, line);
    dl->AddCircle(c, r - line * 1.7f, fade(k.rule, open ? .9f : .55f), 0, std::max(1.f, line * .5f));
    ImFont* tf = title_font();
    float size = std::floor(r * 1.25f);
    ImVec2 ts = tf->CalcTextSizeA(size, FLT_MAX, 0, "?");
    shadow_text(dl, tf, size, ImVec2(c.x - ts.x * .5f, c.y - ts.y * .52f), open || held ? k.glow : k.rule, "?");
}
// Hunter's window frame, over the rest: a red rule with a green one inside it along the top and the bottom, and a gold
// stud on each corner
void draw_frame(ImGuiWindow* w) {
    if (!theme().game) return;
    const ImU32 gold = IM_COL32(222, 190, 40, 255);
    float b = w->WindowBorderSize, x0 = w->Pos.x + b, x1 = w->Pos.x + w->Size.x - b, y0 = w->Pos.y + b,
          y1 = w->Pos.y + w->Size.y - b, line = std::max(2.f, ImGui::GetFontSize() * .14f);
    ImDrawList* dl = w->DrawList;
    dl->PushClipRectFullScreen();
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y0 + line), RULE_RED);
    dl->AddRectFilled(ImVec2(x0, y0 + line), ImVec2(x1, y0 + line * 1.7f), RULE_GREEN);
    dl->AddRectFilled(ImVec2(x0, y1 - line * 1.7f), ImVec2(x1, y1 - line), RULE_GREEN);
    dl->AddRectFilled(ImVec2(x0, y1 - line), ImVec2(x1, y1), RULE_RED);
    float c = ImGui::GetFontSize() * .5f;
    for (ImVec2 corner : {w->Pos, ImVec2(w->Pos.x + w->Size.x, w->Pos.y), ImVec2(w->Pos.x, w->Pos.y + w->Size.y),
                          ImVec2(w->Pos.x + w->Size.x, w->Pos.y + w->Size.y)}) {
        ImVec2 a(corner.x - c * .5f, corner.y - c * .5f), z(corner.x + c * .5f, corner.y + c * .5f);
        dl->AddRectFilled(a, z, IM_COL32(0, 0, 0, 255));
        dl->AddRect(ImVec2(a.x + 1, a.y + 1), ImVec2(z.x - 1, z.y - 1), gold, 0, std::max(1.f, c * .16f));
        dl->AddRectFilled(ImVec2(corner.x - c * .14f, corner.y - c * .14f), ImVec2(corner.x + c * .14f, corner.y + c * .14f), gold);
    }
    dl->PopClipRect();
}

// ---------------------------------------------------------------- the draft
// [Network] RecentLobby1..: the lobbies typed and saved, as written (doubles, Off and the short list's left out)
std::vector<std::string> saved_lobbies() {
    std::vector<std::string> out;
    for (int i = 1; i <= kRecentLobbies; i++) {
        const std::string v = app_setting("Network", ("RecentLobby" + std::to_string(i)).c_str());
        if (typed_lobby(v) && !has_lobby(out, v)) out.push_back(v);
    }
    return out;
}
// The options as they are now: mh4u.ini's, mh4u-app.ini's, the touch controls'
Draft load() {
    const Draft f = defaults();  // (a key the files lack: as the app comes, which android_main.cpp makes it at the start)
    Draft d = f;
    d.fps = game_option_get("fps", f.fps);
    d.steady = game_option_get("steady", f.steady);
    d.widescreen = game_option_get("widescreen", f.widescreen);
    d.hud_edges = game_option_get("hud_edges", f.hud_edges);
    d.hud_size = game_option_get("hud_size", f.hud_size);
    d.single_screen = game_option_get("single_screen", f.single_screen);
    d.bloom = game_option_get("bloom", f.bloom);
    d.motion_blur = game_option_get("motion_blur", f.motion_blur);
    d.depth_of_field = game_option_get("depth_of_field", f.depth_of_field);
    d.radial_blur = game_option_get("radial_blur", f.radial_blur);
    d.haze = game_option_get("haze", f.haze);
    d.antialiasing = game_option_get("antialiasing", f.antialiasing);
    d.fov = game_option_get("fov", f.fov);
    d.kick = game_option_get("kick", f.kick);
    d.target = game_option_get("target", f.target);
    d.skip = game_option_get("skip", f.skip);
    d.skip_end = game_option_get("skip_end", f.skip_end);
    d.skip_end_hold = game_option_get("skip_end_hold", f.skip_end_hold);
    d.qol_gather = game_option_get("qol_hold_gather", f.qol_gather);
    d.gather_all = game_option_get("gather_all", f.gather_all);
    d.net_sync = game_option_get("net_sync", f.net_sync);
    d.qol_flex = game_option_get("qol_flex_cancel", f.qol_flex);
    d.qol_sharpen = game_option_get("qol_sharpen_cancel", f.qol_sharpen);
    d.qol_rewards = game_option_get("qol_rewards_no_limit", f.qol_rewards);
    d.rewards_send_all = game_option_get("rewards_send_all", f.rewards_send_all);
    d.dlc_flags = game_option_get("dlc_flags", f.dlc_flags);
    d.effects = game_option_get("effects", f.effects);
    d.monsters_on_map = game_option_get("monsters_on_map", f.monsters_on_map);
    d.damage_numbers = game_option_get("damage_numbers", f.damage_numbers);
    d.monster_health_bar = game_option_get("monster_health_bar", f.monster_health_bar);
    d.quick_items = game_option_get("quick_items", f.quick_items);
    d.bow_aim = game_option_get("bow_aim", f.bow_aim);
    d.scale = std::clamp(app_setting_int("Graphics", "RenderScale", kRenderScaleDefault), 0, 100);
    d.adreno_fix = app_adreno_fix();
    d.dither = game_option_get("dither", render_scale_used(d.scale, d.adreno_fix) > 1 ? "off" : "on");  // (android_main.cpp)
    d.low_end = app_low_end_level(app_setting("Graphics", "LowEnd", "off"));
    d.stats = app_setting_bool("Graphics", "FpsCounter", false);
    d.shadows = app_setting_bool("Graphics", "Shadows", true);
    d.hd_half = app_setting("Graphics", "HdPackSize", "full") == "half";
    d.number_size = app_setting("Graphics", "DamageNumberSize", "medium");
    d.number_colour = app_setting("Graphics", "DamageNumberColour", "white");
    d.number_style = app_setting("Graphics", "DamageNumberStyle", "classic");
    d.frame_gen = app_setting_bool("Graphics", "FrameGen", false);
    d.ps1_filter = app_setting_bool("Graphics", "Ps1Filter", false);
    d.fsr1 = !strcasecmp(app_setting("Graphics", "Upscale", "smooth").c_str(), "fsr1");
    d.fsr1_sharpness = std::clamp(app_setting_int("Graphics", "Fsr1Sharpness", 80), 0, 100);
    d.text_outline = app_setting_bool("Graphics", "TextOutline", false);
    d.fast_top = app_setting_bool("Graphics", "FastTopScreen", false);
#ifndef __ANDROID__
    pc_draft_load(d.pc);
#endif
    d.volume = std::clamp(app_setting_int("Audio", "Volume", 100), 0, 100);
    d.reverb = app_setting_bool("Audio", "Reverb", true);
    d.rec_audio = app_setting_bool("Audio", "ScreenRecording", false);
    {
        ColourAdjust colour;
        app_colour(&colour);
        d.hue = colour.hue, d.temperature = colour.temperature, d.tint = colour.tint, d.brightness = colour.brightness;
        d.contrast = colour.contrast, d.saturation = colour.saturation, d.gamma = colour.gamma;
    }
    d.screen_shown = ui_bottom_wanted();  // (as it is now: its button on the screen changes it too)
    d.layout = std::max(0, index_of(LAYOUT_KEYS, app_setting("Display", "Layout", "overlay")));
    d.layout_before = std::clamp(index_of(LAYOUT_KEYS, app_setting("Display", "LayoutBefore", "overlay")), 0, 2);
    d.corner = std::max(0, index_of(CORNER_KEYS, app_setting("Display", "Corner", "bottom-right")));
    d.size = std::clamp(app_setting_int("Display", "Size", 42), 20, 60);
    d.opacity = std::clamp(app_setting_int("Display", "Opacity", 100), 30, 100);
    d.haptics = app_setting_bool("Controls", "Haptics", true);
    d.long_press_hide = app_setting_bool("Controls", "LongPressHide", true);
    d.face = std::max(0, index_of(FACE_KEYS, app_setting("Controls", "FaceButtons", "position")));
    d.right_stick = std::max(0, index_of(STICK_KEYS, app_setting("Controls", "RightStick", "camera")));
    d.stick_speed = std::clamp(app_setting_int("Controls", "StickSpeed", 100) / 25 * 25, 25, 300);
    d.invert_x = app_setting_bool("Controls", "InvertX", false);
    d.invert_y = app_setting_bool("Controls", "InvertY", false);
    d.r3 = std::max(0, index_of(R3_KEYS, app_setting("Controls", "R3", "lockon")));
    d.town = std::max(0, index_of(TOWN_KEYS, app_setting("Controls", "TownMenu", "select")));
    d.deadzone = std::clamp(app_setting_int("Controls", "Deadzone", 10), 0, 30);
    d.lock_on = app_setting_bool("Controls", "LockOn", true);
    d.gyro_aim = app_setting_bool("Controls", "GyroAim", false);
    d.gyro_sens = std::clamp((int)std::lround(std::strtof(app_setting("Controls", "GyroSensitivity", "1.0").c_str(), nullptr) * 10), 2, 30);
    d.binds = controls_read_binds();
    d.mouse_sens = std::clamp((int)std::lround(std::strtof(app_setting("Mouse", "MouseSensitivity", "1.0").c_str(), nullptr) * 100), 10, 1000);
#ifndef __ANDROID__
    d.mouse_inv_x = app_setting_bool("Mouse", "InvertX", false), d.mouse_inv_y = app_setting_bool("Mouse", "InvertY", false);
    d.button_icons = std::max(0, index_of(ICON_KEYS, app_setting("Controls", "ButtonIcons", "auto")));
#endif
    d.language = std::max(0, index_of(LANGUAGE_KEYS, app_setting("Interface", "Language", "auto")));
    d.theme = std::max(0, index_of(THEME_KEYS, app_setting("Interface", "Theme", "hunter")));
    d.menu_scale = std::clamp(app_setting_int("Interface", "Scale", 100), 50, 200);
    d.pause_background = app_setting_bool("Interface", "PauseInBackground", true);
    d.updates = app_setting_bool("Interface", "Updates", true);
    d.pause_menu = app_setting_bool("Interface", "PauseInMenu", true);
    std::string v = touch_setting("controls");
    d.controls = std::max(0, index_of(CONTROLS_KEYS, v));
    v = touch_setting("opacity");
    if (!v.empty()) d.touch_opacity = std::clamp((int)std::lround(std::atof(v.c_str()) * 100), 20, 100);
    v = touch_setting("camera_speed");
    if (!v.empty()) d.camera = std::clamp((float)std::round(std::atof(v.c_str()) * 20) / 20.f, .2f, 5.f);
    v = touch_setting("camera_smooth");
    if (!v.empty()) d.camera_smooth = std::clamp((int)std::lround(std::atof(v.c_str()) * 10) * 10, 0, 100);
    v = touch_setting("attack_style");
    if (!v.empty()) d.attack_style = std::clamp(std::atoi(v.c_str()), 0, 2);
    for (int i = 0; i < TB; i++) {
        v = touch_setting(std::string("show_") + TOUCH_BUTTONS[i].key);
        if (!v.empty()) d.show[i] = std::atoi(v.c_str()) != 0;
    }
    for (int k = 0; k < kCombos; k++) d.combo[k] = touch_setting("combo" + std::to_string(k + 1));
    for (int k = 0; k < kCombos; k++)
        d.combo_pad[k] = controls_combo_pad_value(app_setting("Controls", ("Combo" + std::to_string(k + 1) + "Pad").c_str(), "None"));
    d.nick = app_setting("Network", "Nickname");
    d.address = app_setting("Network", "Address");
    d.password = app_setting("Network", "Password");
    d.lobby = app_setting("Network", "Lobby");
    d.lobbies = saved_lobbies();
    d.host_port = std::to_string(std::clamp(app_setting_int("Network", "HostPort", rooms::kDefaultPort), 1, 65535));
    d.slots = std::clamp(app_setting_int("Network", "Slots", 4), 2, 16);
    d.notices = app_setting_bool("Network", "Notices", true);
    {
        const std::string with = app_setting("Network", "PlayWith", "both");
        d.play_with = !strcasecmp(with.c_str(), "hubs") ? PLAY_HUBS : !strcasecmp(with.c_str(), "rooms") ? PLAY_ROOMS : PLAY_BOTH;
    }
    d.hub_server = app_setting("Network", "HubServer");
    d.hub_name = app_setting("Network", "HubName");
    d.hub_tags = app_setting("Network", "HubTags");
    d.hub_password = app_setting("Network", "HubPassword");
    d.hub_slots = std::clamp(app_setting_int("Network", "HubSlots", 4), 2, 4);
    gpu_driver_reload();
    d.driver = gpu_driver_status().chosen;
    g_mod_info = content_mods();
    g_mods_found_off = content_mods_found_off();
    for (const auto& m : g_mod_info) d.mods.push_back({m.name, m.on});
    g_cheat_names.clear();
    for (const ContentCheat& c : content_cheats()) {
        d.cheats.push_back(c.on);
        g_cheat_names.push_back(c.name);
    }
    return d;
}

// The draft's mods as the runtime's list: each one's folder and what it holds, in the draft's order, with its switch
std::vector<mh4n::mods::Mod> draft_mods(const std::vector<Draft::ModRow>& rows) {
    std::vector<mh4n::mods::Mod> out;
    for (const Draft::ModRow& row : rows)
        for (const auto& m : g_mod_info)
            if (m.name == row.name) {
                out.push_back(m);
                out.back().on = row.on;
            }
    return out;
}
const mh4n::mods::Mod* mod_info(const std::string& name) {
    for (const auto& m : g_mod_info)
        if (m.name == name) return &m;
    return nullptr;
}

// [Network] as `d` has it (in memory: app_settings_save writes it)
void set_network(const Draft& d) {
    app_setting_set("Network", "Nickname", d.nick);
    app_setting_set("Network", "Address", d.address);
    app_setting_set("Network", "Password", d.password);
    app_setting_set("Network", "Lobby", d.lobby);
    for (int i = 0; i < kRecentLobbies; i++) {  // (only those there are)
        const std::string key = "RecentLobby" + std::to_string(i + 1);
        if (i < (int)d.lobbies.size()) app_setting_set("Network", key.c_str(), d.lobbies[i]);
        else app_setting_remove("Network", key.c_str());
    }
    app_setting_set("Network", "HostPort", d.host_port);
    app_setting_set_int("Network", "Slots", d.slots);
    app_setting_set("Network", "Notices", d.notices ? "on" : "off");
    app_setting_set("Network", "PlayWith", d.play_with == PLAY_HUBS ? "hubs" : d.play_with == PLAY_ROOMS ? "rooms" : "both");
    app_setting_set("Network", "HubServer", d.hub_server);
    app_setting_set("Network", "HubName", d.hub_name);
    app_setting_set("Network", "HubTags", d.hub_tags);
    app_setting_set("Network", "HubPassword", d.hub_password);
    app_setting_set_int("Network", "HubSlots", d.hub_slots);
}

bool dirty() { return !(s.draft == s.saved); }
// What needs the game to start again: the widescreen, the 3D resolution, the GPU driver, the mods
bool cold_differs(const Draft& a, const Draft& b) {
    return a.widescreen != b.widescreen || a.scale != b.scale || a.driver != b.driver || a.mods != b.mods;
}

// The frame rate's cap to the screen's refresh rate (GameActivity.setFrameCap) and to the performance hints
void frame_cap(float fps) {
    perf_hint_set_fps(fps);
    display_set_frame_rate(nullptr, fps);
#ifdef __ANDROID__
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return;
    jclass cls = env->GetObjectClass(activity);
    jmethodID id = env->GetStaticMethodID(cls, "setFrameCap", "(F)V");
    if (id) env->CallStaticVoidMethod(cls, id, static_cast<double>(fps));  // (a float goes as a double)
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
#endif
}

// [Graphics] FrameGen on or off to GameActivity.setFrameGen: the screen's refresh rate picked again (twice the FPS limit
// or more with it on, where the screen has such a rate)
void frame_gen_to_activity(bool on) {
#ifndef __ANDROID__
    (void)on;
#else
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return;
    jclass cls = env->GetObjectClass(activity);
    jmethodID id = env->GetStaticMethodID(cls, "setFrameGen", "(Z)V");
    if (id) env->CallStaticVoidMethod(cls, id, static_cast<jboolean>(on ? JNI_TRUE : JNI_FALSE));
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
#endif
}

void remember_typed(Draft& d, const Draft& before);  // (the lobbies typed and saved: with the Lobby row, further down)
bool save() {
    remember_typed(s.draft, s.saved);
    const Draft &d = s.draft, &o = s.saved;
    // the port's: each one changed (they apply on the game's thread at its next frame, or at the next start)
    const std::pair<const char*, std::string Draft::*> game[] = {
        {"fps", &Draft::fps},       {"steady", &Draft::steady},           {"widescreen", &Draft::widescreen},
        {"hud_edges", &Draft::hud_edges},           {"single_screen", &Draft::single_screen},
        {"hud_size", &Draft::hud_size},
        {"bloom", &Draft::bloom},   {"motion_blur", &Draft::motion_blur}, {"depth_of_field", &Draft::depth_of_field},
        {"radial_blur", &Draft::radial_blur}, {"haze", &Draft::haze},     {"dither", &Draft::dither},
        {"antialiasing", &Draft::antialiasing}, {"fov", &Draft::fov},
        {"kick", &Draft::kick},     {"target", &Draft::target},           {"skip", &Draft::skip},
        {"skip_end", &Draft::skip_end}, {"skip_end_hold", &Draft::skip_end_hold},
        {"gather_all", &Draft::gather_all},
        {"net_sync", &Draft::net_sync},
        {"qol_hold_gather", &Draft::qol_gather}, {"qol_flex_cancel", &Draft::qol_flex},
        {"qol_sharpen_cancel", &Draft::qol_sharpen}, {"qol_rewards_no_limit", &Draft::qol_rewards},
        {"rewards_send_all", &Draft::rewards_send_all}, {"dlc_flags", &Draft::dlc_flags}, {"effects", &Draft::effects},
        {"monsters_on_map", &Draft::monsters_on_map}, {"damage_numbers", &Draft::damage_numbers},
        {"monster_health_bar", &Draft::monster_health_bar}, {"quick_items", &Draft::quick_items},
        {"bow_aim", &Draft::bow_aim}};
    for (const auto& [key, field] : game)
        if (d.*field != o.*field) game_option_set(key, d.*field);
    // (1.0.2) the Burst style sums a run of hits on one monster in one number: mh4n does the sum
    if (d.number_style != o.number_style)
        game_option_set("damage_number_burst", d.number_style == "burst" ? "on" : "off");
    // (S45) the touch screen's corner follows single-screen mode turned on or off: hidden with the menus on the top
    // screen, shown again without them (as at the start, android_main.cpp)
    if (d.single_screen != o.single_screen) {
        ui_set_bottom_default(!is_on(d.single_screen));
        bool code_mod = false;  // (onescreen_code_mod's: a code mod on keeps them on the top screen, the row can't change)
        for (const auto& m : g_mod_info) code_mod |= m.on && m.code;
        if (!code_mod) ui_set_one_screen(is_on(d.single_screen) && content_title() == kContentTitle);
    }
    // the app's
    static const char* const LOW_END_KEYS[] = {"off", "on", "toaster", "brick"};
    app_setting_set_int("Graphics", "RenderScale", d.scale);
    if (d.adreno_fix != o.adreno_fix) app_setting_set("Graphics", "AdrenoFix", d.adreno_fix ? "auto" : "off");
    app_setting_set("Graphics", "LowEnd", LOW_END_KEYS[std::clamp(d.low_end, 0, 3)]);
    app_setting_set("Graphics", "FpsCounter", d.stats ? "on" : "off");
    app_setting_set("Graphics", "Shadows", d.shadows ? "on" : "off");
    if (d.hd_half != o.hd_half) app_setting_set("Graphics", "HdPackSize", d.hd_half ? "half" : "full");
    if (d.number_size != o.number_size) app_setting_set("Graphics", "DamageNumberSize", d.number_size);
    if (d.number_colour != o.number_colour) app_setting_set("Graphics", "DamageNumberColour", d.number_colour);
    if (d.number_style != o.number_style) app_setting_set("Graphics", "DamageNumberStyle", d.number_style);
    if (d.frame_gen != o.frame_gen) app_setting_set("Graphics", "FrameGen", d.frame_gen ? "on" : "off");
    if (d.ps1_filter != o.ps1_filter) app_setting_set("Graphics", "Ps1Filter", d.ps1_filter ? "on" : "off");
    if (d.fsr1 != o.fsr1) app_setting_set("Graphics", "Upscale", d.fsr1 ? "fsr1" : "smooth");
    if (d.fsr1_sharpness != o.fsr1_sharpness) app_setting_set_int("Graphics", "Fsr1Sharpness", d.fsr1_sharpness);
    if (d.text_outline != o.text_outline) app_setting_set("Graphics", "TextOutline", d.text_outline ? "on" : "off");
    if (d.fast_top != o.fast_top) app_setting_set("Graphics", "FastTopScreen", d.fast_top ? "on" : "off");
    app_setting_set_int("Audio", "Volume", d.volume);
    app_setting_set("Audio", "Reverb", d.reverb ? "on" : "off");
    if (d.rec_audio != o.rec_audio) app_setting_set("Audio", "ScreenRecording", d.rec_audio ? "on" : "off");
    {  // ([Colour]: written once one changed, so a file stays without them until then)
        const ColourAdjust now = colour_of(d), was = colour_of(o);
        if (std::memcmp(&now, &was, sizeof now)) {
            app_setting_set_int("Colour", "Hue", d.hue);
            app_setting_set_int("Colour", "Temperature", d.temperature);
            app_setting_set_int("Colour", "Tint", d.tint);
            app_setting_set_int("Colour", "Brightness", d.brightness);
            app_setting_set_int("Colour", "Contrast", d.contrast);
            app_setting_set_int("Colour", "Saturation", d.saturation);
            app_setting_set_int("Colour", "Gamma", d.gamma);
        }
    }
    app_setting_set("Interface", "PauseInBackground", d.pause_background ? "on" : "off");
    app_setting_set("Interface", "Updates", d.updates ? "on" : "off");
    app_setting_set("Interface", "PauseInMenu", d.pause_menu ? "on" : "off");
    app_setting_set("Display", "TouchScreen", d.screen_shown ? "shown" : "hidden");
    app_setting_set("Display", "Layout", LAYOUT_KEYS[d.layout]);
#ifdef __ANDROID__  // (the PC's layout 3 is its own window: no way back to keep)
    if (d.layout == 3) app_setting_set("Display", "LayoutBefore", LAYOUT_KEYS[d.layout_before]);  // (its way back)
#endif
    app_setting_set("Display", "Corner", CORNER_KEYS[d.corner]);
    app_setting_set_int("Display", "Size", d.size);
    app_setting_set_int("Display", "Opacity", d.opacity);
    app_setting_set("Controls", "Haptics", d.haptics ? "on" : "off");
    app_setting_set("Controls", "LongPressHide", d.long_press_hide ? "on" : "off");
    app_setting_set("Controls", "FaceButtons", FACE_KEYS[d.face]);
    app_setting_set("Controls", "RightStick", STICK_KEYS[d.right_stick]);
    app_setting_set_int("Controls", "StickSpeed", d.stick_speed);
    app_setting_set("Controls", "InvertX", d.invert_x ? "on" : "off");
    app_setting_set("Controls", "InvertY", d.invert_y ? "on" : "off");
    app_setting_set("Controls", "R3", R3_KEYS[d.r3]);
    app_setting_set("Controls", "TownMenu", TOWN_KEYS[d.town]);
    app_setting_set_int("Controls", "Deadzone", d.deadzone);
    app_setting_set("Controls", "LockOn", d.lock_on ? "on" : "off");
    app_setting_set("Controls", "GyroAim", d.gyro_aim ? "on" : "off");
    for (int k = 0; k < kCombos; k++)
        app_setting_set("Controls", ("Combo" + std::to_string(k + 1) + "Pad").c_str(), controls_combo_pad_key(d.combo_pad[k]));
    {
        char sens[16];
        std::snprintf(sens, sizeof sens, "%.1f", d.gyro_sens / 10.0f);
        app_setting_set("Controls", "GyroSensitivity", sens);
    }
    // (the keyboard's and the mouse's: written once changed, so a file stays without them until then)
    if (!(d.binds == o.binds))
        for (int i = 0; i < BINDS; i++) app_setting_set("Keybinds", kBindNames[i], controls_keys_text(d.binds.keys[i]));
    if (d.mouse_sens != o.mouse_sens) {
        char sens[16];
        std::snprintf(sens, sizeof sens, "%.2f", d.mouse_sens / 100.0f);
        app_setting_set("Mouse", "MouseSensitivity", sens);
    }
#ifndef __ANDROID__
    if (d.mouse_inv_x != o.mouse_inv_x) app_setting_set("Mouse", "InvertX", d.mouse_inv_x ? "on" : "off");
    if (d.mouse_inv_y != o.mouse_inv_y) app_setting_set("Mouse", "InvertY", d.mouse_inv_y ? "on" : "off");
    if (d.button_icons != o.button_icons) app_setting_set("Controls", "ButtonIcons", ICON_KEYS[d.button_icons]);
#endif
    app_setting_set("Interface", "Language", LANGUAGE_KEYS[d.language]);
    app_setting_set("Interface", "Theme", THEME_KEYS[d.theme]);
    app_setting_set_int("Interface", "Scale", d.menu_scale);
    set_network(d);
#ifndef __ANDROID__
    pc_draft_save(d.pc, o.pc);
#endif
    std::string error;
    if (!app_settings_save(&error)) {
        s.error = tr("Could not save the settings.");
        return false;
    }
    if (d.driver != o.driver && !gpu_driver_choose(d.driver)) {  // (files/gpu_drivers/selected, read at the next start)
        s.error = tr("Could not save the GPU driver's choice.");
        return false;
    }
    if (d.mods != o.mods && !content_mods_save(draft_mods(d.mods), nullptr)) {  // (load_order.txt, read at the next start)
        s.error = tr("Could not save the mods' order.");
        return false;
    }
    if (d.mods != o.mods) g_mods_found_off.clear();  // (content_mods_save ended their note)
    if (d.cheats != o.cheats && !content_cheats_apply(d.cheats, nullptr)) {  // (in the game from its next frame)
        s.error = tr("Could not save the cheats.");
        return false;
    }
    // the touch controls'
    std::vector<std::pair<std::string, std::string>> touch;
    char text[32];
    if (d.controls != o.controls) touch.emplace_back("controls", CONTROLS_KEYS[d.controls]);
    if (d.touch_opacity != o.touch_opacity) snprintf(text, sizeof text, "%.2f", d.touch_opacity / 100.f), touch.emplace_back("opacity", text);
    if (d.camera != o.camera) snprintf(text, sizeof text, "%.2f", d.camera), touch.emplace_back("camera_speed", text);
    if (d.camera_smooth != o.camera_smooth) snprintf(text, sizeof text, "%.2f", d.camera_smooth / 100.f), touch.emplace_back("camera_smooth", text);
    if (d.attack_style != o.attack_style) touch.emplace_back("attack_style", std::to_string(d.attack_style));
    for (int i = 0; i < TB; i++)
        if (d.show[i] != o.show[i]) touch.emplace_back(std::string("show_") + TOUCH_BUTTONS[i].key, d.show[i] ? "1" : "0");
    for (int k = 0; k < kCombos; k++)
        if (d.combo[k] != o.combo[k]) touch.emplace_back("combo" + std::to_string(k + 1), d.combo[k]);
    for (const auto& [k, v] : touch) touch_set_setting(k, v);
    // what follows at once
    float fps = std::max(1.f, (float)std::atof(d.fps.c_str()));
    if (d.fps != o.fps) frame_cap(fps);

    if (d.volume != o.volume) app_live_volume(d.volume);
    if (d.reverb != o.reverb) app_live_reverb(d.reverb);
    if (d.shadows != o.shadows) app_live_shadows(d.shadows);
    if (d.ps1_filter != o.ps1_filter || (d.low_end == LOW_END_BRICK) != (o.low_end == LOW_END_BRICK))
        app_live_ps1(d.ps1_filter, d.low_end == LOW_END_BRICK);  // (the renderer's part: the textures and the geometry)
    if (d.frame_gen != o.frame_gen) frame_gen_set_enabled(d.frame_gen), frame_gen_to_activity(d.frame_gen);
    if (d.text_outline != o.text_outline) app_live_text_outline(d.text_outline);
    perfstats_set(d.stats, fps, spanish());
#ifndef __ANDROID__
    pc_stats_apply();
#endif
    if (d.screen_shown != ui_bottom_wanted()) ui_toggle_bottom();
    s.saved = s.draft;
    s.saved_notice = true;
    s.error.clear();
    return true;
}

// A room joined or hosted: the Multiplayer tab's options it was done with are kept at once (the others wait for Save)
void remember_network() {
    remember_typed(s.draft, s.saved);
    Draft& o = s.saved;
    const Draft& d = s.draft;
    o.nick = d.nick, o.address = d.address, o.password = d.password, o.lobby = d.lobby, o.lobbies = d.lobbies;
    o.host_port = d.host_port, o.slots = d.slots;
    o.play_with = d.play_with, o.hub_server = d.hub_server, o.hub_name = d.hub_name, o.hub_tags = d.hub_tags;
    o.hub_password = d.hub_password, o.hub_slots = d.hub_slots;
    set_network(o);
    if (!app_settings_save()) s.error = tr("Could not save the settings.");
}

// ---------------------------------------------------------------- moving about
void focus(const std::string& id) {
    s.focus = id;
    s.focus_request = true;
}
void close_dropdown() {
    if (!s.dropdown.open) return;
    s.dropdown.open = false;
    focus(s.dropdown.row);
}
float list_scroll() {
    auto* w = s.list ? ImGui::FindWindowByID(s.list) : nullptr;
    return w ? w->Scroll.y : 0;
}
void push(View view, const std::string& first) {
    s.dropdown.open = false;
    s.drag = {};
    s.origins.push_back({s.view, s.focus, list_scroll(), s.at});
    s.view = view;
    s.help_open.clear();
    focus(first);
    s.error.clear();
    s.restore_scroll = true;
    s.scroll = 0;
    ImGui::ClearActiveID();
}
void pop() {
    s.dropdown.open = false;
    s.drag = {};
    if (s.origins.empty()) {
        s.view = View::Main;
        s.focus.clear();
    } else {
        auto o = s.origins.back();
        s.origins.pop_back();
        s.view = o.view;
        s.at = o.at;
        focus(o.focus);
        s.scroll = o.scroll;
    }
    s.help_open.clear();
    s.restore_scroll = true;
    s.error.clear();
    ImGui::ClearActiveID();
}
void first_page(int page) {
    s.dropdown.open = false;
    s.drag = {};
    s.page = page;
    s.view = View::Main;
    s.at.clear();
    s.help_open.clear();
    s.origins.clear();
    s.focus.clear();
    s.focus_request = true;
    s.restore_scroll = true;
    s.scroll = 0;
    s.error.clear();
}
bool confirmation() {
    return s.view == View::CloseChanges || s.view == View::Restart || s.view == View::Exit || s.view == View::Reset ||
           s.view == View::RemoveDriver || s.view == View::RemoveMod || s.view == View::RemoveCheat || s.view == View::OpenSite ||
           s.view == View::OpenDiscord || s.view == View::KeyCapture || s.view == View::PadCapture || s.view == View::ChatOn;
}
void close_menu(bool* open) {
    *open = false;
    s.dropdown.open = false;
}
// Back (B, the Android Back key): the picker, a submenu or a question one step; the tabs close, asking first when
// something is unsaved
void ask_back(bool* open) {
    if (s.dropdown.open)
        close_dropdown();
    else if (s.view != View::Main) {  // (a page one level down too: back to its section's)
        if (s.view == View::CloseChanges) s.closing = false;  // (the menu stays: no layout editor after all)
        if (s.view == View::PadMap) padmap_cancel();
        pop();
    } else if (dirty())
        push(View::CloseChanges, "dialog.cancel");
    else
        close_menu(open);
}
// Close (X) and Back to the game, from anywhere: asks first when something is unsaved
void ask_close(bool* open) {
    s.dropdown.open = false;
    if (dirty())
        push(View::CloseChanges, "dialog.cancel");
    else
        close_menu(open);
}

// ---------------------------------------------------------------- the keyboard
// Typing in the Keyboard view: its keys, a physical keyboard's, or the phone's on-screen one (asked for with its key).
// Text input is on while the view is up: 1 without the phone's keyboard (a physical one types), 2 with it
int g_text_input = 0;
SDL_Window* text_window() {
    if (SDL_Window* w = SDL_GetKeyboardFocus()) return w;
    int n = 0;
    SDL_Window** all = SDL_GetWindows(&n);
    SDL_Window* w = all && n > 0 ? all[0] : nullptr;
    SDL_free(all);
    return w;
}
void text_input(int want) {
    if (want == g_text_input) return;  // (each frame: nothing to ask SDL for)
    SDL_Window* w = text_window();
    if (!w) return;
    if (g_text_input) SDL_StopTextInput(w);
    if (want) {
        // the phone's keyboard as the text asks: digits, a name or an address as typed, a password, a chat line
        SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, want == 2 ? "1" : "0");
        SDL_PropertiesID props = SDL_CreateProperties();
        int filter = s.keyboard.filter;
        SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_TYPE_NUMBER,
                              filter == KB_DIGITS   ? SDL_TEXTINPUT_TYPE_NUMBER
                              : filter == KB_SECRET ? SDL_TEXTINPUT_TYPE_TEXT_PASSWORD_VISIBLE
                              : filter == KB_NICK   ? SDL_TEXTINPUT_TYPE_TEXT_USERNAME
                                                    : SDL_TEXTINPUT_TYPE_TEXT);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_CAPITALIZATION_NUMBER,
                              filter == KB_CHAT ? SDL_CAPITALIZE_SENTENCES : SDL_CAPITALIZE_NONE);
        SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_AUTOCORRECT_BOOLEAN, filter == KB_CHAT);
        SDL_StartTextInputWithProperties(w, props);
        SDL_DestroyProperties(props);
    }
    g_text_input = want;
}
// The phone's keyboard, up again even if the player had put it away
void phone_keyboard() {
    text_input(0);
    text_input(2);
}
// A character the text takes (a UTF-8 sequence): the nickname as a room allows it, an address, a port
bool allowed(int filter, const std::string& ch) {
    unsigned char c = (unsigned char)ch[0];
    if (c < 0x20 || c == 0x7f) return false;
    bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    switch (filter) {
    case KB_NICK: return ch.size() == 1 && (alnum || c == ' ' || c == '.' || c == '_' || c == '-');
    case KB_ADDRESS: return ch.size() == 1 && (alnum || c == '.' || c == '-' || c == ':');
    case KB_DIGITS: return ch.size() == 1 && c >= '0' && c <= '9';
    default: return true;
    }
}
void open_keyboard(const std::string& title, const std::string& initial, int filter, size_t capacity, const std::string& hint,
                   std::function<bool(const std::string&, std::string&)> commit) {
    s.keyboard = {};
    s.keyboard.title = title, s.keyboard.hint = hint, s.keyboard.buffer = initial;
    s.keyboard.cursor = initial.size(), s.keyboard.capacity = capacity, s.keyboard.filter = filter;
    s.keyboard.commit = std::move(commit);
    push(View::Keyboard, "key.accept");
    // (played by touch: the phone's own keyboard comes up at once, the view's keys under it - a controller's player
    // keeps the view's keys first. Its key had to be found first before, and players took the phone's keyboard for
    // gone, MHGU's Discord. The MHGU port's the same)
    if (!controls_real_pad()) phone_keyboard();
}
// Typed text at the cursor, each character the filter lets in while there is room
void insert(const std::string& text) {
    Keyboard& k = s.keyboard;
    for (size_t i = 0; i < text.size();) {
        size_t n = 1;
        while (i + n < text.size() && ((unsigned char)text[i + n] & 0xc0) == 0x80) ++n;
        std::string ch = text.substr(i, n);
        i += n;
        if (!allowed(k.filter, ch) || k.buffer.size() + ch.size() > k.capacity) continue;
        k.buffer.insert(k.cursor, ch);
        k.cursor += ch.size();
    }
    s.error.clear();
}
void erase_previous() {
    Keyboard& k = s.keyboard;
    if (!k.cursor) return;
    size_t begin = k.cursor - 1;
    while (begin && ((unsigned char)k.buffer[begin] & 0xc0) == 0x80) --begin;
    k.buffer.erase(begin, k.cursor - begin);
    k.cursor = begin;
    s.error.clear();
}
void move_cursor(int d) {
    Keyboard& k = s.keyboard;
    if (d < 0 && k.cursor) {
        --k.cursor;
        while (k.cursor && ((unsigned char)k.buffer[k.cursor] & 0xc0) == 0x80) --k.cursor;
    } else if (d > 0 && k.cursor < k.buffer.size()) {
        ++k.cursor;
        while (k.cursor < k.buffer.size() && ((unsigned char)k.buffer[k.cursor] & 0xc0) == 0x80) ++k.cursor;
    }
}
void paste() {
    if (char* clip = SDL_GetClipboardText()) {
        std::string text = clip;
        SDL_free(clip);
        for (char& c : text)
            if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        insert(text);
    }
}
// (1.0.2) Say something in the chat window with the phone's keyboard up: the text at the window's top and the chat's lines
// under it, where the keys were (they are under the phone's keyboard); Keys on the screen, or the phone's keyboard put
// away, the keys again
bool chat_typing() {
    if (s.view != View::Keyboard || s.keyboard.filter != KB_CHAT || g_text_input != 2) return false;
    SDL_Window* w = text_window();
    return w && SDL_ScreenKeyboardShown(w);
}
// The chat window (1.0.2, the owner's): the chat page and its Say something in a window of their own over the game, a
// little see-through; its header moves it and its corner sizes it (chat.txt keeps both: chat::window). The MHGU port's the
// same
bool chat_window_view() { return s.view == View::Chat || (s.view == View::Keyboard && s.keyboard.filter == KB_CHAT); }
// (1.0.2) Say something in the chat window with the chat's lines under the text, not the view's keys: the phone's keyboard
// up (chat_typing), or a desktop's own keyboard (MH4U for PC: no phone keyboard; the window stays where it is)
bool chat_typing_lines() {
#ifdef __ANDROID__
    return chat_typing();
#else
    return s.view == View::Keyboard && s.keyboard.filter == KB_CHAT;
#endif
}
void accept_keyboard() {
    std::string text = s.keyboard.buffer, error;
    while (!text.empty() && text.back() == ' ') text.pop_back();  // (spaces at the ends: never meant)
    while (!text.empty() && text.front() == ' ') text.erase(0, 1);
    auto commit = s.keyboard.commit;
    pop();  // (first: the commit may open another view, or join a room)
    if (commit && !commit(text, error)) {
        push(View::Keyboard, "key.accept");  // (back to the keys, the text as it was, and why)
        s.error = error;
    }
}

// ---------------------------------------------------------------- the rows
// A row's choices in the picker; selected: the one it has now (-1: none of them). removable: those remembered, which
// remove takes out of their list (an x beside each)
void choices(const Row& row, std::vector<std::string> names, int selected, std::function<void(int)> set,
             std::vector<char> removable = {}, std::function<void(int)> remove = {}) {
    int first = std::clamp(selected, 0, (int)names.size() - 1);
    s.dropdown = {true, false, row.id, row.title, std::move(names), selected, std::move(set), std::move(removable), std::move(remove)};
    s.dropdown.help = help_text(row);
    focus("choice." + std::to_string(first));
}
void add_bool(std::vector<Row>& rows, const char* id, const char* name, bool* value, const char* key = nullptr) {
    Row r;
    r.id = id, r.title = tr(name), r.key = key ? key : "";
    r.value = tr(*value ? "On" : "Off");
    r.toggle = *value ? 1 : 0;
    r.adjust = [value](int d) { *value = d > 0; };
    r.activate = [value]() { *value = !*value; };
    rows.push_back(std::move(r));
}
// A switch of mh4u.ini's, in its own text
void add_switch(std::vector<Row>& rows, const char* id, const char* name, std::string* value, const char* key) {
    Row r;
    bool on = is_on(*value);
    r.id = id, r.title = tr(name), r.key = key;
    r.value = tr(on ? "On" : "Off");
    r.toggle = on ? 1 : 0;
    r.cold = game_option_needs_restart(key);
    r.adjust = [value](int d) { *value = onoff(*value, d > 0); };
    r.activate = [value]() { *value = onoff(*value, !is_on(*value)); };
    rows.push_back(std::move(r));
}
// selected -1: none of them (the value shows as `shown`)
void add_choice(std::vector<Row>& rows, const char* id, const char* name, int selected, const std::vector<std::string>& names,
                std::function<void(int)> set, const char* key = nullptr, const std::string& shown = "") {
    Row r;
    r.id = id, r.title = tr(name), r.key = key ? key : "";
    r.value = selected >= 0 ? names[std::clamp(selected, 0, (int)names.size() - 1)] : shown;
    r.adjust = [selected, names, set](int d) {
        int n = (int)names.size();
        set(selected < 0 ? (d > 0 ? 0 : n - 1) : (selected + d + n) % n);
    };
    Row copy = r;
    r.menu = true;
    r.activate = [copy, names, selected, set]() { choices(copy, names, selected, set); };
    rows.push_back(std::move(r));
}
void add_number(std::vector<Row>& rows, const char* id, const char* name, int* p, int low, int high, int step,
                const char* format, const char* key = nullptr) {
    char text[64];
    snprintf(text, sizeof text, format, *p);
    Row r;
    r.id = id, r.title = tr(name), r.key = key ? key : "";
    r.value = text;
    r.number = true;
    r.adjust = [p, low, high, step](int d) { *p = std::clamp(*p + d * step, low, high); };
    rows.push_back(std::move(r));
}
void header(std::vector<Row>& rows, const std::string& id, const std::string& title) {
    Row r;
    r.id = id, r.title = title;
    r.header = true;
    r.enabled = false;
    rows.push_back(std::move(r));
}
void action(std::vector<Row>& rows, const std::string& id, const std::string& name, std::function<void()> fn,
            bool enabled = true, const char* key = nullptr) {
    Row r;
    r.id = id, r.title = name, r.key = key ? key : "";
    r.action = true, r.arrow = true;
    r.activate = std::move(fn);
    r.enabled = enabled;
    rows.push_back(std::move(r));
}
// Clear shader cache (cache_clear.cpp, 1.0's S30; the same row as MHGU's): a tap leaves the mark the next start deletes the
// shader cache by, before the runtime reads it; another tap takes it back. Nothing to Save: the mark is the setting. The
// size is read again at most every 5 s
void cache_clear_row(std::vector<Row>& r) {
    static uint64_t bytes;
    static Uint64 read_at;
    const Uint64 now = SDL_GetTicks();
    if (!read_at || now - read_at > 5000) bytes = cache_clear_bytes(), read_at = now;
    action(r, "cache.clear", en_es("Clear shader cache", "Borrar la caché de shaders"), []() {
        cache_clear_request(!cache_clear_pending());
        read_at = 0;
    });
    char mb[32];
    std::snprintf(mb, sizeof mb, "%.0f MB", bytes / 1048576.0);
    r.back().value = cache_clear_pending() ? en_es("At the next start", "Al próximo inicio") : mb;
    r.back().help = en_es(
        "Deletes the shaders the game compiled and kept (their SPIR-V and the GPU driver's own cache) at the next start, "
        "before the game reads them: for a picture that shows something wrong after a GPU driver update or a crash. The "
        "list of what earlier sessions drew stays, so that start compiles all of it again, with a plate that says how far: "
        "it takes longer, and the first areas may stutter or show effects late until it is done. A second tap, before the "
        "game starts again, keeps the cache.",
        "Borra los shaders que el juego compiló y guardó (su SPIR-V y la caché del driver de la GPU) en el próximo inicio, "
        "antes de que el juego los lea: para una imagen que muestra algo mal después de actualizar el driver de la GPU o de "
        "un cierre. La lista de lo que dibujaron las sesiones anteriores se queda, así que ese inicio la vuelve a compilar "
        "entera, con un cartel que dice cuánto falta: tarda más, y las primeras zonas pueden tironear o mostrar efectos tarde "
        "hasta que termine. Un segundo toque, antes de que el juego vuelva a empezar, conserva la caché.");
}
// Effects (mh4n port/src/hooks.cpp EffectsApply; 1.0's S27, the same row as MHGU's): the game's particle effects, drawn
// or not, at once
void effects_row(std::vector<Row>& r) {
    add_switch(r, "effects", en_es("Effects", "Efectos"), &s.draft.effects, "effects");
    r.back().help = en_es(
        "The game's particle effects: hits, sparks, fire, breath, dust, rain, the glow of a charged attack. Off: faster where "
        "there are many of them on a weak phone, but the effects that warn of a monster's attack go too. It applies at once.",
        "Los efectos de partículas del juego: golpes, chispas, fuego, alientos, polvo, lluvia, el brillo de un ataque "
        "cargado. Apagados: más rápido donde hay muchos, en un teléfono débil, pero también desaparecen los efectos que "
        "avisan del ataque de un monstruo. Se aplica al momento.");
}
void reset_effects() { s.draft.effects = defaults().effects; }
// The gfx group's places (MENU.md 3.2): Effects on Graphics & Audio, Clear shader cache on Advanced (an action: the mark
// is the setting, nothing to put back)
MENU_ENTRY("graphics", 55, effects_row, reset_effects);
MENU_ENTRY("graphics/advanced", 50, cache_clear_row);
// A row of a button's choice (BUTTON_KEYS, or END_KEYS): another value the file has shows as it is
void add_button(std::vector<Row>& rows, const char* id, const char* name, std::string* value, const char* key,
                const char* const* keys, int n) {
    std::vector<std::string> names;
    for (int i = 0; i < n; i++) names.push_back(!strcasecmp(keys[i], "off") ? tr("Off") : keys[i]);
    add_choice(rows, id, name, index_of(keys, n, *value), names, [value, keys](int v) { *value = keys[v]; }, key, *value);
    rows.back().cold = game_option_needs_restart(key);
}

// Low-end mode, as MHGU's (app_settings.h): the preset for a weak phone, Toaster for the weakest and the battery, and back;
// each of its options still changeable afterwards. On: low_end_fps() (60 with a fast core, else 30) as the most, the 3D
// resolution at most 33 % (1x on a 720-line screen), the screen filters, the game's anti-aliasing and the shadows off.
// Toaster: 30 FPS, the 3DS's resolution, and the reverb off too (back to On, the frame rate and the reverb come back).
// Brick (1.0.1.1): Toaster's, and while it is the level the renderer's textures at a sixteenth of their size, sampled
// nearest, and the top screen's pixels sharp (app_live_ps1, shared_screens_set_ps1: no option of their own)
void low_end(int level) {
    Draft& d = s.draft;
    const int was = d.low_end;
    d.low_end = level;
    std::string* filters[] = {&d.bloom, &d.motion_blur, &d.depth_of_field, &d.radial_blur, &d.haze};
    const bool toaster = level >= LOW_END_TOASTER, was_toaster = was >= LOW_END_TOASTER;  // (Brick sets what Toaster does)
    if (level != LOW_END_OFF) {
        const int scale = toaster ? kToasterScale : kLowEndScale;
        if (was_toaster && d.scale == kToasterScale) d.scale = scale;  // (back to On)
        // (0 is the lowest of all: the 3DS's)
        if (scale == 0 || (d.scale != 0 && d.scale > scale)) d.scale = scale;
        const int cap = toaster ? kToasterFps : low_end_fps();
        if (fps_of(d.fps) > cap || (was_toaster && fps_of(d.fps) == kToasterFps)) d.fps = std::to_string(cap);
        for (std::string* f : filters) *f = onoff(*f, false);
        d.antialiasing = onoff(d.antialiasing, false);
        d.shadows = false;
        if (toaster) d.reverb = false;
        else if (was_toaster) d.reverb = true;
    } else {
        if (d.scale == kToasterScale || d.scale <= kLowEndScale) d.scale = kRenderScaleDefault;
        if (fps_of(d.fps) == kToasterFps) d.fps = "60";
        for (std::string* f : filters) *f = onoff(*f, true);
        d.antialiasing = onoff(d.antialiasing, true);
        d.shadows = d.reverb = true;
    }
}

// A page's entries, and the pages under it (a section's own page: all of its pages)
bool in_page(const char* page, const std::string& prefix) {
    size_t n = prefix.size();
    return !strncmp(page, prefix.c_str(), n) && (page[n] == 0 || page[n] == '/');
}
// Reset: every entry of the page (a section's: of each of its pages) puts its options back as they come, shown or not
void reset_page() {
    for (const MenuEntry& e : menu_entries())
        if (e.reset && in_page(e.page, s.reset_at)) e.reset();
    pop();
}

// The app closes as when the player leaves it (android_main.cpp: the game is stopped and its saves closed first)
void quit_app() {
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit);
}

void restart_app() {
    app_call_activity("restartApp");  // (PhoenixActivity: it waits for this process to end, and opens the app again)
    quit_app();
}

// ---------------------------------------------------------------- the Multiplayer tab
const ImU32 NET_GREEN = IM_COL32(132, 206, 98, 255);
#ifndef __ANDROID__
const char* say(const char* en, const char* es) { return pc_words(spanish() ? es : en); }
#else
const char* say(const char* en, const char* es) { return spanish() ? es : en; }
#endif
bool in_room(const rooms::Status& st) { return st.state == rooms::State::Joined || st.state == rooms::State::Moderator; }

// Why the room refused, dropped or kicked this phone, as the player reads it, and what to try
std::string error_text(rooms::Error e) {
    switch (e) {
    case rooms::Error::LostConnection: return say("The connection to the room was lost, or the room closed.", "Se perdió la conexión con la sala, o la sala se cerró.");
    case rooms::Error::HostKicked: return say("A moderator removed you from the room.", "Un moderador te sacó de la sala.");
    case rooms::Error::UnknownError: return say("The network could not be used: check that the phone is online.", "No se pudo usar la red: revisa que el teléfono tenga conexión.");
    case rooms::Error::NameCollision: return say("That nickname is taken in this room, or is not valid. Choose another one.", "Ese apodo ya está en uso en esta sala, o no es válido. Elige otro.");
    case rooms::Error::MacCollision: return say("Another member of the room has this console's MAC address. Join again.", "Otro miembro de la sala tiene la dirección MAC de esta consola. Vuelve a entrar.");
    case rooms::Error::ConsoleIdCollision: return say("A console with the same ID is already in the room: the same save folder copied to two devices?", "Ya hay en la sala una consola con el mismo ID: ¿la misma carpeta de partida copiada en dos equipos?");
    case rooms::Error::WrongVersion: return say("The room runs another version of the multiplayer protocol.", "La sala usa otra versión del protocolo multijugador.");
    case rooms::Error::WrongPassword: return say("Wrong password for this room.", "La contraseña de la sala no es correcta.");
    case rooms::Error::CouldNotConnect: return say("The room does not answer. Check the address and the port, and that the host's UDP port is open.", "La sala no responde. Revisa la dirección y el puerto, y que el puerto UDP del anfitrión esté abierto.");
    case rooms::Error::RoomIsFull: return say("The room is full.", "La sala está llena.");
    case rooms::Error::HostBanned: return say("You are banned from this room.", "Tienes prohibida la entrada a esta sala.");
    case rooms::Error::PermissionDenied: return say("Only a moderator can do that.", "Solo un moderador puede hacer eso.");
    case rooms::Error::NoSuchUser: return say("There is no hunter of that name in the room.", "No hay en la sala ningún cazador con ese nombre.");
    default: return "";
    }
}
// Why joining a hub failed, or why its link ended (mh4n/hubs.h), and what to try
std::string hub_error_text(hubs::JoinError e) {
    switch (e) {
    case hubs::JoinError::ServerNotFound: return say("The hub server's name has no address: check it, and that the phone is online.", "El nombre del servidor de hubs no tiene dirección: revísalo, y que el teléfono tenga conexión.");
    case hubs::JoinError::ServerUnreachable: return say("Can't reach the hub server. It is run by players and may be down, or this network blocks it: try later, or on another network.", "No se puede llegar al servidor de hubs. Lo mantienen jugadores y puede estar caído, o esta red lo bloquea: prueba más tarde o con otra red.");
    case hubs::JoinError::ServerBusy: return say("The hub server is busy: try again in a moment.", "El servidor de hubs está ocupado: vuelve a probar en un momento.");
    case hubs::JoinError::ServerVersion: return say("The hub server runs another version.", "El servidor de hubs usa otra versión.");
    case hubs::JoinError::HubGone: return say("That hub is gone: show the hubs again.", "Ese hub ya no está: vuelve a ver los hubs.");
    case hubs::JoinError::NoAnswer: return say("The hub's host did not answer: their app may be closed or in the background.", "El anfitrión del hub no respondió: puede tener la app cerrada o en segundo plano.");
    case hubs::JoinError::OtherGame: return say("That hub's host has another version of the game, or of this app.", "El anfitrión de ese hub tiene otra versión del juego, o de esta app.");
    case hubs::JoinError::HubFull: return say("That hub is full.", "Ese hub está lleno.");
    case hubs::JoinError::Lost: return say("The link to the hub's host was lost, or the host closed the hub.", "Se perdió el enlace con el anfitrión del hub, o el anfitrión cerró el hub.");
    default: return "";
    }
}
// A line of the room's, in the menu's language (who said a chat line is the note's value, apart)
std::string line_text(const NetLine& l) {
    switch (l.kind) {
    case NetLine::Joined: return fmt("%s joined the room.", l.nick.c_str());
    case NetLine::Left: return fmt("%s left the room.", l.nick.c_str());
    case NetLine::Kicked: return fmt("%s was kicked from the room.", l.nick.c_str());
    case NetLine::Banned: return fmt("%s was banned from the room.", l.nick.c_str());
    case NetLine::Failed: return error_text(l.error);
    case NetLine::YouJoined: return l.text.empty() ? tr("You joined the room.") : fmt("You joined %s.", l.text.c_str());
    case NetLine::YouLeft: return tr("You left the room.");
    case NetLine::HubFailed: return hub_error_text(l.hub_error);
    default: return l.text;
    }
}
ImU32 line_ink(const NetLine& l) {
    switch (l.kind) {
    case NetLine::Chat: case NetLine::Mine: return 0;
    case NetLine::Joined: case NetLine::YouJoined: return NET_GREEN;
    case NetLine::Failed: case NetLine::Kicked: case NetLine::Banned: case NetLine::HubFailed: return theme().warn;
    default: return touch_look().dim;
    }
}
std::string status_text(const rooms::Status& st) {
    const hubs::JoinStatus hub = hubs::GetJoinStatus();
    const bool in_hub = hubs::InHub(st), hosting_hub = st.hosting && hubs::GetHostStatus().active;
    if (st.state == rooms::State::Idle && hub.state == hubs::JoinState::Connecting)
        return say("Connecting to the hub server...", "Conectando con el servidor de hubs...");
    if (st.state == rooms::State::Idle && hub.state == hubs::JoinState::Waiting)
        return say("Waiting for the hub's host...", "Esperando al anfitrión del hub...");
    switch (st.state) {
    case rooms::State::Joining: return tr(st.waiting_for_console ? "Waiting for the game..." : "Connecting...");
    case rooms::State::Joined:
        if (in_hub) return say("In the hub", "En el hub");
        if (hosting_hub) return say("Hosting a hub, in it", "Anfitrión de un hub, adentro");
        return tr(st.hosting ? "Hosting, in the room" : "In the room");
    case rooms::State::Moderator:
        if (hosting_hub) return say("Hosting a hub, in it", "Anfitrión de un hub, adentro");
        return tr(st.hosting ? "Hosting, in the room" : "In the room, moderator");
    default:
        if (hosting_hub) return say("Hub open", "Hub abierto");
        return st.hosting ? fmt("Room open on UDP %d", (int)st.hosted_port) : tr("Not in a room");
    }
}
ImU32 status_ink(const rooms::Status& st) {
    if (in_room(st)) return NET_GREEN;
    if (st.state == rooms::State::Joining) return touch_look().glow;
    return st.error != rooms::Error::None ? theme().warn : touch_look().dim;
}
std::string status_help(const rooms::Status& st) {
    if (in_room(st) && hubs::InHub(st)) {
        const hubs::JoinStatus hub = hubs::GetJoinStatus();
        return fmt(say("%s, through the hub server %s. The game's Local Play now reaches its hunters: one creates a Gathering Hall and the others find it.",
                       "%s, a través del servidor de hubs %s. El juego local (Local Play) ya llega a sus cazadores: uno crea un Gathering Hall y los demás lo buscan."),
                   st.room.name.c_str(), hub.server.c_str());
    }
    if (in_room(st) && st.hosting && hubs::GetHostStatus().active) {
        const hubs::HostStatus host = hubs::GetHostStatus();
        const std::string where = host.state == hubs::ServerState::Online
                                      ? fmt(say("listed on the hub server %s", "listado en el servidor de hubs %s"), host.server.c_str())
                                      : fmt(say("not listed on the hub server %s yet", "todavía sin listar en el servidor de hubs %s"), host.server.c_str());
        return fmt(say("%s, %s. The others find it in Multiplayer > Join a hub, with no port to open. The game's Local Play reaches its hunters: one creates a Gathering Hall and the others find it.",
                       "%s, %s. Los demás lo encuentran en Multijugador > Entrar a un hub, sin abrir puertos. El juego local (Local Play) llega a sus cazadores: uno crea un Gathering Hall y los demás lo buscan."),
                   st.room.name.c_str(), where.c_str());
    }
    if (in_room(st)) {
        std::string at = st.host + ":" + std::to_string(st.port);
        if (st.hosting) {  // (this phone's room: where the others find it, not the loopback)
            std::vector<std::string> mine = net_local_addresses();
            if (!mine.empty()) at = mine[0] + ":" + std::to_string(st.hosted_port);
        }
        return fmt(say("%s, at %s. The game's Local Play now reaches its hunters: one creates a Gathering Hall and the others find it.",
                       "%s, en %s. El juego local (Local Play) ya llega a sus cazadores: uno crea un Gathering Hall y los demás lo buscan."),
                   st.room.name.c_str(), at.c_str());
    }
    if (st.state == rooms::State::Joining)
        return st.waiting_for_console ? say("The room is joined as soon as the game has started.", "Se entra a la sala apenas arranque el juego.")
                                      : fmt(say("Joining %s:%d as %s.", "Entrando a %s:%d como %s."), st.host.c_str(), (int)st.port, st.nickname.c_str());
    return say("Join a room with its address, or host one on this phone. Then, in the game's Local Play, one hunter creates a Gathering Hall and the others find it. It works while the game runs: do it before going to the Gathering Hall.",
               "Entra a una sala con su dirección, o crea una en este teléfono. Luego, en el juego local (Local Play), un cazador crea un Gathering Hall y los demás lo buscan. Funciona con el juego en marcha: hazlo antes de ir al Gathering Hall.");
}

void note(std::vector<Row>& rows, const std::string& id, const std::string& text, ImU32 ink = 0, const std::string& who = "") {
    Row r;
    r.id = id, r.title = text, r.value = who, r.ink = ink;
    r.note = true;
    r.enabled = false;
    rows.push_back(std::move(r));
}
// A text of the draft, typed on the keyboard; shown: its row's value. check: why a text can't be (empty always can)
void add_text(std::vector<Row>& rows, const char* id, const char* name, std::string* value, int filter, size_t capacity,
              const char* key, const std::string& shown, const std::string& hint,
              std::function<bool(const std::string&, std::string&)> check = {}) {
    Row r;
    r.id = id, r.title = tr(name), r.key = key, r.value = shown;
    r.arrow = true;
    r.activate = [name, value, filter, capacity, hint, check]() {
        open_keyboard(tr(name), *value, filter, capacity, hint, [value, check](const std::string& typed, std::string& error) {
            if (check && !typed.empty() && !check(typed, error)) return false;
            *value = typed;
            return true;
        });
    };
    rows.push_back(std::move(r));
}
bool check_nick(const std::string& typed, std::string& error) {
    if (rooms::ValidNickname(typed)) return true;
    error = tr("A nickname has 4 to 20 letters, digits, spaces, '.', '_' or '-'.");
    return false;
}
bool check_address(const std::string& typed, std::string& error) {
    std::string host;
    std::uint16_t port;
    if (net_parse_address(typed, &host, &port)) return true;
    error = tr("That is not an address: an IP or a name, and :port if it is not 24872.");
    return false;
}
bool check_port(const std::string& typed, std::string& error) {
    std::uint16_t port;
    if (net_parse_port(typed, &port)) return true;
    error = tr("A port is a number from 1 to 65535.");
    return false;
}

// An address of a local network (or of this phone): no notice about playing online for it
bool local_address(const std::string& address) {
    std::string host;
    std::uint16_t port;
    if (!net_parse_address(address, &host, &port)) return true;  // (net_join says what is wrong with it)
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (std::sscanf(host.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return !strcasecmp(host.c_str(), "localhost");
    return a == 10 || a == 127 || (a == 192 && b == 168) || (a == 172 && b >= 16 && b < 32) || (a == 169 && b == 254) ||
           (a == 100 && b >= 64 && b < 128);  // (the last: a carrier's or a VPN's shared addresses, Tailscale's)
}
void join_room_now();
void join_room() {
    const std::string address = s.draft.address;
    if (address.empty() || s.draft.nick.empty() || local_address(address)) {
        join_room_now();
        return;
    }
    online_then(address, []() { join_room_now(); });
}
void join_room_now() {
    Draft& d = s.draft;
    switch (net_join(d.address, d.nick, d.password)) {
    case NetProblem::Nickname: s.error = tr("Choose a nickname first."); focus("net.nick"); return;
    case NetProblem::Address: s.error = tr("Type the room's address first."); focus("net.address"); return;
    default: break;
    }
    s.error.clear();
    remember_network();
    focus("net.status");
}
void host_room() {
    Draft& d = s.draft;
    switch (net_host(d.host_port, d.slots, d.nick, d.password)) {
    case NetProblem::Nickname: s.error = tr("Choose a nickname first."); focus("net.nick"); return;
    case NetProblem::Port: s.error = tr("A port is a number from 1 to 65535."); focus("net.port"); return;
    case NetProblem::PortBusy: s.error = fmt("UDP port %s is in use on this phone: choose another one.", d.host_port.c_str()); focus("net.port"); return;
    default: break;
    }
    s.error.clear();
    remember_network();
    focus("net.status");
}

// Rooms on this Wi-Fi (net_lan.h): a search, and a row per room that answered; a tap joins it
void lan_rows(std::vector<Row>& r) {
#ifdef __ANDROID__
    header(r, "net.lan.head", tr("Rooms on this Wi-Fi"));
#else  // (MH4U for PC: every network the PC is on, pc/pc_net_lan.cpp)
    header(r, "net.lan.head", say("Rooms on this network", "Salas en esta red"));
#endif
    const LanSearch lan = net_lan_result();
    // (enabled while it searches: a second tap waits for the same answer)
#ifdef __ANDROID__
    action(r, "net.lan", tr("Search this Wi-Fi"), []() { net_lan_search(); }, true, "LanSearch");
#else
    action(r, "net.lan", say("Search this network", "Buscar en esta red"), []() { net_lan_search(); }, true, "LanSearch");
#endif
    r.back().value = lan.busy ? tr("Searching...") : !lan.done ? "" : lan.rooms.empty() ? tr("None found") : fmt("%d room(s)", (int)lan.rooms.size());
    if (lan.busy) r.back().ink = touch_look().glow;
    if (lan.done && !lan.busy && lan.rooms.empty())
        note(r, "net.lan.none",
#ifdef __ANDROID__
             lan.no_network ? say("This phone is on no Wi-Fi: connect it to the host's Wi-Fi, or to the host's hotspot.",
                                  "Este teléfono no está en ninguna Wi-Fi: conéctalo a la Wi-Fi del anfitrión, o a su punto de acceso.")
                            : say("No room answered. The host's phone must be on this Wi-Fi, with a room open (Host a room) and the app open.",
                                  "Ninguna sala respondió. El teléfono del anfitrión debe estar en esta Wi-Fi, con una sala abierta (Crear una sala) y la app abierta."),
#else
             lan.no_network ? say("This PC is on no network: connect it to the host's Wi-Fi or network, or to the host's hotspot.",
                                  "Esta PC no está en ninguna red: conéctala a la Wi-Fi o la red del anfitrión, o a su punto de acceso.")
                            : say("No room answered. The host must be on this network (the same Wi-Fi or router), with a room open (Host a room) and the game open.",
                                  "Ninguna sala respondió. El anfitrión debe estar en esta red (la misma Wi-Fi o router), con una sala abierta (Crear una sala) y el juego abierto."),
#endif
             touch_look().dim);
    if (lan.busy || !lan.done) return;
    int shown = 0;
    for (const LanRoom& room : lan.rooms) {
        Row row;
        row.id = "net.wroom." + std::to_string(++shown), row.title = room.name.empty() ? room.address : room.name;
        row.value = fmt("%d / %d", room.members, room.slots);
        if (room.password) row.value = std::string(tr("Password")) + "  " + row.value;
#ifdef __ANDROID__
        if (room.own) row.value = std::string(tr("This phone")) + "  " + row.value;
#else
        if (room.own) row.value = std::string(say("This PC", "Esta PC")) + "  " + row.value;
#endif
        std::string hunters;
        for (const std::string& h : room.hunters) hunters += (hunters.empty() ? "" : ", ") + h;
        row.help = fmt(say("At %s, UDP port %d. Hunters: %s. %s A tap joins it.", "En %s, puerto UDP %d. Cazadores: %s. %s Un toque entra."),
                       room.address.c_str(), (int)room.port, hunters.empty() ? "-" : hunters.c_str(),
                       room.password ? say("It has a password.", "Tiene contraseña.") : say("No password.", "Sin contraseña."));
        row.arrow = true;
        const std::string address = room.address + ":" + std::to_string(room.port);
        const bool locked = room.password;
        row.activate = [address, locked]() {
            s.draft.address = address;
            if (!locked) {
                join_room();
                return;
            }
            open_keyboard(tr("Password"), s.draft.password, KB_SECRET, 64, tr("This room has a password."),
                          [](const std::string& typed, std::string&) {
                              s.draft.password = typed;
                              join_room();
                              return true;
                          });
        };
        r.push_back(std::move(row));
    }
}

// A lobby the player typed to the front of the list (once; the last one out when it is full)
void remember_lobby(std::vector<std::string>& list, const std::string& v) {
    if (!typed_lobby(v)) return;
    for (auto it = list.begin(); it != list.end(); ++it)
        if (!strcasecmp(it->c_str(), v.c_str())) {
            list.erase(it);
            break;
        }
    list.insert(list.begin(), v);
    if ((int)list.size() > kRecentLobbies) list.resize(kRecentLobbies);
}
// Save applies a lobby (or a room joined or hosted does): one the player typed (or picked from the list) goes to the
// front of the list, only when it changes. The one it replaces is never put back: one taken out of the list stays out,
// in use or not (the one typed in an older version went in once, at this version's first start: list_older_lobby).
// The same as MHGU's hub servers and keys (runtime/settings_panel.cpp remember_typed)
void remember_typed(Draft& d, const Draft& before) {
    if (d.lobby != before.lobby) remember_lobby(d.lobbies, d.lobby);
}
// The lobby typed in an older version, the one in use: in the list once, as if typed and saved now, at this version's
// first start ([Network] OlderLobbyListed marks it done, written at once); from then on only what is typed and saved
// goes in. MHGU does the same with its hub server and key, for a file of an older version (runtime/config.c config_load)
void list_older_lobby() {
    if (app_setting_bool("Network", "OlderLobbyListed", false)) return;
    std::vector<std::string> list = saved_lobbies();
    remember_lobby(list, app_setting("Network", "Lobby"));
    for (int i = 0; i < kRecentLobbies; i++) {  // (as set_network writes them)
        const std::string key = "RecentLobby" + std::to_string(i + 1);
        if (i < (int)list.size()) app_setting_set("Network", key.c_str(), list[i]);
        else app_setting_remove("Network", key.c_str());
    }
    app_setting_set("Network", "OlderLobbyListed", "1");
    std::string error;
    if (!app_settings_save(&error)) std::fprintf(stderr, "[Info] App: mh4u-app.ini: %s\n", error.c_str());
}
// A remembered lobby out of the list (its x in the picker), at once and in mh4u-app.ini too: it is no option, so Save and
// leaving without it have nothing to do with it. Then the row's picker again, as the list is now (reopen_picker). The
// file not written: it stays in the list, as MHGU's forget
void forget_lobby(const std::string& v, const char* row, int at) {
    auto out = [&v](Draft& d) {
        for (auto it = d.lobbies.begin(); it != d.lobbies.end(); ++it)
            if (!strcasecmp(it->c_str(), v.c_str())) {
                d.lobbies.erase(it);
                break;
            }
    };
    Draft saved = s.saved;
    out(saved);
    set_network(saved);
    if (!app_settings_save()) {
        set_network(s.saved);  // (the file's as it was: in memory too)
        s.error = tr("Could not save the settings.");
        return;
    }
    out(s.saved);
    out(s.draft);
    s.reopen = row, s.reopen_at = at;
}
// A remembered choice was taken out: the row's picker opens again with the list as it is now, at the same place
void reopen_picker(std::vector<Row>& rows) {
    if (s.reopen.empty()) return;
    const std::string id = s.reopen;
    s.reopen.clear();
    auto it = std::find_if(rows.begin(), rows.end(), [&](const Row& r) { return r.id == id; });
    if (it == rows.end() || !it->activate) {
        close_dropdown();
        return;
    }
    auto fn = it->activate;
    fn();
    if (s.dropdown.open && !s.dropdown.names.empty())
        focus("choice." + std::to_string(std::clamp(s.reopen_at, 0, (int)s.dropdown.names.size() - 1)));
}
// The lobby: the short list (LOBBY_LIST, the default first), the lobbies typed and saved (the last first: [Network]
// RecentLobby1..; an x takes one out), Custom... (typed on the keyboard, the last one offered again) and Off, as MHGU's
// hub server row
std::string g_custom_lobby;
void lobby_row(std::vector<Row>& rows) {
    const std::vector<std::string> used = s.draft.lobbies;
    const int n = (int)std::size(LOBBY_LIST), m = (int)used.size(), custom = n + m, off = custom + 1;
    const std::string now = s.draft.lobby;
    int selected = now.empty() ? 0 : lobby_off(now) ? off : custom;
    for (int i = 0; i < n; i++)  // (one typed as it is in the list: that one)
        if (!strcasecmp(now.c_str(), LOBBY_LIST[i])) selected = i;
    for (int i = 0; i < m && selected == custom; i++)  // (or as one remembered)
        if (!strcasecmp(now.c_str(), used[i].c_str())) selected = n + i;
    // (a typed one in use, remembered or not: Custom... starts from it, as MHGU's hub server and key)
    if (selected >= n && selected <= custom) g_custom_lobby = now;
    std::vector<std::string> names;
    std::vector<char> removable(n, 0);
    for (int i = 0; i < n; i++) names.push_back(i ? std::string(LOBBY_LIST[i]) : std::string(LOBBY_LIST[i]) + "  (" + tr("Default") + ")");
    for (const auto& v : used) names.push_back(v), removable.push_back(1);
    names.push_back(selected == custom ? fmt("Custom: %s", now.c_str()) : std::string(tr("Custom...")));
    names.push_back(tr("Off: no list of public rooms"));
    removable.resize(names.size(), 0);
    Row r;
    r.id = "net.lobby", r.title = tr("Lobby"), r.key = "Lobby";
    r.value = selected < n ? std::string(LOBBY_LIST[selected]) : selected == off ? std::string(tr("No lobby")) : now;
    auto set = [n, used, custom, off](int i) {
        if (i == custom) {
            open_keyboard(tr("Lobby"), g_custom_lobby, KB_ANY, 200, tr("A lobby's address, as http://name:port."),
                          [](const std::string& typed, std::string&) {
                              s.draft.lobby = typed;
                              if (!typed.empty() && !lobby_off(typed)) g_custom_lobby = typed;
                              return true;
                          });
            return;
        }
        s.draft.lobby = i == off ? "Off" : i >= n ? used[i - n] : i ? LOBBY_LIST[i] : "";
    };
    // (left / right go through the list, the remembered ones and Off, not Custom..., which opens the keyboard)
    r.adjust = [selected, custom, off, set](int d) {
        int at = selected >= custom ? custom : selected;  // (Off, or a custom one, is Custom...'s place here)
        at = (at + d + custom + 1) % (custom + 1);
        set(at == custom ? off : at);
    };
    Row copy = r;
    r.menu = true;
    auto remove = [n, used](int i) {
        if (i >= n && i - n < (int)used.size()) forget_lobby(used[i - n], "net.lobby", i);
    };
    r.activate = [copy, names, selected, set, removable, remove]() { choices(copy, names, selected, set, removable, remove); };
    rows.push_back(std::move(r));
}

#include "settings_chat.inc"  // (S32, S35: the chat's pages, the room's password generator)
// (1.0.2, after tabarong's floating chat) The chat window afloat: played by touch with no controller in hand, on the chat's
// lines or its Say something (not its options, a picker or a question), it floats over the game, which goes on - the menu
// doesn't hold it - with the touch controls drawn and working round it (ui.cpp, android_main.cpp). A finger is the window's
// only on its controls (chat_claim: the header with the tabs, the text and Send, a friend's arrows, the corner's grip, a
// row that does something, the list's scroll strip); anywhere else, the blank part of the window too, it is the game's -
// the stick, the camera, the touch screen -, and while typing such a tap closes the window and the phone's keyboard. A
// menu of the game's hides it until it closes (typing stops); the settings button leaves it for the menu. With a
// controller in hand (a handheld's buttons) the window stays a menu, as before: its buttons work it. The MHGU port's the same
bool chat_floating() {  // (a desktop's: afloat always, worked with the mouse - a controller's presses the game's meanwhile)
#ifdef __ANDROID__
    const bool by_hand = !controls_real_pad() || chat_app_test();
#else
    const bool by_hand = true;
#endif
    return by_hand && chat_window_view() && chat_strip() && !chat_options() && !s.dropdown.open;
}
void chat_claim(ImRect r) {
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    if (d.x <= 0 || d.y <= 0 || r.GetWidth() <= 0 || r.GetHeight() <= 0) return;
    g_claims_next.push_back(ImRect(r.Min.x / d.x, r.Min.y / d.y, r.Max.x / d.x, r.Max.y / d.y));
}
#ifndef __ANDROID__
#include "pc/settings_pc_net.inc"  // (the PC's: Multiplayer > Discord, the chat key)
#endif
// ---- Crossplay with Azahar and Citra (S36): the rooms are theirs, so their players and this app's play together. A
// note says so plainly, and a member playing another game (Monster Hunter 4G, another title) is marked: it can't meet
// you in the Gathering Hall. A different update of MH4U can't be told from the room (Azahar's game information has no
// version): the note asks for 1.3.0.
bool other_game(const rooms::Member& m) {
    return m.game.id && m.game.id != rooms::kMh4uUsa && m.game.id != rooms::kMh4uEurope;
}
void crossplay_note(std::vector<Row>& r, const rooms::Status& st) {
    note(r, "net.cross",
         say("Crossplay: these rooms are the same as Azahar's and Citra's (and Lime3DS's) multiplayer rooms. Their players "
             "join a room of this app, and you join theirs, the same way. Everyone needs MH4U with the 1.3.0 update; "
             "another version doesn't find your Gathering Hall.",
             "Juego cruzado: estas salas son las mismas que las salas multijugador de Azahar y Citra (y Lime3DS). Sus "
             "jugadores entran a una sala de esta app, y tú a las suyas, de la misma manera. Todos necesitan MH4U con la "
             "actualización 1.3.0; otra versión no encuentra tu Gathering Hall."),
         touch_look().glow);
    int others = 0;
    std::string who;
    for (const rooms::Member& m : st.members)
        if (other_game(m) && others++ < 3) who += (who.empty() ? "" : ", ") + m.nickname + " (" + m.game.name + ")";
    if (others)
        note(r, "net.cross.other",
             fmt(say("Playing another game, so not in your Gathering Hall: %s.",
                     "Juegan otro juego, así que no estarán en tu Gathering Hall: %s."),
                 who.c_str()),
             theme().warn);
}
// (S36: Multiplayer > Play with emulators, MENU.md: the note, the hunters of another game named)
MENU_ENTRY("multiplayer/crossplay", 10, [](std::vector<Row>& r) { crossplay_note(r, rooms::GetStatus()); });
// The Multiplayer section (MENU.md): where this phone is and its nickname on the section's page, with Leave while in a
// room; joining a room (an address, the rooms on this Wi-Fi, the lobby's public rooms), hosting one, the hunters in it
// and its chat on pages one level down
bool in_room_now() { return in_room(rooms::GetStatus()); }
void status_rows(std::vector<Row>& r) {  // (where this phone is: its colour at a glance, the details in its help, the reason under it)
    const rooms::Status st = rooms::GetStatus();
    Row row;
    row.id = "net.status", row.title = tr("Status"), row.value = status_text(st), row.ink = status_ink(st);
    row.help = status_help(st);
    r.push_back(std::move(row));
    if (st.state == rooms::State::Idle && st.error != rooms::Error::None) note(r, "net.why", error_text(st.error), theme().warn);
    const hubs::JoinStatus hub = hubs::GetJoinStatus();
    if (st.state == rooms::State::Idle && st.error == rooms::Error::None && hub.state == hubs::JoinState::Idle &&
        hub.error != hubs::JoinError::None)
        note(r, "net.hubwhy", hub_error_text(hub.error), theme().warn);
}
MENU_ENTRY("multiplayer", 10, status_rows);
void rows_multiplayer_20(std::vector<Row>& r) {
    Draft& c = s.draft;
    add_text(r, "net.nick", "Nickname", &c.nick, KB_NICK, 20, "Nickname", c.nick.empty() ? tr("Choose one") : c.nick,
             tr("4 to 20 letters, digits, spaces, '.', '_' or '-'."), check_nick);
    if (!rooms::ValidNickname(c.nick)) r.back().ink = theme().warn;
}
void reset_multiplayer_20() { s.draft.nick = defaults().nick; }
MENU_ENTRY("multiplayer", 20, rows_multiplayer_20, reset_multiplayer_20);
void rows_multiplayer_25(std::vector<Row>& r) {
    const rooms::Status st = rooms::GetStatus();
    const bool joining = st.state == rooms::State::Joining;
    if (in_room(st) || joining) action(r, "net.join", tr(joining ? "Stop joining" : "Leave the room"), []() { net_leave(); }, true, "Leave");
}
MENU_ENTRY("multiplayer", 25, rows_multiplayer_25);
// 1.0.2 (T38): the port's consoles in a room or hub that turned it on keep together, so one whose game stalls for a moment is
// waited for instead of dropped by the game (mh4n platform/netsync_native.h: net_sync in mh4u.ini, at once). Off as it comes.
// MHGU for Android has the same option and texts.
void rows_multiplayer_22(std::vector<Row>& r) {
    add_switch(r, "net.sync", en_es("Wait for a player who stalls", "Esperar al jugador que se traba"), &s.draft.net_sync,
               "net_sync");
    r.back().help = en_es("When another player's game stalls for a moment (a slow phone, the app in the background), yours "
                          "waits for it, so the game is less likely to drop that player. Only between players of this app "
                          "who turned it on; the whole room may pause for a moment.",
                          "Cuando el juego de otro jugador se traba un momento (un teléfono lento, la app en segundo plano), "
                          "el tuyo lo espera, así es menos probable que el juego lo eche. Solo entre los jugadores de esta "
                          "app que lo activaron; toda la sala puede detenerse un momento.");
}
void reset_multiplayer_22() { s.draft.net_sync = defaults().net_sync; }
MENU_ENTRY("multiplayer", 22, rows_multiplayer_22, reset_multiplayer_22);
// (1.0.1) Play online with: the hubs' pages, the rooms' pages, or both
bool hubs_shown() { return s.draft.play_with != PLAY_ROOMS; }
bool rooms_shown() { return s.draft.play_with != PLAY_HUBS; }
MENU_PAGE("multiplayer/hubs", "Join a hub", "multiplayer", 28, hubs_shown, [](Row& r) {
    const rooms::Status st = rooms::GetStatus();
    if (hubs::InHub(st)) r.value = st.room.name, r.ink = NET_GREEN;
    r.help = en_es("The hubs of the hub server, for your copy of the game: a tap joins one. No address to type and no port to open.",
                   "Los hubs del servidor de hubs, para tu copia del juego: un toque entra a uno. Sin dirección que escribir ni puertos que abrir.");
});
MENU_PAGE("multiplayer/hubhost", "Create a hub", "multiplayer", 29, hubs_shown, [](Row& r) {
    if (net_hosting_hub()) r.value = en_es("Hub open", "Hub abierto"), r.ink = NET_GREEN;
    r.help = en_es("A hub on this phone, listed on the hub server: the others join it from their own hub list.",
                   "Un hub en este teléfono, listado en el servidor de hubs: los demás entran desde su propia lista de hubs.");
});
MENU_PAGE("multiplayer/join", "Join a room", "multiplayer", 30, rooms_shown, [](Row& r) {
    r.value = s.draft.address;  // (the address it joins)
    r.help = say("Its address and password, the rooms on this Wi-Fi, and the public rooms of the lobby.",
                 "Su dirección y contraseña, las salas en esta Wi-Fi y las salas públicas del lobby.");
});
MENU_PAGE("multiplayer/host", "Host a room", "multiplayer", 35, rooms_shown, [](Row& r) {
    const rooms::Status st = rooms::GetStatus();
    if (st.hosting) r.value = tr("Room open"), r.ink = NET_GREEN;
    r.help = say("A room on this phone, which the others join.", "Una sala en este teléfono, a la que entran los demás.");
});
MENU_PAGE("multiplayer/room", "Hunters in the room", "multiplayer", 38, in_room_now, [](Row& r) {
    const rooms::Status st = rooms::GetStatus();
    r.value = fmt("%d / %d", (int)st.members.size(), (int)st.room.member_slots);
});
MENU_PAGE("multiplayer/chat", "Chat", "multiplayer", 40);  // (1.0: empty, the chat's rows are on the section's page)
MENU_PAGE("multiplayer/friends", "Friends", "multiplayer", 50);              // (1.0: the chat group's, S32)
MENU_PAGE("multiplayer/crossplay", "Play with emulators", "multiplayer", 60, rooms_shown);  // (1.0: the cross group's, S36)
MENU_PAGE("multiplayer/guide", "How to play online", "multiplayer", 90);       // (1.0: S34)
// Multiplayer > Join a room
MENU_ENTRY("multiplayer/join", 5, status_rows);
void rows_multiplayer_join_10(std::vector<Row>& r) {
    Draft& c = s.draft;
    const rooms::Status st = rooms::GetStatus();
    const bool in = in_room(st), joining = st.state == rooms::State::Joining;
    add_text(r, "net.address", "Room address", &c.address, KB_ADDRESS, 128, "Address", c.address.empty() ? tr("No address") : c.address,
             tr("Its IP or name, and :port if it is not 24872."), check_address);
    add_text(r, "net.password", "Password", &c.password, KB_SECRET, 64, "Password",
             c.password.empty() ? tr("No password") : "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2",  // (U+2022)
             tr("The room's, if it has one."));
    chat_password_row(r);
    if (in || joining)
        action(r, "net.join", tr(joining ? "Stop joining" : "Leave the room"), []() { net_leave(); }, true, "Leave");
    else
        action(r, "net.join", tr("Join the room"), []() { join_room(); }, true, "Join");
}
void reset_multiplayer_join_10() {
    Draft f = defaults();
    s.draft.address = f.address;
    s.draft.password = f.password;
}
MENU_ENTRY("multiplayer/join", 10, rows_multiplayer_join_10, reset_multiplayer_join_10);
MENU_ENTRY("multiplayer/join", 20, lan_rows);
void rows_multiplayer_join_30(std::vector<Row>& r) {
    Draft& c = s.draft;
    header(r, "net.lobby.head", tr("Public rooms"));
    lobby_row(r);
    const std::string url = lobby_url(c.lobby);
    const NetLobby lobby = net_lobby();
    const bool answered = lobby.done && lobby.url == url;
    // (asked as the page shows it, once the notice about playing online was closed: once a menu for each lobby)
    if (!url.empty() && !lobby.busy && !answered && s.lobby_asked != url && g_online_seen) {
        s.lobby_asked = url;
        net_lobby_fetch(url);
    }
    // (enabled while it asks, or the picked row would vanish under the finger: a second tap waits for the same answer)
    action(r, "net.refresh", tr("Show its rooms"), []() {
        const std::string u = lobby_url(s.draft.lobby);
        online_then(u, [u]() { net_lobby_fetch(u); });
    }, !url.empty(), "Refresh");
    r.back().value = lobby.busy ? tr("Asking...") : !answered ? "" : lobby.list.ok ? fmt("%d room(s)", (int)lobby.list.rooms.size()) : tr("No answer");
    if (lobby.busy) r.back().ink = touch_look().glow;
    if (answered && !lobby.list.ok) {  // (the runtime's reason, in English, goes to the log)
        r.back().ink = theme().warn;
        // (the default lobby's address is right: players run it and it is down at times, or a network blocks its port
        // 5000 - a player told to check the address had nothing to fix, #bug-reports "MH4U Online error")
        if (url == DEFAULT_LOBBY)
            note(r, "net.lobby.why",
                 say("The public lobby did not answer. Players run it and it is down at times, or this network blocks it: "
                     "try again later, or on another network (Wi-Fi or mobile data). Rooms by address still work.",
                     "El lobby público no respondió. Lo mantienen jugadores y a veces se cae, o esta red lo bloquea: prueba "
                     "más tarde o con otra red (Wi-Fi o datos móviles). Las salas por dirección siguen funcionando."),
                 theme().warn);
        else
            note(r, "net.lobby.why", say("The lobby did not answer with a list of rooms. Check its address: http://name:port.",
                                         "El lobby no respondió con una lista de salas. Revisa su dirección: http://nombre:puerto."),
                 theme().warn);
    }
    if (!answered || !lobby.list.ok) return;
    int shown = 0;
    for (const rooms::LobbyRoom& room : lobby.list.rooms) {
        if (++shown > 40) break;
        Row row;
        row.id = "net.lroom." + std::to_string(shown), row.title = room.name.empty() ? room.address : room.name;
        row.value = fmt("%d / %d", (int)room.players.size(), (int)room.max_players);
        if (room.password) row.value = std::string(tr("Password")) + "  " + row.value;
        if (!room.preferred) row.ink = touch_look().dim;
        std::string players;
        for (const std::string& p : room.players) players += (players.empty() ? "" : ", ") + p;
        row.help = fmt(say("%s. Host: %s. Game: %s. Hunters: %s. A tap joins it.", "%s. Anfitrión: %s. Juego: %s. Cazadores: %s. Un toque entra."),
                       room.description.empty() ? (room.address + ":" + std::to_string(room.port)).c_str() : room.description.c_str(),
                       room.owner.empty() ? "-" : room.owner.c_str(), room.game.empty() ? "-" : room.game.c_str(),
                       players.empty() ? "-" : players.c_str());
        row.arrow = true;
        const std::string address = room.address + ":" + std::to_string(room.port);
        const bool locked = room.password;
        row.activate = [address, locked]() {
            s.draft.address = address;
            if (!locked) {
                join_room();
                return;
            }
            open_keyboard(tr("Password"), s.draft.password, KB_SECRET, 64, tr("This room has a password."),
                          [](const std::string& typed, std::string&) {
                              s.draft.password = typed;
                              join_room();
                              return true;
                          });
        };
        r.push_back(std::move(row));
    }
}
void reset_multiplayer_join_30() { s.draft.lobby = defaults().lobby; }
MENU_ENTRY("multiplayer/join", 30, rows_multiplayer_join_30, reset_multiplayer_join_30);
// Multiplayer > Host a room
MENU_ENTRY("multiplayer/host", 5, status_rows);
void rows_multiplayer_host_10(std::vector<Row>& r) {
    Draft& c = s.draft;
    const rooms::Status st = rooms::GetStatus();
    add_text(r, "net.port", "Port (UDP)", &c.host_port, KB_DIGITS, 5, "HostPort", c.host_port, tr("1 to 65535; rooms use 24872."), check_port);
    add_number(r, "net.slots", "Hunters", &c.slots, 2, 16, 1, "%d", "Slots");
    if (st.hosting) {
        action(r, "net.host", tr("Close the room"), []() { net_close_room(); }, true, "CloseRoom");
        std::vector<std::string> mine = net_local_addresses();  // (what the others type to join it)
        r.back().value = (mine.empty() ? std::string("UDP") : mine[0]) + ":" + std::to_string(st.hosted_port);
        r.back().ink = NET_GREEN;
        std::string all;
        for (const std::string& a : mine) all += (all.empty() ? "" : ", ") + a;
        r.back().help = fmt(say("Closes the room on this phone: everyone in it leaves. The others join it at %s, port %d (UDP); from the internet, at your public address once that port is forwarded to this phone.",
                                "Cierra la sala de este teléfono: salen todos. Los demás entran en %s, puerto %d (UDP); desde internet, en tu dirección pública cuando ese puerto se redirija a este teléfono."),
                            all.empty() ? "?" : all.c_str(), (int)st.hosted_port);
    } else
        action(r, "net.host", tr("Host a room"), []() { host_room(); }, true, "Host");
}
void reset_multiplayer_host_10() {
    Draft f = defaults();
    s.draft.host_port = f.host_port;
    s.draft.slots = f.slots;
}
MENU_ENTRY("multiplayer/host", 10, rows_multiplayer_host_10, reset_multiplayer_host_10);
// Multiplayer > Hunters in the room
void rows_multiplayer_room_10(std::vector<Row>& r) {
    const rooms::Status st = rooms::GetStatus();
    const bool mod = st.state == rooms::State::Moderator;
    for (size_t i = 0; i < st.members.size(); i++) {
        const rooms::Member& m = st.members[i];
        const bool self = m.nickname == st.nickname;
        Row row;
        row.id = "net.member." + std::to_string(i), row.title = m.nickname;
        const bool mh4u = m.game.id == rooms::kMh4uUsa || m.game.id == rooms::kMh4uEurope;
        row.value = self ? tr("You") : mh4u ? "MH4U" : m.game.name.empty() ? "-" : m.game.name;
        if (self) row.ink = touch_look().glow;
        char mac[32];
        snprintf(mac, sizeof mac, "%02X:%02X:%02X:%02X:%02X:%02X", m.mac[0], m.mac[1], m.mac[2], m.mac[3], m.mac[4], m.mac[5]);
        row.help = fmt(say("Playing: %s. Its console's address in the room: %s.", "Juega: %s. La dirección de su consola en la sala: %s."),
                       m.game.name.empty() ? tr("nothing yet") : m.game.name.c_str(), mac);
        if (!self && other_game(m)) {  // (crossplay_note: not in your Gathering Hall)
            row.ink = theme().warn;
            row.help += " ";
            row.help += say("Another game: this hunter can't meet you in the Gathering Hall.",
                            "Otro juego: este cazador no puede encontrarte en el Gathering Hall.");
        }
        if (mod && !self) {  // (the room's moderator: a tap kicks or bans)
            row.help += " ";
            row.help += tr("A tap kicks this hunter out of the room, or bans them.");
            row.menu = true;
            Row copy = row;
            std::string nick = m.nickname;
            row.activate = [copy, nick]() {
                choices(copy, {tr("Kick"), tr("Ban")}, -1, [nick](int v) {
                    if (v == 0) rooms::Default().Kick(nick);
                    else rooms::Default().Ban(nick);
                });
            };
        }
        r.push_back(std::move(row));
    }
}
MENU_ENTRY("multiplayer/room", 10, rows_multiplayer_room_10, {}, in_room_now);
// Multiplayer > Chat (the room's)
// S32, S35 (the chat group's, MENU.md 4b): the room's chat is the chat's Room now, beside Global, General and the friends
// (View::Chat, View::Friends: settings_chat.inc, Room messages over the game on its page); Share this room in the chat
// while in one
void reset_chat_rows() { s.draft.notices = defaults().notices; }
MENU_ENTRY("multiplayer", 40, chat_entry_rows, reset_chat_rows);
// (not in a hub: its room's address is this phone's loopback, of no use to anyone else; a hub is found in the hub list)
MENU_ENTRY("multiplayer", 27, [](std::vector<Row>& r) { chat_share_room_row(r, rooms::GetStatus()); }, {},
           []() { return in_room_now() && !net_in_hub() && (!rooms::GetStatus().hosting || net_room_on_lan()); });

// ---------------------------------------------------------------- internet hubs (mh4n/hubs.h, 1.0.1)
// Multiplayer > Play online with: this app's hubs (a hub server lists them and carries the players: no address, no port),
// Azahar's and Citra's rooms (as in 1.0), or both. It only chooses which pages show; a room or hub joined stays.
void rows_multiplayer_15(std::vector<Row>& r) {
    add_choice(r, "net.playwith", en_es("Play online with", "Jugar en línea con"), s.draft.play_with,
               {en_es("This app's hubs", "Los hubs de esta app"), en_es("Azahar and Citra rooms", "Salas de Azahar y Citra"),
                en_es("Both", "Los dos")},
               [](int v) { s.draft.play_with = std::clamp(v, 0, 2); });
    r.back().help = en_es("This app's hubs: a hub server lists them and carries the players, so nobody types an address or opens a port; only this app's players, with the same region of the game. Azahar and Citra rooms: the rooms of 1.0 (an address, this Wi-Fi, the public lobby), with the emulators' players too. Both: both lists, and a hub you create also takes Azahar and Citra players at this phone's address. A room or hub you are in stays.",
                          "Los hubs de esta app: un servidor de hubs los lista y lleva a los jugadores, así que nadie escribe una dirección ni abre un puerto; solo jugadores de esta app, con la misma región del juego. Salas de Azahar y Citra: las salas de la 1.0 (una dirección, esta Wi-Fi, el lobby público), también con los jugadores de los emuladores. Los dos: las dos listas, y un hub que crees también recibe a jugadores de Azahar y Citra en la dirección de este teléfono. La sala o el hub en que estás sigue.");
}
void reset_multiplayer_15() { s.draft.play_with = defaults().play_with; }
MENU_ENTRY("multiplayer", 15, rows_multiplayer_15, reset_multiplayer_15);

// The hub server: the default (players run it), Custom... (typed) and Off, as the lobby's row
std::string g_custom_hub_server;
void hub_server_row(std::vector<Row>& rows) {
    const std::string now = s.draft.hub_server;
    const int custom = 1, off = 2;
    const int selected = now.empty() ? 0 : hub_server_off(now) ? off : custom;
    if (selected == custom) g_custom_hub_server = now;
    std::vector<std::string> names = {std::string(DEFAULT_HUB_SERVER) + "  (" + tr("Default") + ")",
                                      selected == custom ? fmt("Custom: %s", now.c_str()) : std::string(tr("Custom...")),
                                      en_es("Off: no hubs", "Ninguno: sin hubs")};
    Row r;
    r.id = "net.hubserver", r.title = en_es("Hub server", "Servidor de hubs");
    r.value = selected == 0 ? std::string(DEFAULT_HUB_SERVER) : selected == off ? std::string(en_es("No hubs", "Sin hubs")) : now;
    r.help = fmt(en_es("The server that lists the hubs and carries their players: everyone who uses the same one finds the others' hubs. The default, %s, is run by players, not by this project (MHGU's players use it too: each game's hubs are apart). Custom: type another, a name or IP and :port if it is not 39510. Off: no hubs.",
                       "El servidor que lista los hubs y lleva a sus jugadores: todos los que usan el mismo encuentran los hubs de los demás. El predeterminado, %s, lo mantienen jugadores, no este proyecto (también lo usan los jugadores de MHGU: los hubs de cada juego van aparte). Otro: escribe otro, un nombre o una IP y :puerto si no es el 39510. Ninguno: sin hubs."),
                 DEFAULT_HUB_SERVER);
    auto set = [custom, off](int i) {
        if (i == custom) {
            open_keyboard(en_es("Hub server", "Servidor de hubs"), g_custom_hub_server, KB_ADDRESS, 200,
                          en_es("A name or an IP, and :port if it is not 39510.", "Un nombre o una IP, y :puerto si no es el 39510."),
                          [](const std::string& typed, std::string& error) {
                              std::string host;
                              std::uint16_t port = 0;
                              if (!typed.empty() && !hub_server_off(typed) && !hubs::ParseServer(typed, &host, &port)) {
                                  error = en_es("That is not a server's address: a name or an IP, and :port if it is not 39510.",
                                                "Esa no es la dirección de un servidor: un nombre o una IP, y :puerto si no es el 39510.");
                                  return false;
                              }
                              s.draft.hub_server = typed;
                              if (!typed.empty() && !hub_server_off(typed)) g_custom_hub_server = typed;
                              return true;
                          });
            return;
        }
        s.draft.hub_server = i == off ? "Off" : "";
    };
    r.adjust = [selected, set](int) { set(selected == 0 ? 2 : 0); };  // (left / right: the default and Off; Custom... types)
    Row copy = r;
    r.menu = true;
    r.activate = [copy, names, selected, set]() { choices(copy, names, selected, set); };
    rows.push_back(std::move(r));
}
// What the hub server answered last (its list, this region's public hubs), as the Check row's value
std::string hub_state_text(const NetHubs& h) {
    switch (h.list.state) {
    case hubs::ServerState::Online: return fmt(en_es("Online, %d ms, %d hub(s)", "En línea, %d ms, %d hub(s)"), h.list.ms, (int)h.list.hubs.size());
    case hubs::ServerState::NotFound: return en_es("Not found", "No encontrado");
    case hubs::ServerState::Unreachable: return en_es("Unreachable", "Inalcanzable");
    case hubs::ServerState::Lost: return en_es("Connection lost", "Conexión perdida");
    case hubs::ServerState::NoAnswer: return en_es("No answer", "Sin respuesta");
    case hubs::ServerState::Busy: return en_es("Busy", "Ocupado");
    case hubs::ServerState::Version: return en_es("Other version", "Otra versión");
    case hubs::ServerState::Refused: return en_es("Refused", "Rechazado");
    default: return "";
    }
}
std::string g_hubs_asked;  // (the hub server the Join a hub page asked by itself since the menu opened)
void ask_hubs() {
    const std::string server = hub_server(s.draft.hub_server);
    online_then(server, [server]() { net_hubs_fetch(server); });
}
// The game's region has hubs (MH4U, USA or Europe): a note when it hasn't, or before the game has started
bool hub_game_ok(std::vector<Row>& r) {
    if (hubs::RegionOfProgram(hubs::GameProgram()) != hubs::Region::None) return true;
    note(r, "net.hubgame",
         hubs::GameProgram() == 0 ? en_es("Hubs work once the game has started.", "Los hubs funcionan una vez que arrancó el juego.")
                                  : en_es("This copy of the game has no hubs: they are for MH4U, USA or Europe (Monster Hunter 4G can't join them).",
                                          "Esta copia del juego no tiene hubs: son para MH4U, de EE. UU. o de Europa (Monster Hunter 4G no puede entrar)."),
         theme().warn);
    return false;
}
// Multiplayer > Join a hub
void join_hub(const hubs::Hub& hub, const std::string& password) {
    const std::string server = hub_server(s.draft.hub_server);
    switch (net_hub_join(server, hub, s.draft.nick, password)) {
    case NetProblem::Nickname: s.error = tr("Choose a nickname first."); focus("net.nick"); return;
    case NetProblem::NoGame: s.error = en_es("Hubs work once the game has started.", "Los hubs funcionan una vez que arrancó el juego."); return;
    case NetProblem::NoServer: s.error = en_es("Choose a hub server first.", "Primero elige un servidor de hubs."); focus("net.hubserver"); return;
    case NetProblem::None: break;
    default: s.error = en_es("Could not join the hub.", "No se pudo entrar al hub."); return;
    }
    s.error.clear();
    remember_network();
    focus("net.status");
}
void rows_multiplayer_hubs_10(std::vector<Row>& r) {
    hub_server_row(r);
    const std::string server = hub_server(s.draft.hub_server);
    const NetHubs got = net_hubs();
    const bool answered = got.done && got.server == server;
    // (asked as the page shows it, once the notice about playing online was closed: once a menu for each server)
    if (!server.empty() && !got.busy && !answered && g_hubs_asked != server && g_online_seen &&
        hubs::RegionOfProgram(hubs::GameProgram()) != hubs::Region::None) {
        g_hubs_asked = server;
        net_hubs_fetch(server);
    }
    action(r, "net.hubcheck", en_es("Check hub server", "Comprobar el servidor de hubs"), []() { ask_hubs(); }, !server.empty());
    r.back().value = got.busy ? std::string(en_es("Checking...", "Comprobando...")) : answered ? hub_state_text(got) : std::string(en_es("Not checked", "Sin comprobar"));
    r.back().ink = got.busy ? touch_look().glow : answered && got.list.state == hubs::ServerState::Online ? NET_GREEN : answered ? theme().warn : 0;
    r.back().help = en_es("Asks the hub server above whether it answers: how long it takes, and how many hubs it lists for your copy of the game. It also shows them below.",
                          "Le pregunta al servidor de hubs de arriba si responde: cuánto tarda y cuántos hubs lista para tu copia del juego. También los muestra abajo.");
    if (answered && got.list.state != hubs::ServerState::Online && got.list.state != hubs::ServerState::Unknown)
        note(r, "net.hubserver.why",
             server == DEFAULT_HUB_SERVER
                 ? en_es("The default hub server did not answer. Players run it and it is down at times, or this network blocks it: try again later, or on another network (Wi-Fi or mobile data). Rooms still work.",
                         "El servidor de hubs predeterminado no respondió. Lo mantienen jugadores y a veces se cae, o esta red lo bloquea: prueba más tarde o con otra red (Wi-Fi o datos móviles). Las salas siguen funcionando.")
                 : en_es("The hub server did not answer. Check its address: a name or an IP, and :port if it is not 39510.",
                         "El servidor de hubs no respondió. Revisa su dirección: un nombre o una IP, y :puerto si no es el 39510."),
             theme().warn);
}
void reset_multiplayer_hubs_10() { s.draft.hub_server = defaults().hub_server; }
MENU_ENTRY("multiplayer/hubs", 5, status_rows);
MENU_ENTRY("multiplayer/hubs", 10, rows_multiplayer_hubs_10, reset_multiplayer_hubs_10);
void rows_multiplayer_hubs_20(std::vector<Row>& r) {
    header(r, "net.hubs.head", en_es("Hubs", "Hubs"));
    if (!hub_game_ok(r)) return;
    const std::string server = hub_server(s.draft.hub_server);
    const NetHubs got = net_hubs();
    const bool answered = got.done && got.server == server && got.list.state == hubs::ServerState::Online;
    const rooms::Status st = rooms::GetStatus();
    if (in_room(st) || st.state == rooms::State::Joining || hubs::GetJoinStatus().state != hubs::JoinState::Idle)
        action(r, "net.join", tr(st.state == rooms::State::Joining || !in_room(st) ? "Stop joining" : "Leave the room"), []() { net_leave(); }, true, "Leave");
    if (!answered) {
        if (!server.empty() && !got.busy && got.done && got.server == server) return;  // (the reason is under Check)
        note(r, "net.hubs.none", server.empty() ? en_es("No hub server: choose one above.", "Sin servidor de hubs: elige uno arriba.")
                                                : got.busy ? en_es("Asking the hub server...", "Preguntando al servidor de hubs...")
                                                           : en_es("Check hub server shows its hubs.", "Comprobar el servidor de hubs muestra sus hubs."),
             touch_look().dim);
        return;
    }
    if (got.list.hubs.empty()) {
        note(r, "net.hubs.empty", en_es("No hub open now for your copy of the game. Create one, and the others find it here.",
                                        "Ahora no hay ningún hub abierto para tu copia del juego. Crea uno, y los demás lo encuentran aquí."),
             touch_look().dim);
        return;
    }
    int shown = 0;
    for (const hubs::Hub& hub : got.list.hubs) {
        if (++shown > 40) break;
        Row row;
        row.id = "net.hub." + std::to_string(shown), row.title = hub.name.empty() ? hub.host : hub.name;
        row.value = fmt("%d / %d", (int)hub.players, (int)hub.slots);
        if (hub.password) row.value = std::string(tr("Password")) + "  " + row.value;
        if (hub.players >= hub.slots) row.ink = touch_look().dim;
        std::string hunters;
        for (const std::string& h : hub.hunters) hunters += (hunters.empty() ? "" : ", ") + h;
        row.help = fmt(en_es("%sHost: %s. Hunters: %s. App %s. %s A tap joins it.", "%sAnfitrión: %s. Cazadores: %s. App %s. %s Un toque entra."),
                       hub.description.empty() ? "" : (hub.description + ". ").c_str(), hub.host.empty() ? "-" : hub.host.c_str(),
                       hunters.empty() ? "-" : hunters.c_str(), hub.app_version.empty() ? "-" : hub.app_version.c_str(),
                       hub.password ? en_es("It has a password.", "Tiene contraseña.") : en_es("No password.", "Sin contraseña."));
        row.arrow = true;
        const hubs::Hub picked = hub;
        row.activate = [picked]() {
            const std::string server = hub_server(s.draft.hub_server);
            if (!picked.password) {
                online_then(server, [picked]() { join_hub(picked, ""); });
                return;
            }
            open_keyboard(tr("Password"), s.draft.password, KB_SECRET, 64, en_es("This hub has a password.", "Este hub tiene contraseña."),
                          [picked, server](const std::string& typed, std::string&) {
                              s.draft.password = typed;
                              online_then(server, [picked, typed]() { join_hub(picked, typed); });
                              return true;
                          });
        };
        r.push_back(std::move(row));
    }
}
MENU_ENTRY("multiplayer/hubs", 20, rows_multiplayer_hubs_20);

// Multiplayer > Create a hub: a room on this phone (on the loopback; in Both also at this phone's address), listed
void create_hub() {
    Draft& d = s.draft;
    const std::string server = hub_server(d.hub_server);
    switch (net_hub_create(server, d.hub_name, d.hub_tags, d.hub_slots, d.nick, d.hub_password, d.host_port, d.play_with == PLAY_BOTH)) {
    case NetProblem::Nickname: s.error = tr("Choose a nickname first."); focus("net.nick"); return;
    case NetProblem::NoGame: s.error = en_es("Hubs work once the game has started.", "Los hubs funcionan una vez que arrancó el juego."); return;
    case NetProblem::NoServer: s.error = en_es("Choose a hub server first.", "Primero elige un servidor de hubs."); focus("net.hubserver"); return;
    case NetProblem::Port: s.error = tr("A port is a number from 1 to 65535."); focus("net.hubport"); return;
    case NetProblem::PortBusy: s.error = fmt("UDP port %s is in use on this phone: choose another one.", d.host_port.c_str()); focus("net.hubport"); return;
    default: break;
    }
    s.error.clear();
    remember_network();
    focus("net.status");
}
void rows_multiplayer_hubhost_10(std::vector<Row>& r) {
    Draft& c = s.draft;
    if (!hub_game_ok(r)) return;
    const bool hosting = net_hosting_hub();
    add_text(r, "net.hubname", en_es("Hub name", "Nombre del hub"), &c.hub_name, KB_ANY, 48, "HubName",
             c.hub_name.empty() ? (c.nick.empty() ? std::string("MH4U") : "MH4U - " + c.nick) : c.hub_name,
             en_es("As the hub list shows it.", "Como lo muestra la lista de hubs."));
    r.back().help = en_es("The hub's name in everyone's hub list. Empty: MH4U - your nickname.", "El nombre del hub en la lista de todos. Vacío: MH4U - tu apodo.");
    add_text(r, "net.hubtags", en_es("Description", "Descripción"), &c.hub_tags, KB_ANY, 100, "HubTags",
             c.hub_tags.empty() ? std::string(en_es("None", "Ninguna")) : c.hub_tags,
             en_es("For example: HR 7, G-rank, English.", "Por ejemplo: HR 7, rango G, español."));
    r.back().help = en_es("A few words the hub list shows under its name: your rank, what you hunt, your language.",
                          "Unas palabras que la lista de hubs muestra bajo su nombre: tu rango, qué cazas, tu idioma.");
    add_text(r, "net.hubpassword", en_es("Password", "Contraseña"), &c.hub_password, KB_SECRET, 64, "HubPassword",
             c.hub_password.empty() ? std::string(tr("No password")) : "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2",
             en_es("Empty: anyone can join.", "Vacía: puede entrar cualquiera."));
    r.back().help = en_es("Players who join the hub type it. The hub list shows a lock; the password itself never leaves this phone and the room. Empty: anyone can join.",
                          "Los que entran al hub la escriben. La lista de hubs muestra un candado; la contraseña nunca sale de este teléfono y la sala. Vacía: puede entrar cualquiera.");
    add_number(r, "net.hubslots", en_es("Hunters", "Cazadores"), &c.hub_slots, 2, 4, 1, "%d");
    r.back().help = en_es("How many hunters the hub takes, you included (the Gathering Hall has 4 places).",
                          "Cuántos cazadores entran al hub, contándote a ti (el Gathering Hall tiene 4 lugares).");
    if (c.play_with == PLAY_BOTH) {
        add_text(r, "net.hubport", en_es("Port (UDP)", "Puerto (UDP)"), &c.host_port, KB_DIGITS, 5, "HostPort", c.host_port,
                 tr("1 to 65535; rooms use 24872."), check_port);
        r.back().help = en_es("Play online with is Both: the hub also takes Azahar and Citra players, who join this phone's address and this port as a room (from the internet, once the port is forwarded to this phone). This app's players come through the hub server.",
                              "Jugar en línea con está en Los dos: el hub también recibe a jugadores de Azahar y Citra, que entran a la dirección de este teléfono y este puerto como a una sala (desde internet, cuando el puerto se redirige a este teléfono). Los jugadores de esta app llegan por el servidor de hubs.");
    }
    hub_server_row(r);
    if (!hosting) {
        action(r, "net.hubcreate", en_es("Create the hub", "Crear el hub"), []() {
            online_then(hub_server(s.draft.hub_server), []() { create_hub(); });
        }, !hub_server(c.hub_server).empty());
        r.back().help = en_es("Opens a hub on this phone, lists it on the hub server and joins it as its moderator. The others join it from their hub list: no address and no port. It stays open until you close it or close the app; keep the app open while others play in it.",
                              "Abre un hub en este teléfono, lo lista en el servidor de hubs y entra como su moderador. Los demás entran desde su lista de hubs: sin dirección ni puertos. Queda abierto hasta que lo cierres o cierres la app; deja la app abierta mientras otros juegan en él.");
        return;
    }
    const hubs::HostStatus host = hubs::GetHostStatus();
    action(r, "net.hubcreate", en_es("Close the hub", "Cerrar el hub"), []() { net_hub_close(); });
    std::string listed;
    switch (host.state) {
    case hubs::ServerState::Online: listed = en_es("Listed", "Listado"); break;
    case hubs::ServerState::Checking: listed = en_es("Listing...", "Listando..."); break;
    default: listed = en_es("Not listed", "Sin listar"); break;
    }
    if (host.links > 0) listed += fmt(en_es(", %d through the server", ", %d por el servidor"), (int)host.links);
    r.back().value = listed;
    r.back().ink = host.state == hubs::ServerState::Online ? NET_GREEN : host.state == hubs::ServerState::Checking ? touch_look().glow : theme().warn;
    r.back().help = en_es("Closes the hub: it leaves the list, and everyone in it leaves.", "Cierra el hub: sale de la lista, y salen todos.");
    if (host.state != hubs::ServerState::Online && host.state != hubs::ServerState::Checking) {
        std::string why;
        switch (host.state) {
        case hubs::ServerState::NotFound: why = en_es("its name has no address", "su nombre no tiene dirección"); break;
        case hubs::ServerState::Unreachable: why = en_es("it can't be reached", "no se llega a él"); break;
        case hubs::ServerState::Lost: why = en_es("the connection was lost", "se perdió la conexión"); break;
        case hubs::ServerState::Busy: why = en_es("it is busy (too many hubs from this address)", "está ocupado (demasiados hubs desde esta dirección)"); break;
        case hubs::ServerState::Version: why = en_es("it runs another version", "usa otra versión"); break;
        default: why = en_es("it did not answer", "no respondió"); break;
        }
        note(r, "net.hubhost.why",
             fmt(en_es("Your hub is not listed on %s: %s. It tries again by itself; players already in it stay.",
                       "Tu hub no está listado en %s: %s. Vuelve a probar solo; los que ya están adentro siguen."),
                 host.server.c_str(), why.c_str()),
             theme().warn);
    }
    if (c.play_with == PLAY_BOTH) {
        std::vector<std::string> mine = net_local_addresses();
        const rooms::Status st = rooms::GetStatus();
        note(r, "net.hubhost.cross",
             fmt(en_es("Azahar and Citra players join it as a room at %s:%d (UDP).", "Los jugadores de Azahar y Citra entran como a una sala en %s:%d (UDP)."),
                 mine.empty() ? "?" : mine[0].c_str(), (int)st.hosted_port),
             touch_look().dim);
    }
}
void reset_multiplayer_hubhost_10() {
    Draft f = defaults();
    s.draft.hub_name = f.hub_name;
    s.draft.hub_tags = f.hub_tags;
    s.draft.hub_password = f.hub_password;
    s.draft.hub_slots = f.hub_slots;
}
MENU_ENTRY("multiplayer/hubhost", 5, status_rows);
MENU_ENTRY("multiplayer/hubhost", 10, rows_multiplayer_hubhost_10, reset_multiplayer_hubhost_10);

// Multiplayer > How to play online (S34's page, empty until 1.0.1): the hubs and the rooms in a few lines
void rows_multiplayer_guide_10(std::vector<Row>& r) {
    header(r, "guide.hubs", en_es("Hubs (this app's players)", "Hubs (jugadores de esta app)"));
    note(r, "guide.hubs.1", en_es("One hunter: Multiplayer > Create a hub. The others: Multiplayer > Join a hub, and a tap on it. Nobody types an address or opens a port: the hub server carries the players.",
                                  "Un cazador: Multijugador > Crear un hub. Los demás: Multijugador > Entrar a un hub, y un toque en él. Nadie escribe una dirección ni abre un puerto: el servidor de hubs lleva a los jugadores."));
    note(r, "guide.hubs.2", en_es("Then, in the game: the Gathering Hall's Local Play. One creates a hall, the others find it there. The chat button talks to the hub.",
                                  "Después, en el juego: Local Play (juego local) en el Gathering Hall. Uno crea una sala del gremio y los demás la buscan ahí. El botón del chat habla con el hub."));
    note(r, "guide.hubs.3", en_es("Everyone needs the same region of the game (USA with USA, Europe with Europe) and should have the same version of this app. Keep the app open while you play together.",
                                  "Todos necesitan la misma región del juego (EE. UU. con EE. UU., Europa con Europa) y conviene la misma versión de esta app. Deja la app abierta mientras juegan juntos."));
    header(r, "guide.rooms", en_es("Rooms (also Azahar and Citra)", "Salas (también Azahar y Citra)"));
    note(r, "guide.rooms.1", en_es("Join a room by its address, on this Wi-Fi or from the public lobby, or host one on this phone (from the internet, its UDP port must be forwarded to this phone). Emulator players can be in the same room.",
                                   "Entra a una sala por su dirección, en esta Wi-Fi o desde el lobby público, o crea una en este teléfono (desde internet, hay que redirigir su puerto UDP a este teléfono). Los jugadores de emuladores pueden estar en la misma sala."));
    note(r, "guide.rooms.2", en_es("Play online with chooses which of the two the Multiplayer section shows.", "Jugar en línea con elige cuál de los dos muestra la sección Multijugador."));
}
MENU_ENTRY("multiplayer/guide", 10, rows_multiplayer_guide_10);

// ---------------------------------------------------------------- the GPU driver (gpu_driver.h)
// Why an import did not install anything (DriverActivity.java's codes)
std::string import_error(const GpuDriverImport& in) {
    std::string why = in.error == "no_library" ? say("The .zip has no driver library (.so) in it.", "El .zip no trae ninguna librería de driver (.so).")
                      : in.error == "not_arm64" ? say("Its library is not for this phone (64-bit ARM).", "Su librería no es para este teléfono (ARM de 64 bits).")
                      : in.error == "too_large" ? say("The .zip is too large for a driver.", "El .zip es demasiado grande para un driver.")
                                                : say("The file could not be read as a driver's .zip.", "No se pudo leer el archivo como el .zip de un driver.");
    if (in.error == "unreadable" && !in.detail.empty()) why += " (" + in.detail + ")";
    return say("Not installed: ", "No se instaló: ") + why;
}

// The section at the end of Graphics & Audio: the warning, the driver for the next start (the system's or one installed),
// the one running now, a driver that did not work, Restart when the choice isn't the one running, Import and Remove. On
// any GPU since 1.0.2 (S12, 4u-native #80: an Adreno only before); the MHGU port's the same rows and words.
void driver_rows(std::vector<Row>& r) {
#ifndef __ANDROID__
    (void)r;  // (an Adreno phone's own driver: not on a PC, whose GPU choice is pc/settings_pc.inc's)
    return;
#endif
    Draft& c = s.draft;
    {  // (an import that came back from DriverActivity.java: the driver installed is chosen, for Save)
        const GpuDriverImport in = gpu_driver_import_result();
        if (in.done) {
            s.driver_note_bad = !in.ok;
            s.driver_note = in.ok ? fmt("Installed: %s. It is chosen: save, then restart the game.", in.name.c_str()) : import_error(in);
            if (in.ok) c.driver = in.folder, focus("driver");
        }
    }
    const GpuDriverStatus st = gpu_driver_status();
    const std::vector<GpuDriver> list = gpu_driver_list();
    header(r, "driver.head", tr("GPU driver"));
    // (before any choice: what a driver of one's own can do, on any GPU)
    note(r, "driver.warn", say("A driver that isn't the system's can close the game, draw wrong or not start. It's at your own risk; if it doesn't start, the app goes back to the system's by itself.",
                               "Un driver que no es el del sistema puede cerrar el juego, dibujar mal o no arrancar. Es bajo tu responsabilidad; si no arranca, la app vuelve sola al del sistema."),
         theme().warn);
    {  // the choice: the system's, then each one installed
        std::vector<std::string> names = {tr("System")};
        int selected = c.driver.empty() ? 0 : -1;
        for (size_t i = 0; i < list.size(); i++) {
            names.push_back(list[i].name);
            if (list[i].folder == c.driver) selected = (int)i + 1;
        }
        add_choice(r, "driver", "Driver", selected, names,
                   [list](int v) { s.draft.driver = v <= 0 || v > (int)list.size() ? "" : list[v - 1].folder; }, "Driver", c.driver);
        r.back().cold = true;
    }
    {  // what runs now, as the driver calls itself
        Row row;
        row.id = "driver.running", row.title = tr("Running now");
        row.value = !st.made ? tr("Starting...") : st.running.empty() ? tr("System") : st.running;
        if (!st.running.empty()) row.ink = NET_GREEN;
        row.help = st.made ? fmt(say("%s. Its driver: %s.", "%s. Su driver: %s."), st.gpu.c_str(), st.driver.c_str())
                           : say("The game is starting its GPU.", "El juego está iniciando su GPU.");
        r.push_back(std::move(row));
    }
    if (!st.failed.empty()) {  // (told here: the next start doesn't say it again)
        note(r, "driver.failed", fmt("%s did not work: the system's driver was used instead.", st.failed.c_str()), theme().warn);
        if (!s.failure_told) gpu_driver_failure_seen(), s.failure_told = true;
    }
    if (!s.driver_note.empty()) note(r, "driver.note", s.driver_note, s.driver_note_bad ? theme().warn : NET_GREEN);
    if (st.made && c.driver != st.running_folder) {  // (a driver is opened when the game starts)
        action(r, "driver.restart", tr("Restart the game"), []() { push(View::Restart, "dialog.cancel"); });
        r.back().help = say("The driver chosen is opened when the game starts: this closes the game and opens it again. Save your hunt in the game first.",
                            "El driver elegido se abre al iniciar el juego: esto cierra el juego y lo abre de nuevo. Guarda antes tu cacería en el juego.");
    }
    action(r, "driver.import", tr("Import a driver"), []() { gpu_driver_import(); }, true, "DriverImport");
    action(r, "driver.remove", tr("Remove a driver"),
           [list]() {
               std::vector<std::string> names;
               for (const GpuDriver& d : list) names.push_back(d.name);
               Row row;
               row.id = "driver.remove", row.title = tr("Remove a driver");
               choices(row, names, -1, [list](int i) {
                   if (i < 0 || i >= (int)list.size()) return;
                   s.remove_folder = list[i].folder, s.remove_name = list[i].name;
                   push(View::RemoveDriver, "dialog.cancel");
               });
           },
           !list.empty(), "DriverRemove");
}

// ---------------------------------------------------------------- the Mods and Cheats tabs (content.h)
// Why an import did not bring anything (ImportActivity.java's codes)
std::string import_problem(const ContentImport& in) {
    std::string why =
        in.error == "no_cheats"       ? say("The file has no cheats in it: a cheat starts with its [Name], then its code lines.",
                                            "El archivo no trae trucos: un truco empieza con su [Nombre] y sigue con sus líneas de código.")
        : in.error == "no_parts"      ? say("No romfs or exefs folder, nor the game's own folders (data, eng...), was found in it (up to four folders down).",
                                            "No se encontró una carpeta romfs ni exefs adentro, ni las carpetas propias del juego (data, eng...) (hasta cuatro carpetas de profundidad).")
        : in.error == "not_this_game" ? fmt(say("It is a mod for another game (%s): this one is 0004000000126300.",
                                                "Es un mod de otro juego (%s): este es el 0004000000126300."), in.detail.c_str())
        : in.error == "not_zip"       ? say("The file is not a .zip.", "El archivo no es un .zip.")
        : in.error == "too_large"     ? say("The file is too large for a cheats file.", "El archivo es demasiado grande para ser de trucos.")
        : in.error == "unwritable"    ? say("It could not be written in the app's folder: is the phone's storage full?",
                                            "No se pudo escribir en la carpeta de la app: ¿está llena la memoria del teléfono?")
        : in.error == "not_started"   ? say("The game has not started yet: try again in a moment.", "El juego todavía no arrancó: prueba de nuevo en un momento.")
                                      : say("The file could not be read.", "No se pudo leer el archivo.");
    if (in.error == "unreadable" && !in.detail.empty()) why += " (" + in.detail + ")";
    return say("Not added: ", "No se agregó: ") + why;
}

// The mods as the folder has them now: the draft keeps its order and switches (a new mod goes at its end, as the folder
// has it), unless it had nothing unsaved
void reload_mods() {
    g_mod_info = content_mods();
    g_mods_found_off = content_mods_found_off();
    std::vector<Draft::ModRow> disk, next;
    for (const auto& m : g_mod_info) disk.push_back({m.name, m.on});
    for (const auto& row : s.draft.mods)
        if (mod_info(row.name)) next.push_back(row);
    for (const auto& row : disk)
        if (std::none_of(next.begin(), next.end(), [&](const Draft::ModRow& n) { return n.name == row.name; })) next.push_back(row);
    const bool clean = s.draft.mods == s.saved.mods;
    s.saved.mods = disk;
    s.draft.mods = clean ? disk : next;
    s.mod_scan.reset();  // (compared again)
}

// ---- what the mods change in common (MHGU's conflict check, Apply suggested order, Rescan)
int mod_index(const std::string& name) {
    for (int i = 0; i < (int)s.draft.mods.size(); i++)
        if (s.draft.mods[i].name == name) return i;
    return -1;
}
// The pairs of mods that are both on and change the same files, as the draft orders them: first, second (the one used)
struct ModClash {
    std::string first, second;
    int files = 0;
};
std::vector<ModClash> mod_clashes() {
    std::vector<ModClash> out;
    const auto scan = s.mod_scan;
    if (!scan || !scan->done.load()) return out;
    for (const ContentModScan::Pair& p : scan->conflicts) {
        int a = mod_index(scan->names[p.a]), b = mod_index(scan->names[p.b]);
        if (a < 0 || b < 0 || !s.draft.mods[a].on || !s.draft.mods[b].on) continue;
        if (a > b) std::swap(a, b);
        out.push_back({s.draft.mods[a].name, s.draft.mods[b].name, p.files});
    }
    return out;
}
// Suggested order, as MHGU's: the mods that are on and replace each other's files keep their places among them, the ones
// that change the most files first and the small tweaks last, so a tweak wins over an overhaul
std::vector<Draft::ModRow> suggested_mods() {
    std::vector<Draft::ModRow> order = s.draft.mods;
    const auto scan = s.mod_scan;
    if (!scan || !scan->done.load()) return order;
    std::vector<std::string> clashing;
    for (const ModClash& c : mod_clashes())
        for (const std::string* n : {&c.first, &c.second})
            if (std::find(clashing.begin(), clashing.end(), *n) == clashing.end()) clashing.push_back(*n);
    std::vector<int> slots;
    std::vector<Draft::ModRow> involved;
    for (int i = 0; i < (int)order.size(); i++)
        if (std::find(clashing.begin(), clashing.end(), order[i].name) != clashing.end()) slots.push_back(i), involved.push_back(order[i]);
    auto files = [&](const std::string& name) {
        for (size_t k = 0; k < scan->names.size(); k++)
            if (scan->names[k] == name) return scan->files[k];
        return 0;
    };
    std::stable_sort(involved.begin(), involved.end(),
                     [&](const Draft::ModRow& a, const Draft::ModRow& b) { return files(a.name) > files(b.name); });
    for (size_t k = 0; k < slots.size(); k++) order[slots[k]] = involved[k];
    return order;
}
// One row per pair: which of the two is used; a tap chooses (the one chosen moves just below the other)
void conflict_rows(std::vector<Row>& r) {
    int k = 0;
    for (const ModClash& c : mod_clashes()) {
        Row row;
        row.id = "mods.conflict." + std::to_string(k++);
        row.title = fmt("%s and %s both change %d file(s); %s is used", c.first.c_str(), c.second.c_str(), c.files, c.second.c_str());
        row.help = say("Mods load from top to bottom: where two change the same file, the later one is used. A tap chooses which.",
                       "Los mods se cargan de arriba abajo: donde dos cambian el mismo archivo, se usa el de más abajo. Un toque elige cuál.");
        row.arrow = true;
        Row copy = row;
        copy.title = tr("Which mod is used?");
        row.menu = true;
        const std::string a = c.first, b = c.second;
        row.activate = [copy, a, b]() {
            choices(copy, {fmt("Use %s", a.c_str()), fmt("Use %s", b.c_str())}, 1, [a, b](int v) {
                const int ia = mod_index(a), ib = mod_index(b), win = v ? ib : ia, lose = v ? ia : ib;
                if (ia < 0 || ib < 0 || win > lose) return;  // (already below it)
                const Draft::ModRow moved = s.draft.mods[win];
                s.draft.mods.erase(s.draft.mods.begin() + win);
                s.draft.mods.insert(s.draft.mods.begin() + lose, moved);  // (the other is one place earlier now)
            });
        };
        r.push_back(std::move(row));
    }
}

// The cheats as the engine has them now (one imported or removed): each keeps its switch in the draft, by its name
void sync_cheats() {
    const std::vector<ContentCheat> now = content_cheats();
    std::vector<std::string> names;
    for (const ContentCheat& c : now) names.push_back(c.name);
    if (names == g_cheat_names && now.size() == s.draft.cheats.size()) return;
    std::vector<char> draft, saved;
    for (const ContentCheat& c : now) {
        char on = c.on;
        for (size_t j = 0; j < g_cheat_names.size() && j < s.draft.cheats.size(); j++)
            if (g_cheat_names[j] == c.name) on = s.draft.cheats[j];
        draft.push_back(on);
        saved.push_back(c.on);
    }
    g_cheat_names = names;
    s.draft.cheats = draft;
    s.saved.cheats = saved;
}

void remove_mod() {
    const std::string name = s.remove_mod;
    if (!content_mod_remove(name)) {
        s.error = tr("Could not remove the mod.");
        return;
    }
    for (Draft* d : {&s.draft, &s.saved})
        d->mods.erase(std::remove_if(d->mods.begin(), d->mods.end(), [&](const Draft::ModRow& m) { return m.name == name; }),
                      d->mods.end());
    reload_mods();
    s.mods_note = fmt("Removed: %s.", name.c_str());
    s.mods_note_bad = false;
    pop();
    focus("mods.add");
}

void remove_cheat() {
    const size_t i = s.remove_cheat;
    if (i >= g_cheat_names.size() || !content_cheat_remove(i)) {
        s.error = tr("Could not remove the cheat.");
        return;
    }
    for (Draft* d : {&s.draft, &s.saved})
        if (i < d->cheats.size()) d->cheats.erase(d->cheats.begin() + (long)i);
    g_cheat_names.erase(g_cheat_names.begin() + (long)i);
    s.cheats_note = fmt("Removed: %s.", s.remove_name.c_str());
    s.cheats_note_bad = false;
    pop();
    focus(g_cheat_names.empty() ? "cheats.add" : "cheats.remove");
}

// What a mod holds and where it comes from, in words
std::string mod_help(const mh4n::mods::Mod& m) {
    std::string text;
    if (m.romfs && m.code)
        text = say("It changes the game's files and its code.", "Cambia archivos del juego y su código.");
    else if (m.code)
        text = say("It changes the game's code.", "Cambia el código del juego.");
    else if (m.romfs)
        text = say("It changes the game's files.", "Cambia archivos del juego.");
    else
        text = say("It has nothing the game reads: no romfs and no exefs.", "No tiene nada que el juego lea: ni romfs ni exefs.");
    if (m.code)
        text += " " + std::string(say("The code library must be made with it turned on (System › Game code › Build the game code), or the game won't start.",
                                      "La librería de código tiene que hacerse con él activado (Sistema › Código del juego › Crear el código del juego), o el juego no arranca."));
    text += " " + std::string(say("A tap turns it on or off, moves it or removes it. Lower in the list wins where two mods change the same file. It loads when the game starts.",
                                  "Un toque lo activa o lo desactiva, lo mueve o lo quita. Más abajo en la lista gana donde dos mods cambian el mismo archivo. Se carga al iniciar el juego."));
    if (m.from_game_folder)
        text += " " + std::string(say("It comes from the mods folder beside the game: to remove it, delete it there.",
                                      "Viene de la carpeta mods al lado del juego: para quitarlo, bórralo de ahí."));
    return text;
}

// S48 (4ucode): code mods that don't go together, one warning row each, its help saying what happens and what to do.
// MENU.md 3.5: they belong on mods/list, after the mods.
void code_conflict_rows(std::vector<Row>& r, const std::vector<Draft::ModRow>& mods) {
    using Kind = mh4n::code_patch::CodeConflict::Kind;
    int k = 0;
    for (const auto& c : content_code_conflicts(draft_mods(mods))) {
        Row row;
        row.id = "mods.codeclash." + std::to_string(k++);
        row.title = fmt("Code mods clash: %s, %s", c.a.c_str(), c.b.c_str());
        row.value = "!";
        row.ink = theme().warn;
        switch (c.kind) {
        case Kind::PatchOnReplacement:
            row.help = fmt("%s patches the game's code, but %s replaces that code (as Heavenfall does): the patch lands on the other mod's code and breaks it. Turn one of them off.",
                           c.a.c_str(), c.b.c_str());
            break;
        case Kind::Replaced:
            row.help = fmt("%s and %s both replace the game's code: only %s's runs. Turn one of them off.", c.a.c_str(),
                           c.b.c_str(), c.b.c_str());
            break;
        case Kind::Exheader:
            row.help = fmt("%s brings an exheader.bin for its own code, but the code that runs is %s's: the game won't start. Turn one of them off.",
                           c.a.c_str(), c.b.c_str());
            break;
        case Kind::Overlap:
            row.help = fmt("%s and %s patch the same bytes of the game's code: the one lower in the list wins there and the other breaks. Turn one of them off.",
                           c.a.c_str(), c.b.c_str());
            break;
        case Kind::Bps:
            row.help = fmt("%s's BPS patch checks the code it was made for, and %s's patch changes that code first: it won't apply and the game won't start. Turn one of them off.",
                           c.a.c_str(), c.b.c_str());
            break;
        }
        row.enabled = true;
        row.activate = []() {};
        r.push_back(std::move(row));
    }
}

// S45/S47 (4ucode): a code mod that brings its own exheader.bin (Heavenfall: its code is the whole program, with code
// of its own after the game's) is on: one note row, its help saying what to keep in mind. MENU.md 3.5: mods/list.
void program_mod_rows(std::vector<Row>& r, const std::vector<Draft::ModRow>& mods) {
    std::string name;  // (the last one's: only its program runs)
    for (const auto& m : draft_mods(mods))
        if (m.on && m.exheader) name = m.name;
    if (!name.empty()) {
        const mh4n::mods::Mod m{name};
        Row row;
        row.id = "mods.program." + m.name;
        row.title = fmt("%s replaces the game's program", m.name.c_str());
        row.value = "?";
        row.ink = theme().warn;
        row.help = tr("Mods like Heavenfall bring the whole program with their own code. Export your save before trying one (Your game, Export save): it can keep items and settings of the mod that the game without it doesn't read. Build the code library again with every new version of the mod or when you change its installer's options. Keep the app's cheats off with it. Its own frame-rate, aspect-ratio and single-screen options can clash with the port's.");
        row.enabled = true;
        row.activate = []() {};
        r.push_back(std::move(row));
    }
}

void mods_rows(std::vector<Row>& r) {
    Draft& c = s.draft;
    {  // (an import that came back from ImportActivity.java)
        const ContentImport in = content_mods_import_result();
        if (in.done) {
            s.mods_note_bad = !in.ok;
            if (in.ok) {
                reload_mods();
                s.mods_note = in.code ? fmt("Added: %s. It changes the game's code: checking the code library...", in.name.c_str())
                                      : fmt("Added: %s. It is on, and loads when the game starts again.", in.name.c_str());
                if (in.code) s.try_code_mod = in.name;
                focus("mod." + in.name);
            } else
                s.mods_note = import_problem(in);
        }
    }
    if (!s.try_code_mod.empty()) {  // (a code mod just added starts off; on if the code library was made with it)
        std::vector<Draft::ModRow> rows = c.mods;
        for (auto& m : rows)
            if (m.name == s.try_code_mod) m.on = true;
        const ContentCodeCheck check = content_code_check(draft_mods(rows));
        if (!check.busy) {
            const std::string name = s.try_code_mod;
            s.try_code_mod.clear();
            if (check.known && check.matches) {
                c.mods = rows;
                s.mods_note = fmt("Added: %s. The code library was made with it, so it is on: save, then restart the game.", name.c_str());
                s.mods_note_bad = false;
            } else {
                s.mods_note = fmt("Added: %s, off. It changes the game's code and the code library was not made with it: make the library again with this mod (System › Game code › Build the game code) before turning it on, or the game won't start.", name.c_str());
                s.mods_note_bad = true;
            }
        }
    }
    action(r, "mods.add", tr("Add a mod"), []() { content_mods_import(false); }, true, "ModImport");  // (1.0.2: MHGU's name; a .zip)
    action(r, "mods.folder", tr("Add a mod (folder)"), []() { content_mods_import(true); }, true, "ModFolderImport");
    if (!s.mods_note.empty()) note(r, "mods.note", s.mods_note, s.mods_note_bad ? theme().warn : NET_GREEN);
    {  // (mods of the game folder's mods that only 1.0's search finds: added off, GameFolderActivity.syncMods)
        std::string found;
        for (const std::string& name : g_mods_found_off)
            for (const auto& m : c.mods)
                if (m.name == name && !m.on) found += (found.empty() ? "" : ", ") + name;
        if (!found.empty())
            note(r, "mods.found",
                 fmt(say("Found in the mods folder beside the game and added off: %s. Turn on the ones you want, then save.",
                         "Encontrados en la carpeta mods al lado del juego y agregados desactivados: %s. Activa los que quieras y guarda."),
                     found.c_str()),
                 theme().warn);
    }
    header(r, "mods.head", tr("Mods, in load order"));
    if (c.mods.empty())
        note(r, "mods.none", say("No mods yet. Add one here, or put it in a mods folder beside the game.",
                                 "Todavía no hay mods. Agrega uno aquí, o ponlo en una carpeta mods al lado del juego."),
             touch_look().dim);
    if (!s.mod_scan) s.mod_scan = content_mods_scan(g_mod_info);  // (what they change in common, on a thread of its own)
    const std::vector<ModClash> clashes = mod_clashes();
    bool any_code = false;
    for (size_t i = 0; i < c.mods.size(); i++) {
        const std::string name = c.mods[i].name;
        const bool on = c.mods[i].on;
        const mh4n::mods::Mod* info = mod_info(name);
        const bool code = info && info->code, from_game = info && info->from_game_folder;
        const bool clash = std::any_of(clashes.begin(), clashes.end(), [&](const ModClash& k) { return k.first == name || k.second == name; });
        any_code = any_code || code;
        Row row;
        row.id = "mod." + name;
        row.title = name;
        row.value = std::string(code ? tr("Code") : "") + (code ? "  " : "") + tr(on ? "On" : "Off") + (clash ? " !" : "");
        if (!on) row.ink = touch_look().dim;
        else if (clash) row.ink = theme().warn;
        row.cold = true;
        if (info) row.help = mod_help(*info);
        auto set_on = [name](bool v) {
            for (auto& m : s.draft.mods)
                if (m.name == name) m.on = v;
        };
        row.adjust = [set_on](int d) { set_on(d > 0); };
        Row copy = row;
        row.menu = true;
        row.activate = [copy, name, on, set_on, from_game]() {
            auto move = [name](int d) {
                auto& list = s.draft.mods;
                for (size_t k = 0; k < list.size(); k++)
                    if (list[k].name == name) {
                        const long to = (long)k + d;
                        if (to >= 0 && to < (long)list.size()) std::swap(list[k], list[(size_t)to]);
                        return;
                    }
            };
            std::vector<std::string> names = {tr(on ? "Turn off" : "Turn on"), tr("Move up"), tr("Move down")};
            if (!from_game) names.push_back(tr("Remove"));
            choices(copy, names, -1, [set_on, move, on, name](int v) {
                if (v == 0) set_on(!on);
                else if (v == 1) move(-1);
                else if (v == 2) move(1);
                else {
                    s.remove_mod = name;
                    push(View::RemoveMod, "dialog.cancel");
                }
            });
        };
        r.push_back(std::move(row));
    }
    if (!c.mods.empty()) {  // (MHGU's: the conflict check, the suggested order, a rescan of the folder)
        if (!s.mod_scan->done.load()) {
            action(r, "mods.scanning", tr("Checking for conflicts..."), []() {}, true, "ModConflicts");
            r.back().arrow = false;
            r.back().ink = touch_look().glow;
        } else if (clashes.empty()) {
            action(r, "mods.clear", tr("No conflicts found"), []() {}, true, "ModConflicts");
            r.back().arrow = false;
            r.back().ink = NET_GREEN;
        } else {
            action(r, "mods.conflicts", fmt("Mods change the same files (%d): select to review", (int)clashes.size()),
                   []() { push(View::Conflicts, "mods.conflict.0"); }, true, "ModConflicts");
            r.back().value = "!";
            r.back().ink = theme().warn;
        }
        const std::vector<Draft::ModRow> order = suggested_mods();
        action(r, "mods.suggest", tr("Apply suggested order"), [order]() { s.draft.mods = order; }, order != c.mods, "ModSuggest");
    }
    action(r, "mods.rescan", tr("Rescan mods"), []() { reload_mods(); }, true, "ModRescan");
    if (any_code) {  // (the code library and the code mods that are on: does the game start with them?)
        const ContentCodeCheck check = content_code_check(draft_mods(c.mods));
        Row row;
        row.id = "mods.check", row.title = tr("Code library"), row.key = "CodeLibrary";
        if (check.busy) {
            row.value = tr("Checking...");
            row.ink = touch_look().glow;
        } else if (!check.known) {
            row.value = tr("Can't tell");
            row.ink = touch_look().dim;
        } else if (check.matches) {
            row.value = tr("Matches the mods");
            row.ink = NET_GREEN;
        } else {
            row.value = tr("Doesn't match");
            row.ink = theme().warn;
            row.help = say("With the code mods turned on here, the game's code is not the code your library was made from: the game won't start. Make the library again with the same code mods on (System › Game code › Build the game code, on this phone; or the guide's tool on a PC), or turn them back as they were.",
                           "Con los mods de código activados aquí, el código del juego no es el código con que se hizo tu librería: el juego no arranca. Haz la librería de nuevo con los mismos mods de código activados (Sistema › Código del juego › Crear el código del juego, en este teléfono; o la herramienta de la guía en una PC), o déjalos como estaban.");
        }
        r.push_back(std::move(row));
    }
    code_conflict_rows(r, c.mods);
    program_mod_rows(r, c.mods);
    if (s.saved.mods != s.boot.mods) {  // (mods are read when the game starts)
        action(r, "mods.restart", tr("Restart the game"), []() { push(View::Restart, "dialog.cancel"); });
        r.back().help = say("Mods load when the game starts: this closes the game and opens it again. Save your hunt in the game first.",
                            "Los mods se cargan al iniciar el juego: esto cierra el juego y lo abre de nuevo. Guarda antes tu cacería en el juego.");
    }
}

// S41: a cheat that writes into the game's code - which the port runs recompiled, so the cheat does nothing - made a code
// mod (content_cheat_to_code_mod): built into the code library with the mod on, its change is in the game's code. Its row
// is a menu: on / off, and Make a code mod; the outcome in the page's note.
void cheat_code_mod_menu(Row& row, size_t i, bool on) {
    Row copy = row;
    row.menu = true;
    row.activate = [copy, i, on]() {
        choices(copy, {tr(on ? "Turn off" : "Turn on"), tr("Make a code mod")}, -1, [i](int v) {
            if (v == 0) {
                if (i < s.draft.cheats.size()) s.draft.cheats[i] = !s.draft.cheats[i];
                return;
            }
            std::string name, why;
            if (content_cheat_to_code_mod(i, &name, &why)) {
                reload_mods();  // (the Mods page lists it at once, off)
                s.cheats_note_bad = false;
                s.cheats_note = fmt("Made the code mod \"%s\". Turn it on in Mods and make your code library again with it on: the game won't start with it on until then. Changes of the cheat outside the code still need the cheat on.",
                                    name.c_str());
                return;
            }
            s.cheats_note_bad = true;
            s.cheats_note = why == "conditional" ? tr("It changes the code only while something holds (a button, a value of the game): that can't be built into the code library.")
                          : why == "loaded_offset" ? tr("It finds where to write while the game runs (a pointer): that can't be built into the code library.")
                          : why == "register" ? tr("It writes values it reads while the game runs: that can't be built into the code library.")
                                              : fmt("It could not be made a code mod (%s).", why.c_str());
        });
    };
}

void cheat_rows(std::vector<Row>& r) {
    Draft& c = s.draft;
    {  // (an import that came back from ImportActivity.java: merged into the cheats file)
        const ContentImport in = content_cheats_import_result();
        if (in.done) {
            s.cheats_note_bad = !in.ok;
            s.cheats_note = in.ok ? fmt("Added %d cheat(s) from %s. They are off: turn on the ones you want, then save.", in.count, in.name.c_str())
                                  : import_problem(in);
        }
    }
    bool ready = false;
    const std::vector<ContentCheat> list = content_cheats(&ready);
    sync_cheats();
    action(r, "cheats.add", tr("Add cheats"), []() { content_cheats_import(); }, ready, "CheatImport");  // (1.0.2: MHGU's name)
    if (!s.cheats_note.empty()) note(r, "cheats.note", s.cheats_note, s.cheats_note_bad ? theme().warn : NET_GREEN);
    if (!ready) {
        note(r, "cheats.wait", say("The game is starting: its cheats show in a moment.", "El juego está arrancando: sus trucos aparecen en un momento."),
             touch_look().dim);
        return;
    }
    header(r, "cheats.head", tr("Cheats"));
    if (list.empty())
        note(r, "cheats.none", say("No cheats yet. Add cheats takes a .txt of cheats for MH4U USA 1.3.0 in the format of Azahar and Citra.",
                                   "Todavía no hay trucos. Agregar trucos toma un .txt de trucos para MH4U de EE. UU. 1.3.0 en el formato de Azahar y Citra."),
             touch_look().dim);
    for (size_t i = 0; i < list.size() && i < c.cheats.size(); i++) {
        const ContentCheat& cheat = list[i];
        Row row;
        bool on = c.cheats[i] != 0;
        row.id = "cheat." + std::to_string(i), row.title = cheat.name.empty() ? "-" : cheat.name;
        row.value = tr(on ? "On" : "Off");
        row.toggle = on ? 1 : 0;
        std::string notes = cheat.comments;
        while (!notes.empty() && (notes.back() == '\n' || notes.back() == ' ')) notes.pop_back();
        for (char& ch : notes)
            if (ch == '\n') ch = ' ';
        row.help = notes.empty() ? say("A cheat: it acts from the game's next frame once saved.", "Un truco: actúa desde el próximo cuadro del juego al guardar.")
                                 : notes;
        if (cheat.touches_code)
            row.help += " " + std::string(tr("It writes into the game's code, which this port runs translated ahead of time: as a cheat it does nothing. Select it and choose Make a code mod, then turn that mod on in Mods and make your code library again with it: the change is then in the game's code."));
        if (cheat.invalid_lines)
            row.help += " " + fmt(say("%d of its lines are not codes and do nothing.", "%d de sus líneas no son códigos y no hacen nada."),
                                  (int)cheat.invalid_lines);
        if (cheat.touches_code) row.ink = touch_look().dim;
        row.adjust = [i](int d) {
            if (i < s.draft.cheats.size()) s.draft.cheats[i] = d > 0;
        };
        row.activate = [i]() {
            if (i < s.draft.cheats.size()) s.draft.cheats[i] = !s.draft.cheats[i];
        };
        if (cheat.touches_code) cheat_code_mod_menu(row, i, on);
        r.push_back(std::move(row));
    }
    action(r, "cheats.remove", tr("Remove a cheat"),
           [list]() {
               std::vector<std::string> names;
               for (const ContentCheat& ch : list) names.push_back(ch.name.empty() ? "-" : ch.name);
               Row row;
               row.id = "cheats.remove", row.title = tr("Remove a cheat");
               choices(row, names, -1, [list](int i) {
                   if (i < 0 || i >= (int)list.size()) return;
                   s.remove_cheat = (size_t)i, s.remove_name = list[(size_t)i].name;
                   push(View::RemoveCheat, "dialog.cancel");
               });
           },
           !list.empty(), "CheatRemove");
}

// ---------------------------------------------------------------- the pages and their entries (MENU.md)
// Each option of the menu, as an entry on its page; the pages one level down, each with its row's place on the page
// above. Other options join with a MENU_ENTRY of their own (MENU.md: the places kept for 1.0's)
MENU_PAGE("controls/profiles", "Control profiles", "controls", 20);  // (1.0, the layout group's: S12, S09)
MENU_PAGE("controls/touch", "Touch controls", "controls", 80);
MENU_PAGE("controls/pad", "Controller", "controls", 90);
MENU_PAGE("controls/keys", "Keyboard and mouse", "controls", 100);
MENU_PAGE("graphics/picture", "Picture and sound", "graphics", 80);
MENU_PAGE("graphics/screen", "Screen and HUD", "graphics", 90);
MENU_PAGE("graphics/advanced", "Advanced", "graphics", 100);
MENU_PAGE("gameplay/comforts", "Comforts", "gameplay", 90, {}, [](Row& r) {
    r.help = say("Ideas from jM5557's MH4U QOL mods, done by the app: no mod and no new code library. Off unless you turn them on.",
                 "Ideas de los mods MH4U QOL de jM5557, hechas por la app: sin mods y sin hacer otra librería de código. Apagadas salvo que las actives.");
});
MENU_PAGE("mods/list", "Mods", "mods", 10, {}, [](Row& r) {
    int on = 0;
    for (const auto& m : s.draft.mods) on += m.on;
    r.value = s.draft.mods.empty() ? std::string(tr("No mods")) : fmt("%d of %d on", on, (int)s.draft.mods.size());
    r.help = say("Mods load from top to bottom: where two change the same file, the later one is used. They load when the game starts.",
                 "Los mods se cargan de arriba abajo: donde dos cambian el mismo archivo, se usa el de más abajo. Se cargan al iniciar el juego.");
});
MENU_PAGE("mods/cheats", "Cheats", "mods", 20, {}, [](Row& r) {
    int on = 0;
    for (char c : s.draft.cheats) on += c != 0;
    r.value = s.draft.cheats.empty() ? std::string(tr("No cheats")) : fmt("%d on", on);
    r.help = say("Cheats act from the game's next frame once saved. Online: only use them when everyone in the room agrees to it.",
                 "Los trucos actúan desde el próximo cuadro del juego al guardar. En línea: úsalos solo si todos en la sala están de acuerdo.");
});
MENU_PAGE("mods/dlc", "DLC and event quests", "mods", 30);  // (1.0: the qol group's, S37)
MENU_PAGE("mods/heavenfall", "Heavenfall", "mods", 40);     // (1.0: the mods group's, S45)
MENU_PAGE("mods/textures", "Texture packs", "mods", 50);    // (1.0: S26)
MENU_PAGE("system/menu", "This menu", "system", 20);
MENU_PAGE("system/game", "Your game", "system", 30);
MENU_PAGE("system/code", "Game code", "system", 40);  // (1.0: the build group's, S46)

// ---- Controls
void rows_controls_10(std::vector<Row>& r) {
    add_choice(r, "touch", "Show the touch controls", s.draft.controls, {tr("Auto"), tr("Always"), tr("Never")},
               [](int v) { s.draft.controls = v; }, "controls");
#ifndef __ANDROID__
    r.back().help = en_es("The phone's touch controls on a PC with a touch screen. Auto: they show once a finger touches the "
                          "screen, while no controller is connected. Always: they show for the mouse too (a click is a "
                          "finger). Never: only the game's own touch screen.",
                          "Los controles táctiles del teléfono en una PC con pantalla táctil. Automático: aparecen cuando un "
                          "dedo toca la pantalla, mientras no haya un mando conectado. Siempre: aparecen también para el ratón "
                          "(un clic es un dedo). Nunca: solo la pantalla táctil del juego.");
#endif
}
void reset_controls_10() { s.draft.controls = defaults().controls; }
MENU_ENTRY("controls", 10, rows_controls_10, reset_controls_10);
void rows_controls_30(std::vector<Row>& r) {
    action(r, "touch.edit", tr("Customize controls and HUD"),
           []() {
               if (s.draft == s.saved) {
                   s.closing = true;  // (the editor opens as the menu closes: settings_panel)
                   close_menu(g_open);
               } else {
                   s.closing = true;
                   push(View::CloseChanges, "dialog.save");
               }
           },
           true, "editor");
    r.back().help = tr("Moves and sizes the touch controls, and moves, sizes and hides the game's own HUD: the clock, the "
                       "health and stamina bars, the sharpness, the name plates, the area's map, the target camera and the "
                       "item bar. To see the map on the main screen, take the Map off your panels in Customize Panels (quest "
                       "menu).");
#ifndef __ANDROID__
    r.back().help = en_es("Moves, sizes and hides the game's HUD with the mouse: drag a part where you want it, the wheel or "
                          "- / + sizes it (each part of the HUD, the monsters' health bars, the quick items, the item bar's "
                          "area, the editor's panel), Hide takes a part off the screen and Reset puts it back at its place and "
                          "size. Esc cancels. The whole HUD's size is HUD size in Graphics & Audio > Screen and HUD. With a "
                          "touch screen in use, the touch controls are moved and sized there too. To see the map on the main "
                          "screen, take the Map off your panels in Customize Panels (quest menu).",
                          "Mueve, cambia de tamaño y oculta el HUD del juego con el ratón: arrastra una parte adonde la "
                          "quieras, la rueda o - / + cambia su tamaño (cada parte del HUD, las barras de vida de los "
                          "monstruos, los objetos rápidos, el área de la barra de objetos, el panel del editor), Ocultar saca "
                          "una parte de la pantalla y Restablecer la devuelve a su lugar y tamaño. Esc cancela. El tamaño de "
                          "todo el HUD es Tamaño del HUD en Gráficos y audio > Pantalla y HUD. Con una pantalla táctil en uso, "
                          "también se mueven y cambian de tamaño los controles táctiles. Para ver el mapa en la pantalla "
                          "principal, quita el Mapa de tus paneles en Personalizar paneles (menú de misión).");
#endif
}
MENU_ENTRY("controls", 30, rows_controls_30);
void rows_controls_40(std::vector<Row>& r) {
    action(r, "touch.buttons", tr("Buttons on the screen"), []() {
        s_combos_alone = false;
        push(View::Buttons, "tb.0");
    });
    r.back().help = tr("Choose which of the game's buttons show on the screen, and add buttons of your own that press several at once.");
    int shown = 0, all = 0;
    for (int i = 0; i < TB; i++)
        if (TOUCH_BUTTONS[i].group < 3) shown += s.draft.show[i] != 0, all++;
    r.back().value = std::to_string(shown) + " / " + std::to_string(all);
}
void reset_controls_40() {  // (and the combined buttons, which its view edits too)
    Draft f = defaults();
    s.draft.show = f.show;
    for (int k = 0; k < kCombos; k++) s.draft.combo[k] = f.combo[k], s.draft.combo_pad[k] = f.combo_pad[k];
}
MENU_ENTRY("controls", 40, rows_controls_40, reset_controls_40);
void rows_controls_50(std::vector<Row>& r) {
    Row row;
    char text[32];
    snprintf(text, sizeof text, "%.2fx", s.draft.camera);
    row.id = "touch.camera", row.title = tr("Camera speed"), row.key = "camera_speed", row.value = text;
    row.number = true;
    row.adjust = [](int d) { s.draft.camera = std::clamp(std::round((s.draft.camera + d * .05f) * 20) / 20.f, .2f, 5.f); };
    r.push_back(std::move(row));
    // (1.0.1: the drag's turn smoothed, off by default; the MHGU port's the same row)
    Row smooth;
    if (s.draft.camera_smooth > 0) snprintf(text, sizeof text, "%d%%", s.draft.camera_smooth);
    else snprintf(text, sizeof text, "%s", tr("Off"));
    smooth.id = "touch.smooth", smooth.title = tr("Camera smoothing"), smooth.key = "camera_smooth", smooth.value = text;
    smooth.number = true;
    smooth.adjust = [](int d) { s.draft.camera_smooth = std::clamp(s.draft.camera_smooth + d * 10, 0, 100); };
    r.push_back(std::move(smooth));
}
void reset_controls_50() {
    s.draft.camera = defaults().camera;
    s.draft.camera_smooth = defaults().camera_smooth;
}
MENU_ENTRY("controls", 50, rows_controls_50, reset_controls_50);
void rows_controls_60(std::vector<Row>& r) {
    add_bool(r, "lock_on", "Lock-on", &s.draft.lock_on, "LockOn");  // (the MHGU port's name)
    // (1.0.1.1: a bow's shot goes to the monster as it is let go; mh4u.ini bow_aim, on unless off: mh4n hooks.cpp
    // BowAimStep, taken at the game's next frame)
    add_switch(r, "bow_aim", en_es("Bow aims at the monster", "El arco apunta al monstruo"), &s.draft.bow_aim, "bow_aim");
    r.back().help = en_es(
        "With a bow, letting go of the shot sends the arrow to the monster: the one you locked on to or chose with the "
        "Target Cam, or else the nearest large monster in view. Your own aim still counts: drag on the shot (or turn "
        "the camera) away from the monster and the arrow goes where you aim; the aiming mode is left alone. Off: the "
        "arrow goes where the hunter faces, as the game does it.",
        "Con un arco, al soltar el disparo la flecha va al monstruo: al que fijaste o elegiste con la cámara de "
        "objetivo, o si no al monstruo grande más cercano que se ve. Tu puntería sigue contando: si arrastras sobre el "
        "disparo (o giras la cámara) lejos del monstruo, la flecha va adonde apuntas; el modo de apuntar queda como "
        "está. Apagado: la flecha va hacia donde mira el cazador, como lo hace el juego.");
}
void reset_controls_60() {
    s.draft.lock_on = defaults().lock_on;
    s.draft.bow_aim = defaults().bow_aim;
}
MENU_ENTRY("controls", 60, rows_controls_60, reset_controls_60);
void rows_controls_70(std::vector<Row>& r) {  // (the MHGU port's the same: names, range, default)
    add_bool(r, "gyro.aim", "Aim with the gyro", &s.draft.gyro_aim, "GyroAim");
#ifndef __ANDROID__
    r.back().help = en_es("Aiming a bow or a bowgun, turning a controller with a gyroscope turns the aim too, from where it was "
                          "as the aim began, with what the sticks or the mouse do. The gyroscope is read only while aiming.",
                          "Al apuntar con un arco o una ballesta, girar un mando con giroscopio también gira la mira, desde donde "
                          "estaba al empezar a apuntar, junto con lo que hagan los sticks o el ratón. El giroscopio solo se lee "
                          "mientras apuntas.");
#endif
    if (s.draft.gyro_aim) {
        Row row;
        char text[32];
        snprintf(text, sizeof text, "%.1fx", s.draft.gyro_sens / 10.f);
        row.id = "gyro.sens", row.title = tr("Gyro sensitivity"), row.key = "GyroSensitivity", row.value = text;
        row.number = true;
        row.adjust = [](int d) { s.draft.gyro_sens = std::clamp(s.draft.gyro_sens + d, 2, 30); };
#ifndef __ANDROID__
        row.help = en_es("How far the aim turns for a turn of the controller: 1.0x the same angle.",
                         "Cuánto gira la mira por cada giro del mando: 1.0x el mismo ángulo.");
#endif
        r.push_back(std::move(row));
    }
}
void reset_controls_70() {
    Draft f = defaults();
    s.draft.gyro_aim = f.gyro_aim;
    s.draft.gyro_sens = f.gyro_sens;
}
MENU_ENTRY("controls", 70, rows_controls_70, reset_controls_70);
// Controls > Touch controls
void rows_controls_touch_10(std::vector<Row>& r) {
    add_number(r, "touch.opacity", "Opacity", &s.draft.touch_opacity, 20, 100, 5, "%d%%", "opacity");
}
void reset_controls_touch_10() { s.draft.touch_opacity = defaults().touch_opacity; }
MENU_ENTRY("controls/touch", 10, rows_controls_touch_10, reset_controls_touch_10);
void rows_controls_touch_20(std::vector<Row>& r) {
    add_choice(r, "touch.style", "Attack buttons", s.draft.attack_style, {tr("Pill"), tr("Two buttons"), tr("Corner band")},
               [](int v) { s.draft.attack_style = v; }, "attack_style");
}
void reset_controls_touch_20() { s.draft.attack_style = defaults().attack_style; }
MENU_ENTRY("controls/touch", 20, rows_controls_touch_20, reset_controls_touch_20);
void rows_controls_touch_40(std::vector<Row>& r) {
#ifdef __ANDROID__  // (a PC has no vibration of its own)
    add_bool(r, "haptics", "Vibrate on touch", &s.draft.haptics, "Haptics");
#endif
}
void reset_controls_touch_40() { s.draft.haptics = defaults().haptics; }
MENU_ENTRY("controls/touch", 40, rows_controls_touch_40, reset_controls_touch_40);
// (1.0.1; 1.0.2: between Floating stick and Camera on the right half only, with the MHGU port's place and name)
void rows_controls_touch_61(std::vector<Row>& r) {
    add_bool(r, "touch.lphide", "Long press hides the controls", &s.draft.long_press_hide, "LongPressHide");
}
void reset_controls_touch_61() { s.draft.long_press_hide = defaults().long_press_hide; }
MENU_ENTRY("controls/touch", 61, rows_controls_touch_61, reset_controls_touch_61);
// Controls > Controller (a controller's, controls.h: read as soon as they are saved)
void rows_controls_pad_10(std::vector<Row>& r) {
    add_choice(r, "pad.face", "Face buttons", s.draft.face, {tr("Position"), tr("Label")}, [](int v) { s.draft.face = v; },
               "FaceButtons");
}
void reset_controls_pad_10() { s.draft.face = defaults().face; }
MENU_ENTRY("controls/pad", 10, rows_controls_pad_10, reset_controls_pad_10);
void rows_controls_pad_20(std::vector<Row>& r) {
    if (!controls_real_pad()) return;  // (playing with a controller: Buttons on the screen's combined buttons alone)
    action(r, "pad.combos", tr("Combined buttons"), []() {
        s_combos_alone = true;
        push(View::Buttons, "tc.0");
    });
    r.back().help = tr("Buttons of your own that press several of the game's at once, on a controller's button.");
}
void reset_controls_pad_20() {
    Draft f = defaults();
    for (int k = 0; k < kCombos; k++) s.draft.combo[k] = f.combo[k], s.draft.combo_pad[k] = f.combo_pad[k];
}
MENU_ENTRY("controls/pad", 20, rows_controls_pad_20, reset_controls_pad_20);
void rows_controls_pad_30(std::vector<Row>& r) {
    action(r, "pad.map", tr("Map a controller"), []() { push(View::PadList, "pad.0"); }, true, "PadMap");
}
MENU_ENTRY("controls/pad", 30, rows_controls_pad_30);
void rows_controls_pad_40(std::vector<Row>& r) {
    Draft& c = s.draft;
    add_choice(r, "pad.stick", "Right stick", c.right_stick, {tr("Camera"), tr("C-stick")}, [](int v) { s.draft.right_stick = v; },
               "RightStick");
    const bool turn = c.right_stick == 0;  // (the C-stick: the game's own turn and its own options)
    {
        Row row;
        char text[32];
        snprintf(text, sizeof text, "%.2fx", c.stick_speed / 100.f);
        row.id = "pad.speed", row.title = tr("Stick camera speed"), row.key = "StickSpeed", row.value = text;
        row.number = true;
        row.enabled = turn;
        row.adjust = [](int d) { s.draft.stick_speed = std::clamp(s.draft.stick_speed + d * 25, 25, 300); };
        r.push_back(std::move(row));
    }
    add_bool(r, "pad.invx", "Invert the camera across", &c.invert_x, "InvertX");
    r.back().enabled = turn;
    add_bool(r, "pad.invy", "Invert the camera up and down", &c.invert_y, "InvertY");
    r.back().enabled = turn;
}
void reset_controls_pad_40() {
    Draft f = defaults();
    s.draft.right_stick = f.right_stick, s.draft.stick_speed = f.stick_speed;
    s.draft.invert_x = f.invert_x, s.draft.invert_y = f.invert_y;
}
MENU_ENTRY("controls/pad", 40, rows_controls_pad_40, reset_controls_pad_40);
void rows_controls_pad_80(std::vector<Row>& r) {
    add_choice(r, "pad.r3", "Right stick click", s.draft.r3, {tr("Lock-on"), tr("Camera (L)"), tr("Target (ZL)"), tr("Kick (ZR)"), tr("Off")},
               [](int v) { s.draft.r3 = v; }, "R3");
    add_choice(r, "pad.town", "Town actions button", s.draft.town, {"SELECT", tr("Left stick click"), tr("Right stick click"), tr("Off")},
               [](int v) { s.draft.town = v; }, "TownMenu");
    add_number(r, "pad.dead", "Stick dead zone", &s.draft.deadzone, 0, 30, 2, "%d%%", "Deadzone");
}
void reset_controls_pad_80() {
    Draft f = defaults();
    s.draft.r3 = f.r3, s.draft.town = f.town, s.draft.deadzone = f.deadzone;
}
MENU_ENTRY("controls/pad", 80, rows_controls_pad_80, reset_controls_pad_80);
// Controls > Keyboard and mouse (the keys a keyboard has, as notes, until the keys can be chosen)
void rows_controls_keys_40(std::vector<Row>& r) {
    note(r, "keys.move", tr("W A S D move, Shift walks, I J K L turn the camera."));
    note(r, "keys.buttons", tr("U X, O or F A, Space B, R Y, Q L, E R, Tab ZL, T ZR, Enter START, Backspace SELECT, the arrows the D-pad."));
    note(r, "keys.menu", tr("START and SELECT together open these settings from any controller."));
}
MENU_ENTRY("controls/keys", 40, rows_controls_keys_40, {}, []() { return !controls_keyboard_mouse(); });

// ---- Graphics & Audio: the three that trade looks for speed together first, as in the MHGU port (Low-end mode, the
// frame rate, the 3D resolution), the switches players reach for, the sound and the readout
void rows_graphics_10(std::vector<Row>& r) {
    add_choice(r, "lowend", "Low-end mode", std::clamp(s.draft.low_end, 0, 3), {tr("Off"), tr("On"), tr("Toaster"), tr("Brick")},
               [](int v) { low_end(v); }, "LowEnd");
    // (the help of the mode it shows: what each one trades)
    r.back().help = help_of(s.draft.low_end == LOW_END_BRICK     ? "LowEnd.brick"
                            : s.draft.low_end == LOW_END_TOASTER ? "LowEnd.toaster"
                            : s.draft.low_end == LOW_END_ON      ? "LowEnd.on"
                                                                 : "LowEnd.off");
}
void reset_graphics_10() { s.draft.low_end = defaults().low_end; }
MENU_ENTRY("graphics", 10, rows_graphics_10, reset_graphics_10);
void rows_graphics_20(std::vector<Row>& r) {  // as MHGU's limit row: left / right one frame a second more or less, a tap the common ones
    // (MHGU's list, and 15, 20 and 25 below 30 since 1.0.1: 15 and 20 are a whole 4 and 3 of a 60 Hz screen's vsyncs a
    // frame, paced on them as 30 is; 25 is not, see its help)
#ifdef __ANDROID__
    static const int presets[] = {15, 20, 25, 30, 60, 90, 120, 144, 240};
#else
    static const int presets[] = {15, 20, 25, 30, 60, 90, 120, 144, 165, 240, kPcFpsUnlimited};  // (and No limit)
#endif
    const int now = fps_of(s.draft.fps);
    Row row;
    row.id = "fps", row.title = tr("Frame rate limit"), row.key = "fps", row.value = std::to_string(now);
    row.number = true;
    row.adjust = [now](int d) { s.draft.fps = std::to_string(std::clamp(now + d, kFpsMin, kFpsMax)); };
#ifndef __ANDROID__
    if (now >= kPcFpsUnlimited) row.value = en_es("No limit", "Sin límite");
    row.adjust = [now](int d) {  // (240 and one more: No limit; one less from No limit: 240)
        const int next = now >= kPcFpsUnlimited ? (d > 0 ? kPcFpsUnlimited : kFpsMax) : now + d;
        s.draft.fps = std::to_string(next > kFpsMax ? kPcFpsUnlimited : std::max(next, kFpsMin));
    };
#endif
    Row copy = row;
    row.menu = true;
    row.activate = [copy, now]() {
        std::vector<std::string> names;
        int pick = -1, below = 0;
        for (int i = 0; i < (int)std::size(presets); i++) {
#ifndef __ANDROID__
            if (presets[i] >= kPcFpsUnlimited) {
                names.push_back(en_es("No limit", "Sin límite"));
                if (now >= kPcFpsUnlimited) pick = i;
                continue;
            }
#endif
            names.push_back(std::to_string(presets[i]));
            if (presets[i] == now) pick = i;
            if (presets[i] <= now) below = i;
        }
        choices(copy, names, pick, [](int v) { s.draft.fps = std::to_string(presets[v]); });
        if (pick < 0) focus("choice." + std::to_string(below));  // (not one of them: at the one below)
    };
    r.push_back(std::move(row));
}
void reset_graphics_20() { s.draft.fps = defaults().fps; }
MENU_ENTRY("graphics", 20, rows_graphics_20, reset_graphics_20);
void rows_graphics_30(std::vector<Row>& r) {
    // as MHGU's: a percent of the phone's own resolution, with the whole multiple of the 3DS's it comes to
    Draft& c = s.draft;
    static const int kPercents[] = {100, 75, 50, 33, 0};
    int pick = -1;
    for (int i = 0; i < (int)std::size(kPercents); i++)
        if (kPercents[i] == c.scale) pick = i;
    std::vector<std::string> names;
    for (int p : kPercents)
        names.push_back(p == 0 ? tr("3DS (1x)") : std::to_string(p) + "% (" + std::to_string(render_scale_times(p)) + "x)");
    add_choice(r, "scale", "3D resolution", pick, names, [](int v) { s.draft.scale = kPercents[v]; }, "RenderScale");
    if (pick < 0) r.back().value = std::to_string(c.scale) + "% (" + std::to_string(render_scale_times(c.scale)) + "x)";
    r.back().cold = true;
    // the Adreno fix (first_run.h; its row in Advanced): while it raises the choice, the row says what is drawn
    const int used = render_scale_used(c.scale, c.adreno_fix);
    if (used != render_scale_times(c.scale)) {
        // ("3DS (1x) · 3x": the help says why)
        r.back().value += " \xC2\xB7 " + std::to_string(used) + "x";
        // (a GPU that can't draw the choice, first_run.h gpu_scale_limit: "100% (5x) · 4x")
        r.back().help = help_of("RenderScale") + " " + help_of(adreno_fix_applies() ? "RenderScale.adreno" : "RenderScale.limit");
    }
}
void reset_graphics_30() { s.draft.scale = defaults().scale; }
MENU_ENTRY("graphics", 30, rows_graphics_30, reset_graphics_30);
// (1.0.2, S6: Delta's thread and abelhk96's 4u-native #68) Upscale filter, beside the 3D resolution, as the MHGU port's:
// Smooth (the screens' own blend, as always) or FSR 1 (fsr1.h), with its sharpness; it shows at once
void rows_graphics_35(std::vector<Row>& r) {
    Draft& c = s.draft;
#ifdef __ANDROID__
    add_choice(r, "upscale", en_es("Upscale filter", "Filtro de escalado"), c.fsr1 ? 1 : 0, {en_es("Smooth", "Suave"), "FSR 1"}, [](int v) { s.draft.fsr1 = v == 1; }, "Upscale");
    r.back().value = c.fsr1 ? "FSR 1" : en_es("Smooth", "Suave");
    r.back().help = en_es("How the 3D picture is scaled up when it is smaller than the screen shows it (a 3D resolution below 100 %, or the console's own size): Smooth blends it, as always. FSR 1 (AMD FidelityFX Super Resolution 1.0) keeps the edges sharp and sharpens the picture: a little more work for the GPU. It does nothing at 100 %, and not with the PS1 filter or Brick. The game draws its HUD and text into the same picture, so they are scaled with it.",
                          "Cómo se agranda la imagen 3D cuando es más chica que como la muestra la pantalla (una resolución 3D por debajo de 100 %, o el tamaño de la consola): Suave la mezcla, como siempre. FSR 1 (AMD FidelityFX Super Resolution 1.0) mantiene los bordes nítidos y afila la imagen: un poco más de trabajo para la GPU. No hace nada al 100 %, ni con el filtro PS1 o Ladrillo. El juego dibuja su HUD y su texto en la misma imagen, así que se agrandan con ella.");
#else
    // (the PC: its picture filter of before - Smooth, Sharp pixels, Nearest, pc/settings_pc.inc - and FSR 1 in this one
    // row, [Graphics] Upscale = smooth, sharp, nearest or fsr1; PcDraft::upscale 3 is FSR 1)
    add_choice(r, "upscale", en_es("Upscale filter", "Filtro de escalado"), c.fsr1 ? 3 : std::clamp(c.pc.upscale, 0, 2),
               {en_es("Smooth", "Suave"), en_es("Sharp pixels", "Píxeles nítidos"), en_es("Nearest", "Vecino más cercano"), "FSR 1"},
               [](int v) { s.draft.fsr1 = v == 3, s.draft.pc.upscale = v; }, "Upscale");
    r.back().help = en_es("How the 3D picture is scaled to the window. Smooth: blended, as the game always looked. Sharp pixels: each of the picture's pixels a crisp square, all the same size (best with a low 3D resolution). Nearest: the nearest pixel, crisp but uneven at some sizes. FSR 1 (AMD FidelityFX Super Resolution 1.0): keeps the edges sharp and sharpens the picture where the window shows it larger than the 3D resolution draws it, a little more work for the GPU; shown at its size or smaller, it is drawn as with Smooth. The PS1 filter and Brick use their own. The game draws its HUD and text into the same picture, so they are scaled with it.",
                          "Cómo se escala la imagen 3D a la ventana. Suave: mezclada, como se vio siempre. Píxeles nítidos: cada píxel de la imagen un cuadrado nítido, todos del mismo tamaño (mejor con una resolución 3D baja). Vecino más cercano: el píxel más cercano, nítido pero desparejo en algunos tamaños. FSR 1 (AMD FidelityFX Super Resolution 1.0): mantiene los bordes nítidos y afila la imagen donde la ventana la muestra más grande de lo que la dibuja la resolución 3D, un poco más de trabajo para la GPU; mostrada a su tamaño o más chica, se dibuja como con Suave. El filtro PS1 y Ladrillo usan el suyo. El juego dibuja su HUD y su texto en la misma imagen, así que se escalan con ella.");
#endif
    if (!c.fsr1) return;
    add_number(r, "fsr1.sharp", en_es("FSR 1 sharpness", "Nitidez de FSR 1"), &c.fsr1_sharpness, 0, 100, 5, "%d%%", "Fsr1Sharpness");
    r.back().help = en_es("How much FSR 1 sharpens the picture after scaling it up: 0 % none, 100 % the most. 80 % as it comes.",
                          "Cuánto afila FSR 1 la imagen después de agrandarla: 0 % nada, 100 % lo más. 80 % de fábrica.");
}
void reset_graphics_35() {
    s.draft.fsr1 = defaults().fsr1, s.draft.fsr1_sharpness = defaults().fsr1_sharpness;
#ifndef __ANDROID__
    s.draft.pc.upscale = 0;  // (the PC's Smooth)
#endif
}
MENU_ENTRY("graphics", 35, rows_graphics_35, reset_graphics_35);
// (1.0.2, perf4u2) Fast scaling (more FPS; the ini's FastTopScreen), after the Upscale filter (MH4U's own row): the top screen scaled by the GPU's own filter (shared_screens.h shared_screens_set_fast_top), off as it comes
void rows_graphics_36(std::vector<Row>& r) {
#ifdef __ANDROID__  // (Android's GL path, shared_screens.cpp; the PC draws its screens its own way)
    add_bool(r, "fasttop", en_es("Fast scaling (more FPS)", "Escalado rápido (más FPS)"), &s.draft.fast_top, "FastTopScreen");
    r.back().help = en_es(
        "Scales the game's picture in a simpler way: more frames a second on weak phones; some colours may change very "
        "slightly.",
        "Agranda la imagen del juego de forma más simple: más cuadros por segundo en teléfonos débiles; algunos colores "
        "pueden variar apenas.");
#endif
}
void reset_graphics_36() { s.draft.fast_top = defaults().fast_top; }
MENU_ENTRY("graphics", 36, rows_graphics_36, reset_graphics_36);  // (Fast scaling: after the Upscale filter)
void rows_graphics_40(std::vector<Row>& r) { add_bool(r, "shadows", "Shadows", &s.draft.shadows, "Shadows"); }
void reset_graphics_40() { s.draft.shadows = defaults().shadows; }
MENU_ENTRY("graphics", 40, rows_graphics_40, reset_graphics_40);
void rows_graphics_60(std::vector<Row>& r) { add_number(r, "volume", "Volume", &s.draft.volume, 0, 100, 1, "%d%%", "Volume"); }
void reset_graphics_60() { s.draft.volume = defaults().volume; }
MENU_ENTRY("graphics", 60, rows_graphics_60, reset_graphics_60);
void rows_graphics_screen_85(std::vector<Row>& r) { add_bool(r, "stats", "FPS counter", &s.draft.stats, "FpsCounter"); }
void reset_graphics_screen_85() { s.draft.stats = defaults().stats; }
#ifdef __ANDROID__  // (the PC's: Show FPS and PC stats, pc/settings_pc.inc)
MENU_ENTRY("graphics/screen", 85, rows_graphics_screen_85, reset_graphics_screen_85);
#endif  // (the FPS counter: with the HUD, where the editor moves it)
// Graphics & Audio > Picture and sound
void rows_graphics_picture_10(std::vector<Row>& r) {
    add_switch(r, "aa", "Anti-aliasing", &s.draft.antialiasing, "antialiasing");
}
void reset_graphics_picture_10() { s.draft.antialiasing = defaults().antialiasing; }
MENU_ENTRY("graphics/picture", 10, rows_graphics_picture_10, reset_graphics_picture_10);
void rows_graphics_picture_60(std::vector<Row>& r) {
    Draft& c = s.draft;
    header(r, "filters", tr("Screen filters"));
    add_switch(r, "bloom", "Bloom", &c.bloom, "bloom");
    add_switch(r, "motion", "Motion blur", &c.motion_blur, "motion_blur");
    add_switch(r, "dof", "Depth of field", &c.depth_of_field, "depth_of_field");
    add_switch(r, "radial", "Radial blur", &c.radial_blur, "radial_blur");
    add_switch(r, "haze", "Heat haze", &c.haze, "haze");
    add_switch(r, "dither", "Dither pattern", &c.dither, "dither");
}
void reset_graphics_picture_60() {
    Draft f = defaults();
    Draft& c = s.draft;
    c.bloom = f.bloom, c.motion_blur = f.motion_blur, c.depth_of_field = f.depth_of_field;
    c.radial_blur = f.radial_blur, c.haze = f.haze, c.dither = f.dither;
}
MENU_ENTRY("graphics/picture", 60, rows_graphics_picture_60, reset_graphics_picture_60);
void rows_graphics_picture_80(std::vector<Row>& r) {
    header(r, "audio", tr("Sound"));
    add_bool(r, "reverb", "Reverb", &s.draft.reverb, "Reverb");
#ifdef __ANDROID__  // (a phone's screen recorder and its low-latency sound path: nothing of the kind on a PC)
    add_bool(r, "recaudio", "Sound in screen recordings", &s.draft.rec_audio, "ScreenRecording");
#endif
}
void reset_graphics_picture_80() { s.draft.reverb = defaults().reverb, s.draft.rec_audio = defaults().rec_audio; }
MENU_ENTRY("graphics/picture", 80, rows_graphics_picture_80, reset_graphics_picture_80);
// Graphics & Audio > Colour (1.0.1, the owner's item 9: a filter of our own, colour_filter.h, as MHGU's): the game's two
// screens as they reach the window (the 3D, its HUD, its menus, the touch screen), not the touch controls or this menu.
// Every control at 0 is the picture as the game draws it, byte for byte. The screens follow the rows while the menu is
// open (settings_panel: shared_screens_set_colour); Save keeps them, closing without saving puts the saved ones back.
// (The HDR output: MHGU's, and MH4U for PC's, pc/settings_pc.inc; on Android this app's window is SDL's OpenGL ES one,
// which has no HDR picture.)
MENU_PAGE("graphics/colour", "Colour", "graphics", 85);
void colour_number(std::vector<Row>& r, const char* id, const char* en, const char* es, int* p, const char* key, int high,
                   const char* unit, const char* help_en, const char* help_es) {
    add_number(r, id, en, p, -high, high, 5, "%d", key);
    char text[32];
    std::snprintf(text, sizeof text, *p > 0 ? "+%d%s" : "%d%s", *p, unit);
    r.back().title = en_es(en, es);
    r.back().value = text;
    r.back().help = en_es(help_en, help_es);
}
// The colour presets (1.0.2, S13: abelhk96's 4u-native #68; the MHGU port's the same rows and texts): three slots of
// [Colour] Preset1..3 (app_settings.h app_colour_preset), each a row under Reset colour that opens its own page
// (View::ColourPreset, as a control profile's): Use puts the slot on the Colour page's controls and goes back to it (the
// picture follows at once and Save keeps it, as moving them); Save the current colour here and Clear write the slot at
// once, as the control profiles, and ask for a second tap when they'd replace or empty something
int s_colour_preset = 1;    // the slot View::ColourPreset shows
std::string s_colour_arm;   // the row tapped once that a second tap confirms ("" none)
std::string s_colour_said;  // what the last action did (a note under the rows)
// A slot's contents: Empty, All at 0, or its controls that aren't 0 as the page writes them ("Hue +10°, Temperature -15")
std::string colour_preset_what(int n) {
    ColourAdjust a;
    if (!app_colour_preset(n, &a)) return en_es("Empty", "Vacío");
    if (colour_neutral(&a)) return en_es("All at 0", "Todo en 0");
    const struct {
        int v;
        const char *en, *es, *unit;
    } parts[] = {{a.hue, "Hue", "Tono", "\xC2\xB0"},
                 {a.temperature, "Temperature", "Temperatura", ""},
                 {a.tint, "Tint", "Tinte", ""},
                 {a.brightness, "Brightness", "Brillo", ""},
                 {a.contrast, "Contrast", "Contraste", ""},
                 {a.saturation, "Saturation", "Saturación", ""},
                 {a.gamma, "Gamma", "Gamma", ""}};
    std::string t;
    for (const auto& p : parts) {
        if (!p.v) continue;
        char one[64];
        std::snprintf(one, sizeof one, p.v > 0 ? "%s%s +%d%s" : "%s%s %d%s", t.empty() ? "" : ", ", en_es(p.en, p.es), p.v,
                      p.unit);
        t += one;
    }
    return t;
}
// The Colour page's rows of the three slots, under Reset colour
void colour_preset_links(std::vector<Row>& r) {
    for (int n = 1; n <= kColourPresets; n++) {
        action(r, "colour.p" + std::to_string(n), fmt(en_es("Preset %d", "Preset %d"), n), [n]() {
            s_colour_preset = n, s_colour_arm.clear(), s_colour_said.clear();
            push(View::ColourPreset, "cp.use");
        });
        r.back().value = colour_preset_what(n);
        r.back().help = en_es("Keeps the seven colour controls to come back to them with Use. Saved at once in the settings file; Reset colour doesn't empty it.",
                              "Guarda los siete controles de color para volver a ellos con Usar. Se guarda al momento en el archivo de ajustes; Restablecer el color no lo vacía.");
    }
}
// A slot's page (View::ColourPreset)
void colour_preset_rows(std::vector<Row>& r) {
    const int n = s_colour_preset;
    const ColourAdjust now = colour_of(s.draft);
    ColourAdjust kept;
    const bool used = app_colour_preset(n, &kept), same = used && !std::memcmp(&kept, &now, sizeof now);
    note(r, "cp.what", colour_preset_what(n));
    action(r, "cp.use", tr("Use"), [n]() {
        ColourAdjust a;
        s_colour_arm.clear(), s_colour_said.clear();
        if (!app_colour_preset(n, &a)) return;
        Draft& d = s.draft;
        d.hue = a.hue, d.temperature = a.temperature, d.tint = a.tint, d.brightness = a.brightness;
        d.contrast = a.contrast, d.saturation = a.saturation, d.gamma = a.gamma;
        pop();  // (back to the Colour page, on this slot's row: its controls show the values, the picture follows)
    }, used && !same);
    r.back().arrow = false;
    r.back().help = en_es("Puts this preset on the Colour page's controls: the picture changes at once, and Save keeps it.",
                          "Pone este preset en los controles de la página Color: la imagen cambia en el momento, y Guardar lo conserva.");
    // (an action that replaces or empties the slot: a first tap arms it, its title says to tap again)
    auto armed = [&r](const char* id, const std::string& title, const char* again, bool ask, std::function<bool()> fn,
                      const char* done) {
        const bool on = s_colour_arm == id;
        action(r, id, on ? std::string(tr(again)) : title, [id = std::string(id), ask, fn, done]() {
            if (ask && s_colour_arm != id) {
                s_colour_arm = id;
                return;
            }
            s_colour_arm.clear();
            s_colour_said = fn() ? tr(done) : en_es("It couldn't be saved.", "No se pudo guardar.");
        });
        r.back().arrow = false;
    };
    armed("cp.save", en_es("Save the current colour here", "Guardar el color actual aquí"), "Tap again to replace it", used && !same,
          [n]() {
              const ColourAdjust a = colour_of(s.draft);
              return app_colour_preset_store(n, &a);
          }, "Saved");
    r.back().enabled = !same;
    r.back().help = en_es("Keeps the seven colour controls as the Colour page has them now in this preset, at once: the menu's Save is not needed for it.",
                          "Guarda en este preset los siete controles de color como están ahora en la página Color, en el momento: no hace falta el Guardar del menú.");
    armed("cp.clear", tr("Clear"), "Tap again to empty it", true, [n]() { return app_colour_preset_store(n, nullptr); }, "Emptied");
    r.back().enabled = used;
    r.back().help = en_es("Empties this preset. The colour you play with stays as it is.",
                          "Vacía este preset. El color con el que juegas queda como está.");
    if (!s_colour_said.empty()) note(r, "cp.said", s_colour_said);
    action(r, "cp.back", tr("Back"), []() { s_colour_arm.clear(), pop(); });
    r.back().arrow = false;
}
void rows_graphics_colour_10(std::vector<Row>& r) {
    Draft& d = s.draft;
    note(r, "colour.note", en_es("Changes the game's two screens, its HUD and its menus; the touch controls and this menu stay as they are. Each change shows at once; Save keeps it.",
                                 "Cambia las dos pantallas del juego, su HUD y sus menús; los controles táctiles y este menú quedan como están. Cada cambio se ve en el momento; Guardar lo conserva."));
    colour_number(r, "colour.hue", "Hue", "Tono", &d.hue, "Hue", COLOUR_HUE_MAX, "\xC2\xB0",
                  "Turns every colour of the game's picture round the colour wheel, in degrees. 0: the game's own colours.",
                  "Gira todos los colores de la imagen del juego por la rueda de colores, en grados. 0: los colores del propio juego.");
    colour_number(r, "colour.temp", "Temperature", "Temperatura", &d.temperature, "Temperature", COLOUR_MAX, "",
                  "Warmer (+: more orange) or cooler (-: more blue) whites and colours.",
                  "Blancos y colores más cálidos (+: más anaranjados) o más fríos (-: más azulados).");
    colour_number(r, "colour.tint", "Tint", "Tinte", &d.tint, "Tint", COLOUR_MAX, "",
                  "Magenta (+) or green (-), to correct a screen that leans to one of them.",
                  "Magenta (+) o verde (-), para corregir una pantalla que tira hacia uno de los dos.");
    colour_number(r, "colour.bright", "Brightness", "Brillo", &d.brightness, "Brightness", COLOUR_MAX, "",
                  "Raises or lowers the whole picture, its dark parts too.", "Sube o baja toda la imagen, también sus partes oscuras.");
    colour_number(r, "colour.contrast", "Contrast", "Contraste", &d.contrast, "Contrast", COLOUR_MAX, "",
                  "More (+) or less (-) difference between the dark and the bright parts.",
                  "Más (+) o menos (-) diferencia entre las partes oscuras y las claras.");
    colour_number(r, "colour.sat", "Saturation", "Saturación", &d.saturation, "Saturation", COLOUR_MAX, "",
                  "More vivid (+) or paler (-) colours; -100 is black and white.",
                  "Colores más vivos (+) o más apagados (-); -100 es blanco y negro.");
    colour_number(r, "colour.gamma", "Gamma", "Gamma", &d.gamma, "Gamma", COLOUR_MAX, "",
                  "Brightens (+) or darkens (-) the middle tones and leaves black and white where they are: to see into a dark cave without washing the picture out.",
                  "Aclara (+) u oscurece (-) los tonos medios y deja el negro y el blanco donde están: para ver en una cueva oscura sin lavar la imagen.");
    const ColourAdjust now = colour_of(d);
    action(r, "colour.reset", en_es("Reset colour", "Restablecer el color"), []() {
        Draft& d = s.draft;
        d.hue = d.temperature = d.tint = d.brightness = d.contrast = d.saturation = d.gamma = 0;
    }, !colour_neutral(&now));
    r.back().help = en_es("Puts every colour control back to 0: the picture exactly as the game draws it.",
                          "Vuelve todos los controles de color a 0: la imagen tal como la dibuja el juego.");
    colour_preset_links(r);
}
void reset_graphics_colour_10() {
    Draft& d = s.draft;
    d.hue = d.temperature = d.tint = d.brightness = d.contrast = d.saturation = d.gamma = 0;
}
MENU_ENTRY("graphics/colour", 10, rows_graphics_colour_10, reset_graphics_colour_10);
#include "settings_ps1.inc"  // (1.0.1.1: Graphics & Audio > PS1 filter)
// Graphics & Audio > Screen and HUD (the Display tab of 0.1.9, the widescreen and the HUD at its edges)
void rows_graphics_screen_40(std::vector<Row>& r) {
    Draft& c = s.draft;
    int wide = index_of(WIDE_KEYS, c.widescreen);
    add_choice(r, "wide", "Widescreen", wide, {tr("Match the screen"), "16:9", tr("Off")},
               [](int v) { s.draft.widescreen = v == 0 ? "match" : v == 1 ? "16:9" : "off"; }, "widescreen", c.widescreen);
    r.back().cold = game_option_needs_restart("widescreen");
}
void reset_graphics_screen_40() { s.draft.widescreen = defaults().widescreen; }
MENU_ENTRY("graphics/screen", 40, rows_graphics_screen_40, reset_graphics_screen_40);
void rows_graphics_screen_50(std::vector<Row>& r) {
    Draft& c = s.draft;
    add_switch(r, "hud", "HUD at the screen's edges", &c.hud_edges, "hud_edges");
    if (c.widescreen.empty() || c.widescreen == "0" || !strcasecmp(c.widescreen.c_str(), "off")) {  // (no wide picture)
        r.back().enabled = false;
        r.back().value = tr("Off (no widescreen)");
        r.back().toggle = -1;
    }
}
void reset_graphics_screen_50() { s.draft.hud_edges = defaults().hud_edges; }
// (1.0.2: Field of view, the HUD's place, its size, the FPS counter, Text outline and Hunting aids in the MHGU port's
// order; its HUD anchor is this row's place)
MENU_ENTRY("graphics/screen", 70, rows_graphics_screen_50, reset_graphics_screen_50);
// (1.0.2) the HUD's size: every part of the game's HUD toward its corner, the quests' talk and yes / no windows and the
// rest of the game's HUD with it, at once (mh4u.ini hud_size, hooks.cpp); 60 to 100 %, as Heavenfall's smaller HUD
int hud_size_of(const std::string& v) {
    float f = (float)atof(v.c_str());
    if (f > 0 && f <= 2.0f) f *= 100.0f;
    return f > 0 ? std::clamp((int)lroundf(f), 60, 100) : 100;
}
void rows_graphics_screen_55(std::vector<Row>& r) {
    Row row;
    const int now = hud_size_of(s.draft.hud_size);
    char text[16];
    snprintf(text, sizeof text, "%d%%", now);
    row.id = "hud_size", row.title = tr("HUD size"), row.key = "hud_size", row.value = text;
    row.number = true;
    row.adjust = [now](int d) { s.draft.hud_size = std::to_string(std::clamp(now + d * 5, 60, 100)); };
    r.push_back(std::move(row));
}
void reset_graphics_screen_55() { s.draft.hud_size = defaults().hud_size; }
MENU_ENTRY("graphics/screen", 75, rows_graphics_screen_55, reset_graphics_screen_55);
void rows_graphics_screen_60(std::vector<Row>& r) {  // (MHGU's: 0.5 to 2, in steps of 0.05)
    Row row;
    const float fov = fov_of(s.draft.fov);
    char text[16];
    snprintf(text, sizeof text, "%.2f", fov);
    row.id = "fov", row.title = tr("Field of view"), row.key = "fov", row.value = text;
    row.number = true;
    row.adjust = [fov](int d) {
        char next[16];
        snprintf(next, sizeof next, "%.2f", std::clamp(fov + d * .05f, .5f, 2.f));
        s.draft.fov = next;
    };
    r.push_back(std::move(row));
}
void reset_graphics_screen_60() { s.draft.fov = defaults().fov; }
MENU_ENTRY("graphics/screen", 60, rows_graphics_screen_60, reset_graphics_screen_60);
// Text outline (1.0.2, GU#243, the MHGU port's the same row: red words hard to read): the game's letters drawn over a dark
// outline (mh4n gpu/native SetTextOutline). Off as the app comes; with Save, live
void rows_graphics_screen_65(std::vector<Row>& r) {
    add_bool(r, "textoutline", en_es("Text outline", "Contorno del texto"), &s.draft.text_outline, "TextOutline");
    r.back().help = en_es("A dark outline round the game's letters, so its words, the red ones most, read well over a bright "
                          "picture. Off: the letters as the game draws them.",
                          "Un contorno oscuro alrededor de las letras del juego, para que sus palabras, sobre todo las "
                          "rojas, se lean bien sobre una imagen clara. Desactivado: las letras como las dibuja el juego.");
}
void reset_graphics_screen_65() { s.draft.text_outline = defaults().text_outline; }
MENU_ENTRY("graphics/screen", 92, rows_graphics_screen_65, reset_graphics_screen_65);
void rows_graphics_screen_100(std::vector<Row>& r) {  // (the 3DS's lower screen)
    Draft& c = s.draft;
    header(r, "screen.head", tr("Touch screen"));
    add_bool(r, "screen", "Show the touch screen", &c.screen_shown, "TouchScreen");
#ifdef __ANDROID__
    // (1.0.2) On the second screen: only while Android reports a second display; chosen without one, the layout it
    // goes back to shows (picking one keeps that one)
    const bool second = second_screen_display_present();
    std::vector<std::string> layouts = {tr("In a corner"), tr("Stacked"), tr("Side by side")};
    if (second) layouts.push_back(tr("On the second screen"));
    const int layout = c.layout == 3 && !second ? c.layout_before : c.layout;
    add_choice(r, "layout", "Layout", layout, layouts, [](int v) {
        if (v == 3 && s.draft.layout != 3) s.draft.layout_before = s.draft.layout;
        s.draft.layout = v;
    }, "Layout");
    bool corner = layout == 0;
#else
    // (the PC: In its own window in place of the device's second display; that window goes on any monitor)
    add_choice(r, "layout", "Layout", c.layout,
               {tr("In a corner"), tr("Stacked"), tr("Side by side"), en_es("In its own window", "En su propia ventana")},
               [](int v) { s.draft.layout = v; }, "Layout");
    bool corner = c.layout == 0;
#endif
    add_choice(r, "corner", "Corner", c.corner, {tr("Bottom right"), tr("Bottom left"), tr("Top right"), tr("Top left")},
               [](int v) { s.draft.corner = v; }, "Corner");
    r.back().enabled = corner;
    add_number(r, "size", "Size", &c.size, 20, 60, 2, "%d%%", "Size");
    r.back().enabled = corner;
    add_number(r, "opacity", "Opacity", &c.opacity, 30, 100, 5, "%d%%", "Opacity");
    r.back().enabled = corner;
}
void reset_graphics_screen_100() {
    Draft f = defaults();
    Draft& c = s.draft;
    c.screen_shown = f.screen_shown, c.layout = f.layout, c.corner = f.corner, c.size = f.size, c.opacity = f.opacity;
    c.layout_before = f.layout_before;
}
MENU_ENTRY("graphics/screen", 100, rows_graphics_screen_100, reset_graphics_screen_100);
#include "settings_hunt_aids.inc"  // (1.0.1: Monsters always on the map, Damage numbers)
// Graphics & Audio > Advanced
void rows_graphics_advanced_10(std::vector<Row>& r) {
    add_switch(r, "steady", "Steady frame rate", &s.draft.steady, "steady");
}
void reset_graphics_advanced_10() { s.draft.steady = defaults().steady; }
MENU_ENTRY("graphics/advanced", 10, rows_graphics_advanced_10, reset_graphics_advanced_10);
void rows_graphics_advanced_20(std::vector<Row>& r) {  // (first_run.h: only where it applies)
    add_choice(r, "adrenofix", "Adreno fix", s.draft.adreno_fix ? 0 : 1, {tr("Auto"), tr("Off")},
               [](int v) { s.draft.adreno_fix = v == 0; }, "AdrenoFix");
    r.back().cold = true;
}
void reset_graphics_advanced_20() { s.draft.adreno_fix = defaults().adreno_fix; }
MENU_ENTRY("graphics/advanced", 20, rows_graphics_advanced_20, reset_graphics_advanced_20, adreno_fix_applies);
void reset_graphics_advanced_60() { s.draft.driver = defaults().driver; }
// (1.0.2: the GPU driver's section last, as the MHGU port's GPU driver row: no other row under its title)
MENU_ENTRY("graphics/advanced", 80, driver_rows, reset_graphics_advanced_60);
void rows_graphics_advanced_70(std::vector<Row>& r) {  // [Graphics] FrameGen (frame_gen.h)
    add_bool(r, "framegen", "Frame generation", &s.draft.frame_gen, "FrameGen");
}
void reset_graphics_advanced_70() { s.draft.frame_gen = defaults().frame_gen; }
// (1.0.2: after Clear shader cache, as the MHGU port's)
MENU_ENTRY("graphics/advanced", 55, rows_graphics_advanced_70, reset_graphics_advanced_70);
#ifndef __ANDROID__
#include "settings_pc.inc"  // (MH4U for PC: the window, VSync, the HUD anchor, the PC readout, HDR, the GPU)
#endif

// ---- Gameplay
void rows_gameplay_10(std::vector<Row>& r) {
    Draft& c = s.draft;
    add_button(r, "kick", "Kick button", &c.kick, "kick", BUTTON_KEYS, (int)std::size(BUTTON_KEYS));
    add_button(r, "target", "Target camera button", &c.target, "target", BUTTON_KEYS, (int)std::size(BUTTON_KEYS));
    add_button(r, "skip", "Skip button", &c.skip, "skip", BUTTON_KEYS, (int)std::size(BUTTON_KEYS));
}
void reset_gameplay_10() {
    Draft f = defaults();
    s.draft.kick = f.kick, s.draft.target = f.target, s.draft.skip = f.skip;
}
MENU_ENTRY("gameplay", 10, rows_gameplay_10, reset_gameplay_10);
// 1.0.1, the owner's idea: one gather at a gathering point, or one carve, gives everything it had left at once (mh4n
// port/src/gather.cpp: gather_all in mh4u.ini, applied at the game's next frame; MHGU for Android has the same option, name
// and texts). Off by default (the owner: in the app, off): one item a gather, as the game does.
void gather_rows(std::vector<Row>& r) {
    header(r, "gather.head", en_es("Gathering", "Recolección"));
    add_switch(r, "gather.all", en_es("Gather everything in one press", "Recolectar todo de un toque"), &s.draft.gather_all,
               "gather_all");
    r.back().help = en_es("One gather at a gathering point (herbs, mushrooms, honey, bugs, bones, ore...) or one carve gives "
                          "everything it had left at once, with the game's own rules: your gathering and carving skills, and "
                          "pickaxes and bug nets that can break. It stops if the pouch fills up: the rest stays. Off: one "
                          "item each time, as the game does it.",
                          "Una recolección en un punto (hierbas, hongos, miel, insectos, huesos, minerales...) o un tallado "
                          "da de una vez todo lo que le quedaba, con las reglas del juego: tus habilidades de recolección y "
                          "de tallado, y las piquetas y redes que se pueden romper. Se detiene si la bolsa se llena: lo "
                          "demás queda. Apagado: un objeto cada vez, como lo hace el juego.");
}
void reset_gather() { s.draft.gather_all = defaults().gather_all; }
// (before After a quest, order 60, so that its rows stay under its header)
MENU_ENTRY("gameplay", 50, gather_rows, reset_gather);
void rows_gameplay_60(std::vector<Row>& r) {
    Draft& c = s.draft;
    header(r, "end.head", tr("After a quest"));
    add_button(r, "end", "End the wait (held)", &c.skip_end, "skip_end", END_KEYS, (int)std::size(END_KEYS));
    Row row;
    float hold = std::clamp((float)std::atof(c.skip_end_hold.c_str()), .5f, 5.f);
    row.id = "end.hold", row.title = tr("Hold for"), row.key = "skip_end_hold", row.value = fmt("%.1f s", hold);
    row.number = true;
    row.enabled = strcasecmp(c.skip_end.c_str(), "off") != 0;
    row.cold = game_option_needs_restart("skip_end_hold");
    row.adjust = [hold](int d) {
        char text[16];
        snprintf(text, sizeof text, "%.1f", std::clamp(hold + d * .5f, .5f, 5.f));
        s.draft.skip_end_hold = text;
    };
    r.push_back(std::move(row));
}
void reset_gameplay_60() {
    Draft f = defaults();
    s.draft.skip_end = f.skip_end, s.draft.skip_end_hold = f.skip_end_hold;
}
MENU_ENTRY("gameplay", 60, rows_gameplay_60, reset_gameplay_60);
// Gameplay > Comforts: jM5557's MH4U QOL mods, done by the port's hooks (hooks.cpp Qol*): no code mod and no new code
// library (in the Cheats tab up to 0.1.9.3)
void rows_gameplay_comforts_10(std::vector<Row>& r) {
    Draft& c = s.draft;
    add_switch(r, "qol.gather", "Hold A to gather", &c.qol_gather, "qol_hold_gather");
    add_switch(r, "qol.flex", "Move to skip the flex", &c.qol_flex, "qol_flex_cancel");
    add_switch(r, "qol.sharpen", "B cancels sharpening", &c.qol_sharpen, "qol_sharpen_cancel");
    add_switch(r, "qol.rewards", "No time limit on rewards", &c.qol_rewards, "qol_rewards_no_limit");
}

// S43, Alfonso's idea: "Send to Item Box" on the rewards screen sends every reward at once (mh4n hooks.cpp
// QolRewardsSendAll; MHGU for Android has the same option, name and texts), the Palico's lists too since 1.0.1 (Dark_Light,
// #suggestions "Hacia el Baúl"). Off: one reward an A, as the game does.
void rewards_send_all_row(std::vector<Row>& r) {
    add_switch(r, "end.rewards", "Send all to the box", &s.draft.rewards_send_all, "rewards_send_all");
    r.back().help = say("At the end of a quest, the game's Send to Item Box sends every reward at once, as in the newer "
                        "games, and the Palico's items too. What doesn't fit in the box stays in the list. Off: one at a "
                        "time, as the game does it. Alfonso's idea.",
                        "Al terminar una misión, la opción del juego de enviar a la caja de objetos manda todas las "
                        "recompensas de una vez, como en los juegos más nuevos, y también los objetos del Felyne. Lo que no "
                        "entra en la caja queda en la lista. Apagado: de a una, como lo hace el juego. Idea de Alfonso.");
}

// S37: the player's own DLC and event quests, from a folder they choose (android/.../ExtraImportActivity.java; MHGU for
// Android has the same row and screen)
void extra_import_row(std::vector<Row>& r) {
    action(r, "save.extra", say("Import DLC and event quests", "Importar DLC y misiones de evento"),
           []() { app_call_activity("showExtraImport"); });
    r.back().help = say("Brings the event quests and the DLC you already have, from an emulator (the DLC you installed in "
                        "Azahar), a backup of your console or a decrypted .cia of your own. Only the quest files and the "
                        "DLC are copied: your hunters, guild cards and Palico stay. The app includes none and decrypts "
                        "nothing: you bring your own, at your own risk.",
                        "Trae las misiones de evento y el DLC que ya tienes, de un emulador (el DLC que instalaste en "
                        "Azahar), de una copia de tu consola o de un .cia descifrado tuyo. Solo se copian los archivos de "
                        "misiones y el DLC: tus cazadores, tarjetas de gremio y Felyne quedan. La app no incluye nada ni "
                        "descifra nada: los traes tú, bajo tu responsabilidad.");
}

// S56: a shortcut on the phone's home screen with an image the player picks (android/.../ShortcutActivity.java; the
// same row and screen in MHGU for Android)
void shortcut_row(std::vector<Row>& r) {
#ifndef __ANDROID__
    (void)r;  // (a phone's home screen: not on a PC)
    return;
#endif
    action(r, "app.shortcut", say("Home screen shortcut", "Acceso directo en el inicio"),
           []() { app_call_activity("showShortcut"); });
    r.back().help = say("Puts a shortcut to the game on your home screen, with a picture you choose from your phone and "
                        "the name you want. The app's own icon stays as it is.",
                        "Pone un acceso directo al juego en la pantalla de inicio, con una imagen que elijas de tu teléfono "
                        "y el nombre que quieras. El ícono propio de la app queda como está.");
}
// 1.0.1, the owner's call: the DLC the player imported is used in every save, not only in one whose DLC flag a console
// set (mh4n hooks.cpp DlcFlagsStep sets the flag from the title screen on; the save is copied to save_backups first). On
// by default.
void dlc_flags_row(std::vector<Row>& r) {
    add_switch(r, "dlc.flags", "DLC in every save", &s.draft.dlc_flags, "dlc_flags");
    r.back().help = say("The DLC you imported is used in every save. The game uses it only in a save that got its DLC on "
                        "a console, through an online step that no longer exists, so when the game starts with a save "
                        "without that mark, the app sets it, and the game keeps it when it saves. "
                        "Before that, a copy of your save goes to save_backups. Nothing happens without imported DLC. "
                        "Off: only saves that already had DLC use it (a mark already saved stays).",
                        "El DLC que importaste se usa en todas las partidas. El juego solo lo usa en una partida que "
                        "recibió su DLC en una consola, con un paso en línea que ya no existe; así que cuando el juego "
                        "arranca con una partida sin esa marca, la app se la pone, y el juego la guarda al guardar. "
                        "Antes, una copia de tu partida queda en save_backups. Sin DLC importado no pasa nada. Apagado: "
                        "solo lo usan las partidas que ya tenían DLC (una marca ya guardada queda).");
}
void reset_dlc_flags() { s.draft.dlc_flags = defaults().dlc_flags; }
void reset_rewards_send_all() { s.draft.rewards_send_all = defaults().rewards_send_all; }
// The qol group's places (MENU.md 3.3, 3.5, 3.6): Send all under After a quest (its header: order 60), the import on DLC
// and event quests, the shortcut on System
MENU_ENTRY("gameplay", 70, rewards_send_all_row, reset_rewards_send_all);
MENU_ENTRY("mods/dlc", 10, extra_import_row);
MENU_ENTRY("mods/dlc", 20, dlc_flags_row, reset_dlc_flags);
#ifdef __ANDROID__  // (a shortcut on the phone's home screen: none on the PC)
MENU_ENTRY("system", 75, shortcut_row);
#endif
// S45, one screen: the game's touch-screen menus drawn on the top screen by the game itself (mh4n port/src/onescreen.cpp,
// after Heavenfall's single-screen arrangement by popzol and BlitzWOLF, GPL-3.0), in every language. mh4u.ini's
// single_screen, which applies at the game's next frame. Off while a code mod is on: a single-screen mod (jM5557's,
// Heavenfall) moves the same menus with its own code, and android_main.cpp then keeps it off for the game.
bool onescreen_code_mod() {
    for (const auto& m : g_mod_info)
        if (m.on && m.code) return true;
    return false;
}
void onescreen_rows(std::vector<Row>& r, Draft& c) {
    add_switch(r, "onescreen", "Single-screen mode", &c.single_screen, "single_screen");
    if (onescreen_code_mod()) {
        r.back().enabled = false;
        r.back().value = say("Off (a code mod is on)", "No (hay un mod de código activo)");
        r.back().toggle = -1;
    }
}
void reset_onescreen() {  // (not while a code mod is on: its row can't change then either)
    if (!onescreen_code_mod()) s.draft.single_screen = defaults().single_screen;
}
// (MENU.md 3.2: first on Screen and HUD)
MENU_ENTRY("graphics/screen", 5, [](std::vector<Row>& r) { onescreen_rows(r, s.draft); }, reset_onescreen);
#ifndef __ANDROID__
#include "pc/settings_pc_files.inc"  // (the PC's: the log's folder, the builder's and the licenses' help, the textures)
#endif
// S46 (group build): the game's code library made on the phone (android/.../BuilderActivity.java). Its row (help "build"
// in HELP below, its words in settings_locale.h): MENU.md puts it in System, Game code (system/code, 10).
void game_code_rows(std::vector<Row>& r) {
    action(r, "game.build", tr("Build the game code"), []() { app_call_activity("showBuilder"); }, true, "build");
#ifndef __ANDROID__
    r.back().help = pc_build_help();
#endif
}
MENU_ENTRY("system/code", 10, game_code_rows);
void reset_gameplay_comforts_10() {
    Draft f = defaults();
    Draft& c = s.draft;
    c.qol_gather = f.qol_gather, c.qol_flex = f.qol_flex, c.qol_sharpen = f.qol_sharpen, c.qol_rewards = f.qol_rewards;
}
MENU_ENTRY("gameplay/comforts", 10, rows_gameplay_comforts_10, reset_gameplay_comforts_10);

// ---- Mods & Cheats
void reset_mods_list_10() {  // (every mod on, in the order of their names: as without a list)
    std::sort(s.draft.mods.begin(), s.draft.mods.end(), [](const Draft::ModRow& a, const Draft::ModRow& b) { return a.name < b.name; });
    for (auto& m : s.draft.mods) m.on = true;
}
MENU_ENTRY("mods/list", 10, mods_rows, reset_mods_list_10);
void reset_mods_cheats_10() {
    for (char& on : s.draft.cheats) on = 0;
}
MENU_ENTRY("mods/cheats", 10, cheat_rows, reset_mods_cheats_10);

// ---- System
void rows_system_10(std::vector<Row>& r) {
    add_choice(r, "language", "Language", s.draft.language, {tr("Phone's"), "English", "Español"}, [](int v) { s.draft.language = v; },
               "Language");
}
void reset_system_10() { s.draft.language = defaults().language; }
MENU_ENTRY("system", 10, rows_system_10, reset_system_10);
void rows_system_50(std::vector<Row>& r) {
    add_bool(r, "pause.bg", "Pause in the background", &s.draft.pause_background, "PauseInBackground");
#ifndef __ANDROID__  // (a PC: the window minimized, pc/pc_main.cpp)
    r.back().help = en_es("The game stops while its window is minimized, and goes on where it was when you bring it back. "
                          "Off: it keeps running, as a hunt with others online needs.",
                          "El juego se detiene mientras su ventana está minimizada, y sigue donde estaba al volver a "
                          "abrirla. Apagado: sigue corriendo, como lo necesita una cacería en línea con otros.");
#endif
    add_bool(r, "pause.menu", "Pause in this menu", &s.draft.pause_menu, "PauseInMenu");
}
void reset_system_50() {
    Draft f = defaults();
    s.draft.pause_background = f.pause_background, s.draft.pause_menu = f.pause_menu;
}
MENU_ENTRY("system", 50, rows_system_50, reset_system_50);
void rows_system_70(std::vector<Row>& r) { add_bool(r, "updates", "Check for updates", &s.draft.updates, "Updates"); }
void reset_system_70() { s.draft.updates = defaults().updates; }
MENU_ENTRY("system", 70, rows_system_70, reset_system_70);
void rows_system_90(std::vector<Row>& r) {
    header(r, "app.head", tr("The app"));
    action(r, "restart", tr("Restart"), []() { push(View::Restart, "dialog.cancel"); }, true, "restart");
    action(r, "exit", tr("Exit game"), []() { push(View::Exit, "dialog.cancel"); }, true, "exit");
    action(r, "back.game", tr("Back to the game"), []() { ask_close(g_open); }, true, "back");
}
MENU_ENTRY("system", 90, rows_system_90);
// System > This menu
void rows_system_menu_10(std::vector<Row>& r) {
    add_choice(r, "theme", "Menu colours", s.draft.theme, {tr("Hunter"), tr("Gold"), tr("Blue")}, [](int v) { s.draft.theme = v; },
               "Theme");
    add_number(r, "menu.scale", "Menu size", &s.draft.menu_scale, 50, 200, 10, "%d%%", "MenuScale");
}
void reset_system_menu_10() {
    Draft f = defaults();
    s.draft.theme = f.theme, s.draft.menu_scale = f.menu_scale;
}
MENU_ENTRY("system/menu", 10, rows_system_menu_10, reset_system_menu_10);
// Verify game files (verify_files.h; the MHGU port's same row, view and words): the game and its update read whole on a
// thread of their own, and a report of what is wrong and what to do. The row's value says where the check is
bool verify_running(const VerifyState& v) { return v.stage == VerifyState::Listing || v.stage == VerifyState::Reading; }
std::string verify_value(const VerifyState& v) {
    switch (v.stage) {
    case VerifyState::Listing: return "0 %";
    case VerifyState::Reading: return fmt("%d %%", v.total ? (int)(v.done * 100 / v.total) : 0);
    case VerifyState::Done: return v.problems == 0 ? tr("All good") : v.problems == 1 ? tr("1 problem") : fmt("%d problems", v.problems);
    case VerifyState::Cancelled: return tr("Cancelled");
    default: return "";
    }
}
void verify_open() {
    const VerifyState v = verify_files_state();
    if (v.stage == VerifyState::Idle || v.stage == VerifyState::Cancelled) verify_files_start();
    push(View::Verify, v.stage == VerifyState::Done ? "verify.again" : "verify.cancel");
    if (v.stage == VerifyState::Done) s.focus_request = false;  // (a report opens at its top, the verdict, not scrolled to its last rows)
}
// View::Verify: while it runs, its progress (a bar), Cancel and Back; then the verdict, the findings (problems first, an
// example file or two under them), what to do, where the full list is, Check again and Back
void verify_rows(std::vector<Row>& r) {
    const VerifyState v = verify_files_state();
    const Look k = touch_look();
    auto gb = [](uint64_t bytes) {
        std::string t = fmt("%.1f", bytes / 1e9);
        if (spanish()) std::replace(t.begin(), t.end(), '.', ',');
        return t;
    };
    if (v.stage == VerifyState::Idle || verify_running(v)) {
        std::string line = tr("Listing the files...");
        float progress = 0;
        if (v.stage == VerifyState::Reading && v.total) {
            progress = (float)((double)v.done / (double)v.total);
            line = fmt("Reading the files: %s of %s GB (%d %%)", gb(v.done).c_str(), gb(v.total).c_str(), (int)(v.done * 100 / v.total));
            if (v.reading_s >= 5 && v.done > 0) {  // (the time left, at the rate so far)
                const double left = v.reading_s * (double)(v.total - v.done) / (double)v.done;
                line += left >= 90 ? fmt(" · about %d min left", (int)std::lround(left / 60)) : std::string(tr(" · less than 2 min left"));
            }
        }
        note(r, "verify.progress", line);
        r.back().progress = progress, r.back().mark = theme().accent;
        note(r, "verify.wait", tr("You can leave this page or close the menu: the check goes on."), k.dim);
        action(r, "verify.cancel", tr("Cancel"), []() { verify_files_cancel(); });
        r.back().arrow = false;
        action(r, "verify.back", tr("Back"), []() { pop(); });
        r.back().arrow = false;
        return;
    }
    if (s.focus == "verify.cancel")  // (it ended under the cursor: the report stays at its top)
        s.focus = "verify.again", s.focus_request = false, s.focus_scroll_frames = 0;
    header(r, "verify.verdict", v.stage == VerifyState::Cancelled ? tr("Check cancelled")
                                : v.problems == 0                ? tr("All good: your game files are right")
                                : v.problems == 1                ? tr("1 problem found")
                                                                 : fmt("%d problems found", v.problems));
    bool todo = false;
    int n = 0;
    for (const VerifyLine& l : v.lines) {
        const std::string& text = spanish() ? l.es : l.en;
        if (l.kind == VerifyLine::Todo && !todo) {
            header(r, "verify.todo", tr("What to do"));
            todo = true;
        }
        const std::string id = "verify.l." + std::to_string(n++);
        switch (l.kind) {
        case VerifyLine::Problem: note(r, id, text, theme().warn); break;
        case VerifyLine::Info: note(r, id, text, k.dim); break;
        case VerifyLine::File:
            note(r, id, text, k.dim);
            r.back().sub = true;
            break;
        default:  // (a part that is right, and the advice: the accent's diamond, the text as the others')
            note(r, id, text);
            r.back().mark = theme().accent;
        }
    }
    note(r, "verify.log", tr("The full list is in the log: System › Your game › Share log."), k.dim);
    action(r, "verify.again", tr("Check again"), []() {
        verify_files_start();
        focus("verify.cancel");
    });
    r.back().arrow = false;
    action(r, "verify.back", tr("Back"), []() { pop(); });
    r.back().arrow = false;
}

// System > Your game (the MHGU port's same rows)
void rows_system_game_10(std::vector<Row>& r) {
    action(r, "game.folder", tr("Game folder"), []() { app_call_activity("showGameFolder"); }, true, "folder");
    action(r, "game.verify", tr("Verify game files"), []() { verify_open(); }, true, "verify");
    r.back().value = verify_value(verify_files_state());
    action(r, "save.folder", tr("Save folder"), []() { app_call_activity("showSaveFolder"); }, true, "savefolder");
    action(r, "save.import", tr("Import save"), []() { app_call_activity("showSaveImport"); }, true, "import");
    action(r, "save.export", tr("Export save"), []() { app_call_activity("showSaveExport"); }, true, "export");
#ifdef __ANDROID__
    action(r, "log.share", tr("Share log"), []() { app_call_activity("showShareLog"); }, true, "log");
#else
    pc_log_row(r);
#endif
}
MENU_ENTRY("system/game", 10, rows_system_game_10);

// ---- Credits & Licenses: who made it and where the official one is (the licence's additional terms keep these lines)
// S26 (the gfx group's): Mods & Cheats > Texture packs (MENU.md 3.5), a note on where a pack goes
MENU_ENTRY("mods/textures", 10, [](std::vector<Row>& r) {
    note(r, "textures.how",
         say("Put the pack's folder in your game folder's textures. Each texture takes memory as it's drawn.",
             "Pon la carpeta del pack en la carpeta textures de tu juego. Cada textura ocupa memoria al dibujarse."));
});
// 1.0.1 (4u-native #56): the pack's textures at their full size (the pack's own look, the default) or at half their width
// and height, a quarter of the memory, for a phone short of it (mh4n hd_textures.h, MH4N_HD_SCALE=half: only files four
// times the game's texture or more, so they stay at least twice it). Applied at the next start.
void hd_size_rows(std::vector<Row>& r) {
    add_choice(r, "textures.size", "Pack texture size", s.draft.hd_half ? 1 : 0, {tr("Full"), tr("Half")},
               [](int v) { s.draft.hd_half = v == 1; }, "HdPackSize");
    r.back().cold = true;
}
void reset_hd_size() { s.draft.hd_half = defaults().hd_half; }
MENU_ENTRY("mods/textures", 20, hd_size_rows, reset_hd_size);
// The credits (1.0.2, the owner's: those who contributed to the port, by name - not which idea each gave; the MHGU port's
// the same names, and jM5557's here). Single-screen mode's line stays: it says whose GPL-3.0 work it follows
void credits_10_rows(std::vector<Row>& r) {
    note(r, "about.thanks", say("Contributed to development:", "Aportaron al desarrollo:"));
    for (const char* name : {"tabarong", "Klark231", "Alfonso Matador", "popzol", "BlitzWOLF", "jM5557"}) {
        note(r, std::string("about.thanks.") + name, name);
        r.back().sub = true;
    }
    note(r, "about.onescreen", tr("Single-screen mode: after Heavenfall's, by popzol and BlitzWOLF (GPL-3.0)."));
}
void rows_about_10(std::vector<Row>& r) {
    header(r, "about.head", MH4U_APP_NAME " " MH4U_APP_VERSION);
    note(r, "about.by", tr("Made by khorne-hub. Free and open source, under the GPL-3.0 with additional terms."));
    note(r, "about.site", std::string(tr("Official download:")) + " github.com/khorne-hub/4u-native");
    note(r, "about.base", tr("Based on MH4UltimateDC by CompaLarios18, with parts from Azahar and Citra."));
    note(r, "about.mod", tr("The single-screen mod is by jM5557."));
    note(r, "about.gu", std::string(tr("Also by khorne-hub: MHGU for Android,")) + " github.com/khorne-hub/gu-native");
    credits_10_rows(r);
    note(r, "about.legal", tr("This app includes no game code, files, ROMs or keys: you need your own copy of the game. Not affiliated with the Great C or the Great N."));
    note(r, "about.paid", tr("Only the links khorne-hub gives are official. If you got it anywhere else, paid for it, or it came with ads, it is not the official one."));
    action(r, "credits", tr("Show the credits"), []() { credits_show(); }, true, "credits");
    action(r, "licenses", tr("Licenses"), []() { app_call_activity("showLicenses"); }, true, "licenses");  // (1.0.2: as MHGU's)
#ifndef __ANDROID__
    r.back().help = pc_licenses_help();
#endif
    header(r, "thanks.head", tr("Thank you"));
    note(r, "thanks.body", tr(THANKS_BODY));
    note(r, "thanks.sign", "\xE2\x80\x94 khorne-hub");
    action(r, "thanks", tr("Show the thanks"), []() { thanks_show(); }, true);
    r.back().help = tr("Shows the thank-you again, as it showed the first time the game opened.");  // (1.0.2: as MHGU's)
}
MENU_ENTRY("about", 10, rows_about_10);

// ---------------------------------------------------------------- a page's rows
const MenuPage* find_page(const std::string& id) {
    for (const MenuPage& p : menu_pages())
        if (id == p.id) return &p;
    return nullptr;
}
bool entry_shown(const MenuEntry& e) { return !e.shown || e.shown(); }
// A page with something on it: an entry that shows, or a page under it that has (its row: none on an empty one)
bool page_live(const std::string& id) {
    for (const MenuEntry& e : menu_entries())
        if (id == e.page && entry_shown(e)) return true;
    for (const MenuPage& p : menu_pages())
        if (id == p.parent && (!p.shown || p.shown()) && page_live(p.id)) return true;
    return false;
}
// The page shown now: a section's own, or the one one level down
std::string current_page() { return s.view == View::Page ? s.at : std::string(page_ids[s.page]); }
// A page's title: a section's name, else its own (English: tr)
const char* page_title(const std::string& id) {
    for (int i = 0; i < PAGES; i++)
        if (id == page_ids[i]) return page_names[i];
    const MenuPage* p = find_page(id);
    return p ? p->title : "";
}
// "Section › Page" (one level down), or the section's name
std::string page_path(const std::string& id) {
    const MenuPage* p = find_page(id);
    if (!p) return tr(page_title(id));
    return std::string(tr(page_title(p->parent))) + "  \xE2\x80\xBA  " + tr(p->title);
}
void open_page(const std::string& id) {
    push(View::Page, "");
    s.at = id;
}
// Its entries' rows and the rows of the pages under it, by their order (the same order: in the order they were placed)
std::vector<Row> page_rows(const std::string& id) {
    struct Item {
        int order;
        const MenuEntry* entry;
        const MenuPage* page;
    };
    std::vector<Item> items;
    for (const MenuEntry& e : menu_entries())
        if (id == e.page && entry_shown(e)) items.push_back({e.order, &e, nullptr});
    for (const MenuPage& p : menu_pages())
        if (id == p.parent && (!p.shown || p.shown()) && page_live(p.id)) items.push_back({p.order, nullptr, &p});
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.order < b.order; });
    std::vector<Row> r;
    for (const Item& it : items) {
        if (it.entry) {
            it.entry->rows(r);
            continue;
        }
        const std::string to = it.page->id;
        action(r, "page." + to, tr(it.page->title), [to]() { open_page(to); });
        if (it.page->link) it.page->link(r.back());
    }
    return r;
}
std::vector<Row> main_rows() {
    const std::string id = current_page();
    std::vector<Row> r = page_rows(id);
    if (s.page != PAGE_ABOUT) {  // (Credits & Licenses: nothing to reset)
        const bool page = s.view == View::Page;
        action(r, "reset", tr(page ? "Reset this page" : "Reset this section"), [id]() {
            s.reset_at = id;
            push(View::Reset, "dialog.cancel");
        }, true);
        r.back().help = page ? say("Puts this page's options back as they come. Nothing changes until you save.",
                                   "Devuelve las opciones de esta página a como vienen. Nada cambia hasta que guardes.")
                             : say("Puts this section's options back as they come, on its pages too. Nothing changes until you save.",
                                   "Devuelve las opciones de esta sección a como vienen, también las de sus páginas. Nada cambia hasta que guardes.");
    }
    return r;
}

// The keyboard's keys, ten to a line in the phone's order (letters, or symbols and Spanish letters), the ones the text
// can't take faded; then Shift, Symbols, Space, Delete, Clear; Paste, the phone's keyboard, Cancel, Accept
void keyboard_rows(std::vector<Row>& r) {
    const Keyboard& k = s.keyboard;
    static const char* const LETTERS[4] = {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm._:"};
    static const char* const SYMBOLS[4][10] = {
        {"!", "?", "@", "#", "$", "%", "&", "*", "(", ")"},
        {"+", "=", "/", "\\", "'", "\"", ";", ",", "<", ">"},
        {"[", "]", "{", "}", "~", "^", "|", "`", "_", "-"},
        {"\xC3\xA1", "\xC3\xA9", "\xC3\xAD", "\xC3\xB3", "\xC3\xBA", "\xC3\xB1", "\xC3\xBC", "\xC2\xBF", "\xC2\xA1", "."}};
    static const char* const ACCENTS_UPPER[10] = {"\xC3\x81", "\xC3\x89", "\xC3\x8D", "\xC3\x93", "\xC3\x9A",
                                                  "\xC3\x91", "\xC3\x9C", "\xC2\xBF", "\xC2\xA1", "."};
    int n = 0;
    for (int line = 0; line < 4; line++)
        for (int i = 0; i < 10; i++) {
            std::string ch = !k.symbols           ? std::string(1, k.upper ? (char)SDL_toupper(LETTERS[line][i]) : LETTERS[line][i])
                             : line == 3 && k.upper ? ACCENTS_UPPER[i]
                                                    : SYMBOLS[line][i];
            Row row;
            row.id = "key." + std::to_string(n++), row.title = ch;
            row.grid = 10;
            row.enabled = allowed(k.filter, ch);
            row.activate = [ch]() { insert(ch); };
            r.push_back(std::move(row));
        }
    auto key = [&r](const char* id, const std::string& title, std::function<void()> fn, int grid, bool enabled = true) {
        Row row;
        row.id = id, row.title = title;
        row.grid = grid;
        row.enabled = enabled;
        row.activate = std::move(fn);
        r.push_back(std::move(row));
    };
    key("key.shift", tr("Shift"), []() { s.keyboard.upper = !s.keyboard.upper; }, 5);
    r.back().current = k.upper;
    key("key.symbols", tr(k.symbols ? "Letters" : "Symbols"), []() { s.keyboard.symbols = !s.keyboard.symbols; }, 5);
    key("key.space", tr("Space"), []() { insert(" "); }, 5, allowed(k.filter, " "));
    key("key.delete", tr("Delete"), []() { erase_previous(); }, 5);
    key("key.clear", tr("Clear"), []() {
        s.keyboard.buffer.clear();
        s.keyboard.cursor = 0;
        s.error.clear();
    }, 5);
#ifndef __ANDROID__
    // (the PC: no phone's keyboard - the PC's own types)
    key("key.paste", tr("Paste"), []() { paste(); }, 3);
    key("key.cancel", tr("Cancel"), []() { pop(); }, 3);
    key("key.accept", tr("Accept"), []() { accept_keyboard(); }, 3);
    return;
#endif
    key("key.paste", tr("Paste"), []() { paste(); }, 4);
    key("key.phone", tr("Phone keyboard"), []() { phone_keyboard(); }, 4);
    key("key.cancel", tr("Cancel"), []() { pop(); }, 4);
    key("key.accept", tr("Accept"), []() { accept_keyboard(); }, 4);
}

// Mapping a controller (padmap.h): the list of those connected, then its steps (MHGU's pad_list_rows, pad_map_rows)
void pad_list_rows(std::vector<Row>& r) {
    padmap_watch();
    char names[PADMAP_MAX][64];
    bool custom[PADMAP_MAX];
    SDL_JoystickID ids[PADMAP_MAX];
    int n = padmap_list(names, custom, ids, PADMAP_MAX);
    if (!s.pad_note.empty()) note(r, "pad.note", s.pad_note, theme().accent);
    if (!n) header(r, "pad.none", tr("No controller connected"));
    for (int i = 0; i < n; i++) {
        action(r, "pad." + std::to_string(i), names[i], [i]() {
            s.pad_note.clear();
            padmap_begin(i);
            if (padmap_active()) push(View::PadMap, "pm.go");
        });
        r.back().help = tr("Press each of the 3DS's buttons on it, one by one. From then on it is read that way.");
        const bool used = ids[i] == controls_active_pad();  // (two of a kind: the one in the player's hands)
        r.back().value = tr(custom[i] ? (used ? "Mapped, in use" : "Mapped") : used ? "In use" : "");
        if (custom[i]) {
            std::string name = names[i];
            action(r, "padf." + std::to_string(i), fmt("Forget the map of %s", names[i]), [i, name]() {
                s.pad_note = padmap_forget(i) ? fmt("%s: read as before again.", name.c_str())
                                              : fmt("%s: read as before from the next start.", name.c_str());
                focus("pad." + std::to_string(i));
            });
            r.back().help = tr("It is read as SDL reads it again: at once if SDL knew it, else from the next start.");
        }
    }
    action(r, "pad.back", tr("Back"), []() { pop(); });
    r.back().arrow = false;
}
void pad_map_rows(std::vector<Row>& r) {
    padmap_watch();
    int step = padmap_step(), steps = padmap_steps();
    if (!padmap_active()) {  // (saved, or it was unplugged)
        pop();
        return;
    }
    if (step == PADMAP_REST) {
        header(r, "pm.rest", tr("Before the buttons"));
        note(r, "pm.how", tr("Move both sticks and press the triggers once, then let everything go"));
        action(r, "pm.go", tr("Continue"), []() { padmap_continue(); });
        r.back().help = tr("On the screen, or with another controller: the one being mapped is not read by the menu meanwhile.");
    } else if (step < steps) {
        header(r, "pm.step", tr(padmap_step_title(step)));
        note(r, "pm.how", fmt("%d of %d: press it on the controller, or Skip if it has none.", step + 1, steps));
        action(r, "pm.go", tr("Skip"), []() { padmap_skip(); });
        r.back().help = tr("Leave this one out: the buttons your controller does not have.");
        if (step > 0) action(r, "pm.back", tr("Previous"), []() { padmap_back(); });
    } else {
        header(r, "pm.done", tr("That's all of them"));
        action(r, "pm.go", tr("Save"), []() {
            padmap_save();
            s.pad_note = tr("Saved: the controller is read with its new map.");
            pop();
        });
        action(r, "pm.back", tr("Previous"), []() { padmap_back(); });
    }
    for (Row& row : r) row.arrow = false;
    action(r, "pm.cancel", tr("Cancel"), []() {
        padmap_cancel();
        pop();
    });
    r.back().arrow = false;
}
// ---- the control profiles (S12, after tabarong's design; the 1.0 layout group's): the Controls tab's link to them
// (control_profile_row), their page (View::Profiles: the ten slots) and a slot's (View::ProfileSlot: Use, Save the current
// layout here, New of either kind, Edit, Clear). Each acts at once on the layout in use (touch.cpp touch_profile_*), not on
// the menu's settings; replacing or emptying a slot asks for a second tap. The layout editor's Profile window does the
// same. The MHGU port's the same rows and texts
int s_profile = 1;        // the slot View::ProfileSlot shows
std::string s_prof_arm;   // the row tapped once that a second tap confirms ("" none)
std::string s_prof_said;  // what the last action did (a note under the rows)
std::string profile_what(int n) {
    int kind = touch_profile_kind(n);
    std::string t = tr(kind < 0 ? "Empty" : kind == 1 ? "Emulator style" : "Ours");
    if (n == touch_profile_active()) t += std::string("  \xC2\xB7  ") + tr("In use");
    return t;
}
// The players' own pictures for the touch buttons (S06, the 1.0 layout group's): the screen that copies them in
// (PicturesActivity.java); the layout editor's Picture puts one on a button. The MHGU port's the same row
void button_images_row(std::vector<Row>& r) {
    action(r, "touch.images", tr("Button images"), []() { app_call_activity("importPictures"); });
    r.back().help = tr("Copy pictures of your own (PNG, JPG or WebP) to put on the touch buttons, then pick one in the layout editor with Picture. What you add is your own responsibility.");
}
void control_profile_row(std::vector<Row>& r) {
    action(r, "touch.profiles", tr("Control profiles"), []() {
        s_prof_said.clear();
        push(View::Profiles, "pf.1");
    });
    int n = touch_profile_active(), kind = touch_profile_kind(n);
    r.back().value = fmt(kind == 1 ? "Profile %d: emulator style" : "Profile %d: ours", n);
    r.back().help = tr("Ten slots, each a whole layout of the touch controls and the HUD: the port's own or an emulator-style one. Use one, save the one you play with in another, or edit it. A design by tabarong.");
}
void profile_rows(std::vector<Row>& r) {
    for (int n = 1; n <= 10; n++) {
        action(r, "pf." + std::to_string(n), fmt("Profile %d", n), [n]() {
            s_profile = n, s_prof_arm.clear(), s_prof_said.clear();
            push(View::ProfileSlot, "ps.use");
        });
        r.back().value = profile_what(n);
    }
    note(r, "pf.where", tr("Kept in the \"layout profiles\" folder of your save folder (or of the app's), with your saves."));
    action(r, "pf.back", tr("Back"), []() { pop(); });
    r.back().arrow = false;
}
void profile_slot_rows(std::vector<Row>& r, bool* open) {
    touch_profile_import_poll();  // (the .zip the app's screen copied in, 1.0.2)
    if (std::string said; touch_profile_said(&said)) s_prof_said = said, s_prof_arm.clear();
    int n = s_profile, active = touch_profile_active(), kind = touch_profile_kind(n);
    bool used = kind >= 0, in_use = n == active;
    note(r, "ps.what", profile_what(n));
    // (an action that replaces or empties a slot: a first tap arms it, its title says to tap again)
    auto armed = [&r](const char* id, const char* title, const char* again, bool ask, std::function<bool()> fn, const char* done) {
        bool on = s_prof_arm == id;
        action(r, id, tr(on ? again : title), [id = std::string(id), ask, fn, done]() {
            if (ask && s_prof_arm != id) {
                s_prof_arm = id;
                return;
            }
            s_prof_arm.clear();
            s_prof_said = tr(fn() ? done : "It couldn't be done. Is the save folder there?");
        });
        r.back().arrow = false;
    };
    action(r, "ps.use", tr("Use"), [n]() {
        s_prof_arm.clear();
        s_prof_said = tr(touch_profile_use(n) ? "In use" : "It couldn't be done. Is the save folder there?");
    }, used && !in_use);
    r.back().arrow = false;
    r.back().help = tr("Play with this profile's layout from now on.");
    armed("ps.save", "Save the current layout here", "Tap again to replace it", used && !in_use, [n]() { return touch_profile_store(n); },
          "Saved");
    r.back().help = tr("The layout you play with now goes into this profile, which is the one in use from then on.");
    armed("ps.new", "New: ours", "Tap again to start it over", used, [n]() { return touch_profile_new(n, 0); }, "In use");
    armed("ps.emu", "New: emulator style", "Tap again to start it over", used, [n]() { return touch_profile_new(n, 1); }, "In use");
    r.back().help = tr("Each button presses only its own button of the game's, as an emulator's pad on the screen, laid out the same way. Move, resize and hide each one in the editor.");
    action(r, "ps.edit", tr("Edit"), [n, in_use, open]() {
        if (!in_use && !touch_profile_use(n)) {
            s_prof_said = tr("It couldn't be done. Is the save folder there?");
            return;
        }
        s.closing = true;  // (the editor opens as the menu closes, as Customize controls and HUD)
        if (s.draft == s.saved) close_menu(open);
        else push(View::CloseChanges, "dialog.save");
    }, used);
    r.back().help = tr("Opens the layout editor on this profile: it is the one in use from then on.");
    armed("ps.clear", "Clear", "Tap again to empty it", true, [n]() { return touch_profile_clear(n); }, "Emptied");
    r.back().enabled = used && !in_use;
    // (1.0.2) the profile in a .zip with its pictures and combined buttons, in a folder the player chooses, and back, as
    // the MHGU port's: the app's screen (ProfileZipActivity) moves the .zip; what happened comes back to this page
    action(r, "ps.export", tr("Export"), [n]() {
        s_prof_arm.clear(), s_prof_said.clear();
        if (touch_profile_export_app(n)) app_call_activity("exportProfile");
    }, used);
    r.back().arrow = false;
    r.back().help = tr("Saves this profile with its pictures and combined buttons in a .zip, in a folder you choose.");
    const bool arm = s_prof_arm == "ps.import";
    action(r, "ps.import", tr(arm ? "Tap again to replace it" : "Import"), [n, used]() {
        if (used && s_prof_arm != "ps.import") {
            s_prof_arm = "ps.import";
            return;
        }
        s_prof_arm.clear(), s_prof_said.clear();
        touch_profile_import_wait(n);
        app_call_activity("importProfile");
    });
    r.back().arrow = false;
    r.back().help = tr("Puts a profile from a .zip made with Export in this slot, with its pictures.");
    if (!s_prof_said.empty()) note(r, "ps.said", s_prof_said);
    action(r, "ps.back", tr("Back"), []() { s_prof_arm.clear(), pop(); });
    r.back().arrow = false;
}
// The layout group's places on the menu (MENU.md 3.1): the profiles' link on Controls, the floating stick and the
// pictures on Touch controls
MENU_ENTRY("controls", 20, control_profile_row);
void rows_controls_touch_60(std::vector<Row>& r) {
    int i = 0;
    while (i < TB - 1 && strcmp(TOUCH_BUTTONS[i].key, "floating_stick") != 0) i++;
    Row row;
    bool on = s.draft.show[i] != 0;
    row.id = "touch.float", row.title = tr(TOUCH_BUTTONS[i].title), row.key = "show_floating_stick";
    row.value = tr(on ? "On" : "Off");
    row.toggle = on;
    row.adjust = [i](int d) { s.draft.show[i] = d > 0; };
    row.activate = [i]() { s.draft.show[i] = !s.draft.show[i]; };
    r.push_back(std::move(row));
}
void reset_controls_touch_60() {
    Draft f = defaults();
    for (int i = 0; i < TB; i++)
        if (TOUCH_BUTTONS[i].group == 3) s.draft.show[i] = f.show[i];
}
MENU_ENTRY("controls/touch", 60, rows_controls_touch_60, reset_controls_touch_60);
// (1.0.2, S-CAM, the MHGU port's too) a drag turns the camera only from the screen's right half, off by default
void rows_controls_touch_62(std::vector<Row>& r) {
    int i = 0;
    while (i < TB - 1 && strcmp(TOUCH_BUTTONS[i].key, "camera_right_half") != 0) i++;
    Row row;
    bool on = s.draft.show[i] != 0;
    row.id = "touch.camright", row.title = tr(TOUCH_BUTTONS[i].title), row.key = "show_camera_right_half";
    row.value = tr(on ? "On" : "Off");
    row.toggle = on;
    row.adjust = [i](int d) { s.draft.show[i] = d > 0; };
    row.activate = [i]() { s.draft.show[i] = !s.draft.show[i]; };
    r.push_back(std::move(row));
}
MENU_ENTRY("controls/touch", 62, rows_controls_touch_62);
MENU_ENTRY("controls/touch", 70, button_images_row);
int s_combo = 0;  // the combined button View::ComboEdit shows
// (1.0.2) How many combined buttons the lists show: the first six, then up to one empty one after the last one used (with
// buttons or a controller's button), fifteen at most - so the list doesn't fill with empty ones
int combos_listed() {
    int last = 0;
    for (int k = 0; k < kCombos; k++)
        if (!s.draft.combo[k].empty() || s.draft.combo_pad[k]) last = k + 1;
    return std::clamp(last + 1, 6, kCombos);
}
// (1.0.2) The combined buttons are the profile in use's: one changed from the Control profiles page (Use, New...) brings
// its own, in the draft and as saved (an edit of the one before not saved yet is left behind with it)
void combos_follow_profile() {
    static int was = -1;
    const int now = touch_profile_active();
    if (now == was) return;
    if (was >= 0)
        for (int k = 0; k < kCombos; k++) s.draft.combo[k] = s.saved.combo[k] = touch_setting("combo" + std::to_string(k + 1));
    was = now;
}
// ---- (1.0.2) a combined button on any controller button: the Controller button list's names (kComboPadKeys' order),
// one's name (a raw button by its number), and one chosen - taken from another combined button that had it
std::vector<std::string> combo_pad_names() {
    return {tr("None"), tr("Left stick click"), tr("Right stick click"), "SELECT", "Guide", "Misc", tr("Paddle 1"), tr("Paddle 2"),
            tr("Paddle 3"), tr("Paddle 4"), tr("Touchpad"), tr("A (right)"), tr("B (bottom)"), tr("X (top)"), tr("Y (left)"), "L", "R",
            "ZL", "ZR", "START", tr("D-pad up"), tr("D-pad down"), tr("D-pad left"), tr("D-pad right")};
}
std::string combo_pad_name(int v) {
    if (v >= kComboPadRaw) return fmt("Button %d (no name)", v - kComboPadRaw);
    std::vector<std::string> names = combo_pad_names();
    return v > 0 && v < (int)names.size() ? names[v] : names[0];
}
void combo_pad_take(int k, int v) {
    for (int j = 0; j < kCombos; j++)
        if (j != k && v && s.draft.combo_pad[j] == v) s.draft.combo_pad[j] = 0;
    s.draft.combo_pad[k] = v;
}
// View::PadCapture (Press a controller button): the combined button it is for, when it began; the first button pressed
// is its (controls_take_pad), and after kPadCaptureMs nothing was
int s_pad_capture_combo = 0;
Uint64 s_pad_capture_ms = 0;
constexpr Uint64 kPadCaptureMs = 8000;
void pad_capture() {
    const int v = controls_take_pad();
    if (v > 0) {
        combo_pad_take(s_pad_capture_combo, v);
        pop();
        focus("ce.pad");
    } else if (SDL_GetTicks() - s_pad_capture_ms > kPadCaptureMs) {
        pop();
        focus("ce.press");
    }
}

// ---------------------------------------------------------------- (S14) the keyboard and the mouse, the MHGU port's
// A keyboard and a mouse connected (controls_keyboard_mouse): Keybinds and the mouse's speed on Controls > Keyboard and
// mouse (MENU.md), the notes on its keys there otherwise. The keys and the mouse work as before either way
const char* const BIND_TITLES[BINDS] = {
    "Move forward", "Move back", "Move left", "Move right", "Walk", "Camera up", "Camera down", "Camera left", "Camera right",
    "A", "B", "X", "Y", "L", "R", "ZL", "ZR", "START", "SELECT", "D-pad up", "D-pad down", "D-pad left", "D-pad right",
    "Left stick click", "Right stick click", "Item left", "Item right", "Combined button 1", "Combined button 2",
    "Combined button 3", "Combined button 4", "Combined button 5", "Combined button 6", "Combined button 7",
    "Combined button 8", "Combined button 9", "Combined button 10", "Combined button 11", "Combined button 12",
    "Combined button 13", "Combined button 14", "Combined button 15",
#ifndef __ANDROID__
    "List of the controls", "Item shortcut 1", "Item shortcut 2", "Item shortcut 3", "Item shortcut 4",
    "Guild Cards", "Gestures", "Friends", "Hunters for Hire",
#endif
};
int s_capture_bind = 0;  // (View::KeyCapture: the keybind a key is chosen for, and whether it is added to its keys)
bool s_capture_add = false;
void keyboard_mouse_rows(std::vector<Row>& r) {
    action(r, "keybinds", tr("Keybinds"), []() { push(View::Keybinds, "bind.0"); });
    r.back().help = tr("The keys and mouse buttons for the game's buttons, for moving, the camera, the item bar and the combined buttons. A left click on the game's picture captures the mouse: it turns the camera until Escape lets it go. A click on the touch screen or on a touch control is a tap there. These rows show while a keyboard and a mouse are connected.");
    Row row;
    char text[32];
    snprintf(text, sizeof text, "%.2fx", s.draft.mouse_sens / 100.f);
    row.id = "mouse.sens", row.title = tr("Mouse sensitivity"), row.value = text;
    row.help = tr("How far the camera turns for the mouse's movement: 1.00x as in the MHGU port.");
    row.number = true;
    row.adjust = [](int d) { s.draft.mouse_sens = std::clamp(s.draft.mouse_sens + d * 5, 10, 1000); };
    r.push_back(std::move(row));
}
void reset_keyboard_mouse() {
    Draft f = defaults();
    s.draft.binds = f.binds, s.draft.mouse_sens = f.mouse_sens;
}
MENU_ENTRY("controls/keys", 10, keyboard_mouse_rows, reset_keyboard_mouse, controls_keyboard_mouse);
#ifndef __ANDROID__
#include "settings_pc_input.inc"  // (the PC: the mouse's camera, the list of the controls, Button icons, the guides)
#endif
// What else a key does (another keybind, the keyboard's Back), for its row's warning; empty: nothing
std::string key_use(int code, int skip) {
    if (code == SDL_SCANCODE_AC_BACK) return tr("Settings");
    for (int i = 0; i < BINDS; i++)
        for (int k = 0; k < kBindKeys; k++)
            if (i != skip && s.draft.binds.keys[i][k] == code) return tr(BIND_TITLES[i]);
    return "";
}
void capture_key(int bind, bool add) {
    s_capture_bind = bind, s_capture_add = add;
    controls_take_key();  // (nothing pressed before counts)
    push(View::KeyCapture, "dialog.cancel");
}
// The key chosen (View::KeyCapture, each frame): Escape or Back cancels; a key is the keybind's only one, or added
void key_capture() {
    const int code = controls_take_key();
    if (code < 0) return;
    if (code == 0) {
        pop();
        focus("bind." + std::to_string(s_capture_bind));  // (back on its row: the picker's list was the one drawn last)
        return;
    }
    if (controls_key_name(code).empty()) {
        s.error = tr("That key can't be used.");
        return;
    }
    int* keys = s.draft.binds.keys[s_capture_bind];
    if (!s_capture_add) {
        for (int k = 0; k < kBindKeys; k++) keys[k] = k ? 0 : code;
    } else {
        bool has = false;
        for (int k = 0; k < kBindKeys; k++) has |= keys[k] == code;
        for (int k = 0; k < kBindKeys && !has; k++)
            if (!keys[k]) keys[k] = code, has = true;
    }
    pop();
    focus("bind." + std::to_string(s_capture_bind));
}
Row bind_row(int i) {
    Row row;
    row.id = "bind." + std::to_string(i);
    row.title = tr(BIND_TITLES[i]);
    int n = 0;
    for (int k = 0; k < kBindKeys; k++) {
        const int code = s.draft.binds.keys[i][k];
        if (!code) continue;
        n++;
        row.value += (row.value.empty() ? "" : ", ") + controls_key_name(code);
        const std::string use = key_use(code, i);
        if (!use.empty()) row.value += std::string(" (") + tr("in use:") + " " + use + ")", row.ink = IM_COL32(230, 120, 90, 255);
    }
    if (row.value.empty()) row.value = tr("None");
    if (i >= BIND_CAM_UP && i <= BIND_CAM_RIGHT)
        row.help = tr("Turns the camera as the right stick does, all the way.");
    if (i == BIND_LSTICK) row.help = tr("Shows or hides the touch screen, as a controller's left stick click.");
    if (i == BIND_RSTICK) row.help = tr("What a controller's right stick click does (its row in Controls, Controller): the lock-on by default.");
    if (i == BIND_ITEM_LEFT || i == BIND_ITEM_RIGHT) row.help = tr("Turns the item bar one item, as a swipe on it. The mouse's wheel by default.");
#ifndef __ANDROID__
    pc_bind_help(i, row);  // (the PC's own keybinds: settings_pc_input.inc)
#endif
    if (i >= BIND_COMBO1 && i <= BIND_COMBO15) {
        const std::string& combo = s.draft.combo[i - BIND_COMBO1];
        row.help = !combo.empty() ? fmt("Presses %s together while held: the buttons of combined button %d.", combo.c_str(), i - BIND_COMBO1 + 1)
                                  : fmt("Combined button %d has no buttons yet: choose them in Buttons on the screen.", i - BIND_COMBO1 + 1);
    }
    Row copy = row;
    row.menu = true;
    row.activate = [copy, i, n]() {
        std::vector<std::string> names = {tr("Set key")};
        const bool more = n > 0 && n < kBindKeys;
        if (more) names.push_back(tr("Add a key"));
        names.push_back(tr("None"));
        choices(copy, names, -1, [i, more](int v) {
            if (v == (more ? 2 : 1)) {
                for (int& k : s.draft.binds.keys[i]) k = 0;
                return;
            }
            capture_key(i, v == 1);
        });
    };
    return row;
}
void keybind_rows(std::vector<Row>& r) {
    for (int i = 0; i < BINDS; i++) {
        if (i == BIND_MOVE_UP) header(r, "bind.movement", tr("Movement"));
        if (i == BIND_CAM_UP) header(r, "bind.camera", tr("Camera"));
        if (i == BIND_A) header(r, "bind.buttons", tr("Buttons"));
        if (i == BIND_ITEM_LEFT) header(r, "bind.items", tr("Item bar"));
        if (i == BIND_COMBO1) header(r, "bind.combos", tr("Combined buttons"));
#ifndef __ANDROID__
        if (i == BIND_HELP) header(r, "bind.pc", en_es("The program and the touch screen", "El programa y la pantalla táctil"));
#endif
        // (the empty ones after the next one: hidden; the PC's own keybinds come after the combined buttons)
        if (i >= BIND_COMBO1 + combos_listed() && i <= BIND_COMBO15) continue;
        r.push_back(bind_row(i));
    }
#ifndef __ANDROID__  // (the chat's key, under Chat, as MHGU for PC's Keybinds: pc/settings_pc_net.inc)
    header(r, "bind.other", tr("Chat"));
    rows_multiplayer_chat_key(r);
    action(r, "bind.reset", tr("Reset keybinds"), []() { s.draft.binds = controls_default_binds(), reset_multiplayer_chat_key(); });
#else
    action(r, "bind.reset", tr("Reset keybinds"), []() { s.draft.binds = controls_default_binds(); });
#endif
    r.back().arrow = false;
    action(r, "bind.back", tr("Back"), []() { pop(); });
    r.back().arrow = false;
}

std::vector<Row> rows_for_view(bool* open) {
    g_open = open;
    if (s.view == View::Main || s.view == View::Page) return main_rows();
    std::vector<Row> r;
    if (s.view == View::Conflicts) {  // (the Mods tab's pairs of mods that change the same files)
        conflict_rows(r);
        if (r.empty()) note(r, "mods.conflict.none", tr("No conflicts found"), NET_GREEN);
        action(r, "conflicts.back", tr("Back"), []() { pop(); });
        r.back().arrow = false;
        return r;
    }
    if (s.view == View::PadList) {
        pad_list_rows(r);
        return r;
    }
    if (s.view == View::Verify) {
        verify_rows(r);
        return r;
    }
    if (s.view == View::Profiles) {
        profile_rows(r);
        return r;
    }
    if (s.view == View::ProfileSlot) {
        profile_slot_rows(r, open);
        return r;
    }
    if (s.view == View::ColourPreset) {
        colour_preset_rows(r);
        return r;
    }
    if (s.view == View::PadMap) {
        pad_map_rows(r);
        return r;
    }
    if (s.view == View::Buttons) {
        const char* groups[] = {"Hunting", "Bows and bowguns", "Menus and the rest"};
        for (int g = 0; g < 3 && !s_combos_alone; g++) {
            header(r, std::string("tb.head.") + std::to_string(g), tr(groups[g]));
            for (int i = 0; i < TB; i++) {
                if (TOUCH_BUTTONS[i].group != g) continue;
                Row row;
                bool on = s.draft.show[i] != 0;
                row.id = "tb." + std::to_string(i), row.title = tr(TOUCH_BUTTONS[i].title);
                row.key = std::string("show_") + TOUCH_BUTTONS[i].key;
                // (1.0.2, T18: an emulator-style layout in use - its own button's Hide, at once, the MHGU port's too)
                if (const int emu = touch_emu_shown(TOUCH_BUTTONS[i].key); emu >= 0) {
                    on = emu != 0, row.key.clear();
                    const std::string key = TOUCH_BUTTONS[i].key;
                    row.adjust = [key](int d) { touch_emu_show(key, d > 0); };
                    row.activate = [key, on]() { touch_emu_show(key, !on); };
                    row.help = tr("With an emulator-style layout in use, this shows or hides its own button, as Hide does in its editor.");
                } else {
                    row.adjust = [i](int d) { s.draft.show[i] = d > 0; };
                    row.activate = [i]() { s.draft.show[i] = !s.draft.show[i]; };
                }
                row.value = tr(on ? "On" : "Off");
                row.toggle = on;
                r.push_back(std::move(row));
            }
        }
        header(r, "tc.head", tr("Combined buttons"));
        for (int k = 0; k < combos_listed(); k++) {
            char title[48];
            snprintf(title, sizeof title, tr("Button %d"), k + 1);
            action(r, "tc." + std::to_string(k), title, [k]() {
                s_combo = k;
                push(View::ComboEdit, "ce.0");
            });
            r.back().value = s.draft.combo[k].empty() ? tr("Empty") : s.draft.combo[k];
            r.back().help = s_combos_alone ? tr("A button of your own presses several of the game's together (A+X, say). Give it a controller's button here.")
                                           : tr("A button of your own presses several of the game's together (A+X, say). Place it with Customize controls and HUD.");
        }
        action(r, "tb.back", tr("Back"), []() { pop(); });
        r.back().arrow = false;
        return r;
    }
    if (s.view == View::ComboEdit) {
        static const char* const NAMES[] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR", "START", "SELECT", "UP", "DOWN", "LEFT", "RIGHT"};
        static const char* const TITLES[] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR", "START", "SELECT", "D-pad up", "D-pad down",
                                             "D-pad left", "D-pad right"};
        const int k = s_combo;
        // the buttons in the text, in this order ("A+X")
        auto has = [k](int n) {
            const std::string& t = s.draft.combo[k];
            size_t at = 0;
            while (at <= t.size()) {
                size_t plus = t.find('+', at);
                if (t.compare(at, plus == std::string::npos ? std::string::npos : plus - at, NAMES[n]) == 0) return true;
                if (plus == std::string::npos) break;
                at = plus + 1;
            }
            return false;
        };
        auto set = [k, has](int n, bool on) {
            std::string t;
            for (int m = 0; m < (int)std::size(NAMES); m++)
                if (m == n ? on : has(m)) t += (t.empty() ? "" : "+") + std::string(NAMES[m]);
            s.draft.combo[k] = t;
        };
        for (int n = 0; n < (int)std::size(NAMES); n++) {
            Row row;
            bool on = has(n);
            row.id = "ce." + std::to_string(n), row.title = tr(TITLES[n]);
            row.value = tr(on ? "On" : "Off");
            row.toggle = on;
            row.adjust = [n, set](int d) { set(n, d > 0); };
            row.activate = [n, set, has]() { set(n, !has(n)); };
            r.push_back(std::move(row));
        }
        // (1.0.2: any button of a controller, kComboPadKeys' order; a raw one the controller's map has no name for, found
        // with Press a controller button, at the list's end while it is this one's. One combined button per button: one
        // chosen here is taken from another that had it)
        std::vector<std::string> names = combo_pad_names();
        std::vector<int> values;
        for (int i = 0; i < (int)names.size(); i++) values.push_back(i);
        const int now = s.draft.combo_pad[k];
        if (now >= kComboPadRaw) names.push_back(combo_pad_name(now)), values.push_back(now);
        const int at = (int)(std::find(values.begin(), values.end(), now) - values.begin());
        add_choice(r, "ce.pad", "Controller button", at < (int)values.size() ? at : 0, names,
                   [k, values](int i) { combo_pad_take(k, values[std::clamp(i, 0, (int)values.size() - 1)]); });
        r.back().help = now ? fmt("That controller button presses these buttons instead of its own (%s). Only one combined button can have it.",
                                  combo_pad_name(now).c_str())
                            : tr("That button of a controller does the same, and no longer does what it did.");
        action(r, "ce.press", tr("Press a controller button"), [k]() {
            s_pad_capture_combo = k, s_pad_capture_ms = SDL_GetTicks();
            controls_take_pad();  // (nothing pressed before counts)
            push(View::PadCapture, "dialog.cancel");
        });
        r.back().help = tr("Press the controller button you want for it, even one the list doesn't name (a paddle, say).");
        action(r, "ce.clear", tr("Clear"), [k]() {
            s.draft.combo[k].clear();
            s.draft.combo_pad[k] = 0;
        });
        action(r, "ce.back", tr("Back"), []() { pop(); });
        r.back().arrow = false;
        return r;
    }
    if (chat_typing_lines()) {  // (the chat window's: Send, the keys, and the chat's lines - settings_chat.inc)
        chat_typing_rows(r);
        return r;
    }
    if (s.view == View::Keyboard) {
        keyboard_rows(r);
        return r;
    }
    if (s.view == View::Keybinds) {
        keybind_rows(r);
        return r;
    }
    combos_follow_profile();
    if (s.view == View::KeyCapture) key_capture();  // (a key pressed: it is chosen, and the view goes back)
    if (s.view == View::PadCapture) pad_capture();  // (a controller's button pressed, or the time out: back)
    if (s.view == View::Chat) {
        chat_page_rows(r);
        return r;
    }
    if (s.view == View::Friends) {
        friends_page_rows(r);
        return r;
    }
    action(r, "dialog.cancel", tr("Cancel"), []() {
        s.closing = false;  // (the menu stays: no layout editor after all)
        pop();
    });
    if (s.view == View::Reset) action(r, "dialog.reset", tr("Reset"), []() { reset_page(); });
    if (s.view == View::KeyCapture)
        action(r, "dialog.none", tr("None"), []() {
            for (int& k : s.draft.binds.keys[s_capture_bind]) k = 0;
            pop();
        });
    if (s.view == View::OpenSite)  // (back to where the button was, then the browser: a notice there without one)
        action(r, "dialog.open", tr("Open"), []() {
            pop();
            open_official_page();
        });
    if (s.view == View::OpenDiscord)
        action(r, "dialog.open", tr("Open"), []() {
            pop();
            open_discord();
        });
    if (s.view == View::ChatOn)  // (the online chat's relays, the first time: settings_chat.inc)
        action(r, "dialog.chat", cs("Turn it on", "Activarlo"), []() {
            chat::set_notice_seen();
            chat::set_enabled(true);
            pop();
        });
    if (s.view == View::RemoveMod)
        action(r, "dialog.remove", tr("Remove"), []() { remove_mod(); });
    if (s.view == View::RemoveCheat)
        action(r, "dialog.remove", tr("Remove"), []() { remove_cheat(); });
    if (s.view == View::RemoveDriver)
        action(r, "dialog.remove", tr("Remove"), []() {
            if (!gpu_driver_remove(s.remove_folder)) {
                s.error = tr("Could not remove the driver.");
                return;
            }
            for (Draft* d : {&s.draft, &s.saved})  // (its choice went with it: the system's is chosen)
                if (d->driver == s.remove_folder) d->driver.clear();
            s.driver_note = fmt("Removed: %s.", s.remove_name.c_str());
            s.driver_note_bad = false;
            pop();
            focus(gpu_driver_list().empty() ? "driver.import" : "driver.remove");  // (no driver left: Remove is off)
        });
    if (s.view == View::CloseChanges) {
        action(r, "dialog.save", tr("Save and close"), [open]() {
            if (save()) close_menu(open);
        });
        action(r, "dialog.discard", tr("Discard and close"), [open]() {
            s.draft = s.saved;
            close_menu(open);
        });
    }
    if (s.view == View::Restart) {
        if (dirty())
            action(r, "dialog.restart", tr("Save and restart"), []() {
                if (save()) restart_app();
            });
        else
            action(r, "dialog.restart", tr("Restart"), []() { restart_app(); });
    }
    if (s.view == View::Exit) {  // (as MHGU's: the unsaved options saved first, if wanted)
        if (dirty())
            action(r, "dialog.exit", tr("Save and exit"), []() {
                if (save()) quit_app();
            });
        action(r, dirty() ? "dialog.exit.discard" : "dialog.exit", tr(dirty() ? "Exit without saving" : "Exit game"), []() { quit_app(); });
    }
    return r;
}
std::vector<Row> dropdown_rows() {
    std::vector<Row> r;
    for (int i = 0; i < (int)s.dropdown.names.size(); i++) {
        action(r, "choice." + std::to_string(i), s.dropdown.names[i], [i]() {
            auto set = s.dropdown.set;
            close_dropdown();
            set(i);
        });
        r.back().current = i == s.dropdown.selected;
        r.back().arrow = false;
        if (i < (int)s.dropdown.removable.size() && s.dropdown.removable[i] && s.dropdown.remove)
            r.back().remove = [i]() {
                auto remove = s.dropdown.remove;
                remove(i);
            };
    }
    return r;
}

// ---------------------------------------------------------------- the explanations
// Each option's, by its key, in the phone's words (MHGU's PHONE_HELP)
struct Help {
    const char *key, *en, *es;
};
const Help HELP[] = {
    {"LowEnd.off", "Off: the game as the options below make it, at the resolution and frame rate you chose. On trades looks for frames on a weak phone; Toaster trades them for battery and a cool phone.",
     "No: el juego como lo dejan las opciones de abajo, con la resolución y los cuadros que elegiste. Sí cambia aspecto por cuadros en un teléfono débil; Tostadora, por batería y un teléfono fresco."},
    {"LowEnd.on", "On: frames before looks, for a weak phone. 60 FPS (30 on a phone without a fast core), the 3D picture at a third of your screen's resolution (the 3DS's own on a 720-line screen), and no screen filters, anti-aliasing or shadows. The picture is softer and plainer; a Moto g06 runs twice the frames of 30. You can still change each of them afterwards.",
     "Sí: los cuadros antes que el aspecto, para un teléfono débil. 60 FPS (30 en un teléfono sin un núcleo rápido), la imagen 3D a un tercio de la resolución de tu pantalla (la de la 3DS en una de 720 líneas), y sin filtros de pantalla, antialiasing ni sombras. La imagen se ve más suave y simple; un Moto g06 da el doble de cuadros que a 30. Después puedes cambiar cada uno."},
    {"LowEnd.toaster", "Toaster: the battery and the phone's heat before everything. A steady 30 FPS, the 3DS's own rate (half the phone's work of 60), the 3DS's own resolution, no screen filters, anti-aliasing or shadows, and no reverb. The plainest picture, for long hunts on one charge. You can still change each of them afterwards.",
     "Tostadora: la batería y el calor del teléfono antes que todo. 30 FPS parejos, los de la propia 3DS (la mitad del trabajo del teléfono que a 60), la resolución de la 3DS, sin filtros de pantalla, antialiasing ni sombras, y sin reverberación. La imagen más simple, para cacerías largas con una sola carga. Después puedes cambiar cada uno."},
    {"LowEnd.brick", "Brick: below Toaster, for the weakest phones. Everything Toaster does (a steady 30 FPS, the 3DS's own resolution, no screen filters, anti-aliasing, shadows or reverb), and the 3D world's textures at a sixteenth of their size, drawn as big sharp blocks, with the top screen's pixels sharp: less for the phone to read each frame. It looks blocky, a little like the PS1, but nothing is added for the look (that is the PS1 filter). You can still change each of Toaster's settings afterwards.",
     "Ladrillo: debajo de Tostadora, para los teléfonos más débiles. Todo lo de Tostadora (30 FPS parejos, la resolución de la 3DS, sin filtros de pantalla, antialiasing, sombras ni reverberación), y las texturas del mundo 3D a la dieciseisava parte de su tamaño, dibujadas como bloques grandes y nítidos, con los píxeles de la pantalla de arriba nítidos: menos para leer en cada cuadro. Se ve pixelado, un poco como la PS1, pero no se agrega nada por el aspecto (eso es el Filtro PS1). Después puedes cambiar cada ajuste de Tostadora."},
    {"steady", "When the phone cannot hold the frame rate limit, the game runs at an even half of it (a third, ...) instead of swinging between the two: 60 steady rather than 70 to 110 on a 120 FPS limit. It tries the full rate again now and then, and keeps it when the phone holds it. Smoother to the eye, fewer frames.",
     "Cuando el teléfono no llega al límite de FPS, el juego va a la mitad justa (un tercio, ...) en vez de ir y venir entre los dos: 60 parejos en lugar de 70 a 110 con un límite de 120. Cada tanto vuelve a probar el ritmo completo y se queda en él si el teléfono lo sostiene. Más suave a la vista, menos cuadros."},
    {"fps", "The most frames a second, 15 to 240: left and right change it by one, a tap offers the usual ones. 60 is smoother; 30 is the 3DS's, heats the phone less and saves battery. Above 60 for fast phones with a screen that fast: the game keeps its speed, only more frames are drawn. 15, 20 and 25 for a phone that cannot hold 30, or for the longest battery: the game still keeps its speed, with fewer frames. 15 and 20 are shown as evenly as 30; 25 does not divide a 60 Hz screen, so its frames last two and three refreshes in turn (less even than 20 or 30).",
     "El máximo de cuadros por segundo, de 15 a 240: izquierda y derecha lo cambian de a uno, un toque ofrece los habituales. 60 se ve más fluido; 30 es el de la 3DS, calienta menos el teléfono y gasta menos batería. Más de 60, para teléfonos rápidos con una pantalla así de rápida: el juego mantiene su velocidad, solo se dibujan más cuadros. 15, 20 y 25, para un teléfono que no llega a 30 o para que la batería dure lo más posible: el juego sigue a su velocidad, con menos cuadros. 15 y 20 se muestran tan parejos como 30; 25 no divide una pantalla de 60 Hz, así que sus cuadros duran dos y tres refrescos alternados (menos parejo que 20 o 30)."},
    {"antialiasing", "The game's own anti-aliasing, which smooths the edges. Off saves its pass: at a high 3D resolution the edges stay fine, and a weak phone gains frames.",
     "El antialiasing del propio juego, que suaviza los bordes. Apagado se ahorra su pasada: con mucha resolución 3D los bordes siguen finos, y un teléfono débil gana cuadros."},
    {"HdPackSize", "Full: each texture of the pack as it comes, the pack's own look. Half: half its width and height, a quarter of the memory, for a phone that runs short of it with a whole pack (only textures four times the game's or more, so they stay at least twice as sharp as the game's own). It applies at the next start.",
     "Completo: cada textura del pack como viene, el aspecto propio del pack. Mitad: la mitad de ancho y de alto, un cuarto de la memoria, para un teléfono al que no le alcanza con un pack entero (solo las texturas de cuatro veces la del juego o más, así siguen siendo al menos el doble de nítidas que las del juego). Se aplica al volver a abrir el juego."},
    {"FrameGen", "Doubles the frames you see: a picture made between each two of the game's, so 30 FPS look like 60. It works only when the FPS limit is half the screen's refresh rate or less (30 FPS on a 60 Hz screen) and not in the game's menus. It adds a little input delay (about one refresh of the screen), fast movement can show artifacts (smears at the edges of what moves), and it costs GPU work: if the game falls below its FPS limit with it for a few seconds (a weak phone), it pauses itself. The HUD is never warped. Off by default.",
     "Duplica los cuadros que ves: una imagen hecha entre cada dos del juego, así 30 FPS se ven como 60. Funciona solo cuando el límite de FPS es la mitad de la frecuencia de la pantalla o menos (30 FPS en una pantalla de 60 Hz) y fuera de los menús del juego. Agrega un poco de retraso a los controles (más o menos un refresco de la pantalla), en los movimientos rápidos pueden verse defectos (manchas en los bordes de lo que se mueve) y le cuesta trabajo a la GPU: si con ella el juego baja de su límite de FPS unos segundos (un teléfono débil), se pausa sola. El HUD nunca se deforma. Apagado por defecto."},
    {"Shadows", "The shadows of the hunters, the monsters and the scenery. Off: none are drawn, faster on a weak phone. It applies at once.",
     "Las sombras de los cazadores, los monstruos y el escenario. Apagado: no se dibujan, más rápido en un teléfono débil. Se aplica al momento."},
    // (1.0's S22 and S28: why the HUD softens with it, and why there is no 55 % or exact size as in MHGU: the game draws
    // the HUD into the 3D picture, and the renderer scales the 3DS's screen by whole times, render_scale_times)
    {"RenderScale", "The resolution the 3D picture is drawn at: 100 % is your phone's own, as sharp as its screen shows; less is lighter on the phone. In brackets, the times the 3DS's (400x240) it comes to. 3DS: the console's own, for the weakest phones. It can only be a whole number of times the 3DS's screen (1x, 2x, 3x...), so each percent comes to the nearest of them: a 55 % or an exact size can't be drawn as such. The game draws its HUD and text into the same picture, so they are as sharp as this resolution.",
     "La resolución con que se dibuja la imagen 3D: 100 % es la del teléfono, tan nítida como la muestra su pantalla; menos le exige menos al teléfono. Entre paréntesis, cuántas veces la de la 3DS (400x240) resulta. 3DS: la de la consola, para los teléfonos más débiles. Solo puede ser un número entero de veces la pantalla de la 3DS (1x, 2x, 3x...), así que cada porcentaje se lleva al más cercano: un 55 % o un tamaño exacto no se pueden dibujar como tales. El juego dibuja su HUD y sus textos en la misma imagen, así que son tan nítidos como esta resolución."},
    {"RenderScale.adreno", "With this phone's Adreno driver it is drawn at 3x at least while the Adreno fix is on Auto.",
     "Con el driver Adreno de este teléfono se dibuja al menos a 3x mientras la corrección de Adreno está en Automático."},
    {"RenderScale.limit", "This phone's GPU can't make pictures that large: it is drawn at the most it can, after the dot.",
     "La GPU de este teléfono no puede hacer imágenes tan grandes: se dibuja con la mayor que puede, después del punto."},
    {"AdrenoFix", "Some old Adreno drivers draw coloured flashes over the 3D picture at a low 3D resolution: Auto draws it at 3x at least there. If your phone shows no flashes, or for more speed, choose Off.",
     "Algunos drivers viejos de Adreno dibujan destellos de colores sobre la imagen 3D con una resolución 3D baja: Automático la dibuja al menos a 3x en ese caso. Si tu teléfono no los muestra, o para ganar velocidad, elige No."},
    {"widescreen", "Match the screen: the top screen as wide as your phone's, with nothing left over. 16:9: that shape, with bars on a wider screen. Off: the 3DS's own shape.",
     "Igual a la pantalla: la pantalla superior tan ancha como la del teléfono, sin bordes. 16:9: esa forma, con barras en una pantalla más ancha. No: la forma de la 3DS."},
    {"single_screen", "The menus of the 3DS's touch screen drawn on the top screen, by the game, in every language: the title menu, quest lists and boards, the canteen, shops, Palico menus, help pages and yes / no questions. The touch screen starts hidden (its button brings it back) and keeps what needs a finger: the town's map, the World Map, the keyboard and the character creation. Off while a code mod is on (jM5557's single-screen mod, Heavenfall): they move the same menus themselves. Based on Heavenfall's single-screen mode, by popzol and BlitzWOLF. It applies at once. To see the map on the main screen, take the Map off your panels in Customize Panels (quest menu): the game draws it there.",
     "Los menús de la pantalla táctil de la 3DS dibujados en la pantalla superior, por el juego, en todos los idiomas: el menú de título, las listas y tablones de misiones, la cantina, las tiendas, los menús de los Felyne, las páginas de ayuda y las preguntas de sí o no. La pantalla táctil empieza oculta (su botón la vuelve a mostrar) y conserva lo que se usa con el dedo: el mapa del pueblo, el mapa del mundo, el teclado y la creación del personaje. Apagado mientras haya un mod de código activo (el mod de una pantalla de jM5557, Heavenfall): mueven esos menús por su cuenta. Basado en el modo de una pantalla de Heavenfall, de popzol y BlitzWOLF. Se aplica al momento. Para ver el mapa en la pantalla principal, quita el Mapa de tus paneles en Personalizar paneles (menú de misión): el juego lo dibuja ahí."},
    {"hud_size", "The size of the game's HUD on the main screen, 60 to 100 %: every part goes smaller toward its corner (the clock, health, stamina, sharpness and names at the top left, the map and the target camera at the top right, the items at the bottom right), with the talk and yes / no windows of the quests and the rest of the game's HUD. It applies at once. Each part can also be made smaller, larger or hidden in Customize controls and HUD.",
     "El tamaño del HUD del juego en la pantalla principal, de 60 a 100 %: cada parte se achica hacia su esquina (el reloj, la vida, el aguante, el filo y los nombres arriba a la izquierda, el mapa y la cámara de objetivo arriba a la derecha, los objetos abajo a la derecha), junto con las ventanas de diálogo y de sí o no de las misiones y el resto del HUD del juego. Se aplica al momento. Cada parte también se puede achicar, agrandar u ocultar en Personalizar controles y HUD."},
    {"hud_edges", "On a wide picture the hunting HUD's sides go to the screen's edges: health, stamina and sharpness at the left, the items at the right. Off: it stays in the middle.",
     "Con la imagen ancha, los lados del HUD de caza van a los bordes de la pantalla: vida, aguante y filo a la izquierda, los objetos a la derecha. No: queda en el centro."},
    {"bloom", "The glow over the bright parts of the picture. Off: sharper, with more contrast, and a little faster.",
     "El resplandor sobre las partes brillantes de la imagen. Apagado: más nítida, con más contraste, y un poco más rápido."},
    {"motion_blur", "The blending of earlier frames that some effects add.", "La mezcla con cuadros anteriores que agregan algunos efectos."},
    {"depth_of_field", "The focus blur of cutscenes. For now they stay sharp with it: the original blurs what is far from the camera's focus, and that part isn't drawn yet. Off skips its work, a little faster.",
     "El desenfoque de las escenas. Por ahora quedan nítidas con él: el original desenfoca lo que está lejos del foco de la cámara, y esa parte aún no se dibuja. Apagado se ahorra su trabajo, un poco más rápido."},
    {"radial_blur", "The zoom blur around a roaring monster, and in the cutscenes that use it.",
     "El desenfoque radial alrededor de un monstruo que ruge, y en las escenas que lo usan."},
    {"haze", "The heat shimmer of hot areas.", "La reverberación del calor en las zonas calientes."},
    {"dither", "The game's dither pattern, which hides banding on the 3DS's screens. Above 1x it shows as a fine grid over everything: turn it off.",
     "El patrón de tramado del juego, que disimula las bandas en las pantallas de la 3DS. Por encima de 1x se ve como una grilla fina sobre todo: apágalo."},
    {"Volume", "The game's volume. It applies at once.", "El volumen del juego. Se aplica al momento."},
    {"Reverb", "The echo of caves and rooms. Off: the sound is dry. It applies at once.",
     "El eco de las cuevas y las salas. Apagado: el sonido queda seco. Se aplica al momento."},
    {"ScreenRecording", "The phone's screen recorder records the game's sound too. The sound goes through the phone's usual "
                        "path instead of its fastest one, so it may come a little later. From the next start.",
     "La grabación de pantalla del teléfono graba también el sonido del juego. El sonido va por el camino de siempre del "
     "teléfono en vez del más rápido, así que puede llegar un poco después. Desde el próximo inicio."},
    {"Driver", "At your own risk: a Vulkan driver of your own for this phone's GPU (Turnip for an Adreno, PanVK for a Mali...), from the next start. System is the phone's own. A driver that fails at the start is given up by itself, and the system's is used. Reports of a game with a driver of its own are not looked into: try it with System first.",
     "Bajo tu responsabilidad: un driver Vulkan propio para la GPU de este teléfono (Turnip para una Adreno, PanVK para una Mali...), desde el próximo inicio. Sistema es el del teléfono. Un driver que falla al arrancar se deja solo, y se usa el del sistema. Los reportes con un driver propio no se revisan: prueba antes con Sistema."},
    {"DriverImport", "Adds a driver from a .zip with meta.json and its library, the format of the emulators' driver packages. It is copied into the app: the .zip can be deleted afterwards.",
     "Agrega un driver desde un .zip con meta.json y su librería, el formato de los paquetes de drivers de los emuladores. Se copia dentro de la app: después puedes borrar el .zip."},
    {"DriverRemove", "Deletes an installed driver. If it was the one chosen, the system's is chosen instead.",
     "Borra un driver instalado. Si era el elegido, se elige el del sistema."},
    {"FpsCounter", "Shows the frame rate and the phone's temperature at the top of the screen.",
     "Muestra los FPS y la temperatura del teléfono arriba de la pantalla."},
    {"TouchScreen", "The 3DS's touch screen: the map, the item shortcuts and the game's menus. Its own button on the screen shows and hides it too.",
     "La pantalla táctil de la 3DS: el mapa, los atajos de objetos y los menús del juego. Su botón en la pantalla también la muestra y la oculta."},
    {"Layout", "In a corner: over the picture, which fills the screen. Stacked: under the top screen, as on a 3DS, for a 4:3 screen. Side by side: beside it, for a wide screen. On the second screen: on the device's second display, for devices with two screens, and you touch it there; it is offered only while there is one. If that display goes away, the layout you had before comes back.",
     "En una esquina: sobre la imagen, que llena la pantalla. Apiladas: debajo de la pantalla superior, como en la 3DS, para una pantalla 4:3. Lado a lado: al costado, para una pantalla ancha. En la segunda pantalla: en la segunda pantalla del aparato, para los que tienen dos, y se toca ahí; aparece solo cuando hay una. Si esa pantalla se va, vuelve la disposición que tenías antes."},
    {"Corner", "Where the touch screen goes over the picture. The item bar is at the bottom right of the top screen, unless you moved it (Customize controls and HUD).",
     "Dónde va la pantalla táctil sobre la imagen. La barra de objetos está abajo a la derecha de la pantalla superior, salvo que la hayas movido (Personalizar controles y HUD)."},
    {"Size", "Its height, in percent of the screen's.", "Su altura, en porcentaje de la de la pantalla."},
    {"Opacity", "How solid the touch screen looks over the picture.", "Qué tan sólida se ve la pantalla táctil sobre la imagen."},
    {"fov", "How much of the scene you see: higher shows more around the hunter, lower less. 1.00 is the 3DS's. It applies at once.",
     "Cuánto se ve de la escena: un valor mayor muestra más alrededor del cazador, uno menor menos. 1,00 es el de la 3DS. Se aplica al momento."},
    {"controls", "Auto: they show while no controller is connected. Never: only the settings button stays.",
     "Automático: se ven mientras no haya un mando conectado. Nunca: solo queda el botón de opciones."},
    {"opacity", "How solid the touch controls look.", "Qué tan sólidos se ven los controles táctiles."},
    {"camera_speed", "How far the camera turns for a drag.", "Cuánto gira la cámara al arrastrar."},
    {"camera_smooth", "The camera follows your drag over a moment instead of at once, and glides to a stop after your finger does: a steadier turn. Higher is smoother, and a little slower to answer. Off: as before.",
     "La cámara sigue tu arrastre en un momento en vez de al instante, y frena suave después de tu dedo: un giro más parejo. Más alto es más suave, y responde un poco más tarde. No: como antes."},
    {"attack_style", "Pill: A on its upper half, X on the lower, both on the seam. Two buttons: A and X apart. Corner band: the attacks and the guard round the screen's corner.",
     "Píldora: A en su mitad de arriba, X en la de abajo, las dos en la unión. Dos botones: A y X separados. Banda en la esquina: los ataques y la guardia alrededor de la esquina."},
    {"editor", "Move and resize each control: drag one, pinch it or use - and + for its size, then Save. Holding the settings button opens it too.",
     "Mueve y cambia el tamaño de cada control: arrástralo, pellízcalo o usa - y + para su tamaño, y luego Guardar. Mantener apretado el botón de opciones también lo abre."},
    {"Haptics", "A short vibration when you press a touch control.", "Una vibración corta al tocar un control."},
    {"LongPressHide", "Hold your finger still on an empty part of the screen (no button there) for a moment: every touch control hides, for a cutscene or a picture. Another long press shows them again. A drag of the camera, a hold on a button or two fingers never hide them.",
     "Mantén el dedo quieto un momento en una parte vacía de la pantalla (sin botones): se ocultan todos los controles táctiles, para una escena o una captura. Otra pulsación larga los vuelve a mostrar. Arrastrar la cámara, mantener un botón o usar dos dedos nunca los oculta."},
    {"GyroAim", "Aiming a bow or a bowgun (a gun's aim, its scope, crouching fire; a bow's aiming mode), turning the phone turns the aim too, from where it was as the aim began, with what your fingers or the sticks do. A controller with a gyro is read instead of the phone. The gyroscope is read only while you aim.",
     "Al apuntar con un arco o una ballesta (la mira de la ballesta, su visor, el disparo agachado; el modo de apuntar del arco), girar el teléfono también mueve la mira, desde donde estaba al empezar a apuntar, junto con lo que hagan tus dedos o las palancas. Si el mando tiene giroscopio, se usa el del mando en vez del teléfono. El giroscopio solo se lee mientras apuntas."},
    {"GyroSensitivity", "How far the aim turns for a turn of the phone or the controller: 1.0x the same angle.",
     "Cuánto gira la mira por cada giro del teléfono o del mando: 1.0x, el mismo ángulo."},
    {"FaceButtons", "How A, B, X and Y are read on a controller. Position: as on the 3DS, A is the right button and B the bottom one. Label: by the letter printed on each button (on a PlayStation controller, Cross is A). A controller you mapped is read as you mapped it.",
     "Cómo se leen A, B, X e Y en un mando. Posición: como en la 3DS, A es el botón derecho y B el de abajo. Etiqueta: por la letra impresa en cada botón (en un mando de PlayStation, la X es A). Un mando que mapeaste se lee como lo mapeaste."},
    {"RightStick", "Camera: the right stick turns the camera smoothly, as far as you tilt it, without the game's Circle Pad Pro option. C-stick: it is the New 3DS's C-stick, which the game reads only with that option on and turns in steps.",
     "Cámara: la palanca derecha gira la cámara con suavidad, tanto como la inclines, sin la opción Circle Pad Pro del juego. Palanca C: es la palanca C de la New 3DS, que el juego solo lee con esa opción activada y gira a saltos."},
    {"StickSpeed", "How fast the right stick turns the camera at full tilt. A small tilt turns it slowly, for aiming.",
     "Qué tan rápido gira la cámara la palanca derecha a fondo. Una inclinación leve la gira despacio, para apuntar."},
    {"InvertX", "The camera turns the other way when the right stick goes left or right.",
     "La cámara gira al revés al mover la palanca derecha a la izquierda o a la derecha."},
    {"InvertY", "The camera turns the other way when the right stick goes up or down.",
     "La cámara gira al revés al mover la palanca derecha arriba o abajo."},
    {"R3", "What clicking the right stick does. Lock-on: the camera keeps turning to the monster (with Lock-on on, above). Camera (L): the camera behind the hunter, or to the monster with the target camera on. Target (ZL): the port's target button (Gameplay tab). Kick (ZR): the kick.",
     "Qué hace apretar la palanca derecha. Fijar objetivo: la cámara sigue girando hacia el monstruo (con Fijar objetivo activado, arriba). Cámara (L): la cámara detrás del cazador, o hacia el monstruo con la cámara de objetivo activada. Objetivo (ZL): el botón de objetivo del port (pestaña Juego). Patada (ZR): la patada."},
    {"TownMenu", "The controller's button for the town's actions that only the touch screen had: Guild Cards, Gestures, Friends and Hunters for Hire. In a town, while the hunter walks, it opens their list (the D-pad or the stick moves, A chooses, B closes). SELECT's own use in a town (the touch screen's Customize Panels) is left out while it is SELECT.",
     "El botón del mando para las acciones del pueblo que solo tenía la pantalla táctil: Tarjetas del Gremio, Gestos, Amigos y Cazadores de alquiler. En un pueblo, mientras el cazador camina, abre su lista (la cruceta o la palanca mueven, A elige, B cierra). Mientras sea SELECT, su uso propio en el pueblo (Personalizar paneles de la pantalla táctil) queda fuera."},
    {"Deadzone", "How far a stick has to move before it counts. More if your controller's sticks drift; less to feel the smallest movement.",
     "Cuánto tiene que moverse una palanca para que cuente. Más si las palancas de tu mando se mueven solas; menos para sentir el movimiento más pequeño."},
    {"PadMap", "For a controller the game does not read well: press each button once and it works from then on. The map is kept for that controller.",
     "Para un mando que el juego no lee bien: aprieta cada botón una vez y queda funcionando. El mapeo se guarda para ese mando."},
    {"LockOn", "Tap the Lock-on button on the screen (or click the right stick of a controller) and the camera keeps turning to the monster, the one the Target Cam chose or the nearest large one, until you tap it again. The target button changes the monster. Turning the camera yourself pauses it for a moment.",
     "Toca el botón Fijar objetivo en la pantalla (o aprieta la palanca derecha de un mando) y la cámara sigue girando hacia el monstruo, el que eligió la cámara de objetivo o el grande más cercano, hasta que lo vuelvas a tocar. El botón de objetivo cambia el monstruo. Girar la cámara tú mismo la pausa un momento."},
    {"show_map_button", "A button of the port's that shows or hides the map the game draws on the main screen (the HUD's Area map), in quests. The game draws it there when the Map isn't among your touch screen panels (Customize Panels, quest menu). Customize controls and HUD places it.",
     "Un botón del port que muestra u oculta el mapa que el juego dibuja en la pantalla principal (el Mapa de la zona del HUD), en las misiones. El juego lo dibuja ahí cuando el Mapa no está entre los paneles de tu pantalla táctil (Personalizar paneles, menú de misión). Personalizar controles y HUD lo ubica."},
    {"show_lock_on", "The port's own button, not one of the game's: a tap locks the camera on the monster and it keeps turning to it; another tap lets go. The target button changes the monster. It lights up while the lock-on is on, and shows while Lock-on is on in Controls.",
     "Un botón propio del port, no del juego: un toque fija la cámara en el monstruo y la sigue girando hacia él; otro toque la suelta. El botón de objetivo cambia el monstruo. Se ilumina mientras está fijado, y aparece mientras Fijar objetivo está activado en Controles."},
    {"show_weapon_tree", "The port's own button, in the Smithy while it shows a weapon (Forge, Upgrade): a tap opens that weapon's tree, with what you have and wear, the weapons before it and the next ones as ?. The right stick's click opens it too. Move it or make it bigger with Customize controls and HUD.",
     "Un botón propio del port, en la herrería mientras muestra un arma (Forjar, Mejorar): un toque abre el árbol de esa arma, con las que tienes y la equipada, las anteriores y las siguientes como ?. El clic de la palanca derecha también lo abre. Muévelo o agrándalo con Personalizar controles y HUD."},  // (1.0.2, the MHGU port's ShowWeaponTree)
    {"show_ranged", "With a bow or a bowgun in hand, in a hunt, the touch controls change to a layout of their own.",
     "Con un arco o una ballesta en la mano, en una cacería, los controles táctiles pasan a un diseño propio."},
    {"show_fire", "A bowgun fires. A bow draws while you hold it and shoots when you let go; drag on it to aim.",
     "La ballesta dispara. El arco se tensa mientras lo mantienes y dispara al soltar; arrastra sobre él para apuntar."},
    {"show_second", "A bowgun reloads. A bow does its melee attack.", "La ballesta recarga. El arco da su golpe cuerpo a cuerpo."},
    {"show_aim", "A bowgun: a tap, the scope; held, the quick aim. A bow: the aiming mode.",
     "Ballesta: un toque, la mira; mantenido, el apuntado rápido. Arco: el modo de apuntado."},
    {"show_ranged_special", "A bowgun's crouching fire. A bow applies its coating.", "El fuego agachado de la ballesta. El arco aplica su capa."},
    {"show_ammo", "Change the ammo or the coating. A swipe up or down on the item bar does it too.",
     "Cambian la munición o la capa. También se puede deslizar hacia arriba o abajo sobre la barra de objetos."},
    {"show_floating_stick", "On: the movement stick comes to where your thumb lands near it, and goes back to its place when you lift it. Off: it stays where it is.",
     "Activado: la palanca de movimiento aparece donde apoyas el pulgar cerca de ella, y vuelve a su lugar al levantarlo. Desactivado: se queda donde está."},
    {"show_stick_run", "On: push the movement stick past its edge to run, as before. Slide it up onto the small circle with a padlock that shows above it, which lights up, and lift your thumb there: the run locks and the hunter keeps running forward (steer with the camera), until you touch the stick or the circle again, draw the weapon or open a menu. Off: the stick only walks, and a Run button shows above the attacks instead: it runs while held.",
     "Activado: empuja la palanca de movimiento más allá de su borde para correr, como siempre. Deslízala hacia arriba hasta el círculo chico con un candado que aparece encima, que se ilumina, y levanta el pulgar ahí: la carrera se fija y el cazador sigue corriendo hacia adelante (giras con la cámara), hasta que vuelvas a tocar la palanca o el círculo, desenvaines o abras un menú. Desactivado: la palanca solo camina, y aparece un botón Correr sobre los ataques: corre mientras lo mantienes."},
    {"show_guard_slide", "With Slide between buttons off: with the weapon out, a thumb that lands on Guard and slides onto the attacks keeps guarding (R held), for the moves that need R and an attack together, like a lance's counter or a shield's thrust. Off: the slide lets go of R. With Slide between buttons on, every slide keeps both.",
     "Con Deslizar entre botones desactivado: con el arma desenvainada, un pulgar que se apoya en Guardia (R) y se desliza a los ataques sigue en guardia (R mantenido), para los movimientos que piden R junto con un ataque, como el contraataque de la lanza o el empujón del escudo. Desactivado: al deslizar se suelta R. Con Deslizar entre botones activado, todo deslizamiento mantiene los dos."},
    {"show_camera_right_half", "On: dragging to turn the camera only works on the right half of the screen, so a missed touch near the stick doesn't move the camera. Off: a drag anywhere no control is turns it.",
     "Activado: arrastrar para girar la cámara solo funciona en la mitad derecha de la pantalla, así un toque errado cerca de la palanca no mueve la cámara. Desactivado: cualquier arrastre donde no hay un control la gira."},
    {"show_slide_both", "On: a finger that slides from one button onto another keeps the first pressed with the second, as two fingers would: R onto A or X for the Insect Glaive's kinsect, R onto B, Guard onto the attacks. Back onto the first, that one alone. Not in the game's menus. Off: as before, the slide lets go of the first.",
     "Activado: un dedo que se desliza de un botón a otro mantiene el primero apretado junto con el segundo, como harían dos dedos: R a A o X para el kinsecto de la glaive insecto, R a B, Guardia a los ataques. Al volver al primero, solo ese. No en los menús del juego. Desactivado: como antes, al deslizar se suelta el primero."},
    {"show_select", "SELECT during a hunt. MH4U's hunts do without it. While it is off, the menu pad has a small SELECT below it.",
     "SELECT durante una cacería. Las cacerías de MH4U no lo necesitan. Mientras está desactivado, el pad de menú tiene un SELECT chico debajo."},
    {"show_dpad", "The D-pad during a hunt. In the game's menus the menu pad takes its place, unless D-pad in menus is on.",
     "La cruceta durante una cacería. En los menús del juego la reemplaza el pad de menú, salvo que Cruceta en los menús esté activado."},
    // (the menus' ways: the MHGU port's ShowMenuLR and DpadInMenus, the same words)
    {"show_dpad_menus", "On: the D-pad stays on the screen in the game's menus too, beside the menu pad, and moves their cursor. Off: the menu pad takes its place while a menu is open.",
     "Activado: la cruceta queda en la pantalla también en los menús del juego, junto al pad de menú, y mueve su cursor. Desactivado: el pad de menú la reemplaza mientras hay un menú abierto."},
    {"show_menu_lr", "The menu pad's L and R tabs above it, which turn the menus' pages. Off: the menu pad without them.",
     "Las pestañas L y R sobre el pad de menú, que pasan las páginas de los menús. Desactivado: el pad de menú sin ellas."},
    {"show_menu_dpad", "While a menu of the game's is open, a small pad under your right thumb moves its cursor, with L and R above it to turn the pages (and a small SELECT below it while SELECT in hunts is off). The movement stick moves the cursor too, in the menus that read it. If the pad covers a combined button, move it with Customize controls and HUD. Off: no pad in the menus; a drag on the screen moves their cursor, and the movement stick in the menus that read it.",
     "Mientras hay un menú del juego abierto, un pad chico bajo el pulgar derecho mueve su cursor, con L y R encima para pasar las páginas (y un SELECT chico debajo mientras SELECT en cacerías está desactivado). La palanca de movimiento también mueve el cursor, en los menús que la leen. Si el pad tapa un botón combinado, muévelo con Personalizar controles y HUD. Desactivado: no hay pad en los menús; arrastrar el dedo por la pantalla mueve su cursor, y la palanca de movimiento en los menús que la leen."},
    {"kick", "A button for the kick, which on a 3DS only the touch screen's panel does. The touch controls' kick button is ZR.",
     "Un botón para la patada, que en la 3DS solo hace el panel de la pantalla táctil. El botón de patada de los controles táctiles es ZR."},
    {"target", "A button for the target camera panel: a press puts the camera on the monster, or goes through the large monsters and then off. L then turns the camera to it.",
     "Un botón para el panel de la cámara de objetivo: una pulsación fija la cámara en el monstruo, o pasa por los monstruos grandes y después se apaga. Luego L gira la cámara hacia él."},
    {"skip", "Once a quest is complete, this button taps the touch screen's Skip, which skips the scene of the monster's end.",
     "Al completar una misión, este botón toca Saltar en la pantalla táctil, que salta la escena del final del monstruo."},
    {"skip_end", "Buttons held together to end the wait before the return, the carving time, as if it had run out. Online it ends once every player with the port has held them.",
     "Botones mantenidos juntos para terminar la espera antes de volver, el tiempo para tallar, como si se hubiera acabado. En línea termina cuando todos los jugadores con el port los mantuvieron."},
    {"skip_end_hold", "How long they are held, so nobody ends the quest by accident.",
     "Cuánto tiempo se mantienen, para que nadie termine la misión sin querer."},
    {"Nickname", "Your name in the room: 4 to 20 letters, digits, spaces, '.', '_' or '-'. Two hunters in one room can't share one.",
     "Tu nombre en la sala: de 4 a 20 letras, números, espacios, '.', '_' o '-'. Dos cazadores de una misma sala no pueden usar el mismo."},
    {"Address", "The room's address: its IP or name, and :port if it is not 24872, as 203.0.113.7:24872. Rooms of Azahar, of its room server and of other phones with this app all work. Empty: none.",
     "La dirección de la sala: su IP o nombre, y :puerto si no es el 24872, como 203.0.113.7:24872. Sirven las salas de Azahar, de su servidor de salas y de otros teléfonos con esta app. Vacío: ninguna."},
    {"Password", "The room's password, if it has one. A room you host on this phone asks for this one too. Empty: none.",
     "La contraseña de la sala, si tiene una. Una sala que crees en este teléfono también pide esta. Vacío: ninguna."},
    {"Join", "Joins the room at the address above, and remembers it. It works while the game runs: join before going to the Gathering Hall, and the game's Local Play finds the room's hunters.",
     "Entra a la sala de la dirección de arriba, y la recuerda. Funciona con el juego en marcha: entra antes de ir al Gathering Hall, y el juego local (Local Play) encuentra a los cazadores de la sala."},
    {"Leave", "Leaves the room. A Gathering Hall or a hunt with its hunters ends for you.",
     "Sale de la sala. Un Gathering Hall o una cacería con sus cazadores se termina para ti."},
    {"HostPort", "The UDP port the room on this phone listens on. On the same Wi-Fi the others only need this phone's address; from the internet, forward this port on your router to this phone.",
     "El puerto UDP en que escucha la sala de este teléfono. En la misma Wi-Fi a los demás les basta la dirección de este teléfono; desde internet, redirige este puerto en tu router hacia este teléfono."},
    {"Slots", "How many hunters the room takes, you included.", "Cuántos cazadores entran en la sala, contándote a ti."},
    {"Host", "Opens a room on this phone and joins it as its moderator; the others join it with this phone's address. It stays open until you close it or close the app.",
     "Abre una sala en este teléfono y entra como su moderador; los demás entran con la dirección de este teléfono. Queda abierta hasta que la cierres o cierres la app."},
    {"CloseRoom", "Closes the room on this phone: everyone in it leaves.", "Cierra la sala de este teléfono: salen todos."},
    {"Say", "Writes a line to everyone in the room.", "Escribe una línea para todos los de la sala."},
    {"Notices", "Who comes and goes in the room shows for a few seconds at the top of the screen while you play, and so does its chat while the room chat is off. With the room chat on, its chat follows Lines over the game, above.",
     "Quién entra y sale de la sala se ve unos segundos arriba de la pantalla mientras juegas, y también su chat mientras el chat de la sala está apagado. Con el chat de la sala activo, su chat sigue a Líneas sobre el juego, arriba."},
    {"Lobby", "The lobby that lists public rooms (Azahar's web API): a tap on a room joins it. The default, http://88.198.47.46:5000, is where most of the game's players list their rooms; it is run by players, not by this project. Custom: type another, as http://name:port. Off: no list (rooms by address and on this Wi-Fi still work). The lobbies you type and save stay in this list, the last one first (up to 24): the × beside one takes it out (with a controller, the button that saves).",
     "El lobby que lista las salas públicas (la API web de Azahar): un toque en una sala entra. El predeterminado, http://88.198.47.46:5000, es donde la mayoría de los jugadores del juego listan sus salas; lo mantienen jugadores, no este proyecto. Otro: escribe otro, como http://nombre:puerto. Ninguno: sin lista (las salas por dirección y en esta Wi-Fi siguen funcionando). Los lobbies que escribes y guardas quedan en esta lista, el último primero (hasta 24): la × a su lado lo quita (con un mando, el botón que guarda)."},
    {"LanSearch", "Looks for rooms hosted by phones on the same Wi-Fi as this one (Host a room, on the other phone), so there is no address to type. It takes a second and a half. A tap on a room found joins it.",
     "Busca salas creadas por teléfonos en la misma Wi-Fi que este (Crear una sala, en el otro teléfono), así no hay que escribir ninguna dirección. Tarda un segundo y medio. Un toque en una sala encontrada entra."},
    {"Refresh", "Asks the lobby above for its rooms again (the page asks it by itself as it opens). A tap on one joins it.", "Le vuelve a pedir sus salas al lobby de arriba (la página se las pide sola al abrirse). Un toque en una entra."},
    {"Language", "The language of this menu. Phone's: the one your phone uses.", "El idioma de este menú. Del teléfono: el que usa tu teléfono."},
    {"Theme", "The colours of this menu.", "Los colores de este menú."},
    {"MenuScale", "The size of this menu, 50 % to 200 %.", "El tamaño de este menú, de 50 % a 200 %."},
    {"PauseInBackground", "The game stops while you are in another app, and goes on where it was when you come back. Off: it keeps running, as a hunt with others online needs; it uses battery meanwhile.",
     "El juego se detiene mientras estás en otra app, y sigue donde estaba al volver. Apagado: sigue corriendo, como lo necesita una cacería en línea con otros; mientras tanto gasta batería."},
    {"PauseInMenu", "The game waits while this menu is open, when you play alone. With others in a room it goes on.",
     "El juego espera mientras este menú está abierto, cuando juegas solo. Con otros en una sala, sigue."},
    {"Updates", "At most once a day the app asks GitHub for the latest version (nothing else is sent) and tells you at the next start when there is a new one. It never downloads or installs anything by itself.",
     "Como mucho una vez al día la app le pregunta a GitHub cuál es la última versión (no se envía nada más) y te avisa al abrir el juego cuando hay una nueva. Nunca descarga ni instala nada por su cuenta."},
    {"savefolder", "Where your save is kept. Choose a folder that a sync app shares with your other devices to carry your hunt between them. Your save is copied there first, and the game restarts to use it. The guild cards, event quests and the Palico go with it.",
     "Dónde se guarda tu partida. Elige una carpeta que una app de sincronización comparta con tus otros dispositivos para llevar tu cacería entre ellos. Antes se copia ahí tu partida, y el juego se reinicia para usarla. Las tarjetas de gremio, las misiones de evento y el Felyne van con ella."},
    {"import", "Brings your save from an emulator or another device. It replaces the one on this device, and the current save is kept in a copy.",
     "Trae tu partida desde un emulador o desde otro dispositivo. Reemplaza la de este dispositivo, y la partida actual queda guardada en una copia."},
    {"export", "Copies your save to a folder you choose, to take it to an emulator or another device.",
     "Copia tu partida a la carpeta que elijas, para llevarla a un emulador o a otro dispositivo."},
    {"folder", "Where the game's files are read from. A new folder is used the next time the game starts.",
     "De dónde se leen los archivos del juego. Una carpeta nueva se usa la próxima vez que abras el juego."},
    {"verify", "Reads every game file and compares it with the version the port needs: says what is missing, cut short, changed or from another version, and what to do. It can take a few minutes; it changes and downloads nothing.",
     "Lee todos los archivos del juego y los compara con la versión que el port necesita: dice qué falta, qué está cortado, qué cambió o es de otra versión, y qué hacer. Puede tardar unos minutos; no cambia ni descarga nada."},
    {"build", "Makes the game's code library on this phone, from your own game and update in the game folder, with the code mod you choose (the single-screen mod, for one). No PC needed. The first time it downloads the tools (about 100 MB). It takes 15 minutes to 2 hours and goes on in the background; the game starts again to use it.",
     "Crea la librería de código del juego en este teléfono, desde tu propio juego y su actualización de la carpeta del juego, con el mod de código que elijas (el de una sola pantalla, por ejemplo). Sin PC. La primera vez descarga las herramientas (unos 100 MB). Tarda de 15 minutos a 2 horas y sigue en segundo plano; el juego se reinicia para usarla."},
    {"log", "Sends the game's log to the app you choose, for whoever is helping you with a problem.",
     "Envía el registro del juego a la app que elijas, para quien te esté ayudando con un problema."},
    {"licenses", "The licenses of the software this app is made with, and this app's own with its additional terms.",
     "Las licencias del software con que está hecha esta app, y la de esta app con sus términos adicionales."},
    {"credits", "Shows again the credits that appear when the app opens.", "Muestra otra vez los créditos que aparecen al abrir la app."},
    {"restart", "Closes the game and opens it again, for the options marked *. Save in the game first.",
     "Cierra el juego y lo abre de nuevo, para las opciones marcadas con *. Guarda antes en el juego."},
    {"exit", "Closes the game and the app. Save your hunt in the game first.", "Cierra el juego y la app. Guarda antes tu cacería en el juego."},
    {"back", "Closes this menu.", "Cierra este menú."},
    {"ModImport", "Adds a mod from its .zip: a mod for MH4U USA, with a romfs folder, an exefs folder or both, or the 3DS emulators' layout with a 0004000000126300 folder. It is copied into the app: the .zip can be deleted afterwards.",
     "Agrega un mod desde su .zip: un mod para MH4U de EE. UU., con una carpeta romfs, una carpeta exefs o las dos, o con la estructura de los emuladores de 3DS, con una carpeta 0004000000126300. Se copia dentro de la app: después puedes borrar el .zip."},
    {"ModFolderImport", "Adds a mod from its folder, unpacked: the folder with romfs and exefs in it, or the one that holds 0004000000126300. It is copied into the app.",
     "Agrega un mod desde su carpeta, ya descomprimida: la que tiene romfs y exefs adentro, o la que contiene 0004000000126300. Se copia dentro de la app."},
    {"CodeLibrary", "A mod with an exefs folder changes the game's code, and this port runs that code translated ahead of time: your code library has to be made with the same code mods turned on (System › Game code › Build the game code, on this phone; or the guide's tool on a PC). If they don't match, the game won't start.",
     "Un mod con carpeta exefs cambia el código del juego, y este port ejecuta ese código traducido de antemano: tu librería de código tiene que hacerse con los mismos mods de código activados (Sistema › Código del juego › Crear el código del juego, en este teléfono; o la herramienta de la guía en una PC). Si no coinciden, el juego no arranca."},
    {"ModConflicts", "Mods that change the same game files: where both are on, the one lower in the list is used. Select it to see each pair and choose which mod is used.",
     "Mods que cambian los mismos archivos del juego: donde los dos están activados, se usa el de más abajo en la lista. Selecciónalo para ver cada par y elegir qué mod se usa."},
    {"ModSuggest", "Orders the mods that change the same files from the biggest to the smallest, so a small tweak wins over a large overhaul. The others keep their places.",
     "Ordena los mods que cambian los mismos archivos del más grande al más chico, para que un retoque pequeño gane sobre una reforma grande. Los demás quedan donde están."},
    {"ModRescan", "Reads the mods folder again: a mod added there joins the end of the list, on; one deleted there leaves it.",
     "Vuelve a leer la carpeta de mods: un mod agregado ahí se suma al final de la lista, activado; uno borrado de ahí sale de ella."},
    {"CheatImport", "Adds the cheats of a .txt file in the format of Azahar and Citra ([Name] and the code lines), for MH4U USA 1.3.0. They come off: turn on the ones you want and save. A cheat with the same name is replaced.",
     "Agrega los trucos de un archivo .txt en el formato de Azahar y Citra ([Nombre] y las líneas de código), para MH4U de EE. UU. 1.3.0. Llegan apagados: activa los que quieras y guarda. Un truco con el mismo nombre se reemplaza."},
    {"CheatRemove", "Deletes a cheat from the cheats file.", "Borra un truco del archivo de trucos."},
    {"qol_hold_gather", "Hold A to carve, gather, mine and catch bugs again and again, as in later Monster Hunter games, until you let go. jM5557's idea.",
     "Mantén A para tallar, recolectar, minar y atrapar insectos una y otra vez, como en los Monster Hunter más nuevos, hasta que lo sueltes. Idea de jM5557."},
    {"qol_flex_cancel", "After a potion or a ration, moving, rolling or any other action ends the flex at once. What you drank still works; standing still, the flex plays whole. jM5557's idea.",
     "Después de una poción o una ración, moverte, rodar o cualquier otra acción corta la pose enseguida. Lo que tomaste sigue haciendo efecto; si te quedas quieto, la pose se ve entera. Idea de jM5557."},
    {"qol_sharpen_cancel", "B while sharpening rolls out of it, as in later Monster Hunter games. jM5557's idea.",
     "B mientras afilas rueda y deja de afilar, como en los Monster Hunter más nuevos. Idea de jM5557."},
    {"qol_rewards_no_limit", "The rewards screen after a quest no longer counts down: take your time choosing. jM5557's idea.",
     "La pantalla de recompensas después de una misión ya no tiene cuenta regresiva: elige con calma. Idea de jM5557."},
    {"reset", "Puts the options of this section (or of this page) back as they come. Nothing changes until you save.",
     "Devuelve las opciones de esta sección (o de esta página) a como vienen. Nada cambia hasta que guardes."},
};
std::string help_of(const char* key) {
    for (const Help& h : HELP)
        if (!strcmp(key, h.key)) return spanish() ? h.es : h.en;
    return "";
}
std::string help_text(const Row& r) {
#ifndef __ANDROID__
    if (!r.help.empty() || r.key.empty()) return pc_words(r.help);
    for (const Help& h : HELP)
        if (r.key == h.key) return pc_words(std::string(spanish() ? h.es : h.en));
#endif
    if (!r.help.empty() || r.key.empty()) return r.help;
    for (const Help& h : HELP)
        if (r.key == h.key) return spanish() ? h.es : h.en;
    if (!r.key.compare(0, 5, "show_"))  // (a touch control without words of its own)
        return spanish() ? "Muestra este botón en la pantalla, o lo oculta. Se ubica con Personalizar controles y HUD."
                         : "Shows this button on the screen, or hides it. Customize controls and HUD places it.";
    return "";
}

// ---------------------------------------------------------------- the controller
bool pressed(ImGuiKey key, bool repeat = false) { return ImGui::IsKeyPressed(key, repeat); }
// The 3DS's A, B, X and Y (0..3) on the controller in use: by their place (A on the right), or where Label puts the
// game's A at the bottom (an Xbox or a PlayStation layout: controls.h)
ImGuiKey face_key(int button) {
    static const ImGuiKey place[4] = {ImGuiKey_GamepadFaceRight, ImGuiKey_GamepadFaceDown, ImGuiKey_GamepadFaceUp,
                                      ImGuiKey_GamepadFaceLeft};
    static const ImGuiKey xbox[4] = {ImGuiKey_GamepadFaceDown, ImGuiKey_GamepadFaceRight, ImGuiKey_GamepadFaceLeft,
                                     ImGuiKey_GamepadFaceUp};
    return (controls_a_is_south() ? xbox : place)[button & 3];
}
bool direction(ImGuiKey a, ImGuiKey b, ImGuiKey c) { return pressed(a, true) || pressed(b, true) || pressed(c, true); }
bool confirm_pressed() {  // (A: the right face button, as the 3DS's)
    return settled() && (pressed(face_key(0)) || pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter));
}
// Up / down move to the next row that can be picked, left / right step its value, A does what a tap does; in the
// picker (list) left / right do nothing
void navigate(std::vector<Row>& rows, bool list) {
    if (rows.empty()) return;
    auto it = std::find_if(rows.begin(), rows.end(), [](const Row& r) { return r.id == s.focus && r.enabled; });
    if (it == rows.end()) {
        it = std::find_if(rows.begin(), rows.end(), [](const Row& r) { return r.enabled; });
        if (it == rows.end()) return;
        focus(it->id);
    }
    int index = (int)(it - rows.begin());
    int dy = direction(ImGuiKey_DownArrow, ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickDown) ? 1
             : direction(ImGuiKey_UpArrow, ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadLStickUp)     ? -1
                                                                                                 : 0;
    int dx = direction(ImGuiKey_RightArrow, ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickRight) ? 1
             : direction(ImGuiKey_LeftArrow, ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadLStickLeft)  ? -1
                                                                                                    : 0;
    if (list) dx = 0;
    if (dx && it->adjust) {
        it->adjust(dx);
        return;
    }
    // (the keyboard: Enter accepts the text, keyboard_keys; A types the key picked. The Enter that accepted it does
    // nothing more: the row it came from would open it again)
    bool keys = s.view == View::Keyboard && !list;
    bool confirm = keys ? settled() && pressed(face_key(0)) : confirm_pressed() && !s.typed_enter;
    if (confirm && it->activate) {
        auto fn = it->activate;
        fn();
        return;
    }
    if (keys && (dx || dy)) {  // (the keys as they lie on the screen, last frame: the nearest that way)
        auto from = std::find_if(s.targets.begin(), s.targets.end(), [](const Target& t) { return t.id == s.focus; });
        if (from == s.targets.end()) return;
        float best = 1e30f;
        std::string next;
        for (const Target& t : s.targets) {
            float x = t.center.x - from->center.x, y = t.center.y - from->center.y;
            if (!t.enabled || (dy && y * dy <= 1) || (dx && (x * dx <= 1 || std::fabs(y) > 1))) continue;
            float score = dy ? std::fabs(y) * 1000 + std::fabs(x) : std::fabs(x);
            if (score < best) best = score, next = t.id;
        }
        if (!next.empty()) focus(next);
        return;
    }
    if (dy) {
        int next = index + dy;
        while (next >= 0 && next < (int)rows.size() && !rows[next].enabled) next += dy;
        if (next >= 0 && next < (int)rows.size()) focus(rows[next].id);
    }
}
// The list scrolls to the row the controller moved to (a few frames: the rows settle after a change of view)
void keep_visible(ImGuiWindow* w, const ImRect& rect) {
    if (s.focus_request) {
        s.focus_scroll_frames = 3;
        s.focus_request = false;
    }
    if (s.focus_scroll_frames > 0) {
        --s.focus_scroll_frames;
        ImGui::ScrollToRect(w, rect, ImGuiScrollFlags_KeepVisibleEdgeY | ImGuiScrollFlags_NoScrollParent);
    }
}
void back_keys(bool* open) {
    bool back = pressed(face_key(1)) || pressed(ImGuiKey_Escape) || pressed(ImGuiKey_AppBack);
    if (request_close.exchange(false)) {
        if (s.view == View::PadMap) padmap_cancel();
        if (s.view != View::CloseChanges) ask_close(open);  // (the question is up already: it stays)
        return;
    }
    if (request_back.exchange(false) || (back && settled())) ask_back(open);
    if (s.dropdown.open && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !s.dropdown.hovered) {
        close_dropdown();  // (a tap away from the picker only closes it)
        s.swallow = true;
    }
}
// The keyboard's keys: a physical keyboard's text, Backspace, Ctrl+V, Enter; a controller's X deletes, Y a space, L / R
// move the cursor, START accepts
void keyboard_keys() {
    ImGuiIO& io = ImGui::GetIO();
    for (ImWchar ch : io.InputQueueCharacters) {
        char utf8[5];
        utf8[ImTextCharToUtf8(utf8, ch)] = 0;
        insert(utf8);
    }
    if (pressed(ImGuiKey_Backspace, true) || pressed(face_key(2), true)) erase_previous();
    if (pressed(face_key(3), true)) insert(" ");
    if (io.KeyCtrl && pressed(ImGuiKey_V)) paste();
    if (pressed(ImGuiKey_GamepadL1, true)) move_cursor(-1);
    if (pressed(ImGuiKey_GamepadR1, true)) move_cursor(1);
    if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter) || pressed(ImGuiKey_GamepadStart)) {
        s.typed_enter = true;
        accept_keyboard();
    }
}
// The picker's remembered choice picked by a controller or the keys: X (the button that saves elsewhere) or Delete
// takes it out, as its x does
bool picker_removable() {
    if (!s.dropdown.open || s.focus.compare(0, 7, "choice.")) return false;
    int i = std::atoi(s.focus.c_str() + 7);
    return i >= 0 && i < (int)s.dropdown.removable.size() && s.dropdown.removable[i] && s.dropdown.remove;
}
void picker_keys(const std::vector<Row>& options) {
    if (!s.dropdown.open || !(pressed(face_key(2)) || pressed(ImGuiKey_Delete))) return;
    for (const Row& o : options)
        if (o.id == s.focus && o.remove) {
            auto fn = o.remove;
            fn();
            return;
        }
}
// X saves (not while a question or the keyboard is up), L / R the tab before / after
void page_keys() {
    if (confirmation() || s.view == View::Keyboard) return;
    if (pressed(face_key(2)) && dirty() && !s.dropdown.open) s.save_request = true;
#ifndef __ANDROID__
    // (the PC's keyboard: Ctrl+S saves; Tab and Shift+Tab, Page Down and Page Up the sections)
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && pressed(ImGuiKey_S) && dirty() && !s.dropdown.open) s.save_request = true;
    if (!s.dropdown.open && (s.view == View::Main || s.view == View::Page)) {
        if (pressed(ImGuiKey_PageDown) || (pressed(ImGuiKey_Tab) && !io.KeyShift)) first_page((s.page + 1) % PAGES);
        if (pressed(ImGuiKey_PageUp) || (pressed(ImGuiKey_Tab) && io.KeyShift)) first_page((s.page + PAGES - 1) % PAGES);
    }
#endif
    if (s.dropdown.open || (s.view != View::Main && s.view != View::Page)) return;
    if (pressed(ImGuiKey_GamepadR1)) first_page((s.page + 1) % PAGES);
    if (pressed(ImGuiKey_GamepadL1)) first_page((s.page + PAGES - 1) % PAGES);
}

// ---------------------------------------------------------------- drawing
// A value cut to fit (available: its width), "..." at its end
void fit(std::string& value, float available) {
    if (ImGui::CalcTextSize(value.c_str()).x <= available) return;
    do {
        size_t n = value.size() - 1;
        while (n && ((unsigned char)value[n] & 0xc0) == 0x80) --n;
        value.resize(n);
    } while (!value.empty() && ImGui::CalcTextSize((value + "...").c_str()).x > available);
    value += "...";
}
// (1.0.2, S17: Akarui Hoshi's thread, the MHGU port's the same) A value too long for its room, on the row picked (the
// controller's or the keyboard's) or held by a finger, slides along to be read whole instead of its "...": still for 1 s
// at its start, then at about 40 pixels a second (at the menu's size: two and a half of its letters' height), still for
// 1 s at its end, and from the start again. How far it has slid now (0 to `over`, how much wider it is than its room).
// Each row slides from its own start, kept while it stays picked or held (two at once: the one picked and a finger's)
float slide_offset(const std::string& id, float over) {
    struct Slide {
        std::string id;
        double start = 0;
        int frame = -2;
    };
    static Slide slides[2];
    const int frame = ImGui::GetFrameCount();
    const double now = ImGui::GetTime();
    Slide* s = nullptr;
    for (Slide& one : slides)
        if (one.id == id && one.frame >= frame - 1) s = &one;
    if (s == nullptr) {  // (a row picked or held now: the slot not used last frame, or the older one)
        s = slides[0].frame < slides[1].frame ? &slides[0] : &slides[1];
        s->id = id, s->start = now;
    }
    s->frame = frame;
    const double hold = 1.0, speed = std::max(1.f, ImGui::GetFontSize() * 2.5f), travel = over / speed;
    const double t = std::fmod(now - s->start, hold + travel + hold);
    return static_cast<float>(t < hold ? 0 : t < hold + travel ? (t - hold) * speed : over);
}
// A name cut to fit at a space, never inside a word (room: its width in font at size), "..." at its end; empty when not
// even its first word fits
std::string whole_words(ImFont* font, float size, const std::string& name, float room) {
    if (font->CalcTextSizeA(size, FLT_MAX, 0, name.c_str()).x <= room) return name;
    for (size_t n = name.rfind(' '); n != std::string::npos && n; n = name.rfind(' ', n - 1)) {
        std::string cut = name.substr(0, n) + "...";
        if (font->CalcTextSizeA(size, FLT_MAX, 0, cut.c_str()).x <= room) return cut;
    }
    return "";
}
// An invisible button over r for a finger (the controller never lands on it); true when tapped
bool touch_button(const char* id, const ImRect& r, bool enabled, bool* held) {
    ImGui::SetCursorScreenPos(r.Min);
    ImGui::BeginDisabled(!enabled);
    bool tapped = ImGui::InvisibleButton(id, ImVec2(std::max(1.f, r.GetWidth()), std::max(1.f, r.GetHeight())));
    *held = ImGui::IsItemActive();
    ImGui::EndDisabled();
    return tapped && taps_ok();
}
// The side of the x that takes a remembered choice out of the picker
float forget_side() { return std::min(s.m.row * .72f, ImGui::GetFontSize() * 2.f); }
// A row: lit gold when it is the one picked (a diamond at its left, as the game's cursor), lighter while a finger holds
// it; its title at the left, at the right an On / Off switch, the value between - and + (stepper: draw_rows' buttons,
// room their width), or the value, marked when a tap lists its choices. plated: a question's answer, a plate with its
// title in the middle
void touch_row(ImDrawList* dl, const Row& r, const ImRect& rect, bool lit, bool held, bool plated, ImVec2 label,
               float wrap, float room, bool toggle, bool stepper, float mid = -1) {  // (mid: its title's height, if not the middle)
    const Theme& t = theme();
    const Look k = touch_look();
    float font = ImGui::GetFontSize(), line = s.m.line, cy = mid >= 0 ? mid : rect.GetCenter().y;
    if (plated && !lit) plate(dl, rect, line * .8f, held, r.enabled);
    if (lit) {
        dl->AddRectFilledMultiColor(rect.Min, rect.Max, k.bar0, k.bar1, k.bar1, k.bar0);
        dl->AddRect(rect.Min, rect.Max, fade(k.glow, held ? 1.f : .8f), 0, line * .6f);
        if (held) dl->AddRectFilled(rect.Min, rect.Max, IM_COL32(255, 240, 200, 40));
    } else if (held && !plated)
        dl->AddRectFilled(rect.Min, rect.Max, fade(k.glow, .14f));
    if (!plated && t.game) {  // (lines between the rows, as in the game's lists)
        float y = rect.Max.y + ImGui::GetStyle().ItemSpacing.y * .5f;
        dl->AddLine(ImVec2(rect.Min.x, y), ImVec2(rect.Max.x, y), fade(t.divider, .8f), std::max(1.f, line * .5f));
    }
    dl->PushClipRect(rect.Min, rect.Max, true);
    if (lit && !plated) diamond(dl, ImVec2(rect.Min.x + font * .55f, cy), font * .2f, k.glow);
    ImU32 ink = !r.enabled ? t.text_off : lit ? IM_COL32(255, 248, 232, 255) : r.current ? k.glow : k.ink;
    float x = plated ? rect.GetCenter().x - label.x * .5f : rect.Min.x + font * 1.1f;
    shadow_text(dl, font, ImVec2(x, cy - label.y * .5f), ink, r.title.c_str(), wrap);
    float right = rect.Max.x - font * .6f - (r.remove ? forget_side() + font * .4f : 0);  // (draw_rows' x at the right)
    if (r.current) diamond(dl, ImVec2(right - font * .3f, cy), font * .24f, k.glow);  // (the picker: the choice it has now)
    if (toggle) {
        ImRect slot(right - font * 2.6f, cy - font * .6f, right, cy + font * .6f);
        switch_mark(dl, slot, r.toggle == 1);
        if (r.cold) shadow_text(dl, font, ImVec2(slot.Min.x - font * .8f, cy - font * .5f), lit ? ink : t.warn, "*");
    } else if (!plated) {
        bool marker = r.enabled && (r.menu || r.arrow);  // (a tap lists the choices, or goes on: a mark at the right)
        if (!r.value.empty()) {
            std::string value = r.value + (r.cold ? " *" : "");
            float bs = stepper ? std::min(s.m.row * .84f, font * 2.3f) : 0;
            const float available = stepper ? room - bs * 2 - font * .4f : room - font * (marker ? 1.4f : .3f);
            ImU32 value_ink = !r.enabled ? t.text_off : lit ? ink : r.ink ? r.ink : k.rule;
            const ImVec2 whole = ImGui::CalcTextSize(value.c_str());
            if ((lit || held) && whole.x > available && available > font) {
                // (S17: the picked or held row's value whole, sliding in the room the cut one takes: slide_offset)
                const float box_r = stepper ? right - room * .5f + available * .5f : right - (marker ? font * 1.1f : 0);
                const float box_l = box_r - available;
                dl->PushClipRect(ImVec2(box_l, rect.Min.y), ImVec2(box_r, rect.Max.y), true);
                shadow_text(dl, font, ImVec2(box_l - slide_offset(r.id, whole.x - available), cy - whole.y * .5f), value_ink,
                            value.c_str());
                dl->PopClipRect();
            } else {
                fit(value, available);
                ImVec2 size = ImGui::CalcTextSize(value.c_str());
                float vx = stepper ? right - room * .5f - size.x * .5f : right - size.x - (marker ? font * 1.1f : 0);
                shadow_text(dl, font, ImVec2(vx, cy - size.y * .5f), value_ink, value.c_str());
            }
        }
        if (r.menu && r.enabled) {
            float m = font * .26f, mx = right - font * .4f;
            dl->AddTriangleFilled(ImVec2(mx - m, cy - m * .55f), ImVec2(mx + m, cy - m * .55f), ImVec2(mx, cy + m * .65f),
                                  lit ? ink : k.rule);
        } else if (marker)
            chevron(dl, ImVec2(right - font * .35f, cy), font * .28f, 1, lit ? ink : fade(k.rule, .8f), std::max(2.f, line));
    }
    dl->PopClipRect();
}
// The phone's rows to the pages one level down (MENU.md): plates side by side, two or three to a line, each with its
// title, its summary under it and a chevron; lit as a picked row. Their runs, by row: the place in its run and the run's
// length (0: not a page's row)
void tile_runs(const std::vector<Row>& rows, std::vector<int>& at, std::vector<int>& run) {
    at.assign(rows.size(), 0), run.assign(rows.size(), 0);
    for (size_t i = 0; i < rows.size();) {
        size_t j = i;
        while (j < rows.size() && !rows[j].id.compare(0, 5, "page.") && !rows[j].header)
            j++;
        for (size_t k = i; k < j; k++)
            at[k] = (int)(k - i), run[k] = (int)(j - i);
        i = j > i ? j : i + 1;
    }
}
void tile_draw(ImDrawList* dl, const Row& r, const ImRect& rect, bool lit, bool held) {
    const Look k = touch_look();
    ImFont* font = ImGui::GetFont();
    float f = ImGui::GetFontSize(), line = s.m.line, cy = rect.GetCenter().y;
    if (lit) {
        dl->AddRectFilledMultiColor(rect.Min, rect.Max, k.bar0, k.bar1, k.bar1, k.bar0);
        dl->AddRect(rect.Min, rect.Max, fade(k.glow, held ? 1.f : .85f), 0, line * .7f);
    } else
        plate(dl, rect, line * .8f, held, r.enabled);
    ImU32 ink = !r.enabled ? theme().text_off : lit ? IM_COL32(255, 248, 232, 255) : k.ink;
    float right = rect.Max.x - f * .75f, x = rect.Min.x + f * .8f, room = right - f * .7f - x;
    chevron(dl, ImVec2(right, cy), f * .26f, 1, lit ? ink : fade(k.rule, .85f), std::max(2.f, line));
    if (lit)
        diamond(dl, ImVec2(rect.Min.x + f * .42f, cy), f * .17f, k.glow);
    float size = f, wide = font->CalcTextSizeA(size, FLT_MAX, 0, r.title.c_str()).x;
    if (wide > room)  // (a long title: smaller, down to .8 of the letters, then cut at a word)
        size = std::max(std::floor(f * .8f), std::floor(f * room / wide));
    std::string title = whole_words(font, size, r.title, room);
    if (title.empty())
        title = r.title;
    float small = std::floor(f * .78f);
    std::string value = r.value;
    if (!value.empty())
        value = whole_words(font, small, value, room);
    ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0, title.c_str());
    float y = value.empty() ? cy - ts.y * .5f : cy - (ts.y + small * 1.05f) * .5f;
    dl->PushClipRect(rect.Min, rect.Max, true);
    shadow_text(dl, font, size, ImVec2(x, y), ink, title.c_str());
    if (!value.empty())
        dl->AddText(font, small, ImVec2(x, y + ts.y + small * .05f), lit ? fade(ink, .85f) : k.rule, value.c_str());
    dl->PopClipRect();
}
// A row's help opened or closed (its "?", Y or H): one at a time; the list shows it as it opens
void toggle_help(const std::string& id) {
    s.help_open = s.help_open == id ? std::string() : id;
    s.help_scroll = !s.help_open.empty();
}
// The open picker's help: its row's, as the view's rows have it this frame
void picker_help(const std::vector<Row>& rows) {
    if (!s.dropdown.open) return;
    for (const Row& r : rows)
        if (r.id == s.dropdown.row) {
            s.dropdown.help = help_text(r);
            return;
        }
}
// Y or H: the picked row's help, as its "?" (not while the keyboard or a question is up; in the picker, its row's)
void help_keys(const std::vector<Row>& rows) {
    if (s.view == View::Keyboard || confirmation() || !settled()) return;
    if (!pressed(face_key(3)) && !pressed(ImGuiKey_H)) return;
    if (s.dropdown.open) {  // (the picker's: its row's help over the choices)
        if (!s.dropdown.help.empty()) s.dropdown.show_help = !s.dropdown.show_help;
        return;
    }
    for (const Row& r : rows)
        if (r.id == s.focus && !r.header && !r.note && r.id.compare(0, 5, "page.") && !help_text(r).empty()) {
            toggle_help(r.id);
            return;
        }
}
// A row's help, open under it: a parchment-dark plate the list's width, a gold rule and a diamond at its left, the text
// in the letters' colour; a tap on it closes it
void help_box(ImGuiWindow* w, const Row& r, float width) {
    const Look k = touch_look();
    const std::string text = help_text(r);
    ImFont* font = ImGui::GetFont();
    float f = ImGui::GetFontSize(), size = std::floor(f * .84f), line = s.m.line, in = f * 1.1f;
    float wrap = width - in - f * .9f;
    ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, wrap, text.c_str());
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImRect box(ImVec2(p.x + f * .4f, p.y), ImVec2(p.x + width, p.y + ts.y + f * 1.0f));
    ImGui::PushID(r.id.c_str());
    bool held = false;
    if (touch_button("###helpbox", box, true, &held)) s.help_open.clear();
    ImGui::PopID();
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(width, box.GetHeight()));
    ImDrawList* dl = w->DrawList;
    dl->AddRectFilled(box.Min, box.Max, theme().game ? IM_COL32(62, 42, 30, 240) : fade(k.plate, .9f));
    dl->AddRect(box.Min, box.Max, fade(k.edge, .9f), 0, std::max(1.f, line * .5f));
    dl->AddRectFilled(box.Min, ImVec2(box.Min.x + line * 1.2f, box.Max.y), k.rule);
    diamond(dl, ImVec2(box.Min.x + line * .6f, box.Min.y + f * .85f), f * .2f, k.glow);
    dl->AddText(font, size, ImVec2(box.Min.x + in - f * .4f, box.Min.y + f * .5f), held ? k.glow : k.ink, text.c_str(), nullptr, wrap);
    if (s.help_scroll) {  // (just opened: in sight, its row too)
        s.help_scroll = false;
        ImGui::ScrollToRect(w, ImRect(ImVec2(box.Min.x, box.Min.y - s.m.row), box.Max), ImGuiScrollFlags_KeepVisibleEdgeY | ImGuiScrollFlags_NoScrollParent);
    }
}
// list: the picker's choices, a remembered one with an x at its right that takes it out; else the view's rows (while
// the picker is open they keep its row marked). A row with a help has a "?" at its right that opens it under it
void draw_rows(std::vector<Row>& rows, bool list = false) {
    auto* w = ImGui::GetCurrentWindow();
    float font = ImGui::GetFontSize(), width = ImGui::GetContentRegionAvail().x - s.m.line * 3;  // (the scroll mark)
    bool passive = s.dropdown.open && !list;
    bool plated = !list && confirmation();
    float bs = std::min(s.m.row * .84f, font * 2.3f);  // (a - or + button's side)
    float gap = ImGui::GetStyle().ItemSpacing.x;
    std::function<void()> activate;
    std::function<void(int)> adjust;  // (a - or + tapped: its row's, by step)
    std::function<void()> remove;     // (a remembered choice's x tapped)
    std::string help_tap;             // (a row's "?" tapped)
    const float xs = forget_side();   // (that x's side)
    int step = 0, column = 0;
    // (MENU.md: no paragraphs on the pages. A column for the "?" where a row of the view has one, so the values line up)
    std::vector<int> tile_at, tile_run;  // (the pages' rows: plates side by side)
    tile_runs(rows, tile_at, tile_run);
    if (list || (s.view != View::Main && s.view != View::Page)) tile_run.assign(rows.size(), 0);
    std::vector<char> helps(rows.size(), 0);
    bool seals = false;
    if (!list && !plated && s.view != View::Keyboard)
        for (size_t i = 0; i < rows.size(); i++)
            seals |= helps[i] = !rows[i].header && !rows[i].note && !rows[i].grid && !tile_run[i] && !help_text(rows[i]).empty();
    const float qw = seals ? std::min(s.m.row * .66f, font * 2.3f) : 0;
    if (!list) s.targets.clear();
    float key_h = s.m.row * .82f;  // (the keyboard's keys share the list's height, a line each)
    {
        int lines = 0;
        for (int i = 0; i < (int)rows.size(); i++) {
            if (!rows[i].grid) continue;
            column = i && rows[i - 1].grid == rows[i].grid ? column + 1 : 0;
            lines += column % rows[i].grid == 0;
        }
        column = 0;
        float spacing = ImGui::GetStyle().ItemSpacing.y;
        if (lines)
            key_h = std::clamp((ImGui::GetContentRegionAvail().y - s.m.pad - spacing * (lines - 1)) / lines, s.m.row * .7f, s.m.row * 1.1f);
    }
    for (int i = 0; i < (int)rows.size(); i++) {
        Row& r = rows[i];
        if (r.note) {  // (a line of text: a small diamond, who said it in gold, the text wrapped under itself)
            const Look k = touch_look();
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImFont* fnt = ImGui::GetFont();
            float size = std::floor(font * .9f), indent = font * 1.1f, top = font * .12f;
            std::string who = r.value.empty() ? "" : r.value + ":";
            float who_w = who.empty() ? 0 : fnt->CalcTextSizeA(size, FLT_MAX, 0, who.c_str()).x + size * .35f;
            if (r.sub) indent = font * 2.0f, size = std::floor(font * .82f);  // (an example under the note before it)
            float wrap = std::max(size * 4, width - indent - who_w - font * .4f);
            float text_h = fnt->CalcTextSizeA(size, FLT_MAX, wrap, r.title.c_str()).y;
            float bar = r.progress >= 0 ? std::max(font * .55f, s.m.line * 4) : 0;  // (a progress bar under the text)
            float h = text_h + top * 2 + font * .15f + (bar ? bar + font * .45f : 0);
            ImGui::Dummy(ImVec2(width, h));
            const bool strip = chat_window_view();  // (1.0.2, the owner's: the chat's lines on a dark strip each, over a light
            if (strip)                              // game - grass, snow - in the see-through window; the text bright. How
                w->DrawList->AddRectFilled(         // solid: the chat's options, Message background)
                    ImVec2(p.x, p.y), ImVec2(p.x + width, p.y + h - font * .05f),
                    IM_COL32(10, 7, 5, (int)std::lround(2.55f * chat::opacity(chat::OPACITY_LINES))), font * .25f);
            if (!r.sub)
                diamond(w->DrawList, ImVec2(p.x + font * .55f, p.y + top + size * .55f), size * .16f,
                        r.mark ? r.mark : r.ink ? r.ink : fade(k.rule, .8f));
            if (bar) {  // (a gauge as the game's: a dark slot in a bronze frame, filled gold from the left)
                ImVec2 b0(p.x + indent, p.y + top + text_h + font * .3f), b1(p.x + width - font * .4f, b0.y + bar);
                w->DrawList->AddRectFilled(b0, b1, IM_COL32(14, 10, 8, 235), theme().rounding * .5f);
                float fill = b0.x + (b1.x - b0.x) * std::clamp(r.progress, 0.f, 1.f);
                if (fill > b0.x + 1) {
                    w->DrawList->AddRectFilledMultiColor(b0, ImVec2(fill, b1.y), k.rule, k.glow, k.glow, k.rule);
                    w->DrawList->AddLine(ImVec2(b0.x, b0.y + 1), ImVec2(fill, b0.y + 1), fade(IM_COL32(255, 255, 255, 255), .25f),
                                         std::max(1.f, s.m.line * .5f));
                }
                w->DrawList->AddRect(b0, b1, k.edge, theme().rounding * .5f, std::max(1.f, s.m.line * .6f));
            }
            if (!who.empty()) shadow_text(w->DrawList, fnt, size, ImVec2(p.x + indent, p.y + top), r.who_ink ? (ImU32)r.who_ink : k.glow, who.c_str());
            shadow_text(w->DrawList, fnt, size, ImVec2(p.x + indent + who_w, p.y + top), who.empty() && r.ink && !strip ? r.ink : k.ink,
                        r.title.c_str(), wrap);
            continue;
        }
        if (r.header) {  // (a section's title, a little apart from the rows above it, a rule after it)
            ImVec2 p = ImGui::GetCursorScreenPos();
            float h = font * (i ? 2.2f : 1.6f);
            ImGui::Dummy(ImVec2(width, h));
            ImFont* tf = title_font();
            float size = std::floor(font * .92f);
            ImVec2 ts = tf->CalcTextSizeA(size, FLT_MAX, 0, r.title.c_str());
            shadow_text(w->DrawList, tf, size, ImVec2(p.x + font * .25f, p.y + h - font * 1.3f), theme().accent, r.title.c_str());
            float end = p.x + font * .75f + ts.x, y = p.y + h - font * .78f;
            if (end < p.x + width)
                w->DrawList->AddLine(ImVec2(end, y), ImVec2(p.x + width, y), fade(theme().accent, .45f), std::max(1.f, s.m.line * .5f));
            continue;
        }
        if (tile_run[i]) {  // (a page one level down: a plate of a line of them)
            const int per = width >= font * 34 ? 3 : 2, k = tile_at[i], n = tile_run[i], first = k / per * per;
            const int on = std::min(per, n - first);  // (a last line's fewer: wider)
            float tw = (width - (on - 1) * gap) / on, th = s.m.row;
            for (int j = i - k; j < i - k + n; j++)  // (the run's height: with a summary under a title, a little taller)
                if (!rows[j].value.empty()) th = std::max(s.m.row, font * 2.9f);
            if (k % per) ImGui::SameLine(0, gap);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::PushID(r.id.c_str());
            ImRect rect(p, ImVec2(p.x + tw, p.y + th));
            bool selected = passive ? r.id == s.dropdown.row : s.focus == r.id;
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
            ImGui::BeginDisabled(!r.enabled);
            bool clicked = ImGui::Button("###row", ImVec2(tw, th));
            bool held = ImGui::IsItemActive();
            ImGui::EndDisabled();
            ImGui::PopStyleColor(3);
            if (selected && r.enabled && !passive) keep_visible(w, rect);
            s.targets.push_back({r.id, rect.GetCenter(), r.enabled});
            tile_draw(w->DrawList, r, rect, selected && r.enabled, held);
            ImGui::PopID();
            if (clicked && r.enabled && taps_ok()) {
                focus(r.id);
                activate = r.activate;
            }
            continue;
        }
        // (a key: `grid` of them to a line, each a plate; the line begins again where the keys change)
        column = r.grid && i && rows[i - 1].grid == r.grid ? column + 1 : 0;
        if (r.grid && column % r.grid) ImGui::SameLine(0, gap);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float rw = r.grid ? (width - (r.grid - 1) * gap) / r.grid : width - qw;
        bool stepper = r.number && r.adjust;
        bool toggle = r.toggle >= 0 && r.enabled;
        bool plate_row = plated || r.grid;
        // (the title wraps before the value's room: a switch, - value +, or the value)
        float room = r.grid ? 0 : r.remove ? xs + font * .6f : toggle ? font * 3.6f : stepper ? bs * 2 + font * 5.2f : r.value.empty() ? (r.arrow ? font * 1.2f : 0) : rw * .42f;
        float wrap = std::max(font, rw - font * (plate_row ? 1.2f : 1.9f) - room);
        ImVec2 label = ImGui::CalcTextSize(r.title.c_str(), nullptr, false, wrap);
        float h = r.grid ? key_h : std::max(s.m.row, label.y + font * .9f);
        const bool slider = stepper && r.slide && r.progress >= 0;  // (1.0.2: its bar under the title, the row taller)
        if (slider) h += font * .9f;
        ImGui::PushID(r.id.c_str());
        ImRect rect(p, ImVec2(p.x + rw, p.y + h));
        bool selected = passive ? r.id == s.dropdown.row : s.focus == r.id;
        ImVec4 clear(0, 0, 0, 0);  // (touch_row draws it)
        ImGui::PushStyleColor(ImGuiCol_Button, clear);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, clear);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, clear);
        ImGui::BeginDisabled(!r.enabled);
        if (stepper || r.remove) ImGui::SetNextItemAllowOverlap();  // (its - and +, or its x, are over it)
        bool clicked = ImGui::Button("###row", ImVec2(rw, h));
        bool held = ImGui::IsItemActive();
        ImGui::EndDisabled();
        ImGui::PopStyleColor(3);
        if (selected && r.enabled && !passive) keep_visible(w, rect);
        if (!list) s.targets.push_back({r.id, rect.GetCenter(), r.enabled});
        touch_row(w->DrawList, r, rect, selected && r.enabled, held, plate_row, label, wrap, room, toggle, stepper,
                  slider ? rect.Min.y + (h - font * .9f) * .5f : -1.f);
        if (chat_floating())  // (1.0.2: the chat window afloat, a row that does something - a key, Chat options)
            chat_claim(ImRect(ImMax(rect.Min, w->InnerClipRect.Min), ImMin(rect.Max, w->InnerClipRect.Max)));
        if (slider) {  // (1.0.2) the bar: a dark slot filled gold; a tap or a drag on it sets the value there
            static ImGuiID sliding;
            const ImGuiID me = w->GetID("slide");
            const float bh = std::floor(font * .36f), x0 = p.x + font * .6f, x1 = rect.Max.x - font * .6f - room - font * .4f;
            const ImRect bar(x0, rect.Max.y - font * .55f - bh, std::max(x0 + font, x1), rect.Max.y - font * .55f);
            const ImRect grab(bar.Min.x - font * .4f, bar.Min.y - font * .5f, bar.Max.x + font * .4f, rect.Max.y);
            ImGuiIO& io = ImGui::GetIO();
            if (r.enabled && !passive && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && grab.Contains(io.MousePos) &&
                w->InnerClipRect.Contains(io.MousePos))
                sliding = me;
            if (sliding == me) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && !(s.drag.window == s.list && s.drag.moving)) {
                    r.slide(std::clamp((io.MousePos.x - bar.Min.x) / bar.GetWidth(), 0.f, 1.f));
                    focus(r.id);
                } else
                    sliding = 0;
            }
            const Look k = touch_look();
            const float round = std::min(bh * .5f, theme().rounding), fill = bar.Min.x + bar.GetWidth() * std::clamp(r.progress, 0.f, 1.f);
            w->DrawList->AddRectFilled(bar.Min, bar.Max, IM_COL32(14, 10, 8, 235), round);
            if (fill > bar.Min.x + 1) w->DrawList->AddRectFilledMultiColor(bar.Min, ImVec2(fill, bar.Max.y), k.rule, k.glow, k.glow, k.rule);
            w->DrawList->AddRect(bar.Min, bar.Max, k.edge, round, std::max(1.f, s.m.line * .6f));
            w->DrawList->AddCircleFilled(ImVec2(fill, bar.GetCenter().y), bh * .95f, sliding == me ? k.glow : k.ink, 16);
        }
        if (stepper) {  // - and + (held: they repeat)
            ImVec2 next = ImGui::GetCursorScreenPos();
            float right = rect.Max.x - font * .6f, cy = slider ? rect.Min.y + (h - font * .9f) * .5f : rect.GetCenter().y;
            for (int d = -1; d <= 1; d += 2) {
                float x = d < 0 ? right - room : right - bs;
                ImRect b(x, cy - bs * .5f, x + bs, cy + bs * .5f);
                ImGui::SetCursorScreenPos(b.Min);
                ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
                ImGui::BeginDisabled(!r.enabled);
                // (on release, as a tap; held, it repeats: a drag that starts on it changes nothing)
                bool hit = ImGui::InvisibleButton(d < 0 ? "###less" : "###more", ImVec2(bs, bs), ImGuiButtonFlags_PressedOnClickRelease);
                bool down = ImGui::IsItemActive();
                ImGui::EndDisabled();
                ImGui::PopItemFlag();
                if (hit && r.enabled && taps_ok()) step = d, adjust = r.adjust, focus(r.id);
                plate(w->DrawList, b, s.m.line * .8f, down, r.enabled);
                sign(w->DrawList, b.GetCenter(), bs * .24f, d > 0, down ? touch_look().glow : touch_look().ink,
                     std::max(2.f, s.m.line * 1.4f));
            }
            ImGui::SetCursorScreenPos(next);
        }
        if (r.remove) {  // (a remembered choice: its x, a plate of its own at the right, takes it out of the list)
            ImVec2 next = ImGui::GetCursorScreenPos();
            float right = rect.Max.x - font * .6f, cy = rect.GetCenter().y;
            ImRect b(right - xs, cy - xs * .5f, right, cy + xs * .5f);
            bool down = false;
            if (touch_button("###forget", b, true, &down)) remove = r.remove;
            plate(w->DrawList, b, s.m.line * .8f, down, true);
            cross(w->DrawList, b.GetCenter(), xs * .2f, down || selected ? touch_look().glow : touch_look().ink,
                  std::max(2.f, s.m.line * 1.3f));
            ImGui::SetCursorScreenPos(next);
        }
        if (helps[i]) {  // (its "?": a tap opens its help under it, another closes it)
            ImVec2 next = ImGui::GetCursorScreenPos();
            float cx = p.x + width - qw * .5f, cy = rect.GetCenter().y, rad = std::min(qw, s.m.row) * .34f;
            bool down = false;
            if (touch_button("###help", ImRect(cx - qw * .5f, rect.Min.y, cx + qw * .5f, rect.Max.y), true, &down)) help_tap = r.id;
            help_seal(w->DrawList, ImVec2(cx, cy), rad, s.help_open == r.id, down);
            ImGui::SetCursorScreenPos(next);
        }
        ImGui::PopID();
        if (clicked && r.enabled && taps_ok()) {
            focus(r.id);
            activate = r.activate;
        }
        if (helps[i] && s.help_open == r.id) help_box(w, r, width);
    }
    if (remove) {  // (the list changes under it: the picker opens again, reopen_picker)
        remove();
        return;
    }
    if (!help_tap.empty()) toggle_help(help_tap);
    if (adjust) adjust(step);
    if (activate) activate();
}

const ImGuiWindowFlags fixed_window = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
// A finger dragged on a list scrolls it: a press that moves less than ~2% of the screen's height is still a tap, past
// that the row or button under it lets go. Let go, the list glides on and slows to a stop; a tap on a gliding list only
// stops it. w: the list (a child window), after its rows
void finger_scroll(ImGuiWindow* w) {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiIO& io = g.IO;
    Drag& d = s.drag;
    float dt = std::max(io.DeltaTime, 1e-4f);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && g.HoveredWindow == w) {
        if (d.window == w->ID && std::fabs(d.velocity) > s.m.H * .3f) s.swallow = true;
        d = {w->ID, io.MousePos.y, w->Scroll.y, w->Scroll.y, 0, true, false};
    }
    if (d.window != w->ID) return;
    if (d.pressed && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (!d.moving && std::fabs(io.MousePos.y - d.from) > s.m.H * .022f) {
            d.moving = true;
            d.from = io.MousePos.y, d.scroll = d.last = w->Scroll.y;  // (from here: no jump by the threshold)
            ImGui::ClearActiveID();  // (the row or the button under the finger: not tapped after all)
            s.swallow = true;
        }
        if (d.moving) {
            float to = std::clamp(d.scroll - (io.MousePos.y - d.from), 0.f, w->ScrollMax.y);
            d.velocity = d.velocity * .5f + (to - d.last) / dt * .5f;
            d.last = to;
            ImGui::SetScrollY(w, to);
        }
        return;
    }
    if (d.pressed) {  // (let go: a drag glides on, a tap doesn't)
        d.pressed = false;
        if (!d.moving) d.velocity = 0;
        d.moving = false;
    }
    if (d.velocity) {
        float to = std::clamp(w->Scroll.y + d.velocity * dt, 0.f, w->ScrollMax.y);
        ImGui::SetScrollY(w, to);
        d.velocity *= std::exp(-dt * 4.f);
        if (std::fabs(d.velocity) < s.m.H * .05f || to <= 0 || to >= w->ScrollMax.y) d.velocity = 0;
    }
}
// A thin gold mark at a list's right: how much of it shows, how far down it is
void scroll_mark(ImGuiWindow* w) {
    if (w->ScrollMax.y < 1) return;
    const Look k = touch_look();
    float view = w->Size.y, line = s.m.line, bar = std::max(view * view / (view + w->ScrollMax.y), s.m.row * .5f);
    float y = w->Pos.y + (view - bar) * std::clamp(w->Scroll.y / w->ScrollMax.y, 0.f, 1.f);
    float x = w->Pos.x + w->Size.x - line * 1.5f;
    w->DrawList->AddRectFilled(ImVec2(x, w->Pos.y), ImVec2(x + line, w->Pos.y + view), fade(k.rule, .18f));
    w->DrawList->AddRectFilled(ImVec2(x - line * .25f, y), ImVec2(x + line * 1.25f, y + bar), fade(k.rule, .9f));
}
const char* saved_text() {
    return tr(cold_differs(s.saved, s.boot) ? "Saved - restart pending" : "Saved");
}
// The picker: a row's choices in a plate of their own in the middle of the screen, over the menu dimmed
void touch_picker(std::vector<Row>& options) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const Theme& look = theme();
    const Look k = touch_look();
    ImFont* font = ImGui::GetFont();
    ImFont* tf = title_font();
    float f = s.m.f, pad = s.m.pad, line = s.m.line, spacing = ImGui::GetStyle().ItemSpacing.y;
    float band = std::max(f * 2.4f, s.m.row * .95f), side = band * .74f, title = std::floor(f * 1.05f);
    float frame = std::max(2.f, f * .14f) * 1.7f;  // (draw_frame's rules at the bottom)
    // (its row's help, opened by the "?" in the band: over the choices, in the help's own plate)
    const bool helped = !s.dropdown.help.empty(), helping = helped && s.dropdown.show_help;
    // (the title whole after its diamond, beside its X and its "?" when it has one: on a narrow screen it would be cut)
    float longest = tf->CalcTextSizeA(title, FLT_MAX, 0, s.dropdown.title.c_str()).x + side + pad * 3 + f * .7f +
                    (helped ? side + pad : 0);
    for (const auto& o : options)  // (a remembered one: and its x)
        longest = std::max(longest, font->CalcTextSizeA(f, FLT_MAX, 0, o.title.c_str()).x + f * 3.4f + (o.remove ? forget_side() + f * .6f : 0));
    float w = std::clamp(longest + pad * 2, vp->WorkSize.x * .32f, vp->WorkSize.x * .62f);
    const float hsize = std::floor(f * .84f), hwrap = w - pad * 2 - f * 1.4f;
    const float help_h = helping ? font->CalcTextSizeA(hsize, FLT_MAX, hwrap, s.dropdown.help.c_str()).y + f * 1.0f + spacing : 0;
    float h = std::min(band + line * 2 + pad * 2 + help_h + options.size() * (s.m.row + spacing) + pad + frame, vp->WorkSize.y * .86f);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - w) * .5f, vp->WorkPos.y + (vp->WorkSize.y - h) * .5f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::SetNextWindowFocus();  // (over the menu)
    ImGui::PushStyleColor(ImGuiCol_WindowBg, vec(look.popup));
    ImGui::Begin("Settings##picker", nullptr, fixed_window);
    auto* win = ImGui::GetCurrentWindow();
    auto* dl = win->DrawList;
    float x0 = win->Pos.x, y0 = win->Pos.y, x1 = x0 + win->Size.x, by = y0 + (band - side) * .5f + line * .5f;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y0 + band), k.band);
    if (look.game) {
        dl->AddRectFilled(ImVec2(x0, y0 + band), ImVec2(x1, y0 + band + line), RULE_RED);
        dl->AddRectFilled(ImVec2(x0, y0 + band + line), ImVec2(x1, y0 + band + line * 1.7f), RULE_GREEN);
    } else
        dl->AddRectFilled(ImVec2(x0, y0 + band), ImVec2(x1, y0 + band + line * .7f), look.accent);
    ImRect shut(x1 - pad - side, by, x1 - pad, by + side);
    bool held = false;
    if (touch_button("###shut", shut, true, &held)) close_dropdown();
    plate(dl, shut, line, held, true);
    cross(dl, shut.GetCenter(), side * .2f, held ? k.glow : k.ink, std::max(2.f, line * 1.5f));
    float title_end = shut.Min.x - pad * .5f;
    if (helped) {  // (its "?", left of the X)
        ImRect q(shut.Min.x - pad - side, by, shut.Min.x - pad, by + side);
        bool down = false;
        if (touch_button("###pickhelp", q, true, &down)) s.dropdown.show_help = !s.dropdown.show_help;
        help_seal(dl, q.GetCenter(), side * .4f, s.dropdown.show_help, down);
        title_end = q.Min.x - pad * .5f;
    }
    // (a title longer than the plate allows, its widest on a narrow screen: smaller, to fit whole)
    float tsize = title;
    ImVec2 ts = tf->CalcTextSizeA(tsize, FLT_MAX, 0, s.dropdown.title.c_str());
    const float room = title_end - (x0 + pad * 1.6f + f * .7f);
    if (ts.x > room && room > 0) {
        tsize = std::floor(std::max(title * room / ts.x, title * .6f));
        ts = tf->CalcTextSizeA(tsize, FLT_MAX, 0, s.dropdown.title.c_str());
    }
    dl->PushClipRect(ImVec2(x0, y0), ImVec2(title_end, y0 + band), true);
    diamond(dl, ImVec2(x0 + pad * 1.6f, by + side * .5f), f * .2f, k.rule);
    shadow_text(dl, tf, tsize, ImVec2(x0 + pad * 1.6f + f * .7f, by + side * .5f - ts.y * .5f), k.glow, s.dropdown.title.c_str());
    dl->PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(x0 + pad, y0 + band + line * 2 + pad));
    ImGui::BeginChild("##choices", ImVec2(win->Size.x - pad * 2, std::max(1.f, h - band - line * 2 - pad * 2 - frame)),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    auto* choices_window = ImGui::GetCurrentWindow();
    if (helping) {  // (the row's help, as a row's opened one)
        ImVec2 p = ImGui::GetCursorScreenPos();
        float cw = ImGui::GetContentRegionAvail().x - s.m.line * 3;
        ImRect box(p, ImVec2(p.x + cw, p.y + help_h - spacing));
        ImGui::Dummy(box.GetSize());
        ImDrawList* cl = choices_window->DrawList;
        cl->AddRectFilled(box.Min, box.Max, theme().game ? IM_COL32(62, 42, 30, 240) : fade(k.plate, .9f));
        cl->AddRect(box.Min, box.Max, fade(k.edge, .9f), 0, std::max(1.f, line * .5f));
        cl->AddRectFilled(box.Min, ImVec2(box.Min.x + line * 1.2f, box.Max.y), k.rule);
        diamond(cl, ImVec2(box.Min.x + line * .6f, box.Min.y + f * .85f), f * .2f, k.glow);
        cl->AddText(font, hsize, ImVec2(box.Min.x + f * .7f, box.Min.y + f * .5f), k.ink, s.dropdown.help.c_str(), nullptr, hwrap);
    }
    draw_rows(options, true);
    ImGui::Dummy(ImVec2(1, pad));
    finger_scroll(choices_window);
    scroll_mark(choices_window);
    ImGui::EndChild();
    s.dropdown.hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    draw_frame(win);
    ImGui::End();
    ImGui::PopStyleColor();
}
// The controller's buttons at the panel's foot, while one is connected: what each does here
void draw_guides(ImDrawList* dl, float x, float y, float right, float size) {
#ifndef __ANDROID__
    pc_draw_guides(dl, x, y, right, size);  // (the keyboard's keys too, a controller's buttons in its look)
    return;
#endif
    const Look k = touch_look();
    ImFont* font = ImGui::GetFont();
    struct Item {
        int glyph;
        const char* label;
        bool on;
    };
    const bool keys = s.view == View::Keyboard;  // (the keyboard: X deletes, Y a space, L / R move the cursor)
    const Item items[] = {{SettingsIcons::Dpad, "Move", true},
                          {SettingsIcons::A, keys ? "Type" : "Confirm", true},
                          {SettingsIcons::B, "Back", true},
                          {SettingsIcons::X, keys ? "Delete" : picker_removable() ? "Remove" : "Save",
                           keys || picker_removable() || (dirty() && !confirmation())},
                          {SettingsIcons::Y, keys ? "Space" : "Help", keys || !s.dropdown.open || !s.dropdown.help.empty()},
                          {SettingsIcons::L, "", s.view == View::Main || s.view == View::Page || keys},
                          {SettingsIcons::R, keys ? "Cursor" : "Sections", s.view == View::Main || s.view == View::Page || keys}};
    SettingsIcons::Ink ink;
    ink.plate = k.plate, ink.rim = k.edge, ink.rule = k.rule, ink.letter = k.ink;
    float text = std::floor(size * .78f), gap = size * .9f;
    // (from the right edge leftwards, so the strip ends where the panel's rows do)
    float total = 0;
    auto shown = [](const Item&) { return true; };
    for (const auto& it : items) {
        if (!shown(it)) continue;
        total += SettingsIcons::width(it.glyph, size) + (it.label[0] ? size * .3f + font->CalcTextSizeA(text, FLT_MAX, 0, tr(it.label)).x + gap : size * .15f);
    }
    x = std::max(x, right - total + gap);
    for (const auto& it : items) {
        if (!shown(it)) continue;
        SettingsIcons::Ink use = ink;
        if (!it.on) use.letter = fade(use.letter, .35f), use.rule = fade(use.rule, .35f), use.rim = fade(use.rim, .5f);
        SettingsIcons::draw(dl, font, ImVec2(x, y), size, it.glyph, use);
        x += SettingsIcons::width(it.glyph, size);
        if (!it.label[0]) {
            x += size * .15f;
            continue;
        }
        x += size * .3f;
        const char* label = tr(it.label);
        ImVec2 ts = font->CalcTextSizeA(text, FLT_MAX, 0, label);
        dl->AddText(font, text, ImVec2(x, y + (size - ts.y) * .5f), it.on ? k.dim : fade(k.dim, .4f), label);
        x += ts.x + gap;
    }
}

// The menu: nearly the whole screen - a header with the title, Back (in a submenu or a question), Save and Close; the
// tabs in a column at the left, the rows in a list a finger drags, the picked row's explanation at the right (under
// the rows where the screen is narrower)
// (1.0.2, the owner's) The chat window (chat_window_view): the menu's place and size until the player moves it by its
// header or sizes it by its bottom right corner; chat.txt keeps them (chat::window, fractions of the screen). On the
// screen, at least CHAT_MIN_W by CHAT_MIN_H screen heights; its body as solid as the chat's options say (Window background,
// chat::opacity: 60 % as it comes), the game round it not dimmed. While the phone's keyboard is up for Say something (chat_typing) the window goes to the screen's top for the moment, as
// tall as the keyboard leaves (keyboard_top: on a Galaxy in landscape it takes 57 % of the screen, and a window that only
// rose still had Send and every line under it), with the text and Send at its top and the chat's lines under them; the
// same in the MHGU port
// (1.0.2) The HUD's size just changed (Screen and HUD > HUD size): for a moment the whole menu fades to HUD_PEEK, so the
// HUD shows behind it at its new size as the value moves (the game, waiting while the menu is open, draws it at once);
// each change keeps it faded HUD_PEEK_NS more. The same in the MHGU port
const uint64_t HUD_PEEK_NS = 1500 * 1000000ull;
const float HUD_PEEK = .3f;
uint64_t g_hud_peek_ns;
bool hud_peek() { return g_hud_peek_ns && SDL_GetTicksNS() - g_hud_peek_ns < HUD_PEEK_NS; }
void fade_drawn(ImDrawList * dl, float a) {
    for (ImDrawVert & v : dl->VtxBuffer) v.col = fade(v.col, a);
}
const float CHAT_MIN_W = .95f, CHAT_MIN_H = .5f;
float chat_solid(int which) { return chat::opacity(which) / 100.f; }
// (the window's background: the chat's options page at least as solid as the window came, so it reads whatever it is set to)
float chat_window_solid() { return chat_options() ? std::max(.6f, chat_solid(chat::OPACITY_WINDOW)) : chat_solid(chat::OPACITY_WINDOW); }  // (1.0.2: the chat's options, Window background...: 0 to 1)
struct ChatDrag {
    int mode = 0;            // 1: moved by its header, 2: sized by its corner
    ImVec2 grab, pos, size;  // where the finger landed, and the window then
    ImVec2 now_pos, now_size;
    bool moved = false;
} g_chat_drag;
void chat_window_fit(const ImGuiViewport* vp, ImVec2* at, ImVec2* size) {
    const float W = vp->WorkSize.x, H = vp->WorkSize.y;
    size->x = std::clamp(size->x, std::min(W, CHAT_MIN_W * H), W);
    size->y = std::clamp(size->y, std::min(H, CHAT_MIN_H * H), H);
    at->x = std::clamp(at->x, vp->WorkPos.x, vp->WorkPos.x + W - size->x);
    at->y = std::clamp(at->y, vp->WorkPos.y, vp->WorkPos.y + H - size->y);
}
// Where the phone's keyboard begins, in the menu's coordinates: the window's height less the keyboard's
// (GameActivity.imeInsets, by JNI, asked every 250 ms while the chat's Say something is typed). Not known, or not up yet:
// KEYBOARD_GUESS of the screen's height (a Galaxy S25 Ultra's keyboard took about 57 % of it, in landscape)
const float KEYBOARD_GUESS = .58f;
float keyboard_top(const ImGuiViewport* vp) {
    float top = -1;
#ifdef __ANDROID__
    static uint64_t asked;
    static float seen = -1;  // (the keyboard's top, a part of the window's width; -1: not known)
    const uint64_t now = SDL_GetTicksNS();
    if (now - asked > 250000000ull) {
        asked = now, seen = -1;
        auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        auto act = env ? static_cast<jobject>(SDL_GetAndroidActivity()) : nullptr;
        if (act) {
            jclass cls = env->GetObjectClass(act);
            jmethodID m = cls ? env->GetStaticMethodID(cls, "imeInsets", "()[I") : nullptr;
            auto v = !env->ExceptionCheck() && m ? static_cast<jintArray>(env->CallStaticObjectMethod(cls, m)) : nullptr;
            if (env->ExceptionCheck()) env->ExceptionClear(), v = nullptr;
            if (v && env->GetArrayLength(v) == 3) {
                jint a[3] = {};
                env->GetIntArrayRegion(v, 0, 3, a);
                if (a[0] > 0 && a[1] > 0 && a[2] > a[0]) seen = static_cast<float>(a[2] - a[0]) / static_cast<float>(a[1]);
            }
            if (v) env->DeleteLocalRef(v);
            if (cls) env->DeleteLocalRef(cls);
            env->DeleteLocalRef(act);
        }
    }
    if (seen > 0) top = vp->Pos.y + seen * vp->Size.x;
#endif
    if (top < 0) top = vp->Pos.y + vp->Size.y * (1 - KEYBOARD_GUESS);
    return std::clamp(top, vp->Pos.y + vp->Size.y * .2f, vp->Pos.y + vp->Size.y);
}
void chat_window_rect(const ImGuiViewport* vp, ImVec2* at, ImVec2* size) {
    const float W = vp->WorkSize.x, H = vp->WorkSize.y;
    float x, y, w, h;
    if (g_chat_drag.mode)
        *at = g_chat_drag.now_pos, *size = g_chat_drag.now_size;
    else if (chat::window(&x, &y, &w, &h))
        *at = ImVec2(vp->WorkPos.x + x * W, vp->WorkPos.y + y * H), *size = ImVec2(w * W, h * H);
    else {  // (as it comes: half the screen's width, near its top, not over the whole game)
        *size = ImVec2(std::max(W * .5f, CHAT_MIN_W * H), H * .7f);
        *at = ImVec2(vp->WorkPos.x + (W - size->x) * .5f, vp->WorkPos.y + H * .05f);
    }
    chat_window_fit(vp, at, size);
    if (chat_typing() && !g_chat_drag.mode) {  // (the phone's keyboard up: at the screen's top, as tall as it leaves)
        at->y = vp->WorkPos.y;
        size->y = std::max(H * .2f, keyboard_top(vp) - vp->WorkPos.y - H * .01f);
    }
}
// Its header (but its buttons) moves it, its corner sizes it: where the finger has it until it lets go, then kept. The
// corner's grip drawn as three short rules, as a window's
void chat_window_handles(ImGuiWindow* win, const ImGuiViewport* vp, const ImRect& header, float frame, float line, const Look& k,
                         bool busy) {
    ImDrawList* dl = win->DrawList;
    const float H = vp->WorkSize.y, g = std::max(.05f * H, line * 10);
    const ImVec2 c(win->Pos.x + win->Size.x - frame - line, win->Pos.y + win->Size.y - frame - line);
    const ImRect corner(c.x - g * 1.25f, c.y - g * 1.25f, c.x + frame + line, c.y + frame + line);  // (a thumb's room)
    chat_claim(corner);  // (afloat: the grip is the window's)
    ChatDrag& d = g_chat_drag;
    ImGuiIO& io = ImGui::GetIO();
    if (!d.mode && !busy && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        d.mode = corner.Contains(io.MousePos) ? 2 : header.Contains(io.MousePos) ? 1 : 0;
        d.grab = io.MousePos, d.pos = d.now_pos = win->Pos, d.size = d.now_size = win->Size, d.moved = false;
    }
    for (int i = 1; i <= 3; i++) {
        const float q = g * i / 3.f;
        dl->AddLine(ImVec2(c.x - q, c.y), ImVec2(c.x, c.y - q), d.mode == 2 ? k.glow : fade(k.rule, .85f), std::max(1.5f, line * .8f));
    }
    if (!d.mode) return;
    const ImVec2 by(io.MousePos.x - d.grab.x, io.MousePos.y - d.grab.y);
    if (d.mode == 1)
        d.now_pos = ImVec2(d.pos.x + by.x, d.pos.y + by.y);
    else
        d.now_size = ImVec2(d.size.x + by.x, d.size.y + by.y);
    chat_window_fit(vp, &d.now_pos, &d.now_size);
    d.moved |= by.x * by.x + by.y * by.y > .0001f * H * H;  // (a tap on the header or the grip alone: nothing kept)
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;
    d.mode = 0;  // (let go: kept)
    const float W = vp->WorkSize.x;
    if (d.moved)
        chat::set_window((d.now_pos.x - vp->WorkPos.x) / W, (d.now_pos.y - vp->WorkPos.y) / H, d.now_size.x / W, d.now_size.y / H);
}
void touch_panel(bool* open) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const Theme& look = theme();
    const Look k = touch_look();
    float H = vp->WorkSize.y;
    ImVec2 panel(std::min(vp->WorkSize.x * .94f, H * 2.6f), H * .94f);
    ImVec2 at(vp->WorkPos.x + (vp->WorkSize.x - panel.x) * .5f, vp->WorkPos.y + (vp->WorkSize.y - panel.y) * .5f);
    const bool chat_win = chat_window_view();  // (1.0.2: the chat window, where the player put it)
    if (chat_win)
        chat_window_rect(vp, &at, &panel);
    else
        g_chat_drag.mode = 0;
    // (1.0.1) Graphics & Audio > Colour: the game round the menu as it is, not dimmed, and the panel a little see-through,
    // as in the MHGU port: each change shows on the picture at once (shared_screens_set_colour, below). The chat window too
    const bool colour_page = s.view == View::Page && s.at == "graphics/colour";
    if (!colour_page && !chat_win && !hud_peek()) {  // (the game under it dimmed, the touch controls with it)
        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        bg->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(0, 0, 0, 140));
    }
    ImGui::SetNextWindowPos(at, ImGuiCond_Always);
    ImGui::SetNextWindowSize(panel, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, vec(chat_win ? fade(look.panel, chat_window_solid()) : colour_page ? fade(look.panel, .85f) : look.panel));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, vec(look.text));
    ImGui::PushStyleColor(ImGuiCol_Border, vec(look.edge));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, look.game ? 0.f : look.rounding * 2);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(s.m.pad * .5f, s.m.pad * .35f));
    ImGui::Begin("Settings##main", nullptr, fixed_window);
    auto* window = ImGui::GetCurrentWindow();
    auto* dl = window->DrawList;
    ImFont* font = ImGui::GetFont();
    ImFont* tf = title_font();
    float f = ImGui::GetFontSize();
    s.m.H = H, s.m.f = f, s.m.row = std::max(.09f * H, f * 2.4f), s.m.pad = .014f * H, s.m.line = std::max(2.f, .0035f * H);
    float pad = s.m.pad, line = s.m.line;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && settled()) s.armed = true;
    back_keys(open);
#ifndef __ANDROID__
    // (the PC: a click outside the menu closes it, as Esc does - asking first when something is unsaved)
    if (*open && !s.dropdown.open && !confirmation() && settled() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !window->Rect().Contains(ImGui::GetIO().MousePos))
        ask_close(open);
#endif
    if (!*open) {  // (closed by Back this frame)
        ImGui::End();
        ImGui::PopStyleVar(4);
        ImGui::PopStyleColor(4);
        return;
    }
    page_keys();
    s.typed_enter = false;
    if (s.view == View::Keyboard && !s.dropdown.open) keyboard_keys();
    auto rows = rows_for_view(open);
    reopen_picker(rows);
    picker_help(rows);
    std::vector<Row> options;  // (the picker open: its choices, the controller moves in them)
    if (s.dropdown.open) options = dropdown_rows();
    navigate(s.dropdown.open ? options : rows, s.dropdown.open);
    picker_keys(options);
    help_keys(rows);
    bool dialog = confirmation();
    bool picking = s.dropdown.open && !options.empty();
    // The header: Back at its left, the community's Discord and the official page (their marks), Save and Close at its
    // right, the title between
    float x0 = window->Pos.x, y0 = window->Pos.y, x1 = x0 + window->Size.x, y1 = y0 + window->Size.y;
    // (1.0.2, the owner's: the chat window's own header, smaller - which chat, a picker for it, the gear for its options, X)
    const bool chat_head = chat_win && s.view == View::Chat && !chat_options() && chat_strip();
    float band = chat_win ? std::max(.075f * H, f * 2.1f) : std::max(.11f * H, f * 2.9f), side = band * .8f,
          by = y0 + (band - side) * .5f + line * .5f;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y0 + band), chat_win ? fade(k.band, chat_window_solid()) : k.band);
    if (chat_win)  // (afloat: the header is the window's - its buttons, its tabs, the rest moves it)
        chat_claim(ImRect(x0, y0, x1, y0 + band + line * 2));
    if (look.game) {  // (the game's red and green rules under it)
        dl->AddRectFilled(ImVec2(x0, y0 + band), ImVec2(x1, y0 + band + line), RULE_RED);
        dl->AddRectFilled(ImVec2(x0, y0 + band + line), ImVec2(x1, y0 + band + line * 1.7f), RULE_GREEN);
    } else
        dl->AddRectFilled(ImVec2(x0, y0 + band), ImVec2(x1, y0 + band + line * .7f), look.accent);
    float title_x = x0 + pad * 1.5f, right = x1 - pad;
    bool held = false;
    std::function<void()> later;  // (what the header or a tab asked for: once the frame is drawn as it began)
    if (chat_head) {  // (1.0.2: the chat window's header - "Chat · Room", its picker, the gear and X; the MHGU port's the same)
        ImRect r(right - side, by, right, by + side);
        if (touch_button("###close", r, true, &held)) later = [open]() { ask_close(open); };
        plate(dl, r, line, held, true);
        cross(dl, r.GetCenter(), side * .2f, held ? k.glow : k.ink, std::max(2.f, line * 1.5f));
        ImRect g(r.Min.x - pad - side, by, r.Min.x - pad, by + side);  // (the chat's options)
        bool hg = false;
        if (touch_button("###chatopts", g, true, &hg)) later = []() { chat_options_open(); };
        plate(dl, g, line, hg, true);
        {  // (a gear: a ring with eight teeth)
            const ImVec2 c = g.GetCenter();
            const float gr = side * .22f, th = std::max(2.f, line * 1.4f);
            const ImU32 col = hg ? k.glow : k.ink;
            for (int i = 0; i < 8; i++) {
                const float t = i * 3.14159265f / 4;
                dl->AddLine(ImVec2(c.x + cosf(t) * gr, c.y + sinf(t) * gr), ImVec2(c.x + cosf(t) * gr * 1.45f, c.y + sinf(t) * gr * 1.45f), col, th * 1.3f);
            }
            dl->AddCircle(c, gr, col, 20, th);
            dl->AddCircle(c, gr * .4f, col, 12, th * .8f);
        }
        right = g.Min.x - pad;
        // (1.0.2, after tabarong's floating chat) the tabs in place of the picker: the chats there are now (chat_tabs), the
        // one shown lit, each with its chat's colour under it and its lines not read in a red seal (up to 99). What they
        // leave of the header moves the window: a grip of dots there. None (both chats off): "Chat", as before
        const std::vector<ChatTab> tabs = chat_tabs();
        const float cy = by + side * .5f;
        if (tabs.empty()) {
            const char* name = cs("Chat", "Chat");
            const float size = std::floor(f * 1.05f);
            ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0, name);
            shadow_text(dl, size, ImVec2(title_x, cy - ts.y * .5f), k.glow, name);
            title_x += ts.x + pad;
        } else {
            const float size = std::floor(f * .95f), gap = pad * .6f, seal = side * .3f;
            std::vector<float> wide(tabs.size());
            float total = gap * (float)(tabs.size() - 1);
            for (size_t i = 0; i < tabs.size(); i++)
                total += wide[i] = font->CalcTextSizeA(size, FLT_MAX, 0, tabs[i].name.c_str()).x + f * 1.3f +
                                   (tabs[i].unread ? seal * 2 + f * .3f : 0.f);
            const float room = std::max(f * 4, right - title_x - side * 1.4f);  // (a handle's room left at their right)
            const float q = total > room ? std::max(.55f, room / total) : 1.f;  // (narrower: all of them smaller)
            float x = title_x;
            for (size_t i = 0; i < tabs.size(); i++) {
                const ChatTab& t = tabs[i];
                const ImRect tb(x, by, x + wide[i] * q, by + side);
                const bool on = g_chat == t.channel;
                bool ht = false;
                ImGui::PushID((int)i);
                if (touch_button("###chattab", tb, true, &ht)) {
                    const int c = t.channel;
                    later = [c]() { chat_pick_tab(c); };
                }
                ImGui::PopID();
                if (on) {  // (lit as the menu's picked section)
                    dl->AddRectFilledMultiColor(tb.Min, tb.Max, k.bar0, k.bar1, k.bar1, k.bar0);
                    dl->AddRect(tb.Min, tb.Max, fade(k.glow, .9f), 0.f, line * .7f);
                } else
                    plate(dl, tb, line * .8f, ht, true);
                dl->AddRectFilled(ImVec2(tb.Min.x + line * 1.5f, tb.Max.y - line * 1.6f), ImVec2(tb.Max.x - line * 1.5f, tb.Max.y - line * .5f),
                                  on ? (ImU32)chat::channel_ink(t.channel) : fade((ImU32)chat::channel_ink(t.channel), .55f));
                dl->PushClipRect(tb.Min, tb.Max, true);
                const float ls = std::floor(size * q), sr = seal * q;
                const ImVec2 ts = font->CalcTextSizeA(ls, FLT_MAX, 0, t.name.c_str());
                const float inner = ts.x + (t.unread ? sr * 2 + f * .3f * q : 0.f);
                float tx = tb.GetCenter().x - inner * .5f;
                shadow_text(dl, ls, ImVec2(tx, cy - ts.y * .5f - line * .3f), on ? IM_COL32(255, 248, 232, 255) : ht ? k.glow : k.dim,
                            t.name.c_str());
                if (t.unread) {  // (the lines not read: a red seal, as on the chat button)
                    char n[8];
                    snprintf(n, sizeof n, "%d", std::min(t.unread, 99));
                    const ImVec2 sc(tx + ts.x + f * .3f * q + sr, cy - line * .3f);
                    dl->AddCircleFilled(sc, sr, IM_COL32(176, 34, 30, 255), 20);
                    dl->AddCircle(sc, sr, k.rule, 20, std::max(1.f, line * .5f));
                    const float ns = std::floor(sr * (n[1] ? 1.1f : 1.3f));
                    const ImVec2 nz = font->CalcTextSizeA(ns, FLT_MAX, 0, n);
                    dl->AddText(font, ns, ImVec2(sc.x - nz.x * .5f, sc.y - nz.y * .5f), IM_COL32(255, 243, 214, 255), n);
                }
                dl->PopClipRect();
                x = tb.Max.x + gap;
            }
            title_x = x + pad * .4f;  // (the handle: from here to the gear)
        }
        if (right - title_x > side * .7f) {  // (the handle's grip: two rows of dots in its middle)
            const float gx = (title_x + right) * .5f, d = std::max(1.5f, line * .7f), sp = d * 3.2f;
            for (int row = 0; row < 2; row++)
                for (int col = -2; col <= 2; col++)
                    dl->AddCircleFilled(ImVec2(gx + col * sp, cy + (row ? .5f : -.5f) * sp), d, fade(k.rule, .7f), 8);
        }
    } else {
        if (s.view != View::Main) {  // (a submenu or a question: a step back)
            ImRect r(x0 + pad, by, x0 + pad + side, by + side);
            if (touch_button("###back", r, true, &held)) later = [open]() { ask_back(open); };
            plate(dl, r, line, held, true);
            chevron(dl, r.GetCenter(), side * .2f, -1, held ? k.glow : k.ink, std::max(2.f, line * 1.5f));
            title_x = r.Max.x + pad * 1.2f;
        }
        if (!dialog) {  // (a question asks for those itself)
            ImRect r(right - side, by, right, by + side);
            if (touch_button("###close", r, true, &held)) later = [open]() { ask_close(open); };
            plate(dl, r, line, held, true);
            cross(dl, r.GetCenter(), side * .2f, held ? k.glow : k.ink, std::max(2.f, line * 1.5f));
            bool can = dirty();
            const char* label = tr("Save");
            ImVec2 ts = ImGui::CalcTextSize(label);
            float sw = std::max(side * 1.8f, ts.x + f * 1.8f);
            ImRect save_rect(r.Min.x - pad - sw, by, r.Min.x - pad, by + side);
            if (touch_button("###save", save_rect, can, &held)) s.save_request = true;
            plate(dl, save_rect, line, held, can);
            shadow_text(dl, f, ImVec2(save_rect.GetCenter().x - ts.x * .5f, save_rect.GetCenter().y - ts.y * .5f),
                        can ? k.glow : look.text_off, label);
            right = save_rect.Min.x - pad;
            if (!chat_win) {  // (the chat window: Save and Close alone, its header the more room to move it by)
                // (the project's page, a little apart from Save: GitHub's mark, as in the MHGU port)
                ImRect site(save_rect.Min.x - pad * 1.6f - side, by, save_rect.Min.x - pad * 1.6f, by + side);
                if (touch_button("###site", site, true, &held))  // (asks first: the browser can make Android close the game)
                    later = []() { push(View::OpenSite, "dialog.cancel"); };
                plate(dl, site, line, held, true);
                github_mark(dl, site.GetCenter(), side * .27f, held ? k.glow : k.ink);
                // (the community's Discord beside it, the same size: the title fits in what is left)
                ImRect chat(site.Min.x - pad - side, by, site.Min.x - pad, by + side);
                if (touch_button("###discord", chat, true, &held))  // (asks first, as the page)
                    later = []() { push(View::OpenDiscord, "dialog.cancel"); };
                plate(dl, chat, line, held, true);
                discord_mark(dl, chat.GetCenter(), side * .27f, held ? k.glow : k.ink);
                right = chat.Min.x - pad;
            }
        }
        {  // (the title; the tab's name or the submenu's after it. Where the header's buttons leave less room (a large Menu
           // size on a 4:3 screen), both smaller to fit, down to .6 of their size; narrower still, the name alone, its last
           // words left out if need be: never a word cut in two. The same in the MHGU port)
            const char* title = tr("SETTINGS");
            float size = std::floor(f * 1.2f), cy = by + side * .5f, room = right - title_x;
            std::string sub = s.view == View::Main       ? tr(page_names[s.page])
                              : s.view == View::Page     ? page_path(s.at)
                              : s.view == View::Buttons  ? tr(s_combos_alone ? "Combined buttons" : "Buttons on the screen")
                              : s.view == View::Conflicts ? tr("Mod conflicts")
                              : s.view == View::Verify   ? tr("Verify game files")
                              : s.view == View::PadList || s.view == View::PadMap ? tr("Map a controller")
                              : s.view == View::ComboEdit ? tr("Combined buttons")
                              : s.view == View::Profiles ? tr("Control profiles")
                              : s.view == View::ProfileSlot ? tr("Control profile")
                              : s.view == View::ColourPreset ? fmt(en_es("Colour preset %d", "Preset de color %d"), s_colour_preset)
                              : s.view == View::Keybinds ? tr("Keybinds")
                              : chat_options()           ? std::string(cs("Chat options", "Opciones del chat"))
                              : s.view == View::Chat     ? chat_title()
                              : s.view == View::Friends  ? std::string(cs("Friends", "Amigos"))
                              : s.view == View::Keyboard ? s.keyboard.title
                                                         : "";
            auto sized = [](float full, float q) { return q < 1 ? std::floor(full * q) : full; };
            auto name_w = [&](float q) { return font->CalcTextSizeA(sized(f, q), FLT_MAX, 0, sub.c_str()).x; };
            auto both_w = [&](float q) {
                return tf->CalcTextSizeA(sized(size, q), FLT_MAX, 0, title).x + (sub.empty() ? 0 : f * q * 1.4f + name_w(q));
            };
            float q = std::clamp(room / both_w(1), .6f, 1.f);
            while (q > .6f && both_w(q) > room) q = std::max(.6f, q - .02f);
            bool alone = !sub.empty() && both_w(q) > room;  // (the name without the title before it)
            if (alone) {
                q = std::clamp(room / name_w(1), .6f, 1.f);
                while (q > .6f && name_w(q) > room) q = std::max(.6f, q - .02f);
                std::string cut = whole_words(font, sized(f, q), sub, room);
                if (cut.empty())  // (not even its first word: the title as it was, at its size, clipped at the buttons)
                    alone = false, q = 1;
                else
                    sub = cut;
            }
            dl->PushClipRect(ImVec2(x0, y0), ImVec2(std::max(title_x, right), y0 + band), true);
            float x = title_x;
            if (!alone) {
                ImVec2 ts = tf->CalcTextSizeA(sized(size, q), FLT_MAX, 0, title);
                shadow_text(dl, tf, sized(size, q), ImVec2(x, cy - ts.y * .5f), k.glow, title);
                x += ts.x + f * q * .7f;
                if (!sub.empty()) diamond(dl, ImVec2(x, cy), f * q * .18f, k.rule);
                x += f * q * .7f;
            }
            if (!sub.empty()) {
                ImVec2 ss = font->CalcTextSizeA(sized(f, q), FLT_MAX, 0, sub.c_str());
                shadow_text(dl, sized(f, q), ImVec2(x, cy - ss.y * .5f), k.ink, sub.c_str());
            }
            dl->PopClipRect();
        }
    }
    // (1.0.2, the owner's) The chat window's first row: the text (the chat page: "Say something", a tap types) and Send
    // beside it, and Keys (the keys on the screen instead of the phone's keyboard, or the phone's again) where they fit.
    // Typing (keys): the text is the view's own (the caller draws it in the width this gives). Returns the row's height,
    // or with keys the text's width. The MHGU port's the same
    auto chat_first_row = [&](bool can, bool keys, float rt) -> float {
        const float rh = f * 2.2f, rl = x0 + pad, re = x1 - pad;
        chat_claim(ImRect(x0, rt - pad * .5f, x1, rt + rh + pad * .5f));  // (afloat: the text, Send and Keys are the window's)
        const char* send_t = cs("Send", "Enviar");
#ifdef __ANDROID__
        const char* keys_t = !keys ? nullptr : g_text_input == 2 ? cs("Keys", "Teclas") : cs("Phone keyboard", "Teclado del teléfono");
#else
        const char* keys_t = nullptr;  // (a desktop's own keyboard: no phone keyboard to change to)
#endif
        const float sw = std::max(f * 5.f, ImGui::CalcTextSize(send_t).x + f * 1.8f);
        const float kw = keys_t ? std::max(f * 5.f, ImGui::CalcTextSize(keys_t).x + f * 1.8f) : 0.f;
        const bool keys_fit = keys_t && re - rl - sw - kw - pad * 2 > f * 8;
        ImRect send(re - sw, rt, re, rt + rh);
        ImRect keys_r(send.Min.x - pad - kw, rt, send.Min.x - pad, rt + rh);
        const float field_end = (keys_fit ? keys_r.Min.x : send.Min.x) - pad;
        bool h = false;
        if (touch_button("###chatsend", send, can, &h))
            later = keys ? std::function<void()>([]() { accept_keyboard(); }) : std::function<void()>([]() { chat_say_open(); });
        const bool lit = keys && !s.keyboard.buffer.empty();
        plate(dl, send, line, h, can && (lit || !keys));
        ImVec2 ts = ImGui::CalcTextSize(send_t);
        shadow_text(dl, f, ImVec2(send.GetCenter().x - ts.x * .5f, send.GetCenter().y - ts.y * .5f),
                    h ? k.glow : can && lit ? k.ink : look.text_off, send_t);
        if (keys_fit) {
            bool hk = false;
            if (touch_button("###chatkeys", keys_r, true, &hk))
                later = []() {
                    if (g_text_input == 2) text_input(1);
                    else phone_keyboard();
                };
            plate(dl, keys_r, line, hk, true);
            ImVec2 kt = ImGui::CalcTextSize(keys_t);
            shadow_text(dl, f, ImVec2(keys_r.GetCenter().x - kt.x * .5f, keys_r.GetCenter().y - kt.y * .5f), hk ? k.glow : k.ink, keys_t);
        }
        if (keys) return field_end - rl;
        ImRect field(rl, rt, field_end, rt + rh);  // (the chat page: the text to say, a tap types)
        bool hf = false;
        if (touch_button("###chatfield", field, can, &hf)) later = []() { chat_say_open(); };
        dl->AddRectFilled(field.Min, field.Max,  // (1.0.2: as solid as the chat's options say, Typing field background)
                          IM_COL32(14, 10, 8, (int)std::lround(chat_solid(chat::OPACITY_FIELD) * (can ? 255 : 165))));
        dl->AddRect(field.Min, field.Max, hf ? k.glow : k.rule, 0.f, line * .7f);
        const char* hint = cs("Say something", "Decir algo");
        ImVec2 hs = ImGui::CalcTextSize(hint);
        shadow_text(dl, f, ImVec2(field.Min.x + f * .6f, field.GetCenter().y - hs.y * .5f), can ? k.dim : look.text_off, hint);
        return rh;
    };
    // The body: the sections at the left of a section's pages; a submenu's or a question's rows alone; under them, a strip
    // with the last save's outcome, what the restart mark means and, with a controller, its buttons
    float frame = std::max(2.f, f * .14f) * 1.7f;  // (draw_frame's rules at the bottom)
    bool pad_guides = controls_real_pad();  // (not a phone's own keys, nor a keyboard Android calls a gamepad)
#ifndef __ANDROID__
    pad_guides = true;  // (the PC: the keyboard's keys when no controller was the last)
#endif
    float guide = std::floor(f * 1.15f);
    const bool typing = chat_typing();  // (the chat window over the phone's keyboard: no strip at the foot, the lines' room)
    float top = y0 + band + line * 2 + pad, bottom = y1 - frame - pad - (typing ? 0.f : guide + pad * .8f), left = x0 + pad,
          end = x1 - pad;
    auto error_text = [&](float y, float x, float w) {  // (s.error, wrapped at w; the height it takes)
        if (s.error.empty()) return 0.f;
        dl->AddText(font, f, ImVec2(x, y), look.warn, s.error.c_str(), nullptr, w);
        return font->CalcTextSizeA(f, FLT_MAX, w, s.error.c_str()).y + pad * .6f;
    };
    ImRect list_rect;
    if (s.view == View::Main || s.view == View::Page) {
        // The six sections, each a plate with its mark and its name (the one shown lit as the picked row, a notch at
        // its right pointing at the rows), and Credits & Licenses under them, smaller, past a gold diamond between
        // two rules. A section's name too long for the column: smaller, on one line
        float tw = 0;
        for (int i = 0; i < PAGES - 1; i++) tw = std::max(tw, ImGui::CalcTextSize(tr(page_names[i])).x);
        tw = std::clamp(tw + f * 3.f, (end - left) * .16f, (end - left) * .3f);  // (a 4:3 screen: up to .3, the names at their size)
        float gap = pad * .5f, credits = std::max(f * 2.f, s.m.row * .62f), ornament = pad * 1.6f;
        float th = std::min((bottom - top - credits - ornament - gap * (PAGES - 2)) / (PAGES - 1), s.m.row * 1.1f);
        ImU32 bright = IM_COL32(255, 248, 232, 255);
        for (int i = 0; i < PAGES; i++) {
            bool small = i == PAGE_ABOUT;
            ImRect r = small ? ImRect(left + tw * .05f, bottom - credits, left + tw * .95f, bottom)
                             : ImRect(left, top + i * (th + gap), left + tw, top + i * (th + gap) + th);
            ImGui::PushID(i);
            if (touch_button("###tab", r, true, &held) && (s.page != i || s.view != View::Main)) later = [i]() { first_page(i); };
            ImGui::PopID();
            bool active = s.page == i;
            float cy = r.GetCenter().y, h = r.GetHeight();
            if (active) {  // (lit as the picked row, a notch at its right pointing at the rows)
                float n = std::min(h * .18f, pad);
                dl->AddRectFilledMultiColor(r.Min, r.Max, k.bar0, k.bar1, k.bar1, k.bar0);
                dl->AddRect(r.Min, r.Max, fade(k.glow, .9f), 0, line * .7f);
                if (!small) dl->AddTriangleFilled(ImVec2(r.Max.x, cy - n), ImVec2(r.Max.x + pad * .75f, cy), ImVec2(r.Max.x, cy + n), k.glow);
            } else
                plate(dl, r, line * .8f, held, true);
            float mark = small ? f * .95f : f * 1.25f, mx = r.Min.x + mark * .5f + f * .45f;
            section_icon(dl, i, ImVec2(mx, cy), mark, active ? bright : fade(k.rule, .95f));
            const char* name = tr(page_names[i]);
            float tx = mx + mark * .5f + f * .5f, room = r.Max.x - tx - f * .35f, size = small ? std::floor(f * .8f) : f;
            float wide = font->CalcTextSizeA(size, FLT_MAX, 0, name).x;
            // (a long name: smaller, on one line; never below 1, whatever room a window that small leaves)
            float text = wide > room ? std::max(1.f, std::floor(size * room / wide)) : size;
            ImVec2 ts = font->CalcTextSizeA(text, FLT_MAX, 0, name);
            shadow_text(dl, text, ImVec2(tx, cy - ts.y * .5f), active ? bright : k.dim, name);
        }
        {  // (between the sections and Credits & Licenses: a gold diamond between two rules)
            float y = bottom - credits - ornament * .5f, cx = left + tw * .5f;
            dl->AddLine(ImVec2(left + tw * .1f, y), ImVec2(cx - f * .55f, y), fade(k.rule, .5f), std::max(1.f, line * .5f));
            dl->AddLine(ImVec2(cx + f * .55f, y), ImVec2(left + tw * .9f, y), fade(k.rule, .5f), std::max(1.f, line * .5f));
            diamond(dl, ImVec2(cx, y), f * .26f, fade(k.rule, .85f));
            diamond(dl, ImVec2(cx, y), f * .11f, k.band);
        }
        float lx = left + tw + pad * 1.4f;
        float y = top + error_text(top, lx, end - lx);
        list_rect = ImRect(lx, y, end, bottom);
    } else if (s.view == View::Chat && chat_strip() && !chat_options()) {  // (1.0.2: the text and Send in the window's first row, the lines under)
        float y = top;
        if (g_chat == chat::FRIEND) {  // (1.0.2, after tabarong's floating chat: which friend, the arrows to the others)
            const std::vector<chat::Person> friends = chat_friends();
            const float rh = f * 1.9f;
            const ImRect fr(left, y, end, y + rh);
            chat_claim(ImRect(x0, y, x1, y + rh));
            dl->AddRectFilled(fr.Min, fr.Max, IM_COL32(10, 7, 5, (int)std::lround(chat_solid(chat::OPACITY_LINES) * 255)), f * .25f);
            dl->AddRectFilled(fr.Min, ImVec2(fr.Min.x + line * 1.5f, fr.Max.y), (ImU32)chat::channel_ink(chat::FRIEND));
            if (friends.size() > 1)
                for (int d = -1; d <= 1; d += 2) {
                    const ImRect a = d < 0 ? ImRect(fr.Min.x + line * 2, fr.Min.y, fr.Min.x + line * 2 + rh * 1.2f, fr.Max.y)
                                           : ImRect(fr.Max.x - rh * 1.2f, fr.Min.y, fr.Max.x, fr.Max.y);
                    bool ha = false;
                    ImGui::PushID(d);
                    if (touch_button("###friendstep", a, true, &ha)) later = [d]() { chat_step_friend(d); };
                    ImGui::PopID();
                    plate(dl, a, line * .8f, ha, true);
                    chevron(dl, a.GetCenter(), rh * .17f, d, ha ? k.glow : k.ink, std::max(2.f, line * 1.4f));
                }
            std::string who = cs("No friends yet", "Todavía no tienes amigos"), count;
            bool online = false;
            int others = 0;  // (the other friends' lines not read)
            for (size_t i = 0; i < friends.size(); i++) {
                if (friends[i].id == g_chat_peer) {
                    who = friends[i].name.empty() ? chatc::show_code(friends[i].code) : friends[i].name;
                    online = friends[i].online && friends[i].mutual;
                    count = std::to_string(i + 1) + "/" + std::to_string(friends.size());
                } else
                    others += chat::unread(chat::FRIEND, friends[i].id);
            }
            const float tx = fr.Min.x + (friends.size() > 1 ? rh * 1.2f + line * 2 + pad : pad), cy = fr.GetCenter().y;
            if (!g_chat_peer.empty())  // (online: a green dot, else a grey one)
                dl->AddCircleFilled(ImVec2(tx + f * .3f, cy), f * .22f, online ? IM_COL32(110, 214, 96, 255) : IM_COL32(120, 110, 100, 255), 12);
            const float nx = tx + (g_chat_peer.empty() ? 0.f : f * .9f);
            const ImVec2 ns = font->CalcTextSizeA(f, FLT_MAX, 0, who.c_str());
            dl->PushClipRect(ImVec2(nx, fr.Min.y), ImVec2(fr.Max.x - (friends.size() > 1 ? rh * 1.2f : 0.f) - f * 3.5f, fr.Max.y), true);
            shadow_text(dl, f, ImVec2(nx, cy - ns.y * .5f), g_chat_peer.empty() ? k.dim : (ImU32)chat::name_ink(who), who.c_str());
            dl->PopClipRect();
            std::string tail = count + (others ? "  +" + std::to_string(std::min(others, 99)) : std::string());
            if (!tail.empty()) {
                const float small = std::floor(f * .8f);
                const ImVec2 cs_ = font->CalcTextSizeA(small, FLT_MAX, 0, tail.c_str());
                const float cx = fr.Max.x - (friends.size() > 1 ? rh * 1.2f + pad : pad) - cs_.x;
                dl->AddText(font, small, ImVec2(cx, cy - cs_.y * .5f), others ? IM_COL32(255, 132, 112, 255) : k.dim, tail.c_str());
            }
            y += rh + pad * .6f;
        }
        y += chat_first_row(chat_can_say(), false, y) + pad;
        y += error_text(y, left, end - left);
        list_rect = ImRect(left, y, end, bottom);
    } else if (s.view == View::Buttons || s.view == View::ComboEdit || s.view == View::Conflicts || s.view == View::PadList ||
               s.view == View::PadMap || s.view == View::Profiles || s.view == View::ProfileSlot ||
               s.view == View::ColourPreset || s.view == View::Keybinds || s.view == View::Chat || s.view == View::Friends || s.view == View::Verify) {  // (the whole width)
        float y = top + error_text(top, left, end - left);
        list_rect = ImRect(left, y, end, bottom);
    } else if (s.view == View::Keyboard) {  // (the text typed so far, what it takes beside it, the keys under it)
        const Keyboard& kb = s.keyboard;
        ImRect field(left, top, end, top + f * 2.2f);
        const bool chat_field = kb.filter == KB_CHAT && chat_strip();
        if (chat_field)  // (the chat window's: Send and Keys beside the text, the chat's lines under)
            field.Max.x = left + chat_first_row(true, true, top);
        dl->AddRectFilled(field.Min, field.Max,  // (1.0.2, the chat's: as solid as its options say)
                          IM_COL32(14, 10, 8, chat_field ? (int)std::lround(chat_solid(chat::OPACITY_FIELD) * 255) : 235));
        dl->AddRect(field.Min, field.Max, k.rule, 0.f, line * .7f);
        float small = std::floor(f * .8f);
        ImVec2 hs = font->CalcTextSizeA(small, FLT_MAX, 0, kb.hint.c_str());
        float hint_w = kb.hint.empty() ? 0 : std::min(hs.x + f * 1.6f, field.GetWidth() * .45f);  // (wrapped at hint_w - f)
        std::string text = kb.buffer;
        text.insert(kb.cursor, "|");
        float room = field.GetWidth() - f * 1.2f - hint_w;
        float upto = ImGui::CalcTextSize(text.c_str(), text.c_str() + kb.cursor + 1).x;
        float tx = field.Min.x + f * .6f - std::max(0.f, upto - room);  // (a long text: the part at the cursor shows)
        ImVec2 ts = ImGui::CalcTextSize(text.c_str());
        dl->PushClipRect(ImVec2(field.Min.x + line, field.Min.y), ImVec2(field.Max.x - line - hint_w, field.Max.y), true);
        shadow_text(dl, f, ImVec2(tx, field.GetCenter().y - ts.y * .5f), k.ink, text.c_str());
        dl->PopClipRect();
        if (hint_w > 0) {  // (what the text takes: a nickname's letters, an address's form)
            float hx = field.Max.x - hint_w;
            dl->AddLine(ImVec2(hx, field.Min.y + pad * .6f), ImVec2(hx, field.Max.y - pad * .6f), fade(k.rule, .35f), std::max(1.f, line * .4f));
            dl->AddText(font, small, ImVec2(hx + f * .5f, field.GetCenter().y - font->CalcTextSizeA(small, FLT_MAX, hint_w - f, kb.hint.c_str()).y * .5f),
                        k.dim, kb.hint.c_str(), nullptr, hint_w - f);
        }
        float y = field.Max.y + pad * .8f;
        y += error_text(y, left, end - left);
        list_rect = ImRect(left, y, end, bottom);
    } else if (dialog) {  // (the question, its answers in a column under it)
        float cw = std::min((end - left) * .6f, f * 24), cx = (left + end) * .5f;
        std::string question = s.view == View::ChatOn ? std::string(cs("Turn on the online chat?", "¿Activar el chat en línea?"))
                               : s.view == View::KeyCapture ? fmt("Press a key or mouse button for %s", tr(BIND_TITLES[s_capture_bind]))
                               : s.view == View::PadCapture ? fmt("Press a controller button for combined button %d (8 seconds)", s_pad_capture_combo + 1)
                               : tr(s.view == View::Restart        ? "Restart the game?"
                                  : s.view == View::Exit         ? "Exit game?"
                                  : s.view == View::Reset        ? (s.reset_at.find('/') == std::string::npos ? "Reset this section?" : "Reset this page?")
                                  : s.view == View::RemoveDriver ? "Remove this driver?"
                                  : s.view == View::RemoveMod    ? "Remove this mod?"
                                  : s.view == View::RemoveCheat  ? "Remove this cheat?"
                                  : s.view == View::OpenSite     ? "Open the project's page?"
                                  : s.view == View::OpenDiscord  ? "Open the community's Discord?"
                                                                 : "Unsaved changes");
        std::string detail = s.view == View::ChatOn ? chat_on_detail()
                             : s.view == View::RemoveDriver
                                 ? fmt("%s: its files are deleted. To use it again, import its .zip again.", s.remove_name.c_str())
                             : s.view == View::RemoveMod
                                 ? fmt("%s: its files are deleted from the app. To use it again, add it again.", s.remove_mod.c_str())
                             : s.view == View::RemoveCheat
                                 ? fmt("%s: it is deleted from the cheats file. To use it again, import it again.", s.remove_name.c_str())
                             : s.view == View::KeyCapture
                                 ? tr("Escape or Back: cancel. A mouse button: click away from these buttons.")
                             : s.view == View::PadCapture
                                 ? tr("Press the controller button you want for it, even one the list doesn't name (a paddle, say).")
                                 : tr(s.view == View::Restart ? "The game closes and opens again. Save your hunt in the game first."
                                      : s.view == View::Exit  ? "The game closes. Save your hunt in the game first: what was not saved there is lost."
                                      : s.view == View::Reset ? "Its options go back as they come. Nothing changes until you save."
                                      : s.view == View::OpenSite
                                          ? "It opens in your browser. On phones with little memory, Android may close the game, so save in the game first."
                                      : s.view == View::OpenDiscord
                                          ? "It opens in your browser or the Discord app. On phones with little memory, Android may close the game, so save in the game first."
                                          : "Save them before closing?");
        float size = std::floor(f * 1.25f), y = top + pad * 1.5f;
        diamond(dl, ImVec2(cx, y + f * .2f), f * .26f, k.rule);
        dl->AddLine(ImVec2(cx - cw * .35f, y + f * .2f), ImVec2(cx - f * .6f, y + f * .2f), fade(k.rule, .5f), std::max(1.f, line * .5f));
        dl->AddLine(ImVec2(cx + f * .6f, y + f * .2f), ImVec2(cx + cw * .35f, y + f * .2f), fade(k.rule, .5f), std::max(1.f, line * .5f));
        y += f * .9f;
        ImVec2 qs = tf->CalcTextSizeA(size, FLT_MAX, cw, question.c_str());
        shadow_text(dl, tf, size, ImVec2(cx - qs.x * .5f, y), k.glow, question.c_str(), cw);
        y += qs.y + pad * .6f;
        ImVec2 ds = font->CalcTextSizeA(f * .85f, FLT_MAX, cw, detail.c_str());
        dl->AddText(font, f * .85f, ImVec2(cx - std::min(cw, ds.x) * .5f, y), k.dim, detail.c_str(), nullptr, cw);
        y += ds.y + pad * 1.5f;
        y += error_text(y, cx - cw * .5f, cw);
        list_rect = ImRect(cx - cw * .5f, y, cx + cw * .5f, bottom);
    }
    // (choosing a key: the next one pressed is it, and a mouse button clicked away from the view's buttons - controls.cpp)
    controls_capture(s.view == View::KeyCapture, s.view == View::KeyCapture && !list_rect.Contains(ImGui::GetIO().MousePos));
    controls_pad_capture(s.view == View::PadCapture);  // (a controller's button for a combined button: controls.cpp)
    // The rows' list
    ImGui::SetCursorScreenPos(list_rect.Min);
    ImGui::BeginChild("##list", ImVec2(list_rect.GetWidth(), std::max(1.f, list_rect.GetHeight())), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    auto* list = ImGui::GetCurrentWindow();
    s.list = list->ID;
    if (s.restore_scroll) {
        ImGui::SetScrollY(s.scroll);
        s.restore_scroll = false;
    }
    draw_rows(rows);
    ImGui::Dummy(ImVec2(1, pad));
    finger_scroll(list);
    if (chat_win && list->ScrollMax.y >= 1) {  // (1.0.2, afloat: the lines' scroll strip at the list's right is the window's -
        const float sw = std::max(f * 1.3f, .04f * H);  // a finger drags the lines there; on the lines it is the game's)
        const ImRect strip(list->Pos.x + list->Size.x - sw, list->Pos.y, list->Pos.x + list->Size.x, list->Pos.y + list->Size.y);
        chat_claim(strip);
        if (chat_floating()) list->DrawList->AddRectFilled(strip.Min, strip.Max, fade(k.band, .35f), f * .3f);
    }
    scroll_mark(list);
    if (picking)  // (a window of its own, drawn over the menu's: dimmed on its own)
        list->DrawList->AddRectFilled(list->Pos, ImVec2(list->Pos.x + list->Size.x, list->Pos.y + list->Size.y), IM_COL32(0, 0, 0, 150));
    ImGui::EndChild();
    if (!typing) {  // the strip at the foot: the last save's outcome and the restart mark at its left, a controller's buttons at its right
        float gy = y1 - frame - pad * .6f - guide;
        dl->AddLine(ImVec2(left, gy - pad * .4f), ImVec2(end, gy - pad * .4f), fade(k.rule, .25f), std::max(1.f, line * .4f));
        float small = std::floor(f * .8f), x = left + f * .2f, cy = gy + guide * .5f;
        auto say_foot = [&](const std::string& text, ImU32 ink) {
            ImVec2 ts = font->CalcTextSizeA(small, FLT_MAX, 0, text.c_str());
            dl->AddText(font, small, ImVec2(x, cy - ts.y * .5f), ink, text.c_str());
            x += ts.x + f;
        };
        if (s.saved_notice && s.error.empty()) say_foot(saved_text(), k.glow);
        if (std::any_of(rows.begin(), rows.end(), [](const Row& row) { return row.cold; })) say_foot(tr("* Restart required"), k.dim);
        if (pad_guides && !chat_floating()) draw_guides(dl, x, gy, end - (chat_win ? .07f * H : 0.f), guide);  // (the chat window's corner: its grip; afloat: the game has the controller)
    }
    if (later) later();
    if (s.save_request) {
        s.save_request = false;
        save();
    }
    draw_frame(window);
    if (chat_win)  // (1.0.2: moved by its header, sized by its corner)
        chat_window_handles(window, vp, ImRect(title_x, y0, right, y0 + band), frame, line, k, dialog || picking);
    if (hud_peek()) {  // (1.0.2: the HUD's size just changed - the HUD behind the menu, at its new size)
        fade_drawn(dl, HUD_PEEK);
        fade_drawn(list->DrawList, HUD_PEEK);
    }
    if (picking) {
        dl->PushClipRectFullScreen();
        dl->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(0, 0, 0, 150));
        dl->PopClipRect();
    }
    ImGui::End();
    if (s.dropdown.open && !options.empty())
        touch_picker(options);
    else
        s.dropdown.hovered = false;
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(4);
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) s.swallow = false;  // (the press it was for has let go)
}

void start() {  // (the saved options that act outside the menu: the FPS counter, the room's notices, the colours)
    g_started = true;
    float fps = std::max(1.f, (float)std::atof(game_option_get("fps", "30").c_str()));
    perfstats_set(app_setting_bool("Graphics", "FpsCounter", false), fps, settings_spanish());
    list_older_lobby();
    s.draft = s.saved = load();
}

// A notice over the game: a small plate as the FPS counter's, in the middle at y, faded by a; at its left a diamond in
// `mark` (0: the rule's colour), who said it in gold, the text in `ink` (0: the letters' colour). The y under it.
float notice_plate(float y, float a, const std::string& who, const std::string& text, ImU32 mark, ImU32 ink) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const Look k = touch_look();
    ImFont* font = ImGui::GetFont();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float W = vp->Size.x, H = vp->Size.y, f = std::floor(.03f * H * s.draft.menu_scale / 100.f), line = std::max(2.f, .0035f * H);
    float pad = f * .45f, most = W * .44f;
    float who_w = who.empty() ? 0 : font->CalcTextSizeA(f, FLT_MAX, 0, who.c_str()).x + f * .35f;
    float wrap = most - pad * 2 - f - who_w;
    ImVec2 ts = font->CalcTextSizeA(f, FLT_MAX, wrap, text.c_str());
    float w = ts.x + who_w + f + pad * 2, h = ts.y + pad * 2 + line * 1.7f * 2;
    ImVec2 p0(vp->Pos.x + (W - w) * .5f, y), p1(p0.x + w, y + h);
    dl->AddRectFilled(ImVec2(p0.x, p0.y + line), ImVec2(p1.x, p1.y + line), fade(IM_COL32(0, 0, 0, 120), a));
    dl->AddRectFilled(p0, p1, fade(IM_COL32(50, 25, 24, 236), a));
    dl->AddRect(p0, p1, fade(IM_COL32(0, 0, 0, 255), a), 0, line);
    dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + line), fade(RULE_RED, a));
    dl->AddRectFilled(ImVec2(p0.x, p0.y + line), ImVec2(p1.x, p0.y + line * 1.7f), fade(RULE_GREEN, a));
    dl->AddRectFilled(ImVec2(p0.x, p1.y - line * 1.7f), ImVec2(p1.x, p1.y - line), fade(RULE_GREEN, a));
    dl->AddRectFilled(ImVec2(p0.x, p1.y - line), p1, fade(RULE_RED, a));
    float x = p0.x + pad, ty = p0.y + line * 1.7f + pad;
    diamond(dl, ImVec2(x + f * .3f, ty + f * .55f), f * .17f, fade(mark ? mark : k.rule, a));
    x += f;
    if (!who.empty()) shadow_text(dl, font, f, ImVec2(x, ty), fade(k.glow, a), who.c_str());
    shadow_text(dl, font, f, ImVec2(x + who_w, ty), fade(ink ? ink : k.ink, a), text.c_str(), wrap);
    return p1.y + line * 2;
}

// A player's GPU driver that did not work at this start (gpu_driver.h): said over the game for a few seconds once the
// game's device is made, and not again at the next start. The y under it.
float driver_notice(float y) {
    constexpr double kShown = 8, kFade = .6;
    static double since = -1;
    if (since < 0) {
        if (s.failure_told) return y;
        const GpuDriverStatus st = gpu_driver_status();
        if (st.failed.empty() || !st.made) return y;
        since = SDL_GetTicks() / 1000.0;
        s.failure_text = fmt(say("GPU driver: %s did not work. The system's driver is in use (Settings, Graphics & Audio).",
                                 "Driver de GPU: %s no funcionó. Se usa el del sistema (Opciones, Gráficos y audio)."),
                             st.failed.c_str());
    }
    double shown = SDL_GetTicks() / 1000.0 - since;
    if (shown > kShown) {
        if (!s.failure_told) gpu_driver_failure_seen(), s.failure_told = true;
        return y;
    }
    float a = (float)std::clamp((kShown - shown) / kFade, 0.0, 1.0);
    return notice_plate(y, a, "", s.failure_text, theme().warn, theme().warn);
}

// The app's own notice (settings_panel_notice: a controller that needs a map), over the game for a few seconds. The y
// under it.
float app_notice(float y) {
    const double kShown = g_notice_seconds, kFade = .6;
    if (g_notice_since == -2 && g_present_count >= 30) g_notice_since = SDL_GetTicks() / 1000.0;
    if (g_notice_since < 0) return y;
    const double shown = SDL_GetTicks() / 1000.0 - g_notice_since;
    if (shown > kShown) {
        g_notice_since = -1;
        return y;
    }
    const float a = (float)std::clamp((kShown - shown) / kFade, 0.0, 1.0);
    return notice_plate(y, a, "", spanish() ? g_notice_es : g_notice_en, theme().warn, 0);
}

// The room's lines that came while the menu is closed, over the game for a few seconds: at the top, in the middle
// (under the FPS counter and a driver's failure), each a small plate as the counter's, the newest at the bottom
// ---------------------------------------------------------------- the credits
// Shown over the game for a few seconds when the app opens (again from Credits & Licenses): who made it and where the
// official one is, so a copy passed around elsewhere still says so (ADDITIONAL-TERMS: these lines must stay)
double g_credits_at = -1;
bool g_credits_asked = false;  // (Show the credits: over the menu too)
// A tap closes them (1.0.1, a player's suggestion: "disappear after touching the screen"; MHGU for Android's the same): a
// finger put on the screen (settings_panel_thanks_event, the first to see each event: credits_tap) once they are in
// (kCreditsArm: a finger already down as they opened puts none) fades them out in kCreditsOut instead of their 8 seconds.
// The tap still goes where it went before (they never took input); they still show at every start and from Credits &
// Licenses
constexpr double kCreditsShown = 8, kCreditsArm = .4, kCreditsOut = .25;
double g_credits_shown = kCreditsShown;  // (how long they stay this time: less once a tap closed them)
std::atomic<bool> g_credits_tap{false};
void credits_show() {
    g_credits_at = SDL_GetTicks() / 1000.0, g_credits_asked = true;
    g_credits_shown = kCreditsShown, g_credits_tap = false;  // (the tap on Show the credits is not one on them)
}
void credits_tap(const SDL_Event& e) {
    if (e.type == SDL_EVENT_FINGER_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) g_credits_tap = true;
}
// (each frame they are drawn) a tap since the last one
void credits_tapped(double now) {
    if (!g_credits_tap.exchange(false) || g_credits_at < 0) return;
    const double t = now - g_credits_at;
    if (t < kCreditsArm || t >= g_credits_shown - kCreditsOut) return;
    g_credits_shown = t + kCreditsOut;
    std::fprintf(stderr, "[Info] App: the credits were closed by a tap after %.1f s\n", t);
}
void draw_credits() {
    constexpr double kIn = .4;
    const double now = SDL_GetTicks() / 1000.0;
    if (g_credits_at < 0) g_credits_at = now;
    credits_tapped(now);
    const double kShown = g_credits_shown, kFade = kShown < kCreditsShown ? kCreditsOut : .9;
    const double t = now - g_credits_at;
    if (t > kShown) return;
    const float a = (float)std::clamp(std::min(t / kIn, (kShown - t) / kFade), 0.0, 1.0);
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const Look k = touch_look();
    const Theme& th = theme();
    ImFont* font = ImGui::GetFont();
    ImFont* tf = title_font();
    const char* title = MH4U_APP_NAME;
    const std::string lines[] = {
        std::string(tr("Version")) + " " MH4U_APP_VERSION "  \xC2\xB7  " + tr("by") + " khorne-hub",
        tr("Free and open source (GPL-3.0)"),
        std::string(tr("Official download:")) + " github.com/khorne-hub/4u-native",
        tr("Based on MH4UltimateDC by CompaLarios18"),
    };
    const char* warns[] = {tr("Only the links khorne-hub gives are official."),
                           tr("Got it elsewhere, paid for it or it has ads? It is not the official one.")};
    // (sized to the screen's height, smaller when it doesn't fit its width)
    float f = std::floor(std::clamp(vp->Size.y * .034f, 12.f, 44.f)), w = 0;
    for (int pass = 0; pass < 2; pass++) {
        w = tf->CalcTextSizeA(std::floor(f * 1.5f), FLT_MAX, 0, title).x;
        for (const std::string& l : lines) w = std::max(w, font->CalcTextSizeA(f, FLT_MAX, 0, l.c_str()).x);
        for (const char* l : warns) w = std::max(w, font->CalcTextSizeA(std::floor(f * .85f), FLT_MAX, 0, l).x);
        const float room = vp->Size.x * .9f - f * 2.6f;
        if (w <= room) break;
        f = std::floor(f * room / w);
    }
    const float tsz = std::floor(f * 1.5f), small = std::floor(f * .85f), gap = std::floor(f * .4f), pad = std::floor(f * 1.3f);
    const float line = std::max(1.f, std::floor(f * .09f));
    const float h = tsz + gap * 3 + 4 * (f + gap) + gap + small * 2 + gap * .5f;
    const ImVec2 c(vp->Pos.x + vp->Size.x * .5f, vp->Pos.y + vp->Size.y * .5f);
    const ImRect box(ImVec2(std::floor(c.x - w * .5f - pad), std::floor(c.y - h * .5f - pad)),
                     ImVec2(std::floor(c.x + w * .5f + pad), std::floor(c.y + h * .5f + pad)));
    // (the game's window: a wine-dark panel, a bronze edge, a gold rule inside and gold studs on its corners)
    dl->AddRectFilled(ImVec2(box.Min.x + line * 2, box.Min.y + line * 2), ImVec2(box.Max.x + line * 2, box.Max.y + line * 2),
                      fade(IM_COL32(0, 0, 0, 140), a));
    dl->AddRectFilled(box.Min, box.Max, fade(th.game ? IM_COL32(50, 25, 24, 246) : th.panel, a));
    dl->AddRect(box.Min, box.Max, fade(k.edge, a), 0, line * 1.6f);
    const float in = line * 3.2f;
    dl->AddRect(ImVec2(box.Min.x + in, box.Min.y + in), ImVec2(box.Max.x - in, box.Max.y - in), fade(k.rule, a * .75f), 0, line * .7f);
    for (ImVec2 p : {box.Min, ImVec2(box.Max.x, box.Min.y), ImVec2(box.Min.x, box.Max.y), box.Max}) diamond(dl, p, line * 2.6f, fade(k.rule, a));
    float y = box.Min.y + pad;
    ImVec2 ts = tf->CalcTextSizeA(tsz, FLT_MAX, 0, title);
    shadow_text(dl, tf, tsz, ImVec2(std::floor(c.x - ts.x * .5f), y), fade(th.accent_bright, a), title);
    y += tsz + gap * 1.5f;
    {  // (a gold rule with a diamond in its middle, as under the menu's tabs)
        const float half = std::min(w * .5f, f * 9);
        dl->AddLine(ImVec2(c.x - half, y), ImVec2(c.x - f * .6f, y), fade(k.rule, a * .6f), line * .7f);
        dl->AddLine(ImVec2(c.x + f * .6f, y), ImVec2(c.x + half, y), fade(k.rule, a * .6f), line * .7f);
        diamond(dl, ImVec2(c.x, y), f * .3f, fade(k.rule, a * .9f));
        diamond(dl, ImVec2(c.x, y), f * .13f, fade(k.band, a));
    }
    y += gap * 1.5f;
    for (int i = 0; i < 4; i++) {
        ImVec2 ls = font->CalcTextSizeA(f, FLT_MAX, 0, lines[i].c_str());
        shadow_text(dl, f, ImVec2(std::floor(c.x - ls.x * .5f), y), fade(i == 2 ? th.accent : th.text, a), lines[i].c_str());
        y += f + gap;
    }
    y += gap;
    for (const char* l : warns) {
        ImVec2 ws = font->CalcTextSizeA(small, FLT_MAX, 0, l);
        shadow_text(dl, small, ImVec2(std::floor(c.x - ws.x * .5f), y), fade(th.text_dim, a), l);
        y += small + gap * .5f;
    }
}

// ---------------------------------------------------------------- the thanks
// A card of thanks to the people who made the game and kept it alive, in the credits' style: the first time the app
// opens, once the credits have gone ([Interface] ThanksShown = 1 in mh4u-app.ini once it is let go), and again from
// Credits & Licenses. It stays until Continue, a tap, A, B or Back lets it go.
//
// The same card tells, once, what playing online means (online_then: before the lobby is first asked for its rooms, or
// a room on the internet first joined): the server the phone connects to, and that the servers it connects to see the
// player's IP address. Let go, [Network] OnlineNoticeShown = 1 and what waited for it goes on. MHGU for Android shows
// the same card with the same text before its first hub search.
bool g_thanks_open = false, g_thanks_held = false, g_card_online = false;
bool g_card_press = false;  // (a press made while the card showed has not let go yet: not the menu's)
double g_thanks_at = 0;
void thanks_show() {
    g_thanks_open = true, g_thanks_held = false, g_thanks_at = SDL_GetTicks() / 1000.0, g_card_online = false;
    std::fprintf(stderr, "[Info] App: the thanks\n");
}
void online_then(const std::string& server, std::function<void()> fn) {
    if (g_online_seen || server.empty()) {
        fn();
        return;
    }
    g_online_then = std::move(fn);
    g_online_server = server;
    g_thanks_open = true, g_thanks_held = false, g_thanks_at = SDL_GetTicks() / 1000.0, g_card_online = true;
    std::fprintf(stderr, "[Info] App: the notice about playing online (%s)\n", server.c_str());
}
void thanks_close() {
    if (SDL_GetTicks() / 1000.0 - g_thanks_at < .5) return;  // (the press that opened it, or a finger already down)
    g_thanks_open = g_thanks_held = false;
    // (ImGui got the tap that closed it too, from SDL's mouse events: over the menu, opened under the notice about playing
    // online, it reached the row under the finger - Reset this tab under Continue: card_input)
    s.swallow = true, g_card_press = true;
    if (g_card_online) {
        g_card_online = false;
        g_online_seen = true;
        app_setting_set("Network", "OnlineNoticeShown", "1");
        app_settings_save();
        std::function<void()> then = std::move(g_online_then);
        g_online_then = nullptr;
        if (then) then();
        return;
    }
    if (!app_setting_bool("Interface", "ThanksShown", false)) {
        app_setting_set("Interface", "ThanksShown", "1");
        app_settings_save();
    }
}
// the first time: after the credits shown when the app opens
void thanks_first_run() {
    static int state = 0;  // 0 not asked yet, 1 waiting for the credits to go, 2 done
    if (state == 2) return;
    if (state == 0) state = app_setting_bool("Interface", "ThanksShown", false) ? 2 : 1;
    if (state == 1 && g_credits_at >= 0 && SDL_GetTicks() / 1000.0 - g_credits_at > g_credits_shown + .2)
        thanks_show(), state = 2;
}
// Each frame of the menu's, before its rows: while the card shows, and until the press that closed it has let go, the
// mouse (a finger, as SDL's mouse events) is the card's, so nothing under it is pressed, held or tapped
void card_input() {
    if (g_thanks_open) g_card_press = true;
    if (!g_card_press) return;
    ImGui::SetKeyOwner(ImGuiKey_MouseLeft, 0x63617264, ImGuiInputFlags_LockThisFrame);
    ImGui::GetCurrentContext()->HoveredWindow = nullptr;
    s.swallow = true;
    if (!g_thanks_open && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        g_card_press = false;
}
void draw_thanks() {
    if (!g_thanks_open) return;
    const double t = SDL_GetTicks() / 1000.0 - g_thanks_at;
    const float a = (float)std::clamp(t / .35, 0.0, 1.0);
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const Look k = touch_look();
    const Theme& th = theme();
    ImFont* font = ImGui::GetFont();
    ImFont* tf = title_font();
    const std::string said = g_card_online ? fmt(ONLINE_TEXT, g_online_server.c_str()) : "";
    const char* title = tr(g_card_online ? "Playing online" : "Thank you");
    const char* body = g_card_online ? said.c_str() : tr(THANKS_BODY);
    const char* sign = g_card_online ? "" : "\xE2\x80\x94 khorne-hub";
    const char* cont = tr("Continue");
    dl->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), fade(IM_COL32(0, 0, 0, 150), a));
    const float f = std::floor(std::clamp(vp->Size.y * .036f, 12.f, 44.f)), tsz = std::floor(f * 1.5f);
    const float w = std::floor(std::min(vp->Size.x * .7f, f * 26)), pad = std::floor(f * 1.3f), gap = std::floor(f * .45f);
    const float line = std::max(1.f, std::floor(f * .09f));
    const ImVec2 bs = font->CalcTextSizeA(f, FLT_MAX, w, body);
    const float bh = std::floor(f * 2.1f);
    const float h = tsz + gap * 3 + bs.y + gap * 2 + (*sign ? f + gap * 2 : 0) + bh;
    const ImVec2 c(vp->Pos.x + vp->Size.x * .5f, vp->Pos.y + vp->Size.y * .5f);
    const ImRect box(ImVec2(std::floor(c.x - w * .5f - pad), std::floor(c.y - h * .5f - pad)),
                     ImVec2(std::floor(c.x + w * .5f + pad), std::floor(c.y + h * .5f + pad)));
    // (the credits' window: a wine-dark panel, a bronze edge, a gold rule inside and gold studs on its corners)
    dl->AddRectFilled(ImVec2(box.Min.x + line * 2, box.Min.y + line * 2), ImVec2(box.Max.x + line * 2, box.Max.y + line * 2),
                      fade(IM_COL32(0, 0, 0, 140), a));
    dl->AddRectFilled(box.Min, box.Max, fade(th.game ? IM_COL32(50, 25, 24, 248) : th.panel, a));
    dl->AddRect(box.Min, box.Max, fade(k.edge, a), 0, line * 1.6f);
    const float in = line * 3.2f;
    dl->AddRect(ImVec2(box.Min.x + in, box.Min.y + in), ImVec2(box.Max.x - in, box.Max.y - in), fade(k.rule, a * .75f), 0, line * .7f);
    for (ImVec2 p : {box.Min, ImVec2(box.Max.x, box.Min.y), ImVec2(box.Min.x, box.Max.y), box.Max}) diamond(dl, p, line * 2.6f, fade(k.rule, a));
    float y = box.Min.y + pad;
    const ImVec2 ts = tf->CalcTextSizeA(tsz, FLT_MAX, 0, title);
    shadow_text(dl, tf, tsz, ImVec2(std::floor(c.x - ts.x * .5f), y), fade(th.accent_bright, a), title);
    y += tsz + gap * 1.5f;
    {  // (the credits' gold rule with a diamond in its middle)
        const float half = std::min(w * .5f, f * 9);
        dl->AddLine(ImVec2(c.x - half, y), ImVec2(c.x - f * .6f, y), fade(k.rule, a * .6f), line * .7f);
        dl->AddLine(ImVec2(c.x + f * .6f, y), ImVec2(c.x + half, y), fade(k.rule, a * .6f), line * .7f);
        diamond(dl, ImVec2(c.x, y), f * .3f, fade(k.rule, a * .9f));
        diamond(dl, ImVec2(c.x, y), f * .13f, fade(k.band, a));
    }
    y += gap * 1.5f;
    shadow_text(dl, font, f, ImVec2(std::floor(c.x - w * .5f), y), fade(th.text, a), body, w);
    y += bs.y + gap * 2;
    if (*sign) {  // (the thanks' signature; the notice about playing online has none)
        const ImVec2 ss = font->CalcTextSizeA(f, FLT_MAX, 0, sign);
        shadow_text(dl, font, f, ImVec2(std::floor(c.x + w * .5f - ss.x), y), fade(th.accent, a), sign);
        y += f + gap * 2;
    }
    {  // Continue: a plate as the menu's buttons, the controller's A on it after a controller's press
        const bool pad_last = controls_last_input_pad();
        const ImVec2 cs = font->CalcTextSizeA(f, FLT_MAX, 0, cont);
        const float gw = pad_last ? f * 1.2f : 0, pw = cs.x + gw + f * 2.4f;
        const ImRect pr(ImVec2(std::floor(c.x - pw * .5f), y), ImVec2(std::floor(c.x + pw * .5f), y + bh));
        dl->AddRectFilled(pr.Min, pr.Max, fade(g_thanks_held ? k.lit : k.plate, a));
        dl->AddRect(pr.Min, pr.Max, fade(k.edge, a), 0, line);
        const float pin = line * 1.8f;
        dl->AddRect(ImVec2(pr.Min.x + pin, pr.Min.y + pin), ImVec2(pr.Max.x - pin, pr.Max.y - pin), fade(k.rule, a * .7f), 0,
                    std::max(1.f, line * .45f));
        for (ImVec2 p : {pr.Min, ImVec2(pr.Max.x, pr.Min.y), ImVec2(pr.Min.x, pr.Max.y), pr.Max}) diamond(dl, p, line * 1.5f, fade(k.rule, a));
        float x = pr.Min.x + f * 1.2f;
        if (pad_last) {
            SettingsIcons::Ink ink;
            ink.plate = k.plate, ink.rim = k.edge, ink.rule = k.rule, ink.letter = k.ink;
            SettingsIcons::draw(dl, font, ImVec2(x, pr.GetCenter().y - f * .5f), f, SettingsIcons::A, ink);
            x += gw;
        }
        shadow_text(dl, font, f, ImVec2(x, std::floor(pr.GetCenter().y - f * .55f)), fade(g_thanks_held ? k.glow : k.ink, a), cont);
    }
}

// The start's pipeline list (mh4n pipeline_progress.h; 1.0's S30, MHGU's overlay.cpp shader_plate the same): "Building
// shaders" with how far it is, once the list has compiled for 3 s since the game's first pictures (a warm cache is done
// within a second or two and shows nothing) and while it lasts - a first start, a cleared cache, a new GPU driver - so
// the player knows why effects come late and the first areas stutter; "Shaders built" for a moment once it is done
float shader_plate(float y) {
    constexpr double kDue = 3, kDone = 2.5;
    static double first = -1, shown = -1, done_at = -1;
    static bool finished = false;
    if (finished || g_present_count < 30) return y;
    const double now = SDL_GetTicks() / 1000.0;
    if (first < 0) first = now;
    const mh4n::pipeline_progress::State p = mh4n::pipeline_progress::Get();
    if (p.compiling) {
        if (now - first < kDue || p.total == 0) return y;
        if (shown < 0) {
            shown = now;
            std::fprintf(stderr, "[Info] App: the shader plate: %u of %u built\n", p.done, p.total);
        }
        const unsigned pct = p.total ? p.done * 100u / p.total : 0u;
        char text[200];
        std::snprintf(text, sizeof text,
                      spanish() ? "Armando shaders: %u de %u (%u %%). Los efectos pueden aparecer tarde hasta que terminen."
                                : "Building shaders: %u of %u (%u%%). Effects may show late until they are done.",
                      p.done, p.total, pct);
        return notice_plate(y, (float)std::clamp((now - shown) / .3, 0.0, 1.0), "", text, 0, 0);
    }
    if (shown < 0) {  // (done before the plate was due: nothing to say)
        if (now - first >= kDue) finished = true;
        return y;
    }
    if (done_at < 0) {
        done_at = now;
        std::fprintf(stderr, "[Info] App: the shader plate: done, %.1f s after it showed\n", now - shown);
    }
    if (now - done_at >= kDone) {
        finished = true;
        return y;
    }
    return notice_plate(y, (float)std::clamp((kDone - (now - done_at)) / .5, 0.0, 1.0), "",
                        spanish() ? "Shaders armados." : "Shaders built.", 0, 0);
}
// PowerVR's last resort (perf_hint.c mem_watch, shared_screens.cpp: the process's map entries near the system's most, as
// a BXM-8-256's driver made them until it closed, 4u-native #31): once the screens go to the app as pixels, the player is
// told why the frame rate dropped, for longer than the other notices
void valve_notice() {
    if (!shared_screens_valve_notice_take()) return;
    settings_panel_notice(
        "The phone's GPU driver was running out of memory maps. So that the game doesn't close, the screens now take a "
        "slower path: fewer frames until you start the game again.",
        "El driver de la GPU del teléfono se estaba quedando sin mapas de memoria. Para que el juego no se cierre, las "
        "pantallas toman ahora un camino más lento: menos cuadros hasta que vuelvas a iniciar el juego.",
        15);
}
void draw_notices() {
    constexpr double kShown = 6, kFade = .6;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float y = driver_notice(vp->Pos.y + (perfstats_visible() ? .085f : .015f) * vp->Size.y);
    valve_notice();
    y = app_notice(y);
    y = shader_plate(y);
    const std::vector<NetLine>& lines = net_lines();
    if (lines.empty() || !s.draft.notices) return;
    double now = SDL_GetTicks() / 1000.0;
    std::vector<const NetLine*> show;
    // (someone's chat line: here as in 0.1.9.3 while the room chat is off, as it comes; on, it is the chat's - over the game
    // beside its button, chat_app.cpp)
    const bool room_chat = chat::hub_chat();
    for (auto it = lines.rbegin(); it != lines.rend() && show.size() < 3; ++it) {
        if (it->serial <= s.seen_line || now - it->time > kShown) break;
        if (it->kind != NetLine::Mine && (it->kind != NetLine::Chat || !room_chat)) show.insert(show.begin(), &*it);
    }
    for (const NetLine* l : show) {
        float a = (float)std::clamp((kShown - (now - l->time)) / kFade, 0.0, 1.0);
        bool chat = l->kind == NetLine::Chat;
        ImU32 ink = line_ink(*l);
        y = notice_plate(y, a, chat ? l->nick + ":" : "", line_text(*l), ink, chat || !ink ? 0 : ink);
    }
}

}  // namespace

void settings_panel_open_chat() { g_chat_asked = true; }
#ifndef __ANDROID__
namespace {
std::atomic<bool> g_chat_type{false};  // (the PC's chat key: the chat's keyboard too)
}
void settings_panel_type_chat() {
    g_chat_type = true;
    g_chat_asked = true;
}
#endif

bool settings_spanish() {
    return spanish_for(std::max(0, index_of(LANGUAGE_KEYS, app_setting("Interface", "Language", "auto"))));
}

void settings_panel(bool* open) {
    if (!g_started) start();
    net_poll();  // (the room's events, open or not: the chat and the notices)
    chat_app_init();
    chat_app_frame();  // (the port's chat: its state; over the game its button and last lines, chat_app.cpp)
    if (!*open) {
        controls_capture(false, false);  // (no key chosen with the menu closed)
        controls_pad_capture(false);
        if (s.editing) {  // (closed from outside)
            s.editing = false;
            closed_now = true;
        }
        g_chat_afloat = g_chat_typed = false;  // (1.0.2: no chat window afloat)
        text_input(0);
        is_dirty = false;
        if (perfstats_visible()) {
            ImGuiIO& io = ImGui::GetIO();
            perfstats_draw(io.DisplaySize.x, io.DisplaySize.y);
        }
        draw_notices();
        draw_credits();
        thanks_first_run();
        draw_thanks();
        return;
    }
    if (!s.editing) {
        s.draft = s.saved = load();
        if (!s.boot_set) s.boot = s.saved, s.boot_set = true;
        s.editing = true;
        s.view = View::Main;
        s.at.clear();
        s.help_open.clear();
        s.origins.clear();
        s.focus.clear();
        s.focus_request = true;
        s.error.clear();
        s.saved_notice = false;
        s.dropdown.open = false;
        s.drag = {};
        s.armed = false, s.opened_at = ImGui::GetTime();  // (the tap or the key that opened it is not one on the menu)
        s.closing = false;
        s.restore_scroll = true;
        s.scroll = 0;
        s.driver_note.clear();
        s.lobby_asked.clear();
        g_hubs_asked.clear();
        s.reopen.clear();
        if (!g_online_seen) g_online_seen = app_setting_bool("Network", "OnlineNoticeShown", false);
        request_back = false;
        request_close = false;
    }
    if (g_chat_asked.exchange(false)) {  // (the chat button over the game: settings_panel_open_chat)
        first_page(3);  // (Multiplayer: page_ids)
        push(View::Chat, "chat.say");
#ifndef __ANDROID__
        if (g_chat_type.exchange(false)) pc_chat_say_now();  // (pc/settings_pc_net.inc)
#endif
    }
    // (1.0.2, after tabarong's floating chat) what was asked of the chat window afloat (ui.cpp, the chat button:
    // settings_panel_chat_request)
    switch (request_chat.exchange(0)) {
    case SETTINGS_CHAT_CLOSE:  // (a tap away from it while typing, the chat button again: it closes, the keyboard with it)
        if (chat_window_view()) {
            if (s.view == View::Keyboard) pop();  // (what was being typed is let go)
            std::fprintf(stderr, "[chat] the chat window closed by a tap away from it or the chat button\n");
            ask_close(open);
        }
        break;
    case SETTINGS_CHAT_TO_MENU:  // (the settings button: the menu in its place, typing stopped)
        if (chat_window_view()) {
            while (chat_window_view() && !s.origins.empty()) pop();
            if (chat_window_view()) first_page(0);
            std::fprintf(stderr, "[chat] the settings button: from the chat window to the menu\n");
        }
        break;
    default: break;
    }
    // (afloat, a menu of the game's opening under it - a shop, the quest counter -: hidden until that menu closes, typing
    // stopped; the touch controls are the game's meanwhile. Opened while one is already open - the title, a shop -, it
    // shows: the chat button was tapped for it)
    {
        static bool afloat_was, menu_was, hide;
        static int seen_frame = -10;  // (a frame missed - the menu was closed meanwhile -: afloat again from now)
        if (ImGui::GetFrameCount() != seen_frame + 1) afloat_was = false;
        seen_frame = ImGui::GetFrameCount();
        const bool afloat = *open && chat_floating(), menu = afloat && touch_game_menu();
        if (!afloat || !afloat_was) hide = false;
        else if (menu && !menu_was) hide = true;
        if (!menu) hide = false;
        afloat_was = afloat, menu_was = menu;
        g_chat_hidden = hide;
        if (hide && s.view == View::Keyboard) pop();
    }
    // the letters' size: from the screen's height, and the menu's own size
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImGuiStyle& style = ImGui::GetStyle();
    float H = vp->WorkSize.y;
    // (50 % to 200 %: from a small phone's tight fit to a tablet's or a TV's far-away screen)
    float px = std::clamp(.034f * H * s.draft.menu_scale / 100.f, .016f * H, .07f * H);
    ImGui::PushFont(nullptr, px / std::max(.01f, style.FontScaleMain * style.FontScaleDpi));
    card_input();
    if (*open && !g_chat_hidden) touch_panel(open);
    ImGui::PopFont();
    {  // (1.0.2, after tabarong's floating chat: the chat window afloat and its controls' places, for ui.cpp's fingers)
        const bool afloat = *open && chat_floating();
        std::lock_guard<std::mutex> l(g_claim_lock);
        if (afloat && !g_chat_hidden)
            g_claims.swap(g_claims_next);
        else
            g_claims.clear();
        g_chat_afloat = afloat;
        g_chat_typed = afloat && !g_chat_hidden && s.view == View::Keyboard;
    }
    g_claims_next.clear();
    // (typed text while the keyboard is up, the phone's own keyboard only when asked for; the room's lines: seen)
    text_input(*open && s.view == View::Keyboard ? std::max(1, g_text_input) : 0);
    if (!net_lines().empty()) s.seen_line = net_lines().back().serial;
    is_dirty = *open && (dirty() || confirmation());
    shared_screens_set_colour(colour_of(*open ? s.draft : s.saved));  // (Graphics & Audio > Colour: the screens follow the draft)
#ifndef __ANDROID__
    pc_hdr_preview((*open ? s.draft : s.saved).pc.hdr_brightness, (*open ? s.draft : s.saved).pc.hdr_highlights);
#endif
    {  // (PS1 style on the top screen: the PS1 filter and Brick's sharp pixels, the same)
        const Draft& now = *open ? s.draft : s.saved;
        shared_screens_set_ps1(now.ps1_filter, now.low_end == LOW_END_BRICK);
        // (FSR 1 on the top screen, the same; never with PS1 style, whose pixels are meant to be blocks)
        fsr1_set(now.fsr1 && !now.ps1_filter && now.low_end != LOW_END_BRICK, now.fsr1_sharpness / 100.0f);
        shared_screens_set_fast_top(now.fast_top);  // (FastTopScreen: the same)
    }
    {  // (1.0.2) Screen and HUD > HUD size: the game's HUD follows the row at once, before Save (mh4u.ini keeps it at Save;
       // closed without saving, the saved one goes back). The game, held while the settings are open, runs for a moment
       // so it draws the HUD at that size (touch_hud_size_changed)
        static std::string shown;
        const std::string& now = (*open ? s.draft : s.saved).hud_size;
        if (now != shown) {
            if (!shown.empty() && game_option_preview("hud_size", now)) {
                touch_hud_size_changed();
                if (*open) g_hud_peek_ns = SDL_GetTicksNS();  // (the menu faded a moment: hud_peek)
            }
            shown = now;
        }
    }
    if (!*open) {
        s.editing = false;
        s.dropdown.open = false;
        closed_now = true;
        padmap_cancel();  // (a controller's map under way ends with the menu)
        s.pad_note.clear();
        bool edit = s.closing;
        s.closing = false;
        if (edit) touch_edit_begin();  // (Customize controls and HUD: the editor, now the menu is gone)
    }
    if (*open && g_credits_asked) draw_credits();
    draw_thanks();
}

bool settings_panel_thanks_open() { return g_thanks_open; }

bool settings_panel_thanks_event(const SDL_Event& e) {
    credits_tap(e);  // (the credits shown at the start close at a tap: draw_credits)
    if (!g_thanks_open) return false;
    switch (e.type) {
    case SDL_EVENT_FINGER_DOWN:
        g_thanks_held = true;
        return true;
    case SDL_EVENT_FINGER_UP:
        if (g_thanks_held) thanks_close();
        g_thanks_held = false;
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH || e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST ||
            e.gbutton.button == SDL_GAMEPAD_BUTTON_START || e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK)
            thanks_close();
        return true;
    case SDL_EVENT_KEY_DOWN:
        if (!e.key.repeat && (e.key.scancode == SDL_SCANCODE_AC_BACK || e.key.scancode == SDL_SCANCODE_ESCAPE ||
                              e.key.scancode == SDL_SCANCODE_RETURN || e.key.scancode == SDL_SCANCODE_SPACE ||
                              e.key.scancode == SDL_SCANCODE_O || e.key.scancode == SDL_SCANCODE_F))
            thanks_close();
        return true;
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_CANCELED:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_MOTION:
        return true;
    default:
        return false;
    }
}

bool settings_panel_event(const SDL_Event& e, bool open) {
    if (!open) return false;
    switch (e.type) {
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_TEXT_INPUT:
        return true;
    default:  // (finger, button and key releases: the touch controls and the game let go of what they held)
        return false;
    }
}

void settings_panel_request_back() { request_back = true; }
bool settings_panel_chat_floating() { return g_chat_afloat.load(); }
bool settings_panel_chat_typing() { return g_chat_typed.load(); }
void settings_panel_chat_request(int what) { request_chat.store(what); }
bool settings_panel_chat_claims(float fx, float fy) {
    std::lock_guard<std::mutex> l(g_claim_lock);
    if (!g_chat_afloat.load()) return false;
    for (const ImRect& r : g_claims)
        if (r.Contains(ImVec2(fx, fy))) return true;
    return false;
}

void settings_panel_request_close() { request_close = true; }

void settings_panel_notice(const std::string& english, const std::string& spanish, double seconds) {
    g_notice_en = english, g_notice_es = spanish;
    g_notice_seconds = seconds;
    g_notice_since = -2;
}

bool settings_panel_closed_now() { return closed_now.exchange(false); }

bool settings_panel_dirty() { return is_dirty.load(); }
