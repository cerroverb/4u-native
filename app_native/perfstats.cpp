// Performance readout (from the MHGU port; the settings' FPS counter): frames per second, the frame time and, on a phone, its temperature, in
// a small plate at the top of the screen in the game's window style, as the touch controls draw it (runtime/touch.cpp):
// wine-dark, a black edge, red and green rules top and bottom, gold labels and cream values. It sits in the gap the
// touch HUD leaves at the top, between the health bar's end (~49% of the width) and the target camera's icons (~76%).
//   - frames: those presented (g_present_count: the renderer counts them), over half a second; the frame time is
//     theirs on average, and the longest gap between the game's frames in the last second (this is drawn once a frame,
//     on the game's update thread: runtime/overlay.cpp)
//   - temperature (Android): the battery's (the sticky ACTION_BATTERY_CHANGED intent: no permission needed) and
//     PowerManager's thermal status and headroom, from DeviceStats.java by JNI on the host main thread every 2 s while
//     shown (perfstats_poll), kept in atomics for the game thread
//   - a PC (Windows, MH4U for PC; MHGU for PC's same lines): [Graphics] PcStats basic adds the GPU's temperature; full a
//     plate under it with a line on the GPU (use, temperature, clock, video memory) and one on the CPU (the game's use and
//     memory, the processors' clock, the game's and the window's threads' time a frame), from a sampler on a thread of
//     its own (pc/pc_stats.c)
#include <algorithm>
#include <atomic>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <chrono>
#include <thread>

#include <SDL3/SDL.h>
#ifdef __ANDROID__
#include <jni.h>
#endif

#include "imgui.h"

#include "perfstats.h"
#ifdef _WIN32
#include "pc_stats.h"
// (the PC's level: 0 or 1 the GPU's temperature on the line, 2 the GPU's and the CPU's lines too; settings_pc.inc)
static std::atomic<int> g_pc_level{0};
void perfstats_set_pc_level(int level) { g_pc_level = level; }
#endif

// (MHGU's settings gave these: here the app sets them)
static std::atomic<bool> g_show_stats{false};
static std::atomic<float> g_stats_fps_cap{30.0f};
static std::atomic<int> g_stats_language{0};
extern "C" void perfstats_set(bool visible, float fps_cap, bool spanish) {
    g_show_stats = visible;
    g_stats_fps_cap = fps_cap;
    g_stats_language = spanish ? 1 : 0;
}
static bool config_live_show_stats() { return g_show_stats.load(); }
static float config_live_fps() { return g_stats_fps_cap.load(); }
extern "C" float perfstats_fps_cap(void) { return g_stats_fps_cap.load(); }
static int config_language() { return g_stats_language.load(); }

#define LOG(...) fprintf(stderr, __VA_ARGS__)

extern "C" volatile uint64_t g_present_count;  // (android_main.cpp: the game's frames shown)

namespace {

#ifdef __ANDROID__
std::atomic<int> g_temp{INT_MIN};    // the battery's temperature, tenths of a degree C (INT_MIN: not known)
std::atomic<int> g_thermal{-1};      // PowerManager.THERMAL_STATUS_*: 0 none .. 6 shutdown (-1: not known)
std::atomic<float> g_headroom{NAN};  // getThermalHeadroom(10): 1 = the phone slows itself down (NaN: not known)

// A Java exception pending: logged and cleared (every JNI call after one would fail). true: there was one.
bool jfail(JNIEnv *env) {
    if (!env->ExceptionCheck()) return false;
    env->ExceptionDescribe();
    env->ExceptionClear();
    return true;
}

// com.mh4u.port.DeviceStats through the activity's class loader (FindClass from native code sees only the system's
// classes: runtime/ui.c restart_application found the same). A global reference, or NULL.
jclass device_stats_class(JNIEnv *env) {
    jobject act = (jobject)SDL_GetAndroidActivity();
    if (!act) return nullptr;
    jclass act_cls = env->GetObjectClass(act), loader_cls = nullptr, found = nullptr;
    jobject loader = nullptr, cls = nullptr;
    jstring name = nullptr;
    jmethodID get_loader = env->GetMethodID(act_cls, "getClassLoader", "()Ljava/lang/ClassLoader;"), load = nullptr;
    if (!jfail(env) && get_loader) loader = env->CallObjectMethod(act, get_loader);
    if (!jfail(env) && loader) loader_cls = env->FindClass("java/lang/ClassLoader");
    if (!jfail(env) && loader_cls) load = env->GetMethodID(loader_cls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (!jfail(env) && load) name = env->NewStringUTF("com.mh4u.port.DeviceStats");
    if (!jfail(env) && name) cls = env->CallObjectMethod(loader, load, name);
    if (!jfail(env) && cls) found = (jclass)env->NewGlobalRef(cls);
    for (jobject o : {cls, (jobject)name, (jobject)loader_cls, loader, (jobject)act_cls, act})
        if (o) env->DeleteLocalRef(o);
    return found;
}
#endif

const ImU32 WINE = IM_COL32(50, 25, 24, 230), RED = IM_COL32(176, 34, 30, 255), MOSS = IM_COL32(80, 142, 54, 255),
            GOLD = IM_COL32(222, 186, 96, 255), DIM = IM_COL32(222, 186, 96, 150), INK = IM_COL32(244, 232, 200, 255),
            // (the FPS and the temperature: green, gold or red, lighter than the rules' to read well on the plate)
            GOOD = IM_COL32(132, 204, 96, 255), BAD = IM_COL32(236, 84, 64, 255);

// v with one decimal: a comma in Spanish
void decimal(char *buf, size_t size, double v, bool comma) {
    snprintf(buf, size, "%.1f", v);
    for (char *p = buf; comma && *p; p++)
        if (*p == '.') *p = ',';
}

struct Run { const char *text; ImU32 col; };

}  // namespace

#ifdef __ANDROID__
namespace {
// The three calls (each a binder transaction; the thermal headroom's can take milliseconds) every 2 s on a thread of
// their own, attached to the VM: on the main thread they came right after a present, every 2 s, and could hold the
// next one past its vsync.
void poll_thread(JavaVM *vm, jclass cls, jmethodID m_temp, jmethodID m_status, jmethodID m_headroom) {
    JNIEnv *env = nullptr;
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || env == nullptr) return;
    for (;;) {
        if (config_live_show_stats()) {
            jint t = env->CallStaticIntMethod(cls, m_temp);
            g_temp = jfail(env) ? INT_MIN : (int)t;
            jint s = env->CallStaticIntMethod(cls, m_status);
            g_thermal = jfail(env) ? -1 : (int)s;
            jfloat r = env->CallStaticFloatMethod(cls, m_headroom);
            g_headroom = jfail(env) ? NAN : (float)r;
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}
}  // namespace
#endif

extern "C" void perfstats_poll(void) {
#ifdef __ANDROID__
    static bool failed, started;
    static jclass cls;
    static jmethodID m_temp, m_status, m_headroom;
    if (failed || started || !config_live_show_stats()) return;
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    if (!env) return;
    if (!cls) {
        cls = device_stats_class(env);
        if (cls) m_temp = env->GetStaticMethodID(cls, "batteryTempTenths", "()I");
        if (cls && !jfail(env)) m_status = env->GetStaticMethodID(cls, "thermalStatus", "()I");
        if (cls && !jfail(env)) m_headroom = env->GetStaticMethodID(cls, "thermalHeadroom", "()F");
        jfail(env);
        if (!cls || !m_temp || !m_status || !m_headroom) {
            failed = true;
            LOG("[perf] com.mh4u.port.DeviceStats not usable: no temperature on screen\n");
            return;
        }
    }
    JavaVM *vm = nullptr;
    if (env->GetJavaVM(&vm) != JNI_OK || vm == nullptr) {
        failed = true;
        return;
    }
    started = true;
    std::thread(poll_thread, vm, cls, m_temp, m_status, m_headroom).detach();
#elif defined(_WIN32)
    winstats_level(config_live_show_stats() ? std::max(1, g_pc_level.load()) : 0);  // (the sampler: idle while hidden)
#endif
}

extern "C" bool perfstats_visible(void) { return config_live_show_stats() != 0; }

// The plate as last drawn, for the layout editor's frame (perfstats_rect)
static std::atomic<float> g_plate[4];
static std::atomic<uint64_t> g_plate_at;
extern "C" bool perfstats_rect(float r[4]) {
    uint64_t at = g_plate_at.load();
    if (!at || SDL_GetTicksNS() - at > 500000000ull) return false;
    for (int i = 0; i < 4; i++) r[i] = g_plate[i].load();
    return true;
}

std::atomic<uint64_t> g_framegen_presents{0};  // (frame_gen.cpp: the made pictures presented)
extern "C" void perfstats_draw(float w, float h) {
    // the measures: the frames presented in each half second; the longest gap between two of these calls (the game's
    // frames) in each second, the last whole second's shown (the one going on until there is one)
    static uint64_t last, win_t0, win_n0, sec_t0, win_g0;
    static float fps = -1, avg_ms, worst_cur, worst_ms, shown = 0;
    uint64_t now = SDL_GetTicksNS(), n = g_present_count, gn = g_framegen_presents.load(std::memory_order_relaxed);
    if (!last || now - last > 5000000000ull) {  // (first shown, or after a long gap: hidden meanwhile, the app paused;
                                                // a stall of a second or more still shows)
        win_t0 = sec_t0 = now, win_n0 = n, win_g0 = gn;
        fps = -1, worst_cur = worst_ms = 0, shown = 0;
    } else {
        worst_cur = std::max(worst_cur, (now - last) / 1e6f);
        if (now - win_t0 >= 500000000ull) {
            double s = (now - win_t0) / 1e9;
            fps = (float)((n - win_n0) / s);
            shown = gn > win_g0 ? (float)((n - win_n0 + gn - win_g0) / s) : 0;  // (frame generation: the frames shown)
            avg_ms = n > win_n0 ? (float)(s * 1e3 / (double)(n - win_n0)) : 0;
            win_t0 = now, win_n0 = n, win_g0 = gn;
        }
        if (now - sec_t0 >= 1000000000ull) worst_ms = worst_cur, worst_cur = 0, sec_t0 = now;
    }
    last = now;
    if (fps < 0) return;  // (nothing measured yet)

    // the line: "60 FPS · 16.6 ms · max 24.1 · 38.5 °C (moderate)", the status only when there is one
    bool es = config_language() == 1;
    char f[16], ms[16], mx[16];
    snprintf(f, sizeof f, "%.0f", fps);
    decimal(ms, sizeof ms, avg_ms, es);
    decimal(mx, sizeof mx, worst_ms > 0 ? worst_ms : worst_cur, es);
    const char *SEP = " \xC2\xB7 ";  // (U+00B7)
    Run runs[24];
    int nr = 0;
    auto add = [&](const char *t, ImU32 c) { if (nr < 24) runs[nr++] = {t, c}; };
    float cap = config_live_fps();
#ifdef _WIN32
    if (cap >= 1000) cap = 0;  // (no limit: settings_pc.h kPcFpsUnlimited)
    static WinStats pc{};
    static uint32_t pc_seq = 0;
    pc_seq = winstats_get(&pc, pc_seq);
#endif
    add(f, fps >= cap * .95f ? GOOD : fps >= cap * .75f ? GOLD : BAD);
    add(" FPS", GOLD);
    char sh[16];
    if (shown > 0) {  // ([Graphics] FrameGen making pictures: "30 FPS (60 shown)")
        std::snprintf(sh, sizeof sh, "%.0f", shown);
        add(" (", DIM);
        add(sh, INK);
        add(es ? " mostrados)" : " shown)", GOLD);
    }
    add(SEP, DIM);
    add(avg_ms > 0 ? ms : "--", INK);
    add(" ms", GOLD);
    add(SEP, DIM);
    add(es ? "m\xC3\xA1x " : "max ", GOLD);
    add(mx, INK);
#ifdef _WIN32
    char gt[16];
    if (pc_seq != 0 && std::isfinite(pc.gpu_temp)) {  // (the GPU's temperature: a value a driver leaves at 0 is not one)
        decimal(gt, sizeof gt, pc.gpu_temp, es);
        add(SEP, DIM);
        add("GPU ", GOLD);
        add(gt, pc.gpu_temp >= 85 ? BAD : pc.gpu_temp >= 75 ? GOLD : INK);
        add(" \xC2\xB0" "C", GOLD);
    }
#endif
#ifdef __ANDROID__
    static const char *const STATUS_EN[7] = {"", "light", "moderate", "severe", "critical", "emergency", "shutdown"};
    static const char *const STATUS_ES[7] = {"", "leve", "moderado", "grave", "cr\xC3\xADtico", "emergencia", "apagando"};
    char tc[16];
    int t = g_temp, st = g_thermal;
    float room = g_headroom;  // (NaN: every comparison false)
    if (t != INT_MIN) {
        bool hot = st >= 2 || room >= 1.0f || t >= 450, warm = st == 1 || room >= .85f || t >= 420;
        decimal(tc, sizeof tc, t / 10.0, es);
        add(SEP, DIM);
        add(tc, hot ? BAD : warm ? GOLD : INK);
        add(" \xC2\xB0" "C", GOLD);  // (U+00B0)
    }
    if (st >= 1 && st <= 6) {
        if (t != INT_MIN) add(" (", GOLD); else add(SEP, DIM);
        add(es ? STATUS_ES[st] : STATUS_EN[st], st >= 2 ? BAD : GOLD);
        if (t != INT_MIN) add(")", GOLD);
    }
#endif

    // the plate: text 2.6% of the screen's height (smaller, down to 80%, if it would not fit the gap)
    ImFont *font = ImGui::GetFont();
    float fs = .026f * h, line = .004f * h, padx = .5f * fs, pady = .22f * fs, tw = 0;
    for (int i = 0; i < nr; i++) tw += font->CalcTextSizeA(fs, FLT_MAX, 0, runs[i].text).x;
    float lo = .49f * w, hi = .76f * w;
    if (tw + 2 * padx > hi - lo) {
        float k = std::max((hi - lo) / (tw + 2 * padx), .8f);
        fs *= k, padx *= k, pady *= k, tw *= k;
    }
    float pw = tw + 2 * padx, ph = fs + 2 * pady + 2 * line * 1.7f;
    float x0 = pw <= hi - lo ? std::clamp(.62f * w - pw * .5f, lo, hi - pw) : (lo + hi - pw) * .5f, y0 = .01f * h;
    touch_fps_place(w, h, pw, ph, &x0, &y0);  // (moved in the layout editor: where the player put it)
    ImVec2 p0(x0, y0), p1(x0 + pw, y0 + ph);
    g_plate[0] = p0.x, g_plate[1] = p0.y, g_plate[2] = p1.x, g_plate[3] = p1.y, g_plate_at = SDL_GetTicksNS();
    ImDrawList *dl = ImGui::GetBackgroundDrawList();  // (under the port's menus and notices, as the touch controls)
    dl->AddRectFilled(p0, p1, WINE);
    dl->AddRect(p0, p1, IM_COL32(0, 0, 0, 255), 0.0f, line);
    dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + line), RED);
    dl->AddRectFilled(ImVec2(p0.x, p0.y + line), ImVec2(p1.x, p0.y + line * 1.7f), MOSS);
    dl->AddRectFilled(ImVec2(p0.x, p1.y - line * 1.7f), ImVec2(p1.x, p1.y - line), MOSS);
    dl->AddRectFilled(ImVec2(p0.x, p1.y - line), p1, RED);
    float x = p0.x + padx, y = p0.y + line * 1.7f + pady;
    for (int i = 0; i < nr; i++) {
        dl->AddText(font, fs, ImVec2(x + fs * .05f, y + fs * .06f), IM_COL32(0, 0, 0, 200), runs[i].text);
        dl->AddText(font, fs, ImVec2(x, y), runs[i].col, runs[i].text);
        x += font->CalcTextSizeA(fs, FLT_MAX, 0, runs[i].text).x;
    }
#ifdef _WIN32
    // Full: a plate under it, its width or more, with the GPU's line and the CPU's
    if (g_pc_level.load() >= 2 && pc_seq != 0) {
        char l1[200] = "", l2[240] = "";
        auto put = [&](char *line, size_t size, const char *text) {
            size_t n = strlen(line);
            if (n && n + 3 < size) strcat(line, " \xC2\xB7 "), n = strlen(line);
            if (n < size) snprintf(line + n, size - n, "%s", text);
        };
        char t[64], a[16], b2[16];
        if (std::isfinite(pc.gpu_use)) snprintf(t, sizeof t, es ? "uso %.0f %%" : "%.0f %% use", pc.gpu_use), put(l1, sizeof l1, t);
        if (std::isfinite(pc.gpu_ghz)) decimal(a, sizeof a, pc.gpu_ghz, es), snprintf(t, sizeof t, "%s GHz", a), put(l1, sizeof l1, t);
        if (std::isfinite(pc.vram_gb)) {
            decimal(a, sizeof a, pc.vram_gb, es), decimal(b2, sizeof b2, pc.vram_budget_gb, es);
            snprintf(t, sizeof t, es ? "VRAM %s de %s GB" : "VRAM %s of %s GB", a, b2), put(l1, sizeof l1, t);
        }
        if (std::isfinite(pc.cpu_use)) decimal(a, sizeof a, pc.cpu_use, es), snprintf(t, sizeof t, es ? "uso %s %%" : "%s %% use", a), put(l2, sizeof l2, t);
        if (std::isfinite(pc.cpu_ghz)) decimal(a, sizeof a, pc.cpu_ghz, es), snprintf(t, sizeof t, "%s GHz", a), put(l2, sizeof l2, t);
        if (std::isfinite(pc.ram_gb)) decimal(a, sizeof a, pc.ram_gb, es), snprintf(t, sizeof t, "RAM %s GB", a), put(l2, sizeof l2, t);
        if (std::isfinite(pc.game_ms)) {
            decimal(a, sizeof a, pc.game_ms, es);
            snprintf(t, sizeof t, es ? "juego %s ms" : "game %s ms", a), put(l2, sizeof l2, t);
        }
        const char *names[2] = {"GPU  ", "CPU  "};
        const char *texts[2] = {*l1 ? l1 : "--", *l2 ? l2 : "--"};
        float lw = 0;
        for (int k = 0; k < 2; k++)
            lw = std::max(lw, font->CalcTextSizeA(fs, FLT_MAX, 0, names[k]).x + font->CalcTextSizeA(fs, FLT_MAX, 0, texts[k]).x);
        const float qw = std::min(std::max(pw, lw + 2 * padx), .9f * w), qh = 2 * fs + 3 * pady + 2 * line * 1.7f;
        const float qx = std::clamp(p0.x + pw - qw, .01f * w, w - qw - .01f * w);
        ImVec2 q0(qx, p1.y + .004f * h), q1(qx + qw, p1.y + .004f * h + qh);
        dl->AddRectFilled(q0, q1, WINE);
        dl->AddRect(q0, q1, IM_COL32(0, 0, 0, 255), 0.0f, line);
        dl->AddRectFilled(q0, ImVec2(q1.x, q0.y + line), RED);
        dl->AddRectFilled(ImVec2(q0.x, q1.y - line), q1, RED);
        for (int k = 0; k < 2; k++) {
            float tx = q0.x + padx, ty = q0.y + line * 1.7f + pady + k * (fs + pady);
            dl->AddText(font, fs, ImVec2(tx, ty), GOLD, names[k]);
            tx += font->CalcTextSizeA(fs, FLT_MAX, 0, names[k]).x;
            dl->AddText(font, fs, ImVec2(tx + fs * .05f, ty + fs * .06f), IM_COL32(0, 0, 0, 200), texts[k]);
            dl->AddText(font, fs, ImVec2(tx, ty), INK, texts[k]);
        }
        g_plate[3] = q1.y;
    }
#endif
}
