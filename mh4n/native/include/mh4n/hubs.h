// Internet hubs: a room this process hosts, listed on a hub server and reached through it, so players find each
// other and nobody forwards a port.
//
// The hub server is MHGURecomp's (its repository's server/, also the community instance its players run): it lists
// hubs and pipes each player's connection to one the hub's host opens for them, and passes UDP datagrams between the
// two ends of such a pair. It reads nothing of what goes through a pair, and it keeps the hubs of each "link protocol"
// apart: this port's hubs use their own numbers (LinkProtocol, one per region of the game), so MHGU's players never see
// them, nor these MHGU's, nor a European copy an American copy's hub.
//
// A hub is a network room (mh4n/rooms.h) that this process hosts on the loopback interface; the players' room clients
// reach it through a tunnel instead of over the internet directly:
//
//     player's room client --UDP--> 127.0.0.1:tunnel  ==(hub server: UDP relay, or the spliced TCP link)==>
//     host's endpoint for that player (127.77.x.y) --UDP--> 127.0.0.1:room
//
// Everything a room gives works the same in a hub: the members, the chat, the password, the moderator, the console IDs
// and MAC addresses, and the game's local wireless, which the runtime's nwm::UDS carries over whatever room the client
// is in. The room's ENet datagrams go by UDP through the server when that gets through (they are small: a tunnelled join
// asks for an ENet MTU of kTunnelMtu, so no datagram is cut into IP fragments on a mobile network), else, and for
// anything larger, through the TCP link. Both ends write a line a minute to the log while a link carries datagrams.
//
// Threads: every function may be called from any thread. List blocks (up to its timeout): call it on a worker thread.
// The others return at once; the work is done on threads of this module's own.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mh4n/rooms.h"
#include "mh4n/types.h"

namespace mh4n::hubs {

constexpr u16 kServerPort = 39510;      ///< the hub server's usual port (TCP and UDP)
constexpr u16 kTunnelVersion = 1;       ///< this module's own link through the server (the frames after a splice)
constexpr u16 kTunnelMtu = 1200;        ///< the ENet MTU a tunnelled join asks for (rooms::JoinRequest::mtu)
constexpr std::size_t kListingSize = 0x480;  ///< what the server keeps of a hub (sized as MHGU's NetworkInfo)
constexpr u32 kMaxListSlots = 8;        ///< the server takes hubs of at most 8 places

/// The game's region: copies of different regions don't play together (each has its own update), so neither do their
/// hubs. Monster Hunter 4G (Japan) has none.
enum class Region : u8 { None = 0, Americas = 1, Europe = 2 };

Region RegionOfProgram(u64 program_id);
const char* RegionName(Region region);
/// The hub server's link protocol for this port's hubs of `region` (0 for None): 0x3400 + 0x10 * (kTunnelVersion - 1) +
/// the region. MHGU's hubs use its own PROTO_VERSION (2 and up).
u16 LinkProtocol(Region region);

/// The game this process runs, for hubs: the runtime says so when the game boots (StartNwm). 0 before that.
void SetGameProgram(u64 program_id);
u64 GameProgram();

/// A hub, as its listing on the server says.
struct Hub {
    u64 id = 0;                        ///< the server's
    std::string name;                  ///< up to 63 bytes of UTF-8
    std::string description;          ///< up to 127 bytes (tags: "HR 7, G-rank, English")
    std::string host;                  ///< the host's nickname
    std::vector<std::string> hunters;  ///< everyone in it (at most 8 shown)
    u32 players = 1, slots = 4;
    bool password = false;
    bool crossplay = false;            ///< the room also takes Azahar and Citra players at its own address
    Region region = Region::None;
    u64 program_id = 0;
    std::string app_version;           ///< the host's app ("1.0.1")
};

/// The 0x480 bytes the server lists. It checks five of them (a NetworkInfo's SSID length, network type, node count,
/// node limit and advertise size), which hold valid values; the hub's own fields are elsewhere, after the magic "M4HB".
std::array<u8, kListingSize> EncodeListing(const Hub& hub);
/// The hub a listing describes (`id` left 0), or none for a listing that isn't one of these.
std::optional<Hub> DecodeListing(const u8* data, std::size_t size);

/// "name", "name:port" or "1.2.3.4:port" (kServerPort without a port), spaces around it ignored, an "http://" in front
/// and a "/" behind dropped (as typed from a web page). False when it isn't a server's address.
bool ParseServer(const std::string& text, std::string* host, u16* port);

/// What a hub server answered last.
enum class ServerState : u8 {
    Unknown,      ///< not asked yet
    Checking,     ///< being asked
    Online,       ///< it answered
    NotFound,     ///< its name has no address
    Unreachable,  ///< no connection
    Lost,         ///< the connection broke
    NoAnswer,     ///< connected, but no answer (or not a hub server)
    Busy,         ///< one of its limits was reached (too many hubs or requests from this address)
    Version,      ///< it speaks another version of the server protocol
    Refused,      ///< it refused the request
};
const char* ServerStateName(ServerState state);

struct ListResult {
    ServerState state = ServerState::Unknown;
    int ms = -1;            ///< the request's round trip
    std::vector<Hub> hubs;  ///< free places first (the server's order)
};

/// The hubs `server` lists for `region` and `key` (empty: the public ones). Blocks up to about `timeout_ms`.
ListResult List(const std::string& server, Region region, const std::string& key = {}, int timeout_ms = 3000);

// ---- hosting -------------------------------------------------------------------------------------------------------

struct HostRequest {
    std::string server;              ///< "name[:port]"
    std::string key;                 ///< empty: public
    u64 program_id = 0;              ///< the game (its region chooses the link protocol)
    u16 room_port = 0;               ///< the room this process hosts, reached at 127.0.0.1
    std::string name, description;   ///< as listed
    bool password = false;           ///< the room has one (shown as a lock)
    bool crossplay = false;
    std::string app_version;
    rooms::Server* room = nullptr;   ///< the hosted room whose members are listed (null: rooms::DefaultServer())
};

/// Lists the room on the server and takes the players it sends. Stops by itself when the room closes. False (with
/// `*error`) for a request that can't work (no server, no region, no room port).
bool Host(const HostRequest& request, std::string* error = nullptr);
void StopHosting();

struct HostStatus {
    bool active = false;
    ServerState state = ServerState::Unknown;  ///< Online: listed
    std::string server;
    u64 hub_id = 0;
    u32 links = 0;       ///< players connected through the server now
    u32 links_udp = 0;   ///< of them, those whose datagrams go by UDP
    u64 revision = 0;    ///< changes with anything above
};
HostStatus GetHostStatus();

// ---- joining -------------------------------------------------------------------------------------------------------

struct JoinRequest {
    std::string server;
    std::string key;
    u64 program_id = 0;
    u64 hub_id = 0;
    std::string hub_name;            ///< for the status (as listed)
    rooms::JoinRequest room;         ///< nickname, password, ... (host, port and mtu are the tunnel's)
    rooms::Client* client = nullptr; ///< the room client that joins (null: rooms::Default())
};

enum class JoinState : u8 {
    Idle,
    Connecting,  ///< to the server
    Waiting,     ///< for the hub's host to take the player
    Tunnel,      ///< the tunnel is up: the room client joins (or is in) the hub's room
};

enum class JoinError : u8 {
    None,
    ServerNotFound,
    ServerUnreachable,
    ServerBusy,
    ServerVersion,
    HubGone,        ///< the hub is no longer listed
    NoAnswer,       ///< the host did not take the player in time
    OtherGame,      ///< the host plays another region or a version of this port that can't link
    HubFull,
    Lost,           ///< the link to the host broke
};
const char* JoinErrorName(JoinError error);

struct JoinStatus {
    JoinState state = JoinState::Idle;
    JoinError error = JoinError::None;  ///< the last failure (cleared by the next Join)
    std::string server, hub_name;
    u64 hub_id = 0;
    u16 local_port = 0;  ///< the tunnel's end the room client joins (Tunnel)
    bool udp = false;    ///< datagrams go by UDP (else through the TCP link)
    u64 revision = 0;
};

/// Starts joining a listed hub (a tunnel open before is closed). The room client joins the hub's room through the
/// tunnel once it is up; leaving that room (or joining another) closes the tunnel. False only for a request that can't
/// work.
bool Join(const JoinRequest& request, std::string* error = nullptr);
/// Closes the tunnel (the room client is left too when it is in the hub's room).
void Leave();
JoinStatus GetJoinStatus();
/// True when `status` (a room client's) is in, or joining, the room of the open tunnel.
bool InHub(const rooms::Status& status);

namespace detail {
/// This module's threads still running (each ends within a moment of StopHosting or Leave): for tests.
int RunningThreads();
}  // namespace detail

}  // namespace mh4n::hubs
