// Tests for an HD pack's KTX2 files (gpu/native/ktx2.h; 1.0.2, S9b): files written here (RGBA8 and BC7, plain, rows
// top-down and bottom-up), read back as RGBA8 upright or BC7 blocks bottom-up, the target chosen, files refused, and a
// .ktx2 winning over the .png of the same texture in a pack. Basis Universal's own kinds (ETC1S, UASTC) are tried on the
// PC runner with files the encoder made (the 1.0.2 report); here, a header saying ETC1S is refused when it isn't one.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "gpu/native/ktx2.h"
#include "hdpack/bc7.h"
#include "hdpack/pack.h"

using namespace mh4n;
using namespace mh4n::gpu::native;
namespace fs = std::filesystem;

namespace {

void Put32(std::vector<u8>& b, std::size_t at, u32 v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<u8>(v >> (8 * i));
}
void Put64(std::vector<u8>& b, std::size_t at, u64 v) {
    Put32(b, at, static_cast<u32>(v));
    Put32(b, at + 4, static_cast<u32>(v >> 32));
}

// A KTX2 file of one level holding `level` (vkFormat `vk`), with KTXorientation `orientation` ("" none)
std::vector<u8> Ktx2(u32 vk, u32 w, u32 h, const std::vector<u8>& level, const char* orientation, u32 scheme = 0) {
    std::vector<u8> kvd;
    if (*orientation) {
        const std::string kv = std::string("KTXorientation") + '\0' + orientation + '\0';
        kvd.resize(4 + ((kv.size() + 3) & ~std::size_t{3}), 0);
        Put32(kvd, 0, static_cast<u32>(kv.size()));
        std::memcpy(kvd.data() + 4, kv.data(), kv.size());
    }
    const std::size_t kvd_at = 104, level_at = (kvd_at + kvd.size() + 15) & ~std::size_t{15};
    std::vector<u8> b(level_at + level.size(), 0);
    const unsigned char id[12] = {0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n'};
    std::memcpy(b.data(), id, 12);
    Put32(b, 12, vk);
    Put32(b, 16, 1);
    Put32(b, 20, w), Put32(b, 24, h), Put32(b, 28, 0), Put32(b, 32, 0), Put32(b, 36, 1), Put32(b, 40, 1);
    Put32(b, 44, scheme);
    Put32(b, 56, kvd.empty() ? 0 : static_cast<u32>(kvd_at)), Put32(b, 60, static_cast<u32>(kvd.size()));
    Put64(b, 80, level_at), Put64(b, 88, level.size()), Put64(b, 96, level.size());
    std::memcpy(b.data() + kvd_at, kvd.data(), kvd.size());
    std::memcpy(b.data() + level_at, level.data(), level.size());
    return b;
}

std::string Write(const fs::path& path, const std::vector<u8>& bytes) {
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path.string();
}

// w x h RGBA8, top row first: each pixel's green its row (each 4x4 block a straight line of colours: BC7 keeps it close)
std::vector<u8> Picture(u32 w, u32 h) {
    std::vector<u8> p(std::size_t{w} * h * 4);
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            u8* px = p.data() + (std::size_t{y} * w + x) * 4;
            px[0] = 128, px[1] = static_cast<u8>(y * 16), px[2] = static_cast<u8>(64 + (x & 1)), px[3] = 255;
        }
    return p;
}

std::vector<u8> RowsTurned(const std::vector<u8>& p, u32 w, u32 h, u32 row_bytes_per_pixel = 4) {
    std::vector<u8> out(p.size());
    const std::size_t row = std::size_t{w} * row_bytes_per_pixel;
    for (u32 y = 0; y < h; ++y) std::memcpy(out.data() + y * row, p.data() + (h - 1 - y) * row, row);
    return out;
}

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("mh4n_ktx2_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

}  // namespace

TEST_CASE("ktx2: RGBA8 files, either way up, are read upright") {
    TempDir t;
    const u32 w = 8, h = 4;
    const std::vector<u8> upright = Picture(w, h);
    for (const char* o : {"", "rd", "ru"}) {
        const bool up = std::string(o) == "ru";
        const std::string path = Write(t.path / (std::string("a_") + o + ".ktx2"), Ktx2(37, w, h, up ? RowsTurned(upright, w, h) : upright, o));
        Ktx2Info info;
        REQUIRE(Ktx2Header(path, info));
        CHECK(info.readable);
        CHECK(info.kind == "RGBA8");
        CHECK(info.width == w);
        CHECK(info.height == h);
        CHECK(info.bottom_up == up);
        CHECK(Ktx2Target(info, true) == HdFormat::Rgba8);  // (RGBA8 stays RGBA8)
        std::vector<u8> out;
        std::string error;
        REQUIRE(ReadKtx2(path, HdFormat::Rgba8, w, h, out, error));
        CHECK(out == upright);
        CHECK_FALSE(ReadKtx2(path, HdFormat::Rgba8, w * 2, h, out, error));  // (its size changed since it was sized)
    }
}

TEST_CASE("ktx2: BC7 files go to the GPU as blocks only bottom-up, else decoded upright") {
    TempDir t;
    const u32 w = 8, h = 8;
    const std::vector<u8> upright = Picture(w, h);
    std::vector<u8> blocks_bottom_up(std::size_t{2} * 2 * 16), blocks_top_down(blocks_bottom_up.size());
    const std::vector<u8> turned = RowsTurned(upright, w, h);
    hdpack::EncodeBc7(turned.data(), w, h, hdpack::Bc7Quality{}, blocks_bottom_up.data());
    hdpack::EncodeBc7(upright.data(), w, h, hdpack::Bc7Quality{}, blocks_top_down.data());

    const std::string ru = Write(t.path / "ru.ktx2", Ktx2(145, w, h, blocks_bottom_up, "ru"));
    Ktx2Info info;
    REQUIRE(Ktx2Header(ru, info));
    CHECK(info.kind == "BC7");
    CHECK(Ktx2Target(info, true) == HdFormat::Bc7);
    CHECK(Ktx2Target(info, false) == HdFormat::Rgba8);  // (a device without BC7)
    std::vector<u8> out;
    std::string error;
    REQUIRE(ReadKtx2(ru, HdFormat::Bc7, w, h, out, error));
    CHECK(out == blocks_bottom_up);  // (as they are: the image's rows run bottom-up too)
    REQUIRE(ReadKtx2(ru, HdFormat::Rgba8, w, h, out, error));
    REQUIRE(out.size() == upright.size());
    int off = 0;  // (BC7 is lossy: each channel within a few steps of the picture)
    for (std::size_t i = 0; i < out.size(); ++i) off += std::abs(int(out[i]) - int(upright[i])) > 8;
    CHECK(off == 0);

    const std::string rd = Write(t.path / "rd.ktx2", Ktx2(145, w, h, blocks_top_down, "rd"));
    REQUIRE(Ktx2Header(rd, info));
    CHECK(Ktx2Target(info, true) == HdFormat::Rgba8);  // (top-down blocks can't be turned over)
    REQUIRE(ReadKtx2(rd, HdFormat::Rgba8, w, h, out, error));
    off = 0;
    for (std::size_t i = 0; i < out.size(); ++i) off += std::abs(int(out[i]) - int(upright[i])) > 8;
    CHECK(off == 0);
    CHECK(HdBytes(HdFormat::Bc7, 1024, 1024) * 4 == HdBytes(HdFormat::Rgba8, 1024, 1024));
}

TEST_CASE("ktx2: what isn't read is refused with a reason") {
    TempDir t;
    Ktx2Info info;
    const std::string png = Write(t.path / "not.ktx2", std::vector<u8>(200, 7));
    CHECK_FALSE(Ktx2Header(png, info));
    const std::string astc = Write(t.path / "astc.ktx2", Ktx2(157, 8, 8, std::vector<u8>(64, 0), "rd"));  // (ASTC 4x4)
    REQUIRE(Ktx2Header(astc, info));
    CHECK_FALSE(info.readable);
    std::vector<u8> out;
    std::string error;
    CHECK_FALSE(ReadKtx2(astc, HdFormat::Rgba8, 8, 8, out, error));
    CHECK(error.find("vkFormat 157") != std::string::npos);
    const std::string fake = Write(t.path / "etc1s.ktx2", Ktx2(0, 8, 8, std::vector<u8>(64, 0), "rd", 1));  // (BasisLZ, no data)
    REQUIRE(Ktx2Header(fake, info));
    CHECK(info.etc1s);
    CHECK(Ktx2Target(info, true) == HdFormat::Rgba8);  // (rows top-down)
    CHECK_FALSE(ReadKtx2(fake, HdFormat::Rgba8, 8, 8, out, error));
}

TEST_CASE("ktx2: a pack's .ktx2 wins over the .png of the same texture") {
    TempDir t;
    const std::string stem = "tex1_8x8_00000000DEADBEEF_12";
    Write(t.path / (stem + ".png"), std::vector<u8>(10, 0));
    fs::create_directories(t.path / "zz");
    Write(t.path / "zz" / (stem + ".ktx2"), Ktx2(37, 8, 8, Picture(8, 8), "rd"));
    Write(t.path / "tex1_8x8_00000000CAFEF00D_12.png", std::vector<u8>(10, 0));
    std::string error;
    const std::optional<hdpack::Pack> pack = hdpack::ScanPack(t.path.string(), &error);
    REQUIRE(pack);
    CHECK(pack->stats.ktx2_preferred == 1);
    CHECK(pack->stats.duplicate_maps == 0);
    const auto& m = pack->materials.at(0xDEADBEEFull);
    const int color = m.file[static_cast<int>(hdpack::MapType::Color)];
    REQUIRE(color >= 0);
    CHECK(pack->files[static_cast<std::size_t>(color)].format == hdpack::FileFormat::Ktx2);
    const auto& other = pack->materials.at(0xCAFEF00Dull);
    CHECK(pack->files[static_cast<std::size_t>(other.file[0])].format == hdpack::FileFormat::Png);
}
