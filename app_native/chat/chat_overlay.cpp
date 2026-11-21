// The chat over the game: chat_overlay.h. In the touch controls' look: a dark plate with a bronze edge and a gold rule;
// each line on a wine-dark plate with its chat's colour at its left (Global gold, General green, Hub blue, a friend pink),
// and its sender's name in a colour of its own (name_ink, 1.0.2).
#include "chat_overlay.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "chat.h"
#include "chat_text.h"
#include "imgui.h"

namespace chat {
namespace {
OverlayPort g_port;
struct Shown {
    Line line;
    double at;
};
std::vector<Shown> g_shown;
unsigned g_seen_serial;
bool g_serial_known;
// the button: its centre as fractions of the screen, its size (1: its own); a drag under way
constexpr float HOME_X = .11f, HOME_Y = .30f;  // (right of the column of buttons at the left edge, over the stick)
float g_bx = HOME_X, g_by = HOME_Y, g_bs = 1;
bool g_loaded, g_drag, g_dragged, g_on_screen;
float g_drag_dx, g_drag_dy, g_down_x, g_down_y, g_w = 1, g_h = 1;
constexpr double SHOWN = 8;

void load() {
    if (g_loaded) return;
    g_loaded = true;
    float x, y, s;
    if (bubble(&x, &y, &s)) g_bx = std::clamp(x, 0.f, 1.f), g_by = std::clamp(y, 0.f, 1.f), g_bs = std::clamp(s, .6f, 2.f);
}
bool g_placed;  // (1.0.2: the port placed it, the editor's place and size: placed())
float g_pr;      // (and its radius, a fraction of the screen's height)
bool placed() {
    float x, y, r;
    g_placed = g_port.place && g_port.place(&x, &y, &r);
    if (g_placed) g_bx = std::clamp(x, 0.f, 1.f), g_by = std::clamp(y, 0.f, 1.f), g_pr = r;
    return g_placed;
}
float radius() { return g_placed ? g_h * g_pr : g_h * .048f * g_bs; }
bool menu_open() { return g_port.menu_open && g_port.menu_open(); }
bool lines_hidden() { return menu_open() || (g_port.lines_hidden && g_port.lines_hidden()); }
bool wanted() {  // (the online chat on, or the hub's chat on and a hub up)
#ifdef __ANDROID__
    return bubble_shown() && g_port.controls_shown && g_port.controls_shown() && !menu_open() && (enabled() || (hub_chat() && hub_up()));
#else
    return false;
#endif
}
bool counts(int channel) {
    int o = overlay();
    return o == OVERLAY_ALL || (o == OVERLAY_HUB_FRIENDS && (channel == HUB || channel == FRIEND));
}
ImU32 ink(int c) {
    switch (c) {
    case GLOBAL: return IM_COL32(255, 214, 92, 255);
    case GENERAL: return IM_COL32(150, 214, 110, 255);
    case HUB: return IM_COL32(120, 200, 255, 255);
    default: return IM_COL32(244, 150, 200, 255);
    }
}
std::string mark(int c) { return c == FRIEND ? std::string(t("Friend", "Amigo")) : c == GENERAL ? std::string(t("General", "General")) : channel_name(c); }
ImU32 faded(ImU32 c, float a) { return (c & 0x00ffffffu) | ((ImU32)(((c >> 24) & 0xff) * a) << 24); }
}  // namespace

void overlay_setup(const OverlayPort &port) { g_port = port; }
unsigned channel_ink(int channel) { return ink(channel); }

unsigned name_ink(const std::string &name) {
    // (warm and cool tones that read on the dark plates and strips, apart from the chats' gold, green, blue and pink)
    static const unsigned inks[] = {
        IM_COL32(255, 168, 92, 255),  IM_COL32(255, 124, 112, 255), IM_COL32(198, 156, 255, 255), IM_COL32(112, 228, 204, 255),
        IM_COL32(240, 232, 128, 255), IM_COL32(178, 226, 96, 255),  IM_COL32(150, 176, 255, 255), IM_COL32(232, 184, 140, 255),
        IM_COL32(118, 222, 146, 255), IM_COL32(255, 204, 156, 255), IM_COL32(222, 140, 232, 255), IM_COL32(160, 214, 236, 255),
    };
    uint32_t h = 2166136261u;  // (FNV-1a of the name, its letters' case aside: "Ana" and "ana" the same colour)
    for (unsigned char c : name) h = (h ^ (unsigned)((c >= 'A' && c <= 'Z') ? c + 32 : c)) * 16777619u;
    return inks[h % (sizeof inks / sizeof inks[0])];
}

bool overlay_frame() {
    if (!active()) {  // (both chats off, as they come: nothing over the game, and the button takes no finger)
        if (g_serial_known || g_on_screen) g_shown.clear(), g_on_screen = g_drag = false, g_serial_known = false;
        return false;
    }
    unsigned last = last_serial();
    if (!g_serial_known) g_seen_serial = last, g_serial_known = true;
    double now = SDL_GetTicks() / 1000.0;
    const bool menu = lines_hidden();  // (1.0.2: a menu of the game's too, and the chat window up: none, those up gone)
    if (last != g_seen_serial) {
        for (const Line &l : since(g_seen_serial))
            if (!l.mine && counts(l.channel) && !menu) g_shown.push_back({l, now});
        g_seen_serial = last;
        if (g_shown.size() > 4) g_shown.erase(g_shown.begin(), g_shown.end() - 4);
    }
    g_shown.erase(std::remove_if(g_shown.begin(), g_shown.end(), [now](const Shown &s) { return now - s.at > SHOWN; }), g_shown.end());
    if (menu) g_shown.clear();
    const bool want = wanted();
    if (!want) g_on_screen = false;  // (gone - the hub left, say - and not drawn: no finger for it either)
    return !g_shown.empty() || want;
}

void overlay_draw(float w, float h) {
    g_w = w, g_h = h;
    load();
    placed();
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    ImFont *font = ImGui::GetFont();
    const float scale = std::max(1.f, h / 720.f), line = 2 * scale;
    const double now = SDL_GetTicks() / 1000.0;
    g_on_screen = wanted();
    const float bx = g_bx * w, by = g_by * h, br = radius();
    const ImU32 plate = IM_COL32(28, 20, 15, 215), edge = IM_COL32(112, 76, 34, 255), rule = IM_COL32(222, 186, 96, 255),
                cream = IM_COL32(244, 232, 200, 255);
    if (g_on_screen) {  // (a round plate, a speech bubble in it, the lines not read in a red seal)
        dl->AddCircleFilled(ImVec2(bx, by + line), br, IM_COL32(0, 0, 0, 90), 40);
        dl->AddCircleFilled(ImVec2(bx, by), br, g_drag && g_dragged ? IM_COL32(92, 62, 22, 235) : plate, 40);
        dl->AddCircle(ImVec2(bx, by), br, edge, 40, line * 1.4f);
        dl->AddCircle(ImVec2(bx, by), br - line * 2.2f, rule, 40, std::max(1.f, line * .6f));
        const float bw = br * .95f, bh = br * .62f;
        const ImVec2 a(bx - bw * .5f, by - bh * .62f), b(bx + bw * .5f, by + bh * .38f);
        dl->AddRectFilled(a, b, cream, bh * .35f);
        const ImVec2 tail[3] = {ImVec2(bx - bw * .22f, b.y - 1), ImVec2(bx - bw * .34f, b.y + bh * .38f), ImVec2(bx + bw * .02f, b.y - 1)};
        dl->AddConvexPolyFilled(tail, 3, cream);
        for (int i = -1; i <= 1; i++) dl->AddCircleFilled(ImVec2(bx + i * bw * .22f, (a.y + b.y) * .5f), bh * .09f, plate, 10);
        int unread = 0;
        for (int c = 0; c < CHANNELS; c++)
            if (counts(c) || c == FRIEND) unread += chat::unread(c);
        if (unread) {
            char n[8];
            snprintf(n, sizeof n, "%d", std::min(unread, 99));
            const float fs = br * .62f, rr = br * .42f;
            const ImVec2 c(bx + br * .72f, by - br * .72f);
            dl->AddCircleFilled(c, rr, IM_COL32(176, 34, 30, 255), 24);
            dl->AddCircle(c, rr, rule, 24, std::max(1.f, line * .6f));
            const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, n);
            dl->AddText(font, fs, ImVec2(c.x - ts.x * .5f, c.y - ts.y * .5f), cream, n);
        }
    }
    if (g_shown.empty()) return;
    // the lines: plates beside the button (or where it would be), the newest at the bottom, fading out
    const float fs = ImGui::GetFontSize() * 1.3f, pad = 7 * scale, wrap = std::min(w * .45f, 640 * scale);
    float x = g_on_screen ? bx + br + 8 * scale : bx, y = by - br;
    if (x + wrap + 2 * pad > w) x = std::max(4.f, bx - br - 8 * scale - wrap - 2 * pad);
    for (const Shown &s : g_shown) {
        const float age = (float)(now - s.at), a = std::clamp(std::min(age / .15f, ((float)SHOWN - age) / .6f), 0.f, 1.f);
        const std::string who = s.line.system ? std::string() : s.line.name + ": ", tag = mark(s.line.channel) + "  ";
        const std::string text = line_text(s.line);
        const ImVec2 ms = font->CalcTextSizeA(fs * .8f, FLT_MAX, 0, tag.c_str()), ws = font->CalcTextSizeA(fs, FLT_MAX, 0, who.c_str());
        const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, std::max(fs * 4, wrap - ws.x - ms.x), text.c_str());
        const float pw = ms.x + ws.x + ts.x + 2 * pad, ph = std::max(ts.y, fs) + 2 * pad;
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + pw, y + ph), faded(IM_COL32(24, 12, 11, 235), a));  // (1.0.2: darker, to read over grass and snow)
        {  // (the edge as four lines: AddRect's arguments changed order between the ports' Dear ImGui versions)
            const ImU32 e = faded(edge, a);
            const float t = line * .7f;
            dl->AddLine(ImVec2(x, y), ImVec2(x + pw, y), e, t), dl->AddLine(ImVec2(x + pw, y), ImVec2(x + pw, y + ph), e, t);
            dl->AddLine(ImVec2(x + pw, y + ph), ImVec2(x, y + ph), e, t), dl->AddLine(ImVec2(x, y + ph), ImVec2(x, y), e, t);
        }
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + line * 1.5f, y + ph), faded(ink(s.line.channel), a));
        float tx = x + pad;
        dl->AddText(font, fs * .8f, ImVec2(tx, y + pad + fs * .12f), faded(ink(s.line.channel), a), tag.c_str());
        tx += ms.x;
        if (!who.empty()) dl->AddText(font, fs, ImVec2(tx, y + pad), faded(name_ink(s.line.name), a), who.c_str());
        const float tw = std::max(fs * 4, wrap - ws.x - ms.x);
        dl->AddText(font, fs, ImVec2(tx + ws.x + scale, y + pad + scale), faded(IM_COL32(0, 0, 0, 255), a), text.c_str(), nullptr, tw);
        dl->AddText(font, fs, ImVec2(tx + ws.x, y + pad), faded(cream, a), text.c_str(), nullptr, tw);
        y += ph + 3 * scale;
    }
}

bool bubble_down(float fx, float fy) {
    if (!g_on_screen) return false;  // (its place: the last drawn's - the port's place is asked as it draws, placed())
    const float x = fx * g_w, y = fy * g_h;
    if (std::hypot(x - g_bx * g_w, y - g_by * g_h) > radius() * 1.15f) return false;
    g_drag = true, g_dragged = false;
    g_down_x = x, g_down_y = y, g_drag_dx = g_bx * g_w - x, g_drag_dy = g_by * g_h - y;
    return true;
}
void bubble_finger(bool up, float fx, float fy) {
    if (!g_drag) return;
    const float x = fx * g_w, y = fy * g_h;
    if (std::hypot(x - g_down_x, y - g_down_y) > g_h * .02f) g_dragged = true;
    if (g_dragged && !g_placed) {
        const float r = radius();
        g_bx = std::clamp((x + g_drag_dx) / g_w, r / g_w, 1 - r / g_w);
        g_by = std::clamp((y + g_drag_dy) / g_h, r / g_h, 1 - r / g_h);
    }
    if (!up) return;
    g_drag = false;
    if (g_dragged) {
        if (!g_placed) set_bubble(g_bx, g_by, g_bs);
        return;
    }
    if (g_port.log) g_port.log("[chat] the chat button: the menu's chat");
    if (g_port.open_chat) g_port.open_chat();
}
void bubble_size(float s) {
    load();
    g_bs = std::clamp(s, .6f, 2.f);
    set_bubble(g_bx, g_by, g_bs);
}
float bubble_scale() {
    load();
    return g_bs;
}
void bubble_reset() {
    load();
    g_bx = HOME_X, g_by = HOME_Y, g_bs = 1;
    set_bubble(g_bx, g_by, g_bs);
}

}  // namespace chat
