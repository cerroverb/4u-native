// MH4U for PC: rooms on this network (net_lan.h), the Android app's net_lan.cpp with Windows' sockets: the same packets
// on the same UDP port, so a PC finds a phone's room and a phone finds the PC's (and two PCs each other's).
//
// The packets (little-endian), UDP port kLanPort:
//   search  "MH4R" 1 1 <u16 0> <u32 nonce>
//   answer  "MH4R" 1 2 <u16 room port> <u32 nonce> <u32 room id> <u8 hunters> <u8 slots> <u8 flags: 1 password>
//           <u8 name length> <name> <u8 nicknames> (<u8 length> <nickname>)...
// A room this PC hosts answers searches while it takes players at the PC's own address (not a hub's on the loopback).
// The search asks 255.255.255.255, each network's own broadcast address (every adapter that is up: Wi-Fi, Ethernet,
// a VPN's) and this PC itself, every 0.5 s, listening 1.5 s in all. The first room hosted makes Windows Firewall ask
// whether MH4U may use the network: allowed, others find the room and join it; the search needs no permission (Windows
// lets the answers to a broadcast in).
#include "net_lan.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "mh4n/rooms.h"
#include "net_rooms.h"

namespace rooms = mh4n::rooms;

namespace {

constexpr char kMagic[4] = {'M', 'H', '4', 'R'};
constexpr std::uint8_t kVersion = 1, kSearch = 1, kAnswer = 2;
constexpr int kListenMs = 1500, kAskMs = 500;

void lan_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void lan_log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::fputs("[Info] App: ", stderr);
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
    va_end(ap);
}

bool winsock() {  // (once: the rooms' library starts it too, for itself)
    static const bool ok = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ok;
}

std::uint32_t random32() {
    static std::mt19937 gen{std::random_device{}()};
    return gen();
}

void put16(std::vector<std::uint8_t>& b, std::uint16_t v) {
    b.push_back(v & 0xff);
    b.push_back(v >> 8);
}
void put32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    for (int i = 0; i < 4; i++) b.push_back((v >> (8 * i)) & 0xff);
}
std::uint16_t get16(const std::uint8_t* p) { return p[0] | p[1] << 8; }
std::uint32_t get32(const std::uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (std::uint32_t)p[3] << 24; }
void put_text(std::vector<std::uint8_t>& b, const std::string& text, size_t most) {
    const size_t n = std::min(text.size(), most);
    b.push_back(static_cast<std::uint8_t>(n));
    b.insert(b.end(), text.begin(), text.begin() + n);
}

// Waits up to `ms` for something to read on `s`: true when there is
bool readable(SOCKET s, int ms) {
    WSAPOLLFD p{};
    p.fd = s;
    p.events = POLLRDNORM;
    return WSAPoll(&p, 1, ms) > 0 && (p.revents & (POLLRDNORM | POLLERR | POLLHUP)) != 0;
}

// ---- the answers, while this PC hosts a room

struct Responder {
    std::thread thread;
    std::atomic<bool> stop{false};
    SOCKET socket = INVALID_SOCKET;
    std::uint32_t id = 0;
    std::atomic<bool> password{false};
};
Responder* g_responder = nullptr;
bool g_hosted_password = false;
bool g_answer_failed = false;  // (the port could not be had: not tried again until this room closes)

std::vector<std::uint8_t> answer(std::uint32_t nonce, std::uint32_t id, bool password) {
    const rooms::RoomInformation info = rooms::DefaultServer().Information();
    const std::vector<rooms::Member> members = rooms::DefaultServer().Members();
    std::vector<std::uint8_t> b(kMagic, kMagic + 4);
    b.push_back(kVersion);
    b.push_back(kAnswer);
    put16(b, info.port);
    put32(b, nonce);
    put32(b, id);
    b.push_back(static_cast<std::uint8_t>(std::min<size_t>(members.size(), 255)));
    b.push_back(static_cast<std::uint8_t>(std::min<std::uint32_t>(info.member_slots, 255)));
    b.push_back(password ? 1 : 0);
    put_text(b, info.name, 64);
    const size_t listed = std::min<size_t>(members.size(), 16);
    b.push_back(static_cast<std::uint8_t>(listed));
    for (size_t i = 0; i < listed; i++) put_text(b, members[i].nickname, 32);
    return b;
}

void respond(Responder* r) {
    std::uint8_t buf[512];
    while (!r->stop.load()) {
        if (!readable(r->socket, 200)) continue;
        sockaddr_in from{};
        int len = sizeof from;
        const int n = recvfrom(r->socket, reinterpret_cast<char*>(buf), sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &len);
        if (n < 12 || std::memcmp(buf, kMagic, 4) != 0 || buf[4] != kVersion || buf[5] != kSearch) continue;
        if (!rooms::IsHosting()) continue;
        const std::vector<std::uint8_t> a = answer(get32(buf + 8), r->id, r->password.load());
        sendto(r->socket, reinterpret_cast<const char*>(a.data()), static_cast<int>(a.size()), 0,
               reinterpret_cast<sockaddr*>(&from), len);
    }
}

void start_answering() {
    if (g_responder || g_answer_failed) return;
    g_answer_failed = true;
    const rooms::RoomInformation info = rooms::DefaultServer().Information();
    if (info.port == kLanPort) {
        lan_log("LAN: the room is on UDP port %d, the searches' own: it answers no search", (int)kLanPort);
        return;
    }
    if (!winsock()) return;
    const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return;
    sockaddr_in at{};
    at.sin_family = AF_INET;
    at.sin_port = htons(kLanPort);
    at.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, reinterpret_cast<sockaddr*>(&at), sizeof at) != 0) {
        lan_log("LAN: UDP port %d is in use: the room answers no search (error %d)", (int)kLanPort, WSAGetLastError());
        closesocket(s);
        return;
    }
    auto* r = new Responder;
    r->socket = s;
    r->id = random32();
    r->password = g_hosted_password;
    r->thread = std::thread(respond, r);
    g_responder = r;
    g_answer_failed = false;
    lan_log("LAN: the room answers searches on UDP port %d", (int)kLanPort);
}

void stop_answering() {
    if (!g_responder) return;
    g_responder->stop = true;
    g_responder->thread.join();
    closesocket(g_responder->socket);
    delete g_responder;
    g_responder = nullptr;
    lan_log("LAN: no more answers (the room closed)");
}

// ---- the search, on a thread of its own

struct SearchState {
    std::mutex mutex;
    LanSearch search;
};
SearchState& search_state() {  // (on the heap and never freed: its thread may outlive everything else at the app's end)
    static SearchState* state = new SearchState;
    return *state;
}

// Each network's own broadcast address (every adapter that is up, but the loopback), and this PC's addresses
void networks(std::vector<in_addr>* broadcast, std::vector<in_addr>* mine) {
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buffer(size);
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    ULONG got = GetAdaptersAddresses(AF_INET, flags, nullptr, adapters, &size);
    if (got == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(size);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        got = GetAdaptersAddresses(AF_INET, flags, nullptr, adapters, &size);
    }
    if (got != NO_ERROR) return;
    for (IP_ADAPTER_ADDRESSES* a = adapters; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (u->Address.lpSockaddr == nullptr || u->Address.lpSockaddr->sa_family != AF_INET) continue;
            const in_addr addr = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr;
            mine->push_back(addr);
            const unsigned prefix = u->OnLinkPrefixLength;
            if (prefix == 0 || prefix >= 31) continue;  // (no network around it to broadcast to)
            const std::uint32_t mask = prefix >= 32 ? 0xffffffffu : ~(0xffffffffu >> prefix);
            in_addr b{};
            b.s_addr = htonl((ntohl(addr.s_addr) & mask) | ~mask);
            broadcast->push_back(b);
        }
    }
}

bool parse(const std::uint8_t* b, size_t n, std::uint32_t nonce, LanRoom* room, std::uint32_t* id) {
    if (n < 20 || std::memcmp(b, kMagic, 4) != 0 || b[4] != kVersion || b[5] != kAnswer || get32(b + 8) != nonce) return false;
    room->port = get16(b + 6);
    *id = get32(b + 12);
    room->members = b[16];
    room->slots = b[17];
    room->password = b[18] & 1;
    size_t at = 19;
    auto text = [&](std::string* out) {
        if (at >= n || at + 1 + b[at] > n) return false;
        out->assign(reinterpret_cast<const char*>(b + at + 1), b[at]);
        at += 1 + b[at];
        return true;
    };
    if (!text(&room->name) || room->port == 0) return false;
    if (at < n) {
        const int count = b[at++];
        for (int i = 0; i < count; i++) {
            std::string nick;
            if (!text(&nick)) break;
            room->hunters.push_back(nick);
        }
    }
    return true;
}

void search_thread() {
    std::vector<LanRoom> found;
    std::vector<std::uint32_t> ids;
    std::vector<in_addr> broadcast, mine;
    networks(&broadcast, &mine);
    const SOCKET s = winsock() ? socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP) : INVALID_SOCKET;
    if (s != INVALID_SOCKET) {
        BOOL one = TRUE;
        setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&one), sizeof one);
        const std::uint32_t nonce = random32();
        std::vector<std::uint8_t> probe(kMagic, kMagic + 4);
        probe.push_back(kVersion);
        probe.push_back(kSearch);
        put16(probe, 0);
        put32(probe, nonce);
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = htons(kLanPort);
        auto send_to = [&](in_addr a) {
            to.sin_addr = a;
            sendto(s, reinterpret_cast<const char*>(probe.data()), static_cast<int>(probe.size()), 0,
                   reinterpret_cast<sockaddr*>(&to), sizeof to);
        };
        auto addr = [](std::uint32_t host_order) {
            in_addr a{};
            a.s_addr = htonl(host_order);
            return a;
        };
        using clock = std::chrono::steady_clock;
        const auto end = clock::now() + std::chrono::milliseconds(kListenMs);
        auto next_ask = clock::now();
        int asked = 0;
        for (auto now = clock::now(); now < end; now = clock::now()) {
            if (now >= next_ask) {  // (asked more than once: a sleeping phone's Wi-Fi hears the next one)
                next_ask = now + std::chrono::milliseconds(kAskMs);
                send_to(addr(INADDR_BROADCAST));
                for (in_addr a : broadcast) send_to(a);
                send_to(addr(INADDR_LOOPBACK));
                asked++;
            }
            const auto wait = std::min(next_ask, end) - now;
            const int ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(wait).count();
            if (!readable(s, std::max(ms, 1))) continue;
            std::uint8_t buf[1500];
            sockaddr_in from{};
            int len = sizeof from;
            const int n = recvfrom(s, reinterpret_cast<char*>(buf), sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &len);
            LanRoom room;
            std::uint32_t id = 0;
            if (n <= 0 || !parse(buf, (size_t)n, nonce, &room, &id)) continue;
            char text[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &from.sin_addr, text, sizeof text);
            room.address = text;
            room.own = from.sin_addr.s_addr == htonl(INADDR_LOOPBACK);
            for (in_addr a : mine) room.own = room.own || a.s_addr == from.sin_addr.s_addr;
            const auto seen = std::find(ids.begin(), ids.end(), id);
            if (seen != ids.end()) {  // (this PC's room, by its loopback and its network: the network's address)
                LanRoom& kept = found[seen - ids.begin()];
                if (kept.address == "127.0.0.1" && room.address != "127.0.0.1") kept.address = room.address;
                continue;
            }
            ids.push_back(id);
            found.push_back(std::move(room));
        }
        closesocket(s);
        lan_log("LAN: searched %d network(s) %d times: %d room(s)", (int)broadcast.size(), asked, (int)found.size());
    }
    std::stable_sort(found.begin(), found.end(), [](const LanRoom& a, const LanRoom& b) { return !a.own && b.own; });
    SearchState& state = search_state();
    std::lock_guard lock(state.mutex);
    state.search.rooms = std::move(found);
    state.search.no_network = broadcast.empty();
    state.search.busy = false;
    state.search.done = true;
}

}  // namespace

void net_lan_hosting(bool password) {
    g_hosted_password = password;
    if (g_responder) g_responder->password = password;
}

void net_lan_poll() {
    // (a hub's room on the loopback only can't be joined from this network: not answered)
    const bool answering = rooms::IsHosting() && net_room_on_lan();
    if (answering && !g_responder) start_answering();
    if (!answering) {
        stop_answering();
        g_answer_failed = false;
    }
}

void net_lan_search() {
    SearchState& state = search_state();
    {
        std::lock_guard lock(state.mutex);
        if (state.search.busy) return;
        state.search.busy = true;
        state.search.done = false;
    }
    std::thread(search_thread).detach();
}

LanSearch net_lan_result() {
    SearchState& state = search_state();
    std::lock_guard lock(state.mutex);
    return state.search;
}
