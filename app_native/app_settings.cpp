// The app's own options, mh4u-app.ini (app_settings.h).
#include "app_settings.h"
#include "app_version.h"
#include "diag.h"
#include "colour_filter.h"
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
#ifdef __ANDROID__
        const char* ext = SDL_GetAndroidExternalStoragePath();  // (none: the internal folder, as android_main.cpp's)
        if (ext == nullptr) ext = SDL_GetAndroidInternalStoragePath();
#else
        const char* ext = nullptr;  // (the PC's main gives the program's home first: pc_main.cpp)
#endif
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

void app_colour(ColourAdjust* out) {
    const auto get = [](const char* key, int limit) { return std::clamp(app_setting_int("Colour", key, 0), -limit, limit); };
    out->hue = get("Hue", COLOUR_HUE_MAX);
    out->temperature = get("Temperature", COLOUR_MAX);
    out->tint = get("Tint", COLOUR_MAX);
    out->brightness = get("Brightness", COLOUR_MAX);
    out->contrast = get("Contrast", COLOUR_MAX);
    out->saturation = get("Saturation", COLOUR_MAX);
    out->gamma = get("Gamma", COLOUR_MAX);
}

namespace {
const char* preset_key(int n) {
    static const char* const keys[kColourPresets] = {"Preset1", "Preset2", "Preset3"};
    return keys[std::clamp(n, 1, kColourPresets) - 1];
}
}  // namespace

bool app_colour_preset(int n, ColourAdjust* out) {
    if (n < 1 || n > kColourPresets) return false;
    const std::string v = app_setting("Colour", preset_key(n));
    if (v.empty()) return false;
    int x[7], count = 0;
    const char* p = v.c_str();
    bool ok = true;
    for (;;) {
        char* end = nullptr;
        const long a = std::strtol(p, &end, 10);
        if (end == p || count == 7) {
            ok = false;
            break;
        }
        x[count++] = static_cast<int>(std::clamp<long>(a, -1000, 1000));
        while (*end == ' ' || *end == '\t') ++end;
        if (!*end) break;
        if (*end != ',') {
            ok = false;
            break;
        }
        p = end + 1;
    }
    if (!ok || count != 7) {
        static std::string said[kColourPresets];  // (once for each text: the menu reads the slots every frame)
        if (said[n - 1] != v) {
            said[n - 1] = v;
            std::fprintf(stderr, "[Info] App: mh4u-app.ini [Colour] %s = \"%s\" is not seven numbers: the slot shows empty\n",
                         preset_key(n), v.c_str());
        }
        return false;
    }
    out->hue = std::clamp(x[0], -COLOUR_HUE_MAX, COLOUR_HUE_MAX);
    out->temperature = std::clamp(x[1], -COLOUR_MAX, COLOUR_MAX);
    out->tint = std::clamp(x[2], -COLOUR_MAX, COLOUR_MAX);
    out->brightness = std::clamp(x[3], -COLOUR_MAX, COLOUR_MAX);
    out->contrast = std::clamp(x[4], -COLOUR_MAX, COLOUR_MAX);
    out->saturation = std::clamp(x[5], -COLOUR_MAX, COLOUR_MAX);
    out->gamma = std::clamp(x[6], -COLOUR_MAX, COLOUR_MAX);
    return true;
}

bool app_colour_preset_store(int n, const ColourAdjust* a) {
    if (n < 1 || n > kColourPresets) return false;
    if (!a) {
        app_setting_remove("Colour", preset_key(n));
    } else {
        char text[96];
        std::snprintf(text, sizeof text, "%d,%d,%d,%d,%d,%d,%d", a->hue, a->temperature, a->tint, a->brightness, a->contrast,
                      a->saturation, a->gamma);
        app_setting_set("Colour", preset_key(n), text);
    }
    std::string error;
    if (app_settings_save(&error)) return true;
    std::fprintf(stderr, "[Info] App: mh4u-app.ini: %s\n", error.c_str());
    return false;
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
        note.text = "# " MH4U_APP_NAME ": the app's own options (its settings menu writes them; the game's are in mh4u.ini)";
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
#ifdef _WIN32  // (Windows' rename never replaces a file: pc/compat/pc_compat.h)
    if (!pc_replace_file(tmp.c_str(), g_path.c_str())) {
#else
    if (std::rename(tmp.c_str(), g_path.c_str()) != 0) {
#endif
        if (error) *error = "Could not write " + g_path;
        std::remove(tmp.c_str());
        return false;
    }
    g_version.fetch_add(1);
    std::fprintf(stderr, "[Info] App: settings saved (%s)\n", g_path.c_str());
    {  // (1.0.1, diag) what changed since the last save (or the start), with the session's time
        static std::map<std::string, std::string> last;
        static bool have_last = false;
        auto now = diag_read_ini(g_path);
        if (have_last) diag_log_changes("mh4u-app.ini", last, now);
        last = std::move(now);
        have_last = true;
    }
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

int render_scale_used(int percent, bool adreno_fix) {
    const int used = adreno_fix_scale(render_scale_times(percent), adreno_fix), limit = gpu_scale_limit();
    return limit > 0 ? std::min(used, limit) : used;  // (what this GPU can draw at: first_run.h gpu_scale_limit)
}

bool app_adreno_fix() { return !same(app_setting("Graphics", "AdrenoFix", "auto"), "off"); }

int app_low_end_level(const std::string& text) {
    if (same(text, "brick") || text == "3") return LOW_END_BRICK;
    if (same(text, "toaster") || text == "2") return LOW_END_TOASTER;
    for (const char* on : {"on", "1", "true", "yes"})
        if (same(text, on)) return LOW_END_ON;
    return LOW_END_OFF;
}

int low_end_fps() {
    return first_run_fast_cores() ? kLowEndFpsFast : kLowEndFpsSlow;
}
