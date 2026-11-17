// Damage numbers over the top screen (hunt_numbers.h).
#include "hunt_numbers.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include <string>

#include "app_settings.h"
#include "hunt_aids.h"
#include "imgui.h"

namespace {

// of the top screen's height, in whole pixels: one size for every number (Dear ImGui bakes the font at each size it is
// asked for, so a size that changed every frame would bake new glyphs every frame). The player picks one of three
// ([Graphics] DamageNumberSize: small, medium, large; medium the size they came with) and a colour
// ([Graphics] DamageNumberColour: white, yellow, red, blue, green), read again every half second, so a change in the
// menu shows at once without a file read each frame.
constexpr float kTextHeight = .075f;
constexpr float kSizes[] = {.04f, kTextHeight, .1f};  // (Small was 5.5 %: "still a bit big", the owner on a Galaxy)
constexpr ImU32 kColours[] = {IM_COL32(255, 255, 255, 0), IM_COL32(255, 214, 40, 0), IM_COL32(255, 80, 64, 0),
                              IM_COL32(110, 200, 255, 0), IM_COL32(110, 230, 110, 0)};
struct Style {
    float height = kTextHeight;
    ImU32 colour = kColours[0];
};

int key_index(const std::string& v, std::initializer_list<const char*> keys, int fallback) {
    int i = 0;
    for (const char* k : keys) {
        if (v == k) return i;
        ++i;
    }
    return fallback;
}

const Style& style(std::int64_t now_ns) {
    static Style s;
    static std::int64_t next_ns = 0;
    if (now_ns >= next_ns) {
        next_ns = now_ns + 500000000;
        s.height = kSizes[key_index(app_setting("Graphics", "DamageNumberSize", "medium"),
                                    {"small", "medium", "large"}, 1)];
        s.colour = kColours[key_index(app_setting("Graphics", "DamageNumberColour", "white"),
                                      {"white", "yellow", "red", "blue", "green"}, 0)];
    }
    return s;
}
constexpr float kRise = .08f;          // how far it rises while it lasts, of the screen's height
constexpr float kFade = .6f;           // it fades from 60% of its time on
constexpr float kJitter = .006f;       // numbers side by side: up to 4 steps of this (of the width) apart, by their id

std::int64_t steady_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

std::vector<HuntNumberDrawn> hunt_numbers_layout(const SDL_FRect& top, std::int64_t now_ns, float text_height) {
    std::vector<HuntNumberDrawn> out;
    if (top.w <= 0 || top.h <= 0) return out;
    for (const Port::HuntAids::Number& n : Port::HuntAids::Numbers()) {
        if (!n.shown) continue;
        float t = (float)((double)(now_ns - n.born_ns) / 1e9 / Port::HuntAids::kNumberSeconds);
        if (t > 1) continue;
        t = std::max(t, 0.f);
        const float rise = kRise * top.h * (1 - (1 - t) * (1 - t));  // (eased: fast at first)
        const float jitter = (float)((int)(n.id * 37u % 9u) - 4) * kJitter * top.w;
        HuntNumberDrawn d;
        d.x = top.x + n.x * top.w + jitter;
        d.y = top.y + n.y * top.h - rise;
        d.size = std::max(8.f, std::floor(text_height * top.h + .5f));
        d.alpha = t < kFade ? 1 : 1 - (t - kFade) / (1 - kFade);
        d.value = n.value;
        out.push_back(d);
    }
    return out;
}

void hunt_numbers_draw(const SDL_FRect& top) {
    if (!Port::HuntAids::DamageNumbersOn()) return;
    const std::int64_t now = steady_ns();
    const Style& look = style(now);
    const std::vector<HuntNumberDrawn> numbers = hunt_numbers_layout(top, now, look.height);
    if (numbers.empty()) return;
    ImFont* font = ImGui::GetFont();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();  // (under the touch controls and the menus, as the FPS counter)
    dl->PushClipRect(ImVec2(top.x, top.y), ImVec2(top.x + top.w, top.y + top.h), true);
    for (const HuntNumberDrawn& d : numbers) {
        char text[16];
        std::snprintf(text, sizeof text, "%d", d.value);
        const ImVec2 size = font->CalcTextSizeA(d.size, 1e9f, 0, text);
        const ImVec2 at(d.x - size.x * .5f, d.y - size.y * .5f);
        const int a = (int)(255 * std::clamp(d.alpha, 0.f, 1.f));
        const float o = std::max(1.f, d.size * .07f);  // the outline, as the game's own white numbers on dark
        for (int k = 0; k < 8; ++k) {
            static const float DX[8] = {-1, 0, 1, -1, 1, -1, 0, 1}, DY[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
            dl->AddText(font, d.size, ImVec2(at.x + DX[k] * o, at.y + DY[k] * o), IM_COL32(0, 0, 0, a * 3 / 4), text);
        }
        dl->AddText(font, d.size, at, look.colour | IM_COL32(0, 0, 0, a), text);
    }
    dl->PopClipRect();
}
