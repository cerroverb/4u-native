# MH4U for Linux (two screens)

An experimental Linux host for the native runtime in `mh4n/`: the same recompiled code, runtime and Vulkan
renderer as the Android app, with the 3DS's top screen and touch screen in two windows. It is meant for
dual-screen handhelds running Linux (AArch64) and works on any Linux PC too.

No game code or files are included. You build the code library from your own decrypted copy, on the
machine that runs it.

## What it does

- `--layout dual` (the default): the top screen in the window **MH4U**, the touch screen in **MH4U Bottom**.
  A compositor that sends windows by title to the second panel (ROCKNIX's Sway on dual-screen handhelds
  does this for titles with "Bottom") puts the touch screen on the bottom panel.
- `--layout stacked`: both screens in one window, the top screen above.
- `--top-display N` and `--bottom-display N`: each window fullscreen on that display (0 is the first).
- The touch screen takes a finger or the mouse. A controller uses the 3DS's button positions (the right face
  button is A). The right stick turns the camera (R3 locks on), the triggers are ZL and ZR. The keyboard
  follows the Android app's keys (W A S D, I J K L, Space, Enter, Backspace, the arrows). F11 toggles
  fullscreen. Select with Guide quits.
- In a session whose compositor shows one output and offers the other panel for lease (gamescope's Game Mode on
  Armada), the touch screen goes straight to that panel through drm-lease-v1 and VK_KHR_display, and its
  touchscreen is read from evdev, as Azahar's DRM lease output by virtudude does (linux/drm_lease/).
  MH4U_DRM_LEASE=0 turns it off; MH4U_DRM_LEASE_CONNECTOR, MH4U_DRM_LEASE_ROTATION and MH4U_DRM_LEASE_TOUCH choose
  the connector, the rotation and the touch device.

Not yet: the Android app's touch controls, settings panel, mods screen, network rooms and save import. The
screens are read back from the GPU and drawn with SDL's renderer, which costs some time a frame.

## Build

Needs CMake 3.25+, Ninja, clang or GCC, Python 3.10+, the Vulkan headers and loader, and SDL3 3.4.18 or
newer (build it from source if your distribution has an older one). The leased touch screen also needs
wayland-client, libdrm and wayland-scanner; without them it is left out.

```sh
cmake -S linux -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux
```

Your code library, from the 1.3.0 update (`.cia`), built for this machine:

```sh
python3 mh4n/tools/build_game.py --dump UPDATE.cia --native --romfs GAME.3ds
```

It ends with `code library: PATH` (`0004000000126300_native.so` under `mh4n/build/game/recomp/`). Keep it for
yourself.

## Run

The first time, make the game folder from your game and its update:

```sh
build-linux/mh4u-linux --game ~/mh4u/game --code mh4n/build/game/recomp/0004000000126300_native.so \
  --from-dump GAME.3ds,UPDATE.cia
```

After that, leave out `--from-dump`. Saves and settings go to `~/.local/share/mh4u-linux` (`--user DIR` for
another folder). Options after `--` go to the runtime (`build-linux/mh4u-linux --game X --code Y -- --help`
lists them).

Do not use jM5557's single-screen mod with this host: it moves the touch screen's menus to the top screen.
