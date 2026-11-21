// MH4U for PC: the settings' rows and texts of what pc/pc_files.cpp and the builder do on a PC, where Android's would
// say a phone's: System > Your game's log (MHGU for PC's Log folder, in place of Share log), System > Game code's help
// (the builder closes the game and opens on the game folder screen), Credits & Licenses' Licenses, and Mods & Cheats >
// Texture packs' folder. Included by settings_panel.cpp off Android only.

// System > Your game: the log's folder in File Explorer, log.txt picked (pc_files.cpp showShareLog)
void pc_log_row(std::vector<Row>& r) {
    action(r, "log.open", say("Log folder", "Carpeta del registro"), []() { app_call_activity("showShareLog"); });
    r.back().help = say("Opens the folder with log.txt, the game's log, to send it to whoever is helping you with a problem.",
                        "Abre la carpeta con log.txt, el registro del juego, para mandárselo a quien te ayude con un problema.");
}

// System > Game code: the builder of MH4U.exe (pc/pc_builder.cpp, its page on the game folder screen)
const char* pc_build_help() {
    return say("Makes the game code (mh4u_code_native.dll) again on this PC, from your own game and update in the game "
               "folder, with the code mods that are on: the game asks first, closes and opens its builder. The first time "
               "it downloads its tools (about 90 MB); it takes about 10 to 40 minutes, depending on the PC. Save in the "
               "game first.",
               "Vuelve a crear el código del juego (mh4u_code_native.dll) en esta PC, a partir de tu propio juego y su "
               "actualización de la carpeta del juego, con los mods de código que estén activados: el juego pregunta "
               "antes, se cierra y abre su creador. La primera vez descarga sus herramientas (unos 90 MB); tarda de unos "
               "10 a 40 minutos, según la PC. Guarda la partida antes.");
}

// Credits & Licenses: the licenses folder that comes in the MH4U-PC zip, beside MH4U.exe
const char* pc_licenses_help() {
    return say("Opens the licenses folder beside MH4U.exe: the licenses of the software this program is made with, and "
               "this program's own with its additional terms.",
               "Abre la carpeta licenses junto a MH4U.exe: las licencias del software con que está hecho este programa, y "
               "la de este programa con sus términos adicionales.");
}

// Mods & Cheats > Texture packs: the game folder's textures/ in File Explorer (made when it isn't there yet)
MENU_ENTRY("mods/textures", 15, [](std::vector<Row>& r) {
    action(r, "textures.open", say("Open the textures folder", "Abrir la carpeta textures"),
           []() { app_call_activity("openTextures"); });
    r.back().help = say("Opens your game folder's textures folder in File Explorer, and makes it if it isn't there yet. "
                        "A pack put there is used the next time the game starts.",
                        "Abre la carpeta textures de tu carpeta del juego en el Explorador de archivos, y la crea si todavía "
                        "no está. Un pack que pongas ahí se usa la próxima vez que abras el juego.");
});
