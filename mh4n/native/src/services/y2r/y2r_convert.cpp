// Derived from Azahar src/core/hw/y2r.cpp and src/common/color.h at
// 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-2.0-or-later: each of them says "GPLv2 or any later
// version"); see y2r_convert.h.
// Copyright 2015 Citra Emulator Project (y2r.cpp)
// Copyright 2014 Citra Emulator Project (color.h)
#include "y2r_convert.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "mh4n/log.h"

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace mh4n::y2r {

namespace {

constexpr std::size_t kMaxTiles = 1024 / 8;
constexpr std::size_t kTileSize = 8 * 8;
using ImageTile = std::array<u32, kTileSize>;

// Converts an image strip from the source YUV format into individual 8x8 RGB32 tiles.
void ConvertYuvToRgb(InputFormat format, const u8* input_y, const u8* input_u, const u8* input_v,
                     ImageTile output[], unsigned width, unsigned height,
                     const CoefficientSet& coefficients) {
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            s32 Y, U, V;
            if (format == InputFormat::YUV422_Indiv8 || format == InputFormat::YUV422_Indiv16) {
                Y = input_y[y * width + x];
                U = input_u[(y * width + x) / 2];
                V = input_v[(y * width + x) / 2];
            } else if (format == InputFormat::YUV420_Indiv8 || format == InputFormat::YUV420_Indiv16) {
                Y = input_y[y * width + x];
                U = input_u[((y / 2) * width + x) / 2];
                V = input_v[((y / 2) * width + x) / 2];
            } else {  // YUYV422_Interleaved
                Y = input_y[(y * width + x) * 2];
                U = input_y[(y * width + (x / 2) * 2) * 2 + 1];
                V = input_y[(y * width + (x / 2) * 2) * 2 + 3];
            }

            // This conversion process is bit-exact with hardware, as far as could be tested.
            const auto& c = coefficients;
            const s32 cY = c[0] * Y;

            s32 r = cY + c[1] * V;
            s32 g = cY - c[2] * V - c[3] * U;
            s32 b = cY + c[4] * U;

            const s32 rounding_offset = 0x18;
            r = (r >> 3) + c[5] + rounding_offset;
            g = (g >> 3) + c[6] + rounding_offset;
            b = (b >> 3) + c[7] + rounding_offset;

            const unsigned tile = x / 8;
            const unsigned tile_x = x % 8;
            u32* out = &output[tile][y * 8 + tile_x];
            *out = (static_cast<u32>(std::clamp(r >> 5, 0, 0xFF)) << 24) |
                   (static_cast<u32>(std::clamp(g >> 5, 0, 0xFF)) << 16) |
                   (static_cast<u32>(std::clamp(b >> 5, 0, 0xFF)) << 8);
        }
    }
}

// Simulates an incoming CDMA transfer. `n` converts 16-bit formats to 8-bit. False when the
// transfer cannot be simulated.
bool ReceiveData(GuestMemory& memory, u8* output, ConversionBuffer& buf, std::size_t n,
                 std::size_t amount_of_data) {
    const std::size_t output_unit = buf.transfer_unit / n;
    if (output_unit == 0 || amount_of_data % output_unit != 0) {
        MH4N_LOG(Error, "Service.Y2R", "transfer unit {} does not divide the data ({})", buf.transfer_unit,
                 amount_of_data);
        return false;
    }
    std::vector<u8> unit(buf.transfer_unit);
    while (amount_of_data > 0) {
        if (!memory.ReadBlock(unit.data(), buf.address, buf.transfer_unit)) {
            std::fill(unit.begin(), unit.end(), u8{0});
        }
        for (std::size_t i = 0; i < output_unit; ++i) {
            output[i] = unit[i * n];
        }
        output += output_unit;
        buf.address += buf.transfer_unit + buf.gap;
        buf.image_size -= buf.transfer_unit;
        amount_of_data -= output_unit;
    }
    return true;
}

constexpr u8 To5(u8 v) { return v >> 3; }
constexpr u8 To6(u8 v) { return v >> 2; }
constexpr u8 To1(u8 v) { return v >> 7; }

// Converts the intermediate RGB32 format to the final output format while simulating an outgoing CDMA
// transfer.
bool SendData(GuestMemory& memory, OutputFormat format, const u32* input, ConversionBuffer& buf,
              int amount_of_data, u8 alpha) {
    // Azahar never ends with a unit of 0 (the inner loop adds no pixel and the outer one spins forever).
    if (buf.transfer_unit == 0) {
        MH4N_LOG(Error, "Service.Y2R", "the output transfer unit is 0");
        return false;
    }
    std::vector<u8> unit;
    while (amount_of_data > 0) {
        unit.clear();
        // Azahar converts a full unit even when the strip has fewer pixels left: it reads past the pixel
        // buffer. Here the pixels the strip has are converted and the rest of the unit is zero bytes, so
        // the bytes written and the address advance are the same without reading past the buffer.
        while (unit.size() < buf.transfer_unit && amount_of_data > 0) {
            const u32 color = *input++;
            const u8 r = static_cast<u8>(color >> 24), g = static_cast<u8>(color >> 16),
                     b = static_cast<u8>(color >> 8), a = alpha;
            switch (format) {
            case OutputFormat::RGBA8:
                unit.insert(unit.end(), {a, b, g, r});
                break;
            case OutputFormat::RGB8:
                unit.insert(unit.end(), {b, g, r});
                break;
            case OutputFormat::RGB5A1: {
                const u16 data = static_cast<u16>((To5(r) << 11) | (To5(g) << 6) | (To5(b) << 1) | To1(a));
                unit.push_back(static_cast<u8>(data));
                unit.push_back(static_cast<u8>(data >> 8));
                break;
            }
            case OutputFormat::RGB565: {
                const u16 data = static_cast<u16>((To5(r) << 11) | (To6(g) << 5) | To5(b));
                unit.push_back(static_cast<u8>(data));
                unit.push_back(static_cast<u8>(data >> 8));
                break;
            }
            }
            amount_of_data -= 1;
        }
        if (unit.size() < buf.transfer_unit) unit.resize(buf.transfer_unit, u8{0});
        memory.WriteBlock(buf.address, unit.data(), static_cast<u32>(unit.size()));
        buf.address += buf.transfer_unit + buf.gap;
        buf.image_size -= buf.transfer_unit;
    }
    return true;
}

constexpr std::array<u8, kTileSize> kLinearLut = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
    44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63};

constexpr std::array<u8, kTileSize> kMortonLut = {
    0,  1,  4,  5,  16, 17, 20, 21, 2,  3,  6,  7,  18, 19, 22, 23, 8,  9,  12, 13, 24, 25,
    28, 29, 10, 11, 14, 15, 26, 27, 30, 31, 32, 33, 36, 37, 48, 49, 52, 53, 34, 35, 38, 39,
    50, 51, 54, 55, 40, 41, 44, 45, 56, 57, 60, 61, 42, 43, 46, 47, 58, 59, 62, 63};

void RotateTile0(const ImageTile& input, ImageTile& output, int height, const u8* out_map) {
    for (int i = 0; i < height * 8; ++i) {
        output[out_map[i]] = input[static_cast<std::size_t>(i)];
    }
}
void RotateTile90(const ImageTile& input, ImageTile& output, int height, const u8* out_map) {
    int out_i = 0;
    for (int x = 0; x < 8; ++x) {
        for (int y = height - 1; y >= 0; --y) {
            output[out_map[out_i++]] = input[static_cast<std::size_t>(y * 8 + x)];
        }
    }
}
void RotateTile180(const ImageTile& input, ImageTile& output, int height, const u8* out_map) {
    int out_i = 0;
    for (int i = height * 8 - 1; i >= 0; --i) {
        output[out_map[out_i++]] = input[static_cast<std::size_t>(i)];
    }
}
void RotateTile270(const ImageTile& input, ImageTile& output, int height, const u8* out_map) {
    int out_i = 0;
    for (int x = 8 - 1; x >= 0; --x) {
        for (int y = 0; y < height; ++y) {
            output[out_map[out_i++]] = input[static_cast<std::size_t>(y * 8 + x)];
        }
    }
}
void WriteTileToOutput(u32* output, const ImageTile& tile, int height, int line_stride) {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < 8; ++x) {
            output[y * line_stride + x] = tile[static_cast<std::size_t>(y * 8 + x)];
        }
    }
}

}  // namespace

bool PerformConversionReference(GuestMemory& memory, ConversionConfiguration cvt) {
    if (cvt.input_line_width % 8 != 0 || cvt.input_line_width == 0 ||
        (cvt.block_alignment == BlockAlignment::Block8x8 && cvt.input_lines % 8 != 0)) {
        MH4N_LOG(Error, "Service.Y2R", "invalid line width {} or lines {}", cvt.input_line_width,
                 cvt.input_lines);
        return false;
    }
    const std::size_t num_tiles = cvt.input_line_width / 8;
    if (num_tiles > kMaxTiles) return false;

    // Buffer used as a CDMA source/target.
    std::vector<u8> data_buffer(static_cast<std::size_t>(cvt.input_line_width) * 8 * 4, 0);
    // Intermediate storage for decoded 8x8 image tiles. Always stored as RGB32.
    std::vector<ImageTile> tiles(num_tiles);
    ImageTile tmp_tile{};

    const u8* tile_remap = cvt.block_alignment == BlockAlignment::Linear ? kLinearLut.data() : kMortonLut.data();

    for (unsigned y = 0; y < cvt.input_lines; y += 8) {
        const unsigned row_height = std::min<unsigned>(cvt.input_lines - y, 8u);
        const std::size_t row_data_size = static_cast<std::size_t>(row_height) * cvt.input_line_width;

        u8* input_y = data_buffer.data();
        u8* input_u = input_y + 8 * cvt.input_line_width;
        u8* input_v = input_u + 8 * cvt.input_line_width / 2;

        bool ok = true;
        switch (cvt.input_format) {
        case InputFormat::YUV422_Indiv8:
            ok = ReceiveData(memory, input_y, cvt.src_Y, 1, row_data_size) &&
                 ReceiveData(memory, input_u, cvt.src_U, 1, row_data_size / 2) &&
                 ReceiveData(memory, input_v, cvt.src_V, 1, row_data_size / 2);
            break;
        case InputFormat::YUV420_Indiv8:
            ok = ReceiveData(memory, input_y, cvt.src_Y, 1, row_data_size) &&
                 ReceiveData(memory, input_u, cvt.src_U, 1, row_data_size / 4) &&
                 ReceiveData(memory, input_v, cvt.src_V, 1, row_data_size / 4);
            break;
        case InputFormat::YUV422_Indiv16:
            ok = ReceiveData(memory, input_y, cvt.src_Y, 2, row_data_size) &&
                 ReceiveData(memory, input_u, cvt.src_U, 2, row_data_size / 2) &&
                 ReceiveData(memory, input_v, cvt.src_V, 2, row_data_size / 2);
            break;
        case InputFormat::YUV420_Indiv16:
            ok = ReceiveData(memory, input_y, cvt.src_Y, 2, row_data_size) &&
                 ReceiveData(memory, input_u, cvt.src_U, 2, row_data_size / 4) &&
                 ReceiveData(memory, input_v, cvt.src_V, 2, row_data_size / 4);
            break;
        case InputFormat::YUYV422_Interleaved:
            input_u = nullptr;
            input_v = nullptr;
            ok = ReceiveData(memory, input_y, cvt.src_YUYV, 1, row_data_size * 2);
            break;
        default:
            return false;
        }
        if (!ok) return false;
        ConvertYuvToRgb(cvt.input_format, input_y, input_u, input_v, tiles.data(), cvt.input_line_width,
                        row_height, cvt.coefficients);

        u32* output_buffer = reinterpret_cast<u32*>(data_buffer.data());
        for (std::size_t i = 0; i < num_tiles; ++i) {
            int image_strip_width = 0;
            int output_stride = 0;
            switch (cvt.rotation) {
            case Rotation::None:
                RotateTile0(tiles[i], tmp_tile, static_cast<int>(row_height), tile_remap);
                image_strip_width = cvt.input_line_width;
                output_stride = 8;
                break;
            case Rotation::Clockwise_90:
                RotateTile90(tiles[i], tmp_tile, static_cast<int>(row_height), tile_remap);
                image_strip_width = 8;
                output_stride = 8 * static_cast<int>(row_height);
                break;
            case Rotation::Clockwise_180:
                // For 180 and 270 degree rotations the order of the tiles in the strip is inverted too
                RotateTile180(tiles[num_tiles - i - 1], tmp_tile, static_cast<int>(row_height), tile_remap);
                image_strip_width = cvt.input_line_width;
                output_stride = 8;
                break;
            case Rotation::Clockwise_270:
                RotateTile270(tiles[num_tiles - i - 1], tmp_tile, static_cast<int>(row_height), tile_remap);
                image_strip_width = 8;
                output_stride = 8 * static_cast<int>(row_height);
                break;
            }
            switch (cvt.block_alignment) {
            case BlockAlignment::Linear:
                WriteTileToOutput(output_buffer, tmp_tile, static_cast<int>(row_height), image_strip_width);
                output_buffer += output_stride;
                break;
            case BlockAlignment::Block8x8:
                WriteTileToOutput(output_buffer, tmp_tile, 8, 8);
                output_buffer += kTileSize;
                break;
            }
        }

        if (!SendData(memory, cvt.output_format, reinterpret_cast<const u32*>(data_buffer.data()), cvt.dst,
                      static_cast<int>(row_data_size), static_cast<u8>(cvt.alpha))) {
            return false;
        }
    }
    return true;
}


// ---- the fast way (1.0.2): the same bytes in the same places as PerformConversionReference ---------------------
// MH4U converts a YUV image with y2r:u every few frames, and the reference way (Azahar's) took about a tenth of the
// game thread of a Moto g06 in a field: a format test per pixel, a vector insert per output pixel, and a locked
// ReadBlock or WriteBlock per transfer unit. Here the format is chosen per row, the output units are made in one
// buffer, and a transfer whose whole span is mapped reads and writes guest memory straight (the scheduler thread's
// view, as ReadBlock and WriteBlock use under their lock); a span that is not all mapped goes unit by unit as before.
// MH4N_Y2R_FAST=0: the reference way.

namespace {

// Eight pixels of a planar row (pixel x takes u[x / 2] and v[x / 2]), the hardware formula as in ConvertYuvToRgb
struct Coeffs {
    s32 c0, c1, c2, c3, c4, o5, o6, o7;
};
inline u32 Pixel(s32 Y, s32 U, s32 V, const Coeffs& k) {
    const s32 cY = k.c0 * Y;
    s32 r = cY + k.c1 * V;
    s32 g = cY - k.c2 * V - k.c3 * U;
    s32 b = cY + k.c4 * U;
    r = ((r >> 3) + k.o5) >> 5;
    g = ((g >> 3) + k.o6) >> 5;
    b = ((b >> 3) + k.o7) >> 5;
    return (static_cast<u32>(std::clamp(r, 0, 0xFF)) << 24) | (static_cast<u32>(std::clamp(g, 0, 0xFF)) << 16) |
           (static_cast<u32>(std::clamp(b, 0, 0xFF)) << 8);
}
#if defined(__aarch64__)
// (the same integer operations, four lanes at a time: the products fit 32 bits, the shifts are arithmetic as in C++)
inline void Planar8(const u8* y, const u8* u, const u8* v, u32* out, const Coeffs& k) {
    u32 u4, v4;
    std::memcpy(&u4, u, 4);
    std::memcpy(&v4, v, 4);
    uint8x8_t U8 = vreinterpret_u8_u32(vdup_n_u32(u4));
    uint8x8_t V8 = vreinterpret_u8_u32(vdup_n_u32(v4));
    U8 = vzip1_u8(U8, U8);  // (u0 u0 u1 u1 u2 u2 u3 u3)
    V8 = vzip1_u8(V8, V8);
    const int16x8_t Y16 = vreinterpretq_s16_u16(vmovl_u8(vld1_u8(y)));
    const int16x8_t U16 = vreinterpretq_s16_u16(vmovl_u8(U8));
    const int16x8_t V16 = vreinterpretq_s16_u16(vmovl_u8(V8));
    const int32x4_t lo = vdupq_n_s32(0), hi = vdupq_n_s32(0xFF);
    for (int half = 0; half < 2; ++half) {
        const int32x4_t Yv = half ? vmovl_high_s16(Y16) : vmovl_s16(vget_low_s16(Y16));
        const int32x4_t Uv = half ? vmovl_high_s16(U16) : vmovl_s16(vget_low_s16(U16));
        const int32x4_t Vv = half ? vmovl_high_s16(V16) : vmovl_s16(vget_low_s16(V16));
        const int32x4_t cY = vmulq_n_s32(Yv, k.c0);
        int32x4_t r = vmlaq_n_s32(cY, Vv, k.c1);
        int32x4_t g = vmlsq_n_s32(vmlsq_n_s32(cY, Vv, k.c2), Uv, k.c3);
        int32x4_t b = vmlaq_n_s32(cY, Uv, k.c4);
        r = vshrq_n_s32(vaddq_s32(vshrq_n_s32(r, 3), vdupq_n_s32(k.o5)), 5);
        g = vshrq_n_s32(vaddq_s32(vshrq_n_s32(g, 3), vdupq_n_s32(k.o6)), 5);
        b = vshrq_n_s32(vaddq_s32(vshrq_n_s32(b, 3), vdupq_n_s32(k.o7)), 5);
        r = vmaxq_s32(vminq_s32(r, hi), lo);
        g = vmaxq_s32(vminq_s32(g, hi), lo);
        b = vmaxq_s32(vminq_s32(b, hi), lo);
        const uint32x4_t px = vorrq_u32(vorrq_u32(vshlq_n_u32(vreinterpretq_u32_s32(r), 24), vshlq_n_u32(vreinterpretq_u32_s32(g), 16)),
                                        vshlq_n_u32(vreinterpretq_u32_s32(b), 8));
        vst1q_u32(out + 4 * half, px);
    }
}
#else
inline void Planar8(const u8* y, const u8* u, const u8* v, u32* out, const Coeffs& k) {
    for (unsigned x = 0; x < 8; ++x) out[x] = Pixel(y[x], u[x / 2], v[x / 2], k);
}
#endif

void ConvertRowsFast(InputFormat format, const u8* input_y, const u8* input_u, const u8* input_v, ImageTile output[],
                     unsigned width, unsigned height, const CoefficientSet& c) {
    const Coeffs k{c[0], c[1], c[2], c[3], c[4], c[5] + 0x18, c[6] + 0x18, c[7] + 0x18};
    const bool yuyv = format == InputFormat::YUYV422_Interleaved;
    const bool four20 = format == InputFormat::YUV420_Indiv8 || format == InputFormat::YUV420_Indiv16;
    for (unsigned y = 0; y < height; ++y) {
        if (yuyv) {
            const u8* yrow = input_y + y * width * 2;  // (Y U Y V: a pair of pixels in 4 bytes)
            for (unsigned tile = 0; tile < width / 8; ++tile) {
                u32* out = &output[tile][y * 8];
                for (unsigned tx = 0; tx < 8; ++tx) {
                    const unsigned x = tile * 8 + tx;
                    out[tx] = Pixel(yrow[x * 2], yrow[(x / 2) * 4 + 1], yrow[(x / 2) * 4 + 3], k);
                }
            }
            continue;
        }
        const u8* yrow = input_y + y * width;
        const u8* urow = input_u + ((four20 ? y / 2 : y) * width) / 2;
        const u8* vrow = input_v + ((four20 ? y / 2 : y) * width) / 2;
        for (unsigned tile = 0; tile < width / 8; ++tile) {
            Planar8(yrow + tile * 8, urow + tile * 4, vrow + tile * 4, &output[tile][y * 8], k);
        }
    }
}

// The bytes a transfer of `units` units touches from its address, when that fits the 32-bit address space (else 0)
u64 SpanOf(const ConversionBuffer& buf, u64 units, u64 last_unit_bytes) {
    if (units == 0) return 0;
    const u64 span = (units - 1) * (u64{buf.transfer_unit} + buf.gap) + last_unit_bytes;
    return u64{buf.address} + span <= 0x100000000ull && span <= 0xFFFFFFFFull ? span : 0;
}

bool ReceiveDataFast(GuestMemory& memory, u8* output, ConversionBuffer& buf, std::size_t n, std::size_t amount_of_data) {
    const std::size_t output_unit = buf.transfer_unit / n;
    if (output_unit == 0 || amount_of_data % output_unit != 0) {
        return ReceiveData(memory, output, buf, n, amount_of_data);  // (logs and fails as it did)
    }
    const u64 units = amount_of_data / output_unit;
    const u64 span = SpanOf(buf, units, buf.transfer_unit);
    if (span == 0 || !memory.IsMapped(buf.address, static_cast<u32>(span))) {
        return ReceiveData(memory, output, buf, n, amount_of_data);
    }
    const u8* src = memory.Base() + buf.address;
    const u64 stride = u64{buf.transfer_unit} + buf.gap;
    for (u64 k = 0; k < units; ++k) {
        if (n == 1) {
            std::memcpy(output, src, output_unit);
        } else {
            for (std::size_t i = 0; i < output_unit; ++i) output[i] = src[i * n];
        }
        output += output_unit;
        src += stride;
    }
    buf.address = static_cast<u32>(buf.address + units * stride);
    buf.image_size = static_cast<u32>(buf.image_size - units * buf.transfer_unit);
    return true;
}

bool SendDataFast(GuestMemory& memory, OutputFormat format, const u32* input, ConversionBuffer& buf, int amount_of_data,
                  u8 alpha, std::vector<u8>& staging, std::vector<u32>& sizes) {
    if (buf.transfer_unit == 0) {
        return SendData(memory, format, input, buf, amount_of_data, alpha);  // (logs and fails as it did)
    }
    const std::size_t bpp = format == OutputFormat::RGBA8 ? 4 : format == OutputFormat::RGB8 ? 3 : 2;
    const std::size_t unit = buf.transfer_unit;
    // a unit takes pixels until it has at least `unit` bytes (it may run over), the strip's last one fewer, padded with
    // zero bytes to `unit`
    const std::size_t per_unit = (unit + bpp - 1) / bpp;
    const std::size_t units = amount_of_data > 0 ? (static_cast<std::size_t>(amount_of_data) + per_unit - 1) / per_unit : 0;
    const std::size_t unit_bytes = std::max(unit, per_unit * bpp);
    staging.resize(units * unit_bytes);
    sizes.resize(units);
    for (std::size_t k = 0; k < units; ++k) {
        u8* p = staging.data() + k * unit_bytes;
        std::size_t used = 0;
        // (the unit's pixels: as many as reach `unit` bytes, fewer if the strip ends)
        const std::size_t take = std::min<std::size_t>(per_unit, static_cast<std::size_t>(amount_of_data));
        switch (format) {
        case OutputFormat::RGBA8:  // (bytes a, b, g, r: the little-endian word of the packed colour with the alpha)
            for (std::size_t i = 0; i < take; ++i) {
                const u32 word = input[i] | alpha;
                std::memcpy(p + 4 * i, &word, 4);
            }
            break;
        case OutputFormat::RGB8:
            for (std::size_t i = 0; i < take; ++i) {
                const u32 color = input[i];
                p[3 * i] = static_cast<u8>(color >> 8), p[3 * i + 1] = static_cast<u8>(color >> 16), p[3 * i + 2] = static_cast<u8>(color >> 24);
            }
            break;
        case OutputFormat::RGB5A1:
            for (std::size_t i = 0; i < take; ++i) {
                const u32 color = input[i];
                const u8 r = static_cast<u8>(color >> 24), g = static_cast<u8>(color >> 16), b = static_cast<u8>(color >> 8);
                const u16 data = static_cast<u16>((To5(r) << 11) | (To5(g) << 6) | (To5(b) << 1) | To1(alpha));
                p[2 * i] = static_cast<u8>(data), p[2 * i + 1] = static_cast<u8>(data >> 8);
            }
            break;
        case OutputFormat::RGB565:
            for (std::size_t i = 0; i < take; ++i) {
                const u32 color = input[i];
                const u8 r = static_cast<u8>(color >> 24), g = static_cast<u8>(color >> 16), b = static_cast<u8>(color >> 8);
                const u16 data = static_cast<u16>((To5(r) << 11) | (To6(g) << 5) | To5(b));
                p[2 * i] = static_cast<u8>(data), p[2 * i + 1] = static_cast<u8>(data >> 8);
            }
            break;
        }
        input += take;
        p += take * bpp;
        used = take * bpp;
        amount_of_data -= static_cast<int>(take);
        if (used < unit) {
            std::memset(p, 0, unit - used);
            used = unit;
        }
        sizes[k] = static_cast<u32>(used);
    }
    const u64 span = SpanOf(buf, units, unit_bytes);
    const bool straight = span != 0 && memory.IsMapped(buf.address, static_cast<u32>(span));
    const u64 stride = u64{buf.transfer_unit} + buf.gap;
    u8* const base = memory.Base();
    for (std::size_t k = 0; k < units; ++k) {
        const u8* bytes = staging.data() + k * unit_bytes;
        if (straight) {
            std::memcpy(base + buf.address, bytes, sizes[k]);
        } else {
            memory.WriteBlock(buf.address, bytes, sizes[k]);
        }
        buf.address = static_cast<u32>(buf.address + stride);
        buf.image_size = static_cast<u32>(buf.image_size - buf.transfer_unit);
    }
    return true;
}

// MH4N_Y2R_FAST: unset or 1 the fast way, 0 the reference way, "check" both (the reference after the fast one, over
// the same input, its output compared byte for byte and any difference logged: the arm64 lanes on a phone)
int FastMode() {
    static const int mode = [] {
        const char* v = std::getenv("MH4N_Y2R_FAST");
        if (v == nullptr) return 1;
        if (std::strcmp(v, "check") == 0) return 2;
        return *v == '0' ? 0 : 1;
    }();
    return mode;
}
bool FastOn() { return FastMode() != 0; }

bool PerformConversionFast(GuestMemory& memory, ConversionConfiguration cvt);

}  // namespace

bool PerformConversion(GuestMemory& memory, ConversionConfiguration cvt) {
    if (FastMode() != 2) {
        return PerformConversionFast(memory, cvt);
    }
    // the check: the bytes the conversion may write, after the fast way, then after the reference way from the same
    // starting bytes
    static u64 checked = 0, differed = 0;
    const u64 span = static_cast<u64>(cvt.input_lines) * (u64{cvt.dst.transfer_unit} + cvt.dst.gap) + 8;
    const bool readable = span < (64u << 20) && memory.IsMapped(cvt.dst.address, static_cast<u32>(span));
    std::vector<u8> before, fast;
    if (readable) {
        before.resize(span);
        std::memcpy(before.data(), memory.Base() + cvt.dst.address, span);
    }
    const bool ok_fast = PerformConversionFast(memory, cvt);
    if (!readable) return ok_fast;
    fast.resize(span);
    std::memcpy(fast.data(), memory.Base() + cvt.dst.address, span);
    std::memcpy(memory.Base() + cvt.dst.address, before.data(), span);
    const bool ok_ref = PerformConversionReference(memory, cvt);
    ++checked;
    if (ok_ref != ok_fast || std::memcmp(fast.data(), memory.Base() + cvt.dst.address, span) != 0) {
        if (differed++ < 20) {
            MH4N_LOG(Error, "Service.Y2R", "check: conversion {} differs (format {} -> {}, rotation {}, {}x{})", checked,
                     static_cast<int>(cvt.input_format), static_cast<int>(cvt.output_format), static_cast<int>(cvt.rotation),
                     cvt.input_line_width, cvt.input_lines);
        }
    }
    if ((checked & (checked - 1)) == 0) {
        MH4N_LOG(Info, "Service.Y2R", "check: {} conversions, {} differ", checked, differed);
    }
    return ok_ref;
}

namespace {

bool PerformConversionFast(GuestMemory& memory, ConversionConfiguration cvt) {
    if (!FastOn() || cvt.input_line_width % 8 != 0 || cvt.input_line_width == 0 ||
        (cvt.block_alignment == BlockAlignment::Block8x8 && cvt.input_lines % 8 != 0)) {
        return PerformConversionReference(memory, cvt);  // (an invalid size logs and fails as it did)
    }
    const std::size_t num_tiles = cvt.input_line_width / 8;
    if (num_tiles > kMaxTiles) return false;

    std::vector<u8> data_buffer(static_cast<std::size_t>(cvt.input_line_width) * 8 * 4, 0);
    std::vector<ImageTile> tiles(num_tiles);
    std::vector<u8> staging;
    std::vector<u32> sizes;
    ImageTile tmp_tile{};
    const u8* tile_remap = cvt.block_alignment == BlockAlignment::Linear ? kLinearLut.data() : kMortonLut.data();

    for (unsigned y = 0; y < cvt.input_lines; y += 8) {
        const unsigned row_height = std::min<unsigned>(cvt.input_lines - y, 8u);
        const std::size_t row_data_size = static_cast<std::size_t>(row_height) * cvt.input_line_width;

        u8* input_y = data_buffer.data();
        u8* input_u = input_y + 8 * cvt.input_line_width;
        u8* input_v = input_u + 8 * cvt.input_line_width / 2;

        bool ok = true;
        switch (cvt.input_format) {
        case InputFormat::YUV422_Indiv8:
            ok = ReceiveDataFast(memory, input_y, cvt.src_Y, 1, row_data_size) &&
                 ReceiveDataFast(memory, input_u, cvt.src_U, 1, row_data_size / 2) &&
                 ReceiveDataFast(memory, input_v, cvt.src_V, 1, row_data_size / 2);
            break;
        case InputFormat::YUV420_Indiv8:
            ok = ReceiveDataFast(memory, input_y, cvt.src_Y, 1, row_data_size) &&
                 ReceiveDataFast(memory, input_u, cvt.src_U, 1, row_data_size / 4) &&
                 ReceiveDataFast(memory, input_v, cvt.src_V, 1, row_data_size / 4);
            break;
        case InputFormat::YUV422_Indiv16:
            ok = ReceiveDataFast(memory, input_y, cvt.src_Y, 2, row_data_size) &&
                 ReceiveDataFast(memory, input_u, cvt.src_U, 2, row_data_size / 2) &&
                 ReceiveDataFast(memory, input_v, cvt.src_V, 2, row_data_size / 2);
            break;
        case InputFormat::YUV420_Indiv16:
            ok = ReceiveDataFast(memory, input_y, cvt.src_Y, 2, row_data_size) &&
                 ReceiveDataFast(memory, input_u, cvt.src_U, 2, row_data_size / 4) &&
                 ReceiveDataFast(memory, input_v, cvt.src_V, 2, row_data_size / 4);
            break;
        case InputFormat::YUYV422_Interleaved:
            input_u = nullptr;
            input_v = nullptr;
            ok = ReceiveDataFast(memory, input_y, cvt.src_YUYV, 1, row_data_size * 2);
            break;
        default:
            return false;
        }
        if (!ok) return false;
        ConvertRowsFast(cvt.input_format, input_y, input_u, input_v, tiles.data(), cvt.input_line_width, row_height,
                        cvt.coefficients);

        u32* output_buffer = reinterpret_cast<u32*>(data_buffer.data());
        for (std::size_t i = 0; i < num_tiles; ++i) {
            int image_strip_width = 0;
            int output_stride = 0;
            switch (cvt.rotation) {
            case Rotation::None:
                RotateTile0(tiles[i], tmp_tile, static_cast<int>(row_height), tile_remap);
                image_strip_width = cvt.input_line_width;
                output_stride = 8;
                break;
            case Rotation::Clockwise_90:
                RotateTile90(tiles[i], tmp_tile, static_cast<int>(row_height), tile_remap);
                image_strip_width = 8;
                output_stride = 8 * static_cast<int>(row_height);
                break;
            case Rotation::Clockwise_180:
                RotateTile180(tiles[num_tiles - i - 1], tmp_tile, static_cast<int>(row_height), tile_remap);
                image_strip_width = cvt.input_line_width;
                output_stride = 8;
                break;
            case Rotation::Clockwise_270:
                RotateTile270(tiles[num_tiles - i - 1], tmp_tile, static_cast<int>(row_height), tile_remap);
                image_strip_width = 8;
                output_stride = 8 * static_cast<int>(row_height);
                break;
            }
            switch (cvt.block_alignment) {
            case BlockAlignment::Linear:
                WriteTileToOutput(output_buffer, tmp_tile, static_cast<int>(row_height), image_strip_width);
                output_buffer += output_stride;
                break;
            case BlockAlignment::Block8x8:
                WriteTileToOutput(output_buffer, tmp_tile, 8, 8);
                output_buffer += kTileSize;
                break;
            }
        }

        if (!SendDataFast(memory, cvt.output_format, reinterpret_cast<const u32*>(data_buffer.data()), cvt.dst,
                          static_cast<int>(row_data_size), static_cast<u8>(cvt.alpha), staging, sizes)) {
            return false;
        }
    }
    return true;
}

}  // namespace

}  // namespace mh4n::y2r
