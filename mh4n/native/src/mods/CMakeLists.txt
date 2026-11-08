# mh4n_mods: the mods of the user folder, several at once in the player's order (mod_list.h): what the
# loader's code mods and the SelfNCCH archive's LayeredFS take, what the app's Mods page lists and saves,
# and the check that the code library matches the code mods that are on.
mh4n_module(
    SOURCES
        mod_list.cpp
    DEPENDS
        mh4n_code_patch
        mh4n_loader
        mh4n_runtime
    THIRD_PARTY
        mh4n_fmt
)
# Included as "mods/<name>.h" by the loader, the fs service, the app and the tests.
target_include_directories(mh4n_mods PUBLIC "${MH4N_SRC_DIR}")
