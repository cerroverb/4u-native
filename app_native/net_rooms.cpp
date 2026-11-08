// The app's side of the network rooms (net_rooms.h).
#include "net_rooms.h"

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

namespace {

constexpr size_t kMaxLines = 60;

std::vector<NetLine> g_lines;
unsigned g_serial = 0;
rooms::State g_state = rooms::State::Idle;  // the state the lines last told of

void add(NetLine line) {
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

}  // namespace

void net_poll() {
    net_lan_poll();  // (the answers to searches on this Wi-Fi while this phone hosts a room)
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
    if (status.hosting && !own) {  // (one room at a time)
        rooms::Leave();
        rooms::CloseRoom();
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
    rooms::Leave();
}

void net_close_room() {
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
