// What the composition gives a pipeline provider that lives outside it, and how one is registered (N2-16;
// docs/native/gpu-fragment-design.md 4.5, 4.6, 4.7).
//
// Written for N2-16: nothing is copied from Azahar here.
//
// Part of the header-only target `mh4n_gpu_pipeline_key`: N2-16's `pipeline/` implements a provider against this and
// registers a `PipelineProviderFactory` under `kCachingProviderName` through `mh4n/registry.h`; `--gpu native` takes
// it when it is registered (provider_select.h) and never links `pipeline/`.
//
// The provider gets, through `ProviderHost`:
//   - the backend (the driver's pipeline-cache blob, N2-28's LoadPipelineCache and SavePipelineCache);
//   - the build callback (the pipeline builder's Build; any thread);
//   - the sources of the translated vertex stages as bytes it stores and gives back at the next start (the PICA program
//     words a key's `vertex.program` id was translated from: the key holds only the id, design 4.6 item 2);
//   - a slot for a SPIR-V store the builder asks before it runs glslang (keyed by the hash of the GLSL text, so a change
//     of the generator never finds an old shader);
//   - the options (its own switches, the user folder).
#pragma once

#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "gpu/backend/rhi.h"
#include "gpu/native/pipeline_key.h"
#include "mh4n/options.h"
#include "mh4n/types.h"

namespace mh4n::gpu::native {

// ---- the hash of a shader's text ---------------------------------------------------------------------------------

namespace detail {
constexpr u64 Mix64(u64 x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}
inline u64 HashBytes(const void* data, size_t size, u64 seed) {
    const u8* p = static_cast<const u8*>(data);
    u64 h = Mix64(seed ^ (static_cast<u64>(size) * 0x9e3779b97f4a7c15ull));
    for (; size >= 8; size -= 8, p += 8) {
        u64 word;
        std::memcpy(&word, p, 8);
        h = Mix64(h ^ word) + 0x9e3779b97f4a7c15ull;
    }
    u64 tail = 0;
    std::memcpy(&tail, p, size);
    return Mix64(h ^ tail ^ (static_cast<u64>(size) << 56));
}
}  // namespace detail

enum class ShaderStage : u8 { Vertex = 0, Fragment = 1, Geometry = 2, Compute = 3 };

// 128 bits of two independent hashes, the length and the stage: two texts that differ never share an entry in practice.
struct ShaderTextHash {
    u64 a = 0, b = 0;
    u32 length = 0;
    ShaderStage stage = ShaderStage::Vertex;
    bool operator==(const ShaderTextHash&) const = default;
};
struct ShaderTextHashHasher {
    size_t operator()(const ShaderTextHash& h) const noexcept { return static_cast<size_t>(h.a); }
};
inline ShaderTextHash HashShaderText(std::string_view text, ShaderStage stage) {
    ShaderTextHash h;
    h.a = detail::HashBytes(text.data(), text.size(), 0x53505631ull + static_cast<u64>(stage));
    h.b = detail::HashBytes(text.data(), text.size(), 0x4D48344Eull ^ (static_cast<u64>(stage) << 32));
    h.length = static_cast<u32>(text.size());
    h.stage = stage;
    return h;
}

// Compiled SPIR-V by the hash of its GLSL (N2-16's disk cache implements it). Thread-safe.
class SpirvStore {
public:
    virtual ~SpirvStore() = default;
    virtual bool Find(const ShaderTextHash& hash, std::vector<u32>& spirv) = 0;
    virtual void Put(const ShaderTextHash& hash, std::span<const u32> spirv) = 0;
};

// ---- the host ------------------------------------------------------------------------------------------------------

struct ProviderHost {
    std::shared_ptr<rhi::Backend> backend;
    // Builds a key's pipeline (specialised or ubershader: `key.fragment.kind` says which). Thread-safe.
    std::function<rhi::Result<std::shared_ptr<rhi::Pipeline>>(const PipelineKey&)> build;
    // The source of the translated vertex stage `program` (a key's `vertex.program`) as bytes, empty when it is not
    // known; and the inverse, which registers the source and gives its id, nullopt when the bytes are not a source of
    // this build (another version). Thread-safe.
    std::function<std::vector<u8>(u64 program)> save_vertex_source;
    std::function<std::optional<u64>(std::span<const u8>)> load_vertex_source;
    // The version of those bytes (kept in the header of the file that holds them).
    u64 vertex_source_version = 0;
    // Gives the builder the store it asks before glslang (null: none). The provider sets null before it goes away.
    std::function<void(SpirvStore*)> set_spirv_store;
    // The version of the compiler and of its settings (kept in the SPIR-V file's header).
    u64 spirv_compiler_version = 0;
    // N2-08's `--force-ubershader`.
    bool force_ubershader = false;
    // The options of the program (the provider's own switches, `--user`); valid while the factory runs.
    const Options* options = nullptr;
};

using PipelineProviderFactory = std::function<std::unique_ptr<PipelineProvider>(const ProviderHost&)>;
inline constexpr const char* kCachingProviderName = "cache";

}  // namespace mh4n::gpu::native
