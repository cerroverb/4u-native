# MH4U for PC (Windows x64): MH4U.exe, included by ../CMakeLists.txt when the target isn't Android. The same app as the
# Android one's libmain.so: the native runtime (mh4n, with its app modules: SDL3 audio, input), the app's shared sources
# (the settings, the touch controls, the controllers and the keyboard...) and the PC's own parts in this folder: the
# program's main (pc_main.cpp), the game folder screen (pc_launcher.cpp), the screens drawn with SDL's renderer
# (pc_screens.cpp, in place of shared_screens.cpp's EGL images) and what Android's Java side and its system calls do
# (pc_platform.cpp). The game's code is NOT in here: each player builds it (mh4n tools/build_game.py --pc).
#
#   cmake -S app_native -B <build> -G Ninja -DMH4N_DIR=<mh4n> -DSDL3_DIR=<SDL3 for Windows>/lib/cmake/SDL3
#         -DMH4U_PC_CODE_ID=<mh4n tools/build_game.py --print-id --pc> (llvm-mingw's clang: scripts/build_pc.sh)
set(MH4U_PC_CODE_ID "" CACHE STRING "the code library id MH4U.exe runs with (mh4n tools/build_game.py --print-id --pc)")
if(NOT MH4U_PC_CODE_ID MATCHES "^[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]$")
  message(FATAL_ERROR "MH4U_PC_CODE_ID is '${MH4U_PC_CODE_ID}': give it mh4n's tools/build_game.py --print-id --pc")
endif()
enable_language(RC)  # (pc/MH4U.rc: the program's manifest)

# (llvm-mingw's lld has no --push-state: plain --whole-archive for the runtime's modules, as mh4n's native/CMakeLists.txt
# sets it in its own scope)
set(CMAKE_LINK_LIBRARY_USING_WHOLE_ARCHIVE "LINKER:--whole-archive" "<LINK_ITEM>" "LINKER:--no-whole-archive")
set(CMAKE_LINK_LIBRARY_USING_WHOLE_ARCHIVE_SUPPORTED TRUE)
foreach(lang C CXX)
  set(CMAKE_${lang}_LINK_LIBRARY_USING_WHOLE_ARCHIVE "LINKER:--whole-archive" "<LINK_ITEM>" "LINKER:--no-whole-archive")
  set(CMAKE_${lang}_LINK_LIBRARY_USING_WHOLE_ARCHIVE_SUPPORTED TRUE)
endforeach()

# the app's sources that both apps build (the Android-only ones stay in ../CMakeLists.txt)
set(MH4U_SHARED_SRC controls.cpp padmap.cpp ui.cpp touch.cpp town.cpp third_party/imgui_backends/imgui_impl_sdlrenderer3.cpp
    perfstats.cpp game_options.cpp settings_panel.cpp app_settings.cpp net_rooms.cpp game_keyboard.cpp content.cpp
    app_live.cpp first_run.cpp chat_app.cpp cache_clear.cpp verify_files.cpp diag.cpp hunt_numbers.cpp hunt_bars.cpp
    quick_items.cpp)
# (1.0.2: the Upscale filter's FSR 1, the Smithy's weapon tree, a control profile's .zip written for Export)
list(APPEND MH4U_SHARED_SRC fsr1.cpp weapon_tree_ui.cpp weapon_tree_panel.cpp zipw.c)
set(MH4U_PC_SRC pc/pc_main.cpp pc/pc_launcher.cpp pc/pc_screens.cpp pc/pc_platform.cpp pc/pc_gl.cpp pc/pc_display.cpp
    pc/pc_hdr.cpp pc/pc_stats.c pc/pc_perf.cpp pc/pc_vsync.cpp pc/pc_input.cpp pc/MH4U.rc frame_gen.cpp)
# (multiplayer on the PC: rooms on this network with Windows' sockets, Discord Rich Presence)
list(APPEND MH4U_PC_SRC pc/pc_net.cpp pc/pc_net_lan.cpp pc/pc_discord.cpp)
add_executable(MH4U WIN32 ${MH4U_SHARED_SRC} ${MH4U_PC_SRC})
# the builder (pc_builder.cpp), the settings' files and folders (pc_files.cpp) and the .zip reader they share (unzip.c)
# the error screen when the game crashes (pc_crash.cpp) and the update notice (pc_updates.cpp)
target_sources(MH4U PRIVATE pc/pc_builder.cpp pc/pc_files.cpp pc/unzip.c pc/pc_crash.cpp pc/pc_updates.cpp)
# the port's chat (chat/, as the Android app's: its WebSocket through WinHTTP on Windows, chat_ws_win.cpp)
set(CHAT_SRC chat/chat.cpp chat/chat_crypto.cpp chat/chat_json.cpp chat/chat_text.cpp chat/chat_overlay.cpp
             chat/chat_ws_android.cpp chat/chat_ws_win.cpp chat/chat_ws_none.cpp chat/chat_secp256k1.c)
target_sources(MH4U PRIVATE ${CHAT_SRC})
set_property(SOURCE ${CHAT_SRC} APPEND PROPERTY INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/chat
             ${CMAKE_CURRENT_SOURCE_DIR}/third_party/secp256k1 ${CMAKE_CURRENT_SOURCE_DIR}/third_party/secp256k1/include)
set_source_files_properties(chat/chat_secp256k1.c PROPERTIES COMPILE_OPTIONS "-O2;-w")
target_include_directories(MH4U PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${MH4N_DIR}/native/src ${MH4N_DIR}/port/src
                           third_party/imgui_backends pc pc/compat)
# (the POSIX calls MinGW spells otherwise, for the shared sources: pc/compat/pc_compat.h; android/log.h is a stand-in there)
target_compile_options(MH4U PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_SOURCE_DIR}/pc/compat/pc_compat.h>)
target_compile_definitions(MH4U PRIVATE MH4U_PC_CODE_ID="${MH4U_PC_CODE_ID}" NOMINMAX WIN32_LEAN_AND_MEAN
                           _CRT_SECURE_NO_WARNINGS)
target_link_libraries(MH4U PRIVATE mh4n::app mh4n_imgui SDL3::SDL3 ws2_32 iphlpapi winhttp bcrypt shell32 ole32 shlwapi
                      opengl32 dxgi d3d11 psapi windowscodecs)
# libc++ and the C runtime inside the program (only SDL3.dll beside it), and a bigger stack for the main thread
target_link_options(MH4U PRIVATE -static -Wl,--stack,8388608)
# (beside the runtime's own programs: <build>/bin)
set_target_properties(MH4U PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# MH4U.exe's icon (pc/MH4U.ico: the Android app's launcher icon) and version (pc/MH4U.rc): app_version.h's
# MH4U_APP_VERSION and its numbers, 1.0.1.1 -> 1,0,1,1, unquoted (MH4U.rc makes the text a string). The release's flags
# (optimised, ThinLTO, stripped) are scripts/build_pc.sh's TYPE.
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/app_version.h MH4U_VERSION_LINE REGEX "^#define MH4U_APP_VERSION ")
string(REGEX MATCH "\"([^\"]+)\"" MH4U_VERSION_QUOTED "${MH4U_VERSION_LINE}")
set(MH4U_VERSION "${CMAKE_MATCH_1}")
string(REGEX MATCHALL "[0-9]+" MH4U_RC_NUMBERS "${MH4U_VERSION}")
list(APPEND MH4U_RC_NUMBERS 0 0 0 0)
list(SUBLIST MH4U_RC_NUMBERS 0 4 MH4U_RC_NUMBERS)
string(REPLACE ";" "," MH4U_RC_NUMBERS "${MH4U_RC_NUMBERS}")
set_source_files_properties(pc/MH4U.rc PROPERTIES
                            COMPILE_DEFINITIONS "MH4U_RC_NUMBERS=${MH4U_RC_NUMBERS};MH4U_RC_TEXT=${MH4U_VERSION}"
                            OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/pc/MH4U.ico;${CMAKE_CURRENT_SOURCE_DIR}/pc/MH4U.manifest")

# zigcc.exe, beside MH4U.exe: Zig's C compiler for the builder (pc/zigcc.c; pc_builder.cpp gives it to mh4n's
# tools/pc_build.py as build_game.py's --cc). A console program of its own, with nothing of the app in it
add_executable(zigcc pc/zigcc.c)
target_compile_definitions(zigcc PRIVATE UNICODE _UNICODE)
target_link_options(zigcc PRIVATE -municode -static)
set_target_properties(zigcc PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
