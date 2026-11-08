# MH4U for Android

Play your own copy of Monster Hunter 4 Ultimate on Android, natively and with touch controls made for phones.
It is not an emulator: the game's code is recompiled ahead of time into native code for your device, and runs on a
runtime of its own that does the console's work, with its graphics drawn through Vulkan.

**This project includes no game code, game files, ROMs or keys. You need your own copy of the game.**

[Lee esta guía en español](README.es.md)

## What it does

### Display and performance

- **Your phone's own resolution:** the 3D picture is drawn at your screen's resolution by default (three times the
  3DS's on a 720-line screen). Lower it under the gear, **Graphics & Audio**, **3D resolution**, down to the 3DS's own
- **Widescreen that matches your screen**, from ultra-wide phones to 4:3 handhelds, with no bars
- **Up to 240 fps:** the **Frame rate limit** goes from 30 to 240. The game keeps its speed; only more frames are drawn.
  **Steady frame rate**, off by default, runs an even half of the limit when the phone can't hold it
- **Low-end mode**, for weak phones: 60 fps (30 on a phone without a fast core), the 3D picture at a third of your
  screen's resolution, and no screen filters, anti-aliasing or shadows. On a weak phone it is turned on by itself the first
  time the app starts
- **Toaster mode**, the third choice of Low-end mode, for the battery and a cool phone: a steady 30 fps, the 3DS's own
  resolution and no reverb
- Switches of their own for anti-aliasing, shadows, each of the game's screen filters (bloom, motion blur, depth of field,
  radial blur, heat haze) and reverb, and the field of view
- Optional FPS counter, with the phone's temperature
- **Your own GPU driver** on an Adreno, at your own risk: gear, **Graphics & Audio**, **GPU driver**. If one fails, the app
  goes back to the system's by itself and says so

### Touch controls

- A movement stick, the camera by dragging anywhere on the screen, attacks as a pill, two buttons or a band in the corner,
  and buttons for evade, guard, sheathe, kick, target and the camera
- **Lock-on:** hold the camera button (L)
- **Item bar without the D-pad:** tap the game's item bar to use the item, swipe it to change the item
- **Bows and bowguns** get a layout of their own: a large fire button, reload, aim, crouching fire or coatings, and arrows
  for the ammo
- The controls follow the game: hunting, menus, and bows and bowguns each have their layout
- **Combined buttons:** up to six buttons of your own that press several at once, such as A+X
- **The layout editor:** a long press on the gear. Move and resize every button, and move the game's own HUD: the clock,
  the health and stamina bars, the sharpness, the name plates, the area's map, the target camera and the item bar
- Your phone's keyboard for your hunter's name and the chat
- Vibration on touch, adjustable opacity and camera speed

### Controllers and keyboard

- A controller hides the touch controls, and they come back when it's gone. **START and SELECT together** open the settings
  from any controller
- The right stick turns the camera, without the game's Circle Pad Pro option. Clicking it locks on
- **Town actions without the touch screen:** Guild Cards, Gestures, Friends and Hunters for Hire from a list, with the
  town's touch button or SELECT (Gear, **Controls**, **Town actions button**); what they open shows large in the middle
- Gear, **Controls**, **Controller**: the face buttons by their place (as on the 3DS) or by their letters, the camera's speed
  and direction, the sticks' dead zone. A controller the game reads wrong: **Map a controller**, and press each button once
- A keyboard: W A S D move (Shift: walk), I J K L the camera, U X, O or F A, Space B, R Y, Q L, E R, Tab ZL, T ZR,
  Enter START, Backspace SELECT, the arrows the D-pad

### And more

- Mods, with load order and conflicts, and cheats in Azahar's format: see [Mods and cheats](#mods-and-cheats)
- Playing together over network rooms, and finding rooms on the same Wi-Fi: see [Playing together](#playing-together)
- Import your save from Azahar, Citra or a Checkpoint backup, and export it for them
- The game pauses when you leave the app
- If the game closes because of an error, a screen tells you and lets you share the log

## How well does it run?

Measured with the on-screen counter during development, at the phone's own resolution unless it says otherwise:

| Device | Chip | GPU | RAM | Frames a second |
|---|---|---|---|---|
| Galaxy S25 Ultra | Snapdragon 8 Elite | Adreno 830 | 12 GB | 120 in the village, 100 to 120 in the field |
| Anbernic RG406V | Unisoc T820 | Mali-G57 | 8 GB | about 59 in the field |
| moto g06 power | MediaTek MT6769 | Mali-G52 MC2 | 4 GB | about 30; about 59 with Low-end mode |

**If your phone runs hot, lower the frame rate limit**, or try Toaster mode.

## What you need

- **An Android device:** Android 9 or newer, 64-bit, with Vulkan. Tested on Adreno and Mali GPUs.
- **About 5 GB free** on the device: the game folder takes about 4 GB.
- **A PC with Windows, Linux or macOS,** once, to build the game's code for your device: Python 3.10 or newer and about
  3 GB free.
- **Your own copy of the game, decrypted:** Monster Hunter 4 Ultimate for the USA as a `.3ds` or `.cci` file, and its
  **1.3.0 update** as a `.cia` file.

This project does not explain how to dump a game from your console, and does not provide game files, keys or links to them.

## Setup

You do this once (and the third step again when a new version of the app asks for it).

### Step 1: Put your files in one folder

Put the game and its update in one folder, for example `C:\MH4U`. Any file names work.

### Step 2 (recommended): The single-screen mod

The 3DS has two screens. The app shows the top one on your whole screen and the touch screen in a corner, which you can
move, resize, make see-through or hide (gear, **Display**). For a single screen, with the touch screen's menus moved to the
top one, use **jM5557's single-screen mod** for the USA game. It is not part of this project: download it from its
author's repository, [github.com/jM5557/mh4u-single-screen-mod](https://github.com/jM5557/mh4u-single-screen-mod)
(the green **Code** button, **Download ZIP**), and unzip it into a `mods` folder beside the game:

```
MH4U
├─ the game (.3ds or .cci)
├─ the update (.cia)
└─ mods
   └─ mh4u-single-screen-mod-main
      ├─ 0004000000126300
      └─ 0004000E00126300
```

The mod changes the game's code, so the next step builds it in. With the mod, the app starts with the touch screen hidden;
the touch-screen button on the left shows it again.

### Step 3: Build the game's code

The app has no game code in it, so you build yours from your own copy. It runs on your PC and sends nothing anywhere.

**Windows:**

1. Install [Python](https://www.python.org/downloads/). Tick **Add python.exe to PATH** in the installer.
2. On this page, click the green **Code** button, then **Download ZIP**, and unzip it.
3. Open the unzipped folder, then `mh4n`, `tools`, `android`, and double-click **Build MH4U library for Android.bat**.
4. Choose your update's `.cia`. The game beside it is found by itself; it makes the code faster.
5. When it asks about mods that change the game's code, answer **Yes** if you did step 2 and choose the `mods` folder.

**Linux and macOS:** install Python 3.10 or newer, download and unzip this repository as above, open a terminal in its
folder and run, with your own paths:

```
sh mh4n/tools/android/build-android-library.sh ~/MH4U/update.cia --mods ~/MH4U/mods
```

Leave out `--mods ...` if you don't use the single-screen mod. It uses clang and lld if they are installed; if not, it
downloads Zig's C compiler.

The first time, the Windows script downloads its C compiler (llvm-mingw, about 180 MB). Then it takes a few minutes. When it
says **Done**, your folder has two new files:

```
MH4U
├─ the game (.3ds or .cci)
├─ the update (.cia)
├─ libmh4u_code_native.so
├─ libmh4u_code_native.id
└─ mods
```

**These two files are made from your game: keep them for yourself and don't share them.** Build them again whenever you add,
change or remove a mod that changes the game's code, and when a new version of the app says the library is from another
version.

### Step 4: Install it

1. Copy the whole `MH4U` folder to your device, for example with a USB cable. It can go anywhere in its storage or on an SD
   card, except inside `Android/data`.
2. On your device, download the APK from [Releases](../../releases/latest) and open it. Android may ask you to allow
   installing apps from your browser.
3. Open **MH4U**, tap **Give permission and choose folder**, and turn on file access when Android asks. The game only reads
   that folder: nothing is copied or changed there.
4. Find your `MH4U` folder (it shows a ✓) and tap **Use this folder**. If you'd rather use Android's own picker, tap
   **Android's picker**.

The app checks the folder, tells you what is missing if something is wrong, and starts the game.

Your saves and settings are kept apart, in `Android/data/com.mh4u.native/files`. To choose another folder later: gear,
**Menu Options**, **Game folder**.

## Settings

Tap the gear on the screen to open the settings. A long press on it opens the layout editor. The tabs are **Graphics &
Audio**, **Display**, **Controls**, **Gameplay**, **Multiplayer**, **Mods**, **Cheats** and **Menu Options**. Change what you
like and tap **Save**; a few options say they need a restart, which **Save and restart** does for you.

- **3D resolution:** 100% is your phone's own, as sharp as its screen shows; lower is lighter on the phone. **3DS (1x)** is
  the console's own, for the weakest phones.
- **Frame rate limit:** 30 to 240. 30 is the 3DS's, heats the phone less and saves battery; above 60 is for fast phones with
  a screen that fast.
- **Low-end mode:** **Off**, **On** or **Toaster**, as described [above](#display-and-performance). You can still change each
  of its options afterwards.
- **FPS counter:** the frame rate and the phone's temperature at the top of the screen.

## Import your save

1. Copy the folder with your save's files (`system`, `user1`...) to the device, for example into `Download`. In Azahar or
   Citra: right-click the game, **Open Save Data Location**. From a 3DS: a Checkpoint backup of the game.
2. In the game, gear, **Menu Options**, **Import save**. The app looks for saves on the device (an Azahar app on the same
   device too): tap the one to bring in.

The game restarts with it. The save you had is kept in `Android/data/com.mh4u.native/files/save_backups`.

## Export your save

Save in the game first. Then gear, **Menu Options**, **Export save**, and choose a folder: a new folder named `MH4U save`
with the date is made there, in the layout Azahar and Citra use, with the save, the guild cards, event quests and the Palico.

## Mods and cheats

**Mods:** gear, **Mods**, **Add a mod (.zip)** or **Add a mod (folder)**. A mod for the USA game has a `romfs` folder, an
`exefs` folder or both; the emulators' layout, with a `0004000000126300` folder inside, works too. The page lists the mods in
load order: tap one to turn it on or off, move it, or remove it. Where two mods change the same file, the one lower in the
list wins. Save and restart the game: mods load when it starts. The mods in the `mods` folder beside the game are listed too.

A mod with an `exefs` folder changes the game's code, so your code library has to be built with it (step 3, with that mod in
the `mods` folder). The **Code library** row says whether your library matches the code mods that are on. If the game stops
at start after a change of mods, its error screen offers **Mods of the last start**.

**Cheats:** gear, **Cheats**, **Import cheats (.txt)**, and choose a `.txt` of codes for MH4U USA 1.3.0 in Azahar's and
Citra's format (each cheat is its `[Name]` and its code lines). They come in off: turn on the ones you want and save. They
work at once. Save your game before trying one, and use them online only if everyone in the room agrees.

## Playing together

The game's local play goes through a network room, the same kind of rooms Azahar uses: players of this app and of Azahar can
share one. The app comes with no room or server of its own.

1. Gear, **Multiplayer**: choose a **Nickname**.
2. On the same Wi-Fi: one player taps **Host a room**; the others tap **Search this Wi-Fi** under **Rooms on this Wi-Fi** and
   join it. Over the internet, the host forwards UDP port 24872 on their router to the phone, or everyone joins a room
   someone runs; the others type its **Room address** (and **Password**, if it has one) and tap **Join the room**.
3. In the game: World Map, Solo/Multiplayer, **Local Play**. One player creates a Gathering Hall and the others find it.

Join the room before going to the Gathering Hall. The room's chat is on the same page.

## Samsung: Game Booster

Samsung's performance panel only shows for apps it recognizes as games, and an APK installed by hand doesn't count. To get
it, install the APK once from a PC with [adb](https://developer.android.com/tools/adb):

```
adb install -r -i com.mh4u.native MH4U-0.1.0.apk
```

Use the name of the APK you downloaded. It's optional: the game runs the same without it.

## If something goes wrong

| Problem | What to do |
|---|---|
| `python` is not recognized | Install Python again, tick **Add python.exe to PATH**, and open the script again |
| "... is encrypted" | The game and the update have to be decrypted copies |
| "this code.bin isn't a version the project knows" | Use the USA game's 1.3.0 update |
| The app says the game or the update is missing | Put both in the folder you chose, beside each other |
| The app says the library is missing, or is from another version | Do step 3 again with this version's download, and copy both files (`.so` and `.id`) to the folder |
| The game stops at start after adding or removing a mod | A mod with an `exefs` folder needs the library built with it (step 3). On the error screen, **Mods of the last start** puts the mods back as they were |
| It runs slowly or the phone gets hot | Lower the **Frame rate limit** or the **3D resolution**, or turn on **Low-end mode**; on the weakest phones, **Toaster** |
| The game closes by itself | Close other apps: it's usually a lack of memory. Then share the log |

If it still fails, open an [issue](../../issues) with your device's model and the log. If the game closes because of an error,
a screen lets you share it; if not, it's under the gear, **Menu Options**, **Share log**. Don't attach game files.

## Building the app

For developers; players only need the APK from Releases. On Windows (Git Bash), Linux or macOS, with the Android SDK
(platform 35) and NDK 27.0.12077973, CMake 3.25 or newer, Ninja, Python 3.10 or newer and a JDK 17:

```
sh scripts/build_sdl3.sh    # SDL 3.4.18 from its release, once
sh scripts/build_apk.sh     # libmain.so, the hook libraries, then Gradle
```

`scripts/android_env.sh` says where each tool is looked for. Sign with your own key in `android/keystore.properties` and
`android/mh4u.keystore`; without them you get a debug APK. `app_native/` is the app (SDL3 and Dear ImGui), `android/` its
Android side, and `mh4n/` the runtime and the recompiler (see [mh4n/README.md](mh4n/README.md)).

## Legal

This project is not affiliated with, endorsed by or sponsored by the Great C or the Great N. Monster Hunter is a trademark of
the Great C. The 3DS is a trademark of the Great N.

This repository and its releases contain no game code, game data, ROMs or keys. Use it only with a copy of the game you own.
Requests for game files, keys or ready-made `libmh4u_code_native.so` files will be closed.

Rights holders: please open an issue to get in touch.

## Credits

- The game's recompiler and the native runtime come from [MH4UltimateDC](https://github.com/CompaLarios18/MH4UltimateDC)
  by CompaLarios18 and its contributors.
- Parts of the runtime are derived from [Azahar](https://github.com/azahar-emu/azahar) and Citra.
- From the same author: [MHGU for Android](https://github.com/khorne-hub/gu-native), Monster Hunter Generations Ultimate
  recompiled for Android the same way.
- The single-screen mod is [jM5557's](https://github.com/jM5557/mh4u-single-screen-mod), and is not part of this project.
- The libraries this project uses, with their licenses, are listed in [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES). Their
  licenses are also in the app: gear, **Credits & Licenses**, **Licenses**.

## Official downloads

MH4U for Android is free. The only official downloads are the links khorne-hub gives, starting with this repository's
[Releases](https://github.com/khorne-hub/4u-native/releases). If you got it anywhere else, paid for it, or your copy came with
ads, it is not the official one.

Every official APK is signed with the same key. Its certificate's SHA-256 fingerprint is:

```
8D:E8:8D:5C:5C:F8:93:97:84:5A:D8:93:B5:AA:B7:E7:96:64:74:EC:FD:66:DB:75:78:38:C3:2A:13:7C:C4:03
```

To check an APK: `apksigner verify --print-certs MH4U-<version>.apk` (Android SDK build-tools). A copy signed with
another key can't be installed over the official app either.

## License

MH4U for Android is free software under the [GNU General Public License, version 3](LICENSE) or (at your option) any later
version, like the MH4UltimateDC runtime it builds on. The third-party parts keep their own licenses.

The code by khorne-hub carries [additional terms](ADDITIONAL-TERMS), which section 7 of the GPL allows. Whoever shares the
app, changed or not, must:

- keep the author's credits: the ones the app shows when it opens, its **Credits & Licenses** page, and this file's
  **Credits** and **License** sections;
- mark a changed version as changed, and not present it as the official one;
- give, with the app, its license and its complete source code, as the GPL requires.
