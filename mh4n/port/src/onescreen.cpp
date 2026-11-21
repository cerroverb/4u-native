// One screen (single_screen in mh4u.ini): the game's touch-screen menus drawn on the top screen, by the game.
//
// The game draws both screens with one GUI manager (sMhLayout's uLayout): each root group of a layout carries the
// screen it is drawn on (bits 14-17 of group + 0x44: 0 the top one, 7 the touch one), and the layout pass of each
// screen draws the groups of its own. So a group of a touch-screen menu sent to the top screen is drawn there, at the
// game's own resolution, by the game's own code; this file only says where (docs/research/onescreen.md):
//
//  * once a frame, before the game's frame (Frame), it walks the manager's groups, finds the screen of Heavenfall's
//    arrangement in force from the groups shown (port/src/onescreen_table.h, made by tools/onescreen_table.py from
//    config/onescreen.json and the layouts of the player's game), and writes each placed group's screen (and its draw
//    priority); what it changes it keeps a note of, and gives back as the game had it when the group is no longer
//    placed or the option goes off;
//  * as each group is drawn on the top screen (ObjectConstants, from nLayout::objectConstants), a placed group gets a
//    copy of the layout pass's constants whose projection scales and moves it: the group's every pane, animated or set
//    by the game's code, follows, and nothing of the game's own state (positions, layouts, files) changes.
//
// Nothing is read from or written to the game's files: the layouts are the player's own, in any language, as the game
// loads them. The arrangement is Heavenfall's (popzol and BlitzWOLF, GPL-3.0), without its gameplay changes.
#include "onescreen.h"

#include "game_addr.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <fmt/format.h>

extern "C" {
#include "recomp.h"
}

namespace Port::OneScreen {

namespace Table {
struct Name {
    u32 hash;
    u16 block;
};
struct Place {
    u8 flags;
    s16 priority;  // set, or added to the game's (a negative one sends a group back)
    float scale;
    float tx[2];  // by the group's own screen (0 the top one, 1 the touch one)
    float ty;
};
struct Screen {
    u8 channel;
    u16 all_first, all_count, any_first, any_count, none_first, none_count, entry_first, entry_count;
};
struct Entry {
    u16 block;
    Place place;
};
struct Hide {
    u32 group, pane;  // name hashes
};
}  // namespace Table
}  // namespace Port::OneScreen

#include "onescreen_table.h"

namespace Port::OneScreen {

namespace {

using Table::Place;

// Place flags (tools/onescreen_table.py)
constexpr u8 F_TOP = 1, F_HIDDEN = 2, F_KEEP = 4, F_PRIORITY_SET = 8, F_PRIORITY_ADD = 16, F_COMPOSE = 32, F_BOTTOM = 64;
constexpr int kChannels = 3;
constexpr u32 PSEUDO_PROMPT = 1, PSEUDO_QUEST = 2;  // kNames' pseudo names: a yes / no prompt is open, a quest's scene

// (game addresses: GameAddr, port/src/game_addr.h and config/port_addresses.txt; USA v1.3.0's values)
GameAddr SMHLAYOUT_INSTANCE{0x01057534};        // sMhLayout*
constexpr u32 SMHLAYOUT_MAIN = 0x100;           // uLayout*: the one that holds every loaded layout
constexpr u32 ULAYOUT_GROUPS = 0x154;           // group*[]
constexpr u32 ULAYOUT_GROUP_COUNT = 0x160;
constexpr u32 GROUP_ORDER = 0x18;               // bits 19-26 the draw priority
constexpr u32 GROUP_FLAGS = 0x44;               // 0x400 shown, bits 14-17 the screen (view)
constexpr u32 GROUP_SHOWN = 0x400;
constexpr u32 VIEW_SHIFT = 14, VIEW_MASK = 0xFu << VIEW_SHIFT;
constexpr u32 VIEW_TOP = 0, VIEW_TOUCH = 7, VIEW_NONE = 15;  // (15: a view no camera draws)
constexpr u32 PRIORITY_SHIFT = 19, PRIORITY_MASK = 0xFFu << PRIORITY_SHIFT;
GameAddr FN_GROUP_CHANGED{0x00ae655c};          // (group): the draw list and its caches follow a new priority
// The yes / no box: ui006's owner (static), its button slots' kinds (u8, two apart; 3 "Yes", 6 "No") and whether its
// groups are bound (Heavenfall's single_screen_prompt.s, FUN_002921cc)
GameAddr PROMPT_OWNER{0x0109cdc8};
constexpr u32 PROMPT_KIND0 = 0x1f8, PROMPT_KIND1 = 0x1fa, PROMPT_BOUND = 0x21f;
GameAddr GAME_CONTROL{0x00fb6b7c};              // sGameControl*
constexpr u32 GAME_CONTROL_MODE = 0x2b;           // u8: 1 a quest, 5 its results
// The layout pass's constants (sLayout::beginViewport 0x00af3d60 makes them, cDraw + 0x824 points at them): its view
// and projection matrices, column by column; the projection's first two rows map layout units to the rotated target's
// x (the screen's height) and y (its width). The draw's constant room: cDraw + 0x14 the next free byte, + 0x18 the end.
constexpr u32 CDRAW_SHADER = 0x08, CDRAW_ROOM = 0x14, CDRAW_ROOM_END = 0x18, CDRAW_VIEWPORT = 0x824,
              CDRAW_DIRTY = 0x188;
// (draw, size): a new room for the draw's constants when its own has less than size bytes left, from the game's frame
// allocator (nLayout::objectConstants calls it so for the object's own constants before it takes them)
GameAddr FN_ROOM_MORE{0x005deb68};
constexpr u32 SHADER_VIEWPORT_LAYOUT = 0x614;   // the shader's description of that block: its size in words at + 0x14
constexpr u32 PROJ = 16;                        // the projection's first float in the block
// The draw's render state (0x005e560c copies cDraw + 0x120 .. + 0x15f into the draw's packet when + 0x128 has bit 3,
// and clears it): + 0x12c the viewport, + 0x13c the scissor, each (x0, y0, x1, y1) in the rotated target's pixels
// (x the screen's height, y its width, from its right edge). uLayout::draw's 0x00b048b8 sets the scissor to the
// viewport at the start of each pass.
constexpr u32 CDRAW_STATE = 0x128, CDRAW_STATE_SCISSOR = 8, CDRAW_VIEWPORT_RECT = 0x12c, CDRAW_SCISSOR = 0x13c;
constexpr u32 CONSOLE_ROWS = 400;               // the console's top screen: 400 pixels wide (the target's rows)
// A group's panes (Heavenfall's game_gui.h): + 0x08 the group's first; a pane's + 0x00 name hash, + 0x08 its kind's data,
// + 0x0C its draw node (+ 0x12 = 3 redraws it), + 0x10 its kind (0 sprite, 1 null, 2 text), + 0x14 the next, + 0x20 its
// first child. Shown: a sprite's data + 0x58 bit 7, a null's + 0x24 bit 0, a text's + 0x5C bit 28.
constexpr u32 GROUP_FIRST_PANE = 0x08, PANE_DATA = 0x08, PANE_NODE = 0x0c, PANE_KIND = 0x10, PANE_NEXT = 0x14,
              PANE_CHILD = 0x20;
GameAddr FN_PANE_REDRAW{0x00ae6b78};            // (group, pane): a pane with no draw node redrawn

float F(RCtx* ctx, u32 a) {
    const u32 v = rd32(ctx, a);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
void WF(RCtx* ctx, u32 a, float f) {
    u32 v;
    std::memcpy(&v, &f, 4);
    wr32(ctx, a, v);
}

/// A game function, called from the hook's context (as hooks.cpp's CallGame).
u32 CallGame(RCtx* ctx, u32 function, u32 arg, u32 arg1 = 0) {
    u32 regs[16];
    std::copy(std::begin(ctx->r), std::end(ctx->r), regs);
    const u32 flags[4] = {ctx->fn, ctx->fz, ctx->fc, ctx->fv};
    const u32 expect_next = ctx->expect_next;
    ctx->expect_next = 0;
    ctx->r[0] = arg;
    ctx->r[1] = arg1;
    ctx->r[14] = function;
    rc_call(ctx, function);
    const u32 result = ctx->r[0];
    std::copy(std::begin(regs), std::end(regs), ctx->r);
    ctx->fn = flags[0];
    ctx->fz = flags[1];
    ctx->fc = flags[2];
    ctx->fv = flags[3];
    ctx->expect_next = expect_next;
    return result;
}

/// What this file changed of a group: its screen and priority as the game last had them, and what was written.
struct Note {
    u32 hash = 0;  // the group's name: another layout's group at the same address starts a note of its own
    u32 game_view = 0, written_view = 0;
    u32 game_priority = 0, written_priority = 0;
    bool view = false, priority = false;
};
std::unordered_map<u32, Note> notes;  // group address -> note

/// The move of a group drawn on the top screen this frame.
struct Move {
    float scale, tx, ty;
};
std::unordered_map<u32, Move> moves;  // group address -> move (this frame's)

/// The layout pass's constants a moved group was given (ObjectConstants), to be given back for the next group.
struct Swap {
    u32 draw = 0, game = 0, ours = 0;
} swap;

/// This frame's copies of the pass's constants, one per (draw, pass constants, move): the groups that share a place
/// (a layout's groups, a frame's, ui006's) share one copy. Each copy takes 0xa0 bytes of the draw's constant room,
/// which the game fills with its own; with a copy a group, the room ran out in a full menu (the quest counter with its
/// yes / no box: seen on the PC runner and the phones) and the groups after were drawn where the game puts them
/// (unscaled, unmoved: the quest card at the game's size under the yes / no plates).
struct Copy {
    u32 draw, game, end;  // (end: the room's end when it was made; another room, another copy)
    float scale, tx, ty;
    u32 at, check;        // where it is, and the xor of its words (still ours: nothing wrote over it)
};
std::vector<Copy> copies;  // (this frame's)

/// In a picture wider than the console's screen, the groups placed on the top screen this frame: each is drawn with the
/// scissor cut to the console's 400 pixels (the game parks some panels just off the screen it made them for, and a
/// wider picture showed them at its sides), and the next group gets the game's scissor back.
std::unordered_set<u32> cuts;  // group addresses (this frame's)
struct ScissorSwap {
    u32 draw = 0;
    u32 game[4] = {}, ours[4] = {};
    bool on = false;
} scissor;

bool was_on = false;
u32 frames_on = 0;
std::string last_screens;
bool cut_logged = false;  // (the first cut goes to the log, from the next Frame)
std::string cut_line;
std::unordered_set<u32> unmoved;  // groups once drawn unmoved (for the log)
std::string unmoved_line;

int NameIndex(u32 hash) {
    const auto* begin = std::begin(Table::kNames);
    const auto* end = std::end(Table::kNames);
    const auto* it = std::lower_bound(begin, end, hash, [](const Table::Name& n, u32 h) { return n.hash < h; });
    return it != end && it->hash == hash ? static_cast<int>(it - begin) : -1;
}

bool Has(const std::vector<bool>& shown, u16 first, u16 count, bool all) {
    for (u16 i = 0; i < count; ++i) {
        const bool on = shown[Table::kTerms[first + i]];
        if (all && !on) return false;
        if (!all && on) return true;
    }
    return all;
}

/// `entry` on top of `base`: the entry's map after the base's (a frame or a shift of a place already placed).
Place Compose(const Place& entry, const Place& base) {
    Place p = base;
    if (entry.flags & (F_TOP | F_HIDDEN | F_BOTTOM)) {
        p.flags = static_cast<u8>((p.flags & ~(F_TOP | F_HIDDEN | F_BOTTOM | F_KEEP)) | (entry.flags & (F_TOP | F_HIDDEN | F_BOTTOM)));
    }
    if (entry.flags & (F_PRIORITY_SET | F_PRIORITY_ADD)) {
        p.flags = static_cast<u8>((p.flags & ~(F_PRIORITY_SET | F_PRIORITY_ADD)) |
                                  (entry.flags & (F_PRIORITY_SET | F_PRIORITY_ADD)));
        p.priority = entry.priority;
    }
    p.scale = entry.scale * base.scale;
    for (int own = 0; own < 2; ++own) p.tx[own] = entry.scale * base.tx[own] + entry.tx[own];
    p.ty = entry.scale * base.ty + entry.ty;
    return p;
}

/// Gives back `group`'s screen as the game last had it, if it is still what was written.
void GiveBackView(RCtx* ctx, u32 group, Note& note) {
    if (!note.view) return;
    const u32 flags = rd32(ctx, group + GROUP_FLAGS);
    if ((flags & VIEW_MASK) == note.written_view << VIEW_SHIFT) {
        wr32(ctx, group + GROUP_FLAGS, (flags & ~VIEW_MASK) | note.game_view << VIEW_SHIFT);
        wr8(ctx, group + 0x42, 3);
    }
    note.view = false;
}

/// The same for its draw priority.
void GiveBackPriority(RCtx* ctx, u32 group, Note& note) {
    if (!note.priority) return;
    const u32 order = rd32(ctx, group + GROUP_ORDER);
    if ((order & PRIORITY_MASK) >> PRIORITY_SHIFT == note.written_priority) {
        wr32(ctx, group + GROUP_ORDER, (order & ~PRIORITY_MASK) | note.game_priority << PRIORITY_SHIFT);
        CallGame(ctx, FN_GROUP_CHANGED, group);
    }
    note.priority = false;
}

/// A pane hidden while its group is placed (kHides), to be shown again after.
struct Hidden {
    u32 group = 0, hash = 0;
    bool seen = false;  // its group was walked this frame
    bool kept = false;  // and it is still to be hidden
};
std::unordered_map<u32, Hidden> hidden;  // pane address -> note

/// A pane's "shown" bit: where it is in its data, and the bit (0: a kind that has none).
u32 ShownBit(RCtx* ctx, u32 pane, u32* at) {
    const u32 data = rd32(ctx, pane + PANE_DATA);
    if (data == 0) return 0;
    switch (rd8(ctx, pane + PANE_KIND)) {
    case 0: *at = data + 0x58; return 0x80;        // a sprite
    case 1: *at = data + 0x24; return 0x1;         // a null
    case 2: *at = data + 0x5c; return 1u << 28;    // a text
    default: return 0;
    }
}

void Redraw(RCtx* ctx, u32 group, u32 pane) {
    if (const u32 node = rd32(ctx, pane + PANE_NODE)) {
        wr8(ctx, node + 0x12, 3);
    } else {
        CallGame(ctx, FN_PANE_REDRAW, group, pane);
    }
}

void ShowAgain(RCtx* ctx, u32 pane, const Hidden& h) {
    u32 at = 0;
    if (rd32(ctx, pane) != h.hash) return;
    if (const u32 bit = ShownBit(ctx, pane, &at); bit != 0 && !(rd32(ctx, at) & bit)) {
        wr32(ctx, at, rd32(ctx, at) | bit);
        Redraw(ctx, h.group, pane);
    }
}

/// The panes of `group` the table hides while it is placed: hidden (each frame: the game may show one again).
void HidePanes(RCtx* ctx, u32 group) {
    const u32 hash = rd32(ctx, group);
    const auto* first = std::lower_bound(std::begin(Table::kHides), std::end(Table::kHides), hash,
                                         [](const Table::Hide& h, u32 g) { return h.group < g; });
    if (first == std::end(Table::kHides) || first->group != hash) return;
    u32 stack[8];
    int depth = 0;
    u32 pane = rd32(ctx, group + GROUP_FIRST_PANE);
    for (int walked = 0; pane != 0 && walked < 4096; ++walked) {
        const u32 name = rd32(ctx, pane);
        for (const auto* h = first; h != std::end(Table::kHides) && h->group == hash; ++h) {
            if (h->pane != name) continue;
            u32 at = 0;
            const u32 bit = ShownBit(ctx, pane, &at);
            if (bit == 0) break;
            Hidden& note = hidden[pane];
            note.group = group, note.hash = name, note.seen = note.kept = true;
            if (rd32(ctx, at) & bit) {
                wr32(ctx, at, rd32(ctx, at) & ~bit);
                Redraw(ctx, group, pane);
            }
            break;
        }
        // depth first: its children, then the next pane, then the next of a parent
        if (const u32 child = rd32(ctx, pane + PANE_CHILD); child != 0 && depth < 8) {
            stack[depth++] = rd32(ctx, pane + PANE_NEXT);
            pane = child;
            continue;
        }
        pane = rd32(ctx, pane + PANE_NEXT);
        while (pane == 0 && depth > 0) pane = stack[--depth];
    }
}

/// Every group back as the game had it (the option went off).
void GiveAllBack(RCtx* ctx) {
    const u32 layout = rd32(ctx, SMHLAYOUT_INSTANCE);
    const u32 main = layout ? rd32(ctx, layout + SMHLAYOUT_MAIN) : 0;
    const u32 list = main ? rd32(ctx, main + ULAYOUT_GROUPS) : 0;
    const u32 count = main ? std::min<u32>(rd32(ctx, main + ULAYOUT_GROUP_COUNT), 0x2000) : 0;
    for (u32 i = 0; list != 0 && i < count; ++i) {
        const u32 group = rd32(ctx, list + 4 * i);
        auto it = notes.find(group);
        if (it != notes.end()) {
            GiveBackView(ctx, group, it->second);
            GiveBackPriority(ctx, group, it->second);
        }
        for (const auto& [pane, h] : hidden) {
            if (h.group == group) ShowAgain(ctx, pane, h);
        }
    }
    notes.clear();
    moves.clear();
    cuts.clear();
    copies.clear();
    hidden.clear();
}

/// The scissor of the draw's next group: the console's 400 pixels in the middle of a wider picture (`cut`), or the
/// game's own back after a group that had them. What the game set in between (a list's own scissor) is left as it is.
void Cut(RCtx* ctx, u32 draw, bool cut) {
    u32 now[4];
    for (u32 i = 0; i < 4; ++i) now[i] = rd32(ctx, draw + CDRAW_SCISSOR + 4 * i);
    const bool ours = scissor.on && scissor.draw == draw && std::equal(now, now + 4, scissor.ours);
    if (!cut) {
        if (ours) {
            for (u32 i = 0; i < 4; ++i) wr32(ctx, draw + CDRAW_SCISSOR + 4 * i, scissor.game[i]);
            wr32(ctx, draw + CDRAW_STATE, rd32(ctx, draw + CDRAW_STATE) | CDRAW_STATE_SCISSOR);
        }
        if (scissor.draw == draw) scissor.on = false;
        return;
    }
    if (!ours) std::copy(now, now + 4, scissor.game);  // (the game's: kept while the next groups are cut too)
    const u32* game = scissor.game;
    // the console's rows: centred in the viewport's (the picture's width), within the game's scissor
    const u32 v0 = rd32(ctx, draw + CDRAW_VIEWPORT_RECT + 4), v1 = rd32(ctx, draw + CDRAW_VIEWPORT_RECT + 12);
    if (v1 <= v0 + CONSOLE_ROWS || v1 > 0x1000) {
        scissor.on = false;
        return;
    }
    const u32 first = v0 + (v1 - v0 - CONSOLE_ROWS) / 2;
    const u32 lo = std::max(game[1], first), hi = std::max(lo, std::min(game[3], first + CONSOLE_ROWS));
    const u32 want[4] = {game[0], lo, game[2], hi};
    scissor.draw = draw;
    scissor.on = true;
    std::copy(want, want + 4, scissor.ours);
    if (!std::equal(now, now + 4, want)) {
        for (u32 i = 0; i < 4; ++i) wr32(ctx, draw + CDRAW_SCISSOR + 4 * i, want[i]);
        wr32(ctx, draw + CDRAW_STATE, rd32(ctx, draw + CDRAW_STATE) | CDRAW_STATE_SCISSOR);
    }
    if (!cut_logged) {
        cut_logged = true;
        cut_line = fmt::format("one screen: the menus cut to the console's {} pixels of a {}-pixel picture",
                               CONSOLE_ROWS, v1 - v0);
    }
}

}  // namespace

std::string Frame(RCtx* ctx, bool on, bool wide) {
    copies.clear();  // (a new frame's: ScaleObject makes them with the mode off too)
    if (!on) {
        if (was_on) {
            GiveAllBack(ctx);
            was_on = false;
            return "one screen: off, the touch screen's menus back on it";
        }
        return {};
    }
    const u32 layout = rd32(ctx, SMHLAYOUT_INSTANCE);
    const u32 main = layout ? rd32(ctx, layout + SMHLAYOUT_MAIN) : 0;
    const u32 list = main ? rd32(ctx, main + ULAYOUT_GROUPS) : 0;
    const u32 count = main ? rd32(ctx, main + ULAYOUT_GROUP_COUNT) : 0;
    moves.clear();
    cuts.clear();
    copies.clear();
    if (list == 0 || count == 0 || count > 0x2000) return {};
    std::string line;
    if (!was_on) {
        was_on = true;
        line = fmt::format("one screen: on ({} groups placed by {} rules)", Table::kBlockCount, std::size(Table::kScreens));
    }
    ++frames_on;

    // the names shown, and the pseudo names
    std::vector<bool> shown(Table::kNameCount, false);
    std::vector<std::pair<u32, int>> placed;  // (group, block)
    placed.reserve(256);
    for (u32 i = 0; i < count; ++i) {
        const u32 group = rd32(ctx, list + 4 * i);
        if (group == 0) continue;
        const int n = NameIndex(rd32(ctx, group));
        if (n < 0) continue;
        if (rd32(ctx, group + GROUP_FLAGS) & GROUP_SHOWN) shown[n] = true;
        if (Table::kNames[n].block != 0xFFFF) placed.emplace_back(group, Table::kNames[n].block);
    }
    if (const int n = NameIndex(PSEUDO_PROMPT); n >= 0) {
        shown[n] = rd8(ctx, PROMPT_OWNER + PROMPT_BOUND) != 0 && rd8(ctx, PROMPT_OWNER + PROMPT_KIND0) == 3 &&
                   rd8(ctx, PROMPT_OWNER + PROMPT_KIND1) == 6;
    }
    // (MH4N_ONESCREEN_TRACE=1: the log says when ui006's button kinds or their binding change, for the PC runner)
    static const bool trace = std::getenv("MH4N_ONESCREEN_TRACE") != nullptr;
    if (trace) {
        static u32 last_kinds = ~0u;
        const u32 kinds = rd8(ctx, PROMPT_OWNER + PROMPT_BOUND) << 16 | rd8(ctx, PROMPT_OWNER + PROMPT_KIND0) << 8 |
                          rd8(ctx, PROMPT_OWNER + PROMPT_KIND1);
        if (kinds != last_kinds) {
            last_kinds = kinds;
            line += fmt::format("{}one screen: ui006 kinds {} {} bound {}", line.empty() ? "" : "; ", kinds >> 8 & 0xFF,
                                kinds & 0xFF, kinds >> 16);
        }
    }
    if (const int n = NameIndex(PSEUDO_QUEST); n >= 0) {
        const u32 control = rd32(ctx, GAME_CONTROL);
        const u32 mode = control ? rd8(ctx, control + GAME_CONTROL_MODE) : 0;
        shown[n] = mode == 1 || mode == 5;
    }

    // the screen in force on each channel: the first whose rule holds
    int chosen[kChannels] = {-1, -1, -1};
    for (int s = 0; s < static_cast<int>(std::size(Table::kScreens)); ++s) {
        const Table::Screen& sc = Table::kScreens[s];
        if (sc.channel >= kChannels || chosen[sc.channel] >= 0) continue;
        if (Has(shown, sc.all_first, sc.all_count, true) &&
            (sc.any_count == 0 || Has(shown, sc.any_first, sc.any_count, false)) &&
            !Has(shown, sc.none_first, sc.none_count, false)) {
            chosen[sc.channel] = s;
        }
    }
    // (the log says which screens are in force when they change: the first 200 times, so a menu that goes back and forth
    // can't fill it)
    const std::string screens = fmt::format("{} {} {}", chosen[0], chosen[1], chosen[2]);
    if (screens != last_screens) {
        last_screens = screens;
        static int logged = 0;
        if ((line.empty() || trace) && logged < 200) {
            line += (line.empty() ? "" : "; ") + ("one screen: screens " + screens) +
                    (++logged == 200 ? " (no more of these)" : "");
        }
    }

    for (const auto& [group, block] : placed) {
        Place place = Table::kDefaults[block];
        for (int c = 0; c < kChannels; ++c) {
            if (chosen[c] < 0) continue;
            const Table::Screen& sc = Table::kScreens[chosen[c]];
            const Table::Entry* first = Table::kEntries + sc.entry_first;
            const Table::Entry* last = first + sc.entry_count;
            const auto* it = std::lower_bound(first, last, block,
                                              [](const Table::Entry& e, int b) { return e.block < b; });
            if (it != last && it->block == block) {
                place = it->place.flags & F_COMPOSE ? Compose(it->place, place) : it->place;
            }
        }
        Note& note = notes[group];
        if (const u32 hash = rd32(ctx, group); note.hash != hash) note = Note{hash};
        const u32 flags = rd32(ctx, group + GROUP_FLAGS);
        const u32 view = (flags & VIEW_MASK) >> VIEW_SHIFT;
        if (!note.view || view != note.written_view) note.game_view = view;  // (the game's own, or it set it again)
        u32 want = note.game_view;
        if (place.flags & F_HIDDEN) {
            want = VIEW_NONE;
        } else if (place.flags & F_TOP) {
            want = VIEW_TOP;
        } else if (place.flags & F_BOTTOM) {
            want = VIEW_TOUCH;
        }
        if (want != note.game_view) {
            if (view != want) {
                wr32(ctx, group + GROUP_FLAGS, (flags & ~VIEW_MASK) | want << VIEW_SHIFT);
                wr8(ctx, group + 0x42, 3);
            }
            note.view = true;
            note.written_view = want;
        } else {
            GiveBackView(ctx, group, note);
        }
        // the draw priority: the place's, or the game's plus the place's
        const u32 order = rd32(ctx, group + GROUP_ORDER);
        const u32 priority = (order & PRIORITY_MASK) >> PRIORITY_SHIFT;
        if (!note.priority || priority != note.written_priority) note.game_priority = priority;
        if (want == VIEW_TOP && (place.flags & (F_PRIORITY_SET | F_PRIORITY_ADD))) {
            const int wanted = place.flags & F_PRIORITY_SET ? place.priority
                                                            : static_cast<int>(note.game_priority) + place.priority;
            const u32 target = static_cast<u32>(std::clamp(wanted, 0, 0xFF));
            if (priority != target) {
                wr32(ctx, group + GROUP_ORDER, (order & ~PRIORITY_MASK) | target << PRIORITY_SHIFT);
                CallGame(ctx, FN_GROUP_CHANGED, group);
            }
            note.priority = true;
            note.written_priority = target;
        } else {
            GiveBackPriority(ctx, group, note);
        }
        // (MH4N_ONESCREEN_UNMOVED=<name hash>: that group is not moved, as when the draw has no room left for its
        // constants, for the PC runner)
        static const u32 unmoved_test = [] {
            const char* v = std::getenv("MH4N_ONESCREEN_UNMOVED");
            return v ? static_cast<u32>(std::strtoul(v, nullptr, 16)) : 0u;
        }();
        if (want == VIEW_TOP && (place.scale != 1.0f || place.tx[0] != 0 || place.tx[1] != 0 || place.ty != 0) &&
            (unmoved_test == 0 || rd32(ctx, group) != unmoved_test)) {
            const int own = note.game_view == VIEW_TOUCH ? 1 : 0;
            moves[group] = Move{place.scale, place.tx[own], place.ty};
        }
        if (want == VIEW_TOP && wide) cuts.insert(group);
        for (auto& [pane, h] : hidden) {
            if (h.group == group) h.seen = true;
        }
        if (want == VIEW_TOP) HidePanes(ctx, group);
    }
    // the panes hidden before: shown again once their group is no longer on the top screen (forgotten if it is gone)
    for (auto it = hidden.begin(); it != hidden.end();) {
        Hidden& h = it->second;
        if (h.seen && h.kept) {
            h.seen = h.kept = false;
            ++it;
            continue;
        }
        if (h.seen) ShowAgain(ctx, it->first, h);
        it = hidden.erase(it);
    }
    if (line.empty() && !cut_line.empty()) line = std::move(cut_line), cut_line.clear();
    if (!unmoved_line.empty()) line += (line.empty() ? "" : "; ") + unmoved_line, unmoved_line.clear();
    return line;
}

void ObjectScissor(RCtx* ctx, u32 object, u32 draw, u32 screen) {
    Cut(ctx, draw, screen == 0 && cuts.count(object) != 0);
}

namespace {

/// `object` drawn with a copy of the pass's constants whose projection scales and moves it (`m`); false when there was
/// no room for one (it is then drawn where the game puts it).
bool Apply(RCtx* ctx, u32 object, u32 draw, const Move& m) {
    const u32 block = rd32(ctx, draw + CDRAW_VIEWPORT);
    const u32 game = swap.draw == draw && swap.ours != 0 && block == swap.ours ? swap.game : block;
    const u32 shader = rd32(ctx, draw + CDRAW_SHADER);
    const u32 layout = shader ? rd32(ctx, shader + SHADER_VIEWPORT_LAYOUT) : 0;
    const u32 size = layout ? ((rd32(ctx, layout + 0x14) & 0xFFFF) * 4 + 15) & ~15u : 0;
    u32 at = rd32(ctx, draw + CDRAW_ROOM);
    u32 end = rd32(ctx, draw + CDRAW_ROOM_END);
    const auto words_xor = [&](u32 from) {
        u32 x = 0;
        for (u32 i = 0; i < size; i += 4) x ^= rd32(ctx, from + i) * (i + 1);
        return x;
    };
    // a copy made this frame for the same pass constants and the same move: shared (it lies below the room's next
    // free byte, in the same room, and nothing wrote over it)
    for (const Copy& c : copies) {
        if (c.draw == draw && c.game == game && c.end == end && c.scale == m.scale && c.tx == m.tx && c.ty == m.ty &&
            c.at + size <= at && size >= (PROJ + 16) * 4 && size <= 0x400 && words_xor(c.at) == c.check) {
            wr32(ctx, draw + CDRAW_VIEWPORT, c.at);
            wr32(ctx, draw + CDRAW_DIRTY, rd32(ctx, draw + CDRAW_DIRTY) | 1);
            swap = Swap{draw, game, c.at};
            return true;
        }
    }
    // (only where the library knows the function's address: a library of another version made before the port listed
    // it has none, and USA v1.3.0's would be another function there)
    const auto& table = GameAddressTable();
    const bool room_more_known =
        table.empty() || std::binary_search(table.begin(), table.end(),
                                            std::make_pair(FN_ROOM_MORE.Usa(), GameAddress(FN_ROOM_MORE.Usa())));
    if (room_more_known && game != 0 && size >= (PROJ + 16) * 4 && size <= 0x400 && at + size > end) {
        // the room is full: a new one, as the game takes one for its own constants (the copies of this frame in the
        // full room stay where they are; this room's are made anew)
        CallGame(ctx, FN_ROOM_MORE, draw, size);
        at = rd32(ctx, draw + CDRAW_ROOM);
        end = rd32(ctx, draw + CDRAW_ROOM_END);
        static bool refill_logged = false;
        if (!refill_logged) {
            refill_logged = true;
            unmoved_line = fmt::format("one screen: the draw's constant room was full: a new one from the game's "
                                       "allocator, {:08X}..{:08X}", at, end);
        }
    }
    if (game == 0 || size < (PROJ + 16) * 4 || size > 0x400 || at == 0 || at + size > end) {
        // (no room: drawn where the game puts it this frame; the log says so for the first few groups)
        if (unmoved.size() < 8 && unmoved.insert(rd32(ctx, object)).second) {
            unmoved_line = fmt::format("one screen: group {:08X} drawn unmoved (constants {:08X}, size {:#x}, room "
                                       "{:08X}..{:08X})", rd32(ctx, object), game, size, at, end);
        }
        return false;
    }
    wr32(ctx, draw + CDRAW_ROOM, at + size);
    for (u32 i = 0; i < size; i += 4) wr32(ctx, at + i, rd32(ctx, game + i));
    // pixels p of the top screen go to scale * (p - centre) + centre + (tx, ty): the projection's two rows scaled,
    // then moved (its x row is the screen's height, up; its y row the screen's width, to the left)
    const float m00 = F(ctx, game + 4 * (PROJ + 0)), m11 = F(ctx, game + 4 * (PROJ + 5));
    for (u32 column = 0; column < 4; ++column) {
        for (u32 row = 0; row < 2; ++row) {
            const u32 a = at + 4 * (PROJ + 4 * column + row);
            WF(ctx, a, F(ctx, a) * m.scale);
        }
    }
    WF(ctx, at + 4 * (PROJ + 12), F(ctx, at + 4 * (PROJ + 12)) - m.ty * m00);
    WF(ctx, at + 4 * (PROJ + 13), F(ctx, at + 4 * (PROJ + 13)) - m.tx * m11);
    wr32(ctx, draw + CDRAW_VIEWPORT, at);
    wr32(ctx, draw + CDRAW_DIRTY, rd32(ctx, draw + CDRAW_DIRTY) | 1);
    swap = Swap{draw, game, at};
    if (copies.size() < 64) copies.push_back(Copy{draw, game, end, m.scale, m.tx, m.ty, at, words_xor(at)});
    return true;
}

}  // namespace

void ObjectConstants(RCtx* ctx, u32 object, u32 draw, u32 constants, u32 screen) {
    (void)constants;
    const u32 block = rd32(ctx, draw + CDRAW_VIEWPORT);
    const auto it = screen == 0 ? moves.find(object) : moves.end();
    if (it == moves.end()) {
        if (swap.ours != 0 && swap.draw == draw && block == swap.ours) {  // the pass's own constants again
            wr32(ctx, draw + CDRAW_VIEWPORT, swap.game);
            wr32(ctx, draw + CDRAW_DIRTY, rd32(ctx, draw + CDRAW_DIRTY) | 1);
        }
        return;
    }
    Apply(ctx, object, draw, it->second);
}

bool Placed(u32 object) {
    return moves.count(object) != 0;
}

bool ScaleObject(RCtx* ctx, u32 object, u32 draw, float scale, float tx, float ty) {
    if (moves.count(object) != 0 || (scale == 1.0f && tx == 0 && ty == 0)) return false;
    return Apply(ctx, object, draw, Move{scale, tx, ty});
}

}  // namespace Port::OneScreen
