// The port's chat (S32, after tabarong's design PORT_CHAT.md): three chats and friends, with no server of the project's
// and no account. The same files are in both ports (MHGU: runtime/chat, MH4U for Android: app_native/chat); each port
// gives what differs (Port below) and draws it in its own menu.
//
//   - Hub: the hub (MHGU) or room (MH4U) the player is in, over the link the games already keep between them: MHGU's
//     F_AUX messages, which the hub's host checks and passes on (MHGU runtime/chat_port.cpp), MH4U's room chat
//     (Azahar's, so an emulator's players in the room read it too). Nothing else is needed: no relay, no notice.
//   - Global (both games, every port's player) and General (this game's), friends' private messages and whether a
//     friend is online: Nostr events on open public relays (wss://, the player can change the list). Ephemeral events
//     (kinds 20000-29999): a relay passes them on to whoever is connected and keeps none, so the chat works only while
//     connected, and nothing said is archived. Private messages are encrypted (NIP-44 v2): the relays see who writes to
//     whom, not what. The relays are behind chat_ws.h, and the Nostr part behind a few functions here: another transport
//     can take their place.
//
// The player's identity is a key made on the device the first time (chat.txt), shown as a friend code (XXXX-XXXX-XXXX:
// chat_crypto.h). Every message is signed with it, so nobody can write as someone else. Friends, mutes, blocks and the
// chat's settings live in the same file: deleting the app loses them, and that is fine. Nothing said is written to disk.
//
// Moderation is each player's own: mute (their lines no longer show) and block (no lines, private messages, friend
// requests or presence; out of the friends) act only on the device of the player who does it. Against spam: a player
// can send a line every few seconds and the same line once a minute, and someone who sends too many is silenced here for
// a few minutes.
//
// Threads: every function may be called from the interface's thread; the relays are served by a thread of their own,
// the hub's messages arrive on the link's threads (hub_line).
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace chat {

enum Channel { GLOBAL, GENERAL, HUB, FRIEND, CHANNELS };
enum Status { OFF, CONNECTING, ONLINE, NO_RELAY, UNAVAILABLE };
enum Share { SHARE_NONE, SHARE_KEY, SHARE_ROOM };  // MHGU: hub server + matchmaking key; MH4U: room address + password
enum Overlay { OVERLAY_OFF, OVERLAY_HUB_FRIENDS, OVERLAY_ALL };

struct Line {
    unsigned serial = 0;     // goes up with each line, in every chat
    int channel = GLOBAL;
    std::string peer;        // FRIEND: the friend's key (the conversation it belongs to)
    std::string from;        // who: a key (hex), "hub:<name>" (an unsigned hub line), "room:<nickname>" (MH4U's room)
    std::string name, text;
    std::string game;        // "mhgu" / "mh4u" (Global)
    bool mine = false, system = false;
    int share = SHARE_NONE;
    std::string share_a, share_b;  // the server and key, or the room's address and password
    int64_t at = 0;          // unix time
};

struct Person {
    std::string id;          // a key (hex), or "room:<nickname>" / "hub:<name>" (muted or blocked only)
    std::string code, name;  // the friend code (a key's), the name it last had
    bool is_friend = false, pending = false;  // pending: added by code, not seen online yet (id "code:<code>")
    bool mutual = false;     // a friend who added us too: only then do private messages go both ways
    bool request = false;    // asked to be our friend
    bool muted = false, blocked = false;
    bool online = false, in_hub = false;
    std::string game;
    int64_t seen = 0;        // unix time of its last presence
};

struct Port {
    std::string game;        // "mhgu" or "mh4u": General's chat
    std::string app;         // "MHGU for Android", for the log
    std::string dir;         // where chat.txt is (the app's files, the settings' folder)
    std::string java_class;  // Android: the app's ChatSocket ("com/mhgu/recomp/ChatSocket")
    // the hub or room this device is in: up (its label, for the chat's title), and a line said in it (false: not sent)
    std::function<bool(std::string *label)> hub_up;
    std::function<bool(const std::string &text)> hub_send;
    std::function<std::string()> default_name;  // the hunter's name, or the room nickname
    std::function<void(const char *)> log;
};

void init(const Port &port);
std::string game();  // the port's ("mhgu", "mh4u")
void tick();  // each frame, from the interface: the hub's state

// settings (at once, in chat.txt). Both chats start off (1.0): while both are, the chat takes no part in the game - nothing
// over it, no button, no hub or room watched (active() false)
bool enabled();                   // the relays (Global, General, friends) are on
void set_enabled(bool on);        // (the port shows the notice about the relays first: notice_seen)
bool hub_chat();                  // the hub's (MHGU) or room's (MH4U) chat is on: its lines read and shown, said in it
void set_hub_chat(bool on);       //   (off: MHGU passes the hub's lines on unread, MH4U's room shows its own as before)
bool active();                    // either chat is on (each frame, the link's threads: no lock taken)
bool notice_seen();
void set_notice_seen();
std::string name();               // as others see it
std::string own_name();           // what was typed ("" : the hunter's)
void set_name(const std::string &n);
std::vector<std::string> relays();
std::vector<std::string> default_relays();
void set_relays(const std::vector<std::string> &list);  // empty: the default ones
int overlay();                    // Overlay: which lines show over the game
void set_overlay(int o);
bool bubble(float *x, float *y, float *size);  // the chat button's place (fractions) and size (1: its own); false: none saved
void set_bubble(float x, float y, float size);
bool bubble_shown();
void set_bubble_shown(bool on);
// (1.0.2) the chat window (the menu's chat page on a phone): its place and size, fractions of the screen (x, y its top
// left); false: none saved, the menu's own. reset_window: the menu's own again
bool window(float *x, float *y, float *w, float *h);
void set_window(float x, float y, float w, float h);
void reset_window();
// (1.0.2, after tabarong's floating chat) how solid the chat window is: its background, its lines' strips and the field to
// type in, each a percent from 0 (see-through) to 100 (solid), in steps of 5; the text is always solid. In chat.txt
// ("opacity"; an older version skips the line), saved as the rest of it (chat.txt.tmp, then put in its place)
enum Opacity { OPACITY_WINDOW, OPACITY_LINES, OPACITY_FIELD, OPACITIES };
int opacity(int which);
void set_opacity(int which, int percent);
bool flag(const std::string &name);  // the port's own marks kept in chat.txt (a notice seen...)
void set_flag(const std::string &name);

Status status(int *up = nullptr, int *total = nullptr);
std::string status_reason();      // the last relay's error, for the explanation
int online_count();               // players whose presence came in the last minutes
std::string my_code();            // XXXX-XXXX-XXXX
std::string my_key();             // hex
bool hub_up(std::string *label = nullptr);

// the lines of a chat (FRIEND: with peer), the last max
std::vector<Line> lines(int channel, const std::string &peer = "", size_t max = 60);
std::vector<Line> since(unsigned serial);  // every line after that one (the notices over the game)
unsigned last_serial();
int unread(int channel, const std::string &peer = "");  // FRIEND without a peer: all friends'
int unread_total();
void mark_read(int channel, const std::string &peer = "");
// says something (false, why: not connected, too fast...); a shared key or room goes with its text
bool say(int channel, const std::string &peer, const std::string &text, std::string *why);
bool share(int channel, const std::string &peer, int kind, const std::string &a, const std::string &b, std::string *why);
size_t max_text();                // bytes

// people
std::vector<Person> people();
bool add_friend(const std::string &typed_code, std::string *why);
bool add_friend_key(const std::string &key, const std::string &name);  // from a line of theirs
void accept(const std::string &key);
void remove(const std::string &id);  // a friend, a request, a pending code
void set_muted(const std::string &id, const std::string &name, bool on);
void set_blocked(const std::string &id, const std::string &name, bool on);
bool is_muted(const std::string &id);
bool is_blocked(const std::string &id);
std::string person_name(const std::string &id);

// the port's hub link: a line that came (from: a key if the line was signed, else "hub:<name>" or "room:<name>"); its
// own lines too (mine), as its link sends them (none kept while the hub's chat is off); the hub gone (its lines cleared)
void hub_line(const std::string &from, const std::string &name, const std::string &text, bool mine);
void hub_reset();
// a line for the hub's link of MHGU: signed with the player's key and bound to the hub (its id), and its check
bool hub_sign(const std::string &hub_id, int64_t at, const std::string &name, const std::string &text, uint8_t pub[32], uint8_t sig[64]);
bool hub_verify(const std::string &hub_id, int64_t at, const std::string &name, const std::string &text, const uint8_t pub[32],
                const uint8_t sig[64]);

// S35: a new matchmaking key / room password: 32 lowercase hex digits (128 random bits), made on the device
std::string new_key();

// (1.0.2, tests: a port's test aid - MHGU_CHAT_TEST in a DEV build's env.txt, MH4U_CHAT_TEST in launch.txt's env=) a line
// in that chat as if someone had said it (FRIEND: that friend's, peer their key); nothing is sent anywhere
void test_line(int channel, const std::string &peer, const std::string &name, const std::string &text);
// ...and test_lines: the port's own lines, a few at once and one more every 10 s (the window's tabs, the lines beside the
// button). The first friend's lines are its private ones
void test_lines();

}  // namespace chat
