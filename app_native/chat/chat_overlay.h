// The chat over the game (chat_overlay.cpp, Dear ImGui): the chat button (tabarong's bubble: a tap opens the chat, a drag
// moves it; the number of lines not read on it) and the last lines for a few seconds beside it, as the chat's "Lines over
// the game" says. The same file is in both ports; each says when its touch controls show, when its menu is open, and how
// the chat opens.
#pragma once
#include <functional>
#include <string>

namespace chat {

struct OverlayPort {
    std::function<bool()> controls_shown;  // the touch controls are on the screen (the button shows with them)
    std::function<bool()> menu_open;       // the port's menu is open (nothing of this then)
    std::function<void()> open_chat;       // a tap on the button: the menu's chat page
    // (1.0.2) the button's place and size, as the touch controls' editor has it (its centre, fractions of the screen,
    // and its radius, a fraction of the screen's height): false when the port has none. With one, the button is placed
    // and sized in the editor alone (a drag on it does nothing; a tap opens the chat)
    std::function<bool(float *x, float *y, float *r)> place;
    // (1.0.2) no lines beside the button now (those up go at once): a menu of the game's is open, or the chat window is up
    // (its lines are in it); the button stays. Unset: only the port's menu (menu_open) hides them
    std::function<bool()> lines_hidden;
    std::function<void(const char *)> log;
};
// (1.0.2, after tabarong's floating chat) a colour of its own for each player's name, the same name always the same one
// (the lines beside the button, the chat window's), none of them the chats' own colours; RGBA as Dear ImGui's IM_COL32
unsigned name_ink(const std::string &name);
unsigned channel_ink(int channel);  // (the chats' own: the hub's light blue, General green, Global gold, a friend pink)
void overlay_setup(const OverlayPort &port);
bool overlay_frame();                 // each frame: true while something is to be drawn (never while both chats are off)
void overlay_draw(float w, float h);  // then, over the touch controls (the foreground list)
// a finger that landed on no control (fractions of the screen): true when the button took it; its moves and its lift
bool bubble_down(float fx, float fy);
void bubble_finger(bool up, float fx, float fy);
void bubble_size(float s);  // 1: its own; .6 to 2
float bubble_scale();
void bubble_reset();        // its place and size as they come

}  // namespace chat
