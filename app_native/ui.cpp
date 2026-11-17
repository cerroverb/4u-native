// The app's interface (ui.h): Dear ImGui on SDL_Renderer, and on it the touch controls in the Monster Hunter style
// (touch.cpp, from the MHGU port) and the settings (settings_panel.cpp, from the MHGU port too). Everything here runs on
// the app's main thread.
#include "ui.h"

#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "game_keyboard.h"
#include "hunt_bars.h"
#include "hunt_numbers.h"
#include "settings_panel.h"
#include "touch.h"
#include "town.h"

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
    const bool busy = town_busy();
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
    const char* sans = first_font({"/system/fonts/SourceSansPro-SemiBold.ttf", "/system/fonts/Roboto-Medium.ttf",
                                   "/system/fonts/DroidSans-Bold.ttf", "/system/fonts/RobotoStatic-Regular.ttf",
                                   "/system/fonts/Roboto-Regular.ttf"});
    const char* serif = first_font({"/system/fonts/NotoSerif-Bold.ttf", "/system/fonts/NotoSerif-Regular.ttf"});
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

bool ui_event(const SDL_Event& e, const UiScreens& screens) {
    // A press carries its place: when another screen of the app's came over the game (a file picker), ImGui's backend
    // forgot the mouse's place at the window's mouse-leave, and a tap where the last one was comes with no motion event,
    // so it landed nowhere (the Mods and Cheats pages' import rows took no second tap after a cancelled pick)
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP)
        ImGui::GetIO().AddMousePosEvent(e.button.x, e.button.y);
    ImGui_ImplSDL3_ProcessEvent(&e);
    if (settings_panel_thanks_event(e)) return true;  // (the thanks card takes everything while it shows)
    // the settings, while open, take the fingers, the controller and the keys (not their releases); closed, the Android
    // Back key or a controller's guide button opens them, as the settings button does (touch.cpp)
    if (settings_panel_event(e, g_settings_open)) return true;
    if (!g_settings_open && game_keyboard_event(e)) return true;  // (the phone's keyboard for the game's text)
    // (Android's Back, the key or the gesture: the town's list or large touch screen closes first)
    if (!g_settings_open && e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_AC_BACK && !e.key.repeat &&
        town_back())
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
    // (under the settings: none; under the town's list and large touch screen neither: town.h)
    const bool thanks = settings_panel_thanks_open();
    if (!g_settings_open && !thanks) hunt_numbers_draw(screens.top);  // (1.0.1: damage numbers, under the controls)
    if (!g_settings_open && !thanks) hunt_bars_draw(screens.top);     // (1.0.1: the monsters' health bars, the same)
    if (!g_settings_open && !town_busy() && !thanks) touch_frame(width, height, screens, controller, in);
    if (!g_settings_open && !thanks) town_frame(width, height, controller);
    game_keyboard_frame(width, height, g_settings_open);
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
    if (!g_settings_open && !town_busy() && !settings_panel_thanks_open()) touch_frame(width, height, screens, controller, in);
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
