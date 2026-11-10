// The app's own options, mh4u-app.ini (app_settings.h).
#include "app_settings.h"
#include "first_run.h"

#include <SDL3/SDL.h>
#include <strings.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <vector>

namespace {

// The file as it was read, a line each: a value's line knows its section and key (the others are kept as they are)
struct Line {
    std::string text, section, key, value;
    bool header = false;  // a [Section] line
};

std::mutex g_mutex;
std::string g_path;
std::vector<Line> g_lines;
bool g_loaded = false;
std::atomic<unsigned> g_version{0};

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

bool same(const std::string& a, const char* b) { return !strcasecmp(a.c_str(), b); }

void read_locked() {
    g_lines.clear();
    g_loaded = true;
    if (g_path.empty()) {
        const char* ext = SDL_GetAndroidExternalStoragePath();  // (none: the internal folder, as android_main.cpp's)
        if (ext == nullptr) ext = SDL_GetAndroidInternalStoragePath();
        g_path = std::string(ext ? ext : ".") + "/mh4u-app.ini";
    }
    std::ifstream in(g_path);
    std::string raw, section;
    while (std::getline(in, raw)) {
        Line l;
        l.text = raw;
        const std::string t = trim(raw);
        if (!t.empty() && t[0] == '[' && t.back() == ']') {
            section = trim(t.substr(1, t.size() - 2));
            l.header = true;
            l.section = section;
        } else if (!t.empty() && t[0] != '#' && t[0] != ';') {
            const size_t eq = t.find('=');
            if (eq != std::string::npos) {
                l.section = section;
                l.key = trim(t.substr(0, eq));
                l.value = trim(t.substr(eq + 1));
            }
        }
        g_lines.push_back(std::move(l));
    }
}

void load_locked() {
    if (!g_loaded) read_locked();
}

const Line* find_locked(const char* section, const char* key) {
    const Line* found = nullptr;  // (the last one, as an INI reader takes it)
    for (const Line& l : g_lines)
        if (!l.key.empty() && same(l.section, section) && same(l.key, key)) found = &l;
    return found;
}

}  // namespace

void app_settings_init(const std::string& data_dir) {
    std::lock_guard lock(g_mutex);
    g_path = data_dir + "/mh4u-app.ini";
    read_locked();
}

std::string app_settings_path() {
    std::lock_guard lock(g_mutex);
    load_locked();
    return g_path;
}

void app_settings_reload() {
    std::lock_guard lock(g_mutex);
    read_locked();
}

std::string app_setting(const char* section, const char* key, const std::string& fallback) {
    std::lock_guard lock(g_mutex);
    load_locked();
    const Line* l = find_locked(section, key);
    return l && !l->value.empty() ? l->value : fallback;
}

int app_setting_int(const char* section, const char* key, int fallback) {
    const std::string v = app_setting(section, key);
    char* end = nullptr;
    const long n = v.empty() ? 0 : std::strtol(v.c_str(), &end, 10);
    return v.empty() || end == v.c_str() ? fallback : static_cast<int>(n);
}

bool app_setting_bool(const char* section, const char* key, bool fallback) {
    const std::string v = app_setting(section, key);
    for (const char* on : {"on", "1", "true", "yes", "shown"})
        if (same(v, on)) return true;
    for (const char* off : {"off", "0", "false", "no", "hidden"})
        if (same(v, off)) return false;
    return fallback;
}

void app_setting_set(const char* section, const char* key, const std::string& value) {
    std::lock_guard lock(g_mutex);
    load_locked();
    for (auto it = g_lines.rbegin(); it != g_lines.rend(); ++it)
        if (!it->key.empty() && same(it->section, section) && same(it->key, key)) {
            it->value = value;
            it->text = it->key + " = " + value;
            return;
        }
    Line l;
    l.section = section, l.key = key, l.value = value, l.text = std::string(key) + " = " + value;
    // at the end of its section (before the blank lines that close it), else a new section at the file's end
    int last = -1;
    for (int i = 0; i < static_cast<int>(g_lines.size()); ++i)
        if (same(g_lines[i].section, section) && (g_lines[i].header || !g_lines[i].key.empty())) last = i;
    if (last >= 0) {
        g_lines.insert(g_lines.begin() + last + 1, std::move(l));
        return;
    }
    if (g_lines.empty()) {
        Line note;
        note.text = "# MH4U for Android: the app's own options (its settings menu writes them; the game's are in mh4u.ini)";
        g_lines.push_back(note);
    }
    if (!trim(g_lines.back().text).empty()) g_lines.push_back(Line{});
    Line head;
    head.text = std::string("[") + section + "]", head.section = section, head.header = true;
    g_lines.push_back(std::move(head));
    g_lines.push_back(std::move(l));
}

void app_setting_remove(const char* section, const char* key) {
    std::lock_guard lock(g_mutex);
    load_locked();
    for (auto it = g_lines.begin(); it != g_lines.end();)
        if (!it->key.empty() && same(it->section, section) && same(it->key, key))
            it = g_lines.erase(it);
        else
            ++it;
}

void app_setting_set_int(const char* section, const char* key, int value) {
    app_setting_set(section, key, std::to_string(value));
}

bool app_settings_save(std::string* error) {
    std::lock_guard lock(g_mutex);
    load_locked();
    const std::string tmp = g_path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        for (const Line& l : g_lines) out << l.text << "\n";
        out.flush();
        if (!out) {
            if (error) *error = "Could not write " + g_path;
            std::remove(tmp.c_str());
            return false;
        }
    }
    if (std::rename(tmp.c_str(), g_path.c_str()) != 0) {
        if (error) *error = "Could not write " + g_path;
        std::remove(tmp.c_str());
        return false;
    }
    g_version.fetch_add(1);
    std::fprintf(stderr, "[Info] App: settings saved (%s)\n", g_path.c_str());
    return true;
}

unsigned app_settings_version() {
    return g_version.load();
}

namespace {
std::atomic<int> g_window_w{0}, g_window_h{0};
}

void app_set_window_size(int w, int h) {
    g_window_w = std::max(w, h);
    g_window_h = std::min(w, h);
}

int render_scale_times(int percent) {
    if (percent <= 0) return 1;
    const int w = g_window_w.load(), h = g_window_h.load();
    if (w <= 0 || h <= 0) return 1;
    // the top screen's lines on the phone: the window's height, or less on a screen narrower than 5:3
    const double lines = std::min<double>(h, w * 3.0 / 5.0);
    const double times = lines / 240.0;
    const double want = percent >= 100 ? std::ceil(times * percent / 100.0 - 0.05) : std::round(times * percent / 100.0);
    return std::clamp(static_cast<int>(want), 1, 10);  // (10: the renderer's kMaxScale)
}

int render_scale_used(int percent, bool adreno_fix) { return adreno_fix_scale(render_scale_times(percent), adreno_fix); }

bool app_adreno_fix() { return !same(app_setting("Graphics", "AdrenoFix", "auto"), "off"); }

int app_low_end_level(const std::string& text) {
    if (same(text, "toaster") || text == "2") return LOW_END_TOASTER;
    for (const char* on : {"on", "1", "true", "yes"})
        if (same(text, on)) return LOW_END_ON;
    return LOW_END_OFF;
}

int low_end_fps() {
    return first_run_fast_cores() ? kLowEndFpsFast : kLowEndFpsSlow;
}
