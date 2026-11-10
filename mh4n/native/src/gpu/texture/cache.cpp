#include "gpu/texture/cache.h"

#include <algorithm>
#include <string>

// XXH3 is vendored (native/third_party/xxhash, BSD-2-Clause) and used only here, as the cache's
// own content hash; the custom-texture hashes are CityHash64 (hash.h).
#define XXH_INLINE_ALL
#include <xxhash.h>

#include "gpu/texture/decode.h"
#include "mh4n/log.h"

namespace mh4n::gpu::texture {

namespace {

constexpr std::size_t kPieces = 16, kPiece = 32;
// A texture of up to this many bytes is hashed whole by the spot check (and has no slices).
constexpr std::size_t kWhole = kPieces * kPiece * 2;

// The spot check's hash: the whole of a small texture, else 16 pieces of 32 bytes spread over it.
u64 SpotHash(const u8* memory, std::size_t bytes) {
    if (bytes <= kWhole) {
        return XXH3_64bits(memory, bytes);
    }
    u64 h = 0;
    for (std::size_t i = 0; i < kPieces; ++i) {
        h = XXH3_64bits_withSeed(memory + i * (bytes - kPiece) / (kPieces - 1), kPiece, h);
    }
    return h;
}

// Slice i of a texture of `bytes` bytes is [SliceBegin(bytes, i), SliceBegin(bytes, i + 1)).
std::size_t SliceBegin(std::size_t bytes, std::size_t i) {
    return bytes * i / kSlices;
}

}  // namespace

u64 ContentHash(const u8* memory, std::size_t bytes, std::array<u64, kSlices>* slices) {
    if (bytes <= kWhole) {
        if (slices != nullptr) {
            slices->fill(0);
        }
        return XXH3_64bits(memory, bytes);
    }
    std::array<u64, kSlices> h;
    for (std::size_t i = 0; i < kSlices; ++i) {
        const std::size_t begin = SliceBegin(bytes, i), end = SliceBegin(bytes, i + 1);
        h[i] = XXH3_64bits(memory + begin, end - begin);
    }
    if (slices != nullptr) {
        *slices = h;
    }
    return XXH3_64bits(h.data(), sizeof h);
}

bool TextureCache::Decode(CachedTexture& texture, const u8* memory, std::size_t bytes) {
    pixel_bytes_ -= texture.rgba8.size();
    const bool ok = DecodeToRgba8(memory, bytes, texture.key.width, texture.key.height, texture.key.format, texture.rgba8);
    if (!ok) {
        std::vector<u8>().swap(texture.rgba8);
    }
    pixel_bytes_ += texture.rgba8.size();
    texture.decoded_frame = frame_;
    return ok;
}

void TextureCache::SetPixels(CachedTexture& texture, std::vector<u8>&& rgba8) {
    pixel_bytes_ -= texture.rgba8.size();
    texture.rgba8 = std::move(rgba8);
    pixel_bytes_ += texture.rgba8.size();
    texture.decoded_frame = frame_;
}

void TextureCache::Forget(std::map<CacheKey, CachedTexture>::iterator it) {
    pixel_bytes_ -= it->second.rgba8.size();
    entries_.erase(it);
}

const CachedTexture* TextureCache::Get(const CacheKey& key) {
    if (!IsValidSize(key.width, key.height) || !IsValidFormat(static_cast<u32>(key.format))) {
        ++stats_.failures;
        return nullptr;
    }
    const std::size_t bytes = EncodedBytes(key.format, key.width, key.height);

    auto it = entries_.find(key);
    bool checked_now = false;  // (verify mode: whether a stale hit is one the spot check missed just now)
    if (it != entries_.end() && !it->second.maybe_changed && config_.spot_check && it->second.spot_frame != frame_) {
        CachedTexture& texture = it->second;
        texture.spot_frame = frame_;
        checked_now = true;
        const u8* memory = host_.PhysicalPointer(key.paddr, static_cast<u32>(bytes));
        if (memory != nullptr) {
            bool changed = SpotHash(memory, bytes) != texture.spot_hash;
            stats_.spot_bytes += bytes <= kWhole ? bytes : kPieces * kPiece;
            std::size_t slice = kSlices;  // (none)
            if (!changed && config_.rolling_check && bytes > kWhole) {
                slice = texture.roll;
                texture.roll = static_cast<u32>((slice + 1) % kSlices);
                const std::size_t begin = SliceBegin(bytes, slice), end = SliceBegin(bytes, slice + 1);
                stats_.spot_bytes += end - begin;
                changed = XXH3_64bits(memory + begin, end - begin) != texture.slices[slice];
            }
            if (changed) {
                const bool rolled = slice < kSlices;
                stats_.rolled += rolled ? 1 : 0;
                if (++stats_.spotted <= 10) {  // (in a shared log: the first few)
                    MH4N_LOG(Info, "GPU.Texture", "texture {:08x} {}x{} format {} written with no flush: decoded again ({})",
                             key.paddr, key.width, key.height, static_cast<u32>(key.format),
                             rolled ? fmt::format("the rolling check, slice {} of {}", slice + 1, kSlices)
                                    : std::string("the spot check"));
                }
                texture.maybe_changed = true;  // (hashed whole and decoded again below if it differs)
            }
        }
    }
    if (it != entries_.end() && !it->second.maybe_changed) {
        if (config_.verify) {
            const u8* memory = host_.PhysicalPointer(key.paddr, static_cast<u32>(bytes));
            if (memory != nullptr && ContentHash(memory, bytes) != it->second.content_hash) {
                if (++stats_.stale <= 40) {
                    MH4N_LOG(Warning, "GPU.Texture", "stale texture {:08x} {}x{} format {}: its memory changed with no notification ({})",
                             key.paddr, key.width, key.height, static_cast<u32>(key.format),
                             !config_.spot_check ? "no spot check"
                             : checked_now       ? "the spot check missed it at its first use in this frame"
                                                 : "written after its first use in this frame");
                }
                it->second.maybe_changed = true;  // (decoded again below)
            }
        }
    }
    if (it != entries_.end() && !it->second.maybe_changed) {
        ++stats_.hits;
        it->second.used_frame = frame_;
        return &it->second;
    }

    const u8* memory = host_.PhysicalPointer(key.paddr, static_cast<u32>(bytes));
    if (memory == nullptr) {
        ++stats_.failures;
        if (it != entries_.end()) {
            Forget(it); // its memory is gone; do not keep a copy nobody can validate
        }
        return nullptr;
    }
    std::array<u64, kSlices> slices;
    const u64 hash = ContentHash(memory, bytes, &slices);

    if (it == entries_.end()) {
        CachedTexture texture;
        texture.key = key;
        texture.content_hash = hash;
        texture.slices = slices;
        texture.spot_hash = SpotHash(memory, bytes);
        texture.spot_frame = frame_;
        texture.used_frame = frame_;
        if (!Decode(texture, memory, bytes)) {
            ++stats_.failures;
            return nullptr;
        }
        texture.generation = ++next_generation_;
        ++stats_.misses;
        max_bytes_ = std::max(max_bytes_, static_cast<u32>(bytes));
        return &entries_.emplace(key, std::move(texture)).first->second;
    }

    CachedTexture& texture = it->second;
    ++stats_.rehashes;
    texture.maybe_changed = false;
    texture.spot_hash = SpotHash(memory, bytes);
    texture.spot_frame = frame_;
    texture.used_frame = frame_;
    texture.slices = slices;
    if (hash == texture.content_hash) {
        ++stats_.unchanged;
        return &texture;
    }
    if (!Decode(texture, memory, bytes)) {
        ++stats_.failures;
        Forget(it);
        return nullptr;
    }
    texture.content_hash = hash;
    texture.generation = ++next_generation_;
    texture.changed_once = true;
    ++stats_.redecodes;
    return &texture;
}

const CachedTexture* TextureCache::Pixels(const CacheKey& key) {
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        return Get(key);
    }
    CachedTexture& texture = it->second;
    if (!texture.rgba8.empty()) {
        return &texture;
    }
    const std::size_t bytes = EncodedBytes(key.format, key.width, key.height);
    const u8* memory = host_.PhysicalPointer(key.paddr, static_cast<u32>(bytes));
    if (memory == nullptr) {
        ++stats_.failures;
        Forget(it);
        return nullptr;
    }
    std::array<u64, kSlices> slices;
    const u64 hash = ContentHash(memory, bytes, &slices);
    if (!Decode(texture, memory, bytes)) {
        ++stats_.failures;
        Forget(it);
        return nullptr;
    }
    ++stats_.repixels;
    if (hash != texture.content_hash) {  // (changed since its last lookup: other contents)
        texture.content_hash = hash;
        texture.slices = slices;
        texture.spot_hash = SpotHash(memory, bytes);
        texture.generation = ++next_generation_;
    }
    return &texture;
}

void TextureCache::ReleasePixels(const CacheKey& key) {
    if (!config_.release_pixels) {
        return;
    }
    auto it = entries_.find(key);
    if (it == entries_.end() || it->second.changed_once) {
        return;  // (one whose contents change: Sweep, once it settles)
    }
    pixel_bytes_ -= it->second.rgba8.size();
    std::vector<u8>().swap(it->second.rgba8);
}

size_t TextureCache::Sweep(u64 unused_frames, u64 settled_frames) {
    size_t gone = 0;
    for (auto it = entries_.begin(); it != entries_.end();) {
        CachedTexture& texture = it->second;
        if (frame_ - texture.used_frame > unused_frames) {
            Forget(it++);
            ++gone;
            continue;
        }
        if (config_.release_pixels && texture.changed_once && frame_ - texture.decoded_frame > settled_frames) {
            pixel_bytes_ -= texture.rgba8.size();
            std::vector<u8>().swap(texture.rgba8);
            texture.changed_once = false;  // (settled: let go at once again after its next upload)
        }
        ++it;
    }
    stats_.swept += gone;
    return gone;
}

bool TextureCache::Insert(const CacheKey& key, u64 hash, std::vector<u8>&& rgba8) {
    if (rgba8.empty() || entries_.count(key) != 0 || !IsValidSize(key.width, key.height) ||
        !IsValidFormat(static_cast<u32>(key.format))) {
        return false;
    }
    const std::size_t bytes = EncodedBytes(key.format, key.width, key.height);
    const u8* memory = host_.PhysicalPointer(key.paddr, static_cast<u32>(bytes));
    std::array<u64, kSlices> slices;
    if (memory == nullptr || ContentHash(memory, bytes, &slices) != hash) {
        return false;  // (its memory changed since the copy: Get decodes what it holds now)
    }
    CachedTexture texture;
    texture.key = key;
    texture.content_hash = hash;
    texture.slices = slices;
    texture.spot_hash = SpotHash(memory, bytes);
    texture.spot_frame = frame_;
    texture.used_frame = frame_;
    texture.generation = ++next_generation_;
    SetPixels(texture, std::move(rgba8));
    ++stats_.misses;
    max_bytes_ = std::max(max_bytes_, static_cast<u32>(bytes));
    entries_.emplace(key, std::move(texture));
    return true;
}

void TextureCache::Notify(u32 paddr, u32 size) {
    if (config_.check_all_on_notify) {
        for (auto& [key, texture] : entries_) {
            texture.maybe_changed = true;
        }
        return;
    }
    // (the entries are in address order: only those that begin less than the largest texture's bytes before the range can
    // reach into it; every entry was looked at for each notification before, and each new area adds hundreds)
    const u64 begin = paddr;
    const u64 end = begin + size;
    const u32 from = paddr > max_bytes_ ? paddr - max_bytes_ : 0;
    for (auto it = entries_.lower_bound(CacheKey{from, Format::RGBA8, 0, 0}); it != entries_.end() && it->first.paddr < end;
         ++it) {
        const u64 texture_begin = it->first.paddr;
        const u64 texture_end = texture_begin + EncodedBytes(it->first.format, it->first.width, it->first.height);
        if (begin < texture_end && texture_begin < end) {
            it->second.maybe_changed = true;
        }
    }
}

} // namespace mh4n::gpu::texture
