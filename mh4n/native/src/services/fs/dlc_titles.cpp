// Add-on content titles the player brings (dlc_titles.h, where the parts taken from Azahar are named).
#include "dlc_titles.h"

#include <algorithm>
#include <cctype>
#if defined(_WIN32)
#include <cstdlib>
#endif
#include <cstring>
#include <ctime>
#include <filesystem>
#include <limits>
#include <set>
#include <string>
#include <system_error>

#include <fmt/format.h>

#include "fs_state.h"
#include "host_files.h"
#include "mh4n/log.h"
#include "positioned_file.h"
#include "save_archives.h"

namespace mh4n::fs {

namespace {

constexpr const char* kLog = "Service.FS";

constexpr u16 kContentTypeOptional = 0x4000;  // TMDContentTypeFlag::Optional
constexpr std::size_t kTmdBodySize = 0x9C4;
constexpr std::size_t kTmdChunkSize = 0x30;
constexpr u64 kMediaUnit = 0x200;
constexpr u64 kIvfcHeaderSize = 0x1000;  // the RomFS section's header (kRomFsHeaderSize)
constexpr std::size_t kMaxProblemLines = 8;

u32 Be32(const u8* p) {
    return (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) | (static_cast<u32>(p[2]) << 8) | p[3];
}
u16 Be16(const u8* p) {
    return static_cast<u16>((p[0] << 8) | p[1]);
}
u64 Be64(const u8* p) {
    return (static_cast<u64>(Be32(p)) << 32) | Be32(p + 4);
}
u32 Le32(const u8* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<u32>(p[3]) << 24);
}

// signature.h's GetSignatureSize; 0 for a type it does not know.
u32 SignatureSize(u32 type) {
    switch (type) {
    case 0x10000:  // RSA-4096 SHA-1
    case 0x10003:  // RSA-4096 SHA-256
        return 0x200;
    case 0x10001:  // RSA-2048 SHA-1
    case 0x10004:  // RSA-2048 SHA-256
        return 0x100;
    case 0x10002:  // ECDSA SHA-1
    case 0x10005:  // ECDSA SHA-256
        return 0x3C;
    default:
        return 0;
    }
}

bool IsHex(const std::string& s, std::size_t length) {
    return s.size() == length && std::all_of(s.begin(), s.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
           });
}

std::string Slash(std::string folder) {
    if (!folder.empty() && !host::IsSeparator(folder.back())) folder += '/';
    return folder;
}

// What one .app is: the RomFS window of a readable one, or why it can't be read.
void Inspect(DlcContent& content) {
    if (!host::Exists(content.path) || host::IsDirectory(content.path)) {
        content.state = DlcContentState::Missing;
        return;
    }
    std::string error;
    const std::shared_ptr<PositionedFile> file = PositionedFile::Open(content.path, &error);
    u8 header[0x200] = {};
    if (file == nullptr || file->Size() < sizeof(header) || file->ReadAt(0, header, sizeof(header)) != sizeof(header)) {
        content.state = DlcContentState::CutShort;
        return;
    }
    if (std::memcmp(header, "Z3DS", 4) == 0) {
        content.state = DlcContentState::Compressed;
        return;
    }
    if (std::memcmp(header + 0x100, "NCCH", 4) != 0) {
        content.state = DlcContentState::NotNcch;
        return;
    }
    const u8* flags = header + 0x188;
    if ((flags[7] & 0x04) == 0) {  // NoCrypto
        content.state = DlcContentState::Encrypted;
        return;
    }
    const u64 unit = kMediaUnit << flags[6];
    const u64 offset = static_cast<u64>(Le32(header + 0x1B0)) * unit;
    const u64 size = static_cast<u64>(Le32(header + 0x1B4)) * unit;
    if (size <= kIvfcHeaderSize) {
        content.state = DlcContentState::NoRomFs;
        return;
    }
    if (offset + size > file->Size()) {
        content.state = DlcContentState::CutShort;
        return;
    }
    content.romfs_offset = offset + kIvfcHeaderSize;
    content.romfs_size = size - kIvfcHeaderSize;
    content.state = DlcContentState::Readable;
}

void LogTitle(const DlcTitle& title) {
    const std::size_t readable = title.Readable();
    MH4N_LOG(Info, kLog, "DLC title {:016X} on the {}: version {}, {} contents, {} readable ({})", title.title_id,
             title.media == kDlcMediaSdmc ? "SD card" : "NAND", title.version, title.contents.size(), readable,
             title.folder);
    std::size_t shown = 0, more = 0;
    for (const DlcContent& c : title.contents) {
        if (c.state == DlcContentState::Readable) continue;
        if (shown == kMaxProblemLines) {
            ++more;
            continue;
        }
        ++shown;
        MH4N_LOG(Warning, kLog, "DLC title {:016X}: content {:04X} ({:08x}.app) {}", title.title_id, c.index, c.id,
                 DlcContentStateText(c.state));
    }
    if (more != 0) {
        MH4N_LOG(Warning, kLog, "DLC title {:016X}: {} more contents can't be read either", title.title_id, more);
    }
}

}  // namespace

const char* DlcContentStateText(DlcContentState state) {
    switch (state) {
    case DlcContentState::Readable:
        return "can be read";
    case DlcContentState::Missing:
        return "is not there (the title is installed in part)";
    case DlcContentState::Encrypted:
        return "is encrypted: only a decrypted copy of your own can be read";
    case DlcContentState::Compressed:
        return "is compressed by Azahar's 'compress installed CIA content' option: turn it off and install the DLC "
               "again in Azahar, or import the decrypted .cia";
    case DlcContentState::NotNcch:
        return "is not a readable NCCH (installed encrypted for one emulator's console, or damaged): use a decrypted "
               "copy of your own";
    case DlcContentState::NoRomFs:
        return "has no RomFS";
    case DlcContentState::CutShort:
        return "ends too early (a copy cut short?)";
    }
    return "?";
}

const DlcContent* DlcTitle::Content(u16 index) const {
    for (const DlcContent& c : contents) {
        if (c.index == index) return &c;
    }
    return nullptr;
}

std::size_t DlcTitle::Readable() const {
    return static_cast<std::size_t>(std::count_if(contents.begin(), contents.end(), [](const DlcContent& c) {
        return c.state == DlcContentState::Readable;
    }));
}

bool ParseTitleMetadata(const std::vector<u8>& bytes, DlcTitle* title, std::string* error) {
    auto fail = [&](std::string why) {
        if (error) *error = std::move(why);
        return false;
    };
    if (bytes.size() < 4) return fail("the title metadata is empty");
    const u32 signature = SignatureSize(Be32(bytes.data()));
    if (signature == 0) return fail(fmt::format("the title metadata's signature type 0x{:X} is unknown", Be32(bytes.data())));
    const std::size_t body = (signature + 4 + 0x3F) & ~static_cast<std::size_t>(0x3F);
    if (bytes.size() < body + kTmdBodySize) return fail("the title metadata is cut short");
    const u8* b = bytes.data() + body;
    const u16 count = Be16(b + 0x9E);
    if (bytes.size() < body + kTmdBodySize + count * kTmdChunkSize) return fail("the title metadata is cut short");
    title->title_id = Be64(b + 0x4C);
    title->title_type = Be32(b + 0x54);
    title->version = Be16(b + 0x9C);
    title->contents.clear();
    for (u16 i = 0; i < count; ++i) {
        const u8* c = b + kTmdBodySize + i * kTmdChunkSize;
        DlcContent content;
        content.id = Be32(c);
        content.index = Be16(c + 4);
        content.type = Be16(c + 6);
        content.size = Be64(c + 8);
        title->contents.push_back(content);
    }
    return true;
}

bool ReadDlcTitle(const std::string& folder_in, u64 title_id, u32 media, DlcTitle* title, std::string* error) {
    const std::string folder = Slash(folder_in);
    const std::string content_dir = folder + "content/";
    const auto children = host::ListDirectory(content_dir);
    if (!children) {
        if (error) *error = "it has no content folder";
        return false;
    }
    // GetTitleMetadataPath: the lowest-numbered .tmd
    u32 tmd_id = std::numeric_limits<u32>::max();
    for (const host::Child& child : *children) {
        if (child.is_directory || child.name.size() != 12) continue;
        std::string ext = child.name.substr(8);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return static_cast<char>(std::tolower(c)); });
        const std::string stem = child.name.substr(0, 8);
        if (ext == ".tmd" && IsHex(stem, 8)) tmd_id = std::min(tmd_id, static_cast<u32>(std::stoul(stem, nullptr, 16)));
    }
    if (tmd_id == std::numeric_limits<u32>::max()) {
        if (error) *error = "it has no title metadata (.tmd)";
        return false;
    }
    title->folder = folder;
    title->media = media;
    title->tmd_path = content_dir + fmt::format("{:08x}.tmd", tmd_id);
    std::string read_error;
    const std::shared_ptr<PositionedFile> file = PositionedFile::Open(title->tmd_path, &read_error);
    std::vector<u8> bytes;
    if (file != nullptr && file->Size() < (1u << 20)) {
        bytes.resize(static_cast<std::size_t>(file->Size()));
        if (file->ReadAt(0, bytes.data(), bytes.size()) != bytes.size()) bytes.clear();
    }
    if (bytes.empty()) {
        if (error) *error = "its title metadata can't be read";
        return false;
    }
    if (!ParseTitleMetadata(bytes, title, error)) return false;
    if (title->title_id != title_id) {
        if (error) *error = fmt::format("its title metadata is title {:016X}'s", title->title_id);
        return false;
    }
    // GetTitleContentPath: DLC keeps its .app files in content/00000000/ (more than one content, content 1 optional)
    const bool in_subfolder =
        title->contents.size() > 1 && (title->contents[1].type & kContentTypeOptional) != 0;
    for (DlcContent& c : title->contents) {
        const std::string name = fmt::format("{:08x}.app", c.id);
        const std::string first = content_dir + (in_subfolder ? "00000000/" : "") + name;
        const std::string second = content_dir + (in_subfolder ? "" : "00000000/") + name;
        c.path = host::Exists(first) || !host::Exists(second) ? first : second;
        Inspect(c);
    }
    return true;
}

std::vector<std::pair<u32, std::string>> DlcTitles::TitleRoots(const FsState& state,
                                                                const std::vector<std::string>& extra_folders) {
    std::vector<std::pair<u32, std::string>> roots;
    std::set<std::pair<u32, std::string>> seen;
    const auto add = [&](u32 media, const std::string& root) {
        if (root.empty()) return;
        if (seen.insert({media, root}).second) roots.emplace_back(media, root);
    };
    const auto sd = [&](const std::string& sdmc) {
        if (!sdmc.empty()) add(kDlcMediaSdmc, fmt::format("{}Nintendo 3DS/{}/{}/title/", Slash(sdmc), kSystemId, kSdCardId));
    };
    const auto nand = [&](const std::string& dir) {
        if (!dir.empty()) add(kDlcMediaNand, fmt::format("{}{}/title/", Slash(dir), kSystemId));
    };
    sd(SdmcDirectory(state));
    if (!state.user_folder.empty()) sd(state.user_folder + "sdmc/");
    nand(NandDirectory(state));
    for (const std::string& extra : extra_folders) {
        const std::string dir = Slash(extra);
        if (host::IsDirectory(dir + "Nintendo 3DS")) {  // an SD card's folder (Azahar's sdmc/)
            sd(dir);
        } else {  // an Azahar user folder
            sd(dir + "sdmc/");
            nand(dir + "nand/");
        }
    }
    return roots;
}

void DlcTitles::Scan(const FsState& state, const std::vector<std::string>& extra_folders) {
    titles_.clear();
    std::set<std::pair<u32, u64>> found;
    for (const auto& [media, root] : TitleRoots(state, extra_folders)) {
        const std::string dlc_root = root + fmt::format("{:08x}/", kTidHighDlc);
        const auto lows = host::ListDirectory(dlc_root);
        if (!lows) continue;
        for (const host::Child& low : *lows) {
            if (!low.is_directory || !IsHex(low.name, 8)) continue;
            const u64 title_id = (static_cast<u64>(kTidHighDlc) << 32) | std::stoul(low.name, nullptr, 16);
            if (found.count({media, title_id}) != 0) {
                MH4N_LOG(Info, kLog, "DLC title {:016X} is also in {}{}/: the first one found is used", title_id,
                         dlc_root, low.name);
                continue;
            }
            DlcTitle title;
            std::string error;
            if (!ReadDlcTitle(dlc_root + low.name, title_id, media, &title, &error)) {
                MH4N_LOG(Warning, kLog, "DLC title {:016X} in {}{}/ is left out: {}", title_id, dlc_root, low.name,
                         error);
                continue;
            }
            found.insert({media, title_id});
            LogTitle(title);
            titles_.push_back(std::move(title));
        }
    }
    if (titles_.empty()) {
        MH4N_LOG(Info, kLog, "DLC titles: none (the game's add-on content is answered as not installed)");
    }
}

const DlcTitle* DlcTitles::Find(u32 media, u64 title_id) const {
    for (const DlcTitle& title : titles_) {
        if (title.media == media && title.title_id == title_id) return &title;
    }
    return nullptr;
}

// ---- the installed DLC in every save (1.0.1) ----------------------------------------------------------------------

bool HasReadableGameDlc(const FsState& state) {
    const u32 game_low = static_cast<u32>(state.game.program_id & 0xFFFFFFFF);
    for (const DlcTitle& title : state.dlc.All()) {
        const bool ours = state.game.program_id == 0 || static_cast<u32>(title.title_id & 0xFFFFFFFF) == game_low;
        if (ours && title.Readable() > 0) return true;
    }
    return false;
}

std::string SaveBackupsFolder(const FsState& state) {
#if defined(_WIN32)
    if (const char* own = std::getenv("MH4N_SAVE_BACKUPS"); own != nullptr && *own != 0) {  // (the PC's: pc_main.cpp)
        std::string folder = own;
        if (!host::IsSeparator(folder.back())) folder += '/';
        return folder;
    }
#endif
    std::string folder = state.user_folder;
    if (folder.empty()) return {};
    while (!folder.empty() && host::IsSeparator(folder.back())) folder.pop_back();
    std::size_t cut = folder.size();
    while (cut > 0 && !host::IsSeparator(folder[cut - 1])) --cut;
    return folder.substr(0, cut) + "save_backups/";  // (a user folder named without a folder around it: here)
}

namespace {

// A file or a folder tree copied whole (std::filesystem's own copy, through the host paths of host_files.h).
bool CopyPart(const std::string& from, const std::string& to, std::string* error) {
    std::error_code ec;
    const std::filesystem::path source = host::ToPath(from), target = host::ToPath(to);
    std::filesystem::create_directories(target.parent_path(), ec);
    if (!ec) {
        if (std::filesystem::is_directory(source, ec)) {
            std::filesystem::copy(source, target, std::filesystem::copy_options::recursive, ec);
        } else if (!ec) {
            std::filesystem::copy_file(source, target, ec);
        }
    }
    if (ec) *error = fmt::format("{} could not be copied to {}: {}", from, to, ec.message());
    return !ec;
}

std::string LocalStamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%d_%H-%M-%S", &local);  // (SaveFolder.java's "yyyy-MM-dd_HH-mm-ss")
    return text;
}

}  // namespace

std::string BackUpGameSave(const FsState& state, const std::string& what, std::string* error) {
    std::string ignored;
    std::string& why = error != nullptr ? *error : ignored;
    const std::string backups = SaveBackupsFolder(state);
    const std::string sdmc = SdmcDirectory(state);
    if (backups.empty() || sdmc.empty()) {
        why = "there is no user folder (--user)";
        return {};
    }
    if (state.game.program_id == 0) {
        why = "the game's title id is not known";
        return {};
    }
    if (SdmcUnreachable(state)) {
        why = "the save folder can't be reached";
        return {};
    }
    const u32 low = static_cast<u32>(state.game.program_id & 0xFFFFFFFF);
    const std::string data = SaveDataPath(sdmc, state.game.program_id);
    if (!host::IsDirectory(data)) {
        why = fmt::format("there is no save in {}", data);
        return {};
    }
    const std::string stamp = LocalStamp();
    std::string keep = fmt::format("{}{}-{}/", backups, stamp, what);
    for (int n = 2; host::Exists(keep); ++n) keep = fmt::format("{}{}-{}-{}/", backups, stamp, what, n);
    const std::string title = fmt::format("{}{:08x}/", keep, low);
    const std::string metadata = SaveDataMetadataPath(sdmc, state.game.program_id);
    const std::string extdata = fmt::format("{}Nintendo 3DS/{}/{}/extdata/00000000/{:08x}/", sdmc, kSystemId, kSdCardId,
                                            (low >> 8) & 0xFFFF);
    bool ok = CopyPart(data, title + "data", &why);
    if (ok && host::Exists(metadata)) ok = CopyPart(metadata, title + "00000001.metadata", &why);
    if (ok && host::IsDirectory(extdata)) ok = CopyPart(extdata, title + "extdata", &why);
    if (!ok) {
        host::DeleteDirectoryRecursively(keep);
        return {};
    }
    host::SyncDirectoryTree(keep);
    return keep;
}

}  // namespace mh4n::fs
