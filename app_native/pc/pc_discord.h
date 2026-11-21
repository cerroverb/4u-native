// MH4U for PC: Discord Rich Presence, as MHGU for PC's (its runtime/net/discord.c and runtime/hle/presence.c): with the
// Discord app open on this PC, the player's profile there shows what they are doing: where they are (the menus, a town,
// a quest), the monsters hunted, the map and the area, the hunters with them, the weapon, and the hunter's name and HR.
// Only the Discord app on this PC is talked to (its local connection, a named pipe): nothing goes anywhere else.
//
// mh4u-app.ini [Discord] RichPresence (on as the app comes) and ShowHunter (on: the hunter's name and HR; off leaves them
// out), F1 > Multiplayer > Discord (pc/settings_pc_net.inc). What the game is doing comes from mh4n's port/src/presence.h
// (read from the game's memory only while this is on) and the game state; the names of the monsters and the maps are
// the game's own texts, in the game's language.
//   MH4U_DISCORD_APP=<id>   (tests) another Discord application
//   MH4U_LOG_DISCORD=1      every activity sent, in the log
#pragma once

#include <string>

// The connection's thread, once (the program's start); nothing is sent until pc_discord_frame says what.
void pc_discord_start();
// Each frame of the main loop (cheap: the activity is made again once a second): what Discord shows now.
void pc_discord_frame();
// For the settings: "" off, else the connection's state in the player's language.
std::string pc_discord_status(bool spanish);
