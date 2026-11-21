// MH4U for PC: Discord Rich Presence (pc_discord.h), as MHGU for PC's runtime/net/discord.c.
//
// Discord's local RPC: the Discord app listens on a named pipe, \\.\pipe\discord-ipc-0 .. 9. Frames: u32 opcode, u32
// length (little endian), JSON. We say HANDSHAKE {v: 1, client_id}, Discord answers READY; then FRAME {cmd:
// SET_ACTIVITY, args: {pid, activity}} sets what the profile shows, and closing the connection clears it. Discord takes
// about 5 updates per 20 s, so a change waits until 4 s after the last one (the latest state goes then, the ones in
// between are dropped). No Discord running, or the switch off: nothing happens; a connection is tried every 15 s.
//
// The application is the Discord application whose name the profile shows as the game being played and whose art
// assets are the pictures: "logo" the large one, the weapons' icons the small one ("great_sword" ... "charge_blade", as
// MHGU's). kDiscordApp is MHGU for PC's own application until MH4U has one of its own (MH4U_DISCORD_APP for tests).
#include "pc_discord.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app_settings.h"
#include "hooks.h"
#include "mh4n/hubs.h"
#include "mh4n/rooms.h"
#include "platform/monster_names.h"
#include "presence.h"
#include "settings_panel.h"

#include <windows.h>  // (last: its macros)

namespace {

constexpr const char* kDiscordApp = "1566091085493571724";  // (MHGU for PC's Discord application, for now)
constexpr auto kRetry = std::chrono::seconds(15);
constexpr auto kMinGap = std::chrono::seconds(4);
constexpr std::uint32_t kFrameMax = 0x10000;
enum { OP_HANDSHAKE, OP_FRAME, OP_CLOSE, OP_PING, OP_PONG };

std::mutex g_mutex;
std::condition_variable g_wake;
std::string g_want = "null";     // the activity's JSON ("null": none)
bool g_enabled = true;           // [Discord] RichPresence
std::atomic<int> g_state{0};     // 0 off, 1 looking for Discord, 2 connected
bool g_started = false;
bool g_log_all = false;

void Logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void Logf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::fputs("[discord] ", stderr);
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
    va_end(ap);
}

// ---- the connection (the Discord thread's only)
HANDLE g_pipe = INVALID_HANDLE_VALUE;
bool g_connected = false;

bool conn_open() {
    for (int i = 0; i < 10; i++) {
        char name[32];
        std::snprintf(name, sizeof name, "\\\\.\\pipe\\discord-ipc-%d", i);
        g_pipe = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (g_pipe != INVALID_HANDLE_VALUE) return true;
    }
    return false;
}
void conn_close() {
    if (g_pipe != INVALID_HANDLE_VALUE) CloseHandle(g_pipe);
    g_pipe = INVALID_HANDLE_VALUE;
}
bool conn_write(const void* p, std::uint32_t n) {
    DWORD w = 0;
    return WriteFile(g_pipe, p, n, &w, nullptr) && w == n;
}
int conn_available() {  // bytes waiting, -1: closed
    DWORD n = 0;
    return PeekNamedPipe(g_pipe, nullptr, 0, nullptr, &n, nullptr) ? static_cast<int>(n) : -1;
}
bool conn_read(void* p, std::uint32_t n) {
    for (auto* b = static_cast<std::uint8_t*>(p); n;) {
        DWORD r = 0;
        if (!ReadFile(g_pipe, b, n, &r, nullptr) || r == 0) return false;
        b += r, n -= r;
    }
    return true;
}

bool frame_send(std::uint32_t op, const std::string& json) {
    const std::uint32_t h[2] = {op, static_cast<std::uint32_t>(json.size())};
    return conn_write(h, sizeof h) && conn_write(json.data(), h[1]);
}
// A frame from Discord (its JSON); false: the connection is gone
bool frame_recv(std::uint32_t* op, std::string* body) {
    std::uint32_t h[2];
    if (!conn_read(h, sizeof h) || h[1] > kFrameMax) return false;
    body->resize(h[1]);
    if (h[1] && !conn_read(body->data(), h[1])) return false;
    *op = h[0];
    return true;
}

void disconnect(const char* why) {
    if (g_connected) Logf("disconnected: %s", why);
    conn_close();
    g_connected = false;
}

std::string app_id() {
    const char* env = std::getenv("MH4U_DISCORD_APP");
    return env != nullptr && *env ? env : kDiscordApp;
}

bool connect_discord() {
    if (!conn_open()) return false;
    std::string body;
    std::uint32_t op = 0;
    bool answered = false;
    if (frame_send(OP_HANDSHAKE, "{\"v\":1,\"client_id\":\"" + app_id() + "\"}")) {
        for (int waited = 0; waited < 5000 && !answered; waited += 20) {  // (the handshake's answer: 5 s at most)
            const int n = conn_available();
            if (n < 0) break;
            if (n > 0) answered = true;
            else std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    if (!answered || !frame_recv(&op, &body) || op != OP_FRAME || body.find("\"READY\"") == std::string::npos) {
        Logf("Discord did not accept the connection: %.200s", op == OP_CLOSE ? body.c_str() : "(no answer)");
        conn_close();
        return false;
    }
    g_connected = true;
    Logf("connected");
    return true;
}

// Discord's frames since the last look: pings answered, errors logged. false: the connection is gone
bool drain() {
    for (;;) {
        const int n = conn_available();
        if (n < 0) return false;
        if (n == 0) return true;
        std::uint32_t op = 0;
        std::string body;
        if (!frame_recv(&op, &body)) return false;
        if (op == OP_PING && !frame_send(OP_PONG, body)) return false;
        if (op == OP_CLOSE) {
            Logf("closed by Discord: %.200s", body.c_str());
            return false;
        }
        if (op == OP_FRAME && body.find("\"ERROR\"") != std::string::npos) Logf("%.300s", body.c_str());
    }
}

void discord_thread() {
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);
    using clock = std::chrono::steady_clock;
    std::string sent, want;
    clock::time_point next_try{}, last_sent{};
    unsigned nonce = 0;
    for (;;) {
        bool enabled;
        {
            std::unique_lock lock(g_mutex);
            g_wake.wait_for(lock, std::chrono::seconds(1));
            want = g_want;
            enabled = g_enabled;
        }
        const auto now = clock::now();
        if (!enabled) {
            if (g_connected) disconnect("turned off");
            sent.clear();
            g_state = 0;
            continue;
        }
        if (!g_connected) {
            g_state = 1;
            if (now < next_try) continue;
            next_try = now + kRetry;
            if (!connect_discord()) continue;
            sent.clear();
        }
        g_state = 2;
        if (!drain()) {
            disconnect("connection lost");
            g_state = 1;
            continue;
        }
        if (want == sent || now - last_sent < kMinGap) continue;
        char head[96];
        std::snprintf(head, sizeof head, "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%lu,\"activity\":",
                      static_cast<unsigned long>(GetCurrentProcessId()));
        const std::string msg = head + want + "},\"nonce\":\"" + std::to_string(++nonce) + "\"}";
        if (!frame_send(OP_FRAME, msg)) {
            disconnect("connection lost");
            g_state = 1;
            continue;
        }
        sent = want;
        last_sent = now;
        if (g_log_all) Logf("%s", want.c_str());
    }
}

// ---- the activity
struct Activity {
    std::string details, state;  // the profile's two lines
    std::string large_image = "logo", large_text, small_image, small_text;
    std::int64_t start = 0;      // the elapsed timer's start (Unix time)
    int party = 0, party_max = 0;
    std::string party_id;
};

std::string json_text(const std::string& text) {  // (UTF-8 passes through; quotes, backslashes and controls escaped)
    size_t keep = std::min<size_t>(text.size(), 127);  // (Discord's 128 at most, cut between two characters)
    while (keep < text.size() && keep > 0 && (static_cast<unsigned char>(text[keep]) & 0xc0) == 0x80) --keep;
    std::string o = "\"";
    for (const unsigned char c : text.substr(0, keep)) {
        if (c == '"' || c == '\\') o += '\\', o += static_cast<char>(c);
        else if (c < 0x20) {
            char u[8];
            std::snprintf(u, sizeof u, "\\u%04x", c);
            o += u;
        } else o += static_cast<char>(c);
    }
    return o + "\"";
}

std::string to_json(const Activity& a) {
    std::string o = "{", assets;
    auto add = [](std::string& to, const char* key, const std::string& value) {
        if (value.empty()) return;
        to += (to.size() > 1 ? ",\"" : "\"") + std::string(key) + "\":" + json_text(value);
    };
    add(o, "details", a.details);
    add(o, "state", a.state);
    if (a.start) o += (o.size() > 1 ? "," : "") + std::string("\"timestamps\":{\"start\":") + std::to_string(a.start) + "}";
    assets = "{";
    add(assets, "large_image", a.large_image);
    add(assets, "large_text", a.large_text);
    add(assets, "small_image", a.small_image);
    add(assets, "small_text", a.small_text);
    if (assets.size() > 1) o += (o.size() > 1 ? "," : "") + std::string("\"assets\":") + assets + "}";
    if (a.party > 0 && a.party_max >= a.party) {
        o += (o.size() > 1 ? "," : "") + std::string("\"party\":{\"id\":") + json_text(a.party_id.empty() ? "solo" : a.party_id) +
             ",\"size\":[" + std::to_string(a.party) + "," + std::to_string(a.party_max) + "]}";
    }
    return o + "}";
}

void set_activity(const Activity* a) {
    const std::string json = a ? to_json(*a) : "null";
    std::lock_guard lock(g_mutex);
    if (json == g_want) return;
    g_want = json;
    g_wake.notify_one();
}

// The game's weapon numbers (Presence::Info::weapon): the names, and their icons (the application's art assets, MHGU's)
const char* const kWeaponEn[] = {"Great Sword", "Sword & Shield", "Hammer",     "Lance",       "Heavy Bowgun",
                                 nullptr,       "Light Bowgun",   "Long Sword", "Switch Axe",  "Gunlance",
                                 "Bow",         "Dual Blades",    "Hunting Horn", "Insect Glaive", "Charge Blade"};
const char* const kWeaponEs[] = {"Gran espada", "Espada y escudo", "Martillo",      "Lanza",           "Ballesta pesada",
                                 nullptr,       "Ballesta ligera", "Espada larga",  "Hacha espada",    "Lanza pistola",
                                 "Arco",        "Espadas dobles",  "Cornamusa",     "Glaive insecto",  "Hacha cargada"};
const char* const kWeaponIcon[] = {"great_sword", "sword_and_shield", "hammer",       "lance",         "bowgun",
                                   nullptr,       "light_bowgun",     "long_sword",   "switch_axe",    "gunlance",
                                   "bow",         "dual_blades",      "hunting_horn", "insect_glaive", "charge_blade"};

bool in_room(const mh4n::rooms::Status& st) {
    return st.state == mh4n::rooms::State::Joined || st.state == mh4n::rooms::State::Moderator;
}

// The room's party id, the same in every member's game (its name, its port and its host's), and how many are in it
std::string room_party(const mh4n::rooms::Status& st) {
    const std::string key = st.room.name + "|" + std::to_string(st.room.port) + "|" + st.room.host_username;
    std::uint64_t h = 1469598103934665603ull;  // (FNV-1a)
    for (const unsigned char c : key) h = (h ^ c) * 1099511628211ull;
    char id[24];
    std::snprintf(id, sizeof id, "mh4u-%016llx", static_cast<unsigned long long>(h));
    return id;
}

void make_activity() {
    const bool es = settings_spanish();
    const bool show_hunter = app_setting_bool("Discord", "ShowHunter", true);
    const Port::Presence::Info info = Port::Presence::Current();
    const Port::Hooks::GameState game = Port::Hooks::CurrentGameState();
    const mh4n::rooms::Status room = mh4n::rooms::GetStatus();
    const bool online = in_room(room);
    const int language = info.language;
    if (language >= 1) mh4n::platform::monster_names::Prepare(language);  // (the monsters' and maps' names, once)

    // where: -1 the menus, 1 a town, 2 a quest; 0 between two (what was shown stays)
    int place = 0;
    if (game.front_end == 1 || !info.hunter) place = -1;
    else if (info.quest || game.place == Port::Hooks::GameState::PLACE_QUEST) place = 2;
    else if (game.place == Port::Hooks::GameState::PLACE_TOWN) place = 1;
    static int last_place = -2, last_quest = -1;
    static std::int64_t since = 0;
    static Uint64 menus_since = 0;
    const Uint64 now = SDL_GetTicks();
    // (the menus for a moment between two places, a load: what was shown stays, the menus only after 15 s)
    if (place != -1) menus_since = 0;
    else if (!menus_since) menus_since = now;
    if (place == 0 || (place == -1 && last_place > 0 && now - menus_since < 15000)) return;
    if (place != last_place || (place == 2 && info.quest_id != last_quest)) since = static_cast<std::int64_t>(std::time(nullptr));
    last_place = place, last_quest = place == 2 ? info.quest_id : -1;

    Activity a;
    a.start = since;
    a.large_text = "Monster Hunter 4 Ultimate";
    if (place == -1) {
        a.details = es ? "En los menús" : "In the menus";
        set_activity(&a);
        return;
    }
    const int w = info.weapon;
    const char* weapon = w >= 0 && w < 15 ? (es ? kWeaponEs[w] : kWeaponEn[w]) : nullptr;
    if (w >= 0 && w < 15 && kWeaponIcon[w]) a.small_image = kWeaponIcon[w];
    std::string who = show_hunter ? info.name + ", " + (es ? "RC " : "HR ") + std::to_string(info.hr) : std::string();
    if (weapon) who += (who.empty() ? "" : " - ") + std::string(weapon);
    a.small_text = who;
    const std::string party_id = online ? room_party(room) : std::string();
    if (place == 2) {
        std::string monsters;
        for (const int kind : info.monsters) {
            const std::string name = language >= 1 ? mh4n::platform::monster_names::Name(language, kind) : std::string();
            if (name.empty() || monsters.find(name) != std::string::npos) continue;
            monsters += (monsters.empty() ? "" : ", ") + name;
        }
        a.details = !monsters.empty() ? (es ? "Cazando: " : "Hunting ") + monsters : (es ? "En una misión" : "On a quest");
        const std::string map = language >= 1 && info.stage > 0 ? mh4n::platform::monster_names::MapName(language, info.stage)
                                                               : std::string();
        std::string area;
        if (info.area == 0) area = es ? "Campamento" : "Camp";
        else if (info.area > 0) area = (es ? "Zona " : "Area ") + std::to_string(info.area);
        a.state = map.empty() ? area : area.empty() ? map : map + ", " + area;
        a.large_text = map.empty() ? a.large_text : map;
        if (online || info.hunters > 1) {  // (the quest's hunters, not the room's)
            a.party = std::min(info.hunters, 4), a.party_max = 4;
            a.party_id = (online ? party_id : std::string("solo")) + "-q" + std::to_string(info.quest_id);
        }
    } else {
        a.details = es ? "En el pueblo" : "In town";
        if (online) a.details += es ? " (en línea)" : " (online)";
        a.state = who;
        if (online && room.room.member_slots > 0) {
            a.party = std::max<int>(1, static_cast<int>(room.members.size()));
            a.party_max = std::max<int>(a.party, static_cast<int>(room.room.member_slots));
            a.party_id = party_id;
        }
    }
    set_activity(&a);
}

}  // namespace

void pc_discord_start() {
    if (g_started) return;
    g_started = true;
    const char* env = std::getenv("MH4U_LOG_DISCORD");
    g_log_all = env != nullptr && *env && *env != '0';
    g_enabled = app_setting_bool("Discord", "RichPresence", true);
    Port::Presence::SetWanted(g_enabled);
    std::thread(discord_thread).detach();
}

void pc_discord_frame() {
    if (!g_started) return;
    static unsigned read_version = ~0u;
    static bool enabled = true;
    if (read_version != app_settings_version()) {  // (the switch: at once)
        read_version = app_settings_version();
        enabled = app_setting_bool("Discord", "RichPresence", true);
        Port::Presence::SetWanted(enabled);
        std::lock_guard lock(g_mutex);
        if (g_enabled != enabled) Logf("Rich Presence %s", enabled ? "on" : "off");
        g_enabled = enabled;
        g_wake.notify_one();
    }
    static Uint64 next = 0;
    const Uint64 now = SDL_GetTicks();
    if (!enabled || now < next) return;
    next = now + 1000;
    make_activity();
}

std::string pc_discord_status(bool es) {
    switch (g_state.load()) {
    case 2: return es ? "Conectado a Discord" : "Connected to Discord";
    case 1: return es ? "Discord no está abierto en esta PC" : "Discord isn't open on this PC";
    default: return "";
    }
}
