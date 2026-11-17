// Synthetic add-on content (DLC) titles for the tests (1.0.1): a title metadata file and .app files laid out as
// Azahar installs a title (title/<high>/<low>/content/00000000.tmd, the .app files in content/00000000/ when content 1
// is optional), with made-up bytes. Nothing here comes from a real title: the signature is zeros, the contents are
// patterns the tests choose, and an .app is a bare NCCH header followed by its RomFS section.
#pragma once

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "mh4n/types.h"

namespace mh4n::test_dlc {

enum class AppKind {
    Readable,    // a decrypted NCCH with a RomFS section
    Encrypted,   // the same, without the NoCrypto flag
    Compressed,  // a file that starts with Z3DS
    NotNcch,     // bytes with no NCCH header
    Missing,     // in the title metadata, no .app
    NoRomFs,     // an NCCH without a RomFS section
};

struct App {
    u16 index = 0;
    u32 id = 0;
    AppKind kind = AppKind::Readable;
    std::vector<u8> romfs;  // the bytes the game reads (the RomFS section past its 0x1000-byte header)
};

inline void PutBe(std::vector<u8>& b, std::size_t at, u64 value, int bytes) {
    for (int i = 0; i < bytes; ++i) b[at + static_cast<std::size_t>(i)] = static_cast<u8>(value >> (8 * (bytes - 1 - i)));
}
inline void PutLe(std::vector<u8>& b, std::size_t at, u64 value, int bytes) {
    for (int i = 0; i < bytes; ++i) b[at + static_cast<std::size_t>(i)] = static_cast<u8>(value >> (8 * i));
}

// The size of the section of `romfs` (its 0x1000-byte header and the RomFS, in 0x200-byte units).
inline std::size_t SectionUnits(const App& app) {
    return (0x1000 + app.romfs.size() + 0x1FF) / 0x200;
}

inline std::vector<u8> AppBytes(const App& app) {
    if (app.kind == AppKind::Compressed) {
        std::vector<u8> b(0x400, 0x5A);
        std::memcpy(b.data(), "Z3DS", 4);
        return b;
    }
    if (app.kind == AppKind::NotNcch) return std::vector<u8>(0x400, 0xC3);
    std::vector<u8> b(0x200, 0);
    std::memcpy(b.data() + 0x100, "NCCH", 4);
    PutLe(b, 0x118, 0x0004008C00000000ull | app.id, 8);
    b[0x188 + 7] = app.kind == AppKind::Encrypted ? 0x00 : 0x04;  // NoCrypto
    if (app.kind == AppKind::NoRomFs) return b;
    PutLe(b, 0x1B0, 1, 4);                   // the RomFS section right after the header
    PutLe(b, 0x1B4, SectionUnits(app), 4);
    std::vector<u8> section(SectionUnits(app) * 0x200, 0);
    std::memcpy(section.data(), "IVFC", 4);
    std::memcpy(section.data() + 0x1000, app.romfs.data(), app.romfs.size());
    b.insert(b.end(), section.begin(), section.end());
    return b;
}

inline u64 AppBytesSize(const App& app) {
    return AppBytes(app).size();
}

// The title metadata: RSA-2048 SHA-256 signature type (zeros), the body at 0x140, the chunk records after it.
inline std::vector<u8> Tmd(u64 title_id, u16 version, const std::vector<App>& apps) {
    constexpr std::size_t body = 0x140, body_size = 0x9C4, chunk = 0x30;
    std::vector<u8> b(body + body_size + apps.size() * chunk, 0);
    PutBe(b, 0, 0x00010004, 4);
    PutBe(b, body + 0x4C, title_id, 8);
    PutBe(b, body + 0x54, 0x40, 4);
    PutBe(b, body + 0x9C, version, 2);
    PutBe(b, body + 0x9E, apps.size(), 2);
    for (std::size_t i = 0; i < apps.size(); ++i) {
        const std::size_t c = body + body_size + i * chunk;
        PutBe(b, c, apps[i].id, 4);
        PutBe(b, c + 4, apps[i].index, 2);
        PutBe(b, c + 6, i == 0 ? 0x0000 : 0x4000, 2);  // content 1 on: optional, as DLC's are
        PutBe(b, c + 8, AppBytesSize(apps[i]), 8);
    }
    return b;
}

inline void WriteBytes(const std::filesystem::path& path, const std::vector<u8>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                                static_cast<std::streamsize>(bytes.size()));
}

// Writes the title under `title_root` (a .../title/ folder): <title_root>/0004008c/<low>/content/...
inline std::filesystem::path WriteTitle(const std::filesystem::path& title_root, u64 title_id, u16 version,
                                        const std::vector<App>& apps) {
    char low[9];
    std::snprintf(low, sizeof(low), "%08x", static_cast<unsigned>(title_id & 0xFFFFFFFFu));
    const std::filesystem::path folder = title_root / "0004008c" / low;
    WriteBytes(folder / "content" / "00000000.tmd", Tmd(title_id, version, apps));
    const bool sub = apps.size() > 1;
    for (const App& app : apps) {
        if (app.kind == AppKind::Missing) continue;
        char name[16];
        std::snprintf(name, sizeof(name), "%08x.app", static_cast<unsigned>(app.id));
        WriteBytes(sub ? folder / "content" / "00000000" / name : folder / "content" / name, AppBytes(app));
    }
    return folder;
}

// Azahar's SD card title folder under a user folder (sdmc/Nintendo 3DS/<zeros>/<zeros>/title/).
inline std::filesystem::path SdTitleRoot(const std::filesystem::path& sdmc) {
    const std::string zeros(32, '0');
    return sdmc / "Nintendo 3DS" / zeros / zeros / "title";
}

}  // namespace mh4n::test_dlc
