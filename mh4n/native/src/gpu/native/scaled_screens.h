// Written for this package: nothing is copied from Azahar here.
//
// The screens at the internal resolution (`--scale N`): what the native renderer offers the display GPU
// (`native-display`, display_mem.cpp), found with dynamic_cast<ScaledScreens*>. Header-only, so the display GPU needs
// no link to the renderer.
//
// HOW A SCREEN IS MADE AT N x. Guest memory stays the 1x truth: every write-back of a target downscales it (nearest),
// every load from memory upscales it, and the GX engine's fills, transfers and copies run on the CPU at 1x as before.
// So that the targets keep their N x picture between draws:
//   - after a GX command only the colour targets it writes are loaded again, and only their rows of tiles it touches
//     (depth targets start again at 1.0 after any command, as at 1x); a flush of a range and another target drawn over
//     part of a target do the same (native_gpu.cpp, MarkStale), where 1x loads the whole target again;
//   - a texture that is exactly a colour target held here (address, size, format) samples the target's N x image (its
//     feedback copy when the draw draws into it too): MH4U composes its picture with such draws (`--target-textures`,
//     on by default at every scale; off, the picture is the 1x one enlarged). Any other texture (a part of a target,
//     another format, a cube) is decoded from guest memory, at 1x.
// What changes is what the phone sees:
//   - a display transfer whose input is a render target the renderer holds (the transfer's input address, width and
//     format match a colour target that is not stale, its rows lie in the target, tiled to linear, no scaling) draws,
//     on the GPU, the picture that framebuffer will show: upright, N times the size, from the target's N x image, through
//     the same mapping the 1x path takes (the transfer's flip and crop, the display's rotation and its one-pixel shift)
//     and the same colour depths (the target's format, the transfer's output format), then copies it to a host buffer;
//   - at VBlank the display GPU asks for the framebuffer the shadow registers show; when the renderer has its picture
//     (no fill, CPU write, other transfer or draw has touched that framebuffer since, and the display's format, row
//     length and rows match what the transfer wrote) that picture is published, else the 1x one, enlarged (nearest)
//     so a screen keeps one size.
//
// ASYNCHRONOUS SCREENS (`--async-screens`, on by default with native-display, at every scale, 1x included). On the CPU
// path a display transfer waits for the GPU: the source target is written back (the whole frame's GPU work) before the
// engine blits it. A transfer that reads a target the GPU has drawn since its last write-back, tiled to linear and
// unscaled (MH4U's two screens every frame), instead goes to the GPU as above, with no wait, plus a 1x copy of the
// source rows it reads; the engine skips only the blit (the registers and the PPF interrupt are as always), and the
// framebuffer's memory is owed. It is written (Materialize: the copy read back, the engine's own blit over it) as soon
// as something is about to read it: the CPU (InvalidateRegion; a flush keeps the bytes the CPU wrote), a GX command,
// a texture or a render target loaded from it, the display's memory path (PrepareMemory), the capture checks
// (WriteBackColorBuffers); a command that writes all of it drops it. The display publishes the GPU's picture when its
// copy is done; while it is not, the screen's other framebuffer (the frame before) if its picture is done, else the last
// picture again, for a few VBlanks at most (a GPU that stays behind is then waited for, so the screen never freezes).
//
// SHARED SCREENS (SetSharedScreens, asked by the display GPU when every sink of it shows present::SharedPicture: the
// Android app). The screen's picture is drawn into memory the app's own graphics read (gpu/backend/shared: an
// AHardwareBuffer), a different buffer each time while the last ones are still held, and nothing is read back: the
// display publishes the picture itself as soon as its drawing is submitted, with a sync file to wait on. A framebuffer
// whose picture was drawn so has no CPU copy (ReadScaledScreen gives None for it). Where the device cannot share
// images the renderer goes on as before. The top screen's picture is not even drawn: it is the render target's own
// image, which the reader turns upright (present::SharedPicture::Target, native_gpu.cpp's screen targets;
// MH4N_TARGET_SCREENS=0: drawn upright as the touch screen's is).
#pragma once

#include <memory>
#include <vector>

#include "mh4n/present.h"
#include "mh4n/types.h"

namespace mh4n::gpu {

class ScaledScreens {
public:
    virtual ~ScaledScreens() = default;

    // The internal scale.
    virtual u32 ScreenScale() const = 0;
    // The renderer makes screens (a scale above 1, or asynchronous screens); false: there is nothing to ask for.
    virtual bool HasScreens() const = 0;

    enum class ScreenRead { None, Pending, Ready };
    // The upright picture of the framebuffer at physical address `paddr` in display format `format` (the framebuffer
    // registers' numbering), `stride` bytes per row of the framebuffer (one displayed column) and `rows` rows (the
    // displayed width at 1x): (rows * N) x (stride / bytes per pixel * N) RGBA8 pixels, top row first, alpha 255.
    // Ready: `rgba` has it. Pending (only without `wait`): the GPU has not finished it yet. None: the renderer has no
    // picture of that framebuffer (read guest memory, after PrepareMemory). `rgba` changes only when Ready. `sequence`
    // (when not null) gets the picture's place in the order the transfers ran (it only grows).
    virtual ScreenRead ReadScaledScreen(u32 paddr, u32 format, u32 stride, u32 rows, std::vector<u8>& rgba, bool wait,
                                        u64* sequence = nullptr) = 0;

    // Guest memory [paddr, paddr + size) is about to be read: a framebuffer still owed there is written first.
    virtual void PrepareMemory(u32 paddr, u32 size) = 0;

    // Shared screens (above): asked once, on the renderer's own thread; the renderer makes them from its next screen on
    // when the device can.
    virtual void SetSharedScreens(bool on) { (void)on; }

    // The touch screen's picture is shown (false: nothing shows it, present::ScreenSink::ScreenHidden). Hidden, the
    // renderer leaves out the draws of the touch screen's turn into the target its transfer reads. Any thread.
    virtual void SetTouchScreenShown(bool shown) { (void)shown; }
    struct SharedScreen {
        std::shared_ptr<const present::SharedPicture> picture;
        present::ImageRegion region;  // the screen in the picture's buffer: (rows * N) x (stride / bytes per pixel * N)
        u64 sequence = 0;             // as ReadScaledScreen's
    };
    // As ReadScaledScreen, for a picture drawn as a shared screen: Ready with `out` filled (whether or not the GPU has
    // finished it: the reader waits on it), or None.
    virtual ScreenRead ReadSharedScreen(u32 paddr, u32 format, u32 stride, u32 rows, SharedScreen& out) {
        (void)paddr, (void)format, (void)stride, (void)rows, (void)out;
        return ScreenRead::None;
    }
};

}  // namespace mh4n::gpu
