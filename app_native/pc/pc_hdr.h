// MH4U for PC: the HDR output ([Colour] HDR, HDRBrightness, HDRHighlights: the MHGU port's, runtime/colour_filter.h),
// the game's SDR picture shown on an HDR screen. Its white goes to the paper white (203 nits times 2^(HDRBrightness/100));
// the brightest part of the picture (above half of the white, in light) is spread up towards the screen's peak, by
// HDRHighlights (0: half of the way, -100: none, 100: all of the way), the colours kept. The touch controls, the menus
// and the readout stay at the paper white, unspread (the MHGU port's overlay).
//
// How: the window keeps SDL's OpenGL renderer and every draw it has (the screens' programs, the colour filter, PS1
// style, frame generation, the menus), into two pictures of the window's size instead of the window (the game's, and the
// overlay's over a clear one); a program of ours puts the two together in light (scRGB: linear, BT.709, 1.0 = 80 nits)
// into a 16-bit float texture that is a D3D11 one too (WGL_NV_DX_interop2), copied into a flip-model swapchain (DXGI,
// scRGB) of a child window over the whole of SDL's (the mouse passes through it) and presented there. With HDR off (the default) none of it exists: the window is presented
// by SDL as always. Chosen at the start (a restart to change it); used only where Windows' HDR is on for the window's
// display then. If the display's HDR goes off later the picture goes on in scRGB at SDR's white, unspread.
#pragma once

#include <SDL3/SDL.h>

#include <string>

// Windows' HDR is on for the window's display now (its peak, in nits, in `peak_nits`: 0 not known); read again at
// most once a second
bool pc_hdr_display(SDL_Window* window, float* peak_nits = nullptr);
// The HDR output started at the start ([Colour] HDR on and the display's HDR on then) and is drawing now
bool pc_hdr_on();
// 0 [Colour] HDR off at the start; 1 on; 2 on, but Windows' HDR was off for the window's display at the start (SDR);
// 3 on, but not possible with this driver (logged; SDR)
int pc_hdr_state();
// At the start, the renderer made: [Colour] HDR on and the display's HDR on: the D3D11 swapchain and the interop made
// (false: not possible here, logged; SDL presents as always)
bool pc_hdr_start(SDL_Window* window, SDL_Renderer* renderer);
// A frame in HDR: SDL draws the game into the game's picture from here (false: HDR not on, the window as always)
bool pc_hdr_begin(SDL_Renderer* renderer, int w, int h);
// ... the overlay (the touch controls, the menus, the readout) into its own picture from here
void pc_hdr_overlay(SDL_Renderer* renderer);
// ... the two into the window, presented (VSync on: at the display's refresh; off: at once, tearing allowed)
void pc_hdr_present(SDL_Renderer* renderer, bool vsync);
// The frame drawn last as an ordinary picture (the overlay over the game; tests' shots), or null
SDL_Surface* pc_hdr_read(SDL_Renderer* renderer);
// (tests) The next picture presented, read back and written to `path` as an ordinary PNG of it divided by the paper
// white (pc_hdr.cpp SaveOutput); MH4U_HDR_FORCE=1 makes the output as for HDR on an SDR display too
void pc_hdr_save_output(const std::string& path);
// The settings' rows while the menu is open: the brightness and the highlights shown as they are moved
void pc_hdr_preview(int brightness, int highlights);
void pc_hdr_shutdown();
