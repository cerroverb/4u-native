// The files of the pipeline cache (N2-16). Written for N2-16: nothing is copied from Azahar here (the header with the
// versions of the types the records depend on is the idea of Azahar's vk_shader_disk_cache.cpp, its FILE_INFO entry).
//
// A record file: the magic "MH4NPIPE", the file's kind, two version words, the number of records, each record as its
// size and its bytes, and a 64-bit hash of everything before it. A file of another kind or version is stale (dropped and
// rebuilt by play), a file whose hash or sizes do not add up is damaged (the same). Every file is written whole to a
// temporary beside it and renamed over the old one, so a process that dies while writing leaves the previous file.
#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "mh4n/types.h"

namespace mh4n::gpu::pipeline {

enum class ReadStatus : u8 { Ok, Missing, Stale, Damaged };
const char* ToString(ReadStatus status);

using Records = std::vector<std::vector<u8>>;

bool WriteRecords(const std::filesystem::path& path, u32 kind, u64 version_a, u64 version_b, const Records& records,
                  std::string* error = nullptr);
ReadStatus ReadRecords(const std::filesystem::path& path, u32 kind, u64 version_a, u64 version_b, Records& records);

// A file as it is (the driver's blob, which carries its own header).
bool WriteBytes(const std::filesystem::path& path, std::span<const u8> bytes, std::string* error = nullptr);
std::optional<std::vector<u8>> ReadBytes(const std::filesystem::path& path);

// The kinds.
inline constexpr u32 kKindKeys = 0x5359454B;     // "KEYS"
inline constexpr u32 kKindVertex = 0x43525356;   // "VSRC"
inline constexpr u32 kKindSpirv = 0x56525053;    // "SPRV"

}  // namespace mh4n::gpu::pipeline
