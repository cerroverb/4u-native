#!/usr/bin/env python3
"""The on-phone builder's tools for the APK: mh4n's tools/ and config/ (what tools/build_game.py and
tools/android/phone_build.py read) into android/app/src/main/assets/builder/tools.zip, from the mh4n the APK is built
with, and the check that the library they make on a phone has the id the app wants (assets/code_library_id.txt).

    python scripts/pack_builder_tools.py MH4N_DIR
    python scripts/pack_builder_tools.py --pc MH4N_DIR OUT.zip ID

--pc (scripts/build_pc.sh): the same files into OUT.zip, builder_tools.zip beside MH4U.exe, for the PC's own builder
(app_native/pc/pc_builder.cpp, mh4n tools/pc_build.py), checked to make the PC app's library id (ID: build_game.py
--print-id --pc).

No game data is in there (mh4n's tools/check_no_game_data.py checks the same tree for its releases). The files are
stored in a fixed order with a fixed date, so the same mh4n gives the same zip.
"""
import os
import sys
import tempfile
import time
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(os.path.dirname(HERE), "android", "app", "src", "main", "assets")
# what the phone doesn't run: the PC's research and test tools
SKIP_DIRS = {"__pycache__", "ghidra", "native", "harness", "tests"}


def files(mh4n):
    for top in ("tools", "config"):
        for root, dirs, names in os.walk(os.path.join(mh4n, top)):
            dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS)
            for n in sorted(names):
                if not n.endswith((".pyc", ".exe", ".dll", ".so")):
                    path = os.path.join(root, n)
                    yield path, os.path.relpath(path, mh4n).replace(os.sep, "/")


def main():
    pc = sys.argv[1] == "--pc"
    mh4n = os.path.abspath(sys.argv[2] if pc else sys.argv[1])
    if pc:
        out = os.path.abspath(sys.argv[3])
    else:
        os.makedirs(os.path.join(ASSETS, "builder"), exist_ok=True)
        out = os.path.join(ASSETS, "builder", "tools.zip")
    with zipfile.ZipFile(out + ".part", "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for path, name in files(mh4n):
            info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(path, "rb") as f:
                z.writestr(info, f.read())
    for attempt in range(10):  # (Windows: an antivirus or a Gradle daemon may hold the old zip open for a moment)
        try:
            os.replace(out + ".part", out)
            break
        except PermissionError:
            if attempt == 9:
                raise
            time.sleep(2)
    # the id the zipped tools give, as the phone computes it
    with tempfile.TemporaryDirectory() as tmp:
        with zipfile.ZipFile(out) as z:
            z.extractall(tmp)
        sys.path.insert(0, os.path.join(tmp, "tools"))
        import build_game
        if pc:
            got = build_game.code_library_id(True, False, root=tmp, pc=True)
        else:
            got = build_game.code_library_id(True, False, root=tmp)
    if pc:
        want = sys.argv[4]
    else:
        with open(os.path.join(ASSETS, "code_library_id.txt")) as f:
            want = f.read().strip()
    if got != want:
        sys.exit(f"builder tools: their library id is {got}, the app's is {want}")
    print(f"builder tools: {out} ({os.path.getsize(out) >> 10} KB), library id {got}")


if __name__ == "__main__":
    main()
