// HD texture packs (1.0's S26): the community's packs for the emulators (Azahar's and Citra's layout and hashes: a folder
// of tex1_<w>x<h>_<hash>_<format>.png files, with or without a pack.json) drawn in place of the game's own textures.
//
// The pack is read in place from the player's folder (the app passes it, MH4N_TEXTURE_PACKS; otherwise <--user>/load/
// textures/, Azahar's own place): each folder under it with texture files is one pack (load/textures/<title id>/ as Azahar
// has them, or the pack's own folder), read by Azahar's rules (hdpack/pack.h, ScanPack: the file names, pack.json's options
// and its "textures" map). A texture's custom hash is the one Azahar names its files by (gpu/texture/hash.h: the old hash
// of the decoded texture, or the new one of its bytes when the pack's pack.json says use_new_hash), taken once for each
// content the texture cache sees, and a texture the pack has gets an image of the pack file's size with its pixels.
//
// Only PNG and KTX2 colour maps are drawn (most packs are PNG; DDS and KTX files, normal maps and mip files are counted in
// the log and left out). A .ktx2 (1.0.2, S9b: ktx2.h) wins over the .png of the same map: Basis Universal's ETC1S and
// UASTC and files of RGBA8 or BC7, drawn as BC7 where the device samples it and the file's rows run bottom-up, else as
// RGBA8; its BC7 image takes a quarter of the memory, and counts so in the pack's share. A PNG is read and decoded when its texture is first drawn, on two threads of their own (TakePixels): the
// frame never waits for it. Until its pixels are ready the texture is the game's own, and the HD image is made once they
// are (at most kUploadBytesPerPicture of them a screen picture, the renderer's call), so a pack loads as the areas are
// visited. On
// 1.0 the renderer's thread read and decoded each file as it was first drawn: with a whole pack, a black screen for a
// while at the start and about 0.5 FPS entering the village (4u-native #56). Nor are the files opened at the start: a
// file's size comes from its header when its texture is first found. The pixels go once the image holds them, and ready
// pixels not taken for a few seconds (the texture no longer drawn) are let go. The GPU memory the HD images take is the
// player's call: the log says it as it grows ("[mem] HD textures"), and the images count in the renderer's texture budget
// like the game's own, which lets go of the least used past it.
//
// Orientation: the texture cache's pixels (DecodeToRgba8) are the image upright, its top row first, as Azahar's dumps
// write their PNGs; flip_png_files (true without a pack.json) means the files are so, false that their rows are bottom-up.
// The first texture of a decoded format checks that against the hash's own decode (rows bottom-up) and logs it.
//
//   MH4N_TEXTURE_PACKS=<dir>   the folder of the packs (the app: the game folder's textures/)
//   MH4N_HD_TEXTURES=0         no pack is read
//   MH4N_HD_DUMP=<dir>         (debug) each texture the game draws, once, as <dir>/tex1_<w>x<h>_<old hash>_<format>.png,
//                              upright: a pack of the game's own textures, for tests
//   MH4N_HD_SYNC=1|0           (a test aid) each file read and decoded when its texture is first drawn, the frame waiting
//                              for it, as 1.0 did (a --deterministic run does so too) | on the loader's threads always
//   MH4N_HD_SCALE=half         each file drawn at half its width and height (a quarter of the memory), when that is still
//                              at least twice the game's own texture; the app's Texture packs > Pack texture size (off
//                              unless the player chooses it: full size is the pack's own look)
//   MH4N_HD_READY_MB=<n>       the decoded pixels the loader may keep waiting for the renderer (by default by the device's
//                              memory: 32 MB up to 3 GB, 48 MB up to 4.5 GB, 96 MB more; 1.0.1)
//
// Memory (1.0.1, 4u-native #56): the HD images have a share of the renderer's texture budget (native_gpu.cpp HdBudgetBytes,
// five eighths of it, by the device's memory). A new one is made only while they fit in it; past it the texture stays the
// game's own (its file not read) until room is made: the images not drawn for a couple of seconds go for it, the least
// recently drawn first, and those not drawn for a while (an area left behind) go anyway past half the share
// (HdHousekeeping). Drawn again, a file is read again on the loader's threads. Before, a 4x pack's area on a 4 GB phone
// went past the whole budget and its LRU let go of images the next frames drew again: each was decoded again (a Moto g06:
// 1444 decodes of 380 files). The pixels go from the CPU's memory once uploaded (an image keeps none), the images have one
// level (no mipmaps), and the files are read whole into a buffer only while they are decoded.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "gpu/native/ktx2.h"
#include "gpu/texture/formats.h"
#include "mh4n/types.h"

namespace mh4n::gpu::native {

class HdTextures {
public:
    struct Replacement {
        u32 file = 0;  // index into files_
        u32 width = 0, height = 0;
        HdFormat format = HdFormat::Rgba8;  // what TakePixels gives: RGBA8 upright, or BC7 blocks bottom-up (ktx2.h)
        u64 bytes() const { return HdBytes(format, width, height); }
    };

    // Reads the packs under `root` (or none: an empty root, a folder that isn't there). `sync`: each file read and decoded
    // when its texture is first drawn, the frame waiting for it (a --deterministic run draws the same frames every time)
    // `bc7`: the device samples BC7 (a KTX2 file may then be drawn in it: ktx2.h)
    void Load(const std::string& root, bool sync = false, bool bc7 = false);
    bool active() const { return !by_hash_[0].empty() || !by_hash_[1].empty() || !dump_dir_.empty(); }

    // The pack's texture for a texture whose content hash (the texture cache's) is `content` and whose memory is `data`;
    // nullptr when no pack has it. Answered once for each content.
    const Replacement* Find(u64 content, const u8* data, std::size_t size, u32 width, u32 height, texture::Format format);

    // Its pixels, RGBA8 upright as the texture cache's (or BC7 blocks: r.format), read and decoded now. False when the
    // file can't be read.
    bool Pixels(const Replacement& r, std::vector<u8>& rgba);

    // Its pixels from the loader's threads: Ready (moved into `rgba`), Pending (asked for now if they weren't: the texture
    // is the game's own meanwhile) or Failed (the file could not be read: Refuse it). `may_take` false (this frame's
    // uploads are spent): Pending, the pixels kept for a later one. With MH4N_HD_SYNC=1 or Load's `sync`, Pixels now.
    enum class Take { Ready, Pending, Failed };
    Take TakePixels(const Replacement& r, std::vector<u8>& rgba, bool may_take = true);
    // Each file read and decoded on the renderer's thread when first drawn (MH4N_HD_SYNC=1, Load's `sync`), as 1.0
    bool sync() const { return sync_; }
    // What the loader did so far, and the files read on the renderer's thread, for the log ("" when neither)
    std::string LoaderStats() const;
    // Once a frame: ready pixels no texture took for a few seconds are let go (and asked for again if drawn again)
    void Tick();
    // The bytes the pack's new images may upload for one screen picture (two a game frame; the renderer's budget for
    // TakePixels' answers, started again at each display transfer)
    static constexpr u64 kUploadBytesPerPicture = 8ull << 20;

    HdTextures();
    ~HdTextures();
    HdTextures(const HdTextures&) = delete;
    HdTextures& operator=(const HdTextures&) = delete;
    // A file that could not be read or uploaded: its textures are the game's own from now on (logged once)
    void Refuse(const Replacement& r);

    // A texture drawn for the first time (MH4N_HD_DUMP): written as a PNG of the pack's names, with its decoded pixels.
    void Dump(u64 content, const u8* data, std::size_t size, u32 width, u32 height, texture::Format format,
              const std::vector<u8>& rgba);

    // The images made from the pack: for the log's [mem] line
    void Made(u64 bytes) { ++images_, image_bytes_ += bytes; }
    void Gone(u64 bytes) { --images_, image_bytes_ -= bytes; }
    u64 images() const { return images_; }
    u64 image_bytes() const { return image_bytes_; }
    u64 replaced() const { return replaced_; }

private:
    struct File {
        std::string path;
        bool flip = false;  // the rows are turned over as they are read (the pack's flip_png_files false)
        bool refused = false;  // (Refuse)
        bool sized = false;    // (Sized: its header read)
        bool half = false;     // drawn at half its size (MH4N_HD_SCALE=half: width and height below are the halves)
        bool ktx2 = false;     // a .ktx2 (ktx2.h)
        HdFormat format = HdFormat::Rgba8;  // what its texture gets (a KTX2's: Ktx2Target, when Sized)
        u32 width = 0, height = 0;          // the image's size
        u32 file_width = 0, file_height = 0;  // the PNG's own (its header)
    };
    struct Answer {
        bool found = false;
        Replacement r;
    };
    void CheckOrientation(const u8* data, std::size_t size, u32 width, u32 height, texture::Format format,
                          const std::vector<u8>* rgba);

    struct Loader;  // (hd_textures.cpp)
    // A file's size from its PNG header, read the first time its texture is found (false: unreadable); with
    // MH4N_HD_SCALE=half, halved when that is still twice the game's texture (`game_width` x `game_height`) or more
    bool Sized(File& f, u32 game_width, u32 game_height);

    std::unique_ptr<Loader> loader_;  // made by the first TakePixels
    bool sync_ = false;               // (Load's `sync`, or MH4N_HD_SYNC=1)
    bool half_ = false;               // (MH4N_HD_SCALE=half)
    bool bc7_ = false;                // (Load's `bc7`)
    u64 ktx2_bc7_ = 0, ktx2_rgba8_ = 0;  // the KTX2 files sized so far, by what their textures get
    u64 halved_ = 0;                  // the files drawn at half their size
    std::vector<File> files_;
    std::unordered_map<u64, u32> by_hash_[2];  // old hash, new hash -> file
    std::unordered_map<u64, Answer> answers_;  // by content hash (and size, format)
    std::string dump_dir_;
    std::unordered_map<u64, bool> dumped_;
    bool oriented_ = false;
    u64 images_ = 0, image_bytes_ = 0, replaced_ = 0, lookups_ = 0;
    u64 sync_reads_ = 0;    // files read and decoded by Pixels, on the caller's thread (the renderer's)
    double sync_ms_ = 0.0;  // and the time they took there
};

}  // namespace mh4n::gpu::native
