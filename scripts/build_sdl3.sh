#!/bin/sh
# Builds SDL 3.4.18 for the app (arm64-v8a, Android 9 and later) from its release on GitHub, into build/sdl3/out.
# Run it once before scripts/build_apk.sh:
#
#   sh scripts/build_sdl3.sh
#
# Needs the Android NDK 27.0.12077973, CMake 3.25 or newer, Ninja, curl and tar (see scripts/android_env.sh).
set -e
. "$(dirname "$0")/android_env.sh"

VERSION=3.4.18
SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
URL=https://github.com/libsdl-org/SDL/releases/download/release-$VERSION/SDL3-$VERSION.tar.gz
WORK="$ROOT/build/sdl3"
SRC="$WORK/SDL3-$VERSION"
OUT="$WORK/out"
if [ -f "$OUT/.built" ]; then echo "SDL3 $VERSION: already built in $OUT"; exit 0; fi
mkdir -p "$WORK"

if [ ! -d "$SRC" ]; then
    TARBALL="$WORK/SDL3-$VERSION.tar.gz"
    echo "downloading $URL"
    curl -fL -o "$TARBALL" "$URL"
    if command -v sha256sum >/dev/null 2>&1; then GOT=$(sha256sum "$TARBALL" | cut -d' ' -f1)
    else GOT=$(shasum -a 256 "$TARBALL" | cut -d' ' -f1); fi
    [ "$GOT" = "$SHA256" ] || { echo "$TARBALL: SHA-256 $GOT, expected $SHA256" >&2; rm -f "$TARBALL"; exit 1; }
    tar -xzf "$TARBALL" -C "$WORK"
    rm -f "$TARBALL"
fi

S=$(native_path "$SRC")
"$CMAKE" -S "$S" -B "$(native_path "$WORK/build")" -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_PREFIX="$(native_path "$OUT")" \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
    -DCMAKE_C_FLAGS="-ffile-prefix-map=$S/=SDL3/" -DCMAKE_SHARED_LINKER_FLAGS=-Wl,-z,max-page-size=16384
"$CMAKE" --build "$(native_path "$WORK/build")" --target install --parallel
touch "$OUT/.built"
echo "SDL3 $VERSION: $OUT"
