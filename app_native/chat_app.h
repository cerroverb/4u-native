// MH4U for Android's side of the port's chat (chat_app.cpp; the chat itself: chat/chat.h).
#pragma once
#include <string>

void chat_app_init();   // the interface's first frame: the chat started (its relays only if the player turned them on)
void chat_app_frame();  // each frame, the menu open or not: the chat's state, and over the game its button and last lines
// (1.0.2, tests) MH4U_CHAT_TEST=1 in launch.txt's env=: a room that isn't there, lines in each chat, and the chat window
// afloat even with a controller (a handheld's buttons): the floating window tried on a handheld
bool chat_app_test();
// a line of the room's chat (net_rooms.cpp): someone's, or this phone's as it was sent
void chat_app_room_line(const std::string& nick, const std::string& text, bool mine);
