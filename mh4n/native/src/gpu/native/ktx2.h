// KTX2 files in an HD texture pack (1.0.2, S9b: Dark_Light's "Soporte para texturas (KTX2)"; hd_textures.h).
//
// A pack may carry tex1_<w>x<h>_<hash>_<format>.ktx2 beside or instead of the .png of the same name (the same hash
// rules); where both are there the .ktx2 is drawn. Read here:
//   - Basis Universal's supercompressed kinds (vkFormat 0): ETC1S (BasisLZ) and UASTC LDR 4x4 (plain or Zstandard),
//     transcoded by Basis Universal's transcoder (native/third_party/basisu, Apache-2.0);
//   - files that already hold a format: RGBA8 (VK_FORMAT_R8G8B8A8_UNORM / _SRGB) and BC7 (VK_FORMAT_BC7_UNORM_BLOCK /
//     _SRGB_BLOCK), plain or Zstandard.
// What the GPU gets: BC7 (16 bytes a 4x4 block, a quarter of RGBA8's memory) where the device samples it and the file's
// rows run bottom-up (its KTXorientation key "ru", what toktx --lower_left_maps_to_s0t0 writes): the renderer's images
// hold a texture's rows bottom-up, as the 3DS's, and a block can't be turned over without encoding it again. Otherwise
// RGBA8, its rows upright as a PNG's (turned over here when the file's run bottom-up), the same memory as the PNG. The
// file's first level only (the game's samplers ask for the texture's own levels, and a pack's texture has one). ASTC 4x4
// and ETC2 (the phones' block formats) are not taken yet: the renderer's backend has no such formats (future).
#pragma once

#include <string>
#include <vector>

#include "mh4n/types.h"

namespace mh4n::gpu::native {

enum class HdFormat : u8 { Rgba8 = 0, Bc7 = 1 };

// What a KTX2 file holds, from its header and key/values
struct Ktx2Info {
    u32 width = 0, height = 0;
    u32 vk_format = 0;  // 0: Basis Universal's
    bool basis = false, etc1s = false, uastc = false;
    bool bottom_up = false;  // KTXorientation's second letter 'u'
    std::string kind;        // for the log: "ETC1S", "UASTC", "RGBA8", "BC7", or why it can't be read
    bool readable = false;   // a kind this reader takes
};

// The header of `path` (the first 80 bytes and its key/values); false when it isn't a KTX2 file
bool Ktx2Header(const std::string& path, Ktx2Info& info);
// The format the GPU will get for it: BC7 when `bc7` (the device samples BC7) and the file can give it bottom-up
HdFormat Ktx2Target(const Ktx2Info& info, bool bc7);

// Reads and decodes `path` into `out` in `target` (Ktx2Target's): RGBA8 upright (as ReadPng's), or BC7 blocks with
// their rows bottom-up (the image's order). False, with `error`, when it can't be read or its size changed.
bool ReadKtx2(const std::string& path, HdFormat target, u32 width, u32 height, std::vector<u8>& out, std::string& error);

// The bytes of an image of `format`
inline u64 HdBytes(HdFormat format, u32 width, u32 height) {
    return format == HdFormat::Bc7 ? u64{(width + 3) / 4} * ((height + 3) / 4) * 16 : u64{width} * height * 4;
}

}  // namespace mh4n::gpu::native
