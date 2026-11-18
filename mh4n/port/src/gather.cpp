// [Gathering] Gather everything in one press (gather_all, 1.0.1, the owner's idea; MHGU for Android has the same option,
// name and texts): one gather at a gathering point (herbs, mushrooms, honey, bugs, bones, ore...) or one carve gives
// everything the point had left at once, through the game's own steps for each one, instead of A again for each with
// its animation. Off: the game's way, one item a gather.
//
// The game (USA v1.3.0 addresses; the others through config/port_addresses.txt). The points are cPop (DTI 0x010b5380,
// 0x94 bytes), 128 of them inside sPop ([0x00fb8a1c], DTI 0x010b53a0) from +0x1c, the whole map's in use in a list from
// sPop+0x4a1c (+0x08 the next): +0x19 1 while it can be gathered, +0x28 its item table ({u8 weight, u8 count, u16
// item}..., weight 0xff ends it; a map point's in the quest's data, a carve's in the game's own), +0x2c its number (s16),
// +0x2e its kind (1 and 2 the map's points; 3 a carve, made by a corpse's script through 0x0092600c, one for each part,
// the body, a cut tail; 4 the camp's endless ones; 7 a quest's own), +0x2f its area of the map, +0x30 gathers left (u16;
// 0xffff endless), +0x32 sure gathers left (u16), +0x34 its place (x, y, z), +0x4c its reach, +0x50 its height and +0x54
// what gathers it (0 the hands, 1 a carve, 2 a pickaxe, 3 a bug net). A carve is a gather like the others.
// The hunter's gather and carve actions are one function, 0x00b566b8(hunter, type), mode 0's actions 0x4d, 0x4e, 0x51,
// 0x52, 0x53, 0x54, 0x94, 0x95 with the types 0 to 7 (0x00b12780; 0x96, type 9, takes a quest's object instead). At the
// animation's moment, for this console's hunter ([hunter+0xe30] its state; +9 its area of the map, +0x10 its place;
// [hunter+0xa29c] its item info, +0x5a4 the point's item table, +0x586 the tool in use), once:
//   0x005b95b0(sPop, area, &place, table, &rare): the hunter's point (the first of the list in that area, with that
//     table and gathers left, whose reach the hunter is in), counted as the game counts it: a sure gather first (+0x32),
//     else the chance that the point ends here (the hunter's gathering skills 0x3c, 0x3d, 0x3e or 0xf, 0x3f against a
//     roll of 32; 0xe), else one less; 1 a gather, 2 the last one, 0 none; endless points give 1 and stay;
//   0x005b9b78(sPop, table): the item rolled, its table entry;
//   the save's gather counts (0x00c2347c, and 0x00c260f0 for a rare one), and the first quests' steps (100, 101, 102 and
//     1251: holder+0xad20 one step on for their item, with the note 0x00be2898);
//   0x00b109a4(hunter, type, item, count): with a tool (types 2 and 3, a pickaxe and a bug net), 0x00b3c670 counts its
//     use and its chance to break, which takes it from the pouch with the game's message; the skills' extra one for
//     honey and some others; then the item into the pouch (0x00b8da78 -> 0x00b6d484) with its message: taken, or none
//     (it can't carry more of it), or the pouch full: the game's window to choose ([0x01083854]+4 set). A few items
//     (0x005e58e4's) set the state's +7: the hunter holds them up.
// Each of those is called only from there, so the save's gather count, (save + 0xf236), going up in a frame says this
// console's hunter gathered once in it.
//
// Here: Before() notes, while the local hunter is in one of those actions, the save's gather count, the pouch and the
// points; After(), when the count went up in the game's frame and the item went into the pouch, finds the point the game
// counted (its counts changed) and runs the same steps again on it, as the game would on the next gather, while it has
// gathers left: the same counting, skills, rolls, tool uses and breaks, messages and sounds. It stops when the point is
// spent, when the tool broke and the pouch has no other one of it, or when an item didn't go into the pouch: the pouch
// full (that one is in the game's window, the rest stays in the point) or it can't carry more of that item (the point
// gets that gather back, and the counts with it: nothing lost, nothing spent); and after an item the hunter holds up.
// Endless points are left as the game has them. Only the local hunter's gathers: the points and the pouch are each
// player's own, and what the game sends for a gather it sends here as it would for each press. Fishing is another step.
// It calls those functions only when each is one of the code library's, at its address in the library's version
// (Check, at Install): another version's library made by older tools gets them from the port's copy of its map
// (game_addr.h); without them the option does nothing.
#include "gather.h"

#include "game_addr.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>
#include <fmt/format.h>

extern "C" {
#include "recomp.h"
}

namespace Port::Gather {

namespace {

// (game addresses: GameAddr, port/src/game_addr.h and config/port_addresses.txt; USA v1.3.0's values)
GameAddr SPOP{0x00fb8a1c};              // sPop*: the gathering points
GameAddr SPLAYER{0x00fb83e8};           // sPlayer*
GameAddr GAME_CONTROL{0x00fb6b7c};      // sGameControl*
GameAddr SAVE{0x00fcf7ec};              // the save's object*
GameAddr QUEST_HOLDER{0x00fbeffc};      // the quest's holder*
GameAddr ITEM_WINDOW{0x01083854};       // the pouch-full window: +4 up
GameAddr FRAME_COUNTER{0x010571d4};     // u32: the game's frame counter (the trace's lines: the runner's fN)
GameAddr FN_POINT_COUNT{0x005b95b0};    // (sPop, area, &place, table; &rare): the hunter's point counted, 1 or 2 a gather
GameAddr FN_POINT_ROLL{0x005b9b78};     // (sPop, table): the table entry rolled
GameAddr FN_IS_LOCAL{0x00d04f6c};       // (hunter): nonzero for this console's hunter
GameAddr FN_COUNT_GATHER{0x00c2347c};   // (save): the save's gather count, one more
GameAddr FN_COUNT_RARE{0x00c260f0};     // (save): the rare ones', one more
GameAddr FN_QUEST_100{0x006b3c84};      // (holder): the first quests: nonzero in quest 100,
GameAddr FN_QUEST_101{0x006b3ca0};      //   101,
GameAddr FN_QUEST_102{0x006b3cd4};      //   102
GameAddr FN_QUEST_NOTE{0x00be2898};     // (holder + 0xe220, note, 1; s0 60): a quest's note
GameAddr FN_GIVE{0x00b109a4};           // (hunter, type, item, count): the item, with its tool and skills, into the pouch
GameAddr SENEMY{0x00f32004};            // sEnemy* (gather_test only): the monsters, 32 slots at +0x18
GameAddr FN_CARVE_POINT{0x0092600c};    // (monster, part, flags, 0) (gather_test only): a corpse's carve point, its number

constexpr u32 SPOP_LIST = 0x4a1c;       // cPop*: the points in use, +0x08 the next
constexpr u32 SPOP_FIRST = 0x1c, POINT_SIZE = 0x94, POINT_COUNT = 128;
constexpr u32 PT_NEXT = 0x08, PT_READY = 0x19, PT_TABLE = 0x28, PT_NUMBER = 0x2c, PT_KIND = 0x2e, PT_AREA = 0x2f,
              PT_LEFT = 0x30, PT_SURE = 0x32, PT_PLACE = 0x34, PT_REACH = 0x4c, PT_HEIGHT = 0x50, PT_ACTION = 0x54;
constexpr u16 LEFT_ENDLESS = 0xffff;
constexpr u32 HUNTER_STATE = 0xe30;     // the hunter's state*
constexpr u32 HUNTER_ITEMS = 0xa29c;    // the hunter's item info*
constexpr u32 STATE_HOLDS = 0x07;       // u8: 1 the hunter holds the item up (0x00b109a4)
constexpr u32 STATE_AREA = 0x09;        // u8: the hunter's area of the map
constexpr u32 STATE_PLACE = 0x10;       // the hunter's place (x, y, z)
constexpr u32 STATE_POUCH = 0x236;      // the pouch: 40 slots of (u16 item, u16 count) (0x002af87c)
constexpr u32 POUCH_SLOTS = 40;
constexpr u32 STATE_MODE = 0x371;       // u8: the action's mode (0 for these)
constexpr u32 STATE_ACTION = 0x372;     // u16: the action
constexpr u32 ITEMS_TOOL = 0x586;       // u16: the tool in use
constexpr u32 ITEMS_TABLE = 0x5a4;      // the point's item table
constexpr u32 SPLAYER_UNITS = 0xa98;    // uPlayer*[4], by player number
constexpr u32 GAME_CONTROL_PLAYER = 0x2f;     // s8: this console's player number
constexpr u32 GAME_CONTROL_NO_UNITS = 0x5a;   // u8: 1 where sPlayer has no units (the towns)
constexpr u32 SAVE_GATHERS = 0xf236;    // u8: the save's gather count (0x00c2347c), +1 the rare ones' (0x00c260f0)
constexpr u32 HOLDER_QUEST = 0xad50;    // u32: the quest's number
constexpr u32 HOLDER_STEP = 0xad20;     // u8: the first quests' step
constexpr u32 HOLDER_NOTES = 0xe220;
constexpr u32 WINDOW_UP = 0x04;
constexpr int MAX_EXTRA = 40;           // (a point has a handful: anything past this is not a point's)

// The gather actions (mode 0) and their types (0x00b12780: each calls 0x00b566b8 with its type)
constexpr std::pair<u16, u32> ACTIONS[] = {{0x4d, 0}, {0x4e, 1}, {0x51, 2}, {0x52, 3},
                                           {0x53, 4}, {0x54, 5}, {0x94, 6}, {0x95, 7}};

// The game's functions the rest of a point calls (FN_CARVE_POINT is the tests' only)
const GameAddr* const FUNCTIONS[] = {&FN_POINT_COUNT, &FN_POINT_ROLL, &FN_IS_LOCAL,  &FN_COUNT_GATHER, &FN_COUNT_RARE,
                                     &FN_QUEST_100,   &FN_QUEST_101,  &FN_QUEST_102, &FN_QUEST_NOTE,   &FN_GIVE};

bool gather_all = false;   // the option
bool usable = true;        // the code library has the game's functions it calls (Check); off, nothing is done
bool trace = false;        // gather_trace: a line for every gather the game counts and every item the rest gives
std::vector<std::string> tests;  // gather_test: what the next frame does (tests only)
u32 held_monster = 0;            // gather_test monster=@<hp>: the monster kept in front of the hunter,
int held_frames = 0;             //   for this many more frames
void (*log_line)(const std::string&) = nullptr;

struct Snapshot {
    bool valid = false;
    u32 hunter = 0, state = 0, save = 0, type = 0;
    u8 gathers = 0, window = 0;
    std::array<u32, POUCH_SLOTS> pouch{};          // (item | count << 16)
    std::vector<std::pair<u32, u32>> points;       // (point, left | sure << 16)
} before;

void Log(const std::string& line) {
    if (log_line) log_line(line);
}

bool HeapPointer(u32 p) {
    return p >= 0x08000000 && p < 0x10000000 && (p & 3) == 0;
}

/// An item table's place: a map's points' in the quest's data (the heap or the linear heap: 0x316b.... in a quest on this
/// runtime), a carve's in the game's own data (0x010009a6 an Aptonoth's), at any byte.
bool DataPointer(u32 p) {
    return (p >= 0x00100000 && p < 0x01120000) || (p >= 0x08000000 && p < 0x10000000) || (p >= 0x14000000 && p < 0x1c000000) ||
           (p >= 0x30000000 && p < 0x40000000);
}

float ReadF32(RCtx* ctx, u32 a) {
    const u32 v = rd32(ctx, a);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

/// Calls a game function with up to four arguments, the caller's registers and flags kept; returns its r0 (as
/// hooks.cpp's CallGame).
u32 CallGame(RCtx* ctx, u32 function, std::initializer_list<u32> args) {
    u32 regs[16];
    std::memcpy(regs, ctx->r, sizeof regs);
    const u32 flags[4] = {ctx->fn, ctx->fz, ctx->fc, ctx->fv};
    const u32 expect_next = ctx->expect_next;
    ctx->expect_next = 0;
    int i = 0;
    for (const u32 a : args) ctx->r[i++] = a;
    ctx->r[14] = function;  // (any address: the call ends when the function returns)
    rc_call(ctx, function);
    const u32 result = ctx->r[0];
    std::memcpy(ctx->r, regs, sizeof regs);
    ctx->fn = flags[0], ctx->fz = flags[1], ctx->fc = flags[2], ctx->fv = flags[3];
    ctx->expect_next = expect_next;
    return result;
}

/// This console's hunter in a quest (sPlayer's unit for its player number), or 0.
u32 LocalHunter(RCtx* ctx) {
    const u32 control = rd32(ctx, GAME_CONTROL);
    const u32 players = rd32(ctx, SPLAYER);
    if (!HeapPointer(control) || !HeapPointer(players) || rd8(ctx, control + GAME_CONTROL_NO_UNITS) == 1) return 0;
    const s8 number = static_cast<s8>(rd8(ctx, control + GAME_CONTROL_PLAYER));
    if (number < 0 || number > 3) return 0;
    const u32 hunter = rd32(ctx, players + SPLAYER_UNITS + 4 * static_cast<u32>(number));
    return HeapPointer(hunter) && HeapPointer(rd32(ctx, hunter + HUNTER_STATE)) ? hunter : 0;
}

/// The type of the gather action the hunter is in, or -1.
int GatherType(RCtx* ctx, u32 state) {
    if (rd8(ctx, state + STATE_MODE) != 0) return -1;
    const u16 action = static_cast<u16>(rd16(ctx, state + STATE_ACTION));
    for (const auto& [a, type] : ACTIONS)
        if (a == action) return static_cast<int>(type);
    return -1;
}

bool ToolType(u32 type) {
    return type == 2 || type == 3 || type == 8;  // (0x00b109a4's: a pickaxe or a bug net)
}

u32 PouchCount(RCtx* ctx, u32 state, u32 item) {
    u32 n = 0;
    for (u32 i = 0; i < POUCH_SLOTS; ++i)
        if (rd16(ctx, state + STATE_POUCH + 4 * i) == item) n += rd16(ctx, state + STATE_POUCH + 4 * i + 2);
    return n;
}

u32 PointCounts(RCtx* ctx, u32 point) {
    return rd16(ctx, point + PT_LEFT) | rd16(ctx, point + PT_SURE) << 16;
}

template <typename F>
void ForEachPoint(RCtx* ctx, F&& f) {
    const u32 spop = rd32(ctx, SPOP);
    if (!HeapPointer(spop)) return;
    u32 p = rd32(ctx, spop + SPOP_LIST);
    for (u32 n = 0; HeapPointer(p) && n < POINT_COUNT; ++n, p = rd32(ctx, p + PT_NEXT)) f(p);
}

/// 0x005b95b0 with its fifth argument, the rare flag's address, in a word of stack below the caller's.
u32 CountPoint(RCtx* ctx, u32 spop, u32 area, u32 place, u32 table, u8* rare) {
    const u32 sp = ctx->r[13];
    const u32 out = sp - 8;
    wr32(ctx, out, 0);
    wr32(ctx, sp - 16, out);
    ctx->r[13] = sp - 16;
    const u32 result = CallGame(ctx, FN_POINT_COUNT, {spop, area, place, table}) & 0xffff;
    ctx->r[13] = sp;
    *rare = static_cast<u8>(rd8(ctx, out));
    return result;
}

/// 0x00be2898(holder + 0xe220, note, 1) with s0 60, as the gather action calls it.
void QuestNote(RCtx* ctx, u32 holder, u32 note) {
    const float s0 = ctx->f.s[0];
    ctx->f.s[0] = 60.0f;
    CallGame(ctx, FN_QUEST_NOTE, {holder + HOLDER_NOTES, note, 1});
    ctx->f.s[0] = s0;
}

/// The first quests' steps for a gathered item, as the gather action takes them before it gives the item.
void QuestSteps(RCtx* ctx, u32 holder, u32 item) {
    if (!HeapPointer(holder)) return;
    const u32 step_at = holder + HOLDER_STEP;
    auto next = [&](u32 note) {
        QuestNote(ctx, holder, note);
        wr8(ctx, step_at, static_cast<u8>(rd8(ctx, step_at) + 1));
    };
    if (CallGame(ctx, FN_QUEST_100, {holder}) != 0) {
        if (item == 0x30f) QuestNote(ctx, holder, 0x28);
        return;
    }
    if (CallGame(ctx, FN_QUEST_101, {holder}) != 0 && rd8(ctx, step_at) == 0xa) {
        if (item == 0x2c) next(0x39);
        return;
    }
    if (CallGame(ctx, FN_QUEST_102, {holder}) != 0) {
        const s8 step = static_cast<s8>(rd8(ctx, step_at));
        if (step == 2 && item == 0xb1) next(0x41);
        else if (step == 4 && item == 0xbd) next(0x48);
        else if (step == 6 && item == 0xb0) next(0x45);
        return;
    }
    if (rd32(ctx, holder + HOLDER_QUEST) == 0x4e3 && rd8(ctx, step_at) == 4 && item == 0x2fb) next(0x6a);
}

std::string Describe(RCtx* ctx, u32 point) {
    const u32 left = rd16(ctx, point + PT_LEFT);
    return fmt::format("point {:08X} (number {} kind {} area {} gathered with {}, {} left, {} sure)", point,
                       static_cast<s16>(rd16(ctx, point + PT_NUMBER)), rd8(ctx, point + PT_KIND),
                       rd8(ctx, point + PT_AREA), rd8(ctx, point + PT_ACTION),
                       left == LEFT_ENDLESS ? std::string("endless") : std::to_string(left), rd16(ctx, point + PT_SURE));
}

// ── Tests (the runner's --set gather_test=...; never from the app): list the area's points, the pouch and the monsters
// (list), put a point where the hunter stands (pull=<n>, or pull=k<kind>; gathered with A as at its own place), set a
// point's counts (point=<n>/<left>/<sure>), a pouch slot (pouch=<slot>/<item>/<count>) or the nearest monster's health
// (monster=<hp>; monster=@<hp> also puts it in front of the hunter, for a carve), or make the nearest monster's carve
// point as its corpse's script does (carve).
/// gather_test monster=@<hp>: the monster in front of the hunter (its facing, hunter+0xa350: atan2(x, z) in 65536ths of a
/// turn), at its place (+0x40) and the place's copy (+0xd00), until it falls (a hit at 1 health) or 90 frames pass.
void HoldMonster(RCtx* ctx, u32 hunter, u32 state) {
    const u32 info = HeapPointer(held_monster) ? rd32(ctx, held_monster + 0xe28) : 0;
    if (!HeapPointer(info) || static_cast<s32>(rd32(ctx, info + 0x3e8)) <= 0 || --held_frames <= 0) {
        held_monster = 0, held_frames = 0;
        return;
    }
    const float a = static_cast<float>(rd16(ctx, hunter + 0xa350)) * 6.28318531f / 65536.0f;
    const float place[3] = {ReadF32(ctx, state + STATE_PLACE) + 120.0f * std::sin(a), ReadF32(ctx, state + STATE_PLACE + 4),
                            ReadF32(ctx, state + STATE_PLACE + 8) + 120.0f * std::cos(a)};
    const bool unit = std::fabs(ReadF32(ctx, held_monster + 0x40) - ReadF32(ctx, held_monster + 0xd00)) < 200.0f &&
                      std::fabs(ReadF32(ctx, held_monster + 0x48) - ReadF32(ctx, held_monster + 0xd08)) < 200.0f;
    const bool status = std::fabs(ReadF32(ctx, info + 0x198) - ReadF32(ctx, held_monster + 0xd00)) < 200.0f;
    for (u32 k = 0; k < 3; ++k) {
        u32 v;
        std::memcpy(&v, &place[k], 4);
        wr32(ctx, held_monster + 0xd00 + 4 * k, v);
        if (unit) wr32(ctx, held_monster + 0x40 + 4 * k, v);
        if (status) wr32(ctx, info + 0x198 + 4 * k, v);  // (the status block's place, where its carve goes)
    }
}

void RunTests(RCtx* ctx) {
    const u32 hunter = LocalHunter(ctx);
    const u32 state = hunter ? rd32(ctx, hunter + HUNTER_STATE) : 0;
    if (held_monster && state) HoldMonster(ctx, hunter, state);
    for (const std::string& t : tests) {
        if (!state) {
            Log("gather test: " + t + ": no hunter");
            continue;
        }
        const float hx = ReadF32(ctx, state + STATE_PLACE), hy = ReadF32(ctx, state + STATE_PLACE + 4),
                    hz = ReadF32(ctx, state + STATE_PLACE + 8);
        const u32 spop = rd32(ctx, SPOP);
        if (t == "list") {
            Log(fmt::format("gather test: f{} hunter at ({:.0f}, {:.0f}, {:.0f}), pouch:", rd32(ctx, FRAME_COUNTER), hx,
                            hy, hz));
            std::string pouch;
            for (u32 i = 0; i < POUCH_SLOTS; ++i) {
                const u32 slot = rd32(ctx, state + STATE_POUCH + 4 * i);
                if (slot & 0xffff) pouch += fmt::format(" {}:{:X}x{}", i, slot & 0xffff, slot >> 16);
            }
            Log("gather test:  " + pouch);
            ForEachPoint(ctx, [&](u32 p) {
                const float dx = ReadF32(ctx, p + PT_PLACE) - hx, dz = ReadF32(ctx, p + PT_PLACE + 8) - hz;
                std::string items;
                const u32 table = rd32(ctx, p + PT_TABLE);
                for (u32 e = 0; DataPointer(table) && e < 16 && rd8(ctx, table + 4 * e) != 0xff; ++e)
                    items += fmt::format(" {:X}x{}/{}", rd16(ctx, table + 4 * e + 2), rd8(ctx, table + 4 * e + 1),
                                         rd8(ctx, table + 4 * e));
                Log(fmt::format("gather test:  #{} {} ready {} reach {:.0f} height {:.0f}, {:.0f} away, {:.0f} up, "
                                "table {:08X}:{}",
                                (p - spop - SPOP_FIRST) / POINT_SIZE, Describe(ctx, p), rd8(ctx, p + PT_READY),
                                ReadF32(ctx, p + PT_REACH), ReadF32(ctx, p + PT_HEIGHT), std::sqrt(dx * dx + dz * dz),
                                ReadF32(ctx, p + PT_PLACE + 4) - hy, table, items));
            });
            const u32 enemies = rd32(ctx, SENEMY);
            for (u32 i = 0; HeapPointer(enemies) && i < 32; ++i) {
                const u32 m = rd32(ctx, enemies + 0x18 + 4 * i), info = HeapPointer(m) ? rd32(ctx, m + 0xe28) : 0;
                if (!HeapPointer(info)) continue;
                const float dx = ReadF32(ctx, m + 0xd00) - hx, dz = ReadF32(ctx, m + 0xd00 + 8) - hz;
                Log(fmt::format("gather test:  monster {:08X} kind {:02X} state {:02X} health {}/{} area {}, {:.0f} away "
                                "(+0x40 {:.0f} {:.0f} {:.0f}, +0xd00 {:.0f} {:.0f} {:.0f})", m,
                                rd8(ctx, info + 0x1c0), rd8(ctx, info + 0x1ca), static_cast<s32>(rd32(ctx, info + 0x3e8)),
                                static_cast<s32>(rd32(ctx, info + 0x3ec)), rd8(ctx, info + 0x14c),
                                std::sqrt(dx * dx + dz * dz), ReadF32(ctx, m + 0x40), ReadF32(ctx, m + 0x44),
                                ReadF32(ctx, m + 0x48), ReadF32(ctx, m + 0xd00), ReadF32(ctx, m + 0xd04), ReadF32(ctx, m + 0xd08)));
            }
        } else if (t.rfind("pull=", 0) == 0) {  // pull=<n>, or pull=k<kind>: the list's first of that kind with gathers left
            u32 n = static_cast<u32>(std::strtoul(t.c_str() + 5 + (t[5] == 'k'), nullptr, 0));
            if (t[5] == 'k') {
                const u32 kind = n;
                n = POINT_COUNT;
                ForEachPoint(ctx, [&](u32 p) {
                    if (n == POINT_COUNT && rd8(ctx, p + PT_KIND) == kind && rd16(ctx, p + PT_LEFT) != 0)
                        n = (p - spop - SPOP_FIRST) / POINT_SIZE;
                });
            }
            const u32 p = spop + SPOP_FIRST + POINT_SIZE * n;
            if (!HeapPointer(spop) || n >= POINT_COUNT || rd8(ctx, p + 4) == 0) {
                Log("gather test: " + t + ": no such point");
                continue;
            }
            for (u32 k = 0; k < 3; ++k) wr32(ctx, p + PT_PLACE + 4 * k, rd32(ctx, state + STATE_PLACE + 4 * k));
            const u32 from = rd8(ctx, p + PT_AREA);
            wr8(ctx, p + PT_AREA, static_cast<u8>(rd8(ctx, state + STATE_AREA)));  // (and in the hunter's area)
            Log(fmt::format("gather test: f{} {} put where the hunter stands (from area {})", rd32(ctx, FRAME_COUNTER),
                            Describe(ctx, p), from));
        } else if (t.rfind("monster=", 0) == 0) {
            const u32 enemies = rd32(ctx, SENEMY);
            u32 best = 0;
            float best_d = 1e30f;
            for (u32 i = 0; HeapPointer(enemies) && i < 32; ++i) {
                const u32 m = rd32(ctx, enemies + 0x18 + 4 * i);
                if (!HeapPointer(m) || !HeapPointer(rd32(ctx, m + 0xe28))) continue;
                const float dx = ReadF32(ctx, m + 0xd00) - hx, dz = ReadF32(ctx, m + 0xd00 + 8) - hz;
                const s32 health = static_cast<s32>(rd32(ctx, rd32(ctx, m + 0xe28) + 0x3e8));
                if (health > 0 && dx * dx + dz * dz < best_d) best = m, best_d = dx * dx + dz * dz;
            }
            if (!best) {
                Log("gather test: " + t + ": no monster");
                continue;
            }
            const u32 info = rd32(ctx, best + 0xe28);
            const bool here = t[8] == '@';  // monster=@<hp>: also put it in front of the hunter
            Log(fmt::format("gather test: f{} monster {:08X} (kind {:02X}) {:.0f} away: health {} -> {}{}",
                            rd32(ctx, FRAME_COUNTER), best, rd8(ctx, info + 0x1c0), std::sqrt(best_d),
                            static_cast<s32>(rd32(ctx, info + 0x3e8)), t.c_str() + 8 + here,
                            here ? ", put in front of the hunter" : ""));
            wr32(ctx, info + 0x3e8, static_cast<u32>(std::strtol(t.c_str() + 8 + here, nullptr, 0)));
            if (here) held_monster = best, held_frames = 90;
        } else if (t == "carve") {  // the nearest monster's carve point, as its corpse's script makes it (0x00931e9c)
            const u32 enemies = rd32(ctx, SENEMY);
            u32 best = 0;
            float best_d = 1e30f;
            for (u32 i = 0; HeapPointer(enemies) && i < 32; ++i) {
                const u32 m = rd32(ctx, enemies + 0x18 + 4 * i);
                if (!HeapPointer(m) || !HeapPointer(rd32(ctx, m + 0xe28))) continue;
                const float dx = ReadF32(ctx, m + 0xd00) - hx, dz = ReadF32(ctx, m + 0xd00 + 8) - hz;
                if (dx * dx + dz * dz < best_d) best = m, best_d = dx * dx + dz * dz;
            }
            const u32 made = best ? CallGame(ctx, FN_CARVE_POINT, {best, 0, 1, 0}) : 0xffffffffu;
            if (best && made != 0xffffffffu) wr32(ctx, rd32(ctx, best + 0xe28) + 0x7a48, made);
            Log(fmt::format("gather test: f{} carve point of monster {:08X}: {}", rd32(ctx, FRAME_COUNTER), best,
                            static_cast<s32>(made)));
        } else if (t.rfind("pouch=", 0) == 0 || t.rfind("point=", 0) == 0) {
            // pouch=<slot>/<item>/<count>: a pouch slot; point=<n>/<left>/<sure>: a point's counts (numbers 0x.. or ..)
            u32 v[3] = {};
            const char* s = t.c_str() + 6;
            for (int k = 0; k < 3 && *s; ++k) {
                char* end = nullptr;
                v[k] = static_cast<u32>(std::strtoul(s, &end, 0));
                s = *end ? end + 1 : end;
            }
            if (t[1] == 'o' && t[2] == 'u' && v[0] < POUCH_SLOTS) {
                wr16(ctx, state + STATE_POUCH + 4 * v[0], static_cast<u16>(v[1]));
                wr16(ctx, state + STATE_POUCH + 4 * v[0] + 2, static_cast<u16>(v[2]));
                Log(fmt::format("gather test: f{} pouch slot {}: {:X} x{}", rd32(ctx, FRAME_COUNTER), v[0], v[1], v[2]));
            } else if (t[1] == 'o' && t[2] == 'i' && HeapPointer(spop) && v[0] < POINT_COUNT) {
                const u32 p = spop + SPOP_FIRST + POINT_SIZE * v[0];
                wr16(ctx, p + PT_LEFT, static_cast<u16>(v[1]));
                wr16(ctx, p + PT_SURE, static_cast<u16>(v[2]));
                Log(fmt::format("gather test: f{} {}", rd32(ctx, FRAME_COUNTER), Describe(ctx, p)));
            } else {
                Log("gather test: " + t + ": not done");
            }
        } else {
            Log("gather test: " + t + ": not a test");
        }
    }
    tests.clear();
}

}  // namespace

bool SetOption(const std::string& key, const std::string& value) {
    auto on = [&] { return value == "on" || value == "1" || value == "true" || value == "yes"; };
    if (key == "gather_all") {
        gather_all = on();
    } else if (key == "gather_trace") {
        trace = on();
    } else if (key == "gather_test") {
        tests.push_back(value);
    } else {
        return false;
    }
    return true;
}

bool On() {
    return gather_all;
}

void Reset() {
    gather_all = trace = false;
    usable = true;
    tests.clear();
    held_monster = 0, held_frames = 0;
    before = Snapshot{};
}

std::string Check(const RCodeInfo& info) {
    // Each function must have this version's address (game_addr.h) and be one of the library's functions: a call to
    // anything else runs whatever code is there. (1.0.1 on EUR v1.1.0 libraries made by 1.0's tools, which have no
    // address for these: USA v1.3.0's 0x005b95b0 is, in EUR's code, the `blx r1` that ends the function before its
    // 0x005b95d8, and the game stopped at the first gather with the option on, issue #71.)
    usable = true;
    for (const GameAddr* f : FUNCTIONS) {
        const u32 address = *f;
        const RFuncEntry* first = info.table;
        const RFuncEntry* last = info.table ? info.table + info.count : nullptr;
        const RFuncEntry* it = std::lower_bound(first, last, address,
                                                [](const RFuncEntry& e, u32 a) { return e.addr < a; });
        if (!f->Known() || it == last || it->addr != address || it->mode != 0) {
            usable = false;
            return fmt::format("gather everything in one press: off with this code library: {} (USA v1.3.0's {:08X})",
                               f->Known() ? fmt::format("it has no function at {:08X}", address)
                                          : std::string("no address of this version for a game function"),
                               f->Usa());
        }
    }
    return {};
}

void SetLog(void (*log)(const std::string& line)) {
    log_line = log;
}

void Before(RCtx* ctx) {
    before.valid = false;
    if (!tests.empty() || held_monster) RunTests(ctx);
    if ((!gather_all && !trace) || !usable) return;
    const u32 hunter = LocalHunter(ctx);
    const u32 state = hunter ? rd32(ctx, hunter + HUNTER_STATE) : 0;
    const int type = state ? GatherType(ctx, state) : -1;
    const u32 save = rd32(ctx, SAVE);
    if (type < 0 || !HeapPointer(save)) return;  // (only while this console's hunter gathers)
    before.hunter = hunter, before.state = state, before.save = save, before.type = static_cast<u32>(type);
    before.gathers = static_cast<u8>(rd8(ctx, save + SAVE_GATHERS));
    before.window = static_cast<u8>(rd8(ctx, ITEM_WINDOW + WINDOW_UP));
    for (u32 i = 0; i < POUCH_SLOTS; ++i) before.pouch[i] = rd32(ctx, state + STATE_POUCH + 4 * i);
    before.points.clear();
    ForEachPoint(ctx, [&](u32 p) { before.points.emplace_back(p, PointCounts(ctx, p)); });
    before.valid = true;
}

void After(RCtx* ctx) {
    if (!before.valid) return;
    before.valid = false;
    const u32 hunter = before.hunter, state = before.state, save = before.save, type = before.type;
    if (LocalHunter(ctx) != hunter || rd32(ctx, hunter + HUNTER_STATE) != state || rd32(ctx, SAVE) != save ||
        rd8(ctx, save + SAVE_GATHERS) == before.gathers)
        return;  // (no gather of this console's hunter in this frame)
    // The point the game counted: its counts changed (an endless point's don't)
    u32 point = 0;
    for (const auto& [p, counts] : before.points)
        if (PointCounts(ctx, p) != counts) point = p;
    // What became of the game's own item
    bool taken = false;
    for (u32 i = 0; i < POUCH_SLOTS; ++i) {
        const u32 now = rd32(ctx, state + STATE_POUCH + 4 * i), was = before.pouch[i];
        if ((now & 0xffff) != 0 && (now >> 16) > 0 && ((now & 0xffff) != (was & 0xffff) || (now >> 16) > (was >> 16)))
            taken = true;
    }
    const bool window = rd8(ctx, ITEM_WINDOW + WINDOW_UP) != 0 && before.window == 0;
    const bool holds = rd8(ctx, state + STATE_HOLDS) != 0;
    if (trace)
        Log(fmt::format("gather: f{} the game's gather (type {}): {}, {}", rd32(ctx, FRAME_COUNTER), type,
                        point ? Describe(ctx, point) : std::string("an endless point"),
                        window ? "the pouch is full (the game's window)" : taken ? "the item went into the pouch"
                                                                                  : "the item didn't go into the pouch"));
    if (!gather_all || !point || !taken || window || holds) {
        if (gather_all && point && rd16(ctx, point + PT_LEFT) != 0 && !window && (!taken || holds))
            Log(fmt::format("gather: the rest of the point stays: {}", holds ? "the hunter holds the item up"
                                                                             : "the item didn't go into the pouch"));
        return;
    }
    // The rest of the point, through the game's steps for each gather
    const u32 spop = rd32(ctx, SPOP), items = rd32(ctx, hunter + HUNTER_ITEMS);
    const u32 table = HeapPointer(items) ? rd32(ctx, items + ITEMS_TABLE) : 0;
    if (!HeapPointer(spop) || !DataPointer(table) || rd32(ctx, point + PT_TABLE) != table) {
        Log("gather: the point counted isn't the hunter's: left as it is");
        return;
    }
    const u32 area = rd8(ctx, state + STATE_AREA), place = state + STATE_PLACE;
    const u32 holder = rd32(ctx, QUEST_HOLDER);
    std::string why = ": the point has a lot left (the rest stays)", gives;
    int more = 0;
    for (int i = 0; i < MAX_EXTRA; ++i) {
        const u32 left = rd16(ctx, point + PT_LEFT), sure = rd16(ctx, point + PT_SURE);
        if (left == 0) {
            why = ": the point is spent";
            break;
        }
        if (ToolType(type) && PouchCount(ctx, state, rd16(ctx, items + ITEMS_TOOL)) == 0) {
            why = ": no tool left";
            break;
        }
        if (rd8(ctx, ITEM_WINDOW + WINDOW_UP) != 0) {
            why = ": the pouch window is up";
            break;
        }
        const u8 gathers = static_cast<u8>(rd8(ctx, save + SAVE_GATHERS)),
                 rares = static_cast<u8>(rd8(ctx, save + SAVE_GATHERS + 1));
        const u8 step = HeapPointer(holder) ? static_cast<u8>(rd8(ctx, holder + HOLDER_STEP)) : 0;
        auto give_back = [&] {  // (that gather wasn't one: the point and the counts as they were)
            wr16(ctx, point + PT_LEFT, static_cast<u16>(left));
            wr16(ctx, point + PT_SURE, static_cast<u16>(sure));
            wr8(ctx, save + SAVE_GATHERS, gathers);
            wr8(ctx, save + SAVE_GATHERS + 1, rares);
            if (HeapPointer(holder)) wr8(ctx, holder + HOLDER_STEP, step);
        };
        u8 rare = 0;
        const u32 counted = CountPoint(ctx, spop, area, place, table, &rare);
        if (counted != 1 && counted != 2) {
            why = ": the point gave nothing";
            break;
        }
        const u32 entry = CallGame(ctx, FN_POINT_ROLL, {spop, table});
        if (!DataPointer(entry)) {
            give_back();
            why = ": nothing rolled";
            break;
        }
        const u32 item = rd16(ctx, entry + 2), count = rd8(ctx, entry + 1);
        if (CallGame(ctx, FN_IS_LOCAL, {hunter}) != 0) {
            CallGame(ctx, FN_COUNT_GATHER, {save});
            if (rare == 1) CallGame(ctx, FN_COUNT_RARE, {save});
            QuestSteps(ctx, holder, item);
        }
        const u32 had = item ? PouchCount(ctx, state, item) : 0;
        CallGame(ctx, FN_GIVE, {hunter, type, item, count});
        const u32 has = item ? PouchCount(ctx, state, item) : 0;
        if (trace && gives.size() < 400) gives += fmt::format(" {:X}x{}{}", item, count, has > had ? "" : "(no)");
        if (item == 0) {
            why = ": a gather gave nothing";
            break;
        }
        if (rd8(ctx, ITEM_WINDOW + WINDOW_UP) != 0) {
            ++more;
            why = ": the pouch is full (the game's window; the point keeps the rest)";
            break;
        }
        if (has <= had) {
            give_back();
            why = ": it can't carry more of an item (the point keeps the rest)";
            break;
        }
        ++more;
        if (rd8(ctx, state + STATE_HOLDS) != 0) {
            why = ": the hunter holds an item up";
            break;
        }
    }
    Log(fmt::format("gather: {} more from the point{}{}{}{}", more, why, ToolType(type) ? " (with a tool)" : "",
                    gives.empty() ? "" : "; gives:", gives));
}

}  // namespace Port::Gather
