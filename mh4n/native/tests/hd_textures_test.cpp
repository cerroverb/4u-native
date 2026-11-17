// Tests for the HD pack textures the renderer draws (gpu/native/hd_textures.h): a pack made here of synthetic PNGs named
// as Azahar's dumps name them, read by HdTextures, its files' sizes, and the half size of MH4N_HD_SCALE=half (1.0.1,
// 4u-native #56: a quarter of the memory, only for files at least four times the game's texture). No real pack is read.
#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <lodepng.h>

#include "gpu/native/hd_textures.h"
#include "gpu/texture/hash.h"

using namespace mh4n;
using namespace mh4n::gpu;
namespace fs = std::filesystem;

namespace {

void SetEnv(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value != nullptr ? value : "");
#else
    if (value != nullptr) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

// A game texture of `w` x `h` RGBA8 texels whose bytes are `seed` and on, and the pack file for it: `scale` times its
// size, every pixel `pixel(x, y)`, named by the texture's old hash (no pack.json: Azahar's legacy defaults)
struct Case {
    std::vector<u8> data;
    u32 w = 0, h = 0;
};
template <typename Pixel>
Case Make(const fs::path& dir, u32 w, u32 h, u8 seed, u32 scale, Pixel pixel) {
    Case c;
    c.w = w, c.h = h;
    c.data.resize(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < c.data.size(); ++i) c.data[i] = static_cast<u8>(seed + i * 7);
    const auto hashes = texture::ComputeCustomHashes(c.data.data(), c.data.size(), w, h, texture::Format::RGBA8);
    REQUIRE(hashes.has_value());
    const u32 fw = w * scale, fh = h * scale;
    std::vector<u8> png(static_cast<std::size_t>(fw) * fh * 4);
    for (u32 y = 0; y < fh; ++y) {
        for (u32 x = 0; x < fw; ++x) {
            const std::array<u8, 4> p = pixel(x, y);
            std::copy(p.begin(), p.end(), png.begin() + (static_cast<std::size_t>(y) * fw + x) * 4);
        }
    }
    const fs::path file = dir / (texture::CustomTextureStem(w, h, hashes->old_hash, texture::Format::RGBA8) + ".png");
    REQUIRE(lodepng::encode(file.string(), png, fw, fh) == 0);
    return c;
}

fs::path FreshDir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "pack");
    return dir;
}

}  // namespace

TEST_CASE("HD textures: full size by default, half with MH4N_HD_SCALE=half for files four times the game's or more") {
    const fs::path dir = FreshDir("mh4n_hd_textures_test");
    const fs::path pack = dir / "pack";
    // a 4x file of 1-pixel black and white stripes (its half: grey), a 4x file of one colour (its half: the same), and a
    // 2x file (never halved: its half would be the game's own size)
    const Case stripes = Make(pack, 16, 8, 1, 4, [](u32 x, u32) {
        const u8 v = x % 2 ? 255 : 0;
        return std::array<u8, 4>{v, v, v, 255};
    });
    const Case solid = Make(pack, 8, 8, 2, 4, [](u32, u32) { return std::array<u8, 4>{10, 200, 30, 255}; });
    const Case twice = Make(pack, 8, 16, 3, 2, [](u32, u32) { return std::array<u8, 4>{1, 2, 3, 4}; });

    for (const bool half : {false, true}) {
        CAPTURE(half);
        SetEnv("MH4N_HD_SCALE", half ? "half" : nullptr);
        native::HdTextures hd;
        hd.Load(dir.string(), /*sync=*/true);  // (each file read when taken: no loader threads in a test)
        REQUIRE(hd.active());

        const auto* r = hd.Find(1, stripes.data.data(), stripes.data.size(), stripes.w, stripes.h, texture::Format::RGBA8);
        REQUIRE(r != nullptr);
        CHECK(r->width == (half ? 32u : 64u));
        CHECK(r->height == (half ? 16u : 32u));
        std::vector<u8> rgba;
        REQUIRE(hd.TakePixels(*r, rgba) == native::HdTextures::Take::Ready);
        REQUIRE(rgba.size() == std::size_t{r->width} * r->height * 4);
        // (full: the stripes as they are; half: each pair of them, black and white, averaged)
        CHECK(rgba[0] == (half ? 128 : 0));
        CHECK(rgba[4] == (half ? 128 : 255));
        CHECK(rgba[3] == 255);

        r = hd.Find(2, solid.data.data(), solid.data.size(), solid.w, solid.h, texture::Format::RGBA8);
        REQUIRE(r != nullptr);
        CHECK(r->width == (half ? 16u : 32u));
        REQUIRE(hd.TakePixels(*r, rgba) == native::HdTextures::Take::Ready);
        REQUIRE(rgba.size() == std::size_t{r->width} * r->height * 4);
        for (std::size_t i = 0; i < rgba.size(); i += 4) {
            REQUIRE(rgba[i] == 10);
            REQUIRE(rgba[i + 1] == 200);
            REQUIRE(rgba[i + 2] == 30);
        }

        r = hd.Find(3, twice.data.data(), twice.data.size(), twice.w, twice.h, texture::Format::RGBA8);
        REQUIRE(r != nullptr);
        CHECK(r->width == 16u);
        CHECK(r->height == 32u);
        REQUIRE(hd.TakePixels(*r, rgba) == native::HdTextures::Take::Ready);
        CHECK(rgba.size() == std::size_t{16} * 32 * 4);
    }
    SetEnv("MH4N_HD_SCALE", nullptr);
    std::error_code ec;
    fs::remove_all(dir, ec);
}
