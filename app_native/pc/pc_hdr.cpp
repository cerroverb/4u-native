// MH4U for PC: the HDR output (pc_hdr.h)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_6.h>

#include "pc_hdr.h"
#include "pc_gl.h"
#include "../app_settings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

// WGL_NV_DX_interop(2): OpenGL drawing into a D3D11 texture
typedef HANDLE(WINAPI* OpenDeviceFn)(void*);
typedef BOOL(WINAPI* CloseDeviceFn)(HANDLE);
typedef HANDLE(WINAPI* RegisterObjectFn)(HANDLE, void*, GLuint, GLenum, GLenum);
typedef BOOL(WINAPI* UnregisterObjectFn)(HANDLE, HANDLE);
typedef BOOL(WINAPI* LockObjectsFn)(HANDLE, GLint, HANDLE*);
constexpr GLenum kAccessWriteDiscard = 0x0002;  // WGL_ACCESS_WRITE_DISCARD_NV

constexpr float kPaperNits = 203.0f;   // (BT.2408's reference white for graphics: the MHGU port's HDR_PAPER_NITS)
constexpr float kPeakDefault = 1000.0f;  // (a screen that doesn't say its peak)
constexpr float kScrgbNits = 80.0f;    // (scRGB's 1.0)

struct Hdr {
    int state = 0;  // 0 HDR off; 1 on; 2 [Colour] HDR on but Windows' HDR was off at the start; 3 not possible here
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_GLContext context = nullptr;
    HWND hwnd = nullptr;
    HWND surface = nullptr;  // (a child window over the whole of SDL's: the swapchain's; SDL's own window has OpenGL's)
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* immediate = nullptr;
    IDXGISwapChain1* swapchain = nullptr;
    bool tearing = false;
    int buffers_w = 0, buffers_h = 0;
    // the window's picture in light: a 16-bit float D3D11 texture that OpenGL draws into
    ID3D11Texture2D* picture = nullptr;
    GLuint picture_gl = 0, framebuffer = 0;
    HANDLE interop = nullptr, picture_object = nullptr;
    OpenDeviceFn open_device = nullptr;
    CloseDeviceFn close_device = nullptr;
    RegisterObjectFn register_object = nullptr;
    UnregisterObjectFn unregister_object = nullptr;
    LockObjectsFn lock = nullptr, unlock = nullptr;
    // what SDL draws into: the game's picture and the overlay's
    SDL_Texture* game = nullptr;
    SDL_Texture* overlay = nullptr;
    int w = 0, h = 0;
    GLuint program = 0, corners = 0;
    GLint u_game = -1, u_overlay = -1, u_size = -1, u_light = -1;
    int brightness = 0, highlights = 0;
    bool drawn = false;  // (a frame presented: pc_hdr_read has one)
    bool forced = false;  // (tests: MH4U_HDR_FORCE=1, the output made and mapped as for HDR on an SDR display too)
    std::string output_shot;  // (tests: the next presented picture written as a PNG, pc_hdr_save_output)
};
Hdr g;

struct DisplayState {
    HMONITOR monitor = nullptr;
    Uint64 at = 0;
    bool hdr = false;
    float peak = 0;
};
DisplayState g_display;

template <class T> void Release(T*& p) {
    if (p != nullptr) p->Release();
    p = nullptr;
}

HWND WindowHandle(SDL_Window* window) {
    return static_cast<HWND>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
}

// The adapter and output that show `monitor`, and what the output says of HDR (Windows' HDR on: the HDR10 colour space)
bool FindOutput(HMONITOR monitor, IDXGIAdapter1** adapter_out, bool* hdr, float* peak) {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) return false;
    bool found = false;
    for (UINT a = 0; !found; ++a) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(a, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        for (UINT o = 0; !found; ++o) {
            IDXGIOutput* output = nullptr;
            if (adapter->EnumOutputs(o, &output) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC desc{};
            output->GetDesc(&desc);
            if (desc.Monitor == monitor) {
                found = true;
                IDXGIOutput6* out6 = nullptr;
                DXGI_OUTPUT_DESC1 d1{};
                if (SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput6), reinterpret_cast<void**>(&out6))) &&
                    SUCCEEDED(out6->GetDesc1(&d1))) {
                    if (hdr) *hdr = d1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
                    if (peak) *peak = d1.MaxLuminance;
                }
                Release(out6);
                if (adapter_out) {
                    *adapter_out = adapter;
                    adapter->AddRef();
                }
            }
            output->Release();
        }
        adapter->Release();
    }
    factory->Release();
    return found;
}

void MakeCurrent() {
    SDL_FlushRenderer(g.renderer);
    if (g.context != nullptr && SDL_GL_GetCurrentContext() != g.context) SDL_GL_MakeCurrent(g.window, g.context);
}

GLuint Compile(GLenum kind, const char* text) {
    const GLuint s = glCreateShader(kind);
    glShaderSource(s, 1, &text, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        std::fprintf(stderr, "[Error] App: HDR: a shader of the HDR output: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

// The game's picture in light (an SDR screen's gamma, 2.2), its brightest part spread up (the MHGU port's present.hdr:
// above the knee, towards `top` paper whites), at the paper white; the overlay over it (premultiplied, as SDL's blending
// leaves it over a clear picture) at the paper white, unspread. scRGB: BT.709 primaries, 1.0 = 80 nits. The two pictures
// are SDL's targets (their first row the picture's top), the output a D3D11 texture (its first row the window's top):
// row for row, no turn
constexpr const char* kVertex = R"(#version 100
attribute vec2 a_pos;
void main() { gl_Position = vec4(a_pos * 2.0 - 1.0, 0.0, 1.0); }
)";
constexpr const char* kFragment = R"(#version 100
precision highp float;
uniform sampler2D u_game;
uniform sampler2D u_overlay;
uniform vec2 u_size;   // 1 / the window's size
uniform vec4 u_light;  // x the paper white (scRGB), y where the SDR white goes (in paper whites), z the knee, w the overlay's white
void main() {
    vec2 uv = gl_FragCoord.xy * u_size;
    vec3 l = pow(max(texture2D(u_game, uv).rgb, vec3(0.0)), vec3(2.2));
    float m = max(l.r, max(l.g, l.b));
    if (m > u_light.z) {
        float x = (m - u_light.z) / (1.0 - u_light.z);
        l *= (m + (u_light.y - 1.0) * x * x) / m;
    }
    vec4 o = texture2D(u_overlay, uv);
    vec3 ol = o.a > 0.0 ? pow(o.rgb / o.a, vec3(2.2)) * o.a : vec3(0.0);
    gl_FragColor = vec4(l * u_light.x * (1.0 - o.a) + ol * u_light.w, 1.0);
}
)";

bool MakeProgram() {
    const GLuint vs = Compile(GL_VERTEX_SHADER, kVertex), fs = Compile(GL_FRAGMENT_SHADER, kFragment);
    if (vs == 0 || fs == 0) return false;
    g.program = glCreateProgram();
    glAttachShader(g.program, vs);
    glAttachShader(g.program, fs);
    glBindAttribLocation(g.program, 0, "a_pos");
    glLinkProgram(g.program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(g.program, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(g.program);
        g.program = 0;
        return false;
    }
    g.u_game = glGetUniformLocation(g.program, "u_game");
    g.u_overlay = glGetUniformLocation(g.program, "u_overlay");
    g.u_size = glGetUniformLocation(g.program, "u_size");
    g.u_light = glGetUniformLocation(g.program, "u_light");
    GLint buffer = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
    const float corners[] = {0, 0, 1, 0, 0, 1, 1, 1};
    glGenBuffers(1, &g.corners);
    glBindBuffer(GL_ARRAY_BUFFER, g.corners);
    glBufferData(GL_ARRAY_BUFFER, sizeof corners, corners, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(buffer));
    return true;
}

void DropSized() {
    if (g.picture_object != nullptr) {
        MakeCurrent();
        g.unregister_object(g.interop, g.picture_object);
        g.picture_object = nullptr;
    }
    if (g.picture_gl != 0) {
        MakeCurrent();
        glDeleteTextures(1, &g.picture_gl);
        g.picture_gl = 0;
    }
    Release(g.picture);
    if (g.game != nullptr) SDL_DestroyTexture(g.game);
    if (g.overlay != nullptr) SDL_DestroyTexture(g.overlay);
    g.game = g.overlay = nullptr;
    g.w = g.h = 0;
}

// The pictures of the window's size (again when it changes); the swapchain's buffers too
bool Size(int w, int h) {
    if (w == g.w && h == g.h && g.picture_object != nullptr) return true;
    DropSized();
    UINT flags = g.tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    if (g.surface != nullptr) SetWindowPos(g.surface, HWND_TOP, 0, 0, w, h, SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    if (w != g.buffers_w || h != g.buffers_h) {
        const HRESULT hr = g.swapchain->ResizeBuffers(2, static_cast<UINT>(w), static_cast<UINT>(h),
                                                      DXGI_FORMAT_R16G16B16A16_FLOAT, flags);
        if (FAILED(hr)) {
            std::fprintf(stderr, "[Error] App: HDR: the swapchain's buffers at %dx%d: 0x%08lx\n", w, h, hr);
            return false;
        }
        g.buffers_w = w, g.buffers_h = h;
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(w), td.Height = static_cast<UINT>(h), td.MipLevels = 1, td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(g.device->CreateTexture2D(&td, nullptr, &g.picture))) return false;
    MakeCurrent();
    glGenTextures(1, &g.picture_gl);
    g.picture_object = g.register_object(g.interop, g.picture, g.picture_gl, GL_TEXTURE_2D, kAccessWriteDiscard);
    if (g.picture_object == nullptr) {
        std::fprintf(stderr, "[Error] App: HDR: the picture's texture shared with OpenGL: %lu\n", GetLastError());
        return false;
    }
    g.game = SDL_CreateTexture(g.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
    g.overlay = SDL_CreateTexture(g.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
    if (g.game == nullptr || g.overlay == nullptr) {
        std::fprintf(stderr, "[Error] App: HDR: the window's pictures (%dx%d): %s\n", w, h, SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(g.game, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureScaleMode(g.overlay, SDL_SCALEMODE_NEAREST);
    g.w = w, g.h = h;
    return true;
}

float Half(uint16_t h) {
    const uint32_t sign = (h >> 15) & 1, exp = (h >> 10) & 31, man = h & 1023;
    if (exp == 0) return (sign ? -1.0f : 1.0f) * std::ldexp(static_cast<float>(man), -24);
    if (exp == 31) return 65504.0f;
    return (sign ? -1.0f : 1.0f) * std::ldexp(static_cast<float>(man | 1024), static_cast<int>(exp) - 25);
}

// (tests) The window's picture as presented, read back from D3D11: written as an ordinary PNG of the light divided by
// the paper white (what is above it clipped), with the share of the pixels above the paper white and the brightest
// one in the log
void SaveOutput(const std::string& path, float paper) {
    D3D11_TEXTURE2D_DESC td{};
    g.picture->GetDesc(&td);
    td.BindFlags = 0;
    td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* staging = nullptr;
    if (FAILED(g.device->CreateTexture2D(&td, nullptr, &staging))) return;
    g.immediate->CopyResource(staging, g.picture);
    D3D11_MAPPED_SUBRESOURCE map{};
    std::vector<uint16_t> pixels;
    const int w = static_cast<int>(td.Width), h = static_cast<int>(td.Height);
    if (SUCCEEDED(g.immediate->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
        pixels.resize(static_cast<size_t>(w) * h * 4);
        for (int y = 0; y < h; ++y)
            std::memcpy(&pixels[static_cast<size_t>(y) * w * 4], static_cast<const uint8_t*>(map.pData) + y * map.RowPitch,
                        static_cast<size_t>(w) * 8);
        g.immediate->Unmap(staging, 0);
    }
    staging->Release();
    if (pixels.empty()) return;
    std::thread([pixels = std::move(pixels), w, h, path, paper] {
        SDL_Surface* out = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
        if (out == nullptr) return;
        size_t above = 0;
        float brightest = 0;
        for (int y = 0; y < h; ++y) {
            Uint8* o = static_cast<Uint8*>(out->pixels) + y * out->pitch;
            for (int x = 0; x < w; ++x, o += 4) {
                const uint16_t* p = &pixels[(static_cast<size_t>(y) * w + x) * 4];
                float m = 0;
                for (int c = 0; c < 3; ++c) {
                    const float v = Half(p[c]);
                    m = std::max(m, v);
                    const float e = std::pow(std::clamp(v / paper, 0.0f, 1.0f), 1.0f / 2.2f);
                    o[c] = static_cast<Uint8>(e * 255.0f + 0.5f);
                }
                o[3] = 255;
                above += m > paper * 1.001f;
                brightest = std::max(brightest, m);
            }
        }
        const bool ok = SDL_SavePNG(out, path.c_str());
        SDL_DestroySurface(out);
        std::fprintf(stderr,
                     "[app] HDR: the presented picture saved to %s: %s (the paper white %.0f nits; %.2f %% of it above; "
                     "the brightest %.0f nits)\n",
                     path.c_str(), ok ? "ok" : SDL_GetError(), paper * kScrgbNits, 100.0 * above / (static_cast<double>(w) * h),
                     brightest * kScrgbNits);
    }).detach();
}

// The swapchain's window: a child of SDL's covering all of it, the mouse passing through it to SDL's (HTTRANSPARENT,
// the same thread), never taking the focus. A window of its own because the OpenGL driver may hold SDL's window's
// presentation already (CreateSwapChainForHwnd on it: E_ACCESSDENIED with the game's GL context on it)
LRESULT CALLBACK SurfaceProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND MakeSurface(HWND parent, int w, int h) {
    static bool registered = false;
    const HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = SurfaceProc;
        wc.hInstance = inst;
        wc.lpszClassName = L"MH4UHdrSurface";
        registered = RegisterClassW(&wc) != 0;
    }
    // (the parent clips its children: SDL's window draws nothing under it meanwhile)
    SetWindowLongPtrW(parent, GWL_STYLE, GetWindowLongPtrW(parent, GWL_STYLE) | WS_CLIPCHILDREN);
    return CreateWindowExW(WS_EX_NOPARENTNOTIFY, L"MH4UHdrSurface", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_DISABLED,
                           0, 0, std::max(w, 8), std::max(h, 8), parent, nullptr, inst, nullptr);
}

void Stop(const char* why) {
    std::fprintf(stderr, "[Error] App: HDR: %s: the window as SDR from now on\n", why);
    if (g.renderer != nullptr) SDL_SetRenderTarget(g.renderer, nullptr);
    pc_hdr_shutdown();
    g.state = 3;
}

}  // namespace

bool pc_hdr_display(SDL_Window* window, float* peak_nits) {
    const HWND hwnd = window != nullptr ? WindowHandle(window) : nullptr;
    if (hwnd == nullptr) return false;
    const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    const Uint64 now = SDL_GetTicks();
    if (monitor != g_display.monitor || now - g_display.at >= 1000 || g_display.at == 0) {
        g_display.monitor = monitor;
        g_display.at = now;
        g_display.hdr = false;
        g_display.peak = 0;
        FindOutput(monitor, nullptr, &g_display.hdr, &g_display.peak);
    }
    if (peak_nits) *peak_nits = g_display.peak;
    return g_display.hdr;
}

bool pc_hdr_on() { return g.state == 1; }

int pc_hdr_state() { return g.state; }

bool pc_hdr_start(SDL_Window* window, SDL_Renderer* renderer) {
    g.state = 0;
    if (!app_setting_bool("Colour", "HDR", false)) return false;
    g.window = window;
    g.renderer = renderer;
    g.hwnd = WindowHandle(window);
    float peak = 0;
    const char* force = SDL_getenv("MH4U_HDR_FORCE");
    g.forced = force != nullptr && std::strcmp(force, "1") == 0;
    if (g.hwnd == nullptr || (!pc_hdr_display(window, &peak) && !g.forced)) {
        g.state = 2;
        std::fprintf(stderr, "[Info] App: HDR: [Colour] HDR is on, but Windows' HDR is off for this screen: the window as SDR\n");
        return false;
    }
    g.state = 3;  // (until it is all made)
    g.context = SDL_GL_GetCurrentContext();
    MakeCurrent();
    g.open_device = reinterpret_cast<OpenDeviceFn>(wglGetProcAddress("wglDXOpenDeviceNV"));
    g.close_device = reinterpret_cast<CloseDeviceFn>(wglGetProcAddress("wglDXCloseDeviceNV"));
    g.register_object = reinterpret_cast<RegisterObjectFn>(wglGetProcAddress("wglDXRegisterObjectNV"));
    g.unregister_object = reinterpret_cast<UnregisterObjectFn>(wglGetProcAddress("wglDXUnregisterObjectNV"));
    g.lock = reinterpret_cast<LockObjectsFn>(wglGetProcAddress("wglDXLockObjectsNV"));
    g.unlock = reinterpret_cast<LockObjectsFn>(wglGetProcAddress("wglDXUnlockObjectsNV"));
    if (!g.open_device || !g.close_device || !g.register_object || !g.unregister_object || !g.lock || !g.unlock) {
        Stop("the OpenGL driver has no WGL_NV_DX_interop2");
        return false;
    }
    IDXGIAdapter1* adapter = nullptr;
    FindOutput(MonitorFromWindow(g.hwnd, MONITOR_DEFAULTTONEAREST), &adapter, nullptr, nullptr);
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    HRESULT hr = D3D11CreateDevice(adapter, adapter != nullptr ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                   levels, 2, D3D11_SDK_VERSION, &g.device, nullptr, &g.immediate);
    Release(adapter);
    if (FAILED(hr)) {
        Stop("no D3D11 device");
        return false;
    }
    IDXGIDevice* dxgi_device = nullptr;
    IDXGIAdapter* dxgi_adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    g.device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_device));
    if (dxgi_device) dxgi_device->GetAdapter(&dxgi_adapter);
    if (dxgi_adapter) dxgi_adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory));
    Release(dxgi_device);
    Release(dxgi_adapter);
    if (factory == nullptr) {
        Stop("no DXGI factory");
        return false;
    }
    IDXGIFactory5* factory5 = nullptr;
    BOOL tearing = FALSE;
    if (SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory5), reinterpret_cast<void**>(&factory5))))
        factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof tearing);
    Release(factory5);
    g.tearing = tearing != FALSE;
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = static_cast<UINT>(std::max(w, 8)), sd.Height = static_cast<UINT>(std::max(h, 8));
    sd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = g.tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    g.surface = MakeSurface(g.hwnd, w, h);
    hr = g.surface != nullptr ? factory->CreateSwapChainForHwnd(g.device, g.surface, &sd, nullptr, nullptr, &g.swapchain)
                              : HRESULT_FROM_WIN32(GetLastError());
    if (SUCCEEDED(hr)) factory->MakeWindowAssociation(g.surface, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    Release(factory);
    if (FAILED(hr)) {
        char why[96];
        std::snprintf(why, sizeof why, "no flip-model swapchain on the window (0x%08lx)", hr);
        Stop(why);
        return false;
    }
    g.buffers_w = static_cast<int>(sd.Width), g.buffers_h = static_cast<int>(sd.Height);
    IDXGISwapChain3* sc3 = nullptr;
    UINT support = 0;
    if (SUCCEEDED(g.swapchain->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&sc3))))
        sc3->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &support);
    hr = sc3 != nullptr && (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)
             ? sc3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709)
             : E_FAIL;
    Release(sc3);
    if (FAILED(hr)) {
        Stop("the swapchain can't be scRGB");
        return false;
    }
    MakeCurrent();
    g.interop = g.open_device(g.device);
    if (g.interop == nullptr) {
        Stop("OpenGL can't draw into D3D11 here (wglDXOpenDeviceNV)");
        return false;
    }
    if (!MakeProgram()) {
        Stop("its program");
        return false;
    }
    if (!Size(std::max(w, 8), std::max(h, 8))) {
        Stop("the window's pictures");
        return false;
    }
    g.brightness = app_setting_int("Colour", "HDRBrightness", 0);
    g.highlights = app_setting_int("Colour", "HDRHighlights", 0);
    g.state = 1;
    std::fprintf(stderr,
                 "[Info] App: HDR: on (scRGB through D3D11, the screen's peak %.0f nits; the paper white %.0f nits, the "
                 "highlights %d)%s%s\n",
                 peak, kPaperNits * std::exp2(g.brightness / 100.0f), g.highlights,
                 g.tearing ? "" : "; no tearing presents: VSync off waits too",
                 g.forced ? "; MH4U_HDR_FORCE=1: made and mapped as for HDR though Windows' HDR is off" : "");
    return true;
}

bool pc_hdr_begin(SDL_Renderer* renderer, int w, int h) {
    if (g.state != 1 || renderer != g.renderer || w <= 0 || h <= 0) return false;
    if (!Size(w, h)) {
        Stop("the window's pictures at a new size");
        return false;
    }
    // (the target again, bound anew: frame generation's own drawing may have bound another framebuffer meanwhile)
    SDL_SetRenderTarget(renderer, nullptr);
    SDL_SetRenderTarget(renderer, g.game);
    return true;
}

void pc_hdr_overlay(SDL_Renderer* renderer) {
    if (g.state != 1 || renderer != g.renderer) return;
    SDL_SetRenderTarget(renderer, g.overlay);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
}

void pc_hdr_present(SDL_Renderer* renderer, bool vsync) {
    if (g.state != 1 || renderer != g.renderer) return;
    float peak = 0;
    const bool hdr = pc_hdr_display(g.window, &peak) || g.forced;
    // (the MHGU port's hdr_params, in scRGB; Windows' HDR off since: SDR's white, unspread)
    float paper = 1.0f, top = 1.0f;
    if (hdr) {
        const float nits = kPaperNits * std::exp2(g.brightness / 100.0f);
        if (!(peak > 0)) peak = kPeakDefault;
        peak = std::min(peak, 10000.0f);
        const float e = std::clamp(0.5f + g.highlights / 200.0f, 0.0f, 1.0f);
        top = 1 + e * (std::max(peak / nits, 1.0f) - 1);
        paper = nits / kScrgbNits;
    }
    const GLuint game_gl = static_cast<GLuint>(
        SDL_GetNumberProperty(SDL_GetTextureProperties(g.game), SDL_PROP_TEXTURE_OPENGL_TEXTURE_NUMBER, 0));
    const GLuint overlay_gl = static_cast<GLuint>(
        SDL_GetNumberProperty(SDL_GetTextureProperties(g.overlay), SDL_PROP_TEXTURE_OPENGL_TEXTURE_NUMBER, 0));
    MakeCurrent();
    if (!g.lock(g.interop, 1, &g.picture_object)) {
        Stop("the picture's texture could not be taken for OpenGL");
        return;
    }
    GLint framebuffer = 0, active = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    if (g.framebuffer == 0) glGenFramebuffers(1, &g.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, g.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g.picture_gl, 0);
    glViewport(0, 0, g.w, g.h);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glUseProgram(g.program);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, overlay_gl);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, game_gl);
    glUniform1i(g.u_game, 0);
    glUniform1i(g.u_overlay, 1);
    glUniform2f(g.u_size, 1.0f / g.w, 1.0f / g.h);
    glUniform4f(g.u_light, paper, top, 0.5f, paper);
    glBindBuffer(GL_ARRAY_BUFFER, g.corners);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(static_cast<GLenum>(active));
    glUseProgram(0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    g.unlock(g.interop, 1, &g.picture_object);  // (OpenGL's work on it done before D3D11's: the interop's own sync)
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(g.swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) {
        g.immediate->CopyResource(back, g.picture);
        back->Release();
    }
    if (!g.output_shot.empty()) {
        SaveOutput(g.output_shot, paper);
        g.output_shot.clear();
    }
    const HRESULT hr = g.swapchain->Present(vsync ? 1 : 0, !vsync && g.tearing ? DXGI_PRESENT_ALLOW_TEARING : 0);
    if (FAILED(hr) && hr != DXGI_ERROR_WAS_STILL_DRAWING) {
        static int told = 0;
        if (told++ < 3) std::fprintf(stderr, "[Warning] App: HDR: a present: 0x%08lx\n", hr);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) Stop("the D3D11 device was lost");
    }
    g.drawn = true;
    // SDL draws to the game's picture next (pc_hdr_begin): never to the window's own framebuffer meanwhile
    SDL_SetRenderTarget(renderer, g.game);
}

SDL_Surface* pc_hdr_read(SDL_Renderer* renderer) {
    if (g.state != 1 || renderer != g.renderer || g.game == nullptr) return nullptr;
    SDL_Texture* was = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, g.game);
    SDL_Surface* game = SDL_RenderReadPixels(renderer, nullptr);
    SDL_SetRenderTarget(renderer, g.overlay);
    SDL_Surface* overlay = SDL_RenderReadPixels(renderer, nullptr);
    SDL_SetRenderTarget(renderer, was);
    if (game == nullptr || overlay == nullptr) {
        if (game) SDL_DestroySurface(game);
        if (overlay) SDL_DestroySurface(overlay);
        return nullptr;
    }
    SDL_Surface* a = SDL_ConvertSurface(game, SDL_PIXELFORMAT_RGBA32);
    SDL_Surface* b = SDL_ConvertSurface(overlay, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(game);
    SDL_DestroySurface(overlay);
    if (a == nullptr || b == nullptr || a->w != b->w || a->h != b->h) {
        if (a) SDL_DestroySurface(a);
        if (b) SDL_DestroySurface(b);
        return nullptr;
    }
    // the overlay over the game, as the window shows them (premultiplied over: SDL's blending over a clear picture)
    for (int y = 0; y < a->h; ++y) {
        Uint8* p = static_cast<Uint8*>(a->pixels) + y * a->pitch;
        const Uint8* q = static_cast<const Uint8*>(b->pixels) + y * b->pitch;
        for (int x = 0; x < a->w; ++x, p += 4, q += 4) {
            const int k = 255 - q[3];
            for (int c = 0; c < 3; ++c) p[c] = static_cast<Uint8>(std::min(255, q[c] + (p[c] * k + 127) / 255));
            p[3] = 255;
        }
    }
    SDL_DestroySurface(b);
    return a;
}

void pc_hdr_save_output(const std::string& path) {
    if (g.state == 1) g.output_shot = path;
}

void pc_hdr_preview(int brightness, int highlights) {
    g.brightness = std::clamp(brightness, -100, 100);
    g.highlights = std::clamp(highlights, -100, 100);
}

void pc_hdr_shutdown() {
    if (g.renderer != nullptr && g.state == 1) SDL_SetRenderTarget(g.renderer, nullptr);
    if (g.interop != nullptr || g.picture_gl != 0 || g.program != 0) MakeCurrent();
    DropSized();
    if (g.framebuffer != 0) glDeleteFramebuffers(1, &g.framebuffer);
    if (g.program != 0) glDeleteProgram(g.program);
    if (g.corners != 0) glDeleteBuffers(1, &g.corners);
    g.framebuffer = g.program = g.corners = 0;
    if (g.interop != nullptr) g.close_device(g.interop);
    g.interop = nullptr;
    Release(g.swapchain);
    if (g.surface != nullptr) DestroyWindow(g.surface);
    g.surface = nullptr;
    if (g.immediate) g.immediate->ClearState();
    Release(g.immediate);
    Release(g.device);
    if (g.state == 1) g.state = 0;
}
