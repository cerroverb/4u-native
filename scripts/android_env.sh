# Sourced by build_sdl3.sh and build_apk.sh: finds the Android SDK and NDK, CMake, Ninja and Python.
# Override any of them with ANDROID_SDK_ROOT (or ANDROID_HOME), ANDROID_NDK_HOME, CMAKE, NINJA, PYTHON.
NDK_VERSION=27.0.12077973
ROOT=$(cd "$(dirname "$0")/.." && pwd)

# a path as CMake and the compilers want it (Git Bash on Windows: E:/x/y, elsewhere unchanged)
native_path() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi; }
unix_path() { if command -v cygpath >/dev/null 2>&1; then cygpath -u "$1"; else printf '%s\n' "$1"; fi; }

SDK=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}
if [ -z "$SDK" ]; then
    if [ -n "${LOCALAPPDATA:-}" ]; then SDK="$LOCALAPPDATA/Android/Sdk"
    elif [ -d "$HOME/Library/Android/sdk" ]; then SDK="$HOME/Library/Android/sdk"
    else SDK="$HOME/Android/Sdk"; fi
fi
SDK=$(unix_path "$SDK")
NDK=$(unix_path "${ANDROID_NDK_HOME:-$SDK/ndk/$NDK_VERSION}")
[ -f "$NDK/build/cmake/android.toolchain.cmake" ] || {
    echo "No Android NDK $NDK_VERSION in $NDK: install it with the SDK Manager (Android Studio, SDK Tools, NDK (Side by side)," \
         "Show Package Details) or set ANDROID_NDK_HOME." >&2; exit 1; }
STRIP=$(ls "$NDK"/toolchains/llvm/prebuilt/*/bin/llvm-strip* 2>/dev/null | head -n 1)
[ -n "$STRIP" ] || { echo "No llvm-strip in $NDK" >&2; exit 1; }

CMAKE=${CMAKE:-cmake}
NINJA=${NINJA:-ninja}
command -v "$CMAKE" >/dev/null 2>&1 || { echo "CMake 3.25 or newer is needed on PATH (or set CMAKE): pip install cmake" >&2; exit 1; }
command -v "$NINJA" >/dev/null 2>&1 || { echo "Ninja is needed on PATH (or set NINJA): pip install ninja" >&2; exit 1; }
NINJA=$(native_path "$(command -v "$NINJA")")

if [ -z "${PYTHON:-}" ]; then
    for p in python3 python; do
        if command -v "$p" >/dev/null 2>&1 && "$p" -c 'import sys; sys.exit(sys.version_info < (3, 10))' 2>/dev/null; then
            PYTHON=$p; break
        fi
    done
fi
[ -n "${PYTHON:-}" ] || { echo "Python 3.10 or newer is needed (python3, or set PYTHON)." >&2; exit 1; }

TOOLCHAIN=$(native_path "$NDK/build/cmake/android.toolchain.cmake")
