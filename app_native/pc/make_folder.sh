#!/bin/bash
# MH4U for PC: the program's folder, beside a build of MH4U.exe and zigcc.exe, as a player unpacks it, and with PACK=1 the
# release's zip and its SHA-256. Run by scripts/build_pc.sh after the build (Git Bash on Windows), with:
#   B      the build folder (its bin/ has MH4U.exe and zigcc.exe; the folder goes to B/MH4U, the zip to B)
#   ROOT   this app tree; MH4N its runtime (mh4n); TC llvm-mingw's root; SDL3 SDL 3.4.18 for Windows, installed
#   PY     Python 3.10 or newer; ID the PC code library id (mh4n tools/build_game.py --print-id --pc); PACK=1 the zip
# Nothing of the game goes in: each player builds the game's code library from their own copy.
set -e
: "${B:?}" "${ROOT:?}" "${MH4N:?}" "${TC:?}" "${SDL3:?}" "${PY:?}" "${ID:?}"
m() { cygpath -m "$1"; }
# the program's folder: what a player unpacks (the zip itself is made at the end)
D="$B/MH4U"
mkdir -p "$D"
cp "$B/bin/MH4U.exe" "$D/"
cp "$SDL3/bin/SDL3.dll" "$D/"
# (the app's own cheats, faster eating and drinking: the Android app's asset, read beside MH4U.exe)
mkdir -p "$D/cheats" && cp "$ROOT/android/app/src/main/assets/cheats/comer_rapido.txt" "$D/cheats/"
printf '%s\n' "$ID" > "$D/code_library_id.txt"
# the builder's: zigcc.exe and its tools (app_native/pc/pc_builder.cpp unpacks them: mh4n's tools/ and config/),
# checked to make the id
cp "$B/bin/zigcc.exe" "$D/"
"$PY" "$ROOT/scripts/pack_builder_tools.py" --pc "$(m "$MH4N")" "$(m "$D")/builder_tools.zip" "$ID" || exit 1
# the licenses (F1, Credits & Licenses, Licenses opens this folder): the Android app's, but its phone builder's and
# its phone GPU drivers' (libadrenotools), with what is the PC's own: the legal notice and the third-party notices with a
# paragraph on this version first, the PC builder's tools (app_native/pc/licenses/Builder-tools-PC.txt), llvm-mingw's
# runtime linked into MH4U.exe and zigcc.exe (its own license files), and Windows.txt: what each file of the folder is
A="$ROOT/android/app/src/main/assets/licenses"
L="$D/licenses"
rm -rf "$L" && mkdir -p "$L"
for f in "$A"/*.txt; do
  case "$(basename "$f")" in Builder-tools.txt|libadrenotools.txt|MH4U-LEGAL.txt|THIRD_PARTY_NOTICES.txt) ;; *) cp "$f" "$L/" ;; esac
done
{ echo "This notice also covers MH4U for PC (MH4U.exe), the same project's version for Windows. On PC, the code library each player builds is mh4u_code_native.dll."
  echo; echo "Este aviso también cubre MH4U para PC (MH4U.exe), la versión para Windows del mismo proyecto. En PC, la librería de código que arma cada jugador es mh4u_code_native.dll."
  echo; tr -d '\r' < "$A/MH4U-LEGAL.txt"; } > "$L/MH4U-LEGAL.txt"
{ echo "These notices also cover MH4U for PC (MH4U.exe), the same project built for Windows: the parts below are in MH4U.exe, or in SDL3.dll beside it, except what only the Android app has (its Java code and libraries, libadrenotools and the phone builder's launchers). MH4U.exe also contains llvm-mingw's runtime (LLVM.txt, MinGW-w64.txt); Discord Rich Presence talks to the Discord app with the program's own code, no Discord library. The PC builder's downloads are in Builder-tools.txt."
  echo; tr -d '\r' < "$A/THIRD_PARTY_NOTICES.txt"; } > "$L/THIRD_PARTY_NOTICES.txt"
cp "$ROOT/app_native/pc/licenses/Builder-tools-PC.txt" "$L/Builder-tools.txt"
MW="$TC/x86_64-w64-mingw32/share/mingw32"
[ -f "$TC/LICENSE.TXT" ] && [ -f "$MW/COPYING.MinGW-w64-runtime.txt" ] || { echo "licenses: llvm-mingw's license files are not in $TC"; exit 1; }
{ echo "MH4U.exe and zigcc.exe contain llvm-mingw's libc++, libc++abi, libunwind and compiler-rt, and SDL3.dll compiler-rt's builtins, linked statically, under the Apache License 2.0 with LLVM Exceptions (below)."
  echo; tr -d '\r' < "$TC/LICENSE.TXT"; } > "$L/LLVM.txt"
{ echo "MH4U.exe, zigcc.exe and SDL3.dll contain the mingw-w64 runtime, and winpthreads where a part uses it, linked statically, under their licenses (below)."
  echo; tr -d '\r' < "$MW/COPYING.MinGW-w64-runtime.txt"; echo; tr -d '\r' < "$MW/COPYING.winpthreads.txt"; } > "$L/MinGW-w64.txt"
# (what the folder holds and where each license is, in English and Spanish)
{ echo "MH4U for PC: what is in this folder, and where each license is."
  echo; echo "MH4U.exe: the program, under the project's license (MH4U-GPL-3.0.txt) with its additional terms (MH4U-ADDITIONAL-TERMS.txt). THIRD_PARTY_NOTICES.txt lists the third-party code in it, each with its license file here (SDL3.txt, DearImGui.txt, glslang.txt, plume.txt, volk.txt, VulkanMemoryAllocator.txt, Vulkan-Headers.txt, fmt.txt, xxHash.txt, CityHash.txt, inih.txt, LodePNG.txt, bc7enc.txt, FSR1.txt, BasisUniversal.txt, Zstandard.txt, ENet.txt, libsecp256k1.txt, HIDAPI.txt, Fonts.txt, SystemArchives.txt, MHGURecomp-MIT.txt), and llvm-mingw's runtime (LLVM.txt, MinGW-w64.txt)."
  echo; echo "SDL3.dll: SDL 3.4.18 (SDL3.txt)."
  echo; echo "zigcc.exe, builder_tools.zip and code_library_id.txt: the project's own files, under its license (builder_tools.zip: the tools that make the game code). cheats: the project's own optional cheats, off."
  echo; echo "Nothing of the game is included: each player makes the game code library, mh4u_code_native.dll, from their own copy of the game. To make it, the program downloads Python and Zig the first time (Builder-tools.txt). The Vulkan loader and the OpenGL driver are the graphics card's."
  echo; echo "Monster Hunter is a trademark of the Great C. This project is not affiliated with, endorsed by or sponsored by the Great C or the Great N (MH4U-LEGAL.txt)."
  echo; echo "----------------------------------------"
  echo; echo "MH4U para PC: qué hay en esta carpeta y dónde está cada licencia."
  echo; echo "MH4U.exe: el programa, con la licencia del proyecto (MH4U-GPL-3.0.txt) y sus términos adicionales (MH4U-ADDITIONAL-TERMS.txt). THIRD_PARTY_NOTICES.txt lista el código de terceros que lleva dentro, cada uno con su archivo de licencia aquí, y el runtime de llvm-mingw (LLVM.txt, MinGW-w64.txt)."
  echo; echo "SDL3.dll: SDL 3.4.18 (SDL3.txt)."
  echo; echo "zigcc.exe, builder_tools.zip y code_library_id.txt: archivos propios del proyecto, con su licencia (builder_tools.zip: las herramientas que crean el código del juego). cheats: los trucos opcionales del proyecto, apagados."
  echo; echo "No incluye nada del juego: cada jugador arma la librería de código del juego, mh4u_code_native.dll, con su propia copia del juego. Para crearla, el programa descarga Python y Zig la primera vez (Builder-tools.txt). El cargador de Vulkan y el controlador de OpenGL son los de la tarjeta gráfica."
  echo; echo "Monster Hunter es una marca de la Gran C. Este proyecto no está afiliado, respaldado ni patrocinado por la Gran C ni la Gran N (MH4U-LEGAL.txt)."
} > "$L/Windows.txt"
# (every license file Windows.txt names is there)
for f in MH4U-GPL-3.0 MH4U-ADDITIONAL-TERMS THIRD_PARTY_NOTICES SDL3 DearImGui glslang plume volk VulkanMemoryAllocator \
         Vulkan-Headers fmt xxHash CityHash inih LodePNG bc7enc FSR1 BasisUniversal Zstandard ENet libsecp256k1 HIDAPI Fonts SystemArchives MHGURecomp-MIT \
         LLVM MinGW-w64 Builder-tools MH4U-LEGAL; do
  [ -f "$L/$f.txt" ] || { echo "licenses: $f.txt is missing"; exit 1; }
done
# the short guide at the folder's top (app_native/pc/README.txt, English and Spanish)
cp "$ROOT/app_native/pc/README.txt" "$D/README.txt"
ls -la "$D"
# PACK=1: the release's zip, MH4U-PC-<version>.zip (app_version.h's MH4U_APP_VERSION): the program's own files, by name (a
# folder the program ran in has its settings, log and user folder too; nothing of the game), and its SHA-256 beside it,
# the one each release publishes (certutil -hashfile MH4U-PC-<version>.zip SHA256)
if [ "${PACK:-0}" = 1 ]; then
  VERSION=$(sed -n 's/^#define MH4U_APP_VERSION "\(.*\)"/\1/p' "$ROOT/app_native/app_version.h" | tr -d '\r')
  [ -n "$VERSION" ] || { echo "zip: no MH4U_APP_VERSION in app_version.h"; exit 1; }
  Z="MH4U-PC-$VERSION.zip"
  rm -f "$B/$Z" "$B/$Z.sha256"
  (cd "$B" && "$(cygpath -u "$SYSTEMROOT")/System32/tar.exe" -a -c -f "$Z" MH4U/MH4U.exe MH4U/SDL3.dll MH4U/zigcc.exe \
     MH4U/builder_tools.zip MH4U/code_library_id.txt MH4U/README.txt MH4U/cheats MH4U/licenses) || { echo "zip: failed"; exit 1; }
  # (what went in: only those, nothing of a game or of a run)
  "$PY" - "$(m "$B/$Z")" <<'PY' || exit 1
import sys, zipfile
names = zipfile.ZipFile(sys.argv[1]).namelist()
allowed = ("MH4U/MH4U.exe", "MH4U/SDL3.dll", "MH4U/zigcc.exe", "MH4U/builder_tools.zip", "MH4U/code_library_id.txt",
           "MH4U/README.txt", "MH4U/cheats/", "MH4U/licenses/")
bad = [n for n in names if not n.startswith(allowed) or n.lower().endswith((".cci", ".3ds", ".cia", ".cxi", ".dll.id", ".id", ".ini", ".log"))
       or ("mh4u_code_native" in n.lower())]
print(f"zip: {len(names)} entries")
if bad:
    print("zip: unexpected entries:", *bad)
    sys.exit(1)
PY
  (cd "$B" && sha256sum "$Z" > "$Z.sha256")
  echo "zip: $B/$Z ($(du -k "$B/$Z" | cut -f1) KB)"; echo "SHA-256: $(cut -d' ' -f1 "$B/$Z.sha256")"
fi
echo "BUILD_PC_DONE $(date +%T)"
