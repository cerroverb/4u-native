// Written for N2-16: nothing is copied from Azahar here.
#include "gpu/native/provider_select.h"

#include "gpu/native/default_provider.h"
#include "gpu/native/provider_host.h"
#include "mh4n/log.h"
#include "mh4n/registry.h"

namespace mh4n::gpu::native {

std::unique_ptr<PipelineProvider> MakePipelineProvider(std::shared_ptr<rhi::Backend> backend, PipelineBuilder& builder,
                                                       const Options& options, bool force_ubershader) {
    const std::string name = options.GetString("--pipeline-provider", kCachingProviderName);
    const PipelineProviderFactory* factory =
        name == "default" ? nullptr : Registry<PipelineProviderFactory>::Instance().Find(name);
    if (factory != nullptr) {
        ProviderHost host;
        host.backend = backend;
        host.build = [&builder](const PipelineKey& key) { return builder.Build(key); };
        host.save_vertex_source = [&builder](u64 program) { return builder.SaveVertexSource(program); };
        host.load_vertex_source = [&builder](std::span<const u8> bytes) { return builder.LoadVertexSource(bytes); };
        host.vertex_source_version = VertexSourceVersion();
        host.set_spirv_store = [&builder](SpirvStore* store) { builder.SetSpirvStore(store); };
        host.spirv_compiler_version = SpirvCompilerVersion();
        host.force_ubershader = force_ubershader;
        host.options = &options;
        if (std::unique_ptr<PipelineProvider> provider = (*factory)(host)) {
            return provider;
        }
        MH4N_LOG(Warning, "NativeGpu", "the pipeline provider '{}' could not be made: the default provider is used", name);
    } else if (name != "default") {
        MH4N_LOG(Warning, "NativeGpu", "no pipeline provider '{}' is registered: the default provider is used", name);
    }
    return std::make_unique<DefaultPipelineProvider>([&builder](const PipelineKey& key) { return builder.Build(key); },
                                                     DefaultProviderOptions{force_ubershader});
}

MH4N_OPTION("runner", "--pipeline-provider", "NAME",
            "--gpu native-display: the pipeline provider, 'cache' (N2-16: the disk cache, the list compiled at start, "
            "background compilation behind the ubershader; the default) or 'default' (N2-08's: a synchronous build on every miss)");
MH4N_OPTION("capture-player", "--pipeline-provider", "NAME",
            "--gpu native: the pipeline provider, 'cache' (N2-16, the default) or 'default' (N2-08's synchronous builds)");
MH4N_OPTION("runner", "--force-ubershader", "",
            "--gpu native-display: every draw takes the ubershader (the forced-ubershader timing of D5)");

}  // namespace mh4n::gpu::native
