// Internet hubs (mh4n/hubs.h): a hosted room listed on a hub server, and the tunnels that carry its members' ENet
// datagrams through that server.
//
// The server's protocol is MHGURecomp's (its repository's server/server.go has it): control frames (u32 size of what
// follows, u8 type, payload; little endian), the first of a connection a request whose payload starts with a header
// ("MHGS", u16 server protocol 1, u16 link protocol, the first 16 bytes of a hash of the matchmaking key). LIST answers
// the hubs of that link protocol and key; HOST lists one (0x480 bytes the server checks five of) and keeps the
// connection, which gets INCOMING tickets; a JOIN and the host's ATTACH with that ticket get SPLICED (a pair id and a
// secret for UDP), and from then on the two connections are one pipe. Datagrams to the server's UDP port that start
// with "MHGR", the pair id and the sender's secret are passed (without those 16 bytes) to the other end of the pair.
//
// Over a splice, this module's tunnel (kTunnelVersion): the same framing, frames
//   kHello    player -> host  "M4HT", u16 tunnel version, u16 link protocol, u64 program id
//   kWelcome  host -> player  u32 token (it marks the player's datagrams)
//   kRefuse   host -> player  u32 reason
//   kDatagram both            one ENet datagram, when it can't go by UDP
//   kPing / kPong             u64 the sender's clock (ms), echoed: keeps the link and NAT mappings alive, and times it
//   kBye                      the end is closing
// and, through the server's UDP relay, datagrams of "M4HD", u32 token, u8 kind (kUdpData, kUdpHello, kUdpHelloAck),
// three zero bytes, then the ENet datagram for kUdpData. The player says hello every 0.5 s until the host answers,
// then every 2 s, with a byte that says whether it hears the host by UDP; the host answers each one, and sends by UDP only
// while the player says it hears it (so a network that passes UDP one way only stays on TCP). The host tells the server
// where it is (an empty relayed datagram) every second until a hello comes. Without UDP, or 6 s after the last datagram
// (three hellos missed), everything goes through the TCP link.
//
// A room datagram by UDP (1.0.2) carries a number (bytes 10-11, flag kUdpNumbered in byte 9), and on a slow link (a
// round trip of kCopyRttMs or more by the link's own ping) goes twice: again kCopyDelayMs later, flagged kUdpCopy. ENet
// sends every frame reliably and in order, so one lost datagram holds every later one back until ENet sends it again, a
// round trip and more later (a second and more through a server far away); the game, which takes a peer it has not
// heard from for about a second for gone, then drops the player. With the copy, a datagram is held back only when both
// are lost. The receiver passes the first that comes and drops the other (an end of 1.0.1, which reads neither byte,
// passes both: ENet keeps one), and counts the datagrams that never came for the link's line a minute.
#include "mh4n/hubs.h"

#include <algorithm>
#include <atomic>
#include <bitset>
#include <chrono>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <thread>

#include <enet/enet.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <poll.h>
#endif

#include "../services/cfg/sha256.h"
#include "mh4n/log.h"
#include "net_internal.h"

namespace mh4n::hubs {

namespace {

constexpr const char* kLog = "Network.Hub";

// ---- the server's protocol (server/server.go) ----
constexpr u16 kServerProto = 1;
constexpr std::size_t kHeaderSize = 24;
constexpr u8 kList = 1, kHostReq = 2, kAttach = 3, kJoinReq = 4, kUpdate = 5, kPing = 6;
constexpr u8 kHubs = 16, kHosted = 17, kIncoming = 18, kSpliced = 19, kError = 20;
constexpr u32 kErrNotFound = 1, kErrNoAnswer = 2, kErrBusy = 3, kErrVersion = 4;
constexpr char kRelayMagic[4] = {'M', 'H', 'G', 'R'};
constexpr std::size_t kRelayPrefix = 16;
constexpr std::size_t kMaxServerFrame = 1 << 20;  // a LIST answer: 4 + n * (8 + 0x480)
constexpr int kServerPingMs = 15000;

// ---- the tunnel ----
constexpr char kTunnelMagic[4] = {'M', '4', 'H', 'T'};
constexpr char kUdpMagic[4] = {'M', '4', 'H', 'D'};
constexpr u8 kHello = 1, kWelcome = 2, kRefuse = 3, kDatagram = 4, kTunnelPing = 5, kPong = 6, kBye = 7;
constexpr u8 kUdpData = 0, kUdpHello = 1, kUdpHelloAck = 2;
constexpr std::size_t kUdpHeader = 12;
constexpr u32 kRefuseVersion = 1, kRefuseOtherGame = 2, kRefuseFull = 3;
constexpr std::size_t kMaxTunnelFrame = 8192;
// The largest datagram sent through the server's UDP relay: the relay's 16 bytes, the tunnel's 12 and an ENet datagram
// of kTunnelMtu make 1228, and 28 more of IPv4 and UDP fit 1280 (IPv6's minimum MTU, below what any mobile network
// gives). A larger one goes through the TCP link instead.
constexpr std::size_t kMaxUdpPayload = 1232;
constexpr int kLinkPingMs = 5000;
constexpr int kLinkSilenceMs = 30000;
constexpr int kUdpSilenceMs = 6000;
constexpr int kMaxLinks = 15;
constexpr int kCopyRttMs = 150;
constexpr int kCopyDelayMs = 40;
constexpr u8 kUdpNumbered = 1, kUdpCopy = 2;  // a data datagram's byte 9

// ---- the listing (0x480 bytes) ----
constexpr char kListingMagic[4] = {'M', '4', 'H', 'B'};
constexpr u16 kListingVersion = 1;
constexpr std::size_t kLiRegion = 0x06, kLiFlags = 0x07, kLiProgram = 0x08, kLiPlayers = 0x10, kLiSlots = 0x11;
constexpr std::size_t kNiSsidLength = 0x26, kNiNetType = 0x4B, kNiNodeMax = 0x66, kNiNodeCount = 0x67, kNiAdvSize = 0x26A;
constexpr std::size_t kLiName = 0x80, kLiNameSize = 64;
constexpr std::size_t kLiDescription = 0xC0, kLiDescriptionSize = 128;
constexpr std::size_t kLiHost = 0x140, kLiHostSize = 24;
constexpr std::size_t kLiVersion = 0x158, kLiVersionSize = 16;
constexpr std::size_t kLiHunters = 0x168, kLiHunterSize = 24, kLiHunterCount = 8;
constexpr u8 kFlagPassword = 1, kFlagCrossplay = 2;

s64 NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void Put16(u8* p, u16 v) {
    p[0] = static_cast<u8>(v);
    p[1] = static_cast<u8>(v >> 8);
}
void Put32(u8* p, u32 v) {
    for (int i = 0; i < 4; ++i) {
        p[i] = static_cast<u8>(v >> (8 * i));
    }
}
void Put64(u8* p, u64 v) {
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<u8>(v >> (8 * i));
    }
}
u16 Get16(const u8* p) {
    return static_cast<u16>(p[0] | (p[1] << 8));
}
u32 Get32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}
u64 Get64(const u8* p) {
    return static_cast<u64>(Get32(p)) | (static_cast<u64>(Get32(p + 4)) << 32);
}

u64 Random64() {
    static std::mutex mutex;
    static std::mt19937_64 engine{[] {
        std::random_device device;
        return (static_cast<u64>(device()) << 32) ^ device() ^ static_cast<u64>(NowMs());
    }()};
    std::lock_guard lock(mutex);
    return engine();
}

// A string into a fixed field: cut at a whole UTF-8 character, the rest zero
void PutText(u8* field, std::size_t size, const std::string& text) {
    std::size_t n = std::min(text.size(), size - 1);
    while (n > 0 && n < text.size() && (static_cast<u8>(text[n]) & 0xC0) == 0x80) {
        --n;  // (text[n] continues a character: cut before that character)
    }
    std::memcpy(field, text.data(), n);
}
// A field back into a string: up to the first zero, control characters dropped
std::string GetText(const u8* field, std::size_t size) {
    std::string out;
    for (std::size_t i = 0; i < size && field[i] != 0; ++i) {
        if (field[i] >= 0x20 && field[i] != 0x7F) {
            out.push_back(static_cast<char>(field[i]));
        }
    }
    return out;
}

std::string Trimmed(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

std::string HexId(u64 id) {
    return fmt::format("{:016x}", id);
}

// The request header: magic, versions and the key's hash (all zero without a key; the server never gets the key)
std::array<u8, kHeaderSize> Header(u16 proto, const std::string& key) {
    std::array<u8, kHeaderSize> h{};
    std::memcpy(h.data(), "MHGS", 4);
    Put16(h.data() + 4, kServerProto);
    Put16(h.data() + 6, proto);
    const std::string k = Trimmed(key);
    if (!k.empty()) {
        const std::string salted = "MH4U hub key:" + k;
        const std::array<u8, 32> hash = cfg::Sha256(reinterpret_cast<const u8*>(salted.data()), salted.size());
        std::memcpy(h.data() + 8, hash.data(), 16);
    }
    return h;
}

// ---- sockets (ENet's, which are the same on Windows and POSIX) ----

class Socket {
public:
    Socket() = default;
    explicit Socket(ENetSocket socket) : s_(socket) {}
    ~Socket() { Close(); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept : s_(other.s_) { other.s_ = ENET_SOCKET_NULL; }
    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            Close();
            s_ = other.s_;
            other.s_ = ENET_SOCKET_NULL;
        }
        return *this;
    }
    void Close() {
        if (s_ != ENET_SOCKET_NULL) {
            enet_socket_destroy(s_);
            s_ = ENET_SOCKET_NULL;
        }
    }
    ENetSocket Get() const { return s_; }
    explicit operator bool() const { return s_ != ENET_SOCKET_NULL; }

private:
    ENetSocket s_ = ENET_SOCKET_NULL;
};

// Which of `sockets` are readable within `timeout_ms` (a bit each): poll, not select, as a process may have more
// descriptors than an fd_set holds
u32 WaitReadable(std::initializer_list<ENetSocket> sockets, int timeout_ms) {
#ifdef _WIN32
    WSAPOLLFD fds[4] = {};
#else
    pollfd fds[4] = {};
#endif
    unsigned n = 0;
    for (const ENetSocket s : sockets) {
        if (n < 4) {
            fds[n].fd = s;
            fds[n].events = POLLIN;
            ++n;
        }
    }
#ifdef _WIN32
    const int got = WSAPoll(fds, n, timeout_ms);
#else
    const int got = poll(fds, n, timeout_ms);
#endif
    if (got <= 0) {
        if (got < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min(timeout_ms, 20)));
        }
        return 0;
    }
    u32 ready = 0;
    for (unsigned i = 0; i < n; ++i) {
        if ((fds[i].revents & (POLLIN | POLLERR | POLLHUP)) != 0) {
            ready |= 1u << i;
        }
    }
    return ready;
}

bool Resolve(const std::string& host, u16 port, ENetAddress* out) {
    if (!rooms::detail::EnsureEnet()) {
        return false;
    }
    ENetAddress address{};
    if (enet_address_set_host(&address, host.c_str()) != 0 || address.host == ENET_HOST_ANY) {
        return false;
    }
    address.port = port;
    *out = address;
    return true;
}

// A TCP connection, made within `timeout_ms` (a null socket if not): non-blocking, without Nagle's delay
Socket Connect(const ENetAddress& address, int timeout_ms) {
    Socket socket(enet_socket_create(ENET_SOCKET_TYPE_STREAM));
    if (!socket) {
        return {};
    }
    enet_socket_set_option(socket.Get(), ENET_SOCKOPT_NONBLOCK, 1);
    enet_socket_set_option(socket.Get(), ENET_SOCKOPT_NODELAY, 1);
    if (enet_socket_connect(socket.Get(), &address) != 0) {
        return {};
    }
    enet_uint32 wait = ENET_SOCKET_WAIT_SEND;
    if (enet_socket_wait(socket.Get(), &wait, static_cast<enet_uint32>(timeout_ms)) != 0 ||
        (wait & ENET_SOCKET_WAIT_SEND) == 0) {
        return {};
    }
    int error = 0;
    if (enet_socket_get_option(socket.Get(), ENET_SOCKOPT_ERROR, &error) == 0 && error != 0) {
        return {};
    }
    return socket;
}

// A UDP socket bound to `host` (network order; ENET_HOST_ANY: every address), port 0: the system's choice
Socket Udp(enet_uint32 host, u16* port_out = nullptr) {
    Socket socket(enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM));
    if (!socket) {
        return {};
    }
    ENetAddress address{};
    address.host = host;
    address.port = 0;
    if (enet_socket_bind(socket.Get(), &address) != 0) {
        return {};
    }
    enet_socket_set_option(socket.Get(), ENET_SOCKOPT_NONBLOCK, 1);
    enet_socket_set_option(socket.Get(), ENET_SOCKOPT_RCVBUF, 256 * 1024);
    enet_socket_set_option(socket.Get(), ENET_SOCKOPT_SNDBUF, 256 * 1024);
    if (port_out != nullptr) {
        ENetAddress bound{};
        if (enet_socket_get_address(socket.Get(), &bound) != 0) {
            return {};
        }
        *port_out = bound.port;
    }
    return socket;
}

bool SendAll(ENetSocket socket, const u8* data, std::size_t size, int timeout_ms = 5000) {
    const s64 end = NowMs() + timeout_ms;
    while (size > 0) {
        ENetBuffer buffer;
        buffer.data = const_cast<u8*>(data);
        buffer.dataLength = size;
        const int n = enet_socket_send(socket, nullptr, &buffer, 1);
        if (n < 0) {
            return false;
        }
        if (n == 0) {
            const s64 left = end - NowMs();
            enet_uint32 wait = ENET_SOCKET_WAIT_SEND;
            if (left <= 0 || enet_socket_wait(socket, &wait, static_cast<enet_uint32>(left)) != 0 || wait == 0) {
                return false;
            }
            continue;
        }
        data += n;
        size -= static_cast<std::size_t>(n);
    }
    return true;
}

bool SendFrame(ENetSocket socket, u8 type, const u8* a, std::size_t a_size, const u8* b = nullptr, std::size_t b_size = 0) {
    std::vector<u8> frame(5 + a_size + b_size);
    Put32(frame.data(), static_cast<u32>(1 + a_size + b_size));
    frame[4] = type;
    if (a_size > 0) {
        std::memcpy(frame.data() + 5, a, a_size);
    }
    if (b_size > 0) {
        std::memcpy(frame.data() + 5 + a_size, b, b_size);
    }
    return SendAll(socket, frame.data(), frame.size());
}

void SendDatagram(ENetSocket socket, const ENetAddress& to, const u8* data, std::size_t size) {
    ENetBuffer buffer;
    buffer.data = const_cast<u8*>(data);
    buffer.dataLength = size;
    enet_socket_send(socket, &to, &buffer, 1);
}

// The frames that come in on a connection, read as they arrive
class FrameReader {
public:
    explicit FrameReader(std::size_t max) : max_(max) {}
    // Reads what is there. False: the connection closed or failed (or sent a frame too large).
    bool Fill(ENetSocket socket) {
        u8 chunk[16384];
        ENetBuffer buffer;
        buffer.data = chunk;
        buffer.dataLength = sizeof(chunk);
        const int n = enet_socket_receive(socket, nullptr, &buffer, 1);
        if (n <= 0) {
            return false;  // (called when readable: 0 is the end of the stream)
        }
        data_.insert(data_.end(), chunk, chunk + n);
        return true;
    }
    // The next whole frame, if there is one. Sets `bad` for a frame of a size out of bounds.
    bool Next(u8* type, std::vector<u8>* payload, bool* bad) {
        if (data_.size() < 5) {
            return false;
        }
        const u32 size = Get32(data_.data());
        if (size < 1 || size > max_ + 1) {
            *bad = true;
            return false;
        }
        if (data_.size() < 4 + static_cast<std::size_t>(size)) {
            return false;
        }
        *type = data_[4];
        payload->assign(data_.begin() + 5, data_.begin() + 4 + size);
        data_.erase(data_.begin(), data_.begin() + 4 + size);
        return true;
    }

private:
    std::size_t max_;
    std::vector<u8> data_;
};

// One frame, waited for up to `timeout_ms` (false on a timeout, a closed connection or a bad frame)
bool ReadFrame(ENetSocket socket, FrameReader& reader, int timeout_ms, u8* type, std::vector<u8>* payload,
               const std::atomic<bool>* stop = nullptr) {
    const s64 end = NowMs() + timeout_ms;
    for (;;) {
        bool bad = false;
        if (reader.Next(type, payload, &bad)) {
            return true;
        }
        if (bad || (stop != nullptr && stop->load())) {
            return false;
        }
        const s64 left = end - NowMs();
        if (left <= 0) {
            return false;
        }
        if (WaitReadable({socket}, static_cast<int>(std::min<s64>(left, 100))) != 0 && !reader.Fill(socket)) {
            return false;
        }
    }
}

ServerState StateOfError(u32 code) {
    return code == kErrVersion ? ServerState::Version : code == kErrBusy ? ServerState::Busy : ServerState::Refused;
}

const char* ServerErrorText(u32 code) {
    switch (code) {
    case kErrNotFound: return "the hub is gone";
    case kErrNoAnswer: return "the host did not answer";
    case kErrBusy: return "the server is busy (too many hubs or requests from this address)";
    case kErrVersion: return "the server speaks another protocol version";
    default: return "refused";
    }
}

// ---- what a link carried, for the log's line a minute ----

struct LinkStats {
    enum { UdpOut, UdpIn, TcpOut, TcpIn };
    std::atomic<u32> n[4]{};
    std::atomic<u32> max[4]{};
    std::atomic<s32> rtt{-1};
    // numbered datagrams received (1.0.2): numbers skipped and not filled later (lost both times, as far as the minute
    // tells), datagrams that came only as the copy (the first was lost), and copies sent
    std::atomic<s32> missing{0};
    std::atomic<u32> saved{0}, copies{0};
    s64 since = NowMs();
    void Count(int k, std::size_t size) {
        n[k].fetch_add(1, std::memory_order_relaxed);
        const u32 v = static_cast<u32>(size);
        u32 old = max[k].load(std::memory_order_relaxed);
        while (v > old && !max[k].compare_exchange_weak(old, v, std::memory_order_relaxed)) {
        }
    }
    // (from the link's own loop: a line once a minute when it carried anything)
    void Log(const std::string& who) {
        const s64 now = NowMs();
        if (now - since < 60000) {
            return;
        }
        since = now;
        u32 c[4], m[4];
        for (int k = 0; k < 4; ++k) {
            c[k] = n[k].exchange(0, std::memory_order_relaxed);
            m[k] = max[k].exchange(0, std::memory_order_relaxed);
        }
        const s32 miss = std::max<s32>(missing.exchange(0, std::memory_order_relaxed), 0);
        const u32 save = saved.exchange(0, std::memory_order_relaxed);
        const u32 copy = copies.exchange(0, std::memory_order_relaxed);
        if ((c[0] | c[1] | c[2] | c[3]) == 0) {
            return;
        }
        MH4N_LOG(Info, kLog,
                 "{}, room datagrams the last minute: by UDP {} sent (largest {} bytes), {} received (largest {}); by "
                 "TCP {} sent (largest {}), {} received (largest {}); round trip {} ms; by UDP {} copies sent, {} "
                 "received only as the copy (the first was lost), {} lost both times",
                 who, c[UdpOut], m[UdpOut], c[UdpIn], m[UdpIn], c[TcpOut], m[TcpOut], c[TcpIn], m[TcpIn], rtt.load(),
                 copy, save, miss);
    }
};

// Testing: MH4N_HUB_UDP=0 sends nothing by UDP; MH4N_HUB_LOSS=<percent> drops that share of the datagrams received by
// UDP (a lossy network: ENet sends them again)
bool UdpAllowed() {
    static const bool allowed = [] {
        const char* e = std::getenv("MH4N_HUB_UDP");
        const bool on = e == nullptr || std::strcmp(e, "0") != 0;
        if (!on) {
            MH4N_LOG(Info, kLog, "MH4N_HUB_UDP=0: room datagrams go through the TCP link only");
        }
        return on;
    }();
    return allowed;
}
bool DropForTest() {
    static const int percent = [] {
        const char* e = std::getenv("MH4N_HUB_LOSS");
        const int p = e != nullptr ? std::clamp(std::atoi(e), 0, 100) : 0;
        if (p > 0) {
            MH4N_LOG(Info, kLog, "MH4N_HUB_LOSS: {}% of the datagrams received by UDP are dropped", p);
        }
        return p;
    }();
    return percent > 0 && static_cast<int>(Random64() % 100) < percent;
}

// The relay's prefix and the tunnel's header, then `data`
// (`flag`: a hello's "I hear you by UDP")
std::size_t BuildUdp(u8* out, u32 pair, u64 secret, u32 token, u8 kind, const u8* data, std::size_t size, u8 flag = 0,
                     u16 number = 0) {
    std::memcpy(out, kRelayMagic, 4);
    Put32(out + 4, pair);
    Put64(out + 8, secret);
    u8* h = out + kRelayPrefix;
    std::memcpy(h, kUdpMagic, 4);
    Put32(h + 4, token);
    h[8] = kind;
    h[9] = flag;
    Put16(h + 10, number);
    if (size > 0) {
        std::memcpy(h + kUdpHeader, data, size);
    }
    return kRelayPrefix + kUdpHeader + size;
}

// Testing: MH4N_HUB_COPY=0 sends no copies (MH4N_HUB_COPY=1: on every link, slow or not)
int CopyMode() {
    static const int mode = [] {
        const char* e = std::getenv("MH4N_HUB_COPY");
        const int m = e == nullptr ? 2 : std::strcmp(e, "0") == 0 ? 0 : std::strcmp(e, "1") == 0 ? 1 : 2;
        if (m != 2) {
            MH4N_LOG(Info, kLog, "MH4N_HUB_COPY={}: {}", m,
                     m == 0 ? "no copies of the room datagrams" : "every room datagram by UDP goes twice");
        }
        return m;
    }();
    return mode;
}

// A link's room datagrams by UDP, numbered, each sent again kCopyDelayMs later while the link is slow
class UdpSender {
public:
    // `built`: BuildUdp's kUdpData datagram (its flag and number are set here)
    void Send(ENetSocket socket, const ENetAddress& to, u8* built, std::size_t total, s32 rtt, LinkStats& stats) {
        u8* h = built + kRelayPrefix;
        h[9] = kUdpNumbered;
        Put16(h + 10, next_++);
        SendDatagram(socket, to, built, total);
        const int mode = CopyMode();
        if (mode == 1 || (mode == 2 && rtt >= kCopyRttMs)) {
            h[9] = kUdpNumbered | kUdpCopy;
            copies_.push_back({NowMs() + kCopyDelayMs, std::vector<u8>(built, built + total)});
            stats.copies.fetch_add(1, std::memory_order_relaxed);
        }
    }
    // the copies that are due
    void Flush(ENetSocket socket, const ENetAddress& to, s64 now) {
        while (!copies_.empty() && copies_.front().due <= now) {
            SendDatagram(socket, to, copies_.front().bytes.data(), copies_.front().bytes.size());
            copies_.pop_front();
        }
    }
    // how long the link's loop may wait (until the next copy)
    int WaitMs(int most, s64 now) const {
        return copies_.empty() ? most : static_cast<int>(std::clamp<s64>(copies_.front().due - now, 0, most));
    }

private:
    struct Copy {
        s64 due;
        std::vector<u8> bytes;
    };
    u16 next_ = 0;
    std::deque<Copy> copies_;
};

// The numbers of a link's datagrams received: false for the second of a pair (or one seen already)
class UdpReceiver {
public:
    bool Accept(const u8* h, LinkStats& stats) {
        if ((h[9] & kUdpNumbered) == 0) {
            return true;  // (an end of 1.0.1: no numbers)
        }
        const u16 number = Get16(h + 10);
        if (!any_) {
            any_ = true;
            high_ = number;
            seen_.reset();
            seen_.set(number % kWindow);
            return true;
        }
        const u16 ahead = static_cast<u16>(number - high_);
        if (ahead != 0 && ahead < 0x8000) {
            if (ahead >= kWindow) {
                seen_.reset();
            } else {
                for (u16 k = 1; k < ahead; ++k) {
                    seen_.reset(static_cast<u16>(high_ + k) % kWindow);
                }
            }
            stats.missing.fetch_add(static_cast<s32>(ahead) - 1, std::memory_order_relaxed);
            high_ = number;
            seen_.set(number % kWindow);
            if ((h[9] & kUdpCopy) != 0) {
                stats.saved.fetch_add(1, std::memory_order_relaxed);  // (a copy is sent after its first)
            }
            return true;
        }
        const u16 behind = static_cast<u16>(high_ - number);
        if (behind >= kWindow) {
            return true;  // (too old to tell: ENet keeps one)
        }
        if (seen_.test(number % kWindow)) {
            return false;
        }
        seen_.set(number % kWindow);
        stats.missing.fetch_sub(1, std::memory_order_relaxed);  // (a skipped number came after all)
        if ((h[9] & kUdpCopy) != 0) {
            stats.saved.fetch_add(1, std::memory_order_relaxed);
        }
        return true;
    }

private:
    static constexpr u16 kWindow = 1024;
    bool any_ = false;
    u16 high_ = 0;
    std::bitset<kWindow> seen_;
};

std::atomic<u64> g_game_program{0};
std::atomic<int> g_threads{0};  // this module's threads that still run (tests wait for none)

struct ThreadCount {
    ThreadCount() { g_threads.fetch_add(1); }
    ~ThreadCount() { g_threads.fetch_sub(1); }
};

}  // namespace

// ---- regions, listings, addresses --------------------------------------------------------------------------------

Region RegionOfProgram(u64 program_id) {
    switch (program_id) {
    case rooms::kMh4uUsa: return Region::Americas;
    case rooms::kMh4uEurope: return Region::Europe;
    default: return Region::None;
    }
}

const char* RegionName(Region region) {
    switch (region) {
    case Region::Americas: return "USA";
    case Region::Europe: return "Europe";
    default: return "none";
    }
}

u16 LinkProtocol(Region region) {
    if (region == Region::None) {
        return 0;
    }
    return static_cast<u16>(0x3400 + 0x10 * (kTunnelVersion - 1) + static_cast<u16>(region));
}

void SetGameProgram(u64 program_id) {
    g_game_program = program_id;
}

u64 GameProgram() {
    return g_game_program.load();
}

std::array<u8, kListingSize> EncodeListing(const Hub& hub) {
    std::array<u8, kListingSize> b{};
    u8* p = b.data();
    std::memcpy(p, kListingMagic, 4);
    Put16(p + 4, kListingVersion);
    p[kLiRegion] = static_cast<u8>(hub.region);
    p[kLiFlags] = static_cast<u8>((hub.password ? kFlagPassword : 0) | (hub.crossplay ? kFlagCrossplay : 0));
    Put64(p + kLiProgram, hub.program_id);
    const u32 slots = std::clamp<u32>(hub.slots, 1, kMaxListSlots);
    const u32 players = std::clamp<u32>(hub.players, 1, slots);
    p[kLiPlayers] = static_cast<u8>(players);
    p[kLiSlots] = static_cast<u8>(slots);
    // what the server checks (a NetworkInfo's): no SSID, network type 2, players and places, no advertise data
    p[kNiSsidLength] = 0;
    p[kNiNetType] = 2;
    p[kNiNodeMax] = static_cast<u8>(slots);
    p[kNiNodeCount] = static_cast<u8>(players);
    Put16(p + kNiAdvSize, 0);
    PutText(p + kLiName, kLiNameSize, hub.name);
    PutText(p + kLiDescription, kLiDescriptionSize, hub.description);
    PutText(p + kLiHost, kLiHostSize, hub.host);
    PutText(p + kLiVersion, kLiVersionSize, hub.app_version);
    for (std::size_t i = 0; i < hub.hunters.size() && i < kLiHunterCount; ++i) {
        PutText(p + kLiHunters + i * kLiHunterSize, kLiHunterSize, hub.hunters[i]);
    }
    return b;
}

std::optional<Hub> DecodeListing(const u8* p, std::size_t size) {
    if (size < kListingSize || std::memcmp(p, kListingMagic, 4) != 0 || Get16(p + 4) != kListingVersion) {
        return std::nullopt;
    }
    Hub hub;
    hub.region = static_cast<Region>(p[kLiRegion]);
    if (hub.region != Region::Americas && hub.region != Region::Europe) {
        return std::nullopt;
    }
    hub.password = (p[kLiFlags] & kFlagPassword) != 0;
    hub.crossplay = (p[kLiFlags] & kFlagCrossplay) != 0;
    hub.program_id = Get64(p + kLiProgram);
    hub.slots = std::clamp<u32>(p[kNiNodeMax], 1, kMaxListSlots);
    hub.players = std::clamp<u32>(p[kNiNodeCount], 1, hub.slots);
    hub.name = GetText(p + kLiName, kLiNameSize);
    hub.description = GetText(p + kLiDescription, kLiDescriptionSize);
    hub.host = GetText(p + kLiHost, kLiHostSize);
    hub.app_version = GetText(p + kLiVersion, kLiVersionSize);
    for (std::size_t i = 0; i < kLiHunterCount; ++i) {
        std::string hunter = GetText(p + kLiHunters + i * kLiHunterSize, kLiHunterSize);
        if (!hunter.empty()) {
            hub.hunters.push_back(std::move(hunter));
        }
    }
    return hub;
}

bool ParseServer(const std::string& typed, std::string* host, u16* port) {
    std::string text = Trimmed(typed);
    const auto scheme = text.find("://");
    if (scheme != std::string::npos) {
        text = text.substr(scheme + 3);
    }
    while (!text.empty() && text.back() == '/') {
        text.pop_back();
    }
    u16 p = kServerPort;
    const auto colon = text.rfind(':');
    if (colon != std::string::npos) {
        const std::string digits = text.substr(colon + 1);
        if (digits.empty() || digits.size() > 5 ||
            !std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            return false;
        }
        const long n = std::strtol(digits.c_str(), nullptr, 10);
        if (n < 1 || n > 65535) {
            return false;
        }
        p = static_cast<u16>(n);
        text.resize(colon);
    }
    if (text.empty() || text.size() > 253) {
        return false;
    }
    for (const char c : text) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) {
            return false;
        }
    }
    *host = text;
    *port = p;
    return true;
}

const char* ServerStateName(ServerState state) {
    switch (state) {
    case ServerState::Unknown: return "Unknown";
    case ServerState::Checking: return "Checking";
    case ServerState::Online: return "Online";
    case ServerState::NotFound: return "NotFound";
    case ServerState::Unreachable: return "Unreachable";
    case ServerState::Lost: return "Lost";
    case ServerState::NoAnswer: return "NoAnswer";
    case ServerState::Busy: return "Busy";
    case ServerState::Version: return "Version";
    case ServerState::Refused: return "Refused";
    }
    return "Unknown";
}

const char* JoinErrorName(JoinError error) {
    switch (error) {
    case JoinError::None: return "None";
    case JoinError::ServerNotFound: return "ServerNotFound";
    case JoinError::ServerUnreachable: return "ServerUnreachable";
    case JoinError::ServerBusy: return "ServerBusy";
    case JoinError::ServerVersion: return "ServerVersion";
    case JoinError::HubGone: return "HubGone";
    case JoinError::NoAnswer: return "NoAnswer";
    case JoinError::OtherGame: return "OtherGame";
    case JoinError::HubFull: return "HubFull";
    case JoinError::Lost: return "Lost";
    }
    return "Unknown";
}

// ---- LIST ---------------------------------------------------------------------------------------------------------

ListResult List(const std::string& server, Region region, const std::string& key, int timeout_ms) {
    ListResult result;
    std::string host;
    u16 port = 0;
    ENetAddress address{};
    if (!ParseServer(server, &host, &port) || !Resolve(host, port, &address)) {
        result.state = ServerState::NotFound;
        return result;
    }
    const u16 proto = LinkProtocol(region);
    if (proto == 0) {
        result.state = ServerState::Refused;
        return result;
    }
    const s64 start = NowMs();
    Socket socket = Connect(address, std::min(timeout_ms, 3000));
    if (!socket) {
        result.state = ServerState::Unreachable;
        return result;
    }
    const std::array<u8, kHeaderSize> header = Header(proto, key);
    FrameReader reader(kMaxServerFrame);
    u8 type = 0;
    std::vector<u8> payload;
    const s64 asked = NowMs();
    const int left = static_cast<int>(std::max<s64>(timeout_ms - (asked - start), 500));
    if (!SendFrame(socket.Get(), kList, header.data(), header.size()) ||
        !ReadFrame(socket.Get(), reader, left, &type, &payload)) {
        result.state = ServerState::NoAnswer;
        return result;
    }
    result.ms = static_cast<int>(NowMs() - asked);
    if (type == kError && payload.size() >= 4) {
        result.state = StateOfError(Get32(payload.data()));
        return result;
    }
    if (type != kHubs || payload.size() < 4) {
        result.state = ServerState::NoAnswer;
        return result;
    }
    const u32 count = Get32(payload.data());
    for (u32 i = 0; i < count && 4 + (i + 1) * (8 + kListingSize) <= payload.size(); ++i) {
        const u8* entry = payload.data() + 4 + i * (8 + kListingSize);
        std::optional<Hub> hub = DecodeListing(entry + 8, kListingSize);
        if (hub && hub->region == region) {
            hub->id = Get64(entry);
            result.hubs.push_back(std::move(*hub));
        }
    }
    result.state = ServerState::Online;
    return result;
}

// ---- hosting ------------------------------------------------------------------------------------------------------

namespace {

// A player connected through the server: its spliced link, and its own socket towards the room (a loopback address
// of its own, 127.77.x.y, so the room tells the players apart and none of them is "this machine", whose member with
// the host's nickname is the room's moderator)
struct Endpoint {
    u32 token = 0;
    u32 pair = 0;
    u64 secret = 0;
    Socket local;
    std::atomic<bool> udp_up{false};
    std::atomic<s64> udp_rx{0};
    LinkStats stats;
    UdpSender sender;      // (the player's thread)
    UdpReceiver receiver;  // (the relay's thread)
    std::string who;
};

struct HostSession {
    HostRequest request;
    u16 proto = 0;
    ENetAddress room{};    // 127.0.0.1 and the room's port
    std::atomic<bool> stop{false};
    Socket relay;          // UDP, to the server's relay (every player's datagrams): made before any player's thread
    std::mutex mutex;
    ENetAddress server{};  // TCP and UDP (resolved again at each connection: a dynamic name may move)
    std::map<u32, std::shared_ptr<Endpoint>> endpoints;  // by token
    std::atomic<u32> next_link{1};

    ENetAddress Server() {
        std::lock_guard lock(mutex);
        return server;
    }
};

std::mutex g_host_mutex;
std::shared_ptr<HostSession> g_host;
HostStatus g_host_status;

void SetHostStatus(const HostSession* session, const std::function<void(HostStatus&)>& change) {
    std::lock_guard lock(g_host_mutex);
    if (g_host.get() != session) {
        return;  // (a session that was stopped)
    }
    change(g_host_status);
    ++g_host_status.revision;
}

void CountLinks(const std::shared_ptr<HostSession>& s) {
    u32 links = 0, udp = 0;
    {
        std::lock_guard lock(s->mutex);
        for (const auto& [token, e] : s->endpoints) {
            ++links;
            udp += e->udp_up.load() ? 1 : 0;
        }
    }
    SetHostStatus(s.get(), [links, udp](HostStatus& st) {
        st.links = links;
        st.links_udp = udp;
    });
}

// Waits up to `ms`, or until the session stops (false then)
bool Nap(const std::shared_ptr<HostSession>& s, int ms) {
    const s64 end = NowMs() + ms;
    while (!s->stop.load() && NowMs() < end) {
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min<s64>(100, std::max<s64>(end - NowMs(), 1))));
    }
    return !s->stop.load();
}

rooms::Server& RoomOf(const HostSession& s) {
    return s.request.room != nullptr ? *s.request.room : rooms::DefaultServer();
}

std::array<u8, kListingSize> CurrentListing(const HostSession& s) {
    rooms::Server& room = RoomOf(s);
    const rooms::RoomInformation info = room.Information();
    const std::vector<rooms::Member> members = room.Members();
    Hub hub;
    hub.name = s.request.name.empty() ? info.name : s.request.name;
    hub.description = s.request.description;
    hub.host = info.host_username;
    for (const rooms::Member& m : members) {
        hub.hunters.push_back(m.nickname);
    }
    hub.players = static_cast<u32>(std::max<std::size_t>(members.size(), 1));
    hub.slots = std::min<u32>(std::max<u32>(info.member_slots, 2), kMaxListSlots);
    hub.password = s.request.password;
    hub.crossplay = s.request.crossplay;
    hub.region = RegionOfProgram(s.request.program_id);
    hub.program_id = s.request.program_id;
    hub.app_version = s.request.app_version;
    return EncodeListing(hub);
}

void SendToRoom(const HostSession& s, Endpoint& e, const u8* data, std::size_t size) {
    SendDatagram(e.local.Get(), s.room, data, size);
}

// A player's link, from the server's ticket to its end
void PlayerThread(std::shared_ptr<HostSession> s, u64 ticket) {
    ThreadCount counted;
    const ENetAddress server = s->Server();  // (this pair's, TCP and UDP)
    Socket tcp = Connect(server, 3000);
    if (!tcp) {
        MH4N_LOG(Info, kLog, "a player's connection through the hub server failed (no connection)");
        return;
    }
    const std::array<u8, kHeaderSize> header = Header(s->proto, s->request.key);
    u8 attach[kHeaderSize + 8];
    std::memcpy(attach, header.data(), kHeaderSize);
    Put64(attach + kHeaderSize, ticket);
    FrameReader reader(kMaxTunnelFrame);
    u8 type = 0;
    std::vector<u8> payload;
    if (!SendFrame(tcp.Get(), kAttach, attach, sizeof(attach)) ||
        !ReadFrame(tcp.Get(), reader, 5000, &type, &payload, &s->stop) || type != kSpliced || payload.size() < 12) {
        MH4N_LOG(Info, kLog, "a player's connection through the hub server failed");
        return;
    }
    auto e = std::make_shared<Endpoint>();
    e->pair = Get32(payload.data());
    e->secret = Get64(payload.data() + 4);
    // the player's hello: this port's tunnel, the same link protocol (region), and room for one more
    if (!ReadFrame(tcp.Get(), reader, 10000, &type, &payload, &s->stop) || type != kHello || payload.size() < 16 ||
        std::memcmp(payload.data(), kTunnelMagic, 4) != 0) {
        MH4N_LOG(Info, kLog, "a player through the hub server sent no hello");
        return;
    }
    const u16 version = Get16(payload.data() + 4);
    const u16 proto = Get16(payload.data() + 6);
    u32 refuse = 0;
    std::size_t links = 0;
    {
        std::lock_guard lock(s->mutex);
        links = s->endpoints.size();
    }
    if (version != kTunnelVersion) {
        refuse = kRefuseVersion;
    } else if (proto != s->proto) {
        refuse = kRefuseOtherGame;
    } else if (links >= kMaxLinks) {
        refuse = kRefuseFull;
    }
    // its own loopback address (127.77.x.y), so the room tells it apart
    u16 local_port = 0;
    if (refuse == 0) {
        for (int attempt = 0; attempt < 8 && !e->local; ++attempt) {
            const u32 n = static_cast<u32>(Random64());
            const u32 ip = (127u << 24) | (77u << 16) | (((n >> 8) & 0xFF) << 8) | (1 + n % 254);
            e->local = Udp(ENET_HOST_TO_NET_32(ip), &local_port);
        }
        if (!e->local) {
            MH4N_LOG(Warning, kLog, "no loopback address of 127.77.0.0/16 could be bound for a player: refused");
            refuse = kRefuseFull;
        }
    }
    if (refuse != 0) {
        u8 reason[4];
        Put32(reason, refuse);
        SendFrame(tcp.Get(), kRefuse, reason, sizeof(reason));
        MH4N_LOG(Info, kLog, "refused a player through the hub server ({})",
                 refuse == kRefuseVersion ? "another version of the tunnel"
                 : refuse == kRefuseOtherGame ? "another region of the game" : "full");
        return;
    }
    {
        std::lock_guard lock(s->mutex);
        do {
            e->token = static_cast<u32>(Random64()) | 1;
        } while (s->endpoints.count(e->token) != 0);
        s->endpoints[e->token] = e;
    }
    e->who = fmt::format("hub link {}", s->next_link++);
    u8 welcome[8] = {};
    Put32(welcome, e->token);
    welcome[4] = (s->relay && UdpAllowed()) ? 1 : 0;
    const auto unregister = [&s, &e] {
        std::lock_guard lock(s->mutex);
        s->endpoints.erase(e->token);
    };
    if (!SendFrame(tcp.Get(), kWelcome, welcome, sizeof(welcome))) {
        unregister();
        return;
    }
    MH4N_LOG(Info, kLog, "{}: a player joined through the hub server", e->who);
    CountLinks(s);

    s64 now = NowMs(), last_rx = now, next_ping = now + kLinkPingMs, next_register = 0;
    bool counted_udp = false;
    std::vector<u8> udp(kRelayPrefix + kUdpHeader + 65536);
    std::vector<u8> datagram(65536);
    bool bye = false;
    while (!s->stop.load() && !bye) {
        now = NowMs();
        const bool udp_up = e->udp_up.load() && now - e->udp_rx.load() < kUdpSilenceMs;
        if (e->udp_up.load() && !udp_up) {
            e->udp_up = false;
            MH4N_LOG(Info, kLog, "{}: nothing by UDP for 6 s: datagrams go through the TCP link", e->who);
        }
        if (udp_up != counted_udp) {
            counted_udp = udp_up;
            CountLinks(s);
        }
        if (!udp_up && s->relay && UdpAllowed() && now >= next_register) {
            // (the relay's prefix alone: the server learns where this end is, and passes nothing on)
            next_register = now + 1000;
            BuildUdp(udp.data(), e->pair, e->secret, 0, 0, nullptr, 0);
            SendDatagram(s->relay.Get(), server, udp.data(), kRelayPrefix);
        }
        if (now >= next_ping) {
            next_ping = now + kLinkPingMs;
            u8 stamp[8];
            Put64(stamp, static_cast<u64>(now));
            if (!SendFrame(tcp.Get(), kTunnelPing, stamp, sizeof(stamp))) {
                break;
            }
        }
        if (now - last_rx > kLinkSilenceMs) {
            MH4N_LOG(Info, kLog, "{}: nothing from the player for 30 s", e->who);
            break;
        }
        e->stats.Log(e->who + " (the player)");
        e->sender.Flush(s->relay.Get(), server, NowMs());
        const u32 ready = WaitReadable({tcp.Get(), e->local.Get()}, e->sender.WaitMs(100, NowMs()));
        if (ready & 1) {
            if (!reader.Fill(tcp.Get())) {
                break;
            }
            last_rx = NowMs();
            bool bad = false;
            while (reader.Next(&type, &payload, &bad)) {
                if (type == kDatagram && !payload.empty()) {
                    e->stats.Count(LinkStats::TcpIn, payload.size());
                    SendToRoom(*s, *e, payload.data(), payload.size());
                } else if (type == kTunnelPing && payload.size() >= 8) {
                    SendFrame(tcp.Get(), kPong, payload.data(), 8);
                } else if (type == kPong && payload.size() >= 8) {
                    e->stats.rtt = static_cast<s32>(NowMs() - static_cast<s64>(Get64(payload.data())));
                } else if (type == kBye) {
                    bye = true;
                }
            }
            if (bad) {
                break;
            }
        }
        if (ready & 2) {  // the room's datagrams for this player
            for (int k = 0; k < 64; ++k) {
                ENetAddress from{};
                ENetBuffer buffer;
                buffer.data = datagram.data();
                buffer.dataLength = datagram.size();
                const int n = enet_socket_receive(e->local.Get(), &from, &buffer, 1);
                if (n <= 0) {
                    break;
                }
                if (from.host != s->room.host || from.port != s->room.port) {
                    continue;
                }
                const std::size_t size = static_cast<std::size_t>(n);
                if (e->udp_up.load() && kRelayPrefix + kUdpHeader + size <= kMaxUdpPayload) {
                    const std::size_t total =
                        BuildUdp(udp.data(), e->pair, e->secret, e->token, kUdpData, datagram.data(), size);
                    e->sender.Send(s->relay.Get(), server, udp.data(), total, e->stats.rtt.load(), e->stats);
                    e->stats.Count(LinkStats::UdpOut, size);
                } else {
                    if (!SendFrame(tcp.Get(), kDatagram, datagram.data(), size)) {
                        bye = true;
                        break;
                    }
                    e->stats.Count(LinkStats::TcpOut, size);
                }
            }
        }
    }
    if (!bye) {
        SendFrame(tcp.Get(), kBye, nullptr, 0);
    }
    unregister();
    MH4N_LOG(Info, kLog, "{}: the player's link ended", e->who);
    CountLinks(s);
}

// The datagrams the server relays from the players: each to its player's endpoint
void RelayThread(std::shared_ptr<HostSession> s) {
    ThreadCount counted;
    std::vector<u8> buf(65536);
    std::vector<u8> out(kRelayPrefix + kUdpHeader);
    while (!s->stop.load()) {
        if (WaitReadable({s->relay.Get()}, 100) == 0) {
            continue;
        }
        for (int k = 0; k < 64; ++k) {
            ENetAddress from{};
            ENetBuffer buffer;
            buffer.data = buf.data();
            buffer.dataLength = buf.size();
            const int n = enet_socket_receive(s->relay.Get(), &from, &buffer, 1);
            if (n <= 0) {
                break;
            }
            const std::size_t size = static_cast<std::size_t>(n);
            const ENetAddress server = s->Server();
            if (from.host != server.host || size < kUdpHeader || std::memcmp(buf.data(), kUdpMagic, 4) != 0) {
                continue;
            }
            std::shared_ptr<Endpoint> e;
            {
                std::lock_guard lock(s->mutex);
                const auto it = s->endpoints.find(Get32(buf.data() + 4));
                if (it != s->endpoints.end()) {
                    e = it->second;
                }
            }
            if (e == nullptr) {
                continue;
            }
            const u8 kind = buf[8];
            if (kind == kUdpHello) {
                // (answered always; used for the room's datagrams only while the player hears this end by UDP)
                e->udp_rx = NowMs();
                const bool hears = buf[9] != 0;
                if (hears && !e->udp_up.exchange(true)) {
                    MH4N_LOG(Info, kLog, "{}: UDP through the hub server works both ways: datagrams go by UDP", e->who);
                } else if (!hears && e->udp_up.exchange(false)) {
                    MH4N_LOG(Info, kLog, "{}: the player stopped hearing this end by UDP: datagrams go through the TCP link", e->who);
                }
                const std::size_t total = BuildUdp(out.data(), e->pair, e->secret, e->token, kUdpHelloAck, nullptr, 0);
                SendDatagram(s->relay.Get(), server, out.data(), total);
            } else if (kind == kUdpData && size > kUdpHeader) {
                if (DropForTest()) {
                    continue;
                }
                e->udp_rx = NowMs();
                if (!e->receiver.Accept(buf.data(), e->stats)) {
                    continue;  // (the other of a pair)
                }
                e->stats.Count(LinkStats::UdpIn, size - kUdpHeader);
                SendToRoom(*s, *e, buf.data() + kUdpHeader, size - kUdpHeader);
            }
        }
    }
}

// The hub's listing on the server, and the players it sends
void ListerThread(std::shared_ptr<HostSession> s) {
    ThreadCount counted;
    std::string host;
    u16 port = 0;
    ParseServer(s->request.server, &host, &port);
    bool told_lost = false;
    while (!s->stop.load()) {
        if (!RoomOf(*s).IsOpen()) {
            break;
        }
        ENetAddress resolved{};
        if (!Resolve(host, port, &resolved)) {
            MH4N_LOG(Info, kLog, "hub server {} not found: the hub is not listed (trying again in 30 s)", s->request.server);
            SetHostStatus(s.get(), [](HostStatus& st) { st.state = ServerState::NotFound; });
            if (!Nap(s, 30000)) {
                break;
            }
            continue;
        }
        {
            std::lock_guard lock(s->mutex);
            s->server = resolved;
        }
        if (!s->relay && UdpAllowed()) {  // (once: its thread runs until the session stops)
            s->relay = Udp(ENET_HOST_ANY);
            if (s->relay) {
                std::thread(RelayThread, s).detach();
            }
        }
        Socket tcp = Connect(resolved, 3000);
        if (!tcp) {
            MH4N_LOG(Info, kLog, "hub server {} unreachable: the hub is not listed (trying again in 30 s)", s->request.server);
            SetHostStatus(s.get(), [](HostStatus& st) { st.state = ServerState::Unreachable; });
            if (!Nap(s, 30000)) {
                break;
            }
            continue;
        }
        const std::array<u8, kHeaderSize> header = Header(s->proto, s->request.key);
        std::array<u8, kListingSize> listing = CurrentListing(*s);
        FrameReader reader(kMaxTunnelFrame);
        u8 type = 0;
        std::vector<u8> payload;
        if (!SendFrame(tcp.Get(), kHostReq, header.data(), header.size(), listing.data(), listing.size()) ||
            !ReadFrame(tcp.Get(), reader, 5000, &type, &payload, &s->stop) ||
            !(type == kHosted && payload.size() >= 8)) {
            const bool error = type == kError && payload.size() >= 4;
            const u32 code = error ? Get32(payload.data()) : 0;
            MH4N_LOG(Info, kLog, "hub server {}: can't list the hub ({}); trying again in 30 s", s->request.server,
                     error ? ServerErrorText(code) : "no answer");
            SetHostStatus(s.get(), [error, code](HostStatus& st) {
                st.state = error ? StateOfError(code) : ServerState::NoAnswer;
            });
            if (!Nap(s, 30000)) {
                break;
            }
            continue;
        }
        const u64 id = Get64(payload.data());
        MH4N_LOG(Info, kLog, "hub listed on the hub server {} ({} hubs, link protocol {:#06x}{}): hub {}",
                 s->request.server, RegionName(RegionOfProgram(s->request.program_id)), s->proto,
                 Trimmed(s->request.key).empty() ? ", public" : ", for players with the same key", HexId(id));
        SetHostStatus(s.get(), [id](HostStatus& st) {
            st.state = ServerState::Online;
            st.hub_id = id;
        });
        told_lost = false;
        s64 now = NowMs(), last_rx = now, next_ping = now + kServerPingMs, next_check = now + 1000;
        bool broken = false;
        while (!s->stop.load() && !broken) {
            now = NowMs();
            if (now >= next_check) {
                next_check = now + 1000;
                if (!RoomOf(*s).IsOpen()) {
                    s->stop = true;
                    break;
                }
                const std::array<u8, kListingSize> fresh = CurrentListing(*s);
                if (fresh != listing) {
                    listing = fresh;
                    broken = !SendFrame(tcp.Get(), kUpdate, listing.data(), listing.size());
                }
            }
            if (now >= next_ping) {
                next_ping = now + kServerPingMs;
                broken = broken || !SendFrame(tcp.Get(), kPing, nullptr, 0);
            }
            if (now - last_rx > 3 * kServerPingMs) {
                broken = true;
            }
            if (broken || WaitReadable({tcp.Get()}, 100) == 0) {
                continue;
            }
            if (!reader.Fill(tcp.Get())) {
                broken = true;
                continue;
            }
            last_rx = NowMs();
            bool bad = false;
            while (reader.Next(&type, &payload, &bad)) {
                if (type == kIncoming && payload.size() >= 8) {
                    std::thread(PlayerThread, s, Get64(payload.data())).detach();
                }
            }
            broken = bad;
        }
        if (s->stop.load()) {
            break;
        }
        if (!told_lost) {
            MH4N_LOG(Info, kLog, "lost the hub server {}: the hub is not listed (trying again in 10 s)", s->request.server);
        }
        told_lost = true;
        SetHostStatus(s.get(), [](HostStatus& st) {
            st.state = ServerState::Lost;
            st.hub_id = 0;
        });
        if (!Nap(s, 10000)) {
            break;
        }
    }
    s->stop = true;
    MH4N_LOG(Info, kLog, "the hub is closed (no longer listed on {})", s->request.server);
    SetHostStatus(s.get(), [](HostStatus& st) {
        st.active = false;
        st.hub_id = 0;
    });
}

}  // namespace

bool Host(const HostRequest& request, std::string* error) {
    const auto fail = [error](const char* why) {
        if (error != nullptr) {
            *error = why;
        }
        return false;
    };
    std::string host;
    u16 port = 0;
    if (!ParseServer(request.server, &host, &port)) {
        return fail("No hub server.");
    }
    const u16 proto = LinkProtocol(RegionOfProgram(request.program_id));
    if (proto == 0) {
        return fail("This copy of the game has no hubs (only MH4U, USA or Europe).");
    }
    if (request.room_port == 0) {
        return fail("No room to list.");
    }
    if (!rooms::detail::EnsureEnet()) {
        return fail("The network could not be used.");
    }
    StopHosting();
    auto s = std::make_shared<HostSession>();
    s->request = request;
    s->proto = proto;
    s->room.host = ENET_HOST_TO_NET_32(0x7F000001);
    s->room.port = request.room_port;
    {
        std::lock_guard lock(g_host_mutex);
        g_host = s;
        g_host_status = {};
        g_host_status.active = true;
        g_host_status.state = ServerState::Checking;
        g_host_status.server = request.server;
        g_host_status.revision = 1;
    }
    std::thread(ListerThread, s).detach();
    return true;
}

void StopHosting() {
    std::shared_ptr<HostSession> s;
    {
        std::lock_guard lock(g_host_mutex);
        s = std::move(g_host);
        g_host.reset();
        if (g_host_status.active) {
            g_host_status.active = false;
            g_host_status.hub_id = 0;
            g_host_status.links = g_host_status.links_udp = 0;
            ++g_host_status.revision;
        }
    }
    if (s != nullptr) {
        s->stop = true;  // (its threads end by themselves within a moment; the hub leaves the server's list then)
    }
}

HostStatus GetHostStatus() {
    std::lock_guard lock(g_host_mutex);
    return g_host_status;
}

// ---- joining ------------------------------------------------------------------------------------------------------

namespace {

struct JoinSession {
    JoinRequest request;
    std::atomic<bool> stop{false};
    std::atomic<u16> local_port{0};
    // When the room client has left the hub's room, the tunnel stays a moment for its goodbye to reach the room (else
    // the room keeps the player until ENet gives up on it, up to 30 s): the time it closes, 0 before that
    std::atomic<s64> close_at{0};
};
constexpr int kGoodbyeMs = 1000;

std::mutex g_join_mutex;
std::shared_ptr<JoinSession> g_join;
JoinStatus g_join_status;

void SetJoinStatus(const JoinSession* session, const std::function<void(JoinStatus&)>& change) {
    std::lock_guard lock(g_join_mutex);
    if (g_join.get() != session) {
        return;
    }
    change(g_join_status);
    ++g_join_status.revision;
}

rooms::Client& ClientOf(const JoinSession& s) {
    return s.request.client != nullptr ? *s.request.client : rooms::Default();
}

bool OnTunnel(const rooms::Status& st, u16 local_port) {
    return local_port != 0 && st.host == "127.0.0.1" && st.port == local_port && st.state != rooms::State::Idle;
}

void TunnelThread(std::shared_ptr<JoinSession> s) {
    ThreadCount counted;
    const JoinRequest& r = s->request;
    const auto fail = [&s](JoinError what) {
        SetJoinStatus(s.get(), [what](JoinStatus& st) {
            st.state = JoinState::Idle;
            st.error = what;
            st.udp = false;
            st.local_port = 0;
        });
    };
    const u16 proto = LinkProtocol(RegionOfProgram(r.program_id));
    std::string host;
    u16 port = 0;
    ENetAddress server{};
    if (!ParseServer(r.server, &host, &port) || !Resolve(host, port, &server)) {
        MH4N_LOG(Info, kLog, "joining hub {}: hub server {} not found", HexId(r.hub_id), r.server);
        fail(JoinError::ServerNotFound);
        return;
    }
    Socket tcp = Connect(server, 3000);
    if (!tcp) {
        MH4N_LOG(Info, kLog, "joining hub {}: can't reach the hub server {}", HexId(r.hub_id), r.server);
        fail(JoinError::ServerUnreachable);
        return;
    }
    SetJoinStatus(s.get(), [](JoinStatus& st) { st.state = JoinState::Waiting; });
    const std::array<u8, kHeaderSize> header = Header(proto, r.key);
    u8 join[kHeaderSize + 8];
    std::memcpy(join, header.data(), kHeaderSize);
    Put64(join + kHeaderSize, r.hub_id);
    FrameReader reader(kMaxTunnelFrame);
    u8 type = 0;
    std::vector<u8> payload;
    // (the server waits up to 10 s for the host)
    if (!SendFrame(tcp.Get(), kJoinReq, join, sizeof(join)) ||
        !ReadFrame(tcp.Get(), reader, 12000, &type, &payload, &s->stop)) {
        if (!s->stop.load()) {
            MH4N_LOG(Info, kLog, "joining hub {} through {}: no answer", HexId(r.hub_id), r.server);
            fail(JoinError::NoAnswer);
        }
        return;
    }
    if (type == kError && payload.size() >= 4) {
        const u32 code = Get32(payload.data());
        MH4N_LOG(Info, kLog, "joining hub {} through {} failed: {}", HexId(r.hub_id), r.server, ServerErrorText(code));
        fail(code == kErrNotFound ? JoinError::HubGone
             : code == kErrNoAnswer ? JoinError::NoAnswer
             : code == kErrBusy ? JoinError::ServerBusy
             : code == kErrVersion ? JoinError::ServerVersion : JoinError::NoAnswer);
        return;
    }
    if (type != kSpliced || payload.size() < 12) {
        fail(JoinError::NoAnswer);
        return;
    }
    const u32 pair = Get32(payload.data());
    const u64 secret = Get64(payload.data() + 4);
    u8 hello[16] = {};
    std::memcpy(hello, kTunnelMagic, 4);
    Put16(hello + 4, kTunnelVersion);
    Put16(hello + 6, proto);
    Put64(hello + 8, r.program_id);
    if (!SendFrame(tcp.Get(), kHello, hello, sizeof(hello)) ||
        !ReadFrame(tcp.Get(), reader, 10000, &type, &payload, &s->stop)) {
        if (!s->stop.load()) {
            fail(JoinError::NoAnswer);
        }
        return;
    }
    if (type == kRefuse && payload.size() >= 4) {
        const u32 why = Get32(payload.data());
        MH4N_LOG(Info, kLog, "hub {}: its host refused this player ({})", HexId(r.hub_id),
                 why == kRefuseFull ? "full" : "another region or version");
        fail(why == kRefuseFull ? JoinError::HubFull : JoinError::OtherGame);
        return;
    }
    if (type != kWelcome || payload.size() < 5) {
        fail(JoinError::NoAnswer);
        return;
    }
    const u32 token = Get32(payload.data());
    const bool host_udp = payload[4] != 0;
    u16 local_port = 0;
    Socket proxy = Udp(ENET_HOST_TO_NET_32(0x7F000001), &local_port);
    Socket relay = host_udp && UdpAllowed() ? Udp(ENET_HOST_ANY) : Socket();
    if (!proxy) {
        MH4N_LOG(Error, kLog, "hub {}: no loopback socket for the tunnel", HexId(r.hub_id));
        fail(JoinError::Lost);
        return;
    }
    s->local_port = local_port;
    SetJoinStatus(s.get(), [local_port](JoinStatus& st) {
        st.state = JoinState::Tunnel;
        st.local_port = local_port;
    });
    MH4N_LOG(Info, kLog, "hub {}: the tunnel through {} is up; joining its room", HexId(r.hub_id), r.server);
    rooms::JoinRequest room = r.room;
    room.host = "127.0.0.1";
    room.port = local_port;
    room.mtu = kTunnelMtu;
    rooms::Client& client = ClientOf(*s);
    std::string error;
    if (!client.Join(room, &error)) {
        MH4N_LOG(Info, kLog, "hub {}: the room client didn't start: {}", HexId(r.hub_id), error);
        SendFrame(tcp.Get(), kBye, nullptr, 0);
        fail(JoinError::None);
        return;
    }

    LinkStats stats;
    UdpSender sender;
    UdpReceiver receiver;
    const std::string who = "hub link to the host";
    ENetAddress client_address{};
    bool have_client = false;
    bool udp_up = false;
    s64 now = NowMs(), last_rx = now, udp_rx = 0, next_hello = 0, next_ping = now + kLinkPingMs, next_watch = now;
    std::vector<u8> buf(65536);
    std::vector<u8> udp(kRelayPrefix + kUdpHeader + 65536);
    bool lost = false, left = false;
    while (!s->stop.load()) {
        now = NowMs();
        if (udp_up && now - udp_rx > kUdpSilenceMs) {
            udp_up = false;
            next_hello = 0;  // (the host hears so at once)
            MH4N_LOG(Info, kLog, "{}: nothing by UDP for 6 s: datagrams go through the TCP link", who);
            SetJoinStatus(s.get(), [](JoinStatus& st) { st.udp = false; });
        }
        if (relay && now >= next_hello) {
            next_hello = now + (udp_up ? 2000 : 500);
            const std::size_t n = BuildUdp(udp.data(), pair, secret, token, kUdpHello, nullptr, 0, udp_up ? 1 : 0);
            SendDatagram(relay.Get(), server, udp.data(), n);
        }
        if (now >= next_ping) {
            next_ping = now + kLinkPingMs;
            u8 stamp[8];
            Put64(stamp, static_cast<u64>(now));
            if (!SendFrame(tcp.Get(), kTunnelPing, stamp, sizeof(stamp))) {
                lost = true;
                break;
            }
        }
        if (now - last_rx > kLinkSilenceMs) {
            MH4N_LOG(Info, kLog, "{}: nothing from the host for 30 s", who);
            lost = true;
            break;
        }
        if (now >= next_watch) {  // (the room client left the hub's room, or joined another: the tunnel ends)
            next_watch = now + 100;
            if (s->close_at.load() == 0 && !OnTunnel(client.GetStatus(), local_port)) {
                s64 none = 0;
                s->close_at.compare_exchange_strong(none, now + kGoodbyeMs);
            }
        }
        if (s->close_at.load() != 0 && now >= s->close_at.load()) {
            left = true;
            break;
        }
        stats.Log(who);
        if (relay) {
            sender.Flush(relay.Get(), server, NowMs());
        }
        const int wait = sender.WaitMs(100, NowMs());
        const u32 ready = relay ? WaitReadable({tcp.Get(), proxy.Get(), relay.Get()}, wait)
                                : WaitReadable({tcp.Get(), proxy.Get()}, wait);
        if (ready & 1) {
            if (!reader.Fill(tcp.Get())) {
                lost = true;
                break;
            }
            last_rx = NowMs();
            bool bad = false;
            while (reader.Next(&type, &payload, &bad)) {
                if (type == kDatagram && !payload.empty() && have_client) {
                    stats.Count(LinkStats::TcpIn, payload.size());
                    SendDatagram(proxy.Get(), client_address, payload.data(), payload.size());
                } else if (type == kTunnelPing && payload.size() >= 8) {
                    SendFrame(tcp.Get(), kPong, payload.data(), 8);
                } else if (type == kPong && payload.size() >= 8) {
                    stats.rtt = static_cast<s32>(NowMs() - static_cast<s64>(Get64(payload.data())));
                } else if (type == kBye) {
                    lost = true;
                }
            }
            if (bad || lost) {
                lost = true;
                break;
            }
        }
        if (ready & 2) {  // the room client's datagrams
            for (int k = 0; k < 64; ++k) {
                ENetAddress from{};
                ENetBuffer buffer;
                buffer.data = buf.data();
                buffer.dataLength = buf.size();
                const int n = enet_socket_receive(proxy.Get(), &from, &buffer, 1);
                if (n <= 0) {
                    break;
                }
                if (from.host != ENET_HOST_TO_NET_32(0x7F000001)) {
                    continue;
                }
                client_address = from;
                have_client = true;
                const std::size_t size = static_cast<std::size_t>(n);
                if (udp_up && kRelayPrefix + kUdpHeader + size <= kMaxUdpPayload) {
                    const std::size_t total = BuildUdp(udp.data(), pair, secret, token, kUdpData, buf.data(), size);
                    sender.Send(relay.Get(), server, udp.data(), total, stats.rtt.load(), stats);
                    stats.Count(LinkStats::UdpOut, size);
                } else {
                    if (!SendFrame(tcp.Get(), kDatagram, buf.data(), size)) {
                        lost = true;
                        break;
                    }
                    stats.Count(LinkStats::TcpOut, size);
                }
            }
            if (lost) {
                break;
            }
        }
        if (ready & 4) {  // the host's datagrams, through the server's relay
            for (int k = 0; k < 64; ++k) {
                ENetAddress from{};
                ENetBuffer buffer;
                buffer.data = buf.data();
                buffer.dataLength = buf.size();
                const int n = enet_socket_receive(relay.Get(), &from, &buffer, 1);
                if (n <= 0) {
                    break;
                }
                const std::size_t size = static_cast<std::size_t>(n);
                if (from.host != server.host || size < kUdpHeader || std::memcmp(buf.data(), kUdpMagic, 4) != 0 ||
                    Get32(buf.data() + 4) != token) {
                    continue;
                }
                if (buf[8] == kUdpHelloAck) {
                    udp_rx = NowMs();
                    if (!udp_up) {
                        udp_up = true;
                        next_hello = 0;  // (a hello that says so, at once: the host then sends by UDP too)
                        MH4N_LOG(Info, kLog, "{}: UDP through the hub server works both ways: datagrams go by UDP", who);
                        SetJoinStatus(s.get(), [](JoinStatus& st) { st.udp = true; });
                    }
                } else if (buf[8] == kUdpData && size > kUdpHeader && have_client) {
                    if (DropForTest()) {
                        continue;
                    }
                    udp_rx = NowMs();
                    if (!receiver.Accept(buf.data(), stats)) {
                        continue;  // (the other of a pair)
                    }
                    stats.Count(LinkStats::UdpIn, size - kUdpHeader);
                    SendDatagram(proxy.Get(), client_address, buf.data() + kUdpHeader, size - kUdpHeader);
                }
            }
        }
    }
    if (!lost) {
        SendFrame(tcp.Get(), kBye, nullptr, 0);
    }
    if (lost && !s->stop.load()) {
        MH4N_LOG(Info, kLog, "hub {}: the link to its host ended", HexId(r.hub_id));
        if (OnTunnel(client.GetStatus(), local_port)) {
            client.Leave();
        }
        fail(JoinError::Lost);
        return;
    }
    if (left) {
        MH4N_LOG(Info, kLog, "hub {}: left its room: the tunnel is closed", HexId(r.hub_id));
        fail(JoinError::None);
    }
}

}  // namespace

bool Join(const JoinRequest& request, std::string* error) {
    const auto fail = [error](const char* why) {
        if (error != nullptr) {
            *error = why;
        }
        return false;
    };
    std::string host;
    u16 port = 0;
    if (!ParseServer(request.server, &host, &port)) {
        return fail("No hub server.");
    }
    if (LinkProtocol(RegionOfProgram(request.program_id)) == 0) {
        return fail("This copy of the game has no hubs (only MH4U, USA or Europe).");
    }
    if (!rooms::ValidNickname(request.room.nickname)) {
        return fail("The nickname must be 4 to 20 letters, digits, spaces, '.', '_' or '-'.");
    }
    if (!rooms::detail::EnsureEnet()) {
        return fail("The network could not be used.");
    }
    Leave();
    auto s = std::make_shared<JoinSession>();
    s->request = request;
    {
        std::lock_guard lock(g_join_mutex);
        g_join = s;
        g_join_status = {};
        g_join_status.state = JoinState::Connecting;
        g_join_status.server = request.server;
        g_join_status.hub_id = request.hub_id;
        g_join_status.hub_name = request.hub_name;
        g_join_status.revision = 1;
    }
    MH4N_LOG(Info, kLog, "joining hub {} ({}) through the hub server {}", HexId(request.hub_id), request.hub_name,
             request.server);
    std::thread(TunnelThread, s).detach();
    return true;
}

void Leave() {
    std::shared_ptr<JoinSession> s;
    {
        std::lock_guard lock(g_join_mutex);
        s = std::move(g_join);
        g_join.reset();
        if (g_join_status.state != JoinState::Idle) {
            g_join_status.state = JoinState::Idle;
            g_join_status.local_port = 0;
            g_join_status.udp = false;
            ++g_join_status.revision;
        }
    }
    if (s == nullptr) {
        return;
    }
    rooms::Client& client = ClientOf(*s);
    if (OnTunnel(client.GetStatus(), s->local_port)) {
        // (in the hub's room: its goodbye goes through the tunnel, which closes a moment later by itself)
        s->close_at = NowMs() + kGoodbyeMs;
        client.Leave();
    } else {
        s->stop = true;
    }
}

JoinStatus GetJoinStatus() {
    std::lock_guard lock(g_join_mutex);
    return g_join_status;
}

bool InHub(const rooms::Status& status) {
    const JoinStatus join = GetJoinStatus();
    return join.state == JoinState::Tunnel && OnTunnel(status, join.local_port);
}

namespace detail {

int RunningThreads() {
    return g_threads.load();
}

}  // namespace detail

}  // namespace mh4n::hubs
