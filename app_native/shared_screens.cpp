// The screens straight from the renderer's buffers (shared_screens.h).
//
// SDL's OpenGL ES 2 renderer draws them: an SDL texture is made the usual way (its own storage) and then given the
// EGLImage of the buffer as its storage (glEGLImageTargetTexture2DOES on GL_TEXTURE_2D). The order matters: SDL calls
// glTexImage2D when it makes a GL_TEXTURE_2D texture, and that would cut an EGLImage it was given beforehand loose
// (SDL_PROP_TEXTURE_CREATE_OPENGLES2_TEXTURE_NUMBER alone does not do). GL_TEXTURE_EXTERNAL_OES works with SDL as is (it
// skips glTexImage2D for SDL_PIXELFORMAT_EXTERNAL_OES and has a shader for it) and is the fallback for a driver that
// refuses an RGBA buffer as a GL_TEXTURE_2D. The texture is never updated by SDL (no SDL_UpdateTexture).
//
// Raw GL here leaves SDL's state as it found it: SDL's queue is flushed first (SDL_FlushRenderer), and the texture
// binding put back.
#include "shared_screens.h"

#include "colour_filter.h"

#include "mh4n/pacing_trace.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <android/hardware_buffer.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <string>

namespace {

using mh4n::present::SharedPicture;

SDL_Renderer* g_renderer = nullptr;
bool g_available = false;
bool g_native_fence = false;  // EGL_ANDROID_native_fence_sync + EGL_KHR_wait_sync: the GPU waits for the renderer
// A picture the renderer's GPU has not finished is drawn once it is finished, the main loop woken by its sync file
// (false, the default), or at once, the window's GPU waiting for it (true: MH4U_PRESENT_WAIT=gpu). Measured: on a
// GPU-bound Mali (Anbernic, the village at 3x) the GPU wait held SDL_RenderPresent 21 ms a frame on average (the
// window's frames queued behind the game's), against 2 ms; on the Galaxy the pictures were finished when taken anyway.
bool g_gpu_wait = false;
bool g_logged_texture_mode = false;
bool g_logged_target_mode = false;
// What a log needs when the screens stay black (4u-native #33, #34): the first buffer of each kind described (a screen's,
// a render target's), refusals counted (the first 3 logged), whether the renderer was asked for pixels instead, and
// whether a render target's picture was ever shown (after that its buffers have been sampled: no switch to pixels).
bool g_described[2] = {};
unsigned g_refusals = 0;
bool g_refused = false;
bool g_target_shown = false;
bool g_test_import_fail = false;  // MH4U_TEST_IMPORT_FAIL=1: every import refused (a test of the switch to pixels)

PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC p_eglGetNativeClientBufferANDROID = nullptr;
PFNEGLCREATEIMAGEKHRPROC p_eglCreateImageKHR = nullptr;
PFNEGLDESTROYIMAGEKHRPROC p_eglDestroyImageKHR = nullptr;
PFNEGLCREATESYNCKHRPROC p_eglCreateSyncKHR = nullptr;
PFNEGLDESTROYSYNCKHRPROC p_eglDestroySyncKHR = nullptr;
PFNEGLCLIENTWAITSYNCKHRPROC p_eglClientWaitSyncKHR = nullptr;
PFNEGLWAITSYNCKHRPROC p_eglWaitSyncKHR = nullptr;
PFNEGLDUPNATIVEFENCEFDANDROIDPROC p_eglDupNativeFenceFDANDROID = nullptr;
PFNGLEGLIMAGETARGETTEXTURE2DOESPROC p_glEGLImageTargetTexture2DOES = nullptr;

// One buffer imported: its EGLImage and the SDL texture over it (a render target's buffer: a GL texture of ours only,
// which DrawTarget draws).
struct Import {
    EGLImageKHR image = EGL_NO_IMAGE_KHR;
    SDL_Texture* texture = nullptr;
    GLuint external_name = 0;  // a texture of ours (GL_TEXTURE_EXTERNAL_OES, or a target's): SDL does not delete it
    GLenum external_target = 0;
    int width = 0, height = 0;
    std::uint64_t last_used = 0;
    // (1.0.1) the picture last shown with it: while it lives (a view still shows it) the import stays, however long ago
    // it was shown (shared_screens_frame_done)
    std::weak_ptr<const SharedPicture> shown;
};
std::map<std::uint64_t, Import> g_imports;  // by SharedPicture::BufferId
std::uint64_t g_frame = 0;
bool g_external_oes = false;  // GL_TEXTURE_2D was refused: GL_TEXTURE_EXTERNAL_OES from then on

// ---- render targets' pictures (SharedPicture::Target) ----
// The renderer hands over the render target the top screen's display transfer read, as it is (no upright copy on its
// GPU), with the transfer's mapping (mh4n present::TargetLayout). The pass that draws it into the window turns it: each
// fragment takes the upright picture's coordinates SDL_RenderTexture would give it (its centre, the source and
// destination rectangles), clamped to the upright picture's texel centres (the copy's CLAMP_TO_EDGE), and samples the
// target where the transfer and the display would have put that point: a turn and a flip that take texel centres to
// texel centres, so one bilinear sample of the target is the bilinear sample of the upright picture. Only the top rows
// (v < N + 1/2: the display's one-pixel shift moves the first column's top pixel to the next framebuffer row, and puts
// black past the last one) take the four texels one by one.
struct TargetProgram {
    GLuint program = 0;
    GLint u_tex = -1, u_rect = -1, u_affine = -1, u_yfix = -1, u_size = -1, u_map = -1, u_flip = -1, u_alpha = -1;
    GLint u_inv = -1, u_cut = -1, u_in_shift = -1, u_in_inv = -1, u_in_low = -1, u_out_shift = -1, u_out_inv = -1, u_out_low = -1;
    GLint u_f0 = -1, u_f1 = -1, u_f2 = -1, u_fg = -1;  // (the colour filter's variant)
    GLint u_ps1f = -1;  // (PS1 style's filter variant)
};
TargetProgram g_target_programs[2];  // [0] GL_TEXTURE_2D, [1] GL_TEXTURE_EXTERNAL_OES
bool g_target_programs_tried = false;

// ---- the colour filter ([Colour], colour_filter.h: shared_screens_set_colour) ----
// Off (every control 0), the screens are drawn as they always were. On, the render target's program has a variant with the
// filter's few operations after its sample (kFilterCode: the same source otherwise), and a screen SDL would draw
// (SDL_RenderTexture) is drawn by a program of ours that samples it as SDL does, with the filter: no pass of its own.
ColourAdjust g_colour{};
bool g_colour_on = false;
ColourMatrix g_colour_matrix{};
TargetProgram g_filter_target_programs[2];  // (the render target's, with the filter: made the first time it is on)
bool g_filter_target_tried = false;
// A screen SDL would draw, with the filter: its quad, its part of the texture, its alpha mod
struct TextureProgram {
    GLuint program = 0;
    GLint u_tex = -1, u_rect = -1, u_uv = -1, u_alpha = -1;
    GLint u_f0 = -1, u_f1 = -1, u_f2 = -1, u_fg = -1;
};
TextureProgram g_texture_programs[2];  // [0] GL_TEXTURE_2D, [1] GL_TEXTURE_EXTERNAL_OES
bool g_texture_programs_tried = false;

// ---- PS1 style ([Graphics] Ps1Filter, and Low-end mode's Brick: shared_screens_set_ps1) ----
// Off (both), nothing here runs and the programs above are the ones of always, their source unchanged. Otherwise the top
// screen (a view with `top`: the game's picture and frame generation's made one; never the touch screen) is drawn by a
// variant of the program it takes (the render target's, or a screen's as SDL would draw it, with or without the colour
// filter), in the draw that already puts it into the window (no pass of its own; our overlay is drawn over it afterwards):
//   mode 1, sharp pixels (Brick): the picture's pixels drawn sharp (Ps1Sharp: each one a square, its edge blended over one
//     pixel of the window, so none is wider than another); nothing else, as cheap as the plain draw.
//   mode 2, the PS1 filter (the MHGU port's same look and amounts): the picture on the 3DS's own pixel grid (the picture's
//     height over 240 of its pixels a cell, 400 or so cells across: the PS1's 320 x 240 kind), each cell one colour (the
//     picture sampled at the cell's centre), then (after the colour filter) Ps1: the CRT era's colours (saturation x1.35
//     round Rec. 601 luma, contrast x1.15 round 0.5), each colour cut to 5 bits a channel with the 4x4 Bayer threshold
//     (m + 0.5) / 16 by cell, so the dots and the bands show plainly as on a PS1, and scanlines on the cells' rows (a
//     cosine, 18 % darker at most between two rows), a quarter less of it every other picture: the flicker.
int g_ps1_mode = 0;          // 0 off, 1 sharp pixels, 2 the PS1 filter
unsigned g_ps1_frames = 0;   // the top screen's draws with the filter (the shimmer's phase)
TargetProgram g_ps1_target_programs[2][2][2];  // [mode - 1][the colour filter][0 GL_TEXTURE_2D, 1 GL_TEXTURE_EXTERNAL_OES]
struct Ps1TextureProgram {
    GLuint program = 0;
    GLint u_tex = -1, u_rect = -1, u_uv = -1, u_alpha = -1, u_ps1 = -1, u_ps1b = -1, u_ps1f = -1;
    GLint u_f0 = -1, u_f1 = -1, u_f2 = -1, u_fg = -1;
};
Ps1TextureProgram g_ps1_texture_programs[2][2][2];  // the same
bool g_ps1_programs_tried = false;
constexpr const char* kPs1SharpCode = R"(// a coordinate in texels (texel k from k to k + 1) moved so that a bilinear sample gives the nearest texel's colour but in
// the last window pixel before the next texel; s: the window's pixels a texel
float Ps1Sharp(float x, float s) {
    float k = max(s, 1.0);
    float i = floor(x);
    float f = x - i - 0.5;
    float r = 0.5 - 0.5 / k;
    return i + 0.5 + (f - clamp(f, -r, r)) * k;
}
)";
constexpr const char* kPs1Code = R"(uniform float u_ps1f;  // the flicker's phase: 0 or 1, every other picture
// the CRT era's colours (the MHGU port's same amounts): saturation x1.35 round Rec. 601 luma, contrast x1.15 round 0.5
vec3 Ps1Look(vec3 c) {
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = clamp(mix(vec3(l), c, 1.35), 0.0, 1.0);
    return clamp((c - 0.5) * 1.15 + 0.5, 0.0, 1.0);
}
// those colours cut to 5 bits a channel with the 4x4 Bayer threshold (m + 0.5) / 16 by the cell (virtual pixel), and the
// scanlines: a cosine between the cells' rows, 18 % darker at most, a quarter less of it on every other picture (the flicker)
vec3 Ps1(vec3 c, vec2 cell) {
    c = Ps1Look(c);
    vec2 p = mod(floor(cell), 4.0);
    vec2 a = mod(p, 2.0);
    vec2 b = floor(p * 0.5);
    float m = 4.0 * (2.0 * a.x + 3.0 * a.y - 4.0 * a.x * a.y) + (2.0 * b.x + 3.0 * b.y - 4.0 * b.x * b.y);
    vec3 o = floor(c * 31.0 + (m + 0.5) / 16.0) / 31.0;
    float s = 0.5 + 0.5 * cos(6.2831853 * fract(cell.y));
    return o * (1.0 - 0.18 * (1.0 - 0.25 * u_ps1f) * s);
}
)";
// A screen as SDL would draw it, with PS1 style (%s: the sampler's extension line, its type, the code (kFilterCode or nothing,
// kPs1SharpCode, kPs1Code or nothing), the filter's call and Ps1's, or nothing)
constexpr const char* kPs1TextureFragment = R"(%s
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform %s u_tex;
uniform float u_alpha;
uniform vec4 u_ps1;   // the texture's width and height in texels, the picture's texels a 3DS pixel
uniform vec4 u_ps1b;  // the picture's corner in the texture (texels), the window's pixels a texel across and down
varying vec2 v_uv;
%s
void main() {
    vec2 texel = v_uv * u_ps1.xy;
    vec2 cell = (texel - u_ps1b.xy) / u_ps1.z;
    %svec4 t = texture2D(u_tex, at / u_ps1.xy);
    vec3 c = t.rgb;
    %sgl_FragColor = vec4(c, t.a * u_alpha);
}
)";
// The filter in a fragment shader (GLSL ES 1.00): three rows and the gamma (colour_filter.h ColourMatrix)
constexpr const char* kFilterCode = R"(uniform vec4 u_f0, u_f1, u_f2;
uniform float u_fg;
vec3 Filter(vec3 c) {
    vec4 p = vec4(pow(max(c, vec3(0.0)), vec3(u_fg)), 1.0);
    return clamp(vec3(dot(u_f0, p), dot(u_f1, p), dot(u_f2, p)), 0.0, 1.0);
}
)";
constexpr const char* kTextureVertex = R"(
attribute vec2 a_corner;
uniform vec4 u_rect;  // the destination's corners in clip space: x0, y0, x1, y1
uniform vec4 u_uv;    // the texture's coordinates there: u0, v0, u1, v1
varying vec2 v_uv;
void main() {
    gl_Position = vec4(mix(u_rect.x, u_rect.z, a_corner.x), mix(u_rect.y, u_rect.w, a_corner.y), 0.0, 1.0);
    v_uv = vec2(mix(u_uv.x, u_uv.z, a_corner.x), mix(u_uv.y, u_uv.w, a_corner.y));
}
)";
// (%s: the sampler's extension line, its type, kFilterCode)
constexpr const char* kTextureFragment = R"(%s
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform %s u_tex;
uniform float u_alpha;
varying vec2 v_uv;
%s
void main() {
    vec4 t = texture2D(u_tex, v_uv);
    gl_FragColor = vec4(Filter(t.rgb), t.a * u_alpha);
}
)";
GLuint g_corners = 0;  // a buffer of the quad's corners (0, 0), (1, 0), (0, 1), (1, 1)

constexpr const char* kTargetVertex = R"(
attribute vec2 a_corner;
uniform vec4 u_rect;  // the destination's corners in clip space: x0, y0, x1, y1
void main() {
    gl_Position = vec4(mix(u_rect.x, u_rect.z, a_corner.x), mix(u_rect.y, u_rect.w, a_corner.y), 0.0, 1.0);
}
)";

// (%s: the sampler's extension line, its type)
constexpr const char* kTargetFragment = R"(%s
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform %s u_tex;
uniform vec4 u_affine;  // upright u = x * u_affine.x + u_affine.y, v = y * u_affine.z + u_affine.w (y down)
uniform vec2 u_yfix;    // y down = gl_FragCoord.y * u_yfix.x + u_yfix.y
uniform vec4 u_size;    // the upright picture's width and height, the target's width and height
uniform vec2 u_inv;     // 1 / the target's width and height
uniform vec4 u_map;     // N, pixels a framebuffer row at N x, framebuffer rows at N x, the target row of input row 0
uniform float u_flip;   // the transfer flipped its input
uniform float u_alpha;
uniform vec2 u_cut;     // the colour is cut to fewer bits than 8: x the target's depths, y the framebuffer's
uniform vec3 u_in_shift, u_in_inv, u_in_low;     // 2^(8 - b), 2^(b - 8), 2^(8 - 2b) of each depth
uniform vec3 u_out_shift, u_out_inv, u_out_low;
%s// a texel's colour as the upright copy holds it: cut to the target's depths, then the framebuffer's (8 bits: kept)
vec3 Depths(vec3 c) {
    if (u_cut.y < 0.5 && u_cut.x < 0.5) return c;
    vec3 v = floor(c * 255.0 + 0.5);
    if (u_cut.x > 0.5) {
        vec3 q = floor(v * u_in_inv);
        v = q * u_in_shift + floor(q * u_in_low);
    }
    vec3 q = floor(v * u_out_inv);
    return (q * u_out_shift + floor(q * u_out_low)) * (1.0 / 255.0);
}
// the upright picture's texel `o` (whole numbers), anywhere (the top rows: the display's one-pixel shift)
vec3 Texel(vec2 o) {
    float n = u_map.x, w = u_map.y, h = u_map.z;
    float k = o.y < n ? 1.0 : 0.0;
    float fy = o.x + k;
    float fx = w + n - 1.0 - o.y - k * w;
    if (fy >= h) return vec3(0.0);
    float row = u_flip > 0.5 ? h - 1.0 - fy : fy;
    return Depths(texture2D(u_tex, vec2(fx + 0.5, u_map.w - row + 0.5) * u_inv).rgb);
}
void main() {
    float y = gl_FragCoord.y * u_yfix.x + u_yfix.y;
    float u = clamp(gl_FragCoord.x * u_affine.x + u_affine.y, 0.5, u_size.x - 0.5);
    float v = clamp(y * u_affine.z + u_affine.w, 0.5, u_size.y - 0.5);
    %sfloat n = u_map.x;
    vec3 c;
    if (v >= n + 0.5) {
        if (u_cut.x < 0.5 && u_cut.y < 0.5) {
            // one bilinear sample of the target: a turn and a flip, texel centres to texel centres
            float xt = u_map.y + n - v;
            float yt = u_flip > 0.5 ? u_map.w - u_map.z + 1.0 + u : u_map.w + 1.0 - u;
            c = texture2D(u_tex, vec2(xt, yt) * u_inv).rgb;
        } else {
            // the upright texels around (u, v) one by one, cut, then filtered as GL_LINEAR would (those of weight 0 left
            // out): upright row r is the target's column w + n - 1 - r, upright column c its row first - c (flipped:
            // first - h + 1 + c); the next ones are a column less and a row less (more)
            vec2 p = vec2(u, v) - 0.5;
            vec2 i = floor(p);
            vec2 f = p - i;
            vec2 t = vec2(u_map.y + n - 0.5 - i.y, u_flip > 0.5 ? u_map.w - u_map.z + 1.5 + i.x : u_map.w + 0.5 - i.x);
            vec2 dx = vec2(0.0, i.x + 1.0 < u_size.x ? (u_flip > 0.5 ? 1.0 : -1.0) : 0.0);
            vec2 dy = vec2(i.y + 1.0 < u_size.y ? -1.0 : 0.0, 0.0);
            c = Depths(texture2D(u_tex, t * u_inv).rgb);
            if (f.x > 0.0) c = mix(c, Depths(texture2D(u_tex, (t + dx) * u_inv).rgb), f.x);
            if (f.y > 0.0) {
                vec3 d = Depths(texture2D(u_tex, (t + dy) * u_inv).rgb);
                if (f.x > 0.0) d = mix(d, Depths(texture2D(u_tex, (t + dx + dy) * u_inv).rgb), f.x);
                c = mix(c, d, f.y);
            }
        }
    } else {
        vec2 p = vec2(u, v) - 0.5;
        vec2 i = floor(p);
        vec2 f = p - i;
        vec2 j = min(i + 1.0, u_size.xy - 1.0);
        c = Texel(i);
        if (f.x > 0.0) c = mix(c, Texel(vec2(j.x, i.y)), f.x);
        if (f.y > 0.0) {
            vec3 d = Texel(vec2(i.x, j.y));
            if (f.x > 0.0) d = mix(d, Texel(j), f.x);
            c = mix(c, d, f.y);
        }
    }
    %sgl_FragColor = vec4(c, u_alpha);
}
)";

// ---- FastTopScreen: the top screen's colours cut once a picture, then scaled by the GPU's filter ----
// With its depths cut (MH4U's framebuffer is RGB565, so always) kTargetFragment takes up to four texels a window pixel,
// each turned and cut on its own, and filters them itself: where the window does not show the upright picture one texel
// a pixel (2x on a 720-line screen: 480 rows in 720), that is four reads, four cuts and the filter's arithmetic for every
// pixel of the window, 5.6 ms of a Moto g06's GPU a frame (the window's whole GL time 6.8 ms, MH4U_GL_TIMER=1). With
// FastTopScreen on, a first pass (an offscreen picture of the upright size) holds each upright texel once, turned and cut
// by the same code (Texel), and the window draws that picture with the GPU's own bilinear filter, one sample a pixel, as
// SDL drew the upright copy before 1.0 (gpu3): the window's GL time 2.9 ms. The GPU rounds its filter's weights, so where a
// pixel falls between texels its colour can be off from kTargetFragment's (the Moto at 2x: 13 % of the pixels, by 2 at
// most; MH4U_TOP_FAST=check): a setting, off as the app comes. A cheaper exact way was tried: the same four reads and the same arithmetic from the cut picture
// cost more than kTargetFragment alone (8.1 ms) and still differed in 0.006 % of the pixels (the GPU's compiler).
// The pass is made before SDL's queue runs (the window's own pass is not split for it on a tiler) with every GL state it
// touches put back.
// (%s: the sampler's extension line, its type)
constexpr const char* kPrecutFragment = R"(%s
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform %s u_tex;
uniform vec2 u_inv;
uniform vec4 u_map;
uniform float u_flip;
uniform vec2 u_cut;
uniform vec3 u_in_shift, u_in_inv, u_in_low;
uniform vec3 u_out_shift, u_out_inv, u_out_low;
vec3 Depths(vec3 c) {
    if (u_cut.y < 0.5 && u_cut.x < 0.5) return c;
    vec3 v = floor(c * 255.0 + 0.5);
    if (u_cut.x > 0.5) {
        vec3 q = floor(v * u_in_inv);
        v = q * u_in_shift + floor(q * u_in_low);
    }
    vec3 q = floor(v * u_out_inv);
    return (q * u_out_shift + floor(q * u_out_low)) * (1.0 / 255.0);
}
vec3 Texel(vec2 o) {
    float n = u_map.x, w = u_map.y, h = u_map.z;
    float k = o.y < n ? 1.0 : 0.0;
    float fy = o.x + k;
    float fx = w + n - 1.0 - o.y - k * w;
    if (fy >= h) return vec3(0.0);
    float row = u_flip > 0.5 ? h - 1.0 - fy : fy;
    return Depths(texture2D(u_tex, vec2(fx + 0.5, u_map.w - row + 0.5) * u_inv).rgb);
}
void main() {
    // the whole value (0-255) a quarter up, so that a store that rounds and one that truncates both keep it
    vec3 k = floor(Texel(floor(gl_FragCoord.xy)) * 255.0 + 0.5);
    gl_FragColor = vec4((k + 0.25) * (1.0 / 255.0), 1.0);
}
)";
// The cut picture into the window: one sample of the GPU's bilinear filter at the texel coordinates kTargetFragment
// takes (as SDL draws the upright copy)
constexpr const char* kPrecutLinearFragment = R"(
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform sampler2D u_tex;  // the cut picture, filtered
uniform vec4 u_affine;
uniform vec2 u_yfix;
uniform vec4 u_size;
uniform vec2 u_inv;
uniform float u_alpha;
void main() {
    float y = gl_FragCoord.y * u_yfix.x + u_yfix.y;
    float u = clamp(gl_FragCoord.x * u_affine.x + u_affine.y, 0.5, u_size.x - 0.5);
    float v = clamp(y * u_affine.z + u_affine.w, 0.5, u_size.y - 0.5);
    gl_FragColor = vec4(texture2D(u_tex, vec2(u, v) * u_inv).rgb, u_alpha);
}
)";

GLuint CompileShader(GLenum kind, const char* text) {
    GLuint shader = glCreateShader(kind);
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[Error] App: a target picture's shader: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// The two programs a shader pair makes alike (the render target's, a screen's with the filter): 0 when it fails (logged)
GLuint LinkProgram(const char* vertex, const char* fragment, const char* what) {
    const GLuint vs = CompileShader(GL_VERTEX_SHADER, vertex), fs = CompileShader(GL_FRAGMENT_SHADER, fragment);
    GLuint program = 0;
    if (vs != 0 && fs != 0) {
        program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glBindAttribLocation(program, 0, "a_corner");
        glLinkProgram(program);
        GLint ok = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024] = {};
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            std::fprintf(stderr, "[Error] App: %s program: %s\n", what, log);
            glDeleteProgram(program);
            program = 0;
        }
    }
    if (vs != 0) glDeleteShader(vs);
    if (fs != 0) glDeleteShader(fs);
    return program;
}

// filter: the colour filter's variant (kFilterCode after the sample); ps1: PS1 style's mode (1 the sharp pixels before the
// sample, 2 those and Ps1 last); 0 and no filter give the source of always, unchanged
TargetProgram MakeTargetProgram(bool external, bool filter = false, int ps1 = 0) {
    TargetProgram p;
    char fragment[12288];
    std::string code = std::string(filter ? kFilterCode : "") + (ps1 ? kPs1SharpCode : "") + (ps1 == 2 ? kPs1Code : "");
    std::string last = std::string(filter ? "c = Filter(c);\n    " : "") + (ps1 == 2 ? "c = Ps1(c, ps1_cell);\n    " : "");
    std::snprintf(fragment, sizeof(fragment), kTargetFragment,
                  external ? "#extension GL_OES_EGL_image_external : require" : "",
                  external ? "samplerExternalOES" : "sampler2D", code.c_str(),
                  ps1 == 2 ? "vec2 ps1_cell = vec2(u, v) * (240.0 / u_size.y);\n"  // (the PS1 filter: the cell's centre)
                             "    u = (floor(ps1_cell.x) + 0.5) * (u_size.y / 240.0);\n"
                             "    v = (floor(ps1_cell.y) + 0.5) * (u_size.y / 240.0);\n    "
                  : ps1 == 1 ? "u = Ps1Sharp(u, 1.0 / abs(u_affine.x));\n"  // (Brick: sharp pixels)
                               "    v = Ps1Sharp(v, 1.0 / abs(u_affine.z));\n    "
                             : "",
                  last.c_str());
    const GLuint vs = CompileShader(GL_VERTEX_SHADER, kTargetVertex), fs = CompileShader(GL_FRAGMENT_SHADER, fragment);
    if (vs != 0 && fs != 0) {
        p.program = glCreateProgram();
        glAttachShader(p.program, vs);
        glAttachShader(p.program, fs);
        glBindAttribLocation(p.program, 0, "a_corner");
        glLinkProgram(p.program);
        GLint ok = 0;
        glGetProgramiv(p.program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024] = {};
            glGetProgramInfoLog(p.program, sizeof(log), nullptr, log);
            std::fprintf(stderr, "[Error] App: a target picture's program: %s\n", log);
            glDeleteProgram(p.program);
            p.program = 0;
        }
    }
    if (vs != 0) glDeleteShader(vs);
    if (fs != 0) glDeleteShader(fs);
    if (p.program != 0) {
        p.u_tex = glGetUniformLocation(p.program, "u_tex");
        p.u_rect = glGetUniformLocation(p.program, "u_rect");
        p.u_affine = glGetUniformLocation(p.program, "u_affine");
        p.u_yfix = glGetUniformLocation(p.program, "u_yfix");
        p.u_size = glGetUniformLocation(p.program, "u_size");
        p.u_map = glGetUniformLocation(p.program, "u_map");
        p.u_flip = glGetUniformLocation(p.program, "u_flip");
        p.u_alpha = glGetUniformLocation(p.program, "u_alpha");
        p.u_inv = glGetUniformLocation(p.program, "u_inv");
        p.u_cut = glGetUniformLocation(p.program, "u_cut");
        p.u_in_shift = glGetUniformLocation(p.program, "u_in_shift");
        p.u_in_inv = glGetUniformLocation(p.program, "u_in_inv");
        p.u_in_low = glGetUniformLocation(p.program, "u_in_low");
        p.u_out_shift = glGetUniformLocation(p.program, "u_out_shift");
        p.u_out_inv = glGetUniformLocation(p.program, "u_out_inv");
        p.u_out_low = glGetUniformLocation(p.program, "u_out_low");
        p.u_f0 = glGetUniformLocation(p.program, "u_f0");
        p.u_f1 = glGetUniformLocation(p.program, "u_f1");
        p.u_f2 = glGetUniformLocation(p.program, "u_f2");
        p.u_fg = glGetUniformLocation(p.program, "u_fg");
        p.u_ps1f = glGetUniformLocation(p.program, "u_ps1f");
    }
    return p;
}

TextureProgram MakeTextureProgram(bool external) {
    TextureProgram p;
    char fragment[2048];
    std::snprintf(fragment, sizeof(fragment), kTextureFragment, external ? "#extension GL_OES_EGL_image_external : require" : "",
                  external ? "samplerExternalOES" : "sampler2D", kFilterCode);
    p.program = LinkProgram(kTextureVertex, fragment, "a screen's (colour filter)");
    if (p.program != 0) {
        p.u_tex = glGetUniformLocation(p.program, "u_tex");
        p.u_rect = glGetUniformLocation(p.program, "u_rect");
        p.u_uv = glGetUniformLocation(p.program, "u_uv");
        p.u_alpha = glGetUniformLocation(p.program, "u_alpha");
        p.u_f0 = glGetUniformLocation(p.program, "u_f0");
        p.u_f1 = glGetUniformLocation(p.program, "u_f1");
        p.u_f2 = glGetUniformLocation(p.program, "u_f2");
        p.u_fg = glGetUniformLocation(p.program, "u_fg");
    }
    return p;
}

// The colour filter's uniforms (the rows and the gamma: colour_filter.h) into the program in use
void SetFilterUniforms(GLint f0, GLint f1, GLint f2, GLint fg) {
    glUniform4fv(f0, 1, g_colour_matrix.row[0]);
    glUniform4fv(f1, 1, g_colour_matrix.row[1]);
    glUniform4fv(f2, 1, g_colour_matrix.row[2]);
    glUniform1f(fg, g_colour_matrix.gamma);
}

bool HasExtension(const char* list, const char* name);

// The programs and the corners' buffer, made once (again after the context was lost); SDL's GL state is put back.
bool EnsureTargetPrograms() {
    if (g_target_programs[0].program != 0) return true;
    if (g_target_programs_tried) return false;
    g_target_programs_tried = true;
    SDL_FlushRenderer(g_renderer);
    GLint program = 0, buffer = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
    g_target_programs[0] = MakeTargetProgram(false);
    const char* gl = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (HasExtension(gl, "GL_OES_EGL_image_external")) g_target_programs[1] = MakeTargetProgram(true);
    if (g_corners == 0) {
        const float corners[] = {0, 0, 1, 0, 0, 1, 1, 1};
        glGenBuffers(1, &g_corners);
        glBindBuffer(GL_ARRAY_BUFFER, g_corners);
        glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
    }
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(buffer));
    glUseProgram(static_cast<GLuint>(program));
    std::fprintf(stderr, "[Info] App: target pictures' programs: GL_TEXTURE_2D %s, GL_TEXTURE_EXTERNAL_OES %s\n",
                 g_target_programs[0].program != 0 ? "yes" : "no", g_target_programs[1].program != 0 ? "yes" : "no");
    return g_target_programs[0].program != 0;
}

// The colour filter's programs, made the first time it is on (SDL's GL state put back), as EnsureTargetPrograms does
bool EnsureFilterPrograms() {
    if (g_texture_programs_tried) return g_texture_programs[0].program != 0;
    g_texture_programs_tried = true;
    if (!EnsureTargetPrograms()) return false;  // (the corners' buffer)
    SDL_FlushRenderer(g_renderer);
    GLint program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    const char* gl = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    const bool external = HasExtension(gl, "GL_OES_EGL_image_external");
    g_texture_programs[0] = MakeTextureProgram(false);
    if (external) g_texture_programs[1] = MakeTextureProgram(true);
    g_filter_target_programs[0] = MakeTargetProgram(false, true);
    if (external) g_filter_target_programs[1] = MakeTargetProgram(true, true);
    glUseProgram(static_cast<GLuint>(program));
    std::fprintf(stderr, "[Info] App: the colour filter's programs: screens %s/%s, render targets %s/%s\n",
                 g_texture_programs[0].program != 0 ? "yes" : "no", g_texture_programs[1].program != 0 ? "yes" : "no",
                 g_filter_target_programs[0].program != 0 ? "yes" : "no", g_filter_target_programs[1].program != 0 ? "yes" : "no");
    return g_texture_programs[0].program != 0;
}

Ps1TextureProgram MakePs1TextureProgram(bool external, bool filter, int mode) {
    Ps1TextureProgram p;
    char fragment[8192];
    std::string code = std::string(filter ? kFilterCode : "") + kPs1SharpCode + (mode == 2 ? kPs1Code : "");
    std::string last = std::string(filter ? "c = Filter(c);\n    " : "") +
                       (mode == 2 ? "c = Ps1(c, cell);\n    " : "");
    // (where it samples: the cell's centre for the filter, the sharp pixels' place for Brick)
    const char* at = mode == 2 ? "vec2 at = u_ps1b.xy + (floor(cell) + 0.5) * u_ps1.z;\n    "
                               : "vec2 at = vec2(Ps1Sharp(texel.x, u_ps1b.z), Ps1Sharp(texel.y, u_ps1b.w));\n    ";
    std::snprintf(fragment, sizeof(fragment), kPs1TextureFragment, external ? "#extension GL_OES_EGL_image_external : require" : "",
                  external ? "samplerExternalOES" : "sampler2D", code.c_str(), at, last.c_str());
    p.program = LinkProgram(kTextureVertex, fragment, "a screen's (PS1 style)");
    if (p.program != 0) {
        p.u_tex = glGetUniformLocation(p.program, "u_tex");
        p.u_rect = glGetUniformLocation(p.program, "u_rect");
        p.u_uv = glGetUniformLocation(p.program, "u_uv");
        p.u_alpha = glGetUniformLocation(p.program, "u_alpha");
        p.u_ps1 = glGetUniformLocation(p.program, "u_ps1");
        p.u_ps1b = glGetUniformLocation(p.program, "u_ps1b");
        p.u_ps1f = glGetUniformLocation(p.program, "u_ps1f");
        p.u_f0 = glGetUniformLocation(p.program, "u_f0");
        p.u_f1 = glGetUniformLocation(p.program, "u_f1");
        p.u_f2 = glGetUniformLocation(p.program, "u_f2");
        p.u_fg = glGetUniformLocation(p.program, "u_fg");
    }
    return p;
}

// PS1 style's programs (both modes, with and without the colour filter, both texture kinds), made the first time it is on,
// as EnsureFilterPrograms does
bool EnsurePs1Programs() {
    if (g_ps1_programs_tried) return g_ps1_target_programs[0][0][0].program != 0;
    g_ps1_programs_tried = true;
    if (!EnsureTargetPrograms()) return false;  // (the corners' buffer)
    SDL_FlushRenderer(g_renderer);
    GLint program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    const char* gl = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    const bool external = HasExtension(gl, "GL_OES_EGL_image_external");
    int made = 0, tried = 0;
    for (int mode = 1; mode <= 2; ++mode) {
        for (int filter = 0; filter < 2; ++filter) {
            for (int kind = 0; kind < (external ? 2 : 1); ++kind) {
                TargetProgram& t = g_ps1_target_programs[mode - 1][filter][kind];
                Ps1TextureProgram& x = g_ps1_texture_programs[mode - 1][filter][kind];
                t = MakeTargetProgram(kind == 1, filter != 0, mode);
                x = MakePs1TextureProgram(kind == 1, filter != 0, mode);
                made += (t.program != 0) + (x.program != 0);
                tried += 2;
            }
        }
    }
    glUseProgram(static_cast<GLuint>(program));
    std::fprintf(stderr, "[Info] App: PS1 style's programs: %d of %d made (GL_TEXTURE_EXTERNAL_OES ones: %s)\n", made, tried,
                 external ? "yes" : "no");
    return g_ps1_target_programs[0][0][0].program != 0;
}

// Pictures no longer shown, held until the GL work queued before `fence` (the work that read them) is done.
struct Retiring {
    std::shared_ptr<const SharedPicture> picture;
    EGLSyncKHR fence = EGL_NO_SYNC_KHR;
};
std::deque<Retiring> g_retiring;

// The last frame that waits on the GPU for the renderer: its picture's sync file (to tell whether that wait is over).
int g_waiting_fd = -1;

// What the screens did, logged every 5 s (MH4U_PRESENT_STATS=1)
struct Stats {
    unsigned shown_ready = 0, shown_waiting = 0, deferred = 0, presents = 0;
    std::uint64_t wait_us = 0, present_us = 0, present_max_us = 0;
} g_stats;
bool g_stats_on = false;
std::chrono::steady_clock::time_point g_stats_since;

std::uint64_t NowUs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool HasExtension(const char* list, const char* name) {
    if (list == nullptr) return false;
    const size_t n = std::strlen(name);
    for (const char* p = list; (p = std::strstr(p, name)) != nullptr; p += n) {
        if ((p == list || p[-1] == ' ') && (p[n] == ' ' || p[n] == '\0')) return true;
    }
    return false;
}

void Log(const char* text) {
    std::fprintf(stderr, "[Info] App: %s\n", text);
}

EGLDisplay Display() {
    return eglGetCurrentDisplay();
}

// SDL's queued commands go to GL first, and its context is current.
void SyncWithSdl() {
    SDL_FlushRenderer(g_renderer);
}

bool FdSignaled(int fd) {
    if (fd < 0) return true;
    pollfd p{fd, POLLIN, 0};
    return poll(&p, 1, 0) > 0;
}

void CloseWaiting() {
    if (g_waiting_fd >= 0) close(g_waiting_fd);
    g_waiting_fd = -1;
}

// Drops the pending pictures [0, end) (with their sync files), keeping the newer ones in order.
void DropPending(SharedScreenView& view, int end) {
    end = std::min(end, view.pending_count);
    for (int i = 0; i < end; ++i) {
        if (view.pending_fd[i] >= 0) close(view.pending_fd[i]);
    }
    for (int i = end; i < view.pending_count; ++i) {
        view.pending[i - end] = std::move(view.pending[i]);
        view.pending_region[i - end] = view.pending_region[i];
        view.pending_fd[i - end] = view.pending_fd[i];
    }
    for (int i = view.pending_count - end; i < view.pending_count; ++i) {
        view.pending[i].reset();
        view.pending_fd[i] = -1;
    }
    view.pending_count -= end;
}

void ClosePending(SharedScreenView& view) {
    DropPending(view, view.pending_count);
}

// `picture` is not shown yet because it is not finished: kept after the others (the main loop's wait wakes when one is).
void NotePending(SharedScreenView& view, const std::shared_ptr<const SharedPicture>& picture,
                 const mh4n::present::ImageRegion& region) {
    for (int i = 0; i < view.pending_count; ++i) {
        if (view.pending[i] == picture) return;
    }
    ++g_stats.deferred;  // (each picture once)
    if (view.pending_count == SharedScreenView::kPendingMax) {  // (the oldest is the next one done: the second goes)
        if (view.pending_fd[1] >= 0) close(view.pending_fd[1]);
        for (int i = 2; i < view.pending_count; ++i) {
            view.pending[i - 1] = std::move(view.pending[i]);
            view.pending_region[i - 1] = view.pending_region[i];
            view.pending_fd[i - 1] = view.pending_fd[i];
        }
        --view.pending_count;
        view.pending[view.pending_count].reset();
        view.pending_fd[view.pending_count] = -1;
    }
    const int i = view.pending_count++;
    view.pending[i] = picture;
    view.pending_region[i] = region;
    view.pending_fd[i] = picture->TakeSyncFd();
    mh4n::pacing::Mark(mh4n::pacing::kAppPending, view.pending_fd[i] >= 0 ? 1u : 0u);
}

// The newest pending picture the GPU has finished, taken out of the list with the older ones (null: none yet).
std::shared_ptr<const SharedPicture> TakeFinishedPending(SharedScreenView& view, mh4n::present::ImageRegion& region) {
    for (int i = view.pending_count - 1; i >= 0; --i) {
        if (view.pending[i]->Ready()) {
            std::shared_ptr<const SharedPicture> done = view.pending[i];
            region = view.pending_region[i];
            DropPending(view, i + 1);
            return done;
        }
    }
    return nullptr;
}

// A buffer GL refused (or a part of its import): the first 3 lines logged, then the count at 10, 100, 1000... (a driver
// that refuses every buffer would log one each picture)
void Refusal(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void Refusal(const char* fmt, ...) {
    ++g_refusals;
    if (g_refusals <= 3) {
        va_list ap;
        va_start(ap, fmt);
        std::vfprintf(stderr, fmt, ap);
        va_end(ap);
        if (g_refusals == 3) std::fprintf(stderr, "[Info] App: the next refusals of screen buffers are counted, not logged\n");
        return;
    }
    unsigned p = 10;
    while (p < g_refusals && p <= 100'000'000u) p *= 10;
    if (p == g_refusals) std::fprintf(stderr, "[Warning] App: %u refusals of screen buffers so far\n", g_refusals);
}

// The first buffer of each kind as the system describes it (its stride and usage: what GL is given to import).
void Describe(const SharedPicture& picture) {
    const int kind = picture.Target() != nullptr ? 1 : 0;
    if (g_described[kind]) return;
    g_described[kind] = true;
    AHardwareBuffer_Desc d{};
    AHardwareBuffer_describe(static_cast<const AHardwareBuffer*>(picture.HardwareBuffer()), &d);
    std::fprintf(stderr, "[Info] App: the first %s buffer: %ux%u, %u layer(s), format %u, stride %u, usage 0x%llx\n",
                 kind == 1 ? "render target's" : "screen", d.width, d.height, d.layers, d.format, d.stride,
                 static_cast<unsigned long long>(d.usage));
}

void DropImport(Import& im) {
    if (im.texture != nullptr) SDL_DestroyTexture(im.texture);
    if (im.external_name != 0) glDeleteTextures(1, &im.external_name);
    if (im.image != EGL_NO_IMAGE_KHR) p_eglDestroyImageKHR(Display(), im.image);
    im = Import{};
}

// `picture`'s buffer imported (the first time it is seen): the SDL texture over it, or a render target's GL texture of
// ours. Null when it can't be.
Import* ImportOf(const SharedPicture& picture) {
    const std::uint64_t id = picture.BufferId();
    auto it = g_imports.find(id);
    if (it != g_imports.end()) {
        it->second.last_used = g_frame;
        return &it->second;
    }
    Describe(picture);
    if (g_test_import_fail) {
        Refusal("[Error] App: MH4U_TEST_IMPORT_FAIL: a screen buffer's import refused (a test)\n");
        return nullptr;
    }
    SyncWithSdl();
    const EGLDisplay display = Display();
    EGLClientBuffer client =
        p_eglGetNativeClientBufferANDROID(static_cast<const AHardwareBuffer*>(picture.HardwareBuffer()));
    const EGLint attrs[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
    Import im;
    im.width = static_cast<int>(picture.BufferWidth());
    im.height = static_cast<int>(picture.BufferHeight());
    im.image = client != nullptr ? p_eglCreateImageKHR(display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, client, attrs)
                                 : EGL_NO_IMAGE_KHR;
    if (im.image == EGL_NO_IMAGE_KHR) {
        Refusal("[Error] App: eglCreateImageKHR of a screen buffer failed (0x%x%s)\n", eglGetError(),
                client == nullptr ? ", no client buffer" : "");
        return nullptr;
    }
    GLint previous = 0, active = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glActiveTexture(GL_TEXTURE0);
    while (glGetError() != GL_NO_ERROR) {
    }
    if (picture.Target() != nullptr) {
        // a render target's buffer: a GL texture of ours, GL_TEXTURE_2D or else GL_TEXTURE_EXTERNAL_OES, linear, clamped
        const GLenum kinds[2] = {GL_TEXTURE_2D, GL_TEXTURE_EXTERNAL_OES};
        const GLenum bindings[2] = {GL_TEXTURE_BINDING_2D, GL_TEXTURE_BINDING_EXTERNAL_OES};
        for (int k = g_external_oes ? 1 : 0; k < 2 && im.external_name == 0; ++k) {
            if (g_target_programs[k].program == 0) continue;
            GLuint name = 0;
            glGenTextures(1, &name);
            glGetIntegerv(bindings[k], &previous);
            glBindTexture(kinds[k], name);
            p_glEGLImageTargetTexture2DOES(kinds[k], im.image);
            const GLenum error = glGetError();
            if (error == GL_NO_ERROR) {
                glTexParameteri(kinds[k], GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(kinds[k], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(kinds[k], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(kinds[k], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
            glBindTexture(kinds[k], static_cast<GLuint>(previous));
            if (error == GL_NO_ERROR) {
                im.external_name = name;
                im.external_target = kinds[k];
            } else {
                glDeleteTextures(1, &name);
                Refusal("[Info] App: a render target's buffer as %s refused (0x%x)\n",
                        k == 0 ? "GL_TEXTURE_2D" : "GL_TEXTURE_EXTERNAL_OES", error);
            }
        }
        glActiveTexture(static_cast<GLenum>(active));
        if (im.external_name == 0) {
            DropImport(im);
            return nullptr;
        }
        if (!g_logged_target_mode) {
            g_logged_target_mode = true;
            std::fprintf(stderr, "[Info] App: the top screen's target pictures are shown (%s)\n",
                         im.external_target == GL_TEXTURE_2D ? "GL_TEXTURE_2D" : "GL_TEXTURE_EXTERNAL_OES");
        }
        im.last_used = g_frame;
        return &g_imports.emplace(id, im).first->second;
    }
    if (!g_external_oes) {
        im.texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, im.width, im.height);
        if (im.texture != nullptr) {
            SDL_FlushRenderer(g_renderer);
            const GLuint name = static_cast<GLuint>(
                SDL_GetNumberProperty(SDL_GetTextureProperties(im.texture), SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER, 0));
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
            glBindTexture(GL_TEXTURE_2D, name);
            p_glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, im.image);
            const GLenum error = glGetError();
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
            if (name == 0 || error != GL_NO_ERROR) {
                std::fprintf(stderr, "[Info] App: a screen buffer as GL_TEXTURE_2D refused (0x%x): GL_TEXTURE_EXTERNAL_OES\n",
                             error);
                SDL_DestroyTexture(im.texture);
                im.texture = nullptr;
                g_external_oes = true;
            }
        }
    }
    if (im.texture == nullptr && g_external_oes) {
        glGenTextures(1, &im.external_name);
        im.external_target = GL_TEXTURE_EXTERNAL_OES;
        glGetIntegerv(GL_TEXTURE_BINDING_EXTERNAL_OES, &previous);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, im.external_name);
        p_glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, im.image);
        const GLenum error = glGetError();
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, static_cast<GLuint>(previous));
        if (error == GL_NO_ERROR) {
            SDL_PropertiesID props = SDL_CreateProperties();
            SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_EXTERNAL_OES);
            SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STATIC);
            SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, im.width);
            SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, im.height);
            SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_OPENGLES2_TEXTURE_NUMBER, im.external_name);
            im.texture = SDL_CreateTextureWithProperties(g_renderer, props);
            SDL_DestroyProperties(props);
        } else {
            Refusal("[Error] App: a screen buffer as GL_TEXTURE_EXTERNAL_OES refused too (0x%x)\n", error);
        }
    }
    glActiveTexture(static_cast<GLenum>(active));
    if (im.texture == nullptr) {
        DropImport(im);
        return nullptr;
    }
    SDL_SetTextureScaleMode(im.texture, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(im.texture, SDL_BLENDMODE_BLEND);
    if (!g_logged_texture_mode) {
        g_logged_texture_mode = true;
        std::fprintf(stderr, "[Info] App: the screens are shown from the renderer's buffers (%s, GPU wait: %s)\n",
                     g_external_oes ? "GL_TEXTURE_EXTERNAL_OES" : "GL_TEXTURE_2D",
                     g_native_fence ? "sync file" : "the CPU sees it finished");
    }
    im.last_used = g_frame;
    return &g_imports.emplace(id, im).first->second;
}

SDL_Texture* TextureOf(const SharedPicture& picture) {
    Import* im = ImportOf(picture);
    return im != nullptr ? im->texture : nullptr;
}

// `picture` is no longer shown: let go of it once the GL work queued so far (every frame that read it) is done.
void Retire(std::shared_ptr<const SharedPicture> picture) {
    if (picture == nullptr) return;
    SyncWithSdl();
    EGLSyncKHR fence = p_eglCreateSyncKHR(Display(), EGL_SYNC_FENCE_KHR, nullptr);
    if (fence == EGL_NO_SYNC_KHR) {
        glFinish();  // (no fence: wait for the GL work here, once)
        return;
    }
    // (the flush makes sure the fence signals even if nothing else is queued after it)
    p_eglClientWaitSyncKHR(Display(), fence, EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, 0);
    g_retiring.push_back(Retiring{std::move(picture), fence});
}

// A picture `view` could not show because GL refused its buffer. While the view has shown none of the renderer's buffers
// and no render target's picture has been shown, the renderer is asked for the screens as pixels from now on (the
// display reads it at its next VBlank, before more of its buffers go to GL); after that what is shown stays.
void RefuseShared(const SharedScreenView& view) {
    if (g_refused || view.shown_shared || g_target_shown) return;
    g_refused = true;
    mh4n::present::SetSharedPicturesRefused(true);
    std::fprintf(stderr, "[Warning] App: the renderer's buffers can't be shown on this phone (an import was refused): the "
                         "screens come as pixels from now on\n");
}

void FreeCpuTexture(SharedScreenView& view) {
    if (view.cpu_texture != nullptr) SDL_DestroyTexture(view.cpu_texture);
    view.cpu_texture = nullptr;
    view.cpu_width = view.cpu_height = 0;
    view.uploaded.reset();
}

}  // namespace

namespace {
void TimerInit();
}  // namespace

bool shared_screens_init(SDL_Renderer* renderer) {
    g_renderer = renderer;
    g_available = false;
    // (a run starts: nothing refused yet)
    g_refused = g_target_shown = false;
    g_refusals = 0;
    g_described[0] = g_described[1] = false;
    mh4n::present::SetSharedPicturesRefused(false);
    const char* test = std::getenv("MH4U_TEST_IMPORT_FAIL");
    g_test_import_fail = test != nullptr && *test == '1';
    const char* name = SDL_GetRendererName(renderer);
    if (name == nullptr || std::strcmp(name, "opengles2") != 0) {
        std::fprintf(stderr, "[Info] App: the renderer is %s: the screens come as pixels\n", name ? name : "?");
        return false;
    }
    SDL_FlushRenderer(renderer);
    const EGLDisplay display = eglGetCurrentDisplay();
    {
        const auto gl = [](GLenum what) {
            const char* s = reinterpret_cast<const char*>(glGetString(what));
            return s != nullptr ? s : "?";
        };
        const auto egl = [display](EGLint what) {
            const char* s = display != EGL_NO_DISPLAY ? eglQueryString(display, what) : nullptr;
            return s != nullptr ? s : "?";
        };
        std::fprintf(stderr, "[Info] App: SDL renderer %s: GL \"%s\" on \"%s\", GLSL \"%s\"; EGL \"%s\", vendor \"%s\"\n",
                     name, gl(GL_VERSION), gl(GL_RENDERER), gl(GL_SHADING_LANGUAGE_VERSION), egl(EGL_VERSION),
                     egl(EGL_VENDOR));
    }
    if (g_test_import_fail) {
        std::fprintf(stderr, "[Warning] App: MH4U_TEST_IMPORT_FAIL=1: every screen buffer's import is refused (a test)\n");
    }
    if (display == EGL_NO_DISPLAY) {
        Log("no current EGL display: the screens come as pixels");
        return false;
    }
    const char* egl = eglQueryString(display, EGL_EXTENSIONS);
    const char* gl = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    const bool image = HasExtension(egl, "EGL_KHR_image_base") && HasExtension(egl, "EGL_ANDROID_image_native_buffer") &&
                       HasExtension(egl, "EGL_ANDROID_get_native_client_buffer") && HasExtension(gl, "GL_OES_EGL_image");
    const bool fence = HasExtension(egl, "EGL_KHR_fence_sync");
    g_native_fence = HasExtension(egl, "EGL_ANDROID_native_fence_sync") && HasExtension(egl, "EGL_KHR_wait_sync");
    p_eglGetNativeClientBufferANDROID =
        reinterpret_cast<PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC>(eglGetProcAddress("eglGetNativeClientBufferANDROID"));
    p_eglCreateImageKHR = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
    p_eglDestroyImageKHR = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
    p_eglCreateSyncKHR = reinterpret_cast<PFNEGLCREATESYNCKHRPROC>(eglGetProcAddress("eglCreateSyncKHR"));
    p_eglDestroySyncKHR = reinterpret_cast<PFNEGLDESTROYSYNCKHRPROC>(eglGetProcAddress("eglDestroySyncKHR"));
    p_eglClientWaitSyncKHR = reinterpret_cast<PFNEGLCLIENTWAITSYNCKHRPROC>(eglGetProcAddress("eglClientWaitSyncKHR"));
    p_eglWaitSyncKHR = reinterpret_cast<PFNEGLWAITSYNCKHRPROC>(eglGetProcAddress("eglWaitSyncKHR"));
    p_eglDupNativeFenceFDANDROID =
        reinterpret_cast<PFNEGLDUPNATIVEFENCEFDANDROIDPROC>(eglGetProcAddress("eglDupNativeFenceFDANDROID"));
    p_glEGLImageTargetTexture2DOES =
        reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    g_available = image && fence && p_eglGetNativeClientBufferANDROID && p_eglCreateImageKHR && p_eglDestroyImageKHR &&
                  p_eglCreateSyncKHR && p_eglDestroySyncKHR && p_eglClientWaitSyncKHR && p_glEGLImageTargetTexture2DOES;
    g_native_fence = g_native_fence && p_eglWaitSyncKHR != nullptr;
    const char* wait = std::getenv("MH4U_PRESENT_WAIT");
    g_gpu_wait = g_native_fence && wait != nullptr && std::strcmp(wait, "gpu") == 0;
    const char* stats = std::getenv("MH4U_PRESENT_STATS");
    g_stats_on = stats != nullptr && *stats == '1';
    g_stats_since = std::chrono::steady_clock::now();
    // A render target's picture is drawn with programs of ours: none, and the renderer is told to draw the upright copy
    // (it reads MH4N_TARGET_SCREENS once, when the game starts, after this)
    if (g_available) TimerInit();
    if (g_available && !EnsureTargetPrograms()) {
        setenv("MH4N_TARGET_SCREENS", "0", 1);
        Log("no program for the render targets' pictures: the renderer draws them upright (MH4N_TARGET_SCREENS=0)");
    }
    std::fprintf(stderr, "[Info] App: shared screens %s (EGL images %s, fences %s, sync files %s; an unfinished picture %s)\n",
                 g_available ? "possible" : "impossible: the screens come as pixels", image ? "yes" : "no",
                 fence ? "yes" : "no", g_native_fence ? "yes" : "no",
                 g_gpu_wait ? "drawn at once, the GPU waiting" : "drawn once finished");
    return g_available;
}

bool shared_screens_available() {
    return g_available;
}

// (perf_hint.c, any thread: the process's map entries kept growing) the screens as pixels from the renderer's next VBlank on,
// and the player told why the frame rate drops (settings_panel.cpp valve_notice, on the app's thread)
std::atomic<bool> g_valve_notice{false};
extern "C" void shared_screens_stop_sharing(void) {
    if (!g_available || mh4n::present::SharedPicturesRefused()) return;
    mh4n::present::SetSharedPicturesRefused(true);
    g_valve_notice.store(true);
}

bool shared_screens_valve_notice_take() {
    return g_valve_notice.exchange(false);
}

namespace {
bool Show(SharedScreenView& view, const std::shared_ptr<const SharedPicture>& picture,
          const mh4n::present::ImageRegion& region, bool newest);
}

bool shared_screens_show(SharedScreenView& view, const std::shared_ptr<const SharedPicture>& picture,
                         const mh4n::present::ImageRegion& region) {
    return Show(view, picture, region, true);
}

namespace {

// `newest`: the newest picture of the screen (every pending one is older); else a pending one that was finished (the
// pending ones are newer and stay).
bool Show(SharedScreenView& view, const std::shared_ptr<const SharedPicture>& picture,
          const mh4n::present::ImageRegion& region, bool newest) {
    if (!g_available || picture == nullptr) return false;
    if (picture == view.picture) {
        return true;
    }
    // A picture the renderer's GPU has not finished: the window's GPU waits for it (a sync file), unless the window's
    // last frame still waits for the renderer (then the window would queue frame after frame behind the game's GPU:
    // the picture shown stays, and this one is tried again in a moment).
    int fd = -1;
    if (!picture->Ready()) {
        bool later = !g_gpu_wait || !FdSignaled(g_waiting_fd);  // (shown once finished)
        if (!later) {
            fd = picture->TakeSyncFd();
            later = fd < 0 && !picture->Ready();  // (no sync file for it: shown once finished)
        }
        if (later) {
            NotePending(view, picture, region);
            // an older picture the GPU has finished meanwhile is shown now (it is newer than the one shown)
            mh4n::present::ImageRegion done_region{};
            std::shared_ptr<const SharedPicture> done = TakeFinishedPending(view, done_region);
            return done != nullptr && Show(view, done, done_region, false);
        }
    }
    if (newest) ClosePending(view);  // (this one or an older one: either way it waits no more)
    const bool target = picture->Target() != nullptr;
    Import* import = target && !EnsureTargetPrograms() ? nullptr : ImportOf(*picture);
    if (import == nullptr || (!target && import->texture == nullptr)) {
        if (fd >= 0) close(fd);
        RefuseShared(view);
        return false;
    }
    CloseWaiting();
    if (fd >= 0) {
        SyncWithSdl();
        g_waiting_fd = dup(fd);
        const EGLint attrs[] = {EGL_SYNC_NATIVE_FENCE_FD_ANDROID, fd, EGL_NONE};
        EGLSyncKHR sync = p_eglCreateSyncKHR(Display(), EGL_SYNC_NATIVE_FENCE_ANDROID, attrs);  // (it owns `fd` now)
        if (sync != EGL_NO_SYNC_KHR) {
            const std::uint64_t start = NowUs();
            p_eglWaitSyncKHR(Display(), sync, 0);  // the GL commands after this wait on the GPU for the renderer
            g_stats.wait_us += NowUs() - start;
            p_eglDestroySyncKHR(Display(), sync);
        } else {
            close(fd);
            picture->WaitReady(100'000'000);  // (the sync file was refused: once, on the CPU)
        }
    }
    ++(fd >= 0 ? g_stats.shown_waiting : g_stats.shown_ready);
    Retire(std::move(view.picture));
    import->shown = picture;
    view.picture = picture;
    view.shown_shared = true;
    g_target_shown = g_target_shown || target;
    view.texture = import->texture;  // (a render target's: none, shared_screens_draw draws it)
    view.target = target;
    if (target) {
        view.target_layout = *picture->Target();
        view.gl_name = import->external_name;
        view.gl_target = import->external_target;
        view.buffer_width = import->width;
        view.buffer_height = import->height;
    }
    view.source = SDL_FRect{static_cast<float>(region.x), static_cast<float>(region.y), static_cast<float>(region.width),
                            static_cast<float>(region.height)};
    view.width = static_cast<int>(region.width);
    view.height = static_cast<int>(region.height);
    FreeCpuTexture(view);
    return true;
}

}  // namespace

void shared_screens_show_pixels(SharedScreenView& view, const std::shared_ptr<const std::vector<std::uint8_t>>& pixels,
                                int width, int height) {
    if (pixels == nullptr || pixels == view.uploaded) return;
    if (view.cpu_texture == nullptr || view.cpu_width != width || view.cpu_height != height) {
        if (view.cpu_texture != nullptr) SDL_DestroyTexture(view.cpu_texture);
        view.cpu_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, width, height);
        if (view.cpu_texture == nullptr) return;
        SDL_SetTextureScaleMode(view.cpu_texture, SDL_SCALEMODE_LINEAR);
        view.cpu_width = width;
        view.cpu_height = height;
    }
    SDL_UpdateTexture(view.cpu_texture, nullptr, pixels->data(), width * 4);
    ClosePending(view);
    view.uploaded = pixels;
    view.texture = view.cpu_texture;
    view.target = false;
    view.source = SDL_FRect{0, 0, static_cast<float>(width), static_cast<float>(height)};
    view.width = width;
    view.height = height;
    if (view.picture != nullptr && g_available) Retire(std::move(view.picture));
    view.picture.reset();
}

namespace {

// The layout's and the colour depths' uniforms of a target program (kTargetFragment's, or kPrecutFragment's: a name the
// program lacks is -1, which GL ignores).
void SetLayoutUniforms(const TargetProgram& p, const SharedScreenView& view, bool no_cut) {
    const mh4n::present::TargetLayout& l = view.target_layout;
    glUniform4f(p.u_size, static_cast<float>(l.UprightWidth()), static_cast<float>(l.UprightHeight()),
                static_cast<float>(view.buffer_width), static_cast<float>(view.buffer_height));
    glUniform4f(p.u_map, static_cast<float>(l.scale), static_cast<float>(l.row_pixels), static_cast<float>(l.rows),
                static_cast<float>(l.first));
    glUniform1f(p.u_flip, l.flip ? 1.0f : 0.0f);
    glUniform2f(p.u_inv, 1.0f / static_cast<float>(view.buffer_width), 1.0f / static_cast<float>(view.buffer_height));
    // the colour depths: 2^(8 - b), 2^(b - 8) and 2^(8 - 2b) for each (all exact; 8 bits: nothing cut)
    bool cut_in = false, cut_out = false;
    float in_shift[3], in_inv[3], in_low[3], out_shift[3], out_inv[3], out_low[3];
    for (int c = 0; c < 3; ++c) {
        const int bi = std::clamp<int>(l.bits_in[c], 4, 8), bo = std::clamp<int>(l.bits_out[c], 4, 8);
        cut_in |= bi < 8;
        cut_out |= bo < 8;
        in_shift[c] = std::ldexp(1.0f, 8 - bi);
        in_inv[c] = std::ldexp(1.0f, bi - 8);
        in_low[c] = std::ldexp(1.0f, 8 - 2 * bi);
        out_shift[c] = std::ldexp(1.0f, 8 - bo);
        out_inv[c] = std::ldexp(1.0f, bo - 8);
        out_low[c] = std::ldexp(1.0f, 8 - 2 * bo);
    }
    if (no_cut) cut_in = cut_out = false;  // (a measurement aid: one filtered sample, depths not cut)
    glUniform2f(p.u_cut, cut_in ? 1.0f : 0.0f, cut_in || cut_out ? 1.0f : 0.0f);
    glUniform3fv(p.u_in_shift, 1, in_shift);
    glUniform3fv(p.u_in_inv, 1, in_inv);
    glUniform3fv(p.u_in_low, 1, in_low);
    glUniform3fv(p.u_out_shift, 1, out_shift);
    glUniform3fv(p.u_out_inv, 1, out_inv);
    glUniform3fv(p.u_out_low, 1, out_low);
}

// ---- the cut picture's way ----
// The setting FastTopScreen (shared_screens_set_fast_top), unless MH4U_TOP_FAST (a measurement aid) says: 0 off (as
// 1.0.1.1), 1 on; check: on, and every 30th picture drawn both ways and compared (PrecutCheck, the log); ab: on and off by
// turns, 300 window frames each, the turn marked in the pacing trace (kind 53, ab_phase: 1 on, 2 off).
enum : int { kTopOld = 0, kTopFast = 2 };
struct PrecutModes {
    bool from_env = false;
    int way = kTopOld;
    bool check = false, ab = false;
};
bool g_fast_top_setting = false;
const PrecutModes& PrecutEnv() {
    static const PrecutModes modes = [] {
        PrecutModes m;
        const char* v = std::getenv("MH4U_TOP_FAST");
        if (v == nullptr || *v == '\0') return m;
        const std::string s = v;
        m.from_env = true;
        m.way = s == "0" ? kTopOld : kTopFast;
        m.check = s == "check";
        m.ab = s == "ab";
        return m;
    }();
    return modes;
}
int PrecutWay() { return PrecutEnv().from_env ? PrecutEnv().way : g_fast_top_setting ? kTopFast : kTopOld; }
bool PrecutAbOff() {
    if (!PrecutEnv().ab) return false;
    static std::uint64_t marked = ~0ull;
    const std::uint64_t phase = (g_frame / 300) % 2;
    if (phase != marked) {
        marked = phase;
        mh4n::pacing::Mark(mh4n::pacing::kRenderAbPhase, static_cast<std::uint32_t>(phase + 1));
    }
    return phase == 1;
}
TargetProgram g_precut_pass[2];  // [0] GL_TEXTURE_2D, [1] GL_TEXTURE_EXTERNAL_OES (its u_tex: the target)
TargetProgram g_precut_linear;   // (its u_tex: the cut picture; u_inv: 1 / its size)
bool g_precut_tried = false;
struct PrecutPicture {
    GLuint fbo = 0, texture = 0;
    int width = 0, height = 0;
};
PrecutPicture g_precut[2];  // [0] the top screen's, [1] the touch screen's (a frame draws both without waiting)
bool g_precut_failed = false;

TargetProgram LinkTargetLike(const char* fragment, const char* what) {
    TargetProgram p;
    p.program = LinkProgram(kTargetVertex, fragment, what);
    if (p.program == 0) return p;
    const auto at = [&](const char* a, const char* b = nullptr) {
        const GLint l = glGetUniformLocation(p.program, a);
        return l >= 0 || b == nullptr ? l : glGetUniformLocation(p.program, b);
    };
    p.u_tex = at("u_tex", "u_pre");
    p.u_rect = at("u_rect");
    p.u_affine = at("u_affine");
    p.u_yfix = at("u_yfix");
    p.u_size = at("u_size");
    p.u_map = at("u_map");
    p.u_flip = at("u_flip");
    p.u_alpha = at("u_alpha");
    p.u_inv = at("u_inv", "u_pinv");
    p.u_cut = at("u_cut");
    p.u_in_shift = at("u_in_shift");
    p.u_in_inv = at("u_in_inv");
    p.u_in_low = at("u_in_low");
    p.u_out_shift = at("u_out_shift");
    p.u_out_inv = at("u_out_inv");
    p.u_out_low = at("u_out_low");
    return p;
}

// The programs, made once (no GL binding is changed: SDL's queue may not have run yet)
bool EnsurePrecutPrograms(int kind) {
    if (!g_precut_tried) {
        g_precut_tried = true;
        char fragment[4096];
        std::snprintf(fragment, sizeof(fragment), kPrecutFragment, "", "sampler2D");
        g_precut_pass[0] = LinkTargetLike(fragment, "the top screen's cut");
        const char* gl = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        if (HasExtension(gl, "GL_OES_EGL_image_external")) {
            std::snprintf(fragment, sizeof(fragment), kPrecutFragment, "#extension GL_OES_EGL_image_external : require",
                          "samplerExternalOES");
            g_precut_pass[1] = LinkTargetLike(fragment, "the top screen's cut (external)");
        }
        g_precut_linear = LinkTargetLike(kPrecutLinearFragment, "the top screen's cut picture");
        std::fprintf(stderr, "[Info] App: the top screen cut once a picture (FastTopScreen%s): programs %s/%s, %s\n",
                     PrecutEnv().from_env ? ", MH4U_TOP_FAST" : "", g_precut_pass[0].program != 0 ? "yes" : "no",
                     g_precut_pass[1].program != 0 ? "yes" : "no", g_precut_linear.program != 0 ? "yes" : "no");
    }
    return g_precut_pass[kind].program != 0 && g_precut_linear.program != 0 && g_corners != 0;
}

// Every GL state the cut's pass changes, as it was, put back afterwards (SDL's cache of them stays true)
struct SavedGl {
    GLint fbo = 0, viewport[4] = {}, program = 0, active = 0, tex_2d = 0, tex_ext = 0, array_buffer = 0;
    GLboolean scissor = GL_FALSE, blend = GL_FALSE;
    GLfloat clear[4] = {};
    GLint enabled[3] = {}, a0_buffer = 0, a0_size = 4, a0_type = GL_FLOAT, a0_norm = 0, a0_stride = 0;
    void* a0_pointer = nullptr;
    bool external = false;
    void Save(bool ext) {
        external = ext;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex_2d);
        if (external) glGetIntegerv(GL_TEXTURE_BINDING_EXTERNAL_OES, &tex_ext);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
        scissor = glIsEnabled(GL_SCISSOR_TEST);
        blend = glIsEnabled(GL_BLEND);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
        for (GLuint i = 0; i < 3; ++i) glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled[i]);
        glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &a0_buffer);
        glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_SIZE, &a0_size);
        glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_TYPE, &a0_type);
        glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &a0_norm);
        glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &a0_stride);
        glGetVertexAttribPointerv(0, GL_VERTEX_ATTRIB_ARRAY_POINTER, &a0_pointer);
    }
    void Restore() const {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glUseProgram(static_cast<GLuint>(program));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(tex_2d));
        if (external) glBindTexture(GL_TEXTURE_EXTERNAL_OES, static_cast<GLuint>(tex_ext));
        glActiveTexture(static_cast<GLenum>(active));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(a0_buffer));
        glVertexAttribPointer(0, a0_size, static_cast<GLenum>(a0_type), a0_norm ? GL_TRUE : GL_FALSE, a0_stride, a0_pointer);
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(array_buffer));
        for (GLuint i = 0; i < 3; ++i) {
            if (enabled[i]) glEnableVertexAttribArray(i);
            else glDisableVertexAttribArray(i);
        }
        if (scissor) glEnable(GL_SCISSOR_TEST);
        else glDisable(GL_SCISSOR_TEST);
        if (blend) glEnable(GL_BLEND);
        else glDisable(GL_BLEND);
        glClearColor(clear[0], clear[1], clear[2], clear[3]);
    }
};

// The cut picture of `view` (its upright size, each texel turned and cut once: kPrecutFragment) into g_precut[slot].
// False: not made (the caller draws the target as before).
bool PrecutPass(const SharedScreenView& view, int slot) {
    const int kind = view.gl_target == GL_TEXTURE_EXTERNAL_OES ? 1 : 0;
    if (g_precut_failed || !EnsurePrecutPrograms(kind)) return false;
    const mh4n::present::TargetLayout& l = view.target_layout;
    const int w = static_cast<int>(l.UprightWidth()), h = static_cast<int>(l.UprightHeight());
    if (w <= 0 || h <= 0 || view.buffer_width <= 0 || view.buffer_height <= 0) return false;
    SavedGl saved;
    saved.Save(kind == 1);
    PrecutPicture& pic = g_precut[slot];
    bool ok = true;
    if (pic.texture == 0 || pic.width != w || pic.height != h) {
        if (pic.texture == 0) glGenTextures(1, &pic.texture);
        if (pic.fbo == 0) glGenFramebuffers(1, &pic.fbo);
        glBindTexture(GL_TEXTURE_2D, pic.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, pic.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pic.texture, 0);
        ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        pic.width = w;
        pic.height = h;
        std::fprintf(stderr, "[Info] App: the top screen's cut picture %dx%d: %s\n", w, h, ok ? "made" : "refused, not used again");
        if (!ok) g_precut_failed = true;
    }
    if (ok) {
        const TargetProgram& p = g_precut_pass[kind];
        glBindFramebuffer(GL_FRAMEBUFFER, pic.fbo);
        glViewport(0, 0, w, h);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);  // (all of it is drawn: nothing to load on a tiler)
        glUseProgram(p.program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(view.gl_target, view.gl_name);
        glUniform1i(p.u_tex, 0);
        glUniform4f(p.u_rect, -1.0f, -1.0f, 1.0f, 1.0f);
        SetLayoutUniforms(p, view, false);
        glBindBuffer(GL_ARRAY_BUFFER, g_corners);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glDisableVertexAttribArray(2);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindTexture(view.gl_target, 0);
    }
    saved.Restore();
    return ok;
}

// Whether `view` drawn into `dst` takes the cut picture: FastTopScreen on, its colours cut, not one upright texel a pixel
// (kTargetFragment reads one texel a pixel then already, the same colours), and none of the variants (the colour filter,
// PS1 style).
// `any_size`: whatever the sizes (the check's).
bool PrecutWanted(const SharedScreenView& view, const SDL_FRect& dst, bool no_cut, bool colour, int ps1, bool any_size = false) {
    if (PrecutWay() == kTopOld || no_cut || colour || ps1 != 0 || g_precut_failed || (!any_size && PrecutAbOff())) return false;
    const mh4n::present::TargetLayout& l = view.target_layout;
    bool cut = false;
    for (int c = 0; c < 3; ++c) cut |= l.bits_in[c] < 8 || l.bits_out[c] < 8;
    return cut && (any_size || !(view.source.w == dst.w && view.source.h == dst.h));
}

// A render target's picture into `dst` of what is bound now (the window, `out_w` x `out_h`, or with `into_texture` an SDL
// render target of that size, whose rows SDL keeps the other way up), in SDL's order: its queue goes to GL first, and
// SDL sets its own state again afterwards (SDL_FlushRenderer forgets what it had set); the vertex arrays SDL keeps
// enabled are left as they were.
// colour: with the colour filter (its variant of the program; the program of always where that one couldn't be made)
// precut: -1 as PrecutWanted says, 0 never, 1 whatever the sizes (MH4U_TOP_FAST=check's check).
void DrawTarget(const SharedScreenView& view, const SDL_FRect& dst, int out_w, int out_h, bool into_texture, Uint8 alpha,
                bool no_cut = false, bool colour = false, int ps1 = 0, float phase = 0.0f, int precut = -1) {
    if (!EnsureTargetPrograms() || view.gl_name == 0 || dst.w <= 0 || dst.h <= 0 || out_w <= 0 || out_h <= 0) return;
    const int kind = view.gl_target == GL_TEXTURE_EXTERNAL_OES ? 1 : 0;
    colour = colour && EnsureFilterPrograms() && g_filter_target_programs[kind].program != 0;
    if (ps1 != 0 && !(EnsurePs1Programs() && g_ps1_target_programs[ps1 - 1][colour ? 1 : 0][kind].program != 0)) ps1 = 0;
    // the cut picture (FastTopScreen), made before SDL's queue runs: the window's own pass is not split for it
    const int slot = view.top ? 0 : 1;
    const bool cut_picture = precut != 0 && PrecutWanted(view, dst, no_cut, colour, ps1, precut == 1) && PrecutPass(view, slot);
    const TargetProgram& p = cut_picture ? g_precut_linear
                             : ps1 != 0  ? g_ps1_target_programs[ps1 - 1][colour ? 1 : 0][kind]
                             : colour    ? g_filter_target_programs[kind]
                                         : g_target_programs[kind];
    if (p.program == 0) return;
    SDL_FlushRenderer(g_renderer);
    glUseProgram(p.program);
    glViewport(0, 0, out_w, out_h);
    glDisable(GL_SCISSOR_TEST);
    if (alpha < 255) {
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquation(GL_FUNC_ADD);
    } else {
        glDisable(GL_BLEND);
    }
    glActiveTexture(GL_TEXTURE0);
    const GLenum texture_target = cut_picture ? GL_TEXTURE_2D : view.gl_target;
    glBindTexture(texture_target, cut_picture ? g_precut[slot].texture : view.gl_name);
    glUniform1i(p.u_tex, 0);
    // the destination in clip space, as SDL's projection puts it (the window: y down; an SDL render target: y up)
    const float x0 = 2.0f * dst.x / out_w - 1.0f, x1 = 2.0f * (dst.x + dst.w) / out_w - 1.0f;
    const float y0 = into_texture ? 2.0f * dst.y / out_h - 1.0f : 1.0f - 2.0f * dst.y / out_h;
    const float y1 = into_texture ? 2.0f * (dst.y + dst.h) / out_h - 1.0f : 1.0f - 2.0f * (dst.y + dst.h) / out_h;
    glUniform4f(p.u_rect, x0, y0, x1, y1);
    // the upright picture's texel coordinates of a fragment's centre, as SDL interpolates them over the quad
    const float ax = view.source.w / dst.w, ay = view.source.h / dst.h;
    glUniform4f(p.u_affine, ax, view.source.x - dst.x * ax, ay, view.source.y - dst.y * ay);
    glUniform2f(p.u_yfix, into_texture ? 1.0f : -1.0f, into_texture ? 0.0f : static_cast<float>(out_h));
    glUniform1f(p.u_alpha, alpha / 255.0f);
    SetLayoutUniforms(p, view, no_cut);
    if (cut_picture) {  // (the cut picture's texels: its own size, the upright one)
        glUniform2f(p.u_inv, 1.0f / static_cast<float>(g_precut[slot].width), 1.0f / static_cast<float>(g_precut[slot].height));
    }
    if (colour) SetFilterUniforms(p.u_f0, p.u_f1, p.u_f2, p.u_fg);
    if (ps1 == 2) glUniform1f(p.u_ps1f, phase);
    // (SDL keeps its colour and texture coordinate arrays enabled: off for this draw, the colour one on again after)
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glBindBuffer(GL_ARRAY_BUFFER, g_corners);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(1);
    glBindTexture(texture_target, 0);
}

// A screen SDL would draw (view.texture: a buffer imported as SDL's texture, or the CPU form's), with the colour filter: its
// GL texture sampled as SDL_RenderTexture samples it (the same quad, the same part of the texture, linear, clamped to the
// edge as SDL sets it, its blend mode and alpha mod), in SDL's order as DrawTarget does. False: not drawn (no program, or
// SDL doesn't say the texture's name): the caller draws it with SDL, without the filter.
bool DrawTexturePs1(const SharedScreenView& view, const SDL_FRect& dst, int out_w, int out_h, Uint8 alpha, int mode, float phase);
// into_texture: `dst` of an SDL render target of out_w x out_h (rows the other way up, as DrawTarget's), not the window
bool DrawTextureFiltered(const SharedScreenView& view, const SDL_FRect& dst, int out_w, int out_h, Uint8 alpha, int ps1 = 0,
                         float phase = 0.0f, bool into_texture = false) {
    if (ps1 != 0) return DrawTexturePs1(view, dst, out_w, out_h, alpha, ps1, phase);
    if (!EnsureFilterPrograms() || dst.w <= 0 || dst.h <= 0 || out_w <= 0 || out_h <= 0) return false;
    const SDL_PropertiesID props = SDL_GetTextureProperties(view.texture);
    const GLuint name = static_cast<GLuint>(SDL_GetNumberProperty(props, SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER, 0));
    const GLenum target =
        static_cast<GLenum>(SDL_GetNumberProperty(props, SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_TARGET_NUMBER, GL_TEXTURE_2D));
    float tw = 0, th = 0;
    if (name == 0 || !SDL_GetTextureSize(view.texture, &tw, &th) || tw <= 0 || th <= 0) return false;
    const TextureProgram& p = g_texture_programs[target == GL_TEXTURE_EXTERNAL_OES ? 1 : 0];
    if (p.program == 0) return false;
    SDL_BlendMode blend = SDL_BLENDMODE_NONE;
    SDL_GetTextureBlendMode(view.texture, &blend);
    SDL_FlushRenderer(g_renderer);
    glUseProgram(p.program);
    glViewport(0, 0, out_w, out_h);
    glDisable(GL_SCISSOR_TEST);
    if (blend == SDL_BLENDMODE_BLEND) {  // (SDL's BLEND; NONE: as written)
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquation(GL_FUNC_ADD);
    } else {
        glDisable(GL_BLEND);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(target, name);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);  // (SDL_SCALEMODE_LINEAR, as every screen texture is set)
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glUniform1i(p.u_tex, 0);
    const float x0 = 2.0f * dst.x / out_w - 1.0f, x1 = 2.0f * (dst.x + dst.w) / out_w - 1.0f;  // (the window: y down)
    const float y0 = into_texture ? 2.0f * dst.y / out_h - 1.0f : 1.0f - 2.0f * dst.y / out_h;
    const float y1 = into_texture ? 2.0f * (dst.y + dst.h) / out_h - 1.0f : 1.0f - 2.0f * (dst.y + dst.h) / out_h;
    glUniform4f(p.u_rect, x0, y0, x1, y1);
    glUniform4f(p.u_uv, view.source.x / tw, view.source.y / th, (view.source.x + view.source.w) / tw,
                (view.source.y + view.source.h) / th);
    glUniform1f(p.u_alpha, alpha / 255.0f);
    SetFilterUniforms(p.u_f0, p.u_f1, p.u_f2, p.u_fg);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glBindBuffer(GL_ARRAY_BUFFER, g_corners);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(1);
    glBindTexture(target, 0);
    return true;
}

// A screen SDL would draw (DrawTextureFiltered's), with PS1 style's mode (and the colour filter when it is on): its texture
// sampled at the sharp coordinates, then the filter, then (mode 2) Ps1. False: not drawn (the caller draws it with SDL).
bool DrawTexturePs1(const SharedScreenView& view, const SDL_FRect& dst, int out_w, int out_h, Uint8 alpha, int mode, float phase) {
    if (dst.w <= 0 || dst.h <= 0 || out_w <= 0 || out_h <= 0 || view.source.w <= 0 || view.source.h <= 0) return false;
    const bool colour = g_colour_on && EnsureFilterPrograms();
    if (!EnsurePs1Programs()) return false;
    const SDL_PropertiesID props = SDL_GetTextureProperties(view.texture);
    const GLuint name = static_cast<GLuint>(SDL_GetNumberProperty(props, SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER, 0));
    const GLenum target =
        static_cast<GLenum>(SDL_GetNumberProperty(props, SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_TARGET_NUMBER, GL_TEXTURE_2D));
    float tw = 0, th = 0;
    if (name == 0 || !SDL_GetTextureSize(view.texture, &tw, &th) || tw <= 0 || th <= 0) return false;
    const Ps1TextureProgram& p = g_ps1_texture_programs[mode - 1][colour ? 1 : 0][target == GL_TEXTURE_EXTERNAL_OES ? 1 : 0];
    if (p.program == 0) return false;
    SDL_BlendMode blend = SDL_BLENDMODE_NONE;
    SDL_GetTextureBlendMode(view.texture, &blend);
    SDL_FlushRenderer(g_renderer);
    glUseProgram(p.program);
    glViewport(0, 0, out_w, out_h);
    glDisable(GL_SCISSOR_TEST);
    if (blend == SDL_BLENDMODE_BLEND) {  // (SDL's BLEND; NONE: as written)
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquation(GL_FUNC_ADD);
    } else {
        glDisable(GL_BLEND);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(target, name);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);  // (SDL_SCALEMODE_LINEAR, as every screen texture is set)
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glUniform1i(p.u_tex, 0);
    const float x0 = 2.0f * dst.x / out_w - 1.0f, x1 = 2.0f * (dst.x + dst.w) / out_w - 1.0f;  // (the window: y down)
    const float y0 = 1.0f - 2.0f * dst.y / out_h, y1 = 1.0f - 2.0f * (dst.y + dst.h) / out_h;
    glUniform4f(p.u_rect, x0, y0, x1, y1);
    glUniform4f(p.u_uv, view.source.x / tw, view.source.y / th, (view.source.x + view.source.w) / tw,
                (view.source.y + view.source.h) / th);
    glUniform1f(p.u_alpha, alpha / 255.0f);
    glUniform4f(p.u_ps1, tw, th, view.source.h / 240.0f, 0.0f);
    glUniform4f(p.u_ps1b, view.source.x, view.source.y, dst.w / view.source.w, dst.h / view.source.h);
    if (mode == 2) glUniform1f(p.u_ps1f, phase);
    if (colour) SetFilterUniforms(p.u_f0, p.u_f1, p.u_f2, p.u_fg);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glBindBuffer(GL_ARRAY_BUFFER, g_corners);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(1);
    glBindTexture(target, 0);
    return true;
}

// MH4N_TARGET_SCREENS_CHECK's comparisons
struct Check {
    SDL_Texture* texture = nullptr;  // a render target of the window's size
    int width = 0, height = 0;
    unsigned seen = 0, runs = 0, saved = 0;
    std::uint64_t pixels = 0, differ = 0, differ_more = 0;
    int max_diff = 0;
} g_check;

}  // namespace

void shared_screens_draw(SharedScreenView& view, const SDL_FRect& dst, int win_w, int win_h, Uint8 alpha) {
    const int ps1 = view.top ? g_ps1_mode : 0;  // (PS1 style: the top screen only)
    const float phase = ps1 == 2 && (g_ps1_frames++ & 1) != 0 ? 1.0f : 0.0f;
    if (view.target) {
        DrawTarget(view, dst, win_w, win_h, /*into_texture=*/false, alpha, /*no_cut=*/false, /*colour=*/g_colour_on, ps1, phase);
    } else if (view.texture != nullptr) {
        if ((g_colour_on || ps1 != 0) && DrawTextureFiltered(view, dst, win_w, win_h, alpha, ps1, phase)) return;
        SDL_SetTextureAlphaMod(view.texture, alpha);
        SDL_RenderTexture(g_renderer, view.texture, &view.source, &dst);
    }
}

void shared_screens_draw_part(SharedScreenView& view, const SDL_FRect& part, const SDL_FRect& dst, int win_w, int win_h,
                              Uint8 alpha) {
    // (the view's source cut down to the part for one draw: both ways of drawing map the destination onto the source)
    const SDL_FRect whole = view.source;
    view.source = SDL_FRect{whole.x + part.x * whole.w, whole.y + part.y * whole.h, part.w * whole.w, part.h * whole.h};
    shared_screens_draw(view, dst, win_w, win_h, alpha);
    view.source = whole;
}

void shared_screens_set_fast_top(bool on) {
    if (on != g_fast_top_setting)
        std::fprintf(stderr, "[Info] App: the top screen's scaling %s\n", on ? "fast (FastTopScreen)" : "as before");
    g_fast_top_setting = on;
}

void shared_screens_set_ps1(bool filter, bool sharp) {
    const int mode = filter ? 2 : sharp ? 1 : 0;
    if (mode != g_ps1_mode)
        std::fprintf(stderr, "[Info] App: the top screen %s\n",
                     mode == 2 ? "with the PS1 filter" : mode == 1 ? "with sharp pixels (Brick)" : "as before");
    g_ps1_mode = mode;
}

void shared_screens_set_colour(const ColourAdjust& colour) {
    if (!std::memcmp(&colour, &g_colour, sizeof colour)) return;
    g_colour = colour;
    g_colour_on = !colour_neutral(&colour);
    if (g_colour_on) colour_matrix(&colour, &g_colour_matrix);
}

void shared_screens_draw_upright(SharedScreenView& view, int w, int h) {
    const SDL_FRect all{0, 0, static_cast<float>(w), static_cast<float>(h)};
    if (view.target) {
        DrawTarget(view, all, w, h, /*into_texture=*/true, 255);
    } else if (view.texture != nullptr) {
        SDL_SetTextureAlphaMod(view.texture, 255);
        SDL_RenderTexture(g_renderer, view.texture, &view.source, &all);
    }
}

void shared_screens_draw_into(SharedScreenView& view, const SDL_FRect& dst, int w, int h) {
    if (view.target) {
        DrawTarget(view, dst, w, h, /*into_texture=*/true, 255, /*no_cut=*/false, /*colour=*/g_colour_on);
    } else if (view.texture != nullptr) {
        if (g_colour_on && DrawTextureFiltered(view, dst, w, h, 255, 0, 0.0f, /*into_texture=*/true)) return;
        SDL_SetTextureAlphaMod(view.texture, 255);
        SDL_RenderTexture(g_renderer, view.texture, &view.source, &dst);
    }
}
namespace {
// MH4U_TOP_FAST=check: every 30th picture the top screen drawn both ways into a picture of the window's size (as 1.0.1.1,
// each texel turned and cut in the window's draw, and from the cut picture), read back and compared pixel by pixel; the
// first that differ and the totals logged (each check, and every 10th line the totals alone)
struct PrecutCheckState {
    SDL_Texture* texture = nullptr;
    int width = 0, height = 0;
    unsigned seen = 0, runs = 0;
    std::uint64_t pixels = 0, differ = 0;
    int max_diff = 0;
} g_precut_check;

void PrecutCheck(SharedScreenView& view, const SDL_FRect& dst, int win_w, int win_h) {
    PrecutCheckState& k = g_precut_check;
    if (k.seen++ % 30 != 0 || view.picture == nullptr || !view.picture->WaitReady(500'000'000)) return;
    if (k.texture == nullptr || k.width != win_w || k.height != win_h) {
        if (k.texture != nullptr) SDL_DestroyTexture(k.texture);
        k.texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, win_w, win_h);
        k.width = win_w;
        k.height = win_h;
        if (k.texture == nullptr) return;
    }
    SDL_Texture* before = SDL_GetRenderTarget(g_renderer);
    SDL_SetRenderTarget(g_renderer, k.texture);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    DrawTarget(view, dst, win_w, win_h, /*into_texture=*/true, 255, false, false, 0, 0.0f, /*precut=*/0);
    SDL_Surface* a = SDL_RenderReadPixels(g_renderer, nullptr);
    SDL_RenderClear(g_renderer);
    DrawTarget(view, dst, win_w, win_h, /*into_texture=*/true, 255, false, false, 0, 0.0f, /*precut=*/1);
    SDL_Surface* b = SDL_RenderReadPixels(g_renderer, nullptr);
    SDL_SetRenderTarget(g_renderer, before);
    SDL_Surface* ra = a != nullptr ? SDL_ConvertSurface(a, SDL_PIXELFORMAT_RGBA32) : nullptr;
    SDL_Surface* rb = b != nullptr ? SDL_ConvertSurface(b, SDL_PIXELFORMAT_RGBA32) : nullptr;
    if (ra != nullptr && rb != nullptr && ra->w == rb->w && ra->h == rb->h) {
        ++k.runs;
        std::uint64_t pixels = 0, differ = 0;
        int max_diff = 0, first_x = -1, first_y = -1;
        for (int y = 0; y < ra->h; ++y) {
            const auto* pa = static_cast<const std::uint8_t*>(ra->pixels) + static_cast<size_t>(y) * ra->pitch;
            const auto* pb = static_cast<const std::uint8_t*>(rb->pixels) + static_cast<size_t>(y) * rb->pitch;
            for (int x = 0; x < ra->w; ++x) {
                int d = 0;
                for (int c = 0; c < 3; ++c) d = std::max(d, std::abs(pa[4 * x + c] - pb[4 * x + c]));
                ++pixels;
                if (d > 0 && differ++ == 0) first_x = x, first_y = y;
                max_diff = std::max(max_diff, d);
            }
        }
        k.pixels += pixels;
        k.differ += differ;
        k.max_diff = std::max(k.max_diff, max_diff);
        std::fprintf(stderr,
                     "[Info] App: precut check %u: the top screen %.0fx%.0f at %.0f,%.0f (upright %ux%u, scale %u, flip %d): "
                     "%llu of %llu px differ (max %d, first at %d,%d); all checks: %llu of %llu, max %d\n",
                     k.runs, dst.w, dst.h, dst.x, dst.y, view.target_layout.UprightWidth(), view.target_layout.UprightHeight(),
                     view.target_layout.scale, view.target_layout.flip ? 1 : 0, static_cast<unsigned long long>(differ),
                     static_cast<unsigned long long>(pixels), max_diff, first_x, first_y,
                     static_cast<unsigned long long>(k.differ), static_cast<unsigned long long>(k.pixels), k.max_diff);
    }
    if (ra != nullptr) SDL_DestroySurface(ra);
    if (rb != nullptr) SDL_DestroySurface(rb);
    if (a != nullptr) SDL_DestroySurface(a);
    if (b != nullptr) SDL_DestroySurface(b);
}
}  // namespace

void shared_screens_check(SharedScreenView& view, const SDL_FRect& dst, int win_w, int win_h, const char* dir) {
    if (PrecutEnv().check && view.target && win_w > 0 && win_h > 0) PrecutCheck(view, dst, win_w, win_h);
    if (!view.target || view.picture == nullptr || win_w <= 0 || win_h <= 0) return;
    const std::shared_ptr<const SharedPicture> reference = view.picture->Reference();
    if (reference == nullptr || g_check.seen++ % 30 != 0) return;  // (one picture in 30)
    SDL_Texture* upright = TextureOf(*reference);
    if (upright == nullptr || !reference->WaitReady(500'000'000) || !view.picture->WaitReady(500'000'000)) return;
    if (g_check.texture == nullptr || g_check.width != win_w || g_check.height != win_h) {
        if (g_check.texture != nullptr) SDL_DestroyTexture(g_check.texture);
        g_check.texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, win_w, win_h);
        g_check.width = win_w;
        g_check.height = win_h;
        if (g_check.texture == nullptr) return;
    }
    SDL_Texture* before = SDL_GetRenderTarget(g_renderer);
    SDL_SetRenderTarget(g_renderer, g_check.texture);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    SDL_RenderTexture(g_renderer, upright, &view.source, &dst);  // the upright copy, as the window drew it before
    SDL_Surface* a = SDL_RenderReadPixels(g_renderer, nullptr);
    SDL_RenderClear(g_renderer);
    DrawTarget(view, dst, win_w, win_h, /*into_texture=*/true, 255);  // the target, turned here
    SDL_Surface* b = SDL_RenderReadPixels(g_renderer, nullptr);
    SDL_SetRenderTarget(g_renderer, before);
    SDL_Surface* ra = a != nullptr ? SDL_ConvertSurface(a, SDL_PIXELFORMAT_RGBA32) : nullptr;
    SDL_Surface* rb = b != nullptr ? SDL_ConvertSurface(b, SDL_PIXELFORMAT_RGBA32) : nullptr;
    if (ra != nullptr && rb != nullptr && ra->w == rb->w && ra->h == rb->h) {
        ++g_check.runs;
        std::uint64_t pixels = 0, differ = 0, differ_more = 0;
        int max_diff = 0, first_x = -1, first_y = -1;
        const int x0 = std::max(0, static_cast<int>(dst.x)), x1 = std::min(ra->w, static_cast<int>(dst.x + dst.w + 0.999f));
        const int y0 = std::max(0, static_cast<int>(dst.y)), y1 = std::min(ra->h, static_cast<int>(dst.y + dst.h + 0.999f));
        for (int y = y0; y < y1; ++y) {
            const auto* pa = static_cast<const std::uint8_t*>(ra->pixels) + static_cast<size_t>(y) * ra->pitch;
            const auto* pb = static_cast<const std::uint8_t*>(rb->pixels) + static_cast<size_t>(y) * rb->pitch;
            for (int x = x0; x < x1; ++x) {
                int d = 0;
                for (int c = 0; c < 3; ++c) d = std::max(d, std::abs(pa[4 * x + c] - pb[4 * x + c]));
                ++pixels;
                if (d > 0) {
                    ++differ;
                    if (first_x < 0) first_x = x, first_y = y;
                }
                if (d > 2) ++differ_more;
                max_diff = std::max(max_diff, d);
            }
        }
        g_check.pixels += pixels;
        g_check.differ += differ;
        g_check.differ_more += differ_more;
        g_check.max_diff = std::max(g_check.max_diff, max_diff);
        std::fprintf(stderr,
                     "[Info] App: target check %u: the top screen %.0fx%.0f at %.0f,%.0f in %dx%d (upright %ux%u, scale %u, "
                     "flip %d): %llu of %llu px differ (max %d, %llu by more than 2, first at %d,%d); all checks: %llu of "
                     "%llu, max %d, %llu by more than 2\n",
                     g_check.runs, dst.w, dst.h, dst.x, dst.y, win_w, win_h, view.target_layout.UprightWidth(),
                     view.target_layout.UprightHeight(), view.target_layout.scale, view.target_layout.flip ? 1 : 0,
                     static_cast<unsigned long long>(differ), static_cast<unsigned long long>(pixels), max_diff,
                     static_cast<unsigned long long>(differ_more), first_x, first_y,
                     static_cast<unsigned long long>(g_check.differ), static_cast<unsigned long long>(g_check.pixels),
                     g_check.max_diff, static_cast<unsigned long long>(g_check.differ_more));
        // the fifth pair (the first ones come while the game loads, black), and the first that differs by more than 2
        if (dir != nullptr && ((g_check.saved == 0 && g_check.runs >= 5) || (g_check.saved <= 1 && differ_more > 0))) {
            const std::string base = std::string(dir) + "/target_check_" + std::to_string(g_check.runs);
            SDL_SaveBMP(ra, (base + "_upright.bmp").c_str());
            SDL_SaveBMP(rb, (base + "_target.bmp").c_str());
            ++g_check.saved;
        }
    }
    // MH4U_TARGET_BENCH=1 (with the check): each way of drawing the screen timed alone on the GPU (8 draws between two
    // glFinish): the upright copy as SDL draws it, the target turned here, the target in one filtered sample, and (1.0.1)
    // the colour filter's: the target turned with it, and the upright copy with it (DrawTextureFiltered: a screen SDL would
    // draw). With the filter off, a test filter (saturation +20) for those two, put back after.
    static const bool bench = [] { const char* v = std::getenv("MH4U_TARGET_BENCH"); return v != nullptr && *v == '1'; }();
    if (bench) {
        SDL_SetRenderTarget(g_renderer, g_check.texture);
        const bool was_on = g_colour_on;
        const ColourMatrix was_matrix = g_colour_matrix;
        if (!was_on) {
            ColourAdjust test{};
            test.saturation = 20;
            colour_matrix(&test, &g_colour_matrix);
        }
        SharedScreenView copy_view = view;  // (the upright copy as a screen SDL would draw: DrawTextureFiltered's)
        copy_view.texture = upright;
        copy_view.target = false;
        double ms[5] = {};
        for (int way = 0; way < 5; ++way) {
            SDL_FlushRenderer(g_renderer);
            glFinish();
            const std::uint64_t start = NowUs();
            for (int k = 0; k < 8; ++k) {
                if (way == 0) {
                    SDL_RenderTexture(g_renderer, upright, &view.source, &dst);
                    SDL_FlushRenderer(g_renderer);
                } else if (way == 4) {
                    DrawTextureFiltered(copy_view, dst, win_w, win_h, 255);
                } else {
                    DrawTarget(view, dst, win_w, win_h, /*into_texture=*/true, 255, /*no_cut=*/way == 2, /*colour=*/way == 3);
                }
            }
            SDL_FlushRenderer(g_renderer);
            glFinish();
            ms[way] = (NowUs() - start) / 8000.0;
        }
        g_colour_matrix = was_matrix;
        SDL_SetRenderTarget(g_renderer, before);
        std::fprintf(stderr,
                     "[Info] App: target bench: the upright copy drawn %.3f ms, the target turned %.3f ms, in one sample %.3f ms; "
                     "with the colour filter: the target turned %.3f ms, the upright copy %.3f ms\n",
                     ms[0], ms[1], ms[2], ms[3], ms[4]);
    }
    if (ra != nullptr) SDL_DestroySurface(ra);
    if (rb != nullptr) SDL_DestroySurface(rb);
    if (a != nullptr) SDL_DestroySurface(a);
    if (b != nullptr) SDL_DestroySurface(b);
}

namespace {

// MH4U_GL_TIMER=1 (a measurement aid): the GL time of each frame the window draws (GL_EXT_disjoint_timer_query), read a
// few frames later without a wait, into the pacing trace (kAppTopGpu) and the 5 s stats (MH4U_PRESENT_STATS=1).
struct GlTimer {
    bool on = false;
    PFNGLGENQUERIESEXTPROC gen = nullptr;
    PFNGLBEGINQUERYEXTPROC begin = nullptr;
    PFNGLENDQUERYEXTPROC end = nullptr;
    PFNGLGETQUERYOBJECTIVEXTPROC get_iv = nullptr;
    PFNGLGETQUERYOBJECTUI64VEXTPROC get_u64 = nullptr;
    static constexpr int kQueries = 8;
    GLuint queries[kQueries] = {};
    int next = 0, oldest = 0, pending = 0;
    bool open = false;
    std::uint64_t sum_ns = 0, frames = 0;
} g_timer;

void TimerInit() {
    const char* v = std::getenv("MH4U_GL_TIMER");
    if (v == nullptr || *v != '1') return;
    const char* gl = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (!HasExtension(gl, "GL_EXT_disjoint_timer_query")) {
        Log("MH4U_GL_TIMER: no GL_EXT_disjoint_timer_query");
        return;
    }
    GlTimer& t = g_timer;
    t.gen = reinterpret_cast<PFNGLGENQUERIESEXTPROC>(eglGetProcAddress("glGenQueriesEXT"));
    t.begin = reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(eglGetProcAddress("glBeginQueryEXT"));
    t.end = reinterpret_cast<PFNGLENDQUERYEXTPROC>(eglGetProcAddress("glEndQueryEXT"));
    t.get_iv = reinterpret_cast<PFNGLGETQUERYOBJECTIVEXTPROC>(eglGetProcAddress("glGetQueryObjectivEXT"));
    t.get_u64 = reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));
    if (t.gen == nullptr || t.begin == nullptr || t.end == nullptr || t.get_iv == nullptr || t.get_u64 == nullptr) return;
    t.gen(GlTimer::kQueries, t.queries);
    t.on = true;
    Log("MH4U_GL_TIMER: the window's frames are timed on the GPU");
}

// The results that are in, oldest first.
void TimerCollect() {
    GlTimer& t = g_timer;
    while (t.pending > 0) {
        GLint available = 0;
        t.get_iv(t.queries[t.oldest], GL_QUERY_RESULT_AVAILABLE_EXT, &available);
        if (!available) break;
        GLuint64 ns = 0;
        t.get_u64(t.queries[t.oldest], GL_QUERY_RESULT_EXT, &ns);
        GLint disjoint = 0;
        glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
        if (!disjoint) {
            t.sum_ns += ns;
            ++t.frames;
            mh4n::pacing::Mark(mh4n::pacing::kAppTopGpu, static_cast<std::uint32_t>(std::min<GLuint64>(ns, 0xFFFFFFFFu)));
        }
        t.oldest = (t.oldest + 1) % GlTimer::kQueries;
        --t.pending;
    }
}

}  // namespace

void shared_screens_frame_begin() {
    GlTimer& t = g_timer;
    if (!t.on || t.open) return;
    TimerCollect();
    if (t.pending == GlTimer::kQueries) return;  // (all still on the GPU: this frame goes untimed)
    SDL_FlushRenderer(g_renderer);
    t.begin(GL_TIME_ELAPSED_EXT, t.queries[t.next]);
    t.open = true;
}

void shared_screens_frame_end() {
    GlTimer& t = g_timer;
    if (!t.on || !t.open) return;
    SDL_FlushRenderer(g_renderer);
    t.end(GL_TIME_ELAPSED_EXT);
    t.open = false;
    t.next = (t.next + 1) % GlTimer::kQueries;
    ++t.pending;
}

void shared_screens_note_present(std::uint64_t us) {
    ++g_stats.presents;
    g_stats.present_us += us;
    if (us > g_stats.present_max_us) g_stats.present_max_us = us;
}

void shared_screens_frame_done() {
    if (!g_available) return;
    ++g_frame;
    if (g_stats_on && std::chrono::steady_clock::now() - g_stats_since >= std::chrono::seconds(5)) {
        const Stats& s = g_stats;
        std::fprintf(stderr,
                     "[Info] App: screens in 5 s: %u shown finished, %u shown with a GPU wait (%llu us in the call), %u "
                     "deferred; %u presents, %llu us each, %llu at most\n",
                     s.shown_ready, s.shown_waiting, static_cast<unsigned long long>(s.wait_us), s.deferred, s.presents,
                     static_cast<unsigned long long>(s.presents ? s.present_us / s.presents : 0),
                     static_cast<unsigned long long>(s.present_max_us));
        if (g_timer.frames > 0) {
            std::fprintf(stderr, "[Info] App: the window's GL time: %.3f ms a frame over %llu frames\n",
                         g_timer.sum_ns / 1e6 / g_timer.frames, static_cast<unsigned long long>(g_timer.frames));
            g_timer.sum_ns = 0;
            g_timer.frames = 0;
        }
        g_stats = Stats{};
        g_stats_since = std::chrono::steady_clock::now();
    }
    while (!g_retiring.empty()) {
        Retiring& r = g_retiring.front();
        if (p_eglClientWaitSyncKHR(Display(), r.fence, 0, 0) != EGL_CONDITION_SATISFIED_KHR) break;  // (in order)
        p_eglDestroySyncKHR(Display(), r.fence);
        g_retiring.pop_front();  // the picture goes: the renderer may draw into its buffer again
    }
    // imports of buffers not shown for a few seconds (the renderer made others: a new size, a pool that shrank), but not
    // one whose picture a view still shows: while the game waits (the settings or the layout editor open) the window
    // goes on presenting the same picture, and after 600 frames (10 s at 60 Hz) its texture was deleted under it and
    // the game behind the menu went black (1.0.1, a Moto g06)
    if (g_frame % 120 == 0) {
        for (auto it = g_imports.begin(); it != g_imports.end();) {
            if (g_frame - it->second.last_used > 600 && it->second.shown.expired()) {
                SyncWithSdl();
                DropImport(it->second);
                it = g_imports.erase(it);
            } else {
                ++it;
            }
        }
    }
}

int shared_screens_wait_fds(const SharedScreenView* views, int count, int* fds, int max) {
    int n = 0;
    for (int i = 0; i < count && n < max; ++i) {  // (the oldest unfinished picture of a view is the next one done)
        for (int j = 0; j < views[i].pending_count; ++j) {
            if (views[i].pending_fd[j] >= 0 && !FdSignaled(views[i].pending_fd[j])) {
                fds[n++] = views[i].pending_fd[j];
                break;
            }
        }
    }
    if (n < max && !FdSignaled(g_waiting_fd)) fds[n++] = g_waiting_fd;
    return n;
}

void shared_screens_reset(SharedScreenView* views, int count) {
    // (the textures are gone with the context: SDL's objects are destroyed, the EGL objects dropped)
    for (int i = 0; i < count; ++i) {
        FreeCpuTexture(views[i]);
        ClosePending(views[i]);
        views[i].texture = nullptr;
        views[i].target = false;
        views[i].picture.reset();
    }
    for (auto& [id, im] : g_imports) {
        if (im.texture != nullptr) SDL_DestroyTexture(im.texture);
        if (im.image != EGL_NO_IMAGE_KHR && p_eglDestroyImageKHR != nullptr) p_eglDestroyImageKHR(Display(), im.image);
    }
    g_imports.clear();
    g_retiring.clear();
    CloseWaiting();
    // (the programs and the corners' buffer went with the context: made again)
    for (TargetProgram& p : g_target_programs) p = TargetProgram{};
    g_target_programs_tried = false;
    for (auto& mode : g_ps1_target_programs) for (auto& row : mode) for (TargetProgram& p : row) p = TargetProgram{};  // (PS1 style's)
    for (auto& mode : g_ps1_texture_programs) for (auto& row : mode) for (Ps1TextureProgram& p : row) p = Ps1TextureProgram{};
    g_ps1_programs_tried = false;
    g_corners = 0;
}

void shared_screens_shutdown(SharedScreenView* views, int count) {
    if (g_available) {
        SyncWithSdl();
        glFinish();
    }
    for (int i = 0; i < count; ++i) {
        FreeCpuTexture(views[i]);
        ClosePending(views[i]);
        views[i].texture = nullptr;
        views[i].picture.reset();
    }
    for (Retiring& r : g_retiring) {
        if (r.fence != EGL_NO_SYNC_KHR) p_eglDestroySyncKHR(Display(), r.fence);
    }
    g_retiring.clear();
    for (auto& [id, im] : g_imports) DropImport(im);
    g_imports.clear();
    CloseWaiting();
}
