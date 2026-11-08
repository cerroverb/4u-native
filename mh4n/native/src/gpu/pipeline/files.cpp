// Written for N2-16: nothing is copied from Azahar here.
#include "gpu/pipeline/files.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <system_error>

#include <fmt/format.h>

#include "gpu/native/provider_host.h"

namespace mh4n::gpu::pipeline {
namespace {

constexpr char kMagic[8] = {'M', 'H', '4', 'N', 'P', 'I', 'P', 'E'};
constexpr u64 kHashSeed = 0x46494C45ull;  // "FILE"
constexpr size_t kHeaderSize = 8 + 4 + 8 + 8 + 4;

void Put32(std::vector<u8>& out, u32 v) {
    for (u32 i = 0; i < 4; ++i) out.push_back(static_cast<u8>(v >> (8 * i)));
}
void Put64(std::vector<u8>& out, u64 v) {
    Put32(out, static_cast<u32>(v));
    Put32(out, static_cast<u32>(v >> 32));
}
u32 Get32(const u8* p) {
    u32 v = 0;
    for (u32 i = 0; i < 4; ++i) v |= static_cast<u32>(p[i]) << (8 * i);
    return v;
}
u64 Get64(const u8* p) { return Get32(p) | (static_cast<u64>(Get32(p + 4)) << 32); }

std::filesystem::path TemporaryBeside(const std::filesystem::path& path) {
    static std::atomic<u64> serial{0};
    const u64 tick = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::path tmp = path;
    tmp += fmt::format(".{:x}.{}.tmp", tick & 0xFFFFFFFFFFull, serial.fetch_add(1));
    return tmp;
}

}  // namespace

const char* ToString(ReadStatus status) {
    switch (status) {
    case ReadStatus::Ok: return "ok";
    case ReadStatus::Missing: return "none";
    case ReadStatus::Stale: return "another version";
    case ReadStatus::Damaged: return "damaged";
    }
    return "?";
}

bool WriteBytes(const std::filesystem::path& path, std::span<const u8> bytes, std::string* error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const std::filesystem::path tmp = TemporaryBeside(path);
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = fmt::format("cannot write {}", tmp.string());
            return false;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) {
            if (error) *error = fmt::format("cannot write {}", tmp.string());
            out.close();
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        // A rename that does not replace (some file systems): remove the old file first.
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        ec.clear();
        std::filesystem::rename(tmp, path, ec);
    }
    if (ec) {
        if (error) *error = fmt::format("cannot rename {} to {}: {}", tmp.string(), path.string(), ec.message());
        std::error_code ignored;
        std::filesystem::remove(tmp, ignored);
        return false;
    }
    return true;
}

std::optional<std::vector<u8>> ReadBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        return std::nullopt;
    }
    in.seekg(0, std::ios::beg);
    std::vector<u8> bytes(static_cast<size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!in) {
            return std::nullopt;
        }
    }
    return bytes;
}

bool WriteRecords(const std::filesystem::path& path, u32 kind, u64 version_a, u64 version_b, const Records& records,
                  std::string* error) {
    size_t total = kHeaderSize + 8;
    for (const auto& r : records) total += 4 + r.size();
    std::vector<u8> out;
    out.reserve(total);
    out.insert(out.end(), kMagic, kMagic + 8);
    Put32(out, kind);
    Put64(out, version_a);
    Put64(out, version_b);
    Put32(out, static_cast<u32>(records.size()));
    for (const auto& r : records) {
        Put32(out, static_cast<u32>(r.size()));
        out.insert(out.end(), r.begin(), r.end());
    }
    Put64(out, native::detail::HashBytes(out.data(), out.size(), kHashSeed));
    return WriteBytes(path, out, error);
}

ReadStatus ReadRecords(const std::filesystem::path& path, u32 kind, u64 version_a, u64 version_b, Records& records) {
    records.clear();
    std::optional<std::vector<u8>> file = ReadBytes(path);
    if (!file) {
        return ReadStatus::Missing;
    }
    const std::vector<u8>& b = *file;
    if (b.size() < kHeaderSize + 8 || std::memcmp(b.data(), kMagic, 8) != 0) {
        return ReadStatus::Damaged;
    }
    const size_t body = b.size() - 8;
    if (native::detail::HashBytes(b.data(), body, kHashSeed) != Get64(b.data() + body)) {
        return ReadStatus::Damaged;
    }
    if (Get32(b.data() + 8) != kind || Get64(b.data() + 12) != version_a || Get64(b.data() + 20) != version_b) {
        return ReadStatus::Stale;
    }
    const u32 count = Get32(b.data() + 28);
    size_t at = kHeaderSize;
    records.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        if (at + 4 > body) {
            records.clear();
            return ReadStatus::Damaged;
        }
        const u32 size = Get32(b.data() + at);
        at += 4;
        if (size > body - at) {
            records.clear();
            return ReadStatus::Damaged;
        }
        records.emplace_back(b.begin() + static_cast<std::ptrdiff_t>(at), b.begin() + static_cast<std::ptrdiff_t>(at + size));
        at += size;
    }
    if (at != body) {
        records.clear();
        return ReadStatus::Damaged;
    }
    return ReadStatus::Ok;
}

}  // namespace mh4n::gpu::pipeline
