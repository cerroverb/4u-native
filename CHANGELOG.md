# Changelog

[Español más abajo](#español)

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
