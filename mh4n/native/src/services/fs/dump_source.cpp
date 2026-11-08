// The player's dump read in place (dump_source.h). After tools/extract_ncch.py, tools/blz.py and
// tools/native/prepare_game.py of this repository.
#include "dump_source.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <fmt/format.h>

#include "mh4n/log.h"

namespace mh4n::fs::dump {
namespace {

constexpr u64 kMediaUnit = 0x200;
constexpr u32 kExheaderFullSize = 0x800;

u32 Le32(const u8* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<u32>(p[3]) << 24);
}
u64 Le64(const u8* p) {
    return Le32(p) | (static_cast<u64>(Le32(p + 4)) << 32);
}
u32 Be32(const u8* p) {
    return (static_cast<u32>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}
u16 Be16(const u8* p) {
    return static_cast<u16>((p[0] << 8) | p[1]);
}
u64 Be64(const u8* p) {
    return (static_cast<u64>(Be32(p)) << 32) | Be32(p + 4);
}
u64 Align(u64 x, u64 a) {
    return (x + a - 1) / a * a;
}

bool Read(const PositionedFile& f, u64 offset, std::size_t size, std::vector<u8>* out, std::string* error) {
    out->resize(size);
    bool failed = false;
    if (f.ReadAt(offset, out->data(), size, &failed) != size) {
        if (error) *error = failed ? "the file could not be read" : "the file ends too early (a copy cut short?)";
        return false;
    }
    return true;
}

bool ParseNcch(const PositionedFile& f, u64 base, Ncch* n, std::string* error) {
    std::vector<u8> h;
    if (!Read(f, base, 0x200, &h, error)) return false;
    if (std::memcmp(h.data() + 0x100, "NCCH", 4) != 0) {
        if (error) *error = "this is not a 3DS game (no NCCH header where one should be)";
        return false;
    }
    n->base = base;
    n->program_id = Le64(h.data() + 0x118);
    n->exheader_size = Le32(h.data() + 0x180);
    const u8* flags = h.data() + 0x188;
    const u64 unit = kMediaUnit << flags[6];
    n->exefs_offset = Le32(h.data() + 0x1A0) * unit;
    n->exefs_size = Le32(h.data() + 0x1A4) * unit;
    n->romfs_offset = Le32(h.data() + 0x1B0) * unit;
    n->romfs_size = Le32(h.data() + 0x1B4) * unit;
    n->no_crypto = (flags[7] & 0x04) != 0;
    return true;
}

}  // namespace

bool FindNcch(const PositionedFile& f, Ncch* out, std::string* error) {
    std::vector<u8> head;
    if (!Read(f, 0, 0x200, &head, error)) return false;
    *out = Ncch{};
    if (std::memcmp(head.data() + 0x100, "NCSD", 4) == 0) {  // a cartridge dump: partition 0 is the game
        const u64 offset = Le32(head.data() + 0x120) * kMediaUnit;
        if (Le32(head.data() + 0x124) == 0) {
            if (error) *error = "the cartridge dump's game partition is empty";
            return false;
        }
        return ParseNcch(f, offset, out, error);
    }
    if (std::memcmp(head.data() + 0x100, "NCCH", 4) == 0) {
        return ParseNcch(f, 0, out, error);
    }
    if (Le32(head.data()) == 0x2020) {  // a CIA: its content 0
        const u32 header_size = Le32(head.data() + 0x00), cert = Le32(head.data() + 0x08),
                  ticket = Le32(head.data() + 0x0C), tmd_size = Le32(head.data() + 0x10);
        const u64 cert_off = Align(header_size, 64), tik_off = Align(cert_off + cert, 64),
                  tmd_off = Align(tik_off + ticket, 64), content_off = Align(tmd_off + tmd_size, 64);
        std::vector<u8> tmd;
        if (!Read(f, tmd_off, tmd_size, &tmd, error)) return false;
        const u32 sig = tmd.size() >= 4 ? Be32(tmd.data()) : 0;
        u32 sig_len = 0;
        switch (sig) {
        case 0x10000: case 0x10003: sig_len = 0x200 + 0x3C; break;
        case 0x10001: case 0x10004: sig_len = 0x100 + 0x3C; break;
        case 0x10002: case 0x10005: sig_len = 0x3C + 0x40; break;
        default:
            if (error) *error = fmt::format("the .cia's title metadata is not one this app knows (0x{:X})", sig);
            return false;
        }
        const u64 th = 4 + sig_len;
        const u64 chunks = th + 0xC4 + 0x24 * 64;
        if (tmd.size() < th + 0xA0) {
            if (error) *error = "the .cia's title metadata is cut short";
            return false;
        }
        const u64 title_id = Be64(tmd.data() + th + 0x4C);
        const u16 version = Be16(tmd.data() + th + 0x9C);
        const u16 count = Be16(tmd.data() + th + 0x9E);
        u64 pos = content_off;
        for (u32 i = 0; i < count && chunks + i * 0x30 + 0x10 <= tmd.size(); ++i) {
            const u8* c = tmd.data() + chunks + i * 0x30;
            const u16 index = Be16(c + 4), type = Be16(c + 6);
            const u64 size = Be64(c + 8);
            if (index == 0) {
                if (type & 1) {
                    if (error) *error = "the .cia is encrypted: it has to be a decrypted copy of your own";
                    return false;
                }
                if (!ParseNcch(f, pos, out, error)) return false;
                out->cia_title_id = title_id;
                out->cia_title_version = version;
                return true;
            }
            pos += size;
        }
        if (error) *error = "the .cia has no game content";
        return false;
    }
    if (error) *error = "this file is not a 3DS game (.3ds, .cci, .cxi or .cia)";
    return false;
}

bool ReadExeFsFile(const PositionedFile& f, const Ncch& n, const char* name, std::vector<u8>* out,
                   std::string* error) {
    std::vector<u8> header;
    if (n.exefs_size < 0x200 || !Read(f, n.base + n.exefs_offset, 0x200, &header, error)) {
        if (error && error->empty()) *error = "the game has no ExeFS";
        return false;
    }
    for (int i = 0; i < 10; ++i) {
        const u8* e = header.data() + i * 16;
        char entry[9] = {};
        std::memcpy(entry, e, 8);
        const u32 offset = Le32(e + 8), size = Le32(e + 12);
        if (size != 0 && std::strcmp(entry, name) == 0) {
            if (0x200 + static_cast<u64>(offset) + size > n.exefs_size) {
                if (error) *error = fmt::format("the ExeFS file {} runs past the ExeFS", name);
                return false;
            }
            return Read(f, n.base + n.exefs_offset + 0x200 + offset, size, out, error);
        }
    }
    if (error) *error = fmt::format("the game's ExeFS has no {}", name);
    return false;
}

bool BlzDecompress(std::vector<u8>& data, std::string* error) {
    const std::size_t size = data.size();
    if (size < 8) {
        if (error) *error = "the code is too small to be compressed";
        return false;
    }
    const u32 top_bottom = Le32(data.data() + size - 8), original_bottom = Le32(data.data() + size - 4);
    const u32 top = top_bottom & 0xFFFFFF, bottom = top_bottom >> 24;
    if (bottom < 8 || bottom > 11 || top < bottom || top > size) {
        if (error) *error = "the code's compression footer is not valid";
        return false;
    }
    const std::size_t out_size = size + original_bottom;
    std::vector<u8> out(out_size);
    std::memcpy(out.data(), data.data(), size);
    std::size_t src = size - bottom, dst = out_size;
    const std::size_t end = size - top;
    while (src > end) {
        u8 flags = data[--src];
        for (int i = 0; i < 8; ++i) {
            if ((flags & 0x80) == 0) {
                if (src - end < 1 || dst - end < 1) {
                    if (error) *error = "the compressed code is damaged";
                    return false;
                }
                out[--dst] = data[--src];
            } else {
                if (src - end < 2) {
                    if (error) *error = "the compressed code is damaged";
                    return false;
                }
                src -= 2;
                const u8 hi = data[src + 1], lo = data[src];
                const std::size_t length = (hi >> 4) + 3, disp = (((hi & 0x0F) << 8) | lo) + 3;
                if (dst - end < length || dst + disp > out_size) {
                    if (error) *error = "the compressed code is damaged";
                    return false;
                }
                for (std::size_t k = 0; k < length; ++k) {
                    --dst;
                    out[dst] = out[dst + disp];
                }
            }
            flags = static_cast<u8>(flags << 1);
            if (src <= end) break;
        }
    }
    data = std::move(out);
    return true;
}

std::shared_ptr<PositionedFile> OpenRomFs(const std::string& path, std::string* error) {
    std::shared_ptr<PositionedFile> whole = PositionedFile::Open(path, error);
    if (whole == nullptr) return nullptr;
    Ncch n;
    if (!FindNcch(*whole, &n, error)) return nullptr;
    if (!n.no_crypto) {
        if (error) *error = path + " is encrypted: it has to be a decrypted copy of your own";
        return nullptr;
    }
    if (n.romfs_size == 0) {
        if (error) *error = path + " has no RomFS";
        return nullptr;
    }
    return PositionedFile::OpenWindow(path, n.base + n.romfs_offset, n.romfs_size, error);
}

bool PrepareGameFolder(const std::string& base, const std::string& update, const std::string& folder,
                       std::string* error) {
    auto fail = [&](const std::string& what) {
        if (error) *error = what;
        return false;
    };
    std::string e;
    auto base_file = PositionedFile::Open(base, &e);
    if (!base_file) return fail(e);
    auto update_file = PositionedFile::Open(update, &e);
    if (!update_file) return fail(e);
    Ncch game, patch;
    if (!FindNcch(*base_file, &game, &e)) return fail("the game: " + e);
    if (!FindNcch(*update_file, &patch, &e)) return fail("the update: " + e);
    if (!game.no_crypto) return fail("the game is encrypted: it has to be a decrypted copy of your own");
    if (!patch.no_crypto) return fail("the update is encrypted: it has to be a decrypted copy of your own");
    const u64 game_id = game.program_id, update_id = patch.program_id;
    if ((game_id >> 32) != 0x00040000 || ((game_id & 0xFFFFFFFF) != 0x00126300 && (game_id & 0xFFFFFFFF) != 0x00126100)) {
        return fail(fmt::format("the game is not Monster Hunter 4 Ultimate (title {:016X})", game_id));
    }
    if ((update_id >> 32) != 0x0004000E) {
        return fail(fmt::format("the second file is not an update (title {:016X}): it has to be the update's .cia", update_id));
    }
    if ((update_id & 0xFFFFFFFF) != (game_id & 0xFFFFFFFF)) {
        return fail("the update is not this game's: the game and its update have to be of the same region");
    }
    if (patch.exheader_size == 0) return fail("the update has no exheader");
    std::vector<u8> exheader, code, icon;
    if (!Read(*update_file, patch.base + 0x200, kExheaderFullSize, &exheader, &e)) return fail("the update: " + e);
    if (!ReadExeFsFile(*update_file, patch, ".code", &code, &e)) return fail("the update: " + e);
    if (exheader[0x0D] & 1) {
        if (!BlzDecompress(code, &e)) return fail("the update: " + e);
    }
    const bool has_icon = ReadExeFsFile(*update_file, patch, "icon", &icon, &e);

    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    auto write = [&](const char* name, const std::vector<u8>& bytes) {
        std::ofstream o(folder + "/" + name, std::ios::binary | std::ios::trunc);
        o.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return static_cast<bool>(o);
    };
    if (!write("exheader.bin", exheader) || !write("code.bin", code) || (has_icon && !write("icon.bin", icon))) {
        return fail("the game folder could not be written in " + folder);
    }
    std::filesystem::remove(folder + "/romfs.bin", ec);
    std::filesystem::remove(folder + "/update_romfs.bin", ec);
    {
        std::ofstream s(folder + "/source.txt", std::ios::trunc);
        s << "base=" << base << "\nupdate=" << update << "\n";
    }
    {
        std::ofstream i(folder + "/info.txt", std::ios::trunc);
        i << fmt::format("base game     : program {:016X}\nupdate        : title {:016X}, title version {} "
                         "(v{}.{}.{})\nicon source   : {}\ninput base  : {}, 0x{:X} bytes\ninput update: {}, 0x{:X} bytes\n",
                         game_id, update_id, patch.cia_title_version, patch.cia_title_version >> 10,
                         (patch.cia_title_version >> 4) & 0x3F, patch.cia_title_version & 0xF,
                         has_icon ? "update" : "none", base, base_file->Size(), update, update_file->Size());
    }
    return true;
}

}  // namespace mh4n::fs::dump
