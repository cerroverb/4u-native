// MH4U for PC: the settings' rows and guides only the PC has (settings_panel.cpp includes this in its namespace, after
// Controls > Keyboard and mouse's own rows; Android never builds it). The mouse's camera turned the other way, the list
// of the controls (pc/pc_input.cpp), the controller's buttons in the menu's guides in its own look ([Controls]
// ButtonIcons), the help of the PC's own keybinds (controls.h BIND_HELP..), and the guides at the menu's foot for the
// keyboard and the mouse as for a controller.

bool picker_removable();

// ---- Controls > Keyboard and mouse: the mouse's camera, the list of the controls
void pc_mouse_rows(std::vector<Row>& r) {
    auto add = [&r](const char* id, const char* en, const char* es, bool* value, const char* key, const char* help_en,
                    const char* help_es) {
        Row row;
        row.id = id, row.title = en_es(en, es), row.key = key;
        row.value = tr(*value ? "On" : "Off");
        row.toggle = *value ? 1 : 0;
        row.adjust = [value](int d) { *value = d > 0; };
        row.activate = [value]() { *value = !*value; };
        row.help = en_es(help_en, help_es);
        r.push_back(std::move(row));
    };
    add("mouse.invx", "Invert the mouse across", "Invertir el ratón a los lados", &s.draft.mouse_inv_x, "InvertX",
        "The mouse turns the camera the other way to the sides.", "El ratón gira la cámara al revés hacia los lados.");
    add("mouse.invy", "Invert the mouse up and down", "Invertir el ratón arriba y abajo", &s.draft.mouse_inv_y, "InvertY",
        "Moving the mouse up turns the camera down, as in a flight game.",
        "Mover el ratón hacia arriba baja la cámara, como en un juego de aviones.");
    action(r, "pc.controls", en_es("List of the controls", "Lista de los controles"), []() {
        close_menu(g_open);
        pc_help_show();
    });
    r.back().help = en_es("Every key, mouse button and controller button and what it does, over the game. F2 shows it too, "
                          "and F2, Esc or a click hides it.",
                          "Cada tecla, botón del ratón y botón del mando y lo que hace, sobre el juego. F2 también la muestra, "
                          "y F2, Esc o un clic la ocultan.");
    note(r, "pc.mouse.note",
         en_es("Left click attacks (X). The right click is A only with the weapon out, in a menu or a talk: it never talks, "
               "gathers or carves by mistake (F does). Modifier keys (Ctrl, Alt) press their button when tapped alone, so "
               "Alt+Tab and Alt+Enter don't lock on.",
               "El clic izquierdo ataca (X). El clic derecho es A solo con el arma afuera, en un menú o en una charla: nunca "
               "habla, recolecta ni talla sin querer (F sí). Las teclas modificadoras (Ctrl, Alt) presionan su botón al "
               "tocarlas solas, así Alt+Tab y Alt+Enter no fijan el objetivo."),
         touch_look().dim);
}
void reset_pc_mouse() {
    Draft f = defaults();
    s.draft.mouse_inv_x = f.mouse_inv_x, s.draft.mouse_inv_y = f.mouse_inv_y;
}
[[maybe_unused]] const bool pc_menu_mouse = menu_entry("controls/keys", 20, pc_mouse_rows, reset_pc_mouse, controls_keyboard_mouse);

// ---- Controls > Controller: the controller's buttons in the menu's guides
void pc_icon_rows(std::vector<Row>& r) {
    const std::vector<std::string> names = {en_es("Auto", "Automático"), "Xbox", "PlayStation", "B A Y X",
                                            en_es("The game's letters", "Las letras del juego")};
    Row row;
    row.id = "pad.icons", row.title = en_es("Button icons", "Íconos de los botones"), row.key = "ButtonIcons";
    const int sel = std::clamp(s.draft.button_icons, 0, (int)names.size() - 1);
    row.value = names[sel];
    row.adjust = [sel, names](int d) { s.draft.button_icons = (sel + d + (int)names.size()) % (int)names.size(); };
    row.help = en_es("How the menus show your controller's buttons. Auto: as the controller in use looks (an Xbox, a "
                     "PlayStation controller, or one with B A Y X). The game's letters: its own A, B, X and Y.",
                     "Cómo muestran los menús los botones de tu mando. Automático: como se ve el mando en uso (de Xbox, de "
                     "PlayStation, o uno con B A Y X). Las letras del juego: su propia A, B, X e Y.");
    Row copy = row;
    row.menu = true;
    row.activate = [copy, names, sel]() { choices(copy, names, sel, [](int v) { s.draft.button_icons = v; }); };
    r.push_back(std::move(row));
}
void reset_pc_icons() { s.draft.button_icons = defaults().button_icons; }
[[maybe_unused]] const bool pc_menu_icons = menu_entry("controls/pad", 110, pc_icon_rows, reset_pc_icons);  // (the Controller page's PC rows, last: MHGU for PC's)

// ---- Keybinds: the PC's own rows' help
void pc_bind_help(int i, Row& row) {
    if (i == BIND_HELP)
        row.help = en_es("Shows or hides the list of the controls over the game.", "Muestra u oculta la lista de los controles sobre el juego.");
    if (i >= BIND_SCREEN_ITEM1 && i <= BIND_SCREEN_ITEM4)
        row.help = en_es("In a quest, uses that item of the item shortcuts panel on the touch screen (its first to fourth, "
                         "left to right), as a tap on it does. Only while that panel is on the touch screen.",
                         "En una misión, usa ese objeto del panel de atajos de objetos de la pantalla táctil (del primero al "
                         "cuarto, de izquierda a derecha), como un toque en él. Solo mientras ese panel está en la pantalla táctil.");
    if (i >= BIND_TOWN_CARDS && i <= BIND_TOWN_HIRE)
        row.help = en_es("In a town, opens it as the town's touch screen does (also in SELECT's list of the town's actions).",
                         "En un pueblo, lo abre como la pantalla táctil del pueblo (también está en la lista de acciones del "
                         "pueblo de SELECT).");
}

// ---- the guides at the menu's foot: what each button and key does here, for the last used (a controller's buttons in
// its own look, else the keyboard's keys: pc_input.cpp draws them)
void pc_draw_guides(ImDrawList* dl, float x, float y, float right, float size) {
    const Look k = touch_look();
    ImFont* font = ImGui::GetFont();
    SettingsIcons::Ink ink;
    ink.plate = k.plate, ink.rim = k.edge, ink.rule = k.rule, ink.letter = k.ink;
    const bool pad = controls_last_device() == 2 && controls_real_pad();
    const bool keys = s.view == View::Keyboard;
    const bool remove = picker_removable();
    // (glyph: a face button 0..3, 4 L, 5 R, 6 the d-pad; key: the keyboard's)
    struct Item {
        int glyph;
        const char* key;
        const char* label;
        bool on;
    };
    const Item items[] = {
        {6, "\xE2\x86\x91\xE2\x86\x93", "Move", true},
        {0, "Enter", keys ? "Type" : "Confirm", true},
        {1, "Esc", "Back", true},
        {2, remove ? "Del" : "Ctrl+S", keys ? "Delete" : remove ? "Remove" : "Save", keys || remove || (dirty() && !confirmation())},
        {3, "H", keys ? "Space" : "Help", keys || !s.dropdown.open || !s.dropdown.help.empty()},
        {5, "Tab", keys ? "Cursor" : "Sections", s.view == View::Main || s.view == View::Page || keys},
    };
    const float text = std::floor(size * .78f), gap = size * .9f;
    auto glyph = [&](ImDrawList* d, ImVec2 p, const Item& it, const SettingsIcons::Ink& use) -> float {
        if (!pad) {
            if (keys && it.glyph == 1) return pc_key_cap(d, font, p, size, "Esc", use);
            if (keys && (it.glyph == 2 || it.glyph == 3)) return pc_key_cap(d, font, p, size, it.glyph == 2 ? "Bksp" : "Space", use);
            if (keys && it.glyph == 5) return pc_key_cap(d, font, p, size, "\xE2\x86\x90\xE2\x86\x92", use);
            return pc_key_cap(d, font, p, size, it.key, use);
        }
        if (it.glyph == 6) {
            if (d) SettingsIcons::draw(d, font, p, size, SettingsIcons::Dpad, use);
            return size;
        }
        if (it.glyph >= 4) {  // (the shoulders: L1 / R1, LB / RB, L / R)
            const int look = pc_icon_look(s.draft.button_icons);
            const char* name = look == 1 ? "LB  RB" : look == 2 ? "L1  R1" : "L  R";
            return pc_key_cap(d, font, p, size, name, use);
        }
        return d ? pc_face_glyph(d, font, p, size, it.glyph, pc_icon_look(s.draft.button_icons), use) : size;
    };
    float total = 0;
    for (const auto& it : items) total += glyph(nullptr, ImVec2(0, 0), it, ink) + size * .3f + font->CalcTextSizeA(text, FLT_MAX, 0, tr(it.label)).x + gap;
    x = std::max(x, right - total + gap);
    for (const auto& it : items) {
        SettingsIcons::Ink use = ink;
        if (!it.on) use.letter = fade(use.letter, .35f), use.rule = fade(use.rule, .35f), use.rim = fade(use.rim, .5f);
        x += glyph(dl, ImVec2(x, y), it, use) + size * .3f;
        const char* label = tr(it.label);
        ImVec2 ts = font->CalcTextSizeA(text, FLT_MAX, 0, label);
        dl->AddText(font, text, ImVec2(x, y + (size - ts.y) * .5f), it.on ? k.dim : fade(k.dim, .4f), label);
        x += ts.x + gap;
    }
}

