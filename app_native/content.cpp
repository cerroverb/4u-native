// Cheats and mods for the settings' pages (content.h).
#include "content.h"

#include <android/log.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

#include "app_jni.h"
#include "cheats/cheat_engine.h"

namespace {

namespace fs = std::filesystem;

std::string g_user;

void Log(const char* format, ...) __attribute__((format(printf, 1, 2)));
void Log(const char* format, ...) {
    char text[1024];
    va_list ap;
    va_start(ap, format);
    vsnprintf(text, sizeof text, format, ap);
    va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, "MH4U", "content: %s", text);
    std::fprintf(stderr, "[Info] App: %s\n", text);  // (log.txt)
}

std::vector<std::string> Lines(const std::string& path) {
    std::vector<std::string> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        out.push_back(line);
    }
    return out;
}

bool Exists(const std::string& path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

std::string CheatsDir() { return g_user + "/cheats"; }
std::string ModsDir() { return mh4n::mods::Folder(g_user); }

// An outcome file of ImportActivity.java: ok / <name> [/ 1 when a mod changes the code], or error / <why> / <detail>.
ContentImport ReadOutcome(const std::string& path) {
    ContentImport r;
    if (g_user.empty() || !Exists(path)) return r;
    const std::vector<std::string> lines = Lines(path);
    unlink(path.c_str());
    r.done = true;
    r.ok = !lines.empty() && lines[0] == "ok" && lines.size() >= 2;
    if (r.ok) {
        r.name = lines[1];
        r.code = lines.size() >= 3 && lines[2] == "1";
    } else {
        r.error = lines.size() >= 2 && !lines[1].empty() ? lines[1] : "unreadable";
        r.detail = lines.size() >= 3 ? lines[2] : "";
    }
    return r;
}

// ---- the code check, on a thread of its own
std::mutex g_check_mutex;
std::map<std::string, ContentCodeCheck> g_checks;  // by the set of code mods asked about (busy while a thread works)

std::string CheckKey(const std::vector<mh4n::mods::Mod>& list, const mh4n::mods::LibraryId& lib) {
    std::string key = std::to_string(lib.text_crc32) + ":" + std::to_string(lib.text_size);
    for (const auto& m : list) {
        if (!m.on || !m.code) continue;
        key += "|" + m.path;
        // (the same mod imported again with other files: another answer)
        for (const char* rel : {"exefs/code.ips", "exefs/code.bps", "exefs/code.bin", "code.ips", "code.bps", "code.bin"}) {
            std::error_code ec;
            const fs::path p = fs::path(m.path) / rel;
            const auto size = fs::file_size(p, ec);
            if (ec) continue;
            key += ":" + std::to_string(static_cast<unsigned long long>(size)) + "@" +
                   std::to_string(static_cast<long long>(fs::last_write_time(p, ec).time_since_epoch().count()));
        }
    }
    return key;
}

}  // namespace

void content_init(const std::string& user_dir) {
    g_user = user_dir;
    while (!g_user.empty() && g_user.back() == '/') g_user.pop_back();
}

const std::string& content_user_dir() { return g_user; }

void content_add_port_cheats(const std::string& text) {
    // (once per user folder: a marker beside the cheats file, so a test run's folder or another one gets its own)
    const std::string marker = CheatsDir() + "/app_cheats_added.txt";
    if (g_user.empty() || text.empty() || access(marker.c_str(), F_OK) == 0) return;
    const std::string path = mh4n::cheats::CheatFilePath(g_user, kContentTitle);
    std::string file;
    {
        std::ifstream in(path, std::ios::binary);
        file.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    // the asset's blocks: each from its [Name] line to the next one (the asset has nothing before its first)
    std::vector<std::pair<std::string, std::string>> blocks;
    size_t at = text.find('[');
    while (at != std::string::npos) {
        size_t next = text.find("\n[", at);
        next = next == std::string::npos ? text.size() : next + 1;
        const size_t eol = text.find('\n', at);
        blocks.emplace_back(text.substr(at, (eol == std::string::npos ? text.size() : eol) - at), text.substr(at, next - at));
        at = next < text.size() ? next : std::string::npos;
    }
    std::string add;
    int count = 0;
    for (auto& [name, block] : blocks) {
        std::string line = name;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (file.find(line) != std::string::npos) continue;  // (already there, on or off)
        while (!block.empty() && (block.back() == '\n' || block.back() == '\r')) block.pop_back();
        add += block + "\n\n";
        count++;
    }
    if (!add.empty()) {
        mkdir(CheatsDir().c_str(), 0770);
        std::ofstream out(path, std::ios::binary | std::ios::app);
        if (!file.empty() && file.back() != '\n') out << "\n";
        if (!file.empty()) out << "\n";
        out << add;
        if (!out) {
            Log("the app's cheats: could not write %s", path.c_str());
            return;
        }
    }
    mkdir(CheatsDir().c_str(), 0770);
    std::ofstream note(marker);
    note << "The app's own cheats (faster eating and drinking) were added to this folder's cheats file once.\n";
    Log("the app's cheats: %d added to %s, off", count, path.c_str());
}

// ---- cheats ---------------------------------------------------------------------------------------------------
std::vector<ContentCheat> content_cheats(bool* ready) {
    std::vector<ContentCheat> out;
    const auto engine = mh4n::cheats::Current();
    if (ready) *ready = engine != nullptr;
    if (!engine) return out;
    for (const mh4n::cheats::CheatInfo& c : engine->List()) {
        ContentCheat x;
        x.name = c.name, x.comments = c.comments, x.code = c.code;
        x.on = c.enabled, x.touches_code = c.touches_code, x.invalid_lines = c.invalid_lines;
        out.push_back(std::move(x));
    }
    return out;
}

bool content_cheats_apply(const std::vector<char>& on, std::string* error) {
    const auto engine = mh4n::cheats::Current();
    if (!engine) {
        if (error) *error = "the game has not started";
        return false;
    }
    for (size_t i = 0; i < on.size(); i++) engine->SetEnabled(i, on[i] != 0);
    if (!engine->Save(error)) return false;
    Log("cheats saved: %zu on", engine->EnabledCount());
    return true;
}

bool content_cheat_remove(size_t index) {
    const auto engine = mh4n::cheats::Current();
    if (!engine || !engine->Remove(index)) return false;
    return engine->Save();
}

void content_cheats_import() {
    if (g_user.empty()) return;
    unlink((CheatsDir() + "/import_result.txt").c_str());
    app_call_activity("showCheatImport");
}

ContentImport content_cheats_import_result() {
    ContentImport r = ReadOutcome(CheatsDir() + "/import_result.txt");
    if (!r.done || !r.ok) {
        if (r.done) Log("cheats import failed: %s (%s)", r.error.c_str(), r.detail.c_str());
        return r;
    }
    const std::string file = CheatsDir() + "/import.txt";
    const auto engine = mh4n::cheats::Current();
    if (!engine) {
        r.ok = false, r.error = "not_started";
    } else {
        std::string why;
        r.count = engine->Import(file, &why);
        if (r.count < 0) {
            r.ok = false, r.error = why;
        } else if (!engine->Save(&why)) {
            r.ok = false, r.error = "unwritable", r.detail = why;
        }
    }
    unlink(file.c_str());
    if (r.ok) Log("cheats imported from %s: %d", r.name.c_str(), r.count);
    else Log("cheats import failed: %s (%s)", r.error.c_str(), r.detail.c_str());
    return r;
}

// ---- mods -----------------------------------------------------------------------------------------------------
std::vector<mh4n::mods::Mod> content_mods() {
    if (g_user.empty()) return {};
    return mh4n::mods::List(ModsDir(), kContentTitle);
}

bool content_mods_save(const std::vector<mh4n::mods::Mod>& list, std::string* error) {
    if (g_user.empty()) return false;
    if (!mh4n::mods::SaveOrder(ModsDir(), list, error)) return false;
    std::string names;
    for (const auto& m : list) names += (names.empty() ? "" : ", ") + m.name + (m.on ? "" : " (off)");
    Log("mods saved: %s", names.c_str());
    return true;
}

bool content_mod_remove(const std::string& name) {
    if (g_user.empty() || name.empty() || name.find('/') != std::string::npos || name[0] == '.') return false;
    for (const auto& m : content_mods()) {
        if (m.name != name) continue;
        if (m.from_game_folder) return false;
        std::error_code ec;
        fs::remove_all(ModsDir() + name, ec);
        Log("mod %s removed%s", name.c_str(), ec ? (": " + ec.message()).c_str() : "");
        return !ec;
    }
    return false;
}

void content_mods_import(bool folder) {
    if (g_user.empty()) return;
    unlink((ModsDir() + "import_result.txt").c_str());
    app_call_activity(folder ? "showModFolderImport" : "showModImport");
}

ContentImport content_mods_import_result() {
    ContentImport r = ReadOutcome(ModsDir() + "import_result.txt");
    if (r.done) {
        if (r.ok) Log("mod %s added%s", r.name.c_str(), r.code ? " (it changes the code)" : "");
        else Log("mod import failed: %s (%s)", r.error.c_str(), r.detail.c_str());
    }
    return r;
}

// ---- what the mods change in common
namespace {

// The game files a mod puts over the game's: romfs/ as they lie, romfs_ext/ by the file each patch (.ips, .bps, .stub) is for
std::vector<std::string> ChangedFiles(const mh4n::mods::Mod& m) {
    std::vector<std::string> out;
    for (const char* part : {"romfs", "romfs_ext"}) {
        const fs::path root = fs::path(m.path) / part;
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        const bool ext = std::string(part) == "romfs_ext";
        for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::string rel = it->path().lexically_relative(root).generic_string();
            if (ext) {
                for (const char* patch : {".ips", ".bps", ".stub"}) {
                    const size_t n = strlen(patch);
                    if (rel.size() > n && !strcasecmp(rel.c_str() + rel.size() - n, patch)) rel.resize(rel.size() - n);
                }
            }
            out.push_back(std::move(rel));
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

}  // namespace

std::shared_ptr<ContentModScan> content_mods_scan(const std::vector<mh4n::mods::Mod>& list) {
    auto scan = std::make_shared<ContentModScan>();
    for (const auto& m : list) scan->names.push_back(m.name);
    std::thread([scan, list]() {
        std::vector<std::vector<std::string>> files;
        for (const auto& m : list) files.push_back(ChangedFiles(m));
        std::vector<int> counts;
        std::vector<ContentModScan::Pair> pairs;
        for (size_t a = 0; a < files.size(); a++) {
            counts.push_back(static_cast<int>(files[a].size()));
            for (size_t b = a + 1; b < files.size(); b++) {
                std::vector<std::string> both;
                std::set_intersection(files[a].begin(), files[a].end(), files[b].begin(), files[b].end(), std::back_inserter(both));
                if (!both.empty()) pairs.push_back({static_cast<int>(a), static_cast<int>(b), static_cast<int>(both.size()), both[0]});
            }
        }
        scan->files = std::move(counts);
        scan->conflicts = std::move(pairs);
        Log("mods compared: %d mods, %d pairs change the same files", static_cast<int>(list.size()),
            static_cast<int>(scan->conflicts.size()));
        scan->done.store(true);
    }).detach();
    return scan;
}

ContentCodeCheck content_code_check(const std::vector<mh4n::mods::Mod>& list) {
    const mh4n::mods::LibraryId lib = mh4n::mods::RunningLibrary();
    const std::string game = mh4n::mods::RunningGameFolder();
    if (!lib.known || game.empty()) return {};  // (the game hasn't started: nothing to tell)
    const std::string key = CheckKey(list, lib);
    {
        std::lock_guard lock(g_check_mutex);
        auto it = g_checks.find(key);
        if (it != g_checks.end()) return it->second;
        ContentCodeCheck& c = g_checks[key];
        c.busy = true;
    }
    std::thread([key, game, list, lib]() {
        const mh4n::mods::CodeCheck c = mh4n::mods::CheckCode(game, list, lib);
        std::lock_guard lock(g_check_mutex);
        ContentCodeCheck& out = g_checks[key];
        out.busy = false;
        out.known = c.error.empty();
        out.matches = c.matches;
        out.error = c.error;
        Log("code library %s the code mods %s%s", c.matches ? "matches" : "does not match", key.c_str(),
            c.error.empty() ? "" : (" (" + c.error + ")").c_str());
    }).detach();
    ContentCodeCheck busy;
    busy.busy = true;
    return busy;
}
