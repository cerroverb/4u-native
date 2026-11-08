// Derived from Azahar src/video_core/pica/regs_texturing.h (the unit-0 registers a shadow texture is read from: the type in the
// parameters, the size, the address and the five cube-face addresses of GetCubePhysicalAddress, the format) at
// 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-2.0-or-later: the file says "Licensed under GPLv2 or any later version").
// Copyright 2017-2024 Citra Emulator Project / Azahar Emulator Project (regs_texturing.h)
//
// The host side of the `shadow/` feature (N2-12): what the renderer binds for a shadow lookup. See shadow.cpp for the shaders.
//
// A Shadow2D unit 0 reads the shadow map through its R32_UINT view (`rhi::ImageView::ShadowStorage`, the class `tex0_shadow` of
// bindings.h): the image must be a shadow source, the render target the shadow-write pass drew into (or that memory loaded into
// one). A ShadowCube unit 0 reads its six faces as one RGBA8 cube image in the cube class (`tex0_cube`, sampled with a nearest,
// clamped sampler: the composer's own when the binding gives none); the shader reads each texel at its centre and puts the four
// bytes back together. `texture::TexturesOfDraw` leaves both types out (they are not colour textures): `ShadowTextureOf` is the
// list for them.
#pragma once

#include <array>
#include <optional>

#include "mh4n/types.h"

namespace mh4n::gpu::fragment::shadow {

// The shadow texture of unit 0, from the 0x300 internal registers.
struct ShadowTexture {
    bool cube = false;               // ShadowCube (six faces), else Shadow2D (one)
    u32 width = 0, height = 0;
    u32 format = 0;                  // the unit-0 format (TextureFormat; RGBA8 = 0 is the shadow map's)
    std::array<u32, 6> paddr{};      // the faces' physical addresses (+X, -X, +Y, -Y, +Z, -Z), [0] for Shadow2D
};

// Unit 0 when it is enabled, has an address and has the type Shadow2D or ShadowCube; nullopt otherwise (a unit that is off or has
// address 0 is the composer's dummy, which reads lit).
std::optional<ShadowTexture> ShadowTextureOf(const u32* regs);

}  // namespace mh4n::gpu::fragment::shadow
