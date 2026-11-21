# MH4U for Android and PC

Play your own copy of Monster Hunter 4 Ultimate on Android, natively and with touch controls made for phones,
and on a Windows PC with keyboard, mouse or a controller ([PC (Windows)](#pc-windows)).
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
- **Your own GPU driver** on any GPU, at your own risk: gear, **Graphics & Audio**, **Advanced**, **GPU driver**. If one fails, the app
  goes back to the system's by itself and says so
- **Colour presets:** on the **Colour** page, **Preset 1** to **Preset 3** keep a colour setting each (**Save the
  current colour here**), and **Use** brings it back
- **FSR 1** (gear, **Graphics & Audio**, **Upscale filter**, off by default): the top screen scaled up with sharper
  edges where it is shown larger than its picture, with its own **FSR 1 sharpness**
- **Fast scaling (more FPS)** (gear, **Graphics & Audio**, off by default): scales the game's picture in a simpler way
  when the 3D resolution is not the size your screen shows it at (for example 2x on a 720-line screen): more frames a
  second on weak phones. Some colours may change very slightly
- **HUD size** (gear, **Graphics & Audio**, **Screen and HUD**): the whole HUD smaller, 60 to 100 %, at once, with the
  quests' talk and yes / no windows
- **Text outline** (gear, **Graphics & Audio**, **Screen and HUD**, off by default): a thin dark outline around the
  game's letters, for text that is hard to read
- **Sound in screen recordings** (gear, **Graphics & Audio**, **Picture and sound**, off by default): for phones whose
  screen recorder records no sound of the game, from the next start
- **Devices with two screens:** gear, **Graphics & Audio**, **Screen and HUD**, **Layout**, **On the second screen**
  puts the touch screen on the second display, touched there; it shows only when there is one, and if it goes away your
  previous layout comes back. Side by side, the touch screen keeps clear of a punch hole or notch at the screen's edge

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
- **Combined buttons:** up to 15 buttons of your own that press several at once, such as A+X, each control profile with
  its own. Each one can also go on any controller button (**Controller button**): A, B, X and Y by their place, L, R,
  ZL, ZR, START, SELECT, the D-pad, the stick clicks, the paddles or the touchpad. **Press a controller button** takes
  even one the controller's map doesn't name, as some paddles on Android; that button then presses the combined button
  instead of its own
- **The layout editor:** a long press on the gear. Move and resize every button, and move the game's own HUD: the clock,
  the health and stamina bars, the sharpness, the name plates, the area's map, the target camera and the item bar. The
  menu pad and the run's padlock circle move and resize there too. The editor's panel moves out of the way when you drag
  its title, and gets smaller down to 60 %
- **Control profiles:** 10 slots for your touch layouts, each with everything: the buttons, the HUD, the FPS counter's
  place, held buttons and pictures. Each one is the port's own layout or an **emulator-style** one (A, B, X and Y in a
  diamond, the D-pad, the shoulder buttons, START and SELECT, each pressing only its own button). In the editor,
  **Profile**; or gear, **Controls**, **Control profiles**. They are kept in the `layout profiles` folder of your save
  folder, and your layout from before is profile 1. Designed by tabarong
- **Export and import a profile:** gear, **Controls**, **Control profiles**, a profile, **Export** puts it in a .zip
  with its button pictures and combined buttons, and **Import** brings one back (a picture whose name is taken comes in
  renamed). Leaving a profile without saving never empties it, and the editor asks before **Load** or **New** drop
  changes you haven't saved
- **Each part of the HUD** can be made smaller or larger (- / + or a pinch) and hidden in the layout editor; **Reset**
  and **Defaults** bring it back
- **Map button:** in a hunt, a button at the left edge shows and hides the map on the main screen (gear, **Controls**,
  **Buttons on the screen**, **Map button**). To have the map on the main screen at all, take the Map off your panels in
  Customize Panels (the quest menu)
- **Weapon tree at the Smithy:** while the Smithy shows a weapon (Forge or Upgrade), a round button (or the right
  stick's click) opens that weapon's tree: the ones you have (box or equipped), the ones before them, and the next step
  from each as "?". Read-only, names in the game's language. **Weapon tree button** under **Buttons on the screen**
  takes the button away
- **Buttons that stay pressed:** in the editor, tap a button and turn on **Hold**: a tap holds it down, the next lets go
- **Your own button pictures:** in the editor, tap a control and choose **Picture** (add them there with **Add pictures**,
  or gear, **Controls**, **Touch controls**, **Button images**). Every control the editor places takes one, the stick's
  base and knob each their own (**For: base / knob**), and your picture is the control's whole look: only your PNG is
  drawn, a little smaller and darker while pressed. What you add is your own responsibility
- **Camera on the right half only** (gear, **Controls**, **Touch controls**, off by default): a drag turns the camera
  only on the right half of the screen, so a missed touch near the stick doesn't move it
- A tap on the stick's centre or on the menu pad's SELECT in the editor picks it for a picture of its own
- **Floating stick**, off by default: gear, **Controls**, **Touch controls**, **Floating stick**. The movement stick comes
  to where your thumb lands near it, and goes back when you lift it
- **Hide the controls:** hold one finger still for a moment on an empty spot (no button there) and every touch control
  hides, the gear too; do it again anywhere to bring them back. A camera drag, a hold on a button or a second finger never
  hides them. On by default: gear, **Controls**, **Touch controls**, **Long press hides the controls**
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
picture or more over it is drawn faint so the whole menu reads (it still works and doesn't move); the gear stays where it
is. On a screen wider than the console's, those menus are cut to the console screen's
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

**Mods:** gear, **Mods & Cheats**, **Mods**, **Add a mod** (its .zip) or **Add a mod (folder)**. A mod for the USA game has a `romfs` folder, an
`exefs` folder or both; the emulators' layout, with a `0004000000126300` folder inside, works too. The page lists the mods in
load order: tap one to turn it on or off, move it, or remove it. Where two mods change the same file, the one lower in the
list wins. Save and restart the game: mods load when it starts. The mods in the `mods` folder beside the game are listed too.

A mod with an `exefs` folder changes the game's code, so your code library has to be built with it (step 3, with that mod in
the `mods` folder). The **Code library** row says whether your library matches the code mods that are on. If the game stops
at start after a change of mods, its error screen offers **Mods of the last start**.

**Cheats:** gear, **Mods & Cheats**, **Cheats**, **Add cheats**, and choose a `.txt` of codes for MH4U USA 1.3.0 in Azahar's and
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
`textures/0004000000126300/...`, works too. PNG and KTX2 textures are used (DDS and KTX files are left out). Each texture is read
and decoded in the background the first time the game draws it (the game's own texture shows for a moment), and every HD
texture takes the phone's memory (a 1024x1024 texture is 4 MB). The pack gets a share of the memory the app gives textures,
by the memory your phone has free when the game starts (never less than before): past it some textures stay the game's own, and those of areas you've left go after a while. If a whole
pack is too much for your phone, gear, **Mods & Cheats**, **Texture packs**, **Pack texture size: Half** draws the pack's big
textures at half their width and height (a quarter of the memory; it applies at the next start). The log says how many
textures came from the pack and how much memory they take. No pack comes with this app: get it from its author.

#### Packs in KTX2

A pack can bring `.ktx2` files too, named as its `.png` files (`tex1_..._12.ktx2`); where a texture has both, the `.ktx2` is
used, so a pack can keep its PNGs for older versions. KTX2 files are smaller and quicker to read. Read: Basis Universal's ETC1S
and UASTC, and files already in RGBA8 or BC7. On a phone they take the same memory as a PNG for now (they are turned into
RGBA8). To convert a pack yourself, with your own copy and your own tools:

- toktx (KTX-Software): `toktx --t2 --encode uastc --zcmp 18 --lower_left_maps_to_s0t0 tex1_..._12.ktx2 tex1_..._12.png`
  (`--encode etc1s` makes smaller files, a little less sharp).
- basisu (Basis Universal): `basisu -uastc -ktx2 tex1_..._12.png` (`-etc1s` for ETC1S). Don't add `-y_flip`: basisu doesn't
  write it into the file, and the texture would show upside down.

Keep the names: a texture is found by its name. A file the app can't read stays the game's own texture, and the log says why.

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

**Both chats start off.** Until you turn one on, on the **Chat** page (under the lines), nothing of the chat shows over the game:
**Room chat** turns on the room's (no relay) and **Online chat** Global, General and friends. With the room chat off, the
room's own messages show as before: **Room messages over the game** tells who comes and goes, and what someone says.

The **Chat** page opens as a chat window over the game, smaller and see-through, with the chat alone: its header has a
tab for each chat there is now (Room, General, Global, Friends), each with the lines you haven't read (under **Friends** the
arrows go from friend to friend), the gear opens the chat's options (the switches, **Friends**, the lines
over the game, the chat button...) and X closes it; under it the text to say and **Send**, and the lines, the newest
first, each on a dark strip so it reads over grass or snow. Drag its top to move it and its bottom right corner to make
it larger or smaller; it stays where you leave it (the chat's options: **Chat window back in its place**). While your
phone's keyboard is up, the window goes to the top of the screen and is as tall as the keyboard leaves: the text,
**Send** and the last lines stay in sight. The chat button is moved and sized in **Customize controls and HUD**, like
any button, and kept with the control profile.

Played with your fingers, the game goes on under the chat window and the touch controls work round it: only the
window's own controls take your finger, and a tap on its blank part is the stick's or the camera's. While you type,
a tap away from the window closes it and the keyboard. A menu of the game's hides it until it closes, the settings
button goes to the menu, and the chat button closes it. In the chat's options, **Window background**, **Message
background** and **Typing field background** go from 0 to 100 %; the text is always solid.
On PC the chat window floats the same way, worked with the mouse: a click on its controls is the window's, anywhere
else the game's; the keys are the game's except while you type in it, and **F1** goes to the menu.

A round chat button shows over the game while the online chat is on, or the room chat is on and you are in a room. A tap
opens the chat, a drag moves it, and the number on it counts the lines you haven't read. Under **Chat**, **Options**, **Chat
button** turns it off and **Chat button size** changes its size. New lines show for 8 seconds beside it, up to four, each chat in its colour and each player's name in its own (not yours; they go when a menu opens), the room's too
(with the room chat on): **Lines over the game** picks **Off**, **Room and friends** (the default) or **All**. **Room
messages over the game** still tells who comes and goes in the room.

**The online chat** (Global, General and friends) is off until you turn it on with **Online chat**, on the **Chat**
page. The first time, a question explains it:

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

On a PC, see [If something goes wrong on PC](#if-something-goes-wrong-on-pc).

If it still fails, ask in the [Discord](https://discord.gg/sk5xEjEMwF) or open an [issue](../../issues) with your device's model, the app's version and the log. If the game closes because of an error,
a screen lets you share it; if not, it's under the gear, **System**, **Your game**, **Share log**. Don't attach game files.

## PC (Windows)

The same game folder plays on a Windows PC: `MH4U.exe` runs your game natively, with the same runtime, settings and
features as the app, and what a PC adds: keyboard and mouse, any window size, high frame rates and Discord.

The download has the program only: the game code (`mh4u_code_native.dll` and `mh4u_code_native.id`) is made once from
your own copy, by the program itself: open it, choose your game folder and press **Build the game code on this PC**
([PC step 3](#pc-step-3-build-the-game-code)). Nothing else is needed: no Python or compiler of your own.

### What it does on PC

- **Keyboard and mouse:** WASD moves, the mouse turns the camera, left click attacks (X) and F is the action button (A):
  it talks, opens, gathers and picks up. The right click is A too, but only with the weapon out, in a menu or a talk, or
  out of towns and quests: in town it never talks, gathers or carves by mistake. A tap on Alt locks onto the monster.
  Every key can be changed, up to three per button, mouse buttons included. In the game's menus WASD moves the cursor.
  See [PC controls](#pc-controls)
- **Type with your keyboard:** your hunter's name, and a chat line in a room or hub with **/**
- **F2 shows the list of the controls** over the game, with your own keys, and the game waits while it is open
- **The touch screen beside the game:** the 3DS's bottom screen shows beside the top one, and the mouse is your finger on
  it. It can go under the top screen, in a corner, or in a window of its own; **Single-screen mode** works as in the app
- **Any window:** a borderless full screen at your screen's resolution by default, a window of any size or exclusive full
  screen. **F11** or **Alt+Enter** switches. Drag a window's edge and the picture takes its new shape at once, from 5:3
  to ultra-wide, with the HUD at its edges (or over the middle 16:9, 21:9 or 32:9: **HUD anchor**)
- **Frame rate:** limits up to 240, or **No limit**, with **VSync** on or off; the game keeps its normal speed at any
  frame rate. **Show FPS and PC stats** shows the frame rate, and with **Full** the GPU's and the CPU's use
- **The same picture options as the app:** 3D resolution, Low-end mode, the colour filter, the PS1 filter, Brick, frame
  generation, FSR 1, and an **Upscale filter** for the scaled picture (Smooth, Sharp pixels, Nearest or FSR 1)
- **A controller:** connect it and play. START and SELECT together open the settings; a click of the right stick locks
  onto the monster; the menus can show your controller's buttons (**Button icons**). A controller with a gyroscope can
  aim bows and bowguns, as in the app (**Controls**, **Aim with the gyro**)
- **Discord:** with the Discord app open on the same PC, your profile shows what you are doing: the menus, the town, or
  the monsters you hunt, the map and the area, your weapon, and your hunter's name and HR. **F1**, **Multiplayer**,
  **Discord** turns it off, or leaves out your name
- **Saves, mods, cheats, DLC and event quests, hubs and chat** work as in the app, from the same **F1** menu: see
  [PC saves](#pc-saves), [PC mods and cheats](#pc-mods-and-cheats) and [PC multiplayer](#pc-multiplayer)
- **Verify game files** reads your game and update and says what is wrong (**F1**, **System**, **Your game**)
- If the game closes because of an error, a window tells you what happened and opens the log's folder
- The game stops while its window is minimized, and goes on where it was when you bring it back (**F1**, **System**,
  **Pause in the background**)
- **A new version:** at most once a day the program asks GitHub for the latest version (nothing else is sent) and tells
  you at the next start when there is a new one; it never downloads anything by itself. **F1**, **System**, **Check for
  updates** turns it off

### How well does it run on PC?

Measured on the developer's PC (Ryzen 5 3600XT, Radeon RX 6800 XT), borderless on a 3440x1440 160 Hz monitor with the
3D picture at 6x:

| Where | Settings | Frame rate |
|---|---|---|
| The village | No limit, VSync off | 293 to 315 fps |
| The Gathering Hall | No limit, VSync off | 408 to 420 fps |
| A quest | No limit, VSync off | 305 to 310 fps |
| Village, hall and quest | A limit of 60, 120, 144 or 240 | that limit, steady |
| Village, hall and quest | VSync on, at 160 Hz | 160 |

It hasn't been measured on integrated graphics or a weak PC yet. If you try one, an [issue](../../issues) with your
processor, graphics card and frame rate helps.

### What you need on PC

- **Windows 10 or 11,** 64-bit.
- **A graphics card with Vulkan,** with its driver up to date. The window itself is drawn with OpenGL, which every
  Vulkan driver for Windows has too.
- **8 GB of RAM** recommended. The game itself takes less than 1 GB.
- **Free space:** about 4.3 GB for your game folder, about 2 GB more while the game code is made (about 300 MB stays,
  the tools, which can be deleted afterwards), and about 200 MB for the program and its caches.
- **An internet connection** the first time the game code is made: its tools take about 90 MB.
- **Your own copy of the game, decrypted:** the same as for the app, Monster Hunter 4 Ultimate for the USA as a `.3ds` or
  `.cci` file and its **1.3.0 update** as a `.cia` file.

### PC setup

You do this once.

#### PC step 1: Put your files in one folder

The same folder as for the app ([Step 1](#step-1-put-your-files-in-one-folder)): your game and its update side by side,
for example in `C:\MH4U`. If you use the app too, the same folder works for both, and each keeps its own game code in it.

#### PC step 2: Install it

There is no installer: the program is a folder.

1. Download `MH4U-PC-<version>.zip` from [Releases](../../releases/latest) and unzip all of it into a folder of your own,
   such as `C:\Games` (opened from inside the zip, `MH4U.exe` doesn't start). Not into Program Files: the program keeps
   its settings, caches, log and the tools that make the game code in its own folder, and Windows doesn't let it write
   there.
2. Open `MH4U.exe`. Windows may say it doesn't know the program, because it isn't signed: **More info**, **Run anyway**.
3. The first time, a screen asks for your game folder: **Choose folder**, and pick it. The program checks it and says
   exactly what is missing if something is wrong. It remembers the folder.

#### PC step 3: Build the game code

The program has no game code in it, so it makes yours from your own copy, once, on your PC. Nothing is sent anywhere.

When your folder has everything but the game code, the screen says that `mh4u_code_native.dll` is missing: press **Build
the game code on this PC**, then **Build**.

- The first time it downloads its tools, about 90 MB: Python from python.org and Zig, a C compiler, from PyPI, or from
  ziglang.org, Zig's own site, when PyPI can't be reached. Both are free software, checked against their SHA-256 before
  they are used, and nothing of the game is ever downloaded.
- It needs about 2 GB free on the disk where `MH4U.exe` is while it works. When it ends only the tools stay (about
  300 MB, in the `builder` folder beside `MH4U.exe`), and **Delete the tools** removes them too.
- It takes about 10 to 40 minutes, depending on the PC (13 minutes on the developer's, a 6-core processor). It uses all
  the processor's cores but one, at low priority, so you can use the PC meanwhile.
- **Cancel** stops it, and so does closing the window or restarting the PC: what it did is kept, and **Go on** continues
  from there. **Start over** forgets a build half done and starts from the beginning.
- A code mod in your folder's `mods` (the single-screen mod, for one) goes into the game code, as in the app: **Change the
  code mods** chooses which.

When it says the game code is ready, your folder has two new files, `mh4u_code_native.dll` and `mh4u_code_native.id`, and
**Play** starts the game. **These files are made from your game: keep them for yourself and don't share them.**

To choose another folder later: **F1**, **System**, **Your game**, **Game folder**. To make the game code again: **F1**,
**System**, **Game code**, **Build the game code** (the game asks, closes and opens the builder).

The zip holds one folder with the program's files and nothing of the game:

- `MH4U.exe`, and `SDL3.dll` beside it
- `zigcc.exe` and `builder_tools.zip`: what the program's builder uses to make the game code
- `code_library_id.txt`: the id of the game code this version needs. Your folder's `mh4u_code_native.id` must say the same
- `cheats`: the optional cheats for faster eating and drinking, which the program adds to your cheats, off
- `licenses`: the legal notice and the licenses, also in **F1**, **Credits & Licenses**, **Licenses**
- `README.txt`: a short guide

### PC controls

- **Keyboard and mouse:** WASD moves, left Shift walks, I, J, K, L turn the camera. Left click, U or X is X; F, O or the
  right click is A (the right click only with the weapon out, in a menu or a talk, or out of towns and quests); Space is B,
  and so is Esc; R, Y or the middle button is Y; Q is L, E is R; Tab and T are ZL and ZR; Enter is START, Backspace
  SELECT, and the arrows are the d-pad. A tap on Alt locks onto the monster. The mouse wheel steps the item bar. Click the
  window to turn the camera with the mouse; Esc lets the mouse go.
- **The program's own keys:** **F1** the settings, **F2** the list of the controls, **F11** or **Alt+Enter** full screen
  or a window, **1** to **4** the items of the touch screen's item shortcuts, **G** the town's Gestures, and **/** the
  chat in a room or hub.
- **Every key can be changed,** up to three per button, mouse buttons included: **F1**, **Controls**, **Keyboard and
  mouse**, **Keybinds**. Alt, Ctrl and the Windows key only press their button when tapped alone, so Alt+Tab and
  Alt+Enter don't. The mouse's axes can be inverted there too.
- **The touch screen:** a click on it is a tap. On a PC with a touchscreen, the touch controls show when you touch it.
- **The settings menu (F1)** works with the mouse, the keyboard (the arrows, Enter, Esc, Tab and Page Up/Down change the
  section, Ctrl+S saves) or a controller. A click outside it closes it, asking first if something isn't saved.
- **The HUD:** **F1**, **Controls**, **Customize controls and HUD**: drag any part with the mouse, the wheel
  changes its size, **Hide** takes the game's own parts off the screen too, Esc cancels.
- **Controller:** connect it and play. START and SELECT together open the settings. If the game doesn't read yours well:
  **Controls**, **Controller**, **Map a controller**.

### PC graphics and frame rate

Under **F1**, **Graphics & Audio**: **Frame rate limit** (15 to 240, or **No limit**; 60 by default), the 3D resolution,
the **Upscale filter** (Smooth, Sharp pixels, Nearest or FSR 1), Low-end mode and the filters as in the app. Under its
**Screen and HUD**: **Window mode** (Borderless, Windowed or Full screen), **Screen resolution**, **VSync** (on by
default), **HUD anchor**, **HUD size**, **Show FPS and PC stats** (Basic or Full), Single-screen mode and the touch
screen's **Layout** (Side by side, the PC's default; Stacked; In a corner; In its own window). Under its **Colour**:
**HDR output** (off by default; with Windows' HDR on for the screen). Under its **Advanced**: **GPU**, on a PC with two
graphics cards (by default a dedicated one). The same places as in MHGU for PC.

- **No limit** needs **VSync** off: with VSync on, the monitor's refresh rate is the most. With VSync on and a limit that
  divides the monitor's rate (60 on 120 or 240 Hz, 30 on 60 Hz), every frame shows for the same time.
- **Frame generation** needs VSync and a limit of half the monitor's rate or less, as on a phone.
- **HDR output** (Graphics & Audio > Colour) needs Windows' HDR on for the screen (Settings > Display > Use HDR) and a
  restart; the game draws an ordinary picture, so HDR spreads its light out and adds no detail.
- The window says **Starting the game...** until the game's first picture. The first start takes a little longer while
  the graphics driver prepares its shaders; the next starts are quick.
- On a laptop with integrated graphics, lower the **3D resolution** or turn on **Low-end mode**; on a laptop with two
  graphics cards, check that **GPU** shows the dedicated one.

### PC saves

Your save is in `%APPDATA%\MH4U` (type it in File Explorer's address bar), so a new version unzipped elsewhere finds it.
From **F1**, **System**, **Your game**, as in the app ([Import your save](#import-your-save),
[Export your save](#export-your-save), [Choose where your save is kept](#choose-where-your-save-is-kept)):

- **Import save:** choose a folder: an emulator's (Azahar's or Citra's user folder, or its `sdmc`), a Checkpoint or JKSM
  backup, or a folder exported from the app. The game finds the save in it, says what it found, and restarts with it; the
  save it replaces is kept in `%APPDATA%\MH4U\save_backups`.
- **Export save:** choose a folder, and a `MH4U save <date>` folder is made in it, with `sdmc` in Azahar's layout: copy it
  over the emulator's `sdmc`, or import it in the app.
- **Save folder:** keep the save in another folder, such as one a sync app shares with your other devices. Nothing is
  overwritten without asking: the save that doesn't go on is kept in `save_backups`.
- **Import DLC and event quests** (**F1**, **Mods & Cheats**, **DLC and event quests**): the DLC you installed in Azahar,
  or a decrypted `.cia` of your own, and your event quests, as in the app ([Import DLC and event quests](#import-dlc-and-event-quests)).
  **DLC in every save** works as in the app, and copies your save to `save_backups` first.

### PC mods and cheats

As in the app ([Mods and cheats](#mods-and-cheats)), under **F1**, **Mods & Cheats**: **Add a mod** (its .zip) or **Add a mod
(folder)**, **Add cheats**, and the mods in your game folder's `mods`. HD texture packs go in your game folder's
`textures`: **Texture packs**, **Open the textures folder** opens it in File Explorer. A mod with an `exefs` folder needs
the game code built with it: **F1**, **System**, **Game code**, **Build the game code**, and **Change the code mods** on the
builder's page.

### PC multiplayer

Hubs over the internet, rooms on your network and the chat work as in the app, with the same settings under **F1**,
**Multiplayer** ([Playing together](#playing-together)). A PC and a phone play in the same hub.

- **/** opens the chat to type a line (**Controls**, **Keyboard and mouse**, **Keybinds**, **Chat key** changes it).
- When you host a hub that others join directly (with emulators, or on your network), Windows Firewall asks the first
  time whether MH4U may use the network: allow it, or nobody can find or join your hub. Through the hub server alone it
  doesn't ask.

### Where things are kept on PC

| What | Where |
|---|---|
| Settings, the folder you chose, the log | Beside `MH4U.exe`: `mh4u-app.ini`, `game_folder.txt`, `log.txt` (the sessions before: `log_prev.txt`, `log_prev2.txt`), and `notices.txt` (when it last looked for a new version) |
| The game's small files, the game's own settings, cheats, mods and DLC you added | Beside `MH4U.exe`: `game` (read from your game and update), `user` (`mh4u.ini`, `cheats`, `load/mods`, and the DLC under `sdmc`) |
| The shader cache | `%LOCALAPPDATA%\MH4U\cache`: **F1**, **Graphics & Audio**, **Advanced**, **Clear shader cache** empties it |
| The tools that make the game code, and their log | Beside `MH4U.exe`: `builder` (Python and Zig; while it builds, its work folder too), `builder_log.txt` |
| Your save | `%APPDATA%\MH4U` (or the folder you chose in **Save folder**). A save or event quests you import wait in `save_import` and `save_import_extra` beside `MH4U.exe` until the game restarts |
| Copies of the saves an import or a change replaced | `%APPDATA%\MH4U\save_backups` |
| Your game, the game code, mods and HD packs you put there | Your game folder: `mh4u_code_native.dll` and `.id`, `mods`, `textures` |

### Update the PC version

1. Unzip the new version and copy its files over the old ones. Your settings and the folder you chose stay, and your save
   stays in any case.
2. If the new version needs new game code, the game says "This mh4u_code_native.dll is for another version" when it
   starts: press **Build the game code on this PC**, then **Build** (the tools are already there).

### Uninstall the PC version

- Delete the program's folder, the one with `MH4U.exe`. Its settings, caches, log, the cheats, mods and DLC you added and
  the builder's tools (`builder`) go with it.
- Your save stays in `%APPDATA%\MH4U`. Delete that folder too only if you want it gone, and keep a copy first.
- The shader cache stays in `%LOCALAPPDATA%\MH4U`: delete that folder too.
- Your game folder is yours: keep it for the app, or delete it.
- Nothing else is installed: there was no installer, and the program keeps no settings in the registry. If you allowed
  MH4U through Windows Firewall, that rule stays until you remove it in Windows Security.

### If something goes wrong on PC

| Problem | What to do |
|---|---|
| "SDL3.dll was not found" | Unzip the whole zip into a folder first: `MH4U.exe` doesn't start from inside the zip |
| Smart App Control blocked it | It blocks programs that aren't signed, and `MH4U.exe` isn't: it only runs with Smart App Control off (Windows Security, **App & browser control**). Think it over first: on many versions of Windows it can't be turned back on without reinstalling Windows |
| "The game's code library (mh4u_code_native.dll) isn't in the folder" | The download doesn't include it: press **Build the game code on this PC** on that screen ([PC step 3](#pc-step-3-build-the-game-code)) |
| "This mh4u_code_native.dll is for another version" | It was made for another version, or for the app: press **Build the game code on this PC**, and it is made again for this one |
| The builder can't download its tools | Check the internet connection and press **Go on**: the download continues where it stopped. When PyPI can't be reached, the builder gets Zig from ziglang.org by itself. If both are blocked, another network, or a DNS such as 1.1.1.1, gets around it |
| Your antivirus warns about, blocks or deletes `zig.exe` or `python.exe` in the `builder` folder | They are the official Zig and Python, checked by their SHA-256 before they run. Allow the `builder` folder beside `MH4U.exe` and press **Go on** |
| The builder says there isn't enough space | It needs about 2 GB free on the disk where `MH4U.exe` is. Free some, or move the program's folder to another disk, and press **Go on** |
| The builder stopped and **Go on** fails again | **Start over** starts from the beginning. If it still fails, attach `builder_log.txt` (beside `MH4U.exe`) to an [issue](../../issues) |
| It asks for the game folder at every start | Windows doesn't let it write in its folder: move it out of Program Files |
| It closes at the start, or the window stays black | Update your graphics card's driver. Until the game's first picture the window says **Starting the game...**; the first start takes a little longer while the shaders are prepared |
| The mouse doesn't turn the camera | Click the window first. Esc lets the mouse go |
| It runs slowly | Lower the **3D resolution** or turn on **Low-end mode**. On a laptop with two graphics cards, check that **GPU** shows the dedicated one |
| Not sure your game files are right | **F1**, **System**, **Your game**, **Verify game files**: it reads every file and says what is wrong and what to do |

If the game closes because of an error, a window says so; **Open the log folder** opens the folder with `log.txt` (if you
opened the game again since, that session's is `log_prev.txt`). It's also in **F1**, **System**, **Your game**, **Log
folder**. Attach it to an [issue](../../issues) with your PC's processor and graphics card, or ask in the
[Discord](https://discord.gg/sk5xEjEMwF). Don't attach game files.

## Building the app

For developers; players only need the APK or the PC zip from Releases. On Windows (Git Bash), Linux or macOS, with the Android SDK
(platform 35) and NDK 27.0.12077973, CMake 3.25 or newer, Ninja, Python 3.10 or newer and a JDK 17:

```
sh scripts/build_sdl3.sh    # SDL 3.4.18 from its release, once
sh scripts/build_apk.sh     # libmain.so, the hook libraries, the on-phone builder's launchers and tools, then Gradle
```

`scripts/android_env.sh` says where each tool is looked for. Sign with your own key in `android/keystore.properties` and
`android/mh4u.keystore`; without them you get a debug APK. `app_native/` is the app (SDL3 and Dear ImGui), `android/` its
Android side, `app_native/pc/` the PC's, and `mh4n/` the runtime and the recompiler (see [mh4n/README.md](mh4n/README.md)).

The PC version, in Git Bash on Windows, with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases) 20241217 for
x86_64 with the UCRT, CMake 3.25 or newer, Ninja and Python 3.10 or newer:

```
LLVM_MINGW=C:/llvm-mingw-20241217-ucrt-x86_64 bash scripts/build_pc.sh            # build/pc/MH4U, as a player unpacks it
LLVM_MINGW=C:/llvm-mingw-20241217-ucrt-x86_64 PACK=1 bash scripts/build_pc.sh     # and the release's zip with its .sha256
```

It builds SDL 3.4.18 for Windows from its release the first time.

## Legal

This project is not affiliated with, endorsed by or sponsored by the Great C or the Great N. Monster Hunter is a trademark of
the Great C. The 3DS is a trademark of the Great N.

This repository and its releases contain no game code, game data, ROMs or keys. Use it only with a copy of the game you own.
Requests for game files, keys or ready-made `libmh4u_code_native.so` or `mh4u_code_native.dll` files will be closed.

Rights holders: please open an issue to get in touch.

## Credits

- The game's recompiler and the native runtime come from [MH4UltimateDC](https://github.com/CompaLarios18/MH4UltimateDC)
  by CompaLarios18 and its contributors.
- Parts of the runtime are derived from [Azahar](https://github.com/azahar-emu/azahar) and Citra.
- From the same author: [MHGU for Android and PC](https://github.com/khorne-hub/gu-native), Monster Hunter Generations
  Ultimate recompiled for Android and Windows the same way. Parts of the PC version (its stats readout, its zip reader and
  Discord Rich Presence) follow MHGU for PC's.
- The single-screen mod is [jM5557's](https://github.com/jM5557/mh4u-single-screen-mod), and is not part of this project.
- Contributed to development:
  - tabarong
  - Klark231
  - Alfonso Matador
  - popzol
  - BlitzWOLF
  - jM5557
- Single-screen mode places the menus where [Heavenfall](https://github.com/popzol/4u-heavenfall-mod-beta)'s
  single-screen mode puts them, by popzol and BlitzWOLF (GPL-3.0), without Heavenfall's changes to the game.
- The app's builder, which makes the game's code on the phone, downloads Python ([python.org](https://www.python.org), PSF
  License) and [Zig](https://ziglang.org) (MIT; with LLVM, Apache 2.0 with LLVM exceptions) the first time it runs; their
  licenses are in the app's **Licenses** too.
- The libraries this project uses, with their licenses, are listed in [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES). Their
  licenses are also in the app: gear, **Credits & Licenses**, **Licenses**.

## Official downloads

MH4U for Android and PC is free. The only official downloads are the links khorne-hub gives, starting with this repository's
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

The PC version is the zip `MH4U-PC-<version>.zip`, with its SHA-256 beside it in `MH4U-PC-<version>.zip.sha256`. To
check yours, in a Command Prompt: `certutil -hashfile MH4U-PC-<version>.zip SHA256`, and compare it with that file.

The app also tells you when a new version is out. At most once a day it asks GitHub for this repository's latest release
(nothing else is sent, not even the phone's model), and at the next start it offers to open the release page. It never
downloads or installs anything by itself. Gear, **System**, **Check for updates** turns it off.

## License

MH4U for Android and PC is free software under the [GNU General Public License, version 3](LICENSE) or (at your option) any later
version, like the MH4UltimateDC runtime it builds on. The third-party parts keep their own licenses.

The code by khorne-hub carries [additional terms](ADDITIONAL-TERMS), which section 7 of the GPL allows. Whoever shares the
app, changed or not, must:

- keep the author's credits: the ones the app shows when it opens, its **Credits & Licenses** page, and this file's
  **Credits** and **License** sections;
- mark a changed version as changed, and not present it as the official one;
- give, with the app, its license and its complete source code, as the GPL requires.
