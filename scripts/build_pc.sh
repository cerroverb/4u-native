#!/bin/bash
# Builds MH4U for PC from this tree: MH4U.exe (Windows x64) with llvm-mingw, its builder's zigcc.exe, and the program's
# folder as a player unpacks it, build/pc/MH4U (app_native/pc/make_folder.sh); with PACK=1 also the release's zip,
# build/pc/MH4U-PC-<version>.zip, and its .sha256. SDL 3.4.18 is built from its release the first time (build/sdl3-win).
#
#   bash scripts/build_pc.sh            (in Git Bash on Windows)
#   PACK=1 bash scripts/build_pc.sh
#
# Needs llvm-mingw 20241217 for x86_64 with the UCRT (llvm-mingw-20241217-ucrt-x86_64.zip from
# https://github.com/mstorsjo/llvm-mingw/releases; LLVM_MINGW=<its folder>), CMake 3.25 or newer and Ninja (on PATH, or
# CMAKE and NINJA; pip install cmake ninja works), Python 3.10 or newer (python, or PYTHON), and curl and tar (Git Bash
# has them). TYPE=RelWithDebInfo keeps the symbols; the default is a Release at -O2 without them.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
m() { cygpath -m "$1"; }
[ -n "${LLVM_MINGW:-}" ] || { echo "Set LLVM_MINGW to llvm-mingw 20241217's folder (llvm-mingw-20241217-ucrt-x86_64)." >&2; exit 1; }
TC=$(cygpath -u "$LLVM_MINGW")
[ -f "$TC/bin/x86_64-w64-mingw32-clang.exe" ] || { echo "No x86_64-w64-mingw32-clang.exe in $TC/bin" >&2; exit 1; }
CMAKE=${CMAKE:-cmake}
NINJA=${NINJA:-ninja}
command -v "$CMAKE" >/dev/null 2>&1 || { echo "CMake 3.25 or newer is needed on PATH (or set CMAKE): pip install cmake" >&2; exit 1; }
command -v "$NINJA" >/dev/null 2>&1 || { echo "Ninja is needed on PATH (or set NINJA): pip install ninja" >&2; exit 1; }
NINJA=$(m "$(command -v "$NINJA")")
if [ -z "${PYTHON:-}" ]; then
    for p in python python3; do
        if command -v "$p" >/dev/null 2>&1 && "$p" -c 'import sys; sys.exit(sys.version_info < (3, 10))' 2>/dev/null; then
            PYTHON=$p; break
        fi
    done
fi
[ -n "${PYTHON:-}" ] || { echo "Python 3.10 or newer is needed (python, or set PYTHON)." >&2; exit 1; }
MC=$(m "$TC")/bin
COMPILERS=(-DCMAKE_C_COMPILER="$MC/x86_64-w64-mingw32-clang.exe" -DCMAKE_CXX_COMPILER="$MC/x86_64-w64-mingw32-clang++.exe"
           -DCMAKE_ASM_COMPILER="$MC/x86_64-w64-mingw32-clang.exe" -DCMAKE_RC_COMPILER="$MC/x86_64-w64-mingw32-windres.exe"
           -DCMAKE_AR="$MC/llvm-ar.exe" -DCMAKE_RANLIB="$MC/llvm-ranlib.exe")

# SDL 3.4.18 for Windows, from its release (the same files as the Android build's), once
SDL_VERSION=3.4.18
SDL_SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
WORK="$ROOT/build/sdl3-win"
SDL3="$WORK/out"
if [ ! -f "$SDL3/.built" ]; then
    mkdir -p "$WORK"
    SRC="$WORK/SDL3-$SDL_VERSION"
    if [ ! -d "$SRC" ]; then
        TARBALL="$WORK/SDL3-$SDL_VERSION.tar.gz"
        curl -fL -o "$TARBALL" "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL3-$SDL_VERSION.tar.gz"
        GOT=$(sha256sum "$TARBALL" | cut -d' ' -f1)
        [ "$GOT" = "$SDL_SHA256" ] || { echo "$TARBALL: SHA-256 $GOT, expected $SDL_SHA256" >&2; rm -f "$TARBALL"; exit 1; }
        tar -xzf "$TARBALL" -C "$WORK"
        rm -f "$TARBALL"
    fi
    # (SDL3 is C: its DLL depends on neither libc++ nor libunwind)
    "$CMAKE" -S "$(m "$SRC")" -B "$(m "$WORK/build")" -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA" -DCMAKE_SYSTEM_NAME=Windows \
        -DCMAKE_SYSTEM_PROCESSOR=AMD64 "${COMPILERS[@]}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$(m "$SDL3")" \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
        -DCMAKE_C_FLAGS="-ffile-prefix-map=$(m "$SRC")/=SDL3/" -DCMAKE_SHARED_LINKER_FLAGS="-static-libgcc -static-libstdc++"
    "$CMAKE" --build "$(m "$WORK/build")" --target install --parallel
    touch "$SDL3/.built"
fi

# the code library this build runs with: the game folder screen refuses a library made with other tools
ID=$("$PYTHON" "$(m "$ROOT/mh4n/tools/build_game.py")" --print-id --pc)
echo "PC code library id $ID"
B="$ROOT/build/pc"
TYPE=${TYPE:-Release}
# (no paths of this computer in the program: sources as app_native/..., mh4n/..., the compiler's as llvm-mingw/...)
MAP="-ffile-prefix-map=$(m "$ROOT")/= -ffile-prefix-map=$(m "$TC")=llvm-mingw"
"$CMAKE" -S "$(m "$ROOT/app_native")" -B "$(m "$B")" -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA" "${COMPILERS[@]}" \
    -DCMAKE_C_FLAGS="$MAP" -DCMAKE_CXX_FLAGS="$MAP" -DCMAKE_BUILD_TYPE=$TYPE \
    "-DCMAKE_C_FLAGS_RELEASE=-O2 -DNDEBUG" "-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG" "-DCMAKE_EXE_LINKER_FLAGS_RELEASE=-s" \
    -DMH4N_DIR="$(m "$ROOT/mh4n")" -DSDL3_DIR="$(m "$SDL3")/lib/cmake/SDL3" -DMH4U_PC_CODE_ID=$ID
"$CMAKE" --build "$(m "$B")" --target MH4U zigcc --parallel
B="$B" ROOT="$ROOT" MH4N="$ROOT/mh4n" TC="$TC" SDL3="$SDL3" PY="$PYTHON" ID="$ID" bash "$ROOT/app_native/pc/make_folder.sh"
