// Derived from Azahar src/network/room.h, src/network/room_member.h and src/common/announce_multiplayer_room.h at
// 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-2.0-or-later: each of them says "GPLv2 or any later version"): the
// protocol's constants (network_version 4, the default port, the message size, the member limit), the message and status
// types, the member, room information and Wi-Fi packet structures, the states and errors of a room member, and the
// fields of a lobby's room list.
// Copyright 2017-2025 Citra Emulator Project / Azahar Emulator Project (room.h)
// Copyright 2017-2024 Citra Emulator Project / Azahar Emulator Project (room_member.h)
// Copyright 2017-2026 Citra Emulator Project / Azahar Emulator Project (announce_multiplayer_room.h)
//
// The network rooms (N4-01, D7): Azahar's multiplayer rooms, an ENet server that relays the local-wireless frames of
// every member's console to the others, so MH4U's local play (the Gathering Hall, hunts together) works over the
// internet. The protocol is Azahar's, `network_version` 4, both ways: this client joins rooms of official Azahar, of the
// port's Azahar build (`azahar-room`, Create Room, the Docker room) and of another native runtime, and the room this
// process hosts takes all of them. The local-wireless service (nwm::UDS, native/src/services/nwm) carries the game's
// frames over the room the client is in.
//
// Threads: every function here may be called from any thread (the app's UI thread, a JNI thread, the game's). None
// blocks for long: Join and Leave start the work and return; the room client and the hosted room each run on a thread
// of their own. GetStatus and TakeEvents are how an app follows what happens: poll them (once a frame is plenty), or
// wait for `Status::revision` to change.
//
// A typical app:
//
//     mh4n::rooms::JoinRequest join;
//     join.host = "203.0.113.7";              // or a host name
//     join.port = mh4n::rooms::kDefaultPort;   // 24872
//     join.nickname = "Hunter One";            // 4 to 20 letters, digits, spaces, '.', '_', '-'
//     join.password = "";                      // the room's, if it has one
//     if (!mh4n::rooms::Join(join, &error)) { show(error); }
//     ...
//     const mh4n::rooms::Status status = mh4n::rooms::GetStatus();  // state, error, members, room
//     for (const mh4n::rooms::Event& event : mh4n::rooms::TakeEvents()) { ... chat, joins, leaves ... }
//     mh4n::rooms::SendChat("ready?");
//     mh4n::rooms::Leave();
//
// The console's identity: a room lets in one member per console ID (the SHA-256 of the console's unique ID, as Azahar's
// GetConsoleIdHash). With `JoinRequest::console_id_hash` empty the client uses the running game's, which the runtime
// publishes when it boots (PublishConsole); a Join made before the game boots waits for it, in state Joining. Two copies
// of one user folder have the same ID: give one a new ID first (`--new-console-id`).
//
// Hosting: Host opens a room in this process (Azahar's Create Room, or `azahar-room` without a game), then the app joins
// it like any other, at 127.0.0.1 and the hosted port. The room lives until CloseRoom (or the process ends), whether or
// not this console is in it.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "mh4n/types.h"

namespace mh4n::rooms {

// ---- the protocol -----------------------------------------------------------------------------------------------

constexpr u32 kNetworkVersion = 4;  ///< Azahar's network_version: a room turns away a member of another version
constexpr u16 kDefaultPort = 24872;
constexpr u32 kMaxMessageSize = 500;              ///< a room cuts chat messages to this many bytes
constexpr u32 kMaxConcurrentConnections = 254;    ///< the most members a room can take
constexpr u64 kMh4uUsa = 0x0004000000126300;      ///< MH4U's program ids (the room's preferred game)
constexpr u64 kMh4uEurope = 0x0004000000126100;

using MacAddress = std::array<u8, 6>;
/// Asks the room to give this member a MAC address of its own (the default).
constexpr MacAddress kNoPreferredMac = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
/// 802.11's broadcast address: a frame for it goes to every other member.
constexpr MacAddress kBroadcastMac = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/// The first byte of every message (RoomMessageTypes). Exposed for tests and tools.
enum MessageType : u8 {
    kIdJoinRequest = 1,
    kIdJoinSuccess,
    kIdRoomInformation,
    kIdSetGameInfo,
    kIdWifiPacket,
    kIdChatMessage,
    kIdNameCollision,
    kIdMacCollision,
    kIdVersionMismatch,
    kIdWrongPassword,
    kIdCloseRoom,
    kIdRoomIsFull,
    kIdConsoleIdCollision,
    kIdStatusMessage,
    kIdHostKicked,
    kIdHostBanned,
    kIdModKick,
    kIdModBan,
    kIdModUnban,
    kIdModGetBanList,
    kIdModBanListResponse,
    kIdModPermissionDenied,
    kIdModNoSuchUser,
    kIdJoinSuccessAsMod,
};

/// The kinds of a room's status message (StatusMessageTypes).
enum StatusType : u8 {
    kStatusMemberJoin = 1,
    kStatusMemberLeave,
    kStatusMemberKicked,
    kStatusMemberBanned,
    kStatusAddressUnbanned,
};

/// What a member plays (the room shows it next to the nickname).
struct GameInfo {
    std::string name;
    u64 id = 0;
};

/// A member of the room, as the room tells every member.
struct Member {
    std::string nickname;
    std::string username;      ///< a lobby vouched for it (empty: not checked)
    std::string display_name;  ///< the lobby's display name (often empty)
    std::string avatar_url;
    GameInfo game;
    MacAddress mac{};
};

/// What the room says about itself. `preferred_game_id` is not on the wire (Azahar's room does not send it): it is
/// known only for the room this process hosts.
struct RoomInformation {
    std::string name;
    std::string description;
    u32 member_slots = 0;
    u16 port = 0;
    std::string preferred_game;
    u64 preferred_game_id = 0;
    std::string host_username;
};

/// A local-wireless frame between consoles (Azahar's WifiPacket, the room's own 802.11 header).
struct WifiPacket {
    enum class Type : u8 { Beacon, Data, Authentication, AssociationResponse, Deauthentication, NodeMap };
    Type type = Type::Data;  ///< other values are possible on the wire (the port's net sync uses 0x4d)
    std::vector<u8> data;    ///< the frame's body, from the management frame header
    MacAddress transmitter{};
    MacAddress destination{};
    u8 channel = 0;
};

// ---- the room client --------------------------------------------------------------------------------------------

enum class State : u8 {
    Idle,       ///< not in a room
    Joining,    ///< connecting, waiting for the room's answer, or waiting for the game's console ID
    Joined,     ///< in the room
    Moderator,  ///< in the room, and allowed to kick and ban
};

/// Why the client left a room, or why the room did not take it, or why a moderation request failed.
enum class Error : u8 {
    None,
    LostConnection,      ///< the connection broke, or the room closed
    HostKicked,
    UnknownError,        ///< the network could not be used (no address, no socket)
    NameCollision,       ///< the nickname is taken in that room, or is not a valid nickname
    MacCollision,
    ConsoleIdCollision,  ///< a member with the same console ID is in the room
    WrongVersion,
    WrongPassword,
    CouldNotConnect,     ///< no answer from that address and port
    RoomIsFull,
    HostBanned,
    PermissionDenied,    ///< a moderation request without the right to make it
    NoSuchUser,          ///< a moderation request for a nickname that is not in the room
};

const char* StateName(State state);
const char* ErrorName(Error error);
/// A sentence for the player, and what to try.
const char* ErrorMessage(Error error);

/// Azahar's rule for nicknames (and lobby usernames): 4 to 20 letters, digits, spaces, '.', '_' or '-'.
bool ValidNickname(const std::string& nickname);

struct JoinRequest {
    std::string host = "127.0.0.1";  ///< an IPv4 address or a host name
    u16 port = kDefaultPort;
    std::string nickname;
    std::string password;            ///< the room's password, or empty
    std::string token;               ///< the player's lobby token: the room checks the name with its lobby (rooms
                                     ///< without a lobby ignore it)
    std::string console_id_hash;     ///< empty: the running game's (PublishConsole)
    std::optional<MacAddress> preferred_mac;  ///< none: the room gives one (Azahar's NoPreferredMac)
};

/// Something that happened in the room, for the app's chat and notices.
struct Event {
    enum class Kind : u8 {
        StateChanged,     ///< `state`
        Failed,           ///< `error`: the room refused, dropped or kicked this client, or a moderation request failed
        Chat,             ///< `nickname` (and `username`) said `message`
        MemberJoined,     ///< `nickname`, `username`
        MemberLeft,
        MemberKicked,
        MemberBanned,
        AddressUnbanned,  ///< `nickname` holds the address or username
        RoomChanged,      ///< the room's information or its member list changed (GetStatus has the new ones)
        BanList,          ///< `ban_list` (the answer to RequestBanList)
    };
    Kind kind = Kind::RoomChanged;
    State state = State::Idle;
    Error error = Error::None;
    std::string nickname;
    std::string username;
    std::string message;
    std::vector<std::string> ban_list;  ///< usernames, then addresses
};

struct Status {
    State state = State::Idle;
    Error error = Error::None;   ///< the last failure (cleared by the next Join)
    bool waiting_for_console = false;  ///< Joining, and waiting for the game to publish its console ID
    std::string host;            ///< the room joined (or being joined)
    u16 port = 0;
    std::string nickname;
    std::string username;        ///< as the room's lobby checked it (empty: not checked)
    MacAddress mac{};            ///< the MAC address the room gave this console (Joined or Moderator)
    RoomInformation room;
    std::vector<Member> members; ///< every member, this client included
    u64 revision = 0;            ///< changes whenever anything above changes
    // the room this process hosts
    bool hosting = false;
    u16 hosted_port = 0;
    std::string hosted_name;
};

/// One room client. The functions below the class use the process's own (`Default()`), which is the one the game's
/// local-wireless service talks through; tests and tools may make more.
class Client {
public:
    Client();
    ~Client();  ///< leaves and waits for the network thread
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    /// Starts joining (and leaves a room the client is in first). False, with `*error` set (if given), only for a
    /// request that cannot work: an invalid nickname or an empty host. The answer comes as the state.
    bool Join(const JoinRequest& request, std::string* error = nullptr);
    /// Leaves the room (a Join that waits is cancelled). Returns at once; the goodbye to the room finishes on that
    /// join's network thread (at most 5 s), while a following Join starts on a thread of its own.
    void Leave();
    /// Waits until the client is not Joining any more, or `timeout_ms` passed. True when it is in a room.
    bool WaitJoined(int timeout_ms);

    Status GetStatus() const;
    State GetState() const;
    bool IsConnected() const;  ///< Joining (connected), Joined or Moderator, as Azahar's IsConnected
    /// The events since the last call (at most 256 are kept; older ones are dropped).
    std::vector<Event> TakeEvents();

    bool SendChat(const std::string& message);  ///< false when not in a room
    void SetGameInfo(const GameInfo& game);      ///< kept, and sent to the room when in one
    /// Moderation (a moderator only; the answer is an Event).
    bool Kick(const std::string& nickname);
    bool Ban(const std::string& nickname);
    bool Unban(const std::string& username_or_address);
    bool RequestBanList();

    /// The console's identity, for Joins without a console ID: the runtime publishes it at boot (StartNwm). A Join
    /// waiting for it starts now. `WithdrawConsole` when the game stops.
    void PublishConsole(const std::string& console_id_hash);
    void WithdrawConsole();

    // ---- for the local-wireless service ----
    using WifiListener = std::function<void(const WifiPacket&)>;
    /// `listener` runs on the network thread for each frame the room relays to this client. Remove returns only when no
    /// call of that listener runs, so what the listener uses may go away right after.
    u64 AddWifiListener(WifiListener listener);
    void RemoveWifiListener(u64 id);
    /// Sends a frame to the room, from this client's room MAC address (the transmitter is set). False when not in a
    /// room (Joined or Moderator), as Azahar's SendPacket drops it.
    bool SendWifi(WifiPacket packet);
    /// The MAC address the room gave this client, when it is in one.
    std::optional<MacAddress> RoomMac() const;

    struct Impl;

private:
    Impl* impl_;
};

/// The process's own room client.
Client& Default();

// The default client's functions, as described in Client.
bool Join(const JoinRequest& request, std::string* error = nullptr);
void Leave();
bool WaitJoined(int timeout_ms);
Status GetStatus();
std::vector<Event> TakeEvents();
bool SendChat(const std::string& message);

/// Azahar's GetConsoleIdHash: the lowercase hex SHA-256 of the console's unique ID (8 bytes, little-endian).
std::string ConsoleIdHash(u64 console_unique_id);

// ---- hosting a room -----------------------------------------------------------------------------------------------

struct HostRequest {
    u16 port = kDefaultPort;
    std::string bind_address;   ///< empty: every address of this machine
    std::string name = "MH4U Gathering Hall";
    std::string description;
    std::string password;
    u32 slots = 4;              ///< 2 to kMaxConcurrentConnections (Azahar's Create Room: 2 to 16)
    /// The host's nickname: that member gets a moderator's rights when it joins from this machine (the port's fix to
    /// Azahar's room: a nickname alone is anyone's to type).
    std::string host_username;
    std::string preferred_game = "Monster Hunter 4 Ultimate";
    u64 preferred_game_id = kMh4uUsa;
    std::vector<std::string> banned_usernames;
    std::vector<std::string> banned_addresses;
};

/// A room server (Azahar's Network::Room). Members' lobby tokens are not checked (Azahar's NullBackend): every member
/// is unchecked, and the room is not announced to a lobby.
class Server {
public:
    Server();
    ~Server();  ///< closes the room
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    /// Opens the room and starts its thread. False (with `*error`) when the port cannot be bound, or a room is open.
    bool Open(const HostRequest& request, std::string* error = nullptr);
    /// Tells the members the room closes, and closes it.
    void Close();
    bool IsOpen() const;
    RoomInformation Information() const;
    std::vector<Member> Members() const;
    /// The ban list (usernames, addresses), to keep between sessions.
    std::pair<std::vector<std::string>, std::vector<std::string>> BanList() const;

    struct Impl;

private:
    Impl* impl_;
};

/// The process's own hosted room.
Server& DefaultServer();
bool Host(const HostRequest& request, std::string* error = nullptr);
void CloseRoom();
bool IsHosting();

// ---- a lobby's list of rooms ----------------------------------------------------------------------------------------

/// A room of a lobby's list (GET <lobby>/lobby, Azahar's web API: server/lobby/mh4u_lobby.py speaks it).
struct LobbyRoom {
    std::string name, description, owner, address, game;
    u16 port = 0;
    u64 game_id = 0;
    u32 max_players = 0;
    bool password = false;
    std::vector<std::string> players;  ///< nicknames
    bool preferred = false;            ///< set up for the game asked for
};

struct LobbyList {
    bool ok = false;
    std::string error;
    std::vector<LobbyRoom> rooms;  ///< the preferred game's first, then by name
};

/// The rooms a lobby lists, `game_id`'s first. Plain HTTP only (as Azahar's web client); blocks for at most about
/// `timeout_ms`: call it on a worker thread.
LobbyList FetchLobby(const std::string& lobby_url, u64 game_id = kMh4uUsa, int timeout_ms = 5000);
/// The JSON a lobby answers with, as rooms.
LobbyList ParseLobby(const std::string& json, u64 game_id = kMh4uUsa);
/// A lobby address as typed ("lobby.example.org:24872") with its scheme ("http://lobby.example.org:24872").
std::string NormalizeLobbyUrl(const std::string& typed);

}  // namespace mh4n::rooms
