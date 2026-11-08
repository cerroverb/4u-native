// The caching pipeline provider of `--gpu native` (N2-16; docs/native/gpu-fragment-design.md 4.5 and 4.6).
//
// Written for N2-16 from the design: nothing is copied from Azahar here. Azahar's vk_pipeline_cache.cpp (the driver's
// cache in a file per device, refused on a header mismatch) and vk_shader_disk_cache.cpp (a list of what was compiled,
// with the struct hashes of the key types in the header, compiled again at start) were read for the ideas.
//
// WHAT IT DOES
//   - The pipeline table: a key's pipeline is Queued (a worker will build it), Building, Ready or Failed. A lookup on the
//     renderer thread takes the table's lock for a hash lookup; it never waits for a worker, except for a fallback that
//     a worker is building at that moment (counted `pipeline_sync_waits`).
//   - On a miss: the key is queued for the workers (ahead of the start-up list) and the draw takes the fallback, the
//     ubershader pipeline of the same vertex stage, fixed-function state, formats and pass kind (`AsFallback`). An
//     accelerated draw asks `Prepare` first and, while its pipeline is Pending, is shaded on the CPU and drawn with the
//     trivial vertex stage's fallback (design 4.5, fallback (ii): those fallbacks depend only on the fixed-function state,
//     the formats and the pass kind). A fallback that does not exist yet is built on the renderer thread, once, and
//     counted (`pipeline_sync_builds`); it is listed, so on that machine it never happens again. A key whose own
//     pipeline cannot be built (Failed) keeps its fallback.
//   - The list: every key asked for, with a use count and the order of first sight, written by the saver thread (never
//     by the renderer thread). At start the list is compiled on the workers: the fallbacks first, then the specialised
//     pipelines by decreasing use count. The sources of the translated vertex stages the keys name go into a file of
//     their own (the key holds only the program's id).
//   - The driver's blob (N2-28's SavePipelineCache, LoadPipelineCache: refused when the vendor, the device or the
//     pipelineCacheUUID differ) in a file named after the device; loaded before any pipeline is made.
//   - The SPIR-V store: the builder's glslang output by the hash of the GLSL text (vertex and fragment stages), so the
//     list compiles at start without glslang. Entries not asked for in a run are dropped when the store grows past a cap.
//   - The files are written every `save_seconds` when something changed, and when the provider goes away (the renderer
//     is destroyed at the end of a run). Each is written whole and renamed into place.
//
// FILES, in `<--pipeline-dir>` or else `<--user>/cache/pipelines/` (none without either; the folder is made when the user
// folder exists): `keys.bin` (the list), `vertex.bin` (the vertex sources), `spirv.bin` (the SPIR-V),
// `driver-<vendor>-<device>-<uuid>.bin` (the driver's blob). They are derived from the game and the machine: they stay in
// the user folder, never in the repository or a package.
//
// OPTIONS (scopes `runner` and `capture-player`; each part can be turned off for an A/B measurement):
//   --no-ubershader        no background compilation: a miss is built on the renderer thread (the files stay)
//   --pipeline-wait        a draw with neither its pipeline nor its fallback built builds it on the renderer thread (or
//                          waits for the worker building it), as N2-16 did; by default it is left out and both are
//                          queued (`pipeline_skipped` draws): a new effect never stops the frame, it shows a frame or a
//                          few later. A cold cache (a new install, a new key format) otherwise froze the game for
//                          seconds at a time on a phone. The ubershader fallback is not drawn with either then (heavy on
//                          the GPU: MH4N_PIPELINE_FALLBACK=1 draws with it while the key's own pipeline is built)
//   --warm                 the list is compiled before the first draw returns
//   --pipeline-dir DIR     where the files go (default `<--user>/cache/pipelines`)
//   --no-pipeline-files    nothing is read or written (every run is a cold one)
//   --no-pipeline-list     the list is neither compiled at start nor written
//   --no-driver-cache      the driver's blob is neither loaded nor written
//   --no-spirv-cache       glslang for every shader (no SPIR-V store)
//   --pipeline-workers N   worker threads (default: half the cores less one, 1 to 4)
//   --pipeline-provider default (provider_select.h): N2-08's provider, none of this
// `--force-ubershader` (N2-08) is honoured with N2-33's meaning: every draw takes `AsFallback` of its own key.
//
// COUNTERS per frame: `pipeline_sync_builds` (built on the renderer thread), `pipeline_sync_waits` (waited for a worker),
// `pipeline_sync_us` (the renderer thread's time in both), `pipeline_async_builds` (finished by a worker),
// `pipeline_pending` (accelerated draws shaded on the CPU while their pipeline was built), `pipeline_forced_uber`.
#pragma once

#include <filesystem>
#include <memory>
#include <optional>

#include "gpu/native/provider_host.h"

namespace mh4n::gpu::pipeline {

struct CachingOptions {
    std::filesystem::path folder;  // empty: no files
    bool list = true;
    bool driver_cache = true;
    bool spirv_cache = true;
    bool background = true;  // false: --no-ubershader
    // A draw whose own pipeline and fallback are both missing is left out (both queued for the workers) instead of
    // building on the renderer thread or waiting for a worker there. CachingOptionsFrom turns it on unless
    // --pipeline-wait or --deterministic (a deterministic run draws every draw, whatever the workers' timing)
    bool never_wait = false;
    // With never_wait, how many draws may take the ubershader fallback while their own pipeline is built: at most
    // fallback_per_window in each 8 ms (CachingOptionsFrom: none; MH4N_PIPELINE_FALLBACK=<n> n, =1 no limit); past that
    // a draw is left out. The fallback's fragment stage is heavy: a cold scene drawn with it took 100 to 200 ms of GPU a
    // frame on an Adreno 830 at 3x and on a Mali-G57, and on the Mali 20 fallback draws a frame still took 130 ms. A
    // frame with draws left out is not shown (the renderer's HoldIncomplete: the last whole picture stands in), so a
    // draw left out is a repeated frame, not a flash.
    bool use_fallback = true;
    u32 fallback_per_window = 0;  // 0: no limit
    bool warm = false;
    bool force_ubershader = false;
    u32 workers = 0;  // 0: from the core count
    u32 save_seconds = 20;
};

// The options from the command line (see above); the folder from --pipeline-dir or --user.
CachingOptions CachingOptionsFrom(const Options& options, bool force_ubershader);

struct CachingStats {
    u64 listed = 0;              // keys read from the list at start
    u64 precompiled = 0;         // of them, built by the workers
    u64 async_builds = 0, sync_builds = 0, sync_waits = 0, failures = 0;
    double async_ms = 0.0, sync_ms = 0.0;
    u64 saves = 0;
    bool driver_blob_loaded = false;
};

class CachingProvider;

// Made directly by the tests; `--gpu native` makes it through the registry (kCachingProviderName).
std::unique_ptr<native::PipelineProvider> MakeCachingProvider(const native::ProviderHost& host, const CachingOptions& options);
// The provider's own figures (null for another provider).
std::optional<CachingStats> StatsOf(const native::PipelineProvider& provider);
// Waits until the queue is empty and no build is running (the tests; --warm does it at start).
void WaitIdle(native::PipelineProvider& provider);
// Writes the files now (the tests; the saver thread and the destructor do it otherwise).
void SaveNow(native::PipelineProvider& provider);

}  // namespace mh4n::gpu::pipeline
