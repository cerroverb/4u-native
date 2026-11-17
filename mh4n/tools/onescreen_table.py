#!/usr/bin/env python3
"""One screen's table: where each root group of the game's touch-screen menus is drawn on the top screen.

    python3 tools/onescreen_table.py --romfs GAME/romfs.bin --romfs GAME/update_romfs.bin [--lang eng]

Reads config/onescreen.json (Heavenfall's single-screen arrangement, see its `description` and `source`) and the layouts
of your own game (the layout files in <lang>/data/*.arc of its RomFS), measures what each layout shows where Heavenfall's
data needs it (an anchor, a block's box), and writes port/src/onescreen_table.h: every root group's place as a scale and a
move of the top screen's pixels, the rules that choose among them, nothing of the game's files but the name hashes of
the groups and those numbers. port/src/onescreen.cpp applies it as the game draws (docs/research/onescreen.md).

The layouts are the same in the five languages but for a few pixels of three of them (checked on USA v1.3.0), so one
language's measures serve all.
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import romfs_image  # noqa: E402

LYT_TYPE = 0x15302EF4
RECORD = {0: 0x6C, 1: 0x38, 2: 0x28, 3: 0x7C, 4: 0x30, 5: 0x88}
SPRITE, NULL, GROUP, TEXT, BOUNDARY, WINDOW = range(6)
TOP_HALF = (200.0, 120.0)
TOUCH_HALF = (160.0, 120.0)
CENTRE = (200.0, 120.0)                       # the top screen's centre, pixels (the layouts' origin)
OWN_OFFSET = {"top": 0.0, "bottom": 40.0}     # a touch-screen layout drawn on the top screen sits 40 pixels right
SCREEN_CENTRE = {"top": (200.0, 120.0), "bottom": (160.0, 120.0)}
SCREEN_SIZE = {"top": (400.0, 240.0), "bottom": (320.0, 240.0)}
ANCHORS = {"top_left": (1, 1), "top": (0, 1), "top_right": (-1, 1), "left": (1, 0), "center": (0, 0),
           "right": (-1, 0), "bottom_left": (1, -1), "bottom": (0, -1), "bottom_right": (-1, -1)}
# The archives (under <lang>/data/) that hold the layouts the table places
ARCS = ["core_lobby", "core_common", "core_title", "core_dlc", "core_arena", "core_quest", "core_result",
        "v00a00_map", "v00a01_map", "v03a01_map", "v04a00_map", "v05a00_map", "v05a01_map", "v05a02_map",
        "monnyan_core", "loading_5", "loading_6", "loading_7"] + [f"pl_wp{n:02d}_lyt" for n in range(17)]
# Layouts left on the touch screen: they are driven by touch only (Heavenfall gives them the pad with code of its own)
TOUCH_ONLY = {"ui430", "ui422", "ui800", "ui802", "ui700", "ui705"}

# Place flags (port/src/onescreen.cpp)
F_TOP, F_HIDDEN, F_KEEP, F_PRIORITY_SET, F_PRIORITY_ADD, F_COMPOSE, F_BOTTOM = 1, 2, 4, 8, 16, 32, 64


def name_hash(name: str) -> int:
    return zlib.crc32(name.encode("ascii")) ^ 0xFFFFFFFF


class Pane:
    def __init__(self, data: bytes, kind: int, depth: int, offset: int, name: str, parent: "Pane | None"):
        self.data, self.kind, self.depth, self.offset, self.name, self.parent = data, kind, depth, offset, name, parent
        self.children: list[Pane] = []

    def _f2(self, at: int) -> tuple[float, float]:
        return struct.unpack_from("<2f", self.data, self.offset + at)

    def _u(self, at: int) -> int:
        return struct.unpack_from("<I", self.data, self.offset + at)[0]

    @property
    def position(self):
        return self._f2(0x28 if self.kind == TEXT else 0x0C)

    @property
    def size(self):
        return {SPRITE: self._f2(0x18), TEXT: self._f2(0x34), BOUNDARY: self._f2(0x14),
                WINDOW: self._f2(0x18)}.get(self.kind)

    @property
    def scale(self):
        return {SPRITE: self._f2(0x20), NULL: self._f2(0x18), BOUNDARY: self._f2(0x1C)}.get(self.kind, (1.0, 1.0))

    @property
    def visible(self):
        return {SPRITE: bool(self._u(0x60)), NULL: bool(self._u(0x2C))}.get(self.kind, True)

    @property
    def transparent(self) -> bool:
        at = {SPRITE: 0x38, TEXT: 0x44, WINDOW: 0x38}.get(self.kind)
        return at is not None and all(self.data[self.offset + at + 4 * i + 3] == 0 for i in range(4))

    @property
    def origin(self) -> int:
        if self.kind == SPRITE:
            return self._u(0x50) & 0xFF
        if self.kind == TEXT:
            return self._u(0x70) >> 4 & 0xF
        return 0

    def walk(self):
        yield self
        for child in self.children:
            yield from child.walk()


def parse_layout(data: bytes) -> list[Pane]:
    """The root groups of a layout file (lyt, version 0x70C), each with its tree."""
    if data[:4] != b"lyt\0":
        raise ValueError("not a layout")
    offset = struct.unpack_from("<I", data, 0x28)[0]
    kind, depth = struct.unpack_from("<II", data, offset)
    offset += 8
    roots, stack = [], []
    while kind != 0xFF:
        del stack[depth:]
        parent = stack[-1] if stack else None
        name_at = struct.unpack_from("<I", data, offset + 4)[0]
        name = data[name_at:data.index(b"\0", name_at)].decode("ascii", "replace") if name_at else ""
        pane = Pane(data, kind, depth, offset, name, parent)
        (parent.children if parent else roots).append(pane)
        stack.append(pane)
        size = RECORD[kind]
        kind, depth = struct.unpack_from("<II", data, offset + size - 8)
        offset += size
    return roots


def arc_layouts(buf: bytes) -> dict[str, bytes]:
    """name (ui409) -> the layout file, of an MT archive's layout entries."""
    if buf[:4] != b"ARC\0":
        raise ValueError("not an ARC archive")
    _version, count = struct.unpack_from("<HH", buf, 4)
    out = {}
    for i in range(count):
        base = 0x0C + i * 0x50
        name = buf[base:base + 64].split(b"\0", 1)[0].decode("ascii")
        type_hash, size, _flags, at = struct.unpack_from("<IIII", buf, base + 64)
        if type_hash == LYT_TYPE:
            out[name.split("\\")[-1]] = zlib.decompress(buf[at:at + size])
    return out


# ---- measuring (after Heavenfall's interface/single_screen/layout/single_screen.py and interface/game_gui/geometry.py)

def visible_extent(roots, half=TOP_HALF, skip=(), only=None, skip_panes=()):
    """The box (layout units: x left, y up) around the sprites and texts that show as the layout loads, each read by its
    top-left corner, cut to a screen of `half` size; None when nothing shows."""
    boxes = []

    def walk(pane, x, y, sx, sy):
        for child in pane.children:
            if child.kind == BOUNDARY or not child.visible or child.name in skip_panes:
                continue
            cx, cy = x + sx * child.position[0], y + sy * child.position[1]
            if child.kind == NULL:
                walk(child, cx, cy, sx * child.scale[0], sy * child.scale[1])
                continue
            if child.kind not in (SPRITE, TEXT) or child.transparent:
                continue
            if child.kind == SPRITE:
                w, h = child.size[0] * child.scale[0] * sx, child.size[1] * child.scale[1] * sy
            else:
                w, h = child.size[0] * sx, child.size[1] * sy
            if not w or not h:
                continue
            box = (max(cx - abs(w), -half[0]), max(cy - abs(h), -half[1]), min(cx, half[0]), min(cy, half[1]))
            if box[0] < box[2] and box[1] < box[3]:
                boxes.append(box)

    for root in roots:
        if root.name not in skip and only in (None, root.name):
            walk(root, *root.position, 1.0, 1.0)
    if not boxes:
        return None
    return (min(b[0] for b in boxes), min(b[1] for b in boxes), max(b[2] for b in boxes), max(b[3] for b in boxes))


def origin_point(origin: int) -> tuple[float, float]:
    """A sprite's or text's origin as a fraction of its size (0 the top-left corner ... 8 the bottom-right, column by
    column)."""
    if origin > 8:
        return 0.0, 0.0
    return (origin // 3) / 2.0, (origin % 3) / 2.0


def reference_box(roots, groups, screen: str):
    """A block's box: what its root groups show as the layout loads, each by its origin, in pixels of `screen` (x right,
    y down), cut to that screen; None when nothing shows."""
    ox, oy = SCREEN_CENTRE[screen]
    width, height = SCREEN_SIZE[screen]
    boxes = []

    def walk(pane, x, y, sx, sy):
        for child in pane.children:
            if child.kind == BOUNDARY or not child.visible:
                continue
            cx, cy = x + sx * child.position[0], y + sy * child.position[1]
            if child.kind == NULL:
                walk(child, cx, cy, sx * child.scale[0], sy * child.scale[1])
                continue
            if child.kind not in (SPRITE, TEXT) or child.transparent:
                continue
            scale = child.scale if child.kind == SPRITE else (1.0, 1.0)
            w, h = child.size[0] * scale[0] * sx, child.size[1] * scale[1] * sy
            px, py = ox - cx, oy - cy
            fx, fy = origin_point(child.origin)
            xs = (px - fx * w, px + (1 - fx) * w)
            ys = (py - fy * h, py + (1 - fy) * h)
            box = (max(min(xs), 0.0), max(min(ys), 0.0), min(max(xs), width), min(max(ys), height))
            if box[0] < box[2] and box[1] < box[3]:
                boxes.append(box)

    for root in roots:
        if root.name in groups:
            walk(root, *root.position, 1.0, 1.0)
    if not boxes:
        return None
    return (min(b[0] for b in boxes), min(b[1] for b in boxes), max(b[2] for b in boxes), max(b[3] for b in boxes))


def shift_towards(low, high, side, half_screen):
    if side == 0:
        return -(low + high) / 2
    return side * half_screen - (high if side == 1 else low)


# ---- the places

class Place:
    """Where a root group is drawn: its pixels p of the top screen (as the game draws it there) go to
    scale * (p - centre) + centre + (tx, ty); tx for the group's own screen (0 the top one, 1 the touch one)."""

    def __init__(self, flags=0, priority=0, scale=1.0, tx=(0.0, 0.0), ty=0.0):
        self.flags, self.priority, self.scale, self.tx, self.ty = flags, priority, scale, tuple(tx), ty

    def key(self):
        return (self.flags, self.priority, round(self.scale, 4), round(self.tx[0], 3), round(self.tx[1], 3),
                round(self.ty, 3))


def layout_places(name, setting, roots, top_screen):
    """name -> Place of each root group of a layout of `layouts` (Heavenfall's anchors)."""
    view = F_TOP if name in top_screen else F_KEEP
    groups = [r.name for r in roots]
    hide = set(setting.get("hide", []))
    hidden_roots = hide & set(groups)
    inner_hidden = hide - hidden_roots
    backdrop = setting.get("backdrop")
    fixed = set(setting.get("fixed", []))
    skip = ({backdrop} if backdrop else set()) | fixed | hidden_roots
    factor = float(setting.get("factor", 1.0))
    anchor = setting.get("anchor")
    move = setting.get("move", "xy")
    offset = setting.get("offset", [0.0, 0.0])
    shift = (0.0, 0.0)
    scale = 1.0
    if anchor is not None:
        scale = factor
        extent = visible_extent(roots, skip=skip, skip_panes=inner_hidden)
        if extent is not None:
            e = [v * factor for v in extent]
            side_x, side_y = ANCHORS[anchor]
            shift = (shift_towards(e[0], e[2], side_x, TOP_HALF[0]) if "x" in move else 0.0,
                     shift_towards(e[1], e[3], side_y, TOP_HALF[1]) if "y" in move else 0.0)
    places = {}
    priority = int(setting.get("priority", 0))
    group_priority = setting.get("group_priority", {})
    push = set(setting.get("push", []))
    nudge = setting.get("nudge", {})
    for root in roots:
        g = root.name
        if g in fixed:
            continue
        prio = group_priority.get(g, 0 if g == backdrop else priority)
        flags = view | (F_PRIORITY_ADD if prio else 0)
        if g in hidden_roots:
            places[g] = Place(view | F_HIDDEN)
            continue
        if g == backdrop:
            e = visible_extent(roots, half=TOUCH_HALF, only=g)
            if e is None:
                continue
            cover = max(2 * TOP_HALF[0] / (e[2] - e[0]), 2 * TOP_HALF[1] / (e[3] - e[1]))
            c = ((e[0] + e[2]) / 2, (e[1] + e[3]) / 2)
            places[g] = Place(flags, prio, cover, (cover * c[0], cover * c[0]), cover * c[1])
            continue
        sx, sy = shift
        if g in push and anchor is not None:
            e = visible_extent(roots, only=g, skip_panes=inner_hidden)
            if e is not None:
                e = (e[0] * scale + sx, e[1] * scale + sy, e[2] * scale + sx, e[3] * scale + sy)
                side_x, side_y = ANCHORS[anchor]
                sx += shift_towards(e[0], e[2], side_x, TOP_HALF[0]) if side_x else 0.0
                sy += shift_towards(e[1], e[3], side_y, TOP_HALF[1]) if side_y else 0.0
        tx, ty = -sx + offset[0], -sy + offset[1]
        if g in nudge:
            tx += nudge[g][0]
            ty += nudge[g][1]
        if scale == 1.0 and tx == 0 and ty == 0 and not prio and view == F_KEEP:
            continue
        places[g] = Place(flags, prio, scale, (tx, tx), ty)
    return places


def block_place(place, box, own, backdrop_cover=None):
    """A block's Place from a placement of the arrangement (x, y, scale, screen, priority, hidden; a key missing is the
    game's own) and its reference box on its own screen (`own`)."""
    if place is None:
        return None
    flags = 0
    if place.get("hidden"):
        flags |= F_HIDDEN
    screen = place.get("screen")
    if screen == "top":
        flags |= F_TOP
    elif screen == "bottom":
        flags |= F_BOTTOM
    else:
        flags |= F_KEEP
    prio = 0
    if place.get("priority") is not None:
        flags |= F_PRIORITY_SET
        prio = int(place["priority"])
    scale = 1.0 if place.get("scale") is None else float(place["scale"])
    if backdrop_cover is not None and not place.get("hidden"):
        return Place(flags, prio, *backdrop_cover)
    if place.get("x") is None and place.get("y") is None and place.get("scale") is None:
        return Place(flags, prio)
    if box is None:     # nothing shows as the layout loads (the code shows it later): Heavenfall's box is the corner
        box = (0.0, 0.0, 0.0, 0.0)
    ox, oy = SCREEN_CENTRE[own]
    corner = (ox - box[0], oy - box[1])     # layout units, the same on either screen
    tx = []
    ty = 0.0
    for o in ("top", "bottom"):
        cx, cy = SCREEN_CENTRE[o]
        b0, b1 = cx - corner[0], cy - corner[1]
        x = b0 if place.get("x") is None else float(place["x"])
        y = b1 if place.get("y") is None else float(place["y"])
        dx, dy = x - scale * b0, y - scale * b1
        tx.append(dx - CENTRE[0] + scale * (CENTRE[0] - OWN_OFFSET[o]))
        ty = dy - CENTRE[1] + scale * CENTRE[1]
    return Place(flags, prio, scale, tx, ty)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--romfs", action="append", required=True, help="romfs.bin / update_romfs.bin or a dump (repeat)")
    ap.add_argument("--lang", default="eng")
    ap.add_argument("--config", default=os.path.join(ROOT, "config", "onescreen.json"))
    ap.add_argument("--out", default=os.path.join(ROOT, "port", "src", "onescreen_table.h"))
    args = ap.parse_args(argv)
    config = json.load(open(args.config, encoding="utf-8"))

    layouts: dict[str, list[Pane]] = {}
    for arc in ARCS:
        buf = romfs_image.read_file(args.romfs, f"{args.lang}/data/{arc}.arc")
        if buf is None:
            continue
        for name, data in arc_layouts(buf).items():
            layouts.setdefault(name, parse_layout(data))
    print(f"{len(layouts)} layouts read")

    top_screen = set(config["top_screen"]) - TOUCH_ONLY
    names: dict[str, int] = {}          # group name -> block index
    blocks: list[str] = []              # block names (for the comments)
    defaults: list[Place] = []

    def block(label, groups, place):
        index = len(blocks)
        blocks.append(label)
        defaults.append(place if place is not None else Place(F_KEEP))
        for g in groups:
            if g in names and names[g] != index:
                print(f"note: {g} already in block {blocks[names[g]]}, now {label}")
            names[g] = index
        return index

    # panes inside a root group not drawn while the group is placed: (group, pane)
    pane_hides: set[tuple[str, str]] = set()

    def hide_panes(roots, names):
        for root in roots:
            for pane in root.walk():
                if pane is not root and pane.name in names:
                    pane_hides.add((root.name, pane.name))

    # Heavenfall's anchored layouts: one block a root group, its place the default
    group_block: dict[str, int] = {}
    for name, setting in config["layouts"].items():
        if name in TOUCH_ONLY:
            continue
        roots = layouts.get(name)
        if roots is None:
            print(f"skip {name}: no such layout")
            continue
        places = layout_places(name, setting, roots, top_screen)
        # (but a pane Heavenfall covers with a fill of its own: ui006's panel ground, ui006_naka00)
        hide_panes(roots, set(setting.get("hide", [])) - {r.name for r in roots} - {"ui006_naka00"})
        for root in roots:
            if root.name in places:
                group_block[root.name] = block(f"{name}:{root.name}", [root.name], places[root.name])
            elif name in top_screen:
                group_block[root.name] = block(f"{name}:{root.name}", [root.name], Place(F_TOP))
    # top_screen layouts without a setting: their groups to the top screen as they are
    for name in sorted(top_screen - set(config["layouts"])):
        for root in layouts.get(name, []):
            group_block[root.name] = block(f"{name}:{root.name}", [root.name], Place(F_TOP))

    # the arrangement's blocks
    arrangement = config["arrangement"]
    own_screens = arrangement["layout_screens"]
    backdrops = arrangement.get("backdrops", {})
    arranged: dict[str, dict] = {}
    for name, panes in arrangement.get("hides", {}).items():
        if name not in TOUCH_ONLY and name in layouts:
            hide_panes(layouts[name], set(panes))
    for b in arrangement["blocks"]:
        if b["layout"] in TOUCH_ONLY or b.get("part"):
            continue
        roots = layouts.get(b["layout"])
        if roots is None:
            print(f"skip block {b['id']}: no layout {b['layout']}")
            continue
        own = own_screens.get(b["layout"], "top")
        box = reference_box(roots, set(b["groups"]), own)
        cover = None
        if b.get("backdrop") and backdrops.get(b["layout"]) in b["groups"]:
            e = visible_extent(roots, half=TOUCH_HALF, only=backdrops[b["layout"]])
            if e is not None:
                s = max(2 * TOP_HALF[0] / (e[2] - e[0]), 2 * TOP_HALF[1] / (e[3] - e[1]))
                c = ((e[0] + e[2]) / 2, (e[1] + e[3]) / 2)
                # drawn on the top screen its middle is at layout c, whatever its own screen: middle to the centre
                cover = (s, (s * c[0], s * c[0]), s * c[1])
        info = {"box": box, "own": own, "cover": cover}
        arranged[b["id"]] = info
        info["index"] = block(b["id"], b["groups"], block_place(b.get("default"), box, own, cover))

    # rules: names of groups (by hash) and pseudo names
    rule_names: set[str] = set()

    screens = []   # (channel, all, any, none, entries: [(block, Place)], label)

    def add_screen(channel, label, all_=(), any_=(), none=(), entries=()):
        rule_names.update((*all_, *any_, *none))
        screens.append((channel, label, list(all_), list(any_), list(none), list(entries)))

    for s in arrangement["screens"]:
        entries = []
        for block_id, place in s["blocks"].items():
            info = arranged.get(block_id)
            if info is None:
                continue
            p = block_place(place, info["box"], info["own"], info["cover"]) if place else defaults[info["index"]]
            entries.append((info["index"], p))
        when = s["when"]
        add_screen(0, s["id"], when.get("all", []), when.get("any", []), when.get("none", []), entries)

    # the frames (Palico menus and the like): sets moved together while a scene's rule holds, on top of their place
    frames = config["frames"]
    set_blocks: dict[str, list[int]] = {}
    for set_name, groups in frames["sets"].items():
        ids = []
        for g in groups:
            if g not in group_block:
                group_block[g] = block(f"frame:{g}", [g], Place(F_KEEP))
            ids.append(group_block[g])
        set_blocks[set_name] = ids
    for scene in frames["scenes"]:
        entries = []
        for set_name, f in scene["frames"].items():
            s = float(f["scale"])
            at = f["at"]
            t = (at[0] - (1 - s) * CENTRE[0], at[1] - (1 - s) * CENTRE[1])
            for b in set_blocks[set_name]:
                entries.append((b, Place(F_COMPOSE, 0, s, (t[0], t[0]), t[1])))
        add_screen(1, scene["name"], scene.get("all", []), scene.get("any", []), scene.get("none", []), entries)

    # the touch menus' description panel and the yes / no box (ui006), on top of its anchored place
    help_top = config["help_top"]
    ui006 = [r.name for r in layouts.get("ui006", [])]
    for g in ui006:
        if g not in group_block:
            group_block[g] = block(f"ui006:{g}", [g], Place(F_KEEP))
    stay = set(config.get("prompt_stay_bottom", []))
    # the yes / no box in a menu whose content is where the box goes: the box (its question and its plates together)
    # moved where that menu leaves room (`shift` in pixels, `groups` a group's own), before the box's own place
    for p in config.get("prompt_places", []):
        moves = p.get("groups", {})
        entries = []
        for g in ui006:
            if g not in stay:
                dx, dy = moves.get(g, p["shift"])
                entries.append((group_block[g], Place(F_COMPOSE | F_TOP, 0, 1.0, (dx, dx), dy)))
        add_screen(2, "prompt: " + p["name"], ["@prompt", *p.get("all", [])], p.get("when", []), p.get("unless", []),
                   entries)
    add_screen(2, "prompt", ["@prompt"], [], [],
               [(group_block[g], Place(F_COMPOSE | F_TOP)) for g in ui006 if g not in stay])
    add_screen(2, "stay bottom", [], help_top.get("stay_bottom", []), [], [])
    panel = [help_top["group"], *help_top.get("also", [])]
    for p in help_top["places"]:
        if p.get("when_flags"):
            continue
        entries = []
        if p.get("panel", True):
            for g in panel:
                entries.append((group_block[g], Place(F_COMPOSE | F_TOP, 0, 1.0, (p["shift"][0],) * 2, p["shift"][1])))
        for g, (dx, dy) in p.get("groups", {}).items():
            if g in group_block:
                entries.append((group_block[g], Place(F_COMPOSE | F_TOP, 0, 1.0, (dx, dx), dy)))
        none = list(p.get("unless", [])) + ([] if p.get("quest") else ["@quest"])
        add_screen(2, p["name"], [], p.get("when", []), none, entries)

    # the table's names: every group of a block and every name a rule reads, sorted by hash
    all_names = set(names) | set(rule_names)
    hashed = sorted(((name_hash(n) if not n.startswith("@") else {"@prompt": 1, "@quest": 2}[n], n)
                     for n in all_names))
    index_of = {n: i for i, (_, n) in enumerate(hashed)}
    lines = []
    w = lines.append
    w("// Generated by tools/onescreen_table.py from config/onescreen.json (Heavenfall's single-screen arrangement, by")
    w(f"// popzol and BlitzWOLF, GPL-3.0, commit {config['source']['commit'][:12]}) and the layouts of the game it was run")
    w(f"// on ({args.lang}). Don't edit: run the tool again. port/src/onescreen.cpp reads it. (The hashes end in u: they are")
    w("// names, not game addresses, for tests/test_port_addresses.py.)")
    w("#pragma once")
    w("")
    w("namespace Port::OneScreen::Table {")
    w("")
    w(f"constexpr std::uint16_t kBlockCount = {len(blocks)};")
    w(f"constexpr std::uint32_t kNameCount = {len(hashed)};")
    w("// group name hash (1 a yes / no prompt is open, 2 a quest's scene), its block (0xFFFF none)")
    w("constexpr Name kNames[] = {")
    for h, n in hashed:
        w(f"    {{0x{h:08X}u, {names.get(n, 0xFFFF)}}},  // {n}")
    w("};")
    w("// each block's place when no screen of its channel holds")
    w("constexpr Place kDefaults[] = {")
    for i, p in enumerate(defaults):
        w(f"    {{{p.flags}, {p.priority}, {p.scale:.4f}f, {{{p.tx[0]:.3f}f, {p.tx[1]:.3f}f}}, {p.ty:.3f}f}},  // {i} {blocks[i]}")
    w("};")
    term_list, screen_rows, entry_rows = [], [], []
    for channel, label, all_, any_, none, entries in screens:
        row = [channel]
        for group in (all_, any_, none):
            row += [len(term_list), len(group)]
            term_list += [index_of[n] for n in group]
        entries = sorted(entries, key=lambda e: e[0])
        row += [len(entry_rows), len(entries)]
        entry_rows += entries
        screen_rows.append((row, label))
    w("// the rules' names (indices of kNames)")
    w("constexpr std::uint16_t kTerms[] = {")
    for i in range(0, len(term_list), 16):
        w("    " + ", ".join(str(t) for t in term_list[i:i + 16]) + ",")
    w("};")
    w("// channel (0 the arrangement, 1 the frames, 2 the description panel and the yes / no box), all, any, none, entries")
    w("constexpr Screen kScreens[] = {")
    for row, label in screen_rows:
        w(f"    {{{row[0]}, {row[1]}, {row[2]}, {row[3]}, {row[4]}, {row[5]}, {row[6]}, {row[7]}, {row[8]}}},  // {label}")
    w("};")
    w("// a screen's places, by block")
    w("constexpr Entry kEntries[] = {")
    for b, p in entry_rows:
        w(f"    {{{b}, {{{p.flags}, {p.priority}, {p.scale:.4f}f, {{{p.tx[0]:.3f}f, {p.tx[1]:.3f}f}}, {p.ty:.3f}f}}}},  // {blocks[b]}")
    w("};")
    w("// panes not drawn while their root group is placed: (group name hash, pane name hash), by group")
    w("constexpr Hide kHides[] = {")
    for g, pane in sorted(pane_hides, key=lambda gp: (name_hash(gp[0]), name_hash(gp[1]))):
        w(f"    {{0x{name_hash(g):08X}u, 0x{name_hash(pane):08X}u}},  // {g} {pane}")
    w("};")
    w("")
    w("}  // namespace Port::OneScreen::Table")
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{args.out}: {len(blocks)} blocks, {len(hashed)} names, {len(screens)} screens, {len(entry_rows)} places")
    return 0


if __name__ == "__main__":
    sys.exit(main())
