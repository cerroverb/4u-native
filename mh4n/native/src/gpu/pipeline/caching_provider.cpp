// Written for N2-16: nothing is copied from Azahar here (see caching_provider.h for what was read).
#include "gpu/pipeline/caching_provider.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <fmt/format.h>

#include "common/utf8.h"
#include "gpu/pipeline/files.h"
#include "gpu/pipeline/key_codec.h"
#include "mh4n/log.h"
#include "mh4n/registry.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#include "mh4n/host_thread_name.h"

namespace mh4n::gpu::pipeline {
namespace {

using native::PipelineKey;
using native::ShaderTextHash;
using Clock = std::chrono::steady_clock;

constexpr const char* kLog = "Pipelines";
constexpr u64 kListFormat = 1;
constexpr u64 kVertexFormat = 1;
constexpr u64 kSpirvFormat = 1;
// Past this many SPIR-V entries the ones no run asked for since the store was read are not written again.
constexpr size_t kSpirvKeep = 4096;

double MsSince(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }

// The workers yield to the renderer and the game's threads.
void LowerThreadPriority() {
#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#elif defined(__linux__)
    setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 5);
#endif
}

void Put32(std::vector<u8>& out, u32 v) {
    for (u32 i = 0; i < 4; ++i) out.push_back(static_cast<u8>(v >> (8 * i)));
}
void Put64(std::vector<u8>& out, u64 v) {
    Put32(out, static_cast<u32>(v));
    Put32(out, static_cast<u32>(v >> 32));
}
u32 Get32(const u8* p) {
    u32 v = 0;
    for (u32 i = 0; i < 4; ++i) v |= static_cast<u32>(p[i]) << (8 * i);
    return v;
}
u64 Get64(const u8* p) { return Get32(p) | (static_cast<u64>(Get32(p + 4)) << 32); }

// The driver's file is named after the device the empty cache's header names (VkPipelineCacheHeaderVersionOne: the
// header's size and version, vendorID, deviceID, pipelineCacheUUID), so two devices of one machine keep a file each.
std::string DriverFileName(const std::vector<u8>& empty_cache) {
    if (empty_cache.size() < 32) {
        return "driver.bin";
    }
    std::string uuid;
    for (u32 i = 16; i < 24; ++i) uuid += fmt::format("{:02x}", empty_cache[i]);
    return fmt::format("driver-{:04x}-{:04x}-{}.bin", Get32(empty_cache.data() + 8), Get32(empty_cache.data() + 12), uuid);
}

// ---- the SPIR-V store ------------------------------------------------------------------------------------------------

class SpirvFileStore final : public native::SpirvStore {
public:
    bool Find(const ShaderTextHash& hash, std::vector<u32>& spirv) override {
        std::lock_guard lock(mutex_);
        const auto it = map_.find(hash);
        if (it == map_.end()) {
            ++misses_;
            return false;
        }
        it->second.used = true;
        spirv = it->second.words;
        ++hits_;
        return true;
    }
    void Put(const ShaderTextHash& hash, std::span<const u32> spirv) override {
        std::lock_guard lock(mutex_);
        Entry& e = map_[hash];
        e.words.assign(spirv.begin(), spirv.end());
        e.used = true;
        dirty_ = true;
    }

    // A record: the hash (a, b, length, stage) and the words.
    void Load(const Records& records) {
        std::lock_guard lock(mutex_);
        for (const auto& r : records) {
            if (r.size() < 24 || (r.size() - 24) % 4 != 0) {
                continue;
            }
            ShaderTextHash h;
            h.a = Get64(r.data());
            h.b = Get64(r.data() + 8);
            h.length = Get32(r.data() + 16);
            h.stage = static_cast<native::ShaderStage>(Get32(r.data() + 20) & 0xFF);
            Entry& e = map_[h];
            e.words.resize((r.size() - 24) / 4);
            for (size_t i = 0; i < e.words.size(); ++i) e.words[i] = Get32(r.data() + 24 + 4 * i);
        }
    }
    bool Dirty() const {
        std::lock_guard lock(mutex_);
        return dirty_;
    }
    Records Snapshot() {
        std::lock_guard lock(mutex_);
        dirty_ = false;
        const bool keep_all = map_.size() <= kSpirvKeep;
        Records out;
        out.reserve(map_.size());
        for (const auto& [h, e] : map_) {
            if (!keep_all && !e.used) {
                continue;
            }
            std::vector<u8> r;
            r.reserve(24 + 4 * e.words.size());
            Put64(r, h.a);
            Put64(r, h.b);
            Put32(r, h.length);
            Put32(r, static_cast<u32>(h.stage));
            for (u32 w : e.words) Put32(r, w);
            out.push_back(std::move(r));
        }
        return out;
    }
    size_t size() const {
        std::lock_guard lock(mutex_);
        return map_.size();
    }
    std::pair<u64, u64> HitsMisses() const {
        std::lock_guard lock(mutex_);
        return {hits_, misses_};
    }

private:
    struct Entry {
        std::vector<u32> words;
        bool used = false;
    };
    mutable std::mutex mutex_;
    std::unordered_map<ShaderTextHash, Entry, native::ShaderTextHashHasher> map_;
    bool dirty_ = false;
    u64 hits_ = 0, misses_ = 0;
};

}  // namespace

// ---- the provider ----------------------------------------------------------------------------------------------------

class CachingProvider final : public native::PipelineProvider {
public:
    CachingProvider(const native::ProviderHost& host, const CachingOptions& options);
    ~CachingProvider() override;
    CachingProvider(const CachingProvider&) = delete;
    CachingProvider& operator=(const CachingProvider&) = delete;

    rhi::Result<native::PipelineHandle> Lookup(const PipelineKey& key) override;
    native::PipelineReadiness Prepare(const PipelineKey& key, const PipelineKey& fallback) override;
    void ReportMiss(const PipelineKey& key, std::string_view reason) override;
    void Hurry(const PipelineKey& key) override;
    native::ProviderCounters Counters() const override;
    std::vector<native::FrameCounter> TakeFrameCounters() override;

    CachingStats Stats() const {
        std::lock_guard lock(mutex_);
        return stats_;
    }
    void WaitIdle() {
        std::unique_lock lock(mutex_);
        done_cv_.wait(lock, [&] { return stop_ || (urgent_.empty() && background_.empty() && building_ == 0); });
    }
    void Save(bool final) { WriteFiles(final); }

private:
    enum class State : u8 { Queued, Building, Ready, Failed };
    struct Entry {
        State state = State::Queued;
        bool urgent = false;  // in the urgent queue
        std::shared_ptr<rhi::Pipeline> pipeline;
        std::string error;
        u32 uses = 0;
        u32 order = 0;
    };
    struct Frame {
        u64 sync_builds = 0, sync_waits = 0, sync_us = 0, async_builds = 0, pending = 0, forced_uber = 0, skipped = 0;
    };
    using Built = rhi::Result<std::shared_ptr<rhi::Pipeline>>;

    // mutex_ held for all of these.
    Entry& Insert(const PipelineKey& key, State state);
    void Enqueue(const PipelineKey& key, Entry& entry, bool urgent);
    void NoteWanted(const PipelineKey& key);  // a miss of the renderer: queued (urgent) unless it is under way
    bool IsReady(const PipelineKey& key) const;
    void QueueFallback(const PipelineKey& fallback);  // at the front of the background queue, unless it is known
    void QueueUrgent(const PipelineKey& key);         // in the urgent queue, unless it is built or being built
    bool FallbackAllowed();                           // one more fallback draw in this 8 ms (fallback_per_window)
    Clock::time_point fallback_window_{};
    u32 fallback_used_ = 0;
    void Finish(Entry& entry, Built& built);
    // Builds on the calling thread, or takes a Queued entry over, or waits for a worker; releases the lock meanwhile.
    Built GetNow(std::unique_lock<std::mutex>& lock, const PipelineKey& key);

    void Worker();
    void Saver();
    void ReportProgress();  // (mutex_ held: the saver's turn)
    void Load();
    void WriteFiles(bool final);

    native::ProviderHost host_;
    CachingOptions options_;
    std::unique_ptr<SpirvFileStore> spirv_;
    std::filesystem::path driver_path_;
    Clock::time_point start_ = Clock::now();

    mutable std::mutex mutex_;
    std::condition_variable work_cv_, done_cv_, saver_cv_;
    std::unordered_map<PipelineKey, Entry, native::PipelineKeyHash> table_;
    PipelineKey last_ready_{};      // Prepare: the last key it found ready (under mutex_)
    bool last_ready_valid_ = false;
    std::deque<PipelineKey> urgent_, background_;
    u32 building_ = 0;
    u32 background_building_ = 0;  // builds taken from the background queue (one worker is kept for the urgent one)
    u32 background_limit_ = 1;
    u32 next_order_ = 0;
    bool stop_ = false;
    bool list_dirty_ = false;
    u64 built_since_blob_ = 0;
    bool precompile_reported_ = true;
    u64 precompile_failures_ = 0;  // keys of the list the driver refused (the line "the list is compiled")
    native::ProviderCounters total_;
    Frame frame_;
    CachingStats stats_;
    // The builds and the draws left out since the saver's last progress line (ReportProgress), and its lines so far.
    struct Progress {
        u64 builds = 0, skipped = 0;
        double ms = 0.0, longest_ms = 0.0;
    } progress_;
    u32 progress_lines_ = 0;
    std::mutex save_mutex_;  // one WriteFiles at a time
    std::vector<std::thread> workers_;
    std::thread saver_;
};

CachingProvider::CachingProvider(const native::ProviderHost& host, const CachingOptions& options)
    : host_(host), options_(options) {
    host_.options = nullptr;  // valid only while the factory ran
    Load();
    u32 workers = options_.workers;
    if (workers == 0) {
        const u32 cores = std::max(1u, std::thread::hardware_concurrency());
        workers = std::clamp(cores / 2, 2u, 5u) - 1;
    }
    background_limit_ = std::max(1u, workers - 1);
    if (options_.background || !background_.empty()) {
        for (u32 i = 0; i < workers; ++i) {
            workers_.emplace_back([this] {
                SetHostThreadName("mh4n-pipeline");
                Worker();
            });
        }
    }
    if (!options_.folder.empty()) {
        saver_ = std::thread([this] {
            SetHostThreadName("mh4n-pipesave");
            Saver();
        });
    }
    MH4N_LOG(Info, kLog, "{} worker{}, {}{}{}", workers_.size(), workers_.size() == 1 ? "" : "s",
             options_.background ? "background compilation behind the ubershader" : "a miss is built on the renderer thread",
             options_.force_ubershader ? ", every draw takes the ubershader" : "", options_.warm ? ", the list compiled first" : "");
    if (options_.warm && !background_.empty()) {
        const auto t0 = Clock::now();
        WaitIdle();
        MH4N_LOG(Info, kLog, "--warm: the list compiled in {:.0f} ms", MsSince(t0));
    }
}

CachingProvider::~CachingProvider() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    work_cv_.notify_all();
    done_cv_.notify_all();
    saver_cv_.notify_all();
    for (std::thread& t : workers_) {
        t.join();
    }
    if (saver_.joinable()) {
        saver_.join();
    }
    WriteFiles(true);
    {
        std::lock_guard lock(mutex_);
        MH4N_LOG(Info, kLog,
                 "{} built on workers ({:.0f} ms), {} on the renderer thread ({:.0f} ms with {} waits), {} failed; {} keys known",
                 stats_.async_builds, stats_.async_ms, stats_.sync_builds, stats_.sync_ms, stats_.sync_waits, stats_.failures,
                 table_.size());
    }
    if (host_.set_spirv_store) {
        host_.set_spirv_store(nullptr);
    }
}

// ---- the table ----

CachingProvider::Entry& CachingProvider::Insert(const PipelineKey& key, State state) {
    Entry& e = table_.try_emplace(key).first->second;
    e.state = state;
    e.order = next_order_++;
    list_dirty_ = true;
    return e;
}

void CachingProvider::Enqueue(const PipelineKey& key, Entry& entry, bool urgent) {
    if (urgent) {
        entry.urgent = true;
        urgent_.push_back(key);
    } else {
        background_.push_back(key);
    }
    work_cv_.notify_one();
}

void CachingProvider::NoteWanted(const PipelineKey& key) {
    const auto it = table_.find(key);
    if (it == table_.end()) {
        Entry& e = Insert(key, State::Queued);
        e.uses = 1;
        Enqueue(key, e, true);
        return;
    }
    Entry& e = it->second;
    ++e.uses;
    if (e.state == State::Queued && !e.urgent) {
        Enqueue(key, e, true);  // from the start-up list to the front
    }
}

bool CachingProvider::IsReady(const PipelineKey& key) const {
    const auto it = table_.find(key);
    return it != table_.end() && it->second.state == State::Ready;
}

bool CachingProvider::FallbackAllowed() {
    if (!options_.use_fallback) {
        return false;
    }
    if (options_.fallback_per_window == 0) {
        return true;
    }
    const Clock::time_point now = Clock::now();
    if (now - fallback_window_ > std::chrono::milliseconds(8)) {
        fallback_window_ = now;
        fallback_used_ = 0;
    }
    if (fallback_used_ >= options_.fallback_per_window) {
        return false;
    }
    ++fallback_used_;
    return true;
}

void CachingProvider::QueueUrgent(const PipelineKey& key) {
    const auto it = table_.find(key);
    if (it == table_.end()) {
        Entry& e = Insert(key, State::Queued);
        e.uses = 1;
        Enqueue(key, e, true);
        return;
    }
    Entry& e = it->second;
    ++e.uses;
    if (e.state == State::Queued && !e.urgent) {
        Enqueue(key, e, true);
    }
}

void CachingProvider::Hurry(const PipelineKey& key) {
    std::lock_guard lock(mutex_);
    const auto it = table_.find(key);
    if (it == table_.end() || it->second.state != State::Queued) {
        return;  // (being built, built, failed or never asked for)
    }
    const auto at = std::find(urgent_.begin(), urgent_.end(), key);
    if (at == urgent_.end() || at == urgent_.begin()) {
        return;
    }
    urgent_.erase(at);
    urgent_.push_front(key);
}

void CachingProvider::QueueFallback(const PipelineKey& fallback) {
    if (table_.find(fallback) != table_.end()) {
        return;  // queued, being built or failed
    }
    Insert(fallback, State::Queued);
    background_.push_front(fallback);
    work_cv_.notify_one();
}

void CachingProvider::Finish(Entry& entry, Built& built) {
    if (built.ok() && *built != nullptr) {
        entry.state = State::Ready;
        entry.pipeline = *built;
    } else {
        entry.state = State::Failed;
        entry.error = built.ok() ? "the build callback returned no pipeline" : built.error();
        ++stats_.failures;
        ++total_.build_failures;
        // (the first 10, then one in 100 with the count: a driver that refuses every pipeline would fill the log)
        if (stats_.failures <= 10 || stats_.failures % 100 == 0) {
            MH4N_LOG(Warning, kLog, "a pipeline could not be built{}: {}",
                     stats_.failures < 10    ? std::string()
                     : stats_.failures == 10 ? std::string(" (10 so far; from now on one line in 100)")
                                             : fmt::format(" ({} so far)", stats_.failures),
                     entry.error);
        }
    }
    ++built_since_blob_;
}

CachingProvider::Built CachingProvider::GetNow(std::unique_lock<std::mutex>& lock, const PipelineKey& key) {
    bool counted = false;
    for (;;) {
        auto it = table_.find(key);
        Entry* e = nullptr;
        if (it == table_.end()) {
            e = &Insert(key, State::Building);
            e->uses = 1;
            counted = true;
        } else {
            e = &it->second;
            if (!counted) {
                ++e->uses;
                counted = true;
            }
            if (e->state == State::Ready) {
                ++total_.hits;
                return e->pipeline;
            }
            if (e->state == State::Failed) {
                return rhi::Error{e->error};
            }
            if (e->state == State::Building) {
                // A worker is building it: wait for that build rather than make it twice.
                const auto t0 = Clock::now();
                done_cv_.wait(lock, [&] { return e->state != State::Building || stop_; });
                const double ms = MsSince(t0);
                ++stats_.sync_waits;
                stats_.sync_ms += ms;
                ++frame_.sync_waits;
                frame_.sync_us += static_cast<u64>(ms * 1000.0);
                if (stop_ && e->state == State::Building) {
                    return rhi::Error{"the pipeline provider is stopping"};
                }
                continue;
            }
            e->state = State::Building;  // Queued: built here, the worker skips it
        }
        ++building_;
        lock.unlock();
        const auto t0 = Clock::now();
        Built built = host_.build(key);
        const double ms = MsSince(t0);
        lock.lock();
        --building_;
        Finish(*e, built);
        ++stats_.sync_builds;
        stats_.sync_ms += ms;
        ++total_.sync_builds;
        ++frame_.sync_builds;
        frame_.sync_us += static_cast<u64>(ms * 1000.0);
        done_cv_.notify_all();
        if (e->state != State::Ready) {
            return rhi::Error{e->error};
        }
        return e->pipeline;
    }
}

// ---- the provider interface (the renderer thread) ----

rhi::Result<native::PipelineHandle> CachingProvider::Lookup(const PipelineKey& key) {
    std::unique_lock lock(mutex_);
    ++total_.lookups;
    native::PipelineHandle handle;
    if (options_.force_ubershader) {
        ++total_.forced_uber;
        ++frame_.forced_uber;
        Built fallback = GetNow(lock, key.AsFallback());
        if (!fallback.ok()) {
            return rhi::Error{fallback.error()};
        }
        handle.pipeline = *fallback;
        handle.fallback = !key.IsFallback();
        return handle;
    }
    const auto it = table_.find(key);
    if (it != table_.end() && it->second.state == State::Ready) {
        ++it->second.uses;
        ++total_.hits;
        handle.pipeline = it->second.pipeline;
        return handle;
    }
    const PipelineKey fallback = key.AsFallback();
    const bool failed = it != table_.end() && it->second.state == State::Failed;
    if (options_.background && options_.never_wait && !failed && !key.IsFallback() &&
        (!IsReady(fallback) || !FallbackAllowed())) {
        // (no fallback, or this frame's few fallback draws are taken: the draw waits for its own pipeline, left out)
        QueueUrgent(key);
        if (!IsReady(fallback)) {
            QueueFallback(fallback);
        }
        ++frame_.skipped, ++progress_.skipped;
        handle.pending = true;
        return handle;
    }
    if (options_.background && !key.IsFallback() && (failed || IsReady(fallback))) {
        // The fallback while the own pipeline is built on a worker (for good when it failed).
        if (!failed) {
            NoteWanted(key);
        } else if (options_.never_wait && !IsReady(fallback)) {  // (not built here either: the urgent queue)
            QueueUrgent(fallback);
            ++frame_.skipped, ++progress_.skipped;
            handle.pending = true;
            return handle;
        }
        Built made = GetNow(lock, fallback);
        if (!made.ok()) {
            return rhi::Error{made.error()};
        }
        handle.pipeline = *made;
        handle.fallback = true;
        return handle;
    }
    // No fallback yet (its first build can take seconds: the ubershader is large). By default the draw is left out: the
    // key's own pipeline goes to the urgent queue (a specialised pipeline builds in tens of milliseconds) and the
    // fallback to the background one, and the renderer thread neither builds nor waits (a fallback key asked for itself,
    // a CPU-path draw's, goes to the urgent queue). With --pipeline-wait the key's own pipeline is built here, once.
    if (options_.background && options_.never_wait && !failed) {
        QueueUrgent(key);
        if (!key.IsFallback()) {
            QueueFallback(fallback);
        }
        ++frame_.skipped, ++progress_.skipped;
        handle.pending = true;
        return handle;
    }
    if (options_.background && !key.IsFallback()) {
        QueueFallback(fallback);
    }
    Built own = GetNow(lock, key);
    if (!own.ok()) {
        return rhi::Error{own.error()};
    }
    handle.pipeline = *own;
    return handle;
}

native::PipelineReadiness CachingProvider::Prepare(const PipelineKey& key, const PipelineKey& fallback) {
    std::unique_lock lock(mutex_);
    if (options_.force_ubershader) {
        return native::PipelineReadiness::Ready;  // Lookup answers with this key's fallback
    }
    // The draw before mostly had the same key, and a pipeline once ready stays ready (nothing leaves the table): no
    // hash and no table for it again.
    if (last_ready_valid_ && last_ready_ == key) {
        return native::PipelineReadiness::Ready;
    }
    const auto it = table_.find(key);
    if (it != table_.end()) {
        if (it->second.state == State::Ready) {
            last_ready_ = key;
            last_ready_valid_ = true;
            return native::PipelineReadiness::Ready;
        }
        if (it->second.state == State::Failed) {
            return native::PipelineReadiness::Failed;
        }
    }
    if (options_.background) {
        if (options_.never_wait && (!IsReady(fallback) || !FallbackAllowed())) {
            QueueUrgent(key);
            if (!IsReady(fallback)) {
                QueueFallback(fallback);
            }
            ++frame_.skipped, ++progress_.skipped;
            return native::PipelineReadiness::Pending;
        }
        if (IsReady(fallback)) {
            NoteWanted(key);
            ++frame_.pending;
            return native::PipelineReadiness::Fallback;
        }
        QueueFallback(fallback);
        if (options_.never_wait) {  // (Lookup's reason: nothing is built or waited for on the renderer thread)
            QueueUrgent(key);
            ++frame_.skipped, ++progress_.skipped;
            return native::PipelineReadiness::Pending;
        }
    }
    return GetNow(lock, key).ok() ? native::PipelineReadiness::Ready : native::PipelineReadiness::Failed;
}

void CachingProvider::ReportMiss(const PipelineKey&, std::string_view) {
    std::lock_guard lock(mutex_);
    ++total_.reported_misses;
}

native::ProviderCounters CachingProvider::Counters() const {
    std::lock_guard lock(mutex_);
    return total_;
}

std::vector<native::FrameCounter> CachingProvider::TakeFrameCounters() {
    std::lock_guard lock(mutex_);
    std::vector<native::FrameCounter> out;
    out.reserve(6);
    out.push_back({"pipeline_sync_builds", frame_.sync_builds});
    out.push_back({"pipeline_forced_uber", frame_.forced_uber});
    out.push_back({"pipeline_sync_waits", frame_.sync_waits});
    out.push_back({"pipeline_sync_us", frame_.sync_us});
    out.push_back({"pipeline_async_builds", frame_.async_builds});
    out.push_back({"pipeline_pending", frame_.pending});
    out.push_back({"pipeline_skipped", frame_.skipped});
    frame_ = Frame{};
    return out;
}

// ---- the workers and the saver ----

void CachingProvider::Worker() {
    LowerThreadPriority();
    std::unique_lock lock(mutex_);
    for (;;) {
        work_cv_.wait(lock, [&] {
            return stop_ || !urgent_.empty() || (!background_.empty() && background_building_ < background_limit_);
        });
        if (stop_) {
            return;
        }
        const bool from_list = urgent_.empty();
        std::deque<PipelineKey>& queue = from_list ? background_ : urgent_;
        const PipelineKey key = std::move(queue.front());
        queue.pop_front();
        const auto it = table_.find(key);
        if (it == table_.end() || it->second.state != State::Queued) {
            continue;  // built meanwhile (the renderer took it over, or it was queued twice)
        }
        Entry& e = it->second;
        e.state = State::Building;
        ++building_;
        background_building_ += from_list ? 1 : 0;
        lock.unlock();
        const auto t0 = Clock::now();
        Built built = host_.build(key);
        const double ms = MsSince(t0);
        lock.lock();
        --building_;
        background_building_ -= from_list ? 1 : 0;
        Finish(e, built);
        ++stats_.async_builds;
        stats_.async_ms += ms;
        ++frame_.async_builds;
        ++progress_.builds;
        progress_.ms += ms;
        progress_.longest_ms = std::max(progress_.longest_ms, ms);
        if (from_list) {
            ++stats_.precompiled;
            precompile_failures_ += e.state == State::Failed ? 1 : 0;
            work_cv_.notify_one();  // the background queue may go on
        }
        if (!precompile_reported_ && urgent_.empty() && background_.empty() && building_ == 0) {
            precompile_reported_ = true;
            MH4N_LOG(Info, kLog, "the list is compiled ({} keys built on workers, {} failed, {:.0f} ms after the start)",
                     stats_.precompiled, precompile_failures_, MsSince(start_));
        }
        done_cv_.notify_all();
    }
}

void CachingProvider::Saver() {
    std::unique_lock lock(mutex_);
    while (!stop_) {
        saver_cv_.wait_for(lock, std::chrono::seconds(std::max(1u, options_.save_seconds)), [&] { return stop_; });
        if (stop_) {
            break;
        }
        ReportProgress();
        const bool dirty = list_dirty_ || built_since_blob_ > 0 || (spirv_ != nullptr && spirv_->Dirty());
        if (!dirty) {
            continue;
        }
        lock.unlock();
        WriteFiles(false);
        lock.lock();
    }
}

// At the saver's turn (every save_seconds), while the list is still being compiled, or when the builds since the last
// turn took a second or more of the workers' time: how far the list is, the builds of that time and how long each took,
// the draws left out meanwhile (no pipeline yet) and what is still queued. The list is done within 0.3-3 s on the
// phones' logs (an Adreno 610 on 0.1.8 included); a phone where it is never done in a session of minutes (4u-native #43:
// an Adreno 610 at 7-8 FPS at 1x as at 3x, no "the list is compiled" line) then says whether its builds each take long
// (the driver's cache missed: full compiles) or few are made (the workers kept from the cores). The first 15 lines, then
// one in 15.
void CachingProvider::ReportProgress() {
    const Progress p = progress_;
    progress_ = Progress{};
    const bool listing = !precompile_reported_;
    if (!listing && p.ms < 1000.0) {
        return;
    }
    const u32 n = ++progress_lines_;
    if (n > 15 && n % 15 != 0) {
        return;
    }
    MH4N_LOG(Info, kLog, "{:.0f} s after the start, {}the last {} s: {} builds, {:.0f} ms each (the longest {:.0f} ms), {} draws "
                         "left out; {} urgent and {} more queued{}",
             MsSince(start_) / 1000.0,
             listing ? fmt::format("the list not compiled yet ({} built from it so far, {} failed); ", stats_.precompiled,
                                   precompile_failures_)
                     : std::string(),
             std::max(1u, options_.save_seconds), p.builds, p.builds > 0 ? p.ms / static_cast<double>(p.builds) : 0.0,
             p.longest_ms, p.skipped, urgent_.size(), background_.size(),
             n == 15 ? " (from now on one line in 15)" : "");
}

// ---- the files ----

void CachingProvider::Load() {
    if (options_.folder.empty()) {
        MH4N_LOG(Info, kLog, "no folder (no --user or --pipeline-dir, or --no-pipeline-files): nothing is read or written");
        return;
    }
    const std::filesystem::path& dir = options_.folder;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const bool geometry_shader = host_.backend == nullptr || host_.backend->Caps().geometry_shader;

    // The driver's blob, before any pipeline is made.
    std::string driver = "off";
    if (options_.driver_cache && host_.backend != nullptr) {
        driver_path_ = dir / DriverFileName(host_.backend->SavePipelineCache());
        if (std::optional<std::vector<u8>> blob = ReadBytes(driver_path_)) {
            stats_.driver_blob_loaded = host_.backend->LoadPipelineCache(*blob);
            driver = fmt::format("{} KiB {}", blob->size() / 1024, stats_.driver_blob_loaded ? "loaded" : "refused");
        } else {
            driver = "none";
        }
    }

    // The SPIR-V.
    std::string spirv = "off";
    if (options_.spirv_cache) {
        spirv_ = std::make_unique<SpirvFileStore>();
        Records records;
        const ReadStatus status = ReadRecords(dir / "spirv.bin", kKindSpirv, kSpirvFormat, host_.spirv_compiler_version, records);
        spirv_->Load(records);
        spirv = status == ReadStatus::Ok ? fmt::format("{} shaders", spirv_->size()) : ToString(status);
        if (host_.set_spirv_store) {
            host_.set_spirv_store(spirv_.get());
        }
    }

    // The vertex sources and the list.
    std::string list = "off";
    if (options_.list) {
        Records sources;
        const ReadStatus vstatus =
            ReadRecords(dir / "vertex.bin", kKindVertex, kVertexFormat, host_.vertex_source_version, sources);
        std::unordered_set<u64> restored;
        for (const auto& r : sources) {
            if (host_.load_vertex_source) {
                if (std::optional<u64> id = host_.load_vertex_source(r)) {
                    restored.insert(*id);
                }
            }
        }
        Records records;
        const ReadStatus status = ReadRecords(dir / "keys.bin", kKindKeys, kListFormat, native::kPipelineKeyVersion, records);
        struct Item {
            PipelineKey key;
            u32 uses, order;
        };
        std::vector<Item> items;
        u64 dropped = 0;
        for (const auto& r : records) {
            if (r.size() < 8) {
                ++dropped;
                continue;
            }
            std::optional<PipelineKey> key = DecodeKey(std::span<const u8>(r).subspan(8));
            if (!key || (key->vertex.kind == native::VertexStageKind::Translated && restored.count(key->vertex.program) == 0) ||
                (key->fixup == native::FixupStage::FixedGeometry && !geometry_shader) || table_.count(*key) != 0) {
                ++dropped;  // not a key of this build, or a vertex stage that cannot be made here
                continue;
            }
            items.push_back(Item{*key, Get32(r.data()), Get32(r.data() + 4)});
        }
        // The fallbacks first (they stop a first draw from stalling), then by decreasing use count.
        std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
            if (a.key.IsFallback() != b.key.IsFallback()) return a.key.IsFallback();
            return a.uses > b.uses;
        });
        for (const Item& item : items) {
            Entry& e = table_.try_emplace(item.key).first->second;
            e.state = State::Queued;
            e.uses = item.uses;
            e.order = item.order;
            next_order_ = std::max(next_order_, item.order + 1);
            Enqueue(item.key, e, false);
        }
        stats_.listed = items.size();
        precompile_reported_ = items.empty();
        list = status == ReadStatus::Ok
                   ? fmt::format("{} keys ({} fallbacks; {} dropped), {} vertex sources", items.size(),
                                 std::count_if(items.begin(), items.end(), [](const Item& i) { return i.key.IsFallback(); }),
                                 dropped, restored.size())
                   : fmt::format("{} (vertex sources: {})", ToString(status), ToString(vstatus));
    }
    MH4N_LOG(Info, kLog, "{}: list {}; SPIR-V {}; driver cache {}", utf8::PathToUtf8(dir), list, spirv, driver);
}

void CachingProvider::WriteFiles(bool final) {
    if (options_.folder.empty()) {
        return;
    }
    std::lock_guard save_lock(save_mutex_);
    const auto t0 = Clock::now();
    struct Item {
        PipelineKey key;
        u32 uses, order;
    };
    std::vector<Item> items;
    u64 built = 0;
    {
        std::lock_guard lock(mutex_);
        items.reserve(table_.size());
        for (const auto& [key, e] : table_) {
            if (e.state != State::Failed) {
                items.push_back(Item{key, e.uses, e.order});
            }
        }
        list_dirty_ = false;
        built = built_since_blob_;
        built_since_blob_ = 0;
    }
    const std::filesystem::path& dir = options_.folder;
    std::string error;
    if (options_.list) {
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.order < b.order; });
        Records records;
        records.reserve(items.size());
        std::vector<u64> programs;
        std::unordered_set<u64> seen;
        for (const Item& item : items) {
            std::vector<u8> r;
            Put32(r, item.uses);
            Put32(r, item.order);
            EncodeKey(item.key, r);
            records.push_back(std::move(r));
            if (item.key.vertex.kind == native::VertexStageKind::Translated && seen.insert(item.key.vertex.program).second) {
                programs.push_back(item.key.vertex.program);
            }
        }
        Records sources;
        for (u64 program : programs) {
            std::vector<u8> bytes = host_.save_vertex_source ? host_.save_vertex_source(program) : std::vector<u8>{};
            if (!bytes.empty()) {
                sources.push_back(std::move(bytes));
            }
        }
        // The sources first: a list whose sources are missing would drop its translated keys at the next start.
        if (!WriteRecords(dir / "vertex.bin", kKindVertex, kVertexFormat, host_.vertex_source_version, sources, &error) ||
            !WriteRecords(dir / "keys.bin", kKindKeys, kListFormat, native::kPipelineKeyVersion, records, &error)) {
            MH4N_LOG(Warning, kLog, "the list was not written: {}", error);
        }
    }
    if (spirv_ != nullptr && (final || spirv_->Dirty())) {
        if (!WriteRecords(dir / "spirv.bin", kKindSpirv, kSpirvFormat, host_.spirv_compiler_version, spirv_->Snapshot(), &error)) {
            MH4N_LOG(Warning, kLog, "the SPIR-V was not written: {}", error);
        }
    }
    size_t blob_bytes = 0;
    if (options_.driver_cache && host_.backend != nullptr && built > 0) {
        const std::vector<u8> blob = host_.backend->SavePipelineCache();
        blob_bytes = blob.size();
        if (blob.size() > 32 && !WriteBytes(driver_path_, blob, &error)) {
            MH4N_LOG(Warning, kLog, "the driver cache was not written: {}", error);
        }
    }
    std::lock_guard lock(mutex_);
    ++stats_.saves;
    if (final) {
        MH4N_LOG(Info, kLog, "saved {} keys{} in {:.0f} ms", items.size(),
                 blob_bytes > 0 ? fmt::format(", driver cache {} KiB", blob_bytes / 1024) : std::string(), MsSince(t0));
    }
}

// ---- the options and the registration ----

CachingOptions CachingOptionsFrom(const Options& options, bool force_ubershader) {
    CachingOptions out;
    out.force_ubershader = force_ubershader;
    out.background = !options.Has("--no-ubershader");
    {
        const char* wait = std::getenv("MH4N_PIPELINE_WAIT");
        out.never_wait = !options.Has("--pipeline-wait") && !options.Has("--deterministic") && !(wait != nullptr && *wait == '1');
        const char* fallback = std::getenv("MH4N_PIPELINE_FALLBACK");
        const long n = fallback != nullptr ? std::strtol(fallback, nullptr, 10) : 0;
        out.use_fallback = !out.never_wait || n != 0;
        out.fallback_per_window = !out.never_wait || n == 1 ? 0 : static_cast<u32>(std::clamp<long>(n, 0, 100000));
    }
    out.warm = options.Has("--warm");
    out.list = !options.Has("--no-pipeline-list");
    out.driver_cache = !options.Has("--no-driver-cache");
    out.spirv_cache = !options.Has("--no-spirv-cache");
    try {
        out.workers = static_cast<u32>(std::min<u64>(options.GetUInt("--pipeline-workers", 0), 16));
    } catch (const OptionError& e) {
        MH4N_LOG(Error, kLog, "--pipeline-workers: {}", e.what());
    }
    if (!options.Has("--no-pipeline-files")) {
        if (std::optional<std::string> dir = options.Get("--pipeline-dir")) {
            out.folder = utf8::PathFromUtf8(*dir);
        } else if (std::optional<std::string> user = options.Get("--user")) {
            std::error_code ec;
            const std::filesystem::path root = utf8::PathFromUtf8(*user);
            if (std::filesystem::is_directory(root, ec)) {
                out.folder = root / "cache" / "pipelines";
            }
        }
    }
    return out;
}

std::unique_ptr<native::PipelineProvider> MakeCachingProvider(const native::ProviderHost& host, const CachingOptions& options) {
    if (!host.build || (host.backend == nullptr && !options.folder.empty() && options.driver_cache)) {
        return nullptr;
    }
    return std::make_unique<CachingProvider>(host, options);
}

std::optional<CachingStats> StatsOf(const native::PipelineProvider& provider) {
    if (const auto* p = dynamic_cast<const CachingProvider*>(&provider)) {
        return p->Stats();
    }
    return std::nullopt;
}

void WaitIdle(native::PipelineProvider& provider) {
    if (auto* p = dynamic_cast<CachingProvider*>(&provider)) {
        p->WaitIdle();
    }
}

void SaveNow(native::PipelineProvider& provider) {
    if (auto* p = dynamic_cast<CachingProvider*>(&provider)) {
        p->Save(true);
    }
}

MH4N_REGISTER(native::PipelineProviderFactory, native::kCachingProviderName,
              [](const native::ProviderHost& host) -> std::unique_ptr<native::PipelineProvider> {
                  static const Options none;
                  return MakeCachingProvider(host, CachingOptionsFrom(host.options != nullptr ? *host.options : none,
                                                                      host.force_ubershader));
              });

#define MH4N_PIPELINE_OPTION(name, arg, help) \
    MH4N_OPTION("runner", name, arg, help);   \
    MH4N_OPTION("capture-player", name, arg, help)

MH4N_PIPELINE_OPTION("--no-ubershader", "",
                     "pipelines (N2-16): no background compilation; a pipeline that is missing is built on the renderer thread");
MH4N_PIPELINE_OPTION("--pipeline-wait", "",
                     "pipelines: a draw with neither its pipeline nor its fallback built builds it on the renderer thread "
                     "(the default leaves the draw out and queues both; MH4N_PIPELINE_WAIT=1 does the same)");
MH4N_PIPELINE_OPTION("--warm", "", "pipelines (N2-16): compile the whole list of pipelines before the first draw");
MH4N_PIPELINE_OPTION("--pipeline-dir", "DIR", "pipelines (N2-16): where the pipeline cache lives (default <--user>/cache/pipelines)");
MH4N_PIPELINE_OPTION("--no-pipeline-files", "", "pipelines (N2-16): no pipeline cache is read or written");
MH4N_PIPELINE_OPTION("--no-pipeline-list", "", "pipelines (N2-16): the list of pipelines seen is neither compiled at start nor written");
MH4N_PIPELINE_OPTION("--no-driver-cache", "", "pipelines (N2-16): the driver's pipeline cache is neither loaded nor written");
MH4N_PIPELINE_OPTION("--no-spirv-cache", "", "pipelines (N2-16): every shader goes through glslang (no SPIR-V cache)");
MH4N_PIPELINE_OPTION("--pipeline-workers", "N", "pipelines (N2-16): the threads that compile pipelines (default: from the cores)");

}  // namespace mh4n::gpu::pipeline
