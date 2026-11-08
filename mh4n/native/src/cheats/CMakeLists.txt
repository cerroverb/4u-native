# mh4n_cheats: Gateway / Action Replay cheat codes (Azahar's cheat engine, gateway_cheat.h and
# cheat_engine.h), run once per game frame by the boot step `started.cheats`, with --cheats, --cheat and
# --no-cheats (cheats_step.cpp). The app's Cheats page changes the list while the game runs (Current).
mh4n_module(
    SOURCES
        cheat_engine.cpp
        cheats_step.cpp
        gateway_cheat.cpp
    DEPENDS
        mh4n_code_patch
        mh4n_ipc
        mh4n_runtime
        mh4n_services_hid
    THIRD_PARTY
        mh4n_fmt
)
# Included as "cheats/<name>.h" by the app and the tests.
target_include_directories(mh4n_cheats PUBLIC "${MH4N_SRC_DIR}")
