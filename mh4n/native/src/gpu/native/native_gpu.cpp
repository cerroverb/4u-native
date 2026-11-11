// Derived from Azahar src/video_core/rasterizer_cache/framebuffer_base.h (FramebufferHelper), src/video_core/renderer_vulkan/
// vk_rasterizer.cpp (Draw; SyncTextureUnits: which texture each unit binds, the null surfaces for a unit that is off),
// src/video_core/pica/regs_framebuffer.h, regs_rasterizer.h, regs_texturing.h and src/video_core/gpu.cpp at
// 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-3.0-or-later: vk_rasterizer.cpp says "Licensed under GPLv3 or any later
// version"; the others say "Licensed under GPLv2 or any later version"): the viewport, scissor and draw rectangle of a draw
// (mirrored when the framebuffer is flipped; FramebufferHelper's res_scale for --scale), the surfaces a draw uses, the
// register positions of the framebuffer block. The scaled screen's shader restates, at N x, what gx/blitter.cpp and
// gpu_capture/image.cpp take from Azahar src/video_core/renderer_software/sw_blitter.cpp (the display transfer's flip and
// crop), renderer_software.cpp (the screen read out of the framebuffer, rotated, with its one-pixel shift) and
// src/common/color.h (Convert8To5 and kin, Convert5To8 and kin), and the GX command words of src/video_core/gpu.cpp.
// The rest (the targets, the write-back, the recorder and ring management, the counters, the scaled targets and the
// scaled screens of --scale) is ours.
// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project (vk_rasterizer.cpp, gpu.cpp)
// Copyright 2023-2025 Citra Emulator Project / Azahar Emulator Project (framebuffer_base.h, renderer_software.cpp)
// Copyright 2017-2025 Citra Emulator Project / Azahar Emulator Project (regs_framebuffer.h)
// Copyright 2017-2026 Citra Emulator Project / Azahar Emulator Project (regs_rasterizer.h)
// Copyright 2017-2024 Citra Emulator Project / Azahar Emulator Project (regs_texturing.h)
// Copyright 2023 Citra Emulator Project (sw_blitter.cpp)
// Copyright 2014 Citra Emulator Project (color.h)
//
// `--gpu native` (N2-08). See native_gpu.h for the order of a draw and what the render targets are.
#include "gpu/native/native_gpu.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <array>
#include <mutex>
#include <chrono>
#include <cmath>
#include <type_traits>
#include <bit>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "gpu/backend/shared/shared_images.h"
#include "gpu/backend/state/state.h"
#include "gpu/backend/vulkan_access.h"
#include "gpu/cpuvertex/cpu_vertex.h"
#include "gpu/cpuvertex/primitive_assembly.h"
#include "gpu/cpuvertex/shader_interpreter.h"
#include "gpu/cpuvertex/triangle_batch.h"
#include "gpu/fbops/fixed_function.h"
#include "gpu/fragment/feature.h"
#include "gpu/fragment/generator.h"
#include "gpu/fragment/lighting/fixup.h"
#include "mh4n/pacing_trace.h"
#include "gpu/fragment/profile.h"
#include "gpu/fragment/sampling/sampling.h"
#include "gpu/fragment/shadow/shadow.h"
#include "gpu/fragment/state.h"
#include "gpu/fragment/tables.h"
#include "gpu/gx/engine.h"
#include "gpu/native/compose.h"
#include "gpu/native/pipeline_builder.h"
#include "gpu/native/process_memory.h"
#include "gpu/native/scaled_screens.h"
#include "gpu/native/provider_select.h"
#include "gpu/native/vertex_input.h"
#include "gpu/pica/command_processor.h"
#include "gpu/shader/vertex_shader.h"
#include "gpu/texture/async_decode.h"
#include "gpu/texture/cache.h"
#include "gpu/texture/decode.h"
#include "gpu/texture/texture_regs.h"
#include "gpu/vertex/vertex_loader.h"
#include "mh4n/log.h"
#include "mh4n/registry.h"
#include "mh4n/host_thread_name.h"

namespace mh4n::gpu::native {
namespace {

constexpr const char* kLog = "NativeGpu";

std::atomic<bool> g_shadows{true};  // SetShadows

constexpr u32 kVaddrLcd = 0x1ED02000;
constexpr u32 kLcdRegs = 0x400;

// RegsInternal: FramebufferRegs at 0x100, its framebuffer block at 0x110; RasterizerRegs at 0x40.
constexpr u32 kRegViewportSizeX = 0x41;
constexpr u32 kRegViewportSizeY = 0x43;
constexpr u32 kRegScissorXY1 = 0x66;
constexpr u32 kRegScissorXY2 = 0x67;
constexpr u32 kRegViewportCorner = 0x68;
constexpr u32 kRegDepthFormat = 0x116;
constexpr u32 kRegColorFormat = 0x117;
constexpr u32 kRegDepthAddress = 0x11C;
constexpr u32 kRegColorAddress = 0x11D;
constexpr u32 kRegFramebufferSize = 0x11E;

// The vertex-stage rings; the recording is submitted before they run out (see EnsureRoom).
constexpr u64 kVertexRingBytes = 16ull << 20;
// (8 MB: the uniforms of several frames, as a phone's GPU can be that far behind the renderer; 2 MB made Ring::Map
// wait for it)
constexpr u64 kVsUniformRingBytes = 8ull << 20;
constexpr u64 kFragmentUniformRingBytes = 8ull << 20;
constexpr u64 kStagingRingBytes = 8ull << 20;
// The decoded textures kept as images (a texture is level 0 only until N2-15); past this the least recently used ones the
// current recording does not use are released, down to three quarters of it. 768 MB everywhere before: an area's textures
// are 30-45 MB of images (the High Rank steppe, the village), so only a long session could reach it, and a 3-4 GB phone
// could run out of memory first (each image's pixels were kept in the cache as well). By the device's memory: up to 3 GB
// 192 MB, up to 4.5 GB 256 MB, up to 6.5 GB 384 MB, more 768 MB; MH4N_TEXTURE_BUDGET_MB=<n> another.
u64 TextureBudgetBytes() {
    static const u64 budget = [] {
        if (const char* v = std::getenv("MH4N_TEXTURE_BUDGET_MB"); v != nullptr && std::atoi(v) > 0) {
            return static_cast<u64>(std::atoi(v)) << 20;
        }
        const u64 mb = DeviceMemoryMb();
        const u64 budget_mb = mb == 0 ? 768 : mb <= 3200 ? 192 : mb <= 4600 ? 256 : mb <= 6600 ? 384 : 768;
        return budget_mb << 20;
    }();
    return budget;
}
// Below this much of the system's memory available (MH4N_LOWMEM_MB, as the MHGU port's: Android 14 and newer no longer
// tell a foreground app memory runs low), the images and cache entries not used for two seconds go (TrimForMemory).
[[maybe_unused]] long LowMemoryKb() {
    static const long kb = [] {
        const char* v = std::getenv("MH4N_LOWMEM_MB");
        return (v != nullptr ? std::max(0, std::atoi(v)) : 350) * 1024L;
    }();
    return kb;
}
// A recording is submitted after this many draws, at the first draw that does not go on in the open pass (EnsureRoom,
// gpu4), and after kMaxDrawsPerRecordingHard whatever it breaks.
constexpr u32 kMaxDrawsPerRecording = 256;
constexpr u32 kMaxDrawsPerRecordingHard = 512;
constexpr u32 kVsUniformAllocation = 2048;  // PicaUniformData (1,616) padded to 1,792, then VertexUniformData (32)

enum class ColorFormat : u32 { RGBA8 = 0, RGB8 = 1, RGB5A1 = 2, RGB565 = 3, RGBA4 = 4 };
enum class DepthFormat : u32 { D16 = 0, D24 = 2, D24S8 = 3 };

u32 ColorBytesPerPixel(u32 format) {
    switch (static_cast<ColorFormat>(format)) {
    case ColorFormat::RGBA8: return 4;
    case ColorFormat::RGB8: return 3;
    default: return 2;
    }
}

u32 DepthBytesPerPixel(u32 format) {
    switch (static_cast<DepthFormat>(format)) {
    case DepthFormat::D16: return 2;
    case DepthFormat::D24: return 3;
    default: return 4;
    }
}

rhi::PicaFormat ToPicaFormat(u32 color_format) {
    switch (static_cast<ColorFormat>(color_format)) {
    case ColorFormat::RGB8: return rhi::PicaFormat::Rgb8;
    case ColorFormat::RGB5A1: return rhi::PicaFormat::Rgb5A1;
    case ColorFormat::RGB565: return rhi::PicaFormat::Rgb565;
    case ColorFormat::RGBA4: return rhi::PicaFormat::Rgba4;
    default: return rhi::PicaFormat::Rgba8;
    }
}

rhi::PicaFormat ToPicaDepthFormat(u32 depth_format) {
    switch (static_cast<DepthFormat>(depth_format)) {
    case DepthFormat::D16: return rhi::PicaFormat::D16;
    case DepthFormat::D24: return rhi::PicaFormat::D24;
    default: return rhi::PicaFormat::D24S8;
    }
}

// The logical format of the depth image a PICA depth format is drawn into (the backend's ChooseFormat picks the device's).
rhi::Format DepthImageFormat(u32 depth_format) {
    return static_cast<DepthFormat>(depth_format) == DepthFormat::D16 ? rhi::Format::D16Unorm : rhi::Format::D24S8;
}

struct Rect4 {
    s32 left = 0, top = 0, right = 0, bottom = 0;  // Azahar's Common::Rectangle: top >= bottom (y up)
};

struct DrawGeometry {
    rhi::Viewport viewport;
    rhi::Rect scissor;
    bool empty = false;
    fragment::UniformContext context;
};

// FramebufferHelper's constructor. At `scale` N the rectangles are mirrored in 1x pixels and then multiplied by N (the
// surface rectangle is the whole target, at 0, 0); at 1 every value is the 1x one.
DrawGeometry MakeGeometry(const pica::PicaState& state, u32 width, u32 height, bool flipped, u32 scale) {
    const auto& regs = state.regs;
    DrawGeometry out;
    const u32 corner = regs[kRegViewportCorner];
    auto signed10 = [](u32 v) { return static_cast<s32>(v << 22) >> 22; };
    const s32 corner_x = signed10(pica::BitsOf(corner, 0, 10));
    const s32 corner_y = signed10(pica::BitsOf(corner, 16, 10));
    const s32 size_x = static_cast<s32>(pica::Float24ToFloat(pica::BitsOf(regs[kRegViewportSizeX], 0, 24)) * 2);
    const s32 size_y = static_cast<s32>(pica::Float24ToFloat(pica::BitsOf(regs[kRegViewportSizeY], 0, 24)) * 2);
    // GetViewportRect: left, top = corner.y + height, right, bottom = corner.y
    Rect4 viewport{corner_x, corner_y + size_y, corner_x + size_x, corner_y};
    const s32 h = static_cast<s32>(height);
    if (flipped) {  // VerticalMirror(height)
        viewport = Rect4{viewport.left, h - viewport.bottom, viewport.right, h - viewport.top};
    }
    const s32 n = static_cast<s32>(scale);
    viewport = Rect4{viewport.left * n, viewport.top * n, viewport.right * n, viewport.bottom * n};
    const s32 w = static_cast<s32>(width) * n;
    const s32 scaled_h = h * n;
    Rect4 draw;
    draw.left = std::clamp(viewport.left, 0, w);
    draw.top = std::clamp(viewport.top, 0, scaled_h);
    draw.right = std::clamp(viewport.right, 0, w);
    draw.bottom = std::clamp(viewport.bottom, 0, scaled_h);
    out.viewport.x = static_cast<float>(viewport.left);
    out.viewport.y = static_cast<float>(viewport.bottom);
    out.viewport.width = static_cast<float>(std::abs(viewport.right - viewport.left));
    out.viewport.height = static_cast<float>(std::abs(viewport.bottom - viewport.top));
    out.viewport.min_depth = 0.0f;
    out.viewport.max_depth = 1.0f;
    out.scissor = rhi::Rect{draw.left, draw.bottom, static_cast<u32>(std::abs(draw.right - draw.left)),
                            static_cast<u32>(std::abs(draw.top - draw.bottom))};
    out.empty = out.scissor.width == 0 || out.scissor.height == 0;

    // The scissor uniform: x1, y2, x2, y1 of FramebufferHelper::Scissor() (left, top, right, bottom).
    const u32 xy1 = regs[kRegScissorXY1];
    const u32 xy2 = regs[kRegScissorXY2];
    Rect4 scissor{static_cast<s32>(pica::BitsOf(xy1, 0, 10)), static_cast<s32>(pica::BitsOf(xy2, 16, 10)) + 1,
                  static_cast<s32>(pica::BitsOf(xy2, 0, 10)) + 1, static_cast<s32>(pica::BitsOf(xy1, 16, 10))};
    if (flipped) {
        scissor = Rect4{scissor.left, h - scissor.bottom, scissor.right, h - scissor.top};
    }
    scissor = Rect4{scissor.left * n, scissor.top * n, scissor.right * n, scissor.bottom * n};
    out.context.framebuffer_scale = n;
    out.context.scissor = fragment::UniformContext::Scissor{scissor.left, scissor.bottom, scissor.right, scissor.top};
    // The Include mode is the draw's own scissor rectangle (the specialised fragment stage has no discard for it,
    // generator.cpp's WriteScissor): the pixels x1 <= x < x2, y1 <= y < y2 of the shader's test, cut to the draw's.
    if (pica::BitsOf(regs[fragment::freg::kScissorMode], 0, 2) == static_cast<u32>(fragment::regenum::ScissorMode::Include)) {
        const s32 x0 = std::max(draw.left, scissor.left), x1 = std::min(draw.right, scissor.right);
        const s32 y0 = std::max(draw.bottom, scissor.bottom), y1 = std::min(draw.top, scissor.top);
        out.scissor = rhi::Rect{x0, y0, static_cast<u32>(std::max(x1 - x0, 0)), static_cast<u32>(std::max(y1 - y0, 0))};
        out.empty = out.scissor.width == 0 || out.scissor.height == 0;
    }
    return out;
}

// ---- the render targets --------------------------------------------------------------------------------------------

// Guest rows [first, last) joined to `rows` ([first, last) ranges, apart and in order).
void JoinRows(std::vector<std::pair<u32, u32>>& rows, u32 first, u32 last) {
    if (first >= last) {
        return;
    }
    auto it = std::lower_bound(rows.begin(), rows.end(), std::pair<u32, u32>{first, last});
    it = rows.insert(it, {first, last});
    if (it != rows.begin() && std::prev(it)->second >= it->first) {
        --it;
        it->second = std::max(it->second, std::next(it)->second);
        rows.erase(std::next(it));
    }
    while (std::next(it) != rows.end() && std::next(it)->first <= it->second) {
        it->second = std::max(it->second, std::next(it)->second);
        rows.erase(std::next(it));
    }
}

// ---- screen targets (gpu3) ----
// The top screen's picture as the render target's own image, with no upright copy (MH4N_TARGET_SCREENS=0: the copy, as
// before; docs/research/target-screens.md). The target the top screen's display transfer reads gets images the app's
// OpenGL ES can sample (shared images, gpu/backend/shared), and the picture published is the target's image itself, with
// the transfer's mapping (present::TargetLayout: its rows, flip, colour depths): the app turns it upright, flips it,
// shifts it and cuts its colours as the transfer and the display would, in the pass that draws it into the window
// (app_native/shared_screens.cpp). The upright pass is gone (1632x720 pixels a frame at 3x on a wide phone).
// The image must not change while the picture lives, and the game draws its next frame into the same target. So before
// the target is written again, another of its images takes its place (Detach), given from the published one only what
// it lacks outside what the write clears, and the published one is handed to the app (released to the foreign queue
// family) with that submission. Once the next frame's first clear can be counted on (below), the hand-over is at the
// picture's own transfer; until then at the first of: the next display transfer that reads the target (the hidden touch
// screen's, right after the top one), a write into it (a pass, rows loaded, tiles copied in), the VBlank that takes the
// picture. Where it does not come at a write, what the next frame's first pass is expected to write whole
// (the largest box a pass into the target began with a draw that writes every pixel of it: FirstDrawClears, or a pass
// that loads but whose first draw covers it: MH4U's scene quad) is not copied but owed, once that expectation held twice
// in a row (trust): a later write that clears it settles it, anything else that reaches it takes it back (counted,
// target_owed_taken). By the Vulkan rules for memory another API uses, an image released to the foreign queue family is
// the other side's until it is done with it, and an ownership transfer or a layout change may itself read and write the
// image's memory while the app's OpenGL ES samples it (4u-native #31: a PowerVR picture that went dark every second). So
// the hidden touch screen's snapshot, which reads the published picture's rows right after its hand-over (every frame
// once trusted), is taken at the hand-over, while the image is still the renderer's: the same blit, earlier, into an
// image the snapshot then adopts (PretakeSnapshot). Owed pixels taken back while the app still holds the picture (rare:
// the game sampling its last picture before the expected clear, a fade) are taken from the published image itself, at
// full scale, and only on GPUs where that was seen to work (Mali and Adreno with their own drivers, Vulkan 1.2 or newer:
// OwingAllowed); elsewhere nothing is owed: the box is copied at the hand-over. Once no picture of the image is left,
// the owed pixels are taken from the image itself, acquired back and kept.
// A hand-over that copies more than a quarter of the picture three times in a row (the touch screen shown: its draws come
// into the target right after the transfer and keep little of it) sends the target back to the upright copy for a
// while (120 transfers, doubling up to 1920).
// The images rotate: the target's image, its twin (ping-pong, TargetAsTexture), the ones the app and the display still
// hold, a free one or two (6 at most, 8 for a twin); each knows the guest rows where it differs from the target's image
// (TargetBuffer::behind; the twin's are Target::twin_rows) and is brought up to date in those only.
struct TargetBuffer {
    std::shared_ptr<rhi::SharedImage> shared;
    std::shared_ptr<rhi::Image> image;  // the backend's image over it (holding `shared`)
    // the pictures of it that live (SharedScreenPicture): while there is one, nothing writes it
    std::shared_ptr<std::atomic<int>> holders = std::make_shared<std::atomic<int>>(0);
    // the submission that hands it to the app for the pictures of it (kNotYet until submitted): a new one each time it
    // becomes the target's image
    static constexpr u64 kNotYet = ~0ull;
    std::shared_ptr<std::atomic<u64>> released_at = std::make_shared<std::atomic<u64>>(kNotYet);
    bool release_owed = false;  // its Release is recorded with the next submission (RecordScreenReleases)
    bool released = false;      // the app's side has it: acquired back before the renderer touches it again
    std::vector<std::pair<u32, u32>> behind;  // a spare's guest rows where it differs from the target's image
    // Of those rows, the ones where it may differ in any column (behind_wide); in the others only left of behind_x1 (image
    // pixels), as Target::twin_wide and twin_x1 say for the twin. MH4U's top-screen draws stop at column 744 of 768 at 3x,
    // so a hand-over copies nothing beside the box the next frame's first draw clears (that 24-column strip was 37,440
    // pixels a frame on the Galaxy). MH4N_HANDOVER_COLUMNS=0: whole rows, as before.
    std::vector<std::pair<u32, u32>> behind_wide;
    u32 behind_x1 = 0;
    void BehindNone() {
        behind.clear();
        behind_wide.clear();
        behind_x1 = 0;
    }
    void BehindWhole(u32 first, u32 last) {
        behind.assign(1, {first, last});
        behind_wide = behind;
        behind_x1 = 0;
    }
    // a sync file that signals once its hand-over's submission is done (made right after it): what the app waits on,
    // rather than one made at the VBlank, which also waits for the next frame's work submitted by then
    std::shared_ptr<rhi::SyncFile> sync;
};

struct ScreenTarget {
    std::vector<std::unique_ptr<TargetBuffer>> buffers;  // every image of the target, its image and twin among them
    TargetBuffer* image = nullptr;                       // the one under Target::image
    TargetBuffer* twin = nullptr;                        // under Target::twin (null: none)
    // pixels of the image (rects of image pixels) it lacks, still in `owed_from` (handed over before a write): what the
    // next frame's first pass is expected to write whole
    std::vector<rhi::Rect> owed;
    TargetBuffer* owed_from = nullptr;
    // The hidden touch screen's snapshot of `pretaken_of` (the published image that owes), taken at its hand-over while
    // the image was still the renderer's (PretakeSnapshot): guest rows [pretaken_first, + pretaken_rows) at 1x, adopted
    // by the snapshot that reads those rows of it. The rows the last hidden touch screen snapshot of the target read
    // (snap_rows 0: none yet) say which rows to take.
    std::shared_ptr<rhi::Image> pretaken;
    const TargetBuffer* pretaken_of = nullptr;
    u32 pretaken_first = 0, pretaken_rows = 0;
    u32 snap_first = 0, snap_rows = 0;
    // the image the last hand-over copied the box from, owing nothing, while the target's image has not been written
    // since (OwedCheck: a snapshot of a later picture compared with it differed by what the frame drew)
    const TargetBuffer* box_from = nullptr;
    rhi::Rect clear_box{};      // the largest box a pass's first draw wrote whole (a clear), the frame before
    rhi::Rect clear_box_now{};  // ... in the frame being drawn
    rhi::Rect watch{};          // after a hand-over before a write that owed nothing: the clear expected first (trust)
    u32 trust = 0;              // the expected clear came first after that many hand-overs in a row
    u64 picture_px = 0;         // the upright picture's pixels (what the copy it replaces draws)
    u32 expensive = 0;          // hand-overs in a row that copied more than a quarter of that
    u32 fallback = 0;           // transfers left that draw the upright copy (a hand-over copied much: the touch screen shown)
    u32 fallback_next = 120;    // the next such pause (it doubles, up to 1920)

    // Every image but the target's and its twin falls behind in guest rows [first, last), in image columns under `x1`
    // (~0u: any).
    void Behind(u32 first, u32 last, u32 x1 = ~0u) {
        for (const auto& b : buffers) {
            if (b.get() != image && b.get() != twin) {
                JoinRows(b->behind, first, last);
                if (x1 == ~0u) {
                    JoinRows(b->behind_wide, first, last);
                } else {
                    b->behind_x1 = std::max(b->behind_x1, x1);
                }
            }
        }
    }
};

struct Target {
    std::shared_ptr<rhi::Image> image;  // width x height times `scale`
    u32 scale = 1;                      // the internal scale, or 1 for a shadow map (GetTarget)
    bool depth = false;
    u32 paddr = 0;
    u32 format = 0;  // the PICA's colour or depth format
    u32 width = 0, height = 0;  // the PICA's size (1x)
    u32 bytes = 0;
    bool shadow = false;     // a shadow source (N2-12): the shadow-write pass stores into it, Shadow2D lookups read it
    u64 off_version = ~0ull;  // shadows off (SetShadows): `version` when it was last cleared to "nothing in the way"
    bool gpu_dirty = false;  // drawn into since the last write-back
    // the guest rows those draws touched (whole rows of tiles; first >= last: none), which is what a write-back writes
    u32 dirty_first = 0, dirty_last = 0;
    bool stale = true;       // guest memory may be newer than the image (and has never been loaded)
    // Colour targets: guest rows [first, last) where guest memory may be newer than the image (whole rows of tiles;
    // MarkStale). Never rows the draws since the last write-back touched (those are written back before they go stale).
    std::vector<std::pair<u32, u32>> stale_rows;
    // what last made it stale, whole or in rows, and in which game frame (MH4N_SCALE_LOG)
    const char* stale_why = "never loaded";
    u32 stale_frame = 0;
    std::shared_ptr<rhi::Image> upload;  // scaled colour targets: the 1x image a load from guest memory goes through
    u64 drawn = 0;                       // the draw serial of the last draw into it
    u64 version = 0;                     // bumped whenever its image changes (a draw, a load, tiles copied into it)
    // Another target's tiles copied into this one (CopyTiles): the source, its version then, and the guest rows of this
    // target they cover. Copied again only when the source has changed since; a change of those rows here forgets it.
    struct Copied {
        const Target* src = nullptr;
        u64 src_version = 0;
        u32 first = 0, last = 0;
    };
    std::vector<Copied> copied;
    // The render area of its passes (image pixels): the box the draws into it have covered so far, out to 16 pixels (it
    // only grows). Outside it a pass loads and stores nothing (plume patch 15). Width 0: no draw yet.
    rhi::Rect area{};
    // Depth targets: the image has been cleared whole once (a new image's contents are undefined), and a clear to 1.0
    // is owed (LoadTarget): the next pass that draws with it clears its render area, which holds every pixel drawn since
    // that whole clear (the area only grows), or anything else that reads the image clears it whole first.
    bool cleared_once = false;
    bool clear_pending = false;
    // The boxes the passes drawing into it covered, by their order since the last display transfer
    // (PassAreaLearned), and the order of the next one
    // this order's, and the two before it's (MH4U's display transfers alternate between the two screens: the order two
    // transfers back is the same screen's)
    std::vector<rhi::Rect> pass_boxes, pass_boxes_before, pass_boxes_before2;
    u32 pass_index = 0;
    // Sampled while it is the draw's own target (TargetAsTexture): its copy, and the guest rows changed since that copy
    // was brought up to date ([first, last) ranges, apart and in order).
    std::shared_ptr<rhi::Image> twin;
    std::vector<std::pair<u32, u32>> twin_rows;
    // Of those rows, the ones where they may differ in any column (twin_wide); in the others only left of twin_x1 (image
    // pixels). A draw changes its scissor's columns only: MH4U's scene draws reach column 744 of 768 at 3x and its
    // ping-pong boxes 720, so a ping-pong's twin is given 24 columns beside its box, not 48 (gpu4).
    std::vector<std::pair<u32, u32>> twin_wide;
    u32 twin_x1 = 0;
    // The top screen's target: its images are shared, and its picture is its own image (ScreenTarget, gpu3).
    std::unique_ptr<ScreenTarget> screen;
    bool screen_failed = false;  // it could not be made one

    bool Overlaps(u32 begin, u32 end) const { return paddr < end && begin < paddr + bytes; }

    // The twin and the image now differ in guest rows [first, last), in image columns under `x1` (~0u: any).
    void TwinDiffers(u32 first, u32 last, u32 x1 = ~0u) {
        if (first >= last) {
            return;
        }
        if (twin_rows.empty()) {
            twin_wide.clear();
            twin_x1 = 0;
        }
        if (x1 == ~0u) {
            JoinRowsInto(twin_wide, first, last);
        } else {
            twin_x1 = std::max(twin_x1, x1);
        }
        JoinRowsInto(twin_rows, first, last);
    }
    void TwinSame() {
        twin_rows.clear();
        twin_wide.clear();
        twin_x1 = 0;
    }
    static void JoinRowsInto(std::vector<std::pair<u32, u32>>& rows, u32 first, u32 last) {
        auto it = std::lower_bound(rows.begin(), rows.end(), std::pair<u32, u32>{first, last});
        it = rows.insert(it, {first, last});
        if (it != rows.begin() && std::prev(it)->second >= it->first) {
            --it;  // joins the range before it
            it->second = std::max(it->second, std::next(it)->second);
            rows.erase(std::next(it));
        }
        while (std::next(it) != rows.end() && std::next(it)->first <= it->second) {
            it->second = std::max(it->second, std::next(it)->second);
            rows.erase(std::next(it));
        }
    }

    // Its image changed in guest rows [first, last) (whole rows of tiles), in image columns under `x1` (~0u: any).
    // `copy`: tiles copied in by CopyTiles, which records them itself.
    void Changed(u32 first, u32 last, bool copy = false, u32 x1 = ~0u) {
        ++version;
        if (first >= last) {
            return;
        }
        TwinDiffers(first, last, x1);
        if (screen != nullptr) {
            screen->Behind(first, last, x1);  // (its other images too)
        }
        if (!copy) {
            std::erase_if(copied, [&](const Copied& c) { return c.first < last && first < c.last; });
        }
    }
};

// ---- the screens at the internal scale (scaled_screens.h) ---------------------------------------------------------

// The guest memory a GX command writes, as physical ranges; `all` when it cannot be told.
struct GxWrites {
    std::vector<std::pair<u32, u32>> ranges;  // [begin, end)
    bool all = false;
    bool Overlaps(u32 begin, u32 end) const {
        if (all) return true;
        for (const auto& [b, e] : ranges) {
            if (b < end && begin < e) return true;
        }
        return false;
    }
};

// A screen's picture drawn into a shared image (scaled_screens.h, SHARED SCREENS), as the display publishes it, or a
// screen target's own image (gpu3, `layout`). While it lives its buffer is not drawn into again (`holders` counts the
// pictures of it); it keeps the hardware buffer itself, and only a weak hold on the backend (to tell whether the GPU has
// finished it). `ready`: the submission after which the app may read it (a target's: its hand-over, set once submitted).
class SharedScreenPicture final : public present::SharedPicture {
public:
    SharedScreenPicture(std::shared_ptr<std::atomic<int>> holders, std::shared_ptr<void> buffer, u64 id, u32 width,
                        u32 height, std::weak_ptr<rhi::Backend> backend, std::shared_ptr<std::atomic<u64>> ready,
                        std::optional<present::TargetLayout> layout = std::nullopt)
        : holders_(std::move(holders)), buffer_(std::move(buffer)), id_(id), width_(width), height_(height),
          backend_(std::move(backend)), ready_(std::move(ready)), layout_(layout) {
        holders_->fetch_add(1, std::memory_order_relaxed);
    }
    ~SharedScreenPicture() override { holders_->fetch_sub(1, std::memory_order_release); }

    void* HardwareBuffer() const override { return buffer_.get(); }
    u64 BufferId() const override { return id_; }
    u32 BufferWidth() const override { return width_; }
    u32 BufferHeight() const override { return height_; }
    const present::TargetLayout* Target() const override { return layout_ ? &*layout_ : nullptr; }
    std::shared_ptr<const present::SharedPicture> Reference() const override { return reference_; }
    bool Ready() const override {
        const u64 ready = ready_->load(std::memory_order_acquire);
        const std::shared_ptr<rhi::Backend> backend = backend_.lock();
        if (backend == nullptr || (ready != TargetBuffer::kNotYet && backend->CompletedValue() >= ready)) {
            return true;
        }
        // Its sync file says so as well, and can say it first: on the Anbernic's Mali the timeline's value was still
        // short of it when the app woke on the sync file, and the app slept on to its next turn (a picture a frame late
        // and the next one with it, three times in ten, where the sync file covered nothing after the picture's work).
        std::lock_guard lock(mutex_);
        return ready != TargetBuffer::kNotYet && sync_ != nullptr && sync_->Signaled();
    }
    int TakeSyncFd() const override {
        if (Ready()) {
            return -1;
        }
        std::lock_guard lock(mutex_);
        return sync_ != nullptr ? sync_->Dup() : -1;
    }
    bool WaitReady(u64 timeout_ns) const override {
        const u64 ready = ready_->load(std::memory_order_acquire);
        const std::shared_ptr<rhi::Backend> backend = backend_.lock();
        return backend == nullptr ||
               (ready != TargetBuffer::kNotYet && rhi::SharedImages::WaitFor(*backend, ready, timeout_ns));
    }
    // MH4N_TARGET_SCREENS_CHECK=1: the upright copy of the same screen
    void SetReference(std::shared_ptr<const present::SharedPicture> reference) { reference_ = std::move(reference); }

    bool HasSync() const {
        std::lock_guard lock(mutex_);
        return sync_ != nullptr;
    }
    void SetSync(std::shared_ptr<rhi::SyncFile> sync) const {
        std::lock_guard lock(mutex_);
        if (sync_ == nullptr) {
            sync_ = std::move(sync);
        }
    }

private:
    std::shared_ptr<std::atomic<int>> holders_;
    std::shared_ptr<void> buffer_;
    u64 id_;
    u32 width_, height_;
    std::weak_ptr<rhi::Backend> backend_;
    std::shared_ptr<std::atomic<u64>> ready_;
    std::optional<present::TargetLayout> layout_;
    std::shared_ptr<const present::SharedPicture> reference_;
    mutable std::mutex mutex_;
    mutable std::shared_ptr<rhi::SyncFile> sync_;  // set once, at the first publication that finds it unfinished
};

// A buffer of the shared screens' pool (Impl::TakeSharedSlot).
struct SharedSlot {
    std::shared_ptr<rhi::SharedImage> image;
    std::shared_ptr<std::atomic<int>> busy = std::make_shared<std::atomic<int>>(0);  // the pictures of it that live
    bool released = false;  // handed to the other API at least once: taken back (Acquire) before the next draw
    u64 last_used = 0;      // draw_serial_ of its last draw
};

// What a display transfer drew for one framebuffer (its output): the upright screen at N x and its copy for the host.
struct ScaledScreen {
    u32 address = 0;           // the transfer's output (physical)
    u32 bytes = 0;             // the bytes it wrote there
    u32 format = 0;            // its output format (gx::PixelFormat, the display's numbering)
    u32 width = 0, height = 0; // its output at 1x: pixels per framebuffer row, rows
    std::shared_ptr<rhi::Image> image;     // (height x N) x (width x N), upright
    std::shared_ptr<rhi::Buffer> readback; // the image's pixels, rows packed
    rhi::TimelineValue ready = 0;          // the submission that filled `readback`
    // A shared screen: the picture went into a shared buffer instead of `image`, and `readback` does not hold it.
    std::shared_ptr<const SharedScreenPicture> picture;
    bool valid = false;                    // nothing has written the framebuffer since
    // An asynchronous transfer (--async-screens): the GX engine did not blit, so the framebuffer's memory is not written
    // yet. Materialize writes it when something is about to read that memory: the transfer's source rows, kept at 1x
    // in `snapshot` (in a target's row order) when the transfer ran, read back and blitted by the CPU as the engine
    // would have.
    bool deferred = false;
    gx::TransferRegs regs;                 // the transfer as the engine ran it (physical addresses)
    u32 source_format = 0;                 // the source target's colour format
    u32 snapshot_rows = 0;                 // the source's guest rows in `snapshot`, from the transfer's input address
    std::shared_ptr<rhi::Image> snapshot;
};

// The host a deferred transfer is blitted through: its input window is served from `bytes` (the source rows read back
// from the snapshot), everything else is the real host's.
class InputOverlay final : public Host {
public:
    InputOverlay(Host& real, u32 base, std::vector<u8>& bytes) : real_(real), base_(base), bytes_(bytes) {}
    u8* PhysicalPointer(u32 paddr, u32 size) override {
        if (paddr >= base_ && u64{paddr} + size <= u64{base_} + bytes_.size()) {
            return bytes_.data() + (paddr - base_);
        }
        return real_.PhysicalPointer(paddr, size);
    }
    std::optional<u32> VirtualToPhysical(u32 vaddr) override { return real_.VirtualToPhysical(vaddr); }
    u8* GuestPointer(u32 vaddr, u32 size) override { return real_.GuestPointer(vaddr, size); }
    void SignalInterrupt(Interrupt id, u64 delay_ns) override { real_.SignalInterrupt(id, delay_ns); }
    u64 InterruptDelayNs() const override { return real_.InterruptDelayNs(); }
    void Post(std::function<void()> work) override { real_.Post(std::move(work)); }
    void* CodeSymbol(const char* name) override { return real_.CodeSymbol(name); }

private:
    Host& real_;
    u32 base_;
    std::vector<u8>& bytes_;
};

// The bits of r, g, b of a display or transfer format (gx::PixelFormat): RGBA8, RGB8, RGB565, RGB5A1, RGBA4.
std::array<u32, 3> TransferBits(u32 format) {
    switch (format) {
    case 2: return {5, 6, 5};
    case 3: return {5, 5, 5};
    case 4: return {4, 4, 4};
    default: return {8, 8, 8};
    }
}

// A PICA colour-buffer format as the transfer format of the same layout (RGB5A1 and RGB565 swap numbers).
u32 ColorToTransferFormat(u32 color_format) {
    switch (static_cast<ColorFormat>(color_format)) {
    case ColorFormat::RGB5A1: return 3;
    case ColorFormat::RGB565: return 2;
    default: return color_format;
    }
}

// The full-screen triangle and the mapping of a display transfer and the display (Impl::DrawScaledScreen).
constexpr const char* kScreenVertexGlsl = R"(#version 450
void main() {
    vec2 uv = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
)";

// One pixel (x, y) of the upright screen at N x. The display shows column x, row y of the screen from the framebuffer's
// pixel x * W + W - y (DisplayShadow::BuildScreen: W pixels per framebuffer row, the one-pixel shift included); at N x
// that is (x + 1) * W' - y + N - 1 with W' = W * N, so each 1x pixel becomes its N x N block. That framebuffer pixel
// (row fy, column fx) came from the transfer's input row fy (flipped: H' - 1 - fy) and column fx, and the transfer's input
// row r is image row `first_row - r` of the target (the image is stored bottom up). The colour goes through the depths
// the 1x path keeps: the target's format (the write-back cuts each component to its bits, the transfer expands them, as
// Azahar's Convert8To5 and Convert5To8 do) and the transfer's output format (cut, then expanded by the display).
constexpr const char* kScreenFragmentGlsl = R"(#version 450
#extension GL_EXT_samplerless_texture_functions : require
layout(set = 0, binding = 0) uniform texture2D target;
layout(std140, set = 0, binding = 1) uniform Params {
    ivec4 map;       // x: N, y: W' (pixels per framebuffer row at N x), z: H' (rows the transfer wrote at N x), w: flipped
    ivec4 first;     // x: the target's image row of the transfer's input row 0
    uvec4 bits_in;   // bits of r, g, b of the target's format
    uvec4 bits_out;  // bits of r, g, b of the transfer's output format
} p;
layout(location = 0) out vec4 color;

uint Expand(uint v, uint bits) {
    if (bits >= 8u) return v;
    if (bits == 6u) return (v << 2) | (v >> 4);
    if (bits == 5u) return (v << 3) | (v >> 2);
    if (bits == 4u) return (v << 4) | v;
    return v * 255u;
}

float Depths(float f, uint bits_in, uint bits_out) {
    uint v = uint(roundEven(clamp(f, 0.0, 1.0) * 255.0));
    v = Expand(v >> (8u - bits_in), bits_in);
    v = Expand(v >> (8u - bits_out), bits_out);
    return float(v) / 255.0;
}

void main() {
    ivec2 o = ivec2(gl_FragCoord.xy);
    int n = p.map.x, w = p.map.y, h = p.map.z;
    // index = (o.x + 1) * w - o.y + n - 1, fy = index / w, fx = index % w, without the divide (a long sequence on a
    // Mali): 0 <= o.y < w, so index / w is o.x, or o.x + 1 for the first n columns (o.y < n)
    int k = o.y < n ? 1 : 0;
    int fy = o.x + k;
    int fx = w + n - 1 - o.y - k * w;
    if (fy >= h) {
        color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    int row = p.map.w != 0 ? h - 1 - fy : fy;
    vec4 t = texelFetch(target, ivec2(fx, p.first.x - row), 0);
    if (all(equal(p.bits_in.xyz, uvec3(8u))) && all(equal(p.bits_out.xyz, uvec3(8u)))) {
        color = vec4(t.rgb, 1.0);  // (8 bits in and out: the depths change nothing)
        return;
    }
    color = vec4(Depths(t.r, p.bits_in.x, p.bits_out.x), Depths(t.g, p.bits_in.y, p.bits_out.y),
                 Depths(t.b, p.bits_in.z, p.bits_out.z), 1.0);
}
)";

struct ScreenParams {
    s32 map[4];
    s32 first[4];
    u32 bits_in[4];
    u32 bits_out[4];
};

// Another target's tiles drawn into a target (Impl::DrawTiles, gpu4): the copy's own regions (TileRegions: a rectangle
// of the destination's image and the source's pixel at its corner, TileRegionBlock), one instance of two triangles
// each, a texel fetched at the same offset for each pixel. A rectangle's edges are whole pixels, so it covers exactly
// its pixels' centres. The copy is exact: RGBA8 to float and back gives the same bytes.
constexpr u32 kTileRegionsPerDraw = 256;
constexpr const char* kTileVertexGlsl = R"(#version 450
layout(std140, set = 0, binding = 1) uniform Regions {
    vec4 size;       // x, y: the destination image's width and height
    ivec4 r[512];    // a region: x, y, width, height in the destination; then the source's x and y less those
} p;
layout(location = 0) flat out ivec2 offset;
void main() {
    ivec4 d = p.r[2 * gl_InstanceIndex];
    int c = gl_VertexIndex;  // two triangles: (0,0) (1,0) (1,1), (0,0) (1,1) (0,1)
    int cx = (c == 1 || c == 2 || c == 4) ? 1 : 0;
    int cy = (c == 2 || c == 4 || c == 5) ? 1 : 0;
    vec2 xy = vec2(float(d.x + cx * d.z), float(d.y + cy * d.w));
    gl_Position = vec4(xy / p.size.xy * 2.0 - 1.0, 0.0, 1.0);
    offset = p.r[2 * gl_InstanceIndex + 1].xy;
}
)";
constexpr const char* kTileFragmentGlsl = R"(#version 450
#extension GL_EXT_samplerless_texture_functions : require
layout(set = 0, binding = 0) uniform texture2D src;
layout(location = 0) flat in ivec2 offset;
layout(location = 0) out vec4 color;
void main() {
    color = texelFetch(src, ivec2(gl_FragCoord.xy) + offset, 0);
}
)";

struct TileRegionBlock {
    float size[4];
    s32 r[2 * kTileRegionsPerDraw][4];
};

// ---- the textures (N2-23) ------------------------------------------------------------------------------------------------

// A texture image is keyed by what the cache is keyed by: a 2D texture by one cache key, a cube by its six faces.
struct TexImageKey {
    bool cube = false;
    std::array<texture::CacheKey, 6> faces{};
    friend bool operator<(const TexImageKey& a, const TexImageKey& b) { return std::tie(a.cube, a.faces) < std::tie(b.cube, b.faces); }
};
struct TexImage {
    std::shared_ptr<rhi::Image> image;
    std::array<u32, 6> generation{};  // of the cache's decoded face the image holds
    u64 bytes = 0;
    u64 stamp = 0;  // the recording that last bound it
    u64 used_frame = 0;  // the frame (the texture cache's count of screen pictures) that last bound it
};

// ---- the GPU --------------------------------------------------------------------------------------------------------

class Impl final : public NativeGpu, public pica::Backend, public ScaledScreens {
public:
    Impl(Host& host, const NativeOptions& options)
        : host_(host), options_(options), gx_(host), processor_(host, *this), registry_(&fragment::FeatureRegistry::Global()),
          scale_(std::clamp<u32>(options.scale, 1, kMaxScale)), async_(options.async_screens) {
        processor_.SetRegisterWriteLog(true);
        if (scale_ > 1) {
            MH4N_LOG(Info, kLog, "internal resolution {}x", scale_);
        }
        // An asynchronous display transfer (TryAsyncTransfer) asks the engine to leave its blit out.
        gx_.SetDisplayTransferHook([this](const gx::TransferRegs&) {
            const bool skip = skip_blit_;
            skip_blit_ = false;
            return skip;
        });
    }

    ~Impl() override {
        if (check_thread_.joinable()) {  // (the pipeline check, over before the backend is let go here)
            check_thread_.join();
        }
        if (renderer_ready_) {
            FlushRecording();
            backend_->WaitIdle();
        }
    }

    // ---- Gpu ----
    void Execute(const GxCommand& command) override {
        FlushHeldCpuDraw();
        if ((command.words[0] & 0xFF) == 1) {  // SubmitCmdList
            const u32 vaddr = command.words[1];
            const u32 paddr = vaddr == 0 ? 0 : host_.VirtualToPhysical(vaddr).value_or(vaddr);
            processor_.SubmitGxCmdList(paddr, command.words[2]);
            FlushRecording();
            return;
        }
        if ((command.words[0] & 0xFF) == static_cast<u32>(gx::CommandId::CacheFlush)) {
            // FlushCacheRegions: the CPU wrote up to three regions (address, size pairs), like FlushDataCache.
            for (u32 i = 0; i < 3; ++i) {
                const u32 vaddr = command.words[1 + 2 * i], size = command.words[2 + 2 * i];
                if (vaddr != 0 && size != 0) {
                    FlushRegion(vaddr, size);
                }
            }
            gx_.Execute(command);
            return;
        }
        // Fills, transfers, copies and DMA are the CPU engine's: the memory they read must be what the GPU drew, and
        // the targets they write must be loaded again afterwards. Only the targets in their ranges: a write-back waits
        // for the GPU, which on some drivers (Mali-G52) costs milliseconds each time, and a frame has several of
        // these commands (every target written back before each, as before: MH4N_GX_WRITEBACK_ALL=1).
        static const bool all = [] {
            const char* v = std::getenv("MH4N_GX_WRITEBACK_ALL");
            return v != nullptr && *v != '0';
        }();
        if (renderer_ready_ && (command.words[0] & 0xFF) == static_cast<u32>(gx::CommandId::DisplayTransfer)) {
            ++ab_transfers_;
            if (texture_cache_) {
                texture_cache_->NextFrame();  // (a screen's picture is done: each texture's spot check once a picture)
                MemoryHousekeeping();
            }
            if (AbFrames() != 0 && fragment_state_ != nullptr && AbOffFor("shaders") != fragment::ShadersAsBefore()) {
                fragment::SetShadersAsBefore(AbOffFor("shaders"));
                fragment_state_->Invalidate();
            }
            TraceTransfer(GxPhysical(command.words[1]), GxPhysical(command.words[2]));
            FragStatsTransfer();
            NoteScreenTransfer(command);
            for (auto& t : targets_) {
                if (t->pass_index != 0) {
                    t->pass_boxes_before2 = std::move(t->pass_boxes_before);
                    t->pass_boxes_before = std::move(t->pass_boxes);
                    t->pass_boxes.clear();
                }
                t->pass_index = 0;
            }
        }
        // --async-screens: a display transfer of a target the GPU is still drawing goes to the GPU, with no wait.
        if (async_ && !all && (command.words[0] & 0xFF) == static_cast<u32>(gx::CommandId::DisplayTransfer) &&
            TryAsyncTransfer(command)) {
            return;
        }
        const CauseScope cause(*this, WritebackCause::Gx);
        const StaleWhy why(*this, GxName(command.words[0] & 0xFF));
        GxRanges ranges;
        const bool ranged = !all && GxCommandRanges(command, &ranges);
        const GxWrites precise = WritesOf(command);
        SettleDeferred(ranged ? &ranges : nullptr, &precise);
        if (!ranged) {
            WriteBackAll();
        } else if (renderer_ready_) {
            for (auto& t : targets_) {
                bool touched = false;
                for (const auto& [begin, end] : ranges.reads) touched |= DirtyOverlaps(*t, begin, end);
                for (const auto& [begin, end] : ranges.writes) touched |= DirtyOverlaps(*t, begin, end);
                if (touched) {
                    WriteBack(*t);  // (a target written in part keeps the rest)
                }
            }
        }
        if (scale_ > 1) {
            // Scaled: only the rows of tiles the command writes are loaded again (a load of anything else would bring back
            // its 1x picture); a depth target it writes starts again at 1.0, as at 1x.
            const GxWrites writes = WritesOf(command);
            ForgetScaledScreens(writes);
            bool screen = false;
            if ((command.words[0] & 0xFF) == static_cast<u32>(gx::CommandId::DisplayTransfer)) {
                screen = DrawScaledScreen(command) != nullptr;
            }
            if (ScaleLogLimit() != 0) {
                ScaleLogGx(command, ranged ? &ranges : nullptr, screen);
                if (writes.all) {
                    ScaleLog(fmt::format("gx {} writes where the engine's registers say: every target stale",
                                         GxName(command.words[0] & 0xFF)));
                }
            }
            gx_.Execute(command);
            for (auto& t : targets_) {
                if (writes.all || (!ranged && t->depth)) {
                    t->stale = true;
                    NoteStale(*t);
                    continue;
                }
                for (const auto& [begin, end] : writes.ranges) {
                    if (t->depth) {
                        if (t->Overlaps(begin, end)) t->stale = true;
                    } else {
                        MarkStale(*t, begin, end);
                    }
                }
            }
        } else {
            if (async_) {
                ForgetScaledScreens(WritesOf(command));  // the GPU's pictures of the framebuffers it writes
            }
            gx_.Execute(command);
            if (!ranged) {
                MarkAllStale();
            } else if (renderer_ready_) {
                for (auto& t : targets_) {
                    for (const auto& [begin, end] : ranges.writes) {
                        if (t->depth) {
                            if (t->Overlaps(begin, end)) t->stale = true;
                        } else {
                            MarkStale(*t, begin, end);  // the rows of tiles it wrote
                        }
                    }
                }
            }
        }
        if (texture_cache_) {
            if (!ranged) {
                texture_cache_->OnGpuWrite(0, 0xFFFFFFFFu);  // a fill, transfer or copy may have written any texture's memory
            } else {
                for (const auto& [begin, end] : ranges.writes) texture_cache_->OnGpuWrite(begin, end - begin);
            }
        }
    }

    // The physical ranges a GX command reads and writes (GxEngine::Execute's word layout), generous where its size
    // depends on gaps; false when an address is not physical memory (then every target, as before).
    struct GxRanges {
        std::vector<std::pair<u32, u32>> reads, writes;
    };
    bool GxCommandRanges(const GxCommand& command, GxRanges* out) {
        const u32* w = command.words;
        auto range = [&](u32 vaddr, u64 size, std::vector<std::pair<u32, u32>>& to) {
            if (vaddr == 0 || size == 0) return true;
            const std::optional<u32> p = host_.VirtualToPhysical(vaddr);
            if (!p) return false;
            to.emplace_back(*p, static_cast<u32>(std::min<u64>(u64{*p} + size, 0xFFFFFFFFull)));
            return true;
        };
        switch (w[0] & 0xFF) {
        case 0:  // RequestDma: source, destination, size
            return range(w[1], w[3], out->reads) && range(w[2], w[3], out->writes);
        case 2:  // MemoryFill: start1 value1 end1 start2 value2 end2
            return (w[1] == 0 || w[3] <= w[1] || range(w[1], w[3] - w[1], out->writes)) &&
                   (w[4] == 0 || w[6] <= w[4] || range(w[4], w[6] - w[4], out->writes));
        case 3: {  // DisplayTransfer: in out in_size out_size flags (sizes: width | height << 16; 4 bytes a pixel at most)
            const u64 in_bytes = u64{w[3] & 0xFFFF} * (w[3] >> 16) * 4, out_bytes = u64{w[4] & 0xFFFF} * (w[4] >> 16) * 4;
            return range(w[1], in_bytes, out->reads) && range(w[2], out_bytes, out->writes);
        }
        case 4: {  // TextureCopy: in out size in_gap out_gap flags (the gaps can double the span: twice the size)
            const u64 bytes = u64{w[3]} * 2 + 0x1000;
            return range(w[1], bytes, out->reads) && range(w[2], bytes, out->writes);
        }
        case 5:  // FlushCacheRegions: nothing is cached here
            return true;
        default:
            return false;
        }
    }

    void WriteReg(u32 addr, u32 value) override {
        FlushHeldCpuDraw();
        const u32 window = addr & 0xFFFFF000;
        if (window == pica::kGpuMmioBase || window == pica::kGpuMmioBase + 0x1000) {
            const u32 index = (addr - pica::kGpuMmioBase) / 4;
            if (index >= pica::kExternalRegs) {
                if (index - pica::kExternalRegs < pica::kNumRegs) {
                    processor_.WriteHardwareRegister(index - pica::kExternalRegs, value);
                }
                return;
            }
        }
        const bool trigger = addr == pica::kGpuMmioBase + 4 * gx::kRegFillControl0 ||
                             addr == pica::kGpuMmioBase + 4 * gx::kRegFillControl1 ||
                             addr == pica::kGpuMmioBase + 4 * gx::kRegTransferTrigger;
        if (HasScreens() && trigger) {
            // a fill or transfer started by a register: where it reads and writes is not followed
            if (ScaleLogLimit() != 0) {
                ScaleLog(fmt::format("a fill or transfer started by register {:08x}: every screen's picture forgotten", addr));
            }
            MaterializeOverlapping(0, 0xFFFFFFFFu);
            ForgetScaledScreens(GxWrites{{}, true});
        }
        gx_.WriteReg(addr, value);
        if (trigger && texture_cache_ != nullptr) {
            // ... and any texture's memory may be what it wrote (the GX commands' fills and transfers name their ranges; these
            // were not told to the cache at all). Not seen from the game so far: the first few are logged.
            if (++register_triggers_ <= 3) {
                MH4N_LOG(Info, kLog, "a fill or transfer started by a register write ({:08x} = {:08x}): every texture is checked again",
                         addr, value);
            }
            texture_cache_->OnGpuWrite(0, 0xFFFFFFFFu);
        }
    }

    u32 ReadReg(u32 addr) override {
        const u32 window = addr & 0xFFFFF000;
        if (window == pica::kGpuMmioBase || window == pica::kGpuMmioBase + 0x1000) {
            const u32 index = (addr - pica::kGpuMmioBase) / 4;
            if (index >= pica::kExternalRegs) {
                return index - pica::kExternalRegs < pica::kNumRegs ? processor_.state().regs[index - pica::kExternalRegs] : 0;
            }
        }
        return gx_.ReadReg(addr);
    }

    void SetBufferSwap(u32 screen, const FrameBufferInfo& info) override { gx_.SetBufferSwap(screen, info); }
    void SetColorFill(u32 fill) override { gx_.SetColorFill(fill); }

    void FlushRegion(u32 vaddr, u32 size) override {
        // The CPU wrote this range: a target that covers it is out of date.
        if (!renderer_ready_) {
            return;
        }
        FlushHeldCpuDraw();
        const CauseScope cause(*this, WritebackCause::Cpu);
        const StaleWhy why(*this, "the CPU's flush");
        if (const std::optional<u32> paddr = host_.VirtualToPhysical(vaddr)) {
            // a deferred framebuffer there is written first, and what the CPU wrote since stays (Materialize)
            const u32 end = static_cast<u32>(std::min<u64>(u64{*paddr} + size, 0xFFFFFFFFull));
            MaterializeOverlapping(*paddr, end, /*keep_cpu_writes=*/true);
            WriteBackOverlapping(*paddr, size, /*stale=*/true);
            texture_cache_->OnFlush(*paddr, size);
            if (HasScreens()) {
                ForgetScaledScreens(GxWrites{{{*paddr, end}}, false});
            }
        } else {
            if (ScaleLogLimit() != 0) {
                ScaleLog(fmt::format("the CPU's flush of {:08x} (+{:x}), no physical address: every target stale", vaddr, size));
            }
            const StaleWhy no_physical(*this, "the CPU's flush of memory with no physical address");
            MaterializeOverlapping(0, 0xFFFFFFFFu);
            WriteBackAll();
            MarkAllStale();
            texture_cache_->OnFlush(0, 0xFFFFFFFFu);
            if (HasScreens()) {
                ForgetScaledScreens(GxWrites{{}, true});
            }
        }
    }

    void InvalidateRegion(u32 vaddr, u32 size) override {
        // The CPU is about to read this range: what the GPU drew there goes to memory first.
        if (!renderer_ready_) {
            return;
        }
        FlushHeldCpuDraw();
        const CauseScope cause(*this, WritebackCause::Cpu);
        if (const std::optional<u32> paddr = host_.VirtualToPhysical(vaddr)) {
            MaterializeOverlapping(*paddr, static_cast<u32>(std::min<u64>(u64{*paddr} + size, 0xFFFFFFFFull)));
            WriteBackOverlapping(*paddr, size, /*stale=*/false);
            texture_cache_->OnInvalidate(*paddr, size);
        } else {
            MaterializeOverlapping(0, 0xFFFFFFFFu);
            WriteBackAll();
            texture_cache_->OnInvalidate(0, 0xFFFFFFFFu);
        }
    }

    void VBlank(u32) override {}

    // ---- ScaledScreens ----
    u32 ScreenScale() const override { return scale_; }
    bool HasScreens() const override { return scale_ > 1 || async_; }

    ScreenRead ReadScaledScreen(u32 paddr, u32 format, u32 stride, u32 rows, std::vector<u8>& rgba, bool wait,
                                u64* sequence) override {
        if (!HasScreens() || !renderer_ready_) {
            return ScreenRead::None;
        }
        FlushHeldCpuDraw();
        for (ScaledScreen& s : scaled_screens_) {
            if (!s.valid || s.address != paddr) {
                continue;
            }
            if (s.format != format || stride != s.width * gx::BytesPerPixel(format) || rows == 0 || rows > s.height) {
                return ScreenRead::None;  // the display reads it otherwise than the transfer wrote it
            }
            if (s.picture != nullptr || s.readback == nullptr) {
                return ScreenRead::None;  // a shared screen: there is no copy for the CPU
            }
            if (s.ready > backend_->LastSubmittedValue()) {
                FlushRecording();
            }
            if (!wait && backend_->CompletedValue() < s.ready) {
                ++frame_.screens_pending;
                return ScreenRead::Pending;  // the GPU has not finished it: the caller shows the last one again
            }
            backend_->WaitIdle(s.ready);
            if (sequence != nullptr) {
                *sequence = s.ready;  // (timeline values grow with each submission)
            }
            const u64 image_width = static_cast<u64>(s.height) * scale_;  // the upright image: framebuffer rows across
            const u64 image_height = static_cast<u64>(s.width) * scale_;
            const u64 out_width = static_cast<u64>(rows) * scale_;
            s.readback->Invalidate(0, image_width * image_height * 4);
            const u8* data = s.readback->Mapped().data();
            rgba.resize(out_width * image_height * 4);
            if (out_width == image_width) {
                std::memcpy(rgba.data(), data, rgba.size());
            } else {
                for (u64 y = 0; y < image_height; ++y) {
                    std::memcpy(rgba.data() + y * out_width * 4, data + y * image_width * 4, out_width * 4);
                }
            }
            if (rows == s.height) {
                PutExtraPixel(paddr + stride * rows, format, rows, rgba);
            }
            return ScreenRead::Ready;
        }
        return ScreenRead::None;
    }

    // The display's read-out takes its last column's top pixel from one pixel past the framebuffer (gpu_capture's
    // DisplayShadow::BuildScreen, Azahar's index): past what the transfer wrote, so the picture has black there. The same
    // pixel from guest memory, as on the memory path, unless a framebuffer still owed covers it (then black stays).
    void PutExtraPixel(u32 paddr, u32 format, u32 rows, std::vector<u8>& rgba) {
        const u32 bpp = gx::BytesPerPixel(format);
        for (const ScaledScreen& s : scaled_screens_) {
            if (s.deferred && s.address < paddr + bpp && paddr < s.address + s.bytes) {
                return;
            }
        }
        const u8* p = bpp != 0 ? host_.PhysicalPointer(paddr, bpp) : nullptr;
        if (p == nullptr) {
            return;
        }
        u8 rgb[3];
        const u32 v = static_cast<u32>(p[0]) | (bpp > 1 ? static_cast<u32>(p[1]) << 8 : 0u);
        const auto c5 = [](u32 x) { return static_cast<u8>((x << 3) | (x >> 2)); };
        const auto c6 = [](u32 x) { return static_cast<u8>((x << 2) | (x >> 4)); };
        const auto c4 = [](u32 x) { return static_cast<u8>(x * 17); };
        switch (format) {
        case 0: rgb[0] = p[3], rgb[1] = p[2], rgb[2] = p[1]; break;  // RGBA8
        case 1: rgb[0] = p[2], rgb[1] = p[1], rgb[2] = p[0]; break;  // RGB8
        case 2: rgb[0] = c5((v >> 11) & 0x1F), rgb[1] = c6((v >> 5) & 0x3F), rgb[2] = c5(v & 0x1F); break;
        case 3: rgb[0] = c5((v >> 11) & 0x1F), rgb[1] = c5((v >> 6) & 0x1F), rgb[2] = c5((v >> 1) & 0x1F); break;
        default: rgb[0] = c4((v >> 12) & 0xF), rgb[1] = c4((v >> 8) & 0xF), rgb[2] = c4((v >> 4) & 0xF); break;
        }
        const u64 width = u64{rows} * scale_;
        for (u64 y = 0; y < scale_; ++y) {
            for (u64 x = width - scale_; x < width; ++x) {
                u8* out = rgba.data() + (y * width + x) * 4;
                out[0] = rgb[0], out[1] = rgb[1], out[2] = rgb[2], out[3] = 0xFF;
            }
        }
    }

    void PrepareMemory(u32 paddr, u32 size) override {
        FlushHeldCpuDraw();
        if (renderer_ready_ && size != 0) {
            MaterializeOverlapping(paddr, static_cast<u32>(std::min<u64>(u64{paddr} + size, 0xFFFFFFFFull)));
        }
    }

    void SetSharedScreens(bool on) override { shared_wanted_ = on; }

    void SetTouchScreenShown(bool shown) override {
        if (touch_shown_.exchange(shown, std::memory_order_relaxed) != shown) {
            const bool skip = touch_skip_enabled_.load(std::memory_order_relaxed);
            MH4N_LOG(Info, kLog, "the touch screen is {}",
                     shown ? "shown" : skip ? "hidden: its draws are left out" : "hidden (its draws are still made)");
        }
    }

    // A display transfer (before it runs). MH4U draws both screens into one target, in turns: the top screen's draws, its
    // transfer, the touch screen's draws, its transfer (a transfer to a 240x320 framebuffer), and again. While the touch
    // screen is hidden and the turns have kept alternating, the draws after a top screen's transfer into the target the
    // touch screen's transfer reads are its own: DrawImpl leaves them out (its picture then holds whatever the target
    // had, which nothing shows). A turn that breaks the order stops it at once: at worst the top screen repeats its last
    // picture once, as the target still holds it.
    void NoteScreenTransfer(const GxCommand& command) {
        const u32* w = command.words;
        gx::TransferRegs regs;
        regs.input_address = GxPhysical(w[1]) >> 3;
        regs.output_address = GxPhysical(w[2]) >> 3;
        regs.input_size = w[3];
        regs.output_size = w[4];
        regs.flags = w[5];
        const bool touch = regs.OutputWidth() == 240 && regs.OutputHeight() == 320;
        transfer_alternations_ = touch != last_transfer_touch_ ? std::min<u32>(transfer_alternations_ + 1, 1000) : 0;
        last_transfer_touch_ = touch;
        if (touch) {
            const u32 input = regs.InputAddress();
            touch_source_ = 0;
            for (const auto& t : targets_) {
                if (!t->depth && input >= t->paddr && input < t->paddr + t->bytes) {
                    touch_source_ = t->paddr;
                    break;
                }
            }
        }
        const bool hidden = touch_forced_hidden_ || (touch_skip_enabled_.load(std::memory_order_relaxed) &&
                                                     !touch_shown_.load(std::memory_order_relaxed));
        touch_phase_ = hidden && !touch && transfer_alternations_ >= 4 && touch_source_ != 0;
        if (touch_phase_ && !touch_phase_told_) {
            touch_phase_told_ = true;
            MH4N_LOG(Info, kLog, "the touch screen's draws into {:08x} are left out while it is hidden", touch_source_);
        }
    }

    ScreenRead ReadSharedScreen(u32 paddr, u32 format, u32 stride, u32 rows, SharedScreen& out) override {
        if (!HasScreens() || !renderer_ready_ || shared_ == nullptr) {
            return ScreenRead::None;
        }
        for (ScaledScreen& s : scaled_screens_) {
            if (!s.valid || s.address != paddr) {
                continue;
            }
            if (s.picture == nullptr || s.format != format || stride != s.width * gx::BytesPerPixel(format) || rows == 0 ||
                rows > s.height) {
                return ScreenRead::None;
            }
            if (s.picture->Target() != nullptr) {
                HandOverTargetPicture(s);  // (a screen target's image: handed to the app now, if nothing wrote it yet)
            }
            if (s.ready > backend_->LastSubmittedValue()) {
                FlushRecording();
            }
            // Not finished: a sync file for the reader's GPU to wait on (one per publication at most, shared by the
            // pictures it covers).
            if (backend_->CompletedValue() >= s.ready) {
                pacing::Mark(pacing::kSharedFence, 1000u);  // (finished when published)
            }
            if (!s.picture->HasSync() && backend_->CompletedValue() < s.ready && shared_->CanExportFences()) {
                if (shared_sync_ == nullptr || shared_sync_value_ < s.ready) {
                    shared_sync_ = shared_->ExportFence();
                    shared_sync_value_ = backend_->LastSubmittedValue();
                    pacing::Mark(pacing::kSharedFence, static_cast<u32>(shared_sync_value_ - s.ready));
                }
                if (shared_sync_ != nullptr) {
                    s.picture->SetSync(shared_sync_);
                }
            }
            out.picture = s.picture;
            out.region = present::ImageRegion{0, 0, rows * scale_, s.width * scale_};
            out.sequence = s.ready;
            return ScreenRead::Ready;
        }
        return ScreenRead::None;
    }

    // Shared images, made the first time they are asked for (null: shared screens are off, or the device cannot).
    rhi::SharedImages* EnsureShared() {
        if (!shared_wanted_) {
            return nullptr;
        }
        if (!shared_tried_) {
            shared_tried_ = true;
            std::string why;
            shared_ = rhi::SharedImages::Create(backend_, &why);
            if (shared_ != nullptr) {
                MH4N_LOG(Info, kLog, "the screens go to the display in shared buffers (nothing read back)");
            } else {
                MH4N_LOG(Info, kLog, "the screens are read back for the display: {}", why);
            }
        }
        return shared_.get();
    }

    // A buffer of the shared screens' pool of that size that no picture holds; one made when there is none (a few per
    // size: the two framebuffers of a screen each hold their last picture, the app the one it shows and the newest).
    // Null when shared screens are off, the device cannot share images, or too many are held.
    SharedSlot* TakeSharedSlot(u32 width, u32 height) {
        if (EnsureShared() == nullptr) {
            return nullptr;
        }
        constexpr size_t kMaxPerSize = 8, kMaxSlots = 16;
        size_t same_size = 0;
        SharedSlot* oldest_free = nullptr;
        for (const auto& slot : shared_slots_) {
            const bool fits = slot->image->Width() == width && slot->image->Height() == height;
            const bool free = slot->busy->load(std::memory_order_acquire) == 0;
            if (fits && free) {
                slot->last_used = draw_serial_;
                return slot.get();
            }
            same_size += fits ? 1 : 0;
            if (free && (oldest_free == nullptr || slot->last_used < oldest_free->last_used)) {
                oldest_free = slot.get();
            }
        }
        if (same_size >= kMaxPerSize) {
            if (shared_logs_++ < 4) {
                MH4N_LOG(Warning, kLog, "all {} shared buffers of {}x{} are held: this screen is read back", same_size,
                         width, height);
            }
            return nullptr;
        }
        if (shared_slots_.size() >= kMaxSlots && oldest_free != nullptr) {  // (a size no longer drawn, the oldest first)
            backend_->Retire(oldest_free->image, last_submit_);
            std::erase_if(shared_slots_, [&](const auto& slot) { return slot.get() == oldest_free; });
        }
        rhi::Result<std::shared_ptr<rhi::SharedImage>> made = shared_->Make(width, height);
        if (!made.ok()) {
            if (shared_logs_++ < 4) {
                MH4N_LOG(Warning, kLog, "a shared buffer of {}x{}: {}", width, height, made.error());
            }
            if (shared_slots_.empty()) {
                shared_.reset();  // (none could ever be made: read back from now on)
            }
            return nullptr;
        }
        auto slot = std::make_unique<SharedSlot>();
        slot->image = *made;
        slot->last_used = draw_serial_;
        shared_slots_.push_back(std::move(slot));
        MH4N_LOG(Info, kLog, "shared buffer {} of {}x{} made ({} in all)", shared_slots_.back()->image->Id(), width,
                 height, shared_slots_.size());
        return shared_slots_.back().get();
    }

    // ---- GpuDebug ----
    void LoadInitState(const InitState& init) override {
        FlushHeldCpuDraw();
        processor_.LoadInitState(init);
        // The GPU register block (the external registers) belongs to the GX engine; the three trigger words are written
        // with their trigger bits cleared so that loading a state starts no fill or transfer.
        const auto gpu_regs = init.Block(InitBlock::GpuRegs);
        if (gpu_regs.size() == gx::kGpuRegCount * 4 || gpu_regs.size() == pica::kExternalRegs * 4) {
            for (size_t i = 0; i < gpu_regs.size() / 4; ++i) {
                u32 word;
                std::memcpy(&word, gpu_regs.data() + 4 * i, 4);
                if (i == gx::kRegFillControl0 || i == gx::kRegFillControl1 || i == gx::kRegTransferTrigger) {
                    word &= ~1u;
                }
                gx_.WriteReg(pica::kGpuMmioBase + 4 * static_cast<u32>(i), word);
            }
        }
        const auto lcd_regs = init.Block(InitBlock::LcdRegs);
        if (lcd_regs.size() == kLcdRegs * 4) {
            for (size_t i = 0; i < kLcdRegs; ++i) {
                u32 word;
                std::memcpy(&word, lcd_regs.data() + 4 * i, 4);
                gx_.WriteReg(kVaddrLcd + 4 * static_cast<u32>(i), word);
            }
        }
    }

    std::optional<std::vector<RegisterWrite>> TakeRegisterWrites() override { return processor_.TakeRegisterWrites(); }

    void WriteBackColorBuffers() override {
        FlushHeldCpuDraw();
        if (renderer_ready_) {
            MaterializeOverlapping(0, 0xFFFFFFFFu);
        }
        WriteBackAll();
    }

    std::vector<Counter> TakeFrameCounters() override {
        if (renderer_ready_) {
            CountComposer();  // (per frame, not per draw: the composer's and the provider's counters add up until taken)
        }
        std::vector<Counter> out = PeekCounters();
        {
            u64 ranges = 0, copied = 0;
            for (const auto& t : targets_) ranges += t->stale_rows.size(), copied += t->copied.size();
            out.push_back(Counter{"stale_ranges", ranges});
            out.push_back(Counter{"copied_entries", copied});
        }
        if (GpuSpans() && renderer_ready_) {  // (the GPU's time of the screens finished since the last take)
            const u64 total = backend_->FrameGpuTimeTotalNs();
            out.push_back(Counter{"gpu_us", (total - std::min(total, gpu_total_taken_)) / 1000});
            gpu_total_taken_ = total;
        }
        LogKeyStats();
        frame_ = FrameCounters{};
        tally_passes_ = rhi::Tally().render_passes.load();
        tally_transitions_ = rhi::Tally().transitions.load();
        tally_ring_waits_ = rhi::Tally().ring_waits.load();
        tally_ring_wait_us_ = rhi::Tally().ring_wait_us.load();
        return out;
    }

    std::vector<Counter> PeekCounters() const override {
        std::vector<Counter> out;
        auto add = [&](const std::string& name, u64 value) { out.push_back(Counter{name, value}); };
        add("native_draws", frame_.draws);
        add("native_draws_dropped", frame_.dropped);
        add("cpu_path_draws", frame_.cpu_path_draws);
        add("cpu_path_unhandled", frame_.cpu_path_unhandled);
        add("cpu_path_load_failed", frame_.cpu_path_load_failed);
        add("translate_failures", frame_.translate_failures);
        add(vertex::kCounterRestartWidened, frame_.restart_widened);
        add("pipeline_sync_builds", frame_.sync_builds);
        add("pipeline_forced_uber", frame_.forced_uber);
        add("stubbed:fixup", frame_.stubbed_fixup);
        add("dst_copies", frame_.dst_copies);
        add("pingpongs", frame_.pingpongs);
        add("pingpong_lazy_px", frame_.pingpong_lazy_px);
        add("first_draw_clears", frame_.first_draw_clears);
        add("ab_phase", AbFrames() != 0 ? AbPhase() + 1 : 0u);
        add("fallback_draws", frame_.fallback_draws);
        add("stubbed:blend_plan", frame_.stubbed_blend_plan);
        add("texture_binds", frame_.texture_binds);
        add("texture_uploads", frame_.texture_uploads);
        add("uploads_ahead", frame_.uploads_ahead);
        add("submits_deferred", frame_.submits_deferred);
        add("texture_unavailable", frame_.texture_unavailable);
        add("sampler_creates", frame_.sampler_creates);
        add("texture_levels_missing", frame_.texture_levels_missing);
        add("cpu_gs", frame_.cpu_gs);
        add("cpu_immediate", frame_.cpu_immediate);
        add("cpu_refused", frame_.cpu_refused);
        add("cpu_untranslated", frame_.cpu_untranslated);
        add("cpu_vertices", frame_.cpu_vertices);
        add("cpu_us", frame_.cpu_us);
        add("draw_us", frame_.draw_us);
        add("prep_us", frame_.prep_us);
        add("tex_us", frame_.tex_us);
        add("vtx_us", frame_.vtx_us);
        add("tgt_us", frame_.tgt_us);
        add("compose_us", frame_.compose_us);
        add("pass_begins", frame_.pass_begins);
        add("pace_us", frame_.pace_us);
        add("vk_render_passes", rhi::Tally().render_passes.load() - tally_passes_);
        add("vk_transitions", rhi::Tally().transitions.load() - tally_transitions_);
        add("ring_waits", rhi::Tally().ring_waits.load() - tally_ring_waits_);
        add("ring_wait_us", rhi::Tally().ring_wait_us.load() - tally_ring_wait_us_);
        add("submits", frame_.submits);
        add("writebacks", frame_.writebacks);
        add("writeback_kb", frame_.writeback_kb);
        add("writeback_us", frame_.writeback_us);
        add("decode_us", frame_.decode_us);
        add("upload_us", frame_.upload_us);
        add("texture_deferred", frame_.texture_deferred);
        add("load_us", frame_.load_us);
        add("create_us", frame_.create_us);
        // where the write-backs come from: GX commands, another target drawn over one, a texture read, the CPU, other
        add("writebacks_gx", frame_.writeback_cause[static_cast<u32>(WritebackCause::Gx)]);
        add("writebacks_overlap", frame_.writeback_cause[static_cast<u32>(WritebackCause::Overlap)]);
        add("writebacks_texture", frame_.writeback_cause[static_cast<u32>(WritebackCause::Texture)]);
        add("writebacks_cpu", frame_.writeback_cause[static_cast<u32>(WritebackCause::Cpu)]);
        add("writebacks_other", frame_.writeback_cause[static_cast<u32>(WritebackCause::Other)]);
        add("async_transfers", frame_.async_transfers);
        add("async_materialized", frame_.async_materialized);
        add("materialize_us", frame_.materialize_us);
        add("screens_pending", frame_.screens_pending);
        add("tile_copies", frame_.tile_copies);
        add("tile_copies_reused", frame_.tile_copies_reused);
        add("tile_draws", frame_.tile_draws);
        add("fixup_draws", frame_.fixup_draws);
        add("pull_draws", frame_.pull_draws);
        add("pull_vertices", frame_.pull_vertices);
        add("touch_skipped", frame_.touch_skipped);
        add("target_pictures", frame_.target_pictures);
        add("target_write_handovers", frame_.target_write_handovers);
        add("target_vblank_handovers", frame_.target_vblank_handovers);
        add("target_pingpong_handovers", frame_.target_pingpong_handovers);
        add("target_copy_px", frame_.target_copy_px);
        add("target_owed_px", frame_.target_owed_px);
        add("target_owed_taken", frame_.target_owed_taken);
        add("target_owed_cleared", frame_.target_owed_cleared);
        add("target_fallbacks", frame_.target_fallbacks);
        add("target_no_spare", frame_.target_no_spare);
        add("target_buffers_made", frame_.target_buffers_made);
        add("target_owed_read", frame_.target_owed_read);
        add("target_owed_pretaken", frame_.target_owed_pretaken);
        add("target_owed_taken_back", frame_.target_owed_taken_back);
        add("target_box_copied", frame_.target_box_copied);
        // what the renderer's images hold, in MB (4 bytes a pixel, every mip level): targets (with their 1x upload images and
        // feedback twins), the upright screens, the shared screens' pool, the decoded textures
        {
            const auto mb = [](const rhi::Image* image) -> u64 {
                if (image == nullptr) {
                    return 0;
                }
                u64 bytes = 0;
                for (u32 level = 0; level < image->MipLevels(); ++level) {
                    bytes += u64{std::max(image->Width() >> level, 1u)} * std::max(image->Height() >> level, 1u) * 4 * image->Layers();
                }
                return bytes;
            };
            u64 targets = 0, screens = 0, shared = 0, target_buffers = 0;
            for (const auto& t : targets_) {
                targets += mb(t->image.get()) + mb(t->upload.get()) + mb(t->twin.get());
                if (t->screen != nullptr) {
                    for (const auto& b : t->screen->buffers) {
                        if (b.get() != t->screen->image && b.get() != t->screen->twin) target_buffers += mb(b->image.get());
                    }
                }
            }
            for (const ScaledScreen& s : scaled_screens_) {
                screens += mb(s.image.get());
            }
            for (const auto& slot : shared_slots_) {
                shared += slot->image != nullptr ? u64{slot->image->Width()} * slot->image->Height() * 4 : 0;
            }
            add("targets_mb", targets >> 20);
            add("screens_mb", screens >> 20);
            add("shared_screens_mb", shared >> 20);
            add("target_buffers_mb", target_buffers >> 20);  // (a screen target's images but its image and twin, gpu3)
            add("textures_mb", texture_bytes_ >> 20);
            add("texture_images", texture_images_.size());
            if (texture_cache_ != nullptr) {
                const texture::CacheStats& c = texture_cache_->stats();
                add("texture_entries", texture_cache_->size());
                add("texture_pixels_mb", texture_cache_->pixel_bytes() >> 20);
                add("texture_spot_kb", c.spot_bytes >> 10);  // (since the start)
                add("texture_spotted", c.spotted);
                add("texture_rolled", c.rolled);
                add("texture_stale", c.stale);
                add("texture_repixels", c.repixels);
                add("texture_swept", c.swept);
                add("textures_released", textures_released_);
            }
            if (backend_ != nullptr) {
                const rhi::Backend::MemoryUse memory = backend_->Memory();
                add("vma_blocks_mb", memory.block_bytes >> 20);
                add("vma_allocations_mb", memory.allocation_bytes >> 20);
                add("vma_allocations", memory.allocations);
            }
        }
        if (native_check_) {
            const cpuvertex::NativeCheckCounts checked = cpuvertex::GetNativeCheckCounts();
            add("pica_native_checked", checked.runs);
            add("pica_native_differences", checked.differences);
        }
        add("cpu_held", frame_.cpu_held);
        add("cpu_joined", frame_.cpu_joined);
        add("loads", frame_.loads);
        add("load_kb", frame_.load_kb);
        for (const char* name : {"sampling", "lighting", "fog", "proctex", "shadow", "gas"}) {
            const auto it = frame_.stubbed.find(std::string_view(name));
            add(std::string("stubbed:") + name, it == frame_.stubbed.end() ? 0 : it->second);
        }
        for (const auto& [name, value] : frame_.provider) {
            add(name, value);
        }
        return out;
    }

    // The GPU's time per screen, from two timestamps around the work of each (DrawScaledScreen), read without a wait:
    // for the pacing trace and the hooks' steady pacing (pacing::g_gpu_total_ns). Off with MH4N_GPU_TIME=1 (its own
    // marks, read with a wait) and with MH4N_GPU_SPANS=0.
    static bool GpuSpans() {
        static const bool on = [] {
            const char* t = std::getenv("MH4N_GPU_TIME");
            const char* s = std::getenv("MH4N_GPU_SPANS");
            return !(t != nullptr && *t == '1') && !(s != nullptr && *s == '0');
        }();
        return on;
    }

    std::optional<u64> TakeFrameGpuTimeNs() override {
        if (!renderer_ready_ || !frame_marked_) {
            return std::nullopt;
        }
        FlushHeldCpuDraw();
        EnsureRecording();
        rec_->MarkFrame(rhi::FrameMark::End);
        frame_marked_ = false;
        FlushRecording();
        backend_->WaitIdle();
        return backend_->FrameGpuTimeNs();
    }

    // ---- pica::Backend ----
    void Draw(const pica::DrawCall& draw) override {
        if (!EnsureRenderer()) {
            ++frame_.dropped;
            return;
        }
        const u64 start = NowUs();
        DrawImpl(draw);
        frame_.draw_us += NowUs() - start;
    }

private:
    struct FrameCounters {
        u64 draws = 0, dropped = 0;
        u64 cpu_path_draws = 0, cpu_path_unhandled = 0, cpu_path_load_failed = 0;
        u64 translate_failures = 0, restart_widened = 0;
        u64 sync_builds = 0, forced_uber = 0;
        u64 stubbed_fixup = 0, dst_copies = 0, fallback_draws = 0, stubbed_blend_plan = 0;
        u64 pingpongs = 0;  // self-sampling draws drawn into the twin (PingPongBox) instead of sampling a copy
        u64 pingpong_lazy_px = 0;  // ... the pixels their twins were not given (other targets' tiles, gpu4)
        u64 first_draw_clears = 0;  // passes begun with a clear by a draw that writes all of them (FirstDrawClears)
        u64 texture_binds = 0, texture_uploads = 0, texture_unavailable = 0, sampler_creates = 0, texture_levels_missing = 0;
        u64 uploads_ahead = 0;  // ... of them, uploaded in the recording's prelude (UploadFace: no pass broken, gpu4)
        u64 submits_deferred = 0;  // draws past kMaxDrawsPerRecording that went on in the open pass (SubmitAtPassBreak)
        // where the time goes (MH4N_GPU_COUNTERS): the CPU vertex path, submits, write-backs (a wait on the GPU) and loads
        u64 cpu_gs = 0, cpu_immediate = 0, cpu_refused = 0, cpu_untranslated = 0, cpu_vertices = 0, cpu_us = 0;
        u64 pass_begins = 0, pace_us = 0;
        u64 submits = 0, writebacks = 0, writeback_kb = 0, writeback_us = 0, loads = 0, load_kb = 0;
        u64 decode_us = 0, upload_us = 0;  // textures: their decode (and hash) by the cache, their uploads
        u64 texture_deferred = 0;          // new textures handed to AsyncDecoder (past the texture budget)
        u64 load_us = 0;                   // render targets' rows loaded from guest memory (decoded and uploaded)
        u64 create_us = 0;                 // textures' images made (the image and its memory)
        std::array<u64, 5> writeback_cause{};                       // by WritebackCause
        u64 async_transfers = 0, async_materialized = 0, materialize_us = 0, screens_pending = 0;  // --async-screens
        u64 tile_copies = 0;  // TargetAsTexture: another target's tiles copied instead of written back
        u64 tile_copies_reused = 0;  // ... and not copied again: the target still holds them
        u64 tile_draws = 0;          // ... drawn in one pass instead of copied (DrawTiles, gpu4)
        u64 fixup_draws = 0;  // draws with the lighting fix-up's geometry stage
        u64 pull_draws = 0, pull_vertices = 0;  // ... with it in the vertex stage, and their vertices
        u64 touch_skipped = 0;                  // draws of the hidden touch screen left out (NoteScreenTransfer)
        // screen targets (gpu3): pictures that were the target's own image; hand-overs at a write, at a VBlank and at a
        // ping-pong; the pixels they copied and owed; owed pixels taken back (the expected clear did not come) and owed
        // ones a clear wrote; transfers that drew the upright copy after costly hand-overs or for want of a free image;
        // images made
        u64 target_pictures = 0, target_write_handovers = 0, target_vblank_handovers = 0, target_pingpong_handovers = 0;
        u64 target_copy_px = 0, target_owed_px = 0, target_owed_taken = 0, target_owed_cleared = 0;
        u64 target_fallbacks = 0, target_no_spare = 0, target_buffers_made = 0;
        u64 target_owed_read = 0;  // the hidden touch screen's snapshot of the published picture (what is owed)
        u64 target_owed_pretaken = 0;  // ... taken at its hand-over (PretakeSnapshot) and adopted
        u64 target_owed_taken_back = 0;  // owed pixels or a snapshot taken from the app's side (OwingAllowed's GPUs)
        u64 target_box_copied = 0;  // hand-overs that copied the box they would have owed (no owing: OwingAllowed)
        u64 cpu_held = 0, cpu_joined = 0;        // CPU-path draws held back, and draws that joined them
        u64 draw_us = 0, prep_us = 0, tex_us = 0, vtx_us = 0, tgt_us = 0, compose_us = 0;
        std::map<std::string, u64, std::less<>> stubbed;
        std::map<std::string, u64, std::less<>> provider;  // the pipeline provider's other counters (N2-16)
    };
    // (0 unless MH4N_GPU_COUNTERS is set: the draw path reads the clock about ten times a draw for its counters)
    static u64 NowUs() {
        static const bool counting = [] {
            const char* v = std::getenv("MH4N_GPU_COUNTERS");
            return v != nullptr && *v != '0';
        }();
        if (!counting) return 0;
        return static_cast<u64>(std::chrono::duration_cast<std::chrono::microseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    // ---- the renderer, made on first use ----
    bool EnsureRenderer() {
        if (renderer_ready_) {
            return true;
        }
        if (renderer_failed_) {
            return false;
        }
        renderer_failed_ = true;
        rhi::Result<std::shared_ptr<rhi::Instance>> instance = rhi::CreateInstance();
        if (!instance.ok()) {
            MH4N_LOG(Error, kLog, "no Vulkan instance: {}", instance.error());
            return false;
        }
        instance_ = *instance;
        rhi::Result<std::shared_ptr<rhi::Backend>> backend = rhi::CreateBackend(instance_);
        if (!backend.ok()) {
            MH4N_LOG(Error, kLog, "no Vulkan device: {}", backend.error());
            return false;
        }
        backend_ = *backend;
        const rhi::Capabilities& caps = backend_->Caps();
        MH4N_LOG(Info, kLog, "device: {} ({})", caps.device_name, caps.driver_name);
        owing_allowed_ = OwingFor(caps);
        MH4N_LOG(Info, kLog, "the top screen's hand-over {}", owing_allowed_ ? "may owe the box the next frame clears"
                                                                              : "copies the box the next frame clears (owes nothing)");
        if (!TargetScreensOn()) {
            MH4N_LOG(Info, kLog, "the top screen is shown from the upright copy (a GPU that owes nothing; MH4N_TARGET_SCREENS=1: "
                                 "the target's own pictures)");
        }
        profile_ = fragment::ProfileOf(caps);

        // The quaternion fix-up of lit draws (lighting/, N2-09; design 5.5): --lighting-fixup (native_gpu.h), the environment's
        // MH4N_LIGHTING_FIXUP first. The fixed geometry stage is registered on a device with geometry shaders unless the
        // fix-up is off (in vs, the draws the vertex stage cannot take use it); with none, the draws that would have it are
        // counted stubbed:fixup.
        {
            const char* env = std::getenv("MH4N_LIGHTING_FIXUP");
            const std::string mode = env != nullptr ? std::string(env) : options_.lighting_fixup;
            constexpr u32 kVendorArm = 0x13B5;
            if (mode == "0" || mode == "off") {
                fixup_mode_ = FixupMode::Off;
            } else if (mode == "gs" && caps.geometry_shader) {
                fixup_mode_ = FixupMode::Gs;
            } else if (mode == "vs" || mode == "gs") {
                fixup_mode_ = FixupMode::Vs;
            } else {
                if (mode != "auto" && mode != "1") {
                    MH4N_LOG(Error, kLog, "--lighting-fixup {}: gs, vs, off or auto; auto is used", mode);
                }
                // (the vertex stage on every GPU now: on an Adreno 830 at 3x the geometry stage cost 8 % more of the
                // GPU's cycles a frame in the village and 10 % more in the Arena (2.35 against 2.13 million), and the
                // Galaxy's GPU is the limit at its thermal cap)
                fixup_mode_ = FixupMode::Vs;
            }
            // (MH4N_PULL_PREFILL=0|1: the vertex stage's indices filled on the CPU first, or not, on any device)
            const char* prefill = std::getenv("MH4N_PULL_PREFILL");
            pull_prefill_indices_ = prefill != nullptr ? *prefill == '1' : caps.vendor_id == kVendorArm;
            const char* touch_skip = std::getenv("MH4N_TOUCH_SKIP");
            // (on every GPU: on an Adreno 830 at 3x the hidden touch screen's draws were 18 % of the GPU's cycles a frame)
            touch_skip_enabled_.store(touch_skip != nullptr ? *touch_skip == '1' : true, std::memory_order_relaxed);
            MH4N_LOG(Info, kLog, "lighting fix-up: {}", fixup_mode_ == FixupMode::Off  ? "off"
                                                        : fixup_mode_ == FixupMode::Gs ? "geometry stage"
                                                                                        : "vertex stage");
        }
        if (caps.geometry_shader && !FixupGeneratorRegistry::Global().Has() && fixup_mode_ != FixupMode::Off) {
            const bool clip_distance = caps.clip_distance;
            FixupGeneratorRegistry::Global().Set([clip_distance](const VertexStageOutputs& outputs) {
                fragment::lighting::FixupStageInputs inputs;
                inputs.num_outputs = outputs.num_outputs;
                inputs.clip_distance = clip_distance;
                for (size_t s = 0; s < outputs.semantic_map.size() && s < inputs.semantic_map.size(); ++s) {
                    inputs.semantic_map[s] = {outputs.semantic_map[s].attribute, outputs.semantic_map[s].component};
                }
                std::string error;
                std::vector<u32> spirv = fragment::lighting::GenerateFixupGeometrySpirv(inputs, &error);
                if (spirv.empty()) {
                    MH4N_LOG(Error, kLog, "the fix-up geometry stage does not compile: {}", error);
                }
                return spirv;
            });
        }

        builder_ = std::make_unique<PipelineBuilder>(backend_, *registry_);
        provider_ = MakePipelineProvider(backend_, *builder_, options_.options, options_.force_ubershader);
        // A check that the driver takes what the renderer builds (an old driver may refuse it all, and the game then runs
        // on behind a black screen: 4u-native #33), on a thread of its own: the colour readback's compute pipeline (made
        // now and kept for ReadbackToGuest) and a graphics pipeline made as the scaled screens' is, through the path every
        // pipeline takes (glslang at the SPIR-V chosen, plume), then let go. A refusal names its VkResult. Not on the
        // renderer's thread: glslang's first compile of a vertex and a fragment stage (its built-in tables) took ~200 ms
        // there on the Anbernic, a frame of the start 230 ms longer. (After the provider, which loads the driver's
        // pipeline cache first.)
        check_thread_ = std::thread([backend = backend_] {
            SetHostThreadName("mh4n-check");
            const auto start = std::chrono::steady_clock::now();
            const rhi::Result<void> readback = backend->PrepareReadback();
            std::string graphics;  // (empty: built)
            {
                std::vector<u32> vs, fs;
                std::string error;
                if (!shader::CompileGlslToSpirv(kScreenVertexGlsl, vs, error) ||
                    !fragment::CompileFragmentGlslToSpirv(kScreenFragmentGlsl, fs, error)) {
                    graphics = "its shaders do not compile: " + error;
                } else {
                    rhi::GraphicsPipelineDesc desc;
                    desc.vertex.spirv = vs;
                    desc.fragment.spirv = fs;
                    desc.topology = rhi::Topology::TriangleList;
                    desc.color_format = backend->ChooseFormat(rhi::Format::Rgba8Unorm, rhi::FormatUse::ColorTarget).format;
                    desc.sets = {ScreenSetDesc()};
                    desc.name = "pipeline check";
                    rhi::Result<std::shared_ptr<rhi::Pipeline>> pipeline = backend->CreateGraphicsPipeline(desc);
                    if (!pipeline.ok()) graphics = pipeline.error();
                }
            }
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            present::SetRendererCannotDraw(!readback.ok() || !graphics.empty());
            if (readback.ok() && graphics.empty()) {
                MH4N_LOG(Info, kLog, "pipeline check: the driver built a compute and a graphics pipeline ({:.0f} ms)", ms);
            } else {
                MH4N_LOG(Error, kLog, "pipeline check: the driver refused {}{}{}: nothing drawn may show",
                         !readback.ok() ? "the readback's compute pipeline (" + readback.error() + ")" : std::string(),
                         !readback.ok() && !graphics.empty() ? " and " : "",
                         !graphics.empty() ? "a graphics pipeline (" + graphics + ")" : std::string());
            }
        });
        rhi::Result<std::unique_ptr<DrawComposer>> composer = DrawComposer::Create(backend_, *provider_);
        if (!composer.ok()) {
            MH4N_LOG(Error, kLog, "composer: {}", composer.error());
            return false;
        }
        composer_ = std::move(*composer);
        rhi::Result<std::unique_ptr<fragment::FragmentUniformRing>> uniforms = fragment::FragmentUniformRing::Create(*backend_, kFragmentUniformRingBytes);
        rhi::Result<std::unique_ptr<fragment::TableRings>> tables = fragment::TableRings::Create(*backend_);
        rhi::Result<std::unique_ptr<rhi::Ring>> vertex_ring =
            backend_->CreateRing(kVertexRingBytes, rhi::BufferUsage::Vertex | rhi::BufferUsage::Index | rhi::BufferUsage::Storage,
                                 "native vertices");
        rhi::Result<std::unique_ptr<rhi::Ring>> vs_ring =
            backend_->CreateRing(kVsUniformRingBytes, rhi::BufferUsage::Uniform | rhi::BufferUsage::Storage,
                                 "native vertex-stage uniforms");
        rhi::Result<std::unique_ptr<rhi::Ring>> staging =
            backend_->CreateRing(kStagingRingBytes, rhi::BufferUsage::Transfer, "native staging");
        if (!uniforms.ok() || !tables.ok() || !vertex_ring.ok() || !vs_ring.ok() || !staging.ok()) {
            MH4N_LOG(Error, kLog, "rings: {} {} {} {} {}", uniforms.error(), tables.error(), vertex_ring.error(), vs_ring.error(),
                     staging.error());
            return false;
        }
        uniform_ring_ = std::move(*uniforms);
        tables_ = std::move(*tables);
        vertex_ring_ = std::move(*vertex_ring);
        vs_ring_ = std::move(*vs_ring);
        staging_ring_ = std::move(*staging);
        fragment_state_ = std::make_unique<fragment::FragmentState>(profile_, *registry_);
        // MH4N_SHADERS_BEFORE=1 (a measurement aid): the fragment stages as before gpu5 for the whole run (what
        // MH4N_AB_FEATURE=shaders's off phase draws), for one run's watts against another's
        if (const char* v = std::getenv("MH4N_SHADERS_BEFORE"); v != nullptr && *v == '1') {
            fragment::SetShadersAsBefore(true);
        }
        // Precise invalidation: a flush, an invalidate or a GPU write marks only the textures in its range (the game
        // flushes what it writes, as the 3DS's GPU needs). MH4N_TEXTURE_CHECK_ALL=1: every texture on any of them.
        texture::TextureCache::Config cache_config;
        cache_config.check_all_on_notify = [] {
            const char* v = std::getenv("MH4N_TEXTURE_CHECK_ALL");
            return v != nullptr && *v == '1';
        }();
        cache_config.verify = [] {
            const char* v = std::getenv("MH4N_TEXTURE_VERIFY");
            return v != nullptr && *v == '1';
        }();
        cache_config.spot_check = [] {
            const char* v = std::getenv("MH4N_TEXTURE_SPOT");
            return v == nullptr || *v != '0';
        }();
        cache_config.rolling_check = [] {
            const char* v = std::getenv("MH4N_TEXTURE_ROLL");
            return v == nullptr || *v != '0';
        }();
        cache_config.release_pixels = [] {
            const char* v = std::getenv("MH4N_TEXTURE_KEEP_PIXELS");
            return v == nullptr || *v == '0';
        }();
        MH4N_LOG(Info, kLog, "textures: images up to {} MB (the device's memory {} MB), {}{}", TextureBudgetBytes() >> 20,
                 DeviceMemoryMb(), cache_config.release_pixels ? "decoded copies let go once uploaded" : "decoded copies kept",
                 !cache_config.spot_check ? ", no spot check" : cache_config.rolling_check ? "" : ", no rolling check");
        texture_cache_ = std::make_unique<texture::TextureCache>(host_, cache_config);
        samplers_ = std::make_unique<fragment::sampling::SamplerCache>(backend_);

        vertex::VertexCaps vertex_caps;
        vertex_caps.triangle_fans = caps.triangle_fans;
        vertex_caps.index_uint8 = caps.index_uint8;
        vertex_caps.apple = caps.apple;
        vertex_caps.stride_alignment = caps.stride_alignment;
        vertex_caps.primitive_restart_always_on = caps.primitive_restart_always_on;
        loader_ = std::make_unique<vertex::VertexLoader>(vertex_caps);

        renderer_failed_ = false;
        // The PICA programs the code library carries compiled (tools/pica_native.py): the CPU path runs them instead of
        // interpreting. MH4N_PICA_NATIVE=off: the interpreter only; =check: both, compared run by run (counted).
        const char* native_mode = std::getenv("MH4N_PICA_NATIVE");
        const std::string pica_native = native_mode != nullptr ? native_mode : "on";
        if (pica_native != "off") {
            if (const auto table = reinterpret_cast<PicaNativeTableFunction>(host_.CodeSymbol(PICA_NATIVE_TABLE_SYMBOL))) {
                unsigned count = 0, version = 0;
                const PicaNativeEntry* entries = table(&count, &version);
                if (version == PICA_NATIVE_VERSION && entries != nullptr) {
                    native_check_ = pica_native == "check";
                    runner_.SetNativePrograms(entries, count, native_check_);
                    MH4N_LOG(Info, kLog, "the code library has {} PICA programs compiled{}", count,
                             native_check_ ? " (each run checked against the interpreter)" : "");
                } else {
                    MH4N_LOG(Warning, kLog, "the code library's compiled PICA programs are version {}, not {}: interpreted",
                             version, PICA_NATIVE_VERSION);
                }
            }
        }
        renderer_ready_ = true;
        return true;
    }

    void EnsureRecording() {
        if (rec_) {
            return;
        }
        rec_.emplace(backend_->BeginRecording());
        if (!frame_marked_) {
            rec_->MarkFrame(rhi::FrameMark::Begin);
            frame_marked_ = true;
        }
        composer_->BeginRecording(*rec_);
        uniform_ring_->BeginRecording();
        ++recording_serial_;
        if (trace_.active) {
            TraceEvent("recording");  // (what each recording does first: the dummies' clears, until they are cleared once)
        }
        TrimTextures();
        draws_in_recording_ = 0;
        vertex_bytes_in_recording_ = 0;
        dynamic_applied_ = false;
        pull_jobs_.clear();
    }

    void FlushRecording() {
        if (!rec_) {
            return;
        }
        FlushHeldCpuDraw();
        TraceEvent("submit");
        composer_->EndPass(*rec_);
        RecordFixupPasses();
        RecordScreenReleases();
        ++frame_.submits;
        last_submit_ = backend_->Submit(std::move(*rec_));
        rec_.reset();
        composer_->Submitted(last_submit_ > 0);
        for (std::shared_ptr<rhi::Image>& image : retire_after_submit_) backend_->Retire(std::move(image), last_submit_);
        retire_after_submit_.clear();
        if (!releasing_.empty()) {
            std::shared_ptr<rhi::SyncFile> sync = shared_->CanExportFences() ? shared_->ExportFence() : nullptr;
            for (TargetBuffer* b : releasing_) {
                b->released_at->store(last_submit_, std::memory_order_release);  // (its pictures may be read after this one)
                b->sync = sync;
            }
            releasing_.clear();
        }
        // (images of a target that is no screen target any more, once no picture holds them)
        std::erase_if(orphan_buffers_, [&](const std::unique_ptr<TargetBuffer>& b) {
            if (b->release_owed || b->holders->load(std::memory_order_acquire) > 0) return false;
            backend_->Retire(b->image, last_submit_);
            return true;
        });
    }

    // Submits the recording when the rings that hold its data would run out, or when it has kMaxDrawsPerRecordingHard
    // draws.
    void EnsureRoom(u64 vertex_bytes) {
        if (rec_ && (draws_in_recording_ >= kMaxDrawsPerRecordingHard ||
                     vertex_bytes_in_recording_ + 2 * vertex_bytes > kVertexRingBytes / 2)) {
            FlushRecording();
        }
        EnsureRecording();
    }

    // At a draw's start: the recording is submitted once it has kMaxDrawsPerRecording draws, but not while the draw most
    // likely goes on in the open pass (the same colour and depth buffers as the last draw recorded, and that pass still
    // open): a submission ends the pass, and on a tiler the pass after it loads what the first half stored (the village's
    // HUD pass at 3x on a wide screen, 4.7 MB stored and loaded again; a scene of more than 256 draws, its depth too,
    // gpu4). It waits for the first draw that breaks the pass anyway, up to kMaxDrawsPerRecordingHard (EnsureRoom).
    // MH4N_SUBMIT_AT_PASS=0: at the 256th draw, as before.
    void SubmitAtPassBreak(const pica::PicaState& state) {
        static const bool on = [] {
            const char* v = std::getenv("MH4N_SUBMIT_AT_PASS");
            return v == nullptr || *v != '0';
        }();
        if (!rec_ || draws_in_recording_ < kMaxDrawsPerRecording) {
            return;
        }
        if (on && !AbOffFor("submit") && last_draw_target_.valid) {
            const u32 color_address = pica::BitsOf(state.regs[kRegColorAddress], 0, 28) * 8;
            const u32 depth_address = pica::BitsOf(state.regs[kRegDepthAddress], 0, 28) * 8;
            if (color_address == last_draw_target_.color_address && depth_address == last_draw_target_.depth_address &&
                composer_->OpenArea(last_draw_target_.color, last_draw_target_.depth).has_value()) {
                ++frame_.submits_deferred;
                return;
            }
        }
        FlushRecording();
    }

    // ---- targets ----
    Target* FindTarget(bool depth, u32 paddr, u32 format, u32 width, u32 height) {
        for (auto& t : targets_) {
            if (t->depth == depth && t->paddr == paddr && t->format == format && t->width == width && t->height == height) {
                return t.get();
            }
        }
        return nullptr;
    }

    // `shadow`: the target must be a shadow source (N2-12). A shadow source also serves the ordinary draws into it (the game clears
    // its shadow map with one), so an ordinary target that the shadow-write pass then draws into becomes one: its image replaced by
    // a shadow source at 1x, which takes the old image's picture by a nearest blit on the GPU (the sample a write-back takes: the
    // middle of each scale x scale block), its stale rows still to load from memory and its drawn rows still to write back. It was
    // written back and loaded again from memory: a wait for the GPU, 18 ms of the Anbernic's renderer at 3x (6.6 at 1x) in the
    // Arena's load frame, where the game's new shadow map appears (MH4N_SHADOW_REMAKE=memory: as before).
    Target* GetTarget(bool depth, u32 paddr, u32 format, u32 width, u32 height, bool shadow = false) {
        Target* found = FindTarget(depth, paddr, format, width, height);
        if (found != nullptr && (!shadow || found->shadow)) {
            return found;
        }
        rhi::ImageDesc desc;
        desc.format = depth ? DepthImageFormat(format) : rhi::Format::Rgba8Unorm;
        // a shadow map stays 1x: its lookups and its write pass address the 3DS's own texels
        const u32 scale = shadow ? 1 : scale_;
        desc.width = width * scale;
        desc.height = height * scale;
        desc.shadow_source = shadow;
        desc.name = depth ? "pica depth buffer" : (shadow ? "pica shadow map" : "pica colour buffer");
        rhi::Result<std::shared_ptr<rhi::Image>> image = backend_->CreateImage(desc);
        if (!image.ok()) {
            MH4N_LOG(Error, kLog, "render target {}x{}: {}", desc.width, desc.height, image.error());
            return nullptr;
        }
        if (found != nullptr) {
            static const bool through_memory = [] {
                const char* v = std::getenv("MH4N_SHADOW_REMAKE");
                return v != nullptr && std::string_view(v) == "memory";
            }();
            if (found->screen != nullptr) {
                DemoteScreenTarget(*found);  // (its images become ordinary ones: the pictures keep theirs)
            }
            const bool blit = !through_memory && !found->depth && !found->stale && renderer_ready_;
            if (blit) {
                EnsureRecording();
                composer_->EndPass(*rec_);
                TraceEventF("shadow source {:08x} {}x{} from its colour image", found->paddr, width, height);
                rhi::ImageBlitRegion region;
                region.src = rhi::Rect{0, 0, width * found->scale, height * found->scale};
                region.dst = rhi::Rect{0, 0, width, height};
                rec_->Blit(*found->image, **image, std::span<const rhi::ImageBlitRegion>(&region, 1), /*linear=*/false);
            } else {
                WriteBack(*found);  // (cause: other, the scope of the caller)
            }
            FlushRecording();  // the old image may be in the open recording (and the blit from it is)
            backend_->Retire(found->image, last_submit_);
            found->image = *image;
            found->twin.reset();
            found->Changed(0, found->height);
            found->scale = scale;
            found->area = {};  // (in the old image's pixels: at a scale the new one is smaller, and a pass over the old
                               // box would be outside it)
            found->cleared_once = false;
            found->clear_pending = false;
            found->pass_boxes.clear();
            found->pass_boxes_before.clear();
            found->pass_boxes_before2.clear();
            found->shadow = true;
            if (!blit) {
                found->stale = true;
                found->stale_rows.clear();
                const StaleWhy why(*this, "made a shadow map");
                NoteStale(*found);
            }  // (blit: its stale rows stay stale, its drawn rows dirty, as in the old image)
            if (found->upload != nullptr) {
                backend_->Retire(found->upload, last_submit_);
                found->upload.reset();
            }
            return found;
        }
        auto target = std::make_unique<Target>();
        target->image = *image;
        target->scale = scale;
        target->depth = depth;
        target->paddr = paddr;
        target->format = format;
        target->width = width;
        target->height = height;
        target->shadow = shadow;
        target->bytes = width * height * (depth ? DepthBytesPerPixel(format) : ColorBytesPerPixel(format));
        targets_.push_back(std::move(target));
        return targets_.back().get();
    }

    u8* GuestMemory(const Target& t) { return host_.PhysicalPointer(t.paddr, t.bytes); }

    // Loads a target from guest memory (colour) or starts it at 1.0 (depth). Outside a pass. A scaled target is loaded
    // into its 1x upload image and enlarged from it (nearest).
    void LoadTarget(Target& t) {
        t.stale = false;
        t.stale_rows.clear();
        t.Changed(0, t.height);
        if (t.depth && t.cleared_once) {
            // owed to the next pass with it (DrawImpl: its render area, cleared inside the pass) or to whatever reads it
            // first (ResolveDepthClear): no clear of the whole image, and no pass break, here
            t.clear_pending = true;
            return;
        }
        EnsureRecording();
        composer_->EndPass(*rec_);
        if (t.depth) {
            ClearDepthWhole(t);
            return;
        }
        if (!LoadRows(t, 0, t.height)) {
            const rhi::Rect whole = WholeImage(t);
            ScreenWrite(t, whole, &whole, false, "a clear of the whole image");
            rhi::ClearValue clear;
            clear.color = {0.0f, 0.0f, 0.0f, 0.0f};
            rec_->ClearImage(*t.image, clear);
        }
    }

    void ClearDepthWhole(Target& t) {
        TraceEventF("clear depth {:08x} {}x{}", t.paddr, t.width, t.height);
        rhi::ClearValue clear;
        clear.depth = 1.0f;
        clear.stencil = 0;
        rec_->ClearImage(*t.image, clear);
        t.cleared_once = true;
        t.clear_pending = false;
    }

    // The clear a depth target owes, done now on the whole image, before something other than a pass reads it.
    void ResolveDepthClear(Target& t) {
        if (!t.clear_pending) {
            return;
        }
        EnsureRecording();
        composer_->EndPass(*rec_);
        ClearDepthWhole(t);
    }

    // Guest memory under [begin, end) may be newer than the image of `t` (the CPU or the GX engine wrote it, or another
    // target there was drawn and written back). A depth target is loaded again whole (cleared) before its next use; a
    // colour target only in the rows of tiles that range touches, so the rest keeps what it holds (its scaled picture, or
    // the rows another target that shares its memory does not touch).
    void MarkStale(Target& t, u32 begin, u32 end) {
        const u32 tile_row = t.width * 8 * ColorBytesPerPixel(t.format);
        if (t.depth || tile_row == 0) {
            t.stale = true;
            NoteStale(t);
            return;
        }
        const u32 lo = std::max(begin, t.paddr);
        const u32 hi = std::min<u64>(end, u64{t.paddr} + t.bytes);
        if (lo >= hi || t.stale) {
            return;
        }
        NoteStale(t);
        const u32 first = (lo - t.paddr) / tile_row * 8;
        const u32 last = std::min(t.height, (hi - t.paddr + tile_row - 1) / tile_row * 8);
        if (first == 0 && last >= t.height) {
            t.stale = true;
            t.stale_rows.clear();
            return;
        }
        // (kept in order and merged with the ranges it touches: a target whose rows another one marked stale every frame,
        // in rows its own draws never refresh, kept one more range a frame for ever, and every look at them took longer)
        auto it = std::lower_bound(t.stale_rows.begin(), t.stale_rows.end(), std::pair<u32, u32>{first, last});
        it = t.stale_rows.insert(it, {first, last});
        if (it != t.stale_rows.begin() && std::prev(it)->second >= it->first) {
            --it;
            it->second = std::max(it->second, std::next(it)->second);
            t.stale_rows.erase(std::next(it));
        }
        while (std::next(it) != t.stale_rows.end() && std::next(it)->first <= it->second) {
            it->second = std::max(it->second, std::next(it)->second);
            t.stale_rows.erase(std::next(it));
        }
    }

    // Brings the image of `t` up to guest memory where it is stale, in guest rows [from, to) (whole rows of tiles; all
    // of them by default): the stale rows outside stay stale. Outside a pass.
    void Refresh(Target& t, u32 from = 0, u32 to = 0xFFFFFFFFu) {
        if (t.stale) {
            LoadTarget(t);
            return;
        }
        if (t.stale_rows.empty()) {
            return;
        }
        std::vector<std::pair<u32, u32>> rows, keep;
        for (const auto& [a, b] : t.stale_rows) {
            if (b <= from || a >= to) {
                keep.emplace_back(a, b);
                continue;
            }
            if (a < from) keep.emplace_back(a, from);
            if (b > to) keep.emplace_back(to, b);
            rows.emplace_back(std::max(a, from), std::min(b, to));
        }
        t.stale_rows = std::move(keep);
        if (rows.empty()) {
            return;
        }
        std::sort(rows.begin(), rows.end());
        EnsureRecording();
        composer_->EndPass(*rec_);
        for (size_t i = 0; i < rows.size();) {
            u32 first = rows[i].first, last = rows[i].second;
            for (++i; i < rows.size() && rows[i].first <= last; ++i) {
                last = std::max(last, rows[i].second);
            }
            if (!LoadRows(t, first, last)) {
                LoadTarget(t);
                return;
            }
        }
    }

    // Loads guest rows [first, last) (whole rows of tiles) of a colour target from guest memory. False when the memory
    // cannot be read or decoded.
    bool LoadRows(Target& t, u32 first, u32 last) {
        struct Timed {  // (load_us: the whole load, its decode and upload)
            u64& total;
            u64 start = NowUs();
            ~Timed() { total += NowUs() - start; }
        } timed{frame_.load_us};
        const u32 tile_row = t.width * 8 * ColorBytesPerPixel(t.format);
        const u32 rows = last - first;
        // a deferred framebuffer write that these rows overlap is made first (async screens)
        const u32 begin = t.paddr + first / 8 * tile_row;
        MaterializeOverlapping(begin, static_cast<u32>(std::min<u64>(u64{begin} + rows / 8 * tile_row, 0xFFFFFFFFull)));
        std::vector<u8> rgba;
        const u8* memory = host_.PhysicalPointer(t.paddr + first / 8 * tile_row, rows / 8 * tile_row);
        const bool decoded = memory != nullptr && t.width % 8 == 0 && first % 8 == 0 && rows % 8 == 0 && rows != 0 &&
                             texture::DecodeToRgba8(memory, rows / 8 * tile_row, t.width, rows,
                                                    static_cast<texture::Format>(t.format), rgba);
        if (!decoded) {
            return false;
        }
        if (t.scale > 1 && ScaleLogLimit() != 0) {
            const std::string event = fmt::format("load: target {} rows {}-{} loaded from memory at 1x ({}", Describe(t), first,
                                                  last, t.stale_why);
            ScaleLog(event + fmt::format("; stale since f{})", t.stale_frame), event.size());
        }
        t.Changed(first, last);
        ++frame_.loads;
        frame_.load_kb += rgba.size() / 1024;
        rhi::Result<rhi::RingAllocation> alloc = staging_ring_->Map(rgba.size(), 16);
        if (!alloc.ok()) {
            FlushRecording();
            EnsureRecording();
            alloc = staging_ring_->Map(rgba.size(), 16);
        }
        if (!alloc.ok()) {
            MH4N_LOG(Error, kLog, "staging for a render target: {}", alloc.error());
            return true;  // (as before: the image is left as it is)
        }
        // The decoder gives the memory's row 0 first; a target's image holds guest row 0 as its last row, the order
        // ReadbackToGuest writes back in and UploadRgba8 uploads textures in (sampling.h, "WHICH ROW IS ROW 0"). Copied
        // in the memory's order, a target written back and loaded again came back upside down. Guest rows [first, last)
        // are image rows [height - last, height - first).
        const std::size_t row_bytes = static_cast<std::size_t>(t.width) * 4;
        for (u32 y = 0; y < rows; ++y) {
            std::memcpy(alloc->data.data() + y * row_bytes, rgba.data() + (rows - 1 - y) * row_bytes, row_bytes);
        }
        staging_ring_->GetBuffer()->Flush(alloc->offset, rgba.size());
        if (!staging_ring_->Commit(rgba.size()).ok()) {
            return true;
        }
        {
            // (whole rows of the image are written: a screen target's picture keeps its image, gpu3)
            const rhi::Rect written = RowsRect(t, first, last);
            ScreenWrite(t, written, &written, false, "rows loaded from memory");
        }
        rhi::Image* upload = t.image.get();
        if (t.scale > 1) {
            if (t.upload == nullptr) {
                rhi::ImageDesc desc;
                desc.format = rhi::Format::Rgba8Unorm;
                desc.width = t.width;
                desc.height = t.height;
                desc.name = "pica colour buffer (1x load)";
                rhi::Result<std::shared_ptr<rhi::Image>> made = backend_->CreateImage(desc);
                if (!made.ok()) {
                    MH4N_LOG(Error, kLog, "the 1x image of a render target: {}", made.error());
                    return true;
                }
                t.upload = *made;
            }
            upload = t.upload.get();
        }
        TraceEventF("load {:08x} {}x{} rows {}-{}", t.paddr, t.width, t.height, first, last);
        const s32 top = static_cast<s32>(t.height - last);
        rhi::BufferImageRegion region;
        region.buffer_offset = alloc->offset;
        region.image_rect = rhi::Rect{0, top, t.width, rows};
        region.aspect = rhi::ImageAspect::Color;
        rec_->CopyBufferToImage(*staging_ring_->GetBuffer(), *upload, std::span<const rhi::BufferImageRegion>(&region, 1));
        if (t.scale > 1) {
            const s32 n = static_cast<s32>(t.scale);
            rhi::ImageBlitRegion blit;
            blit.src = rhi::Rect{0, top, t.width, rows};
            blit.dst = rhi::Rect{0, top * n, t.width * t.scale, rows * t.scale};
            rec_->Blit(*t.upload, *t.image, std::span<const rhi::ImageBlitRegion>(&blit, 1), /*linear=*/false);
        }
        return true;
    }

    // Writes what the draws since the last write-back touched (their rows of tiles) into guest memory: the other rows
    // of the image may be older than memory (rows another target or the GX engine wrote, MarkStale), and are not
    // written over it.
    void WriteBack(Target& t) {
        if (!t.gpu_dirty) {
            return;
        }
        ResolveDepthClear(t);
        t.gpu_dirty = false;
        u32 first = t.dirty_first, last = t.dirty_last;
        t.dirty_first = t.dirty_last = 0;
        const u32 tile_row = TileRowBytes(t);
        if (first >= last || tile_row == 0) {
            return;  // the draws touched no pixel (an empty draw rectangle)
        }
        if (t.height % 8 != 0) {
            first = 0, last = t.height;  // (rows of tiles cannot be cut out of it: the whole target, as before)
        }
        TraceEventF("writeback {} {:08x} {}x{} rows {}-{}", t.depth ? "depth" : "colour", t.paddr, t.width,
                               t.height, first, last);
        const u64 start = NowUs();
        const auto slow_start = std::chrono::steady_clock::now();
        if (t.screen != nullptr) {
            ScreenRead(t, RowsRect(t, first, last), "a write-back");
        }
        FlushRecording();
        const u32 offset = first / 8 * tile_row;
        const u32 bytes = first == 0 && last == t.height ? t.bytes : (last - first) / 8 * tile_row;
        u8* memory = host_.PhysicalPointer(t.paddr + offset, bytes);
        if (memory == nullptr) {
            return;
        }
        ++frame_.writebacks;
        ++frame_.writeback_cause[static_cast<u32>(writeback_cause_)];
        frame_.writeback_kb += bytes / 1024;
        const rhi::PicaFormat format = t.depth ? ToPicaDepthFormat(t.format) : ToPicaFormat(t.format);
        // guest rows [first, last) are the image's rows [height - last, height - first) (at 1x; times the scale)
        rhi::Result<void> r = backend_->ReadbackToGuest(
            *t.image, rhi::Rect{0, static_cast<s32>(t.height - last), t.width, last - first}, format,
            std::span<u8>(memory, bytes), t.scale);
        if (!r.ok()) {
            MH4N_LOG(Error, kLog, "write-back of a render target: {}", r.error());
        }
        frame_.writeback_us += NowUs() - start;
        texture_cache_->OnGpuWrite(t.paddr + offset, bytes);  // a texture drawn into is out of date in the cache
        const u64 took_us = static_cast<u64>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - slow_start).count());
        if (took_us >= 2000 && slow_writebacks_told_ < 40) {  // (which write-backs stall: the first 40)
            ++slow_writebacks_told_;
            constexpr const char* kCauses[] = {"other", "a transfer", "another target over it", "a texture", "the CPU"};
            MH4N_LOG(Info, kLog, "write-back {} {:08x} {}x{} fmt {} rows {}-{} ({} KB, for {}): {} us", t.depth ? "depth" : "colour",
                     t.paddr, t.width, t.height, t.format, first, last, bytes / 1024,
                     kCauses[std::min<u32>(static_cast<u32>(writeback_cause_), 4)], took_us);
        }
    }

    // A row of 8x8 tiles of `t`, in bytes.
    static u32 TileRowBytes(const Target& t) {
        return t.width * 8 * (t.depth ? DepthBytesPerPixel(t.format) : ColorBytesPerPixel(t.format));
    }

    // Guest memory the draws since the last write-back of `t` wrote over [begin, end): what a reader of that range has
    // to wait for.
    bool DirtyOverlaps(const Target& t, u32 begin, u32 end) const {
        if (!t.gpu_dirty) {
            return false;
        }
        const u32 tile_row = TileRowBytes(t);
        const u64 lo = u64{t.paddr} + u64{t.dirty_first / 8} * tile_row;
        const u64 hi = t.dirty_last >= t.height ? u64{t.paddr} + t.bytes : u64{t.paddr} + u64{(t.dirty_last + 7) / 8} * tile_row;
        return t.dirty_first < t.dirty_last && lo < end && begin < hi;
    }

    // A draw into `t` covers `scissor` (image pixels at `scale`; image row r is guest row height - 1 - r): those guest
    // rows, in whole rows of tiles, join its dirty rows.
    static void MarkDirty(Target& t, const rhi::Rect& scissor, u32 scale) {
        if (scissor.width == 0 || scissor.height == 0 || scale == 0) {
            return;
        }
        const u32 top = static_cast<u32>(std::max(scissor.y, 0)) / scale;
        const u32 bottom = (static_cast<u32>(std::max(scissor.y, 0)) + scissor.height + scale - 1) / scale;
        const u32 first = t.height > bottom ? (t.height - bottom) / 8 * 8 : 0;
        const u32 last = std::min(t.height, (t.height > top ? t.height - top + 7 : 0) / 8 * 8);
        if (first >= last) {
            return;
        }
        if (t.dirty_first >= t.dirty_last) {
            t.dirty_first = first, t.dirty_last = last;
        } else {
            t.dirty_first = std::min(t.dirty_first, first), t.dirty_last = std::max(t.dirty_last, last);
        }
        t.Changed(first, last, false, static_cast<u32>(std::max(scissor.x, 0)) + scissor.width);
    }

    void WriteBackAll() {
        if (!renderer_ready_) {
            return;
        }
        for (auto& t : targets_) {
            WriteBack(*t);
        }
    }

    void WriteBackOverlapping(u32 paddr, u32 size, bool stale) {
        for (auto& t : targets_) {
            if (t->Overlaps(paddr, paddr + size)) {
                if (DirtyOverlaps(*t, paddr, paddr + size)) {
                    WriteBack(*t);
                }
                if (stale) {
                    MarkStale(*t, paddr, paddr + size);
                }
            }
        }
    }

    void MarkAllStale() {
        for (auto& t : targets_) {
            t->stale = true;
            NoteStale(*t);
        }
    }

    // The target a draw uses: written back and stale-marked where another target overlaps, loaded when stale.
    // `rows`: the guest rows [first, last) the use covers (a draw's rectangle, whole rows of tiles; all of them by default).
    // Only there is another target written back first (when what it drew lies there) and marked stale, and are this one's
    // stale rows loaded: the rest of the target is left as it is.
    Target* UseTarget(bool depth, u32 paddr, u32 format, u32 width, u32 height, bool shadow = false,
                      std::pair<u32, u32> rows = {0, 0xFFFFFFFFu}) {
        Target* t = GetTarget(depth, paddr, format, width, height, shadow);
        if (t == nullptr) {
            return nullptr;
        }
        const u32 tile_row = TileRowBytes(*t);
        const u32 first = std::min(rows.first, t->height), last = std::min(rows.second, t->height);
        const u32 begin = t->paddr + first / 8 * tile_row;
        const u32 end = last >= t->height ? t->paddr + t->bytes : t->paddr + (last + 7) / 8 * tile_row;
        const CauseScope cause(*this, WritebackCause::Overlap);
        const StaleWhy why(*this, "another target drawn over it");
        for (auto& other : targets_) {
            if (other.get() != t && first < last && other->Overlaps(begin, end)) {
                if (DirtyOverlaps(*other, begin, end)) {
                    WriteBack(*other);  // what it drew lies under this use
                }
                MarkStale(*other, begin, end);
            }
        }
        Refresh(*t, first, last);
        return t;
    }

    // The guest rows a draw rectangle (image pixels at `scale`) covers in a target of `height` rows, in whole rows of
    // tiles (MarkDirty's rows).
    static std::pair<u32, u32> RowsOf(u32 height, const rhi::Rect& scissor, u32 scale) {
        Target probe;
        probe.height = height;
        MarkDirty(probe, scissor, scale);
        return {probe.dirty_first, probe.dirty_last};
    }

    // Another target's newer tiles (its dirty rows) that lie in `dst`'s memory, copied image to image: the same tile
    // layout in memory (same colour format and scale, tile-aligned addresses), so `dst` holds what `src` drew there with
    // no write-back. False: the tiles cannot be copied (the caller writes `src` back).
    static bool CanCopyTiles(const Target& src, const Target& dst) {
        if (src.depth || dst.depth || src.shadow || dst.shadow || src.format != dst.format || src.scale != dst.scale ||
            src.stale || src.width % 8 != 0 || dst.width % 8 != 0 || src.height % 8 != 0 || dst.height % 8 != 0 ||
            src.dirty_first >= src.dirty_last) {
            return false;
        }
        // Stale rows of src (memory newer than its image) among the rows the copy takes, its dirty ones: not from its
        // image. Elsewhere they do not matter. (MH4U's Gathering Hall cutscene: a target whose rows 80-88 another one
        // wrote, sampled under the scene's target every frame, was written back whole each frame instead: 400 KB read
        // back, 18-35 ms of a Mali's frame.)
        for (const auto& [a, b] : src.stale_rows) {
            if (a < src.dirty_last && src.dirty_first < b) {
                return false;
            }
        }
        const u32 tile = 64 * ColorBytesPerPixel(src.format);
        const s64 delta = static_cast<s64>(src.paddr) - static_cast<s64>(dst.paddr);
        return delta % static_cast<s64>(tile) == 0;
    }

    // The guest memory CopyTiles brings over from `src`: its dirty rows of tiles.
    static std::pair<u32, u32> CopiedBytes(const Target& src) {
        const u32 tile_row = TileRowBytes(src);
        const u32 end = src.dirty_last >= src.height ? src.paddr + src.bytes : src.paddr + (src.dirty_last + 7) / 8 * tile_row;
        return {src.paddr + src.dirty_first / 8 * tile_row, end};
    }

    // Stale rows of `dst` that tiles copied in will cover whole are not loaded from guest memory first (memory there is
    // older than the tiles): `bytes`, the guest memory the copies bring, merged.
    static void DropCoveredStaleRows(Target& dst, std::vector<std::pair<u32, u32>> bytes) {
        if (dst.stale || dst.stale_rows.empty() || bytes.empty()) {
            return;
        }
        std::sort(bytes.begin(), bytes.end());
        std::vector<std::pair<u32, u32>> merged;
        for (const auto& b : bytes) {
            if (!merged.empty() && b.first <= merged.back().second) {
                merged.back().second = std::max(merged.back().second, b.second);
            } else {
                merged.push_back(b);
            }
        }
        const u32 tile_row = TileRowBytes(dst);
        auto covered = [&](u32 row) {  // the row of tiles that starts at guest row `row`
            const u64 begin = u64{dst.paddr} + u64{row / 8} * tile_row, end = begin + tile_row;
            for (const auto& [b, e] : merged) {
                if (b <= begin && end <= e) return true;
            }
            return false;
        };
        std::vector<std::pair<u32, u32>> keep;
        for (const auto& [a, b] : dst.stale_rows) {
            u32 run = a;
            for (u32 row = a; row < b; row += 8) {
                if (covered(row)) {
                    if (run < row) keep.emplace_back(run, row);
                    run = row + 8;
                }
            }
            if (run < b) keep.emplace_back(run, b);
        }
        dst.stale_rows = std::move(keep);
    }

    // The tiles of each of `srcs` (CanCopyTiles holds for each) into `dst`, in one CopyImages; a source whose tiles `dst`
    // still holds as they are is left out.
    void CopyTiles(std::span<Target* const> srcs, Target& dst) {
        std::vector<std::vector<rhi::ImageCopyRegion>> sets;
        std::vector<const Target*> from;
        for (const Target* src : srcs) {
            const bool held = std::any_of(dst.copied.begin(), dst.copied.end(), [&](const Target::Copied& c) {
                return c.src == src && c.src_version == src->version;
            });
            if (held) {
                ++frame_.tile_copies_reused;
                continue;
            }
            std::vector<rhi::ImageCopyRegion> regions = TileRegions(*src, dst);
            if (!regions.empty()) {
                sets.push_back(std::move(regions));
                from.push_back(src);
            }
        }
        if (from.empty()) {
            return;
        }
        if (trace_.active) {
            u64 px = 0;
            for (const auto& set : sets) {
                for (const rhi::ImageCopyRegion& r : set) px += u64{r.width} * r.height;
            }
            TraceEventF("tiles x{} -> {:08x} {}x{} px {}", from.size(), dst.paddr, dst.width, dst.height, px);
        }
        if (dst.screen != nullptr || std::any_of(from.begin(), from.end(), [](const Target* t) { return t->screen != nullptr; })) {
            // (gpu3: screen targets' pixels read and written here, the rows of tiles the copies touch)
            const u32 tile_row = TileRowBytes(dst);
            u32 lo_all = dst.height, hi_all = 0;
            for (const Target* src : from) {
                const auto [begin, end] = CopiedBytes(*src);
                lo_all = std::min(lo_all, begin > dst.paddr ? (begin - dst.paddr) / tile_row * 8 : 0);
                hi_all = std::max(hi_all, std::min(dst.height, end > dst.paddr ? (end - dst.paddr + tile_row - 1) / tile_row * 8 : 0));
                if (src->screen != nullptr) {
                    ScreenRead(*const_cast<Target*>(src), WholeImage(*src), "tiles copied from it");
                }
            }
            if (lo_all < hi_all) {
                ScreenWrite(dst, RowsRect(dst, lo_all, hi_all), nullptr, false, "tiles copied into it");
            }
        }
        EnsureRecording();
        composer_->EndPass(*rec_);
        size_t regions = 0;
        for (const auto& set : sets) regions += set.size();
        if (!(regions > 8 && DrawTiles(from, sets, dst))) {
            std::vector<rhi::ImageCopyJob> jobs;
            for (size_t i = 0; i < from.size(); ++i) {
                jobs.push_back(rhi::ImageCopyJob{from[i]->image.get(), dst.image.get(), sets[i]});
            }
            rec_->CopyImages(jobs);
        }
        frame_.tile_copies += from.size();
        const u32 tile_row = TileRowBytes(dst);
        for (const Target* src : from) {
            // the guest rows of dst the copy wrote (in part or whole)
            const auto [begin, end] = CopiedBytes(*src);
            const u32 lo = begin > dst.paddr ? (begin - dst.paddr) / tile_row * 8 : 0;
            const u32 hi = std::min(dst.height, end > dst.paddr ? (end - dst.paddr + tile_row - 1) / tile_row * 8 : 0);
            dst.Changed(lo, hi, /*copy=*/true);
            std::erase_if(dst.copied, [&](const Target::Copied& c) { return c.src == src; });
            dst.copied.push_back(Target::Copied{src, src->version, lo, hi});
        }
    }

    // ---- other targets' tiles drawn in (gpu4) ----
    // A source narrower than the target (the bloom's buffers inside MH4U's wide scene target: 64, 32 and 16 pixels wide
    // in one 256 wide) makes a copy region of each of its rows of tiles: about 110 small copies a frame, 4.1 ms of a Moto
    // g06's GPU at 3x for 313k pixels. The same regions are drawn here in one pass over the destination's rows they reach,
    // an instanced draw a source (kTileVertexGlsl): two triangles a region, a texel fetched at the region's offset a
    // pixel. Used when the copies have more than 8 regions; false when it cannot be drawn (the caller copies; nothing was
    // recorded). MH4N_TILE_DRAW=0: always the copies.
    bool EnsureTilePipeline() {
        if (tile_pipeline_ != nullptr) {
            return true;
        }
        if (tile_pipeline_failed_ || !EnsureScreenPipeline()) {
            return false;
        }
        tile_pipeline_failed_ = true;
        std::string error;
        if (!shader::CompileGlslToSpirv(kTileVertexGlsl, tile_vs_, error) ||
            !fragment::CompileFragmentGlslToSpirv(kTileFragmentGlsl, tile_fs_, error)) {
            MH4N_LOG(Error, kLog, "the tile pass's shaders do not compile: {}", error);
            return false;
        }
        rhi::GraphicsPipelineDesc desc;
        desc.vertex.spirv = tile_vs_;
        desc.fragment.spirv = tile_fs_;
        desc.topology = rhi::Topology::TriangleList;
        desc.color_format = backend_->ChooseFormat(rhi::Format::Rgba8Unorm, rhi::FormatUse::ColorTarget).format;
        desc.sets = {ScreenSetDesc()};
        desc.name = "tiles";
        rhi::Result<std::shared_ptr<rhi::Pipeline>> pipeline = backend_->CreateGraphicsPipeline(desc);
        if (!pipeline.ok()) {
            MH4N_LOG(Error, kLog, "the tile pass's pipeline: {}", pipeline.error());
            return false;
        }
        tile_pipeline_ = *pipeline;
        tile_pipeline_failed_ = false;
        return true;
    }

    bool DrawTiles(const std::vector<const Target*>& from, const std::vector<std::vector<rhi::ImageCopyRegion>>& sets,
                   Target& dst) {
        static const bool on = [] {
            const char* v = std::getenv("MH4N_TILE_DRAW");
            return v == nullptr || *v != '0';
        }();
        if (!on || AbOffFor("tiledraw") || !EnsureTilePipeline()) {
            return false;
        }
        const u32 n = dst.scale, width = dst.width * n, height = dst.height * n;
        const u64 alignment = std::max<u64>(backend_->Caps().uniform_buffer_offset_alignment, 256);
        struct Job {
            rhi::BindingSet* set;
            u32 count;
        };
        std::vector<Job> jobs;
        s32 y0 = static_cast<s32>(height), y1 = 0;
        for (size_t i = 0; i < from.size(); ++i) {
            const std::vector<rhi::ImageCopyRegion>& regions = sets[i];
            for (size_t first = 0; first < regions.size(); first += kTileRegionsPerDraw) {
                const u32 count = static_cast<u32>(std::min<size_t>(kTileRegionsPerDraw, regions.size() - first));
                rhi::Result<rhi::RingAllocation> alloc = vs_ring_->Map(sizeof(TileRegionBlock), alignment);
                if (!alloc.ok()) {
                    return false;
                }
                TileRegionBlock* out = reinterpret_cast<TileRegionBlock*>(alloc->data.data());
                out->size[0] = static_cast<float>(width);
                out->size[1] = static_cast<float>(height);
                out->size[2] = out->size[3] = 0.0f;
                for (u32 k = 0; k < count; ++k) {
                    const rhi::ImageCopyRegion& r = regions[first + k];
                    out->r[2 * k][0] = static_cast<s32>(r.dst_x);
                    out->r[2 * k][1] = static_cast<s32>(r.dst_y);
                    out->r[2 * k][2] = static_cast<s32>(r.width);
                    out->r[2 * k][3] = static_cast<s32>(r.height);
                    out->r[2 * k + 1][0] = static_cast<s32>(r.src_x) - static_cast<s32>(r.dst_x);
                    out->r[2 * k + 1][1] = static_cast<s32>(r.src_y) - static_cast<s32>(r.dst_y);
                    out->r[2 * k + 1][2] = out->r[2 * k + 1][3] = 0;
                    y0 = std::min(y0, static_cast<s32>(r.dst_y));
                    y1 = std::max(y1, static_cast<s32>(r.dst_y + r.height));
                }
                vs_ring_->GetBuffer()->Flush(alloc->offset, sizeof(TileRegionBlock));
                if (!vs_ring_->Commit(sizeof(TileRegionBlock)).ok()) {
                    return false;
                }
                rhi::Result<rhi::BindingSet*> set = screen_arena_->Acquire(ScreenSetDesc());
                if (!set.ok()) {
                    return false;
                }
                rhi::Result<void> bound = (*set)->SetSampledImage(0, *from[i]->image, rhi::ImageView::Sampled);
                if (bound.ok()) {
                    bound = (*set)->SetUniformBuffer(1, *vs_ring_->GetBuffer(), alloc->offset, sizeof(TileRegionBlock));
                }
                if (!bound.ok()) {
                    return false;
                }
                jobs.push_back(Job{*set, count});
            }
        }
        if (jobs.empty() || y1 <= y0) {
            return false;
        }
        rhi::TargetPassDesc pass;
        pass.color = dst.image.get();
        pass.color_load = rhi::LoadOp::Load;
        pass.area = rhi::Rect{0, y0, width, static_cast<u32>(y1 - y0)};
        rec_->BeginTargetPass(pass);
        rec_->SetPipeline(*tile_pipeline_);
        rhi::Viewport viewport;
        viewport.width = static_cast<float>(width);
        viewport.height = static_cast<float>(height);
        viewport.max_depth = 1.0f;
        rec_->SetViewport(viewport);
        rec_->SetScissor(pass.area);
        for (const Job& job : jobs) {
            rec_->Bind(*job.set, 0);
            rec_->Draw(6, job.count);
        }
        rec_->EndPass();
        ++frame_.tile_draws;
        return true;
    }

    // The regions of `src`'s dirty rows of tiles in `dst`'s image, in runs along one row of tiles of both.
    static std::vector<rhi::ImageCopyRegion> TileRegions(const Target& src, const Target& dst) {
        const u32 tile = 64 * ColorBytesPerPixel(src.format);
        const s64 delta = static_cast<s64>(src.paddr) - static_cast<s64>(dst.paddr);
        const u32 n = src.scale;
        const u32 src_cols = src.width / 8, dst_cols = dst.width / 8;
        const u32 dst_tiles = dst.bytes / tile;
        std::vector<rhi::ImageCopyRegion> regions;
        // the tiles of src's dirty rows, in runs along one row of tiles of both images
        const u32 last_tile = std::min(src.bytes / tile, (src.dirty_last + 7) / 8 * src_cols);
        for (u32 i = src.dirty_first / 8 * src_cols; i < last_tile;) {
            const s64 j64 = static_cast<s64>(i) + delta / static_cast<s64>(tile);
            if (j64 < 0 || j64 >= static_cast<s64>(dst_tiles)) {
                ++i;
                continue;
            }
            const u32 j = static_cast<u32>(j64);
            const u32 sx = i % src_cols, sy = i / src_cols, dx = j % dst_cols, dy = j / dst_cols;
            const u32 run = std::min({src_cols - sx, dst_cols - dx, last_tile - i, dst_tiles - j});
            rhi::ImageCopyRegion region;
            region.src_x = sx * 8 * n;
            region.src_y = (src.height - (sy + 1) * 8) * n;  // a target's image holds guest row 0 last
            region.dst_x = dx * 8 * n;
            region.dst_y = (dst.height - (dy + 1) * 8) * n;
            region.width = run * 8 * n;
            region.height = 8 * n;
            regions.push_back(region);
            i += run;
        }
        return regions;
    }

    // ---- the screens at the internal scale (scaled_screens.h) ----
    // GxEngine::ToPhysical without its log (the engine logs an address it does not know when it runs the command).
    u32 GxPhysical(u32 vaddr) {
        return vaddr == 0 ? 0u : host_.VirtualToPhysical(vaddr).value_or(vaddr);
    }

    // Where the GX engine will write for `command` (engine.cpp and blitter.cpp: what each command writes, in bytes).
    GxWrites WritesOf(const GxCommand& command) {
        const u32* w = command.words;
        GxWrites out;
        const auto add = [&](u64 begin, u64 end) {
            if (end > begin) {
                out.ranges.emplace_back(static_cast<u32>(begin), static_cast<u32>(std::min<u64>(end, 0xFFFFFFFFull)));
            }
        };
        switch (static_cast<gx::CommandId>(w[0] & 0xFF)) {
        case gx::CommandId::RequestDma: {
            // Page by page through the physical view; a page without one holds no target.
            constexpr u64 kPage = 0x1000;
            u64 vaddr = w[2];
            const u64 end = vaddr + w[3];
            while (vaddr < end && vaddr <= 0xFFFFFFFFull) {
                const u64 chunk = std::min<u64>(kPage - (vaddr & (kPage - 1)), end - vaddr);
                if (const std::optional<u32> p = host_.VirtualToPhysical(static_cast<u32>(vaddr))) {
                    if (!out.ranges.empty() && out.ranges.back().second == *p) {
                        out.ranges.back().second = static_cast<u32>(std::min<u64>(u64{*p} + chunk, 0xFFFFFFFFull));
                    } else {
                        add(*p, u64{*p} + chunk);
                    }
                }
                vaddr += chunk;
            }
            break;
        }
        case gx::CommandId::MemoryFill: {
            const u32 starts[2] = {w[1], w[4]};
            const u32 ends[2] = {w[3], w[6]};
            const u32 controls[2] = {w[7] & 0xFFFF, w[7] >> 16};
            for (int i = 0; i < 2; ++i) {
                if (starts[i] == 0 || (controls[i] & 1) == 0) {
                    continue;  // no fill
                }
                const u32 begin = GxPhysical(starts[i]) & ~7u;  // the registers hold the address / 8
                const u32 end = GxPhysical(ends[i]) & ~7u;
                if (end <= begin) {
                    continue;
                }
                const u64 length = end - begin;
                u64 written = (length + 1) / 2 * 2;  // 16-bit units, the last one whole
                if ((controls[i] >> 8) & 1) {
                    written = (length + 2) / 3 * 3;  // 24-bit units
                } else if ((controls[i] >> 9) & 1) {
                    written = length / 4 * 4;  // 32-bit units
                }
                add(begin, u64{begin} + written);
            }
            break;
        }
        case gx::CommandId::DisplayTransfer: {
            gx::TransferRegs regs;
            regs.output_address = GxPhysical(w[2]) >> 3;
            regs.input_size = w[3];
            regs.output_size = w[4];
            regs.flags = w[5];
            if (regs.IsTextureCopy() || (regs.FlipVertically() && regs.CropInputLines())) {
                out.all = true;  // a copy with the registers the engine holds; a shifted output
                break;
            }
            const u32 bpp = gx::BytesPerPixel(regs.OutputFormat());
            if (bpp == 0 || regs.Scaling() > 2) {
                break;  // the engine writes nothing
            }
            const u64 width = regs.OutputWidth() >> (regs.Scaling() != 0 ? 1 : 0);
            const u64 height = regs.OutputHeight() >> (regs.Scaling() == 2 ? 1 : 0);
            const bool tiled_out = regs.InputLinear() ? !regs.DontSwizzle() : regs.DontSwizzle();
            const u64 bytes = tiled_out ? ((width + 7) & ~u64{7}) * ((height + 7) & ~u64{7}) * bpp : width * height * bpp;
            add(regs.OutputAddress(), u64{regs.OutputAddress()} + bytes);
            break;
        }
        case gx::CommandId::TextureCopy: {
            if ((w[6] & 8) == 0) {
                out.all = true;  // the flags make it a display transfer with the registers the engine holds
                break;
            }
            const u32 dst = GxPhysical(w[2]) & ~7u;
            const u32 size = w[3] & ~15u;
            const u32 gap = (w[5] >> 16) * 16;
            const u32 width = gap == 0 ? size : (w[5] & 0xFFFF) * 16;
            if (size == 0 || width == 0) {
                break;
            }
            const u64 rows = (u64{size} + width - 1) / width;
            add(dst, u64{dst} + size + (rows - 1) * gap);
            break;
        }
        default:
            break;  // a cache flush, an unknown command: nothing is written (command lists do not come here)
        }
        return out;
    }

    // The pictures of the framebuffers that `writes` touches are no longer what they show.
    // The same for one range [begin, end) (a draw's: no list made)
    void ForgetScaledScreens(u32 begin, u32 end) {
        for (ScaledScreen& s : scaled_screens_) {
            if (s.valid && s.address < end && begin < s.address + s.bytes) {
                s.valid = false;
                s.picture.reset();
            }
        }
    }

    void ForgetScaledScreens(const GxWrites& writes) {
        for (ScaledScreen& s : scaled_screens_) {
            if (s.valid && writes.Overlaps(s.address, s.address + s.bytes)) {
                s.valid = false;
                s.picture.reset();  // (its buffer free again once the display has let go of it too)
            }
        }
    }

    // ---- screen targets (gpu3; ScreenTarget above) ----
    // The top screen as the target's own pictures on the GPUs that may owe (OwingAllowed: Mali and Adreno with their own
    // drivers, Vulkan 1.2 or later), the only ones it was seen to work on. The others copy the box at every hand-over,
    // and with the touch screen hidden on a wide screen they copied the whole picture: three of those in a row sent the
    // top screen to the upright copy for 120 transfers, then back to the target's pictures, and so on, and there the
    // target's pictures were not drawn as the upright copy is (bright or broken frames every few seconds that showing
    // the touch screen ended, as its costly hand-overs kept the upright copy: 4u-native #31 PowerVR BXM, #33 Mali-G71
    // r19, #41 Mali-G72). Those GPUs keep the upright copy, which costs them the same copy a frame.
    // MH4N_TARGET_SCREENS=0: the upright copy always, as before; =1: the target's pictures on any GPU (measurement aids).
    bool TargetScreensOn() const {
        static const int env = [] {
            const char* v = std::getenv("MH4N_TARGET_SCREENS");
            return v != nullptr && (*v == '0' || *v == '1') ? *v - '0' : -1;
        }();
        return env < 0 ? owing_allowed_ : env == 1;
    }
    // MH4N_TARGET_SCREENS_CHECK=1 (a measurement aid): the upright copy is drawn as well, and goes with the picture
    // (SharedPicture::Reference) for the app to compare its own turn of the target with it.
    static bool TargetScreensCheck() {
        static const bool on = [] {
            const char* v = std::getenv("MH4N_TARGET_SCREENS_CHECK");
            return v != nullptr && *v == '1';
        }();
        return on;
    }
    // MH4N_TARGET_EAGER=0: a picture is handed over at the next transfer that reads its target, a write, or the VBlank,
    // never at its own transfer (a measurement aid).
    static bool EagerHandOver() {
        static const bool on = [] {
            const char* v = std::getenv("MH4N_TARGET_EAGER");
            return v == nullptr || *v != '0';
        }();
        return on;
    }
    static constexpr size_t kTargetBuffers = 6;  // a screen target's images, more made only for a twin (up to 8)

    static rhi::Rect WholeImage(const Target& t) { return rhi::Rect{0, 0, t.width * t.scale, t.height * t.scale}; }
    // Guest rows [first, last) of `t` as image pixels (the whole width; guest row 0 is the image's last row).
    static rhi::Rect RowsRect(const Target& t, u32 first, u32 last) {
        const u32 n = t.scale, lo = std::min(first, t.height), hi = std::min(last, t.height);
        return rhi::Rect{0, static_cast<s32>((t.height - hi) * n), t.width * n, (hi - std::min(lo, hi)) * n};
    }
    static bool Empty(const rhi::Rect& r) { return r.width == 0 || r.height == 0; }
    static u64 Pixels(const rhi::Rect& r) { return u64{r.width} * r.height; }
    static u64 Pixels(const std::vector<rhi::Rect>& rects) {
        u64 px = 0;
        for (const rhi::Rect& r : rects) px += Pixels(r);
        return px;
    }
    static std::optional<rhi::Rect> Intersect(const rhi::Rect& a, const rhi::Rect& b) {
        const s32 x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
        const s32 x1 = std::min(a.x + static_cast<s32>(a.width), b.x + static_cast<s32>(b.width));
        const s32 y1 = std::min(a.y + static_cast<s32>(a.height), b.y + static_cast<s32>(b.height));
        if (x1 <= x0 || y1 <= y0) return std::nullopt;
        return rhi::Rect{x0, y0, static_cast<u32>(x1 - x0), static_cast<u32>(y1 - y0)};
    }
    static bool Contains(const rhi::Rect& outer, const rhi::Rect& inner) {
        return inner.x >= outer.x && inner.y >= outer.y &&
               inner.x + static_cast<s32>(inner.width) <= outer.x + static_cast<s32>(outer.width) &&
               inner.y + static_cast<s32>(inner.height) <= outer.y + static_cast<s32>(outer.height);
    }
    // `rects` less `cut` (each split in up to four).
    static std::vector<rhi::Rect> Subtract(const std::vector<rhi::Rect>& rects, const rhi::Rect& cut) {
        std::vector<rhi::Rect> out;
        for (const rhi::Rect& r : rects) {
            const std::optional<rhi::Rect> in = Intersect(r, cut);
            if (!in) {
                out.push_back(r);
                continue;
            }
            const s32 rx1 = r.x + static_cast<s32>(r.width), ry1 = r.y + static_cast<s32>(r.height);
            const s32 ix1 = in->x + static_cast<s32>(in->width), iy1 = in->y + static_cast<s32>(in->height);
            auto add = [&](s32 x0, s32 y0, s32 x1, s32 y1) {
                if (x1 > x0 && y1 > y0) out.push_back(rhi::Rect{x0, y0, static_cast<u32>(x1 - x0), static_cast<u32>(y1 - y0)});
            };
            add(r.x, r.y, rx1, in->y);    // above
            add(r.x, iy1, rx1, ry1);      // below
            add(r.x, in->y, in->x, iy1);  // beside
            add(ix1, in->y, rx1, iy1);
        }
        return out;
    }
    // Guest rows (whole rows of tiles) as image rects of the whole width.
    static std::vector<rhi::Rect> RowRects(const Target& t, const std::vector<std::pair<u32, u32>>& rows) {
        std::vector<rhi::Rect> out;
        for (const auto& [first, last] : rows) {
            const rhi::Rect r = RowsRect(t, first, last);
            if (!Empty(r)) out.push_back(r);
        }
        return out;
    }
    static bool HandOverColumns() {
        static const bool on = [] {
            const char* v = std::getenv("MH4N_HANDOVER_COLUMNS");
            return v == nullptr || *v != '0';
        }();
        return on;
    }
    // The image rects of `t` where a spare lacking guest rows `rows` differs: whole rows in `wide`, the others left of `x1`
    // (image pixels). MH4N_HANDOVER_COLUMNS=0 (or the A/B's "hocols" phase): whole rows, as before.
    std::vector<rhi::Rect> LackingRects(const Target& t, const std::vector<std::pair<u32, u32>>& rows,
                                        const std::vector<std::pair<u32, u32>>& wide, u32 x1) const {
        if (!HandOverColumns() || AbOffFor("hocols")) {
            return RowRects(t, rows);
        }
        std::vector<rhi::Rect> out;
        const auto narrow = [&](u32 a, u32 z) {
            rhi::Rect r = RowsRect(t, a, z);
            r.width = std::min(r.width, x1);
            if (!Empty(r)) out.push_back(r);
        };
        for (const auto& [first, last] : rows) {
            u32 at = first;  // (the range's parts in `wide` whole, the rest left of x1)
            for (const auto& [wf, wl] : wide) {
                if (wl <= at || wf >= last) continue;
                const u32 a = std::max(wf, at), z = std::min(wl, last);
                if (a > at) narrow(at, a);
                const rhi::Rect r = RowsRect(t, a, z);
                if (!Empty(r)) out.push_back(r);
                at = z;
            }
            if (at < last) narrow(at, last);
        }
        return out;
    }
    // The guest rows (whole rows of tiles) an image rect of `t` covers.
    static std::pair<u32, u32> GuestRows(const Target& t, const rhi::Rect& r) { return RowsOf(t.height, r, t.scale); }

    // Copies `rects` of `from` into `to` (no pass open).
    void CopyRects(rhi::Image& from, rhi::Image& to, const std::vector<rhi::Rect>& rects) {
        if (rects.empty()) return;
        std::vector<rhi::ImageCopyRegion> regions;
        for (const rhi::Rect& r : rects) {
            rhi::ImageCopyRegion region;
            region.src_x = region.dst_x = static_cast<u32>(r.x);
            region.src_y = region.dst_y = static_cast<u32>(r.y);
            region.width = r.width;
            region.height = r.height;
            regions.push_back(region);
        }
        const rhi::ImageCopyJob job{&from, &to, regions};
        rec_->CopyImages(std::span<const rhi::ImageCopyJob>(&job, 1));
    }

    // A new shared image for `t`, joined to its images (null: it could not be made).
    TargetBuffer* MakeTargetBuffer(ScreenTarget& s, const Target& t) {
        rhi::Result<std::shared_ptr<rhi::SharedImage>> made = shared_->Make(t.width * t.scale, t.height * t.scale);
        if (!made.ok()) {
            if (target_logs_++ < 8) {
                MH4N_LOG(Warning, kLog, "a shared image for the screen target {:08x}: {}", t.paddr, made.error());
            }
            return nullptr;
        }
        auto b = std::make_unique<TargetBuffer>();
        b->shared = *made;
        b->image = std::shared_ptr<rhi::Image>(b->shared, &b->shared->GetImage());
        b->BehindWhole(0, t.height);
        s.buffers.push_back(std::move(b));
        return s.buffers.back().get();
    }

    // The top screen's target gets shared images: its image and twin, copied whole into new ones (once).
    bool PromoteScreenTarget(Target& t) {
        auto screen = std::make_unique<ScreenTarget>();
        TargetBuffer* image = MakeTargetBuffer(*screen, t);
        TargetBuffer* twin = image != nullptr && t.twin != nullptr ? MakeTargetBuffer(*screen, t) : nullptr;
        if (image == nullptr) {
            t.screen_failed = true;
            return false;
        }
        EnsureRecording();
        composer_->EndPass(*rec_);
        const std::vector<rhi::Rect> whole{WholeImage(t)};
        CopyRects(*t.image, *image->image, whole);
        image->BehindNone();
        if (twin != nullptr) {
            CopyRects(*t.twin, *twin->image, whole);
            twin->BehindNone();
        }
        FlushRecording();  // (the old images are retired after the copies' submission)
        backend_->Retire(t.image, last_submit_);
        t.image = image->image;
        if (t.twin != nullptr) {
            backend_->Retire(t.twin, last_submit_);
            t.twin = twin != nullptr ? twin->image : nullptr;
            if (twin == nullptr) t.TwinSame();
        }
        screen->image = image;
        screen->twin = twin;
        t.screen = std::move(screen);
        MH4N_LOG(Info, kLog,
                 "the top screen's target {:08x} {}x{} is shown as it is, turned upright by the app (its images are "
                 "shared; MH4N_TARGET_SCREENS=0: the upright copy)",
                 t.paddr, t.width, t.height);
        return true;
    }

    // Its images become ordinary ones (a shadow source is made of it, GetTarget): the pictures keep theirs (handed over
    // with the next submission), the others go.
    void DemoteScreenTarget(Target& t) {
        ScreenTarget& s = *t.screen;
        if (!s.owed.empty()) TakeOwed(t, "a shadow source made of it");
        for (auto& b : s.buffers) {
            if (b->holders->load(std::memory_order_acquire) > 0 && !b->released) {
                b->release_owed = true;  // (the picture of the target's image is handed over too)
                any_release_owed_ = true;
            }
            orphan_buffers_.push_back(std::move(b));  // (dropped once submitted and let go of: FlushRecording)
        }
        if (s.pretaken != nullptr) retire_after_submit_.push_back(std::move(s.pretaken));  // (the open recording may use it)
        t.screen.reset();
    }

    // The screen target and image of a picture's buffer (or null, null; an image no target holds any more: null, its).
    std::pair<Target*, TargetBuffer*> FindTargetBuffer(u64 id) {
        for (auto& t : targets_) {
            if (t->screen == nullptr) continue;
            for (const auto& b : t->screen->buffers) {
                if (b->shared->Id() == id) return {t.get(), b.get()};
            }
        }
        for (const auto& b : orphan_buffers_) {
            if (b->shared->Id() == id) return {nullptr, b.get()};
        }
        return {nullptr, nullptr};
    }

    // Free: no picture, no hand-over owed, not owed from, not the target's image or twin.
    static bool FreeBuffer(const ScreenTarget& s, const TargetBuffer& b) {
        return &b != s.image && &b != s.twin && &b != s.owed_from && !b.release_owed &&
               b.holders->load(std::memory_order_acquire) == 0;
    }
    size_t SparesAvailable(const ScreenTarget& s) const {
        size_t n = s.twin != nullptr ? 1 : 0;
        for (const auto& b : s.buffers) n += FreeBuffer(s, *b) ? 1 : 0;
        return n + (s.buffers.size() < kTargetBuffers ? kTargetBuffers - s.buffers.size() : 0);
    }

    // An image of `t` to become its image or twin, and the guest rows where it differs from the image: its twin (when
    // `twin_too`), else a free one (the one that lacks least), else a new one. Acquired back from the app's side if it was
    // there. Null: none could be had.
    // `lacking`: the guest rows the spare lacks; of those, `lacking_wide` in any column, the others left of `lacking_x1`.
    TargetBuffer* TakeSpare(Target& t, bool twin_too, std::vector<std::pair<u32, u32>>& lacking,
                            std::vector<std::pair<u32, u32>>& lacking_wide, u32& lacking_x1) {
        ScreenTarget& s = *t.screen;
        TargetBuffer* b = nullptr;
        if (twin_too && s.twin != nullptr) {
            b = s.twin;
            lacking = std::move(t.twin_rows);
            lacking_wide = std::move(t.twin_wide);
            lacking_x1 = t.twin_x1;
            t.TwinSame();
            t.twin.reset();
            s.twin = nullptr;
        } else {
            u64 least = ~0ull;
            for (const auto& c : s.buffers) {
                if (!FreeBuffer(s, *c)) continue;
                u64 rows = 0;
                for (const auto& [a, z] : c->behind) rows += z - a;
                if (rows < least) {
                    least = rows;
                    b = c.get();
                }
            }
            if (b == nullptr && s.buffers.size() < kTargetBuffers + 2) {
                b = MakeTargetBuffer(s, t);
                if (b != nullptr) {
                    ++frame_.target_buffers_made;
                    MH4N_LOG(Info, kLog, "the screen target {:08x} has {} images", t.paddr, s.buffers.size());
                }
            }
            if (b == nullptr) return nullptr;
            lacking = std::move(b->behind);
            lacking_wide = std::move(b->behind_wide);
            lacking_x1 = b->behind_x1;
            b->BehindNone();
        }
        if (b->released) {
            EnsureRecording();
            composer_->EndPass(*rec_);
            shared_->AcquireAll(*rec_, *b->shared);
            b->released = false;
        }
        return b;
    }

    // The top screen's picture of `t` as its own image (null: the upright copy this time). The touch screen's keeps the
    // copy; so does a device whose shared images cannot be copied.
    TargetBuffer* TargetPicture(Target& t, u32 width, u32 height, u32 in_format, u32 out_format) {
        if (!TargetScreensOn() || AbScreensOff() || (width == 240 && height == 320) || t.depth || t.shadow || t.screen_failed) {
            return nullptr;
        }
        const auto refuse = [&](const char* why) -> TargetBuffer* {
            if (!target_refusal_told_) {
                target_refusal_told_ = true;
                MH4N_LOG(Info, kLog, "the top screen's picture is drawn upright: {} (transfer formats {} -> {})", why, in_format,
                         out_format);
            }
            return nullptr;
        };
        rhi::SharedImages* shared = EnsureShared();
        if (shared == nullptr) {
            return refuse("the device shares no images");
        }
        if (!shared->CanCopy()) {
            return refuse("the device's shared images cannot be copied from and to");
        }
        if (t.screen == nullptr && !PromoteScreenTarget(t)) {
            return nullptr;
        }
        ScreenTarget& s = *t.screen;
        s.clear_box = s.clear_box_now;  // (this frame's clear: the next one's expected)
        s.clear_box_now = {};
        if (s.fallback > 0) {
            --s.fallback;
            ++frame_.target_fallbacks;
            return nullptr;
        }
        if (SparesAvailable(s) < 2) {  // (the hand-over takes one, the next twin another)
            ++frame_.target_no_spare;
            return nullptr;
        }
        s.picture_px = u64{width} * height * t.scale * t.scale;
        ++frame_.target_pictures;
        return s.image;
    }

    // Another image takes the place of `t`'s image, which a picture holds (Detach, above): given what it lacks from it
    // but `cleared` (pixels the write that comes writes whatever was there) and, once trusted, but the part of the box the
    // next frame's first clear is expected to write that this write does not reach (owed). The old image goes to the app
    // with the next submission. `area`: the pixels the write may read or write (null: a VBlank's hand-over, none). (On
    // the Galaxy's wide picture the first write is a load of the target's rows the top framebuffer lies over, before the
    // scene's clear: 1.2 million pixels copied a frame until the box was owed there too.)
    void Detach(Target& t, const rhi::Rect* area, const rhi::Rect* cleared, bool keep_twin = false) {
        ScreenTarget& s = *t.screen;
        TargetBuffer* x = s.image;
        std::vector<std::pair<u32, u32>> lacking, lacking_wide;
        u32 lacking_x1 = 0;
        const size_t made_before = s.buffers.size();
        // (not the twin the draw being prepared samples or draws into: ping-pong, a feedback copy)
        keep_twin |= pingpong_.target == &t || self_sampled_;
        TargetBuffer* b = TakeSpare(t, /*twin_too=*/!keep_twin, lacking, lacking_wide, lacking_x1);
        if (b == nullptr) {
            if (target_logs_++ < 8) {
                MH4N_LOG(Error, kLog, "the screen target {:08x} has no image free: the picture shown may change", t.paddr);
            }
            return;
        }
        const bool fresh = s.buffers.size() > made_before;
        EnsureRecording();
        composer_->EndPass(*rec_);
        std::vector<rhi::Rect> need = LackingRects(t, lacking, lacking_wide, lacking_x1);
        if (cleared != nullptr) {
            need = Subtract(need, *cleared);
        }
        // (not the target's stale rows: guest memory or another target's tiles are newer than any of its images there,
        // and whatever reads them brings them up to date first. The bloom's rows a ping-pong left to their buffers' tiles,
        // gpu4: 313k pixels on the Moto at 3x, which made the hand-over "costly" and sent the target to the upright copy)
        for (const rhi::Rect& r : RowRects(t, t.stale_rows)) {
            need = Subtract(need, r);
        }
        // what the next frame's first clear is expected to write, but where this write reaches (it reads or writes there)
        std::vector<rhi::Rect> owe;
        const bool may_owe = OwingAllowed() && !AbOffFor("owing");
        if (!Empty(s.clear_box)) {
            // (the trust in that clear: this write is it, or reaches the box otherwise first, or the next one will tell)
            const bool is_clear = area != nullptr && cleared != nullptr && Contains(*cleared, s.clear_box);
            const bool reaches = area != nullptr && !is_clear && Intersect(*area, s.clear_box).has_value();
            if (is_clear) {
                s.trust = std::min<u32>(s.trust + 1, 1000);
            } else if (reaches) {
                s.trust = 0;
            } else {
                s.watch = s.clear_box;
            }
            if (s.trust >= 2) {
                const std::vector<rhi::Rect> defer = area != nullptr ? Subtract({s.clear_box}, *area)
                                                                     : std::vector<rhi::Rect>{s.clear_box};
                for (const rhi::Rect& d : defer) {
                    for (const rhi::Rect& r : need) {
                        if (const std::optional<rhi::Rect> part = Intersect(r, d)) owe.push_back(*part);
                    }
                    need = Subtract(need, d);
                }
                if (!owe.empty() && may_owe) s.watch = {};  // (what is owed tells instead: TakeOwed, ScreenWrite)
            }
        }
        // A GPU where owed pixels could not be taken back from the published image while the app holds it (OwingAllowed)
        // owes nothing: the box is copied now, while the image is still the renderer's (not counted as a costly hand-over:
        // the target stays on its own pictures).
        u64 box_px = 0;
        if (!owe.empty() && !may_owe) {
            box_px = Pixels(owe);
            ++frame_.target_box_copied;
            need.insert(need.end(), owe.begin(), owe.end());
            owe.clear();
        }
        s.box_from = box_px > 0 ? x : nullptr;
        s.pretaken_of = nullptr;
        if (!owe.empty() && area == nullptr) {  // (after a write, the next snapshot comes after another hand-over)
            PretakeSnapshot(t, *x, owe);
        }
        CopyRects(*x->image, *b->image, need);
        // b is the target's image now; where the others differ from it: what it is owed, what the write changes
        t.image = b->image;
        s.image = b;
        b->BehindNone();
        b->released_at = std::make_shared<std::atomic<u64>>(TargetBuffer::kNotYet);
        b->sync.reset();
        std::vector<std::pair<u32, u32>> rows;
        for (const rhi::Rect& r : owe) {
            const auto [f, l] = GuestRows(t, r);
            JoinRows(rows, f, l);
        }
        for (const rhi::Rect* r : {area, cleared}) {
            if (r != nullptr) {
                const auto [f, l] = GuestRows(t, *r);
                JoinRows(rows, f, l);
            }
        }
        u32 rows_x1 = 0;  // (the rects' right edge, for the twin's columns: Target::TwinDiffers)
        for (const rhi::Rect& r : owe) rows_x1 = std::max(rows_x1, static_cast<u32>(std::max(r.x, 0)) + r.width);
        for (const rhi::Rect* r : {area, cleared}) {
            if (r != nullptr) rows_x1 = std::max(rows_x1, static_cast<u32>(std::max(r->x, 0)) + r->width);
        }
        x->behind = rows;
        x->behind_wide.clear();
        x->behind_x1 = rows_x1;
        for (const auto& c : s.buffers) {
            if (c.get() == x || c.get() == b) continue;
            for (const auto& [f, l] : rows) {
                if (c.get() == s.twin) {
                    t.TwinDiffers(f, l, rows_x1);
                } else {
                    JoinRows(c->behind, f, l);
                    c->behind_x1 = std::max(c->behind_x1, rows_x1);
                }
            }
        }
        x->release_owed = true;
        any_release_owed_ = true;
        const std::string_view where = handover_where_ != nullptr ? handover_where_ : "";
        pacing::Mark(pacing::kTargetHandOver, area != nullptr           ? 3u
                                              : where == "at the VBlank" ? 2u
                                              : where == "at its transfer" ? 4u
                                                                         : 1u);
        s.owed = std::move(owe);
        s.owed_from = s.owed.empty() ? nullptr : x;
        const u64 copied = Pixels(need), owed = Pixels(s.owed);
        ++(area == nullptr ? frame_.target_vblank_handovers : frame_.target_write_handovers);
        frame_.target_copy_px += copied;
        frame_.target_owed_px += owed;
        const auto rect = [](const rhi::Rect* r) {
            return r != nullptr ? fmt::format("{},{} {}x{}", r->x, r->y, r->width, r->height) : std::string("-");
        };
        TraceEventF("hand-over {:08x} {}: {} px copied, {} owed{} (area {}, cleared {}, expected clear {}, trust {})",
                               t.paddr, area != nullptr ? "at a write" : handover_where_ != nullptr ? handover_where_ : "before a write", copied, owed, fresh ? " (a new image)" : "",
                               rect(area), rect(cleared), rect(&s.clear_box), s.trust);
        static const bool log_handovers = [] {
            const char* v = std::getenv("MH4N_TARGET_SCREENS_LOG");
            return v != nullptr && *v == '1';
        }();
        if (log_handovers) {
            MH4N_LOG(Info, kLog, "hand-over {:08x} {}: {} px copied, {} owed{} (area {}, cleared {}, expected clear {}, trust {})",
                     t.paddr, area != nullptr ? "at a write" : handover_where_ != nullptr ? handover_where_ : "before a write", copied, owed, fresh ? " (a new image)" : "",
                     rect(area), rect(cleared), rect(&s.clear_box), s.trust);
        }
        // a hand-over that copied more than a quarter of the picture three times in a row (the touch screen shown: its
        // draws come into the target right after the transfer and keep little of it): the upright copy for a while
        if (!fresh && s.picture_px != 0 && (copied - box_px) * 4 > s.picture_px) {
            if (++s.expensive >= 3) {
                s.expensive = 0;
                s.fallback = s.fallback_next;
                if (target_logs_++ < 8) {
                    MH4N_LOG(Info, kLog, "the screen target {:08x}: its hand-overs copy {} px of a {} px picture: the upright "
                                         "copy for {} transfers", t.paddr, copied, s.picture_px, s.fallback);
                }
                s.fallback_next = std::min<u32>(s.fallback_next * 2, 1920);
            }
        } else if (!fresh) {
            s.expensive = 0;
            s.fallback_next = 120;
        }
    }

    // The rows the hidden touch screen's snapshot read of `t` the last time (the snapshot after the top screen's, right
    // after this hand-over), if they reach what `from` owes: taken now from `from` (about to be handed over, so still the
    // renderer's) with the snapshot's own blit (nearest, to 1x), into the target's `pretaken`, which the snapshot adopts
    // instead of reading the published image (DrawScaledScreen). The A/B's "pretake" phase: not taken (the snapshot reads
    // the published image from the app's side, as before 4u-native #31; a measurement aid).
    void PretakeSnapshot(Target& t, const TargetBuffer& from, const std::vector<rhi::Rect>& owe) {
        ScreenTarget& s = *t.screen;
        if (s.snap_rows == 0 || s.snap_first + s.snap_rows > t.height || AbOffFor("pretake")) return;
        const rhi::Rect rows = RowsRect(t, s.snap_first, s.snap_first + s.snap_rows);
        if (std::none_of(owe.begin(), owe.end(), [&](const rhi::Rect& o) { return Intersect(o, rows).has_value(); })) return;
        if (s.pretaken == nullptr || s.pretaken->Width() != t.width || s.pretaken->Height() != s.snap_rows) {
            if (s.pretaken != nullptr) retire_after_submit_.push_back(std::move(s.pretaken));  // (the open recording may use it)
            rhi::ImageDesc desc;
            desc.format = rhi::Format::Rgba8Unorm;
            desc.width = t.width;
            desc.height = s.snap_rows;
            desc.name = "async screen source";
            rhi::Result<std::shared_ptr<rhi::Image>> made = backend_->CreateImage(desc);
            if (!made.ok()) {
                if (target_logs_++ < 8) {
                    MH4N_LOG(Warning, kLog, "the screen target {:08x}'s snapshot taken ahead: {}", t.paddr, made.error());
                }
                return;
            }
            s.pretaken = *made;
        }
        const u32 n = t.scale;
        rhi::ImageBlitRegion blit;
        blit.src = rhi::Rect{0, static_cast<s32>((t.height - s.snap_first - s.snap_rows) * n), t.width * n, s.snap_rows * n};
        blit.dst = rhi::Rect{0, 0, t.width, s.snap_rows};
        rec_->Blit(*from.image, *s.pretaken, std::span<const rhi::ImageBlitRegion>(&blit, 1), /*linear=*/false);
        s.pretaken_of = &from;
        s.pretaken_first = s.snap_first;
        s.pretaken_rows = s.snap_rows;
    }

    // Owing (a hand-over that leaves the box the next frame's first clear writes to that clear) needs the owed pixels
    // taken back from the published image when something else reaches the box first, even while the app's side holds
    // and draws the picture (the game sampling its last picture: a fade). That was seen to work on Mali and Adreno with
    // their own drivers (Vulkan 1.3: Mali r40 and r49, Adreno 830), the only GPUs that owe; the others copy the box at
    // the hand-over, as the rules for memory another API uses allow. So do drivers below Vulkan 1.2 and drivers that
    // don't say which they are (no driver properties): older ones, where it was never seen. MH4N_OWING=0: never owe,
    // =1: owe on any GPU (measurement aids).
    bool OwingAllowed() const { return owing_allowed_; }
    static bool OwingFor(const rhi::Capabilities& caps) {
        const char* v = std::getenv("MH4N_OWING");
        if (v != nullptr && (*v == '0' || *v == '1')) return *v == '1';
        constexpr u32 kVulkan12 = (1u << 22) | (2u << 12);
        if (caps.api_version < kVulkan12 || caps.driver_id == 0) return false;
        constexpr u32 kDriverQualcomm = 8, kDriverArm = 9;  // (VkDriverId)
        return caps.driver_id == kDriverQualcomm || caps.driver_id == kDriverArm;
    }

    // The pixels the image owes (the expected clear did not come first), taken back from the image that has them. Once
    // no picture of it is left, it is the renderer's again (acquired back from the app's side if it was there, and kept);
    // while one lives, the app's side may be drawing it: acquired for the copy and handed back (OwingAllowed's GPUs only,
    // counted: target_owed_taken_back), and the trust starts again.
    void TakeOwed(Target& t, const char* why) {
        ScreenTarget& s = *t.screen;
        TargetBuffer* from = s.owed_from;
        if (from != nullptr && !s.owed.empty()) {
            EnsureRecording();
            composer_->EndPass(*rec_);
            const bool held = from->released && from->holders->load(std::memory_order_acquire) > 0;
            const char* how = held             ? "from the published image the app still shows"
                              : from->released ? "from the image (no picture of it left)"
                                               : "from the image (not handed over yet)";
            if (from->released) shared_->AcquireAll(*rec_, *from->shared);
            CopyRects(*from->image, *t.image, s.owed);
            if (held) {
                shared_->ReleaseAll(*rec_, *from->shared);
                ++frame_.target_owed_taken_back;
            } else {
                from->released = false;  // (no picture of it lives: the renderer's again)
            }
            ++frame_.target_owed_taken;
            if (owed_logs_++ < 32) {
                MH4N_LOG(Info, kLog, "the screen target {:08x}: {} px owed taken back for {} (the expected clear did not come "
                                     "first), {}", t.paddr, Pixels(s.owed), why, how);
            }
            TraceEventF("owed taken {:08x} {} px{}", t.paddr, Pixels(s.owed), held ? " (held)" : "");
        }
        s.owed.clear();
        s.owed_from = nullptr;
        s.pretaken_of = nullptr;
        s.trust = 0;
    }

    // MH4N_OWED_CHECK=1 (a measurement aid, on a GPU where taking the image back from the app's side was seen to work):
    // one hidden touch screen snapshot in 30 taken at the hand-over (PretakeSnapshot), or read from the target's own image
    // after a hand-over that copied the box instead of owing it, is also taken the old way, from the published image, and
    // the two are compared once submitted (logged: the pixels that differ).
    static bool OwedCheck() {
        static const bool on = [] {
            const char* v = std::getenv("MH4N_OWED_CHECK");
            return v != nullptr && *v == '1';
        }();
        return on;
    }
    // (the check's other side: `blit` of the published image, taken back from the app's side the old way)
    void OwedCheckTake(const TargetBuffer& published, const rhi::ImageBlitRegion& blit, const ScaledScreen* entry) {
        const u32 w = blit.dst.width, h = blit.dst.height;
        if (owed_check_ == nullptr || owed_check_->Width() != w || owed_check_->Height() != h) {
            rhi::ImageDesc desc;
            desc.format = rhi::Format::Rgba8Unorm;
            desc.width = w;
            desc.height = h;
            desc.name = "owed check";
            if (owed_check_ != nullptr) retire_after_submit_.push_back(std::move(owed_check_));
            rhi::Result<std::shared_ptr<rhi::Image>> made = backend_->CreateImage(desc);
            owed_check_ = made.ok() ? *made : nullptr;
        }
        if (owed_check_ == nullptr) return;
        shared_->AcquireAll(*rec_, *published.shared);
        rec_->Blit(*published.image, *owed_check_, std::span<const rhi::ImageBlitRegion>(&blit, 1), /*linear=*/false);
        shared_->ReleaseAll(*rec_, *published.shared);
        owed_check_entry_ = entry;
    }
    void CompareOwedCheck(const ScaledScreen& entry) {
        if (owed_check_ == nullptr || entry.snapshot == nullptr) return;
        const u32 w = entry.snapshot->Width(), h = entry.snapshot->Height();
        rhi::Result<std::vector<u8>> a = backend_->ReadImage(*entry.snapshot, 0, 0, w, h);
        rhi::Result<std::vector<u8>> b = backend_->ReadImage(*owed_check_, 0, 0, w, h);
        if (!a.ok() || !b.ok() || a->size() != b->size()) {
            MH4N_LOG(Warning, kLog, "owed check: not read back ({} {})", a.ok() ? "" : a.error(), b.ok() ? "" : b.error());
            return;
        }
        u64 differ = 0;
        u32 row0 = h, row1 = 0;  // (the snapshot rows where they differ: its row r is the source's guest row first + h - 1 - r)
        for (size_t i = 0; i + 3 < a->size(); i += 4) {
            if (std::memcmp(a->data() + i, b->data() + i, 4) != 0) {
                ++differ;
                const u32 row = static_cast<u32>(i / 4 / w);
                row0 = std::min(row0, row);
                row1 = std::max(row1, row);
            }
        }
        ++owed_compared_;
        owed_differ_ += differ;
        MH4N_LOG(Info, kLog, "owed check {}: the snapshot of {}x{} as taken and from the published image: {} px differ{} "
                             "({} px in {} checks)", owed_compared_, w, h, differ,
                 differ != 0 ? fmt::format(" in its rows {}..{}", row0, row1) : std::string(), owed_differ_, owed_compared_);
    }

    // After a VBlank hand-over that owed nothing: would the box have been written by the next write first?
    static void WatchTrust(ScreenTarget& s, const rhi::Rect& area, const rhi::Rect* cleared) {
        if (Empty(s.watch)) return;
        if (cleared != nullptr && Contains(*cleared, s.watch)) {
            s.trust = std::min<u32>(s.trust + 1, 1000);
        } else if (Intersect(area, s.watch)) {
            s.trust = 0;
        } else {
            return;  // (elsewhere: still watching)
        }
        s.watch = {};
    }

    // Before `area` of a screen target's image is read: what it owes there taken back. `watch` false: the read does not
    // count against the trust in the next frame's first clear (the hidden touch screen's snapshot, which reads the
    // published picture's rows, owed or not).
    void ScreenRead(Target& t, const rhi::Rect& area, const char* why, bool watch = true) {
        if (t.screen == nullptr) return;
        ScreenTarget& s = *t.screen;
        if (watch) WatchTrust(s, area, nullptr);
        for (const rhi::Rect& o : s.owed) {
            if (Intersect(o, area)) {
                TakeOwed(t, why);
                return;
            }
        }
    }

    // Before `area` of a screen target's image is written (`cleared`: pixels written whatever was there): what it owes
    // there that is not cleared taken back (what is cleared is owed no more), and the image a picture holds replaced
    // (`keep_twin`: not by the twin, which the write samples).
    void ScreenWrite(Target& t, const rhi::Rect& area, const rhi::Rect* cleared, bool keep_twin, const char* why) {
        if (t.screen == nullptr) return;
        ScreenTarget& s = *t.screen;
        WatchTrust(s, area, cleared);
        if (!s.owed.empty()) {
            bool take = false;
            for (const rhi::Rect& o : s.owed) {
                if (const std::optional<rhi::Rect> part = Intersect(o, area)) {
                    take |= cleared == nullptr || !Contains(*cleared, *part);
                }
            }
            if (take) {
                TakeOwed(t, why);
            } else if (cleared != nullptr) {
                s.owed = Subtract(s.owed, *cleared);
                if (s.owed.empty()) {
                    s.owed_from = nullptr;
                    s.trust = std::min<u32>(s.trust + 1, 1000);
                    ++frame_.target_owed_cleared;
                }
            }
        }
        if (s.image->holders->load(std::memory_order_acquire) > 0) {
            Detach(t, &area, cleared, keep_twin);
        }
        s.box_from = nullptr;  // (the image differs from the one it was given the box from: OwedCheck)
    }

    // A screen target's picture, as the display takes it: if nothing has written the target since, its image is handed
    // over now; the hand-over is submitted, and the picture is ready after it.
    void HandOverTargetPicture(ScaledScreen& entry) {
        FlushHeldCpuDraw();
        const auto [t, b] = FindTargetBuffer(entry.picture->BufferId());
        if (b == nullptr) {
            return;
        }
        if (t != nullptr && t->screen->image == b) {
            handover_where_ = "at the VBlank";
            Detach(*t, nullptr, nullptr);
            handover_where_ = nullptr;
        }
        if (b->release_owed) {
            EnsureRecording();
            FlushRecording();
        }
        const u64 at = b->released_at->load(std::memory_order_acquire);
        if (at != TargetBuffer::kNotYet) {
            entry.ready = std::max(entry.ready, at);
            if (b->sync != nullptr && backend_->CompletedValue() < at) {
                entry.picture->SetSync(b->sync);  // (the hand-over's own: nothing submitted after it)
            }
        }
    }

    // The hand-overs owed, into the recording about to be submitted (FlushRecording); images let go of are dropped.
    void RecordScreenReleases() {
        if (!any_release_owed_ && orphan_buffers_.empty()) {
            return;
        }
        any_release_owed_ = false;
        auto release = [&](TargetBuffer& b) {
            if (!b.release_owed) return;
            shared_->ReleaseAll(*rec_, *b.shared);
            b.release_owed = false;
            b.released = true;
            releasing_.push_back(&b);
        };
        for (auto& t : targets_) {
            if (t->screen == nullptr) continue;
            for (const auto& b : t->screen->buffers) release(*b);
        }
        for (const auto& b : orphan_buffers_) release(*b);
    }

    static rhi::BindingSetDesc ScreenSetDesc() {
        rhi::BindingSetDesc desc;
        desc.slots = {{0, rhi::BindingType::SampledImage}, {1, rhi::BindingType::UniformBuffer}};
        return desc;
    }

    bool EnsureScreenPipeline() {
        if (screen_pipeline_ != nullptr) {
            return true;
        }
        if (screen_pipeline_failed_) {
            return false;
        }
        screen_pipeline_failed_ = true;
        std::string error;
        if (!shader::CompileGlslToSpirv(kScreenVertexGlsl, screen_vs_, error) ||
            !fragment::CompileFragmentGlslToSpirv(kScreenFragmentGlsl, screen_fs_, error)) {
            MH4N_LOG(Error, kLog, "the scaled screens' shaders do not compile: {}", error);
            return false;
        }
        rhi::GraphicsPipelineDesc desc;
        desc.vertex.spirv = screen_vs_;
        desc.fragment.spirv = screen_fs_;
        desc.topology = rhi::Topology::TriangleList;
        desc.color_format = backend_->ChooseFormat(rhi::Format::Rgba8Unorm, rhi::FormatUse::ColorTarget).format;
        desc.sets = {ScreenSetDesc()};
        desc.name = "scaled screen";
        rhi::Result<std::shared_ptr<rhi::Pipeline>> pipeline = backend_->CreateGraphicsPipeline(desc);
        if (!pipeline.ok()) {
            MH4N_LOG(Error, kLog, "the scaled screens' pipeline: {}", pipeline.error());
            return false;
        }
        screen_pipeline_ = *pipeline;
        screen_arena_ = std::make_unique<rhi::BindingArena>(backend_);
        screen_pipeline_failed_ = false;
        return true;
    }

    ScaledScreen* NoteUnscaledScreen(u32 input, const char* why, const Target* stale = nullptr) {
        if (ScaleLogLimit() != 0) {
            const std::string event = fmt::format("screen: a display transfer from {:08x} shown from guest memory at 1x ({}", input, why);
            ScaleLog(event + (stale != nullptr ? fmt::format(": target {}, {}; stale since f{})", Describe(*stale), stale->stale_why,
                                                             stale->stale_frame)
                                               : std::string(")")),
                     event.size());
        }
        if (screen_fallback_logs_ < 8) {
            ++screen_fallback_logs_;
            MH4N_LOG(Info, kLog, "a display transfer from 0x{:08x} ({}): its framebuffer is shown from guest memory", input,
                     why);
        }
        return nullptr;
    }

    // ---- asynchronous screens (--async-screens, scaled_screens.h) ----
    // A display transfer that reads a colour target the GPU has drawn since its last write-back (the frame's picture on
    // its way to a framebuffer) is done on the GPU with no wait: the picture that framebuffer shows, and a 1x copy of the
    // source rows it reads (DrawScaledScreen). The engine leaves its blit out and does the rest as always (registers,
    // the PPF interrupt); the source is not written back; the framebuffer's memory is owed (ScaledScreen::deferred) and
    // written by Materialize when something is about to read it. Any other transfer (linear input, scaling, a shifted
    // output, a source already in memory, an input or output that a deferred write overlaps) takes the CPU path:
    // false, with nothing done.
    bool TryAsyncTransfer(const GxCommand& command) {
        if (!renderer_ready_) {
            return false;
        }
        const u32* w = command.words;
        gx::TransferRegs regs;
        regs.input_address = GxPhysical(w[1]) >> 3;
        regs.output_address = GxPhysical(w[2]) >> 3;
        regs.input_size = w[3];
        regs.output_size = w[4];
        regs.flags = w[5];
        if (regs.IsTextureCopy() || regs.InputLinear() || regs.DontSwizzle() || regs.Scaling() != 0 ||
            (regs.FlipVertically() && regs.CropInputLines())) {
            return false;
        }
        const u32 sbpp = gx::BytesPerPixel(regs.InputFormat()), dbpp = gx::BytesPerPixel(regs.OutputFormat());
        if (sbpp == 0 || dbpp == 0) {
            return false;
        }
        const u32 in = regs.InputAddress(), out = regs.OutputAddress();
        const u64 in_end = u64{in} + ((u64{regs.InputWidth()} + 7) & ~u64{7}) * ((u64{regs.InputHeight()} + 7) & ~u64{7}) * sbpp + 4;
        const u64 out_end64 = u64{out} + u64{regs.OutputWidth()} * regs.OutputHeight() * dbpp;
        if (out_end64 > 0xFFFFFFFFull || (out < in_end && in < out_end64)) {
            return false;  // in place, or past the address space
        }
        const u32 out_end = static_cast<u32>(out_end64);
        bool dirty = false;
        for (const auto& t : targets_) {
            dirty |= !t->depth && t->gpu_dirty && t->Overlaps(in, in + 1);
        }
        if (!dirty) {
            return false;  // the source is in memory already: the CPU blit waits for nothing
        }
        for (const ScaledScreen& s : scaled_screens_) {
            if (s.deferred && s.address < in_end && in < s.address + s.bytes) {
                return false;  // it reads a framebuffer still owed: the CPU path makes that first
            }
        }
        // What it writes over: a deferred framebuffer it covers whole is dropped, one it covers in part made first; a target
        // there is written back first (a target written in part keeps the rest), as on the CPU path.
        const StaleWhy why(*this, "a display transfer (on the GPU)");
        GxRanges ranges;
        ranges.writes.emplace_back(out, out_end);
        const GxWrites writes{{{out, out_end}}, false};
        SettleDeferred(&ranges, &writes);
        {
            const CauseScope cause(*this, WritebackCause::Gx);
            for (auto& t : targets_) {
                // (only what was drawn there: MH4U's widescreen top framebuffer lies over the unused first rows of the
                // 1024-row scene depth buffer)
                if (DirtyOverlaps(*t, out, out_end)) {
                    WriteBack(*t);
                }
            }
        }
        ForgetScaledScreens(writes);
        ScaledScreen* entry = DrawScaledScreen(command, /*snapshot=*/true);
        if (entry == nullptr) {
            return false;
        }
        entry->deferred = true;
        entry->regs = regs;
        PaceGpu(entry->ready);
        skip_blit_ = true;
        gx_.Execute(command);
        skip_blit_ = false;
        // what it wrote, as far as the targets and the texture cache are concerned
        for (auto& t : targets_) {
            if (!t->Overlaps(out, out_end)) {
                continue;
            }
            if (t->depth) {
                t->stale = true;
            } else {
                MarkStale(*t, out, out_end);
            }
        }
        if (texture_cache_) {
            texture_cache_->OnGpuWrite(out, out_end - out);
        }
        ++frame_.async_transfers;
        return true;
    }

    // The pace of the GPU: a screen transfer is the end of a frame, and when more frames than the frames in flight (2;
    // MH4N_GPU_FRAMES=<n> another number, =auto 3 while the GPU is the limit: AdaptGpuFrames; two screens each) are still
    // on the GPU, the renderer waits for the oldest. Without it the game ran ahead of a
    // slow GPU and then stopped for it all at once (a Moto g06: 30 ms frames, then one of 200 ms); with it each frame
    // waits a little, at the GPU's own pace.
    // Memory for textures made ahead (MH4N_RESERVE_MB, default 16, 0: never): when the allocator's blocks have less than
    // that free, a thread of its own makes it take a new one (Backend::ReserveImageMemory). Taken while making a texture,
    // a new 32 MB block cost the Anbernic's renderer 15.7 ms, twice in the frame of the Arena's load.
    void ReserveTextureMemory() {
        static const u64 want = [] {
            const char* v = std::getenv("MH4N_RESERVE_MB");
            return (v != nullptr ? static_cast<u64>(std::max(0, std::atoi(v))) : u64{16}) << 20;
        }();
        if (want == 0 || ++reserve_checks_ % 30 != 0 || reserving_->load(std::memory_order_acquire)) {
            return;
        }
        if (backend_->FreeImageMemory() >= want) {
            return;
        }
        reserving_->store(true, std::memory_order_release);
        std::thread([backend = backend_, flag = reserving_, bytes = std::min<u64>(want, 12ull << 20)] {
            SetHostThreadName("mh4n-reserve");
            backend->ReserveImageMemory(bytes);
            flag->store(false, std::memory_order_release);
        }).detach();
    }

    void PaceGpu(u64 ready) {
        texture_spent_us_ = 0;  // (a screen's frame ends here: the texture budget starts again)
        ReserveTextureMemory();
        if (GpuSpans()) {
            pacing::g_gpu_total_ns.store(backend_->FrameGpuTimeTotalNs(), std::memory_order_relaxed);
        }
        // MH4N_GPU_FRAMES=<n>: that many; =auto: 2, or 3 while the GPU is the limit (AdaptGpuFrames); otherwise 2. (3 was
        // tried as the default: on the Anbernic at 3x one pair of Arena hunts said better, the next no better, and the Galaxy
        // at its 222 MHz cap was worse with it: 1% low 47.7 -> 39.5 FPS, intervals over 1.5 vsyncs 14.8 -> 19.1 %)
        static const u32 fixed = [] {
            const char* v = std::getenv("MH4N_GPU_FRAMES");
            if (v != nullptr && std::string_view(v) == "auto") return 0u;
            return v != nullptr ? static_cast<u32>(std::clamp(std::atoi(v), 1, 16)) : 2u;
        }();
        const u32 frames = fixed != 0 ? fixed : gpu_frames_now_;
        gpu_frames_.push_back(ready);
        bool waited = false;
        while (gpu_frames_.size() > 2u * frames) {
            const u64 oldest = gpu_frames_.front();
            gpu_frames_.pop_front();
            if (backend_->CompletedValue() < oldest) {
                const u64 start = NowUs();
                const auto wait_start = std::chrono::steady_clock::now();
                backend_->WaitIdle(oldest);
                frame_.pace_us += NowUs() - start;
                waited |= std::chrono::steady_clock::now() - wait_start > std::chrono::microseconds(500);
            }
        }
        if (fixed == 0) {
            AdaptGpuFrames(waited);
        }
    }

    // MH4N_GPU_FRAMES=auto: the frames the GPU may have in flight (PaceGpu): 2, or 3 while the GPU is the limit. With 3
    // the renderer has a frame of slack for a heavy scene's spikes instead of stopping for the GPU (the Anbernic at 3x in
    // one Arena hunt: intervals
    // over 1.5 vsyncs 1.03 -> 0.12 %, 1% low 30 -> 52.6 FPS), for a frame more of latency (+12 ms there), which a phone
    // whose GPU has time over gains nothing for. Up to 3 when the renderer waited for the GPU at one transfer in four of
    // the last 240 (a load's few waits do not count: the Moto at Low-end 60 had one in ten); back to 2 only when the GPU
    // was busy (its timestamps) under 30 % of the time over the same span: those spans are not the GPU's busy time
    // exactly (on the Galaxy at its 222 MHz cap, 99 % busy by kgsl, they said 53 %), and at 70 % both the Anbernic at 3x
    // and the Galaxy went back and forth.
    void AdaptGpuFrames(bool waited) {
        ++adapt_transfers_;
        adapt_waits_ += waited ? 1 : 0;
        if (adapt_transfers_ < 240) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        const u64 gpu = GpuSpans() ? backend_->FrameGpuTimeTotalNs() : 0;
        const double wall = std::chrono::duration<double, std::nano>(now - adapt_since_).count();
        const double share = gpu > adapt_gpu_ && wall > 0 ? static_cast<double>(gpu - adapt_gpu_) / wall : -1.0;
        if (adapt_gpu_ != 0) {  // (not the first span: its start is not known)
            if (gpu_frames_now_ == 2 && adapt_waits_ * 4 >= adapt_transfers_) {
                gpu_frames_now_ = 3;
                MH4N_LOG(Info, kLog, "the GPU is the limit ({} of {} transfers waited for it): 3 frames in flight",
                         adapt_waits_, adapt_transfers_);
            } else if (gpu_frames_now_ == 3 && share >= 0.0 && share < 0.30) {
                gpu_frames_now_ = 2;
                MH4N_LOG(Info, kLog, "the GPU has time over (busy {:.0f} %): 2 frames in flight", share * 100);
            }
        }
        adapt_transfers_ = adapt_waits_ = 0;
        adapt_since_ = now;
        adapt_gpu_ = gpu;
    }

    // Before a GX command: a deferred framebuffer it reads, or writes over in part, is made first; one it writes over
    // whole (by `precise`, what the engine writes) is dropped. Null ranges: the command's ranges are not known, every
    // deferred framebuffer is made.
    void SettleDeferred(const GxRanges* ranges, const GxWrites* precise) {
        for (ScaledScreen& s : scaled_screens_) {
            if (!s.deferred) {
                continue;
            }
            const u32 begin = s.address, end = s.address + s.bytes;
            if (ranges == nullptr || precise == nullptr || precise->all) {
                Materialize(s);
                continue;
            }
            bool read = false, touched = false, covered = false;
            for (const auto& [b, e] : ranges->reads) read |= b < end && begin < e;
            for (const auto& [b, e] : ranges->writes) touched |= b < end && begin < e;
            for (const auto& [b, e] : precise->ranges) covered |= b <= begin && end <= e;
            if (read || (touched && !covered)) {
                Materialize(s);
            } else if (covered) {
                s.deferred = false;  // its bytes are replaced whole
            }
        }
    }

    // The deferred framebuffers in [begin, end) are made (something is about to read guest memory there).
    void MaterializeOverlapping(u32 begin, u32 end, bool keep_cpu_writes = false) {
        for (ScaledScreen& s : scaled_screens_) {
            if (s.deferred && s.address < end && begin < s.address + s.bytes) {
                Materialize(s, keep_cpu_writes ? begin : 0, keep_cpu_writes ? end : 0);
            }
        }
    }

    // Writes a deferred framebuffer: the source rows from the snapshot (read back: this waits for the GPU) in the
    // input's layout, the rest of the blitter's window from guest memory as it is, and the engine's own blit over them.
    // [keep_begin, keep_end): bytes the CPU wrote over the framebuffer after the transfer (a flush says so), which stay.
    void Materialize(ScaledScreen& s, u32 keep_begin = 0, u32 keep_end = 0) {
        if (!s.deferred) {
            return;
        }
        s.deferred = false;
        TraceEventF("materialize {:08x}", s.address);
        ++frame_.async_materialized;
        const u64 start = NowUs();
        const gx::TransferRegs& regs = s.regs;
        const u32 in = regs.InputAddress();
        const u32 sbpp = gx::BytesPerPixel(regs.InputFormat());
        const u64 window = ((u64{regs.InputWidth()} + 7) & ~u64{7}) * ((u64{regs.InputHeight()} + 7) & ~u64{7}) * sbpp + 4;
        std::vector<u8> input(window, 0);
        const std::span<u8> current = gx::Window(host_, in, window);
        std::memcpy(input.data(), current.data(), current.size());
        const u64 snapshot_bytes = u64{regs.InputWidth()} * s.snapshot_rows * ColorBytesPerPixel(s.source_format);
        if (s.snapshot != nullptr && snapshot_bytes <= window) {
            FlushRecording();
            const rhi::Result<void> r =
                backend_->ReadbackToGuest(*s.snapshot, rhi::Rect{0, 0, regs.InputWidth(), s.snapshot_rows},
                                          ToPicaFormat(s.source_format), std::span<u8>(input.data(), snapshot_bytes), 1);
            if (!r.ok()) {
                MH4N_LOG(Error, kLog, "an async screen's source rows: {}", r.error());
            }
        }
        const u32 keep_lo = std::max(keep_begin, s.address), keep_hi = std::min(keep_end, s.address + s.bytes);
        std::vector<u8> kept;
        u8* keep_at = keep_lo < keep_hi ? host_.PhysicalPointer(keep_lo, keep_hi - keep_lo) : nullptr;
        if (keep_at != nullptr) {
            kept.assign(keep_at, keep_at + (keep_hi - keep_lo));
        }
        InputOverlay overlay(host_, in, input);
        gx::DisplayTransfer(overlay, regs);
        if (keep_at != nullptr) {
            std::memcpy(keep_at, kept.data(), kept.size());
        }
        if (texture_cache_) {
            texture_cache_->OnGpuWrite(s.address, s.bytes);
        }
        frame_.materialize_us += NowUs() - start;
    }

    // MH4N_SCALE_LOG=<n> (a measurement aid: where a picture the GPU drew at the internal scale goes through guest memory at
    // 1x, which looks blurred or blocky at a higher scale): the first n events of four kinds, each logged the first time
    // and again when it comes back after 60 frames without it, with the game's frame:
    //  - "gx": a fill, transfer, copy or DMA that reads rows of a scaled colour target (it reads their 1x write-back);
    //  - "screen": a display transfer whose framebuffer is shown from guest memory (the 1x picture, enlarged by the app);
    //  - "texture": a texture decoded from guest memory under a scaled colour target (not that target itself);
    //  - "load": rows of a scaled colour target loaded from guest memory (the 1x picture, enlarged nearest).
    static u32 ScaleLogLimit() {
        static const u32 limit = [] {
            const char* v = std::getenv("MH4N_SCALE_LOG");
            return v != nullptr ? static_cast<u32>(std::max(0, std::atoi(v))) : 0u;
        }();
        return limit;
    }
    // (`what` up to `key` is the event: what follows, such as when its target went stale, is not)
    void ScaleLog(const std::string& what, size_t key = std::string::npos) {
        if (scale_logged_ >= ScaleLogLimit()) {
            return;
        }
        const u32 frame = pacing::g_game_frame.load(std::memory_order_relaxed);
        auto [it, fresh] = scale_seen_.try_emplace(what.substr(0, key), frame);
        const bool again = !fresh && frame - it->second > 60;
        it->second = frame;
        if (fresh || again) {
            ++scale_logged_;
            MH4N_LOG(Info, kLog, "scale f{}: {}", frame, what);
        }
    }
    // What marks targets stale while it is in scope (MarkStale, MarkAllStale: Target::stale_why).
    struct StaleWhy {
        StaleWhy(Impl& impl, const char* why) : impl_(impl), saved_(impl.stale_why_) { impl_.stale_why_ = why; }
        ~StaleWhy() { impl_.stale_why_ = saved_; }
        StaleWhy(const StaleWhy&) = delete;
        StaleWhy& operator=(const StaleWhy&) = delete;
        Impl& impl_;
        const char* saved_;
    };
    void NoteStale(Target& t) {
        t.stale_why = stale_why_;
        t.stale_frame = pacing::g_game_frame.load(std::memory_order_relaxed);
    }
    static std::string Describe(const Target& t) {
        return fmt::format("{:08x} {}x{} fmt {}", t.paddr, t.width, t.height, t.format);
    }
    static const char* GxName(u32 id) {
        switch (id) {
        case 0: return "a DMA";
        case 2: return "a fill";
        case 3: return "a display transfer";
        case 4: return "a texture copy";
        default: return "a GX command";
        }
    }
    // A GX command that reads rows of a scaled colour target, before it runs (MH4N_SCALE_LOG): `screen`, it also made the
    // picture its framebuffer shows at the scale (DrawScaledScreen).
    void ScaleLogGx(const GxCommand& command, const GxRanges* ranges, bool screen) {
        if (ranges == nullptr) {
            ScaleLog(fmt::format("gx {} with addresses that are not physical: every target written back", GxName(command.words[0] & 0xFF)));
            return;
        }
        const u32* w = command.words;
        for (const auto& [begin, end] : ranges->reads) {
            for (const auto& t : targets_) {
                if (t->depth || t->shadow || t->scale <= 1 || t->stale || !t->Overlaps(begin, end)) {
                    continue;
                }
                const u32 tile_row = TileRowBytes(*t);
                const u32 lo = std::max(begin, t->paddr), hi = std::min<u64>(end, u64{t->paddr} + t->bytes);
                std::string to;
                const u32 out = GxPhysical(w[2]);
                for (const auto& o : targets_) {
                    if (o.get() != t.get() && o->Overlaps(out, out + 1)) {
                        to = fmt::format(" into target {}", Describe(*o));
                        break;
                    }
                }
                std::string detail;
                if ((w[0] & 0xFF) == 3) {
                    detail = fmt::format(" in {}x{} out {}x{} flags {:08x}", w[3] & 0xFFFF, w[3] >> 16, w[4] & 0xFFFF, w[4] >> 16, w[5]);
                } else if ((w[0] & 0xFF) == 4) {
                    detail = fmt::format(" size {:x} gaps {:08x} {:08x} flags {:08x}", w[3], w[4], w[5], w[6]);
                } else if ((w[0] & 0xFF) == 0) {
                    detail = fmt::format(" size {:x}", w[3]);
                }
                ScaleLog(fmt::format("gx {} reads target {} rows {}-{} at 1x{}: {:08x} -> {:08x}{}{}", GxName(w[0] & 0xFF),
                                     Describe(*t), (lo - t->paddr) / tile_row * 8,
                                     std::min(t->height, (hi - t->paddr + tile_row - 1) / tile_row * 8), detail,
                                     GxPhysical(w[1]), out, to, screen ? " (its screen picture made at the scale)" : ""));
            }
        }
    }
    // A texture decoded from guest memory (BindUnit, MH4N_SCALE_LOG): the scaled targets under its memory (a depth target:
    // a draw that reads the depth as a texture).
    void ScaleLogTexture(const texture::TextureRef& ref) {
        const u32 bytes = static_cast<u32>(texture::EncodedBytes(ref.format, ref.width, ref.height));
        const u32 begin = ref.paddr, end = static_cast<u32>(std::min<u64>(u64{ref.paddr} + bytes, 0xFFFFFFFFull));
        for (const auto& t : targets_) {
            if (t->shadow || t->scale <= 1 || !t->Overlaps(begin, end)) {
                continue;
            }
            std::string why;
            if (t->depth) {
                why = "a depth target";
            } else if (t->paddr != ref.paddr) {
                why = fmt::format("at {} bytes into it", static_cast<s64>(ref.paddr) - static_cast<s64>(t->paddr));
            } else if (t->width != ref.width || t->height != ref.height) {
                why = "another size";
            } else if (static_cast<u32>(ref.format) != t->format) {
                why = "another format";
            } else if (t->stale) {
                why = fmt::format("the target stale ({})", t->stale_why);
            } else {
                why = "the same texture (not taken as the target)";
            }
            ScaleLog(fmt::format("texture {:08x} {}x{} fmt {} unit {} decoded from memory at 1x under target {}: {}", ref.paddr,
                                 ref.width, ref.height, static_cast<u32>(ref.format), ref.unit, Describe(*t), why));
        }
    }

    // Which write-backs the counters put where (MH4N_GPU_COUNTERS: writebacks_gx, _overlap, _texture, _cpu).
    enum class WritebackCause : u32 { Other = 0, Gx = 1, Overlap = 2, Texture = 3, Cpu = 4 };
    struct CauseScope {
        CauseScope(Impl& impl, WritebackCause cause) : impl_(impl), saved_(impl.writeback_cause_) {
            impl_.writeback_cause_ = cause;
        }
        ~CauseScope() { impl_.writeback_cause_ = saved_; }
        CauseScope(const CauseScope&) = delete;
        CauseScope& operator=(const CauseScope&) = delete;
        Impl& impl_;
        WritebackCause saved_;
    };

    // A display transfer, before the engine runs it: when its input is a render target held here, the picture its output
    // will show, drawn at the internal scale from the target's image and copied for the host (scaled_screens.h). With
    // `snapshot` (an asynchronous transfer) also the source rows the transfer reads, at 1x, for Materialize. Its entry;
    // null when it draws nothing.
    ScaledScreen* DrawScaledScreen(const GxCommand& command, bool snapshot = false) {
        if (!renderer_ready_) {
            return nullptr;
        }
        const u32* w = command.words;
        gx::TransferRegs regs;
        regs.input_address = GxPhysical(w[1]) >> 3;
        regs.output_address = GxPhysical(w[2]) >> 3;
        regs.input_size = w[3];
        regs.output_size = w[4];
        regs.flags = w[5];
        const u32 input = regs.InputAddress();
        const u32 width = regs.OutputWidth(), height = regs.OutputHeight();
        const u32 in_format = regs.InputFormat(), out_format = regs.OutputFormat();
        if (regs.IsTextureCopy() || regs.InputLinear() || regs.DontSwizzle()) {
            return NoteUnscaledScreen(input, "not a tiled-to-linear transfer");
        }
        if (regs.Scaling() != 0 || (regs.FlipVertically() && regs.CropInputLines())) {
            return NoteUnscaledScreen(input, "a scaled or shifted transfer");
        }
        if (width == 0 || height == 0 || width > regs.InputWidth() || in_format > 4 || out_format > 4) {
            return NoteUnscaledScreen(input, "its sizes or formats");
        }
        // The colour target it reads: the input's width and format, the input at the start of a row of tiles inside it.
        Target* source = nullptr;
        u32 first_row = 0;
        bool stale = false;
        const Target* stale_target = nullptr;
        for (auto& t : targets_) {
            if (t->depth || t->scale != scale_ || t->width != regs.InputWidth() || ColorToTransferFormat(t->format) != in_format ||
                input < t->paddr || input >= t->paddr + t->bytes) {
                continue;
            }
            const u32 tile_row_bytes = t->width * 8 * ColorBytesPerPixel(t->format);
            if ((input - t->paddr) % tile_row_bytes != 0) {
                continue;
            }
            const u32 row = (input - t->paddr) / tile_row_bytes * 8;
            if (row + height > t->height) {
                continue;
            }
            if (t->stale) {
                stale = true;
                stale_target = t.get();
                continue;
            }
            if (source == nullptr || t->drawn > source->drawn) {
                source = t.get();
                first_row = row;
            }
        }
        if (source == nullptr) {
            return NoteUnscaledScreen(input, stale ? "its render target is older than guest memory" : "no render target holds it",
                                      stale_target);
        }
        // the rows a snapshot holds: those the blitter's window has in the source, in whole rows of tiles
        const u32 snapshot_rows = std::min<u32>((source->height - first_row) / 8 * 8, (regs.InputHeight() + 7) / 8 * 8);
        if (snapshot && snapshot_rows < height) {
            return nullptr;
        }
        if (!EnsureScreenPipeline()) {
            return nullptr;
        }
        // rows another target or the GX engine wrote since, of those the transfer reads
        Refresh(*source, first_row, first_row + std::max(snapshot_rows, (height + 7) / 8 * 8));

        // The framebuffer's entry (one per output address: the two buffers of each screen).
        const u32 output = regs.OutputAddress();
        ScaledScreen* entry = nullptr;
        for (ScaledScreen& s : scaled_screens_) {
            if (s.address == output) {
                entry = &s;
                break;
            }
        }
        if (entry == nullptr) {
            constexpr size_t kMaxScreens = 8;
            if (scaled_screens_.size() >= kMaxScreens) {
                const auto unused = std::find_if(scaled_screens_.begin(), scaled_screens_.end(),
                                                 [](const ScaledScreen& s) { return !s.valid && !s.deferred; });
                entry = unused != scaled_screens_.end() ? &*unused : &scaled_screens_.front();
                Materialize(*entry);  // its framebuffer is owed its bytes before the entry is someone else's
                entry->valid = false;
                entry->picture.reset();
            } else {
                entry = &scaled_screens_.emplace_back();
            }
            entry->address = output;
        }
        const u32 n = scale_;
        const u32 image_width = height * n;  // upright: the framebuffer's rows across
        const u32 image_height = width * n;
        // Shared screens: the picture goes into a free buffer of the pool, and nothing is read back; else into the
        // entry's own image, copied for the host.
        // The hidden touch screen's picture is not drawn: nobody shows it (DisplayMemGpu does not build that screen while
        // hidden) and its draws were left out (touch_phase_). Its snapshot is still taken, for its framebuffer's memory.
        // (A 960x720 picture a frame on the Anbernic at 3x: 0.8-0.9 ms of its GPU.)
        const bool hidden = touch_forced_hidden_ || (touch_skip_enabled_.load(std::memory_order_relaxed) &&
                                                     !touch_shown_.load(std::memory_order_relaxed));
        const bool picture = !(hidden && width == 240 && height == 320 && transfer_alternations_ >= 4);
        // A screen target's image may owe pixels (gpu3: handed over at the top screen's transfer, or a VBlank, before
        // this one): the hidden touch screen's snapshot, its only read then, is of the published picture (all of it),
        // taken at the hand-over (PretakeSnapshot: the rows read here the last time) and adopted; other rows are taken
        // from the published image, acquired from the app's side (OwingAllowed's GPUs, the only ones that owe). Anything
        // else takes the owed pixels back.
        const rhi::Rect read_rows = RowsRect(*source, first_row, first_row + std::max(snapshot_rows, (height + 7) / 8 * 8));
        if (!picture && snapshot && source->screen != nullptr) {
            source->screen->snap_first = first_row;  // (what PretakeSnapshot takes the next time)
            source->screen->snap_rows = snapshot_rows;
        }
        TargetBuffer* read_from = nullptr;
        if (!picture && snapshot && source->screen != nullptr && source->screen->owed_from != nullptr &&
            std::any_of(source->screen->owed.begin(), source->screen->owed.end(),
                        [&](const rhi::Rect& o) { return Intersect(o, read_rows).has_value(); })) {
            read_from = source->screen->owed_from;
            ++frame_.target_owed_read;
        } else {
            // (the hidden touch screen's snapshot reads what a hand-over that owes would have read from the published
            // picture: a GPU that owes nothing, OwingAllowed, copied the box instead, and the trust holds as it does there)
            ScreenRead(*source, read_rows, "a display transfer", /*watch=*/picture || !snapshot);
        }
        // gpu3: the top screen's picture is the target's own image where it can be (no upright copy; ScreenTarget).
        // MH4N_TARGET_SCREENS_CHECK=1 draws the copy too, to go with it.
        TargetBuffer* const direct = picture ? TargetPicture(*source, width, height, in_format, out_format) : nullptr;
        SharedSlot* const slot =
            picture && (direct == nullptr || TargetScreensCheck()) ? TakeSharedSlot(image_width, image_height) : nullptr;
        const bool upright = picture && (direct == nullptr || slot != nullptr);
        if (upright && slot == nullptr &&
            (entry->image == nullptr || entry->image->Width() != image_width || entry->image->Height() != image_height)) {
            if (entry->image != nullptr) {
                backend_->Retire(entry->image, last_submit_);
                backend_->Retire(entry->readback, last_submit_);
            }
            entry->image.reset();
            entry->readback.reset();
            entry->valid = false;
            rhi::ImageDesc desc;
            desc.format = rhi::Format::Rgba8Unorm;
            desc.width = image_width;
            desc.height = image_height;
            desc.name = "scaled screen";
            rhi::Result<std::shared_ptr<rhi::Image>> image = backend_->CreateImage(desc);
            rhi::BufferDesc buffer;
            buffer.size = u64{image_width} * image_height * 4;
            buffer.heap = rhi::Heap::Readback;
            buffer.usage = rhi::BufferUsage::Transfer;
            buffer.name = "scaled screen readback";
            rhi::Result<std::shared_ptr<rhi::Buffer>> readback = backend_->CreateBuffer(buffer);
            if (!image.ok() || !readback.ok()) {
                MH4N_LOG(Error, kLog, "a scaled screen {}x{}: {} {}", image_width, image_height, image.error(), readback.error());
                return nullptr;
            }
            entry->image = *image;
            entry->readback = *readback;
        }
        entry->format = out_format;
        entry->width = width;
        entry->height = height;
        entry->bytes = width * height * gx::BytesPerPixel(out_format);

        // No pass may be open on the source (the set's barrier): start from an empty recording.
        FlushRecording();
        EnsureRecording();
        composer_->EndPass(*rec_);
        TraceEventF("screen {:08x} {}x{} -> {}x{}{}", source->paddr, source->width, source->height, image_width,
                               image_height, direct != nullptr ? " (the target's image itself)" : "");
        if (upright) {
            ScreenParams params{};
            params.map[0] = static_cast<s32>(n);
            params.map[1] = static_cast<s32>(width * n);
            params.map[2] = static_cast<s32>(height * n);
            params.map[3] = regs.FlipVertically() ? 1 : 0;
            params.first[0] = static_cast<s32>(source->height * n) - 1 - static_cast<s32>(first_row * n);
            const std::array<u32, 3> bits_in = TransferBits(in_format);
            const std::array<u32, 3> bits_out = TransferBits(out_format);
            for (int c = 0; c < 3; ++c) {
                params.bits_in[c] = bits_in[c];
                params.bits_out[c] = bits_out[c];
            }
            const u64 alignment = std::max<u64>(backend_->Caps().uniform_buffer_offset_alignment, 256);
            rhi::Result<rhi::RingAllocation> alloc = vs_ring_->Map(sizeof(ScreenParams), alignment);
            if (!alloc.ok()) {
                MH4N_LOG(Error, kLog, "a scaled screen's parameters: {}", alloc.error());
                return nullptr;
            }
            std::memcpy(alloc->data.data(), &params, sizeof(params));
            vs_ring_->GetBuffer()->Flush(alloc->offset, sizeof(params));
            if (!vs_ring_->Commit(sizeof(params)).ok()) {
                return nullptr;
            }
            rhi::Result<rhi::BindingSet*> set = screen_arena_->Acquire(ScreenSetDesc());
            if (!set.ok()) {
                MH4N_LOG(Error, kLog, "a scaled screen's bindings: {}", set.error());
                return nullptr;
            }
            rhi::Result<void> bound = (*set)->SetSampledImage(0, *source->image, rhi::ImageView::Sampled);
            if (bound.ok()) {
                bound = (*set)->SetUniformBuffer(1, *vs_ring_->GetBuffer(), alloc->offset, sizeof(ScreenParams));
            }
            if (!bound.ok()) {
                MH4N_LOG(Error, kLog, "a scaled screen's bindings: {}", bound.error());
                return nullptr;
            }
            if (slot != nullptr && slot->released) {
                shared_->Acquire(*rec_, *slot->image);  // (the app's reads of it are over: it let go of its picture)
            }
            rhi::Image& target = slot != nullptr ? slot->image->GetImage() : *entry->image;
            rhi::TargetPassDesc pass;
            pass.color = &target;
            pass.color_load = rhi::LoadOp::Clear;
            pass.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
            pass.area = rhi::Rect{0, 0, image_width, image_height};
            rec_->BeginTargetPass(pass);
            rec_->SetPipeline(*screen_pipeline_);
            rec_->Bind(**set, 0);
            rhi::Viewport viewport;
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(image_width);
            viewport.height = static_cast<float>(image_height);
            viewport.min_depth = 0.0f;
            viewport.max_depth = 1.0f;
            rec_->SetViewport(viewport);
            rec_->SetScissor(rhi::Rect{0, 0, image_width, image_height});
            rec_->Draw(3);
            rec_->EndPass();
            if (slot != nullptr) {
                shared_->Release(*rec_, *slot->image);
                slot->released = true;
            } else {
                rhi::BufferImageRegion region;
                region.image_rect = rhi::Rect{0, 0, image_width, image_height};
                region.aspect = rhi::ImageAspect::Color;
                rec_->CopyImageToBuffer(*entry->image, *entry->readback, std::span<const rhi::BufferImageRegion>(&region, 1));
            }
        }
        if (snapshot) {
            // The source rows the transfer reads, at 1x (a nearest blit: the sample a write-back takes), in a target's row
            // order: guest rows [first_row, first_row + snapshot_rows) are the source image's rows
            // [(height - first_row - snapshot_rows) N, (height - first_row) N).
            rhi::ImageBlitRegion blit;
            blit.src = rhi::Rect{0, static_cast<s32>((source->height - first_row - snapshot_rows) * n), source->width * n,
                                 snapshot_rows * n};
            blit.dst = rhi::Rect{0, 0, source->width, snapshot_rows};
            ScreenTarget* const st = source->screen.get();
            const bool adopt = read_from != nullptr && st->pretaken != nullptr && st->pretaken_of == read_from &&
                               st->pretaken_first == first_row && st->pretaken_rows == snapshot_rows;
            if (adopt) {
                // (taken at the hand-over, the same blit of the same image: the entry's image becomes the next one taken
                // ahead)
                if (entry->snapshot != nullptr && entry->snapshot->Width() == source->width &&
                    entry->snapshot->Height() == snapshot_rows) {
                    std::swap(entry->snapshot, st->pretaken);
                } else {
                    if (entry->snapshot != nullptr) {
                        backend_->Retire(entry->snapshot, last_submit_);
                    }
                    entry->snapshot = std::move(st->pretaken);
                }
                st->pretaken_of = nullptr;
                ++frame_.target_owed_pretaken;
                if (OwedCheck() && read_from->released && owed_checks_++ % 30 == 0) {
                    OwedCheckTake(*read_from, blit, entry);
                }
            } else {
                if (entry->snapshot == nullptr || entry->snapshot->Width() != source->width ||
                    entry->snapshot->Height() != snapshot_rows) {
                    if (entry->snapshot != nullptr) {
                        backend_->Retire(entry->snapshot, last_submit_);
                    }
                    rhi::ImageDesc desc;
                    desc.format = rhi::Format::Rgba8Unorm;
                    desc.width = source->width;
                    desc.height = snapshot_rows;
                    desc.name = "async screen source";
                    rhi::Result<std::shared_ptr<rhi::Image>> made = backend_->CreateImage(desc);
                    if (!made.ok()) {
                        MH4N_LOG(Error, kLog, "an async screen's source copy: {}", made.error());
                        entry->snapshot.reset();
                        FlushRecording();
                        return nullptr;
                    }
                    entry->snapshot = *made;
                }
                // (the image is the renderer's: the target's own, or a published one not yet handed over; else the
                // published image taken back from the app's side for the blit: rows not taken ahead, or the A/B's
                // "pretake" phase, as before 4u-native #31)
                const bool back = read_from != nullptr && read_from->released;
                if (back) {
                    shared_->AcquireAll(*rec_, *read_from->shared);
                    ++frame_.target_owed_taken_back;
                }
                rec_->Blit(read_from != nullptr ? *read_from->image : *source->image, *entry->snapshot,
                           std::span<const rhi::ImageBlitRegion>(&blit, 1), /*linear=*/false);
                if (back) shared_->ReleaseAll(*rec_, *read_from->shared);
                // (MH4N_OWED_CHECK on a hand-over that copied the box instead of owing it: the snapshot of the target's
                // own image against the published one it was copied from)
                if (OwedCheck() && read_from == nullptr && !picture && st != nullptr && st->box_from != nullptr &&
                    st->box_from->released && st->box_from->holders->load(std::memory_order_acquire) > 0 &&
                    owed_checks_++ % 30 == 0) {
                    OwedCheckTake(*st->box_from, blit, entry);
                }
            }
            entry->snapshot_rows = snapshot_rows;
            entry->source_format = source->format;
        }
        if (direct == nullptr && source->screen != nullptr &&
            source->screen->image->holders->load(std::memory_order_acquire) > 0) {
            // A transfer that reads a screen target whose picture was published (gpu3: the hidden touch screen's, right
            // after the top screen's): what read the published image is done, and nothing has written the target since;
            // it is handed over now, with this submission, rather than with the next frame's first pass (that pass's
            // work would come before the picture: on the Galaxy at 60, one present in five came a vsync late).
            handover_where_ = "at a transfer";
            Detach(*source, nullptr, nullptr);
            handover_where_ = nullptr;
        } else if (direct != nullptr && source->screen->trust >= 2 && !Empty(source->screen->clear_box) && EagerHandOver()) {
            // The picture's own transfer hands it over at once, once the next frame's first clear can be counted on: the
            // picture is ready with this submission, as the upright copy was (handed over at the next transfer, a GPU
            // behind by a frame showed one picture in fourteen a vsync late on the Anbernic at 4x). What the hidden
            // touch screen's snapshot reads then comes from the published image (read_from).
            handover_where_ = "at its transfer";
            Detach(*source, nullptr, nullptr);
            handover_where_ = nullptr;
        }
        dynamic_applied_ = false;  // another pipeline was bound: the PICA draws' dynamic state is set again
        if (GpuSpans() && frame_marked_) {
            // the GPU's time of the work up to this screen: from the first recording after the last screen (its Begin,
            // EnsureRecording) to here
            rec_->MarkFrame(rhi::FrameMark::End);
            frame_marked_ = false;
        }
        FlushRecording();
        entry->ready = last_submit_;
        entry->valid = picture;
        if (owed_check_entry_ == entry) {
            owed_check_entry_ = nullptr;
            CompareOwedCheck(*entry);
        }
        std::shared_ptr<const SharedScreenPicture> upright_picture;
        if (slot != nullptr) {
            upright_picture = std::make_shared<const SharedScreenPicture>(
                slot->busy, slot->image->BufferRef(), slot->image->Id(), image_width, image_height, backend_,
                std::make_shared<std::atomic<u64>>(entry->ready));
        }
        if (direct != nullptr) {
            // the target's image, as it is (handed to the app before it is written again: Detach), with the mapping the
            // upright copy's shader takes (kScreenFragmentGlsl)
            present::TargetLayout layout;
            layout.scale = n;
            layout.row_pixels = width * n;
            layout.rows = height * n;
            layout.flip = regs.FlipVertically();
            layout.first = static_cast<s32>(source->height * n) - 1 - static_cast<s32>(first_row * n);
            for (int c = 0; c < 3; ++c) {
                layout.bits_in[c] = static_cast<u8>(TransferBits(in_format)[c]);
                layout.bits_out[c] = static_cast<u8>(TransferBits(out_format)[c]);
            }
            auto made = std::make_shared<SharedScreenPicture>(direct->holders, direct->shared->BufferRef(),
                                                              direct->shared->Id(), direct->shared->Width(),
                                                              direct->shared->Height(), backend_, direct->released_at, layout);
            made->SetReference(upright_picture);
            entry->picture = std::move(made);
            HoldIncomplete(*entry, image_width, image_height);
        } else if (slot != nullptr) {
            entry->picture = std::move(upright_picture);
            HoldIncomplete(*entry, image_width, image_height);
        } else {
            entry->picture.reset();
        }
        return entry;
    }

    // ---- textures (N2-23) ----
    // Which texture units the draw reads: a TEV stage that is not a pass-through, or lighting's bump map or shadow map.
    std::array<bool, 3> UnitsRead(const fragment::FragmentKey& key) const {
        std::array<bool, 3> used{fragment::TevUsesSource(key, 3), fragment::TevUsesSource(key, 4), fragment::TevUsesSource(key, 5)};
        if (key.lighting.enable() != 0) {
            if (key.lighting.bump_mode() != fragment::regenum::LightingBumpMode::None && key.lighting.bump_selector() < 3) {
                used[key.lighting.bump_selector()] = true;
            }
            if (key.lighting.enable_shadow() != 0 && key.lighting.shadow_selector() < 3) {
                used[key.lighting.shadow_selector()] = true;
            }
        }
        return used;
    }

    // The 1x1 images a unit holds when it has nothing to show (Azahar binds a null surface to a unit that is off; the software
    // renderer gives transparent black for it and opaque black for an enabled unit with address 0).
    void EnsureNullTextures() {
        if (null_2d_ == nullptr) {
            rhi::ImageDesc desc;
            desc.format = rhi::Format::Rgba8Unorm;
            desc.width = desc.height = 1;
            desc.name = "null texture";
            auto a = backend_->CreateImage(desc);
            desc.name = "null opaque-black texture";
            auto b = backend_->CreateImage(desc);
            desc.name = "null cube texture";
            desc.cube = true;
            desc.layers = 6;
            auto c = backend_->CreateImage(desc);
            if (!a.ok() || !b.ok() || !c.ok()) {
                MH4N_LOG(Error, kLog, "null textures: {} {} {}", a.error(), b.error(), c.error());
                return;
            }
            null_2d_ = *a;
            null_black_2d_ = *b;
            null_cube_ = *c;
            null_cleared_ = false;
        }
        if (!null_cleared_) {
            EnsureRecording();
            composer_->EndPass(*rec_);
            rhi::ClearValue clear;
            rec_->ClearImage(*null_2d_, clear);
            rec_->ClearImage(*null_cube_, clear, 0, 0);
            for (u32 layer = 1; layer < 6; ++layer) {
                rec_->ClearImage(*null_cube_, clear, 0, layer);
            }
            clear.color = {0.0f, 0.0f, 0.0f, 1.0f};
            rec_->ClearImage(*null_black_2d_, clear);
            null_cleared_ = true;
        }
    }

    // Releases images the current recording does not use when the budget is exceeded, the least recently bound first, down
    // to three quarters of it (called when a recording begins, with everything earlier submitted: the GPU may still be reading
    // them, so the backend keeps them until it has passed that). Every one the current recording does not use went at once
    // before, and the next frames made and uploaded them all again. After an image could not be made (texture_memory_short_)
    // every one not used in the last 120 pictures goes, or every one not used in this recording if that frees nothing.
    void TrimTextures() {
        if (binding_textures_) {  // not while a draw is still collecting its images
            return;
        }
        if (texture_memory_short_) {
            texture_memory_short_ = false;
            const u64 before = texture_bytes_;
            const size_t images = texture_images_.size();
            ReleaseTextures(0, TextureFrame() > 120 ? TextureFrame() - 120 : 0);
            if (texture_bytes_ == before) {
                ReleaseTextures(0, ~u64{0});
            }
            if (++texture_memory_short_told_ <= 10) {  // (the first few: a memory that stays short gives one each recording)
                const rhi::Backend::MemoryUse use = backend_->Memory();
                MH4N_LOG(Warning, kLog, "texture memory ran out: {} of {} images let go ({} -> {} MB); the allocator's blocks {} "
                         "MB ({} MB in use), the system's available memory {} MB",
                         images - texture_images_.size(), images, before >> 20, texture_bytes_ >> 20, use.block_bytes >> 20,
                         use.allocation_bytes >> 20, AvailableMemoryKb() / 1024);
            }
            return;
        }
        if (texture_bytes_ <= TextureBudgetBytes()) {
            return;
        }
        ReleaseTextures(TextureBudgetBytes() / 4 * 3, ~u64{0});
    }

    // Releases the images not bound by the current recording and last bound before frame `before`, the least recently bound
    // first, until the images hold at most `down_to` bytes.
    void ReleaseTextures(u64 down_to, u64 before) {
        std::vector<std::pair<u64, std::map<TexImageKey, TexImage>::iterator>> unused;
        for (auto it = texture_images_.begin(); it != texture_images_.end(); ++it) {
            if (it->second.stamp != recording_serial_ && it->second.used_frame < before) {
                unused.emplace_back(it->second.stamp, it);
            }
        }
        std::sort(unused.begin(), unused.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (auto& [stamp, it] : unused) {
            if (texture_bytes_ <= down_to) {
                break;
            }
            texture_bytes_ -= it->second.bytes;
            backend_->Retire(it->second.image, last_submit_);
            texture_images_.erase(it);
            ++textures_released_;
        }
    }

    u64 TextureFrame() const {
        return texture_cache_ != nullptr ? texture_cache_->frame() : 0;
    }

    // At each screen picture (two a game frame, one per screen): every 600 pictures the cache's entries no draw used in the
    // last 36000 go (five to ten minutes: an entry is small once its pixels went, and one used again after that is decoded
    // and uploaded again, a stutter for a big one), and a changing texture's pixels once it has not changed in 600; every
    // 120 the system's available memory is read, and below MH4N_LOWMEM_MB what was not used in the last 120 goes
    // (TrimForMemory); the memory line (MemoryLine) every minute.
    void MemoryHousekeeping() {
        const u64 frame = TextureFrame();
        if (frame % 600 == 0) {
            texture_cache_->Sweep(36000, 600);
        }
#if !defined(_WIN32)
        if (frame % 120 == 0 && LowMemoryKb() > 0) {
            const long available = AvailableMemoryKb();
            if (available >= 0 && available < LowMemoryKb()) {
                TrimForMemory(available);
            } else if (available >= LowMemoryKb() + 100 * 1024) {
                memory_short_told_ = false;  // (an episode is over: the next one is told again)
            }
        }
#endif
        static const u64 every_us = [] {
            const char* v = std::getenv("MH4N_MEM_LOG");
            return static_cast<u64>(v != nullptr && std::atoi(v) > 0 ? std::atoi(v) : 60) * 1000000;
        }();
        const u64 now = static_cast<u64>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        if (now >= next_memory_line_us_) {
            next_memory_line_us_ = now + every_us;
            MemoryLine();
        }
    }

    // Little of the system's memory left: the images and the cache entries not used in the last 120 pictures (one or two
    // seconds) go.
    void TrimForMemory(long available_kb) {
        const u64 frame = TextureFrame();
        const u64 before = texture_bytes_, pixels = texture_cache_->pixel_bytes();
        const size_t entries = texture_cache_->size();
        if (!binding_textures_) {
            ReleaseTextures(0, frame > 120 ? frame - 120 : 0);
        }
        texture_cache_->Sweep(120, 0);
        if (!memory_short_told_) {
            memory_short_told_ = true;
            MH4N_LOG(Warning, kLog, "memory short ({} MB of the system's available): textures' images {} -> {} MB, decoded copies "
                     "{} -> {} MB, cache entries {} -> {}", available_kb / 1024, before >> 20, texture_bytes_ >> 20, pixels >> 20,
                     texture_cache_->pixel_bytes() >> 20, entries, texture_cache_->size());
        }
    }

    // The renderer's memory line (MH4N_MEM_LOG=<seconds>, every minute otherwise), beside the app's [mem] line (its map
    // entries and the system's memory, read on a thread of its own: /proc/self/maps takes the memory map lock): the process's
    // resident memory, the GPU allocator's blocks, the textures' images and decoded copies, the targets. A long session that
    // runs out shows here which part grew.
    void MemoryLine() {
        const ProcessMemory m = ReadProcessMemory();
        if (first_resident_kb_ < 0) {
            first_resident_kb_ = m.resident_kb;
        }
        const rhi::Backend::MemoryUse use = backend_ != nullptr ? backend_->Memory() : rhi::Backend::MemoryUse{};
        u64 targets = 0;
        for (const auto& t : targets_) {
            for (const rhi::Image* image : {t->image.get(), t->upload.get(), t->twin.get()}) {
                if (image != nullptr) targets += u64{image->Width()} * image->Height() * 4 * image->Layers();
            }
        }
        const texture::CacheStats& c = texture_cache_->stats();
        MH4N_LOG(Info, "Memory",
                 "[mem] the renderer: resident {} MB ({:+}; own {}, files {}); GPU allocator {} MB in {} blocks ({} MB in {} "
                 "allocations); textures: {} images {} MB (up to {}; {} let go so far), {} cache entries with {} MB decoded ({} "
                 "gone unused, {} decoded again for an image, {} caught by the spot check, {} of them by the rolling one, {} "
                 "failed); targets {} MB",
                 m.resident_kb / 1024, (m.resident_kb - first_resident_kb_) / 1024, m.anon_kb / 1024, m.file_kb / 1024,
                 use.block_bytes >> 20, use.blocks, use.allocation_bytes >> 20, use.allocations, texture_images_.size(),
                 texture_bytes_ >> 20, TextureBudgetBytes() >> 20, textures_released_, texture_cache_->size(),
                 texture_cache_->pixel_bytes() >> 20, c.swept, c.repixels, c.spotted, c.rolled, c.failures, targets >> 20);
    }

    // Uploads one decoded face into `image` (submitting the recording once when the staging ring is full). False on failure.
    // `ahead`: nothing recorded so far in this recording reads the image (a new texture, or one this recording has not
    // bound yet), so the upload goes into the recording's prelude, which the submission runs before everything else it
    // holds: the open pass goes on. A texture first seen in the middle of the scene pass broke it in two, and on a tiler
    // the second half loads what the first stored, colour and depth (18.8 MB at 3x on a wide screen, gpu4). The image
    // reaches its draws in GENERAL with the copy visible as before (the prelude's barriers come first in the queue).
    // MH4N_UPLOAD_AHEAD=0: every upload in order, as before.
    static bool UploadAheadOn() {
        static const bool on = [] {
            const char* v = std::getenv("MH4N_UPLOAD_AHEAD");
            return v == nullptr || *v != '0';
        }();
        return on;
    }
    bool UploadFace(rhi::Image& image, u32 layer, u32 width, u32 height, const std::vector<u8>& rgba, bool ahead = false) {
        EnsureRecording();
        if (ahead && UploadAheadOn() && !AbOffFor("uploads")) {
            rec_->BeginPrelude();
            const rhi::Result<void> r = fragment::sampling::UploadRgba8(*rec_, *staging_ring_, image, 0, layer, width, height, rgba);
            rec_->EndPrelude();
            if (r.ok()) {
                ++frame_.texture_uploads;
                ++frame_.uploads_ahead;
                return true;
            }
            // (the staging ring full: in order, below, which submits the recording first)
        }
        TraceEventF("upload {}x{}", width, height);
        composer_->EndPass(*rec_);
        rhi::Result<void> r = fragment::sampling::UploadRgba8(*rec_, *staging_ring_, image, 0, layer, width, height, rgba);
        if (!r.ok()) {
            FlushRecording();
            EnsureRecording();
            r = fragment::sampling::UploadRgba8(*rec_, *staging_ring_, image, 0, layer, width, height, rgba);
        }
        if (!r.ok()) {
            MH4N_LOG(Error, kLog, "texture upload {}x{}: {}", width, height, r.error());
            return false;
        }
        ++frame_.texture_uploads;
        return true;
    }

    // The image of a texture (one 2D face or the six of a cube), decoded by the cache and uploaded when new or changed.
    // Null when the memory cannot be read or decoded.
    rhi::Image* TextureImage(const std::vector<const texture::TextureRef*>& faces) {
        TexImageKey key;
        key.cube = faces.size() == 6;
        const texture::TextureRef& first = *faces[0];
        for (size_t i = 0; i < faces.size(); ++i) {
            const texture::TextureRef& f = *faces[i];
            if (f.width != first.width || f.height != first.height || f.format != first.format) {
                return nullptr;  // a cube whose faces differ
            }
            key.faces[i] = texture::CacheKey{f.paddr, f.format, f.width, f.height};
            // A render target that holds this memory is written back first (until the surface cache, N2-15, samples it directly),
            // and a deferred framebuffer write there made.
            const u32 bytes = static_cast<u32>(texture::EncodedBytes(f.format, f.width, f.height));
            MaterializeOverlapping(f.paddr, static_cast<u32>(std::min<u64>(u64{f.paddr} + bytes, 0xFFFFFFFFull)));
            const CauseScope cause(*this, WritebackCause::Texture);
            if (trace_.active) {
                for (auto& t : targets_) {
                    if (DirtyOverlaps(*t, f.paddr, f.paddr + bytes)) {
                        TraceEventF("texture {:08x} {}x{} fmt {} reads target {:08x} {}x{} fmt {}{}", f.paddr, f.width,
                                               f.height, static_cast<u32>(f.format), t->paddr, t->width, t->height, t->format,
                                               t->depth ? " depth" : "");
                    }
                }
            }
            WriteBackOverlapping(f.paddr, bytes, /*stale=*/false);
        }
        if (texture_budget_us_ > 0) {
            // past the budget a texture never decoded before is decoded on AsyncDecoder's threads
            if (decoder_ == nullptr) {
                decoder_ = std::make_unique<texture::AsyncDecoder>();
            }
            for (size_t i = 0; i < faces.size(); ++i) {
                const texture::CacheKey& k = key.faces[i];
                if (texture_cache_->Has(k)) {
                    continue;
                }
                if (std::optional<texture::AsyncDecoder::Result> done = decoder_->Take(k)) {
                    texture_cache_->Insert(k, done->hash, std::move(done->rgba8));  // (else Get decodes it below)
                } else if (decoder_->Pending(k)) {
                    texture_pending_ = true;
                } else if (texture_spent_us_ >= texture_budget_us_) {
                    const std::size_t bytes = texture::EncodedBytes(k.format, k.width, k.height);
                    if (const u8* memory = host_.PhysicalPointer(k.paddr, static_cast<u32>(bytes));
                        memory != nullptr && decoder_->Start(k, memory, bytes)) {
                        ++frame_.texture_deferred;
                        texture_pending_ = true;
                    }
                }
            }
            if (texture_pending_) {
                return nullptr;  // (the draw is left out: DrawImpl)
            }
        }
        std::array<const texture::CachedTexture*, 6> decoded{};
        const auto decode_start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < faces.size(); ++i) {
            decoded[i] = texture_cache_->Get(key.faces[i]);
            if (decoded[i] == nullptr) {
                return nullptr;
            }
        }
        const u64 decode_us = static_cast<u64>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - decode_start).count());
        texture_spent_us_ += decode_us;
        frame_.decode_us += decode_us;
        if (decode_us >= 2000 && slow_decodes_told_ < 40) {  // (which textures take long: the first 40)
            ++slow_decodes_told_;
            MH4N_LOG(Info, kLog, "texture {:08x} {}x{} format {}{}: {} us to decode (and hash)", first.paddr, first.width,
                     first.height, static_cast<u32>(first.format), key.cube ? " (cube)" : "", decode_us);
        }
        auto it = texture_images_.find(key);
        if (it == texture_images_.end() && texture_memory_short_) {
            return nullptr;  // (an image could not be made in this recording: TrimTextures first)
        }
        if (it == texture_images_.end()) {
            rhi::ImageDesc desc;
            desc.format = rhi::Format::Rgba8Unorm;
            desc.width = first.width;
            desc.height = first.height;
            desc.cube = key.cube;
            desc.layers = key.cube ? 6 : 1;
            desc.name = key.cube ? "pica cube texture" : "pica texture";
            const auto create_start = std::chrono::steady_clock::now();
            rhi::Result<std::shared_ptr<rhi::Image>> image = backend_->CreateImage(desc);
            const u64 create_us = static_cast<u64>(
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - create_start).count());
            frame_.create_us += create_us;
            if (create_us >= 1000 && slow_creates_told_ < 40) {  // (which images take long to make: the first 40)
                ++slow_creates_told_;
                const rhi::Backend::MemoryUse use = backend_->Memory();
                MH4N_LOG(Info, kLog, "texture image {}x{}{}: {} us to make (the allocator's blocks {} MB, {} of them)",
                         first.width, first.height, key.cube ? " (cube)" : "", create_us, use.block_bytes >> 20, use.blocks);
            }
            if (!image.ok()) {
                if (texture_create_failures_++ < 10) {
                    MH4N_LOG(Error, kLog, "texture {}x{}: {} (images {} MB in {})", first.width, first.height, image.error(),
                             texture_bytes_ >> 20, texture_images_.size());
                }
                // (the images not used lately go when the next recording begins, and no image is made until then: each
                // failing one can cost the allocator a new block's try)
                texture_memory_short_ = true;
                return nullptr;
            }
            TexImage entry;
            entry.image = *image;
            entry.bytes = static_cast<u64>(first.width) * first.height * 4 * faces.size();
            texture_bytes_ += entry.bytes;
            it = texture_images_.emplace(key, std::move(entry)).first;
        }
        TexImage& entry = it->second;
        // (no draw of this recording has bound it: its uploads may go ahead of the recording, UploadFace)
        const bool unread_here = entry.stamp != recording_serial_;
        entry.stamp = recording_serial_;
        entry.used_frame = TextureFrame();
        bool uploaded_any = false;  // (only then is there a decoded copy to let go)
        for (size_t i = 0; i < faces.size(); ++i) {
            if (entry.generation[i] == decoded[i]->generation) {
                continue;
            }
            if (decoded[i]->rgba8.empty()) {
                // its pixels went once an image held them (TextureCache::ReleasePixels): this image is new or was let go
                const u64 pixels_start = NowUs();
                decoded[i] = texture_cache_->Pixels(key.faces[i]);
                frame_.decode_us += NowUs() - pixels_start;
                if (decoded[i] == nullptr) {
                    return nullptr;
                }
            }
            const u64 upload_start = NowUs();
            const bool uploaded = UploadFace(*entry.image, static_cast<u32>(i), first.width, first.height, decoded[i]->rgba8,
                                             unread_here && entry.stamp == recording_serial_);
            frame_.upload_us += NowUs() - upload_start;
            if (!uploaded) {
                return nullptr;
            }
            entry.generation[i] = decoded[i]->generation;
            uploaded_any = true;
            // Uploading may have submitted the recording: the entry stays valid (the map is not touched), but the stamp of this
            // recording is the new one.
            entry.stamp = recording_serial_;
        }
        for (size_t i = 0; i < faces.size() && uploaded_any; ++i) {
            texture_cache_->ReleasePixels(key.faces[i]);  // (the image holds them now)
        }
        return entry.image.get();
    }

    // Render to texture (`--target-textures`, on by default, at any scale): the image of the colour target that is exactly
    // this texture (address, size and format), when it is not older than guest memory, in place of the write-back, the
    // decode and the upload; at a scale it is also what keeps the picture at that scale. A target's image holds its rows
    // in a texture's order (guest row 0 last, as UploadRgba8 stores a texture), so it is sampled as it is, with the
    // texture's own sampler. The draw's own colour target cannot be sampled in its pass: then the target's feedback copy,
    // made now, before the draw. Null otherwise.
    rhi::Image* TargetAsTexture(const pica::PicaState& state, const texture::TextureRef& ref, u32 unit = 0, bool plain_2d = false) {
        Target* found = nullptr;
        for (auto& t : targets_) {
            // (not a shadow map: its image holds what the shadow-write pass stored, not the texels of its memory)
            if (!t->depth && !t->shadow && !t->stale && t->paddr == ref.paddr && t->width == ref.width &&
                t->height == ref.height &&
                static_cast<u32>(ref.format) == t->format && (found == nullptr || t->drawn > found->drawn)) {
                found = t.get();
            }
        }
        if (found == nullptr) {
            return nullptr;
        }
        // A draw into this target that writes every pixel of its box (ping-pong, below) reads only the rows its texture
        // coordinates reach: only those are brought up to date here (gpu4). MH4U's colour filters on a wide screen read the
        // scene's rows, and the bloom's buffers lie in the target's other rows: their tiles are not copied in for them.
        const u32 color_address = pica::BitsOf(state.regs[kRegColorAddress], 0, 28) * 8;
        std::optional<rhi::Rect> pingpong_box;
        std::pair<u32, u32> rows{0, found->height};  // the guest rows the draw can read
        if (found->paddr == color_address) {
            pingpong_box = PingPongBox(*found);
            if (pingpong_box && plain_2d && LazyTilesOn()) {
                if (const std::optional<std::pair<u32, u32>> read = SampledRows(*found, unit, state)) {
                    rows = *read;
                }
            }
        }
        const u32 read_begin = found->paddr + rows.first / 8 * TileRowBytes(*found);
        const u32 read_end = rows.second >= found->height ? found->paddr + found->bytes
                                                          : found->paddr + rows.second / 8 * TileRowBytes(*found);
        // What a texture decoded from memory would hold: what another target drew over part of it since its last
        // write-back, copied tile by tile from that target's image (same format and scale), or written back and those rows
        // loaded again (two targets are never dirty in the same bytes: UseTarget writes the other back).
        std::vector<Target*> copies;
        std::vector<std::pair<u32, u32>> copied_bytes;
        for (auto& other : targets_) {
            if (other.get() != found && DirtyOverlaps(*other, read_begin, read_end)) {
                if (CanCopyTiles(*other, *found)) {
                    copies.push_back(other.get());  // its tiles, copied after the refresh below (CopyTiles)
                    copied_bytes.push_back(CopiedBytes(*other));
                    continue;
                }
                const CauseScope cause(*this, WritebackCause::Texture);
                if (trace_.active) {
                    std::string stale;
                    for (const auto& [a, b] : other->stale_rows) stale += fmt::format(" {}-{}", a, b);
                    TraceEventF("target {:08x} {}x{} as a texture: {:08x} {}x{} fmt {} over it, dirty {}-{}, stale{}{}",
                                           found->paddr, found->width, found->height, other->paddr, other->width, other->height,
                                           other->format, other->dirty_first, other->dirty_last, other->stale ? " all" : "", stale);
                }
                WriteBack(*other);
                const StaleWhy why(*this, "another target over it, sampled");
                MarkStale(*found, other->paddr, other->paddr + other->bytes);
            }
        }
        DropCoveredStaleRows(*found, std::move(copied_bytes));
        Refresh(*found, rows.first, rows.second);  // rows another target or the GX engine wrote since
        CopyTiles(copies, *found);
        // (a screen target's image: what it owes taken back first, gpu3)
        ScreenRead(*found, rows.first == 0 && rows.second >= found->height ? WholeImage(*found) : RowsRect(*found, rows.first, rows.second),
                   "a texture");
        if (found->paddr != color_address) {
            return found->image.get();
        }
        // The draw's own target: its copy, brought up to date in the rows that changed since (the whole image the first
        // time), sampled in its place.
        self_sampled_ = true;
        EnsureRecording();
        composer_->EndPass(*rec_);
        if (found->twin == nullptr && found->screen != nullptr) {
            // (a screen target's twin is one of its shared images: a free one, or a new one)
            std::vector<std::pair<u32, u32>> lacking, lacking_wide;
            u32 lacking_x1 = 0;
            TargetBuffer* b = TakeSpare(*found, /*twin_too=*/false, lacking, lacking_wide, lacking_x1);
            if (b == nullptr) {
                MH4N_LOG(Error, kLog, "the sampled copy of a screen target: no image");
                return nullptr;
            }
            found->screen->twin = b;
            found->twin = b->image;
            found->TwinSame();
            // (the rows it lacks, with their columns: whole rows where it may differ anywhere; MH4N_HANDOVER_COLUMNS=0, or
            // the A/B's "hocols" phase: every row whole, as before)
            const bool columns = HandOverColumns() && !AbOffFor("hocols");
            for (const auto& [f, l] : lacking) found->TwinDiffers(f, l, columns ? lacking_x1 : ~0u);
            if (columns) {
                for (const auto& [f, l] : lacking_wide) found->TwinDiffers(f, l);
            }
        }
        if (found->twin == nullptr) {
            rhi::ImageDesc desc;
            desc.format = rhi::Format::Rgba8Unorm;
            desc.width = found->width * found->scale;
            desc.height = found->height * found->scale;
            desc.name = "pica colour buffer (twin)";
            rhi::Result<std::shared_ptr<rhi::Image>> twin = backend_->CreateImage(desc);
            if (!twin.ok()) {
                MH4N_LOG(Error, kLog, "the sampled copy of a render target: {}", twin.error());
                return nullptr;
            }
            found->twin = *twin;
            found->TwinSame();
            found->TwinDiffers(0, found->height);
        }
        // A draw that writes every pixel of its box whatever was there (PingPongBox) samples the target itself and draws
        // into the twin, which then becomes the target (CommitPingPong, once the draw is recorded): only what the twin
        // lacks outside that box is copied, not the rows that changed.
        if (pingpong_box) {
            pingpong_ = PingPong{found, *pingpong_box};
            return found->image.get();
        }
        if (!found->twin_rows.empty()) {
            const u32 n = found->scale;
            std::vector<rhi::ImageCopyRegion> regions;
            std::string rows;
            for (const auto& [first, last] : found->twin_rows) {
                rhi::ImageCopyRegion region;
                region.src_y = region.dst_y = (found->height - std::min(last, found->height)) * n;  // guest row 0 last
                region.width = found->width * n;
                region.height = (std::min(last, found->height) - first) * n;
                regions.push_back(region);
                if (trace_.active) rows += fmt::format(" {}-{}", first, last);
            }
            TraceEventF("feedback {:08x} {}x{} rows{}", found->paddr, found->width, found->height, rows);
            const rhi::ImageCopyJob job{found->image.get(), found->twin.get(), regions};
            rec_->CopyImages(std::span<const rhi::ImageCopyJob>(&job, 1));
            found->TwinSame();
            ++frame_.dst_copies;
        }
        return found->twin.get();
    }

    // ---- ping-pong: a draw that samples its own target and replaces every pixel of its box ----
    // MH4U's post-processing draws full-screen quads into the scene's target that sample that same target (a colour filter,
    // the bloom's composite). Such a draw that writes every pixel of its box, whatever was there before, need not sample a
    // copy: it samples the target's image and draws into the twin, and the twin becomes the target's image. The twin then
    // needs only what it lacks outside the box (the rows that changed since it was last the same, less the box), and
    // the old image is the new twin, the same as the target outside the box. The same pixels are read and written as
    // with the copy. MH4N_PINGPONG=0: always the copy, as before.

    // What the draw being prepared is, for TargetAsTexture (DrawImpl sets it around BindTextures).
    struct SelfDraw {
        const pica::DrawCall* draw = nullptr;
        const fragment::FragmentKey* key = nullptr;
        const fragment::Needs* needs = nullptr;
        const rhi::FixedFunctionState* ff = nullptr;
        const DrawGeometry* geometry = nullptr;
        u32 scale = 1;
    };
    struct PingPong {
        Target* target = nullptr;  // the target whose twin the draw draws into
        rhi::Rect box;             // the image pixels the draw writes, every one of them
        // guest rows the twin was not given: other targets' dirty tiles cover them whole (PreparePingPong, gpu4)
        std::vector<std::pair<u32, u32>> lazy;
    };

    // Every fragment of the draw replaces the target's colour, whatever was there: no blending (or one that only takes the
    // source), the whole colour written, no logic op, nothing the fragment stage may discard (alpha test Always, no
    // Exclude scissor), and no depth or stencil test (`tests`: or only ones that always pass, Always).
    static bool ReplacesColour(const SelfDraw& s, bool tests) {
        const rhi::FixedFunctionState& ff = *s.ff;
        const auto takes_source = [](rhi::BlendFactor src, rhi::BlendFactor dst, rhi::BlendOp op) {
            return src == rhi::BlendFactor::One && dst == rhi::BlendFactor::Zero && op == rhi::BlendOp::Add;
        };
        if (ff.blend_enabled && !(takes_source(ff.src_color, ff.dst_color, ff.color_op) &&
                                  takes_source(ff.src_alpha, ff.dst_alpha, ff.alpha_op))) {
            return false;
        }
        if (ff.color_write_mask != rhi::kWriteAll || ff.logic_op_enabled) {
            return false;
        }
        if (tests ? ((ff.depth_test && ff.depth_compare != rhi::CompareOp::Always) ||
                     (ff.stencil_test && (ff.stencil_front.compare != rhi::CompareOp::Always ||
                                          ff.stencil_back.compare != rhi::CompareOp::Always)))
                  : (ff.depth_test || ff.depth_write || ff.stencil_test)) {
            return false;
        }
        const fragment::FramebufferConfig& fb = s.key->framebuffer;
        return fb.alpha_test_func() == fragment::regenum::CompareFunc::Always &&
               fb.scissor_test_mode() != fragment::regenum::ScissorMode::Exclude && fb.shadow_rendering() == 0 &&
               !s.needs->dst_copy && !s.needs->shadow_write_pass;
    }

    // The draw's scissor box (image pixels of `t`) when the draw covers every pixel of it: no culling, no user clip plane,
    // and two triangles whose corners (shaded on the CPU) make a rectangle that holds every pixel centre of the box with a
    // quarter of a pixel to spare, inside the clip volume's depth (or exactly on its near or far plane). Anything else, or
    // anything that cannot be told: none.
    std::optional<rhi::Rect> CoveredBox(const SelfDraw& s, const Target& t) {
        const pica::PicaState& state = *s.draw->state;
        if (s.ff->cull != rhi::CullMode::None || (state.regs[shader::reg::kClipEnable] & 1u) != 0 ||
            cpuvertex::CpuPathReason(*s.draw, assembler_.IsEmpty()) != cpuvertex::CpuReason::None) {
            return std::nullopt;
        }
        const rhi::Rect box = s.geometry->scissor;
        if (box.width == 0 || box.height == 0 || box.x < 0 || box.y < 0 || box.x + box.width > t.width * t.scale ||
            box.y + box.height > t.height * t.scale) {
            return std::nullopt;
        }
        // the two triangles: a list of six vertices, or a strip or a fan of four
        const u32 topology = pica::BitsOf(state.regs[pica::reg::kTriangleTopology], 8, 2);
        std::array<std::array<u32, 3>, 2> triangles;
        if (topology == pica::kTopologyList && s.draw->num_vertices == 6) {
            triangles = {{{0, 1, 2}, {3, 4, 5}}};
        } else if (topology == pica::kTopologyStrip && s.draw->num_vertices == 4) {
            triangles = {{{0, 1, 2}, {1, 2, 3}}};
        } else if (topology == pica::kTopologyFan && s.draw->num_vertices == 4) {
            triangles = {{{0, 1, 2}, {0, 2, 3}}};
        } else {
            return std::nullopt;
        }
        const cpuvertex::RunStatus ran = runner_.Run(host_, *s.draw, cpuvertex::RunMode::VertexOnly);
        seen_cpu_ = runner_.counters();  // (not a CPU-path draw: kept out of its counters)
        if (ran != cpuvertex::RunStatus::Ok) {
            return std::nullopt;
        }
        const std::vector<cpuvertex::OutputVertex>& v = runner_.output().vertices;
        if (v.size() != s.draw->num_vertices) {
            return std::nullopt;
        }
        std::array<float, 6> nx{}, ny{};
        float x0 = 0, x1 = 0, y0 = 0, y1 = 0;
        for (size_t i = 0; i < v.size(); ++i) {
            const float x = v[i].pos[0], y = v[i].pos[1], z = v[i].pos[2], w = v[i].pos[3];
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(w) || !(w > 0.0f)) {
                return std::nullopt;
            }
            // the depth the clip volume keeps, 0 <= -z <= w (the vertex stage's gl_Position.z is -z), with a margin, or
            // exactly on its near or far plane (exact values, as a clear's quad at depth 0 or 1 has: the GPU gets them too)
            if (!(z == 0.0f || z == -w || (-z >= w * 1e-4f && -z <= w * (1.0f - 1e-4f)))) {
                return std::nullopt;
            }
            nx[i] = x / w;
            ny[i] = y / w;
            x0 = i == 0 ? nx[i] : std::min(x0, nx[i]);
            x1 = i == 0 ? nx[i] : std::max(x1, nx[i]);
            y0 = i == 0 ? ny[i] : std::min(y0, ny[i]);
            y1 = i == 0 ? ny[i] : std::max(y1, ny[i]);
        }
        // each vertex on a corner of that rectangle; each triangle three corners; the corners they leave out opposite
        constexpr float kCorner = 1e-5f;
        if (!(x1 - x0 > 0.01f && y1 - y0 > 0.01f)) {
            return std::nullopt;
        }
        std::array<u32, 6> corner{};
        for (size_t i = 0; i < v.size(); ++i) {
            const bool left = std::abs(nx[i] - x0) <= kCorner, right = std::abs(nx[i] - x1) <= kCorner;
            const bool low = std::abs(ny[i] - y0) <= kCorner, high = std::abs(ny[i] - y1) <= kCorner;
            if (left == right || low == high) {
                return std::nullopt;
            }
            corner[i] = (right ? 1u : 0u) | (high ? 2u : 0u);
        }
        std::array<u32, 2> left_out{};
        for (u32 k = 0; k < 2; ++k) {
            const u32 a = corner[triangles[k][0]], b = corner[triangles[k][1]], c = corner[triangles[k][2]];
            if (a == b || b == c || a == c) {
                return std::nullopt;
            }
            left_out[k] = 6u - a - b - c;  // (0 + 1 + 2 + 3 less the three)
        }
        if ((left_out[0] ^ left_out[1]) != 3u) {
            return std::nullopt;
        }
        // that rectangle in the window, either way up, holds every pixel centre of the box with a quarter pixel to spare
        const rhi::Viewport& vp = s.geometry->viewport;
        const float wx0 = vp.x + (x0 + 1.0f) * vp.width * 0.5f, wx1 = vp.x + (x1 + 1.0f) * vp.width * 0.5f;
        const float wy0 = vp.y + (std::max(y0, -y1) + 1.0f) * vp.height * 0.5f;  // (the lower edge, either way up)
        const float wy1 = vp.y + (std::min(y1, -y0) + 1.0f) * vp.height * 0.5f;
        if (!(wx0 <= static_cast<float>(box.x) + 0.25f && wx1 >= static_cast<float>(box.x + static_cast<s32>(box.width)) - 0.25f &&
              wy0 <= static_cast<float>(box.y) + 0.25f && wy1 >= static_cast<float>(box.y + static_cast<s32>(box.height)) - 0.25f)) {
            return std::nullopt;
        }
        return box;
    }

    // ---- other targets' tiles in a ping-pong target (gpu4) ----
    // On a wide screen MH4U's scene target holds the bloom's buffers in rows it never draws (scaled_screens.h's note in
    // native_gpu.h): the first downsample reads every row of the target, so those buffers' tiles are copied in (CopyTiles),
    // and the colour filter's ping-pong then copied them into the twin as rows that changed (313k of its 392k pixels on the
    // Moto at 3x), though the next frame's downsample copies them in again and nothing reads them in between. Now:
    //   - a ping-pong draw brings its target up to date only in the rows its texture coordinates reach (SampledRows): the
    //     colour filters read the scene's rows;
    //   - its twin is not given rows that other targets' dirty tiles cover whole (and its own draws did not touch): after
    //     the swap those rows are stale in the target (memory, or those tiles, are newer than its image) and the target
    //     forgets that it holds the tiles (Target::copied), so the next read that reaches them copies them in again, or
    //     loads them once the other target is written back (TargetAsTexture, DropCoveredStaleRows).
    // MH4N_LAZY_TILES=0: as before (MH4N_AB_FEATURE=tiles: in turn).
    bool LazyTilesOn() const {
        static const bool on = [] {
            const char* v = std::getenv("MH4N_LAZY_TILES");
            return v == nullptr || *v != '0';
        }();
        return on && !AbOffFor("tiles");
    }

    // The guest rows (whole rows of tiles) a draw's texture unit `unit` can read in `t`, from the vertices CoveredBox has
    // just shaded on the CPU: their texture coordinates for that unit, which the rasteriser interpolates between them (a
    // convex combination, perspective or not), within [0, 1] (no wrap, no border), and a bilinear filter's texel either
    // side, with a margin. None when it cannot be told.
    std::optional<std::pair<u32, u32>> SampledRows(const Target& t, u32 unit, const pica::PicaState& state) {
        const std::vector<cpuvertex::OutputVertex>& v = runner_.output().vertices;
        if (v.empty() || unit > 2) {
            return std::nullopt;
        }
        const bool unit2_coord1 = unit == 2 && pica::BitsOf(state.regs[texture::kMainConfig], 13, 1) != 0;
        float lo = 0.0f, hi = 0.0f;
        for (size_t i = 0; i < v.size(); ++i) {
            const float c = unit == 0 ? v[i].tc0[1] : (unit == 1 || unit2_coord1) ? v[i].tc1[1] : v[i].tc2[1];
            if (!std::isfinite(c)) {
                return std::nullopt;
            }
            lo = i == 0 ? c : std::min(lo, c);
            hi = i == 0 ? c : std::max(hi, c);
        }
        if (lo < 0.0f || hi > 1.0f) {
            return std::nullopt;
        }
        const s64 n = t.scale, image_rows = s64{t.height} * n;
        const s64 r0 = std::clamp<s64>(static_cast<s64>(std::floor(lo * static_cast<float>(image_rows))) - 2, 0, image_rows - 1);
        const s64 r1 = std::clamp<s64>(static_cast<s64>(std::ceil(hi * static_cast<float>(image_rows))) + 2, 0, image_rows - 1);
        // image row r is guest row height - 1 - r / n (guest row 0 last)
        const u32 g0 = static_cast<u32>(s64{t.height} - 1 - r1 / n), g1 = static_cast<u32>(s64{t.height} - 1 - r0 / n);
        return std::pair<u32, u32>{g0 / 8 * 8, std::min(t.height, (g1 + 8) / 8 * 8)};
    }

    // The image pixels the draw being prepared writes, each whatever the target held there (ReplacesColour with no depth
    // or stencil at all, and CoveredBox), for ping-pong.
    std::optional<rhi::Rect> PingPongBox(const Target& t) {
        static const bool enabled = [] {
            const char* v = std::getenv("MH4N_PINGPONG");
            return v == nullptr || *v != '0';
        }();
        const SelfDraw& s = self_draw_;
        if (!enabled || AbOff() || s.draw == nullptr || s.scale != t.scale || t.depth || t.shadow || !ReplacesColour(s, false)) {
            return std::nullopt;
        }
        return CoveredBox(s, t);
    }

    // ---- the first draw of a pass that writes every pixel of it ----
    // A pass begun by a draw that writes every pixel of its box whatever was there (ReplacesColour, depth and stencil
    // tests only if they always pass, CoveredBox), with a render area no wider than that box out to 16 pixels, does not
    // load its colour: its area becomes the box and it clears the colour instead (LoadOp::DontCare). Its depth-stencil
    // attachment is cleared too when the draw writes both of them there (depth Always with the write, and the stencil, if
    // the format has one, Always with Replace on both faces and every bit written). MH4U clears the scene's target and its
    // depth with such a quad. MH4N_FIRST_DRAW_CLEAR=0: every pass loads, as before.
    static bool FirstDrawClearOn() {
        static const bool on = [] {
            const char* v = std::getenv("MH4N_FIRST_DRAW_CLEAR");
            return v == nullptr || *v != '0';
        }();
        return on;
    }

    // A pass's render area `area` becomes `box` when it holds the box and is no more than the box out to 16 pixels (the
    // learned areas' rounding): the draws of the pass that lie inside the box go on in it (PassAreaLearned), and the box
    // is what the first draw writes whole. False, and `area` unchanged, otherwise.
    static bool ShrinkToBox(rhi::Rect& area, const rhi::Rect& box) {
        const rhi::Rect& a = area;
        const rhi::Rect& b = box;
        const s32 bx1 = b.x + static_cast<s32>(b.width), by1 = b.y + static_cast<s32>(b.height);
        const s32 rx0 = b.x / 16 * 16, ry0 = b.y / 16 * 16, rx1 = (bx1 + 15) / 16 * 16, ry1 = (by1 + 15) / 16 * 16;
        const s32 ax1 = a.x + static_cast<s32>(a.width), ay1 = a.y + static_cast<s32>(a.height);
        if (a.width == 0 || a.height == 0 || b.width == 0 || b.height == 0 || a.x < rx0 || a.y < ry0 || ax1 > rx1 ||
            ay1 > ry1 || b.x < a.x || b.y < a.y || bx1 > ax1 || by1 > ay1) {
            return false;
        }
        area = box;
        return true;
    }

    void FirstDrawClears(const SelfDraw& s, Target* color, Target* depth, DrawDesc& desc) {
        if (!FirstDrawClearOn() || AbOff() || !LearnedPassAreas() || color == nullptr || color->scale != s.scale ||
            desc.target.depth_load == rhi::LoadOp::Clear ||  // (a clear owed over every pixel drawn since keeps its area)
            composer_->Continues(desc) || !ReplacesColour(s, true)) {
            return;
        }
        const std::optional<rhi::Rect> covered = CoveredBox(s, *color);
        static const bool log = std::getenv("MH4N_DRAW_LOG") != nullptr;
        if (log && trace_.active) {
            std::string v;
            for (const cpuvertex::OutputVertex& o : runner_.output().vertices) {
                v += fmt::format(" ({:.7g},{:.7g},{:.7g},{:.7g})", o.pos[0], o.pos[1], o.pos[2], o.pos[3]);
            }
            MH4N_LOG(Info, kLog, "FIRST {:08x} covered {} area {},{} {}x{} topology {} n {}{}", color->paddr, covered.has_value(),
                     desc.target.area.x, desc.target.area.y, desc.target.area.width, desc.target.area.height,
                     pica::BitsOf(s.draw->state->regs[pica::reg::kTriangleTopology], 8, 2), s.draw->num_vertices, v);
        }
        if (!covered) {
            return;
        }
        const rhi::Rect& b = *covered;
        if (!ShrinkToBox(desc.target.area, b)) {
            return;
        }
        desc.target.color_load = rhi::LoadOp::DontCare;
        bool depth_cleared = false;
        if (depth != nullptr) {
            const rhi::FixedFunctionState& ff = *s.ff;
            const bool stencil = static_cast<DepthFormat>(depth->format) == DepthFormat::D24S8;
            const fbops::DynamicState dynamic = fbops::MakeDynamicState(*s.draw->state);
            const bool stencil_written = !stencil || (ff.stencil_test && ff.stencil_front.pass == rhi::StencilOp::Replace &&
                                                      ff.stencil_back.pass == rhi::StencilOp::Replace &&
                                                      (dynamic.stencil_write_mask & 0xFF) == 0xFF);
            if (ff.depth_test && ff.depth_write && ff.depth_compare == rhi::CompareOp::Always && stencil_written) {
                desc.target.depth_load = rhi::LoadOp::Clear;
                desc.target.clear_depth = 1.0f;
                desc.target.clear_stencil = 0;
                depth_cleared = true;
            }
        }
        ++frame_.first_draw_clears;
        if (color->screen != nullptr && Pixels(b) > Pixels(color->screen->clear_box_now)) {
            color->screen->clear_box_now = b;  // (the next frame's expected clear, gpu3)
        }
        TraceEventF("first-draw clear {:08x} box {},{} {}x{}{}", color->paddr, b.x, b.y, b.width, b.height,
                               depth_cleared ? " and its depth" : "");
    }

    // ---- MH4N_AB=<frames> (a measurement aid): the optimizations above on and off in turn ----
    // Every <frames> game frames (display transfers, two a frame) ping-pong and the first draw's clear are turned off or
    // on again, and the phase is a counter (ab_phase: 1 on, 2 off) the pacing trace marks with the GPU's time, so one
    // run compares both on the same phone, scene and heat.
    // MH4N_AB_FEATURE=<name>[,<name>...]: the A/B turns other optimizations off instead, one at a time: the phases go
    // all on (ab_phase 1), the first one off (2), all on, the second one off (3), ... Names: screens, the screen targets'
    // own pictures (gpu3); uploads, new textures uploaded ahead of the recording (UploadFace); submit, the submission at
    // the 256th draw held to the next pass break (SubmitAtPassBreak); tiles, other targets' tiles left out of a
    // ping-pong's twin and of what a ping-pong draw does not read (LazyTilesOn); tiledraw, other targets' tiles drawn in
    // one pass instead of copied (DrawTiles); columns, a ping-pong's twin given only the columns that changed
    // (Target::TwinDiffers) (gpu4); notex, a measurement aid: the game's textures (not the targets read as textures)
    // the 1x1 null texture, not the picture; shaders, the fragment stages as before gpu5 (fragment::SetShadersAsBefore: the
    // TEV's roundings and clamps, the LOD of a unit with levels); pretake, a measurement aid: the hidden touch screen's
    // snapshot of a published picture that owes not taken at the hand-over (PretakeSnapshot) but from the image the app's
    // side holds, as before 4u-native #31; owing, a hand-over that owes nothing (the box copied, as on a GPU OwingAllowed
    // says no to).
    static u32 AbFrames() {
        static const u32 frames = [] {
            const char* v = std::getenv("MH4N_AB");
            return v != nullptr ? static_cast<u32>(std::strtoul(v, nullptr, 10)) : 0u;
        }();
        return frames;
    }
    static const std::vector<std::string>& AbFeatures() {
        static const std::vector<std::string> features = [] {
            std::vector<std::string> out;
            const char* v = std::getenv("MH4N_AB_FEATURE");
            std::string_view s = v != nullptr ? v : "";
            while (!s.empty()) {
                const size_t comma = s.find(',');
                if (comma != 0) out.emplace_back(s.substr(0, comma));
                s = comma == std::string_view::npos ? std::string_view{} : s.substr(comma + 1);
            }
            return out;
        }();
        return features;
    }
    // 0: everything on; i > 0: the i-th feature off (or, with no MH4N_AB_FEATURE, ping-pong and the clears)
    u32 AbPhase() const {
        if (AbFrames() == 0) return 0;
        const u64 step = ab_transfers_ / (2 * u64{AbFrames()});
        if (step % 2 == 0) return 0;
        const u64 n = std::max<size_t>(AbFeatures().size(), 1);
        return static_cast<u32>((step / 2) % n) + 1;
    }
    bool AbPhaseOff() const { return AbPhase() != 0; }
    bool AbOff() const { return AbFeatures().empty() && AbPhaseOff(); }
    bool AbOffFor(std::string_view feature) const {
        const u32 phase = AbPhase();
        return phase != 0 && phase <= AbFeatures().size() && AbFeatures()[phase - 1] == feature;
    }
    bool AbScreensOff() const { return AbOffFor("screens"); }
    // A trial (a measurement aid, not the picture): MH4N_AB_FEATURE=+<name> turns <name> on in its phase instead. tiny:
    // every draw's scissor cut to 8x8, as MH4N_DEBUG_TINY_SCISSOR=1 (the GPU's time that is not fragment work).
    bool AbTry(std::string_view feature) const {
        const u32 phase = AbPhase();
        if (phase == 0 || phase > AbFeatures().size()) return false;
        const std::string& f = AbFeatures()[phase - 1];
        return f.size() == feature.size() + 1 && f[0] == '+' && std::string_view(f).substr(1) == feature;
    }

    // The draw of pingpong_ is about to be recorded into the twin of its target: the twin is given what it lacks outside the
    // box (the rows that changed since it was last the same as the target, less the box) from the target's image.
    void PreparePingPong() {
        Target& t = *pingpong_.target;
        const rhi::Rect& box = pingpong_.box;
        const u32 n = t.scale, w = t.width * n;
        const s32 bx0 = box.x, bx1 = box.x + static_cast<s32>(box.width), by0 = box.y, by1 = box.y + static_cast<s32>(box.height);
        std::vector<rhi::ImageCopyRegion> regions;
        auto add = [&](s32 x0, s32 y0, s32 x1, s32 y1) {
            if (x1 <= x0 || y1 <= y0) return;
            rhi::ImageCopyRegion region;
            region.src_x = region.dst_x = static_cast<u32>(x0);
            region.src_y = region.dst_y = static_cast<u32>(y0);
            region.width = static_cast<u32>(x1 - x0);
            region.height = static_cast<u32>(y1 - y0);
            regions.push_back(region);
        };
        // (rows of tiles outside the box's rows that other targets' dirty tiles cover whole, and that the target's own
        // draws since its last write-back did not touch, are left to those tiles: LazyTilesOn above)
        std::vector<std::pair<u32, u32>> rows = t.twin_rows;
        pingpong_.lazy.clear();
        if (LazyTilesOn() && t.height % 8 == 0 && !t.twin_rows.empty()) {
            std::vector<std::pair<u32, u32>> covering;  // guest memory of the dirty tiles that could be copied in
            for (const auto& other : targets_) {
                if (other.get() != &t && DirtyOverlaps(*other, t.paddr, t.paddr + t.bytes) && CanCopyTiles(*other, t)) {
                    covering.push_back(CopiedBytes(*other));
                }
            }
            if (!covering.empty()) {
                std::sort(covering.begin(), covering.end());
                std::vector<std::pair<u32, u32>> merged;
                for (const auto& c : covering) {
                    if (!merged.empty() && c.first <= merged.back().second) {
                        merged.back().second = std::max(merged.back().second, c.second);
                    } else {
                        merged.push_back(c);
                    }
                }
                const u32 tile_row = TileRowBytes(t);
                auto lazy_row = [&](u32 g) {  // the row of tiles that starts at guest row g
                    const s32 y0 = static_cast<s32>((t.height - g - 8) * n), y1 = static_cast<s32>((t.height - g) * n);
                    if (y0 < by1 && by0 < y1) return false;  // (the box's rows)
                    if (t.gpu_dirty && t.dirty_first < g + 8 && g < t.dirty_last) return false;  // (its own drawing)
                    const u64 begin = u64{t.paddr} + u64{g / 8} * tile_row, end = begin + tile_row;
                    for (const auto& [b, e] : merged) {
                        if (b <= begin && end <= e) return true;
                    }
                    return false;
                };
                rows.clear();
                for (const auto& [first, last] : t.twin_rows) {
                    for (u32 g = first / 8 * 8; g < last; g += 8) {
                        const u32 a = std::max(g, first), b = std::min(g + 8, last);
                        JoinRows(lazy_row(g) && a == g && b == g + 8 ? pingpong_.lazy : rows, a, b);
                    }
                }
            }
        }
        // (in rows outside twin_wide, the columns from twin_x1 on are the same in both: Target::TwinDiffers;
        // MH4N_AB_FEATURE=columns: the whole rows, as before)
        const bool narrow = !AbOffFor("columns");
        std::vector<std::pair<std::pair<u32, u32>, s32>> spans;  // (guest rows, right edge)
        for (const auto& [first, last] : rows) {
            u32 at = first;
            for (const auto& [wa, wb] : t.twin_wide) {
                if (wb <= at || wa >= last) continue;
                if (wa > at) spans.push_back({{at, wa}, static_cast<s32>(std::min<u64>(w, t.twin_x1))});
                spans.push_back({{std::max(wa, at), std::min(wb, last)}, static_cast<s32>(w)});
                at = std::min(wb, last);
            }
            if (at < last) spans.push_back({{at, last}, static_cast<s32>(std::min<u64>(w, t.twin_x1))});
        }
        for (const auto& [range, right] : spans) {
            const auto [first, last] = range;
            const s32 xr = narrow ? right : static_cast<s32>(w);
            const s32 ya = static_cast<s32>((t.height - std::min(last, t.height)) * n);  // guest row 0 last
            const s32 yb = static_cast<s32>((t.height - std::min(first, t.height)) * n);
            add(0, ya, xr, std::min(yb, by0));                                      // above the box
            add(0, std::max(ya, by1), xr, yb);                                      // below it
            add(0, std::max(ya, by0), std::min(bx0, xr), std::min(yb, by1));        // beside it
            add(bx1, std::max(ya, by0), xr, std::min(yb, by1));
        }
        u64 pixels = 0, lazy_px = 0;
        for (const rhi::ImageCopyRegion& r : regions) pixels += u64{r.width} * r.height;
        for (const auto& [a, b] : pingpong_.lazy) lazy_px += u64{b - a} * n * w;
        frame_.pingpong_lazy_px += lazy_px;
        TraceEventF("pingpong {:08x} {}x{} box {},{} {}x{} copied {} px{}", t.paddr, t.width, t.height, box.x, box.y,
                               box.width, box.height, pixels, lazy_px != 0 ? fmt::format(", left to other targets' tiles {} px", lazy_px) : "");
        if (!regions.empty()) {
            composer_->EndPass(*rec_);
            const rhi::ImageCopyJob job{t.image.get(), t.twin.get(), regions};
            rec_->CopyImages(std::span<const rhi::ImageCopyJob>(&job, 1));
        }
        ++frame_.pingpongs;
    }

    // MH4N_PINGPONG_DISCARD=0: a ping-pong draw's pass loads the twin as any pass does (a measurement aid).
    static bool DiscardPingPongLoads() {
        static const bool on = [] {
            const char* v = std::getenv("MH4N_PINGPONG_DISCARD");
            return v == nullptr || *v != '0';
        }();
        return on;
    }

    // The draw is recorded into the twin: the twin is the target's image now, and the old image the twin, which differs from
    // it only in the rows of the box (the rows the draw marked changed).
    void CommitPingPong(std::pair<u32, u32> draw_rows) {
        Target& t = *pingpong_.target;
        std::swap(t.image, t.twin);
        // (the old image, now the twin, differs from the new one only inside the box)
        t.TwinSame();
        t.TwinDiffers(draw_rows.first, draw_rows.second, static_cast<u32>(pingpong_.box.x) + pingpong_.box.width);
        // (the rows the twin was not given: the image lacks them now, the old one has them; stale in the target, which
        // no longer holds the other targets' tiles there: the next read copies them in or loads them, gpu4)
        for (const auto& [a, b] : pingpong_.lazy) {
            t.TwinDiffers(a, b);
            JoinRows(t.stale_rows, a, b);
            std::erase_if(t.copied, [&](const Target::Copied& c) { return c.first < b && a < c.last; });
        }
        if (t.screen != nullptr) t.screen->box_from = nullptr;  // (as ScreenWrite: OwedCheck)
        if (t.screen != nullptr && t.screen->twin != nullptr) {
            // (a screen target, gpu3: the images swap their places too; an old image a picture holds is not the twin: it
            // goes to the app with the next submission, and the next twin is another one)
            ScreenTarget& s = *t.screen;
            std::swap(s.image, s.twin);
            s.image->released_at = std::make_shared<std::atomic<u64>>(TargetBuffer::kNotYet);
            s.image->sync.reset();
            if (s.twin != nullptr && s.twin->holders->load(std::memory_order_acquire) > 0) {
                TargetBuffer* x = s.twin;
                x->behind = t.twin_rows;
                x->behind_wide = t.twin_wide;
                x->behind_x1 = t.twin_x1;
                x->release_owed = true;
                any_release_owed_ = true;
                s.twin = nullptr;
                t.twin.reset();
                t.TwinSame();
                ++frame_.target_pingpong_handovers;
            }
        }
    }

    // MH4N_SELF_LOG (with MH4N_PASS_TRACE, a measurement aid): a draw that samples its own target, its state and its
    // vertices shaded on the CPU (the vertex unit alone, which keeps no state)
    void LogSelfSampled(const pica::DrawCall& draw, const pica::PicaState& state, const fragment::FragmentKey& key,
                        const fragment::Needs& needs, const rhi::FixedFunctionState& ff, const DrawGeometry& g) {
        const auto& regs = state.regs;
        std::string s = fmt::format(
            "SELF draw kind {} n {} | blend {} {}/{}/{} {}/{}/{} mask {:x} logic {} {} depth {} {} stencil {} | alpha_test {} "
            "scissor_mode {} fbraw {:x} dst_copy {} | viewport {},{} {}x{} scissor {},{} {}x{} | main {:x}",
            static_cast<int>(draw.kind), draw.num_vertices, ff.blend_enabled, static_cast<int>(ff.src_color),
            static_cast<int>(ff.dst_color), static_cast<int>(ff.color_op), static_cast<int>(ff.src_alpha),
            static_cast<int>(ff.dst_alpha), static_cast<int>(ff.alpha_op), ff.color_write_mask, ff.logic_op_enabled,
            static_cast<int>(ff.logic_op), ff.depth_test, ff.depth_write, ff.stencil_test,
            static_cast<int>(key.framebuffer.alpha_test_func()), static_cast<int>(key.framebuffer.scissor_test_mode()),
            key.framebuffer.raw, needs.dst_copy, g.viewport.x, g.viewport.y, g.viewport.width, g.viewport.height, g.scissor.x,
            g.scissor.y, g.scissor.width, g.scissor.height, regs[texture::kMainConfig]);
        const u32 config[3] = {texture::kTexture0, texture::kTexture1, texture::kTexture2};
        for (u32 u = 0; u < 3; ++u) {
            s += fmt::format(" | unit{} size {:x} params {:x} addr {:08x}", u, regs[config[u] + 1], regs[config[u] + 2],
                             pica::BitsOf(regs[config[u] + 4], 0, 28) * 8);
        }
        const cpuvertex::RunStatus ran = runner_.Run(host_, draw, cpuvertex::RunMode::VertexOnly);
        seen_cpu_ = runner_.counters();
        if (ran == cpuvertex::RunStatus::Ok) {
            const auto& v = runner_.output().vertices;
            for (size_t i = 0; i < v.size() && i < 8; ++i) {
                s += fmt::format(" | v{} pos {:.6f},{:.6f},{:.6f},{:.6f} tc0 {:.6f},{:.6f} tc1 {:.6f},{:.6f} tc2 {:.6f},{:.6f} col {:.3f},{:.3f},{:.3f},{:.3f}",
                                 i, v[i].pos[0], v[i].pos[1], v[i].pos[2], v[i].pos[3], v[i].tc0[0], v[i].tc0[1], v[i].tc1[0],
                                 v[i].tc1[1], v[i].tc2[0], v[i].tc2[1], v[i].color[0], v[i].color[1], v[i].color[2], v[i].color[3]);
            }
        } else {
            s += " | (the CPU run failed)";
        }
        MH4N_LOG(Info, kLog, "{}", s);
    }

    // The binding of one unit (design 4.8: a texture and a sampler per unit; vk_rasterizer.cpp's SyncTextureUnits).
    TextureBinding BindUnit(const pica::PicaState& state, u32 unit, const std::vector<texture::TextureRef>& refs) {
        using texture::Type;
        const auto& regs = state.regs;
        const u32 config[3] = {texture::kTexture0, texture::kTexture1, texture::kTexture2};
        const u32 params = regs[config[unit] + 2];
        const Type type = unit == 0 ? static_cast<Type>(pica::BitsOf(params, 28, 3)) : Type::Texture2D;
        const bool cube = type == Type::TextureCube || type == Type::ShadowCube;
        TextureBinding binding;
        if (type == Type::Disabled) {
            return binding;  // a Disabled unit 0 never reads its texture
        }
        if (type == Type::Shadow2D || type == Type::ShadowCube) {
            return BindShadowUnit(state);
        }
        EnsureNullTextures();
        auto null_texture = [&](bool opaque) {
            binding.image = cube ? null_cube_.get() : (opaque ? null_black_2d_.get() : null_2d_.get());
            return binding;
        };
        if (((regs[texture::kMainConfig] >> unit) & 1) == 0) {
            return null_texture(false);  // the unit is off: the software renderer's transparent black
        }
        if (pica::BitsOf(regs[config[unit] + 4], 0, 28) == 0) {
            return null_texture(true);  // an enabled unit with address 0: opaque black
        }
        std::vector<const texture::TextureRef*>& faces = unit_faces_;  // (kept from draw to draw: no allocation)
        faces.clear();
        for (const texture::TextureRef& ref : refs) {
            if (ref.unit == unit) {
                faces.push_back(&ref);
            }
        }
        // A unit 0 type the PICA does not define (6, 7) is sampled as Texture2D (sampling.cpp), but TexturesOfDraw leaves it out:
        // describe the 2D texture from the registers the same way.
        texture::TextureRef undefined_type;
        if (faces.empty() && unit == 0 && (type == static_cast<Type>(6) || type == static_cast<Type>(7))) {
            const u32 size_word = regs[config[0] + 1];
            undefined_type.unit = 0;
            undefined_type.width = pica::BitsOf(size_word, 16, 11);
            undefined_type.height = pica::BitsOf(size_word, 0, 11);
            undefined_type.paddr = pica::BitsOf(regs[config[0] + 4], 0, 28) * 8;
            const u32 format_value = pica::BitsOf(regs[texture::kTexture0Format], 0, 4);
            undefined_type.format = static_cast<texture::Format>(format_value);
            if (texture::IsValidFormat(format_value) && texture::IsValidSize(undefined_type.width, undefined_type.height)) {
                faces.push_back(&undefined_type);
            }
        }
        if (faces.empty() || (cube && faces.size() != 6)) {
            ++frame_.texture_unavailable;
            return null_texture(false);
        }
        // A 2D texture that is exactly a colour target held here samples the target's image (at its scale: the game draws
        // its picture through such textures); any other texture is decoded from guest memory, at 1x.
        rhi::Image* image = options_.target_textures && faces.size() == 1 ? TargetAsTexture(state, *faces[0], unit, type == Type::Texture2D)
                                                                          : nullptr;
        if (image == nullptr) {
            if (ScaleLogLimit() != 0 && scale_ > 1) {
                for (const texture::TextureRef* f : faces) ScaleLogTexture(*f);
            }
            image = TextureImage(faces);
            // MH4N_AB_FEATURE=notex (a measurement aid, not the picture): while the phase is off, the game's textures
            // are the 1x1 null texture, to tell what their reads cost the GPU (the render targets read as textures stay)
            if (image != nullptr && AbOffFor("notex")) {
                return null_texture(true);
            }
        }
        if (image == nullptr) {
            ++frame_.texture_unavailable;
            return null_texture(false);
        }
        rhi::Result<std::shared_ptr<rhi::Sampler>> sampler = samplers_->Get(fragment::sampling::SamplerParamsOf(state, unit));
        if (!sampler.ok()) {
            MH4N_LOG(Error, kLog, "sampler: {}", sampler.error());
            ++frame_.texture_unavailable;
            return null_texture(false);
        }
        frame_.sampler_creates += samplers_->creates() - seen_sampler_creates_;
        seen_sampler_creates_ = samplers_->creates();
        ++frame_.texture_binds;
        // Azahar gives a texture lod.max_level + 1 levels from guest memory; this binds level 0 only.
        if (pica::BitsOf(regs[config[unit] + 3], 16, 4) > 0) {
            levels_missing_in_draw_ = true;
        }
        binding.image = image;
        binding.view = rhi::ImageView::Sampled;
        binding.sampler = sampler->get();
        return binding;
    }

    // A Shadow2D or ShadowCube unit 0 (N2-12, fragment/shadow/shadow.h). Shadow2D: the shadow source at the texture's address, the
    // one the shadow-write pass drew or the memory loaded into one, read through its R32_UINT view. ShadowCube: the six faces as an
    // RGBA8 cube from the texture cache, with no sampler (the composer's nearest one). None (the composer's dummy, lit) for a unit
    // that is off or has address 0, or a map that cannot be had.
    TextureBinding BindShadowUnit(const pica::PicaState& state) {
        TextureBinding binding;
        const std::optional<fragment::shadow::ShadowTexture> shadow = fragment::shadow::ShadowTextureOf(state.regs.data());
        if (!shadow) {
            return binding;
        }
        if (!texture::IsValidFormat(shadow->format) || !texture::IsValidSize(shadow->width, shadow->height) ||
            (!shadow->cube && static_cast<ColorFormat>(shadow->format) != ColorFormat::RGBA8)) {
            ++frame_.texture_unavailable;
            return binding;
        }
        if (!shadow->cube) {
            Target* t = UseTarget(false, shadow->paddr[0], static_cast<u32>(ColorFormat::RGBA8), shadow->width, shadow->height,
                                  /*shadow=*/true);
            if (t == nullptr) {
                ++frame_.texture_unavailable;
                return binding;
            }
            if (!g_shadows.load(std::memory_order_relaxed) && t->off_version != t->version) {
                // shadows off: the map cleared once to "nothing in the way" (depth 0xFFFFFF, stencil 0xFF: every lookup lit), and
                // again only if something else (a load from memory) changed it since
                EnsureRecording();
                composer_->EndPass(*rec_);
                rhi::ClearValue clear;
                clear.color = {1.0f, 1.0f, 1.0f, 1.0f};
                rec_->ClearImage(*t->image, clear);
                t->stale = false;
                t->stale_rows.clear();
                t->Changed(0, t->height);
                t->off_version = t->version;
                if (!shadows_clear_told_) {
                    MH4N_LOG(Info, kLog, "shadows off: the {}x{} shadow map at {:08x} cleared", t->width, t->height, t->paddr);
                    shadows_clear_told_ = true;
                }
            }
            binding.image = t->image.get();
            binding.view = rhi::ImageView::ShadowStorage;
        } else {
            std::array<texture::TextureRef, 6> refs{};
            std::vector<const texture::TextureRef*> faces;
            for (u32 face = 0; face < 6; ++face) {
                refs[face] = texture::TextureRef{0, face, true, shadow->paddr[face], shadow->width, shadow->height,
                                                 static_cast<texture::Format>(shadow->format)};
                faces.push_back(&refs[face]);
            }
            rhi::Image* image = TextureImage(faces);
            if (image == nullptr) {
                ++frame_.texture_unavailable;
                return binding;
            }
            binding.image = image;
            binding.view = rhi::ImageView::Sampled;
        }
        ++frame_.texture_binds;
        return binding;
    }

    void BindTextures(const pica::PicaState& state, const fragment::FragmentKey& key, DrawDesc& desc) {
        (void)key;
        const std::array<bool, 3> used = units_read_;  // (UnitsRead(key), DrawImpl's)
        if (!used[0] && !used[1] && !used[2]) {
            return;
        }
        EnsureRecording();
        binding_textures_ = true;
        levels_missing_in_draw_ = false;
        texture::TexturesOfDraw(state.regs.data(), draw_refs_);  // (into a vector kept from draw to draw)
        for (u32 unit = 0; unit < 3; ++unit) {
            if (used[unit]) {
                desc.units[unit] = BindUnit(state, unit, draw_refs_);
            }
        }
        binding_textures_ = false;
        if (levels_missing_in_draw_) {
            ++frame_.texture_levels_missing;
        }
    }

    // MH4N_GAS_LOG=<n> (a measurement aid): the first n draws in a gas mode (fog mode 7 or fragment operation mode 1, the
    // depth of field's passes) with the registers that drive them
    void GasLog(const pica::DrawCall& draw, const pica::PicaState& state, u32 color, u32 depth, u32 width, u32 height) {
        static const u32 limit = [] {
            const char* v = std::getenv("MH4N_GAS_LOG");
            return v != nullptr ? static_cast<u32>(std::atoi(v)) : 0u;
        }();
        static u32 told = 0, after = 0;
        if (told >= limit) return;
        const auto& r = state.regs;
        const bool fog_gas = (r[0xE0] & 7) == 7, op_gas = (r[0x100] & 3) == 1;
        if (!fog_gas && !op_gas) {
            if (after > 0) {  // (the draws after a shading pass: what reads what it wrote)
                --after;
                MH4N_LOG(Info, kLog, "  after: c {:08x} d {:08x} {}x{} verts {} | 100 {:08x} 101 {:08x} 107 {:08x} 112 {:08x} "
                         "113 {:08x} | tex0 {:08x} {:08x} {:08x} cfg {:08x} | tev0 {:08x} {:08x} {:08x} {:08x}",
                         color, depth, width, height, draw.num_vertices, r[0x100], r[0x101], r[0x107], r[0x112], r[0x113],
                         r[0x85], r[0x82], r[0x8E], r[0x83], r[0xC0], r[0xC1], r[0xC2], r[0xC3]);
            }
            return;
        }
        ++told;
        if (fog_gas && told <= 4) {
            const auto& t = state.gas.lut;
            MH4N_LOG(Info, kLog, "gas table {:08x} {:08x} {:08x} {:08x} {:08x} {:08x} {:08x} {:08x} | {:08x} {:08x} {:08x} {:08x} "
                     "{:08x} {:08x} {:08x} {:08x}", t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7], t[8], t[9], t[10], t[11],
                     t[12], t[13], t[14], t[15]);
            after = 6;
        }
        MH4N_LOG(Info, kLog,
                 "gas draw f{} {}{}: c {:08x} d {:08x} {}x{} verts {} | E0 {:08x} E4 {:08x} E5 {:08x} 100 {:08x} 101 {:08x} "
                 "102 {:08x} 103 {:08x} 104 {:08x} 105 {:08x} 106 {:08x} 107 {:08x} 112 {:08x} 113 {:08x} 114 {:08x} 115 {:08x} "
                 "116 {:08x} 117 {:08x} | 120 {:08x} 121 {:08x} 122 {:08x} 125 {:08x} 126 {:08x} | 4D {:08x} 4E {:08x} | "
                 "tev0 {:08x} {:08x} {:08x} {:08x} {:08x} tevbuf {:08x} {:08x}",
                 pacing::g_game_frame.load(std::memory_order_relaxed), fog_gas ? "shading" : "", op_gas ? "density" : "", color,
                 depth, width, height, draw.num_vertices, r[0xE0], r[0xE4], r[0xE5], r[0x100], r[0x101], r[0x102], r[0x103],
                 r[0x104], r[0x105], r[0x106], r[0x107], r[0x112], r[0x113], r[0x114], r[0x115], r[0x116], r[0x117], r[0x120],
                 r[0x121], r[0x122], r[0x125], r[0x126], r[0x4D], r[0x4E], r[0xC0], r[0xC1], r[0xC2], r[0xC3], r[0xC4], r[0xFD],
                 r[0xE1]);
    }

    // ---- a draw ----
    rhi::Result<DrawResult> DrawImpl(const pica::DrawCall& draw) {
        const pica::PicaState& state = *draw.state;
        const auto& regs = state.regs;
        ++frame_.draws;

        // The primitive assembler applies the draw's register side effects first, whatever happens to the draw.
        assembler_.ApplyRegisterWrites(state);
        if (held_.active) {
            if (JoinHeldCpuDraw(draw, state)) {
                return DrawResult{};
            }
            FlushHeldCpuDraw();
        }
        self_sampled_ = false;
        pingpong_ = PingPong{};
        SubmitAtPassBreak(state);
        EnsureRoom(0);

        // The surfaces the draw names (their registers), and the viewport, scissor and the scissor uniform from them.
        const u32 color_address = pica::BitsOf(regs[kRegColorAddress], 0, 28) * 8;
        const u32 depth_address = pica::BitsOf(regs[kRegDepthAddress], 0, 28) * 8;
        const u32 size_word = regs[kRegFramebufferSize];
        const u32 width = pica::BitsOf(size_word, 0, 11);
        const u32 height = pica::BitsOf(size_word, 12, 10) + 1;
        const bool flipped = pica::BitsOf(size_word, 24, 1) == 0;
        const u32 color_format = pica::BitsOf(regs[kRegColorFormat], 16, 3);
        const u32 depth_format = pica::BitsOf(regs[kRegDepthFormat], 0, 2);
        // A shadow map stays 1x (GetTarget): its write pass, and the ordinary draws into it (the game clears it with one),
        // keep the 3DS's own geometry.
        bool into_shadow_map = (pica::BitsOf(regs[fragment::freg::kFragmentOperation], 0, 2) ==
                                static_cast<u32>(fragment::regenum::FragmentOperationMode::Shadow));
        bool into_known_shadow_map = false;  // (an ordinary draw over the whole of a shadow source: its clear)
        for (const auto& t : targets_) {
            into_shadow_map |= t->shadow && !t->depth && t->paddr == color_address;
            into_known_shadow_map |= t->shadow && !t->depth && t->paddr == color_address && t->width == width && t->height == height;
        }
        const u32 draw_scale = into_shadow_map ? 1 : scale_;
        const DrawGeometry geometry = MakeGeometry(state, width, height, flipped, draw_scale);

        bool runner_saw = false;
        auto drop = [&](const char* why) -> rhi::Result<DrawResult> {
            ++frame_.dropped;
            if (!runner_saw) {
                runner_.NoteDraw(draw);
                runner_saw = true;
            }
            return rhi::Error{why};
        };

        // The fragment state first, for every draw: the dirty flags the processor clears after this draw are read here.
        u64 lap = NowUs();
        const auto section = [&](u64& into) {
            const u64 now = NowUs();
            into += now - lap;
            lap = now;
        };
        if (touch_phase_ && color_address == touch_source_) {  // the hidden touch screen's turn (NoteScreenTransfer)
            // Left out before its fragment state is prepared (a third or more of the draws while the touch screen is hidden):
            // what changed with it is kept for the next draw that is prepared, since the processor clears the flags after
            // every draw (PicaState::ClearDirty)
            for (size_t w = 0; w < touch_dirty_.qwords.size(); ++w) {
                touch_dirty_.qwords[w] |= state.dirty.qwords[w];
            }
            touch_lighting_ |= state.lighting.lut_dirty;
            touch_fog_ = touch_fog_ || state.fog.lut_dirty;
            touch_gas_ = touch_gas_ || state.gas.table_dirty;
            touch_proctex_ = static_cast<u8>(touch_proctex_ | state.proctex.table_dirty);
            touch_pending_ = true;
            if (!runner_saw) {
                runner_.NoteDraw(draw);
                runner_saw = true;
            }
            ++frame_.touch_skipped;
            return rhi::Error{"the touch screen is hidden"};
        }
        if (touch_pending_) {
            // (the draws left out had changed these: the state is the processor's own, which clears them after this draw)
            pica::PicaState& live = const_cast<pica::PicaState&>(state);
            for (size_t w = 0; w < touch_dirty_.qwords.size(); ++w) {
                live.dirty.qwords[w] |= touch_dirty_.qwords[w];
            }
            live.lighting.lut_dirty |= touch_lighting_;
            live.fog.lut_dirty = live.fog.lut_dirty || touch_fog_;
            live.gas.table_dirty = live.gas.table_dirty || touch_gas_;
            live.proctex.table_dirty = static_cast<u8>(live.proctex.table_dirty | touch_proctex_);
            touch_dirty_.Reset();
            touch_lighting_ = 0;
            touch_fog_ = touch_gas_ = false;
            touch_proctex_ = 0;
            touch_pending_ = false;
        }
        rhi::Result<fragment::FragmentState::Prepared> prepared = fragment_state_->Prepare(state, geometry.context, tables_.get());
        if (!prepared.ok()) {
            MH4N_LOG(Error, kLog, "fragment state: {}", prepared.error());
            return drop("the fragment state could not be prepared");
        }
        // (a block changed here and not written, its draw dropped, is written by the next draw that gets that far)
        block_unwritten_ = block_unwritten_ || prepared->block_changed;
        GasLog(draw, state, color_address, depth_address, width, height);
        const fragment::FragmentKey key = fragment_state_->key();
        const fragment::Needs needs = fragment_state_->needs();
        // (what follows from the key alone, again only when it changed: the texture units its shader reads, its stubs)
        if (prepared->key_changed || !key_memo_valid_) {
            units_read_ = UnitsRead(key);
            stub_count_ = fragment::StubbedFeatures(key, *registry_, stub_names_);
            key_memo_valid_ = true;
        }
        for (size_t i = 0; i < stub_count_; ++i) {
            ++frame_.stubbed[stub_names_[i]];
        }

        // The shadow-write pass (N2-12): no attachments; the colour buffer is the shadow map, a shadow source the shader stores into.
        // No depth or stencil test and no blending (the software renderer writes the shadow before the output merger).
        const bool shadow_write = needs.shadow_write_pass;
        // Shadows off (SetShadows): nothing is drawn into a shadow map, neither the shadow-write draws nor the game's clear
        // of it; BindShadowUnit clears it once to "nothing in the way" instead
        const bool shadows = g_shadows.load(std::memory_order_relaxed);
        if (shadows != shadows_were_) {  // (each change is logged once)
            shadows_were_ = shadows;
            shadows_off_told_ = shadows_clear_told_ = false;
        }
        if ((shadow_write || into_known_shadow_map) && !shadows) {
            if (!runner_saw) {
                runner_.NoteDraw(draw);
                runner_saw = true;
            }
            if (!shadows_off_told_) {
                MH4N_LOG(Info, kLog, "shadows off: the draws into the shadow maps are left out");
                shadows_off_told_ = true;
            }
            return rhi::Error{"shadows off"};
        }
        rhi::FixedFunctionState ff = fbops::MakeFixedFunctionState(state);
        if (shadow_write) {
            ff.depth_test = ff.depth_write = ff.stencil_test = false;
            ff.blend_enabled = ff.logic_op_enabled = false;
        }
        const bool wants_depth = ff.depth_test || ff.depth_write || ff.stencil_test;
        if (width == 0 || height == 0 || (color_address == 0 && (shadow_write || !(depth_address != 0 && wants_depth)))) {
            return drop("no surface");
        }

        // ---- the textures the draw reads (before any vertex data is committed: binding one can submit the recording) ----
        DrawDesc desc;
        section(frame_.prep_us);
        self_draw_ = SelfDraw{&draw, &key, &needs, &ff, &geometry, draw_scale};
        BindTextures(state, key, desc);
        self_draw_ = SelfDraw{};
        section(frame_.tex_us);
        if (self_sampled_ && trace_.active && std::getenv("MH4N_SELF_LOG") != nullptr) {
            LogSelfSampled(draw, state, key, needs, ff, MakeGeometry(state, width, height, flipped, draw_scale));
        }
        if (texture_pending_) {
            // a texture past the budget is being decoded on another thread: left out this time, as a draw whose pipeline is
            // not built yet (its picture held, HoldIncomplete)
            texture_pending_ = false;
            DrawResult skipped;
            skipped.skipped = true;
            ++skipped_draws_;
            ++skipped_big_draws_;  // (a texture being decoded: held as before, whatever its size)
            return skipped;
        }

        // ---- the vertices ----
        const cpuvertex::CpuReason cpu_reason = cpuvertex::CpuPathReason(draw, assembler_.IsEmpty());
        bool use_cpu = cpu_reason != cpuvertex::CpuReason::None;
        frame_.cpu_gs += cpu_reason == cpuvertex::CpuReason::GeometryShader;
        frame_.cpu_immediate += cpu_reason == cpuvertex::CpuReason::Immediate;
        frame_.cpu_refused += cpu_reason == cpuvertex::CpuReason::Refused;
        bool cpu_fallback = false;  // N2-16: an accelerated draw shaded on the CPU while its pipeline is built
        bool hold = false;          // a CPU-path draw held back for the next ones to join (JoinHeldCpuDraw)
        vertex::LoadedDraw& loaded = loaded_scratch_;  // its storage kept from draw to draw
        shader::VertexShaderConfig vs_config;
        u64 vs_hash = 0;  // vs_config.Hash(), once a draw
        bool pull_draw = false;  // the lighting fix-up in the vertex stage, which reads its own vertices (PullRange)
        if (!use_cpu) {
            // (the vertices are copied once, into the ring, below; a draw that may take the fix-up in the vertex stage keeps
            // its index list as read: WritePullList copies it from guest memory, and only the other paths need it packed,
            // PackIndices below)
            loader_->LoadInto(host_, draw, loaded, false, needs.fixup_gs && fixup_mode_ == FixupMode::Vs);
            frame_.restart_widened += loader_->counters().restart_widened - seen_restart_widened_;
            seen_restart_widened_ = loader_->counters().restart_widened;
            if (loaded.status != vertex::LoadStatus::Ok) {
                return drop("the vertex data could not be loaded");
            }
            shader::ConfigOptions options;
            // without shaderClipDistance (Mali) Vulkan's own clip volume still cuts at the PICA's near plane
            // (gl_Position.z = -z); only the optional user plane is lost, as in Azahar on those devices
            options.use_clip_planes = backend_->Caps().clip_distance;
            options.exact_arithmetic = exact_shaders_;
            // the fix-up in the vertex stage when the draw can take it (PullRange), else the geometry stage
            pull_draw = needs.fixup_gs && fixup_mode_ == FixupMode::Vs && EnsureFlipPass() && PullRange(loaded);
            options.pull_fixup = pull_draw;
            options.geometry_stage = !pull_draw && DecideFixup(needs.fixup_gs, VertexStageKind::Translated,
                                                                backend_->Caps().geometry_shader,
                                                                FixupGeneratorRegistry::Global().Has()) == FixupDecision::Applied;
            vs_config = shader::MakeVertexShaderConfig(state, &loaded, options);
            vs_hash = vs_config.Hash();
            // The program is translated with its pipeline (PipelineBuilder::Build; under N2-16's provider on a worker). Azahar
            // falls back to the software path when AccelerateDrawBatch fails (a program that does not translate): Failed. A
            // pipeline still being built: the CPU path and the ubershader meanwhile (design 4.5, fallback (ii)).
            // (a lookup under the builder's lock a draw: not for the program of the draw before, registered already; the
            // builder never forgets one)
            if (vs_hash != last_vs_source_) {
                builder_->AddVertexSource(vs_config, state.vs, vs_hash);
                last_vs_source_ = vs_hash;
            }
            const bool big = BigDraw(draw, geometry, width, height, draw_scale);
            const PipelineReadiness readiness = PrepareAccelerated(key, needs, ff, loaded, vs_hash, color_address != 0,
                                                                   depth_address != 0 && wants_depth, depth_format, pull_draw,
                                                                   big);
            if (readiness == PipelineReadiness::Pending) {
                // (neither its pipeline nor the fallback is built yet: left out this time, before any vertex work; the
                // provider's workers build them)
                DrawResult skipped;
                skipped.skipped = true;
                ++skipped_draws_;
                skipped_big_draws_ += big ? 1 : 0;
                return skipped;
            }
            if (readiness != PipelineReadiness::Ready) {
                frame_.translate_failures += readiness == PipelineReadiness::Failed ? 1 : 0;
                frame_.cpu_untranslated += readiness == PipelineReadiness::Failed ? 1 : 0;
                cpu_fallback = readiness == PipelineReadiness::Fallback;
                use_cpu = true;
                pull_draw = false;
            }
        }

        const rhi::Capabilities& caps = backend_->Caps();
        std::vector<rhi::VertexBufferBinding>& vertex_buffers = draw_buffers_;  // (kept from draw to draw)
        vertex_buffers.clear();
        u32 count = 0;
        s32 base_vertex = 0;
        const rhi::Buffer* index_buffer = nullptr;
        u64 index_offset = 0;
        rhi::IndexType index_type = rhi::IndexType::U16;
        rhi::Topology topology = rhi::Topology::TriangleList;

        if (use_cpu) {
            const u64 cpu_start = NowUs();
            const cpuvertex::RunStatus status =
                runner_.Run(host_, draw, cpu_fallback ? cpuvertex::RunMode::VertexOnly : cpuvertex::RunMode::Full);
            frame_.cpu_us += NowUs() - cpu_start;
            runner_saw = true;
            const cpuvertex::Counters& c = runner_.counters();
            frame_.cpu_vertices += c.vertices - seen_cpu_.vertices;
            frame_.cpu_path_draws += c.draws - seen_cpu_.draws;
            frame_.cpu_path_unhandled += c.unhandled - seen_cpu_.unhandled;
            frame_.cpu_path_load_failed += c.load_failed - seen_cpu_.load_failed;
            seen_cpu_ = c;
            if (status != cpuvertex::RunStatus::Ok) {
                return drop("the CPU path did not run the draw");
            }
            batch_.Clear();
            assembler_.SubmitStream(runner_.output(), [&](const cpuvertex::OutputVertex& a, const cpuvertex::OutputVertex& b,
                                                          const cpuvertex::OutputVertex& v) { batch_.AddTriangle(a, b, v); });
            if (cpu_fallback) {
                assembler_.Reset();  // the accelerated draw it stands for leaves the assembler as it found it: empty
            }
            if (batch_.Empty()) {
                return drop("the CPU path made no triangle");
            }
            EnsureRoom(batch_.VertexCount() * cpuvertex::kHardwareVertexStride);
            // held back (its triangles uploaded when it is drawn) unless it reads what it draws or is the stand-in of an
            // accelerated draw
            hold = hold_enabled_ && !cpu_fallback && !trace_.active && !needs.dst_copy && !needs.shadow_write_pass &&
                   !self_sampled_;
            if (hold) {
                count = static_cast<u32>(batch_.VertexCount());
            } else {
                rhi::Result<cpuvertex::StreamedBatch> streamed = cpuvertex::UploadBatch(*vertex_ring_, batch_);
                if (!streamed.ok() || streamed->vertex_count == 0) {
                    return drop("the triangle batch could not be uploaded");
                }
                vertex_buffers.push_back(rhi::VertexBufferBinding{vertex_ring_->GetBuffer().get(), streamed->offset});
                count = streamed->vertex_count;
            }
            vertex_bytes_in_recording_ += batch_.VertexCount() * cpuvertex::kHardwareVertexStride;
            desc.vertex.kind = VertexStageKind::Trivial;
            desc.vertex.program = 0;
            static const rhi::VertexLayout layout = cpuvertex::MakeBatchVertexLayout();  // (the same for every CPU-path draw)
            desc.vertex.input = InputKeyOf(layout);
            desc.fallback_only = cpu_fallback;
        } else {
            runner_.NoteDraw(draw);
            runner_saw = true;
            if (!pull_draw) {
                loader_->PackIndices(loaded);  // (a list LoadInto left as read)
            }
            u64 bytes = pull_draw ? PullBytes() + PullJobBytes() + 16 : loaded.index_data.size();
            for (const vertex::VertexBuffer& b : loaded.buffers) {
                bytes += b.Bytes() + 16;
            }
            EnsureRoom(bytes);
            // The buffers and the index list, or a pull draw's block, in one allocation of the ring (each part 16-byte
            // aligned): one Map, Flush and Commit a draw (each one takes the ring's lock and Commit a recorder lookup).
            const bool upload_indices = loaded.index_type != vertex::IndexType::None && !pull_draw;
            const auto part = [](u64 size) { return (std::max<u64>(size, 16) + 15) & ~u64{15}; };
            u64 upload = upload_indices ? part(loaded.index_data.size()) : 0;
            for (const vertex::VertexBuffer& b : loaded.buffers) {
                upload += part(b.Bytes());
            }
            const u64 pull_at = upload;  // (a pull draw's block, after its buffers)
            if (pull_draw) {
                upload += PullBytes();
            }
            rhi::Result<rhi::RingAllocation> upload_alloc = vertex_ring_->Map(upload, 16);
            if (!upload_alloc.ok()) {
                return drop("the vertex buffers do not fit the ring");
            }
            u64 at = 0;
            for (const vertex::VertexBuffer& b : loaded.buffers) {
                b.CopyTo(upload_alloc->data.data() + at);  // from guest memory
                vertex_buffers.push_back(rhi::VertexBufferBinding{vertex_ring_->GetBuffer().get(), upload_alloc->offset + at});
                at += part(b.Bytes());
            }
            if (upload_indices) {
                std::memcpy(upload_alloc->data.data() + at, loaded.index_data.data(), loaded.index_data.size());
                index_buffer = vertex_ring_->GetBuffer().get();
                index_offset = upload_alloc->offset + at;
            }
            if (pull_draw) {
                // The header (each binding's data in the ring, as bytes) and the corners, which the CPU writes, then the
                // quaternions and the indices, which the compute passes write (vertex_shader.h). The indices as they are
                // without a flip, the first one the largest the flip pass can write: the flip pass overwrites them all, but
                // a driver that finds an indexed draw's range by reading its indices when the draw is recorded (Mali's)
                // then finds the right one, not that of whatever the ring held there before.
                const u32 vertices = static_cast<u32>(pull_high_ - pull_low_ + 1);
                u32* words = reinterpret_cast<u32*>(upload_alloc->data.data() + pull_at);
                for (u32 b = 0; b < shader::kPullHeaderWords; ++b) {
                    words[b] = b < vertex_buffers.size() ? static_cast<u32>(vertex_buffers[b].offset) : 0u;
                }
                WritePullList(loaded, reinterpret_cast<u8*>(words + shader::kPullHeaderWords));
                pull_corners_ = 3 * WritePullTriangles(loaded, words + shader::kPullHeaderWords + pull_list_words_);
                if (pull_prefill_indices_ && pull_corners_ != 0) {
                    // Mali's driver finds an indexed draw's range by reading its indices when the draw is recorded: zeros
                    // and, first, the largest the flip pass can write give it the right one (the vertices are numbered
                    // from the draw's first), not that of whatever the ring held there before
                    u32* indices = words + shader::kPullHeaderWords + pull_list_words_ + pull_table_words_ + 4 * vertices;
                    std::memset(indices, 0, 4 * u64{pull_corners_});
                    indices[0] = static_cast<u32>(pull_high_) * 2 + 1;
                }
            }
            if (!vertex_ring_->Commit(upload).ok()) {  // (which flushes the space on memory that is not coherent)
                return drop("the vertex buffers could not be committed");
            }
            if (upload_indices) {
                index_type = loaded.index_type == vertex::IndexType::U8    ? rhi::IndexType::U8
                             : loaded.index_type == vertex::IndexType::U32 ? rhi::IndexType::U32
                                                                           : rhi::IndexType::U16;
            }
            vertex_bytes_in_recording_ += bytes;
            count = loaded.count;
            base_vertex = loaded.base_vertex;
            topology = ToRhiTopology(loaded.topology);
            desc.vertex.kind = VertexStageKind::Translated;
            desc.vertex.program = vs_hash;
            desc.vertex.input = MakeVertexInput(loaded);
            if (pull_draw) {
                // the draw: an indexed list with no vertex input (its block and RecordFixupPasses' records and tables are
                // in `bytes` already)
                const u32 vertices = static_cast<u32>(pull_high_ - pull_low_ + 1);
                const u32 corners = pull_corners_;
                const u32 header_word = static_cast<u32>((upload_alloc->offset + pull_at) / 4);
                const u32 list_word = header_word + shader::kPullHeaderWords;
                const u32 table_word = list_word + pull_list_words_;
                const u32 quaternions_word = table_word + pull_table_words_;
                const u32 indices_word = quaternions_word + 4 * vertices;
                pull_job_ = PullJob{quaternion_pipeline_, 0, header_word, static_cast<u32>(pull_low_), vertices, quaternions_word,
                                    list_word, corners, indices_word};
                pull_job_.list_form = pull_list_form_;
                pull_job_.base_vertex = pull_base_vertex_;
                pull_job_.table_word = table_word;
                desc.first_instance = header_word;
                vertex_buffers.clear();
                index_buffer = vertex_ring_->GetBuffer().get();
                index_offset = u64{indices_word} * 4;
                index_type = rhi::IndexType::U32;
                count = corners;
                base_vertex = 0;
                topology = rhi::Topology::TriangleList;
                desc.vertex.input = VertexInputKey{};
                ++frame_.pull_draws;
                frame_.pull_vertices += count;
            }
            if (count == 0) {
                return drop("nothing to draw");
            }
        }

        // ---- the targets ----
        section(frame_.vtx_us);
        const std::pair<u32, u32> draw_rows = RowsOf(height, geometry.scissor, draw_scale);  // the rows the draw covers
        Target* color = color_address == 0 ? nullptr
                        : shadow_write     ? UseTarget(false, color_address, static_cast<u32>(ColorFormat::RGBA8), width, height, true,
                                                       draw_rows)
                                           : UseTarget(false, color_address, color_format, width, height, false, draw_rows);
        Target* depth = depth_address != 0 && wants_depth
                            ? UseTarget(true, depth_address, depth_format, width, height, false, draw_rows)
                            : nullptr;
        if (color == nullptr && depth == nullptr) {
            return drop("no render target");
        }
        if (color != nullptr) color->gpu_dirty = true;
        if (depth != nullptr) depth->gpu_dirty = true;
        if (color != nullptr) {
            color->drawn = ++draw_serial_;  // (TargetAsTexture takes the latest of two candidates)
            MarkDirty(*color, geometry.scissor, draw_scale);
        }
        if (depth != nullptr) {
            MarkDirty(*depth, geometry.scissor, draw_scale);
        }
        if (HasScreens() && color != nullptr) {
            ForgetScaledScreens(color->paddr, color->paddr + color->bytes);  // drawn into a framebuffer
        }
        if (depth == nullptr) {
            ff.depth_test = ff.depth_write = ff.stencil_test = false;
        }
        if (color == nullptr) {
            ff.blend_enabled = false;
            ff.logic_op_enabled = false;
        }

        if (geometry.empty) {
            return drop("empty draw rectangle");
        }
        section(frame_.tgt_us);
        EnsureRecording();

        rhi::Result<fragment::FragmentUniformRing::Allocation> block = uniform_ring_->Write(fragment_state_->block(), block_unwritten_);
        if (!block.ok()) {
            FlushRecording();
            EnsureRecording();
            block = uniform_ring_->Write(fragment_state_->block(), true);
            if (!block.ok()) {
                return drop("the fragment uniforms do not fit the ring");
            }
        }
        block_unwritten_ = false;

        // The vertex-stage uniforms: PicaUniformData then VertexUniformData, one ring allocation. The last one is used
        // again while the uniforms have not changed (their serial), within the same recording (its ring space lives
        // until that recording's submission is done).
        const shader::VertexUniformData vs_data = shader::MakeVertexUniformData(state, flipped);
        const bool vs_reuse = state.vs.uniforms_serial != 0 && vs_last_.recording == recording_serial_ &&
                              vs_last_.serial == state.vs.uniforms_serial &&
                              std::memcmp(&vs_last_.data, &vs_data, sizeof(vs_data)) == 0;
        if (!vs_reuse) {
            rhi::Result<rhi::RingAllocation> vs_alloc =
                vs_ring_->Map(kVsUniformAllocation, std::max<u64>(caps.uniform_buffer_offset_alignment, 256));
            if (!vs_alloc.ok()) {
                return drop("the vertex uniforms do not fit the ring");
            }
            // (straight into the ring: the uniforms are 1.6 KB a draw)
            shader::WritePicaUniformData(state.vs, reinterpret_cast<shader::PicaUniformData*>(vs_alloc->data.data()));
            std::memset(vs_alloc->data.data() + sizeof(shader::PicaUniformData), 0, 1792 - sizeof(shader::PicaUniformData));
            std::memcpy(vs_alloc->data.data() + 1792, &vs_data, sizeof(vs_data));
            vs_ring_->GetBuffer()->Flush(vs_alloc->offset, kVsUniformAllocation);
            if (!vs_ring_->Commit(kVsUniformAllocation).ok()) {
                return drop("the vertex uniforms could not be committed");
            }
            vs_last_ = VsUniformsUploaded{vs_alloc->offset, state.vs.uniforms_serial, recording_serial_, vs_data};
        }
        struct {
            u64 offset;
        } vs_alloc_value{vs_last_.offset};
        const auto* vs_alloc = &vs_alloc_value;

        // ---- the dynamic state ----
        rhi::Result<rhi::state::PipelinePlan> plan = rhi::state::PlanPipeline(ff, caps);
        if (!plan.ok()) {
            return drop("the fixed-function state has no plan");
        }
        if (plan->rgb_uses_blend_rgb) {
            // The colour half of the blend that the plan moves into the shader although it is not a Min or Max half
            // (design: the fragment key holds only the Min/Max halves): not generated. Counted; the pipeline draws without it.
            ++frame_.stubbed_blend_plan;
        }
        fbops::DynamicState dynamic = fbops::MakeDynamicState(state);
        if (!dynamic_applied_ || !(dynamic == last_dynamic_) || plan->constants_from_alpha != last_constants_from_alpha_) {
            fbops::ApplyDynamicState(*rec_, dynamic, plan->constants_from_alpha);
            last_dynamic_ = dynamic;
            last_constants_from_alpha_ = plan->constants_from_alpha;
            dynamic_applied_ = true;
        }

        // ---- the draw ----
        desc.fragment = &key;
        desc.needs = needs;
        if (pull_draw) {
            desc.needs.fixup_gs = false;  // done in the vertex stage
        }
        desc.state = ff;
        desc.topology = use_cpu ? rhi::Topology::TriangleList : topology;
        desc.color_format = color != nullptr && !shadow_write ? rhi::Format::Rgba8Unorm : rhi::Format::Undefined;
        desc.depth_format = depth != nullptr ? DepthImageFormat(depth_format) : rhi::Format::Undefined;
        desc.target.color = color != nullptr && !shadow_write ? color->image.get() : nullptr;
        desc.shadow_target = shadow_write ? color->image.get() : nullptr;
        desc.target.depth = depth != nullptr ? depth->image.get() : nullptr;
        if (depth != nullptr && depth->clear_pending) {
            // (PassArea joined the depth target's area into the render area: every pixel drawn since its whole clear)
            desc.target.depth_load = rhi::LoadOp::Clear;
            desc.target.clear_depth = 1.0f;
            desc.target.clear_stencil = 0;
            depth->clear_pending = false;
        }
        desc.target.area = PassArea(color != nullptr && !shadow_write ? color : nullptr, depth, geometry.scissor,
                                    width * draw_scale, height * draw_scale);
        if (LearnedPassAreas() && !shadow_write && desc.target.depth_load != rhi::LoadOp::Clear) {
            // (a pass that clears the depth it owes keeps the whole box drawn so far: its clear must reach every pixel)
            desc.target.area = PassAreaLearned(color, depth, desc, geometry.scissor, desc.target.area);
        }
        desc.vs_pica_uniforms = UniformRange{vs_ring_->GetBuffer().get(), vs_alloc->offset, sizeof(shader::PicaUniformData)};
        desc.vs_uniforms = UniformRange{vs_ring_->GetBuffer().get(), vs_alloc->offset + 1792, sizeof(shader::VertexUniformData)};
        if (pull_draw) {
            // its compute passes' job (RecordFixupPasses), with its PicaUniformData's word in the uniform ring
            pull_job_.uniforms_word = static_cast<u32>(vs_alloc->offset / 4);
            pull_jobs_.push_back(pull_job_);
        }
        desc.fragment_uniforms = *block;
        desc.tables = tables_.get();
        desc.pull_words = vertex_ring_->GetBuffer().get();  // (every draw: one set 0 serves them all)
        desc.vertex_buffers = vertex_buffers;
        desc.index_buffer = index_buffer;
        desc.index_offset = index_offset;
        desc.index_type = index_type;
        desc.count = count;
        desc.base_vertex = base_vertex;
        desc.viewport = geometry.viewport;
        desc.scissor = geometry.scissor;
        // MH4N_DEBUG_TINY_SCISSOR=1 (a measurement aid): every draw cut to an 8x8 square, to tell a GPU bound by its pixels
        static const bool tiny_scissor = [] { const char* v = std::getenv("MH4N_DEBUG_TINY_SCISSOR"); return v != nullptr && *v == '1'; }();
        if ((tiny_scissor || AbTry("tiny")) && desc.scissor) {
            desc.scissor->width = std::min<u32>(desc.scissor->width, 8);
            desc.scissor->height = std::min<u32>(desc.scissor->height, 8);
        }

        EnsureRecording();
        if (KeyStatsEvery() > 0) {
            NoteKeyStats(state, key, ff, desc, pull_draw, use_cpu);
        }
        if (hold && color != nullptr && !shadow_write && color->screen != nullptr) {
            // (a screen target whose image a picture holds gets another one first, gpu3; its pass's area is loaded)
            ScreenWrite(*color, desc.target.area, nullptr, self_sampled_, "a pass (a held draw)");
            desc.target.color = color->image.get();
        }
        if (hold) {
            held_.active = true;
            held_.desc = desc;
            held_.key = key;
            std::swap(held_.batch, batch_);
            ++frame_.cpu_held;
            return DrawResult{};
        }
        // (a ping-pong draw, PingPongBox: into its target's twin, given first what it lacks outside the box)
        const bool pingpong = pingpong_.target != nullptr && pingpong_.target == color && !shadow_write;
        if (pingpong) {
            PreparePingPong();
            desc.target.color = color->twin.get();
            // its pass, begun over its box (when the area is no more than that out to 16 pixels), need not load the twin:
            // the draw writes all of it
            if (DiscardPingPongLoads() && !AbOff() && ShrinkToBox(desc.target.area, pingpong_.box)) {
                desc.target.color_load = rhi::LoadOp::DontCare;
            }
        } else if (!shadow_write && color != nullptr) {
            // (a pass begun by a draw that writes all of it: its colour, and maybe its depth, cleared instead of loaded)
            const SelfDraw first{&draw, &key, &needs, &ff, &geometry, draw_scale};
            FirstDrawClears(first, color, depth, desc);
            if (color->screen != nullptr) {
                // (a screen target whose image a picture holds gets another one first, given what the pass does not clear,
                // gpu3; the twin stays when the draw samples it. A pass that loads, but whose first draw writes all of its box
                // whatever was there, needs nothing there either: MH4U's scene pass when its depth is owed a clear)
                // (that box is also the next frame's expected clear, as FirstDrawClears' is)
                const bool clears = desc.target.color_load == rhi::LoadOp::DontCare;
                std::optional<rhi::Rect> replaced;
                if (!clears && !composer_->Continues(desc) && ReplacesColour(first, true)) {
                    replaced = CoveredBox(first, *color);
                    if (replaced && Pixels(*replaced) > Pixels(color->screen->clear_box_now)) {
                        color->screen->clear_box_now = *replaced;
                    }
                }
                const rhi::Rect* cleared = clears ? &desc.target.area : replaced ? &*replaced : nullptr;
                ScreenWrite(*color, desc.target.area, cleared, self_sampled_, "a pass");
                desc.target.color = color->image.get();
            }
        }
        if (trace_.active) {
            TraceDraw(desc, shadow_write ? nullptr : color, depth, shadow_write ? color : nullptr);
            if (std::getenv("MH4N_DRAW_LOG") != nullptr) {  // (a measurement aid: each draw of the traced window)
                const rhi::Rect& sc = *desc.scissor;
                MH4N_LOG(Info, kLog, "DRAW c {:08x} d {:08x} n {} cpu {} scissor {},{} {}x{} area {},{} {}x{} blend {} {}/{} mask {:x} at {} "
                         "self {} depth {}{} cmp {} stencil {} cull {} units {:x} t0 {:08x} vp {},{} {}x{}",
                         color != nullptr ? color->paddr : 0, depth != nullptr ? depth->paddr : 0, count, use_cpu, sc.x, sc.y,
                         sc.width, sc.height, desc.target.area.x, desc.target.area.y, desc.target.area.width,
                         desc.target.area.height, ff.blend_enabled, static_cast<int>(ff.src_color), static_cast<int>(ff.dst_color),
                         ff.color_write_mask, static_cast<int>(key.framebuffer.alpha_test_func()), self_sampled_, ff.depth_test,
                         ff.depth_write, static_cast<int>(ff.depth_compare), ff.stencil_test, static_cast<int>(ff.cull),
                         regs[texture::kMainConfig] & 7, pica::BitsOf(regs[texture::kTexture0 + 4], 0, 28) * 8,
                         geometry.viewport.x, geometry.viewport.y, geometry.viewport.width, geometry.viewport.height);
                if (ff.stencil_test) {
                    const fbops::DynamicState dyn = fbops::MakeDynamicState(state);
                    MH4N_LOG(Info, kLog, "DRAW   stencil front {}/{}/{} cmp {} back {}/{}/{} cmp {} ref {:x} cmask {:x} wmask {:x}",
                             static_cast<int>(ff.stencil_front.fail), static_cast<int>(ff.stencil_front.pass),
                             static_cast<int>(ff.stencil_front.depth_fail), static_cast<int>(ff.stencil_front.compare),
                             static_cast<int>(ff.stencil_back.fail), static_cast<int>(ff.stencil_back.pass),
                             static_cast<int>(ff.stencil_back.depth_fail), static_cast<int>(ff.stencil_back.compare),
                             dyn.stencil_reference, dyn.stencil_compare_mask, dyn.stencil_write_mask);
                }
                // (a quad of the translated vertex stage: its vertices shaded on the CPU, position and colour)
                if (!use_cpu && count <= 6 &&
                    runner_.Run(host_, draw, cpuvertex::RunMode::VertexOnly) == cpuvertex::RunStatus::Ok) {
                    seen_cpu_ = runner_.counters();
                    std::string v;
                    for (const cpuvertex::OutputVertex& o : runner_.output().vertices) {
                        v += fmt::format(" ({:.4g},{:.4g},{:.4g},{:.4g} c {:.4g},{:.4g},{:.4g},{:.4g})", o.pos[0], o.pos[1], o.pos[2],
                                         o.pos[3], o.color[0], o.color[1], o.color[2], o.color[3]);
                    }
                    MH4N_LOG(Info, kLog, "DRAW   key {:016x} vertices{}", fragment::HashKey(key), v);
                }
            }
        }
        FragStatsTag(key, color, depth);
        rhi::Result<DrawResult> result = composer_->Draw(*rec_, desc);
        FragStatsTag();
        if (pingpong && result.ok() && !result->skipped) {
            CommitPingPong(draw_rows);
        }
        if (result.ok() && !result->skipped) {
            last_draw_target_ = LastDrawTarget{color_address, depth_address, desc.target.color, desc.target.depth, true};
        }
        pingpong_ = PingPong{};
        section(frame_.compose_us);
        if (!result.ok()) {
            MH4N_LOG(Error, kLog, "draw: {}", result.error());
            ++frame_.dropped;
            return result;
        }
        skipped_draws_ += result->skipped ? 1 : 0;
        ++draws_in_recording_;
        frame_.fixup_draws += result->fixup ? 1 : 0;
        trace_.fixups += result->fixup && trace_.active ? 1 : 0;
        return result;
    }

    // The render area of a draw's pass: the boxes its targets' draws have covered so far, this one's scissor included,
    // out to 16 pixels (MH4N_FULL_AREA=1: the whole image, as before).
    static rhi::Rect PassArea(Target* color, Target* depth, const rhi::Rect& scissor, u32 image_width, u32 image_height) {
        static const bool full = [] { const char* v = std::getenv("MH4N_FULL_AREA"); return v != nullptr && *v == '1'; }();
        if (full) {
            return rhi::Rect{0, 0, image_width, image_height};
        }
        const s32 x0 = std::max(scissor.x, 0) / 16 * 16, y0 = std::max(scissor.y, 0) / 16 * 16;
        const u32 x1 = std::min(image_width, (static_cast<u32>(std::max(scissor.x, 0)) + scissor.width + 15) / 16 * 16);
        const u32 y1 = std::min(image_height, (static_cast<u32>(std::max(scissor.y, 0)) + scissor.height + 15) / 16 * 16);
        rhi::Rect box{x0, y0, x1 > static_cast<u32>(x0) ? x1 - x0 : 0, y1 > static_cast<u32>(y0) ? y1 - y0 : 0};
        auto join = [](rhi::Rect& a, const rhi::Rect& b) {
            if (b.width == 0 || b.height == 0) return;
            if (a.width == 0 || a.height == 0) {
                a = b;
                return;
            }
            const s32 ax1 = a.x + static_cast<s32>(a.width), ay1 = a.y + static_cast<s32>(a.height);
            const s32 bx1 = b.x + static_cast<s32>(b.width), by1 = b.y + static_cast<s32>(b.height);
            a.x = std::min(a.x, b.x), a.y = std::min(a.y, b.y);
            a.width = static_cast<u32>(std::max(ax1, bx1) - a.x), a.height = static_cast<u32>(std::max(ay1, by1) - a.y);
        };
        rhi::Rect area{};
        for (Target* t : {color, depth}) {
            if (t != nullptr) {
                join(t->area, box);
                join(area, t->area);
            }
        }
        if (area.width == 0 || area.height == 0) {
            return rhi::Rect{0, 0, image_width, image_height};
        }
        if (area.x < 0 || area.y < 0 || area.x + area.width > image_width || area.y + area.height > image_height) {
            // a box kept from another image of the target (GetTarget forgets it when it makes a new one): logged once,
            // and the pass cut to the image instead of the backend's assertion
            static bool told = false;
            if (!told) {
                told = true;
                MH4N_LOG(Warning, kLog, "pass area {},{} {}x{} outside the {}x{} image (colour box {}x{} at {}x, depth box {}x{} at {}x)",
                         area.x, area.y, area.width, area.height, image_width, image_height,
                         color ? color->area.width : 0u, color ? color->area.height : 0u, color ? color->scale : 0u,
                         depth ? depth->area.width : 0u, depth ? depth->area.height : 0u, depth ? depth->scale : 0u);
            }
            const s32 x0 = std::clamp<s32>(area.x, 0, static_cast<s32>(image_width));
            const s32 y0 = std::clamp<s32>(area.y, 0, static_cast<s32>(image_height));
            const s32 x1 = std::clamp<s32>(area.x + static_cast<s32>(area.width), x0, static_cast<s32>(image_width));
            const s32 y1 = std::clamp<s32>(area.y + static_cast<s32>(area.height), y0, static_cast<s32>(image_height));
            area = rhi::Rect{x0, y0, static_cast<u32>(x1 - x0), static_cast<u32>(y1 - y0)};
            if (area.width == 0 || area.height == 0) {
                return rhi::Rect{0, 0, image_width, image_height};
            }
        }
        return area;
    }

    // A pass loads and stores only the box its own draws cover, not the box every draw into the
    // target has covered (PassArea). The box of a pass is known only when it ends, so it is learned: the passes into a
    // target come in the same order between two display transfers, and the n-th one starts with the box the n-th one
    // covered two orders back (the same screen's), its first draw's box added. A draw outside the open pass's area makes a
    // new pass with both (the order's box grows, so it happens once). Outside a pass's area nothing is loaded, stored
    // or drawn: the attachments keep what they hold there. `whole` is PassArea's box, the fallback.
    // MH4N_PASS_AREA=union: PassArea's box for every pass, as before (a measurement aid).
    static bool LearnedPassAreas() {
        static const bool learned = [] {
            const char* v = std::getenv("MH4N_PASS_AREA");
            return v == nullptr || std::string_view(v) != "union";
        }();
        return learned;
    }

    rhi::Rect PassAreaLearned(Target* color, Target* depth, const DrawDesc& desc, const rhi::Rect& scissor, const rhi::Rect& whole) {
        Target* t = color != nullptr ? color : depth;
        if (t == nullptr) {
            return whole;
        }
        const u32 image_width = t->image->Width(), image_height = t->image->Height();
        const s32 x0 = std::max(scissor.x, 0) / 16 * 16, y0 = std::max(scissor.y, 0) / 16 * 16;
        const u32 x1 = std::min(image_width, (static_cast<u32>(std::max(scissor.x, 0)) + scissor.width + 15) / 16 * 16);
        const u32 y1 = std::min(image_height, (static_cast<u32>(std::max(scissor.y, 0)) + scissor.height + 15) / 16 * 16);
        if (x1 <= static_cast<u32>(x0) || y1 <= static_cast<u32>(y0)) {
            return whole;
        }
        const rhi::Rect box{x0, y0, x1 - static_cast<u32>(x0), y1 - static_cast<u32>(y0)};
        auto join = [](rhi::Rect a, const rhi::Rect& b) {
            if (b.width == 0 || b.height == 0) return a;
            if (a.width == 0 || a.height == 0) return b;
            const s32 ax1 = a.x + static_cast<s32>(a.width), ay1 = a.y + static_cast<s32>(a.height);
            const s32 bx1 = b.x + static_cast<s32>(b.width), by1 = b.y + static_cast<s32>(b.height);
            const s32 nx = std::min(a.x, b.x), ny = std::min(a.y, b.y);
            return rhi::Rect{nx, ny, static_cast<u32>(std::max(ax1, bx1) - nx), static_cast<u32>(std::max(ay1, by1) - ny)};
        };
        auto inside = [](const rhi::Rect& b, const rhi::Rect& a) {
            return b.x >= a.x && b.y >= a.y && b.x + static_cast<s32>(b.width) <= a.x + static_cast<s32>(a.width) &&
                   b.y + static_cast<s32>(b.height) <= a.y + static_cast<s32>(a.height);
        };
        const std::optional<rhi::Rect> open = composer_->OpenArea(desc.target.color, desc.target.depth);
        // (its scissor, not its box out to 16 pixels: a pass begun over a draw's exact box, FirstDrawClears, goes on with
        // the draws inside it)
        const rhi::Rect exact{std::max(scissor.x, 0), std::max(scissor.y, 0), scissor.width, scissor.height};
        if (open && (inside(box, *open) || inside(exact, *open))) {
            return *open;  // the pass goes on
        }
        u32 index;
        rhi::Rect area;
        if (open) {
            // the open pass's draws went outside its box: the same pass in the order, again with both
            index = t->pass_index > 0 ? t->pass_index - 1 : 0;
            area = join(*open, box);
        } else {
            index = t->pass_index++;
            area = box;
        }
        if (index >= 64) {
            return whole;  // (an order this long is not a frame's: the box of everything)
        }
        if (t->pass_boxes.size() <= index) {
            t->pass_boxes.resize(index + 1);
        }
        if (!open && index < t->pass_boxes_before2.size()) {
            area = join(area, t->pass_boxes_before2[index]);  // the box the same pass had two orders back
        }
        area = join(area, t->pass_boxes[index]);
        // (cut to the image: a box learned on another image of the target, made anew at another size, is wider)
        const s32 ax1 = std::min<s32>(area.x + static_cast<s32>(area.width), static_cast<s32>(image_width));
        const s32 ay1 = std::min<s32>(area.y + static_cast<s32>(area.height), static_cast<s32>(image_height));
        area.x = std::max(area.x, 0);
        area.y = std::max(area.y, 0);
        if (ax1 <= area.x || ay1 <= area.y) {
            return whole;
        }
        area.width = static_cast<u32>(ax1 - area.x);
        area.height = static_cast<u32>(ay1 - area.y);
        t->pass_boxes[index] = area;
        return area;
    }

    // ---- MH4N_KEY_STATS=N (a measurement aid) ----
    // The draws of each fragment key since the start, with the pixels of their scissor boxes (an upper bound of what
    // they shade) and what the fragment stage's depth and discards depend on; the 40 keys with the most pixels are
    // logged at every N-th frame counter take (MH4N_GPU_COUNTERS=1 takes them every frame). With MH4N_DUMP_GLSL=DIR
    // the fragment stages are in DIR as fs_<key>.frag.
    static int KeyStatsEvery() {
        static const int every = [] { const char* v = std::getenv("MH4N_KEY_STATS"); return v ? std::atoi(v) : 0; }();
        return every;
    }
    struct KeyStat {
        u64 draws = 0, pixels = 0, depth_identity = 0, depth_test = 0, depth_write = 0, blend = 0, cpu = 0, pull = 0;
        u32 scale_raw = 0, offset_raw = 0, fb_raw = 0, light = 0, tex = 0;
    };
    std::map<u64, KeyStat> key_stats_;
    std::map<u32, std::pair<u64, u64>> blend_stats_;  // blend state (factors and ops, 4 bits each; 0: off) -> draws, pixels
    u64 key_stats_takes_ = 0;

    void NoteKeyStats(const pica::PicaState& state, const fragment::FragmentKey& key, const rhi::FixedFunctionState& ff,
                      const DrawDesc& desc, bool pull_draw, bool cpu) {
        KeyStat& s = key_stats_[fragment::HashKey(key)];
        ++s.draws;
        if (desc.scissor) {
            s.pixels += static_cast<u64>(desc.scissor->width) * desc.scissor->height;
        }
        const u32 scale_raw = pica::BitsOf(state.regs[fragment::freg::kViewportDepthRange], 0, 24);
        const u32 offset_raw = pica::BitsOf(state.regs[fragment::freg::kViewportDepthNearPlane], 0, 24);
        s.scale_raw = scale_raw;
        s.offset_raw = offset_raw;
        s.depth_identity += pica::Float24ToFloat(scale_raw) == -1.0f && pica::Float24ToFloat(offset_raw) == 0.0f;
        s.depth_test += ff.depth_test;
        s.depth_write += ff.depth_write;
        s.blend += ff.blend_enabled;
        s.cpu += cpu;
        s.pull += pull_draw;
        s.fb_raw = key.framebuffer.raw;
        s.light = key.lighting.raw;
        s.tex = key.texture.raw;
        const auto b = [](auto v) { return static_cast<u32>(v) & 15u; };
        const u32 blend = !ff.blend_enabled ? 0u
                                            : (1u << 24) | (b(ff.src_color) << 20) | (b(ff.dst_color) << 16) | (b(ff.color_op) << 12) |
                                                  (b(ff.src_alpha) << 8) | (b(ff.dst_alpha) << 4) | b(ff.alpha_op);
        auto& bs = blend_stats_[blend];
        ++bs.first;
        bs.second += desc.scissor ? static_cast<u64>(desc.scissor->width) * desc.scissor->height : 0;
    }

    void LogKeyStats() {
        if (KeyStatsEvery() <= 0 || ++key_stats_takes_ % static_cast<u64>(KeyStatsEvery()) != 0) {
            return;
        }
        std::vector<std::pair<u64, KeyStat>> all(key_stats_.begin(), key_stats_.end());
        std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.second.pixels > b.second.pixels; });
        u64 total = 0, identity_px = 0, wbuf_px = 0, scissor_px = 0, alpha_px = 0;
        for (const auto& [hash, s] : all) {
            total += s.pixels;
            identity_px += s.draws != 0 ? s.pixels * s.depth_identity / s.draws : 0;
            wbuf_px += pica::BitsOf(s.fb_raw, 5, 1) == 0 ? s.pixels : 0;
            scissor_px += pica::BitsOf(s.fb_raw, 3, 2) != 0 ? s.pixels : 0;
            alpha_px += pica::BitsOf(s.fb_raw, 0, 3) != 1 ? s.pixels : 0;
        }
        MH4N_LOG(Info, kLog, "key stats: {} keys, {} px; depth identity {} px, w-buffer {} px, scissor test {} px, alpha test {} px",
                 all.size(), total, identity_px, wbuf_px, scissor_px, alpha_px);
        for (const auto& [blend, n] : blend_stats_) {
            MH4N_LOG(Info, kLog, "blend {:07x} (on, src dst op colour, src dst op alpha): draws {} px {}", blend, n.first, n.second);
        }
        for (size_t i = 0; i < all.size() && i < 40; ++i) {
            const KeyStat& s = all[i].second;
            MH4N_LOG(Info, kLog,
                     "key {:016x}: draws {} px {} ({:.1f}%) depth id {} test {} write {} blend {} cpu {} pull {} scale {:06x} "
                     "offset {:06x} fb {:03x} light {:08x} tex {:05x}",
                     all[i].first, s.draws, s.pixels, total ? 100.0 * s.pixels / total : 0.0, s.depth_identity, s.depth_test,
                     s.depth_write, s.blend, s.cpu, s.pull, s.scale_raw, s.offset_raw, s.fb_raw, s.light, s.tex);
        }
    }

    // ---- CPU-path draws joined (MH4N_CPU_JOIN=0: each drawn alone, as before) ----
    // A CPU-path draw (the PICA's geometry shader, immediate mode: its triangles made on the CPU, drawn with the trivial
    // vertex stage) is held back after everything else it needs is recorded. The next draw joins it when it is a
    // CPU-path draw too and wrote no register the GPU side reads since (all of them are below 0x200: the vertex
    // loaders and the shader units, 0x200 and up, are the CPU path's alone): its triangles are added to the held
    // draw's, in order, and nothing is recorded for it. Anything else draws the held one first: any other draw, and
    // every entry point that records or reads (a GX command, a register write, a flush, a submit, a screen). Most of
    // MH4U's draws are such draws (particles, effects): over half of them join the one before.
    bool JoinHeldCpuDraw(const pica::DrawCall& draw, const pica::PicaState& state) {
        // Registers 0x000-0x1FF but these: the vertex shader's output map (0x04F-0x056, read by the CPU path itself) and the
        // framebuffer's cache triggers (0x110 invalidate, 0x111 flush: MH4U writes them before every draw; they hold no
        // state, and nothing here acts on them).
        constexpr u64 kOutputMap = ((1ull << 8) - 1) << (0x04F - 0x040);
        constexpr u64 kCacheTriggers = 3ull << (0x110 - 0x100);
        for (u32 q = 0; q < 8; ++q) {
            const u64 ignored = q == 1 ? kOutputMap : q == 4 ? kCacheTriggers : 0;
            if ((state.dirty.qwords[q] & ~ignored) != 0) {
                return false;
            }
        }
        const cpuvertex::CpuReason reason = cpuvertex::CpuPathReason(draw, assembler_.IsEmpty());
        if (reason == cpuvertex::CpuReason::None) {
            return false;
        }
        if (vertex_bytes_in_recording_ > kVertexRingBytes / 4) {
            return false;  // (the held batch is uploaded without EnsureRoom)
        }
        const u64 cpu_start = NowUs();
        const cpuvertex::RunStatus status = runner_.Run(host_, draw, cpuvertex::RunMode::Full);
        frame_.cpu_us += NowUs() - cpu_start;
        const cpuvertex::Counters& c = runner_.counters();
        frame_.cpu_vertices += c.vertices - seen_cpu_.vertices;
        frame_.cpu_path_draws += c.draws - seen_cpu_.draws;
        frame_.cpu_path_unhandled += c.unhandled - seen_cpu_.unhandled;
        frame_.cpu_path_load_failed += c.load_failed - seen_cpu_.load_failed;
        seen_cpu_ = c;
        frame_.cpu_gs += reason == cpuvertex::CpuReason::GeometryShader;
        frame_.cpu_immediate += reason == cpuvertex::CpuReason::Immediate;
        frame_.cpu_refused += reason == cpuvertex::CpuReason::Refused;
        if (status != cpuvertex::RunStatus::Ok) {
            ++frame_.dropped;  // (as the draw would be alone)
            return true;
        }
        const size_t before = held_.batch.VertexCount();
        assembler_.SubmitStream(runner_.output(), [&](const cpuvertex::OutputVertex& a, const cpuvertex::OutputVertex& b,
                                                      const cpuvertex::OutputVertex& v) { held_.batch.AddTriangle(a, b, v); });
        vertex_bytes_in_recording_ += (held_.batch.VertexCount() - before) * cpuvertex::kHardwareVertexStride;
        ++frame_.cpu_joined;
        return true;
    }

    // The held CPU-path draw, recorded now with every triangle that joined it.
    void FlushHeldCpuDraw() {
        if (!held_.active) {
            return;
        }
        held_.active = false;
        if (!rec_) {
            return;  // (never: the held draw's recording is submitted only after this)
        }
        rhi::Result<cpuvertex::StreamedBatch> streamed = cpuvertex::UploadBatch(*vertex_ring_, held_.batch);
        if (!streamed.ok() || streamed->vertex_count == 0) {
            ++frame_.dropped;
            MH4N_LOG(Error, kLog, "a held CPU-path draw could not be uploaded");
            return;
        }
        held_buffers_.assign(1, rhi::VertexBufferBinding{vertex_ring_->GetBuffer().get(), streamed->offset});
        held_.desc.vertex_buffers = held_buffers_;
        held_.desc.count = streamed->vertex_count;
        held_.desc.fragment = &held_.key;
        FragStatsTag(held_.key, nullptr, nullptr);
        rhi::Result<DrawResult> result = composer_->Draw(*rec_, held_.desc);
        FragStatsTag();
        if (!result.ok()) {
            MH4N_LOG(Error, kLog, "draw: {}", result.error());
            ++frame_.dropped;
            return;
        }
        skipped_draws_ += result->skipped ? 1 : 0;
        ++draws_in_recording_;
    }

    // ---- MH4N_FRAG_STATS=f<frame>,N (a measurement aid, for a desktop GPU) ----
    // From the first display transfer once the game's frame counter reaches <frame>, for N transfers, every draw is
    // inside a pipeline statistics query (fragment shader invocations, rhi::DrawQueries): the fragments each fragment
    // key shaded, by colour target, logged at the end (`FS key <hash> c <target>: draws D inv I`), for weighting an
    // offline compiler's cycles of MH4N_DUMP_GLSL's shaders. The draws the PICA did not ask for (copies, tiles,
    // screens) are key 0.
    static constexpr u32 kFragQueries = 65536;
    struct FragStats {
        bool configured = false, active = false;
        u32 from_frame = 0;
        u64 left = 0;
        VkQueryPool pool = VK_NULL_HANDLE;
        u64 tag = 0;
        u32 target = 0, depth = 0;
        std::vector<std::tuple<u64, u32, u32>> tags;                  // per query: the key's hash, colour and depth targets
        std::map<std::tuple<u64, u32, u32>, std::pair<u64, u64>> totals;  // draws, invocations
    };
    FragStats frag_;

    void FragStatsTag(const fragment::FragmentKey& key, const Target* color, const Target* depth) {
        if (frag_.active) {
            frag_.tag = fragment::HashKey(key);
            frag_.target = color != nullptr ? color->paddr : 0;
            frag_.depth = depth != nullptr ? depth->paddr : 0;
        }
    }
    void FragStatsTag() {
        frag_.tag = 0;
        frag_.target = 0;
        frag_.depth = 0;
    }

    static u32 FragStatsNext(void* user) {
        FragStats& f = *static_cast<FragStats*>(user);
        if (!f.active || f.tags.size() >= kFragQueries) {
            return UINT32_MAX;
        }
        f.tags.emplace_back(f.tag, f.target, f.depth);
        return static_cast<u32>(f.tags.size() - 1);
    }

    void FragStatsTransfer() {
        if (!frag_.configured) {
            frag_.configured = true;
            unsigned long long from = 0, count = 0;
            const char* v = std::getenv("MH4N_FRAG_STATS");
            if (v != nullptr && *v == 'f' && std::sscanf(v + 1, "%llu,%llu", &from, &count) == 2 && count > 0) {
                frag_.from_frame = static_cast<u32>(std::max<unsigned long long>(from, 1));
                frag_.left = count;
            }
        }
        if (frag_.active) {
            --frag_.left;
            if (frag_.left == 0 || frag_.tags.size() + 4096 > kFragQueries) {
                FragStatsRead();
                if (frag_.left == 0) {
                    FragStatsLog();
                    return;
                }
            } else {
                return;
            }
        }
        if (frag_.left == 0 || frag_.from_frame == 0 || pacing::g_game_frame.load(std::memory_order_relaxed) < frag_.from_frame) {
            return;
        }
        if (frag_.pool == VK_NULL_HANDLE) {
            VkQueryPoolCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            info.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
            info.queryCount = kFragQueries;
            info.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
            if (vkCreateQueryPool(rhi::Handles(*backend_).device, &info, nullptr, &frag_.pool) != VK_SUCCESS) {
                frag_.pool = VK_NULL_HANDLE;
                frag_.left = 0;
                MH4N_LOG(Error, kLog, "MH4N_FRAG_STATS: no pipeline statistics query pool");
                return;
            }
        }
        EnsureRecording();
        composer_->EndPass(*rec_);
        vkCmdResetQueryPool(rhi::CommandBuffer(*rec_), frag_.pool, 0, kFragQueries);
        frag_.tags.clear();
        frag_.active = true;
        rhi::DrawQueries() = rhi::DrawQueryHook{reinterpret_cast<u64>(frag_.pool), &FragStatsNext, &frag_};
    }

    void FragStatsRead() {
        rhi::DrawQueries() = rhi::DrawQueryHook{};
        frag_.active = false;
        FlushRecording();
        backend_->WaitIdle();
        const u32 count = static_cast<u32>(frag_.tags.size());
        std::vector<u64> values(count, 0);
        if (count != 0) {
            const VkResult r = vkGetQueryPoolResults(rhi::Handles(*backend_).device, frag_.pool, 0, count, count * sizeof(u64),
                                                     values.data(), sizeof(u64), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
            if (r != VK_SUCCESS) {
                MH4N_LOG(Error, kLog, "MH4N_FRAG_STATS: the queries could not be read ({})", static_cast<int>(r));
            }
        }
        for (u32 i = 0; i < count; ++i) {
            auto& t = frag_.totals[frag_.tags[i]];
            ++t.first;
            t.second += values[i];
        }
        frag_.tags.clear();
    }

    void FragStatsLog() {
        std::vector<std::pair<std::tuple<u64, u32, u32>, std::pair<u64, u64>>> all(frag_.totals.begin(), frag_.totals.end());
        std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.second.second > b.second.second; });
        u64 total = 0;
        std::map<u32, u64> by_target;
        for (const auto& [tag, v] : all) {
            total += v.second;
            by_target[std::get<1>(tag)] += v.second;
        }
        MH4N_LOG(Info, kLog, "MH4N_FRAG_STATS: {} keys and targets, {} fragment invocations", all.size(), total);
        for (const auto& [target, inv] : by_target) {
            MH4N_LOG(Info, kLog, "FS target {:08x}: inv {}", target, inv);
        }
        for (const auto& [tag, v] : all) {
            MH4N_LOG(Info, kLog, "FS key {:016x} c {:08x} d {:08x}: draws {} inv {}", std::get<0>(tag), std::get<1>(tag),
                     std::get<2>(tag), v.first, v.second);
        }
        frag_.totals.clear();
    }

    // ---- MH4N_PASS_TRACE=F,N (a measurement aid) ----
    // From the F-th display transfer (each screen's counts) for N of them, what the renderer records on the GPU is
    // logged: each render pass (attachments, render area, draws, the box their scissors cover), copy, load, clear,
    // write-back, upload and submit, with the GPU time from it to the next one (a timestamp at the bottom of the pipe
    // before each, read when the window closes, after a wait for the GPU). The timestamps are written outside passes:
    // the trace ends a pass only where a new one would begin anyway (a composer draw's own destination copy is inside
    // its pass's time, marked "dst-copy").
    static constexpr u32 kTraceQueries = 8192;
    struct PassTrace {
        bool configured = false;
        u64 from = ~0ull, to = 0, transfers = 0;
        // MH4N_PASS_TRACE=f<frame>,N: from the first transfer once the game's frame counter (pacing::g_game_frame) has
        // reached <frame>
        u32 from_frame = 0;
        bool active = false;
        VkQueryPool pool = VK_NULL_HANDLE;
        std::vector<std::string> events;  // event i begins at timestamp i
        size_t open = SIZE_MAX;           // the event of the pass being traced
        u32 draws = 0, dst_copies = 0, fixups = 0;
        u64 pixels = 0;
        s32 x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    };

    void TraceTransfer(u32 input, u32 output) {
        if (!trace_.configured) {
            trace_.configured = true;
            if (const char* v = std::getenv("MH4N_PASS_TRACE")) {
                unsigned long long from = 0, count = 0;
                if (*v == 'f' && std::sscanf(v + 1, "%llu,%llu", &from, &count) == 2 && count > 0) {
                    trace_.from_frame = static_cast<u32>(std::max<unsigned long long>(from, 1));
                    trace_.to = count;  // (a count until the frame is reached)
                } else if (std::sscanf(v, "%llu,%llu", &from, &count) == 2 && count > 0) {
                    trace_.from = from;
                    trace_.to = from + count;
                }
            }
        }
        ++trace_.transfers;
        // (with MH4N_AB, in a phase with everything on)
        if (trace_.from_frame != 0 && pacing::g_game_frame.load(std::memory_order_relaxed) >= trace_.from_frame &&
            AbPhase() == 0) {
            MH4N_LOG(Info, kLog, "MH4N_PASS_TRACE: game frame {} at transfer {}", pacing::g_game_frame.load(), trace_.transfers);
            trace_.from = trace_.transfers;
            trace_.to += trace_.from;
            trace_.from_frame = 0;
        }
        if (!trace_.active && trace_.transfers == trace_.from) {
            TraceStart();
        }
        TraceEventF("transfer {} {:08x} -> {:08x}", trace_.transfers, input, output);
        if (trace_.active && trace_.transfers >= trace_.to) {
            TraceFinish();
        }
    }

    void TraceStart() {
        if (trace_.pool == VK_NULL_HANDLE) {
            VkQueryPoolCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            info.queryType = VK_QUERY_TYPE_TIMESTAMP;
            info.queryCount = kTraceQueries;
            if (vkCreateQueryPool(rhi::Handles(*backend_).device, &info, nullptr, &trace_.pool) != VK_SUCCESS) {
                trace_.pool = VK_NULL_HANDLE;
                MH4N_LOG(Error, kLog, "MH4N_PASS_TRACE: no query pool");
                return;
            }
        }
        EnsureRecording();
        composer_->EndPass(*rec_);
        vkCmdResetQueryPool(rhi::CommandBuffer(*rec_), trace_.pool, 0, kTraceQueries);
        trace_.events.clear();
        trace_.open = SIZE_MAX;
        trace_.active = true;
    }

    void TraceClosePass() {
        if (trace_.open == SIZE_MAX) {
            return;
        }
        trace_.events[trace_.open] += fmt::format(" | draws {} box {},{}-{},{} px {}{}{}", trace_.draws, trace_.x0, trace_.y0,
                                                  trace_.x1, trace_.y1, trace_.pixels,
                                                  trace_.dst_copies != 0 ? fmt::format(" dst-copy {}", trace_.dst_copies) : "",
                                                  trace_.fixups != 0 ? fmt::format(" fix-up {}", trace_.fixups) : "");
        trace_.open = SIZE_MAX;
    }

    // An event at this point of the recording (outside a pass); its index, SIZE_MAX when not tracing.
    // (the event formatted only when the trace takes it: most of these come with every draw or transfer)
    template <typename... Args>
    size_t TraceEventF(fmt::format_string<Args...> format, Args&&... args) {
        if (!trace_.active || trace_.events.size() >= kTraceQueries) {
            return SIZE_MAX;
        }
        return TraceEvent(fmt::format(format, std::forward<Args>(args)...));
    }

    size_t TraceEvent(std::string what) {
        if (!trace_.active || trace_.events.size() >= kTraceQueries) {
            return SIZE_MAX;
        }
        TraceClosePass();
        EnsureRecording();
        composer_->EndPass(*rec_);
        vkCmdWriteTimestamp(rhi::CommandBuffer(*rec_), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, trace_.pool,
                            static_cast<u32>(trace_.events.size()));
        trace_.events.push_back(std::move(what));
        return trace_.events.size() - 1;
    }

    void TraceDraw(const DrawDesc& desc, const Target* color, const Target* depth, const Target* shadow) {
        if (trace_.open == SIZE_MAX || !composer_->Continues(desc)) {
            std::string what = shadow != nullptr ? fmt::format("pass shadow {:08x} {}x{}", shadow->paddr, shadow->width, shadow->height)
                                                 : std::string("pass");
            if (color != nullptr) {
                what += fmt::format(" c {:08x} {}x{} fmt {}", color->paddr, color->width, color->height, color->format);
            }
            if (depth != nullptr) {
                what += fmt::format(" d {:08x} {}x{}", depth->paddr, depth->width, depth->height);
            }
            what += fmt::format(" area {}x{}", desc.target.area.width, desc.target.area.height);
            const size_t i = TraceEvent(std::move(what));
            if (i == SIZE_MAX) {
                return;
            }
            trace_.open = i;
            trace_.draws = trace_.dst_copies = trace_.fixups = 0;
            trace_.pixels = 0;
            trace_.x0 = trace_.y0 = 1 << 30;
            trace_.x1 = trace_.y1 = 0;
        }
        ++trace_.draws;
        if (desc.needs.dst_copy) {
            ++trace_.dst_copies;
        }
        if (desc.scissor) {
            const rhi::Rect& r = *desc.scissor;
            trace_.pixels += u64{r.width} * r.height;
            trace_.x0 = std::min(trace_.x0, r.x);
            trace_.y0 = std::min(trace_.y0, r.y);
            trace_.x1 = std::max(trace_.x1, r.x + static_cast<s32>(r.width));
            trace_.y1 = std::max(trace_.y1, r.y + static_cast<s32>(r.height));
        }
    }

    void TraceFinish() {
        TraceEvent("end");
        trace_.active = false;
        FlushRecording();
        backend_->WaitIdle();
        const u32 count = static_cast<u32>(trace_.events.size());
        std::vector<u64> ticks(count, 0);
        const VkResult r = vkGetQueryPoolResults(rhi::Handles(*backend_).device, trace_.pool, 0, count, count * sizeof(u64),
                                                 ticks.data(), sizeof(u64), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        if (r != VK_SUCCESS) {
            MH4N_LOG(Error, kLog, "MH4N_PASS_TRACE: the timestamps could not be read ({})", static_cast<int>(r));
        }
        const double period = backend_->Caps().timestamp_period;
        {
            // (in the GPU's order: a prelude's event is recorded after the main list's events that run after it)
            std::vector<u32> order(count);
            for (u32 i = 0; i < count; ++i) order[i] = i;
            std::stable_sort(order.begin(), order.end(), [&](u32 a, u32 b) { return ticks[a] < ticks[b]; });
            std::vector<u64> t(count);
            std::vector<std::string> e(count);
            for (u32 i = 0; i < count; ++i) {
                t[i] = ticks[order[i]];
                e[i] = std::move(trace_.events[order[i]]);
            }
            ticks = std::move(t);
            trace_.events = std::move(e);
        }
        std::map<std::string, std::pair<u64, double>> totals;  // by kind (the event's first word, the pass's target)
        for (u32 i = 0; i + 1 < count; ++i) {
            const double us = ticks[i + 1] >= ticks[i] ? static_cast<double>(ticks[i + 1] - ticks[i]) * period / 1000.0 : -1.0;
            MH4N_LOG(Info, kLog, "PT {:5} {:9.1f} us  {}", i, us, trace_.events[i]);
            const std::string& e = trace_.events[i];
            std::string kind = e.substr(0, e.find(' '));
            if (kind == "pass") {
                const size_t bar = e.find(" area");
                kind = e.substr(0, bar);
            }
            totals[kind].first += 1;
            totals[kind].second += std::max(us, 0.0);
        }
        for (const auto& [kind, v] : totals) {
            MH4N_LOG(Info, kLog, "PT total {:9.1f} us in {:4} x  {}", v.second, v.first, kind);
        }
        trace_.events.clear();
    }

    void CountStubs(const fragment::FragmentKey& key) {
        std::array<const char*, 8> names{};
        const size_t n = fragment::StubbedFeatures(key, *registry_, names);
        for (size_t i = 0; i < n; ++i) {
            ++frame_.stubbed[names[i]];
        }
    }

    void CountComposer() {
        // The composer's and the provider's counters since the last draw.
        for (const FrameCounter& c : composer_->TakeFrameCounters()) {
            if (c.name == "stubbed:fixup") frame_.stubbed_fixup += c.value;
            else if (c.name == "dst_copies") frame_.dst_copies += c.value;
            else if (c.name == "fallback_draws") frame_.fallback_draws += c.value;
            else if (c.name == "pass_begins") frame_.pass_begins += c.value;
        }
        for (const FrameCounter& c : provider_->TakeFrameCounters()) {
            if (c.name == "pipeline_sync_builds") frame_.sync_builds += c.value;
            else if (c.name == "pipeline_forced_uber") frame_.forced_uber += c.value;
            else frame_.provider[c.name] += c.value;  // N2-16's
        }
    }

    // N2-16 (design 4.5, fallback (ii)): PipelineProvider::Prepare for an accelerated draw, with the key of its own pipeline
    // and of the fallback it takes on the CPU path (the trivial vertex stage's ubershader pipeline), both with the targets the
    // draw is going to have (the draw path below makes the same adjustments once it has them).
    PipelineReadiness PrepareAccelerated(const fragment::FragmentKey& key, const fragment::Needs& needs, rhi::FixedFunctionState ff,
                                         const vertex::LoadedDraw& loaded, u64 program, bool color, bool depth, u32 depth_format,
                                         bool pull, bool big) {
        if (!depth) {
            ff.depth_test = ff.depth_write = ff.stencil_test = false;
        }
        if (!color) {
            ff.blend_enabled = false;
            ff.logic_op_enabled = false;
        }
        DrawDesc probe;
        probe.vertex.kind = VertexStageKind::Translated;
        probe.vertex.program = program;
        probe.vertex.input = pull ? VertexInputKey{} : MakeVertexInput(loaded);
        probe.fragment = &key;
        probe.needs = needs;
        probe.needs.fixup_gs = needs.fixup_gs && !pull;
        probe.state = ff;
        probe.topology = pull ? rhi::Topology::TriangleList : ToRhiTopology(loaded.topology);
        probe.color_format = color ? rhi::Format::Rgba8Unorm : rhi::Format::Undefined;
        probe.depth_format = depth ? DepthImageFormat(depth_format) : rhi::Format::Undefined;
        const PipelineKey own = composer_->KeyOf(probe);
        static const VertexInputKey batch_input = InputKeyOf(cpuvertex::MakeBatchVertexLayout());
        PipelineKey fallback;
        bool have_fallback = false;
        const auto fallback_key = [&]() -> const PipelineKey& {
            if (!have_fallback) {
                probe.vertex = VertexStageKey{VertexStageKind::Trivial, 0, batch_input};
                probe.topology = rhi::Topology::TriangleList;
                probe.fallback_only = true;
                fallback = composer_->KeyOf(probe);
                have_fallback = true;
            }
            return fallback;
        };
        // The key of the draw before, found ready then, is ready still (a pipeline never leaves the provider): no lookup.
        PipelineReadiness readiness = PipelineReadiness::Ready;
        if (!(last_ready_valid_ && own == last_ready_key_)) {
            readiness = provider_->Prepare(own, fallback_key());
            last_ready_valid_ = readiness == PipelineReadiness::Ready;
            if (last_ready_valid_) last_ready_key_ = own;
            if (readiness == PipelineReadiness::Pending && big) {
                provider_->Hurry(own);  // (its picture is held until it is built: before the small draws' keys)
            }
        }
        if (!pull || readiness == PipelineReadiness::Failed || readiness == PipelineReadiness::Pending) {
            return readiness;
        }
        // the quaternion pass (vertex_shader.h, `pull_fixup`): both pipelines, or the CPU path meanwhile; the last one found
        // ready is kept with its program (a pipeline never leaves the provider), for the draws of that program that follow
        if (quaternion_program_ != program || quaternion_pipeline_ == nullptr) {
            PipelineKey quaternion_key;
            quaternion_key.vertex.kind = VertexStageKind::QuaternionPass;
            quaternion_key.vertex.program = program;
            const PipelineReadiness quaternion = provider_->Prepare(quaternion_key, fallback_key());
            if (quaternion == PipelineReadiness::Pending && big) {
                provider_->Hurry(quaternion_key);
            }
            if (quaternion != PipelineReadiness::Ready) {
                return quaternion == PipelineReadiness::Failed    ? PipelineReadiness::Failed
                       : quaternion == PipelineReadiness::Pending ? PipelineReadiness::Pending
                                                                  : PipelineReadiness::Fallback;
            }
            rhi::Result<PipelineHandle> handle = provider_->Lookup(quaternion_key);
            if (!handle.ok() || handle->fallback || handle->pipeline == nullptr || !handle->pipeline->IsCompute()) {
                return PipelineReadiness::Failed;  // (force_ubershader answers with a graphics fallback)
            }
            quaternion_pipeline_ = handle->pipeline.get();
            quaternion_program_ = program;
        }
        return readiness;
    }

    // The flip pass's pipeline and the compute passes' binding set (vertex_shader.h, bindings.h FixupPassSets), made once,
    // at the first draw that needs them. False when they cannot be made.
    bool EnsureFlipPass() {
        if (flip_pass_ != nullptr) {
            return true;
        }
        if (flip_pass_failed_) {
            return false;
        }
        flip_pass_failed_ = true;
        std::vector<u32> spirv;
        std::string error;
        if (!shader::CompileGlslToSpirv(shader::FlipPassGlsl(), spirv, error, nullptr, true)) {
            MH4N_LOG(Error, kLog, "the flip pass does not compile: {}", error);
            return false;
        }
        rhi::ComputePipelineDesc desc;
        desc.shader.spirv = spirv;
        desc.sets = FixupPassSets();
        desc.name = "flip pass";
        rhi::Result<std::shared_ptr<rhi::Pipeline>> made = backend_->CreateComputePipeline(desc);
        rhi::Result<std::shared_ptr<rhi::BindingSet>> set = backend_->CreateBindingSet(FixupPassSets()[0]);
        if (!made.ok() || !set.ok()) {
            MH4N_LOG(Error, kLog, "the flip pass: {} {}", made.ok() ? "" : made.error(), set.ok() ? "" : set.error());
            return false;
        }
        const rhi::Buffer& uniforms = *vs_ring_->GetBuffer();
        const rhi::Buffer& vertices = *vertex_ring_->GetBuffer();
        rhi::Result<void> written = (*set)->SetStorageBuffer(shader::kBindingPassUniformWords, uniforms, 0, uniforms.Size());
        if (written.ok()) {
            written = (*set)->SetUniformBuffer(shader::kBindingPassParams, uniforms, 0, 16);
        }
        if (written.ok()) {
            written = (*set)->SetStorageBuffer(binding::kPullWords, vertices, 0, vertices.Size());
        }
        if (!written.ok()) {
            MH4N_LOG(Error, kLog, "the fix-up passes' set: {}", written.error());
            return false;
        }
        flip_pass_ = *made;
        fixup_set_ = *set;
        flip_pass_failed_ = false;
        return true;
    }

    // A pull draw's job record and its workgroup table entries, recorded with the recording's (RecordFixupPasses).
    u64 PullJobBytes() const {
        const u64 groups = (u64{pull_corners_} + 2 * u64(pull_high_ - pull_low_ + 1)) / shader::kPullComputeGroup + 2;
        return 4 * (shader::kPullJobWords + groups);
    }

    // The compute passes of the recording's pull draws (vertex_shader.h), into its prelude: the job records and the
    // workgroup tables in the vertex ring, one quaternion pass a program, then one flip pass for all.
    void RecordFixupPasses() {
        if (pull_jobs_.empty()) {
            return;
        }
        constexpr u32 kGroup = shader::kPullComputeGroup;
        // the programs, in the order of their first draw, and each one's jobs
        std::vector<std::pair<const rhi::Pipeline*, std::vector<u32>>> programs;
        u32 quaternion_groups = 0, flip_groups = 0;
        for (u32 j = 0; j < pull_jobs_.size(); ++j) {
            const PullJob& job = pull_jobs_[j];
            auto it = std::find_if(programs.begin(), programs.end(), [&](const auto& p) { return p.first == job.pipeline; });
            if (it == programs.end()) {
                programs.push_back({job.pipeline, {}});
                it = programs.end() - 1;
            }
            it->second.push_back(j);
            quaternion_groups += (job.vertices + kGroup - 1) / kGroup;
            flip_groups += (job.corners + kGroup - 1) / kGroup;
        }
        const u64 words = u64{shader::kPullJobWords} * pull_jobs_.size() + quaternion_groups + flip_groups;
        rhi::Result<rhi::RingAllocation> alloc = vertex_ring_->Map(4 * words, 16);
        if (!alloc.ok()) {
            // (EnsureRoom keeps room for them: not expected) the draws keep their indices without a flip
            MH4N_LOG(Error, kLog, "the fix-up passes' records do not fit the ring: {} draws without the flip", pull_jobs_.size());
            pull_jobs_.clear();
            return;
        }
        u32* out = reinterpret_cast<u32*>(alloc->data.data());
        const u32 jobs_word = static_cast<u32>(alloc->offset / 4);
        u32 table = shader::kPullJobWords * static_cast<u32>(pull_jobs_.size());  // the next table entry (in `out`)
        // the quaternion passes' tables, one a program (a workgroup's number is its dispatch's), then the flip pass's
        std::vector<u32> quaternion_tables;
        std::vector<u32> quaternion_dispatch_groups;
        for (const auto& [pipeline, jobs] : programs) {
            quaternion_tables.push_back(jobs_word + table);
            u32 group = 0;
            for (u32 j : jobs) {
                const u32 n = (pull_jobs_[j].vertices + kGroup - 1) / kGroup;
                pull_jobs_[j].quaternion_group = group;
                for (u32 g = 0; g < n; ++g) {
                    out[table++] = j;
                }
                group += n;
            }
            quaternion_dispatch_groups.push_back(group);
        }
        const u32 flip_table = jobs_word + table;
        u32 group = 0;
        for (u32 j = 0; j < pull_jobs_.size(); ++j) {
            const u32 n = (pull_jobs_[j].corners + kGroup - 1) / kGroup;
            pull_jobs_[j].flip_group = group;
            for (u32 g = 0; g < n; ++g) {
                out[table++] = j;
            }
            group += n;
        }
        for (u32 j = 0; j < pull_jobs_.size(); ++j) {
            const PullJob& job = pull_jobs_[j];
            u32* r = out + shader::kPullJobWords * j;
            std::fill(r, r + shader::kPullJobWords, 0u);
            r[shader::kPullJobUniforms] = job.uniforms_word;
            r[shader::kPullJobHeader] = job.header_word;
            r[shader::kPullJobFirstVertex] = job.first_vertex;
            r[shader::kPullJobVertices] = job.vertices;
            r[shader::kPullJobQuaternions] = job.quaternions_word;
            r[shader::kPullJobIndexList] = job.list_word;
            r[shader::kPullJobCornerCount] = job.corners;
            r[shader::kPullJobIndices] = job.indices_word;
            r[shader::kPullJobQuaternionGroup] = job.quaternion_group;
            r[shader::kPullJobFlipGroup] = job.flip_group;
            r[shader::kPullJobIndexForm] = job.list_form;
            r[shader::kPullJobBaseVertex] = static_cast<u32>(job.base_vertex);
            r[shader::kPullJobTriangleTable] = job.table_word;
        }
        vertex_ring_->GetBuffer()->Flush(alloc->offset, 4 * words);
        if (!vertex_ring_->Commit(4 * words).ok()) {
            MH4N_LOG(Error, kLog, "the fix-up passes' records could not be committed: {} draws without the flip", pull_jobs_.size());
            pull_jobs_.clear();
            return;
        }
        // each dispatch's parameters (pass_params)
        const u64 alignment = std::max<u64>(backend_->Caps().uniform_buffer_offset_alignment, 16);
        const auto params = [&](u32 table_word) -> std::optional<u32> {
            rhi::Result<rhi::RingAllocation> p = vs_ring_->Map(16, alignment);
            if (!p.ok()) {
                return std::nullopt;
            }
            const u32 block[4] = {jobs_word, table_word, 0, 0};
            std::memcpy(p->data.data(), block, sizeof(block));
            vs_ring_->GetBuffer()->Flush(p->offset, sizeof(block));
            if (!vs_ring_->Commit(16).ok()) {
                return std::nullopt;
            }
            return static_cast<u32>(p->offset);
        };
        rec_->BeginPrelude();
        if (trace_.active && trace_.events.size() < kTraceQueries) {
            // (written into the prelude, which runs before the main list: TraceFinish orders the events by their times)
            vkCmdWriteTimestamp(rhi::CommandBuffer(*rec_), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, trace_.pool,
                                static_cast<u32>(trace_.events.size()));
            trace_.events.push_back(fmt::format("prelude fix-up: {} draws, {} programs, {} + {} groups", pull_jobs_.size(),
                                                programs.size(), quaternion_groups, flip_groups));
        }
        for (size_t k = 0; k < programs.size(); ++k) {
            const std::optional<u32> offset = params(quaternion_tables[k]);
            if (!offset) {
                break;
            }
            rec_->SetPipeline(*programs[k].first);
            rec_->Bind(*fixup_set_, 0, std::span<const u32>(&*offset, 1));
            rec_->Dispatch(quaternion_dispatch_groups[k], 1, 1);
        }
        rec_->Barrier(rhi::Stage::ComputeShader, rhi::Stage::ComputeShader);  // the quaternions, written above
        if (const std::optional<u32> offset = params(flip_table)) {
            rec_->SetPipeline(*flip_pass_);
            rec_->Bind(*fixup_set_, 0, std::span<const u32>(&*offset, 1));
            rec_->Dispatch(group, 1, 1);
        }
        rec_->EndPrelude();
        pull_jobs_.clear();
    }

    // A pull draw's bytes in the vertex ring: the header, its index list (words), its triangle table (room for every
    // triangle), the quaternions (4 words a vertex), the indices (room for every triangle's corners).
    u64 PullBytes() const {
        return 4 * (shader::kPullHeaderWords + u64{pull_list_words_} + u64{pull_table_words_} +
                    4 * u64(pull_high_ - pull_low_ + 1) + 3 * u64{pull_triangles_});
    }

    // The lighting fix-up in the vertex stage (vertex_shader.h, `pull_fixup`): whether the draw can take it, and the range of
    // its vertices (pull_low_, pull_high_), its corners (three a triangle) and its index list's form (WritePullList). False
    // when it cannot: the PICA's shader topology, a vertex number past 65535 or past its buffer's vertices, an input
    // register without its buffer.
    bool PullRange(const vertex::LoadedDraw& d) {
        if (d.topology == vertex::Topology::Shader || d.count < 3 || d.buffers.empty() ||
            d.buffers.size() > std::min<size_t>(rhi::kMaxVertexBindings, shader::kPullHeaderWords)) {
            return false;
        }
        s64 limit = 65536;
        for (const vertex::VertexBuffer& b : d.buffers) {
            if (!b.per_instance) {
                limit = std::min<s64>(limit, b.vertex_count);
            }
        }
        for (const vertex::VertexAttribute& a : d.attributes) {
            if (a.binding >= d.buffers.size()) {
                return false;
            }
        }
        s64 low = 0, high = 0;
        const auto scan = [&](const auto* index) {
            auto lo = index[0], hi = index[0];
            for (u32 i = 1; i < d.count; ++i) {
                lo = std::min(lo, index[i]);
                hi = std::max(hi, index[i]);
            }
            low = static_cast<s64>(lo) + d.base_vertex;
            high = static_cast<s64>(hi) + d.base_vertex;
        };
        static_assert(std::endian::native == std::endian::little, "the index list is little-endian");
        u32 size = shader::kPullIndexNone, bytes = 0;  // the index list's: its entries' size, its bytes
        if (d.indices_deferred) {
            low = static_cast<s64>(d.list_low) + d.base_vertex;  // (the loader's own range)
            high = static_cast<s64>(d.list_high) + d.base_vertex;
            if (d.index_source == nullptr) {
                return false;
            }
            size = d.index_source_u16 ? shader::kPullIndexU16 : shader::kPullIndexU8;
            bytes = d.count * (d.index_source_u16 ? 2u : 1u);
        } else {
            switch (d.index_type) {
            case vertex::IndexType::None: high = static_cast<s64>(d.count) - 1; break;
            case vertex::IndexType::U8: scan(d.index_data.data()); size = shader::kPullIndexU8; break;
            case vertex::IndexType::U16:
                scan(reinterpret_cast<const u16*>(d.index_data.data()));
                size = shader::kPullIndexU16;
                break;
            case vertex::IndexType::U32:
                scan(reinterpret_cast<const u32*>(d.index_data.data()));
                size = shader::kPullIndexU32;
                break;
            }
            bytes = size == shader::kPullIndexNone ? 0u : static_cast<u32>(d.index_data.size());
        }
        if (low < 0 || high >= limit) {
            return false;
        }
        pull_low_ = low;
        pull_high_ = high;
        pull_triangles_ = d.topology == vertex::Topology::TriangleList ? d.count / 3 : d.count - 2;
        pull_corners_ = 3 * pull_triangles_;  // (until WritePullTriangles knows)
        pull_list_words_ = (bytes + 3) / 4;
        pull_table_words_ = size == shader::kPullIndexNone ? 0 : pull_triangles_;  // (an array's vertices are all different)
        const u32 topology = d.topology == vertex::Topology::TriangleStrip ? shader::kPullTriangleStrip
                             : d.topology == vertex::Topology::TriangleFan ? shader::kPullTriangleFan
                                                                           : shader::kPullTriangleList;
        pull_list_form_ = size | (topology << 2) | (pull_table_words_ != 0 ? shader::kPullTriangleTable : 0u);
        pull_base_vertex_ = size == shader::kPullIndexNone ? 0 : d.base_vertex;
        return true;
    }

    // A pull draw's triangle table (`table`: room for pull_triangles_ words): the number of each triangle with three
    // different vertices, in the topology (vertex_shader.h); how many. Those with two equal ones have no area and draw
    // nothing, nor does the geometry stage's: left out, the draw and the flip pass have their corners only (a strip that
    // joins its parts with such triangles has nearly as many of them as of the others).
    u32 WritePullTriangles(const vertex::LoadedDraw& d, u32* table) const {
        if (pull_table_words_ == 0) {
            return pull_triangles_;
        }
        if (d.indices_deferred) {
            return WritePullTrianglesOf(d.list.data(), d.count, d.topology, table);
        }
        const u8* data = d.index_data.data();
        switch (d.index_type) {
        case vertex::IndexType::U8: return WritePullTrianglesOf(data, d.count, d.topology, table);
        case vertex::IndexType::U16:
            return WritePullTrianglesOf(reinterpret_cast<const u16*>(data), d.count, d.topology, table);
        case vertex::IndexType::U32:
            return WritePullTrianglesOf(reinterpret_cast<const u32*>(data), d.count, d.topology, table);
        case vertex::IndexType::None: break;
        }
        return 0;
    }

    template <typename Index>
    static u32 WritePullTrianglesOf(const Index* index, u32 n, vertex::Topology topology, u32* table) {
        u32 kept = 0;
        const auto put = [&](u32 t, u32 a, u32 b, u32 c) {
            table[kept] = t;
            kept += (a != b && b != c && a != c) ? 1u : 0u;
        };
        switch (topology) {
        case vertex::Topology::TriangleList:
            for (u32 t = 0; 3 * t + 2 < n; ++t) put(t, index[3 * t], index[3 * t + 1], index[3 * t + 2]);
            break;
        case vertex::Topology::TriangleStrip:
            for (u32 t = 0; t + 2 < n; ++t) put(t, index[t], index[t + 1], index[t + 2]);
            break;
        case vertex::Topology::TriangleFan:
            for (u32 t = 0; t + 2 < n; ++t) put(t, index[t + 1], index[t + 2], index[0]);
            break;
        case vertex::Topology::Shader:
            break;
        }
        return kept;
    }

    // A pull draw's index list into the vertex ring (`out`: pull_list_words_ words), as the guest has it: the flip pass
    // reads it to find each corner's vertex and its triangle's first (vertex_shader.h). A copy, nothing per triangle.
    void WritePullList(const vertex::LoadedDraw& d, u8* out) const {
        if (pull_list_words_ == 0) {
            return;
        }
        const u8* source = d.indices_deferred ? d.index_source : d.index_data.data();
        const u32 bytes = d.indices_deferred ? d.count * (d.index_source_u16 ? 2u : 1u) : static_cast<u32>(d.index_data.size());
        std::memcpy(out, source, bytes);
        std::memset(out + bytes, 0, 4 * pull_list_words_ - bytes);  // (the last word's unused bytes)
    }

    static VertexInputKey InputKeyOf(const rhi::VertexLayout& layout) {
        VertexInputKey key;
        key.binding_count = static_cast<u8>(layout.bindings.size());
        for (size_t i = 0; i < layout.bindings.size(); ++i) {
            key.bindings[i].stride = layout.bindings[i].stride;
            key.bindings[i].per_instance = layout.bindings[i].per_instance ? 1 : 0;
        }
        key.attribute_count = static_cast<u8>(layout.attributes.size());
        for (size_t i = 0; i < layout.attributes.size(); ++i) {
            key.attributes[i].location = static_cast<u8>(layout.attributes[i].location);
            key.attributes[i].binding = static_cast<u8>(layout.attributes[i].binding);
            key.attributes[i].format = layout.attributes[i].format;
            key.attributes[i].offset = layout.attributes[i].offset;
        }
        return key;
    }

    Host& host_;
    NativeOptions options_;
    gx::GxEngine gx_;
    pica::CommandProcessor processor_;
    const fragment::FeatureRegistry* registry_;
    const u32 scale_;  // the internal resolution, 1 to kMaxScale
    const bool async_;  // --async-screens
    bool skip_blit_ = false;  // the next display transfer the engine runs is not blitted (TryAsyncTransfer)
    WritebackCause writeback_cause_ = WritebackCause::Other;
    // --exact-shaders (or MH4N_EXACT_SHADERS=1): the vertex programs' arithmetic to the PICA's last bit (the capture
    // checks); by default the GPU's own dot products, reciprocals and roots
    const bool exact_shaders_ = [] {
        const char* v = std::getenv("MH4N_EXACT_SHADERS");
        return v != nullptr && *v == '1';
    }();

    // the renderer (made on the first draw, on the thread that calls Execute)
    bool renderer_ready_ = false;
    bool renderer_failed_ = false;
    std::shared_ptr<rhi::Instance> instance_;
    std::shared_ptr<rhi::Backend> backend_;
    fragment::DeviceProfile profile_;
    std::unique_ptr<PipelineBuilder> builder_;
    std::unique_ptr<PipelineProvider> provider_;
    std::unique_ptr<DrawComposer> composer_;
    std::unique_ptr<fragment::FragmentUniformRing> uniform_ring_;
    std::unique_ptr<fragment::TableRings> tables_;
    std::unique_ptr<rhi::Ring> vertex_ring_, vs_ring_, staging_ring_;
    std::unique_ptr<fragment::FragmentState> fragment_state_;
    std::unique_ptr<vertex::VertexLoader> loader_;
    cpuvertex::CpuVertexRunner runner_;
    bool native_check_ = false;  // MH4N_PICA_NATIVE=check
    cpuvertex::PrimitiveAssembler assembler_;
    cpuvertex::TriangleBatch batch_;
    std::vector<std::unique_ptr<Target>> targets_;

    // textures (N2-23): the decoded texture cache of N2-14, the images made from it, the sampler objects
    std::unique_ptr<texture::TextureCache> texture_cache_;
    // The texture budget (MH4N_TEXTURE_BUDGET_MS, 0: none): the texture work a screen's frame may do on this thread
    // (the cache's decodes and hashes); past it a new texture is decoded on AsyncDecoder's threads and the draws that
    // need it are left out until it is ready (texture_pending_: the draw's skip, the picture held).
    const u64 texture_budget_us_ = [] {
        const char* v = std::getenv("MH4N_TEXTURE_BUDGET_MS");
        return v != nullptr ? static_cast<u64>(std::max(0, std::atoi(v))) * 1000 : u64{0};
    }();
    u64 texture_spent_us_ = 0;  // this screen's so far (PaceGpu starts it again)
    bool texture_pending_ = false;
    u32 slow_decodes_told_ = 0, slow_creates_told_ = 0, slow_writebacks_told_ = 0;
    u64 reserve_checks_ = 0;
    u32 gpu_frames_now_ = 2;  // AdaptGpuFrames
    u32 adapt_transfers_ = 0, adapt_waits_ = 0;
    u64 adapt_gpu_ = 0;
    std::chrono::steady_clock::time_point adapt_since_ = std::chrono::steady_clock::now();
    std::shared_ptr<std::atomic<bool>> reserving_ = std::make_shared<std::atomic<bool>>(false);
    std::unique_ptr<texture::AsyncDecoder> decoder_;
    std::unique_ptr<fragment::sampling::SamplerCache> samplers_;
    std::map<TexImageKey, TexImage> texture_images_;
    u64 texture_bytes_ = 0;
    u64 textures_released_ = 0;           // images let go (the budget, the memory short)
    u64 register_triggers_ = 0;           // fills and transfers started by a register write (WriteReg)
    bool texture_memory_short_ = false;   // an image could not be made: TrimTextures at the next recording
    u32 texture_create_failures_ = 0;
    u32 texture_memory_short_told_ = 0;  // "texture memory ran out" lines written (TrimTextures)
    bool memory_short_told_ = false;
    u64 next_memory_line_us_ = 0;
    long first_resident_kb_ = -1;
    u64 recording_serial_ = 0;
    rhi::TimelineValue last_submit_ = 0;
    std::shared_ptr<rhi::Image> null_2d_, null_black_2d_, null_cube_;  // transparent black, opaque black, transparent black cube
    bool null_cleared_ = false;
    bool binding_textures_ = false;
    bool levels_missing_in_draw_ = false;
    u64 last_vs_source_ = 0;  // the vertex program the last draw registered with the builder (DrawImpl)
    // the flags the hidden touch screen's draws left out had (DrawImpl: given to the next draw that is prepared)
    pica::DirtyRegs touch_dirty_{};
    u32 touch_lighting_ = 0;
    bool touch_fog_ = false, touch_gas_ = false, touch_pending_ = false;
    u8 touch_proctex_ = 0;
    // what DrawImpl takes from the fragment key, kept until the key changes (FragmentState::Prepared::key_changed)
    bool key_memo_valid_ = false;
    std::array<bool, 3> units_read_{};
    std::array<const char*, 8> stub_names_{};
    size_t stub_count_ = 0;
    PipelineKey last_ready_key_{};  // the last key PrepareAccelerated found ready (a pipeline never leaves the provider)
    bool last_ready_valid_ = false;
    // the draw path's lists, kept from draw to draw so that a draw allocates none (BindTextures, BindUnit, DrawImpl)
    std::vector<texture::TextureRef> draw_refs_;
    std::vector<const texture::TextureRef*> unit_faces_;
    std::vector<rhi::VertexBufferBinding> draw_buffers_;
    u64 seen_sampler_creates_ = 0;

    // the screens at the internal scale (scaled_screens.h)
    u64 draw_serial_ = 0;
    PassTrace trace_;
    std::vector<ScaledScreen> scaled_screens_;
    std::shared_ptr<rhi::Pipeline> screen_pipeline_;
    std::unique_ptr<rhi::BindingArena> screen_arena_;
    std::vector<u32> screen_vs_, screen_fs_;
    bool screen_pipeline_failed_ = false;
    std::shared_ptr<rhi::Pipeline> tile_pipeline_;  // DrawTiles (gpu4)
    std::vector<u32> tile_vs_, tile_fs_;
    bool tile_pipeline_failed_ = false;
    u32 screen_fallback_logs_ = 0;
    // MH4N_SCALE_LOG: what marks targets stale now (StaleWhy), each event's last game frame, the lines written
    const char* stale_why_ = "something else";
    std::map<std::string, u32> scale_seen_;
    u32 scale_logged_ = 0;
    // shared screens (SetSharedScreens, TakeSharedSlot, ReadSharedScreen)
    bool shared_wanted_ = false, shared_tried_ = false;
    std::unique_ptr<rhi::SharedImages> shared_;
    // screen targets (gpu3): hand-overs owed (RecordScreenReleases), those recorded into the recording being submitted,
    // images of a target that is no screen target any more (kept while a picture holds them), log lines written
    bool any_release_owed_ = false;
    std::vector<TargetBuffer*> releasing_;
    std::vector<std::unique_ptr<TargetBuffer>> orphan_buffers_;
    u32 target_logs_ = 0;
    bool target_refusal_told_ = false;
    const char* handover_where_ = nullptr;  // (the log's word for a hand-over before a write: Detach)
    bool owing_allowed_ = false;  // (OwingAllowed)
    std::thread check_thread_;  // the pipeline check after the device is made (EnsureRenderer), joined by the destructor
    // MH4N_OWED_CHECK: the snapshots taken, the old way's copy of the one being checked, its entry, the totals
    u64 owed_checks_ = 0, owed_compared_ = 0, owed_differ_ = 0;
    u32 owed_logs_ = 0;  // (TakeOwed's lines: the first 32)
    std::shared_ptr<rhi::Image> owed_check_;
    const ScaledScreen* owed_check_entry_ = nullptr;
    std::vector<std::shared_ptr<rhi::Image>> retire_after_submit_;  // (images the open recording may use: FlushRecording)
    std::vector<std::unique_ptr<SharedSlot>> shared_slots_;
    std::shared_ptr<rhi::SyncFile> shared_sync_;     // the last sync file made, and the submissions it covers
    rhi::TimelineValue shared_sync_value_ = 0;
    u32 shared_logs_ = 0;

    // the touch screen hidden (SetTouchScreenShown; MH4N_TOUCH_HIDDEN=1 as if it always were): NoteScreenTransfer, DrawImpl.
    // Its draws are left out on Mali (EnsureRenderer; MH4N_TOUCH_SKIP=0|1 on any device): at 3x in the village, with the
    // frames locked to the VBlank, the Anbernic went from 19.8 to 16.7 ms (every frame at 60) and the Moto from 66.2 to
    // 48.2 ms; the Galaxy's mean went from 9.0 to 9.8 ms (its median from 8.9 to 8.5), for reasons not yet found.
    std::atomic<bool> touch_shown_{true};
    const bool touch_forced_hidden_ = [] {
        const char* v = std::getenv("MH4N_TOUCH_HIDDEN");
        return v != nullptr && *v == '1';
    }();
    std::atomic<bool> touch_skip_enabled_{false};
    bool last_transfer_touch_ = false;  // the last display transfer went to the touch screen
    u32 transfer_alternations_ = 0;     // the transfers in a row that went to the other screen than the one before
    u32 touch_source_ = 0;              // the colour target the touch screen's last transfer read (its address)
    bool touch_phase_ = false;          // the draws until the next transfer are the hidden touch screen's: left out
    bool touch_phase_told_ = false;

    // the recording
    std::optional<rhi::Recorder> rec_;
    bool frame_marked_ = false;
    u64 gpu_total_taken_ = 0;  // FrameGpuTimeTotalNs at the last TakeFrameCounters
    u32 draws_in_recording_ = 0;
    u64 vertex_bytes_in_recording_ = 0;
    bool dynamic_applied_ = false;
    fbops::DynamicState last_dynamic_{};
    bool last_constants_from_alpha_ = false;

    // counters
    FrameCounters frame_;
    u64 seen_restart_widened_ = 0;
    vertex::LoadedDraw loaded_scratch_;
    // PullRange's: the draw's triangles and corners (WritePullTriangles: those drawn), its index list (words,
    // kPullJobIndexForm), its triangle table's words and base vertex
    u32 pull_triangles_ = 0, pull_corners_ = 0, pull_list_words_ = 0, pull_list_form_ = 0, pull_table_words_ = 0;
    s32 pull_base_vertex_ = 0;
    bool pull_prefill_indices_ = false;  // the pull draws' indices are filled on the CPU first (Mali; EnsureRenderer)
    s64 pull_low_ = 0, pull_high_ = 0;  // ... the range of its vertices
    // A pull draw's compute passes (RecordFixupPasses; the words of the vertex ring's and the uniform ring's buffers).
    struct PullJob {
        const rhi::Pipeline* pipeline = nullptr;  // its quaternion pass
        u32 uniforms_word = 0, header_word = 0, first_vertex = 0, vertices = 0, quaternions_word = 0, list_word = 0,
            corners = 0, indices_word = 0;
        u32 quaternion_group = 0, flip_group = 0;  // (RecordFixupPasses)
        u32 list_form = 0;    // its index list's size and topology (kPullJobIndexForm)
        s32 base_vertex = 0;  // ... and what is added to each index
        u32 table_word = 0;   // its triangle table's word
    };
    PullJob pull_job_;                // the draw's, while it is made
    std::vector<PullJob> pull_jobs_;  // the recording's
    std::shared_ptr<rhi::Pipeline> flip_pass_;  // EnsureFlipPass
    std::shared_ptr<rhi::BindingSet> fixup_set_;  // ... and the passes' set
    const rhi::Pipeline* quaternion_pipeline_ = nullptr;  // PrepareAccelerated: the last quaternion pass found ready
    u64 quaternion_program_ = 0;                          // ... and its program
    bool flip_pass_failed_ = false;
    struct HeldCpuDraw {
        bool active = false;
        DrawDesc desc;
        fragment::FragmentKey key;
        cpuvertex::TriangleBatch batch;
    };
    HeldCpuDraw held_;  // JoinHeldCpuDraw
    std::vector<rhi::VertexBufferBinding> held_buffers_;
    bool self_sampled_ = false;  // the draw samples the target it draws into (TargetAsTexture's copy)
    SelfDraw self_draw_;         // the draw BindTextures is binding for (PingPongBox)
    PingPong pingpong_;          // the draw being prepared samples its target and draws into the twin (PingPongBox)
    u64 ab_transfers_ = 0;       // display transfers, for MH4N_AB's phase
    // the last draw recorded: its colour and depth buffers' addresses (the registers) and its attachments
    // (SubmitAtPassBreak)
    struct LastDrawTarget {
        u32 color_address = 0, depth_address = 0;
        const rhi::Image* color = nullptr;
        const rhi::Image* depth = nullptr;
        bool valid = false;
    };
    LastDrawTarget last_draw_target_;
    const bool hold_enabled_ = [] {
        const char* v = std::getenv("MH4N_CPU_JOIN");
        return !(v != nullptr && *v == '0');
    }();
    enum class FixupMode : u8 { Off, Gs, Vs };
    FixupMode fixup_mode_ = FixupMode::Gs;  // EnsureRenderer
    std::deque<u64> gpu_frames_;  // PaceGpu: the timeline values of the screen transfers still on the GPU
    // Frames with big draws left out (BigDraw; no pipeline built for them yet: the provider's never-wait mode) are not
    // shown: the screen's last whole picture stands in for them, for at most kMaxHoldUs (50 ms) in a row (then the
    // incomplete ones are shown, so that a cold area is not frozen), and their keys go before the others in the build
    // queue (PipelineProvider::Hurry). A large draw left out (the sky, the ground) was a black flash on the phone. A frame
    // with only small draws left out is shown: at 30 FPS each held picture was a 33 ms stop (66-100 ms gaps). The limit
    // was 6 frames: 50 ms at 120 FPS, but 120-140 ms of a frozen picture on a Mali at 45-50 FPS, whose pipelines take
    // longer to build. MH4N_HOLD_INCOMPLETE=0: shown as they are.
    u64 skipped_draws_ = 0;      // draws left out so far
    u64 skipped_big_draws_ = 0;  // ... of them big ones (BigDraw): only those hold a picture

    // A big draw: its box (the viewport within the scissor) covers a quarter of its target or more, and it is a full-screen
    // pass (a quad: 6 vertices at most) or a large mesh (300 or more: the sky, the ground, a wall, a monster). Left out,
    // such a draw was a black flash or a hole in the picture; a small one (a spark, a particle, a small prop) missing for a
    // frame shows less than the picture held for one: at 30 FPS one held picture is a 66 ms gap, two are 100 ms.
    static bool BigDraw(const pica::DrawCall& draw, const DrawGeometry& g, u32 width, u32 height, u32 scale) {
        const s64 x0 = std::max<s64>(static_cast<s64>(g.viewport.x), g.scissor.x);
        const s64 y0 = std::max<s64>(static_cast<s64>(g.viewport.y), g.scissor.y);
        const s64 x1 = std::min<s64>(static_cast<s64>(g.viewport.x + g.viewport.width), s64{g.scissor.x} + g.scissor.width);
        const s64 y1 = std::min<s64>(static_cast<s64>(g.viewport.y + g.viewport.height), s64{g.scissor.y} + g.scissor.height);
        const s64 area = std::max<s64>(0, x1 - x0) * std::max<s64>(0, y1 - y0);
        const s64 whole = s64{width} * height * scale * scale;
        if (whole <= 0 || area * 4 < whole) {
            return false;
        }
        return draw.num_vertices <= 6 || draw.num_vertices >= 300;
    }
    struct HeldScreen {
        u32 width = 0, height = 0;
        std::shared_ptr<const SharedScreenPicture> last;  // its last picture with nothing left out
        u64 skipped_seen = 0;                             // skipped_big_draws_ at its previous transfer
        u64 small_seen = 0;                               // skipped_draws_ then
        u32 holds = 0;                                    // its pictures replaced by `last` in a row
        u64 held_since_us = 0;                            // when the first of them came
        bool expired = false;                             // held kMaxHoldUs already: shown as they come until a whole one
    };
    std::vector<HeldScreen> held_screens_;
    u64 held_pictures_ = 0, small_shown_ = 0;  // (counted for the log)

    void HoldIncomplete(ScaledScreen& entry, u32 width, u32 height) {
        static const bool on = [] { const char* v = std::getenv("MH4N_HOLD_INCOMPLETE"); return v == nullptr || *v != '0'; }();
        constexpr u64 kMaxHoldUs = 50000;  // (3 frames at 60 FPS, 6 at 120)
        HeldScreen* h = nullptr;
        for (HeldScreen& s : held_screens_) {
            if (s.width == width && s.height == height) h = &s;
        }
        if (h == nullptr) {
            h = &held_screens_.emplace_back();
            h->width = width;
            h->height = height;
            h->skipped_seen = skipped_big_draws_;
        }
        const bool incomplete = skipped_big_draws_ != h->skipped_seen;
        h->skipped_seen = skipped_big_draws_;
        if (!incomplete && skipped_draws_ != h->small_seen && small_shown_++ % 64 == 0) {
            MH4N_LOG(Info, kLog, "a picture with only small draws left out is shown ({} so far)", small_shown_);
        }
        h->small_seen = skipped_draws_;
        if (on && incomplete && h->last != nullptr && !h->expired) {
            const u64 now_us = static_cast<u64>(std::chrono::duration_cast<std::chrono::microseconds>(
                                                    std::chrono::steady_clock::now().time_since_epoch()).count());
            if (h->holds == 0) {
                h->held_since_us = now_us;
            }
            if (now_us - h->held_since_us < kMaxHoldUs) {
                entry.picture = h->last;  // (the new one is let go: its buffer is free again, in the queue's order)
                ++h->holds;
                if (held_pictures_++ % 64 == 0) {
                    MH4N_LOG(Info, kLog, "a picture with draws left out (pipelines still being built) is not shown: the last "
                                         "whole one stands in ({} so far)", held_pictures_);
                }
                return;
            }
            h->expired = true;
        }
        if (!incomplete) {
            h->last = entry.picture;
            h->holds = 0;
            h->expired = false;
        }
    }
    // The vertex-stage uniforms last uploaded (DrawImpl): where, from which uniforms, in which recording.
    struct VsUniformsUploaded {
        u64 offset = 0;
        u64 serial = 0;
        u64 recording = ~0ull;
        shader::VertexUniformData data{};
    } vs_last_;
    bool block_unwritten_ = false;  // the fragment block changed since the uniform ring last had it written (DrawImpl)
    u64 tally_passes_ = 0, tally_transitions_ = 0, tally_ring_waits_ = 0, tally_ring_wait_us_ = 0;  // rhi::Tally() at the last TakeFrameCounters
    cpuvertex::Counters seen_cpu_{};
    // SetShadows as the last draw saw it, and whether this setting's skipped draws and cleared map are in the log yet
    bool shadows_were_ = true, shadows_off_told_ = false, shadows_clear_told_ = false;
};

}  // namespace

void SetShadows(bool on) {
    g_shadows.store(on, std::memory_order_relaxed);
}

bool Shadows() {
    return g_shadows.load(std::memory_order_relaxed);
}

std::unique_ptr<NativeGpu> MakeNativeGpu(Host& host, const NativeOptions& options) {
    return std::make_unique<Impl>(host, options);
}

MH4N_OPTION("capture-player", "--force-ubershader", "",
            "--gpu native: every draw takes the ubershader (with the GPU time per frame, the forced-ubershader timing of D5)");
MH4N_OPTION("runner", "--scale", "N",
            "--gpu native-display: the internal resolution, 1 (the 3DS's, the default) to 10 times it; the screens are published "
            "at N times their size");
MH4N_OPTION("capture-player", "--scale", "N",
            "--gpu native: render at N times the PICA's resolution (1 to 10, default 1); guest memory, and so what is judged, "
            "stays 1x (downscaled)");
MH4N_OPTION("runner", "--target-textures", "on|off",
            "--gpu native-display: a texture that is exactly a render target samples the target's image (on, the default), "
            "or is written back and decoded from guest memory (off)");
MH4N_OPTION("capture-player", "--target-textures", "on|off",
            "--gpu native: a texture that is exactly a render target samples the target's image (on, the default), or is "
            "written back and decoded from guest memory (off)");

MH4N_OPTION("runner", "--async-screens", "on|off",
            "--gpu native-display: a display transfer of a frame the GPU is still drawing goes to the GPU with no wait, its "
            "framebuffer's memory written only when something reads it (on, the default), or waits and blits on the CPU (off)");
MH4N_OPTION("capture-player", "--async-screens", "on|off",
            "--gpu native: display transfers on the GPU, their framebuffers written when read (default off: the player "
            "judges the memory of every command)");

MH4N_OPTION("runner", "--lighting-fixup", "gs|vs|off|auto",
            "--gpu native: the quaternion fix-up of lit draws in a geometry stage (gs), in the vertex stage (vs), or none (off); "
            "auto (the default): vs on every GPU (MH4N_LIGHTING_FIXUP overrides it)");
MH4N_OPTION("capture-player", "--lighting-fixup", "gs|vs|off|auto",
            "--gpu native: the quaternion fix-up of lit draws in a geometry stage (gs), in the vertex stage (vs), or none (off); "
            "auto (the default): vs on every GPU");

MH4N_OPTION("runner", "--shadows", "on|off",
            "--gpu native: the shadow maps drawn (on, the default), or not (off: no shadow-write draws and no clear of them; each map "
            "cleared once to nothing in the way, so nothing is in shadow). An app changes it while the game runs (SetShadows)");

MH4N_REGISTER(GpuFactory, "native", [](Host& host, const Options& options) -> std::unique_ptr<Gpu> {
    if (options.Has("--shadows")) {  // (process-wide: SetShadows)
        const std::string shadows = options.GetString("--shadows", "on");
        SetShadows(!(shadows == "off" || shadows == "0" || shadows == "false" || shadows == "no"));
    }
    NativeOptions native;
    // on by default where the screens are shown (native-display), off for the capture player's --gpu native
    const std::string async_screens =
        options.GetString("--async-screens", options.GetString("--gpu", "") == "native-display" ? "on" : "off");
    native.async_screens = async_screens == "on" || async_screens == "1" || async_screens == "true" || async_screens == "yes";
    if (!native.async_screens && async_screens != "off" && async_screens != "0" && async_screens != "false" &&
        async_screens != "no") {
        MH4N_LOG(Error, kLog, "--async-screens {}: on or off; off is used", async_screens);
    }
    native.options = options;
    native.force_ubershader = options.Has("--force-ubershader");
    const std::string target_textures = options.GetString("--target-textures", "on");
    if (target_textures == "off" || target_textures == "0" || target_textures == "false" || target_textures == "no") {
        native.target_textures = false;
    } else if (target_textures != "on" && target_textures != "1" && target_textures != "true" && target_textures != "yes") {
        MH4N_LOG(Error, kLog, "--target-textures {}: on or off; on is used", target_textures);
    }
    u64 scale = 1;
    try {
        scale = options.GetUInt("--scale", 1);
    } catch (const OptionError& e) {
        MH4N_LOG(Error, kLog, "--scale: {}", e.what());
    }
    if (scale < 1 || scale > kMaxScale) {
        MH4N_LOG(Error, kLog, "--scale {} is outside 1 to {}: {} is used", scale, kMaxScale, std::clamp<u64>(scale, 1, kMaxScale));
        scale = std::clamp<u64>(scale, 1, kMaxScale);
    }
    native.scale = static_cast<u32>(scale);
    native.lighting_fixup = options.GetString("--lighting-fixup", "auto");
    return MakeNativeGpu(host, native);
});

}  // namespace mh4n::gpu::native
