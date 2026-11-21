// The game's touch screen on a second display of the device (second_screen.h).
//
// Raw GL here leaves SDL's state as shared_screens.cpp's draws do: SDL's queue is flushed first (SDL_FlushRenderer,
// after which SDL sets its own state again), the vertex arrays SDL keeps enabled are left as they were, and the EGL
// surfaces that were current (the window's) are made current again before anything else of SDL's runs.
#include "second_screen.h"

#include "shared_screens.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace {

SDL_Renderer* g_renderer = nullptr;
std::atomic<Uint32> g_event{0};
std::atomic<bool> g_display_present{false};
std::atomic<bool> g_has_window{false};
std::atomic<bool> g_needs_draw{false};

// Shared by the Java side's thread (the surface comes and goes there) and the main thread (which draws into it)
std::mutex g_mutex;
EGLDisplay g_egl = EGL_NO_DISPLAY;  // SDL's (second_screen_init)
EGLConfig g_config = nullptr;       // the config of SDL's context: a surface of it can be current with that context
EGLint g_format = 0;                // its native visual: the format of the window's buffers
ANativeWindow* g_window = nullptr;  // the Presentation's SurfaceView's
EGLSurface g_surface = EGL_NO_SURFACE;
int g_view_w = 0, g_view_h = 0;  // the SurfaceView's size, as Java gave it: the fingers' frame
bool g_interval_set = false;
bool g_broken = false;  // no surface could be made, made current or swapped: logged, not tried again until a new one

// The main thread's alone
SDL_Texture* g_target = nullptr;  // the touch screen as the second display shows it, the picture's size
int g_target_w = 0, g_target_h = 0;
GLuint g_program = 0, g_corners = 0;
GLint g_u_rect = -1, g_u_tex = -1;
bool g_program_failed = false;
unsigned g_draws = 0;

// (g_mutex held) the EGL surface destroyed (it is never current then: the main thread makes it current only with the
// lock held) and the window let go
void DropSurfaceLocked() {
    if (g_surface != EGL_NO_SURFACE) eglDestroySurface(g_egl, g_surface);
    g_surface = EGL_NO_SURFACE;
    if (g_window != nullptr) ANativeWindow_release(g_window);
    g_window = nullptr;
    g_has_window.store(false);
}

// An event of this module's to the main loop (SDL_PushEvent from any thread; its event watch wakes the loop's wait)
void Push(int action, int a = 0, int b = 0) {
    const Uint32 type = g_event.load();
    if (type == 0) return;
    SDL_Event e{};
    e.type = type;
    e.user.code = action;
    e.user.data1 = reinterpret_cast<void*>(static_cast<intptr_t>(a));
    e.user.data2 = reinterpret_cast<void*>(static_cast<intptr_t>(b));
    SDL_PushEvent(&e);
}

// The touch screen's place on a surface of w x h: as large as it fits with its 4:3 shape, centred, in whole pixels
SDL_Rect Fit(int w, int h) {
    const float scale = std::min(w / 320.0f, h / 240.0f);
    const int fw = std::max(1, static_cast<int>(320 * scale)), fh = std::max(1, static_cast<int>(240 * scale));
    return SDL_Rect{(w - fw) / 2, (h - fh) / 2, fw, fh};
}

constexpr const char* kVertex = R"(attribute vec2 a_corner;
uniform vec4 u_rect;  // the picture in clip space: left, top, right, bottom
varying vec2 v_uv;
void main() {
    v_uv = a_corner;  // (an SDL render target's texture: its first row is the picture's top)
    gl_Position = vec4(mix(u_rect.x, u_rect.z, a_corner.x), mix(u_rect.y, u_rect.w, a_corner.y), 0.0, 1.0);
}
)";

constexpr const char* kFragment = R"(#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform sampler2D u_tex;
varying vec2 v_uv;
void main() {
    gl_FragColor = vec4(texture2D(u_tex, v_uv).rgb, 1.0);
}
)";

GLuint Compile(GLenum kind, const char* text) {
    const GLuint shader = glCreateShader(kind);
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetShaderInfoLog(shader, sizeof log, nullptr, log);
        std::fprintf(stderr, "[Error] App: the second display's shader: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// The program that copies the target onto the surface, and the quad's corners (0, 0), (1, 0), (0, 1), (1, 1)
bool EnsureProgram() {
    if (g_program != 0) return true;
    if (g_program_failed) return false;
    const GLuint vs = Compile(GL_VERTEX_SHADER, kVertex), fs = Compile(GL_FRAGMENT_SHADER, kFragment);
    if (vs != 0 && fs != 0) {
        const GLuint program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glBindAttribLocation(program, 0, "a_corner");
        glLinkProgram(program);
        GLint ok = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (ok) {
            g_program = program;
        } else {
            char log[1024] = {};
            glGetProgramInfoLog(program, sizeof log, nullptr, log);
            std::fprintf(stderr, "[Error] App: the second display's program: %s\n", log);
            glDeleteProgram(program);
        }
    }
    if (vs != 0) glDeleteShader(vs);
    if (fs != 0) glDeleteShader(fs);
    if (g_program == 0) {
        g_program_failed = true;
        return false;
    }
    g_u_rect = glGetUniformLocation(g_program, "u_rect");
    g_u_tex = glGetUniformLocation(g_program, "u_tex");
    const float corners[] = {0, 0, 1, 0, 0, 1, 1, 1};
    glGenBuffers(1, &g_corners);
    glBindBuffer(GL_ARRAY_BUFFER, g_corners);
    glBufferData(GL_ARRAY_BUFFER, sizeof corners, corners, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

// (g_mutex held) gives up on this surface until a new one comes: the touch screen goes back into the window
void BrokenLocked(const char* what, EGLint error) {
    std::fprintf(stderr, "[Warning] App: the second display: %s (EGL 0x%x): the touch screen goes back into the window\n",
                 what, static_cast<unsigned>(error));
    g_broken = true;
    g_has_window.store(false);
}

}  // namespace

void second_screen_init(SDL_Renderer* renderer) {
    g_renderer = renderer;
    if (g_event.load() == 0) {
        const Uint32 type = SDL_RegisterEvents(1);
        if (type != 0) g_event.store(type);
    }
    const char* name = SDL_GetRendererName(renderer);
    if (name == nullptr || std::strcmp(name, "opengles2") != 0) return;  // (no EGL context of SDL's to draw with)
    SDL_FlushRenderer(renderer);
    const EGLDisplay egl = eglGetCurrentDisplay();
    const EGLContext context = eglGetCurrentContext();
    if (egl == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT) return;
    EGLint id = 0;
    EGLConfig config = nullptr;
    EGLint count = 0;
    if (eglQueryContext(egl, context, EGL_CONFIG_ID, &id) && id != 0) {
        const EGLint attrs[] = {EGL_CONFIG_ID, id, EGL_NONE};
        if (!eglChooseConfig(egl, attrs, &config, 1, &count) || count < 1) config = nullptr;
    }
    EGLint format = 0;
    if (config != nullptr) eglGetConfigAttrib(egl, config, EGL_NATIVE_VISUAL_ID, &format);
    std::lock_guard lock(g_mutex);
    g_egl = egl;
    g_config = config;
    g_format = format;
    if (config == nullptr) std::fprintf(stderr, "[Warning] App: the second display: no config of SDL's context (id %d)\n", id);
}

bool second_screen_display_present() {
    return g_display_present.load();
}

bool second_screen_ready() {
    return g_has_window.load();
}

bool second_screen_needs_draw() {
    return g_has_window.load() && g_needs_draw.load();
}

bool second_screen_draw(SharedScreenView& view) {
    if (g_renderer == nullptr || !g_has_window.load()) return false;
    std::lock_guard lock(g_mutex);
    const EGLContext context = eglGetCurrentContext();
    if (g_window == nullptr || g_broken || g_egl == EGL_NO_DISPLAY || g_config == nullptr || context == EGL_NO_CONTEXT) {
        return false;
    }
    if (g_surface == EGL_NO_SURFACE) {
        ANativeWindow_setBuffersGeometry(g_window, 0, 0, g_format);
        g_surface = eglCreateWindowSurface(g_egl, g_config, g_window, nullptr);
        if (g_surface == EGL_NO_SURFACE) {
            BrokenLocked("no EGL surface on its window", eglGetError());
            return false;
        }
        EGLint w = 0, h = 0;
        eglQuerySurface(g_egl, g_surface, EGL_WIDTH, &w);
        eglQuerySurface(g_egl, g_surface, EGL_HEIGHT, &h);
        const SDL_Rect f = Fit(w, h);
        std::fprintf(stderr, "[Info] App: the second display's surface: %dx%d (its view %dx%d), the touch screen %dx%d at %d,%d\n",
                     w, h, g_view_w, g_view_h, f.w, f.h, f.x, f.y);
    }
    EGLint sw = 0, sh = 0;
    eglQuerySurface(g_egl, g_surface, EGL_WIDTH, &sw);
    eglQuerySurface(g_egl, g_surface, EGL_HEIGHT, &sh);
    if (sw <= 0 || sh <= 0) return false;
    const SDL_Rect f = Fit(sw, sh);
    // 1. the touch screen into a target of the picture's size, as the window draws it (the colour filter too)
    if (g_target == nullptr || g_target_w != f.w || g_target_h != f.h) {
        if (g_target != nullptr) SDL_DestroyTexture(g_target);
        g_target = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, f.w, f.h);
        g_target_w = g_target_h = 0;
        if (g_target == nullptr) {
            std::fprintf(stderr, "[Warning] App: the second display: no %dx%d target (%s)\n", f.w, f.h, SDL_GetError());
            BrokenLocked("no target to draw the touch screen into", EGL_SUCCESS);
            return false;
        }
        g_target_w = f.w, g_target_h = f.h;
    }
    SDL_Texture* const before = SDL_GetRenderTarget(g_renderer);
    SDL_SetRenderTarget(g_renderer, g_target);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    shared_screens_draw_into(view, SDL_FRect{0, 0, static_cast<float>(f.w), static_cast<float>(f.h)}, f.w, f.h);
    SDL_SetRenderTarget(g_renderer, before);
    SDL_FlushRenderer(g_renderer);  // (SDL's commands to GL now; what it had set it sets again at its next draw)
    const GLuint texture = static_cast<GLuint>(
        SDL_GetNumberProperty(SDL_GetTextureProperties(g_target), SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER, 0));
    // 2. that target onto the second display's surface, with the same context
    const EGLSurface draw = eglGetCurrentSurface(EGL_DRAW), read = eglGetCurrentSurface(EGL_READ);
    GLint framebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    if (!eglMakeCurrent(g_egl, g_surface, g_surface, context)) {  // (on failure the window's stay current)
        BrokenLocked("its surface can't be made current", eglGetError());
        return false;
    }
    if (!g_interval_set) {  // (per surface: this one never waits for the second display's vsync)
        eglSwapInterval(g_egl, 0);
        g_interval_set = true;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, sw, sh);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (texture != 0 && EnsureProgram()) {
        glUseProgram(g_program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(g_u_tex, 0);
        const float x0 = 2.0f * f.x / sw - 1.0f, x1 = 2.0f * (f.x + f.w) / sw - 1.0f;
        const float y0 = 1.0f - 2.0f * f.y / sh, y1 = 1.0f - 2.0f * (f.y + f.h) / sh;  // (the surface: y down)
        glUniform4f(g_u_rect, x0, y0, x1, y1);
        // (SDL keeps its colour and texture coordinate arrays enabled: off for this draw, the colour one on again after)
        glDisableVertexAttribArray(1);
        glDisableVertexAttribArray(2);
        glBindBuffer(GL_ARRAY_BUFFER, g_corners);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glEnableVertexAttribArray(1);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    const bool swapped = eglSwapBuffers(g_egl, g_surface) == EGL_TRUE;
    const EGLint swap_error = swapped ? EGL_SUCCESS : eglGetError();
    eglMakeCurrent(g_egl, draw, read, context);  // (the window's again, before anything of SDL's)
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    if (!swapped) {
        BrokenLocked("its surface can't be presented", swap_error);
        return false;
    }
    g_needs_draw.store(false);
    if (++g_draws == 1) std::fprintf(stderr, "[Info] App: the second display shows the touch screen\n");
    return true;
}

Uint32 second_screen_event_type() {
    return g_event.load();
}

SecondScreenAction second_screen_event(const SDL_Event& e, int* x, int* y, bool* inside) {
    const intptr_t a = reinterpret_cast<intptr_t>(e.user.data1), b = reinterpret_cast<intptr_t>(e.user.data2);
    *x = static_cast<int>(a);
    *y = static_cast<int>(b & 0xffff);
    *inside = (b & 0x10000) != 0;
    return static_cast<SecondScreenAction>(e.user.code);
}

std::shared_ptr<const std::vector<std::uint8_t>> second_screen_test_card(int x, int y) {
    auto pixels = std::make_shared<std::vector<std::uint8_t>>(320 * 240 * 4);
    for (int j = 0; j < 240; ++j) {
        for (int i = 0; i < 320; ++i) {
            std::uint8_t* p = pixels->data() + (j * 320 + i) * 4;
            const bool dark = ((i / 40) + (j / 40)) & 1;  // (squares of 40: 8 across, 6 down)
            std::uint8_t r = dark ? 62 : 226, g = dark ? 38 : 182, bl = dark ? 22 : 92;
            if (i < 40 && j < 40) r = 200, g = 30, bl = 30;                    // (red: the top left corner)
            if (i < 2 || j < 2 || i >= 318 || j >= 238) r = g = bl = 255;      // (white: the picture's edge)
            if (x >= 0 && ((std::abs(i - x) <= 1 && std::abs(j - y) <= 14) || (std::abs(j - y) <= 1 && std::abs(i - x) <= 14))) {
                r = 40, g = 220, bl = 255;  // (cyan: the last finger)
            }
            p[0] = r, p[1] = g, p[2] = bl, p[3] = 255;
        }
    }
    return pixels;
}

void second_screen_reset() {
    // (the GL objects went with the context; SDL's texture is destroyed as SDL's)
    if (g_target != nullptr) SDL_DestroyTexture(g_target);
    g_target = nullptr;
    g_target_w = g_target_h = 0;
    g_program = g_corners = 0;
    g_program_failed = false;
    std::lock_guard lock(g_mutex);
    if (g_surface != EGL_NO_SURFACE) eglDestroySurface(g_egl, g_surface);  // (made again on the window, if it is there)
    g_surface = EGL_NO_SURFACE;
    g_needs_draw.store(g_window != nullptr);
}

void second_screen_shutdown() {
    if (g_target != nullptr) SDL_DestroyTexture(g_target);
    g_target = nullptr;
    std::lock_guard lock(g_mutex);
    if (g_surface != EGL_NO_SURFACE) eglDestroySurface(g_egl, g_surface);
    g_surface = EGL_NO_SURFACE;
    g_renderer = nullptr;
}

// ---- the Java side (com.mh4u.port.SecondScreen), on Android's interface thread ------------------------------

extern "C" {

// A presentation display apart from the game's came (true) or the last one went (false)
JNIEXPORT void JNICALL Java_com_mh4u_port_SecondScreen_nativeDisplay(JNIEnv*, jclass, jboolean present) {
    g_display_present.store(present != JNI_FALSE);
    Push(SECOND_SURFACE);  // (the main loop looks again)
}

// The Presentation's SurfaceView has a surface of w x h (surfaceChanged), or none any more (null: surfaceDestroyed, which
// returns only once the EGL surface on it is gone)
JNIEXPORT void JNICALL Java_com_mh4u_port_SecondScreen_nativeSurface(JNIEnv* env, jclass, jobject surface, jint w, jint h) {
    {
        std::lock_guard lock(g_mutex);
        DropSurfaceLocked();
        g_broken = false;
        g_interval_set = false;
        if (surface != nullptr) {
            g_window = ANativeWindow_fromSurface(env, surface);
            g_view_w = w, g_view_h = h;
        }
        g_has_window.store(g_window != nullptr);
        g_needs_draw.store(g_window != nullptr);
    }
    Push(SECOND_SURFACE);
}

// A finger on it: 0 down, 1 moved, 2 up (or cancelled); x, y in the view's pixels
JNIEXPORT void JNICALL Java_com_mh4u_port_SecondScreen_nativeTouch(JNIEnv*, jclass, jint action, jfloat x, jfloat y) {
    int w = 0, h = 0;
    {
        std::lock_guard lock(g_mutex);
        w = g_view_w, h = g_view_h;
    }
    if (w <= 0 || h <= 0 || action < SECOND_TOUCH_DOWN || action > SECOND_TOUCH_UP) return;
    const SDL_Rect f = Fit(w, h);
    const float u = (x - f.x) / f.w, v = (y - f.y) / f.h;
    const bool inside = u >= 0 && u < 1 && v >= 0 && v < 1;
    const int tx = static_cast<int>(std::clamp(u, 0.0f, 0.999f) * 320), ty = static_cast<int>(std::clamp(v, 0.0f, 0.999f) * 240);
    Push(action, tx, ty | (inside ? 0x10000 : 0));
}

}  // extern "C"
