#include "gpu/native/ktx2.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>

#include <basisu_transcoder.h>
#include <basisu_transcoder_internal.h>
#include <zstd.h>

#include "../../common/utf8.h"
#include "hdpack/image.h"

namespace mh4n::gpu::native {
namespace {

constexpr unsigned char kIdentifier[12] = {0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n'};
constexpr u32 kVkRgba8Unorm = 37, kVkRgba8Srgb = 43, kVkBc7Unorm = 145, kVkBc7Srgb = 146;
constexpr u32 kSuperNone = 0, kSuperBasisLz = 1, kSuperZstd = 2;

u32 U32(const u8* p) {
    return u32{p[0]} | (u32{p[1]} << 8) | (u32{p[2]} << 16) | (u32{p[3]} << 24);
}
u64 U64(const u8* p) {
    return u64{U32(p)} | (u64{U32(p + 4)} << 32);
}

bool ReadAll(const std::string& path, std::vector<u8>& bytes) {
    std::ifstream in(utf8::PathFromUtf8(path), std::ios::binary | std::ios::ate);
    if (!in) return false;
    const std::streamsize n = in.tellg();
    if (n < 80) return false;
    bytes.resize(static_cast<std::size_t>(n));
    in.seekg(0);
    return static_cast<bool>(in.read(reinterpret_cast<char*>(bytes.data()), n));
}

// The header's facts (80 bytes) and the key/values' KTXorientation
bool Parse(const u8* d, std::size_t size, Ktx2Info& info, u32* super = nullptr) {
    if (size < 104 || std::memcmp(d, kIdentifier, 12) != 0) return false;
    info.vk_format = U32(d + 12);
    info.width = U32(d + 20), info.height = U32(d + 24);
    const u32 depth = U32(d + 28), layers = U32(d + 32), faces = U32(d + 36), scheme = U32(d + 44);
    if (super) *super = scheme;
    const u32 kvd_offset = U32(d + 56), kvd_length = U32(d + 60);
    if (kvd_length && u64{kvd_offset} + kvd_length <= size) {
        for (u32 at = kvd_offset; at + 4 <= kvd_offset + kvd_length;) {
            const u32 n = U32(d + at);
            if (n == 0 || u64{at} + 4 + n > u64{kvd_offset} + kvd_length) break;
            const char* key = reinterpret_cast<const char*>(d + at + 4);
            const std::size_t key_len = strnlen(key, n);
            if (key_len < n && std::strcmp(key, "KTXorientation") == 0 && key_len + 2 < n) {
                info.bottom_up = key[key_len + 2] == 'u';  // ("rd": rows top-down; "ru": bottom-up)
            }
            at += 4 + ((n + 3) & ~3u);
        }
    }
    info.basis = info.vk_format == 0;
    if (info.width == 0 || info.height == 0 || info.width > hdpack::kMaxDimension || info.height > hdpack::kMaxDimension) {
        info.kind = "a size this reader doesn't take";
    } else if (depth > 1 || layers > 1 || faces != 1) {
        info.kind = "a 3D, array or cube texture";
    } else if (info.basis) {
        info.readable = true;
        info.kind = scheme == kSuperBasisLz ? "ETC1S" : "UASTC";
        info.etc1s = scheme == kSuperBasisLz, info.uastc = !info.etc1s;
    } else if (scheme != kSuperNone && scheme != kSuperZstd) {
        info.kind = "a supercompression this reader doesn't take";
    } else if (info.vk_format == kVkRgba8Unorm || info.vk_format == kVkRgba8Srgb) {
        info.readable = true, info.kind = "RGBA8";
    } else if (info.vk_format == kVkBc7Unorm || info.vk_format == kVkBc7Srgb) {
        info.readable = true, info.kind = "BC7";
    } else {
        info.kind = "vkFormat " + std::to_string(info.vk_format) + " (only Basis Universal's, RGBA8 and BC7 are read)";
    }
    return true;
}

void BasisInit() {
    static std::once_flag once;
    std::call_once(once, [] { basist::basisu_transcoder_init(); });
}

// The rows of `rows` x `row_bytes` turned over, in place
void TurnOver(std::vector<u8>& data, u32 rows, std::size_t row_bytes) {
    std::vector<u8> line(row_bytes);
    for (u32 y = 0; y < rows / 2; ++y) {
        u8* a = data.data() + y * row_bytes;
        u8* b = data.data() + (rows - 1 - y) * row_bytes;
        std::memcpy(line.data(), a, row_bytes);
        std::memcpy(a, b, row_bytes);
        std::memcpy(b, line.data(), row_bytes);
    }
}

}  // namespace

bool Ktx2Header(const std::string& path, Ktx2Info& info) {
    std::ifstream in(utf8::PathFromUtf8(path), std::ios::binary);
    if (!in) return false;
    std::vector<u8> head(80);
    if (!in.read(reinterpret_cast<char*>(head.data()), 80)) return false;
    const u32 kvd_offset = U32(head.data() + 56), kvd_length = U32(head.data() + 60);
    if (kvd_length && kvd_length < (1u << 20)) {  // (the key/values follow the header and the level index)
        head.resize(std::max<std::size_t>(head.size(), u64{kvd_offset} + kvd_length));
        in.seekg(0);
        in.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
        head.resize(static_cast<std::size_t>(in.gcount()));
    }
    if (head.size() < 104) head.resize(104, 0);
    return Parse(head.data(), head.size(), info);
}

HdFormat Ktx2Target(const Ktx2Info& info, bool bc7) {
    const bool blocks = info.basis || info.kind == "BC7";
    return bc7 && blocks && info.bottom_up ? HdFormat::Bc7 : HdFormat::Rgba8;
}

bool ReadKtx2(const std::string& path, HdFormat target, u32 width, u32 height, std::vector<u8>& out, std::string& error) {
    std::vector<u8> bytes;
    if (!ReadAll(path, bytes)) {
        error = "cannot be read";
        return false;
    }
    Ktx2Info info;
    u32 scheme = 0;
    if (!Parse(bytes.data(), bytes.size(), info, &scheme) || !info.readable) {
        error = "not a KTX2 file this reader takes (" + info.kind + ")";
        return false;
    }
    if (info.width != width || info.height != height) {
        error = "its size changed";
        return false;
    }
    const u32 bw = (width + 3) / 4, bh = (height + 3) / 4;
    const bool want_blocks = target == HdFormat::Bc7;
    if (info.basis) {
        BasisInit();
        basist::ktx2_transcoder t;
        if (!t.init(bytes.data(), static_cast<u32>(bytes.size())) || !t.start_transcoding()) {
            error = "Basis Universal's transcoder refused it";
            return false;
        }
        if (!t.is_etc1s() && !t.is_uastc()) {
            error = "a Basis Universal kind other than ETC1S and UASTC LDR 4x4";
            return false;
        }
        if (want_blocks) {
            out.resize(u64{bw} * bh * 16);
            if (!t.transcode_image_level(0, 0, 0, out.data(), bw * bh, basist::transcoder_texture_format::cTFBC7_RGBA)) {
                error = "could not be transcoded to BC7";
                return false;
            }
            return true;  // (bottom-up already: Ktx2Target asks for blocks only then)
        }
        out.resize(u64{width} * height * 4);
        if (!t.transcode_image_level(0, 0, 0, out.data(), width * height, basist::transcoder_texture_format::cTFRGBA32)) {
            error = "could not be transcoded";
            return false;
        }
        if (info.bottom_up) TurnOver(out, height, std::size_t{width} * 4);
        return true;
    }
    // a format of its own: the first level, plain or Zstandard
    const u64 offset = U64(bytes.data() + 80), length = U64(bytes.data() + 88), plain = U64(bytes.data() + 96);
    if (offset + length > bytes.size()) {
        error = "its first level is past the file's end";
        return false;
    }
    std::vector<u8> level;
    if (scheme == kSuperZstd) {
        level.resize(static_cast<std::size_t>(plain));
        const std::size_t n = ZSTD_decompress(level.data(), level.size(), bytes.data() + offset, static_cast<std::size_t>(length));
        if (ZSTD_isError(n) || n != plain) {
            error = "its Zstandard data could not be read";
            return false;
        }
    } else {
        level.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
    }
    if (info.kind == "RGBA8") {
        if (level.size() < u64{width} * height * 4) {
            error = "its first level is too small";
            return false;
        }
        level.resize(u64{width} * height * 4);
        if (info.bottom_up) TurnOver(level, height, std::size_t{width} * 4);
        out = std::move(level);
        return true;
    }
    // BC7
    if (level.size() < u64{bw} * bh * 16) {
        error = "its first level is too small";
        return false;
    }
    level.resize(u64{bw} * bh * 16);
    if (want_blocks) {
        out = std::move(level);
        return true;
    }
    out.assign(u64{width} * height * 4, 0);  // (each block decoded: no BC7 on this device, or rows top-down)
    for (u32 by = 0; by < bh; ++by) {
        for (u32 bx = 0; bx < bw; ++bx) {
            basist::color_rgba px[16];
            if (!basist::bc7u::unpack_bc7(level.data() + (u64{by} * bw + bx) * 16, px)) {
                error = "a BC7 block could not be decoded";
                return false;
            }
            for (u32 y = 0; y < 4 && by * 4 + y < height; ++y) {
                for (u32 x = 0; x < 4 && bx * 4 + x < width; ++x) {
                    std::memcpy(out.data() + ((u64{by} * 4 + y) * width + bx * 4 + x) * 4, &px[y * 4 + x], 4);
                }
            }
        }
    }
    if (info.bottom_up) TurnOver(out, height, std::size_t{width} * 4);
    return true;
}

}  // namespace mh4n::gpu::native
