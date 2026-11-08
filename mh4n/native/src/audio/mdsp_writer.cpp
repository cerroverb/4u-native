#include "audio/mdsp_writer.h"

#include <algorithm>
#include <cinttypes>
#include <utility>

#include "mh4n/crc32.h"
#include "mh4n/log.h"

namespace mh4n::audio {

namespace {
constexpr const char* kLog = "Audio.DSP";

// Zeros, for the part of a sample read the memory could not give.
constexpr std::size_t kZeroChunk = 4096;
const u8 kZeros[kZeroChunk] = {};

// Writes `count` zero bytes (or adds them to the CRC when `file` is null).
bool PadWithZeros(std::FILE* file, u64 count, u32* crc) {
    while (count != 0) {
        const std::size_t step = static_cast<std::size_t>(std::min<u64>(count, kZeroChunk));
        if (file != nullptr) {
            if (std::fwrite(kZeros, 1, step, file) != step) {
                return false;
            }
        } else {
            *crc = Crc32(kZeros, step, *crc);
        }
        count -= step;
    }
    return true;
}
}  // namespace

bool MdspWriter::Open(const std::string& path, u64 program_id, std::string tool) {
    Close();
    file_ = std::fopen(path.c_str(), "wb");
    if (file_ == nullptr) {
        MH4N_LOG(Error, kLog, "cannot write {}", path);
        return false;
    }
    path_ = path;
    tool_ = std::move(tool);
    program_id_ = program_id;
    std::fill(std::begin(counts_), std::end(counts_), 0u);
    seen_.clear();
    failed_ = false;
    // The format is little-endian; so is every host this runs on (ipc.h asserts it).
    const u32 version = kMdspVersion;
    if (std::fwrite("MDSP", 1, 4, file_) != 4 || std::fwrite(&version, 4, 1, file_) != 1) {
        MH4N_LOG(Error, kLog, "cannot write {}", path);
        std::fclose(file_);
        file_ = nullptr;
        return false;
    }
    return true;
}

bool MdspWriter::WriteHeader(u32 type, u32 frame, u64 ticks, u32 size) {
    return std::fwrite(&type, 4, 1, file_) == 1 && std::fwrite(&frame, 4, 1, file_) == 1 &&
           std::fwrite(&ticks, 8, 1, file_) == 1 && std::fwrite(&size, 4, 1, file_) == 1;
}

bool MdspWriter::AddRegions(u32 frame, u64 ticks, const u8* region0, const u8* region1,
                            u32 region_size) {
    if (file_ == nullptr) {
        return false;
    }
    const bool ok = WriteHeader(kMdspRegions, frame, ticks, 2 * region_size) &&
                    std::fwrite(region0, 1, region_size, file_) == region_size &&
                    std::fwrite(region1, 1, region_size, file_) == region_size;
    if (!ok) {
        failed_ = true;
        return false;
    }
    ++counts_[kMdspRegions];
    return true;
}

bool MdspWriter::AddPcm(u32 frame, u64 ticks, const s16* samples) {
    if (file_ == nullptr) {
        return false;
    }
    const bool ok = WriteHeader(kMdspPcm, frame, ticks, kMdspPcmBytes) &&
                    std::fwrite(samples, 1, kMdspPcmBytes, file_) == kMdspPcmBytes;
    if (!ok) {
        failed_ = true;
        return false;
    }
    ++counts_[kMdspPcm];
    return true;
}

bool MdspWriter::AddSampleRead(u32 frame, u64 ticks, u32 physical_address, u32 size,
                               std::span<const u8> data) {
    if (file_ == nullptr) {
        return false;
    }
    if (data.size() > size) {
        data = data.first(size);
    }
    u32 crc = Crc32(data.data(), data.size());
    PadWithZeros(nullptr, size - data.size(), &crc);
    // A record holds at most a u32 of payload: a read whose bytes would not fit keeps only its head.
    const bool first = seen_.insert({physical_address, size, crc}).second &&
                       static_cast<u64>(size) + 12 <= 0xFFFFFFFFull;
    const u32 payload = 12 + (first ? size : 0);
    bool ok = WriteHeader(kMdspSamples, frame, ticks, payload) &&
              std::fwrite(&physical_address, 4, 1, file_) == 1 && std::fwrite(&size, 4, 1, file_) == 1 &&
              std::fwrite(&crc, 4, 1, file_) == 1;
    if (ok && first) {
        ok = (data.empty() || std::fwrite(data.data(), 1, data.size(), file_) == data.size()) &&
             PadWithZeros(file_, size - data.size(), nullptr);
    }
    if (!ok) {
        failed_ = true;
        return false;
    }
    ++counts_[kMdspSamples];
    return true;
}

void MdspWriter::Close() {
    if (file_ == nullptr) {
        return;
    }
    if (std::fclose(file_) != 0) {
        failed_ = true;
    }
    file_ = nullptr;
    if (failed_) {
        MH4N_LOG(Error, kLog, "writing {} failed; the capture is incomplete", path_);
    }
    if (std::FILE* manifest = std::fopen((path_ + ".json").c_str(), "w")) {
        std::fprintf(manifest,
                     "{\n  \"tool\": \"%s\",\n  \"format\": \"MDSP\",\n  \"version\": %u,\n"
                     "  \"game\": \"0x%016" PRIx64 "\",\n  \"frames\": %u,\n"
                     "  \"records\": {\"1\": %u, \"2\": %u, \"3\": %u}\n}\n",
                     tool_.c_str(), kMdspVersion, program_id_, counts_[kMdspRegions],
                     counts_[kMdspRegions], counts_[kMdspPcm], counts_[kMdspSamples]);
        std::fclose(manifest);
    } else {
        MH4N_LOG(Error, kLog, "cannot write {}.json", path_);
    }
}

}  // namespace mh4n::audio
