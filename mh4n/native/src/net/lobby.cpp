// Derived from Azahar src/web_service/announce_room_json.cpp at 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6
// (GPL-2.0-or-later: it says "GPLv2 or any later version"): the names of a room list's fields.
// Copyright 2017 Citra Emulator Project
//
// A lobby's list of rooms (mh4n/rooms.h): GET <lobby>/lobby over plain HTTP, and its JSON, as the port's app reads it
// (port/app/launcher/lobby.cpp: the fields, MH4U's rooms first, the other region's MH4U named with its region). The
// field names are those of Azahar's web API (name, description, owner,
// address, port, preferredGameName, preferredGameId, maxPlayers, hasPassword, players[].nickname), which
// server/lobby/mh4u_lobby.py serves. The HTTP client and the JSON parser are small ones of our own: no TLS (Azahar's
// web client has none either, see docs/SERVER.md, HTTPS).
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <enet/enet.h>

#include "mh4n/log.h"
#include "mh4n/rooms.h"
#include "net_internal.h"

namespace mh4n::rooms {

namespace {

constexpr const char* kLog = "Network.Lobby";
constexpr std::size_t kMaxBody = 8 << 20;

// ---- a small JSON reader ----------------------------------------------------------------------------------------

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool boolean = false;
    double number = 0;
    bool integer = false;  // a number without a fraction or exponent, not negative
    u64 unsigned_value = 0;
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> members;

    const Json* Find(const char* key) const {
        for (const auto& [name, value] : members) {
            if (name == key) {
                return &value;
            }
        }
        return nullptr;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : s_(text) {}

    bool Parse(Json* out) {
        if (!Value(out, 0)) {
            return false;
        }
        Space();
        return pos_ == s_.size();
    }

private:
    void Space() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) {
            ++pos_;
        }
    }
    bool Literal(const char* word) {
        const std::size_t n = std::strlen(word);
        if (s_.compare(pos_, n, word) != 0) {
            return false;
        }
        pos_ += n;
        return true;
    }
    static void PutUtf8(std::string& out, u32 cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    bool Hex4(u32* out) {
        if (pos_ + 4 > s_.size()) {
            return false;
        }
        u32 v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') {
                v |= static_cast<u32>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                v |= static_cast<u32>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                v |= static_cast<u32>(c - 'A' + 10);
            } else {
                return false;
            }
        }
        *out = v;
        return true;
    }
    bool String(std::string* out) {
        if (pos_ >= s_.size() || s_[pos_] != '"') {
            return false;
        }
        ++pos_;
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"') {
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                return false;
            }
            if (c != '\\') {
                out->push_back(c);
                continue;
            }
            if (pos_ >= s_.size()) {
                return false;
            }
            const char e = s_[pos_++];
            switch (e) {
            case '"': out->push_back('"'); break;
            case '\\': out->push_back('\\'); break;
            case '/': out->push_back('/'); break;
            case 'b': out->push_back('\b'); break;
            case 'f': out->push_back('\f'); break;
            case 'n': out->push_back('\n'); break;
            case 'r': out->push_back('\r'); break;
            case 't': out->push_back('\t'); break;
            case 'u': {
                u32 cp = 0;
                if (!Hex4(&cp)) {
                    return false;
                }
                if (cp >= 0xD800 && cp < 0xDC00 && pos_ + 6 <= s_.size() && s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
                    pos_ += 2;
                    u32 low = 0;
                    if (!Hex4(&low)) {
                        return false;
                    }
                    if (low >= 0xDC00 && low < 0xE000) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else {
                        PutUtf8(*out, 0xFFFD);
                        cp = low;
                    }
                } else if (cp >= 0xD800 && cp < 0xE000) {
                    cp = 0xFFFD;  // an unpaired surrogate
                }
                PutUtf8(*out, cp);
                break;
            }
            default: return false;
            }
        }
        return false;
    }
    bool Number(Json* out) {
        const std::size_t start = pos_;
        bool plain = true;
        if (pos_ < s_.size() && s_[pos_] == '-') {
            plain = false;
            ++pos_;
        }
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if (c >= '0' && c <= '9') {
                ++pos_;
            } else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                plain = false;
                ++pos_;
            } else {
                break;
            }
        }
        if (pos_ == start) {
            return false;
        }
        const std::string text = s_.substr(start, pos_ - start);
        out->type = Json::Type::Number;
        char* end = nullptr;
        out->number = std::strtod(text.c_str(), &end);
        if (end == nullptr || *end != '\0') {
            return false;
        }
        if (plain && text.size() <= 20) {
            u64 v = 0;
            bool fits = true;
            for (const char c : text) {
                const u64 digit = static_cast<u64>(c - '0');
                if (v > (~u64{0} - digit) / 10) {
                    fits = false;
                    break;
                }
                v = v * 10 + digit;
            }
            out->integer = fits;
            out->unsigned_value = v;
        }
        return true;
    }
    bool Value(Json* out, int depth) {
        if (depth > 64) {
            return false;
        }
        Space();
        if (pos_ >= s_.size()) {
            return false;
        }
        const char c = s_[pos_];
        if (c == '{') {
            ++pos_;
            out->type = Json::Type::Object;
            Space();
            if (pos_ < s_.size() && s_[pos_] == '}') {
                ++pos_;
                return true;
            }
            while (true) {
                Space();
                std::string key;
                if (!String(&key)) {
                    return false;
                }
                Space();
                if (pos_ >= s_.size() || s_[pos_] != ':') {
                    return false;
                }
                ++pos_;
                Json value;
                if (!Value(&value, depth + 1)) {
                    return false;
                }
                out->members.emplace_back(std::move(key), std::move(value));
                Space();
                if (pos_ < s_.size() && s_[pos_] == ',') {
                    ++pos_;
                    continue;
                }
                if (pos_ < s_.size() && s_[pos_] == '}') {
                    ++pos_;
                    return true;
                }
                return false;
            }
        }
        if (c == '[') {
            ++pos_;
            out->type = Json::Type::Array;
            Space();
            if (pos_ < s_.size() && s_[pos_] == ']') {
                ++pos_;
                return true;
            }
            while (true) {
                Json value;
                if (!Value(&value, depth + 1)) {
                    return false;
                }
                out->items.push_back(std::move(value));
                Space();
                if (pos_ < s_.size() && s_[pos_] == ',') {
                    ++pos_;
                    continue;
                }
                if (pos_ < s_.size() && s_[pos_] == ']') {
                    ++pos_;
                    return true;
                }
                return false;
            }
        }
        if (c == '"') {
            out->type = Json::Type::String;
            return String(&out->text);
        }
        if (Literal("true")) {
            out->type = Json::Type::Bool;
            out->boolean = true;
            return true;
        }
        if (Literal("false")) {
            out->type = Json::Type::Bool;
            return true;
        }
        if (Literal("null")) {
            out->type = Json::Type::Null;
            return true;
        }
        return Number(out);
    }

    const std::string& s_;
    std::size_t pos_ = 0;
};

std::string Text(const Json& object, const char* key) {
    const Json* v = object.Find(key);
    return v != nullptr && v->type == Json::Type::String ? v->text : std::string();
}

u64 Number(const Json& object, const char* key) {
    const Json* v = object.Find(key);
    return v != nullptr && v->type == Json::Type::Number && v->integer ? v->unsigned_value : 0;
}

std::string Folded(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// ---- a small HTTP client ------------------------------------------------------------------------------------------

struct Url {
    std::string host;
    u16 port = 80;
};

bool ParseUrl(const std::string& url, Url* out, std::string* error) {
    const auto scheme_end = url.find("://");
    const std::string scheme = Folded(url.substr(0, scheme_end));
    if (scheme == "https") {
        *error = "This build speaks plain HTTP only (as Azahar does): use the lobby's http:// address.";
        return false;
    }
    if (scheme != "http") {
        *error = "\"" + url + "\" isn't a lobby address (http://HOST:PORT).";
        return false;
    }
    std::string rest = url.substr(scheme_end + 3);
    rest = rest.substr(0, rest.find('/'));
    const auto colon = rest.rfind(':');
    out->host = rest.substr(0, colon);
    if (colon != std::string::npos) {
        const std::string digits = rest.substr(colon + 1);
        if (digits.empty() || digits.size() > 5 ||
            !std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; }) ||
            std::stoul(digits) == 0 || std::stoul(digits) > 65535) {
            *error = "\"" + url + "\" has a bad port.";
            return false;
        }
        out->port = static_cast<u16>(std::stoul(digits));
    }
    if (out->host.empty()) {
        *error = "\"" + url + "\" has no host.";
        return false;
    }
    return true;
}

struct SocketCloser {
    ENetSocket socket;
    ~SocketCloser() { enet_socket_destroy(socket); }
};

// GET `path` from `url`'s host. Returns the status code (0 on a network failure, with `*error`).
int HttpGet(const std::string& url, const std::string& path, int timeout_ms, std::string* body, std::string* error) {
    Url target;
    if (!ParseUrl(url, &target, error)) {
        return 0;
    }
    if (!detail::EnsureEnet()) {
        *error = "The network library didn't start.";
        return 0;
    }
    ENetAddress address{};
    if (enet_address_set_host(&address, target.host.c_str()) != 0) {
        *error = "Unknown host " + target.host + ".";
        return 0;
    }
    address.port = target.port;
    const ENetSocket socket = enet_socket_create(ENET_SOCKET_TYPE_STREAM);
    if (socket == ENET_SOCKET_NULL) {
        *error = "No socket.";
        return 0;
    }
    SocketCloser closer{socket};
    enet_socket_set_option(socket, ENET_SOCKOPT_NONBLOCK, 1);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    const auto left = [&deadline] {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())
                            .count();
        return static_cast<enet_uint32>(std::max<long long>(ms, 0));
    };
    if (enet_socket_connect(socket, &address) != 0) {
        *error = "The lobby at " + url + " didn't answer.";
        return 0;
    }
    enet_uint32 wait = ENET_SOCKET_WAIT_SEND;
    if (enet_socket_wait(socket, &wait, left()) != 0 || (wait & ENET_SOCKET_WAIT_SEND) == 0) {
        *error = "The lobby at " + url + " didn't answer.";
        return 0;
    }
    int socket_error = 0;
    if (enet_socket_get_option(socket, ENET_SOCKOPT_ERROR, &socket_error) == 0 && socket_error != 0) {
        *error = "The lobby at " + url + " refused the connection.";
        return 0;
    }
    const std::string request = "GET " + path + " HTTP/1.1\r\nHost: " + target.host + ":" +
                                std::to_string(target.port) +
                                "\r\napi-version: 1\r\nAccept: application/json\r\nUser-Agent: mh4n\r\n"
                                "Connection: close\r\n\r\n";
    std::size_t sent = 0;
    while (sent < request.size()) {
        ENetBuffer buffer;
        buffer.data = const_cast<char*>(request.data() + sent);
        buffer.dataLength = request.size() - sent;
        const int n = enet_socket_send(socket, nullptr, &buffer, 1);
        if (n < 0) {
            *error = "The lobby at " + url + " closed the connection.";
            return 0;
        }
        if (n == 0) {
            wait = ENET_SOCKET_WAIT_SEND;
            if (enet_socket_wait(socket, &wait, left()) != 0 || wait == 0) {
                *error = "The lobby at " + url + " didn't answer in time.";
                return 0;
            }
            continue;
        }
        sent += static_cast<std::size_t>(n);
    }
    std::string response;
    while (response.size() < kMaxBody) {
        wait = ENET_SOCKET_WAIT_RECEIVE;
        if (enet_socket_wait(socket, &wait, left()) != 0) {
            break;
        }
        if (wait == 0) {
            *error = "The lobby at " + url + " didn't answer in time.";
            return 0;
        }
        char chunk[16384];
        ENetBuffer buffer;
        buffer.data = chunk;
        buffer.dataLength = sizeof(chunk);
        const int n = enet_socket_receive(socket, nullptr, &buffer, 1);
        if (n <= 0) {
            break;  // closed (0 after a ready wait) or failed
        }
        response.append(chunk, static_cast<std::size_t>(n));
        // stop early once the body that Content-Length announced is in
        const auto head_end = response.find("\r\n\r\n");
        if (head_end != std::string::npos) {
            const std::string head = Folded(response.substr(0, head_end));
            const auto cl = head.find("\r\ncontent-length:");
            if (cl != std::string::npos) {
                const std::size_t length = std::strtoul(head.c_str() + cl + 17, nullptr, 10);
                if (response.size() >= head_end + 4 + length) {
                    break;
                }
            }
        }
    }
    const auto head_end = response.find("\r\n\r\n");
    if (head_end == std::string::npos || response.compare(0, 5, "HTTP/") != 0) {
        *error = "The lobby at " + url + " gave no HTTP answer.";
        return 0;
    }
    const std::string head = Folded(response.substr(0, head_end));
    const auto space = head.find(' ');
    const int status = space == std::string::npos ? 0 : std::atoi(head.c_str() + space + 1);
    std::string raw = response.substr(head_end + 4);
    if (head.find("\r\ntransfer-encoding: chunked") != std::string::npos) {
        std::string decoded;
        std::size_t pos = 0;
        while (pos < raw.size()) {
            const auto line_end = raw.find("\r\n", pos);
            if (line_end == std::string::npos) {
                break;
            }
            const std::size_t size = std::strtoul(raw.c_str() + pos, nullptr, 16);
            pos = line_end + 2;
            if (size == 0 || pos + size > raw.size()) {
                break;
            }
            decoded.append(raw, pos, size);
            pos += size + 2;
        }
        raw = std::move(decoded);
    } else {
        const auto cl = head.find("\r\ncontent-length:");
        if (cl != std::string::npos) {
            const std::size_t length = std::strtoul(head.c_str() + cl + 17, nullptr, 10);
            if (raw.size() > length) {
                raw.resize(length);
            }
        }
    }
    *body = std::move(raw);
    return status;
}

}  // namespace

std::string NormalizeLobbyUrl(const std::string& typed) {
    std::string url = typed;
    const auto first = url.find_first_not_of(" \t");
    const auto last = url.find_last_not_of(" \t/");
    url = first == std::string::npos ? std::string() : url.substr(first, last - first + 1);
    if (!url.empty() && url.find("://") == std::string::npos) {
        url = "http://" + url;
    }
    return url;
}

LobbyList ParseLobby(const std::string& body, u64 game) {
    LobbyList list;
    Json json;
    JsonParser parser(body);
    const Json* rooms = nullptr;
    if (parser.Parse(&json) && json.type == Json::Type::Object) {
        rooms = json.Find("rooms");
    }
    if (rooms == nullptr || rooms->type != Json::Type::Array) {
        list.error = "The lobby's answer isn't a list of rooms.";
        return list;
    }
    for (const Json& item : rooms->items) {
        if (item.type != Json::Type::Object) {
            continue;
        }
        LobbyRoom room;
        room.name = Text(item, "name");
        room.description = Text(item, "description");
        room.owner = Text(item, "owner");
        room.address = Text(item, "address");
        room.game = Text(item, "preferredGameName");
        room.port = static_cast<u16>(std::min<u64>(Number(item, "port"), 65535));
        room.game_id = Number(item, "preferredGameId");
        room.max_players = static_cast<u32>(std::min<u64>(Number(item, "maxPlayers"), 65535));
        const Json* password = item.Find("hasPassword");
        room.password = password != nullptr && password->type == Json::Type::Bool && password->boolean;
        if (const Json* players = item.Find("players"); players != nullptr && players->type == Json::Type::Array) {
            for (const Json& player : players->items) {
                if (player.type == Json::Type::Object) {
                    room.players.push_back(Text(player, "nickname"));
                }
            }
        }
        room.preferred = room.game_id == game;
        if (!room.preferred && (room.game_id == kMh4uUsa || room.game_id == kMh4uEurope)) {
            room.game = std::string("Monster Hunter 4 Ultimate (") + (room.game_id == kMh4uUsa ? "USA" : "Europe") + ")";
        }
        if (room.address.empty() || room.port == 0) {
            continue;  // nowhere to join
        }
        list.rooms.push_back(std::move(room));
    }
    std::stable_sort(list.rooms.begin(), list.rooms.end(), [](const LobbyRoom& a, const LobbyRoom& b) {
        if (a.preferred != b.preferred) {
            return a.preferred;
        }
        return Folded(a.name) < Folded(b.name);
    });
    list.ok = true;
    return list;
}

LobbyList FetchLobby(const std::string& lobby_url, u64 game, int timeout_ms) {
    LobbyList list;
    const std::string url = NormalizeLobbyUrl(lobby_url);
    if (url.empty()) {
        list.error = "No lobby address.";
        return list;
    }
    std::string body;
    std::string error;
    // Azahar asks for /lobby at the root, whatever path the URL has
    const int status = HttpGet(url, "/lobby", timeout_ms, &body, &error);
    if (status == 0) {
        list.error = error;
        MH4N_LOG(Warning, kLog, "{}", list.error);
        return list;
    }
    if (status != 200) {
        list.error = "The lobby at " + url + " answered with error " + std::to_string(status) + ".";
        MH4N_LOG(Warning, kLog, "{}", list.error);
        return list;
    }
    list = ParseLobby(body, game);
    MH4N_LOG(Info, kLog, "{} rooms in the lobby at {}", list.rooms.size(), url);
    return list;
}

}  // namespace mh4n::rooms
