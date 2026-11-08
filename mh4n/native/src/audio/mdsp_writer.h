// The writer of MDSP captures (N1-18, --capture-dsp): the binary format of tools/native/capfmt.py
// (its docstring defines it) and of mh4u_runner's --capture-dsp (N0-06): the magic "MDSP", a u32
// version (1), then records {u32 type, u32 frame, u64 ticks, u32 size, payload}, all little-endian,
// and a sibling <file>.json with the manifest (tool, format, version, game, frames, record counts).
//
// The records of one audio frame, in the order N0-06's recorder writes them: type 1, the two
// 0x8000-byte regions of the DSP memory as the DSP sees them at the start of the frame; type 3, one
// per buffer of sample data a source read while the frame was generated (the physical address, the
// size, the CRC-32 of the bytes, then the bytes only the first time that address, size and CRC
// appear in the file); type 2, the 160 stereo s16 samples the frame handed to the sink. The service
// writes all three with `--dsp hle` (N3-11) and type 1 only with `--dsp status` (N1-18's model, which
// reads no sample data and outputs nothing). Captures hold game data (the application's audio state
// and its sounds): they live under build/ and never in the repository.
#pragma once

#include <cstdio>
#include <set>
#include <span>
#include <string>
#include <tuple>

#include "mh4n/types.h"

namespace mh4n::audio {

inline constexpr u32 kMdspVersion = 1;
// The record types: capfmt.py's DSP_REGIONS, DSP_PCM and DSP_SAMPLES.
inline constexpr u32 kMdspRegions = 1;
inline constexpr u32 kMdspPcm = 2;
inline constexpr u32 kMdspSamples = 3;
inline constexpr u32 kMdspPcmBytes = 640;  // a type-2 payload: 160 stereo s16

class MdspWriter {
public:
    MdspWriter() = default;
    ~MdspWriter() { Close(); }
    MdspWriter(const MdspWriter&) = delete;
    MdspWriter& operator=(const MdspWriter&) = delete;

    // Creates `path` and writes the file header. False (and nothing open) if it cannot be written.
    // `tool` and `program_id` go to the manifest.
    bool Open(const std::string& path, u64 program_id, std::string tool = "mh4u_native_runner");
    bool IsOpen() const { return file_ != nullptr; }

    // Type 1: region 0 and then region 1, `region_size` bytes each.
    bool AddRegions(u32 frame, u64 ticks, const u8* region0, const u8* region1, u32 region_size);
    // Type 2: the frame's output, kMdspPcmBytes bytes (160 interleaved stereo s16, left first).
    bool AddPcm(u32 frame, u64 ticks, const s16* samples);
    // Type 3: a read of `size` bytes at `physical_address`. `data` is what the memory gave, which may
    // be shorter than `size`: the rest reads as zeros, and is hashed and recorded as zeros. The bytes go
    // into the file only the first time this (address, size, CRC-32) is written.
    bool AddSampleRead(u32 frame, u64 ticks, u32 physical_address, u32 size, std::span<const u8> data);

    // Audio frames written (the type-1 records).
    u32 Frames() const { return counts_[kMdspRegions]; }
    // The records of one type written so far (types 1 to 3; 0 for any other).
    u32 Count(u32 type) const { return type <= kMdspSamples ? counts_[type] : 0; }

    // Closes the file and writes the manifest. Safe to call twice.
    void Close();

private:
    bool WriteHeader(u32 type, u32 frame, u64 ticks, u32 size);

    std::FILE* file_ = nullptr;
    std::string path_;
    std::string tool_;
    u64 program_id_ = 0;
    u32 counts_[kMdspSamples + 1] = {};
    std::set<std::tuple<u32, u32, u32>> seen_;  // (address, size, CRC-32) whose bytes are in the file
    bool failed_ = false;
};

}  // namespace mh4n::audio
