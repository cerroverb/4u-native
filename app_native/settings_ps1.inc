// PS1 style (1.0.1.1, the owner's; the MHGU port has the same two things and names), included by settings_panel.cpp:
//   PS1 filter ([Graphics] Ps1Filter = on|off in mh4u-app.ini, off as the app comes; a row on Graphics & Audio): the look
//     alone, no setting of the game changes. The renderer (mh4n, app_live_ps1): the 3D world's textures sampled nearest at
//     an eighth of their size (the blocks of the player's picture that asked for it), and its vertices snapped to a coarse
//     grid with affine textures (the PS1's wobble and warp;
//     new shaders, built the first time they are drawn). The app (shared_screens.h): the top screen on the 3DS's own pixel
//     grid, the CRT era's colours (more saturation and contrast), 15-bit colour with the PS1's ordered dither, faint
//     scanlines and the dither's shimmer, after the colour filter; never the
//     touch screen, the touch controls or this menu. The screens follow the row while the menu is open; the renderer's part
//     applies at Save.
//   Brick (Low-end mode's level below Toaster, settings_panel.cpp low_end): speed only, no effect that costs: what Toaster
//     sets, and the 3D world's textures at a sixteenth of their size, sampled nearest, and the top screen's pixels sharp.
void ps1_rows(std::vector<Row>& r) {
    add_bool(r, "ps1", en_es("PS1 filter", "Filtro PS1"), &s.draft.ps1_filter, "Ps1Filter");
    r.back().help = en_es(
        "A retro look of the PS1 era: textures in big blocks, wobbling polygons with warping textures, and the top screen "
        "shown on the console's own pixel grid with the vivid colours of a CRT TV, cut to 15-bit colour with its dot pattern "
        "and faint TV scanlines. Only the look changes: the resolution, the frame rate and the other graphics settings stay as "
        "you set them (for speed, see Low-end mode: Brick). The game's HUD keeps its own textures and shapes; the touch screen "
        "and the touch controls stay as they are. The first time it is on, the 3D shaders are built again (a short wait, or a "
        "few stutters, in each new place). Off: the picture as before.",
        "Un aspecto retro de la época de la PS1: texturas en bloques grandes, polígonos que tiemblan con texturas que se "
        "deforman, y la pantalla de arriba en la grilla de píxeles de la propia consola con los colores vivos de un televisor "
        "de tubo, en colores de 15 bits con su trama de puntos y líneas de televisor suaves. Solo cambia el aspecto: la "
        "resolución, los FPS y los demás ajustes gráficos quedan como los pusiste (para ganar velocidad, mira Modo gama baja: "
        "Ladrillo). El HUD del juego mantiene sus texturas y formas; la pantalla táctil y los controles táctiles quedan como "
        "están. La primera vez que se activa, los shaders 3D se arman de nuevo (una espera corta, o algunos tirones, en cada "
        "lugar nuevo). Apagado: la imagen como antes.");
}
void reset_ps1() { s.draft.ps1_filter = defaults().ps1_filter; }
// (1.0.2: before Shadows, in the MHGU port's place)
MENU_ENTRY("graphics", 37, ps1_rows, reset_ps1);
