// Monster health bars over the top screen (hunt_bars.h).
#include "hunt_bars.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "imgui.h"
#include "platform/monster_names.h"  // (the names: mh4n reads them from the game's RomFS, 1.0.1.1)
#include "settings_panel.h"
#include "ui.h"

namespace {

// (of the top screen's height) a plate 46% long and 3% tall as they come (11 by 7 of the console's 400 by 240 lines):
// thinner and shorter than the first bars (62% by 3.4%), which covered too much of the picture (the owner, on a phone)
constexpr float kLength = .46f;
constexpr float kShortest = .32f;  // shorter than this beside the hunter's bars, they go under them
constexpr float kHeight = .03f;
constexpr float kMargin = .02f;    // from the top and right edges, and under a HUD part in the corner
constexpr float kGap = .3f;        // between two plates, of a plate's height
constexpr float kAway = .55f;      // a monster in another area of the map
constexpr float kText = .64f;      // the numbers, of a plate's height
constexpr float kNumbers = 4.4f;   // the room they take at the plate's right end, of a plate's height
constexpr float kName = .45f;      // the most of the room left of the numbers a name takes (the groove keeps the rest)
// the app's HUD (touch.cpp, perfstats.cpp): the plate, its rules, the ink of the numbers; the health in the FPS
// counter's green, gold and red (lighter than the rules', to read on the plate)
constexpr ImU32 kWine = IM_COL32(50, 25, 24, 255), kRule = IM_COL32(176, 34, 30, 255), kMoss = IM_COL32(80, 142, 54, 255),
                kDeep = IM_COL32(28, 20, 15, 255), kInk = IM_COL32(244, 232, 200, 255),
                kGood = IM_COL32(132, 204, 96, 255), kGold = IM_COL32(222, 186, 96, 255),
                kBad = IM_COL32(236, 84, 64, 255);
float g_rect[4];              // (hunt_bars_rect: the block as last drawn)
bool g_rect_set = false;      // (once drawn)
std::uint64_t g_rect_ns = 0;  // (when: SDL ticks)

ImU32 alpha_of(ImU32 c, float a) {
    return (c & 0x00ffffffu) | ((ImU32)std::clamp(((c >> 24) & 0xff) * a, 0.f, 255.f) << 24);
}

}  // namespace

HuntBarsRoom hunt_bars_room(const SDL_FRect& top, const Port::Hooks::GameState& game, float k) {
    HuntBarsRoom r{top.x, top.y, 0};
    if (top.w <= 0 || top.h <= 0) return r;
    if (const Port::Hooks::GameState::HudPart& p = game.hud[1]; p.shown && p.rect[1] < .34f) {
        r.left = top.x + p.rect[2] * top.w, r.under = top.y + p.rect[3] * top.h;
    }
    const float corner_x = 1 - (kLength * std::clamp(k, .5f, 2.f) + kMargin) * top.h / top.w;
    for (const int part : {4, 5}) {
        const Port::Hooks::GameState::HudPart& p = game.hud[part];
        if (p.shown && p.rect[2] > corner_x && p.rect[1] < .34f)
            r.below = std::max(r.below, top.y + p.rect[3] * top.h + kMargin * top.h);
    }
    return r;
}

std::vector<HuntBarDrawn> hunt_bars_layout(const SDL_FRect& top, const std::vector<Port::HuntAids::Health>& healths,
                                           float left, float under, float below, float k, float px, float py,
                                           const std::vector<std::string>* names, ImFont* font) {
    std::vector<HuntBarDrawn> out;
    if (top.w <= 0 || top.h <= 0) return out;
    k = std::clamp(k, .5f, 2.f);
    const float h = std::max(5.f, std::floor(kHeight * k * top.h + .5f)), m = kMargin * top.h;
    const float gap = std::max(2.f, std::floor(kGap * h + .5f)), length = kLength * k * top.h;
    const float n = (float)healths.size(), block = n * h + std::max(0.f, n - 1) * gap;
    float right = top.x + top.w - m, w, y;
    if (px < 0 || py < 0) {  // (their own place: the top right corner, clear of the HUD there)
        w = std::min(length, right - std::max(left + m, top.x + m));
        y = std::max(top.y + m, below);
        if (w < kShortest * k * top.h) {  // (no room beside the hunter's bars: under them)
            w = std::min(length, top.w - 2 * m);
            y = std::max(y, under + m);
        }
    } else {  // (where the player put them: whole, on the picture)
        w = std::min(length, top.w);
        right = std::clamp(top.x + px * top.w, top.x + w, top.x + top.w);
        y = std::clamp(top.y + py * top.h, top.y, std::max(top.y, top.y + top.h - block));
    }
    const float x = right - w;
    const float line = std::max(1.f, std::floor(h * .09f + .5f)), pad = std::max(2.f, std::floor(h * .25f));
    const float text = std::max(6.f, std::floor(kText * h)), groove = std::max(2.f, std::floor(h * .36f));
    const float numbers = std::min(kNumbers * h, w * .55f);
    for (std::size_t i = 0; i < healths.size(); ++i) {
        const Port::HuntAids::Health& b = healths[i];
        HuntBarDrawn d;
        d.frame = SDL_FRect{x, std::floor(y), w, h};
        // the name at the left, shortened to its room; the groove after it (the whole room without one)
        const float room = std::max(4.f, w - numbers - 2 * pad);
        float nw = 0;
        if (names && font && i < names->size()) d.name = hunt_bars_fit_name(font, text, (*names)[i], kName * room, &nw);
        if (d.name.empty()) nw = 0;
        d.name_x = x + pad;
        const float before = nw > 0 ? nw + pad : 0.f;
        d.track = SDL_FRect{x + pad + before, d.frame.y + std::floor((h - groove) * .5f), std::max(4.f, room - before),
                            groove};
        d.fill = b.max > 0 ? std::clamp((float)b.health / (float)b.max, 0.f, 1.f) : 0;
        d.colour = d.fill > .5f ? kGood : d.fill > .25f ? kGold : kBad;
        d.alpha = b.here ? 1.f : kAway;
        d.line = line, d.text_size = text, d.text_right = x + w - pad, d.text_y = d.frame.y + h * .5f;
        char t[32];
        std::snprintf(t, sizeof t, "%d / %d", b.health, b.max);
        d.text = t;
        out.push_back(d);
        y += h + gap;
    }
    return out;
}

std::string hunt_bars_fit_name(ImFont* font, float size, const std::string& name, float most, float* w) {
    *w = 0;
    if (!font || name.empty() || most <= 0) return {};
    for (std::size_t i = 0; i < name.size();) {  // (a character the font lacks: no name rather than a box)
        const unsigned char b = name[i];
        const int n = b < 0x80 ? 1 : b >= 0xf0 ? 4 : b >= 0xe0 ? 3 : b >= 0xc0 ? 2 : 0;
        if (n == 0 || i + n > name.size()) return {};
        unsigned int c = n == 1 ? b : b & (0x7f >> n);
        for (int k = 1; k < n; ++k) c = c << 6 | (name[i + k] & 0x3f);
        if (c > 0xffff || !font->IsGlyphInFont((ImWchar)c)) return {};
        i += n;
    }
    float width = font->CalcTextSizeA(size, 1e9f, 0, name.c_str()).x;
    if (width <= most) return *w = width, name;
    const char* dots = font->IsGlyphInFont(0x2026) ? "\xE2\x80\xA6" : "...";
    for (std::size_t n = name.size(); n > 0;) {
        do --n; while (n > 0 && (name[n] & 0xC0) == 0x80);  // (back a whole character)
        std::size_t keep = n;
        while (keep > 0 && name[keep - 1] == ' ') --keep;  // (no space before the ellipsis)
        if (!keep) break;
        const std::string t = name.substr(0, keep) + dots;
        width = font->CalcTextSizeA(size, 1e9f, 0, t.c_str()).x;
        if (width <= most) return *w = width, t;
    }
    return {};
}

void hunt_bars_drag(const SDL_FRect& top, const float block[4], float dx, float dy, float* px, float* py) {
    if (top.w <= 0 || top.h <= 0) return;
    const float right = std::clamp(block[2] + dx, top.x + (block[2] - block[0]), top.x + top.w);
    const float y = std::clamp(block[1] + dy, top.y, std::max(top.y, top.y + top.h - (block[3] - block[1])));
    *px = (right - top.x) / top.w, *py = (y - top.y) / top.h;
}

float hunt_bars_size(float k) {
    return std::clamp(std::round(k * 20.f) / 20.f, .5f, 2.f);
}

float hunt_bars_grip(float k0, const float block[4], float x0, float y0, float x, float y) {
    const float d0 = std::hypot(x0 - block[2], y0 - block[1]), d = std::hypot(x - block[2], y - block[1]);
    return hunt_bars_size(d0 >= 1.f ? k0 * d / d0 : k0);
}

bool hunt_bars_rect(float r[4]) {
    if (!g_rect_set || SDL_GetTicksNS() - g_rect_ns > 500000000ull) return false;
    for (int i = 0; i < 4; ++i) r[i] = g_rect[i];
    return true;
}

void hunt_bars_draw(const SDL_FRect& top) {
    if (!Port::HuntAids::HealthBarOn()) return;
    const int language = Port::HuntAids::GameLanguage();  // (its names: read by the runtime as soon as it is known)
    std::vector<Port::HuntAids::Health> healths = Port::HuntAids::Healths();
    const bool editing = touch_editing();
    std::vector<std::string> names;
    if (healths.empty() && editing) {  // (a stand-in, named in the menus' language)
        healths.push_back(Port::HuntAids::Health{1900, 2546, true, 0, 0});
        names.push_back(settings_spanish() ? "Monstruo" : "Monster");
    } else {
        for (const Port::HuntAids::Health& h : healths) names.push_back(mh4n::platform::monster_names::Name(language, h.kind));
    }
    if (healths.empty()) return;
    const Port::Hooks::GameState game = Port::Hooks::CurrentGameState();
    if (game.menu == 1 && !editing) return;  // (the START menu, a talk, the map: the game's own window comes first)
    float px = -1, py = -1, k = 1;
    if (!touch_bars_place(&px, &py, &k)) px = py = -1;
    const HuntBarsRoom room = hunt_bars_room(top, game, k);
    ImFont* font = ImGui::GetFont();
    const std::vector<HuntBarDrawn> bars =
        hunt_bars_layout(top, healths, room.left, room.under, room.below, k, px, py, &names, font);
    if (bars.empty()) return;
    g_rect[0] = bars.front().frame.x, g_rect[1] = bars.front().frame.y;  // (the block, for the layout editor)
    g_rect[2] = bars.back().frame.x + bars.back().frame.w, g_rect[3] = bars.back().frame.y + bars.back().frame.h;
    g_rect_ns = SDL_GetTicksNS(), g_rect_set = true;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();  // (under the touch controls and the menus, as the damage numbers)
    dl->PushClipRect(ImVec2(top.x, top.y), ImVec2(top.x + top.w, top.y + top.h), true);
    for (const HuntBarDrawn& b : bars) {
        const float a = b.alpha, l = b.line;
        const ImVec2 p0(b.frame.x, b.frame.y), p1(b.frame.x + b.frame.w, b.frame.y + b.frame.h);
        // the plate (the FPS counter's, perfstats.cpp: wine, a black edge, red and moss rules at the top and bottom)
        dl->AddRectFilled(p0, p1, alpha_of(kWine, a * .9f));
        dl->AddRect(p0, p1, alpha_of(IM_COL32(0, 0, 0, 255), a), 0.f, l);
        dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + l), alpha_of(kRule, a));
        dl->AddRectFilled(ImVec2(p0.x, p0.y + l), ImVec2(p1.x, p0.y + l * 1.7f), alpha_of(kMoss, a));
        dl->AddRectFilled(ImVec2(p0.x, p1.y - l * 1.7f), ImVec2(p1.x, p1.y - l), alpha_of(kMoss, a));
        dl->AddRectFilled(ImVec2(p0.x, p1.y - l), p1, alpha_of(kRule, a));
        // the name: the numbers' ink, size and shadow
        if (!b.name.empty()) {
            const ImVec2 ns = font->CalcTextSizeA(b.text_size, 1e9f, 0, b.name.c_str());
            const ImVec2 at(b.name_x, b.text_y - ns.y * .5f);
            dl->AddText(font, b.text_size, ImVec2(at.x + b.text_size * .05f, at.y + b.text_size * .06f),
                        alpha_of(IM_COL32(0, 0, 0, 200), a), b.name.c_str());
            dl->AddText(font, b.text_size, at, alpha_of(kInk, a), b.name.c_str());
        }
        // the groove and the health in it
        const ImVec2 t0(b.track.x, b.track.y), t1(b.track.x + b.track.w, b.track.y + b.track.h);
        dl->AddRectFilled(ImVec2(t0.x - l, t0.y - l), ImVec2(t1.x + l, t1.y + l), alpha_of(IM_COL32(0, 0, 0, 255), a * .8f));
        dl->AddRectFilled(t0, t1, alpha_of(kDeep, a));
        if (b.fill > 0) {
            const float fx = t0.x + b.track.w * b.fill;
            dl->AddRectFilled(t0, ImVec2(fx, t1.y), alpha_of(b.colour, a));
            dl->AddRectFilled(t0, ImVec2(fx, t0.y + std::max(1.f, b.track.h * .34f)),
                              alpha_of(IM_COL32(255, 255, 255, 70), a));
        }
        // the numbers: ink on the plate, a shadow as the app's other texts
        const ImVec2 size = font->CalcTextSizeA(b.text_size, 1e9f, 0, b.text.c_str());
        const ImVec2 at(b.text_right - size.x, b.text_y - size.y * .5f);
        dl->AddText(font, b.text_size, ImVec2(at.x + b.text_size * .05f, at.y + b.text_size * .06f),
                    alpha_of(IM_COL32(0, 0, 0, 200), a), b.text.c_str());
        dl->AddText(font, b.text_size, at, alpha_of(kInk, a), b.text.c_str());
    }
    dl->PopClipRect();
}
