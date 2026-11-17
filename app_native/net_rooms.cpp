// The app's side of the network rooms (net_rooms.h).
#include "net_rooms.h"

#include "app_version.h"
#include "chat_app.h"
#include "net_lan.h"

#include <SDL3/SDL.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace rooms = mh4n::rooms;
namespace hubs = mh4n::hubs;

namespace {

constexpr size_t kMaxLines = 60;
bool g_room_loopback = false;  // the room this phone hosts is a hub's on the loopback only (net_hub_create)

std::vector<NetLine> g_lines;
unsigned g_serial = 0;
rooms::State g_state = rooms::State::Idle;  // the state the lines last told of

void add(NetLine line) {
    // (a chat line, someone's or this phone's: the port's chat too, as its Room - chat_app.cpp)
    if (line.kind == NetLine::Chat || line.kind == NetLine::Mine) chat_app_room_line(line.nick, line.text, line.kind == NetLine::Mine);
    line.time = SDL_GetTicks() / 1000.0;
    line.serial = ++g_serial;
    g_lines.push_back(std::move(line));
    if (g_lines.size() > kMaxLines) g_lines.erase(g_lines.begin(), g_lines.begin() + (g_lines.size() - kMaxLines));
}

bool in_room(rooms::State state) { return state == rooms::State::Joined || state == rooms::State::Moderator; }

// The lobby's fetch: on the heap and never freed, as its thread may outlive everything else at the app's end
struct LobbyState {
    std::mutex mutex;
    NetLobby lobby;
};
LobbyState& lobby_state() {
    static LobbyState* state = new LobbyState;
    return *state;
}
// The hub server's list: the same
struct HubsState {
    std::mutex mutex;
    NetHubs hubs;
};
HubsState& hubs_state() {
    static HubsState* state = new HubsState;
    return *state;
}

// A hub joined that failed, or whose link broke: a line (its state goes back to Idle with the reason)
void poll_hub() {
    static std::uint64_t seen = 0;
    const hubs::JoinStatus st = hubs::GetJoinStatus();
    if (st.revision == seen) return;
    seen = st.revision;
    if (st.state != hubs::JoinState::Idle || st.error == hubs::JoinError::None) return;
    NetLine line;
    line.kind = NetLine::HubFailed;
    line.hub_error = st.error;
    line.text = st.hub_name;
    add(line);
}

}  // namespace

void net_poll() {
    net_lan_poll();  // (the answers to searches on this Wi-Fi while this phone hosts a room)
    poll_hub();
    std::vector<rooms::Event> events = rooms::TakeEvents();
    if (events.empty()) return;
    const rooms::Status status = rooms::GetStatus();  // (this phone's nickname, the room's name)
    // (the room lost or refused: its state comes before the failure, in the same batch; "you left" only without one)
    bool failed = false, left = false;
    for (const rooms::Event& e : events) {
        NetLine line;
        switch (e.kind) {
        case rooms::Event::Kind::StateChanged:
            if (in_room(e.state) && !in_room(g_state)) {
                line.kind = NetLine::YouJoined;
                line.text = status.room.name;
                add(line);
            } else if (e.state == rooms::State::Idle && in_room(g_state)) {
                left = true;
            }
            g_state = e.state;
            break;
        case rooms::Event::Kind::Failed:
            failed = true;
            line.kind = NetLine::Failed;
            line.error = e.error;
            add(line);
            break;
        case rooms::Event::Kind::Chat:
            line.kind = NetLine::Chat;
            line.nick = e.nickname;
            line.text = e.message;
            add(line);
            break;
        case rooms::Event::Kind::MemberJoined:
        case rooms::Event::Kind::MemberLeft:
        case rooms::Event::Kind::MemberKicked:
        case rooms::Event::Kind::MemberBanned:
            if (e.nickname == status.nickname && e.kind == rooms::Event::Kind::MemberJoined) break;  // (this phone)
            line.kind = e.kind == rooms::Event::Kind::MemberJoined ? NetLine::Joined
                        : e.kind == rooms::Event::Kind::MemberLeft ? NetLine::Left
                        : e.kind == rooms::Event::Kind::MemberKicked ? NetLine::Kicked
                                                                     : NetLine::Banned;
            line.nick = e.nickname;
            add(line);
            break;
        default:
            break;
        }
    }
    if (left && !failed) {
        NetLine line;
        line.kind = NetLine::YouLeft;
        add(line);
    }
}

const std::vector<NetLine>& net_lines() {
    return g_lines;
}

bool net_parse_port(const std::string& text, std::uint16_t* port) {
    if (text.empty() || text.size() > 5 || !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return false;
    const long n = std::strtol(text.c_str(), nullptr, 10);
    if (n < 1 || n > 65535) return false;
    *port = static_cast<std::uint16_t>(n);
    return true;
}

bool net_parse_address(const std::string& typed, std::string* host, std::uint16_t* port) {
    std::string text = typed;
    while (!text.empty() && text.back() == ' ') text.pop_back();
    while (!text.empty() && text.front() == ' ') text.erase(0, 1);
    *port = rooms::kDefaultPort;
    const size_t colon = text.rfind(':');
    if (colon != std::string::npos) {
        if (!net_parse_port(text.substr(colon + 1), port)) return false;
        text.resize(colon);
    }
    if (text.empty() || text.size() > 253) return false;
    for (char c : text)  // (an IPv4 address or a host name: rooms are IPv4 only)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-'))
            return false;
    *host = text;
    return true;
}

NetProblem net_join(const std::string& address, const std::string& nickname, const std::string& password) {
    if (!rooms::ValidNickname(nickname)) return NetProblem::Nickname;
    rooms::JoinRequest request;
    if (!net_parse_address(address, &request.host, &request.port)) return NetProblem::Address;
    const rooms::Status status = rooms::GetStatus();
    bool own = request.host == "127.0.0.1" || request.host == "localhost";  // (this phone's room, at any of its addresses)
    for (const std::string& mine : net_local_addresses()) own = own || request.host == mine;
    own = own && request.port == status.hosted_port;
    hubs::Leave();  // (a hub joined: one place at a time)
    if (status.hosting && !own) {  // (one room at a time)
        rooms::Leave();
        rooms::CloseRoom();
        hubs::StopHosting();
    }
    request.nickname = nickname;
    request.password = password;
    // (no console ID: the running game's, which the runtime publishes at boot; a join before that waits for it)
    std::string error;
    if (!rooms::Join(request, &error)) {
        std::fprintf(stderr, "[Info] App: room: %s\n", error.c_str());
        return NetProblem::Address;
    }
    std::fprintf(stderr, "[Info] App: room: joining %s:%u as %s\n", request.host.c_str(), static_cast<unsigned>(request.port),
                 nickname.c_str());
    return NetProblem::None;
}

NetProblem net_host(const std::string& port_text, int slots, const std::string& nickname, const std::string& password) {
    if (!rooms::ValidNickname(nickname)) return NetProblem::Nickname;
    std::uint16_t port = 0;
    if (!net_parse_port(port_text, &port)) return NetProblem::Port;
    hubs::Leave();
    hubs::StopHosting();
    rooms::Leave();
    rooms::CloseRoom();
    rooms::HostRequest request;
    request.port = port;
    request.name = "MH4U - " + nickname;
    request.password = password;
    request.slots = static_cast<std::uint32_t>(std::clamp(slots, 2, 16));
    request.host_username = nickname;  // (this phone joins as the room's moderator)
    std::string error;
    if (!rooms::Host(request, &error)) {
        std::fprintf(stderr, "[Info] App: room: could not host on UDP port %u: %s\n", static_cast<unsigned>(port), error.c_str());
        return NetProblem::PortBusy;
    }
    g_room_loopback = false;
    std::fprintf(stderr, "[Info] App: room: hosting on UDP port %u, %d hunters\n", static_cast<unsigned>(port), slots);
    return net_join("127.0.0.1:" + std::to_string(port), nickname, password);
}

std::vector<std::string> net_local_addresses() {
    static std::vector<std::string> cached;
    static Uint64 when = 0;
    const Uint64 now = SDL_GetTicks();
    if (when && now - when < 3000) return cached;  // (asked each frame the tab shows)
    when = now;
    cached.clear();
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return cached;
    std::vector<std::pair<int, std::string>> found;  // (Wi-Fi first, then the rest: mobile data, a hotspot)
    for (ifaddrs* a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || (a->ifa_flags & IFF_LOOPBACK) || !(a->ifa_flags & IFF_UP))
            continue;
        char text[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr, text, sizeof text);
        const std::string name = a->ifa_name ? a->ifa_name : "";
        found.emplace_back(name.rfind("wlan", 0) == 0 ? 0 : name.rfind("ap", 0) == 0 || name.rfind("swlan", 0) == 0 ? 1 : 2, text);
    }
    freeifaddrs(list);
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& f : found) cached.push_back(f.second);
    return cached;
}

void net_leave() {
    hubs::Leave();  // (in a hub: its room too, whose goodbye goes through the tunnel before that closes)
    rooms::Leave();
}

void net_close_room() {
    hubs::StopHosting();
    rooms::Leave();
    rooms::CloseRoom();
    std::fprintf(stderr, "[Info] App: room: closed\n");
}

bool net_say(const std::string& text) {
    if (text.empty() || !rooms::SendChat(text)) return false;
    NetLine line;  // (the room sends a chat line to everyone but its sender)
    line.kind = NetLine::Mine;
    line.nick = rooms::GetStatus().nickname;
    line.text = text.substr(0, rooms::kMaxMessageSize);
    add(line);
    return true;
}

void net_lobby_fetch(const std::string& url) {
    LobbyState& state = lobby_state();
    {
        std::lock_guard lock(state.mutex);
        if (state.lobby.busy) return;
        state.lobby.busy = true;
        state.lobby.done = false;
        state.lobby.url = url;
    }
    std::thread([url, &state] {
        rooms::LobbyList list = rooms::FetchLobby(url, rooms::kMh4uUsa, 6000);
        std::lock_guard lock(state.mutex);
        state.lobby.list = std::move(list);
        state.lobby.busy = false;
        state.lobby.done = true;
    }).detach();
}

NetLobby net_lobby() {
    LobbyState& state = lobby_state();
    std::lock_guard lock(state.mutex);
    return state.lobby;
}

// ---- hubs
void net_hubs_fetch(const std::string& server) {
    HubsState& state = hubs_state();
    {
        std::lock_guard lock(state.mutex);
        if (state.hubs.busy) return;
        state.hubs.busy = true;
        state.hubs.done = false;
        state.hubs.server = server;
    }
    const hubs::Region region = hubs::RegionOfProgram(hubs::GameProgram());
    std::thread([server, region, &state] {
        hubs::ListResult list = hubs::List(server, region, {}, 4000);
        std::fprintf(stderr, "[Info] App: hub server %s: %s, %d ms, %d hub(s) for %s\n", server.c_str(),
                     hubs::ServerStateName(list.state), list.ms, (int)list.hubs.size(), hubs::RegionName(region));
        std::lock_guard lock(state.mutex);
        state.hubs.list = std::move(list);
        state.hubs.busy = false;
        state.hubs.done = true;
    }).detach();
}

NetHubs net_hubs() {
    HubsState& state = hubs_state();
    std::lock_guard lock(state.mutex);
    return state.hubs;
}

NetProblem net_hub_join(const std::string& server, const hubs::Hub& hub, const std::string& nickname,
                        const std::string& password) {
    if (!rooms::ValidNickname(nickname)) return NetProblem::Nickname;
    if (hubs::RegionOfProgram(hubs::GameProgram()) == hubs::Region::None) return NetProblem::NoGame;
    std::string host;
    std::uint16_t port = 0;
    if (!hubs::ParseServer(server, &host, &port)) return NetProblem::NoServer;
    if (rooms::GetStatus().hosting) {  // (one room at a time)
        hubs::StopHosting();
        rooms::Leave();
        rooms::CloseRoom();
    }
    hubs::JoinRequest request;
    request.server = server;
    request.program_id = hubs::GameProgram();
    request.hub_id = hub.id;
    request.hub_name = hub.name;
    request.room.nickname = nickname;
    request.room.password = password;
    std::string error;
    if (!hubs::Join(request, &error)) {
        std::fprintf(stderr, "[Info] App: hub: %s\n", error.c_str());
        return NetProblem::Address;
    }
    std::fprintf(stderr, "[Info] App: hub: joining \"%s\" through %s as %s\n", hub.name.c_str(), server.c_str(), nickname.c_str());
    return NetProblem::None;
}

NetProblem net_hub_create(const std::string& server, const std::string& name, const std::string& tags, int slots,
                          const std::string& nickname, const std::string& password, const std::string& port_text,
                          bool crossplay) {
    if (!rooms::ValidNickname(nickname)) return NetProblem::Nickname;
    const std::uint64_t program = hubs::GameProgram();
    if (hubs::RegionOfProgram(program) == hubs::Region::None) return NetProblem::NoGame;
    std::string host;
    std::uint16_t server_port = 0, port = 0;
    if (!hubs::ParseServer(server, &host, &server_port)) return NetProblem::NoServer;
    if (!net_parse_port(port_text, &port)) return NetProblem::Port;
    hubs::Leave();
    hubs::StopHosting();
    rooms::Leave();
    rooms::CloseRoom();
    rooms::HostRequest request;
    request.name = name.empty() ? "MH4U - " + nickname : name;
    request.description = tags;
    request.password = password;
    request.slots = static_cast<std::uint32_t>(std::clamp(slots, 2, 8));
    request.host_username = nickname;  // (this phone joins as the room's moderator)
    request.preferred_game_id = program;
    // (a hub alone: the room listens on the loopback only, the players come through the server; for crossplay it
    // also takes Azahar and Citra players at this phone's own address)
    request.bind_address = crossplay ? "" : "127.0.0.1";
    std::string error;
    bool open = false;
    for (int k = 0; k < (crossplay ? 1 : 16) && !open; k++) {  // (on the loopback any free port does)
        request.port = static_cast<std::uint16_t>(port + k);
        open = request.port != 0 && rooms::Host(request, &error);
    }
    if (!open) {
        std::fprintf(stderr, "[Info] App: hub: could not open its room on UDP port %u: %s\n", static_cast<unsigned>(port), error.c_str());
        return NetProblem::PortBusy;
    }
    g_room_loopback = !crossplay;
    hubs::HostRequest hub;
    hub.server = server;
    hub.program_id = program;
    hub.room_port = request.port;
    hub.name = request.name;
    hub.description = tags;
    hub.password = !password.empty();
    hub.crossplay = crossplay;
    hub.app_version = MH4U_APP_VERSION;
    if (!hubs::Host(hub, &error)) {
        std::fprintf(stderr, "[Info] App: hub: %s\n", error.c_str());
        rooms::CloseRoom();
        return NetProblem::NoServer;
    }
    std::fprintf(stderr, "[Info] App: hub: \"%s\" on %s (room on UDP %u%s), %d hunters\n", request.name.c_str(),
                 server.c_str(), static_cast<unsigned>(request.port), crossplay ? ", also at this phone's address" : ", loopback",
                 (int)request.slots);
    return net_join("127.0.0.1:" + std::to_string(request.port), nickname, password);
}

void net_hub_close() {
    net_close_room();
}

bool net_in_hub() {
    return hubs::InHub(rooms::GetStatus()) || hubs::GetJoinStatus().state != hubs::JoinState::Idle;
}

bool net_hosting_hub() {
    return hubs::GetHostStatus().active && rooms::GetStatus().hosting;
}

bool net_room_on_lan() {
    return rooms::IsHosting() && !g_room_loopback;
}
