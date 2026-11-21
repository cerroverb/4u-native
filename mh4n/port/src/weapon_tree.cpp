// The weapon tree's runtime side (weapon_tree.h): what the Smithy shows, the player's weapons and the game's tables.
//
// The game's weapon tables: their places and record formats are those Heavenfall documents for the European
// executable (core/equipment/tables/layout.py and records.py of Heavenfall, by popzol and BlitzWOLF, GPL-3.0:
// https://github.com/popzol/Heavenfall). The tables are byte for byte the same, at the same addresses, in USA v1.3.0
// (Heavenfall's digest of the table regions, c7b1d7b7..., matches this executable's): the addresses below are those
// facts, the code that reads them is this port's own. Their record formats:
// - a melee class: 24 bytes an id from its table's start; the game's own record of the weapon begins 4 bytes in, so its
//   last 4 bytes (sort key, flags, sound) are the first 4 of the next id's: sharpness row +0x06 and level +0x07, price
//   u32 +0x08, attack u16 at +0x0c (the true attack: the game shows it times the class's multiplier), defense +0x0e,
//   affinity s8 +0x0f, element +0x10 and its value / 10 s8 +0x11 (negative: needs Awaken), status +0x12 and +0x13,
//   slots +0x14, rarity index +0x17, flags (the next id's +0x02: a weapon of the game's when its low two bits are 1
//   or 2);
// - a bow or a bowgun: 40 bytes: rarity index +0x04, price u32 +0x08, attack u16 +0x0c, defense +0x0e, slots +0x10,
//   affinity +0x11, element +0x12 and +0x13, status +0x14 and +0x15, flags +0x1c;
// - the upgrades: a record an id (fewer records than weapons: the last ones have none), 24 bytes (4 materials to upgrade
//   into it, u16 item and u16 count each, then the 4 weapons it upgrades into, u16 at +0x10), the insect glaive's 12 (2
//   materials, its one child u16 at +0x08);
// - the create recipes (forged from scratch): 24 bytes each, its equipment type +0x00, its id u16 +0x02 and 4
//   materials from +0x04 (u16 item, u16 count);
// - the sharpness rows: 7 u16 each, where each colour ends (red ... purple); a weapon shows its row up to 150 + 50 x its
//   sharpness level (Heavenfall's gear_weapons.py: MH Generations' level byte / 2 is MH4U's level).
// What forging or upgrading costs is the weapon's price (the Smithy asks it for either).
//
// Where the game is (found for this port, USA v1.3.0, docs/research/weapon-tree.md): in a town the controller cLbMain
// ([0x00edbe90], its class checked by its vtable) runs one facility at a time, its number the byte +0x9788 (the
// facility switch 0x009ecbc0: 4 the Smithy, 0x009ad564). The Smithy keeps its state at +0xb708: +0x00 its state (2 the
// classes, 7 never set, 8 Upgrade...), +0x10 the list's row under the cursor (s16), +0x1c the list's rows, and from
// +0x434 the rows, 20 bytes each: the equipment type +0, the id u16 +4 (as 0x00a38450 reads them for the stats view).
// The save object [0x00fcf7ec] (its data from +0x5c): the weapon worn's type +0x9c and id +0x9e; the equipment box
// from the data's +0x173e, 0x1c bytes a slot, 1100 slots and 200 more for each of the data's +0xf220 bits 0x40 and
// 0x80 (0x00c1e7b4), each slot's type at +0 and id at +2.
#include "weapon_tree.h"

#include "game_addr.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <fmt/format.h>

extern "C" {
#include "recomp.h"
}

namespace Port::WeaponTree {

namespace {

// (game addresses: GameAddr, port/src/game_addr.h and config/port_addresses.txt; USA v1.3.0's values)
GameAddr LOBBY{0x00edbe90};         // cLbMain*: the towns' controller (null in quests)
GameAddr VT_CLBMAIN{0x00e6b544};    // its vtable
GameAddr SAVE{0x00fcf7ec};          // the save object*
GameAddr GAME_CONTROL{0x00fb6b7c};  // sGameControl*
GameAddr WEAPON_RECIPES{0x00f51dfc};  // the weapons' create recipes, 565 of 24 bytes
GameAddr WEAPON_SHARPNESS{0x00f581e4};  // the sharpness rows, 119 of 14 bytes
// each class's stats table and upgrade table, in equipment type order (7 the great sword ... 20 the charge blade)
GameAddr GS_STATS{0x00f70850}, GS_UP{0x00f61fea};
GameAddr SNS_STATS{0x00f71f60}, SNS_UP{0x00f6354a};
GameAddr HAMMER_STATS{0x00f73688}, HAMMER_UP{0x00f64b52};
GameAddr LANCE_STATS{0x00f74db0}, LANCE_UP{0x00f660fa};
GameAddr LBG_STATS{0x00f7f894}, LBG_UP{0x00f6e662};
GameAddr HBG_STATS{0x00f7e00c}, HBG_UP{0x00f6d912};
GameAddr LS_STATS{0x00f761f0}, LS_UP{0x00f6870a};
GameAddr SA_STATS{0x00f77738}, SA_UP{0x00f69a72};
GameAddr GL_STATS{0x00f789c8}, GL_UP{0x00f673ba};
GameAddr BOW_STATS{0x00f8157c}, BOW_UP{0x00f6f682};
GameAddr DB_STATS{0x00f79e80}, DB_UP{0x00f6ab82};
GameAddr HH_STATS{0x00f7b3f8}, HH_UP{0x00f6bf92};
GameAddr IG_STATS{0x00f7c550}, IG_UP{0x00f569ac};
GameAddr CB_STATS{0x00f7d450}, CB_UP{0x00f6cf9a};

struct ClassTable {
    GameAddr& stats;
    int count;
    GameAddr& upgrades;
    int upgrade_count;
    float multiplier;  // the attack shown, of the true one
    bool ranged;
    bool short_upgrades;  // the insect glaive's: 12 bytes, one child
};
const ClassTable kTables[kClasses] = {
    {GS_STATS, 246, GS_UP, 228, 4.8f, false, false},      // 7 great sword
    {SNS_STATS, 247, SNS_UP, 235, 1.4f, false, false},    // 8 sword and shield
    {HAMMER_STATS, 247, HAMMER_UP, 231, 5.2f, false, false},  // 9 hammer
    {LANCE_STATS, 216, LANCE_UP, 200, 2.3f, false, false},    // 10 lance
    {LBG_STATS, 185, LBG_UP, 172, 1.3f, true, false},     // 11 light bowgun
    {HBG_STATS, 157, HBG_UP, 142, 1.5f, true, false},     // 12 heavy bowgun
    {LS_STATS, 227, LS_UP, 207, 3.3f, false, false},      // 13 long sword
    {SA_STATS, 198, SA_UP, 182, 5.4f, false, false},      // 14 switch axe
    {GL_STATS, 221, GL_UP, 206, 2.3f, false, false},      // 15 gunlance
    {BOW_STATS, 206, BOW_UP, 190, 1.2f, true, false},     // 16 bow
    {DB_STATS, 229, DB_UP, 214, 1.4f, false, false},      // 17 dual blades
    {HH_STATS, 185, HH_UP, 171, 5.2f, false, false},      // 18 hunting horn
    {IG_STATS, 160, IG_UP, 134, 3.1f, false, true},       // 19 insect glaive
    {CB_STATS, 125, CB_UP, 101, 3.6f, false, false},      // 20 charge blade
};
constexpr int kRecipes = 565, kSharpnessRows = 119;
// FNV-1a of every region above (the melee tables with the 4 bytes past their last id, then the upgrades, the create
// recipes and the sharpness rows): the game's own tables (computed on USA v1.3.0's code.bin, sha1 c9225f87...)
constexpr std::uint64_t kTablesDigest = 0x85f3097e62b06025ull;

constexpr u32 LOBBY_FACILITY = 0x9788;  // u8: the facility the town runs (4 the Smithy)
constexpr u8 FACILITY_SMITHY = 4;
constexpr u32 SMITHY = 0xb708;          // the Smithy's state block
constexpr u32 SMITHY_STATE = 0x00, SMITHY_ROW = 0x10, SMITHY_ROWS = 0x1c, SMITHY_LIST = 0x434, SMITHY_ROW_SIZE = 20;
constexpr int kMaxRows = 400;
constexpr u32 SAVE_DATA = 0x5c, SAVE_WORN = 0x9c;
constexpr u32 BOX_AT = 0x173e, BOX_SLOT = 0x1c, BOX_FLAGS = 0xf220;
constexpr u32 GAME_CONTROL_LANGUAGE = 0x11;  // u8: the game's language (hunt_aids.cpp)
constexpr int kBoxEvery = 30;  // in the Smithy: the box read again every this many frames

struct Options {
    bool on = true;
    bool trace = false;
    std::uint32_t states = ~0u;  // weapon_tree_states: the Smithy's states (bits 0-31) whose list's weapon shows
};
Options options;
void (*log_line)(const std::string&) = nullptr;
std::atomic<std::string (*)(int, int, int)> name_of{nullptr};  // SetNames
std::atomic<void (*)(int)> prepare_names{nullptr};
std::atomic<std::string (*)(int, int)> item_of{nullptr};
std::atomic<bool (*)(int, std::vector<std::uint8_t>*, int*)> icons_of{nullptr};

void Log(const std::string& line) {
    if (log_line != nullptr) log_line(line);
}

bool IsOn(const std::string& v) {
    return v == "1" || v == "true" || v == "on" || v == "yes";
}
bool IsOff(const std::string& v) {
    return v == "0" || v == "false" || v == "off" || v == "no";
}

bool HeapPointer(u32 p) {
    return p >= 0x08000000 && p < 0x10000000 && (p & 3) == 0;
}

// ── What the frames publish, under the mutex
std::mutex mutex;
Smithy smithy;                                   // Current()
std::vector<weapon_tree::Class> classes;         // the tables, by class (no names), once read
bool tables_known = false;                       // read, and the game's own
std::vector<std::vector<std::uint8_t>> owned(kClasses);  // by class, by id: a copy in the box
int worn_type = -1, worn_id = -1;

// ── The emulation thread's own
bool tables_read = false;
std::uint32_t frames = 0, box_at = 0;
int last_type = -1, last_id = -1, last_state = -1;

// A recipe's `n` materials from `at` (u16 item, u16 count each): the items' ids (Tree names them)
void Materials(RCtx* ctx, u32 at, int n, weapon_tree::Recipe* out) {
    for (int k = 0; k < n; ++k) {
        const int item = static_cast<int>(rd16(ctx, at + 4 * k)), count = static_cast<int>(rd16(ctx, at + 4 * k + 2));
        if (item <= 0 || count <= 0) continue;
        weapon_tree::Material m;
        m.item = item, m.count = count;
        out->items.push_back(m);
    }
}

int ElementOf(int type) {  // the tables' element numbers: 1 fire, 2 water, 3 thunder, 4 dragon, 5 ice
    switch (type) {
    case 1: return weapon_tree::kFire;
    case 2: return weapon_tree::kWater;
    case 3: return weapon_tree::kThunder;
    case 4: return weapon_tree::kDragon;
    case 5: return weapon_tree::kIce;
    default: return weapon_tree::kElementNone;
    }
}
int StatusOf(int type) {  // 1 poison, 2 paralysis, 3 sleep, 4 blast
    switch (type) {
    case 1: return weapon_tree::kPoison;
    case 2: return weapon_tree::kParalysis;
    case 3: return weapon_tree::kSleep;
    case 4: return weapon_tree::kBlast;
    default: return weapon_tree::kElementNone;
    }
}

std::uint64_t Digest(RCtx* ctx) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    auto add = [&](u32 at, u32 size) {
        for (u32 i = 0; i < size; ++i) h = (h ^ rd8(ctx, at + i)) * 0x100000001b3ull;
    };
    for (const ClassTable& t : kTables) {
        add(t.stats, t.count * (t.ranged ? 40 : 24) + (t.ranged ? 0 : 4));
        add(t.upgrades, t.upgrade_count * (t.short_upgrades ? 12 : 24));
    }
    add(WEAPON_RECIPES, kRecipes * 24);
    add(WEAPON_SHARPNESS, kSharpnessRows * 14);
    return h;
}

// The game's tables, once: every class's weapons, their upgrades and which are forged from scratch.
void ReadTables(RCtx* ctx) {
    tables_read = true;
    const std::uint64_t digest = Digest(ctx);
    if (digest != kTablesDigest) {
        Log(fmt::format("weapon tree: the game's weapon tables are not the ones known (digest {:016x}): the Smithy's "
                        "tree is off", digest));
        return;
    }
    std::vector<weapon_tree::Class> out(kClasses);
    for (int c = 0; c < kClasses; ++c) {
        const ClassTable& t = kTables[c];
        std::vector<weapon_tree::Weapon>& ws = out[c].weapons;
        ws.resize(t.count);
        for (int id = 0; id < t.count; ++id) {
            weapon_tree::Weapon& w = ws[id];
            const u32 r = t.stats + id * (t.ranged ? 40 : 24);
            const u32 flags = t.ranged ? rd8(ctx, r + 0x1c) : rd8(ctx, r + 24 + 2);
            w.real = (flags & 3) == 1 || (flags & 3) == 2;
            if (!w.real) continue;
            const int stored = static_cast<int>(rd16(ctx, r + 0x0c));
            w.attack = static_cast<int>(std::lround(stored * t.multiplier));
            int element, element_value, status, status_value;
            const int price = static_cast<int>(rd32(ctx, r + 0x08));
            w.defense = rd8(ctx, r + 0x0e);
            if (t.ranged) {
                w.rarity = rd8(ctx, r + 0x04) + 1;
                w.slots = rd8(ctx, r + 0x10);
                w.affinity = static_cast<s8>(rd8(ctx, r + 0x11));
                element = rd8(ctx, r + 0x12), element_value = static_cast<s8>(rd8(ctx, r + 0x13));
                status = rd8(ctx, r + 0x14), status_value = static_cast<s8>(rd8(ctx, r + 0x15));
                if (c == 11 - kFirstType || c == 12 - kFirstType) element = status = 0;  // (bowguns: ammo instead)
            } else {
                w.rarity = rd8(ctx, r + 0x17) + 1;
                w.slots = rd8(ctx, r + 0x14);
                w.affinity = static_cast<s8>(rd8(ctx, r + 0x0f));
                element = rd8(ctx, r + 0x10), element_value = static_cast<s8>(rd8(ctx, r + 0x11));
                status = rd8(ctx, r + 0x12), status_value = static_cast<s8>(rd8(ctx, r + 0x13));
                const int row = rd8(ctx, r + 0x06), level = rd8(ctx, r + 0x07);
                if (row < kSharpnessRows && level <= 6) {  // (the row up to its level's cut)
                    for (int k = 0; k < 7; ++k)
                        w.sharpness.push_back(std::min<int>(rd16(ctx, WEAPON_SHARPNESS + 14 * row + 2 * k), 150 + 50 * level));
                }
            }
            const bool has_element = ElementOf(element) != weapon_tree::kElementNone && element_value != 0;
            const bool has_status = StatusOf(status) != weapon_tree::kElementNone && status_value != 0;
            if (has_element) {
                w.element = ElementOf(element), w.element_value = std::abs(element_value) * 10;
                w.element_hidden = element_value < 0;
            }
            if (has_status) {
                int& kind = has_element ? w.element2 : w.element;
                int& value = has_element ? w.element2_value : w.element_value;
                kind = StatusOf(status), value = std::abs(status_value) * 10;
                if (!has_element) w.element_hidden = status_value < 0;
            }
            if (id < t.upgrade_count) {
                const u32 u = t.upgrades + id * (t.short_upgrades ? 12 : 24);
                const int kids = t.short_upgrades ? 1 : 4;
                for (int k = 0; k < kids; ++k) {
                    const int child = static_cast<int>(rd16(ctx, u + (t.short_upgrades ? 8 : 16) + 2 * k));
                    if (child > 0 && child < t.count) w.children.push_back(child);
                }
                Materials(ctx, u, t.short_upgrades ? 2 : 4, &w.upgrade);
                if (!w.upgrade.items.empty()) w.upgrade.zenny = price;
            }
            w.create.zenny = price;  // (kept for a create recipe: the recipes below)
        }
    }
    for (int i = 0; i < kRecipes; ++i) {
        const u32 r = WEAPON_RECIPES + 24 * i;
        const int type = rd8(ctx, r), id = static_cast<int>(rd16(ctx, r + 2));
        const int c = type - kFirstType;
        if (c >= 0 && c < kClasses && id >= 0 && id < static_cast<int>(out[c].weapons.size())) {
            weapon_tree::Weapon& w = out[c].weapons[id];
            w.forge = true;
            if (w.create.items.empty()) Materials(ctx, r + 4, 4, &w.create);
        }
    }
    for (int c = 0; c < kClasses; ++c) {
        if (!kTables[c].ranged) out[c].sharpness_full = 450;
        for (weapon_tree::Weapon& w : out[c].weapons)
            if (!w.forge) w.create = weapon_tree::Recipe{};  // (its price only where a create recipe is)
    }
    int count = 0;
    for (const auto& c : out)
        count += static_cast<int>(std::count_if(c.weapons.begin(), c.weapons.end(), [](const auto& w) { return w.real; }));
    Log(fmt::format("weapon tree: the game's weapon tables read ({} weapons in 14 classes)", count));
    std::lock_guard lock(mutex);
    classes = std::move(out);
    tables_known = true;
}

// The box's weapons and the one worn.
void ReadBox(RCtx* ctx) {
    const u32 save = rd32(ctx, SAVE);
    if (!HeapPointer(save)) return;
    const u32 data = save + SAVE_DATA;
    const u32 bits = rd32(ctx, data + BOX_FLAGS);
    const u32 slots = 1100 + ((bits & 0x40) ? 200 : 0) + ((bits & 0x80) ? 200 : 0);
    std::vector<std::vector<std::uint8_t>> had(kClasses);
    for (int c = 0; c < kClasses; ++c) had[c].assign(kTables[c].count, 0);
    for (u32 i = 0; i < slots; ++i) {
        const u32 slot = data + BOX_AT + i * BOX_SLOT;
        const int c = static_cast<int>(rd8(ctx, slot)) - kFirstType;
        if (c < 0 || c >= kClasses) continue;
        const int id = static_cast<int>(rd16(ctx, slot + 2));
        if (id > 0 && id < kTables[c].count) had[c][id] = 1;
    }
    const int wt = rd8(ctx, save + SAVE_WORN), wid = static_cast<int>(rd16(ctx, save + SAVE_WORN + 2));
    std::lock_guard lock(mutex);
    owned = std::move(had);
    worn_type = wt, worn_id = wid;
}

void Publish(bool shown, int type, int id, int language) {
    std::lock_guard lock(mutex);
    if (shown != smithy.shown || type != smithy.type || id != smithy.id) ++smithy.serial;
    smithy.shown = shown, smithy.type = type, smithy.id = id, smithy.language = language;
}

}  // namespace

Smithy Current() {
    std::lock_guard lock(mutex);
    return smithy;
}

bool Tree(weapon_tree::Class* cls, weapon_tree::View* view, int* type) {
    Smithy now;
    std::vector<std::uint8_t> had;
    int worn = -1;
    {
        std::lock_guard lock(mutex);
        if (!smithy.shown || !tables_known) return false;
        now = smithy;
        const int c = now.type - kFirstType;
        *cls = classes[c];
        had = owned[c];
        worn = worn_type == now.type ? worn_id : -1;
    }
    const auto name = name_of.load();
    const auto item = item_of.load();
    for (int id = 0; id < static_cast<int>(cls->weapons.size()); ++id) {
        weapon_tree::Weapon& w = cls->weapons[id];
        if (!w.real) continue;
        if (name != nullptr) w.name = name(now.language, now.type, id);
        for (weapon_tree::Recipe* r : {&w.create, &w.upgrade})
            for (weapon_tree::Material& m : r->items)
                if (item != nullptr) m.name = item(now.language, m.item);
    }
    *view = weapon_tree::Build(*cls, now.id, had, worn);
    *type = now.type;
    return !view->nodes.empty();
}

bool SetOption(const std::string& key, const std::string& value) {
    if (key == "weapon_tree") {
        options.on = !IsOff(value);
        if (!options.on) Publish(false, -1, -1, -1);
    } else if (key == "weapon_tree_states") {  // (tests: "all", or the states, "3 8")
        options.states = 0;
        std::size_t at = 0;
        while (at < value.size()) {
            const std::size_t end = value.find_first_of(" ,", at);
            const std::string word = value.substr(at, end == std::string::npos ? std::string::npos : end - at);
            if (word == "all") options.states = ~0u;
            else if (!word.empty() && word.find_first_not_of("0123456789") == std::string::npos && std::stoi(word) < 32)
                options.states |= 1u << std::stoi(word);
            if (end == std::string::npos) break;
            at = end + 1;
        }
    } else if (key == "weapon_tree_trace") {
        options.trace = IsOn(value);
    } else {
        return false;
    }
    return true;
}

bool On() {
    return options.on;
}

void Reset() {
    options = Options{};
    tables_read = false;
    frames = box_at = 0;
    last_type = last_id = last_state = -1;
    std::lock_guard lock(mutex);
    smithy = Smithy{};
    classes.clear();
    tables_known = false;
    owned.assign(kClasses, {});
    worn_type = worn_id = -1;
}

void SetLog(void (*log)(const std::string& line)) {
    log_line = log;
}

void SetNames(std::string (*name)(int language, int type, int id), void (*prepare)(int language),
              std::string (*item)(int language, int id), bool (*icons)(int, std::vector<std::uint8_t>*, int*)) {
    name_of = name;
    prepare_names = prepare;
    item_of = item;
    icons_of = icons;
}

bool Icons(int language, std::vector<std::uint8_t>* rgba, int* cell) {
    const auto icons = icons_of.load();
    return icons != nullptr && icons(language, rgba, cell);
}

void Frame(RCtx* ctx) {
    ++frames;
    if (!options.on) return;
    const u32 lobby = rd32(ctx, LOBBY);
    const bool town = HeapPointer(lobby) && rd32(ctx, lobby) == VT_CLBMAIN;
    if (town && options.trace && !tables_read) {  // (tests: the tables' check and the names' and icons' reading in any
        ReadTables(ctx);                          // town, logged)
        const u32 control = rd32(ctx, GAME_CONTROL);
        const auto prepare = prepare_names.load();
        if (HeapPointer(control) && prepare != nullptr) prepare(rd8(ctx, control + GAME_CONTROL_LANGUAGE));
    }
    if (!town || rd8(ctx, lobby + LOBBY_FACILITY) != FACILITY_SMITHY) {
        if (last_state != -1) {
            if (options.trace) Log(fmt::format("weapon tree: f{} the Smithy closed", frames));
            last_state = last_type = last_id = -1;
            Publish(false, -1, -1, -1);
        }
        return;
    }
    if (!tables_read) ReadTables(ctx);
    const u32 st = lobby + SMITHY;
    const int state = rd8(ctx, st + SMITHY_STATE);
    const int rows = static_cast<int>(rd16(ctx, st + SMITHY_ROWS));
    const int row = static_cast<s16>(rd16(ctx, st + SMITHY_ROW));
    int type = -1, id = -1;
    if (state < 32 && (options.states >> state & 1) && rows > 0 && rows <= kMaxRows && row >= 0 && row < rows) {
        const u32 r = st + SMITHY_LIST + SMITHY_ROW_SIZE * row;
        type = rd8(ctx, r);
        id = static_cast<int>(rd16(ctx, r + 4));
        const int c = type - kFirstType;
        if (c < 0 || c >= kClasses || id <= 0 || id >= kTables[c].count) type = id = -1;
    }
    const u32 control = rd32(ctx, GAME_CONTROL);
    const int language = HeapPointer(control) ? rd8(ctx, control + GAME_CONTROL_LANGUAGE) : -1;
    if (const auto prepare = prepare_names.load(); prepare != nullptr && language >= 0) prepare(language);  // (once)
    const bool changed = type != last_type || id != last_id;
    if (changed || frames - box_at >= kBoxEvery) {
        ReadBox(ctx);
        box_at = frames;
    }
    static int last_sub = -1;  // (the trace's: the state's sub-state too, Upgrade's box or its list)
    const int sub = rd8(ctx, st + 1);
    if (options.trace && (state != last_state || sub != last_sub || changed)) {
        Log(fmt::format("weapon tree: f{} Smithy state {} sub {} class {} view {} row {} of {}: {}", frames, state,
                        sub, rd8(ctx, st + 7), rd8(ctx, st + 0x1bc), row, rows,
                        type < 0 ? std::string("no weapon") : fmt::format("type {} id {}", type, id)));
    }
    last_state = state, last_sub = sub;
    last_type = type, last_id = id;
    Publish(type >= 0, type, id, language);
    if (options.trace && changed && type >= 0) {
        weapon_tree::Class cls;
        weapon_tree::View view;
        int t = 0;
        if (Tree(&cls, &view, &t)) {
            Log(fmt::format("weapon tree: the tree of type {} id {} ({} nodes, {} columns, {} rows):\n{}", t, id,
                            view.nodes.size(), view.columns, view.rows, weapon_tree::Text(cls, view)));
        }
    }
}

}  // namespace Port::WeaponTree
