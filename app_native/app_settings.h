// The app's own options (mh4u-app.ini in the app's data folder, the external files folder by default): the things the
// app decides and the game's mh4u.ini doesn't hold (game_options.h has those). The settings panel writes them;
// android_main.cpp and ui.cpp read them. An INI file of [Sections] and `Key = value` lines; its comments, its order and
// the keys this build doesn't know stay as they are when it is written again.
//
//   [Graphics]  RenderScale = 100|75|50|0 (the 3D resolution, as MHGU's: a percent of the phone's own, 0 the 3DS's;
//               render_scale_times makes it the runtime's --scale; next start)
//               LowEnd = off|on|toaster (the preset's choice, as the panel last set it), FpsCounter = on|off,
//               Shadows = on|off (at once: app_live.h)
//   [Audio]     Volume = 0..100, Reverb = on|off (at once: app_live.h)
//   [Display]   Layout = overlay|stacked|side, Corner = bottom-right|bottom-left|top-right|top-left,
//               Size = 20..60 (% of the window's height), Opacity = 30..100, TouchScreen = shown|hidden
//   [Controls]  Haptics = on|off (a short vibration for a pressed touch control)
//   [Interface] Language = auto|en|es (auto: the phone's), Theme = hunter|gold|blue, Scale = 50..200 (the menu's size, %),
//               PauseInBackground = on|off (the game stops while the app is away; off: it goes on, a room's hunt too)
//   [Network]   Nickname (the room's), Address = host[:port] (the room to join; empty: none), Password,
//               HostPort = 1..65535 (24872), Slots = 2..16 (a room hosted on the phone), Lobby (a lobby's address;
//               empty: none: the app has no lobby of its own), Notices = on|off (the room's lines over the game)
//
// Any thread (one lock); the file is read the first time it is asked for, and again by app_settings_reload().
#pragma once
#include <string>

// The folder of mh4u-app.ini (android_main.cpp: the external files folder, the one launch.txt is in). Without it, the
// app's external files folder (SDL_GetAndroidExternalStoragePath). Reads the file.
void app_settings_init(const std::string& data_dir);
std::string app_settings_path();
// Reads the file again (after something else wrote it).
void app_settings_reload();

// A value: the file's (sections and keys in any case), else `fallback`.
std::string app_setting(const char* section, const char* key, const std::string& fallback = "");
int app_setting_int(const char* section, const char* key, int fallback);
// on / 1 / true / yes / shown -> true; off / 0 / false / no / hidden -> false; anything else: `fallback`.
bool app_setting_bool(const char* section, const char* key, bool fallback);

// Sets a value in memory (app_settings_save writes it): under its section, where it was, else at the section's end.
void app_setting_set(const char* section, const char* key, const std::string& value);
void app_setting_set_int(const char* section, const char* key, int value);
// Writes the file (a temporary file renamed over it). False, with *error, when it can't.
bool app_settings_save(std::string* error = nullptr);
// Goes up by one each time the file is saved: a reader that keeps the number knows when to read the options again.
unsigned app_settings_version();

// The 3D resolution. The renderer draws at whole multiples of the 3DS's top screen (400x240, `--scale N`); the player
// picks a percent of the phone's own resolution, as in MHGU. The top screen shows at most as tall as the window and,
// on a screen narrower than 5:3, as wide: at 100 % the multiple that is at least that many lines (720 lines: 3x;
// 1080: 5x; 1440: 6x), below it the nearest one, and 0 is the 3DS's (1x). The window's size (landscape or not) is
// android_main's, given once to app_set_window_size.
inline constexpr int kRenderScaleDefault = 100;
void app_set_window_size(int w, int h);
int render_scale_times(int percent);

// Low-end mode, as MHGU's ([Graphics] LowEnd; the settings' preset, and a weak phone's first start, first_run.h). On: frames
// before looks: low_end_fps() (60, or 30 on a phone without a fast core), the 3D resolution at most kLowEndScale %, the
// game's screen filters and anti-aliasing and the shadows off. Toaster: the battery and the heat before everything: a
// steady kToasterFps, the 3DS's resolution, and the reverb off too. (An even 30: the performance hints' target at 70 % of
// the period, perf_hint.c, and the buffers' times on the vsync grid, present_pacing.) Each option can be changed again afterwards.
enum { LOW_END_OFF, LOW_END_ON, LOW_END_TOASTER };
inline constexpr int kLowEndScale = 33, kToasterScale = 0;  // (33 %: 1x on a 720-line screen)
inline constexpr int kLowEndFpsFast = 60, kLowEndFpsSlow = 30, kToasterFps = 30;
// Low-end mode's frame rate on this phone: 60 where it has a fast core (Cortex-A75 class or newer: the game and the
// renderer each keep one busy; a Moto g06 runs 58-59 FPS at 1x in a hunt), 30 where every core is a small one (Cortex-A53,
// A55 and their kind: first_run_fast_cores)
int low_end_fps();

inline constexpr const char* kLowEndFilters[] = {"bloom", "motion_blur", "depth_of_field", "radial_blur", "haze"};
// LowEnd's text (off|on|toaster, and the switch's 0/1/true/false of older files) as LOW_END_*
int app_low_end_level(const std::string& text);
