// Derived from Azahar src/network/room.cpp, src/network/room_member.cpp and src/network/packet.cpp at
// 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-2.0-or-later: each says "GPLv2 or any later version"): the expected
// behaviour of the room protocol (the order of the join checks and their answers, the relay of Wi-Fi packets, chat,
// moderation, the serialization's byte order) is the one those sources give.
// Copyright 2017-2025 Citra Emulator Project / Azahar Emulator Project (room.cpp)
// Copyright 2017 Citra Emulator Project (room_member.cpp)
// Copyright 2017-2024 Citra Emulator Project / Azahar Emulator Project (packet.cpp)
//
// Tests of N4-01: the room client and the room server of native/src/net, in one process over the loopback interface,
// the packet serialization, the console ID hash and a lobby's room list. Everything is synthetic.
#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../src/net/packet.h"
#include "mh4n/rooms.h"

namespace {

using namespace mh4n;
using namespace mh4n::rooms;

// A port of its own for each test case, so a case never meets the last one's goodbye.
u16 NextPort() {
    static std::atomic<u16> port{26100};
    return port++;
}

template <class Pred>
bool WaitFor(Pred pred, int ms = 5000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

JoinRequest Request(u16 port, const std::string& nick, u64 console) {
    JoinRequest request;
    request.host = "127.0.0.1";
    request.port = port;
    request.nickname = nick;
    request.console_id_hash = ConsoleIdHash(console);
    return request;
}

HostRequest Room(u16 port, u32 slots = 4) {
    HostRequest room;
    room.port = port;
    room.name = "Test hall";
    room.slots = slots;
    return room;
}

}  // namespace

TEST_CASE("packet: network byte order, strings and vectors as Azahar's Packet") {
    detail::Packet packet;
    packet << u8{0x12} << u16{0x3456} << u32{0x789ABCDE} << u64{0x0102030405060708} << std::string("ab")
           << std::array<u8, 3>{9, 8, 7} << std::vector<u8>{5, 6};
    const std::vector<u8> expected = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 1, 2, 3, 4, 5, 6, 7, 8,
                                      0, 0, 0, 2, 'a', 'b', 9, 8, 7, 0, 0, 0, 2, 5, 6};
    REQUIRE(packet.Size() == expected.size());
    CHECK(std::vector<u8>(packet.Data(), packet.Data() + packet.Size()) == expected);

    detail::Packet in(packet.Data(), packet.Size());
    u8 a = 0;
    u16 b = 0;
    u32 c = 0;
    u64 d = 0;
    std::string e;
    std::array<u8, 3> f{};
    std::vector<u8> g;
    in >> a >> b >> c >> d >> e >> f >> g;
    CHECK(static_cast<bool>(in));
    CHECK(a == 0x12);
    CHECK(b == 0x3456);
    CHECK(c == 0x789ABCDE);
    CHECK(d == 0x0102030405060708);
    CHECK(e == "ab");
    CHECK(f == std::array<u8, 3>{9, 8, 7});
    CHECK(g == std::vector<u8>{5, 6});
    CHECK(in.EndOfPacket());
    u32 past = 7;
    in >> past;
    CHECK(!in);
    CHECK(past == 0);
}

TEST_CASE("packet: a length past the end reads nothing and allocates nothing") {
    const u8 bytes[] = {0xFF, 0xFF, 0xFF, 0xF0, 'x'};
    detail::Packet in(bytes, sizeof(bytes));
    std::vector<u8> v;
    in >> v;
    CHECK(!in);
    CHECK(v.empty());
    detail::Packet again(bytes, sizeof(bytes));
    std::vector<std::string> list;
    again >> list;
    CHECK(!again);
    CHECK(list.empty());
}

TEST_CASE("console id hash: the lowercase SHA-256 of the 8 little-endian bytes") {
    // SHA-256 of eight zero bytes
    CHECK(ConsoleIdHash(0) == "af5570f5a1810b7af78caf4bc70a660f0df51e42baf91d4de5b2328de0e83dfc");
    CHECK(ConsoleIdHash(1).size() == 64);
    CHECK(ConsoleIdHash(1) != ConsoleIdHash(2));
}

TEST_CASE("nicknames: Azahar's rule") {
    CHECK(ValidNickname("Hunter One"));
    CHECK(ValidNickname("a.b_c-d"));
    CHECK(!ValidNickname("abc"));
    CHECK(!ValidNickname("123456789012345678901"));
    CHECK(!ValidNickname("hunter!"));
    CHECK(!ValidNickname("hünter"));
}

TEST_CASE("room: two members join, see each other, chat and relay Wi-Fi packets") {
    const u16 port = NextPort();
    Server server;
    std::string error;
    REQUIRE(server.Open(Room(port), &error));
    CHECK(server.IsOpen());

    Client one;
    Client two;
    std::mutex mutex;
    std::vector<WifiPacket> got_one, got_two;
    one.AddWifiListener([&](const WifiPacket& p) {
        std::lock_guard lock(mutex);
        got_one.push_back(p);
    });
    two.AddWifiListener([&](const WifiPacket& p) {
        std::lock_guard lock(mutex);
        got_two.push_back(p);
    });
    one.SetGameInfo({"Monster Hunter 4 Ultimate", kMh4uUsa});
    REQUIRE(one.Join(Request(port, "Hunter One", 1), &error));
    REQUIRE(one.WaitJoined(5000));
    REQUIRE(two.Join(Request(port, "Hunter Two", 2), &error));
    REQUIRE(two.WaitJoined(5000));

    REQUIRE(WaitFor([&] { return one.GetStatus().members.size() == 2 && two.GetStatus().members.size() == 2; }));
    const Status s1 = one.GetStatus();
    const Status s2 = two.GetStatus();
    CHECK(s1.state == State::Joined);
    CHECK(s1.room.name == "Test hall");
    CHECK(s1.room.member_slots == 4);
    CHECK(s1.room.port == port);
    CHECK(s1.room.preferred_game == "Monster Hunter 4 Ultimate");
    CHECK(s1.members[0].nickname == "Hunter One");
    CHECK(s1.members[0].game.name == "Monster Hunter 4 Ultimate");
    CHECK(s1.members[0].game.id == kMh4uUsa);
    CHECK(s1.members[1].nickname == "Hunter Two");
    // the room gives MAC addresses with the Great N's OUI, one each
    CHECK(s1.mac[0] == 0x00);
    CHECK(s1.mac[1] == 0x1F);
    CHECK(s1.mac[2] == 0x32);
    CHECK(s1.mac != s2.mac);
    CHECK(one.RoomMac() == s1.mac);
    CHECK(server.Members().size() == 2);

    // chat goes to the others, cut to 500 bytes
    REQUIRE(one.SendChat("hello" + std::string(600, '!')));
    std::vector<Event> events;
    REQUIRE(WaitFor([&] {
        for (Event& e : two.TakeEvents()) {
            events.push_back(std::move(e));
        }
        return std::any_of(events.begin(), events.end(), [](const Event& e) { return e.kind == Event::Kind::Chat; });
    }));
    const auto chat = std::find_if(events.begin(), events.end(), [](const Event& e) { return e.kind == Event::Kind::Chat; });
    CHECK(chat->nickname == "Hunter One");
    CHECK(chat->message.size() == kMaxMessageSize);

    // a broadcast reaches the other member, not the sender; a unicast reaches its destination
    WifiPacket packet;
    packet.type = WifiPacket::Type::Beacon;
    packet.channel = 11;
    packet.destination = kBroadcastMac;
    packet.data = {1, 2, 3};
    REQUIRE(one.SendWifi(packet));
    packet.type = WifiPacket::Type::Data;
    packet.destination = s1.mac;
    packet.data = {4, 5};
    REQUIRE(two.SendWifi(packet));
    REQUIRE(WaitFor([&] {
        std::lock_guard lock(mutex);
        return got_two.size() == 1 && got_one.size() == 1;
    }));
    {
        std::lock_guard lock(mutex);
        CHECK(got_two[0].type == WifiPacket::Type::Beacon);
        CHECK(got_two[0].transmitter == s1.mac);
        CHECK(got_two[0].destination == kBroadcastMac);
        CHECK(got_two[0].channel == 11);
        CHECK(got_two[0].data == std::vector<u8>{1, 2, 3});
        CHECK(got_one[0].type == WifiPacket::Type::Data);
        CHECK(got_one[0].transmitter == s2.mac);
        CHECK(got_one[0].data == std::vector<u8>{4, 5});
    }

    // leaving: the other member hears of it
    two.Leave();
    CHECK(two.GetState() == State::Idle);
    CHECK(!two.SendWifi(packet));
    REQUIRE(WaitFor([&] { return one.GetStatus().members.size() == 1; }));
    one.Leave();
    server.Close();
    CHECK(!server.IsOpen());
}

TEST_CASE("room: the join checks answer as Azahar's room does") {
    const u16 port = NextPort();
    Server server;
    HostRequest room = Room(port, 2);
    room.password = "secret";
    REQUIRE(server.Open(room));
    Client one, two, three;

    JoinRequest request = Request(port, "Hunter One", 1);
    REQUIRE(one.Join(request));
    CHECK(!one.WaitJoined(5000));
    CHECK(one.GetStatus().error == Error::WrongPassword);

    request.password = "secret";
    REQUIRE(one.Join(request));
    REQUIRE(one.WaitJoined(5000));

    JoinRequest same_name = Request(port, "Hunter One", 2);
    same_name.password = "secret";
    REQUIRE(two.Join(same_name));
    CHECK(!two.WaitJoined(5000));
    CHECK(two.GetStatus().error == Error::NameCollision);

    JoinRequest same_console = Request(port, "Hunter Two", 1);
    same_console.password = "secret";
    REQUIRE(two.Join(same_console));
    CHECK(!two.WaitJoined(5000));
    CHECK(two.GetStatus().error == Error::ConsoleIdCollision);

    JoinRequest same_mac = Request(port, "Hunter Two", 2);
    same_mac.password = "secret";
    same_mac.preferred_mac = one.GetStatus().mac;
    REQUIRE(two.Join(same_mac));
    CHECK(!two.WaitJoined(5000));
    CHECK(two.GetStatus().error == Error::MacCollision);

    JoinRequest fine = Request(port, "Hunter Two", 2);
    fine.password = "secret";
    fine.preferred_mac = MacAddress{0x02, 0, 0, 0, 0, 0x22};
    REQUIRE(two.Join(fine));
    REQUIRE(two.WaitJoined(5000));
    CHECK(two.GetStatus().mac == MacAddress{0x02, 0, 0, 0, 0, 0x22});

    JoinRequest full = Request(port, "Hunter Three", 3);
    full.password = "secret";
    REQUIRE(three.Join(full));
    CHECK(!three.WaitJoined(5000));
    CHECK(three.GetStatus().error == Error::RoomIsFull);

    // a request that cannot work is refused at once
    std::string error;
    CHECK(!three.Join(Request(port, "no", 3), &error));
    CHECK(!error.empty());
}

TEST_CASE("room: nobody at the address answers CouldNotConnect") {
    Client client;
    REQUIRE(client.Join(Request(NextPort(), "Hunter One", 1)));
    CHECK(!client.WaitJoined(8000));
    CHECK(client.GetStatus().error == Error::CouldNotConnect);
}

TEST_CASE("room: the host's nickname from this machine is a moderator, and kicks") {
    const u16 port = NextPort();
    Server server;
    HostRequest room = Room(port);
    room.host_username = "Hall Owner";
    REQUIRE(server.Open(room));
    Client owner, guest;
    REQUIRE(owner.Join(Request(port, "Hall Owner", 1)));
    REQUIRE(owner.WaitJoined(5000));
    CHECK(owner.GetState() == State::Moderator);
    REQUIRE(guest.Join(Request(port, "Some Guest", 2)));
    REQUIRE(guest.WaitJoined(5000));
    CHECK(guest.GetState() == State::Joined);

    // a guest may not kick
    REQUIRE(guest.Kick("Hall Owner"));
    std::vector<Event> events;
    REQUIRE(WaitFor([&] {
        for (Event& e : guest.TakeEvents()) {
            events.push_back(std::move(e));
        }
        return std::any_of(events.begin(), events.end(), [](const Event& e) {
            return e.kind == Event::Kind::Failed && e.error == Error::PermissionDenied;
        });
    }));
    CHECK(guest.GetState() == State::Joined);

    REQUIRE(owner.Kick("Some Guest"));
    REQUIRE(WaitFor([&] { return guest.GetState() == State::Idle; }));
    CHECK(guest.GetStatus().error == Error::HostKicked);
    REQUIRE(WaitFor([&] { return owner.GetStatus().members.size() == 1; }));
}

TEST_CASE("room: closing the room drops its members with LostConnection") {
    const u16 port = NextPort();
    Server server;
    REQUIRE(server.Open(Room(port)));
    Client client;
    REQUIRE(client.Join(Request(port, "Hunter One", 1)));
    REQUIRE(client.WaitJoined(5000));
    server.Close();
    REQUIRE(WaitFor([&] { return client.GetState() == State::Idle; }));
    CHECK(client.GetStatus().error == Error::LostConnection);
}

TEST_CASE("room: a join without a console id waits for the game to publish it") {
    const u16 port = NextPort();
    Server server;
    REQUIRE(server.Open(Room(port)));
    Client client;
    JoinRequest request = Request(port, "Hunter One", 1);
    request.console_id_hash.clear();
    REQUIRE(client.Join(request));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(client.GetState() == State::Joining);
    CHECK(client.GetStatus().waiting_for_console);
    CHECK(server.Members().empty());
    client.PublishConsole(ConsoleIdHash(5));
    REQUIRE(client.WaitJoined(5000));
    CHECK(!client.GetStatus().waiting_for_console);
}

TEST_CASE("room: a second room on the same port is refused") {
    const u16 port = NextPort();
    Server a, b;
    REQUIRE(a.Open(Room(port)));
    std::string error;
    CHECK(!b.Open(Room(port), &error));
    CHECK(!error.empty());
    CHECK(!a.Open(Room(NextPort()), &error));  // one room per server
}

TEST_CASE("lobby: Azahar's room list, MH4U's first, rooms without an address left out") {
    const std::string json = R"({"rooms":[
        {"name":"zeta","address":"1.2.3.4","port":24872,"preferredGameName":"Other","preferredGameId":5,
         "maxPlayers":4,"hasPassword":false,"players":[]},
        {"name":"Alpha é 😀","description":"d","owner":"o","address":"5.6.7.8","port":24873,
         "preferredGameName":"Monster Hunter 4 Ultimate","preferredGameId":1125899908047616,"maxPlayers":16,
         "netVersion":4,"hasPassword":true,"players":[{"nickname":"Hunter One","gameName":"x","gameId":0}]},
        {"name":"europe","address":"9.9.9.9","port":1,"preferredGameId":1125899908047104,"maxPlayers":4},
        {"name":"nowhere","port":24872,"preferredGameId":1125899908047616}
    ]})";
    const LobbyList list = ParseLobby(json, kMh4uUsa);
    REQUIRE(list.ok);
    REQUIRE(list.rooms.size() == 3);
    CHECK(list.rooms[0].name == "Alpha \xC3\xA9 \xF0\x9F\x98\x80");
    CHECK(list.rooms[0].preferred);
    CHECK(list.rooms[0].password);
    CHECK(list.rooms[0].port == 24873);
    CHECK(list.rooms[0].max_players == 16);
    CHECK(list.rooms[0].game_id == kMh4uUsa);
    CHECK(list.rooms[0].players == std::vector<std::string>{"Hunter One"});
    CHECK(list.rooms[1].name == "europe");
    CHECK(list.rooms[1].game == "Monster Hunter 4 Ultimate (Europe)");
    CHECK(list.rooms[2].name == "zeta");
    CHECK(!ParseLobby("{\"rooms\": 3}").ok);
    CHECK(!ParseLobby("not json").ok);
    CHECK(NormalizeLobbyUrl("  lobby.example.org:24872/ ") == "http://lobby.example.org:24872");
    CHECK(NormalizeLobbyUrl("https://x") == "https://x");
    CHECK(!FetchLobby("https://example.invalid").ok);
}
