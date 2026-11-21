// The game's screens shown straight from the renderer's buffers (mh4n present::SharedPicture: AHardwareBuffers the
// renderer's Vulkan draws into), with no copy through the CPU. Each buffer is imported once into SDL's OpenGL ES
// renderer (an EGLImage under an SDL texture, kept while the buffer is in use). A picture is drawn once the renderer's
// GPU has finished it: the main loop's wait ends on the picture's sync file, so nothing in the loop waits for the game's
// GPU (MH4U_PRESENT_WAIT=gpu draws it at once instead, GL waiting on the GPU through EGL_ANDROID_native_fence_sync). A
// picture is let go, so that the renderer can draw into its buffer again, only once the GL work that read it has
// finished (an EGL fence).
// Main thread only (SDL's renderer thread).
#pragma once

#include <SDL3/SDL.h>

#include <memory>

#include "mh4n/present.h"

// What the device's EGL and GL can do; false: shared pictures can't be shown (the app asks for CPU pixels).
bool shared_screens_init(SDL_Renderer* renderer);
bool shared_screens_available();
// The screens went to the app as pixels as a last resort (shared_screens_stop_sharing): true once, for the notice
bool shared_screens_valve_notice_take();

// One screen as the window shows it: a shared picture or CPU pixels, whichever came last.
struct SharedScreenView {
    SDL_Texture* texture = nullptr;  // what is drawn (null: nothing yet, or a render target's picture: `target`)
    SDL_FRect source{};              // the part of `texture` that holds the screen (a target's: its upright pixels)
    int width = 0, height = 0;       // the screen's size in pixels
    std::shared_ptr<const mh4n::present::SharedPicture> picture;  // the shared picture shown, if it is one
    // A render target's picture (SharedPicture::Target): its buffer as a GL texture of ours, turned upright where it is
    // drawn (shared_screens_draw), with no copy on the renderer's side.
    bool target = false;
    mh4n::present::TargetLayout target_layout{};
    unsigned gl_name = 0, gl_target = 0;
    int buffer_width = 0, buffer_height = 0;
    // a picture from the renderer's buffers has been shown in this view (its buffer imported and sampled by GL)
    bool shown_shared = false;
    // the top screen's view (the game's picture, or frame generation's made one): PS1 style goes on it alone
    bool top = false;
    // the pictures newer than the one shown that the renderer's GPU has not finished, oldest first, each with its sync
    // file (the main loop's wait ends when one is readable) and its region. The GPU finishes them in order: the newest
    // one finished is shown and the older ones dropped. A newer picture no longer replaces an unfinished one (it did, and
    // where the GPU took longer than a VBlank, as on a Mali at 3x, most pictures were replaced before they were finished:
    // the window showed 17 of the game's 48 a second). At most kPendingMax: past that the second oldest goes.
    static constexpr int kPendingMax = 3;
    std::shared_ptr<const mh4n::present::SharedPicture> pending[kPendingMax];
    mh4n::present::ImageRegion pending_region[kPendingMax]{};
    int pending_fd[kPendingMax] = {-1, -1, -1};
    int pending_count = 0;
    // the CPU form's own texture and the pixels last uploaded into it
    SDL_Texture* cpu_texture = nullptr;
    int cpu_width = 0, cpu_height = 0;
    std::shared_ptr<const std::vector<std::uint8_t>> uploaded;
};

// Shows `picture` (its `region` of the buffer) from now on, unless the renderer's GPU has not finished it (then false:
// the view keeps what it shows; try again when shared_screens_wait_fd is readable). The picture shown before is let go
// once the GL work that read it is done. A buffer GL refuses to import gives false too; when the view has shown none of
// the renderer's buffers yet and no render target's picture has been shown, the renderer is then asked for the screens
// as pixels from its next VBlank on (present::SetSharedPicturesRefused). MH4U_TEST_IMPORT_FAIL=1 (launch.txt's env=, a
// test) refuses every import.
bool shared_screens_show(SharedScreenView& view, const std::shared_ptr<const mh4n::present::SharedPicture>& picture,
                         const mh4n::present::ImageRegion& region);
// Draws what the view shows into `dst` of the window (`win_w` x `win_h` pixels), as SDL_RenderTexture would: a render
// target's picture turned upright, flipped and shifted as the display transfer and the display would, in a GL pass of
// its own in SDL's order (SDL's queue goes to GL first). Nothing when it has nothing. `alpha`: the texture's alpha mod.
void shared_screens_draw(SharedScreenView& view, const SDL_FRect& dst, int win_w, int win_h, Uint8 alpha = 255);
// The same for a part of the screen (`part`: x, y, w, h, fractions of the screen): the quick items' piece (quick_items.h).
void shared_screens_draw_part(SharedScreenView& view, const SDL_FRect& part, const SDL_FRect& dst, int win_w, int win_h,
                              Uint8 alpha = 255);
// The colour filter the screens are drawn with from now on ([Colour], colour_filter.h; the settings panel's draft while it
// is open, the saved values otherwise: android_main.cpp). All 0: SDL's own draw and the render target's program as before.
struct ColourAdjust;
void shared_screens_set_colour(const ColourAdjust& colour);
// PS1 style on the top screen (a view with `top`), the settings panel's draft while it is open: `filter` ([Graphics]
// Ps1Filter) its pixels sharp, cut to 5 bits a channel after an ordered dither by 3DS pixel, with faint scanlines and the
// PS1's shimmer, after the colour filter; `sharp` alone (Low-end mode's Brick) its pixels sharp. Both false: the screens are
// drawn as before.
void shared_screens_set_ps1(bool filter, bool sharp);
// FastTopScreen ([Graphics], off as the app comes): a top screen the window shows at another size than its picture's is
// scaled by the GPU's own filter from a picture cut once (shared_screens.cpp kPrecutLinearFragment): much lighter on a
// weak GPU (2x on a 720-line screen), its colours by 1 or 2 off in places. Off: as 1.0.1.1.
void shared_screens_set_fast_top(bool on);
// The screen upright at `w` x `h`, the whole of what is bound now (an SDL render target of that size: frame_gen.cpp keeps
// the top screen's pictures so), drawn as shared_screens_draw draws it into the window but with nothing added to its
// colours (the made picture is drawn with shared_screens_draw, which adds them once).
void shared_screens_draw_upright(SharedScreenView& view, int w, int h);
// The screen into `dst` of what is bound now, an SDL render target of `w` x `h` (the second display's picture:
// second_screen.h), as shared_screens_draw draws it into the window: the colour filter on it, no PS1 style (the touch
// screen never has it), fully opaque.
void shared_screens_draw_into(SharedScreenView& view, const SDL_FRect& dst, int w, int h);
// MH4N_TARGET_SCREENS_CHECK=1 (a measurement aid; the renderer draws the upright copy as well): now and then the screen
// drawn both ways into a texture of the window's size and compared pixel by pixel; logged, and the first pair saved as
// BMPs into `dir`.
void shared_screens_check(SharedScreenView& view, const SDL_FRect& dst, int win_w, int win_h, const char* dir);
// Shows CPU pixels (RGBA8, width x height) from now on (uploaded into the view's own texture).
void shared_screens_show_pixels(SharedScreenView& view, const std::shared_ptr<const std::vector<std::uint8_t>>& pixels,
                                int width, int height);
// MH4U_GL_TIMER=1 (a measurement aid): the GL time of what the window draws between these two calls, each frame (before
// the first draw, before SDL_RenderPresent), into the pacing trace (kAppTopGpu) and MH4U_PRESENT_STATS's lines.
void shared_screens_frame_begin();
void shared_screens_frame_end();
// Once a frame, after SDL_RenderPresent: pictures whose reads are done are let go; imports unused for a while dropped.
void shared_screens_frame_done();
// SDL_RenderPresent took `us` (MH4U_PRESENT_STATS=1 logs it with the rest every 5 s).
void shared_screens_note_present(std::uint64_t us);
// The descriptors that become readable when the renderer's GPU finishes a picture the window waits for (one a view has
// not shown yet, or the one the window's last frame waits on), for the main loop's wait: up to `max` into `fds`; how many.
int shared_screens_wait_fds(const SharedScreenView* views, int count, int* fds, int max);
// The GL context was lost (SDL_EVENT_RENDER_DEVICE_RESET): every import and texture is made again when needed.
void shared_screens_reset(SharedScreenView* views, int count);
// Before the game stops: every picture let go (after the GPU has finished), every import dropped.
void shared_screens_shutdown(SharedScreenView* views, int count);
