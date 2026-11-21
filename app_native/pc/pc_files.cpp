// MH4U for PC: what the Android app's Java screens do with files and folders (pc_app.h pc_files_action), on Windows: the
// system's file and folder dialogs (SDL's), then the same work as the Java side's on the dialog's own thread; what the
// player has to know comes back over the game (settings_panel_notice, on the main thread), in a question (a message box on
// the main thread) or, for what is applied before the game starts, in a message box then. The same rules, texts and file
// layouts as Android's:
//   - Log folder (Share log on Android): File Explorer on the program's folder, log.txt picked.
//   - Licenses: the licenses folder beside MH4U.exe, in File Explorer.
//   - Cheats and mods (ImportActivity.java): a .txt, or a mod's .zip or folder, into the user folder; the outcome in
//     import_result.txt, which content.cpp reads as on Android.
//   - The game folder's mods/ into the user folder's load/mods at each start (GameFolderActivity.syncMods).
//   - Saves (SaveActivity, SaveFolder, SaveFolderActivity): Import save and Save folder are staged and applied before the
//     game starts again (the game holds the save in memory and writes it at any moment), the replaced save copied to
//     save_backups first; Export save copies it into a folder of the player's.
//   - DLC and event quests (ExtraImportActivity, DlcImport): the DLC installed into the game's own folder at once, the
//     quest files staged and put into the extra data before the game starts again.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "common/utf8.h"
#include "pc_app.h"
#include "settings_panel.h"
#include "unzip.h"
#include "verify_files.h"

#include <windows.h>  // (last: its macros)
#include <commctrl.h>
#include <shellapi.h>
#include <wincodec.h>

namespace fs = std::filesystem;

namespace {

const char* const TITLES[] = {"00126300", "00126100"};  // the USA game's save title, Europe's
const char* const ZEROS = "00000000000000000000000000000000";
constexpr const char* kTitle = "0004000000126300";
constexpr const char* kUpdate = "0004000E00126300";
constexpr uint64_t kSystemSize = 512, kUserSize = 81408, kUserMax = 512 * 1024, kMaxCheats = 4u << 20, kQuestMax = 1u << 20;
// the save archive's format record (FormatSaveData's values for MH4U, the same in Citra, Azahar and here)
const unsigned char kMetadata[16] = {0, 0, 4, 0, 0, 0, 0, 0, 4, 0, 0, 0, 1, 0, 0, 0};

// ---- paths and files (UTF-8 names, as everywhere in the program)
fs::path P(const std::string& utf8) { return mh4n::utf8::PathFromUtf8(utf8); }
std::string U(const fs::path& p) { return mh4n::utf8::PathToUtf8(p); }
std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool IsDir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}
bool IsFile(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}
uint64_t SizeOf(const fs::path& p) {
    std::error_code ec;
    const auto n = fs::file_size(p, ec);
    return ec ? 0 : n;
}
fs::file_time_type TimeOf(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    return ec ? fs::file_time_type::min() : t;
}
void DeleteTree(const fs::path& p) {
    std::error_code ec;
    fs::remove_all(p, ec);
}
std::vector<fs::path> Kids(const fs::path& dir, bool dirs) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (dirs ? it->is_directory(e2) : it->is_regular_file(e2)) out.push_back(it->path());
    }
    std::sort(out.begin(), out.end());
    return out;
}
std::string Name(const fs::path& p) { return U(p.filename()); }
// d's child named `name` in any capitals (a folder, or a file)
fs::path Child(const fs::path& d, const std::string& name, bool folder) {
    for (const fs::path& k : Kids(d, folder))
        if (Lower(Name(k)) == Lower(name)) return k;
    return {};
}
struct Fail {
    std::string code, detail;
};
// A file copied whole (beside its place first, then renamed; its date kept): throws Fail
void CopyChecked(const fs::path& src, const fs::path& dst) {
    std::error_code ec;
    fs::create_directories(dst.parent_path(), ec);
    fs::path tmp = dst;
    tmp += ".mh4u-copy";
    fs::remove(tmp, ec);
    if (!fs::copy_file(src, tmp, fs::copy_options::overwrite_existing, ec) || SizeOf(tmp) != SizeOf(src)) {
        fs::remove(tmp, ec);
        throw Fail{"unwritable", "couldn't copy " + U(src) + (ec ? ": " + ec.message() : "")};
    }
    fs::rename(tmp, dst, ec);  // (std::filesystem's rename replaces a file on Windows)
    if (ec) {
        fs::remove(tmp, ec);
        throw Fail{"unwritable", "couldn't replace " + U(dst)};
    }
    fs::last_write_time(dst, TimeOf(src), ec);
}
void CopyTree(const fs::path& from, const fs::path& to) {
    if (IsDir(from)) {
        std::error_code ec;
        fs::create_directories(to, ec);
        if (ec) throw Fail{"unwritable", "couldn't make " + U(to)};
        for (const fs::path& f : Kids(from, false)) CopyChecked(f, to / f.filename());
        for (const fs::path& d : Kids(from, true)) CopyTree(d, to / d.filename());
        return;
    }
    CopyChecked(from, to);
}
void WriteText(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
}
std::string ReadText(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}
std::vector<std::string> Lines(const fs::path& p) {
    std::vector<std::string> out;
    std::ifstream in(p);
    for (std::string l; std::getline(in, l);) {
        while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
        out.push_back(l);
    }
    return out;
}
std::string Stamp(const char* format = "%Y-%m-%d_%H-%M-%S") {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &now);
    char b[64];
    std::strftime(b, sizeof b, format, &tm);
    return b;
}
void Log(const std::string& line) { std::fprintf(stderr, "[files] %s\n", line.c_str()); }
// A test's variable, as the process has it now (the restart hands the next stage's on: Restart)
std::string TestEnv(const char* name) {
    char v[1024];
    const DWORD n = GetEnvironmentVariableA(name, v, sizeof v);
    return n > 0 && n < sizeof v ? std::string(v, n) : std::string();
}
std::string OneLine(std::string s) {
    for (char& c : s)
        if (c == '\n' || c == '\r') c = ' ';
    return s;
}

// ---- the program's places (pc_app.h)
fs::path Home() { return P(pc_paths().home); }
fs::path User() { return P(pc_paths().user); }
fs::path ModsDir() { return User() / "load" / "mods"; }
fs::path SaveRoot() { return P(pc_paths().save_folder); }  // (the folder that holds sdmc/)
fs::path Backups() { return P(pc_paths().backups); }
fs::path Sdmc(const fs::path& root) { return root / "sdmc" / "Nintendo 3DS" / ZEROS / ZEROS; }
fs::path DataDir(const fs::path& root, const std::string& t) { return Sdmc(root) / "title" / "00040000" / t / "data" / "00000001"; }
fs::path MetaFile(const fs::path& root, const std::string& t) {
    return Sdmc(root) / "title" / "00040000" / t / "data" / "00000001.metadata";
}
std::string ExtId(const std::string& t) { return t == "00126100" ? "00001261" : "00001263"; }
fs::path ExtDir(const fs::path& root, const std::string& t) { return Sdmc(root) / "extdata" / "00000000" / ExtId(t); }

// The game's program id from the game files' exheader (0: none)
uint64_t ProgramId() {
    std::ifstream in(P(pc_paths().game_files) / "exheader.bin", std::ios::binary);
    unsigned char e[0x208];
    if (!in.read(reinterpret_cast<char*>(e), sizeof e)) return 0;
    uint64_t id = 0;
    for (int i = 7; i >= 0; i--) id = (id << 8) | e[0x200 + i];
    return id;
}
// The game the program runs, as its save's title folder is named: the exheader's (an update's id counts as the game's),
// else the one with a save, else the USA one (SaveFolder.gameTitle)
std::string GameTitle() {
    const uint64_t id = ProgramId();
    char low[16];
    std::snprintf(low, sizeof low, "%08llx", static_cast<unsigned long long>(id & 0xffffffffull));
    if (((id >> 32) & ~0xEull) == 0x00040000ull)
        for (const char* t : TITLES)
            if (low == std::string(t)) return t;
    for (const char* t : TITLES)
        if (IsFile(DataDir(SaveRoot(), t) / "system")) return t;
    return TITLES[0];
}

// ---- the main thread's: notices, questions, the restart
SDL_Window* GameWindow() {
    int n = 0;
    SDL_Window** list = SDL_GetWindows(&n);
    SDL_Window* w = list != nullptr && n > 0 ? list[0] : nullptr;
    SDL_free(list);
    return w;
}
bool Spanish() { return settings_spanish(); }
struct NoticeArg {
    std::string en, es;
    double seconds;
};
void SDLCALL NoticeOnMain(void* ud) {
    std::unique_ptr<NoticeArg> a(static_cast<NoticeArg*>(ud));
    settings_panel_notice(a->en, a->es, a->seconds);
}
// A notice over the game, from any thread
void Notice(const std::string& en, const std::string& es, double seconds = 10) {
    Log(en);
    SDL_RunOnMainThread(NoticeOnMain, new NoticeArg{en, es, seconds}, false);
}
struct AskArg {
    std::string title, text;
    std::vector<std::string> buttons;
    int answer = -1;
};
// Windows' task dialog (comctl32 6, MH4U.manifest): the question as Windows' own, its text wrapped and its buttons as wide
// as their words at any display scaling (SDL's box cut long ones short at 150 %); false when it can't be shown
bool TaskDialogAsk(AskArg* a) {
    using Fn = HRESULT(WINAPI*)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);
    static const Fn fn = reinterpret_cast<Fn>(
        reinterpret_cast<void*>(GetProcAddress(LoadLibraryW(L"comctl32.dll"), "TaskDialogIndirect")));
    if (fn == nullptr) return false;
    std::vector<std::wstring> labels;
    for (const std::string& b : a->buttons) labels.push_back(mh4n::utf8::ToWide(b));
    std::vector<TASKDIALOG_BUTTON> buttons;
    for (size_t i = 0; i < labels.size(); i++) buttons.push_back({static_cast<int>(100 + i), labels[i].c_str()});
    const std::wstring title = mh4n::utf8::ToWide(a->title), text = mh4n::utf8::ToWide(a->text);
    TASKDIALOGCONFIG c{};
    c.cbSize = sizeof c;
    if (SDL_Window* w = GameWindow())
        c.hwndParent = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    c.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    c.pszWindowTitle = L"MH4U";
    c.pszMainInstruction = title.c_str();
    c.pszContent = text.c_str();
    c.pButtons = buttons.data();
    c.cButtons = static_cast<UINT>(buttons.size());
    c.nDefaultButton = 100;
    int pressed = 0;
    if (FAILED(fn(&c, &pressed, nullptr, nullptr))) return false;
    a->answer = pressed >= 100 && pressed < 100 + static_cast<int>(buttons.size()) ? pressed - 100
                                                                                  : static_cast<int>(buttons.size()) - 1;  // (Esc)
    return true;
}
void SDLCALL AskOnMain(void* ud) {
    auto* a = static_cast<AskArg*>(ud);
    if (TaskDialogAsk(a)) return;
    std::vector<SDL_MessageBoxButtonData> b;
    for (size_t i = 0; i < a->buttons.size(); i++) {
        SDL_MessageBoxButtonData d{};
        d.buttonID = static_cast<int>(i);
        d.text = a->buttons[i].c_str();
        if (i == 0) d.flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
        if (i + 1 == a->buttons.size()) d.flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
        b.push_back(d);
    }
    SDL_MessageBoxData m{};
    m.flags = SDL_MESSAGEBOX_INFORMATION | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT;
    m.window = GameWindow();
    m.title = a->title.c_str();
    m.message = a->text.c_str();
    m.numbuttons = static_cast<int>(b.size());
    m.buttons = b.data();
    if (!SDL_ShowMessageBox(&m, &a->answer)) a->answer = -1;
}
// A question with buttons (the first one the default, the last one Esc's), on the main thread; the button chosen, -1 none.
// MH4U_TEST_ANSWER=<n> (a test) answers every question with button n, unasked
int Ask(const std::string& title, const std::string& text, const std::vector<std::string>& buttons) {
    Log("asked: " + title + ": " + OneLine(text));
    if (const std::string forced = TestEnv("MH4U_TEST_ANSWER"); !forced.empty()) {
        const int n = SDL_atoi(forced.c_str());
        Log("answered by MH4U_TEST_ANSWER: " + std::to_string(n));
        return n < static_cast<int>(buttons.size()) ? n : static_cast<int>(buttons.size()) - 1;
    }
    AskArg a{title, text, buttons};
    if (SDL_IsMainThread()) AskOnMain(&a);
    else SDL_RunOnMainThread(AskOnMain, &a, true);
    Log("answer: " + std::to_string(a.answer));
    return a.answer;
}
void SDLCALL RestartOnMain(void*) { pc_action("restartApp"); }
// The game closes and starts again (the program's restart: pc_main.cpp). Tests: MH4U_TEST_<X>_NEXT becomes MH4U_TEST_<X>
// for the program it starts, and _NEXT2 its _NEXT (ACTIONS, PICK, ANSWER: a test of an import and of its applying at the
// next start, or of a change and its undoing)
void Restart() {
    for (const char* x : {"ACTIONS", "PICK", "ANSWER"}) {
        const std::string now = std::string("MH4U_TEST_") + x, next = now + "_NEXT", next2 = now + "_NEXT2";
        const std::string v = TestEnv(next.c_str()), v2 = TestEnv(next2.c_str());
        if (v.empty() && v2.empty() && TestEnv(now.c_str()).empty()) continue;
        SetEnvironmentVariableA(now.c_str(), v.empty() ? nullptr : v.c_str());
        SetEnvironmentVariableA(next.c_str(), v2.empty() ? nullptr : v2.c_str());
        SetEnvironmentVariableA(next2.c_str(), nullptr);
    }
    SDL_RunOnMainThread(RestartOnMain, nullptr, false);
}

// The system's dialogs: the work goes on in `then` with the path picked, on a thread of its own. MH4U_TEST_PICK=<path>
// (a test; paths with ';' between: one for each dialog, in turn) answers the next dialog with that path, unshown
struct Dialog {
    std::function<void(const std::string&)> then;
};
void SDLCALL Picked(void* ud, const char* const* list, int) {
    std::shared_ptr<Dialog> d(static_cast<Dialog*>(ud));
    if (list == nullptr || list[0] == nullptr) return;  // (cancelled, or the dialog failed)
    const std::string picked = list[0];
    Log("picked: " + picked);
    std::thread([d, picked] { d->then(picked); }).detach();
}
bool TestPick(Dialog* d) {
    std::string forced = TestEnv("MH4U_TEST_PICK");
    if (forced.empty()) return false;
    const size_t cut = forced.find(';');
    SetEnvironmentVariableA("MH4U_TEST_PICK", cut == std::string::npos ? nullptr : forced.c_str() + cut + 1);
    if (cut != std::string::npos) forced.resize(cut);
    const char* list[] = {forced.c_str(), nullptr};
    Picked(d, list, 0);
    return true;
}
void PickFolder(std::function<void(const std::string&)> then) {
    auto* d = new Dialog{std::move(then)};
    if (!TestPick(d)) SDL_ShowOpenFolderDialog(Picked, d, GameWindow(), nullptr, false);
}
void PickFile(const SDL_DialogFileFilter* filters, int n, std::function<void(const std::string&)> then) {
    auto* d = new Dialog{std::move(then)};
    if (!TestPick(d)) SDL_ShowOpenFileDialog(Picked, d, GameWindow(), filters, n, nullptr, false);
}
// File Explorer on a folder, or on a file's folder with it picked. MH4U_TEST_NO_EXPLORER (a test) only logs it
void Explore(const fs::path& path, bool select) {
    const fs::path p = fs::path(path).make_preferred();  // (File Explorer's /select wants backslashes only)
    Log("File Explorer: " + U(p) + (select ? " (picked)" : ""));
    if (!TestEnv("MH4U_TEST_NO_EXPLORER").empty()) return;
    const std::wstring w = p.wstring();
    if (select) {
        const std::wstring args = L"/select,\"" + w + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    } else {
        ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

// ---- cheats and mods (ImportActivity.java)
void ImportResult(const fs::path& dir, const std::string& text) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    WriteText(dir / "import_result.txt.tmp", text);
    fs::rename(dir / "import_result.txt.tmp", dir / "import_result.txt", ec);
}

void ImportCheats(const std::string& picked) {
    const fs::path dir = User() / "cheats", src = P(picked);
    std::string outcome;
    try {
        const uint64_t n = SizeOf(src);
        if (n > kMaxCheats) throw Fail{"too_large", std::to_string(n) + " bytes"};
        std::ifstream in(src);
        bool section = false;
        for (std::string l; !section && std::getline(in, l);) {
            while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
            while (!l.empty() && l.front() == ' ') l.erase(0, 1);
            section = l.size() > 2 && l.front() == '[' && l.back() == ']';
        }
        if (!section) throw Fail{"no_cheats", ""};
        CopyChecked(src, dir / "import.txt");
        outcome = "ok\n" + OneLine(Name(src)) + "\n";
    } catch (const Fail& f) {
        outcome = "error\n" + f.code + "\n" + OneLine(f.detail) + "\n";
    }
    ImportResult(dir, outcome);
    Log("cheats import from " + picked + ": " + OneLine(outcome));
}

bool IsTitle(const std::string& n) {
    if (n.size() != 16) return false;
    for (char c : n)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    return true;
}
bool IsOurs(const std::string& n) {
    const std::string l = Lower(n);
    return l == "0004000000126300" || l == "0004000e00126300" || l == "0004000000126100" || l == "0004000e00126100";
}
bool IsModRoot(const fs::path& d) {
    for (const char* part : {"romfs", "romfs_ext", "exefs"})
        if (!Child(d, part, true).empty()) return true;
    for (const char* file : {"code.ips", "code.bps", "code.bin"})
        if (IsFile(d / file)) return true;
    return false;
}
const char* const ROMFS_TOP[] = {"data", "eng", "fre", "ger", "ita", "spa", "jpn"};
fs::path FindGameFolders(const fs::path& d, int depth) {
    for (const char* top : ROMFS_TOP)
        if (!Child(d, top, true).empty()) return d;
    if (depth >= 3) return {};
    for (const fs::path& k : Kids(d, true))
        if (fs::path r = FindGameFolders(k, depth + 1); !r.empty()) return r;
    return {};
}
int Rank(const std::string& name) {
    const std::string l = Lower(name);
    return l == Lower(kTitle) ? 0 : l == Lower(kUpdate) ? 1 : IsTitle(name) ? 3 : 2;
}
fs::path FindRoot(const fs::path& d, int depth) {
    if (IsModRoot(d)) return d;
    if (depth >= 4) return {};
    std::vector<fs::path> kids = Kids(d, true);
    std::stable_sort(kids.begin(), kids.end(), [](const fs::path& a, const fs::path& b) {
        const int ra = Rank(Name(a)), rb = Rank(Name(b));
        return ra != rb ? ra < rb : Lower(Name(a)) < Lower(Name(b));
    });
    for (const fs::path& k : kids)
        if (fs::path r = FindRoot(k, depth + 1); !r.empty()) return r;
    return {};
}
// The parts' names as the runtime reads them (RomFS/ -> romfs/, ExeFS/ -> exefs/, Code.ips -> code.ips)
void LowerParts(const fs::path& root) {
    std::vector<std::pair<fs::path, bool>> parts;
    std::error_code ec;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        parts.push_back({it->path(), it->is_directory(e2)});
    }
    for (const auto& [path, dir] : parts) {
        const std::string name = Name(path), low = Lower(name);
        const bool part = dir ? low == "romfs" || low == "romfs_ext" || low == "exefs"
                              : low == "code.ips" || low == "code.bps" || low == "code.bin";
        if (!part || low == name) continue;
        std::error_code e2;
        fs::rename(path, root / (".rename-" + low), e2);  // (two steps: Windows' names ignore capitals)
        if (!e2) fs::rename(root / (".rename-" + low), root / low, e2);
        if (e2) throw Fail{"unwritable", U(root / low)};
    }
}
bool ChangesCode(const fs::path& d) {
    for (const char* f : {"exefs/code.ips", "exefs/code.bps", "exefs/code.bin", "code.ips", "code.bps", "code.bin"})
        if (IsFile(d / f)) return true;
    return false;
}
std::string SafeName(const std::string& s) {
    std::string n;
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        n += u < 0x80 && (std::isalnum(u) || std::strchr(" ._()[]+-", c) != nullptr) ? c : '_';
    }
    while (!n.empty() && (n.front() == '.' || n.front() == '_' || n.front() == ' ')) n.erase(0, 1);
    while (!n.empty() && n.back() == ' ') n.pop_back();
    if (n.size() > 60) n.resize(60);
    return n;
}
std::set<std::string> GameFolderMods() {
    std::set<std::string> out;
    for (const std::string& l : Lines(ModsDir() / ".from_game_folder")) {
        const std::string name = l.substr(0, l.find('\t'));
        if (!name.empty()) out.insert(name);
    }
    return out;
}
// A mod that changes the code starts off: load_order.txt gets "# off: <name>" unless it lists the name already
void KeepOff(const fs::path& mods, const std::string& name) {
    const fs::path list = mods / "load_order.txt";
    std::vector<std::string> lines = Lines(list);
    for (const std::string& l : lines)
        if (l == name || l == "# off: " + name) return;
    if (lines.empty()) lines.push_back("# Written by the app: the mods in load order, one per line; \"# off: name\" keeps one that is off.");
    lines.push_back("# off: " + name);
    std::string text;
    for (const std::string& l : lines) text += l + "\n";
    WriteText(list, text);
}
// A .zip unpacked into `to` (nothing outside it)
void Unpack(const std::string& zip, const fs::path& to) {
    Unzip z;
    if (unzip_open(&z, zip.c_str()) != UNZIP_OK) {
        unzip_close(&z);
        throw Fail{"not_zip", SDL_GetError()};
    }
    bool any = false;
    for (int i = 0; i < z.count; i++) {
        const UnzipEntry& e = z.entries[i];
        const std::string rel = e.name;
        bool bad = rel.empty() || rel[0] == '/' || rel.find(':') != std::string::npos;
        for (size_t at = 0; !bad;) {
            const size_t end = rel.find('/', at);
            if (rel.substr(at, end == std::string::npos ? std::string::npos : end - at) == "..") bad = true;
            if (end == std::string::npos) break;
            at = end + 1;
        }
        if (bad) continue;
        any = true;
        const fs::path out = to / P(rel);
        std::error_code ec;
        if (e.dir) {
            fs::create_directories(out, ec);
            continue;
        }
        fs::create_directories(out.parent_path(), ec);
        const UnzipResult r = unzip_extract(&z, i, U(out).c_str(), nullptr, nullptr);
        if (r != UNZIP_OK) {
            const std::string why = std::string(e.name) + ": " + SDL_GetError();
            unzip_close(&z);
            throw Fail{r == UNZIP_WRITE ? "unwritable" : "unreadable", why};
        }
    }
    unzip_close(&z);
    if (!any) throw Fail{"not_zip", ""};
}

void ImportMod(const std::string& picked, bool folder) {
    const fs::path mods = ModsDir(), tmp = mods / ".new";
    std::string outcome;
    try {
        std::error_code ec;
        DeleteTree(tmp);
        fs::create_directories(tmp, ec);
        if (ec) throw Fail{"unwritable", U(tmp)};
        std::string shown = Name(P(picked));
        if (folder) {
            CopyTree(P(picked), tmp);
        } else {
            if (Lower(shown).ends_with(".zip")) shown.resize(shown.size() - 4);
            Unpack(picked, tmp);
        }
        fs::path root = FindRoot(tmp, 0);
        if (root.empty()) {  // the game's own folders without a romfs/ around them: into one
            root = FindGameFolders(tmp, 0);
            if (root.empty()) throw Fail{"no_parts", ""};
            const fs::path romfs = root / ".romfs-new";
            fs::create_directories(romfs, ec);
            for (const char* top : ROMFS_TOP)
                if (fs::path g = Child(root, top, true); !g.empty()) {
                    fs::rename(g, romfs / g.filename(), ec);
                    if (ec) throw Fail{"unwritable", U(g)};
                }
            fs::rename(romfs, root / "romfs", ec);
            if (ec) throw Fail{"unwritable", U(romfs)};
            Log("mod import: the game's own folders put in a romfs/");
        }
        LowerParts(root);
        for (fs::path p = root; p != tmp && p.has_relative_path() && p != p.parent_path(); p = p.parent_path())
            if (IsTitle(Name(p)) && !IsOurs(Name(p))) throw Fail{"not_this_game", Name(p)};
        std::string name = SafeName(shown);
        if (name.empty() || IsTitle(name) || Lower(name) == "romfs" || Lower(name) == "exefs" || Lower(name) == "mods") {
            const std::string own = root == tmp ? "" : SafeName(Name(root));
            name = own.empty() || IsTitle(own) ? "Mod" : own;
        }
        const std::set<std::string> from_game = GameFolderMods();
        const std::string base = name;
        for (int i = 2; from_game.count(name) || Lower(name) == Lower(kTitle); i++) name = base + " (" + std::to_string(i) + ")";
        const bool code = ChangesCode(root);
        const fs::path to = mods / P(name);
        DeleteTree(to);  // (the same mod again: the new copy replaces it, its place and state kept)
        fs::rename(root, to, ec);
        if (ec) throw Fail{"unwritable", U(to)};
        if (code) KeepOff(mods, name);
        outcome = "ok\n" + name + "\n" + (code ? "1" : "0") + "\n";
    } catch (const Fail& f) {
        outcome = "error\n" + f.code + "\n" + OneLine(f.detail) + "\n";
    }
    DeleteTree(tmp);
    ImportResult(mods, outcome);
    Log("mod import from " + picked + ": " + OneLine(outcome));
}

// ---- saves (SaveActivity.java, SaveFolder.java)
struct SaveInfo {
    bool valid = false;
    int hunters = 0;
    fs::file_time_type time = fs::file_time_type::min();
};
SaveInfo DescribeSave(const fs::path& root) {
    SaveInfo i;
    for (const char* t : TITLES) {
        const fs::path d = DataDir(root, t);
        if (SizeOf(d / "system") != kSystemSize) continue;
        i.valid = true;
        i.time = std::max(i.time, TimeOf(d / "system"));
        for (int u = 1; u <= 3; u++)
            if (const fs::path f = d / ("user" + std::to_string(u)); SizeOf(f) > 0) i.hunters++, i.time = std::max(i.time, TimeOf(f));
    }
    return i;
}
std::string When(fs::file_time_type t) {
    if (t == fs::file_time_type::min()) return "?";
    const auto sys = std::chrono::system_clock::now() +
                     std::chrono::duration_cast<std::chrono::system_clock::duration>(t - fs::file_time_type::clock::now());
    const std::time_t tt = std::chrono::system_clock::to_time_t(sys);
    std::tm tm{};
    localtime_s(&tm, &tt);
    char b[32];
    std::strftime(b, sizeof b, "%Y-%m-%d %H:%M", &tm);
    return b;
}
std::string SaveLine(const SaveInfo& i, bool es) {
    if (!i.valid) return es ? "sin partida" : "no save";
    const std::string who = es ? (i.hunters == 1 ? "1 cazador" : std::to_string(i.hunters) + " cazadores")
                               : (i.hunters == 1 ? "1 hunter" : std::to_string(i.hunters) + " hunters");
    return who + (es ? " · guardada el " : " · saved ") + When(i.time);
}
// A copy of the save of root in save_backups/<date>-<what>/<title>/{data, extdata, 00000001.metadata}
fs::path Backup(const fs::path& root, const std::string& what) {
    fs::path keep = Backups() / (Stamp() + "-" + what);
    for (int n = 2; IsDir(keep); n++) keep = Backups() / (Stamp() + "-" + what + "-" + std::to_string(n));
    for (const char* t : TITLES) {
        if (!IsDir(DataDir(root, t))) continue;
        CopyTree(DataDir(root, t), keep / t / "data");
        if (IsFile(MetaFile(root, t))) CopyChecked(MetaFile(root, t), keep / t / "00000001.metadata");
        if (IsDir(ExtDir(root, t))) CopyTree(ExtDir(root, t), keep / t / "extdata");
    }
    Log("the save of " + U(root) + " kept in " + U(keep));
    return keep;
}
void Swap(const fs::path& staged, const fs::path& place, const fs::path& aside) {
    std::error_code ec;
    fs::create_directories(place.parent_path(), ec);
    if (fs::exists(place, ec)) {
        fs::create_directories(aside.parent_path(), ec);
        fs::rename(place, aside, ec);
        if (ec) throw Fail{"unwritable", "couldn't move " + U(place) + " aside"};
    }
    fs::rename(staged, place, ec);
    if (ec) {
        std::error_code e2;
        if (fs::exists(aside, e2)) fs::rename(aside, place, e2);  // (the old one back)
        throw Fail{"unwritable", "couldn't put " + U(place) + " in place"};
    }
}
// The save of one root (the folder that holds sdmc/) into another, for each title it has: copied first into
// <to>/.mh4u-incoming (each file checked), then each part takes the old one's place by renames
void CopySave(const fs::path& from, const fs::path& to) {
    const fs::path incoming = to / ".mh4u-incoming", old = to / ".mh4u-old";
    DeleteTree(incoming);
    DeleteTree(old);
    std::vector<std::string> titles;
    for (const char* t : TITLES) {
        if (!IsFile(DataDir(from, t) / "system")) continue;
        titles.push_back(t);
        CopyTree(DataDir(from, t), incoming / t / "data");
        if (IsFile(MetaFile(from, t))) CopyChecked(MetaFile(from, t), incoming / t / "meta");
        if (IsDir(ExtDir(from, t))) CopyTree(ExtDir(from, t), incoming / t / "extdata");
    }
    for (const std::string& t : titles) {
        Swap(incoming / t / "data", DataDir(to, t), old / t / "data");
        if (IsFile(incoming / t / "meta")) Swap(incoming / t / "meta", MetaFile(to, t), old / t / "meta");
        if (IsDir(incoming / t / "extdata")) Swap(incoming / t / "extdata", ExtDir(to, t), old / t / "extdata");
    }
    DeleteTree(incoming);
    DeleteTree(old);
}

// ---- Import save: what a folder holds
struct Source {
    fs::path data, ext;
    std::string title, label, problem;  // title: the game its place names ("" none); problem: why not ("" it can)
    fs::file_time_type time = fs::file_time_type::min();
};
std::map<std::string, fs::path> SaveFiles(const fs::path& dir) {
    std::map<std::string, fs::path> m;
    for (const fs::path& f : Kids(dir, false)) {
        const std::string n = Lower(Name(f));
        for (const char* s : {"system", "user1", "user2", "user3"})
            if (n == s && !m.count(n)) m[n] = f;
    }
    return m;
}
bool Quick(const fs::path& dir) {
    for (int u = 1; u <= 3; u++)
        if (IsFile(dir / ("user" + std::to_string(u)))) return true;
    return false;
}
int HunterFiles(const std::map<std::string, fs::path>& m) {
    int n = 0;
    for (int u = 1; u <= 3; u++)
        if (auto it = m.find("user" + std::to_string(u)); it != m.end() && SizeOf(it->second) > 0 && SizeOf(it->second) <= kUserMax) n++;
    return n;
}
std::string TitleOf(const fs::path& dir) {
    for (fs::path d = dir; d.has_relative_path(); d = d.parent_path()) {
        const std::string n = Lower(Name(d));
        if (n == "00126300" || n == "00001263" || n.find("0004000000126300") != std::string::npos || n.rfind("0x01263", 0) == 0) return "00126300";
        if (n == "00126100" || n == "00001261" || n.find("0004000000126100") != std::string::npos || n.rfind("0x01261", 0) == 0) return "00126100";
        if (n == "0011d700" || n == "000011d7" || n.find("000400000011d700") != std::string::npos || n.rfind("0x011d7", 0) == 0) return "0011d700";
        if (d == d.parent_path()) break;
    }
    return "";
}
fs::path ExtFor(const fs::path& data, const std::string& title) {
    const fs::path d = data.parent_path(), t = d.parent_path(), h = t.parent_path(), titles = h.parent_path(), id1 = titles.parent_path();
    if (title.empty() || Name(data) != "00000001" || Name(d) != "data" || Name(h) != "00040000" || Name(titles) != "title") return {};
    for (const std::string& id : {ExtId(title), Lower(ExtId(title))})
        if (IsDir(id1 / "extdata" / "00000000" / id / "user")) return id1 / "extdata" / "00000000" / id;
    return {};
}
std::vector<fs::path> EmulatorSaves(const fs::path& n3ds) {
    std::vector<fs::path> out;
    for (const fs::path& id0 : Kids(n3ds, true))
        for (const fs::path& id1 : Kids(id0, true))
            for (const char* t : {"00126300", "00126100", "0011d700"})
                if (const fs::path d = id1 / "title" / "00040000" / t / "data" / "00000001"; Quick(d)) out.push_back(d);
    return out;
}
bool Under(const fs::path& p, const fs::path& dir) {
    const std::string a = Lower(U(p)), b = Lower(U(dir));
    return a.size() > b.size() && a.rfind(b, 0) == 0 && (a[b.size()] == '/' || a[b.size()] == '\\');
}
bool InUse(const fs::path& dir) { return Under(dir, User()) || Under(dir, SaveRoot()); }
std::string GameName(const std::string& title, bool es) {
    return title == "00126100" ? (es ? "el europeo" : "the European one")
           : title == "0011d700" ? (es ? "el japonés (Monster Hunter 4G)" : "the Japanese one (Monster Hunter 4G)")
                                 : (es ? "el de EE. UU." : "the USA one");
}
std::string Problem(const Source& s, const std::map<std::string, fs::path>& files, const std::string& game, bool es) {
    if (InUse(s.data)) return es ? "Esa es la partida que el juego usa ahora." : "That is the save the game uses now.";
    if (s.title == "0011d700")
        return es ? "Esa partida es del juego japonés (Monster Hunter 4G): MH4U no puede leerla. No se cambió nada."
                  : "That save is from the Japanese game (Monster Hunter 4G): MH4U can't read it. Nothing was changed.";
    if (!s.title.empty() && s.title != game)
        return es ? "Esa partida es de un juego de otra región (" + GameName(s.title, es) + ") y el tuyo es " + GameName(game, es) +
                        ": una partida solo sirve en un juego de la misma región. No se cambió nada."
                  : "That save is from another region's game (" + GameName(s.title, es) + ") and yours is " + GameName(game, es) +
                        ": a save only works in a copy of the same region. Nothing was changed.";
    const auto system = files.find("system");
    if (system != files.end() && SizeOf(system->second) != kSystemSize) {
        const std::string n = std::to_string(SizeOf(system->second));
        return es ? "El archivo system de esa carpeta tiene " + n + " bytes y el de MH4U tiene 512: no es una partida de MH4U, o la copia "
                    "está dañada. No se cambió nada."
                  : "The system file in that folder is " + n + " bytes and MH4U's is 512: it isn't an MH4U save, or the copy is "
                    "damaged. Nothing was changed.";
    }
    if (system == files.end() && !IsFile(DataDir(SaveRoot(), game) / "system"))
        return es ? "En esa carpeta está el archivo del cazador pero no el archivo system que va con él. Cópialo también (está junto a "
                    "user1 en la carpeta de la partida) y vuelve a importar."
                  : "That folder has the hunter's file but not the system file that goes with it. Copy that one too (it's next to "
                    "user1 in the save's folder) and import again.";
    for (int u = 1; u <= 3; u++) {
        const auto f = files.find("user" + std::to_string(u));
        if (f != files.end() && SizeOf(f->second) > 0 && SizeOf(f->second) < kUserSize) {
            const std::string n = Name(f->second), size = std::to_string(SizeOf(f->second));
            return es ? n + " está cortado (" + size + " de 81408 bytes): cópialo otra vez, con el emulador cerrado. No se cambió nada."
                      : n + " is cut short (" + size + " of 81408 bytes): copy it again, with the emulator closed. Nothing was changed.";
        }
    }
    return "";
}
std::string Label(const Source& s, const std::map<std::string, fs::path>& files, const std::string& game, bool es) {
    const std::string p = U(s.data), low = Lower(p);
    std::string where = low.find("citra") != std::string::npos        ? "Citra"
                        : low.find("azahar") != std::string::npos     ? "Azahar"
                        : low.find("lime3ds") != std::string::npos    ? "Lime3DS"
                        : low.find("checkpoint") != std::string::npos ? "Checkpoint"
                        : low.find("jksm") != std::string::npos       ? "JKSM"
                                                                      : "";
    if (where.empty()) {
        size_t cut = low.find("/sdmc/");
        if (cut == std::string::npos) cut = low.find("\\sdmc\\");
        where = cut != std::string::npos ? p.substr(0, cut) : Name(s.data) == "00000001" ? U(s.data.parent_path()) : p;
    }
    const int users = HunterFiles(files);
    const std::string region = s.title.empty() || s.title == game ? ""
                               : " · " + std::string(s.title == "00126100"   ? (es ? "juego europeo" : "European game")
                                                     : s.title == "0011d700" ? (es ? "juego japonés" : "Japanese game")
                                                                             : (es ? "juego de EE. UU." : "USA game"));
    return where + region + " · " + std::to_string(users) +
           (es ? (users == 1 ? " personaje" : " personajes") : (users == 1 ? " hunter" : " hunters")) + " · " + When(s.time);
}
bool Candidate(const fs::path& dir, const std::string& game, bool es, Source* out) {
    if (!Quick(dir)) return false;
    const auto files = SaveFiles(dir);
    if (HunterFiles(files) == 0) return false;
    Source s;
    s.data = dir;
    s.title = TitleOf(dir);
    s.ext = ExtFor(dir, s.title);
    for (const auto& [n, f] : files) s.time = std::max(s.time, TimeOf(f));
    s.label = Label(s, files, game, es);
    s.problem = Problem(s, files, game, es);
    *out = s;
    return true;
}
void Collect(const fs::path& dir, int depth, std::vector<Source>& out, int& budget, const std::string& game, bool es) {
    if (depth > 9 || --budget < 0 || out.size() > 40) return;
    if (Name(dir) == "Nintendo 3DS") {
        for (const fs::path& d : EmulatorSaves(dir))
            if (Source s; Candidate(d, game, es, &s)) out.push_back(s);
        return;
    }
    if (Source s; Candidate(dir, game, es, &s)) {
        out.push_back(s);
        return;
    }
    for (const fs::path& k : Kids(dir, true)) {
        const std::string n = Name(k);
        if (n.empty() || n[0] == '.' || n == "content" || n == "cache" || n == "shaders") continue;
        Collect(k, depth + 1, out, budget, game, es);
    }
}
std::string NothingHere(const fs::path& dir, bool es) {
    bool ext = false, sav = false, zip = false;
    std::vector<fs::path> look{dir};
    for (size_t i = 0; i < look.size() && i < 400; i++) {
        const std::string n = Lower(Name(look[i]));
        if (n == "00001263" || n == "00001261" || n == "000011d7" || IsFile(look[i] / "cardbox") || IsFile(look[i] / "user" / "cardbox"))
            ext = true;
        for (const fs::path& k : Kids(look[i], true))
            if (!Name(k).empty() && Name(k)[0] != '.' && look.size() < 400) look.push_back(k);
        for (const fs::path& f : Kids(look[i], false)) {
            const std::string fn = Lower(Name(f));
            if (fn.ends_with(".sav")) sav = true;
            if (fn.ends_with(".zip") || fn.ends_with(".7z") || fn.ends_with(".rar")) zip = true;
        }
    }
    if (sav)
        return es ? "Ahí hay una imagen de partida (.sav, de la tarjeta SD de una consola o de un volcado del cartucho): el juego no "
                    "puede leerla así. Respáldala con Checkpoint o JKSM en la consola e importa esa carpeta."
                  : "There's a raw save image there (.sav, from a console's SD card or a cartridge dump): the game can't read it "
                    "like that. Back the save up with Checkpoint or JKSM on the console and import that folder.";
    if (ext)
        return es ? "Ahí están los datos extra (tarjetas de gremio, misiones de evento, Felyne), pero no la partida. La partida está "
                    "en title/00040000/00126300/data/00000001 (Azahar, Citra) o en la copia \"saves\" de Checkpoint. Las misiones "
                    "de evento se traen con Importar DLC y misiones de evento."
                  : "That holds the extra data (guild cards, event quests, Palico), not the save. The save is in "
                    "title/00040000/00126300/data/00000001 (Azahar, Citra) or in Checkpoint's \"saves\" backup. Event quests "
                    "come in with Import DLC and event quests.";
    const std::string base = es ? "En esa carpeta no hay una partida de MH4U. Elige la que tiene system y user1, o la carpeta sdmc de tu emulador."
                                : "There is no MH4U save in that folder. Choose the one with system and user1, or your emulator's sdmc folder.";
    return zip ? base + (es ? " Si tu partida está en un .zip, descomprímelo primero." : " If your save is in a .zip, unzip it first.") : base;
}

void ImportSave(const std::string& picked) {
    const bool es = Spanish();
    const std::string game = GameTitle();
    std::vector<Source> found;
    int budget = 4000;
    Collect(P(picked), 0, found, budget, game, es);
    const Source* best = nullptr;
    for (const Source& s : found)
        if (s.problem.empty() && (best == nullptr || s.time > best->time)) best = &s;
    if (best == nullptr && !found.empty()) best = &found[0];
    const fs::path staged = Home() / "save_import";
    std::string record = Stamp("%Y-%m-%d %H:%M:%S") + " chosen folder " + picked + ": ";
    const std::string cant = es ? "NO SE PUEDE IMPORTAR" : "IT CAN'T BE IMPORTED", ok = es ? "Entendido" : "OK";
    if (best == nullptr) {
        const std::string why = NothingHere(P(picked), es);
        WriteText(Home() / "save_import.txt", record + why + "\n");
        Ask(cant, why, {ok});
        return;
    }
    record += "from " + U(best->data) + " (" + best->label + "), the place says " + (best->title.empty() ? "nothing of the game" : best->title) +
              ", the game is " + game + (best->ext.empty() ? ", no extdata" : ", extdata " + U(best->ext));
    if (!best->problem.empty()) {
        WriteText(Home() / "save_import.txt", record + "\nrefused: " + best->problem + "\n");
        Ask(cant, best->problem, {ok});
        return;
    }
    const int sure = Ask(es ? "IMPORTAR PARTIDA" : "IMPORT SAVE",
                         (es ? "Se encontró: " : "Found: ") + best->label + "\n\n" +
                             (es ? "La partida actual queda guardada en save_backups antes de reemplazarla. El juego se reinicia: "
                                   "guarda antes lo que no quieras perder."
                                 : "The current save is kept in save_backups before it is replaced. The game restarts: save first "
                                   "if you have progress to keep."),
                         {es ? "Importar y reiniciar" : "Import and restart", es ? "Cancelar" : "Cancel"});
    if (sure != 0) return;
    DeleteTree(staged);
    const fs::path to = staged / game, data = to / "data";
    std::string names;
    try {
        for (const auto& [name, f] : SaveFiles(best->data)) {
            if (name != "system" && SizeOf(f) == 0) continue;  // (an empty slot)
            CopyChecked(f, data / name);
            names += " " + name + "=" + std::to_string(SizeOf(f));
        }
        if (!best->ext.empty()) CopyTree(best->ext, to / "extdata");
    } catch (const Fail& f) {
        DeleteTree(staged);
        WriteText(Home() / "save_import.txt", record + "\nnot copied: " + f.detail + "\n");
        Ask(es ? "NO SE IMPORTÓ LA PARTIDA" : "THE SAVE WASN'T IMPORTED",
            es ? "No se pudo copiar esa partida (" + f.detail + "). No se cambió nada." : "That save could not be copied (" + f.detail + "). Nothing was changed.",
            {ok});
        return;
    }
    WriteText(staged / "from.txt", best->label.substr(0, best->label.find(" · ")));
    WriteText(staged / "ready", "");
    WriteText(Home() / "save_import.txt", record + "\nstaged:" + names + (best->ext.empty() ? "" : " + extdata") + "\n");
    Log("save import staged from " + U(best->data) + ": the game restarts");
    Restart();
}

// Before the game starts: the staged save in place of the current one (SaveActivity.applyPending). {title, text} or {}
std::pair<std::string, std::string> ApplySaveImport(bool es) {
    const fs::path staged = Home() / "save_import";
    if (!IsFile(staged / "ready")) return {};
    const fs::path root = SaveRoot(), from = staged / "root";
    const std::string where = ReadText(staged / "from.txt");
    int hunters = 0;
    fs::path backup;
    std::string error;
    try {
        DeleteTree(from);
        int titles = 0;
        for (const char* t : TITLES) {
            const fs::path src = staged / t, data = src / "data";
            if (!IsDir(data)) continue;
            if (!IsFile(data / "system")) {
                if (!IsFile(DataDir(root, t) / "system")) throw Fail{"", es ? "falta su archivo system" : "its system file is missing"};
                CopyChecked(DataDir(root, t) / "system", data / "system");
            }
            titles++;
            for (int u = 1; u <= 3; u++)
                if (SizeOf(data / ("user" + std::to_string(u))) > 0) hunters++;
            CopyTree(data, DataDir(from, t));
            if (IsFile(MetaFile(root, t))) CopyChecked(MetaFile(root, t), MetaFile(from, t));
            else WriteText(MetaFile(from, t), std::string(reinterpret_cast<const char*>(kMetadata), sizeof kMetadata));
            if (IsDir(src / "extdata")) CopyTree(src / "extdata", ExtDir(from, t));
        }
        if (titles == 0) throw Fail{"", es ? "no había nada preparado" : "nothing was staged"};
        if (DescribeSave(root).valid) backup = Backup(root, "import");
        CopySave(from, root);
        Log("save import applied in " + U(root) + (backup.empty() ? " (there was no save before)" : ", the old save kept in " + U(backup)));
    } catch (const Fail& f) {
        error = f.detail;
        Log("save import NOT applied, the save stays as it was: " + error);
    }
    DeleteTree(staged);
    if (!error.empty())
        return {es ? "NO SE IMPORTÓ LA PARTIDA" : "THE SAVE WASN'T IMPORTED",
                (es ? "No se pudo traer la partida: " : "The save couldn't be brought in: ") + error + ".\n\n" +
                    (es ? "Tu partida quedó como estaba. Puedes intentarlo otra vez en los ajustes (F1), Sistema, Tu partida, Importar partida."
                        : "Your save is as it was. You can try again in the settings (F1), System, Your game, Import save.")};
    const std::string who = es ? (hunters == 1 ? "1 cazador" : std::to_string(hunters) + " cazadores")
                               : (hunters == 1 ? "1 hunter" : std::to_string(hunters) + " hunters");
    const std::string kept = backup.empty() ? ""
                             : es           ? "\n\nLa partida que tenías quedó guardada en " + U(backup) + "."
                                            : "\n\nThe save you had is kept in " + U(backup) + ".";
    return {es ? "PARTIDA IMPORTADA" : "SAVE IMPORTED",
            (es ? "Se trajo la partida (" + who + ")" : "The save was brought in (" + who + ")") +
                (where.empty() ? "" : (es ? " desde " : " from ") + where) + ". " +
                (es ? "Elige tu cazador en la selección de personaje del juego." : "Pick your hunter on the game's Character Select.") + kept};
}

// Export save: a new folder "MH4U save <date>" in the one chosen, the save in Azahar's layout
void ExportSave(const std::string& picked) {
    const bool es = Spanish();
    const fs::path out = P(picked) / ("MH4U save " + Stamp());
    int copied = 0;
    try {
        for (const char* t : TITLES) {
            const fs::path save = DataDir(SaveRoot(), t);
            if (!IsFile(save / "system")) continue;
            CopyTree(save, DataDir(out, t));
            if (IsFile(MetaFile(SaveRoot(), t))) CopyChecked(MetaFile(SaveRoot(), t), MetaFile(out, t));
            if (IsDir(ExtDir(SaveRoot(), t))) CopyTree(ExtDir(SaveRoot(), t), ExtDir(out, t));
            copied++;
        }
        if (copied == 0) throw Fail{"", es ? "todavía no hay una partida" : "there is no save yet"};
    } catch (const Fail& f) {
        Ask(es ? "EXPORTAR PARTIDA" : "EXPORT SAVE", (es ? "No se pudo copiar: " : "Could not copy it: ") + f.detail, {es ? "Entendido" : "OK"});
        return;
    }
    Log("save export: " + U(out));
    Notice("Done: folder \"" + Name(out) + "\". Copy its sdmc folder over Azahar's or Citra's.",
           "Listo: carpeta \"" + Name(out) + "\". Copia su carpeta sdmc sobre la de Azahar o Citra.", 12);
    Explore(out, false);
}

// Save folder (SaveFolderActivity): a change waits in save_folder_pending.txt and is applied before the game starts again
void SaveFolderChosen(const std::string& picked) {
    const bool es = Spanish();
    const std::string title = es ? "CARPETA DE PARTIDAS" : "SAVE FOLDER", ok = es ? "Entendido" : "OK";
    fs::path to = P(picked);
    if (!DescribeSave(to).valid && DescribeSave(to / "MH4U save").valid) to /= "MH4U save";
    if (Under(to, Home()) || Under(Home(), to) || Lower(U(to)) == Lower(U(Home()))) {
        Ask(title, es ? "Esa es la carpeta del programa (o la contiene). Elige otra." : "That is the program's own folder (or holds it). Choose another one.", {ok});
        return;
    }
    const fs::path probe = to / ".mh4u_write_test";
    WriteText(probe, "0");
    if (!IsFile(probe)) {
        Ask(title, es ? "El juego no puede escribir en esa carpeta. Elige otra." : "The game can't write in that folder. Choose another one.", {ok});
        return;
    }
    std::error_code ec;
    fs::remove(probe, ec);
    const SaveInfo there = DescribeSave(to), here = DescribeSave(SaveRoot());
    bool keep_folder = there.valid;
    if (there.valid && here.valid) {
        const int a = Ask(es ? "DOS PARTIDAS" : "TWO SAVES",
                          (es ? "Esa carpeta ya tiene una partida (" : "That folder has a save already (") + SaveLine(there, es) + ")" +
                              (es ? " y la que usas ahora es otra (" : " and the one you use now is another (") + SaveLine(here, es) + ").\n\n" +
                              (es ? "¿Cuál sigue? La otra queda en save_backups. El juego se reinicia."
                                  : "Which one goes on? The other one is kept in save_backups. The game restarts."),
                          {es ? "La de esa carpeta" : "That folder's", es ? "La de ahora" : "The one I use now", es ? "Cancelar" : "Cancel"});
        if (a != 0 && a != 1) return;
        keep_folder = a == 0;
    } else if (Ask(title,
                   (es ? "La partida se guardará en " : "The save will be kept in ") + U(to) + "." +
                       (here.valid && !there.valid ? (es ? " Tu partida se copia ahí." : " Your save is copied there.") : "") +
                       (there.valid ? (es ? " Se usa la partida que tiene." : " Its save is used.") : "") +
                       (es ? "\n\nEl juego se reinicia: guarda antes lo que no quieras perder." : "\n\nThe game restarts: save first if you have progress to keep."),
                   {es ? "Cambiar y reiniciar" : "Change and restart", es ? "Cancelar" : "Cancel"}) != 0) {
        return;
    }
    WriteText(Home() / "save_folder_pending.txt", "folder=" + U(to) + "\nkeep=" + (keep_folder ? "folder" : "device") + "\n");
    Restart();
}
void SaveFolderAction() {
    const bool es = Spanish();
    const std::string now = pc_paths().save_folder, own = pc_paths().own_saves;
    const bool chosen = Lower(now) != Lower(own);
    std::vector<std::string> buttons = {es ? "Elegir carpeta" : "Choose a folder"};
    if (chosen) buttons.push_back(es ? "Volver a la carpeta propia" : "Back to the game's own folder");
    buttons.push_back(es ? "Cancelar" : "Cancel");
    const int a = Ask(es ? "CARPETA DE PARTIDAS" : "SAVE FOLDER",
                      (es ? "La partida está en " : "The save is kept in ") + now + " (" + SaveLine(DescribeSave(SaveRoot()), es) + ").\n\n" +
                          (es ? "Puedes elegir otra carpeta, por ejemplo una que una app de sincronización comparte con tus otros "
                                "equipos. Nada se pisa sin preguntar: la partida que no sigue queda en save_backups."
                              : "You can choose another folder, for example one a sync app shares with your other devices. Nothing is "
                                "overwritten without asking: the save that doesn't go on is kept in save_backups."),
                      buttons);
    if (a == 0) {
        SDL_RunOnMainThread([](void*) { PickFolder(SaveFolderChosen); }, nullptr, false);
    } else if (chosen && a == 1) {
        const SaveInfo there = DescribeSave(SaveRoot()), here = DescribeSave(P(own));
        bool keep_folder = there.valid;
        if (there.valid && here.valid) {
            const int b = Ask(es ? "DOS PARTIDAS" : "TWO SAVES",
                              (es ? "La carpeta propia tiene otra partida (" : "The game's own folder has another save (") + SaveLine(here, es) + ").\n\n" +
                                  (es ? "¿Cuál sigue? La otra queda en save_backups." : "Which one goes on? The other one is kept in save_backups."),
                              {es ? "La de la carpeta elegida" : "The chosen folder's", es ? "La de la carpeta propia" : "The own folder's",
                               es ? "Cancelar" : "Cancel"});
            if (b != 0 && b != 1) return;
            keep_folder = b == 0;
        }
        WriteText(Home() / "save_folder_pending.txt", std::string("folder=\nkeep=") + (keep_folder ? "folder" : "device") + "\n");
        Restart();
    }
}
// Before the game starts (SaveFolder.applyPending): "" or why not
std::string ApplySaveFolder() {
    const fs::path pend = Home() / "save_folder_pending.txt";
    if (!IsFile(pend)) return "";
    std::map<std::string, std::string> k;
    for (const std::string& l : Lines(pend))
        if (const size_t eq = l.find('='); eq != std::string::npos) k[l.substr(0, eq)] = l.substr(eq + 1);
    const bool keep_folder = k["keep"] == "folder";
    const fs::path from = SaveRoot(), own = P(pc_paths().own_saves);
    std::string error;
    try {
        if (!k["folder"].empty()) {
            const fs::path to = P(k["folder"]);
            std::error_code ec;
            fs::create_directories(to / "sdmc", ec);
            if (ec) throw Fail{"", "couldn't make " + U(to / "sdmc")};
            const SaveInfo there = DescribeSave(to), here = DescribeSave(from);
            if (Lower(U(to)) != Lower(U(from))) {
                if (!keep_folder || !there.valid) {
                    if (there.valid) Backup(to, "folder");
                    if (here.valid) CopySave(from, to);
                } else if (here.valid) {
                    Backup(from, "device");
                }
            }
            WriteText(Home() / "save_folder.txt", U(to) + "\n");
            Log("save folder: " + U(to) + (keep_folder ? " (its save kept)" : " (this PC's save copied there)"));
        } else {
            const SaveInfo there = DescribeSave(from), here = DescribeSave(own);
            if (keep_folder && there.valid) {
                if (here.valid) Backup(own, "device");
                CopySave(from, own);
            }
            std::error_code ec;
            fs::remove(Home() / "save_folder.txt", ec);
            Log(std::string("save folder: back to the game's own") + (keep_folder ? " (the folder's save copied)" : ""));
        }
    } catch (const Fail& f) {
        error = f.detail;
        Log("save folder: " + error);
    }
    std::error_code ec;
    fs::remove(pend, ec);
    return error;
}

// ---- DLC and event quests (ExtraImportActivity.java, DlcImport.java)
uint32_t Be32(const unsigned char* b) { return uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3]; }
uint16_t Be16(const unsigned char* b) { return static_cast<uint16_t>(b[0] << 8 | b[1]); }
uint64_t Be64(const unsigned char* b) { return uint64_t(Be32(b)) << 32 | Be32(b + 4); }
uint32_t Le32(const unsigned char* b) { return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24; }
uint64_t Align(uint64_t x, uint64_t a) { return (x + a - 1) / a * a; }
struct Content {
    uint32_t id = 0;
    uint16_t index = 0, type = 0;
    uint64_t size = 0;
    int64_t offset = -1;  // in the .cia (-1: not in it)
    fs::path app;         // the installed .app (none for a .cia)
    std::string state;    // "" readable; else why not (encrypted, compressed, notncch, noromfs, short, missing)
};
struct Title {
    uint64_t title_id = 0;
    uint16_t version = 0;
    fs::path folder, cia;
    std::vector<Content> contents;
    uint64_t bytes = 0;
    std::string problem;
    int readable = 0;
    std::string Id() const {
        char b[20];
        std::snprintf(b, sizeof b, "%016llX", static_cast<unsigned long long>(title_id));
        return b;
    }
    std::string Low() const {
        char b[12];
        std::snprintf(b, sizeof b, "%08x", static_cast<unsigned>(title_id & 0xffffffffu));
        return b;
    }
};
bool ReadAt(std::ifstream& f, uint64_t at, unsigned char* out, size_t n) {
    f.clear();
    f.seekg(static_cast<std::streamoff>(at));
    f.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(n));
    return static_cast<size_t>(f.gcount()) == n;
}
bool ParseTmd(const std::vector<unsigned char>& tmd, Title* t) {
    if (tmd.size() < 4) return false;
    size_t sig;
    switch (Be32(tmd.data())) {
    case 0x10000: case 0x10003: sig = 0x200; break;
    case 0x10001: case 0x10004: sig = 0x100; break;
    case 0x10002: case 0x10005: sig = 0x3C; break;
    default: return false;
    }
    const size_t body = Align(sig + 4, 0x40);
    if (tmd.size() < body + 0x9C4) return false;
    const size_t count = Be16(&tmd[body + 0x9E]);
    if (tmd.size() < body + 0x9C4 + count * 0x30) return false;
    t->title_id = Be64(&tmd[body + 0x4C]);
    t->version = Be16(&tmd[body + 0x9C]);
    t->contents.clear();
    for (size_t i = 0; i < count; i++) {
        const unsigned char* c = &tmd[body + 0x9C4 + i * 0x30];
        Content k;
        k.id = Be32(c), k.index = Be16(c + 4), k.type = Be16(c + 6), k.size = Be64(c + 8);
        t->contents.push_back(k);
    }
    return true;
}
bool InSubfolder(const Title& t) { return t.contents.size() > 1 && (t.contents[1].type & 0x4000) != 0; }
std::string NcchState(std::ifstream& f, uint64_t at, uint64_t length) {
    unsigned char h[0x200];
    if (!ReadAt(f, at, h, sizeof h)) return "short";
    if (!std::memcmp(h, "Z3DS", 4)) return "compressed";
    if (std::memcmp(h + 0x100, "NCCH", 4)) return "notncch";
    if ((h[0x188 + 7] & 0x04) == 0) return "encrypted";
    const uint64_t unit = 0x200ull << h[0x188 + 6];
    const uint64_t size = Le32(h + 0x1B4) * unit;
    if (size <= 0x1000) return "noromfs";
    if (at + Le32(h + 0x1B0) * unit + size > length) return "short";
    return "";
}
constexpr uint64_t kDlcHigh = 0x0004008Cull;
bool ReadInstalled(const fs::path& folder, Title* out) {
    const fs::path content = folder / "content";
    fs::path tmd;
    for (const fs::path& f : Kids(content, false)) {
        const std::string n = Lower(Name(f));
        if (n.size() == 12 && n.ends_with(".tmd") && (tmd.empty() || n < Lower(Name(tmd)))) tmd = f;
    }
    if (tmd.empty() || SizeOf(tmd) > (1u << 20)) return false;
    Title t;
    t.folder = folder;
    std::ifstream in(tmd, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!ParseTmd(bytes, &t) || (t.title_id >> 32) != kDlcHigh) return false;
    t.bytes = SizeOf(tmd);
    const bool sub = InSubfolder(t);
    for (Content& k : t.contents) {
        char name[16];
        std::snprintf(name, sizeof name, "%08x.app", k.id);
        const fs::path first = sub ? content / "00000000" / name : content / name, second = sub ? content / name : content / "00000000" / name;
        k.app = IsFile(first) || !IsFile(second) ? first : second;
        if (!IsFile(k.app)) {
            k.state = "missing";
            continue;
        }
        std::ifstream f(k.app, std::ios::binary);
        k.state = NcchState(f, 0, SizeOf(k.app));
        t.bytes += SizeOf(k.app);
    }
    *out = t;
    return true;
}
bool ReadCia(const fs::path& cia, Title* out) {
    std::ifstream f(cia, std::ios::binary);
    const uint64_t length = SizeOf(cia);
    std::vector<unsigned char> head(0x2040);
    if (length < 0x2040 || !ReadAt(f, 0, head.data(), head.size()) || Le32(head.data()) != 0x2020) return false;
    const uint64_t cert = Align(0x2020, 64), tik = Align(cert + Le32(&head[8]), 64), tmd_at = Align(tik + Le32(&head[12]), 64);
    const uint64_t tmd_size = Le32(&head[16]);
    if (tmd_size > (1u << 20)) return false;
    Title t;
    t.cia = cia;
    std::vector<unsigned char> tmd(tmd_size);
    if (!ReadAt(f, tmd_at, tmd.data(), tmd.size()) || !ParseTmd(tmd, &t) || (t.title_id >> 32) != kDlcHigh) return false;
    uint64_t pos = Align(tmd_at + tmd_size, 64);
    t.bytes = tmd_size;
    for (size_t i = 0; i < t.contents.size(); i++) {
        Content& k = t.contents[i];
        const bool present = i < 0x2000 * 8 && (head[0x20 + (i >> 3)] & (0x80 >> (i & 7))) != 0;
        if (!present) {
            k.offset = -1, k.state = "missing";
            continue;
        }
        k.offset = static_cast<int64_t>(pos);
        pos += k.size;
        if (k.type & 1) {
            k.state = "encrypted";
            continue;
        }
        if (static_cast<uint64_t>(k.offset) + k.size > length) {
            k.state = "short";
            continue;
        }
        k.state = NcchState(f, static_cast<uint64_t>(k.offset), length);
        t.bytes += k.size;
    }
    *out = t;
    return true;
}
std::vector<Title> FindDlc(const fs::path& dir) {
    std::vector<Title> out;
    if (!IsDir(dir)) return out;
    if (Title here; ReadInstalled(dir, &here)) return {here};
    std::vector<fs::path> roots;
    const std::string name = Lower(Name(dir));
    if (name == "0004008c") roots.push_back(dir);
    if (name == "title") roots.push_back(dir / "0004008c");
    for (const fs::path& base : {dir, dir / "sdmc"}) {
        const fs::path n3ds = Name(base) == "Nintendo 3DS" ? base : base / "Nintendo 3DS";
        for (const fs::path& id0 : Kids(n3ds, true))
            for (const fs::path& id1 : Kids(id0, true)) roots.push_back(id1 / "title" / "0004008c");
    }
    for (const fs::path& root : roots)
        for (const fs::path& low : Kids(root, true))
            if (Title t; ReadInstalled(low, &t)) out.push_back(t);
    for (const fs::path& f : Kids(dir, false))
        if (Lower(Name(f)).ends_with(".cia"))
            if (Title t; ReadCia(f, &t)) out.push_back(t);
    std::vector<Title> unique;
    for (const Title& t : out)
        if (std::none_of(unique.begin(), unique.end(), [&](const Title& u) { return u.title_id == t.title_id; })) unique.push_back(t);
    return unique;
}
std::string Why(const std::string& state, bool es) {
    if (state == "encrypted")
        return es ? "está cifrado. El juego solo puede leer una copia descifrada, la tuya (un .cia descifrado, o el DLC como Azahar instala un .cia descifrado). La app no descifra nada."
                  : "is encrypted. The game can only read a decrypted copy of your own (a decrypted .cia, or the DLC as Azahar installs a decrypted .cia). The app decrypts nothing.";
    if (state == "compressed")
        return es ? "está comprimido por Azahar (la opción de comprimir los CIA instalados). Apaga esa opción en Azahar e instala el DLC de nuevo, o elige el .cia descifrado."
                  : "was compressed by Azahar (its option to compress installed CIA content). Turn that option off in Azahar and install the DLC again, or choose the decrypted .cia.";
    if (state == "notncch")
        return es ? "no se puede leer: Azahar lo guardó cifrado para su propia consola, o la copia está dañada. Usa el .cia descifrado, el tuyo."
                  : "can't be read: Azahar stored it encrypted for its own console, or the copy is damaged. Use your own decrypted .cia.";
    if (state == "noromfs") return es ? "no tiene datos (RomFS)." : "has no data (RomFS).";
    if (state == "short") return es ? "termina antes de tiempo (¿una copia cortada?)." : "ends too early (a copy cut short?).";
    return es ? "no está." : "is not there.";
}
void CheckDlc(Title& t, const std::string& game, bool es) {
    t.readable = 0;
    std::string first;
    for (const Content& k : t.contents) {
        if (k.state.empty()) t.readable++;
        else if (first.empty() && k.state != "missing") first = k.state;
    }
    const std::string low = t.Low();
    if (low != "00126300" && low != "00126100")
        t.problem = es ? "El DLC " + t.Id() + " no es de Monster Hunter 4 Ultimate." : "The DLC " + t.Id() + " is not Monster Hunter 4 Ultimate's.";
    else if (low != game)
        t.problem = es ? std::string("Ese DLC es de la versión ") + (low == "00126100" ? "europea" : "de EE. UU.") + " y tu juego es de la otra: solo sirve el de la misma región."
                       : std::string("That DLC is for the ") + (low == "00126100" ? "European" : "USA") + " game and yours is the other one: only the same region's works.";
    else if (t.readable == 0)
        t.problem = (es ? "El DLC " : "The DLC ") + t.Id() + " " + Why(first.empty() ? "missing" : first, es);
    else if (!first.empty())
        t.problem = (es ? "En el DLC " + t.Id() + " hay contenido que " : "Part of the DLC " + t.Id() + " ") + Why(first, es);
    else
        t.problem.clear();
}
fs::path DlcTarget(const Title& t) { return Sdmc(User()) / "title" / "0004008c" / t.Low(); }
void WritePart(std::ifstream& f, uint64_t at, uint64_t size, const fs::path& to) {
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    std::vector<char> buf(1 << 16);
    f.clear();
    f.seekg(static_cast<std::streamoff>(at));
    for (uint64_t left = size; left > 0;) {
        const size_t n = static_cast<size_t>(std::min<uint64_t>(buf.size(), left));
        if (!f.read(buf.data(), static_cast<std::streamsize>(n))) throw Fail{"", "short read in " + Name(to)};
        out.write(buf.data(), static_cast<std::streamsize>(n));
        left -= n;
    }
    out.close();
    if (!out) throw Fail{"", "couldn't write " + U(to)};
}
void InstallDlc(const Title& t) {
    const fs::path place = DlcTarget(t), work = place.parent_path() / (t.Low() + ".importing"), content = work / "content";
    DeleteTree(work);
    std::error_code ec;
    fs::create_directories(content, ec);
    if (ec) throw Fail{"", "couldn't make " + U(content)};
    const bool sub = InSubfolder(t);
    try {
        if (!t.cia.empty()) {
            std::ifstream f(t.cia, std::ios::binary);
            unsigned char head[0x40];
            if (!ReadAt(f, 0, head, sizeof head)) throw Fail{"", "short read"};
            const uint64_t cert = Align(0x2020, 64), tik = Align(cert + Le32(&head[8]), 64), tmd_at = Align(tik + Le32(&head[12]), 64);
            WritePart(f, tmd_at, Le32(&head[16]), content / "00000000.tmd");
            for (const Content& k : t.contents) {
                if (k.offset < 0 || !k.state.empty()) continue;
                char name[16];
                std::snprintf(name, sizeof name, "%08x.app", k.id);
                WritePart(f, static_cast<uint64_t>(k.offset), k.size, sub ? content / "00000000" / name : content / name);
            }
        } else {
            for (const fs::path& tmd : Kids(t.folder / "content", false))
                if (Lower(Name(tmd)).ends_with(".tmd")) CopyChecked(tmd, content / Lower(Name(tmd)));
            for (const Content& k : t.contents) {
                if (!k.state.empty()) continue;
                char name[16];
                std::snprintf(name, sizeof name, "%08x.app", k.id);
                CopyChecked(k.app, sub ? content / "00000000" / name : content / name);
            }
        }
        const fs::path old = place.parent_path() / (t.Low() + ".old");
        DeleteTree(old);
        if (fs::exists(place, ec)) {
            fs::rename(place, old, ec);
            if (ec) throw Fail{"", "couldn't move " + U(place) + " aside"};
        }
        fs::rename(work, place, ec);
        if (ec) {
            std::error_code e2;
            fs::rename(old, place, e2);
            throw Fail{"", "couldn't put " + U(work) + " in place"};
        }
        DeleteTree(old);
    } catch (...) {
        DeleteTree(work);
        throw;
    }
    Log("dlc import: DLC title " + t.Id() + " (version " + std::to_string(t.version) + ", " + std::to_string(t.readable) + " of " +
        std::to_string(t.contents.size()) + " contents) from " + U(t.cia.empty() ? t.folder : t.cia) + " into " + U(place));
}
bool IsQuest(const std::string& n) {
    const std::string l = Lower(n);
    return l.size() == 6 && l.rfind("quest", 0) == 0 && l[5] >= '1' && l[5] <= '5';
}
std::vector<fs::path> QuestsIn(const fs::path& dir) {
    for (const fs::path& d : {dir, dir / "user"}) {
        std::vector<fs::path> out;
        for (const fs::path& f : Kids(d, false))
            if (IsQuest(Name(f))) out.push_back(f);
        if (!out.empty()) return out;
    }
    return {};
}
std::string QuestTitleOf(const fs::path& dir) {
    for (fs::path d = dir; d.has_relative_path(); d = d.parent_path()) {
        const std::string n = Lower(Name(d));
        if (n == "00001263" || n.find("0x01263") != std::string::npos) return "00126300";
        if (n == "00001261" || n.find("0x01261") != std::string::npos) return "00126100";
        if (d == d.parent_path()) break;
    }
    return "";
}
struct Quests {
    std::vector<fs::path> files;
    std::string title;
    fs::path from;
};
bool FindQuests(const fs::path& dir, int depth, Quests* out) {
    if (dir.empty() || depth > 3) return false;
    if (auto here = QuestsIn(dir); !here.empty()) {
        out->files = here, out->from = here[0].parent_path(), out->title = QuestTitleOf(out->from);
        return true;
    }
    fs::path n3ds = Name(dir) == "Nintendo 3DS" ? dir : dir / "Nintendo 3DS";
    if (!IsDir(n3ds) && IsDir(dir / "sdmc" / "Nintendo 3DS")) n3ds = dir / "sdmc" / "Nintendo 3DS";
    if (IsDir(n3ds)) {
        Quests best;
        bool any = false;
        for (const fs::path& id0 : Kids(n3ds, true))
            for (const fs::path& id1 : Kids(id0, true))
                for (const char* ext : {"00001263", "00001261"})
                    if (Quests q; FindQuests(id1 / "extdata" / "00000000" / ext, 3, &q) && (!any || q.files.size() > best.files.size()))
                        best = q, any = true;
        if (any) {
            *out = best;
            return true;
        }
    }
    if (depth >= 2) return false;
    std::vector<fs::path> kids = Kids(dir, true);
    if (kids.size() > 40) return false;
    std::sort(kids.begin(), kids.end(), [](const fs::path& a, const fs::path& b) { return TimeOf(a) > TimeOf(b); });
    for (const fs::path& k : kids)
        if (!Name(k).empty() && Name(k)[0] != '.' && FindQuests(k, depth + 1, out)) return true;
    return false;
}

void ImportExtra(const std::string& picked) {
    const bool es = Spanish();
    const std::string game = GameTitle(), title = es ? "IMPORTAR DLC Y MISIONES DE EVENTO" : "IMPORT DLC AND EVENT QUESTS",
                      ok = es ? "Entendido" : "OK";
    const fs::path dir = P(picked);
    Quests q;
    const bool quests = FindQuests(dir, 0, &q);
    std::vector<Title> dlc = FindDlc(dir);
    std::string wrong;
    if (!quests && dlc.empty())
        wrong = es ? "En esa carpeta no hay misiones de evento ni DLC de MH4U. Elige la que tiene quest1, quest2... o el .cia del DLC, o la de tu emulador (la que tiene sdmc)."
                   : "There are no MH4U event quests or DLC in that folder. Choose the one with quest1, quest2... or the DLC's .cia, or your emulator's (the one with sdmc).";
    uint64_t need = 0;
    for (Title& t : dlc) {
        if (!wrong.empty()) break;
        CheckDlc(t, game, es);
        if (!t.problem.empty()) wrong = t.problem + (es ? " No se copió nada." : " Nothing was copied.");
        else if (!t.folder.empty() && Lower(U(t.folder)) == Lower(U(DlcTarget(t))))
            wrong = es ? "Ese es el DLC que el juego ya usa." : "That is the DLC the game already uses.";
        need += t.bytes;
    }
    if (wrong.empty() && need > 0) {
        std::error_code ec;
        const auto space = fs::space(User(), ec);
        if (!ec && space.available < need + (need >> 4) + (64u << 20))
            wrong = es ? "No hay espacio para el DLC (" + std::to_string(need >> 20) + " MB). Libera espacio y vuelve a intentarlo."
                       : "There is no room for the DLC (" + std::to_string(need >> 20) + " MB). Free some space and try again.";
    }
    if (wrong.empty() && quests) {
        if (!q.title.empty() && q.title != game)
            wrong = es ? std::string("Esas misiones son de la versión ") + (q.title == "00126100" ? "europea" : "de EE. UU.") + " y tu juego es de la otra: solo sirven las de la misma región."
                       : std::string("Those quests are for the ") + (q.title == "00126100" ? "European" : "USA") + " game and yours is the other one: only the same region's work.";
        for (const fs::path& f : q.files) {
            if (!wrong.empty()) break;
            const uint64_t n = SizeOf(f);
            if (n == 0 || n > kQuestMax)
                wrong = Name(f) + (es ? " no parece un archivo de misiones (" : " doesn't look like a quest file (") + std::to_string(n) +
                        (es ? " bytes). No se copió nada." : " bytes). Nothing was copied.");
            else if (InUse(f.parent_path()))
                wrong = es ? "Esas son las misiones que el juego ya usa." : "Those are the quests the game already uses.";
        }
        if (wrong.empty() && !IsDir(ExtDir(SaveRoot(), game) / "user"))
            wrong = es ? "Todavía no hay datos extra del juego: empieza a jugar y guarda una vez, y vuelve a importarlas."
                       : "The game has no extra data yet: start playing and save once, then import them again.";
    }
    if (!wrong.empty()) {
        Ask(title, wrong, {ok});
        return;
    }
    std::string what;
    if (quests) {
        std::string names;
        for (const fs::path& f : q.files) names += (names.empty() ? "" : ", ") + Name(f);
        what += std::to_string(q.files.size()) +
                (es ? (q.files.size() == 1 ? " archivo de misiones" : " archivos de misiones") : (q.files.size() == 1 ? " quest file" : " quest files")) +
                " (" + names + "). ";
    }
    for (const Title& t : dlc)
        what += "DLC " + t.Id() + " (" + std::to_string(t.readable) + (es ? " contenidos, " : " contents, ") +
                std::to_string(std::max<uint64_t>(1, t.bytes >> 20)) + " MB). ";
    const int sure = Ask(title, what + "\n\n" +
                                    (es ? "Solo se copian los archivos de misiones: tus cazadores, tarjetas de gremio y Felyne quedan como "
                                          "están; los archivos de misiones que se reemplazan quedan en save_backups. El DLC se copia a la "
                                          "carpeta de MH4U y reemplaza el que estaba. El juego se reinicia: guarda antes lo que no "
                                          "quieras perder."
                                        : "Only the quest files are copied: your hunters, guild cards and Palico stay as they are; the quest "
                                          "files they replace are kept in save_backups. The DLC is copied into MH4U's own folder and "
                                          "replaces the one there. The game restarts: save first if you have progress to keep."),
                         {es ? "Importar y reiniciar" : "Import and restart", es ? "Cancelar" : "Cancel"});
    if (sure != 0) return;
    try {
        for (const Title& t : dlc) InstallDlc(t);
    } catch (const Fail& f) {
        Ask(title, es ? "No se pudo copiar el DLC (" + f.detail + "). No se cambió nada." : "The DLC could not be copied (" + f.detail + "). Nothing was changed.", {ok});
        return;
    }
    const fs::path staged = Home() / "save_import_extra";
    DeleteTree(staged);
    if (quests) {
        try {
            for (const fs::path& f : q.files) CopyChecked(f, staged / game / Lower(Name(f)));
            WriteText(staged / "ready", "");
            Log("dlc import: " + std::to_string(q.files.size()) + " quest files from " + U(q.from) + " staged");
        } catch (const Fail& f) {
            DeleteTree(staged);
            Ask(title, es ? "No se pudieron copiar las misiones (" + f.detail + ")." : "The quests could not be copied (" + f.detail + ").", {ok});
        }
    }
    Restart();
}
// Before the game starts (ExtraImportActivity.applyPending): the staged quest files into the extra data
void ApplyExtra() {
    const fs::path staged = Home() / "save_import_extra";
    if (!IsFile(staged / "ready")) return;
    try {
        int copied = 0;
        for (const char* t : TITLES) {
            std::vector<fs::path> files;
            for (const fs::path& f : Kids(staged / t, false))
                if (IsQuest(Name(f))) files.push_back(f);
            if (files.empty()) continue;
            const fs::path user = ExtDir(SaveRoot(), t) / "user";
            if (!IsDir(user)) throw Fail{"", "no extra data at " + U(user)};
            fs::path keep;
            for (const fs::path& f : files) {
                const fs::path old = user / f.filename();
                if (!IsFile(old)) continue;
                if (keep.empty()) keep = Backups() / (Stamp() + "-dlc") / t / "extdata" / "user";
                CopyChecked(old, keep / f.filename());
            }
            for (const fs::path& f : files) CopyChecked(f, user / f.filename()), copied++;
            Log("dlc import: " + std::to_string(files.size()) + " quest files into " + U(user) + (keep.empty() ? "" : ", the old ones in " + U(keep)));
        }
        if (copied == 0) throw Fail{"", "nothing staged"};
        DeleteTree(staged);
    } catch (const Fail& f) {
        Log("dlc import: " + f.detail);
    }
}

// ---- the game folder's mods (GameFolderActivity.syncMods)
std::string ModTitle() {
    uint64_t id = ProgramId() & ~0x0000000E00000000ull;
    if ((id >> 32) != 0x00040000ull) return kTitle;
    char b[20];
    std::snprintf(b, sizeof b, "%016llX", static_cast<unsigned long long>(id));
    return b;
}
fs::path TitleIn(const fs::path& dir, const std::string& title) {
    std::string update = title;
    update[7] = 'E';
    fs::path base, upd;
    for (const fs::path& k : Kids(dir, true)) {
        const std::string n = Lower(Name(k));
        if (n == Lower(title)) base = k;
        else if (n == Lower(update)) upd = k;
    }
    return !base.empty() ? base : upd;
}
fs::path ModRootIn(const fs::path& d, int depth) {
    if (IsModRoot(d)) return d;
    if (depth >= 3) return {};
    for (const fs::path& k : Kids(d, true))
        if (fs::path r = ModRootIn(k, depth + 1); !r.empty()) return r;
    return {};
}
// A folder's signature: its files' count, size and latest date (copied again only when it changed)
std::string Signature(const fs::path& dir) {
    uint64_t files = 0, bytes = 0;
    long long latest = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        files++, bytes += it->file_size(e2);
        latest = std::max<long long>(latest, it->last_write_time(e2).time_since_epoch().count());
    }
    return std::to_string(files) + " " + std::to_string(bytes) + " " + std::to_string(latest);
}

}  // namespace

namespace {
void SyncMods(const std::string& game_folder) {
    const fs::path from = P(game_folder) / "mods", to = ModsDir(), mark = to / ".from_game_folder";
    std::map<std::string, std::string> before;
    if (IsFile(mark)) {
        bool any = false;
        for (const std::string& l : Lines(mark)) {
            if (l.empty()) continue;
            any = true;
            const size_t tab = l.find('\t');
            before[tab != std::string::npos ? l.substr(0, tab) : l] = tab != std::string::npos ? l.substr(tab + 1) : "";
        }
        if (!any)
            for (const fs::path& f : Kids(to, true))
                if (Name(f)[0] != '.') before[Name(f)] = "";
    }
    std::map<std::string, fs::path> now;
    std::set<std::string> bare;
    const std::string title = ModTitle();
    if (IsDir(from)) {
        if (fs::path t = TitleIn(from, title); !t.empty()) now[title] = t;
        for (const fs::path& k : Kids(from, true)) {
            const std::string name = Name(k);
            if (IsTitle(name) || name.empty() || name[0] == '.') continue;
            fs::path src = TitleIn(k, title);
            if (src.empty()) src = ModRootIn(k, 0);
            if (src.empty() && !(src = FindGameFolders(k, 0)).empty()) bare.insert(name);
            if (!src.empty()) now[name] = src;
        }
    }
    for (const auto& [name, sig] : before)  // (gone from there: gone from here)
        if (!now.count(name) && !name.empty() && name[0] != '.' && name.find('/') == std::string::npos) {
            DeleteTree(to / P(name));
            Log("mods: " + name + " is gone from the game folder: removed");
        }
    std::string list;
    for (const auto& [name, src] : now) {
        const std::string sig = Signature(src);
        const fs::path dest = to / P(name);
        if (!before.count(name) && IsDir(dest)) {  // (a mod of that name added in the settings: it stays)
            Log("mods: " + name + " is in the game folder and in the program: the program's is kept");
            continue;
        }
        if (!before.count(name) && ChangesCode(src)) KeepOff(to, name);  // (a code mod starts off: build the code with it)
        if (!before.count(name) || before[name] != sig || !IsDir(dest)) {
            const fs::path tmp = to / ".sync";
            DeleteTree(tmp);
            try {
                CopyTree(src, bare.count(name) ? tmp / "romfs" : tmp);
                DeleteTree(dest);
                std::error_code ec;
                fs::rename(tmp, dest, ec);
                Log("mods: " + name + (ec ? " not copied: " + ec.message() : " copied from the game folder"));
            } catch (const Fail& f) {
                Log("mods: " + name + " not copied: " + f.detail);
                DeleteTree(tmp);
            }
        }
        list += name + "\t" + sig + "\n";
    }
    std::error_code ec;
    if (list.empty()) {
        fs::remove(mark, ec);
        return;
    }
    WriteText(mark, list);
}

}  // namespace

namespace {
// Tests: MH4U_TEST_ACTIONS="<seconds>:<method>[;...]" calls the settings' actions as their rows do (pc_action, on the main
// thread), each that many seconds after the game's start (counted from the one before); verifyFiles is Verify game files,
// its findings into the log; quit closes the program (a test's end by time: uncapped, game frames come at any rate)
void SDLCALL ActionOnMain(void* ud) {
    std::unique_ptr<std::string> m(static_cast<std::string*>(ud));
    Log("MH4U_TEST_ACTIONS: " + *m);
    if (*m == "quit") {  // (the program closes as when the player closes it: a test's end by time, frame rates vary)
        SDL_Event e{};
        e.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&e);
        return;
    }
    if (*m != "verifyFiles") {
        pc_action(m->c_str());
        return;
    }
    verify_files_start();  // (Verify game files, as its row starts it: its report into the log once done)
    std::thread([] {
        for (VerifyState st = verify_files_state();; st = verify_files_state()) {
            if (st.stage == VerifyState::Done || st.stage == VerifyState::Cancelled) {
                Log("verify: " + std::to_string(st.problems) + " problems, " + std::to_string(st.done >> 20) + " MB read in " +
                    std::to_string(static_cast<int>(st.reading_s)) + " s");
                for (const VerifyLine& l : st.lines) Log("verify: " + l.en);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }).detach();
}
void TestActions() {
    std::string list = TestEnv("MH4U_TEST_ACTIONS");
    if (list.empty()) return;
    std::vector<std::pair<int, std::string>> steps;
    for (size_t at = 0; at < list.size();) {
        size_t end = list.find(';', at);
        if (end == std::string::npos) end = list.size();
        const std::string one = list.substr(at, end - at);
        if (const size_t colon = one.find(':'); colon != std::string::npos)
            steps.push_back({std::atoi(one.c_str()), one.substr(colon + 1)});
        at = end + 1;
    }
    std::thread([steps] {
        for (const auto& [seconds, method] : steps) {
            std::this_thread::sleep_for(std::chrono::seconds(seconds));
            SDL_RunOnMainThread(ActionOnMain, new std::string(method), false);
        }
    }).detach();
}

// ---- Controls, Touch controls, Button images (and the layout editor's Add pictures): a picture of the player's own for
// the touch buttons, as Android's PicturesActivity: read by Windows (PNG, JPG, BMP, GIF, and WebP where Windows has its
// codec), made at most 256 pixels a side, written as PNG into the layout profiles' images folder (touch.cpp images_dir: in
// the save folder the player chose, else the home), where the editor lists it. What the player adds is their own.
template <class T>
struct Com {
    T* p = nullptr;
    ~Com() {
        if (p) p->Release();
    }
    T** operator&() { return &p; }
    T* operator->() { return p; }
};
fs::path ImagesDir() {
    std::string chosen = ReadText(Home() / "save_folder.txt");
    chosen = chosen.substr(0, chosen.find_first_of("\r\n"));
    while (!chosen.empty() && (chosen.back() == ' ' || chosen.back() == '/')) chosen.pop_back();
    return (chosen.empty() ? Home() : P(chosen)) / "layout profiles" / "images";
}
std::string PictureName(std::string n) {  // (PicturesActivity's safeName)
    const std::string low = Lower(n);
    for (const char* e : {".png", ".jpg", ".jpeg", ".webp", ".bmp", ".gif"})
        if (low.size() > std::strlen(e) && low.compare(low.size() - std::strlen(e), std::string::npos, e) == 0) {
            n.resize(n.size() - std::strlen(e));
            break;
        }
    std::string out;
    for (unsigned char c : n) {
        const bool ok = std::isalnum(c) || c == '.' || c == '_' || c == '-';
        if (ok) out += static_cast<char>(c);
        else if (out.empty() || out.back() != '_') out += '_';
    }
    if (out.size() > 40) out.resize(40);
    return (out.empty() || out[0] == '.' ? "picture" + out : out) + ".png";
}
// The picture at `from` as a PNG at `to`, at most 256 a side; "" or what went wrong
std::string PictureToPng(const fs::path& from, const fs::path& to, bool es) {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const fs::path tmp = fs::path(to) += L".part";
    std::string error;
    {
        Com<IWICImagingFactory> f;
        Com<IWICBitmapDecoder> dec;
        Com<IWICBitmapFrameDecode> frame;
        Com<IWICBitmapScaler> scaler;
        Com<IWICFormatConverter> conv;
        Com<IWICStream> stream;
        Com<IWICBitmapEncoder> enc;
        Com<IWICBitmapFrameEncode> out;
        Com<IPropertyBag2> props;
        UINT w = 0, h = 0;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f.p)))) {
            error = es ? "Windows no puede leer imágenes (WIC)" : "Windows can't read pictures (WIC)";
        } else if (FAILED(f->CreateDecoderFromFilename(from.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) ||
                   FAILED(dec->GetFrame(0, &frame)) || FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0) {
            error = es ? "no es una imagen que Windows lea" : "it isn't a picture Windows can read";
        } else {
            const double k = std::min(1.0, 256.0 / std::max(w, h));
            const UINT nw = k < 1 ? std::max(1u, static_cast<UINT>(w * k + .5)) : w, nh = k < 1 ? std::max(1u, static_cast<UINT>(h * k + .5)) : h;
            IWICBitmapSource* src = frame.p;
            if (k < 1) {
                if (SUCCEEDED(f->CreateBitmapScaler(&scaler)) &&
                    SUCCEEDED(scaler->Initialize(frame.p, nw, nh, WICBitmapInterpolationModeFant)))
                    src = scaler.p;
                else
                    error = es ? "no se pudo achicar" : "it couldn't be made smaller";
            }
            WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
            if (error.empty() &&
                (FAILED(f->CreateFormatConverter(&conv)) ||
                 FAILED(conv->Initialize(src, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) ||
                 FAILED(f->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(tmp.c_str(), GENERIC_WRITE)) ||
                 FAILED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) ||
                 FAILED(enc->Initialize(stream.p, WICBitmapEncoderNoCache)) || FAILED(enc->CreateNewFrame(&out, &props)) ||
                 FAILED(out->Initialize(props.p)) || FAILED(out->SetSize(nw, nh)) || FAILED(out->SetPixelFormat(&format)) ||
                 FAILED(out->WriteSource(conv.p, nullptr)) || FAILED(out->Commit()) || FAILED(enc->Commit())))
                error = (es ? "no se pudo escribir " : "can't write ") + U(to.filename());
        }
    }
    std::error_code ec;
    if (error.empty()) {
        fs::remove(to, ec);
        fs::rename(tmp, to, ec);
        if (ec) error = (es ? "no se pudo escribir " : "can't write ") + U(to.filename());
    }
    if (!error.empty()) fs::remove(tmp, ec);
    if (SUCCEEDED(init)) CoUninitialize();
    return error;
}
void ImportPicture(const std::string& picked) {
    const bool es = Spanish();
    const fs::path dir = ImagesDir();
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::string name = PictureName(Name(P(picked)));
    const std::string error = PictureToPng(P(picked), dir / P(name), es);
    if (error.empty()) {
        Log("picture: " + U(dir / P(name)));
        Notice("Done: " + name + ". Pick it in the layout editor with Picture.", "Listo: " + name + ". Elígela en el editor con Imagen.");
    } else {
        Notice("It couldn't be done: " + error, "No se pudo: " + error);
    }
}
// (1.0.2) Controls > Control profiles > Export: the .zip touch.cpp made in profile_export/ (touch_profile_export_app: the
// profile, its pictures and its combined buttons) copied into the folder the player picks, as Android's ProfileZipActivity
void ExportProfile(const std::string& picked) {
    fs::path zip;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(Home() / "profile_export", ec))
        if (e.is_regular_file(ec) && Lower(U(e.path().extension())) == ".zip") zip = e.path();
    try {
        if (zip.empty()) throw Fail{"", Spanish() ? "no se encontró el .zip del perfil" : "the profile's .zip wasn't found"};
        CopyChecked(zip, P(picked) / zip.filename());
    } catch (const Fail& f) {
        Notice("It couldn't be done: " + f.detail, "No se pudo: " + f.detail);
        return;
    }
    Log("profile export: " + U(P(picked) / zip.filename()));
    Notice("Done: " + Name(zip) + " is in that folder.", "Listo: " + Name(zip) + " está en esa carpeta.");
}
// ...and Import: the .zip the player picks copied as profile_import.zip in the app's folder (beside it first, then
// renamed: never half of it), which touch.cpp puts in the slot (touch_profile_import_poll); the profile's page says how
// it went
void ImportProfile(const std::string& picked) {
    try {
        CopyChecked(P(picked), Home() / "profile_import.zip");
    } catch (const Fail& f) {
        Notice("It couldn't be done: " + f.detail, "No se pudo: " + f.detail);
        return;
    }
    Log("profile import: " + picked);
}
}  // namespace

std::vector<std::pair<std::string, std::string>> pc_files_before_game(const std::string& game_folder) {
    const bool es = Spanish();
    TestActions();
    std::vector<std::pair<std::string, std::string>> notices;
    if (IsFile(Home() / "save_folder_pending.txt")) {
        if (const std::string error = ApplySaveFolder(); !error.empty())
            notices.push_back({es ? "CARPETA DE PARTIDAS" : "SAVE FOLDER",
                               (es ? "No se pudo cambiar la carpeta de partidas: " : "The save folder couldn't be changed: ") + error});
        pc_paths_reload_saves();
        Log("saves in " + pc_paths().save_folder);
    }
    if (auto n = ApplySaveImport(es); !n.first.empty()) notices.push_back(n);
    ApplyExtra();
    SyncMods(game_folder);
    return notices;
}

bool pc_files_action(const std::string& m) {
    if (m == "showBuilder") {  // (the game closes into the builder's page: asked first, as MHGU for PC does)
        std::thread([] {
            const bool es = Spanish();
            if (Ask(es ? "CREAR EL CÓDIGO DEL JUEGO" : "BUILD THE GAME CODE",
                    es ? "¿Cerrar el juego y crear el código del juego? Guarda la partida antes."
                       : "Close the game and build the game code? Save in the game first.",
                    {es ? "Cerrar y crear" : "Close and build", es ? "Cancelar" : "Cancel"}) == 0)
                SDL_RunOnMainThread([](void*) { pc_action("showBuilderNow"); }, nullptr, false);
        }).detach();
        return true;
    }
    if (m == "showShareLog") {  // (the log's folder, log.txt picked)
        Explore(Home() / "log.txt", true);
        return true;
    }
    if (m == "showLicenses") {
        const fs::path licenses = P(pc_paths().exe_dir) / "licenses";
        if (IsDir(licenses)) Explore(licenses, false);
        else Notice("The licenses are in the licenses folder of the MH4U-PC zip.", "Las licencias están en la carpeta licenses del zip MH4U-PC.");
        return true;
    }
    if (m == "showCheatImport") {
        static const SDL_DialogFileFilter txt[] = {{"Cheats (.txt)", "txt"}, {"All files", "*"}};
        PickFile(txt, 2, ImportCheats);
        return true;
    }
    if (m == "showModImport") {
        static const SDL_DialogFileFilter zip[] = {{"Mod (.zip)", "zip"}};
        PickFile(zip, 1, [](const std::string& p) { ImportMod(p, false); });
        return true;
    }
    if (m == "importPictures") {  // (Button images, the layout editor's Add pictures: one picture at a time, as Android's)
        static const SDL_DialogFileFilter pics[] = {{"Pictures", "png;jpg;jpeg;webp;bmp;gif"}, {"All files", "*"}};
        PickFile(pics, 2, ImportPicture);
        return true;
    }
    if (m == "exportProfile") {  // (1.0.2: a control profile's .zip, into a folder the player picks)
        PickFolder(ExportProfile);
        return true;
    }
    if (m == "importProfile") {  // (1.0.2: a control profile from a .zip made with Export)
        static const SDL_DialogFileFilter zip[] = {{"Control profile (.zip)", "zip"}, {"All files", "*"}};
        PickFile(zip, 2, ImportProfile);
        return true;
    }
    if (m == "showModFolderImport") {
        PickFolder([](const std::string& p) { ImportMod(p, true); });
        return true;
    }
    if (m == "showSaveImport") {
        PickFolder(ImportSave);
        return true;
    }
    if (m == "showSaveExport") {
        bool any = false;
        for (const char* t : TITLES) any |= IsFile(DataDir(SaveRoot(), t) / "system");
        if (!any) Notice("There is no save to export yet.", "Todavía no hay una partida para exportar.");
        else PickFolder(ExportSave);
        return true;
    }
    if (m == "showSaveFolder") {
        std::thread(SaveFolderAction).detach();
        return true;
    }
    if (m == "showExtraImport") {
        PickFolder(ImportExtra);
        return true;
    }
    if (m == "openTextures") {  // (Mods & Cheats, Texture packs: the game folder's textures in File Explorer)
        const std::string folder = pc_game_folder();
        if (!folder.empty()) {
            std::error_code ec;
            fs::create_directories(P(folder) / "textures", ec);
            Explore(P(folder) / "textures", false);
        }
        return true;
    }
    return false;
}
