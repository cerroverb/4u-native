// mh4n_room: a network room without a game (N4-01), the native counterpart of Azahar's dedicated room server
// (azahar-room): official Azahar, the port's Azahar build and the native runtime all join it. With --join it is a room
// client instead, for tests: it joins, prints the room's members, its chat and status messages and the Wi-Fi packets it
// gets, and can send a chat line and a Wi-Fi packet.
//
//   mh4n_room [--port 24872] [--name NAME] [--password TEXT] [--slots 16] [--bind ADDR] [--host-nick NICK]
//             [--seconds N] [--ban-list FILE]
//   mh4n_room --join HOST:PORT --nick NAME [--password TEXT] [--console-id HEX] [--chat TEXT] [--send-wifi]
//             [--seconds N] [--lobby URL]
//
// The room runs until `q` and Enter on its standard input (or --seconds pass).
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "mh4n/rooms.h"

namespace {

using namespace mh4n;

int Usage() {
    std::fprintf(stderr,
                 "usage: mh4n_room [--port N] [--name NAME] [--password TEXT] [--slots N] [--bind ADDR]\n"
                 "                 [--host-nick NICK] [--seconds N] [--ban-list FILE]\n"
                 "       mh4n_room --join HOST:PORT --nick NAME [--password TEXT] [--console-id HEX]\n"
                 "                 [--chat TEXT] [--send-wifi] [--seconds N]\n"
                 "       mh4n_room --lobby URL\n");
    return 2;
}

std::string Mac(const rooms::MacAddress& mac) {
    char text[32];
    std::snprintf(text, sizeof text, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return text;
}

std::atomic<bool> g_quit{false};

void WatchInput() {
    std::thread([] {
        // (a closed input, as under a service manager, does not close the room)
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "q" || line == "quit") {
                g_quit = true;
                return;
            }
        }
    }).detach();
}

int RunLobby(const std::string& url) {
    const rooms::LobbyList list = rooms::FetchLobby(url);
    if (!list.ok) {
        std::printf("lobby: %s\n", list.error.c_str());
        return 1;
    }
    for (const rooms::LobbyRoom& room : list.rooms) {
        std::string players;
        for (const std::string& p : room.players) {
            players += (players.empty() ? "" : ", ") + p;
        }
        std::printf("%s%s  %s:%u  %zu/%u  %s  [%s]\n", room.preferred ? "* " : "  ", room.name.c_str(),
                    room.address.c_str(), room.port, room.players.size(), room.max_players, room.game.c_str(),
                    players.c_str());
    }
    return 0;
}

int RunClient(const std::string& target, const std::string& nick, const std::string& password,
              const std::string& console_id, const std::string& chat, bool send_wifi, int seconds) {
    const auto colon = target.rfind(':');
    if (colon == std::string::npos || colon == 0) {
        return Usage();
    }
    rooms::JoinRequest request;
    request.host = target.substr(0, colon);
    request.port = static_cast<u16>(std::atoi(target.c_str() + colon + 1));
    request.nickname = nick;
    request.password = password;
    request.console_id_hash = rooms::ConsoleIdHash(std::strtoull(console_id.c_str(), nullptr, 16));
    rooms::Client client;
    client.SetGameInfo({"Monster Hunter 4 Ultimate", rooms::kMh4uUsa});
    client.AddWifiListener([](const rooms::WifiPacket& packet) {
        std::printf("wifi: type %u from %s to %s, %zu bytes\n", static_cast<unsigned>(packet.type),
                    Mac(packet.transmitter).c_str(), Mac(packet.destination).c_str(), packet.data.size());
        std::fflush(stdout);
    });
    std::string error;
    if (!client.Join(request, &error)) {
        std::printf("join: %s\n", error.c_str());
        return 2;
    }
    if (!client.WaitJoined(10000)) {
        const rooms::Status status = client.GetStatus();
        std::printf("join: failed (%s, %s)\n", rooms::StateName(status.state), rooms::ErrorName(status.error));
        return 1;
    }
    rooms::Status status = client.GetStatus();
    std::printf("joined %s:%u as %s (%s), MAC %s, room \"%s\" (%u slots)\n", request.host.c_str(), request.port,
                nick.c_str(), rooms::StateName(status.state), Mac(status.mac).c_str(), status.room.name.c_str(),
                status.room.member_slots);
    std::fflush(stdout);
    if (!chat.empty()) {
        client.SendChat(chat);
    }
    if (send_wifi) {
        rooms::WifiPacket packet;
        packet.type = rooms::WifiPacket::Type::Data;
        packet.channel = 11;
        packet.destination = rooms::kBroadcastMac;
        packet.data = {1, 2, 3, 4};
        client.SendWifi(packet);
    }
    u64 shown = 0;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < end && !g_quit.load()) {
        for (const rooms::Event& event : client.TakeEvents()) {
            switch (event.kind) {
            case rooms::Event::Kind::Chat: std::printf("chat: %s: %s\n", event.nickname.c_str(), event.message.c_str()); break;
            case rooms::Event::Kind::MemberJoined: std::printf("status: %s joined\n", event.nickname.c_str()); break;
            case rooms::Event::Kind::MemberLeft: std::printf("status: %s left\n", event.nickname.c_str()); break;
            case rooms::Event::Kind::Failed: std::printf("error: %s\n", rooms::ErrorName(event.error)); break;
            case rooms::Event::Kind::StateChanged: std::printf("state: %s\n", rooms::StateName(event.state)); break;
            default: break;
            }
        }
        status = client.GetStatus();
        if (status.revision != shown) {
            shown = status.revision;
            std::string list;
            for (const rooms::Member& m : status.members) {
                list += (list.empty() ? "" : ", ") + m.nickname + " [" + Mac(m.mac) + " " + m.game.name + "]";
            }
            std::printf("members: %s\n", list.c_str());
        }
        std::fflush(stdout);
        if (status.state == rooms::State::Idle) {
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    client.Leave();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    rooms::HostRequest room;
    room.name = "MH4U Gathering Hall";
    room.slots = 16;
    std::string join, nick, password, console_id = "1", chat, lobby, ban_file;
    bool send_wifi = false;
    int seconds = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::exit(Usage());
            }
            return argv[++i];
        };
        if (arg == "--port") {
            room.port = static_cast<u16>(std::atoi(next().c_str()));
        } else if (arg == "--name") {
            room.name = next();
        } else if (arg == "--password") {
            password = next();
        } else if (arg == "--slots") {
            room.slots = static_cast<u32>(std::atoi(next().c_str()));
        } else if (arg == "--bind") {
            room.bind_address = next();
        } else if (arg == "--host-nick") {
            room.host_username = next();
        } else if (arg == "--seconds") {
            seconds = std::atoi(next().c_str());
        } else if (arg == "--ban-list") {
            ban_file = next();
        } else if (arg == "--join") {
            join = next();
        } else if (arg == "--nick") {
            nick = next();
        } else if (arg == "--console-id") {
            console_id = next();
        } else if (arg == "--chat") {
            chat = next();
        } else if (arg == "--send-wifi") {
            send_wifi = true;
        } else if (arg == "--lobby") {
            lobby = next();
        } else {
            return Usage();
        }
    }
    if (!lobby.empty()) {
        return RunLobby(lobby);
    }
    WatchInput();
    if (!join.empty()) {
        return RunClient(join, nick, password, console_id, chat, send_wifi, seconds > 0 ? seconds : 3600 * 24);
    }
    room.password = password;
    if (!ban_file.empty()) {
        // one entry per line: "user NAME" or "ip ADDRESS"
        std::ifstream in(ban_file);
        std::string kind, value;
        while (in >> kind >> value) {
            (kind == "user" ? room.banned_usernames : room.banned_addresses).push_back(value);
        }
    }
    std::string error;
    if (!rooms::Host(room, &error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("room \"%s\" open on UDP port %u, %u members at most%s; q and Enter closes it\n", room.name.c_str(),
                room.port, room.slots, room.password.empty() ? "" : ", with a password");
    std::fflush(stdout);
    std::size_t last_count = static_cast<std::size_t>(-1);
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds > 0 ? seconds : 3600 * 24 * 365);
    while (!g_quit.load() && std::chrono::steady_clock::now() < end) {
        const std::vector<rooms::Member> members = rooms::DefaultServer().Members();
        if (members.size() != last_count) {
            last_count = members.size();
            std::string list;
            for (const rooms::Member& m : members) {
                list += (list.empty() ? "" : ", ") + m.nickname + " [" + Mac(m.mac) + " " + m.game.name + "]";
            }
            std::printf("members (%zu): %s\n", members.size(), list.c_str());
            std::fflush(stdout);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (!ban_file.empty()) {
        const auto [users, addresses] = rooms::DefaultServer().BanList();
        std::ofstream out(ban_file);
        for (const std::string& u : users) {
            out << "user " << u << "\n";
        }
        for (const std::string& a : addresses) {
            out << "ip " << a << "\n";
        }
    }
    rooms::CloseRoom();
    return 0;
}
