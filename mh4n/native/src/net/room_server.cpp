// Derived from Azahar src/network/room.cpp and src/network/room.h at 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6
// (GPL-2.0-or-later: both say "GPLv2 or any later version"), with the port's fix to HandleJoinRequest
// (port/azahar/azahar.patch, the src/network/room.cpp hunk): the room server, an ENet host of up to `slots` + 1 peers
// and one channel; the join request's checks and their order (full, password, nickname, MAC, console ID, version,
// bans); the MAC addresses it gives (the Great N's OUI and three random bytes); the relay of Wi-Fi packets (to the
// destination's member, or to every other member for the broadcast address); chat (cut to 500 bytes), game
// information, the room information broadcast, status messages, and moderation (kick, ban by username and address,
// unban, the ban list).
// Copyright 2017-2025 Citra Emulator Project / Azahar Emulator Project
//
// Departures from Azahar:
//  - Members' tokens are not checked with a lobby (Azahar's NullBackend: every member is unchecked).
//  - Received messages are checked: a short or malformed one is dropped (Azahar reads past its end).
//  - Close waits up to 500 ms for the members' disconnects to go out before the host is destroyed.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <random>
#include <thread>

#include <enet/enet.h>

#include "mh4n/log.h"
#include "mh4n/rooms.h"
#include "net_internal.h"
#include "packet.h"

namespace mh4n::rooms {

namespace {

constexpr const char* kLog = "Network.Room";
constexpr std::size_t kNumChannels = 1;
constexpr MacAddress kNintendoOui = {0x00, 0x1F, 0x32, 0x00, 0x00, 0x00};

std::string AddressText(const ENetAddress& address) {
    char raw[256] = {};
    enet_address_get_host_ip(&address, raw, sizeof(raw) - 1);
    return raw;
}

}  // namespace

using detail::Packet;

struct Server::Impl {
    struct UserData {
        std::string username;
        std::string display_name;
        std::string avatar_url;
        bool moderator = false;
    };
    struct RoomMember {
        std::string nickname;
        std::string console_id_hash;
        GameInfo game_info;
        MacAddress mac_address{};
        UserData user_data;
        ENetPeer* peer = nullptr;
    };

    ENetHost* server = nullptr;
    std::atomic<bool> open{false};
    RoomInformation room_information;
    std::string password;
    std::mt19937 random_gen{std::random_device()()};

    mutable std::mutex member_mutex;
    std::vector<RoomMember> members;
    mutable std::mutex ban_list_mutex;
    std::vector<std::string> username_ban_list;
    std::vector<std::string> ip_ban_list;
    mutable std::mutex info_mutex;  // room_information, for other threads

    std::thread thread;

    void Loop();
    void Send(ENetPeer* peer, const Packet& packet) {
        ENetPacket* out = enet_packet_create(packet.Data(), packet.Size(), ENET_PACKET_FLAG_RELIABLE);
        if (out != nullptr && enet_peer_send(peer, 0, out) != 0) {
            enet_packet_destroy(out);
        }
        enet_host_flush(server);
    }
    void SendType(ENetPeer* peer, u8 type) {
        Packet packet;
        packet << type;
        Send(peer, packet);
    }
    void HandleJoinRequest(const ENetEvent& event);
    void HandleModKick(const ENetEvent& event);
    void HandleModBan(const ENetEvent& event);
    void HandleModUnban(const ENetEvent& event);
    void HandleWifiPacket(const ENetEvent& event);
    void HandleChat(const ENetEvent& event);
    void HandleGameInfo(const ENetEvent& event);
    void HandleDisconnection(ENetPeer* client);
    void SendStatusMessage(u8 type, const std::string& nickname, const std::string& username, const std::string& ip);
    void BroadcastRoomInformation();
    void SendCloseMessage();
    bool HasModPermission(const ENetPeer* client) const;
    bool IsValidNickname(const std::string& nickname) const;
    bool IsValidMacAddress(const MacAddress& address) const;
    bool IsValidConsoleId(const std::string& console_id_hash) const;
    MacAddress GenerateMacAddress();
};

void Server::Impl::Loop() {
    while (open.load()) {
        ENetEvent event;
        if (enet_host_service(server, &event, 5) > 0) {
            switch (event.type) {
            case ENET_EVENT_TYPE_RECEIVE:
                if (event.packet->dataLength > 0) {
                    switch (event.packet->data[0]) {
                    case kIdJoinRequest: HandleJoinRequest(event); break;
                    case kIdSetGameInfo: HandleGameInfo(event); break;
                    case kIdWifiPacket: HandleWifiPacket(event); break;
                    case kIdChatMessage: HandleChat(event); break;
                    case kIdModKick: HandleModKick(event); break;
                    case kIdModBan: HandleModBan(event); break;
                    case kIdModUnban: HandleModUnban(event); break;
                    case kIdModGetBanList:
                        if (!HasModPermission(event.peer)) {
                            SendType(event.peer, kIdModPermissionDenied);
                        } else {
                            Packet packet;
                            packet << static_cast<u8>(kIdModBanListResponse);
                            {
                                std::lock_guard lock(ban_list_mutex);
                                packet << username_ban_list << ip_ban_list;
                            }
                            Send(event.peer, packet);
                        }
                        break;
                    default: break;
                    }
                }
                enet_packet_destroy(event.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                HandleDisconnection(event.peer);
                break;
            case ENET_EVENT_TYPE_NONE:
            case ENET_EVENT_TYPE_CONNECT:
                break;
            }
        }
    }
    SendCloseMessage();
}

void Server::Impl::HandleJoinRequest(const ENetEvent& event) {
    {
        std::lock_guard lock(member_mutex);
        if (members.size() >= room_information.member_slots) {
            SendType(event.peer, kIdRoomIsFull);
            return;
        }
    }
    Packet packet(event.packet->data, event.packet->dataLength);
    packet.IgnoreBytes(1);
    std::string nickname, console_id_hash, pass, token;
    MacAddress preferred_mac{};
    u32 client_version = 0;
    // (a request cut short reads empty strings and zeros from there on, as Azahar's does)
    packet >> nickname >> console_id_hash >> preferred_mac >> client_version >> pass >> token;

    if (pass != password) {
        SendType(event.peer, kIdWrongPassword);
        return;
    }
    if (!IsValidNickname(nickname)) {
        SendType(event.peer, kIdNameCollision);
        return;
    }
    if (preferred_mac != kNoPreferredMac) {
        if (!IsValidMacAddress(preferred_mac)) {
            SendType(event.peer, kIdMacCollision);
            return;
        }
    } else {
        preferred_mac = GenerateMacAddress();
    }
    if (!IsValidConsoleId(console_id_hash)) {
        SendType(event.peer, kIdConsoleIdCollision);
        return;
    }
    if (client_version != kNetworkVersion) {
        Packet answer;
        answer << static_cast<u8>(kIdVersionMismatch) << kNetworkVersion;
        Send(event.peer, answer);
        return;
    }

    RoomMember member;
    member.mac_address = preferred_mac;
    member.console_id_hash = console_id_hash;
    member.nickname = nickname;
    member.peer = event.peer;
    // (the lobby's check of the token: Azahar's NullBackend gives empty user data)

    // MH4U port: the host's rights go to the host's checked name (a lobby vouched for it), or to the host's own
    // client, which joins the room it created from this machine. A nickname alone is anyone's to type.
    const bool from_this_machine = event.peer->address.host == ENET_HOST_TO_NET_32(0x7F000001);
    if (!room_information.host_username.empty() &&
        (member.user_data.username == room_information.host_username ||
         (from_this_machine && nickname == room_information.host_username))) {
        member.user_data.moderator = true;
        MH4N_LOG(Info, kLog, "User {} is a moderator", room_information.host_username);
    }

    std::string ip;
    {
        std::lock_guard lock(ban_list_mutex);
        if (!member.user_data.username.empty() &&
            std::find(username_ban_list.begin(), username_ban_list.end(), member.user_data.username) !=
                username_ban_list.end()) {
            SendType(event.peer, kIdHostBanned);
            return;
        }
        ip = AddressText(event.peer->address);
        if (std::find(ip_ban_list.begin(), ip_ban_list.end(), ip) != ip_ban_list.end()) {
            SendType(event.peer, kIdHostBanned);
            return;
        }
    }

    SendStatusMessage(kStatusMemberJoin, member.nickname, member.user_data.username, ip);
    {
        std::lock_guard lock(member_mutex);
        members.push_back(std::move(member));
    }
    BroadcastRoomInformation();
    Packet answer;
    answer << static_cast<u8>(HasModPermission(event.peer) ? kIdJoinSuccessAsMod : kIdJoinSuccess) << preferred_mac;
    Send(event.peer, answer);
}

void Server::Impl::HandleModKick(const ENetEvent& event) {
    if (!HasModPermission(event.peer)) {
        SendType(event.peer, kIdModPermissionDenied);
        return;
    }
    Packet packet(event.packet->data, event.packet->dataLength);
    packet.IgnoreBytes(1);
    std::string nickname;
    packet >> nickname;
    std::string username, ip;
    {
        std::lock_guard lock(member_mutex);
        const auto target = std::find_if(members.begin(), members.end(),
                                         [&nickname](const RoomMember& m) { return m.nickname == nickname; });
        if (target == members.end()) {
            SendType(event.peer, kIdModNoSuchUser);
            return;
        }
        SendType(target->peer, kIdHostKicked);
        username = target->user_data.username;
        ip = AddressText(target->peer->address);
        enet_peer_disconnect(target->peer, 0);
        members.erase(target);
    }
    SendStatusMessage(kStatusMemberKicked, nickname, username, ip);
    BroadcastRoomInformation();
}

void Server::Impl::HandleModBan(const ENetEvent& event) {
    if (!HasModPermission(event.peer)) {
        SendType(event.peer, kIdModPermissionDenied);
        return;
    }
    Packet packet(event.packet->data, event.packet->dataLength);
    packet.IgnoreBytes(1);
    std::string nickname;
    packet >> nickname;
    std::string username, ip;
    {
        std::lock_guard lock(member_mutex);
        const auto target = std::find_if(members.begin(), members.end(),
                                         [&nickname](const RoomMember& m) { return m.nickname == nickname; });
        if (target == members.end()) {
            SendType(event.peer, kIdModNoSuchUser);
            return;
        }
        SendType(target->peer, kIdHostBanned);
        nickname = target->nickname;
        username = target->user_data.username;
        ip = AddressText(target->peer->address);
        enet_peer_disconnect(target->peer, 0);
        members.erase(target);
    }
    {
        std::lock_guard lock(ban_list_mutex);
        if (!username.empty() &&
            std::find(username_ban_list.begin(), username_ban_list.end(), username) == username_ban_list.end()) {
            username_ban_list.push_back(username);
        }
        if (std::find(ip_ban_list.begin(), ip_ban_list.end(), ip) == ip_ban_list.end()) {
            ip_ban_list.push_back(ip);
        }
    }
    SendStatusMessage(kStatusMemberBanned, nickname, username, ip);
    BroadcastRoomInformation();
}

void Server::Impl::HandleModUnban(const ENetEvent& event) {
    if (!HasModPermission(event.peer)) {
        SendType(event.peer, kIdModPermissionDenied);
        return;
    }
    Packet packet(event.packet->data, event.packet->dataLength);
    packet.IgnoreBytes(1);
    std::string address;
    packet >> address;
    bool unbanned = false;
    {
        std::lock_guard lock(ban_list_mutex);
        auto it = std::find(username_ban_list.begin(), username_ban_list.end(), address);
        if (it != username_ban_list.end()) {
            unbanned = true;
            username_ban_list.erase(it);
        }
        it = std::find(ip_ban_list.begin(), ip_ban_list.end(), address);
        if (it != ip_ban_list.end()) {
            unbanned = true;
            ip_ban_list.erase(it);
        }
    }
    if (unbanned) {
        SendStatusMessage(kStatusAddressUnbanned, address, "", "");
    } else {
        SendType(event.peer, kIdModNoSuchUser);
    }
}

bool Server::Impl::IsValidNickname(const std::string& nickname) const {
    if (!ValidNickname(nickname)) {
        return false;
    }
    std::lock_guard lock(member_mutex);
    return std::all_of(members.begin(), members.end(),
                       [&nickname](const RoomMember& m) { return m.nickname != nickname; });
}

bool Server::Impl::IsValidMacAddress(const MacAddress& address) const {
    std::lock_guard lock(member_mutex);
    return std::all_of(members.begin(), members.end(),
                       [&address](const RoomMember& m) { return m.mac_address != address; });
}

bool Server::Impl::IsValidConsoleId(const std::string& console_id_hash) const {
    std::lock_guard lock(member_mutex);
    return std::all_of(members.begin(), members.end(),
                       [&console_id_hash](const RoomMember& m) { return m.console_id_hash != console_id_hash; });
}

bool Server::Impl::HasModPermission(const ENetPeer* client) const {
    std::lock_guard lock(member_mutex);
    const auto sending = std::find_if(members.begin(), members.end(),
                                      [client](const RoomMember& m) { return m.peer == client; });
    if (sending == members.end()) {
        return false;
    }
    if (sending->user_data.moderator) {
        return true;
    }
    return !room_information.host_username.empty() &&
           sending->user_data.username == room_information.host_username;
}

void Server::Impl::SendCloseMessage() {
    Packet packet;
    packet << static_cast<u8>(kIdCloseRoom);
    std::lock_guard lock(member_mutex);
    if (!members.empty()) {
        ENetPacket* out = enet_packet_create(packet.Data(), packet.Size(), ENET_PACKET_FLAG_RELIABLE);
        for (const RoomMember& m : members) {
            enet_peer_send(m.peer, 0, out);
        }
    }
    enet_host_flush(server);
    for (const RoomMember& m : members) {
        enet_peer_disconnect(m.peer, 0);
    }
}

void Server::Impl::SendStatusMessage(u8 type, const std::string& nickname, const std::string& username,
                                     const std::string& ip) {
    Packet packet;
    packet << static_cast<u8>(kIdStatusMessage) << type << nickname << username;
    {
        std::lock_guard lock(member_mutex);
        if (!members.empty()) {
            ENetPacket* out = enet_packet_create(packet.Data(), packet.Size(), ENET_PACKET_FLAG_RELIABLE);
            for (const RoomMember& m : members) {
                enet_peer_send(m.peer, 0, out);
            }
        }
    }
    enet_host_flush(server);
    const std::string display_name = username.empty() ? nickname : nickname + " (" + username + ")";
    switch (type) {
    case kStatusMemberJoin: MH4N_LOG(Info, kLog, "[{}] {} has joined.", ip, display_name); break;
    case kStatusMemberLeave: MH4N_LOG(Info, kLog, "[{}] {} has left.", ip, display_name); break;
    case kStatusMemberKicked: MH4N_LOG(Info, kLog, "[{}] {} has been kicked.", ip, display_name); break;
    case kStatusMemberBanned: MH4N_LOG(Info, kLog, "[{}] {} has been banned.", ip, display_name); break;
    case kStatusAddressUnbanned: MH4N_LOG(Info, kLog, "{} has been unbanned.", display_name); break;
    default: break;
    }
}

void Server::Impl::BroadcastRoomInformation() {
    Packet packet;
    packet << static_cast<u8>(kIdRoomInformation) << room_information.name << room_information.description
           << room_information.member_slots << room_information.port << room_information.preferred_game
           << room_information.host_username;
    {
        std::lock_guard lock(member_mutex);
        packet << static_cast<u32>(members.size());
        for (const RoomMember& m : members) {
            packet << m.nickname << m.mac_address << m.game_info.name << m.game_info.id << m.user_data.username
                   << m.user_data.display_name << m.user_data.avatar_url;
        }
    }
    ENetPacket* out = enet_packet_create(packet.Data(), packet.Size(), ENET_PACKET_FLAG_RELIABLE);
    enet_host_broadcast(server, 0, out);
    enet_host_flush(server);
}

MacAddress Server::Impl::GenerateMacAddress() {
    MacAddress result = kNintendoOui;
    std::uniform_int_distribution<> dis(0x00, 0xFF);
    do {
        for (std::size_t i = 3; i < result.size(); ++i) {
            result[i] = static_cast<u8>(dis(random_gen));
        }
    } while (!IsValidMacAddress(result));
    return result;
}

void Server::Impl::HandleWifiPacket(const ENetEvent& event) {
    Packet in(event.packet->data, event.packet->dataLength);
    in.IgnoreBytes(1 + 1 + 1 + 6);  // message type, frame type, channel, transmitter
    MacAddress destination{};
    in >> destination;
    if (!in) {
        return;
    }
    ENetPacket* out = enet_packet_create(event.packet->data, event.packet->dataLength, ENET_PACKET_FLAG_RELIABLE);
    if (out == nullptr) {
        return;
    }
    std::lock_guard lock(member_mutex);
    if (destination == kBroadcastMac) {
        bool sent = false;
        for (const RoomMember& m : members) {
            if (m.peer != event.peer) {
                sent = true;
                enet_peer_send(m.peer, 0, out);
            }
        }
        if (!sent) {
            enet_packet_destroy(out);
        }
    } else {
        const auto target = std::find_if(members.begin(), members.end(),
                                         [&destination](const RoomMember& m) { return m.mac_address == destination; });
        if (target != members.end()) {
            enet_peer_send(target->peer, 0, out);
        } else {
            MH4N_LOG(Error, kLog,
                     "Attempting to send to unknown MAC address: {:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}",
                     destination[0], destination[1], destination[2], destination[3], destination[4], destination[5]);
            enet_packet_destroy(out);
        }
    }
    enet_host_flush(server);
}

void Server::Impl::HandleChat(const ENetEvent& event) {
    Packet in(event.packet->data, event.packet->dataLength);
    in.IgnoreBytes(1);
    std::string message;
    in >> message;
    std::lock_guard lock(member_mutex);
    const auto sending = std::find_if(members.begin(), members.end(),
                                      [&event](const RoomMember& m) { return m.peer == event.peer; });
    if (sending == members.end()) {
        return;  // a chat message from an unknown sender
    }
    message.resize(std::min<std::size_t>(message.size(), kMaxMessageSize));
    Packet out_packet;
    out_packet << static_cast<u8>(kIdChatMessage) << sending->nickname << sending->user_data.username << message;
    ENetPacket* out = enet_packet_create(out_packet.Data(), out_packet.Size(), ENET_PACKET_FLAG_RELIABLE);
    bool sent = false;
    for (const RoomMember& m : members) {
        if (m.peer != event.peer) {
            sent = true;
            enet_peer_send(m.peer, 0, out);
        }
    }
    if (!sent) {
        enet_packet_destroy(out);
    }
    enet_host_flush(server);
    if (sending->user_data.username.empty()) {
        MH4N_LOG(Info, kLog, "{}: {}", sending->nickname, message);
    } else {
        MH4N_LOG(Info, kLog, "{} ({}): {}", sending->nickname, sending->user_data.username, message);
    }
}

void Server::Impl::HandleGameInfo(const ENetEvent& event) {
    Packet in(event.packet->data, event.packet->dataLength);
    in.IgnoreBytes(1);
    GameInfo game_info;
    in >> game_info.name >> game_info.id;
    {
        std::lock_guard lock(member_mutex);
        const auto member = std::find_if(members.begin(), members.end(),
                                         [&event](const RoomMember& m) { return m.peer == event.peer; });
        if (member != members.end()) {
            member->game_info = game_info;
            const std::string display_name = member->user_data.username.empty()
                                                 ? member->nickname
                                                 : member->nickname + " (" + member->user_data.username + ")";
            if (game_info.name.empty()) {
                MH4N_LOG(Info, kLog, "{} is not playing", display_name);
            } else {
                MH4N_LOG(Info, kLog, "{} is playing {}", display_name, game_info.name);
            }
        }
    }
    BroadcastRoomInformation();
}

void Server::Impl::HandleDisconnection(ENetPeer* client) {
    std::string nickname, username, ip;
    {
        std::lock_guard lock(member_mutex);
        const auto member = std::find_if(members.begin(), members.end(),
                                         [client](const RoomMember& m) { return m.peer == client; });
        if (member != members.end()) {
            nickname = member->nickname;
            username = member->user_data.username;
            ip = AddressText(member->peer->address);
            members.erase(member);
        }
    }
    enet_peer_disconnect(client, 0);
    if (!nickname.empty()) {
        SendStatusMessage(kStatusMemberLeave, nickname, username, ip);
    }
    BroadcastRoomInformation();
}

// ---- Server -------------------------------------------------------------------------------------------------------

Server::Server() : impl_(new Impl) {}

Server::~Server() {
    Close();
    delete impl_;
}

bool Server::Open(const HostRequest& request, std::string* error) {
    const auto fail = [error](const std::string& why) {
        if (error != nullptr) {
            *error = why;
        }
        return false;
    };
    if (impl_->open.load()) {
        return fail("A room is open already.");
    }
    if (request.slots < 2 || request.slots > kMaxConcurrentConnections) {
        return fail("A room takes 2 to 254 members.");
    }
    if (!detail::EnsureEnet()) {
        return fail("The network library didn't start.");
    }
    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    if (!request.bind_address.empty() && enet_address_set_host(&address, request.bind_address.c_str()) != 0) {
        return fail("Unknown address " + request.bind_address + ".");
    }
    address.port = request.port;
    // one slot more than members, so that a client gets the room-is-full message instead of no answer
    impl_->server = enet_host_create(&address, request.slots + 1, kNumChannels, 0, 0);
    if (impl_->server == nullptr) {
        return fail("Could not open UDP port " + std::to_string(request.port) + " (is another room using it?).");
    }
    {
        std::lock_guard lock(impl_->info_mutex);
        impl_->room_information = {};
        impl_->room_information.name = request.name;
        impl_->room_information.description = request.description;
        impl_->room_information.member_slots = request.slots;
        impl_->room_information.port = request.port;
        impl_->room_information.preferred_game = request.preferred_game;
        impl_->room_information.preferred_game_id = request.preferred_game_id;
        impl_->room_information.host_username = request.host_username;
    }
    impl_->password = request.password;
    {
        std::lock_guard lock(impl_->ban_list_mutex);
        impl_->username_ban_list = request.banned_usernames;
        impl_->ip_ban_list = request.banned_addresses;
    }
    impl_->open = true;
    impl_->thread = std::thread([impl = impl_] { impl->Loop(); });
    MH4N_LOG(Info, kLog, "room \"{}\" open on UDP port {} ({} members at most)", request.name, request.port,
             request.slots);
    return true;
}

void Server::Close() {
    if (!impl_->open.exchange(false)) {
        return;
    }
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
    // let the close message and the disconnects go out
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < deadline) {
        ENetEvent event;
        const int got = enet_host_service(impl_->server, &event, 20);
        if (got > 0 && event.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(event.packet);
        }
        bool any = false;
        for (std::size_t i = 0; i < impl_->server->peerCount; ++i) {
            const ENetPeerState state = impl_->server->peers[i].state;
            any = any || (state != ENET_PEER_STATE_DISCONNECTED && state != ENET_PEER_STATE_ZOMBIE);
        }
        if (!any || got < 0) {
            break;
        }
    }
    enet_host_destroy(impl_->server);
    impl_->server = nullptr;
    {
        std::lock_guard lock(impl_->member_mutex);
        impl_->members.clear();
    }
    MH4N_LOG(Info, kLog, "room closed");
}

bool Server::IsOpen() const {
    return impl_->open.load();
}

RoomInformation Server::Information() const {
    std::lock_guard lock(impl_->info_mutex);
    return impl_->room_information;
}

std::vector<Member> Server::Members() const {
    std::vector<Member> list;
    std::lock_guard lock(impl_->member_mutex);
    for (const Impl::RoomMember& m : impl_->members) {
        Member member;
        member.nickname = m.nickname;
        member.username = m.user_data.username;
        member.display_name = m.user_data.display_name;
        member.avatar_url = m.user_data.avatar_url;
        member.mac = m.mac_address;
        member.game = m.game_info;
        list.push_back(std::move(member));
    }
    return list;
}

std::pair<std::vector<std::string>, std::vector<std::string>> Server::BanList() const {
    std::lock_guard lock(impl_->ban_list_mutex);
    return {impl_->username_ban_list, impl_->ip_ban_list};
}

Server& DefaultServer() {
    static Server server;
    return server;
}

bool Host(const HostRequest& request, std::string* error) {
    return DefaultServer().Open(request, error);
}

void CloseRoom() {
    DefaultServer().Close();
}

bool IsHosting() {
    return DefaultServer().IsOpen();
}

}  // namespace mh4n::rooms
