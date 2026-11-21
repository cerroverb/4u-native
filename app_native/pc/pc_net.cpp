// MH4U for PC: the program's side of multiplayer (pc_net.h).
#include "pc_net.h"

#include <algorithm>
#include <deque>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "app_settings.h"
#include "pc_discord.h"
#include "settings_panel.h"
#include "settings_pc.h"
#include "ui.h"

void pc_net_start() {
    pc_discord_start();
}

void pc_net_frame() {
    pc_discord_frame();
}

std::vector<std::string> pc_chat_keys() {
    return {"/", "`", "\\", ";", "'", ""};
}

std::string pc_chat_key_label(const std::string& key, bool spanish) {
    if (key.empty()) return spanish ? "Ninguna" : "None";
    const SDL_Scancode sc = SDL_GetScancodeFromName(key.c_str());
    if (sc == SDL_SCANCODE_UNKNOWN) return key;
    // (what the key types on this keyboard's layout: the US "/" is "-" on a Spanish one)
    const char* name = SDL_GetKeyName(SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false));
    return name != nullptr && *name ? name : key;
}

bool pc_net_event(const SDL_Event& e) {
    // (1.0.2: over the chat window afloat too, but while Say something is typed in it - the key is a letter then)
    if (e.type != SDL_EVENT_KEY_DOWN || e.key.repeat || (ui_settings_open() && !(settings_panel_chat_floating() && !settings_panel_chat_typing())))
        return false;
    static unsigned read_version = ~0u;
    static SDL_Scancode key = SDL_SCANCODE_SLASH;
    if (read_version != app_settings_version()) {
        read_version = app_settings_version();
        const std::string name = app_setting("Controls", "ChatKey", "/");
        key = name.empty() ? SDL_SCANCODE_UNKNOWN : SDL_GetScancodeFromName(name.c_str());
    }
    if (key == SDL_SCANCODE_UNKNOWN || e.key.scancode != key || (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)))
        return false;
    settings_panel_type_chat();
    ui_open_settings();
    return true;
}

namespace {
// One step of MH4U_TEST_NET (and of the live file's CLICK and TYPE): where and what
struct Step {
    unsigned frame = 0;
    bool click = false;
    float x = 0, y = 0;
    std::string text;
};
bool parse_step(const std::string& w, Step* st) {
    const size_t a = w.find(':'), b = a == std::string::npos ? a : w.find(':', a + 1);
    if (w.size() < 2 || w[0] != 'f' || b == std::string::npos) return false;
    st->frame = static_cast<unsigned>(std::atol(w.c_str() + 1));
    const std::string kind = w.substr(a + 1, b - a - 1), rest = w.substr(b + 1);
    if (kind == "CLICK") {
        st->click = true;
        st->x = static_cast<float>(std::atof(rest.c_str()));
        const size_t c = rest.find(':');
        st->y = c == std::string::npos ? 0 : static_cast<float>(std::atof(rest.c_str() + c + 1));
        return true;
    }
    if (kind == "TYPE") {
        st->text = rest;
        for (char& c : st->text)
            if (c == '_') c = ' ';
        return true;
    }
    return false;
}
void do_step(const Step& st, SDL_Window* window) {
    SDL_Event e{};
    if (st.click) {
        int w = 0, h = 0;
        SDL_GetWindowSize(window, &w, &h);
        const float x = st.x * w, y = st.y * h;
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.windowID = SDL_GetWindowID(window);
        e.motion.x = x, e.motion.y = y;
        SDL_PushEvent(&e);
        for (const bool down : {true, false}) {
            SDL_Event b{};
            b.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            b.button.windowID = SDL_GetWindowID(window);
            b.button.button = SDL_BUTTON_LEFT;
            b.button.down = down;
            b.button.clicks = 1;
            b.button.x = x, b.button.y = y;
            SDL_PushEvent(&b);
        }
    } else {
        e.type = SDL_EVENT_TEXT_INPUT;
        e.text.windowID = SDL_GetWindowID(window);
        e.text.text = st.text.c_str();  // (a step's text lives as long as the program: the callers keep it)
        SDL_PushEvent(&e);
    }
}
void pc_net_test_step(const std::string& w, SDL_Window* window) {
    static std::deque<Step> kept;  // (TYPE's text: alive while SDL holds the event; a deque never moves it)
    Step st;
    if (!parse_step(w, &st)) return;
    kept.push_back(st);
    do_step(kept.back(), window);
}
}  // namespace

namespace {
// MH4U_TEST_LIVE: the lines added to the file since the last look (every few frames), and the keys still held
void live_steps(unsigned, SDL_Window* window) {
    // (by the clock, not the game's frames: those stop while the menu is open; a step's "frames" are 60ths of a second)
    const Uint64 frame = SDL_GetTicks() * 60 / 1000;
    static const char* path = std::getenv("MH4U_TEST_LIVE");
    static std::streamoff done = 0;
    static Uint64 next_look = 0;
    static std::vector<std::pair<Uint64, SDL_Scancode>> held;  // (release time, in 60ths of a second, key)
    auto key = [window](SDL_Scancode sc, bool down) {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.timestamp = SDL_GetTicksNS();
        e.key.windowID = SDL_GetWindowID(window);
        e.key.scancode = sc;
        e.key.key = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
        e.key.down = down;
        SDL_PushEvent(&e);
    };
    for (auto it = held.begin(); it != held.end();) {
        if (frame >= it->first) key(it->second, false), it = held.erase(it);
        else ++it;
    }
    // TAP: a finger on the window (a touch screen's, as a touch screen PC gives it), lifted after its frames
    static Uint64 finger_up = 0;
    static float finger_x = 0, finger_y = 0;
    auto finger = [window](bool down, float x, float y) {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_FINGER_DOWN : SDL_EVENT_FINGER_UP;
        e.tfinger.timestamp = SDL_GetTicksNS();
        e.tfinger.touchID = 7;
        e.tfinger.fingerID = 77;
        e.tfinger.x = x, e.tfinger.y = y;
        e.tfinger.pressure = down ? 1.0f : 0.0f;
        e.tfinger.windowID = SDL_GetWindowID(window);
        SDL_PushEvent(&e);
    };
    if (finger_up != 0 && frame >= finger_up) finger(false, finger_x, finger_y), finger_up = 0;
    if (path == nullptr || *path == 0 || frame < next_look) return;
    next_look = frame + 6;
    std::ifstream in(path, std::ios::binary);
    if (!in) return;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size <= done) return;
    in.seekg(done);
    std::string text(static_cast<size_t>(size - done), '\0');
    in.read(text.data(), size - done);
    const size_t last = text.rfind('\n');
    if (last == std::string::npos) return;  // (a line still being written)
    done += static_cast<std::streamoff>(last + 1);
    std::istringstream lines(text.substr(0, last + 1));
    std::string line;
    while (std::getline(lines, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        std::istringstream w(line);
        std::string what, a, b;
        w >> what >> a >> b;
        std::fprintf(stderr, "[app] test (live): %s\n", line.c_str());
        if (what == "KEY") {
            for (char& c : a)
                if (c == '_') c = ' ';
            const SDL_Scancode sc = SDL_GetScancodeFromName(a.c_str());
            if (sc == SDL_SCANCODE_UNKNOWN) continue;
            key(sc, true);
            held.emplace_back(frame + static_cast<Uint64>(std::max(1, std::atoi(b.c_str()))), sc);
        } else if (what == "TAP") {  // TAP <x> <y> <frames>: fractions of the window
            std::string c;
            w >> c;
            if (finger_up != 0) finger(false, finger_x, finger_y);
            finger_x = static_cast<float>(std::atof(a.c_str())), finger_y = static_cast<float>(std::atof(b.c_str()));
            finger(true, finger_x, finger_y);
            finger_up = frame + static_cast<Uint64>(std::max(2, std::atoi(c.c_str())));
        } else if (what == "MENU") {
            ui_open_settings();
            pc_settings_test_page(a);
        } else if (what == "CLICK" || what == "TYPE") {
            pc_net_test_step(what == "CLICK" ? "f0:CLICK:" + a + ":" + b : "f0:TYPE:" + line.substr(5), window);
        }
    }
}
}  // namespace

void pc_net_test(unsigned frame, SDL_Window* window) {
    live_steps(frame, window);
    static std::vector<Step> steps;
    static size_t next = 0;
    static bool read = false;
    if (!read) {
        read = true;
        const char* env = std::getenv("MH4U_TEST_NET");
        std::string all = env != nullptr ? env : "";
        for (char& c : all)
            if (c == ',') c = ' ';
        std::istringstream words(all);
        std::string w;
        Step st;
        while (words >> w)
            if (st = Step{}, parse_step(w, &st)) steps.push_back(st);
        std::sort(steps.begin(), steps.end(), [](const Step& p, const Step& q) { return p.frame < q.frame; });
    }
    while (next < steps.size() && frame >= steps[next].frame) do_step(steps[next++], window);
}
