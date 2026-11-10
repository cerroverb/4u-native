// The texture cache (N2-14): decoded textures keyed by physical address, format and size, kept
// valid by a content hash.
//
// Principle 2 of docs/native/ROADMAP.md: the CPU writes memory the GPU reads, and the game tells
// the GPU so with a cache flush or invalidate. A cached texture whose memory such a notification
// touches may have changed, so the next lookup hashes the memory again (XXH3, 64 bits) and
// decodes it again only if the hash differs. A texture nobody touched is returned without
// reading its memory.
//
// The renderer marks only the textures a notification's range touches (native_gpu.cpp; the
// CPU writers it hears of: the game's flushes through gsp::Gpu, the GX commands, the camera and
// y2r services, and the file reads of fs, whose bytes the game never flushes). `check_all_on_notify`
// (MH4N_TEXTURE_CHECK_ALL=1) marks every cached texture on any notification instead, as before. The
// game also writes some textures with no flush at all: the spot check (Config::spot_check) looks at a
// few pieces of each texture's memory at its first hit in a frame and catches those, and the rolling
// check (Config::rolling_check) one 32nd of it more, a different one each frame, so a change too
// small for the pieces is caught within 32 frames; `verify` (MH4N_TEXTURE_VERIFY=1) hashes every hit
// again and says which texture a missing notification left stale.
//
// This is the CPU side. The GPU-side copy of a texture (an image on the backend) is N2-27's and
// N2-15's; they use `generation` to know when to upload again. Once an image holds a texture, its
// decoded copy here goes (ReleasePixels) and comes back only when an image must be made again
// (Pixels); entries no draw used for long go too (Sweep). Before, every texture a session ever drew
// stayed here decoded, as much memory again as its images.
#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <tuple>
#include <vector>

#include "gpu/texture/formats.h"
#include "mh4n/gpu.h"

namespace mh4n::gpu::texture {

struct CacheKey {
    u32 paddr = 0;
    Format format = Format::RGBA8;
    u32 width = 0;
    u32 height = 0;

    friend bool operator==(const CacheKey&, const CacheKey&) = default;
    friend bool operator<(const CacheKey& a, const CacheKey& b) {
        return std::tie(a.paddr, a.format, a.width, a.height) <
               std::tie(b.paddr, b.format, b.width, b.height);
    }
};

// The rolling check's slices: a texture bigger than the spot check hashes whole is cut in this many, and each first hit in a
// frame hashes the next one, so that every byte of it is looked at again within kSlices frames (half a second at two
// pictures a game frame, 30 a second). 16 hashed twice the bytes for a quarter of a second less: the steppe's textures are
// about 3.5 MB, 110 KB a frame with 32 slices.
constexpr std::size_t kSlices = 32;

struct CachedTexture {
    CacheKey key;
    u64 content_hash = 0;       // ContentHash of the texture's bytes in memory
    // A number from the cache's own count, new each time the texture is decoded from contents it had not been decoded from
    // (the first time, and after a change): two contents of one key never share it, even when the cache let the entry go
    // and made it again, so an image that holds an older one is always uploaded again.
    u32 generation = 0;
    // width * height * 4, decoded by DecodeToRgba8. Empty once the texture's image holds it (ReleasePixels): the GPU's copy
    // is the one that is drawn, and Pixels decodes it again when an image must be made again.
    std::vector<u8> rgba8;
    bool maybe_changed = false; // a flush or invalidate touched it since the last lookup
    u64 spot_hash = 0;          // SpotHash of its bytes (the spot check)
    u64 spot_frame = 0;         // the frame its spot check last ran
    u64 used_frame = 0;         // the frame of its last lookup (Sweep)
    u64 decoded_frame = 0;      // the frame it was last decoded
    bool changed_once = false;  // decoded again after a change (a video's frames): its pixels go only once it settles (Sweep)
    u32 roll = 0;               // the rolling check's next slice
    std::array<u64, kSlices> slices{};  // the slices' hashes (a texture bigger than the spot check hashes whole)
};

struct CacheStats {
    u64 hits = 0;       // returned without reading memory
    u64 misses = 0;     // decoded for the first time
    u64 rehashes = 0;   // hashed again because of a notification
    u64 unchanged = 0;  // ... and the hash was the same
    u64 redecodes = 0;  // ... and it differed
    u64 failures = 0;   // memory unmapped, or a size that is not a whole number of tiles
    u64 stale = 0;      // verify mode: a hit whose memory had changed with no notification
    u64 spotted = 0;    // the spot check found a hit's memory changed with no notification
    u64 rolled = 0;     // ... of them, the rolling check (a change none of the 16 pieces saw)
    u64 repixels = 0;   // pixels decoded again for an image made again (Pixels)
    u64 swept = 0;      // entries let go after a long time unused (Sweep)
    u64 spot_bytes = 0; // the bytes the spot and rolling checks hashed
};

// The content hash of a texture's bytes: XXH3 of all of them, or for one bigger than the spot check hashes whole, XXH3 of
// the kSlices slices' XXH3s (written to `slices`, the rolling check's), the same bytes hashed once. AsyncDecoder's too.
u64 ContentHash(const u8* memory, std::size_t bytes, std::array<u64, kSlices>* slices = nullptr);

class TextureCache {
public:
    struct Config {
        bool check_all_on_notify = true;
        // Every hit hashes the memory again (MH4N_TEXTURE_VERIFY=1, a measurement aid): a texture whose memory changed
        // with no flush, invalidate or GPU write naming it is counted (`stale`), told in the log (the first 40) and
        // decoded again, so a missing notification shows instead of an old picture.
        bool verify = false;
        // The spot check (on by default; MH4N_TEXTURE_SPOT=0 turns it off): the first hit of a texture in a frame
        // (NextFrame) hashes 16 small pieces of its memory, and a change there decodes it again. The game writes some
        // textures with the CPU and no flush at all (a clothing type chosen in character creation: its 128x128 ETC1A4 went
        // into the place of the type before's, 4u-native #18), which the console's GPU sees once its data cache lets the
        // lines go; a whole new texture changes those pieces.
        bool spot_check = true;
        // The rolling check (with the spot check; MH4N_TEXTURE_ROLL=0 turns it off): the same first hit also hashes the
        // next of the texture's kSlices slices, so a change too small for the 16 pieces (a few texels, a corner) is
        // caught within kSlices frames. A 128x128 ETC1A4 hashes 512 bytes a frame for it.
        bool rolling_check = true;
        // The decoded copy is let go once the texture's image holds it (ReleasePixels; MH4N_TEXTURE_KEEP_PIXELS=1 keeps
        // it, as before). Kept, every texture an area ever drew stayed in memory twice, as pixels here and as an image.
        bool release_pixels = true;
    };

    explicit TextureCache(gpu::Host& host) : host_(host) {}
    TextureCache(gpu::Host& host, Config config) : host_(host), config_(config) {}

    // The decoded texture, valid until the next call that changes the cache; nullptr when its
    // memory is not mapped or its size is not valid.
    const CachedTexture* Get(const CacheKey& key);

    // The CPU wrote [paddr, paddr + size) (FlushDataCache), the CPU is about to read it
    // (InvalidateDataCache), or the GPU wrote it (a fill, a transfer, a draw into it). Physical
    // addresses.
    void OnFlush(u32 paddr, u32 size) {
        Notify(paddr, size);
    }
    void OnInvalidate(u32 paddr, u32 size) {
        Notify(paddr, size);
    }
    void OnGpuWrite(u32 paddr, u32 size) {
        Notify(paddr, size);
    }

    // Whether the texture has been decoded before (changed since or not).
    bool Has(const CacheKey& key) const {
        return entries_.count(key) != 0;
    }
    // A texture decoded elsewhere (AsyncDecoder) from bytes whose XXH3 is `hash`: kept, as Get would have decoded it,
    // when its memory still holds those bytes; false otherwise (Get decodes it then).
    bool Insert(const CacheKey& key, u64 hash, std::vector<u8>&& rgba8);

    // The texture's decoded pixels, for an image made again (or made for another view of the same memory): decoded from
    // memory when ReleasePixels let them go, and then a new generation if the memory changed since. nullptr as Get.
    const CachedTexture* Pixels(const CacheKey& key);
    // The texture's image holds its pixels now: the decoded copy goes (Config::release_pixels), at once for a texture
    // decoded once, by Sweep for one whose contents change (a video's: decoded again every frame into the same buffer).
    void ReleasePixels(const CacheKey& key);

    // A new frame: each texture's first hit in it runs the spot check again.
    void NextFrame() {
        ++frame_;
    }
    u64 frame() const {
        return frame_;
    }
    // Lets go of the entries no lookup asked for in the last `unused_frames` frames (their memory is likely another
    // texture's by now; one that is drawn again is decoded again, with a new generation), and of the pixels of the textures
    // that changed once and not in the last `settled_frames` frames. Returns how many entries went.
    size_t Sweep(u64 unused_frames, u64 settled_frames);

    void Clear() {
        entries_.clear();
        pixel_bytes_ = 0;
    }
    size_t size() const {
        return entries_.size();
    }
    // The decoded pixels the cache holds, in bytes.
    u64 pixel_bytes() const {
        return pixel_bytes_;
    }
    const CacheStats& stats() const {
        return stats_;
    }
    const Config& config() const {
        return config_;
    }

private:
    void Notify(u32 paddr, u32 size);
    // Decodes `memory` into the entry's pixels; false when the bytes do not decode.
    bool Decode(CachedTexture& texture, const u8* memory, std::size_t bytes);
    void SetPixels(CachedTexture& texture, std::vector<u8>&& rgba8);
    void Forget(std::map<CacheKey, CachedTexture>::iterator it);

    gpu::Host& host_;
    Config config_;
    std::map<CacheKey, CachedTexture> entries_;  // (in address order: Notify looks only at the entries near its range)
    CacheStats stats_;
    u64 frame_ = 1;
    u32 next_generation_ = 0;
    u64 pixel_bytes_ = 0;
    u32 max_bytes_ = 0;  // the largest texture's bytes in memory so far (how far before a range an entry can begin)
};

} // namespace mh4n::gpu::texture
