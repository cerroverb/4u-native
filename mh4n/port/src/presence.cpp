// Where the player is in the game, for the PC app's Discord Rich Presence (presence.h).
//
// The hunter: the save object ([0x00fcf7ec], hooks.cpp's ITEM_BOX_OWNER) holds the character from its +0x5c, laid out as
// the save file's character block (docs/research/field-saves.md): +0x00 the name (UTF-16, 12 characters at most), +0x2c
// the Hunter Rank (u32), +0x40 the weapon's record (28 bytes: its equipment type first, 7 the great sword ... 21 the
// charge blade, 11 the heavy bowgun and 13 the light one; type - 7 is the game's weapon number). At the title it holds
// the last save shown or nothing: the app says "in the menus" there by the game state's front end.
// A quest (sGameControl [0x00fb6b7c] +0x2b = 1): the hunter's area at +0xc90 (hunt_aids.cpp), the quest's number at the
// quest holder's ([0x00fbeffc]) +0xad50 (0x006b3c84 asks it for quest 100), the hunters in it (sPlayer's units, as
// hunt_aids.cpp counts them) and the large monsters alive (sEnemy's slots, the game's test of a large monster, as the
// health bars read them). The map: the area map's layout the game draws, ui<stage + 280> (hooks.cpp
// MapLayoutSeen; the quest file's own map number was not found), named by the game's mapName list (monster_names.h).
#include "presence.h"

#include "game_addr.h"
#include "hooks.h"  // (MapLayoutSeen: the quest's map)

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>
#include <fmt/format.h>

extern "C" {
#include "recomp.h"
}

namespace Port::Presence {

namespace {

// (game addresses: GameAddr, port/src/game_addr.h and config/port_addresses.txt; USA v1.3.0's values)
GameAddr SAVE_OBJECT{0x00fcf7ec};     // the save object* (hooks.cpp's ITEM_BOX_OWNER): the character at +0x5c
GameAddr GAME_CONTROL{0x00fb6b7c};    // sGameControl*
GameAddr QUEST_HOLDER{0x00fbeffc};    // the quest's holder*
GameAddr SPLAYER{0x00fb83e8};         // sPlayer*: the hunters' units at +0xa98, by player number
GameAddr SENEMY{0x00f32004};          // sEnemy*: 32 monster slots at +0x18
GameAddr MONSTER_TABLE{0x00f3a020};   // [[[[table] + 4*kind]]]: bit 0 a large monster (0x0068882c)
GameAddr FRAME_COUNTER{0x010571d4};   // u32: the game's frame counter

constexpr u32 SAVE_CHARACTER = 0x5c;
constexpr u32 CHAR_NAME = 0x00, CHAR_NAME_UNITS = 12, CHAR_HR = 0x2c, CHAR_WEAPON = 0x40;
constexpr u32 GAME_CONTROL_MODE = 0x2b;   // u8: 1 a quest
constexpr u32 GAME_CONTROL_AREA = 0xc90;  // u32: the hunter's area of the map
constexpr u32 GAME_CONTROL_LANGUAGE = 0x11;
constexpr u32 HOLDER_QUEST_ID = 0xad50;   // u32: the quest's number (0x006b3c84)
constexpr u32 SPLAYER_UNITS = 0xa98;
constexpr u32 ENEMY_SLOTS = 0x18, ENEMY_SLOT_COUNT = 32;
constexpr u32 EM_INFO = 0xe28, INFO_KIND = 0x1c0, INFO_STATE = 0x1ca;
constexpr int READ_EVERY = 15;            // frames between two readings (4 a second at 60)

std::mutex g_mutex;
Info g_info;
std::atomic<bool> g_wanted{false};
u32 g_frames = 0;
bool g_trace = false;
void (*g_log)(const std::string&) = nullptr;

void Log(const std::string& line) {
    if (g_log != nullptr) g_log(line);
}

bool HeapPointer(u32 p) {
    return p >= 0x08000000 && p < 0x10000000 && (p & 3) == 0;
}
bool DataPointer(u32 p) {
    return (p & 3) == 0 && ((p >= 0x00100000 && p < 0x01200000) || (p >= 0x08000000 && p < 0x10000000) ||
                            (p >= 0x14000000 && p < 0x1c000000) || (p >= 0x30000000 && p < 0x38000000));
}

u32 LargeMonsterInfo(RCtx* ctx, u32 monster) {
    const u32 info = HeapPointer(monster) ? rd32(ctx, monster + EM_INFO) : 0;
    const u32 table = HeapPointer(info) ? rd32(ctx, MONSTER_TABLE) : 0;
    const u32 entry = DataPointer(table) ? rd32(ctx, table + 4 * rd8(ctx, info + INFO_KIND)) : 0;
    const u32 row = DataPointer(entry) ? rd32(ctx, entry) : 0;
    return DataPointer(row) && (rd32(ctx, row) & 1) != 0 ? info : 0;
}

bool Dead(RCtx* ctx, u32 info) {
    const u8 state = rd8(ctx, info + INFO_STATE);
    return state == 0xb || state == 0xe || (state == 0xc && rd8(ctx, info + INFO_STATE + 1) == 0xff);
}

// The UTF-16 name into UTF-8 (a character that isn't a letter the game types ends it)
std::string Name(RCtx* ctx, u32 at) {
    std::string s;
    for (u32 i = 0; i < CHAR_NAME_UNITS; ++i) {
        u32 c = rd16(ctx, at + 2 * i);
        if (c == 0) break;
        if (c < 0x20 || (c >= 0xd800 && c < 0xe000)) return {};
        if (c < 0x80) {
            s += static_cast<char>(c);
        } else if (c < 0x800) {
            s += static_cast<char>(0xc0 | c >> 6), s += static_cast<char>(0x80 | (c & 0x3f));
        } else {
            s += static_cast<char>(0xe0 | c >> 12), s += static_cast<char>(0x80 | (c >> 6 & 0x3f));
            s += static_cast<char>(0x80 | (c & 0x3f));
        }
    }
    return s;
}

// (presence_trace) the bytes at `at`, `n` of them, as hex words
std::string Bytes(RCtx* ctx, u32 at, u32 n) {
    std::string s;
    for (u32 i = 0; i < n; i += 4) s += fmt::format("{}{:08x}", i % 32 == 0 ? fmt::format("\n  +{:x}: ", i) : " ", rd32(ctx, at + i));
    return s;
}

// The stage of an area map's layout, as the game's files pair them (<language>/data/m<nn>a<aa>_map*.arc hold
// <language>\lyt\quest\m<nn>\ui<n>a<aa>): ui<280 + stage> from the Ancestral Steppe (281) to the Polar Field (302, stage
// 20); the Volcanic Hollow (15) draws some areas with the Sunken Hollow's ui282 and the others with ui295, so 282 is the
// Sunken Hollow (2) until a ui295 is seen in the quest. The Great Desert (6) and the Great Sea (14, 21) have no area map.
// -1 for none or another layout.
int StageOfLayout(int layout, bool volcanic) {
    switch (layout) {
    case 282: return volcanic ? 15 : 2;
    case 295: return 15;
    case 302: return 20;
    case 281: case 283: case 284: case 285: case 287: case 288: case 289: case 290: case 291: case 292: case 293:
    case 296: case 297: case 298: case 299:
        return layout - 280;
    default: return -1;
    }
}

Info Read(RCtx* ctx) {
    Info info;
    const u32 save = rd32(ctx, SAVE_OBJECT);
    if (HeapPointer(save)) {
        const u32 character = save + SAVE_CHARACTER;
        info.name = Name(ctx, character + CHAR_NAME);
        const u32 hr = rd32(ctx, character + CHAR_HR);
        info.hunter = !info.name.empty() && hr >= 1 && hr <= 999;
        info.hr = info.hunter ? static_cast<int>(hr) : 0;
        const u32 type = rd8(ctx, character + CHAR_WEAPON);
        info.weapon = info.hunter && type >= 7 && type <= 21 && type != 12 ? static_cast<int>(type) - 7 : -1;
        if (!info.hunter) info.name.clear();
    }
    const u32 control = rd32(ctx, GAME_CONTROL);
    if (HeapPointer(control)) info.language = static_cast<int>(rd8(ctx, control + GAME_CONTROL_LANGUAGE));
    info.quest = HeapPointer(control) && rd8(ctx, control + GAME_CONTROL_MODE) == 1;
    if (!info.quest) return info;
    info.area = static_cast<int>(rd32(ctx, control + GAME_CONTROL_AREA) & 0xff);
    const u32 holder = rd32(ctx, QUEST_HOLDER);
    if (HeapPointer(holder)) {
        const u32 id = rd32(ctx, holder + HOLDER_QUEST_ID);
        info.quest_id = id > 0 && id < 0x100000 ? static_cast<int>(id) : 0;
    }
    const u32 players = rd32(ctx, SPLAYER);
    for (u32 n = 0; n < 4 && HeapPointer(players); ++n)
        info.hunters += HeapPointer(rd32(ctx, players + SPLAYER_UNITS + 4 * n)) ? 1 : 0;
    const u32 enemies = rd32(ctx, SENEMY);
    for (u32 i = 0; i < ENEMY_SLOT_COUNT && HeapPointer(enemies); ++i) {
        const u32 monster_info = LargeMonsterInfo(ctx, rd32(ctx, enemies + ENEMY_SLOTS + 4 * i));
        if (monster_info == 0 || Dead(ctx, monster_info)) continue;
        const int kind = static_cast<int>(rd8(ctx, monster_info + INFO_KIND));
        if (std::find(info.monsters.begin(), info.monsters.end(), kind) == info.monsters.end()) info.monsters.push_back(kind);
    }
    return info;
}

}  // namespace

Info Current() {
    std::lock_guard lock(g_mutex);
    return g_info;
}

void SetWanted(bool on) {
    g_wanted.store(on, std::memory_order_relaxed);
}

void Frame(RCtx* ctx) {
    if (!g_wanted.load(std::memory_order_relaxed) && !g_trace) return;
    if (g_frames++ % READ_EVERY != 0) return;
    Info info = Read(ctx);
    {  // the map: the area map's layout the game draws (hooks.cpp), forgotten at a quest's start (StageOfLayout)
        static bool was_quest = false, volcanic = false;
        if (info.quest && !was_quest) Hooks::ClearMapLayout(), volcanic = false;
        was_quest = info.quest;
        const int layout = info.quest ? Hooks::MapLayoutSeen() : -1;
        volcanic = volcanic || layout == 295;
        info.stage = StageOfLayout(layout, volcanic);
    }
    if (g_trace) {
        static Info last;
        static bool any = false;
        if (!any || info.hunter != last.hunter || info.name != last.name || info.hr != last.hr || info.weapon != last.weapon ||
            info.quest != last.quest || info.area != last.area || info.quest_id != last.quest_id || info.stage != last.stage ||
            info.hunters != last.hunters || info.monsters != last.monsters) {
            std::string monsters;
            for (int k : info.monsters) monsters += fmt::format(" {:02x}", k);
            Log(fmt::format("presence: f{} hunter {} \"{}\" HR {} weapon {}, quest {} (id {}) stage {} (layout ui{}) area {} "
                            "hunters {} monsters{}", rd32(ctx, FRAME_COUNTER), info.hunter, info.name, info.hr,
                            info.weapon, info.quest, info.quest_id, info.stage, Hooks::MapLayoutSeen(), info.area,
                            info.hunters, monsters));
            if (info.quest && (!any || !last.quest || info.quest_id != last.quest_id)) {
                const u32 control = rd32(ctx, GAME_CONTROL), holder = rd32(ctx, QUEST_HOLDER);
                if (HeapPointer(control)) Log("presence: sGameControl +0xc40" + Bytes(ctx, control + 0xc40, 0x80));
                if (HeapPointer(holder)) Log("presence: the quest holder +0xad00" + Bytes(ctx, holder + 0xad00, 0x100));
                if (HeapPointer(holder)) Log("presence: the quest holder +0xb400" + Bytes(ctx, holder + 0xb400, 0x200));
            }
            last = info, any = true;
        }
    }
    std::lock_guard lock(g_mutex);
    info.serial = g_info.serial + 1;
    g_info = std::move(info);
}

void Reset() {
    std::lock_guard lock(g_mutex);
    g_info = Info{};
    g_frames = 0;
    const char* env = std::getenv("MH4N_PRESENCE_TRACE");
    g_trace = env != nullptr && *env != 0 && *env != '0';
}

bool SetOption(const std::string& key, const std::string& value) {
    if (key != "presence_trace") return false;
    g_trace = value == "1" || value == "on" || value == "true" || value == "yes";
    return true;
}

void SetLog(void (*log)(const std::string& line)) {
    g_log = log;
}

}  // namespace Port::Presence
