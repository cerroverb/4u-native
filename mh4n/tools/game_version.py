#!/usr/bin/env python3
"""The game's version as an input of the tools: which version a code.bin is, what a code mod's exheader.bin says
about the code it brings, the port's own addresses in that version, and what the code library is told about it.

Every version has its own addresses: the project's tables (config/functions.tsv, hooks.txt, tunables.txt) and the
port's native code (config/port_addresses.txt: port/src/hooks.cpp, swkbd_bridge.cpp, the runtime's frame counter)
hold USA v1.3.0's, and config/shifts/<version>.txt moves them to another version (tools/version_map.py).
config/versions.json lists the versions; each has a short key (usa_v130, eur_v110, usa_v010, ...).

A library made for another version than USA v1.3.0 carries its version and the port's addresses in it
(game_version.c, written beside the recompiled code): rc_game_version, rc_port_addr_usa[] and rc_port_addr_here[],
which the native runtime reads (mh4n/game_version.h) to translate the port's addresses. A USA v1.3.0 library has no
table (the port's addresses are its own), so that library, and its id, stay what they were. Every library also
carries the id of the tools that made it (rc_library_id, the .id file's) and, when a code mod put code past the
game's data (Heavenfall's "extension"), that code's range and CRC-32 (rc_ext_*), which the runtime checks.

    python3 tools/game_version.py list                      # the versions and whether the port runs them
    python3 tools/game_version.py translate usa_v010 0x01057194 ...
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import zlib
from dataclasses import dataclass

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import exheader as exh  # noqa: E402
import version_map  # noqa: E402

VERSIONS = os.path.join(ROOT, "config", "versions.json")
PORT_ADDRESSES = os.path.join(ROOT, "config", "port_addresses.txt")
PAGE = 0x1000
# The source file build_game.py writes beside the recompiled code (not one of build_game.ID_FILES: what it holds
# is the game's version, not the tools').
SOURCE_NAME = "game_version.c"


class VersionError(ValueError):
    """A code or a mod's exheader the tools can't make a library of; the text says what to do."""


def load_versions(path: str | None = None) -> list[dict]:
    with open(path or VERSIONS, encoding="utf-8") as f:
        return json.load(f)["versions"]


def find(key: str, versions: list[dict]) -> dict:
    for v in versions:
        if v.get("key") == key:
            return v
    raise VersionError(f"no version {key!r} in config/versions.json (known: "
                       + ", ".join(v["key"] for v in versions if v.get("key")) + ")")


def by_code(code: bytes, versions: list[dict]) -> dict | None:
    """The version whose decompressed code.bin this is, by its SHA-1."""
    sha1 = hashlib.sha1(code).hexdigest()
    return next((v for v in versions if v.get("code_sha1") == sha1), None)


def mod_id(program_id: int) -> int:
    if program_id & 0x000000FF_00000000 == 0x0000000E_00000000:
        program_id &= ~0x0000000E_00000000
    return program_id


def buildable(version: dict) -> bool:
    """The tables are for the target version; another one needs its shifts (tools/version_map.py)."""
    return bool(version.get("target") or version.get("shifts"))


def segments(version: dict) -> dict[str, tuple[int, int]]:
    return {k: (int(a, 16), int(s, 16)) for k, (a, s) in version.get("segments", {}).items()}


def unknown_code_message(program_id: int | None, versions: list[dict]) -> str:
    """What to tell a player whose code.bin is no version the project lists."""
    same_title = [v for v in versions if program_id is not None and v.get("program_id")
                  and mod_id(int(v["program_id"], 16)) == mod_id(program_id)]
    runs = [v["name"] for v in versions if buildable(v) and not v.get("mod_base_only")]
    if same_title and not any(v.get("code_sha1") for v in same_title):
        v = same_title[0]
        return (f"this is {v['name']}, a version the port has no addresses for yet: its library needs its own "
                f"tables (config/shifts), which someone with both that dump and the USA v1.3.0 one makes with "
                f"tools/version_map.py. The port runs: {', '.join(runs)}")
    if same_title:
        return ("this code.bin is a version of the game the project doesn't list (config/versions.json): another "
                "update, a modified code.bin or a damaged dump. The port runs: " + ", ".join(runs))
    return ("this code.bin isn't a version the project knows (config/versions.json); the port runs: " + ", ".join(runs))


# ── a code mod's exheader ──────────────────────────────────────────────────

@dataclass(frozen=True)
class ModCode:
    version: dict                       # the version whose code the mod's code is made from
    header: exh.ExHeader                # the mod's exheader: the segments the code is loaded with
    extension: tuple[int, int] | None   # [start, end) of the mod's code past the version's data and .bss


def _align(x: int) -> int:
    return (x + PAGE - 1) & ~(PAGE - 1)


def check_mod_exheader(game: exh.ExHeader, mod: exh.ExHeader, code_size: int, versions: list[dict],
                       where: str = "exheader.bin") -> ModCode:
    """A code mod's exheader.bin, checked as the native loader checks it (code_patch::CheckExheaderOverride) and
    matched to a version: the same program, its .text and .rodata where and as big as one known version's, its
    .data where that version's is, either as big (and then the same .bss) or bigger and covering that version's
    whole .bss (Heavenfall: the old .bss in .data, zeros in the file, and new pages after it), and code.bin as big
    as its segments. Returns the version and the range past the version's data (the mod's "extension"), or raises
    VersionError with what is wrong."""
    if game.program_id is not None and mod.program_id is not None and mod_id(mod.program_id) != mod_id(game.program_id):
        raise VersionError(f"{where} is for program {mod.program_id:016X}, not this game's {game.program_id:016X}")
    expected = (mod.text.num_pages + mod.ro.num_pages + mod.data.num_pages) * PAGE
    if code_size != expected:
        raise VersionError(f"{where}: the mod's code.bin is 0x{code_size:X} bytes but its segments take 0x{expected:X}: "
                           "the code.bin and exheader.bin are not from the same build of the mod")
    for name in ("text", "ro", "data"):
        s = getattr(mod, name)
        if s.size > s.num_pages * PAGE:
            raise VersionError(f"{where}: its .{name} segment is larger than its pages")
    candidates = []
    for v in versions:
        seg = segments(v)
        if not seg or not v.get("program_id") or mod_id(int(v["program_id"], 16)) != mod_id(mod.program_id or 0):
            continue
        if (mod.text.address, mod.text.size) != seg["text"] or (mod.ro.address, mod.ro.size) != seg["rodata"]:
            continue
        data_addr, data_size = seg["data"]
        bss_addr, bss_size = seg["bss"]
        if mod.data.address != data_addr or mod.data.size < data_size:
            continue
        mod_end = mod.data.address + mod.data.size
        if mod.data.size == data_size:
            if mod.bss_size != bss_size:
                continue
            candidates.append((v, None))
        elif mod_end >= bss_addr + bss_size:
            start = _align(bss_addr + bss_size)
            candidates.append((v, (start, mod_end) if mod_end > start else None))
    if not candidates:
        raise VersionError(f"{where}: the code it describes (.text 0x{mod.text.size:X} bytes, .rodata 0x{mod.ro.size:X}, "
                           f".data 0x{mod.data.size:X}) is no version of the game the tools know: a mod made for "
                           "another version or region, or a layout the port can't run")
    v, ext = candidates[0]
    return ModCode(v, mod, ext)


# ── the port's own addresses ───────────────────────────────────────────────

def load_port_addresses(path: str | None = None) -> list[tuple[int, str]]:
    """config/port_addresses.txt: `<address> <name>  # where` per line (USA v1.3.0)."""
    out = []
    with open(path or PORT_ADDRESSES, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line:
                parts = line.split()
                out.append((int(parts[0], 16), parts[1] if len(parts) > 1 else ""))
    return out


def port_table(version: dict, addresses: list[tuple[int, str]], root: str = ROOT) -> list[tuple[int, int]]:
    """(USA v1.3.0 address, this version's) for each port address; [] for the target version (the addresses are
    its own). An address without a translation is a VersionError: the library would point the hooks elsewhere."""
    if version.get("target"):
        return []
    if not version.get("shifts"):
        raise VersionError(f"{version.get('name', 'this version')} has no address map (config/shifts)")
    shifts = version_map.Shifts.load(os.path.join(root, version["shifts"]))
    out, missing = [], []
    for usa, name in addresses:
        here = shifts.translate(usa)
        if here is None:
            missing.append(f"0x{usa:08x} {name}")
        else:
            out.append((usa, here))
    if missing:
        raise VersionError(f"{version['shifts']} has no address for the port's " + ", ".join(missing)
                           + ": rebuild it with tools/version_map.py")
    return sorted(set(out))


def version_source(key: str, library_id: str, table: list[tuple[int, int]],
                   extension: tuple[int, int, int] | None = None) -> str:
    """game_version.c: what the runtime is told about the game version the library was made for."""
    base, size, crc = extension or (0, 0, 0)
    usa = ", ".join(f"0x{a:08x}u" for a, _ in table) or "0"
    here = ", ".join(f"0x{b:08x}u" for _, b in table) or "0"
    return f"""/* Written by tools/build_game.py (tools/game_version.py): the game version this library was made for. */
#if defined(_WIN32)
#define GV_EXPORT __declspec(dllexport)
#else
#define GV_EXPORT __attribute__((visibility("default")))
#endif
/* config/versions.json's key */
GV_EXPORT const char rc_game_version[] = "{key}";
/* the id of the tools that made it (build_game.py's code_library_id, the .id file beside the library) */
GV_EXPORT const char rc_library_id[] = "{library_id}";
/* the port's addresses (config/port_addresses.txt): USA v1.3.0's and this version's; none for USA v1.3.0 */
GV_EXPORT const unsigned rc_port_addr_count = {len(table)}u;
GV_EXPORT const unsigned rc_port_addr_usa[] = {{{usa}}};
GV_EXPORT const unsigned rc_port_addr_here[] = {{{here}}};
/* a code mod's code past the game's data (its "extension"): where, how big and its CRC-32 when the library was made */
GV_EXPORT const unsigned rc_ext_base = 0x{base:08x}u;
GV_EXPORT const unsigned rc_ext_size = 0x{size:x}u;
GV_EXPORT const unsigned rc_ext_crc32 = 0x{crc:08x}u;
"""


# The port's own copy of the maps (port/src/game_shifts.h): a library carries the port's addresses that
# config/port_addresses.txt had when it was made, and its id doesn't change with that list, so a library of another
# version made by older tools lacks the addresses added since (1.0's EUR v1.1.0 libraries and 1.0.1's gather.cpp). The
# port takes those from this copy, as port_table would have (port/src/game_addr.h's SetGameAddresses).
PORT_SHIFTS = os.path.join(ROOT, "port", "src", "game_shifts.h")


def port_shifts_header(versions: list[dict] | None = None, root: str = ROOT) -> str:
    """port/src/game_shifts.h: every mapped version's config/shifts, as C++ tables (LF line ends)."""
    def name(key: str, part: str) -> str:
        return "kShifts" + "".join(w.capitalize() for w in key.split("_")) + part

    def rows(intervals: list[tuple[int, int, int]]) -> str:
        return "".join(f"    {{0x{s:08x}u, 0x{e:08x}u, {'-' if d < 0 else ''}0x{abs(d):x}}},\n" for s, e, d in intervals)

    tables, entries = [], []
    for v in versions if versions is not None else load_versions():
        if v.get("target") or not v.get("shifts"):
            continue
        key = v["key"]
        shifts = version_map.Shifts.load(os.path.join(root, v["shifts"]))
        text_start, text_end = shifts.text or (0, 0)
        tables.append(f"// {v['shifts']}: {v['name']}\n"
                      f"inline constexpr GameShift {name(key, 'Text')}[] = {{\n{rows(shifts.intervals)}}};\n")
        data = "nullptr, 0"
        data_start, data_end = shifts.data or (0, 0)
        if shifts.data:
            tables.append(f"inline constexpr GameShift {name(key, 'Data')}[] = {{\n{rows(shifts.data_intervals)}}};\n")
            data = f"{name(key, 'Data')}, {len(shifts.data_intervals)}"
        entries.append(f"    {{\"{key}\", 0x{text_start:08x}u, 0x{text_end:08x}u, {name(key, 'Text')}, "
                       f"{len(shifts.intervals)}, 0x{data_start:08x}u, 0x{data_end:08x}u, {data}}},\n")
    return ("// Written by tools/game_version.py port-shifts from config/shifts (don't edit it: tests/test_port_addresses.py\n"
            "// checks that it is current). Each mapped version's map of USA v1.3.0's addresses, as tools/version_map.py's\n"
            "// Shifts: an address of USA's .text in an interval [start, end) is at address + shift in that version, one of\n"
            "// .text in none has no address there; outside .text, and outside the data when a version's data moved, an\n"
            "// address is the same. game_addr.h takes from it the port's addresses that a code library of that version\n"
            "// doesn't carry (made by tools from before they were in config/port_addresses.txt).\n"
            "#pragma once\n\n#include <cstdint>\n\nnamespace Port {\n\n"
            "struct GameShift {\n    std::uint32_t start, end;  // USA v1.3.0's [start, end)\n"
            "    std::int32_t shift;        // where they are in that version: address + shift\n};\n\n"
            "struct GameShifts {\n"
            "    const char* key;                     // config/versions.json's key (a library's rc_game_version)\n"
            "    std::uint32_t text_start, text_end;  // USA's .text\n"
            "    const GameShift* text;\n    std::uint32_t text_count;\n"
            "    std::uint32_t data_start, data_end;  // USA's .rodata to the end of .bss, when the data moved (else 0, 0)\n"
            "    const GameShift* data;\n    std::uint32_t data_count;\n};\n\n"
            + "\n".join(tables)
            + "\ninline constexpr GameShifts kGameShifts[] = {\n" + "".join(entries) + "};\n\n}  // namespace Port\n")


def extension_crc(code: bytes, text_base: int, extension: tuple[int, int] | None) -> tuple[int, int, int] | None:
    """(base, size, CRC-32) of the extension's bytes in the code (code.bin starts at text_base)."""
    if not extension:
        return None
    start, end = extension
    data = code[start - text_base:end - text_base]
    return start, len(data), zlib.crc32(data)


def port_code_ranges(version: dict, functions: dict[int, int], hooks: dict[int, str],
                     tunables: dict[int, tuple[int, str]], addresses: list[tuple[int, str]],
                     root: str = ROOT) -> list[tuple[int, int, str]]:
    """The code the port hooks, tunes or calls, at `version`'s addresses: (start, end, what). `functions`: USA v1.3.0
    function start -> size (config/functions.tsv); `hooks` and `tunables` are already at the version's addresses."""
    shifts = None if version.get("target") or not version.get("shifts") else \
        version_map.Shifts.load(os.path.join(root, version["shifts"]))
    here = (lambda a: a) if shifts is None else shifts.translate
    sizes = {}
    for usa, size in functions.items():
        t = here(usa)
        if t is not None:
            sizes[t] = size
    out = [(a, a + sizes.get(a, 4), f"hook {name}") for a, name in hooks.items()]
    out += [(a, a + 4, f"tunable {name} 0x{a:08x}") for a, (_, name) in tunables.items()]
    for usa, name in addresses:
        t = here(usa)
        if t is not None and t in sizes and not name.startswith("hook:"):
            out.append((t, t + sizes[t], f"function {name}"))
    return sorted(set(out))


def touched(reference: bytes, modded: bytes, text_base: int, ranges: list[tuple[int, int, str]]) -> list[str]:
    """What of `ranges` a code mod changed: the bytes differ between the reference code and the modded one."""
    out = []
    for start, end, what in ranges:
        a, b = start - text_base, end - text_base
        if 0 <= a < b <= min(len(reference), len(modded)) and reference[a:b] != modded[a:b]:
            out.append(what)
    return out


def reference_code(version: dict, sources: list[str], work_dir: str) -> bytes | None:
    """The unmodified code of `version` from one of the player's dumps (a .3ds/.cci/.cia given with --dump or --romfs),
    if one of them is that version (its SHA-1); None otherwise."""
    import codebin2elf
    import extract_ncch
    want = version.get("code_sha1")
    if not want:
        return None
    for i, path in enumerate(sources):
        if os.path.splitext(path)[1].lower() not in (".3ds", ".cci", ".cia", ".cxi"):
            continue
        out = os.path.join(work_dir, f"reference{i}")
        try:
            extract_ncch.extract(path, out)
            with open(os.path.join(out, "exheader.bin"), "rb") as f:
                header = exh.parse(f.read())
            with open(os.path.join(out, "code.bin"), "rb") as f:
                code = codebin2elf.load_code(header, f.read())
        except (OSError, ValueError):
            continue
        if hashlib.sha1(code).hexdigest() == want:
            return code
    return None


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list", help="the versions and whether the port runs them")
    t = sub.add_parser("translate", help="a version's addresses for USA v1.3.0 ones")
    t.add_argument("key")
    t.add_argument("addresses", nargs="+")
    p = sub.add_parser("port-shifts", help="write port/src/game_shifts.h, the port's copy of config/shifts")
    p.add_argument("-o", "--output", default=PORT_SHIFTS)
    args = ap.parse_args(argv)
    versions = load_versions()
    if args.cmd == "port-shifts":
        with open(args.output, "wb") as f:
            f.write(port_shifts_header(versions).encode())
        print(f"wrote {args.output}")
        return 0
    if args.cmd == "list":
        for v in versions:
            state = ("the tables' own" if v.get("target") else
                     f"through {v['shifts']}" if v.get("shifts") else "no addresses yet")
            print(f"{v.get('key', '?'):10} {v['name']}: {state}")
        return 0
    v = find(args.key, versions)
    shifts = version_map.Shifts.load(os.path.join(ROOT, v["shifts"])) if v.get("shifts") else None
    for a in args.addresses:
        usa = int(a, 16)
        here = usa if v.get("target") else shifts.translate(usa) if shifts else None
        print(f"0x{usa:08x} -> " + ("unmapped" if here is None else f"0x{here:08x}"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
