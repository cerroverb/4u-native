// The port's options while the game runs (the settings panel): queued from any thread, applied on the game's own
// thread at its next frame (Port::Hooks::SetOption must run there), and written to mh4u.ini so they stay.
#pragma once
#include <string>
#include <vector>

// Queues `key = value` for the hooks (fps, widescreen, hud_edges, bloom, motion_blur, depth_of_field, radial_blur,
// haze, kick, target, skip, ...). Returns false for a key the hooks can't change while the game runs: those are
// saved and apply at the next start.
bool game_option_set(const std::string& key, const std::string& value);
// Queues `key = value` for the hooks without writing it to mh4u.ini (the layout editor while a HUD part is dragged: the
// game shows it at once; game_option_set keeps it, or the old value goes back). False for a key the game can't change now.
bool game_option_preview(const std::string& key, const std::string& value);
// The value in mh4u.ini (or `fallback`).
std::string game_option_get(const std::string& key, const std::string& fallback = "");
// Keys that only apply at the next start (the hooks read them once, at Install).
bool game_option_needs_restart(const std::string& key);

// android_main.cpp: the user folder (mh4u.ini's), and the runtime once it runs (the queue is drained there).
void game_options_init(const std::string& user_dir);
