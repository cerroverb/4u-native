// The word positions of the framebuffer and colour fill registers are Azahar's, from
// src/video_core/pica/regs_external.h and regs_lcd.h at 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6
// (GPL-2.0-or-later), as gpu_capture/image.cpp has them; see native/THIRD_PARTY.md. The rest is ours.
// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project (regs_external.h)
// Copyright 2023-2024 Citra Emulator Project / Azahar Emulator Project (regs_lcd.h)
#include "display_mem/display_mem.h"

#include <algorithm>
#include <utility>

#include "gpu/native/scaled_screens.h"
#define XXH_INLINE_ALL
#include <xxhash.h>
#include "gpu/queue/queue_gpu.h"
#include "mh4n/options.h"
#include "mh4n/pacing_trace.h"
#include "mh4n/gpu_debug.h"
#include "mh4n/log.h"

#include <cstdlib>
#include <cstring>
#include "mh4n/registry.h"

#if MH4N_DISPLAY_MEM_PLATFORM
#include "platform/platform_native.h"
#endif

namespace mh4n::display_mem {

namespace {

constexpr const char* kLog = "GPU.DisplayMem";

#if MH4N_DISPLAY_MEM_PLATFORM
// The top screen's framebuffer size register (word 0x17 of the first framebuffer block, which starts
// at word 0x100 of the GPU registers): the high half is the number of columns stored, the width the
// top screen shows (400, or what the hooks make of it).
constexpr u32 kTopSizeRegister = gpu_capture::DisplayShadow::kGpuBase + 4 * (0x100 + 0x17);
#endif

// Word indices of a screen's framebuffer registers (framebuffer_config[screen] of the GPU register
// array, as image.cpp reads them) and of the colour fill registers in the LCD array.
constexpr u32 kFbConfigBase = 0x100;
constexpr u32 kFbConfigWords = 0x40;
constexpr u32 kFbSize = 0x17;
constexpr u32 kFbAddressLeft1 = 0x1A;
constexpr u32 kFbAddressLeft2 = 0x1B;
constexpr u32 kFbFormat = 0x1C;
constexpr u32 kFbActive = 0x1E;
constexpr u32 kFbStride = 0x24;
constexpr u32 kLcdColorFillTop = 0x81;
constexpr u32 kLcdColorFillBottom = 0x281;

// The RGBA8 `pixels` (width x height) enlarged `scale` times, nearest.
std::shared_ptr<std::vector<u8>> Enlarge(const std::vector<u8>& pixels, u32 width, u32 height, u32 scale) {
    const std::size_t out_width = static_cast<std::size_t>(width) * scale;
    auto out = std::make_shared<std::vector<u8>>(out_width * height * scale * 4);
    for (u32 y = 0; y < height; ++y) {
        u8* row = out->data() + static_cast<std::size_t>(y) * scale * out_width * 4;
        const u8* in = pixels.data() + static_cast<std::size_t>(y) * width * 4;
        for (u32 x = 0; x < width; ++x) {
            for (u32 i = 0; i < scale; ++i) {
                std::memcpy(row + (static_cast<std::size_t>(x) * scale + i) * 4, in + static_cast<std::size_t>(x) * 4, 4);
            }
        }
        for (u32 i = 1; i < scale; ++i) {
            std::memcpy(row + i * out_width * 4, row, out_width * 4);
        }
    }
    return out;
}

}  // namespace

DisplayMemGpu::DisplayMemGpu(gpu::Host& host, std::unique_ptr<gpu::Gpu> inner)
    : host_(host), inner_(std::move(inner)) {
    // Load takes the registers as little-endian bytes: read them from the inner GPU's register file.
    std::vector<u8> gpu_regs(4 * gpu_capture::DisplayShadow::kGpuRegWords);
    std::vector<u8> lcd_regs(4 * gpu_capture::DisplayShadow::kLcdRegWords);
    const auto put = [](std::vector<u8>& bytes, u32 index, u32 value) {
        for (u32 i = 0; i < 4; ++i) {
            bytes[4 * index + i] = static_cast<u8>(value >> (8 * i));
        }
    };
    for (u32 i = 0; i < gpu_capture::DisplayShadow::kGpuRegWords; ++i) {
        put(gpu_regs, i, inner_->ReadReg(gpu_capture::DisplayShadow::kGpuBase + 4 * i));
    }
    for (u32 i = 0; i < gpu_capture::DisplayShadow::kLcdRegWords; ++i) {
        put(lcd_regs, i, inner_->ReadReg(gpu_capture::DisplayShadow::kLcdBase + 4 * i));
    }
    shadow_.Load(gpu_regs.data(), lcd_regs.data());
    scaled_ = dynamic_cast<gpu::ScaledScreens*>(inner_.get());
    scale_ = scaled_ != nullptr ? std::max<u32>(scaled_->ScreenScale(), 1) : 1;
    if (scaled_ != nullptr && !scaled_->HasScreens()) {
        scaled_ = nullptr;  // (a scale of 1 without asynchronous screens: the screens come from guest memory)
    }
}

void DisplayMemGpu::Execute(const gpu::GxCommand& command) {
    inner_->Execute(command);
}

void DisplayMemGpu::WriteReg(u32 addr, u32 value) {
    inner_->WriteReg(addr, value);
    shadow_.WriteReg(addr, value);
}

u32 DisplayMemGpu::ReadReg(u32 addr) {
    return inner_->ReadReg(addr);
}

void DisplayMemGpu::SetBufferSwap(u32 screen, const gpu::FrameBufferInfo& info) {
    inner_->SetBufferSwap(screen, info);
    if (screen == 0) {
        ++top_swaps_;
    }
    // GPU::VirtualToPhysicalAddress: 0 stays 0, an address in no window is kept.
    const auto physical = [this](u32 vaddr) {
        if (vaddr == 0) {
            return 0u;
        }
        const auto converted = host_.VirtualToPhysical(vaddr);
        return converted ? *converted : vaddr;
    };
    shadow_.ApplySwap(screen, info, physical(info.address_left), physical(info.address_right));
}

void DisplayMemGpu::SetColorFill(u32 fill) {
    inner_->SetColorFill(fill);
    shadow_.ApplyFill(fill);
}

void DisplayMemGpu::FlushRegion(u32 vaddr, u32 size) {
    inner_->FlushRegion(vaddr, size);
}

void DisplayMemGpu::InvalidateRegion(u32 vaddr, u32 size) {
    inner_->InvalidateRegion(vaddr, size);
}

namespace {
// The pacing trace (MH4N_PACING_TRACE): a renderer frame counter worth a mark (the times need MH4N_GPU_COUNTERS too:
// the renderer reads its clock only then).
void MarkCounter(const gpu::Counter& c) {
    const std::string& n = c.name;
    const pacing::Kind kind = n == "pipeline_sync_us" ? pacing::kRenderSync
                              : n == "tex_us"         ? pacing::kRenderTex
                              : n == "pace_us"        ? pacing::kRenderPace
                              : n == "draw_us"        ? pacing::kRenderDraw
                              : n == "writeback_us"   ? pacing::kRenderWriteback
                              : n == "ring_wait_us"   ? pacing::kRenderRing
                              : n == "pipeline_skipped" ? pacing::kRenderSkipped
                              : n == "cpu_us"         ? pacing::kRenderCpu
                              : n == "materialize_us" ? pacing::kRenderMaterialize
                              : n == "compose_us"     ? pacing::kRenderCompose
                              : n == "vtx_us"         ? pacing::kRenderVertex
                              : n == "prep_us"        ? pacing::kRenderPrep
                              : n == "tgt_us"         ? pacing::kRenderTarget
                              : n == "fallback_draws" ? pacing::kRenderFallback
                              : n == "pipeline_async_builds" ? pacing::kRenderAsyncBuilds
                              : n == "texture_uploads" ? pacing::kRenderUploads
                              : n == "gpu_us"         ? pacing::kRenderGpu
                              : n == "decode_us"      ? pacing::kRenderDecode
                              : n == "upload_us"      ? pacing::kRenderUpload
                              : n == "load_us"        ? pacing::kRenderLoad
                              : n == "create_us"      ? pacing::kRenderCreate
                              : n == "writebacks_cpu" ? pacing::kRenderWbCpu
                              : n == "writebacks_texture" ? pacing::kRenderWbTexture
                              : n == "writebacks_gx"  ? pacing::kRenderWbGx
                              : n == "writebacks_overlap" ? pacing::kRenderWbOverlap
                              : n == "writebacks_other" ? pacing::kRenderWbOther
                              : n == "ab_phase"       ? pacing::kRenderAbPhase
                                                      : pacing::Kind{0};
    if (kind != 0 && c.value != 0) {
        pacing::Mark(kind, static_cast<u32>(std::min<u64>(c.value, 0xFFFFFFFFu)));
    }
}
}  // namespace

void DisplayMemGpu::VBlank(u32 screen) {
    inner_->VBlank(screen);
    // MH4N_GPU_COUNTERS=N: every N top-screen VBlanks, the inner GPU's non-zero frame counters (a debugging aid)
    static const int counters_every = [] { const char* v = std::getenv("MH4N_GPU_COUNTERS"); return v ? std::atoi(v) : 0; }();
    if (screen == 0 && counters_every > 0) {
        if (auto* debug = dynamic_cast<gpu::GpuDebug*>(inner_.get())) {
            std::string line;
            for (const gpu::Counter& c : debug->TakeFrameCounters()) {
                MarkCounter(c);
                if (c.value != 0) {
                    line += fmt::format(" {}={}", c.name, c.value);
                }
            }
            // MH4N_GPU_TIME=1: the GPU's time for the frame's work (timestamps; it waits for the GPU at every VBlank)
            static const bool gpu_time = [] { const char* v = std::getenv("MH4N_GPU_TIME"); return v != nullptr && *v == '1'; }();
            if (gpu_time) {
                if (const std::optional<u64> ns = debug->TakeFrameGpuTimeNs()) {
                    line += fmt::format(" gpu_us={}", *ns / 1000);
                }
            }
            if (++counter_vblanks_ % counters_every == 0) {
                MH4N_LOG(Info, kLog, "vblank {}:{}", counter_vblanks_, line);
            }
        }
    }
    // the pacing trace (MH4N_PACING_TRACE, without MH4N_GPU_COUNTERS: it takes them): where the renderer's frame went
    if (screen == 0 && counters_every <= 0 && pacing::Trace::Get().On()) {
        if (auto* debug = dynamic_cast<gpu::GpuDebug*>(inner_.get())) {
            for (const gpu::Counter& c : debug->TakeFrameCounters()) {
                MarkCounter(c);
            }
        }
    }
    if (screen != 0 || sinks_.empty()) {
        return;
    }
    if (scaled_ != nullptr) {
        const bool hidden = std::any_of(sinks_.begin(), sinks_.end(), [](const present::ScreenSink* s) {
            return s->ScreenHidden(1);
        });
        if (hidden != touch_hidden_) {
            touch_hidden_ = hidden;
            scaled_->SetTouchScreenShown(!hidden);
        }
    }
    present::DisplayFrame frame;
    frame.sequence = next_sequence_++;
    if (link_ != nullptr) {
        frame.game_frame = link_->game_frame.load();
        if (link_->runtime != nullptr) {
            frame.tick = link_->runtime->Clock().Now();
        }
    }
    frame.top = BuildScreen(0);
    // the touch screen while it is hidden: not built (the renderer draws neither it nor its picture then, and
    // building it from its framebuffer's memory would wait for the GPU)
    const bool bottom_hidden = scaled_ != nullptr && touch_hidden_;
    if (!bottom_hidden) {
        frame.bottom = BuildScreen(1);
    }
    pacing::Mark(pacing::kPublish, frame.game_frame);
    pacing::Mark(pacing::kPublishSwap, top_swaps_);
    for (present::ScreenSink* sink : sinks_) {
        sink->Publish(frame);
    }
}

std::optional<present::ScreenImage> DisplayMemGpu::BuildScreen(u32 screen) {
#if MH4N_DISPLAY_MEM_PLATFORM
    if (screen == 0 && link_ != nullptr && link_->runtime != nullptr) {
        // The reference's hooks write the top framebuffer's size register; here the platform keeps
        // what they asked for.
        if (const auto platform = platform::NativePlatform::Find(*link_->runtime)) {
            const u32 rows = std::min<u32>(platform->TopScreenRows(), 0xFFFF);
            if (rows != 0) {
                const u32 old = shadow_.GpuReg((kTopSizeRegister - gpu_capture::DisplayShadow::kGpuBase) / 4);
                shadow_.WriteReg(kTopSizeRegister, (old & 0xFFFF) | (rows << 16));
            }
        }
    }
#endif
    if (scaled_ != nullptr) {
        if (shared_asked_ != shared_wanted_) {  // (asked here, on the renderer's own thread)
            MH4N_LOG(Info, kLog, "shared screens asked of the renderer: {}", shared_wanted_ ? "on" : "off");
            scaled_->SetSharedScreens(shared_wanted_);
            shared_asked_ = shared_wanted_;
        }
        if (shared_wanted_) {
            if (std::optional<present::ScreenImage> shared = BuildSharedScreen(screen)) {
                last_error_[screen].clear();
                last_scaled_[screen] = LastScaled{};
                return shared;
            }
        }
        if (std::optional<present::ScreenImage> scaled = BuildScaledScreen(screen)) {
            last_error_[screen].clear();
            return scaled;
        }
        last_scaled_[screen] = LastScaled{};
        PrepareScreenMemory(screen);  // a framebuffer the renderer still owes (asynchronous screens) is written first
    }
    static const bool check = [] {
        const char* v = std::getenv("MH4N_DISPLAY_CHECK");
        return v != nullptr && *v == '1';
    }();
    std::optional<present::ScreenImage> fast = BuildMemoryScreen(screen);
    if (fast && !check) {
        last_error_[screen].clear();
        return fast;
    }
    gpu_capture::Image image;
    std::string error;
    const gpu_capture::PhysicalPointerFn memory = [this](u32 paddr, u32 size) -> const u8* {
        return host_.PhysicalPointer(paddr, size);
    };
    if (!shadow_.BuildScreen(screen, memory, image, &error)) {
        if (error != last_error_[screen]) {
            MH4N_LOG(Warning, kLog, "screen {} is not shown: {}", screen, error);
            last_error_[screen] = error;
        }
        return std::nullopt;
    }
    last_error_[screen].clear();
    present::ScreenImage result;
    result.width = image.width;
    result.height = image.height;
    auto pixels = std::make_shared<std::vector<u8>>(static_cast<std::size_t>(image.width) * image.height * 4);
    for (std::size_t i = 0; i < static_cast<std::size_t>(image.width) * image.height; ++i) {
        (*pixels)[4 * i + 0] = image.rgb[3 * i + 0];
        (*pixels)[4 * i + 1] = image.rgb[3 * i + 1];
        (*pixels)[4 * i + 2] = image.rgb[3 * i + 2];
        (*pixels)[4 * i + 3] = 0xFF;
    }
    if (scale_ > 1) {
        // The renderer has no picture of this framebuffer at its scale: the 1x one, as large.
        pixels = Enlarge(*pixels, result.width, result.height, scale_);
        result.width *= scale_;
        result.height *= scale_;
    }
    result.pixels = std::move(pixels);
    result.region = {0, 0, result.width, result.height};
    if (fast) {  // MH4N_DISPLAY_CHECK=1: the fast path against this one
        const bool same = fast->width == result.width && fast->height == result.height &&
                          *fast->pixels == *result.pixels;
        static int reported = 0;
        if (!same && reported++ < 5) {
            MH4N_LOG(Error, kLog, "screen {}: the fast picture differs from the reference one", screen);
        }
    }
    return result;
}

namespace {

// One framebuffer pixel as RGBA (gpu_capture::DecodeDisplayPixel, alpha 0xFF).
template <u32 Format>
inline void PutPixel(const u8* p, u8* out) {
    const auto c5 = [](u32 v) { return static_cast<u8>((v << 3) | (v >> 2)); };
    const auto c6 = [](u32 v) { return static_cast<u8>((v << 2) | (v >> 4)); };
    const auto c4 = [](u32 v) { return static_cast<u8>(v * 17); };
    if constexpr (Format == 0) {  // RGBA8
        out[0] = p[3], out[1] = p[2], out[2] = p[1];
    } else if constexpr (Format == 1) {  // RGB8
        out[0] = p[2], out[1] = p[1], out[2] = p[0];
    } else {
        const u32 v = static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8);
        if constexpr (Format == 2) {  // RGB565
            out[0] = c5((v >> 11) & 0x1F), out[1] = c6((v >> 5) & 0x3F), out[2] = c5(v & 0x1F);
        } else if constexpr (Format == 3) {  // RGB5A1
            out[0] = c5((v >> 11) & 0x1F), out[1] = c5((v >> 6) & 0x1F), out[2] = c5((v >> 1) & 0x1F);
        } else {  // RGBA4
            out[0] = c4((v >> 12) & 0xF), out[1] = c4((v >> 8) & 0xF), out[2] = c4((v >> 4) & 0xF);
        }
    }
    out[3] = 0xFF;
}

// DisplayShadow::BuildScreen's picture (RendererSoftware's reading order, Azahar's index y * stride + stride - x:
// for x = 0 the first pixel of the next column, past the buffer for the last one), as RGBA.
template <u32 Format, u32 Bpp>
void DecodeFramebuffer(const u8* data, const u8* extra, u32 stride, u32 columns, u8* out) {
    static const u8 kZero[4] = {0, 0, 0, 0};
    const u64 stored = static_cast<u64>(columns) * stride;
    for (u32 y = 0; y < columns; ++y) {
        const u64 first = static_cast<u64>(y) * stride + stride;  // x = 0
        const u8* p0 = first < stored ? data + first * Bpp : (extra ? extra : kZero);
        PutPixel<Format>(p0, out + static_cast<size_t>(y) * 4);
        const u8* column = data + static_cast<u64>(y) * stride * Bpp;
        for (u32 x = 1; x < stride; ++x) {
            PutPixel<Format>(column + static_cast<size_t>(stride - x) * Bpp,
                             out + (static_cast<size_t>(x) * columns + y) * 4);
        }
    }
}

}  // namespace

std::optional<present::ScreenImage> DisplayMemGpu::BuildMemoryScreen(u32 screen) {
    const u32 fb = kFbConfigBase + kFbConfigWords * screen;
    const u32 fill = shadow_.LcdReg(screen == 0 ? kLcdColorFillTop : kLcdColorFillBottom);
    if (((fill >> 24) & 1) != 0) {
        return std::nullopt;  // a colour fill: the slow path makes it (rare)
    }
    const u32 address = shadow_.GpuReg(fb + kFbActive) == 0 ? shadow_.GpuReg(fb + kFbAddressLeft1)
                                                             : shadow_.GpuReg(fb + kFbAddressLeft2);
    const u32 format = shadow_.GpuReg(fb + kFbFormat) & 7;
    const u32 bpp = gpu_capture::BytesPerPixel(format);
    if (bpp == 0 || format > 4) {
        return std::nullopt;
    }
    const u32 stride = shadow_.GpuReg(fb + kFbStride) / bpp;
    const u32 columns = shadow_.GpuReg(fb + kFbSize) >> 16;
    const u64 stored = static_cast<u64>(columns) * stride * bpp;
    if (stride == 0 || columns == 0 || stored > 0xFFFFFFFFull) {
        return std::nullopt;
    }
    const u8* data = host_.PhysicalPointer(address, static_cast<u32>(stored));
    if (data == nullptr) {
        return std::nullopt;
    }
    const u8* extra = host_.PhysicalPointer(address + static_cast<u32>(stored), bpp);
    // what the picture is made of: the registers and the bytes (the extra pixel too)
    u64 key = XXH3_64bits(data, static_cast<size_t>(stored));
    const u32 params[6] = {address, format, stride, columns, scale_, extra != nullptr ? 1u : 0u};
    key = XXH3_64bits_withSeed(params, sizeof(params), key);
    if (extra != nullptr) {
        key = XXH3_64bits_withSeed(extra, bpp, key);
    }
    LastScreen& last = last_screen_[screen];
    if (last.valid && last.key == key) {
        return last.image;  // unchanged: the same pixels, nothing decoded
    }
    present::ScreenImage result;
    result.width = columns;
    result.height = stride;
    const size_t bytes = static_cast<size_t>(columns) * stride * 4;
    std::shared_ptr<std::vector<u8>> pixels;
    last.image.pixels.reset();
    if (last.storage != nullptr && last.storage.use_count() == 1 && scale_ == 1 && last.storage->size() == bytes) {
        pixels = std::move(last.storage);  // nobody holds the old picture any more: its storage again
    } else {
        pixels = std::make_shared<std::vector<u8>>(bytes);
    }
    last.storage.reset();
    switch (format) {
    case 0: DecodeFramebuffer<0, 4>(data, extra, stride, columns, pixels->data()); break;
    case 1: DecodeFramebuffer<1, 3>(data, extra, stride, columns, pixels->data()); break;
    case 2: DecodeFramebuffer<2, 2>(data, extra, stride, columns, pixels->data()); break;
    case 3: DecodeFramebuffer<3, 2>(data, extra, stride, columns, pixels->data()); break;
    default: DecodeFramebuffer<4, 2>(data, extra, stride, columns, pixels->data()); break;
    }
    if (scale_ > 1) {
        pixels = Enlarge(*pixels, result.width, result.height, scale_);
        result.width *= scale_;
        result.height *= scale_;
    }
    last.storage = pixels;
    result.pixels = std::move(pixels);
    result.region = {0, 0, result.width, result.height};
    last.key = key;
    last.valid = true;
    last.image = result;
    return result;
}

std::optional<present::ScreenImage> DisplayMemGpu::BuildScaledScreen(u32 screen) {
    // The framebuffer the shadow shows (RendererSoftware::LoadFBToScreenInfo, as DisplayShadow::BuildScreen reads it).
    const u32 fb = kFbConfigBase + kFbConfigWords * screen;
    const u32 fill = shadow_.LcdReg(screen == 0 ? kLcdColorFillTop : kLcdColorFillBottom);
    if (((fill >> 24) & 1) != 0) {
        return std::nullopt;  // a colour fill: the 1x path makes it
    }
    const bool first_buffer = shadow_.GpuReg(fb + kFbActive) == 0;
    const u32 address = shadow_.GpuReg(fb + (first_buffer ? kFbAddressLeft1 : kFbAddressLeft2));
    const u32 other = shadow_.GpuReg(fb + (first_buffer ? kFbAddressLeft2 : kFbAddressLeft1));
    const u32 format = shadow_.GpuReg(fb + kFbFormat) & 7;
    const u32 bpp = gpu_capture::BytesPerPixel(format);
    const u32 stride = shadow_.GpuReg(fb + kFbStride);
    const u32 rows = shadow_.GpuReg(fb + kFbSize) >> 16;
    if (bpp == 0 || stride / bpp == 0 || rows == 0) {
        return std::nullopt;
    }
    present::ScreenImage result;
    result.width = rows * scale_;
    result.height = stride / bpp * scale_;
    LastScaled& last = last_scaled_[screen];
    const bool last_fits = last.image && last.image->width == result.width && last.image->height == result.height;
    auto pixels = std::make_shared<std::vector<u8>>();
    u64 sequence = 0;
    using Read = gpu::ScaledScreens::ScreenRead;
    Read read = scaled_->ReadScaledScreen(address, format, stride, rows, *pixels, /*wait=*/false, &sequence);
    if (read == Read::Pending) {
        // The GPU has not finished this picture (it may be frames behind the game): the screen's other framebuffer, the
        // frame before, when its picture is done and not older than the last one shown; else the last one again, for
        // kMaxRepeats VBlanks at most, so that a GPU that stays behind is waited for rather than the screen frozen.
        // (8: a phone's GPU often finishes a frame 3 to 5 VBlanks after the game made it; a wait there stops the
        // renderer, and the game behind it, every few frames: a Moto g06 showed one frame in five at 110-170 ms.)
        constexpr u64 kMaxRepeats = 8;
        if (other != 0 && other != address &&
            scaled_->ReadScaledScreen(other, format, stride, rows, *pixels, /*wait=*/false, &sequence) == Read::Ready &&
            (!last_fits || sequence >= last.sequence)) {
            read = Read::Ready;
        } else if (last_fits && next_sequence_ - last.made < kMaxRepeats) {
            return last.image;
        } else {
            read = scaled_->ReadScaledScreen(address, format, stride, rows, *pixels, /*wait=*/true, &sequence);
        }
    }
    if (read != Read::Ready || pixels->size() != static_cast<std::size_t>(result.width) * result.height * 4) {
        return std::nullopt;
    }
    result.pixels = std::move(pixels);
    result.region = {0, 0, result.width, result.height};
    last.image = result;
    last.sequence = sequence;
    last.made = next_sequence_;
    return result;
}

std::optional<present::ScreenImage> DisplayMemGpu::BuildSharedScreen(u32 screen) {
    // (the registers as BuildScaledScreen reads them)
    const u32 fb = kFbConfigBase + kFbConfigWords * screen;
    const u32 fill = shadow_.LcdReg(screen == 0 ? kLcdColorFillTop : kLcdColorFillBottom);
    if (((fill >> 24) & 1) != 0) {
        return std::nullopt;
    }
    const u32 address = shadow_.GpuReg(fb + (shadow_.GpuReg(fb + kFbActive) == 0 ? kFbAddressLeft1 : kFbAddressLeft2));
    const u32 format = shadow_.GpuReg(fb + kFbFormat) & 7;
    const u32 bpp = gpu_capture::BytesPerPixel(format);
    const u32 stride = shadow_.GpuReg(fb + kFbStride);
    const u32 rows = shadow_.GpuReg(fb + kFbSize) >> 16;
    if (bpp == 0 || stride / bpp == 0 || rows == 0) {
        return std::nullopt;
    }
    gpu::ScaledScreens::SharedScreen got;
    if (scaled_->ReadSharedScreen(address, format, stride, rows, got) != gpu::ScaledScreens::ScreenRead::Ready ||
        got.picture == nullptr) {
        return std::nullopt;
    }
    present::ScreenImage result;
    result.shared = std::move(got.picture);
    result.region = got.region;
    result.width = got.region.width;  // (the size at the renderer's scale, as the CPU form of these screens has it)
    result.height = got.region.height;
    if (!result.Valid()) {
        return std::nullopt;
    }
    return result;
}

void DisplayMemGpu::PrepareScreenMemory(u32 screen) {
    const u32 fb = kFbConfigBase + kFbConfigWords * screen;
    const u32 address = shadow_.GpuReg(fb + kFbActive) == 0 ? shadow_.GpuReg(fb + kFbAddressLeft1)
                                                             : shadow_.GpuReg(fb + kFbAddressLeft2);
    const u32 bpp = std::max<u32>(gpu_capture::BytesPerPixel(shadow_.GpuReg(fb + kFbFormat) & 7), 1);
    const u64 bytes = u64{shadow_.GpuReg(fb + kFbStride)} * (shadow_.GpuReg(fb + kFbSize) >> 16) + bpp;  // (the extra pixel too)
    scaled_->PrepareMemory(address, static_cast<u32>(std::min<u64>(bytes, 0xFFFFFFFFull - address)));
}

void DisplayMemGpu::AddScreenSink(present::ScreenSink* sink) {
    sinks_.push_back(sink);
    shared_wanted_ = std::all_of(sinks_.begin(), sinks_.end(), [](const present::ScreenSink* s) {
        return s->WantsSharedPictures();
    });
    MH4N_LOG(Info, kLog, "a screen sink: {} sink(s), shared pictures {}", sinks_.size(), shared_wanted_ ? "wanted" : "not wanted");
}

void DisplayMemGpu::RemoveScreenSink(present::ScreenSink* sink) {
    std::erase(sinks_, sink);
}

void DisplayMemGpu::ReadScreen(const present::ScreenImage& screen, std::function<void(present::RgbImage)> done) {
    present::RgbImage out;
    if (screen.Valid() && screen.IsCpu()) {
        out.width = screen.width;
        out.height = screen.height;
        const std::size_t count = static_cast<std::size_t>(screen.width) * screen.height;
        out.rgb.resize(count * 3);
        for (std::size_t i = 0; i < count; ++i) {
            std::copy_n(screen.pixels->data() + 4 * i, 3, out.rgb.data() + 3 * i);
        }
    }
    done(std::move(out));
}

void DisplayMemGpu::Attach(std::shared_ptr<RuntimeLink> link) {
    link_ = std::move(link);
}

namespace {

std::unique_ptr<gpu::Gpu> MakeDisplayMem(gpu::Host& host, const Options& options) {
    const gpu::GpuFactory* null_factory = Registry<gpu::GpuFactory>::Instance().Find("null");
    if (null_factory == nullptr) {
        MH4N_LOG(Error, kLog, "the null GPU is not registered");
        return nullptr;
    }
    return std::make_unique<DisplayMemGpu>(host, (*null_factory)(host, options));
}

// The native renderer (gpu/native) with its screens shown from guest memory: it writes every target
// back before a display transfer, so at VBlank the framebuffers hold what it drew. Its GX engine
// starts with zeroed registers, so they are seeded with the values the null GPU starts with
// (PicaCore::InitializeRegs: the framebuffer defaults of nn::gx, the IRQ mask, ...).
std::unique_ptr<gpu::Gpu> MakeNativeDisplayOn(gpu::Host& host, const Options& options) {
    const auto& registry = Registry<gpu::GpuFactory>::Instance();
    const gpu::GpuFactory* native_factory = registry.Find("native");
    const gpu::GpuFactory* null_factory = registry.Find("null");
    if (native_factory == nullptr || null_factory == nullptr) {
        MH4N_LOG(Error, kLog, "the native or the null GPU is not registered");
        return nullptr;
    }
    std::unique_ptr<gpu::Gpu> inner = (*native_factory)(host, options);
    std::unique_ptr<gpu::Gpu> seed = (*null_factory)(host, options);
    if (inner == nullptr || seed == nullptr) {
        return nullptr;
    }
    using Shadow = gpu_capture::DisplayShadow;
    for (u32 i = 0; i < Shadow::kGpuRegWords; ++i) {
        const u32 addr = Shadow::kGpuBase + 4 * i;
        if (const u32 value = seed->ReadReg(addr); value != 0 && inner->ReadReg(addr) == 0) {
            inner->WriteReg(addr, value);
        }
    }
    for (u32 i = 0; i < Shadow::kLcdRegWords; ++i) {
        const u32 addr = Shadow::kLcdBase + 4 * i;
        if (const u32 value = seed->ReadReg(addr); value != 0 && inner->ReadReg(addr) == 0) {
            inner->WriteReg(addr, value);
        }
    }
    return std::make_unique<DisplayMemGpu>(host, std::move(inner));
}

// `--render-thread on` (or MH4N_RENDER_THREAD=1): the renderer and the screens' publication run on a thread of their
// own (gpu/queue, N2-22), so the game's thread only queues the GPU's work. Everything the renderer does, it does
// in the order the game asked, and InvalidateRegion waits for it: the guest sees the same memory, one frame later
// at most. Default off until each device has been measured.
bool RenderThreadWanted(const Options& options) {
    if (const char* env = std::getenv("MH4N_RENDER_THREAD")) {
        return *env == '1';
    }
    const std::string value = options.GetString("--render-thread", "off");
    return value == "on" || value == "1";
}

std::unique_ptr<gpu::Gpu> MakeNativeDisplay(gpu::Host& host, const Options& options) {
    if (!RenderThreadWanted(options)) {
        return MakeNativeDisplayOn(host, options);
    }
    // How many frames the game may run ahead of the renderer before its VBlank waits (`--render-queue N`, 2 to 4,
    // default 2): the queue's own default (4) let a GPU-bound phone run in bursts, a frame shown quickly and the next
    // late, with the input as many frames behind the picture.
    gpu::queue::QueueConfig config;
    u64 queued = 2;
    try {
        queued = options.GetUInt("--render-queue", 2);
    } catch (const OptionError& e) {
        MH4N_LOG(Error, kLog, "--render-queue: {}", e.what());
    }
    config.max_frames_queued = static_cast<u32>(std::clamp<u64>(queued, 2, 4));
    MH4N_LOG(Info, kLog, "the renderer runs on its own thread ({} frames queued at most)", config.max_frames_queued);
    // The wrapped GPU is made on the renderer thread (it owns every image it makes); `options` outlives the call.
    return std::make_unique<gpu::queue::QueueGpu>(
        host, [&options](gpu::Host& queue_host) { return MakeNativeDisplayOn(queue_host, options); }, config);
}

// The GPU factory gets no runtime: once the `devices` step has made the GPU, this step tells it
// which runtime it belongs to and follows the frame counter.
void AttachStep(Runtime& rt) {
    auto* gpu = gpu::FindGpu<DisplayMemGpu>(rt.Interfaces().gpu);
    if (gpu == nullptr) {
        return;
    }
    auto link = std::make_shared<RuntimeLink>();
    link->runtime = &rt;
    rt.OnFrame([link](u32 frame, u64) { link->game_frame.store(frame); });
    gpu->Attach(std::move(link));
}

}  // namespace

MH4N_REGISTER(gpu::GpuFactory, "null-display", MakeDisplayMem);
MH4N_REGISTER(present::DisplayGpu, "null-display", {});
MH4N_REGISTER(gpu::GpuFactory, "native-display", MakeNativeDisplay);
MH4N_OPTION("runner", "--render-queue", "N",
            "--render-thread on: the frames the game may run ahead of the renderer, 2 to 4 (default 2)");
MH4N_OPTION("runner", "--render-thread", "on|off",
            "--gpu native-display: run the renderer on a thread of its own (default off; MH4N_RENDER_THREAD=1 too)");
MH4N_REGISTER(present::DisplayGpu, "native-display", {});
MH4N_REGISTER(BootStep, "display_mem.attach", BootStep{Phase::Services, AttachStep});

}  // namespace mh4n::display_mem
