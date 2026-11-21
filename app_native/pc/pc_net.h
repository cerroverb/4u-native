// MH4U for PC: the program's side of multiplayer (pc_net.cpp): Discord Rich Presence (pc_discord.h) and the chat key, which
// opens the chat over the game with its keyboard ready for a line (the PC has no chat button; MHGU for PC's ChatKey).
// Rooms and hubs are the Android app's (net_rooms.cpp), rooms on this network pc_net_lan.cpp's.
#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

void pc_net_start();                      // the program's start (once the settings are read)
void pc_net_frame();                      // each frame of the main loop
bool pc_net_event(const SDL_Event& e);    // the chat key over the game: true when it was taken
// (tests) MH4U_TEST_NET's steps at the game frame `frame`, each once, separated by spaces or commas:
//   f<frame>:CLICK:<x>:<y>  a left click at that place of the window (fractions of its size: the settings' rows)
//   f<frame>:TYPE:<text>    text typed ('_' for a space), as a keyboard types it into a text the menu has open
// (tests) MH4U_TEST_LIVE=<file>: lines appended to it while the game runs, each done once as it comes (a tester's hands;
// frames here are 60ths of a second by the clock, the menu open or not):
//   KEY <SDL's name of a key, '_' a space> <frames held>    CLICK <x> <y>    TAP <x> <y> <frames> (a finger)
//   TYPE <text>    MENU <page>[#<row>]
void pc_net_test(unsigned frame, SDL_Window* window);

// The chat key's choices ([Controls] ChatKey: SDL's names of scancodes, "" none) and how one shows on this keyboard
std::vector<std::string> pc_chat_keys();
std::string pc_chat_key_label(const std::string& key, bool spanish);
