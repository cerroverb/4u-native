# mh4u-linux: the native runtime (mh4n, with its app modules: SDL3 audio, input) and a small Linux host
# (linux_main.cpp) that shows the top screen and the touch screen in two windows. The game's code is NOT in here:
# the player builds it (mh4n/tools/build_game.py --native) on the machine that runs it.
cmake_minimum_required(VERSION 3.25)
project(mh4u_linux C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MH4N_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../mh4n" CACHE PATH "the mh4n tree")
find_package(SDL3 3.4.18 CONFIG REQUIRED)
set(MH4N_TESTS OFF CACHE BOOL "" FORCE)
set(MH4N_APP ON CACHE BOOL "" FORCE)
add_subdirectory(${MH4N_DIR}/native mh4n)
# The interpreter's test data is for mh4n's unit tests, which this build leaves out (MH4N_TESTS OFF)
if (TARGET mh4n_cpu_interp_test_data)
    set_property(TARGET mh4n_cpu_interp_test_data PROPERTY EXCLUDE_FROM_ALL TRUE)
endif()

add_executable(mh4u-linux linux_main.cpp)
target_include_directories(mh4u-linux PRIVATE ${MH4N_DIR}/native/src ${MH4N_DIR}/port/src)
target_link_libraries(mh4u-linux PRIVATE mh4n::app SDL3::SDL3)

# The touch screen on a leased panel in a session whose compositor shows one output (gamescope's Game Mode):
# drm-lease-v1, VK_KHR_display and evdev touch (drm_lease/drm_lease_output.h). As Azahar's citra_qt/CMakeLists.txt
# (virtudude's drm-leasing branch): built when wayland-client, libdrm and wayland-scanner are there, else left out.
find_package(PkgConfig)
if (PkgConfig_FOUND)
    pkg_check_modules(WAYLAND_CLIENT IMPORTED_TARGET wayland-client)
    pkg_check_modules(LIBDRM IMPORTED_TARGET libdrm)
    pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
endif()
if (WAYLAND_CLIENT_FOUND AND LIBDRM_FOUND AND WAYLAND_SCANNER)
    set(DRM_LEASE_XML "${CMAKE_CURRENT_SOURCE_DIR}/drm_lease/drm-lease-v1.xml")
    set(DRM_LEASE_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/drm_lease")
    file(MAKE_DIRECTORY "${DRM_LEASE_GEN_DIR}")
    add_custom_command(
        OUTPUT "${DRM_LEASE_GEN_DIR}/drm-lease-v1-client-protocol.h"
               "${DRM_LEASE_GEN_DIR}/drm-lease-v1-protocol.c"
        COMMAND "${WAYLAND_SCANNER}" client-header "${DRM_LEASE_XML}"
                "${DRM_LEASE_GEN_DIR}/drm-lease-v1-client-protocol.h"
        COMMAND "${WAYLAND_SCANNER}" private-code "${DRM_LEASE_XML}"
                "${DRM_LEASE_GEN_DIR}/drm-lease-v1-protocol.c"
        DEPENDS "${DRM_LEASE_XML}"
        VERBATIM
    )
    target_sources(mh4u-linux PRIVATE
        drm_lease/drm_lease_client.cpp
        drm_lease/drm_lease_touch.cpp
        drm_lease/drm_lease_output.cpp
        "${DRM_LEASE_GEN_DIR}/drm-lease-v1-protocol.c"
    )
    target_include_directories(mh4u-linux PRIVATE "${DRM_LEASE_GEN_DIR}")
    # (the tree's one copy of the Vulkan headers; the entry points come from SDL's loader)
    target_include_directories(mh4u-linux SYSTEM PRIVATE "${MH4N_DIR}/native/third_party/vulkan/include")
    target_link_libraries(mh4u-linux PRIVATE PkgConfig::WAYLAND_CLIENT PkgConfig::LIBDRM)
    target_compile_definitions(mh4u-linux PRIVATE MH4U_DRM_LEASE)
    message(STATUS "mh4u-linux: the touch screen on a leased panel: on")
else()
    message(STATUS "mh4u-linux: the touch screen on a leased panel: off (needs wayland-client, libdrm, wayland-scanner)")
endif()
