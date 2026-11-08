// The controller's glyphs in the settings panel's guide strip (settings_panel.cpp), drawn as vectors in the touch
// controls' style (touch.cpp): a dark disc with a bronze rim and the 3DS's letter in cream, the shoulders as small
// tabs, the D-pad as a cross. No fonts but the panel's, no textures.
#pragma once
#include "imgui.h"

namespace SettingsIcons {

enum { A, B, X, Y, L, R, Dpad };

struct Ink {
    ImU32 plate = IM_COL32(28, 20, 15, 240), rim = IM_COL32(112, 76, 34, 255), rule = IM_COL32(222, 186, 96, 255),
          letter = IM_COL32(244, 232, 200, 255);
};

// `size`: the glyph's height; returns its width
inline float width(int glyph, float size) {
    return glyph == L || glyph == R ? size * 1.5f : size;
}

inline void draw(ImDrawList* d, ImFont* font, ImVec2 p, float size, int glyph, const Ink& k = Ink()) {
    const float line = size > 24 ? 2.0f : 1.5f;
    const ImVec2 c(p.x + width(glyph, size) * .5f, p.y + size * .5f);
    if (glyph == Dpad) {
        const float a = size * .44f, b = size * .15f;
        d->AddRectFilled(ImVec2(c.x - a, c.y - b), ImVec2(c.x + a, c.y + b), k.rim, b * .5f);
        d->AddRectFilled(ImVec2(c.x - b, c.y - a), ImVec2(c.x + b, c.y + a), k.rim, b * .5f);
        const float i = line;
        d->AddRectFilled(ImVec2(c.x - a + i, c.y - b + i), ImVec2(c.x + a - i, c.y + b - i), k.letter, b * .4f);
        d->AddRectFilled(ImVec2(c.x - b + i, c.y - a + i), ImVec2(c.x + b - i, c.y + a - i), k.letter, b * .4f);
        d->AddCircleFilled(c, b * .45f, k.rim);
        return;
    }
    const char* name = glyph == A ? "A" : glyph == B ? "B" : glyph == X ? "X" : glyph == Y ? "Y" : glyph == L ? "L" : "R";
    const float text = size * .62f;
    const ImVec2 ts = font->CalcTextSizeA(text, FLT_MAX, 0, name);
    if (glyph == L || glyph == R) {
        const ImVec2 a(p.x, p.y + size * .14f), b(p.x + size * 1.5f, p.y + size * .86f);
        d->AddRectFilled(a, b, k.plate, size * .3f);
        d->AddRect(a, b, k.rim, size * .3f, line);
        d->AddLine(ImVec2(a.x + size * .3f, a.y + line * 1.5f), ImVec2(b.x - size * .3f, a.y + line * 1.5f), k.rule,
                   line * .6f);
    } else {
        d->AddCircleFilled(c, size * .48f, k.plate);
        d->AddCircle(c, size * .48f, k.rim, 0, line);
        d->AddCircle(c, size * .48f - line * 1.6f, (k.rule & 0x00ffffffu) | 0x80000000u, 0, line * .6f);
    }
    d->AddText(font, text, ImVec2(c.x - ts.x * .5f + 1, c.y - ts.y * .5f + 1), IM_COL32(0, 0, 0, 170), name);
    d->AddText(font, text, ImVec2(c.x - ts.x * .5f, c.y - ts.y * .5f), k.letter, name);
}

}  // namespace SettingsIcons
