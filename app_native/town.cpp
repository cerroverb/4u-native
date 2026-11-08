// The town's touch-only actions without the touch screen (town.h).
//
// The town's touch screen (the same with the single-screen mod, and in any shape of the window: it is the 3DS's own 320 x
// 240) has Guild Cards, Gestures and Friends in a row at its top and Hunters for Hire at the right; a tap on the first
// three opens the game's menu on the touch screen, on the fourth the Hunters for Hire panel takes the map's place (the
// hunter is still walked; its Town Map button, where Hunters for Hire was, brings the map back). The touch screen's page
// (cLbMain+0x172d5, the game state's town_panel) says which: 2 the map, 18 Hunters for Hire, the menus' own pages
// else. (The game state's menu, cLbMain+0x225, stays 1 after a gesture is done and its menu closed, until the hunter
// moves: it doesn't say the menu closed.) build/town_runs (mh4n's runner, the single-screen mod) has the screenshots.
#include "town.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "controls.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "mh4n/host.h"
#include "settings_icons.h"
#include "settings_locale.h"
#include "settings_panel.h"
#include "touch.h"

namespace {

enum Action { A_CARDS, A_GESTURES, A_FRIENDS, A_HIRE, A_COUNT };
struct Spot {
    const char* name;
    int x, y;  // the button's middle on the town's touch screen
};
const Spot SPOTS[A_COUNT] = {
    {"Guild Cards", 60, 51}, {"Gestures", 150, 51}, {"Friends", 232, 51}, {"Hunters for Hire", 292, 135}};
const int TAP_FRAMES = 8;     // a tap's length, in the game's frames
const int OPEN_WAIT = 150;    // frames a menu has to open in after the tap
const int BACK_FRAMES = 6;    // a B of the module's, in frames
const int CLOSE_EVERY = 16;   // closing a menu: a B this often while it is open (one page back each)
const int CLOSE_MOST = 10;    // and no more than these
const float ANIM_MS = 130.f;  // the list's and the large screen's fade and scale, in and out

// LIST the list; TAP the action's tap; WAIT for its menu; VIEW_MENU / VIEW_PANEL the large touch screen (a menu of the
// game's, the Hunters for Hire panel); RESTORE the panel's Town Map tapped; CLOSING the menu closed with Bs
enum Mode { IDLE, LIST, TAP, WAIT, VIEW_MENU, VIEW_PANEL, RESTORE, CLOSING };
Mode g_mode = IDLE;
int g_action = 0, g_sel = 0, g_out_action = 0;
int g_place = -1, g_menu = -1, g_menu_zero = 0, g_panel = -1;
bool g_panel_seen = false;  // (the Hunters for Hire panel was seen up since the large screen came)
const int PANEL_MAP = 2, PANEL_HIRE = 18;
// the touch screen at rest, no menu on it: the map or Hunters for Hire (not known: the game's menu byte says)
bool at_rest() { return g_panel >= 0 ? g_panel == PANEL_MAP || g_panel == PANEL_HIRE : g_menu == 0; }
std::uint32_t g_frame = 0, g_menu_since = 0;
int g_tap_x = 0, g_tap_y = 0;
std::uint32_t g_tap_from = 0, g_wait_from = 0, g_back_from = 0, g_close_from = 0;
int g_close_pulses = 0;
// the panel's cursor (the 3DS's touch screen pixels), held down while A is (a tap's length at least)
float g_cx = 160, g_cy = 120;
bool g_cursor_down = false;
std::uint32_t g_cursor_from = 0;
std::uint32_t g_prev_held = ~0u;
Uint64 g_last_ms = 0, g_repeat_at = 0;
int g_repeat_dir = 0;
// the animations' starts (SDL_GetTicks; 0: none)
Uint64 g_list_in = 0, g_list_out = 0, g_view_in = 0, g_view_out = 0;
// the window at the last frame (the fingers' pixels), the plates' places there, the finger this module follows
float g_w = 0, g_h = 0;
ImRect g_list_rect, g_rows[A_COUNT], g_x_rect, g_back_rect;
SDL_FingerID g_finger = 0;
bool g_finger_on = false, g_finger_view = false, g_finger_outside = false, g_x_held = false, g_back_held = false;
int g_finger_row = -1;
ImVec2 g_finger_p;

const char* T(const char* english) { return settings_text(settings_spanish() ? 1 : 0, english); }
bool is_view(Mode m) { return m == VIEW_MENU || m == VIEW_PANEL; }
float ease(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return 1 - (1 - t) * (1 - t) * (1 - t);
}

void set_mode(Mode m, const char* why) {
    if (m == g_mode) return;
    std::fprintf(stderr, "[town] %d -> %d (%s)\n", (int)g_mode, (int)m, why);
    const Uint64 now = SDL_GetTicks();
    if (g_mode == LIST) g_list_out = now;
    if (m == LIST) g_list_in = now, g_list_out = 0;
    if (is_view(g_mode) && !is_view(m)) g_view_out = now, g_out_action = g_action;
    if (is_view(m) && !is_view(g_mode)) g_view_in = now, g_view_out = 0;
    g_mode = m;
    g_cursor_down = false;
    g_menu_since = 0, g_menu_zero = 0, g_panel_seen = false;
    g_prev_held = ~0u;  // (what is held now is not a press for the next mode)
    g_finger_on = g_x_held = g_back_held = false;
}

void begin_tap(int x, int y) {
    g_tap_x = x, g_tap_y = y, g_tap_from = 0;
}

void pick(int a) {
    g_action = a;
    begin_tap(SPOTS[a].x, SPOTS[a].y);
    g_cx = 160, g_cy = 120;
    if (a == A_HIRE && g_panel == PANEL_HIRE) set_mode(VIEW_PANEL, "the panel was up");  // (a tap would bring the map)
    else set_mode(TAP, SPOTS[a].name);
}

// The panel's own buttons, tapped on the large touch screen (a finger or the cursor): Town Map brings the map back by
// itself, World Map and Move to Hall leave it; the large screen goes away with them, without the closing tap
bool panel_leaves(float x, float y) {
    return (x >= 262 && y >= 100) || (x >= 70 && x <= 190 && y >= 212);
}

// The panel closed by the player: its Town Map tapped, where Hunters for Hire was
void close_panel() {
    begin_tap(SPOTS[A_HIRE].x, SPOTS[A_HIRE].y);
    if (g_panel >= 0 && g_panel != PANEL_HIRE) set_mode(IDLE, "the panel closed");  // (the map is back already)
    else set_mode(RESTORE, "the panel closed");
}

// The large touch screen's place, unscaled: below a band at the top (the FPS counter's, perfstats.cpp: never more than
// 6.5% of the height) and the title plate's
ImRect view_rect(float w, float h) {
    const float vh = std::min(h * .78f, w * .68f * .75f), vw = vh * 4 / 3;
    const float y = h * .15f + (h * .82f - vh) / 2;
    return ImRect(ImVec2(std::floor((w - vw) / 2), std::floor(y)), ImVec2(std::floor((w + vw) / 2), std::floor(y + vh)));
}
// how far in (1) or out (0) the large screen is, -1 not shown
float view_alpha() {
    const Uint64 now = SDL_GetTicks();
    if (is_view(g_mode)) return ease((now - g_view_in) / ANIM_MS);
    if (g_view_out && now - g_view_out < ANIM_MS) return 1 - ease((now - g_view_out) / ANIM_MS);
    return -1;
}

// ---- drawing: the touch controls' colours (touch.cpp): wine-dark plates with red and green rules, bronze and gold
const ImU32 INK = IM_COL32(244, 232, 200, 255), GOLD = IM_COL32(222, 186, 96, 255), BRONZE = IM_COL32(112, 76, 34, 255),
            DEEP = IM_COL32(28, 20, 15, 255), GLOW = IM_COL32(255, 214, 92, 255), RED = IM_COL32(176, 34, 30, 255),
            MOSS = IM_COL32(80, 142, 54, 255), DIM = IM_COL32(204, 186, 160, 255), BRIGHT = IM_COL32(255, 248, 225, 255);

// What was drawn since `from`, faded by a and scaled by s round c (the open and close animations)
void animate(ImDrawList* dl, int from, ImVec2 c, float s, float a) {
    for (int i = from; i < dl->VtxBuffer.Size; ++i) {
        ImDrawVert& v = dl->VtxBuffer.Data[i];
        v.pos.x = c.x + (v.pos.x - c.x) * s, v.pos.y = c.y + (v.pos.y - c.y) * s;
        const float al = ((v.col >> IM_COL32_A_SHIFT) & 0xff) * a;
        v.col = (v.col & ~IM_COL32_A_MASK) | ((ImU32)std::clamp(al, 0.f, 255.f) << IM_COL32_A_SHIFT);
    }
}
void diamond(ImDrawList* dl, ImVec2 c, float d, ImU32 col) {
    ImVec2 q[4] = {ImVec2(c.x, c.y - d), ImVec2(c.x + d, c.y), ImVec2(c.x, c.y + d), ImVec2(c.x - d, c.y)};
    dl->AddConvexPolyFilled(q, 4, col);
}
void text(ImDrawList* dl, ImFont* f, float size, ImVec2 p, ImU32 col, const char* t) {
    float o = std::max(1.f, size * .06f);
    dl->AddText(f, size, ImVec2(p.x + o, p.y + o), IM_COL32(0, 0, 0, 190), t);
    dl->AddText(f, size, p, col, t);
}
// the game's window: wine-dark, a black edge, a red rule with a green one inside it top and bottom, gold studs
void window_plate(ImDrawList* dl, const ImRect& r, float line) {
    dl->AddRectFilled(ImVec2(r.Min.x + line, r.Min.y + line * 1.5f), ImVec2(r.Max.x + line, r.Max.y + line * 1.5f),
                      IM_COL32(0, 0, 0, 120));
    dl->AddRectFilled(r.Min, r.Max, IM_COL32(50, 25, 24, 242));
    dl->AddRect(r.Min, r.Max, IM_COL32(0, 0, 0, 255), 0, line);
    dl->AddRectFilled(r.Min, ImVec2(r.Max.x, r.Min.y + line), RED);
    dl->AddRectFilled(ImVec2(r.Min.x, r.Min.y + line), ImVec2(r.Max.x, r.Min.y + line * 1.7f), MOSS);
    dl->AddRectFilled(ImVec2(r.Min.x, r.Max.y - line * 1.7f), ImVec2(r.Max.x, r.Max.y - line), MOSS);
    dl->AddRectFilled(ImVec2(r.Min.x, r.Max.y - line), r.Max, RED);
    for (ImVec2 c : {r.Min, ImVec2(r.Max.x, r.Min.y), ImVec2(r.Min.x, r.Max.y), r.Max}) diamond(dl, c, line * 1.9f, GOLD);
}
// a row or a button: a dark plate with a bronze edge and a gold rule inside, lit ochre with a glowing edge when picked,
// brighter still while a finger holds it
void row_plate(ImDrawList* dl, const ImRect& r, float line, bool lit, bool pressed = false) {
    float in = line * 1.6f;
    if (lit || pressed) {
        dl->AddRectFilledMultiColor(r.Min, r.Max, IM_COL32(150, 104, 30, 250), IM_COL32(206, 158, 42, 250),
                                    IM_COL32(122, 82, 22, 250), IM_COL32(92, 62, 22, 250));
        if (pressed) dl->AddRectFilled(r.Min, r.Max, IM_COL32(255, 236, 170, 46));
    } else {
        dl->AddRectFilled(r.Min, r.Max, IM_COL32(28, 20, 15, 235));
    }
    dl->AddRect(r.Min, r.Max, lit || pressed ? GLOW : BRONZE, 0, line);
    dl->AddRect(ImVec2(r.Min.x + in, r.Min.y + in), ImVec2(r.Max.x - in, r.Max.y - in),
                lit || pressed ? IM_COL32(255, 236, 170, 200) : IM_COL32(222, 186, 96, 150), 0, std::max(1.f, line * .45f));
}
// a controller's glyph (the settings panel's: settings_icons.h), its middle at c
void glyph(ImDrawList* dl, ImFont* f, ImVec2 c, float size, int g) {
    SettingsIcons::Ink ink;
    ink.plate = IM_COL32(28, 20, 15, 240), ink.rim = BRONZE, ink.rule = GOLD, ink.letter = INK;
    SettingsIcons::draw(dl, f, ImVec2(c.x - SettingsIcons::width(g, size) / 2, c.y - size / 2), size, g, ink);
}
// a back arrow (the touch's Back) and a cross (the list's close plate)
void back_arrow(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float t) {
    dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), col, t);
    dl->PathLineTo(ImVec2(c.x - r * .2f, c.y - r * .75f));
    dl->PathLineTo(ImVec2(c.x - r, c.y));
    dl->PathLineTo(ImVec2(c.x - r * .2f, c.y + r * .75f));
    dl->PathStroke(col, t);
}
void cross(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float t) {
    dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, t);
    dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), col, t);
}
// Each action's icon in gold lines (the settings' and the touch controls' style), in a square of half side s round c
void action_icon(ImDrawList* dl, int a, ImVec2 c, float s, ImU32 col) {
    const float t = std::max(1.5f, s * .13f);
    auto P = [&](float x, float y) { return ImVec2(c.x + x * s, c.y + y * s); };
    auto bust = [&](float x, float y, float k, ImU32 cl) {  // a head and its shoulders
        dl->AddCircle(P(x, y - .3f * k), .26f * k * s, cl, 16, t);
        dl->PathArcTo(P(x, y + .55f * k), .5f * k * s, IM_PI * 1.08f, IM_PI * 1.92f, 14);
        dl->PathStroke(cl, t);
    };
    switch (a) {
    case A_CARDS:  // a guild card: its edge, a hunter's bust and lines of text
        dl->AddRect(P(-.95f, -.68f), P(.95f, .68f), col, .12f * s, t);
        bust(-.42f, .02f, .75f, col);
        for (int i = 0; i < 3; i++) dl->AddLine(P(.1f, -.3f + i * .28f), P(i == 2 ? .48f : .7f, -.3f + i * .28f), col, t);
        break;
    case A_GESTURES:  // a figure waving, with the wave's lines
        dl->AddCircle(P(-.1f, -.62f), .2f * s, col, 14, t);
        dl->AddLine(P(-.1f, -.4f), P(-.1f, .25f), col, t);
        dl->AddLine(P(-.1f, -.22f), P(-.62f, .12f), col, t);
        dl->AddLine(P(-.1f, -.22f), P(.42f, -.68f), col, t);
        dl->AddLine(P(-.1f, .25f), P(-.45f, .9f), col, t);
        dl->AddLine(P(-.1f, .25f), P(.25f, .9f), col, t);
        dl->PathArcTo(P(.42f, -.68f), .32f * s, -IM_PI * .35f, IM_PI * .15f, 8);
        dl->PathStroke(col, t * .8f);
        dl->PathArcTo(P(.42f, -.68f), .52f * s, -IM_PI * .35f, IM_PI * .15f, 10);
        dl->PathStroke(col, t * .8f);
        break;
    case A_FRIENDS:  // two busts, one behind the other
        bust(.32f, -.12f, .82f, (col & 0x00ffffffu) | (0xa0u << IM_COL32_A_SHIFT));
        bust(-.28f, .1f, 1.f, col);
        break;
    default:  // Hunters for Hire: a hunter's bust and the great sword on the back
        dl->AddLine(P(.72f, -.92f), P(-.05f, .72f), col, t * 1.5f);
        dl->AddLine(P(.18f, .14f), P(.58f, .34f), col, t);
        bust(-.32f, .05f, .95f, col);
        break;
    }
}
ImFont* title_font() {
    ImGuiIO& io = ImGui::GetIO();
    return io.Fonts->Fonts.Size > 1 ? io.Fonts->Fonts[1] : ImGui::GetFont();
}

// The list, in (a 1) or going (a toward 0): its places for the fingers are the unscaled ones
void draw_list(ImDrawList* dl, float W, float H, float a, bool pad) {
    ImFont* font = ImGui::GetFont();
    ImFont* title = title_font();
    const float line = std::max(2.f, .0042f * H);
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 0, (int)(110 * a)));
    const int v0 = dl->VtxBuffer.Size;
    const float fs = .05f * H, rh = .105f * H, gap = .018f * H, pw = std::min(W * .8f, .74f * H);
    const float th = .095f * H, hint_h = pad ? .065f * H : 0;
    const float ph = th + gap + A_COUNT * (rh + gap) + hint_h + gap;
    const ImVec2 p0(std::floor((W - pw) / 2), std::floor((H - ph) / 2));
    g_list_rect = ImRect(p0, ImVec2(p0.x + pw, p0.y + ph));
    window_plate(dl, g_list_rect, line);
    diamond(dl, ImVec2(p0.x + .05f * H, p0.y + th * .55f), .012f * H, GOLD);
    text(dl, title, .055f * H, ImVec2(p0.x + .08f * H, p0.y + th * .55f - .033f * H), GLOW, T("Town actions"));
    // the close plate (X) in the corner
    const float xs = .066f * H, xr = p0.x + pw - .03f * H;
    g_x_rect = ImRect(ImVec2(xr - xs, p0.y + th * .55f - xs / 2), ImVec2(xr, p0.y + th * .55f + xs / 2));
    row_plate(dl, g_x_rect, line, false, g_x_held);
    cross(dl, g_x_rect.GetCenter(), xs * .2f, g_x_held ? BRIGHT : INK, std::max(2.f, xs * .085f));
    float y = p0.y + th + gap;
    for (int i = 0; i < A_COUNT; ++i) {
        ImRect r(ImVec2(p0.x + .035f * H, y), ImVec2(p0.x + pw - .035f * H, y + rh));
        g_rows[i] = r;
        const bool pressed = g_mode == LIST && g_finger_on && g_finger_row == i && r.Contains(g_finger_p);
        const bool lit = pad ? i == g_sel : pressed;  // (touch: lit while pressed; a controller: its cursor)
        row_plate(dl, r, line, lit, pressed);
        action_icon(dl, i, ImVec2(r.Min.x + .062f * H, r.GetCenter().y), .03f * H, lit ? GLOW : GOLD);
        text(dl, font, fs, ImVec2(r.Min.x + .115f * H, r.GetCenter().y - fs * .55f), lit ? BRIGHT : INK, T(SPOTS[i].name));
        if (pad && lit) glyph(dl, font, ImVec2(r.Max.x - .05f * H, r.GetCenter().y), .05f * H, SettingsIcons::A);
        y += rh + gap;
    }
    if (pad) {  // (a controller's: A chooses, B closes)
        const float gs = .042f * H, cy = y + hint_h * .4f, ts = .036f * H;
        float x = p0.x + .05f * H;
        glyph(dl, font, ImVec2(x + gs / 2, cy), gs, SettingsIcons::A);
        text(dl, font, ts, ImVec2(x + gs * 1.2f, cy - ts * .58f), DIM, T("Choose"));
        x += gs * 1.2f + font->CalcTextSizeA(ts, FLT_MAX, 0, T("Choose")).x + .045f * H;
        glyph(dl, font, ImVec2(x + gs / 2, cy), gs, SettingsIcons::B);
        text(dl, font, ts, ImVec2(x + gs * 1.2f, cy - ts * .58f), DIM, T("Close"));
    }
    animate(dl, v0, g_list_rect.GetCenter(), .94f + .06f * a, a);
}

// The large touch screen's frame, title, Back and cursor (the screen itself is android_main.cpp's, under this)
void draw_view(ImDrawList* dl, float W, float H, float a, bool pad) {
    ImFont* font = ImGui::GetFont();
    ImFont* title = title_font();
    const float line = std::max(2.f, .0042f * H);
    const ImRect v = view_rect(W, H);
    const bool live = is_view(g_mode);
    const int act = live ? g_action : g_out_action;
    const int v0 = dl->VtxBuffer.Size;
    const ImRect fr(ImVec2(v.Min.x - line * 2, v.Min.y - line * 2), ImVec2(v.Max.x + line * 2, v.Max.y + line * 2));
    dl->AddRect(fr.Min, fr.Max, IM_COL32(0, 0, 0, 255), 0, line * 2.5f);
    dl->AddRect(ImVec2(fr.Min.x - line, fr.Min.y - line), ImVec2(fr.Max.x + line, fr.Max.y + line), BRONZE, 0, line);
    dl->AddRect(ImVec2(fr.Min.x - line * 2, fr.Min.y - line * 2), ImVec2(fr.Max.x + line * 2, fr.Max.y + line * 2), GOLD, 0,
                std::max(1.f, line * .6f));
    for (ImVec2 c : {fr.Min, ImVec2(fr.Max.x, fr.Min.y), ImVec2(fr.Min.x, fr.Max.y), fr.Max}) diamond(dl, c, line * 2.6f, GOLD);
    {  // the title: above the screen's left, in the band under the FPS counter's (which ends by 6.5% of the height)
        const char* t = T(SPOTS[act].name);
        const float fs = .04f * H, th = .062f * H;
        const float tw = title->CalcTextSizeA(fs, FLT_MAX, 0, t).x + .11f * H;
        const ImRect tr(ImVec2(v.Min.x, .075f * H), ImVec2(v.Min.x + tw, .075f * H + th));
        window_plate(dl, tr, std::max(1.5f, line * .7f));
        action_icon(dl, act, ImVec2(tr.Min.x + .04f * H, tr.GetCenter().y), .019f * H, GOLD);
        text(dl, title, fs, ImVec2(tr.Min.x + .075f * H, tr.GetCenter().y - fs * .6f), GLOW, t);
    }
    {  // Back (B to the game's menu; the panel: closed): beside the screen at the bottom right, else under it
        const char* label = T(g_mode == VIEW_PANEL ? "Close" : "Back");
        const float ts = .04f * H, mark = pad ? .05f * H : .045f * H;
        const float bw = std::max(.19f * H, mark + font->CalcTextSizeA(ts, FLT_MAX, 0, label).x + .085f * H);
        const bool beside = W - v.Max.x >= bw + .05f * H;
        const float bh = beside ? .095f * H : std::min(.08f * H, H - v.Max.y - .025f * H);
        const ImVec2 b0 = beside ? ImVec2(W - .025f * H - bw, v.Max.y - bh) : ImVec2(v.Max.x - bw, v.Max.y + .014f * H);
        g_back_rect = ImRect(b0, ImVec2(b0.x + bw, b0.y + bh));
        row_plate(dl, g_back_rect, line, false, g_back_held);
        const ImVec2 mc(b0.x + .028f * H + mark / 2, g_back_rect.GetCenter().y);
        if (pad) glyph(dl, font, mc, mark, SettingsIcons::B);  // (a controller last: its B; a finger: an arrow)
        else back_arrow(dl, mc, mark * .42f, g_back_held ? BRIGHT : INK, std::max(2.f, mark * .1f));
        text(dl, font, ts, ImVec2(mc.x + mark / 2 + .022f * H, g_back_rect.GetCenter().y - ts * .58f), INK, label);
    }
    if (live && g_mode == VIEW_PANEL && pad) {
        // the controller's cursor: a gold arrowhead on the touch screen, a ring under its tip while A holds it down
        const ImVec2 tip(v.Min.x + g_cx / 320.f * v.GetWidth(), v.Min.y + g_cy / 240.f * v.GetHeight());
        const float s = .05f * H;
        const ImVec2 a0(tip.x, tip.y), b(tip.x + s * .32f, tip.y + s * .9f), c(tip.x + s * .48f, tip.y + s * .55f),
            d(tip.x + s * .9f, tip.y + s * .32f);
        if (g_cursor_down) dl->AddCircle(tip, s * .45f, GLOW, 24, line * 1.2f);
        dl->AddQuadFilled(ImVec2(a0.x + line, a0.y + line), ImVec2(b.x + line, b.y + line), ImVec2(c.x + line, c.y + line),
                          ImVec2(d.x + line, d.y + line), IM_COL32(0, 0, 0, 150));
        dl->AddQuadFilled(a0, b, c, d, g_cursor_down ? GLOW : GOLD);
        dl->AddQuad(a0, b, c, d, DEEP, std::max(1.5f, line * .8f));
        // and how it goes, under the screen's left
        const float gs = .036f * H, ts = .031f * H, cy = std::min(H - gs * .7f, v.Max.y + .012f * H + gs * .6f);
        float x = v.Min.x;
        glyph(dl, font, ImVec2(x + gs / 2, cy), gs, SettingsIcons::Dpad);
        text(dl, font, ts, ImVec2(x + gs * 1.15f, cy - ts * .58f), DIM, T("Move"));
        x += gs * 1.15f + font->CalcTextSizeA(ts, FLT_MAX, 0, T("Move")).x + .035f * H;
        glyph(dl, font, ImVec2(x + gs / 2, cy), gs, SettingsIcons::A);
        text(dl, font, ts, ImVec2(x + gs * 1.15f, cy - ts * .58f), DIM, T("Tap"));
    }
    animate(dl, v0, v.GetCenter(), .94f + .06f * a, a);
}

}  // namespace

void town_set_game(int place, int menu, std::uint32_t frame, int panel) {
    g_place = place, g_menu = menu, g_frame = frame, g_panel = place == 1 ? panel : -1;
    if (g_mode == IDLE) return;
    if (place == 0 || place == 2) {  // (out of the town: whatever was under way is over)
        set_mode(IDLE, "left the town");
        return;
    }
    switch (g_mode) {
    case LIST:
        if (menu == 1) set_mode(IDLE, "the game took the buttons");  // (a talk came up)
        break;
    case WAIT:
        if (!at_rest() || (g_panel < 0 && menu == 1)) set_mode(VIEW_MENU, "the menu opened");
        else if (frame - g_wait_from > OPEN_WAIT) set_mode(IDLE, "nothing opened");
        break;
    case VIEW_MENU:
        // (the menu closed: the touch screen at rest some turns running; a change between its pages may show it a moment)
        if (at_rest()) {
            if (++g_menu_zero >= 3) set_mode(IDLE, "the menu closed");
        } else {
            g_menu_zero = 0;
        }
        break;
    case CLOSING:
        if (at_rest()) {
            if (++g_menu_zero >= 3) set_mode(IDLE, "the menu closed");
        } else {
            g_menu_zero = 0;
            if (frame >= g_close_from + CLOSE_EVERY) {  // (one more page back)
                if (++g_close_pulses >= CLOSE_MOST) set_mode(IDLE, "the menu stayed open");
                else g_close_from = frame;
            }
        }
        break;
    case VIEW_PANEL:
        // (the panel gone: the player's Town Map, World Map or Move to Hall)
        if (g_panel >= 0) {
            if (g_panel == PANEL_HIRE) g_panel_seen = true, g_menu_zero = 0;
            else if (g_panel_seen && ++g_menu_zero >= 3) set_mode(IDLE, "the panel went");
            if (g_mode != VIEW_PANEL) break;
        }
        // (the panel's World Map, a talk: the buttons the game's for half a second; the panel's change shows a moment's 1)
        if (menu != 1) g_menu_since = 0;
        else if (g_menu_since == 0) g_menu_since = frame ? frame : 1;
        else if (frame - g_menu_since >= 30) set_mode(IDLE, "the game took the buttons");
        break;
    default:
        break;
    }
}

bool town_available() {
    return g_mode == IDLE && g_place == 1 && g_menu == 0 && at_rest();
}

void town_open(bool by_pad) {
    if (!town_available()) return;
    if (g_sel < 0 || g_sel >= A_COUNT) g_sel = 0;
    set_mode(LIST, by_pad ? "a controller's button" : "the touch button");
}

void town_close() {
    if (g_mode == LIST) set_mode(IDLE, "closed");
}

bool town_back() {
    switch (g_mode) {
    case IDLE:
        return false;
    case LIST:
        set_mode(IDLE, "Back");
        return true;
    case VIEW_MENU:
        g_close_from = g_frame ? g_frame : 1, g_close_pulses = 0;
        set_mode(CLOSING, "closed by the player");
        return true;
    case VIEW_PANEL:
        close_panel();
        return true;
    default:
        return true;  // (under way: the key is the module's a moment)
    }
}

bool town_list_open() {
    return g_mode == LIST;
}

bool town_busy() {
    return g_mode != IDLE;
}

bool town_view(int w, int h, SDL_FRect* rect, float* alpha) {
    const float a = view_alpha();
    if (a < 0) return false;
    const ImRect v = view_rect((float)w, (float)h);
    const float s = .94f + .06f * a, vw = v.GetWidth() * s, vh = v.GetHeight() * s;
    const ImVec2 c = v.GetCenter();
    *rect = SDL_FRect{c.x - vw / 2, c.y - vh / 2, vw, vh};
    *alpha = a;
    return true;
}

bool town_takes_pad() {
    return g_mode == LIST || g_mode == TAP || g_mode == WAIT || g_mode == VIEW_PANEL || g_mode == RESTORE || g_mode == CLOSING;
}

void town_pad(std::uint32_t held, std::uint32_t pressed, float sx, float sy) {
    const std::uint32_t edge = (held & ~g_prev_held) | pressed;
    g_prev_held = held;
    const Uint64 now = SDL_GetTicks();
    const float dt = g_last_ms ? std::min<Uint64>(now - g_last_ms, 50) / 1000.f : 0.f;
    g_last_ms = now;
    if (g_mode == LIST) {
        int dir = (held & mh4n::kPadUp) || sy > .55f ? -1 : (held & mh4n::kPadDown) || sy < -.55f ? 1 : 0;
        if (dir != 0 && (dir != g_repeat_dir || now >= g_repeat_at)) {
            g_repeat_at = now + (dir != g_repeat_dir ? 380 : 130);
            g_sel = (g_sel + dir + A_COUNT) % A_COUNT;
        } else if (dir == 0 && (edge & (mh4n::kPadUp | mh4n::kPadDown))) {  // (a press that came and went: one step)
            g_sel = (g_sel + ((edge & mh4n::kPadUp) ? -1 : 1) + A_COUNT) % A_COUNT;
        }
        g_repeat_dir = dir;
        if (edge & mh4n::kPadA) pick(g_sel);
        else if (edge & (mh4n::kPadB | mh4n::kPadSelect)) set_mode(IDLE, "closed");
        return;
    }
    if (g_mode == VIEW_PANEL) {
        float vx = sx + ((held & mh4n::kPadRight) ? 1.f : 0.f) - ((held & mh4n::kPadLeft) ? 1.f : 0.f);
        float vy = -sy + ((held & mh4n::kPadDown) ? 1.f : 0.f) - ((held & mh4n::kPadUp) ? 1.f : 0.f);
        const float m = std::min(1.f, std::hypot(vx, vy));
        if (m > 0) {
            const float speed = 240.f * std::pow(m, 1.5f) / std::hypot(vx, vy);  // (3DS pixels a second at full tilt)
            g_cx = std::clamp(g_cx + vx * speed * dt, 0.f, 319.f), g_cy = std::clamp(g_cy + vy * speed * dt, 0.f, 239.f);
        }
        if (edge & mh4n::kPadA) g_cursor_down = true, g_cursor_from = g_frame;
        if (g_cursor_down && !(held & mh4n::kPadA) && g_frame >= g_cursor_from + TAP_FRAMES) {  // (a tap's length at least)
            g_cursor_down = false;
            if (g_panel < 0 && panel_leaves(g_cx, g_cy)) set_mode(IDLE, "a panel's button");
        }
        if (edge & mh4n::kPadB) close_panel();
    }
}

int town_touch(int* x, int* y) {
    switch (g_mode) {
    case TAP:
    case RESTORE:
        if (g_tap_from == 0) g_tap_from = g_frame ? g_frame : 1;
        if (g_mode == RESTORE && g_panel >= 0 && g_panel != PANEL_HIRE && g_frame > g_tap_from + 1) {  // (the map is back)
            set_mode(IDLE, "the map is back");
            return 0;
        }
        if (g_frame < g_tap_from + TAP_FRAMES) {
            *x = g_tap_x, *y = g_tap_y;
            return 1;
        }
        if (g_mode == RESTORE) {
            set_mode(IDLE, "the map is back");
        } else if (g_action == A_HIRE) {
            set_mode(VIEW_PANEL, "the panel");
        } else {
            g_wait_from = g_frame;
            set_mode(WAIT, "tapped");
        }
        return 0;
    case VIEW_PANEL:
        if (g_cursor_down) {
            *x = (int)g_cx, *y = (int)g_cy;
            return 1;
        }
        return 0;
    default:
        return -1;
    }
}

std::uint32_t town_buttons() {
    if (g_mode == CLOSING) return !at_rest() && g_frame < g_close_from + BACK_FRAMES ? mh4n::kPadB : 0;
    if (g_back_from == 0) return 0;
    if (g_mode == VIEW_MENU && (g_back_held || g_frame < g_back_from + BACK_FRAMES)) return mh4n::kPadB;
    g_back_from = 0;
    return 0;
}

bool town_event(const SDL_Event& e) {
    if (g_mode == IDLE || g_w <= 0) return false;
    const bool down = e.type == SDL_EVENT_FINGER_DOWN,
               up = e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED;
    if (!down && !up && e.type != SDL_EVENT_FINGER_MOTION) return false;
    const ImVec2 p(e.tfinger.x * g_w, e.tfinger.y * g_h);
    const ImRect v = view_rect(g_w, g_h);
    const bool view = is_view(g_mode), on_view = view && v.Contains(p);
    if (down && !g_finger_on) {
        g_finger_on = true, g_finger = e.tfinger.fingerID, g_finger_p = p;
        g_finger_row = -1, g_finger_view = g_finger_outside = false;
        if (g_mode == LIST) {
            if (g_x_rect.Contains(p)) {
                g_x_held = true;
                touch_haptic_tap();
                return true;
            }
            for (int i = 0; i < A_COUNT; ++i)
                if (g_rows[i].Contains(p)) g_finger_row = i, g_sel = i;
            if (g_finger_row >= 0) touch_haptic_tap();
            else if (!g_list_rect.Contains(p)) g_finger_outside = true;
            return true;
        }
        if (view && g_back_rect.Contains(p)) {
            g_back_held = true;
            touch_haptic_tap();
            if (g_mode == VIEW_MENU) g_back_from = g_frame ? g_frame : 1;
            return true;
        }
        if (on_view) {
            g_finger_view = true;  // (the game's: android_main.cpp gives it to the touch screen)
            return false;
        }
        g_finger_outside = view;  // (beside the large screen: closes it when it lifts there)
        return true;
    }
    if (!g_finger_on || e.tfinger.fingerID != g_finger) return !on_view;
    if (!up) {
        g_finger_p = p;
        return !g_finger_view;
    }
    g_finger_on = false;
    if (g_mode == LIST) {
        if (g_x_held) {
            g_x_held = false;
            if (g_x_rect.Contains(p)) set_mode(IDLE, "its close plate");
        } else if (g_finger_row >= 0 && g_rows[g_finger_row].Contains(p)) {
            pick(g_finger_row);
        } else if (g_finger_outside && !g_list_rect.Contains(p)) {
            set_mode(IDLE, "a tap beside the list");
        }
        return true;
    }
    if (g_back_held) {
        g_back_held = false;
        if (g_mode == VIEW_PANEL && g_back_rect.Contains(p)) close_panel();
        return true;
    }
    if (g_finger_view) {
        g_finger_view = false;
        if (g_mode == VIEW_PANEL && view && g_panel < 0) {  // (the panel's own buttons, tapped by the finger)
            const float u = (p.x - v.Min.x) / v.GetWidth() * 320, w = (p.y - v.Min.y) / v.GetHeight() * 240;
            if (panel_leaves(u, w)) set_mode(IDLE, "a panel's button");
        }
        return false;
    }
    if (g_finger_outside && view && !on_view && !g_back_rect.Contains(p)) town_back();
    return true;
}

void town_frame(int w, int h, bool controller) {
    (void)controller;
    g_w = (float)w, g_h = (float)h;
    if (w <= 0 || h <= 0) return;
    const Uint64 now = SDL_GetTicks();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const bool pad = controls_last_input_pad();
    if (g_mode == LIST) draw_list(dl, (float)w, (float)h, ease((now - g_list_in) / ANIM_MS), pad);
    else if (g_list_out && now - g_list_out < ANIM_MS) draw_list(dl, (float)w, (float)h, 1 - ease((now - g_list_out) / ANIM_MS), pad);
    const float a = view_alpha();
    if (a >= 0) draw_view(dl, (float)w, (float)h, a, pad);
}
