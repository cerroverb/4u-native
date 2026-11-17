// Hunting aids (1.0.1, the owner's: "in the app, off by default"): the large monsters always on the game's maps, and
// the damage of each hit as a number over the monster. Graphics & Audio > Screen and HUD. The port does both from the
// game's memory, without a new code library (mh4n port/src/hunt_aids.cpp: monsters_on_map and damage_numbers in
// mh4u.ini, applied at the game's next frame); the numbers are drawn by the app over the top screen (hunt_numbers.cpp),
// never in the game's picture. Included by settings_panel.cpp.
void hunt_aids_rows(std::vector<Row>& r) {
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
        "Each hit on a large monster shows its damage as a number over the monster, which rises and fades in about a "
        "second. Any damage counts: yours, your Palico's, bombs, poison and the other hunters' in your room. The app "
        "draws the numbers over the top screen; the game's picture is not changed. Off: no numbers, as the game does it.",
        "Cada golpe a un monstruo grande muestra su daño como un número sobre el monstruo, que sube y se desvanece en "
        "un segundo, más o menos. Cuenta todo el daño: el tuyo, el de tu Felyne, bombas, veneno y el de los otros "
        "cazadores de tu sala. La app dibuja los números sobre la pantalla de arriba; la imagen del juego no cambia. "
        "Apagado: sin números, como lo hace el juego.");
    // the numbers' look ([Graphics] DamageNumberSize / DamageNumberColour in mh4u-app.ini, hunt_numbers.cpp): Medium and
    // White are the look they had when they came
    static const char* const SIZE_KEYS[] = {"small", "medium", "large"};
    static const char* const COLOUR_KEYS[] = {"white", "yellow", "red", "blue", "green"};
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
    // the monsters' health bars (mh4u.ini monster_health_bar: mh4n's HuntAids::Healths(), drawn by hunt_bars.cpp)
    add_switch(r, "hunt.bar", en_es("Monster health bar", "Barra de vida de los monstruos"),
               &s.draft.monster_health_bar, "monster_health_bar");
    r.back().help = en_es(
        "In a quest, a thin bar at the top right of the top screen shows each large monster's name (in the game's "
        "language) and health, with its numbers; one in another area of the map is shown fainter. It only reads the game, so it works in a room too, and it hides "
        "while a menu of the game is open. Move it and size it with Customize controls and HUD. Off: as the game does it.",
        "En una misión, una barra fina arriba a la derecha de la pantalla de arriba muestra el nombre (en el idioma del "
        "juego) y la vida de cada monstruo grande, con sus números; uno que está en otra zona del mapa se ve más tenue. Solo lee el juego, así que también anda en "
        "una sala, y se esconde mientras hay un menú del juego abierto. Se mueve y se cambia de tamaño con Personalizar "
        "controles y HUD. Apagado: como lo hace el juego.");
}
void reset_hunt_aids() {
    s.draft.monster_health_bar = defaults().monster_health_bar;
    s.draft.monsters_on_map = defaults().monsters_on_map;
    s.draft.damage_numbers = defaults().damage_numbers;
    s.draft.number_size = defaults().number_size;
    s.draft.number_colour = defaults().number_colour;
}
MENU_ENTRY("graphics/screen", 70, hunt_aids_rows, reset_hunt_aids);
