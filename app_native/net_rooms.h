// The app's side of the network rooms (mh4n/rooms.h, Azahar's multiplayer rooms): joining, hosting and leaving from
// the settings panel's Multiplayer tab, the room's chat and notices kept as lines, and a lobby's list of rooms fetched
// on a thread of its own. The runtime's local-wireless service talks to the process's room client by itself, so a room
// joined here is the one the game's Local Play (the Gathering Hall) reaches; nothing is passed to the game.
//
// No server is built in: the room's address and the lobby's are the player's to type (empty: none).
// Everything here runs on the app's main thread (the interface's), but for the lobby's fetch, which has its own.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "mh4n/rooms.h"

// Something the room said, or this phone did in it, for the chat and the notices over the game
struct NetLine {
    enum Kind { Chat, Mine, Joined, Left, Kicked, Banned, Failed, YouJoined, YouLeft } kind = Chat;
    std::string nick, text;  // who; what they said (Chat, Mine), the room's name (YouJoined)
    mh4n::rooms::Error error = mh4n::rooms::Error::None;  // (Failed)
    double time = 0;         // seconds since the app started, when it came
    unsigned serial = 0;     // goes up by one with each line
};

// Why a join or a host request was not even tried
enum class NetProblem { None, Nickname, Address, Port, PortBusy };

// Each frame, whether the settings are open or not: the room's events into the lines (the last 60).
void net_poll();
const std::vector<NetLine>& net_lines();

// "host" or "host:port" (24872 when it has none): false when it is not an address the room client can join.
bool net_parse_address(const std::string& text, std::string* host, std::uint16_t* port);
// "1".."65535"
bool net_parse_port(const std::string& text, std::uint16_t* port);

// Joins the room at `address` (a room this phone hosts is closed first, unless it is that one). The answer comes as
// the client's state (mh4n::rooms::GetStatus).
NetProblem net_join(const std::string& address, const std::string& nickname, const std::string& password);
// Opens a room on this phone (UDP `port`, `slots` hunters, `password` or none) and joins it.
NetProblem net_host(const std::string& port, int slots, const std::string& nickname, const std::string& password);
// This phone's IPv4 addresses (not the loopback), the ones others join a room it hosts at: Wi-Fi's first
std::vector<std::string> net_local_addresses();
void net_leave();       // leaves the room (a room this phone hosts stays open)
void net_close_room();  // leaves, and closes the room this phone hosts
// Sends a chat line to the room (and keeps it as this phone's): false when not in a room.
bool net_say(const std::string& text);

// A lobby's list of rooms (GET <lobby>/lobby), fetched on a thread of its own
struct NetLobby {
    bool busy = false, done = false;  // fetching; fetched (list holds the answer)
    std::string url;                  // the address it was asked of
    mh4n::rooms::LobbyList list;
};
void net_lobby_fetch(const std::string& url);
NetLobby net_lobby();
