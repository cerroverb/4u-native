// Frame generation for the top screen (frame_gen.h; the project's own design and code, the same as MHGU's
// runtime/nvn/vulkan/framegen.c, in GL ES 3.1 here). The game held to half the display's rate or less (present_pacing's
// cadence n >= 2): when a new top picture B comes, a picture between it and the one before, A, is made; android_main.cpp
// presents it first, and B n - k vsyncs after it (k = n / 2; 30 -> 60 shown at n = 2). B is shown a vsync later than
// without it: the input delay the setting's help speaks of. The game gives no motion vectors, so they are found from the
// two pictures alone:
//
//  0. B drawn upright at its own size into a picture of ours (the same pass the window draws it with: shared_screens),
//     kept for the next pair;
//  1. luma pyramid: B's luma at 1/4, 1/8, 1/16 and 1/32 of its size (box averages), kept for the next pair;
//  2. a full search at 1/32, one vector per 64x64 pixels: every whole motion of up to 6 texels each way, the motion split
//     between the two pictures (A at c - floor(v / 2), B at c + v - floor(v / 2)), a 4x4 window;
//  3. at 1/16, 1/8 and 1/4 (a vector per 32, 16 and 16 pixels) each level takes the coarser one's four vectors round the
//     block, their mix and no motion, the best a 3x3 step further, at 1/4 below a texel by a parabola; a small cost for
//     leaving the coarser level's mix;
//  4. a 3x3 median of the field;
//  5. the picture: each pixel takes A at x - t v and B at x + (1 - t) v (t = k / n), their blend where they agree and,
//     where they don't (something uncovered or covered), the one nearer A and B's blend at x (a median of the three).
//     Pixels the same in A and B, and every pixel inside a part of the game's HUD (the hooks' boxes: the clock, the bars,
//     the sharpness, the plates, the map, the target camera, the item bar), take B's own: the HUD is never warped, and a
//     sample that would land in a HUD box is not used. Where the match is poor (a cut to another scene, smoke, a flash)
//     B's own pixel is taken.
// The touch screen, the touch controls and the menus are drawn over it by the window as over the game's own pictures. No
// picture is made, and the history starts again, while a menu of the game's, the title or the settings are open.
//
//   MH4U_FRAMEGEN=1            on whatever the setting (tests)
//   MH4U_FRAMEGEN_EVAL=<n>     (tests) n samples of quality: at the game's own rate, the picture between two frames two
//                              apart made and saved with the real one between them (BMPs in MH4U_FRAMEGEN_DIR, by
//                              default the app's external files folder: fgeval_<i>_{a,t,b,g}.bmp: earlier, truth, later,
//                              made); nothing extra is presented. MH4U_FRAMEGEN_EVAL_EVERY=<frames> (20),
//                              MH4U_FRAMEGEN_EVAL_AT=<s> after the first frame, MH4U_FRAMEGEN_EVAL_FROM=<n>: from the
//                              n-th top picture on (a scripted run's game frame, near enough at the game's own rate)
#include "frame_gen.h"

#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <GLES2/gl2ext.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "mh4n/pacing_trace.h"
#include "perfstats.h"
#include "shared_screens.h"

namespace {

#define FG_FRAG_HEAD "#version 310 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\nout vec4 o;\n"

constexpr const char* kVertex =
    "#version 310 es\n"
    "void main() {\n"
    "  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

// the next level of the luma pyramid: from the picture (b.x = 0) each texel the luma of a 4x4 block's average (four
// bilinear taps), and in green the HUD's mask (1 where the texel's 4x4 pixels touch a HUD box: the made picture reads it
// instead of testing every box at every pixel); from a level (b.x = 1) the average of 2x2 texels (one tap at their corner)
constexpr const char* kDown = FG_FRAG_HEAD
    "uniform sampler2D src;\n"
    "uniform vec4 a, b;  // a: 1 / the source's size, the level's size; b.x: from a level, b.y: HUD boxes\n"
    "uniform vec4 hud[12];\n"
    "void main() {\n"
    "  vec2 p = floor(gl_FragCoord.xy);\n"
    "  float l, hm = 0.0;\n"
    "  if (b.x > 0.5) {\n"
    "    l = texture(src, (p * 2.0 + 1.0) * a.xy).r;\n"
    "  } else {\n"
    "    vec2 q = p * 4.0;\n"
    "    vec4 s = texture(src, (q + vec2(1.0, 1.0)) * a.xy) + texture(src, (q + vec2(3.0, 1.0)) * a.xy)\n"
    "           + texture(src, (q + vec2(1.0, 3.0)) * a.xy) + texture(src, (q + vec2(3.0, 3.0)) * a.xy);\n"
    "    l = dot(s.rgb * 0.25, vec3(0.299, 0.587, 0.114));\n"
    "    vec4 px = vec4(q, q + 4.0);\n"
    "    for (int k = 0; k < int(b.y); k++)\n"
    "      if (px.x < hud[k].z && px.z > hud[k].x && px.y < hud[k].w && px.w > hud[k].y) hm = 1.0;\n"
    "  }\n"
    "  o = vec4(l, hm, 0.0, 1.0);\n"
    "}\n";

// The mean absolute difference of the 4x4 texels round the block's centre c (a texel corner) between A and B for the
// whole motion v (texels of the level), split between the two: A at c - floor(v / 2), B at c + v - floor(v / 2). Each
// gather takes exactly the 2x2 texels round a corner.
#define FG_SAD                                                                                 \
    "float sad4(vec2 c, vec2 v) {\n"                                                           \
    "  vec2 h = floor(v * 0.5), ca = c - h, cb = c + v - h;\n"                                 \
    "  float s = 0.0;\n"                                                                       \
    "  for (int j = 0; j < 2; j++)\n"                                                          \
    "    for (int i = 0; i < 2; i++) {\n"                                                      \
    "      vec2 d0 = vec2(float(i * 2 - 1), float(j * 2 - 1));\n"                              \
    "      vec4 d = abs(textureGather(la, (ca + d0) * a.xy) - textureGather(lb, (cb + d0) * a.xy));\n" \
    "      s += d.x + d.y + d.z + d.w;\n"                                                      \
    "    }\n"                                                                                  \
    "  return s * 0.0625;\n"                                                                   \
    "}\n"

// The top of the pyramid (1/32): one vector per 2x2 texels (64x64 pixels), every whole motion of up to R texels each way
constexpr const char* kSearch = FG_FRAG_HEAD
    "uniform sampler2D la, lb;\n"
    "uniform vec4 a, b;  // a: 1 / the level's size, the field's size; b.x: pixels a texel, b.y: R\n"
    FG_SAD
    "void main() {\n"
    "  vec2 c = floor(gl_FragCoord.xy) * 2.0 + 1.0;\n"
    "  int r = int(b.y);\n"
    "  float best = 1e9;\n"
    "  vec2 bv = vec2(0.0);\n"
    "  for (int y = -r; y <= r; y++)\n"
    "    for (int x = -r; x <= r; x++) {\n"
    "      vec2 v = vec2(float(x), float(y));\n"
    "      float s = sad4(c, v) + 0.0005 * (abs(v.x) + abs(v.y));\n"
    "      if (s < best) { best = s; bv = v; }\n"
    "    }\n"
    "  o = vec4(bv * b.x, best, 0.0);\n"
    "}\n";

// A finer level: one vector per bs x bs texels; candidates the parent level's four vectors round the block, their mix and
// no motion, the best taken a 3x3 step further (at the last level below a texel by a parabola)
constexpr const char* kRefine = FG_FRAG_HEAD
    "uniform sampler2D la, lb, vp;\n"
    "uniform vec4 a, b, c;  // a: 1 / the level's size, the field's size; b: pixels a texel, texels a block, the parent\n"
    "// field's size; c.x: pixels a parent block, c.y: the cost of leaving the parents' mix, c.z: 1 at the last level\n"
    FG_SAD
    "vec2 pm;\n"
    "float cost(vec2 q, vec2 v) { return sad4(q, v) + c.y * length(v - pm); }\n"
    "void main() {\n"
    "  vec2 p = floor(gl_FragCoord.xy);\n"
    "  vec2 cc = p * b.y + b.y * 0.5;\n"
    "  vec2 q = cc * b.x / c.x - 0.5;\n"
    "  ivec2 q0 = ivec2(floor(q)), hi = ivec2(b.zw) - 1;\n"
    "  vec2 f = q - vec2(q0);\n"
    "  vec2 p00 = texelFetch(vp, clamp(q0, ivec2(0), hi), 0).xy / b.x;\n"
    "  vec2 p10 = texelFetch(vp, clamp(q0 + ivec2(1, 0), ivec2(0), hi), 0).xy / b.x;\n"
    "  vec2 p01 = texelFetch(vp, clamp(q0 + ivec2(0, 1), ivec2(0), hi), 0).xy / b.x;\n"
    "  vec2 p11 = texelFetch(vp, clamp(q0 + ivec2(1, 1), ivec2(0), hi), 0).xy / b.x;\n"
    "  pm = mix(mix(p00, p10, f.x), mix(p01, p11, f.x), f.y);\n"
    "  vec2 cand[6];\n"
    "  cand[0] = floor(pm + 0.5); cand[1] = floor(p00 + 0.5); cand[2] = floor(p10 + 0.5);\n"
    "  cand[3] = floor(p01 + 0.5); cand[4] = floor(p11 + 0.5); cand[5] = vec2(0.0);\n"
    "  float best = 1e9;\n"
    "  vec2 bv = vec2(0.0);\n"
    "  for (int i = 0; i < 6; i++) {\n"
    "    float s = cost(cc, cand[i]);\n"
    "    if (s < best) { best = s; bv = cand[i]; }\n"
    "  }\n"
    "  float s3[9];\n"
    "  int bi = 4;\n"
    "  for (int y = -1; y <= 1; y++)\n"
    "    for (int x = -1; x <= 1; x++) {\n"
    "      int k = (y + 1) * 3 + x + 1;\n"
    "      s3[k] = k == 4 ? best : cost(cc, bv + vec2(float(x), float(y)));\n"
    "      if (s3[k] < s3[bi]) bi = k;\n"
    "    }\n"
    "  vec2 sub = vec2(0.0);\n"
    "  if (bi == 4 && c.z > 0.5) {\n"
    "    float dx = s3[3] - 2.0 * s3[4] + s3[5], dy = s3[1] - 2.0 * s3[4] + s3[7];\n"
    "    if (dx > 1e-5) sub.x = clamp(0.5 * (s3[3] - s3[5]) / dx, -0.5, 0.5);\n"
    "    if (dy > 1e-5) sub.y = clamp(0.5 * (s3[1] - s3[7]) / dy, -0.5, 0.5);\n"
    "  }\n"
    "  bv += vec2(float(bi % 3 - 1), float(bi / 3 - 1));\n"
    "  o = vec4((bv + sub) * b.x, s3[bi], 0.0);\n"
    "}\n";

constexpr const char* kClean = FG_FRAG_HEAD
    "uniform sampler2D vi;\n"
    "uniform vec4 a;  // a.xy: the field's size\n"
    "#define S(i, j) { vec4 lo = min(v[i], v[j]); v[j] = max(v[i], v[j]); v[i] = lo; }\n"
    "void main() {\n"
    "  ivec2 p = ivec2(gl_FragCoord.xy), hi = ivec2(a.xy) - 1;\n"
    "  vec4 v[9];\n"
    "  for (int y = -1; y <= 1; y++)\n"
    "    for (int x = -1; x <= 1; x++) v[(y + 1) * 3 + x + 1] = texelFetch(vi, clamp(p + ivec2(x, y), ivec2(0), hi), 0);\n"
    "  S(1, 2) S(4, 5) S(7, 8) S(0, 1) S(3, 4) S(6, 7) S(1, 2) S(4, 5) S(7, 8) S(0, 3) S(5, 8) S(4, 7) S(3, 6)\n"
    "  S(1, 4) S(2, 5) S(4, 7) S(4, 2) S(6, 4) S(4, 2)\n"
    "  o = v[4];\n"
    "}\n";

constexpr const char* kCompose = FG_FRAG_HEAD
    "uniform sampler2D pa, pb, vf, hm;  // hm: B's 1/4 level, its green the HUD's mask\n"
    "uniform vec4 a, b, c;  // a: size, 1 / size; b: t, -, the same-pixel threshold, HUD boxes; c: 1 / the field in pixels, the poor-match range\n"
    "bool in_hud(vec2 x) { return texelFetch(hm, ivec2(x * 0.25), 0).g > 0.5; }\n"
    "float maxc(vec4 d) { return max(max(d.r, d.g), d.b); }\n"
    "vec4 med3(vec4 x, vec4 y, vec4 z) { return max(min(x, y), min(max(x, y), z)); }\n"
    "void main() {\n"
    "  ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "  vec4 c1 = texelFetch(pb, p, 0), c0 = texelFetch(pa, p, 0);\n"
    "  vec2 x = vec2(p) + 0.5;\n"
    "  vec4 r = c1;\n"
    "  if (maxc(abs(c1 - c0)) > b.z && !in_hud(x)) {\n"
    "    vec4 m = texture(vf, x * c.xy);\n"
    "    float t = b.x;\n"
    "    vec2 xa = x - t * m.xy, xb = x + (1.0 - t) * m.xy;\n"
    "    bool oka = all(greaterThanEqual(xa, vec2(0.0))) && all(lessThan(xa, a.xy)) && !in_hud(xa);\n"
    "    bool okb = all(greaterThanEqual(xb, vec2(0.0))) && all(lessThan(xb, a.xy)) && !in_hud(xb);\n"
    "    vec4 sa = texture(pa, xa * a.zw), sb = texture(pb, xb * a.zw);\n"
    "    vec4 g = c1;\n"
    "    if (oka && okb) {\n"
    "      float k = smoothstep(0.04, 0.12, maxc(abs(sa - sb)));\n"
    "      g = mix(mix(sa, sb, t), med3(sa, sb, mix(c0, c1, t)), k);\n"
    "    } else if (oka) {\n"
    "      g = sa;\n"
    "    } else if (okb) {\n"
    "      g = sb;\n"
    "    }\n"
    "    r = mix(g, c1, smoothstep(c.z, c.w, m.z));\n"
    "  }\n"
    "  o = vec4(r.rgb, 1.0);\n"
    "}\n";

// ES 3 entry points, looked up (the context SDL asked for is ES 2; the drivers give a 3.x one where they can)
PFNGLTEXSTORAGE2DPROC p_glTexStorage2D = nullptr;
PFNGLGENSAMPLERSPROC p_glGenSamplers = nullptr;
PFNGLBINDSAMPLERPROC p_glBindSampler = nullptr;
PFNGLSAMPLERPARAMETERIPROC p_glSamplerParameteri = nullptr;
PFNGLDELETESAMPLERSPROC p_glDeleteSamplers = nullptr;
PFNGLGENQUERIESPROC p_glGenQueries = nullptr;
PFNGLBEGINQUERYPROC p_glBeginQuery = nullptr;
PFNGLENDQUERYPROC p_glEndQuery = nullptr;
PFNGLGETQUERYOBJECTUIVPROC p_glGetQueryObjectuiv = nullptr;
PFNGLGETQUERYOBJECTUI64VEXTPROC p_glGetQueryObjectui64vEXT = nullptr;
constexpr GLenum kTimeElapsed = 0x88BF;  // GL_TIME_ELAPSED_EXT
constexpr GLenum kDisjoint = 0x8FBB;     // GL_GPU_DISJOINT_EXT

struct Prog {
    GLuint id = 0;
    GLint u[16] = {};
};
enum { U_A, U_B, U_C, U_HUD, U_T0, U_T1, U_T2, U_T3 };

GLuint Compile(GLenum kind, const char* text, const char* name) {
    GLuint s = glCreateShader(kind);
    glShaderSource(s, 1, &text, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[Error] App: frame generation's %s shader: %s\n", name, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool Link(Prog& p, const char* frag, const char* name, const char* const* samplers, int ns) {
    const GLuint vs = Compile(GL_VERTEX_SHADER, kVertex, name), fs = Compile(GL_FRAGMENT_SHADER, frag, name);
    if (vs != 0 && fs != 0) {
        p.id = glCreateProgram();
        glAttachShader(p.id, vs);
        glAttachShader(p.id, fs);
        glLinkProgram(p.id);
        GLint ok = 0;
        glGetProgramiv(p.id, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024] = {};
            glGetProgramInfoLog(p.id, sizeof(log), nullptr, log);
            std::fprintf(stderr, "[Error] App: frame generation's %s program: %s\n", name, log);
            glDeleteProgram(p.id);
            p.id = 0;
        }
    }
    if (vs != 0) glDeleteShader(vs);
    if (fs != 0) glDeleteShader(fs);
    if (p.id == 0) return false;
    p.u[U_A] = glGetUniformLocation(p.id, "a");
    p.u[U_B] = glGetUniformLocation(p.id, "b");
    p.u[U_C] = glGetUniformLocation(p.id, "c");
    p.u[U_HUD] = glGetUniformLocation(p.id, "hud");
    glUseProgram(p.id);
    for (int i = 0; i < ns; ++i) glUniform1i(glGetUniformLocation(p.id, samplers[i]), i);
    return true;
}

struct Tex {
    GLuint name = 0;
    int w = 0, h = 0;
    SDL_Texture* sdl = nullptr;  // (the upright pictures and the made one are SDL render targets: the window draws them)
};

struct State {
    int tried = 0;  // 0 not yet, 1 ready, -1 not possible on this device (logged once)
    Prog down, search, refine, clean, compose;
    GLuint fbo = 0, lin = 0, near = 0;
    int w = 0, h = 0, lw[5] = {}, lh[5] = {}, fw[5] = {}, fh[5] = {};  // the picture, the levels 1..4, the fields 1..4
    Tex up[3], lv[3][5], fd[5], vm, gen;  // the upright pictures (A, B...) and their levels; the fields; the made picture
    int ring = 0, have = 0;
    // GPU time (GL_EXT_disjoint_timer_query)
    bool timer = false;
    GLuint q[8] = {};
    std::uint64_t qframe[8] = {}, made_no = 0;
    // the per-minute line
    std::chrono::steady_clock::time_point log_t0{};
    unsigned n_game = 0, n_made = 0, n_shown = 0, n_warm = 0, n_skip = 0, n_cadence = 0;
    double gpu_sum = 0, gpu_max = 0;
    unsigned gpu_n = 0;
    int last_n = 0;
    int told_state = -1, told_lines = 0;
    const char* told_why = nullptr;  // (not making: the reason logged; another one is logged too)
    // the screen's refresh rate (display_vsync.cpp's period), a new one taken once it holds for 15 game frames, and when
    // it last changed (from what)
    double rate_hz = 0, rate_from = 0, rate_new = 0;
    int rate_held = 0, rate_lines = 0;
    std::chrono::steady_clock::time_point rate_at{};
    const char* pause_why = nullptr;  // (the reason a pause gave: kept while it lasts)
    // the game's own rate while pictures are made (a phone whose GPU can't do both: none made for a while)
    bool making = false;
    std::chrono::steady_clock::time_point last_real{}, pause_until{};
    double avg_ms = 0;
    int slow = 0, pauses = 0;
    unsigned n_slow = 0;
    std::chrono::steady_clock::time_point told_at{};
    // quality samples (MH4U_FRAMEGEN_EVAL)
    int eval_left = -1, eval_every = 20, eval_no = 0;
    unsigned eval_frame = 0;
    double eval_at = 0;
    unsigned eval_from = 0, calls = 0;
    std::chrono::steady_clock::time_point first{};
    std::string eval_dir;
} g;

bool g_enabled = false;
int g_env = -1;

bool HasExtension(const char* list, const char* name) {
    if (list == nullptr) return false;
    const size_t n = std::strlen(name);
    for (const char* p = list; (p = std::strstr(p, name)) != nullptr; p += n)
        if ((p == list || p[-1] == ' ') && (p[n] == ' ' || p[n] == '\0')) return true;
    return false;
}

bool Setup() {
    if (g.tried != 0) return g.tried > 0;
    g.tried = -1;
    const char* ver = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    int major = 0, minor = 0;
    if (ver == nullptr || std::sscanf(ver, "OpenGL ES %d.%d", &major, &minor) != 2 || major * 10 + minor < 31) {
        std::fprintf(stderr, "[Info] App: frame generation needs OpenGL ES 3.1 (this context: %s): off\n", ver ? ver : "?");
        return false;
    }
    const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (major * 10 + minor < 32 && !HasExtension(ext, "GL_EXT_color_buffer_half_float") &&
        !HasExtension(ext, "GL_EXT_color_buffer_float")) {
        std::fprintf(stderr, "[Info] App: frame generation needs half-float render targets, which this GPU lacks: off\n");
        return false;
    }
    const auto proc = [](const char* n) { return eglGetProcAddress(n); };
    p_glTexStorage2D = reinterpret_cast<PFNGLTEXSTORAGE2DPROC>(proc("glTexStorage2D"));
    p_glGenSamplers = reinterpret_cast<PFNGLGENSAMPLERSPROC>(proc("glGenSamplers"));
    p_glBindSampler = reinterpret_cast<PFNGLBINDSAMPLERPROC>(proc("glBindSampler"));
    p_glSamplerParameteri = reinterpret_cast<PFNGLSAMPLERPARAMETERIPROC>(proc("glSamplerParameteri"));
    p_glDeleteSamplers = reinterpret_cast<PFNGLDELETESAMPLERSPROC>(proc("glDeleteSamplers"));
    p_glGenQueries = reinterpret_cast<PFNGLGENQUERIESPROC>(proc("glGenQueries"));
    p_glBeginQuery = reinterpret_cast<PFNGLBEGINQUERYPROC>(proc("glBeginQuery"));
    p_glEndQuery = reinterpret_cast<PFNGLENDQUERYPROC>(proc("glEndQuery"));
    p_glGetQueryObjectuiv = reinterpret_cast<PFNGLGETQUERYOBJECTUIVPROC>(proc("glGetQueryObjectuiv"));
    p_glGetQueryObjectui64vEXT = reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(proc("glGetQueryObjectui64vEXT"));
    if (!p_glTexStorage2D || !p_glGenSamplers || !p_glBindSampler || !p_glSamplerParameteri) {
        std::fprintf(stderr, "[Info] App: frame generation: the GL ES 3 functions are missing: off\n");
        return false;
    }
    static const char* const s1[] = {"src"};
    static const char* const s2[] = {"la", "lb"};
    static const char* const s3[] = {"la", "lb", "vp"};
    static const char* const sv[] = {"vi"};
    static const char* const sc[] = {"pa", "pb", "vf", "hm"};
    if (!Link(g.down, kDown, "down", s1, 1) || !Link(g.search, kSearch, "search", s2, 2) ||
        !Link(g.refine, kRefine, "refine", s3, 3) || !Link(g.clean, kClean, "clean", sv, 1) ||
        !Link(g.compose, kCompose, "compose", sc, 4)) {
        std::fprintf(stderr, "[Info] App: frame generation's shaders could not be made: off\n");
        return false;
    }
    glGenFramebuffers(1, &g.fbo);
    GLuint s[2];
    p_glGenSamplers(2, s);
    g.lin = s[0], g.near = s[1];
    for (int i = 0; i < 2; ++i) {
        p_glSamplerParameteri(s[i], GL_TEXTURE_MIN_FILTER, i ? GL_NEAREST : GL_LINEAR);
        p_glSamplerParameteri(s[i], GL_TEXTURE_MAG_FILTER, i ? GL_NEAREST : GL_LINEAR);
        p_glSamplerParameteri(s[i], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        p_glSamplerParameteri(s[i], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    g.timer = HasExtension(ext, "GL_EXT_disjoint_timer_query") && p_glGenQueries && p_glBeginQuery && p_glEndQuery &&
              p_glGetQueryObjectuiv && p_glGetQueryObjectui64vEXT;
    if (g.timer) p_glGenQueries(8, g.q);
    g.tried = 1;
    std::fprintf(stderr, "[Info] App: frame generation ready (%s; GPU time %s)\n", ver,
                 g.timer ? "measured" : "not measurable here");
    return true;
}

void FreeTex(Tex& t) {
    if (t.sdl != nullptr) SDL_DestroyTexture(t.sdl);
    else if (t.name != 0) glDeleteTextures(1, &t.name);
    t = Tex{};
}

void FreeImages() {
    for (int i = 0; i < 3; ++i) {
        FreeTex(g.up[i]);
        for (int k = 1; k <= 4; ++k) FreeTex(g.lv[i][k]);
    }
    for (int k = 1; k <= 4; ++k) FreeTex(g.fd[k]);
    FreeTex(g.vm), FreeTex(g.gen);
    g.w = g.h = 0, g.have = 0;
}

bool OwnTex(Tex& t, GLenum fmt, int w, int h) {
    glGenTextures(1, &t.name);
    glBindTexture(GL_TEXTURE_2D, t.name);
    p_glTexStorage2D(GL_TEXTURE_2D, 1, fmt, w, h);
    glBindTexture(GL_TEXTURE_2D, 0);
    t.w = w, t.h = h;
    glBindFramebuffer(GL_FRAMEBUFFER, g.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.name, 0);
    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return ok;
}

bool SdlTex(SDL_Renderer* r, Tex& t, int w, int h) {
    t.sdl = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
    if (t.sdl == nullptr) return false;
    SDL_SetTextureScaleMode(t.sdl, SDL_SCALEMODE_LINEAR);
    t.name = static_cast<GLuint>(SDL_GetNumberProperty(SDL_GetTextureProperties(t.sdl), SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER, 0));
    t.w = w, t.h = h;
    return t.name != 0;
}

bool ImagesFor(SDL_Renderer* r, int w, int h) {
    if (g.w == w && g.h == h) return g.gen.name != 0;
    FreeImages();
    g.w = w, g.h = h;
    g.lw[1] = (w + 3) / 4, g.lh[1] = (h + 3) / 4;
    for (int k = 2; k <= 4; ++k) g.lw[k] = (g.lw[k - 1] + 1) / 2, g.lh[k] = (g.lh[k - 1] + 1) / 2;
    g.fw[1] = (g.lw[1] + 3) / 4, g.fh[1] = (g.lh[1] + 3) / 4;  // (4x4 texels a vector at 1/4, 2x2 at the others)
    for (int k = 2; k <= 4; ++k) g.fw[k] = (g.lw[k] + 1) / 2, g.fh[k] = (g.lh[k] + 1) / 2;
    SDL_FlushRenderer(r);
    bool ok = true;
    for (int i = 0; i < 3 && ok; ++i) {
        ok = SdlTex(r, g.up[i], w, h);
        for (int k = 1; k <= 4 && ok; ++k) ok = OwnTex(g.lv[i][k], k == 1 ? GL_RG8 : GL_R8, g.lw[k], g.lh[k]);
    }
    for (int k = 1; k <= 4 && ok; ++k) ok = OwnTex(g.fd[k], GL_RGBA16F, g.fw[k], g.fh[k]);
    ok = ok && OwnTex(g.vm, GL_RGBA16F, g.fw[1], g.fh[1]) && SdlTex(r, g.gen, w, h);
    if (!ok) {
        std::fprintf(stderr, "[Warning] App: frame generation's pictures (%dx%d) could not be made: none made at this size\n", w, h);
        FreeImages();
        g.w = w, g.h = h;
        return false;
    }
    std::fprintf(stderr, "[Info] App: frame generation: pictures %dx%d, vectors %dx%d (searched at %dx%d)\n", w, h,
                 g.fw[1], g.fh[1], g.fw[4], g.fh[4]);
    return true;
}

// The GL state SDL's renderer leaves (it is put back exactly after the passes: SDL keeps its own copy of it)
struct Saved {
    GLint fbo = 0, prog = 0, active = 0, vp[4] = {}, buf = 0, tex[4] = {}, attr[3] = {};
    GLboolean blend = GL_FALSE, scissor = GL_FALSE, depth = GL_FALSE, cull = GL_FALSE;
};
Saved SaveState() {
    Saved s;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &s.fbo);
    glGetIntegerv(GL_CURRENT_PROGRAM, &s.prog);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &s.active);
    glGetIntegerv(GL_VIEWPORT, s.vp);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &s.buf);
    for (int i = 0; i < 4; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &s.tex[i]);
    }
    for (int i = 0; i < 3; ++i) glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &s.attr[i]);
    s.blend = glIsEnabled(GL_BLEND), s.scissor = glIsEnabled(GL_SCISSOR_TEST);
    s.depth = glIsEnabled(GL_DEPTH_TEST), s.cull = glIsEnabled(GL_CULL_FACE);
    return s;
}
void RestoreState(const Saved& s) {
    for (int i = 0; i < 4; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, s.tex[i]);
        p_glBindSampler(i, 0);
    }
    glActiveTexture(s.active);
    glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
    glUseProgram(s.prog);
    glViewport(s.vp[0], s.vp[1], s.vp[2], s.vp[3]);
    glBindBuffer(GL_ARRAY_BUFFER, s.buf);
    for (int i = 0; i < 3; ++i) s.attr[i] ? glEnableVertexAttribArray(i) : glDisableVertexAttribArray(i);
    s.blend ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
    s.scissor ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);
    s.depth ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
    s.cull ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
}

// One pass: `p` into `dst` with the textures bound to units 0.. (linear or nearest)
void Pass(const Prog& p, const Tex& dst, const Tex* const* src, const bool* linear, int ns) {
    glBindFramebuffer(GL_FRAMEBUFFER, g.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst.name, 0);
    glViewport(0, 0, dst.w, dst.h);
    glUseProgram(p.id);
    for (int i = 0; i < ns; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, src[i]->name);
        p_glBindSampler(i, linear[i] ? g.lin : g.near);
    }
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void ReadTimes() {
    if (!g.timer) return;
    GLint disjoint = 0;
    glGetIntegerv(kDisjoint, &disjoint);
    for (int i = 0; i < 8; ++i) {
        if (g.qframe[i] == 0 || g.made_no - g.qframe[i] < 3) continue;
        GLuint ready = 0;
        p_glGetQueryObjectuiv(g.q[i], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready && g.made_no - g.qframe[i] < 8) continue;
        g.qframe[i] = 0;
        if (!ready || disjoint) continue;
        GLuint64 ns = 0;
        p_glGetQueryObjectui64vEXT(g.q[i], GL_QUERY_RESULT, &ns);
        const double ms = ns / 1e6;
        if (ms > 100) continue;
        g.gpu_sum += ms, ++g.gpu_n;
        if (ms > g.gpu_max) g.gpu_max = ms;
    }
}

void MinuteLine() {
    const auto now = std::chrono::steady_clock::now();
    if (g.log_t0.time_since_epoch().count() == 0) g.log_t0 = now;
    const double s = std::chrono::duration<double>(now - g.log_t0).count();
    if (s < 60) return;
    if (g.n_game)
        std::fprintf(stderr,
                     "[Info] App: frame generation, last %.0f s: %u pictures made between %u of the game's (shown %.1f FPS, "
                     "the game %.1f); not made: %u warming up, %u menus or settings, %u not at half the refresh rate or "
                     "less, %u paused (the game fell behind its limit); GPU %.2f ms a made picture (max %.2f, %u measured), %dx%d, %d "
                     "vsyncs a game frame\n",
                     s, g.n_made, g.n_game, (g.n_game + g.n_shown) / s, g.n_game / s, g.n_warm, g.n_skip, g.n_cadence, g.n_slow,
                     g.gpu_n ? g.gpu_sum / g.gpu_n : 0.0, g.gpu_max, g.gpu_n, g.w, g.h, g.last_n);
    g.log_t0 = now;
    g.n_game = g.n_made = g.n_shown = g.n_warm = g.n_skip = g.n_cadence = g.n_slow = 0;
    g.gpu_sum = g.gpu_max = 0, g.gpu_n = 0;
}

// A change between making pictures and not, or of the reason none is made (1.0.1: the FPS limit not half the refresh
// rate after the title's reason was never said), with the reason (at most one a 5 s, 60 lines)
void Tell(int making, const char* why) {
    const auto now = std::chrono::steady_clock::now();
    const bool same = making ? g.told_state == 1
                             : g.told_state == 0 && g.told_why != nullptr && std::strcmp(why, g.told_why) == 0;
    if (same || g.told_lines >= 60 ||
        (g.told_lines > 0 && std::chrono::duration<double>(now - g.told_at).count() < 5)) return;
    g.told_state = making, g.told_why = making ? nullptr : why, g.told_at = now, ++g.told_lines;
    std::fprintf(stderr, "[Info] App: frame generation: %s%s\n", making ? "making pictures" : "not making pictures: ",
                 making ? "" : why);
}

}  // namespace

void frame_gen_set_enabled(bool on) { g_enabled = on; }

bool frame_gen_enabled() {
    if (g_env < 0) {
        const char* e = std::getenv("MH4U_FRAMEGEN");
        const char* q = std::getenv("MH4U_FRAMEGEN_EVAL");  // (quality samples: frame_gen_make has to run)
        g_env = (e != nullptr && *e == '1') || (q != nullptr && std::atoi(q) > 0);
    }
    return g_enabled || g_env > 0;
}

extern std::atomic<uint64_t> g_framegen_presents;  // (perfstats.cpp: the FPS counter's "(60 shown)")
void frame_gen_presented() {
    ++g.n_shown;
    g_framegen_presents.fetch_add(1, std::memory_order_relaxed);
}

void frame_gen_reset() {  // (the textures went with the context: forgotten, not deleted)
    g = State{};
}

namespace {
// One of the pictures into a BMP (glReadPixels through the passes' framebuffer; rows bottom-up there: the BMP is upright)
void SaveBmp(const Tex& t, const char* tag) {
    glBindFramebuffer(GL_FRAMEBUFFER, g.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.name, 0);
    SDL_Surface* sf = SDL_CreateSurface(t.w, t.h, SDL_PIXELFORMAT_RGBA32);
    if (sf == nullptr) return;
    glReadPixels(0, 0, t.w, t.h, GL_RGBA, GL_UNSIGNED_BYTE, sf->pixels);  // (row 0 = the picture's top: kept so)
    for (int y = 0; y < t.h; ++y)
        for (int x = 0; x < t.w; ++x) static_cast<std::uint8_t*>(sf->pixels)[(static_cast<size_t>(y) * sf->pitch) + x * 4 + 3] = 255;
    char path[512];
    std::snprintf(path, sizeof path, "%s/fgeval_%03d_%s.bmp", g.eval_dir.c_str(), g.eval_no, tag);
    if (!SDL_SaveBMP(sf, path)) std::fprintf(stderr, "[Warning] App: frame generation: %s not saved: %s\n", path, SDL_GetError());
    SDL_DestroySurface(sf);
}
}  // namespace

SDL_Texture* frame_gen_make(SDL_Renderer* renderer, SharedScreenView& view, int n, const FrameGenBox* hud, int nhud,
                            const char* skip, int* follow) {
    *follow = 0;
    if (g.tried < 0) return nullptr;
    if (g.eval_left < 0) {
        const char* e = std::getenv("MH4U_FRAMEGEN_EVAL");
        g.eval_left = e != nullptr ? std::max(0, std::atoi(e)) : 0;
        const char* k = std::getenv("MH4U_FRAMEGEN_EVAL_EVERY");
        g.eval_every = k != nullptr && std::atoi(k) > 2 ? std::atoi(k) : 20;
        const char* at = std::getenv("MH4U_FRAMEGEN_EVAL_AT");
        g.eval_at = at != nullptr ? std::atof(at) : 0;
        const char* from = std::getenv("MH4U_FRAMEGEN_EVAL_FROM");
        g.eval_from = from != nullptr ? static_cast<unsigned>(std::atol(from)) : 0;
        const char* d = std::getenv("MH4U_FRAMEGEN_DIR");
        const char* ext = SDL_GetAndroidExternalStoragePath();
        g.eval_dir = d != nullptr ? d : ext != nullptr ? ext : ".";
        if (g.eval_left > 0)
            std::fprintf(stderr, "[Info] App: frame generation: %d quality samples, one every %d frames, from %.0f s, into %s\n",
                         g.eval_left, g.eval_every, g.eval_at, g.eval_dir.c_str());
    }
    const auto now = std::chrono::steady_clock::now();
    if (g.first.time_since_epoch().count() == 0) g.first = now;
    const bool eval = g.eval_left > 0;
    ++g.calls;
    if (eval && (std::chrono::duration<double>(now - g.first).count() < g.eval_at || g.calls < g.eval_from)) return nullptr;
    ReadTimes();
    MinuteLine();
    ++g.n_game;
    g.last_n = n;
    {  // The screen's refresh rate: a change logged, whether the app asked for it (FrameGen or the FPS limit changed:
       // GameActivity.pickRefreshRate logs its own "display mode" line) or not (a Galaxy S25 Ultra's panel went 60 -> 120
       // -> 60 Hz by itself 3 minutes into a 30 FPS run), and named by a pause that follows it. Taken once it holds for 15
       // game frames (one late vsync callback measures a period twice as long)
        const double period = static_cast<double>(mh4n::pacing::g_display_period_ns.load(std::memory_order_relaxed));
        const double hz = period > 0 ? 1e9 / period : 0;
        if (hz <= 0) {
        } else if (g.rate_hz <= 0) {
            g.rate_hz = hz;
        } else if (std::fabs(hz - g.rate_hz) <= g.rate_hz * .03) {
            g.rate_held = 0;
        } else {
            g.rate_held = g.rate_held > 0 && std::fabs(hz - g.rate_new) <= g.rate_new * .03 ? g.rate_held + 1 : 1;
            g.rate_new = hz;
            if (g.rate_held >= 15) {
                if (g.rate_lines++ < 40)
                    std::fprintf(stderr, "[Info] App: frame generation: the screen's refresh rate changed: %.0f Hz now, %.0f Hz "
                                         "before (the FPS limit %.0f, %d vsync(s) a game frame)\n", hz, g.rate_hz,
                                 perfstats_fps_cap(), n);
                g.rate_from = g.rate_hz, g.rate_hz = hz, g.rate_at = now, g.rate_held = 0;
            }
        }
    }
    const int w = view.target ? view.target_layout.UprightWidth() : view.width;
    const int h = view.target ? view.target_layout.UprightHeight() : view.height;
    const char* why = skip;
    if (why != nullptr) ++g.n_skip;
    else if (!eval && n < 2) why = "the FPS limit is not half the screen's refresh rate or less", ++g.n_cadence;
    else if (w < 64 || h < 64 || (!view.target && view.texture == nullptr)) why = "no picture of the top screen yet", ++g.n_skip;
    else if (!eval && g.pauses >= 3) why = "the game can't keep its frame rate with it: off until the game starts again", ++g.n_slow;
    else if (!eval && now < g.pause_until)
        why = g.pause_why != nullptr ? g.pause_why : "the game fell behind its frame rate with it: paused for 20 s", ++g.n_slow;
    {  // The game's own rate while pictures are made: when the time between its pictures stays 12 % over the limit's for
       // 3 s (a phone whose GPU can't draw the game and the made pictures both), none is made for 20 s, and after the
       // third time not again until the game starts again. A load's hitch alone is far from 3 s of it.
        const double dt = g.last_real.time_since_epoch().count() != 0
                              ? std::chrono::duration<double, std::milli>(now - g.last_real).count() : 0;
        g.last_real = now;
        const float cap = perfstats_fps_cap();
        if (!eval && why == nullptr && g.making && dt > 0 && dt < 1000 && cap > 0) {
            const double want = 1000.0 / cap;
            g.avg_ms = g.avg_ms > 0 ? g.avg_ms * 0.95 + dt * 0.05 : dt;
            g.slow = g.avg_ms > want * 1.12 ? g.slow + 1 : 0;
            if (g.slow * want >= 3000) {
                ++g.pauses, g.slow = 0, g.avg_ms = 0;
                g.pause_until = now + std::chrono::seconds(20);
                // (the screen's refresh rate changed just before: said so)
                const double since = g.rate_at.time_since_epoch().count() != 0
                                         ? std::chrono::duration<double>(now - g.rate_at).count() : 1e9;
                char after[96] = "";
                if (since < 10)
                    std::snprintf(after, sizeof after, ", %.1f s after the screen's refresh rate changed (%.0f -> %.0f Hz)",
                                  since, g.rate_from, g.rate_hz);
                std::fprintf(stderr, "[Info] App: frame generation: the game falls behind its %.0f FPS with the made "
                                     "pictures (%.1f ms a frame%s): %s\n", cap, dt, after,
                             g.pauses >= 3 ? "none made until the game starts again" : "none made for 20 s");
                g.pause_why = since < 10 ? "the game fell behind its frame rate with it, right after the screen's refresh "
                                           "rate changed: paused for 20 s"
                                         : "the game fell behind its frame rate with it: paused for 20 s";
                why = g.pauses >= 3 ? "the game can't keep its frame rate with it: off until the game starts again"
                                    : g.pause_why;
                ++g.n_slow;
            }
        } else if (why != nullptr || !g.making) {
            g.slow = 0, g.avg_ms = 0;
        }
    }
    if (why != nullptr) {
        g.have = 0, g.making = false;
        Tell(0, why);
        return nullptr;
    }
    SDL_FlushRenderer(renderer);
    if (!Setup()) {
        Tell(0, "not possible on this GPU");
        return nullptr;
    }
    if (!ImagesFor(renderer, w, h)) return nullptr;
    // 0. B upright into the history (the window's own pass for it, into an SDL render target)
    const int slot = (g.ring + 1) % 3;
    SDL_Texture* before = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, g.up[slot].sdl);
    shared_screens_draw_upright(view, w, h);
    SDL_SetRenderTarget(renderer, before);
    SDL_FlushRenderer(renderer);
    const Saved saved = SaveState();
    glDisable(GL_BLEND), glDisable(GL_SCISSOR_TEST), glDisable(GL_DEPTH_TEST), glDisable(GL_CULL_FACE);
    for (int i = 0; i < 3; ++i) glDisableVertexAttribArray(i);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    const int gap = eval ? 2 : 1;
    const bool make = g.have >= gap && (!eval || g.n_game - g.eval_frame >= static_cast<unsigned>(g.eval_every));
    const int qi = static_cast<int>(g.made_no % 8);
    if (make && g.timer) p_glBeginQuery(kTimeElapsed, g.q[qi]);
    // this frame's HUD boxes, in pixels of the picture, a little wider all round (the first level's mask)
    float boxes[12][4];
    int nb = 0;
    for (int i = 0; i < nhud && nb < 12; ++i) {
        boxes[nb][0] = hud[i].r[0] * w - 3, boxes[nb][1] = hud[i].r[1] * h - 3;
        boxes[nb][2] = hud[i].r[2] * w + 3, boxes[nb][3] = hud[i].r[3] * h + 3;
        ++nb;
    }
    {  // 1. B's levels: 1/4 from the picture, then 1/8, 1/16 and 1/32
        const bool lin[] = {true};
        glUseProgram(g.down.id);
        for (int k = 1; k <= 4; ++k) {
            const Tex* src[] = {k == 1 ? &g.up[slot] : &g.lv[slot][k - 1]};
            glUniform4f(g.down.u[U_A], 1.0f / src[0]->w, 1.0f / src[0]->h, static_cast<float>(g.lw[k]), static_cast<float>(g.lh[k]));
            glUniform4f(g.down.u[U_B], k == 1 ? 0.0f : 1.0f, k == 1 ? static_cast<float>(nb) : 0.0f, 0, 0);
            if (k == 1 && nb) glUniform4fv(g.down.u[U_HUD], nb, &boxes[0][0]);
            Pass(g.down, g.lv[slot][k], src, lin, 1);
        }
    }
    const int a = (slot + 3 - gap) % 3;
    if (make) {
        // 2-4. a full search at 1/32, refined at 1/16, 1/8 and 1/4, and the median
        const bool nn[] = {false, false, false};
        glUseProgram(g.search.id);
        glUniform4f(g.search.u[U_A], 1.0f / g.lw[4], 1.0f / g.lh[4], static_cast<float>(g.fw[4]), static_cast<float>(g.fh[4]));
        glUniform4f(g.search.u[U_B], 32.0f, 6.0f, 0, 0);
        const Tex* ss[] = {&g.lv[a][4], &g.lv[slot][4]};
        Pass(g.search, g.fd[4], ss, nn, 2);
        glUseProgram(g.refine.id);
        for (int k = 3; k >= 1; --k) {
            const float texel = static_cast<float>(4 << (k - 1)), block = k == 1 ? 4.0f : 2.0f;
            const float parent = k == 3 ? 64.0f : k == 2 ? 32.0f : 16.0f;
            glUniform4f(g.refine.u[U_A], 1.0f / g.lw[k], 1.0f / g.lh[k], static_cast<float>(g.fw[k]), static_cast<float>(g.fh[k]));
            glUniform4f(g.refine.u[U_B], texel, block, static_cast<float>(g.fw[k + 1]), static_cast<float>(g.fh[k + 1]));
            glUniform4f(g.refine.u[U_C], parent, 0.002f, k == 1 ? 1.0f : 0.0f, 0);
            const Tex* sr[] = {&g.lv[a][k], &g.lv[slot][k], &g.fd[k + 1]};
            Pass(g.refine, g.fd[k], sr, nn, 3);
        }
        glUseProgram(g.clean.id);
        glUniform4f(g.clean.u[U_A], static_cast<float>(g.fw[1]), static_cast<float>(g.fh[1]), 0, 0);
        const Tex* sm[] = {&g.fd[1]};
        Pass(g.clean, g.vm, sm, nn, 1);
        // 5. the made picture
        const float t = eval || n < 2 ? 0.5f : static_cast<float>(n / 2) / n;
        glUseProgram(g.compose.id);
        glUniform4f(g.compose.u[U_A], static_cast<float>(w), static_cast<float>(h), 1.0f / w, 1.0f / h);
        glUniform4f(g.compose.u[U_B], t, 1, 1.5f / 255.0f, 0);
        glUniform4f(g.compose.u[U_C], 1.0f / (16.0f * g.fw[1]), 1.0f / (16.0f * g.fh[1]), 0.10f, 0.25f);
        const Tex* sp[] = {&g.up[a], &g.up[slot], &g.vm, &g.lv[slot][1]};
        const bool lp[] = {true, true, true, false};
        Pass(g.compose, g.gen, sp, lp, 4);
    }
    if (make && g.timer) {
        p_glEndQuery(kTimeElapsed);
        g.qframe[qi] = ++g.made_no;
    } else if (make) {
        ++g.made_no;
    }
    if (make && eval) {  // (the earlier, the truth between, the later, the made one)
        SaveBmp(g.up[a], "a"), SaveBmp(g.up[(slot + 2) % 3], "t"), SaveBmp(g.up[slot], "b"), SaveBmp(g.gen, "g");
        ++g.eval_no;
        g.eval_frame = g.n_game;
        if (--g.eval_left == 0) std::fprintf(stderr, "[Info] App: frame generation: quality samples done\n");
    }
    RestoreState(saved);
    g.ring = slot;
    if (g.have < 2) ++g.have;
    if (!make || eval) {
        ++g.n_warm;
        return nullptr;
    }
    ++g.n_made;
    g.making = true;
    Tell(1, nullptr);
    *follow = n - n / 2;
    return g.gen.sdl;
}
