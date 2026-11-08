// The port's options while the game runs (game_options.h).
#include "game_options.h"

#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <utility>

#include "hooks.h"
#include "mh4n/registry.h"
#include "mh4n/runtime.h"

namespace {

std::mutex g_mutex;
std::string g_ini;                                      // <user>/mh4u.ini
std::vector<std::pair<std::string, std::string>> g_queue;  // for the game's thread

// The options SetOption takes while the game runs (hooks.h): everything else applies at the next start.
const char* const kLive[] = {"fps", "refresh_rate", "steady", "dither", "antialiasing", "fov", "render_flags_set", "render_flags_clear", "kick",
                             "target", "skip", "skip_panel", "skip_hint", "touch_overlay", "quest_overlay",
                             "target_overlay", "hud_edges", "bloom", "motion_blur", "depth_of_field", "radial_blur",
                             "haze", "skip_end", "skip_end_hold"};

std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

// mh4u.ini with `key` set to `value`: the line replaced where it is (comments and order kept), else added.
void WriteIni(const std::string& key, const std::string& value) {
    std::vector<std::string> lines;
    {
        std::ifstream in(g_ini);
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
    }
    bool found = false;
    for (std::string& line : lines) {
        const std::string t = Trim(line);
        if (t.empty() || t[0] == '#') continue;
        const auto eq = t.find('=');
        if (eq != std::string::npos && Trim(t.substr(0, eq)) == key) {
            line = key + " = " + value;
            found = true;
        }
    }
    if (!found) lines.push_back(key + " = " + value);
    std::ofstream out(g_ini, std::ios::trunc);
    for (const std::string& line : lines) out << line << "\n";
}

// The queue, drained on the game's thread at each new frame.
void Drain(mh4n::Runtime& rt) {
    rt.OnFrame([](mh4n::u32, mh4n::u64) {
        std::vector<std::pair<std::string, std::string>> todo;
        {
            std::lock_guard lock(g_mutex);
            if (g_queue.empty()) return;
            todo.swap(g_queue);
        }
        for (const auto& [key, value] : todo) Port::Hooks::SetOption(key, value);
    });
}
MH4N_REGISTER(mh4n::BootStep, "zz.android.options", mh4n::BootStep{mh4n::Phase::Started, Drain});

}  // namespace

void game_options_init(const std::string& user_dir) {
    std::lock_guard lock(g_mutex);
    g_ini = user_dir + "/mh4u.ini";
}

bool game_option_needs_restart(const std::string& key) {
    for (const char* k : kLive) {
        if (key == k) return false;
    }
    // (the HUD's parts, hud_<part>, and item_bar as an earlier version wrote the item bar's place)
    return key != "item_bar" && key.compare(0, 4, "hud_") != 0;
}

bool game_option_preview(const std::string& key, const std::string& value) {
    std::lock_guard lock(g_mutex);
    if (game_option_needs_restart(key)) return false;
    g_queue.emplace_back(key, value);
    return true;
}

bool game_option_set(const std::string& key, const std::string& value) {
    std::lock_guard lock(g_mutex);
    if (!g_ini.empty()) WriteIni(key, value);
    if (game_option_needs_restart(key)) return false;
    g_queue.emplace_back(key, value);
    return true;
}

std::string game_option_get(const std::string& key, const std::string& fallback) {
    std::lock_guard lock(g_mutex);
    std::ifstream in(g_ini);
    std::string line, value = fallback;
    while (std::getline(in, line)) {
        const std::string t = Trim(line);
        if (t.empty() || t[0] == '#') continue;
        const auto eq = t.find('=');
        if (eq != std::string::npos && Trim(t.substr(0, eq)) == key) value = Trim(t.substr(eq + 1));
    }
    return value;
}
