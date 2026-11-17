# MH4U for Android

Play your own copy of Monster Hunter 4 Ultimate on Android, natively and with touch controls made for phones.
It is not an emulator: the game's code is recompiled ahead of time into native code for your device, and runs on a
runtime of its own that does the console's work, with its graphics drawn through Vulkan.

**This project includes no game code, game files, ROMs or keys. You need your own copy of the game.**

[Lee esta guía en español](README.es.md)

Talk with other players and ask for help in the community's [Discord](https://discord.gg/sk5xEjEMwF).

**The same save on your PC and your phone?** See [Play on PC and phone with the same save (Google Drive)](#play-on-pc-and-phone-with-the-same-save-google-drive).

## Play on PC and phone with the same save (Google Drive)

One save for your phone, your tablet and an emulator on your PC (Azahar or Citra), with Google Drive in between. The app
has no cloud of its own and doesn't sync by itself: a free sync app does it on the phone, and on the PC you copy the save
in and out by hand.

- **Between phones and tablets, by itself:** in the game, **Save folder** puts your save in a folder of the phone, and a
  sync app (**FolderSync** or **Autosync for Google Drive**) keeps that folder the same as one in your Google Drive. Step
  by step: [Keep your save in the cloud](#keep-your-save-in-the-cloud-google-drive-and-the-like).
- **With an emulator on the PC, by hand:** install **Google Drive for desktop** on the PC: your Drive shows up in File
  Explorer, so the phone's synced folder is right there. `MH4U save` is in the emulators' own layout (an `sdmc` folder).
  - **Phone to PC:** save on the phone and let the sync app finish. Open the emulator's user folder, the one with `sdmc` in it
    (from the emulator's **File** menu), keep a copy of its `sdmc` folder, and copy the `sdmc` folder of your Drive's
    `MH4U save` over it, replacing the files. The guild cards, event quests and Palico come along.
  - **PC to phone:** in the emulator, right-click the game, **Open Save Data Location**, and copy that folder into the Drive
    folder the phone syncs. On the phone, once the sync app has brought it down: gear, **System**, **Your game**, **Import
    save**, **Choose a folder**, and pick it.
- Play on one device at a time, and save before you switch. On the phone the save it replaces is kept in `save_backups`.
  More: [Import your save](#import-your-save) and [Export your save](#export-your-save).

## What it does

### Display and performance

- **Your phone's own resolution:** the 3D picture is drawn at your screen's resolution by default (three times the
  3DS's on a 720-line screen). Lower it under the gear, **Graphics & Audio**, **3D resolution**, down to the 3DS's own
- **Widescreen that matches your screen**, from ultra-wide phones to 4:3 handhelds, with no bars
- **15 to 240 fps:** the **Frame rate limit** goes from 15 to 240. The game keeps its speed; only more or fewer frames are drawn.
  **Steady frame rate**, off by default, runs an even half of the limit when the phone can't hold it
- **Low-end mode**, for weak phones: 60 fps (30 on a phone without a fast core), the 3D picture at a third of your
  screen's resolution, and no screen filters, anti-aliasing or shadows. On a weak phone it is turned on by itself the first
  time the app starts
- **Toaster mode**, the third choice of Low-end mode, for the battery and a cool phone: a steady 30 fps, the 3DS's own
  resolution and no reverb
- Switches of their own for anti-aliasing, shadows, each of the game's screen filters (bloom, motion blur, depth of field,
  radial blur, heat haze) and reverb, and the field of view
- Optional FPS counter, with the phone's temperature, that the layout editor moves anywhere
- **Single-screen mode:** the touch screen's menus drawn on the top screen by the game, in every language: see
  [Step 2](#step-2-optional-one-screen)
- **Effects** can be turned off for speed, and **HD texture packs** made for Azahar and Citra load: see [Settings](#settings)
  and [Mods and cheats](#mods-and-cheats)
- **Colour:** gear, **Graphics & Audio**, **Colour**: hue, temperature, tint, brightness, contrast, saturation and gamma for
  the game's picture on both screens (never the touch controls or this menu), all at 0, no change, by default, with
  **Reset colour**
- **Your own GPU driver** on an Adreno, at your own risk: gear, **Graphics & Audio**, **Advanced**, **GPU driver**. If one fails, the app
  goes back to the system's by itself and says so

### Touch controls

- A movement stick, the camera by dragging anywhere on the screen, attacks as a pill, two buttons or a band in the corner,
  and buttons for evade, guard, sheathe, kick, target, lock-on and the camera
- **Run with the stick:** push it past its edge to run. Slide your thumb up onto the small circle with a padlock above the
  stick, which lights up, and lift it there: the run locks and the hunter keeps running, steered with the camera, until you
  touch the stick or the circle again, draw your weapon or open a menu. To run only while you hold a button instead: gear,
  **Controls**, **Buttons on the screen**, turn off **Run with the stick**, and a **Run** button shows
- **Lock-on:** in a quest, tap the **Lock-on** button (left of the camera button, L) and the camera keeps turning to the
  monster until you tap it again, as in the MHGU port. It follows the monster the target camera chose, or the nearest
  large one; the target button changes the monster. Turning the camera yourself only pauses it for a moment. The button is
  the port's own and presses none of the game's buttons, so holding **L** is the game's **L** again. Move it in the layout
  editor, hide it under gear, **Controls**, **Buttons on the screen**, **Lock-on button**, or turn the lock-on off under
  **Controls**, **Lock-on**
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
- **Control profiles:** 10 slots for your touch layouts, each with everything: the buttons, the HUD, the FPS counter's
  place, held buttons and pictures. Each one is the port's own layout or an **emulator-style** one (A, B, X and Y in a
  diamond, the D-pad, the shoulder buttons, START and SELECT, each pressing only its own button). In the editor,
  **Profile**; or gear, **Controls**, **Control profiles**. They are kept in the `layout profiles` folder of your save
  folder, and your layout from before is profile 1. Designed by tabarong
- **Buttons that stay pressed:** in the editor, tap a button and turn on **Hold**: a tap holds it down, the next lets go
- **Your own button pictures:** in the editor, tap a control and choose **Picture** (add them there with **Add pictures**,
  or gear, **Controls**, **Touch controls**, **Button images**). Every control the editor places takes one, the stick's
  base and knob each their own (**For: base / knob**), and your picture is the control's whole look: only your PNG is
  drawn, a little smaller and darker while pressed. What you add is your own responsibility
- **Floating stick**, off by default: gear, **Controls**, **Touch controls**, **Floating stick**. The movement stick comes
  to where your thumb lands near it, and goes back when you lift it
- **Hide the controls:** hold one finger still for a moment on an empty spot (no button there) and every touch control
  hides, the gear too; do it again anywhere to bring them back. A camera drag, a hold on a button or a second finger never
  hides them. On by default: gear, **Controls**, **Touch controls**, **Long-press hides the controls**
- **Slide between buttons:** a thumb that slides from one button onto another keeps both pressed, as two fingers would
  (R onto A is R + A, R onto X is R + X, as the Insect Glaive needs); slid back onto the first, that one alone. On by
  default: gear, **Controls**, **Buttons on the screen**, **Slide between buttons: both pressed**
- **The item bar's touch area:** in the editor, tap the item bar and use - / + (or pinch) to make the area where a tap uses
  the item and a swipe turns the bar bigger or smaller (the bar itself is the game's)
- Your phone's keyboard for your hunter's name and the chat
- Vibration on touch, adjustable opacity and camera speed, and **Camera smoothing** (gear, **Controls**, off by default):
  a drag's turn of the camera eased in, the whole turn still given

### Controllers and keyboard

- A controller hides the touch controls, and they come back when it's gone. **START and SELECT together** open the settings
  from any controller
- The right stick turns the camera, without the game's Circle Pad Pro option. Clicking it locks on
- **Town actions without the touch screen:** Guild Cards, Gestures, Friends and Hunters for Hire from a list, with the
  town's touch button or SELECT (gear, **Controls**, **Controller**, **Town actions button**); what they open shows large in the middle
- Gear, **Controls**, **Controller**: the face buttons by their place (as on the 3DS) or by their letters, the camera's speed
  and direction, the sticks' dead zone. A controller the game reads wrong: **Map a controller**, and press each button once.
  While a controller is connected, a **Combined buttons** row there puts your combined buttons on its buttons
- A keyboard: W A S D move (Shift: walk), I J K L the camera, U X, O or F A, Space B, R Y, Q L, E R, Tab ZL, T ZR,
  Enter START, Backspace SELECT, the arrows the D-pad
- **A keyboard and a mouse on the phone:** while both are connected, gear, **Controls**, **Keyboard and mouse**,
  **Keybinds** changes every key (mouse buttons too) and **Mouse sensitivity** the camera's speed. A click in the game
  captures the mouse to turn the camera; Escape lets it go

### And more

- Mods, with load order and conflicts, HD texture packs, and cheats in Azahar's format: see
  [Mods and cheats](#mods-and-cheats)
- Playing together over network rooms, and finding rooms on the same Wi-Fi: see [Playing together](#playing-together)
- **Hubs**, online with no address to type and no port to open, through the same hub server as MHGU's: see
  [Hubs](#hubs-online-with-no-ports-to-open)
- Import your save from Azahar, Citra, or a Checkpoint or JKSM backup, and export it for them
- Keep your save in a folder a sync app shares with your other devices: see
  [Choose where your save is kept](#choose-where-your-save-is-kept), and in the cloud with a sync app
- Bring the event quests and the DLC you already have: see [Import DLC and event quests](#import-dlc-and-event-quests)
- **Send all to the box** at the end of a quest, and a **home screen shortcut** with your own picture: see
  [Settings](#settings)
- **The game's code built on the phone**, no PC needed: see [Step 3](#step-3-build-the-games-code)
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

- **An Android device:** Android 9 or newer, 64-bit, with Vulkan. Tested on Adreno and Mali GPUs. A phone with a 32-bit
  Android (many Android Go phones) can't install the app: Android says something about "ABI". An update can't change that.
- **About 5 GB free** on the device: the game folder takes about 4 GB.
- **Optional: a PC with Windows, Linux or macOS,** once, to build the game's code for your device: Python 3.10 or newer
  and about 3 GB free. Without one, the app builds it on the phone itself (Step 3): it then needs about 1 GB more free
  while it works, an internet connection the first time, and time.
- **Your own copy of the game, decrypted:** Monster Hunter 4 Ultimate for the USA as a `.3ds` or `.cci` file, and its
  **1.3.0 update** as a `.cia` file.

This project does not explain how to dump a game from your console, and does not provide game files, keys or links to them.

## Setup

You do this once (and the third step again when a new version of the app asks for it). In short:

1. On your PC, put your game and its update in one folder.
2. Optional: one screen. The app's own single-screen mode is on by default; jM5557's mod is another way.
3. Build the game's code library, two files, in that folder: on the phone, from the app, or on a PC with the tool from
   the same version as your APK.
4. Copy the folder to your phone, install the APK, and choose the folder in the app (on the phone, the folder is chosen
   first and the app offers to build the library there).

Something went wrong? Most problems are in [If something goes wrong](#if-something-goes-wrong), with what to do.

### Step 1: Put your files in one folder

Put the game and its update in one folder, for example `C:\MH4U`. Any file names work. Use the ones from your own copy:
game files from other places are often encrypted, incomplete or for another region, and then the tool or the app stops
with an error. Once the game opens, gear → **System** → **Your game** → **Verify game files** reads the game and its
update whole (about a minute) and tells you what is missing, cut short, changed or from another version, and what to do.

### Step 2 (optional): One screen

The 3DS has two screens. The app shows the top one on your whole screen and the touch screen in a corner, which you can
move, resize, make see-through or hide (gear, **Graphics & Audio**, **Screen and HUD**).

**Single-screen mode** (gear, **Graphics & Audio**, **Screen and HUD**, on unless you turn it off, also after an
update) puts the menus of the
touch screen on the top one, in every language: the title menu, the quest counters and their lists, the canteen, the
shops, the Palico menus, the help pages and the yes / no questions. The game itself draws them there, as sharp as the rest
of the picture, so there is nothing to install and no library to make again. With the USA game the touch screen starts
hidden; its button (on the left) brings it back for what still needs a finger: the town's map, the World Map, the
keyboard and the character creation. While a menu of the game's is on the top screen, a touch button with half its
picture or more over it is drawn faint so the whole menu reads (it still works and doesn't move), and on a 4:3 screen the
gear waits in the bottom-left corner. On a screen wider than the console's, those menus are cut to the console screen's
width, as the console cut them. The touch screen's plates and yes / no questions go beside the boxes, lists and cards
instead of over them.
Nothing changes in a hunt. It is based on the single-screen mode of
[Heavenfall](https://github.com/popzol/4u-heavenfall-mod-beta), by popzol and BlitzWOLF, without Heavenfall's changes to
the game.

**jM5557's single-screen mod** still works if you prefer it (while a code mod is on, the app's single-screen mode stays
off: both would move the same menus). It is made for the USA game in English: with the game in
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

The app has no game code in it, so you build yours from your own copy, once. Nothing is sent anywhere. There are two
ways: on the phone, with no PC, or on a PC (faster).

**On the phone.** Copy the `MH4U` folder (the game, the update and, if you use it, `mods`) to the phone, install the APK
(Step 4) and choose the folder. The app says the library is missing: tap **Build on this phone**, then **Build**. Later
it is also in the gear, **System**, **Game code**, **Build the game code** (after adding or removing a mod that changes
the game's code, for one).

- A code mod in the folder's `mods`, like the single-screen mod, is built in; **Change the code mod** picks another or
  none.
- The first time it downloads its tools, about 100 MB: Python from python.org and Zig, a C compiler, from PyPI. Both
  are free software, checked before they are used, and nothing of the game is ever downloaded.
- It needs about 1 GB free while it works; when it ends, only the tools (about 200 MB) stay, and **Delete the tools**
  removes them too. It needs about 4 GB of memory (RAM) or more: on a phone with less, close the other apps first.
- In the app it took about 32 minutes on an Anbernic RG406V (a mid-range Mali-G57), and about 92 minutes on a Galaxy
  S25 Ultra that kept pausing for heat; a low-end phone should take about 2 hours. It goes on with the screen off or in
  another app, with its progress in a notification, and if it is cut (the phone restarts, the app is closed) **Go on** goes on from
  where it stopped, straight to the files still to compile, without analysing the game's code again. **Start over**, on the
  same screen after a build was cut, cancelled or failed, deletes what it left (its work files and the record it goes on
  from), so the next **Build** starts from the beginning; it asks first, and keeps the downloaded tools unless you choose
  to delete them too.
- The phone gets warm: keep it charging, out of its case if you can. Warm, it goes slower; too hot, it waits until the
  phone cools down.

When it says the game code is ready, `libmh4u_code_native.so` and `libmh4u_code_native.id` are in your folder, and
**Play** starts the game.

**On a PC:**

**Windows:**

1. Install [Python](https://www.python.org/downloads/). Tick **Add python.exe to PATH** in the installer.
2. Get the tool from the same version as your APK: on [Releases](../../releases/latest), under **Assets**, click
   **Source code (zip)**.
3. Unzip all of it (right-click the ZIP, **Extract All**). From inside the ZIP the tool can't find its other files, and
   its window closes at once.
4. Open the unzipped folder, then `mh4n`, `tools`, `android`, and double-click **Build MH4U library for Android.bat**.
5. Choose your update's `.cia`. The game beside it is found by itself; it makes the code faster.
6. When it asks about mods that change the game's code, answer **Yes** if you did step 2 and choose the `mods` folder.

**Linux and macOS:** install Python 3.10 or newer, download and unzip the release's **Source code (zip)** as above, open
a terminal in its folder and run, with your own paths:

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
   installing apps from your browser. If you had a copy of the app from another website, Android won't install this one
   over it: see [Switch to the official app](#switch-to-the-official-app).
3. Open **MH4U**, tap **Give permission and choose folder**, and turn on file access when Android asks. The game only reads
   that folder: nothing is copied or changed there.
4. Find your `MH4U` folder (it shows a ✓) and tap **Use this folder**. If you'd rather use Android's own picker, tap
   **Android's picker**.

The app checks the folder, tells you what is missing if something is wrong, and starts the game.

Your saves and settings are kept apart, in `Android/data/com.mh4u.native/files`. To choose another folder later: gear,
**System**, **Your game**, **Game folder**.

## Update to a new version

Download the new APK from [Releases](../../releases/latest) and open it: it installs over the app you have. Your saves,
settings, layout and game folder stay. If the app then says the library is from another version, do
[step 3](#step-3-build-the-games-code) again with the new version's **Source code (zip)**, and copy both new files to the
folder on your phone. Each version's notes say whether the library stays valid.

## Switch to the official app

Did you get the app from another website (apkvision, for example)? That copy was signed by someone else. Android only
installs an update signed like the app you have, so the official APK won't go over it. Android then says "App not
installed" or "package conflicts with an existing package". Do this once:

1. Keep your save. In the app you have: gear, **System**, **Your game**, **Export save**, and choose a folder, such as Download.
2. Open your phone's **Settings**, **Apps**, **MH4U**, **Storage**, and tap **Clear data** (or **Clear storage**).
3. Tap **Uninstall**. If Android asks whether to keep the app's data, say no.
4. Install the APK from [Releases](../../releases/latest).
5. Open it and choose your game folder. Then gear, **System**, **Your game**, **Import save**, **Choose a folder**, and pick the
   `MH4U save` folder from step 1.

Your game folder and your code library stay as they are. Still "App not installed"?

- You uninstalled the old copy but kept its data: install that old copy again, then do steps 2 and 3.
- The old copy may also be in a second space of the phone (Second space, Dual apps, Secure Folder or a work profile):
  uninstall it there too.

The official APK is only on this page's [Releases](../../releases/latest): see [Official downloads](#official-downloads).

## Settings

Tap the gear on the screen to open the settings. A long press on it opens the layout editor. The settings have six
sections, in the column at the left: **Controls**, **Graphics & Audio**, **Gameplay**, **Multiplayer**, **Mods & Cheats**
and **System**, with **Credits & Licenses** under them. A section's page has its main options; a row with an arrow opens
a page with the rest (**Screen and HUD**, **Advanced**, **Join a room**...). The **?** at a row's right says what it
does. Change what you like and tap **Save**; a few options say they need a restart, which **Save and restart** does for you.

- **3D resolution:** 100% is your phone's own, as sharp as its screen shows; lower is lighter on the phone. **3DS (1x)** is
  the console's own, for the weakest phones. It can only be a whole number of times the console's screen (400x240: 1x,
  2x, 3x...), so each percentage goes to the nearest of them, shown in brackets: there is no 55% or exact size, as in the
  MHGU port. The game draws its HUD and text into the same picture, so they are as sharp as this resolution.
- **Frame rate limit:** 15 to 240. 30 is the 3DS's, heats the phone less and saves battery; above 60 is for fast phones with
  a screen that fast. 15, 20 and 25 are for a phone that can't hold 30, or for the longest battery: 15 and 20 are as even as
  30 on a 60 Hz screen, 25 is less even (it doesn't divide 60).
- **Low-end mode:** **Off**, **On** or **Toaster**, as described [above](#display-and-performance). You can still change each
  of its options afterwards.
- **Effects:** off, the game's particle effects are not drawn (hits, sparks, fire, breath, dust, rain): faster where there
  are many, but the effects that warn of a monster's attack go too. It applies at once.
- **FPS counter** (gear, **Graphics & Audio**, **Screen and HUD**): the frame rate and the phone's temperature at the top
  of the screen; the layout editor moves it.
- **Clear shader cache** (gear, **Graphics & Audio**, **Advanced**): the next start deletes the shaders the game compiled
  and kept (and the GPU driver's own cache) and compiles them all again from the list of what earlier sessions drew, for
  a picture that shows something wrong after a GPU driver update or a crash. While the list is compiled, a small plate
  says how far it is.
- **Send all to the box:** at the end of a quest, the game's **Send to Item Box** sends every reward at once, as in the
  newer games, and the Palico's items too; what doesn't fit in the box stays in the list. Off by default: gear, **Gameplay**, **Send all to the box**.
  Alfonso's idea.
- **Home screen shortcut:** gear, **System**, **Home screen shortcut** puts the game on your home screen with a picture and
  a name you choose. The app's own icon stays as it is.

## Import your save

1. Copy your save to the device, for example into `Download`. From Azahar or Citra: the folder **Open Save Data Location**
   opens (right-click the game), or the emulator's whole user folder, the one with `sdmc` in it. From the console: a
   Checkpoint or JKSM backup of the game.
2. In the game, gear, **System**, **Your game**, **Import save**. The app looks for saves on the device (an Azahar app on the same
   device too): tap the one to bring in. If it isn't listed, **Choose a folder** and pick any of these: the save's own
   folder (`system`, `user1`...), its `data` or title folder, an emulator's whole folder or its `sdmc`, the backup tool's
   folder (with its dated backups), or a folder **Export save** made. If the folder holds something else (only the extra
   data, a `.sav` image, a `.zip`), the app says what it found.

The save has to be from the same game as yours: one from the other region, or from the Japanese MH4G, is refused with a
message that names both games, and so is a hunter's file cut short. The guild cards, event quests and Palico beside an
emulator's save come along. The game restarts with it, and a notice says what came in (**Share log** includes it). The
save you had is kept in `Android/data/com.mh4u.native/files/save_backups`, and it is never
deleted before the new one is in place.

## Export your save

Save in the game first. Then gear, **System**, **Your game**, **Export save**, and choose a folder: a new folder named `MH4U save`
with the date is made there, in the layout Azahar and Citra use, with the save, the guild cards, event quests and the Palico.

## Import DLC and event quests

The event quests you downloaded in the game are files of its extra data: `quest1` to `quest5`. You can bring the ones you
already have, from your own copy: an emulator, or a backup of your console. The app includes none and downloads none: you
bring your own, at your own risk.

1. Copy the folder that holds them to the device, for example into `Download`:
   - **Azahar or Citra:** the emulator's user folder, the one with `sdmc` in it (the quests are under it, in
     `extdata/00000000/00001263/user`).
   - **Checkpoint or JKSM on the console:** the game's **extdata** backup.
   - **Any folder** with the files `quest1`, `quest2`... If they come in a `.zip`, unzip it first.
2. In the game, gear, **Mods & Cheats**, **DLC and event quests**, **Import DLC and event quests**, **Choose a folder**, and
   pick that folder. The app checks it and tells you which quest files it found.
3. Tap **Import and restart**.

- Only the quest files are copied: your hunters, guild cards and Palico stay as they are. The quest files they replace are
  kept in `Android/data/com.mh4u.native/files/save_backups`.
- They must be for the same region as your game (USA: `00001263`, Europe: `00001261`). The app says so when they aren't.
- Your game needs its extra data first: start a game and save once before you import them.

**The DLC itself** (the packs of the game's own DLC shop) is a title of its own, apart from the event quests. Since 1.0.1
the same **Import DLC and event quests** brings it too, from your own copy:

1. Copy to the device the emulator's user folder you installed it in (Azahar or Citra: the one with `sdmc` in it), or a
   folder with your own DLC `.cia`.
2. Gear, **Mods & Cheats**, **DLC and event quests**, **Import DLC and event quests**, **Choose a folder**, pick that
   folder, and **Import and restart**.

- It must be decrypted: the app decrypts nothing. If the emulator compressed it when it installed it, turn that option off
  there and install it again, or use the `.cia`. The app tells you when a file can't be used, and why.
- It must be for the same region as your game.
- The game uses the DLC only for a save that already received it (one from your console or an emulator): the game's own DLC
  menu needs an online service that no longer exists, so a save that never got it can't get it now.
- The log says which DLC the game found and whether the game asked for it (gear, **System**, **Your game**, **Share
  log**). This was tried with test files only: tell us how it goes with yours.

## Choose where your save is kept

The save is in the app's own folder unless you choose another one, for example a folder that a sync app (Syncthing,
FolderSync and the like) shares with your other devices. Go to the gear, **System**, **Your game**, **Save folder**, and
choose the folder:

- Your save, with its extra data, goes in a folder named `MH4U save` inside the one you choose, so one shared folder can
  hold MH4U's and MHGU for Android's (`MHGU save`). A folder that already holds a save, such as one **Export save** made,
  is used as it is.
- Your save is copied there before anything changes. If the folder already has a save, you choose which one stays; the
  other is kept in a copy, in `save_backups` in the app's own folder.
- The game restarts to use the folder: save first. **Use the game's own folder**, on the same screen, goes back.
- On two devices, hunt on one at a time, and let the sync app finish before you open the game on the other.

When the game starts it checks the folder. If it can't reach it, if the sync app kept two versions of the save, or if
another device has the game open, it says so and lets you choose what to do.

### Keep your save in the cloud (Google Drive and the like)

The app has no cloud of its own, and it doesn't need one: **Save folder** works with any app that keeps a folder of your
phone the same as a folder in your cloud. Step by step, with Google Drive:

1. Install a sync app that keeps a phone folder and a Google Drive folder the same both ways (two-way sync), for example
   **FolderSync** or **Autosync for Google Drive**. Google Drive's own app doesn't keep phone folders in sync.
2. With your phone's file manager, make a folder for your saves, for example `Saves` in the phone's storage (not inside
   `Android/data`).
3. In the game, save. Then go to the gear, **System**, **Your game**, **Save folder**, and choose that folder. The game
   restarts, and your save (with the guild cards, event quests and Palico) is in a folder `MH4U save` inside it.
4. In the sync app, pair that folder with a new folder in your Google Drive. Choose **two-way** sync, syncing when files
   change (or every few minutes), and leave off any option that deletes or moves files after syncing. Let the first sync
   finish: `MH4U save` shows up in your Drive.
5. On your other phone or tablet: install this app and the sync app, pair the same Google Drive folder with a folder there,
   and wait until `MH4U save` has come down. Then in the game, **Save folder**, and choose that folder: the game uses the
   save that is in it.
6. From then on, hunt on one device at a time, and before you open the game on the other one, check that the sync app has
   finished on both.

If the sync app ever keeps two versions of the save, or the other device still has the game open, the game says so at its
start and lets you choose; the save not kept goes to `save_backups`. MHGU for Android keeps its save the same way
(`MHGU save`), so one synced folder can hold both.

## Mods and cheats

**Mods:** gear, **Mods & Cheats**, **Mods**, **Add a mod (.zip)** or **Add a mod (folder)**. A mod for the USA game has a `romfs` folder, an
`exefs` folder or both; the emulators' layout, with a `0004000000126300` folder inside, works too. The page lists the mods in
load order: tap one to turn it on or off, move it, or remove it. Where two mods change the same file, the one lower in the
list wins. Save and restart the game: mods load when it starts. The mods in the `mods` folder beside the game are listed too.

A mod with an `exefs` folder changes the game's code, so your code library has to be built with it (step 3, with that mod in
the `mods` folder). The **Code library** row says whether your library matches the code mods that are on. If the game stops
at start after a change of mods, its error screen offers **Mods of the last start**.

**Cheats:** gear, **Mods & Cheats**, **Cheats**, **Import cheats (.txt)**, and choose a `.txt` of codes for MH4U USA 1.3.0 in Azahar's and
Citra's format (each cheat is its `[Name]` and its code lines). They come in off: turn on the ones you want and save. They
work at once. Save your game before trying one, and use them online only if everyone in the room agrees. A cheat that
writes into the game's code does nothing by itself in this port, whose code is translated ahead of time: select it and
choose **Make a code mod**, turn that mod on in **Mods**, and build your code library again with it on (step 3).

**Faster eating and drinking:** two cheats come with the app, off, at the end of the list, as in the MHGU port. **Comer
rapido siempre (beber x1.7, carne x1.8)** gives the Speed Eating skill's effect without the skill; **Beber muy rapido
(x2.5)** drinks faster still (meat x1.8). Use one of the two. They speed up the animation, but don't let you walk while
drinking. Turned off, the effect lasts until the next quest.

### HD texture packs

The texture packs made for Azahar and Citra work as they come: put the pack's folder (the one with the `tex1_...png` files and,
if it has one, `pack.json`) inside a `textures` folder in your game folder, beside the `.cci` and `mods`. Azahar's own layout,
`textures/0004000000126300/...`, works too. Only PNG textures are used (DDS and KTX files are left out). Each texture is read
and decoded in the background the first time the game draws it (the game's own texture shows for a moment), and every HD
texture takes the phone's memory (a 1024x1024 texture is 4 MB). The pack gets a share of the memory the app gives textures,
by your phone's RAM: past it some textures stay the game's own, and those of areas you've left go after a while. If a whole
pack is too much for your phone, gear, **Mods & Cheats**, **Texture packs**, **Pack texture size: Half** draws the pack's big
textures at half their width and height (a quarter of the memory; it applies at the next start). The log says how many
textures came from the pack and how much memory they take. No pack comes with this app: get it from its author.

### Comforts

Under the gear, **Gameplay**, **Comforts** has four ideas from jM5557's [MH4U QOL mods](https://github.com/jM5557/MH4U-QOL-Cheats), done by
the app itself: no mod and no new code library. They are off until you turn them on, and work at once:

- **Hold A to gather:** hold A to carve, gather, mine and catch bugs again and again, until you let go.
- **Move to skip the flex:** after a potion or a ration, moving, rolling or any other action ends the flex. What you drank
  still works; if you stand still, the flex plays whole.
- **B cancels sharpening:** B while sharpening rolls out of it.
- **No time limit on rewards:** the rewards screen doesn't count down.

The wait after a quest already ends early here: hold ZL and ZR (gear, **Gameplay**, **End the wait**), as jM5557's *Skip
Quest End Timer* does with A and B.

jM5557's other mods change the game's code, so they work as code mods: *Full Item Box In Villages And Hubs* (the house's
full item box, with equipment and appearance, at the village and hall boxes) and the *Hide Number* version of the rewards
one. Get them from [jM5557's repository](https://github.com/jM5557/MH4U-QOL-Cheats), from its
**USA v1.1** folder (made for the 1.3.0 update), and build your code library with the one you want (step 3, with that
mod's folder, the one with `exefs` in it, in the `mods` folder). The library takes one code mod, as Azahar does (the
single-screen mod is one too); *ALL Cheats* has all of them together, comforts included, so with it leave the app's
comforts off. Their *Send All Rewards To Item Box (Hold X+Y)* isn't needed: the app's **Send all to the box** (gear,
**Gameplay**) does it with the game's own steps.

## Playing together

The game's local play goes through a network room, the same kind of rooms Azahar uses: players of this app and of Azahar can
share one. From 1.0.1, this app's players can also meet in **hubs**, with no address to type and no port to open (below).

### Hubs: online with no ports to open

As MHGU for Android's players do, you can meet in **hubs**: a hub server lists the hubs and carries their players, so
nobody types an address or forwards a port on a router.

1. Gear, **Multiplayer**: choose a **Nickname**. **Play online with** chooses what the section shows: **This app's hubs**,
   **Azahar and Citra rooms** (the rooms below, as in 1.0) or **Both** (as it comes).
2. One hunter: **Create a hub**. Give it a **Hub name** and, if you like, a **Description** (your rank, what you hunt, your
   language), a **Password** if only your friends should come in, and how many **Hunters** (2 to 4). Then **Create the
   hub**. Keep the app open while the others play in it.
3. The others: **Join a hub**. It lists the hubs open now for their copy of the game, with their hunters and a lock when
   they have a password. A tap joins one.
4. Then, as in a room: in the game, the **Gathering Hall**'s **Local Play**. One hunter creates a hall and the others find
   it there. The chat's **Room** chat is the hub's.

Only copies of the same region play together: USA with USA, Europe with Europe (Monster Hunter 4G has no hubs). Everyone
should have the same version of this app.

**Hub server** chooses the server. The default, `hunterhubid-gu-hub.duckdns.org`, is the community server MHGU's players
run (not this project); the same server keeps each game's and region's hubs apart. **Custom...** types another (a name or
an IP, and `:port` if it is not 39510), and **Off** turns hubs off. **Check hub server** says whether it answers, how long
it takes, and how many hubs it lists. Like any server on the internet, it sees your IP address; the other players don't.

With **Play online with: Both**, a hub you create also takes Azahar and Citra players: they join it as a room, at your
phone's address and its **Port (UDP)** (from the internet, once that port is forwarded to the phone).

A hub is a room on the host's phone that the others reach through the hub server: its game traffic goes by UDP through the
server when that gets through, else through the server's TCP connection. The game's own limits on local play still apply:
a slow connection, or a phone that stops for a moment, can drop a hunter, as in a room.

### Host a hub server

The hub server is the same program as MHGU for Android's,
[gu-native's `server/`](https://github.com/khorne-hub/gu-native/tree/main/server): one small Go program with no database
and no configuration file, and one running server serves both games. To run your own, for your group or closer to you:

```
git clone https://github.com/khorne-hub/gu-native
cd gu-native/server
go build -o mhgurecomp-hubserver .     # Go 1.22 or newer
./mhgurecomp-hubserver -addr :39510
```

Open port **39510 for TCP and UDP** in the firewall (and forward it, behind a router). Its options (limits per address,
bandwidth), Docker and systemd are in its README. The players then choose **Hub server**, **Custom...**, and its name or
address. A hub's game traffic goes through the server: put it where the players have a good connection.

**Crossplay with Azahar and Citra:** the rooms are the same as the multiplayer rooms of Azahar, Citra and Lime3DS, so their
players join a room of this app and you join theirs, the same way (by address, or from **Public rooms**: that lobby is where
most of them list their rooms). Everyone needs MH4U with the 1.3.0 update: a room can't tell another version, which
doesn't find your Gathering Hall. **Hunters in the room** marks in another colour anyone playing another game (Monster
Hunter 4G, or anything else): they can't meet you in the Gathering Hall. Players on an emulator don't wait for the others
when their game stutters (only this app's players keep each other in step), so a stutter there can drop them.

### Play online, step by step

1. Gear, **Multiplayer**: choose a **Nickname**.
2. Join a room, under **Join a room** on that page:
   - **Public rooms** lists the rooms of a community lobby, `http://88.198.47.46:5000`, which players run (not this
     project); MH4U's come first. Everyone in your group taps the same room to join it (an empty one, so you are alone in
     it).
   - Or type the room's **Room address** (and **Password**, if it has one) and tap **Join the room**.
   - Or host one on the phone, under **Host a room**. On the same Wi-Fi, the others tap **Search this Wi-Fi** under **Rooms on this
     Wi-Fi** and join it. Over the internet, the host forwards UDP port 24872 on their router to the phone, or everyone
     joins a room someone runs; the others type its address.
3. **Being in the room is not enough.** In the game, go to the **Gathering Hall** and choose **Local Play** (World Map,
   Solo/Multiplayer, **Local Play**). One hunter creates a hall and the others search for it there. When you join a room, a
   notice reminds you of this.

**A room only for your friends:** under **Password**, **Generate a password** makes a random one (32 letters and digits) and
copies it. Host the room with it, or give it to the room's host, and join. Then, with the online chat on, **Share this room
in the chat** sends the room's address and password to a friend online, or to General or Global: a friend taps **Join this
room** on that line and is in. A room on your phone is reached from the internet only with its port forwarded to it.

Join the room before going to the Gathering Hall. The room's chat is the **Room** chat: see
[Chat and friends](#chat-and-friends). **Lobby** picks another lobby (**Custom...**) or none (**Off**); the lobbies you type and save stay in its list, the last one first (up to 24), and the
**×** beside one takes it out. The first time you play online a notice says which server the app connects to: like any
server on the internet, it sees your IP address.

### Chat and friends

The app has a chat of its own: gear, **Multiplayer**, **Chat** and **Friends**. It has three chats, and private messages
with your friends:

- **Room:** the room's own chat, the one Azahar's rooms have, so players on an emulator in the room read it too. It needs no
  relay and no online chat: turn on **Room chat**.
- **General:** everyone in the online chat who plays MH4U.
- **Global:** everyone in the online chat of both ports, MHGU and MH4U.

**Both chats start off.** Until you turn one on, at the top of the **Chat** page, nothing of the chat shows over the game:
**Room chat** turns on the room's (no relay) and **Online chat** Global, General and friends. With the room chat off, the
room's own messages show as before: **Room messages over the game** tells who comes and goes, and what someone says.

A round chat button shows over the game while the online chat is on, or the room chat is on and you are in a room. A tap
opens the chat, a drag moves it, and the number on it counts the lines you haven't read. Under **Chat**, **Options**, **Chat
button** turns it off and **Chat button size** changes its size. New lines show for a few seconds beside it, the room's too
(with the room chat on): **Lines over the game** picks **Off**, **Room and friends** (the default) or **All**. **Room
messages over the game** still tells who comes and goes in the room.

**The online chat** (Global, General and friends) is off until you turn it on with **Online chat**, at the top of the
**Chat** page. The first time, a question explains it:

- It connects to public Nostr relays: `relay.damus.io`, `nos.lol` and `relay.primal.net`, or others you choose under
  **Relays**. Like any server on the internet, they see your IP address.
- Anyone can read Global and General: never share anything private there. Messages to a friend are encrypted on the device.
- This project runs no server and there is no account: you are a key made on the device, shown as your **friend code**
  (`XXXX-XXXX-XXXX`).
- Nothing is stored anywhere: lines aren't saved, and the chat works only while you are connected.
- Your friends, mutes, blocks and chat settings are kept only on the device, in `chat.txt` beside the app's settings.
  Uninstalling the app loses them and gives you a new friend code.

**Friends:** on the **Friends** page, **Your friend code** shows yours (a tap copies it). **Add a friend** with their code:
they are found as soon as you are both online, and they get a request. Private messages work once they add you back, by
accepting it under **Requests**. A friend's row says whether they are online, in a room, or when they were last seen. A tap
on it offers **Talk**, **Send this room** (while you are in one), **Mute**, **Block** and **Remove**.

**Mute and block** are each player's own: they only affect you, on your device. **Mute** hides someone's lines in the chats.
**Block** also stops their private messages, requests and online status, and takes them out of your friends. Use **Someone
in this chat**, on the chat page, or the friend's row. **Muted and blocked**, under **Friends**, undoes them.

**Against spam:** you can send a line every 4 seconds in Global and General (at most 5 a minute), and every second in Room
and with friends; the same line twice in a minute isn't sent. Someone who sends more than 6 lines in 20 seconds is silenced
on your device for 5 minutes. A line is up to 200 characters.

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
| Not sure your game files are right | Gear → **System** → **Your game** → **Verify game files**. It reads the game and its update whole, says what is wrong (missing, cut short, changed, another version or region, a modified copy, the code library and its `.id`) and what to do; the full list goes to the log. It changes and downloads nothing |
| "App not installed", or "package conflicts with an existing package" | You have a copy of the app from another website, signed by someone else. Keep your save and follow [Switch to the official app](#switch-to-the-official-app) |
| The installer talks about "ABI", or says the app isn't compatible | This phone runs a 32-bit Android (for example Android Go). The app needs a 64-bit one, so it can't be installed on this phone |
| The window of **Build MH4U library for Android.bat** opens and closes at once | Unzip all of the **Source code (zip)** first (right-click, **Extract All**) and open the `.bat` from the unzipped folder, not from inside the ZIP. Still closing: open that `android` folder, click the address bar, type `cmd`, press Enter, then type `"Build MH4U library for Android.bat"` and press Enter. The window stays open and shows what went wrong |
| `python` is not recognized | Install Python again, tick **Add python.exe to PATH**, and open the script again |
| "... is encrypted" | The game and the update have to be decrypted copies of your own |
| "this code.bin isn't a version the project knows" | Use the USA game's 1.3.0 update |
| The app says the game or the update is missing | Put both in the folder you chose, beside each other |
| The app says the library is missing, or is from another version | The library was made with another version's tool. Do step 3 again with the **Source code (zip)** of the same release as your APK, and copy both files (`.so` and `.id`) to the folder |
| The game stops at start, and the log says the library "was built against a different recomp.h" | Same as above: the library comes from another tool or another version. Build it again with this version's tool (step 3) and copy both files |
| The game stops at start after adding or removing a mod | A mod with an `exefs` folder needs the library built with it (step 3). On the error screen, **Mods of the last start** puts the mods back as they were |
| A big mod made for emulators doesn't work | Mods are their authors' work, and some change the game in ways this port's tool can't build yet. Heavenfall should build with this version's tool (export your save first), but it hasn't been tried with the real mod: if it fails, tell us with the log. Take the mod out of `mods`, build the library again without it (step 3), and copy both files |
| Black areas or missing parts, with game files you didn't make yourself | Game files from other places are often incomplete or damaged, and the app can't fix them. Use your own copy, as [step 1](#step-1-put-your-files-in-one-folder) says |
| It runs slowly or the phone gets hot | Lower the **Frame rate limit** or the **3D resolution**, or turn on **Low-end mode**; on the weakest phones, **Toaster** |
| The game closes by itself | Close other apps: it's usually a lack of memory. Then share the log |
| A black screen at start, with the menu's music playing (some older Mali GPUs) | When the phone's graphics refuse the usual way of showing the picture, the app now switches to another by itself. If it stays black, attach `log.txt` from `Android/data/com.mh4u.native/files` (a file manager or a PC can copy it) to an issue |

MH4U for Android is for Android only: this project has no PC version. A PC port of MH4U you find elsewhere is another
project, made by other people.

If it still fails, ask in the [Discord](https://discord.gg/sk5xEjEMwF) or open an [issue](../../issues) with your device's model, the app's version and the log. If the game closes because of an error,
a screen lets you share it; if not, it's under the gear, **System**, **Your game**, **Share log**. Don't attach game files.

## Building the app

For developers; players only need the APK from Releases. On Windows (Git Bash), Linux or macOS, with the Android SDK
(platform 35) and NDK 27.0.12077973, CMake 3.25 or newer, Ninja, Python 3.10 or newer and a JDK 17:

```
sh scripts/build_sdl3.sh    # SDL 3.4.18 from its release, once
sh scripts/build_apk.sh     # libmain.so, the hook libraries, the on-phone builder's launchers and tools, then Gradle
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
- The chat, the room keys (the room password generator) and the control profiles follow the designs of tabarong (Discord;
  GitHub: [oliveaglien](https://github.com/oliveaglien)).
- The **Comforts** follow jM5557's [MH4U QOL mods](https://github.com/jM5557/MH4U-QOL-Cheats); no code of them is in this project.
- Single-screen mode places the menus where [Heavenfall](https://github.com/popzol/4u-heavenfall-mod-beta)'s
  single-screen mode puts them, by popzol and BlitzWOLF (GPL-3.0), without Heavenfall's changes to the game.
- Building the game's code on the phone was [Klark231](https://github.com/klark231)'s idea
  ([mh4u-builder](https://github.com/klark231/mh4u-builder)); the app's builder is this project's own implementation of
  it. It downloads Python ([python.org](https://www.python.org), PSF License) and [Zig](https://ziglang.org) (MIT; with
  LLVM, Apache 2.0 with LLVM exceptions) the first time it runs; their licenses are in the app's **Licenses** too.
- Alfonso, for the idea of sending all the rewards to the box at once.
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
downloads or installs anything by itself. Gear, **System**, **Check for updates** turns it off.

## License

MH4U for Android is free software under the [GNU General Public License, version 3](LICENSE) or (at your option) any later
version, like the MH4UltimateDC runtime it builds on. The third-party parts keep their own licenses.

The code by khorne-hub carries [additional terms](ADDITIONAL-TERMS), which section 7 of the GPL allows. Whoever shares the
app, changed or not, must:

- keep the author's credits: the ones the app shows when it opens, its **Credits & Licenses** page, and this file's
  **Credits** and **License** sections;
- mark a changed version as changed, and not present it as the official one;
- give, with the app, its license and its complete source code, as the GPL requires.
