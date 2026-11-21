// MH4U for PC: the update notice, as Android's (android/.../Notices.java): at most once a day, on a thread of its own, one
// plain GET of GitHub's latest release of the repository (no other data is sent: the request names itself by the
// repository's name, the same for every PC), its tag kept in the home's notices.txt; a newer tag than this version is
// shown before the game starts, at most once a day: View opens the release page, Later goes on. F1, System, Check for
// updates ([Interface] Updates = off) turns both off. Offline, or any error: nothing is said.
//
// Tests: MH4U_TEST_LATEST=<tag> keeps that tag as GitHub's answer, without asking it; MH4U_TEST_SHOT_DIR saves the notice
// to update_notice.png (unanswered); MH4U_TEST_ANSWER=<n> answers it with button n, unshown; MH4U_TEST_NO_EXPLORER only
// logs the page View would open.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "app_settings.h"
#include "app_version.h"
#include "common/utf8.h"
#include "pc_app.h"

#include <windows.h>  // (last: its macros)
#include <shellapi.h>
#include <winhttp.h>

namespace fs = std::filesystem;

namespace {

constexpr const char* kRepo = "4u-native";
constexpr const wchar_t* kHost = L"api.github.com";
constexpr const wchar_t* kPath = L"/repos/khorne-hub/4u-native/releases/latest";
constexpr const char* kPage = "https://github.com/khorne-hub/4u-native/releases/latest";
constexpr long long kDay = 24LL * 60 * 60;

std::mutex g_lock;  // (notices.txt: the check's thread and the start's notice)
std::string g_file;

void Log(const std::string& line) { std::fprintf(stderr, "[updates] %s\n", line.c_str()); }
std::string TestEnv(const char* name) {
    char v[512];
    const DWORD n = GetEnvironmentVariableA(name, v, sizeof v);
    return n > 0 && n < sizeof v ? std::string(v, n) : std::string();
}
long long Now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// notices.txt: key=value lines (checked_at, shown_at: seconds since 1970; latest: GitHub's tag)
std::map<std::string, std::string> Read() {
    std::map<std::string, std::string> kv;
    std::ifstream in(mh4n::utf8::PathFromUtf8(g_file));
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (const size_t eq = line.find('='); eq != std::string::npos) kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return kv;
}
void Write(const std::map<std::string, std::string>& kv) {
    const fs::path to = mh4n::utf8::PathFromUtf8(g_file), tmp = mh4n::utf8::PathFromUtf8(g_file + ".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        for (const auto& [k, v] : kv) out << k << '=' << v << '\n';
        if (!out) return;
    }
    std::error_code ec;
    fs::rename(tmp, to, ec);
}
void Set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> hold(g_lock);
    auto kv = Read();
    kv[key] = value;
    Write(kv);
}
long long Number(const std::map<std::string, std::string>& kv, const char* key) {
    auto it = kv.find(key);
    return it == kv.end() ? 0 : std::atoll(it->second.c_str());
}
// (now - then < a day, and the clock didn't go back)
bool WithinADay(long long then) {
    const long long now = Now();
    return now >= then && now - then < kDay;
}

// "v1.0.2" / "1.0.2" against "1.0.1.1": the first is newer. Anything that isn't numbers and dots: not newer (Notices.java)
bool Parts(std::string v, std::vector<int>* out) {
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(v.begin());
    size_t cut = 0;
    while (cut < v.size() && ((v[cut] >= '0' && v[cut] <= '9') || v[cut] == '.')) cut++;
    v.resize(cut);
    if (v.empty()) return false;
    std::stringstream ss(v);
    for (std::string part; std::getline(ss, part, '.');) {
        if (part.empty() || part.size() > 9) return false;
        out->push_back(std::atoi(part.c_str()));
    }
    return !out->empty();
}
bool Newer(const std::string& tag, const std::string& mine) {
    std::vector<int> a, b;
    if (!Parts(tag, &a) || !Parts(mine, &b)) return false;
    for (size_t i = 0; i < std::max(a.size(), b.size()); i++) {
        const int x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;
        if (x != y) return x > y;
    }
    return false;
}

// One GET of the latest release; its tag_name, or "" (offline, a time-out, anything)
std::string AskGitHub() {
    std::string body, tag;
    HINTERNET ses = WinHttpOpen(L"4u-native", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) ses = WinHttpOpen(L"4u-native", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) return "";
    WinHttpSetTimeouts(ses, 8000, 8000, 8000, 8000);
    HINTERNET con = WinHttpConnect(ses, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", kPath, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             WINHTTP_FLAG_SECURE)
                        : nullptr;
    DWORD status = 0, size = sizeof status;
    if (req && WinHttpSendRequest(req, L"Accept: application/vnd.github+json\r\n", static_cast<DWORD>(-1L),
                                  WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr) &&
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
                            &size, WINHTTP_NO_HEADER_INDEX) &&
        status == 200) {
        char buf[8192];
        for (DWORD got = 0; WinHttpReadData(req, buf, sizeof buf, &got) && got > 0 && body.size() < (256u << 10);)
            body.append(buf, got);
        std::smatch m;
        static const std::regex kTag("\"tag_name\"\\s*:\\s*\"([^\"]+)\"");
        if (std::regex_search(body, m, kTag)) tag = m[1].str();
    } else {
        Log("no answer (" + std::to_string(status ? status : GetLastError()) + ")");
    }
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return tag;
}

std::string Shown(std::string tag) {
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) tag.erase(tag.begin());
    return tag;
}

}  // namespace

void pc_updates_check(const std::string& home) {
    g_file = home + "/notices.txt";
    if (!app_setting_bool("Interface", "Updates", true)) return;
    {
        std::lock_guard<std::mutex> hold(g_lock);
        auto kv = Read();
        if (TestEnv("MH4U_TEST_LATEST").empty() && WithinADay(Number(kv, "checked_at"))) return;
        kv["checked_at"] = std::to_string(Now());
        Write(kv);
    }
    std::thread([] {
        std::string tag = TestEnv("MH4U_TEST_LATEST");
        if (!tag.empty()) Log("MH4U_TEST_LATEST: " + tag + " (GitHub not asked)");
        else tag = AskGitHub();
        if (tag.empty()) return;
        Set("latest", tag);
        Log("the latest release is " + tag);
    }).detach();
}

void pc_update_notice(bool es) {
    if (g_file.empty() || !app_setting_bool("Interface", "Updates", true)) return;
    std::string latest;
    {
        std::lock_guard<std::mutex> hold(g_lock);
        auto kv = Read();
        latest = kv.count("latest") ? kv["latest"] : "";
        if (latest.empty() || !Newer(latest, MH4U_APP_VERSION) || WithinADay(Number(kv, "shown_at"))) return;
        kv["shown_at"] = std::to_string(Now());
        Write(kv);
    }
    const std::string v = Shown(latest), mine = MH4U_APP_VERSION;
    const std::string title = es ? "HAY UNA VERSIÓN NUEVA" : "A NEW VERSION IS OUT";
    const std::string text =
        es ? "La versión " + v + " ya está disponible (tienes la " + mine + "). Para actualizar, cierra el juego, descomprime "
                 "el zip nuevo y copia sus archivos sobre los de esta carpeta: tu partida y tus ajustes se quedan como están."
           : "Version " + v + " is out (you have " + mine + "). To update, close the game, unzip the new zip and copy its files "
                 "over the ones in this folder: your save and your settings stay as they are.";
    Log("notice: " + latest + " is newer than " + mine);
    int answer = -1;
    if (const std::string dir = TestEnv("MH4U_TEST_SHOT_DIR"); !dir.empty()) {
        const std::string shot = dir + "/update_notice.png";
        pc_message_screen(title, text, {}, {es ? "Ver" : "View", es ? "Más tarde" : "Later"}, es, shot.c_str());
    } else if (const std::string forced = TestEnv("MH4U_TEST_ANSWER"); !forced.empty()) {
        answer = std::atoi(forced.c_str());
        Log("answered by MH4U_TEST_ANSWER: " + forced);
    } else {
        answer = pc_message_screen(title, text, {}, {es ? "Ver" : "View", es ? "Más tarde" : "Later"}, es);
    }
    if (answer != 0) return;  // (Later, or the window closed: on to the game)
    Log(std::string("View: ") + kPage);
    if (TestEnv("MH4U_TEST_NO_EXPLORER").empty())
        ShellExecuteW(nullptr, L"open", mh4n::utf8::ToWide(kPage).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
