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

#include <jni.h>
#include <strings.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "app_jni.h"
#include "app_live.h"
#include "app_settings.h"
#include "content.h"
#include "controls.h"
#include "game_options.h"
#include "gpu_driver.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "net_lan.h"
#include "net_rooms.h"
#include "padmap.h"
#include "perf_hint.h"
#include "perfstats.h"
#include "app_version.h"
#include "settings_icons.h"
#include "settings_locale.h"
#include "touch.h"
#include "ui.h"

namespace {

// ---------------------------------------------------------------- what the menu edits
constexpr int PAGES = 9;
enum { PAGE_GRAPHICS, PAGE_DISPLAY, PAGE_CONTROLS, PAGE_GAMEPLAY, PAGE_NETWORK, PAGE_MODS, PAGE_CHEATS, PAGE_MENU, PAGE_ABOUT };
const char* const page_names[PAGES] = {"Graphics & Audio", "Display", "Controls", "Gameplay", "Multiplayer", "Mods", "Cheats", "Menu Options",
                                       "Credits & Licenses"};
namespace rooms = mh4n::rooms;
void credits_show();  // (the credits shown when the app opens: draw_credits, further down)
void thanks_show();   // (the thanks card: draw_thanks, further down)
const char* const THANKS_BODY = "To everyone who created this game, and to everyone who kept it alive over the years: thank you. This work is a love letter to the Monster Hunter series, with my deepest respect to the people who develop it.";
enum { THEME_HUNTER, THEME_GOLD, THEME_BLUE };
const char* const THEME_KEYS[] = {"hunter", "gold", "blue"};
const char* const LANGUAGE_KEYS[] = {"auto", "en", "es"};
const char* const LAYOUT_KEYS[] = {"overlay", "stacked", "side"};
const char* const CORNER_KEYS[] = {"bottom-right", "bottom-left", "top-right", "top-left"};
const char* const CONTROLS_KEYS[] = {"auto", "on", "off"};
const char* const WIDE_KEYS[] = {"match", "16:9", "off"};
// [Controls] of mh4u-app.ini: a controller's (controls.h)
const char* const FACE_KEYS[] = {"position", "label"};
const char* const STICK_KEYS[] = {"camera", "cstick"};
const char* const R3_KEYS[] = {"lockon", "L", "ZL", "ZR", "off"};
const char* const TOWN_KEYS[] = {"select", "l3", "r3", "off"};  // [Controls] TownMenu (town.h)

// The touch controls' buttons that can be hidden (touch.cpp's TB_KEY, show_<key>), and whether each shows at first
struct TouchButton {
    const char *key, *title;
    bool on;
    int group;  // 0 hunting, 1 bows and bowguns, 2 menus and the rest
};
const TouchButton TOUCH_BUTTONS[] = {
    {"attacks", "Attacks (A / X)", true, 0},        {"dodge", "Evade (B)", true, 0},
    {"guard", "Guard / dash (R)", true, 0},         {"sheathe", "Sheathe (Y)", true, 0},
    {"zr", "Kick (ZR)", true, 0},                   {"zl", "Target (ZL)", true, 0},
    {"l", "Camera (L)", true, 0},                   {"stick", "Movement stick", true, 0},
    {"select", "SELECT in hunts", false, 0},
    {"dpad", "D-pad in hunts", false, 0},           {"ranged", "Layout for bows and bowguns", true, 1},
    {"fire", "Fire / shoot", true, 1},              {"second", "Reload / melee", true, 1},
    {"aim", "Aim", true, 1},                        {"ranged_special", "Crouching fire / coating", true, 1},
    {"ammo", "Ammo arrows", true, 1},               {"start", "Game menu (START)", true, 2},
    {"screen", "Touch screen button", true, 2},
    {"menu_dpad", "D-pad in menus", true, 2},       {"item_arrows", "Item bar arrows", true, 0},
};
constexpr int TB = sizeof TOUCH_BUTTONS / sizeof *TOUCH_BUTTONS;

// The port's buttons (hooks.cpp PadBits), as the Gameplay tab offers them
const char* const BUTTON_KEYS[] = {"off", "ZR", "ZL", "R", "L", "X", "Y"};
const char* const END_KEYS[] = {"ZL+ZR", "L+R", "off"};

struct Draft {
    // mh4u.ini (game_options.h), as the file has them
    std::string fps, steady, widescreen, hud_edges, bloom, motion_blur, depth_of_field, radial_blur, haze, dither, antialiasing, fov;
    std::string kick, target, skip, skip_end, skip_end_hold;
    // mh4u-app.ini (app_settings.h)
    int scale = kRenderScaleDefault, volume = 100;  // scale: the 3D resolution, % of the phone's (0: the 3DS's)
    int low_end = LOW_END_OFF;
    bool stats = false, shadows = true, reverb = true;
    bool screen_shown = true;
    int layout = 0, corner = 0, size = 42, opacity = 100;
    bool haptics = true;
    bool lock_on = true;  // [Controls] LockOn: a long press on L, a pad's right stick click (touch.cpp)
    int language = 0, theme = THEME_HUNTER, menu_scale = 100;
    bool pause_background = true;
    // the touch controls (touch_layout.txt)
    int controls = 0, touch_opacity = 80, attack_style = 0;
    float camera = 1.0f;
    std::vector<char> show = std::vector<char>(TB, 1);
    // a controller's (mh4u-app.ini's [Controls], controls.h): FACE_KEYS, STICK_KEYS, R3_KEYS by index; the camera's
    // speed and the sticks' dead zone in %
    int face = 0, right_stick = 0, stick_speed = 100, r3 = 0, deadzone = 10, town = 0;
    bool invert_x = false, invert_y = false;
    std::string combo[6];  // the combined buttons: "A+X" (touch.cpp's combo1..combo6)
    // mh4u-app.ini's [Network] (net_rooms.h): no room and no lobby come with the app
    std::string nick, address, password, lobby, host_port = "24872";
    int slots = 4;
    bool notices = true;
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
    bool operator==(const Draft&) const = default;
};
// What each mod holds, as last read (the draft keeps only their order and switches), and the cheats' names in the order
// of Draft::cheats
std::vector<mh4n::mods::Mod> g_mod_info;
std::vector<std::string> g_cheat_names;

// The options as the app comes (first_run.cpp writes the same into a new mh4u.ini, and android_main.cpp adds a key the
// app's own mh4u.ini lacks): what Reset puts back, and what a key the files lack reads as (load)
Draft defaults() {
    Draft d;
    d.fps = "60", d.widescreen = "match", d.hud_edges = "1";
    d.bloom = d.motion_blur = d.depth_of_field = d.radial_blur = d.haze = "on";
    d.dither = render_scale_times(d.scale) > 1 ? "off" : "on";  // (a grid above 1x)
    d.antialiasing = "on", d.fov = "1";
    d.steady = "off";
    d.kick = "ZR", d.target = "ZL", d.skip = "ZR", d.skip_end = "ZL+ZR", d.skip_end_hold = "1.0";
    for (int i = 0; i < TB; i++) d.show[i] = TOUCH_BUTTONS[i].on;
    return d;
}
// The field of view's factor in mh4u.ini's text (0.5 to 2, 1 when it is not a number)
float fov_of(const std::string& text) {
    const float f = (float)std::atof(text.c_str());
    return f > 0 ? std::clamp(std::round(f * 20) / 20.f, .5f, 2.f) : 1.f;
}
// The frame rate's cap in mh4u.ini's text (30 to 240; the game's own 30 when it is not a number)
int fps_of(const std::string& text) {
    const int fps = (int)std::lround(std::atof(text.c_str()));
    return fps > 0 ? std::clamp(fps, 30, 240) : 30;
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
enum class View { Main, Buttons, ComboEdit, Keyboard, CloseChanges, Restart, Exit, Reset, RemoveDriver, RemoveMod, RemoveCheat, Conflicts,
                  PadList, PadMap };
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
    std::function<void(int)> adjust;
    std::function<void()> activate;
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
};
// A row's choices, in the picker over the menu: a tap picks, a tap away from it, its X or Back closes it
struct Dropdown {
    bool open = false, hovered = false;
    std::string row, title;  // the row it belongs to, and its title over the choices
    std::vector<std::string> names;
    int selected = 0;
    std::function<void(int)> set;
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
    int page = 0;
    View view = View::Main;
    std::string focus, error;
    std::vector<Origin> origins;
    Dropdown dropdown;
    Keyboard keyboard;
    std::vector<Target> targets;
    unsigned seen_line = 0;  // the room's last line shown in the menu (no notice over the game for it)
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
} s;
std::atomic<bool> request_back{false}, request_close{false}, closed_now{false}, is_dirty{false};
bool settled() { return ImGui::GetTime() - s.opened_at > .25; }
bool taps_ok() { return s.armed && !s.swallow; }
bool g_started = false;  // (the FPS counter set from the saved options, once)
}  // namespace
extern "C" volatile uint64_t g_present_count;  // (android_main.cpp: the game's frames shown)
namespace {
std::string g_notice_en, g_notice_es;  // settings_panel_notice's, shown from g_notice_since (s; -1: none; -2: from
double g_notice_since = -1;            // the game's first pictures: one told at the start would be gone before them)

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
const char* tr(const char* text) { return settings_text(spanish() ? 1 : 0, text); }
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
// The options as they are now: mh4u.ini's, mh4u-app.ini's, the touch controls'
Draft load() {
    const Draft f = defaults();  // (a key the files lack: as the app comes, which android_main.cpp makes it at the start)
    Draft d = f;
    d.fps = game_option_get("fps", f.fps);
    d.steady = game_option_get("steady", f.steady);
    d.widescreen = game_option_get("widescreen", f.widescreen);
    d.hud_edges = game_option_get("hud_edges", f.hud_edges);
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
    d.scale = std::clamp(app_setting_int("Graphics", "RenderScale", kRenderScaleDefault), 0, 100);
    d.dither = game_option_get("dither", render_scale_times(d.scale) > 1 ? "off" : "on");  // (android_main.cpp)
    d.low_end = app_low_end_level(app_setting("Graphics", "LowEnd", "off"));
    d.stats = app_setting_bool("Graphics", "FpsCounter", false);
    d.shadows = app_setting_bool("Graphics", "Shadows", true);
    d.volume = std::clamp(app_setting_int("Audio", "Volume", 100), 0, 100);
    d.reverb = app_setting_bool("Audio", "Reverb", true);
    d.screen_shown = ui_bottom_wanted();  // (as it is now: its button on the screen changes it too)
    d.layout = std::max(0, index_of(LAYOUT_KEYS, app_setting("Display", "Layout", "overlay")));
    d.corner = std::max(0, index_of(CORNER_KEYS, app_setting("Display", "Corner", "bottom-right")));
    d.size = std::clamp(app_setting_int("Display", "Size", 42), 20, 60);
    d.opacity = std::clamp(app_setting_int("Display", "Opacity", 100), 30, 100);
    d.haptics = app_setting_bool("Controls", "Haptics", true);
    d.face = std::max(0, index_of(FACE_KEYS, app_setting("Controls", "FaceButtons", "position")));
    d.right_stick = std::max(0, index_of(STICK_KEYS, app_setting("Controls", "RightStick", "camera")));
    d.stick_speed = std::clamp(app_setting_int("Controls", "StickSpeed", 100) / 25 * 25, 25, 300);
    d.invert_x = app_setting_bool("Controls", "InvertX", false);
    d.invert_y = app_setting_bool("Controls", "InvertY", false);
    d.r3 = std::max(0, index_of(R3_KEYS, app_setting("Controls", "R3", "lockon")));
    d.town = std::max(0, index_of(TOWN_KEYS, app_setting("Controls", "TownMenu", "select")));
    d.deadzone = std::clamp(app_setting_int("Controls", "Deadzone", 10), 0, 30);
    d.lock_on = app_setting_bool("Controls", "LockOn", true);
    d.language = std::max(0, index_of(LANGUAGE_KEYS, app_setting("Interface", "Language", "auto")));
    d.theme = std::max(0, index_of(THEME_KEYS, app_setting("Interface", "Theme", "hunter")));
    d.menu_scale = std::clamp(app_setting_int("Interface", "Scale", 100), 50, 200);
    d.pause_background = app_setting_bool("Interface", "PauseInBackground", true);
    std::string v = touch_setting("controls");
    d.controls = std::max(0, index_of(CONTROLS_KEYS, v));
    v = touch_setting("opacity");
    if (!v.empty()) d.touch_opacity = std::clamp((int)std::lround(std::atof(v.c_str()) * 100), 20, 100);
    v = touch_setting("camera_speed");
    if (!v.empty()) d.camera = std::clamp((float)std::round(std::atof(v.c_str()) * 20) / 20.f, .2f, 5.f);
    v = touch_setting("attack_style");
    if (!v.empty()) d.attack_style = std::clamp(std::atoi(v.c_str()), 0, 2);
    for (int i = 0; i < TB; i++) {
        v = touch_setting(std::string("show_") + TOUCH_BUTTONS[i].key);
        if (!v.empty()) d.show[i] = std::atoi(v.c_str()) != 0;
    }
    for (int k = 0; k < 6; k++) d.combo[k] = touch_setting("combo" + std::to_string(k + 1));
    d.nick = app_setting("Network", "Nickname");
    d.address = app_setting("Network", "Address");
    d.password = app_setting("Network", "Password");
    d.lobby = app_setting("Network", "Lobby");
    d.host_port = std::to_string(std::clamp(app_setting_int("Network", "HostPort", rooms::kDefaultPort), 1, 65535));
    d.slots = std::clamp(app_setting_int("Network", "Slots", 4), 2, 16);
    d.notices = app_setting_bool("Network", "Notices", true);
    gpu_driver_reload();
    d.driver = gpu_driver_status().chosen;
    g_mod_info = content_mods();
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
    app_setting_set("Network", "HostPort", d.host_port);
    app_setting_set_int("Network", "Slots", d.slots);
    app_setting_set("Network", "Notices", d.notices ? "on" : "off");
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
}

bool save() {
    const Draft &d = s.draft, &o = s.saved;
    // the port's: each one changed (they apply on the game's thread at its next frame, or at the next start)
    const std::pair<const char*, std::string Draft::*> game[] = {
        {"fps", &Draft::fps},       {"steady", &Draft::steady},           {"widescreen", &Draft::widescreen},
        {"hud_edges", &Draft::hud_edges},
        {"bloom", &Draft::bloom},   {"motion_blur", &Draft::motion_blur}, {"depth_of_field", &Draft::depth_of_field},
        {"radial_blur", &Draft::radial_blur}, {"haze", &Draft::haze},     {"dither", &Draft::dither},
        {"antialiasing", &Draft::antialiasing}, {"fov", &Draft::fov},
        {"kick", &Draft::kick},     {"target", &Draft::target},           {"skip", &Draft::skip},
        {"skip_end", &Draft::skip_end}, {"skip_end_hold", &Draft::skip_end_hold}};
    for (const auto& [key, field] : game)
        if (d.*field != o.*field) game_option_set(key, d.*field);
    // the app's
    static const char* const LOW_END_KEYS[] = {"off", "on", "toaster"};
    app_setting_set_int("Graphics", "RenderScale", d.scale);
    app_setting_set("Graphics", "LowEnd", LOW_END_KEYS[std::clamp(d.low_end, 0, 2)]);
    app_setting_set("Graphics", "FpsCounter", d.stats ? "on" : "off");
    app_setting_set("Graphics", "Shadows", d.shadows ? "on" : "off");
    app_setting_set_int("Audio", "Volume", d.volume);
    app_setting_set("Audio", "Reverb", d.reverb ? "on" : "off");
    app_setting_set("Interface", "PauseInBackground", d.pause_background ? "on" : "off");
    app_setting_set("Display", "TouchScreen", d.screen_shown ? "shown" : "hidden");
    app_setting_set("Display", "Layout", LAYOUT_KEYS[d.layout]);
    app_setting_set("Display", "Corner", CORNER_KEYS[d.corner]);
    app_setting_set_int("Display", "Size", d.size);
    app_setting_set_int("Display", "Opacity", d.opacity);
    app_setting_set("Controls", "Haptics", d.haptics ? "on" : "off");
    app_setting_set("Controls", "FaceButtons", FACE_KEYS[d.face]);
    app_setting_set("Controls", "RightStick", STICK_KEYS[d.right_stick]);
    app_setting_set_int("Controls", "StickSpeed", d.stick_speed);
    app_setting_set("Controls", "InvertX", d.invert_x ? "on" : "off");
    app_setting_set("Controls", "InvertY", d.invert_y ? "on" : "off");
    app_setting_set("Controls", "R3", R3_KEYS[d.r3]);
    app_setting_set("Controls", "TownMenu", TOWN_KEYS[d.town]);
    app_setting_set_int("Controls", "Deadzone", d.deadzone);
    app_setting_set("Controls", "LockOn", d.lock_on ? "on" : "off");
    app_setting_set("Interface", "Language", LANGUAGE_KEYS[d.language]);
    app_setting_set("Interface", "Theme", THEME_KEYS[d.theme]);
    app_setting_set_int("Interface", "Scale", d.menu_scale);
    set_network(d);
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
    if (d.attack_style != o.attack_style) touch.emplace_back("attack_style", std::to_string(d.attack_style));
    for (int i = 0; i < TB; i++)
        if (d.show[i] != o.show[i]) touch.emplace_back(std::string("show_") + TOUCH_BUTTONS[i].key, d.show[i] ? "1" : "0");
    for (int k = 0; k < 6; k++)
        if (d.combo[k] != o.combo[k]) touch.emplace_back("combo" + std::to_string(k + 1), d.combo[k]);
    for (const auto& [k, v] : touch) touch_set_setting(k, v);
    // what follows at once
    float fps = std::max(1.f, (float)std::atof(d.fps.c_str()));
    if (d.fps != o.fps) frame_cap(fps);

    if (d.volume != o.volume) app_live_volume(d.volume);
    if (d.reverb != o.reverb) app_live_reverb(d.reverb);
    if (d.shadows != o.shadows) app_live_shadows(d.shadows);
    perfstats_set(d.stats, fps, spanish());
    if (d.screen_shown != ui_bottom_wanted()) ui_toggle_bottom();
    s.saved = s.draft;
    s.saved_notice = true;
    s.error.clear();
    return true;
}

// A room joined or hosted: the Multiplayer tab's options it was done with are kept at once (the others wait for Save)
void remember_network() {
    Draft& o = s.saved;
    const Draft& d = s.draft;
    o.nick = d.nick, o.address = d.address, o.password = d.password, o.lobby = d.lobby;
    o.host_port = d.host_port, o.slots = d.slots;
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
    s.origins.push_back({s.view, s.focus, list_scroll()});
    s.view = view;
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
        focus(o.focus);
        s.scroll = o.scroll;
    }
    s.restore_scroll = true;
    s.error.clear();
    ImGui::ClearActiveID();
}
void first_page(int page) {
    s.dropdown.open = false;
    s.drag = {};
    s.page = page;
    s.view = View::Main;
    s.origins.clear();
    s.focus.clear();
    s.focus_request = true;
    s.restore_scroll = true;
    s.scroll = 0;
    s.error.clear();
}
bool confirmation() {
    return s.view == View::CloseChanges || s.view == View::Restart || s.view == View::Exit || s.view == View::Reset ||
           s.view == View::RemoveDriver || s.view == View::RemoveMod || s.view == View::RemoveCheat;
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
    else if (s.view != View::Main) {
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
void choices(const Row& row, std::vector<std::string> names, int selected, std::function<void(int)> set) {
    int first = std::clamp(selected, 0, (int)names.size() - 1);
    s.dropdown = {true, false, row.id, row.title, std::move(names), selected, std::move(set)};
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
// Toaster: 30 FPS, the 3DS's resolution, and the reverb off too (back to On, the frame rate and the reverb come back)
void low_end(int level) {
    Draft& d = s.draft;
    const int was = d.low_end;
    d.low_end = level;
    std::string* filters[] = {&d.bloom, &d.motion_blur, &d.depth_of_field, &d.radial_blur, &d.haze};
    if (level != LOW_END_OFF) {
        const int scale = level == LOW_END_TOASTER ? kToasterScale : kLowEndScale;
        if (was == LOW_END_TOASTER && d.scale == kToasterScale) d.scale = scale;  // (back to On)
        // (0 is the lowest of all: the 3DS's)
        if (scale == 0 || (d.scale != 0 && d.scale > scale)) d.scale = scale;
        const int cap = level == LOW_END_TOASTER ? kToasterFps : low_end_fps();
        if (fps_of(d.fps) > cap || (was == LOW_END_TOASTER && fps_of(d.fps) == kToasterFps)) d.fps = std::to_string(cap);
        for (std::string* f : filters) *f = onoff(*f, false);
        d.antialiasing = onoff(d.antialiasing, false);
        d.shadows = false;
        if (level == LOW_END_TOASTER) d.reverb = false;
        else if (was == LOW_END_TOASTER) d.reverb = true;
    } else {
        if (d.scale == kToasterScale || d.scale <= kLowEndScale) d.scale = kRenderScaleDefault;
        if (fps_of(d.fps) == kToasterFps) d.fps = "60";
        for (std::string* f : filters) *f = onoff(*f, true);
        d.antialiasing = onoff(d.antialiasing, true);
        d.shadows = d.reverb = true;
    }
}

void reset_page() {
    Draft f = defaults();
    Draft& c = s.draft;
    switch (s.page) {
    case PAGE_GRAPHICS:
        c.fps = f.fps, c.scale = f.scale, c.widescreen = f.widescreen, c.hud_edges = f.hud_edges, c.low_end = f.low_end;
        c.bloom = f.bloom, c.motion_blur = f.motion_blur, c.depth_of_field = f.depth_of_field;
        c.radial_blur = f.radial_blur, c.haze = f.haze, c.dither = f.dither, c.volume = f.volume, c.stats = f.stats;
        c.antialiasing = f.antialiasing, c.shadows = f.shadows, c.reverb = f.reverb;
        c.driver = f.driver;
        break;
    case PAGE_DISPLAY:
        c.screen_shown = f.screen_shown, c.layout = f.layout, c.corner = f.corner, c.size = f.size, c.opacity = f.opacity;
        c.fov = f.fov;
        break;
    case PAGE_CONTROLS:
        c.controls = f.controls, c.touch_opacity = f.touch_opacity, c.camera = f.camera, c.attack_style = f.attack_style;
        c.show = f.show, c.haptics = f.haptics, c.lock_on = f.lock_on;
        for (int k = 0; k < 6; k++) c.combo[k] = f.combo[k];
        c.face = f.face, c.right_stick = f.right_stick, c.stick_speed = f.stick_speed, c.r3 = f.r3, c.deadzone = f.deadzone;
        c.town = f.town;
        c.invert_x = f.invert_x, c.invert_y = f.invert_y;
        break;
    case PAGE_GAMEPLAY:
        c.kick = f.kick, c.target = f.target, c.skip = f.skip, c.skip_end = f.skip_end, c.skip_end_hold = f.skip_end_hold;
        break;
    case PAGE_NETWORK:  // (the room joined stays)
        c.nick = f.nick, c.address = f.address, c.password = f.password, c.lobby = f.lobby;
        c.host_port = f.host_port, c.slots = f.slots, c.notices = f.notices;
        break;
    case PAGE_MODS:  // (every mod on, in the order of their names: as without a list)
        std::sort(c.mods.begin(), c.mods.end(), [](const Draft::ModRow& a, const Draft::ModRow& b) { return a.name < b.name; });
        for (auto& m : c.mods) m.on = true;
        break;
    case PAGE_CHEATS:
        for (char& on : c.cheats) on = 0;
        break;
    case PAGE_MENU:
        c.language = f.language, c.theme = f.theme, c.menu_scale = f.menu_scale, c.pause_background = f.pause_background;
        break;
    case PAGE_ABOUT:  // (nothing to set)
        break;
    }
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
const char* say(const char* en, const char* es) { return spanish() ? es : en; }
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
    default: return l.text;
    }
}
ImU32 line_ink(const NetLine& l) {
    switch (l.kind) {
    case NetLine::Chat: case NetLine::Mine: return 0;
    case NetLine::Joined: case NetLine::YouJoined: return NET_GREEN;
    case NetLine::Failed: case NetLine::Kicked: case NetLine::Banned: return theme().warn;
    default: return touch_look().dim;
    }
}
std::string status_text(const rooms::Status& st) {
    switch (st.state) {
    case rooms::State::Joining: return tr(st.waiting_for_console ? "Waiting for the game..." : "Connecting...");
    case rooms::State::Joined: return tr(st.hosting ? "Hosting, in the room" : "In the room");
    case rooms::State::Moderator: return tr(st.hosting ? "Hosting, in the room" : "In the room, moderator");
    default: return st.hosting ? fmt("Room open on UDP %d", (int)st.hosted_port) : tr("Not in a room");
    }
}
ImU32 status_ink(const rooms::Status& st) {
    if (in_room(st)) return NET_GREEN;
    if (st.state == rooms::State::Joining) return touch_look().glow;
    return st.error != rooms::Error::None ? theme().warn : touch_look().dim;
}
std::string status_help(const rooms::Status& st) {
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

void join_room() {
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
    header(r, "net.lan.head", tr("Rooms on this Wi-Fi"));
    const LanSearch lan = net_lan_result();
    // (enabled while it searches: a second tap waits for the same answer)
    action(r, "net.lan", tr("Search this Wi-Fi"), []() { net_lan_search(); }, true, "LanSearch");
    r.back().value = lan.busy ? tr("Searching...") : !lan.done ? "" : lan.rooms.empty() ? tr("None found") : fmt("%d room(s)", (int)lan.rooms.size());
    if (lan.busy) r.back().ink = touch_look().glow;
    if (lan.done && !lan.busy && lan.rooms.empty())
        note(r, "net.lan.none",
             lan.no_network ? say("This phone is on no Wi-Fi: connect it to the host's Wi-Fi, or to the host's hotspot.",
                                  "Este teléfono no está en ninguna Wi-Fi: conéctalo a la Wi-Fi del anfitrión, o a su punto de acceso.")
                            : say("No room answered. The host's phone must be on this Wi-Fi, with a room open (Host a room) and the app open.",
                                  "Ninguna sala respondió. El teléfono del anfitrión debe estar en esta Wi-Fi, con una sala abierta (Crear una sala) y la app abierta."),
             touch_look().dim);
    if (lan.busy || !lan.done) return;
    int shown = 0;
    for (const LanRoom& room : lan.rooms) {
        Row row;
        row.id = "net.wroom." + std::to_string(++shown), row.title = room.name.empty() ? room.address : room.name;
        row.value = fmt("%d / %d", room.members, room.slots);
        if (room.password) row.value = std::string(tr("Password")) + "  " + row.value;
        if (room.own) row.value = std::string(tr("This phone")) + "  " + row.value;
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

void network_rows(std::vector<Row>& r) {
    Draft& c = s.draft;
    const rooms::Status st = rooms::GetStatus();
    const bool in = in_room(st), joining = st.state == rooms::State::Joining, mod = st.state == rooms::State::Moderator;
    {  // (where this phone is: its colour at a glance, the details in its explanation, the reason under it)
        Row row;
        row.id = "net.status", row.title = tr("Status"), row.value = status_text(st), row.ink = status_ink(st);
        row.help = status_help(st);
        r.push_back(std::move(row));
    }
    if (st.state == rooms::State::Idle && st.error != rooms::Error::None) note(r, "net.why", error_text(st.error), theme().warn);
    header(r, "net.room.head", tr("Room"));
    add_text(r, "net.nick", "Nickname", &c.nick, KB_NICK, 20, "Nickname", c.nick.empty() ? tr("Choose one") : c.nick,
             tr("4 to 20 letters, digits, spaces, '.', '_' or '-'."), check_nick);
    if (!rooms::ValidNickname(c.nick)) r.back().ink = theme().warn;
    add_text(r, "net.address", "Room address", &c.address, KB_ADDRESS, 128, "Address", c.address.empty() ? tr("No address") : c.address,
             tr("Its IP or name, and :port if it is not 24872."), check_address);
    add_text(r, "net.password", "Password", &c.password, KB_SECRET, 64, "Password",
             c.password.empty() ? tr("No password") : "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2",  // (U+2022)
             tr("The room's, if it has one."));
    if (in || joining)
        action(r, "net.join", tr(joining ? "Stop joining" : "Leave the room"), []() { net_leave(); }, true, "Leave");
    else
        action(r, "net.join", tr("Join the room"), []() { join_room(); }, true, "Join");
    lan_rows(r);
    header(r, "net.host.head", tr("Host a room on this phone"));
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
    if (in) {
        header(r, "net.members.head", fmt("Hunters in the room: %d / %d", (int)st.members.size(), (int)st.room.member_slots));
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
    header(r, "net.chat.head", tr("Chat"));
    action(r, "net.say", tr("Say something"),
           []() {
               open_keyboard(tr("Say something"), "", KB_CHAT, 400, "", [](const std::string& typed, std::string& error) {
                   if (typed.empty() || net_say(typed)) return true;
                   error = tr("You are not in a room.");
                   return false;
               });
           },
           in, "Say");
    {
        const std::vector<NetLine>& lines = net_lines();
        if (lines.empty()) note(r, "net.line.none", tr("Nothing has been said yet."), touch_look().dim);
        for (size_t i = lines.size() > 8 ? lines.size() - 8 : 0; i < lines.size(); i++) {
            const NetLine& l = lines[i];
            const bool chat = l.kind == NetLine::Chat || l.kind == NetLine::Mine;
            note(r, "net.line." + std::to_string(l.serial), line_text(l), line_ink(l), chat ? l.nick : "");
        }
    }
    add_bool(r, "net.notices", "Room messages over the game", &c.notices, "Notices");
    header(r, "net.lobby.head", tr("Public rooms"));
    add_text(r, "net.lobby", "Lobby", &c.lobby, KB_ANY, 200, "Lobby", c.lobby.empty() ? tr("No lobby") : c.lobby,
             tr("A lobby's address, as http://name:port. None comes with the app."));
    const NetLobby lobby = net_lobby();
    const bool answered = lobby.done && lobby.url == c.lobby;
    // (enabled while it asks, or the picked row would vanish under the finger: a second tap waits for the same answer)
    action(r, "net.refresh", tr("Show its rooms"), []() { net_lobby_fetch(s.draft.lobby); }, !c.lobby.empty(), "Refresh");
    r.back().value = lobby.busy ? tr("Asking...") : !answered ? "" : lobby.list.ok ? fmt("%d room(s)", (int)lobby.list.rooms.size()) : tr("No answer");
    if (lobby.busy) r.back().ink = touch_look().glow;
    if (answered && !lobby.list.ok) {  // (the runtime's reason, in English, goes to the log)
        r.back().ink = theme().warn;
        note(r, "net.lobby.why", say("The lobby did not answer with a list of rooms. Check its address: http://name:port.",
                                     "El lobby no respondió con una lista de salas. Revisa su dirección: http://nombre:puerto."),
             theme().warn);
    }
    if (answered && lobby.list.ok) {
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
}

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

// The section at the end of Graphics & Audio: the driver for the next start (the system's or one installed), the one
// running now, a driver that did not work, Restart when the choice isn't the one running, Import and Remove. A phone
// without an Adreno GPU gets only the row that says why it uses the system's driver.
void driver_rows(std::vector<Row>& r) {
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
    if (!st.adreno) {
        Row row;
        row.id = "driver.running", row.title = tr("GPU driver"), row.value = tr("System");
        row.help = st.gpu.empty() ? say("Drivers of your own are for Adreno GPUs only, and this phone's GPU is not one: it uses the system's driver.",
                                        "Los drivers propios son solo para GPU Adreno, y la de este teléfono no lo es: usa el driver del sistema.")
                                  : fmt(say("Drivers of your own are for Adreno GPUs only, and this phone's GPU is a %s: it uses the system's driver.",
                                            "Los drivers propios son solo para GPU Adreno, y la de este teléfono es una %s: usa el driver del sistema."),
                                        st.gpu.c_str());
        if (!st.driver.empty()) row.help += fmt(say(" Its driver: %s.", " Su driver: %s."), st.driver.c_str());
        r.push_back(std::move(row));
        return;
    }
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
        : in.error == "no_parts"      ? say("No romfs or exefs folder was found in it (up to three folders down).",
                                            "No se encontró una carpeta romfs ni exefs adentro (hasta tres carpetas de profundidad).")
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
        text += " " + std::string(say("The code library must be made on the PC with it turned on, or the game won't start.",
                                      "La librería de código tiene que hacerse en la PC con él activado, o el juego no arranca."));
    text += " " + std::string(say("A tap turns it on or off, moves it or removes it. Lower in the list wins where two mods change the same file. It loads when the game starts.",
                                  "Un toque lo activa o lo desactiva, lo mueve o lo quita. Más abajo en la lista gana donde dos mods cambian el mismo archivo. Se carga al iniciar el juego."));
    if (m.from_game_folder)
        text += " " + std::string(say("It comes from the mods folder beside the game: to remove it, delete it there.",
                                      "Viene de la carpeta mods al lado del juego: para quitarlo, bórralo de ahí."));
    return text;
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
                s.mods_note = fmt("Added: %s, off. It changes the game's code and the code library was not made with it: make the library again on the PC with this mod before turning it on, or the game won't start.", name.c_str());
                s.mods_note_bad = true;
            }
        }
    }
    action(r, "mods.add", tr("Add a mod (.zip)"), []() { content_mods_import(false); }, true, "ModImport");
    action(r, "mods.folder", tr("Add a mod (folder)"), []() { content_mods_import(true); }, true, "ModFolderImport");
    if (!s.mods_note.empty()) note(r, "mods.note", s.mods_note, s.mods_note_bad ? theme().warn : NET_GREEN);
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
            row.help = say("With the code mods turned on here, the game's code is not the code your library was made from: the game won't start. Make the library again on the PC with the same code mods on (step 1 of the guide, answering Yes and choosing the mods), or turn them back as they were.",
                           "Con los mods de código activados aquí, el código del juego no es el código con que se hizo tu librería: el juego no arranca. Haz la librería de nuevo en la PC con los mismos mods de código activados (el paso 1 de la guía, respondiendo Sí y eligiendo los mods), o déjalos como estaban.");
        }
        r.push_back(std::move(row));
    }
    if (s.saved.mods != s.boot.mods) {  // (mods are read when the game starts)
        action(r, "mods.restart", tr("Restart the game"), []() { push(View::Restart, "dialog.cancel"); });
        r.back().help = say("Mods load when the game starts: this closes the game and opens it again. Save your hunt in the game first.",
                            "Los mods se cargan al iniciar el juego: esto cierra el juego y lo abre de nuevo. Guarda antes tu cacería en el juego.");
    }
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
    action(r, "cheats.add", tr("Import cheats (.txt)"), []() { content_cheats_import(); }, ready, "CheatImport");
    if (!s.cheats_note.empty()) note(r, "cheats.note", s.cheats_note, s.cheats_note_bad ? theme().warn : NET_GREEN);
    if (!ready) {
        note(r, "cheats.wait", say("The game is starting: its cheats show in a moment.", "El juego está arrancando: sus trucos aparecen en un momento."),
             touch_look().dim);
        return;
    }
    header(r, "cheats.head", tr("Cheats"));
    if (list.empty())
        note(r, "cheats.none", say("No cheats yet. Import a .txt of cheats for MH4U USA 1.3.0 in the format of Azahar and Citra.",
                                   "Todavía no hay trucos. Importa un .txt de trucos para MH4U de EE. UU. 1.3.0 en el formato de Azahar y Citra."),
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
            row.help += " " + std::string(say("It writes into the game's code, which this port runs translated ahead of time: it does nothing here.",
                                              "Escribe en el código del juego, que este port ejecuta traducido de antemano: aquí no hace nada."));
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

std::vector<Row> main_rows(bool* open) {
    std::vector<Row> r;
    Draft& c = s.draft;
    switch (s.page) {
    case PAGE_GRAPHICS: {
        add_choice(r, "lowend", "Low-end mode", std::clamp(c.low_end, 0, 2), {tr("Off"), tr("On"), tr("Toaster")},
                   [](int v) { low_end(v); }, "LowEnd");
        // (the help of the mode it shows: what each one trades)
        r.back().help = help_of(c.low_end == LOW_END_TOASTER ? "LowEnd.toaster" : c.low_end == LOW_END_ON ? "LowEnd.on" : "LowEnd.off");
        {  // as MHGU's limit row: left / right one frame a second more or less, a tap the common ones
            static const int presets[] = {30, 60, 90, 120, 144};
            const int now = fps_of(c.fps);
            Row row;
            row.id = "fps", row.title = tr("Frame rate limit"), row.key = "fps", row.value = std::to_string(now);
            row.number = true;
            row.adjust = [now](int d) { s.draft.fps = std::to_string(std::clamp(now + d, 30, 240)); };
            Row copy = row;
            row.menu = true;
            row.activate = [copy, now]() {
                std::vector<std::string> names;
                int pick = -1, below = 0;
                for (int i = 0; i < (int)std::size(presets); i++) {
                    names.push_back(std::to_string(presets[i]));
                    if (presets[i] == now) pick = i;
                    if (presets[i] <= now) below = i;
                }
                choices(copy, names, pick, [](int v) { s.draft.fps = std::to_string(presets[v]); });
                if (pick < 0) focus("choice." + std::to_string(below));  // (not one of them: at the one below)
            };
            r.push_back(std::move(row));
        }
        add_switch(r, "steady", "Steady frame rate", &c.steady, "steady");
        {
            // as MHGU's: a percent of the phone's own resolution, with the whole multiple of the 3DS's it comes to
            static const int kPercents[] = {100, 75, 50, 33, 0};
            int pick = -1;
            for (int i = 0; i < (int)std::size(kPercents); i++)
                if (kPercents[i] == c.scale) pick = i;
            std::vector<std::string> names;
            for (int p : kPercents)
                names.push_back(p == 0 ? tr("3DS (1x)") : std::to_string(p) + "% (" + std::to_string(render_scale_times(p)) + "x)");
            add_choice(r, "scale", "3D resolution", pick, names, [](int v) { s.draft.scale = kPercents[v]; }, "RenderScale");
            if (pick < 0) r.back().value = std::to_string(c.scale) + "% (" + std::to_string(render_scale_times(c.scale)) + "x)";
        }
        r.back().cold = true;
        int wide = index_of(WIDE_KEYS, c.widescreen);
        add_choice(r, "wide", "Widescreen", wide, {tr("Match the screen"), "16:9", tr("Off")},
                   [](int v) { s.draft.widescreen = v == 0 ? "match" : v == 1 ? "16:9" : "off"; }, "widescreen", c.widescreen);
        r.back().cold = game_option_needs_restart("widescreen");
        add_switch(r, "hud", "HUD at the screen's edges", &c.hud_edges, "hud_edges");
        if (c.widescreen.empty() || c.widescreen == "0" || !strcasecmp(c.widescreen.c_str(), "off")) {  // (no wide picture)
            r.back().enabled = false;
            r.back().value = tr("Off (no widescreen)");
            r.back().toggle = -1;
        }
        add_switch(r, "aa", "Anti-aliasing", &c.antialiasing, "antialiasing");
        add_bool(r, "shadows", "Shadows", &c.shadows, "Shadows");
        header(r, "filters", tr("Screen filters"));
        add_switch(r, "bloom", "Bloom", &c.bloom, "bloom");
        add_switch(r, "motion", "Motion blur", &c.motion_blur, "motion_blur");
        add_switch(r, "dof", "Depth of field", &c.depth_of_field, "depth_of_field");
        add_switch(r, "radial", "Radial blur", &c.radial_blur, "radial_blur");
        add_switch(r, "haze", "Heat haze", &c.haze, "haze");
        add_switch(r, "dither", "Dither pattern", &c.dither, "dither");
        header(r, "audio", tr("Audio and readout"));
        add_number(r, "volume", "Volume", &c.volume, 0, 100, 1, "%d%%", "Volume");
        add_bool(r, "reverb", "Reverb", &c.reverb, "Reverb");
        add_bool(r, "stats", "FPS counter", &c.stats, "FpsCounter");
        driver_rows(r);
        break;
    }
    case PAGE_DISPLAY: {
        add_bool(r, "screen", "Show the touch screen", &c.screen_shown, "TouchScreen");
        add_choice(r, "layout", "Layout", c.layout, {tr("In a corner"), tr("Stacked"), tr("Side by side")},
                   [](int v) { s.draft.layout = v; }, "Layout");
        bool corner = c.layout == 0;
        add_choice(r, "corner", "Corner", c.corner, {tr("Bottom right"), tr("Bottom left"), tr("Top right"), tr("Top left")},
                   [](int v) { s.draft.corner = v; }, "Corner");
        r.back().enabled = corner;
        add_number(r, "size", "Size", &c.size, 20, 60, 2, "%d%%", "Size");
        r.back().enabled = corner;
        add_number(r, "opacity", "Opacity", &c.opacity, 30, 100, 5, "%d%%", "Opacity");
        r.back().enabled = corner;
        header(r, "view.head", tr("The picture"));
        {  // (MHGU's Display tab: 0.5 to 2, in steps of 0.05)
            Row row;
            const float fov = fov_of(c.fov);
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
        break;
    }
    case PAGE_CONTROLS: {
        header(r, "touch.head", tr("Touch controls"));
        add_choice(r, "touch", "Show the touch controls", c.controls, {tr("Auto"), tr("Always"), tr("Never")},
                   [](int v) { s.draft.controls = v; }, "controls");
        add_number(r, "touch.opacity", "Opacity", &c.touch_opacity, 20, 100, 5, "%d%%", "opacity");
        {
            Row row;
            char text[32];
            snprintf(text, sizeof text, "%.2fx", c.camera);
            row.id = "touch.camera", row.title = tr("Camera speed"), row.key = "camera_speed", row.value = text;
            row.number = true;
            row.adjust = [](int d) { s.draft.camera = std::clamp(std::round((s.draft.camera + d * .05f) * 20) / 20.f, .2f, 5.f); };
            r.push_back(std::move(row));
        }
        add_choice(r, "touch.style", "Attack buttons", c.attack_style, {tr("Pill"), tr("Two buttons"), tr("Corner band")},
                   [](int v) { s.draft.attack_style = v; }, "attack_style");
        action(r, "touch.buttons", tr("Buttons on the screen"), []() { push(View::Buttons, "tb.0"); });
        r.back().help = tr("Choose which of the game's buttons show on the screen.");
        {
            int shown = 0;
            for (char on : c.show) shown += on != 0;
            r.back().value = std::to_string(shown) + " / " + std::to_string(TB);
        }
        action(r, "touch.edit", tr("Customize the layout"),
               [open]() {
                   if (s.draft == s.saved) {
                       s.closing = true;  // (the editor opens as the menu closes: settings_panel)
                       close_menu(open);
                   } else {
                       s.closing = true;
                       push(View::CloseChanges, "dialog.save");
                   }
               },
               true, "editor");
        r.back().help = tr("Moves and sizes the touch controls, and moves the game's own HUD: the clock, the health and "
                           "stamina bars, the sharpness, the name plates, the area's map, the target camera and the item bar.");
        add_bool(r, "haptics", "Vibrate on touch", &c.haptics, "Haptics");
        add_bool(r, "lock_on", "Lock-on (hold L)", &c.lock_on, "LockOn");
        // a controller's (controls.h): read as soon as they are saved
        header(r, "pad.head", tr("Controller"));
        add_choice(r, "pad.face", "Face buttons", c.face, {tr("Position"), tr("Label")}, [](int v) { s.draft.face = v; },
                   "FaceButtons");
        add_choice(r, "pad.stick", "Right stick", c.right_stick, {tr("Camera"), tr("C-stick")},
                   [](int v) { s.draft.right_stick = v; }, "RightStick");
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
        add_choice(r, "pad.r3", "Right stick click", c.r3,
                   {tr("Lock-on"), tr("Camera (L)"), tr("Target (ZL)"), tr("Kick (ZR)"), tr("Off")},
                   [](int v) { s.draft.r3 = v; }, "R3");
        add_choice(r, "pad.town", "Town actions button", c.town,
                   {"SELECT", tr("Left stick click"), tr("Right stick click"), tr("Off")},
                   [](int v) { s.draft.town = v; }, "TownMenu");
        add_number(r, "pad.dead", "Stick dead zone", &c.deadzone, 0, 30, 2, "%d%%", "Deadzone");
        action(r, "pad.map", tr("Map a controller"), []() { push(View::PadList, "pad.0"); }, true, "PadMap");
        header(r, "keys.head", tr("Keyboard"));
        note(r, "keys.move", tr("W A S D move, Shift walks, I J K L turn the camera."));
        note(r, "keys.buttons", tr("U X, O or F A, Space B, R Y, Q L, E R, Tab ZL, T ZR, Enter START, Backspace SELECT, the arrows the D-pad."));
        note(r, "keys.menu", tr("START and SELECT together open these settings from any controller."));
        break;
    }
    case PAGE_GAMEPLAY: {
        add_button(r, "kick", "Kick button", &c.kick, "kick", BUTTON_KEYS, (int)std::size(BUTTON_KEYS));
        add_button(r, "target", "Target camera button", &c.target, "target", BUTTON_KEYS, (int)std::size(BUTTON_KEYS));
        add_button(r, "skip", "Skip button", &c.skip, "skip", BUTTON_KEYS, (int)std::size(BUTTON_KEYS));
        header(r, "end.head", tr("After a quest"));
        add_button(r, "end", "End the wait (held)", &c.skip_end, "skip_end", END_KEYS, (int)std::size(END_KEYS));
        {
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
        break;
    }
    case PAGE_NETWORK:
        network_rows(r);
        break;
    case PAGE_MODS:
        mods_rows(r);
        break;
    case PAGE_CHEATS:
        cheat_rows(r);
        break;
    case PAGE_MENU: {
        add_choice(r, "language", "Language", c.language, {tr("Phone's"), "English", "Español"},
                   [](int v) { s.draft.language = v; }, "Language");
        add_choice(r, "theme", "Menu colours", c.theme, {tr("Hunter"), tr("Gold"), tr("Blue")},
                   [](int v) { s.draft.theme = v; }, "Theme");
        add_number(r, "menu.scale", "Menu size", &c.menu_scale, 50, 200, 10, "%d%%", "MenuScale");
        add_bool(r, "pause.bg", "Pause in the background", &c.pause_background, "PauseInBackground");
        header(r, "data.head", tr("Your game"));
        action(r, "save.import", tr("Import save"), []() { app_call_activity("showSaveImport"); }, true, "import");
        action(r, "save.export", tr("Export save"), []() { app_call_activity("showSaveExport"); }, true, "export");
        action(r, "game.folder", tr("Game folder"), []() { app_call_activity("showGameFolder"); }, true, "folder");
        action(r, "log.share", tr("Share log"), []() { app_call_activity("showShareLog"); }, true, "log");
        header(r, "app.head", tr("The app"));
        action(r, "restart", tr("Restart"), []() { push(View::Restart, "dialog.cancel"); }, true, "restart");
        action(r, "exit", tr("Exit game"), []() { push(View::Exit, "dialog.cancel"); }, true, "exit");
        action(r, "back.game", tr("Back to the game"), [open]() { ask_close(open); }, true, "back");
        break;
    }
    case PAGE_ABOUT:  // (who made it and where the official one is: the licence's additional terms keep these lines)
        header(r, "about.head", "MH4U for Android " MH4U_APP_VERSION);
        note(r, "about.by", tr("Made by khorne-hub. Free and open source, under the GPL-3.0 with additional terms."));
        note(r, "about.site", std::string(tr("Official download:")) + " github.com/khorne-hub/4u-native");
        note(r, "about.base", tr("Based on MH4UltimateDC by CompaLarios18, with parts from Azahar and Citra."));
        note(r, "about.mod", tr("The single-screen mod is by jM5557."));
        note(r, "about.gu", std::string(tr("Also by khorne-hub: MHGU for Android,")) + " github.com/khorne-hub/gu-native");
        note(r, "about.legal", tr("This app includes no game code, files, ROMs or keys: you need your own copy of the game. Not affiliated with the Great C or the Great N."));
        note(r, "about.paid", tr("Only the links khorne-hub gives are official. If you got it anywhere else, paid for it, or it came with ads, it is not the official one."));
        action(r, "credits", tr("Show the credits"), []() { credits_show(); }, true, "credits");
        header(r, "thanks.head", tr("Thank you"));
        note(r, "thanks.body", tr(THANKS_BODY));
        note(r, "thanks.sign", "\xE2\x80\x94 khorne-hub");
        action(r, "thanks", tr("Show the thanks"), []() { thanks_show(); }, true);
        action(r, "licenses", tr("Licenses"), []() { app_call_activity("showLicenses"); }, true, "licenses");
        break;
    }
    if (s.page != PAGE_ABOUT)  // (Credits & Licenses: nothing to reset)
        action(r, "reset", tr("Reset this tab"), []() { push(View::Reset, "dialog.cancel"); }, true, "reset");
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
int s_combo = 0;  // the combined button View::ComboEdit shows

std::vector<Row> rows_for_view(bool* open) {
    if (s.view == View::Main) return main_rows(open);
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
    if (s.view == View::PadMap) {
        pad_map_rows(r);
        return r;
    }
    if (s.view == View::Buttons) {
        const char* groups[] = {"Hunting", "Bows and bowguns", "Menus and the rest"};
        for (int g = 0; g < 3; g++) {
            header(r, std::string("tb.head.") + std::to_string(g), tr(groups[g]));
            for (int i = 0; i < TB; i++) {
                if (TOUCH_BUTTONS[i].group != g) continue;
                Row row;
                bool on = s.draft.show[i] != 0;
                row.id = "tb." + std::to_string(i), row.title = tr(TOUCH_BUTTONS[i].title);
                row.key = std::string("show_") + TOUCH_BUTTONS[i].key;
                row.value = tr(on ? "On" : "Off");
                row.toggle = on;
                row.adjust = [i](int d) { s.draft.show[i] = d > 0; };
                row.activate = [i]() { s.draft.show[i] = !s.draft.show[i]; };
                r.push_back(std::move(row));
            }
        }
        header(r, "tc.head", tr("Combined buttons"));
        for (int k = 0; k < 6; k++) {
            char title[48];
            snprintf(title, sizeof title, tr("Button %d"), k + 1);
            action(r, "tc." + std::to_string(k), title, [k]() {
                s_combo = k;
                push(View::ComboEdit, "ce.0");
            });
            r.back().value = s.draft.combo[k].empty() ? tr("Empty") : s.draft.combo[k];
            r.back().help = tr("A button of your own presses several of the game's together (A+X, say). Place it with Customize the layout.");
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
        action(r, "ce.clear", tr("Clear"), [k]() { s.draft.combo[k].clear(); });
        action(r, "ce.back", tr("Back"), []() { pop(); });
        r.back().arrow = false;
        return r;
    }
    if (s.view == View::Keyboard) {
        keyboard_rows(r);
        return r;
    }
    action(r, "dialog.cancel", tr("Cancel"), []() {
        s.closing = false;  // (the menu stays: no layout editor after all)
        pop();
    });
    if (s.view == View::Reset) action(r, "dialog.reset", tr("Reset"), []() { reset_page(); });
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
    {"steady", "When the phone cannot hold the frame rate limit, the game runs at an even half of it (a third, ...) instead of swinging between the two: 60 steady rather than 70 to 110 on a 120 FPS limit. It tries the full rate again now and then, and keeps it when the phone holds it. Smoother to the eye, fewer frames.",
     "Cuando el teléfono no llega al límite de FPS, el juego va a la mitad justa (un tercio, ...) en vez de ir y venir entre los dos: 60 parejos en lugar de 70 a 110 con un límite de 120. Cada tanto vuelve a probar el ritmo completo y se queda en él si el teléfono lo sostiene. Más suave a la vista, menos cuadros."},
    {"fps", "The most frames a second, 30 to 240: left and right change it by one, a tap offers the usual ones. 60 is smoother; 30 is the 3DS's, heats the phone less and saves battery. Above 60 for fast phones with a screen that fast: the game keeps its speed, only more frames are drawn.",
     "El máximo de cuadros por segundo, de 30 a 240: izquierda y derecha lo cambian de a uno, un toque ofrece los habituales. 60 se ve más fluido; 30 es el de la 3DS, calienta menos el teléfono y gasta menos batería. Más de 60, para teléfonos rápidos con una pantalla así de rápida: el juego mantiene su velocidad, solo se dibujan más cuadros."},
    {"antialiasing", "The game's own anti-aliasing, which smooths the edges. Off saves its pass: at a high 3D resolution the edges stay fine, and a weak phone gains frames.",
     "El antialiasing del propio juego, que suaviza los bordes. Apagado se ahorra su pasada: con mucha resolución 3D los bordes siguen finos, y un teléfono débil gana cuadros."},
    {"Shadows", "The shadows of the hunters, the monsters and the scenery. Off: none are drawn, faster on a weak phone. It applies at once.",
     "Las sombras de los cazadores, los monstruos y el escenario. Apagado: no se dibujan, más rápido en un teléfono débil. Se aplica al momento."},
    {"RenderScale", "The resolution the 3D picture is drawn at: 100 % is your phone's own, as sharp as its screen shows; less is lighter on the phone. In brackets, the times the 3DS's (400x240) it comes to. 3DS: the console's own, for the weakest phones.",
     "La resolución con que se dibuja la imagen 3D: 100 % es la del teléfono, tan nítida como la muestra su pantalla; menos le exige menos al teléfono. Entre paréntesis, cuántas veces la de la 3DS (400x240) resulta. 3DS: la de la consola, para los teléfonos más débiles."},
    {"widescreen", "Match the screen: the top screen as wide as your phone's, with nothing left over. 16:9: that shape, with bars on a wider screen. Off: the 3DS's own shape.",
     "Igual a la pantalla: la pantalla superior tan ancha como la del teléfono, sin bordes. 16:9: esa forma, con barras en una pantalla más ancha. No: la forma de la 3DS."},
    {"hud_edges", "On a wide picture the hunting HUD's sides go to the screen's edges: health, stamina and sharpness at the left, the items at the right. Off: it stays in the middle.",
     "Con la imagen ancha, los lados del HUD de caza van a los bordes de la pantalla: vida, aguante y filo a la izquierda, los objetos a la derecha. No: queda en el centro."},
    {"bloom", "The glow over the bright parts of the picture. Off: sharper, with more contrast, and a little faster.",
     "El resplandor sobre las partes brillantes de la imagen. Apagado: más nítida, con más contraste, y un poco más rápido."},
    {"motion_blur", "The blending of earlier frames that some effects add.", "La mezcla con cuadros anteriores que agregan algunos efectos."},
    {"depth_of_field", "The focus blur of cutscenes. It can blur more than it should: off keeps them sharp.",
     "El desenfoque de las escenas. Puede desenfocar más de la cuenta: apagado las deja nítidas."},
    {"radial_blur", "The zoom blur around a roaring monster, and in the cutscenes that use it.",
     "El desenfoque radial alrededor de un monstruo que ruge, y en las escenas que lo usan."},
    {"haze", "The heat shimmer of hot areas.", "La reverberación del calor en las zonas calientes."},
    {"dither", "The game's dither pattern, which hides banding on the 3DS's screens. Above 1x it shows as a fine grid over everything: turn it off.",
     "El patrón de tramado del juego, que disimula las bandas en las pantallas de la 3DS. Por encima de 1x se ve como una grilla fina sobre todo: apágalo."},
    {"Volume", "The game's volume. It applies at once.", "El volumen del juego. Se aplica al momento."},
    {"Reverb", "The echo of caves and rooms. Off: the sound is dry. It applies at once.",
     "El eco de las cuevas y las salas. Apagado: el sonido queda seco. Se aplica al momento."},
    {"Driver", "Adreno GPUs only, at your own risk: a Vulkan driver of your own, such as Turnip, from the next start. System is the phone's own. A driver that fails at the start is given up by itself, and the system's is used.",
     "Solo GPU Adreno, bajo tu propio riesgo: un driver Vulkan propio, como Turnip, desde el próximo inicio. Sistema es el del teléfono. Un driver que falla al arrancar se deja solo, y se usa el del sistema."},
    {"DriverImport", "Adds a driver from a .zip with meta.json and its library, the format of the emulators' driver packages. It is copied into the app: the .zip can be deleted afterwards.",
     "Agrega un driver desde un .zip con meta.json y su librería, el formato de los paquetes de drivers de los emuladores. Se copia dentro de la app: después puedes borrar el .zip."},
    {"DriverRemove", "Deletes an installed driver. If it was the one chosen, the system's is chosen instead.",
     "Borra un driver instalado. Si era el elegido, se elige el del sistema."},
    {"FpsCounter", "Shows the frame rate and the phone's temperature at the top of the screen.",
     "Muestra los FPS y la temperatura del teléfono arriba de la pantalla."},
    {"TouchScreen", "The 3DS's touch screen: the map, the item shortcuts and the game's menus. Its own button on the screen shows and hides it too.",
     "La pantalla táctil de la 3DS: el mapa, los atajos de objetos y los menús del juego. Su botón en la pantalla también la muestra y la oculta."},
    {"Layout", "In a corner: over the picture, which fills the screen. Stacked: under the top screen, as on a 3DS, for a 4:3 screen. Side by side: beside it, for a wide screen.",
     "En una esquina: sobre la imagen, que llena la pantalla. Apiladas: debajo de la pantalla superior, como en la 3DS, para una pantalla 4:3. Lado a lado: al costado, para una pantalla ancha."},
    {"Corner", "Where the touch screen goes over the picture. The item bar is at the bottom right of the top screen, unless you moved it (Customize the layout).",
     "Dónde va la pantalla táctil sobre la imagen. La barra de objetos está abajo a la derecha de la pantalla superior, salvo que la hayas movido (Personalizar el diseño)."},
    {"Size", "Its height, in percent of the screen's.", "Su altura, en porcentaje de la de la pantalla."},
    {"Opacity", "How solid the touch screen looks over the picture.", "Qué tan sólida se ve la pantalla táctil sobre la imagen."},
    {"fov", "How much of the scene you see: higher shows more around the hunter, lower less. 1.00 is the 3DS's. It applies at once.",
     "Cuánto se ve de la escena: un valor mayor muestra más alrededor del cazador, uno menor menos. 1,00 es el de la 3DS. Se aplica al momento."},
    {"controls", "Auto: they show while no controller is connected. Never: only the settings button stays.",
     "Automático: se ven mientras no haya un mando conectado. Nunca: solo queda el botón de opciones."},
    {"opacity", "How solid the touch controls look.", "Qué tan sólidos se ven los controles táctiles."},
    {"camera_speed", "How far the camera turns for a drag.", "Cuánto gira la cámara al arrastrar."},
    {"attack_style", "Pill: A on its upper half, X on the lower, both on the seam. Two buttons: A and X apart. Corner band: the attacks and the guard round the screen's corner.",
     "Píldora: A en su mitad de arriba, X en la de abajo, las dos en la unión. Dos botones: A y X separados. Banda en la esquina: los ataques y la guardia alrededor de la esquina."},
    {"editor", "Move and resize each control: drag one, pinch it or use - and + for its size, then Save. Holding the settings button opens it too.",
     "Mueve y cambia el tamaño de cada control: arrástralo, pellízcalo o usa - y + para su tamaño, y luego Guardar. Mantener apretado el botón de opciones también lo abre."},
    {"Haptics", "A short vibration when you press a touch control.", "Una vibración corta al tocar un control."},
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
    {"LockOn", "Hold L (or click the right stick of a controller) and the camera keeps turning to the monster, the one the Target Cam chose or the nearest large one, until you do it again. Turning the camera yourself pauses it for a moment.",
     "Mantén apretado L (o aprieta el stick derecho de un mando) y la cámara sigue girando hacia el monstruo, el que eligió la cámara de objetivo o el grande más cercano, hasta que lo hagas de nuevo. Girar la cámara tú mismo la pausa un momento."},
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
    {"show_select", "SELECT during a hunt. MH4U's hunts do without it.", "SELECT durante una cacería. Las cacerías de MH4U no lo necesitan."},
    {"show_dpad", "The D-pad during a hunt. The game's menus have their own.", "La cruceta durante una cacería. Los menús del juego tienen la suya."},
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
    {"Notices", "The room's chat, and who comes and goes, show for a few seconds at the top of the screen while you play.",
     "El chat de la sala, y quién entra y sale, se ven unos segundos arriba de la pantalla mientras juegas."},
    {"Lobby", "The address of a lobby that lists rooms (Azahar's web API), to pick a room from its list. None comes with the app. Empty: none.",
     "La dirección de un lobby que lista salas (la API web de Azahar), para elegir una sala de su lista. La app no trae ninguno. Vacío: ninguno."},
    {"LanSearch", "Looks for rooms hosted by phones on the same Wi-Fi as this one (Host a room, on the other phone), so there is no address to type. It takes a second and a half. A tap on a room found joins it.",
     "Busca salas creadas por teléfonos en la misma Wi-Fi que este (Crear una sala, en el otro teléfono), así no hay que escribir ninguna dirección. Tarda un segundo y medio. Un toque en una sala encontrada entra."},
    {"Refresh", "Asks the lobby above for its rooms. A tap on one joins it.", "Le pide sus salas al lobby de arriba. Un toque en una entra."},
    {"Language", "The language of this menu. Phone's: the one your phone uses.", "El idioma de este menú. Del teléfono: el que usa tu teléfono."},
    {"Theme", "The colours of this menu.", "Los colores de este menú."},
    {"MenuScale", "The size of this menu, 50 % to 200 %.", "El tamaño de este menú, de 50 % a 200 %."},
    {"PauseInBackground", "The game stops while you are in another app, and goes on where it was when you come back. Off: it keeps running, as a hunt with others online needs; it uses battery meanwhile.",
     "El juego se detiene mientras estás en otra app, y sigue donde estaba al volver. Apagado: sigue corriendo, como lo necesita una cacería en línea con otros; mientras tanto gasta batería."},
    {"import", "Brings your save from an emulator or another device. It replaces the one on this device, and the current save is kept in a copy.",
     "Trae tu partida desde un emulador o desde otro dispositivo. Reemplaza la de este dispositivo, y la partida actual queda guardada en una copia."},
    {"export", "Copies your save to a folder you choose, to take it to an emulator or another device.",
     "Copia tu partida a la carpeta que elijas, para llevarla a un emulador o a otro dispositivo."},
    {"folder", "Where the game's files are read from. A new folder is used the next time the game starts.",
     "De dónde se leen los archivos del juego. Una carpeta nueva se usa la próxima vez que abras el juego."},
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
    {"CodeLibrary", "A mod with an exefs folder changes the game's code, and this port runs that code translated ahead of time: your code library has to be made on the PC with the same code mods turned on (step 1 of the guide, answering Yes and choosing the mod). If they don't match, the game won't start.",
     "Un mod con carpeta exefs cambia el código del juego, y este port ejecuta ese código traducido de antemano: tu librería de código tiene que hacerse en la PC con los mismos mods de código activados (el paso 1 de la guía, respondiendo Sí y eligiendo el mod). Si no coinciden, el juego no arranca."},
    {"ModConflicts", "Mods that change the same game files: where both are on, the one lower in the list is used. Select it to see each pair and choose which mod is used.",
     "Mods que cambian los mismos archivos del juego: donde los dos están activados, se usa el de más abajo en la lista. Selecciónalo para ver cada par y elegir qué mod se usa."},
    {"ModSuggest", "Orders the mods that change the same files from the biggest to the smallest, so a small tweak wins over a large overhaul. The others keep their places.",
     "Ordena los mods que cambian los mismos archivos del más grande al más chico, para que un retoque pequeño gane sobre una reforma grande. Los demás quedan donde están."},
    {"ModRescan", "Reads the mods folder again: a mod added there joins the end of the list, on; one deleted there leaves it.",
     "Vuelve a leer la carpeta de mods: un mod agregado ahí se suma al final de la lista, activado; uno borrado de ahí sale de ella."},
    {"CheatImport", "Adds the cheats of a .txt file in the format of Azahar and Citra ([Name] and the code lines), for MH4U USA 1.3.0. They come off: turn on the ones you want and save. A cheat with the same name is replaced.",
     "Agrega los trucos de un archivo .txt en el formato de Azahar y Citra ([Nombre] y las líneas de código), para MH4U de EE. UU. 1.3.0. Llegan apagados: activa los que quieras y guarda. Un truco con el mismo nombre se reemplaza."},
    {"CheatRemove", "Deletes a cheat from the cheats file.", "Borra un truco del archivo de trucos."},
    {"reset", "Puts this tab's options back as they come. Nothing changes until you save.",
     "Devuelve las opciones de esta pestaña a como vienen. Nada cambia hasta que guardes."},
};
std::string help_of(const char* key) {
    for (const Help& h : HELP)
        if (!strcmp(key, h.key)) return spanish() ? h.es : h.en;
    return "";
}
std::string help_text(const Row& r) {
    if (!r.help.empty() || r.key.empty()) return r.help;
    for (const Help& h : HELP)
        if (r.key == h.key) return spanish() ? h.es : h.en;
    if (!r.key.compare(0, 5, "show_"))  // (a touch control without words of its own)
        return spanish() ? "Muestra este botón en la pantalla, o lo oculta. Se ubica con Personalizar el diseño."
                         : "Shows this button on the screen, or hides it. Customize the layout places it.";
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
// X saves (not while a question or the keyboard is up), L / R the tab before / after
void page_keys() {
    if (confirmation() || s.view == View::Keyboard) return;
    if (pressed(face_key(2)) && dirty() && !s.dropdown.open) s.save_request = true;
    if (s.dropdown.open || s.view != View::Main) return;
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
// An invisible button over r for a finger (the controller never lands on it); true when tapped
bool touch_button(const char* id, const ImRect& r, bool enabled, bool* held) {
    ImGui::SetCursorScreenPos(r.Min);
    ImGui::BeginDisabled(!enabled);
    bool tapped = ImGui::InvisibleButton(id, ImVec2(std::max(1.f, r.GetWidth()), std::max(1.f, r.GetHeight())));
    *held = ImGui::IsItemActive();
    ImGui::EndDisabled();
    return tapped && taps_ok();
}
// A row: lit gold when it is the one picked (a diamond at its left, as the game's cursor), lighter while a finger holds
// it; its title at the left, at the right an On / Off switch, the value between - and + (stepper: draw_rows' buttons,
// room their width), or the value, marked when a tap lists its choices. plated: a question's answer, a plate with its
// title in the middle
void touch_row(ImDrawList* dl, const Row& r, const ImRect& rect, bool lit, bool held, bool plated, ImVec2 label,
               float wrap, float room, bool toggle, bool stepper) {
    const Theme& t = theme();
    const Look k = touch_look();
    float font = ImGui::GetFontSize(), line = s.m.line, cy = rect.GetCenter().y;
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
    float right = rect.Max.x - font * .6f;
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
            fit(value, stepper ? room - bs * 2 - font * .4f : room - font * (marker ? 1.4f : .3f));
            ImVec2 size = ImGui::CalcTextSize(value.c_str());
            float vx = stepper ? right - room * .5f - size.x * .5f : right - size.x - (marker ? font * 1.1f : 0);
            ImU32 value_ink = !r.enabled ? t.text_off : lit ? ink : r.ink ? r.ink : k.rule;
            shadow_text(dl, font, ImVec2(vx, cy - size.y * .5f), value_ink, value.c_str());
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
// list: the picker's choices; else the view's rows (while the picker is open they keep its row marked)
void draw_rows(std::vector<Row>& rows, bool list = false) {
    auto* w = ImGui::GetCurrentWindow();
    float font = ImGui::GetFontSize(), width = ImGui::GetContentRegionAvail().x - s.m.line * 3;  // (the scroll mark)
    bool passive = s.dropdown.open && !list;
    bool plated = !list && confirmation();
    float bs = std::min(s.m.row * .84f, font * 2.3f);  // (a - or + button's side)
    float gap = ImGui::GetStyle().ItemSpacing.x;
    std::function<void()> activate;
    std::function<void(int)> adjust;  // (a - or + tapped: its row's, by step)
    int step = 0, column = 0;
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
            float wrap = std::max(size * 4, width - indent - who_w - font * .4f);
            float h = fnt->CalcTextSizeA(size, FLT_MAX, wrap, r.title.c_str()).y + top * 2 + font * .15f;
            ImGui::Dummy(ImVec2(width, h));
            diamond(w->DrawList, ImVec2(p.x + font * .55f, p.y + top + size * .55f), size * .16f, r.ink ? r.ink : fade(k.rule, .8f));
            if (!who.empty()) shadow_text(w->DrawList, fnt, size, ImVec2(p.x + indent, p.y + top), k.glow, who.c_str());
            shadow_text(w->DrawList, fnt, size, ImVec2(p.x + indent + who_w, p.y + top), who.empty() && r.ink ? r.ink : k.ink,
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
        // (a key: `grid` of them to a line, each a plate; the line begins again where the keys change)
        column = r.grid && i && rows[i - 1].grid == r.grid ? column + 1 : 0;
        if (r.grid && column % r.grid) ImGui::SameLine(0, gap);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float rw = r.grid ? (width - (r.grid - 1) * gap) / r.grid : width;
        bool stepper = r.number && r.adjust;
        bool toggle = r.toggle >= 0 && r.enabled;
        bool plate_row = plated || r.grid;
        // (the title wraps before the value's room: a switch, - value +, or the value)
        float room = r.grid ? 0 : toggle ? font * 3.6f : stepper ? bs * 2 + font * 5.2f : r.value.empty() ? (r.arrow ? font * 1.2f : 0) : rw * .42f;
        float wrap = std::max(font, rw - font * (plate_row ? 1.2f : 1.9f) - room);
        ImVec2 label = ImGui::CalcTextSize(r.title.c_str(), nullptr, false, wrap);
        float h = r.grid ? key_h : std::max(s.m.row, label.y + font * .9f);
        ImGui::PushID(r.id.c_str());
        ImRect rect(p, ImVec2(p.x + rw, p.y + h));
        bool selected = passive ? r.id == s.dropdown.row : s.focus == r.id;
        ImVec4 clear(0, 0, 0, 0);  // (touch_row draws it)
        ImGui::PushStyleColor(ImGuiCol_Button, clear);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, clear);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, clear);
        ImGui::BeginDisabled(!r.enabled);
        if (stepper) ImGui::SetNextItemAllowOverlap();  // (its - and + are over it)
        bool clicked = ImGui::Button("###row", ImVec2(rw, h));
        bool held = ImGui::IsItemActive();
        ImGui::EndDisabled();
        ImGui::PopStyleColor(3);
        if (selected && r.enabled && !passive) keep_visible(w, rect);
        if (!list) s.targets.push_back({r.id, rect.GetCenter(), r.enabled});
        touch_row(w->DrawList, r, rect, selected && r.enabled, held, plate_row, label, wrap, room, toggle, stepper);
        if (stepper) {  // - and + (held: they repeat)
            ImVec2 next = ImGui::GetCursorScreenPos();
            float right = rect.Max.x - font * .6f, cy = rect.GetCenter().y;
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
        ImGui::PopID();
        if (clicked && r.enabled && taps_ok()) {
            focus(r.id);
            activate = r.activate;
        }
    }
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
// The picked row's explanation: its name and what it does; the last save's outcome and what the restart mark means at
// the bottom. side: a column at the right of the rows, else a box under them (the explanation at its left, the outcome
// and the mark at its right)
void touch_help(ImDrawList* dl, const ImRect& r, const std::vector<Row>& rows, bool side) {
    const Theme& t = theme();
    const Look k = touch_look();
    ImFont* font = ImGui::GetFont();
    float f = s.m.f, pad = f * .55f, small = std::floor(f * .8f), line = s.m.line;
    dl->AddRectFilled(r.Min, r.Max, fade(k.plate, .6f));
    dl->AddRect(r.Min, r.Max, k.edge, 0, std::max(1.f, line * .5f));
    for (ImVec2 c : {r.Min, ImVec2(r.Max.x, r.Min.y), ImVec2(r.Min.x, r.Max.y), r.Max})  // (the plates' studs)
        if (t.game) diamond(dl, c, line * 1.3f, fade(k.rule, .8f));
    std::vector<std::pair<std::string, ImU32>> foot;
    if (!s.error.empty())
        foot.push_back({s.error, t.warn});
    else if (s.saved_notice)
        foot.push_back({saved_text(), k.glow});
    foot.push_back({tr("* Restart required"), k.dim});
    float x = r.Min.x + pad, wrap = r.GetWidth() - pad * 2, bottom = r.Max.y - pad * .7f;
    if (side) {
        for (auto it = foot.rbegin(); it != foot.rend(); ++it) {
            bottom -= font->CalcTextSizeA(small, FLT_MAX, wrap, it->first.c_str()).y;
            dl->AddText(font, small, ImVec2(x, bottom), it->second, it->first.c_str(), nullptr, wrap);
            bottom -= pad * .45f;
        }
        dl->AddLine(ImVec2(x, bottom), ImVec2(r.Max.x - pad, bottom), fade(k.rule, .4f), std::max(1.f, line * .4f));
    } else {
        float most = wrap * .34f, widest = 0, y = bottom;
        for (auto it = foot.rbegin(); it != foot.rend(); ++it) {
            ImVec2 size = font->CalcTextSizeA(small, FLT_MAX, most, it->first.c_str());
            y -= size.y;
            dl->AddText(font, small, ImVec2(r.Max.x - pad - size.x, y), it->second, it->first.c_str(), nullptr, most);
            widest = std::max(widest, size.x);
        }
        float sx = r.Max.x - pad - widest - pad * .8f;  // (a rule between the explanation and the outcome)
        dl->AddLine(ImVec2(sx, r.Min.y + pad), ImVec2(sx, r.Max.y - pad), fade(k.rule, .3f), std::max(1.f, line * .4f));
        wrap = std::max(f * 4, sx - x - pad * .8f);
    }
    const std::string& id = s.dropdown.open ? s.dropdown.row : s.focus;
    const Row* row = nullptr;
    for (const Row& candidate : rows)
        if (candidate.id == id && !candidate.header) row = &candidate;
    if (!row) return;
    dl->PushClipRect(r.Min, ImVec2(r.Max.x, std::max(r.Min.y, bottom - pad * .3f)), true);
    float y = r.Min.y + pad * .7f;
    if (side) {
        ImFont* tf = title_font();
        shadow_text(dl, tf, f, ImVec2(x, y), k.glow, row->title.c_str(), wrap);
        y += tf->CalcTextSizeA(f, FLT_MAX, wrap, row->title.c_str()).y + pad * .5f;
        dl->AddLine(ImVec2(x, y), ImVec2(x + wrap, y), fade(k.rule, .5f), std::max(1.f, line * .4f));
        y += pad * .6f;
    }
    std::string help = help_text(*row);
    dl->AddText(font, small, ImVec2(x, y), k.ink, help.c_str(), nullptr, wrap);
    dl->PopClipRect();
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
    float longest = tf->CalcTextSizeA(title, FLT_MAX, 0, s.dropdown.title.c_str()).x + side + pad * 3;
    for (const auto& o : options) longest = std::max(longest, font->CalcTextSizeA(f, FLT_MAX, 0, o.title.c_str()).x + f * 3.4f);
    float w = std::clamp(longest + pad * 2, vp->WorkSize.x * .32f, vp->WorkSize.x * .62f);
    float h = std::min(band + line * 2 + pad * 2 + options.size() * (s.m.row + spacing) + pad + frame, vp->WorkSize.y * .86f);
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
    ImVec2 ts = tf->CalcTextSizeA(title, FLT_MAX, 0, s.dropdown.title.c_str());
    dl->PushClipRect(ImVec2(x0, y0), ImVec2(shut.Min.x - pad * .5f, y0 + band), true);
    diamond(dl, ImVec2(x0 + pad * 1.6f, by + side * .5f), f * .2f, k.rule);
    shadow_text(dl, tf, title, ImVec2(x0 + pad * 1.6f + f * .7f, by + side * .5f - ts.y * .5f), k.glow, s.dropdown.title.c_str());
    dl->PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(x0 + pad, y0 + band + line * 2 + pad));
    ImGui::BeginChild("##choices", ImVec2(win->Size.x - pad * 2, std::max(1.f, h - band - line * 2 - pad * 2 - frame)),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    auto* choices_window = ImGui::GetCurrentWindow();
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
                          {SettingsIcons::X, keys ? "Delete" : "Save", keys || (dirty() && !confirmation())},
                          {SettingsIcons::Y, keys ? "Space" : "", keys},
                          {SettingsIcons::L, "", s.view == View::Main || keys},
                          {SettingsIcons::R, keys ? "Cursor" : "Tabs", s.view == View::Main || keys}};
    SettingsIcons::Ink ink;
    ink.plate = k.plate, ink.rim = k.edge, ink.rule = k.rule, ink.letter = k.ink;
    float text = std::floor(size * .78f), gap = size * .9f;
    // (from the right edge leftwards, so the strip ends where the panel's rows do)
    float total = 0;
    auto shown = [keys](const Item& it) { return keys || it.glyph != SettingsIcons::Y; };
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
void touch_panel(bool* open) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const Theme& look = theme();
    const Look k = touch_look();
    float H = vp->WorkSize.y;
    ImVec2 panel(std::min(vp->WorkSize.x * .94f, H * 2.6f), H * .94f);
    {  // (the game under it dimmed, the touch controls with it)
        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        bg->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(0, 0, 0, 140));
    }
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - panel.x) * .5f, vp->WorkPos.y + (vp->WorkSize.y - panel.y) * .5f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(panel, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, vec(look.panel));
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
    std::vector<Row> options;  // (the picker open: its choices, the controller moves in them)
    if (s.dropdown.open) options = dropdown_rows();
    navigate(s.dropdown.open ? options : rows, s.dropdown.open);
    bool dialog = confirmation();
    bool picking = s.dropdown.open && !options.empty();
    // The header: Back at its left, Save and Close at its right, the title between
    float x0 = window->Pos.x, y0 = window->Pos.y, x1 = x0 + window->Size.x, y1 = y0 + window->Size.y;
    float band = std::max(.11f * H, f * 2.9f), side = band * .8f, by = y0 + (band - side) * .5f + line * .5f;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y0 + band), k.band);
    if (look.game) {  // (the game's red and green rules under it)
        dl->AddRectFilled(ImVec2(x0, y0 + band), ImVec2(x1, y0 + band + line), RULE_RED);
        dl->AddRectFilled(ImVec2(x0, y0 + band + line), ImVec2(x1, y0 + band + line * 1.7f), RULE_GREEN);
    } else
        dl->AddRectFilled(ImVec2(x0, y0 + band), ImVec2(x1, y0 + band + line * .7f), look.accent);
    float title_x = x0 + pad * 1.5f, right = x1 - pad;
    bool held = false;
    std::function<void()> later;  // (what the header or a tab asked for: once the frame is drawn as it began)
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
    }
    {  // (the title; the tab's name or the submenu's after it)
        const char* title = tr("SETTINGS");
        float size = std::floor(f * 1.2f), cy = by + side * .5f;
        ImVec2 ts = tf->CalcTextSizeA(size, FLT_MAX, 0, title);
        std::string sub = s.view == View::Main       ? tr(page_names[s.page])
                          : s.view == View::Buttons  ? tr("Buttons on the screen")
                          : s.view == View::Conflicts ? tr("Mod conflicts")
                          : s.view == View::PadList || s.view == View::PadMap ? tr("Map a controller")
                          : s.view == View::ComboEdit ? tr("Combined buttons")
                          : s.view == View::Keyboard ? s.keyboard.title
                                                     : "";
        dl->PushClipRect(ImVec2(x0, y0), ImVec2(std::max(title_x, right), y0 + band), true);
        shadow_text(dl, tf, size, ImVec2(title_x, cy - ts.y * .5f), k.glow, title);
        if (!sub.empty()) {
            float dx = title_x + ts.x + f * .7f;
            diamond(dl, ImVec2(dx, cy), f * .18f, k.rule);
            ImVec2 ss = ImGui::CalcTextSize(sub.c_str());
            shadow_text(dl, f, ImVec2(dx + f * .7f, cy - ss.y * .5f), k.ink, sub.c_str());
        }
        dl->PopClipRect();
    }
    // The body: the tabs and the explanation round the list on the tabs; a submenu's or a question's rows alone
    float frame = std::max(2.f, f * .14f) * 1.7f;  // (draw_frame's rules at the bottom)
    bool pad_guides = controls_real_pad();  // (not a phone's own keys, nor a keyboard Android calls a gamepad)
    float guide = pad_guides ? std::floor(f * 1.15f) : 0;
    float top = y0 + band + line * 2 + pad, bottom = y1 - frame - pad - (guide ? guide + pad * .8f : 0), left = x0 + pad,
          end = x1 - pad;
    auto error_text = [&](float y, float x, float w) {  // (s.error, wrapped at w; the height it takes)
        if (s.error.empty()) return 0.f;
        dl->AddText(font, f, ImVec2(x, y), look.warn, s.error.c_str(), nullptr, w);
        return font->CalcTextSizeA(f, FLT_MAX, w, s.error.c_str()).y + pad * .6f;
    };
    ImRect list_rect, help_rect;
    bool side_help = false;
    // the rows from lx to the panel's right, and the picked row's explanation in a column at the right where there is
    // room for both, else in a box under them as tall as the longest of the view's
    auto with_help = [&](float lx) {
        float help_w = f * 13;
        side_help = end - lx - help_w - pad >= f * 24;
        if (side_help) {
            list_rect = ImRect(lx, top, end - help_w - pad, bottom);
            help_rect = ImRect(end - help_w, top, end, bottom);
            return;
        }
        float small = std::floor(f * .8f), hw = end - lx - pad * 2, tall = small * 3.4f;
        for (const Row& r : rows)
            if (!r.header) tall = std::max(tall, font->CalcTextSizeA(small, FLT_MAX, std::max(f * 4, hw * .62f), help_text(r).c_str()).y);
        float box = std::min(tall + f * .55f * 1.7f, (bottom - top) * .4f);
        list_rect = ImRect(lx, top, end, bottom - box - pad);
        help_rect = ImRect(lx, bottom - box, end, bottom);
    };
    if (s.view == View::Main) {
        float tw = 0;
        for (const char* name : page_names) tw = std::max(tw, ImGui::CalcTextSize(tr(name)).x);
        tw = std::clamp(tw + f * 1.8f, (end - left) * .16f, (end - left) * .24f);
        float gap = pad * .6f, th = std::min((bottom - top - gap * (PAGES - 1)) / PAGES, s.m.row * 1.05f);
        for (int i = 0; i < PAGES; i++) {
            ImRect r(left, top + i * (th + gap), left + tw, top + i * (th + gap) + th);
            ImGui::PushID(i);
            if (touch_button("###tab", r, true, &held) && s.page != i) later = [i]() { first_page(i); };
            ImGui::PopID();
            bool active = s.page == i;
            float cy = r.GetCenter().y;
            ImU32 bright = IM_COL32(255, 248, 232, 255);
            if (active) {  // (lit as the picked row, a notch at its right pointing at the rows)
                float n = std::min(th * .18f, pad);
                dl->AddRectFilledMultiColor(r.Min, r.Max, k.bar0, k.bar1, k.bar1, k.bar0);
                dl->AddRect(r.Min, r.Max, fade(k.glow, .9f), 0, line * .7f);
                dl->AddTriangleFilled(ImVec2(r.Max.x, cy - n), ImVec2(r.Max.x + pad * .75f, cy), ImVec2(r.Max.x, cy + n), k.glow);
                diamond(dl, ImVec2(r.Min.x + f * .5f, cy), f * .17f, bright);
            } else
                plate(dl, r, line * .8f, held, true);
            const char* name = tr(page_names[i]);
            float room = tw - f * 1.4f, wide = ImGui::CalcTextSize(name).x;
            float text = wide > room ? std::floor(f * room / wide) : f;  // (a long name: smaller, on one line)
            ImVec2 ts = font->CalcTextSizeA(text, FLT_MAX, 0, name);
            shadow_text(dl, text, ImVec2(r.Min.x + f * .95f, cy - ts.y * .5f), active ? bright : k.dim, name);
        }
        {  // (under the tabs, the game's crest of sorts: a gold diamond between two rules, and the port's name)
            float y = top + PAGES * (th + gap) + pad * 1.2f, cx = left + tw * .5f;
            if (bottom - y > f * 3) {
                dl->AddLine(ImVec2(left + tw * .12f, y), ImVec2(cx - f * .6f, y), fade(k.rule, .5f), std::max(1.f, line * .5f));
                dl->AddLine(ImVec2(cx + f * .6f, y), ImVec2(left + tw * .88f, y), fade(k.rule, .5f), std::max(1.f, line * .5f));
                diamond(dl, ImVec2(cx, y), f * .3f, fade(k.rule, .85f));
                diamond(dl, ImVec2(cx, y), f * .13f, k.band);
                const char* name = "MH4U";
                float size = std::floor(f * .9f);
                ImVec2 ns = tf->CalcTextSizeA(size, FLT_MAX, 0, name);
                shadow_text(dl, tf, size, ImVec2(cx - ns.x * .5f, y + f * .6f), fade(k.rule, .75f), name);
            }
        }
        with_help(left + tw + pad * 1.4f);
    } else if (s.view == View::Buttons || s.view == View::Conflicts || s.view == View::PadList || s.view == View::PadMap) {  // (the whole width, the explanation beside or under)
        with_help(left);
    } else if (s.view == View::Keyboard) {  // (the text typed so far, what it takes beside it, the keys under it)
        const Keyboard& kb = s.keyboard;
        ImRect field(left, top, end, top + f * 2.2f);
        dl->AddRectFilled(field.Min, field.Max, IM_COL32(14, 10, 8, 235));
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
        std::string question = tr(s.view == View::Restart        ? "Restart the game?"
                                  : s.view == View::Exit         ? "Exit game?"
                                  : s.view == View::Reset        ? "Reset this tab?"
                                  : s.view == View::RemoveDriver ? "Remove this driver?"
                                  : s.view == View::RemoveMod    ? "Remove this mod?"
                                  : s.view == View::RemoveCheat  ? "Remove this cheat?"
                                                                 : "Unsaved changes");
        std::string detail = s.view == View::RemoveDriver
                                 ? fmt("%s: its files are deleted. To use it again, import its .zip again.", s.remove_name.c_str())
                             : s.view == View::RemoveMod
                                 ? fmt("%s: its files are deleted from the app. To use it again, add it again.", s.remove_mod.c_str())
                             : s.view == View::RemoveCheat
                                 ? fmt("%s: it is deleted from the cheats file. To use it again, import it again.", s.remove_name.c_str())
                                 : tr(s.view == View::Restart ? "The game closes and opens again. Save your hunt in the game first."
                                      : s.view == View::Exit  ? "The game closes. Save your hunt in the game first: what was not saved there is lost."
                                      : s.view == View::Reset ? "Its options go back as they come. Nothing changes until you save."
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
    scroll_mark(list);
    if (picking)  // (a window of its own, drawn over the menu's: dimmed on its own)
        list->DrawList->AddRectFilled(list->Pos, ImVec2(list->Pos.x + list->Size.x, list->Pos.y + list->Size.y), IM_COL32(0, 0, 0, 150));
    ImGui::EndChild();
    if (s.view == View::Main || s.view == View::Buttons || s.view == View::Conflicts || s.view == View::PadList || s.view == View::PadMap)
        touch_help(dl, help_rect, rows, side_help);
    if (pad_guides) {
        float gy = y1 - frame - pad * .6f - guide;
        dl->AddLine(ImVec2(left, gy - pad * .4f), ImVec2(end, gy - pad * .4f), fade(k.rule, .25f), std::max(1.f, line * .4f));
        draw_guides(dl, left, gy, end, guide);
    }
    if (later) later();
    if (s.save_request) {
        s.save_request = false;
        save();
    }
    draw_frame(window);
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
    constexpr double kShown = 8, kFade = .6;
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
void credits_show() { g_credits_at = SDL_GetTicks() / 1000.0, g_credits_asked = true; }
void draw_credits() {
    constexpr double kShown = 8, kIn = .4, kFade = .9;
    const double now = SDL_GetTicks() / 1000.0;
    if (g_credits_at < 0) g_credits_at = now;
    const double t = now - g_credits_at;
    if (t > kShown) return;
    const float a = (float)std::clamp(std::min(t / kIn, (kShown - t) / kFade), 0.0, 1.0);
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const Look k = touch_look();
    const Theme& th = theme();
    ImFont* font = ImGui::GetFont();
    ImFont* tf = title_font();
    const char* title = "MH4U for Android";
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
bool g_thanks_open = false, g_thanks_held = false;
double g_thanks_at = 0;
void thanks_show() {
    g_thanks_open = true, g_thanks_held = false, g_thanks_at = SDL_GetTicks() / 1000.0;
    std::fprintf(stderr, "[Info] App: the thanks\n");
}
void thanks_close() {
    if (SDL_GetTicks() / 1000.0 - g_thanks_at < .5) return;  // (the press that opened it, or a finger already down)
    g_thanks_open = g_thanks_held = false;
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
    if (state == 1 && g_credits_at >= 0 && SDL_GetTicks() / 1000.0 - g_credits_at > 8.2) thanks_show(), state = 2;
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
    const char* title = tr("Thank you");
    const char* body = tr(THANKS_BODY);
    const char* sign = "\xE2\x80\x94 khorne-hub";
    const char* cont = tr("Continue");
    dl->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), fade(IM_COL32(0, 0, 0, 150), a));
    const float f = std::floor(std::clamp(vp->Size.y * .036f, 12.f, 44.f)), tsz = std::floor(f * 1.5f);
    const float w = std::floor(std::min(vp->Size.x * .7f, f * 26)), pad = std::floor(f * 1.3f), gap = std::floor(f * .45f);
    const float line = std::max(1.f, std::floor(f * .09f));
    const ImVec2 bs = font->CalcTextSizeA(f, FLT_MAX, w, body);
    const float bh = std::floor(f * 2.1f);
    const float h = tsz + gap * 3 + bs.y + gap * 2 + f + gap * 2 + bh;
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
    const ImVec2 ss = font->CalcTextSizeA(f, FLT_MAX, 0, sign);
    shadow_text(dl, font, f, ImVec2(std::floor(c.x + w * .5f - ss.x), y), fade(th.accent, a), sign);
    y += f + gap * 2;
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

void draw_notices() {
    constexpr double kShown = 6, kFade = .6;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float y = driver_notice(vp->Pos.y + (perfstats_visible() ? .085f : .015f) * vp->Size.y);
    y = app_notice(y);
    const std::vector<NetLine>& lines = net_lines();
    if (lines.empty() || !s.draft.notices) return;
    double now = SDL_GetTicks() / 1000.0;
    std::vector<const NetLine*> show;
    for (auto it = lines.rbegin(); it != lines.rend() && show.size() < 3; ++it) {
        if (it->serial <= s.seen_line || now - it->time > kShown) break;
        if (it->kind != NetLine::Mine) show.insert(show.begin(), &*it);
    }
    for (const NetLine* l : show) {
        float a = (float)std::clamp((kShown - (now - l->time)) / kFade, 0.0, 1.0);
        bool chat = l->kind == NetLine::Chat;
        ImU32 ink = line_ink(*l);
        y = notice_plate(y, a, chat ? l->nick + ":" : "", line_text(*l), ink, chat || !ink ? 0 : ink);
    }
}

}  // namespace

bool settings_spanish() {
    return spanish_for(std::max(0, index_of(LANGUAGE_KEYS, app_setting("Interface", "Language", "auto"))));
}

void settings_panel(bool* open) {
    if (!g_started) start();
    net_poll();  // (the room's events, open or not: the chat and the notices)
    if (!*open) {
        if (s.editing) {  // (closed from outside)
            s.editing = false;
            closed_now = true;
        }
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
        request_back = false;
        request_close = false;
    }
    // the letters' size: from the screen's height, and the menu's own size
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImGuiStyle& style = ImGui::GetStyle();
    float H = vp->WorkSize.y;
    // (50 % to 200 %: from a small phone's tight fit to a tablet's or a TV's far-away screen)
    float px = std::clamp(.034f * H * s.draft.menu_scale / 100.f, .016f * H, .07f * H);
    ImGui::PushFont(nullptr, px / std::max(.01f, style.FontScaleMain * style.FontScaleDpi));
    touch_panel(open);
    ImGui::PopFont();
    // (typed text while the keyboard is up, the phone's own keyboard only when asked for; the room's lines: seen)
    text_input(*open && s.view == View::Keyboard ? std::max(1, g_text_input) : 0);
    if (!net_lines().empty()) s.seen_line = net_lines().back().serial;
    is_dirty = *open && (dirty() || confirmation());
    if (!*open) {
        s.editing = false;
        s.dropdown.open = false;
        closed_now = true;
        padmap_cancel();  // (a controller's map under way ends with the menu)
        s.pad_note.clear();
        bool edit = s.closing;
        s.closing = false;
        if (edit) touch_edit_begin();  // (Customize the layout: the editor, now the menu is gone)
    }
    if (*open && g_credits_asked) draw_credits();
    draw_thanks();
}

bool settings_panel_thanks_open() { return g_thanks_open; }

bool settings_panel_thanks_event(const SDL_Event& e) {
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

void settings_panel_request_close() { request_close = true; }

void settings_panel_notice(const std::string& english, const std::string& spanish) {
    g_notice_en = english, g_notice_es = spanish;
    g_notice_since = -2;
}

bool settings_panel_closed_now() { return closed_now.exchange(false); }

bool settings_panel_dirty() { return is_dirty.load(); }
