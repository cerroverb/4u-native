// The app's interface (ui.h): Dear ImGui on SDL_Renderer, and on it the touch controls in the Monster Hunter style
// (touch.cpp, from the MHGU port) and the settings (settings_panel.cpp, from the MHGU port too). Everything here runs on
// the app's main thread.
#include "ui.h"

#include <unistd.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "game_keyboard.h"
#include "hunt_bars.h"
#include "quick_items.h"
#include "hunt_numbers.h"
#include "settings_panel.h"
#include "touch.h"
#include "town.h"
#ifndef __ANDROID__
#include "pc_input.h"  // (the PC's list of the controls: pc/pc_input.cpp)
#endif
#include "weapon_tree_ui.h"

namespace {

SDL_Renderer* g_renderer = nullptr;
SDL_Window* g_window = nullptr;
UiInput g_input;
std::string g_ini;
bool g_settings_open = false;  // the settings panel is over the game

// The phone's own fonts (ImGui 1.92 draws them at any size, crisp): a bold sans for the letters and labels, a serif
// for titles, as the game's own. The first one found of each.
const char* first_font(std::initializer_list<const char*> paths) {
    for (const char* p : paths)
        if (access(p, R_OK) == 0) return p;
    return nullptr;
}

// The town's list or large touch screen came up: what the fingers held on the touch controls lets go (their ups go to
// town.cpp now); the settings over them close the list
void town_watch() {
    static bool was = false;
    if (g_settings_open) town_close();
    if (g_settings_open) weapon_tree_close();  // (1.0.2: the Smithy's weapon tree, weapon_tree_ui.h: the same)
    const bool busy = town_busy() || weapon_tree_is_open();
    if (busy && !was) touch_reset();
    was = busy;
}

// Whether the window can take a frame of the menus now. SDL marks it minimized as the app goes to the background
// (another of the app's screens over the game, as the code builder's; the home screen; the screen off) for the one loop
// turn before it waits, and ImGui's SDL backend then gives a 0 x 0 display: the settings' sizes, made from the screen's,
// came out at 0 or below, and a section's name asked for at a size below 0 crashed ImGui's font atlas (a SIGSEGV in
// stbrp__skyline_find_best_pos, the settings open behind the builder: "the app closes at Translate the game's code
// (10 %)"). Nothing of the menus is drawn then; the next frame, back in front, is as before.
bool ui_drawable() {
    if (g_window == nullptr) return true;
    int w = 0, h = 0;
    const bool minimized = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MINIMIZED) != 0;
    const bool drawable = !minimized && SDL_GetWindowSize(g_window, &w, &h) && w > 0 && h > 0;
    static bool was = true;
    static int said = 0;
    if (drawable != was && said < 20) {  // (each change, the first 20 times)
        ++said;
        std::fprintf(stderr, "[Info] App: %s\n", drawable ? "the window is back: the menus drawn again"
                                                          : minimized ? "the window is minimized: no frame of the menus"
                                                                      : "the window has no size: no frame of the menus");
    }
    was = drawable;
    return drawable;
}

}  // namespace

void ui_init(SDL_Window* window, SDL_Renderer* renderer, const std::string& data_dir) {
    g_renderer = renderer;
    g_window = window;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    g_ini = data_dir + "/imgui.ini";
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
    ImFontConfig cfg;
    cfg.SizePixels = 32.0f;
#ifdef _WIN32  // (the PC: Windows' own, Segoe UI's semibold and Georgia's bold)
    const char* sans = first_font({"C:/Windows/Fonts/seguisb.ttf", "C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/arialbd.ttf"});
    const char* serif = first_font({"C:/Windows/Fonts/georgiab.ttf", "C:/Windows/Fonts/georgia.ttf"});
#else
    const char* sans = first_font({"/system/fonts/SourceSansPro-SemiBold.ttf", "/system/fonts/Roboto-Medium.ttf",
                                   "/system/fonts/DroidSans-Bold.ttf", "/system/fonts/RobotoStatic-Regular.ttf",
                                   "/system/fonts/Roboto-Regular.ttf"});
    const char* serif = first_font({"/system/fonts/NotoSerif-Bold.ttf", "/system/fonts/NotoSerif-Regular.ttf"});
#endif
    ImFont* font = sans ? io.Fonts->AddFontFromFileTTF(sans, cfg.SizePixels, &cfg) : nullptr;
    if (!font) io.Fonts->AddFontDefaultVector(&cfg);
    if (serif) io.Fonts->AddFontFromFileTTF(serif, cfg.SizePixels, &cfg);  // (Fonts[1]: touch.cpp's titles)
    std::fprintf(stderr, "[Info] App: ImGui %s, font %s, titles %s\n", IMGUI_VERSION, sans ? sans : "ImGui's",
                 serif ? serif : "the same");
    ImGui::StyleColorsDark();
    ImGui::GetStyle().FontSizeBase = cfg.SizePixels;
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    touch_init(renderer, data_dir);
}

// (1.0.2, after tabarong's floating chat) The chat window afloat (settings_panel.h). A finger is the window's only on its
// controls - those reach Dear ImGui as its mouse, by hand; the mouse SDL makes of the fingers is left out meanwhile. A
// finger anywhere else goes past the settings to the touch controls and the game, as with the menu closed; while typing,
// such a tap closes the window and the phone's keyboard as well. A desktop's mouse the same way: a button pressed on the
// window's controls is the window's until it lets go, anywhere else the game's (typing: the window closes first), its other
// moves both's; the keys and the text are the game's - it goes on under the window - but while Say something is typed in
// it (a key's release both's, so nothing stays held). Routes:
enum { ROUTE_MENU, ROUTE_TAKEN, ROUTE_GAME, ROUTE_BOTH };
static SDL_FingerID g_chat_finger;
static bool g_chat_fingered, g_chat_mouse;
static int chat_float_event(const SDL_Event& e) {
    ImGuiIO& io = ImGui::GetIO();
    auto on_window = [&](float x, float y) {
        return io.DisplaySize.x > 0 && io.DisplaySize.y > 0 && settings_panel_chat_claims(x / io.DisplaySize.x, y / io.DisplaySize.y);
    };
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION:
        if (e.motion.which == SDL_TOUCH_MOUSEID) return ROUTE_TAKEN;  // (the mouse made of a finger: the fingers say)
        return g_chat_mouse ? ROUTE_MENU : ROUTE_BOTH;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.which == SDL_TOUCH_MOUSEID) return ROUTE_TAKEN;
        if (on_window(e.button.x, e.button.y)) {
            g_chat_mouse = true;
            return ROUTE_MENU;
        }
        if (settings_panel_chat_typing()) {
            settings_panel_chat_request(SETTINGS_CHAT_CLOSE);
            std::fprintf(stderr, "[input] a click away from the chat window while typing: it closes\n");
        }
        return ROUTE_GAME;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.which == SDL_TOUCH_MOUSEID) return ROUTE_TAKEN;
        if (g_chat_mouse) {
            g_chat_mouse = false;
            return ROUTE_MENU;
        }
        return ROUTE_BOTH;
    case SDL_EVENT_MOUSE_WHEEL:
        if (e.wheel.which == SDL_TOUCH_MOUSEID) return ROUTE_TAKEN;
        return on_window(e.wheel.mouse_x, e.wheel.mouse_y) ? ROUTE_MENU : ROUTE_GAME;
#ifndef __ANDROID__  // (a phone's keys - Back, its keyboard's - stay the menu's, as before)
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_TEXT_INPUT:
        return settings_panel_chat_typing() ? ROUTE_MENU : ROUTE_GAME;
    case SDL_EVENT_KEY_UP:
        return settings_panel_chat_typing() ? ROUTE_MENU : ROUTE_BOTH;
#endif
    case SDL_EVENT_FINGER_DOWN:
        if (e.tfinger.touchID == SDL_MOUSE_TOUCHID) return ROUTE_GAME;
        if (!g_chat_fingered && on_window(e.tfinger.x * io.DisplaySize.x, e.tfinger.y * io.DisplaySize.y)) {
            g_chat_fingered = true, g_chat_finger = e.tfinger.fingerID;
            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            io.AddMousePosEvent(e.tfinger.x * io.DisplaySize.x, e.tfinger.y * io.DisplaySize.y);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            return ROUTE_TAKEN;
        }
        if (settings_panel_chat_typing()) {
            settings_panel_chat_request(SETTINGS_CHAT_CLOSE);
            std::fprintf(stderr, "[touch] a tap away from the chat window while typing: it closes, the keyboard too\n");
        }
        return ROUTE_GAME;
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        if (!g_chat_fingered || e.tfinger.fingerID != g_chat_finger) return ROUTE_GAME;
        io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
        if (e.type == SDL_EVENT_FINGER_CANCELED) io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);  // (taken by the system: no tap)
        else io.AddMousePosEvent(e.tfinger.x * io.DisplaySize.x, e.tfinger.y * io.DisplaySize.y);
        if (e.type != SDL_EVENT_FINGER_MOTION) {
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);  // (the finger is gone: nothing stays hovered)
            g_chat_fingered = false;
        }
        return ROUTE_TAKEN;
    default:
        return ROUTE_MENU;
    }
}

// (1.0.2) the settings are up as a menu, the game's input theirs: the chat window afloat is none - the game and its
// controls go on under it -, but while Say something is typed in it (the keys are the text then)
bool ui_menu_up() {
    return g_settings_open && !(settings_panel_chat_floating() && !settings_panel_chat_typing());
}

bool ui_event(const SDL_Event& e, const UiScreens& screens) {
    // (1.0.2) the chat window afloat: whose each event is (chat_float_event) - taken here, the menu's as ever, past the
    // settings to the controls and the game, or both
    int route = ROUTE_MENU;
    if (g_settings_open && settings_panel_chat_floating()) {
        route = chat_float_event(e);
        if (route == ROUTE_TAKEN) return true;
    } else if (g_chat_fingered && (e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED) &&
               e.tfinger.fingerID == g_chat_finger) {  // (the window gone under the finger: let go of)
        g_chat_fingered = false;
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        return true;
    }
    // A press carries its place: when another screen of the app's came over the game (a file picker), ImGui's backend
    // forgot the mouse's place at the window's mouse-leave, and a tap where the last one was comes with no motion event,
    // so it landed nowhere (the Mods and Cheats pages' import rows took no second tap after a cancelled pick)
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP)
        ImGui::GetIO().AddMousePosEvent(e.button.x, e.button.y);
    if (route != ROUTE_GAME) ImGui_ImplSDL3_ProcessEvent(&e);
    if (settings_panel_thanks_event(e)) return true;  // (the thanks card takes everything while it shows)
#ifndef __ANDROID__
    if (pc_input_event(e)) return true;  // (the list of the controls, while it shows)
    // (the PC: the layout editor with the mouse's wheel, in the window's pixels as the fingers are, and Esc to cancel)
    if (!g_settings_open && touch_editing()) {
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE && !e.key.repeat && touch_edit_escape()) return true;
        if (e.type == SDL_EVENT_MOUSE_WHEEL && e.wheel.which != SDL_TOUCH_MOUSEID) {
            int ww = 0, wh = 0, pw = 0, ph = 0;
            SDL_GetWindowSize(g_window, &ww, &wh);
            SDL_GetRenderOutputSize(g_renderer, &pw, &ph);
            const float kx = ww > 0 ? (float)pw / ww : 1.f, ky = wh > 0 ? (float)ph / wh : 1.f;
            const int n = (int)e.wheel.integer_y * (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1);
            if (touch_edit_wheel(e.wheel.mouse_x * kx, e.wheel.mouse_y * ky, n)) return true;
        }
    }
#endif
    // the settings, while open, take the fingers, the controller and the keys (not their releases); closed, the Android
    // Back key or a controller's guide button opens them, as the settings button does (touch.cpp)
    if (route == ROUTE_MENU && settings_panel_event(e, g_settings_open)) return true;
    if (!g_settings_open && game_keyboard_event(e)) return true;  // (the phone's keyboard for the game's text)
    // (Android's Back, the key or the gesture: the town's list or large touch screen closes first, and the Smithy's
    // weapon tree)
    if (!g_settings_open && e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_AC_BACK && !e.key.repeat &&
        (weapon_tree_back() || town_back()))
        return true;
    if (!g_settings_open && ((e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_AC_BACK && !e.key.repeat) ||
                             (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE))) {
        ui_open_settings();
        return true;
    }
    switch (e.type) {
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        touch_reset();  // (every finger gone: their ups will not come)
        return false;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        // (the town's list or its large touch screen: the touch controls are away, the module takes the fingers)
        if (weapon_tree_is_open()) return weapon_tree_event(e);  // (1.0.2: over everything but the settings)
        if (town_busy()) return town_event(e);
        return touch_event(e, screens);
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        return touch_pad_button(e);
    default:
        return false;
    }
}

void ui_frame(int width, int height, const UiScreens& screens, bool controller) {
    if (!ui_drawable()) {  // (going to the background: no frame of the menus, nothing held)
        g_input = UiInput{};
        return;
    }
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    // this frame's buttons and sticks (the camera's turn adds up until ui_take_input hands it over)
    UiInput in;
    in.cam_yaw = g_input.cam_yaw, in.cam_pitch = g_input.cam_pitch;
    town_watch();
    // (under the settings: none; under the town's list and large touch screen neither: town.h. The chat window afloat is
    // no menu here: the game goes on under it with all of these, 1.0.2)
    const bool thanks = settings_panel_thanks_open();
    const bool menu = g_settings_open && !settings_panel_chat_floating();
    if (!menu && !thanks) hunt_numbers_draw(screens.top);  // (1.0.1: damage numbers, under the controls)
    if (!menu && !thanks) hunt_bars_draw(screens.top);     // (1.0.1: the monsters' health bars, the same)
    if (!menu && !thanks) quick_items_draw_stand_in();     // (1.0.1.1: the quick items' stand-in, the editor's)
    if (!menu && !town_busy() && !weapon_tree_is_open() && !thanks) touch_frame(width, height, screens, controller, in);
    if (!menu && !thanks) town_frame(width, height, controller);
    if (!menu && !thanks) weapon_tree_frame(width, height, controller);  // (1.0.2: the Smithy's weapon tree)
    game_keyboard_frame(width, height, g_settings_open);
#ifndef __ANDROID__
    pc_input_frame(width, height);  // (the list of the controls; the first starts' line)
#endif
    settings_panel(&g_settings_open);  // (also the FPS counter, while it is on)
    g_input = in;
    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), g_renderer);
}

void ui_redraw() {
    ImDrawData* d = ImGui::GetDrawData();
    if (ui_drawable() && d != nullptr && d->Valid) ImGui_ImplSDLRenderer3_RenderDrawData(d, g_renderer);
}

void ui_update(int width, int height, const UiScreens& screens, bool controller) {
    if (!ui_drawable()) {  // (as ui_frame)
        g_input = UiInput{};
        return;
    }
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    UiInput in;
    in.cam_yaw = g_input.cam_yaw, in.cam_pitch = g_input.cam_pitch;
    town_watch();
    if ((!g_settings_open || settings_panel_chat_floating()) && !town_busy() && !weapon_tree_is_open() && !settings_panel_thanks_open())
        touch_frame(width, height, screens, controller, in);
    game_keyboard_frame(width, height, g_settings_open);
    settings_panel(&g_settings_open);
    g_input = in;
    ImGui::EndFrame();
}

UiInput ui_take_input() {
    UiInput out = g_input;
    g_input.cam_yaw = g_input.cam_pitch = 0;
    return out;
}

bool ui_bottom_wanted() {
    return touch_bottom_wanted();
}

void ui_set_bottom_default(bool shown) {
    touch_set_bottom_default(shown);
}

void ui_set_one_screen(bool on) {
    touch_set_one_screen(on);
}

void ui_toggle_bottom() {
    touch_toggle_bottom();
}

void ui_set_game_state(const UiGameState& state) {
    touch_set_game_state(state);
}

void ui_open_settings() {
    if (g_settings_open && settings_panel_chat_floating()) {  // (1.0.2: the settings button, the chat window afloat: the menu)
        settings_panel_chat_request(SETTINGS_CHAT_TO_MENU);
        return;
    }
    if (g_settings_open) return;
    g_settings_open = true;
    touch_reset();  // (what the fingers held lets go: the settings take the screen)
    std::fprintf(stderr, "[Info] App: settings\n");
}

bool ui_settings_open() {
    return g_settings_open;
}

void ui_shutdown() {
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}
