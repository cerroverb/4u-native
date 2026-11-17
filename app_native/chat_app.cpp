// MH4U for Android's side of the port's chat (app_native/chat/chat.h, the same files as MHGU's runtime/chat; the menu's
// Chat and Friends pages are settings_chat.inc's):
//   - the chat started with the app's interface: the room nickname for a name, chat.txt beside mh4u-app.ini, the app's
//     ChatSocket for the relays
//   - the room's chat as the chat's Room: the room's own chat (Azahar's, net_rooms.cpp), which an emulator's players in
//     the room read too. A line from it comes as "room:<nickname>" (the room says no more of who it is). Only with the
//     room chat on (off as it comes): off, the room's lines are not the chat's and show as in 0.1.9.3
//   - over the game: the chat button and the last lines (chat/chat_overlay.cpp), with this app's touch controls and menu
#include "chat_app.h"

#include <cstdio>
#include <string>

#include "app_settings.h"
#include "chat/chat.h"
#include "chat/chat_overlay.h"
#include "chat/chat_text.h"
#include "imgui.h"
#include "mh4n/hubs.h"
#include "mh4n/rooms.h"
#include "net_rooms.h"
#include "settings_panel.h"
#include "touch.h"
#include "ui.h"

namespace rooms = mh4n::rooms;

namespace {
bool g_started;
bool in_room(const rooms::Status& st) { return st.state == rooms::State::Joined || st.state == rooms::State::Moderator; }
void chat_log(const char* s) { std::fprintf(stderr, "%s\n", s); }
}  // namespace

void chat_app_init() {
    if (g_started) return;
    g_started = true;
    chat::Port p;
    p.game = "mh4u";
    p.app = "MH4U for Android";
    p.java_class = "com/mh4u/port/ChatSocket";
    std::string ini = app_settings_path();
    size_t slash = ini.find_last_of('/');
    p.dir = slash == std::string::npos ? std::string(".") : ini.substr(0, slash);
    p.hub_up = [](std::string* label) {
        const rooms::Status st = rooms::GetStatus();
        if (!in_room(st)) return false;
        if (label) *label = st.room.name;
        return true;
    };
    p.hub_send = [](const std::string& text) { return net_say(text); };  // (its line comes back through net_rooms.cpp)
    p.default_name = []() { return app_setting("Network", "Nick"); };
    p.log = chat_log;
    chat::init(p);
    chat::OverlayPort o;
    o.controls_shown = []() { return touch_controls_shown(); };
    o.menu_open = []() { return ui_settings_open(); };
    o.open_chat = []() {
        ui_open_settings();
        settings_panel_open_chat();
    };
    o.log = chat_log;
    chat::overlay_setup(o);
}

// S34: in a room, the step players miss (4u-native #45, Discord): the game's own Local Play in the Gathering Hall, with
// the room's chat on (both chats start off, and this notice with them: 0.1.9.3 had none)
void room_notice() {
    static bool was;
    const rooms::Status st = rooms::GetStatus();
    const bool now = in_room(st);
    if (now && !was && chat::hub_chat()) {
        if (mh4n::hubs::InHub(st) || (st.hosting && mh4n::hubs::GetHostStatus().active))  // (1.0.1: a hub is a room too)
            settings_panel_notice("In the hub. Now, in the game: go to the Gathering Hall and choose Local Play. One hunter "
                                  "creates a hall, the others search for it there. The chat button talks to the hub.",
                                  "Ya estás en el hub. Ahora, en el juego: ve al Gathering Hall y elige Local Play (juego local). "
                                  "Un cazador crea la sala del gremio y los demás la buscan ahí. El botón del chat habla con el hub.");
        else
            settings_panel_notice("In the room. Now, in the game: go to the Gathering Hall and choose Local Play. One hunter "
                                  "creates a hall, the others search for it there. The chat button talks to the room.",
                                  "Ya estás en la sala. Ahora, en el juego: ve al Gathering Hall y elige Local Play (juego local). "
                                  "Un cazador crea la sala del gremio y los demás la buscan ahí. El botón del chat habla con la sala.");
    }
    was = now;
}

void chat_app_frame() {
    if (!g_started) return;
    // (both chats off, as they come: no room watched, no notice, nothing over the game; the room's own lines show with
    // Room messages over the game, as in 0.1.9.3 - settings_panel.cpp draw_notices)
    if (!chat::active()) {
        chat::overlay_frame();
        return;
    }
    chat::set_spanish(settings_spanish());
    chat::tick();
    room_notice();
    if (!chat::overlay_frame()) return;
    ImGuiIO& io = ImGui::GetIO();
    chat::overlay_draw(io.DisplaySize.x, io.DisplaySize.y);
}

void chat_app_room_line(const std::string& nick, const std::string& text, bool mine) {
    if (!g_started) return;
    chat::hub_line(mine ? chat::my_key() : "room:" + nick, nick, text, mine);
}
