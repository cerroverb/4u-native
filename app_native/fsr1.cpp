// FSR 1 for the top screen (fsr1.h).
#include "fsr1.h"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "shared_screens.h"

namespace {

// AMD FidelityFX Super Resolution 1.0's ffx_fsr1.h (MIT), its 32-bit paths in GLSL ES 3.00, as the MHGU port's
// runtime/nvn/vulkan/shaders.c (fsr1_easu.frag, fsr1_rcas.frag) with the same numbers; the 12 taps read one by one
// (texelFetch: OpenGL ES 3.0 has no textureGather), clamped to the picture's edge.
constexpr const char* kVertex = R"(#version 300 es
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
constexpr const char* kEasu = R"(#version 300 es
precision highp float;
precision highp int;
uniform highp sampler2D t;
uniform vec4 u_con0;  // FsrEasuCon's first: the picture's size over the target's, and half of it less a half
out vec4 o;
float rcp_lo(float a) { return uintBitsToFloat(0x7ef07ebbu - floatBitsToUint(a)); }
float rsq_lo(float a) { return uintBitsToFloat(0x5f347d74u - (floatBitsToUint(a) >> 1u)); }
ivec2 hi;
vec3 at(ivec2 p) { return texelFetch(t, clamp(p, ivec2(0), hi), 0).rgb; }
float luma(vec3 c) { return c.b * 0.5 + (c.r * 0.5 + c.g); }
void tap(inout vec3 ac, inout float aw, vec2 off, vec2 dir, vec2 len, float lob, float clp, vec3 c) {
    vec2 v = vec2(off.x * dir.x + off.y * dir.y, off.x * -dir.y + off.y * dir.x) * len;
    float d2 = min(v.x * v.x + v.y * v.y, clp);
    float wb = 2.0 / 5.0 * d2 - 1.0, wa = lob * d2 - 1.0;
    wb *= wb;
    wa *= wa;
    wb = 25.0 / 16.0 * wb - (25.0 / 16.0 - 1.0);
    float w = wb * wa;
    ac += c * w;
    aw += w;
}
void set(inout vec2 dir, inout float len, float w, float la, float lb, float lc, float ld, float le) {
    float dc = ld - lc, cb = lc - lb;
    float lenx = rcp_lo(max(abs(dc), abs(cb)));
    float dirx = ld - lb;
    dir.x += dirx * w;
    lenx = clamp(abs(dirx) * lenx, 0.0, 1.0);
    lenx *= lenx;
    len += lenx * w;
    float ec = le - lc, ca = lc - la;
    float leny = rcp_lo(max(abs(ec), abs(ca)));
    float diry = le - la;
    dir.y += diry * w;
    leny = clamp(abs(diry) * leny, 0.0, 1.0);
    leny *= leny;
    len += leny * w;
}
void main() {
    hi = textureSize(t, 0) - 1;
    vec2 pp = floor(gl_FragCoord.xy) * u_con0.xy + u_con0.zw;
    vec2 fp = floor(pp);
    pp -= fp;
    ivec2 f0 = ivec2(fp);
    //    b c
    //  e f g h
    //  i j k l
    //    n o
    vec3 b = at(f0 + ivec2(0, -1)), c = at(f0 + ivec2(1, -1));
    vec3 e = at(f0 + ivec2(-1, 0)), f = at(f0), g = at(f0 + ivec2(1, 0)), h = at(f0 + ivec2(2, 0));
    vec3 i = at(f0 + ivec2(-1, 1)), j = at(f0 + ivec2(0, 1)), k = at(f0 + ivec2(1, 1)), l = at(f0 + ivec2(2, 1));
    vec3 n = at(f0 + ivec2(0, 2)), q = at(f0 + ivec2(1, 2));
    float bL = luma(b), cL = luma(c), eL = luma(e), fL = luma(f), gL = luma(g), hL = luma(h);
    float iL = luma(i), jL = luma(j), kL = luma(k), lL = luma(l), nL = luma(n), oL = luma(q);
    vec2 dir = vec2(0.0);
    float len = 0.0;
    set(dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);
    set(dir, len, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL);
    set(dir, len, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL);
    set(dir, len, pp.x * pp.y, gL, jL, kL, lL, oL);
    vec2 dir2 = dir * dir;
    float dirr = dir2.x + dir2.y;
    bool zro = dirr < 1.0 / 32768.0;
    dirr = rsq_lo(dirr);
    dirr = zro ? 1.0 : dirr;
    dir.x = zro ? 1.0 : dir.x;
    dir *= vec2(dirr);
    len = len * 0.5;
    len *= len;
    float stretch = (dir.x * dir.x + dir.y * dir.y) * rcp_lo(max(abs(dir.x), abs(dir.y)));
    vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
    float clp = rcp_lo(lob);
    vec3 min4 = min(min(f, g), min(j, k));
    vec3 max4 = max(max(f, g), max(j, k));
    vec3 ac = vec3(0.0);
    float aw = 0.0;
    tap(ac, aw, vec2(0.0, -1.0) - pp, dir, len2, lob, clp, b);
    tap(ac, aw, vec2(1.0, -1.0) - pp, dir, len2, lob, clp, c);
    tap(ac, aw, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, i);
    tap(ac, aw, vec2(0.0, 1.0) - pp, dir, len2, lob, clp, j);
    tap(ac, aw, vec2(0.0, 0.0) - pp, dir, len2, lob, clp, f);
    tap(ac, aw, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, e);
    tap(ac, aw, vec2(1.0, 1.0) - pp, dir, len2, lob, clp, k);
    tap(ac, aw, vec2(2.0, 1.0) - pp, dir, len2, lob, clp, l);
    tap(ac, aw, vec2(2.0, 0.0) - pp, dir, len2, lob, clp, h);
    tap(ac, aw, vec2(1.0, 0.0) - pp, dir, len2, lob, clp, g);
    tap(ac, aw, vec2(1.0, 2.0) - pp, dir, len2, lob, clp, q);
    tap(ac, aw, vec2(0.0, 2.0) - pp, dir, len2, lob, clp, n);
    o = vec4(min(max4, max(min4, ac * (1.0 / aw))), 1.0);
}
)";
constexpr const char* kRcas = R"(#version 300 es
precision highp float;
precision highp int;
uniform highp sampler2D t;
uniform float u_sharp;  // FsrRcasCon's: exp2(-stops)
out vec4 o;
#define FSR_RCAS_LIMIT (0.25 - (1.0 / 16.0))
float rcp_med(float a) { float b = uintBitsToFloat(0x7ef19fffu - floatBitsToUint(a)); return b * (-b * a + 2.0); }
void main() {
    ivec2 sp = ivec2(gl_FragCoord.xy), hi = textureSize(t, 0) - 1;
    vec3 b = texelFetch(t, clamp(sp + ivec2(0, -1), ivec2(0), hi), 0).rgb;
    vec3 d = texelFetch(t, clamp(sp + ivec2(-1, 0), ivec2(0), hi), 0).rgb;
    vec3 e = texelFetch(t, sp, 0).rgb;
    vec3 f = texelFetch(t, clamp(sp + ivec2(1, 0), ivec2(0), hi), 0).rgb;
    vec3 h = texelFetch(t, clamp(sp + ivec2(0, 1), ivec2(0), hi), 0).rgb;
    vec3 mn4 = min(min(min(b, d), f), h);
    vec3 mx4 = max(max(max(b, d), f), h);
    vec3 hitmin = min(mn4, e) / max(4.0 * mx4, vec3(1e-6));
    vec3 hitmax = (1.0 - max(mx4, e)) / min(4.0 * mn4 - 4.0, vec3(-1e-6));
    vec3 lobe3 = max(-hitmin, hitmax);
    float lobe = max(-FSR_RCAS_LIMIT, min(max(max(lobe3.r, lobe3.g), lobe3.b), 0.0)) * u_sharp;
    float rcpl = rcp_med(4.0 * lobe + 1.0);
    o = vec4((lobe * b + lobe * d + lobe * h + lobe * f + e) * rcpl, 1.0);
}
)";

struct Tex {
    SDL_Texture* sdl = nullptr;
    GLuint name = 0;
    int w = 0, h = 0;
};
struct State {
    bool on = false;
    float sharpness = 0.8f;
    int tried = 0;  // 1 the programs are made, -1 they can't be (said once)
    GLuint easu = 0, rcas = 0, fbo = 0;
    GLint easu_con0 = -1, easu_t = -1, rcas_sharp = -1, rcas_t = -1;
    Tex up, mid, out;
    unsigned drawn = 0;
} g;

GLuint Compile(GLenum kind, const char* text) {
    GLuint s = glCreateShader(kind);
    glShaderSource(s, 1, &text, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        std::fprintf(stderr, "[Warning] App: FSR 1's shader: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}
GLuint Link(const char* fragment) {
    const GLuint vs = Compile(GL_VERTEX_SHADER, kVertex), fs = Compile(GL_FRAGMENT_SHADER, fragment);
    GLuint p = 0;
    if (vs && fs) {
        p = glCreateProgram();
        glAttachShader(p, vs);
        glAttachShader(p, fs);
        glLinkProgram(p);
        GLint ok = 0;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024] = {};
            glGetProgramInfoLog(p, sizeof log, nullptr, log);
            std::fprintf(stderr, "[Warning] App: FSR 1's program: %s\n", log);
            glDeleteProgram(p);
            p = 0;
        }
    }
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    return p;
}
bool Setup() {
    if (g.tried) return g.tried > 0;
    g.tried = -1;
    const char* ver = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    int major = 0, minor = 0;
    if (ver == nullptr || std::sscanf(ver, "OpenGL ES %d.%d", &major, &minor) != 2 || major < 3) {
        std::fprintf(stderr, "[Info] App: FSR 1 needs OpenGL ES 3.0 (this context: %s): the top screen is scaled as before\n",
                     ver ? ver : "?");
        return false;
    }
    g.easu = Link(kEasu);
    g.rcas = Link(kRcas);
    if (!g.easu || !g.rcas) {
        std::fprintf(stderr, "[Info] App: FSR 1's shaders could not be made: the top screen is scaled as before\n");
        return false;
    }
    g.easu_con0 = glGetUniformLocation(g.easu, "u_con0"), g.easu_t = glGetUniformLocation(g.easu, "t");
    g.rcas_sharp = glGetUniformLocation(g.rcas, "u_sharp"), g.rcas_t = glGetUniformLocation(g.rcas, "t");
    glGenFramebuffers(1, &g.fbo);
    g.tried = 1;
    std::fprintf(stderr, "[Info] App: FSR 1 ready (%s)\n", ver);
    return true;
}
void Free(Tex& t) {
    if (t.sdl) SDL_DestroyTexture(t.sdl);
    t = Tex{};
}
// An SDL render target of w x h (its GL texture: the passes' input and output)
bool Sized(SDL_Renderer* r, Tex& t, int w, int h) {
    if (t.sdl && t.w == w && t.h == h) return true;
    Free(t);
    t.sdl = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
    if (!t.sdl) return false;
    SDL_SetTextureScaleMode(t.sdl, SDL_SCALEMODE_LINEAR);
    t.name = static_cast<GLuint>(SDL_GetNumberProperty(SDL_GetTextureProperties(t.sdl), SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER, 0));
    t.w = w, t.h = h;
    return t.name != 0;
}
// One pass: `program` from `src` into `dst`, the whole of it
void Pass(GLuint program, GLint sampler, const Tex& src, const Tex& dst) {
    glBindFramebuffer(GL_FRAMEBUFFER, g.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst.name, 0);
    glViewport(0, 0, dst.w, dst.h);
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src.name);
    glUniform1i(sampler, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

}  // namespace

void fsr1_set(bool on, float sharpness) {
    if (on != g.on) std::fprintf(stderr, "[Info] App: FSR 1 %s\n", on ? "on (the top screen, when it is shown larger than its picture)" : "off");
    g.on = on;
    g.sharpness = std::clamp(sharpness, 0.0f, 1.0f);
}

bool fsr1_draw(SDL_Renderer* renderer, SharedScreenView& view, const SDL_FRect& dst, int win_w, int win_h) {
    if (!g.on || g.tried < 0 || (view.texture == nullptr && !view.target)) return false;
    const int pw = view.width, ph = view.height;
    const int dw = static_cast<int>(std::lround(dst.w)), dh = static_cast<int>(std::lround(dst.h));
    // (only scaled up: shown at its size or smaller, or a size no GPU target takes, it is drawn as before)
    if (pw <= 0 || ph <= 0 || (dw <= pw && dh <= ph) || dw > 8192 || dh > 8192) return false;
    SDL_FlushRenderer(renderer);
    if (!Setup()) return false;
    if (!Sized(renderer, g.up, pw, ph) || !Sized(renderer, g.mid, dw, dh) || !Sized(renderer, g.out, dw, dh)) {
        std::fprintf(stderr, "[Warning] App: FSR 1's pictures (%dx%d to %dx%d) could not be made: scaled as before\n", pw, ph, dw, dh);
        Free(g.up), Free(g.mid), Free(g.out);
        g.tried = -1;
        return false;
    }
    // 0. the picture upright, at its own size (the window's own pass for it, into an SDL render target)
    SDL_Texture* before = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, g.up.sdl);
    shared_screens_draw_upright(view, pw, ph);
    SDL_SetRenderTarget(renderer, before);
    SDL_FlushRenderer(renderer);
    // (the GL state SDL's renderer leaves, put back after the passes: SDL keeps its own copy of it)
    GLint fbo = 0, prog = 0, active = 0, vp[4] = {}, tex0 = 0, buf = 0, attr[3] = {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buf);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex0);
    for (int i = 0; i < 3; ++i) glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &attr[i]);
    const GLboolean blend = glIsEnabled(GL_BLEND), scissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_BLEND), glDisable(GL_SCISSOR_TEST);
    for (int i = 0; i < 3; ++i) glDisableVertexAttribArray(i);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    // 1. EASU: the picture scaled up to the size it is shown at; 2. RCAS: that sharpened
    glUseProgram(g.easu);
    glUniform4f(g.easu_con0, static_cast<float>(pw) / dw, static_cast<float>(ph) / dh, 0.5f * pw / dw - 0.5f,
                0.5f * ph / dh - 0.5f);
    Pass(g.easu, g.easu_t, g.up, g.mid);
    glUseProgram(g.rcas);
    glUniform1f(g.rcas_sharp, std::exp2(-(2.0f - 2.0f * g.sharpness)));
    Pass(g.rcas, g.rcas_t, g.mid, g.out);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex0);
    glActiveTexture(active);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glUseProgram(prog);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glBindBuffer(GL_ARRAY_BUFFER, buf);
    for (int i = 0; i < 3; ++i) attr[i] ? glEnableVertexAttribArray(i) : glDisableVertexAttribArray(i);
    blend ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
    scissor ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);
    // 3. drawn as a screen SDL would draw, at its size: the colour filter goes on it as on the game's own
    SharedScreenView shown;
    shown.texture = g.out.sdl;
    shown.top = true;
    shown.source = SDL_FRect{0, 0, static_cast<float>(dw), static_cast<float>(dh)};
    shown.width = dw, shown.height = dh;
    shared_screens_draw(shown, dst, win_w, win_h);
    if (g.drawn++ == 0) std::fprintf(stderr, "[Info] App: FSR 1: the top screen %dx%d shown at %dx%d\n", pw, ph, dw, dh);
    return true;
}

void fsr1_reset() {
    g.up = g.mid = g.out = Tex{};  // (the textures went with the context: SDL makes its own again)
    g.easu = g.rcas = g.fbo = 0;
    g.tried = 0;
}
