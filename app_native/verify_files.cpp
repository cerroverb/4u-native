// Verify game files (verify_files.h): the checks, on a thread of their own. The containers are read as Dump.java and
// mh4n's dump_source.cpp read them (a decrypted .3ds/.cci, .cxi or .cia); the hash tree as the 3DS checks its own.
// VERIFY_FILES_TEST (the PC test driver) leaves out the parts that need the running app (the mods, the library the
// game runs).
#include "verify_files.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#ifdef _WIN32
#include <io.h>
#else
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#if defined(__aarch64__)
#include <arm_neon.h>
#if defined(__linux__) || defined(__ANDROID__)
#include <sys/auxv.h>
#endif
#endif

#ifndef VERIFY_FILES_TEST
#include "content.h"
#include "mods/mod_list.h"
#endif

namespace {

// ---------------------------------------------------------------- hashes
constexpr uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t v, int n) { return (v >> n) | (v << (32 - n)); }
inline uint32_t rotl(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

void sha256_blocks_c(uint32_t st[8], const uint8_t* p, size_t blocks) {
    for (; blocks--; p += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = st[0], b = st[1], c = st[2], d = st[3], e = st[4], f = st[5], g = st[6], h = st[7];
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
            uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
        }
        st[0] += a, st[1] += b, st[2] += c, st[3] += d, st[4] += e, st[5] += f, st[6] += g, st[7] += h;
    }
}

#if defined(__aarch64__)
// The ARMv8 SHA-256 instructions (most phones have them: several times faster than the plain code). Checked against a
// known hash before it is used (sha256_init)
__attribute__((target("sha2"))) void sha256_blocks_arm(uint32_t st[8], const uint8_t* p, size_t blocks) {
    uint32x4_t s0 = vld1q_u32(st), s1 = vld1q_u32(st + 4);
    for (; blocks--; p += 64) {
        const uint32x4_t save0 = s0, save1 = s1;
        uint32x4_t m[4];
        for (int i = 0; i < 4; i++) m[i] = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p + i * 16)));
        for (int i = 0; i < 16; i++) {
            const uint32x4_t wk = vaddq_u32(m[i & 3], vld1q_u32(K256 + i * 4));
            const uint32x4_t abcd = s0;
            s0 = vsha256hq_u32(s0, s1, wk);
            s1 = vsha256h2q_u32(s1, abcd, wk);
            if (i < 12) m[i & 3] = vsha256su1q_u32(vsha256su0q_u32(m[i & 3], m[(i + 1) & 3]), m[(i + 2) & 3], m[(i + 3) & 3]);
        }
        s0 = vaddq_u32(s0, save0), s1 = vaddq_u32(s1, save1);
    }
    vst1q_u32(st, s0), vst1q_u32(st + 4, s1);
}
#endif

void (*g_sha256_blocks)(uint32_t*, const uint8_t*, size_t) = sha256_blocks_c;

struct Sha256 {
    uint32_t st[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t buf[64];
    size_t n = 0;
    uint64_t len = 0;
    void add(const void* data, size_t size) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        len += size;
        if (n) {
            size_t k = std::min(size, 64 - n);
            memcpy(buf + n, p, k), n += k, p += k, size -= k;
            if (n == 64) g_sha256_blocks(st, buf, 1), n = 0;
        }
        if (size >= 64) g_sha256_blocks(st, p, size / 64), p += size / 64 * 64, size %= 64;
        if (size) memcpy(buf, p, size), n = size;
    }
    void end(uint8_t out[32]) {
        uint64_t bits = len * 8;
        uint8_t pad = 0x80, zero = 0;
        add(&pad, 1);
        while (n != 56) add(&zero, 1);
        uint8_t l[8];
        for (int i = 0; i < 8; i++) l[i] = (uint8_t)(bits >> (56 - i * 8));
        add(l, 8);
        for (int i = 0; i < 8; i++) out[i * 4] = st[i] >> 24, out[i * 4 + 1] = st[i] >> 16, out[i * 4 + 2] = st[i] >> 8, out[i * 4 + 3] = st[i];
    }
};
void sha256(const void* p, size_t n, uint8_t out[32]) {
    Sha256 h;
    h.add(p, n);
    h.end(out);
}
// A 4 KB block of the hash tree (or any whole multiple of 64 bytes): straight through the blocks, no copies
void sha256_padded(const uint8_t* p, size_t n, size_t block, uint8_t out[32]) {
    static const uint8_t zeros[64] = {};
    Sha256 h;
    h.add(p, n);
    for (size_t left = block - n; left;) {
        size_t k = std::min<size_t>(left, 64);
        h.add(zeros, k), left -= k;
    }
    h.end(out);
}

void sha256_init() {
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
#ifndef HWCAP_SHA2
#define HWCAP_SHA2 (1 << 6)
#endif
    if (!(getauxval(AT_HWCAP) & HWCAP_SHA2)) return;
    // (the same hashes from both: a short text and a long one, every round and the schedule)
    uint8_t big[4096], want[2][32], got[2][32];
    for (int i = 0; i < 4096; i++) big[i] = (uint8_t)(i * 7 + 3);
    g_sha256_blocks = sha256_blocks_c;
    sha256("abc", 3, want[0]), sha256(big, sizeof big, want[1]);
    g_sha256_blocks = sha256_blocks_arm;
    sha256("abc", 3, got[0]), sha256(big, sizeof big, got[1]);
    if (memcmp(got, want, sizeof got)) {
        g_sha256_blocks = sha256_blocks_c;
        fprintf(stderr, "[verify] the CPU's SHA-256 instructions gave another hash: the plain code is used\n");
    }
#endif
}

std::string sha1_hex(const uint8_t* p, size_t n) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::vector<uint8_t> m(p, p + n);
    uint64_t bits = (uint64_t)n * 8;
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 7; i >= 0; i--) m.push_back((uint8_t)(bits >> (i * 8)));
    for (size_t o = 0; o < m.size(); o += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) w[i] = (uint32_t)m[o + i * 4] << 24 | (uint32_t)m[o + i * 4 + 1] << 16 | (uint32_t)m[o + i * 4 + 2] << 8 | m[o + i * 4 + 3];
        for (int i = 16; i < 80; i++) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) f = (b & c) | (~b & d), k = 0x5A827999;
            else if (i < 40) f = b ^ c ^ d, k = 0x6ED9EBA1;
            else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
            else f = b ^ c ^ d, k = 0xCA62C1D6;
            uint32_t t = rotl(a, 5) + f + e + k + w[i];
            e = d, d = c, c = rotl(b, 30), b = a, a = t;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
    }
    char out[41];
    for (int i = 0; i < 5; i++) snprintf(out + i * 8, 9, "%08x", h[i]);
    return out;
}

std::string hex(const uint8_t* p, size_t n) {
    std::string s;
    char b[3];
    for (size_t i = 0; i < n; i++) snprintf(b, sizeof b, "%02x", p[i]), s += b;
    return s;
}

// ---------------------------------------------------------------- files
struct File {
    int fd = -1;
    uint64_t size = 0;
    std::string path;
    bool open(const std::string& p) {
        close();
        path = p;
#ifdef _WIN32
        fd = ::_open(p.c_str(), _O_RDONLY | _O_BINARY);
        if (fd < 0) return false;
        size = (uint64_t)_lseeki64(fd, 0, SEEK_END);
#else
        fd = ::open(p.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) return false;
        struct stat st{};
        size = fstat(fd, &st) == 0 ? (uint64_t)st.st_size : 0;
#endif
        return true;
    }
    // n bytes at off (fewer past the end); -1: a read error
    int64_t read(uint64_t off, void* dst, size_t n) const {
        if (fd < 0) return -1;
        uint8_t* d = static_cast<uint8_t*>(dst);
        size_t got = 0;
#ifdef _WIN32
        if (_lseeki64(fd, (__int64)off, SEEK_SET) < 0) return -1;
        while (got < n) {
            int r = ::_read(fd, d + got, (unsigned)std::min<size_t>(n - got, 1 << 24));
            if (r < 0) return -1;
            if (r == 0) break;
            got += (size_t)r;
        }
#else
        while (got < n) {
            ssize_t r = ::pread(fd, d + got, n - got, (off_t)(off + got));
            if (r < 0) return -1;
            if (r == 0) break;
            got += (size_t)r;
        }
#endif
        return (int64_t)got;
    }
    bool read_exact(uint64_t off, void* dst, size_t n) const { return read(off, dst, n) == (int64_t)n; }
    // (Android) what was read won't be read again: out of the page cache, so the game's own files stay in it
    void forget(uint64_t off, uint64_t n) const {
#if defined(__ANDROID__) || defined(__linux__)
        if (fd >= 0) posix_fadvise(fd, (off_t)off, (off_t)n, POSIX_FADV_DONTNEED);
#else
        (void)off, (void)n;
#endif
    }
    void close() {
        if (fd >= 0) {
#ifdef _WIN32
            ::_close(fd);
#else
            ::close(fd);
#endif
        }
        fd = -1;
    }
    ~File() { close(); }
};

bool exists(const std::string& p) {
    struct stat st{};
    return stat(p.c_str(), &st) == 0;
}
bool is_dir(const std::string& p) {
    struct stat st{};
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
uint64_t file_size(const std::string& p) {
    struct stat st{};
    return stat(p.c_str(), &st) == 0 ? (uint64_t)st.st_size : 0;
}
std::string first_line(const std::string& p) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return "";
    char b[4096];
    std::string s = fgets(b, sizeof b, f) ? b : "";
    fclose(f);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}
std::string base_name(const std::string& p) {
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}
std::string lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}
bool ends_with(const std::string& s, const char* e) {
    size_t n = strlen(e);
    return s.size() >= n && !s.compare(s.size() - n, n, e);
}
uint64_t le(const uint8_t* b, int n) {
    uint64_t v = 0;
    for (int i = n - 1; i >= 0; i--) v = v << 8 | b[i];
    return v;
}
uint64_t be(const uint8_t* b, int n) {
    uint64_t v = 0;
    for (int i = 0; i < n; i++) v = v << 8 | b[i];
    return v;
}
uint64_t align(uint64_t x, uint64_t a) { return (x + a - 1) / a * a; }

std::string F(const char* format, ...) {
    char b[1024];
    va_list a;
    va_start(a, format);
    vsnprintf(b, sizeof b, format, a);
    va_end(a);
    return b;
}
// Megabytes and gigabytes, one decimal (Spanish: a comma)
std::string decimal(double v, bool es) {
    std::string s = F("%.1f", v);
    if (es)
        for (char& c : s)
            if (c == '.') c = ',';
    return s;
}

// ---------------------------------------------------------------- the report
struct Report {
    std::vector<VerifyLine> first, problems, oks, infos;  // (first: a cancelled check's note, under the verdict)
    std::vector<VerifyLine> todos;
    std::vector<std::string> log;  // (every file of each finding, for the log)
    int count = 0;
    std::vector<VerifyLine>* last = nullptr;
    void problem(const std::string& en, const std::string& es) {
        problems.push_back({VerifyLine::Problem, en, es}), last = &problems, count++;
        log.push_back("problem: " + en);
    }
    void ok(const std::string& en, const std::string& es) {
        oks.push_back({VerifyLine::Ok, en, es}), last = &oks;
        log.push_back("ok: " + en);
    }
    void ok_first(size_t at, const std::string& en, const std::string& es) {  // (the game's and the update's: first)
        oks.insert(oks.begin() + std::min(at, oks.size()), {VerifyLine::Ok, en, es}), last = nullptr;
        log.push_back("ok: " + en);
    }
    void info(const std::string& en, const std::string& es) {
        infos.push_back({VerifyLine::Info, en, es}), last = &infos;
        log.push_back("note: " + en);
    }
    // examples under the last finding: at most 5, then "and N more"; all of them in the log
    void files(const std::vector<std::string>& names) {
        if (!last) return;
        for (size_t i = 0; i < names.size(); i++) {
            if (i < 5) last->push_back({VerifyLine::File, "· " + names[i], "· " + names[i]});
            if (i < 2000) log.push_back("  " + names[i]);
        }
        if (names.size() > 5) {
            int more = (int)names.size() - 5;
            last->push_back({VerifyLine::File, F("· and %d more", more), F("· y %d más", more)});
        }
    }
    void todo(const std::string& en, const std::string& es) {
        for (const auto& t : todos)
            if (t.en == en) return;
        todos.push_back({VerifyLine::Todo, en, es});
        log.push_back("to do: " + en);
    }
    std::vector<VerifyLine> lines() const {
        std::vector<VerifyLine> out = first;
        out.insert(out.end(), problems.begin(), problems.end());
        out.insert(out.end(), oks.begin(), oks.end());
        out.insert(out.end(), infos.begin(), infos.end());
        out.insert(out.end(), todos.begin(), todos.end());
        return out;
    }
};

// The advice every port shares (VERIFY_SPEC.md)
const char* const NEED_EN = "the game and its 1.3.0 update, USA, decrypted, from your own copy.";
const char* const NEED_ES = "el juego y su actualización 1.3.0, de América, descifrados, de tu propia copia.";
void todo_copy_again(Report& r) {
    r.todo(F("Make your copy again from your own game, as the README's steps say: %s", NEED_EN),
           F("Vuelve a hacer tu copia desde tu propio juego, como dicen los pasos del README: %s", NEED_ES));
}
void todo_choose_again(Report& r) {
    r.todo("Choose the game folder again: System › Your game › Game folder.",
           "Vuelve a elegir la carpeta del juego: Sistema › Tu partida › Carpeta del juego.");
}
void todo_library(Report& r) {
    r.todo("Make the code library again, with this version: System › Game code › Build the game code, or the guide's tool on a PC.",
           "Vuelve a crear la librería de código, con esta versión: Sistema › Código del juego › Crear el código del juego, o la herramienta de la guía en una PC.");
}
[[maybe_unused]] void todo_mods(Report& r) {
    r.todo("If the game fails, turn the mods off (Mods & Cheats › Mods) and try again.",
           "Si el juego falla, desactiva los mods (Mods y trucos › Mods) y vuelve a probar.");
}

// ---------------------------------------------------------------- the versions (mh4n config/versions.json)
constexpr uint32_t USA = 0x00126300, EUR = 0x00126100, MH4G = 0x0011D700;
struct Version {
    const char* key;
    uint32_t title;     // the low word of the program id
    bool update;        // the code of an update (else a base game's)
    int title_version;  // the CIA's
    const char* code_sha1;
    bool runs;
    const char *en, *es;
};
const Version VERSIONS[] = {
    {"usa_v130", USA, true, 1072, "c9225f87ca3f65e8537484d029b08b8b04c3717d", true, "the USA 1.3.0 update", "la actualización 1.3.0 de América"},
    {"eur_v110", EUR, true, 1040, "0a927cb1a59d90c8c806fed091b3755cd518b3f8", true, "the European 1.1.0 update", "la actualización 1.1.0 de Europa"},
    {"usa_v010", USA, false, 16, "6fac6b17f64e453a1c70b5727787fd1618119917", false, "USA", "América"},
    {"eur_v100", EUR, false, 0, "af19fba3a7e5150b55edfd2e2675dc24bfcabc06", false, "Europe", "Europa"},
};
const Version* by_sha1(const std::string& sha1) {
    for (const Version& v : VERSIONS)
        if (sha1 == v.code_sha1) return &v;
    return nullptr;
}
// The original dumps' superblocks (the USA game and its 1.3.0 update, from the project's own copies): the whole RomFS and
// ExeFS hang from them, so a copy whose tree checks but whose superblock is another was rebuilt
struct Known {
    uint64_t program;
    const char *exefs, *romfs;
};
const Known KNOWN[] = {
    {0x0004000000126300ull, "f973747da74aa5af98496ca0a1dea95fe66064c4fb5997f5f44aab3298f1df8c",
     "149dd01369958de31553d7a7cc9346d69ddce73a149e3d7ea7124e2eae5070b9"},
    {0x0004000E00126300ull, "12441523058b231b6dcd7d79945966f8f11df068319bf8b836ce31163d32cb06",
     "64e2aa727fda340e50e075f65e02bc378f2e3ef20b31fed1459bc6a3d3ba4f02"},
};
const char* region_en(uint32_t t) { return t == USA ? "USA" : t == EUR ? "Europe" : t == MH4G ? "Japan" : "?"; }
const char* region_es(uint32_t t) { return t == USA ? "América" : t == EUR ? "Europa" : t == MH4G ? "Japón" : "?"; }
std::string version_text(int v) { return F("%d.%d.%d", v >> 10, (v >> 4) & 0x3F, v & 0xF); }

// ---------------------------------------------------------------- the containers
struct Ncch {
    uint8_t h[0x200];
    uint64_t base = 0, program = 0, unit = 0x200, content = 0;
    uint64_t exefs_off = 0, exefs_size = 0, exefs_hash = 0, romfs_off = 0, romfs_size = 0, romfs_hash = 0;
    bool plain = false, cia = false;
    uint64_t cia_title = 0;
    int cia_version = -1;
    std::string err_en, err_es;  // (not a 3DS file, encrypted...)
};

bool ncch_at(const File& f, uint64_t base, Ncch& n) {
    if (!f.read_exact(base, n.h, 0x200) || memcmp(n.h + 0x100, "NCCH", 4)) {
        n.err_en = "it is not a 3DS game", n.err_es = "no es un juego de 3DS";
        return false;
    }
    n.base = base;
    n.program = le(n.h + 0x118, 8);
    n.unit = 0x200ull << n.h[0x188 + 6];
    n.content = le(n.h + 0x104, 4) * n.unit;
    n.exefs_off = le(n.h + 0x1A0, 4) * n.unit, n.exefs_size = le(n.h + 0x1A4, 4) * n.unit, n.exefs_hash = le(n.h + 0x1A8, 4) * n.unit;
    n.romfs_off = le(n.h + 0x1B0, 4) * n.unit, n.romfs_size = le(n.h + 0x1B4, 4) * n.unit, n.romfs_hash = le(n.h + 0x1B8, 4) * n.unit;
    n.plain = (n.h[0x188 + 7] & 0x04) != 0;
    return true;
}

// The game's NCCH in a file: an NCSD's partition 0, a CIA's content 0, or an NCCH at 0 (Dump.find)
bool find_ncch(const File& f, Ncch& n) {
    uint8_t head[0x200];
    if (!f.read_exact(0, head, sizeof head)) {
        n.err_en = "it can't be read", n.err_es = "no se puede leer";
        return false;
    }
    if (!memcmp(head + 0x100, "NCSD", 4)) {
        if (!le(head + 0x124, 4)) {
            n.err_en = "the game's partition is empty", n.err_es = "la partición del juego está vacía";
            return false;
        }
        return ncch_at(f, le(head + 0x120, 4) * 0x200, n);
    }
    if (!memcmp(head + 0x100, "NCCH", 4)) return ncch_at(f, 0, n);
    if (le(head, 4) == 0x2020) {
        uint64_t cert = align(le(head, 4), 64), tik = align(cert + le(head + 8, 4), 64), tmd_at = align(tik + le(head + 12, 4), 64);
        uint64_t tmd_size = le(head + 16, 4), content = align(tmd_at + tmd_size, 64);
        if (tmd_size > (1 << 20)) {
            n.err_en = "this .cia is not one the app knows", n.err_es = "este .cia no es uno que la app conozca";
            return false;
        }
        std::vector<uint8_t> tmd(tmd_size);
        if (!f.read_exact(tmd_at, tmd.data(), tmd.size())) {
            n.err_en = "the file ends too early (a copy cut short?)", n.err_es = "el archivo termina antes de tiempo (¿una copia cortada?)";
            return false;
        }
        int sig_len;
        switch ((uint32_t)be(tmd.data(), 4)) {
        case 0x10000: case 0x10003: sig_len = 0x200 + 0x3C; break;
        case 0x10001: case 0x10004: sig_len = 0x100 + 0x3C; break;
        case 0x10002: case 0x10005: sig_len = 0x3C + 0x40; break;
        default:
            n.err_en = "this .cia is not one the app knows", n.err_es = "este .cia no es uno que la app conozca";
            return false;
        }
        size_t th = 4 + sig_len, chunks = th + 0xC4 + 0x24 * 64;
        if (chunks > tmd.size()) {
            n.err_en = "this .cia is not one the app knows", n.err_es = "este .cia no es uno que la app conozca";
            return false;
        }
        uint64_t title = be(&tmd[th + 0x4C], 8);
        int version = (int)be(&tmd[th + 0x9C], 2), count = (int)be(&tmd[th + 0x9E], 2);
        uint64_t pos = content;
        for (int i = 0; i < count && chunks + i * 0x30 + 0x10 <= tmd.size(); i++) {
            size_t c = chunks + i * 0x30;
            int index = (int)be(&tmd[c + 4], 2), type = (int)be(&tmd[c + 6], 2);
            uint64_t size = be(&tmd[c + 8], 8);
            if (index == 0) {
                if (type & 1) {
                    n.err_en = "the .cia is encrypted: it has to be a decrypted copy of your own";
                    n.err_es = "el .cia está cifrado: tiene que ser una copia descifrada, la tuya";
                    return false;
                }
                if (!ncch_at(f, pos, n)) return false;
                n.cia = true, n.cia_title = title, n.cia_version = version;
                return true;
            }
            pos += size;
        }
        n.err_en = "the .cia has no game content", n.err_es = "el .cia no tiene contenido del juego";
        return false;
    }
    n.err_en = "it is not a 3DS game (.3ds, .cci, .cxi or .cia)", n.err_es = "no es un juego de 3DS (.3ds, .cci, .cxi o .cia)";
    return false;
}

// Backward LZ77, as the 3DS compresses its ExeFS .code (Dump.blz, mh4n tools/blz.py)
bool blz(std::vector<uint8_t>& data) {
    size_t size = data.size();
    if (size < 8) return false;
    uint32_t top_bottom = (uint32_t)le(&data[size - 8], 4), extra = (uint32_t)le(&data[size - 4], 4);
    size_t top = top_bottom & 0xFFFFFF, bottom = top_bottom >> 24;
    if (bottom < 8 || bottom > 11 || top < bottom || top > size) return false;
    size_t out_size = size + extra;
    std::vector<uint8_t> out(out_size);
    memcpy(out.data(), data.data(), size);
    size_t src = size - bottom, dst = out_size, end = size - top;
    while (src > end) {
        int flags = data[--src];
        for (int i = 0; i < 8; i++) {
            if (!(flags & 0x80)) {
                if (src - end < 1 || dst - end < 1) return false;
                out[--dst] = data[--src];
            } else {
                if (src - end < 2) return false;
                src -= 2;
                int hi = data[src + 1], lo = data[src];
                size_t length = (hi >> 4) + 3, disp = (((hi & 0x0F) << 8) | lo) + 3;
                if (dst - end < length || dst + disp > out_size) return false;
                for (size_t k = 0; k < length; k++) --dst, out[dst] = out[dst + disp];
            }
            flags = (flags << 1) & 0xFF;
            if (src <= end) break;
        }
    }
    data.swap(out);
    return true;
}

// The id a code library carries inside itself (rc_library_id, mh4n tools/game_version.py): read from its ELF symbol
// table without loading it (GameFolderActivity.idInside); "" for one made before the tools wrote it
std::string id_inside(const std::string& path) {
    File f;
    if (!f.open(path)) return "";
    uint8_t h[64];
    if (!f.read_exact(0, h, 64) || memcmp(h, "\x7f" "ELF", 4) || h[4] != 2 || h[5] != 1) return "";
    uint64_t shoff = le(h + 0x28, 8);
    unsigned entsize = (unsigned)le(h + 0x3A, 2), num = (unsigned)le(h + 0x3C, 2);
    if (!shoff || entsize < 0x40 || !num || num > 4096) return "";
    std::vector<uint8_t> sh((size_t)entsize * num);
    if (!f.read_exact(shoff, sh.data(), sh.size())) return "";
    for (unsigned i = 0; i < num; i++) {
        const uint8_t* s = &sh[(size_t)i * entsize];
        if (le(s + 4, 4) != 11) continue;  // SHT_DYNSYM
        uint64_t sym_off = le(s + 0x18, 8), sym_size = le(s + 0x20, 8);
        unsigned link = (unsigned)le(s + 0x28, 4);
        if (link >= num || sym_size > (8u << 20)) return "";
        const uint8_t* l = &sh[(size_t)link * entsize];
        uint64_t str_off = le(l + 0x18, 8), str_size = le(l + 0x20, 8);
        if (str_size > (8u << 20)) return "";
        std::vector<uint8_t> syms(sym_size), strs(str_size);
        if (!f.read_exact(sym_off, syms.data(), syms.size()) || !f.read_exact(str_off, strs.data(), strs.size())) return "";
        static const char want[] = "rc_library_id";
        for (size_t k = 0; k + 24 <= syms.size(); k += 24) {
            uint64_t name = le(&syms[k], 4), shndx = le(&syms[k + 6], 2);
            if (!name || name + sizeof want > strs.size() || memcmp(&strs[name], want, sizeof want)) continue;
            if (!shndx || shndx >= num) continue;
            const uint8_t* ss = &sh[(size_t)shndx * entsize];
            uint64_t value = le(&syms[k + 8], 8), addr = le(ss + 0x10, 8), off = le(ss + 0x18, 8);
            char id[65] = {};
            int64_t got = f.read(off + (value - addr), id, 64);
            if (got <= 0) return "";
            id[got] = 0;
            std::string s = id;
            while (!s.empty() && (s.back() == ' ' || s.back() == '\n')) s.pop_back();
            return s;
        }
        return "";
    }
    return "";
}

// ---------------------------------------------------------------- the state
// (on the heap, never freed: a check still running when the app exits keeps them, and no global thread is left joinable)
struct Shared {
    std::mutex lock;
    VerifyState state;
    VerifyPaths paths;
    std::chrono::steady_clock::time_point reading_at;  // (Reading: when it began)
};
Shared& G() {
    static Shared* g = new Shared;
    return *g;
}
std::atomic<bool> g_cancel{false}, g_busy{false};

// One of the two files (the game, the update): what the quick pass found and what the reading needs
struct Part {
    std::string path, name;
    bool update = false;
    File f;
    Ncch n;
    bool usable = false;      // a decrypted NCCH: its trees can be read
    bool cut = false;         // shorter than its NCCH
    bool damaged = false;     // a hash that doesn't match
    bool tree_read = false;   // its RomFS read whole
    bool rebuilt = false;     // its tree checks out but isn't the original's (USA)
    bool known = false;       // its superblocks are the original's
    std::string code_sha1;    // its code's, decompressed
    std::vector<uint8_t> code, exheader;
    // the RomFS's hash tree
    uint64_t lvl_off[3] = {}, lvl_size[3] = {}, lvl_block[3] = {};
    std::vector<uint8_t> master, level1, level2;
    std::vector<uint64_t> bad3;  // bad blocks of level 3 (offsets in it)
    int bad12 = 0;               // bad blocks of levels 1 and 2
};

std::string mb(uint64_t bytes, bool es) { return decimal(bytes / 1048576.0, es); }

// The quick pass of one file: its container, its exheader, its ExeFS and its code
void quick(Part& p, Report& r) {
    if (!p.f.open(p.path)) {
        r.problem(F("%s can't be read.", p.name.c_str()), F("No se puede leer %s.", p.name.c_str()));
        todo_choose_again(r);
        return;
    }
    if (!find_ncch(p.f, p.n)) {
        r.problem(F("%s: %s.", p.name.c_str(), p.n.err_en.c_str()), F("%s: %s.", p.name.c_str(), p.n.err_es.c_str()));
        todo_copy_again(r);
        return;
    }
    if (!p.n.plain) {
        r.problem(F("%s is encrypted: it has to be a decrypted copy of your own.", p.name.c_str()),
                  F("%s está cifrado: tiene que ser una copia descifrada, la tuya.", p.name.c_str()));
        todo_copy_again(r);
        return;
    }
    const uint64_t end = p.n.base + p.n.content;
    if (p.f.size < end) {
        p.cut = true;
        r.problem(F("%s is cut short: %s of %s MB. It was not copied whole.", p.name.c_str(), mb(p.f.size, false).c_str(), mb(end, false).c_str()),
                  F("%s está cortado: %s de %s MB. No se copió entero.", p.name.c_str(), mb(p.f.size, true).c_str(), mb(end, true).c_str()));
        r.todo(F("Copy %s to the device again: the copy was cut short.", p.name.c_str()),
               F("Vuelve a copiar %s al dispositivo: la copia quedó cortada.", p.name.c_str()));
    }
    p.usable = true;
    // the exheader: its hash in the NCCH's header
    p.exheader.resize(0x800);
    uint8_t got[32];
    if (!p.f.read_exact(p.n.base + 0x200, p.exheader.data(), 0x800)) {
        p.damaged = true;
        return;
    }
    sha256(p.exheader.data(), 0x400, got);
    if (memcmp(got, p.n.h + 0x160, 32)) {
        p.damaged = true;
        r.problem(F("%s: its exheader doesn't match its hash: the file is damaged or was edited.", p.name.c_str()),
                  F("%s: su exheader no coincide con su hash: el archivo está dañado o fue editado.", p.name.c_str()));
        todo_copy_again(r);
    }
    // the ExeFS: its superblock (the header) and each section
    if (p.n.exefs_size < 0x200 || p.n.exefs_hash < 0x200 || p.n.exefs_hash > p.n.exefs_size) {
        p.damaged = true;
        r.problem(F("%s has no ExeFS: it is not a whole game.", p.name.c_str()), F("%s no tiene ExeFS: no es un juego entero.", p.name.c_str()));
        return;
    }
    std::vector<uint8_t> eh(p.n.exefs_hash);
    const uint64_t exefs = p.n.base + p.n.exefs_off;
    if (!p.f.read_exact(exefs, eh.data(), eh.size())) return;  // (cut short: told above)
    sha256(eh.data(), eh.size(), got);
    bool exefs_ok = !memcmp(got, p.n.h + 0x1C0, 32);
    std::vector<std::string> bad_sections;
    for (int i = 0; i < 10; i++) {
        const uint8_t* e = &eh[i * 16];
        std::string name((const char*)e, strnlen((const char*)e, 8));
        uint64_t off = le(e + 8, 4), size = le(e + 12, 4);
        if (!size) continue;
        if (0x200 + off + size > p.n.exefs_size) {
            bad_sections.push_back(name);
            continue;
        }
        std::vector<uint8_t> data(size);
        if (!p.f.read_exact(exefs + 0x200 + off, data.data(), size)) {
            bad_sections.push_back(name);
            continue;
        }
        sha256(data.data(), size, got);
        if (memcmp(got, &eh[0x200 - (i + 1) * 32], 32)) bad_sections.push_back(name);
        if (name == ".code") p.code.swap(data);
    }
    if ((!exefs_ok || !bad_sections.empty()) && !p.cut) {
        p.damaged = true;
        r.problem(F("%s: its ExeFS (the game's code, icon and banner) doesn't match its hashes: the file is damaged or was edited.", p.name.c_str()),
                  F("%s: su ExeFS (el código, el ícono y el banner del juego) no coincide con sus hashes: el archivo está dañado o fue editado.", p.name.c_str()));
        todo_copy_again(r);
        r.files(bad_sections);
    }
    if (!p.code.empty() && (p.exheader[0x0D] & 1) && !blz(p.code)) {
        p.damaged = true;
        r.problem(F("%s: its code is damaged (it can't be unpacked).", p.name.c_str()),
                  F("%s: su código está dañado (no se puede descomprimir).", p.name.c_str()));
        p.code.clear();
    }
    if (!p.code.empty()) p.code_sha1 = sha1_hex(p.code.data(), p.code.size());
    // the RomFS's own header: its superblock, the hash tree's levels
    if (p.n.romfs_size < 0x1000 || p.n.romfs_hash < 0x60 || p.n.romfs_hash > 0x100000) {
        p.damaged = true;
        r.problem(F("%s has no RomFS: it is not a whole game.", p.name.c_str()), F("%s no tiene RomFS: no es un juego entero.", p.name.c_str()));
        return;
    }
    const uint64_t romfs = p.n.base + p.n.romfs_off;
    std::vector<uint8_t> rh(p.n.romfs_hash);
    if (!p.f.read_exact(romfs, rh.data(), rh.size())) return;
    sha256(rh.data(), rh.size(), got);
    bool romfs_super = !memcmp(got, p.n.h + 0x1E0, 32) && !memcmp(rh.data(), "IVFC", 4);
    uint64_t master = romfs_super ? le(&rh[8], 4) : 0;
    if (romfs_super && 0x60 + master <= rh.size()) {
        for (int i = 0; i < 3; i++) {
            p.lvl_size[i] = le(&rh[0x0C + i * 0x18 + 8], 8);
            unsigned lg = (unsigned)le(&rh[0x0C + i * 0x18 + 16], 4);
            if (lg < 6 || lg > 20) romfs_super = false;
            else p.lvl_block[i] = 1ull << lg;
        }
        if (romfs_super) {
            p.lvl_off[2] = align(0x60 + master, p.lvl_block[2]);
            p.lvl_off[0] = align(p.lvl_off[2] + p.lvl_size[2], p.lvl_block[0]);
            p.lvl_off[1] = align(p.lvl_off[0] + p.lvl_size[0], p.lvl_block[1]);
            romfs_super = p.lvl_off[1] + p.lvl_size[1] <= p.n.romfs_size && p.lvl_size[0] <= (64u << 20) &&
                          p.lvl_size[1] <= (512u << 20) && master == (p.lvl_size[0] + p.lvl_block[0] - 1) / p.lvl_block[0] * 32;
            p.master.assign(rh.begin() + 0x60, rh.begin() + 0x60 + master);
        }
    } else {
        romfs_super = false;
    }
    if (!romfs_super && !p.cut) {
        p.damaged = true;
        r.problem(F("%s: its RomFS (the game's files) doesn't match its hash: the file is damaged or was edited.", p.name.c_str()),
                  F("%s: su RomFS (los archivos del juego) no coincide con su hash: el archivo está dañado o fue editado.", p.name.c_str()));
        todo_copy_again(r);
        p.lvl_size[0] = p.lvl_size[1] = p.lvl_size[2] = 0;
    }
    // the original's superblocks (the USA game and update)
    for (const Known& k : KNOWN)
        if (k.program == p.n.program) {
            p.known = hex(p.n.h + 0x1C0, 32) == k.exefs && hex(p.n.h + 0x1E0, 32) == k.romfs;
            p.rebuilt = !p.known;
        }
}

// The bytes the reading pass reads of a file
uint64_t tree_bytes(const Part& p) { return p.cut || !p.usable ? 0 : p.lvl_size[0] + p.lvl_size[1] + p.lvl_size[2]; }

// One level of the RomFS's hash tree against the hashes over it (keep: its bytes, for the level under it). False: cancelled
bool read_level(Part& p, int level, const std::vector<uint8_t>& hashes, std::vector<uint8_t>* keep, uint64_t& done_all) {
    const uint64_t at = p.n.base + p.n.romfs_off + p.lvl_off[level], size = p.lvl_size[level], block = p.lvl_block[level];
    const size_t chunk = (size_t)std::max<uint64_t>(block, 1 << 20) / block * block;
    std::vector<uint8_t> buf(chunk);
    if (keep) keep->clear();
    uint8_t got[32];
    for (uint64_t o = 0; o < size; o += chunk) {
        if (g_cancel) return false;
        const size_t want = (size_t)std::min<uint64_t>(chunk, size - o);
        int64_t n = p.f.read(at + o, buf.data(), want);
        if (n < 0) n = 0;
        if ((size_t)n < want) memset(buf.data() + n, 0, want - (size_t)n);  // (unread: its blocks won't match)
        if (keep) keep->insert(keep->end(), buf.begin(), buf.begin() + want);
        for (size_t b = 0; b < want; b += block) {
            const uint64_t index = (o + b) / block;
            const size_t len = (size_t)std::min<uint64_t>(block, want - b);
            sha256_padded(buf.data() + b, len, block, got);
            bool good = (index + 1) * 32 <= hashes.size() && !memcmp(got, &hashes[index * 32], 32) && (size_t)n >= b + len;
            if (!good) {
                if (level == 2) {
                    if (p.bad3.size() < 100000) p.bad3.push_back(o + b);
                } else {
                    p.bad12++;
                }
            }
        }
        p.f.forget(at + o, want);
        done_all += want;
        std::lock_guard<std::mutex> l(G().lock);
        G().state.done = done_all;
    }
    return true;
}

// The files of the RomFS (level 3: its own tables) where the bad blocks fall; "" for its tables or padding
std::vector<std::string> damaged_files(Part& p) {
    std::vector<std::string> out;
    const uint64_t l3 = p.n.base + p.n.romfs_off + p.lvl_off[2];
    uint8_t h[0x28];
    if (!p.f.read_exact(l3, h, sizeof h) || le(h, 4) != 0x28) return out;
    uint64_t dir_off = le(h + 0x0C, 4), dir_size = le(h + 0x10, 4), file_off = le(h + 0x1C, 4), file_size = le(h + 0x20, 4),
             data_off = le(h + 0x24, 4);
    if (dir_size > (32u << 20) || file_size > (32u << 20)) return out;
    std::vector<uint8_t> dirs(dir_size), files(file_size);
    if (!p.f.read_exact(l3 + dir_off, dirs.data(), dirs.size()) || !p.f.read_exact(l3 + file_off, files.data(), files.size())) return out;
    auto utf8 = [](const uint8_t* s, uint64_t bytes) {
        std::string o;
        for (uint64_t i = 0; i + 1 < bytes; i += 2) {
            unsigned c = s[i] | s[i + 1] << 8;
            if (c < 0x80) o += (char)c;
            else if (c < 0x800) o += (char)(0xC0 | c >> 6), o += (char)(0x80 | (c & 0x3F));
            else o += (char)(0xE0 | c >> 12), o += (char)(0x80 | ((c >> 6) & 0x3F)), o += (char)(0x80 | (c & 0x3F));
        }
        return o;
    };
    std::map<uint64_t, std::string> dir_path;
    auto path_of = [&](uint64_t d) {
        std::string path;
        for (int depth = 0; depth < 64 && d + 0x18 <= dirs.size(); depth++) {
            uint64_t parent = le(&dirs[d], 4), len = le(&dirs[d + 0x14], 4);
            if (d == 0 || d + 0x18 + len > dirs.size()) break;
            path = utf8(&dirs[d + 0x18], len) + "/" + path;
            if (parent == d) break;
            d = parent;
        }
        return path;
    };
    struct Range {
        uint64_t from, to;
        std::string name;
    };
    std::vector<Range> ranges;
    for (uint64_t at = 0; at + 0x20 <= files.size();) {
        uint64_t parent = le(&files[at], 4), off = le(&files[at + 8], 8), size = le(&files[at + 16], 8), len = le(&files[at + 0x1C], 4);
        if (at + 0x20 + len > files.size()) break;
        if (!dir_path.count(parent)) dir_path[parent] = path_of(parent);
        ranges.push_back({data_off + off, data_off + off + size, "romfs/" + dir_path[parent] + utf8(&files[at + 0x20], len)});
        at += 0x20 + align(len, 4);
    }
    std::sort(ranges.begin(), ranges.end(), [](const Range& a, const Range& b) { return a.from < b.from; });
    std::set<std::string> seen;
    bool tables = false;
    const uint64_t bs = p.lvl_block[2];
    for (uint64_t b : p.bad3) {
        bool any = false;
        auto it = std::upper_bound(ranges.begin(), ranges.end(), b + bs - 1, [](uint64_t v, const Range& r) { return v < r.from; });
        while (it != ranges.begin()) {
            --it;
            if (it->to <= b) break;  // (the files don't overlap and are in order: none further back reaches it)
            any = true;
            if (seen.insert(it->name).second) out.push_back(it->name);
        }
        if (!any) tables = true;
    }
    if (tables) out.insert(out.begin(), "romfs: its own file tables");
    return out;
}

// ---------------------------------------------------------------- the checks
std::string mods_names(const std::vector<std::string>& names) {
    std::string s;
    for (size_t i = 0; i < names.size() && i < 5; i++) s += (i ? ", " : "") + names[i];
    if (names.size() > 5) s += F(" (+%d)", (int)names.size() - 5);
    return s;
}

#ifndef VERIFY_FILES_TEST
// The parts that need the running app: the mods that are on, the code mods that clash and the library the game runs
void app_checks(Report& r) {
    namespace m = mh4n::mods;
    const auto mods = m::Enabled(m::Folder(G().paths.user), content_title());
    std::vector<std::string> files, code;
    for (const auto& mod : mods) {
        if (mod.code) code.push_back(mod.name);
        if (mod.romfs) files.push_back(mod.name);
    }
    for (const auto& c : m::CodeConflicts(mods)) {
        r.problem(F("Code mods that don't go together: %s and %s.", c.a.c_str(), c.b.c_str()),
                  F("Mods de código que no van juntos: %s y %s.", c.a.c_str(), c.b.c_str()));
        r.todo("Turn one of the clashing code mods off: Mods & Cheats › Mods.",
               "Desactiva uno de los mods de código que chocan: Mods y trucos › Mods.");
    }
    const m::LibraryId lib = m::RunningLibrary();
    if (lib.known && !G().paths.game.empty()) {
        const m::CodeCheck check = m::CheckCode(G().paths.game, mods, lib);
        if (check.matches) {
            r.ok(code.empty() ? "The code library the game runs was made from this game's code."
                              : "The code library the game runs was made from this game's code, with the code mods that are on.",
                 code.empty() ? "La librería de código que usa el juego se hizo con el código de este juego."
                              : "La librería de código que usa el juego se hizo con el código de este juego, con los mods de código activados.");
        } else {
            r.problem(code.empty() ? "The code library wasn't made from this game's code: the game may not start or may crash."
                                   : "The code library wasn't made from this game's code with the code mods that are on: the game may not start or may crash.",
                      code.empty() ? "La librería de código no se hizo con el código de este juego: el juego puede no arrancar o cerrarse."
                                   : "La librería de código no se hizo con el código de este juego y los mods de código activados: el juego puede no arrancar o cerrarse.");
            if (!check.error.empty()) r.files({check.error});
            todo_library(r);
        }
    }
    if (!code.empty()) {
        r.info(F("Code mods on: %s. The code library has to be made with them.", mods_names(code).c_str()),
               F("Mods de código activados: %s. La librería de código tiene que hacerse con ellos.", mods_names(code).c_str()));
    }
    if (!files.empty()) {
        r.info(F("Mods on that change the game's files: %s. The check reads your game, not the mods.", mods_names(files).c_str()),
               F("Mods activados que cambian archivos del juego: %s. La comprobación lee tu juego, no los mods.", mods_names(files).c_str()));
    }
    if (!mods.empty()) todo_mods(r);
}
#endif

void run() {
    sha256_init();
    const auto t0 = std::chrono::steady_clock::now();
    Report r;
    const VerifyPaths& P = G().paths;
    fprintf(stderr, "[verify] start: game folder %s\n", first_line(P.ext + "/game_folder.txt").c_str());
    // ---- the game folder and its files (GameFolderActivity.contents), and what the app made from them (source.txt)
    const std::string folder = first_line(P.ext + "/game_folder.txt");
    std::string game_path, update_path, library;
    std::vector<std::string> games, updates;
    if (!folder.empty() && is_dir(folder)) {
        if (DIR* d = opendir(folder.c_str())) {
            std::vector<std::string> names;
            while (dirent* e = readdir(d)) names.push_back(e->d_name);
            closedir(d);
            std::sort(names.begin(), names.end());
            for (const std::string& n : names) {
                const std::string p = folder + "/" + n, l = lower(n);
                if (n[0] == '.' || is_dir(p)) continue;
                if (l == "libmh4u_code_native.so" || (ends_with(l, "_native.so") && library.empty())) library = p;
                if (ends_with(l, ".3ds") || ends_with(l, ".cci") || ends_with(l, ".cxi") || ends_with(l, ".cia")) {
                    File f;
                    Ncch n2;
                    if (!f.open(p) || !find_ncch(f, n2)) continue;
                    uint32_t t = (uint32_t)n2.program, hi = (uint32_t)(n2.program >> 32);
                    if (t != USA && t != EUR && t != MH4G) continue;
                    if (hi == 0x00040000) games.push_back(p);
                    if (hi == 0x0004000E) updates.push_back(p);
                }
            }
        }
    }
    std::string run_base, run_update;
    if (!P.game.empty())
        if (FILE* f = fopen((P.game + "/source.txt").c_str(), "rb")) {
            char b[4096];
            while (fgets(b, sizeof b, f)) {
                std::string s = b;
                while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
                if (!s.compare(0, 5, "base=")) run_base = s.substr(5);
                if (!s.compare(0, 7, "update=")) run_update = s.substr(7);
            }
            fclose(f);
        }
    auto pick = [](const std::string& ran, const std::vector<std::string>& found) {
        if (!ran.empty() && exists(ran)) return ran;
        return found.empty() ? std::string() : found[0];
    };
    game_path = pick(run_base, games), update_path = pick(run_update, updates);
    if (folder.empty() || !is_dir(folder)) {
        r.problem("The game folder isn't there: no folder was chosen, or it was moved or deleted.",
                  "La carpeta del juego no está: no se eligió ninguna, o se movió o se borró.");
        todo_choose_again(r);
    }
    if ((!run_base.empty() && !exists(run_base)) || (!run_update.empty() && !exists(run_update))) {
        r.problem("The game started from files that aren't there now (moved or deleted).",
                  "El juego arrancó con archivos que ya no están (se movieron o se borraron).");
        std::vector<std::string> gone;
        for (const std::string& f : {run_base, run_update})
            if (!f.empty() && !exists(f)) gone.push_back(f);
        r.files(gone);
        todo_choose_again(r);
    }
    if (game_path.empty() && !folder.empty() && is_dir(folder)) {
        r.problem("The game (.3ds or .cci, decrypted) is missing from the game folder.",
                  "Falta el juego (.3ds o .cci, descifrado) en la carpeta del juego.");
        todo_copy_again(r);
    }
    if (update_path.empty() && !folder.empty() && is_dir(folder)) {
        r.problem("The 1.3.0 update (.cia, decrypted) is missing from the game folder: the port runs the game with it applied.",
                  "Falta la actualización 1.3.0 (.cia, descifrada) en la carpeta del juego: el port corre el juego con ella aplicada.");
        todo_copy_again(r);
    }
    if (games.size() > 1 || updates.size() > 1) {
        std::vector<std::string> extra;
        for (const auto& g : games)
            if (g != game_path) extra.push_back(base_name(g));
        for (const auto& u : updates)
            if (u != update_path) extra.push_back(base_name(u));
        r.info(F("The game folder holds more than one game or update: %s and %s are the ones used.", base_name(game_path).c_str(),
                 base_name(update_path).c_str()),
               F("La carpeta del juego tiene más de un juego o actualización: se usan %s y %s.", base_name(game_path).c_str(),
                 base_name(update_path).c_str()));
        r.files(extra);
    }
    Part parts[2];
    parts[0].path = game_path, parts[0].name = base_name(game_path);
    parts[1].path = update_path, parts[1].name = base_name(update_path), parts[1].update = true;
    for (Part& p : parts)
        if (!p.path.empty()) quick(p, r);
    // ---- what each file is: the game, its update, which version (the 4ucode tools' detection: the code's SHA-1)
    Part &game = parts[0], &upd = parts[1];
    const uint32_t gt = (uint32_t)game.n.program, ut = (uint32_t)upd.n.program;
    const bool game_ok = game.usable && (game.n.program >> 32) == 0x00040000, upd_ok = upd.usable && (upd.n.program >> 32) == 0x0004000E;
    const Version* uv = upd_ok ? by_sha1(upd.code_sha1) : nullptr;
    const Version* gv = game_ok ? by_sha1(game.code_sha1) : nullptr;
    bool modified_code = false;
    std::string game_en, game_es, upd_en, upd_es;  // (their OK lines: after the reading, which says whether they are whole)
    if ((game_ok && gt == MH4G) || (upd_ok && ut == MH4G)) {
        r.problem("This is Monster Hunter 4G (Japan). The port doesn't run it yet: its code differs from MH4U's and the tools don't have its addresses yet. Use MH4U (America or Europe) with its update.",
                  "Esto es Monster Hunter 4G (Japón). El port todavía no lo corre: su código es distinto del de MH4U y las herramientas aún no tienen sus direcciones. Usa MH4U (América o Europa) con su actualización.");
    } else if (game_ok && upd_ok && gt != ut) {
        r.problem(F("The update is not this game's: the game is %s's and the update %s's. They have to be of the same region.", region_en(gt), region_en(ut)),
                  F("La actualización no es de este juego: el juego es de %s y la actualización de %s. Tienen que ser de la misma región.",
                    region_es(gt), region_es(ut)));
        todo_copy_again(r);
    }
    if (upd_ok && ut != MH4G) {
        const int tv = upd.n.cia ? upd.n.cia_version : -1;
        if (uv && uv->update && uv->runs) {
            upd_en = F("%s: %s, the version the port runs", upd.name.c_str(), uv->en);
            upd_es = F("%s: %s, la versión que corre el port", upd.name.c_str(), uv->es);
            if (uv->title == EUR)
                r.info("The European game runs through its own address map: it was tested less than the American one.",
                       "El juego europeo corre con su propio mapa de direcciones: se probó menos que el americano.");
        } else if (tv >= 0 && ((ut == USA && tv != 1072) || (ut == EUR && tv != 1040))) {
            const char* want = ut == USA ? "1.3.0" : "1.1.0";
            r.problem(F("%s is the update %s, not %s: the port runs %s %s.", upd.name.c_str(), version_text(tv).c_str(), want, region_en(ut), want),
                      F("%s es la actualización %s, no la %s: el port corre %s %s.", upd.name.c_str(), version_text(tv).c_str(), want,
                        region_es(ut), want));
            todo_copy_again(r);
        } else if (!upd.code_sha1.empty()) {
            modified_code = true;
            r.problem(F("%s: its code is no version of the game the port knows (SHA-1 %s): a changed or another update.",
                        upd.name.c_str(), upd.code_sha1.substr(0, 12).c_str()),
                      F("%s: su código no es ninguna versión del juego que el port conozca (SHA-1 %s): una actualización cambiada u otra.",
                        upd.name.c_str(), upd.code_sha1.substr(0, 12).c_str()));
            todo_copy_again(r);
        }
    }
    if (game_ok && gt != MH4G) {
        if (gt != USA && gt != EUR) {
            r.problem(F("%s is not Monster Hunter 4 Ultimate.", game.name.c_str()), F("%s no es Monster Hunter 4 Ultimate.", game.name.c_str()));
            todo_copy_again(r);
        } else if (gv && !gv->update) {
            game_en = F("%s: the game (%s)", game.name.c_str(), gv->en);
            game_es = F("%s: el juego (%s)", game.name.c_str(), gv->es);
        } else if (!game.code_sha1.empty()) {
            // (its own code isn't the one that runs, the update's is; but a base whose code was changed was changed elsewhere too)
            r.info(F("%s: its own code is not the original base game's (the update's code is the one that runs).", game.name.c_str()),
                   F("%s: su propio código no es el del juego base original (el que corre es el de la actualización).", game.name.c_str()));
        }
    }
    // ---- the app's own copy of the update's code (Dump.prepare: files/game), made when the folder was chosen
    if (!P.game.empty() && upd_ok && !upd.code.empty()) {
        std::vector<uint8_t> have;
        if (FILE* f = fopen((P.game + "/code.bin").c_str(), "rb")) {
            uint8_t b[1 << 16];
            size_t n;
            while ((n = fread(b, 1, sizeof b, f)) > 0) have.insert(have.end(), b, b + n);
            fclose(f);
        }
        if (have != upd.code) {
            r.problem("The game runs another copy of the update's code than the one in your game folder: the folder changed since it was chosen.",
                      "El juego usa otra copia del código de la actualización que la de tu carpeta del juego: la carpeta cambió desde que se eligió.");
            todo_choose_again(r);
        }
    }
    // ---- the code library: an arm64 ELF, its id inside and beside it, this app's
    if (!folder.empty() && is_dir(folder)) {
        if (library.empty()) {
            r.problem("libmh4u_code_native.so is missing from the game folder: the game code, made once from your own game.",
                      "Falta libmh4u_code_native.so en la carpeta del juego: el código del juego, que se crea una vez desde tu propio juego.");
            todo_library(r);
        } else {
            const std::string name = base_name(library);
            uint8_t h[20] = {};
            File lf;
            bool elf = lf.open(library) && lf.read_exact(0, h, 20) && !memcmp(h, "\x7f" "ELF", 4) && h[4] == 2 && h[18] == 183;
            lf.close();
            const std::string beside = first_line(library.substr(0, library.size() - 3) + ".id"), inside = elf ? id_inside(library) : "";
            const std::string have = inside.empty() ? beside : inside;
            if (!elf) {
                r.problem(F("%s is not an Android (arm64) library.", name.c_str()), F("%s no es una librería para Android (arm64).", name.c_str()));
                todo_library(r);
            } else if (!inside.empty() && !beside.empty() && inside != beside) {
                r.problem(F("%s and the .id beside it come from different builds (copied from two places). Don't use a library someone else made.", name.c_str()),
                          F("%s y el .id que tiene al lado son de armados distintos (copiados de dos lugares). No uses una librería que hizo otra persona.",
                            name.c_str()));
                todo_library(r);
            } else if (!P.wanted_id.empty() && have.empty()) {
                r.problem(F("%s has no .id beside it: it was made with an older version of the tool.", name.c_str()),
                          F("%s no trae su .id al lado: se hizo con una versión anterior de la herramienta.", name.c_str()));
                todo_library(r);
            } else if (!P.wanted_id.empty() && have != P.wanted_id) {
                r.problem(F("%s is from another version of the app (id %s; this one runs %s).", name.c_str(), have.c_str(), P.wanted_id.c_str()),
                          F("%s es de otra versión de la app (id %s; esta usa %s).", name.c_str(), have.c_str(), P.wanted_id.c_str()));
                todo_library(r);
            } else {
                r.ok(F("%s: made for this version of the app (%s)%s.", name.c_str(), have.c_str(), beside.empty() ? "" : ", with its .id"),
                     F("%s: hecha para esta versión de la app (%s)%s.", name.c_str(), have.c_str(), beside.empty() ? "" : ", con su .id"));
            }
            if (!P.code.empty() && exists(P.code) && file_size(P.code) != file_size(library))
                r.info("The game folder's code library changed since the game started: the new one is used at the next start.",
                       "La librería de código de la carpeta del juego cambió desde que arrancó el juego: la nueva se usa al volver a abrirlo.");
        }
    }
    // ---- the save and its extra data (SaveFolder.java's layout)
    {
        const uint32_t t = game_ok && gt == EUR ? EUR : USA;
        const std::string sdmc = P.saves + "/sdmc/Nintendo 3DS/00000000000000000000000000000000/00000000000000000000000000000000";
        const std::string data = sdmc + F("/title/00040000/%08x/data/00000001", t), ext = sdmc + F("/extdata/00000000/%08x", t >> 8);
        bool save = exists(data + "/system"), extra = false;
        if (DIR* d = opendir(ext.c_str())) {
            while (dirent* e = readdir(d))
                if (e->d_name[0] != '.') extra = true;
            closedir(d);
        }
        if (P.saves.empty()) {
        } else if (!save) {
            r.info("No save yet: the game makes one when you start a new game.", "Todavía no hay partida: el juego crea una cuando empiezas una nueva.");
        } else if (!extra) {
            r.info("Your save is there, but not its extra data (guild cards, event quests, the Palico): the game makes it new at its start.",
                   "Tu partida está, pero no sus datos extra (tarjetas de gremio, misiones de evento, el Felyne): el juego los crea de nuevo al arrancar.");
            r.todo("If you brought your save from elsewhere, import it again with its extra data: System › Your game › Import save.",
                   "Si trajiste tu partida de otro lado, vuelve a importarla con sus datos extra: Sistema › Tu partida › Importar partida.");
        } else {
            r.ok("Your save and its extra data are there.", "Tu partida y sus datos extra están.");
        }
    }
#ifndef VERIFY_FILES_TEST
    app_checks(r);
#endif
    // ---- the reading: each RomFS's whole hash tree
    uint64_t total = 0, done = 0;
    for (Part& p : parts) total += tree_bytes(p);
    {
        std::lock_guard<std::mutex> l(G().lock);
        G().state.total = total, G().state.done = 0, G().state.stage = VerifyState::Reading;
        G().reading_at = std::chrono::steady_clock::now();
    }
    const auto reading = std::chrono::steady_clock::now();
    bool cancelled = false;
    for (Part& p : parts) {
        if (!tree_bytes(p) || cancelled) continue;
        if (!read_level(p, 0, p.master, &p.level1, done) || !read_level(p, 1, p.level1, &p.level2, done) ||
            !read_level(p, 2, p.level2, nullptr, done)) {
            cancelled = true;
            break;
        }
        p.level1.clear(), p.level1.shrink_to_fit(), p.level2.clear(), p.level2.shrink_to_fit();
        p.tree_read = true;
        if (p.bad12 || !p.bad3.empty()) {
            p.damaged = true;
            const int blocks = p.bad12 + (int)p.bad3.size();
            const int all = (int)((p.lvl_size[0] + p.lvl_block[0] - 1) / p.lvl_block[0] + (p.lvl_size[1] + p.lvl_block[1] - 1) / p.lvl_block[1] +
                                  (p.lvl_size[2] + p.lvl_block[2] - 1) / p.lvl_block[2]);
            r.problem(F("%s is damaged: blocks of its RomFS (the game's files) don't match their hashes, %d of %d.", p.name.c_str(), blocks, all),
                      F("%s está dañado: bloques de su RomFS (los archivos del juego) no coinciden con sus hashes, %d de %d.", p.name.c_str(), blocks, all));
            if (!p.bad3.empty()) r.files(damaged_files(p));
            if (!p.cut) todo_copy_again(r);
        }
    }
    const double read_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - reading).count();
    // ---- a copy rebuilt with hashes of its own (USA): a repack
    std::vector<std::string> rebuilt;
    for (Part& p : parts)
        if (p.usable && p.rebuilt && !p.damaged) rebuilt.push_back(p.name);
    if (!rebuilt.empty() || modified_code) {
        std::string what_en, what_es;
        if (!rebuilt.empty())
            what_en = mods_names(rebuilt) + " was rebuilt and differs from the original game",
            what_es = mods_names(rebuilt) + " se rearmó y no es igual al juego original";
        if (modified_code)
            what_en += std::string(what_en.empty() ? "" : "; ") + "the update's code was changed",
            what_es += std::string(what_es.empty() ? "" : "; ") + "el código de la actualización fue cambiado";
        r.problem(F("This copy looks modified (a repack from a download site?): %s. The port needs a copy made from your own game, untouched.", what_en.c_str()),
                  F("Esta copia parece modificada (¿un repack de una página de descargas?): %s. El port necesita una copia hecha de tu propio juego, sin tocar.",
                    what_es.c_str()));
        todo_copy_again(r);
    }
    // the two files' OK lines: whole and unchanged (the original's superblocks, USA), or whole by their own hashes (EUR)
    const std::string* oks[2][2] = {{&game_en, &game_es}, {&upd_en, &upd_es}};
    for (int i = 0, at = 0; i < 2; i++) {
        const Part& p = parts[i];
        if (oks[i][0]->empty()) continue;
        const bool whole = p.tree_read && !p.damaged && !p.cut;
        if (whole && p.known) r.ok_first(at++, *oks[i][0] + ", whole and unchanged.", *oks[i][1] + (i ? ", completa y sin cambios." : ", completo y sin cambios."));
        else if (whole && !p.rebuilt) r.ok_first(at++, *oks[i][0] + ", whole: every block matches its hash.", *oks[i][1] + (i ? ", completa: cada bloque coincide con su hash." : ", completo: cada bloque coincide con su hash."));
        else if (p.cut || p.damaged) r.info(*oks[i][0] + ".", *oks[i][1] + ".");  // (what it is, beside what is wrong with it)
        else r.ok_first(at++, *oks[i][0] + ".", *oks[i][1] + ".");
    }
    if (cancelled)
        r.first.push_back({VerifyLine::Info, F("Cancelled after reading %s of %s GB: what wasn't read was checked by its size only.", decimal(done / 1e9, false).c_str(),
                 decimal(total / 1e9, false).c_str()),
               F("Cancelada después de leer %s de %s GB: lo que no se leyó se comprobó solo por su tamaño.", decimal(done / 1e9, true).c_str(),
                 decimal(total / 1e9, true).c_str())});
    for (const VerifyLine& l : r.first) r.log.insert(r.log.begin(), "note: " + l.en);
    // ---- the report, and the log's lines
    const double all_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    fprintf(stderr, "[verify] %s: %d problem(s); %.1f of %.1f GB read in %.1f s (%.0f MB/s), %.1f s in all\n",
            cancelled ? "cancelled" : "done", r.count, done / 1e9, total / 1e9, read_s, read_s > 0 ? done / 1048576.0 / read_s : 0.0, all_s);
    for (Part& p : parts)
        if (p.usable)
            fprintf(stderr, "[verify] %s: program %016llx%s, code sha1 %s, exefs %s, romfs %s%s\n", p.path.c_str(),
                    (unsigned long long)p.n.program, p.n.cia ? F(" v%d", p.n.cia_version).c_str() : "", p.code_sha1.c_str(),
                    hex(p.n.h + 0x1C0, 8).c_str(), hex(p.n.h + 0x1E0, 8).c_str(),
                    p.known ? " (the original's)" : p.rebuilt ? " (not the original's)" : "");
    for (const std::string& l : r.log) fprintf(stderr, "[verify] %s\n", l.c_str());
    if (!cancelled) {  // (1.0.1, diag) verify_last.txt in the app's folder: the last check's verdict, which Share log takes
        if (FILE* last = fopen((P.ext + "/verify_last.txt").c_str(), "w")) {
            const time_t now = time(nullptr);
            char when[32] = "?";
            struct tm tmv {};
            if (localtime_r(&now, &tmv)) strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tmv);
            fprintf(last, "Verify game files, %s: %d problem(s)\n", when, r.count);
            static const char* const kinds[] = {"ok", "PROBLEM", "note", "file", "to do"};
            for (const VerifyLine& v : r.lines()) fprintf(last, "  %s: %s\n", kinds[v.kind], v.en.c_str());
            fclose(last);
        }
    }
    std::lock_guard<std::mutex> l(G().lock);
    G().state.lines = r.lines();
    G().state.problems = r.count;
    G().state.done = done;
    G().state.reading_s = read_s;
    G().state.stage = cancelled ? VerifyState::Cancelled : VerifyState::Done;
}

}  // namespace

void verify_files_init(const VerifyPaths& paths) {
    std::lock_guard<std::mutex> l(G().lock);
    G().paths = paths;
}

void verify_files_start() {
    if (g_busy.exchange(true)) return;
    g_cancel = false;
    {
        std::lock_guard<std::mutex> l(G().lock);
        G().state = VerifyState();
        G().state.stage = VerifyState::Listing;
    }
    std::thread([]() {
#if defined(__ANDROID__) || defined(__linux__)
        setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 10);  // (behind the game's threads)
#endif
        run();
        g_busy = false;
    }).detach();
}

void verify_files_cancel() { g_cancel = true; }

VerifyState verify_files_state() {
    std::lock_guard<std::mutex> l(G().lock);
    VerifyState s = G().state;
    if (s.stage == VerifyState::Reading) s.reading_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - G().reading_at).count();
    return s;
}

void verify_files_run_here() {
    g_cancel = false;
    {
        std::lock_guard<std::mutex> l(G().lock);
        G().state = VerifyState();
        G().state.stage = VerifyState::Listing;
    }
    run();
}
