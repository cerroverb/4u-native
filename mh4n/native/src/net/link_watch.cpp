#include "link_watch.h"

#include <algorithm>

#include <enet/enet.h>

#include "mh4n/log.h"

namespace mh4n::rooms::detail {

void LinkWatch::Sample(ENetPeer* peer) {
    if (peer == nullptr || peer->state != ENET_PEER_STATE_CONNECTED) {
        return;
    }
    const u32 now = enet_time_get();
    if (started_ && now - last_sample_ < 50) {
        return;
    }
    last_sample_ = now;
    if (!started_) {
        started_ = true;
        minute_start_ = now;
        lost_seen_ = peer->packetsLost;
    }
    const u32 rtt = peer->roundTripTime;
    // ENet's count of commands sent again since its last 10-second tally (it is zeroed there)
    const u32 lost = peer->packetsLost;
    resent_ += lost >= lost_seen_ ? lost - lost_seen_ : lost;
    lost_seen_ = lost;
    throttle_min_ = std::min<u32>(throttle_min_, peer->packetThrottle);
    // the oldest reliable command still waiting for its acknowledgement
    if (!enet_list_empty(&peer->sentReliableCommands)) {
        const auto* first = reinterpret_cast<const ENetOutgoingCommand*>(enet_list_front(&peer->sentReliableCommands));
        if (!head_ || first->reliableSequenceNumber != head_seq_) {
            head_ = true;
            head_seq_ = first->reliableSequenceNumber;
            head_since_ = first->sendAttempts <= 1 ? first->sentTime : now;
        }
        ack_wait_max_ = std::max(ack_wait_max_, now - head_since_);
    } else {
        head_ = false;
    }
    // frames waiting for the window: the throttle keeps them from going out
    if (!enet_list_empty(&peer->outgoingSendReliableCommands)) {
        const u32 window = std::max<u32>(peer->packetThrottle * peer->windowSize / ENET_PEER_PACKET_THROTTLE_SCALE, peer->mtu);
        if (peer->reliableDataInTransit + peer->mtu > window) {
            ++window_full_;
        }
    }
    // frames received past a missing one: held back until it comes
    bool gap = false;
    if (peer->channels != nullptr && peer->channelCount > 0) {
        const ENetChannel& channel = peer->channels[0];
        if (!enet_list_empty(&channel.incomingReliableCommands)) {
            const auto* next = reinterpret_cast<const ENetIncomingCommand*>(
                enet_list_front(const_cast<ENetList*>(&channel.incomingReliableCommands)));
            gap = next->reliableSequenceNumber != static_cast<u16>(channel.incomingReliableSequenceNumber + 1);
        }
    }
    if (gap && !gap_) {
        gap_ = true;
        gap_since_ = now;
    } else if (!gap && gap_) {
        gap_ = false;
        const u32 held = now - gap_since_;
        held_max_ = std::max(held_max_, held);
        if (held >= 300) {
            ++held_count_;
        }
        if (held >= 500 && rtt >= kSlowLinkMs && now - last_gap_line_ >= 1000) {
            last_gap_line_ = now;
            MH4N_LOG(Info, "Network.Room",
                     "{}: frames held back {} ms for a lost one (round trip {} ms, ENet sends a lost one again after "
                     "{} ms or more)",
                     who_, held, rtt, rtt + 4 * peer->roundTripTimeVariance);
        }
    }
    if (now - minute_start_ >= 60000) {
        Minute(now, rtt, peer->roundTripTimeVariance);
    }
}

void LinkWatch::Minute(u32 now, u32 rtt, u32 variance) {
    if (rtt >= kSlowLinkMs) {
        MH4N_LOG(Info, "Network.Room",
                 "{}, the last minute: round trip {} ms (+-{}); {} frames sent again; the longest wait for an "
                 "acknowledgement {} ms; frames held back for a lost one {} times for 0.3 s or more (the longest {} "
                 "ms); window full {} times; throttle {}/32 at the lowest",
                 who_, rtt, variance, resent_, ack_wait_max_, held_count_, held_max_, window_full_, throttle_min_);
    }
    minute_start_ = now;
    resent_ = ack_wait_max_ = held_max_ = held_count_ = window_full_ = 0;
    throttle_min_ = 32;
}

}  // namespace mh4n::rooms::detail
