// What ENet does on one room link, for the log (native/src/net: the room client's link to its room, and the room
// server's link to each member). A room carries every frame reliably and in order, so on a slow link (a hub's: a server
// far away, some 800 ms of round trip) one lost datagram holds every later frame back until ENet sends it again, a round
// trip and more later. The watch samples the link from its own network thread (at most every 50 ms) and says, on links
// whose round trip is long (kSlowLinkMs or more): when frames were held back for half a second or more waiting for a
// lost one, and once a minute what the link did (frames ENet sent again, the longest wait for an acknowledgement, the
// longest hold-back, the throttle of its window).
#pragma once

#include <string>

#include "mh4n/types.h"

struct _ENetPeer;

namespace mh4n::rooms::detail {

class LinkWatch {
public:
    static constexpr u32 kSlowLinkMs = 60;

    explicit LinkWatch(std::string who) : who_(std::move(who)) {}
    void Sample(_ENetPeer* peer);

private:
    void Minute(u32 now, u32 rtt, u32 variance);

    std::string who_;
    u32 last_sample_ = 0;
    u32 minute_start_ = 0;
    bool started_ = false;
    // the oldest frame sent and not acknowledged yet
    u16 head_seq_ = 0;
    bool head_ = false;
    u32 head_since_ = 0;
    // frames received after a missing one (held back)
    bool gap_ = false;
    u32 gap_since_ = 0;
    u32 last_gap_line_ = 0;
    // ENet's own count of frames sent again (it starts over every 10 s)
    u32 lost_seen_ = 0;
    // this minute's
    u32 resent_ = 0;
    u32 ack_wait_max_ = 0;
    u32 held_max_ = 0;
    u32 held_count_ = 0;
    u32 window_full_ = 0;
    u32 throttle_min_ = 32;
};

}  // namespace mh4n::rooms::detail
