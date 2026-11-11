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
