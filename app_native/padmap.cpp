// Any controller, button by button (the MHGU port's runtime/padmap.c). SDL only reads a controller as a gamepad if it
// knows its layout, and for some (old DirectInput ones, cheap Bluetooth ones, arcade sticks) it does not, or guesses
// wrong. Here the player presses, one by one, what each of the 3DS's buttons should be; that makes an ordinary SDL
// mapping, which SDL then applies to every controller of that kind, and from there on it is a controller like any
// other (controls.cpp). Kept in gamepad_mappings.txt beside mh4u-app.ini: one SDL mapping a line, the first field the
// controller's GUID; a line "#before,<mapping>" keeps the map SDL had for it before (its own, or the one Android made),
// so that forgetting the player's puts that one back at once.
//
// SDL's own names are by position: "a" is the bottom face button, "b" the right one. The 3DS's A is the right one, so A
// here goes to "b", and a mapped controller is always read by position (controls.cpp: padmap_custom).
//
// It starts with the player moving every stick and trigger once and letting go: a trigger rests at one end of its axis
// but a controller reports that only from its first movement, and what rests where has to be known to tell a press.
#include "padmap.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr Uint64 WATCH_NS = 1500000000ull;  // the panel has not looked for this long: its pages were left
constexpr Uint64 LIST_NS = 500000000ull;
constexpr int PRESS = 14000;  // of 32768: an axis this far from rest
constexpr int RELEASED = 6000;
constexpr int MAX_AXES = 32, MAX_HATS = 8, MAX_BUTTONS = 64;
constexpr int TRIGGER_REST = 20000;  // an axis resting beyond this runs from one end to the other, as a trigger's
constexpr int INPUT_TEXT = 16;
constexpr int GUID_TEXT = 33;  // (SDL_GUIDToString: 32 digits)

const struct {
    const char *field, *title;
} STEPS[] = {
    {"b", "A (right button: confirm, special attack)"},
    {"a", "B (bottom button: cancel, dodge)"},
    {"y", "X (top button: attack)"},
    {"x", "Y (left button: use item, sheathe)"},
    {"leftshoulder", "L (camera)"},
    {"rightshoulder", "R (guard, run)"},
    {"lefttrigger", "ZL"},
    {"righttrigger", "ZR"},
    {"start", "START (the game's menu)"},
    {"back", "SELECT"},
    {"dpup", "D-pad up"},
    {"dpdown", "D-pad down"},
    {"dpleft", "D-pad left"},
    {"dpright", "D-pad right"},
    {"-lefty", "Left stick up"},
    {"+lefty", "Left stick down"},
    {"-leftx", "Left stick left"},
    {"+leftx", "Left stick right"},
    {"leftstick", "Left stick click"},
    {"-righty", "Right stick up"},
    {"+righty", "Right stick down"},
    {"-rightx", "Right stick left"},
    {"+rightx", "Right stick right"},
    {"rightstick", "Right stick click"},
};
constexpr int STEP_COUNT = (int)(sizeof STEPS / sizeof *STEPS);

struct {
    Uint64 watched = 0;  // padmap_watch's last call
    Uint64 next_list = 0;
    int count = 0;  // the list
    SDL_JoystickID ids[PADMAP_MAX] = {};
    char names[PADMAP_MAX][64] = {};
    bool custom[PADMAP_MAX] = {};
    bool active = false;  // a controller is being mapped
    SDL_Joystick* joy = nullptr;
    SDL_JoystickID joy_id = 0;
    int step = PADMAP_REST;
    bool held = false;  // the last press has yet to be let go
    int naxes = 0, nhats = 0, nbuttons = 0;
    Sint16 axis_rest[MAX_AXES] = {};
    Uint8 hat_rest[MAX_HATS] = {};
    char input[STEP_COUNT][INPUT_TEXT] = {};
} P;

std::string g_file;
// the player's maps and the maps SDL had before them, by GUID, as the file's lines
std::map<std::string, std::string> g_maps, g_before;

std::string guid_of(SDL_GUID guid) {
    char text[GUID_TEXT];
    SDL_GUIDToString(guid, text, sizeof text);
    return text;
}

void save_file() {
    if (g_file.empty()) return;
    const std::string tmp = g_file + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "w");
    if (!f) {
        std::fprintf(stderr, "[padmap] can't write %s\n", g_file.c_str());
        return;
    }
    std::fprintf(f, "# MH4U: the controllers mapped in the settings (Controls, Map a controller), SDL mappings\n");
    for (const auto& [guid, line] : g_maps) {
        std::fprintf(f, "%s\n", line.c_str());
        if (auto it = g_before.find(guid); it != g_before.end()) std::fprintf(f, "#before,%s\n", it->second.c_str());
    }
    std::fclose(f);
    std::rename(tmp.c_str(), g_file.c_str());
}

// ---- reading it
void snapshot() {
    for (int i = 0; i < P.naxes; i++) P.axis_rest[i] = SDL_GetJoystickAxis(P.joy, i);
    for (int i = 0; i < P.nhats; i++) P.hat_rest[i] = SDL_GetJoystickHat(P.joy, i);
}

bool all_released() {
    for (int i = 0; i < P.nbuttons; i++)
        if (SDL_GetJoystickButton(P.joy, i)) return false;
    for (int i = 0; i < P.nhats; i++)
        if (SDL_GetJoystickHat(P.joy, i) != P.hat_rest[i]) return false;
    for (int i = 0; i < P.naxes; i++)
        if (std::abs(SDL_GetJoystickAxis(P.joy, i) - P.axis_rest[i]) > RELEASED) return false;
    return true;
}

bool is_trigger(int step) {
    return !std::strcmp(STEPS[step].field, "lefttrigger") || !std::strcmp(STEPS[step].field, "righttrigger");
}

// what is pressed now, in a mapping's terms: b3, h0.1, +a2... (the strongest axis, if nothing else); false: nothing
bool read_input(int step, char* out) {
    for (int i = 0; i < P.nbuttons; i++)
        if (SDL_GetJoystickButton(P.joy, i)) {
            std::snprintf(out, INPUT_TEXT, "b%d", i);
            return true;
        }
    for (int i = 0; i < P.nhats; i++) {
        Uint8 v = SDL_GetJoystickHat(P.joy, i) & ~P.hat_rest[i];
        static const Uint8 dirs[4] = {SDL_HAT_UP, SDL_HAT_RIGHT, SDL_HAT_DOWN, SDL_HAT_LEFT};
        for (int d = 0; d < 4; d++)
            if (v & dirs[d]) {
                std::snprintf(out, INPUT_TEXT, "h%d.%d", i, dirs[d]);
                return true;
            }
    }
    int best = -1, far = PRESS;
    for (int i = 0; i < P.naxes; i++) {
        int d = std::abs(SDL_GetJoystickAxis(P.joy, i) - P.axis_rest[i]);
        if (d > far) far = d, best = i;
    }
    if (best < 0) return false;
    int rest = P.axis_rest[best], now = SDL_GetJoystickAxis(P.joy, best);
    if (is_trigger(step) && rest < -TRIGGER_REST) std::snprintf(out, INPUT_TEXT, "a%d", best);
    else if (is_trigger(step) && rest > TRIGGER_REST) std::snprintf(out, INPUT_TEXT, "a%d~", best);
    else std::snprintf(out, INPUT_TEXT, "%ca%d", now > rest ? '+' : '-', best);
    return true;
}

bool used(const char* input, int upto) {  // (a press already taken: a noisy axis, a double press)
    for (int i = 0; i < upto; i++)
        if (!std::strcmp(P.input[i], input)) return true;
    return false;
}

void stop() {
    if (P.joy) SDL_CloseJoystick(P.joy);
    P.joy = nullptr, P.joy_id = 0, P.active = false, P.held = false;
}

void apply() {
    const std::string guid = guid_of(SDL_GetJoystickGUID(P.joy));
    char name[64], line[1024];
    std::snprintf(name, sizeof name, "%s", SDL_GetJoystickName(P.joy) ? SDL_GetJoystickName(P.joy) : "Controller");
    for (char* c = name; *c; c++)
        if (*c == ',' || *c == '\r' || *c == '\n') *c = ' ';
    int n = std::snprintf(line, sizeof line, "%s,%s,", guid.c_str(), name);
    for (int i = 0; i < STEP_COUNT; i++)
        if (P.input[i][0]) n += std::snprintf(line + n, sizeof line - (size_t)n, "%s:%s,", STEPS[i].field, P.input[i]);
    std::snprintf(line + n, sizeof line - (size_t)n, "platform:%s,", SDL_GetPlatform());
    // the map SDL had for it (its own, or the one Android made), unless the player's own is it already
    if (!g_maps.count(guid) && !g_before.count(guid)) {
        if (char* before = SDL_GetGamepadMappingForGUID(SDL_GetJoystickGUID(P.joy))) {
            g_before[guid] = before;
            SDL_free(before);
        }
    }
    int r = SDL_AddGamepadMapping(line);
    std::fprintf(stderr, "[padmap] %s: %s (%s)\n", name, line, r < 0 ? SDL_GetError() : "in use");
    if (r < 0) return;
    g_maps[guid] = line;
    save_file();
}

void refresh_list() {
    int n = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&n);
    P.count = 0;
    for (int i = 0; i < n && P.count < PADMAP_MAX; i++) {
        const char* name = SDL_GetJoystickNameForID(ids[i]);
        // (not controllers, though Android lists them: controls.cpp says the same of gamepads)
        static const char* const not_pads[] = {"qwerty", "virtual", "uinput", "gpio", "keypad", "consumer control"};
        bool skip = false;
        for (const char* k : not_pads) skip |= name && SDL_strcasestr(name, k) != nullptr;
        if (skip) continue;
        P.ids[P.count] = ids[i];
        std::snprintf(P.names[P.count], 64, "%s", name ? name : "?");
        P.custom[P.count++] = padmap_custom(ids[i]);
    }
    SDL_free(ids);
}

}  // namespace

void padmap_init(const std::string& data_dir) {
    g_file = data_dir + "/gamepad_mappings.txt";
    std::ifstream in(g_file);
    std::string l;
    int n = 0;
    while (std::getline(in, l)) {
        while (!l.empty() && (l.back() == '\r' || l.back() == '\n' || l.back() == ' ')) l.pop_back();
        const bool before = !l.compare(0, 8, "#before,");
        if (before) l.erase(0, 8);
        else if (l.empty() || l[0] == '#') continue;
        const auto comma = l.find(',');
        if (comma == std::string::npos) continue;
        const std::string guid = l.substr(0, comma);
        if (before) {
            g_before[guid] = l;
            continue;
        }
        if (SDL_AddGamepadMapping(l.c_str()) < 0) {
            std::fprintf(stderr, "[padmap] a map SDL refused: %s (%s)\n", l.c_str(), SDL_GetError());
            continue;
        }
        g_maps[guid] = l;
        n++;
    }
    std::fprintf(stderr, "[padmap] %d mapped controller(s)\n", n);
}

bool padmap_custom(SDL_JoystickID id) {
    return g_maps.count(guid_of(SDL_GetJoystickGUIDForID(id))) != 0;
}

void padmap_poll() {
    const Uint64 now = SDL_GetTicksNS();
    const bool watched = P.watched && now - P.watched < WATCH_NS;
    if (watched && now >= P.next_list) refresh_list(), P.next_list = now + LIST_NS;
    if (P.joy && (!watched || !SDL_JoystickConnected(P.joy))) {
        std::fprintf(stderr, "[padmap] stopped: %s\n", !watched ? "the menu left" : "the controller is gone");
        stop();
    }
    if (P.joy && P.step >= 0 && P.step < STEP_COUNT) {
        if (P.held) {
            if (all_released()) P.held = false;
        } else {
            char in[INPUT_TEXT];
            if (read_input(P.step, in)) {
                if (!used(in, P.step)) {
                    std::fprintf(stderr, "[padmap] %s: %s\n", STEPS[P.step].field, in);
                    std::memcpy(P.input[P.step++], in, INPUT_TEXT);
                }
                P.held = true;
            }
        }
    }
}

bool padmap_active() {
    return P.active;
}

SDL_JoystickID padmap_joystick() {
    return P.active ? P.joy_id : 0;
}

void padmap_watch() {
    const Uint64 now = SDL_GetTicksNS();
    const bool fresh = P.watched && now - P.watched < WATCH_NS;
    P.watched = now;
    if (!fresh) refresh_list(), P.next_list = now + LIST_NS;  // (the pages just opened: the list at once)
}

int padmap_list(char names[][64], bool* custom, SDL_JoystickID* ids, int max) {
    int n = P.count < max ? P.count : max;
    for (int i = 0; i < n; i++) {
        std::memcpy(names[i], P.names[i], 64);
        custom[i] = P.custom[i];
        ids[i] = P.ids[i];
    }
    return n;
}

void padmap_begin(int index) {
    P.watched = SDL_GetTicksNS();
    if (P.joy) stop();
    SDL_Joystick* j = index >= 0 && index < P.count ? SDL_OpenJoystick(P.ids[index]) : nullptr;
    if (!j) return;
    P.joy = j, P.joy_id = P.ids[index], P.active = true, P.step = PADMAP_REST, P.held = false;
    P.naxes = SDL_clamp(SDL_GetNumJoystickAxes(j), 0, MAX_AXES);
    P.nhats = SDL_clamp(SDL_GetNumJoystickHats(j), 0, MAX_HATS);
    P.nbuttons = SDL_clamp(SDL_GetNumJoystickButtons(j), 0, MAX_BUTTONS);
    std::memset(P.input, 0, sizeof P.input);
    std::fprintf(stderr, "[padmap] mapping %s: %d axes, %d hats, %d buttons\n", P.names[index], P.naxes, P.nhats,
                 P.nbuttons);
}

int padmap_step() {
    return P.step;
}

int padmap_steps() {
    return STEP_COUNT;
}

const char* padmap_step_title(int step) {
    return step >= 0 && step < STEP_COUNT ? STEPS[step].title : "";
}

void padmap_continue() {
    if (P.joy && P.step == PADMAP_REST) snapshot(), P.step = 0, P.held = !all_released();
}

void padmap_skip() {
    if (P.joy && P.step >= 0 && P.step < STEP_COUNT) P.input[P.step++][0] = 0, P.held = false;
}

void padmap_back() {
    if (P.joy && P.step > 0) P.input[--P.step][0] = 0, P.held = true;
}

void padmap_save() {
    if (!P.joy) return;
    apply();
    stop();
    P.next_list = 0;
}

void padmap_cancel() {
    if (P.joy) std::fprintf(stderr, "[padmap] cancelled\n");
    stop();
}

bool padmap_forget(int index) {
    if (index < 0 || index >= P.count) return false;
    const std::string guid = guid_of(SDL_GetJoystickGUIDForID(P.ids[index]));
    bool now = false;
    g_maps.erase(guid);
    if (auto it = g_before.find(guid); it != g_before.end()) {
        now = SDL_AddGamepadMapping(it->second.c_str()) >= 0;
        g_before.erase(it);
    }
    save_file();
    std::fprintf(stderr, "[padmap] %s forgotten (%s)\n", P.names[index], now ? "SDL's map is back" : "at the next start");
    P.next_list = 0;
    refresh_list();
    return now;
}
