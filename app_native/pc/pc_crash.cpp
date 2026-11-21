// MH4U for PC: the error screen when the game crashes (crash_screen.h on the PC; Android's crash_screen.cpp and
// LogShareActivity, MHGU for PC's crash notice). The game doesn't just vanish:
//   - a crash of the program's own (an exception nothing handles): its code, the address, the module and the thread, and a
//     backtrace, go to the log (unhandled_filter);
//   - a bad guest memory access: the runtime writes its report to the log and calls the FatalHook installed here;
//   - the GPU's device lost (gpu_driver.cpp's hook, through pc_platform.cpp): a line in the log;
//   - the game's own end with an error (pc_main.cpp, the runtime's Boot failing).
// Each starts MH4U.exe --crashed=<what> (a process of its own, as Android's LogShareActivity: this one is ending): the
// error screen, a box over the game's display that says what happened, opens the log's folder, and offers what Android's
// offers for a code library or a code mod that doesn't fit (Build the game code, Back to the mods of the last start, Turn
// the code mods off). MH4U_TEST_CRASH=guest|host|abort|devicelost[:seconds] (a test) makes one, after 20 s by default;
// MH4U_TEST_ANSWER=<n> (a test) answers the screen with its button n, unshown.
#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "common/utf8.h"
#include "diag.h"
#include "mh4n/memory.h"
#include "mh4n/registry.h"
#include "mh4n/runtime.h"
#include "pc_app.h"

#include <windows.h>  // (last: its macros)
#include <io.h>
#include <shellapi.h>

namespace fs = std::filesystem;

namespace {

std::atomic<int> g_crashing{0};  // a crash is being reported (a second one, meanwhile, does not report again)
wchar_t g_exe[MAX_PATH];

void Put(const char* text) { (void)!_write(2, text, static_cast<unsigned>(std::strlen(text))); }

// MH4U.exe again, with `args` (no heap: this may run in a crashing process)
bool Start(const wchar_t* args) {
    if (!g_exe[0] && GetModuleFileNameW(nullptr, g_exe, MAX_PATH) == 0) return false;
    static wchar_t cmd[MAX_PATH + 64];
    _snwprintf(cmd, MAX_PATH + 64, L"\"%ls\" %ls", g_exe, args);
    cmd[MAX_PATH + 63] = 0;
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(g_exe, cmd, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    char pids[MAX_PATH];  // (a test: MH4U_TEST_PIDS, the runner waits for this one too)
    if (GetEnvironmentVariableA("MH4U_TEST_PIDS", pids, MAX_PATH) > 0) {
        HANDLE f = CreateFileA(pids, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            char line[24];
            const int n = std::snprintf(line, sizeof line, "%lu\n", pi.dwProcessId);
            DWORD wrote = 0;
            WriteFile(f, line, static_cast<DWORD>(n), &wrote, nullptr);
            CloseHandle(f);
        }
    }
    return true;
}

// The error screen, MH4U.exe --crashed=<why>, for this session's log
void OpenScreen(const char* why) {
    wchar_t args[64];
    _snwprintf(args, 64, L"--crashed=%hs", why);
    args[63] = 0;
    char line[96];
    std::snprintf(line, sizeof line, Start(args) ? "*** MH4U: the error screen opens.\n" : "*** MH4U: the error screen could not open.\n");
    Put(line);
}

// "MH4U.exe+0x1234" for an address in a module, else the address alone
void Where(uint64_t pc, char* out, size_t size) {
    HMODULE m = nullptr;
    char path[MAX_PATH];
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(pc)), &m) &&
        GetModuleFileNameA(m, path, sizeof path)) {
        const char* base = std::strrchr(path, '\\');
        std::snprintf(out, size, "%s+0x%llx", base != nullptr ? base + 1 : path,
                      static_cast<unsigned long long>(pc - reinterpret_cast<uintptr_t>(m)));
    } else {
        std::snprintf(out, size, "0x%llx", static_cast<unsigned long long>(pc));
    }
}

bool Readable(uint64_t at) {
    MEMORY_BASIC_INFORMATION mi;
    return VirtualQuery(reinterpret_cast<void*>(static_cast<uintptr_t>(at)), &mi, sizeof mi) && mi.State == MEM_COMMIT &&
           !(mi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
}

// The thread's name (SetThreadDescription's: the runtime names its threads), "" none
void ThreadName(char* out, size_t size) {
    out[0] = 0;
    using Get = HRESULT(WINAPI*)(HANDLE, PWSTR*);
    const auto get = reinterpret_cast<Get>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription")));
    PWSTR name = nullptr;
    if (get == nullptr || FAILED(get(GetCurrentThread(), &name)) || name == nullptr) return;
    WideCharToMultiByte(CP_UTF8, 0, name, -1, out, static_cast<int>(size), nullptr, nullptr);
    LocalFree(name);
}

LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* ep) {
    if (g_crashing.exchange(1) != 0) return EXCEPTION_EXECUTE_HANDLER;
    std::fflush(stderr);
    const EXCEPTION_RECORD* er = ep->ExceptionRecord;
    char when[24], where[200], name[64], line[512];
    diag_clock_into(when, sizeof when);
    Where(reinterpret_cast<uintptr_t>(er->ExceptionAddress), where, sizeof where);
    ThreadName(name, sizeof name);
    const char* what = er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION ? "an access violation"
                       : er->ExceptionCode == EXCEPTION_STACK_OVERFLOW ? "a stack overflow"
                       : er->ExceptionCode == EXCEPTION_ILLEGAL_INSTRUCTION ? "an illegal instruction"
                       : er->ExceptionCode == EXCEPTION_INT_DIVIDE_BY_ZERO ? "a division by zero"
                       : er->ExceptionCode == 0xC0000409 ? "a fast fail (the C runtime stopped the program)"
                                                          : "an exception";
    char access[96] = "";
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
        std::snprintf(access, sizeof access, ", %s 0x%llx",
                      er->ExceptionInformation[0] == 0 ? "reading" : er->ExceptionInformation[0] == 1 ? "writing" : "running",
                      static_cast<unsigned long long>(er->ExceptionInformation[1]));
    std::snprintf(line, sizeof line,
                  "\n*** MH4U: the game stopped at %s into the session: %s (exception %08lx%s) at %s, on thread %lu (%s).\n",
                  when, what, er->ExceptionCode, access, where, GetCurrentThreadId(), name[0] ? name : "no name");
    Put(line);
    // the backtrace from the faulting context (x64 unwind data: the compiler writes it for every function)
    CONTEXT ctx = *ep->ContextRecord;
    for (int i = 0; i < 32 && ctx.Rip != 0; i++) {
        Where(ctx.Rip, where, sizeof where);
        std::snprintf(line, sizeof line, "  #%-2d %016llx %s\n", i, static_cast<unsigned long long>(ctx.Rip), where);
        Put(line);
        DWORD64 base = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &base, nullptr);
        if (fn == nullptr) {  // (a leaf: its return address on top of the stack)
            if (!Readable(ctx.Rsp)) break;
            ctx.Rip = *reinterpret_cast<DWORD64*>(static_cast<uintptr_t>(ctx.Rsp));
            ctx.Rsp += 8;
        } else {
            void* data = nullptr;
            DWORD64 frame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, ctx.Rip, fn, &ctx, &data, &frame, nullptr);
        }
    }
    OpenScreen("host");
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_EXECUTE_HANDLER;
}

// The runtime's FatalHook: its report of the bad guest access is in the log already
void OnGuestFault() {
    if (g_crashing.exchange(1) != 0) return;
    char when[24], line[200];
    diag_clock_into(when, sizeof when);
    std::snprintf(line, sizeof line, "*** MH4U: the game stopped at %s into the session: a bad guest memory access (above).\n", when);
    Put(line);
    OpenScreen("guest");
}

// ---- a test-only trigger: MH4U_TEST_CRASH=guest|host|abort|devicelost[:seconds], after 20 s by default
void TestTrigger(mh4n::Runtime& rt) {
    const char* what = std::getenv("MH4U_TEST_CRASH");
    if (what == nullptr || *what == 0) return;
    std::string kind = what;
    int seconds = 20;
    if (const auto colon = kind.find(':'); colon != std::string::npos) {
        seconds = std::atoi(kind.c_str() + colon + 1);
        kind.resize(colon);
    }
    SetEnvironmentVariableW(L"MH4U_TEST_CRASH", nullptr);  // (not for the screen's process, nor a game it starts)
    std::fprintf(stderr, "[Warning] App: MH4U_TEST_CRASH: a %s crash in %d s (a test)\n", kind.c_str(), seconds);
    mh4n::u8* guest = rt.Memory().Base();
    std::thread([kind, seconds, guest] {
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        std::fprintf(stderr, "[Warning] App: MH4U_TEST_CRASH: %s now\n", kind.c_str());
        std::fflush(stderr);
        if (kind == "guest") {
            *reinterpret_cast<volatile mh4n::u32*>(guest + 0x100000010ull) = 0xDEAD;  // (the guard past the 4 GiB)
        } else if (kind == "abort") {
            std::abort();
        } else if (kind == "devicelost") {
            pc_crash_device_lost("MH4U_TEST_CRASH, a test");
        } else {
            *reinterpret_cast<volatile int*>(static_cast<uintptr_t>(0x8)) = 1;
        }
    }).detach();
}
MH4N_REGISTER(mh4n::BootStep, "zz.pc.crash_test", mh4n::BootStep{mh4n::Phase::Started, TestTrigger});

// ---- the screen (MH4U.exe --crashed=<why>)
fs::path P(const std::string& utf8) { return mh4n::utf8::PathFromUtf8(utf8); }

// The log's last 256 KB says `what`
bool LogSays(const std::string& log, const char* what) { return log.find(what) != std::string::npos; }
std::string LogTail() {
    std::ifstream in(P(pc_paths().home + "/log.txt"), std::ios::binary | std::ios::ate);
    if (!in) return "";
    const std::streamoff size = in.tellg(), from = size > (256 << 10) ? size - (256 << 10) : 0;
    in.seekg(from);
    std::string s(static_cast<size_t>(size - from), '\0');
    in.read(s.data(), static_cast<std::streamsize>(s.size()));
    return s;
}

fs::path ModsDir() { return P(pc_paths().user) / "load" / "mods"; }
std::vector<std::string> Lines(const fs::path& p) {
    std::vector<std::string> out;
    std::ifstream in(p);
    for (std::string l; std::getline(in, l);) {
        while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
        out.push_back(l);
    }
    return out;
}
std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(0, 1);
    return s;
}
bool WriteLines(const fs::path& p, const std::vector<std::string>& lines) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    for (const std::string& l : lines) out << l << '\n';
    return static_cast<bool>(out);
}
std::vector<std::string> ModFolders() {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(ModsDir(), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        const std::string n = mh4n::utf8::PathToUtf8(it->path().filename());
        if (it->is_directory(e2) && !n.empty() && n[0] != '.' && n[0] != '_') out.push_back(n);
    }
    return out;
}
// The mods as at the last start the code library took (load_order.ok, the runtime's): its list becomes load_order.txt,
// and a mod added since starts off (LogShareActivity.restoreMods)
bool RestoreMods() {
    std::vector<std::string> lines = Lines(ModsDir() / "load_order.ok");
    if (lines.empty()) return false;
    std::vector<std::string> named;
    for (const std::string& l : lines) {
        const std::string t = Trim(l);
        if (!t.compare(0, 7, "# off: ")) named.push_back(Trim(t.substr(7)));
        else if (!t.empty() && t[0] != '#') named.push_back(t);
    }
    for (const std::string& n : ModFolders())
        if (std::find(named.begin(), named.end(), n) == named.end()) lines.push_back("# off: " + n);
    std::fprintf(stderr, "[crash] mods: back to those of the last start\n");
    return WriteLines(ModsDir() / "load_order.txt", lines);
}
// Every code mod turned off in load_order.txt, the rest as they were (LogShareActivity.codeModsOff)
bool CodeModsOff() {
    std::vector<std::string> lines = Lines(ModsDir() / "load_order.txt");
    int n = 0;
    for (const std::string& name : ModFolders()) {
        std::error_code ec;
        const fs::path d = ModsDir() / P(name);
        if (!fs::is_directory(d / "exefs", ec) && !fs::is_directory(d / "0004000000126300" / "exefs", ec) &&
            !fs::is_directory(d / "0004000E00126300" / "exefs", ec))
            continue;
        n++;
        bool listed = false;
        for (std::string& l : lines) {
            const std::string t = Trim(l);
            if (t == name) l = "# off: " + name, listed = true;
            else if (t == "# off: " + name) listed = true;
        }
        if (!listed) lines.push_back("# off: " + name);
    }
    if (n == 0) return false;
    std::fprintf(stderr, "[crash] mods: %d code mod(s) turned off\n", n);
    return WriteLines(ModsDir() / "load_order.txt", lines);
}

}  // namespace

void pc_crash_install() {
    GetModuleFileNameW(nullptr, g_exe, MAX_PATH);
    ULONG guarantee = 64 << 10;  // (room on a thread's stack for the report after a stack overflow)
    SetThreadStackGuarantee(&guarantee);
    SetUnhandledExceptionFilter(UnhandledFilter);
    mh4n::SetFatalHook(OnGuestFault);
}

void pc_crash_device_lost(const char* where) {
    if (g_crashing.exchange(1) != 0) return;
    std::fflush(stderr);
    char line[512], when[24];
    diag_clock_into(when, sizeof when);
    std::snprintf(line, sizeof line, "\n*** MH4U: the game stopped at %s into the session: the GPU stopped answering (Vulkan "
                                     "device lost: %s).\n", when, where != nullptr ? where : "?");
    Put(line);
    OpenScreen("gpu");
    TerminateProcess(GetCurrentProcess(), 1);
}

void pc_crash_open(const char* why) {
    if (g_crashing.exchange(1) != 0) return;
    std::fflush(stderr);
    OpenScreen(why);
}

int pc_crash_screen(const std::string& why, bool es) {
    const std::string log = LogTail();
    const bool library0 = LogSays(log, "does not match");
    const bool code_mod = !library0 && LogSays(log, "cannot load") && (LogSays(log, "Loader: code mod") || LogSays(log, ": mods: "));
    const bool recomp_h = LogSays(log, "different recomp.h"), two_builds = LogSays(log, "come from different builds");
    const bool mod_build = LogSays(log, "another build of the code mod"), library = library0 || mod_build;
    std::error_code ec;
    const bool last_ok = fs::is_regular_file(ModsDir() / "load_order.ok", ec);
    std::fprintf(stderr, "[crash] the error screen: %s%s%s%s%s%s\n", why.c_str(), library0 ? ", the code library doesn't match" : "",
                 code_mod ? ", a code mod can't load" : "", recomp_h ? ", another tool's library" : "",
                 two_builds ? ", a library and an .id of two builds" : "", mod_build ? ", another build of the code mod" : "");

    std::string text = es ? "El registro explica qué pasó, en qué PC y con qué ajustes y mods: es log.txt, en la carpeta del "
                            "programa (Abrir la carpeta del registro). Puedes enviarlo a quien te esté ayudando. No lleva tu "
                            "partida, cuentas, correo ni ubicación, pero puede incluir la dirección de tu red local si jugaste "
                            "en LAN."
                          : "The log says what happened, on which PC and with which settings and mods: it is log.txt, in the "
                            "program's folder (Open the log folder). You can send it to whoever is helping you. Your save, "
                            "accounts, e-mail and location are not in it, but it can hold your local network address if you "
                            "played over LAN.";
    std::string more;
    if (recomp_h)
        more = es ? "La librería de código se hizo con otra versión de la herramienta (o la hizo otra persona). Vuelve a crearla con "
                    "esta versión (Crear el código del juego, en esta PC), y no uses una librería ajena."
                  : "The code library was made with another version of the tool (or by someone else). Build it again with this "
                    "version (Build the game code, on this PC), and don't use a library someone else made.";
    else if (two_builds)
        more = es ? "La librería de código y el .id que tiene al lado son de armados distintos. Copia juntos los dos archivos de un "
                    "mismo armado, o vuelve a crearlos en esta PC (Crear el código del juego)."
                  : "The code library and the .id beside it come from different builds. Copy both files of the same build "
                    "together, or build them again on this PC (Build the game code).";
    else if (mod_build)
        more = es ? "La librería de código se hizo con otro armado del mod de código que agranda el programa (como Heavenfall): "
                    "cambiaste el mod o sus opciones. Vuelve a hacer la librería con los mods que están activados, o vuelve a los "
                    "mods del último inicio que funcionó."
                  : "The code library was made with another build of the code mod that makes the program bigger (as Heavenfall "
                    "does): the mod or its options changed. Make the library again with the mods that are on, or go back to "
                    "the mods of the last start that worked.";
    else if (library)
        more = es ? "La librería de código no coincide con el código del juego y los mods de código que están activados (los que "
                    "traen una carpeta exefs). Vuelve a crear la librería con esos mods (Crear el código del juego: se hace en "
                    "esta PC y tarda un rato), o vuelve a los mods del último inicio que funcionó."
                  : "The code library doesn't match the game's code with the code mods that are on (the ones with an exefs "
                    "folder). Build the library again with those mods (Build the game code: it is made on this PC and takes a "
                    "while), or go back to the mods of the last start that worked.";
    else if (code_mod && LogSays(log, "exheader.bin:"))
        more = es ? "Un mod de código trae un exheader.bin que no va con el juego o con su propio code.bin (otro juego o región, o "
                    "archivos de dos armados del mod). Vuelve a armar el mod con tu juego, vuelve a los mods del último inicio "
                    "que funcionó, o apaga los mods de código."
                  : "A code mod brings an exheader.bin that doesn't go with the game or with its own code.bin (another game or "
                    "region, or files of two builds of the mod). Build the mod again from your game, go back to the mods of the "
                    "last start that worked, or turn the code mods off.";
    else if (code_mod)
        more = es ? "Un mod de código cambió el programa del juego de una forma que este port no puede cargar: está hecho para los "
                    "emuladores. Vuelve a los mods del último inicio que funcionó, o apaga los mods de código."
                  : "A code mod changed the game's program in a way this port can't load: it is made for the emulators. Go back "
                    "to the mods of the last start that worked, or turn the code mods off.";
    else if (why == "gpu")
        more = es ? "La tarjeta gráfica dejó de responder (su controlador se reinició o falló). Actualizar el controlador de la "
                    "tarjeta gráfica suele ayudar."
                  : "The graphics card stopped answering (its driver was reset or failed). Updating the graphics card's driver "
                    "often helps.";

    enum { BACK, OFF, BUILD, LOG, CLOSE };
    std::vector<std::pair<int, std::string>> buttons;
    if ((library || code_mod) && last_ok) buttons.push_back({BACK, es ? "Volver a los mods del último inicio" : "Back to the mods of the last start"});
    else if (code_mod) buttons.push_back({OFF, es ? "Apagar los mods de código" : "Turn the code mods off"});
    if (recomp_h || two_builds || library) buttons.push_back({BUILD, es ? "Crear el código del juego" : "Build the game code"});
    buttons.push_back({LOG, es ? "Abrir la carpeta del registro" : "Open the log folder"});
    buttons.push_back({CLOSE, es ? "Cerrar" : "Close"});

    const std::string title = es ? "MH4U: EL JUEGO SE CERRÓ POR UN ERROR" : "MH4U: THE GAME STOPPED BECAUSE OF AN ERROR";
    const std::string message = (more.empty() ? "" : more + "\n\n") + text;
    // the screen in the game folder screen's look (pc_launcher.cpp): the ways back each on a row, then the log and Close
    std::vector<std::string> wide, pair;
    for (const auto& [what, label] : buttons) (what == LOG || what == CLOSE ? pair : wide).push_back(label);
    std::string shot;
    if (const char* dir = SDL_getenv("MH4U_TEST_SHOT_DIR"); dir != nullptr && *dir) shot = std::string(dir) + "/error_screen.png";
    for (;;) {
        int answer = -1;
        if (const char* forced = SDL_getenv("MH4U_TEST_ANSWER"); forced != nullptr && *forced) {
            // (a test: the screen shown a moment and saved into the shots, then the answer it was given)
            if (!shot.empty()) pc_message_screen(title, message, wide, pair, es, shot.c_str()), shot.clear();
            answer = std::min(SDL_atoi(forced), static_cast<int>(buttons.size()) - 1);
            std::fprintf(stderr, "[crash] %s\n[crash] answered by MH4U_TEST_ANSWER: %d\n", message.c_str(), answer);
        } else {
            answer = pc_message_screen(title, message, wide, pair, es);
        }
        const int what = answer >= 0 && answer < static_cast<int>(buttons.size()) ? buttons[answer].first : CLOSE;
        if (what == LOG) {
            const std::wstring args = L"/select,\"" + P(pc_paths().home + "/log.txt").make_preferred().wstring() + L"\"";
            if (SDL_getenv("MH4U_TEST_NO_EXPLORER") == nullptr)
                ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
            std::fprintf(stderr, "[crash] the log's folder in File Explorer\n");
            if (SDL_getenv("MH4U_TEST_ANSWER") != nullptr) break;  // (a test: once)
            continue;
        }
        if (what == BACK && RestoreMods()) Start(L"");
        else if (what == OFF && CodeModsOff()) Start(L"");
        else if (what == BUILD) Start(L"--builder");
        break;
    }
    return 0;
}
