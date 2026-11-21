# MH4U para Android y PC

Juega tu propia copia de Monster Hunter 4 Ultimate en Android, de forma nativa y con controles táctiles pensados para
teléfonos, y en una PC con Windows con teclado, ratón o mando ([PC (Windows)](#pc-windows)). No es un emulador: el código del juego se recompila de antemano a código nativo para tu dispositivo, y corre
sobre un runtime propio que hace el trabajo de la consola, con los gráficos dibujados con Vulkan.

**Este proyecto no incluye código del juego, archivos del juego, ROMs ni keys. Necesitas tu propia copia del juego.**

[Read this guide in English](README.md)

Habla con otros jugadores y pide ayuda en el [Discord](https://discord.gg/sk5xEjEMwF) de la comunidad.

**¿La misma partida en tu PC y en tu teléfono?** Mira [Jugar en la PC y en el teléfono con la misma partida (Google Drive)](#jugar-en-la-pc-y-en-el-teléfono-con-la-misma-partida-google-drive).

## Jugar en la PC y en el teléfono con la misma partida (Google Drive)

Una sola partida para tu teléfono, tu tablet y un emulador en tu PC (Azahar o Citra), con Google Drive en el medio. La app
no tiene una nube propia ni sincroniza sola: en el teléfono lo hace una app de sincronización gratuita, y en la PC la
partida se copia a mano.

- **Entre teléfonos y tablets, sola:** en el juego, **Carpeta de partidas** pone tu partida en una carpeta del teléfono, y
  una app de sincronización (**FolderSync** o **Autosync for Google Drive**) mantiene esa carpeta igual a una de tu Google
  Drive. Paso a paso: [Tener tu partida en la nube](#tener-tu-partida-en-la-nube-google-drive-y-similares).
- **Con un emulador en la PC, a mano:** instala **Google Drive para computadoras** en la PC: tu Drive aparece en el
  Explorador de archivos, así que la carpeta sincronizada del teléfono está ahí. `MH4U save` tiene la forma de los
  emuladores (una carpeta `sdmc`).
  - **Del teléfono a la PC:** guarda en el teléfono y deja que termine la app de sincronización. Abre la carpeta de usuario del
    emulador, la que tiene `sdmc` adentro (desde el menú **File** del emulador), guarda una copia de su carpeta `sdmc`, y copia
    encima la carpeta `sdmc` de `MH4U save` de tu Drive, reemplazando los archivos. Las tarjetas de gremio, las misiones
    de evento y el Felyne también pasan.
  - **De la PC al teléfono:** en el emulador, clic derecho en el juego, **Open Save Data Location**, y copia esa carpeta a la
    carpeta de Drive que sincroniza el teléfono. En el teléfono, cuando la app de sincronización la haya bajado: engranaje,
    **Sistema**, **Tu partida**, **Importar partida**, **Elegir carpeta**, y elígela.
- Juega en un dispositivo a la vez y guarda antes de cambiar. En el teléfono, la partida que se reemplaza queda en
  `save_backups`. Más: [Importar tu partida](#importar-tu-partida) y [Exportar tu partida](#exportar-tu-partida).

## Qué ofrece

### Imagen y rendimiento

- **La resolución de tu teléfono:** la imagen 3D se dibuja por defecto a la resolución de tu pantalla (el triple de la de
  la 3DS en una pantalla de 720 líneas). Se baja en el engranaje, **Gráficos y audio**, **Resolución 3D**, hasta la propia
  de la 3DS
- **Pantalla ancha igual a tu pantalla**, desde teléfonos ultra anchos hasta consolas 4:3, sin barras
- **De 15 a 240 fps:** el **Límite de cuadros por segundo** va de 15 a 240. El juego mantiene su velocidad; solo se dibujan
  más o menos cuadros. **Ritmo estable**, apagado por defecto, va a la mitad pareja del límite cuando el teléfono no lo sostiene
- **Modo gama baja**, para teléfonos débiles: 60 fps (30 en un teléfono sin un núcleo rápido), la imagen 3D a un tercio de
  la resolución de tu pantalla, y sin filtros de pantalla, antialiasing ni sombras. En un teléfono débil se activa solo la
  primera vez que arranca la app
- **Modo Tostadora**, la tercera opción del modo gama baja, para la batería y un teléfono fresco: 30 fps estables, la
  resolución propia de la 3DS y sin reverberación
- Interruptores propios para el antialiasing, las sombras, cada filtro de pantalla del juego (resplandor, desenfoque de
  movimiento, profundidad de campo, desenfoque radial, reverberación del calor) y la reverberación, y el campo de visión
- Contador de FPS opcional, con la temperatura del teléfono, que el editor del diseño mueve a cualquier lugar
- **Modo de pantalla única:** los menús de la pantalla táctil dibujados en la de arriba por el juego, en todos los
  idiomas: mira el [Paso 2](#paso-2-opcional-una-sola-pantalla)
- Los **Efectos** se pueden apagar para ganar velocidad, y cargan los **packs de texturas HD** hechos para Azahar y
  Citra: mira [Opciones](#opciones) y [Mods y trucos](#mods-y-trucos)
- **Color:** engranaje, **Gráficos y audio**, **Color**: tono, temperatura, tinte, brillo, contraste, saturación y gamma de
  la imagen del juego en las dos pantallas (nunca los controles táctiles ni este menú), todo en 0, sin cambio, por defecto,
  con **Restablecer el color**
- **Tu propio driver de GPU** en cualquier GPU, bajo tu responsabilidad: engranaje, **Gráficos y audio**, **Avanzado**, **Driver de GPU**. Si
  uno falla, la app vuelve sola al del sistema y lo avisa
- **Presets de color:** en la página **Color**, **Preset 1** a **Preset 3** guardan un ajuste de color cada uno
  (**Guardar el color actual aquí**), y **Usar** lo trae de vuelta
- **FSR 1** (engranaje, **Gráficos y audio**, **Filtro de escalado**, apagado por defecto): la pantalla de arriba
  agrandada con bordes más nítidos cuando se muestra más grande que su imagen, con su propia **Nitidez de FSR 1**
- **Escalado rápido (más FPS)** (engranaje, **Gráficos y audio**, apagado por defecto): agranda la imagen del juego de
  forma más simple cuando la resolución 3D no es el tamaño con que la muestra tu pantalla (por ejemplo 2x en una
  pantalla de 720 líneas): más cuadros por segundo en teléfonos débiles. Algunos colores pueden variar apenas
- **Tamaño del HUD** (engranaje, **Gráficos y audio**, **Pantalla y HUD**): todo el HUD más chico, de 60 a 100 %, al
  momento, con las ventanas de diálogo y de sí o no de las misiones
- **Contorno del texto** (engranaje, **Gráficos y audio**, **Pantalla y HUD**, apagado por defecto): un contorno oscuro
  fino alrededor de las letras del juego, para el texto que cuesta leer
- **Sonido en las grabaciones de pantalla** (engranaje, **Gráficos y audio**, **Imagen y sonido**, apagado por defecto):
  para los teléfonos cuya grabación de pantalla no graba el sonido del juego, desde el próximo inicio
- **Aparatos con dos pantallas:** engranaje, **Gráficos y audio**, **Pantalla y HUD**, **Disposición**, **En la segunda
  pantalla** pone la pantalla táctil en la segunda pantalla, y se toca ahí; solo aparece si hay una, y si se va vuelve
  la disposición que tenías. En lado a lado, la pantalla táctil queda fuera del agujero de la cámara o la muesca del
  borde

### Controles táctiles

- Una palanca de movimiento, la cámara arrastrando en cualquier parte de la pantalla, los ataques como píldora, dos botones
  o una banda en la esquina, y botones para esquivar, cubrirse, envainar, patear, el objetivo, fijar objetivo y la cámara
- **Correr con la palanca:** empújala más allá de su borde para correr. Desliza el pulgar hacia arriba hasta el círculo
  chico con un candado que hay encima de la palanca, que se ilumina, y levántalo ahí: la carrera se fija y el cazador sigue
  corriendo, y giras con la cámara, hasta que vuelvas a tocar la palanca o el círculo, desenvaines o abras un menú. Para
  correr solo mientras mantienes un botón: engranaje, **Controles**, **Botones en la pantalla**, desactiva **Correr con la
  palanca**, y aparece un botón **Correr**
- **Fijar objetivo:** en una misión, toca el botón **Fijar objetivo** (a la izquierda del botón de la cámara, L) y la cámara
  sigue girando hacia el monstruo hasta que lo vuelvas a tocar, como en el port de MHGU. Sigue al monstruo que eligió la
  cámara de objetivo, o al grande más cercano; el botón de objetivo cambia el monstruo. Girar la cámara tú mismo solo la
  pausa un momento. El botón es propio del port y no aprieta ningún botón del juego, así que mantener **L** vuelve a ser la
  **L** del juego. Se puede mover en el editor del diseño, ocultar en el engranaje, **Controles**, **Botones en la
  pantalla**, **Botón de fijar objetivo**, o desactivar en **Controles**, **Fijar objetivo**
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
- **Botones combinados:** hasta 15 botones propios que aprietan varios a la vez, como A+X, cada perfil de controles con
  los suyos. Cada uno también puede ir en cualquier botón del mando (**Botón del mando**): A, B, X e Y por su lugar, L,
  R, ZL, ZR, START, SELECT, la cruceta, los clics de las palancas, las paletas o el panel táctil. **Apretar un botón del
  mando** toma también uno que el mapa del mando no nombra, como algunas paletas en Android; ese botón aprieta entonces
  el combinado en vez del suyo
- **El editor del diseño:** una pulsación larga en el engranaje. Mueve y cambia el tamaño de cada botón, y mueve el HUD del
  propio juego: el reloj, las barras de vida y aguante, el filo, los nombres, el mapa de la zona, la cámara de objetivo y
  la barra de objetos. El pad de menú y el círculo del candado de carrera también se mueven y cambian de tamaño ahí. El
  panel del editor se aparta si arrastras su título, y se achica hasta un 60 %
- **Perfiles de controles:** 10 espacios para tus diseños táctiles, cada uno con todo: los botones, el HUD, el lugar del
  contador de FPS, los botones que quedan apretados y las imágenes. Cada uno es el diseño propio del port o uno **de
  emulador** (A, B, X e Y en rombo, la cruceta, los gatillos, START y SELECT, cada uno aprieta solo su botón). En el
  editor, **Perfil**; o engranaje, **Controles**, **Perfiles de controles**. Se guardan en la carpeta `layout profiles`
  de tu carpeta de partidas, y tu diseño de antes es el perfil 1. Diseñado por tabarong
- **Botones que quedan apretados:** en el editor, toca un botón y activa **Mantener**: un toque lo deja apretado, el
  siguiente lo suelta
- **Tus propias imágenes en los botones:** en el editor, toca un control y elige **Imagen** (agrégalas ahí con **Agregar
  imágenes**, o en el engranaje, **Controles**, **Controles táctiles**, **Imágenes de los botones**). Todo control que
  ubica el editor acepta una, la base y el centro de la palanca cada uno la suya (**Para: base / centro**), y tu imagen es
  todo el aspecto del control: solo se dibuja tu PNG, un poco más chico y más oscuro mientras lo aprietas. Lo que agregues
  es tu responsabilidad
- **Exportar e importar un perfil:** engranaje, **Controles**, **Perfiles de controles**, un perfil, **Exportar** lo
  pone en un .zip con sus imágenes y sus botones combinados, e **Importar** trae uno de vuelta (si ya hay una imagen con
  ese nombre, la nueva entra con otro). Dejar un perfil sin guardar ya no lo vacía, y el editor pregunta antes de que
  **Cargar** o **Nuevo** descarten cambios sin guardar
- **Cada parte del HUD** se puede achicar o agrandar (- / + o pellizcando) y ocultar en el editor del diseño;
  **Restablecer** y **Por defecto** la devuelven
- **Botón del mapa:** en una cacería, un botón en el borde izquierdo muestra u oculta el mapa de la pantalla principal
  (engranaje, **Controles**, **Botones en la pantalla**, **Botón del mapa**). Para tener el mapa en la pantalla
  principal, quita el Mapa de tus paneles en Personalizar paneles (el menú de misión)
- **Árbol de armas en la herrería:** cuando la herrería muestra un arma (Forjar o Mejorar), un botón redondo (o el clic
  de la palanca derecha) abre el árbol de esa arma: las que tienes (en la caja o equipada), las anteriores y el paso
  siguiente de cada una como "?". Solo consulta, con los nombres en el idioma del juego. **Botón del árbol de armas**,
  en **Botones en la pantalla**, saca el botón
- **Cámara solo en la mitad derecha** (engranaje, **Controles**, **Controles táctiles**, apagado por defecto): arrastrar
  gira la cámara solo en la mitad derecha de la pantalla, así un toque errado cerca de la palanca no la mueve
- Un toque en el centro de la palanca o en el SELECT de la cruceta de menú, en el editor, lo elige para una imagen
  propia
- **Palanca flotante**, desactivada por defecto: engranaje, **Controles**, **Controles táctiles**, **Palanca flotante**. La
  palanca de movimiento aparece donde apoyas el pulgar cerca de ella, y vuelve a su lugar al levantarlo
- **Ocultar los controles:** mantén un dedo quieto un momento en un lugar vacío (sin botón) y se ocultan todos los
  controles táctiles, el engranaje también; hazlo otra vez en cualquier lugar para que vuelvan. Arrastrar la cámara,
  mantener un botón o un segundo dedo nunca los ocultan. Activado por defecto: engranaje, **Controles**, **Controles
  táctiles**, **Pulsación larga oculta los controles**
- **Deslizar entre botones:** un pulgar que se desliza de un botón a otro deja los dos apretados, como dos dedos (R a A es
  R + A, R a X es R + X, como pide la glaive insecto); al volver al primero, ese solo. Activado por defecto: engranaje,
  **Controles**, **Botones en la pantalla**, **Deslizar entre botones: los dos apretados**
- **El área táctil de la barra de objetos:** en el editor, toca la barra de objetos y usa - / + (o pellizca) para agrandar
  o achicar la zona donde un toque usa el objeto y un deslizamiento gira la barra (la barra en sí es la del juego)
- El teclado de tu teléfono para el nombre de tu cazador y el chat
- Vibración al tocar, opacidad y velocidad de la cámara ajustables, y **Suavizado de la cámara** (engranaje, **Controles**,
  apagado por defecto): el giro de la cámara al arrastrar entra de a poco, y llega entero

### Mandos y teclado

- Un mando oculta los controles táctiles, y vuelven cuando se desconecta. **START y SELECT juntos** abren las opciones
  desde cualquier mando
- La palanca derecha gira la cámara, sin la opción Circle Pad Pro del juego. Su clic fija el objetivo
- **Acciones del pueblo sin la pantalla táctil:** Tarjetas del Gremio, Gestos, Amigos y Cazadores de alquiler desde una
  lista, con el botón táctil del pueblo o SELECT (engranaje, **Controles**, **Mando**, **Botón de acciones del pueblo**); lo
  que abren
  se ve grande en el centro
- Engranaje, **Controles**, **Mando**: los botones frontales por su posición (como en la 3DS) o por sus letras, la
  velocidad y dirección de la cámara, la zona muerta de las palancas. Un mando que el juego lee mal: **Mapear un mando**, y
  aprieta cada botón una vez. Con un mando conectado, una fila **Botones combinados** ahí pone tus botones combinados en
  sus botones
- Un teclado: W A S D mueven (Mayús: caminar), I J K L la cámara, U X, O o F A, Espacio B, R Y, Q L, E R, Tab ZL, T ZR,
  Intro START, Retroceso SELECT, las flechas la cruceta
- **Un teclado y un ratón en el teléfono:** mientras los dos están conectados, engranaje, **Controles**, **Teclado y
  ratón**, **Asignación de teclas** cambia cada tecla (también los botones del ratón) y **Sensibilidad del ratón** la
  velocidad de la cámara. Un clic en el juego captura el ratón para girar la cámara; Escape lo suelta

### Y más

- Mods, con orden de carga y conflictos, packs de texturas HD, y trucos en el formato de Azahar: mira
  [Mods y trucos](#mods-y-trucos)
- Jugar con otros en salas de red, y buscar salas en la misma Wi-Fi: mira [Jugar con otros](#jugar-con-otros)
- **Hubs**, en línea sin escribir direcciones ni abrir puertos, por el mismo servidor de hubs que el MHGU: mira
  [Hubs](#hubs-en-línea-sin-abrir-puertos)
- Importa tu partida de Azahar, Citra o un respaldo de Checkpoint o JKSM, y expórtala para ellos
- Guarda tu partida en una carpeta que una app de sincronización comparta con tus otros dispositivos: mira
  [Elegir dónde se guarda tu partida](#elegir-dónde-se-guarda-tu-partida), y en la nube con una app de sincronización
- Trae las misiones de evento y el DLC que ya tienes: mira [Importar DLC y misiones de evento](#importar-dlc-y-misiones-de-evento)
- **Enviar todo a la caja** al terminar una misión, y un **acceso directo en el inicio** con tu propia imagen: mira
  [Opciones](#opciones)
- **El código del juego creado en el teléfono**, sin PC: mira el [Paso 3](#paso-3-crea-el-código-del-juego)
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

- **Un dispositivo Android:** Android 9 o superior, de 64 bits, con Vulkan. Probado con GPU Adreno y Mali. Un teléfono con
  un Android de 32 bits (muchos teléfonos con Android Go) no puede instalar la app: Android dice algo sobre "ABI". Una
  actualización no puede cambiar eso.
- **Unos 5 GB libres** en el dispositivo: la carpeta del juego ocupa unos 4 GB.
- **Opcional: una PC con Windows, Linux o macOS,** una vez, para crear el código del juego para tu dispositivo: Python
  3.10 o superior y unos 3 GB libres. Sin PC, la app lo crea en el mismo teléfono (Paso 3): entonces necesita alrededor
  de 1 GB más libre mientras trabaja, internet la primera vez, y tiempo.
- **Tu propia copia del juego, descifrada:** Monster Hunter 4 Ultimate de América como archivo `.3ds` o `.cci`, y su
  **actualización 1.3.0** como archivo `.cia`.

Este proyecto no explica cómo volcar un juego de tu consola y no ofrece archivos del juego, keys ni enlaces a ellos.

## Instalación

Se hace una sola vez (y el tercer paso de nuevo cuando una versión nueva de la app lo pida). En resumen:

1. En tu PC, pon tu juego y su actualización en una misma carpeta.
2. Opcional: una sola pantalla. El modo de pantalla única propio de la app viene activado; el mod de jM5557 es otra forma.
3. Crea la librería de código del juego, dos archivos, en esa carpeta: en el teléfono, desde la app, o en una PC con la
   herramienta de la misma versión que tu APK.
4. Copia la carpeta a tu teléfono, instala el APK y elige la carpeta en la app (en el teléfono, primero se elige la
   carpeta y la app ofrece crear ahí la librería).

¿Algo salió mal? La mayoría de los problemas están en [Si algo sale mal](#si-algo-sale-mal), con qué hacer.

### Paso 1: Pon tus archivos en una carpeta

Pon el juego y su actualización en una misma carpeta, por ejemplo `C:\MH4U`. Sirve cualquier nombre de archivo. Usa los de
tu propia copia: los archivos del juego sacados de otros lados suelen venir cifrados, incompletos o de otra región, y
entonces la herramienta o la app se detienen con un error. Cuando el juego abra, engranaje → **Sistema** → **Tu partida** →
**Verificar archivos del juego** lee el juego y su actualización enteros (cerca de un minuto) y te dice qué falta, qué está
cortado, qué cambió o es de otra versión, y qué hacer.

### Paso 2 (opcional): Una sola pantalla

La 3DS tiene dos pantallas. La app muestra la de arriba en toda tu pantalla y la táctil en una esquina, que puedes mover,
agrandar, hacer transparente u ocultar (engranaje, **Gráficos y audio**, **Pantalla y HUD**).

El **modo de pantalla única** (engranaje, **Gráficos y audio**, **Pantalla y HUD**, activado salvo que lo apagues,
también al actualizar) pasa
los menús de la pantalla táctil a la de arriba, en todos los idiomas: el menú de título, los mostradores de misiones y sus
listas, la cantina, las tiendas, los menús de los Felyne, las páginas de ayuda y las preguntas de sí o no. Los dibuja el
propio juego, tan nítidos como el resto de la imagen: no hay nada que instalar ni librería que volver a crear. Con el juego
de EE. UU. la pantalla táctil arranca oculta; su botón (a la izquierda) la vuelve a mostrar para lo que todavía se usa con
el dedo: el mapa del pueblo, el mapa del mundo, el teclado y la creación del personaje. Mientras un menú del juego está en
la pantalla de arriba, un botón táctil con la mitad de su imagen o más encima del menú se dibuja tenue para que el menú
se lea entero (sigue funcionando y no se mueve); el engranaje queda donde está. En una pantalla más ancha que la de
la consola, esos menús se recortan al ancho de la pantalla de la consola, como los recortaba la consola. Las placas de
la pantalla táctil y las preguntas de sí o no van al lado de las cajas, las listas y las fichas en vez de encima. En la
cacería no cambia nada. Está basado en el modo de
una pantalla de [Heavenfall](https://github.com/popzol/4u-heavenfall-mod-beta), de popzol y BlitzWOLF, sin los cambios
que Heavenfall le hace al juego.

El **mod de una sola pantalla de jM5557** sigue funcionando si lo prefieres (mientras haya un mod de código activo, el
modo de pantalla única de la app queda apagado: los dos moverían los mismos menús). Está hecho para el juego de América en
inglés: con el juego en otro idioma sus menús se superponen
([#30](https://github.com/khorne-hub/4u-native/issues/30)). No es parte de este proyecto: descárgalo del repositorio de su autor,
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

La app no trae código del juego, así que creas el tuyo a partir de tu copia, una sola vez. No se envía nada a ningún
lado. Hay dos formas: en el teléfono, sin PC, o en una PC (más rápido).

**En el teléfono.** Copia la carpeta `MH4U` (el juego, la actualización y, si lo usas, `mods`) al teléfono, instala el
APK (Paso 4) y elige la carpeta. La app dice que falta la librería: toca **Crear en este teléfono** y después **Crear**.
Más tarde también está en el engranaje, **Sistema**, **Código del juego**, **Crear el código del juego** (después de
agregar o quitar un mod que cambia el código del juego, por ejemplo).

- Un mod de código de la carpeta `mods`, como el de una sola pantalla, se incluye; **Cambiar el mod de código** elige
  otro o ninguno.
- La primera vez descarga sus herramientas, unos 100 MB: Python desde python.org y Zig, un compilador de C, desde PyPI.
  Las dos son software libre, se comprueban antes de usarse, y nunca se descarga nada del juego.
- Necesita alrededor de 1 GB libre mientras trabaja; al terminar quedan solo las herramientas (unos 200 MB), y **Borrar
  las herramientas** también las quita. Necesita unos 4 GB de memoria (RAM) o más: en un teléfono con menos, cierra antes
  las demás apps.
- Dentro de la app tardó unos 32 minutos en una Anbernic RG406V (gama media, Mali-G57), y unos 92 minutos en un Galaxy
  S25 Ultra que se pausaba por el calor; un teléfono de gama baja debería tardar unas 2 horas. Sigue con la pantalla
  apagada o en otra app, con su avance en una notificación, y si se corta (el teléfono se reinicia, se cierra la app)
  **Seguir** continúa desde donde quedó, directo a los archivos que faltan compilar, sin volver a analizar el código del
  juego. **Empezar de cero**, en la misma pantalla después de una creación cortada, cancelada o fallida, borra lo que dejó
  (sus archivos de trabajo y el registro desde donde sigue), así la próxima vez **Crear** empieza desde el principio;
  pregunta antes, y deja las herramientas descargadas salvo que elijas borrarlas también.
- El teléfono se calienta: déjalo cargando, sin funda si puedes. Caliente, va más despacio; muy caliente, espera hasta que
  se enfría.

Cuando dice que el código del juego está listo, `libmh4u_code_native.so` y `libmh4u_code_native.id` están en tu carpeta,
y **Jugar** abre el juego.

**En una PC:**

**Windows:**

1. Instala [Python](https://www.python.org/downloads/). Marca **Add python.exe to PATH** en el instalador.
2. Baja la herramienta de la misma versión que tu APK: en [Releases](../../releases/latest), en **Assets**, haz clic en
   **Source code (zip)**.
3. Descomprímelo entero (clic derecho en el ZIP, **Extraer todo**). Desde dentro del ZIP la herramienta no encuentra sus
   otros archivos, y su ventana se cierra al instante.
4. Abre la carpeta descomprimida, luego `mh4n`, `tools`, `android`, y haz doble clic en **Build MH4U library for Android.bat**.
5. Elige el `.cia` de tu actualización. El juego que está al lado se encuentra solo; hace que el código ande más rápido.
6. Cuando pregunte por mods que cambian el código del juego, responde **Sí** si hiciste el paso 2 y elige la carpeta `mods`.

**Linux y macOS:** instala Python 3.10 o superior, descarga y descomprime el **Source code (zip)** de la versión como
arriba, abre una terminal en su carpeta y ejecuta, con tus propias rutas:

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
   instalación de apps desde el navegador. Si tenías una copia de la app bajada de otra página, Android no instala esta
   encima: mira [Pasarte a la app oficial](#pasarte-a-la-app-oficial).
3. Abre **MH4U**, toca **Dar permiso y elegir carpeta** y activa el acceso a los archivos cuando Android lo pida. El juego
   solo lee esa carpeta: ahí no se copia ni se cambia nada.
4. Busca tu carpeta `MH4U` (aparece con un ✓) y toca **Usar esta carpeta**. Si prefieres el selector de Android, toca
   **Selector de Android**.

La app revisa la carpeta, te dice qué falta si algo está mal y arranca el juego.

Tus partidas y ajustes se guardan aparte, en `Android/data/com.mh4u.native/files`. Para elegir otra carpeta después:
engranaje, **Sistema**, **Tu partida**, **Carpeta del juego**.

## Actualizar a una versión nueva

Descarga el APK nuevo desde [Releases](../../releases/latest) y ábrelo: se instala encima de la app que tienes. Tus
partidas, ajustes, controles y carpeta del juego se mantienen. Si después la app dice que la librería es de otra versión,
haz de nuevo el [paso 3](#paso-3-crea-el-código-del-juego) con el **Source code (zip)** de la versión nueva, y copia los dos
archivos nuevos a la carpeta de tu teléfono. Las notas de cada versión dicen si la librería sigue sirviendo.

## Pasarte a la app oficial

¿Bajaste la app de otra página (apkvision, por ejemplo)? Esa copia la firmó otra persona. Android solo instala una
actualización firmada igual que la app que tienes, así que el APK oficial no se instala encima. Android dice entonces "No
se instaló la app" o "el paquete entra en conflicto con un paquete existente". Haz esto una vez:

1. Guarda tu partida. En la app que tienes: engranaje, **Sistema**, **Tu partida**, **Exportar partida**, y elige una carpeta,
   como Descargas.
2. Abre los **Ajustes** del teléfono, **Aplicaciones**, **MH4U**, **Almacenamiento**, y toca **Borrar datos** (o **Borrar
   almacenamiento**).
3. Toca **Desinstalar**. Si Android pregunta si quieres conservar los datos de la app, di que no.
4. Instala el APK desde [Releases](../../releases/latest).
5. Ábrelo y elige tu carpeta del juego. Después engranaje, **Sistema**, **Tu partida**, **Importar partida**, **Elegir
   una carpeta**, y elige la carpeta `MH4U save` del paso 1.

Tu carpeta del juego y tu librería de código quedan como están. ¿Sigue diciendo "No se instaló la app"?

- Desinstalaste la copia vieja pero conservaste sus datos: instala de nuevo esa copia vieja y haz los pasos 2 y 3.
- La copia vieja puede estar también en otro espacio del teléfono (Segundo espacio, Apps duales, Carpeta segura o un
  perfil de trabajo): desinstálala también ahí.

El APK oficial está solo en las [Releases](../../releases/latest) de esta página: mira [Descargas oficiales](#descargas-oficiales).

## Opciones

Toca el engranaje en la pantalla para abrir las opciones. Una pulsación larga abre el editor del diseño. Las opciones
tienen seis secciones, en la columna de la izquierda: **Controles**, **Gráficos y audio**, **Juego**, **Multijugador**,
**Mods y trucos** y **Sistema**, con **Créditos y licencias** debajo. La página de una sección tiene sus opciones
principales; una fila con flecha abre una página con el resto (**Pantalla y HUD**, **Avanzado**, **Entrar a una
sala**...). El **?** a la derecha de una fila dice qué hace. Cambia lo que quieras y toca **Guardar**; algunas opciones avisan que necesitan reiniciar, y **Guardar y reiniciar** lo hace
por ti.

- **Resolución 3D:** 100 % es la de tu teléfono, tan nítida como la muestra su pantalla; menos es más liviano para el
  teléfono. **3DS (1x)** es la de la consola, para los teléfonos más débiles. Solo puede ser un número entero de veces la
  pantalla de la consola (400x240: 1x, 2x, 3x...), así que cada porcentaje se lleva al más cercano, que se ve entre
  paréntesis: no hay 55 % ni tamaño exacto como en el port de MHGU. El juego dibuja su HUD y sus textos en la misma
  imagen, así que son tan nítidos como esta resolución.
- **Límite de cuadros por segundo:** de 15 a 240. 30 es el de la 3DS, calienta menos el teléfono y ahorra batería; más de
  60 es para teléfonos rápidos con una pantalla así de rápida. 15, 20 y 25 son para un teléfono que no llega a 30, o para que
  la batería dure lo más posible: 15 y 20 son tan parejos como 30 en una pantalla de 60 Hz, 25 es menos parejo (no divide 60).
- **Modo gama baja:** **No**, **Sí** o **Tostadora**, como se describe [arriba](#imagen-y-rendimiento). Igual puedes cambiar
  cada una de sus opciones después.
- **Efectos:** apagados, no se dibujan los efectos de partículas del juego (golpes, chispas, fuego, alientos, polvo,
  lluvia): más rápido donde hay muchos, pero también desaparecen los efectos que avisan del ataque de un monstruo. Se
  aplica al momento.
- **Contador de FPS** (engranaje, **Gráficos y audio**, **Pantalla y HUD**): los cuadros por segundo y la temperatura del
  teléfono arriba de la pantalla; el editor del diseño lo mueve.
- **Borrar la caché de shaders** (engranaje, **Gráficos y audio**, **Avanzado**): el próximo inicio borra los shaders que
  el juego compiló y guardó (y la caché del driver de la GPU) y los vuelve a compilar todos desde la lista de lo que
  dibujaron las sesiones anteriores, para una imagen que muestra algo mal después de actualizar el driver de la GPU o de
  un cierre. Mientras se compila la lista, un cartel chico dice cuánto falta.
- **Enviar todo a la caja:** al terminar una misión, la opción del juego de enviar a la caja de objetos manda todas las
  recompensas de una vez, como en los juegos más nuevos, y también los objetos del Felyne; lo que no entra en la caja
  queda en la lista. Apagado por
  defecto: engranaje, **Juego**, **Enviar todo a la caja**. Idea de Alfonso.
- **Acceso directo en el inicio:** engranaje, **Sistema**, **Acceso directo en el inicio** pone el juego en tu pantalla de
  inicio con la imagen y el nombre que elijas. El ícono propio de la app queda como está.

## Importar tu partida

1. Copia tu partida al dispositivo, por ejemplo a `Download`. De Azahar o Citra: la carpeta que abre **Open Save Data
   Location** (clic derecho en el juego), o la carpeta de usuario entera del emulador, la que tiene `sdmc` adentro. Desde
   la consola: un respaldo de Checkpoint o JKSM del juego.
2. En el juego, engranaje, **Sistema**, **Tu partida**, **Importar partida**. La app busca partidas en el dispositivo (también las
   de una app de Azahar en el mismo dispositivo): toca la que quieras traer. Si no aparece, **Elegir carpeta** y elige
   cualquiera de estas: la carpeta de tu partida (`system`, `user1`...), su carpeta `data` o la del título, la carpeta
   entera de un emulador o su `sdmc`, la carpeta de la herramienta de respaldos (con sus copias por fecha) o una carpeta
   hecha con **Exportar partida**. Si la carpeta tiene otra cosa (solo los datos extra, una imagen `.sav`, un `.zip`), la
   app dice qué encontró.

La partida tiene que ser del mismo juego que el tuyo: una de la otra región, o del MH4G japonés, se rechaza con un mensaje
que nombra los dos juegos, igual que un archivo de cazador cortado. Las tarjetas de gremio, las misiones de evento y el
Felyne que están junto a la partida de un emulador también vienen. El juego se reinicia con ella, y un aviso dice qué se
trajo (**Compartir registro** lo incluye). La partida que tenías queda guardada en `Android/data/com.mh4u.native/files/save_backups`,
y nunca se borra antes de que la nueva esté en su lugar.

## Exportar tu partida

Primero guarda en el juego. Después engranaje, **Sistema**, **Tu partida**, **Exportar partida**, y elige una carpeta: ahí se crea
una carpeta nueva llamada `MH4U save` con la fecha, con la forma que usan Azahar y Citra, con la partida, las tarjetas de
gremio, las misiones de evento y el Felyne.

## Importar DLC y misiones de evento

Las misiones de evento que descargaste en el juego son archivos de sus datos extra: `quest1` a `quest5`. Puedes traer las
que ya tienes, de tu propia copia: un emulador o una copia de tu consola. La app no incluye ni descarga ninguna: las traes
tú, bajo tu responsabilidad.

1. Copia al dispositivo la carpeta que las tiene, por ejemplo a `Download`:
   - **Azahar o Citra:** la carpeta de usuario del emulador, la que tiene `sdmc` (las misiones están dentro, en
     `extdata/00000000/00001263/user`).
   - **Checkpoint o JKSM en la consola:** la copia de los datos extra (**extdata**) del juego.
   - **Cualquier carpeta** con los archivos `quest1`, `quest2`... Si vienen en un `.zip`, descomprímelo antes.
2. En el juego, engranaje, **Mods y trucos**, **DLC y misiones de evento**, **Importar DLC y misiones de evento**, **Elegir
   carpeta**, y elige esa carpeta. La app la revisa y te dice qué archivos de misiones encontró.
3. Toca **Importar y reiniciar**.

- Solo se copian los archivos de misiones: tus cazadores, tarjetas de gremio y Felyne quedan como están. Los archivos de
  misiones que se reemplazan quedan en `Android/data/com.mh4u.native/files/save_backups`.
- Tienen que ser de la misma región que tu juego (EE. UU.: `00001263`, Europa: `00001261`). Si no lo son, la app te lo dice.
- Tu juego necesita antes sus datos extra: empieza una partida y guarda una vez antes de importarlas.

**El DLC en sí** (los packs de la tienda de DLC del propio juego) es un título aparte de las misiones de evento. Desde la
1.0.1, la misma opción **Importar DLC y misiones de evento** también lo trae, de tu propia copia:

1. Copia al dispositivo la carpeta de usuario del emulador donde lo instalaste (Azahar o Citra: la que tiene `sdmc`), o una
   carpeta con tu propio `.cia` del DLC.
2. Engranaje, **Mods y trucos**, **DLC y misiones de evento**, **Importar DLC y misiones de evento**, **Elegir carpeta**,
   elige esa carpeta, y después **Importar y reiniciar**.

- Tiene que estar descifrado: la app no descifra nada. Si el emulador lo comprimió al instalarlo, apaga esa opción ahí y
  vuelve a instalarlo, o usa el `.cia`. La app te avisa cuando un archivo no sirve, y por qué.
- Tiene que ser de la misma región que tu juego.
- El juego usa el DLC solo en una partida que ya lo había recibido (una que traes de tu consola o de un emulador): el menú
  de DLC del propio juego necesita un servicio en línea que ya no existe, así que una partida que nunca lo recibió no
  puede recibirlo ahora.
- El registro dice qué DLC encontró el juego y si el juego lo pidió (engranaje, **Sistema**, **Tu partida**, **Compartir
  registro**). Esto se probó solo con archivos de prueba: cuéntanos cómo te va con el tuyo.

## Elegir dónde se guarda tu partida

La partida está en la carpeta propia de la app, salvo que elijas otra; por ejemplo, una carpeta que una app de
sincronización (Syncthing, FolderSync y similares) comparta con tus otros dispositivos. Ve al engranaje, **Sistema**, **Tu
partida**, **Carpeta de partidas**, y elige la carpeta:

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

### Tener tu partida en la nube (Google Drive y similares)

La app no tiene una nube propia, y no le hace falta: **Carpeta de partidas** funciona con cualquier app que mantenga una
carpeta del teléfono igual a una carpeta de tu nube. Paso a paso, con Google Drive:

1. Instala una app de sincronización que mantenga igual una carpeta del teléfono y una de Google Drive en los dos sentidos
   (sincronización bidireccional), por ejemplo **FolderSync** o **Autosync for Google Drive**. La app de Google Drive por sí
   sola no sincroniza carpetas del teléfono.
2. Con el administrador de archivos del teléfono, crea una carpeta para tus partidas, por ejemplo `Partidas` en la memoria
   del teléfono (no dentro de `Android/data`).
3. En el juego, guarda. Después ve al engranaje, **Sistema**, **Tu partida**, **Carpeta de partidas**, y elige esa carpeta.
   El juego se reinicia, y tu partida (con las tarjetas de gremio, las misiones de evento y el Felyne) queda en una carpeta
   `MH4U save` dentro de ella.
4. En la app de sincronización, une esa carpeta con una carpeta nueva de tu Google Drive. Elige sincronización
   **bidireccional**, que sincronice cuando cambian los archivos (o cada pocos minutos), y deja apagada toda opción que borre
   o mueva los archivos después de sincronizar. Deja que termine la primera sincronización: `MH4U save` aparece en tu Drive.
5. En tu otro teléfono o tablet: instala esta app y la de sincronización, une la misma carpeta de Google Drive con una
   carpeta de ahí, y espera a que baje `MH4U save`. Después, en el juego, **Carpeta de partidas**, y elige esa carpeta: el
   juego usa la partida que está ahí.
6. Desde entonces, caza en un dispositivo a la vez, y antes de abrir el juego en el otro, fíjate que la app de
   sincronización haya terminado en los dos.

Si alguna vez la app de sincronización guarda dos versiones de la partida, o el otro dispositivo todavía tiene el juego
abierto, el juego te lo dice al arrancar y te deja elegir; la partida que no se queda va a `save_backups`. MHGU para Android
guarda su partida de la misma forma (`MHGU save`), así una sola carpeta sincronizada sirve para las dos.

## Mods y trucos

**Mods:** engranaje, **Mods y trucos**, **Mods**, **Agregar un mod** (su .zip) o **Agregar un mod (carpeta)**. Un mod para el juego de América
tiene una carpeta `romfs`, una `exefs` o las dos; también sirve la forma de los emuladores, con una carpeta
`0004000000126300` adentro. La página lista los mods en orden de carga: toca uno para activarlo o desactivarlo, moverlo o
quitarlo. Si dos mods cambian el mismo archivo, gana el que está más abajo en la lista. Guarda y reinicia el juego: los mods
se cargan al arrancar. También aparecen los mods de la carpeta `mods` que está junto al juego.

Un mod con carpeta `exefs` cambia el código del juego, así que tu librería de código tiene que crearse con él (paso 3, con
ese mod en la carpeta `mods`). La fila **Librería de código** dice si tu librería coincide con los mods de código que están
activos. Si el juego se detiene al arrancar después de cambiar los mods, su pantalla de error ofrece **Mods del último
inicio**.

**Trucos:** engranaje, **Mods y trucos**, **Trucos**, **Agregar trucos**, y elige un `.txt` de códigos para MH4U de América 1.3.0 en el
formato de Azahar y Citra (cada truco es su `[Nombre]` y sus líneas de código). Entran desactivados: activa los que quieras
y guarda. Funcionan al momento. Guarda tu partida antes de probar uno, y úsalos en línea solo si todos en la sala están de
acuerdo. Un truco que escribe en el código del juego no hace nada por sí solo en este port, cuyo código está traducido de
antemano: selecciónalo y elige **Hacer un mod de código**, activa ese mod en **Mods** y vuelve a crear tu librería de código con
él activado (paso 3).

**Comer y beber más rápido:** la app trae dos trucos, apagados, al final de la lista, como el port del MHGU. **Comer rapido
siempre (beber x1.7, carne x1.8)** da el efecto de la habilidad Comer rápido sin tenerla; **Beber muy rapido (x2.5)** bebe
todavía más rápido (la carne x1.8). Usa uno de los dos. Aceleran la animación, pero no dejan caminar mientras bebes. Al
apagarlos, el efecto dura hasta la próxima misión.

### Packs de texturas HD

Los packs de texturas hechos para Azahar y Citra funcionan tal como vienen: pon la carpeta del pack (la que tiene los archivos
`tex1_...png` y, si lo trae, `pack.json`) dentro de una carpeta `textures` en la carpeta de tu juego, junto al `.cci` y a
`mods`. También sirve la forma de Azahar, `textures/0004000000126300/...`. Se usan las texturas PNG y KTX2 (los archivos DDS y
KTX se omiten). Cada textura se lee y se decodifica en segundo plano la primera vez que el juego la dibuja (mientras tanto se ve la del
juego), y cada textura HD ocupa memoria del teléfono (una de 1024x1024 son 4 MB). El pack tiene una parte de la memoria que la
app da a las texturas, según la memoria libre de tu teléfono al abrir el juego (nunca menos que antes): pasada esa parte, algunas texturas quedan como las del juego, y las de las
zonas que dejaste se liberan al rato. Si un pack entero es mucho para tu teléfono, engranaje, **Mods y trucos**, **Packs de
texturas**, **Tamaño de las texturas del pack: Mitad** dibuja las texturas grandes del pack a la mitad de ancho y de alto (un
cuarto de la memoria; se aplica al volver a abrir el juego). El registro dice cuántas texturas salieron del pack y cuánta
memoria ocupan. Esta app no trae ningún pack: consíguelo de su autor.

#### Packs en KTX2

Un pack también puede traer archivos `.ktx2`, con los mismos nombres que sus `.png` (`tex1_..._12.ktx2`); si una textura tiene
los dos, se usa el `.ktx2`, así el pack puede conservar sus PNG para las versiones viejas. Los KTX2 ocupan menos y se leen más
rápido. Se leen: ETC1S y UASTC de Basis Universal, y archivos que ya vienen en RGBA8 o BC7. En el teléfono, por ahora, ocupan la
misma memoria que un PNG (se convierten a RGBA8). Para convertir un pack tú mismo, con tu propia copia y tus herramientas:

- toktx (KTX-Software): `toktx --t2 --encode uastc --zcmp 18 --lower_left_maps_to_s0t0 tex1_..._12.ktx2 tex1_..._12.png`
  (`--encode etc1s` hace archivos más chicos, un poco menos nítidos).
- basisu (Basis Universal): `basisu -uastc -ktx2 tex1_..._12.png` (`-etc1s` para ETC1S). No agregues `-y_flip`: basisu no lo
  anota en el archivo y la textura se vería al revés.

Conserva los nombres: cada textura se encuentra por su nombre. Un archivo que la app no puede leer queda como la textura del
juego, y el registro dice por qué.

### Comodidades

En el engranaje, **Juego**, **Comodidades** tiene cuatro ideas de los [mods MH4U QOL](https://github.com/jM5557/MH4U-QOL-Cheats) de
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
Hubs* (el cofre completo de la casa, con equipo y apariencia, en los cofres de la aldea y del salón) y la versión *Hide
Number* del de las recompensas. Bájalos del
[repositorio de jM5557](https://github.com/jM5557/MH4U-QOL-Cheats), de su carpeta **USA v1.1** (hecha para la actualización
1.3.0), y crea tu librería de código con el que quieras (paso 3, con la carpeta de ese mod, la que tiene `exefs` adentro,
en la carpeta `mods`). La librería lleva un solo mod de código, como Azahar (el mod de una sola pantalla también es uno);
*ALL Cheats* los tiene todos juntos, comodidades incluidas, así que con él deja apagadas las comodidades de la app. Su
*Send All Rewards To Item Box (Hold X+Y)* no hace falta: **Enviar todo a la caja** de la app (engranaje, **Juego**) lo
hace con los pasos propios del juego.

## Jugar con otros

El modo local del juego pasa por una sala de red, del mismo tipo que usa Azahar: jugadores de esta app y de Azahar pueden
compartir una. Desde la 1.0.1, los jugadores de esta app también pueden encontrarse en **hubs**, sin escribir direcciones
ni abrir puertos (abajo).

### Hubs: en línea sin abrir puertos

Como los jugadores de MHGU para Android, pueden encontrarse en **hubs**: un servidor de hubs lista los hubs y lleva a sus
jugadores, así que nadie escribe una dirección ni abre un puerto en el router.

1. Engranaje, **Multijugador**: elige un **Apodo**. **Jugar en línea con** elige qué muestra la sección: **Los hubs de esta
   app**, **Salas de Azahar y Citra** (las salas de abajo, como en la 1.0) o **Los dos** (como viene).
2. Un cazador: **Crear un hub**. Ponle un **Nombre del hub** y, si quieres, una **Descripción** (tu rango, qué cazas, tu
   idioma), una **Contraseña** si solo deben entrar tus amigos, y cuántos **Cazadores** (de 2 a 4). Después, **Crear el
   hub**. Deja la app abierta mientras los demás juegan en él.
3. Los demás: **Entrar a un hub**. Muestra los hubs abiertos ahora para su copia del juego, con sus cazadores y un candado
   si tienen contraseña. Un toque entra a uno.
4. Después, como en una sala: en el juego, **Local Play** (juego local) en el **Gathering Hall**. Un cazador crea una sala
   del gremio y los demás la buscan ahí. El chat **Sala** del chat es el del hub.

Solo juegan juntas copias de la misma región: EE. UU. con EE. UU., Europa con Europa (Monster Hunter 4G no tiene hubs).
Conviene que todos tengan la misma versión de esta app.

**Servidor de hubs** elige el servidor. El predeterminado, `hunterhubid-gu-hub.duckdns.org`, es el servidor de la comunidad
que mantienen jugadores de MHGU (no este proyecto); el mismo servidor mantiene aparte los hubs de cada juego y región.
**Otro...** escribe otro (un nombre o una IP, y `:puerto` si no es el 39510) y **Ninguno** apaga los hubs. **Comprobar el
servidor de hubs** dice si responde, cuánto tarda y cuántos hubs lista. Como cualquier servidor de internet, ve tu dirección
IP; los demás jugadores no.

Con **Jugar en línea con: Los dos**, un hub que crees también recibe a jugadores de Azahar y Citra: entran como a una sala,
con la dirección de tu teléfono y su **Puerto (UDP)** (desde internet, cuando ese puerto se redirige al teléfono).

Un hub es una sala en el teléfono del anfitrión a la que los demás llegan por el servidor de hubs: su tráfico del juego va
por UDP a través del servidor cuando pasa, y si no por la conexión TCP del servidor. Los límites del propio juego para el
juego local siguen: una conexión lenta, o un teléfono que se detiene un momento, puede desconectar a un cazador, como en una
sala.

### Montar un servidor de hubs

El servidor de hubs es el mismo programa que el de MHGU para Android,
[`server/` de gu-native](https://github.com/khorne-hub/gu-native/tree/main/server): un programa chico en Go, sin base de
datos ni archivo de configuración, y un mismo servidor sirve a los dos juegos. Para tener el tuyo, para tu grupo o más
cerca de ti:

```
git clone https://github.com/khorne-hub/gu-native
cd gu-native/server
go build -o mhgurecomp-hubserver .     # Go 1.22 o más nuevo
./mhgurecomp-hubserver -addr :39510
```

Abre el puerto **39510 para TCP y UDP** en el firewall (y redirígelo, detrás de un router). Sus opciones (límites por
dirección, ancho de banda), Docker y systemd están en su README. Después, los jugadores eligen **Servidor de hubs**,
**Otro...**, y su nombre o dirección. El tráfico del juego de un hub pasa por el servidor: ponlo donde los jugadores
tengan buena conexión.

**Juego cruzado con Azahar y Citra:** las salas son las mismas que las salas multijugador de Azahar, Citra y Lime3DS, así
que sus jugadores entran a una sala de esta app y tú a las suyas, de la misma manera (con la dirección, o desde **Salas
públicas**: en ese lobby la mayoría lista sus salas). Todos necesitan MH4U con la actualización 1.3.0: una sala no
distingue otra versión, que no encuentra tu Gathering Hall. **Cazadores en la sala** marca con otro color a quien juega otro
juego (Monster Hunter 4G, o cualquier otro): no pueden encontrarte en el Gathering Hall. Los jugadores de un emulador no
esperan a los demás cuando su juego se traba (solo los jugadores de esta app se mantienen al mismo paso), así que una
trabada allí puede desconectarlos.

### Jugar en línea, paso a paso

1. Engranaje, **Multijugador**: elige un **Apodo**.
2. Entra a una sala, en **Entrar a una sala** de esa página:
   - **Salas públicas** muestra las salas de un lobby de la comunidad, `http://88.198.47.46:5000`, que mantienen jugadores
     (no este proyecto); las de MH4U primero. Todos los de tu grupo tocan la misma sala para entrar (una vacía, así quedan
     solos).
   - O escribe su **Dirección de la sala** (y la **Contraseña**, si tiene) y toca **Entrar a la sala**.
   - O crea una en el teléfono, en **Crear una sala**. En la misma Wi-Fi, los demás tocan **Buscar en esta Wi-Fi**, en **Salas
     en esta Wi-Fi**, y entran. Por internet, el anfitrión abre el puerto UDP 24872 de su router hacia el teléfono, o todos
     entran a una sala que alguien mantiene; los demás escriben su dirección.
3. **Estar en la sala no alcanza.** En el juego, ve al **Gathering Hall** y elige **Local Play** (World Map,
   Solo/Multiplayer, **Local Play**). Un cazador crea un Gathering Hall y los demás lo buscan ahí. Al entrar a una sala, un
   cartel te lo recuerda.

**Una sala solo para tus amigos:** en **Contraseña**, **Generar una contraseña** crea una al azar (32 letras y números) y la
copia. Crea la sala con ella, o dásela a quien la crea, y entra. Después, con el chat en línea activo, **Compartir esta sala
en el chat** manda la dirección y la contraseña de la sala a un amigo en línea, o a General o Global: tu amigo toca **Entrar
a esta sala** en esa línea y entra. Una sala en tu teléfono se alcanza desde internet solo con su puerto redirigido a él.

Entra a la sala antes de ir al Gathering Hall. El chat de la sala es el chat **Sala**: mira [Chat y amigos](#chat-y-amigos).
**Lobby** elige otro lobby (**Otro...**) o ninguno (**Ninguno**); los lobbies que escribes y guardas quedan en su lista, el
último primero (hasta 24), y la **×** a su lado lo quita. La primera vez que juegas en línea, un cartel dice a qué servidor
se conecta la app: como cualquier servidor de internet, ve tu dirección IP.

### Chat y amigos

La app tiene un chat propio: engranaje, **Multijugador**, **Chat** y **Amigos**. Tiene tres chats, y mensajes privados con
tus amigos:

- **Sala:** el chat propio de la sala, el que tienen las salas de Azahar, así que también lo leen los jugadores de un
  emulador que están en ella. No necesita relés ni el chat en línea: activa **Chat de la sala**.
- **General:** todos los del chat en línea que juegan MH4U.
- **Global:** todos los del chat en línea de los dos ports, MHGU y MH4U.

**Los dos chats empiezan apagados.** Hasta que actives uno, en la página **Chat** (debajo de las líneas), no se ve nada del chat sobre el
juego: **Chat de la sala** activa el de la sala (sin relés) y **Chat en línea** Global, General y amigos. Con el chat de la
sala apagado, los mensajes propios de la sala se ven como antes: **Mensajes de la sala sobre el juego** avisa quién entra y
sale, y lo que dice alguien.

La página **Chat** se abre como una ventana de chat sobre el juego, más chica y que deja ver lo de atrás, solo con el
chat: su encabezado tiene una pestaña por cada chat que hay ahora (Sala, General, Global, Amigos), cada una con las líneas que
no leíste (en **Amigos** las flechas pasan de un amigo a otro), el engranaje abre las opciones del chat (los
interruptores, **Amigos**, las líneas sobre el juego, el botón del chat...) y la X la cierra; debajo, el texto para
escribir y **Enviar**, y las líneas, la más nueva primero, cada una sobre una tira oscura para que se lea sobre pasto o
nieve. Arrastra su parte de arriba para moverla y su esquina de abajo a la derecha para agrandarla o achicarla; queda
donde la dejes (opciones del chat: **Ventana del chat a su lugar**). Mientras el teclado del teléfono está abierto, la
ventana sube arriba de todo de la pantalla y usa el alto que deja el teclado: el texto, **Enviar** y las últimas líneas
quedan a la vista. El botón del chat se mueve y se cambia de tamaño en **Personalizar controles y HUD**, como cualquier
botón, y queda guardado con el perfil de controles.

Jugando con el dedo, el juego sigue debajo de la ventana del chat y los controles táctiles funcionan alrededor: solo
los controles de la ventana toman el dedo, y tocar su parte vacía mueve la palanca o la cámara. Mientras escribes,
tocar fuera de la ventana la cierra junto con el teclado. Un menú del juego la esconde hasta que se cierra, el botón
de ajustes va al menú y el botón del chat la cierra. En las opciones del chat, **Fondo de la ventana**, **Fondo de
los mensajes** y **Fondo del campo de escribir** van de 0 a 100 %; el texto queda siempre sólido.
En PC la ventana del chat flota igual, con el ratón: un clic en sus controles es de la ventana, en cualquier otro lado
del juego; las teclas son del juego salvo mientras escribes en ella, y **F1** va al menú.

Un botón redondo del chat aparece sobre el juego mientras el chat en línea está activo, o el chat de la sala está activo y
estás en una sala. Un toque abre el chat, arrastrarlo lo mueve, y el número que tiene cuenta las líneas que no leíste. En
**Chat**, **Opciones**, **Botón del chat** lo apaga y **Tamaño del botón del chat** cambia su tamaño. Las líneas nuevas se
ven 8 segundos a su lado, hasta cuatro, cada chat con su color y el nombre de cada jugador con el suyo (las tuyas no;
se van al abrirse un menú), también las de la sala (con el chat de la sala activo): **Líneas sobre el juego** elige
**Apagado**, **Sala y amigos** (lo predeterminado) o **Todos**. **Mensajes de la sala sobre el juego** sigue avisando quién
entra y sale de la sala.

**El chat en línea** (Global, General y amigos) está apagado hasta que lo activas con **Chat en línea**, en la página
**Chat**. La primera vez, una pregunta lo explica:

- Se conecta a relés públicos de Nostr: `relay.damus.io`, `nos.lol` y `relay.primal.net`, u otros que elijas en **Relés**.
  Como cualquier servidor de internet, ven tu dirección IP.
- Cualquiera puede leer Global y General: nunca compartas nada privado ahí. Los mensajes a un amigo se cifran en el
  dispositivo.
- Este proyecto no tiene servidores y no hay cuentas: eres una clave creada en el dispositivo, que se muestra como tu
  **código de amigo** (`XXXX-XXXX-XXXX`).
- No se guarda nada en ningún lado: las líneas no quedan guardadas, y el chat funciona solo mientras estás conectado.
- Tus amigos, silenciados, bloqueados y opciones del chat se guardan solo en el dispositivo, en `chat.txt`, junto a los
  ajustes de la app. Desinstalar la app los borra y te da un código de amigo nuevo.

**Amigos:** en la página **Amigos**, **Tu código de amigo** muestra el tuyo (un toque lo copia). **Agregar un amigo** con su
código: se lo encuentra apenas los dos están en línea, y le llega una solicitud. Los mensajes privados funcionan cuando te
agrega también, al aceptarla en **Solicitudes**. La fila de un amigo dice si está en línea, en una sala, o cuándo se lo vio
por última vez. Un toque en ella ofrece **Hablar**, **Mandar esta sala** (mientras estás en una), **Silenciar**,
**Bloquear** y **Quitar**.

**Silenciar y bloquear** los decide cada jugador: solo te afectan a ti, en tu dispositivo. **Silenciar** oculta las líneas de
alguien en los chats. **Bloquear** además corta sus mensajes privados, sus solicitudes y su estado en línea, y lo quita de
tus amigos. Usa **Alguien de este chat**, en la página del chat, o la fila del amigo. **Silenciados y bloqueados**, en
**Amigos**, lo deshace.

**Contra el spam:** puedes mandar una línea cada 4 segundos en Global y General (hasta 5 por minuto), y cada segundo en Sala
y con amigos; la misma línea dos veces en un minuto no se manda. Quien manda más de 6 líneas en 20 segundos queda silenciado
en tu dispositivo por 5 minutos. Una línea tiene hasta 200 caracteres.

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
| No sabes si tus archivos del juego están bien | Engranaje → **Sistema** → **Tu partida** → **Verificar archivos del juego**. Lee el juego y su actualización enteros, dice qué está mal (falta, cortado, cambiado, otra versión o región, una copia modificada, la librería del código y su `.id`) y qué hacer; la lista completa va al registro. No cambia ni descarga nada |
| "No se instaló la app", o "el paquete entra en conflicto con un paquete existente" | Tienes una copia de la app bajada de otra página, firmada por otra persona. Guarda tu partida y sigue [Pasarte a la app oficial](#pasarte-a-la-app-oficial) |
| El instalador habla de "ABI", o dice que la app no es compatible | Este teléfono tiene un Android de 32 bits (por ejemplo Android Go). La app necesita uno de 64 bits, así que no se puede instalar en este teléfono |
| La ventana de **Build MH4U library for Android.bat** se abre y se cierra al instante | Descomprime entero el **Source code (zip)** primero (clic derecho, **Extraer todo**) y abre el `.bat` desde la carpeta descomprimida, no desde dentro del ZIP. Si se sigue cerrando: abre esa carpeta `android`, haz clic en la barra de direcciones, escribe `cmd`, presiona Enter, escribe `"Build MH4U library for Android.bat"` y presiona Enter. La ventana queda abierta y muestra qué salió mal |
| `python` no se reconoce | Instala Python de nuevo, marca **Add python.exe to PATH** y vuelve a abrir el script |
| "... está cifrado" o "is encrypted" | El juego y la actualización tienen que ser copias descifradas de tu propio juego |
| "this code.bin isn't a version the project knows" | Usa la actualización 1.3.0 del juego de América |
| La app dice que falta el juego o la actualización | Pon los dos en la carpeta que elegiste, uno junto al otro |
| La app dice que falta la librería, o que es de otra versión | La librería se creó con la herramienta de otra versión. Haz el paso 3 de nuevo con el **Source code (zip)** de la misma versión que tu APK, y copia los dos archivos (`.so` e `.id`) a la carpeta |
| El juego se detiene al arrancar, y el registro dice que la librería "was built against a different recomp.h" | Lo mismo que arriba: la librería viene de otra herramienta o de otra versión. Créala de nuevo con la herramienta de esta versión (paso 3) y copia los dos archivos |
| El juego se detiene al arrancar después de agregar o quitar un mod | Un mod con carpeta `exefs` necesita la librería creada con él (paso 3). En la pantalla de error, **Mods del último inicio** deja los mods como estaban |
| Un mod grande hecho para emuladores no funciona | Los mods son obra de sus autores, y algunos cambian el juego de formas que la herramienta de este port todavía no puede crear. Heavenfall debería poder crearse con la herramienta de esta versión (exporta antes tu partida), pero no se probó con el mod real: si falla, cuéntanos con el registro. Saca el mod de `mods`, crea la librería de nuevo sin él (paso 3) y copia los dos archivos |
| Zonas negras o partes que faltan, con archivos del juego que no sacaste tú | Los archivos del juego de otros lados suelen venir incompletos o dañados, y la app no puede arreglarlos. Usa tu propia copia, como dice el [paso 1](#paso-1-pon-tus-archivos-en-una-carpeta) |
| Anda lento o el teléfono se calienta | Baja el **Límite de cuadros por segundo** o la **Resolución 3D**, o activa el **Modo gama baja**; en los teléfonos más débiles, **Tostadora** |
| El juego se cierra solo | Cierra otras apps: suele ser falta de memoria. Después comparte el registro |
| Pantalla negra al arrancar, con la música del menú sonando (algunas GPU Mali viejas) | Cuando los gráficos del teléfono rechazan la forma habitual de mostrar la imagen, la app ahora pasa sola a otra. Si sigue negra, adjunta a un issue el `log.txt` de `Android/data/com.mh4u.native/files` (lo copia un explorador de archivos o una PC) |

En una PC, mira [Si algo falla en PC](#si-algo-falla-en-pc).

Si sigue fallando, pregunta en el [Discord](https://discord.gg/sk5xEjEMwF) o abre un [issue](../../issues) con el modelo de tu dispositivo, la
versión de la app y el registro. Si el juego se cierra por un
error, una pantalla te deja compartirlo; si no, está en el engranaje, **Sistema**, **Tu partida**, **Compartir registro**. No
adjuntes archivos del juego.

## PC (Windows)

La misma carpeta del juego se juega en una PC con Windows: `MH4U.exe` corre tu juego de forma nativa, con el mismo
runtime, los mismos ajustes y las mismas funciones que la app, y lo que agrega una PC: teclado y ratón, una ventana de
cualquier tamaño, muchos cuadros por segundo y Discord.

La descarga trae solo el programa: el código del juego (`mh4u_code_native.dll` y `mh4u_code_native.id`) se arma una vez
con tu propia copia, en el mismo programa: ábrelo, elige tu carpeta del juego y toca **Crear el código del juego en esta
PC** ([paso 3 en PC](#paso-3-en-pc-crear-el-código-del-juego)). No hace falta nada más: ni Python ni un compilador propios.

### Qué hace en PC

- **Teclado y ratón:** WASD mueve, el ratón gira la cámara, el clic izquierdo ataca (X) y F es el botón de acción (A):
  habla, abre, recolecta y recoge. El clic derecho también es A, pero solo con el arma afuera, en un menú o una charla, o
  fuera del pueblo y de las misiones: en el pueblo nunca habla, recolecta ni talla por error. Un toque de Alt fija la
  cámara en el monstruo. Cada tecla se puede cambiar, hasta tres por botón, con los botones del ratón. En los menús del
  juego WASD mueve el cursor. Ver [Controles en PC](#controles-en-pc)
- **Escribe con tu teclado:** el nombre de tu cazador, y una línea del chat en una sala o un hub con **/**
- **F2 muestra la lista de los controles** sobre el juego, con tus propias teclas, y el juego espera mientras está abierta
- **La pantalla táctil al lado del juego:** la pantalla de abajo de la 3DS se ve al lado de la de arriba, y el ratón es tu
  dedo en ella. Puede ir debajo de la de arriba, en una esquina o en una ventana propia; el **Modo de pantalla única**
  funciona como en la app
- **Cualquier ventana:** pantalla completa sin bordes a la resolución de tu pantalla de fábrica, una ventana de cualquier
  tamaño o pantalla completa exclusiva. **F11** o **Alt+Enter** cambia. Arrastra un borde de la ventana y la imagen toma
  su forma nueva al instante, de 5:3 a ultra ancha, con el HUD en sus bordes (o sobre el 16:9, 21:9 o 32:9 del centro:
  **Anclaje del HUD**)
- **Cuadros por segundo:** límites de hasta 240, o **Sin límite**, con **VSync** encendido o apagado; el juego mantiene su
  velocidad normal a cualquier cantidad de cuadros. **Mostrar FPS y datos de la PC** muestra los cuadros por segundo, y
  con **Completo** el uso de la GPU y de la CPU
- **Las mismas opciones de imagen que la app:** resolución 3D, modo gama baja, el filtro de color, el filtro PS1, Ladrillo,
  la generación de cuadros, FSR 1, y un **Filtro de escalado** para la imagen escalada (Suave, Píxeles nítidos, Vecino
  más cercano o FSR 1)
- **Un mando:** conéctalo y juega. START y SELECT juntos abren los ajustes; un clic del stick derecho fija la cámara en el
  monstruo; los menús pueden mostrar los botones de tu mando (**Íconos de los botones**). Un mando con giroscopio puede
  apuntar arcos y ballestas, como en la app (**Controles**, **Apuntar con giroscopio**)
- **Discord:** con la app de Discord abierta en la misma PC, tu perfil muestra qué estás haciendo: los menús, el pueblo, o
  los monstruos que cazas, el mapa y la zona, tu arma, y el nombre y el RC de tu cazador. **F1**, **Multijugador**,
  **Discord** lo apaga, o deja afuera tu nombre
- **Partidas, mods, trucos, DLC y misiones de evento, hubs y chat** funcionan como en la app, desde el mismo menú **F1**:
  ver [Partidas en PC](#partidas-en-pc), [Mods y trucos en PC](#mods-y-trucos-en-pc) y
  [Multijugador en PC](#multijugador-en-pc)
- **Verificar archivos del juego** lee tu juego y tu actualización y dice qué está mal (**F1**, **Sistema**, **Tu partida**)
- Si el juego se cierra por un error, una ventana te dice qué pasó y abre la carpeta del registro
- El juego se detiene mientras su ventana está minimizada, y sigue donde estaba al volver a abrirla (**F1**,
  **Sistema**, **Pausar en segundo plano**)
- **Una versión nueva:** como mucho una vez al día el programa le pregunta a GitHub cuál es la última versión (no se
  envía nada más) y te avisa al arrancar la próxima vez cuando hay una nueva; nunca descarga nada por su cuenta. **F1**,
  **Sistema**, **Buscar actualizaciones** lo apaga

### ¿Qué tal anda en PC?

Medido en la PC del desarrollador (Ryzen 5 3600XT, Radeon RX 6800 XT), sin bordes en un monitor de 3440x1440 y 160 Hz,
con la imagen 3D a 6x:

| Dónde | Ajustes | Cuadros por segundo |
|---|---|---|
| La aldea | Sin límite, VSync apagado | de 293 a 315 |
| El salón | Sin límite, VSync apagado | de 408 a 420 |
| Una misión | Sin límite, VSync apagado | de 305 a 310 |
| Aldea, salón y misión | Límite de 60, 120, 144 o 240 | ese límite, firme |
| Aldea, salón y misión | VSync encendido, a 160 Hz | 160 |

Todavía no se midió con gráficos integrados ni en una PC modesta. Si pruebas una, un [issue](../../issues) con tu
procesador, tu tarjeta gráfica y los cuadros por segundo ayuda.

### Qué necesitas en PC

- **Windows 10 u 11,** de 64 bits.
- **Una tarjeta gráfica con Vulkan,** con su controlador al día. La ventana se dibuja con OpenGL, que todo controlador con
  Vulkan para Windows también trae.
- **8 GB de RAM** recomendados. El juego en sí usa menos de 1 GB.
- **Espacio libre:** unos 4,3 GB para tu carpeta del juego, unos 2 GB más mientras se crea el código del juego (quedan
  unos 300 MB, las herramientas, que después se pueden borrar), y unos 200 MB para el programa y sus cachés.
- **Conexión a internet** la primera vez que se crea el código del juego: sus herramientas pesan unos 90 MB.
- **Tu propia copia del juego, descifrada:** la misma que para la app, Monster Hunter 4 Ultimate de EE. UU. como archivo
  `.3ds` o `.cci` y su **actualización 1.3.0** como archivo `.cia`.

### Instalación en PC

Lo haces una sola vez.

#### Paso 1 en PC: Pon tus archivos en una carpeta

La misma carpeta que para la app ([paso 1](#paso-1-pon-tus-archivos-en-una-carpeta)): tu juego y su actualización juntos,
por ejemplo en `C:\MH4U`. Si también usas la app, la misma carpeta sirve para las dos, y cada una guarda en ella su propio
código del juego.

#### Paso 2 en PC: Instálalo

No hay instalador: el programa es una carpeta.

1. Descarga `MH4U-PC-<versión>.zip` de [Releases](../../releases/latest) y descomprímelo entero en una carpeta tuya, como
   `C:\Juegos` (abierto desde dentro del zip, `MH4U.exe` no arranca). No en Archivos de programa: el programa guarda sus
   ajustes, cachés, registro y las herramientas que crean el código del juego en su propia carpeta, y Windows no lo deja
   escribir ahí.
2. Abre `MH4U.exe`. Windows puede decir que no conoce el programa, porque no está firmado: **Más información**,
   **Ejecutar de todas formas**.
3. La primera vez, una pantalla pide tu carpeta del juego: **Elegir carpeta**, y elígela. El programa la revisa y dice
   exactamente qué falta si algo está mal. Recuerda la carpeta.

#### Paso 3 en PC: Crear el código del juego

El programa no trae código del juego, así que arma el tuyo con tu propia copia, una vez, en tu PC. No se envía nada a
ningún lado.

Cuando tu carpeta tiene todo menos el código del juego, la pantalla dice que falta `mh4u_code_native.dll`: toca **Crear
el código del juego en esta PC** y después **Crear**.

- La primera vez descarga sus herramientas, unos 90 MB: Python de python.org y Zig, un compilador de C, de PyPI, o de
  ziglang.org, el sitio de Zig, cuando PyPI no responde. Las dos son software libre, se verifican con su SHA-256 antes de
  usarlas, y nunca se descarga nada del juego.
- Necesita unos 2 GB libres en el disco donde está `MH4U.exe` mientras trabaja. Al terminar quedan solo las herramientas
  (unos 300 MB, en la carpeta `builder` al lado de `MH4U.exe`), y **Borrar las herramientas** también las quita.
- Tarda de unos 10 a 40 minutos, según la PC (13 minutos en la del desarrollador, con un procesador de 6 núcleos). Usa
  todos los núcleos del procesador menos uno, con prioridad baja, así que puedes usar la PC mientras tanto.
- **Cancelar** lo detiene, y también cerrar la ventana o reiniciar la PC: lo que hizo se guarda, y **Seguir** continúa
  desde ahí. **Empezar de cero** olvida una creación a medias y empieza desde el principio.
- Un mod de código en la carpeta `mods` de tu carpeta (el de una sola pantalla, por ejemplo) entra en el código del juego,
  como en la app: **Cambiar los mods de código** elige cuáles.

Cuando dice que el código del juego está listo, tu carpeta tiene dos archivos nuevos, `mh4u_code_native.dll` y
`mh4u_code_native.id`, y **Jugar** arranca el juego. **Esos archivos están hechos con tu juego: guárdalos para ti y no los
compartas.**

Para elegir otra carpeta más adelante: **F1**, **Sistema**, **Tu partida**, **Carpeta del juego**. Para volver a crear el
código del juego: **F1**, **Sistema**, **Código del juego**, **Crear el código del juego** (el juego pregunta, se cierra y
abre el creador).

El zip trae una carpeta con los archivos del programa y nada del juego:

- `MH4U.exe`, y `SDL3.dll` a su lado
- `zigcc.exe` y `builder_tools.zip`: lo que usa el creador del programa para armar el código del juego
- `code_library_id.txt`: el id del código del juego que necesita esta versión. El `mh4u_code_native.id` de tu carpeta tiene
  que decir lo mismo
- `cheats`: los trucos opcionales para comer y beber más rápido, que el programa suma a tus trucos, apagados
- `licenses`: el aviso legal y las licencias, también en **F1**, **Créditos y licencias**, **Licencias**
- `README.txt`: una guía corta

### Controles en PC

- **Teclado y ratón:** WASD mueve, Shift izquierdo camina, I, J, K, L giran la cámara. El clic izquierdo, U o X es X; F, O
  o el clic derecho es A (el clic derecho solo con el arma afuera, en un menú o una charla, o fuera del pueblo y de las
  misiones); Espacio es B, y Esc también; R, Y o el botón del medio es Y; Q es L, E es R; Tab y T son ZL y ZR; Enter es
  START, Retroceso SELECT, y las flechas son la cruceta. Un toque de Alt fija la cámara en el monstruo. La rueda del ratón
  mueve la barra de objetos. Haz clic en la ventana para girar la cámara con el ratón; Esc suelta el ratón.
- **Las teclas propias del programa:** **F1** los ajustes, **F2** la lista de los controles, **F11** o **Alt+Enter**
  pantalla completa o ventana, **1** a **4** los objetos de los atajos de objetos de la pantalla táctil, **G** los Gestos
  del pueblo, y **/** el chat en una sala o un hub.
- **Cada tecla se puede cambiar,** hasta tres por botón, con los botones del ratón: **F1**, **Controles**, **Teclado y
  ratón**, **Asignación de teclas**. Alt, Ctrl y la tecla de Windows solo presionan su botón si se tocan solas, así que
  Alt+Tab y Alt+Enter no lo hacen. Ahí también se pueden invertir los ejes del ratón.
- **La pantalla táctil:** un clic en ella es un toque. En una PC con pantalla táctil, los controles táctiles aparecen
  cuando la tocas.
- **El menú de ajustes (F1)** funciona con el ratón, con el teclado (las flechas, Enter, Esc; Tab y Re Pág/Av Pág cambian
  de sección, Ctrl+S guarda) o con un mando. Un clic fuera de él lo cierra, y pregunta antes si algo no está guardado.
- **El HUD:** **F1**, **Controles**, **Personalizar controles y HUD**: arrastra cualquier parte con el ratón, la rueda
  cambia su tamaño, **Ocultar** también quita de la pantalla las partes propias del juego, y Esc cancela.
- **Mando:** conéctalo y juega. START y SELECT juntos abren los ajustes. Si el juego no lee bien el tuyo: **Controles**,
  **Mando**, **Mapear un mando**.

### Gráficos y cuadros por segundo en PC

En **F1**, **Gráficos y audio**: **Límite de FPS** (de 15 a 240, o **Sin límite**; 60 de fábrica), la resolución 3D, el
**Filtro de escalado** (Suave, Píxeles nítidos, Vecino más cercano o FSR 1), el modo gama baja y los filtros como en la
app. En su **Pantalla y HUD**: **Modo de ventana** (Sin bordes, Ventana o Pantalla completa), **Resolución de pantalla**,
**VSync** (encendido de fábrica), **Anclaje del HUD**, **Tamaño del HUD**, **Mostrar FPS y datos de la PC** (Básico o
Completo), el modo de pantalla única y la **Disposición** de la pantalla táctil (Lado a lado, la de fábrica en PC;
Apiladas; En una esquina; En su propia ventana). En su **Color**: **Salida HDR** (apagada de fábrica; con el HDR de
Windows activado para la pantalla). En su **Avanzado**: **GPU**, en una PC con dos tarjetas gráficas (de fábrica, una
dedicada). Los mismos lugares que en MHGU para PC.

- **Sin límite** necesita **VSync** apagado: con VSync encendido, la frecuencia del monitor es el máximo. Con VSync
  encendido y un límite que divide la frecuencia del monitor (60 en 120 o 240 Hz, 30 en 60 Hz), cada cuadro se ve el
  mismo tiempo.
- **Generación de cuadros** necesita VSync y un límite de la mitad de la frecuencia del monitor o menos, como en un
  teléfono.
- La **Salida HDR** (Gráficos y audio > Color) necesita el HDR de Windows activado para la pantalla (Configuración >
  Pantalla > Usar HDR) y reiniciar; el juego dibuja una imagen común, así que el HDR reparte su luz y no agrega detalle.
- La ventana dice **Iniciando el juego...** hasta la primera imagen del juego. El primer arranque tarda un poco más
  mientras el controlador de la tarjeta gráfica prepara sus shaders; los siguientes arrancan rápido.
- En una laptop con gráficos integrados, baja la **Resolución 3D** o enciende el **Modo gama baja**; en una laptop con dos
  tarjetas gráficas, revisa que **GPU** muestre la dedicada.

### Partidas en PC

Tu partida está en `%APPDATA%\MH4U` (escríbelo en la barra de direcciones del Explorador de archivos), así una versión
nueva descomprimida en otro lado la encuentra. Desde **F1**, **Sistema**, **Tu partida**, como en la app
([Importar tu partida](#importar-tu-partida), [Exportar tu partida](#exportar-tu-partida),
[Elegir dónde se guarda tu partida](#elegir-dónde-se-guarda-tu-partida)):

- **Importar partida:** elige una carpeta: la de un emulador (la carpeta de usuario de Azahar o de Citra, o su `sdmc`), una
  copia de Checkpoint o JKSM, o una carpeta exportada desde la app. El juego encuentra la partida en ella, dice qué
  encontró y se reinicia con ella; la partida que reemplaza queda en `%APPDATA%\MH4U\save_backups`.
- **Exportar partida:** elige una carpeta, y se crea en ella una carpeta `MH4U save <fecha>` con `sdmc` como lo ordena
  Azahar: cópiala sobre el `sdmc` del emulador, o impórtala en la app.
- **Carpeta de partidas:** guarda la partida en otra carpeta, como una que una app de sincronización comparte con tus otros
  dispositivos. Nada se pisa sin preguntar: la partida que no sigue queda en `save_backups`.
- **Importar DLC y misiones de evento** (**F1**, **Mods y trucos**, **DLC y misiones de evento**): el DLC que instalaste en
  Azahar, o un `.cia` descifrado tuyo, y tus misiones de evento, como en la app
  ([Importar DLC y misiones de evento](#importar-dlc-y-misiones-de-evento)). **DLC en todas las partidas** funciona como
  en la app, y antes copia tu partida a `save_backups`.

### Mods y trucos en PC

Como en la app ([Mods y trucos](#mods-y-trucos)), en **F1**, **Mods y trucos**: **Agregar un mod** (su .zip) o **Agregar un
mod (carpeta)**, **Agregar trucos**, y los mods de la carpeta `mods` de tu carpeta del juego. Los packs de texturas
HD van en la carpeta `textures` de tu carpeta del juego: **Packs de texturas**, **Abrir la carpeta textures** la abre en
el Explorador de archivos. Un mod con carpeta `exefs` necesita el código del juego creado con él: **F1**, **Sistema**,
**Código del juego**, **Crear el código del juego**, y **Cambiar los mods de código** en la página del creador.

### Multijugador en PC

Los hubs por internet, las salas en tu red y el chat funcionan como en la app, con los mismos ajustes en **F1**,
**Multijugador** ([Jugar con otros](#jugar-con-otros)). Una PC y un teléfono juegan en el mismo hub.

- **/** abre el chat para escribir una línea (**Controles**, **Teclado y ratón**, **Asignación de teclas**, **Tecla del chat** la cambia).
- Cuando creas un hub al que otros entran directo (con emuladores, o en tu red), el Firewall de Windows pregunta la primera
  vez si MH4U puede usar la red: permítelo, o nadie podrá encontrar ni entrar a tu hub. Solo por el servidor de hubs no
  pregunta.

### Dónde se guarda cada cosa en PC

| Qué | Dónde |
|---|---|
| Ajustes, la carpeta que elegiste, el registro | Al lado de `MH4U.exe`: `mh4u-app.ini`, `game_folder.txt`, `log.txt` (las sesiones anteriores: `log_prev.txt`, `log_prev2.txt`), y `notices.txt` (cuándo buscó una versión nueva por última vez) |
| Los archivos chicos del juego, los ajustes propios del juego, trucos, mods y DLC que agregaste | Al lado de `MH4U.exe`: `game` (leídos de tu juego y tu actualización), `user` (`mh4u.ini`, `cheats`, `load/mods`, y el DLC dentro de `sdmc`) |
| La caché de shaders | `%LOCALAPPDATA%\MH4U\cache`: **F1**, **Gráficos y audio**, **Avanzado**, **Borrar la caché de shaders** la vacía |
| Las herramientas que crean el código del juego, y su registro | Al lado de `MH4U.exe`: `builder` (Python y Zig; mientras crea, también su carpeta de trabajo), `builder_log.txt` |
| Tu partida | `%APPDATA%\MH4U` (o la carpeta que elegiste en **Carpeta de partidas**). Una partida o misiones de evento que importas esperan en `save_import` y `save_import_extra` al lado de `MH4U.exe` hasta que el juego se reinicia |
| Copias de las partidas que una importación o un cambio reemplazó | `%APPDATA%\MH4U\save_backups` |
| Tu juego, el código del juego, los mods y packs HD que pongas ahí | Tu carpeta del juego: `mh4u_code_native.dll` y `.id`, `mods`, `textures` |

### Actualizar la versión de PC

1. Descomprime la versión nueva y copia sus archivos sobre los viejos. Tus ajustes y la carpeta que elegiste quedan, y tu
   partida queda de todas formas.
2. Si la versión nueva necesita un código del juego nuevo, el juego dice "Esta mh4u_code_native.dll es de otra versión"
   al arrancar: toca **Crear el código del juego en esta PC** y después **Crear** (las herramientas ya están).

### Desinstalar la versión de PC

- Borra la carpeta del programa, la que tiene `MH4U.exe`. Sus ajustes, cachés, registro, los trucos, mods y DLC que
  agregaste y las herramientas del creador (`builder`) se van con ella.
- Tu partida queda en `%APPDATA%\MH4U`. Borra esa carpeta también solo si quieres que desaparezca, y guarda una copia
  antes.
- La caché de shaders queda en `%LOCALAPPDATA%\MH4U`: borra esa carpeta también.
- Tu carpeta del juego es tuya: guárdala para la app, o bórrala.
- No se instaló nada más: no hubo instalador, y el programa no guarda ajustes en el registro de Windows. Si permitiste MH4U
  en el Firewall de Windows, esa regla queda hasta que la quites en Seguridad de Windows.

### Si algo falla en PC

| Problema | Qué hacer |
|---|---|
| "No se encontró SDL3.dll" | Descomprime todo el zip en una carpeta primero: `MH4U.exe` no arranca desde dentro del zip |
| El Control inteligente de aplicaciones lo bloqueó | Bloquea los programas que no están firmados, y `MH4U.exe` no lo está: solo corre con el Control inteligente de aplicaciones apagado (Seguridad de Windows, **Control de aplicaciones y navegador**). Piénsalo antes: en muchas versiones de Windows no se puede volver a encender sin reinstalar Windows |
| "La librería de código del juego (mh4u_code_native.dll) no está en la carpeta" | La descarga no la incluye: toca **Crear el código del juego en esta PC** en esa pantalla ([paso 3 en PC](#paso-3-en-pc-crear-el-código-del-juego)) |
| "Esta mh4u_code_native.dll es de otra versión" | Se hizo para otra versión, o para la app: toca **Crear el código del juego en esta PC**, y se vuelve a crear para esta |
| El creador no puede descargar sus herramientas | Revisa la conexión a internet y toca **Seguir**: la descarga continúa donde quedó. Si PyPI no responde, el creador baja Zig de ziglang.org solo. Si los dos están bloqueados, otra red, o un DNS como 1.1.1.1, lo resuelve |
| Tu antivirus avisa, bloquea o borra `zig.exe` o `python.exe` en la carpeta `builder` | Son el Zig y el Python oficiales, verificados con su SHA-256 antes de correr. Permite la carpeta `builder` al lado de `MH4U.exe` y toca **Seguir** |
| El creador dice que no hay espacio | Necesita unos 2 GB libres en el disco donde está `MH4U.exe`. Libera espacio, o mueve la carpeta del programa a otro disco, y toca **Seguir** |
| El creador se detuvo y **Seguir** vuelve a fallar | **Empezar de cero** empieza desde el principio. Si sigue fallando, adjunta `builder_log.txt` (al lado de `MH4U.exe`) a un [issue](../../issues) |
| Pide la carpeta del juego en cada arranque | Windows no lo deja escribir en su carpeta: sácalo de Archivos de programa |
| Se cierra al arrancar, o la ventana queda en negro | Actualiza el controlador de tu tarjeta gráfica. Hasta la primera imagen del juego la ventana dice **Iniciando el juego...**; el primer arranque tarda un poco más mientras se preparan los shaders |
| El ratón no gira la cámara | Haz clic en la ventana primero. Esc suelta el ratón |
| Va lento | Baja la **Resolución 3D** o enciende el **Modo gama baja**. En una laptop con dos tarjetas gráficas, revisa que **GPU** muestre la dedicada |
| No sabes si tus archivos del juego están bien | **F1**, **Sistema**, **Tu partida**, **Verificar archivos del juego**: lee todos los archivos y dice qué está mal y qué hacer |

Si el juego se cierra por un error, una ventana lo dice; **Abrir la carpeta del registro** abre la carpeta con `log.txt`
(si volviste a abrir el juego después, el de esa sesión es `log_prev.txt`). También está en **F1**, **Sistema**, **Tu
partida**, **Carpeta del registro**. Adjúntalo a un [issue](../../issues) con el procesador y la tarjeta gráfica de tu PC,
o pregunta en el [Discord](https://discord.gg/sk5xEjEMwF). No adjuntes archivos del juego.

## Compilar la app

Para desarrolladores; los jugadores solo necesitan el APK o el zip de PC de Releases. En Windows (Git Bash), Linux o macOS, con el SDK de
Android (plataforma 35) y el NDK 27.0.12077973, CMake 3.25 o superior, Ninja, Python 3.10 o superior y un JDK 17:

```
sh scripts/build_sdl3.sh    # SDL 3.4.18 desde su release, una vez
sh scripts/build_apk.sh     # libmain.so, las librerías de hooks, el armado en el teléfono y después Gradle
```

`scripts/android_env.sh` dice dónde se busca cada herramienta. Firma con tu propia clave en `android/keystore.properties` y
`android/mh4u.keystore`; sin ellas sale un APK de depuración. `app_native/` es la app (SDL3 y Dear ImGui), `android/` su
parte de Android, `app_native/pc/` la de PC, y `mh4n/` el runtime y el recompilador (mira [mh4n/README.md](mh4n/README.md)).

La versión de PC, en Git Bash en Windows, con [llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases) 20241217 para
x86_64 con la UCRT, CMake 3.25 o superior, Ninja y Python 3.10 o superior:

```
LLVM_MINGW=C:/llvm-mingw-20241217-ucrt-x86_64 bash scripts/build_pc.sh            # build/pc/MH4U, como la descomprime un jugador
LLVM_MINGW=C:/llvm-mingw-20241217-ucrt-x86_64 PACK=1 bash scripts/build_pc.sh     # y el zip de la versión con su .sha256
```

La primera vez compila SDL 3.4.18 para Windows desde su release.

## Legal

Este proyecto no está afiliado, respaldado ni patrocinado por la Gran C ni la Gran N. Monster Hunter es una marca de la Gran
C. 3DS es una marca de la Gran N.

Este repositorio y sus releases no contienen código del juego, datos del juego, ROMs ni keys. Úsalo solo con una copia del
juego que te pertenezca. Los pedidos de archivos del juego, keys o archivos `libmh4u_code_native.so` o `mh4u_code_native.dll` ya hechos se cerrarán.

Titulares de derechos: abran un issue para ponerse en contacto.

## Créditos

- El recompilador del juego y el runtime nativo vienen de [MH4UltimateDC](https://github.com/CompaLarios18/MH4UltimateDC),
  de CompaLarios18 y sus colaboradores.
- Partes del runtime derivan de [Azahar](https://github.com/azahar-emu/azahar) y Citra.
- Del mismo autor: [MHGU para Android y PC](https://github.com/khorne-hub/gu-native), Monster Hunter Generations
  Ultimate recompilado para Android y Windows de la misma forma. Partes de la versión de PC (sus datos de la PC, su lector
  de zip y Discord Rich Presence) siguen a las del MHGU para PC.
- El mod de una sola pantalla es de [jM5557](https://github.com/jM5557/mh4u-single-screen-mod), y no es parte de este
  proyecto.
- Aportaron al desarrollo:
  - tabarong
  - Klark231
  - Alfonso Matador
  - popzol
  - BlitzWOLF
  - jM5557
- El modo de pantalla única pone los menús donde los pone el modo de una pantalla de
  [Heavenfall](https://github.com/popzol/4u-heavenfall-mod-beta), de popzol y BlitzWOLF (GPL-3.0), sin los cambios que
  Heavenfall le hace al juego.
- El creador de la app, que crea el código del juego en el teléfono, la primera vez descarga Python
  ([python.org](https://www.python.org), licencia PSF) y [Zig](https://ziglang.org) (MIT; con LLVM, Apache 2.0 con
  excepciones de LLVM); sus licencias también están en **Licencias** de la app.
- Las librerías que usa este proyecto, con sus licencias, están en [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES). Sus licencias
  también están en la app: engranaje, **Créditos y licencias**, **Licencias**.

## Descargas oficiales

MH4U para Android y PC es gratis. Las únicas descargas oficiales son los links que da khorne-hub, empezando por las
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

La versión de PC es el zip `MH4U-PC-<versión>.zip`, con su SHA-256 al lado en `MH4U-PC-<versión>.zip.sha256`. Para
revisar el tuyo, en un Símbolo del sistema: `certutil -hashfile MH4U-PC-<versión>.zip SHA256`, y compáralo con ese
archivo.

La app también te avisa cuando sale una versión nueva. Como mucho una vez al día le pregunta a GitHub cuál es la última
versión de este repositorio (no se envía nada más, ni siquiera el modelo del teléfono), y al abrir el juego te ofrece abrir
su página. Nunca descarga ni instala nada por su cuenta. Engranaje, **Sistema**, **Buscar actualizaciones** lo apaga.

## Licencia

MH4U para Android y PC es software libre bajo la [Licencia Pública General GNU, versión 3](LICENSE) o (a tu elección) cualquier
versión posterior, como el runtime de MH4UltimateDC sobre el que se construye. Las partes de terceros conservan sus propias
licencias.

El código de khorne-hub tiene [términos adicionales](ADDITIONAL-TERMS), como lo permite la sección 7 de la GPL. Quien
comparta la app, modificada o no, tiene que:

- conservar los créditos del autor: los que la app muestra al abrirse, su página **Créditos y licencias**, y las secciones
  **Créditos** y **Licencia** de este archivo;
- indicar que una versión modificada está modificada, y no presentarla como la oficial;
- dar, junto con la app, su licencia y su código fuente completo, como lo pide la GPL.
