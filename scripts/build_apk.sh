#!/bin/sh
# Builds the app's APK from this tree: libmain.so (app_native/ with the runtime in mh4n/), libadrenotools' hook
# libraries and SDL3's library (scripts/build_sdl3.sh first), the code library id the app checks, then Gradle.
#
#   sh scripts/build_apk.sh
#
# The APK is android/app/build/outputs/apk/release/app-release.apk when android/keystore.properties (storePassword,
# keyAlias, keyPassword) and android/mh4u.keystore hold your own signing key; without them it builds a debug APK
# (android/app/build/outputs/apk/debug/app-debug.apk) signed with your Android debug key. Either way it can't replace
# an installed APK signed with another key: Android asks to uninstall it first, and that deletes its saves (export
# them first: gear, Menu Options, Export save).
#
# Needs the Android SDK (platform 35) and NDK 27.0.12077973, CMake 3.25 or newer, Ninja, Python 3.10 or newer and a
# JDK 17 (JAVA_HOME; Android Studio's own, jbr, works). See scripts/android_env.sh.
set -e
. "$(dirname "$0")/android_env.sh"

SDL3_DIR="$ROOT/build/sdl3/out"
[ -f "$SDL3_DIR/lib/libSDL3.so" ] || { echo "No SDL3 in $SDL3_DIR: run sh scripts/build_sdl3.sh first." >&2; exit 1; }
BUILD="$ROOT/build/app"
R=$(native_path "$ROOT")
N=$(native_path "$NDK")
# (no paths of this computer in the libraries: sources as mh4n/..., app_native/...)
MAP="-ffile-prefix-map=$R/= -ffile-prefix-map=$N/=ndk/"
"$CMAKE" -S "$R/app_native" -B "$(native_path "$BUILD")" -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_static \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DMH4N_DIR="$R/mh4n" -DSDL3_DIR="$(native_path "$SDL3_DIR")/lib/cmake/SDL3" \
    -DCMAKE_C_FLAGS="$MAP" -DCMAKE_CXX_FLAGS="$MAP" -DCMAKE_ASM_FLAGS="$MAP"
HOOKS="hook_impl main_hook file_redirect_hook gsl_alloc_hook"
# shellcheck disable=SC2086
"$CMAKE" --build "$(native_path "$BUILD")" --target main $HOOKS --parallel

LIBS="$ROOT/android/app/src/main/jniLibs/arm64-v8a"
mkdir -p "$LIBS"
"$STRIP" --strip-debug -o "$(native_path "$LIBS/libmain.so")" "$(native_path "$BUILD/libmain.so")"
for H in $HOOKS; do
    "$STRIP" --strip-unneeded -o "$(native_path "$LIBS/lib$H.so")" "$(native_path "$BUILD/adrenotools/src/hook/lib$H.so")"
done
"$STRIP" --strip-unneeded -o "$(native_path "$LIBS/libSDL3.so")" "$(native_path "$SDL3_DIR/lib/libSDL3.so")"

# the id of the code library this build runs with: the app refuses a library made with other tools
mkdir -p "$ROOT/android/app/src/main/assets"
"$PYTHON" "$R/mh4n/tools/build_game.py" --print-id > "$ROOT/android/app/src/main/assets/code_library_id.txt"
echo "code library id: $(cat "$ROOT/android/app/src/main/assets/code_library_id.txt")"

cd "$ROOT/android"
ANDROID_HOME=$(native_path "$SDK"); export ANDROID_HOME   # (Gradle's SDK, without a local.properties)
if [ -f keystore.properties ] && [ -f mh4u.keystore ]; then TASK=assembleRelease; KIND=release; else TASK=assembleDebug; KIND=debug; fi
sh ./gradlew "$TASK" -q
ls -l "app/build/outputs/apk/$KIND/"*.apk
