# mh4n: the runtime and the game's recompiler

This folder comes from the `native` branch of [MH4UltimateDC](https://github.com/CompaLarios18/MH4UltimateDC), by
CompaLarios18 and its contributors, under the GPL-3.0-or-later ([LICENSE](LICENSE)). MH4U for Android builds on it, with
its own changes for Android under the same licence.

| Path | What |
|---|---|
| `tools/` | The recompiler (`recompile.py`, `recomp/`) and `build_game.py`, which turns the player's own update into the game's code library. `tools/android/` has the player's scripts for Windows (`Build MH4U library for Android.bat`) and for Linux and macOS (`build-android-library.sh`) |
| `config/` | The tables the recompiler and the runtime read: the game's functions, hook points and tunables, the versions it knows. They hold addresses, not game code |
| `native/` | The runtime: the console's kernel and services, the sound (DSP), the PICA200 GPU on Vulkan, mods, cheats and network rooms. `native/THIRD_PARTY.md` lists every vendored library and every file derived from Azahar, with its licence |
| `port/src/` | The game's hooks (widescreen, camera, frame pacing, HUD, controls) and the code host the recompiled code runs on |
| `port/app/` | Shared app sources (settings, input, rooms) and Dear ImGui |

Left out of this copy: the upstream's research and process documents, its CI, the Python tools' tests, its servers and
the patch of its Azahar-based PC version. They are in the upstream repository.

The code library `build_game.py` makes is built from the player's own copy of the game: it is never part of this
repository or of the app, and it must not be shared.
