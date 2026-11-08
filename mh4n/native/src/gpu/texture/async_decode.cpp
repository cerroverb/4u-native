#include "gpu/texture/async_decode.h"

#include <algorithm>
#include <utility>

#define XXH_INLINE_ALL
#include <xxhash.h>

#include "gpu/texture/decode.h"
#include "mh4n/host_thread_name.h"

namespace mh4n::gpu::texture {

AsyncDecoder::AsyncDecoder() {
    // two threads: a load's textures in parallel with the renderer and with each other, a phone's cores to spare
    const unsigned n = std::clamp(std::thread::hardware_concurrency() / 4, 1u, 2u);
    for (unsigned i = 0; i < n; ++i) {
        threads_.emplace_back([this] {
            SetHostThreadName("mh4n-texload");
            Run();
        });
    }
}

AsyncDecoder::~AsyncDecoder() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    work_.notify_all();
    for (std::thread& t : threads_) {
        t.join();
    }
}

bool AsyncDecoder::Start(const CacheKey& key, const u8* bytes, std::size_t size) {
    std::lock_guard lock(mutex_);
    if (jobs_.count(key) != 0) {
        return false;
    }
    jobs_.emplace(key, std::nullopt);
    queue_.push_back(Job{key, std::vector<u8>(bytes, bytes + size)});
    work_.notify_one();
    return true;
}

bool AsyncDecoder::Pending(const CacheKey& key) {
    std::lock_guard lock(mutex_);
    return jobs_.count(key) != 0;
}

std::optional<AsyncDecoder::Result> AsyncDecoder::Take(const CacheKey& key) {
    std::lock_guard lock(mutex_);
    auto it = jobs_.find(key);
    if (it == jobs_.end() || !it->second.has_value()) {
        return std::nullopt;
    }
    std::optional<Result> result = std::move(it->second);
    jobs_.erase(it);
    return result;
}

void AsyncDecoder::Run() {
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            work_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) {
                return;
            }
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        Result result;
        result.hash = XXH3_64bits(job.bytes.data(), job.bytes.size());
        if (!DecodeToRgba8(job.bytes.data(), job.bytes.size(), job.key.width, job.key.height, job.key.format, result.rgba8)) {
            result.rgba8.clear();
        }
        std::lock_guard lock(mutex_);
        auto it = jobs_.find(job.key);
        if (it != jobs_.end()) {
            it->second = std::move(result);
        }
    }
}

}  // namespace mh4n::gpu::texture
