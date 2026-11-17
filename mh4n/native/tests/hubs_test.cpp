// Tests of internet hubs (mh4n/hubs.h): the listing, the server addresses and regions, and a hub end to end in one
// process over the loopback interface: a hosted room listed on a hub server, a second room client that joins it
// through the server, chat and Wi-Fi packets both ways, by UDP through the server's relay and through the TCP link.
//
// The server here is a stand-in written for these tests, after the protocol of MHGURecomp's hub server (its
// repository's server/server.go): LIST, HOST with UPDATE and PING, JOIN and ATTACH spliced into one pipe, and the UDP
// relay between the two ends of a pair. Its limits and rate checks are left out. The real server was tried by hand
// (docs/native/rooms.md, Hubs).
#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <enet/enet.h>

#include "mh4n/hubs.h"
#include "mh4n/rooms.h"

namespace {

using namespace mh4n;

template <class Pred>
bool WaitFor(Pred pred, int ms = 8000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

u16 NextPort() {
    static std::atomic<u16> port{26600};
    return port++;
}

u32 Get32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}
u64 Get64(const u8* p) {
    return static_cast<u64>(Get32(p)) | (static_cast<u64>(Get32(p + 4)) << 32);
}
void Put32(std::vector<u8>& out, u32 v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<u8>(v >> (8 * i)));
    }
}
void Put64(std::vector<u8>& out, u64 v) {
    Put32(out, static_cast<u32>(v));
    Put32(out, static_cast<u32>(v >> 32));
}

// ---- a stand-in hub server (blocking sockets, a thread per connection) ----

bool ReadAll(ENetSocket s, u8* data, std::size_t size) {
    while (size > 0) {
        ENetBuffer b;
        b.data = data;
        b.dataLength = size;
        const int n = enet_socket_receive(s, nullptr, &b, 1);
        if (n <= 0) {
            return false;
        }
        data += n;
        size -= static_cast<std::size_t>(n);
    }
    return true;
}
bool WriteAll(ENetSocket s, const std::vector<u8>& data) {
    std::size_t done = 0;
    while (done < data.size()) {
        ENetBuffer b;
        b.data = const_cast<u8*>(data.data() + done);
        b.dataLength = data.size() - done;
        const int n = enet_socket_send(s, nullptr, &b, 1);
        if (n <= 0) {
            return false;
        }
        done += static_cast<std::size_t>(n);
    }
    return true;
}
bool ReadFrame(ENetSocket s, u8* type, std::vector<u8>* payload) {
    u8 h[5];
    if (!ReadAll(s, h, 5)) {
        return false;
    }
    const u32 size = Get32(h);
    if (size < 1 || size > (1 << 20)) {
        return false;
    }
    *type = h[4];
    payload->resize(size - 1);
    return payload->empty() || ReadAll(s, payload->data(), payload->size());
}
std::vector<u8> Frame(u8 type, const std::vector<u8>& payload = {}) {
    std::vector<u8> out;
    Put32(out, static_cast<u32>(1 + payload.size()));
    out.push_back(type);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

class StandInServer {
public:
    explicit StandInServer(bool relay_udp = true) : relay_udp_(relay_udp) {
        for (int attempt = 0; attempt < 10 && !ok_; ++attempt) {  // (a port another program holds: the next one)
            if (listen_ != ENET_SOCKET_NULL) {
                enet_socket_destroy(listen_);
                enet_socket_destroy(udp_);
            }
            port_ = NextPort();
            listen_ = enet_socket_create(ENET_SOCKET_TYPE_STREAM);
            udp_ = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
            ENetAddress a{};
            a.host = ENET_HOST_TO_NET_32(0x7F000001);
            a.port = port_;
            enet_socket_set_option(listen_, ENET_SOCKOPT_REUSEADDR, 1);
            ok_ = enet_socket_bind(listen_, &a) == 0 && enet_socket_listen(listen_, 16) == 0 &&
                  enet_socket_bind(udp_, &a) == 0;
        }
        if (ok_) {
            accept_ = std::thread([this] { AcceptLoop(); });
            relay_ = std::thread([this] { RelayLoop(); });
        }
    }
    ~StandInServer() {
        stop_ = true;
        enet_socket_shutdown(listen_, ENET_SOCKET_SHUTDOWN_READ_WRITE);
        enet_socket_destroy(listen_);
        enet_socket_destroy(udp_);
        {
            std::lock_guard lock(mutex_);
            for (ENetSocket s : open_) {
                enet_socket_shutdown(s, ENET_SOCKET_SHUTDOWN_READ_WRITE);
            }
        }
        if (accept_.joinable()) {
            accept_.join();
        }
        if (relay_.joinable()) {
            relay_.join();
        }
        for (;;) {
            std::vector<std::thread> threads;
            {
                std::lock_guard lock(mutex_);
                threads.swap(threads_);
                for (ENetSocket s : open_) {
                    enet_socket_shutdown(s, ENET_SOCKET_SHUTDOWN_READ_WRITE);
                }
            }
            if (threads.empty()) {
                break;
            }
            for (std::thread& t : threads) {
                t.join();
            }
        }
    }
    bool ok() const { return ok_; }
    std::string address() const { return "127.0.0.1:" + std::to_string(port_); }
    std::size_t hubs() {
        std::lock_guard lock(mutex_);
        return hubs_.size();
    }
    int udp_relayed() const { return udp_relayed_.load(); }

private:
    struct HubEntry {
        u16 proto = 0;
        std::vector<u8> room, info;
        ENetSocket host = ENET_SOCKET_NULL;
        std::mutex write;
    };
    struct Pending {
        ENetSocket host = ENET_SOCKET_NULL;
        bool done = false;
    };
    struct Pair {
        u64 secret[2] = {};
        ENetAddress addr[2] = {};
        bool known[2] = {};
    };

    void Track(ENetSocket s, bool add) {
        std::lock_guard lock(mutex_);
        if (add) {
            open_.push_back(s);
        } else {
            open_.erase(std::remove(open_.begin(), open_.end(), s), open_.end());
        }
    }

    void AcceptLoop() {
        while (!stop_) {
            ENetAddress from{};
            const ENetSocket c = enet_socket_accept(listen_, &from);
            if (c == ENET_SOCKET_NULL) {
                if (stop_) {
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            std::lock_guard lock(mutex_);
            open_.push_back(c);
            threads_.emplace_back([this, c] {
                Handle(c);
                Track(c, false);
                enet_socket_destroy(c);
            });
        }
    }

    void Handle(ENetSocket c) {
        u8 type = 0;
        std::vector<u8> p;
        if (!ReadFrame(c, &type, &p) || p.size() < 24 || std::memcmp(p.data(), "MHGS", 4) != 0) {
            return;
        }
        const u16 proto = static_cast<u16>(p[6] | (p[7] << 8));
        const std::vector<u8> room(p.begin() + 8, p.begin() + 24);
        const std::vector<u8> body(p.begin() + 24, p.end());
        if (type == 1) {  // LIST
            std::vector<u8> out;
            std::lock_guard lock(mutex_);
            u32 n = 0;
            std::vector<u8> entries;
            for (auto& [id, h] : hubs_) {
                if (h->proto == proto && h->room == room) {
                    Put64(entries, id);
                    entries.insert(entries.end(), h->info.begin(), h->info.end());
                    ++n;
                }
            }
            Put32(out, n);
            out.insert(out.end(), entries.begin(), entries.end());
            WriteAll(c, Frame(16, out));
        } else if (type == 2 && body.size() == 0x480) {  // HOST
            auto h = std::make_shared<HubEntry>();
            h->proto = proto;
            h->room = room;
            h->info = body;
            h->host = c;
            u64 id = 0;
            {
                std::lock_guard lock(mutex_);
                id = next_id_++;
                hubs_[id] = h;
            }
            std::vector<u8> out;
            Put64(out, id);
            {
                std::lock_guard w(h->write);
                WriteAll(c, Frame(17, out));
            }
            while (ReadFrame(c, &type, &p)) {
                if (type == 5 && p.size() == 0x480) {
                    std::lock_guard lock(mutex_);
                    h->info = p;
                }
            }
            std::lock_guard lock(mutex_);
            hubs_.erase(id);
        } else if (type == 4 && body.size() == 8) {  // JOIN
            Join(c, proto, room, Get64(body.data()));
        } else if (type == 3 && body.size() == 8) {  // ATTACH
            std::shared_ptr<Pending> pending;
            {
                std::lock_guard lock(mutex_);
                const auto it = pending_.find(Get64(body.data()));
                if (it != pending_.end()) {
                    pending = it->second;
                    pending_.erase(it);
                }
            }
            if (pending == nullptr) {
                return;
            }
            std::unique_lock lock(mutex_);
            pending->host = c;
            cv_.notify_all();
            cv_.wait(lock, [&] { return pending->done || stop_.load(); });
        }
    }

    void Join(ENetSocket c, u16 proto, const std::vector<u8>& room, u64 id) {
        std::shared_ptr<HubEntry> h;
        {
            std::lock_guard lock(mutex_);
            const auto it = hubs_.find(id);
            if (it != hubs_.end() && it->second->proto == proto && it->second->room == room) {
                h = it->second;
            }
        }
        if (h == nullptr) {
            std::vector<u8> e;
            Put32(e, 1);  // not found
            WriteAll(c, Frame(20, e));
            return;
        }
        auto pending = std::make_shared<Pending>();
        const u64 ticket = 1000 + next_id_++;
        {
            std::lock_guard lock(mutex_);
            pending_[ticket] = pending;
        }
        {
            std::vector<u8> t;
            Put64(t, ticket);
            std::lock_guard w(h->write);
            WriteAll(h->host, Frame(18, t));
        }
        ENetSocket hc = ENET_SOCKET_NULL;
        {
            std::unique_lock lock(mutex_);
            cv_.wait_for(lock, std::chrono::seconds(10), [&] { return pending->host != ENET_SOCKET_NULL || stop_.load(); });
            hc = pending->host;
            pending_.erase(ticket);
        }
        const auto finish = [&] {
            std::lock_guard lock(mutex_);
            pending->done = true;
            cv_.notify_all();
        };
        if (hc == ENET_SOCKET_NULL) {
            std::vector<u8> e;
            Put32(e, 2);  // no answer
            WriteAll(c, Frame(20, e));
            finish();
            return;
        }
        const u32 pair_id = next_pair_++;
        {
            std::lock_guard lock(mutex_);
            Pair& pr = pairs_[pair_id];
            pr.secret[0] = 0x1111000000000000ULL + pair_id;
            pr.secret[1] = 0x2222000000000000ULL + pair_id;
        }
        std::vector<u8> sh, sp;
        Put32(sh, pair_id);
        Put64(sh, 0x1111000000000000ULL + pair_id);
        Put32(sp, pair_id);
        Put64(sp, 0x2222000000000000ULL + pair_id);
        WriteAll(hc, Frame(19, sh));
        WriteAll(c, Frame(19, sp));
        // the splice: each way copied until either end closes
        std::thread back([hc, c] {
            u8 buf[16384];
            for (;;) {
                ENetBuffer b;
                b.data = buf;
                b.dataLength = sizeof(buf);
                const int n = enet_socket_receive(hc, nullptr, &b, 1);
                if (n <= 0 || !WriteAll(c, std::vector<u8>(buf, buf + n))) {
                    break;
                }
            }
            enet_socket_shutdown(c, ENET_SOCKET_SHUTDOWN_READ_WRITE);
            enet_socket_shutdown(hc, ENET_SOCKET_SHUTDOWN_READ_WRITE);
        });
        u8 buf[16384];
        for (;;) {
            ENetBuffer b;
            b.data = buf;
            b.dataLength = sizeof(buf);
            const int n = enet_socket_receive(c, nullptr, &b, 1);
            if (n <= 0 || !WriteAll(hc, std::vector<u8>(buf, buf + n))) {
                break;
            }
        }
        enet_socket_shutdown(c, ENET_SOCKET_SHUTDOWN_READ_WRITE);
        enet_socket_shutdown(hc, ENET_SOCKET_SHUTDOWN_READ_WRITE);
        back.join();
        {
            std::lock_guard lock(mutex_);
            pairs_.erase(pair_id);
        }
        finish();
    }

    void RelayLoop() {
        std::vector<u8> buf(9000);
        while (!stop_) {
            enet_uint32 wait = ENET_SOCKET_WAIT_RECEIVE;
            if (enet_socket_wait(udp_, &wait, 100) != 0 || wait == 0) {
                continue;
            }
            ENetAddress from{};
            ENetBuffer b;
            b.data = buf.data();
            b.dataLength = buf.size();
            const int n = enet_socket_receive(udp_, &from, &b, 1);
            if (n < 16 || std::memcmp(buf.data(), "MHGR", 4) != 0) {
                continue;
            }
            const u32 id = Get32(buf.data() + 4);
            const u64 secret = Get64(buf.data() + 8);
            ENetAddress to{};
            bool send = false;
            {
                std::lock_guard lock(mutex_);
                const auto it = pairs_.find(id);
                if (it == pairs_.end()) {
                    continue;
                }
                Pair& pr = it->second;
                const int side = pr.secret[0] == secret ? 0 : pr.secret[1] == secret ? 1 : -1;
                if (side < 0) {
                    continue;
                }
                pr.addr[side] = from;
                pr.known[side] = true;
                if (n > 16 && pr.known[1 - side] && relay_udp_) {
                    to = pr.addr[1 - side];
                    send = true;
                }
            }
            if (send) {
                ENetBuffer out;
                out.data = buf.data() + 16;
                out.dataLength = static_cast<std::size_t>(n) - 16;
                enet_socket_send(udp_, &to, &out, 1);
                ++udp_relayed_;
            }
        }
    }

    bool relay_udp_;
    u16 port_ = 0;
    bool ok_ = false;
    std::atomic<bool> stop_{false};
    ENetSocket listen_ = ENET_SOCKET_NULL, udp_ = ENET_SOCKET_NULL;
    std::thread accept_, relay_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::thread> threads_;
    std::vector<ENetSocket> open_;
    std::map<u64, std::shared_ptr<HubEntry>> hubs_;
    std::map<u64, std::shared_ptr<Pending>> pending_;
    std::map<u32, Pair> pairs_;
    u64 next_id_ = 1;
    u32 next_pair_ = 1;
    std::atomic<int> udp_relayed_{0};
};

rooms::JoinRequest Member(const std::string& nick, u64 console) {
    rooms::JoinRequest r;
    r.nickname = nick;
    r.console_id_hash = rooms::ConsoleIdHash(console);
    return r;
}

bool InRoom(const rooms::Client& c) {
    const rooms::State s = c.GetState();
    return s == rooms::State::Joined || s == rooms::State::Moderator;
}

void EndHubs() {
    hubs::Leave();
    hubs::StopHosting();
    WaitFor([] { return hubs::detail::RunningThreads() == 0; }, 10000);
}

}  // namespace

TEST_CASE("hubs: a listing keeps its fields and passes the server's checks") {
    hubs::Hub hub;
    hub.name = "Night hunts";
    hub.description = "HR 7, G-rank";
    hub.host = "Hunter One";
    hub.hunters = {"Hunter One", "Hunter Two"};
    hub.players = 2;
    hub.slots = 4;
    hub.password = true;
    hub.region = hubs::Region::Europe;
    hub.program_id = rooms::kMh4uEurope;
    hub.app_version = "1.0.1";
    const auto b = hubs::EncodeListing(hub);
    REQUIRE(b.size() == 0x480);
    // server/server.go infoOK: SSID length <= 32, network type 2, 1 <= count <= max <= 8, advertise size <= 0x180
    CHECK(b[0x26] == 0);
    CHECK(b[0x4B] == 2);
    CHECK(b[0x66] == 4);
    CHECK(b[0x67] == 2);
    CHECK((b[0x26A] | (b[0x26B] << 8)) == 0);
    const auto back = hubs::DecodeListing(b.data(), b.size());
    REQUIRE(back.has_value());
    CHECK(back->name == hub.name);
    CHECK(back->description == hub.description);
    CHECK(back->host == hub.host);
    CHECK(back->hunters == hub.hunters);
    CHECK(back->players == 2);
    CHECK(back->slots == 4);
    CHECK(back->password);
    CHECK(!back->crossplay);
    CHECK(back->region == hubs::Region::Europe);
    CHECK(back->program_id == rooms::kMh4uEurope);
    CHECK(back->app_version == "1.0.1");

    // a hub of more than 8 places is listed as 8; players never below 1 nor above the places
    hub.slots = 16;
    hub.players = 0;
    const auto clamped = hubs::EncodeListing(hub);
    CHECK(clamped[0x66] == 8);
    CHECK(clamped[0x67] == 1);

    // a long name is cut at a whole UTF-8 character
    hub.name = std::string(62, 'a') + "\xC3\xB1\xC3\xB1";
    const auto cut = hubs::DecodeListing(hubs::EncodeListing(hub).data(), 0x480);
    REQUIRE(cut.has_value());
    CHECK(cut->name == std::string(62, 'a'));

    // MHGU's NetworkInfo (no magic) and a listing of no region are not hubs of this port
    std::array<u8, 0x480> other{};
    other[0x4B] = 2;
    other[0x66] = 4;
    other[0x67] = 1;
    CHECK(!hubs::DecodeListing(other.data(), other.size()).has_value());
    hub.region = hubs::Region::None;
    CHECK(!hubs::DecodeListing(hubs::EncodeListing(hub).data(), 0x480).has_value());
    CHECK(!hubs::DecodeListing(b.data(), 0x47F).has_value());
}

TEST_CASE("hubs: regions, link protocols and server addresses") {
    CHECK(hubs::RegionOfProgram(rooms::kMh4uUsa) == hubs::Region::Americas);
    CHECK(hubs::RegionOfProgram(rooms::kMh4uEurope) == hubs::Region::Europe);
    CHECK(hubs::RegionOfProgram(0x000400000011D700) == hubs::Region::None);  // Monster Hunter 4G
    CHECK(hubs::LinkProtocol(hubs::Region::Americas) == 0x3401);
    CHECK(hubs::LinkProtocol(hubs::Region::Europe) == 0x3402);
    CHECK(hubs::LinkProtocol(hubs::Region::None) == 0);

    std::string host;
    u16 port = 0;
    CHECK(hubs::ParseServer("hub.example.org", &host, &port));
    CHECK(host == "hub.example.org");
    CHECK(port == 39510);
    CHECK(hubs::ParseServer("  http://10.0.0.2:4000/ ", &host, &port));
    CHECK(host == "10.0.0.2");
    CHECK(port == 4000);
    CHECK(!hubs::ParseServer("", &host, &port));
    CHECK(!hubs::ParseServer("hub.example.org:0", &host, &port));
    CHECK(!hubs::ParseServer("hub.example.org:99999", &host, &port));
    CHECK(!hubs::ParseServer("hub example", &host, &port));
}

TEST_CASE("hubs: a server that isn't there") {
    const hubs::ListResult none = hubs::List("127.0.0.1:" + std::to_string(NextPort()), hubs::Region::Americas, {}, 1500);
    CHECK(none.state == hubs::ServerState::Unreachable);
    CHECK(hubs::List("no.such.host.invalid", hubs::Region::Americas, {}, 1500).state == hubs::ServerState::NotFound);
}

// MH4N_HUB_TEST_SERVER=HOST:PORT runs the UDP case against that server instead (the real one, by hand: gu-native's
// server/, `mhgurecomp-hubserver -addr 127.0.0.1:39510`); the stand-in's own counts are left out then
std::string RealServer() {
    const char* e = std::getenv("MH4N_HUB_TEST_SERVER");
    return e != nullptr ? e : "";
}

static void HubEndToEnd(bool udp) {
    const std::string real = udp ? RealServer() : std::string();
    StandInServer stand_in(udp);
    REQUIRE(stand_in.ok());
    // (the real server takes 2 requests a second from one address, 15 at once: its lists are asked at most that often,
    // and a list it refused counts as none of the answers waited for)
    struct Target {
        StandInServer& s;
        std::string real;
        std::string address() const { return real.empty() ? s.address() : real; }
        hubs::ListResult List() const {
            if (!real.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(600));
            }
            return hubs::List(address(), hubs::Region::Americas);
        }
        std::size_t hubs() const {
            if (real.empty()) {
                return s.hubs();
            }
            const hubs::ListResult l = List();
            return l.state == hubs::ServerState::Online ? l.hubs.size() : static_cast<std::size_t>(-1);
        }
        int udp_relayed() const { return real.empty() ? s.udp_relayed() : 1; }
    } server{stand_in, real};
    if (!real.empty()) {
        MESSAGE("against the hub server at " << real);
    }
    const u16 room_port = NextPort();
    rooms::Server room;
    rooms::HostRequest open;
    open.port = room_port;
    open.bind_address = "127.0.0.1";
    open.name = "Test hub";
    open.slots = 4;
    open.host_username = "Hunter One";
    std::string error;
    REQUIRE(room.Open(open, &error));

    rooms::Client host_client;
    rooms::JoinRequest own = Member("Hunter One", 1);
    own.host = "127.0.0.1";
    own.port = room_port;
    REQUIRE(host_client.Join(own, &error));
    REQUIRE(host_client.WaitJoined(5000));
    CHECK(host_client.GetState() == rooms::State::Moderator);

    hubs::HostRequest listing;
    listing.server = server.address();
    listing.program_id = rooms::kMh4uUsa;
    listing.room_port = room_port;
    listing.name = "Test hub";
    listing.description = "low rank";
    listing.app_version = "test";
    listing.room = &room;
    REQUIRE(hubs::Host(listing, &error));
    REQUIRE(WaitFor([] { return hubs::GetHostStatus().state == hubs::ServerState::Online; }));

    // listed for its region only
    hubs::ListResult list = hubs::List(server.address(), hubs::Region::Americas);
    REQUIRE(list.state == hubs::ServerState::Online);
    REQUIRE(list.hubs.size() == 1);
    CHECK(list.hubs[0].name == "Test hub");
    CHECK(list.hubs[0].description == "low rank");
    CHECK(list.hubs[0].host == "Hunter One");
    CHECK(list.hubs[0].players == 1);
    CHECK(list.hubs[0].hunters == std::vector<std::string>{"Hunter One"});
    CHECK(hubs::List(server.address(), hubs::Region::Europe).hubs.empty());
    CHECK(hubs::List(server.address(), hubs::Region::Americas, "a key").hubs.empty());

    // a European copy can't join it (the server keeps the link protocols apart)
    rooms::Client guest;
    hubs::JoinRequest join;
    join.server = server.address();
    join.program_id = rooms::kMh4uEurope;
    join.hub_id = list.hubs[0].id;
    join.hub_name = list.hubs[0].name;
    join.room = Member("Hunter Two", 2);
    join.client = &guest;
    REQUIRE(hubs::Join(join, &error));
    REQUIRE(WaitFor([] { return hubs::GetJoinStatus().state == hubs::JoinState::Idle; }));
    CHECK(hubs::GetJoinStatus().error == hubs::JoinError::HubGone);

    // an American one joins through the server
    std::mutex mutex;
    std::vector<rooms::WifiPacket> got_host, got_guest;
    host_client.AddWifiListener([&](const rooms::WifiPacket& p) {
        std::lock_guard lock(mutex);
        got_host.push_back(p);
    });
    guest.AddWifiListener([&](const rooms::WifiPacket& p) {
        std::lock_guard lock(mutex);
        got_guest.push_back(p);
    });
    join.program_id = rooms::kMh4uUsa;
    REQUIRE(hubs::Join(join, &error));
    REQUIRE(WaitFor([&] { return InRoom(guest); }));
    CHECK(guest.GetState() == rooms::State::Joined);  // (not a moderator: it is not "this machine")
    CHECK(hubs::InHub(guest.GetStatus()));
    CHECK(!hubs::InHub(host_client.GetStatus()));
    REQUIRE(WaitFor([&] { return host_client.GetStatus().members.size() == 2; }));
    REQUIRE(WaitFor([&] { return hubs::GetHostStatus().links == 1; }));
    if (udp) {
        REQUIRE(WaitFor([] { return hubs::GetJoinStatus().udp && hubs::GetHostStatus().links_udp == 1; }));
    }

    // the listing follows the room
    REQUIRE(WaitFor([&] {
        const hubs::ListResult l = server.List();
        return l.hubs.size() == 1 && l.hubs[0].players == 2;
    }));

    // chat and Wi-Fi packets both ways
    REQUIRE(guest.SendChat("hello from the hub"));
    std::vector<rooms::Event> events;
    REQUIRE(WaitFor([&] {
        for (rooms::Event& e : host_client.TakeEvents()) {
            events.push_back(std::move(e));
        }
        return std::any_of(events.begin(), events.end(), [](const rooms::Event& e) {
            return e.kind == rooms::Event::Kind::Chat && e.message == "hello from the hub";
        });
    }));
    rooms::WifiPacket packet;
    packet.type = rooms::WifiPacket::Type::Data;
    packet.destination = rooms::kBroadcastMac;
    packet.data.assign(1400, 0x5A);  // (larger than the tunnel's MTU: ENet cuts it in two)
    REQUIRE(host_client.SendWifi(packet));
    packet.data.assign(64, 0xA5);
    packet.destination = host_client.GetStatus().mac;
    REQUIRE(guest.SendWifi(packet));
    REQUIRE(WaitFor([&] {
        std::lock_guard lock(mutex);
        return got_guest.size() == 1 && got_host.size() == 1;
    }));
    {
        std::lock_guard lock(mutex);
        CHECK(got_guest[0].data == std::vector<u8>(1400, 0x5A));
        CHECK(got_host[0].data == std::vector<u8>(64, 0xA5));
    }
    if (udp) {
        CHECK(server.udp_relayed() > 0);
    } else {
        CHECK(!hubs::GetJoinStatus().udp);
        CHECK(server.udp_relayed() == 0);
    }

    // leaving the room closes the tunnel, and the room sees the player leave
    guest.Leave();
    REQUIRE(WaitFor([] { return hubs::GetJoinStatus().state == hubs::JoinState::Idle; }));
    CHECK(hubs::GetJoinStatus().error == hubs::JoinError::None);
    REQUIRE(WaitFor([&] { return host_client.GetStatus().members.size() == 1; }, 15000));
    REQUIRE(WaitFor([] { return hubs::GetHostStatus().links == 0; }));

    // a password the room asks for: the wrong one is refused by the room, through the tunnel
    hubs::StopHosting();
    REQUIRE(WaitFor([&] { return server.hubs() == 0; }));
    host_client.Leave();
    room.Close();
    open.password = "secret";
    REQUIRE(room.Open(open, &error));
    own.password = "secret";
    REQUIRE(host_client.Join(own, &error));
    REQUIRE(host_client.WaitJoined(5000));
    listing.password = true;
    REQUIRE(hubs::Host(listing, &error));
    REQUIRE(WaitFor([] { return hubs::GetHostStatus().state == hubs::ServerState::Online; }));
    list = server.List();
    REQUIRE(list.hubs.size() == 1);
    CHECK(list.hubs[0].password);
    join.hub_id = list.hubs[0].id;
    join.room.password = "wrong";
    REQUIRE(hubs::Join(join, &error));
    REQUIRE(WaitFor([&] { return guest.GetStatus().error == rooms::Error::WrongPassword; }));
    REQUIRE(WaitFor([] { return hubs::GetJoinStatus().state == hubs::JoinState::Idle; }));
    join.room.password = "secret";
    REQUIRE(hubs::Join(join, &error));
    REQUIRE(WaitFor([&] { return InRoom(guest); }));

    // the host closes its hub: the player's tunnel ends, and so does its stay in the room
    hubs::StopHosting();
    REQUIRE(WaitFor([] { return hubs::GetJoinStatus().state == hubs::JoinState::Idle; }));
    CHECK(hubs::GetJoinStatus().error == hubs::JoinError::Lost);
    REQUIRE(WaitFor([&] { return !InRoom(guest); }));
    host_client.Leave();
    room.Close();
    EndHubs();
}

TEST_CASE("hubs: a hub end to end, by UDP through the server's relay") {
    HubEndToEnd(true);
}

TEST_CASE("hubs: a hub end to end through the TCP link (a server or network that passes no UDP)") {
    HubEndToEnd(false);
}

TEST_CASE("hubs: joining a hub that is gone, and a host that never answers") {
    StandInServer server;
    REQUIRE(server.ok());
    rooms::Client guest;
    hubs::JoinRequest join;
    join.server = server.address();
    join.program_id = rooms::kMh4uUsa;
    join.hub_id = 12345;
    join.room = Member("Hunter Two", 2);
    join.client = &guest;
    std::string error;
    REQUIRE(hubs::Join(join, &error));
    REQUIRE(WaitFor([] { return hubs::GetJoinStatus().state == hubs::JoinState::Idle; }));
    CHECK(hubs::GetJoinStatus().error == hubs::JoinError::HubGone);
    CHECK(guest.GetState() == rooms::State::Idle);

    // a game this module has no hubs for
    join.program_id = 0x000400000011D700;
    CHECK(!hubs::Join(join, &error));
    hubs::HostRequest host;
    host.server = server.address();
    host.program_id = 0x000400000011D700;
    host.room_port = 1;
    CHECK(!hubs::Host(host, &error));
    EndHubs();
}
