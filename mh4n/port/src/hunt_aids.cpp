// Hunting aids of the port (1.0.1), both off unless the player turns them on (hunt_aids.h).
//
// Large monsters always on the map (monsters_on_map). The game shows a large monster on its maps (the touch screen's
// and the top screen's area map) while the monster is painted: its paint timer, a float at the monster's status block
// + 0x20c, is above 0 (0x0095541c; the maps' code, 0x00b8668c and 0x00b86d38, asks it of each large monster, after
// the HUD's own mask: the Autotracker skill, 0x00b964cc). A paintball sets it, and the game sets it itself (36000) at
// the start for the monsters it always shows (0x00910678); the monster's step takes the frame's time off it
// (0x009171a8). Here, in a quest, each large monster that is alive and not painted gets a timer of PAINT_KEEP, kept
// between PAINT_LOW and that once a frame: the game then shows it on both maps, in its place and with its own icon,
// from the quest's start. It stays above 7200, under which a Palico says the paint is wearing off (0x00985edc). A
// monster the player paints (a timer above that) is left to the game; with the option turned off during a quest, a
// timer of ours goes back to 0. Nothing else of the monster's changes. Only in a quest this console hunts alone: with
// other hunters in the quest (a room's) the paint is left to the game, since a painted monster is shown to the whole
// party (a paintball's mark goes to everyone) and it was not proven that a timer written here stays on this console.
//
// Damage numbers (damage_numbers). Once a frame, each large monster's health (s32 at the status block + 0x3e8; its
// maximum at + 0x3ec, 0x0091ad08) is compared with the last frame's: a drop is a number, placed where the hit landed
// (the monster's + 0x1338, a place the game writes at each hit) or, when that place didn't change (poison, a hit the
// game didn't place there), at the middle of the monster's body (+ 0x168, the centre of its model's bounding sphere,
// whose radius follows at + 0x174), a little above it (NUMBER_LIFT). The app draws the numbers (Numbers()) over the top
// screen's picture, never in it; here each one's place is worked out again every frame with the camera the top screen is
// drawn with: its view from the camera's eye, up and target (camera + 0x40, + 0x50, + 0x60, as the game's 0x00b70fe0
// makes it) and its projection as getProjMatrix gave it this frame (Projection, from hooks.cpp, after the port's
// widescreen and field of view), so the number stays where the hit was while the camera moves. Any drop counts: a
// hunter's hit, a bomb, poison, another player's (the room keeps everyone's health the same).
//
// Monster health bars (monster_health_bar). Once a frame, every large monster of the quest that is alive (not dead, not
// captured), with its health and maximum (+ 0x3e8, + 0x3ec) and whether it is in the hunter's area, in sEnemy's order:
// Healths() for the app, which draws a bar for each over the top screen. Nothing is written, so it is the same in a
// room's quest (where the room keeps every console's health the same) as alone.
#include "hunt_aids.h"

#include "game_addr.h"
#include "hooks.h"

#include <algorithm>
#include <atomic>
#include <chrono>
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

namespace Port::HuntAids {

namespace {

// (game addresses: GameAddr, port/src/game_addr.h and config/port_addresses.txt; USA v1.3.0's values)
GameAddr SENEMY{0x00f32004};          // sEnemy*: 32 monster slots at +0x18 (0x0068fb9c)
GameAddr MONSTER_TABLE{0x00f3a020};   // [[[[table] + 4*kind]]]: bit 0 a large monster (0x0068882c)
GameAddr GAME_CONTROL{0x00fb6b7c};    // sGameControl*
GameAddr SCAMERA_INSTANCE{0x0105729c};  // sMhCamera*
GameAddr FRAME_COUNTER{0x010571d4};   // u32: the game's frame counter (hunt_trace's lines: the runner's fN)
GameAddr SPLAYER{0x00fb83e8};         // sPlayer*: the hunters' units at +0xa98, by player number

constexpr u32 ENEMY_SLOTS = 0x18, ENEMY_SLOT_COUNT = 32;
constexpr u32 EM_INFO = 0xe28;           // monster: its status block
constexpr u32 INFO_AREA = 0x14c;         // u8: the area of the map it is in
constexpr u32 INFO_KIND = 0x1c0;         // u8: its row of the monster table
constexpr u32 INFO_STATE = 0x1ca;        // u8: 0xb, 0xe dead; 0xc with +0x1cb 0xff captured (0x0094d770)
constexpr u32 INFO_PAINT = 0x20c;        // f32: the paint's time left, painted while above 0 (0x0095541c)
constexpr u32 INFO_HEALTH = 0x3e8;       // s32: health; + 4 its maximum (0x0091ad08)
constexpr u32 UNIT_CENTER = 0x168;       // vec3: the centre of the model's bounding sphere (+ 0x174 its radius)
constexpr u32 UNIT_HIT = 0x1338;         // vec3: where the last hit landed, written as it lands
constexpr u32 GAME_CONTROL_MODE = 0x2b;  // u8: 1 a quest
constexpr u32 GAME_CONTROL_AREA = 0xc90; // u32: the hunter's area of the map
constexpr u32 GAME_CONTROL_LANGUAGE = 0x11;  // u8: the game's language, its folders' row (0x002ffeb4, 0x005aec4c)
constexpr u32 SPLAYER_UNITS = 0xa98;     // uPlayer*[4], by player number (0 for a place nobody plays)
constexpr u32 SCAMERA_VIEWPORTS = 0x18, VIEWPORT_SIZE = 0x1a8, VIEWPORT_CAMERA = 0x04, VIEWPORT_DISPLAY = 0x16;
constexpr u32 CAMERA_EYE = 0x40, CAMERA_UP = 0x50, CAMERA_TARGET = 0x60;  // vec3s (0x00b70fe0)

constexpr float PAINT_KEEP = 7400.0f;    // the timer kept on a monster: above the Palico's 7200 (0x00985edc)
constexpr float PAINT_LOW = 7300.0f;     // under this it is set to PAINT_KEEP again
constexpr float NUMBER_LIFT = 20.0f;     // a number starts this far above the place (a hunter's eye is 170 up)
constexpr int MAX_NUMBERS = 24;

struct Options {
    bool monsters_on_map = false;
    bool damage_numbers = false;
    bool health_bar = false;  // (monster_health_bar: Healths() for the app's bars)
    bool trace = false;
    bool cost = false;  // (tests: MH4N_HUNT_COST=1, CostMeter's line without hunt_trace's others)
};
Options options;
void (*log_line)(const std::string&) = nullptr;

void Log(const std::string& line) {
    if (log_line != nullptr) log_line(line);
}

bool IsOn(const std::string& v) {
    return v == "1" || v == "true" || v == "on" || v == "yes";
}

float ReadF32(RCtx* ctx, u32 addr) {
    const u32 v = rd32(ctx, addr);
    float f;
    std::memcpy(&f, &v, sizeof(f));
    return f;
}

void WriteF32(RCtx* ctx, u32 addr, float f) {
    u32 v;
    std::memcpy(&v, &f, sizeof(v));
    wr32(ctx, addr, v);
}

bool HeapPointer(u32 p) {
    return p >= 0x08000000 && p < 0x10000000 && (p & 3) == 0;
}

/// The code's data, the application heap or the linear heap (the monster table's rows live there).
bool DataPointer(u32 p) {
    return (p & 3) == 0 && ((p >= 0x00100000 && p < 0x01200000) || (p >= 0x08000000 && p < 0x10000000) ||
                            (p >= 0x14000000 && p < 0x1c000000) || (p >= 0x30000000 && p < 0x38000000));
}

/// `monster`'s status block if it is a large monster (the game's test, 0x0068882c), else 0.
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

std::int64_t SteadyNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// ── The camera of the top screen: its projection as getProjMatrix gave it (Projection), its view from its fields

struct CameraSeen {
    u32 camera = 0;
    u64 frame = 0;     // the frame it was seen in (frames below)
    float proj[16] = {};
};
CameraSeen cameras[8];
u64 frames = 0;        // Frame()s since the boot

/// The view matrix the game's 0x00b70ff0 makes from an eye, a target and an up vector (row vectors: p' = p * view):
/// f the eye's direction, s = up x f, u = f x s, then rows (-s.x, u.x, -f.x, 0) ... (s.e, -u.e, f.e, 1).
void LookAt(const float e[3], const float t[3], const float up[3], float view[16]) {
    float f[3] = {t[0] - e[0], t[1] - e[1], t[2] - e[2]};
    const float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (fl > 1e-6f) {
        for (float& c : f) c /= fl;
    }
    float s[3] = {up[1] * f[2] - up[2] * f[1], up[2] * f[0] - up[0] * f[2], up[0] * f[1] - up[1] * f[0]};
    const float sl = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    if (sl > 1e-6f) {
        for (float& c : s) c /= sl;
    }
    const float u[3] = {f[1] * s[2] - f[2] * s[1], f[2] * s[0] - f[0] * s[2], f[0] * s[1] - f[1] * s[0]};
    for (int r = 0; r < 3; ++r) {
        view[r * 4 + 0] = -s[r];
        view[r * 4 + 1] = u[r];
        view[r * 4 + 2] = -f[r];
        view[r * 4 + 3] = 0.0f;
    }
    view[12] = s[0] * e[0] + s[1] * e[1] + s[2] * e[2];
    view[13] = -(u[0] * e[0] + u[1] * e[1] + u[2] * e[2]);
    view[14] = f[0] * e[0] + f[1] * e[1] + f[2] * e[2];
    view[15] = 1.0f;
}

/// p * m for a point (w = 1) and a row-vector matrix.
void Transform(const float m[16], const float p[3], float out[4]) {
    for (int c = 0; c < 4; ++c) {
        out[c] = p[0] * m[c] + p[1] * m[4 + c] + p[2] * m[8 + c] + m[12 + c];
    }
}

struct Camera {
    bool known = false;
    float view[16] = {}, proj[16] = {};
};

/// The camera the top screen's 3D picture is drawn with this frame: the first top-screen viewport's whose projection
/// getProjMatrix gave this frame or the last.
Camera TopCamera(RCtx* ctx) {
    Camera c;
    const u32 scamera = rd32(ctx, SCAMERA_INSTANCE);
    if (!HeapPointer(scamera)) {
        return c;
    }
    for (u32 i = 0; i < 8 && !c.known; ++i) {
        const u32 vp = scamera + SCAMERA_VIEWPORTS + i * VIEWPORT_SIZE;
        const u32 camera = rd32(ctx, vp + VIEWPORT_CAMERA);
        if (!HeapPointer(camera) || rd8(ctx, vp + VIEWPORT_DISPLAY) != 0) {
            continue;
        }
        for (const CameraSeen& seen : cameras) {
            if (seen.camera == camera && frames - seen.frame <= 1 && seen.proj[15] == 0.0f) {
                float e[3], up[3], t[3];
                for (int k = 0; k < 3; ++k) {
                    e[k] = ReadF32(ctx, camera + CAMERA_EYE + 4 * k);
                    up[k] = ReadF32(ctx, camera + CAMERA_UP + 4 * k);
                    t[k] = ReadF32(ctx, camera + CAMERA_TARGET + 4 * k);
                }
                LookAt(e, t, up, c.view);
                std::memcpy(c.proj, seen.proj, sizeof(c.proj));
                c.known = std::all_of(std::begin(c.view), std::end(c.view), [](float v) { return std::isfinite(v); });
                break;
            }
        }
    }
    return c;
}

/// Where the world point p is on the top screen's picture (fractions from its left and top edges); false behind the
/// camera or off the picture.
bool Project(const Camera& c, const float p[3], float* fx, float* fy) {
    float v[4], clip[4];
    Transform(c.view, p, v);
    Transform(c.proj, v, clip);
    if (!(clip[3] > 1e-3f)) {
        return false;
    }
    const float nx = clip[0] / clip[3], ny = clip[1] / clip[3];
    *fx = (nx + 1.0f) * 0.5f;
    *fy = (1.0f - ny) * 0.5f;
    return std::isfinite(*fx) && std::isfinite(*fy) && *fx > -0.1f && *fx < 1.1f && *fy > -0.1f && *fy < 1.1f;
}

// ── The monsters seen this quest

struct Seen {
    u32 monster = 0, info = 0;
    u8 kind = 0;
    s32 health = 0;
    bool painted_by_us = false;
    u64 frame = 0;    // last seen
    float hit[3] = {};  // the monster's last hit place (UNIT_HIT) as it was at the last number
};
std::vector<Seen> seen;
bool in_quest = false;

Seen* Find(u32 monster) {
    for (Seen& s : seen) {
        if (s.monster == monster) return &s;
    }
    return nullptr;
}

/// A timer of ours back to 0 (the option off, the quest over): only one still in our band (not one the player's
/// paintball set since).
void Unpaint(RCtx* ctx, Seen& s) {
    if (s.painted_by_us && HeapPointer(s.info) && rd32(ctx, s.monster + EM_INFO) == s.info &&
        ReadF32(ctx, s.info + INFO_PAINT) <= PAINT_KEEP) {
        WriteF32(ctx, s.info + INFO_PAINT, 0.0f);
    }
    s.painted_by_us = false;
}

// ── The numbers (the world place each one was dealt at; Numbers() gives them placed on the screen)

struct Dealt {
    float world[3];
    Number number;
};
std::vector<Dealt> dealt;
u32 next_id = 1;
std::mutex numbers_mutex;
std::vector<Number> published;  // Numbers(), under the mutex
std::atomic<bool> any_published{false};

void Publish(std::vector<Number>&& numbers) {
    std::lock_guard lock(numbers_mutex);
    published = std::move(numbers);
    any_published.store(!published.empty(), std::memory_order_relaxed);
}

// ── The health bars (monster_health_bar): only read, so the same in a room's quest as alone
std::mutex healths_mutex;
std::vector<Health> healths;  // Healths(), under the mutex
std::atomic<bool> any_healths{false};
std::atomic<int> language{-1};  // GameLanguage()

void PublishHealths(std::vector<Health>&& list) {
    if (list.empty() && !any_healths.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(healths_mutex);
    healths = std::move(list);
    any_healths.store(!healths.empty(), std::memory_order_relaxed);
}

}  // namespace

std::vector<Number> Numbers() {
    if (!any_published.load(std::memory_order_relaxed)) {
        return {};
    }
    std::lock_guard lock(numbers_mutex);
    const std::int64_t now = SteadyNs();
    std::vector<Number> out;
    for (const Number& n : published) {
        if (now - n.born_ns <= static_cast<std::int64_t>(kNumberSeconds * 1e9)) out.push_back(n);
    }
    return out;
}

std::vector<Health> Healths() {
    if (!any_healths.load(std::memory_order_relaxed)) {
        return {};
    }
    std::lock_guard lock(healths_mutex);
    return healths;
}

bool SetOption(const std::string& key, const std::string& value) {
    if (key == "monster_health_bar") {
        options.health_bar = IsOn(value);
        if (!options.health_bar) PublishHealths({});
    } else if (key == "monsters_on_map") {
        options.monsters_on_map = IsOn(value);
    } else if (key == "damage_numbers") {
        options.damage_numbers = IsOn(value);
        if (!options.damage_numbers) {
            dealt.clear();
            Publish({});
        }
    } else if (key == "hunt_trace") {
        options.trace = IsOn(value);
    } else {
        return false;
    }
    return true;
}

bool MonstersOnMap() {
    return options.monsters_on_map;
}

bool DamageNumbersOn() {
    return options.damage_numbers;
}

bool HealthBarOn() {
    return options.health_bar;
}

int GameLanguage() {
    return language.load(std::memory_order_relaxed);
}

void Reset() {
    options = Options{};
    language.store(-1, std::memory_order_relaxed);
    if (const char* v = std::getenv("MH4U_MONSTERS_ON_MAP")) options.monsters_on_map = IsOn(v);
    if (const char* v = std::getenv("MH4U_DAMAGE_NUMBERS")) options.damage_numbers = IsOn(v);
    if (const char* v = std::getenv("MH4N_HUNT_TRACE")) options.trace = IsOn(v);
    if (const char* v = std::getenv("MH4N_HUNT_COST")) options.cost = IsOn(v);
    seen.clear();
    dealt.clear();
    in_quest = false;
    frames = 0;
    for (CameraSeen& c : cameras) c = CameraSeen{};
    Publish({});
    PublishHealths({});
}

void SetLog(void (*log)(const std::string& line)) {
    log_line = log;
}

void Projection(RCtx* ctx, u32 camera, u32 matrix) {
    if (!options.damage_numbers && !options.trace) {
        return;
    }
    CameraSeen* slot = nullptr;
    for (CameraSeen& c : cameras) {
        if (c.camera == camera) {
            slot = &c;
            break;
        }
    }
    if (slot == nullptr) {  // (the oldest one's place)
        slot = &*std::min_element(std::begin(cameras), std::end(cameras),
                                  [](const CameraSeen& a, const CameraSeen& b) { return a.frame < b.frame; });
    }
    slot->camera = camera;
    slot->frame = frames;
    for (int k = 0; k < 16; ++k) slot->proj[k] = ReadF32(ctx, matrix + 4 * k);
}

/// hunt_trace (or MH4N_HUNT_COST=1): what Frame costs, a line every 3600 frames (the average and the longest, in
/// microseconds).
struct CostMeter {
    std::int64_t start = options.trace || options.cost ? SteadyNs() : 0;
    ~CostMeter() {
        if (start == 0) return;
        static std::int64_t total = 0, longest = 0;
        static u32 count = 0;
        const std::int64_t ns = SteadyNs() - start;
        total += ns, longest = std::max(longest, ns);
        if (++count == 3600) {
            Log(fmt::format("hunt aids: a frame's step took {:.2f} us on average, {:.2f} us at most, over {} frames",
                            total / 1e3 / count, longest / 1e3, count));
            total = longest = 0, count = 0;
        }
    }
};

void Frame(RCtx* ctx) {
    ++frames;
    const bool any = options.monsters_on_map || options.damage_numbers || options.health_bar || options.trace;
    if (!any && seen.empty() && !in_quest) {
        return;  // (off: nothing read)
    }
    const CostMeter meter;
    const u32 control = rd32(ctx, GAME_CONTROL);
    if (options.health_bar && HeapPointer(control)) {  // (the bars' names: the app reads them in this language)
        const int now_language = rd8(ctx, control + GAME_CONTROL_LANGUAGE);
        if (language.exchange(now_language, std::memory_order_relaxed) != now_language && options.trace) {
            Log(fmt::format("hunt aids: f{} the game's language {}", rd32(ctx, FRAME_COUNTER), now_language));
        }
    }
    const bool quest = HeapPointer(control) && rd8(ctx, control + GAME_CONTROL_MODE) == 1;
    const u32 enemies = quest ? rd32(ctx, SENEMY) : 0;
    if (!quest || !HeapPointer(enemies)) {
        if (in_quest) {  // (the quest is over: what was ours goes, nothing carries over to the next)
            in_quest = false;
            seen.clear();
            dealt.clear();
            Publish({});
            PublishHealths({});
        }
        return;
    }
    in_quest = true;
    const u32 area = rd32(ctx, control + GAME_CONTROL_AREA) & 0xff;
    const u32 players = rd32(ctx, SPLAYER);
    int hunters = 0;  // (the hunters in the quest: more than this console's own, a room's quest)
    for (u32 n = 0; n < 4 && HeapPointer(players); ++n) {
        hunters += HeapPointer(rd32(ctx, players + SPLAYER_UNITS + 4 * n)) ? 1 : 0;
    }
    const bool paint = options.monsters_on_map && hunters <= 1;
    const std::int64_t now = SteadyNs();
    std::vector<Health> bars;
    for (u32 i = 0; i < ENEMY_SLOT_COUNT; ++i) {
        const u32 monster = rd32(ctx, enemies + ENEMY_SLOTS + 4 * i);
        const u32 info = LargeMonsterInfo(ctx, monster);
        if (info == 0) {
            continue;
        }
        const u8 kind = rd8(ctx, info + INFO_KIND);
        const s32 health = static_cast<s32>(rd32(ctx, info + INFO_HEALTH));
        Seen* s = Find(monster);
        if (s != nullptr && (s->info != info || s->kind != kind)) {  // (another monster in the same memory)
            *s = Seen{};
            s->monster = monster, s->health = health;
            for (int k = 0; k < 3; ++k) s->hit[k] = ReadF32(ctx, monster + UNIT_HIT + 4 * k);
        }
        if (s == nullptr) {
            seen.push_back(Seen{monster, info, kind, health, false, frames});
            s = &seen.back();
            for (int k = 0; k < 3; ++k) s->hit[k] = ReadF32(ctx, monster + UNIT_HIT + 4 * k);  // (an older hit's place)
        }
        s->info = info, s->kind = kind, s->frame = frames;
        const bool dead = Dead(ctx, info);
        // the bars: every large monster alive (one in another area too: the app shows it dimmer)
        if (options.health_bar && !dead) {
            const s32 max = static_cast<s32>(rd32(ctx, info + INFO_HEALTH + 4));
            if (max > 0 && max < 1000000) {
                bars.push_back(Health{std::clamp<s32>(health, 0, max), max, rd8(ctx, info + INFO_AREA) == area, kind,
                                      monster});
            }
        }
        // the map: a paint timer kept on the monster while the option is on
        if (paint && !dead) {
            const float paint = ReadF32(ctx, info + INFO_PAINT);
            if (!(paint > PAINT_KEEP)) {     // (a timer of the player's paintball is left alone)
                if (!(paint >= PAINT_LOW)) WriteF32(ctx, info + INFO_PAINT, PAINT_KEEP);
                s->painted_by_us = true;
            } else {
                s->painted_by_us = false;
            }
        } else if (s->painted_by_us) {
            Unpaint(ctx, *s);
        }
        // the numbers: a drop of health since the last frame
        if (options.damage_numbers && health < s->health && s->health - health < 100000) {
            Dealt d{};
            float hit[3], center[3];
            for (int k = 0; k < 3; ++k) {
                hit[k] = ReadF32(ctx, monster + UNIT_HIT + 4 * k);
                center[k] = ReadF32(ctx, monster + UNIT_CENTER + 4 * k);
            }
            const bool placed = std::memcmp(hit, s->hit, sizeof hit) != 0 && (hit[0] != 0 || hit[1] != 0 || hit[2] != 0);
            std::memcpy(s->hit, hit, sizeof hit);
            std::memcpy(d.world, placed ? hit : center, sizeof d.world);
            d.world[1] += NUMBER_LIFT;
            d.number.value = s->health - std::max(health, 0);
            d.number.born_ns = now;
            d.number.id = next_id++;
            if (rd8(ctx, info + INFO_AREA) == area && std::isfinite(d.world[0] + d.world[1] + d.world[2])) {
                if (dealt.size() >= MAX_NUMBERS) dealt.erase(dealt.begin());
                dealt.push_back(d);
            }
            if (options.trace) {
                Log(fmt::format("hunt aids: f{} monster {:08X} health {} -> {} (max {}): a number of {} at the {}{}",
                                rd32(ctx, FRAME_COUNTER), monster, s->health, health,
                                static_cast<s32>(rd32(ctx, info + INFO_HEALTH + 4)),
                                d.number.value, placed ? "hit" : "body's middle",
                                rd8(ctx, info + INFO_AREA) == area ? "" : " (in another area: not shown)"));
            }
        }
        s->health = health;
    }
    // monsters gone (another area's left, a quest's end): forget them; with both options off, all of them (a timer of
    // ours went back to 0 just above: from the next frame on nothing is read)
    seen.erase(std::remove_if(seen.begin(), seen.end(), [](const Seen& s) { return s.frame != frames; }), seen.end());
    if (!any) {
        seen.clear(), dealt.clear(), in_quest = false;
        Publish({});
        PublishHealths({});
        return;
    }
    if (options.trace && options.health_bar) {  // (every frame: tests draw the bars on the runner's screenshots)
        // (the HUD's parts the app's bars keep clear of: the hunter's own bars, the area map, the target camera)
        const Hooks::GameState game = Hooks::CurrentGameState();
        std::string parts;
        for (const int k : {1, 4, 5}) {
            const Hooks::GameState::HudPart& p = game.hud[k];
            parts += fmt::format(" {} {} {:.3f},{:.3f},{:.3f},{:.3f}", Hooks::HudPartKey(k), p.shown, p.rect[0],
                                 p.rect[1], p.rect[2], p.rect[3]);
        }
        Log(fmt::format("hunt aids: f{} hud menu {}{}", rd32(ctx, FRAME_COUNTER), game.menu, parts));
        for (const Health& h : bars) {
            Log(fmt::format("hunt aids: f{} bar {:08X} kind {:02X} {}/{} {}", rd32(ctx, FRAME_COUNTER), h.id, h.kind,
                            h.health, h.max, h.here ? "here" : "away"));
        }
    }
    PublishHealths(std::move(bars));
    // the numbers on the screen, placed with this frame's camera
    if (options.damage_numbers || !dealt.empty()) {
        dealt.erase(std::remove_if(dealt.begin(), dealt.end(),
                                   [now](const Dealt& d) {
                                       return now - d.number.born_ns > static_cast<std::int64_t>(kNumberSeconds * 1e9);
                                   }),
                    dealt.end());
        const Camera camera = TopCamera(ctx);
        std::vector<Number> out;
        out.reserve(dealt.size());
        for (Dealt& d : dealt) {
            d.number.shown = camera.known && Project(camera, d.world, &d.number.x, &d.number.y);
            out.push_back(d.number);
            if (options.trace) {  // (every frame it lasts: tests draw it on the runner's screenshots, tools of out/)
                Log(fmt::format("hunt aids: f{} number {} {} at {:.4f},{:.4f} age {:.3f}{}", rd32(ctx, FRAME_COUNTER),
                                d.number.id, d.number.value, d.number.x, d.number.y, (now - d.number.born_ns) / 1e9,
                                d.number.shown ? "" : " (not on the picture)"));
            }
        }
        Publish(std::move(out));
    }
    if (options.trace && frames % 60 == 0) {
        const Camera camera = TopCamera(ctx);
        for (const Seen& s : seen) {
            const u32 info = s.info;
            const float p[3] = {ReadF32(ctx, s.monster + UNIT_CENTER), ReadF32(ctx, s.monster + UNIT_CENTER + 4),
                                ReadF32(ctx, s.monster + UNIT_CENTER + 8)};
            float fx = -1, fy = -1;
            const bool on = camera.known && Project(camera, p, &fx, &fy);
            Log(fmt::format("hunt aids: f{} monster {:08X} kind {:02X} area {} (hunter's {}, hunters {}) health {}/{} "
                            "paint {:.0f} "
                            "at {:.0f},{:.0f},{:.0f} screen {}{:.3f},{:.3f} camera {}",
                            rd32(ctx, FRAME_COUNTER), s.monster, s.kind, rd8(ctx, info + INFO_AREA), area, hunters,
                            s.health,
                            static_cast<s32>(rd32(ctx, info + INFO_HEALTH + 4)), ReadF32(ctx, info + INFO_PAINT),
                            p[0], p[1], p[2], on ? "" : "off ", fx, fy, camera.known ? "known" : "not known"));
        }
    }
}

}  // namespace Port::HuntAids
