// The A/B switches of the CPU cuts the renderer cannot reach by a call (MH4N_AB phases: a measurement aid, set by the
// renderer at each display transfer, read by any thread). Each is false (the cut on) unless a phase turns it off.
#pragma once

#include <atomic>

namespace mh4n::ab {

// The caching pipeline provider's list of ready pipelines (MH4N_PIPELINE_MRU; the MH4N_AB_FEATURE name "pmru").
inline std::atomic<bool> g_pipeline_mru_off{false};
// The CPU path's vertex cache searched by its table (MH4N_VERTEX_CACHE_TABLE; "vtable").
inline std::atomic<bool> g_vertex_cache_table_off{false};
// The geometry pipeline's backends made again in place (MH4N_GS_BACKEND_REUSE; "gsreuse").
inline std::atomic<bool> g_gs_backend_reuse_off{false};

}  // namespace mh4n::ab
