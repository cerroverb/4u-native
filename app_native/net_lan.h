// Rooms on the same Wi-Fi, without an address to type (as the MHGU port's LAN search, runtime/net/ldn.c): a room this
// phone hosts answers searches on UDP port kLanPort, and "Search this Wi-Fi" asks the whole network for rooms.
//
// The search: a probe to 255.255.255.255, to each network's own broadcast address (255.255.255.255 follows the default
// route, which on a phone can be mobile data) and to this phone itself, every 0.5 s, listening 1.5 s in all. On Wi-Fi a
// broadcast waits at the router for a sleeping device's next beacon (MHGU: 400 ms found nothing), and Android drops
// broadcasts unless the app holds a Wi-Fi multicast lock (GameActivity.lanSearch): held while a search runs and while
// this phone hosts a room. The answer says the room's name, its UDP port, its hunters and slots, and whether it has a
// password; a tap on a room found joins it at the address the answer came from.
//
// Main thread (the interface's) only, but for the search and the answers, which have threads of their own.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

constexpr std::uint16_t kLanPort = 24871;  // (beside the rooms' 24872; a room hosted on this port answers no search)

struct LanRoom {
    std::string address;               // where the answer came from (IPv4)
    std::uint16_t port = 0;            // the room's UDP port
    std::string name;
    int members = 0, slots = 0;
    bool password = false;
    bool own = false;                  // this phone's room
    std::vector<std::string> hunters;  // their nicknames
};

struct LanSearch {
    bool busy = false, done = false;  // searching; searched (rooms holds the answer)
    bool no_network = false;          // no network to search on (no Wi-Fi, no hotspot)
    std::vector<LanRoom> rooms;
};

// Each frame: starts or stops the answers with the room this phone hosts, and holds the Wi-Fi multicast lock while
// anything needs it. `password`: whether the room hosted here has one (the room does not tell).
void net_lan_poll();
void net_lan_hosting(bool password);
void net_lan_search();
LanSearch net_lan_result();
