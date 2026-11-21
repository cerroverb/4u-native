// MH4U for PC: the game's code library made inside MH4U.exe (pc_builder.h), the Android app's on-phone builder for
// Windows, as MHGU for PC's runtime/pc_builder.c (whose downloads, unpacking, job and resume this follows). The player's
// own game and update in the game folder become mh4u_code_native.dll and its .id beside them, with no Python or compiler
// of their own: the first time, it downloads them from their official sources and checks each one against its SHA-256
// (python.org's embeddable Python for Windows; Zig, a C compiler, from PyPI's ziglang wheel, or from ziglang.org when PyPI
// can't be reached) and keeps them in builder\ beside the settings. Never anything of the game. Then mh4n's
// tools/pc_build.py (tools/build_game.py --pc, with Zig through zigcc.exe beside MH4U.exe), from builder_tools.zip beside
// MH4U.exe, runs in a process of its own: below normal priority, all the processor's cores but one, in a job object that
// ends it and its compilers with Cancel or with this program. Its "@@" lines move the stages; builder_log.txt keeps all
// of it.
//
// Resuming and Start over are Android's: the work folder is kept (builder\work), a run cut short goes on from its objects
// and from its translation when its inputs are the same (pc_build.py's inputs.txt); Start over sets the work folder (and,
// chosen, the tools) aside in one rename and deletes it on a thread of its own.
#include "pc_builder.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>

#include "app_version.h"
#include "pc_app.h"
#include "unzip.h"

const char* const pcb_stage_names[PCB_STAGES] = {"tools", "extract", "recompile", "compile", "install"};

namespace {

// ---- the downloads: where from, their SHA-256 and size (the official pages: python.org's release page lists the
// embeddable package's SHA-256; PyPI's JSON API the wheel's; ziglang.org's download/index.json its zip's, 0.13.0,
// x86_64-windows, which Zig's minisign key signs too). The same files as MHGU for PC's builder.
struct Tool {
    const char *name, *version, *url, *sha256;
    int64_t size, unpacked;
    const char* prefix;  // the folder in the archive the tool is in (null: the archive's root, all of it kept)
    const char* from;    // the source, in the log and on the page
};
const Tool PYTHON = {"python", "3.14.9", "https://www.python.org/ftp/python/3.14.9/python-3.14.9-embed-amd64.zip",
                     "3c01d7cfc666f746a6f064f60fb2156ab0ec464d3d738ab796371a46a9bb24a2", 12609248, 30LL << 20, nullptr,
                     "python.org"};
const Tool ZIG = {"zig", "0.13.0",
                  "https://files.pythonhosted.org/packages/e0/0c/66f4710aedf5544e15779ff9c6e770322986e15daf9299b1a4df0be19e09/"
                  "ziglang-0.13.0-py3-none-win_amd64.whl",
                  "b64b2aa936ebbfeecfbb2611df331a5474edebe1246b98c2f56d6ae4d58b71fc", 80418705, 270LL << 20, "ziglang/", "PyPI"};
// Zig from its own site, when PyPI can't give it (a provider's DNS answered 127.9.9.9 for files.pythonhosted.org): the
// official zip, the very files the wheel has
const Tool ZIG_ORG = {"zig", "0.13.0", "https://ziglang.org/download/0.13.0/zig-windows-x86_64-0.13.0.zip",
                      "d859994725ef9402381e557c60bb57497215682e355204d754ee3df75ee3c158", 79163968, 270LL << 20,
                      "zig-windows-x86_64-0.13.0/", "ziglang.org"};
const Tool* const TOOLS[] = {&PYTHON, &ZIG};
const Tool* other_source(const Tool* t) { return t == &ZIG ? &ZIG_ORG : nullptr; }
// The parts of Zig `zig cc` reads for x86_64-windows-gnu C (its MinGW-w64 and compiler-rt are built from them once),
// under the archive's prefix: the C libraries and headers of other systems, libc++ and the sanitizers stay out
const char* const ZIG_KEEP[] = {"zig.exe", "LICENSE", "lib/c.zig", "lib/zig.h", "lib/compiler_rt.zig", "lib/compiler_rt/",
                                "lib/compiler/", "lib/std/", "lib/include/", "lib/libc/mingw/",
                                "lib/libc/include/any-windows-any/", "lib/libunwind/"};
constexpr int64_t WORK_SPACE = 1300LL << 20;  // (tools/pc_build.py's: the C, the objects, Zig's cache, the library)

// ---- paths (wide: the player's folders can have any name)
wchar_t g_home[MAX_PATH], g_root[MAX_PATH], g_exe_dir[MAX_PATH];
bool g_ready;

void wjoin(wchar_t* out, size_t n, const wchar_t* a, const wchar_t* b) {
    _snwprintf(out, n, L"%ls\\%ls", a, b);
    out[n - 1] = 0;
}
void in_root(wchar_t* out, size_t n, const wchar_t* name) { wjoin(out, n, g_root, name); }
bool to_utf8(const wchar_t* w, char* out, size_t n) {
    if (WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, nullptr, nullptr) > 0) return true;
    out[0] = 0;
    return false;
}
bool from_utf8(const char* s, wchar_t* out, size_t n) {
    if (MultiByteToWideChar(CP_UTF8, 0, s, -1, out, (int)n) > 0) {
        for (wchar_t* p = out; *p; p++)
            if (*p == L'/') *p = L'\\';
        return true;
    }
    out[0] = 0;
    return false;
}
bool exists(const wchar_t* p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }
bool is_dir(const wchar_t* p) {
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
// The folders up to path, made where missing (path: a folder)
void mkdirs(const wchar_t* path) {
    wchar_t p[MAX_PATH];
    wcsncpy(p, path, MAX_PATH - 1), p[MAX_PATH - 1] = 0;
    for (wchar_t* s = p + 3; *s; s++)
        if (*s == L'\\' || *s == L'/') {
            wchar_t c = *s;
            *s = 0;
            CreateDirectoryW(p, nullptr);
            *s = c;
        }
    CreateDirectoryW(p, nullptr);
}
int64_t file_size(const wchar_t* p) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(p, GetFileExInfoStandard, &a)) return -1;
    return (int64_t)a.nFileSizeHigh << 32 | a.nFileSizeLow;
}
// What a folder takes, its links not followed
int64_t tree_size(const wchar_t* dir) {
    wchar_t pat[MAX_PATH], sub[MAX_PATH];
    wjoin(pat, MAX_PATH, dir, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int64_t n = 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            wjoin(sub, MAX_PATH, dir, fd.cFileName);
            n += tree_size(sub);
        } else {
            n += (int64_t)fd.nFileSizeHigh << 32 | fd.nFileSizeLow;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}
// A folder and all it holds, deleted; never through a link (a junction, a symbolic link: the link alone goes)
void tree_delete(const wchar_t* dir) {
    DWORD a = GetFileAttributesW(dir);
    if (a == INVALID_FILE_ATTRIBUTES) return;
    if (!(a & FILE_ATTRIBUTE_DIRECTORY)) {
        SetFileAttributesW(dir, FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(dir);
        return;
    }
    if (a & FILE_ATTRIBUTE_REPARSE_POINT) {
        RemoveDirectoryW(dir);
        return;
    }
    wchar_t pat[MAX_PATH], sub[MAX_PATH];
    wjoin(pat, MAX_PATH, dir, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            wjoin(sub, MAX_PATH, dir, fd.cFileName);
            tree_delete(sub);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir);
}
bool read_text(const wchar_t* p, char* out, size_t n) {
    out[0] = 0;
    FILE* f = _wfopen(p, L"rb");
    if (!f) return false;
    size_t k = fread(out, 1, n - 1, f);
    fclose(f);
    out[k] = 0;
    while (k && (out[k - 1] == '\n' || out[k - 1] == '\r' || out[k - 1] == ' ')) out[--k] = 0;
    return true;
}
bool write_text(const wchar_t* p, const char* text) {
    FILE* f = _wfopen(p, L"wb");
    if (!f) return false;
    bool ok = fputs(text, f) >= 0;
    return fclose(f) == 0 && ok;
}
// A tool in place: its .installed mark names it, from either of its sources
bool tool_installed(const Tool* t) {
    wchar_t name[32], mark[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, t->name, -1, name, 32);
    _snwprintf(mark, MAX_PATH, L"%ls\\%ls\\.installed", g_root, name);
    char text[200], want[200];
    if (!read_text(mark, text, sizeof text)) return false;
    for (const Tool* s = t; s; s = other_source(s)) {
        snprintf(want, sizeof want, "%s %s %s", s->name, s->version, s->sha256);
        if (!strcmp(text, want)) return true;
    }
    return false;
}
// Where a source's download is kept while it comes (downloads\<its file name>.part: a cut one goes on from there)
void part_path(const Tool* t, wchar_t* out, size_t n) {
    const char* base = strrchr(t->url, '/');
    wchar_t b[200];
    MultiByteToWideChar(CP_UTF8, 0, base ? base + 1 : t->name, -1, b, 200);
    _snwprintf(out, n, L"%ls\\downloads\\%ls.part", g_root, b);
    out[n - 1] = 0;
}

// ---- the job (the launcher's thread reads it, the builder's thread writes it)
SRWLOCK g_lock = SRWLOCK_INIT;
PcbJob g_job;
constexpr int TAIL = 60;
char g_tail[TAIL][160];
int g_tail_n, g_tail_at;
volatile LONG g_cancel, g_busy;  // (g_busy: a build's thread is on, from pcb_start until it ends)
HANDLE g_jobobj, g_python;       // (the build's processes: their job, and Python's own)
FILE* g_log;                     // (builder_log.txt, the builder's thread only)

template <typename F>
void job_set(F fn) {
    AcquireSRWLockExclusive(&g_lock);
    fn(g_job);
    ReleaseSRWLockExclusive(&g_lock);
}
void set_stage(int s) {
    job_set([s](PcbJob& j) {
        if (s != j.stage) j.stage = s, j.stage_started_ms = (int64_t)SDL_GetTicks(), j.done = j.total = 0;
    });
}
void set_step(const char* detail, int64_t done, int64_t total) {
    job_set([&](PcbJob& j) {
        if (detail) snprintf(j.detail, sizeof j.detail, "%s", detail);
        j.done = done, j.total = total;
    });
}

// A line of the build's: builder_log.txt, the game's log, and the screen's last lines (not the @@ ones)
void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char* fmt, ...) {
    char line[1200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (g_log) fprintf(g_log, "%s\n", line), fflush(g_log);
    if (!strncmp(line, "@@", 2)) return;
    fprintf(stderr, "[builder] %s\n", line);
    AcquireSRWLockExclusive(&g_lock);
    snprintf(g_tail[g_tail_at], sizeof g_tail[0], "%s", line);
    g_tail_at = (g_tail_at + 1) % TAIL;
    if (g_tail_n < TAIL) g_tail_n++;
    ReleaseSRWLockExclusive(&g_lock);
}

// ---- the info (sizes walk thousands of files: a thread of its own, when something changed)
PcbInfo g_info;
volatile LONG g_info_dirty = 1, g_info_thread;
DWORD WINAPI info_thread(void* arg) {
    while (InterlockedExchange(&g_info_dirty, 0)) {
        PcbInfo in;
        wchar_t p[MAX_PATH];
        char text[200];
        for (const Tool* t : TOOLS) {
            wchar_t name[32];
            MultiByteToWideChar(CP_UTF8, 0, t->name, -1, name, 32);
            in_root(p, MAX_PATH, name);
            if (tool_installed(t)) {
                in.installed += tree_size(p);
            } else {
                int64_t left = t->size;  // (what is left of the source with the most already here)
                for (const Tool* s = t; s; s = other_source(s)) {
                    wchar_t part[MAX_PATH];
                    part_path(s, part, MAX_PATH);
                    int64_t have = file_size(part);
                    if (have > 0 && have <= s->size && s->size - have < left) left = s->size - have;
                }
                in.to_download += left;
                in.needed += t->size + t->unpacked;
            }
        }
        in.used = is_dir(g_root) ? tree_size(g_root) : 0;
        in_root(p, MAX_PATH, L"work");
        int64_t work = is_dir(p) ? tree_size(p) : 0;
        in.needed += WORK_SPACE > work ? WORK_SPACE - work : 0;
        in.half_done = work > 0;
        in_root(p, MAX_PATH, L"state.txt");
        in.interrupted = read_text(p, text, sizeof text) && !strncmp(text, "running", 7);
        wchar_t zig_cc[MAX_PATH];
        wjoin(p, MAX_PATH, g_exe_dir, L"builder_tools.zip");
        wjoin(zig_cc, MAX_PATH, g_exe_dir, L"zigcc.exe");
        in.scripts_ok = exists(p) && exists(zig_cc);
        to_utf8(g_root, in.where, sizeof in.where);
        AcquireSRWLockExclusive(&g_lock);
        int64_t free_now = g_info.free;
        g_info = in;
        g_info.free = free_now;
        ReleaseSRWLockExclusive(&g_lock);
    }
    InterlockedExchange(&g_info_thread, 0);
    if (g_info_dirty && !InterlockedExchange(&g_info_thread, 1)) return info_thread(arg);  // (asked again meanwhile)
    return 0;
}
void info_changed() {
    InterlockedExchange(&g_info_dirty, 1);
    if (InterlockedExchange(&g_info_thread, 1)) return;
    HANDLE h = CreateThread(nullptr, 0, info_thread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    else InterlockedExchange(&g_info_thread, 0);
}

// ---- the folders a start over set aside, deleted (one cut short by the program's end: the next time)
DWORD WINAPI drop_trash(void*) {
    wchar_t pat[MAX_PATH], p[MAX_PATH];
    in_root(pat, MAX_PATH, L"*.trash-*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            in_root(p, MAX_PATH, fd.cFileName);
            tree_delete(p);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    _snwprintf(pat, MAX_PATH, L"%ls\\builder.trash-*", g_home);
    h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            wjoin(p, MAX_PATH, g_home, fd.cFileName);
            tree_delete(p);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    info_changed();
    return 0;
}
void drop_trash_later() {
    HANDLE h = CreateThread(nullptr, 0, drop_trash, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
}
// One of the builder's folders set aside in one step (renamed: no run finds half of it), or deleted where it can't be
void set_aside(const wchar_t* path) {
    if (!exists(path)) return;
    wchar_t trash[MAX_PATH];
    _snwprintf(trash, MAX_PATH, L"%ls.trash-%llu", path, (unsigned long long)GetTickCount64());
    if (!MoveFileExW(path, trash, 0)) tree_delete(path);
}

// ---- downloads (WinHTTP: the system's TLS, its proxy settings)
struct Result {
    bool ok = true;
    char why[400] = "";
};
void why(Result* r, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void why(Result* r, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r->why, sizeof r->why, fmt, ap);
    va_end(ap);
    r->ok = false;
}

// The loopback (or 0.x) address the system's resolver answers for a host, "" when none: a provider's or a filter's
// block (127.9.9.9 was seen for files.pythonhosted.org)
void loopback_of(const char* host, char* out, size_t n) {
    out[0] = 0;
    WSADATA w;
    if (WSAStartup(MAKEWORD(2, 2), &w)) return;
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    if (!getaddrinfo(host, "443", &hints, &res) && res) {
        auto* a = reinterpret_cast<sockaddr_in*>(res->ai_addr);
        auto* b = reinterpret_cast<unsigned char*>(&a->sin_addr);
        if (b[0] == 127 || b[0] == 0) snprintf(out, n, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
        freeaddrinfo(res);
    }
    WSACleanup();
}
void blocked_note(const char* host, char* out, size_t n) {
    char a[32];
    loopback_of(host, a, sizeof a);
    out[0] = 0;
    if (a[0]) snprintf(out, n, " (%s answers %s here: the internet provider or a filter blocks it)", host, a);
}

// A file's SHA-256, in hex (Windows' own, through CNG); false when it can't be read
bool sha256_file(const wchar_t* path, char hex[65]) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return false;
    bool ok = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0;
    FILE* f = ok ? _wfopen(path, L"rb") : nullptr;
    ok = f != nullptr;
    static unsigned char buf[1 << 16];
    size_t k;
    while (ok && (k = fread(buf, 1, sizeof buf, f)) > 0) ok = BCryptHashData(hash, buf, (ULONG)k, 0) == 0;
    if (f) fclose(f);
    unsigned char digest[32];
    ok = ok && BCryptFinishHash(hash, digest, sizeof digest, 0) == 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (ok)
        for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return ok;
}

Result download(const Tool* t, const wchar_t* part) {
    Result r;
    wchar_t url[600], host[200], path[600];
    MultiByteToWideChar(CP_UTF8, 0, t->url, -1, url, 600);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    uc.lpszHostName = host, uc.dwHostNameLength = 200, uc.lpszUrlPath = path, uc.dwUrlPathLength = 600;
    if (!WinHttpCrackUrl(url, 0, 0, &uc)) {
        why(&r, "bad address %s", t->url);
        return r;
    }
    char host8[200];
    to_utf8(host, host8, sizeof host8);
    for (int attempt = 0;; attempt++) {
        int64_t have = file_size(part);
        if (have > t->size) DeleteFileW(part), have = 0;
        if (have < 0) have = 0;
        if (have < t->size) {
            HINTERNET ses = WinHttpOpen(L"MH4U-PC-builder/" MH4U_APP_VERSION, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (!ses) ses = WinHttpOpen(L"MH4U-PC-builder", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                        WINHTTP_NO_PROXY_BYPASS, 0);
            HINTERNET con = ses ? WinHttpConnect(ses, host, uc.nPort, 0) : nullptr;
            HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                     uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                                : nullptr;
            if (req) WinHttpSetTimeouts(req, 20000, 20000, 30000, 30000);
            wchar_t range[64] = L"";
            if (have > 0) _snwprintf(range, 64, L"Range: bytes=%lld-", (long long)have);
            bool sent = req && WinHttpSendRequest(req, range[0] ? range : WINHTTP_NO_ADDITIONAL_HEADERS, range[0] ? (DWORD)-1L : 0,
                                                  WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                        WinHttpReceiveResponse(req, nullptr);
            if (!sent) {
                DWORD e = GetLastError();
                char note[200];
                blocked_note(host8, note, sizeof note);
                why(&r, "%s can't be reached (WinHTTP error %lu)%s", host8, (unsigned long)e, note);
                if (req) WinHttpCloseHandle(req);
                if (con) WinHttpCloseHandle(con);
                if (ses) WinHttpCloseHandle(ses);
                return r;
            }
            DWORD code = 0, sz = sizeof code;
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code,
                                &sz, WINHTTP_NO_HEADER_INDEX);
            bool append = code == 206;
            if (code != 200 && !append) {
                why(&r, "%s: HTTP %lu", t->url, (unsigned long)code);
                WinHttpCloseHandle(req), WinHttpCloseHandle(con), WinHttpCloseHandle(ses);
                return r;
            }
            if (have > 0)
                say("%s: %s", t->name, append ? "the download goes on from where it stopped (HTTP 206)"
                                              : "the server sends it whole again (HTTP 200)");
            if (!append) have = 0;
            FILE* f = _wfopen(part, append ? L"ab" : L"wb");
            if (!f) {
                why(&r, "can't write %s's download (%s)", t->name, strerror(errno));
                WinHttpCloseHandle(req), WinHttpCloseHandle(con), WinHttpCloseHandle(ses);
                return r;
            }
            static char buf[1 << 16];
            char detail[32];
            snprintf(detail, sizeof detail, "download:%s@%s", t->name, t->from);  // (the page names the source)
            int64_t last = 0;
            bool failed = false;
            for (;;) {
                if (g_cancel) break;
                DWORD got = 0;
                if (!WinHttpReadData(req, buf, sizeof buf, &got)) {
                    failed = true;
                    break;
                }
                if (!got) break;
                if (fwrite(buf, 1, got, f) != got) {
                    failed = true;
                    break;
                }
                have += got;
                if (have - last > (256 << 10)) set_step(detail, have, t->size), last = have;
                if (have > t->size) break;
            }
            DWORD e = GetLastError();
            bool closed = fclose(f) == 0;
            WinHttpCloseHandle(req), WinHttpCloseHandle(con), WinHttpCloseHandle(ses);
            if (g_cancel) {
                why(&r, "cancelled");
                return r;
            }
            if (!closed) {
                why(&r, "can't write %s's download: the disk may be full", t->name);
                return r;
            }
            if (failed) {
                why(&r, "%s: the download was cut (error %lu); it goes on from there next time", t->name, (unsigned long)e);
                return r;
            }
            set_step(detail, have < t->size ? have : t->size, t->size);
        }
        // whole: its SHA-256
        if (file_size(part) == t->size) {
            char hex[65] = "";
            if (sha256_file(part, hex)) {
                if (!strcmp(hex, t->sha256)) return r;
                say("%s: the download's SHA-256 is %s, not %s: downloaded again", t->name, hex, t->sha256);
            }
        }
        DeleteFileW(part);  // (another file, or a damaged one: once more from the start, then it's an error)
        if (attempt >= 1) {
            why(&r, "%s: the download from %s doesn't match its SHA-256", t->name, t->from);
            return r;
        }
    }
}

// ---- unpacking: into a folder beside its place, renamed when whole (a cut never leaves half a tool)
bool keep_zig(const char* name) {
    for (const char* k : ZIG_KEEP) {
        size_t n = strlen(k);
        if (k[n - 1] == '/' ? !strncmp(name, k, n) : !strcmp(name, k)) return true;
    }
    return false;
}
// A name of the archive as a path under dest: none that leaves it (.., a drive, a leading slash)
bool safe_name(const char* name) {
    if (!*name || name[0] == '/' || name[0] == '\\' || strchr(name, ':')) return false;
    for (const char* p = name; *p;) {
        const char* e = p + strcspn(p, "/\\");
        if (e - p == 2 && p[0] == '.' && p[1] == '.') return false;
        p = *e ? e + 1 : e;
    }
    return true;
}
struct UnpackProgress {
    int64_t done = 0, total = 0;
};
void unpack_progress(void* ud, uint64_t n) { static_cast<UnpackProgress*>(ud)->done += (int64_t)n; }

Result unpack(const Tool* t, const wchar_t* archive, const wchar_t* dest) {
    Result r;
    char a8[MAX_PATH * 3], d8[MAX_PATH * 3];
    to_utf8(archive, a8, sizeof a8), to_utf8(dest, d8, sizeof d8);
    Unzip z;
    UnzipResult u = unzip_open(&z, a8);
    if (u != UNZIP_OK) {
        unzip_close(&z);
        why(&r, "%s: the archive can't be read (%d)", t->name, (int)u);
        return r;
    }
    // (Zig's archives: only what is under their prefix, ziglang/ in the wheel, zig-windows-x86_64-0.13.0/ in the zip, and
    // of that only ZIG_KEEP, unpacked without the prefix: the same tree from either)
    const char* prefix = t->prefix;
    size_t np = prefix ? strlen(prefix) : 0;
    auto kept = [&](const char* n) { return !prefix || (!strncmp(n, prefix, np) && keep_zig(n + np)); };
    char detail[32];
    snprintf(detail, sizeof detail, "unpack:%s", t->name);
    UnpackProgress pr;
    for (int i = 0; i < z.count; i++)
        if (!z.entries[i].dir && kept(z.entries[i].name)) pr.total += (int64_t)z.entries[i].size;
    for (int i = 0; i < z.count && r.ok; i++) {
        UnzipEntry* e = &z.entries[i];
        if (e->dir || !kept(e->name)) continue;
        if (g_cancel) {
            why(&r, "cancelled");
            break;
        }
        const char* rel = e->name + np;
        if (!safe_name(rel)) {
            why(&r, "%s: a path outside its folder in the archive: %s", t->name, e->name);
            break;
        }
        char to[MAX_PATH * 3];
        snprintf(to, sizeof to, "%s/%s", d8, rel);
        wchar_t wto[MAX_PATH];
        from_utf8(to, wto, MAX_PATH);
        wchar_t* slash = wcsrchr(wto, L'\\');
        if (slash) *slash = 0, mkdirs(wto), *slash = L'\\';
        u = unzip_extract(&z, i, to, unpack_progress, &pr);
        if (u != UNZIP_OK) why(&r, "%s: %s couldn't be unpacked (%d: %s)", t->name, e->name, (int)u, SDL_GetError());
        set_step(detail, pr.done, pr.total);
    }
    unzip_close(&z);
    return r;
}

// python.org's embeddable package reads its paths from pythonNN._pth, beside it, and nothing from the environment: the
// builder's tools (build_game.py's own imports, and phone_build.py's beside them) are named there
Result python_paths(const wchar_t* dir) {
    Result r;
    wchar_t pat[MAX_PATH], p[MAX_PATH];
    wjoin(pat, MAX_PATH, dir, L"python3*._pth");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        why(&r, "python: the package has no ._pth file");
        return r;
    }
    FindClose(h);
    wjoin(p, MAX_PATH, dir, fd.cFileName);
    char zip[64];
    WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, zip, sizeof zip, nullptr, nullptr);
    char* dot = strstr(zip, "._pth");
    if (dot) strcpy(dot, ".zip");
    char text[400];
    snprintf(text, sizeof text, "%s\r\n.\r\n..\\scripts\\tools\r\n..\\scripts\\tools\\android\r\n", zip);
    if (!write_text(p, text)) why(&r, "python: its ._pth file can't be written");
    return r;
}

// A tool from one of its sources: downloaded into downloads\, checked, unpacked beside its place and renamed when whole
Result install_from(const Tool* t, const wchar_t* dest) {
    Result r;
    wchar_t tmp[MAX_PATH], mark[MAX_PATH], part[MAX_PATH], dl[MAX_PATH];
    char want[200];
    snprintf(want, sizeof want, "%s %s %s", t->name, t->version, t->sha256);
    in_root(dl, MAX_PATH, L"downloads");
    mkdirs(dl);
    part_path(t, part, MAX_PATH);
    uint64_t t0 = SDL_GetTicks();
    int64_t had = file_size(part);
    set_step(nullptr, 0, t->size);
    r = download(t, part);
    if (!r.ok) return r;
    uint64_t t1 = SDL_GetTicks();
    say("%s %s: %lld MB downloaded from %s in %llu s%s, SHA-256 checked", t->name, t->version,
        (long long)((t->size - (had > 0 ? had : 0)) >> 20), t->from, (unsigned long long)((t1 - t0) / 1000),
        had > 0 ? " (part of it was already here)" : "");
    _snwprintf(tmp, MAX_PATH, L"%ls.new", dest);
    tree_delete(tmp);
    mkdirs(tmp);
    r = unpack(t, part, tmp);
    if (r.ok && t == &PYTHON) r = python_paths(tmp);
    if (!r.ok) return r;
    wjoin(mark, MAX_PATH, tmp, L".installed");
    if (!write_text(mark, want)) {
        why(&r, "%s: can't write in %s", t->name, "builder");
        return r;
    }
    set_aside(dest);
    if (!MoveFileExW(tmp, dest, 0)) {
        why(&r, "%s: can't move it in place (error %lu)", t->name, GetLastError());
        return r;
    }
    DeleteFileW(part);
    say("%s: unpacked in %llu s, %lld MB", t->name, (unsigned long long)((SDL_GetTicks() - t1) / 1000),
        (long long)(tree_size(dest) >> 20));
    return r;
}
void drop_parts(const Tool* t) {
    for (const Tool* s = t; s; s = other_source(s)) {
        wchar_t part[MAX_PATH];
        part_path(s, part, MAX_PATH);
        DeleteFileW(part);
    }
}
// A tool in place: from its own source, else from its other one (Zig: ziglang.org when PyPI fails, or straight away when
// PyPI's host answers a loopback address here). Not when it was cancelled or the disk is the trouble: the other source
// would fail the same. Once in place, no source's half download stays
Result install_tool(const Tool* t) {
    Result r;
    if (tool_installed(t)) return r;
    wchar_t name[32], dest[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, t->name, -1, name, 32);
    in_root(dest, MAX_PATH, name);
    const Tool* other = other_source(t);
    char host[200], addr[32] = "";
    const char* h = strstr(t->url, "://");
    h = h ? h + 3 : t->url;
    snprintf(host, sizeof host, "%.*s", (int)strcspn(h, "/:"), h);
    if (other) loopback_of(host, addr, sizeof addr);
    if (addr[0]) {
        why(&r, "%s: %s is blocked here (%s answers %s)", t->name, t->from, host, addr);
        say("%s: %s is blocked here (%s answers %s): from %s", t->name, t->from, host, addr, other->from);
    } else {
        r = install_from(t, dest);
        if (r.ok) drop_parts(t);
        if (r.ok || !other || g_cancel || strstr(r.why, "disk may be full") || strstr(r.why, "can't write") ||
            strstr(r.why, "can't move"))
            return r;
        say("%s: from %s it failed (%s): from %s instead", t->name, t->from, r.why, other->from);
    }
    Result first = r;
    r = install_from(other, dest);
    if (r.ok) {
        drop_parts(t);
    } else if (!g_cancel) {
        char second[400];
        snprintf(second, sizeof second, "%s", r.why);
        why(&r, "%s; and from %s: %s", first.why, other->from, second);
    }
    return r;
}

// builder_tools.zip beside MH4U.exe (mh4n's tools/ and config/, the very ones this program was built from) into
// builder\scripts, again whenever it changes
Result install_scripts() {
    Result r;
    wchar_t zip[MAX_PATH], dest[MAX_PATH], tmp[MAX_PATH], mark[MAX_PATH], zig_cc[MAX_PATH];
    wjoin(zip, MAX_PATH, g_exe_dir, L"builder_tools.zip");
    wjoin(zig_cc, MAX_PATH, g_exe_dir, L"zigcc.exe");
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(zip, GetFileExInfoStandard, &a) || !exists(zig_cc)) {
        why(&r, "builder_tools.zip or zigcc.exe is not beside MH4U.exe: unzip all of the program's zip again");
        return r;
    }
    char stamp[200], text[200];
    snprintf(stamp, sizeof stamp, "%s %s %lu %lu %lu", MH4U_APP_VERSION, MH4U_PC_CODE_ID, a.nFileSizeLow,
             a.ftLastWriteTime.dwLowDateTime, a.ftLastWriteTime.dwHighDateTime);
    in_root(dest, MAX_PATH, L"scripts");
    wjoin(mark, MAX_PATH, dest, L".installed");
    if (read_text(mark, text, sizeof text) && !strcmp(text, stamp)) return r;
    _snwprintf(tmp, MAX_PATH, L"%ls.new", dest);
    tree_delete(tmp);
    mkdirs(tmp);
    static const Tool SCRIPTS = {"scripts", "", "", "", 0, 0, nullptr, ""};
    r = unpack(&SCRIPTS, zip, tmp);
    if (!r.ok) return r;
    wjoin(mark, MAX_PATH, tmp, L".installed");
    write_text(mark, stamp);
    set_aside(dest);
    if (!MoveFileExW(tmp, dest, 0)) why(&r, "the builder's scripts can't be moved in place (error %lu)", GetLastError());
    return r;
}

// ---- the build's process
// How many compilers at once: all the cores but one, as the free memory allows (a compiler of the game code, Zig's
// driver and its clang, takes up to about 400 MB), half of the last run's when that one stopped while compiling. The free
// memory is the smaller of the physical memory's and the commit's (what the page file can still back)
int jobs_for_now() {
    int cores = (int)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    int jobs = cores > 1 ? cores - 1 : 1;
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof m;
    if (GlobalMemoryStatusEx(&m)) {
        uint64_t smaller = m.ullAvailPhys < m.ullAvailPageFile ? m.ullAvailPhys : m.ullAvailPageFile;
        int64_t avail = (int64_t)(smaller >> 20) - 1536;
        int by_memory = avail > 400 ? (int)(avail / 400) : 1;
        if (by_memory < jobs) jobs = by_memory;
    }
    wchar_t p[MAX_PATH];
    char text[64];
    in_root(p, MAX_PATH, L"last_failure.txt");
    if (read_text(p, text, sizeof text) && !strncmp(text, "compile ", 8)) {
        int last = atoi(text + 8) / 2;
        if (last >= 1 && last < jobs) jobs = last;
    }
    if (const char* forced = SDL_getenv("MH4U_BUILDER_JOBS"); forced && atoi(forced) > 0) jobs = atoi(forced);  // (tests)
    if (jobs < 1) jobs = 1;
    say("%d compilers at once (%d cores, %lld MB of memory free, %lld MB of commit)", jobs, cores,
        (long long)(m.ullAvailPhys >> 20), (long long)(m.ullAvailPageFile >> 20));
    return jobs;
}

// An argument for a Windows command line (the C runtime's rules: quotes, and the backslashes before one doubled)
void add_arg(wchar_t* cmd, size_t n, const wchar_t* arg) {
    size_t at = wcslen(cmd);
    if (at && at < n - 1) cmd[at++] = L' ';
    bool quote = !*arg || wcspbrk(arg, L" \t\"");
    if (quote && at < n - 1) cmd[at++] = L'"';
    for (const wchar_t* p = arg; *p && at < n - 4; p++) {
        size_t bs = 0;
        while (p[bs] == L'\\') bs++;
        if (bs && (p[bs] == L'"' || (!p[bs] && quote))) {
            for (size_t k = 0; k < bs * 2 && at < n - 4; k++) cmd[at++] = L'\\';
            p += bs - 1;
            continue;
        }
        if (*p == L'"') cmd[at++] = L'\\';
        cmd[at++] = *p;
    }
    if (quote && at < n - 1) cmd[at++] = L'"';
    cmd[at] = 0;
}

int cmp_env(const void* a, const void* b) { return _wcsicmp(*(const wchar_t* const*)a, *(const wchar_t* const*)b); }
// The build's environment: this one's, with no Python or Zig variable of the player's, its temporary files and Zig's
// cache in the builder's folder (never the system drive's temporary folder), and zigcc.exe's Zig
wchar_t* build_env(const wchar_t* tmp, const wchar_t* cache, const wchar_t* zig) {
    wchar_t* base = GetEnvironmentStringsW();
    size_t count = 0;
    for (wchar_t* p = base; *p; p += wcslen(p) + 1) count++;
    auto** list = static_cast<wchar_t**>(calloc(count + 8, sizeof(wchar_t*)));
    static wchar_t extra[6][MAX_PATH + 40];
    int n = 0, k = 0;
    for (wchar_t* p = base; *p; p += wcslen(p) + 1) {
        if (!_wcsnicmp(p, L"PYTHON", 6) || !_wcsnicmp(p, L"ZIG_", 4) || !_wcsnicmp(p, L"TMP=", 4) || !_wcsnicmp(p, L"TEMP=", 5) ||
            !_wcsnicmp(p, L"MH4U_ZIG=", 9))
            continue;
        list[n++] = p;
    }
    _snwprintf(extra[k], MAX_PATH + 40, L"TMP=%ls", tmp), list[n++] = extra[k++];
    _snwprintf(extra[k], MAX_PATH + 40, L"TEMP=%ls", tmp), list[n++] = extra[k++];
    _snwprintf(extra[k], MAX_PATH + 40, L"ZIG_GLOBAL_CACHE_DIR=%ls", cache), list[n++] = extra[k++];
    _snwprintf(extra[k], MAX_PATH + 40, L"ZIG_LOCAL_CACHE_DIR=%ls", cache), list[n++] = extra[k++];
    _snwprintf(extra[k], MAX_PATH + 40, L"MH4U_ZIG=%ls", zig), list[n++] = extra[k++];
    qsort(list, (size_t)n, sizeof *list, cmp_env);
    size_t total = 1;
    for (int i = 0; i < n; i++) total += wcslen(list[i]) + 1;
    auto* env = static_cast<wchar_t*>(malloc(total * sizeof(wchar_t)));
    wchar_t* at = env;
    for (int i = 0; i < n; i++) {
        size_t len = wcslen(list[i]) + 1;
        memcpy(at, list[i], len * sizeof *at);
        at += len;
    }
    *at = 0;
    free(list);
    FreeEnvironmentStringsW(base);
    return env;
}

struct RunOut {
    char stage[16] = "", text[400] = "", library[1100] = "";
};
// One @@ line of pc_build.py (phone_build.py's)
void at_line(char* line, RunOut* o) {
    char *w = line + 2, *rest = strchr(w, ' ');
    if (rest) *rest++ = 0;
    else rest = w + strlen(w);
    if (!strcmp(w, "stage")) {
        int s = !strcmp(rest, "mods") || !strcmp(rest, "extract") || !strcmp(rest, "version") ? 1
                : !strcmp(rest, "recompile") || !strcmp(rest, "shaders")                    ? 2
                : !strcmp(rest, "compile") || !strcmp(rest, "link")                         ? 3
                : !strcmp(rest, "install")                                                  ? 4
                                                                                            : -1;
        if (s >= 0) set_stage(s);
        char d[32];
        snprintf(d, sizeof d, "%s", rest);
        set_step(d, 0, 0);
    } else if (!strcmp(w, "progress")) {
        char what[32];
        long long done = 0, total = 0;
        if (sscanf(rest, "%31s %lld %lld", what, &done, &total) == 3) {
            int s = !strcmp(what, "recompile") ? 2 : !strcmp(what, "compile") ? 3 : -1;
            if (s >= 0) set_stage(s), set_step(nullptr, done, total);
        }
    } else if (!strcmp(w, "error")) {
        char* text = strchr(rest, ' ');
        if (text) *text++ = 0;
        snprintf(o->stage, sizeof o->stage, "%s", rest);
        snprintf(o->text, sizeof o->text, "%s", text ? text : "");
    } else if (!strcmp(w, "done")) {
        snprintf(o->library, sizeof o->library, "%s", rest);
    }
}

struct StartArg {
    wchar_t game[MAX_PATH], base[MAX_PATH], update[MAX_PATH], mods_flag[16], mods[MAX_PATH];
    char game8[1100];
};

int run_build(const StartArg* a, int jobs, RunOut* o) {
    wchar_t py[MAX_PATH], script[MAX_PATH], work[MAX_PATH], zig[MAX_PATH], zig_cc[MAX_PATH], tmp[MAX_PATH], cache[MAX_PATH],
        njobs[16];
    _snwprintf(py, MAX_PATH, L"%ls\\python\\python.exe", g_root);
    _snwprintf(script, MAX_PATH, L"%ls\\scripts\\tools\\pc_build.py", g_root);
    in_root(work, MAX_PATH, L"work");
    _snwprintf(zig, MAX_PATH, L"%ls\\zig\\zig.exe", g_root);
    wjoin(zig_cc, MAX_PATH, g_exe_dir, L"zigcc.exe");
    in_root(tmp, MAX_PATH, L"tmp");
    _snwprintf(cache, MAX_PATH, L"%ls\\zig-cache", work);
    mkdirs(tmp);
    _snwprintf(njobs, 16, L"%d", jobs);
    wchar_t id[32];
    MultiByteToWideChar(CP_UTF8, 0, MH4U_PC_CODE_ID, -1, id, 32);
    static wchar_t cmd[16384];
    cmd[0] = 0;
    // (MH4U_BUILDER_DEST, a test: the library into that folder, not the game folder, which is only read then)
    wchar_t dest[MAX_PATH];
    if (!GetEnvironmentVariableW(L"MH4U_BUILDER_DEST", dest, MAX_PATH) || !dest[0]) wcscpy(dest, a->game);
    const wchar_t* args[] = {py,     L"-X",     L"utf8",  L"-u",     script, L"--update", a->update, L"--game", a->base,
                             L"--work", work,   L"--dest", dest,     L"--cc", zig_cc,     L"--jobs",  njobs,     L"--clean",
                             L"--expect-id", id};
    for (const wchar_t* arg : args) add_arg(cmd, 16384, arg);
    if (a->mods_flag[0] && is_dir(a->mods)) add_arg(cmd, 16384, a->mods_flag), add_arg(cmd, 16384, a->mods);
    char cmd8[16384 * 3];
    to_utf8(cmd, cmd8, sizeof cmd8);
    say("run: %s", cmd8);

    SECURITY_ATTRIBUTES sa = {sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -2;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdOutput = si.StartupInfo.hStdError = wr;
    si.StartupInfo.hStdInput = nullptr;
    // (only the pipe goes to the build: no other handle of this program)
    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
    si.lpAttributeList = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(malloc(attr_size));
    HANDLE inherit[1] = {wr};
    bool attrs = si.lpAttributeList && InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attr_size) &&
                 UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof inherit,
                                           nullptr, nullptr);
    wchar_t* env = build_env(tmp, cache, zig);
    PROCESS_INFORMATION pi{};
    DWORD flags = CREATE_SUSPENDED | CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS | CREATE_UNICODE_ENVIRONMENT |
                  (attrs ? EXTENDED_STARTUPINFO_PRESENT : 0);
    BOOL started = CreateProcessW(py, cmd, nullptr, nullptr, TRUE, flags, env, g_root, (STARTUPINFOW*)&si, &pi);
    DWORD err = GetLastError();
    free(env);
    if (attrs) DeleteProcThreadAttributeList(si.lpAttributeList);
    free(si.lpAttributeList);
    CloseHandle(wr);
    if (!started) {
        CloseHandle(rd);
        snprintf(o->stage, sizeof o->stage, "start");
        snprintf(o->text, sizeof o->text, "Python couldn't start (error %lu)", (unsigned long)err);
        return -2;
    }
    // the job: below normal for all of it, and ended with Cancel or with this program (a crash too)
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim{};
    lim.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_PRIORITY_CLASS;
    lim.BasicLimitInformation.PriorityClass = BELOW_NORMAL_PRIORITY_CLASS;
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &lim, sizeof lim) ||
        !AssignProcessToJobObject(job, pi.hProcess)) {
        say("(the build's processes couldn't be put in a job, error %lu: Cancel ends Python only)", GetLastError());
        if (job) CloseHandle(job);
        job = nullptr;
    }
    AcquireSRWLockExclusive(&g_lock);
    g_jobobj = job;
    g_python = pi.hProcess;
    ReleaseSRWLockExclusive(&g_lock);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    if (g_cancel) pcb_cancel();

    char buf[4096], line[2048];
    size_t at = 0;
    DWORD got;
    while (ReadFile(rd, buf, sizeof buf, &got, nullptr) && got) {
        for (DWORD i = 0; i < got; i++) {
            char c = buf[i];
            if (c == '\r') continue;
            if (c != '\n' && at < sizeof line - 1) {
                line[at++] = c;
                continue;
            }
            if (c != '\n') continue;
            line[at] = 0, at = 0;
            say("%s", line);
            if (!strncmp(line, "@@", 2)) at_line(line, o);
        }
    }
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    AcquireSRWLockExclusive(&g_lock);
    g_jobobj = nullptr;
    g_python = nullptr;
    ReleaseSRWLockExclusive(&g_lock);
    CloseHandle(pi.hProcess);
    if (job) TerminateJobObject(job, 1), CloseHandle(job);  // (anything of the build's still there)
    say("exit %lu", (unsigned long)code);
    return (int)code;
}

void finish(int status, const char* stage, const char* text, int jobs) {
    wchar_t p[MAX_PATH];
    in_root(p, MAX_PATH, L"last_failure.txt");
    if (status == PCB_FAILED && stage && (!strcmp(stage, "compile") || !strcmp(stage, "link") || !strcmp(stage, "memory"))) {
        char t[32];
        snprintf(t, sizeof t, "compile %d", jobs);
        write_text(p, t);
    } else if (status == PCB_DONE) {
        DeleteFileW(p);
    }
    in_root(p, MAX_PATH, L"state.txt");
    write_text(p, status == PCB_DONE ? "done" : status == PCB_CANCELLED ? "cancelled" : "failed");
    job_set([&](PcbJob& j) {
        j.status = status;
        j.ended_ms = (int64_t)SDL_GetTicks();
        if (stage && *stage) {
            snprintf(j.error_stage, sizeof j.error_stage, "%s", stage);
            snprintf(j.error_text, sizeof j.error_text, "%s", text ? text : "");
        }
    });
    say("%s%s%s%s", status == PCB_DONE ? "done" : status == PCB_CANCELLED ? "cancelled" : "failed", stage && *stage ? " in " : "",
        stage ? stage : "", text && *text ? ": " : "");
    if (text && *text) say("  %s", text);
    info_changed();
}

// Before anything is downloaded or built: the room the tools still missing and the work folder need, on the disk of the
// builder's folder (beside MH4U.exe and its settings, never the system's temporary folder)
Result room_check() {
    Result r;
    int64_t need = 0;
    for (const Tool* t : TOOLS)
        if (!tool_installed(t)) need += t->size + t->unpacked;
    wchar_t work[MAX_PATH];
    in_root(work, MAX_PATH, L"work");
    int64_t have = is_dir(work) ? tree_size(work) : 0;
    need += WORK_SPACE > have ? WORK_SPACE - have : 0;
    ULARGE_INTEGER avail;
    if (!GetDiskFreeSpaceExW(g_root, &avail, nullptr, nullptr)) return r;  // (unknown: the build's own checks still stop it)
    char where[MAX_PATH * 3];
    to_utf8(g_root, where, sizeof where);
    say("space: %lld MB free in %s, %lld MB needed", (long long)(avail.QuadPart >> 20), where, (long long)(need >> 20));
    if ((int64_t)avail.QuadPart < need)
        why(&r, "%lld MB free on the disk of %s, %lld MB needed while it builds", (long long)(avail.QuadPart >> 20), where,
            (long long)(need >> 20));
    return r;
}

DWORD WINAPI build_thread(void* arg) {
    auto* a = static_cast<StartArg*>(arg);
    wchar_t p[MAX_PATH];
    mkdirs(g_root);
    wjoin(p, MAX_PATH, g_home, L"builder_log.txt");
    g_log = _wfopen(p, L"wb");
    SYSTEMTIME now;
    GetLocalTime(&now);
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof m;
    GlobalMemoryStatusEx(&m);
    say("MH4U builder (PC) %s, %04d-%02d-%02d %02d:%02d, %lu cores, %llu MB of memory, code library id %s", MH4U_APP_VERSION,
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, (unsigned long)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS),
        (unsigned long long)(m.ullTotalPhys >> 20), MH4U_PC_CODE_ID);
    say("game folder %s", a->game8);
    char root8[MAX_PATH * 3];
    to_utf8(g_root, root8, sizeof root8);
    say("builder %s", root8);
    in_root(p, MAX_PATH, L"state.txt");
    char state[1200];
    snprintf(state, sizeof state, "running\n%s", a->game8);
    write_text(p, state);
    drop_trash(nullptr);
    int jobs = 0;
    Result r = room_check();
    if (r.ok) r = install_scripts();
    for (int i = 0; i < 2 && r.ok; i++) r = install_tool(TOOLS[i]);
    if (!r.ok) {
        bool space = strstr(r.why, "disk may be full") != nullptr || strstr(r.why, "needed while it builds") != nullptr;
        if (g_cancel) finish(PCB_CANCELLED, "", "", 0);
        else finish(PCB_FAILED, space ? "space" : strstr(r.why, "builder_tools.zip") ? "tools" : "download", r.why, 0);
    } else {
        say("tools: python %s, zig %s", PYTHON.version, ZIG.version);
        set_stage(1);
        set_step("extract", 0, 0);
        jobs = jobs_for_now();
        job_set([jobs](PcbJob& j) { j.jobs = jobs; });
        RunOut o;
        int code = g_cancel ? -1 : run_build(a, jobs, &o);
        PcbJob j;
        pcb_snapshot(&j);
        if (g_cancel) finish(PCB_CANCELLED, "", "", jobs);
        else if (code == 0 && o.library[0]) finish(PCB_DONE, "", "", jobs);
        else if (o.stage[0]) finish(PCB_FAILED, o.stage, o.text, jobs);
        else finish(PCB_FAILED, pcb_stage_names[j.stage], code == -2 ? "the build couldn't start" : "the build stopped", jobs);
    }
    if (g_log) fclose(g_log), g_log = nullptr;
    delete a;
    InterlockedExchange(&g_busy, 0);
    return 0;
}

void reset_job(const StartArg* a) {
    job_set([a](PcbJob& j) {
        j = PcbJob{};
        j.status = a ? PCB_RUNNING : PCB_IDLE;
        j.started_ms = j.stage_started_ms = (int64_t)SDL_GetTicks();
        if (a) snprintf(j.folder, sizeof j.folder, "%s", a->game8);
    });
}

}  // namespace

void pcb_init() {
    if (g_ready) return;
    from_utf8(pc_paths().home.c_str(), g_home, MAX_PATH);
    wjoin(g_root, MAX_PATH, g_home, L"builder");
    from_utf8(pc_paths().exe_dir.c_str(), g_exe_dir, MAX_PATH);
    g_job.status = PCB_IDLE;
    g_ready = true;
    drop_trash_later();
}

void pcb_info(PcbInfo* out) {
    static uint64_t last_free;
    uint64_t now = SDL_GetTicks();
    if (g_ready && (now - last_free > 1000 || !last_free)) {  // (the free space once a second)
        ULARGE_INTEGER avail;
        wchar_t p[MAX_PATH];
        wcscpy(p, is_dir(g_root) ? g_root : g_home);
        if (GetDiskFreeSpaceExW(p, &avail, nullptr, nullptr)) {
            AcquireSRWLockExclusive(&g_lock);
            g_info.free = (int64_t)avail.QuadPart;
            ReleaseSRWLockExclusive(&g_lock);
        }
        last_free = now;
    }
    AcquireSRWLockShared(&g_lock);
    *out = g_info;
    // (something to start over from: a work folder, or a run cut, failed or cancelled; never while a build runs)
    if (g_busy) out->half_done = out->interrupted = false;
    else out->half_done |= out->interrupted || g_job.status == PCB_FAILED || g_job.status == PCB_CANCELLED;
    ReleaseSRWLockShared(&g_lock);
}

void pcb_snapshot(PcbJob* out) {
    AcquireSRWLockShared(&g_lock);
    *out = g_job;
    ReleaseSRWLockShared(&g_lock);
}

int pcb_tail(char (*lines)[160], int max) {
    AcquireSRWLockShared(&g_lock);
    int n = g_tail_n < max ? g_tail_n : max;
    for (int i = 0; i < n; i++) memcpy(lines[i], g_tail[(g_tail_at - n + i + TAIL) % TAIL], sizeof g_tail[0]);
    ReleaseSRWLockShared(&g_lock);
    return n;
}

bool pcb_running() { return g_busy != 0; }
const char* pcb_wanted_id() { return MH4U_PC_CODE_ID; }

namespace {
// A mod folder (a title folder in it, or exefs) with a code patch or a whole code.bin (BuilderActivity.hasCode)
bool has_code(const std::filesystem::path& dir) {
    std::error_code ec;
    for (const char* t : {"", "0004000000126300", "0004000E00126300", "0004000e00126300", "0004000000126100", "0004000E00126100"})
        for (const char* f : {"exefs/code.ips", "exefs/code.bps", "exefs/code.bin", "code.ips", "code.bps", "code.bin"})
            if (std::filesystem::is_regular_file(dir / t / f, ec)) return true;
    return false;
}
// The mods of a mods folder (one folder each) that change the game's code (BuilderActivity.codeMods)
std::string code_mods(const std::filesystem::path& dir) {
    std::vector<std::string> names;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_directory(e2)) continue;
        const std::u8string u = it->path().filename().u8string();
        const std::string n(u.begin(), u.end());
        bool title = n.size() == 16;
        for (char c : n) title = title && isxdigit(static_cast<unsigned char>(c));
        if (n.empty() || n[0] == '.' || title || !has_code(it->path())) continue;
        names.push_back(n);
    }
    std::sort(names.begin(), names.end());
    std::string out;
    for (const std::string& n : names) out += (out.empty() ? "" : ", ") + n;
    return out;
}
std::filesystem::path u8path(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }
}  // namespace

std::vector<PcbMods> pcb_mod_choices(const std::string& folder, const std::string& user) {
    std::vector<PcbMods> out;
    const std::filesystem::path from = u8path(folder) / "mods";
    std::string in_folder = code_mods(from);
    if (has_code(from)) in_folder = "0004000000126300" + std::string(in_folder.empty() ? "" : ", ") + in_folder;
    if (!in_folder.empty()) out.push_back({"--game-mods", folder + "/mods", in_folder});
    if (const std::string added = code_mods(u8path(user) / "load" / "mods"); !added.empty())
        out.push_back({"--user-mods", user, added});
    out.push_back({"", "", ""});
    return out;
}

bool pcb_start(const std::string& folder, const std::string& base, const std::string& update, const PcbMods& mods) {
    pcb_init();
    if (InterlockedCompareExchange(&g_busy, 1, 0)) return false;
    auto* a = new StartArg{};
    wchar_t w[MAX_PATH];
    bool ok = from_utf8(folder.c_str(), w, MAX_PATH) && GetFullPathNameW(w, MAX_PATH, a->game, nullptr) && is_dir(a->game) &&
              from_utf8(base.c_str(), a->base, MAX_PATH) && from_utf8(update.c_str(), a->update, MAX_PATH) &&
              from_utf8(mods.flag.c_str(), a->mods_flag, 16) && from_utf8(mods.path.c_str(), w, MAX_PATH) &&
              (mods.path.empty() || GetFullPathNameW(w, MAX_PATH, a->mods, nullptr));
    if (!ok) {
        delete a;
        InterlockedExchange(&g_busy, 0);
        return false;
    }
    size_t n = wcslen(a->game);
    while (n > 3 && (a->game[n - 1] == L'\\' || a->game[n - 1] == L'/')) a->game[--n] = 0;  // (no trailing slash: quotes)
    to_utf8(a->game, a->game8, sizeof a->game8);
    InterlockedExchange(&g_cancel, 0);
    AcquireSRWLockExclusive(&g_lock);
    g_tail_n = g_tail_at = 0;
    ReleaseSRWLockExclusive(&g_lock);
    reset_job(a);
    HANDLE h = CreateThread(nullptr, 256 << 10, build_thread, a, 0, nullptr);
    if (!h) {
        delete a;
        reset_job(nullptr);
        InterlockedExchange(&g_busy, 0);
        return false;
    }
    SetThreadPriority(h, THREAD_PRIORITY_BELOW_NORMAL);
    CloseHandle(h);
    fprintf(stderr, "[builder] started for %s, code mods: %s\n", folder.c_str(), mods.what.empty() ? "none" : mods.what.c_str());
    return true;
}

void pcb_cancel() {
    InterlockedExchange(&g_cancel, 1);
    AcquireSRWLockShared(&g_lock);  // (the handles stay open while it is held: run_build closes them after taking it)
    if (g_jobobj) TerminateJobObject(g_jobobj, 1);
    else if (g_python) TerminateProcess(g_python, 1);
    ReleaseSRWLockShared(&g_lock);
}

bool pcb_start_over(bool tools) {
    pcb_init();
    if (g_busy) return false;
    wchar_t p[MAX_PATH];
    const wchar_t* const parts[] = {L"work", L"tmp", L"python", L"zig", L"scripts", L"downloads"};
    for (int i = 0; i < (tools ? 6 : 2); i++) {
        in_root(p, MAX_PATH, parts[i]);
        set_aside(p);
    }
    in_root(p, MAX_PATH, L"state.txt");
    DeleteFileW(p);
    AcquireSRWLockExclusive(&g_lock);
    g_tail_n = g_tail_at = 0;
    ReleaseSRWLockExclusive(&g_lock);
    reset_job(nullptr);
    fprintf(stderr, "[builder] started over (%s)\n", tools ? "the tools deleted too" : "the tools kept");
    drop_trash_later();
    info_changed();
    return true;
}

bool pcb_delete_tools() {
    pcb_init();
    if (g_busy) return false;
    wchar_t trash[MAX_PATH];
    _snwprintf(trash, MAX_PATH, L"%ls\\builder.trash-%llu", g_home, (unsigned long long)GetTickCount64());
    if (exists(g_root) && !MoveFileExW(g_root, trash, 0)) tree_delete(g_root);
    reset_job(nullptr);
    fprintf(stderr, "[builder] the tools deleted\n");
    drop_trash_later();
    info_changed();
    return true;
}
