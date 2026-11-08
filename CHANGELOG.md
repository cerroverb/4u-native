# Changelog

[Español más abajo](#español)

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
