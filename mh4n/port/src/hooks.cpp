#include "hooks.h"

#include "swkbd_bridge.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>
#include <fmt/format.h>
#if __has_include("mh4n/pacing_trace.h")
#include "mh4n/pacing_trace.h"
#define PACE_MARK(kind, value) mh4n::pacing::Mark(mh4n::pacing::kind, (value))
#define PACE_GAME_FRAME(value) mh4n::pacing::g_game_frame.store((value), std::memory_order_relaxed)
#else
#define PACE_MARK(kind, value) ((void)0)
#define PACE_GAME_FRAME(value) ((void)0)
#endif

// Guest memory is read and written like recompiled code does, in the mode the build selects
// (RC_PAGED for Azahar's page table, flat otherwise: see tools/recomp/runtime/recomp.h).
extern "C" {
#include "recomp.h"
}

namespace Port::Hooks {

namespace {

// ── The platform: everything the hooks use from the program that runs the game (platform.h).

/// What the hooks do when no platform was set: nothing.
class NullPlatform final : public Platform {
public:
    void Log(LogLevel, const std::string&) override {}
    std::string UserDir() override { return {}; }
    void SetRefreshRate(double) override {}
    void SetTopScreenRows(std::uint32_t) override {}
    double GameSeconds() override { return 0.0; }
    bool IsRunning() override { return false; }
    void RefreshScreenLayout() override {}
    void SetScreenOverlay(const ScreenOverlay&) override {}
    void SetTopScreenWidth(std::uint32_t) override {}
    bool SetOption(const std::string&, const std::string&) override { return false; }
    std::string GetOption(const std::string&) override { return {}; }
    void InstallNetSync(NetSyncMode, double, std::vector<std::string>&) override {}
    void VoteEndSkip(bool) override {}
    bool EndSkipAgreed() override { return true; }  // no room, no peers
};
NullPlatform null_platform;
Platform* platform = &null_platform;

Platform& P() {
    return *platform;
}

template <typename... Args>
void LogInfo(fmt::format_string<Args...> format, Args&&... args) {
    P().Log(LogLevel::Info, fmt::format(format, std::forward<Args>(args)...));
}

// USA v1.3.0 addresses and offsets (docs/research/system-services.md, widescreen.md).
constexpr u32 SMAIN_INSTANCE = 0x01057194;  // sMhMain*: the engine's main system (an sMain)
constexpr u32 SMAIN_RATE = 0x2c;            // f32: the unit of delta, in frames per second (60)
constexpr u32 SMAIN_CAP = 0x30;             // f32: frame-rate cap of the frame limiter
constexpr u32 SMAIN_FPS = 0x34;             // f32: measured frame rate, refreshed each second
constexpr u32 SMAIN_DELTA = 0x5c;           // f32: last frame's length in 1/rate s
constexpr u32 VBLANK_TOP = 0x0110d004;      // VBlank counters kept by the GX library
constexpr u32 VBLANK_BOTTOM = 0x0110d008;
constexpr u32 FRAMES_SUBMITTED = 0x010571d4;
constexpr u32 SLTRENDER_INSTANCE = 0x010572cc;
constexpr u32 SLTRENDER_DISPLAY_SKIP = 0x8aa8;  // u32[2] mDisplaySkip: top, bottom
// Render settings word: bits set by sLtRender's property setters (0x00bf685c registers them):
// 0x1 mVSyncEdit, 0x4 mTripleBufferRenderWaitEdit, 0x8 mUpsideToTopEdit, 0x10 mYFlipEdit,
// 0x40 mULCDEnableEdit, 0x100 mParallelRenderingEdit, 0x400 mFSAA_2D,
// 0x100000 mDitherDisplay0, 0x200000 mDitherDisplay1, 0x400000 mProfileRFPS
constexpr u32 SLTRENDER_RENDER_FLAGS = 0x6ba8;
constexpr u32 RENDER_DITHER = 0x300000;         // mDitherDisplay0 | mDitherDisplay1
constexpr u32 RENDER_STEREO = 0x40;             // mULCDEnableEdit: the top screen drawn once per eye
constexpr u32 RENDER_ANTIALIASING = 0x4400;     // mFSAA_2D (0x400) and 0x4000: the game's anti-aliasing
                                                // (the community's Trinity Disabler clears both; without
                                                // them the edges are jagged)
constexpr u32 SAREA_INSTANCE = 0x01057298;      // sMhArea*: the game areas alive (scene-observable.md)
constexpr u32 SCAMERA_INSTANCE = 0x0105729c;    // sMhCamera*
constexpr u32 SCAMERA_VIEWPORTS = 0x18;         // 8 viewports of 0x1a8 bytes
constexpr u32 VIEWPORT_SIZE = 0x1a8;
constexpr u32 VIEWPORT_CAMERA = 0x04;           // uCamera*
constexpr u32 VIEWPORT_DISPLAY = 0x16;          // u8: 0 top screen, 1 bottom screen
constexpr u32 VIEWPORT_SCENE_TEXTURE = 0x0c;    // sub-scene texture: region from its size
constexpr u32 VIEWPORT_Y0 = 0x28, VIEWPORT_Y1 = 0x30;  // region rows (the screen's width)
constexpr u32 VIEWPORT_MODE = 0x15;             // u8 mMode: 0 the whole 400 rows, 2..9 halves or
                                                // quarters of them, 1 and >= 10 a region its
                                                // owner sets (0xa: the virtual-rect mode)
constexpr u32 SLTRENDER_SCENE = 0x6c28;         // nDraw::Scene*: the main render target
constexpr u32 SLTRENDER_TOP_TRANSFER_Y = 0x6e1c;   // u32: first row of the top screen's image in
                                                   // the composited target (512 - 400 = 112)
constexpr u32 SLTRENDER_BOTTOM_TRANSFER_Y = 0x6e24;  // u32: the same for the bottom screen's (112)
constexpr u32 SCENE_TARGET = 0x50, TARGET_TEXTURES = 0x34, TEXTURE_SIZE = 0x18;
constexpr u32 TOP_ROWS = 400, LCD_COLUMNS = 240;   // top screen in framebuffer orientation
constexpr float NATIVE_ASPECT = 400.0f / 240.0f;

float ReadF32(RCtx* ctx, u32 addr) {
    const u32 v = rd32(ctx, addr);
    float f;
    std::memcpy(&f, &v, sizeof(f));
    return f;
}

void WriteF32(RCtx* ctx, u32 addr, float f) {
    u32 v;
    std::memcpy(&v, &f, sizeof(v));
    wr32(ctx, addr, v);
}

/// Options from <Azahar user dir>/mh4u.ini, overridden by environment variables.
struct Options {
    float fps_cap = 0;      // fps / MH4U_FPS: frame-rate cap, 0 = the game's own
    // pace / MH4U_PACE: what starts a frame. vblank: the top screen's emulated VBlank, as a console's vsync does (one
    // frame start a VBlank, or every n-th one for a cap of 1/n of the refresh rate; a frame that runs past its VBlank
    // starts the next at once, so a slow scene is not halved); limiter: the game's own limiter alone (its clock, so
    // its frames drift against the VBlank and the display's vsync, and a picture now and then is shown twice or never).
    // limiter unless set (the Android app sets vblank): the deterministic runs keep the reference's timeline
    bool pace_vblank = false;
    // limiter_sleep / MH4U_LIMITER_SLEEP: the game's limiter sleeps whole milliseconds and then spins on the clock for
    // the rest (1 to 2 ms a frame at full speed: a core busy for nothing, and heat); on: the port sleeps most of it,
    // to within a fraction of a millisecond, and the limiter spins only that (off unless set, as pace)
    bool limiter_sleep = false;
    // steady / MH4U_STEADY (with pace = vblank): when the game cannot hold the cap, it runs at an even half (a third...)
    // of it, every 2nd (3rd...) VBlank, instead of swinging between them (on a 120 Hz screen: 60 steady instead of 70 to
    // 110); back up once its frames have room for the faster rate again, or a try of it holds. Off unless set: the
    // player's choice
    bool steady = false;
    float refresh_rate = 0; // refresh_rate / MH4U_REFRESH_RATE: emulated LCD Hz, 0 = the 3DS's
                            // (or, with an fps above 60, that fps: see RefreshRate)
    float widescreen = 0;   // widescreen / MH4U_WIDESCREEN: top-screen aspect ratio, 0 = native
    bool widescreen_match = false;  // widescreen = match: the top screen takes the frontend's
                                    // window shape (SetWindowSize), resolved by Install
    bool wide_render = false;  // wide_render / MH4U_WIDE_RENDER: render the wider image itself
    u32 trace_every = 0;    // trace_frames / MH4U_TRACE_FRAMES: log frame timing every n frames
    bool trace_panels = false;  // trace_panels / MH4U_TRACE_PANELS: log touch-panel activity
    u32 render_set = 0, render_clear = 0;  // render_flags_set/_clear: bits of the render word
    bool dither = true;     // dither / MH4U_DITHER: the game's dither pattern on both screens
    bool stereo_3d = false; // stereo_3d / MH4U_STEREO_3D: the game's stereoscopic 3D (a second pass
                            // of the top screen for the right eye, even with the 3D slider at 0)
    bool antialiasing = true;  // antialiasing / MH4U_ANTIALIASING: the game's own anti-aliasing (off
                               // saves its pass at a high internal resolution, where it matters less)
    float fov = 1.0f;       // fov / MH4U_FOV: the top screen's cameras' vertical field of view times
                            // this, 0.5 to 2 (GetProjMatrix; the MHGU port's Field of view)
    u32 kick_button = 0;    // kick / MH4U_KICK: 3DS button that kicks (the game's pad bits)
    u32 target_button = 0;  // target / MH4U_TARGET: 3DS button for the target camera panel
    u32 skip_button = 0x800;  // skip / MH4U_SKIP: 3DS button that taps the touch screen's Skip
                              // once a quest is complete (ZR unless set: the kick does nothing then)
    u32 skip_end_buttons = 0x400 | 0x800;  // skip_end / MH4U_SKIP_END: buttons held together to end
                                           // the wait after a quest (ZL+ZR), with everyone's say-so online
    float skip_end_hold = 1.0f;  // skip_end_hold / MH4U_SKIP_END_HOLD: seconds they are held
    u32 skip_panel = 0xb;   // skip_panel: the touch-screen button whose tap the skip button makes
                            // (the panel manager's Skip, 0xb, as the stylus's tap on it shows)
    int skip_hint = 1;      // skip_hint / MH4U_SKIP_HINT: 0 off, 1 a hint on screen, 2 flashing
    ScreenOverlay touch_overlay;  // touch_overlay / MH4U_TOUCH_OVERLAY (custom layout)
    PanelView quest_overlay{true};   // quest_overlay / MH4U_QUEST_OVERLAY: in quests, the map
                                     // alone, at its size and place
    PanelView target_overlay{true};  // target_overlay / MH4U_TARGET_OVERLAY: with it, the
                                     // target camera panel in a view of its own
    int quest_overlay_test = -1;  // quest_overlay_test / MH4U_QUEST_OVERLAY_TEST: show the map's
                                  // part of slot n all the time (to check the drawing)
    bool hud_edges = true;  // hud_edges / MH4U_HUD_EDGES: with wide_render, HUD to the edges
    u32 trace_layout = 0;   // trace_layout / MH4U_TRACE_LAYOUT: log HUD objects every n frames
    NetSyncMode net_sync = NetSyncMode::Session;  // net_sync / MH4U_NET_SYNC
    float net_sync_slack = 0;  // net_sync_slack / MH4U_NET_SYNC_SLACK: ms, 0 = the default
    u32 filters_off = 0;    // bloom, motion_blur, depth_of_field, radial_blur, haze = off: the
                            // screen filters (FILTER_* bits) the port skips
    bool trace_filters = false;  // trace_filters / MH4U_TRACE_FILTERS: log the filters drawn
    bool trace_state = false;    // trace_state / MH4U_TRACE_STATE: log the game's state (GameState) as it changes
    bool trace_state_actions = false;  // trace_state = actions: the hunter's every action too
    u32 hud_probe = 0;      // hud_probe (tests): the top-screen layout object with this hash drawn off the screen
    // hud_<part> = dx dy: a part of the hunting HUD moved by this much (top-screen pixels, right and down: the app's
    // layout editor drags it; item_bar is hud_items as an earlier version wrote it)
    float hud_dx[HUD_PART_COUNT] = {}, hud_dy[HUD_PART_COUNT] = {};
};

// The screen filters an option can turn off (see SCREEN_FILTERS below), by option name.
constexpr u32 FILTER_BLOOM = 1u << 0, FILTER_MOTION_BLUR = 1u << 1, FILTER_DEPTH_OF_FIELD = 1u << 2,
              FILTER_RADIAL_BLUR = 1u << 3, FILTER_HAZE = 1u << 4;
constexpr std::pair<const char*, u32> FILTER_OPTIONS[] = {{"bloom", FILTER_BLOOM},
                                                          {"motion_blur", FILTER_MOTION_BLUR},
                                                          {"depth_of_field", FILTER_DEPTH_OF_FIELD},
                                                          {"radial_blur", FILTER_RADIAL_BLUR},
                                                          {"haze", FILTER_HAZE}};

/// The 3DS buttons by name, and the bit sPad::processInput gives each in the held word.
constexpr std::pair<const char*, u32> PAD_BITS[] = {
    {"A", 0x2000}, {"B", 0x4000},  {"X", 0x1000},  {"Y", 0x8000},  {"L", 0x100},
    {"R", 0x200},  {"ZL", 0x400},  {"ZR", 0x800},  {"START", 0x8}, {"UP", 0x10},
    {"DOWN", 0x40}, {"LEFT", 0x80}, {"RIGHT", 0x20}};

/// A 3DS button name -> the bit sPad::processInput gives it in the held word.
u32 PadBit(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::toupper(c); });
    for (const auto& [n, bit] : PAD_BITS) {
        if (name == n) {
            return bit;
        }
    }
    return 0;
}

/// The name of the 3DS button with that bit ("ZR"), or "".
std::string PadName(u32 bit) {
    for (const auto& [n, b] : PAD_BITS) {
        if (b == bit) {
            return n;
        }
    }
    return "";
}

/// "ZL+ZR" -> the bits of those buttons (0 for a name that isn't one, or "off").
u32 PadBits(const std::string& names) {
    u32 bits = 0;
    std::stringstream in(names);
    std::string name;
    while (std::getline(in, name, '+')) {
        name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char c) { return std::isspace(c); }),
                   name.end());
        const u32 bit = PadBit(name);
        if (bit == 0) {
            return 0;
        }
        bits |= bit;
    }
    return bits;
}

/// The names of the buttons in `bits`, "ZL+ZR".
std::string PadNames(u32 bits) {
    std::string names;
    for (const auto& [n, b] : PAD_BITS) {
        if (bits & b) {
            names += (names.empty() ? "" : "+") + std::string(n);
        }
    }
    return names;
}

bool IsOn(const std::string& value) {
    return value == "1" || value == "true" || value == "on" || value == "yes";
}

bool IsOff(const std::string& value) {
    return value == "0" || value == "false" || value == "off" || value == "no";
}

/// The HUD's parts the player can move (HUD_PARTS below), by their keys: hud_<key> in mh4u.ini.
constexpr const char* HUD_KEYS[HUD_PART_COUNT] = {"clock", "bars", "sharpness", "party", "map", "target", "items"};

/// "hud_bars" -> the part's index, else -1.
int HudPartIndex(const std::string& key) {
    if (key.compare(0, 4, "hud_") != 0) {
        return -1;
    }
    for (int k = 0; k < HUD_PART_COUNT; ++k) {
        if (key.compare(4, std::string::npos, HUD_KEYS[k]) == 0) {
            return k;
        }
    }
    return -1;
}
Options options;
u64 frames = 0;
int missing_hook_points = 0;  // in the code library Install saw last
std::atomic<bool> quest_map_off{false};  // ToggleQuestMap: the whole touch screen in quests too

// The frontend's surface shape (Port::SetWindowSize), for `widescreen = match`.
u32 window_width = 0;
u32 window_height = 0;

// An overlay or widescreen read at boot has to have the screen layout worked out again once
// the game draws (see Frame): Android works the layout out as the surface appears, before
// the game starts and Install() reads the options. The portrait flag is captured in Install
// too, which runs on the Java thread; the frame hook runs on the guest fiber and must not
// call JNI.
bool relayout_for_overlay = false;
bool relayout_portrait = false;

/// "16:9", "1.7778", "off"/"0" -> aspect ratio (0 = native).
float ParseAspect(const std::string& v) {
    const auto colon = v.find(':');
    if (colon != std::string::npos) {
        const float w = std::strtof(v.c_str(), nullptr);
        const float h = std::strtof(v.c_str() + colon + 1, nullptr);
        return h > 0 ? w / h : 0.0f;
    }
    return std::max(0.0f, std::strtof(v.c_str(), nullptr));
}

void Set(Options& o, const std::string& key, const std::string& value) {
    if (key == "fps") {
        o.fps_cap = std::max(0.0f, std::strtof(value.c_str(), nullptr));
    } else if (key == "pace") {
        o.pace_vblank = value != "limiter";
    } else if (key == "limiter_sleep") {
        o.limiter_sleep = IsOn(value);
    } else if (key == "steady") {
        o.steady = IsOn(value);
    } else if (key == "refresh_rate") {
        o.refresh_rate = std::max(0.0f, std::strtof(value.c_str(), nullptr));
    } else if (key == "widescreen") {
        // match: the frontend passes its window's shape (Port::SetWindowSize) and Install
        // resolves it; the other values are the aspect ratio itself
        o.widescreen_match = value == "match" || value == "auto" || value == "screen";
        o.widescreen = o.widescreen_match ? 0.0f : ParseAspect(value);
    } else if (key == "wide_render") {
        o.wide_render = IsOn(value);
    } else if (key == "dither") {
        o.dither = IsOn(value);
    } else if (key == "stereo_3d") {
        o.stereo_3d = IsOn(value);
    } else if (key == "antialiasing") {
        o.antialiasing = IsOn(value);
    } else if (key == "fov") {
        const float f = std::strtof(value.c_str(), nullptr);
        o.fov = f > 0.0f ? std::clamp(f, 0.5f, 2.0f) : 1.0f;
    } else if (key == "render_flags_set") {
        o.render_set = static_cast<u32>(std::strtoul(value.c_str(), nullptr, 0));
    } else if (key == "render_flags_clear") {
        o.render_clear = static_cast<u32>(std::strtoul(value.c_str(), nullptr, 0));
    } else if (key == "trace_panels") {
        o.trace_panels = IsOn(value);
    } else if (key == "kick") {
        o.kick_button = PadBit(value);
    } else if (key == "target") {
        o.target_button = PadBit(value);
    } else if (key == "skip") {
        o.skip_button = PadBit(value);
    } else if (key == "skip_end") {
        o.skip_end_buttons = PadBits(value);
    } else if (key == "skip_end_hold") {
        o.skip_end_hold = std::clamp(std::strtof(value.c_str(), nullptr), 0.2f, 10.0f);
    } else if (key == "skip_panel") {
        o.skip_panel = static_cast<u32>(std::clamp(std::strtol(value.c_str(), nullptr, 0), 0L, 0xffL));
    } else if (key == "skip_hint") {
        o.skip_hint = IsOff(value) ? 0 : value == "flash" ? 2 : 1;
    } else if (key == "touch_overlay") {
        o.touch_overlay = ParseOverlay(value);
    } else if (key == "quest_overlay") {
        o.quest_overlay = ParseQuestOverlay(value);
    } else if (key == "target_overlay") {
        o.target_overlay = ParseTargetOverlay(value);
    } else if (key == "quest_overlay_test") {
        // a slot the map can start in: 0, 1, 3 or 4
        const long slot = std::strtol(value.c_str(), nullptr, 10);
        o.quest_overlay_test = slot >= 0 && slot <= 4 && slot != 2 ? static_cast<int>(slot) : -1;
    } else if (key == "net_sync") {
        o.net_sync = ParseNetSyncMode(value);
    } else if (key == "net_sync_slack") {
        o.net_sync_slack = std::max(0.0f, std::strtof(value.c_str(), nullptr));
    } else if (key == "hud_edges") {
        o.hud_edges = IsOn(value);
    } else if (key == "trace_layout") {
        o.trace_layout = static_cast<u32>(std::max(0L, std::strtol(value.c_str(), nullptr, 10)));
    } else if (key == "trace_frames") {
        o.trace_every = static_cast<u32>(std::max(0L, std::strtol(value.c_str(), nullptr, 10)));
    } else if (key == "trace_filters") {
        o.trace_filters = IsOn(value);
    } else if (key == "trace_state") {
        o.trace_state = IsOn(value) || value == "actions";
        o.trace_state_actions = value == "actions";
    } else if (key == "hud_probe") {
        o.hud_probe = static_cast<u32>(std::strtoul(value.c_str(), nullptr, 16));
    } else if (const int part = key == "item_bar" ? HUD_PART_COUNT - 1 : HudPartIndex(key); part >= 0) {
        float dx = 0, dy = 0;
        std::istringstream(value) >> dx >> dy;
        o.hud_dx[part] = std::clamp(dx, -400.0f, 400.0f);
        o.hud_dy[part] = std::clamp(dy, -240.0f, 240.0f);
    } else {
        for (const auto& [name, bit] : FILTER_OPTIONS) {
            if (key == name) {
                // on (the game's own, as on a 3DS) unless turned off
                o.filters_off = IsOff(value) ? o.filters_off | bit : o.filters_off & ~bit;
            }
        }
    }
}

std::string Trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    const auto e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? "" : s.substr(b, e - b + 1);
}

Options LoadOptions(std::vector<std::string>& log) {
    Options o;
    const std::string path = P().UserDir() + "mh4u.ini";
    // through the platform, not std::ifstream: on Android the user folder is not the host's
    // root, and only the host's file layer translates the guest path to the file under it.
    std::vector<std::string> lines;
    if (P().ReadTextLines(path, lines)) {
        for (auto& line : lines) {
            line = Trim(line.substr(0, line.find('#')));
            const auto eq = line.find('=');
            if (eq != std::string::npos) {
                Set(o, Trim(line.substr(0, eq)), Trim(line.substr(eq + 1)));
            }
        }
        log.push_back("options from " + path);
    }
    const std::pair<const char*, const char*> env[] = {
        {"MH4U_FPS", "fps"},
        {"MH4U_REFRESH_RATE", "refresh_rate"},
        {"MH4U_PACE", "pace"},
        {"MH4U_LIMITER_SLEEP", "limiter_sleep"},
        {"MH4U_STEADY", "steady"},
        {"MH4U_WIDESCREEN", "widescreen"},
        {"MH4U_WIDE_RENDER", "wide_render"},
        {"MH4U_TRACE_FRAMES", "trace_frames"},
        {"MH4U_TRACE_PANELS", "trace_panels"},
        {"MH4U_KICK", "kick"},
        {"MH4U_TARGET", "target"},
        {"MH4U_SKIP", "skip"},
        {"MH4U_SKIP_END", "skip_end"},
        {"MH4U_SKIP_END_HOLD", "skip_end_hold"},
        {"MH4U_SKIP_HINT", "skip_hint"},
        {"MH4U_TOUCH_OVERLAY", "touch_overlay"},
        {"MH4U_QUEST_OVERLAY", "quest_overlay"},
        {"MH4U_TARGET_OVERLAY", "target_overlay"},
        {"MH4U_QUEST_OVERLAY_TEST", "quest_overlay_test"},
        {"MH4U_HUD_EDGES", "hud_edges"},
        {"MH4U_DITHER", "dither"},
        {"MH4U_STEREO_3D", "stereo_3d"},
        {"MH4U_ANTIALIASING", "antialiasing"},
        {"MH4U_FOV", "fov"},
        {"MH4U_RENDER_FLAGS_SET", "render_flags_set"},
        {"MH4U_RENDER_FLAGS_CLEAR", "render_flags_clear"},
        {"MH4U_TRACE_LAYOUT", "trace_layout"},
        {"MH4U_NET_SYNC", "net_sync"},
        {"MH4U_NET_SYNC_SLACK", "net_sync_slack"},
        {"MH4U_BLOOM", "bloom"},
        {"MH4U_MOTION_BLUR", "motion_blur"},
        {"MH4U_DEPTH_OF_FIELD", "depth_of_field"},
        {"MH4U_RADIAL_BLUR", "radial_blur"},
        {"MH4U_HAZE", "haze"},
        {"MH4U_TRACE_FILTERS", "trace_filters"},
        {"MH4U_TRACE_STATE", "trace_state"}};
    for (const auto& [var, key] : env) {
        if (const char* value = std::getenv(var)) {
            Set(o, key, value);
        }
    }
    return o;
}

/// The emulated screen refresh rate to use: refresh_rate when set; otherwise an fps above 60
/// (the game presents at most once per VBlank, so more frames need more VBlanks), else 0 (the
/// 3DS's 59.83 Hz).
float RefreshRate() {
    if (options.refresh_rate > 0) {
        return options.refresh_rate;
    }
    return options.fps_cap > 60.0f ? options.fps_cap : 0.0f;
}

constexpr u32 SVC_SLEEP_THREAD = 0x0010d934;  // nn::svc::SleepThread(s64 ns), r0:r1 (the limiter's own sleep)
constexpr float NATIVE_REFRESH = 59.831f;     // the 3DS's LCD

#if __has_include("mh4n/pacing_trace.h")
/// The emulated VBlank: its next host time and period (the GSP publishes them); false while not known.
bool VBlankGrid(u64& next, u64& period) {
    next = mh4n::pacing::g_next_vblank_ns.load(std::memory_order_relaxed);
    period = mh4n::pacing::g_vblank_period_ns.load(std::memory_order_relaxed);
    return next != 0 && period != 0;
}
/// The GPU's time so far (the renderer's timestamps), 0 while not known.
u64 GpuTotalNs() {
    return mh4n::pacing::g_gpu_total_ns.load(std::memory_order_relaxed);
}
/// The VBlanks a frame takes now, for the app's buffer times (0: the frames are not on the VBlank's grid).
void PublishFrameVBlanks(int n) {
    mh4n::pacing::g_frame_vblanks.store(n, std::memory_order_relaxed);
}
#else
bool VBlankGrid(u64&, u64&) {
    return false;
}
u64 GpuTotalNs() {
    return 0;
}
void PublishFrameVBlanks(int) {}
#endif

u64 HostNs() {
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count());
}

/// Calls a game function with r0:r1, its registers left as they were (UpdateFrameTiming's own address as the return).
void CallGame64(RCtx* ctx, u32 function, u64 arg) {
    u32 regs[16];
    std::copy(std::begin(ctx->r), std::end(ctx->r), regs);
    const u32 flags[4] = {ctx->fn, ctx->fz, ctx->fc, ctx->fv};
    const u32 expect_next = ctx->expect_next;
    ctx->expect_next = 0;
    ctx->r[0] = static_cast<u32>(arg);
    ctx->r[1] = static_cast<u32>(arg >> 32);
    ctx->r[14] = 0x006442f8;
    rc_call(ctx, function);
    std::copy(std::begin(regs), std::end(regs), ctx->r);
    ctx->fn = flags[0];
    ctx->fz = flags[1];
    ctx->fc = flags[2];
    ctx->fv = flags[3];
    ctx->expect_next = expect_next;
}

u64 pace_start_ns = 0;     // when the last frame started, on the VBlank's grid when it waited for one (pace = vblank)

/// steady: the VBlanks a frame now takes (1: the cap's own rate). A frame's cost is the longer of its own time (start to
/// the limiter, without the wait for the VBlank) and the GPU's time a frame (pacing::g_gpu_total_ns, over the last 8: its
/// timestamps come a frame or two late, a few at once): a GPU-bound phone's game thread only waits. A step down when more
/// than 1 frame in 10 of the last 60 cost more than the current interval; a step up when 95 % of the last 120 would have
/// fit the faster one with 15 % to spare. The GPU's clock drops with its load, so at the slower rate its time a frame
/// grows and may never show the room it has at full clock: after 20 s at a slower step the faster one is tried anyway
/// (back at once if it does not hold, and the next try waits twice as long, up to about 3 minutes).
struct Steady {
    int step = 1;
    u64 work[120] = {};
    int count = 0, next = 0;
    int since_change = 0;
    u64 gpu_at[9] = {};  // g_gpu_total_ns at the last 9 notes
    int gpu_count = 0;
    int probe_after = 0;   // frames at a slower step before the faster one is tried (0: not set yet)
    bool probing = false;  // the step was lowered by a try, not by the costs
    void Reset() { step = 1, count = next = since_change = gpu_count = probe_after = 0, probing = false; }
    void Note(u64 ns, u64 gpu_total) {
        for (int i = 8; i > 0; --i) gpu_at[i] = gpu_at[i - 1];
        gpu_at[0] = gpu_total;
        gpu_count = std::min(gpu_count + 1, 9);
        if (gpu_count == 9 && gpu_at[8] != 0 && gpu_total >= gpu_at[8]) {
            ns = std::max(ns, (gpu_total - gpu_at[8]) / 8);
        }
        work[next] = ns;
        next = (next + 1) % 120;
        count = std::min(count + 1, 120);
        ++since_change;
    }
    int Update(u64 period, int base, int max_step) {
        if (since_change < 60) return step;
        const auto nth = [&](int last, double share) {
            u64 v[120];
            const int n = std::min(last, count);
            for (int i = 0; i < n; ++i) v[i] = work[(next - 1 - i + 120) % 120];
            std::sort(v, v + n);
            return v[std::min(n - 1, static_cast<int>(n * share))];
        };
        const u64 interval = static_cast<u64>(base * step) * period;
        const int per_second = static_cast<int>(1e9 / static_cast<double>(interval));
        if (step < max_step && nth(60, 0.90) > interval + period / 8) {
            // (a failed try doubles the wait for the next one; slower for the costs: the first wait)
            probe_after = probing ? std::min(probe_after * 2, 9 * 20 * per_second / 2) : 0;
            probing = false;
            ++step, since_change = 0;
        } else if (step > 1 && since_change >= 120 &&
                   nth(120, 0.95) * 100 < static_cast<u64>(base * (step - 1)) * period * 85) {
            --step, since_change = 0, probe_after = 0, probing = false;
        } else if (step > 1) {
            if (probe_after == 0) probe_after = 20 * per_second;
            if (since_change >= probe_after) {
                --step, since_change = 0, probing = true;
            }
        } else if (since_change >= 600) {
            probing = false;  // (held the faster rate a while: a try that worked)
        }
        return step;
    }
};
Steady steady;
u64 limiter_done_ns = 0;   // when the limiter last let a frame go (limiter_sleep)
u64 sleep_late_ns = 300000;  // how late the sleeps wake up, a slow average (the margin left to the limiter's spin)

/// sMain::updateFrameTiming(this): the frame limiter. Delta time is counted in 1/60 s
/// whatever the cap, and gameplay scales with it, so the cap is what limits the frame rate.
///
/// Pacing (pace = vblank, the app's default): a frame that is early starts on the next VBlank when that comes by a
/// quarter of a period after the start the cap asks for (one a VBlank, or one every n when the cap is 1/n of the
/// refresh rate), else at that start. The game hands its picture over
/// during the next frame and the GSP latches it at a VBlank, so frames that start on the VBlank reach the screen one each
/// VBlank; frames timed by the limiter's own clock drift against the VBlank, and where they cross it a VBlank gets two
/// pictures (one never shown) and the next none (the last one shown twice). A frame that is on time or late starts the
/// next at once: no wait at all, so a scene that cannot hold the rate loses nothing (waiting for the next VBlank there
/// put a Moto g06 at 3x on whole steps of vsyncs: 48.7 -> 65.8 ms a frame). The VBlank's times come from the GSP
/// (pacing::g_next_vblank_ns), not from the game's VBlank counter, which moves only once the game's interrupt thread has
/// run. The limiter then only measures the frame (its cap is set half again above the rate: it never waits itself).
/// Otherwise (pace = limiter, or a cap the VBlank does not divide) the limiter waits, and with limiter_sleep most of
/// its wait is a sleep to within a fraction of a millisecond instead of its spin on the clock.
void UpdateFrameTiming(RCtx* ctx, RFunc original) {
    PACE_MARK(kLimiterIn, 0);
    const u32 cap_addr = ctx->r[0] + SMAIN_CAP;
    const float cap = options.fps_cap > 0 ? options.fps_cap : ReadF32(ctx, cap_addr);
    u64 vb_next = 0, vb_period = 0;
    const bool grid = options.pace_vblank && VBlankGrid(vb_next, vb_period);
    const float refresh = grid ? static_cast<float>(1e9 / static_cast<double>(vb_period))
                               : RefreshRate() > 0 ? RefreshRate() : NATIVE_REFRESH;
    const int every = cap > 0 ? std::max(1, static_cast<int>(std::lround(refresh / cap))) : 1;
    const bool on_vblank = grid && cap > 0 && std::fabs(refresh / every - cap) <= 0.02f * cap;
    if (on_vblank) {
        WriteF32(ctx, cap_addr, cap * 1.5f);
        const s64 now = static_cast<s64>(HostNs()), p = static_cast<s64>(vb_period);
        s64 start = now;
        int frame_every = every;
        if (options.steady && pace_start_ns != 0) {
            steady.Note(static_cast<u64>(now - static_cast<s64>(pace_start_ns)), GpuTotalNs());
            frame_every = every * steady.Update(vb_period, every, std::max(1, static_cast<int>(refresh / 20.0f) / every));
        } else {
            steady.Reset();
        }
        PublishFrameVBlanks(frame_every);
        if (pace_start_ns != 0) {
            // the start the cap asks for; on time or late: now. Early: on the next VBlank if it comes by a quarter of a
            // period after that start (the frames fall back onto the VBlank's grid whenever they have the slack), else
            // at that start (the cap's own cadence, off the grid for now)
            const s64 wanted = static_cast<s64>(pace_start_ns) + frame_every * p;
            if (now + 100000 < wanted) {
                // (the first VBlank after now and not more than a quarter of a period before that start: with a cap of
                // 1/n of the rate, one n VBlanks on)
                const s64 d = std::max(now + 100000, wanted - p / 4) - static_cast<s64>(vb_next);
                const s64 k = d >= 0 ? (d + p - 1) / p : -((-d) / p);
                const s64 vblank = static_cast<s64>(vb_next) + k * p;
                start = vblank <= wanted + p / 4 ? vblank : wanted;
                CallGame64(ctx, SVC_SLEEP_THREAD, static_cast<u64>(start - now + (start == vblank ? 150000 : 0)));
                PACE_MARK(kSleep, static_cast<u32>((start - now) / 1000));
            }
        }
        pace_start_ns = static_cast<u64>(start);
    } else {
        PublishFrameVBlanks(0);
        if (options.fps_cap > 0) {
            WriteF32(ctx, cap_addr, options.fps_cap);
        }
        if (options.limiter_sleep && cap > 0 && limiter_done_ns != 0) {
            const u64 period = static_cast<u64>(1e9 / cap);
            const u64 margin = std::clamp<u64>(sleep_late_ns + 100000, 200000, 2000000);
            const u64 start = HostNs(), target = limiter_done_ns + period;
            if (start + margin + 500000 < target) {
                const u64 want = target - margin - start;
                CallGame64(ctx, SVC_SLEEP_THREAD, want);
                const u64 slept = HostNs() - start;
                const u64 late = slept > want ? slept - want : 0;
                sleep_late_ns = (sleep_late_ns * 15 + std::min<u64>(late, 3000000)) / 16;
                PACE_MARK(kSleep, static_cast<u32>(want / 1000));
            }
        }
    }
    original(ctx);
    limiter_done_ns = HostNs();
    PACE_MARK(kLimiterOut, 0);
}

/// sMhMain::frame(this): one iteration of the main loop.
void WideTransfer(RCtx* ctx);
void UpdateQuestOverlay(RCtx* ctx);
void UpdateGameState(RCtx* ctx);
void ItemBarStep(RCtx* ctx);
void TraceFilters();

void Frame(RCtx* ctx, RFunc original) {
    PACE_MARK(kFrameStart, static_cast<u32>(frames));
    PACE_GAME_FRAME(static_cast<u32>(frames));
    if (relayout_for_overlay) {
        // The renderer works the screen layout out when the window changes. Android does that
        // as the surface appears, before the game starts and Install() reads the touch
        // overlay and widescreen options, so they would not take effect until the next resize
        // (seen on the device). Work the layout out again on the first frame, renderer up.
        relayout_for_overlay = false;
        P().RefreshScreenLayoutFor(relayout_portrait);
    }
    if (frames == 0 && RefreshRate() > 0) {
        // the game presents at most once per VBlank: more VBlanks allow more than 60 fps
        P().SetRefreshRate(RefreshRate());
    }
    const u32 clear = options.render_clear | (options.dither ? 0 : RENDER_DITHER) |
                      (options.stereo_3d ? 0 : RENDER_STEREO) | (options.antialiasing ? 0 : RENDER_ANTIALIASING);
    if (options.render_set || clear) {
        // the game reads these bits every frame (the community's quality cheats write the same
        // word); without the dither bits it draws no dither pattern, which an upscaled picture
        // shows as a screen-door grid; without the stereo bit it draws the top screen once (one
        // eye) instead of twice, as the community's "3D off" cheat does
        if (const u32 r = rd32(ctx, SLTRENDER_INSTANCE)) {
            const u32 flags = rd32(ctx, r + SLTRENDER_RENDER_FLAGS);
            wr32(ctx, r + SLTRENDER_RENDER_FLAGS, (flags & ~clear) | options.render_set);
        }
    }
    WideTransfer(ctx);
    if (const std::string line = Keyboard::Frame(ctx); !line.empty()) LogInfo("{}", line);  // (the phone's keyboard)
    original(ctx);
    ++frames;
    UpdateQuestOverlay(ctx);
    UpdateGameState(ctx);
    ItemBarStep(ctx);
    if (options.trace_filters) {
        TraceFilters();
    }
    if (options.trace_every == 0 || frames % options.trace_every != 0) {
        return;
    }
    const u32 main = rd32(ctx, SMAIN_INSTANCE);
    const u32 render = rd32(ctx, SLTRENDER_INSTANCE);
    if (main == 0 || render == 0) {
        return;
    }
    LogInfo("frame {}: delta {:.3f} fps {:.2f} rate {:.1f} cap {:.1f} vblanks {}/{} "
            "submitted {} display skip {}/{} render flags {:08X}",
            frames, ReadF32(ctx, main + SMAIN_DELTA), ReadF32(ctx, main + SMAIN_FPS),
            ReadF32(ctx, main + SMAIN_RATE), ReadF32(ctx, main + SMAIN_CAP), rd32(ctx, VBLANK_TOP),
            rd32(ctx, VBLANK_BOTTOM), rd32(ctx, FRAMES_SUBMITTED),
            rd32(ctx, render + SLTRENDER_DISPLAY_SKIP),
            rd32(ctx, render + SLTRENDER_DISPLAY_SKIP + 4),
            rd32(ctx, render + SLTRENDER_RENDER_FLAGS));
}

/// Whether `camera` is shown on the bottom screen (only top-screen views get widened).
bool OnBottomScreen(RCtx* ctx, u32 camera) {
    const u32 scamera = rd32(ctx, SCAMERA_INSTANCE);
    if (scamera == 0) {
        return false;
    }
    for (u32 i = 0; i < 8; ++i) {
        const u32 vp = scamera + SCAMERA_VIEWPORTS + i * VIEWPORT_SIZE;
        if (rd32(ctx, vp + VIEWPORT_CAMERA) == camera && rd8(ctx, vp + VIEWPORT_DISPLAY) == 1) {
            return true;
        }
    }
    return false;
}

/// The cameras' getProjMatrix(): returns the projection by value (result pointer in r0,
/// the camera in r1). Widescreen: divide m00 so the view gets wider by target/native
/// (Hor+); culling and stereo are derived from this matrix, so they follow. The frontend
/// shows the top screen stretched to the same aspect ratio (Azahar's aspect setting). An
/// aspect ratio narrower than the 3DS's (16:10, 4:3) narrows the view the same way, so that
/// the picture squeezed to that shape keeps its proportions.
u32 WideRows();

void GetProjMatrix(RCtx* ctx, RFunc original) {
    const u32 out = ctx->r[0];
    const u32 camera = ctx->r[1];
    original(ctx);
    // with wide_render the game's own aspect is already W/240; what is left (a target wider
    // than the widest render target, or narrower than the 3DS's) is done here and the frontend
    // stretches or squeezes the picture to it
    const float rendered = WideRows() ? WideRows() / static_cast<float>(LCD_COLUMNS) : NATIVE_ASPECT;
    if (options.widescreen > 0.0f && std::fabs(options.widescreen - rendered) > 0.001f &&
        !OnBottomScreen(ctx, camera)) {
        WriteF32(ctx, out, ReadF32(ctx, out) * rendered / options.widescreen);
    }
    // fov: the vertical angle times the factor, as the MHGU port's camera_fovy (the horizontal one
    // follows through the aspect). A perspective matrix only (m33 0; m11 = cot(fovy / 2)), on the
    // top screen; m00 and m11 scaled alike, so the aspect and an off-centre frustum stay as they are
    if (options.fov != 1.0f && !OnBottomScreen(ctx, camera) && ReadF32(ctx, out + 60) == 0.0f) {
        const float m11 = ReadF32(ctx, out + 20);
        if (m11 > 0.0f) {
            const float fovy = 2.0f * std::atan(1.0f / m11);
            const float wanted = std::clamp(fovy * options.fov, 0.05f, 3.0f);
            const float k = std::tan(fovy * 0.5f) / std::tan(wanted * 0.5f);
            WriteF32(ctx, out, ReadF32(ctx, out) * k);
            WriteF32(ctx, out + 20, m11 * k);
        }
    }
}

// ── True widescreen (wide_render, experimental): the top screen is rendered W pixels wide
// instead of 400 (docs/research/widescreen.md, "What has to grow"). The aspect ratio,
// projection, culling, GPU viewport and display transfer all follow from the sizes below.

/// W: the widescreen width rounded to whole 8-pixel tiles, or 0 when off. The scene target is
/// a texture with a power-of-two number of rows: 512 up to W = 512, then 1024 (twice the VRAM,
/// which the game has room for). PICA framebuffers have at most 1024 rows, so that is the limit;
/// a wider aspect ratio would get the rest from the projection (anamorphic).
constexpr u32 MAX_WIDE_ROWS = 1024;
u32 WideRows() {
    if (!options.wide_render || options.widescreen <= NATIVE_ASPECT) {
        return 0;
    }
    const u32 rows = static_cast<u32>(std::lround(LCD_COLUMNS * options.widescreen / 8.0f)) * 8;
    return std::min(rows, MAX_WIDE_ROWS);
}

/// nDraw::Scene::Scene(this, params): params {columns, rows, ...}; the main scene target.
void SceneCtor(RCtx* ctx, RFunc original) {
    const u32 rows = WideRows();
    const u32 params = ctx->r[1];
    if (rows && rd32(ctx, params) == LCD_COLUMNS && rd32(ctx, params + 4) == TOP_ROWS) {
        wr32(ctx, params + 4, rows);
        LogInfo("wide render: scene target {}x{}", rows, LCD_COLUMNS);
    }
    original(ctx);
}

/// nDraw::DisplayBuffer::init(this, columns, rows, format, ...): the top screen's buffers;
/// Azahar's LCD then has to show that many rows.
void DisplayBufferInit(RCtx* ctx, RFunc original) {
    const u32 rows = WideRows();
    if (rows && ctx->r[1] == LCD_COLUMNS && ctx->r[2] == TOP_ROWS) {
        ctx->r[2] = rows;
        P().SetTopScreenRows(rows);
    }
    original(ctx);
}

/// The rows each viewport with a region of its owner's was last given here, so that the region
/// is moved once and not again on every call.
struct OwnerRegion {
    u32 viewport, y0, y1;
};
std::vector<OwnerRegion> owner_regions;

/// sCamera::Viewport::updateRegion(this): top-screen viewports drawn into the scene target.
/// The regions updateRegion computes from the 400 rows (modes 0 and 2..9, every call) are
/// scaled to the wider target. A region its owner set (mode 1, >= 10), such as the weapon
/// preview at the smithy, stays as it is between calls; it frames a 3D view inside a HUD box,
/// and the HUD is centred in the wider picture, so it moves by half the extra rows, once (a
/// region over the whole 400 rows is widened instead).
void UpdateRegion(RCtx* ctx, RFunc original) {
    const u32 viewport = ctx->r[0];
    original(ctx);
    const u32 rows = WideRows();
    if (!rows || rd8(ctx, viewport + VIEWPORT_DISPLAY) != 0 ||
        rd32(ctx, viewport + VIEWPORT_SCENE_TEXTURE) != 0) {
        return;
    }
    const u32 render = rd32(ctx, SLTRENDER_INSTANCE);
    if (render == 0 || rd32(ctx, render + SLTRENDER_SCENE) == 0) {
        return;
    }
    const u8 mode = rd8(ctx, viewport + VIEWPORT_MODE);
    if (mode == 0 || (mode >= 2 && mode <= 9)) {
        for (const u32 field : {VIEWPORT_Y0, VIEWPORT_Y1}) {
            const u32 y = rd32(ctx, viewport + field);
            if (y <= TOP_ROWS) {
                wr32(ctx, viewport + field, y * rows / TOP_ROWS);
            }
        }
        return;
    }
    if (mode == 0xa) {
        return; // setupDraw puts its own rectangle in (a tunable makes it W rows)
    }
    const u32 y0 = rd32(ctx, viewport + VIEWPORT_Y0);
    const u32 y1 = rd32(ctx, viewport + VIEWPORT_Y1);
    auto it = std::find_if(owner_regions.begin(), owner_regions.end(),
                           [viewport](const OwnerRegion& r) { return r.viewport == viewport; });
    if (it != owner_regions.end() && it->y0 == y0 && it->y1 == y1) {
        return; // moved already
    }
    if (y0 > y1 || y1 > TOP_ROWS) {
        return; // not a region of the native 400 rows
    }
    const bool whole = y0 == 0 && y1 == TOP_ROWS;
    const u32 shift = (rows - TOP_ROWS) / 2;
    const u32 new_y0 = whole ? 0 : y0 + shift;
    const u32 new_y1 = whole ? rows : y1 + shift;
    wr32(ctx, viewport + VIEWPORT_Y0, new_y0);
    wr32(ctx, viewport + VIEWPORT_Y1, new_y1);
    if (it == owner_regions.end()) {
        owner_regions.push_back({viewport, new_y0, new_y1});
    } else {
        it->y0 = new_y0;
        it->y1 = new_y1;
    }
}

/// The display transfer copies the top screen from row `512 - 400` of the composited target
/// (the GPU draws from the bottom up, so the image ends at the last row). With W rows, the
/// image starts at `height - W`; the sLtRender constructor writes 112, so set it every frame.
/// The bottom screen is drawn into the same target afterwards, in the region (0, 80)-(240, 400)
/// (viewport 7), and copied from row `height - 400`: still 112 while the target has 512 rows,
/// but 624 once a W over 512 makes it 1024 rows tall.
void WideTransfer(RCtx* ctx) {
    const u32 rows = WideRows();
    const u32 render = rows ? rd32(ctx, SLTRENDER_INSTANCE) : 0;
    const u32 scene = render ? rd32(ctx, render + SLTRENDER_SCENE) : 0;
    const u32 target = scene ? rd32(ctx, scene + SCENE_TARGET) : 0;
    const u32 textures = target ? rd32(ctx, target + TARGET_TEXTURES) : 0;
    const u32 texture = textures ? rd32(ctx, textures) : 0;
    if (texture == 0) {
        return;
    }
    const u32 height = (rd32(ctx, texture + TEXTURE_SIZE) >> 16) & 0x7FFF;
    if (height >= rows && rd32(ctx, render + SLTRENDER_TOP_TRANSFER_Y) != height - rows) {
        wr32(ctx, render + SLTRENDER_TOP_TRANSFER_Y, height - rows);
        LogInfo("wide render: top screen transferred from row {} of {}", height - rows, height);
    }
    if (height >= TOP_ROWS && rd32(ctx, render + SLTRENDER_BOTTOM_TRANSFER_Y) != height - TOP_ROWS) {
        wr32(ctx, render + SLTRENDER_BOTTOM_TRANSFER_Y, height - TOP_ROWS);
        LogInfo("wide render: bottom screen transferred from row {} of {}", height - TOP_ROWS,
                height);
    }
}

// ── The HUD in wide render (docs/research/widescreen.md, "2D HUD"). The rLayout HUD keeps
// its 400-pixel layout space, centred in the wider picture. Each layout object's translation
// goes to the GPU as a shader constant written by 0x00ae65a4; the objects that make up the
// sides of the hunting HUD get theirs moved by half the extra width, out to the edges. They
// are told apart by the hash at the start of each object (its name's, it seems: the same
// objects have the same hashes in every run). Layout x grows to the left: pixel = 200 - x.
constexpr u32 CDRAW_OBJECT_CONSTANTS = 0x83c;  // cDraw: where 0x00ae65a4 put (x, y, z, 1), or 0
constexpr u32 CDRAW_SCREEN = 0x1bc;             // cDraw: bits 1-5, the screen being drawn (0 top)
constexpr u32 LAYOUT_TOUCH_SCREEN = 7;          //   (7 the touch screen's: viewport 7)
//
// The parts of the hunting HUD, which the player can move (the app's layout editor: hud_<key> in mh4u.ini) and which
// keep to their side of a wider picture (hud_edges). Each is a group of layout objects, by hash, and the box its art
// takes on the 400x240 screen where the game puts it: measured on the runner's screenshots (USA v1.3.0, the
// single-screen mod's target camera too) by drawing each object off the screen for a frame and taking the pixels that
// changed. The other top-screen objects of a hunt are the markers over the world (the A prompt, the arrows), the
// windows (a talk, the quest's start) and objects that drew nothing in the runs.
//   clock    the quest's clock: 2E7FA61D
//   bars     health (its fill, 8293D551) and stamina (the frame of both, D07F4AA2)
//   sharpness  the weapon's sharpness (BD56507D), a bow's gauge in its place (FF5A1702), and two objects drawn
//            beside them that showed nothing (B2DA1296, 138BE89E, a bow's)
//   party    the name plates: the hunter 8AC1CCC4, the Palico FDC6FC52, a second hunter and their Palico in local
//            play (64CFADE8, 13C89D7E), and four rows that were empty with two players (4639C2C3, 60497133,
//            174E41A5, 892AD406: a larger party's); the box is the first row's
//   map      the area's map at the top screen's right (not in the Arena): an object per area of each map, named
//            ui<map>a<area>_l_<layer> (the area itself was ui293a00_l_area_2, 65DD1792, on an expedition, with its
//            numbers ui293a00_l_suuji, DA514713; the names are in the code image, MapObjectHashes below), and 4273292C,
//            which drew nothing seen; the box is the area's on that expedition, a little larger
//   target   the target camera on the top screen (the single-screen mod's, 42A3E03D)
//   items    the item bar: the item selection (1833E2C8), its name (3C598C15), the selected item's count (D329C787,
//            drawn while the item has one: found on the phone with trace_layout, the box from the screenshots) and,
//            with a bow or a bowgun, the coating or the ammo above it (18AFCC8C)
// A part's box is the union of the boxes of its objects drawn in the last frames (the first of each part when none is).
constexpr int HUD_CLOCK = 0, HUD_BARS = 1, HUD_SHARPNESS = 2, HUD_PARTY = 3, HUD_MAP = 4, HUD_TARGET = 5, HUD_ITEMS = 6;
// -1 the left side, 1 the right (hud_edges), 0 where the game puts it in its 400 pixels: the map and the target camera,
// as before they were parts (at the right edge they would be under the touch controls' right thumb)
constexpr int HUD_SIDE[HUD_PART_COUNT] = {-1, -1, -1, -1, 0, 0, 1};
struct HudObject {
    u32 hash;
    int part;
    float x0, y0, x1, y1;  // its art at the game's place, 400x240 pixels (0: not known, it adds nothing to the box)
};
constexpr HudObject HUD_OBJECTS[] = {
    {0x2E7FA61D, HUD_CLOCK, 2, 1, 43, 42},
    {0xD07F4AA2, HUD_BARS, 44, 1, 277, 14},
    {0x8293D551, HUD_BARS, 44, 4, 277, 8},
    {0xBD56507D, HUD_SHARPNESS, 46, 15, 91, 27},
    {0xFF5A1702, HUD_SHARPNESS, 50, 17, 87, 30},
    {0xB2DA1296, HUD_SHARPNESS, 0, 0, 0, 0},
    {0x138BE89E, HUD_SHARPNESS, 0, 0, 0, 0},
    {0x8AC1CCC4, HUD_PARTY, 3, 46, 89, 57},
    {0xFDC6FC52, HUD_PARTY, 0, 0, 0, 0},
    {0x64CFADE8, HUD_PARTY, 0, 0, 0, 0},
    {0x13C89D7E, HUD_PARTY, 0, 0, 0, 0},
    {0x4639C2C3, HUD_PARTY, 0, 0, 0, 0},
    {0x60497133, HUD_PARTY, 0, 0, 0, 0},
    {0x174E41A5, HUD_PARTY, 0, 0, 0, 0},
    {0x892AD406, HUD_PARTY, 0, 0, 0, 0},
    {0, HUD_MAP, 300, 44, 366, 120},  // (the area objects: MapObjectHashes)
    {0x4273292C, HUD_MAP, 0, 0, 0, 0},
    {0x42A3E03D, HUD_TARGET, 361, 13, 388, 38},
    {0x1833E2C8, HUD_ITEMS, 296, 193, 347, 224},
    {0x3C598C15, HUD_ITEMS, 273, 223, 377, 238},
    {0x18AFCC8C, HUD_ITEMS, 269, 179, 389, 192},
    {0xD329C787, HUD_ITEMS, 331, 210, 351, 228},  // (the selected item's count, drawn while it has one)
};
constexpr int HUD_OBJECT_COUNT = static_cast<int>(std::size(HUD_OBJECTS));
constexpr int HUD_MAP_AREA = 15;  // (HUD_OBJECTS' index of the area objects)

/// The layout's name hash: the CRC-32 of the name without its final inversion (makeNameKey 0x005ed628).
u32 LayoutNameHash(std::string_view name) {
    u32 crc = 0xFFFFFFFF;
    for (const char c : name) {
        crc ^= static_cast<u8>(c);
        for (int b = 0; b < 8; ++b) {
            crc = crc & 1 ? (crc >> 1) ^ 0xEDB88320 : crc >> 1;
        }
    }
    return crc;
}

/// The hashes of every area map's objects (sorted): ui<270-299>a<00-12>_l_ and the layers the code image names (area,
/// area_1 .. area_6, icon, map1, map2, yaji, koware, suuji); the names that don't exist cost nothing.
const std::vector<u32>& MapObjectHashes() {
    static const std::vector<u32> hashes = [] {
        std::vector<u32> v;
        static constexpr const char* layers[] = {"area", "area_1", "area_2", "area_3", "area_4", "area_5", "area_6",
                                                 "icon", "map1", "map2", "yaji", "koware", "suuji"};
        for (int map = 270; map <= 299; ++map) {
            for (int area = 0; area <= 12; ++area) {
                for (const char* layer : layers) {
                    v.push_back(LayoutNameHash(fmt::format("ui{}a{:02}_l_{}", map, area, layer)));
                }
            }
        }
        std::sort(v.begin(), v.end());
        return v;
    }();
    return hashes;
}

/// HUD_OBJECTS' index of a top-screen layout object, or -1.
int HudObjectOf(u32 hash) {
    for (int i = 0; i < HUD_OBJECT_COUNT; ++i) {
        if (HUD_OBJECTS[i].hash == hash && hash != 0) {
            return i;
        }
    }
    return std::binary_search(MapObjectHashes().begin(), MapObjectHashes().end(), hash) ? HUD_MAP_AREA : -1;
}

bool WatchingQuestMap();                         // the quest's touch panels (below)
void NoteTouchScreenObject(float x, float y);
void NoteHudObject(int object);                  // the game's state (below)

/// 0x00ae65a4(object, cDraw): uploads a layout object's translation and colour.
void LayoutObjectConstants(RCtx* ctx, RFunc original) {
    const u32 object = ctx->r[0];
    const u32 draw = ctx->r[1];
    original(ctx);
    const u32 rows = options.hud_edges ? WideRows() : 0;
    const bool trace = options.trace_layout && frames % options.trace_layout == 0;
    const u32 constants = rd32(ctx, draw + CDRAW_OBJECT_CONSTANTS);
    if (constants == 0) {
        return;
    }
    const u32 screen = rd32(ctx, draw + CDRAW_SCREEN) >> 1 & 0x1f;
    const u32 hash = rd32(ctx, object);
    if (trace) {
        LogInfo("layout: frame {} object {:08X} hash {:08X} screen {} at {:.1f}, {:.1f}", frames, object,
                hash, screen, ReadF32(ctx, constants), ReadF32(ctx, constants + 4));
    }
    if (screen == LAYOUT_TOUCH_SCREEN) {
        if (WatchingQuestMap()) {
            NoteTouchScreenObject(ReadF32(ctx, constants), ReadF32(ctx, constants + 4));
        }
        return;
    }
    if (screen != 0) {
        return;
    }
    if (options.hud_probe != 0 && hash == options.hud_probe) {
        WriteF32(ctx, constants + 4, ReadF32(ctx, constants + 4) + 2000.0f);
    }
    const int object_index = HudObjectOf(hash);
    if (object_index < 0) {
        return;
    }
    // the part at its side's edge of a wider picture, and where the player put it (pixels right and down; layout x
    // grows to the left, y up)
    const int part = HUD_OBJECTS[object_index].part;
    const float dx = HUD_SIDE[part] * (rows != 0 ? (static_cast<float>(rows) - TOP_ROWS) / 2.0f : 0.0f) +
                     options.hud_dx[part];
    const float dy = options.hud_dy[part];
    if (dx != 0 || dy != 0) {
        WriteF32(ctx, constants, ReadF32(ctx, constants) - dx);
        WriteF32(ctx, constants + 4, ReadF32(ctx, constants + 4) - dy);
    }
    NoteHudObject(object_index);
}

/// Sets every tunable with this name (config/tunables.txt); returns how many there were.
u32 SetTunables(const RCodeInfo& info, std::string_view name, u32 value) {
    u32 set = 0;
    for (u32 i = 0; i < info.tunable_count; ++i) {
        if (info.tunable_names[i] && name == info.tunable_names[i]) {
            info.tunables[i] = value;
            ++set;
        }
    }
    return set;
}

/// Shows the top screen at the widescreen aspect ratio: every screen layout gets a top
/// screen that wide, and the single-screen layout uses the matching aspect setting.
void SetScreenAspect(float aspect, std::vector<std::string>& log) {
    P().SetTopScreenWidth(static_cast<u32>(std::lround(LCD_COLUMNS * aspect)));
    // the platform's names for the aspect ratios the single-screen layout knows
    const std::pair<float, const char*> known[] = {{16.0f / 9.0f, "16:9"},
                                                   {16.0f / 10.0f, "16:10"},
                                                   {21.0f / 9.0f, "21:9"},
                                                   {4.0f / 3.0f, "4:3"}};
    std::string choice = "stretch";
    for (const auto& [value, ratio] : known) {
        if (std::fabs(aspect - value) < 0.01f) {
            choice = ratio;
        }
    }
    P().SetOption("aspect_ratio", choice);
    log.push_back(choice == "stretch"
                      ? "screen layouts show the top screen wider; single screen stretches it to the window"
                      : "screen layouts show the top screen at that aspect ratio");
}

// ── Touch panels (docs/research/second-screen.md): tracing, to map panel ids to actions
// before binding them to buttons.
constexpr u32 PANEL_MANAGER = 0x0109a350;
constexpr u32 PANEL_FLAGS = 0xcc;     // u16: event flags of the last touch (0x0200 tapped)
constexpr u32 PANEL_ID = 0xce;        // u16: id of the panel touched
constexpr u32 SMHPAD_INJECTED = 0x3f0;   // u32: gameplay action bits set by panels
constexpr u32 SMHPAD_INJECTED_MENU = 0x3f4;
constexpr u32 SPAD_HELD = 0x2c + 0x60;   // u32: buttons held, as sPad::processInput encodes them
constexpr u32 ACTION_KICK = 0x0001;      // the action no button produces: panel 7, the kick
u32 last_panel_touch = 0xFFFFFFFF;
// The target camera panel (panel type 13). Each frame the quest HUD's target driver
// (0x00b94804) runs the panel's taps through 0x00b8d024, which also takes a request byte
// as a tap: with one monster it toggles the target camera on it, with two it cycles
// first, second, off (a monster not yet seen gets the "no" sound). The HUD clears the byte
// at the end of the same update (0x00b9dfe0), and the game sets it itself from a menu
// action in one control mode only. sCamera+0xed5 is the result: 0 off, n monster n.
constexpr u32 QUEST_HUD = 0x0108260c;     // static (getQuestHud 0x0013f230)
constexpr u32 HUD_TARGET_REQUEST = 0x27e;  // u8: toggle the target camera as a tap would
// The quest HUD's update (0x00b9dfe0) switches on its state: in state 2 (hunting) it runs the
// panels' state machine (0x00b9ba38) unless the panel manager is busy (0x002a4da0, as it is
// while the area changes), and that runs the small panels in its states 0 and 1, a big panel
// in 2 and the way back in 3. START takes the HUD to states 3 and 4 while the quest menu is
// open (its touch screen has Customize Panels, Panel Schemes and the like); 0 and 1 set the
// HUD up again afterwards. While someone talks the HUD isn't updated at all.
constexpr u32 HUD_STATE = 0x2a8;        // u8
constexpr u8 HUD_HUNTING = 2, HUD_MENU_OPENING = 3, HUD_MENU = 4;
constexpr u8 HUD_BOX_OPENING = 9, HUD_BOX = 10;  // the supply box (or the item box at camp) open, traced on the Galaxy
constexpr u32 HUD_PANEL_STATE = 0x2a9;  // u8
constexpr u8 PANELS_BIG = 2;
constexpr u32 PANEL_BUSY = 0xac;        // u8 (panel manager)
u32 target_held = 0;
std::atomic<bool> target_tap_asked{false};  // TapTargetCamera
// The skip after a quest (docs/research/second-screen.md, "Skipping the wait after a quest").
// Once the quest is complete the touch screen shows "Skip" (the panel manager's fixed
// button 0xb) while the scene of the monster's end plays, and a tap on it skips the scene;
// the game handles that tap inside PanelManager::processTouch (0x002951fc), in the state
// machine that follows the hit test (0x00292ea4: for 0xb the camera's skip), not through
// isTapped. So the skip button's tap is made where the hit test leaves its result:
// processTouch calls 0x0028f2f0 (whether touches are allowed) with the hit in r5, the index
// of the touched button plus event flags; the hook on it puts the Skip button's index there
// for three frames, "pressed", "held" and "released on it, activated", as the maintainer's
// log shows the stylus's tap (4001, 1001, 0A01), and processTouch goes on as for a real tap.
// UpdateSkip (below, once a frame) works out when the skip is offered.
constexpr u16 PANEL_TAP_FLAGS = 0x0a00;  // released on the button pressed (0x0800), activated
                                         // (0x0200)
constexpr u16 PANEL_PRESS_FLAGS = 0x4000;  // pressed on the button
constexpr u16 PANEL_HELD_FLAGS = 0x1000;   // still on it
constexpr u32 PANEL_MODE = 0xb1;         // u8: which buttons the touch screen has (the id table)
constexpr u32 PANEL_ID_TABLES = 0x00df5410;  // u32[4] per mode: the slot of each hit index
constexpr u32 PANEL_SLOT_IDS = 0xb4;     // u8[]: the id of the panel in each slot (processTouch 0x00295800)
constexpr u32 PANEL_TOUCH_ALLOWED = 0x0028f2f0;  // the call processTouch makes with the hit in r5
constexpr u32 PANEL_PROCESS_TOUCH = 0x002951fc;  // PanelManager::processTouch
constexpr u32 PANEL_TOUCH_ALLOWED_RETURN = 0x002957a4;  // its return address in processTouch
// That return address in the running version of the game: Install moves it with processTouch,
// whose code is the same in the other versions (only where it sits differs)
u32 panel_touch_allowed_return = PANEL_TOUCH_ALLOWED_RETURN;
// The wait itself (aQuest::playing 0x0065c18c, hooked as QuestPlaying below)
constexpr u32 AQUEST_WAIT_LEFT = 0x3c;    // s32: units of the wait left (loaded from the holder's
                                          // +0xad60: 20 s × sMain+0x2c)
constexpr u32 AQUEST_END_STAGE = 0x48;    // u32: the end sequence's stage, 0 while waiting
constexpr u32 QUEST_STATE_PHASE = 0x24;   // u8 in the state block: 0 playing, 1 waiting, then over
constexpr u8 QUEST_PHASE_WAITING = 1;
std::atomic<bool> skip_offered{false};   // SkipOffered()
bool skip_held = false;                  // the skip button, last frame
int skip_tap = 0;                        // frames of the tap left: 3 pressed, 2 held, 1 released
bool skip_tap_direct = false;            // the tap couldn't go through the hit test: write
                                         // the id and flags after processTouch instead
bool skip_tap_seen = false;              // isTapped has reported that tap
u32 skip_polls = 0;                      // isTapped asked about the button while offered
// The end of the wait (skip_end): the buttons held for skip_end_hold seconds ask for it; the
// wait is cut once every port console in the session asks (NetSync), by QuestPlaying
double end_hold_since = 0;               // emulated seconds when the hold began, 0 while not held
std::atomic<float> end_hold_fraction{0}; // EndSkipHold(): 0..1 of the hold, 1 once asked
std::atomic<bool> end_requested{false};  // EndSkipRequested(): this player asked
bool end_cut = false;                    // the wait was cut this quest
void UpdateSkip(RCtx* ctx);
double EmulatedSeconds();

/// The hit index that is the button `panel` on the touch screen now, or -1. processTouch
/// turns a hit index into a slot through the mode's table (0x00df5410) and the slot into
/// the panel's id through the manager's slot bytes (+0xb4 + slot); the maintainer's log
/// showed a hit on index 2 (slot 7 in mode 0) becoming panel 0, so both steps are taken here.
int PanelIndex(RCtx* ctx, u32 panel) {
    const u32 mode = rd8(ctx, PANEL_MANAGER + PANEL_MODE);
    for (u32 index = 0; index < 4; ++index) {
        const u32 slot = rd32(ctx, PANEL_ID_TABLES + mode * 16 + index * 4) & 0xFF;
        if (slot != 0 && rd8(ctx, PANEL_MANAGER + PANEL_SLOT_IDS + slot) == panel) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

/// 0x0028f2f0 (whether touches are allowed), called by processTouch with the hit test's
/// result in r5 (kept in r5 across the call): while the skip button's tap is being made, the
/// Skip button's index goes there, pressed one frame and released the next, so processTouch
/// handles it as it would the stylus.
void PanelTouchAllowed(RCtx* ctx, RFunc original) {
    const bool from_process_touch = ctx->r[14] == panel_touch_allowed_return;
    original(ctx);
    if (!from_process_touch || skip_tap == 0) {
        return;
    }
    const int index = PanelIndex(ctx, options.skip_panel);
    if (index < 0) {
        // no such button on this touch screen: the direct write after processTouch instead
        const u32 mode = rd8(ctx, PANEL_MANAGER + PANEL_MODE);
        LogInfo("skip: no hit index is button {} (mode {}: slots {} {} {} {}, their panels {} {} {} {}); the tap "
                "is written after processTouch instead",
                options.skip_panel, mode, rd32(ctx, PANEL_ID_TABLES + mode * 16) & 0xFF,
                rd32(ctx, PANEL_ID_TABLES + mode * 16 + 4) & 0xFF, rd32(ctx, PANEL_ID_TABLES + mode * 16 + 8) & 0xFF,
                rd32(ctx, PANEL_ID_TABLES + mode * 16 + 12) & 0xFF,
                rd8(ctx, PANEL_MANAGER + PANEL_SLOT_IDS + (rd32(ctx, PANEL_ID_TABLES + mode * 16) & 0xFF)),
                rd8(ctx, PANEL_MANAGER + PANEL_SLOT_IDS + (rd32(ctx, PANEL_ID_TABLES + mode * 16 + 4) & 0xFF)),
                rd8(ctx, PANEL_MANAGER + PANEL_SLOT_IDS + (rd32(ctx, PANEL_ID_TABLES + mode * 16 + 8) & 0xFF)),
                rd8(ctx, PANEL_MANAGER + PANEL_SLOT_IDS + (rd32(ctx, PANEL_ID_TABLES + mode * 16 + 12) & 0xFF)));
        skip_tap_direct = true;
        return;
    }
    const u16 flags = skip_tap == 3 ? PANEL_PRESS_FLAGS : skip_tap == 2 ? PANEL_HELD_FLAGS : PANEL_TAP_FLAGS;
    const u32 hit = static_cast<u32>(index) | flags;
    ctx->r[5] = hit;
    LogInfo("skip: {} the touch screen's button {} (hit {:04X}, touches {})",
            skip_tap == 3 ? "pressing" : skip_tap == 2 ? "holding" : "releasing", options.skip_panel, hit,
            ctx->r[0] ? "allowed" : "not allowed");
    --skip_tap;
}

/// PanelManager::processTouch(): runs the touch groups, leaves the touched panel's id.
void PanelProcessTouch(RCtx* ctx, RFunc original) {
    original(ctx);
    if (skip_tap_direct) {
        skip_tap_direct = false;
        skip_tap = 0;
        skip_tap_seen = false;
        wr8(ctx, PANEL_MANAGER + PANEL_ID, options.skip_panel);
        wr16(ctx, PANEL_MANAGER + PANEL_FLAGS, PANEL_TAP_FLAGS);
        LogInfo("skip: a tap on the touch screen's button {} (written after processTouch)",
                options.skip_panel);
        return;
    }
    const bool skipping = skip_offered.load(std::memory_order_relaxed);
    if (!options.trace_panels && !skipping) {
        return;
    }
    const u32 touch = rd16(ctx, PANEL_MANAGER + PANEL_ID) | rd16(ctx, PANEL_MANAGER + PANEL_FLAGS) << 16;
    if (touch != last_panel_touch && !(touch >> 16 & 0x8000)) {
        // while the skip is offered, taps are logged anyway: a tap on the touch screen's Skip
        // says which button it is (skip_panel)
        LogInfo("{}: touch on panel {} flags {:04X}", skipping ? "skip" : "panels",
                touch & 0xFF, touch >> 16);
    }
    last_panel_touch = touch;
}

/// PanelManager::isTapped(id): polled by the code that reacts to each panel.
void PanelIsTapped(RCtx* ctx, RFunc original) {
    const u32 id = ctx->r[0];
    const u32 caller = ctx->r[14];
    original(ctx);
    if (id == options.skip_panel && skip_offered.load(std::memory_order_relaxed)) {
        ++skip_polls;
        if (ctx->r[0] != 0 && !skip_tap_seen) {
            skip_tap_seen = true;
            LogInfo("skip: button {} tapped, handled at {:08X}", id, caller);
            return;
        }
    }
    if (options.trace_panels && ctx->r[0] != 0) {
        LogInfo("panels: panel {} tapped, handled at {:08X}", id, caller);
    }
}

/// sMhPad::update(this): turns pad state and the panels' injected bits into actions.
/// The kick button injects the kick for the next update, like the touch panel does (the
/// button keeps its own action too, so pick one the game doesn't use).
std::atomic<u32> host_triggers{0};  // SetHostTriggers: ZL 0x400 and ZR 0x800, as the held word has them

void ItemBarHold(RCtx* ctx);  // (the item bar by touch, below)

void MhPadUpdate(RCtx* ctx, RFunc original) {
    const u32 pad = ctx->r[0];
    if (options.trace_panels) {
        const u32 gameplay = rd32(ctx, pad + SMHPAD_INJECTED);
        const u32 menu = rd32(ctx, pad + SMHPAD_INJECTED_MENU);
        if (gameplay || menu) {
            LogInfo("panels: injected actions {:04X}, menu actions {:04X}", gameplay, menu);
        }
    }
    original(ctx);
    PACE_MARK(kPadRead, rd32(ctx, pad + SPAD_HELD));  // (the pacing trace: the frame that reads a press)
    // (the app's ZL and ZR too: the game has them only through the Circle Pad Pro, with its option on)
    const u32 held = rd32(ctx, pad + SPAD_HELD) | host_triggers.load(std::memory_order_relaxed);
    if (options.kick_button && (held & options.kick_button)) {
        wr32(ctx, pad + SMHPAD_INJECTED, rd32(ctx, pad + SMHPAD_INJECTED) | ACTION_KICK);
    }
    if (options.target_button) {
        // a press, not a hold: one request per press, as one tap
        const u32 now = held & options.target_button;
        if (now && !target_held) {
            wr8(ctx, QUEST_HUD + HUD_TARGET_REQUEST, 1);
        }
        target_held = now;
    }
    if (target_tap_asked.exchange(false) && rd8(ctx, QUEST_HUD + HUD_STATE) == HUD_HUNTING) {
        wr8(ctx, QUEST_HUD + HUD_TARGET_REQUEST, 1);  // (the app's tap on the icon; only while hunting: the HUD clears it)
    }
    ItemBarHold(ctx);
    UpdateSkip(ctx);
    const bool offered = skip_offered.load(std::memory_order_relaxed);
    // the end of the wait: the combo held for skip_end_hold seconds, once per quest
    const u32 combo = options.skip_end_buttons;
    const bool combo_held = combo && (held & combo) == combo;
    if (offered && combo_held && !end_requested.load(std::memory_order_relaxed)) {
        const double now = EmulatedSeconds();
        if (end_hold_since == 0) {
            end_hold_since = now;
        }
        const float fraction = std::min(1.0f, static_cast<float>((now - end_hold_since) / options.skip_end_hold));
        end_hold_fraction.store(fraction, std::memory_order_relaxed);
        if (fraction >= 1.0f) {
            end_requested.store(true, std::memory_order_relaxed);
            P().VoteEndSkip(true);
            LogInfo("skip: {} held {:.1f} s: asking to end the wait", PadNames(combo),
                    options.skip_end_hold);
        }
    } else {
        end_hold_since = 0;
        if (!end_requested.load(std::memory_order_relaxed)) {
            end_hold_fraction.store(0.0f, std::memory_order_relaxed);
        }
    }
    // the touch screen's Skip: a press while it is offered, one tap per press; not while the
    // rest of the combo is held (a combo that includes this button is the end, not the skip)
    const u32 rest = combo & ~options.skip_button;
    const bool rest_held = rest && (held & rest) == rest;
    const bool skip_now = options.skip_button && (held & options.skip_button);
    if (skip_now && !skip_held && offered && !rest_held) {
        skip_tap = 3;
        skip_tap_seen = false;
        LogInfo("skip: {} pressed", PadName(options.skip_button));
    }
    skip_held = skip_now;
}

/// aQuest's playing state (this, state block), once a frame during a quest. Once the quest is
/// complete, state +0x24 is 1 while the wait before the return runs (the carving time): +0x40
/// of the quest area accumulates the frame's delta (sMain+0x5c) and takes one off the counter
/// at +0x3c per whole unit; when the counter is 0 or less the game sets sGameControl+0x2f89
/// and its end sequence (0x0065a378, stage at +0x48, 0 while waiting) goes on to the "Quest
/// Complete" stamp. Once this player has asked to end the wait (skip_end held) and every port
/// console in the session has too, the counter is set to 0 here: what running out of time does.
void QuestPlaying(RCtx* ctx, RFunc original) {
    const u32 quest = ctx->r[0];
    const u32 state = ctx->r[1];
    if (end_requested.load(std::memory_order_relaxed) && !end_cut && P().EndSkipAgreed()) {
        const std::int32_t left = static_cast<std::int32_t>(rd32(ctx, quest + AQUEST_WAIT_LEFT));
        if (rd8(ctx, state + QUEST_STATE_PHASE) == QUEST_PHASE_WAITING &&
            rd32(ctx, quest + AQUEST_END_STAGE) == 0 && left > 0) {
            wr32(ctx, quest + AQUEST_WAIT_LEFT, 0);
            end_cut = true;
            LogInfo("skip: everyone asked; the wait ended with {} units left", left);
        }
    }
    original(ctx);
}

// ── Quest start (docs/research/system-services.md, "A loading race at the start of a
// quest"). Right before a quest starts, 0x0069b21c builds a list of quest-file resources by
// asking the resource manager for each id of a table (0x0069af14); 0x00b7c010 then has
// 0x0069b4d0 copy the quest's data out of that list and releases it (0x006bc838). A file
// that is still being set up at that moment is left out of the list. At 60 fps that is a
// quest nobody starts; at higher frame rates it is often the one being started, which
// then runs on an empty stage: the hunter falls into a void and the quest fails. Until its
// quest is in the list, startQuest is put off a frame and the list released, so that the
// game builds it again.
constexpr u32 QUEST_HOLDER = 0x00fbeffc;      // the object that holds the current quest
constexpr u32 QUEST_TABLE = 0xe604;           // holder: u16 quest ids, 0-terminated
constexpr u32 QUEST_LIST = 0xe608;            // holder: the resources found for them
constexpr u32 QUEST_LIST_COUNT = 0xe610;
constexpr u32 RESOURCE_BUFFER = 0x64;         // resource: its file contents
constexpr u32 QUEST_FLOW_PARAMS = 0x00edbe90; // [this]+0x9788: what 0x00b7c010 starts
constexpr u32 QUEST_LIST_RELEASE = 0x006bc838;
constexpr u32 QUEST_START = 0x00b7c010;
constexpr u32 QUEST_HOLD_MAX_FRAMES = 600;

/// The quest id 0x00b7c010 is about to set up (its three branches), or 0.
u32 NextQuestId(RCtx* ctx) {
    const u32 base = rd32(ctx, QUEST_FLOW_PARAMS);
    if (!base) {
        return 0;
    }
    const u32 p = base + 0x9788;
    if (rd8(ctx, p + 0x18)) {
        const u32 id = rd16(ctx, p + 0x1a);
        if (id != 0xffff) {
            return id;
        }
        const u32 which = rd8(ctx, p + 0x19);
        return which < 3 ? 0xafc8 + which : 0;
    }
    return rd8(ctx, p + 0x89) ? rd16(ctx, p + 0x8c) : rd16(ctx, p + 0x3c);
}

/// Whether the list was built from a table with quest `id` but doesn't have it: the quest's
/// file was not ready when 0x0069af14 asked for it (0x006ae870 reads an entry's quest id at
/// buffer + [buffer] + 0x20).
bool QuestLeftOut(RCtx* ctx, u32 holder, u32 id) {
    const u32 list = rd32(ctx, holder + QUEST_LIST);
    const u32 table = rd32(ctx, holder + QUEST_TABLE);
    const u32 count = list ? rd32(ctx, holder + QUEST_LIST_COUNT) : 0;
    if (!list || !table || count > 4096) {
        return false;
    }
    bool in_table = false;
    for (u32 i = 0; i < 4096; ++i) {
        const u32 t = rd16(ctx, table + 2 * i);
        if (t == 0) {
            break;
        }
        in_table = in_table || t == id;
    }
    if (!in_table) {
        return false;
    }
    for (u32 i = 0; i < count; ++i) {
        const u32 res = rd32(ctx, list + 4 * i);
        const u32 data = res ? rd32(ctx, res + RESOURCE_BUFFER) : 0;
        const u32 header = data ? rd32(ctx, data) : 0;
        if (header != 0 && header < 0x10000 && rd16(ctx, data + header + 0x20) == id) {
            return false;
        }
    }
    return true;
}

/// Calls a recompiled game function with one argument, leaving the caller's registers as
/// they were.
void CallGame(RCtx* ctx, u32 function, u32 arg) {
    u32 regs[16];
    std::copy(std::begin(ctx->r), std::end(ctx->r), regs);
    const u32 flags[4] = {ctx->fn, ctx->fz, ctx->fc, ctx->fv};
    const u32 expect_next = ctx->expect_next;
    ctx->expect_next = 0;
    ctx->r[0] = arg;
    ctx->r[14] = QUEST_START;
    rc_call(ctx, function);
    std::copy(std::begin(regs), std::end(regs), ctx->r);
    ctx->fn = flags[0];
    ctx->fz = flags[1];
    ctx->fc = flags[2];
    ctx->fv = flags[3];
    ctx->expect_next = expect_next;
}

u32 quest_start_held = 0;

/// QuestFlow::startQuest(this).
void StartQuest(RCtx* ctx, RFunc original) {
    const u32 holder = rd32(ctx, QUEST_HOLDER);
    const u32 id = NextQuestId(ctx);
    if (holder && id && quest_start_held < QUEST_HOLD_MAX_FRAMES && QuestLeftOut(ctx, holder, id)) {
        if (quest_start_held++ == 0) {
            LogInfo("quest {}: its file wasn't ready for the quest list; building the list again",
                    id);
        }
        CallGame(ctx, QUEST_LIST_RELEASE, holder);
        rc_hook_return(ctx);
        return;
    }
    if (quest_start_held) {
        LogInfo("quest {}: started after {} more frames{}", id, quest_start_held,
                quest_start_held >= QUEST_HOLD_MAX_FRAMES ? " (gave up)" : "");
        quest_start_held = 0;
    }
    original(ctx);
}

// ── The wait after a quest is complete (docs/research/second-screen.md, "Skipping the wait
// after a quest"). The quest holder's status byte says how the quest stands: 3 and 7 complete
// (0x006a62c8 reads 3, 4 and 7 as complete, 5 and 6 as failed and abandoned). From the moment
// it is complete, the quest area (aQuest, 0x0065c18c) counts down the 20 seconds before the
// return (from the holder's +0xad60, set by 0x006b2de0); when they are over it sets
// sGameControl+0x2f89 and its end sequence (0x0065a378) plays the "Quest Complete" stamp and
// sets the status to 4. So the skip is offered while the status is 3 or 7, until 0x2f89 is
// set (if it wasn't at the start), and for 25 seconds at most: quests that end at once (the
// arena, training) leave the status as it was without a wait.
constexpr u32 QUEST_STATUS = 0xad24;             // u8 in the quest holder
constexpr u32 GAME_CONTROL = 0x00fb6b7c;         // sGameControl*
constexpr u32 GAME_CONTROL_RETURN_DUE = 0x2f89;  // u8: the wait is over
constexpr double SKIP_MOST_SECONDS = 25.0;
bool skip_complete = false;     // last frame: the quest complete
double skip_since = 0;          // when it was
u32 skip_due_at_start = 0;      // GAME_CONTROL_RETURN_DUE then

/// Once a frame (sMhPad::update): whether the game offers the skip now.
void UpdateSkip(RCtx* ctx) {
    const u32 holder = rd32(ctx, QUEST_HOLDER);
    const u32 status = holder ? rd8(ctx, holder + QUEST_STATUS) : 0;
    const bool complete = status == 3 || status == 7;
    const u32 control = rd32(ctx, GAME_CONTROL);
    const u32 due = control ? rd8(ctx, control + GAME_CONTROL_RETURN_DUE) : 0;
    const double now = EmulatedSeconds();
    if (complete && !skip_complete) {
        skip_since = now;
        skip_due_at_start = due;
        skip_polls = 0;
        LogInfo("skip: quest complete (status {}, return {})", status, due ? "due" : "not due");
    }
    skip_complete = complete;
    const bool offered =
        complete && now - skip_since < SKIP_MOST_SECONDS && !(skip_due_at_start == 0 && due != 0);
    if (offered != skip_offered.load(std::memory_order_relaxed)) {
        skip_offered.store(offered, std::memory_order_relaxed);
        if (offered) {
            LogInfo("skip: offered ({} taps the touch screen's button {})",
                    options.skip_button ? PadName(options.skip_button) : "no button", options.skip_panel);
        } else {
            LogInfo("skip: no longer offered after {:.1f} s (status {}, return {}); the game asked about "
                    "button {} {} times",
                    now - skip_since, status, due ? "due" : "not due", options.skip_panel, skip_polls);
        }
        skip_tap = 0;
        skip_tap_direct = false;
        end_hold_since = 0;
        end_hold_fraction.store(0.0f, std::memory_order_relaxed);
        if (end_requested.exchange(false)) {
            P().VoteEndSkip(false);
        }
        end_cut = false;
    }
}

// ── The touch screen during a quest (docs/research/second-screen.md, "The quest's touch
// panels"). While the quest's small touch panels are up, the HUD's panel state machine
// (0x00b9ba38) runs them once a frame through 0x00b92e24; it doesn't while a big panel
// (the full map and the like) covers the touch screen. The panels sit in six slots, two
// columns of three under the touch screen's top bar; a panel takes one slot, or two one
// above the other (the slot below is then marked hidden), or two side by side. The map is
// one shape only, two slots tall. With quest_overlay, the touch screen in the corner of
// the custom layout shows the map alone while the panels are up, and with target_overlay the
// target camera panel in a view of its own beside it.
constexpr u32 QUEST_PANELS = 0x01082cec;  // (getter 0x001287a4)
constexpr u32 PANEL_TYPES = 0x7a;         // u16 per slot
constexpr u32 PANEL_HIDDEN = 0xb1;        // u8: a bit per slot covered by the panel before it
constexpr u32 PANEL_SLOTS = 6;
constexpr u32 PANEL_TYPE_MAP = 6;
constexpr u32 PANEL_TYPE_TARGET = 13;     // one slot only
constexpr int SLOT_WIDTH = PANEL_SLOT_WIDTH, SLOT_HEIGHT = PANEL_SLOT_HEIGHT;  // touch-screen pixels
constexpr int SLOTS_TOP = PANEL_SLOTS_TOP;
bool quest_panels_ran = false;  // this frame
int quest_map_slot = -1;        // the map's slot the last time the panels ran, -1 none
int quest_target_slot = -1;     // the target camera's then, -1 none
int map_slot_seen = -1;         // the last slot a quest had the map in (the layout editor's)
int target_slot_seen = -1;      // and the target camera
bool quest_map_drawn = false;   // this frame: the map panel drawn on the touch screen
bool layout_editing = false;    // SetLayoutEditing
QuestOverlayChoice quest_choice;
int applied_map_slot = -1, applied_target_slot = -1;  // what the touch overlay shows
u32 last_panel_states = 0xFFFFFFFF;  // trace_panels: the HUD's states last frame

/// Whether the touch screen's layout objects are looked at for the map panel's. While someone
/// talks the HUD isn't updated, so the panels don't run, but the touch screen still shows them.
bool WatchingQuestMap() {
    return quest_map_slot >= 0 && !quest_map_drawn && options.quest_overlay.on &&
           options.touch_overlay.height > 0;
}

/// A layout object drawn on the touch screen at (x, y) (layout units, which grow to the left
/// and up): every object of a panel sits at its slot's corner.
void NoteTouchScreenObject(float x, float y) {
    if (!WatchingQuestMap()) {
        return;
    }
    const int column = quest_map_slot / 3, row = quest_map_slot % 3;
    if (std::fabs(x + static_cast<float>(SLOT_WIDTH * column)) < 0.5f &&
        std::fabs(y + static_cast<float>(SLOTS_TOP + SLOT_HEIGHT * row)) < 0.5f) {
        quest_map_drawn = true;
    }
}

/// QuestHud::updatePanels(): the quest's small touch panels, once a frame while they are up.
void QuestPanels(RCtx* ctx, RFunc original) {
    original(ctx);
    quest_panels_ran = true;
    quest_map_slot = -1;
    quest_target_slot = -1;
    const u32 hidden = rd8(ctx, QUEST_PANELS + PANEL_HIDDEN);
    for (u32 slot = 0; slot < PANEL_SLOTS; ++slot) {
        if (hidden >> slot & 1) {
            continue;
        }
        const u32 type = rd16(ctx, QUEST_PANELS + PANEL_TYPES + 2 * slot);
        if (type == PANEL_TYPE_MAP && slot % 3 < 2 && quest_map_slot < 0) {
            quest_map_slot = static_cast<int>(slot);
        } else if (type == PANEL_TYPE_TARGET && quest_target_slot < 0) {
            quest_target_slot = static_cast<int>(slot);
        }
    }
    if (quest_map_slot >= 0) {
        map_slot_seen = quest_map_slot;
        target_slot_seen = quest_target_slot;
    }
}

/// Emulated time in seconds.
double EmulatedSeconds() {
    return P().GameSeconds();
}

/// The map's slot the touch overlay shows, -1 for the whole touch screen.
int ShownMapSlot() {
    if (options.touch_overlay.height <= 0 || !options.quest_overlay.on) {
        return -1;
    }
    if (layout_editing) {
        return map_slot_seen >= 0 ? map_slot_seen : 0;  // the default layout's (Type A)
    }
    return quest_choice.Shown();
}

/// The target camera panel's slot shown beside the map in `map_slot`, -1 none.
int ShownTargetSlot(int map_slot) {
    if (map_slot < 0 || !options.target_overlay.on) {
        return -1;
    }
    const bool stand_in = layout_editing || options.quest_overlay_test >= 0;
    const int seen = layout_editing ? target_slot_seen : quest_target_slot;
    if (seen >= 0 || !stand_in) {
        return seen;
    }
    // the slot of the map's column the map leaves (in the default layout, the target's)
    return map_slot / 3 * 3 + (map_slot % 3 == 0 ? 2 : 0);
}

/// The touch overlay as the options and the quest's panels now say.
void ApplyQuestOverlay(bool force) {
    const int map = ShownMapSlot();
    const int target = ShownTargetSlot(map);
    if (!force && map == applied_map_slot && target == applied_target_slot) {
        return;
    }
    const bool changed = map != applied_map_slot || target != applied_target_slot;
    applied_map_slot = map;
    applied_target_slot = target;
    P().SetScreenOverlay(map < 0 ? options.touch_overlay
                                 : QuestOverlay(options.touch_overlay, map, options.quest_overlay,
                                                target, options.target_overlay));
    if (frames > 0 && P().IsRunning()) {
        // the screen layout is worked out when it changes, so work it out again
        P().RefreshScreenLayout();
    }
    if (!changed || options.touch_overlay.height <= 0) {
        return;
    }
    if (map < 0) {
        LogInfo("touch overlay: the whole touch screen");
    } else if (target < 0) {
        LogInfo("touch overlay: the quest's map (panel slot {})", map);
    } else {
        LogInfo("touch overlay: the quest's map (panel slot {}) and target camera (slot {})", map,
                target);
    }
}

/// Once a frame: the part of the touch screen the touch overlay shows in quests (see
/// QuestOverlayChoice).
void UpdateQuestOverlay(RCtx* ctx) {
    const bool panels = quest_panels_ran;
    const u8 hud = rd8(ctx, QUEST_HUD + HUD_STATE);
    const bool map_drawn = quest_map_drawn && hud == HUD_HUNTING;
    quest_panels_ran = false;
    quest_map_drawn = false;
    const u8 panel_state = rd8(ctx, QUEST_HUD + HUD_PANEL_STATE);
    if (options.trace_panels) {
        const u32 states = hud | panel_state << 8 | rd8(ctx, PANEL_MANAGER + PANEL_BUSY) << 16 |
                           (panels ? 1u : 0u) << 24 | (map_drawn ? 1u : 0u) << 25;
        if (states != last_panel_states) {
            LogInfo("panels: at {:.2f} s HUD state {}, panel state {}, panel manager {}, small panels {}, "
                    "map {}, target camera {}",
                    EmulatedSeconds(), hud, panel_state, states >> 16 & 0xFF ? "busy" : "free",
                    panels ? "ran" : "not run", map_drawn ? "drawn" : "not drawn",
                    panels ? fmt::format("in slot {}", quest_target_slot) : std::string("-"));
            last_panel_states = states;
        }
    }
    if (!options.quest_overlay.on || options.touch_overlay.height <= 0) {
        return;
    }
    const bool touch_ui = hud == HUD_MENU_OPENING || hud == HUD_MENU ||
                          (hud == HUD_HUNTING && panel_state == PANELS_BIG);
    const int forced = quest_map_off                     ? -1
                       : options.quest_overlay_test >= 0 ? options.quest_overlay_test
                                                         : QuestOverlayChoice::FREE;
    quest_choice.Update(EmulatedSeconds(), panels, quest_map_slot, map_drawn, touch_ui, forced);
    ApplyQuestOverlay(false);
}

// ── Screen filters (docs/research/screen-filters.md). The engine's post-processing passes are
// units of uFilter's subclasses that register with sScene; each frame the scene calls their
// render(this, cDraw*) (virtual slot 12), which draws the pass into the scene's target: bloom
// adds a glow made from the bright parts of the picture at a quarter of its resolution, depth
// of field mixes in a blurred quarter-resolution copy by distance, motion blur blends in the
// frames before. A pass turned off returns at once, as if its filter weren't there (the
// community's "bloom off" cheat for MH4G writes a `bx lr` over the start of bloom's render).
// Classes are told apart by their vtable, since some share a render function.
struct ScreenFilter {
    u32 vtable;        // USA v1.3.0
    const char* name;  // in the log
    u32 option;        // the FILTER_* bit that turns it off, 0 none
};
constexpr ScreenFilter SCREEN_FILTERS[] = {
    {0x00e5e7f4, "bloom", FILTER_BLOOM},
    {0x00e624d0, "motion blur", FILTER_MOTION_BLUR},
    {0x00e5bafc, "depth of field", FILTER_DEPTH_OF_FIELD},
    {0x00e5e844, "bokeh", FILTER_DEPTH_OF_FIELD},
    {0x00e62520, "radial blur", FILTER_RADIAL_BLUR},
    {0x00e6313c, "radial blur (MH)", FILTER_RADIAL_BLUR},
    {0x00e61e08, "multi blur", FILTER_MOTION_BLUR},
    {0x00e5d56c, "haze", FILTER_HAZE},
    {0x00e5ff74, "haze (MH)", FILTER_HAZE},
    {0x00e62f28, "colour correction", 0},
    {0x00e5e970, "colour", 0},
    {0x00e5d474, "fade", 0},
    {0x00e61cbc, "cross-fade", 0},
    {0x00e62480, "image plane", 0},
    {0x00e63ac4, "image plane range", 0},
    {0x00e60ff8, "outline", 0},
    {0x00e611a4, "TV noise", 0},
};
static_assert(std::size(SCREEN_FILTERS) <= 32);
u32 filters_drawn = 0, filters_skipped = 0;  // bits: SCREEN_FILTERS index, since the last log line
u32 filters_logged = 0xFFFFFFFF, skipped_logged = 0xFFFFFFFF;
double filters_since = 0;

/// render(this, cDraw*) of a screen filter class.
void FilterRender(RCtx* ctx, RFunc original) {
    const u32 vtable = rd32(ctx, ctx->r[0]);
    for (u32 i = 0; i < std::size(SCREEN_FILTERS); ++i) {
        if (SCREEN_FILTERS[i].vtable != vtable) {
            continue;
        }
        if (SCREEN_FILTERS[i].option & options.filters_off) {
            filters_skipped |= 1u << i;
            rc_hook_return(ctx);
            return;
        }
        filters_drawn |= 1u << i;
        break;
    }
    original(ctx);
}

std::string FilterNames(u32 bits) {
    std::string names;
    for (u32 i = 0; i < std::size(SCREEN_FILTERS); ++i) {
        if (bits >> i & 1) {
            names += (names.empty() ? "" : ", ") + std::string(SCREEN_FILTERS[i].name);
        }
    }
    return names.empty() ? "none" : names;
}

/// Once a frame with trace_filters: the filters drawn and skipped over the last second, logged
/// when they change (a filter can draw every other frame, so a frame alone says too little).
void TraceFilters() {
    const double now = EmulatedSeconds();
    if (now - filters_since < 1.0) {
        return;
    }
    if (filters_drawn != filters_logged || filters_skipped != skipped_logged) {
        LogInfo("screen filters at {:.1f} s: {}{}", now, FilterNames(filters_drawn),
                filters_skipped ? " (skipped: " + FilterNames(filters_skipped) + ")" : "");
        filters_logged = filters_drawn;
        skipped_logged = filters_skipped;
    }
    filters_drawn = filters_skipped = 0;
    filters_since = now;
}

// ── The mouse turns the camera (docs/research/camera.md) ──
//
// sMhCamera::turnByInput (0x00bfcfd8; this, input, invert flags) turns the camera's input
// angles by the camera buttons' bits (D-pad, C-stick, the touch screen's camera pad): the
// pitch at input+8 and the yaw at input+0xc, u16 angles (0x10000 = a turn), the pitch kept
// within +-0x3f00. The quest camera (sMhCamera::updatePlayerCamera 0x00c0095c) then eases its
// rig (sMhCamera+0xe40: pitch +0x80/+0x88, yaw +0x84/+0x8c) toward them. The hook adds the
// mouse's turn after the original: to the input angles, and for the main camera
// (input = sMhCamera+0xe80) to the rig as well, so that the turn shows in the same frame
// instead of being eased in.
constexpr u32 CAMERA_INPUT_MAIN = 0xe80;   // sMhCamera: the player camera's input angles
constexpr u32 CAMERA_RIG = 0xe40;          // sMhCamera: its rig
constexpr u32 INPUT_PITCH = 0x08, INPUT_YAW = 0x0c;
constexpr u32 RIG_PITCH = 0x80, RIG_PITCH_NOW = 0x88, RIG_YAW = 0x84, RIG_YAW_NOW = 0x8c;
constexpr int PITCH_LIMIT = 0x3f00;        // the game's own limit, either way
// "camera left" (pad action 0x0800, input bit 6) adds to the yaw, "camera up" (0x2000, bit 2)
// to the pitch: the mouse to the right turns the other way, the mouse up as "camera up"
constexpr float YAW_SIGN = -1.0f, PITCH_SIGN = 1.0f;
constexpr float UNITS_PER_DEGREE = 65536.0f / 360.0f;

std::atomic<double> camera_turn_yaw{0.0}, camera_turn_pitch{0.0};  // degrees not taken yet
std::atomic<s64> camera_hook_ran_at{0};    // steady-clock ns of its last run
float camera_rest_yaw = 0.0f, camera_rest_pitch = 0.0f;  // angle units' fractions left over

s64 SteadyNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

u32 AddPitch(u32 pitch, int delta) {
    const int now = static_cast<s16>(static_cast<u16>(pitch));
    return static_cast<u16>(std::clamp(now + delta, -PITCH_LIMIT, PITCH_LIMIT));
}

// ── The game's state for the app's touch controls (docs/research/game-state.md) ──
//
// Worked out once a frame, after the main loop's frame, from the game's own objects: the area on
// top of sArea's list says where the game is (scene-observable.md); in a town the lobby's
// controller (cLbMain) has a byte that is set while a menu, a talk or the World Map has the
// buttons; in a quest the hunter is sPlayer's unit for this console's player number, a class per
// weapon, and the quest's START menu is the quest HUD's state. Every pointer is checked for the
// heap and every object for its class before a field of it is read, so another version of the
// game, or a moment between areas, reads as "not known" (-1).
constexpr u32 AREA_COUNT = 0x3818, AREA_TABLE = 0x3820;  // sArea: the areas alive, topmost last
constexpr u32 VT_AINIT = 0x00e64d94, VT_AGAMESYS = 0x00e6c100, VT_ADEMO = 0x00e64d5c,
              VT_ANEWGAME = 0x00e6c138, VT_ACONTINUE = 0x00e6c778, VT_AVILLAGE = 0x00e6c170,
              VT_AQUEST = 0x00e655c8, VT_AARENA = 0x00e650bc;  // the areas' classes (their vtables)
constexpr u32 LOBBY = 0x00edbe90;          // cLbMain*: the towns' controller (null in quests)
constexpr u32 VT_CLBMAIN = 0x00e6b544;
constexpr u32 LOBBY_BUSY = 0x225;          // u8: 1 while a menu, a talk or the map has the buttons
constexpr u32 LOBBY_PANEL = 0x172d5;       // u8: the touch screen's page (GameState::town_panel)
constexpr u32 SPLAYER = 0x00fb83e8;        // sPlayer*
constexpr u32 SPLAYER_UNITS = 0xa98;       // uPlayer*[4], by player number
constexpr u32 GAME_CONTROL_PLAYER = 0x2f;  // s8: this console's player number
constexpr u32 GAME_CONTROL_NO_UNITS = 0x5a;  // u8: 1 where sPlayer has no units (the towns)
// The game's own "is a menu open" (0x005e7144, asked by the touch panels and the quest HUD): the START
// menu's byte, four window bits beside it, and in a town or a quest a byte of the quest HUD's
constexpr u32 GAME_MENU = 0x0108b9bc;      // (its getter 0x0012580c)
constexpr u32 GAME_MENU_OPEN = 0x04;       // u8: the START menu is up (in towns and quests alike)
constexpr u32 GAME_MENU_WINDOWS = 0x1e;    // u16: bits 0-3
constexpr u32 HUD_MENU_PANEL = 0x01083858; // u8 (0x00b89288 reads it)
constexpr u32 GAME_CONTROL_MODE = 0x2b;    // u8: 1 a quest, 2 a town, 3 the Arena's equipment
// Whether the weapon is out: a byte of the hunter's state object ([hunter+0xe30], the one camera.md
// calls its input object), which the hunter's virtual 50 (0x00b2a598) sets and 0x00b084d0 tests for 1
// (123 callers; the touch screen's kick is refused with it)
constexpr u32 HUNTER_STATE = 0xe30;
constexpr u32 HUNTER_ACTION = 0x244;       // u16: the hunter's action (its virtual 33)
constexpr u32 STATE_WEAPON_OUT = 0x6f9;    // u8: 1 out, 0 sheathed
// The hunter's class, uWeapon00..14 in the game's weapon numbers (no 05 and 06): the vtables, from
// each class's getDTI (virtual 5) in config/mt_classes.tsv. 4 is both bowguns, 10 the bow.
constexpr u32 WEAPON_VTABLES[15] = {0x00e6edbc, 0x00e6ef2c, 0x00e6f09c, 0x00e6f20c, 0x00e6f37c, 0, 0,
                                    0x00e6f4ec, 0x00e6f65c, 0x00e6f7cc, 0x00e6f93c, 0x00e6faac,
                                    0x00e6fc1c, 0x00e6fd8c, 0x00e6fefc};
constexpr int WEAPON_BOWGUN = 4, WEAPON_BOW = 10;
// The HUD's parts (HUD_OBJECTS above): each is drawn while it is on the top screen, where the game draws it (its boxes)
// moved by the hooks (hud_edges, the player's offset), in the HUD's 400 pixels centred in a wider picture.
constexpr u64 HUD_PART_FRAMES = 3;         // drawn within this many frames: shown

std::mutex game_state_mutex;
GameState game_state;                      // CurrentGameState(), under the mutex
GameState game_state_logged;               // trace_state: the last state logged
u64 hud_drawn_at[HUD_PART_COUNT] = {};     // frames when each part was last drawn (0 never)
u64 hud_object_drawn_at[HUD_OBJECT_COUNT] = {};  // the same for each object
u64& item_bar_drawn_at = hud_drawn_at[HUD_ITEMS];
constexpr u64 ITEM_BAR_FRAMES = HUD_PART_FRAMES;

void NoteHudObject(int object) {
    hud_object_drawn_at[object] = frames;
    hud_drawn_at[HUD_OBJECTS[object].part] = frames;
}

/// A pointer to a game object in the application heap (or 0).
bool HeapPointer(u32 p) {
    return p >= 0x08000000 && p < 0x10000000 && (p & 3) == 0;
}

/// The local hunter's unit in a quest, or 0: sPlayer's unit for this console's player number, as
/// sPlayer::getPlayer (0x00af9a60) gives it, if its class is one of the weapons'.
u32 LocalHunter(RCtx* ctx, int* weapon) {
    const u32 control = rd32(ctx, GAME_CONTROL);
    const u32 players = rd32(ctx, SPLAYER);
    if (!HeapPointer(control) || !HeapPointer(players) || rd8(ctx, control + GAME_CONTROL_NO_UNITS) == 1) {
        return 0;
    }
    const s8 number = static_cast<s8>(rd8(ctx, control + GAME_CONTROL_PLAYER));
    if (number < 0 || number > 3) {
        return 0;
    }
    const u32 hunter = rd32(ctx, players + SPLAYER_UNITS + 4 * static_cast<u32>(number));
    if (!HeapPointer(hunter)) {
        return 0;
    }
    const u32 vtable = rd32(ctx, hunter);
    for (int i = 0; i < 15; ++i) {
        if (WEAPON_VTABLES[i] != 0 && WEAPON_VTABLES[i] == vtable) {
            *weapon = i;
            return hunter;
        }
    }
    return 0;
}

/// 1 the hunter's weapon is out, 0 sheathed, -1 not known.
int WeaponOut(RCtx* ctx, u32 hunter) {
    const u32 state = rd32(ctx, hunter + HUNTER_STATE);
    if (!HeapPointer(state)) {
        return -1;
    }
    const u8 out = rd8(ctx, state + STATE_WEAPON_OUT);
    return out <= 1 ? out : -1;
}

/// The game's own answer to "is a menu open" (0x005e7144).
bool GameMenuOpen(RCtx* ctx) {
    if (rd8(ctx, GAME_MENU + GAME_MENU_OPEN) != 0 || (rd16(ctx, GAME_MENU + GAME_MENU_WINDOWS) & 0xf) != 0) {
        return true;
    }
    const u32 control = rd32(ctx, GAME_CONTROL);
    const u8 mode = HeapPointer(control) ? rd8(ctx, control + GAME_CONTROL_MODE) : 0;
    return (mode == 1 || mode == 2) && rd8(ctx, HUD_MENU_PANEL) != 0;
}

/// Calls a game function with up to four arguments, the caller's registers and flags kept; returns its r0.
u32 CallGame(RCtx* ctx, u32 function, std::initializer_list<u32> args) {
    u32 regs[16];
    std::copy(std::begin(ctx->r), std::end(ctx->r), regs);
    const u32 flags[4] = {ctx->fn, ctx->fz, ctx->fc, ctx->fv};
    const u32 expect_next = ctx->expect_next;
    ctx->expect_next = 0;
    int i = 0;
    for (const u32 a : args) {
        ctx->r[i++] = a;
    }
    ctx->r[14] = function;  // (any address: the call ends when the function returns)
    rc_call(ctx, function);
    const u32 result = ctx->r[0];
    std::copy(std::begin(regs), std::end(regs), ctx->r);
    ctx->fn = flags[0];
    ctx->fz = flags[1];
    ctx->fc = flags[2];
    ctx->fv = flags[3];
    ctx->expect_next = expect_next;
    return result;
}

// ── The item bar by touch (the app's swipe on the bar) ──
//
// The hunter's item-bar handler (0x00b15394, from the player's update) turns the bar while L is held and Y or A is
// pressed: it plays sound 7, takes the next pouch slot that holds an item from 0x00b31fa4(hunter, slot, 0 the next
// slot up, A's way, the bar's right / 1 down, Y's, its left) into the selected slot of the hunter's item info
// ([hunter+0xa29c]+0x584) and sets bit 1 (A) or 2 (Y) of +0x589, the bar's turn. The pouch's slots 0-23 hold items,
// 44 is the bar's empty place, "(No Item)", between the last and the first. The touch screen's swipe on the bar
// (0x00ba6b38) takes the same step without L: the strip follows the finger (a swipe to the right brings the item on
// the left, Y's way). The app's swipe takes it here, after the game's frame, one step a frame, under the handler's own
// conditions (the hunter is this console's, not in the states the handler closes the bar in, the quest not ending):
// L is not pressed, so the camera is not turned behind the hunter at each step, as the buttons' way did.
constexpr u32 HUNTER_ITEMS = 0xa29c;        // the hunter's item info*
constexpr u32 ITEMS_SLOT = 0x584;           // u16: the selected pouch slot
constexpr u32 ITEMS_TURN = 0x589;           // u8: bit 1 turned left, bit 2 right (the bar's animation)
constexpr u32 FN_ITEM_NEXT = 0x00b31fa4;    // (hunter, slot, 0 left / 1 right): the next slot with an item
constexpr u32 FN_IS_LOCAL = 0x00d04f6c;     // (hunter): nonzero for this console's hunter
constexpr u32 FN_PLAY_SOUND = 0x00312928;   // (the sound manager, sound)
constexpr u32 SOUND_MANAGER = 0x00fccd6c;   // its pointer
constexpr u32 SOUND_ITEM_TURN = 7;
constexpr u32 FN_HOLDER_ASK = 0x006b3fe0;   // (holder, 5): nonzero, no item now
constexpr u32 FN_HOLDER_ENDING = 0x006b3c84;  // (holder): nonzero, no item now
constexpr u32 STATE_MODE = 0x371;           // u8 of the hunter's state object: 8 and 9, and 6 with
constexpr u32 STATE_MODE_ACTION = 0x372;    //   u16 0x1a here, close the bar; 0 with 0x78 or 0x79 too
constexpr s64 ITEM_STEP_KEEP = 600'000'000; // ns a step the hunter can't take now waits (a swing, a roll)
std::atomic<int> item_steps_asked{0};       // ScrollItemBar: steps not taken yet, > 0 to the right
std::atomic<s64> item_steps_at{0};          // steady ns of the last
int item_test_steps = 0;                    // the item_step option (tests)

/// Whether the hunter's item-bar handler would let the hunter pick an item now.
bool ItemSelectAllowed(RCtx* ctx, u32 hunter) {
    const u32 state = rd32(ctx, hunter + HUNTER_STATE);
    const u32 holder = rd32(ctx, QUEST_HOLDER);
    if (!HeapPointer(state) || !HeapPointer(holder) || CallGame(ctx, FN_IS_LOCAL, {hunter}) == 0) {
        return false;
    }
    const u8 mode = rd8(ctx, state + STATE_MODE);
    const u16 action = rd16(ctx, state + STATE_MODE_ACTION);
    if (mode == 8 || mode == 9 || (mode == 6 && action == 0x1a) || (mode == 0 && (action == 0x78 || action == 0x79))) {
        return false;
    }
    return CallGame(ctx, FN_HOLDER_ASK, {holder, 5}) == 0 && CallGame(ctx, FN_HOLDER_ENDING, {holder}) == 0;
}

// The bar open, as while L is held (its neighbours shown): the handler opens it when L is held or when bit 20 of the
// item info's +0xec is set, and closes it (sound 2) when neither is. HoldItemBarOpen sets the bit from sMhPad's update,
// before the player's, for as long as the app's finger holds the bar.
constexpr u32 ITEMS_FLAGS = 0xec;
constexpr u32 ITEMS_FLAG_OPEN = 0x100000;
std::atomic<bool> item_open_asked{false};   // HoldItemBarOpen
u32 item_open_set = 0;                      // the item info whose bit the hooks set, 0 none

void ItemBarHold(RCtx* ctx) {
    const bool asked = item_open_asked.load(std::memory_order_relaxed);
    if (!asked && item_open_set == 0) {
        return;
    }
    int weapon = -1;
    const u32 hunter = asked ? LocalHunter(ctx, &weapon) : 0;
    const u32 items = hunter ? rd32(ctx, hunter + HUNTER_ITEMS) : 0;
    if (item_open_set != 0 && item_open_set != items) {
        // (let go, or another hunter's info now: the bit the hooks set goes, if that info is still a hunter's)
        if (HeapPointer(item_open_set)) {
            wr32(ctx, item_open_set + ITEMS_FLAGS, rd32(ctx, item_open_set + ITEMS_FLAGS) & ~ITEMS_FLAG_OPEN);
        }
        item_open_set = 0;
    }
    if (asked && HeapPointer(items)) {
        wr32(ctx, items + ITEMS_FLAGS, rd32(ctx, items + ITEMS_FLAGS) | ITEMS_FLAG_OPEN);
        item_open_set = items;
    }
}

/// Once a frame (Frame, after the original): one step of the item bar the app asked for, if the hunter can take it.
void ItemBarStep(RCtx* ctx) {
    if (item_test_steps != 0) {
        item_steps_asked += item_test_steps;
        item_steps_at = SteadyNs();
        item_test_steps = 0;
    }
    const int asked = item_steps_asked.load(std::memory_order_relaxed);
    if (asked == 0) {
        return;
    }
    const bool old = SteadyNs() - item_steps_at.load(std::memory_order_relaxed) > ITEM_STEP_KEEP;
    int weapon = -1;
    const u32 hunter = LocalHunter(ctx, &weapon);
    const u32 items = hunter ? rd32(ctx, hunter + HUNTER_ITEMS) : 0;
    const bool bar = item_bar_drawn_at != 0 && frames - item_bar_drawn_at <= ITEM_BAR_FRAMES;
    if (!HeapPointer(items) || !bar || GameMenuOpen(ctx) || !ItemSelectAllowed(ctx, hunter)) {
        if (old) {
            item_steps_asked = 0;  // (not now: dropped once it has waited a moment)
        }
        return;
    }
    const int dir = asked > 0 ? 1 : -1;
    item_steps_asked.fetch_sub(dir, std::memory_order_relaxed);
    const u32 slot = rd16(ctx, items + ITEMS_SLOT);
    // (to the right, A's way: the next slot up, 0x00b31fa4's 0, the turn's bit 1; to the left, Y's: 1, bit 2)
    const u32 next = CallGame(ctx, FN_ITEM_NEXT, {hunter, slot, dir > 0 ? 0u : 1u}) & 0xffff;
    wr16(ctx, items + ITEMS_SLOT, static_cast<u16>(next));
    wr8(ctx, items + ITEMS_TURN, static_cast<u8>(rd8(ctx, items + ITEMS_TURN) | (dir > 0 ? 2 : 4)));
    if (const u32 sound = rd32(ctx, SOUND_MANAGER); sound != 0) {
        CallGame(ctx, FN_PLAY_SOUND, {sound, SOUND_ITEM_TURN});
    }
    if (options.trace_state) {
        LogInfo("item bar: frame {} slot {} -> {} ({})", frames, slot, next, dir > 0 ? "right" : "left");
    }
}

std::string FormatGameState(const GameState& s) {
    constexpr const char* places[] = {"other", "town", "quest"};
    std::string text = fmt::format("place {} menu {} weapon {} drawn {} action {:04X} ranged {} item bar ",
                                   s.place < 0 ? "-" : places[s.place], s.menu, s.weapon, s.weapon_drawn,
                                   static_cast<u16>(s.action), s.ranged);
    if (s.item_bar_shown == 1) {
        text += fmt::format("at {:.3f} {:.3f} {:.3f} {:.3f}", s.item_bar[0], s.item_bar[1], s.item_bar[2],
                            s.item_bar[3]);
    } else {
        text += s.item_bar_shown == 0 ? "not shown" : "-";
    }
    return text;
}

/// Once a frame (Frame, after the original): CurrentGameState's copy.
void UpdateGameState(RCtx* ctx) {
    GameState s;
    s.frame = rd32(ctx, FRAMES_SUBMITTED);
    const u32 areas = rd32(ctx, SAREA_INSTANCE);
    const u32 count = HeapPointer(areas) ? rd32(ctx, areas + AREA_COUNT) : 0;
    const u32 top = count >= 1 && count <= 8 ? rd32(ctx, areas + AREA_TABLE + 4 * (count - 1)) : 0;
    const u32 scene = HeapPointer(top) ? rd32(ctx, top) : 0;
    bool hold = false;  // between areas: the menu as it was
    switch (scene) {
    case VT_AVILLAGE: {
        s.place = GameState::PLACE_TOWN;
        const u32 lobby = rd32(ctx, LOBBY);
        if (HeapPointer(lobby) && rd32(ctx, lobby) == VT_CLBMAIN) {
            s.town_panel = rd8(ctx, lobby + LOBBY_PANEL);
            // The lobby's byte stays 1 after a gesture is done and the Gestures menu closed, until the hunter moves:
            // not a menu. Only on that way out of the touch screen's three menus (Guild Cards 5, Gestures 8, Friends
            // 11, back to the map 2 or Hunters for Hire 18) is the byte taken as stale, until it drops; a talk, the
            // item box or a shop set it from the map and are menus as before (docs/research/game-state.md).
            // (the way out passes a page 1 for a frame: whether one of the three was up since the page was last at rest)
            static bool from_menu = false;
            static bool busy_stale = false;
            const bool busy = rd8(ctx, lobby + LOBBY_BUSY) != 0;
            const int panel = s.town_panel;
            const bool rest = panel == 2 || panel == 18;
            if (panel == 5 || panel == 8 || panel == 11) from_menu = true;
            if (!busy || !rest) busy_stale = false;
            if (rest) {
                if (from_menu && busy) busy_stale = true;
                from_menu = false;
            }
            s.menu = (busy && !busy_stale) || GameMenuOpen(ctx) ? 1 : 0;
        }
        break;
    }
    case VT_AQUEST: {
        s.place = GameState::PLACE_QUEST;
        const u8 hud = rd8(ctx, QUEST_HUD + HUD_STATE);
        // (not the helpers' talks that come up by themselves in a hunt, as the Caravaneer's: the hunt's
        // layout has A and B for them, and nothing found tells them apart from the hunt yet)
        s.menu = hud == HUD_MENU_OPENING || hud == HUD_MENU || hud == HUD_BOX_OPENING || hud == HUD_BOX ||
                         GameMenuOpen(ctx) ? 1 : 0;
        int weapon = -1;
        if (const u32 hunter = LocalHunter(ctx, &weapon)) {
            s.weapon = weapon;
            s.ranged = weapon == WEAPON_BOW ? 1 : weapon == WEAPON_BOWGUN ? 2 : 0;
            s.weapon_drawn = WeaponOut(ctx, hunter);
            s.action = rd16(ctx, hunter + HUNTER_ACTION);
            if (options.trace_panels) {  // (what tells a window over the hunt: the HUD's state, the hunter's +0x54c)
                static u32 last = ~0u;
                const u32 key = hud | rd8(ctx, hunter + 0x54c) << 8 | rd8(ctx, 0x0108b9c0) << 16 |
                                (rd16(ctx, 0x0108b9da) & 0xf) << 20 | rd8(ctx, 0x01083858) << 24;
                if (key != last) {
                    LogInfo("state: frame {} hud {} hunter+54c {} menu bytes {} {} {} action {:04X}", s.frame, hud,
                            key >> 8 & 0xff, key >> 16 & 0xf, key >> 20 & 0xf, key >> 24, s.action);
                    last = key;
                }
            }
        }
        break;
    }
    case VT_AARENA:     // the Arena's equipment screen
    case VT_ADEMO:      // the intro, the title
    case VT_ANEWGAME:   // the character's creation
    case VT_ACONTINUE:  // the choice of a save
        s.place = GameState::PLACE_OTHER;
        s.menu = 1;
        break;
    case VT_AGAMESYS:  // (loading)
    case VT_AINIT:
        hold = true;  // loading: aGameSys alone
        break;
    default:
        break;
    }
    // (out of a quest, to a town or the title: no lock-on left on for the next hunt)
    if ((s.place == GameState::PLACE_TOWN || s.place == GameState::PLACE_OTHER) && LockOn()) {
        SetLockOn(false);
        LogInfo("lock-on: off (the quest is over)");
    }
    // every part where it is drawn, or (not drawn now) where it would be: the HUD's 400 pixels centred in a wider
    // picture, the part moved by its side's edge (hud_edges) and the player's offset
    const float width = WideRows() ? static_cast<float>(WideRows()) : static_cast<float>(TOP_ROWS);
    s.top_width = static_cast<int>(width);
    for (int k = 0; k < HUD_PART_COUNT; ++k) {
        // its box: its objects' drawn in the last frames, or its first object's
        float box[4] = {1e9f, 1e9f, -1e9f, -1e9f};
        int first = -1;
        for (int i = 0; i < HUD_OBJECT_COUNT; ++i) {
            const HudObject& o = HUD_OBJECTS[i];
            if (o.part != k || o.x1 <= o.x0) {
                continue;
            }
            if (first < 0) {
                first = i;
            }
            if (hud_object_drawn_at[i] != 0 && frames - hud_object_drawn_at[i] <= HUD_PART_FRAMES) {
                box[0] = std::min(box[0], o.x0), box[1] = std::min(box[1], o.y0);
                box[2] = std::max(box[2], o.x1), box[3] = std::max(box[3], o.y1);
            }
        }
        if (box[2] < box[0] && first >= 0) {
            const HudObject& o = HUD_OBJECTS[first];
            box[0] = o.x0, box[1] = o.y0, box[2] = o.x1, box[3] = o.y1;
        }
        GameState::HudPart& out = s.hud[k];
        out.shown = hud_drawn_at[k] != 0 && frames - hud_drawn_at[k] <= HUD_PART_FRAMES ? 1 : 0;
        out.seen = hud_drawn_at[k] != 0 ? 1 : 0;
        out.offset[0] = options.hud_dx[k];
        out.offset[1] = options.hud_dy[k];
        // (the hooks' own move of it: the boxes were measured where the game draws the parts, its own translation
        // included, as the area map's, which the game places with one: -272, -16 on the expedition)
        const u32 rows = options.hud_edges ? WideRows() : 0;
        const float tx = -(HUD_SIDE[k] * (rows != 0 ? (static_cast<float>(rows) - TOP_ROWS) / 2.0f : 0.0f) +
                           options.hud_dx[k]);
        const float ty = -options.hud_dy[k];
        const float dx = (width - TOP_ROWS) / 2.0f - tx, dy = -ty;
        out.rect[0] = (box[0] + dx) / width;
        out.rect[1] = (box[1] + dy) / LCD_COLUMNS;
        out.rect[2] = (box[2] + 1 + dx) / width;
        out.rect[3] = (box[3] + 1 + dy) / LCD_COLUMNS;
    }
    // the item bar's touch zone: the bar itself (the selection, its name and count), where the part is, but not the
    // coating or the ammo above it, whose place a bow's or a bowgun's touch controls use (the part's box has them)
    const GameState::HudPart& bar = s.hud[HUD_ITEMS];
    if (bar.shown) {
        const u32 rows = options.hud_edges ? WideRows() : 0;
        const float dx = (width - TOP_ROWS) / 2.0f + HUD_SIDE[HUD_ITEMS] *
                         (rows != 0 ? (static_cast<float>(rows) - TOP_ROWS) / 2.0f : 0.0f) + options.hud_dx[HUD_ITEMS];
        const float dy = options.hud_dy[HUD_ITEMS];
        constexpr float X0 = 273, Y0 = 193, X1 = 378, Y1 = 239;  // (HUD_OBJECTS' selection and name)
        s.item_bar_shown = 1;
        s.item_bar[0] = std::clamp((X0 + dx) / width, 0.0f, 1.0f);
        s.item_bar[1] = std::clamp((Y0 + dy) / LCD_COLUMNS, 0.0f, 1.0f);
        s.item_bar[2] = std::clamp((X1 + dx) / width, 0.0f, 1.0f);
        s.item_bar[3] = std::clamp((Y1 + dy) / LCD_COLUMNS, 0.0f, 1.0f);
    } else {
        s.item_bar_shown = scene != 0 ? 0 : -1;
    }
    std::lock_guard lock(game_state_mutex);
    if (hold) {
        s.menu = game_state.menu;
        s.place = game_state.place;
    }
    game_state = s;
    if (options.trace_state && (s.place != game_state_logged.place || s.menu != game_state_logged.menu ||
                                s.weapon != game_state_logged.weapon ||
                                s.weapon_drawn != game_state_logged.weapon_drawn ||
                                (options.trace_state_actions && s.action != game_state_logged.action) ||
                                s.item_bar_shown != game_state_logged.item_bar_shown ||
                                (s.item_bar_shown == 1 && (std::fabs(s.item_bar[0] - game_state_logged.item_bar[0]) > 0.01f ||
                                                           std::fabs(s.item_bar[1] - game_state_logged.item_bar[1]) > 0.01f)))) {
        LogInfo("state: frame {} {} (area {:08X})", s.frame, FormatGameState(s), scene);
        game_state_logged = s;
    }
}

// ── The lock-on (the app's touch controls: a long press on L; a controller's right stick click) ──
//
// While it is on, the quest camera turns toward a large monster every frame, eased, as in the newer
// games (the MHGU port's hle/mousecam.c), from the same hook as the mouse. The monster is the one the
// Target Cam has chosen (sMhCamera+0xed5: the n-th large monster, as 0x00bfea8c counts them), else the
// nearest large monster; only one in the hunter's area of the map and not dead, as the game's own L to
// the target (0x00bff694) asks. The angles are the game's (0x00c040fc, the camera's turn to its target):
// the yaw of the vector from the monster to the hunter, the pitch toward the monster from 170 units above
// the hunter's feet. A turn by hand (the drag, the C-stick, the D-pad) stops the following for a moment.
constexpr u32 SENEMY = 0x00f32004;         // sEnemy*: 32 monster slots at +0x18 (0x0068fb9c)
constexpr u32 ENEMY_SLOTS = 0x18, ENEMY_SLOT_COUNT = 32;
constexpr u32 EM_INFO = 0xe28;             // monster: its status block
constexpr u32 INFO_KIND = 0x1c0;           // u8: its row of the monster table
constexpr u32 INFO_AREA = 0x14c;           // u8: the area of the map it is in
constexpr u32 INFO_STATE = 0x1ca;          // u8: 0xb, 0xe dead; 0xc with +0x1cb 0xff captured (0x0094d770)
constexpr u32 MONSTER_TABLE = 0x00f3a020;  // [[[[table] + 4*kind]]]: bit 0 a large monster (0x0068882c)
constexpr u32 UNIT_POSITION = 0xd00;       // a unit's place (x, y, z floats; fps-observables.md)
constexpr u32 HUNTER_POINT = 0x10;         // [hunter+0xe30]+0x10: the hunter's place, as the camera reads it
constexpr u32 GAME_CONTROL_AREA = 0xc90;   // u32: the hunter's area of the map
constexpr u32 CAMERA_TARGET = 0xed5;       // u8: the Target Cam's monster, 1.., 0 none
constexpr float LOCK_EYE = 170.0f;         // the game's height of the eye above the hunter's feet
constexpr float LOCK_AIM = 90.0f;          // the camera aims this far above the monster's feet
constexpr float LOCK_TAU = 0.12f;          // seconds to turn ~63% of the way (eased)
constexpr float LOCK_RANGE = 6000.0f;      // a monster farther than this is not followed
constexpr float LOCK_NEAR0 = 120.0f, LOCK_NEAR1 = 380.0f;  // under the monster the camera holds still
constexpr s64 LOCK_AFTER_TURN = 600'000'000;  // ns after a turn by hand before following again
std::atomic<bool> lock_on{false};          // SetLockOn
std::atomic<int> target_cam_now{-1};       // TargetCamMonster: the camera's CAMERA_TARGET each frame of a hunt
std::atomic<s64> lock_followed_at{0};      // steady ns when the camera last turned to a monster
s64 lock_hand_at = 0;                      // steady ns of the last turn by hand
u32 lock_monster = 0;                      // the monster followed last
double lock_time = 0;                      // emulated seconds of the last step

/// A pointer the game keeps to its own data: the code's data and bss, the application heap, or the linear heap
/// (the monster table's rows live there).
bool DataPointer(u32 p) {
    return (p & 3) == 0 && ((p >= 0x00100000 && p < 0x01200000) || (p >= 0x08000000 && p < 0x10000000) ||
                            (p >= 0x14000000 && p < 0x1c000000) || (p >= 0x30000000 && p < 0x38000000));
}

/// Whether `monster` (any unit of sEnemy's) is a large monster, and its status block. The game's own test
/// (0x0091dee8 and 0x0068882c): bit 0 of [[[[0x00f3a020] + 4*row]]].
bool LargeMonster(RCtx* ctx, u32 monster, u32* info_out) {
    const u32 info = HeapPointer(monster) ? rd32(ctx, monster + EM_INFO) : 0;
    const u32 table = HeapPointer(info) ? rd32(ctx, MONSTER_TABLE) : 0;
    const u32 entry = DataPointer(table) ? rd32(ctx, table + 4 * rd8(ctx, info + INFO_KIND)) : 0;
    const u32 row = DataPointer(entry) ? rd32(ctx, entry) : 0;
    if (!DataPointer(row)) {
        return false;
    }
    *info_out = info;
    return (rd32(ctx, row) & 1) != 0;
}

/// The monster to follow (see above), or 0. `hx`, `hz`: the hunter's place.
u32 LockTarget(RCtx* ctx, u32 camera, float hx, float hz) {
    const u32 enemies = rd32(ctx, SENEMY);
    const u32 control = rd32(ctx, GAME_CONTROL);
    if (!HeapPointer(enemies) || !HeapPointer(control)) {
        return 0;
    }
    const u32 area = rd32(ctx, control + GAME_CONTROL_AREA);
    const u32 chosen = rd8(ctx, camera + CAMERA_TARGET);
    u32 large = 0, best = 0;
    float best_d = LOCK_RANGE * LOCK_RANGE, current_d = -1;
    for (u32 i = 0; i < ENEMY_SLOT_COUNT; ++i) {
        const u32 monster = rd32(ctx, enemies + ENEMY_SLOTS + 4 * i);
        u32 info = 0;
        if (!LargeMonster(ctx, monster, &info)) {
            continue;
        }
        ++large;  // (the Target Cam counts the large monsters, as 0x00bfea8c)
        const u8 state = rd8(ctx, info + INFO_STATE);
        const bool dead = state == 0xb || state == 0xe || (state == 0xc && rd8(ctx, info + INFO_STATE + 1) == 0xff);
        if (dead || rd8(ctx, info + INFO_AREA) != area) {
            continue;
        }
        if (chosen != 0) {
            if (large == chosen) {
                return monster;
            }
            continue;
        }
        const float dx = ReadF32(ctx, monster + UNIT_POSITION) - hx;
        const float dz = ReadF32(ctx, monster + UNIT_POSITION + 8) - hz;
        const float d = dx * dx + dz * dz;
        if (!std::isfinite(d)) {
            continue;
        }
        if (monster == lock_monster && d < LOCK_RANGE * LOCK_RANGE) {
            current_d = d;
        }
        if (d < best_d) {
            best_d = d;
            best = monster;
        }
    }
    if (chosen == 0 && current_d >= 0 && best != lock_monster && best_d >= current_d * 0.49f) {
        return lock_monster;  // (another one only when clearly nearer: no back and forth)
    }
    return best;
}

/// u16 angle units of the vector (x, z), as 0x00568c64.
int AngleUnits(float x, float z) {
    return static_cast<int>(std::lround(std::atan2(x, z) * 65536.0f / 6.28318531f)) & 0xffff;
}

/// The step toward the monster, for the main camera (after the turns of this frame).
void LockOnStep(RCtx* ctx, u32 camera, u32 input, bool turned_by_hand) {
    const s64 now = SteadyNs();
    if (turned_by_hand) {
        lock_hand_at = now;
    }
    const double time = EmulatedSeconds();
    const float dt = std::clamp(static_cast<float>(time - lock_time), 0.0f, 0.1f);
    lock_time = time;
    target_cam_now.store(rd8(ctx, camera + CAMERA_TARGET), std::memory_order_relaxed);
    if (!lock_on.load(std::memory_order_relaxed) || now - lock_hand_at < LOCK_AFTER_TURN) {
        return;
    }
    int weapon = -1;
    const u32 hunter = LocalHunter(ctx, &weapon);
    const u32 state = hunter ? rd32(ctx, hunter + HUNTER_STATE) : 0;
    if (!HeapPointer(state)) {
        return;
    }
    const float hx = ReadF32(ctx, state + HUNTER_POINT), hy = ReadF32(ctx, state + HUNTER_POINT + 4),
                hz = ReadF32(ctx, state + HUNTER_POINT + 8);
    const u32 monster = LockTarget(ctx, camera, hx, hz);
    if (monster != lock_monster) {
        LogInfo("lock-on: {}", monster ? fmt::format("monster {:08X}", monster) : std::string("no monster to follow"));
        lock_monster = monster;
    }
    if (monster == 0) {  // (the target gone, the hunter carted to camp: the lock lets go, never left on with nothing)
        lock_on.store(false, std::memory_order_relaxed);
        LogInfo("lock-on: off (no monster to follow)");
        return;
    }
    const float mx = ReadF32(ctx, monster + UNIT_POSITION), my = ReadF32(ctx, monster + UNIT_POSITION + 4),
                mz = ReadF32(ctx, monster + UNIT_POSITION + 8);
    const float dx = hx - mx, dz = hz - mz, distance = std::sqrt(dx * dx + dz * dz);
    if (!std::isfinite(distance) || !std::isfinite(my) || dt <= 0) {
        return;
    }
    // eased: a part of the way each frame, and less of it right under the monster
    const float near = std::clamp((distance - LOCK_NEAR0) / (LOCK_NEAR1 - LOCK_NEAR0), 0.0f, 1.0f);
    const float k = (1.0f - std::exp(-dt / LOCK_TAU)) * near;
    const int yaw_now = static_cast<int>(rd32(ctx, input + INPUT_YAW) & 0xffff);
    const int yaw_to = AngleUnits(dx, dz);
    const int yaw = static_cast<int>(std::lround(static_cast<s16>(static_cast<u16>(yaw_to - yaw_now)) * k));
    const int pitch_now = static_cast<s16>(static_cast<u16>(rd32(ctx, input + INPUT_PITCH)));
    const int pitch_to = std::clamp(static_cast<int>(static_cast<s16>(static_cast<u16>(
                                        AngleUnits(-(hy + LOCK_EYE - (my + LOCK_AIM)), distance)))),
                                    -PITCH_LIMIT, PITCH_LIMIT);
    const int pitch = static_cast<int>(std::lround((pitch_to - pitch_now) * k));
    lock_followed_at.store(now, std::memory_order_relaxed);
    if (yaw == 0 && pitch == 0) {
        return;
    }
    wr32(ctx, input + INPUT_YAW, static_cast<u16>(yaw_now + yaw));
    wr32(ctx, input + INPUT_PITCH, AddPitch(rd32(ctx, input + INPUT_PITCH), pitch));
    const u32 rig = camera + CAMERA_RIG;
    for (const u32 field : {RIG_YAW, RIG_YAW_NOW}) {
        wr32(ctx, rig + field, static_cast<u16>(rd32(ctx, rig + field) + yaw));
    }
    for (const u32 field : {RIG_PITCH, RIG_PITCH_NOW}) {
        wr32(ctx, rig + field, AddPitch(rd32(ctx, rig + field), pitch));
    }
}

void CameraTurnByInput(RCtx* ctx, RFunc original) {
    const u32 camera = ctx->r[0];
    const u32 input = ctx->r[1];
    const bool main_camera = input == camera + CAMERA_INPUT_MAIN;
    const auto angles = [&] { return (rd32(ctx, input + INPUT_YAW) & 0xffff) | rd32(ctx, input + INPUT_PITCH) << 16; };
    const u32 before = main_camera ? angles() : 0;
    original(ctx);
    camera_hook_ran_at.store(SteadyNs(), std::memory_order_relaxed);
    const u32 r0 = ctx->r[0];  // (the original's result stays)
    const bool game_turned = main_camera && angles() != before;  // (the C-stick, the D-pad)
    const float yaw_units = static_cast<float>(camera_turn_yaw.exchange(0.0)) * UNITS_PER_DEGREE * YAW_SIGN +
                            camera_rest_yaw;
    const float pitch_units = static_cast<float>(camera_turn_pitch.exchange(0.0)) * UNITS_PER_DEGREE *
                                  PITCH_SIGN +
                              camera_rest_pitch;
    const int yaw = static_cast<int>(yaw_units), pitch = static_cast<int>(pitch_units);
    camera_rest_yaw = yaw_units - static_cast<float>(yaw);
    camera_rest_pitch = pitch_units - static_cast<float>(pitch);
    if (yaw != 0 || pitch != 0) {
        wr32(ctx, input + INPUT_YAW, static_cast<u16>(rd32(ctx, input + INPUT_YAW) + yaw));
        wr32(ctx, input + INPUT_PITCH, AddPitch(rd32(ctx, input + INPUT_PITCH), pitch));
        if (main_camera) {
            const u32 rig = camera + CAMERA_RIG;
            for (const u32 field : {RIG_YAW, RIG_YAW_NOW}) {
                wr32(ctx, rig + field, static_cast<u16>(rd32(ctx, rig + field) + yaw));
            }
            for (const u32 field : {RIG_PITCH, RIG_PITCH_NOW}) {
                wr32(ctx, rig + field, AddPitch(rd32(ctx, rig + field), pitch));
            }
        }
    }
    if (main_camera) {
        LockOnStep(ctx, camera, input, game_turned || yaw != 0 || pitch != 0);
    }
    ctx->r[0] = r0;
}

struct Hook {
    u32 addr;          // USA v1.3.0's (config/hooks.txt)
    const char* name;  // as in config/hooks.txt
    RHookFn fn;
};
constexpr Hook HOOKS[] = {
    {0x006442f8, "sMain::updateFrameTiming", UpdateFrameTiming},
    {0x00af5250, "sMhMain::frame", Frame},
    {0x00c46380, "uMhCamera::getProjMatrix", GetProjMatrix},
    {0x002535f8, "uFreeCamera::getProjMatrix", GetProjMatrix},
    {0x0032e338, "uMotionCamera::getProjMatrix", GetProjMatrix},
    {0x00633188, "nDraw::Scene::Scene", SceneCtor},
    {0x0062e184, "nDraw::DisplayBuffer::init", DisplayBufferInit},
    {0x00aefd44, "sCamera::Viewport::updateRegion", UpdateRegion},
    {0x002951fc, "PanelManager::processTouch", PanelProcessTouch},
    {0x0028f2f0, "PanelManager::touchAllowed", PanelTouchAllowed},
    {0x00288f4c, "PanelManager::isTapped", PanelIsTapped},
    {0x00694c98, "sMhPad::update", MhPadUpdate},
    {0x00bfcfd8, "sMhCamera::turnByInput", CameraTurnByInput},
    {0x00b7c010, "QuestFlow::startQuest", StartQuest},
    {0x0065c18c, "aQuest::playing", QuestPlaying},
    {0x00ae65a4, "nLayout::objectConstants", LayoutObjectConstants},
    {0x00b92e24, "QuestHud::updatePanels", QuestPanels},
    {0x002bc238, "uBloomFilter::render", FilterRender},
    {0x003b0804, "uMotionBlurFilter::render", FilterRender},
    {0x001b0890, "uDOFFilter::render", FilterRender},
    {0x002bd53c, "uBokehFilter::render", FilterRender},
    {0x003b2938, "uRadialBlurFilter::render", FilterRender},
    {0x002d3168, "uMultiBlurFilter::render", FilterRender},
    {0x0025655c, "uHazeFilter::render", FilterRender},
    {0x003dd740, "uColorCorrectFilter::render", FilterRender},
    {0x002bffc0, "uColorFilter::render", FilterRender},
    {0x003a1a6c, "uCrossFadeFilter::render", FilterRender},
    {0x003aeccc, "uImagePlaneFilter::render", FilterRender},
    {0x0040a218, "uImagePlaneFilterRange::render", FilterRender},
    {0x003808ac, "uOutlineFilter::render", FilterRender},
    {0x00385c34, "uTVNoiseFilter::render", FilterRender},
};

/// Where a hook point is in this library: the libraries name their hook points, so another
/// version of the game (EUR, whose code sits a little further along) finds them by name;
/// a library without names is USA v1.3.0's.
u32 HookAddress(const RCodeInfo& info, const Hook& hook) {
    if (info.hook_names) {
        for (u32 i = 0; i < info.hook_count; ++i) {
            if (info.hook_names[i] && std::string_view(info.hook_names[i]) == hook.name) {
                return info.hook_addrs[i];
            }
        }
    }
    return hook.addr;
}

} // namespace

void SetPlatform(Platform* p) {
    platform = p ? p : &null_platform;
}

void SetWindowSize(std::uint32_t width, std::uint32_t height) {
    window_width = width;
    window_height = height;
}

std::vector<std::string> Install(const RCodeInfo& info) {
    std::vector<std::string> log;
    options = LoadOptions(log);
    if (options.widescreen_match) {
        // "match": the top screen takes the window's shape. It has to be resolved here, before
        // the game's first area (the layout and the code's size tunables are fixed in this
        // call) and the renderer does not exist yet, so the frontend passed its size.
        options.widescreen = window_height > 0 && window_width > window_height
                                 ? static_cast<float>(window_width) / window_height
                                 : 0.0f;
        log.push_back(options.widescreen > 0
                          ? fmt::format("widescreen: match, the window's shape {:.4f} ({}x{})",
                                        options.widescreen, window_width, window_height)
                          : "widescreen: match, but no landscape window size was passed; off");
    }
    // a new boot (Azahar can start the game again in the same process): nothing carries over
    // from the last one, whose emulated clock and game objects are gone
    frames = 0;
    owner_regions.clear();
    quest_start_held = 0;
    target_held = 0;
    skip_offered = false;
    skip_held = skip_tap_direct = skip_tap_seen = skip_complete = false;
    skip_tap = 0;
    skip_polls = 0;
    end_hold_since = 0;
    end_hold_fraction = 0.0f;
    end_requested = false;
    end_cut = false;
    last_panel_touch = 0xFFFFFFFF;
    quest_panels_ran = false;
    quest_map_slot = -1;
    quest_map_drawn = false;
    quest_choice = {};
    last_panel_states = 0xFFFFFFFF;
    filters_drawn = filters_skipped = 0;
    filters_logged = skipped_logged = 0xFFFFFFFF;
    filters_since = 0;
    camera_turn_yaw = camera_turn_pitch = 0.0;
    camera_hook_ran_at = 0;
    camera_rest_yaw = camera_rest_pitch = 0.0f;
    lock_on = false;
    lock_followed_at = 0;
    lock_hand_at = 0;
    lock_monster = 0;
    lock_time = 0;
    for (int k = 0; k < HUD_PART_COUNT; ++k) {
        hud_drawn_at[k] = 0;
    }
    for (int i = 0; i < HUD_OBJECT_COUNT; ++i) {
        hud_object_drawn_at[i] = 0;
    }
    {
        std::lock_guard lock(game_state_mutex);
        game_state = game_state_logged = GameState{};
    }
    // local wireless play over network rooms, for any program on recompiled code
    P().InstallNetSync(options.net_sync, options.net_sync_slack, log);
    if (!info.set_hook || info.hook_count == 0) {
        return log; // a test program, or a library from before hook points
    }
    if (options.fps_cap > 0) {
        log.push_back(fmt::format("frame-rate cap {}", options.fps_cap));
    }
    if (RefreshRate() > 0) {
        log.push_back(fmt::format("emulated screen refresh rate {} Hz{} (experimental)", RefreshRate(),
                                  options.refresh_rate > 0 ? "" : ", as fps"));
    }
    if (options.widescreen > 0) {
        log.push_back(WideRows() ? fmt::format("widescreen: top screen rendered {}x{} (experimental)",
                                               WideRows(), LCD_COLUMNS)
                                 : fmt::format("widescreen: 3D view {} to aspect ratio {:.4f}",
                                               options.widescreen > NATIVE_ASPECT ? "widened" : "narrowed",
                                               options.widescreen));
        SetScreenAspect(options.widescreen, log);
        if (WideRows()) {
            // the code's own 400s for the top screen (config/tunables.txt)
            const u32 set = SetTunables(info, "top_rows", WideRows());
            log.push_back(fmt::format("widescreen: {} of the code's top-screen sizes set to {}", set,
                                      WideRows()));
            if (options.hud_edges) {
                log.push_back(fmt::format("widescreen: the hunting HUD's sides moved {} pixels out to the edges",
                                          (WideRows() - TOP_ROWS) / 2));
            }
        }
    }
    if (options.trace_every) {
        log.push_back(fmt::format("tracing frame timing every {} frames", options.trace_every));
    }
    if (options.trace_panels) {
        log.push_back("tracing touch panels");
    }
    if (options.kick_button) {
        log.push_back(fmt::format("kick on pad button {:04X}", options.kick_button));
    }
    if (options.target_button) {
        log.push_back(fmt::format("target camera on pad button {:04X}", options.target_button));
    }
    if (options.skip_button) {
        log.push_back(fmt::format("skip after a quest on {} (a tap on the touch screen's button {})",
                                  PadName(options.skip_button), options.skip_panel));
    }
    if (options.skip_end_buttons) {
        log.push_back(fmt::format("the end of the wait after a quest on {} held {:.1f} s, with every port "
                                  "player's say-so",
                                  PadNames(options.skip_end_buttons), options.skip_end_hold));
    }
    P().SetScreenOverlay(options.touch_overlay);
    if (options.touch_overlay.height > 0 || options.widescreen > 0) {
        // Frame: the layout this boot computed before these options were read (Android works
        // it out as the surface appears, before the game starts) has to be worked out again
        // on the first frame. The portrait flag is read here because Install runs on the Java
        // thread that loads the code library, while Frame runs on the guest fiber.
        relayout_for_overlay = true;
        relayout_portrait = P().IsPortrait();
    }
    applied_map_slot = applied_target_slot = -1;
    if (const auto& overlay = options.touch_overlay; overlay.height > 0) {
        constexpr const char* corners[] = {"top left", "top right", "bottom left", "bottom right"};
        log.push_back(fmt::format("custom screen layout: the touch screen over the top screen, {}, "
                                  "{:.0f}% of the window's height, {:.0f}% opaque",
                                  corners[static_cast<int>(overlay.corner)], overlay.height * 100,
                                  overlay.opacity * 100));
        if (options.quest_overlay.on) {
            const Placement map = MapPlacement(overlay, options.quest_overlay);
            log.push_back(fmt::format("in quests the touch overlay shows the map alone, {:.1f}% of the "
                                      "window's height at {:.1f}% {:.1f}%",
                                      map.height * 100, map.x * 100, map.y * 100));
            if (options.target_overlay.on) {
                const Placement target = TargetPlacement(overlay, options.quest_overlay, options.target_overlay);
                log.push_back(fmt::format("and the target camera panel, {:.1f}% of the window's height at "
                                          "{:.1f}% {:.1f}%",
                                          target.height * 100, target.x * 100, target.y * 100));
            }
        }
    }
    if (options.trace_layout) {
        log.push_back(fmt::format("tracing HUD layout objects every {} frames", options.trace_layout));
    }
    if (!options.dither) {
        log.push_back("dithering off");
    }
    if (!options.stereo_3d) {
        log.push_back("stereoscopic 3D off: the top screen drawn once");
    }
    if (!options.antialiasing) {
        log.push_back("the game's anti-aliasing off");
    }
    if (options.fov != 1.0f) {
        log.push_back(fmt::format("field of view x{:.2f}", options.fov));
    }
    if (options.filters_off) {
        std::string off;
        for (const auto& [name, bit] : FILTER_OPTIONS) {
            if (options.filters_off & bit) {
                off += (off.empty() ? "" : ", ") + std::string(name);
            }
        }
        log.push_back("screen filters off: " + off);
    }
    if (options.trace_filters) {
        log.push_back("tracing screen filters");
    }
    if (options.trace_state) {
        log.push_back("tracing the game's state (menus, the weapon, the item bar)");
    }
    if (options.render_set || options.render_clear) {
        log.push_back(fmt::format("render flags: set {:08X}, clear {:08X} (experimental)",
                                  options.render_set, options.render_clear));
    }
    missing_hook_points = 0;
    panel_touch_allowed_return = PANEL_TOUCH_ALLOWED_RETURN;
    for (const Hook& hook : HOOKS) {
        const u32 addr = HookAddress(info, hook);
        if (!info.set_hook(addr, hook.fn)) {
            log.push_back(fmt::format("no hook point for {} at {:08X} (rebuild the code library)",
                                      hook.name, addr));
            ++missing_hook_points;
        } else if (hook.addr == PANEL_PROCESS_TOUCH) {
            panel_touch_allowed_return = addr + (PANEL_TOUCH_ALLOWED_RETURN - PANEL_PROCESS_TOUCH);
        }
    }
    if (info.title_id != 0 && info.hook_names && info.hook_count > 0 &&
        HookAddress(info, HOOKS[0]) != HOOKS[0].addr) {
        log.push_back(fmt::format("hook points found by name: game {:016X}", info.title_id));
    }
    return log;
}

void AddCameraTurn(float yaw_degrees, float pitch_degrees) {
    const auto add = [](std::atomic<double>& total, double v) {
        double now = total.load(std::memory_order_relaxed);
        while (!total.compare_exchange_weak(now, now + v, std::memory_order_relaxed)) {
        }
    };
    add(camera_turn_yaw, yaw_degrees);
    add(camera_turn_pitch, pitch_degrees);
}

bool CameraTurnActive() {
    const s64 at = camera_hook_ran_at.load(std::memory_order_relaxed);
    return at != 0 && SteadyNs() - at < 300'000'000;  // within 0.3 s
}

void ClearCameraTurn() {
    camera_turn_yaw = camera_turn_pitch = 0.0;
}

void SetHostTriggers(bool zl, bool zr) {
    host_triggers.store((zl ? PadBit("ZL") : 0u) | (zr ? PadBit("ZR") : 0u), std::memory_order_relaxed);
}

void SetLockOn(bool on) {
    if (lock_on.exchange(on) != on) {
        LogInfo("lock-on {}", on ? "on" : "off");
    }
}

bool LockOn() {
    return lock_on.load(std::memory_order_relaxed);
}

int TargetCamMonster() {
    return target_cam_now.load(std::memory_order_relaxed);
}

bool LockOnFollowing() {
    const s64 at = lock_followed_at.load(std::memory_order_relaxed);
    return lock_on.load(std::memory_order_relaxed) && at != 0 && SteadyNs() - at < 300'000'000;
}

int MissingHookPoints() {
    return missing_hook_points;
}

GameState CurrentGameState() {
    std::lock_guard lock(game_state_mutex);
    return game_state;
}

void ToggleQuestMap() {
    const bool off = !quest_map_off.load();
    quest_map_off = off;
    LogInfo("touch overlay: {} in quests", off ? "the whole touch screen" : "the map");
}

bool SkipOffered() {
    return skip_offered.load(std::memory_order_relaxed);
}

std::string SkipEndButtons() {
    return PadNames(options.skip_end_buttons);
}

float EndSkipHold() {
    return end_hold_fraction.load(std::memory_order_relaxed);
}

bool EndSkipRequested() {
    return end_requested.load(std::memory_order_relaxed);
}

std::string SkipButton() {
    return PadName(options.skip_button);
}

int SkipHint() {
    return options.skip_hint;
}

const char* HudPartKey(int part) {
    return part >= 0 && part < HUD_PART_COUNT ? HUD_KEYS[part] : "";
}

void TapTargetCamera() {
    target_tap_asked = true;
}

void HoldItemBarOpen(bool open) {
    item_open_asked = open;
}

void ScrollItemBar(int steps) {
    if (steps == 0) {
        return;
    }
    item_steps_at = SteadyNs();
    // (a few waiting at most: a long swipe while the hunter can't pick an item doesn't run on afterwards)
    const int now = item_steps_asked.load(std::memory_order_relaxed);
    item_steps_asked = std::clamp(now + steps, -4, 4);
}

bool SetOption(const std::string& key, const std::string& value) {
    if (key == "lock_on") {  // (the runner's --set, for tests: the app calls SetLockOn)
        SetLockOn(IsOn(value));
        return true;
    }
    if (key == "target_tap") {  // (tests: the app calls TapTargetCamera)
        TapTargetCamera();
        return true;
    }
    if (key == "item_open") {  // (tests: the app calls HoldItemBarOpen)
        HoldItemBarOpen(IsOn(value));
        return true;
    }
    if (key == "item_step") {  // (the runner's --set, for tests: the app calls ScrollItemBar)
        item_test_steps += static_cast<int>(std::strtol(value.c_str(), nullptr, 10));
        return true;
    }
    // the options the hooks read each time they run; the others are applied once at start-up
    static constexpr std::string_view live[] = {
        "fps", "refresh_rate", "steady", "dither", "stereo_3d", "antialiasing", "fov", "render_flags_set", "render_flags_clear",
        "kick",
        "target", "skip", "skip_panel", "skip_hint", "skip_end", "skip_end_hold", "touch_overlay",
        "quest_overlay", "target_overlay",
        "hud_edges", "trace_frames", "trace_layout", "trace_panels", "bloom", "motion_blur",
        "depth_of_field", "radial_blur", "haze", "trace_filters", "trace_state", "item_bar", "hud_probe"};
    if (std::find(std::begin(live), std::end(live), key) == std::end(live) && HudPartIndex(key) < 0) {
        return false;
    }
    Set(options, key, value);
    if ((key == "refresh_rate" || key == "fps") && frames > 0) {
        P().SetRefreshRate(RefreshRate()); // 0: the 3DS's
    }
    if (key == "touch_overlay" || key == "quest_overlay" || key == "target_overlay") {
        // the corner as the options now say, with the quest's map in it if it shows the map
        ApplyQuestOverlay(true);
    }
    return true;
}

void SetLayoutEditing(bool on) {
    if (on != layout_editing) {
        layout_editing = on;
        LogInfo("touch overlay: {}", on ? "the layout editor is up" : "the layout editor is done");
    }
    ApplyQuestOverlay(true);
}

ScreenOverlay ParseOverlay(std::string v) {
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return std::tolower(c); });
    using Corner = ScreenOverlay::Corner;
    ScreenOverlay overlay{0.30f, 0.85f, Corner::TopRight};
    std::istringstream words(v);
    std::string corner;
    words >> corner;
    std::replace(corner.begin(), corner.end(), '_', '-');
    const std::pair<const char*, Corner> corners[] = {{"top-left", Corner::TopLeft},
                                                      {"top-right", Corner::TopRight},
                                                      {"bottom-left", Corner::BottomLeft},
                                                      {"bottom-right", Corner::BottomRight}};
    const auto it = std::find_if(std::begin(corners), std::end(corners),
                                 [&](const auto& c) { return corner == c.first; });
    if (it == std::end(corners)) {
        return IsOn(corner) ? overlay : ScreenOverlay{};
    }
    overlay.corner = it->second;
    std::string height, opacity;
    if (words >> height) {
        overlay.height = std::clamp(std::strtof(height.c_str(), nullptr) / 100.0f, 0.05f, 1.0f);
    }
    if (words >> opacity) {
        overlay.opacity = std::clamp(std::strtof(opacity.c_str(), nullptr) / 100.0f, 0.1f, 1.0f);
    }
    return overlay;
}

namespace {

/// The rest of a panel view's option after its first word: "[SIZE] [at X Y]", in %.
PanelView ParsePanelView(std::istringstream& words) {
    PanelView view{true};
    std::string word;
    while (words >> word) {
        if (word == "at") {
            std::string x, y;
            if (words >> x >> y) {
                view.placed = true;
                view.x = std::clamp(std::strtof(x.c_str(), nullptr) / 100.0f, 0.0f, 1.0f);
                view.y = std::clamp(std::strtof(y.c_str(), nullptr) / 100.0f, 0.0f, 1.0f);
            }
            break;
        }
        view.height = std::clamp(std::strtof(word.c_str(), nullptr) / 100.0f, 0.03f, 1.0f);
    }
    return view;
}

/// A fraction as a percentage to 0.1, without a needless ".0".
std::string Percent(float fraction) {
    return fmt::format("{:g}", std::round(fraction * 1000.0f) / 10.0f);
}

std::string FormatPanelView(const char* on, const PanelView& view) {
    if (!view.on) {
        return "off";
    }
    std::string text = on;
    if (view.height > 0) {
        text += " " + Percent(view.height);
    }
    if (view.placed) {
        text += " at " + Percent(view.x) + " " + Percent(view.y);
    }
    return text;
}

std::string LowerCase(std::string v) {
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return std::tolower(c); });
    return v;
}

/// The part of the touch screen in `slot` that is `rows` slots tall, in fractions.
void CropToSlots(float& left, float& top, float& right, float& bottom, int slot, int rows) {
    // 0-2 are the left column from the top, 3-5 the right one
    const int column = slot / 3, row = slot % 3;
    const float x0 = static_cast<float>(column * SLOT_WIDTH);
    const float y0 = static_cast<float>(SLOTS_TOP + row * SLOT_HEIGHT);
    left = x0 / kBottomScreenWidth;
    right = (x0 + SLOT_WIDTH) / kBottomScreenWidth;
    top = y0 / kBottomScreenHeight;
    bottom = (y0 + static_cast<float>(rows * SLOT_HEIGHT)) / kBottomScreenHeight;
}

} // namespace

PanelView ParseQuestOverlay(std::string v) {
    std::istringstream words(LowerCase(v));
    std::string what;
    words >> what;
    if (what != "map" && !IsOn(what)) {
        return {};
    }
    return ParsePanelView(words);
}

PanelView ParseTargetOverlay(std::string v) {
    std::istringstream words(LowerCase(v));
    std::string what;
    words >> what;
    if (what != "target" && !IsOn(what)) {
        return {};
    }
    return ParsePanelView(words);
}

std::string FormatQuestOverlay(const PanelView& map) {
    return FormatPanelView("map", map);
}

std::string FormatTargetOverlay(const PanelView& target) {
    return FormatPanelView("on", target);
}

Placement MapPlacement(const ScreenOverlay& touch, const PanelView& map) {
    using Corner = ScreenOverlay::Corner;
    Placement p;
    // at the touch overlay's own scale unless a height is given
    p.height = map.height > 0 ? map.height
                              : touch.height * static_cast<float>(2 * SLOT_HEIGHT) / kBottomScreenHeight;
    if (map.placed) {
        p.x = map.x;
        p.y = map.y;
    } else {
        p.x = touch.corner == Corner::TopLeft || touch.corner == Corner::BottomLeft ? 0.0f : 1.0f;
        p.y = touch.corner == Corner::TopLeft || touch.corner == Corner::TopRight ? 0.0f : 1.0f;
    }
    return p;
}

Placement TargetPlacement(const ScreenOverlay& touch, const PanelView& map, const PanelView& target) {
    Placement p;
    p.height = target.height > 0 ? target.height
                                 : touch.height * static_cast<float>(SLOT_HEIGHT) / kBottomScreenHeight;
    p.height = std::min(p.height, 1.0f);
    if (target.placed) {
        p.x = target.x;
        p.y = target.y;
        return p;
    }
    // right under the map (over it if there is no room), on the same side: positions are in
    // fractions of the window's height, so this holds at any window size
    const Placement m = MapPlacement(touch, map);
    const float map_top = m.y * (1.0f - std::min(m.height, 1.0f));
    const float under = map_top + m.height;
    const float top = under + p.height <= 1.0f ? under : std::max(0.0f, map_top - p.height);
    p.x = m.x;
    p.y = p.height < 1.0f ? std::clamp(top / (1.0f - p.height), 0.0f, 1.0f) : 0.0f;
    return p;
}

ScreenOverlay QuestOverlay(const ScreenOverlay& touch, int map_slot, const PanelView& map,
                                   int target_slot, const PanelView& target) {
    ScreenOverlay overlay = touch;
    // the map takes two slots, one above the other
    CropToSlots(overlay.crop_left, overlay.crop_top, overlay.crop_right, overlay.crop_bottom, map_slot, 2);
    const Placement m = MapPlacement(touch, map);
    overlay.height = m.height;
    overlay.x = m.x;
    overlay.y = m.y;
    overlay.views = {};
    if (target_slot >= 0 && target_slot < static_cast<int>(PANEL_SLOTS) && target.on) {
        auto& view = overlay.views[0];
        CropToSlots(view.crop_left, view.crop_top, view.crop_right, view.crop_bottom, target_slot, 1);
        const Placement t = TargetPlacement(touch, map, target);
        view.height = t.height;
        view.x = t.x;
        view.y = t.y;
    }
    return overlay;
}

bool QuestOverlayChoice::Update(double time, bool panels, int map_slot, bool map_drawn, bool touch_ui,
                                int forced) {
    if (panels || map_drawn) {
        panels_seen = true;
        panels_last = time;
    }
    const bool gone = !panels_seen || time - panels_last >= HOLD;
    const int want = forced != FREE     ? forced
                     : panels           ? map_slot
                     : touch_ui || gone ? -1
                                        : shown; // a short break: keep what is shown
    if (want != next) {
        next = want;
        next_since = time;
    }
    if (want == shown || time - next_since < SETTLE) {
        return false;
    }
    shown = want;
    return true;
}

} // namespace Port::Hooks
