// mh4u-native's network rooms (App::Multiplayer, Launcher::Lobby) on the native room client and server (N4-01,
// mh4n/rooms.h): the shared UI's calls, with the contract of port/app/multiplayer.h. The runtime's local-wireless
// service talks to the process's room client by itself, so there is nothing here to tie to the game: the console's
// ID (and the MAC address the room gives) are the runtime's business (the `services.ipc.nwm` boot step).
//
// What the Azahar app's multiplayer.cpp does and this does not yet: listing a hosted room in a lobby (the native room
// server is not announced, N4-04), this PC's addresses and public address for the invite, and the traffic counters
// of each member ("heard"). Join and Host return at once, as there: the room client works on its own thread.
#include <algorithm>
#include <chrono>
#include <functional>
#include <mutex>

#include "config.h"
#include "invite.h"
#include "launcher/lobby.h"
#include "mh4n/rooms.h"
#include "multiplayer.h"

namespace App::Multiplayer {

namespace {

namespace rooms = mh4n::rooms;

struct State {
    std::mutex mutex;
    Config* config = nullptr;
    std::string password;     // the room joined's (for the invite)
    bool joined_in_game = false;
    std::string error, hint;  // the last failure, as the player reads it
    bool join_later = false;
    JoinRequest later;
    double later_seconds = 0;
    bool tick_seen = false;
    std::chrono::steady_clock::time_point started;
};

State& S() {
    static State state;
    return state;
}

std::string NicknameLocked(const State& s) {
    return s.config != nullptr ? s.config->nickname : std::string();
}

void Remember(State& s, const std::function<void(Config&)>& change) {
    if (s.config == nullptr) {
        return;
    }
    change(*s.config);
    Config file = App::Load();
    change(file);
    App::Save(file);
}

bool JoinRoom(State& s, const std::string& host, int port, const std::string& password, std::string* error) {
    rooms::JoinRequest request;
    request.host = host;
    request.port = static_cast<u16>(port);
    request.nickname = NicknameLocked(s);
    if (request.nickname.empty()) {
        request.nickname = "Hunter";  // (the console's user name is the runtime's, which this layer does not see)
    }
    request.password = password;
    if (s.config != nullptr) {
        request.token = s.config->token;
    }
    // (an empty console id: the running game's, published at boot; a join before the game boots waits for it)
    if (!rooms::Join(request, error)) {
        return false;
    }
    s.password = password;
    return true;
}

}  // namespace

bool Available() {
    return true;
}

void Init(Config& config) {
    State& s = S();
    std::lock_guard lock(s.mutex);
    s.config = &config;
    s.join_later = false;
    s.tick_seen = false;
}

void Shutdown() {
    State& s = S();
    rooms::Leave();
    rooms::CloseRoom();
    std::lock_guard lock(s.mutex);
    s.config = nullptr;
}

bool StartAtBoot(std::string& error) {
    State& s = S();
    error.clear();
    Config c;
    {
        std::lock_guard lock(s.mutex);
        if (s.config == nullptr) {
            return true;
        }
        c = *s.config;
    }
    if (c.join.empty() && c.host <= 0) {
        return true;
    }
    std::string host = "127.0.0.1", password = c.password;
    int port = c.host;
    if (c.host > 0) {
        rooms::HostRequest request;
        request.port = static_cast<u16>(c.host);
        request.name = c.room_name;
        request.slots = static_cast<u32>(std::clamp(c.room_members, 2, 254));
        request.password = c.password;
        request.host_username = c.nickname;
        if (!rooms::Host(request, &error)) {
            return false;
        }
    } else {
        const auto room = Invite::Parse(c.join);
        if (!room.ok) {
            error = "join = " + c.join + " in mh4u-app.ini: " + room.error;
            return false;
        }
        host = room.host;
        port = room.port;
        password = room.code ? room.password : c.password;
    }
    std::lock_guard lock(s.mutex);
    // The join itself finishes when the game publishes its console ID (the room client waits for it): the menu's
    // status shows how it went.
    if (!JoinRoom(s, host, port, password, &error)) {
        return false;
    }
    const std::string last = c.host > 0 ? "host" : Invite::Format(host, port, password);
    Remember(s, [last](Config& config) { config.last_room = last; });
    return true;
}

bool Join(const JoinRequest& request) {
    State& s = S();
    std::lock_guard lock(s.mutex);
    if (s.config == nullptr) {
        return false;
    }
    if (rooms::IsHosting()) {
        rooms::Leave();
        rooms::CloseRoom();  // one room at a time
    }
    std::string error;
    if (!JoinRoom(s, request.host, request.port, request.password, &error)) {
        s.error = error;
        s.hint.clear();
        return false;
    }
    s.error.clear();
    s.hint.clear();
    s.joined_in_game = true;
    const std::string address = Invite::Address(request.host, request.port);
    const std::string code = Invite::Format(request.host, request.port, request.password);
    const std::string password = request.password;
    Remember(s, [address, password, code](Config& c) {
        c.join = address;
        c.password = password;
        c.host = 0;
        c.last_room = code;
    });
    return true;
}

bool Host(const HostRequest& request) {
    State& s = S();
    std::lock_guard lock(s.mutex);
    if (s.config == nullptr) {
        return false;
    }
    rooms::Leave();
    rooms::CloseRoom();
    rooms::HostRequest room;
    room.port = static_cast<u16>(request.port);
    room.name = request.name;
    room.slots = static_cast<u32>(std::clamp(request.slots, 2, 254));
    room.password = request.password;
    room.host_username = NicknameLocked(s);
    std::string error;
    if (!rooms::Host(room, &error)) {
        s.error = error;
        s.hint = "Another program may be using UDP port " + std::to_string(request.port) + ".";
        return false;
    }
    if (!JoinRoom(s, "127.0.0.1", request.port, request.password, &error)) {
        s.error = error;
        s.hint.clear();
        rooms::CloseRoom();
        return false;
    }
    s.error.clear();
    s.hint.clear();
    s.joined_in_game = true;
    Remember(s, [request](Config& c) {
        c.host = request.port;
        c.room_port = request.port;
        c.room_name = request.name;
        c.room_members = request.slots;
        c.password = request.password;
        c.room_public = request.listed;
        c.join.clear();
        c.last_room = "host";
    });
    return true;
}

bool Leave(bool close) {
    State& s = S();
    std::lock_guard lock(s.mutex);
    if (s.config == nullptr) {
        return false;
    }
    rooms::Leave();
    if (close) {
        rooms::CloseRoom();
    }
    s.joined_in_game = false;
    s.error.clear();
    s.hint.clear();
    Remember(s, [](Config& c) {
        c.join.clear();
        c.host = 0;
    });
    return true;
}

bool Rejoin() {
    Config c;
    {
        State& s = S();
        std::lock_guard lock(s.mutex);
        if (s.config == nullptr) {
            return false;
        }
        c = *s.config;
    }
    if (c.last_room == "host") {
        HostRequest request;
        request.port = c.room_port;
        request.name = c.room_name;
        request.slots = c.room_members;
        request.password = c.password;
        request.listed = c.room_public;
        return Host(request);
    }
    const auto room = Invite::Parse(c.last_room);
    if (!room.ok) {
        return false;
    }
    JoinRequest request;
    request.host = room.host;
    request.port = room.port;
    request.password = room.password;
    return Join(request);
}

std::string LastRoom() {
    State& s = S();
    std::lock_guard lock(s.mutex);
    if (s.config == nullptr) {
        return {};
    }
    const std::string& last = s.config->last_room;
    if (last == "host") {
        return "host \"" + s.config->room_name + "\" on port " + std::to_string(s.config->room_port);
    }
    const auto room = Invite::Parse(last);
    return room.ok ? "join " + Invite::Address(room.host, room.port) : "";
}

void Tick() {
    State& s = S();
    bool join_now = false;
    JoinRequest later;
    {
        std::lock_guard lock(s.mutex);
        const auto now = std::chrono::steady_clock::now();
        if (!s.tick_seen) {
            s.tick_seen = true;
            s.started = now;
        }
        if (s.join_later && now - s.started >= std::chrono::duration<double>(s.later_seconds)) {
            s.join_later = false;
            join_now = true;
            later = s.later;
        }
    }
    if (join_now) {
        Join(later);
    }
}

void JoinLater(const JoinRequest& request, double seconds) {
    State& s = S();
    std::lock_guard lock(s.mutex);
    s.join_later = true;
    s.later = request;
    s.later_seconds = seconds;
}

Status GetStatus() {
    State& s = S();
    const rooms::Status room = rooms::GetStatus();
    std::lock_guard lock(s.mutex);
    Status status;
    switch (room.state) {
    case rooms::State::Idle: status.phase = Phase::Off; break;
    case rooms::State::Joining:
        status.phase = Phase::Busy;
        status.action = (room.waiting_for_console ? "Waiting for the game to join " : "Joining ") +
                        Invite::Address(room.host, room.port) + "...";
        break;
    case rooms::State::Joined:
    case rooms::State::Moderator: status.phase = Phase::InRoom; break;
    }
    status.error = s.error;
    status.hint = s.hint;
    if (status.error.empty() && room.error != rooms::Error::None && room.state == rooms::State::Idle) {
        status.error = rooms::ErrorMessage(room.error);
    }
    status.room_name = room.room.name;
    status.host = room.host;
    status.port = room.port;
    status.password = s.password;
    status.slots = room.room.member_slots;
    status.hosting = room.hosting;
    status.moderator = room.state == rooms::State::Moderator;
    status.nickname = room.nickname;
    status.joined_in_game = s.joined_in_game && status.phase == Phase::InRoom;
    if (room.hosting) {
        status.lobby_note = "Not listed in a lobby (this build's room server isn't announced yet).";
    }
    if (status.phase == Phase::InRoom) {
        for (const rooms::Member& m : room.members) {
            Member member;
            member.nickname = m.nickname;
            member.username = m.username;
            member.game = m.game.name;
            member.you = m.nickname == room.nickname;
            status.members.push_back(std::move(member));
        }
    }
    return status;
}

std::string Nickname() {
    State& s = S();
    std::lock_guard lock(s.mutex);
    return NicknameLocked(s);
}

bool ValidNickname(const std::string& name) {
    return rooms::ValidNickname(name);
}

} // namespace App::Multiplayer

namespace Launcher::Lobby {

namespace {

List FromRooms(const mh4n::rooms::LobbyList& in) {
    List list;
    list.ok = in.ok;
    list.error = in.error;
    for (const mh4n::rooms::LobbyRoom& r : in.rooms) {
        Room room;
        room.name = r.name;
        room.description = r.description;
        room.owner = r.owner;
        room.address = r.address;
        room.game = r.game;
        room.port = r.port;
        room.game_id = r.game_id;
        room.max_players = r.max_players;
        room.password = r.password;
        room.players = r.players;
        room.mh4u = r.preferred;
        list.rooms.push_back(std::move(room));
    }
    return list;
}

}  // namespace

List Fetch(const std::string& lobby_url, u64 game) {
    if (NormalizeUrl(lobby_url).empty()) {
        List list;
        list.error = "No lobby: put its address under Your name and lobby.";
        return list;
    }
    return FromRooms(mh4n::rooms::FetchLobby(lobby_url, game, 10000));
}

List Parse(const std::string& json, u64 game) {
    return FromRooms(mh4n::rooms::ParseLobby(json, game));
}

std::string NormalizeUrl(const std::string& typed) {
    return mh4n::rooms::NormalizeLobbyUrl(typed);
}

} // namespace Launcher::Lobby
