// The mods of the user folder (mod_list.h).
#include "mods/mod_list.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>

#include <fmt/format.h>

#include "../common/utf8.h"
#include "mh4n/crc32.h"
#include "mh4n/loader.h"
#include "mh4n/log.h"
#include "mh4n/registry.h"
#include "mh4n/runtime.h"

namespace mh4n::mods {

namespace stdfs = std::filesystem;

namespace {

constexpr const char* kLog = "Mods";
constexpr const char* kOffPrefix = "# off: ";

std::string Trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

bool IsTitleId(const std::string& name) {
    return name.size() == 16 && std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

bool Exists(const stdfs::path& p) {
    std::error_code ec;
    return stdfs::exists(p, ec);
}
bool IsDir(const stdfs::path& p) {
    std::error_code ec;
    return stdfs::is_directory(p, ec);
}

std::vector<std::string> Lines(const stdfs::path& p) {
    std::vector<std::string> out;
    std::ifstream in(p, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) out.push_back(Trim(line));
    return out;
}

std::string WithSlash(std::string dir) {
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';
    return dir;
}

// What one folder holds.
Mod Describe(const std::string& folder, const std::string& name) {
    Mod m;
    m.name = name;
    m.path = WithSlash(folder) + name + "/";
    const stdfs::path p = utf8::PathFromUtf8(m.path);
    m.romfs = IsDir(p / "romfs") || IsDir(p / "romfs_ext");
    for (const char* rel : {"exefs/code.ips", "exefs/code.bps", "exefs/code.bin", "code.ips", "code.bps", "code.bin"}) {
        if (Exists(p / stdfs::path(rel))) m.code = true;
    }
    return m;
}

// ---- the running game -------------------------------------------------------------------------------
std::mutex g_mutex;
LibraryId g_library;
std::string g_game_folder;
u64 g_program_id = 0;

void RecordRunning(Runtime& rt) {
    std::lock_guard lock(g_mutex);
    g_game_folder = rt.GameFolder();
    g_program_id = rt.Image() ? rt.Image()->program_id : 0;
    g_library = {};
    if (const CodeLibrary* library = rt.GetCodeHost().Library()) {
        g_library.known = true;
        g_library.text_base = library->TextBase();
        g_library.text_size = library->TextSize();
        g_library.text_crc32 = library->TextCrc32();
    }
    if (!rt.UserFolder().empty() && g_program_id != 0) {
        const std::string folder = Folder(rt.UserFolder());
        const std::vector<Mod> list = List(folder, g_program_id);
        std::string names;
        for (const Mod& m : list) {
            if (m.on) names += (names.empty() ? "" : ", ") + m.name + (m.code ? " (code)" : "");
        }
        if (!names.empty()) MH4N_LOG(Info, kLog, "mods on, in load order: {}", names);
        // (the code library took the code with these: the app's error screen can go back to them)
        if (g_library.known && IsDir(utf8::PathFromUtf8(folder))) SaveOrder(folder, list, nullptr, kLastGoodFile);
    }
    rt.OnFinish([] {
        std::lock_guard l(g_mutex);
        g_library = {};
        g_game_folder.clear();
        g_program_id = 0;
    });
}
MH4N_REGISTER(BootStep, "started.mods", BootStep{Phase::Started, RecordRunning});

}  // namespace

std::string Folder(const std::string& user_folder) {
    if (user_folder.empty()) return {};
    return WithSlash(user_folder) + "load/mods/";
}

std::vector<Mod> List(const std::string& folder, u64 program_id) {
    std::vector<Mod> out;
    if (folder.empty()) return out;
    const stdfs::path root = utf8::PathFromUtf8(folder);
    const std::string own_id = fmt::format("{:016X}", code_patch::ModId(program_id));
    // the folders, by name
    std::vector<std::string> names;
    {
        std::error_code ec;
        for (stdfs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code dir_ec;
            if (!it->is_directory(dir_ec)) continue;
            const std::string name = utf8::PathToUtf8(it->path().filename());
            if (name.empty() || name[0] == '.' || name[0] == '_') continue;
            if (IsTitleId(name)) {
                std::string upper = name;
                std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return (char)std::toupper(c); });
                if (upper != own_id) continue;  // (another title's)
            }
            names.push_back(name);
        }
        std::sort(names.begin(), names.end());
    }
    std::set<std::string> from_game;
    for (const std::string& line : Lines(root / kGameFolderList)) {
        const std::size_t tab = line.find('\t');
        const std::string name = tab == std::string::npos ? line : line.substr(0, tab);
        if (!name.empty()) from_game.insert(name);
    }
    // the list's order first
    std::set<std::string> placed;
    for (const std::string& line : Lines(root / kOrderFile)) {
        bool on = true;
        std::string name = line;
        if (name.compare(0, std::char_traits<char>::length(kOffPrefix), kOffPrefix) == 0) {
            name = Trim(name.substr(std::char_traits<char>::length(kOffPrefix)));
            on = false;
        } else if (name.empty() || name[0] == '#') {
            continue;
        }
        if (placed.count(name) || std::find(names.begin(), names.end(), name) == names.end()) continue;
        Mod m = Describe(folder, name);
        m.on = on;
        out.push_back(std::move(m));
        placed.insert(name);
    }
    // then the rest, on
    for (const std::string& name : names) {
        if (placed.count(name)) continue;
        out.push_back(Describe(folder, name));
    }
    for (Mod& m : out) m.from_game_folder = from_game.count(m.name) != 0;
    return out;
}

std::vector<Mod> Enabled(const std::string& folder, u64 program_id) {
    std::vector<Mod> all = List(folder, program_id), on;
    for (Mod& m : all) {
        if (m.on) on.push_back(std::move(m));
    }
    return on;
}

bool SaveOrder(const std::string& folder, const std::vector<Mod>& mods, std::string* error, const char* file) {
    const stdfs::path root = utf8::PathFromUtf8(folder);
    std::error_code ec;
    stdfs::create_directories(root, ec);
    const stdfs::path path = root / file;
    stdfs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << "# Written by the Mods page of the settings: the mods in load order, one per line. Where two change\n"
               "# the same file, the one further down wins. \"# off: name\" keeps a mod that is off in its place.\n"
               "# A folder added since loads after these, on.\n";
        for (const Mod& m : mods) out << (m.on ? "" : kOffPrefix) << m.name << "\n";
        out.close();
        if (!out) {
            if (error) *error = "cannot write " + utf8::PathToUtf8(path);
            return false;
        }
    }
    stdfs::rename(tmp, path, ec);
    if (ec) {
        if (error) *error = "cannot write " + utf8::PathToUtf8(path) + ": " + ec.message();
        return false;
    }
    return true;
}

code_patch::Outcome ApplyCode(const std::vector<Mod>& mods, std::vector<u8>& code, u32 bss_pages) {
    std::vector<std::string> folders;
    for (const Mod& m : mods) {
        if (m.on && m.code) folders.push_back(m.path);
    }
    if (folders.empty()) {
        code.resize(code.size() + static_cast<std::size_t>(bss_pages) * code_patch::kPageSize, 0);
        return {};
    }
    return code_patch::ApplyFolders(folders, code, bss_pages);
}

std::vector<Layer> RomFsLayers(const std::vector<Mod>& mods) {
    std::vector<Layer> out;
    for (const Mod& m : mods) {
        if (m.on && m.romfs) out.push_back({m.path + "romfs", m.path + "romfs_ext"});
    }
    return out;
}

LibraryId RunningLibrary() {
    std::lock_guard lock(g_mutex);
    return g_library;
}
std::string RunningGameFolder() {
    std::lock_guard lock(g_mutex);
    return g_game_folder;
}
u64 RunningProgramId() {
    std::lock_guard lock(g_mutex);
    return g_program_id;
}

CodeCheck CheckCode(const std::string& game_folder, const std::vector<Mod>& mods, const LibraryId& library) {
    CodeCheck check;
    if (!library.known) {
        check.error = "no code library";
        return check;
    }
    const stdfs::path dir = utf8::PathFromUtf8(game_folder);
    auto read = [](const stdfs::path& p, std::vector<u8>* out) {
        std::ifstream in(p, std::ios::binary);
        if (!in) return false;
        out->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return !in.bad();
    };
    std::vector<u8> exheader, code;
    if (!read(dir / "exheader.bin", &exheader) || !read(dir / "code.bin", &code)) {
        check.error = "the game folder's code can't be read";
        return check;
    }
    ExHeader header;
    std::string error;
    if (!ParseExHeader(exheader.data(), exheader.size(), &header, &error)) {
        check.error = error;
        return check;
    }
    const code_patch::Outcome outcome = ApplyCode(mods, code, 0);
    if (!outcome.ok) {
        check.error = outcome.error + (outcome.detail.empty() ? "" : " (" + outcome.detail + ")");
        return check;
    }
    // CodeLibrary::MatchesWithin: the text, or (a library made past the text, into its last page) more of it
    const std::size_t text = header.text.size, pages = static_cast<std::size_t>(header.text.num_pages) * 0x1000;
    std::size_t size = text;
    if (library.text_size > text && library.text_size <= pages) size = library.text_size;
    if (size > code.size()) {
        check.error = "the code is shorter than its text segment";
        return check;
    }
    check.matches = header.text.address == library.text_base && size == library.text_size &&
                    Crc32(code.data(), size) == library.text_crc32;
    return check;
}

}  // namespace mh4n::mods
