// MH4U for PC: the window and its display (pc_display.h).
#include "pc_display.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "app_settings.h"

#include <dxgi.h>  // (last: windows.h's macros)

namespace {

PcWindowMode ModeOf(const std::string& text) {
    return text == "windowed" ? PC_WINDOWED : text == "fullscreen" ? PC_FULLSCREEN : PC_BORDERLESS;
}
const char* NameOf(PcWindowMode m) { return m == PC_WINDOWED ? "windowed" : m == PC_FULLSCREEN ? "fullscreen" : "borderless"; }

// The windowed size: [Display] WindowSize, else four fifths of the display's usable area at 16:9
void WindowedSize(SDL_DisplayID display, int* w, int* h) {
    SDL_Rect usable{0, 0, 1920, 1080};
    SDL_GetDisplayUsableBounds(display, &usable);
    int ww = 0, hh = 0;
    if (std::sscanf(app_setting("Display", "WindowSize").c_str(), "%dx%d", &ww, &hh) == 2 && ww >= 640 && hh >= 360) {
        *w = std::min(ww, usable.w), *h = std::min(hh, usable.h);
        return;
    }
    ww = usable.w * 4 / 5, hh = ww * 9 / 16;
    if (hh > usable.h * 4 / 5) hh = usable.h * 4 / 5, ww = hh * 16 / 9;
    *w = ww, *h = hh;
}

// The exclusive mode [Display] FullscreenMode names on `display` (the closest SDL has), null: the desktop's
const SDL_DisplayMode* ExclusiveMode(SDL_DisplayID display) {
    int w = 0, h = 0;
    float hz = 0;
    const std::string want = app_setting("Display", "FullscreenMode");
    if (std::sscanf(want.c_str(), "%dx%d@%f", &w, &h, &hz) < 2 || w <= 0 || h <= 0) return nullptr;
    static SDL_DisplayMode closest;
    if (!SDL_GetClosestFullscreenDisplayMode(display, w, h, hz, true, &closest)) return nullptr;
    return &closest;
}

void GoFull(SDL_Window* window, PcWindowMode mode) {
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    const SDL_DisplayMode* exclusive = mode == PC_FULLSCREEN ? ExclusiveMode(display) : nullptr;
    if (mode == PC_FULLSCREEN && exclusive == nullptr) {  // (the desktop's own mode, exclusive)
        static SDL_DisplayMode desktop;
        if (const SDL_DisplayMode* d = SDL_GetDesktopDisplayMode(display)) desktop = *d, exclusive = &desktop;
    }
    SDL_SetWindowFullscreenMode(window, exclusive);  // (null: borderless at the desktop's resolution)
    SDL_SetWindowFullscreen(window, true);
    std::fprintf(stderr, "[app] the window: %s%s\n", exclusive ? "full screen, exclusive " : "borderless full screen",
                 exclusive ? (std::to_string(exclusive->w) + "x" + std::to_string(exclusive->h) + "@" +
                              std::to_string(static_cast<int>(std::lround(exclusive->refresh_rate)))).c_str()
                           : "");
}

void GoWindowed(SDL_Window* window) {
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    SDL_SetWindowFullscreen(window, false);
    int w = 0, h = 0;
    WindowedSize(display, &w, &h);
    SDL_SetWindowSize(window, w, h);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    std::fprintf(stderr, "[app] the window: windowed %dx%d\n", w, h);
}

}  // namespace

PcWindowMode pc_display_mode() { return ModeOf(app_setting("Display", "WindowMode", "borderless")); }

SDL_Window* pc_display_create_window(const char* title, SDL_DisplayID display, SDL_WindowFlags extra) {
    const PcWindowMode mode = pc_display_mode();
    int w = 0, h = 0;
    WindowedSize(display, &w, &h);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, w);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, h);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER,
                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN | extra);
    SDL_Window* window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (window == nullptr) return nullptr;
    SDL_SetWindowMinimumSize(window, 640, 360);
    if (mode != PC_WINDOWED) GoFull(window, mode);
    else std::fprintf(stderr, "[app] the window: windowed %dx%d\n", w, h);
    SDL_ShowWindow(window);
    SDL_SyncWindow(window);
    return window;
}

void pc_display_toggle(SDL_Window* window) {
    const bool full = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    PcWindowMode mode = pc_display_mode();
    if (full) {
        GoWindowed(window);
        if (mode != PC_WINDOWED) app_setting_set("Display", "FullscreenKind", NameOf(mode));
        mode = PC_WINDOWED;
    } else {
        // (back to the full screen it was: borderless unless exclusive was the one chosen)
        mode = ModeOf(app_setting("Display", "FullscreenKind", "borderless"));
        if (mode == PC_WINDOWED) mode = PC_BORDERLESS;
        GoFull(window, mode);
    }
    app_setting_set("Display", "WindowMode", NameOf(mode));
    app_settings_save();
}

void pc_display_apply(SDL_Window* window) {
    const PcWindowMode mode = pc_display_mode();
    const bool full = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    if (mode == PC_WINDOWED) {
        if (full) GoWindowed(window);
        return;
    }
    // (the full screen chosen, again: borderless or exclusive, its mode as the settings now say)
    const SDL_DisplayMode* now = SDL_GetWindowFullscreenMode(window);
    const SDL_DisplayMode* want = mode == PC_FULLSCREEN ? ExclusiveMode(SDL_GetDisplayForWindow(window)) : nullptr;
    const bool same = full && ((now == nullptr && mode == PC_BORDERLESS) ||
                               (now != nullptr && mode == PC_FULLSCREEN &&
                                (want == nullptr || (now->w == want->w && now->h == want->h &&
                                                     std::fabs(now->refresh_rate - want->refresh_rate) < 0.5f))));
    if (!same) GoFull(window, mode);
}

void pc_display_remember(SDL_Window* window) {
    if ((SDL_GetWindowFlags(window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED)) != 0) return;
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    if (w < 640 || h < 360) return;
    const std::string size = std::to_string(w) + "x" + std::to_string(h);
    if (size == app_setting("Display", "WindowSize")) return;
    app_setting_set("Display", "WindowSize", size);
    app_settings_save();
}

bool pc_display_vsync() { return app_setting_bool("Graphics", "VSync", true); }

int pc_display_upscale() {
    const std::string v = app_setting("Graphics", "Upscale", "smooth");
    return v == "sharp" ? 1 : v == "nearest" ? 2 : 0;
}

std::vector<std::string> pc_display_modes(SDL_Window* window) {
    std::vector<std::string> out;
    const SDL_DisplayID display = window != nullptr ? SDL_GetDisplayForWindow(window) : SDL_GetPrimaryDisplay();
    const auto name = [](const SDL_DisplayMode& m) {
        return std::to_string(m.w) + "x" + std::to_string(m.h) + "@" + std::to_string(static_cast<int>(std::lround(m.refresh_rate)));
    };
    if (const SDL_DisplayMode* d = SDL_GetDesktopDisplayMode(display)) out.push_back(name(*d));
    int n = 0;
    if (SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &n)) {
        for (int i = 0; i < n; ++i) {
            const std::string m = name(*modes[i]);
            if (std::find(out.begin(), out.end(), m) == out.end()) out.push_back(m);
        }
        SDL_free(modes);
    }
    return out;
}

namespace {
SDL_Window* g_window = nullptr;
}
SDL_Window* pc_window() { return g_window; }
void pc_set_window(SDL_Window* window) { g_window = window; }

std::vector<std::string> pc_gpu_names() {
    std::vector<std::string> out;
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) return out;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d{};
        if (SUCCEEDED(adapter->GetDesc1(&d)) && (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) {
            char name[256] = {};
            WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, sizeof name, nullptr, nullptr);
            if (*name != 0 && std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
        }
        adapter->Release();
    }
    factory->Release();
    return out;
}
