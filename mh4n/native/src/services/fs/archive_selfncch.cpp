// Derived from Azahar src/core/file_sys/archive_selfncch.cpp, src/core/file_sys/archive_selfncch.h,
// src/core/file_sys/ncch_container.cpp, src/core/file_sys/romfs_reader.cpp, src/core/loader/ncch.cpp and
// src/core/hle/service/fs/file.cpp at 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-3.0-or-later:
// ncch_container.cpp and file.cpp say "GPLv3 or any later version"; the others say "GPLv2 or any later
// version"): the SelfNCCH archive (archive id 3): the file path
// types and their layout (SelfNCCHFilePath: RomFS 0, Code 1, ExeFS 2, UpdateRomFS 5), what each answers,
// that the open mode is not checked, that an archive of an unregistered program has no RomFS, where
// the readable RomFS starts (NCCHContainer::ReadRomFS), the ExeFS section names (OpenExeFS), the file
// that serves one (ExeFSSectionFile, with NCCHContainer::LoadSectionExeFS giving it the section's bytes;
// AppLoader_NCCH::ReadIcon is LoadSectionExeFS("icon")) and that a failed read replies with its result code
// and 0 bytes (File::Read).
// Copyright 2017-2024 Citra Emulator Project / Azahar Emulator Project (archive_selfncch.cpp,
//   archive_selfncch.h)
// Copyright 2017-2026 Citra Emulator Project / Azahar Emulator Project (ncch_container.cpp)
// Copyright 2018-2026 Citra Emulator Project / Azahar Emulator Project (romfs_reader.cpp, file.cpp)
// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project (ncch.cpp)
//
// SelfNCCH (N1-12): the application's own RomFS, served from the game folder's romfs.bin, and the update's
// from update_romfs.bin. Departures from Azahar:
//  - ExeFS sections: the game folder holds the icon only, as icon.bin (prepare_game.py, D38), so `icon` is
//    answered like Azahar when that file is there, and `logo` and `banner` always answer "section not found"
//    (Azahar answers with the cartridge's bytes); `icon` answers "section not found" too without icon.bin.
//    The icon is read from the file when the game reads it, not kept in memory from the start.
//  - The data is registered once for the game's program id (from exheader.bin); a client that did not
//    initialise its session has program id 0 and finds no RomFS, as in Azahar (`ncch_data[program_id]`).
//
// The host reads the same images (game_romfs.h: OpenGameRomFsFile, 1.0.1.1, the health bars' monster names): the
// update's RomFS, then the game's, with the same mods laid over them (the same LayeredFS images, built once).
#include <algorithm>
#include <cstring>

#include <filesystem>
#include <map>
#include <mutex>

#include "../../common/utf8.h"
#include "../../layeredfs/layeredfs.h"
#include "../../mods/mod_list.h"
#include "file_backends.h"
#include "fs_state.h"
#include "game_romfs.h"
#include "mh4n/log.h"
#include "mh4n/registry.h"

namespace mh4n::fs {

namespace {

constexpr const char* kLog = "Service.FS";

// SelfNCCHFilePathType
constexpr u32 kPathRomFs = 0;
constexpr u32 kPathCode = 1;
constexpr u32 kPathExeFs = 2;
constexpr u32 kPathUpdateRomFs = 5;

// struct SelfNCCHFilePath: the type, then the ExeFS file name (12 bytes)
constexpr std::size_t kFilePathSize = 12;

// The readable part of a RomFS section: from 0x1000 to the end, or null if there is none.
std::shared_ptr<FileBackend> OpenRomFsSection(const std::shared_ptr<PositionedFile>& image) {
    if (image == nullptr || image->Size() < kRomFsHeaderSize) {
        return nullptr;
    }
    return std::make_shared<SliceFile>(image, kRomFsHeaderSize, image->Size() - kRomFsHeaderSize);
}

// What the game reads for `image` (SelfNCCH's OpenImage, and the host's OpenGameRomFsFile): LayeredFS (Azahar's
// NCCHContainer::ReadRomFS), the mods' romfs/ and romfs_ext/ over the image, for the base RomFS and the update's alike
// (both use the base title's mods, GetModId), when a mod that is on has either; else the image's RomFS section. Azahar
// has one mods folder; here every mod that is on is a layer, in load order (mods/mod_list.h). Built once per image and
// kept: the game opens its RomFS more than once. Null when there is no RomFS.
std::shared_ptr<FileBackend> ImageWithMods(const std::shared_ptr<PositionedFile>& image, const char* what,
                                           const std::vector<layeredfs::Layer>& layers, const std::string& mods_dir) {
    if (image != nullptr && image->Size() >= kRomFsHeaderSize && !layers.empty()) {
        std::error_code ec;
        const bool any = std::any_of(layers.begin(), layers.end(), [&ec](const layeredfs::Layer& l) {
            return std::filesystem::is_directory(utf8::PathFromUtf8(l.romfs_dir), ec) ||
                   std::filesystem::is_directory(utf8::PathFromUtf8(l.romfs_ext_dir), ec);
        });
        if (any) {
            static std::mutex mutex;
            static std::map<const PositionedFile*, std::shared_ptr<FileBackend>> layered;
            std::lock_guard lock(mutex);
            auto& entry = layered[image.get()];
            if (entry == nullptr) {
                std::string error;
                entry = layeredfs::Build(image, kRomFsHeaderSize, image->Size() - kRomFsHeaderSize, layers, &error);
                if (entry == nullptr) {
                    MH4N_LOG(Error, kLog, "SelfNCCH: the mods of {} could not be laid over the {}: {}", mods_dir, what,
                             error);
                } else {
                    MH4N_LOG(Info, kLog, "SelfNCCH: the {} with {} mod(s) of {}", what, layers.size(), mods_dir);
                }
            }
            if (entry != nullptr) {
                return entry;
            }
        }
    }
    return OpenRomFsSection(image);
}

// The romfs/ of each mod of the game that is on, in load order, and their folder (<user>/load/mods/; none without
// --user). An update's mods are the base game's (Azahar's GetModId, in mods::List).
std::vector<layeredfs::Layer> GameLayers(const std::string& user_folder, u64 program_id, std::string* mods) {
    std::vector<layeredfs::Layer> layers;
    mods->clear();
    if (!user_folder.empty()) {
        *mods = user_folder + "load/mods/";
        for (const mods::Layer& layer : mods::RomFsLayers(mods::Enabled(*mods, program_id))) {
            layers.push_back({layer.romfs, layer.romfs_ext});
        }
    }
    return layers;
}

// The game's FsState while its SelfNCCH factory lives (OpenGameRomFsFile)
std::mutex g_game_mutex;
FsState* g_game_state = nullptr;

// A read-only ExeFS section (Azahar's ExeFSSectionFile): it can be read once, whole, from its start. It is
// served from the file on the host, which is as big as the section.
class ExeFsSectionFile final : public FileBackend {
public:
    explicit ExeFsSectionFile(std::shared_ptr<PositionedFile> data) : data_(std::move(data)) {}

    bool ReadsRequestedLength() const override { return true; }

    Result Read(u64 offset, std::size_t length, u8* buffer, std::size_t* read) override {
        *read = 0;
        if (offset != 0) {
            MH4N_LOG(Error, kLog, "offset must be zero!");
            return kResultUnsupportedOpenFlags;
        }
        if (length != data_->Size()) {
            MH4N_LOG(Error, kLog, "size must match the file size!");
            return kResultIncorrectExefsReadSize;
        }
        bool failed = false;
        *read = data_->ReadAt(0, buffer, length, &failed);
        if (failed || *read != length) {
            // The file changed or the host failed since it was opened: Azahar's copy in memory cannot.
            MH4N_LOG(Error, kLog, "the icon could not be read from the game folder");
            *read = 0;
            return kResultHostFailure;
        }
        return kResultSuccess;
    }
    Result Write(u64, std::size_t, bool, bool, const u8*, std::size_t* written) override {
        MH4N_LOG(Error, kLog, "The file is read-only!");
        *written = 0;
        return kResultUnsupportedOpenFlags;
    }
    u64 GetSize() override { return data_->Size(); }

private:
    std::shared_ptr<PositionedFile> data_;
};

class SelfNcchArchive final : public ArchiveBackend {
public:
    SelfNcchArchive(std::shared_ptr<PositionedFile> romfs, std::shared_ptr<PositionedFile> update_romfs,
                    std::shared_ptr<PositionedFile> icon, std::vector<layeredfs::Layer> layers = {},
                    std::string mods_dir = {})
        : romfs_(std::move(romfs)), update_romfs_(std::move(update_romfs)), icon_(std::move(icon)),
          layers_(std::move(layers)), mods_dir_(std::move(mods_dir)) {}

    std::string GetName() const override { return "SelfNCCHArchive"; }

    Result OpenFile(const Path& path, u32 mode, u32 attributes,
                    std::shared_ptr<FileBackend>* file) override {
        (void)mode, (void)attributes;  // SelfNCCHArchive doesn't check the open mode
        if (!path.IsBinary()) {
            MH4N_LOG(Error, kLog, "SelfNCCH: the path needs to be binary");
            return kResultInvalidPath;
        }
        if (path.data.size() != kFilePathSize) {
            MH4N_LOG(Error, kLog, "SelfNCCH: wrong path size {}", path.data.size());
            return kResultInvalidPath;
        }
        u32 type = 0;
        std::memcpy(&type, path.data.data(), sizeof(type));
        switch (type) {
        case kPathUpdateRomFs:
            return OpenImage(update_romfs_, "update RomFS", file, layers_, mods_dir_);
        case kPathRomFs:
            return OpenImage(romfs_, "RomFS", file, layers_, mods_dir_);
        case kPathCode:
            MH4N_LOG(Error, kLog, "SelfNCCH: reading the code section is not supported");
            return kResultCommandNotAllowed;
        case kPathExeFs: {
            const char* name = reinterpret_cast<const char*>(path.data.data()) + sizeof(u32);
            const std::string filename(name, strnlen(name, 8));
            if (filename == "icon") {
                if (icon_ != nullptr) {
                    *file = std::make_shared<ExeFsSectionFile>(icon_);
                    return kResultSuccess;
                }
                MH4N_LOG(Warning, kLog, "SelfNCCH: unable to read {}", filename);
                return kResultExefsSectionNotFound;
            }
            if (filename == "logo" || filename == "banner") {
                MH4N_LOG(Warning, kLog, "SelfNCCH: unable to read {}", filename);
                return kResultExefsSectionNotFound;
            }
            MH4N_LOG(Error, kLog, "SelfNCCH: unknown ExeFS section {}", filename);
            return kResultInvalidPath;
        }
        default:
            MH4N_LOG(Error, kLog, "SelfNCCH: unknown file type {}", type);
            return kResultInvalidPath;
        }
    }

    // Every other operation is refused with UnsupportedOpenFlags (Azahar's SelfNCCHArchive).
    Result FileDelete(const Path&) override { return kResultUnsupportedOpenFlags; }
    Result DirectoryDelete(const Path&) override { return kResultUnsupportedOpenFlags; }
    Result DirectoryDeleteRecursively(const Path&) override { return kResultUnsupportedOpenFlags; }
    Result FileCreate(const Path&, u64, u32) override { return kResultUnsupportedOpenFlags; }
    Result DirectoryCreate(const Path&, u32) override { return kResultUnsupportedOpenFlags; }
    Result DirectoryOpen(const Path&, std::unique_ptr<DirectoryBackend>*) override {
        return kResultUnsupportedOpenFlags;
    }

private:
    static Result OpenImage(const std::shared_ptr<PositionedFile>& image, const char* what,
                            std::shared_ptr<FileBackend>* file, const std::vector<layeredfs::Layer>& layers,
                            const std::string& mods_dir) {
        *file = ImageWithMods(image, what, layers, mods_dir);
        if (*file == nullptr) {
            MH4N_LOG(Info, kLog, "SelfNCCH: unable to read the {}", what);
            return kResultRomfsNotFound;
        }
        return kResultSuccess;
    }

    std::shared_ptr<PositionedFile> romfs_, update_romfs_, icon_;
    std::vector<layeredfs::Layer> layers_;  // the romfs of each mod that is on, in load order
    std::string mods_dir_;                   // <user>/load/mods/, empty without --user
};

class SelfNcchFactory final : public ArchiveFactory {
public:
    explicit SelfNcchFactory(FsState& state) : state_(state) {
        std::lock_guard lock(g_game_mutex);
        g_game_state = &state_;
    }
    ~SelfNcchFactory() override {
        std::lock_guard lock(g_game_mutex);
        if (g_game_state == &state_) g_game_state = nullptr;
    }
    std::string GetName() const override { return "SelfNCCH"; }

    // The archive path is not looked at (Azahar's ArchiveFactory_SelfNCCH::Open).
    Result Open(const Path& path, u64 program_id, std::unique_ptr<ArchiveBackend>* archive) override {
        (void)path;
        if (program_id == state_.ClientProgramId()) {
            std::string mods;
            std::vector<layeredfs::Layer> layers = GameLayers(state_.user_folder, program_id, &mods);
            *archive = std::make_unique<SelfNcchArchive>(state_.game.romfs, state_.game.update_romfs,
                                                              state_.game.icon, std::move(layers), std::move(mods));
        } else {
            *archive = std::make_unique<SelfNcchArchive>(nullptr, nullptr, nullptr);
        }
        return kResultSuccess;
    }

private:
    FsState& state_;
};

// A file of a RomFS (level 3, from its header) by its path: where its data is and how long
bool FindInRomFs(FileBackend& image, const std::string& path, u64* offset, u64* size) {
    auto read = [&image](u64 at, void* out, std::size_t length) {
        std::size_t done = 0;
        while (done < length) {
            std::size_t n = 0;
            if (IsError(image.Read(at + done, length - done, static_cast<u8*>(out) + done, &n)) || n == 0) return false;
            done += n;
        }
        return true;
    };
    auto le32 = [](const u8* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; };
    u8 header[0x28];
    if (!read(0, header, sizeof header) || le32(header) != 0x28) return false;
    const u32 dir_meta = le32(header + 12), dir_size = le32(header + 16), file_meta = le32(header + 28),
              file_size = le32(header + 32), data = le32(header + 36);
    if (dir_size > (16u << 20) || file_size > (64u << 20)) return false;
    std::vector<u8> dirs(dir_size), files(file_size);
    if (!read(dir_meta, dirs.data(), dir_size) || !read(file_meta, files.data(), file_size)) return false;
    auto named = [](const u8* name, u32 bytes, const std::string& want) {  // (the RomFS's UTF-16 against ASCII)
        if (bytes != 2 * want.size()) return false;
        for (std::size_t i = 0; i < want.size(); ++i)
            if (name[2 * i] != static_cast<u8>(want[i]) || name[2 * i + 1] != 0) return false;
        return true;
    };
    u32 dir = 0;  // (the root)
    std::size_t from = 0;
    for (int depth = 0; depth < 64; ++depth) {
        const std::size_t slash = path.find('/', from);
        const std::string part = path.substr(from, slash == std::string::npos ? std::string::npos : slash - from);
        if (dir + 24 > dir_size) return false;
        if (slash == std::string::npos) {  // (the file, in this directory's list)
            for (u32 e = le32(dirs.data() + dir + 12), n = 0; e != 0xffffffffu && n < 1000000; ++n) {
                if (e + 32 > file_size || e + 32 + le32(files.data() + e + 28) > file_size) return false;
                if (named(files.data() + e + 32, le32(files.data() + e + 28), part)) {
                    *offset = data + (le32(files.data() + e + 8) | u64(le32(files.data() + e + 12)) << 32);
                    *size = le32(files.data() + e + 16) | u64(le32(files.data() + e + 20)) << 32;
                    return true;
                }
                e = le32(files.data() + e + 4);
            }
            return false;
        }
        bool found = false;  // (a directory, among this one's children)
        for (u32 c = le32(dirs.data() + dir + 8), n = 0; c != 0xffffffffu && n < 1000000; ++n) {
            if (c + 24 > dir_size || c + 24 + le32(dirs.data() + c + 20) > dir_size) return false;
            if (named(dirs.data() + c + 24, le32(dirs.data() + c + 20), part)) {
                dir = c, found = true;
                break;
            }
            c = le32(dirs.data() + c + 4);
        }
        if (!found) return false;
        from = slash + 1;
    }
    return false;
}

MH4N_REGISTER(ArchiveProvider, "03.selfncch", ArchiveProvider{archive_id::kSelfNCCH, [](FsState& state) {
                                                                  return std::make_unique<SelfNcchFactory>(
                                                                      state);
                                                              }});

}  // namespace

std::size_t GameRomFsFile::Read(u64 at, void* buffer, std::size_t length) const {
    if (image == nullptr || at >= size) return 0;
    std::size_t done = 0, n = 0;
    length = static_cast<std::size_t>(std::min<u64>(length, size - at));
    while (done < length && !IsError(image->Read(offset + at + done, length - done, static_cast<u8*>(buffer) + done, &n)) &&
           n > 0) {
        done += n;
    }
    return done;
}

bool OpenGameRomFsFile(const std::string& path, GameRomFsFile* file, std::string* why) {
    std::shared_ptr<PositionedFile> romfs, update_romfs;
    std::string user_folder;
    u64 program_id = 0;
    {
        std::lock_guard lock(g_game_mutex);
        if (g_game_state == nullptr) return *why = "no game runs", false;
        romfs = g_game_state->game.romfs, update_romfs = g_game_state->game.update_romfs;
        user_folder = g_game_state->user_folder, program_id = g_game_state->ClientProgramId();
    }
    std::string mods;
    const std::vector<layeredfs::Layer> layers = GameLayers(user_folder, program_id, &mods);
    bool any = false;
    for (const bool update : {true, false}) {  // (the update's first, as the game reads its files)
        std::shared_ptr<FileBackend> image =
            ImageWithMods(update ? update_romfs : romfs, update ? "update RomFS" : "RomFS", layers, mods);
        if (image == nullptr) continue;
        any = true;
        u64 offset = 0, size = 0;
        if (FindInRomFs(*image, path, &offset, &size) && offset + size <= image->GetSize()) {
            *file = GameRomFsFile{std::move(image), offset, size, update};
            return true;
        }
    }
    return *why = any ? "no " + path + " in the update's RomFS or the game's" : "the game has no RomFS", false;
}

}  // namespace mh4n::fs
