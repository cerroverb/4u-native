// Written for this package from the design: nothing is copied from Azahar here.
//
// The pipeline builder of `--gpu native` (N2-08): turns a `PipelineKey` into a graphics pipeline. It is the build callback of
// N2-33's default provider (and what N2-16's provider calls on a worker thread):
//   - the base description (vertex layout, topology, host formats, the three set shapes) is `BaseDescOf`;
//   - the fixed-function state is the requested one with the device profile applied (`rhi::state::PlanPipeline`: the
//     hardware logic op or none, the blend of an emulated min/max half);
//   - the vertex stage is N2-06's SPIR-V for a translated program, or N2-37's trivial stage for the CPU path's batches. The key
//     holds only the program's id: the draw registers either the translated stage (`AddVertexStage`) or, since N2-16, the
//     program it is translated from (`AddVertexSource`: the words are copied once per id and the translation runs inside
//     `Build`, so under N2-16's provider on a worker thread, once per id);
//   - the fragment stage is the specialised shader of the key's `FragmentKey` (glslang, on the calling thread) or the
//     ubershader of its module (generated once per device profile, kept);
//   - the fix-up geometry stage (key.fixup) from the generator that lighting/ registers, for the vertex stage's output map.
// A key of kind VertexStageKind::QuaternionPass is a compute pipeline instead: the quaternion pass of its vertex source (a
// `pull_fixup` config, shader/vertex_shader.h) with set 0 of the draws' layout.
//
// N2-16 adds: a SPIR-V store (`SetSpirvStore`, provider_host.h) asked before glslang for the vertex and fragment stages,
// keyed by the GLSL text; the vertex sources as bytes (`SaveVertexSource`, `LoadVertexSource`) for the provider's files.
// `MH4N_TRACE_PIPELINES=1` logs each pipeline built with its glslang and creation times.
//
// Thread: the builder's tables are guarded by a mutex; `Build` may be called from several threads.
#pragma once

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_map>

#include "gpu/backend/rhi.h"
#include "gpu/fragment/feature.h"
#include "gpu/fragment/generator.h"
#include "gpu/native/compose.h"
#include "gpu/native/pipeline_key.h"
#include "gpu/native/provider_host.h"
#include "gpu/shader/vertex_shader.h"

namespace mh4n::gpu::native {

struct BuilderStats {
    u64 specialised_builds = 0;
    u64 uber_generated = 0;     // ubershader modules generated (once per module and profile)
    u64 uber_builds = 0;        // pipelines built from an ubershader
    u64 fragment_failures = 0;  // the generated GLSL did not compile
    double glslang_ms = 0.0;    // spent on the fragment shaders
    u64 vertex_translations = 0;  // vertex sources translated (once per id)
    u64 vertex_failures = 0;      // ... that did not translate or compile
    double vertex_glslang_ms = 0.0;
    u64 spirv_hits = 0;           // shaders the SPIR-V store had (glslang not run)
    double create_ms = 0.0;       // in CreateGraphicsPipeline
};

// The version of the bytes of SaveVertexSource (VertexShaderConfig's layout and the format below).
u64 VertexSourceVersion();
// The version of the SPIR-V the builder makes (glslang and its settings).
u64 SpirvCompilerVersion();

class PipelineBuilder {
public:
    PipelineBuilder(std::shared_ptr<rhi::Backend> backend, const fragment::FeatureRegistry& features);

    // Registers a translated vertex stage under the id the key's `vertex.program` holds.
    void AddVertexStage(u64 program, std::shared_ptr<const shader::TranslatedVertexShader> stage);
    // Registers the program a translated vertex stage is made from, under `config.Hash()`; the words are copied the first
    // time an id is seen (later calls cost a lookup). Build translates it, once.
    void AddVertexSource(const shader::VertexShaderConfig& config, const pica::ShaderSetup& setup);
    // The same with `config.Hash()` already known (the draw path computes it once a draw).
    void AddVertexSource(const shader::VertexShaderConfig& config, const pica::ShaderSetup& setup, u64 id);
    // A registered source as bytes (empty when the id is unknown) and back, registered under the id it returns (nullopt
    // when the bytes are not a valid source of this version: the id, the program's and the swizzle table's hashes are
    // checked).
    std::vector<u8> SaveVertexSource(u64 program) const;
    std::optional<u64> LoadVertexSource(std::span<const u8> bytes);

    // The store asked before glslang (null: none).
    void SetSpirvStore(SpirvStore* store) { spirv_store_.store(store); }

    // The default provider's build callback.
    rhi::Result<std::shared_ptr<rhi::Pipeline>> Build(const PipelineKey& key);

    // The specialised shader of a key, as Build makes it (for the tests and the logs).
    fragment::Generated GenerateFor(const PipelineKey& key) const;

    BuilderStats stats() const;

private:
    struct VertexSource {
        shader::VertexShaderConfig config;
        std::vector<u32> program, swizzle;
        std::once_flag once;
        std::shared_ptr<const shader::TranslatedVertexShader> stage;  // after `once`: null when it did not translate
        std::string error;
        std::once_flag quaternion_once;  // the quaternion pass (QuaternionPass keys), the same way
        std::shared_ptr<const shader::TranslatedVertexShader> quaternion;
        std::string quaternion_error;
    };
    struct Uber {
        std::once_flag once;
        std::shared_ptr<const fragment::Generated> generated;
    };

    std::shared_ptr<const shader::TranslatedVertexShader> VertexStage(u64 program, std::string& error);
    void Translate(u64 program, VertexSource& source, bool quaternion_pass = false);
    std::shared_ptr<VertexSource> SourceOf(u64 program);
    rhi::Result<std::shared_ptr<rhi::Pipeline>> BuildQuaternionPass(const PipelineKey& key);
    // GLSL to SPIR-V through the store.
    bool Compile(ShaderStage stage, const std::string& glsl, std::vector<u32>& spirv, std::string& error, double& ms);

    std::shared_ptr<rhi::Backend> backend_;
    const fragment::FeatureRegistry& features_;
    fragment::DeviceProfile profile_;
    std::atomic<SpirvStore*> spirv_store_{nullptr};
    mutable std::mutex mutex_;
    std::unordered_map<u64, std::shared_ptr<const shader::TranslatedVertexShader>> vertex_stages_;
    std::unordered_map<u64, std::shared_ptr<VertexSource>> vertex_sources_;
    std::array<Uber, 2> ubers_;  // by UberModule
    BuilderStats stats_;
};

}  // namespace mh4n::gpu::native
