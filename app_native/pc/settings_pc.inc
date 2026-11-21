// MH4U for PC: the PC's own settings rows (settings_pc.h), included by settings_panel.cpp (Windows only): MHGU for PC's
// same rows and words where it has them.
void open_page(const std::string& id);  // (further down settings_panel.cpp)
}  // namespace

void pc_draft_load(PcDraft& d) {
    const std::string mode = app_setting("Display", "WindowMode", "borderless");
    d.window_mode = mode == "windowed" ? 1 : mode == "fullscreen" ? 2 : 0;
    d.fullscreen_mode = app_setting("Display", "FullscreenMode");
    d.vsync = app_setting_bool("Graphics", "VSync", true);
    const std::string up = app_setting("Graphics", "Upscale", "smooth");
    d.upscale = up == "sharp" ? 1 : up == "nearest" ? 2 : up == "fsr1" ? 3 : 0;  // (1.0.2: FSR 1 too, the Upscale filter)
    const std::string stats = app_setting("Graphics", "PcStats", "");
    d.stats = stats == "full" ? 2 : stats == "basic" ? 1 : stats == "off" ? 0 : app_setting_bool("Graphics", "FpsCounter", false) ? 1 : 0;
    d.hud_anchor = game_option_get("hud_anchor", "whole");
    d.gpu = app_setting("Graphics", "Gpu");
    d.hdr = app_setting_bool("Colour", "HDR", false);
    d.hdr_brightness = std::clamp(app_setting_int("Colour", "HDRBrightness", 0), -100, 100);
    d.hdr_highlights = std::clamp(app_setting_int("Colour", "HDRHighlights", 0), -100, 100);
}

void pc_draft_save(const PcDraft& d, const PcDraft& o) {
    static const char* const kModes[] = {"borderless", "windowed", "fullscreen"};
    static const char* const kUpscale[] = {"smooth", "sharp", "nearest", "fsr1"};
    static const char* const kStats[] = {"off", "basic", "full"};
    app_setting_set("Display", "WindowMode", kModes[std::clamp(d.window_mode, 0, 2)]);
    if (d.window_mode != 1) app_setting_set("Display", "FullscreenKind", kModes[std::clamp(d.window_mode, 0, 2)]);
    app_setting_set("Display", "FullscreenMode", d.fullscreen_mode);
    app_setting_set("Graphics", "VSync", d.vsync ? "on" : "off");
    app_setting_set("Graphics", "Upscale", kUpscale[std::clamp(d.upscale, 0, 3)]);
    app_setting_set("Graphics", "PcStats", kStats[std::clamp(d.stats, 0, 2)]);
    app_setting_set("Graphics", "Gpu", d.gpu);
    app_setting_set("Colour", "HDR", d.hdr ? "on" : "off");
    app_setting_set_int("Colour", "HDRBrightness", d.hdr_brightness);
    app_setting_set_int("Colour", "HDRHighlights", d.hdr_highlights);
    if (d.hud_anchor != o.hud_anchor) game_option_set("hud_anchor", d.hud_anchor);
}

// (tests: pc_main.cpp's MH4U_TEST_MENU) the settings shown at a section ("graphics") or a page ("graphics/screen"), and
// after a '#' the row picked ("graphics/screen#pc.window")
void pc_settings_test_page(const std::string& at) {
    const size_t hash = at.find('#');
    const std::string id = at.substr(0, hash);
    const size_t slash = id.find('/');
    for (int i = 0; i < PAGES; ++i)
        if (id.compare(0, slash, page_ids[i]) == 0) first_page(i);
    if (slash != std::string::npos) open_page(id);
    if (hash != std::string::npos) focus(at.substr(hash + 1));
}
void pc_settings_test_save() { s.save_request = true; }

void pc_stats_apply() {
    PcDraft d;
    pc_draft_load(d);
    const float fps = std::max(1.f, (float)std::atof(game_option_get("fps", "60").c_str()));
    perfstats_set(d.stats > 0, fps, settings_spanish());
    perfstats_set_pc_level(d.stats);
}

namespace {

// Screen and HUD: VSync, after the window mode and the screen resolution, as MHGU for PC's 1.0.2 menu (with it on,
// the monitor's refresh rate is the most)
void pc_rows_vsync(std::vector<Row>& r) {
    add_bool(r, "pc.vsync", en_es("VSync", "VSync"), &s.draft.pc.vsync);
    r.back().help = en_es("On: each picture waits for the monitor's refresh, so nothing tears, and the frame rate goes no "
                          "higher than the monitor's refresh rate. Off: every picture the game makes is shown at once, up "
                          "to the frame rate limit or with no limit at all; the picture may tear.",
                          "Activado: cada imagen espera el refresco del monitor, así nada se corta, y los FPS no pasan la "
                          "frecuencia del monitor. Desactivado: cada imagen que hace el juego se muestra enseguida, hasta "
                          "el límite de FPS o sin límite; la imagen puede cortarse.");
}
void pc_reset_vsync() { s.draft.pc.vsync = true; }
MENU_ENTRY("graphics/screen", 12, pc_rows_vsync, pc_reset_vsync);

// Graphics & Audio > Screen and HUD: the window, first
void pc_rows_window(std::vector<Row>& r) {
    PcDraft& d = s.draft.pc;
    add_choice(r, "pc.window", en_es("Window mode", "Modo de ventana"), d.window_mode,
               {en_es("Borderless", "Sin bordes"), en_es("Windowed", "Ventana"), en_es("Full screen", "Pantalla completa")},
               [](int v) { s.draft.pc.window_mode = v; });
    r.back().help = en_es("Borderless: the whole screen at your desktop's resolution, as a window without edges (switching "
                          "to other programs is instant). Windowed: a window of any size; the picture takes its shape as "
                          "you drag its edges. Full screen: the screen for the game alone, at the resolution below. F11 or "
                          "Alt+Enter switches between the window and the full screen at any time.",
                          "Sin bordes: toda la pantalla a la resolución del escritorio, como una ventana sin bordes (pasar "
                          "a otros programas es inmediato). Ventana: una ventana de cualquier tamaño; la imagen toma su "
                          "forma mientras arrastras sus bordes. Pantalla completa: la pantalla solo para el juego, a la "
                          "resolución de abajo. F11 o Alt+Enter cambian entre la ventana y la pantalla completa cuando "
                          "quieras.");
    std::vector<std::string> modes = pc_display_modes(pc_window());
    std::vector<std::string> names;
    int pick = 0;
    for (size_t i = 0; i < modes.size(); ++i) {
        int mw = 0, mh = 0, hz = 0;
        std::sscanf(modes[i].c_str(), "%dx%d@%d", &mw, &mh, &hz);
        std::string n = std::to_string(mw) + " x " + std::to_string(mh) + " \xC2\xB7 " + std::to_string(hz) + " Hz";
        names.push_back(i == 0 ? n + en_es(" (desktop)", " (escritorio)") : n);
        if (modes[i] == d.fullscreen_mode) pick = static_cast<int>(i);
    }
    if (names.empty()) names.push_back(en_es("The desktop's", "La del escritorio"));
    add_choice(r, "pc.fsmode", en_es("Screen resolution", "Resolución de pantalla"), pick, names, [modes](int v) {
        s.draft.pc.fullscreen_mode = v <= 0 || v >= static_cast<int>(modes.size()) ? "" : modes[v];
    });
    r.back().enabled = d.window_mode == 2;
    r.back().help = en_es("The resolution and refresh rate of Full screen. Borderless always uses the desktop's.",
                          "La resolución y la frecuencia de Pantalla completa. Sin bordes usa siempre la del escritorio.");
}
void pc_reset_window() {
    s.draft.pc.window_mode = 0;
    s.draft.pc.fullscreen_mode.clear();
}
MENU_ENTRY("graphics/screen", 10, pc_rows_window, pc_reset_window);

// Screen and HUD: the HUD anchor (MHGU's: the HUD over the middle 16:9, 21:9 or 32:9 of a wider screen, or its whole width),
// after HUD at the screen's edges and before HUD size, as MHGU for PC's 1.0.2 menu (HUD anchor, then HUD size)
void pc_rows_anchor(std::vector<Row>& r) {
    static const char* const kKeys[] = {"whole", "16:9", "21:9", "32:9"};
    int pick = 0;
    for (int i = 0; i < 4; ++i)
        if (s.draft.pc.hud_anchor == kKeys[i]) pick = i;
    add_choice(r, "pc.anchor", en_es("HUD anchor", "Anclaje del HUD"), pick,
               {en_es("Whole width", "Todo el ancho"), "16:9", "21:9", "32:9"},
               [](int v) { s.draft.pc.hud_anchor = kKeys[std::clamp(v, 0, 3)]; }, "hud_anchor");
    const bool wide = !(s.draft.widescreen.empty() || s.draft.widescreen == "0" || !strcasecmp(s.draft.widescreen.c_str(), "off"));
    r.back().enabled = wide && is_on(s.draft.hud_edges);
    r.back().help = en_es("On a wide screen, how far out the HUD's sides go: to the screen's edges, or to those of a 16:9, "
                          "21:9 or 32:9 area in its middle, nearer your eyes on an ultra-wide monitor. Needs HUD at the "
                          "screen's edges.",
                          "En una pantalla ancha, hasta dónde van los costados del HUD: hasta los bordes de la pantalla, o "
                          "hasta los de una zona 16:9, 21:9 o 32:9 en el medio, más cerca de la vista en un monitor "
                          "ultra ancho. Necesita HUD en los bordes de la pantalla.");
}
void pc_reset_anchor() { s.draft.pc.hud_anchor = "whole"; }
MENU_ENTRY("graphics/screen", 72, pc_rows_anchor, pc_reset_anchor);

// Screen and HUD: Show FPS and PC stats (MHGU's: Basic, and Full with the GPU's and the CPU's lines)
void pc_rows_stats(std::vector<Row>& r) {
    add_choice(r, "stats", en_es("Show FPS and PC stats", "Mostrar FPS y datos de la PC"), std::clamp(s.draft.pc.stats, 0, 2),
               {tr("Off"), en_es("Basic", "Básico"), en_es("Full", "Completo")}, [](int v) {
                   s.draft.pc.stats = v;
                   s.draft.stats = v > 0;
               });
    r.back().help = en_es("Basic shows the frame rate, the frame time, the longest frame and the GPU's temperature. Full adds "
                          "a line on the GPU (use, clock, video memory) and one on the CPU (the game's use and memory, the "
                          "processor's clock). Windows doesn't let a program read the CPU's temperature.",
                          "Básico muestra los FPS, el tiempo de cada cuadro, el cuadro más largo y la temperatura de la GPU. "
                          "Completo agrega una línea sobre la GPU (uso, frecuencia, memoria de video) y otra sobre la CPU "
                          "(el uso y la memoria del juego, la frecuencia del procesador). Windows no deja que un programa "
                          "lea la temperatura de la CPU.");
}
void pc_reset_stats() {
    s.draft.pc.stats = 0;
    s.draft.stats = false;
}
MENU_ENTRY("graphics/screen", 85, pc_rows_stats, pc_reset_stats);

// Colour: the HDR output (pc_hdr.h; the MHGU port's rows and words). Shown where Windows' HDR is on for the window's
// display, and while it is On (to turn it off); On where it couldn't start says so
bool pc_hdr_shown() { return s.draft.pc.hdr || pc_hdr_state() != 0 || pc_hdr_display(pc_window()); }
void pc_rows_hdr(std::vector<Row>& r) {
    PcDraft& d = s.draft.pc;
    header(r, "hdr.head", "HDR");
    add_bool(r, "hdr", "HDR output", &d.hdr);
    r.back().title = en_es("HDR output", "Salida HDR");
    r.back().cold = true;
    r.back().help = en_es("On a screen with HDR, the bright parts of the game's picture (the sky, fire, lightning) shine brighter "
                          "than the rest, as in HDR video. The game draws an ordinary picture, so this spreads its light out; "
                          "it adds no detail. The touch controls and the menus stay at the picture's white. Needs a restart, "
                          "and Windows' HDR on for the screen (Settings > Display > Use HDR).",
                          "En una pantalla con HDR, las partes brillantes de la imagen del juego (el cielo, el fuego, los "
                          "rayos) brillan más que el resto, como en un video HDR. El juego dibuja una imagen común, así que "
                          "esto reparte su luz; no agrega detalle. Los controles táctiles y los menús quedan en el blanco de "
                          "la imagen. Necesita reiniciar, y el HDR de Windows activado para la pantalla (Configuración > "
                          "Pantalla > Usar HDR).");
    const int state = pc_hdr_state();
    if (d.hdr && (state == 2 || state == 3)) {  // (On since the start, and the picture stayed SDR)
        r.back().value = en_es("On (not available)", "Activada (no disponible)");
        r.back().help = state == 2 ? en_es("Windows' HDR was off for this screen when the game started: the picture stayed as it "
                                           "is. Turn on Settings > Display > Use HDR and restart the game.",
                                           "El HDR de Windows estaba apagado para esta pantalla cuando empezó el juego: la "
                                           "imagen quedó como está. Activa Configuración > Pantalla > Usar HDR y reinicia el juego.")
                                   : en_es("The graphics driver can't show the HDR picture here (the log says why): the "
                                           "picture stayed as it is.",
                                           "El driver gráfico no puede mostrar la imagen HDR acá (el registro dice por qué): "
                                           "la imagen quedó como está.");
    }
    if (!d.hdr) return;
    add_number(r, "hdr.bright", "HDR brightness", &d.hdr_brightness, -100, 100, 10, "%d");
    char nits[32];
    std::snprintf(nits, sizeof nits, "%.0f nits", 203.0 * std::exp2(d.hdr_brightness / 100.0));
    r.back().title = en_es("HDR brightness", "Brillo HDR");
    r.back().value = nits;
    r.back().help = en_es("The picture's white in HDR: higher if the game looks dim next to the rest of the desktop.",
                          "El blanco de la imagen en HDR: más alto si el juego se ve apagado al lado del resto del escritorio.");
    colour_number(r, "hdr.high", "HDR highlights", "Realces HDR", &d.hdr_highlights, nullptr, 100, "",
                  "How far the brightest parts go towards the screen's peak: -100 not at all (the ordinary picture), 100 all the way.",
                  "Cuánto suben las partes más brillantes hacia el máximo de la pantalla: -100 nada (la imagen común), 100 todo.");
}
void pc_reset_hdr() {
    s.draft.pc.hdr = false;
    s.draft.pc.hdr_brightness = s.draft.pc.hdr_highlights = 0;
}
MENU_ENTRY("graphics/colour", 50, pc_rows_hdr, pc_reset_hdr, pc_hdr_shown);

// (1.0.2) The 3D picture's filter as it is scaled to the window (Smooth, Sharp pixels, Nearest) is the Upscale filter's
// row now, with FSR 1, where Android has it (settings_panel.cpp rows_graphics_35): [Graphics] Upscale, the same key

// Advanced: the GPU (MHGU's: on a PC with two graphics cards, the one the game runs on)
void pc_rows_gpu(std::vector<Row>& r) {
    static const std::vector<std::string> gpus = pc_gpu_names();
    std::vector<std::string> names = {en_es("Automatic (a dedicated one if there is one)", "Automática (una dedicada si hay)")};
    int pick = 0;
    for (size_t i = 0; i < gpus.size(); ++i) {
        names.push_back(gpus[i]);
        if (gpus[i] == s.draft.pc.gpu) pick = static_cast<int>(i) + 1;
    }
    add_choice(r, "pc.gpu", en_es("GPU", "GPU"), pick, names,
               [](int v) { s.draft.pc.gpu = v <= 0 || v > static_cast<int>(gpus.size()) ? "" : gpus[v - 1]; });
    if (pick == 0 && !s.draft.pc.gpu.empty()) r.back().value = s.draft.pc.gpu;
    r.back().cold = true;
    r.back().help = en_es("On a PC with two graphics cards (a laptop with integrated and dedicated graphics), the one the game "
                          "runs on. Automatic takes a dedicated one if there is one. Takes effect when the game restarts.",
                          "En una PC con dos placas de video (una notebook con gráficos integrados y dedicados), en cuál "
                          "corre el juego. Automática toma una dedicada si hay. Se aplica al reiniciar el juego.");
}
void pc_reset_gpu() { s.draft.pc.gpu.clear(); }
MENU_ENTRY("graphics/advanced", 30, pc_rows_gpu, pc_reset_gpu);  // (MHGU for PC's place)
