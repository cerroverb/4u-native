// Quick items on the main screen (quick_items.h).
#include "quick_items.h"

#include <algorithm>
#include <cmath>

#include "hooks.h"
#include "imgui.h"
#include "shared_screens.h"
#include "ui.h"

namespace {

constexpr float kHeight = .10f;  // the piece's height at size 1, of the window's (68 of the touch screen's 240 rows)
constexpr float kMargin = .02f;  // its own place: this far above the bottom edge
// the app's HUD (hunt_bars.cpp, the FPS counter's plate): the stand-in's plate and cells
constexpr ImU32 kWine = IM_COL32(50, 25, 24, 235), kRule = IM_COL32(176, 34, 30, 255), kMoss = IM_COL32(80, 142, 54, 255),
                kDeep = IM_COL32(28, 20, 15, 235), kGold = IM_COL32(222, 186, 96, 255), kInk = IM_COL32(244, 232, 200, 255);

struct State {
    bool shown = false, stand_in = false, wide = false;
    SDL_FRect rect{};
    float crop[4] = {};
} g;
bool g_wide_seen = false;  // (the stand-in's shape: the form a quest last had, else one slot as the default layout's)

}  // namespace

void quick_items_crop(int slot, bool wide, float crop[4]) {
    const int column = std::clamp(slot, 0, 5) / 3, row = std::clamp(slot, 0, 5) % 3;
    crop[0] = column * kQuickItemsSlotW / 320.f;
    crop[1] = (kQuickItemsTop + row * kQuickItemsSlotH) / 240.f;
    crop[2] = std::min(1.f, crop[0] + (wide ? 2 : 1) * kQuickItemsSlotW / 320.f);
    crop[3] = crop[1] + kQuickItemsSlotH / 240.f;
}

SDL_FRect quick_items_layout(float w, float h, bool wide, float x, float y, float k) {
    k = std::clamp(k, .5f, 2.f);
    float ph = std::floor(kHeight * k * h + .5f), pw = std::floor(ph * (wide ? 2 : 1) * kQuickItemsSlotW / kQuickItemsSlotH + .5f);
    if (pw > w) ph = std::floor(ph * w / pw), pw = w;  // (a narrow window: the whole piece in it)
    float cx = x < 0 || y < 0 ? w * .5f : x * w, cy = x < 0 || y < 0 ? h - kMargin * h - ph * .5f : y * h;
    cx = std::clamp(cx, pw * .5f, std::max(pw * .5f, w - pw * .5f));
    cy = std::clamp(cy, ph * .5f, std::max(ph * .5f, h - ph * .5f));
    return SDL_FRect{std::floor(cx - pw * .5f), std::floor(cy - ph * .5f), pw, ph};
}

void quick_items_touch_point(const SDL_FRect& rect, const float crop[4], float x, float y, int* tx, int* ty) {
    const float u = rect.w > 0 ? std::clamp((x - rect.x) / rect.w, 0.f, .999f) : 0;
    const float v = rect.h > 0 ? std::clamp((y - rect.y) / rect.h, 0.f, .999f) : 0;
    *tx = std::clamp(static_cast<int>((crop[0] + u * (crop[2] - crop[0])) * 320), 0, 319);
    *ty = std::clamp(static_cast<int>((crop[1] + v * (crop[3] - crop[1])) * 240), 0, 239);
}

void quick_items_drag(float w, float h, const float block[4], float dx, float dy, float* px, float* py) {
    if (w <= 0 || h <= 0) return;
    const float hw = (block[2] - block[0]) * .5f, hh = (block[3] - block[1]) * .5f;
    const float cx = std::clamp((block[0] + block[2]) * .5f + dx, hw, std::max(hw, w - hw));
    const float cy = std::clamp((block[1] + block[3]) * .5f + dy, hh, std::max(hh, h - hh));
    *px = cx / w, *py = cy / h;
}

float quick_items_size(float k) {
    return std::clamp(std::round(k * 20.f) / 20.f, .5f, 2.f);
}

float quick_items_grip(float k0, const float block[4], float x0, float y0, float x, float y) {
    const float cx = (block[0] + block[2]) * .5f, cy = (block[1] + block[3]) * .5f;
    const float d0 = std::hypot(x0 - cx, y0 - cy), d = std::hypot(x - cx, y - cy);
    return quick_items_size(d0 >= 1.f ? k0 * d / d0 : k0);
}

bool quick_items_update(int w, int h, bool bottom_shown) {
    g.shown = false;
    if (!Port::Hooks::QuickItemsOn() || w <= 0 || h <= 0 || ui_settings_open()) return false;
    const Port::Hooks::GameState game = Port::Hooks::CurrentGameState();
    const bool editing = touch_editing();
    if (game.items_slot >= 0) g_wide_seen = game.items_wide != 0;
    if (!editing && (game.items_slot < 0 || bottom_shown)) return false;
    g.stand_in = game.items_slot < 0;  // (the editor out of quests)
    g.wide = g.stand_in ? g_wide_seen : game.items_wide != 0;
    if (!g.stand_in) quick_items_crop(game.items_slot, g.wide, g.crop);
    float x = -1, y = -1, k = 1;
    if (!touch_items_place(&x, &y, &k)) x = y = -1;
    g.rect = quick_items_layout(static_cast<float>(w), static_cast<float>(h), g.wide, x, y, k);
    g.shown = true;
    return true;
}

bool quick_items_needs_touch_screen() {
    return g.shown && !g.stand_in;
}

void quick_items_draw(SharedScreenView& bottom, int win_w, int win_h, Uint8 alpha) {
    if (!g.shown || g.stand_in) return;
    // the touch screen's own picture of the panel
    const SDL_FRect part{g.crop[0], g.crop[1], g.crop[2] - g.crop[0], g.crop[3] - g.crop[1]};
    shared_screens_draw_part(bottom, part, g.rect, win_w, win_h, alpha);
}

void quick_items_draw_stand_in() {
    if (!g.shown || !g.stand_in) return;
    // the stand-in (the layout editor out of quests): a plate in the app's look with the panel's cells
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 p0(g.rect.x, g.rect.y), p1(g.rect.x + g.rect.w, g.rect.y + g.rect.h);
    const float l = std::max(1.f, std::floor(g.rect.h * .03f + .5f));
    dl->AddRectFilled(p0, p1, kWine);
    dl->AddRect(p0, p1, IM_COL32(0, 0, 0, 255), 0.f, l);
    dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + l), kRule);
    dl->AddRectFilled(ImVec2(p0.x, p0.y + l), ImVec2(p1.x, p0.y + l * 1.7f), kMoss);
    dl->AddRectFilled(ImVec2(p0.x, p1.y - l * 1.7f), ImVec2(p1.x, p1.y - l), kMoss);
    dl->AddRectFilled(ImVec2(p0.x, p1.y - l), p1, kRule);
    const int cells = g.wide ? 4 : 2;
    const float pad = g.rect.h * .12f, cw = (g.rect.w - pad * (cells + 1)) / cells, ch = g.rect.h - 2 * pad;
    ImFont* font = ImGui::GetFont();
    for (int i = 0; i < cells; ++i) {
        const ImVec2 c0(p0.x + pad + i * (cw + pad), p0.y + pad), c1(c0.x + cw, c0.y + ch);
        dl->AddRectFilled(c0, c1, kDeep, ch * .12f);
        dl->AddRect(c0, c1, kGold, ch * .12f, std::max(1.f, l * .8f));
        const float fs = std::floor(ch * .45f);
        const ImVec2 ts = font->CalcTextSizeA(fs, 1e9f, 0, "?");
        dl->AddText(font, fs, ImVec2((c0.x + c1.x - ts.x) * .5f, (c0.y + c1.y - ts.y) * .5f), kInk, "?");
    }
}

bool quick_items_hit(float x, float y) {
    return g.shown && !g.stand_in && x >= g.rect.x && x < g.rect.x + g.rect.w && y >= g.rect.y && y < g.rect.y + g.rect.h;
}

bool quick_items_touch(float x, float y, int* tx, int* ty) {
    if (!g.shown || g.stand_in) return false;
    quick_items_touch_point(g.rect, g.crop, x, y, tx, ty);
    return true;
}

bool quick_items_rect(float r[4]) {
    if (!g.shown) return false;
    r[0] = g.rect.x, r[1] = g.rect.y, r[2] = g.rect.x + g.rect.w, r[3] = g.rect.y + g.rect.h;
    return true;
}
