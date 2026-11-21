# Changelog

[Español más abajo](#español)

## 1.0.2

Your code library stays valid: there's no need to build it again.

1.0.2 brings MH4U for PC, fixes for what you reported since 1.0.1.1 and a big batch of your suggestions, most of them in both ports.

**MH4U for PC.** The same game folder now plays on a Windows PC: `MH4U-PC-1.0.2.zip` on the release, with its
SHA-256. The Android app's code library stays valid: there's no need to build it again for the phone.

### MH4U for PC (new)

- `MH4U.exe` runs your own copy natively on Windows 10 and 11 (64-bit, a graphics card with Vulkan), with the same
  runtime, settings, saves, mods and multiplayer as the app. The zip has the program only, nothing of the game.
- **The game code is built by the program itself** from your own game and update, on your PC: it downloads Python and
  Zig the first time (about 90 MB, checked by their SHA-256), then takes 10 to 40 minutes, at low priority. **Cancel**,
  **Go on** and **Start over** as on the phone; the code mods in your folder's `mods` go in, as in the app. Later:
  **F1**, **System**, **Game code**, **Build the game code**.
- **Keyboard and mouse:** WASD, the mouse turns the camera, left click attacks, F is A, a tap on Alt locks on, the right
  click is A only with the weapon out; every key can be changed (up to three a button, mouse buttons too); **F2** lists
  your controls over the game; **1** to **4** the touch screen's item shortcuts; **/** the chat. WASD moves the cursor
  in the game's menus; the hunter's name and the chat are typed with the keyboard.
- **The touch screen** beside the top one (the PC's default), under it, in a corner or in its own window, with the mouse
  as your finger; single-screen mode as in the app.
- **Window:** borderless full screen by default, any window or exclusive full screen (**F11** / **Alt+Enter**); the
  picture follows the window's shape live, from 5:3 to ultra-wide; **HUD anchor** for wide screens.
- **Frame rate:** limits up to 240 or **No limit**, VSync on or off; with no limit on the developer's PC (3440x1440,
  6x), 293 to 315 fps in the village, 408 to 420 in the Gathering Hall, 305 to 310 in a quest; the limits hold steady.
  **Starting the game...** shows until the first picture. **Show FPS and PC stats** (Basic, Full). **Upscale filter**
  (Smooth, Sharp pixels, Nearest or FSR 1), **HDR output** (off by default), **GPU**.
- **Controllers:** START and SELECT open the settings, the right stick's click locks on, **Button icons** in your
  controller's style; **Aim with the gyro** reads a controller's gyroscope.
- **Saves:** your save in `%APPDATA%\MH4U`; **Import save**, **Export save** and **Save folder** as in the app, with
  copies in `save_backups`. **Import DLC and event quests** and **DLC in every save** as in the app.
- **Discord Rich Presence**, and **Verify game files**. If the game closes because of an error, a window says what
  happened and opens the log's folder.
- **Pause in the background** stops the game while its window is minimized; **Check for updates** looks for a new
  version on GitHub once a day at most and says so at the next start; **Button images** adds your own pictures for the
  touch controls on a touchscreen PC.
- **The F1 menu is the app's:** the same pages, sections and names, with the PC's own rows (window, VSync, keyboard and
  mouse, HDR, GPU, Discord) in the places MHGU for PC has them.

### Fixes

- **The gear moved when a menu opened** and sat on other buttons: in single-screen mode it went to the screen's corner
  (and with the touch screen's corner over its side, it moved with that side's buttons). It should now stay where it is,
  or where you put it in **Customize controls and HUD**.
- **Quick items: the piece turned into a small copy of the top screen** after a few seconds, until you touched it, and
  the touch screen's corner flashed the top screen as it opened. Both are gone.
- **The menu pad didn't come up in the delivery box's window**
  ([#73](https://github.com/khorne-hub/4u-native/issues/73)): at **Deliver**, with the quantity, the hunting controls
  stayed. It comes up there now, as at the supply box.
- **The Area map moved in Customize controls and HUD left the whole map behind**
  ([#72](https://github.com/khorne-hub/4u-native/issues/72), [#75](https://github.com/khorne-hub/4u-native/issues/75),
  [#76](https://github.com/khorne-hub/4u-native/issues/76)): when the main screen shows the hunting ground's whole map,
  it now goes with the part, so the hunter's arrow no longer ends up outside it.
- **Phones whose GPU driver needs a lot of memory for very large shaders** (an Exynos 2200 closed a few seconds after
  starting): the large backup shaders are no longer built ahead of time, never two at once, and not again right after a
  session that was cut while building one. That should stop those closes (not tried on that phone).
- **A saved shader list that grew for weeks** (on an Adreno 610 it broke textures and brought the "slower path" notice):
  the list keeps its 16,384 most used shaders, and the start stops building them ahead near the phone's limit of memory
  maps. It should be gone.
- **The phone build paused "for heat" with a cool phone** (battery at 35 °C): some phones report a high thermal state
  while cool; under 40 °C of battery that reading should no longer pause the build.
- **The first loads after a long time in the background** could take much longer than usual: coming back after a minute
  or more, the app now asks Android for the game's memory back at once, so they should be as quick as usual.
- **Control profiles: profile 1 came back as the default layout** after going to another profile without saving. Leaving
  a profile should never empty it now, and the editor should ask before **Load** or **New** drop changes you haven't
  saved.
- **Insect Glaive: firing the kinsect needed a slide from R to X and back:** a single slide from R onto X should fire it
  now.
- **Gyro aim with Bow Controls Type 2** stopped in the aiming mode: it should keep turning the aim while the bow draws
  and charges.
- **Emulator-style layout:** the **Buttons on the screen** rows should now show or hide its own buttons too.
- **The touch controls vanished mid-game:** when something the phone calls a controller hides them, tapping the game a
  few times should now say which device it is and how to show them anyway.
- **The game's pictures and a texture pack's pictures showed in the phone's gallery:** a `.nomedia` in the game folder
  keeps them out.
- **Performance:** the picture conversion the game asks for every few seconds is several times faster, and the renderer
  no longer keeps a log nobody read right after each load. On weak phones the first seconds after a load should be
  smoother.
- **A phone that joined a PC's hub left the Gathering Hall after a few minutes** (the PC hosting): the game's connection
  carries everything in order, so one packet lost on the way to a far hub server held the rest back for about a second,
  and the game took that for a player who had left. On a slow link (a round trip of 150 ms or more) each packet now goes
  twice, 40 ms apart, so a single loss no longer holds the game back, and the connection no longer narrows itself on
  such links (it sometimes waited a whole round trip to send). In our tests, with a hub server made as slow and lossy as
  a far one, the phone stayed in the PC's hall; between two phones it should help too. Such a link takes about twice the
  data (some 10 to 20 KB/s each way). Everyone in the hub needs 1.0.2 for the whole fix; older versions still play with
  you as before.
- **Phones wait for a late host, as the PC does:** in a Gathering Hall, when the host has sent nothing for half a second
  (a slow hub, or a PC host that stalls for a moment), the game waits for it instead of taking it for gone and leaving
  the hall.

### New, from your suggestions

- **HUD size** (gear, **Graphics & Audio**, **Screen and HUD**): the whole HUD smaller, 60 to 100 %, changing as you
  move the value, with the quests' talk and yes / no windows.
- **Each part of the HUD can be made smaller or larger** (- / + or a pinch) **and hidden** in **Customize controls and
  HUD**.
- **Map button:** a button at the left edge shows and hides the map during a hunt (**Controls**, **Buttons on the
  screen**, **Map button**). The help also says how to have the map on the main screen: take the Map off your panels in
  Customize Panels (quest menu).
- **Weapon tree at the Smithy:** while the Smithy shows a weapon (Forge or Upgrade), a new round button (or the right
  stick's click) opens that weapon's tree, which should look like the series' trees: each weapon is its class's icon,
  read from your own copy of the game and in its rarity's colour, with its name; the ones you have (box or equipped),
  the ones before them, and the next step from each as "?". Beside the tree, the selected weapon's attack, element or
  status, affinity, defense, slots, rarity and sharpness, and what forging or upgrading it takes (materials and zenny).
  Read-only: forging stays in the Smithy. Names in the game's language. It should show in every Smithy screen with a
  weapon: tell us if it misses one. **Controls**, **Buttons on the screen**, **Weapon tree button** takes the button
  away.
- **Supply Box without the wait:** in a quest each item goes to the pouch at once, and you can take the next one right
  away (it was half a second each). The game's rules stay (room in the pouch, each item's limit), and online the host
  still hands out each item, so playing with others should work the same. Thanks to Alfonso Matador for the idea.
- **Combined buttons on any controller button:** A, B, X, Y (by their place), L, R, ZL and ZR (pressed past half way),
  START, SELECT, the D-pad, the stick clicks, Guide, the paddles and the touchpad. **Press a controller button** takes
  even one the controller's map doesn't name, as some paddles on Android. That button should then press the combined
  button instead of its own.
- **Up to 15 combined buttons, each control profile with its own:** your current ones should stay in your existing
  profiles.
- **Export and import a control profile** as a .zip (**Controls**, **Control profiles**, a profile): it should carry its
  button pictures and combined buttons, and a picture whose name is taken should come in renamed.
- **Camera on the right half only** (**Controls**, **Touch controls**, off by default): dragging should turn the camera
  only on the right half of the screen.
- **Touch editor:** a tap on the stick's centre or on the menu pad's SELECT should pick it for a picture of its own.
- **Colour presets** (**Graphics & Audio**, **Colour**, **Preset 1** to **3**): keep a colour setting and go back to it
  with **Use**; negative values are kept too.
- **FSR 1** in the **Upscale filter** (**Graphics & Audio**, off by default): the top screen scaled up with sharper
  edges where it is shown larger than its picture, with its own **FSR 1 sharpness**.
- **Your own GPU driver on any GPU** (**Graphics & Audio**, **Advanced**, **GPU driver**), at your own risk: if a driver
  doesn't start, the app should go back to the system's by itself. The log and the error screen name the driver in use.
- **HD texture packs:** their memory budget now follows the memory the phone has free (with a safety margin and a 3 GB
  ceiling), and goes down when Android asks for memory. Packs can bring **.ktx2** textures (Basis Universal ETC1S or
  UASTC, or RGBA8 / BC7) beside or instead of their .png files; on phones they take the same memory as a PNG for now.
- **Damage numbers** should also show on small monsters, and a new **Damage number style** (**Graphics & Audio**,
  **Screen and HUD**) offers **Classic**, **Outlined**, **Compact** and **Burst** (a run of hits should add up in one
  number). MHGU's **By hit** colour (weak points, hard parts, critical hits, the element) needs a change in MH4U that
  would make everyone build their code library again, so it is left for a later version.
- **Text outline** (**Graphics & Audio**, **Screen and HUD**, off by default): it should draw a thin dark outline around
  the game's letters, for text that is hard to read.
- **Sound in screen recordings** (**Graphics & Audio**, **Picture and sound**, off): for phones whose screen recorder
  records no sound of the game. It should record it from the next start.
- **Multiplayer › Wait for a player who stalls** (off by default): when another player's game stalls for a moment (a
  slow phone, the app in the background), yours waits for it instead of running ahead, so the game should be less likely
  to drop that player. Only between players of this app who turn it on; the whole room may pause for a moment. Older
  versions and emulators play with you as before.
- **The chat in a window of its own:** the chat page (gear, **Multiplayer**, **Chat**, or the chat button) opens as a
  smaller see-through window over the game with the chat alone: its header has a tab for each chat there is now, the
  gear opens the chat's options and X closes it; under it the text to say and **Send**, and the lines, the newest first,
  each on a dark strip so it reads over grass or snow. The switches, **Friends** and the other options are on the chat's
  options page. Drag its top to move it and its bottom right corner to size it; it stays where you leave it. While your
  phone's keyboard is up, the window should go to the top of the screen and be as tall as the keyboard leaves, so the
  text, **Send** and the last lines stay in sight.
- **The chat window floats over the game:** played with your fingers, and on PC with the mouse, the game goes on under
  it and the touch controls work round it. Only the window's own controls take a finger or a click (its header and tabs,
  the text and **Send**, the corner's grip); a tap on its blank part moves the stick or turns the camera. While you
  type, a tap or a click away from it closes it, and the phone's keyboard with it. A menu of the game's opening hides it
  until that menu closes, the settings button (**F1** on PC) goes to the menu, and the chat button closes it. On a phone
  with a controller in hand it stays a menu its buttons work, as before.
- **Tabs in the chat window:** Room, General, Global and Friends, only the ones there are now, each with the lines you
  haven't read (up to 99); under **Friends**, arrows go from friend to friend.
- **How see-through the chat window is:** in the chat's options, **Window background**, **Message background** and
  **Typing field background**, from 0 to 100 % in steps of 5, with - and + or a bar; the text always stays solid.
- **The lines beside the chat button:** 8 seconds, up to four, each chat in its colour (the room light blue, General
  green, Global gold, a friend pink) and each player's name in a colour of its own, in the chat window too; not yours,
  and they go when a menu opens.
- **Credits:** Credits & Licenses and the README list who contributed to the port, by name.
- **The chat button in Customize controls and HUD:** moved and sized like any other button, and kept with the control
  profile (the chat's options: **Move the chat button**).
- **The chat button in Customize controls and HUD:** moved and sized like any other button, and kept with the control
  profile (the chat's options: **Move the chat button**).
- **The menu pad, on or off:** the pad that comes up in the game's menus can be turned off under gear, **Controls**,
  **Buttons on the screen**, **Menu pad** (on as it comes); its help now says what moves the cursor without it.
- **Devices with two screens** (AYN Thor, Retroid Pocket Duo): **Graphics & Audio**, **Screen and HUD**, **Layout**,
  **On the second screen** puts the touch screen on the second display, touched there; if that display goes away your
  previous layout comes back. Off by default, and it only shows when there is a second display. Not tried on such a
  device yet: it should work, tell us how it goes.
- **Side by side**, the touch screen should keep clear of a punch hole or notch at the screen's edge.
- **Settings:** a value too long for its row should now slide along in the row you pick or touch, so it can be read
  whole.
- **Fast scaling (more FPS)** (gear, **Graphics & Audio**, off by default): scales the game's picture in a simpler way
  when the 3D resolution is not the size your screen shows it at (for example 2x on a 720-line screen): weak phones get
  many more frames a second (a Moto g06 in Normal mode at 2x: town 47 → 56 FPS, the hall 45 → 57, the field 40 → 50).
  Some colours may change very slightly.
- **The settings menu is laid out as MHGU's:** the same pages, sections, order and names in both apps. In **Screen and
  HUD**: **Field of view**, **HUD at the screen's edges**, **HUD size**, **FPS counter**, **Text outline**, then the
  **Hunting aids** section (with **Monster health bars**). **PS1 filter** comes before **Shadows**; in **Touch
  controls**, **Long press hides the controls** comes after **Floating stick**; in **Advanced**, **Frame generation**
  comes after **Clear shader cache** and the **GPU driver** section goes last; **Licenses** is under **Show the
  credits**. Two rows have MHGU's names: **Add a mod** (the .zip) and **Add cheats**. Your settings stay as they are.

## 1.0.1.1

Your code library stays valid: there's no need to build it again.

1.0.1.1 is a small update after 1.0.1: fixes for what you reported, the PS1 filter you asked for, and quick items on the
main screen.

### Fixes

- **"Gather everything in one press" closed the game** ([#71](https://github.com/khorne-hub/4u-native/issues/71)) at
  gathering points with more than one use and when carving, with the European version of the game whose code had been
  built with 1.0: that code lacked the places in the game that 1.0.1 added, and the option called a wrong one. The app
  now fills them in by itself, with no rebuild, and if one were ever missing the option would do nothing instead of
  closing the game.
- **The monster's name on the health bars** didn't show on phones: it is now read from the game's files the way the
  game reads them.
- **Bow: letting go of the shot sends the arrow to the monster by itself**, as asked for 1.0, which only worked with
  Lock-on on and with one of the game's two Bow Controls. Now it goes to the monster you locked on to or chose with
  the Target Cam, else to the nearest large monster in view, with or without Lock-on and with either Bow Controls.
  Turn the camera, or drag on Fire, away from the monster while drawing and the arrow goes where you aim. Gear,
  **Controls**, **Bow aims at the monster** (on). If your game's Bow Controls draw with R, the touch Fire button now
  draws and shoots (before, it opened the aiming mode).
- **The phone build looked stuck at 10 %** ("Translate the game's code"): parts of the translation worked for minutes
  without showing any progress. Now the percentage moves the whole way, the screen's timer keeps running, it uses a
  little less memory, and if it goes quiet its log says what it is doing every minute. If yours stopped there, update
  and tap **Go on**: it continues from where it stopped.
### New, from your suggestions

- **PS1 filter**, off by default (gear, **Graphics & Audio**, **PS1 filter**): the look of the 3D games of the late
  90s. Big sharp texture blocks, the top screen's picture on the console's own 240 lines with stronger colours and
  15-bit colour with a fine dot pattern, wobbly polygons and bending textures, and faint flickering scanlines. It
  changes no other setting, and the touch screen, the touch controls and our menus stay sharp. Thanks to Ysacaiah for
  the idea.
- **Low-end mode › Brick**, a new level below Toaster, for the weakest phones: everything Toaster does, and the 3D
  world's textures at a sixteenth of their size, drawn as big sharp blocks, so the phone reads less memory each frame.
  Brick is the one for frame rate, the PS1 filter is the one for looks, and you can use both together.
- **Show quick items**, off by default (gear, **Graphics & Audio**, **Screen and HUD**): in a quest, the touch
  screen's quick items panel also shows on the main screen, small, and a tap on an item there uses it. It hides while
  the whole touch screen shows, with a game menu open, and if your panel layout has no quick items panel. Move and
  size it in **Customize controls and HUD**. It turns on and off at once, also in the middle of a quest, and doesn't
  cost frame rate.

## 1.0.1

Your code library stays valid: there's no need to build it again.

1.0.1 fixes problems you reported after 1.0 and adds some of your suggestions. Its first fixes were also tried on our
phones (Mali-G57, Mali-G52 and Adreno 830); the rest only on a PC so far: where a line says "should", please tell us how it
goes on yours (the Discord or an issue, with the log).

- **PowerVR GE8300, GE8320 and other PowerVR Rogue GPUs:** 1.0 closed at every start after the first one: it built the
  shaders you had already seen in the background, and this GPU's driver crashes when shaders are built in two places at
  once. These GPUs go back to how 0.1.9.2 built them: one at a time, when they first show up, with nothing in the
  background. That should end the closes; the short stutter the first time an effect shows up is back to 0.1.9.2's.
  PowerVR BXM GPUs keep 1.0's way (Discord).
- **PowerVR BXM GPUs closing after about ten minutes** ([#31](https://github.com/khorne-hub/4u-native/issues/31),
  Discord): on these GPUs the top screen reaches the display through an upright copy, and drawing it began with a clear
  that never showed. The PowerVR B-Series driver ran its shader compiler for that clear on every frame and kept some
  memory each time, until Android let the app map no more (the log's map entries, about 7,000 more a minute). The copy
  no longer clears, and the picture is the same (checked on a PC, pixel by pixel). We have no PowerVR phone: please tell
  us whether the closes are gone, with the log.
- **The app closing during the phone build** (around 10 %, at "Translate the game's code"): the settings menu, open
  behind the builder, crashed while the game was in the background, and took the build with it. The menus are no longer
  drawn while the game's window is hidden, which should stop it (Discord).
- **The phone build paused as "too hot" on a cool phone:** some phones report that they are hot whatever their real
  temperature. The builder now also reads the battery's temperature (and the thermal headroom on Android 11 and newer):
  with a cool battery it goes on, more slowly, instead of pausing, and a battery at 47 °C or more still pauses it. A phone
  that is really hot pauses as before. **Share log** now includes the builder's last run (Discord).
- **"Make the library again on the PC (step 3 of the guide)":** the error screen and the **Mods** page now point to gear,
  **System**, **Game code**, **Build the game code** (the PC tool is still the other way), and the error screen has a
  **Build the game code** button: once the library is made, **Play** starts the game (Discord).
- **Whole HD texture packs** ([#56](https://github.com/khorne-hub/4u-native/issues/56)): 1.0 opened every file of the pack at the start (a long black screen) and
  read each texture on the drawing thread the first time it showed (the game nearly stopped). Now only the pack's list
  is read at the start, and two threads of their own read the textures while the game keeps running: a texture shows at
  normal quality for a moment, then turns HD. Tried on a PC with a test pack of 380 textures; a real pack should behave the
  same. HD textures still take the phone's memory.
- **Coloured bars and blocks with the bloom on Adreno 6xx GPUs with the Vulkan 1.1 driver (1.1.128)**
  ([#32](https://github.com/khorne-hub/4u-native/issues/32), Discord): the likely cause is the bloom's small passes, which started by clearing part of the picture;
  these older drivers seem to get that part's edges wrong. On Adreno GPUs with a Vulkan 1.1 driver they now load the
  picture instead (the same picture, a little more memory traffic). This should remove the bars. The bloom stays on as
  you have it. We have no such phone: please tell us whether they're gone.
- **The notch on Android 9 and 10** ([#60](https://github.com/khorne-hub/4u-native/issues/60)): the game now draws under a notch or punch hole there too, as it
  already did from Android 11, so it should fill the screen.
- **Bow with the lock-on** ([#58](https://github.com/khorne-hub/4u-native/issues/58)): with the lock-on on, a drawn bow turned the hunter to the monster on every
  frame, so the hunter could only walk straight at it. Now the hunter turns to the monster when you press **X** and when
  you shoot; while you walk with the bow drawn, the stick decides the direction. Bowguns work the same way. Not tried in
  a hunt yet.
- **The lock-on camera and flying monsters** ([#62](https://github.com/khorne-hub/4u-native/issues/62)): the camera followed a flying monster's height all the way
  up; it now tilts up only so far (about 17°, eased), so your hunter and the ground stay in view. Not tried in a hunt yet.
- **Public rooms:** when the default lobby gives no list, the note now says that players run it and it is down at times,
  or that this network blocks it, instead of asking you to check an address you never typed. Rooms by address still work
  (Discord).
- **The game behind the settings menu turned black** a few seconds after opening the menu (since 1.0, on every page and in
  the layout editor): the app let go of the game's last picture while the game waited behind the menu. It's kept now, so
  the game stays visible there. On the **Colour** page the game around the panel isn't dimmed and the panel is
  translucent, so you see each change as you make it. Tried on a Mali-G52 phone.
- **Single-screen mode on wide screens, and its plates** ([#57](https://github.com/khorne-hub/4u-native/issues/57), Discord):
  on a screen wider than 16:9, the menus our single-screen mode draws on the top screen could show a list parked at the
  side (the quest counter's). Whenever the picture is wider than the console's screen, they're now cut to its width, as
  the console cut them; on a 4:3 screen nothing changes. The touch screen's plates and the yes / no questions now go where they cover nothing: in the item and
  equipment boxes, **Confirm** and **Back** sit left of the grid, and a question (like "Sell for 500z?") goes to the bottom
  left with Yes / No above it; at the quest counter, "Select this Quest?" goes under the list with Yes / No side by side
  inside its box, clear of the quest's card; in the Guild Store, the purchase question goes to the bottom left. The canteen's page
  counter no longer covers the description, and the open-air kitchen's Cost panel is drawn in front of its paper. The
  World Map, the town map and the hunt's map stay on the touch screen, as before: they work by touch. In a full menu,
  some of its parts (the quest's card at the counter, for one) could be drawn unmoved, at the game's own size, under
  the plates: our mode had run out of room for its moves. It now asks the game for more room. Tried on a PC in English
  and Spanish at 4:3, 16:9 and 20:9, and on phones at 4:3, 20:9 and 20.5:9 (the quest counter, the item and equipment
  boxes, the canteen); the rest should look the same on a phone.
- **The area map on an expedition** ([#65](https://github.com/khorne-hub/4u-native/issues/65)): moved in the layout
  editor, the map left your arrow, the monsters' icons and some areas' ivy behind. They now move together. Tried on a
  PC.
- **The renderer uses less CPU:** the part of the drawing that runs on the processor does the same work in less time
  (the vertex programs the code library doesn't carry, the vertex cache, the pipeline lookups, and no empty submission
  for a hidden touch screen). On a Mali-G52 phone in the field the renderer's thread used about 3 % less CPU, and once
  the phone was warm and had slowed its processor, it held 59.8 FPS where it had dropped to 55.5. The picture is the
  same (145 PC screenshots, not one pixel different).
- **Charge Blade from a sprint** (Discord): with the touch controls, X + A while sprinting (a tap on the middle of the
  attack pill) did nothing with a sheathed Charge Blade since 0.1.9: the controls let go of the sprint's R, and the game
  draws the Charge Blade straight into its axe only with R + X + A. A sprint that has gone on for a moment now keeps R
  through X + A, as a controller's R held does, for every weapon (the Great Sword guards from its sheath, as with a
  controller); a sprint that has only just started still lets R go, so the Great Sword doesn't guard by accident. Tried on
  a PC; not yet in a hunt on a phone.
- **Insect Glaive: R, then sliding onto A or X, only did R** (Discord): in the emulator-style layout the sliding thumb
  wasn't followed, and in the port's own layout R was let go when the thumb reached the attacks. Sliding between buttons
  now keeps both pressed (below).
- **More in the log:** when the GPU stops answering ("device lost"), the log now says so in plain words
  ([#61](https://github.com/khorne-hub/4u-native/issues/61)).

**New in 1.0.1, from your suggestions**

- **Your picture is the whole control** (Discord): with your PNG on a control, only your PNG is drawn, without the port's
  disc, ring, letter or glow; pressed, the picture gets a little smaller and darker. Every control the editor places takes
  a picture now: the stick (base and knob apart: **For: base / knob**), both D-pads, the sprint lock, the item bar's
  arrows, the attack pill, the corner band, the menu pad, the settings, touch screen, town, lock-on and camera buttons,
  the combined buttons and every emulator-style button. Without a picture nothing changes. Before, pictures past 48
  didn't show; now up to 64 are kept, and the ones not on the screen are let go.
- **Hide the controls with a long press** (Discord): hold one finger still on an empty spot (no button there) for about
  three quarters of a second and every touch control hides, the gear too, for cutscenes or clean screenshots; do it again
  anywhere to bring them back. A camera drag, a hold on a button or a second finger never hides them, and a drag still
  turns the camera while they're hidden. On by default: gear, **Controls**, **Touch controls**, **Long-press hides the
  controls**.
- **Slide between buttons** (Discord): a thumb that slides from one button onto another keeps both pressed, as two fingers
  would: R onto A is R + A, R onto X is R + X, R onto B is R + B, and back onto R is R alone. On by default (gear,
  **Controls**, **Buttons on the screen**, **Slide between buttons: both pressed**); off, slides work as in 1.0.
- **The item bar's touch area** (Discord): in the editor, tap the item bar and use - / + (or pinch) to make the area where
  a tap uses the item and a swipe turns the bar bigger or smaller. The bar itself is the game's and keeps its size.
- **Camera smoothing** (gear, **Controls**, off by default): a drag's turn of the camera is eased in, a little behind your
  finger; the whole turn still comes.
- **Frame rate limit down to 15** (Discord): 15, 20 and 25 join the list. 15 and 20 are paced as evenly as 30 on a 60 Hz
  screen; 25 is less even. The game keeps its speed; it only draws fewer frames. The log's line each minute says the
  limit and how the frames were paced.
- **Start over in the phone builder** (Discord): after a build was cut, cancelled or failed, **Start over** deletes what
  it left (its work files and the record it goes on from), so the next **Build** starts from the beginning. It asks
  first, and keeps the downloaded tools unless you choose to delete them too. **Delete the tools** now leaves the screen
  clean, and the builder's log notes the phone's heat when a build starts and at each stage. Not tried on a phone yet.
- **Colour** (Discord): gear, **Graphics & Audio**, **Colour**, a filter of our own: hue, temperature, tint, brightness,
  contrast, saturation and gamma, all at 0 (no change) by default, and **Reset colour**. Each change shows at once while
  the menu is open; **Save** keeps it. It changes the game's picture on both screens, its HUD and menus too, never the
  touch controls or this menu. At 0 the picture is drawn exactly as before, and the filter adds no pass of its own (on a
  Mali-G57 phone the top screen's draw went from 0.255 to 0.267 ms).
- **Your own DLC** (Discord): **Import DLC and event quests** also brings the DLC you installed in Azahar or Citra, or
  your own decrypted DLC `.cia`. The app decrypts nothing. The game used installed DLC only in a save that had received
  it online, from a service that no longer exists: now the app gives every save that mark (**DLC in every save**, on by
  default, on the DLC and event quests page; a copy of your save goes to `save_backups` first). Tried with test files
  only: tell us how it goes with yours. The steps are in the README, **Import DLC and event quests**.
- **Gather everything in one press**, off by default (gear, **Gameplay**, **Gathering**): one gather at a gathering
  point (herbs, mushrooms, honey, bugs, bones, ore...) or one carve gives everything it had left at once, with the
  game's own rules: gathering and carving skills, pickaxes and bug nets that can break. It stops if the pouch fills up
  (the game's swap window shows and the rest stays in the point) or if you can't carry more of an item.
- **Two hunting aids**, included and off by default (gear, **Graphics & Audio**, **Screen and HUD**): **Monsters always
  on the map** shows the large monsters on both maps from the start of a quest, as if painted (in quests you hunt
  alone), and **Damage numbers** shows each hit's damage as a number that rises over the monster, small, medium or large
  and in the colour you pick. Both work from the game's memory: no new game code to build.
- **Monster health bar**, off by default (gear, **Graphics & Audio**, **Screen and HUD**): in a quest, a thin bar in the
  look of the app's HUD at the top right of the top screen shows each large monster's name and health with its numbers
  (fainter while it is in another area). Move and resize it in **Customize controls and HUD** (drag it; its corner
  handle, a pinch or - / +; Reset puts it back). It only reads the game, so it works in rooms too.
- **Send all to the box, the Palico's items too** (Discord, Dark_Light's idea): with **Send all to the box** on (gear,
  **Gameplay**), **Send to Item Box** at the end of a quest also sends your Palico's items and your Palicoes' at once;
  what doesn't fit stays in the list, as always. Tried on a PC.
- **HD texture packs use less memory** ([#56](https://github.com/khorne-hub/4u-native/issues/56)): the pack gets a share of the memory the app gives
  textures, by your phone's RAM; textures of areas you've left are let go after a while; and a texture that doesn't fit is
  no longer decoded over and over (on a PC with a 4 GB phone's budget, a whole test pack went from 3,903 decodes to 497).
  On a 4 GB phone with a whole 4x pack, some textures may stay the game's own rather than stutter. New: gear, **Mods &
  Cheats**, **Texture packs**, **Pack texture size: Half** (off by default) draws the pack's big textures at half their
  width and height, a quarter of the memory; it applies at the next start. Measured on a PC.
- **Frame generation** ([#59](https://github.com/khorne-hub/4u-native/issues/59); optional, off by default): gear,
  **Graphics & Audio**, **Advanced**, **Frame generation**. With the FPS limit at half your screen's refresh rate or less
  (30 on a 60 Hz screen), the app makes a picture between each two of the game's top-screen pictures, so you see 60. It's
  our own, for any GPU; the HUD is never warped, and it stays off in the game's menus, on the title, in this menu and in
  the layout editor. It adds a little input delay (about one refresh of the screen), fast camera turns can leave smears
  at the edges of the hunter and of what moves, and it costs GPU time: if the game falls behind its limit with it on, it
  pauses itself (after the third time, it stays off until the next start). Tried on a Mali-G57 handheld (the game at 30,
  60 shown, about 4 ms of GPU per made picture at 1200x720); not measured yet on Mali-G52-class phones, where it may well
  pause itself. The FPS counter shows "30 FPS (60 shown)" when it works. With it on, the app asks the screen for twice
  the FPS limit where the screen has such a rate, so on a 120 Hz screen it also works at a 60 FPS limit; the log says
  why no pictures are made and when the screen changes its refresh rate.
- **Richer logs and Share log**, for the problems we can't reproduce: the log now says much more about the phone (Android
  version, processor, memory, storage), the GPU and its driver, your options and each change, where you are in the game,
  the app going to the background, heat and memory warnings, and each minute's frame times. The log of the session
  before, which came out empty until now, is kept, two sessions back. **Share log** (gear, **System**, **Your game**,
  **Share log**) sends the last three sessions, your options (names, keys and passwords hidden), your mods, the app
  folder's file names and sizes, the builder's log and Android's warnings about the app, zipped when it's big. It never
  includes your accounts, your Wi-Fi's name, your location or your save.
- **The same save on your PC and your phone:** the README now explains near its top how to carry your save with Google
  Drive: between phones by itself, and to an emulator on a PC by hand.
- **The credits card at the start closes when you touch the screen** (Discord): it still shows at every start, but one
  tap fades it out.

- **Hubs: online with no ports to open** (asked for since MHGU got them): gear, **Multiplayer**, **Create a hub** and
  **Join a hub**. A hub server lists the hubs and carries their players, so nobody types an address or forwards a port on
  a router. The default server is the community one MHGU's players run (not this project): the same program serves both
  games and keeps
  each game's and region's hubs apart (USA with USA, Europe with Europe; Monster Hunter 4G has none). **Hub server**
  picks another, **Check hub server** says whether it answers, and the README says how to run your own. A hub works as a
  room: its hunters, its chat (the **Room** chat), a password if you like, and the game's **Local Play** in the
  Gathering Hall. **Play online with** chooses this app's hubs, Azahar and Citra rooms (as in 1.0) or both (as it comes;
  with both, a hub you create also takes Azahar and Citra players at your phone's address). Tried on our two phones and
  a PC on one Wi-Fi with a test server (two hunters in one Gathering Hall: phone with phone, and phone with PC both
  ways), and on a PC with the real server program; not yet with players far apart over the internet: tell us how it
  goes (the log has a line a minute about each hub link).

Thanks to everyone who sent logs, screenshots, videos and ideas. The phone builder is Klark231's idea.

## 1.0

Your code library stays valid: there's no need to build it again.

1.0 was tried on a PC and on our phones (Mali-G57, Mali-G52 and Adreno 830), but not every part of it on a phone: where a
line says "should", please tell us how it goes on yours (the Discord or an issue, with the log).

**New in 1.0**

- **A new settings menu:** six sections down the left, **Controls**, **Graphics & Audio**, **Gameplay**, **Multiplayer**,
  **Mods & Cheats** and **System**, with **Credits & Licenses** under them. Each section's page has its main options, and
  the rest is one level down (**Touch controls**, **Controller**, **Screen and HUD**, **Advanced**, **Join a room**...).
  Each row's explanation is behind its **?**. Your options keep their values; only their places changed: **Display** is
  now **Graphics & Audio**, **Screen and HUD**; the **Comforts** are in **Gameplay**; **Menu Options** is **System**
  (with **Your game** for the folders, **Import save**, **Export save** and **Share log**).
- **Build the game's code on the phone, no PC needed:** in the game folder screen, **Build on this phone** when the library
  is missing or from another version, and later gear, **System**, **Game code**, **Build the game code**. It builds in the
  code mod of your `mods` folder (or another, or none), downloads its tools the first time (about 100 MB: Python from
  python.org and Zig from PyPI; never anything of the game), and goes on in the background, slower when the phone is warm
  and paused when it is hot or the battery is low; a cut build goes on from where it stopped: **Go on** goes straight back
  to compiling the files still missing, without analysing and translating the game's code again (within a second on an
  RG406V; on a hot Galaxy S25 Ultra, 71 times within 0-2 s, where translating it all takes about 70 s). A broken entry
  in the compiler's cache is now emptied and compiled again instead of stopping the build (seen and fixed on the Galaxy).
  A library from a build that was cut can differ byte for byte from one made in one go, in one very large function (a
  limit of the compiler), and it runs the same. Inside the app a whole build took 32 minutes on an RG406V (Mali-G57; again
  with this release's own APK) and
  about 92 minutes on a Galaxy S25 Ultra that paused 72 times for heat; both made the same library, and the game runs Val
  Habar at 60 FPS with it. It should take about 2 hours on a low-end phone. Klark231's idea.
- **Single-screen mode of our own:** the touch screen's menus drawn on the top screen by the game itself, in every
  language, Spanish included: the title menu, quest counters and lists, the canteen, shops, Palico menus, help pages and
  yes / no questions. On by default, also when you update (gear, **Graphics & Audio**, **Screen and HUD**); with the USA
  game the touch screen starts hidden and its button brings it back for the town's map, the World Map, the keyboard and
  character creation. It stays off while a code mod is on (jM5557's mod, Heavenfall). Based on Heavenfall's single-screen
  mode, by popzol and BlitzWOLF, without its gameplay changes. Tried in English and Spanish on an RG406V and a Galaxy S25
  Ultra (title, saves, quest counter and lists, START menu, pouch, item box); the canteen and the smithy weren't reached
  on a phone yet.
- **Control profiles:** 10 slots for your touch layouts, each with everything: buttons, HUD, the FPS counter's place, held
  buttons and pictures. Each one is the port's own layout or an **emulator-style** one (A, B, X and Y in a diamond, the
  D-pad, the shoulder buttons, START and SELECT, each pressing only its own button). In the editor, **Profile**, or gear,
  **Controls**, **Control profiles**. Your layout from before is profile 1, unchanged. Designed by tabarong.
- **Buttons that stay pressed** (the editor's **Hold**), **your own pictures on the buttons** (the editor's **Picture**, or
  **Controls**, **Touch controls**, **Button images**), the **FPS counter** dragged anywhere in the editor, and a
  **floating stick**, off by default (**Controls**, **Touch controls**). Tried in the MHGU port on a phone; here they should
  work the same: tell us if something is off.
- **Chat and friends:** gear, **Multiplayer**, **Chat** and **Friends**. The **Room** chat is the room's own (Azahar's
  players read it too); **General** (MH4U) and **Global** (both ports) and private messages with friends go through public
  Nostr relays, only after you turn the online chat on, with no account and no server of ours: you are a friend code made
  on your phone. Mute and block are yours alone, and there is a limit against spam. Both chats start off: until you turn
  on **Room chat** or **Online chat**, at the top of **Chat**, there's no chat button and nothing of the chat over the game,
  and the room's own messages show as before (**Room messages over the game**). With a chat on, a chat button over the game
  shows new lines. Tested between a phone and a PC; between two phones it should work the same. tabarong's design.
- **Room passwords:** under **Join a room**, **Password**, **Generate a password** makes one, and **Share this room in the
  chat** sends the room to a friend, who joins it with one tap. Joining a room with the room chat on, a notice reminds you
  to go to the Gathering Hall's **Local Play**.
- **Crossplay with Azahar and Citra,** said plainly: the rooms are the same, everyone needs the 1.3.0 update, and a hunter
  in the room playing another game is marked (they can't reach your Gathering Hall). See **Multiplayer**, **Play with
  emulators**.
- **Effects:** gear, **Graphics & Audio**, **Effects** off leaves out the game's particle effects (hits, sparks, fire,
  breath, dust, rain), at once: faster where there are many, but the effects that warn of an attack go too.
- **HD texture packs:** the packs made for Azahar and Citra load from a `textures` folder in your game folder (PNG
  textures). Each texture takes the phone's memory: a big pack can close the game on a phone with little RAM. Tested with a
  test pack on the PC; a real pack should load the same.
- **Clear shader cache** (gear, **Graphics & Audio**, **Advanced**): for a picture that shows something wrong after a GPU
  driver update, the next start compiles them again, and a small plate says how far it is.
- **PowerVR:** when the app has to switch to its slower, safer way of showing the screens, a notice says why the frame rate
  dropped.
- **3D resolution:** its help says why there is no 55% or exact size (the picture can only be a whole number of times the
  console's screen) and that the HUD is as sharp as this resolution.
- **A keyboard and a mouse on the phone:** while both are connected, **Controls**, **Keyboard and mouse**, **Keybinds**
  changes every key, and a left click on the game's picture captures the mouse to turn the camera (Escape lets it go); a
  click on the touch screen or on a touch control is still a tap there. Tried on an RG406V with a virtual keyboard and
  mouse; a real mouse in a quest should work the same.
- **Bows and bowguns with the lock-on:** with the lock-on on, a bow's draw and a bowgun's shot turn the hunter to the
  monster followed, as in the MHGU port. It should help; not yet tried in a hunt with a monster.
- **No more double tap on the camera:** an accidental one in a fight turned the camera away. The camera button (L) still
  centres it.
- **Menus:** two options in **Buttons on the screen**: **L and R in menus** (the menu pad's tabs, on) and **D-pad in menus**
  (off: the menu pad takes its place, as before).
- **Controllers:** a controller that sends A, B, X and Y without Android listing them is added again at its first press,
  so the game reads it, as in the MHGU port; and the log lists every input device.
- **Send all to the box:** at the end of a quest, the game's **Send to Item Box** sends every reward at once; what doesn't
  fit stays in the list. Off by default: gear, **Gameplay**, **Send all to the box**. Tried in a low-rank quest on an
  RG406V. Alfonso's idea.
- **Import DLC and event quests** (gear, **Mods & Cheats**, **DLC and event quests**): bring the event quests you already
  have, from an emulator or a backup of your console. The app includes none: you bring your own, at your own risk.
- **Home screen shortcut** (gear, **System**): the game on your home screen with a picture and a name you choose.
- **Verify game files** (gear, **System**, **Your game**): reads the game and its update whole and says plainly what is
  missing, cut short, changed or from another version or region (USA 1.3.0, Europe 1.1.0, the base game alone, MH4G),
  whether the copy looks modified (a repack), whether the code library and its `.id` fit this version and come from the
  same build, which code mods are on or clash, whether the save has its extra data, and what to do. It runs in the
  background with its progress and **Cancel**, and the full list goes to the log for whoever helps you. It changes and
  downloads nothing: only checksums ship with the app.
- **Mods:** code mods that don't go together are named on the **Mods** page; the error screen says plainly when a library
  comes from another tool, from two different builds (`.so` and `.id`) or from another build of a mod; a code mod's own
  `exheader.bin` is used. Files and folders spelled with other capitals (`Data` for `data`) now replace the game's, and
  **Add a mod** finds a mod deeper inside a zip (also Luma's `luma/titles` layout). The game folder's `mods` is searched
  as deep too: a mod found there only this way is added off, and the **Mods** page says so; turn it on there.
- **Heavenfall** should now build as an optional mod you get yourself, from its 1.3.0 update route and from its web
  installer's 1.0 route. It was only tried with a stand-in we made, never with the real mod: tell us. Its layered armour
  comes with it. Export your save before you try it.
- **Cheats as the lists write them:** tabs, codes written together, comments after the codes, CTRPF notes and files with a
  UTF-8 mark load now. A cheat that writes into the game's code can be made a code mod (**Cheats**, select it, **Make a code
  mod**), built into your library.
- **The European game (1.1.0)** should work too, with its own mods and cheats (we have no European copy to try it).
  **MH4G** is recognized but not supported yet.

Thanks to tabarong for the designs of the control profiles, the chat and the room passwords, to Klark231 for the idea of
building the game's code on the phone, to popzol and BlitzWOLF for Heavenfall's single-screen mode, the base of ours,
and to Alfonso for sending all the rewards at once.

**Fixes and more**

- **Ammo with L held:** a step of the ammo or the coating with **L** held should keep the ammo you chose again (it went back
  to the one before; seen on a Moto g06 with a light bowgun, Discord).
- **Lock-on in another area:** it stays on with no monster to follow (another area, a cart to camp) and follows again once
  one is back, as in the MHGU port; the button, a controller's right stick click and the quest's end still turn it off
  (seen on a Moto g06).
- **A Lock-on button of its own:** in a quest, a tap on the new **Lock-on** button (left of the camera button, L) keeps
  the camera turning to the monster until you tap it again, as in the MHGU port. It presses none of the game's buttons,
  so holding **L** works as the game's **L** again, for example **L** + **X** / **B** to change a bowgun's ammo (Discord).
  Move it in the layout editor, or hide it: gear, **Controls**, **Buttons on the screen**, **Lock-on button**. A
  controller still locks on with the right stick's click.
- **Keyboard:** played by touch, the menu's keyboard opens with your phone's own keyboard already up, as in the MHGU port.
- **Samsung DeX** ([#40](https://github.com/khorne-hub/4u-native/issues/40)): maximizing the game's window restarted the
  game over and over. The window can now change size without a restart, which should fix it (we have no DeX device to
  try it on: please tell us).
- **Adreno 610 stuck at 0 FPS** ([#47](https://github.com/khorne-hub/4u-native/issues/47)): Adreno GPUs with older
  Vulkan 1.1 drivers show the picture the way 0.1.9 did again (0.1.9.1 had changed it for them). And if the phone's GPU
  driver stops answering, the game no longer freezes: it closes with the error screen, where you can share the log.
- **Coloured streaks on an Adreno 619** (Moto G34, Discord): the 3x minimum of the 3D resolution is back for that GPU
  with its system driver, which should remove them. **Adreno fix** turns it off.
- **PowerVR** ([#48](https://github.com/khorne-hub/4u-native/issues/48), Discord): the shaders you've already seen are
  built again in the background at the start, one at a time, as in the MHGU port, so the short stops at each new effect
  should get much rarer after your first session. A PowerVR BXM that closed at every start should start now. Older
  PowerVR GPUs that showed a black screen with sound now draw the 3D at the most their GPU can: the 3D resolution row
  says so, for example "100 % (5x) · 4x".
- **Long sessions on a PowerVR BXM** ([#31](https://github.com/khorne-hub/4u-native/issues/31)): `log.txt` now names
  what keeps growing until the game closes, and close to the system's limit the app switches to a slower but safer way of
  showing the screens. This may help; a log of a long session will tell us more.
- **HUD at the screen's edges:** the weapon gauges under the sharpness (a Charge Blade's phials, the other weapons'
  gauges), the Frenzy and a bowgun's ammo now move with the rest of the HUD (Discord).
- **More in the log:** after a crash inside the GPU driver, the thread's name and the system's own error lines (Discord);
  the performance line each minute now has the phone's temperature status and CPU speed, to tell whether a slowdown
  comes from the phone's heat ([#50](https://github.com/khorne-hub/4u-native/issues/50), Discord); and a picture shown
  for one wrong frame leaves lines in the log (Discord).
- **The library builder on PC:** its window no longer closes at once. It says when the download wasn't unzipped first,
  and always waits so you can read its last lines (Discord).
- **Pickers' titles:** the title of a window to choose from fits whole on a 4:3 screen, its diamond and **?** included:
  drawn smaller when it has to, instead of cut.
- **The gear and the game's menus:** with single-screen mode (or a code mod's) on a 4:3 screen, the settings gear waits in
  the bottom-left corner while a menu of the game's is on the top screen, instead of covering it; a gear you moved stays
  where you put it.
- **The touch buttons and the game's menus:** with single-screen mode (or a code mod's), while a menu of the game's is on
  the top screen (the item box, the pouch, a shop), a touch button with half its picture or more over it is drawn faint,
  so the whole menu reads; on a 20:9 phone the right-hand buttons covered the item box's last column. They still work as
  always, and nothing moves.
- **Import save from a PC** (Discord): a save from Azahar, Citra, Checkpoint or JKSM comes in from whatever folder you
  pick: the emulator's whole folder, its `sdmc`, the save's `data` or title folder, or the backup tool's folder. A
  European save went where the USA game never looks, so your hunter didn't show: a save from the other region (or from
  the Japanese MH4G) is now refused with a message that names both games, and so are a hunter's file cut short and one
  without its `system` file when you have no save yet. The guild cards, event quests and Palico next to an emulator's
  save come along too (they never did). After the restart a notice says what came in, and **Share log** includes the
  last import. We couldn't make the black screen after an import happen here: if you still get it, please send the log.
- **README:** the setup in short, the tool from the **Source code (zip)** of the same release as your APK, how to switch
  from a copy from another website without losing your save, more rows in *If something goes wrong*, the Lock-on
  button, and for 1.0: building the game's code on the phone, single-screen mode, play online step by step, chat and
  friends, crossplay, the event quests' import, keeping your save in the cloud with a sync app, HD packs and the new
  menu's places.

## 0.1.9.2

Your code library stays valid: there's no need to build it again.

- **Combined buttons on a controller:** each combined button can go on a controller's button (L3, R3, SELECT, Guide,
  Misc, the paddles or the touchpad): gear, **Controls**, **Controller**, **Combined buttons**, a row that shows while a
  controller is connected. A player's suggestion on Discord.
- **More in the log for slow phones** ([#43](https://github.com/khorne-hub/4u-native/issues/43)): `log.txt` now tells how
  far the game's list of shaders has got while it is being built, and how long it took, and has a performance line once a
  minute.
- **The single-screen mod** is made for the USA game in English: with the game in another language its menus overlap
  ([#30](https://github.com/khorne-hub/4u-native/issues/30)). The README says so now.

## 0.1.9.1

Your code library stays valid: there's no need to build it again.

- **Fixed the flicker with the touch screen hidden** (bright, dark or broken frames every few seconds that stopped when
  the touch screen showed) on GPUs with older graphics drivers, such as Mali-G71 and G72, and on PowerVR: the top screen
  now always reaches the display the same way there. Mali and Adreno GPUs with recent drivers are unchanged.

## 0.1.9

Your code library stays valid: there's no need to build it again.

- **Run with the stick:** push it past its edge to run, and lift your thumb on the padlock circle above it to keep running;
  touching the stick or the circle again stops. The circle moves and resizes in the layout editor. Turned off, a **Run**
  button runs while you hold it.
- **Bows and bowguns:** a tap on aim keeps the aim up until the next tap; hold and slide to aim. **Aim with the gyro**
  (off by default), with its sensitivity, turns the aim as you turn the phone.
- **Menu pad:** in the game's menus a small pad comes in under your right thumb, with L and R above it, in place of the small
  menu D-pad. Quest rewards and the Reward Box count as menus, and the hunt D-pad steps aside while the pad shows.
- **Lock-on:** the padlock by the target icon, which didn't respond, is gone; a gold ring round the icon shows the lock-on.
- **Black screen with the menu's music on some older Mali GPUs:** when the phone's graphics refuse the usual way of showing
  the picture, the app now switches to another by itself, and `log.txt` tells more about the phone's graphics.
- **Picture:** cutscenes are sharp again; textures the game rewrites are caught (no more outfits with the old pattern in
  character creation, or wrong ground and sky); long sessions use less memory for textures, so they keep loading on 3-4 GB
  phones; a steady 30 fps on phones that switch the screen to 30 Hz.
- **Other GPUs:** older Vulkan 1.1 drivers get shaders they accept; on Adreno the 3x minimum is only for drivers older than
  1.1.128, and **Adreno fix** turns it off; Adreno 610-class phones start with the light settings; PowerVR and other GPUs
  no longer draw over a picture still on the screen.
- **Save folder** (Menu Options): keep your save in a folder a sync app shares with your other devices. MHGU for Android
  can share the same folder.
- **Import save** has **Choose a folder** and finds emulators' folders by itself. An import never deletes your old save
  before the new one is in place.
- **Faster eating and drinking:** two cheats, off, as in the MHGU port. **Comforts** in Cheats, after jM5557's QOL mods:
  hold A to gather, move to skip the flex, B cancels sharpening, no time limit on rewards.
- **Playing together:** a community lobby comes set, the lobbies you type are remembered (the × removes one), and the first
  time online a notice names the server.
- **Playing alone,** the game waits while the settings or the layout editor are open.
- **GitHub and Discord buttons** beside Save open the project's page and the community's Discord (they ask first).
- **Notices:** at most once a day the app checks for a new version (**Check for updates** turns it off), and a copy that
  isn't the official one says so.
- Also: dashing and tapping the camera in a village no longer opens the single-screen mod's World Map (L and R together
  still do); a thumb landing on the stick takes the stick; the error screen's way back to the last start's mods works with
  one tap, also for a code mod the app can't load; the layout editor's panel moves and gets smaller; a 240 fps choice.

## 0.1.8

- **Fixed a strip of wrong colours along the top edge** at low 3D resolutions (Low-end mode, 50 %): the game never draws the
  top screen's first row, which showed as a whole line at 1x.

## 0.1.7

- **Fixed a black screen with "No game folder at /game"** on phones where Android gives the app no external folder (for
  example a Redmi Note 8): the game now reads its setup from the same folder the folder screen writes it to.
- **Adreno GPUs with a Vulkan 1.1 driver** (for example the Galaxy S9's Adreno 630): their driver misdraws the 3D picture
  below 720 lines (coloured bands), so there the 3D resolution is 3x at least.

## 0.1.6

- **PowerVR GPUs:** 0.1.4 played, but the next start stopped again: the GPU's shader compiler also crashed when the app built
  its saved shaders on a second thread. Now every shader is built on one thread when first needed.

## 0.1.5

- **Fixed the combined buttons page** (gear, Controls, buttons on the screen, Button 1-6): it showed nothing.

## 0.1.4

- **PowerVR GPUs** (for example the GE8300 of the Helio A22): the game stopped a few seconds after start, when the GPU's
  own shader compiler crashed on the app's largest shader. That shader is no longer built there; each picture's shaders
  are built when first needed, so a new effect may hitch once. Untested on a PowerVR phone: please report how it goes.

## 0.1.3

- **Fixed the crash at start that 0.1.2 still had on some phones whose graphics driver is Vulkan 1.1** (for example
  Mali-G57 phones with a Unisoc chip). Your code library stays valid.

## 0.1.2

- **Fixed a crash right after start on phones whose graphics driver is Vulkan 1.1** (for example some Adreno 730 phones):
  0.1.1 got past the black screen, then stopped at the first frame. Your code library stays valid.
- **Fixed the game stopping on the first start on some Android 14 phones** (for example a Moto g55): it couldn't make its
  save folders.

## 0.1.1

- **Fixed a black screen on start on Adreno 7xx phones** (for example the Snapdragon 8+ Gen 1's Adreno 730): their
  graphics driver was refused at start. Your code library stays valid: no need to build it again.

## 0.1.0

The first public version: Monster Hunter 4 Ultimate (USA, 1.3.0) recompiled to native code for Android, with your own
copy of the game.

- **Native, not emulated:** the game's code is recompiled on your PC into a library for your phone, and runs on a
  runtime of its own, with the 3DS's graphics drawn through Vulkan.
- **Your phone's resolution** by default, widescreen that matches your screen (4:3 handhelds too, no bars), and a frame
  rate limit from 30 to 240, with an optional steady frame rate.
- **Low-end mode** and **Toaster mode** for weak phones and long hunts on one charge; turned on by itself the first time on
  a weak phone.
- **Touch controls made for phones:** lock-on with the padlock (the camera follows the monster) and the target button to change monster, the item bar by tap and swipe without the D-pad, a layout of their own for
  bows and bowguns, combined buttons, and a layout editor that also moves the game's own HUD.
- **Controllers and keyboards:** the right stick as the camera, button mapping per controller, START+SELECT for the
  settings, a full keyboard layout.
- **Town actions without the touch screen:** Guild Cards, Gestures, Friends and Hunters for Hire from a list (a touch
  button in town, SELECT on a controller); what they open shows large in the middle, with a cursor for the controller.
- **Menus with your own layout:** the controls don't switch when a menu opens. A small D-pad shows only while a menu
  needs it (item boxes, shops, the quest board, the START menu…), and your buttons fade to half so the menu stays
  readable. Both can be placed, sized or turned off.
- **Item bar arrows:** a tap moves one item, holding repeats; they follow the item bar wherever you put it. Swiping still
  works.
- **Quick taps always count:** a tap on the touch screen is held until the game has seen it.
- **Credits & Licenses:** the credits when the app opens, a one-time thank-you, and a page with the credits, the legal
  notice and every license.
- **The single-screen mod** works (get it from its author; it's built into your library).
- Mods with load order and conflicts, cheats in Azahar's format, network rooms and rooms on the same Wi-Fi, save import and
  export, your own GPU driver on Adreno, an FPS counter with the temperature, and an error screen that shares the log.
- Android 9 and newer, 64-bit.

### Known

- The single-screen mod's big area map (shown after you take the Map from the supply box) can't be moved yet.
- In town, SELECT opens the town actions instead of the game's Customize Panels; set "Town actions button" to another
  button or off in Controls to get it back.
- With a power-saving game profile, some phones hold about 105-110 FPS instead of 120 in the field; 60 is steady.

### Official downloads

MH4U for Android is free. Only the links khorne-hub gives are official: if you got it anywhere else, paid for it, or it
came with ads, it isn't the official one. Check the APK's SHA-256 against the release's `.sha256` file.

## Español

## 1.0.2

Tu librería de código sigue sirviendo: no hace falta crearla de nuevo.

La 1.0.2 trae MH4U para PC, arreglos de lo que reportaron desde la 1.0.1.1 y una buena tanda de sus sugerencias, casi
todas en los dos ports.

**MH4U para PC.** La misma carpeta del juego ahora se juega en una PC con Windows: `MH4U-PC-1.0.2.zip` en la versión
publicada, con su SHA-256. La librería de código de la app de Android sigue sirviendo: no hace falta volver a armarla
para el teléfono.

### MH4U para PC (nuevo)

- `MH4U.exe` corre tu propia copia de forma nativa en Windows 10 y 11 (64 bits, una tarjeta gráfica con Vulkan), con el
  mismo runtime, los mismos ajustes, partidas, mods y multijugador que la app. El zip trae solo el programa, nada del juego.
- **El código del juego lo crea el mismo programa** con tu propio juego y su actualización, en tu PC: la primera vez
  descarga Python y Zig (unos 90 MB, verificados con su SHA-256), y después tarda de 10 a 40 minutos, con prioridad baja.
  **Cancelar**, **Seguir** y **Empezar de cero** como en el teléfono; los mods de código de la carpeta `mods` entran, como
  en la app. Más adelante: **F1**, **Sistema**, **Código del juego**, **Crear el código del juego**.
- **Teclado y ratón:** WASD, el ratón gira la cámara, el clic izquierdo ataca, F es A, un toque de Alt fija la cámara, el
  clic derecho es A solo con el arma afuera; cada tecla se puede cambiar (hasta tres por botón, también botones del
  ratón); **F2** lista tus controles sobre el juego; **1** a **4** los atajos de objetos de la pantalla táctil; **/** el chat.
  WASD mueve el cursor en los menús del juego; el nombre del cazador y el chat se escriben con el teclado.
- **La pantalla táctil** al lado de la de arriba (lo de fábrica en PC), debajo, en una esquina o en su propia ventana, con
  el ratón como tu dedo; el modo de pantalla única como en la app.
- **Ventana:** pantalla completa sin bordes de fábrica, cualquier ventana o pantalla completa exclusiva (**F11** /
  **Alt+Enter**); la imagen sigue la forma de la ventana en vivo, de 5:3 a ultra ancha; **Anclaje del HUD** para
  pantallas anchas.
- **Cuadros por segundo:** límites de hasta 240 o **Sin límite**, VSync encendido o apagado; sin límite en la PC del
  desarrollador (3440x1440, 6x), de 293 a 315 en la aldea, de 408 a 420 en el salón y de 305 a 310 en una misión; los
  límites se mantienen firmes. **Iniciando el juego...** se ve hasta la primera imagen. **Mostrar FPS y datos de la PC**
  (Básico, Completo). **Filtro de escalado** (Suave,
  Píxeles nítidos, Vecino más cercano o FSR 1), **Salida HDR** (apagada de fábrica), **GPU**.
- **Mandos:** START y SELECT abren los ajustes, el clic del stick derecho fija la cámara, **Íconos de los botones** al
  estilo de tu mando; **Apuntar con giroscopio** lee el giroscopio de un mando.
- **Partidas:** tu partida en `%APPDATA%\MH4U`; **Importar partida**, **Exportar partida** y **Carpeta de partidas** como
  en la app, con copias en `save_backups`. **Importar DLC y misiones de evento** y **DLC en todas las partidas** como en
  la app.
- **Discord Rich Presence**, y **Verificar archivos del juego**. Si el juego se cierra por un error, una ventana dice qué
  pasó y abre la carpeta del registro.
- **Pausar en segundo plano** detiene el juego mientras su ventana está minimizada; **Buscar actualizaciones** busca una
  versión nueva en GitHub como mucho una vez al día y avisa al arrancar la próxima vez; **Imágenes de los botones**
  agrega tus propias imágenes para los controles táctiles en una PC con pantalla táctil.
- **El menú F1 es el de la app:** las mismas páginas, secciones y nombres, con las filas propias de la PC (ventana,
  VSync, teclado y ratón, HDR, GPU, Discord) en los lugares donde las tiene el MHGU para PC.

### Arreglos

- **El engranaje se movía al abrirse un menú** y quedaba encima de otros botones: en el modo de pantalla única se iba a
  la esquina de la pantalla (y con la esquina de la pantalla táctil sobre su lado, se corría con los botones de ese
  lado). Ahora debería quedar donde está, o donde lo pusiste en **Personalizar controles y HUD**.
- **Objetos rápidos: la pieza se volvía una copia chica de la pantalla de arriba** a los pocos segundos, hasta que la
  tocabas, y la esquina de la pantalla táctil mostraba un destello de la pantalla de arriba al abrirse. Las dos cosas se
  fueron.
- **La cruceta de menú no aparecía en la ventana de Entregas**
  ([#73](https://github.com/khorne-hub/4u-native/issues/73)): en **Entregar**, con la cantidad, quedaban los controles
  de caza. Ahora aparece ahí, como en la caja de suministros.
- **El Mapa de la zona movido en Personalizar controles y HUD dejaba atrás el mapa completo**
  ([#72](https://github.com/khorne-hub/4u-native/issues/72), [#75](https://github.com/khorne-hub/4u-native/issues/75),
  [#76](https://github.com/khorne-hub/4u-native/issues/76)): cuando la pantalla principal muestra el mapa completo del
  coto, ahora va con la pieza, así la flecha del cazador ya no queda fuera del mapa.
- **Teléfonos cuyo driver necesita mucha memoria para shaders muy grandes** (un Exynos 2200 se cerraba a los pocos
  segundos de abrir): los shaders de respaldo grandes ya no se arman de antemano, nunca dos a la vez, ni justo después
  de una sesión que se cortó armando uno. Debería dejar de cerrarse (no probado en ese teléfono).
- **Una lista de shaders guardada que crecía durante semanas** (en una Adreno 610 rompía texturas y sacaba el aviso del
  "camino más lento"): la lista guarda los 16.384 shaders más usados, y el arranque deja de armarlos de antemano cerca
  del límite de mapas de memoria del teléfono. Debería desaparecer.
- **La creación en el teléfono se pausaba "por calor" con el teléfono frío** (batería a 35 °C): algunos teléfonos dicen
  estar calientes sin estarlo; con la batería por debajo de 40 °C esa lectura ya no debería pausar la creación.
- **Las primeras cargas después de mucho tiempo en segundo plano** podían tardar mucho más que lo normal: al volver
  después de un minuto o más, la app le pide enseguida a Android la memoria del juego, así que deberían tardar lo de
  siempre.
- **Perfiles de controles: el perfil 1 volvía al diseño de fábrica** al pasar a otro perfil sin guardar. Dejar un perfil
  ya no debería vaciarlo, y el editor debería preguntar antes de que **Cargar** o **Nuevo** descarten cambios sin
  guardar.
- **Glaive insecto: para lanzar el kinsecto hacía falta deslizar de R a X y volver:** un solo deslizamiento de R a X
  debería lanzarlo ahora.
- **El giroscopio con los controles de arco Tipo 2** se cortaba en el modo de apuntar: debería seguir girando la mira
  mientras el arco se tensa y carga.
- **Diseño de emulador:** las filas de **Botones en la pantalla** ahora también deberían mostrar u ocultar sus propios
  botones.
- **Los controles táctiles desaparecían en plena partida:** si algo que el teléfono toma por un mando los oculta, tocar
  el juego unas veces debería decir qué dispositivo es y cómo verlos igual.
- **Las imágenes del juego y las de un pack de texturas aparecían en la galería del teléfono:** un `.nomedia` en la
  carpeta del juego las deja afuera.
- **Rendimiento:** la conversión de imágenes que el juego pide cada pocos segundos es varias veces más rápida, y el
  renderer ya no guarda un registro que nadie leía justo después de cada carga. En teléfonos modestos los primeros
  segundos después de una carga deberían ir más parejos.
- **Un teléfono que entraba al hub de una PC salía del Salón a los pocos minutos** (con la PC de anfitrión): la conexión
  del juego lleva todo en orden, así que un paquete perdido camino a un servidor de hubs lejano frenaba el resto cerca
  de un segundo, y el juego lo tomaba como un jugador que se había ido. En un enlace lento (150 ms o más de ida y
  vuelta) cada paquete ahora va dos veces, a 40 ms, así que una pérdida sola ya no frena el juego, y la conexión ya no
  se angosta en esos enlaces (a veces esperaba una ida y vuelta entera para mandar). En nuestras pruebas, con un
  servidor de hubs tan lento y con tantas pérdidas como uno lejano, el teléfono se quedó en el Salón de la PC; entre dos
  teléfonos también debería ayudar. Ese enlace usa más o menos el doble de datos (unos 10 a 20 KB/s por sentido). Para
  el arreglo completo todos en el hub necesitan la 1.0.2; las versiones anteriores siguen jugando contigo como antes.
- **Los teléfonos esperan a un anfitrión que se atrasa, como la PC:** en un Salón, cuando el anfitrión no mandó nada por
  medio segundo (un hub lento, o una PC anfitriona que se traba un momento), el juego lo espera en vez de darlo por ido
  y salir del Salón.

### Lo nuevo, por sus sugerencias

- **Tamaño del HUD** (engranaje, **Gráficos y audio**, **Pantalla y HUD**): todo el HUD más chico, de 60 a 100 %, que
  cambia al mover el valor, con las ventanas de diálogo y de sí o no de las misiones.
- **Cada parte del HUD se puede achicar o agrandar** (- / + o pellizcando) **y ocultar** en **Personalizar controles y
  HUD**.
- **Botón del mapa:** un botón en el borde izquierdo muestra u oculta el mapa durante la cacería (**Controles**,
  **Botones en la pantalla**, **Botón del mapa**). La ayuda también dice cómo ver el mapa en la pantalla principal:
  quita el Mapa de tus paneles en Personalizar paneles (menú de misión).
- **Árbol de armas en la herrería:** cuando la herrería muestra un arma (Forjar o Mejorar), un botón redondo nuevo (o el
  clic de la palanca derecha) abre el árbol de esa arma, que debería verse como los árboles de la serie: cada arma es el
  ícono de su clase, leído de tu propia copia del juego y con el color de su rareza, con su nombre; las que tienes (en
  la caja o equipada), las anteriores y el paso siguiente de cada una como "?". Al lado, del arma elegida: ataque,
  elemento o estado, afinidad, defensa, ranuras, rareza y filo, y lo que hace falta para forjarla o mejorarla
  (materiales y zenny). Solo consulta: se forja en la herrería. Los nombres, en el idioma del juego. Debería salir en
  todas las pantallas de la herrería con un arma: avísennos si falta en alguna. **Controles**, **Botones en la
  pantalla**, **Botón del árbol de armas** saca el botón.
- **Caja de suministros sin espera:** en una misión cada objeto pasa a la bolsa al instante y se puede sacar el
  siguiente enseguida (antes era medio segundo cada uno). Las reglas del juego no cambian (el lugar en la bolsa, el
  máximo por objeto), y en línea el anfitrión sigue repartiendo cada objeto, así que con otros debería funcionar igual.
  Gracias a Alfonso Matador por la idea.
- **Botones combinados en cualquier botón del mando:** A, B, X, Y (por su lugar), L, R, ZL y ZR (pasada la mitad),
  START, SELECT, la cruceta, los clics de las palancas, Guide, las paletas y el panel táctil. **Apretar un botón del
  mando** toma también uno que el mapa del mando no nombra, como algunas paletas en Android. Ese botón debería apretar
  entonces el combinado en vez del suyo.
- **Hasta 15 botones combinados, cada perfil de controles con los suyos:** los que ya tenías deberían quedar en tus
  perfiles.
- **Exportar e importar un perfil de controles** en un .zip (**Controles**, **Perfiles de controles**, un perfil):
  debería llevar sus imágenes y sus botones combinados, y si ya hay una imagen con ese nombre, la nueva debería entrar
  con otro nombre.
- **Cámara solo en la mitad derecha** (**Controles**, **Controles táctiles**, apagado de fábrica): arrastrar debería
  girar la cámara solo en la mitad derecha de la pantalla.
- **Editor táctil:** un toque en el centro de la palanca o en el SELECT de la cruceta de menú debería elegirlo para una
  imagen propia.
- **Presets de color** (**Gráficos y audio**, **Color**, **Preset 1** a **3**): guardan un ajuste de color y se vuelve a
  él con **Usar**; también guardan los valores negativos.
- **FSR 1** en el **Filtro de escalado** (**Gráficos y audio**, apagado de fábrica): la pantalla de arriba agrandada con
  bordes más nítidos cuando se muestra más grande que su imagen, con su propia **Nitidez de FSR 1**.
- **Driver de GPU propio en cualquier GPU** (**Gráficos y audio**, **Avanzado**, **Driver de GPU**), bajo tu
  responsabilidad: si un driver no arranca, la app debería volver sola al del sistema. El registro y la pantalla de
  error dicen qué driver se usa.
- **Packs de texturas HD:** su presupuesto de memoria ahora sigue a la memoria libre del teléfono (con un margen y un
  techo de 3 GB), y baja cuando Android pide memoria. Los packs pueden traer texturas **.ktx2** (ETC1S o UASTC de Basis
  Universal, o RGBA8 / BC7) junto a sus .png o en su lugar; en el teléfono, por ahora, ocupan la misma memoria que un
  PNG.
- **Los números de daño** también deberían salir en los monstruos chicos, y hay un nuevo **Estilo de los números de
  daño** (**Gráficos y audio**, **Pantalla y HUD**): **Clásico**, **Con contorno**, **Compacto** y **Ráfaga** (una
  seguidilla de golpes debería sumarse en un solo número). El color **Por golpe** del MHGU (punto débil, partes duras,
  críticos, el elemento) necesita en el MH4U un cambio que obligaría a todos a volver a crear la librería de código, así
  que queda para una versión más adelante.
- **Contorno del texto** (**Gráficos y audio**, **Pantalla y HUD**, apagado de fábrica): debería dibujar un contorno
  oscuro fino alrededor de las letras del juego, para el texto que cuesta leer.
- **Sonido en las grabaciones de pantalla** (**Gráficos y audio**, **Imagen y sonido**, apagado): para los teléfonos
  cuya grabación de pantalla no graba el sonido del juego. Debería grabarlo desde el próximo inicio.
- **Multijugador › Esperar al jugador que se traba** (apagado de fábrica): cuando el juego de otro jugador se traba un
  momento (un teléfono lento, la app en segundo plano), el tuyo lo espera en vez de adelantarse, así debería ser menos
  probable que el juego lo eche. Solo entre jugadores de esta app que lo activen; toda la sala puede detenerse un
  momento. Las versiones anteriores y los emuladores juegan contigo como antes.
- **El chat en una ventana propia:** la página del chat (engranaje, **Multijugador**, **Chat**, o el botón del chat) se
  abre como una ventana más chica sobre el juego, que deja ver lo de atrás, solo con el chat: su encabezado tiene una
  pestaña por cada chat que hay ahora, el engranaje abre las opciones del chat y la X la cierra; debajo, el texto para
  escribir y **Enviar**, y las líneas, la más nueva primero, cada una sobre una tira oscura para que se lea sobre pasto
  o nieve. Los interruptores, **Amigos** y las demás opciones están en la página de opciones del chat. Arrastra su parte
  de arriba para moverla y su esquina de abajo a la derecha para cambiar su tamaño; queda donde la dejes. Mientras el
  teclado del teléfono está abierto, la ventana debería subir arriba de todo de la pantalla y usar el alto que deja el
  teclado, así el texto, **Enviar** y las últimas líneas quedan a la vista.
- **La ventana del chat flota sobre el juego:** jugando con el dedo, y en PC con el ratón, el juego sigue debajo y los
  controles táctiles funcionan alrededor. Solo los controles de la ventana toman el dedo o el clic (su encabezado y
  pestañas, el texto y **Enviar**, el tirador de la esquina); tocar su parte vacía mueve la palanca o la cámara.
  Mientras escribes, tocar o hacer clic fuera de ella la cierra, y con ella el teclado del teléfono. Un menú del juego
  que se abre la esconde hasta que ese menú se cierra, el botón de ajustes (**F1** en PC) va al menú y el botón del chat
  la cierra. En un teléfono con un mando en la mano sigue siendo un menú que se maneja con sus botones, como antes.
- **Pestañas en la ventana del chat:** Sala, General, Global y Amigos, solo las que hay ahora, cada una con las líneas
  que no leíste (hasta 99); en **Amigos**, las flechas pasan de un amigo a otro.
- **Qué tan transparente es la ventana del chat:** en las opciones del chat, **Fondo de la ventana**, **Fondo de los
  mensajes** y **Fondo del campo de escribir**, de 0 a 100 % de a 5, con - y + o una barra; el texto queda siempre
  sólido.
- **Las líneas junto al botón del chat:** 8 segundos, hasta cuatro, cada chat con su color (la sala celeste, General
  verde, Global dorado, un amigo rosa) y el nombre de cada jugador con un color propio, también en la ventana del chat;
  las tuyas no, y se van al abrirse un menú.
- **Créditos:** Créditos y licencias y el README nombran a quienes aportaron al desarrollo del port.
- **El botón del chat en Personalizar controles y HUD:** se mueve y se cambia de tamaño como cualquier botón, y queda
  guardado con el perfil de controles (opciones del chat: **Mover el botón del chat**).
- **El botón del chat en Personalizar controles y HUD:** se mueve y se cambia de tamaño como cualquier botón, y queda
  guardado con el perfil de controles (opciones del chat: **Mover el botón del chat**).
- **El pad de menú, prendido o apagado:** el pad que aparece en los menús del juego se apaga en engranaje,
  **Controles**, **Botones en la pantalla**, **Pad de menú** (prendido de fábrica); su ayuda ahora dice qué mueve el
  cursor sin él.
- **Aparatos con dos pantallas** (AYN Thor, Retroid Pocket Duo): **Gráficos y audio**, **Pantalla y HUD**,
  **Disposición**, **En la segunda pantalla** pone la pantalla táctil en la segunda pantalla, y se toca ahí; si esa
  pantalla se va, vuelve la disposición que tenías. Apagado de fábrica, y solo aparece si hay una segunda pantalla.
  Todavía no se probó en uno de esos aparatos: debería funcionar, cuéntennos cómo anda.
- **En lado a lado**, la pantalla táctil debería quedar fuera del agujero de la cámara o la muesca del borde.
- **Ajustes:** un valor demasiado largo para su fila ahora debería deslizarse despacio en la fila elegida o tocada, para
  poder leerlo entero.
- **Escalado rápido (más FPS)** (engranaje, **Gráficos y audio**, apagado de fábrica): agranda la imagen del juego de
  forma más simple cuando la resolución 3D no es el tamaño con que la muestra tu pantalla (por ejemplo 2x en una
  pantalla de 720 líneas): los teléfonos débiles ganan muchos cuadros por segundo (un Moto g06 en modo Normal a 2x:
  pueblo 47 → 56 FPS, Salón 45 → 57, campo 40 → 50). Algunos colores pueden variar apenas.
- **El menú de ajustes, ordenado como el del MHGU:** las mismas páginas, secciones, orden y nombres en las dos apps. En
  **Pantalla y HUD**: **Campo de visión**, **HUD en los bordes**, **Tamaño del HUD**, **Contador de FPS**, **Contorno
  del texto** y después la sección **Ayudas de caza**. **Filtro PS1** va antes de **Sombras**; en **Controles
  táctiles**, **Pulsación larga oculta los controles** va después de **Palanca flotante**; en **Avanzado**, **Generación
  de cuadros** va después de **Borrar la caché de shaders** y la sección **Driver de GPU** queda al final; **Licencias**
  está debajo de **Mostrar los créditos**. Dos filas tienen los nombres del MHGU: **Agregar un mod** (el .zip) y
  **Agregar trucos**. Tus ajustes quedan como están.

## 1.0.1.1

Tu librería de código sigue sirviendo: no hace falta crearla de nuevo.

La 1.0.1.1 es una actualización chica después de la 1.0.1: arreglos de lo que reportaron, el filtro PS1 que pidieron y
los objetos rápidos en la pantalla principal.

### Arreglos

- **"Recolectar todo de un toque" cerraba el juego** ([#71](https://github.com/khorne-hub/4u-native/issues/71)) en los
  puntos de varios usos y al tallar, con la versión europea del juego cuyo código se armó con la 1.0: a ese código le
  faltaban los lugares del juego que agregó la 1.0.1, y la opción llamaba a uno equivocado. Ahora la app los completa
  sola, sin rearmar nada, y si alguna vez faltara uno la opción no haría nada en vez de cerrar el juego.
- **El nombre del monstruo en las barras de vida** no se veía en los teléfonos: ahora se lee de los archivos del juego
  como los lee el juego.
- **Arco: al soltar el disparo la flecha va sola al monstruo**, como se pidió para la 1.0, que solo andaba con Fijar
  objetivo y con uno de los dos controles de arco del juego. Ahora va al monstruo que fijaste o elegiste con la cámara
  de objetivo, o si no al monstruo grande más cercano que se ve, con o sin Fijar objetivo y con cualquiera de los dos
  controles de arco. Si al tensar giras la cámara, o arrastras sobre Disparo, lejos del monstruo, la flecha va adonde
  apuntas. Engranaje, **Controles**, **El arco apunta al monstruo** (activado). Si en el juego el arco se tensa con R,
  el botón táctil Disparo ahora tensa y dispara (antes abría el modo de apuntar).
- **La creación en el teléfono parecía trabada en 10 %** ("Traducir el código del juego"): partes de la traducción
  trabajaban minutos sin mostrar avance. Ahora el porcentaje avanza todo el tiempo, el reloj de la pantalla sigue
  corriendo, usa un poco menos de memoria y, si se queda callada, su registro dice cada minuto qué está haciendo. Si
  la tuya se quedó ahí, actualiza y toca **Seguir**: continúa desde donde quedó.
### Lo nuevo, por sus sugerencias

- **Filtro PS1**, apagado por defecto (engranaje, **Gráficos y audio**, **Filtro PS1**): el aspecto de los juegos 3D
  de fines de los 90. Texturas en bloques grandes y nítidos, la imagen de la pantalla de arriba en las 240 líneas de
  la consola con colores más fuertes y colores de 15 bits con un patrón fino de puntitos, polígonos que tiemblan y
  texturas que se doblan, y líneas de barrido suaves que parpadean. No cambia ningún otro ajuste, y la pantalla
  táctil, los controles táctiles y nuestros menús siguen nítidos. Gracias a Ysacaiah por la idea.
- **Modo gama baja › Ladrillo**, un nivel nuevo por debajo de Tostadora, para los teléfonos más débiles: todo lo de
  Tostadora, y las texturas del mundo 3D a un dieciseisavo de su tamaño, en bloques grandes y nítidos, así el teléfono
  lee menos memoria en cada cuadro. Ladrillo es para los FPS, el filtro PS1 es para el aspecto, y se pueden usar
  juntos.
- **Mostrar objetos rápidos**, apagado por defecto (engranaje, **Gráficos y audio**, **Pantalla y HUD**): en una
  misión, el panel de objetos rápidos de la pantalla táctil también se ve en la pantalla principal, en chico, y tocar
  un objeto ahí lo usa. Se esconde mientras se ve toda la pantalla táctil, con un menú del juego abierto y si tu
  distribución de paneles no tiene panel de objetos rápidos. Se mueve y se cambia de tamaño en **Personalizar
  controles y HUD**. Se prende y se apaga al momento, también en medio de una misión, y no baja los FPS.

## 1.0.1

Tu librería de código sigue sirviendo: no hace falta crearla de nuevo.

La 1.0.1 arregla problemas que nos contaste después de la 1.0 y suma algunas de tus sugerencias. Sus primeros arreglos
también se probaron en nuestros teléfonos (Mali-G57, Mali-G52 y Adreno 830); lo demás, por ahora solo en una PC: donde una
línea dice "debería", cuéntanos cómo te va en el tuyo (en el Discord o en un issue, con el registro).

- **PowerVR GE8300, GE8320 y otras GPU PowerVR Rogue:** la 1.0 se cerraba en cada inicio después del primero: armaba en
  segundo plano los shaders que ya habías visto, y el driver de estas GPU se cae cuando se arman shaders en dos lugares a
  la vez. Estas GPU vuelven a la forma de la 0.1.9.2: de a uno, cuando aparecen por primera vez, sin nada en segundo
  plano. Eso debería terminar con los cierres; el tirón corto la primera vez que aparece un efecto vuelve a ser como en la
  0.1.9.2. Las PowerVR BXM siguen como en la 1.0 (Discord).
- **GPU PowerVR BXM que se cerraban a los diez minutos** ([#31](https://github.com/khorne-hub/4u-native/issues/31),
  Discord): en estas GPU la pantalla superior llega a la pantalla del teléfono por una copia enderezada, y dibujarla
  empezaba con un borrado que nunca se veía. El driver de las PowerVR B-Series armaba un shader para ese borrado en cada
  cuadro y se quedaba con algo de memoria cada vez, hasta que Android no le dejaba a la app mapear más (las entradas de
  mapa del registro, unas 7.000 más por minuto). La copia ya no borra, y la imagen es la misma (comprobado en una PC,
  píxel por píxel). No tenemos un teléfono PowerVR: cuéntanos si los cierres se fueron, con el registro.
- **La app se cerraba durante el armado en el teléfono** (cerca del 10 %, en "Traducir el código del juego"): el menú de
  opciones, abierto detrás del armado, se caía mientras el juego estaba en segundo plano y se llevaba el armado con él.
  Los menús ya no se dibujan mientras la ventana del juego está oculta, lo que debería arreglarlo (Discord).
- **El armado se pausaba por "muy caliente" con el teléfono fresco:** algunos teléfonos avisan que están calientes sea
  cual sea su temperatura real. Ahora el armado también mira la temperatura de la batería (y el margen térmico desde
  Android 11): con la batería fresca sigue, más despacio, en vez de pausarse, y una batería a 47 °C o más lo sigue
  pausando. Un teléfono que de verdad está caliente se pausa como antes. **Compartir registro** ahora incluye el último
  armado (Discord).
- **"Vuelve a hacer la librería en la PC (paso 3 de la guía)":** la pantalla de error y la página **Mods** ahora llevan a
  engranaje, **Sistema**, **Código del juego**, **Crear el código del juego** (la herramienta de la PC sigue siendo la otra
  forma), y la pantalla de error tiene un botón **Crear el código del juego**: cuando la librería está hecha, **Jugar**
  arranca el juego (Discord).
- **Packs HD de texturas enteros** ([#56](https://github.com/khorne-hub/4u-native/issues/56)): la 1.0 abría cada archivo del pack al empezar (una pantalla negra
  larga) y leía cada textura en el hilo de dibujo la primera vez que aparecía (el juego casi se detenía). Ahora al empezar
  solo se lee la lista del pack, y dos hilos propios leen las texturas mientras el juego sigue: una textura se ve a calidad
  normal un momento y después pasa a HD. Probado en una PC con un pack de prueba de 380 texturas; un pack real debería
  comportarse igual. Las texturas HD siguen ocupando memoria del teléfono.
- **Barras y bloques de colores con el bloom en GPU Adreno 6xx con el driver Vulkan 1.1 (1.1.128)**
  ([#32](https://github.com/khorne-hub/4u-native/issues/32), Discord): la causa probable son las pasadas chicas del bloom, que empezaban limpiando una parte de la
  imagen; estos drivers más viejos parecen dibujar mal los bordes de esa parte. En las GPU Adreno con un driver Vulkan 1.1
  ahora cargan la imagen en su lugar (la misma imagen, un poco más de tráfico de memoria). Esto debería quitar las barras.
  El bloom queda activado como lo tengas. No tenemos un teléfono así: cuéntanos si desaparecieron.
- **El notch en Android 9 y 10** ([#60](https://github.com/khorne-hub/4u-native/issues/60)): ahora el juego también se dibuja debajo de un notch o un agujero de
  cámara en esas versiones, como ya lo hacía desde Android 11, así que debería llenar la pantalla.
- **Arco con fijar objetivo** ([#58](https://github.com/khorne-hub/4u-native/issues/58)): con el objetivo fijado, un arco tensado giraba al cazador hacia el monstruo
  en cada cuadro, así que solo podía caminar derecho hacia él. Ahora el cazador gira hacia el monstruo cuando aprietas
  **X** y cuando disparas; mientras caminas con el arco tensado, la palanca decide la dirección. Las ballestas funcionan
  igual. Todavía sin probar en una cacería.
- **La cámara de fijar objetivo y los monstruos voladores** ([#62](https://github.com/khorne-hub/4u-native/issues/62)): la cámara seguía la altura de un monstruo en
  el aire hasta arriba; ahora solo se inclina hacia arriba hasta cierto punto (unos 17°, de a poco), así que tu cazador y
  el suelo siguen a la vista. Todavía sin probar en una cacería.
- **Salas públicas:** cuando el lobby por defecto no da una lista, el aviso ahora dice que lo mantienen jugadores y a
  veces se cae, o que esta red lo bloquea, en vez de pedirte revisar una dirección que nunca escribiste. Las salas por
  dirección siguen funcionando (Discord).
- **El juego detrás del menú de opciones se ponía negro** a los pocos segundos de abrir el menú (desde la 1.0, en todas
  las páginas y en el editor de controles): la app soltaba la última imagen del juego mientras el juego esperaba detrás
  del menú. Ahora la conserva, así que el juego sigue a la vista ahí. En la página **Color** el juego alrededor del panel
  no se oscurece y el panel es translúcido, así ves cada cambio mientras lo haces. Probado en un teléfono con Mali-G52.
- **El modo de pantalla única en pantallas anchas, y sus placas** ([#57](https://github.com/khorne-hub/4u-native/issues/57),
  Discord): en una pantalla más ancha que 16:9, los menús que nuestro modo de pantalla única dibuja arriba podían mostrar
  una lista estacionada al costado (la del mostrador de misiones). Ahora, siempre que la imagen es más ancha que la
  pantalla de la consola, se recortan a su ancho, como los recortaba la consola; en una pantalla 4:3 no cambia nada.
  Las placas de la pantalla táctil y las preguntas de sí o no ahora van donde no tapan nada: en la caja de objetos y en la de equipo, las placas de confirmar y volver quedan a la
  izquierda de la grilla, y una pregunta (como "¿Vender por 500z?") va abajo a la izquierda con Sí / No encima; en el
  mostrador de misiones, "¿Elegir esta misión?" va debajo de la lista con Sí / No uno al lado del otro dentro de su
  recuadro, sin tapar la ficha de la misión; en la Tienda del gremio, la pregunta de compra va abajo a la izquierda. El contador de páginas de la
  cantina ya no tapa la descripción, y el panel de Costo de la cocina al aire libre se dibuja delante de su papel. El
  mapa del mundo, el del pueblo y el de la cacería siguen en la pantalla táctil, como antes: se usan tocando. En un menú
  lleno, algunas de sus partes (la ficha de la misión en el mostrador, por ejemplo) podían dibujarse sin mover, al
  tamaño del juego, debajo de las placas: a nuestro modo se le acababa el lugar para moverlas. Ahora le pide más lugar
  al juego. Probado en una PC en inglés y en español en 4:3, 16:9 y 20:9, y en teléfonos 4:3, 20:9 y 20,5:9 (el
  mostrador de misiones, la caja de objetos y la de equipo, la cantina); lo demás debería verse igual en un teléfono.
- **El mapa de la zona en una expedición** ([#65](https://github.com/khorne-hub/4u-native/issues/65)): movido en el
  editor, el mapa dejaba atrás tu flecha, los íconos de los monstruos y la hiedra de algunas zonas. Ahora se mueven
  juntos. Probado en una PC.
- **El dibujo usa menos CPU:** la parte del dibujo que corre en el procesador hace el mismo trabajo en menos tiempo (los
  programas de vértices que la librería de código no trae, el caché de vértices, la búsqueda de pipelines, y ya no se
  envía trabajo vacío por una pantalla táctil oculta). En un teléfono con Mali-G52, en el campo, el hilo del dibujo usó
  cerca de un 3 % menos de CPU, y con el teléfono ya caliente y su procesador más lento, se mantuvo en 59,8 FPS donde
  antes bajaba a 55,5. La imagen es la misma (145 capturas en una PC, ni un píxel distinto).
- **Hacha cargada corriendo** (Discord): con los controles táctiles, X + A corriendo (un toque en el medio de la píldora de
  ataques) no hacía nada con el hacha cargada enfundada desde la 0.1.9: los controles soltaban la R de la carrera, y el
  juego desenfunda el hacha cargada directo en modo hacha solo con R + X + A. Ahora una carrera que ya lleva un momento
  mantiene la R durante X + A, como una R apretada en un mando, con todas las armas (la gran espada se cubre desde la
  funda, como con un mando); una carrera que recién empieza sigue soltando la R, para que la gran espada no se cubra sin
  querer. Probado en una PC; todavía no en una cacería en un teléfono.
- **Glaive insecto: R y deslizar hacia A o X solo hacía R** (Discord): en el diseño de emulador no se seguía el pulgar que
  se deslizaba, y en el diseño propio del port la R se soltaba al llegar a los ataques. Ahora deslizar entre botones deja
  los dos apretados (abajo).
- **Más detalle en el registro:** cuando la GPU deja de responder ("device lost"), el registro ahora lo dice con palabras
  claras ([#61](https://github.com/khorne-hub/4u-native/issues/61)).

**Lo nuevo de la 1.0.1, por tus sugerencias**

- **Tu imagen es todo el control** (Discord): con tu PNG en un control, solo se dibuja tu PNG, sin el disco, el aro, la
  letra ni el brillo del port; apretado, la imagen se achica y se oscurece un poco. Ahora todo control que ubica el editor
  acepta imagen: la palanca (base y centro aparte: **Para: base / centro**), las dos crucetas, el candado de carrera, las
  flechas de la barra de objetos, la píldora de ataques, la banda de la esquina, el pad de menú, los botones de opciones,
  pantalla táctil, pueblo, fijar objetivo y cámara, los botones combinados y todos los del diseño de emulador. Sin imagen
  nada cambia. Antes, pasadas las 48 imágenes las demás no se veían; ahora se guardan hasta 64, y se sueltan las que no
  están en la pantalla.
- **Ocultar los controles con una pulsación larga** (Discord): mantén un dedo quieto en un lugar vacío (sin botón) unos tres
  cuartos de segundo y se ocultan todos los controles táctiles, el engranaje también, para las escenas o capturas limpias;
  hazlo otra vez en cualquier lugar para que vuelvan. Arrastrar la cámara, mantener un botón o un segundo dedo nunca los
  ocultan, y con ellos ocultos arrastrar sigue girando la cámara. Activado por defecto: engranaje, **Controles**,
  **Controles táctiles**, **Pulsación larga oculta los controles**.
- **Deslizar entre botones** (Discord): un pulgar que se desliza de un botón a otro deja los dos apretados, como dos dedos:
  R a A es R + A, R a X es R + X, R a B es R + B, y al volver a R, R sola. Activado por defecto (engranaje, **Controles**,
  **Botones en la pantalla**, **Deslizar entre botones: los dos apretados**); apagado, deslizar funciona como en la 1.0.
- **El área táctil de la barra de objetos** (Discord): en el editor, toca la barra de objetos y usa - / + (o pellizca)
  para agrandar o achicar la zona donde un toque usa el objeto y un deslizamiento gira la barra. La barra en sí es la del
  juego y no cambia de tamaño.
- **Suavizado de la cámara** (engranaje, **Controles**, apagado por defecto): el giro de la cámara al arrastrar entra de a
  poco, un poco detrás de tu dedo; el giro llega entero.
- **Límite de FPS desde 15** (Discord): se suman 15, 20 y 25. 15 y 20 van tan parejos como 30 en una pantalla de 60 Hz;
  25 es menos parejo. El juego mantiene su velocidad; solo dibuja menos cuadros. La línea de cada minuto del registro dice
  el límite y cómo se repartieron los cuadros.
- **Empezar de cero en el armado en el teléfono** (Discord): después de una creación cortada, cancelada o fallida,
  **Empezar de cero** borra lo que dejó (sus archivos de trabajo y el registro desde donde sigue), así la próxima vez
  **Crear** empieza desde el principio. Pregunta antes, y deja las herramientas descargadas salvo que elijas borrarlas
  también. **Borrar las herramientas** ahora deja la pantalla limpia, y el registro del armado anota el calor del teléfono
  al empezar y en cada etapa. Todavía sin probar en un teléfono.
- **Color** (Discord): engranaje, **Gráficos y audio**, **Color**, un filtro propio: tono, temperatura, tinte, brillo,
  contraste, saturación y gamma, todo en 0 (sin cambio) por defecto, y **Restablecer el color**. Cada cambio se ve en el
  momento mientras el menú está abierto; **Guardar** lo conserva. Cambia la imagen del juego en las dos pantallas, también
  su HUD y sus menús, nunca los controles táctiles ni este menú. En 0 la imagen se dibuja exactamente como antes, y el
  filtro no agrega una pasada propia (en un teléfono con Mali-G57, el dibujo de la pantalla de arriba pasó de 0,255 a
  0,267 ms).
- **Tu propio DLC** (Discord): **Importar DLC y misiones de evento** también trae el DLC que instalaste en Azahar o
  Citra, o tu propio `.cia` descifrado del DLC. La app no descifra nada. El juego usaba el DLC instalado solo en una
  partida que lo había recibido en línea, de un servicio que ya no existe: ahora la app le pone esa marca a todas las
  partidas (**DLC en todas las partidas**, activado por defecto, en la página de DLC y misiones de evento; antes guarda
  una copia de tu partida en `save_backups`). Probado solo con archivos de prueba: cuéntanos cómo te va con el tuyo. Los
  pasos están en el README, **Importar DLC y misiones de evento**.
- **Recolectar todo de un toque**, desactivado por defecto (engranaje, **Juego**, **Recolección**): una recolección en
  un punto (hierbas, hongos, miel, insectos, huesos, minerales...) o un tallado da de una vez todo lo que le quedaba,
  con las reglas del juego: habilidades de recolección y de tallado, piquetas y redes que se pueden romper. Se detiene
  si la bolsa se llena (sale la ventana del juego para cambiar o tirar, y lo demás queda en el punto) o si no puedes
  llevar más de un objeto.
- **Dos ayudas de caza**, incluidas y desactivadas por defecto (engranaje, **Gráficos y audio**, **Pantalla y HUD**):
  **Monstruos siempre en el mapa** muestra los monstruos grandes en los dos mapas desde el principio de la misión, como
  si estuvieran pintados (en las misiones que cazas solo), y **Números de daño** muestra el daño de cada golpe como un
  número que sube sobre el monstruo, chico, mediano o grande y del color que elijas. Las dos funcionan desde la memoria
  del juego: no hace falta volver a armar el código del juego.
- **Barra de vida de los monstruos**, desactivada por defecto (engranaje, **Gráficos y audio**, **Pantalla y HUD**): en
  una misión, una barra fina con el aspecto del HUD de la app, arriba a la derecha de la pantalla de arriba, muestra el
  nombre y la vida de cada monstruo grande con sus números (más tenue mientras está en otra zona). Se mueve y se cambia
  de tamaño en **Personalizar controles y HUD** (arrastrándola; con el asa de su esquina, un pellizco o - / +;
  Restablecer la devuelve a su lugar). Solo lee el juego, así que también anda en las salas.
- **Enviar todo a la caja, también lo del Felyne** (Discord, idea de Dark_Light): con **Enviar todo a la caja** activado
  (engranaje, **Juego**), **Enviar a la caja de objetos** al final de una misión también manda de una vez los objetos de tu
  Felyne y los de tus Felynes; lo que no entra queda en la lista, como siempre. Probado en una PC.
- **Los packs de texturas HD usan menos memoria** ([#56](https://github.com/khorne-hub/4u-native/issues/56)): el pack recibe una parte de la memoria que la
  app da a las texturas, según la RAM de tu teléfono; las texturas de las zonas que dejaste se sueltan después de un rato;
  y una textura que no entra ya no se decodifica una y otra vez (en una PC con el presupuesto de un teléfono de 4 GB, un
  pack de prueba entero pasó de 3903 decodificaciones a 497). En un teléfono de 4 GB con un pack 4x entero, algunas
  texturas pueden quedar como las del juego en vez de dar tirones. Nuevo: engranaje, **Mods y trucos**, **Packs de
  texturas**, **Tamaño de las texturas del pack: Mitad** (apagado por defecto) dibuja las texturas grandes del pack a la
  mitad de su ancho y su alto, un cuarto de la memoria; se aplica en el próximo inicio. Medido en una PC.
- **Generación de cuadros** ([#59](https://github.com/khorne-hub/4u-native/issues/59); opcional, apagada por defecto):
  engranaje, **Gráficos y audio**, **Avanzado**, **Generación de cuadros**. Con el límite de FPS en la mitad de la
  frecuencia de tu pantalla o menos (30 en una pantalla de 60 Hz), la app hace una imagen entre cada dos imágenes de la
  pantalla de arriba del juego, así ves 60. Es propia, para cualquier GPU; el HUD nunca se deforma, y queda apagada en los
  menús del juego, en el título, en este menú y en el editor de controles. Agrega un poco de retraso al control (más o
  menos un refresco de la pantalla), los giros rápidos de cámara pueden dejar manchas en los bordes del cazador y de lo
  que se mueve, y usa tiempo de GPU: si el juego no llega a su límite con ella prendida, se pausa sola (a la tercera,
  queda apagada hasta el próximo inicio). Probada en una consola portátil con Mali-G57 (el juego a 30, 60 mostrados, unos
  4 ms de GPU por imagen hecha a 1200x720); todavía sin medir en teléfonos tipo Mali-G52, donde es probable que se pause
  sola. El contador de FPS muestra "30 FPS (60 mostrados)" cuando anda. Con ella prendida, la app le pide a la pantalla
  el doble del límite de FPS si lo tiene, así que en una pantalla de 120 Hz también anda con el límite en 60; el
  registro dice por qué no se hacen imágenes y cuándo la pantalla cambia su frecuencia.
- **Registros más completos y Compartir registro**, para los problemas que no podemos reproducir: el registro ahora dice
  mucho más del teléfono (versión de Android, procesador, memoria, almacenamiento), de la GPU y su driver, de tus opciones
  y cada cambio, de dónde estás en el juego, de cuando la app pasa a segundo plano, de los avisos de calor y de memoria, y
  los tiempos de los cuadros de cada minuto. El registro de la sesión anterior, que hasta ahora salía vacío, se guarda,
  dos sesiones hacia atrás. **Compartir registro** (engranaje, **Sistema**, **Tu partida**, **Compartir registro**) manda
  las últimas tres sesiones, tus opciones (nombres, claves y contraseñas ocultos), tus mods, los nombres y tamaños de los
  archivos de la carpeta de la app, el registro del armado y los avisos de Android sobre la app, comprimido cuando es
  grande. Nunca incluye tus cuentas, el nombre de tu Wi-Fi, tu ubicación ni tu partida.
- **La misma partida en tu PC y en tu teléfono:** el README ahora explica, cerca del principio, cómo llevar tu partida con
  Google Drive: entre teléfonos sola, y a un emulador en la PC a mano.
- **El cartel de créditos del arranque se cierra al tocar la pantalla** (Discord): sigue apareciendo en cada inicio,
  pero un toque lo desvanece.

- **Hubs: en línea sin abrir puertos** (pedidos desde que los tiene el MHGU): engranaje, **Multijugador**, **Crear un
  hub** y **Entrar a un hub**. Un servidor de hubs lista los hubs y lleva a sus jugadores, así que nadie escribe una
  dirección ni abre un puerto en el router. El servidor predeterminado es el de la comunidad que mantienen jugadores del
  MHGU (no este proyecto): el mismo programa sirve a los dos juegos y mantiene aparte los hubs de cada juego y región (EE. UU. con EE. UU.,
  Europa con Europa; Monster Hunter 4G no tiene). **Servidor de hubs** elige otro, **Comprobar el servidor de hubs** dice
  si responde, y el README explica cómo correr el tuyo. Un hub funciona como una sala: sus cazadores, su chat (el chat
  **Sala**), una contraseña si quieres, y el **Local Play** (juego local) del juego en el Gathering Hall. **Jugar en línea
  con** elige los hubs de esta app, las salas de Azahar y Citra (como en la 1.0) o los dos (como viene; con los dos, un
  hub que crees también recibe a jugadores de Azahar y Citra en la dirección de tu teléfono). Probado en nuestros dos
  teléfonos y una PC en una misma Wi-Fi con un servidor de prueba (dos cazadores en un mismo Gathering Hall: teléfono con
  teléfono, y teléfono con PC en los dos sentidos), y en una PC con el programa del servidor real; todavía no con
  jugadores lejos por internet: cuéntennos cómo les va (el registro tiene una línea por minuto de cada enlace del hub).

Gracias a todos los que mandaron registros, capturas, videos e ideas. El armado en el teléfono es idea de Klark231.

## 1.0

Tu librería de código sigue sirviendo: no hace falta crearla de nuevo.

La 1.0 se probó en una PC y en nuestros teléfonos (Mali-G57, Mali-G52 y Adreno 830), pero no todo en un teléfono: donde una
línea dice "debería", cuéntanos cómo te va en el tuyo (en el Discord o en un issue, con el registro).

**Lo nuevo de la 1.0**

- **Un menú de opciones nuevo:** seis secciones a la izquierda, **Controles**, **Gráficos y audio**, **Juego**,
  **Multijugador**, **Mods y trucos** y **Sistema**, con **Créditos y licencias** debajo. La página de cada sección tiene sus
  opciones principales, y el resto está un nivel más abajo (**Controles táctiles**, **Mando**, **Pantalla y HUD**,
  **Avanzado**, **Entrar a una sala**...). La explicación de cada fila está detrás de su **?**. Tus opciones conservan sus
  valores; solo cambiaron de lugar: **Pantalla** ahora es **Gráficos y audio**, **Pantalla y HUD**; las **Comodidades** están
  en **Juego**; **Opciones del menú** es **Sistema** (con **Tu partida** para las carpetas, **Importar partida**,
  **Exportar partida** y **Compartir registro**).
- **Crear el código del juego en el teléfono, sin PC:** en la pantalla de la carpeta del juego, **Crear en este teléfono**
  cuando falta la librería o es de otra versión, y después en el engranaje, **Sistema**, **Código del juego**, **Crear el
  código del juego**. Incluye el mod de código de tu carpeta `mods` (u otro, o ninguno), descarga sus herramientas la
  primera vez (unos 100 MB: Python desde python.org y Zig desde PyPI; nunca nada del juego) y sigue en segundo plano, más
  despacio cuando el teléfono está caliente y en pausa cuando está muy caliente o con poca batería; si se corta, sigue desde
  donde quedó: **Seguir** vuelve directo a compilar los archivos que faltan, sin volver a analizar ni traducir el código
  del juego (en menos de un segundo en una RG406V; en un Galaxy S25 Ultra caliente, 71 veces en 0-2 s, donde traducirlo
  todo tarda unos 70 s). Una entrada rota en la caché del compilador ahora se vacía y se vuelve a compilar en vez de
  frenar el armado (visto y arreglado en el Galaxy). Una librería de un armado que se cortó puede ser distinta byte a byte
  de una hecha de una vez, en una función muy grande (un límite del compilador), y anda igual. Dentro de la app un armado
  entero tardó 32 minutos en una RG406V (Mali-G57; de nuevo con la APK de esta versión) y unos 92 minutos en un Galaxy S25
  Ultra que paró 72 veces por calor;
  los dos crearon la misma librería, y con ella el juego anda en Val Habar a 60 FPS. En un teléfono de gama baja debería
  tardar unas 2 horas. Idea de Klark231.
- **Un modo de pantalla única propio:** los menús de la pantalla táctil dibujados en la de arriba por el propio juego, en
  todos los idiomas, español incluido: el menú de título, los mostradores y listas de misiones, la cantina, las tiendas, los
  menús de los Felyne, las páginas de ayuda y las preguntas de sí o no. Activado por defecto, también al actualizar
  (engranaje, **Gráficos y audio**, **Pantalla y HUD**); con el juego de EE. UU. la pantalla táctil arranca oculta y su
  botón la vuelve a mostrar para el mapa del pueblo, el mapa del mundo, el teclado y la creación del personaje. Queda
  apagado mientras haya un mod de código activo (el mod de jM5557, Heavenfall). Basado en el modo de una pantalla de
  Heavenfall, de popzol y BlitzWOLF, sin sus cambios al juego. Probado en inglés y en español en una RG406V y un Galaxy
  S25 Ultra (título, partidas, mostrador y listas de misiones, menú START, bolsa, caja de objetos); la cantina y la
  herrería todavía no se probaron en un teléfono.
- **Perfiles de controles:** 10 espacios para tus diseños táctiles, cada uno con todo: botones, HUD, el lugar del contador
  de FPS, los botones que quedan apretados y las imágenes. Cada uno es el diseño propio del port o uno **de emulador** (A,
  B, X e Y en rombo, la cruceta, los gatillos, START y SELECT, cada uno aprieta solo su botón). En el editor, **Perfil**, o
  engranaje, **Controles**, **Perfiles de controles**. Tu diseño de antes es el perfil 1, sin cambios. Diseño de tabarong.
- **Botones que quedan apretados** (**Mantener** en el editor), **tus propias imágenes en los botones** (**Imagen** en el
  editor, o **Controles**, **Controles táctiles**, **Imágenes de los botones**), el **contador de FPS** arrastrado a
  cualquier lugar en el editor, y una **palanca flotante**, desactivada por defecto (**Controles**, **Controles
  táctiles**). Probado en el port de MHGU en un teléfono; aquí debería funcionar igual: avísanos si algo no anda bien.
- **Chat y amigos:** engranaje, **Multijugador**, **Chat** y **Amigos**. El chat **Sala** es el propio de la sala (también lo
  leen los jugadores de Azahar); **General** (MH4U) y **Global** (los dos ports) y los mensajes privados con amigos pasan por
  relés públicos de Nostr, solo después de que actives el chat en línea, sin cuenta ni servidor nuestro: eres un código de
  amigo creado en tu teléfono. Silenciar y bloquear son solo tuyos, y hay un límite contra el spam. Los dos chats empiezan
  apagados: hasta que actives **Chat de la sala** o **Chat en línea**, arriba en **Chat**, no hay botón del chat ni nada del
  chat sobre el juego, y los mensajes propios de la sala se ven como antes (**Mensajes de la sala sobre el juego**). Con un
  chat activo, un botón de chat sobre el juego muestra las líneas nuevas. Probado entre un teléfono y una PC; entre dos
  teléfonos debería funcionar igual.
  Diseño de tabarong.
- **Contraseñas de sala:** en **Entrar a una sala**, **Contraseña**, **Generar una contraseña** crea una, y **Compartir esta
  sala en el chat** le manda la sala a un amigo, que entra con un toque. Al entrar a una sala con el chat de la sala activo,
  un cartel te recuerda ir al **Local Play** del Gathering Hall.
- **Juego cruzado con Azahar y Citra,** dicho claro: las salas son las mismas, todos necesitan la actualización 1.3.0, y un
  cazador de la sala que juega otro juego aparece marcado (no puede llegar a tu Gathering Hall). Mira **Multijugador**,
  **Jugar con emuladores**.
- **Efectos:** engranaje, **Gráficos y audio**, **Efectos** apagado deja afuera los efectos de partículas del juego (golpes,
  chispas, fuego, alientos, polvo, lluvia), al momento: más rápido donde hay muchos, pero también desaparecen los efectos que
  avisan de un ataque.
- **Packs de texturas HD:** los packs hechos para Azahar y Citra cargan desde una carpeta `textures` en la carpeta de tu
  juego (texturas PNG). Cada textura ocupa memoria del teléfono: un pack grande puede cerrar el juego en un teléfono con
  poca RAM. Probado con un pack de prueba en la PC; un pack real debería cargar igual.
- **Borrar la caché de shaders** (engranaje, **Gráficos y audio**, **Avanzado**): para una imagen que muestra algo mal
  después de actualizar el driver de la GPU, el próximo inicio los vuelve a compilar, y un cartel chico dice cuánto falta.
- **PowerVR:** cuando la app tiene que pasar a su forma más lenta y segura de mostrar las pantallas, un cartel dice por qué
  bajaron los cuadros por segundo.
- **Resolución 3D:** su ayuda explica por qué no hay 55 % ni tamaño exacto (la imagen solo puede ser un número entero de
  veces la pantalla de la consola) y que el HUD es tan nítido como esa resolución.
- **Un teclado y un ratón en el teléfono:** mientras los dos están conectados, **Controles**, **Teclado y ratón**,
  **Asignación de teclas** cambia cada tecla, y un clic izquierdo en la imagen del juego captura el ratón para girar la
  cámara (Escape lo suelta); un clic en la pantalla táctil o en un control táctil sigue siendo un toque ahí. Probado en una
  RG406V con un teclado y un ratón virtuales; un ratón real en una misión debería andar igual.
- **Arcos y ballestas con el fijado:** con el fijado activado, tensar el arco y disparar la ballesta giran al cazador hacia
  el monstruo que sigue, como en el port de MHGU. Debería ayudar; todavía sin probar en una cacería con un monstruo.
- **Se acabó el doble toque en la cámara:** uno sin querer en plena pelea la giraba. El botón de la cámara (L) la sigue
  centrando.
- **Menús:** dos opciones en **Botones en la pantalla**: **L y R en los menús** (las pestañas del pad de menú, activadas) y
  **Cruceta en los menús** (desactivada: el pad de menú ocupa su lugar, como antes).
- **Mandos:** un mando que manda A, B, X e Y sin que Android los liste se vuelve a agregar con su primer botón, así el juego
  lo lee, como en el port de MHGU; y el registro lista todos los dispositivos de entrada.
- **Enviar todo a la caja:** al terminar una misión, la opción del juego de enviar a la caja de objetos manda todas las
  recompensas de una vez; lo que no entra queda en la lista. Apagado por defecto: engranaje, **Juego**, **Enviar todo a la
  caja**. Probado en una misión de rango bajo en una RG406V. Idea de Alfonso.
- **Importar DLC y misiones de evento** (engranaje, **Mods y trucos**, **DLC y misiones de evento**): trae las misiones de
  evento que ya tienes, de un emulador o de una copia de tu consola. La app no incluye ninguna: las traes tú, bajo tu
  responsabilidad.
- **Acceso directo en el inicio** (engranaje, **Sistema**): el juego en tu pantalla de inicio con la imagen y el nombre que
  elijas.
- **Verificar archivos del juego** (engranaje, **Sistema**, **Tu partida**): lee el juego y su actualización enteros y dice
  claro qué falta, qué está cortado, qué cambió o es de otra versión o región (USA 1.3.0, Europa 1.1.0, el juego base solo,
  MH4G), si la copia parece modificada (un repack), si la librería del código y su `.id` corresponden a esta versión y son
  del mismo armado, qué mods de código están activos o chocan, si la partida tiene sus datos extra, y qué hacer. Corre de
  fondo con su progreso y **Cancelar**, y la lista completa va al registro para quien te ayude. No cambia ni descarga
  nada: con la app solo van sumas de control.
- **Mods:** la página **Mods** nombra los mods de código que no van juntos; la pantalla de error dice claro cuando una
  librería viene de otra herramienta, de dos armados distintos (`.so` e `.id`) o de otro armado de un mod; se usa el
  `exheader.bin` propio de un mod de código. Los archivos y carpetas escritos con otras mayúsculas (`Data` por `data`) ahora
  reemplazan los del juego, y **Agregar un mod** encuentra el mod más adentro de un zip (también con la forma
  `luma/titles` de Luma). La carpeta `mods` de la carpeta del juego se busca igual de adentro: un mod que solo se encuentra
  así entra desactivado, y la página **Mods** lo dice; actívalo ahí.
- **Heavenfall** debería poder crearse ahora como un mod opcional que consigues tú, por su ruta de la actualización 1.3.0 y
  por la ruta 1.0 de su instalador web. Solo se probó con un sustituto hecho por nosotros, nunca con el mod real:
  cuéntanos. Su armadura de capas viene con él. Exporta tu partida antes de probarlo.
- **Trucos como los escriben las listas:** tabulaciones, códigos escritos juntos, comentarios después de los códigos, notas
  de CTRPF y archivos con marca UTF-8 ahora cargan. Un truco que escribe en el código del juego se puede convertir en un mod
  de código (**Trucos**, selecciónalo, **Hacer un mod de código**), que se incluye en tu librería.
- **El juego europeo (1.1.0)** debería funcionar también, con sus propios mods y trucos (no tenemos una copia europea para
  probarlo). **MH4G** se reconoce pero todavía no se puede usar.

Gracias a tabarong por los diseños de los perfiles de controles, el chat y las contraseñas de sala, a Klark231 por la idea
de crear el código del juego en el teléfono, a popzol y BlitzWOLF por el modo de una pantalla de Heavenfall, la base del
nuestro, y a Alfonso por mandar todas las recompensas de una vez.

**Arreglos y más**

- **Munición con L mantenida:** un paso de la munición o de la capa con **L** mantenida debería volver a quedarse con la
  munición que elegiste (volvía a la anterior; visto en un Moto g06 con una ballesta ligera, Discord).
- **Fijar objetivo en otra zona:** se queda activado sin un monstruo que seguir (otra zona, un carro al campamento) y vuelve
  a seguir apenas aparece uno, como en el port de MHGU; el botón, el clic de la palanca derecha de un mando y el final de la
  misión lo siguen apagando (visto en un Moto g06).
- **Un botón propio para fijar objetivo:** en una misión, un toque en el botón nuevo **Fijar objetivo** (a la izquierda
  del botón de la cámara, L) hace que la cámara siga girando hacia el monstruo hasta que lo vuelvas a tocar, como en el
  port de MHGU. No aprieta ningún botón del juego, así que mantener **L** vuelve a funcionar como la **L** del juego, por
  ejemplo **L** + **X** / **B** para cambiar la munición de una ballesta (Discord). Se mueve en el editor del diseño, o se
  oculta: engranaje, **Controles**, **Botones en la pantalla**, **Botón de fijar objetivo**. Con un mando se sigue
  fijando con el clic de la palanca derecha.
- **Teclado:** jugando con la pantalla táctil, el teclado del menú se abre con el teclado de tu teléfono ya visible, como
  en el port de MHGU.
- **Samsung DeX** ([#40](https://github.com/khorne-hub/4u-native/issues/40)): al maximizar la ventana, el juego se
  reiniciaba una y otra vez. Ahora la ventana puede cambiar de tamaño sin reiniciar, lo que debería arreglarlo (no
  tenemos un equipo con DeX para probarlo: cuéntanos).
- **Adreno 610 trabado en 0 FPS** ([#47](https://github.com/khorne-hub/4u-native/issues/47)): las GPU Adreno con drivers
  Vulkan 1.1 más viejos vuelven a mostrar la imagen como en la 0.1.9 (la 0.1.9.1 lo había cambiado para ellas). Y si el
  driver de la GPU del teléfono deja de responder, el juego ya no se congela: se cierra con la pantalla de error, desde
  donde puedes compartir el registro.
- **Rayas de colores en un Adreno 619** (Moto G34, Discord): vuelve el mínimo de 3x de la resolución 3D para esa GPU con
  su driver del sistema, lo que debería quitarlas. **Corrección de Adreno** lo desactiva.
- **PowerVR** ([#48](https://github.com/khorne-hub/4u-native/issues/48), Discord): los shaders que ya viste se vuelven a
  armar en segundo plano al empezar, de a uno, como en el port de MHGU, así que los tirones cortos en cada efecto nuevo
  deberían ser mucho menos frecuentes después de tu primera sesión. Un PowerVR BXM que se cerraba en cada arranque
  debería arrancar ahora. Los PowerVR más viejos que mostraban pantalla negra con sonido ahora dibujan el 3D a lo máximo
  que permite su GPU: la fila de la resolución 3D lo dice, por ejemplo "100 % (5x) · 4x".
- **Sesiones largas en un PowerVR BXM** ([#31](https://github.com/khorne-hub/4u-native/issues/31)): `log.txt` ahora dice
  qué es lo que crece hasta que el juego se cierra, y cerca del límite del sistema la app pasa a una forma más lenta pero
  más segura de mostrar las pantallas. Puede ayudar; un registro de una sesión larga nos dirá más.
- **HUD en los bordes de la pantalla:** los medidores de las armas debajo del filo (los frascos de la Charge Blade, los
  de las otras armas), el Frenesí y la munición de una ballesta ahora se mueven con el resto del HUD (Discord).
- **Más detalle en el registro:** después de un cierre dentro del driver de la GPU, el nombre del hilo y las líneas de
  error del propio sistema (Discord); la línea de rendimiento de cada minuto ahora trae el estado de temperatura del
  teléfono y la velocidad del procesador, para saber si un bajón viene del calor del teléfono
  ([#50](https://github.com/khorne-hub/4u-native/issues/50), Discord); y una imagen equivocada durante un solo cuadro
  deja líneas en el registro (Discord).
- **La herramienta de la librería en la PC:** su ventana ya no se cierra enseguida. Avisa si la descarga no se
  descomprimió antes, y siempre espera para que puedas leer sus últimas líneas (Discord).
- **Títulos de las ventanas para elegir:** el título entra entero en una pantalla 4:3, con su rombo y su **?**: se achica
  si hace falta, en vez de cortarse.
- **El engranaje y los menús del juego:** con el modo de pantalla única (o el de un mod de código) en una pantalla 4:3, el
  engranaje de opciones espera en la esquina de abajo a la izquierda mientras un menú del juego está en la pantalla de
  arriba, en vez de taparlo; un engranaje que moviste queda donde lo pusiste.
- **Los botones táctiles y los menús del juego:** con el modo de pantalla única (o el de un mod de código), mientras un menú
  del juego está en la pantalla de arriba (la caja de objetos, la bolsa, una tienda), un botón táctil con la mitad de su
  imagen o más encima del menú se dibuja tenue para que el menú se lea entero; en un teléfono 20:9 los botones de la derecha tapaban la última
  columna de la caja de objetos. Siguen funcionando igual y nada se mueve.
- **Importar partida desde la PC** (Discord): una partida de Azahar, Citra, Checkpoint o JKSM entra desde la carpeta que
  elijas: la carpeta entera del emulador, su `sdmc`, la carpeta `data` o la del título de la partida, o la carpeta de la
  herramienta de copias. Una partida europea iba a donde el juego de EE. UU. nunca mira, así que tu cazador no aparecía:
  ahora una partida de la otra región (o del MH4G japonés) se rechaza con un mensaje que nombra los dos juegos, igual que
  un archivo de cazador cortado o uno sin su archivo `system` cuando todavía no tienes partida. Las tarjetas de gremio,
  misiones de evento y Felyne que están junto a la partida de un emulador también vienen (antes nunca venían). Después
  del reinicio un aviso dice qué se trajo, y **Compartir registro** incluye la última importación. No pudimos reproducir
  la pantalla negra después de importar: si te sigue pasando, mándanos el registro.
- **README:** la instalación en resumen, la herramienta desde el **Source code (zip)** de la misma versión que tu APK,
  cómo pasarte desde una copia de otra página sin perder tu partida, más filas en *Si algo sale mal*, el botón Fijar
  objetivo, y para la 1.0: crear el código del juego en el teléfono, el modo de pantalla única, jugar en línea paso a paso,
  chat y amigos, juego cruzado, importar las misiones de evento, tener tu partida en la nube con una app de
  sincronización, los packs HD y los lugares del menú nuevo.

## 0.1.9.2

Tu librería de código sigue sirviendo: no hace falta crearla de nuevo.

- **Botones combinados en un mando:** cada botón combinado puede ir en un botón del mando (L3, R3, SELECT, Guide, Misc,
  las paletas o el panel táctil): engranaje, **Controles**, **Mando**, **Botones combinados**, una fila que se ve con un
  mando conectado. Una sugerencia de un jugador en Discord.
- **Más detalle en el registro para teléfonos lentos** ([#43](https://github.com/khorne-hub/4u-native/issues/43)):
  `log.txt` ahora dice cuánto lleva armada la lista de shaders del juego mientras se arma, y cuánto tardó, y tiene una
  línea de rendimiento por minuto.
- **El mod de una sola pantalla** está hecho para el juego de América en inglés: con el juego en otro idioma sus menús se
  superponen ([#30](https://github.com/khorne-hub/4u-native/issues/30)). Ahora el README lo dice.

## 0.1.9.1

Tu librería de código sigue sirviendo: no hace falta crearla de nuevo.

- **Arreglado el parpadeo con la pantalla táctil oculta** (cuadros claros, oscuros o rotos cada pocos segundos que
  paraban al mostrar la pantalla táctil) en GPU con drivers gráficos más viejos, como Mali-G71 y G72, y en PowerVR: ahí la
  pantalla de arriba llega ahora siempre de la misma forma a la pantalla del teléfono. Las GPU Mali y Adreno con drivers
  recientes no cambian.

## 0.1.9

Tu librería de código sigue sirviendo: no hace falta crearla de nuevo.

- **Correr con la palanca:** empújala más allá de su borde para correr, y levanta el pulgar sobre el círculo del candado,
  encima de ella, para seguir corriendo; volver a tocar la palanca o el círculo lo corta. El círculo se mueve y cambia de
  tamaño en el editor del diseño. Desactivado, un botón **Correr** corre mientras lo mantienes.
- **Arcos y ballestas:** un toque en apuntar deja la mira puesta hasta el próximo toque; mantén y desliza para apuntar.
  **Apuntar con giroscopio** (desactivado por defecto), con su sensibilidad, gira la mira al girar el teléfono.
- **Pad de menú:** en los menús del juego aparece un pad chico bajo el pulgar derecho, con L y R encima, en lugar de la
  cruceta chica de los menús. Las recompensas de la misión y la Caja de recompensas cuentan como menú, y la cruceta de
  cacería se aparta mientras se ve el pad.
- **Fijar objetivo:** se fue el candado junto al ícono de objetivo, que no respondía; un aro dorado alrededor del ícono
  muestra que está fijado.
- **Pantalla negra con la música del menú en algunas GPU Mali viejas:** cuando los gráficos del teléfono rechazan la forma
  habitual de mostrar la imagen, la app ahora pasa sola a otra, y `log.txt` dice más sobre los gráficos del teléfono.
- **Imagen:** las escenas vuelven a verse nítidas; se detectan las texturas que el juego reescribe (se acabaron la ropa con
  el dibujo anterior al crear el personaje y el suelo o el cielo equivocados); las sesiones largas usan menos memoria para
  las texturas, así siguen cargando en teléfonos de 3-4 GB; 30 fps estables en teléfonos que pasan la pantalla a 30 Hz.
- **Otras GPU:** los drivers Vulkan 1.1 más viejos reciben shaders que aceptan; en Adreno el mínimo de 3x es solo para
  drivers anteriores a 1.1.128, y **Corrección de Adreno** lo apaga; los teléfonos de la clase Adreno 610 empiezan con los
  ajustes livianos; PowerVR y otras GPU ya no dibujan sobre una imagen que todavía está en pantalla.
- **Carpeta de partidas** (Opciones del menú): guarda tu partida en una carpeta que una app de sincronización comparta con
  tus otros dispositivos. El MHGU para Android puede compartir la misma carpeta.
- **Importar partida** tiene **Elegir carpeta** y encuentra solo las carpetas de los emuladores. Una importación nunca borra
  tu partida anterior antes de que la nueva esté en su lugar.
- **Comer y beber más rápido:** dos trucos, apagados, como en el port del MHGU. **Comodidades** en Trucos, según los mods
  QOL de jM5557: mantener A para recolectar, moverse corta la pose, B cancela el afilado, recompensas sin tiempo.
- **Jugar con otros:** viene puesto un lobby de la comunidad, los lobbies que escribes se recuerdan (la × quita uno), y la
  primera vez en línea un cartel dice a qué servidor se conecta.
- **Jugando solo,** el juego espera mientras las opciones o el editor del diseño están abiertos.
- **Botones de GitHub y Discord** junto a Guardar: abren la página del proyecto y el Discord de la comunidad (preguntan
  antes).
- **Avisos:** como mucho una vez al día la app busca una versión nueva (**Buscar actualizaciones** lo apaga), y una copia
  que no es la oficial lo avisa.
- Además: correr y tocar la cámara en una aldea ya no abre el mapa del mundo del mod de una sola pantalla (L y R juntos sí);
  un pulgar que cae en la palanca toma la palanca; en la pantalla de error la vuelta a los mods del último inicio funciona
  con un toque, también para un mod de código que la app no puede cargar; el panel del editor se mueve y se achica; opción
  de 240 fps.

## 0.1.8

- **Arreglada una franja de colores equivocados en el borde de arriba** con resoluciones 3D bajas (modo gama baja, 50 %): el
  juego nunca dibuja la primera fila de la pantalla de arriba, que a 1x se veía como una línea entera.

## 0.1.7

- **Arreglada la pantalla negra con "No game folder at /game"** en teléfonos donde Android no le da a la app una carpeta
  externa (por ejemplo un Redmi Note 8): el juego ahora lee su configuración de la misma carpeta donde la guarda la pantalla
  de la carpeta del juego.
- **GPU Adreno con driver Vulkan 1.1** (por ejemplo la Adreno 630 del Galaxy S9): su driver dibuja mal la imagen 3D por
  debajo de 720 líneas (franjas de colores), así que ahí la resolución 3D es de 3x como mínimo.

## 0.1.6

- **GPU PowerVR:** con la 0.1.4 se podía jugar, pero al volver a entrar se cerraba otra vez: el compilador de shaders de la GPU
  también fallaba cuando la app armaba sus shaders guardados en un segundo hilo. Ahora cada shader se arma en un solo hilo la
  primera vez que hace falta.

## 0.1.5

- **Arreglada la página de botones combinados** (engranaje, Controles, botones en pantalla, Botón 1-6): no mostraba nada.

## 0.1.4

- **GPU PowerVR** (por ejemplo la GE8300 del Helio A22): el juego se cerraba a los pocos segundos de iniciar, cuando el
  propio compilador de shaders de la GPU fallaba con el shader más grande de la app. Ahí ya no se arma ese shader; cada
  shader se arma la primera vez que hace falta, así que un efecto nuevo puede dar un tirón una vez. Sin probar en un
  teléfono PowerVR: cuéntanos cómo te va.

## 0.1.3

- **Arreglado el cierre al iniciar que la 0.1.2 todavía tenía en algunos teléfonos con driver gráfico Vulkan 1.1** (por
  ejemplo teléfonos con Mali-G57 y chip Unisoc). Tu librería de código sigue sirviendo.

## 0.1.2

- **Arreglado el cierre justo después de iniciar en teléfonos cuyo driver gráfico es Vulkan 1.1** (por ejemplo algunos
  Adreno 730): la 0.1.1 pasaba la pantalla negra y se cerraba en el primer cuadro. Tu librería de código sigue sirviendo.
- **Arreglado el cierre del juego en el primer inicio en algunos teléfonos con Android 14** (por ejemplo un Moto g55): no
  podía crear sus carpetas de partidas.

## 0.1.1

- **Arreglada la pantalla negra al iniciar en teléfonos con Adreno 7xx** (por ejemplo el Adreno 730 del Snapdragon 8+ Gen
  1): su driver gráfico se rechazaba al iniciar. Tu librería de código sigue sirviendo: no hace falta armarla de nuevo.

## 0.1.0

La primera versión pública: Monster Hunter 4 Ultimate (América, 1.3.0) recompilado a código nativo para Android, con tu
propia copia del juego.

- **Nativo, no emulado:** el código del juego se recompila en tu PC a una librería para tu teléfono, y corre sobre un
  runtime propio, con los gráficos de la 3DS dibujados con Vulkan.
- **La resolución de tu teléfono** por defecto, pantalla ancha igual a tu pantalla (también las consolas 4:3, sin barras) y
  un límite de cuadros por segundo de 30 a 240, con ritmo estable opcional.
- **Modo gama baja** y **modo Tostadora** para teléfonos débiles y cacerías largas con una sola carga; se activa solo la
  primera vez en un teléfono débil.
- **Controles táctiles pensados para teléfonos:** fijar objetivo con el candado (la cámara sigue al monstruo) y el botón de objetivo para cambiar de monstruo, la barra de objetos con toques y deslizamientos sin
  cruceta, un diseño propio para arcos y ballestas, botones combinados, y un editor del diseño que también mueve el HUD del
  juego.
- **Mandos y teclados:** la palanca derecha como cámara, mapeo de botones por mando, START+SELECT para las opciones, el
  teclado completo.
- **Acciones del pueblo sin la pantalla táctil:** Tarjetas del Gremio, Gestos, Amigos y Cazadores de alquiler desde una
  lista (un botón táctil en el pueblo, SELECT en un mando); lo que abren se ve grande en el centro, con un cursor para el
  mando.
- **Menús con tu propio diseño:** los controles no cambian al abrir un menú. Una cruceta chica aparece solo mientras un
  menú la necesita (cajas de objetos, tiendas, el tablón de misiones, el menú de START…), y tus botones bajan a la mitad
  para que el menú se lea. Las dos cosas se pueden mover, cambiar de tamaño o apagar.
- **Flechas de la barra de objetos:** un toque mueve un objeto y mantenerla repite; siguen a la barra donde la pongas.
  Deslizar sigue funcionando.
- **Los toques rápidos siempre cuentan:** un toque en la pantalla táctil se sostiene hasta que el juego lo ve.
- **Créditos y licencias:** los créditos al abrir la app, un agradecimiento que aparece una sola vez, y una página con los
  créditos, el aviso legal y todas las licencias.
- **El mod de una sola pantalla** funciona (se descarga de su autor; va dentro de tu librería).
- Mods con orden de carga y conflictos, trucos en el formato de Azahar, salas de red y salas en la misma Wi-Fi, importar y
  exportar partidas, tu propio driver de GPU en Adreno, contador de FPS con la temperatura, y una pantalla de error que
  comparte el registro.
- Android 9 o superior, de 64 bits.

### Conocido

- El mapa grande de zonas del mod de una sola pantalla (aparece al tomar el Mapa de la caja de suministros) todavía no se
  puede mover.
- En el pueblo, SELECT abre las acciones del pueblo en lugar de "Personalizar paneles" del juego; para recuperarlo, pon
  "Botón de acciones del pueblo" en otro botón o apagado, en Controles.
- Con un perfil de juego de ahorro de energía, algunos teléfonos sostienen unos 105-110 FPS en lugar de 120 en el campo; a
  60 es estable.

### Descargas oficiales

MH4U para Android es gratis. Solo los links que da khorne-hub son oficiales: si la bajaste de otro lado, pagaste por ella o
vino con publicidad, no es la oficial. Compara el SHA-256 del APK con el archivo `.sha256` de la versión.
