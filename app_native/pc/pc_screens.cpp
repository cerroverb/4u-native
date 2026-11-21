// MH4U for PC: the game's screens in the window (shared_screens.h, pc_screens.h), with SDL's OpenGL renderer.
//
// Android's shared_screens.cpp, for Windows: the renderer's buffers are Vulkan allocations exported as NT handles
// (mh4n present.h Win32SharedMemory) where Android has AHardwareBuffers; each one is imported once into GL
// (GL_EXT_memory_object_win32: a memory object and an immutable GL texture of ours over it, glTexStorageMem2DEXT; an SDL
// texture's own storage redefined onto it showed black now and then on AMD's driver). The programs below draw it (with
// the colour filter at 0 they are the identity); the view's SDL texture is a 1x1 stand-in that only says a picture is
// there (frame_gen.cpp and the main loop look at it). A picture is shown once the
// renderer's GPU has finished it (SharedPicture::Ready: Windows has no sync files, the main loop looks again each pass,
// pc_screens_poll), and let go, so that the renderer can draw into its buffer again, once the GL work that read it has
// finished (a GL fence). The pixels' form (the renderer reads the screens back) is the fallback: an SDL texture of the
// view's own, as before. The renderer's render targets themselves are not shared on PC: the app sets
// MH4N_TARGET_SCREENS=0 (the renderer draws the top screen upright, as the touch screen; a copy that costs a desktop GPU
// next to nothing).
//
// The colour filter, PS1 style and the sharp pixels (Brick, and the PC's picture filter) are the Android file's programs,
// byte for byte (the same source, GLSL ES 1.00 run through the desktop GL's ES compatibility: "#version 100" first): change
// both. Raw GL leaves SDL's state as SDL expects it: its queue goes first (SDL_FlushRenderer, which also makes SDL set all
// of its state again before its next draw), and the generic vertex array, the buffer and the texture binding are put back.
#include "pc_screens.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <string>

#include "colour_filter.h"
#include "mh4n/present.h"
#include "pc_gl.h"

namespace {

using mh4n::present::SharedPicture;

SDL_Renderer* g_renderer = nullptr;   // the window's
SDL_Window* g_window = nullptr;
SDL_GLContext g_context = nullptr;
SDL_Renderer* g_second = nullptr;     // the touch screen's own window (pc_screens_set_second)
SDL_Window* g_second_window = nullptr;
SDL_GLContext g_second_context = nullptr;
bool g_gl = false;          // SDL's OpenGL renderer, the functions loaded
bool g_available = false;   // the renderer's buffers can be imported here
bool g_refused = false;     // ... and were refused: the screens come as pixels
bool g_uuid_checked = false;
unsigned g_refusals = 0;
int g_upscale = 0;          // 0 smooth, 1 sharp pixels, 2 nearest

struct Import {
    GLuint memory = 0;
    GLuint name = 0;  // our texture over it
    int width = 0, height = 0;
    std::uint64_t last_used = 0;
    std::weak_ptr<const SharedPicture> shown;
};
std::map<std::uint64_t, Import> g_imports;  // by SharedPicture::BufferId
SDL_Texture* g_stand_in = nullptr;          // (a shared picture's view.texture: 1x1, never drawn)
std::uint64_t g_frame = 0;

// ---- the programs (shared_screens.cpp's: the same source) ----
ColourAdjust g_colour{};
bool g_colour_on = false;
ColourMatrix g_colour_matrix{};
struct TextureProgram {
    GLuint program = 0;
    GLint u_tex = -1, u_rect = -1, u_uv = -1, u_alpha = -1;
    GLint u_f0 = -1, u_f1 = -1, u_f2 = -1, u_fg = -1;
};
TextureProgram g_texture_program;
bool g_texture_program_tried = false;
int g_ps1_mode = 0;          // 0 off, 1 sharp pixels, 2 the PS1 filter
unsigned g_ps1_frames = 0;
struct Ps1TextureProgram {
    GLuint program = 0;
    GLint u_tex = -1, u_rect = -1, u_uv = -1, u_alpha = -1, u_ps1 = -1, u_ps1b = -1, u_ps1f = -1;
    GLint u_f0 = -1, u_f1 = -1, u_f2 = -1, u_fg = -1;
};
Ps1TextureProgram g_ps1_texture_programs[2][2];  // [mode - 1][the colour filter]
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
constexpr const char* kFilterCode = R"(uniform vec4 u_f0, u_f1, u_f2;
uniform float u_fg;
vec3 Filter(vec3 c) {
    vec4 p = vec4(pow(max(c, vec3(0.0)), vec3(u_fg)), 1.0);
    return clamp(vec3(dot(u_f0, p), dot(u_f1, p), dot(u_f2, p)), 0.0, 1.0);
}
)";
constexpr const char* kTextureVertex = R"(#version 100
attribute vec2 a_corner;
uniform vec4 u_rect;  // the destination's corners in clip space: x0, y0, x1, y1
uniform vec4 u_uv;    // the texture's coordinates there: u0, v0, u1, v1
varying vec2 v_uv;
void main() {
    gl_Position = vec4(mix(u_rect.x, u_rect.z, a_corner.x), mix(u_rect.y, u_rect.w, a_corner.y), 0.0, 1.0);
    v_uv = vec2(mix(u_uv.x, u_uv.z, a_corner.x), mix(u_uv.y, u_uv.w, a_corner.y));
}
)";
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
constexpr const char* kVersion = "#version 100";  // (the programs' first line: GLSL ES 1.00, as on Android)
GLuint g_corners = 0;  // a buffer of the quad's corners (0, 0), (1, 0), (0, 1), (1, 1)

GLuint CompileShader(GLenum kind, const char* text) {
    GLuint shader = glCreateShader(kind);
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[Error] App: a screen's shader: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

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

void SetFilterUniforms(GLint f0, GLint f1, GLint f2, GLint fg) {
    glUniform4fv(f0, 1, g_colour_matrix.row[0]);
    glUniform4fv(f1, 1, g_colour_matrix.row[1]);
    glUniform4fv(f2, 1, g_colour_matrix.row[2]);
    glUniform1f(fg, g_colour_matrix.gamma);
}

// The renderer `r`'s GL context current, its queued SDL work done first
void Current(SDL_Renderer* r) {
    SDL_FlushRenderer(r);
    SDL_Window* w = r == g_second ? g_second_window : g_window;
    SDL_GLContext c = r == g_second ? g_second_context : g_context;
    if (c != nullptr && SDL_GL_GetCurrentContext() != c) SDL_GL_MakeCurrent(w, c);
}

bool EnsureCorners() {
    if (g_corners != 0) return true;
    GLint buffer = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
    const float corners[] = {0, 0, 1, 0, 0, 1, 1, 1};
    glGenBuffers(1, &g_corners);
    glBindBuffer(GL_ARRAY_BUFFER, g_corners);
    glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(buffer));
    return g_corners != 0;
}

bool EnsureTextureProgram() {
    if (g_texture_program_tried) return g_texture_program.program != 0;
    g_texture_program_tried = true;
    if (!g_gl) return false;
    Current(g_renderer);
    if (!EnsureCorners()) return false;
    char fragment[2048];
    std::snprintf(fragment, sizeof(fragment), kTextureFragment, kVersion, "sampler2D", kFilterCode);
    TextureProgram& p = g_texture_program;
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
    glUseProgram(0);
    std::fprintf(stderr, "[Info] App: the colour filter's program: %s\n", p.program != 0 ? "yes" : "no");
    return p.program != 0;
}

Ps1TextureProgram MakePs1TextureProgram(bool filter, int mode) {
    Ps1TextureProgram p;
    char fragment[8192];
    std::string code = std::string(filter ? kFilterCode : "") + kPs1SharpCode + (mode == 2 ? kPs1Code : "");
    std::string last = std::string(filter ? "c = Filter(c);\n    " : "") + (mode == 2 ? "c = Ps1(c, cell);\n    " : "");
    const char* at = mode == 2 ? "vec2 at = u_ps1b.xy + (floor(cell) + 0.5) * u_ps1.z;\n    "
                               : "vec2 at = vec2(Ps1Sharp(texel.x, u_ps1b.z), Ps1Sharp(texel.y, u_ps1b.w));\n    ";
    std::snprintf(fragment, sizeof(fragment), kPs1TextureFragment, kVersion, "sampler2D", code.c_str(), at, last.c_str());
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

bool EnsurePs1Programs() {
    if (g_ps1_programs_tried) return g_ps1_texture_programs[0][0].program != 0;
    g_ps1_programs_tried = true;
    if (!g_gl) return false;
    Current(g_renderer);
    if (!EnsureCorners()) return false;
    int made = 0;
    for (int mode = 1; mode <= 2; ++mode)
        for (int filter = 0; filter < 2; ++filter) {
            g_ps1_texture_programs[mode - 1][filter] = MakePs1TextureProgram(filter != 0, mode);
            made += g_ps1_texture_programs[mode - 1][filter].program != 0;
        }
    glUseProgram(0);
    std::fprintf(stderr, "[Info] App: PS1 style's programs: %d of 4 made\n", made);
    return g_ps1_texture_programs[0][0].program != 0;
}

// Pictures no longer shown, held until the GL work queued before `fence` (the work that read them) is done.
struct Retiring {
    std::shared_ptr<const SharedPicture> picture;
    GLsync fence = nullptr;
};
std::deque<Retiring> g_retiring;

void Refusal(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void Refusal(const char* fmt, ...) {
    if (++g_refusals > 3) return;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void DropImport(Import& im) {
    if (im.name != 0 || im.memory != 0) Current(g_renderer);
    if (im.name != 0) glDeleteTextures(1, &im.name);
    if (im.memory != 0) glDeleteMemoryObjectsEXT(1, &im.memory);
    im = Import{};
}

// The renderer's GPU and driver are this GL context's (the first picture's UUIDs against GL's)
bool SameDevice(const mh4n::present::Win32SharedMemory& m) {
    if (glGetUnsignedBytei_vEXT == nullptr || glGetUnsignedBytevEXT == nullptr) return true;  // (can't tell: tried)
    GLubyte device[16] = {}, driver[16] = {};
    while (glGetError() != GL_NO_ERROR) {
    }
    glGetUnsignedBytei_vEXT(GL_DEVICE_UUID_EXT, 0, device);
    glGetUnsignedBytevEXT(GL_DRIVER_UUID_EXT, driver);
    if (glGetError() != GL_NO_ERROR) return true;
    const bool same = !std::memcmp(device, m.device_uuid, 16) && !std::memcmp(driver, m.driver_uuid, 16);
    std::fprintf(stderr, "[Info] App: the renderer's GPU and the window's OpenGL: %s\n",
                 same ? "the same device and driver" : "not the same (another GPU or driver): the screens come as pixels");
    return same;
}

Import* ImportOf(const SharedPicture& picture) {
    const std::uint64_t id = picture.BufferId();
    auto it = g_imports.find(id);
    if (it != g_imports.end()) {
        it->second.last_used = g_frame;
        return &it->second;
    }
    const auto* m = static_cast<const mh4n::present::Win32SharedMemory*>(picture.HardwareBuffer());
    if (m == nullptr || m->handle == nullptr || m->width == 0 || m->height == 0) {
        Refusal("[Error] App: a screen buffer without its handle\n");
        return nullptr;
    }
    Current(g_renderer);
    if (!g_uuid_checked) {
        g_uuid_checked = true;
        if (!SameDevice(*m)) {
            g_available = false;
            return nullptr;
        }
    }
    Import im;
    im.width = static_cast<int>(m->width);
    im.height = static_cast<int>(m->height);
    if (g_stand_in == nullptr) g_stand_in = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 1, 1);
    Current(g_renderer);
    while (glGetError() != GL_NO_ERROR) {
    }
    GLenum error = GL_INVALID_OPERATION;
    if (g_stand_in != nullptr) {
        glCreateMemoryObjectsEXT(1, &im.memory);
        const GLint dedicated = m->dedicated ? GL_TRUE : GL_FALSE;
        glMemoryObjectParameterivEXT(im.memory, GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
        glImportMemoryWin32HandleEXT(im.memory, m->size, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, m->handle);
        GLint previous = 0, active = 0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
        glGenTextures(1, &im.name);
        glBindTexture(GL_TEXTURE_2D, im.name);
        // (the tiling the renderer made it with: linear where it can)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_TILING_EXT, m->linear ? GL_LINEAR_TILING_EXT : GL_OPTIMAL_TILING_EXT);
        glTexStorageMem2DEXT(GL_TEXTURE_2D, 1, GL_RGBA8, im.width, im.height, im.memory, 0);
        error = glGetError();
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
        glActiveTexture(static_cast<GLenum>(active));
    }
    if (error != GL_NO_ERROR) {
        Refusal("[Error] App: a screen buffer's import into GL failed (0x%x, %dx%d, %llu bytes)\n", error, im.width,
                im.height, static_cast<unsigned long long>(m->size));
        DropImport(im);
        return nullptr;
    }
    if (g_imports.empty())
        std::fprintf(stderr, "[Info] App: the screens are shown from the renderer's buffers (no copy; %dx%d first)\n", im.width,
                     im.height);
    im.last_used = g_frame;
    return &g_imports.emplace(id, im).first->second;
}

// `picture` is no longer shown: let go of it once the GL work queued so far (every frame that read it) is done.
void Retire(std::shared_ptr<const SharedPicture> picture) {
    if (picture == nullptr) return;
    Current(g_renderer);
    GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (fence == nullptr) {
        glFinish();
        return;
    }
    glFlush();  // (so that the fence signals however little is queued after it)
    g_retiring.push_back(Retiring{std::move(picture), fence});
}

void RefuseShared() {
    if (g_refused) return;
    g_refused = true;
    mh4n::present::SetSharedPicturesRefused(true);
    std::fprintf(stderr, "[Warning] App: the renderer's buffers can't be shown here: the screens come as pixels from now on\n");
}

void FreeCpuTexture(SharedScreenView& view) {
    if (view.cpu_texture != nullptr) SDL_DestroyTexture(view.cpu_texture);
    view.cpu_texture = nullptr;
    view.cpu_width = view.cpu_height = 0;
    view.uploaded.reset();
}

void DropPending(SharedScreenView& view, int end) {
    end = std::min(end, view.pending_count);
    for (int i = end; i < view.pending_count; ++i) {
        view.pending[i - end] = std::move(view.pending[i]);
        view.pending_region[i - end] = view.pending_region[i];
    }
    for (int i = view.pending_count - end; i < view.pending_count; ++i) view.pending[i].reset();
    view.pending_count -= end;
}

void NotePending(SharedScreenView& view, const std::shared_ptr<const SharedPicture>& picture,
                 const mh4n::present::ImageRegion& region) {
    for (int i = 0; i < view.pending_count; ++i)
        if (view.pending[i] == picture) return;
    if (view.pending_count == SharedScreenView::kPendingMax) {  // (the oldest is the next one done: the second goes)
        for (int i = 2; i < view.pending_count; ++i) {
            view.pending[i - 1] = std::move(view.pending[i]);
            view.pending_region[i - 1] = view.pending_region[i];
        }
        --view.pending_count;
        view.pending[view.pending_count].reset();
    }
    const int i = view.pending_count++;
    view.pending[i] = picture;
    view.pending_region[i] = region;
}

bool Show(SharedScreenView& view, const std::shared_ptr<const SharedPicture>& picture,
          const mh4n::present::ImageRegion& region, bool newest);

// The newest pending picture that is finished, shown (with the older ones dropped)
bool ShowFinishedPending(SharedScreenView& view) {
    for (int i = view.pending_count - 1; i >= 0; --i) {
        if (view.pending[i]->Ready()) {
            std::shared_ptr<const SharedPicture> done = view.pending[i];
            const mh4n::present::ImageRegion region = view.pending_region[i];
            DropPending(view, i + 1);
            return Show(view, done, region, false);
        }
    }
    return false;
}

bool Show(SharedScreenView& view, const std::shared_ptr<const SharedPicture>& picture,
          const mh4n::present::ImageRegion& region, bool newest) {
    if (!g_available || picture == nullptr) return false;
    if (picture == view.picture) return true;
    if (picture->Target() != nullptr) {  // (not asked for on PC: MH4N_TARGET_SCREENS=0)
        RefuseShared();
        return false;
    }
    if (!picture->Ready()) {
        NotePending(view, picture, region);
        return ShowFinishedPending(view);
    }
    if (newest) DropPending(view, view.pending_count);
    Import* import = ImportOf(*picture);
    if (import == nullptr || import->name == 0) {
        if (!view.shown_shared) RefuseShared();
        return false;
    }
    Retire(std::move(view.picture));
    import->shown = picture;
    view.picture = picture;
    view.shown_shared = true;
    view.texture = g_stand_in;
    view.gl_name = import->name;
    view.gl_target = GL_TEXTURE_2D;
    view.buffer_width = import->width;
    view.buffer_height = import->height;
    view.target = false;
    view.source = SDL_FRect{static_cast<float>(region.x), static_cast<float>(region.y), static_cast<float>(region.width),
                            static_cast<float>(region.height)};
    view.width = static_cast<int>(region.width);
    view.height = static_cast<int>(region.height);
    FreeCpuTexture(view);
    return true;
}

// A view's texture (an import or the pixels' own) drawn with a program of ours into `dst` of what `r` has bound
// (`out_w` x `out_h`: the window, y down, or with `into_texture` an SDL render target, y up): the colour filter (the
// texture program: with the filter off it is the identity), or PS1 style's mode (1 the sharp pixels, 2 the PS1 filter).
// False: not drawn (no program, or no GL name), the caller draws it with SDL.
bool DrawTexture(SDL_Renderer* r, const SharedScreenView& view, const SDL_FRect& dst, int out_w, int out_h, Uint8 alpha,
                 int ps1, float phase, bool into_texture = false, bool nearest = false) {
    if (view.texture == nullptr || dst.w <= 0 || dst.h <= 0 || out_w <= 0 || out_h <= 0 || view.source.w <= 0 ||
        view.source.h <= 0)
        return false;
    const bool colour = g_colour_on;
    if (ps1 != 0 ? !EnsurePs1Programs() : !EnsureTextureProgram()) return false;
    GLuint name = view.gl_name;
    float tw = static_cast<float>(view.buffer_width), th = static_cast<float>(view.buffer_height);
    if (name == 0) {
        const SDL_PropertiesID props = SDL_GetTextureProperties(view.texture);
        name = static_cast<GLuint>(SDL_GetNumberProperty(props, SDL_PROP_TEXTURE_OPENGL_TEXTURE_NUMBER, 0));
        if (name == 0 || !SDL_GetTextureSize(view.texture, &tw, &th)) return false;
    }
    if (tw <= 0 || th <= 0) return false;
    const Ps1TextureProgram* p1 = ps1 != 0 ? &g_ps1_texture_programs[ps1 - 1][colour ? 1 : 0] : nullptr;
    if (p1 != nullptr && p1->program == 0) return false;
    const TextureProgram& p0 = g_texture_program;
    const GLuint program = p1 != nullptr ? p1->program : p0.program;
    if (program == 0) return false;
    SDL_BlendMode blend = view.gl_name != 0 ? (alpha < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE) : SDL_BLENDMODE_NONE;
    if (view.gl_name == 0) SDL_GetTextureBlendMode(view.texture, &blend);
    Current(r);
    glUseProgram(program);
    glViewport(0, 0, out_w, out_h);
    glDisable(GL_SCISSOR_TEST);
    if (blend == SDL_BLENDMODE_BLEND) {
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquation(GL_FUNC_ADD);
    } else {
        glDisable(GL_BLEND);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, name);
    const GLint filter = nearest ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const float x0 = 2.0f * dst.x / out_w - 1.0f, x1 = 2.0f * (dst.x + dst.w) / out_w - 1.0f;
    const float y0 = into_texture ? 2.0f * dst.y / out_h - 1.0f : 1.0f - 2.0f * dst.y / out_h;
    const float y1 = into_texture ? 2.0f * (dst.y + dst.h) / out_h - 1.0f : 1.0f - 2.0f * (dst.y + dst.h) / out_h;
    const float u0 = view.source.x / tw, v0 = view.source.y / th;
    const float u1 = (view.source.x + view.source.w) / tw, v1 = (view.source.y + view.source.h) / th;
    if (p1 != nullptr) {
        glUniform1i(p1->u_tex, 0);
        glUniform4f(p1->u_rect, x0, y0, x1, y1);
        glUniform4f(p1->u_uv, u0, v0, u1, v1);
        glUniform1f(p1->u_alpha, alpha / 255.0f);
        glUniform4f(p1->u_ps1, tw, th, view.source.h / 240.0f, 0.0f);
        glUniform4f(p1->u_ps1b, view.source.x, view.source.y, dst.w / view.source.w, dst.h / view.source.h);
        if (ps1 == 2) glUniform1f(p1->u_ps1f, phase);
        if (colour) SetFilterUniforms(p1->u_f0, p1->u_f1, p1->u_f2, p1->u_fg);
    } else {
        glUniform1i(p0.u_tex, 0);
        glUniform4f(p0.u_rect, x0, y0, x1, y1);
        glUniform4f(p0.u_uv, u0, v0, u1, v1);
        glUniform1f(p0.u_alpha, alpha / 255.0f);
        if (colour) {
            SetFilterUniforms(p0.u_f0, p0.u_f1, p0.u_f2, p0.u_fg);
        } else {  // (the identity: the second window draws a screen this way with the filter off)
            static const float id[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
            glUniform4fv(p0.u_f0, 1, id[0]);
            glUniform4fv(p0.u_f1, 1, id[1]);
            glUniform4fv(p0.u_f2, 1, id[2]);
            glUniform1f(p0.u_fg, 1.0f);
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, g_corners);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    return true;
}

}  // namespace

bool shared_screens_init(SDL_Renderer* renderer) {
    g_renderer = renderer;
    g_window = SDL_GetRenderWindow(renderer);
    g_available = g_refused = g_uuid_checked = false;
    g_refusals = 0;
    mh4n::present::SetSharedPicturesRefused(false);
    const char* name = SDL_GetRendererName(renderer);
    g_gl = name != nullptr && !std::strcmp(name, "opengl");
    if (!g_gl) {
        std::fprintf(stderr, "[Info] App: the renderer is %s: the screens come as pixels, without the colour filter or PS1 style\n",
                     name != nullptr ? name : "?");
        return false;
    }
    SDL_FlushRenderer(renderer);
    g_context = SDL_GL_GetCurrentContext();
    g_gl = pc_gl_load();
    const char* cpu = std::getenv("MH4U_PRESENT_CPU");
    const bool cpu_forced = cpu != nullptr && *cpu == '1';
    g_available = g_gl && pc_gl_memory_objects() && !cpu_forced;
    std::fprintf(stderr, "[Info] App: shared screens %s\n",
                 g_available ? "possible (GL_EXT_memory_object_win32)"
                 : !g_gl     ? "impossible (OpenGL functions missing): the screens come as pixels"
                 : cpu_forced ? "off (MH4U_PRESENT_CPU=1): the screens come as pixels"
                              : "impossible (no GL_EXT_memory_object_win32): the screens come as pixels");
    return g_available;
}

bool shared_screens_available() { return g_available; }
bool shared_screens_valve_notice_take() { return false; }
bool pc_screens_shared_possible() { return g_available && !g_refused; }

bool shared_screens_show(SharedScreenView& view, const std::shared_ptr<const SharedPicture>& picture,
                         const mh4n::present::ImageRegion& region) {
    return Show(view, picture, region, true);
}

bool pc_screens_poll(SharedScreenView& view) { return view.pending_count > 0 && ShowFinishedPending(view); }

bool pc_screens_pending(const SharedScreenView* views, int count) {
    for (int i = 0; i < count; ++i)
        if (views[i].pending_count > 0) return true;
    return false;
}

void shared_screens_show_pixels(SharedScreenView& view, const std::shared_ptr<const std::vector<std::uint8_t>>& pixels,
                                int width, int height) {
    if (pixels == nullptr || pixels == view.uploaded || g_renderer == nullptr || width <= 0 || height <= 0) return;
    if (pixels->size() < static_cast<size_t>(width) * height * 4) return;
    if (view.cpu_texture == nullptr || view.cpu_width != width || view.cpu_height != height) {
        if (view.cpu_texture != nullptr) SDL_DestroyTexture(view.cpu_texture);
        view.cpu_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, width, height);
        if (view.cpu_texture == nullptr) {
            std::fprintf(stderr, "[app] a screen's texture (%dx%d) could not be made: %s\n", width, height, SDL_GetError());
            return;
        }
        SDL_SetTextureScaleMode(view.cpu_texture, SDL_SCALEMODE_LINEAR);
        view.cpu_width = width;
        view.cpu_height = height;
    }
    SDL_UpdateTexture(view.cpu_texture, nullptr, pixels->data(), width * 4);
    DropPending(view, view.pending_count);
    view.uploaded = pixels;
    view.texture = view.cpu_texture;
    view.gl_name = 0;
    view.target = false;
    view.source = SDL_FRect{0, 0, static_cast<float>(width), static_cast<float>(height)};
    view.width = width;
    view.height = height;
    if (view.picture != nullptr) Retire(std::move(view.picture));
    view.picture.reset();
}

void shared_screens_draw(SharedScreenView& view, const SDL_FRect& dst, int win_w, int win_h, Uint8 alpha) {
    if (view.texture == nullptr || g_renderer == nullptr) return;
    // PS1 style on the top screen; else the picture filter there (sharp pixels: Brick's program; nearest: SDL's)
    int ps1 = view.top ? g_ps1_mode : 0;
    if (ps1 == 0 && view.top && g_upscale == 1) ps1 = 1;
    const float phase = ps1 == 2 && (g_ps1_frames++ & 1) != 0 ? 1.0f : 0.0f;
    const bool nearest = view.top && g_upscale == 2 && g_ps1_mode == 0;
    // (into an SDL target, y up, when one is bound: the HDR output's pictures, pc_hdr.h)
    const bool into = SDL_GetRenderTarget(g_renderer) != nullptr;
    if (g_gl && (view.gl_name != 0 || g_colour_on || ps1 != 0) &&
        DrawTexture(g_renderer, view, dst, win_w, win_h, alpha, ps1, phase, into, nearest))
        return;
    if (view.gl_name != 0) return;  // (no program: nothing to draw a shared picture with)
    SDL_SetTextureScaleMode(view.texture, view.top && g_upscale == 2 && g_ps1_mode == 0 ? SDL_SCALEMODE_NEAREST
                                                                                         : SDL_SCALEMODE_LINEAR);
    SDL_SetTextureAlphaMod(view.texture, alpha);
    SDL_RenderTexture(g_renderer, view.texture, &view.source, &dst);
}

void shared_screens_draw_part(SharedScreenView& view, const SDL_FRect& part, const SDL_FRect& dst, int win_w, int win_h,
                              Uint8 alpha) {
    const SDL_FRect whole = view.source;
    view.source = SDL_FRect{whole.x + part.x * whole.w, whole.y + part.y * whole.h, part.w * whole.w, part.h * whole.h};
    shared_screens_draw(view, dst, win_w, win_h, alpha);
    view.source = whole;
}

void shared_screens_set_colour(const ColourAdjust& colour) {
    if (!std::memcmp(&colour, &g_colour, sizeof colour)) return;
    g_colour = colour;
    g_colour_on = !colour_neutral(&colour);
    if (g_colour_on) colour_matrix(&colour, &g_colour_matrix);
}

void shared_screens_set_ps1(bool filter, bool sharp) {
    const int mode = filter ? 2 : sharp ? 1 : 0;
    if (mode != g_ps1_mode)
        std::fprintf(stderr, "[Info] App: the top screen %s\n",
                     mode == 2 ? "with the PS1 filter" : mode == 1 ? "with sharp pixels (Brick)" : "as before");
    g_ps1_mode = mode;
}

void pc_screens_set_upscale(int mode) {
    mode = std::clamp(mode, 0, 2);
    if (mode != g_upscale)
        std::fprintf(stderr, "[Info] App: the 3D picture scaled to the window %s\n",
                     mode == 1 ? "with sharp pixels" : mode == 2 ? "with the nearest pixel" : "smooth");
    g_upscale = mode;
}

void shared_screens_draw_upright(SharedScreenView& view, int w, int h) {
    if (view.texture == nullptr || g_renderer == nullptr) return;
    const SDL_FRect all{0, 0, static_cast<float>(w), static_cast<float>(h)};
    if (view.gl_name != 0) {  // (into the SDL render target bound now: y up; the colour filter's identity, as SDL's own draw)
        const bool colour = g_colour_on;
        g_colour_on = false;
        DrawTexture(g_renderer, view, all, w, h, 255, 0, 0.0f, true);
        g_colour_on = colour;
        return;
    }
    SDL_SetTextureScaleMode(view.texture, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureAlphaMod(view.texture, 255);
    SDL_RenderTexture(g_renderer, view.texture, &view.source, &all);
}

void shared_screens_check(SharedScreenView&, const SDL_FRect&, int, int, const char*) {}
void shared_screens_frame_begin() {}
void shared_screens_frame_end() {}
void shared_screens_note_present(std::uint64_t) {}
// (1.0.2: Android's Fast scaling (more FPS), a cheaper pass of its GL path; the PC draws its screens with its own programs)
void shared_screens_set_fast_top(bool) {}
int shared_screens_wait_fds(const SharedScreenView*, int, int*, int) { return 0; }

void shared_screens_frame_done() {
    ++g_frame;
    if (!g_gl) return;
    if (!g_retiring.empty()) Current(g_renderer);
    while (!g_retiring.empty()) {
        Retiring& r = g_retiring.front();
        if (glClientWaitSync(r.fence, 0, 0) == GL_TIMEOUT_EXPIRED) break;  // (in order)
        glDeleteSync(r.fence);
        g_retiring.pop_front();  // the picture goes: the renderer may draw into its buffer again
    }
    if (g_frame % 120 == 0) {  // imports of buffers not shown for a while, unless a view still shows one
        for (auto it = g_imports.begin(); it != g_imports.end();) {
            if (g_frame - it->second.last_used > 600 && it->second.shown.expired()) {
                DropImport(it->second);
                it = g_imports.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void shared_screens_reset(SharedScreenView* views, int count) {
    for (int i = 0; i < count; ++i) {
        FreeCpuTexture(views[i]);
        DropPending(views[i], views[i].pending_count);
        views[i].texture = nullptr;
        views[i].gl_name = 0;
        views[i].picture.reset();
    }
    g_imports.clear();
    g_retiring.clear();
    g_stand_in = nullptr;
    g_texture_program = TextureProgram{};
    g_texture_program_tried = false;
    for (auto& mode : g_ps1_texture_programs)
        for (Ps1TextureProgram& p : mode) p = Ps1TextureProgram{};
    g_ps1_programs_tried = false;
    g_corners = 0;
}

void shared_screens_shutdown(SharedScreenView* views, int count) {
    if (g_gl) {
        Current(g_renderer);
        glFinish();
    }
    for (int i = 0; i < count; ++i) {
        FreeCpuTexture(views[i]);
        DropPending(views[i], views[i].pending_count);
        views[i].texture = nullptr;
        views[i].gl_name = 0;
        views[i].picture.reset();
    }
    for (Retiring& r : g_retiring)
        if (r.fence != nullptr) glDeleteSync(r.fence);
    g_retiring.clear();
    for (auto& [id, im] : g_imports) DropImport(im);
    g_imports.clear();
}

// ---- the touch screen's own window ----

void pc_screens_set_second(SDL_Renderer* renderer) {
    g_second = renderer;
    g_second_window = renderer != nullptr ? SDL_GetRenderWindow(renderer) : nullptr;
    g_second_context = nullptr;
    if (renderer != nullptr) {
        SDL_RenderClear(renderer);
        SDL_FlushRenderer(renderer);
        g_second_context = SDL_GL_GetCurrentContext();
    }
    pc_screens_make_current();
}

bool pc_screens_draw_second(SharedScreenView& view, const SDL_FRect& dst, int out_w, int out_h) {
    if (g_second == nullptr || !g_gl || view.gl_name == 0) return false;
    // (the program and the corners' buffer are made in the first context, whose objects the second one shares)
    if (!EnsureTextureProgram()) return false;
    return DrawTexture(g_second, view, dst, out_w, out_h, 255, 0, 0.0f);
}

void pc_screens_make_current() {
    if (g_window != nullptr && g_context != nullptr && SDL_GL_GetCurrentContext() != g_context)
        SDL_GL_MakeCurrent(g_window, g_context);
}
