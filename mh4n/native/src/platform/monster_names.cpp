// The large monsters' names for the app's health bars, from the game's RomFS (monster_names.h).
#include "platform/monster_names.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

#include "hunt_aids.h"
#include "lodepng.h"  // (its zlib inflate: the archives' files are zlib streams)
#include "mh4n/log.h"
#include "mh4n/registry.h"
#include "mh4n/runtime.h"
#include "services/fs/game_romfs.h"

namespace mh4n::platform::monster_names {

namespace {

constexpr const char* kLog = "Frontend";  // (the hunt aids' lines: "hunt aids: ...")

std::uint32_t Le32(const unsigned char* p) {
    return p[0] | p[1] << 8 | p[2] << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

// A file of an MT Framework archive (ARC\0, version 0x13: 80-byte entries, a name of 64, its type, sizes and place;
// the data a zlib stream, or stored) by its name ("eng\msg\emName_eng"), inflated
bool ArcFile(const fs::GameRomFsFile& arc, const std::string& name, std::vector<unsigned char>* out) {
    unsigned char h[12];
    if (arc.size < 12 || arc.Read(0, h, sizeof h) != sizeof h || std::memcmp(h, "ARC", 4) != 0) return false;
    const unsigned count = h[6] | h[7] << 8;
    if (12 + 80ull * count > arc.size) return false;
    std::vector<unsigned char> table(80ull * count);
    if (arc.Read(12, table.data(), table.size()) != table.size()) return false;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned char* e = table.data() + 80ull * i;
        if (strnlen(reinterpret_cast<const char*>(e), 64) != name.size() || std::memcmp(e, name.data(), name.size()) != 0)
            continue;
        const std::uint32_t packed = Le32(e + 68), full = Le32(e + 72) & 0x1fffffffu, offset = Le32(e + 76);
        if (packed == 0 || packed > (8u << 20) || full > (8u << 20) || offset + std::uint64_t{packed} > arc.size) return false;
        std::vector<unsigned char> in(packed);
        if (arc.Read(offset, in.data(), packed) != packed) return false;
        out->clear();
        if (lodepng::decompress(*out, in.data(), in.size()) != 0) {  // (stored as it is: the same size)
            if (packed != full) return false;
            *out = std::move(in);
        }
        return out->size() == full;
    }
    return false;
}

std::mutex g_mutex;
std::map<int, std::vector<std::string>> g_names;  // (a language read; empty while being read, or with none)
std::map<int, bool> g_started;

}  // namespace

const char* Folder(int language) {
    static const char* const kFolders[] = {nullptr, "eng", "fre", "spa", "ger", "ita"};  // (0 jpn: none in this game)
    return language >= 1 && language < 6 ? kFolders[language] : nullptr;
}

bool LmdTexts(const std::vector<unsigned char>& d, std::vector<std::string>* out) {
    // lmd\0, version 0x0001010b: the count at +8, a table of (place, length in UTF-16 units, the same) at +0x1c's place
    if (d.size() < 0x24 || std::memcmp(d.data(), "lmd", 4) != 0) return false;
    const std::uint32_t n = Le32(d.data() + 8), table = Le32(d.data() + 0x1c);
    if (n > 4096 || table + 12ull * n > d.size()) return false;
    out->clear();
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint32_t at = Le32(d.data() + table + 12 * i), len = Le32(d.data() + table + 12 * i + 4);
        if (at + 2ull * len > d.size()) return false;
        std::string s;
        for (std::uint32_t k = 0; k < len; ++k) {
            std::uint32_t c = d[at + 2 * k] | d[at + 2 * k + 1] << 8;
            if (c >= 0xd800 && c < 0xdc00 && k + 1 < len) {  // (a pair of surrogates)
                const std::uint32_t lo = d[at + 2 * k + 2] | d[at + 2 * k + 3] << 8;
                if (lo >= 0xdc00 && lo < 0xe000) c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00), ++k;
            }
            if (c == 0) break;
            if (c < 0x80) {
                s += static_cast<char>(c);
            } else if (c < 0x800) {
                s += static_cast<char>(0xc0 | c >> 6), s += static_cast<char>(0x80 | (c & 0x3f));
            } else if (c < 0x10000) {
                s += static_cast<char>(0xe0 | c >> 12), s += static_cast<char>(0x80 | (c >> 6 & 0x3f));
                s += static_cast<char>(0x80 | (c & 0x3f));
            } else {
                s += static_cast<char>(0xf0 | c >> 18), s += static_cast<char>(0x80 | (c >> 12 & 0x3f));
                s += static_cast<char>(0x80 | (c >> 6 & 0x3f)), s += static_cast<char>(0x80 | (c & 0x3f));
            }
        }
        out->push_back(std::move(s));
    }
    return true;
}

bool Read(int language, std::vector<std::string>* names, std::string* why) {
    const char* lang = Folder(language);
    if (!lang) return *why = "no texts in that language", false;
    const std::string arc_path = std::string(lang) + "/data/core_common.arc";
    const std::string file = std::string(lang) + "\\msg\\emName_" + lang;
    fs::GameRomFsFile arc;
    if (!fs::OpenGameRomFsFile(arc_path, &arc, why)) return false;
    std::vector<unsigned char> lmd;
    const char* where = arc.update ? "the update's RomFS" : "the game's RomFS";
    if (!ArcFile(arc, file, &lmd)) return *why = arc_path + " of " + where + " has no " + file, false;
    if (!LmdTexts(lmd, names)) return *why = file + " of " + where + " can't be read", false;
    *why = arc_path + " of " + where;
    return true;
}

void Prepare(int language) {
    if (!Folder(language)) return;
    {
        std::lock_guard lock(g_mutex);
        if (g_started[language]) return;
        g_started[language] = true;
    }
    std::thread([language]() {
        std::vector<std::string> names;
        std::string why;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = Read(language, &names, &why);
        if (!ok && why == "no game runs") {  // (too early: again on a later frame)
            std::lock_guard lock(g_mutex);
            g_started.erase(language);
            return;
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (ok) {
            MH4N_LOG(Info, kLog, "monster names: {} in {}, from {} ({:.1f} ms): 1 {} ... {} {}", names.size(),
                     Folder(language), why, ms, names.size() > 1 ? names[1] : std::string(), names.size() - 1,
                     names.empty() ? std::string() : names.back());
        } else {
            MH4N_LOG(Warning, kLog, "monster names: none in {} ({})", Folder(language), why);
        }
        std::lock_guard lock(g_mutex);
        g_names[language] = std::move(names);
    }).detach();
}

std::string Name(int language, int kind) {
    std::lock_guard lock(g_mutex);
    const auto it = g_names.find(language);
    if (it == g_names.end() || kind <= 0 || kind >= static_cast<int>(it->second.size())) return {};
    return it->second[kind];
}

namespace {

// Phase `started`: each frame, once the bars are on and the game's language is known, its names (read once a
// language: Prepare does nothing more after that). Nothing while the option is off.
void StartedStep(Runtime& runtime) {
    runtime.OnFrame([](u32, u64) {
        if (Port::HuntAids::HealthBarOn()) Prepare(Port::HuntAids::GameLanguage());
    });
}

MH4N_REGISTER(BootStep, "platform.monster_names", BootStep{Phase::Started, StartedStep});

}  // namespace

}  // namespace mh4n::platform::monster_names
