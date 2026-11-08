// The pipeline key as bytes (N2-16; docs/native/gpu-fragment-design.md 4.3 and 4.6). Written for N2-16: nothing is copied
// from Azahar here.
//
// What is serialised is exactly what `core::ToPlume` consumes (rhi::FixedFunctionState, field by field, the floats by their
// bits) plus the topology, the vertex-input layout, the stage ids, the pass kind and the logical formats, and the whole
// FragmentKey of a specialised key (its bytes: the type has a unique object representation). Little-endian, no padding. The
// file that holds keys keeps `kPipelineKeyVersion` (pipeline_key.h) in its header, so a change to a key type drops the list.
#pragma once

#include <optional>
#include <span>
#include <vector>

#include "gpu/native/pipeline_key.h"

namespace mh4n::gpu::pipeline {

std::vector<u8> EncodeKey(const native::PipelineKey& key);
void EncodeKey(const native::PipelineKey& key, std::vector<u8>& out);  // appends
// nullopt when the bytes are not a key (too short or too long, a count over the maximum, a value out of its enum).
std::optional<native::PipelineKey> DecodeKey(std::span<const u8> bytes);

}  // namespace mh4n::gpu::pipeline
