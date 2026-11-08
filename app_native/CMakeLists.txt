# libmain.so of the MH4U Android app: the native runtime (mh4n, with its app modules: SDL3 audio, input)
# and the app's own side (android_main.cpp). The game's code is NOT in here: the player builds it on a PC.
cmake_minimum_required(VERSION 3.25)
project(mh4u_android C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL3 3.4.18 CONFIG REQUIRED)
set(MH4N_TESTS OFF CACHE BOOL "" FORCE)
set(MH4N_APP ON CACHE BOOL "" FORCE)
add_subdirectory(${MH4N_DIR}/native mh4n)

add_library(main SHARED android_main.cpp controls.cpp padmap.cpp ui.cpp touch.cpp town.cpp third_party/imgui_backends/imgui_impl_sdlrenderer3.cpp perf_hint.c perfstats.cpp game_options.cpp settings_panel.cpp app_settings.cpp net_rooms.cpp net_lan.cpp game_keyboard.cpp gpu_driver.cpp content.cpp
            app_live.cpp crash_screen.cpp first_run.cpp shared_screens.cpp display_vsync.cpp present_pacing.cpp)
target_include_directories(main PRIVATE ${MH4N_DIR}/native/src ${MH4N_DIR}/port/src third_party/imgui_backends)
target_link_libraries(main PRIVATE mh4n::app mh4n_imgui SDL3::SDL3 android log)
# the screens straight from the renderer's buffers (shared_screens.cpp): EGL images and fences, GLES calls on SDL's context
target_link_libraries(main PRIVATE EGL GLESv2 nativewindow)
# A player's own GPU driver for an Adreno (gpu_driver.cpp): libadrenotools, BSD-2-Clause, commit 8fae8ce of
# github.com/bylaws/libadrenotools with its linkernsbypass at aa39758, the copy the MHGU port vendors. Its hook
# libraries (hook_impl, main_hook, file_redirect_hook, gsl_alloc_hook) go into the APK beside libmain.so
# (scripts/build_app.sh): they are opened from the app's nativeLibraryDir.
add_subdirectory(third_party/libadrenotools adrenotools)
target_link_libraries(main PRIVATE adrenotools)
foreach(hook hook_impl main_hook file_redirect_hook gsl_alloc_hook)
  target_link_options(${hook} PRIVATE -Wl,-z,max-page-size=16384)
endforeach()
# every registration of the runtime's modules has to survive (they are linked whole by mh4n::all);
# 16 KB pages for Android 15 devices
target_link_options(main PRIVATE -Wl,-z,max-page-size=16384 -Wl,--exclude-libs,ALL)
# The APK's minSdk is 28 (Android 9, as MHGU's): a libmain.so built for a newer API would not load there (ELF TLS and
# newer symbols). Configure with -DANDROID_PLATFORM=android-28.
if(ANDROID AND ANDROID_PLATFORM_LEVEL GREATER 28)
  message(FATAL_ERROR "ANDROID_PLATFORM is android-${ANDROID_PLATFORM_LEVEL}; the app's minSdk is 28: configure with -DANDROID_PLATFORM=android-28")
endif()
