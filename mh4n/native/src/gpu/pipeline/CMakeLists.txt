# mh4n_gpu_pipeline (N2-16): the caching pipeline provider of `--gpu native` (docs/native/gpu-fragment-design.md 4.5, 4.6;
# caching_provider.h): the disk cache in the user folder (the driver's pipeline-cache blob, the list of keys seen, the
# sources of the translated vertex stages, the SPIR-V by GLSL hash), the list compiled at start on worker threads, and
# background compilation behind the ubershader. It implements N2-33's PipelineProvider against native/'s header-only
# `mh4n_gpu_pipeline_key` and registers itself through mh4n/registry.h; the composition never links it.
mh4n_module(
    SOURCES
        caching_provider.cpp
        files.cpp
        key_codec.cpp
    DEPENDS
        mh4n_gpu_backend_core
        mh4n_gpu_backend_sync
)
target_include_directories(mh4n_gpu_pipeline PUBLIC "${MH4N_SRC_DIR}")
target_link_libraries(mh4n_gpu_pipeline PUBLIC mh4n_gpu_pipeline_key)
