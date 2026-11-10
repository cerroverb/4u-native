// Textures decoded on a thread of their own (the renderer's texture budget, native_gpu.cpp): an area's load brings dozens
// of new textures in one frame, and decoding them all on the renderer thread held that frame up for 40-160 ms. Past the
// frame's budget a new texture's bytes are copied and decoded here; the draws that need it are left out meanwhile (the
// last whole picture stands in, as for a pipeline not built yet), and the cache takes the result when its memory still
// holds the same bytes (TextureCache::Insert).
#pragma once

#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "gpu/texture/cache.h"

namespace mh4n::gpu::texture {

class AsyncDecoder {
public:
    struct Result {
        u64 hash = 0;            // ContentHash of the bytes it was decoded from
        std::vector<u8> rgba8;   // empty: the bytes did not decode
    };

    AsyncDecoder();
    ~AsyncDecoder();
    AsyncDecoder(const AsyncDecoder&) = delete;
    AsyncDecoder& operator=(const AsyncDecoder&) = delete;

    // Decodes a copy of `bytes` (the texture's encoded memory) for `key`; false when one is under way for it already.
    bool Start(const CacheKey& key, const u8* bytes, std::size_t size);
    // A decode started for `key` and not taken yet (finished or not).
    bool Pending(const CacheKey& key);
    // The finished decode of `key`, taken out; nothing while it runs or when none was started.
    std::optional<Result> Take(const CacheKey& key);

private:
    struct Job {
        CacheKey key;
        std::vector<u8> bytes;
    };
    void Run();

    std::mutex mutex_;
    std::condition_variable work_;
    std::deque<Job> queue_;
    std::map<CacheKey, std::optional<Result>> jobs_;  // started; the result once done
    bool stop_ = false;
    std::vector<std::thread> threads_;
};

}  // namespace mh4n::gpu::texture
