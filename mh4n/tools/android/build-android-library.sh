#!/bin/sh
# Builds the game's code library for the MH4U Android app from YOUR Monster Hunter 4 Ultimate USA v1.3.0 update
# (.cia), on Linux or macOS: the same as "Build MH4U library for Android.bat" on Windows.
#
#   sh build-android-library.sh UPDATE.cia [GAME.3ds|GAME.cci] [--mods FOLDER]
#
# GAME, the base game, is optional but makes a faster library: its geometry shaders are compiled into it. A .3ds or
# .cci beside the .cia is taken by itself. --mods is the mods folder you put on the phone (with 0004000000126300 in
# it, or a mod's folder with 0004000000126300 inside), or a mod's own folder (with exefs in it): a mod that changes
# the game's code (the single-screen mod does) has to be built into the library.
#
# It needs Python 3.10 or newer. It uses clang and ld.lld if they are installed; otherwise it downloads Zig's C
# compiler (from PyPI, checked against its SHA-256) into build/android-game/toolchain. Everything it makes from the
# game stays on this computer; libmh4u_code_native.so and its .id are written next to the .cia.
set -e
usage() { echo "usage: sh build-android-library.sh UPDATE.cia [GAME.3ds|GAME.cci] [--mods FOLDER]" >&2; exit 2; }

CIA= GAME= MODS=
while [ $# -gt 0 ]; do
    case "$1" in
        --mods) [ $# -ge 2 ] || usage; MODS=$2; shift 2 ;;
        -h|--help) usage ;;
        *) if [ -z "$CIA" ]; then CIA=$1; elif [ -z "$GAME" ]; then GAME=$1; else usage; fi; shift ;;
    esac
done
[ -n "$CIA" ] || usage
[ -f "$CIA" ] || { echo "Can't find $CIA" >&2; exit 1; }

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
TOOL="$ROOT/tools/build_game.py"
PY=
for p in python3 python; do
    if command -v "$p" >/dev/null 2>&1 && "$p" -c 'import sys; sys.exit(sys.version_info < (3, 10))' 2>/dev/null; then
        PY=$p; break
    fi
done
[ -n "$PY" ] || { echo "Python 3.10 or newer is needed (python3)." >&2; exit 1; }

CIA_DIR=$(cd "$(dirname "$CIA")" && pwd)
if [ -z "$GAME" ]; then
    for f in "$CIA_DIR"/*.3ds "$CIA_DIR"/*.cci "$CIA_DIR"/*.3DS "$CIA_DIR"/*.CCI; do
        [ -f "$f" ] || continue
        if [ -n "$GAME" ]; then GAME=; echo "More than one game beside the .cia: give it as the second argument." >&2; break; fi
        GAME=$f
    done
fi

OUT="$ROOT/build/android-game"
set -- "$TOOL" --dump "$CIA" --native --android --fetch-compiler --out "$OUT"
if [ -n "$GAME" ]; then
    [ -f "$GAME" ] || { echo "Can't find $GAME" >&2; exit 1; }
    echo "With the game's shaders from $GAME"
    set -- "$@" --romfs "$GAME"
fi
if [ -n "$MODS" ]; then
    # build_game.py reads the code mods as the app does, from <user dir>/load/mods/<title>: a folder made for it
    TITLE=$(find "$MODS" -mindepth 1 -maxdepth 2 -type d \( -name 0004000000126300 -o -name 0004000E00126300 \
            -o -name 0004000e00126300 \) | sort | head -n 1)
    if [ -z "$TITLE" ] && [ -d "$MODS/exefs" ]; then TITLE=$MODS; fi
    [ -n "$TITLE" ] || { echo "There is no 0004000000126300 or exefs folder in $MODS." >&2; exit 1; }
    USER_DIR="$ROOT/build/android-mods"
    rm -rf "$USER_DIR"
    mkdir -p "$USER_DIR/load/mods"
    cp -R "$TITLE" "$USER_DIR/load/mods/0004000000126300"
    echo "With the mods of $TITLE"
    set -- "$@" --mods "$USER_DIR"
else
    set -- "$@" --no-mods
fi

echo "Building the library from $CIA (a few minutes)"
"$PY" "$@"
LIB="$OUT/recomp/android/libmh4u_code_native.so"
[ -f "$LIB" ] || { echo "Can't find $LIB" >&2; exit 1; }
cp "$LIB" "$CIA_DIR/libmh4u_code_native.so"
cp "$OUT/recomp/android/libmh4u_code_native.id" "$CIA_DIR/libmh4u_code_native.id"
echo
echo "Done: $CIA_DIR/libmh4u_code_native.so"
echo "Copy that library and its .id to the phone, into the folder with the game and the update."
