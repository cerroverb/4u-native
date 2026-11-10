# MH4U para Android

Juega tu propia copia de Monster Hunter 4 Ultimate en Android, de forma nativa y con controles táctiles pensados para
teléfonos. No es un emulador: el código del juego se recompila de antemano a código nativo para tu dispositivo, y corre
sobre un runtime propio que hace el trabajo de la consola, con los gráficos dibujados con Vulkan.

**Este proyecto no incluye código del juego, archivos del juego, ROMs ni keys. Necesitas tu propia copia del juego.**

[Read this guide in English](README.md)

Habla con otros jugadores y pide ayuda en el [Discord](https://discord.gg/sk5xEjEMwF) de la comunidad.

## Qué ofrece

### Imagen y rendimiento

- **La resolución de tu teléfono:** la imagen 3D se dibuja por defecto a la resolución de tu pantalla (el triple de la de
  la 3DS en una pantalla de 720 líneas). Se baja en el engranaje, **Gráficos y audio**, **Resolución 3D**, hasta la propia
  de la 3DS
- **Pantalla ancha igual a tu pantalla**, desde teléfonos ultra anchos hasta consolas 4:3, sin barras
- **Hasta 240 fps:** el **Límite de cuadros por segundo** va de 30 a 240. El juego mantiene su velocidad; solo se dibujan
  más cuadros. **Ritmo estable**, apagado por defecto, va a la mitad pareja del límite cuando el teléfono no lo sostiene
- **Modo gama baja**, para teléfonos débiles: 60 fps (30 en un teléfono sin un núcleo rápido), la imagen 3D a un tercio de
  la resolución de tu pantalla, y sin filtros de pantalla, antialiasing ni sombras. En un teléfono débil se activa solo la
  primera vez que arranca la app
- **Modo Tostadora**, la tercera opción del modo gama baja, para la batería y un teléfono fresco: 30 fps estables, la
  resolución propia de la 3DS y sin reverberación
- Interruptores propios para el antialiasing, las sombras, cada filtro de pantalla del juego (resplandor, desenfoque de
  movimiento, profundidad de campo, desenfoque radial, reverberación del calor) y la reverberación, y el campo de visión
- Contador de FPS opcional, con la temperatura del teléfono
- **Tu propio driver de GPU** en un Adreno, bajo tu responsabilidad: engranaje, **Gráficos y audio**, **Driver de GPU**. Si
  uno falla, la app vuelve sola al del sistema y lo avisa

### Controles táctiles

- Una palanca de movimiento, la cámara arrastrando en cualquier parte de la pantalla, los ataques como píldora, dos botones
  o una banda en la esquina, y botones para esquivar, cubrirse, envainar, patear, el objetivo y la cámara
- **Correr con la palanca:** empújala más allá de su borde para correr. Desliza el pulgar hacia arriba hasta el círculo
  chico con un candado que hay encima de la palanca, que se ilumina, y levántalo ahí: la carrera se fija y el cazador sigue
  corriendo, y giras con la cámara, hasta que vuelvas a tocar la palanca o el círculo, desenvaines o abras un menú. Para
  correr solo mientras mantienes un botón: engranaje, **Controles**, **Botones en la pantalla**, desactiva **Correr con la
  palanca**, y aparece un botón **Correr**
- **Fijar objetivo:** mantén el botón de la cámara (L)
- **Barra de objetos sin cruceta:** toca la barra de objetos del juego para usar el objeto, deslízala para cambiarlo
- **Arcos y ballestas** tienen su propio diseño: un disparo grande, recarga, apuntar, fuego agachado o capas, y flechas
  para la munición. Un toque en apuntar deja la mira puesta hasta el próximo toque; mantenlo y desliza para apuntar, y el
  deslizamiento gira la mira como arrastrar la vista
- **Apuntar con giroscopio:** engranaje, **Controles**, **Apuntar con giroscopio** (desactivado por defecto) y
  **Sensibilidad del giroscopio**. Mientras un arco o una ballesta apunta, girar el teléfono gira la mira, desde como lo
  tenías al empezar a apuntar, sumado a tu dedo. También sirve un mando con giroscopio. El sensor solo se lee mientras
  apuntas
- Los controles siguen al juego: la cacería, los menús, y los arcos y ballestas tienen cada uno su diseño
- **Menús pensados para tu pulgar:** al abrirse un menú del juego los controles táctiles se atenúan y aparece un pad chico
  bajo el pulgar derecho, con los botones de página L y R encima. La palanca de movimiento y arrastrar en la pantalla
  también mueven el cursor, y A y B quedan donde están. Para ocultarlo: engranaje, **Controles**, **Botones en la
  pantalla**, **Pad de menú**
- **Botones combinados:** hasta seis botones propios que aprietan varios a la vez, como A+X
- **El editor del diseño:** una pulsación larga en el engranaje. Mueve y cambia el tamaño de cada botón, y mueve el HUD del
  propio juego: el reloj, las barras de vida y aguante, el filo, los nombres, el mapa de la zona, la cámara de objetivo y
  la barra de objetos. El pad de menú y el círculo del candado de carrera también se mueven y cambian de tamaño ahí. El
  panel del editor se aparta si arrastras su título, y se achica hasta un 60 %
- El teclado de tu teléfono para el nombre de tu cazador y el chat
- Vibración al tocar, opacidad y velocidad de la cámara ajustables

### Mandos y teclado

- Un mando oculta los controles táctiles, y vuelven cuando se desconecta. **START y SELECT juntos** abren las opciones
  desde cualquier mando
- La palanca derecha gira la cámara, sin la opción Circle Pad Pro del juego. Su clic fija el objetivo
- **Acciones del pueblo sin la pantalla táctil:** Tarjetas del Gremio, Gestos, Amigos y Cazadores de alquiler desde una
  lista, con el botón táctil del pueblo o SELECT (engranaje, **Controles**, **Botón de acciones del pueblo**); lo que abren
  se ve grande en el centro
- Engranaje, **Controles**, **Mando**: los botones frontales por su posición (como en la 3DS) o por sus letras, la
  velocidad y dirección de la cámara, la zona muerta de las palancas. Un mando que el juego lee mal: **Mapear un mando**, y
  aprieta cada botón una vez
- Un teclado: W A S D mueven (Mayús: caminar), I J K L la cámara, U X, O o F A, Espacio B, R Y, Q L, E R, Tab ZL, T ZR,
  Intro START, Retroceso SELECT, las flechas la cruceta

### Y más

- Mods, con orden de carga y conflictos, y trucos en el formato de Azahar: mira [Mods y trucos](#mods-y-trucos)
- Jugar con otros en salas de red, y buscar salas en la misma Wi-Fi: mira [Jugar con otros](#jugar-con-otros)
- Importa tu partida de Azahar, Citra o un respaldo de Checkpoint, y expórtala para ellos
- Guarda tu partida en una carpeta que una app de sincronización comparta con tus otros dispositivos: mira
  [Elegir dónde se guarda tu partida](#elegir-dónde-se-guarda-tu-partida)
- Trucos para **comer y beber más rápido**, y **Comodidades** según los mods QOL de jM5557: mira
  [Mods y trucos](#mods-y-trucos)
- Jugando solo, el juego espera mientras las opciones o el editor del diseño están abiertos
- El juego se pausa cuando sales de la app
- Si el juego se cierra por un error, una pantalla te avisa y te deja compartir el registro
- Un aviso cuando sale una versión nueva, y cuando una copia no es la oficial: mira
  [Descargas oficiales](#descargas-oficiales)

## ¿Qué tan bien anda?

Medido con el contador en pantalla durante el desarrollo, a la resolución propia del teléfono salvo que diga otra cosa:

| Dispositivo | Chip | GPU | RAM | Cuadros por segundo |
|---|---|---|---|---|
| Galaxy S25 Ultra | Snapdragon 8 Elite | Adreno 830 | 12 GB | 120 en la aldea, de 100 a 120 en el campo |
| Anbernic RG406V | Unisoc T820 | Mali-G57 | 8 GB | unos 59 en el campo |
| moto g06 power | MediaTek MT6769 | Mali-G52 MC2 | 4 GB | unos 30; unos 59 con el modo gama baja |

**Si tu teléfono se calienta, baja el límite de cuadros por segundo**, o prueba el modo Tostadora.

## Qué necesitas

- **Un dispositivo Android:** Android 9 o superior, de 64 bits, con Vulkan. Probado con GPU Adreno y Mali.
- **Unos 5 GB libres** en el dispositivo: la carpeta del juego ocupa unos 4 GB.
- **Una PC con Windows, Linux o macOS,** una vez, para crear el código del juego para tu dispositivo: Python 3.10 o superior y
  unos 3 GB libres.
- **Tu propia copia del juego, descifrada:** Monster Hunter 4 Ultimate de América como archivo `.3ds` o `.cci`, y su
  **actualización 1.3.0** como archivo `.cia`.

Este proyecto no explica cómo volcar un juego de tu consola y no ofrece archivos del juego, keys ni enlaces a ellos.

## Instalación

Se hace una sola vez (y el tercer paso de nuevo cuando una versión nueva de la app lo pida).

### Paso 1: Pon tus archivos en una carpeta

Pon el juego y su actualización en una misma carpeta, por ejemplo `C:\MH4U`. Sirve cualquier nombre de archivo.

### Paso 2 (recomendado): El mod de una sola pantalla

La 3DS tiene dos pantallas. La app muestra la de arriba en toda tu pantalla y la táctil en una esquina, que puedes mover,
agrandar, hacer transparente u ocultar (engranaje, **Pantalla**). Para una sola pantalla, con los menús de la táctil
pasados a la de arriba, usa el **mod de una sola pantalla de jM5557** para el juego de América. No es parte de este
proyecto: descárgalo del repositorio de su autor,
[github.com/jM5557/mh4u-single-screen-mod](https://github.com/jM5557/mh4u-single-screen-mod) (el botón verde **Code**,
**Download ZIP**), y descomprímelo dentro de una carpeta `mods` junto al juego:

```
MH4U
├─ el juego (.3ds o .cci)
├─ la actualización (.cia)
└─ mods
   └─ mh4u-single-screen-mod-main
      ├─ 0004000000126300
      └─ 0004000E00126300
```

El mod cambia el código del juego, así que el paso siguiente lo incluye. Con el mod, la app arranca con la pantalla táctil
oculta; el botón de la pantalla táctil, a la izquierda, la vuelve a mostrar.

### Paso 3: Crea el código del juego

La app no trae código del juego, así que creas el tuyo a partir de tu copia. Se hace en tu PC y no envía nada a ningún lado.

**Windows:**

1. Instala [Python](https://www.python.org/downloads/). Marca **Add python.exe to PATH** en el instalador.
2. En esta página, haz clic en el botón verde **Code**, luego en **Download ZIP**, y descomprímelo.
3. Abre la carpeta descomprimida, luego `mh4n`, `tools`, `android`, y haz doble clic en **Build MH4U library for Android.bat**.
4. Elige el `.cia` de tu actualización. El juego que está al lado se encuentra solo; hace que el código ande más rápido.
5. Cuando pregunte por mods que cambian el código del juego, responde **Sí** si hiciste el paso 2 y elige la carpeta `mods`.

**Linux y macOS:** instala Python 3.10 o superior, descarga y descomprime este repositorio como arriba, abre una terminal en
su carpeta y ejecuta, con tus propias rutas:

```
sh mh4n/tools/android/build-android-library.sh ~/MH4U/update.cia --mods ~/MH4U/mods
```

Quita `--mods ...` si no usas el mod de una sola pantalla. Usa clang y lld si están instalados; si no, descarga el
compilador de C de Zig.

La primera vez, el script de Windows descarga su compilador de C (llvm-mingw, unos 180 MB). Después tarda unos minutos.
Cuando diga **Listo**, tu carpeta tiene dos archivos nuevos:

```
MH4U
├─ el juego (.3ds o .cci)
├─ la actualización (.cia)
├─ libmh4u_code_native.so
├─ libmh4u_code_native.id
└─ mods
```

**Estos dos archivos salen de tu juego: guárdalos para ti y no los compartas.** Créalos de nuevo cada vez que agregues,
cambies o quites un mod que cambia el código del juego, y cuando una versión nueva de la app diga que la librería es de otra
versión.

### Paso 4: Instala

1. Copia la carpeta `MH4U` completa a tu dispositivo, por ejemplo con un cable USB. Puede ir en cualquier lugar de la memoria
   o de una tarjeta SD, menos dentro de `Android/data`.
2. En tu dispositivo, descarga el APK desde [Releases](../../releases/latest) y ábrelo. Android puede pedirte permitir la
   instalación de apps desde el navegador.
3. Abre **MH4U**, toca **Dar permiso y elegir carpeta** y activa el acceso a los archivos cuando Android lo pida. El juego
   solo lee esa carpeta: ahí no se copia ni se cambia nada.
4. Busca tu carpeta `MH4U` (aparece con un ✓) y toca **Usar esta carpeta**. Si prefieres el selector de Android, toca
   **Selector de Android**.

La app revisa la carpeta, te dice qué falta si algo está mal y arranca el juego.

Tus partidas y ajustes se guardan aparte, en `Android/data/com.mh4u.native/files`. Para elegir otra carpeta después:
engranaje, **Opciones del menú**, **Carpeta del juego**.

## Opciones

Toca el engranaje en la pantalla para abrir las opciones. Una pulsación larga abre el editor del diseño. Las pestañas son
**Gráficos y audio**, **Pantalla**, **Controles**, **Juego**, **Multijugador**, **Mods**, **Trucos** y **Opciones del menú**.
Cambia lo que quieras y toca **Guardar**; algunas opciones avisan que necesitan reiniciar, y **Guardar y reiniciar** lo hace
por ti.

- **Resolución 3D:** 100 % es la de tu teléfono, tan nítida como la muestra su pantalla; menos es más liviano para el
  teléfono. **3DS (1x)** es la de la consola, para los teléfonos más débiles.
- **Límite de cuadros por segundo:** de 30 a 240. 30 es el de la 3DS, calienta menos el teléfono y ahorra batería; más de
  60 es para teléfonos rápidos con una pantalla así de rápida.
- **Modo gama baja:** **No**, **Sí** o **Tostadora**, como se describe [arriba](#imagen-y-rendimiento). Igual puedes cambiar
  cada una de sus opciones después.
- **Contador de FPS:** los cuadros por segundo y la temperatura del teléfono arriba de la pantalla.

## Importar tu partida

1. Copia al dispositivo la carpeta con los archivos de tu partida (`system`, `user1`...), por ejemplo a `Download`. En Azahar
   o Citra: clic derecho en el juego, **Open Save Data Location**. Desde una 3DS: un respaldo de Checkpoint del juego.
2. En el juego, engranaje, **Opciones del menú**, **Importar partida**. La app busca partidas en el dispositivo (también las
   de una app de Azahar en el mismo dispositivo): toca la que quieras traer. Si no aparece, **Elegir carpeta** y elige la
   carpeta de tu partida (`system`, `user1`...), la carpeta `sdmc` de un emulador o una carpeta hecha con **Exportar
   partida**.

El juego se reinicia con ella. La partida que tenías queda guardada en `Android/data/com.mh4u.native/files/save_backups`,
y nunca se borra antes de que la nueva esté en su lugar.

## Exportar tu partida

Primero guarda en el juego. Después engranaje, **Opciones del menú**, **Exportar partida**, y elige una carpeta: ahí se crea
una carpeta nueva llamada `MH4U save` con la fecha, con la forma que usan Azahar y Citra, con la partida, las tarjetas de
gremio, las misiones de evento y el Felyne.

## Elegir dónde se guarda tu partida

La partida está en la carpeta propia de la app, salvo que elijas otra; por ejemplo, una carpeta que una app de
sincronización (Syncthing, FolderSync y similares) comparta con tus otros dispositivos. Ve al engranaje, **Opciones del
menú**, **Carpeta de partidas**, y elige la carpeta:

- Tu partida, con sus datos extra, va en una carpeta llamada `MH4U save` dentro de la que elijas, así una sola carpeta
  compartida sirve para la de MH4U y la de MHGU para Android (`MHGU save`). Una carpeta que ya tiene una partida, como la
  que hace **Exportar partida**, se usa tal cual.
- Tu partida se copia ahí antes de cambiar nada. Si la carpeta ya tiene una partida, eliges cuál se queda; la otra queda
  guardada en una copia, en `save_backups`, en la carpeta propia de la app.
- El juego se reinicia para usar la carpeta: guarda antes. **Usar la carpeta propia del juego**, en la misma pantalla, vuelve
  atrás.
- En dos dispositivos, caza en uno a la vez y deja que la app de sincronización termine antes de abrir el juego en el otro.

Al arrancar, el juego revisa la carpeta. Si no llega a ella, si la app de sincronización guardó dos versiones de la
partida o si otro dispositivo tiene el juego abierto, te lo dice y te deja elegir qué hacer.

## Mods y trucos

**Mods:** engranaje, **Mods**, **Agregar un mod (.zip)** o **Agregar un mod (carpeta)**. Un mod para el juego de América
tiene una carpeta `romfs`, una `exefs` o las dos; también sirve la forma de los emuladores, con una carpeta
`0004000000126300` adentro. La página lista los mods en orden de carga: toca uno para activarlo o desactivarlo, moverlo o
quitarlo. Si dos mods cambian el mismo archivo, gana el que está más abajo en la lista. Guarda y reinicia el juego: los mods
se cargan al arrancar. También aparecen los mods de la carpeta `mods` que está junto al juego.

Un mod con carpeta `exefs` cambia el código del juego, así que tu librería de código tiene que crearse con él (paso 3, con
ese mod en la carpeta `mods`). La fila **Librería de código** dice si tu librería coincide con los mods de código que están
activos. Si el juego se detiene al arrancar después de cambiar los mods, su pantalla de error ofrece **Mods del último
inicio**.

**Trucos:** engranaje, **Trucos**, **Importar trucos (.txt)**, y elige un `.txt` de códigos para MH4U de América 1.3.0 en el
formato de Azahar y Citra (cada truco es su `[Nombre]` y sus líneas de código). Entran desactivados: activa los que quieras
y guarda. Funcionan al momento. Guarda tu partida antes de probar uno, y úsalos en línea solo si todos en la sala están de
acuerdo.

**Comer y beber más rápido:** la app trae dos trucos, apagados, al final de la lista, como el port del MHGU. **Comer rapido
siempre (beber x1.7, carne x1.8)** da el efecto de la habilidad Comer rápido sin tenerla; **Beber muy rapido (x2.5)** bebe
todavía más rápido (la carne x1.8). Usa uno de los dos. Aceleran la animación, pero no dejan caminar mientras bebes. Al
apagarlos, el efecto dura hasta la próxima misión.

### Comodidades

Debajo de los trucos, **Comodidades** tiene cuatro ideas de los [mods MH4U QOL](https://github.com/jM5557/MH4U-QOL-Cheats) de
jM5557, hechas por la propia app: sin mods y sin hacer otra librería de código. Vienen apagadas hasta que las actives, y
funcionan en el acto:

- **Mantener A para recolectar:** mantén A para tallar, recolectar, minar y atrapar insectos una y otra vez, hasta que lo
  sueltes.
- **Moverse corta la pose:** después de una poción o una ración, moverte, rodar o cualquier otra acción corta la pose. Lo
  que tomaste sigue haciendo efecto; si te quedas quieto, la pose se ve entera.
- **B cancela el afilado:** B mientras afilas rueda y deja de afilar.
- **Recompensas sin tiempo:** la pantalla de recompensas no tiene cuenta regresiva.

La espera después de una misión ya se puede terminar antes: mantén ZL y ZR (engranaje, **Juego**, **Terminar la
espera**), como hace con A y B el *Skip Quest End Timer* de jM5557.

Los otros mods de jM5557 cambian el código del juego, así que van como mods de código: *Full Item Box In Villages And
Hubs* (el cofre completo de la casa, con equipo y apariencia, en los cofres de la aldea y del salón), *Send All Rewards To
Item Box (Hold X+Y)* y la versión *Hide Number* del de las recompensas. Bájalos del
[repositorio de jM5557](https://github.com/jM5557/MH4U-QOL-Cheats), de su carpeta **USA v1.1** (hecha para la actualización
1.3.0), y crea tu librería de código con el que quieras (paso 3, con la carpeta de ese mod, la que tiene `exefs` adentro,
en la carpeta `mods`). La librería lleva un solo mod de código, como Azahar (el mod de una sola pantalla también es uno);
*ALL Cheats* los tiene todos juntos, comodidades incluidas, así que con él deja apagadas las comodidades de la app.

## Jugar con otros

El modo local del juego pasa por una sala de red, del mismo tipo que usa Azahar: jugadores de esta app y de Azahar pueden
compartir una.

1. Engranaje, **Multijugador**: elige un **Apodo**.
2. **Salas públicas** muestra las salas de un lobby de la comunidad, `http://88.198.47.46:5000`, que mantienen jugadores
   (no este proyecto); las de MH4U primero. Todos los de tu grupo tocan la misma sala para entrar (una vacía, así quedan
   solos).
3. O, en la misma Wi-Fi: un jugador toca **Crear una sala**; los demás tocan **Buscar en esta Wi-Fi**, en **Salas en esta
   Wi-Fi**, y entran. Por internet, el anfitrión abre el puerto UDP 24872 de su router hacia el teléfono, o todos entran a
   una sala que alguien mantiene; los demás escriben su **Dirección de la sala** (y la **Contraseña**, si tiene) y tocan
   **Entrar a la sala**.
4. En el juego: World Map, Solo/Multiplayer, **Local Play**. Un jugador crea un Gathering Hall y los demás lo encuentran.

Entra a la sala antes de ir al Gathering Hall. El chat de la sala está en la misma página. **Lobby** elige otro lobby
(**Otro...**) o ninguno (**Ninguno**); los lobbies que escribes y guardas quedan en su lista, el último primero (hasta 24),
y la **×** a su lado lo quita. La primera vez que juegas en línea, un cartel dice a qué servidor se conecta la app: como
cualquier servidor de internet, ve tu dirección IP.

## Samsung: Game Booster

El panel de rendimiento de Samsung solo aparece en las apps que reconoce como juegos, y un APK instalado a mano no cuenta.
Para tenerlo, instala el APK una vez desde una PC con [adb](https://developer.android.com/tools/adb):

```
adb install -r -i com.mh4u.native MH4U-<version>.apk
```

Usa el nombre del APK que descargaste. Es opcional: el juego anda igual sin esto.

## Si algo sale mal

| Problema | Qué hacer |
|---|---|
| `python` no se reconoce | Instala Python de nuevo, marca **Add python.exe to PATH** y vuelve a abrir el script |
| "... está cifrado" o "is encrypted" | El juego y la actualización tienen que ser copias descifradas |
| "this code.bin isn't a version the project knows" | Usa la actualización 1.3.0 del juego de América |
| La app dice que falta el juego o la actualización | Pon los dos en la carpeta que elegiste, uno junto al otro |
| La app dice que falta la librería, o que es de otra versión | Haz el paso 3 de nuevo con la descarga de esta versión, y copia los dos archivos (`.so` e `.id`) a la carpeta |
| El juego se detiene al arrancar después de agregar o quitar un mod | Un mod con carpeta `exefs` necesita la librería creada con él (paso 3). En la pantalla de error, **Mods del último inicio** deja los mods como estaban |
| Anda lento o el teléfono se calienta | Baja el **Límite de cuadros por segundo** o la **Resolución 3D**, o activa el **Modo gama baja**; en los teléfonos más débiles, **Tostadora** |
| El juego se cierra solo | Cierra otras apps: suele ser falta de memoria. Después comparte el registro |
| Pantalla negra al arrancar, con la música del menú sonando (algunas GPU Mali viejas) | Cuando los gráficos del teléfono rechazan la forma habitual de mostrar la imagen, la app ahora pasa sola a otra. Si sigue negra, adjunta a un issue el `log.txt` de `Android/data/com.mh4u.native/files` (lo copia un explorador de archivos o una PC) |

Si sigue fallando, pregunta en el [Discord](https://discord.gg/sk5xEjEMwF) o abre un [issue](../../issues) con el modelo de tu dispositivo y el
registro. Si el juego se cierra por un
error, una pantalla te deja compartirlo; si no, está en el engranaje, **Opciones del menú**, **Compartir registro**. No
adjuntes archivos del juego.

## Compilar la app

Para desarrolladores; los jugadores solo necesitan el APK de Releases. En Windows (Git Bash), Linux o macOS, con el SDK de
Android (plataforma 35) y el NDK 27.0.12077973, CMake 3.25 o superior, Ninja, Python 3.10 o superior y un JDK 17:

```
sh scripts/build_sdl3.sh    # SDL 3.4.18 desde su release, una vez
sh scripts/build_apk.sh     # libmain.so, las librerías de hooks y después Gradle
```

`scripts/android_env.sh` dice dónde se busca cada herramienta. Firma con tu propia clave en `android/keystore.properties` y
`android/mh4u.keystore`; sin ellas sale un APK de depuración. `app_native/` es la app (SDL3 y Dear ImGui), `android/` su
parte de Android, y `mh4n/` el runtime y el recompilador (mira [mh4n/README.md](mh4n/README.md)).

## Legal

Este proyecto no está afiliado, respaldado ni patrocinado por la Gran C ni la Gran N. Monster Hunter es una marca de la Gran
C. 3DS es una marca de la Gran N.

Este repositorio y sus releases no contienen código del juego, datos del juego, ROMs ni keys. Úsalo solo con una copia del
juego que te pertenezca. Los pedidos de archivos del juego, keys o archivos `libmh4u_code_native.so` ya hechos se cerrarán.

Titulares de derechos: abran un issue para ponerse en contacto.

## Créditos

- El recompilador del juego y el runtime nativo vienen de [MH4UltimateDC](https://github.com/CompaLarios18/MH4UltimateDC),
  de CompaLarios18 y sus colaboradores.
- Partes del runtime derivan de [Azahar](https://github.com/azahar-emu/azahar) y Citra.
- Del mismo autor: [MHGU para Android](https://github.com/khorne-hub/gu-native), Monster Hunter Generations Ultimate
  recompilado para Android de la misma forma.
- El mod de una sola pantalla es de [jM5557](https://github.com/jM5557/mh4u-single-screen-mod), y no es parte de este
  proyecto.
- Las **Comodidades** siguen los [mods MH4U QOL](https://github.com/jM5557/MH4U-QOL-Cheats) de jM5557; este proyecto no
  tiene código de ellos.
- Las librerías que usa este proyecto, con sus licencias, están en [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES). Sus licencias
  también están en la app: engranaje, **Créditos y licencias**, **Licencias**.

## Descargas oficiales

MH4U para Android es gratis. Las únicas descargas oficiales son los links que da khorne-hub, empezando por las
[Releases](https://github.com/khorne-hub/4u-native/releases) de este repositorio. Si la bajaste de otro lado, pagaste por
ella o tu copia vino con publicidad, no es la oficial. En la app, el botón de GitHub arriba en el menú de opciones, junto a
**Guardar**, abre esta página en tu navegador, y el botón de Discord a su lado el [Discord](https://discord.gg/sk5xEjEMwF) de la comunidad.

Todos los APK oficiales están firmados con la misma clave. La huella SHA-256 de su certificado es:

```
8D:E8:8D:5C:5C:F8:93:97:84:5A:D8:93:B5:AA:B7:E7:96:64:74:EC:FD:66:DB:75:78:38:C3:2A:13:7C:C4:03
```

Para revisar un APK: `apksigner verify --print-certs MH4U-<versión>.apk` (build-tools del SDK de Android). Una copia
firmada con otra clave tampoco se puede instalar encima de la app oficial. Desde la 0.1.9 la app también lo revisa: una
copia firmada con otra clave lo avisa una vez al arrancar.

La app también te avisa cuando sale una versión nueva. Como mucho una vez al día le pregunta a GitHub cuál es la última
versión de este repositorio (no se envía nada más, ni siquiera el modelo del teléfono), y al abrir el juego te ofrece abrir
su página. Nunca descarga ni instala nada por su cuenta. Engranaje, **Opciones del menú**, **Buscar actualizaciones** lo
apaga.

## Licencia

MH4U para Android es software libre bajo la [Licencia Pública General GNU, versión 3](LICENSE) o (a tu elección) cualquier
versión posterior, como el runtime de MH4UltimateDC sobre el que se construye. Las partes de terceros conservan sus propias
licencias.

El código de khorne-hub tiene [términos adicionales](ADDITIONAL-TERMS), como lo permite la sección 7 de la GPL. Quien
comparta la app, modificada o no, tiene que:

- conservar los créditos del autor: los que la app muestra al abrirse, su página **Créditos y licencias**, y las secciones
  **Créditos** y **Licencia** de este archivo;
- indicar que una versión modificada está modificada, y no presentarla como la oficial;
- dar, junto con la app, su licencia y su código fuente completo, como lo pide la GPL.
