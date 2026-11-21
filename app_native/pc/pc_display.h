// MH4U for PC: the window and its display (pc_display.cpp), as MHGU for PC has them.
//
// [Display] WindowMode in mh4u-app.ini: borderless (the default: a borderless full screen at the desktop's resolution, on
// the display the window is on), windowed (a window of any size: [Display] WindowSize = WxH, the last one used; resizing
// changes the picture's shape at once) or fullscreen (exclusive, at [Display] FullscreenMode = WxH@Hz, the desktop's by
// default). F11 or Alt+Enter switches between the window and the full screen chosen (kept for the next start).
// [Graphics] VSync on|off (on by default: off lets the window show every picture the game makes, up to its FPS limit or
// none, tearing where the display shows it), [Graphics] Upscale smooth|sharp|nearest (the 3D picture's filter when it is
// scaled to the window).
#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

enum PcWindowMode { PC_BORDERLESS = 0, PC_WINDOWED = 1, PC_FULLSCREEN = 2 };

PcWindowMode pc_display_mode();
// The window, on `display`, as [Display] WindowMode says (shown); null with SDL's error
SDL_Window* pc_display_create_window(const char* title, SDL_DisplayID display, SDL_WindowFlags extra);
// F11 / Alt+Enter: the window <-> the full screen chosen (WindowMode kept)
void pc_display_toggle(SDL_Window* window);
// The settings changed (the menu closed): the window as they say now
void pc_display_apply(SDL_Window* window);
// Before the program closes: a window's size kept for the next start
void pc_display_remember(SDL_Window* window);
bool pc_display_vsync();
int pc_display_upscale();  // 0 smooth, 1 sharp pixels, 2 nearest
// The game's window (set by pc_main.cpp once made; null before)
SDL_Window* pc_window();
void pc_set_window(SDL_Window* window);
// The GPUs of this PC by name (DXGI's adapters, the software one left out; each name once): the settings' GPU row, a
// name the renderer's Vulkan device is chosen by (MH4N_VULKAN_DEVICE: a part of its name)
std::vector<std::string> pc_gpu_names();
// The exclusive full screen's modes of the display the window is on ("2560x1440@75"), the desktop's first
std::vector<std::string> pc_display_modes(SDL_Window* window);
