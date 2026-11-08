// Derived from Azahar src/video_core/texture/texture_decode.cpp, texture_decode.h, etc1.cpp,
// src/video_core/rasterizer_cache/texture_codec.h, pixel_format.h, utils.cpp, src/video_core/utils.h,
// src/common/color.h and src/video_core/pica/regs_texturing.h at
// 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-2.0-or-later; every one of those files says
// "Licensed under GPLv2 or any later version").
// Copyright 2017-2026 Citra Emulator Project / Azahar Emulator Project (texture_decode.cpp,
//   texture_decode.h)
// Copyright 2017 Citra Emulator Project (etc1.cpp)
// Copyright 2022-2026 Citra Emulator Project / Azahar Emulator Project (texture_codec.h)
// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project (pixel_format.h)
// Copyright 2023 Citra Emulator Project (utils.cpp)
// Copyright 2014 Citra Emulator Project (utils.h, color.h)
// Copyright 2017-2024 Citra Emulator Project / Azahar Emulator Project (regs_texturing.h)
#include "gpu/texture/decode.h"

#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#include "mh4n/host_thread_name.h"

namespace mh4n::gpu::texture {
namespace {

// A few threads that share a big texture's decode with the caller, by rows of tiles (an area's load decodes dozens of
// textures in one frame on the renderer thread: 17 ms of a Moto g06's frame at the Gathering Hall). The caller takes a
// share too and waits for the rest; the parts write rows of the output apart. MH4N_DECODE_THREADS=<n> (0: none).
class DecodePool {
public:
    static DecodePool& Get() {
        static DecodePool pool;
        return pool;
    }
    // fn(begin, end) over [0, count), split in up to threads + 1 parts
    void For(u32 count, const std::function<void(u32, u32)>& fn) {
        const u32 parts = std::min<u32>(count, static_cast<u32>(threads_) + 1);
        if (parts <= 1) {
            fn(0, count);
            return;
        }
        struct Done {
            std::mutex mutex;
            std::condition_variable cv;
            u32 left = 0;
        } done;
        done.left = parts - 1;
        {
            std::lock_guard lock(mutex_);
            for (u32 i = 1; i < parts; ++i) {
                const u32 begin = count * i / parts, end = count * (i + 1) / parts;
                queue_.push_back([&fn, &done, begin, end] {
                    fn(begin, end);
                    std::lock_guard l(done.mutex);
                    if (--done.left == 0) done.cv.notify_one();
                });
            }
        }
        work_.notify_all();
        fn(0, count / parts);
        std::unique_lock l(done.mutex);
        done.cv.wait(l, [&] { return done.left == 0; });
    }

private:
    DecodePool() {
        const char* v = std::getenv("MH4N_DECODE_THREADS");
        const unsigned cores = std::thread::hardware_concurrency();
        threads_ = v != nullptr ? static_cast<unsigned>(std::max(0, std::atoi(v))) : std::min(3u, cores > 2 ? cores - 2 : 0u);
        for (unsigned i = 0; i < threads_; ++i) {
            std::thread([this] {
                SetHostThreadName("mh4n-decode");
                for (;;) {
                    std::function<void()> job;
                    {
                        std::unique_lock lock(mutex_);
                        work_.wait(lock, [this] { return !queue_.empty(); });
                        job = std::move(queue_.front());
                        queue_.pop_front();
                    }
                    job();
                }
            }).detach();  // (they live as long as the process: nothing to join at exit)
        }
    }
    std::mutex mutex_;
    std::condition_variable work_;
    std::deque<std::function<void()>> queue_;
    unsigned threads_ = 0;
};

// A texture of at least this many texels is decoded by DecodePool's threads too: ETC1's, and the others' (cheaper a
// texel). Below that the threads' wake-up costs more than they save (a 128x128 RGBA8: 0.010 ms alone, 0.021 shared).
constexpr u32 kParallelTexelsEtc = 128 * 128;
constexpr u32 kParallelTexels = 256 * 256;

// Common::Color (color.h)
constexpr u8 Convert4To8(u8 value) {
    return static_cast<u8>((value << 4) | value);
}
constexpr u8 Convert5To8(u8 value) {
    return static_cast<u8>((value << 3) | (value >> 2));
}
constexpr u8 Convert6To8(u8 value) {
    return static_cast<u8>((value << 2) | (value >> 4));
}
constexpr u8 Convert1To8(u8 value) {
    return value ? 255 : 0;
}

u16 Load16(const u8* p) {
    return static_cast<u16>(p[0] | (p[1] << 8));
}

u64 Load64(const u8* p) {
    u64 value = 0;
    for (int i = 7; i >= 0; --i) {
        value = (value << 8) | p[i];
    }
    return value;
}

// etc1.cpp: the modifier table, indexed by [table][bit of the low half].
constexpr u8 kEtc1Modifier[8][2] = {{2, 8},   {5, 17},  {9, 29},  {13, 42},
                                    {18, 60}, {24, 80}, {33, 106}, {47, 183}};

// SampleETC1Subtile / ETC1Tile::GetRGB: texel (x, y) of a 4x4 block, x and y below 4.
// The rasterizer cache decodes the same blocks in DecodeETC1Block (texture_codec.h); both read
// the block the same way: bits 0-15 the low half of the modifier index, 16-31 its sign, bit 32
// the flip, bit 33 differential mode, 34-36 and 37-39 the two tables, 40-63 the base colours.
Rgba SampleEtc1(u64 block, u32 x, u32 y) {
    const u32 texel = 4 * x + y;
    if ((block >> 32) & 1) {
        std::swap(x, y); // flip: the two halves are top and bottom
    }

    const bool differential = ((block >> 33) & 1) != 0;
    int rgb[3];
    constexpr u32 kShift[3] = {56, 48, 40}; // r, g, b
    for (int c = 0; c < 3; ++c) {
        const u32 sh = kShift[c];
        if (differential) {
            // 5-bit base in bits sh+3.., 3-bit signed delta in bits sh..sh+2
            int value = static_cast<int>((block >> (sh + 3)) & 0x1F);
            if (x >= 2) {
                const int delta = static_cast<int>((block >> sh) & 7);
                value += delta >= 4 ? delta - 8 : delta;
            }
            rgb[c] = Convert5To8(static_cast<u8>(value));
        } else {
            const u32 nibble = x < 2 ? static_cast<u32>((block >> (sh + 4)) & 0xF)
                                     : static_cast<u32>((block >> sh) & 0xF);
            rgb[c] = Convert4To8(static_cast<u8>(nibble));
        }
    }

    const u32 table = static_cast<u32>((block >> (x < 2 ? 37 : 34)) & 7);
    int modifier = kEtc1Modifier[table][(block >> texel) & 1];
    if (((block >> (16 + texel)) & 1) != 0) {
        modifier = -modifier;
    }
    return {static_cast<u8>(std::clamp(rgb[0] + modifier, 0, 255)),
            static_cast<u8>(std::clamp(rgb[1] + modifier, 0, 255)),
            static_cast<u8>(std::clamp(rgb[2] + modifier, 0, 255)), 255};
}

// A whole 4x4 ETC1 block into `out` (row y, column x at out + y * stride + x * 4), with its alpha block (ETC1A4) or
// opaque: the texels of SampleEtc1 and LookupTexelInTile, with the block's two base colours and tables worked out once
// instead of for each of its 16 texels (an area's load decodes dozens of textures in one frame).
void DecodeEtc1Block(u64 block, const u8* alpha, u8* out, std::size_t stride) {
    const bool flip = ((block >> 32) & 1) != 0;
    const bool differential = ((block >> 33) & 1) != 0;
    int base[2][3];
    constexpr u32 kShift[3] = {56, 48, 40};  // r, g, b
    for (int c = 0; c < 3; ++c) {
        const u32 sh = kShift[c];
        if (differential) {
            const int value = static_cast<int>((block >> (sh + 3)) & 0x1F);
            const int delta = static_cast<int>((block >> sh) & 7);
            base[0][c] = Convert5To8(static_cast<u8>(value));
            base[1][c] = Convert5To8(static_cast<u8>(value + (delta >= 4 ? delta - 8 : delta)));
        } else {
            base[0][c] = Convert4To8(static_cast<u8>((block >> (sh + 4)) & 0xF));
            base[1][c] = Convert4To8(static_cast<u8>((block >> sh) & 0xF));
        }
    }
    const u32 table[2] = {static_cast<u32>((block >> 37) & 7), static_cast<u32>((block >> 34) & 7)};
    const u64 alpha_bits = alpha != nullptr ? Load64(alpha) : 0;
    for (u32 y = 0; y < 4; ++y) {
        u8* dst = out + y * stride;
        for (u32 x = 0; x < 4; ++x, dst += 4) {
            const u32 texel = 4 * x + y;
            const int half = (flip ? y : x) >= 2 ? 1 : 0;
            int modifier = kEtc1Modifier[table[half]][(block >> texel) & 1];
            if (((block >> (16 + texel)) & 1) != 0) {
                modifier = -modifier;
            }
            dst[0] = static_cast<u8>(std::clamp(base[half][0] + modifier, 0, 255));
            dst[1] = static_cast<u8>(std::clamp(base[half][1] + modifier, 0, 255));
            dst[2] = static_cast<u8>(std::clamp(base[half][2] + modifier, 0, 255));
            dst[3] = alpha != nullptr ? Convert4To8(static_cast<u8>((alpha_bits >> (4 * (x * 4 + y))) & 0xF)) : 255;
        }
    }
}

// True for the formats whose bytes the old-style hash keeps as they are (the "converted" ones of
// texture_codec.h's tables, left unconverted).
constexpr bool KeepsNativeBytes(Format format) {
    return format == Format::RGBA8 || format == Format::RGB8 || format == Format::RGB5A1 ||
           format == Format::RGB565 || format == Format::RGBA4;
}

} // namespace

Rgba LookupTexelInTile(const u8* source, u32 x, u32 y, Format format, bool disable_alpha) {
    switch (format) {
    case Format::RGBA8: {
        // DecodeRGBA8: the bytes are a, b, g, r
        const u8* p = source + MortonInterleave(x, y) * 4;
        return {p[3], p[2], p[1], static_cast<u8>(disable_alpha ? 255 : p[0])};
    }
    case Format::RGB8: {
        const u8* p = source + MortonInterleave(x, y) * 3;
        return {p[2], p[1], p[0], 255};
    }
    case Format::RGB5A1: {
        const u16 pixel = Load16(source + MortonInterleave(x, y) * 2);
        return {Convert5To8((pixel >> 11) & 0x1F), Convert5To8((pixel >> 6) & 0x1F),
                Convert5To8((pixel >> 1) & 0x1F),
                static_cast<u8>(disable_alpha ? 255 : Convert1To8(pixel & 1))};
    }
    case Format::RGB565: {
        const u16 pixel = Load16(source + MortonInterleave(x, y) * 2);
        return {Convert5To8((pixel >> 11) & 0x1F), Convert6To8((pixel >> 5) & 0x3F),
                Convert5To8(pixel & 0x1F), 255};
    }
    case Format::RGBA4: {
        const u16 pixel = Load16(source + MortonInterleave(x, y) * 2);
        return {Convert4To8((pixel >> 12) & 0xF), Convert4To8((pixel >> 8) & 0xF),
                Convert4To8((pixel >> 4) & 0xF),
                static_cast<u8>(disable_alpha ? 255 : Convert4To8(pixel & 0xF))};
    }
    case Format::IA8: {
        const u8* p = source + MortonInterleave(x, y) * 2;
        if (disable_alpha) {
            return {p[1], p[0], 0, 255}; // intensity as red, alpha as green
        }
        return {p[1], p[1], p[1], p[0]};
    }
    case Format::RG8: {
        const u8* p = source + MortonInterleave(x, y) * 2;
        return {p[1], p[0], 0, 255};
    }
    case Format::I8: {
        const u8 i = source[MortonInterleave(x, y)];
        return {i, i, i, 255};
    }
    case Format::A8: {
        const u8 a = source[MortonInterleave(x, y)];
        if (disable_alpha) {
            return {a, a, a, 255};
        }
        return {0, 0, 0, a};
    }
    case Format::IA4: {
        const u8 byte = source[MortonInterleave(x, y)];
        const u8 i = Convert4To8((byte & 0xF0) >> 4);
        const u8 a = Convert4To8(byte & 0xF);
        if (disable_alpha) {
            return {i, a, 0, 255};
        }
        return {i, i, i, a};
    }
    case Format::I4: {
        const u32 morton = MortonInterleave(x, y);
        const u8 byte = source[morton / 2];
        const u8 i = Convert4To8((morton % 2) ? ((byte & 0xF0) >> 4) : (byte & 0xF));
        return {i, i, i, 255};
    }
    case Format::A4: {
        const u32 morton = MortonInterleave(x, y);
        const u8 byte = source[morton / 2];
        const u8 a = Convert4To8((morton % 2) ? ((byte & 0xF0) >> 4) : (byte & 0xF));
        if (disable_alpha) {
            return {a, a, a, 255};
        }
        return {0, 0, 0, a};
    }
    case Format::ETC1:
    case Format::ETC1A4: {
        const bool has_alpha = format == Format::ETC1A4;
        const std::size_t subtile_size = has_alpha ? 16 : 8;
        // ETC1 divides each 8x8 tile into four 4x4 subtiles, in this order: top-left, top-right,
        // bottom-left, bottom-right (y = 0 first in memory)
        const u32 subtile_index = (x / 4) + 2 * (y / 4);
        x %= 4;
        y %= 4;
        const u8* subtile = source + subtile_index * subtile_size;
        u8 alpha = 255;
        if (has_alpha) {
            alpha = Convert4To8(static_cast<u8>((Load64(subtile) >> (4 * (x * 4 + y))) & 0xF));
            subtile += 8;
        }
        Rgba rgb = SampleEtc1(Load64(subtile), x, y);
        rgb.a = disable_alpha ? 255 : alpha;
        return rgb;
    }
    }
    return {};
}

Rgba LookupTexture(const u8* source, u32 x, u32 y, u32 width, Format format, bool disable_alpha) {
    const std::size_t stride = TileBytes(format) * (width / 8);
    const u8* tile = source + (y / 8) * stride + (x / 8) * TileBytes(format);
    return LookupTexelInTile(tile, x % 8, y % 8, format, disable_alpha);
}

bool DecodeToRgba8(const u8* source, std::size_t size, u32 width, u32 height, Format format,
                   std::vector<u8>& out) {
    if (!IsValidSize(width, height) || size < EncodedBytes(format, width, height)) {
        return false;
    }
    out.resize(static_cast<std::size_t>(width) * height * 4);
    // Tile by tile with the format fixed at compile time: the same texels as LookupTexture, without its
    // per-texel tile arithmetic and format switch (textures are decoded again whenever their memory changes,
    // a video's every frame).
    const std::size_t tile_bytes = TileBytes(format);
    const std::size_t stride = tile_bytes * (width / 8);
    // the rows of tiles, on DecodePool's threads too for a big texture
    const auto rows_of_tiles = [&](const std::function<void(u32, u32)>& fn) {
        const bool etc = format == Format::ETC1 || format == Format::ETC1A4;
        if (width * height >= (etc ? kParallelTexelsEtc : kParallelTexels)) {
            DecodePool::Get().For(height / 8, fn);
        } else {
            fn(0, height / 8);
        }
    };
    const auto decode = [&]<Format F>() {
        rows_of_tiles([&](u32 first, u32 last) {
            for (u32 ty = first * 8; ty < last * 8; ty += 8) {
                for (u32 tx = 0; tx < width; tx += 8) {
                    const u8* tile = source + (ty / 8) * stride + (tx / 8) * tile_bytes;
                    for (u32 y = 0; y < 8; ++y) {
                        u8* dst = out.data() + (static_cast<std::size_t>(ty + y) * width + tx) * 4;
                        for (u32 x = 0; x < 8; ++x) {
                            const Rgba texel = LookupTexelInTile(tile, x, y, F, false);
                            dst[0] = texel.r;
                            dst[1] = texel.g;
                            dst[2] = texel.b;
                            dst[3] = texel.a;
                            dst += 4;
                        }
                    }
                }
            }
        });
    };
    static const bool no_fast = std::getenv("MH4N_NO_FAST") != nullptr;  // (A/B checks of the fast path)
    if (!no_fast && (format == Format::ETC1 || format == Format::ETC1A4)) {
        // a block at a time: an 8x8 tile holds four, top-left, top-right, bottom-left, bottom-right
        const bool has_alpha = format == Format::ETC1A4;
        const std::size_t block_bytes = has_alpha ? 16 : 8;
        const std::size_t row = static_cast<std::size_t>(width) * 4;
        rows_of_tiles([&](u32 first, u32 last) {
            for (u32 ty = first * 8; ty < last * 8; ty += 8) {
                for (u32 tx = 0; tx < width; tx += 8) {
                    const u8* tile = source + (ty / 8) * stride + (tx / 8) * tile_bytes;
                    for (u32 b = 0; b < 4; ++b) {
                        const u8* p = tile + b * block_bytes;
                        u8* out_block = out.data() + static_cast<std::size_t>(ty + (b / 2) * 4) * row + (tx + (b % 2) * 4) * 4;
                        DecodeEtc1Block(Load64(has_alpha ? p + 8 : p), has_alpha ? p : nullptr, out_block, row);
                    }
                }
            }
        });
        return true;
    }
    switch (no_fast ? Format(0xFF) : format) {
    case Format::RGBA8: decode.template operator()<Format::RGBA8>(); break;
    case Format::RGB8: decode.template operator()<Format::RGB8>(); break;
    case Format::RGB5A1: decode.template operator()<Format::RGB5A1>(); break;
    case Format::RGB565: decode.template operator()<Format::RGB565>(); break;
    case Format::RGBA4: decode.template operator()<Format::RGBA4>(); break;
    case Format::IA8: decode.template operator()<Format::IA8>(); break;
    case Format::RG8: decode.template operator()<Format::RG8>(); break;
    case Format::I8: decode.template operator()<Format::I8>(); break;
    case Format::A8: decode.template operator()<Format::A8>(); break;
    case Format::IA4: decode.template operator()<Format::IA4>(); break;
    case Format::I4: decode.template operator()<Format::I4>(); break;
    case Format::A4: decode.template operator()<Format::A4>(); break;
    case Format::ETC1: decode.template operator()<Format::ETC1>(); break;
    case Format::ETC1A4: decode.template operator()<Format::ETC1A4>(); break;
    default:
        for (u32 y = 0; y < height; ++y) {
            u8* dst = out.data() + static_cast<std::size_t>(y) * width * 4;
            for (u32 x = 0; x < width; ++x, dst += 4) {
                const Rgba texel = LookupTexture(source, x, y, width, format);
                dst[0] = texel.r;
                dst[1] = texel.g;
                dst[2] = texel.b;
                dst[3] = texel.a;
            }
        }
        break;
    }
    return true;
}

bool DecodeForHash(const u8* source, std::size_t size, u32 width, u32 height, Format format,
                   std::vector<u8>& out) {
    if (!IsValidSize(width, height) || size < EncodedBytes(format, width, height)) {
        return false;
    }
    const u32 bpp = HashBytesPerTexel(format);
    out.resize(static_cast<std::size_t>(width) * height * bpp);
    const std::size_t stride = TileBytes(format) * (width / 8);
    for (u32 y = 0; y < height; ++y) {
        // MortonCopy writes the linear buffer from the bottom: memory row y is output row
        // height - 1 - y.
        u8* dst = out.data() + static_cast<std::size_t>(height - 1 - y) * width * bpp;
        for (u32 x = 0; x < width; ++x, dst += bpp) {
            if (KeepsNativeBytes(format)) {
                // DecodePixel with converted = false: the texel's bytes, unchanged
                const u8* tile = source + (y / 8) * stride + (x / 8) * TileBytes(format);
                std::memcpy(dst, tile + MortonInterleave(x % 8, y % 8) * bpp, bpp);
            } else {
                const Rgba texel = LookupTexture(source, x, y, width, format);
                dst[0] = texel.r;
                dst[1] = texel.g;
                dst[2] = texel.b;
                dst[3] = texel.a;
            }
        }
    }
    return true;
}

} // namespace mh4n::gpu::texture
