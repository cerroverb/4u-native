// Derived from Azahar src/video_core/rasterizer_cache/framebuffer_base.h (FramebufferHelper: the draw rectangle, the viewport
// and the scissor of a draw), src/video_core/renderer_vulkan/vk_rasterizer.cpp (Draw: which framebuffer surfaces a draw uses),
// src/video_core/pica/regs_framebuffer.h and regs_rasterizer.h (the register positions) and src/video_core/gpu.cpp (the
// register windows) at 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-3.0-or-later: vk_rasterizer.cpp says "Licensed under GPLv3
// or any later version"; the others say "Licensed under GPLv2 or any later version").
// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project (vk_rasterizer.cpp, gpu.cpp)
// Copyright 2023-2025 Citra Emulator Project / Azahar Emulator Project (framebuffer_base.h)
// Copyright 2017-2025 Citra Emulator Project / Azahar Emulator Project (regs_framebuffer.h)
// Copyright 2017-2026 Citra Emulator Project / Azahar Emulator Project (regs_rasterizer.h)
//
// `--gpu native` (N2-08): the native renderer assembled. A `gpu::Gpu` for the capture player and, later, the runtime:
//
//   GX engine (N2-03)        every GX command that is not a command list; the register windows other than the PICA's
//   command processor (N2-04) the command lists, the internal registers; every draw reaches `Draw` below
//   vertex loading (N2-05) + vertex stage (N2-06)   the accelerated draws
//   CPU path (N2-36, N2-37)  geometry-shader, immediate-mode and refused draws: shaded on the CPU, assembled into triangles
//                            with the quaternion fix-up, drawn with the trivial vertex stage (counted `cpu_path_draws`)
//   fragment stage (N2-33, N2-08)   the key, the uniforms, the generator; the specialised shader on a miss (a counted
//                            synchronous build) or the ubershader (`--force-ubershader`, every draw)
//   framebuffer operations (N2-13)  the fixed-function state and the dynamic state
//
// PER-DRAW ORDER (what N2-23, N2-09 to N2-12, N2-16 and N2-20 can rely on), in `Draw`:
//   1. the renderer is created on first use, on the thread that calls Execute (never in the constructor: under the capture
//      player's `--queue` the GPU is made on another thread);
//   2. `FragmentState::Prepare` (key, uniforms, tables) runs for EVERY draw, also for one that is dropped, so the dirty
//      flags the command processor clears after the draw are not lost;
//   3. the primitive assembler applies the draw's register side effects, then `NeedsCpuPath(draw, assembler.IsEmpty())`;
//   4. accelerated: N2-05's loader, then the provider's `Prepare` for the pipeline (N2-16: N2-06's translation runs inside
//      the pipeline's build; a program that does not translate takes the CPU path, counted `translate_failures`; a pipeline
//      still being built on a worker takes the CPU path in the vertex unit's mode and the ubershader); CPU path: the runner,
//      the assembler, the batch;
//   5. the render targets for the colour and depth buffers (`TargetRegistry` below), the fixed-function state and the
//      dynamic state, the vertex-stage and fragment uniforms;
//   6. `DrawComposer::Draw` (N2-33) with the `PipelineKey` made from all of it; the provider (provider_select.h: N2-16's
//      caching provider, or N2-08's default one, which builds the pipeline on a miss) answers with `PipelineBuilder::Build`.
//
// RENDER TARGETS (replaced by N2-15's surface cache): a colour target is an RGBA8 image per (address, format, size), a depth
// target a D16 or D24S8 image; guest memory stays the truth. A target is loaded from guest memory the first time it is
// used and again after the CPU side (a GX command, a flush or invalidate of its range) may have changed it; the draws
// write the image, and `WriteBackColorBuffers`, every GX command and every flush or invalidate that touches a target's
// range write it back first (N2-28's ReadbackToGuest). Depth is NOT loaded from guest memory: a new depth target starts at 1.0.
// By rows of 8x8 tiles: a target keeps the rows its draws covered since its last write-back (MarkDirty), and a write-back
// writes those rows only, when they lie under what is about to read or write memory (DirtyOverlaps); a colour target's
// image goes stale only in the rows another target, the GX engine or the CPU wrote (MarkStale), and a draw loads only the
// stale rows it covers (UseTarget). Two targets that share memory therefore leave each other's rows alone: MH4U's
// widescreen scene target (1024 rows) holds the bloom buffers in rows it never draws. A texture that is a target takes
// another target's newer tiles there image to image (CopyTiles, every source in one CopyImages) instead of waiting for a
// write-back, and only when that source changed since it last did (Target::copied); a target sampled by a draw into it
// gets its copy (Target::twin) brought up to date in the rows changed since (twin_rows), not copied whole. A pass on a
// target renders, loads and stores only the box its draws have covered (Target::area, plume patch 15).
// MH4N_PASS_TRACE=F,N logs N display transfers' worth of the GPU's passes, copies and loads, with GPU timestamps.
//
// TEXTURES (N2-23): a draw binds the textures its TEV stages and lighting read (BindTextures: N2-14's cache and decoder, one image per
// texture or cube, level 0 only, uploaded flipped as sampling.h says; a unit that is off, has address 0 or cannot be decoded gets a
// 1x1 null image; the sampler objects are cached by value), made on the thread that calls Execute; the counters are `texture_binds`,
// `texture_uploads`, `texture_unavailable` and `sampler_creates`. A render target a texture overlaps is written back first.
// TARGETS AS TEXTURES (`--target-textures`, on by default): a 2D texture that is exactly a colour target held here (address,
// size, format) and not older than guest memory samples the target's image, or its feedback copy when the draw draws into
// it too, instead of being written back, decoded and uploaded (TargetAsTexture); `--target-textures off` decodes it from
// memory as every other texture.
// LIMITATION: only level 0 of a texture is bound, where Azahar's Vulkan renderer gives a texture `lod.max_level + 1` levels from
// guest memory. `texture_levels_missing` counts the draws that bind a unit whose max_level register is above 0 (a plain counter,
// not `stubbed:*`): a minified draw of such a texture samples level 0 and is not the game's picture.
//
// RESOLUTION SCALING (`--scale N`, the shortest path to N2-15; scaled_screens.h has the whole of it): the targets are images
// of N times the size, the viewport, the draw rectangle and the scissor are N times larger; a write-back downscales
// (ReadbackToGuest at scale N) and a load upscales (nearest), so guest memory keeps the 1x picture.
//
// LIGHTING FIX-UP (`--lighting-fixup`, NativeOptions::lighting_fixup): the quaternion flip of lit draws in the fixed
// geometry stage (fragment/lighting/fixup.h) or without one (shader/vertex_shader.h, `pull_fixup`): the draw's triangles
// with three different vertices go into the vertex ring as corners (each one's vertex and its triangle's first, after a
// header of the bindings' offsets: MakePullPairs), with room for the vertices' quaternions and the indices (written
// without a flip, which Mali's driver reads to find the draw's range); at the submission the recording's job records go
// into the ring and its compute passes into the recorder's prelude (RecordFixupPasses): a quaternion pass a program (a
// compute pipeline of the provider, key VertexStageKind::QuaternionPass), then one flip pass. The draw is an indexed
// list that the vertex stage reads through set 0 binding 6, each vertex shaded once for each sign its quaternion takes.
// Counters `fixup_draws` (geometry stage), `pull_draws`, `pull_vertices` (their corners).
//
// WHAT IS NOT HERE: lighting, fog, procedural textures and shadows (the stubs,
// each counted `stubbed:<feature>`); a shadow-write draw is counted and not drawn; N2-15's surface cache.
#pragma once

#include <memory>

#include "mh4n/gpu.h"
#include "mh4n/gpu_debug.h"
#include "mh4n/options.h"

namespace mh4n::gpu::native {

struct NativeOptions {
    // The program's options, for the pipeline provider (N2-16: provider_select.h; its own switches, `--user`).
    Options options;
    // Every draw takes the ubershader (`--force-ubershader`).
    bool force_ubershader = false;
    // The internal resolution (`--scale N`, 1 to kMaxScale): render targets at N times the PICA's size (see
    // scaled_screens.h). 1 is the PICA's own resolution, exactly as without the option.
    u32 scale = 1;
    // A texture that is exactly a colour target samples the target's image (`--target-textures on|off`, default on).
    bool target_textures = true;
    // Display transfers of a target the GPU is still drawing go to the GPU, with no wait (`--async-screens on|off`,
    // scaled_screens.h; on by default with native-display, off for the capture player).
    bool async_screens = false;
    // The lighting fix-up of lit draws (`--lighting-fixup gs|vs|off|auto`; MH4N_LIGHTING_FIXUP overrides it, 0 is off): the
    // fixed geometry stage (gs), the vertex stage reading its own vertices (vs, vertex_shader.h), none (off, the PICA's
    // rasteriser's quaternion flip is lost); auto: vs on a Mali (whose geometry stage is slow) and on a device without
    // geometry stages, gs elsewhere.
    std::string lighting_fixup = "auto";
};
inline constexpr u32 kMaxScale = 10;

// The `--gpu native` Gpu. Constructed with the host and the options; it touches no device until its first draw.
class NativeGpu : public Gpu, public GpuDebug {
public:
    ~NativeGpu() override = default;

    // Counters since the last call (`TakeFrameCounters` of GpuDebug resets them); `Peek` leaves them, for the tests.
    virtual std::vector<Counter> PeekCounters() const = 0;
};

std::unique_ptr<NativeGpu> MakeNativeGpu(Host& host, const NativeOptions& options);

// SHADOWS (`--shadows on|off`, an app's own switch; process-wide, any thread, read at every draw): off, nothing is drawn into a
// shadow map (the shadow-write draws, and the ordinary draws over the whole of a shadow source, the game's clear of it) and a
// Shadow2D lookup's map is cleared once to "nothing in the way" (again only if a load from memory changed it), so every lookup is
// lit. On again, the game's next draws fill the maps as before.
void SetShadows(bool on);
bool Shadows();

}  // namespace mh4n::gpu::native
