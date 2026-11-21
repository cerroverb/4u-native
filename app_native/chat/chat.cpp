// The port's chat: chat.h. The relays' protocol is Nostr's (NIP-01: ["EVENT", event], ["REQ", id, filter...], ["OK",
// ...]), with three ephemeral kinds of the port's own (no other program uses them):
//   KIND_CHANNEL   a line in Global or General: tags t (the chat: mhchat-global, mhchat-mhgu, mhchat-mh4u), n (the
//                  name), g (the game), s (a shared key or room: kind, server or address, key or password); the text
//   KIND_PRESENCE  every minute while connected: t mhchat-presence, c (the friend code), n, g, h (1: in a hub or room),
//                  x hello (just connected: friends answer with theirs). Friends see each other online; a code typed to
//                  add a friend finds its key here
//   KIND_DIRECT    to one player (p: their key), the content encrypted (NIP-44 v2): {"k": "msg" | "req" | "share",
//                  "n": name, "x": text, "s": [kind, a, b]}. req: "I added you as a friend"
// Each event is signed (BIP-340) and checked on arrival, with its id; one older than 5 minutes is dropped.
#include "chat.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iterator>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#endif

#include "chat_crypto.h"
#include "chat_json.h"
#include "chat_ws.h"

namespace chat {
std::string name_locked();  // (the name as others see it: g.mu held)
bool add_key_locked(const std::string &key, const std::string &n, bool accepting);
namespace {

constexpr int KIND_CHANNEL = 25910, KIND_PRESENCE = 25911, KIND_DIRECT = 25912;
constexpr const char *TAG_GLOBAL = "mhchat-global", *TAG_PRESENCE = "mhchat-presence";
constexpr size_t MAX_TEXT = 200, MAX_NAME = 24, KEEP_LINES = 100, KEEP_RECENT = 200;
constexpr int PRESENCE_EVERY = 60, ONLINE_WITHIN = 150, MAX_AGE = 300;
const char *const DEFAULT_RELAYS[] = {"wss://relay.damus.io", "wss://nos.lol", "wss://relay.primal.net"};

int64_t unix_now() { return (int64_t)std::time(nullptr); }
int64_t ms_now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// UTF-8 cut to n bytes on a character's boundary, control characters as spaces, trimmed
std::string clean(const std::string &s, size_t n) {
    std::string o;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!len || i + len > s.size()) { i++; continue; }  // (a broken byte: left out)
        bool ok = true;
        for (size_t k = 1; k < len; k++)
            if (((unsigned char)s[i + k] >> 6) != 2) ok = false;
        if (!ok) { i++; continue; }
        if (o.size() + len > n) break;
        if (len == 1 && (c < 0x20 || c == 0x7f)) o += ' ';
        else o.append(s, i, len);
        i += len;
    }
    size_t a = o.find_first_not_of(' '), b = o.find_last_not_of(' ');
    return a == std::string::npos ? std::string() : o.substr(a, b - a + 1);
}
std::string lower(std::string s) {
    for (char &c : s)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

struct Relay {
    std::string url;
    int h = 0;
    bool subscribed = false;
    int64_t next_try = 0, opened = 0;
    int backoff = 2;
    std::string error;
};
struct Rate {  // what this device said in a chat (the anti-spam's own limits)
    std::deque<int64_t> times;
    std::string last;
    int64_t last_ms = 0;
};
struct Sender {  // what someone said lately (the anti-spam's limits on others)
    std::deque<int64_t> times;
    int64_t silenced_until = 0;
};
struct Online {
    int64_t seen = 0;
    std::string name, game;
    bool in_hub = false;
};

struct State {
    std::mutex mu;
    bool inited = false;
    Port port;
    std::string file;
    // identity
    uint8_t sec[32], pub[32];
    std::string key, code;
    // settings
    bool enabled = false, notice = false, bubble_shown = true, has_bubble = false;
    bool hub = false;  // the hub's or room's chat (off as it comes)
    std::atomic<bool> hub_on{false}, any_on{false};  // (hub, and hub || enabled, read without the lock: hub_chat(), active())
    std::string name;
    std::vector<std::string> relays;
    int overlay = OVERLAY_HUB_FRIENDS;
    std::set<std::string> flags;
    float bx = 0, by = 0, bsize = 1;
    bool has_window = false;  // (1.0.2: the chat window's place and size, fractions of the screen)
    float wx = 0, wy = 0, ww = 1, wh = 1;
    int opacity[OPACITIES] = {60, 75, 90};  // (1.0.2: the chat window's background, lines and field, as they came)
    // people: friends, pending codes ("code:XXXX"), requests, mutes and blocks, by id
    std::map<std::string, Person> people;
    std::map<std::string, Online> online;
    std::map<std::string, std::vector<uint8_t>> conv_keys;
    std::set<std::string> asked;  // (friends asked again this session to add us back)
    // lines
    unsigned serial = 0;
    std::deque<Line> chans[3];
    std::map<std::string, std::deque<Line>> direct;
    std::deque<Line> recent;
    unsigned read[3] = {0, 0, 0};
    std::map<std::string, unsigned> read_direct;
    std::map<std::string, Rate> rates;
    std::map<std::string, Sender> senders;
    // relays
    std::vector<Relay> conns;
    std::deque<std::pair<int64_t, std::string>> out;  // (queued at, ["EVENT", ...])
    std::deque<std::string> seen_order;
    std::unordered_set<std::string> seen;
    int64_t presence_at = 0;
    bool presence_soon = false, hello = true;
    int64_t answered_hello = 0;
    std::string reason;
    // the thread
    std::thread th;
    std::atomic<bool> run{false};
    bool hub_was = false;
    std::string hub_label;
};
State &g = *new State;  // (never destroyed: the relays' thread may still run as the process ends, and a joinable thread's destructor aborts)

void note(const char *fmt, ...) {
    char b[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    if (g.port.log) g.port.log(b);
}

// ---- chat.txt
void save_locked() {
    std::string tmp = g.file + ".tmp";
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f) return;
    fprintf(f, "# The chat's own file (this device only, never sent anywhere): its key, its settings, friends, mutes and blocks.\n"
               "# The key is your identity in the chat (your friend code comes from it): keep it to yourself.\n");
    fprintf(f, "secret\t%s\n", chatc::hex(g.sec, 32).c_str());
    fprintf(f, "enabled\t%d\nhub\t%d\nnotice\t%d\noverlay\t%d\n", (int)g.enabled, (int)g.hub, (int)g.notice, g.overlay);
    fprintf(f, "name\t%s\n", g.name.c_str());
    std::string rl;
    for (auto &r : g.relays) rl += (rl.empty() ? "" : " ") + r;
    fprintf(f, "relays\t%s\n", rl.c_str());
    if (g.has_bubble) fprintf(f, "bubble\t%.4f %.4f %.3f\n", g.bx, g.by, g.bsize);
    fprintf(f, "bubbleshown\t%d\n", (int)g.bubble_shown);
    if (g.has_window) fprintf(f, "window\t%.4f %.4f %.4f %.4f\n", g.wx, g.wy, g.ww, g.wh);
    fprintf(f, "opacity\t%d %d %d\n", g.opacity[OPACITY_WINDOW], g.opacity[OPACITY_LINES], g.opacity[OPACITY_FIELD]);
    for (auto &fl : g.flags) fprintf(f, "flag\t%s\n", fl.c_str());
    for (auto &kv : g.people) {
        const Person &p = kv.second;
        if (p.blocked) fprintf(f, "block\t%s\t%s\n", p.id.c_str(), p.name.c_str());
        else if (p.is_friend) fprintf(f, "friend\t%s\t%s\t%d\n", p.id.c_str(), p.name.c_str(), (int)p.mutual);
        else if (p.pending) fprintf(f, "pending\t%s\t%s\n", p.code.c_str(), p.name.c_str());
        if (p.muted && !p.blocked) fprintf(f, "mute\t%s\t%s\n", p.id.c_str(), p.name.c_str());
    }
    bool ok = fflush(f) == 0;
    ok = fclose(f) == 0 && ok;
    if (!ok) return;
#ifdef _WIN32
    MoveFileExA(tmp.c_str(), g.file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
    rename(tmp.c_str(), g.file.c_str());
#endif
}
Person &person_locked(const std::string &id) {
    Person &p = g.people[id];
    p.id = id;
    if (p.code.empty() && id.size() == 64) {
        uint8_t k[32];
        if (chatc::unhex(id, k, 32)) p.code = chatc::friend_code(k);
    }
    return p;
}
void mirror_locked() {
    g.hub_on = g.hub;
    g.any_on = g.hub || g.enabled;
}
void load_locked() {
    FILE *f = fopen(g.file.c_str(), "rb");
    bool have_key = false, have_hub = false;
    g.relays.clear();
    if (f) {
        char line[1024];
        while (fgets(line, sizeof line, f)) {
            std::string l = line;
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            if (l.empty() || l[0] == '#') continue;
            std::vector<std::string> p;
            size_t at = 0;
            for (;;) {
                size_t tab = l.find('\t', at);
                p.push_back(l.substr(at, tab == std::string::npos ? std::string::npos : tab - at));
                if (tab == std::string::npos) break;
                at = tab + 1;
            }
            const std::string &k = p[0];
            std::string v = p.size() > 1 ? p[1] : "", n = p.size() > 2 ? clean(p[2], MAX_NAME) : "";
            if (k == "secret" && chatc::unhex(v, g.sec, 32) && chatc::public_key(g.sec, g.pub)) have_key = true;
            else if (k == "enabled") g.enabled = v == "1";
            else if (k == "hub") g.hub = v == "1", have_hub = true;
            else if (k == "notice") g.notice = v == "1";
            else if (k == "overlay") g.overlay = std::clamp(atoi(v.c_str()), 0, 2);
            else if (k == "name") g.name = clean(v, MAX_NAME);
            else if (k == "relays") {
                size_t s = 0;
                while (s < v.size()) {
                    size_t e = v.find(' ', s);
                    std::string r = v.substr(s, e == std::string::npos ? std::string::npos : e - s);
                    if (r.rfind("wss://", 0) == 0) g.relays.push_back(r);
                    if (e == std::string::npos) break;
                    s = e + 1;
                }
            } else if (k == "bubble") {
                if (sscanf(v.c_str(), "%f %f %f", &g.bx, &g.by, &g.bsize) == 3) g.has_bubble = true;
            } else if (k == "bubbleshown") g.bubble_shown = v != "0";
            else if (k == "window") {
                if (sscanf(v.c_str(), "%f %f %f %f", &g.wx, &g.wy, &g.ww, &g.wh) == 4 && g.ww > 0 && g.wh > 0) g.has_window = true;
            } else if (k == "opacity") {
                int o[OPACITIES];
                if (sscanf(v.c_str(), "%d %d %d", &o[0], &o[1], &o[2]) == OPACITIES)
                    for (int i = 0; i < OPACITIES; i++) g.opacity[i] = std::clamp(o[i] / 5 * 5, 0, 100);
            }
            else if (k == "flag" && !v.empty()) g.flags.insert(v);
            else if (k == "friend" && v.size() == 64) {
                Person &q = person_locked(lower(v));
                q.is_friend = true, q.name = n, q.mutual = p.size() > 3 && p[3] == "1";
            } else if (k == "pending" && !chatc::normalize_code(v).empty()) {
                Person &q = person_locked("code:" + chatc::normalize_code(v));
                q.pending = true, q.code = chatc::normalize_code(v), q.name = n;
            } else if (k == "mute" && !v.empty()) {
                Person &q = person_locked(v);
                q.muted = true;
                if (q.name.empty()) q.name = n;
            } else if (k == "block" && !v.empty()) {
                Person &q = person_locked(v);
                q.blocked = true, q.is_friend = false;
                if (q.name.empty()) q.name = n;
            }
        }
        fclose(f);
    }
    // (a chat.txt from before the hub's chat had a switch of its own - the test builds of 1.0, where it was on for all: a
    // player who had turned the chat on keeps the hub's on too; anyone else starts with it off)
    if (!have_hub) g.hub = g.enabled;
    mirror_locked();
    if (!have_key) {
        chatc::new_secret(g.sec);
        chatc::public_key(g.sec, g.pub);
    }
    g.key = chatc::hex(g.pub, 32);
    g.code = chatc::friend_code(g.pub);
    if (!have_key) save_locked();
}

// ---- lines
void push_locked(Line l) {
    l.serial = ++g.serial;
    if (!l.at) l.at = unix_now();
    std::deque<Line> &d = l.channel == FRIEND ? g.direct[l.peer] : g.chans[l.channel];
    d.push_back(l);
    if (d.size() > KEEP_LINES) d.pop_front();
    g.recent.push_back(l);
    if (g.recent.size() > KEEP_RECENT) g.recent.pop_front();
    if (l.mine) {  // (what this device says is read)
        if (l.channel == FRIEND) g.read_direct[l.peer] = l.serial;
        else g.read[l.channel] = l.serial;
    }
}
void system_line_locked(int channel, const std::string &peer, const std::string &text) {
    Line l;
    l.channel = channel, l.peer = peer, l.text = text, l.system = true;
    push_locked(l);
}
// Someone said something: false when it is not shown (muted, blocked, silenced, too many)
bool admit_locked(const std::string &from, const std::string &name, int channel, const std::string &peer) {
    auto it = g.people.find(from);
    if (it != g.people.end() && (it->second.blocked || (it->second.muted && channel != FRIEND))) return false;
    if (from.rfind("hub:", 0) != 0 && from.rfind("room:", 0) != 0 && channel == HUB) {  // (a signed hub line: its name too)
        auto byname = g.people.find("hub:" + name);
        if (byname != g.people.end() && (byname->second.blocked || byname->second.muted)) return false;
    }
    Sender &s = g.senders[from];
    int64_t now = ms_now();
    if (now < s.silenced_until) return false;
    while (!s.times.empty() && now - s.times.front() > 20000) s.times.pop_front();
    s.times.push_back(now);
    if (s.times.size() > 6) {  // (more than 6 lines in 20 s: silenced here for 5 minutes)
        s.silenced_until = now + 5 * 60000;
        s.times.clear();
        system_line_locked(channel, peer, "\x01silenced\x01" + name);
        note("[chat] %s silenced for 5 minutes (too many lines)", name.c_str());
        return false;
    }
    if (g.senders.size() > 4000) g.senders.clear();
    return true;
}

// ---- events
std::string tags_json(const std::vector<std::vector<std::string>> &tags) {
    std::string t = "[";
    for (size_t i = 0; i < tags.size(); i++) {
        t += i ? ",[" : "[";
        for (size_t k = 0; k < tags[i].size(); k++) t += (k ? "," : "") + chatj::quote(tags[i][k]);
        t += "]";
    }
    return t + "]";
}
// A signed event, as ["EVENT", {...}] for the relays
std::string event_locked(int kind, const std::vector<std::vector<std::string>> &tags, const std::string &content) {
    int64_t at = unix_now();
    std::string tj = tags_json(tags);
    std::string ser = "[0,\"" + g.key + "\"," + std::to_string(at) + "," + std::to_string(kind) + "," + tj + "," + chatj::quote(content) + "]";
    uint8_t id[32], sig[64];
    chatc::sha256(ser.data(), ser.size(), id);
    if (!chatc::sign(g.sec, id, sig)) return "";
    return "[\"EVENT\",{\"id\":\"" + chatc::hex(id, 32) + "\",\"pubkey\":\"" + g.key + "\",\"created_at\":" + std::to_string(at) +
           ",\"kind\":" + std::to_string(kind) + ",\"tags\":" + tj + ",\"content\":" + chatj::quote(content) + ",\"sig\":\"" +
           chatc::hex(sig, 64) + "\"}]";
}
void queue_locked(const std::string &msg) {
    if (msg.empty()) return;
    g.out.emplace_back(ms_now(), msg);
    if (g.out.size() > 64) g.out.pop_front();
}
std::string game_tag(const std::string &game) { return "mhchat-" + game; }
int open_relays_locked() {
    int n = 0;
    for (auto &r : g.conns) n += r.subscribed;
    return n;
}
void presence_locked() {
    std::string label;
    bool hub = g.port.hub_up && g.port.hub_up(&label);
    std::vector<std::vector<std::string>> tags = {{"t", TAG_PRESENCE}, {"c", g.code}, {"n", name_locked()}, {"g", g.port.game}, {"h", hub ? "1" : "0"}};
    if (g.hello) tags.push_back({"x", "hello"});
    queue_locked(event_locked(KIND_PRESENCE, tags, ""));
    g.hello = false;
    g.presence_at = unix_now();
    g.presence_soon = false;
}
const uint8_t *conv_key_locked(const std::string &peer) {
    auto it = g.conv_keys.find(peer);
    if (it != g.conv_keys.end()) return it->second.data();
    uint8_t their[32], k[32];
    if (!chatc::unhex(peer, their, 32) || !chatc::conversation_key(g.sec, their, k)) return nullptr;
    auto &v = g.conv_keys[peer];
    v.assign(k, k + 32);
    return v.data();
}
bool direct_locked(const std::string &peer, const std::string &kind, const std::string &text, int share, const std::string &a,
                   const std::string &b) {
    const uint8_t *ck = conv_key_locked(peer);
    if (!ck) return false;
    std::string body = "{\"k\":" + chatj::quote(kind) + ",\"n\":" + chatj::quote(name_locked()) + ",\"x\":" + chatj::quote(text);
    if (share) body += ",\"s\":[" + std::to_string(share) + "," + chatj::quote(a) + "," + chatj::quote(b) + "]";
    body += "}";
    std::string sealed;
    if (!chatc::nip44_encrypt(ck, body, sealed)) return false;
    queue_locked(event_locked(KIND_DIRECT, {{"p", peer}}, sealed));
    return true;
}
std::string tag(const chatj::Value &ev, const char *name, size_t index = 1) {
    const chatj::Value *tags = ev.get("tags");
    if (!tags || tags->type != chatj::Value::Array) return "";
    for (auto &t : tags->items)
        if (t.type == chatj::Value::Array && t.items.size() > index && t.items[0].type == chatj::Value::String &&
            t.items[0].s == name && t.items[index].type == chatj::Value::String)
            return t.items[index].s;
    return "";
}
// The event's id and signature: true when it is what its author signed
bool check_event(const chatj::Value &ev, std::string &id_out) {
    std::string id = ev.str("id"), pk = ev.str("pubkey"), sig = ev.str("sig"), content = ev.str("content");
    const chatj::Value *tags = ev.get("tags"), *at = ev.get("created_at"), *kind = ev.get("kind");
    if (id.size() != 64 || pk.size() != 64 || sig.size() != 128 || !tags || tags->type != chatj::Value::Array || !at ||
        at->type != chatj::Value::Number || !kind || kind->type != chatj::Value::Number)
        return false;
    std::vector<std::vector<std::string>> t;
    for (auto &tv : tags->items) {
        if (tv.type != chatj::Value::Array) return false;
        t.emplace_back();
        for (auto &s : tv.items) {
            if (s.type != chatj::Value::String) return false;
            t.back().push_back(s.s);
        }
    }
    std::string ser = "[0,\"" + pk + "\"," + at->s + "," + kind->s + "," + tags_json(t) + "," + chatj::quote(content) + "]";
    uint8_t h[32], want[32], p[32], s[64];
    chatc::sha256(ser.data(), ser.size(), h);
    if (!chatc::unhex(id, want, 32) || memcmp(h, want, 32) || !chatc::unhex(pk, p, 32) || !chatc::unhex(sig, s, 64)) return false;
    if (!chatc::verify(p, h, s)) return false;
    id_out = lower(id);
    return true;
}
void event_received_locked(const chatj::Value &ev) {
    std::string id = lower(ev.str("id"));
    if (id.empty() || g.seen.count(id)) return;  // (the same event from another relay)
    if (!check_event(ev, id)) return;
    g.seen.insert(id);
    g.seen_order.push_back(id);
    if (g.seen_order.size() > 4000) g.seen.erase(g.seen_order.front()), g.seen_order.pop_front();
    std::string from = lower(ev.str("pubkey"));
    int64_t at = ev.integer("created_at"), now = unix_now();
    if (at < now - MAX_AGE || at > now + MAX_AGE || from == g.key) return;
    int kind = (int)ev.integer("kind");
    auto pit = g.people.find(from);
    bool blocked = pit != g.people.end() && pit->second.blocked;
    if (kind == KIND_PRESENCE) {
        if (tag(ev, "t") != TAG_PRESENCE) return;
        uint8_t pk[32];
        chatc::unhex(from, pk, 32);
        std::string code = chatc::friend_code(pk);
        Online &o = g.online[from];
        o.seen = now, o.name = clean(tag(ev, "n"), MAX_NAME), o.game = clean(tag(ev, "g"), 8), o.in_hub = tag(ev, "h") == "1";
        if (g.online.size() > 5000) g.online.clear();
        if (blocked) return;
        auto pending = g.people.find("code:" + code);
        if (pending != g.people.end() && tag(ev, "c") == code) {  // (a code typed to add a friend: now its key is known)
            g.people.erase(pending);
            Person &p = person_locked(from);
            p.is_friend = true, p.name = o.name;
            direct_locked(from, "req", "", SHARE_NONE, "", "");
            g.asked.insert(from);
            system_line_locked(FRIEND, from, "\x01" "added\x01" + o.name);
            save_locked();
            note("[chat] friend %s found", chatc::show_code(code).c_str());
        }
        auto f = g.people.find(from);
        if (f != g.people.end() && f->second.is_friend) {
            if (!o.name.empty() && f->second.name != o.name) f->second.name = o.name, save_locked();
            if (!f->second.mutual && g.asked.insert(from).second) direct_locked(from, "req", "", SHARE_NONE, "", "");  // (once a session)
            if (tag(ev, "x") == "hello" && now - g.answered_hello > 20) g.presence_soon = true, g.answered_hello = now;
        }
        return;
    }
    if (blocked) return;
    if (kind == KIND_CHANNEL) {
        std::string t = tag(ev, "t");
        int ch = t == TAG_GLOBAL ? GLOBAL : t == game_tag(g.port.game) ? GENERAL : -1;
        if (ch < 0) return;
        Line l;
        l.channel = ch, l.from = from, l.at = at;
        l.name = clean(tag(ev, "n"), MAX_NAME);
        if (l.name.empty()) l.name = "?";
        l.text = clean(ev.str("content"), MAX_TEXT);
        l.game = clean(tag(ev, "g"), 8);
        std::string sk = tag(ev, "s");
        if (sk == "1" || sk == "2") l.share = atoi(sk.c_str()), l.share_a = clean(tag(ev, "s", 2), 128), l.share_b = clean(tag(ev, "s", 3), 64);
        if (l.text.empty() && !l.share) return;
        if (!admit_locked(from, l.name, ch, "")) return;
        push_locked(l);
        return;
    }
    if (kind == KIND_DIRECT) {
        if (lower(tag(ev, "p")) != g.key) return;
        const uint8_t *ck = conv_key_locked(from);
        std::string plain;
        if (!ck || !chatc::nip44_decrypt(ck, ev.str("content"), plain)) return;
        chatj::Value m;
        if (!chatj::parse(plain, m) || m.type != chatj::Value::Object) return;
        std::string k = m.str("k"), n = clean(m.str("n"), MAX_NAME);
        if (n.empty()) n = "?";
        Person *p = nullptr;
        auto it = g.people.find(from);
        if (it != g.people.end()) p = &it->second;
        if (k == "acc") {  // (they added us back)
            if (p && p->is_friend && !p->mutual) {
                p->mutual = true;
                system_line_locked(FRIEND, from, "\x01" "accepted\x01" + p->name);
                save_locked();
            }
            return;
        }
        if (k == "req") {
            if (p && p->is_friend) {  // (both added each other)
                if (!p->mutual) p->mutual = true, save_locked();
                direct_locked(from, "acc", "", SHARE_NONE, "", "");
                return;
            }
            if (!admit_locked(from, n, FRIEND, from)) return;
            Person &q = person_locked(from);
            if (!q.request) {
                q.request = true, q.name = n;
                system_line_locked(FRIEND, from, "\x01request\x01" + n);
            }
            return;
        }
        if (k != "msg" && k != "share") return;
        if (!p || !p->is_friend) return;  // (only friends write privately)
        if (!admit_locked(from, n, FRIEND, from)) return;
        Line l;
        l.channel = FRIEND, l.peer = from, l.from = from, l.name = n, l.at = at;
        l.text = clean(m.str("x"), MAX_TEXT);
        const chatj::Value *s = m.get("s");
        if (k == "share" && s && s->type == chatj::Value::Array && s->items.size() == 3 && s->items[0].type == chatj::Value::Number) {
            l.share = (int)s->items[0].n;
            if (l.share != SHARE_KEY && l.share != SHARE_ROOM) l.share = SHARE_NONE;
            l.share_a = s->items[1].type == chatj::Value::String ? clean(s->items[1].s, 128) : "";
            l.share_b = s->items[2].type == chatj::Value::String ? clean(s->items[2].s, 64) : "";
        }
        if (l.text.empty() && !l.share) return;
        if (p->name != n) p->name = n, save_locked();
        push_locked(l);
    }
}
void relay_message_locked(Relay &r, const std::string &msg) {
    chatj::Value v;
    if (!chatj::parse(msg, v) || v.type != chatj::Value::Array || v.items.empty() || v.items[0].type != chatj::Value::String) return;
    const std::string &type = v.items[0].s;
    if (type == "EVENT" && v.items.size() >= 3 && v.items[2].type == chatj::Value::Object) event_received_locked(v.items[2]);
    else if (type == "OK" && v.items.size() >= 4 && v.items[2].type == chatj::Value::Bool && !v.items[2].b) {
        std::string why = v.items[3].type == chatj::Value::String ? v.items[3].s : "";
        g.reason = r.url + ": " + why;
        note("[chat] %s refused an event: %s", r.url.c_str(), why.c_str());
    } else if ((type == "NOTICE" || type == "CLOSED") && v.items.size() >= 2) {
        const chatj::Value &w = v.items[v.items.size() - 1];
        note("[chat] %s %s: %s", r.url.c_str(), type.c_str(), w.type == chatj::Value::String ? w.s.c_str() : "");
    }
}
std::string subscriptions_locked() {  // (each relay: the chats, everyone's presence, what comes to this key)
    std::string since = std::to_string(unix_now() - 60);
    return "[\"REQ\",\"mhchat\",{\"kinds\":[" + std::to_string(KIND_CHANNEL) + "],\"#t\":[\"" + TAG_GLOBAL + "\",\"" + game_tag(g.port.game) +
           "\"],\"since\":" + since + "},{\"kinds\":[" + std::to_string(KIND_PRESENCE) + "],\"#t\":[\"" + TAG_PRESENCE +
           "\"],\"since\":" + since + "},{\"kinds\":[" + std::to_string(KIND_DIRECT) + "],\"#p\":[\"" + g.key + "\"],\"since\":" + since + "}]";
}

// ---- the relays' thread
void relay_thread() {
    std::vector<std::string> urls;
    {
        std::lock_guard<std::mutex> l(g.mu);
        urls = g.relays.empty() ? default_relays() : g.relays;
        g.conns.clear();
        for (auto &u : urls) {
            Relay r;
            r.url = u;
            g.conns.push_back(r);
        }
        g.hello = true;
        g.reason.clear();
    }
    note("[chat] relays on (%d)", (int)urls.size());
    while (g.run) {
        int64_t now = ms_now();
        for (size_t i = 0;; i++) {
            Relay r;
            {
                std::lock_guard<std::mutex> l(g.mu);
                if (i >= g.conns.size()) break;
                r = g.conns[i];
            }
            if (!r.h) {
                if (now >= r.next_try) r.h = chatws::open(r.url), r.opened = now, r.subscribed = false;
            } else {
                int st = chatws::state(r.h);
                if (st == chatws::CLOSED || (st == chatws::CONNECTING && now - r.opened > 20000)) {
                    r.error = chatws::error(r.h);
                    if (r.error.empty()) r.error = st == chatws::CLOSED ? "closed" : "no answer";
                    note("[chat] %s: %s (again in %d s)", r.url.c_str(), r.error.c_str(), r.backoff);
                    chatws::close(r.h);
                    r.h = 0, r.subscribed = false, r.next_try = now + r.backoff * 1000;
                    r.backoff = std::min(r.backoff * 2, 120);
                } else if (st == chatws::OPEN) {
                    if (!r.subscribed) {
                        std::string req;
                        {
                            std::lock_guard<std::mutex> l(g.mu);
                            req = subscriptions_locked();
                        }
                        if (chatws::send(r.h, req)) {
                            r.subscribed = true, r.backoff = 2, r.error.clear();
                            note("[chat] %s connected", r.url.c_str());
                            std::lock_guard<std::mutex> l(g.mu);
                            g.presence_soon = true;
                        }
                    }
                    std::string msg;
                    for (int n = 0; n < 200 && chatws::poll(r.h, msg); n++) {
                        std::lock_guard<std::mutex> l(g.mu);
                        relay_message_locked(r, msg);
                    }
                }
            }
            std::lock_guard<std::mutex> l(g.mu);
            if (i < g.conns.size()) g.conns[i] = r;
        }
        std::vector<std::string> send;
        std::vector<int> open;
        {
            std::lock_guard<std::mutex> l(g.mu);
            for (auto &r : g.conns)
                if (r.subscribed) open.push_back(r.h);
            if (!open.empty() && (g.presence_soon || unix_now() - g.presence_at >= PRESENCE_EVERY)) presence_locked();
            while (!g.out.empty() && now - g.out.front().first > 30000) g.out.pop_front();  // (no relay for 30 s: lost)
            if (!open.empty())
                for (auto &m : g.out) send.push_back(m.second);
            if (!open.empty()) g.out.clear();
        }
        for (auto &m : send)
            for (int h : open) chatws::send(h, m);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::lock_guard<std::mutex> l(g.mu);
    for (auto &r : g.conns)
        if (r.h) chatws::close(r.h);
    g.conns.clear();
    g.out.clear();
    g.online.clear();
    note("[chat] relays off");
}
void start_locked() {
    if (g.run || !chatws::available()) return;
    if (g.th.joinable()) g.th.join();
    g.run = true;
    g.th = std::thread(relay_thread);
}
void stop() {
    g.run = false;
    if (g.th.joinable()) g.th.join();
}

bool rate_ok_locked(int channel, const std::string &peer, const std::string &text, std::string *why) {
    Rate &r = g.rates[std::to_string(channel) + peer];
    int64_t now = ms_now();
    bool pub = channel == GLOBAL || channel == GENERAL;
    int64_t gap = pub ? 4000 : 1000, window = pub ? 60000 : 30000;
    size_t most = pub ? 5 : 12;
    while (!r.times.empty() && now - r.times.front() > window) r.times.pop_front();
    auto wait = [why](int64_t ms) {
        if (why) *why = "\x01wait\x01" + std::to_string((ms + 999) / 1000);
        return false;
    };
    if (now - r.last_ms < gap) return wait(gap - (now - r.last_ms));
    if (r.times.size() >= most) return wait(window - (now - r.times.front()));
    if (!text.empty() && text == r.last && now - r.last_ms < 60000) {
        if (why) *why = "\x01same\x01";
        return false;
    }
    r.times.push_back(now);
    r.last = text, r.last_ms = now;
    return true;
}
bool online_locked(const std::string &key) {
    auto it = g.online.find(key);
    return it != g.online.end() && unix_now() - it->second.seen < ONLINE_WITHIN;
}

}  // namespace

// ---- the API
void init(const Port &port) {
    std::lock_guard<std::mutex> l(g.mu);
    if (g.inited) return;
    g.inited = true;
    g.port = port;
    g.file = port.dir.empty() ? std::string("chat.txt") : port.dir + "/chat.txt";
    chatws::platform_init(port.java_class.c_str());
    load_locked();
    note("[chat] %s: friend code %s, relays %s, hub chat %s", port.app.c_str(), chatc::show_code(g.code).c_str(), g.enabled ? "on" : "off",
         g.hub ? "on" : "off");
    if (g.enabled && g.notice) start_locked();
}
std::string game() {
    std::lock_guard<std::mutex> l(g.mu);
    return g.port.game;
}
void tick() {
    std::string label;
    bool up;
    {
        std::lock_guard<std::mutex> l(g.mu);
        if (!g.inited) return;
        up = g.port.hub_up && g.port.hub_up(&label);
        if (up != g.hub_was) {
            g.hub_was = up;
            if (!up) g.chans[HUB].clear();
            g.presence_soon = true;  // (friends see it at once)
        }
        g.hub_label = label;
    }
}
bool enabled() { std::lock_guard<std::mutex> l(g.mu); return g.enabled; }
void set_enabled(bool on) {
    {
        std::lock_guard<std::mutex> l(g.mu);
        if (g.enabled == on && (g.run || !on)) return;
        g.enabled = on;
        mirror_locked();
        save_locked();
        if (on) {
            start_locked();
            return;
        }
    }
    stop();
}
bool hub_chat() { return g.hub_on; }
void set_hub_chat(bool on) {
    std::lock_guard<std::mutex> l(g.mu);
    if (g.hub == on) return;
    g.hub = on;
    mirror_locked();
    if (!on) g.chans[HUB].clear();  // (and nothing more comes in: hub_line)
    save_locked();
    note("[chat] hub chat %s", on ? "on" : "off");
}
bool active() { return g.any_on; }
bool notice_seen() { std::lock_guard<std::mutex> l(g.mu); return g.notice; }
void set_notice_seen() {
    std::lock_guard<std::mutex> l(g.mu);
    g.notice = true;
    save_locked();
}
std::string name_locked() {
    if (!g.name.empty()) return g.name;
    std::string n = g.port.default_name ? clean(g.port.default_name(), MAX_NAME) : "";
    return n.empty() ? "Hunter" : n;
}
std::string name() {
    std::lock_guard<std::mutex> l(g.mu);
    return name_locked();
}
std::string own_name() { std::lock_guard<std::mutex> l(g.mu); return g.name; }
void set_name(const std::string &n) {
    std::lock_guard<std::mutex> l(g.mu);
    g.name = clean(n, MAX_NAME);
    g.presence_soon = true;
    save_locked();
}
std::vector<std::string> default_relays() { return std::vector<std::string>(std::begin(DEFAULT_RELAYS), std::end(DEFAULT_RELAYS)); }
std::vector<std::string> relays() {
    std::lock_guard<std::mutex> l(g.mu);
    return g.relays.empty() ? default_relays() : g.relays;
}
void set_relays(const std::vector<std::string> &list) {
    bool restart;
    {
        std::lock_guard<std::mutex> l(g.mu);
        g.relays.clear();
        for (auto &r : list)
            if (r.rfind("wss://", 0) == 0 && r.size() < 200) g.relays.push_back(r);
        if (g.relays == default_relays()) g.relays.clear();
        save_locked();
        restart = g.run;
    }
    if (restart) {
        stop();
        std::lock_guard<std::mutex> l(g.mu);
        start_locked();
    }
}
int overlay() { std::lock_guard<std::mutex> l(g.mu); return g.overlay; }
void set_overlay(int o) {
    std::lock_guard<std::mutex> l(g.mu);
    g.overlay = std::clamp(o, 0, 2);
    save_locked();
}
bool bubble(float *x, float *y, float *size) {
    std::lock_guard<std::mutex> l(g.mu);
    *x = g.bx, *y = g.by, *size = g.bsize;
    return g.has_bubble;
}
void set_bubble(float x, float y, float size) {
    std::lock_guard<std::mutex> l(g.mu);
    g.bx = x, g.by = y, g.bsize = size, g.has_bubble = true;
    save_locked();
}
bool window(float *x, float *y, float *w, float *h) {
    std::lock_guard<std::mutex> l(g.mu);
    *x = g.wx, *y = g.wy, *w = g.ww, *h = g.wh;
    return g.has_window;
}
void set_window(float x, float y, float w, float h) {
    std::lock_guard<std::mutex> l(g.mu);
    g.wx = x, g.wy = y, g.ww = w, g.wh = h, g.has_window = true;
    save_locked();
}
void reset_window() {
    std::lock_guard<std::mutex> l(g.mu);
    g.has_window = false;
    save_locked();
}
int opacity(int which) {
    std::lock_guard<std::mutex> l(g.mu);
    return which >= 0 && which < OPACITIES ? g.opacity[which] : 100;
}
void set_opacity(int which, int percent) {
    if (which < 0 || which >= OPACITIES) return;
    const int v = std::clamp((percent + 2) / 5 * 5, 0, 100);
    std::lock_guard<std::mutex> l(g.mu);
    if (g.opacity[which] == v) return;
    g.opacity[which] = v;
    save_locked();  // (chat.txt.tmp, then put in its place: a cut-off write leaves the last one whole)
}
bool bubble_shown() { std::lock_guard<std::mutex> l(g.mu); return g.bubble_shown; }
void set_bubble_shown(bool on) {
    std::lock_guard<std::mutex> l(g.mu);
    g.bubble_shown = on;
    save_locked();
}

bool flag(const std::string &name) {
    std::lock_guard<std::mutex> l(g.mu);
    return g.flags.count(name) != 0;
}
void set_flag(const std::string &name) {
    std::lock_guard<std::mutex> l(g.mu);
    if (g.flags.insert(name).second) save_locked();
}

Status status(int *up, int *total) {
    std::lock_guard<std::mutex> l(g.mu);
    int n = open_relays_locked(), all = (int)g.conns.size();
    if (up) *up = n;
    if (total) *total = all ? all : (int)(g.relays.empty() ? std::size(DEFAULT_RELAYS) : g.relays.size());
    if (!chatws::available()) return UNAVAILABLE;
    if (!g.enabled || !g.run) return OFF;
    if (n) return ONLINE;
    bool trying = false, failed = false;
    for (auto &r : g.conns) trying |= r.h != 0, failed |= !r.error.empty();
    return failed && !trying ? NO_RELAY : CONNECTING;
}
std::string status_reason() {
    std::lock_guard<std::mutex> l(g.mu);
    for (auto &r : g.conns)
        if (!r.error.empty()) return r.url + ": " + r.error;
    return g.reason;
}
int online_count() {
    std::lock_guard<std::mutex> l(g.mu);
    int n = 0;
    int64_t now = unix_now();
    for (auto &kv : g.online) n += now - kv.second.seen < ONLINE_WITHIN;
    return n + (g.run && open_relays_locked() ? 1 : 0);  // (and this player)
}
std::string my_code() { std::lock_guard<std::mutex> l(g.mu); return chatc::show_code(g.code); }
std::string my_key() { std::lock_guard<std::mutex> l(g.mu); return g.key; }
bool hub_up(std::string *label) {
    std::lock_guard<std::mutex> l(g.mu);
    if (label) *label = g.hub_label;
    return g.hub_was;
}

std::vector<Line> lines(int channel, const std::string &peer, size_t max) {
    std::lock_guard<std::mutex> l(g.mu);
    const std::deque<Line> *d = nullptr;
    if (channel == FRIEND) {
        auto it = g.direct.find(peer);
        if (it != g.direct.end()) d = &it->second;
    } else if (channel >= 0 && channel < 3)
        d = &g.chans[channel];
    std::vector<Line> v;
    if (!d) return v;
    for (size_t i = d->size() > max ? d->size() - max : 0; i < d->size(); i++) v.push_back((*d)[i]);
    return v;
}
std::vector<Line> since(unsigned serial) {
    std::lock_guard<std::mutex> l(g.mu);
    std::vector<Line> v;
    for (auto &x : g.recent)
        if (x.serial > serial) v.push_back(x);
    return v;
}
unsigned last_serial() { std::lock_guard<std::mutex> l(g.mu); return g.serial; }
int unread(int channel, const std::string &peer) {
    std::lock_guard<std::mutex> l(g.mu);
    int n = 0;
    if (channel == FRIEND) {
        for (auto &kv : g.direct) {
            if (!peer.empty() && kv.first != peer) continue;
            unsigned r = g.read_direct.count(kv.first) ? g.read_direct[kv.first] : 0;
            for (auto &x : kv.second) n += x.serial > r && !x.mine;
        }
    } else if (channel >= 0 && channel < 3)
        for (auto &x : g.chans[channel]) n += x.serial > g.read[channel] && !x.mine && !x.system;
    return n;
}
int unread_total() {
    int n = 0;
    for (int c = 0; c < CHANNELS; c++) n += unread(c);
    return n;
}
void mark_read(int channel, const std::string &peer) {
    std::lock_guard<std::mutex> l(g.mu);
    if (channel == FRIEND) g.read_direct[peer] = g.serial;
    else if (channel >= 0 && channel < 3) g.read[channel] = g.serial;
}
size_t max_text() { return MAX_TEXT; }

bool share(int channel, const std::string &peer, int kind, const std::string &a, const std::string &b, std::string *why) {
    std::string text;
    std::unique_lock<std::mutex> l(g.mu);
    if (channel == HUB) {
        l.unlock();
        if (why) *why = "\x01hubshare\x01";
        return false;
    }
    if (!g.enabled || !open_relays_locked()) {
        if (why) *why = "\x01offline\x01";
        return false;
    }
    if (channel == FRIEND) {
        auto it = g.people.find(peer);
        if (it == g.people.end() || !it->second.is_friend) return false;
        if (!it->second.mutual) {
            if (why) *why = "\x01" "notyet\x01" + it->second.name;
            return false;
        }
        if (!online_locked(peer)) {
            if (why) *why = "\x01" "away\x01" + it->second.name;
            return false;
        }
    }
    std::string sa = clean(a, 128), sb = clean(b, 64);
    if (sb.empty() && sa.empty()) return false;
    if (!rate_ok_locked(channel, peer, "\x02" + sa + sb, why)) return false;
    Line ln;
    ln.channel = channel, ln.peer = peer, ln.from = g.key, ln.name = name_locked(), ln.mine = true, ln.game = g.port.game;
    ln.share = kind, ln.share_a = sa, ln.share_b = sb;
    if (channel == FRIEND) direct_locked(peer, "share", "", kind, sa, sb);
    else
        queue_locked(event_locked(KIND_CHANNEL, {{"t", channel == GLOBAL ? TAG_GLOBAL : game_tag(g.port.game)}, {"n", ln.name},
                                                 {"g", g.port.game}, {"s", std::to_string(kind), sa, sb}}, ""));
    push_locked(ln);
    return true;
}
bool say(int channel, const std::string &peer, const std::string &typed, std::string *why) {
    std::string text = clean(typed, MAX_TEXT);
    if (text.empty()) return false;
    std::unique_lock<std::mutex> l(g.mu);
    if (channel == HUB) {  // (the limits count only what could be sent)
        auto send = g.port.hub_send;
        if (!g.hub) {
            if (why) *why = "\x01hubchat\x01";
            return false;
        }
        if (!g.hub_was || !send) {
            if (why) *why = "\x01nohub\x01";
            return false;
        }
        if (!rate_ok_locked(channel, peer, text, why)) return false;
        l.unlock();
        if (!send(text)) {
            if (why) *why = "\x01nohub\x01";
            return false;
        }
        return true;
    }
    if (!g.enabled || !open_relays_locked()) {
        if (why) *why = "\x01offline\x01";
        return false;
    }
    Line ln;
    ln.channel = channel, ln.peer = peer, ln.from = g.key, ln.name = name_locked(), ln.text = text, ln.mine = true, ln.game = g.port.game;
    if (channel == FRIEND) {
        auto it = g.people.find(peer);
        if (it == g.people.end() || !it->second.is_friend) return false;
        if (!it->second.mutual) {
            if (why) *why = "\x01" "notyet\x01" + it->second.name;
            return false;
        }
        if (!online_locked(peer)) {
            if (why) *why = "\x01" "away\x01" + it->second.name;
            return false;
        }
        if (!rate_ok_locked(channel, peer, text, why) || !direct_locked(peer, "msg", text, SHARE_NONE, "", "")) return false;
    } else {
        if (!rate_ok_locked(channel, peer, text, why)) return false;
        queue_locked(event_locked(KIND_CHANNEL, {{"t", channel == GLOBAL ? TAG_GLOBAL : game_tag(g.port.game)}, {"n", ln.name}, {"g", g.port.game}}, text));
    }
    push_locked(ln);
    return true;
}

std::vector<Person> people() {
    std::lock_guard<std::mutex> l(g.mu);
    std::vector<Person> v;
    int64_t now = unix_now();
    for (auto &kv : g.people) {
        Person p = kv.second;
        auto o = g.online.find(p.id);
        if (o != g.online.end()) {
            p.online = now - o->second.seen < ONLINE_WITHIN && !p.blocked;
            p.in_hub = o->second.in_hub, p.game = o->second.game, p.seen = o->second.seen;
        }
        v.push_back(p);
    }
    std::sort(v.begin(), v.end(), [](const Person &a, const Person &b) {
        if (a.online != b.online) return a.online;
        return lower(a.name) < lower(b.name);
    });
    return v;
}
bool add_friend(const std::string &typed, std::string *why) {
    std::string c = chatc::normalize_code(typed);
    std::lock_guard<std::mutex> l(g.mu);
    if (c.empty()) {
        if (why) *why = "\x01" "badcode\x01";
        return false;
    }
    if (c == g.code) {
        if (why) *why = "\x01" "yours\x01";
        return false;
    }
    for (auto &kv : g.people)
        if (kv.second.code == c && kv.second.is_friend) {
            if (why) *why = "\x01" "already\x01" + kv.second.name;
            return false;
        }
    for (auto &kv : g.online) {  // (online now: added at once)
        uint8_t pk[32];
        if (!chatc::unhex(kv.first, pk, 32) || chatc::friend_code(pk) != c) continue;
        auto had = g.people.find(kv.first);
        bool asked_us = had != g.people.end() && had->second.request;  // (they asked first: this is the answer)
        person_locked(kv.first).name = kv.second.name;
        return add_key_locked(kv.first, kv.second.name, asked_us);
    }
    Person &p = person_locked("code:" + c);
    p.pending = true, p.code = c;
    save_locked();
    return true;
}
bool add_friend_key(const std::string &key, const std::string &n) {
    std::lock_guard<std::mutex> l(g.mu);
    return add_key_locked(key, n, false);
}
void accept(const std::string &key) {
    std::lock_guard<std::mutex> l(g.mu);
    auto it = g.people.find(key);
    if (it == g.people.end() || !it->second.request) return;
    add_key_locked(key, it->second.name, true);
}
bool add_key_locked(const std::string &key, const std::string &n, bool accepting) {
    std::string k = lower(key);
    uint8_t pk[32];
    if (k == g.key || !chatc::unhex(k, pk, 32)) return false;
    Person &p = person_locked(k);
    p.is_friend = true, p.blocked = false, p.request = false;
    if (accepting) p.mutual = true;
    if (p.name.empty()) p.name = clean(n, MAX_NAME);
    g.people.erase("code:" + chatc::friend_code(pk));
    if (g.run) direct_locked(k, accepting ? "acc" : "req", "", SHARE_NONE, "", ""), g.asked.insert(k);
    save_locked();
    return true;
}
void remove(const std::string &id) {
    std::lock_guard<std::mutex> l(g.mu);
    auto it = g.people.find(id);
    if (it == g.people.end()) return;
    Person &p = it->second;
    p.is_friend = p.pending = p.request = false;
    if (!p.muted && !p.blocked) g.people.erase(it);
    save_locked();
}
void set_muted(const std::string &id, const std::string &n, bool on) {
    if (id.empty() || id == my_key()) return;
    std::lock_guard<std::mutex> l(g.mu);
    Person &p = person_locked(id);
    p.muted = on;
    if (p.name.empty()) p.name = clean(n, MAX_NAME);
    if (!p.muted && !p.blocked && !p.is_friend && !p.pending && !p.request) g.people.erase(id);
    save_locked();
}
void set_blocked(const std::string &id, const std::string &n, bool on) {
    if (id.empty() || id == my_key()) return;
    std::lock_guard<std::mutex> l(g.mu);
    Person &p = person_locked(id);
    p.blocked = on;
    if (on) p.is_friend = p.request = false;
    if (p.name.empty()) p.name = clean(n, MAX_NAME);
    if (!p.muted && !p.blocked && !p.is_friend && !p.pending && !p.request) g.people.erase(id);
    save_locked();
}
bool is_muted(const std::string &id) {
    std::lock_guard<std::mutex> l(g.mu);
    auto it = g.people.find(id);
    return it != g.people.end() && it->second.muted;
}
bool is_blocked(const std::string &id) {
    std::lock_guard<std::mutex> l(g.mu);
    auto it = g.people.find(id);
    return it != g.people.end() && it->second.blocked;
}
std::string person_name(const std::string &id) {
    std::lock_guard<std::mutex> l(g.mu);
    auto it = g.people.find(id);
    return it == g.people.end() ? "" : it->second.name;
}

void hub_line(const std::string &from, const std::string &n, const std::string &typed, bool mine) {
    std::lock_guard<std::mutex> l(g.mu);
    if (!g.hub) return;  // (the hub's chat off: its lines are not the chat's)
    std::string text = clean(typed, 500), who = clean(n, MAX_NAME);
    if (text.empty()) return;
    if (!mine && !admit_locked(from, who, HUB, "")) return;
    Line ln;
    ln.channel = HUB, ln.from = mine ? g.key : from, ln.name = who.empty() ? "?" : who, ln.text = text, ln.mine = mine;
    push_locked(ln);
}
void hub_reset() {
    std::lock_guard<std::mutex> l(g.mu);
    g.chans[HUB].clear();
}
void test_line(int channel, const std::string &peer, const std::string &n, const std::string &typed) {
    std::lock_guard<std::mutex> l(g.mu);
    if (channel < 0 || channel >= CHANNELS || (channel == FRIEND && peer.empty())) return;
    Line ln;
    ln.channel = channel, ln.peer = channel == FRIEND ? peer : std::string(), ln.name = clean(n, MAX_NAME), ln.text = clean(typed, 500);
    ln.from = channel == FRIEND ? peer : "test:" + ln.name;
    if (channel == GLOBAL) ln.game = g.port.game;
    push_locked(ln);
}
void test_lines() {
    static const struct {
        int channel;
        const char *name, *text;
    } said[] = {
        {HUB, "Ana", "Hola, alguien para una mision facil?"},
        {GENERAL, "Kai", "Anyone up for a low rank hunt?"},
        {GLOBAL, "Rex", "Buenas desde el otro port"},
        {FRIEND, "", "Jugamos despues?"},
        {HUB, "Bo", "Voy a la tienda y vuelvo"},
        {GENERAL, "Lia", "Thanks for the help!"},
        {GLOBAL, "Tom", "Que tal el chat nuevo?"},
        {FRIEND, "", "Te mando la clave en un rato"},
    };
    static uint64_t next;
    static unsigned done;
    const uint64_t now = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (done >= 40 || (done >= 4 && now < next)) return;
    std::string peer, friend_name;
    for (const Person &p : people())
        if (p.is_friend && peer.empty()) peer = p.id, friend_name = p.name;
    do {
        const auto &x = said[done % (sizeof said / sizeof said[0])];
        test_line(x.channel, peer, x.channel == FRIEND ? friend_name : x.name, x.text);
        done++;
    } while (done < 4);
    next = now + 10000;
}
static void hub_digest(const std::string &hub_id, int64_t at, const std::string &n, const std::string &text, uint8_t out[32]) {
    chatc::Sha256 s;
    s.add("MHPC", 4);
    s.add(hub_id.data(), hub_id.size());
    uint8_t t[8];
    for (int i = 0; i < 8; i++) t[i] = (uint8_t)((uint64_t)at >> (56 - 8 * i));
    s.add(t, 8);
    s.add(n.data(), n.size());
    s.add("", 1);
    s.add(text.data(), text.size());
    s.done(out);
}
bool hub_sign(const std::string &hub_id, int64_t at, const std::string &n, const std::string &text, uint8_t pub[32], uint8_t sig[64]) {
    std::lock_guard<std::mutex> l(g.mu);
    uint8_t d[32];
    hub_digest(hub_id, at, n, text, d);
    memcpy(pub, g.pub, 32);
    return chatc::sign(g.sec, d, sig);
}
bool hub_verify(const std::string &hub_id, int64_t at, const std::string &n, const std::string &text, const uint8_t pub[32],
                const uint8_t sig[64]) {
    uint8_t d[32];
    hub_digest(hub_id, at, n, text, d);
    return chatc::verify(pub, d, sig);
}

std::string new_key() {
    uint8_t b[16];
    chatc::random_bytes(b, 16);
    return chatc::hex(b, 16);
}

}  // namespace chat
