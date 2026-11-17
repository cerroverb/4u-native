// The large monsters' names on their health bars, from the game's files (hunt_names.h).
#include "hunt_names.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

#include "lodepng.h"          // (its zlib inflate: the archive's files are zlib streams; mh4n's copy, linked in already)
#include "mods/mod_list.h"    // (the game folder the running game was loaded from)

namespace {

// ---- the files' formats

std::uint32_t le32(const unsigned char* p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (std::uint32_t)p[3] << 24;
}

bool read_at(std::ifstream& f, std::uint64_t at, void* out, std::size_t size) {
    f.clear();
    f.seekg((std::streamoff)at);
    return (bool)f.read(static_cast<char*>(out), (std::streamsize)size);
}

// A file of a RomFS image (romfs.bin, update_romfs.bin: the level 3 at 0x1000 after the IVFC header) by its path
// ("eng/data/core_common.arc"): where its data is in the image and how long; false if it has none.
bool romfs_find(std::ifstream& f, const std::string& path, std::uint64_t* at, std::uint64_t* size) {
    constexpr std::uint64_t kBase = 0x1000;
    unsigned char h[40];
    if (!read_at(f, kBase, h, sizeof h) || le32(h) != 0x28) return false;
    const std::uint32_t dir_meta = le32(h + 12), dir_size = le32(h + 16), file_meta = le32(h + 28), file_size = le32(h + 32);
    const std::uint32_t data = le32(h + 36);
    if (dir_size > (16u << 20) || file_size > (64u << 20)) return false;
    std::vector<unsigned char> dirs(dir_size), files(file_size);
    if (!read_at(f, kBase + dir_meta, dirs.data(), dir_size) || !read_at(f, kBase + file_meta, files.data(), file_size))
        return false;
    auto named = [](const unsigned char* name, std::uint32_t bytes, const std::string& want) {  // (UTF-16 against ASCII)
        if (bytes != 2 * want.size()) return false;
        for (std::size_t i = 0; i < want.size(); ++i)
            if (name[2 * i] != (unsigned char)want[i] || name[2 * i + 1] != 0) return false;
        return true;
    };
    std::uint32_t dir = 0;  // (the root)
    std::size_t from = 0;
    for (;;) {
        const std::size_t slash = path.find('/', from);
        const std::string part = path.substr(from, slash == std::string::npos ? std::string::npos : slash - from);
        if (slash == std::string::npos) {  // (the file, in this directory's list)
            if (dir + 24 > dir_size) return false;
            for (std::uint32_t e = le32(dirs.data() + dir + 12), n = 0; e != 0xffffffffu && n < 100000; ++n) {
                if (e + 32 > file_size) return false;
                const std::uint32_t len = le32(files.data() + e + 28);
                if (e + 32 + len > file_size) return false;
                if (named(files.data() + e + 32, len, part)) {
                    *at = kBase + data + (le32(files.data() + e + 8) | (std::uint64_t)le32(files.data() + e + 12) << 32);
                    *size = le32(files.data() + e + 16) | (std::uint64_t)le32(files.data() + e + 20) << 32;
                    return true;
                }
                e = le32(files.data() + e + 4);
            }
            return false;
        }
        bool found = false;  // (a directory, among this one's children)
        if (dir + 24 > dir_size) return false;
        for (std::uint32_t c = le32(dirs.data() + dir + 8), n = 0; c != 0xffffffffu && n < 100000; ++n) {
            if (c + 24 > dir_size) return false;
            const std::uint32_t len = le32(dirs.data() + c + 20);
            if (c + 24 + len > dir_size) return false;
            if (named(dirs.data() + c + 24, len, part)) {
                dir = c, found = true;
                break;
            }
            c = le32(dirs.data() + c + 4);
        }
        if (!found) return false;
        from = slash + 1;
    }
}

// A file of an MT Framework archive (ARC\0, version 0x13: 80-byte entries, a name of 64, its type, sizes and place; the
// data a zlib stream) by its name ("eng\msg\emName_eng"), inflated
bool arc_file(std::ifstream& f, std::uint64_t at, std::uint64_t size, const std::string& name, std::vector<unsigned char>* out) {
    unsigned char h[12];
    if (size < 12 || !read_at(f, at, h, sizeof h) || std::memcmp(h, "ARC", 4) != 0) return false;
    const unsigned count = h[6] | h[7] << 8;
    if (12 + 80ull * count > size) return false;
    std::vector<unsigned char> table(80ull * count);
    if (!read_at(f, at + 12, table.data(), table.size())) return false;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned char* e = table.data() + 80ull * i;
        if (strnlen(reinterpret_cast<const char*>(e), 64) != name.size() || std::memcmp(e, name.data(), name.size()) != 0)
            continue;
        const std::uint32_t packed = le32(e + 68), full = le32(e + 72) & 0x1fffffffu, offset = le32(e + 76);
        if (packed == 0 || packed > (8u << 20) || full > (8u << 20) || offset + (std::uint64_t)packed > size) return false;
        std::vector<unsigned char> in(packed);
        if (!read_at(f, at + offset, in.data(), packed)) return false;
        out->clear();
        if (lodepng::decompress(*out, in.data(), in.size()) != 0) {  // (stored whole: the same size)
            if (packed != full) return false;
            *out = std::move(in);
        }
        return out->size() == full;
    }
    return false;
}

// The texts of an lmd message file (lmd\0, version 0x0001010b): their count at +8, a table of (place, length in
// UTF-16 units, the same again) at the place +0x1c gives, in their order; as UTF-8
bool lmd_texts(const std::vector<unsigned char>& d, std::vector<std::string>* out) {
    if (d.size() < 0x24 || std::memcmp(d.data(), "lmd", 4) != 0) return false;
    const std::uint32_t n = le32(d.data() + 8), table = le32(d.data() + 0x1c);
    if (n > 4096 || table + 12ull * n > d.size()) return false;
    out->clear();
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint32_t at = le32(d.data() + table + 12 * i), len = le32(d.data() + table + 12 * i + 4);
        if (at + 2ull * len > d.size()) return false;
        std::string s;
        for (std::uint32_t k = 0; k < len; ++k) {
            std::uint32_t c = d[at + 2 * k] | d[at + 2 * k + 1] << 8;
            if (c >= 0xd800 && c < 0xdc00 && k + 1 < len) {  // (a pair of surrogates)
                const std::uint32_t lo = d[at + 2 * k + 2] | d[at + 2 * k + 3] << 8;
                if (lo >= 0xdc00 && lo < 0xe000) c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00), ++k;
            }
            if (c == 0) break;
            if (c < 0x80) s += (char)c;
            else if (c < 0x800) s += (char)(0xc0 | c >> 6), s += (char)(0x80 | (c & 0x3f));
            else if (c < 0x10000) s += (char)(0xe0 | c >> 12), s += (char)(0x80 | (c >> 6 & 0x3f)), s += (char)(0x80 | (c & 0x3f));
            else s += (char)(0xf0 | c >> 18), s += (char)(0x80 | (c >> 12 & 0x3f)), s += (char)(0x80 | (c >> 6 & 0x3f)),
                 s += (char)(0x80 | (c & 0x3f));
        }
        out->push_back(std::move(s));
    }
    return true;
}

// ---- what was read (each language once)

std::mutex g_mutex;
std::map<int, std::vector<std::string>> g_names;  // (a language read, or being read: empty)
std::map<int, bool> g_started;

}  // namespace

const char* hunt_names_folder(int language) {
    static const char* const kFolders[] = {nullptr, "eng", "fre", "spa", "ger", "ita"};  // (0 jpn: none in this game)
    return language >= 1 && language < 6 ? kFolders[language] : nullptr;
}

bool hunt_names_read(const std::string& game_folder, int language, std::vector<std::string>* names, std::string* why) {
    const char* lang = hunt_names_folder(language);
    if (!lang) return *why = "no texts in that language", false;
    const std::string arc = std::string(lang) + "/data/core_common.arc";
    const std::string file = std::string(lang) + "\\msg\\emName_" + lang;
    for (const char* image : {"update_romfs.bin", "romfs.bin"}) {  // (the update's first: the game loads its own)
        std::ifstream f(game_folder + "/" + image, std::ios::binary);
        std::uint64_t at = 0, size = 0;
        if (!f || !romfs_find(f, arc, &at, &size)) continue;
        std::vector<unsigned char> lmd;
        if (!arc_file(f, at, size, file, &lmd)) return *why = std::string(image) + ": " + arc + " has no " + file, false;
        if (!lmd_texts(lmd, names)) return *why = std::string(image) + ": " + file + " can't be read", false;
        return true;
    }
    return *why = "no " + arc + " in " + game_folder, false;
}

void hunt_names_prepare(int language) {
    if (!hunt_names_folder(language)) return;
    {
        std::lock_guard lock(g_mutex);
        if (g_started[language]) return;
    }
    const std::string folder = mh4n::mods::RunningGameFolder();
    if (folder.empty()) return;  // (no game yet: again later)
    {
        std::lock_guard lock(g_mutex);
        if (g_started[language]) return;
        g_started[language] = true;
    }
    std::thread([folder, language]() {
        std::vector<std::string> names;
        std::string why;
        const std::uint64_t t0 = SDL_GetTicksNS();
        const bool ok = hunt_names_read(folder, language, &names, &why);
        if (ok) SDL_Log("monster names: %zu in %s, read in %.1f ms", names.size(), hunt_names_folder(language),
                        (SDL_GetTicksNS() - t0) / 1e6);
        else SDL_Log("monster names: none in %s (%s)", hunt_names_folder(language), why.c_str());
        std::lock_guard lock(g_mutex);
        g_names[language] = std::move(names);
    }).detach();
}

std::string hunt_name(int language, int kind) {
    std::lock_guard lock(g_mutex);
    auto it = g_names.find(language);
    if (it == g_names.end() || kind <= 0 || kind >= (int)it->second.size()) return {};
    return it->second[kind];
}
