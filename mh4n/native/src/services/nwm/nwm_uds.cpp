// Derived from Azahar src/core/hle/service/nwm/nwm_uds.cpp, src/core/hle/service/nwm/nwm_uds.h,
// src/core/hle/service/nwm/uds_data.h and src/core/hle/service/nwm/uds_beacon.h at
// 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-2.0-or-later: each of them says "GPLv2 or any later
// version"): the commands of nwm::UDS and their replies, the checks and result codes of each, the
// connection status, node and network structures (NodeInfo, ConnectionStatus, NetworkInfo,
// BeaconDataReplyHeader), and which events are signalled where; with a room (N4-02), the handling of every
// frame another console sends through it (beacons, authentication, association, EAPoL-Start and -Logoff, the
// node map, secure data, deauthentication), the beacon frame sent every 102.4 ms while hosting, the data
// frames of SendTo and the queue of PullPacket, DecryptBeaconData, and the port's fix to HandleEAPoLPacket
// (port/azahar/azahar.patch, the nwm_uds.cpp hunk). Also read, for the timing of the two commands that put
// the client to sleep and for the reply that is written when it wakes: src/core/hle/kernel/hle_ipc.cpp
// (SleepClientThread, ThreadCallback), src/core/hle/kernel/server_session.cpp (HandleSyncRequest) and
// src/core/hle/kernel/thread.cpp (WakeAfterDelay, ThreadWakeupCallback), all GPL-2.0-or-later as well.
// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project (nwm_uds.cpp, nwm_uds.h, thread.cpp)
// Copyright 2017 Citra Emulator Project (uds_beacon.h)
// Copyright 2017-2026 Citra Emulator Project / Azahar Emulator Project (uds_data.h)
// Copyright 2017-2025 Citra Emulator Project / Azahar Emulator Project (hle_ipc.cpp)
// Copyright 2016-2024 Citra Emulator Project / Azahar Emulator Project (server_session.cpp)
//
// nwm::UDS, the local-wireless service (N1-29, N4-02). Its frames go through the network room the process's room
// client is in (mh4n/rooms.h, native/src/net): Azahar's design, where the room stands for the air and every frame is
// one of the room's Wi-Fi packets. Without a room (the client is not Joined), every answer is the one Azahar gives
// when it is not in one: SendPacket sends nothing and no frame ever arrives, so the village's Hunter Search scans,
// hosts a network for a while, destroys it and starts over, and that loop stays idle.
//
// What is the same as Azahar's:
//  - One service state for all the sessions (Azahar has one NWM_UDS object): the connection status, the node list,
//    the node map (MAC address to node id), the bound data channels with their queues, the received beacons (the last
//    15, one per sender) and the connection status event.
//  - RecvBeaconBroadcastData writes the beacons received since the last scan (all of them, or the one of the MAC
//    address asked for) and the client gets the reply 300 ms of game time after it asked.
//  - ConnectNetwork moves the status to Connecting and sends the authentication frame to the host; the reply comes
//    when the host's EAPoL-Logoff gives this console its node id, or with the timeout error 5 s of game time later
//    (the status then stays Connecting).
//  - BeginHostingNetwork makes this console the host (status 6, node 1) with the room's MAC address as the host's,
//    signals the connection status event and sends a beacon every 102.4 ms of game time; DestroyNetwork and
//    DisconnectNetwork go back to NotConnected, signal that event and every bound channel's event, and drop the
//    bindings.
//  - A host gives a joining client the first free node id, broadcasts the node map and the EAPoL-Logoff (every member
//    gets it: the others update their node lists from it), and routes a frame for another node by broadcasting it.
//  - Data frames are not encrypted, and the beacon's node list is encrypted under an all-zero key, as Azahar does: a
//    console of the room only has to understand Azahar's frames.
//
// Departures from Azahar:
//  - Frames from the room arrive on the room client's network thread and are handled on the scheduler thread
//    (Kernel::Post), where every command runs: no lock is needed. Azahar handles them on its network thread, under a
//    mutex.
//  - The port's fix: a console that is Connecting takes an EAPoL-Logoff only when it names this console's MAC address
//    (or names none: a host that is official Azahar sends zeros there), and this host names the console in each one it
//    sends. Without it, two consoles that join at the same time both take the first logoff, end up with the same node
//    id, and the game drops one.
//  - Frames that are too short to read are dropped (Azahar copies past their end).
//  - The beacon broadcast timer (every 102.4 ms of game time while hosting) runs whether or not the client is in a
//    room, as Azahar's; the frame is built only when the client is in one (with no room it would only go to a
//    SendPacket that sends nothing). It is an event of the console's timeline either way (the deterministic mode ends
//    a slice at each, as Azahar does).
//  - SignalEventAsync: with the deterministic clock the signal is queued and runs on core 1 two slices later, the
//    way the reference runner's Azahar does it (see Signal below); in real time at once, as in a frontend.
//  - Where Azahar asserts and aborts, the call logs an error and goes on, except for the two checks the real UDS
//    module makes fatal (hosting a network of fewer than 2 nodes, application data of more than 0xC8 bytes): those
//    log a critical message and end the run with status 3, as err:f does. Azahar's assertions are: the shared memory
//    block of Initialize has the size given (and exists), Bind finds the channel free (the new binding replaces the
//    old one here), the buffer sizes of BeginHostingNetwork, ConnectToNetwork, SetApplicationData and SendTo agree
//    with their parameters, RecvBeaconBroadcastData gets an output buffer of the size given (beacons that do not fit
//    are left out here), a successful association, a free node for an EAPoL-Start (refused here), no management
//    frames, and the Great N's OUI and tag in DecryptBeaconData (refused here with the not-found error).
//  - PullPacket on an empty queue answers the source node 0; Azahar pushes a SecureDataHeader that it never filled
//    in, so its value is whatever was on the stack.
//  - GetApplicationData with a buffer smaller than the data answers two words (the result and a size of 0); Azahar
//    declares two more translate words that it leaves as they were in the command buffer.
//  - The SPOOF_FRIEND_CODE_SEED hack of Azahar (it replaces the friend code seed with the MAC address for chosen
//    titles) is not implemented; it is off for every title by default. The shared page's MAC address is set at
//    start-up (D40, see StartNwm below): the room's when the client is in one then, else cfg's console MAC; when the
//    client joins later, the page gets the room's at the next frame (the port's app does the same, SetConsoleMac).
//  - The deprecated commands read the first 0x3C bytes of the network information as Azahar does; the rest of the
//    structure is zero (Azahar leaves it uninitialised).
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "../../kernel/deterministic.h"  // kernel_detail::ScheduleThreadSafe (the runtime module reaches it the same way)
#include "mh4n/cfg.h"
#include "mh4n/clock.h"
#include "mh4n/ipc.h"
#include "mh4n/kernel.h"
#include "mh4n/loader.h"
#include "mh4n/log.h"
#include "mh4n/options.h"
#include "mh4n/registry.h"
#include "mh4n/rooms.h"
#include "mh4n/runtime.h"
#include "mh4n/service.h"
#include "uds_frames.h"

namespace mh4n {

using nwm::ConnectionType;
using nwm::MacAddress;
using nwm::NetworkInfo;
using nwm::NodeInfo;

namespace {

constexpr const char* kLog = "Service.NWM";

// Azahar's ErrorModule::UDS and the ErrorDescription / ErrorSummary / ErrorLevel values the commands use
// (src/core/hle/result.h).
constexpr u32 kModuleUds = 68;
constexpr u32 kDescTooLarge = 1001;
constexpr u32 kDescNotAuthorized = 1002;
constexpr u32 kDescOutOfMemory = 1011;
constexpr u32 kDescNotInitialized = 1016;
constexpr u32 kDescNotFound = 1018;
constexpr u32 kDescTimeout = 1022;
constexpr u32 kDescWrongStatus = 490;  // ErrCodes::WrongStatus of nwm_uds.cpp
constexpr u32 kSumOutOfResource = 3;
constexpr u32 kSumInvalidState = 5;
constexpr u32 kSumWrongArgument = 8;
constexpr u32 kSumCanceled = 9;
constexpr u32 kSumStatusChanged = 10;
constexpr u32 kLevelStatus = 25;
constexpr u32 kLevelUsage = 28;

constexpr Result kErrNotInitialized =
    MakeResult(kDescNotInitialized, kModuleUds, kSumStatusChanged, kLevelStatus);
constexpr Result kErrNodeNotFound =
    MakeResult(kDescNotFound, kModuleUds, kSumWrongArgument, kLevelStatus);
constexpr Result kErrBindArgsZero =
    MakeResult(kDescNotAuthorized, kModuleUds, kSumWrongArgument, kLevelUsage);
constexpr Result kErrBindMaxBinds =
    MakeResult(kDescOutOfMemory, kModuleUds, kSumOutOfResource, kLevelStatus);
constexpr Result kErrTooLargeUsage =
    MakeResult(kDescTooLarge, kModuleUds, kSumWrongArgument, kLevelUsage);
constexpr Result kErrNotConnected =
    MakeResult(kDescNotAuthorized, kModuleUds, kSumInvalidState, kLevelStatus);
constexpr Result kErrEjectNotHost =
    MakeResult(kDescNotAuthorized, kModuleUds, kSumInvalidState, kLevelUsage);
constexpr Result kErrWrongStatus =
    MakeResult(kDescWrongStatus, kModuleUds, kSumInvalidState, kLevelStatus);
constexpr Result kErrConnectTimeout =
    MakeResult(kDescTimeout, kModuleUds, kSumCanceled, kLevelStatus);
static_assert(kErrWrongStatus == 0xC8A111EA, "WrongStatus is description 490, UDS, InvalidState, Status");

// ---- structures (nwm_uds.h) ------------------------------------------------------------------------------------

constexpr u8 kDefaultNetworkChannel = 11;
constexpr u32 kMaxBindNodes = 16;
constexpr u32 kMinRecvBufferSize = 0x5F4;
constexpr u32 kMaxSendSize = 0x5C6;
constexpr u32 kUdsMaxNodes = nwm::kUdsMaxNodes;
constexpr u16 kBroadcastNetworkNodeId = 0xFFFF;
constexpr u16 kHostDestNodeId = 1;
constexpr u32 kPullPacketBufferWords = 0x172;
constexpr u8 kBroadcastFlag = 0x2;
constexpr std::size_t kMaxBeaconFrames = 15;  // MaxBeaconFrames: beacons kept before the old ones are dropped
constexpr u8 kNintendoTagNetworkInfo = 21;

enum class NetworkStatus : u32 {
    NotConnected = 3,
    ConnectedAsHost = 6,
    Connecting = 7,
    ConnectedAsClient = 9,
    ConnectedAsSpectator = 10,
};

enum class NetworkStatusChangeReason : u32 {
    None = 0,
    ConnectionEstablished = 1,
    ConnectionLost = 4,
};

struct ConnectionStatus {
    u32 status;
    u32 status_change_reason;
    u16 network_node_id;
    u16 changed_nodes;
    u16 nodes[kUdsMaxNodes];
    u8 total_nodes;
    u8 max_nodes;
    u16 node_bitmask;
};
static_assert(sizeof(ConnectionStatus) == 0x30, "ConnectionStatus has incorrect size.");

using nwm::BeaconDataReplyHeader;
using nwm::BeaconEntryHeader;

// The beacon interval: Azahar's `msToCycles(DefaultBeaconInterval * MillisecondsPerTU)`, 100 time units of
// 1.024 ms, through the double overload (truncated).
constexpr u64 kBeaconIntervalTicks =
    static_cast<u64>(static_cast<double>(268111856) * 0.001 * (100 * 1.024));
static_assert(kBeaconIntervalTicks == 27454654, "102.4 ms in ticks, as the reference's timeline has it");

// How long the two commands that sleep the client take, in game time.
constexpr s64 kBeaconScanNs = 300000000;          // UDSBeaconScanInterval
constexpr s64 kConnectionTimeoutNs = 5000000000;  // UDSConnectionTimeout

// ---- what all the sessions share --------------------------------------------------------------------------

struct BindNodeData {
    u32 bind_node_id = 0;
    u8 channel = 0;
    u16 network_node_id = 0;
    std::shared_ptr<Event> event;
    std::deque<std::vector<u8>> received_packets;
};

// Azahar's Node: what the node map knows of a MAC address.
struct Node {
    bool connected = false;
    bool spec = false;
    u16 node_id = 0;
};

struct UdsState {
    ServiceContext* context = nullptr;  // set by the first session and by StartNwm (the state lives in it)
    bool initialized = false;
    std::shared_ptr<Event> connection_status_event;
    std::shared_ptr<SharedMemory> recv_buffer_memory;
    ConnectionStatus connection_status{};
    std::vector<NodeInfo> node_info;
    NodeInfo current_node{};
    ConnectionType conn_type = ConnectionType::Client;
    std::map<u32, BindNodeData> channel_data;  // by data channel
    u8 network_channel = kDefaultNetworkChannel;
    NetworkInfo network_info{};
    std::map<MacAddress, Node> node_map;
    std::list<rooms::WifiPacket> received_beacons;
    // The events queued for the deterministic mode's asynchronous signal (Azahar's pending_async_event_signals).
    std::deque<std::shared_ptr<Event>> pending_async;
    // The beacon timer's events that are waiting (DestroyNetwork takes them back).
    std::set<u64> beacon_events;
    // ConnectToNetwork, while it waits for the host (Azahar's connection_event and the sleeping client).
    std::shared_ptr<Request> connect_request;
    u64 connect_timeout = 0;
    bool connect_timeout_set = false;
};

bool Is(const UdsState& s, NetworkStatus status) {
    return s.connection_status.status == static_cast<u32>(status);
}

// ---- the room ----------------------------------------------------------------------------------------------

// Azahar's SendPacket: to the room this console is in, from the room's MAC address; nothing when it is in none.
void SendPacket(rooms::WifiPacket& packet) {
    rooms::Default().SendWifi(packet);
}

// Azahar's NWM_UDS::GetMacAddress: the room's address for this console when it is in a room, else the console's.
MacAddress GetMacAddress(const UdsState& s) {
    if (const std::optional<MacAddress> room = rooms::Default().RoomMac()) {
        return *room;
    }
    return s.context != nullptr ? cfg::GetMacAddress(*s.context) : MacAddress{};
}

// Azahar's SignalEventAsync. On the emulation thread of a frontend it signals at once, and so does this, everything
// here running on the scheduler thread. The reference runner never registers its thread as the core loop
// (`System::RegisterCoreLoopThreadId` is called by the frontends only), so there it goes the other way: the event is
// queued, and `ScheduleEvent(0, handle_async_event_signals_event, 0, 1, true)` signals it on core 1 two slices of time
// later (MAX_SLICE_LENGTH * 2), one event per call, in order. The deterministic mode follows that, because it is the
// mode that is compared with the reference's traces, tick for tick (the connection status event wakes a thread of the
// game's, whose first request is then 2.29 million ticks later than it would be). Which of the two is decided by the
// kernel: ScheduleThreadSafe does nothing and says so when there is no deterministic clock.
//
// This runs on the scheduler thread, and ScheduleThreadSafe asserts so: the kernel's timer queue is not locked. The
// room's frames are Posted to the scheduler thread before they get here.
void Signal(const std::shared_ptr<UdsState>& state, const std::shared_ptr<Event>& event) {
    if (!event) {
        return;
    }
    const std::weak_ptr<UdsState> weak = state;
    const bool scheduled = kernel_detail::ScheduleThreadSafe(1, [weak] {
        const std::shared_ptr<UdsState> locked = weak.lock();
        if (locked == nullptr || locked->pending_async.empty()) {
            return;
        }
        const std::shared_ptr<Event> next = std::move(locked->pending_async.front());
        locked->pending_async.pop_front();
        next->Signal();
    });
    if (!scheduled) {
        event->Signal();  // the real-time clock: at once
        return;
    }
    state->pending_async.push_back(event);
}

// Azahar's beacon_broadcast_event: while this console hosts a network its callback builds a beacon frame and hands it
// to SendPacket, then schedules itself one interval after the tick it was due at (`interval - cycles_late` from the
// tick it ran at); the chain ends when the status is not "host" any more, and DestroyNetwork takes the pending event
// back (UnscheduleEvent). The frame is built only in a room (see the departures). A function of the state, not of a
// service object: it must outlive the sessions.
void ScheduleBeacon(Kernel& kernel, const std::shared_ptr<UdsState>& state, u64 due_tick) {
    auto id = std::make_shared<u64>(0);
    const std::weak_ptr<UdsState> weak = state;
    *id = kernel.ScheduleAt(due_tick, [weak, id, due_tick, &kernel] {
        const std::shared_ptr<UdsState> locked = weak.lock();
        if (locked == nullptr) {
            return;
        }
        locked->beacon_events.erase(*id);
        if (!Is(*locked, NetworkStatus::ConnectedAsHost)) {
            return;
        }
        if (rooms::Default().RoomMac().has_value()) {
            rooms::WifiPacket packet;
            packet.type = rooms::WifiPacket::Type::Beacon;
            packet.data = nwm::GenerateBeaconFrame(locked->network_info, locked->node_info);
            packet.destination = rooms::kBroadcastMac;
            packet.channel = locked->network_channel;
            SendPacket(packet);
        }
        ScheduleBeacon(kernel, locked, due_tick + kBeaconIntervalTicks);
    });
    state->beacon_events.insert(*id);
}

// ---- the frames from other consoles (Azahar's OnWifiPacketReceived and its handlers) -------------------------

u16 GetNextAvailableNodeId(const UdsState& s) {
    for (u16 index = 0; index < s.connection_status.max_nodes; ++index) {
        if ((s.connection_status.node_bitmask & (1 << index)) == 0) {
            return static_cast<u16>(index + 1);
        }
    }
    return 0;  // full: the caller refuses the connection
}

std::optional<MacAddress> GetNodeMacAddress(const UdsState& s, u16 dest_node_id, u8 flags) {
    if ((flags & kBroadcastFlag) != 0 || dest_node_id == kBroadcastNetworkNodeId) {
        return rooms::kBroadcastMac;
    }
    if (dest_node_id == kHostDestNodeId) {
        return s.network_info.HostMac();
    }
    for (const auto& [mac, node] : s.node_map) {
        if (node.node_id == dest_node_id && node.connected) {
            return mac;
        }
    }
    return std::nullopt;
}

// Note: this is not how UDS on a 3DS does it, but it should not be needed for the room (Azahar's note). The entry count
// is Azahar's `std::size_t`, 8 bytes, as its 64-bit builds send it, then the MAC address and the node id (host order,
// little-endian) of every connected client.
void BroadcastNodeMap(UdsState& s) {
    rooms::WifiPacket packet;
    packet.channel = s.network_channel;
    packet.type = rooms::WifiPacket::Type::NodeMap;
    packet.destination = rooms::kBroadcastMac;
    u64 count = 0;
    for (const auto& [mac, node] : s.node_map) {
        count += node.connected && !node.spec ? 1 : 0;
    }
    for (int i = 0; i < 8; ++i) {
        packet.data.push_back(static_cast<u8>(count >> (8 * i)));
    }
    for (const auto& [mac, node] : s.node_map) {
        if (node.connected && !node.spec) {
            packet.data.insert(packet.data.end(), mac.begin(), mac.end());
            packet.data.push_back(static_cast<u8>(node.node_id));
            packet.data.push_back(static_cast<u8>(node.node_id >> 8));
        }
    }
    SendPacket(packet);
}

void HandleNodeMapPacket(UdsState& s, const rooms::WifiPacket& packet) {
    if (Is(s, NetworkStatus::ConnectedAsHost)) {
        MH4N_LOG(Debug, kLog, "Ignored NodeMapPacket since connection_status is host");
        return;
    }
    if (packet.data.size() < 8) {
        return;
    }
    u64 count = 0;
    for (int i = 0; i < 8; ++i) {
        count |= static_cast<u64>(packet.data[static_cast<std::size_t>(i)]) << (8 * i);
    }
    s.node_map.clear();
    std::size_t offset = 8;
    for (u64 i = 0; i < count && offset + 8 <= packet.data.size(); ++i) {
        MacAddress mac{};
        std::memcpy(mac.data(), packet.data.data() + offset, 6);
        const u16 id = static_cast<u16>(packet.data[offset + 6] | (packet.data[offset + 7] << 8));
        s.node_map[mac].connected = true;
        s.node_map[mac].node_id = id;
        offset += 8;
    }
}

void HandleBeaconFrame(UdsState& s, const rooms::WifiPacket& packet) {
    // a newer beacon from the same MAC replaces the old one
    s.received_beacons.remove_if(
        [&packet](const rooms::WifiPacket& old) { return old.transmitter == packet.transmitter; });
    s.received_beacons.push_back(packet);
    if (s.received_beacons.size() > kMaxBeaconFrames) {
        s.received_beacons.pop_front();
    }
}

void HandleAssociationResponseFrame(UdsState& s, const rooms::WifiPacket& packet) {
    const auto result = nwm::ParseAssociationResponse(packet.data);
    if (!result || result->status != 0) {
        MH4N_LOG(Error, kLog, "Could not join network");
        return;
    }
    if (!Is(s, NetworkStatus::Connecting)) {
        MH4N_LOG(Debug, kLog, "Ignored AssociationResponseFrame because connection status is {}",
                 s.connection_status.status);
        return;
    }
    // Send the EAPoL-Start packet to the server.
    rooms::WifiPacket eapol_start;
    eapol_start.channel = s.network_channel;
    eapol_start.data = nwm::GenerateEapolStartFrame(result->association_id, s.conn_type, s.current_node);
    eapol_start.destination = packet.transmitter;
    eapol_start.type = rooms::WifiPacket::Type::Data;
    SendPacket(eapol_start);
}

void FinishConnect(UdsState& s, Kernel& kernel) {
    if (s.connect_timeout_set) {
        kernel.Cancel(s.connect_timeout);
        s.connect_timeout_set = false;
    }
    if (s.connect_request == nullptr) {
        return;
    }
    const std::shared_ptr<Request> request = std::move(s.connect_request);
    s.connect_request.reset();
    if (request->Finished() || request->Cancelled()) {
        return;
    }
    Reply reply(request->CommandId(), 1, 0);
    reply.Push(kResultSuccess);
    request->Deliver(std::move(reply));
    MH4N_LOG(Debug, kLog, "connection sequence finished");
}

void HandleEapolPacket(const std::shared_ptr<UdsState>& state, const rooms::WifiPacket& packet) {
    UdsState& s = *state;
    const std::optional<u16> type = nwm::EapolFrameType(packet.data);
    if (!type) {
        return;
    }
    if (*type == nwm::kEapolStartMagic) {
        if (!Is(s, NetworkStatus::ConnectedAsHost)) {
            MH4N_LOG(Debug, kLog, "Connection sequence aborted, because connection status is {}",
                     s.connection_status.status);
            return;
        }
        const auto node_it = s.node_map.find(packet.transmitter);
        if (node_it == s.node_map.end()) {
            MH4N_LOG(Debug, kLog,
                     "Connection sequence aborted, because the AuthenticationFrame of the client wasn't recieved");
            return;
        }
        if (node_it->second.connected) {
            MH4N_LOG(Debug, kLog, "Connection sequence aborted, because the client is already connected");
            return;
        }
        const auto eapol_start = nwm::ParseEapolStartFrame(packet.data);
        if (!eapol_start) {
            return;
        }
        NodeInfo node = eapol_start->node;
        if (eapol_start->connection_type == static_cast<u8>(ConnectionType::Client)) {
            const u16 node_id = GetNextAvailableNodeId(s);
            if (node_id == 0 || s.connection_status.max_nodes == s.connection_status.total_nodes) {
                MH4N_LOG(Error, kLog, "No available connection slots in the network");
                return;
            }
            node.network_node_id = node_id;
            s.connection_status.node_bitmask |= static_cast<u16>(1 << (node_id - 1));
            s.connection_status.changed_nodes |= static_cast<u16>(1 << (node_id - 1));
            s.connection_status.nodes[node_id - 1] = node.network_node_id;
            s.connection_status.total_nodes++;
            if (node_id - 1 < static_cast<int>(s.node_info.size())) {
                s.node_info[node_id - 1] = node;
            }
            s.network_info.total_nodes++;
            Node& entry = s.node_map[packet.transmitter];
            entry.node_id = node.network_node_id;
            entry.connected = true;
            entry.spec = false;
            BroadcastNodeMap(s);
        } else if (eapol_start->connection_type == static_cast<u8>(ConnectionType::Spectator)) {
            Node& entry = s.node_map[packet.transmitter];
            entry.node_id = nwm::kNodeIdSpec;
            entry.connected = true;
            entry.spec = true;
        } else {
            MH4N_LOG(Error, kLog, "Client tried connecting with unknown connection type: {:#x}",
                     eapol_start->connection_type);
        }
        // Send the EAPoL-Logoff packet. On a 3DS it goes to the new client only, and a packet with the node
        // information is broadcast; Azahar broadcasts the logoff instead (its TODO), and so does this.
        rooms::WifiPacket eapol_logoff;
        eapol_logoff.channel = s.network_channel;
        eapol_logoff.data = nwm::GenerateEapolLogoffFrame(packet.transmitter, node.network_node_id, s.node_info,
                                                          s.network_info.max_nodes, s.network_info.total_nodes);
        eapol_logoff.destination = rooms::kBroadcastMac;
        eapol_logoff.type = rooms::WifiPacket::Type::Data;
        SendPacket(eapol_logoff);
        Signal(state, s.connection_status_event);
        return;
    }

    const auto logoff = nwm::ParseEapolLogoffFrame(packet.data);
    if (!logoff) {
        return;
    }
    if (Is(s, NetworkStatus::Connecting)) {
        // The host broadcasts the logoff that gives a joining console its node id, so a console that joins at the
        // same time as another sees both: take only the one for this console, or two consoles end up with the same
        // node id and the game drops one. (A host that doesn't fill in the address sends zeros: take those as
        // before.)
        if (logoff->client_mac != MacAddress{}) {
            const std::optional<MacAddress> mine = rooms::Default().RoomMac();
            if (mine && logoff->client_mac != *mine) {
                MH4N_LOG(Debug, kLog, "Ignored an EAPoL-Logoff for another console");
                return;
            }
        }
        s.network_info.SetHostMac(packet.transmitter);
        s.network_info.total_nodes = logoff->connected_nodes;
        s.network_info.max_nodes = logoff->max_nodes;
        s.connection_status.network_node_id = logoff->assigned_node_id;
        s.connection_status.total_nodes = logoff->connected_nodes;
        s.connection_status.max_nodes = logoff->max_nodes;
        s.node_info.clear();
        s.node_info.resize(s.network_info.max_nodes);
        for (const NodeInfo& node : logoff->nodes) {
            const u16 index = node.network_node_id;
            if (index == 0 || index > kUdsMaxNodes) {
                continue;
            }
            s.connection_status.node_bitmask |= static_cast<u16>(1 << (index - 1));
            s.connection_status.changed_nodes |= static_cast<u16>(1 << (index - 1));
            s.connection_status.nodes[index - 1] = index;
            if (index - 1 < static_cast<int>(s.node_info.size())) {
                s.node_info[index - 1] = node;
            }
        }
        if (s.conn_type == ConnectionType::Client) {
            s.connection_status.status = static_cast<u32>(NetworkStatus::ConnectedAsClient);
        } else if (s.conn_type == ConnectionType::Spectator) {
            s.connection_status.status = static_cast<u32>(NetworkStatus::ConnectedAsSpectator);
        } else {
            MH4N_LOG(Error, kLog, "Unknown connection type: {:#x}", static_cast<u32>(s.conn_type));
        }
        // We're now connected, signal the application
        s.connection_status.status_change_reason = static_cast<u32>(NetworkStatusChangeReason::ConnectionEstablished);
        Signal(state, s.connection_status_event);
        if (s.context != nullptr) {
            FinishConnect(s, s.context->GetKernel());  // Azahar: SignalEventAsync(connection_event)
        }
        MH4N_LOG(Info, kLog, "connected to the network of {:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X} as node {}",
                 packet.transmitter[0], packet.transmitter[1], packet.transmitter[2], packet.transmitter[3],
                 packet.transmitter[4], packet.transmitter[5], logoff->assigned_node_id);
        return;
    }
    if (Is(s, NetworkStatus::ConnectedAsClient) || Is(s, NetworkStatus::ConnectedAsSpectator)) {
        // In the current implementation the host isn't broadcasting the node information, so the clients take it from
        // the logoffs (Azahar's TODO).
        s.network_info.total_nodes = logoff->connected_nodes;
        s.connection_status.total_nodes = logoff->connected_nodes;
        std::memset(s.connection_status.nodes, 0, sizeof(s.connection_status.nodes));
        const u16 old_bitmask = s.connection_status.node_bitmask;
        s.connection_status.node_bitmask = 0;
        s.node_info.clear();
        s.node_info.resize(s.network_info.max_nodes);
        for (const NodeInfo& node : logoff->nodes) {
            const u16 index = node.network_node_id;
            if (index == 0 || index > kUdsMaxNodes) {
                continue;
            }
            s.connection_status.node_bitmask |= static_cast<u16>(1 << (index - 1));
            s.connection_status.nodes[index - 1] = index;
            if (index - 1 < static_cast<int>(s.node_info.size())) {
                s.node_info[index - 1] = node;
            }
        }
        s.connection_status.changed_nodes = static_cast<u16>(old_bitmask ^ s.connection_status.node_bitmask);
        Signal(state, s.connection_status_event);
    }
}

void HandleSecureDataPacket(const std::shared_ptr<UdsState>& state, const rooms::WifiPacket& packet) {
    UdsState& s = *state;
    const auto secure_data = nwm::ParseSecureDataHeader(packet.data);
    if (!secure_data) {
        return;
    }
    if (!Is(s, NetworkStatus::ConnectedAsHost) && !Is(s, NetworkStatus::ConnectedAsClient) &&
        !Is(s, NetworkStatus::ConnectedAsSpectator)) {
        MH4N_LOG(Trace, kLog, "Ignored SecureDataPacket because connection status is {}", s.connection_status.status);
        return;
    }
    if (secure_data->src_node_id == s.connection_status.network_node_id) {
        return;  // a packet that came from ourselves
    }
    if (secure_data->dest_node_id != s.connection_status.network_node_id &&
        secure_data->dest_node_id != kBroadcastNetworkNodeId) {
        // Not for us: only a host routes it. A client may have got it from a host's broadcast: it ignores it.
        if (packet.destination != rooms::kBroadcastMac && !Is(s, NetworkStatus::ConnectedAsHost)) {
            MH4N_LOG(Error, kLog, "Received packet addressed to others but we're not a host");
            return;
        }
        if (Is(s, NetworkStatus::ConnectedAsHost) && secure_data->dest_node_id != kBroadcastNetworkNodeId) {
            // Broadcast the packet so the right receiver can get it.
            rooms::WifiPacket out = packet;
            out.destination = rooms::kBroadcastMac;
            SendPacket(out);
        }
        return;
    }
    if (secure_data->is_management != 0) {
        MH4N_LOG(Error, kLog, "a UDS management frame (not handled)");
        return;
    }
    const auto channel_info = s.channel_data.find(secure_data->data_channel);
    if (channel_info == s.channel_data.end()) {
        return;  // a channel nobody bound
    }
    if (channel_info->second.network_node_id != kBroadcastNetworkNodeId &&
        channel_info->second.network_node_id != secure_data->src_node_id) {
        return;
    }
    channel_info->second.received_packets.push_back(packet.data);
    Signal(state, channel_info->second.event);
}

void SendAssociationResponseFrame(UdsState& s, const MacAddress& address) {
    if (!Is(s, NetworkStatus::ConnectedAsHost)) {
        MH4N_LOG(Error, kLog, "Connection sequence aborted, because connection status is {}",
                 s.connection_status.status);
        return;
    }
    rooms::WifiPacket assoc_response;
    assoc_response.channel = s.network_channel;
    // This makes every client's association id 1, but nothing uses it (Azahar's TODO).
    assoc_response.data = nwm::GenerateAssocResponseFrame(0, 1, s.network_info.NetworkId());
    assoc_response.destination = address;
    assoc_response.type = rooms::WifiPacket::Type::AssociationResponse;
    SendPacket(assoc_response);
}

void HandleAuthenticationFrame(UdsState& s, const rooms::WifiPacket& packet) {
    // Only the SEQ1 frame is handled; SEQ2 needs nothing.
    const std::optional<u16> seq = nwm::AuthenticationSeqNumber(packet.data);
    if (!seq || *seq != static_cast<u16>(nwm::AuthenticationSeq::SEQ1)) {
        return;
    }
    if (!Is(s, NetworkStatus::ConnectedAsHost)) {
        MH4N_LOG(Error, kLog, "Connection sequence aborted, because connection status is {}",
                 s.connection_status.status);
        return;
    }
    if (s.node_map.find(packet.transmitter) != s.node_map.end()) {
        MH4N_LOG(Error, kLog,
                 "Connection sequence aborted, because there is already a connected client with that MAC-Adress");
        return;
    }
    if (s.connection_status.max_nodes == s.connection_status.total_nodes) {
        // Reject connection attempt (which packet a console sends is not known)
        MH4N_LOG(Error, kLog, "Reached maximum nodes, but reject packet wasn't sent.");
        return;
    }
    rooms::WifiPacket auth_response;
    auth_response.channel = s.network_channel;
    auth_response.data = nwm::GenerateAuthenticationFrame(nwm::AuthenticationSeq::SEQ2);
    auth_response.destination = packet.transmitter;
    auth_response.type = rooms::WifiPacket::Type::Authentication;
    s.node_map[packet.transmitter].connected = false;
    SendPacket(auth_response);
    SendAssociationResponseFrame(s, packet.transmitter);
}

void HandleDeauthenticationFrame(const std::shared_ptr<UdsState>& state, const rooms::WifiPacket& packet) {
    UdsState& s = *state;
    if (!Is(s, NetworkStatus::ConnectedAsHost)) {
        MH4N_LOG(Error, kLog, "Got deauthentication frame but we are not the host");
        return;
    }
    const auto found = s.node_map.find(packet.transmitter);
    if (found == s.node_map.end()) {
        MH4N_LOG(Error, kLog, "Got deauthentication frame from unknown node");
        return;
    }
    const Node node = found->second;
    s.node_map.erase(found);
    if (!node.connected) {
        MH4N_LOG(Debug, kLog, "Received DeauthenticationFrame from a not connected MAC Address");
        return;
    }
    const auto node_it = std::find_if(s.node_info.begin(), s.node_info.end(), [&node](const NodeInfo& info) {
        return info.network_node_id == node.node_id;
    });
    if (node_it == s.node_info.end()) {
        MH4N_LOG(Error, kLog, "node_it is last node of node_info");
        return;
    }
    if (!node.spec && node.node_id >= 1 && node.node_id <= kUdsMaxNodes) {
        s.connection_status.node_bitmask &= static_cast<u16>(~(1 << (node.node_id - 1)));
        s.connection_status.changed_nodes |= static_cast<u16>(1 << (node.node_id - 1));
        s.connection_status.total_nodes--;
        s.connection_status.nodes[node.node_id - 1] = 0;
        s.network_info.total_nodes--;
    }
    *node_it = NodeInfo{};
    Signal(state, s.connection_status_event);
}

void OnWifiPacketReceived(const std::shared_ptr<UdsState>& state, const rooms::WifiPacket& packet) {
    UdsState& s = *state;
    if (!s.initialized) {
        return;
    }
    switch (packet.type) {
    case rooms::WifiPacket::Type::Beacon:
        HandleBeaconFrame(s, packet);
        break;
    case rooms::WifiPacket::Type::Authentication:
        HandleAuthenticationFrame(s, packet);
        break;
    case rooms::WifiPacket::Type::AssociationResponse:
        HandleAssociationResponseFrame(s, packet);
        break;
    case rooms::WifiPacket::Type::Data: {
        const std::optional<u16> ether = nwm::FrameEtherType(packet.data);
        if (ether == static_cast<u16>(nwm::EtherType::EAPoL)) {
            HandleEapolPacket(state, packet);
        } else if (ether == static_cast<u16>(nwm::EtherType::SecureData)) {
            HandleSecureDataPacket(state, packet);
        }
        break;
    }
    case rooms::WifiPacket::Type::Deauthentication:
        HandleDeauthenticationFrame(state, packet);
        break;
    case rooms::WifiPacket::Type::NodeMap:
        HandleNodeMapPacket(s, packet);
        break;
    default:
        break;  // not a frame of a console (the port's net sync reports are type 0x4d)
    }
}

class NwmUds final : public Service {
public:
    explicit NwmUds(ServiceContext& context)
        : Service(context, "nwm::UDS"), state_(context.State<UdsState>("nwm::UDS")) {
        state_->context = &context;
        if (!state_->connection_status_event) {
            state_->connection_status_event = context.GetKernel().NewEvent(
                ResetType::OneShot, "NWM::connection_status_event");
        }
        Register(0x0001, "Initialize (deprecated)", &NwmUds::InitializeDeprecated);
        Register(0x0002, "Scrap");
        Register(0x0003, "Shutdown", &NwmUds::Shutdown);
        Register(0x0004, "BeginHostingNetwork (deprecated)", &NwmUds::BeginHostingNetworkDeprecated);
        Register(0x0005, "EjectClient", &NwmUds::EjectClient);
        Register(0x0006, "EjectSpectators", &NwmUds::EjectSpectators);
        Register(0x0007, "UpdateNetworkAttribute", &NwmUds::UpdateNetworkAttribute);
        Register(0x0008, "DestroyNetwork", &NwmUds::DestroyNetwork);
        Register(0x0009, "ConnectToNetwork (deprecated)", &NwmUds::ConnectToNetworkDeprecated);
        Register(0x000A, "DisconnectNetwork", &NwmUds::DisconnectNetwork);
        Register(0x000B, "GetConnectionStatus", &NwmUds::GetConnectionStatus);
        Register(0x000D, "GetNodeInformation", &NwmUds::GetNodeInformation);
        Register(0x000E, "DecryptBeaconData (deprecated)", &NwmUds::DecryptBeaconData);
        Register(0x000F, "RecvBeaconBroadcastData", &NwmUds::RecvBeaconBroadcastData);
        Register(0x0010, "SetApplicationData", &NwmUds::SetApplicationData);
        Register(0x0011, "GetApplicationData", &NwmUds::GetApplicationData);
        Register(0x0012, "Bind", &NwmUds::Bind);
        Register(0x0013, "Unbind", &NwmUds::Unbind);
        Register(0x0014, "PullPacket", &NwmUds::PullPacket);
        Register(0x0015, "SetMaxSendDelay");
        Register(0x0017, "SendTo", &NwmUds::SendTo);
        Register(0x001A, "GetChannel", &NwmUds::GetChannel);
        Register(0x001B, "InitializeWithVersion", &NwmUds::InitializeWithVersion);
        Register(0x001D, "BeginHostingNetwork", &NwmUds::BeginHostingNetwork);
        Register(0x001E, "ConnectToNetwork", &NwmUds::ConnectToNetwork);
        Register(0x001F, "DecryptBeaconData", &NwmUds::DecryptBeaconData);
        Register(0x0020, "Flush");
        Register(0x0021, "SetProbeResponseParam");
        Register(0x0022, "ScanOnConnection");
    }

private:
    UdsState& S() { return *state_; }

    void Signal(const std::shared_ptr<Event>& event) { mh4n::Signal(state_, event); }

    // The client sleeps `ns` of game time, then gets `reply`. Azahar puts the client thread in
    // WaitHleEvent with a timeout (WakeAfterDelay: nsToCycles), writes the reply when the timeout
    // wakes it, and adds no IPC delay; the event it waits on is one nobody holds, so only the timeout
    // wakes it.
    Handled ReplyAfter(Request& request, s64 ns, Reply reply) {
        Kernel& kernel = Context().GetKernel();
        auto shared = std::make_shared<Reply>(std::move(reply));
        kernel.ScheduleAt(kernel.NowTicks() + NsToTicks(static_cast<u64>(ns)),
                          [pending = request.Share(), shared] {
                              pending->Deliver(std::move(*shared));
                          });
        return Handled::Deferred;
    }

    void FatalRequest(const char* what) {
        MH4N_LOG(Critical, kLog, "fatal error of the UDS module: {}", what);
        Context().RequestExit(3, std::string("nwm::UDS: ") + what);
    }

    // ---- Initialize / Shutdown ----

    void DoInitialize(Request& request, u32 sharedmem_size, const NodeInfo& node,
                      std::shared_ptr<SharedMemory> sharedmem) {
        UdsState& s = S();
        s.current_node = node;
        s.initialized = true;
        s.recv_buffer_memory = std::move(sharedmem);
        if (!s.recv_buffer_memory) {
            MH4N_LOG(Error, kLog, "Initialize without a shared memory block");
        } else if (s.recv_buffer_memory->Size() != sharedmem_size) {
            MH4N_LOG(Error, kLog, "Invalid shared memory size ({:#x}, the call says {:#x})",
                     s.recv_buffer_memory->Size(), sharedmem_size);
        }
        // Reset the connection status, it contains all zeros after initialization, except for the
        // actual status value.
        s.connection_status = {};
        s.connection_status.status = static_cast<u32>(NetworkStatus::NotConnected);
        s.node_info.clear();
        s.node_info.push_back(s.current_node);
        s.channel_data.clear();

        Reply& reply = request.MakeReply(1, 2);
        reply.Push(kResultSuccess);
        reply.PushCopyObjects(s.connection_status_event);
    }

    // Inputs:  0: 0x001B0302, 1: shared memory size, 2-11: NodeInfo, 12: version, 13: descriptor,
    //          14: shared memory handle
    // Outputs: 0: 0x001B0042, 1: result, 2: descriptor, 3: connection status event handle
    void InitializeWithVersion(Request& request) {
        const u32 sharedmem_size = request.Pop<u32>();
        const auto node = request.PopRaw<NodeInfo>();
        const u16 version = request.Pop<u16>();
        auto sharedmem = request.PopObject<SharedMemory>();
        MH4N_LOG(Debug, kLog, "InitializeWithVersion sharedmem_size={:#x} version={:#x}",
                 sharedmem_size, version);
        DoInitialize(request, sharedmem_size, node, std::move(sharedmem));
    }

    // Inputs:  1: shared memory size, 2-11: NodeInfo, 12: descriptor, 13: shared memory handle
    void InitializeDeprecated(Request& request) {
        const u32 sharedmem_size = request.Pop<u32>();
        const auto node = request.PopRaw<NodeInfo>();
        auto sharedmem = request.PopObject<SharedMemory>();
        MH4N_LOG(Debug, kLog, "Initialize (deprecated) sharedmem_size={:#x}", sharedmem_size);
        DoInitialize(request, sharedmem_size, node, std::move(sharedmem));
    }

    // Outputs: 0: 0x00030040, 1: result
    void Shutdown(Request& request) {
        UdsState& s = S();
        s.initialized = false;
        for (auto& bind_node : s.channel_data) {
            Signal(bind_node.second.event);
        }
        s.channel_data.clear();
        s.node_map.clear();
        s.recv_buffer_memory.reset();
        request.MakeReply(1, 0).Push(kResultSuccess);
        MH4N_LOG(Debug, kLog, "Shutdown");
    }

    // ---- the connection status ----

    // Outputs: 0: 0x000B00C0, 1: result, 2-13: the connection status
    void GetConnectionStatus(Request& request) {
        UdsState& s = S();
        const ConnectionStatus out = s.connection_status;
        // Reset the bitmask of changed nodes after each call to this function to prevent falsely
        // informing games of outstanding changes in subsequent calls.
        s.connection_status.changed_nodes = 0;
        Reply& reply = request.MakeReply(13, 0);
        reply.Push(kResultSuccess);
        reply.PushRaw(out);
    }

    // Inputs:  1: node id
    // Outputs: 1: result, 2-11: NodeInfo
    void GetNodeInformation(Request& request) {
        const u16 network_node_id = request.Pop<u16>();
        UdsState& s = S();
        if (!s.initialized) {
            request.MakeReply(1, 0).Push(kErrNotInitialized);
            return;
        }
        const auto node =
            std::find_if(s.node_info.begin(), s.node_info.end(), [network_node_id](const NodeInfo& n) {
                return n.network_node_id == network_node_id;
            });
        if (node == s.node_info.end()) {
            request.MakeReply(1, 0).Push(kErrNodeNotFound);
            return;
        }
        Reply& reply = request.MakeReply(11, 0);
        reply.Push(kResultSuccess);
        reply.PushRaw(*node);
    }

    // Outputs: 1: result, 2: the channel of the network (0 when not connected)
    void GetChannel(Request& request) {
        UdsState& s = S();
        const bool is_connected = !Is(s, NetworkStatus::NotConnected);
        const u8 channel = is_connected ? s.network_channel : 0;
        Reply& reply = request.MakeReply(2, 0);
        reply.Push(kResultSuccess);
        reply.Push(channel);
    }

    // ---- scanning and connecting ----

    // Inputs:  1: output buffer size, 2-3: unknown, 4-5: MAC address, 6-14: unused, 15: wlan comm id,
    //          16: id, 17-18: input event handle, 19-20: output mapped buffer
    // Outputs: 1: result, 2-3: the output buffer
    Handled RecvBeaconBroadcastData(Request& request) {
        const u32 out_buffer_size = request.Pop<u32>();
        request.Pop<u32>();  // unk1
        request.Pop<u32>();  // unk2
        const auto mac_address = request.PopRaw<std::array<u8, 6>>();  // the MAC address filter
        for (int i = 0; i < 9; ++i) {
            request.Pop<u32>();  // the rest of the scan input structure
        }
        const u32 wlan_comm_id = request.Pop<u32>();
        const u32 id = request.Pop<u32>();
        // 3dbrew: official processes make an event for this and never keep it. Azahar does not use it.
        request.PopObject<Event>();
        const MappedBuffer* out_buffer = request.PopMappedBuffer();
        if (out_buffer == nullptr) {
            MH4N_LOG(Error, kLog, "RecvBeaconBroadcastData without an output buffer");
            request.MakeReply(1, 0).Push(kResultSuccess);
            return Handled::Done;
        }
        if (out_buffer->Size() != out_buffer_size) {
            MH4N_LOG(Error, kLog, "RecvBeaconBroadcastData output buffer is {:#x} bytes, not {:#x}",
                     out_buffer->Size(), out_buffer_size);
        }

        // GetReceivedBeacons: the one of the MAC address asked for (taken out of the list), or all of them.
        UdsState& s = S();
        std::list<rooms::WifiPacket> beacons;
        if (mac_address != rooms::kBroadcastMac) {
            const auto beacon = std::find_if(s.received_beacons.begin(), s.received_beacons.end(),
                                             [&mac_address](const rooms::WifiPacket& packet) {
                                                 return packet.transmitter == mac_address;
                                             });
            if (beacon != s.received_beacons.end()) {
                beacons.push_back(*beacon);
                s.received_beacons.erase(beacon);
            }
        } else {
            beacons.swap(s.received_beacons);
        }

        std::size_t cur_buffer_size = sizeof(BeaconDataReplyHeader);
        u32 entries = 0;
        for (const rooms::WifiPacket& beacon : beacons) {
            BeaconEntryHeader entry{};
            entry.unk_size = static_cast<u32>(sizeof(BeaconEntryHeader) + beacon.data.size());
            entry.total_size = static_cast<u32>(sizeof(BeaconEntryHeader) + beacon.data.size());
            entry.wifi_channel = beacon.channel;
            entry.header_size = sizeof(BeaconEntryHeader);
            std::memcpy(entry.mac_address, beacon.transmitter.data(), 6);
            if (cur_buffer_size + entry.total_size > out_buffer_size) {
                MH4N_LOG(Error, kLog, "RecvBeaconBroadcastData: a beacon does not fit in the output buffer");
                break;
            }
            out_buffer->Write(&entry, static_cast<u32>(cur_buffer_size), sizeof(BeaconEntryHeader));
            cur_buffer_size += sizeof(BeaconEntryHeader);
            out_buffer->Write(beacon.data.data(), static_cast<u32>(cur_buffer_size),
                              static_cast<u32>(beacon.data.size()));
            cur_buffer_size += beacon.data.size();
            ++entries;
        }

        BeaconDataReplyHeader header{};
        header.max_output_size = out_buffer_size;
        header.total_size = static_cast<u32>(cur_buffer_size);
        header.total_entries = entries;
        if (!out_buffer->Write(&header, 0, sizeof(header))) {
            MH4N_LOG(Error, kLog, "RecvBeaconBroadcastData could not write the beacon data header");
        }
        MH4N_LOG(Debug, kLog,
                 "RecvBeaconBroadcastData out_buffer_size={:#x} wlan_comm_id={:#x} id={:#x} entries={}",
                 out_buffer_size, wlan_comm_id, id, entries);

        Reply reply(request.CommandId(), 1, 2);
        reply.Push(kResultSuccess);
        reply.PushMappedBuffer(*out_buffer);
        // On a real 3DS this is about 0.38 seconds.
        return ReplyAfter(request, kBeaconScanNs, std::move(reply));
    }

    Handled ConnectCommon(Request& request, const std::vector<u8>& info_bytes, u8 connection_type) {
        UdsState& s = S();
        NetworkInfo info{};
        std::memcpy(&info, info_bytes.data(), std::min(info_bytes.size(), sizeof(info)));
        s.network_info = info;
        s.conn_type = static_cast<ConnectionType>(connection_type);
        // StartConnectionSequence: Connecting, then an authentication frame with SEQ1 to the host.
        s.connection_status.status = static_cast<u32>(NetworkStatus::Connecting);
        rooms::WifiPacket auth_request;
        auth_request.channel = s.network_channel;
        auth_request.data = nwm::GenerateAuthenticationFrame(nwm::AuthenticationSeq::SEQ1);
        auth_request.destination = s.network_info.HostMac();
        auth_request.type = rooms::WifiPacket::Type::Authentication;
        SendPacket(auth_request);
        MH4N_LOG(Debug, kLog, "ConnectToNetwork");

        // Originally 300 ms, Azahar made it 5 s to accommodate high ping. The host's EAPoL-Logoff
        // completes the connection (FinishConnect); without it the sleep times out with the error.
        Kernel& kernel = Context().GetKernel();
        if (s.connect_timeout_set) {
            // a connection still waiting (Azahar sleeps the new client on a new event: the old one times out)
            kernel.Cancel(s.connect_timeout);
            s.connect_timeout_set = false;
            const std::shared_ptr<Request> old = std::move(s.connect_request);
            s.connect_request.reset();
            if (old != nullptr && !old->Finished() && !old->Cancelled()) {
                Reply timeout(old->CommandId(), 1, 0);
                timeout.Push(kErrConnectTimeout);
                old->Deliver(std::move(timeout));
            }
        }
        s.connect_request = request.Share();
        const std::weak_ptr<UdsState> weak = state_;
        s.connect_timeout = kernel.ScheduleAt(
            kernel.NowTicks() + NsToTicks(static_cast<u64>(kConnectionTimeoutNs)), [weak] {
                const std::shared_ptr<UdsState> locked = weak.lock();
                if (locked == nullptr) {
                    return;
                }
                locked->connect_timeout_set = false;
                const std::shared_ptr<Request> pending = std::move(locked->connect_request);
                locked->connect_request.reset();
                if (pending == nullptr || pending->Finished() || pending->Cancelled()) {
                    return;
                }
                MH4N_LOG(Error, kLog, "timed out when trying to connect to UDS server");
                Reply reply(pending->CommandId(), 1, 0);
                reply.Push(kErrConnectTimeout);
                pending->Deliver(std::move(reply));
            });
        s.connect_timeout_set = true;
        return Handled::Deferred;
    }

    // Inputs:  1: connection type, 2: passphrase size, 3-4: the network structure (static buffer 0),
    //          5-6: the passphrase (static buffer 2)
    Handled ConnectToNetwork(Request& request) {
        const u8 connection_type = request.Pop<u8>();
        request.Pop<u32>();  // passphrase size
        const StaticBuffer* network = request.PopStaticBuffer();
        request.PopStaticBuffer();  // the passphrase
        std::vector<u8> bytes = network != nullptr ? network->data : std::vector<u8>{};
        if (bytes.size() != sizeof(NetworkInfo)) {
            MH4N_LOG(Error, kLog, "ConnectToNetwork network structure of {} bytes, not {}",
                     bytes.size(), sizeof(NetworkInfo));
        }
        return ConnectCommon(request, bytes, connection_type);
    }

    // Inputs:  1-15: the first 0x3C bytes of the network structure, 16: connection type,
    //          17: passphrase size, 18-19: the passphrase
    Handled ConnectToNetworkDeprecated(Request& request) {
        const auto network = request.PopRaw<std::array<u8, 0x3C>>();
        const u8 connection_type = request.Pop<u8>();
        request.Pop<u32>();  // passphrase size
        request.PopStaticBuffer();
        return ConnectCommon(request, std::vector<u8>(network.begin(), network.end()), connection_type);
    }

    // ---- hosting ----

    Result DoBeginHosting(const std::vector<u8>& info_bytes) {
        UdsState& s = S();
        NetworkInfo info{};
        std::memcpy(&info, info_bytes.data(), std::min(info_bytes.size(), sizeof(info)));
        // The real UDS module throws a fatal error if this fails.
        if (info.max_nodes <= 1) {
            FatalRequest("trying to host a network of only one member");
            return kResultSuccess;
        }
        // Ensure the application data size is less than the maximum value.
        if (info.application_data_size > nwm::kApplicationDataSize) {
            FatalRequest("application data is too big");
            return kResultSuccess;
        }
        s.network_info = info;
        // Basic information for this network (Azahar: NintendoOUI and NintendoTagId::NetworkInfo).
        s.network_info.oui_value[0] = nwm::kNintendoOui[0];
        s.network_info.oui_value[1] = nwm::kNintendoOui[1];
        s.network_info.oui_value[2] = nwm::kNintendoOui[2];
        s.network_info.oui_type = kNintendoTagNetworkInfo;

        s.connection_status.status = static_cast<u32>(NetworkStatus::ConnectedAsHost);
        s.connection_status.status_change_reason =
            static_cast<u32>(NetworkStatusChangeReason::ConnectionEstablished);
        s.connection_status.max_nodes = s.network_info.max_nodes;

        // Resize the nodes list to hold max_nodes.
        s.node_info.clear();
        s.node_info.resize(s.network_info.max_nodes);

        // There's currently only one node in the network (the host).
        s.connection_status.total_nodes = 1;
        s.network_info.total_nodes = 1;

        // The host is always the first node.
        s.connection_status.network_node_id = 1;
        s.current_node.network_node_id = 1;
        s.connection_status.nodes[0] = s.connection_status.network_node_id;
        // Set the bit 0 in the nodes bitmask to indicate that node 1 is already taken.
        s.connection_status.node_bitmask |= 1;
        // Notify the application that the first node was set.
        s.connection_status.changed_nodes |= 1;

        s.network_info.SetHostMac(GetMacAddress(s));

        s.node_info[0] = s.current_node;

        // If the game has a preferred channel, use that instead.
        if (s.network_info.channel != 0) {
            s.network_channel = s.network_info.channel;
        } else {
            s.network_info.channel = kDefaultNetworkChannel;
        }

        Signal(s.connection_status_event);

        // Start broadcasting the network, send a beacon frame every 102.4 ms.
        ScheduleBeacon(Context().GetKernel(), state_, Context().GetKernel().NowTicks() + kBeaconIntervalTicks);
        return kResultSuccess;
    }

    // Inputs:  1: passphrase size, 2-3: the network structure (static buffer 0), 4-5: the passphrase
    void BeginHostingNetwork(Request& request) {
        request.Pop<u32>();  // passphrase size
        const StaticBuffer* network = request.PopStaticBuffer();
        request.PopStaticBuffer();  // the passphrase
        std::vector<u8> bytes = network != nullptr ? network->data : std::vector<u8>{};
        if (bytes.size() != sizeof(NetworkInfo)) {
            MH4N_LOG(Error, kLog, "BeginHostingNetwork network structure of {} bytes, not {}",
                     bytes.size(), sizeof(NetworkInfo));
        }
        MH4N_LOG(Debug, kLog, "BeginHostingNetwork");
        const Result result = DoBeginHosting(bytes);
        request.MakeReply(1, 0).Push(result);
    }

    // Inputs:  1-15: the first 0x3C bytes of the network structure, 16: passphrase size,
    //          17-18: the passphrase
    void BeginHostingNetworkDeprecated(Request& request) {
        const auto network = request.PopRaw<std::array<u8, 0x3C>>();
        request.Pop<u32>();  // passphrase size
        request.PopStaticBuffer();
        MH4N_LOG(Debug, kLog, "BeginHostingNetwork (deprecated)");
        const Result result = DoBeginHosting(std::vector<u8>(network.begin(), network.end()));
        request.MakeReply(1, 0).Push(result);
    }

    // Outputs: 1: result
    void DestroyNetwork(Request& request) {
        UdsState& s = S();
        // Unschedule the beacon broadcast event (before the check, as Azahar does).
        for (const u64 id : s.beacon_events) {
            Context().GetKernel().Cancel(id);
        }
        s.beacon_events.clear();
        // Only a host can destroy.
        if (!Is(s, NetworkStatus::ConnectedAsHost)) {
            MH4N_LOG(Warning, kLog, "DestroyNetwork called with status {}", s.connection_status.status);
            request.MakeReply(1, 0).Push(kErrWrongStatus);
            return;
        }
        // (a 3DS sends three deauthentication frames here: Azahar's TODO)
        const u16 node_id = s.connection_status.network_node_id;
        s.connection_status = {};
        s.connection_status.status = static_cast<u32>(NetworkStatus::NotConnected);
        s.connection_status.network_node_id = node_id;
        s.node_map.clear();
        Signal(s.connection_status_event);
        for (auto& bind_node : s.channel_data) {
            Signal(bind_node.second.event);
        }
        s.channel_data.clear();
        request.MakeReply(1, 0).Push(kResultSuccess);
        MH4N_LOG(Debug, kLog, "DestroyNetwork");
    }

    // Outputs: 1: result
    void DisconnectNetwork(Request& request) {
        UdsState& s = S();
        MH4N_LOG(Debug, kLog, "disconnecting from network");
        if (Is(s, NetworkStatus::ConnectedAsHost)) {
            // A real 3DS makes strange things here. We do the same.
            const u16 node_id = s.connection_status.network_node_id;
            s.connection_status = {};
            s.connection_status.status = static_cast<u32>(NetworkStatus::ConnectedAsHost);
            s.connection_status.network_node_id = node_id;
            s.node_map.clear();
            MH4N_LOG(Debug, kLog, "called as a host");
            request.MakeReply(1, 0).Push(kErrWrongStatus);
            return;
        }
        const u16 node_id = s.connection_status.network_node_id;
        s.connection_status = {};
        s.connection_status.status = static_cast<u32>(NetworkStatus::NotConnected);
        s.connection_status.network_node_id = node_id;
        s.node_map.clear();
        Signal(s.connection_status_event);

        rooms::WifiPacket deauth;
        deauth.channel = s.network_channel;
        deauth.destination = s.network_info.HostMac();
        deauth.type = rooms::WifiPacket::Type::Deauthentication;
        SendPacket(deauth);

        for (auto& bind_node : s.channel_data) {
            Signal(bind_node.second.event);
        }
        s.channel_data.clear();
        request.MakeReply(1, 0).Push(kResultSuccess);
    }

    // Inputs:  1: node id
    void EjectClient(Request& request) {
        const u16 network_node_id = request.Pop<u16>();
        MH4N_LOG(Warning, kLog, "(stubbed) EjectClient called");
        UdsState& s = S();
        Result result = kResultSuccess;
        if (network_node_id == 1) {
            // The host can not be kicked.
            result = kErrBindArgsZero;
        } else if (!Is(s, NetworkStatus::ConnectedAsHost)) {
            // Only the host can kick people.
            MH4N_LOG(Warning, kLog, "called with status {}", s.connection_status.status);
            result = kErrEjectNotHost;
        } else {
            std::optional<MacAddress> dest = rooms::kBroadcastMac;
            if (network_node_id != kBroadcastNetworkNodeId) {
                dest = GetNodeMacAddress(s, network_node_id, 0);  // none: no error
            }
            if (dest) {
                rooms::WifiPacket deauth;
                deauth.channel = s.network_channel;
                deauth.destination = *dest;
                deauth.type = rooms::WifiPacket::Type::Deauthentication;
                const int times = network_node_id == kBroadcastNetworkNodeId ? 3 : 1;
                for (int i = 0; i < times; ++i) {
                    rooms::WifiPacket copy = deauth;
                    SendPacket(copy);
                }
            }
        }
        request.MakeReply(1, 0).Push(result);
    }

    void EjectSpectators(Request& request) {
        MH4N_LOG(Warning, kLog, "(STUBBED) EjectSpectators called");
        request.MakeReply(1, 0).Push(kResultSuccess);
    }

    // Inputs:  1: node bitmask (u16), 2: flag (u8)
    void UpdateNetworkAttribute(Request& request) {
        // Stubbed in Azahar: success whatever the arguments.
        request.MakeReply(1, 0).Push(kResultSuccess);
    }

    // ---- application data ----

    // Inputs:  1: size, 2-3: the data (static buffer 0)
    void SetApplicationData(Request& request) {
        const u32 size = request.Pop<u32>();
        const StaticBuffer* buffer = request.PopStaticBuffer();
        const std::size_t have = buffer != nullptr ? buffer->data.size() : 0;
        if (have != size) {
            MH4N_LOG(Error, kLog, "SetApplicationData: the buffer has {} bytes, the call says {}", have,
                     size);
        }
        MH4N_LOG(Debug, kLog, "SetApplicationData");
        if (size > nwm::kApplicationDataSize) {
            request.MakeReply(1, 0).Push(kErrTooLargeUsage);
            return;
        }
        NetworkInfo& info = S().network_info;
        info.application_data_size = static_cast<u8>(size);
        if (buffer != nullptr) {
            std::memcpy(info.application_data, buffer->data.data(), std::min<std::size_t>(size, have));
        }
        request.MakeReply(1, 0).Push(kResultSuccess);
    }

    // Inputs:  1: the size of the caller's buffer
    // Outputs: 1: result, 2: the size of the data, 3-4: the data (static buffer 0)
    void GetApplicationData(Request& request) {
        const u32 input_size = request.Pop<u32>();
        const NetworkInfo& info = S().network_info;
        const u8 appdata_size = info.application_data_size;
        if (input_size < appdata_size) {
            Reply& reply = request.MakeReply(2, 0);
            reply.Push(kResultSuccess);
            reply.Push(u32{0});
            return;
        }
        Reply& reply = request.MakeReply(2, 2);
        reply.Push(kResultSuccess);
        reply.Push(appdata_size);
        reply.PushStaticBuffer(std::vector<u8>(info.application_data, info.application_data + appdata_size), 0);
    }

    // Inputs:  1-2: the network structure (static buffer 0), 3-4: the encrypted data of tag 24 (static buffer 1),
    //          5-6: the encrypted data of tag 25 (static buffer 2), each from its OUI
    // Outputs: 1: result, 2-3: the node list, 16 NodeInfo (static buffer 0)
    void DecryptBeaconData(Request& request) {
        const StaticBuffer* network = request.PopStaticBuffer();
        const StaticBuffer* data0 = request.PopStaticBuffer();
        const StaticBuffer* data1 = request.PopStaticBuffer();
        MH4N_LOG(Debug, kLog, "DecryptBeaconData");
        NetworkInfo info{};
        if (network != nullptr) {
            if (network->data.size() != sizeof(NetworkInfo)) {
                MH4N_LOG(Error, kLog, "DecryptBeaconData network structure of {} bytes, not {}",
                         network->data.size(), sizeof(NetworkInfo));
            }
            std::memcpy(&info, network->data.data(), std::min(network->data.size(), sizeof(info)));
        }
        const std::vector<u8> empty;
        const auto nodes = nwm::DecryptBeaconNodes(info, data0 != nullptr ? data0->data : empty,
                                                   data1 != nullptr ? data1->data : empty);
        if (!nodes) {
            MH4N_LOG(Error, kLog, "DecryptBeaconData: not the Great N's encrypted beacon data");
            request.MakeReply(1, 0).Push(kErrNodeNotFound);
            return;
        }
        std::vector<u8> output(sizeof(NodeInfo) * kUdsMaxNodes, 0);
        std::memcpy(output.data(), nodes->data(), sizeof(NodeInfo) * std::min<std::size_t>(nodes->size(), kUdsMaxNodes));
        Reply& reply = request.MakeReply(1, 2);
        reply.Push(kResultSuccess);
        reply.PushStaticBuffer(std::move(output), 0);
    }

    // ---- data channels ----

    // Inputs:  1: bind node id, 2: receive buffer size, 3: data channel, 4: network node id
    // Outputs: 1: result, 2: descriptor, 3: the data available event
    void Bind(Request& request) {
        const u32 bind_node_id = request.Pop<u32>();
        const u32 recv_buffer_size = request.Pop<u32>();
        const u8 data_channel = request.Pop<u8>();
        const u16 network_node_id = request.Pop<u16>();
        UdsState& s = S();

        if (data_channel == 0 || bind_node_id == 0) {
            MH4N_LOG(Warning, kLog, "data_channel = {}, bind_node_id = {}", data_channel, bind_node_id);
            request.MakeReply(1, 0).Push(kErrBindArgsZero);
            return;
        }
        if (s.channel_data.size() >= kMaxBindNodes) {
            MH4N_LOG(Warning, kLog, "max bind nodes");
            request.MakeReply(1, 0).Push(kErrBindMaxBinds);
            return;
        }
        if (recv_buffer_size < kMinRecvBufferSize) {
            MH4N_LOG(Warning, kLog, "MinRecvBufferSize");
            request.MakeReply(1, 0).Push(kErrTooLargeUsage);
            return;
        }
        if (s.channel_data.count(data_channel) != 0) {
            // Azahar asserts that the channel is free.
            MH4N_LOG(Error, kLog, "data channel {} is already bound; the new binding replaces it",
                     data_channel);
        }
        auto event = Context().GetKernel().NewEvent(
            ResetType::OneShot, "NWM::BindNodeEvent" + std::to_string(bind_node_id));
        // TODO(B3N30) in Azahar: support more than one bind node per channel.
        BindNodeData& data = s.channel_data[data_channel];
        data = {};
        data.bind_node_id = bind_node_id;
        data.channel = data_channel;
        data.network_node_id = network_node_id;
        data.event = event;
        Reply& reply = request.MakeReply(1, 2);
        reply.Push(kResultSuccess);
        reply.PushCopyObjects(std::move(event));
    }

    // Inputs:  1: bind node id
    // Outputs: 1: result, 2: the bind node id, 3-5: zero
    void Unbind(Request& request) {
        const u32 bind_node_id = request.Pop<u32>();
        if (bind_node_id == 0) {
            request.MakeReply(1, 0).Push(kErrBindArgsZero);
            return;
        }
        UdsState& s = S();
        const auto bound = std::find_if(s.channel_data.begin(), s.channel_data.end(), [&](const auto& d) {
            return d.second.bind_node_id == bind_node_id;
        });
        if (bound != s.channel_data.end()) {
            Signal(bound->second.event);
            s.channel_data.erase(bound);
        }
        Reply& reply = request.MakeReply(5, 0);
        reply.Push(kResultSuccess);
        reply.Push(bind_node_id);
        reply.Push(u32{0});
        reply.Push(u32{0});
        reply.Push(u32{0});
    }

    // Inputs:  1: bind node id, 2: output buffer size >> 2, 3: output buffer size
    // Outputs: 1: result, 2: the size received, 3: the source node, 4-5: the data (static buffer 0)
    void PullPacket(Request& request) {
        const u32 bind_node_id = request.Pop<u32>();
        const u32 max_out_buff_size_aligned = request.Pop<u32>();
        const u32 max_out_buff_size = request.Pop<u32>();
        UdsState& s = S();

        // This size is hard coded into the uds module. We don't know the meaning yet.
        const u32 buff_size = std::min<u32>(max_out_buff_size_aligned, kPullPacketBufferWords) << 2;

        if (!Is(s, NetworkStatus::ConnectedAsHost) && !Is(s, NetworkStatus::ConnectedAsClient) &&
            !Is(s, NetworkStatus::ConnectedAsSpectator)) {
            MH4N_LOG(Error, kLog, "Not connected yet.");
            request.MakeReply(1, 0).Push(kErrNotConnected);
            return;
        }
        const auto channel = std::find_if(s.channel_data.begin(), s.channel_data.end(), [&](const auto& d) {
            return d.second.bind_node_id == bind_node_id;
        });
        if (channel == s.channel_data.end()) {
            MH4N_LOG(Error, kLog, "Could not find channel bn {:#x}.", bind_node_id);
            request.MakeReply(1, 0).Push(kErrNotConnected);
            return;
        }
        std::deque<std::vector<u8>>& queue = channel->second.received_packets;
        if (queue.empty()) {
            // Azahar answers a zeroed buffer of buff_size bytes.
            Reply& reply = request.MakeReply(3, 2);
            reply.Push(kResultSuccess);
            reply.Push(u32{0});
            reply.Push(u16{0});
            reply.PushStaticBuffer(std::vector<u8>(buff_size, 0), 0);
            return;
        }
        const std::vector<u8>& next_packet = queue.front();
        const auto secure_data = nwm::ParseSecureDataHeader(next_packet);
        const u32 data_size = secure_data ? secure_data->ActualDataSize() : 0;
        if (data_size > max_out_buff_size) {
            MH4N_LOG(Error, kLog, "Data size was too large.");
            request.MakeReply(1, 0).Push(kErrTooLargeUsage);
            return;
        }
        std::vector<u8> output(buff_size, 0);
        const std::size_t offset = nwm::kLlcHeaderSize + nwm::kSecureDataHeaderSize;
        const std::size_t available = next_packet.size() > offset ? next_packet.size() - offset : 0;
        std::memcpy(output.data(), next_packet.data() + std::min(offset, next_packet.size()),
                    std::min<std::size_t>({data_size, available, output.size()}));
        const u16 src_node_id = secure_data ? secure_data->src_node_id : u16{0};
        queue.pop_front();
        Reply& reply = request.MakeReply(3, 2);
        reply.Push(kResultSuccess);
        reply.Push(data_size);
        reply.Push(src_node_id);
        reply.PushStaticBuffer(std::move(output), 0);
    }

    // Inputs:  2: destination node id, 3: data channel, 5: data size, 6: flags,
    //          7-8: the data (static buffer 0)
    void SendTo(Request& request) {
        request.Pop<u32>();  // unknown
        const u16 dest_node_id = request.Pop<u16>();
        const u8 data_channel = request.Pop<u8>();
        request.Pop<u32>();  // buffer size >> 2
        const u32 data_size = request.Pop<u32>();
        const u8 flags = request.Pop<u8>();
        const StaticBuffer* buffer = request.PopStaticBuffer();
        const std::size_t have = buffer != nullptr ? buffer->data.size() : 0;
        if (have < data_size) {
            MH4N_LOG(Error, kLog, "SendTo: the buffer has {} bytes, the call says {}", have, data_size);
        }
        UdsState& s = S();

        if (!Is(s, NetworkStatus::ConnectedAsClient) && !Is(s, NetworkStatus::ConnectedAsHost)) {
            MH4N_LOG(Error, kLog,
                     "You are not connected as a client or a host. (you are connected as type {})",
                     s.connection_status.status);
            request.MakeReply(1, 0).Push(kErrNotConnected);
            return;
        }
        // There should never be a dest_node_id of 0, and a node does not send to itself.
        if (dest_node_id == 0) {
            MH4N_LOG(Error, kLog, "dest_node_id is 0");
            request.MakeReply(1, 0).Push(kErrNodeNotFound);
            return;
        }
        if (dest_node_id == s.connection_status.network_node_id) {
            MH4N_LOG(Error, kLog, "tried to send packet to itself");
            request.MakeReply(1, 0).Push(kErrNodeNotFound);
            return;
        }
        if (flags >> 2) {
            MH4N_LOG(Error, kLog, "Unexpected flags {:#04x}", flags);
        }
        const std::optional<MacAddress> dest_address = GetNodeMacAddress(s, dest_node_id, flags);
        if (!dest_address) {
            MH4N_LOG(Error, kLog, "Destination address was 0");
            request.MakeReply(1, 0).Push(kErrNodeNotFound);
            return;
        }
        if (data_size > kMaxSendSize) {
            MH4N_LOG(Error, kLog, "Data size was greater than the max packet size {} > {}", data_size,
                     kMaxSendSize);
            request.MakeReply(1, 0).Push(kErrTooLargeUsage);
            return;
        }
        // Azahar's TODO: the sequence number should grow with each packet sent; it is 0.
        std::vector<u8> input = buffer != nullptr ? buffer->data : std::vector<u8>{};
        input.resize(data_size);
        rooms::WifiPacket packet;
        packet.destination = *dest_address;
        packet.channel = s.network_channel;
        packet.data = nwm::GenerateDataPayload(input, data_channel, dest_node_id, s.connection_status.network_node_id, 0);
        packet.type = rooms::WifiPacket::Type::Data;
        SendPacket(packet);
        request.MakeReply(1, 0).Push(kResultSuccess);
    }

    std::shared_ptr<UdsState> state_;
};

MH4N_REGISTER(ServiceFactory, "nwm::UDS",
              ServiceFactory([](ServiceContext& context) { return std::make_unique<NwmUds>(context); }));

// ---- the room and the runtime ----------------------------------------------------------------------------------

MH4N_OPTION("runner", "--room-join", "HOST:PORT",
            "join a network room (Azahar's multiplayer) before the game starts; the game's local wireless play then "
            "reaches the other members (N4)");
MH4N_OPTION("runner", "--room-host", "PORT",
            "host a network room on this UDP port and join it (N4)");
MH4N_OPTION("runner", "--nick", "NAME",
            "nickname in the room, 4 to 20 letters, digits, spaces, '.', '_' or '-' (default \"runner\")");
MH4N_OPTION("runner", "--room-password", "TEXT", "the room's password (with --room-join or --room-host)");

// What ties the runtime's local-wireless service to the process's room client: the listener that hands each frame to
// the scheduler thread, and the frame callback that gives the shared page the room's MAC address. Owned by the runtime
// (destroyed before the kernel) and stopped when the run ends.
struct RoomLink {
    u64 listener = 0;
    void Stop() {
        if (listener != 0) {
            rooms::Default().RemoveWifiListener(listener);
            listener = 0;
        }
    }
    ~RoomLink() { Stop(); }
};

std::string TitleOf(Runtime& rt) {
    Loader* loader = rt.GetLoader();
    if (loader == nullptr) {
        return {};
    }
    switch (loader->Image().program_id) {
    case rooms::kMh4uUsa:
    case rooms::kMh4uEurope:
        return "Monster Hunter 4 Ultimate";
    default:
        return loader->Image().name;
    }
}

// --room-join / --room-host: the reference runner's, before the game starts (Azahar's local-wireless service talks
// to the room the client is in when it starts). Waits until the room has taken or refused this console; a failure ends
// the run with status 5, as the reference's does.
bool JoinFromOptions(Runtime& rt, const std::string& console_id_hash) {
    const Options& options = rt.GetOptions();
    const bool join = options.Has("--room-join");
    const bool host = options.Has("--room-host");
    if (!join && !host) {
        return true;
    }
    if (join && host) {
        MH4N_LOG(Error, kLog, "--room-join and --room-host exclude each other");
        rt.RequestExit(2, "bad room options");
        return false;
    }
    const std::string nickname = options.GetString("--nick", "runner");
    if (!rooms::ValidNickname(nickname)) {
        MH4N_LOG(Error, kLog, "bad --nick: {}", nickname);
        rt.RequestExit(2, "bad --nick");
        return false;
    }
    rooms::JoinRequest request;
    request.nickname = nickname;
    request.password = options.GetString("--room-password");
    request.console_id_hash = console_id_hash;
    if (host) {
        const s64 port = options.GetInt("--room-host", 0);
        if (port <= 0 || port > 65535) {
            MH4N_LOG(Error, kLog, "bad --room-host: {}", options.GetString("--room-host"));
            rt.RequestExit(2, "bad --room-host");
            return false;
        }
        rooms::HostRequest room;
        room.port = static_cast<u16>(port);
        room.name = "mh4n_runner";
        room.slots = 16;
        room.password = request.password;
        room.host_username = nickname;
        std::string error;
        if (!rooms::Host(room, &error)) {
            MH4N_LOG(Error, kLog, "room: could not host on port {}: {}", port, error);
            rt.RequestExit(5, "could not host a room");
            return false;
        }
        std::printf("room: hosting on port %u\n", static_cast<unsigned>(port));
        request.host = "127.0.0.1";
        request.port = static_cast<u16>(port);
    } else {
        const std::string text = options.GetString("--room-join");
        const auto colon = text.rfind(':');
        const s64 port = colon == std::string::npos ? 0 : std::atoll(text.c_str() + colon + 1);
        if (colon == std::string::npos || colon == 0 || port <= 0 || port > 65535) {
            MH4N_LOG(Error, kLog, "bad --room-join: {} (want HOST:PORT)", text);
            rt.RequestExit(2, "bad --room-join");
            return false;
        }
        request.host = text.substr(0, colon);
        request.port = static_cast<u16>(port);
    }
    std::string error;
    if (!rooms::Join(request, &error)) {
        MH4N_LOG(Error, kLog, "room: {}", error);
        rt.RequestExit(5, "could not join the room");
        return false;
    }
    if (!rooms::WaitJoined(10000)) {
        const rooms::Status status = rooms::GetStatus();
        MH4N_LOG(Error, kLog, "room: could not join {}:{} ({}: {})", request.host, request.port,
                 rooms::StateName(status.state), rooms::ErrorName(status.error));
        rooms::Leave();
        rt.RequestExit(5, "could not join the room");
        return false;
    }
    const rooms::Status status = rooms::GetStatus();
    std::printf("room: joined %s:%u as %s, MAC %02X:%02X:%02X:%02X:%02X:%02X\n", request.host.c_str(),
                static_cast<unsigned>(request.port), nickname.c_str(), status.mac[0], status.mac[1], status.mac[2],
                status.mac[3], status.mac[4], status.mac[5]);
    std::fflush(stdout);
    return true;
}

// Azahar's UDS constructor puts the MAC address in the shared page (`GetSharedPageHandler().SetMacAddress(
// GetMacAddress())`: the room's when the emulator is in a room, else the cfg module's), when the system makes its
// services and before the game runs (D40). The name sorts after "services.cfg" (the console ID is made there) and
// "services.ipc", which makes the service manager whose context holds the cfg module's state. A runtime without a
// loader has no shared page. This step also publishes the console's ID and game for the room client (a Join that
// waits for the game starts now), joins the room the options ask for, and ties the service to the room client.
void StartNwm(Runtime& rt) {
    ServiceManager* manager = FindServiceManager(rt);
    if (manager == nullptr) {
        MH4N_LOG(Error, kLog, "the nwm module needs the service manager");
        return;
    }
    ServiceContext& context = manager->Context();
    const std::shared_ptr<UdsState> state = context.State<UdsState>("nwm::UDS");
    state->context = &context;

    rooms::Client& client = rooms::Default();
    const std::string console_id_hash = rooms::ConsoleIdHash(cfg::GetConsoleUniqueId(context));
    if (rt.GetLoader() != nullptr) {
        client.SetGameInfo({TitleOf(rt), rt.GetLoader()->Image().program_id});
    }
    if (!JoinFromOptions(rt, console_id_hash)) {
        return;
    }
    client.PublishConsole(console_id_hash);

    // Each frame of the room goes to the scheduler thread (the listener runs on the room client's thread).
    auto link = std::make_shared<RoomLink>();
    const std::weak_ptr<UdsState> weak = state;
    Kernel* kernel = &rt.GetKernel();
    link->listener = client.AddWifiListener([weak, kernel](const rooms::WifiPacket& packet) {
        if (weak.expired()) {
            return;
        }
        kernel->Post([weak, packet] {
            if (const std::shared_ptr<UdsState> locked = weak.lock()) {
                OnWifiPacketReceived(locked, packet);
            }
        });
    });
    rt.Own(link);
    rt.OnFinish([link] {
        link->Stop();
        rooms::Default().WithdrawConsole();
    });

    SystemPages* pages = rt.Pages();
    if (pages == nullptr) {
        return;
    }
    const MacAddress mac = GetMacAddress(*state);
    pages->SetMacAddress(mac);
    // A room joined later (the app's menu while the game runs) gives the console its MAC address, as the port's app
    // does: the page follows the room's address at the next frame.
    auto shown = std::make_shared<MacAddress>(mac);
    rt.OnFrame([pages, shown](u32, u64) {
        const std::optional<MacAddress> room = rooms::Default().RoomMac();
        if (room && *room != *shown) {
            *shown = *room;
            pages->SetMacAddress(*room);
            MH4N_LOG(Info, kLog, "the console's MAC address is the room's now");
        }
    });
}

MH4N_REGISTER(BootStep, "services.ipc.nwm", BootStep{Phase::Services, StartNwm});

}  // namespace

}  // namespace mh4n
