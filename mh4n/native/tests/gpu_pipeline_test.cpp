// N2-16's pipeline cache (native/src/gpu/pipeline): the key's bytes and their stability, the record files and their checks,
// the caching provider's policy (a miss takes a ready fallback and a worker builds the key's own pipeline; with no fallback the
// own pipeline is built on the calling thread, once, and the fallback is queued), the list compiled at the next start (the
// fallbacks first, --warm), the SPIR-V store's round trip, and, on whatever Vulkan device the machine has, a forced miss that
// gives the same image as the specialised pipeline, the driver's blob through its file, and the SPIR-V store asked by the
// pipeline builder. Written for N2-16: nothing is copied from Azahar here.
#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "gpu/cpuvertex/triangle_batch.h"
#include "gpu/fbops/fixed_function.h"
#include "gpu/fragment/feature.h"
#include "gpu/fragment/key.h"
#include "gpu/fragment/profile.h"
#include "gpu/fragment/state.h"
#include "gpu/fragment/tables.h"
#include "gpu/fragment/uniforms.h"
#include "gpu/native/compose.h"
#include "gpu/native/pipeline_builder.h"
#include "gpu/native/pipeline_key.h"
#include "gpu/native/provider_host.h"
#include "gpu/pipeline/caching_provider.h"
#include "gpu/pipeline/files.h"
#include "gpu/pipeline/key_codec.h"
#include "mh4n/log.h"
#include "support/vulkan_device.h"

using namespace mh4n;
using namespace mh4n::gpu::fragment;
namespace re = mh4n::gpu::fragment::regenum;
namespace native = mh4n::gpu::native;
namespace pipeline = mh4n::gpu::pipeline;
namespace cpuvertex = mh4n::gpu::cpuvertex;
namespace fbops = mh4n::gpu::fbops;
namespace rhi = mh4n::gpu::rhi;
namespace test = mh4n::test;
namespace stdfs = std::filesystem;

namespace {

struct TempDir {
    stdfs::path path;
    TempDir() {
        static std::atomic<int> counter{0};
        path = stdfs::temp_directory_path() /
               ("mh4n_pipeline_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000007) + "_" +
                std::to_string(counter++));
        stdfs::remove_all(path);
        stdfs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        stdfs::remove_all(path, ec);
    }
};

// A non-null pipeline pointer that owns nothing and is never dereferenced: the provider only keeps and hands out the
// pointers (an rhi::Pipeline cannot be made without a device).
std::shared_ptr<rhi::Pipeline> Dummy() {
    alignas(16) static unsigned char storage[16];
    return std::shared_ptr<rhi::Pipeline>(std::shared_ptr<void>{}, reinterpret_cast<rhi::Pipeline*>(storage));
}

native::PipelineKey SpecialisedKey(u32 tev_word, rhi::CullMode cull = rhi::CullMode::None) {
    native::PipelineKey key;
    key.vertex.kind = native::VertexStageKind::Trivial;
    key.vertex.input.binding_count = 1;
    key.vertex.input.bindings[0].stride = 88;
    key.vertex.input.attribute_count = 2;
    key.vertex.input.attributes[0] = {0, 0, rhi::Format::Rgba32Float, 0};
    key.vertex.input.attributes[1] = {1, 0, rhi::Format::Rgba32Float, 16};
    key.fragment.key.texture.tev_stages[0].sources_raw = tev_word;
    key.state.cull = cull;
    key.color_format = rhi::Format::Rgba8Unorm;
    key.depth_format = rhi::Format::D24S8;
    return key;
}

native::PipelineKey TranslatedKey(u64 program, u32 tev_word) {
    native::PipelineKey key = SpecialisedKey(tev_word);
    key.vertex.kind = native::VertexStageKind::Translated;
    key.vertex.program = program;
    key.topology = rhi::Topology::TriangleStrip;
    return key;
}

// A fake host: the build callback counts and can wait or fail; the vertex sources are their ids as eight bytes.
struct FakeHost {
    std::mutex mutex;
    std::vector<native::PipelineKey> built;
    std::vector<std::thread::id> threads;
    std::set<u64> failing;         // fragment tev words whose builds fail
    std::set<u64> known_programs;  // vertex sources the "builder" has
    int specialised_ms = 0;
    native::SpirvStore* spirv = nullptr;

    native::ProviderHost Host() {
        native::ProviderHost host;
        host.build = [this](const native::PipelineKey& key) -> rhi::Result<std::shared_ptr<rhi::Pipeline>> {
            {
                std::lock_guard lock(mutex);
                built.push_back(key);
                threads.push_back(std::this_thread::get_id());
            }
            if (!key.IsFallback() && specialised_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(specialised_ms));
            }
            if (!key.IsFallback() && failing.count(key.fragment.key.texture.tev_stages[0].sources_raw) != 0) {
                return rhi::Error{"this key does not build"};
            }
            return Dummy();
        };
        host.save_vertex_source = [this](u64 program) {
            std::lock_guard lock(mutex);
            std::vector<u8> bytes;
            if (known_programs.count(program) != 0) {
                for (u32 i = 0; i < 8; ++i) bytes.push_back(static_cast<u8>(program >> (8 * i)));
            }
            return bytes;
        };
        host.load_vertex_source = [this](std::span<const u8> bytes) -> std::optional<u64> {
            if (bytes.size() != 8) return std::nullopt;
            u64 id = 0;
            for (u32 i = 0; i < 8; ++i) id |= static_cast<u64>(bytes[i]) << (8 * i);
            if (id == 0xBAD) return std::nullopt;  // a source of another version
            std::lock_guard lock(mutex);
            known_programs.insert(id);
            return id;
        };
        host.vertex_source_version = 7;
        host.set_spirv_store = [this](native::SpirvStore* store) { spirv = store; };
        host.spirv_compiler_version = 9;
        return host;
    }
    size_t Count() {
        std::lock_guard lock(mutex);
        return built.size();
    }
    size_t CountOf(const native::PipelineKey& key) {
        std::lock_guard lock(mutex);
        return static_cast<size_t>(std::count(built.begin(), built.end(), key));
    }
};

pipeline::CachingOptions NoFiles(u32 workers = 2) {
    pipeline::CachingOptions options;
    options.workers = workers;
    return options;
}

u64 Counter(native::PipelineProvider& provider, const char* name) {
    for (const native::FrameCounter& c : provider.TakeFrameCounters()) {
        if (c.name == name) return c.value;
    }
    return 0;
}

}  // namespace

// ---- the key's bytes ------------------------------------------------------------------------------------------------------

TEST_CASE("pipeline/the_key_round_trips_through_its_bytes_and_the_bytes_are_stable") {
    std::vector<native::PipelineKey> keys;
    keys.push_back(SpecialisedKey(0x123));
    keys.push_back(SpecialisedKey(0x123).AsFallback());
    keys.push_back(TranslatedKey(0x0123456789ABCDEFull, 0x456));
    native::PipelineKey full = TranslatedKey(42, 0x789);
    full.pass = native::PassKind::ShadowWrite;
    full.fixup = native::FixupStage::FixedGeometry;
    full.state.blend_enabled = true;
    full.state.src_color = rhi::BlendFactor::SrcAlpha;
    full.state.dst_color = rhi::BlendFactor::OneMinusSrcAlpha;
    full.state.alpha_op = rhi::BlendOp::Max;
    full.state.color_write_mask = 0x5;
    full.state.logic_op = rhi::LogicOp::Xor;
    full.state.depth_test = true;
    full.state.depth_compare = rhi::CompareOp::GreaterEqual;
    full.state.stencil_test = true;
    full.state.stencil_front.pass = rhi::StencilOp::Replace;
    full.state.stencil_back.compare = rhi::CompareOp::Never;
    full.state.front_face = rhi::FrontFace::Clockwise;
    full.state.depth_bias_enabled = true;
    full.state.depth_bias_constant = -1.5f;
    full.state.depth_bias_slope = 0.25f;
    full.fragment.key.lighting.raw = 0xDEADBEEF;
    full.fragment.key.user.raw = 3;
    keys.push_back(full);

    for (const native::PipelineKey& key : keys) {
        const std::vector<u8> bytes = pipeline::EncodeKey(key);
        CHECK(bytes == pipeline::EncodeKey(key));  // the same bytes every time
        const std::optional<native::PipelineKey> back = pipeline::DecodeKey(bytes);
        REQUIRE(back.has_value());
        CHECK(*back == key);
        CHECK(back->Hash() == key.Hash());
        CHECK(pipeline::EncodeKey(*back) == bytes);
        // too short, too long, a count over the maximum: not a key
        CHECK_FALSE(pipeline::DecodeKey(std::span<const u8>(bytes).first(bytes.size() - 1)).has_value());
        std::vector<u8> longer = bytes;
        longer.push_back(0);
        CHECK_FALSE(pipeline::DecodeKey(longer).has_value());
        std::vector<u8> bad = bytes;
        bad[10] = 200;  // binding_count
        CHECK_FALSE(pipeline::DecodeKey(bad).has_value());
    }
    // Different keys, different bytes; an ubershader key holds no fragment key.
    CHECK(pipeline::EncodeKey(keys[0]) != pipeline::EncodeKey(SpecialisedKey(0x124)));
    CHECK(pipeline::EncodeKey(keys[1]).size() + sizeof(FragmentKey) == pipeline::EncodeKey(keys[0]).size());
    // The layout of the bytes is fixed: a trivial-stage key with 1 binding and 2 attributes of the format above.
    CHECK(pipeline::EncodeKey(keys[0]).size() == 2 + 8 + 2 + 5 + 2 * 7 + 3 + sizeof(FragmentKey) + 14 + 8 + 3 + 8 + 3);
}

// ---- the files --------------------------------------------------------------------------------------------------------------

TEST_CASE("pipeline/record_files_round_trip_and_a_damaged_or_stale_one_is_refused") {
    TempDir dir;
    const stdfs::path file = dir.path / "sub" / "keys.bin";
    pipeline::Records records = {{1, 2, 3}, {}, std::vector<u8>(1000, 0xAB)};
    REQUIRE(pipeline::WriteRecords(file, pipeline::kKindKeys, 1, 2, records));
    pipeline::Records back;
    CHECK(pipeline::ReadRecords(file, pipeline::kKindKeys, 1, 2, back) == pipeline::ReadStatus::Ok);
    CHECK(back == records);
    CHECK(pipeline::ReadRecords(file, pipeline::kKindKeys, 1, 3, back) == pipeline::ReadStatus::Stale);
    CHECK(back.empty());
    CHECK(pipeline::ReadRecords(file, pipeline::kKindSpirv, 1, 2, back) == pipeline::ReadStatus::Stale);
    CHECK(pipeline::ReadRecords(dir.path / "none.bin", pipeline::kKindKeys, 1, 2, back) == pipeline::ReadStatus::Missing);

    // a flipped byte, a cut file
    std::vector<u8> bytes = *pipeline::ReadBytes(file);
    bytes[40] ^= 1;
    REQUIRE(pipeline::WriteBytes(file, bytes));
    CHECK(pipeline::ReadRecords(file, pipeline::kKindKeys, 1, 2, back) == pipeline::ReadStatus::Damaged);
    bytes[40] ^= 1;
    bytes.resize(bytes.size() - 3);
    REQUIRE(pipeline::WriteBytes(file, bytes));
    CHECK(pipeline::ReadRecords(file, pipeline::kKindKeys, 1, 2, back) == pipeline::ReadStatus::Damaged);

    // a write replaces the file and leaves no temporary
    REQUIRE(pipeline::WriteRecords(file, pipeline::kKindKeys, 1, 2, {{9}}));
    CHECK(pipeline::ReadRecords(file, pipeline::kKindKeys, 1, 2, back) == pipeline::ReadStatus::Ok);
    CHECK(back == pipeline::Records{{9}});
    size_t files = 0;
    for (const auto& entry : stdfs::directory_iterator(file.parent_path())) {
        (void)entry;
        ++files;
    }
    CHECK(files == 1);
}

// ---- the policy -------------------------------------------------------------------------------------------------------------

TEST_CASE("pipeline/a_miss_without_a_fallback_builds_its_own_pipeline_once_and_queues_the_fallback") {
    FakeHost fake;
    auto provider = pipeline::MakeCachingProvider(fake.Host(), NoFiles());
    REQUIRE(provider != nullptr);
    const native::PipelineKey a = SpecialisedKey(1);

    rhi::Result<native::PipelineHandle> first = provider->Lookup(a);
    REQUIRE(first.ok());
    CHECK_FALSE(first->fallback);  // no fallback yet: the own pipeline, built here
    CHECK(fake.CountOf(a) == 1);
    CHECK(Counter(*provider, "pipeline_sync_builds") == 1);
    pipeline::WaitIdle(*provider);
    CHECK(fake.CountOf(a.AsFallback()) == 1);  // the fallback, built by a worker meanwhile

    // A second key of the same fixed-function state: the fallback at once, the own pipeline from a worker.
    fake.specialised_ms = 30;
    const native::PipelineKey b = SpecialisedKey(2);
    rhi::Result<native::PipelineHandle> pending = provider->Lookup(b);
    REQUIRE(pending.ok());
    CHECK(pending->fallback);
    std::vector<native::FrameCounter> frame = provider->TakeFrameCounters();
    CHECK(frame[0].name == "pipeline_sync_builds");
    CHECK(frame[0].value == 0);
    pipeline::WaitIdle(*provider);
    rhi::Result<native::PipelineHandle> ready = provider->Lookup(b);
    REQUIRE(ready.ok());
    CHECK_FALSE(ready->fallback);
    CHECK(fake.CountOf(b) == 1);
    CHECK(provider->Counters().sync_builds == 1);
    CHECK(pipeline::StatsOf(*provider)->async_builds == 2);

    // Its own pipeline was built on a worker, not on this thread.
    {
        std::lock_guard lock(fake.mutex);
        for (size_t i = 0; i < fake.built.size(); ++i) {
            if (fake.built[i] == b) CHECK(fake.threads[i] != std::this_thread::get_id());
        }
    }
    // the same key again is a hit, built no more
    REQUIRE(provider->Lookup(b).ok());
    CHECK(fake.CountOf(b) == 1);
}

TEST_CASE("pipeline/prepare_answers_fallback_while_a_worker_builds_and_failed_for_good") {
    FakeHost fake;
    auto provider = pipeline::MakeCachingProvider(fake.Host(), NoFiles());
    const native::PipelineKey cpu_fallback = SpecialisedKey(0).AsFallback();
    const native::PipelineKey a = TranslatedKey(5, 1);

    // No fallback: built here, Ready; the fallback is queued.
    CHECK(provider->Prepare(a, cpu_fallback) == native::PipelineReadiness::Ready);
    CHECK(fake.CountOf(a) == 1);
    pipeline::WaitIdle(*provider);
    CHECK(fake.CountOf(cpu_fallback) == 1);

    fake.specialised_ms = 30;
    const native::PipelineKey b = TranslatedKey(6, 2);
    CHECK(provider->Prepare(b, cpu_fallback) == native::PipelineReadiness::Fallback);
    CHECK(Counter(*provider, "pipeline_pending") == 1);
    pipeline::WaitIdle(*provider);
    CHECK(provider->Prepare(b, cpu_fallback) == native::PipelineReadiness::Ready);
    CHECK(fake.CountOf(b) == 1);

    // A key that does not build: the fallback while it is tried, then Failed (the CPU path for good), never tried again.
    fake.failing.insert(3);
    const native::PipelineKey bad = TranslatedKey(7, 3);
    CHECK(provider->Prepare(bad, cpu_fallback) == native::PipelineReadiness::Fallback);
    pipeline::WaitIdle(*provider);
    CHECK(provider->Prepare(bad, cpu_fallback) == native::PipelineReadiness::Failed);
    CHECK(provider->Prepare(bad, cpu_fallback) == native::PipelineReadiness::Failed);
    CHECK(fake.CountOf(bad) == 1);

    // Lookup of a key whose own pipeline failed answers with its fallback.
    const native::PipelineKey bad_cpu = SpecialisedKey(3);
    REQUIRE(provider->Lookup(bad_cpu.AsFallback()).ok());
    rhi::Result<native::PipelineHandle> tried = provider->Lookup(bad_cpu);  // the fallback; a worker tries the own one
    REQUIRE(tried.ok());
    CHECK(tried->fallback);
    pipeline::WaitIdle(*provider);
    rhi::Result<native::PipelineHandle> again = provider->Lookup(bad_cpu);
    REQUIRE(again.ok());
    CHECK(again->fallback);
    CHECK(fake.CountOf(bad_cpu) == 1);
}

TEST_CASE("pipeline/no_ubershader_builds_every_miss_on_the_calling_thread") {
    FakeHost fake;
    pipeline::CachingOptions options = NoFiles();
    options.background = false;
    auto provider = pipeline::MakeCachingProvider(fake.Host(), options);
    REQUIRE(provider->Lookup(SpecialisedKey(1)).ok());
    rhi::Result<native::PipelineHandle> b = provider->Lookup(SpecialisedKey(2));
    REQUIRE(b.ok());
    CHECK_FALSE(b->fallback);
    CHECK(provider->Prepare(TranslatedKey(1, 3), SpecialisedKey(0).AsFallback()) == native::PipelineReadiness::Ready);
    CHECK(fake.Count() == 3);  // no fallback was built
    CHECK(provider->Counters().sync_builds == 3);
}

TEST_CASE("pipeline/force_ubershader_answers_every_draw_with_its_fallback") {
    FakeHost fake;
    pipeline::CachingOptions options = NoFiles();
    options.force_ubershader = true;
    auto provider = pipeline::MakeCachingProvider(fake.Host(), options);
    const native::PipelineKey a = TranslatedKey(9, 1);
    CHECK(provider->Prepare(a, SpecialisedKey(0).AsFallback()) == native::PipelineReadiness::Ready);
    rhi::Result<native::PipelineHandle> h = provider->Lookup(a);
    REQUIRE(h.ok());
    CHECK(h->fallback);
    REQUIRE(provider->Lookup(TranslatedKey(9, 2)).ok());
    CHECK(fake.Count() == 1);  // one fallback: the same vertex stage and state
    CHECK(fake.CountOf(a.AsFallback()) == 1);
    CHECK(Counter(*provider, "pipeline_forced_uber") == 2);
}

// ---- the list ----------------------------------------------------------------------------------------------------------------

TEST_CASE("pipeline/the_keys_seen_are_compiled_at_the_next_start_the_fallbacks_first") {
    TempDir dir;
    std::vector<native::PipelineKey> seen;
    {
        FakeHost fake;
        fake.known_programs = {5, 6, 0xBAD};
        pipeline::CachingOptions options = NoFiles();
        options.folder = dir.path;
        options.driver_cache = false;
        auto provider = pipeline::MakeCachingProvider(fake.Host(), options);
        const native::PipelineKey cpu_fallback = SpecialisedKey(0).AsFallback();
        CHECK(provider->Prepare(TranslatedKey(5, 1), cpu_fallback) == native::PipelineReadiness::Ready);
        pipeline::WaitIdle(*provider);
        CHECK(provider->Prepare(TranslatedKey(6, 2), cpu_fallback) == native::PipelineReadiness::Fallback);
        REQUIRE(provider->Lookup(SpecialisedKey(3, rhi::CullMode::Back)).ok());
        // a key whose vertex source will not load at the next start
        CHECK(provider->Prepare(TranslatedKey(0xBAD, 4), cpu_fallback) == native::PipelineReadiness::Fallback);
        pipeline::WaitIdle(*provider);
        std::lock_guard lock(fake.mutex);
        seen = fake.built;
    }  // the provider writes the files as it goes
    CHECK(stdfs::exists(dir.path / "keys.bin"));
    CHECK(stdfs::exists(dir.path / "vertex.bin"));

    FakeHost fake;
    pipeline::CachingOptions options = NoFiles();
    options.folder = dir.path;
    options.driver_cache = false;
    options.warm = true;  // the constructor returns once the list is compiled
    auto provider = pipeline::MakeCachingProvider(fake.Host(), options);
    const size_t expected = seen.size() - 1;  // all but the key of the source that does not load
    CHECK(fake.Count() == expected);
    CHECK(pipeline::StatsOf(*provider)->listed == expected);
    CHECK(fake.known_programs == std::set<u64>{5, 6});
    // the fallbacks before the specialised pipelines
    {
        std::lock_guard lock(fake.mutex);
        bool specialised_seen = false;
        for (const native::PipelineKey& key : fake.built) {
            if (key.IsFallback()) CHECK_FALSE(specialised_seen);
            else specialised_seen = true;
        }
    }
    // every key is ready: no build at the first draw
    for (const native::PipelineKey& key : seen) {
        if (key.vertex.program == 0xBAD) continue;
        rhi::Result<native::PipelineHandle> h = provider->Lookup(key);
        REQUIRE(h.ok());
        CHECK_FALSE(h->fallback);
    }
    CHECK(provider->Counters().sync_builds == 0);
    CHECK(fake.Count() == expected);

    // Another version of the key types: the list is dropped. (Damaged files are tested above.)
    pipeline::Records records;
    CHECK(pipeline::ReadRecords(dir.path / "keys.bin", pipeline::kKindKeys, 1, native::kPipelineKeyVersion + 1, records) ==
          pipeline::ReadStatus::Stale);
}

TEST_CASE("pipeline/a_list_still_compiling_at_the_savers_turn_says_how_far_it_is") {
    TempDir dir;
    {
        FakeHost fake;
        pipeline::CachingOptions options = NoFiles();
        options.folder = dir.path;
        options.driver_cache = false;
        auto provider = pipeline::MakeCachingProvider(fake.Host(), options);
        for (u32 k = 1; k <= 8; ++k) REQUIRE(provider->Lookup(SpecialisedKey(k)).ok());
        pipeline::WaitIdle(*provider);
    }  // (the list written as the provider goes)
    std::vector<std::string> lines;
    std::mutex lines_mutex;
    SetLogSink([&](const LogRecord& r) {
        std::lock_guard lock(lines_mutex);
        lines.emplace_back(r.message);
    });
    {
        FakeHost fake;
        fake.specialised_ms = 400;  // (a driver that compiles every pipeline anew: one listed key every 400 ms)
        pipeline::CachingOptions options = NoFiles(2);  // (one worker for the list, one kept for the urgent keys)
        options.folder = dir.path;
        options.driver_cache = false;
        options.save_seconds = 1;
        auto provider = pipeline::MakeCachingProvider(fake.Host(), options);
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));  // (the saver's first turn: 2 or 3 keys built of 8)
        pipeline::WaitIdle(*provider);
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));  // (a turn after the list: no line, its builds took < 1 s)
    }
    SetLogSink({});
    std::lock_guard lock(lines_mutex);
    const auto count = [&](const char* needle) {
        return std::count_if(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(needle) != std::string::npos; });
    };
    CHECK(count("the list not compiled yet (") >= 1);
    CHECK(count("ms each (the longest ") >= 1);
    CHECK(count("the list is compiled (") == 1);
    // (the line after the list compiled is not there: once the list is done, a turn's line needs a second of builds)
    const auto last_progress = std::find_if(lines.rbegin(), lines.rend(), [](const std::string& l) {
        return l.find("ms each (the longest ") != std::string::npos;
    });
    REQUIRE(last_progress != lines.rend());
    CHECK(last_progress->find("the list not compiled yet") != std::string::npos);
}

TEST_CASE("pipeline/the_spirv_store_round_trips_and_is_offered_to_the_builder") {
    TempDir dir;
    const native::ShaderTextHash h1 = native::HashShaderText("void main() {}", native::ShaderStage::Fragment);
    const native::ShaderTextHash h2 = native::HashShaderText("void main() {}", native::ShaderStage::Vertex);
    const native::ShaderTextHash h3 = native::HashShaderText("void main() { }", native::ShaderStage::Fragment);
    CHECK_FALSE(h1 == h2);
    CHECK_FALSE(h1 == h3);
    const std::vector<u32> words = {0x07230203, 1, 2, 3};
    {
        FakeHost fake;
        pipeline::CachingOptions options = NoFiles();
        options.folder = dir.path;
        options.driver_cache = false;
        auto provider = pipeline::MakeCachingProvider(fake.Host(), options);
        REQUIRE(fake.spirv != nullptr);
        std::vector<u32> out;
        CHECK_FALSE(fake.spirv->Find(h1, out));
        fake.spirv->Put(h1, words);
        CHECK(fake.spirv->Find(h1, out));
        CHECK(out == words);
        provider.reset();
        CHECK(fake.spirv == nullptr);  // taken back before the provider went away
    }
    FakeHost fake;
    pipeline::CachingOptions options = NoFiles();
    options.folder = dir.path;
    options.driver_cache = false;
    auto provider = pipeline::MakeCachingProvider(fake.Host(), options);
    REQUIRE(fake.spirv != nullptr);
    std::vector<u32> out;
    CHECK(fake.spirv->Find(h1, out));
    CHECK(out == words);
    CHECK_FALSE(fake.spirv->Find(h2, out));
    // another compiler version: the store starts empty
    provider.reset();
    native::ProviderHost host = fake.Host();
    host.spirv_compiler_version = 10;
    provider = pipeline::MakeCachingProvider(host, options);
    CHECK_FALSE(fake.spirv->Find(h1, out));
}

// ---- on a device ----------------------------------------------------------------------------------------------------------

namespace {

constexpr u32 kSize = 4;

float B(int byte) { return static_cast<float>(byte) / 255.0f; }

cpuvertex::OutputVertex Vertex(float x, float y, std::array<int, 4> color) {
    cpuvertex::OutputVertex v{};
    v.pos[0] = x;
    v.pos[1] = y;
    v.pos[2] = -0.5f;
    v.pos[3] = 1.0f;
    v.quat[3] = 1.0f;
    for (int i = 0; i < 4; ++i) v.color[i] = B(color[i]);
    v.tc0_w = 1.0f;
    return v;
}

native::VertexInputKey BatchInput() {
    const rhi::VertexLayout layout = cpuvertex::MakeBatchVertexLayout();
    native::VertexInputKey key;
    key.binding_count = static_cast<u8>(layout.bindings.size());
    for (size_t i = 0; i < layout.bindings.size(); ++i) key.bindings[i].stride = layout.bindings[i].stride;
    key.attribute_count = static_cast<u8>(layout.attributes.size());
    for (size_t i = 0; i < layout.attributes.size(); ++i) {
        key.attributes[i] = {static_cast<u8>(layout.attributes[i].location), static_cast<u8>(layout.attributes[i].binding),
                             layout.attributes[i].format, layout.attributes[i].offset};
    }
    return key;
}

// The pass-through stage a game leaves alone, with stage 0 replacing the colour by the primary colour.
FragmentKey PrimaryColourKey() {
    FragmentKey key;
    key.framebuffer.set_alpha_test_func(re::CompareFunc::Always);
    key.framebuffer.set_logic_op(re::LogicOp::Copy);
    key.framebuffer.requested_logic_op = re::LogicOp::Copy;
    key.framebuffer.set_depthmap_enable(re::DepthBuffering::ZBuffering);
    key.texture.set_texture0_type(re::TextureType::Texture2D);
    for (auto& stage : key.texture.tev_stages) {
        stage.sources_raw = 0xF | (0xF << 4) | (0xF << 8) | (0xF << 16) | (0xF << 20) | (0xF << 24);  // Previous
    }
    key.texture.tev_stages[0].sources_raw = 0x0 | (0xF << 4) | (0xF << 8) | (0x0 << 16) | (0xF << 20) | (0xF << 24);
    return key;
}

// The real builder behind the caching provider, drawing one full-target triangle through the composition.
struct DeviceRig {
    std::shared_ptr<rhi::Backend> backend;
    DeviceProfile profile;
    std::unique_ptr<native::PipelineBuilder> builder;
    std::unique_ptr<native::PipelineProvider> provider;
    std::unique_ptr<native::DrawComposer> composer;
    std::unique_ptr<FragmentUniformRing> uniforms;
    std::unique_ptr<TableRings> tables;
    std::unique_ptr<rhi::Ring> vertex_ring, vs_ring;
    std::shared_ptr<rhi::Image> color;
    native::DrawResult last;

    DeviceRig(std::shared_ptr<rhi::Backend> b, const pipeline::CachingOptions& options) : backend(std::move(b)) {
        profile = ProfileOf(backend->Caps());
        builder = std::make_unique<native::PipelineBuilder>(backend, FeatureRegistry::Global());
        native::ProviderHost host;
        host.backend = backend;
        host.build = [this](const native::PipelineKey& key) { return builder->Build(key); };
        host.save_vertex_source = [this](u64 id) { return builder->SaveVertexSource(id); };
        host.load_vertex_source = [this](std::span<const u8> bytes) { return builder->LoadVertexSource(bytes); };
        host.vertex_source_version = native::VertexSourceVersion();
        host.set_spirv_store = [this](native::SpirvStore* store) { builder->SetSpirvStore(store); };
        host.spirv_compiler_version = native::SpirvCompilerVersion();
        provider = pipeline::MakeCachingProvider(host, options);
        REQUIRE(provider != nullptr);
        auto made = native::DrawComposer::Create(backend, *provider);
        REQUIRE_MESSAGE(made.ok(), made.error());
        composer = std::move(*made);
        auto ring = FragmentUniformRing::Create(*backend);
        REQUIRE_MESSAGE(ring.ok(), ring.error());
        uniforms = std::move(*ring);
        auto table_rings = TableRings::Create(*backend);
        REQUIRE_MESSAGE(table_rings.ok(), table_rings.error());
        tables = std::move(*table_rings);
        auto vr = backend->CreateRing(1 << 20, rhi::BufferUsage::Vertex, "pipeline test vertices");
        auto sr = backend->CreateRing(1 << 20, rhi::BufferUsage::Uniform, "pipeline test vs uniforms");
        REQUIRE(vr.ok());
        REQUIRE(sr.ok());
        vertex_ring = std::move(*vr);
        vs_ring = std::move(*sr);
        rhi::ImageDesc desc;
        desc.format = rhi::Format::Rgba8Unorm;
        desc.width = desc.height = kSize;
        desc.name = "pipeline test colour";
        auto image = backend->CreateImage(desc);
        REQUIRE_MESSAGE(image.ok(), image.error());
        color = *image;
    }
    ~DeviceRig() {
        composer.reset();
        provider.reset();  // before the builder its workers call
    }

    native::DrawDesc Desc(const FragmentKey& key) const {
        native::DrawDesc d;
        d.vertex.kind = native::VertexStageKind::Trivial;
        d.vertex.input = BatchInput();
        d.fragment = &key;
        d.needs = NeedsOf(key, profile, FeatureRegistry::Global());
        d.color_format = rhi::Format::Rgba8Unorm;
        return d;
    }

    std::vector<u8> Render(const FragmentKey& key, std::array<int, 4> primary) {
        rhi::Recorder rec = backend->BeginRecording();
        composer->BeginRecording(rec);
        uniforms->BeginRecording();
        FragmentBlock block;
        std::memset(&block, 0, sizeof(block));
        block.cfg = key;
        auto alloc = uniforms->Write(block, true);
        REQUIRE_MESSAGE(alloc.ok(), alloc.error());
        cpuvertex::TriangleBatch batch;
        batch.AddTriangle(Vertex(-1, -1, primary), Vertex(3, -1, primary), Vertex(-1, 3, primary));
        auto streamed = cpuvertex::UploadBatch(*vertex_ring, batch);
        REQUIRE_MESSAGE(streamed.ok(), streamed.error());
        auto vs = vs_ring->Map(2048, std::max<u64>(256, backend->Caps().uniform_buffer_offset_alignment));
        REQUIRE_MESSAGE(vs.ok(), vs.error());
        std::memset(vs->data.data(), 0, 2048);
        REQUIRE(vs_ring->Commit(2048).ok());
        rhi::TargetPassDesc pass;
        pass.color = color.get();
        pass.area = rhi::Rect{0, 0, kSize, kSize};
        rhi::TargetPassDesc clear_pass = pass;
        clear_pass.color_load = rhi::LoadOp::Clear;
        clear_pass.clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
        composer->BeginTargetPass(rec, clear_pass);
        fbops::ApplyDynamicState(rec, fbops::DynamicState{}, false);
        native::DrawDesc d = Desc(key);
        d.target = pass;
        d.vs_pica_uniforms = native::UniformRange{vs_ring->GetBuffer().get(), vs->offset, 1616};
        d.vs_uniforms = native::UniformRange{vs_ring->GetBuffer().get(), vs->offset + 1792, 32};
        d.fragment_uniforms = *alloc;
        d.tables = tables.get();
        const rhi::VertexBufferBinding binding{vertex_ring->GetBuffer().get(), streamed->offset};
        d.vertex_buffers = std::span<const rhi::VertexBufferBinding>(&binding, 1);
        d.count = streamed->vertex_count;
        d.viewport = rhi::Viewport{0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0.0f, 1.0f};
        d.scissor = rhi::Rect{0, 0, kSize, kSize};
        auto result = composer->Draw(rec, d);
        REQUIRE_MESSAGE(result.ok(), result.error());
        last = *result;
        backend->WaitIdle(backend->Submit(std::move(rec)));
        auto pixels = backend->ReadImage(*color, 0, 0, kSize, kSize);
        REQUIRE_MESSAGE(pixels.ok(), pixels.error());
        return *pixels;
    }
};

bool SameWithin(const std::vector<u8>& a, const std::vector<u8>& b, int tolerance) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])) > tolerance) return false;
    }
    return true;
}

}  // namespace

TEST_CASE("pipeline/a_forced_miss_draws_the_same_image_through_the_ubershader_and_the_cache_round_trips") {
    auto backend = test::RequireBackend();
    if (!backend) return;
    TempDir dir;
    pipeline::CachingOptions options;
    options.folder = dir.path;
    options.workers = 2;
    const FragmentKey key = PrimaryColourKey();
    std::vector<u8> fallback_image, own_image;
    {
        DeviceRig rig(backend, options);
        // The fallback first (built here), so the draw's own pipeline is a miss that the ubershader answers.
        const native::PipelineKey own_key = rig.composer->KeyOf(rig.Desc(key));
        REQUIRE(rig.provider->Lookup(own_key.AsFallback()).ok());
        fallback_image = rig.Render(key, {100, 150, 200, 250});
        CHECK(rig.last.fallback);
        pipeline::WaitIdle(*rig.provider);
        own_image = rig.Render(key, {100, 150, 200, 250});
        CHECK_FALSE(rig.last.fallback);
        CHECK(SameWithin(fallback_image, own_image, 1));
        CHECK(own_image[0] == 100);
        CHECK(own_image[1] == 150);
        CHECK(own_image[2] == 200);
        CHECK(own_image[3] == 250);
        CHECK(rig.builder->stats().spirv_hits == 0);
    }
    CHECK(stdfs::exists(dir.path / "spirv.bin"));
    bool driver_file = false;
    for (const auto& entry : stdfs::directory_iterator(dir.path)) {
        driver_file |= entry.path().filename().string().rfind("driver", 0) == 0;
    }
    CHECK(driver_file);

    // The next start, on a new device: the driver's blob is accepted, both pipelines are compiled from the list without
    // glslang (the SPIR-V store has both shaders), and the first draw takes the own pipeline.
    auto second = test::RequireBackend();
    if (!second) return;
    options.warm = true;
    DeviceRig rig(second, options);
    CHECK(pipeline::StatsOf(*rig.provider)->driver_blob_loaded);
    CHECK(pipeline::StatsOf(*rig.provider)->listed == 2);
    CHECK(rig.builder->stats().spirv_hits == 2);
    CHECK(rig.builder->stats().glslang_ms == 0.0);
    const std::vector<u8> warm_image = rig.Render(key, {100, 150, 200, 250});
    CHECK_FALSE(rig.last.fallback);
    CHECK(warm_image == own_image);
    CHECK(rig.provider->Counters().sync_builds == 0);
}
