// Written for this package from the design: nothing is copied from Azahar here.
#include "gpu/native/pipeline_builder.h"

#include <fmt/format.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "mh4n/log.h"
#include "mh4n/spirv_target.h"

#include "gpu/backend/state/state.h"
#include "gpu/cpuvertex/trivial_vertex.h"
#include "gpu/fragment/profile.h"

namespace mh4n::gpu::native {
namespace {

constexpr const char* kLog = "NativeGpu";

bool Trace() {
    static const bool trace = std::getenv("MH4N_TRACE_PIPELINES") != nullptr;
    return trace;
}

double MsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// ---- the bytes of a vertex source: little-endian words, then the program and the swizzle table ----

constexpr u64 kVertexSourceFormat = 1;

void Put32(std::vector<u8>& out, u32 v) {
    for (u32 i = 0; i < 4; ++i) out.push_back(static_cast<u8>(v >> (8 * i)));
}
void Put64(std::vector<u8>& out, u64 v) {
    Put32(out, static_cast<u32>(v));
    Put32(out, static_cast<u32>(v >> 32));
}

struct Reader {
    std::span<const u8> bytes;
    size_t at = 0;
    bool ok = true;
    u32 U32() {
        if (at + 4 > bytes.size()) {
            ok = false;
            return 0;
        }
        u32 v = 0;
        for (u32 i = 0; i < 4; ++i) v |= static_cast<u32>(bytes[at + i]) << (8 * i);
        at += 4;
        return v;
    }
    u64 U64() {
        const u64 lo = U32();
        return lo | (static_cast<u64>(U32()) << 32);
    }
};

}  // namespace

u64 VertexSourceVersion() {
    using C = shader::VertexShaderConfig;
    return fragment::HashCombine(kVertexSourceFormat, sizeof(C), offsetof(C, program_hash), offsetof(C, outmap_attributes),
                                 offsetof(C, inputs), offsetof(C, use_clip_planes), sizeof(shader::InputRegisterConfig),
                                 offsetof(C, pull), sizeof(shader::PullAttribute),
                                 pica::kMaxProgramWords, pica::kMaxSwizzleWords);
}

u64 SpirvCompilerVersion() {
    // glslang 16.6.0 (native/third_party/glslang), Vulkan 1.2, SPIR-V 1.5 (1.1 and 1.3 below Vulkan 1.2:
    // mh4n/spirv_target.h, set before this is asked), no optimiser: bump the first number when the compiler or a
    // setting of gpu/shader/spirv_compile.cpp or gpu/fragment/spirv_compile.cpp changes.
    return SpirvTarget13() ? fragment::HashCombine(1, 16, 6, 0, 11, 13) : fragment::HashCombine(1, 16, 6, 0, 12, 15);
}

PipelineBuilder::PipelineBuilder(std::shared_ptr<rhi::Backend> backend, const fragment::FeatureRegistry& features)
    : backend_(std::move(backend)), features_(features), profile_(fragment::ProfileOf(backend_->Caps())) {}

void PipelineBuilder::AddVertexStage(u64 program, std::shared_ptr<const shader::TranslatedVertexShader> stage) {
    std::lock_guard lock(mutex_);
    vertex_stages_[program] = std::move(stage);
}

void PipelineBuilder::AddVertexSource(const shader::VertexShaderConfig& config, const pica::ShaderSetup& setup) {
    AddVertexSource(config, setup, config.Hash());
}

void PipelineBuilder::AddVertexSource(const shader::VertexShaderConfig& config, const pica::ShaderSetup& setup, u64 id) {
    std::lock_guard lock(mutex_);
    if (vertex_sources_.find(id) != vertex_sources_.end()) {
        return;
    }
    auto source = std::make_shared<VertexSource>();
    source->config = config;
    const u32 words = std::min(config.program_words, pica::kMaxProgramWords);
    const u32 swizzle = std::min(config.swizzle_words, pica::kMaxSwizzleWords);
    source->program.assign(setup.program_code.begin(), setup.program_code.begin() + words);
    source->swizzle.assign(setup.swizzle_data.begin(), setup.swizzle_data.begin() + swizzle);
    vertex_sources_.emplace(id, std::move(source));
}

std::vector<u8> PipelineBuilder::SaveVertexSource(u64 program) const {
    std::shared_ptr<VertexSource> source;
    {
        std::lock_guard lock(mutex_);
        const auto it = vertex_sources_.find(program);
        if (it == vertex_sources_.end()) {
            return {};
        }
        source = it->second;
    }
    const shader::VertexShaderConfig& c = source->config;
    std::vector<u8> out;
    out.reserve(160 + 4 * (source->program.size() + source->swizzle.size()));
    Put64(out, program);
    Put64(out, c.program_hash);
    Put64(out, c.swizzle_hash);
    Put32(out, c.program_words);
    Put32(out, c.swizzle_words);
    Put32(out, c.main_offset);
    Put32(out, c.output_mask);
    Put32(out, c.outmap_total);
    for (u32 word : c.outmap_attributes) Put32(out, word);
    for (const shader::InputRegisterConfig& input : c.inputs) {
        Put32(out, static_cast<u32>(input.conversion) | (input.alias_w ? 0x100u : 0u));
    }
    Put32(out, (c.use_clip_planes ? 1u : 0u) | (c.geometry_stage ? 2u : 0u) | (c.capture_outputs ? 4u : 0u) |
                   (c.exact_arithmetic ? 8u : 0u) | (c.pull_fixup ? 16u : 0u));
    for (const shader::PullAttribute& a : c.pull) {
        Put32(out, a.binding | (static_cast<u32>(a.format) << 8) | (static_cast<u32>(a.per_instance) << 16));
        Put32(out, a.offset | (static_cast<u32>(a.stride) << 16));
    }
    Put32(out, static_cast<u32>(source->program.size()));
    Put32(out, static_cast<u32>(source->swizzle.size()));
    for (u32 word : source->program) Put32(out, word);
    for (u32 word : source->swizzle) Put32(out, word);
    return out;
}

std::optional<u64> PipelineBuilder::LoadVertexSource(std::span<const u8> bytes) {
    Reader r{bytes};
    auto source = std::make_shared<VertexSource>();
    shader::VertexShaderConfig& c = source->config;
    const u64 id = r.U64();
    c.program_hash = r.U64();
    c.swizzle_hash = r.U64();
    c.program_words = r.U32();
    c.swizzle_words = r.U32();
    c.main_offset = r.U32();
    c.output_mask = r.U32();
    c.outmap_total = r.U32();
    for (u32& word : c.outmap_attributes) word = r.U32();
    for (shader::InputRegisterConfig& input : c.inputs) {
        const u32 v = r.U32();
        if ((v & 0xFF) > static_cast<u32>(vertex::Conversion::FromUint)) r.ok = false;
        input.conversion = static_cast<vertex::Conversion>(v & 0xFF);
        input.alias_w = (v & 0x100u) != 0;
    }
    const u32 flags = r.U32();
    c.use_clip_planes = (flags & 1u) != 0;
    c.geometry_stage = (flags & 2u) != 0;
    c.capture_outputs = (flags & 4u) != 0;
    c.exact_arithmetic = (flags & 8u) != 0;
    c.pull_fixup = (flags & 16u) != 0;
    for (shader::PullAttribute& a : c.pull) {
        const u32 v = r.U32(), w = r.U32();
        if (((v >> 8) & 0xFF) > static_cast<u32>(vertex::VertexFormat::R32G32B32A32_FLOAT)) r.ok = false;
        a.binding = static_cast<u8>(v & 0xFF);
        a.format = static_cast<vertex::VertexFormat>((v >> 8) & 0xFF);
        a.per_instance = static_cast<u8>((v >> 16) & 0xFF);
        a.offset = static_cast<u16>(w & 0xFFFF);
        a.stride = static_cast<u16>(w >> 16);
    }
    const u32 words = r.U32();
    const u32 swizzle = r.U32();
    if (!r.ok || words > pica::kMaxProgramWords || swizzle > pica::kMaxSwizzleWords ||
        words != std::min(c.program_words, pica::kMaxProgramWords) || swizzle != std::min(c.swizzle_words, pica::kMaxSwizzleWords) ||
        bytes.size() - r.at != 4ull * (words + swizzle)) {
        return std::nullopt;
    }
    source->program.resize(words);
    source->swizzle.resize(swizzle);
    for (u32& word : source->program) word = r.U32();
    for (u32& word : source->swizzle) word = r.U32();
    if (!r.ok || c.Hash() != id || shader::HashWords(source->program.data(), words) != c.program_hash ||
        shader::HashWords(source->swizzle.data(), swizzle) != c.swizzle_hash) {
        return std::nullopt;
    }
    std::lock_guard lock(mutex_);
    vertex_sources_.try_emplace(id, std::move(source));
    return id;
}

BuilderStats PipelineBuilder::stats() const {
    std::lock_guard lock(mutex_);
    return stats_;
}

fragment::Generated PipelineBuilder::GenerateFor(const PipelineKey& key) const {
    const fragment::ShaderPass pass =
        key.pass == PassKind::ShadowWrite ? fragment::ShaderPass::ShadowWrite : fragment::ShaderPass::Normal;
    return fragment::GenerateSpecialised(key.fragment.key, profile_, pass, features_);
}

bool PipelineBuilder::Compile(ShaderStage stage, const std::string& glsl, std::vector<u32>& spirv, std::string& error, double& ms) {
    ms = 0.0;
    SpirvStore* store = spirv_store_.load();
    const ShaderTextHash hash = HashShaderText(glsl, stage);
    if (store != nullptr && store->Find(hash, spirv) && !spirv.empty()) {
        std::lock_guard lock(mutex_);
        ++stats_.spirv_hits;
        return true;
    }
    const bool ok = stage == ShaderStage::Vertex    ? shader::CompileGlslToSpirv(glsl, spirv, error, &ms)
                    : stage == ShaderStage::Compute ? shader::CompileGlslToSpirv(glsl, spirv, error, &ms, true)
                                                    : fragment::CompileFragmentGlslToSpirv(glsl, spirv, error, &ms);
    if (ok && !spirv.empty()) {
        error.clear();
        if (store != nullptr) {
            store->Put(hash, spirv);
        }
        return true;
    }
    return false;
}

void PipelineBuilder::Translate(u64 program, VertexSource& source, bool quaternion_pass) {
    // The translator reads only the program's and the swizzle table's first words (shader::TranslateVertexShader).
    auto setup = std::make_unique<pica::ShaderSetup>();
    std::copy(source.program.begin(), source.program.end(), setup->program_code.begin());
    std::copy(source.swizzle.begin(), source.swizzle.end(), setup->swizzle_data.begin());
    setup->biggest_program_size = static_cast<u32>(source.program.size());
    setup->biggest_swizzle_size = static_cast<u32>(source.swizzle.size());
    shader::TranslateResult generated = shader::GenerateVertexGlsl(*setup, source.config, quaternion_pass);
    double ms = 0.0;
    std::shared_ptr<const shader::TranslatedVertexShader>& out = quaternion_pass ? source.quaternion : source.stage;
    std::string& out_error = quaternion_pass ? source.quaternion_error : source.error;
    if (generated.ok()) {
        auto stage = std::const_pointer_cast<shader::TranslatedVertexShader>(generated.shader);
        std::string error;
        if (Compile(quaternion_pass ? ShaderStage::Compute : ShaderStage::Vertex, stage->glsl, stage->spirv, error, ms)) {
            stage->compile_ms = ms;
            out = std::move(stage);
        } else {
            out_error = (quaternion_pass ? "the quaternion pass does not compile: " : "vertex shader does not compile: ") + error;
        }
    } else {
        out_error = generated.error;
    }
    std::lock_guard lock(mutex_);
    ++stats_.vertex_translations;
    stats_.vertex_glslang_ms += ms;
    if (out == nullptr) {
        ++stats_.vertex_failures;
        // Azahar falls back to the software path when AccelerateDrawBatch fails: the draws of this program take the CPU path.
        MH4N_LOG(Warning, kLog, "a vertex program was not translated ({}): CPU path", out_error);
    } else if (Trace()) {
        MH4N_LOG(Info, kLog, "vertex stage {:016x}: glslang {:.2f} ms", program, ms);
    }
}

std::shared_ptr<PipelineBuilder::VertexSource> PipelineBuilder::SourceOf(u64 program) {
    std::lock_guard lock(mutex_);
    const auto it = vertex_sources_.find(program);
    return it != vertex_sources_.end() ? it->second : nullptr;
}

rhi::Result<std::shared_ptr<rhi::Pipeline>> PipelineBuilder::BuildQuaternionPass(const PipelineKey& key) {
    std::shared_ptr<VertexSource> source = SourceOf(key.vertex.program);
    if (source == nullptr) {
        return rhi::Error{"pipeline builder: the vertex source of this quaternion pass was not registered"};
    }
    std::call_once(source->quaternion_once, [&] { Translate(key.vertex.program, *source, true); });
    if (source->quaternion == nullptr) {
        return rhi::Error{source->quaternion_error};
    }
    rhi::ComputePipelineDesc desc;
    desc.shader.spirv = source->quaternion->spirv;
    desc.sets = FixupPassSets();
    desc.name = fmt::format("quaternion pass {:016x}", key.vertex.program);
    const auto create_start = std::chrono::steady_clock::now();
    rhi::Result<std::shared_ptr<rhi::Pipeline>> created = backend_->CreateComputePipeline(desc);
    std::lock_guard lock(mutex_);
    stats_.create_ms += MsSince(create_start);
    return created;
}

std::shared_ptr<const shader::TranslatedVertexShader> PipelineBuilder::VertexStage(u64 program, std::string& error) {
    std::shared_ptr<VertexSource> source;
    {
        std::lock_guard lock(mutex_);
        if (const auto it = vertex_stages_.find(program); it != vertex_stages_.end() && it->second != nullptr) {
            return it->second;
        }
        if (const auto it = vertex_sources_.find(program); it != vertex_sources_.end()) {
            source = it->second;
        }
    }
    if (source == nullptr) {
        error = "pipeline builder: the translated vertex stage of this key was not registered";
        return nullptr;
    }
    std::call_once(source->once, [&] { Translate(program, *source); });
    if (source->stage == nullptr) {
        error = source->error;
    }
    return source->stage;
}

rhi::Result<std::shared_ptr<rhi::Pipeline>> PipelineBuilder::Build(const PipelineKey& key) {
    if (key.vertex.kind == VertexStageKind::QuaternionPass) {
        return BuildQuaternionPass(key);
    }
    rhi::GraphicsPipelineDesc desc = BaseDescOf(key, *backend_);

    // The fixed-function state on this device.
    rhi::Result<rhi::state::PipelinePlan> plan = rhi::state::PlanPipeline(key.state, backend_->Caps());
    if (!plan.ok()) {
        return rhi::Error{fmt::format("pipeline plan: {}", plan.error())};
    }
    desc.state = plan->built;

    // The vertex stage.
    std::shared_ptr<const shader::TranslatedVertexShader> translated;
    std::span<const u32> vertex_spirv;
    if (key.vertex.kind == VertexStageKind::Translated) {
        std::string error;
        translated = VertexStage(key.vertex.program, error);
        if (translated == nullptr) {
            return rhi::Error{error};
        }
        vertex_spirv = translated->spirv;
    } else {
        vertex_spirv = cpuvertex::TrivialVertexShader(backend_->Caps().clip_distance);
    }
    desc.vertex.spirv = vertex_spirv;

    // The fragment stage.
    std::shared_ptr<const fragment::Generated> uber;
    fragment::Generated specialised;
    if (key.IsFallback()) {
        const bool shadow = key.fragment.uber == UberModule::ShadowWrite;
        Uber& slot = ubers_[shadow ? 1 : 0];
        std::call_once(slot.once, [&] {
            auto generated = std::make_shared<fragment::Generated>();
            generated->glsl = fragment::GenerateUberGlsl(shadow ? fragment::ShaderPass::ShadowWrite : fragment::ShaderPass::Normal,
                                                         profile_, features_);
            Compile(ShaderStage::Fragment, generated->glsl, generated->spirv, generated->error, generated->compile_ms);
            std::lock_guard lock(mutex_);
            ++stats_.uber_generated;
            stats_.glslang_ms += generated->compile_ms;
            slot.generated = std::move(generated);
        });
        uber = slot.generated;
        if (!uber->ok()) {
            std::lock_guard lock(mutex_);
            ++stats_.fragment_failures;
            return rhi::Error{fmt::format("the ubershader does not compile: {}", uber->error)};
        }
        desc.fragment.spirv = uber->spirv;
        {
            std::lock_guard lock(mutex_);
            ++stats_.uber_builds;
        }
        desc.name += " (uber)";
    } else {
        const fragment::ShaderPass pass =
            key.pass == PassKind::ShadowWrite ? fragment::ShaderPass::ShadowWrite : fragment::ShaderPass::Normal;
        specialised.glsl = fragment::GenerateSpecialisedGlsl(key.fragment.key, profile_, pass, features_);
        Compile(ShaderStage::Fragment, specialised.glsl, specialised.spirv, specialised.error, specialised.compile_ms);
        {
            std::lock_guard lock(mutex_);
            ++stats_.specialised_builds;
            stats_.glslang_ms += specialised.compile_ms;
            if (!specialised.ok()) {
                ++stats_.fragment_failures;
            }
        }
        if (!specialised.ok()) {
            return rhi::Error{fmt::format("a fragment shader does not compile: {}", specialised.error)};
        }
        desc.fragment.spirv = specialised.spirv;
    }
    // MH4N_DUMP_GLSL=DIR (a measurement aid, for an offline compiler's report such as the Mali Offline Compiler's
    // cycles): the specialised fragment stage as DIR/fs_<fragment key hash>.frag, the translated vertex stage as
    // DIR/vs_<program>.vert, each once
    static const char* dump_glsl = std::getenv("MH4N_DUMP_GLSL");
    if (dump_glsl != nullptr && *dump_glsl != '\0') {
        const auto dump = [&](const std::string& path, const std::string& text) {
            if (FILE* f = std::fopen(path.c_str(), "rb")) {
                std::fclose(f);
                return;
            }
            if (FILE* f = std::fopen(path.c_str(), "wb")) {
                std::fwrite(text.data(), 1, text.size(), f);
                std::fclose(f);
            }
        };
        if (!key.IsFallback()) {
            dump(fmt::format("{}/fs_{:016x}.frag", dump_glsl, fragment::HashKey(key.fragment.key)), specialised.glsl);
        }
        if (translated != nullptr) {
            dump(fmt::format("{}/vs_{:016x}.vert", dump_glsl, key.vertex.program), translated->glsl);
        }
    }

    // The fix-up stage (design 5.5): lighting/ generates it for the vertex stage's output map.
    std::vector<u32> geometry;
    if (key.fixup == FixupStage::FixedGeometry) {
        if (translated == nullptr) {
            return rhi::Error{"pipeline builder: a fix-up stage needs a translated vertex stage"};
        }
        VertexStageOutputs outputs;
        outputs.program = key.vertex.program;
        outputs.num_outputs = translated->num_outputs;
        outputs.output_map = translated->output_map;
        for (const shader::SemanticSlot& slot : translated->semantic_map) {
            outputs.semantic_map.push_back({slot.attribute, slot.component});
        }
        geometry = FixupGeneratorRegistry::Global().Generate(outputs);
        if (geometry.empty()) {
            return rhi::Error{"pipeline builder: the fix-up generator made no geometry stage"};
        }
        desc.geometry.spirv = geometry;
    }

    const auto create_start = std::chrono::steady_clock::now();
    rhi::Result<std::shared_ptr<rhi::Pipeline>> created = backend_->CreateGraphicsPipeline(desc);
    const double create_ms = MsSince(create_start);
    {
        std::lock_guard lock(mutex_);
        stats_.create_ms += create_ms;
    }
    if (Trace()) {
        static std::mutex trace_mutex;
        std::lock_guard trace_lock(trace_mutex);
        static int count = 0;
        static const auto t0 = create_start;
        MH4N_LOG(Info, kLog, "pipeline {}: '{}' vs {} words, fs {} words, gs {} words; t={:.0f} ms fs glslang {:.2f} ms, create {:.2f} ms",
                 count, desc.name, desc.vertex.spirv.size(), desc.fragment.spirv.size(), desc.geometry.spirv.size(),
                 std::chrono::duration<double, std::milli>(create_start - t0).count(), key.IsFallback() ? 0.0 : specialised.compile_ms,
                 create_ms);
        if (const char* dir = std::getenv("MH4N_DUMP_SPIRV")) {
            const auto dump = [&](const char* stage, std::span<const u32> words) {
                if (words.empty()) {
                    return;
                }
                if (FILE* f = std::fopen(fmt::format("{}/p{:04}_{}.spv", dir, count, stage).c_str(), "wb")) {
                    std::fwrite(words.data(), 4, words.size(), f);
                    std::fclose(f);
                }
            };
            dump("vs", desc.vertex.spirv);
            dump("fs", desc.fragment.spirv);
            dump("gs", desc.geometry.spirv);
        }
        ++count;
    }
    return created;
}

}  // namespace mh4n::gpu::native
