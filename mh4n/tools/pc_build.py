#!/usr/bin/env python3
"""Builds the game's code library for PC INSIDE MH4U.exe (the game folder screen's "Build the game code on this PC", and
F1, System, Game code): the same tools/build_game.py a player runs by hand (--pc), with Zig's C compiler instead of
llvm-mingw (--cc: the program's zigcc.exe, which runs `zig cc` for Windows) and a work folder that is kept (--work), so
that a run cut short goes on from where it stopped. The PC's counterpart of tools/android/phone_build.py, whose "@@"
lines for the program, stages, progress and resume stamp it shares (it imports them).

    python pc_build.py --update UPDATE.cia [--game GAME.cci] [--game-mods FOLDER | --user-mods USER] --work DIR
                       --dest FOLDER --cc ZIGCC [--jobs N] [--expect-id ID] [--clean]

MH4U.exe (app_native/pc/pc_builder.cpp) runs it with the Python it downloaded (python.org's embeddable package for
Windows) and Zig (PyPI's ziglang 0.13.0 for Windows, or ziglang.org's), below normal priority, with all the processor's
cores but one. The code mods built in, as the game will run them (the player's choice, as on Android): --game-mods, the
game folder's mods/, or --user-mods, the user folder whose load/mods has the mods that are on; neither, none.

Its output is build_game.py's, with phone_build.py's @@ lines (@@pid, @@id, @@stage, @@progress, @@error, @@done).
Resuming is phone_build.py's too: once the translation is through, the work folder's inputs.txt keeps a hash of what it
was made from (the update and the game, the code mods, these tools and the Python) and of the C it wrote; a run whose
inputs give the same goes straight to compiling the objects that are missing (an object cut short by the last run is
made again). The library and its .id reach --dest only whole (written beside their place, then renamed).

Everything this writes is made from the player's own game and stays on the PC.
"""
from __future__ import annotations

import argparse
import errno
import os
import shutil
import struct
import sys
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "android"))

import build_game  # noqa: E402
import phone_build as pb  # noqa: E402 (the @@ lines, the stages and the resume stamp, shared with the phone's)

LIBRARY = build_game.pc_library_name()
# room the work folder needs: the recompiled C (about 330 MB), its objects (~140 MB), Zig's cache (its copies of them and
# the MinGW-w64 runtime it builds once) and the library (~180 MB)
WORK_SPACE = 1200 << 20


def can_write(folder: str) -> bool:
    """A file can be made in the folder (on Windows os.access reads only the read-only flag)."""
    probe = os.path.join(folder, f".mh4u_write_test_{os.getpid()}")
    try:
        with open(probe, "wb") as f:
            f.write(b"0")
        os.remove(probe)
        return True
    except OSError:
        return False


def whole_object(path: str) -> bool:
    """A COFF object (Zig's for Windows) whose sections and symbol table are all inside the file: one cut short while it
    was written isn't. An ELF one as phone_build.py checks it."""
    try:
        size = os.path.getsize(path)
        with open(path, "rb") as f:
            head = f.read(20)
            if head[:4] == b"\x7fELF":
                return pb.whole_object(path)
            if len(head) < 20:
                return False
            machine, sections, _, symtab, symbols, opt, _ = struct.unpack("<HHIIIHH", head)
            if machine != 0x8664 or opt != 0 or sections == 0:
                return False
            table = f.read(40 * sections)
            if len(table) < 40 * sections:
                return False
            for i in range(sections):
                raw_size, raw_at = struct.unpack_from("<II", table, i * 40 + 16)
                if raw_at and raw_at + raw_size > size:
                    return False
            if symtab:
                strings = symtab + 18 * symbols
                if strings + 4 > size:
                    return False
                f.seek(strings)
                (strings_size,) = struct.unpack("<I", f.read(4))
                if strings + strings_size > size:
                    return False
        return True
    except OSError:
        return False


def drop_broken_objects(obj_dir: str) -> int:
    dropped = 0
    if os.path.isdir(obj_dir):
        for name in os.listdir(obj_dir):
            path = os.path.join(obj_dir, name)
            if name.endswith(".o") and not whole_object(path):
                os.remove(path)
                dropped += 1
    return dropped


def install(lib: str, dest_dir: str) -> str:
    """The library and its .id into the game folder, each written beside its place first and then renamed: a cut leaves
    the old pair or the new one, never half of one."""
    pairs = [(lib, os.path.join(dest_dir, LIBRARY)),
             (build_game.id_path(lib), os.path.join(dest_dir, build_game.id_path(LIBRARY)))]
    for src, dst in pairs:
        part = dst + ".part"
        shutil.copyfile(src, part)
        with open(part, "rb+") as f:
            os.fsync(f.fileno())
        os.replace(part, dst)
    return pairs[0][1]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--update", required=True, help="the decrypted update .cia")
    ap.add_argument("--game", help="the base game's decrypted .3ds/.cci: its geometry shaders go into the library")
    mods_from = ap.add_mutually_exclusive_group()
    mods_from.add_argument("--game-mods", help="the game folder's mods/: its code mods, as the game runs them")
    mods_from.add_argument("--user-mods", help="the user folder: the code mods that are on in its load/mods")
    ap.add_argument("--work", required=True, help="the work folder (kept between runs to go on where one stopped)")
    ap.add_argument("--dest", required=True, help="the folder the library and its .id go to (the game folder)")
    ap.add_argument("--cc", required=True, help="the C compiler (the program's zigcc.exe)")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    ap.add_argument("--expect-id", default="", help="the id MH4U.exe wants: anything else stops before the long part")
    ap.add_argument("--clean", action="store_true", help="once the library is in place, delete the work folder")
    args = ap.parse_args(argv)

    t0 = time.time()
    tap = pb.Stages()
    pb.emit(f"@@pid {os.getpid()}")
    lib_id = build_game.code_library_id(True, False, pc=True)
    pb.emit(f"@@id {lib_id}")
    if args.expect_id and args.expect_id != lib_id:
        pb.emit(f"@@error tools these tools make libraries with id {lib_id}, the program wants {args.expect_id}")
        return 1
    try:
        os.makedirs(args.work, exist_ok=True)
        free = shutil.disk_usage(args.work).free
        out = os.path.join(args.work, "game")
        recomp = os.path.join(out, "recomp")
        have = sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(args.work) for f in fs)
        if free + have < WORK_SPACE:
            pb.emit(f"@@error space {(WORK_SPACE - have - free) >> 20} MB more free space needed")
            return 1
        dropped = drop_broken_objects(os.path.join(recomp, "obj"))
        if dropped:
            print(f"{dropped} objects cut short by an earlier run: made again")
        if not can_write(args.dest):
            pb.emit(f"@@error dest can't write in {args.dest}")
            return 1

        # the stages before the compile: kept from the last run if they were made from the same files and tools
        if args.game_mods:
            mods = f"game-mods {args.game_mods} {pb.mods_digest(args.game_mods)}"
        elif args.user_mods:
            mods = f"user-mods {args.user_mods} {pb.mods_digest(os.path.join(args.user_mods, 'load', 'mods'))}"
        else:
            mods = "mods none"
        lines = [f"tools {lib_id} {pb.tools_digest()}", "python " + " ".join(sys.version.split()),
                 f"update {args.update} {pb.any_digest(args.update)}", f"game {args.game} {pb.any_digest(args.game)}", mods]
        inputs = pb.stamp_hash(lines)
        kept_inputs, kept_made = pb.read_stamp(args.work)
        reuse = kept_inputs == inputs and kept_made != "none" and kept_made == pb.written(out)
        if reuse:
            print(f"inputs as the last run's ({time.time() - t0:.1f} s to check): its translation is kept, on to the compile")
        else:
            pb.drop_stamp(args.work)
            if kept_inputs:
                print("the last run's translation isn't for these inputs (or its C is gone): translating again")

        cmd = ["--pc", "--dump", args.update, "--cc", args.cc, "--out", out, "-j", str(args.jobs)]
        if args.game:
            cmd += ["--romfs", args.game]
        if args.game_mods and os.path.isdir(args.game_mods):
            cmd += ["--game-mods", args.game_mods]
            print(f"code mods of {args.game_mods}")
        elif args.user_mods and os.path.isdir(os.path.join(args.user_mods, "load", "mods")):
            cmd += ["--mods", args.user_mods]
            print(f"code mods that are on in {os.path.join(args.user_mods, 'load', 'mods')}")
        else:
            cmd += ["--no-mods"]
        tap.set("extract")
        if reuse:
            cmd += ["--compile-only"]
        else:
            pb.translation_progress(tap)
        progress = pb.CompileProgress(recomp)

        def on_stage(stage):
            if stage != "compile" or progress.is_alive():
                return
            if not reuse:  # (the stages before it are through: what they were made from, and the C they wrote)
                pb.write_stamp(args.work, inputs, pb.written(out), lines)
            print(f"compiling from {time.time() - t0:.1f} s after the start")
            progress.start()
        tap.on_stage = on_stage
        sys.stdout = tap
        try:
            errors = pb.Tail()
            sys.stderr = errors
            try:
                build_game.main(cmd)
            except SystemExit as e:
                # (Zig's cache naming an object it no longer has, as phone_build.py: emptied, the compile once more)
                zig_cache = os.environ.get("ZIG_GLOBAL_CACHE_DIR") or os.path.join(args.work, "zig-cache")
                if e.code in (0, None) or tap.stage not in ("compile", "link") or "zig-cache" not in errors.text or \
                        not os.path.isdir(zig_cache):
                    raise
                print("Zig's cache names a file it no longer has: emptied, the compile goes on")
                shutil.rmtree(zig_cache, ignore_errors=True)
                build_game.main(cmd if "--compile-only" in cmd else cmd + ["--compile-only"])
        finally:
            progress.stop.set()
            sys.stdout, sys.stderr = sys.__stdout__, sys.__stderr__
        lib = os.path.join(recomp, "pc", LIBRARY)
        tap.set("install")
        placed = install(lib, args.dest)
        if args.clean:
            shutil.rmtree(args.work, ignore_errors=True)
        pb.emit(f"@@done {placed}")
        return 0
    except SystemExit as e:
        if e.code in (0, None):
            return 0
        message = str(e.code) if not isinstance(e.code, int) else f"stopped with code {e.code}"
        pb.emit(f"@@error {tap.stage} {' '.join(message.split())}")
        return 1
    except OSError as e:
        traceback.print_exc()
        kind = "space" if e.errno == errno.ENOSPC or getattr(e, "winerror", 0) in (112, 39) else tap.stage
        pb.emit(f"@@error {kind} {e}")
        return 1
    except MemoryError:
        pb.emit(f"@@error memory {tap.stage}")
        return 1
    except Exception as e:  # noqa: BLE001 (any other failure: the program shows it, the log has the traceback)
        traceback.print_exc()
        pb.emit(f"@@error {tap.stage} {type(e).__name__}: {' '.join(str(e).split())}")
        return 1


if __name__ == "__main__":
    t0 = time.time()
    code = main()
    print(f"({time.time() - t0:.0f} s)")
    sys.exit(code)
