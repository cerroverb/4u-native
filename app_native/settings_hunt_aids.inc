// Hunting aids (1.0.1, the owner's: "in the app, off by default"): the large monsters always on the game's maps, and
// the damage of each hit as a number over the monster. Graphics & Audio > Screen and HUD. The port does both from the
// game's memory, without a new code library (mh4n port/src/hunt_aids.cpp: monsters_on_map and damage_numbers in
// mh4u.ini, applied at the game's next frame); the numbers are drawn by the app over the top screen (hunt_numbers.cpp),
// never in the game's picture. Included by settings_panel.cpp.
void hunt_aids_rows(std::vector<Row>& r) {
    header(r, "hunt.header", en_es("Hunting aids", "Ayudas de caza"));  // (1.0.2: the MHGU port's section, its name)
    add_switch(r, "hunt.map", en_es("Monsters always on the map", "Monstruos siempre en el mapa"),
               &s.draft.monsters_on_map, "monsters_on_map");
    r.back().help = en_es(
        "In a quest, the large monsters show on the game's maps (the touch screen's and the top screen's) from the start, "
        "as if they were painted, with no paintball and no skill, as in the newer games. In quests you hunt alone; in "
        "a quest with other hunters (a room) the map is as the game has it. Off: as the game does it.",
        "En una misión, los monstruos grandes aparecen en los mapas del juego (el de la pantalla táctil y el de la "
        "pantalla de arriba) desde el principio, como si estuvieran pintados, sin bola de pintura ni habilidades, como "
        "en los juegos nuevos. En las misiones que cazas solo; en una misión con otros cazadores (una sala) el mapa "
        "queda como lo tiene el juego. Apagado: como lo hace el juego.");
    add_switch(r, "hunt.numbers", en_es("Damage numbers", "Números de daño"), &s.draft.damage_numbers,
               "damage_numbers");
    r.back().help = en_es(
        "Each hit on a monster, large or small, shows its damage as a number over the monster, which rises and fades in "
        "about a second. Any damage counts: yours, your Palico's, bombs, poison and the other hunters' in your room. The "
        "app draws the numbers over the top screen; the game's picture is not changed. Off: no numbers, as the game "
        "does it.",
        "Cada golpe a un monstruo, grande o chico, muestra su daño como un número sobre el monstruo, que sube y se "
        "desvanece en un segundo, más o menos. Cuenta todo el daño: el tuyo, el de tu Felyne, bombas, veneno y el de "
        "los otros cazadores de tu sala. La app dibuja los números sobre la pantalla de arriba; la imagen del juego no "
        "cambia. Apagado: sin números, como lo hace el juego.");
    // the numbers' look ([Graphics] DamageNumberSize / DamageNumberColour in mh4u-app.ini, hunt_numbers.cpp): Medium and
    // White are the look they had when they came
    static const char* const SIZE_KEYS[] = {"small", "medium", "large"};
    static const char* const COLOUR_KEYS[] = {"white", "yellow", "red", "blue", "green"};
    static const char* const STYLE_KEYS[] = {"classic", "outline", "compact", "burst"};
    add_choice(r, "hunt.numbers.size", en_es("Damage number size", "Tamaño de los números de daño"),
               index_of(SIZE_KEYS, 3, s.draft.number_size),
               {en_es("Small", "Chico"), en_es("Medium", "Mediano"), en_es("Large", "Grande")},
               [](int v) { s.draft.number_size = SIZE_KEYS[v]; }, "DamageNumberSize", s.draft.number_size);
    r.back().help = en_es("How big the damage numbers are on the top screen. It applies at once.",
                          "Qué tan grandes se ven los números de daño en la pantalla de arriba. Se aplica al momento.");
    add_choice(r, "hunt.numbers.colour", en_es("Damage number colour", "Color de los números de daño"),
               index_of(COLOUR_KEYS, 5, s.draft.number_colour),
               {en_es("White", "Blanco"), en_es("Yellow", "Amarillo"), en_es("Red", "Rojo"), en_es("Light blue", "Celeste"),
                en_es("Green", "Verde")},
               [](int v) { s.draft.number_colour = COLOUR_KEYS[v]; }, "DamageNumberColour", s.draft.number_colour);
    r.back().help = en_es("The damage numbers' colour, always with a dark outline so they read on any ground. It applies "
                          "at once.",
                          "El color de los números de daño, siempre con un borde oscuro para que se lean sobre cualquier "
                          "fondo. Se aplica al momento.");
    add_choice(r, "hunt.numbers.style", en_es("Damage number style", "Estilo de los números de daño"),
               index_of(STYLE_KEYS, 4, s.draft.number_style),
               {en_es("Classic", "Clásico"), en_es("Outlined", "Con contorno"), en_es("Compact", "Compacto"),
                en_es("Burst", "Ráfaga")},
               [](int v) { s.draft.number_style = STYLE_KEYS[v]; }, "DamageNumberStyle", s.draft.number_style);
    r.back().help = en_es(
        "How the damage numbers look. It applies at once. Classic: as they came, rising and fading. Outlined: a thicker "
        "outline, and each number pops in. Compact: smaller and shorter. Burst: a run of hits on one monster adds up "
        "in one number that grows with each hit, with how many hits it holds.",
        "Cómo se ven los números de daño. Se aplica al momento. Clásico: como venían, suben y se desvanecen. Con "
        "contorno: un borde más grueso, y cada número aparece con un salto. Compacto: más chicos y cortos. Ráfaga: una "
        "seguidilla de golpes a un monstruo se suma en un solo número que crece con cada golpe, con cuántos golpes "
        "lleva.");
    // the monsters' health bars (mh4u.ini monster_health_bar: mh4n's HuntAids::Healths(), drawn by hunt_bars.cpp)
    add_switch(r, "hunt.bar", en_es("Monster health bars", "Barra de vida de los monstruos"),
               &s.draft.monster_health_bar, "monster_health_bar");
    r.back().help = en_es(
        "In a quest, a thin bar at the top right of the top screen shows each large monster's name (in the game's "
        "language) and health, with its numbers; one in another area of the map is shown fainter. It only reads the game, so it works in a room too, and it hides "
        "while a menu of the game is open. Move it and size it with Customize controls and HUD. Off: as the game does it.",
        "En una misión, una barra fina arriba a la derecha de la pantalla de arriba muestra el nombre (en el idioma del "
        "juego) y la vida de cada monstruo grande, con sus números; uno que está en otra zona del mapa se ve más tenue. Solo lee el juego, así que también anda en "
        "una sala, y se esconde mientras hay un menú del juego abierto. Se mueve y se cambia de tamaño con Personalizar "
        "controles y HUD. Apagado: como lo hace el juego.");
    // the touch screen's quick items on the main screen (1.0.1.1, mh4u.ini quick_items: mh4n's GameState items_slot,
    // drawn by quick_items.cpp)
    add_switch(r, "hunt.items", en_es("Show quick items", "Mostrar objetos rápidos"), &s.draft.quick_items,
               "quick_items");
    r.back().help = en_es(
        "In a quest under way, the touch screen's quick items panel (the item shortcuts of your panel layout) also shows "
        "on the main screen, small, and a tap on an item there uses it as on the touch screen. It hides while the whole "
        "touch screen shows, with the quest menu or a large panel open, and when your layout has no quick items panel. "
        "Move it and size it with Customize controls and HUD. Off: only on the touch screen, as the game does it.",
        "En una misión en curso, el panel de objetos rápidos de la pantalla táctil (los atajos de objetos de tu "
        "distribución de paneles) también se ve en la pantalla principal, en chico, y tocar un objeto ahí lo usa como en "
        "la pantalla táctil. Se esconde mientras se ve toda la pantalla táctil, con el menú de la misión o un panel "
        "grande abierto, y si tu distribución no tiene panel de objetos rápidos. Se mueve y se cambia de tamaño con "
        "Personalizar controles y HUD. Apagado: solo en la pantalla táctil, como lo hace el juego.");
}
void reset_hunt_aids() {
    s.draft.monster_health_bar = defaults().monster_health_bar;
    s.draft.quick_items = defaults().quick_items;
    s.draft.monsters_on_map = defaults().monsters_on_map;
    s.draft.damage_numbers = defaults().damage_numbers;
    s.draft.number_size = defaults().number_size;
    s.draft.number_colour = defaults().number_colour;
    s.draft.number_style = defaults().number_style;
}
// (1.0.2: the place of the MHGU port's Hunting aids, the last section before the touch screen's)
MENU_ENTRY("graphics/screen", 95, hunt_aids_rows, reset_hunt_aids);
