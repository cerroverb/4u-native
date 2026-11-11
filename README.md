# MH4U for Android

Play your own copy of Monster Hunter 4 Ultimate on Android, natively and with touch controls made for phones.
It is not an emulator: the game's code is recompiled ahead of time into native code for your device, and runs on a
runtime of its own that does the console's work, with its graphics drawn through Vulkan.

**This project includes no game code, game files, ROMs or keys. You need your own copy of the game.**

[Lee esta guía en español](README.es.md)

Talk with other players and ask for help in the community's [Discord](https://discord.gg/sk5xEjEMwF).

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
- **Run with the stick:** push it past its edge to run. Slide your thumb up onto the small circle with a padlock above the
  stick, which lights up, and lift it there: the run locks and the hunter keeps running, steered with the camera, until you
  touch the stick or the circle again, draw your weapon or open a menu. To run only while you hold a button instead: gear,
  **Controls**, **Buttons on the screen**, turn off **Run with the stick**, and a **Run** button shows
- **Lock-on:** hold the camera button (L)
- **Item bar without the D-pad:** tap the game's item bar to use the item, swipe it to change the item
- **Bows and bowguns** get a layout of their own: a large fire button, reload, aim, crouching fire or coatings, and arrows
  for the ammo. A tap on aim keeps the aim up until the next tap; hold and slide it to aim, the slide turning the aim as
  dragging the view does
- **Aim with the gyro:** gear, **Controls**, **Aim with the gyro** (off by default) and **Gyro sensitivity**. While a bow or
  a bowgun aims, turning the phone turns the aim, from where you held it when the aim began, added to your finger. A
  controller with a gyro works too. The sensor is only read while aiming
- The controls follow the game: hunting, menus, and bows and bowguns each have their layout
- **Menus made for your thumb:** when a menu of the game's opens, the touch controls fade and a small pad comes in under
  your right thumb, with the page buttons L and R above it. The movement stick and a drag on the screen move the cursor
  too, and A and B stay where they are. To hide it: gear, **Controls**, **Buttons on the screen**, **Menu pad**
- **Combined buttons:** up to six buttons of your own that press several at once, such as A+X. Each one can also go on a
  controller's button (**Controller button**)
- **The layout editor:** a long press on the gear. Move and resize every button, and move the game's own HUD: the clock,
  the health and stamina bars, the sharpness, the name plates, the area's map, the target camera and the item bar. The
  menu pad and the run's padlock circle move and resize there too. The editor's panel moves out of the way when you drag
  its title, and gets smaller down to 60 %
- Your phone's keyboard for your hunter's name and the chat
- Vibration on touch, adjustable opacity and camera speed

### Controllers and keyboard

- A controller hides the touch controls, and they come back when it's gone. **START and SELECT together** open the settings
  from any controller
- The right stick turns the camera, without the game's Circle Pad Pro option. Clicking it locks on
- **Town actions without the touch screen:** Guild Cards, Gestures, Friends and Hunters for Hire from a list, with the
  town's touch button or SELECT (Gear, **Controls**, **Town actions button**); what they open shows large in the middle
- Gear, **Controls**, **Controller**: the face buttons by their place (as on the 3DS) or by their letters, the camera's speed
  and direction, the sticks' dead zone. A controller the game reads wrong: **Map a controller**, and press each button once.
  While a controller is connected, a **Combined buttons** row there puts your combined buttons on its buttons
- A keyboard: W A S D move (Shift: walk), I J K L the camera, U X, O or F A, Space B, R Y, Q L, E R, Tab ZL, T ZR,
  Enter START, Backspace SELECT, the arrows the D-pad

### And more

- Mods, with load order and conflicts, and cheats in Azahar's format: see [Mods and cheats](#mods-and-cheats)
- Playing together over network rooms, and finding rooms on the same Wi-Fi: see [Playing together](#playing-together)
- Import your save from Azahar, Citra or a Checkpoint backup, and export it for them
- Keep your save in a folder a sync app shares with your other devices: see
  [Choose where your save is kept](#choose-where-your-save-is-kept)
- **Faster eating and drinking** cheats, and **Comforts** after jM5557's QOL mods: see [Mods and cheats](#mods-and-cheats)
- When you play alone, the game waits while the settings or the layout editor are open
- The game pauses when you leave the app
- If the game closes because of an error, a screen tells you and lets you share the log
- A notice when a new version is out, and when a copy isn't the official one: see [Official downloads](#official-downloads)

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
top one, use **jM5557's single-screen mod** for the USA game. It is made for the USA game in English: with the game in
another language its menus overlap ([#30](https://github.com/khorne-hub/4u-native/issues/30)). It is not part of this project: download
it from its author's repository, [github.com/jM5557/mh4u-single-screen-mod](https://github.com/jM5557/mh4u-single-screen-mod)
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
   device too): tap the one to bring in. If it isn't listed, **Choose a folder** and pick your save's own folder (`system`,
   `user1`...), an emulator's `sdmc` folder, or a folder **Export save** made.

The game restarts with it. The save you had is kept in `Android/data/com.mh4u.native/files/save_backups`, and it is never
deleted before the new one is in place.

## Export your save

Save in the game first. Then gear, **Menu Options**, **Export save**, and choose a folder: a new folder named `MH4U save`
with the date is made there, in the layout Azahar and Citra use, with the save, the guild cards, event quests and the Palico.

## Choose where your save is kept

The save is in the app's own folder unless you choose another one, for example a folder that a sync app (Syncthing,
FolderSync and the like) shares with your other devices. Go to the gear, **Menu Options**, **Save folder**, and choose the
folder:

- Your save, with its extra data, goes in a folder named `MH4U save` inside the one you choose, so one shared folder can
  hold MH4U's and MHGU for Android's (`MHGU save`). A folder that already holds a save, such as one **Export save** made,
  is used as it is.
- Your save is copied there before anything changes. If the folder already has a save, you choose which one stays; the
  other is kept in a copy, in `save_backups` in the app's own folder.
- The game restarts to use the folder: save first. **Use the game's own folder**, on the same screen, goes back.
- On two devices, hunt on one at a time, and let the sync app finish before you open the game on the other.

When the game starts it checks the folder. If it can't reach it, if the sync app kept two versions of the save, or if
another device has the game open, it says so and lets you choose what to do.

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

**Faster eating and drinking:** two cheats come with the app, off, at the end of the list, as in the MHGU port. **Comer
rapido siempre (beber x1.7, carne x1.8)** gives the Speed Eating skill's effect without the skill; **Beber muy rapido
(x2.5)** drinks faster still (meat x1.8). Use one of the two. They speed up the animation, but don't let you walk while
drinking. Turned off, the effect lasts until the next quest.

### Comforts

Under the cheats, **Comforts** has four ideas from jM5557's [MH4U QOL mods](https://github.com/jM5557/MH4U-QOL-Cheats), done by
the app itself: no mod and no new code library. They are off until you turn them on, and work at once:

- **Hold A to gather:** hold A to carve, gather, mine and catch bugs again and again, until you let go.
- **Move to skip the flex:** after a potion or a ration, moving, rolling or any other action ends the flex. What you drank
  still works; if you stand still, the flex plays whole.
- **B cancels sharpening:** B while sharpening rolls out of it.
- **No time limit on rewards:** the rewards screen doesn't count down.

The wait after a quest already ends early here: hold ZL and ZR (gear, **Gameplay**, **End the wait**), as jM5557's *Skip
Quest End Timer* does with A and B.

jM5557's other mods change the game's code, so they work as code mods: *Full Item Box In Villages And Hubs* (the house's
full item box, with equipment and appearance, at the village and hall boxes), *Send All Rewards To Item Box (Hold X+Y)* and
the *Hide Number* version of the rewards one. Get them from [jM5557's repository](https://github.com/jM5557/MH4U-QOL-Cheats), from its
**USA v1.1** folder (made for the 1.3.0 update), and build your code library with the one you want (step 3, with that
mod's folder, the one with `exefs` in it, in the `mods` folder). The library takes one code mod, as Azahar does (the
single-screen mod is one too); *ALL Cheats* has all of them together, comforts included, so with it leave the app's
comforts off.

## Playing together

The game's local play goes through a network room, the same kind of rooms Azahar uses: players of this app and of Azahar can
share one.

1. Gear, **Multiplayer**: choose a **Nickname**.
2. **Public rooms** lists the rooms of a community lobby, `http://88.198.47.46:5000`, which players run (not this
   project); MH4U's come first. Everyone in your group taps the same room to join it (an empty one, so you are alone in it).
3. Or, on the same Wi-Fi: one player taps **Host a room**; the others tap **Search this Wi-Fi** under **Rooms on this Wi-Fi** and
   join it. Over the internet, the host forwards UDP port 24872 on their router to the phone, or everyone joins a room
   someone runs; the others type its **Room address** (and **Password**, if it has one) and tap **Join the room**.
4. In the game: World Map, Solo/Multiplayer, **Local Play**. One player creates a Gathering Hall and the others find it.

Join the room before going to the Gathering Hall. The room's chat is on the same page. **Lobby** picks another lobby
(**Custom...**) or none (**Off**); the lobbies you type and save stay in its list, the last one first (up to 24), and the
**×** beside one takes it out. The first time you play online a notice says which server the app connects to: like any
server on the internet, it sees your IP address.

## Samsung: Game Booster

Samsung's performance panel only shows for apps it recognizes as games, and an APK installed by hand doesn't count. To get
it, install the APK once from a PC with [adb](https://developer.android.com/tools/adb):

```
adb install -r -i com.mh4u.native MH4U-<version>.apk
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
| A black screen at start, with the menu's music playing (some older Mali GPUs) | When the phone's graphics refuse the usual way of showing the picture, the app now switches to another by itself. If it stays black, attach `log.txt` from `Android/data/com.mh4u.native/files` (a file manager or a PC can copy it) to an issue |

If it still fails, ask in the [Discord](https://discord.gg/sk5xEjEMwF) or open an [issue](../../issues) with your device's model and the log. If the game closes because of an error,
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
- The **Comforts** follow jM5557's [MH4U QOL mods](https://github.com/jM5557/MH4U-QOL-Cheats); no code of them is in this project.
- The libraries this project uses, with their licenses, are listed in [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES). Their
  licenses are also in the app: gear, **Credits & Licenses**, **Licenses**.

## Official downloads

MH4U for Android is free. The only official downloads are the links khorne-hub gives, starting with this repository's
[Releases](https://github.com/khorne-hub/4u-native/releases). If you got it anywhere else, paid for it, or your copy came with
ads, it is not the official one. In the app, the GitHub button at the top of the settings menu, beside **Save**, opens this
page in your browser, and the Discord button beside it the community's [Discord](https://discord.gg/sk5xEjEMwF).

Every official APK is signed with the same key. Its certificate's SHA-256 fingerprint is:

```
8D:E8:8D:5C:5C:F8:93:97:84:5A:D8:93:B5:AA:B7:E7:96:64:74:EC:FD:66:DB:75:78:38:C3:2A:13:7C:C4:03
```

To check an APK: `apksigner verify --print-certs MH4U-<version>.apk` (Android SDK build-tools). A copy signed with
another key can't be installed over the official app either. From 0.1.9 the app checks it too: a copy signed with another
key says so once when it starts.

The app also tells you when a new version is out. At most once a day it asks GitHub for this repository's latest release
(nothing else is sent, not even the phone's model), and at the next start it offers to open the release page. It never
downloads or installs anything by itself. Gear, **Menu Options**, **Check for updates** turns it off.

## License

MH4U for Android is free software under the [GNU General Public License, version 3](LICENSE) or (at your option) any later
version, like the MH4UltimateDC runtime it builds on. The third-party parts keep their own licenses.

The code by khorne-hub carries [additional terms](ADDITIONAL-TERMS), which section 7 of the GPL allows. Whoever shares the
app, changed or not, must:

- keep the author's credits: the ones the app shows when it opens, its **Credits & Licenses** page, and this file's
  **Credits** and **License** sections;
- mark a changed version as changed, and not present it as the official one;
- give, with the app, its license and its complete source code, as the GPL requires.
