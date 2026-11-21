// The game's touch screen on a second display of the device (1.0.2, S38: dual-screen handhelds), as the emulators do it:
// Settings > Graphics & Audio > Screen and HUD > Layout > On the second screen ([Display] Layout=second of mh4u-app.ini,
// off by default; the choice shows only while Android reports such a display).
//
// Android lists that display as a presentation display (DisplayManager.DISPLAY_CATEGORY_PRESENTATION). The Java side
// (com.mh4u.port.SecondScreen) says when one comes and goes, and when the main loop asks for it opens a Presentation on it
// with a SurfaceView; its surface comes here, and an EGL window surface of its own is made on it with SDL's context (and
// its config). The touch screen is drawn into an SDL render target of the picture's size as the window would draw it
// (shared_screens_draw_into: the colour filter too), and that target onto the surface, which is swapped with an interval
// of 0: the second display never makes the main loop wait (the window keeps its own pacing, present_pacing.cpp). The
// fingers on it come back as SDL events of this module's (SDL_PushEvent, which wakes the main loop), already turned
// into the touch screen's coordinates.
//
// When the display goes away (or the Presentation closes, or the app goes to the background) the surface goes, and the
// main loop shows the touch screen in the window again with the layout the player had before ([Display] LayoutBefore).
// Main thread (SDL's) unless said otherwise.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <vector>

struct SharedScreenView;

// Once, after shared_screens_init: SDL's EGL display, context and config, the event type of the fingers.
void second_screen_init(SDL_Renderer* renderer);
// Android reports a presentation display apart from the game's own (any thread)
bool second_screen_display_present();
// The second display's window has a surface to draw into: the touch screen goes there, not into the window
bool second_screen_ready();
// The surface is new (or was lost) and has not been drawn yet: a present is due even with nothing new
bool second_screen_needs_draw();
// Draws `view` (the touch screen) on the second display, as large as it fits with its shape, black beside it, and
// presents it. Outside SDL's frame: after SDL_RenderPresent. False: no surface (or it could not be drawn: logged once).
bool second_screen_draw(SharedScreenView& view);

// The SDL events of this module (SDL_EVENT_USER and up; 0 before second_screen_init)
Uint32 second_screen_event_type();
enum SecondScreenAction { SECOND_TOUCH_DOWN = 0, SECOND_TOUCH_MOVE = 1, SECOND_TOUCH_UP = 2, SECOND_SURFACE = 3 };
// One of those events: what it is, and for a finger where it is on the touch screen (320 x 240, kept on it) and whether
// it is on the picture (not on the black beside it)
SecondScreenAction second_screen_event(const SDL_Event& e, int* x, int* y, bool* inside);

// MH4U_TEST_SECOND_SCREEN=1 (launch.txt's env=, a test without the game): a test card of the touch screen's size (RGBA8,
// 320 x 240) with a cross where the last finger was (x < 0: none), for the main loop to show as the touch screen
std::shared_ptr<const std::vector<std::uint8_t>> second_screen_test_card(int x, int y);

// The GL context was lost: the program, the buffer and the target are made again when needed
void second_screen_reset();
// Before SDL's renderer goes
void second_screen_shutdown();
