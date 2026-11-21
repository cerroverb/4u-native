// The net sync on the native runtime (1.0.2; the net_sync option, off as it comes): the port's consoles in a network room
// (Azahar's rooms, or a hub of mh4n/hubs.h, which carries a room) keep together, so that one whose game stalls for a moment
// (a shader built in the middle of a quest, the app in the background, a slow phone) is waited for by the others instead of
// being dropped by the game. The game's code, rules and network are untouched: this only holds a console's game back, as a
// slow frame would.
//
// It is the Azahar build's net sync (the port's netsync.cpp, the same packets) on the native runtime: each port console that
// has it on tells the room, 20 times a second, how far its game has run, in a Wi-Fi packet of type 0x4d that no 3DS frame
// uses (rooms pass Wi-Fi packets on without looking at them; the local-wireless service of Azahar, of this runtime and of
// the port's earlier versions ignores that type). A console that has run more than the slack (200 ms) further than a peer
// it plays with (game data addressed to it within the last 3 s of its game) waits for that peer. A peer silent for 6 s is
// left to the game; a wait that lasts 8 s counts the leads again from zero; the reports say whom a console waits for, so
// two that each think they are ahead don't wait for each other.
//
// How far a game has run: on the native runtime the game clock follows the host's time, so a stalled game thread does not
// stop it (in Azahar the emulated clock stood still). So the platform counts the time between the game's frames, each up
// to kStepCapMs: a frame that stalled for a second counts 50 ms, and a wait here counts nothing. While a console waits,
// its game clock is held (GameClock::Hold) so that its game doesn't see the time jump when the wait ends.
//
// This header is the bookkeeping alone (no room, no clock: the tests drive it with their own times); the platform
// (platform_native.cpp) sends and receives the packets and waits.
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "mh4n/rooms.h"
#include "platform.h"

namespace mh4n::netsync {

using MacAddress = rooms::MacAddress;

constexpr std::uint8_t kPacketType = 0x4d;     // no 3DS frame's type
constexpr std::uint32_t kMagic = 0x434e5953;   // "SYNC"
constexpr std::uint32_t kVersion = 2;          // 2: the flags byte (the end-skip vote), as the Azahar build's
constexpr std::uint8_t kFlagEndSkip = 0x01;
constexpr double kSendEveryS = 0.05;           // how often a console reports
constexpr double kSessionMs = 3000.0;          // game data from a peer within this much of our game: we play together
constexpr double kPeerTimeoutS = 6.0;          // a peer silent this long is left to the game
constexpr double kMaxWaitS = 8.0;              // then the leads count again from zero
constexpr double kDefaultSlackMs = 200.0;
constexpr double kStepCapMs = 50.0;            // one frame counts at most this much of the game's run

struct Report {
    std::uint32_t version = 0;
    double clock_ms = 0.0;
    MacAddress waiting_for{};
    std::uint8_t flags = 0;
};

/// The packet's 24 bytes: magic, version (u32 each), the game's run in ms (f64), the MAC of the peer this console waits
/// for (zeros: none), the flags, a byte of padding: the Azahar build's layout.
std::vector<std::uint8_t> Encode(double clock_ms, const MacAddress& waiting_for, std::uint8_t flags);
/// A report from a packet of type kPacketType; nothing for another magic, an unknown version or a bad clock.
std::optional<Report> Decode(const std::vector<std::uint8_t>& data);

/// How far ahead of one peer this console has run since they began to play together.
class Lead {
public:
    void Start(double own_ms, double peer_ms) { lead_ = 0.0, last_own_ = own_ms, last_peer_ = peer_ms; }
    void Update(double own_ms, double peer_ms) {
        lead_ += (own_ms - last_own_) - (peer_ms - last_peer_);
        last_own_ = own_ms, last_peer_ = peer_ms;
    }
    void Limit(double max_ms) { lead_ = lead_ > max_ms ? max_ms : lead_; }
    double Value() const { return lead_; }

private:
    double lead_ = 0.0, last_own_ = 0.0, last_peer_ = 0.0;
};

/// The peers and their reports. Any thread: the room's network thread reports, the game's thread computes.
class Peers {
public:
    void Configure(Port::NetSyncMode mode, double slack_ms);
    Port::NetSyncMode Mode() const;
    double Slack() const;
    /// Forgets every peer (out of the room, or turned off).
    void Clear();
    void SetSelf(const MacAddress& self);

    /// A report from `from`, at host time `now_s` (seconds).
    void OnReport(const MacAddress& from, const Report& report, double now_s);
    /// A game data frame from `from` to `to` (only those to this console count: broadcasts reach other groups too).
    void OnData(const MacAddress& from, const MacAddress& to);

    struct Ahead {
        MacAddress mac{};
        double lead = 0.0;  // ms; 0 when none
    };
    /// The peer this console has run furthest ahead of (own_ms: how far it has run), as of host time now_s. restart:
    /// every lead from zero. Lines for the log go to *log.
    Ahead Compute(double own_ms, double now_s, bool restart, std::vector<std::string>* log);
    /// How many peers it keeps together with now (for the tests and the log).
    int Syncing() const;

private:
    struct Peer {
        double clock_ms = 0.0;
        MacAddress waiting_for{};
        double heard_s = 0.0;
        bool reported = false;
        std::uint64_t data_packets = 0, data_seen = 0;
        double data_at_ms = -1e18;
        bool syncing = false;
        Lead lead;
    };
    mutable std::mutex mutex_;
    Port::NetSyncMode mode_ = Port::NetSyncMode::Off;
    double slack_ms_ = kDefaultSlackMs;
    MacAddress self_{};
    std::map<MacAddress, Peer> peers_;
};

std::string MacText(const MacAddress& mac);

}  // namespace mh4n::netsync
