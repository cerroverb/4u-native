// Which pipeline provider `--gpu native` uses (N2-16). Written for N2-16: nothing is copied from Azahar here.
//
// N2-16's caching provider (`native/src/gpu/pipeline/`) when it is registered under `kCachingProviderName`
// (provider_host.h, through mh4n/registry.h); N2-08's default provider (a synchronous build on every miss, no files) when
// it is not, or with `--pipeline-provider default` (the "before" of an A/B measurement). The options of the caching
// provider are its own (`native/src/gpu/pipeline/caching_provider.h`).
#pragma once

#include <memory>

#include "gpu/native/pipeline_builder.h"
#include "gpu/native/pipeline_key.h"
#include "mh4n/options.h"

namespace mh4n::gpu::native {

std::unique_ptr<PipelineProvider> MakePipelineProvider(std::shared_ptr<rhi::Backend> backend, PipelineBuilder& builder,
                                                       const Options& options, bool force_ubershader);

}  // namespace mh4n::gpu::native
