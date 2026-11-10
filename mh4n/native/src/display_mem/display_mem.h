// The `null-display` GPU (N2b-07): the null GPU, plus the screens it would show.
//
// It wraps the `null` GPU found in the registry (nothing of it is copied) and forwards every call.
// It keeps a gpu_capture::DisplayShadow of the display registers, loaded at start from the inner
// GPU's register file and updated by WriteReg, SetBufferSwap and SetColorFill as the capture player
// updates it. At VBlank(0) it builds both screens from the runtime host's physical memory with
// DisplayShadow::BuildScreen (what Azahar's software renderer shows, upright, at 1x) and publishes a
// present::DisplayFrame of CPU pixels (RGBA8) to its sinks. Nothing is built while there is no sink.
//
// The null GPU skips display transfers and texture copies, so the game's own screens stay as the
// CPU left them: this is for programs that draw with the CPU and for the app's plumbing, not a
// picture of the game.
//
// The `native-display` GPU wraps the native renderer the same way. With `--scale N` the renderer offers
// the screens at N x (gpu/native/scaled_screens.h): a screen whose framebuffer it drew is published at
// N times its size from its picture; any other (a fill, a CPU-drawn or 1x-only framebuffer) is built at 1x
// as above and enlarged N times (nearest), so a screen always has one size. With asynchronous screens (the
// renderer's `--async-screens`, also at 1x) its picture is used the same way, the screen's last picture is
// repeated while the GPU has not finished the next, and a framebuffer the renderer still owes is written
// (ScaledScreens::PrepareMemory) before it is read from guest memory.
//
// When every sink wants shared pictures (present::ScreenSink::WantsSharedPictures: the Android app), the renderer is asked
// for shared screens (scaled_screens.h) and a screen it drew so is published as its present::SharedPicture the VBlank
// its drawing was submitted, finished or not (the sink waits for the GPU on its side): nothing is read back or copied
// by the CPU. Any other screen comes as before.
//
// Threads (principle 6): only the scheduler thread calls it; sinks are added before the run and
// removed after it.
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gpu_capture/image.h"
#include "mh4n/gpu.h"
#include "mh4n/present.h"
#include "mh4n/runtime.h"

namespace mh4n::gpu {
class ScaledScreens;
}  // namespace mh4n::gpu

namespace mh4n::display_mem {

// What the GPU learns from the runtime once the `display_mem.attach` boot step has run.
struct RuntimeLink {
    Runtime* runtime = nullptr;
    // The game's frame counter at its last change (0 until the first; a program that does not map
    // the counter never sees one). Written by the frame callback, read at VBlank: same thread.
    std::atomic<u32> game_frame{0};
};

class DisplayMemGpu final : public gpu::Gpu, public present::Display {
public:
    // `inner` is the GPU every call is forwarded to; its register file seeds the shadow.
    DisplayMemGpu(gpu::Host& host, std::unique_ptr<gpu::Gpu> inner);

    void Execute(const gpu::GxCommand& command) override;
    void WriteReg(u32 addr, u32 value) override;
    u32 ReadReg(u32 addr) override;
    void SetBufferSwap(u32 screen, const gpu::FrameBufferInfo& info) override;
    void SetColorFill(u32 fill) override;
    void FlushRegion(u32 vaddr, u32 size) override;
    void InvalidateRegion(u32 vaddr, u32 size) override;
    void VBlank(u32 screen) override;

    void AddScreenSink(present::ScreenSink* sink) override;
    void RemoveScreenSink(present::ScreenSink* sink) override;
    void ReadScreen(const present::ScreenImage& screen,
                    std::function<void(present::RgbImage)> done) override;

    // Gives the GPU the runtime's clock, the top screen's rows and the game's frame counter.
    void Attach(std::shared_ptr<RuntimeLink> link);
    gpu::Gpu& Inner() { return *inner_; }

private:
    std::optional<present::ScreenImage> BuildScreen(u32 screen);
    // The screen from guest memory, decoded straight to RGBA; the last picture again (the same pixels) when the
    // framebuffer and its registers are as they were. nullopt: the slow path decides (a fill, an error).
    std::optional<present::ScreenImage> BuildMemoryScreen(u32 screen);
    void LogMemoryScreen(u32 screen);
    // The screen from the inner GPU's picture at its scale; nullopt when it has none.
    std::optional<present::ScreenImage> BuildScaledScreen(u32 screen);
    // The same as a shared picture (shared screens); nullopt when the renderer has none of that framebuffer.
    std::optional<present::ScreenImage> BuildSharedScreen(u32 screen);
    // The framebuffer the screen shows is about to be read from guest memory: the inner GPU writes it first if it still
    // owes it (asynchronous screens).
    void PrepareScreenMemory(u32 screen);

    gpu::Host& host_;
    std::unique_ptr<gpu::Gpu> inner_;
    gpu::ScaledScreens* scaled_ = nullptr;  // the inner GPU's, when it renders above 1x
    u32 scale_ = 1;
    u64 counter_vblanks_ = 0;
    gpu_capture::DisplayShadow shadow_;
    std::vector<present::ScreenSink*> sinks_;
    bool shared_wanted_ = false;  // every sink shows shared pictures
    bool shared_asked_ = false;   // what the renderer was last asked (on its own thread, at a VBlank)
    bool touch_hidden_ = false;   // a sink hides the touch screen (ScreenHidden(1)), as the renderer was last told
    std::shared_ptr<RuntimeLink> link_;
    u64 next_sequence_ = 0;
    u32 top_swaps_ = 0;  // the top screen's buffer swaps seen, in the GSP's order (the pacing trace)
    std::string last_error_[2];
    // MH4N_SCALE_LOG: a screen built from guest memory above 1x (LogMemoryScreen): seen, its last frame, the lines written
    bool memory_screen_seen_[2] = {};
    u32 memory_screen_frame_[2] = {};
    u32 memory_screen_logs_ = 0;
    // BuildMemoryScreen's last picture of each screen and what it was made from
    struct LastScreen {
        u64 key = 0;
        bool valid = false;
        present::ScreenImage image;
        std::shared_ptr<std::vector<u8>> storage;  // image.pixels, writable
    };
    LastScreen last_screen_[2];
    // BuildScaledScreen's last picture of each screen, shown again while the GPU has not finished the next one: its place
    // in the transfers' order, and the frame (next_sequence_) it was read at
    struct LastScaled {
        std::optional<present::ScreenImage> image;
        u64 sequence = 0;
        u64 made = 0;
    };
    LastScaled last_scaled_[2];
};

}  // namespace mh4n::display_mem
