#!/usr/bin/env python3
"""One command from your own dump to the PC port's code library.

    python3 tools/build_game.py --dump "MH4U v1.3.0 update.cia" [--opt O1] [--install DIR]

1. extracts exheader + ExeFS code from your decrypted dump (.cia update, .3ds/.cci, .cxi),
2. checks it is a version the port runs (config/versions.json): USA v1.3.0, whose addresses
   the tables in config/ hold, or EUR v1.1.0, through its shifts (config/shifts/),
3. applies the code mods Azahar would apply from its user folder (load/mods/<program id>/:
   a code.bin, code.ips or code.bps; tools/code_mods.py), so that the library is the code
   that will run,
4. recompiles it to C (build/game/recomp/src, never shared),
5. compiles the code library (clang; on Windows llvm-mingw's x86_64-w64-mingw32-clang);
   --native builds it for the native runtime instead of Azahar's (flat memory, a
   preemption flag at the yield points; named <program id>_native.<ext>), and with
   --deterministic the yield points charge cycles as Azahar's do (<program id>_native_det.<ext>),
6. optionally copies it to <Azahar user dir>/recomp/<program id>.<so|dll|dylib>, where the
   port's Azahar build picks it up (--install alone: Azahar's usual user directory).

--android builds the library for the port's Android app (arm64) instead, in
<out>/recomp/android/: <program id>.so for Azahar's app (copy it to the device, into Azahar's
user folder as recomp/<program id>.so, or give --install that folder, synced or mounted), and
with --native libmh4u_code_native.so (_native_det with --deterministic), which the native app
loads from its own folder (copy it there; --mods gives the user folder whose code mods it is
built with). It is built with the same compilers, without the Android NDK: clang with lld,
Zig's (--fetch-compiler on Linux and macOS) or llvm-mingw's (Windows). Beside the native app's
library it writes its id (libmh4u_code_native.id): a hash of what the library's code depends on
in this version of the tools (code_library_id), which the app compares with its own before it
starts the game, so that a library made with another version is found before it crashes.
--print-id prints that id and does nothing else (the app's build puts it in the APK).

--romfs gives the base game's dump (.3ds/.cci) or a game folder's romfs.bin (repeatable; a .3ds/.cci --dump is looked
in too): the game's geometry shader programs in it (system/ltshader/LtShaderPackage.lfx) are compiled into the library
(tools/pica_native.py), which the native runtime's CPU vertex path then runs instead of interpreting them. Without it the
library has none and the runtime interprets them, as before. The generated C, like the recompiled code, stays in the
build directory.

On Windows it needs llvm-mingw's clang; --fetch-compiler downloads it (from the
llvm-mingw project on GitHub) into the output directory. On Linux and macOS it uses clang
when it's installed; otherwise --fetch-compiler downloads Zig's C compiler (the `ziglang`
package on PyPI, a self-contained clang with C library headers), e.g. on a Steam Deck.

Everything this writes contains game code and stays on your machine.
"""
from __future__ import annotations

import argparse
import glob
import hashlib
import json
import os
import shutil
import struct
import sys
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import code_mods  # noqa: E402
import codebin2elf  # noqa: E402
import exheader as exh  # noqa: E402
import extract_ncch  # noqa: E402
import mods  # noqa: E402
import pica_native  # noqa: E402
import recompile  # noqa: E402
import version_map  # noqa: E402
from recomp import build as rbuild  # noqa: E402
from recomp.analysis import Image  # noqa: E402

# What a code library's code depends on (code_library_id): the translator, the runtime it is compiled with and the tables of
# config/ it is translated with (functions, hook points, tunables). A change to any of them makes a library of another kind,
# whatever the game: every player makes theirs again, and the native app (GameFolderActivity) says so before it starts.
ID_FILES = ["tools/recompile.py", "tools/build_function_table.py", "tools/recomp/analysis.py", "tools/recomp/arm.py",
            "tools/recomp/build.py", "tools/recomp/emit.py", "tools/recomp/insn.py", "tools/recomp/thumb.py",
            "tools/recomp/runtime/recomp.h", "tools/recomp/runtime/rc_dll.c", "tools/recomp/runtime/android/fenv.h",
            "tools/recomp/runtime/android/math.h", "tools/recomp/runtime/android/string.h", "config/functions.tsv",
            "tools/pica_native.py", "tools/romfs_image.py", "tools/recomp/runtime/pica_native.h",
            "config/extra_entries.txt", "config/hooks.txt", "config/tunables.txt"]


def code_library_id(native: bool = True, deterministic: bool = False, root: str = ROOT) -> str:
    """16 hex digits: the hash of ID_FILES (their bytes, without carriage returns, so a checkout's line endings don't
    matter) and of the build mode. The same tools give the same id on every machine."""
    h = hashlib.sha256()
    for name in ID_FILES:
        h.update(name.encode() + b"\0")
        with open(os.path.join(root, name), "rb") as f:
            h.update(f.read().replace(b"\r", b""))
    h.update(" ".join(rbuild.native_defines(native, deterministic) or ["RC_PAGED"]).encode())
    return h.hexdigest()[:16]


def id_path(library: str) -> str:
    """The id file beside a library: libmh4u_code_native.so -> libmh4u_code_native.id."""
    return os.path.splitext(library)[0] + ".id"


def library_name(program_id: int, native: bool = False, deterministic: bool = False) -> str:
    """<program id>.<ext>; a library for the native runtime is <program id>_native.<ext> (or
    _native_det with --deterministic), so it can't be mistaken for the one Azahar loads."""
    ext = "dll" if sys.platform.startswith("win") else "dylib" if sys.platform == "darwin" else "so"
    return f"{program_id:016X}{rbuild.native_suffix(native, deterministic)}.{ext}"


LLVM_MINGW = "llvm-mingw-20241217-ucrt-x86_64"
LLVM_MINGW_URL = f"https://github.com/mstorsjo/llvm-mingw/releases/download/20241217/{LLVM_MINGW}.zip"
# the SHA-256 of that zip: the compiler runs on the player's PC, so it must be exactly this file
LLVM_MINGW_SHA256 = "f4f3ad8616c4183ce7b0d72df634400945b41ea9816145fc2430df6003455db7"
MINGW_CLANG = "x86_64-w64-mingw32-clang"


def default_user_dir() -> str:
    """Azahar's user directory on this platform (where it looks for recomp/)."""
    if sys.platform.startswith("win"):
        return os.path.join(os.environ.get("APPDATA", os.path.expanduser("~")), "Azahar")
    if sys.platform == "darwin":
        return os.path.expanduser("~/Library/Application Support/Azahar")
    data = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
    return os.path.join(data, "azahar-emu")


ZIG_VERSION = "0.13.0"
ZIG_INDEX = f"https://pypi.org/pypi/ziglang/{ZIG_VERSION}/json"
ZIG_WRAPPER = "zigcc"


def zig_wheel(index: dict, system: str, machine: str) -> dict:
    """The entry of PyPI's file list for the ziglang wheel that runs here."""
    machine = {"amd64": "x86_64", "arm64": "aarch64"}.get(machine.lower(), machine.lower())
    if system == "darwin":
        want = ("macosx", "x86_64" if machine == "x86_64" else "arm64")
    elif system == "linux":
        want = ("manylinux", machine)
    else:
        raise SystemExit(f"no Zig download for {system}")
    for f in index.get("urls", []):
        name = f.get("filename", "")
        if name.endswith(".whl") and all(w in name for w in want):
            return f
    raise SystemExit(f"no ziglang {ZIG_VERSION} wheel for {system} {machine}")


def zig_wrapper(toolchain_dir: str) -> str | None:
    """The `zig cc` wrapper of an unpacked Zig, if there is one."""
    path = os.path.join(toolchain_dir, ZIG_WRAPPER)
    return path if os.path.isfile(path) else None


def fetch_zig(toolchain_dir: str, index_url: str = ZIG_INDEX, system: str | None = None,
              machine: str | None = None) -> str:
    """Download the ziglang wheel from PyPI (checking its SHA-256), unpack it into
    toolchain_dir and write a `zigcc` script that runs `zig cc`; returns that script."""
    import hashlib
    import platform
    found = zig_wrapper(toolchain_dir)
    if found:
        return found
    os.makedirs(toolchain_dir, exist_ok=True)
    with urllib.request.urlopen(index_url) as r:
        index = json.load(r)
    entry = zig_wheel(index, system or sys.platform, machine or platform.machine())
    wheel = os.path.join(toolchain_dir, entry["filename"])
    print(f"downloading {entry['url']}")
    digest = hashlib.sha256()
    with urllib.request.urlopen(entry["url"]) as r, open(wheel + ".part", "wb") as f:
        total, done = int(r.headers.get("Content-Length") or 0), 0
        while chunk := r.read(1 << 20):
            f.write(chunk)
            digest.update(chunk)
            done += len(chunk)
            if total:
                print(f"\r  {done >> 20}/{total >> 20} MB", end="", flush=True)
    print()
    expected = entry.get("digests", {}).get("sha256")
    if expected and digest.hexdigest() != expected:
        os.remove(wheel + ".part")
        raise SystemExit(f"{entry['filename']}: SHA-256 doesn't match PyPI's")
    os.replace(wheel + ".part", wheel)
    print(f"unpacking {entry['filename']}")
    zig_dir = os.path.join(toolchain_dir, f"zig-{ZIG_VERSION}")
    with zipfile.ZipFile(wheel) as z:
        z.extractall(zig_dir)
    os.remove(wheel)
    zig = os.path.join(zig_dir, "ziglang", "zig")
    if not os.path.isfile(zig):
        raise SystemExit(f"{entry['filename']} doesn't contain ziglang/zig")
    os.chmod(zig, os.stat(zig).st_mode | 0o111)   # zip files don't keep the x bit
    wrapper = os.path.join(toolchain_dir, ZIG_WRAPPER)
    with open(wrapper, "w") as f:
        # zig cc turns on trapping undefined-behaviour checks at -O0 (the generated code is
        # plain C that relies on -fwrapv and friends, not on those checks), and it writes
        # debug info unless told not to: for the game's code, a build twice as long and a
        # library five times the size
        f.write(f'#!/bin/sh\nexec "{zig}" cc -fno-sanitize=undefined -g0 "$@"\n')
    os.chmod(wrapper, 0o755)
    return wrapper


def fetched_compilers(toolchain_dir: str) -> list[str]:
    exe = ".exe" if sys.platform.startswith("win") else ""
    return sorted(glob.glob(os.path.join(toolchain_dir, "*", "bin", MINGW_CLANG + exe)))


def fetch_compiler(toolchain_dir: str, source: str = LLVM_MINGW_URL, sha256: str | None = None) -> str:
    """Download and unpack llvm-mingw (a zip URL, or a local zip for tests) into
    toolchain_dir; returns its x86_64-w64-mingw32-clang. A download is refused unless its
    SHA-256 is sha256 (by default LLVM_MINGW_SHA256 for LLVM_MINGW_URL; another URL needs one)."""
    if sha256 is None and source == LLVM_MINGW_URL:
        sha256 = LLVM_MINGW_SHA256
    found = fetched_compilers(toolchain_dir)
    if found:
        return found[0]
    os.makedirs(toolchain_dir, exist_ok=True)
    archive = source
    if "://" in source:
        if not sha256:
            raise SystemExit(f"{source}: no SHA-256 to check the compiler against")
        archive = os.path.join(toolchain_dir, os.path.basename(source))
        print(f"downloading {source}")
        with urllib.request.urlopen(source) as r, open(archive + ".part", "wb") as f:
            total, done = int(r.headers.get("Content-Length") or 0), 0
            while chunk := r.read(1 << 20):
                f.write(chunk)
                done += len(chunk)
                if total:
                    print(f"\r  {done >> 20}/{total >> 20} MB", end="", flush=True)
        print()
        digest = hashlib.sha256()
        with open(archive + ".part", "rb") as f:
            while chunk := f.read(1 << 20):
                digest.update(chunk)
        if digest.hexdigest() != sha256.lower():
            os.remove(archive + ".part")
            raise SystemExit(f"{source}: its SHA-256 is {digest.hexdigest()}, not {sha256.lower()} "
                             "(a changed or damaged download): nothing was installed")
        os.replace(archive + ".part", archive)
    print(f"unpacking {os.path.basename(archive)}")
    with zipfile.ZipFile(archive) as z:
        z.extractall(toolchain_dir)
    if archive != source:
        os.remove(archive)
    found = fetched_compilers(toolchain_dir)
    if not found:
        raise SystemExit(f"{archive} doesn't contain bin/{MINGW_CLANG}")
    for path in glob.glob(os.path.join(os.path.dirname(found[0]), "*")):
        os.chmod(path, os.stat(path).st_mode | 0o111)   # zip files don't keep the x bit
    return found[0]


def find_android_compiler(requested: str | None, toolchain_dir: str, fetch: bool) -> str:
    """A compiler that builds the Android library: one that was asked for, llvm-mingw's clang on
    Windows (it has lld), a clang with ld.lld next to it, or Zig's (which brings its own lld)."""
    if requested:
        return requested
    if sys.platform.startswith("win"):
        return find_compiler(None, toolchain_dir, fetch)
    if shutil.which("clang") and shutil.which("ld.lld"):
        return "clang"
    if zig_wrapper(toolchain_dir) or fetch:
        return fetch_zig(toolchain_dir)
    raise SystemExit("no compiler for Android found: install clang and lld (LLVM), or run again with "
                     "--fetch-compiler to download Zig's C compiler")


def android_library_name(program_id: int, native: bool = False, deterministic: bool = False) -> str:
    """The Android library's name: <program id>.so for Azahar's app (recomp/ in its user folder),
    and for the native app the name it loads, libmh4u_code_native.so (or _native_det)."""
    return rbuild.android_soname(native, deterministic) if native else f"{program_id:016X}.so"


def find_compiler(requested: str | None, toolchain_dir: str, fetch: bool) -> str:
    if requested:
        return requested
    windows = sys.platform.startswith("win")
    # on Windows only llvm-mingw's clang links a DLL without Visual Studio
    candidates = [MINGW_CLANG] if windows else ["clang", "cc"]
    for c in candidates:
        if shutil.which(c):
            return c
    if windows:
        if fetched_compilers(toolchain_dir) or fetch:
            return fetch_compiler(toolchain_dir)
        raise SystemExit("no C compiler found: run again with --fetch-compiler to download llvm-mingw "
                         "(or install it from https://github.com/mstorsjo/llvm-mingw/releases and put its "
                         "bin folder on PATH)")
    if zig_wrapper(toolchain_dir) or fetch:
        return fetch_zig(toolchain_dir)
    raise SystemExit("no C compiler found: install clang (LLVM), or run again with --fetch-compiler to "
                     "download Zig's C compiler")


def check_version(header: exh.ExHeader, code: bytes, versions_path: str | None = None) -> dict | None:
    with open(versions_path or os.path.join(ROOT, "config", "versions.json")) as f:
        versions = json.load(f)["versions"]
    sha1 = hashlib.sha1(code).hexdigest()
    for v in versions:
        if v.get("code_sha1") == sha1:
            return v
    return None


def buildable(version: dict) -> bool:
    """The tables are for the target version; another one needs its shifts (tools/version_map.py)."""
    return bool(version.get("target") or version.get("shifts"))


def translate_tables(version: dict, entries: dict, hooks: dict, tunables: dict) -> tuple[dict, dict, dict]:
    """The target version's entry points, hooks and tunables at this version's addresses.
    Entry points that have none are left out (functions this version changed: the recompiler
    finds them through their callers); a hook or tunable without one is an error."""
    if version.get("target") or not version.get("shifts"):
        return entries, hooks, tunables
    shifts = version_map.Shifts.load(os.path.join(ROOT, version["shifts"]))
    entries, dropped = shifts.translate_keys(entries)
    hooks, missing_hooks = shifts.translate_keys(hooks)
    tunables, missing_tunables = shifts.translate_keys(tunables)
    print(f"addresses from {version['shifts']}: {len(entries)} entry points "
          f"({len(dropped)} in code this version changed, left to the analysis)")
    if missing_hooks or missing_tunables:
        raise SystemExit("no address in this version for "
                         + ", ".join([f"hook 0x{a:08x}" for a in missing_hooks]
                                     + [f"tunable 0x{a:08x}" for a in missing_tunables])
                         + f" ({version['shifts']}; rebuild it with tools/version_map.py)")
    return entries, hooks, tunables


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dump", help="decrypted .cia / .3ds / .cci / .cxi of your own copy")
    ap.add_argument("--print-id", action="store_true",
                    help="print the native app's code library id for these tools (with --deterministic, the _det "
                         "library's) and exit")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "game"))
    ap.add_argument("--opt", default="O1", help="O0 builds fast, O1/O2 run faster")
    ap.add_argument("--cc", help="C compiler (default: clang, or x86_64-w64-mingw32-clang on Windows)")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--install", metavar="AZAHAR_USER_DIR", nargs="?", const=default_user_dir(),
                    help="copy the library to AZAHAR_USER_DIR/recomp/; without a value, to Azahar's "
                         f"usual user directory ({default_user_dir()})")
    ap.add_argument("--native", action="store_true",
                    help="build the library for the native runtime (RC_NATIVE) instead of Azahar's "
                         "backend; Azahar refuses it")
    ap.add_argument("--deterministic", action="store_true",
                    help="with --native: charge cycles at every yield point (RC_NATIVE_DETERMINISTIC), "
                         "for reproducible scheduling")
    ap.add_argument("--fetch-compiler", action="store_true",
                    help="no compiler installed: download llvm-mingw (Windows), or Zig's C compiler "
                         "(Linux, macOS; used only without clang), into the output directory")
    ap.add_argument("--compiler-zip", default=LLVM_MINGW_URL, help=argparse.SUPPRESS)
    ap.add_argument("--android", action="store_true",
                    help="build the library for the port's Android app (arm64) instead of this PC; with "
                         "--native, for the native app (libmh4u_code_native.so)")
    ap.add_argument("--any-version", action="store_true", help="don't insist on a known code.bin")
    ap.add_argument("--mods", metavar="AZAHAR_USER_DIR",
                    help="apply the code mods in AZAHAR_USER_DIR/load/mods/<program id>/ (default: the "
                         "--install directory, if any)")
    ap.add_argument("--no-mods", action="store_true", help="build from the unmodified code")
    ap.add_argument("--romfs", action="append", default=[], metavar="DUMP_OR_ROMFS",
                    help="the base game's dump (.3ds/.cci) or a game folder's romfs.bin: its geometry shaders are compiled "
                         "into the library (repeatable)")
    ap.add_argument("--functions", default=os.path.join(ROOT, "config", "functions.tsv"), help=argparse.SUPPRESS)
    ap.add_argument("--extra", default=os.path.join(ROOT, "config", "extra_entries.txt"), help=argparse.SUPPRESS)
    ap.add_argument("--hooks", default=os.path.join(ROOT, "config", "hooks.txt"), help=argparse.SUPPRESS)
    ap.add_argument("--tunables", default=os.path.join(ROOT, "config", "tunables.txt"), help=argparse.SUPPRESS)
    ap.add_argument("--versions", default=os.path.join(ROOT, "config", "versions.json"), help=argparse.SUPPRESS)
    args = ap.parse_args(argv)
    if args.print_id:
        print(code_library_id(True, args.deterministic))
        return 0
    if not args.dump:
        ap.error("--dump is required")
    modes = rbuild.native_defines(args.native, args.deterministic)   # exits on --deterministic alone
    if args.android and args.install and args.native:
        raise SystemExit("with --native --android, --install has no folder to go to: copy "
                         f"{rbuild.android_soname(args.native, args.deterministic)} to the native app's folder "
                         "yourself (and give --mods the user folder whose code mods it should have)")
    if args.android and args.install == default_user_dir():
        raise SystemExit("with --android, --install takes the device's Azahar folder (synced or mounted): "
                         "this PC's Azahar can't use an Android library")

    dump_dir = os.path.join(args.out, "dump")
    print(f"extracting {args.dump}")
    extract_ncch.extract(args.dump, dump_dir)
    with open(os.path.join(dump_dir, "exheader.bin"), "rb") as f:
        exheader_bytes = f.read()
    header = exh.parse(exheader_bytes)
    with open(os.path.join(dump_dir, "code.bin"), "rb") as f:
        code = codebin2elf.load_code(header, f.read())
    version = check_version(header, code, args.versions)
    if version:
        print(f"version: {version['name']}")
        if not buildable(version) and not args.any_version:
            raise SystemExit(f"{version['name']} isn't one the port runs: use the USA v1.3.0 or the "
                             "EUR v1.1.0 update .cia")
    elif not args.any_version:
        raise SystemExit("this code.bin isn't a version the project knows (config/versions.json); "
                         "use the USA v1.3.0 or the EUR v1.1.0 update .cia")
    program_id = struct.unpack_from("<Q", exheader_bytes, 0x200)[0]

    mods_dir = None if args.no_mods else (args.mods or args.install)
    used, changes_code = [], False
    unmodded_code = code
    if mods_dir:
        modded, used = code_mods.apply_mods(code, mods_dir, program_id, header.bss_size)
        if used:
            segments = [("code", 0, header.text.size),
                        ("read-only data", header.ro.address - header.text.address, header.ro.size),
                        ("data", header.data.address - header.text.address, header.data.size)]
            print("code mods: " + ", ".join(used))
            for line in code_mods.describe(code, modded, segments):
                print(f"  {line}")
            cave = code_mods.changed_bytes(code, modded, header.text.size, header.text.padded_size - header.text.size)
            if cave:
                print(f"  after the code, in its last page: {cave} bytes changed (new code)")
            changes_code = code_mods.changed_bytes(code, modded, 0, header.text.padded_size) > 0
            if changes_code:
                print("  the mod changes the game's code: this library runs only with the same mod in "
                      "Azahar's mods folder (build again after adding, changing or removing it)")
            code = modded

    out = os.path.join(args.out, "recomp")
    print("recompiling (a couple of minutes)")
    size = recompile.code_size(header, code)
    if size != header.text.size:
        print(f"  the code mod puts new code after the game's own: recompiling 0x{size:X} bytes "
              f"(to the end of the code's last page) instead of 0x{header.text.size:X}")
    # a code mod's new code in the read-only data (some mods put their routines there, which only an
    # emulator runs as it is): the runs of bytes the mod changed there, 16 bytes or longer, may hold code
    extra_code = []
    if changes_code and unmodded_code is not None:
        ro_start = header.ro.address - header.text.address
        ro_end = ro_start + header.ro.padded_size  # (the padding after it too: a mod may put code there)
        run = None
        for off in range(ro_start, min(ro_end, len(code), len(unmodded_code))):
            if code[off] != unmodded_code[off]:
                run = [off, off + 1] if run is None or off - run[1] > 8 else [run[0], off + 1]
                if not extra_code or extra_code[-1][0] != run[0]:
                    extra_code.append(tuple(run))
                else:
                    extra_code[-1] = tuple(run)
        extra_code = [(header.text.address + s, header.text.address + e) for s, e in extra_code if e - s >= 16]
        if extra_code:
            print(f"  the code mod changes {len(extra_code)} stretches of the read-only data that may hold code: "
                  "recompiled too where the code reaches them")
    img = Image(code, header.text.address, size, extra_code)
    entries, hooks, tunables = translate_tables(
        version or {}, recompile.load_entries(args.functions, [args.extra] if args.extra else []),
        recompile.load_hooks(args.hooks), recompile.load_tunables(args.tunables))
    segs = [(header.ro.address, header.ro.size), (header.data.address, header.data.size)]
    res = recompile.recompile(img, entries, out, data_segments=segs, title_id=program_id,
                              hooks=hooks, tunables=tunables, log=lambda m: print(m, flush=True))
    print(f"{res['report']['functions']} functions, {res['report']['hook_points']} hook points")
    for p in res["problems"]:
        print(f"  warning: {p}")

    # the game's geometry shaders as C beside the recompiled code (a stale file from an earlier build goes)
    shader_c = os.path.join(out, "src", "pica_native.c")
    romfs_sources = list(args.romfs)
    if args.dump and os.path.splitext(args.dump)[1].lower() in (".3ds", ".cci"):
        romfs_sources.append(args.dump)
    package = None
    if romfs_sources:
        try:
            package = pica_native.find_package(romfs_sources)
        except (OSError, ValueError) as e:
            print(f"  warning: the RomFS could not be read ({e}): the geometry shaders stay interpreted")
    if package is not None:
        text, functions, skipped = pica_native.generate(package)
        with open(shader_c, "w", encoding="ascii", newline="\n") as f:
            f.write(text)
        print(f"geometry shaders: {functions} entry points compiled in ({skipped} left to the interpreter)")
    else:
        if os.path.exists(shader_c):
            os.remove(shader_c)
        print("geometry shaders: interpreted (give --romfs with the base game's dump or romfs.bin to compile them in)")

    toolchain_dir = os.path.join(args.out, "toolchain")
    if args.fetch_compiler and not args.cc and sys.platform.startswith("win"):
        cc = fetch_compiler(toolchain_dir, args.compiler_zip)
    elif args.android:
        cc = find_android_compiler(args.cc, toolchain_dir, args.fetch_compiler)
    else:
        cc = find_compiler(args.cc, toolchain_dir, args.fetch_compiler)
    name = (android_library_name if args.android else library_name)(program_id, args.native, args.deterministic)
    print(f"compiling with {cc} -{args.opt}{' for Android' if args.android else ''} ({args.jobs} jobs; "
          "-O1 takes a while)")
    lib = rbuild.build(out, args.jobs, args.opt, modes or ["RC_PAGED"], os.path.join("android", name) if args.android
                       else name, [rbuild.DLL_GLUE], cc=cc, extra_flags=["-fvisibility=hidden"], android=args.android)
    print(f"code library: {lib}")
    if args.android and args.native:  # (the native app checks it: GameFolderActivity)
        with open(id_path(lib), "w", encoding="ascii") as f:
            f.write(code_library_id(True, args.deterministic) + "\n")
        print(f"its id, for the app: {id_path(lib)}")
    if args.android and not args.install:
        print(f"next: copy it to your device, into the native app's folder as {name}" if args.native else
              f"next: copy it to your device, into Azahar's user folder as recomp/{name}")
    if args.install:
        dest_dir = os.path.join(args.install, "recomp")
        os.makedirs(dest_dir, exist_ok=True)
        shutil.copy2(lib, os.path.join(dest_dir, name))
        # what it was built from, for tools/mods.py status (docs/MODS.md)
        mods.write_build_record(os.path.join(dest_dir, name), used, header.text.size, changes_code, program_id,
                                code_size=size)
        print(f"installed to {os.path.join(dest_dir, name)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
