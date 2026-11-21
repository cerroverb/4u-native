// The weapon tree's panel (weapon_tree_panel.h). The same file in both ports.
#include "weapon_tree_panel.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace weapon_tree_panel {

namespace {

// the apps' colours (MH4U's town.cpp and touch.cpp, MHGU's touch.cpp): wine-dark plates, red and moss rules, bronze, gold
const ImU32 INK = IM_COL32(244, 232, 200, 255), GOLD = IM_COL32(222, 186, 96, 255), BRONZE = IM_COL32(112, 76, 34, 255),
            GLOW = IM_COL32(255, 214, 92, 255), RED = IM_COL32(176, 34, 30, 255), MOSS = IM_COL32(80, 142, 54, 255),
            DIM = IM_COL32(204, 186, 160, 255), FAINT = IM_COL32(150, 132, 108, 255), BRIGHT = IM_COL32(255, 248, 225, 255),
            SHADOW = IM_COL32(96, 84, 72, 255);  // (a "?": its icon's silhouette)

ImU32 Alpha(ImU32 c, float a) {
    const float al = ((c >> IM_COL32_A_SHIFT) & 0xff) * a;
    return (c & ~IM_COL32_A_MASK) | ((ImU32)std::clamp(al, 0.f, 255.f) << IM_COL32_A_SHIFT);
}

// the rarity's colour, as the games tint a weapon's icon by its rarity (1 white, 2 violet, 3 yellow, 4 pink, 5 green,
// 6 blue, 7 red, 8 sky blue, 9 orange, 10 lavender, 11 and on gold)
ImU32 RarityColour(int r) {
    static const ImU32 k[] = {IM_COL32(236, 236, 236, 255), IM_COL32(178, 128, 236, 255), IM_COL32(236, 214, 84, 255),
                              IM_COL32(244, 150, 196, 255), IM_COL32(110, 204, 92, 255), IM_COL32(80, 128, 236, 255),
                              IM_COL32(232, 70, 60, 255),   IM_COL32(110, 214, 236, 255), IM_COL32(244, 150, 52, 255),
                              IM_COL32(200, 160, 244, 255)};
    if (r >= 1 && r <= 10) return k[r - 1];
    return r > 10 ? GLOW : k[0];
}

ImU32 ElementColour(int e) {
    switch (e) {
    case weapon_tree::kFire: return IM_COL32(236, 92, 44, 255);
    case weapon_tree::kWater: return IM_COL32(64, 140, 236, 255);
    case weapon_tree::kThunder: return IM_COL32(240, 212, 60, 255);
    case weapon_tree::kIce: return IM_COL32(160, 224, 244, 255);
    case weapon_tree::kDragon: return IM_COL32(150, 40, 70, 255);
    case weapon_tree::kPoison: return IM_COL32(156, 76, 196, 255);
    case weapon_tree::kParalysis: return IM_COL32(214, 196, 50, 255);
    case weapon_tree::kSleep: return IM_COL32(150, 170, 214, 255);
    case weapon_tree::kBlast: return IM_COL32(244, 140, 40, 255);
    default: return DIM;
    }
}

const char* ElementWord(int e) {
    switch (e) {
    case weapon_tree::kFire: return "Fire";
    case weapon_tree::kWater: return "Water";
    case weapon_tree::kThunder: return "Thunder";
    case weapon_tree::kIce: return "Ice";
    case weapon_tree::kDragon: return "Dragon";
    case weapon_tree::kPoison: return "Poison";
    case weapon_tree::kParalysis: return "Paralysis";
    case weapon_tree::kSleep: return "Sleep";
    case weapon_tree::kBlast: return "Blast";
    default: return "";
    }
}

// the sharpness bar's colours, red to purple
const ImU32 SHARP[7] = {IM_COL32(222, 38, 30, 255),  IM_COL32(242, 128, 26, 255), IM_COL32(240, 222, 40, 255),
                        IM_COL32(92, 200, 58, 255),  IM_COL32(56, 108, 232, 255), IM_COL32(244, 244, 244, 255),
                        IM_COL32(176, 82, 226, 255)};

void Diamond(ImDrawList* dl, ImVec2 c, float d, ImU32 col) {
    ImVec2 q[4] = {ImVec2(c.x, c.y - d), ImVec2(c.x + d, c.y), ImVec2(c.x, c.y + d), ImVec2(c.x - d, c.y)};
    dl->AddConvexPolyFilled(q, 4, col);
}

void Text(ImDrawList* dl, ImFont* f, float size, ImVec2 p, ImU32 col, const char* t, const char* end = nullptr) {
    const float o = std::max(1.f, size * .06f);
    dl->AddText(f, size, ImVec2(p.x + o, p.y + o), IM_COL32(0, 0, 0, (int)(190 * ((col >> 24) & 0xff) / 255)), t, end);
    dl->AddText(f, size, p, col, t, end);
}
float Width(ImFont* f, float size, const char* t) { return f->CalcTextSizeA(size, FLT_MAX, 0, t).x; }

// the game's window: wine-dark, a black edge, a red rule with a green one inside it top and bottom, gold studs
void WindowPlate(ImDrawList* dl, const ImRect& r, float line) {
    dl->AddRectFilled(ImVec2(r.Min.x + line, r.Min.y + line * 1.5f), ImVec2(r.Max.x + line, r.Max.y + line * 1.5f),
                      IM_COL32(0, 0, 0, 120));
    dl->AddRectFilled(r.Min, r.Max, IM_COL32(50, 25, 24, 245));
    dl->AddRect(r.Min, r.Max, IM_COL32(0, 0, 0, 255), 0, line);
    dl->AddRectFilled(r.Min, ImVec2(r.Max.x, r.Min.y + line), RED);
    dl->AddRectFilled(ImVec2(r.Min.x, r.Min.y + line), ImVec2(r.Max.x, r.Min.y + line * 1.7f), MOSS);
    dl->AddRectFilled(ImVec2(r.Min.x, r.Max.y - line * 1.7f), ImVec2(r.Max.x, r.Max.y - line), MOSS);
    dl->AddRectFilled(ImVec2(r.Min.x, r.Max.y - line), r.Max, RED);
    for (ImVec2 c : {r.Min, ImVec2(r.Max.x, r.Min.y), ImVec2(r.Min.x, r.Max.y), r.Max}) Diamond(dl, c, line * 1.9f, GOLD);
}

// a row or a button: a dark plate, a bronze edge and a gold rule inside; lit ochre with a glowing edge
void Plate(ImDrawList* dl, const ImRect& r, float line, bool lit, bool pressed = false, float fade = 1) {
    const float in = line * 1.6f;
    if (lit || pressed) {
        dl->AddRectFilledMultiColor(r.Min, r.Max, Alpha(IM_COL32(150, 104, 30, 250), fade), Alpha(IM_COL32(206, 158, 42, 250), fade),
                                    Alpha(IM_COL32(122, 82, 22, 250), fade), Alpha(IM_COL32(92, 62, 22, 250), fade));
        if (pressed) dl->AddRectFilled(r.Min, r.Max, IM_COL32(255, 236, 170, 46));
    } else {
        dl->AddRectFilled(r.Min, r.Max, Alpha(IM_COL32(28, 20, 15, 238), fade));
    }
    dl->AddRect(r.Min, r.Max, Alpha(lit || pressed ? GLOW : BRONZE, fade), 0, line);
    dl->AddRect(ImVec2(r.Min.x + in, r.Min.y + in), ImVec2(r.Max.x - in, r.Max.y - in),
                Alpha(lit || pressed ? IM_COL32(255, 236, 170, 200) : IM_COL32(222, 186, 96, 150), fade), 0,
                std::max(1.f, line * .45f));
}

void Cross(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float t) {
    dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, t);
    dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), col, t);
}

// the game's cursor: four corner brackets round `r`
void Brackets(ImDrawList* dl, const ImRect& r, float len, float t, ImU32 col) {
    const ImVec2 a = r.Min, b = r.Max;
    for (int k = 0; k < 4; ++k) {
        const float x = (k & 1) ? b.x : a.x, y = (k & 2) ? b.y : a.y, sx = (k & 1) ? -1.f : 1.f, sy = (k & 2) ? -1.f : 1.f;
        dl->AddLine(ImVec2(x, y), ImVec2(x + sx * len, y), IM_COL32(0, 0, 0, 200), t + 2);
        dl->AddLine(ImVec2(x, y), ImVec2(x, y + sy * len), IM_COL32(0, 0, 0, 200), t + 2);
        dl->AddLine(ImVec2(x, y), ImVec2(x + sx * len, y), col, t);
        dl->AddLine(ImVec2(x, y), ImVec2(x, y + sy * len), col, t);
    }
}

// ---- our marks (drawn)
// the tree's own sign (its button and its title): a trunk with three branches, a node at each end
void TreeSign(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    const float t = std::max(1.5f, r * .16f);
    const ImVec2 a(c.x - r * .75f, c.y), m(c.x - r * .1f, c.y);
    dl->AddLine(a, m, col, t);
    for (float dy : {-.62f, 0.f, .62f}) {
        const ImVec2 e(c.x + r * .62f, c.y + dy * r);
        dl->AddLine(m, ImVec2(m.x, e.y), col, t);
        dl->AddLine(ImVec2(m.x, e.y), e, col, t);
        dl->AddCircleFilled(e, r * .2f, col);
    }
    dl->AddCircleFilled(a, r * .24f, col);
}
// the box: a chest with a lid line
void BoxMark(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    const float t = std::max(1.f, r * .2f);
    dl->AddRectFilled(ImVec2(c.x - r, c.y - r * .55f), ImVec2(c.x + r, c.y + r * .75f), IM_COL32(90, 58, 26, 255), r * .15f);
    dl->AddRect(ImVec2(c.x - r, c.y - r * .55f), ImVec2(c.x + r, c.y + r * .75f), col, r * .15f, t);
    dl->AddLine(ImVec2(c.x - r, c.y - r * .05f), ImVec2(c.x + r, c.y - r * .05f), col, t);
    dl->AddRectFilled(ImVec2(c.x - r * .2f, c.y - r * .2f), ImVec2(c.x + r * .2f, c.y + r * .2f), col);
}
// worn: a round gold plate with an E
void WornMark(ImDrawList* dl, ImFont* f, ImVec2 c, float r, ImU32 col) {
    dl->AddCircleFilled(c, r, IM_COL32(28, 20, 15, 255));
    dl->AddCircle(c, r, col, 0, std::max(1.f, r * .16f));
    const float fs = r * 1.45f;
    const ImVec2 sz = f->CalcTextSizeA(fs, FLT_MAX, 0, "E");
    dl->AddText(f, fs, ImVec2(c.x - sz.x / 2, c.y - sz.y / 2), col, "E");
}
// forged from scratch: a small hammer
void HammerMark(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    const float t = std::max(1.f, r * .22f);
    dl->AddLine(ImVec2(c.x - r * .7f, c.y + r * .8f), ImVec2(c.x + r * .25f, c.y - r * .15f), col, t);
    ImVec2 q[4] = {ImVec2(c.x - r * .15f, c.y - r * .95f), ImVec2(c.x + r * .9f, c.y + r * .1f),
                   ImVec2(c.x + r * .55f, c.y + r * .45f), ImVec2(c.x - r * .5f, c.y - r * .6f)};
    dl->AddConvexPolyFilled(q, 4, col);
}
// an element's or a status's mark: a disc of its colour with a sign of its own
void ElementMark(ImDrawList* dl, ImVec2 c, float r, int e, bool hidden) {
    const ImU32 col = ElementColour(e), ink = IM_COL32(255, 250, 235, 255);
    dl->AddCircleFilled(c, r, hidden ? IM_COL32(70, 62, 54, 255) : col);
    dl->AddCircle(c, r, IM_COL32(0, 0, 0, 200), 0, std::max(1.f, r * .14f));
    const float t = std::max(1.f, r * .18f), s = r * .58f;
    const ImU32 k = hidden ? col : ink;
    switch (e) {
    case weapon_tree::kFire: {
        ImVec2 q[3] = {ImVec2(c.x, c.y - s * 1.1f), ImVec2(c.x + s * .7f, c.y + s * .7f), ImVec2(c.x - s * .7f, c.y + s * .7f)};
        dl->AddConvexPolyFilled(q, 3, k);
        break;
    }
    case weapon_tree::kWater:
        dl->AddCircleFilled(ImVec2(c.x, c.y + s * .25f), s * .55f, k);
        dl->AddTriangleFilled(ImVec2(c.x, c.y - s * .95f), ImVec2(c.x + s * .52f, c.y + s * .1f), ImVec2(c.x - s * .52f, c.y + s * .1f), k);
        break;
    case weapon_tree::kThunder:
    case weapon_tree::kParalysis:
        dl->PathLineTo(ImVec2(c.x + s * .35f, c.y - s));
        dl->PathLineTo(ImVec2(c.x - s * .35f, c.y));
        dl->PathLineTo(ImVec2(c.x + s * .35f, c.y));
        dl->PathLineTo(ImVec2(c.x - s * .35f, c.y + s));
        dl->PathStroke(k, t);
        if (e == weapon_tree::kParalysis) dl->AddCircle(c, s * 1.05f, k, 0, t * .6f);
        break;
    case weapon_tree::kIce:
        for (int i = 0; i < 3; ++i) {
            const float an = 3.14159f * i / 3;
            dl->AddLine(ImVec2(c.x - std::cos(an) * s, c.y - std::sin(an) * s), ImVec2(c.x + std::cos(an) * s, c.y + std::sin(an) * s), k, t);
        }
        break;
    case weapon_tree::kDragon:
        dl->AddQuad(ImVec2(c.x, c.y - s), ImVec2(c.x + s * .7f, c.y), ImVec2(c.x, c.y + s), ImVec2(c.x - s * .7f, c.y), k, t);
        break;
    case weapon_tree::kPoison:
        dl->AddCircleFilled(ImVec2(c.x - s * .3f, c.y + s * .25f), s * .38f, k);
        dl->AddCircleFilled(ImVec2(c.x + s * .35f, c.y - s * .3f), s * .28f, k);
        break;
    case weapon_tree::kSleep:
        dl->PathLineTo(ImVec2(c.x - s * .55f, c.y - s * .6f));
        dl->PathLineTo(ImVec2(c.x + s * .55f, c.y - s * .6f));
        dl->PathLineTo(ImVec2(c.x - s * .55f, c.y + s * .6f));
        dl->PathLineTo(ImVec2(c.x + s * .55f, c.y + s * .6f));
        dl->PathStroke(k, t);
        break;
    case weapon_tree::kBlast:
        for (int i = 0; i < 4; ++i) {
            const float an = 3.14159f * i / 4;
            dl->AddLine(ImVec2(c.x - std::cos(an) * s, c.y - std::sin(an) * s), ImVec2(c.x + std::cos(an) * s, c.y + std::sin(an) * s), k, t);
        }
        break;
    default: break;
    }
}

// ---- a weapon class's icon: the game's (its icon sheet's cell, tinted by rarity as the game tints it) or, without
// it, one drawn here: a white shape with a dark edge, tinted the same way
ImTextureData* g_icon_tex = nullptr;
std::uint32_t g_icon_serial = 0;

ImTextureData* IconTexture(const Icons* ic) {
    if (!ic || !ic->serial || ic->cell <= 0 || ic->rgba.size() != (size_t)ic->cell * ic->cell * 4 * kWeaponClasses)
        return nullptr;
    if (g_icon_tex && g_icon_serial == ic->serial) return g_icon_tex;
    const int w = ic->cell * kWeaponClasses, h = ic->cell;
    if (!g_icon_tex) {
        g_icon_tex = IM_NEW(ImTextureData)();
        g_icon_tex->Create(ImTextureFormat_RGBA32, w, h);
        std::memcpy(g_icon_tex->Pixels, ic->rgba.data(), ic->rgba.size());
        ImGui::RegisterUserTexture(g_icon_tex);
    } else if (g_icon_tex->Width == w && g_icon_tex->Height == h && g_icon_tex->Status == ImTextureStatus_OK) {
        std::memcpy(g_icon_tex->Pixels, ic->rgba.data(), ic->rgba.size());  // (another language's sheet)
        ImTextureRect all;
        all.x = all.y = 0, all.w = (unsigned short)w, all.h = (unsigned short)h;
        g_icon_tex->UpdateRect = all;
        g_icon_tex->Updates.resize(0);
        g_icon_tex->Updates.push_back(all);
        g_icon_tex->SetStatus(ImTextureStatus_WantUpdates);
    } else {
        return g_icon_tex;  // (not ready for new pixels yet: the next frame)
    }
    g_icon_serial = ic->serial;
    return g_icon_tex;
}

// a drawn shape: points of a unit square (0..1, y down) in `r`
struct Shape {
    ImDrawList* dl;
    ImRect r;
    ImU32 fill, edge;
    float t;
    ImVec2 P(float x, float y) const { return ImVec2(r.Min.x + x * r.GetWidth(), r.Min.y + y * r.GetHeight()); }
    void Poly(std::initializer_list<ImVec2> pts) const {
        ImVec2 q[16];
        int n = 0;
        for (ImVec2 p : pts) q[n++] = P(p.x, p.y);
        dl->AddPolyline(q, n, edge, t * 2.2f, ImDrawFlags_Closed);
        dl->AddConcavePolyFilled(q, n, fill);
    }
    void Bar(float x0, float y0, float x1, float y1, float w) const {  // a thick line with an edge
        dl->AddLine(P(x0, y0), P(x1, y1), edge, (w + .07f) * r.GetWidth());
        dl->AddLine(P(x0, y0), P(x1, y1), fill, w * r.GetWidth());
    }
    void Disc(float x, float y, float rr) const {
        dl->AddCircleFilled(P(x, y), rr * r.GetWidth() + t, edge);
        dl->AddCircleFilled(P(x, y), rr * r.GetWidth(), fill);
    }
};

void DrawnIcon(ImDrawList* dl, const ImRect& r, int cls, ImU32 col) {
    const Shape s{dl, r, col, IM_COL32(16, 10, 8, 255), std::max(1.f, r.GetWidth() * .03f)};
    switch (cls) {
    case kGreatSword:
        s.Poly({{.18f, .74f}, {.66f, .14f}, {.88f, .1f}, {.84f, .32f}, {.3f, .84f}});
        s.Bar(.24f, .78f, .1f, .92f, .07f);
        break;
    case kLongSword:
        s.Poly({{.24f, .7f}, {.84f, .1f}, {.9f, .12f}, {.3f, .76f}});
        s.Bar(.18f, .64f, .34f, .8f, .06f);
        s.Bar(.24f, .76f, .1f, .9f, .05f);
        break;
    case kSwordAndShield:
        s.Poly({{.2f, .12f}, {.3f, .12f}, {.3f, .64f}, {.2f, .64f}});
        s.Bar(.1f, .66f, .4f, .66f, .06f);
        s.Bar(.25f, .68f, .25f, .88f, .06f);
        s.Disc(.68f, .58f, .22f);
        break;
    case kDualBlades:
        s.Poly({{.12f, .82f}, {.7f, .14f}, {.78f, .18f}, {.2f, .88f}});
        s.Poly({{.88f, .82f}, {.3f, .14f}, {.22f, .18f}, {.8f, .88f}});
        break;
    case kHammer:
        s.Bar(.5f, .4f, .5f, .9f, .08f);
        s.Poly({{.18f, .12f}, {.82f, .12f}, {.82f, .46f}, {.18f, .46f}});
        break;
    case kHuntingHorn:
        s.Bar(.3f, .9f, .48f, .5f, .07f);
        s.Poly({{.38f, .52f}, {.6f, .1f}, {.9f, .24f}, {.84f, .44f}, {.56f, .62f}});
        break;
    case kLance:
        s.Poly({{.3f, .08f}, {.38f, .3f}, {.34f, .92f}, {.26f, .92f}, {.22f, .3f}});
        s.Poly({{.5f, .3f}, {.86f, .3f}, {.86f, .74f}, {.68f, .9f}, {.5f, .74f}});
        break;
    case kGunlance:
        s.Poly({{.28f, .08f}, {.36f, .28f}, {.34f, .92f}, {.22f, .92f}, {.2f, .28f}});
        s.Bar(.14f, .5f, .42f, .5f, .08f);
        s.Poly({{.52f, .3f}, {.88f, .3f}, {.88f, .74f}, {.7f, .9f}, {.52f, .74f}});
        break;
    case kLightBowgun:
        s.Poly({{.12f, .4f}, {.86f, .4f}, {.86f, .54f}, {.5f, .54f}, {.4f, .72f}, {.28f, .72f}, {.32f, .54f}, {.12f, .54f}});
        break;
    case kHeavyBowgun:
        s.Poly({{.06f, .3f}, {.92f, .3f}, {.92f, .5f}, {.6f, .5f}, {.48f, .8f}, {.32f, .8f}, {.36f, .56f}, {.06f, .56f}});
        s.Bar(.6f, .3f, .6f, .2f, .1f);
        break;
    case kBow:
        dl->PathArcTo(s.P(.5f, .5f), r.GetWidth() * .4f, -2.3f, -0.84f, 12);
        dl->PathStroke(s.edge, r.GetWidth() * .14f);
        dl->PathArcTo(s.P(.5f, .5f), r.GetWidth() * .4f, -2.3f, -0.84f, 12);
        dl->PathStroke(s.fill, r.GetWidth() * .08f);
        s.Bar(.24f, .26f, .76f, .26f, .025f);
        s.Bar(.5f, .9f, .5f, .14f, .04f);
        break;
    case kSwitchAxe:
        s.Bar(.32f, .9f, .52f, .12f, .07f);
        s.Poly({{.5f, .14f}, {.88f, .2f}, {.84f, .5f}, {.46f, .4f}});
        break;
    case kInsectGlaive:
        s.Bar(.16f, .86f, .82f, .2f, .05f);
        s.Poly({{.78f, .24f}, {.9f, .06f}, {.86f, .3f}});
        s.Poly({{.2f, .82f}, {.08f, .94f}, {.12f, .76f}});
        s.Disc(.3f, .32f, .1f);
        break;
    case kChargeBlade:
        s.Poly({{.12f, .2f}, {.5f, .2f}, {.5f, .64f}, {.31f, .84f}, {.12f, .64f}});
        s.Poly({{.62f, .1f}, {.74f, .1f}, {.74f, .74f}, {.62f, .74f}});
        s.Bar(.68f, .76f, .68f, .92f, .06f);
        break;
    default: s.Disc(.5f, .5f, .3f); break;
    }
}

void ClassIcon(ImDrawList* dl, const ImRect& r, const Content& c, ImU32 col) {
    if (c.weapon_class < 0 || c.weapon_class >= kWeaponClasses) return;
    if (ImTextureData* t = IconTexture(c.icons)) {
        const float n = static_cast<float>(int{kWeaponClasses});
        const float u0 = c.weapon_class / n, u1 = (c.weapon_class + 1) / n;
        dl->AddImage(t->GetTexRef(), r.Min, r.Max, ImVec2(u0, 0), ImVec2(u1, 1), col);
    } else {
        DrawnIcon(dl, r, c.weapon_class, col);
    }
}

ImFont* TitleFont() {
    ImGuiIO& io = ImGui::GetIO();
    return io.Fonts->Fonts.Size > 1 ? io.Fonts->Fonts[1] : ImGui::GetFont();
}

// `t` cut with "..." to fit `most` pixels
std::string Fit(ImFont* f, float size, const std::string& t, float most) {
    if (f->CalcTextSizeA(size, FLT_MAX, 0, t.c_str()).x <= most) return t;
    std::string s = t;
    while (!s.empty()) {
        do s.pop_back(); while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xc0) == 0x80);  // (UTF-8)
        if (!s.empty()) s.pop_back();
        const std::string cut = s + "...";
        if (f->CalcTextSizeA(size, FLT_MAX, 0, cut.c_str()).x <= most) return cut;
    }
    return "";
}

// a line of `t` in `most` pixels: smaller (down to 3/4) before it is cut
void FitText(ImDrawList* dl, ImFont* f, float size, ImVec2 p, ImU32 col, const std::string& t, float most) {
    const float wide = Width(f, size, t.c_str());
    const float fs = wide > most ? std::max(size * .75f, size * most / wide) : size;
    Text(dl, f, fs, ImVec2(p.x, p.y + (size - fs) * .5f), col, Fit(f, fs, t, most).c_str());
}

// `t` in one line of `most` pixels, or in two (broken at a space, the second cut with "..." if it must)
std::vector<std::string> TwoLines(ImFont* f, float size, const std::string& t, float most) {
    if (Width(f, size, t.c_str()) <= most) return {t};
    std::size_t best = std::string::npos;
    for (std::size_t at = t.find(' '); at != std::string::npos; at = t.find(' ', at + 1))
        if (Width(f, size, t.substr(0, at).c_str()) <= most) best = at;
    if (best == std::string::npos) return {Fit(f, size, t, most)};
    return {t.substr(0, best), Fit(f, size, t.substr(best + 1), most)};
}

// A name in a box `most` wide and `tall` high: one line at `size`, else two a little smaller, centred on the box
void NameText(ImDrawList* dl, ImFont* f, float size, ImVec2 p, float tall, ImU32 col, const std::string& t, float most) {
    if (Width(f, size, t.c_str()) <= most) {
        Text(dl, f, size, ImVec2(p.x, p.y + (tall - size) * .5f), col, t.c_str());
        return;
    }
    // (a little smaller each time until both lines fit whole: a desktop's wide font)
    float fs = std::min(size * .84f, tall / 2.3f);
    std::vector<std::string> l = TwoLines(f, fs, t, most);
    for (int k = 0; k < 3 && l.back().size() >= 3 && l.back().compare(l.back().size() - 3, 3, "...") == 0; ++k)
        fs *= .9f, l = TwoLines(f, fs, t, most);
    const float y = p.y + (tall - fs * (l.size() == 2 ? 2.1f : 1.f)) * .5f;
    for (std::size_t i = 0; i < l.size(); ++i) Text(dl, f, fs, ImVec2(p.x, y + i * fs * 1.1f), col, l[i].c_str());
}

// A weapon's name as a node shows it: with its level where one is known (MHGU: "Buster Sword Lv3")
std::string Named(const weapon_tree::Weapon& w, int id, bool es) {
    std::string n = w.name.empty() ? "#" + std::to_string(id) : w.name;
    if (w.level > 0) n += std::string(" ") + Words("Lv", es) + std::to_string(w.level);
    return n;
}

// "Upgraded from <the one before>" (MHGU: "... Lv3", the level of it that upgrades)
std::string From(const weapon_tree::Weapon& p, const weapon_tree::Weapon& w, bool es) {
    std::string t = std::string(Words("Upgraded from", es)) + " " + p.name;
    if (w.from_level > 0) t += std::string(" ") + Words("Lv", es) + std::to_string(w.from_level);
    return t;
}

struct Metrics {
    float line, nw, nh, px, py, pad;
};
Metrics Measure(float w, float h) {
    Metrics m;
    m.line = std::max(2.f, .0042f * h);
    m.nh = .07f * h;                     // a node: its icon's square, then its name (narrower in a 4:3 window:
    m.nw = w < 1.5f * h ? .215f * h : .25f * h;  // three columns in sight)
    m.px = m.nw + (w < 1.5f * h ? .04f : .045f) * h;  // from a column to the next
    m.py = m.nh + .026f * h;             // from a row to the next
    m.pad = .025f * h;                   // round the tree in its area
    return m;
}

ImVec2 NodeAt(const Metrics& m, const ImRect& area, const State& s, const weapon_tree::Node& n) {
    return ImVec2(area.Min.x + m.pad + n.column * m.px - s.scroll_x, area.Min.y + m.pad + n.row * m.py - s.scroll_y);
}

// How far the tree scrolls: as far as whole columns and rows go (it can start at any column's or row's edge)
ImVec2 ScrollMost(const Metrics& m, const ImRect& area, const Content& c) {
    const float cw = 2 * m.pad + std::max(0, c.view.columns - 1) * m.px + m.nw;
    const float ch = 2 * m.pad + std::max(0, c.view.rows - 1) * m.py + m.nh;
    return ImVec2(cw > area.GetWidth() ? std::ceil((cw - area.GetWidth()) / m.px) * m.px : 0.f,
                  ch > area.GetHeight() ? std::ceil((ch - area.GetHeight()) / m.py) * m.py : 0.f);
}

void ClampScroll(const Metrics& m, const ImRect& area, const Content& c, State& s) {
    const ImVec2 most = ScrollMost(m, area, c);
    s.scroll_x = std::clamp(s.scroll_x, 0.f, most.x);
    s.scroll_y = std::clamp(s.scroll_y, 0.f, most.y);
}

// a gold arrow on the tree's edge where more of it is out of sight (dx, dy: the way it points)
void EdgeArrow(ImDrawList* dl, ImVec2 c, float r, int dx, int dy) {
    const ImVec2 tip(c.x + dx * r, c.y + dy * r), a(c.x - dx * r * .2f + dy * r, c.y - dy * r * .2f + dx * r),
        b(c.x - dx * r * .2f - dy * r, c.y - dy * r * .2f - dx * r);
    dl->AddTriangleFilled(ImVec2(tip.x + 1.5f, tip.y + 1.5f), ImVec2(a.x + 1.5f, a.y + 1.5f), ImVec2(b.x + 1.5f, b.y + 1.5f), IM_COL32(0, 0, 0, 160));
    dl->AddTriangleFilled(tip, a, b, GOLD);
}

// the focus kept in sight
void Reveal(const Metrics& m, const ImRect& area, const Content& c, State& s) {
    if (s.focus < 0 || s.focus >= static_cast<int>(c.view.nodes.size())) return;
    const weapon_tree::Node& n = c.view.nodes[s.focus];
    const float x = m.pad + n.column * m.px, y = m.pad + n.row * m.py;
    if (x - s.scroll_x < m.pad) s.scroll_x = x - m.pad;
    if (x + m.nw - s.scroll_x > area.GetWidth() - m.pad) s.scroll_x = x + m.nw - area.GetWidth() + m.pad;
    if (y - s.scroll_y < m.pad) s.scroll_y = y - m.pad;
    if (y + m.nh - s.scroll_y > area.GetHeight() - m.pad) s.scroll_y = y + m.nh - area.GetHeight() + m.pad;
    ClampScroll(m, area, c, s);
}

// A weapon's place in the tree: its icon on a dark tile, its marks, its name; the Smithy's lit, a "?" its icon's
// silhouette under a question mark
void DrawNode(ImDrawList* dl, const Metrics& m, const ImRect& r, const weapon_tree::Node& node, const Content& c, float H,
              bool es) {
    ImFont* font = ImGui::GetFont();
    ImFont* title = TitleFont();
    const bool next = node.flags & weapon_tree::kNext, smithy = node.flags & weapon_tree::kSelected;
    const bool owned = node.flags & (weapon_tree::kOwned | weapon_tree::kWorn);
    Plate(dl, r, m.line, smithy, false, next ? .82f : 1.f);
    if (owned && !smithy) dl->AddRectFilled(ImVec2(r.Min.x + m.line, r.Min.y + m.line), ImVec2(r.Max.x - m.line, r.Max.y - m.line),
                                           IM_COL32(152, 126, 40, 46));
    const ImRect tile(ImVec2(r.Min.x + m.line * 1.5f, r.Min.y + m.line * 1.5f), ImVec2(r.Min.x + m.nh - m.line * 1.5f, r.Max.y - m.line * 1.5f));
    dl->AddRectFilled(tile.Min, tile.Max, IM_COL32(10, 7, 6, 235));
    dl->AddRect(tile.Min, tile.Max, Alpha(smithy ? GLOW : BRONZE, .9f), 0, std::max(1.f, m.line * .5f));
    const float inset = tile.GetWidth() * .06f;
    const ImRect icon(ImVec2(tile.Min.x + inset, tile.Min.y + inset), ImVec2(tile.Max.x - inset, tile.Max.y - inset));
    const float x0 = tile.Max.x + .014f * H, room = r.Max.x - x0 - .012f * H;
    const float fs = .03f * H;
    if (next) {
        ClassIcon(dl, icon, c, SHADOW);
        const float qs = .05f * H;
        const ImVec2 sz = title->CalcTextSizeA(qs, FLT_MAX, 0, "?");
        Text(dl, title, qs, ImVec2(icon.GetCenter().x - sz.x / 2, icon.GetCenter().y - sz.y / 2), GOLD, "?");
        Text(dl, font, fs, ImVec2(x0, r.GetCenter().y - fs * .56f), FAINT, "???");
        return;
    }
    const weapon_tree::Weapon& w = c.cls.weapons[node.id];
    ClassIcon(dl, icon, c, RarityColour(w.rarity));
    // its marks on the icon's corners: worn or in the box below left, the element above right
    const float mr = tile.GetWidth() * .2f;
    if (node.flags & weapon_tree::kWorn) WornMark(dl, font, ImVec2(tile.Min.x + mr * .95f, tile.Max.y - mr * .95f), mr, GLOW);
    else if (node.flags & weapon_tree::kOwned) BoxMark(dl, ImVec2(tile.Min.x + mr * 1.05f, tile.Max.y - mr * .9f), mr * .85f, GOLD);
    if (w.element != weapon_tree::kElementNone)
        ElementMark(dl, ImVec2(tile.Max.x - mr * .9f, tile.Min.y + mr * .9f), mr * .8f, w.element, w.element_hidden);
    if (w.forge)  // (forged from scratch: the small hammer below right, as the games mark it)
        HammerMark(dl, ImVec2(tile.Max.x - mr * .85f, tile.Max.y - mr * .85f), mr * .75f, GLOW);
    const ImU32 ink = smithy ? BRIGHT : (node.flags & weapon_tree::kPast) ? FAINT : INK;
    NameText(dl, font, fs, ImVec2(x0, r.Min.y), r.GetHeight(), ink, Named(w, node.id, es), room);
}

// ---- the details of the weapon under the cursor
struct Column {
    ImDrawList* dl;
    ImFont *font, *title;
    float x, w, y, H;
    float ls;  // a line's text size
    void Rule() {
        y += ls * .3f;
        dl->AddLine(ImVec2(x, y), ImVec2(x + w, y), Alpha(BRONZE, .9f), std::max(1.f, H * .002f));
        y += ls * .45f;
    }
    // a label and its value, the value at a fixed column
    void Row(const char* label, const std::string& value, ImU32 vcol = INK, float indent = 0) {
        Text(dl, font, ls, ImVec2(x + indent, y), DIM, label);
        const float vx = x + w * .44f;
        FitText(dl, font, ls, ImVec2(vx, y), vcol, value, x + w - vx);
        y += ls * 1.28f;
    }
    void Line(const std::string& t, ImU32 col, float size, float indent = 0, ImFont* f = nullptr) {
        FitText(dl, f ? f : font, size, ImVec2(x + indent, y), col, t, w - indent);
        y += size * 1.28f;
    }
    // a text wrapped in the column's width (as many lines as it takes)
    void Wrap(const std::string& t, ImU32 col, float size, float indent = 0) {
        const float ww = w - indent, o = std::max(1.f, size * .06f);
        dl->AddText(font, size, ImVec2(x + indent + o, y + o), IM_COL32(0, 0, 0, 190), t.c_str(), nullptr, ww);
        dl->AddText(font, size, ImVec2(x + indent, y), col, t.c_str(), nullptr, ww);
        y += font->CalcTextSizeA(size, FLT_MAX, ww, t.c_str()).y + size * .28f;
    }
};

// A recipe: its heading and cost ("Upgrade from  1500z"), what it comes from on the next line, its materials and (MHGU)
// the category's value the game also asks for
void Recipe(Column& k, const char* head, const std::string& more, const weapon_tree::Recipe& r, bool es) {
    if (r.Empty()) return;
    k.y += k.ls * .25f;
    float zw = 0;
    if (r.zenny > 0) {  // (its cost at the right of its heading)
        char z[32];
        std::snprintf(z, sizeof z, "%dz", r.zenny);
        zw = Width(k.font, k.ls, z) + k.ls * .6f;
        Text(k.dl, k.font, k.ls, ImVec2(k.x + k.w - zw + k.ls * .6f, k.y), GOLD, z);
    }
    const std::string whole = std::string(Words(head, es)) + more;
    if (Width(k.title, k.ls, whole.c_str()) <= k.w - zw) {
        Text(k.dl, k.title, k.ls, ImVec2(k.x, k.y), GLOW, whole.c_str());
        k.y += k.ls * 1.3f;
    } else {
        FitText(k.dl, k.title, k.ls, ImVec2(k.x, k.y), GLOW, Words(head, es), k.w - zw);
        k.y += k.ls * 1.25f;
        k.Wrap(more.size() > 1 ? more.substr(1) : more, DIM, k.ls * .92f, k.ls * .3f);
    }
    const float is = k.ls * .92f;
    for (const weapon_tree::Material& mt : r.items) {
        char n[16];
        std::snprintf(n, sizeof n, "x%d", mt.count);
        const float nw = Width(k.font, is, n);
        Diamond(k.dl, ImVec2(k.x + is * .45f, k.y + is * .55f), is * .18f, FAINT);
        FitText(k.dl, k.font, is, ImVec2(k.x + is * 1.f, k.y), INK, mt.name.empty() ? "#" + std::to_string(mt.item) : mt.name,
                k.w - nw - is * 1.6f);
        Text(k.dl, k.font, is, ImVec2(k.x + k.w - nw, k.y), INK, n);
        k.y += is * 1.26f;
    }
    if (!r.points.empty()) {
        Diamond(k.dl, ImVec2(k.x + is * .45f, k.y + is * .55f), is * .18f, FAINT);
        k.Line(r.points, DIM, is, is * 1.f);
    }
}

void Details(ImDrawList* dl, const ImRect& box, const Content& c, State& s, float H, const Metrics& m) {
    const bool es = c.spanish;
    Column k{dl, ImGui::GetFont(), TitleFont(), box.Min.x + .024f * H, box.GetWidth() - .048f * H, 0, H, .03f * H};
    const float top = box.Min.y + .02f * H;
    k.y = top - s.detail_scroll;
    const int n = static_cast<int>(c.view.nodes.size());
    if (s.focus < 0 || s.focus >= n) return;
    const weapon_tree::Node& node = c.view.nodes[s.focus];
    const weapon_tree::Weapon& w = c.cls.weapons[node.id];
    const bool next = node.flags & weapon_tree::kNext;
    char line[256];
    // its icon, large, its name and rarity
    const float big = .12f * H;
    const ImRect tile(ImVec2(k.x, k.y), ImVec2(k.x + big, k.y + big));
    dl->AddRectFilled(tile.Min, tile.Max, IM_COL32(10, 7, 6, 240));
    dl->AddRect(tile.Min, tile.Max, (node.flags & weapon_tree::kSelected) ? GLOW : BRONZE, 0, m.line * .8f);
    const float in = big * .07f;
    const ImRect icon(ImVec2(tile.Min.x + in, tile.Min.y + in), ImVec2(tile.Max.x - in, tile.Max.y - in));
    const float tx = tile.Max.x + .018f * H, tw = k.x + k.w - tx;
    if (next) {
        ClassIcon(dl, icon, c, SHADOW);
        const float qs = .08f * H;
        const ImVec2 sz = k.title->CalcTextSizeA(qs, FLT_MAX, 0, "?");
        Text(dl, k.title, qs, ImVec2(icon.GetCenter().x - sz.x / 2, icon.GetCenter().y - sz.y / 2), GOLD, "?");
        Text(dl, k.title, .042f * H, ImVec2(tx, k.y + .006f * H), GOLD, "???");
        k.y = tile.Max.y + .012f * H;
        k.Wrap(Words("Next: not known yet", es), INK, k.ls);
        if (node.parent >= 0) k.Wrap(From(c.cls.weapons[c.view.nodes[node.parent].id], w, es), DIM, k.ls * .92f);
        s.detail_height = k.y + s.detail_scroll - top;
        return;
    }
    ClassIcon(dl, icon, c, RarityColour(w.rarity));
    // (its name in one line, or two a little smaller)
    float ts = .04f * H;
    std::vector<std::string> name = TwoLines(k.title, ts, Named(w, node.id, es), tw);
    for (int k2 = 0; k2 < 4 && name.size() > 1 && (ts > .034f * H || (name.back().size() >= 3 &&
                                                     name.back().compare(name.back().size() - 3, 3, "...") == 0)); ++k2)
        ts = std::min(ts * .9f, .034f * H), name = TwoLines(k.title, ts, Named(w, node.id, es), tw);
    float ty = k.y + .002f * H;
    for (const std::string& l : name) Text(dl, k.title, ts, ImVec2(tx, ty), RarityColour(w.rarity), l.c_str()), ty += ts * 1.12f;
    ty += ts * .18f;
    if (w.rarity > 0) {
        std::snprintf(line, sizeof line, "%s %d", Words("Rarity", es), w.rarity);
        Text(dl, k.font, k.ls, ImVec2(tx, ty), RarityColour(w.rarity), line);
        ty += k.ls * 1.25f;
    }
    if (w.levels > 1 && w.level > 0) {
        std::snprintf(line, sizeof line, "%s %d / %d", Words("Level", es), w.level, w.levels);
        Text(dl, k.font, k.ls, ImVec2(tx, ty), INK, line);
        ty += k.ls * 1.25f;
    }
    {  // where it is: worn, in the box, the Smithy's
        const char* where = (node.flags & weapon_tree::kWorn) ? "Equipped" : (node.flags & weapon_tree::kOwned) ? "In the box"
                            : (node.flags & weapon_tree::kSelected) ? "The Smithy's weapon" : nullptr;
        if (where) FitText(dl, k.font, k.ls * .92f, ImVec2(tx, ty), DIM, Words(where, es), tw), ty += k.ls * 1.2f;
    }
    k.y = std::max(tile.Max.y, ty - k.ls * .2f) + .006f * H;
    k.Rule();
    if (w.attack > 0) {
        k.Row(Words("Attack", es), std::to_string(w.attack));
        for (int e = 0; e < 2; ++e) {
            const int el = e == 0 ? w.element : w.element2, v = e == 0 ? w.element_value : w.element2_value;
            if (el == weapon_tree::kElementNone) continue;
            const bool hid = e == 0 && w.element_hidden;
            ElementMark(dl, ImVec2(k.x + k.ls * .5f, k.y + k.ls * .55f), k.ls * .5f, el, hid);
            std::snprintf(line, sizeof line, hid ? "%d (%s)" : "%d", v, Words("needs Awaken", es));
            Text(dl, k.font, k.ls, ImVec2(k.x + k.ls * 1.25f, k.y), DIM, Words(ElementWord(el), es));
            const float vx = k.x + k.w * .44f;
            FitText(dl, k.font, k.ls, ImVec2(vx, k.y), hid ? FAINT : INK, line, k.x + k.w - vx);
            k.y += k.ls * 1.28f;
        }
        std::snprintf(line, sizeof line, "%+d%%", w.affinity);
        k.Row(Words("Affinity", es), w.affinity ? line : "0%", w.affinity > 0 ? IM_COL32(150, 220, 120, 255)
                                                            : w.affinity < 0 ? IM_COL32(236, 110, 96, 255) : INK);
        if (w.defense != 0) {
            std::snprintf(line, sizeof line, "%+d", w.defense);
            k.Row(Words("Defense", es), line);
        }
        Text(dl, k.font, k.ls, ImVec2(k.x, k.y), DIM, Words("Slots", es));
        for (int i = 0; i < 3; ++i) {
            const ImVec2 sc(k.x + k.w * .44f + k.ls * .45f + i * k.ls * 1.05f, k.y + k.ls * .56f);
            if (i < w.slots) dl->AddCircleFilled(sc, k.ls * .34f, GOLD), dl->AddCircle(sc, k.ls * .34f, IM_COL32(0, 0, 0, 200), 0, 1.5f);
            else dl->AddLine(ImVec2(sc.x - k.ls * .3f, sc.y), ImVec2(sc.x + k.ls * .3f, sc.y), FAINT, std::max(1.5f, m.line * .7f));
        }
        k.y += k.ls * 1.28f;
        if (!w.sharpness.empty() && c.cls.sharpness_full > 0) {  // the bar, black where it has none
            Text(dl, k.font, k.ls, ImVec2(k.x, k.y), DIM, Words("Sharpness", es));
            const float bx = k.x + k.w * .44f, bw = k.x + k.w - bx, bh = k.ls * .62f, by = k.y + k.ls * .2f;
            dl->AddRectFilled(ImVec2(bx - 2, by - 2), ImVec2(bx + bw + 2, by + bh + 2), IM_COL32(120, 120, 120, 255));
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), IM_COL32(8, 8, 8, 255));
            const float u = bw / c.cls.sharpness_full;
            int from = 0;
            for (int i = 0; i < 7 && i < static_cast<int>(w.sharpness.size()); ++i) {
                const int to = std::min(w.sharpness[i], c.cls.sharpness_full);
                if (to > from) dl->AddRectFilled(ImVec2(bx + from * u, by), ImVec2(bx + to * u, by + bh), SHARP[i]);
                from = std::max(from, to);
            }
            if (w.sharpness_plus > from) {  // (what the skill adds: a thin bar under it, in the colours it reaches)
                int at = from;
                for (int i = 0; i < 7 && i < static_cast<int>(w.sharpness.size()); ++i) {
                    const int lo = i ? w.sharpness[i - 1] : 0, hi = std::min(w.sharpness[i], w.sharpness_plus);
                    const int a0 = std::max(lo, at);
                    if (hi > a0) dl->AddRectFilled(ImVec2(bx + a0 * u, by + bh * .55f), ImVec2(bx + hi * u, by + bh), Alpha(SHARP[i], .75f));
                    at = std::max(at, hi);
                }
            }
            k.y += k.ls * 1.28f;
        }
    }
    // what making it takes: forged from scratch, the upgrade from the one before it, (MHGU) its next level
    std::string from_more;
    if (node.parent >= 0) {
        const weapon_tree::Weapon& p = c.cls.weapons[c.view.nodes[node.parent].id];
        from_more = " " + p.name;
        if (w.from_level > 0) from_more += std::string(" ") + Words("Lv", es) + std::to_string(w.from_level);
    }
    k.Rule();
    if (w.create.Empty() && w.forge) k.Wrap(Words("Forged from scratch", es), DIM, k.ls * .92f);  // (no recipe read)
    Recipe(k, "Forge", "", w.create, es);
    if (node.parent >= 0) {
        if (w.upgrade.Empty()) k.Wrap(From(c.cls.weapons[c.view.nodes[node.parent].id], w, es), DIM, k.ls * .92f);
        Recipe(k, "Upgrade from", from_more, w.upgrade, es);
    }
    if (w.next.level > 0) Recipe(k, "Next level:", std::string(" ") + Words("Lv", es) + std::to_string(w.next.level), w.next, es);
    s.detail_height = k.y + s.detail_scroll - top;
}

// the legend under the tree: box, worn, ?, hammer, and the panel's note; it wraps when it is narrow
void Legend(ImDrawList* dl, const ImRect& r, float H, bool es) {
    ImFont* font = ImGui::GetFont();
    ImFont* title = TitleFont();
    const char* items[4] = {"In the box", "Equipped", "Next: not known yet", "Forged from scratch"};
    // (in a row, or in as few as fit, wrapped where the next does not fit; smaller when three would be needed)
    float ls = .027f * H, mr = .013f * H, gap = .03f * H;
    auto rows = [&]() {
        int n = 1;
        float x = 0;
        for (const char* t : items) {
            const float iw = mr * 2.8f + Width(font, ls, Words(t, es));
            if (x > 0 && x + iw > r.GetWidth()) ++n, x = 0;
            x += iw + gap;
        }
        return n;
    };
    for (int k = 0; k < 4 && rows() > 2; ++k) ls *= .9f, mr *= .9f, gap *= .9f;
    const int n = rows();
    float x = r.Min.x, y = r.Min.y + (r.GetHeight() - ls * (n == 1 ? 1.f : 2.3f)) * .5f;
    for (int i = 0; i < 4; ++i) {
        const float iw = mr * 2.8f + Width(font, ls, Words(items[i], es));
        if (x > r.Min.x && x + iw > r.Max.x) x = r.Min.x, y += ls * 1.3f;
        const ImVec2 c(x + mr, y + ls * .55f);
        if (i == 0) BoxMark(dl, c, mr, GOLD);
        if (i == 1) WornMark(dl, font, c, mr * 1.1f, GLOW);
        if (i == 2) Text(dl, title, ls * 1.2f, ImVec2(x + mr * .45f, y - ls * .12f), GOLD, "?");
        if (i == 3) HammerMark(dl, c, mr, DIM);
        Text(dl, font, ls, ImVec2(x + mr * 2.8f, y), DIM, Words(items[i], es));
        x += iw + gap;
    }
}

}  // namespace

const char* Words(const char* english, bool spanish) {
    if (!spanish) return english;
    static const struct {
        const char *en, *es;
    } k[] = {
        {"Weapon tree", "Árbol de armas"},
        {"In the box", "En la caja"},
        {"Equipped", "Equipada"},
        {"Next: not known yet", "Siguiente: todavía no la conoces"},
        {"Forged from scratch", "Se forja desde cero"},
        {"Upgraded from", "Se mejora desde"},
        {"Upgrade from", "Mejora desde"},
        {"Forge", "Forja"},
        {"Next level:", "Nivel siguiente:"},
        {"value", "valor"},
        {"Lv", "Nv"},
        {"Level", "Nivel"},
        {"Rarity", "Rareza"},
        {"Attack", "Ataque"},
        {"Affinity", "Afinidad"},
        {"Defense", "Defensa"},
        {"Slots", "Ranuras"},
        {"Sharpness", "Filo"},
        {"needs Awaken", "necesita Despertar"},
        {"Fire", "Fuego"},
        {"Water", "Agua"},
        {"Thunder", "Trueno"},
        {"Ice", "Hielo"},
        {"Dragon", "Dragón"},
        {"Poison", "Veneno"},
        {"Paralysis", "Parálisis"},
        {"Sleep", "Sueño"},
        {"Blast", "Explosión"},
        {"Close", "Cerrar"},
        {"Move", "Mover"},
        {"The Smithy's weapon", "El arma de la herrería"},
        {"Read only: forge and upgrade in the Smithy.", "Solo consulta: se forja y se mejora en la herrería."},
        {"Great Sword", "Gran espada"},
        {"Sword and Shield", "Espada y escudo"},
        {"Hammer", "Martillo"},
        {"Lance", "Lanza"},
        {"Light Bowgun", "Ballesta ligera"},
        {"Heavy Bowgun", "Ballesta pesada"},
        {"Long Sword", "Espada larga"},
        {"Switch Axe", "Hacha espada"},
        {"Gunlance", "Lanza pistola"},
        {"Bow", "Arco"},
        {"Dual Blades", "Espadas dobles"},
        {"Hunting Horn", "Cornamusa"},
        {"Insect Glaive", "Glaive insecto"},
        {"Charge Blade", "Hacha cargada"},
    };
    for (const auto& m : k)
        if (std::strcmp(m.en, english) == 0) return m.es;
    return english;
}

void Draw(ImDrawList* dl, float W, float H, float a, bool pad, const Content& c, State& s) {
    const bool es = c.spanish;
    ImFont* title = TitleFont();
    ImFont* font = ImGui::GetFont();
    const Metrics m = Measure(W, H);
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 0, (int)(120 * a)));
    const int v0 = dl->VtxBuffer.Size;
    // the window: as wide as fits (16:9 and wider: up to 1.75 heights): the title, the tree and its legend at the
    // left, the details at the right
    const float pw = std::min(W * .96f, H * 1.75f), ph = H * .92f;
    const ImVec2 p0(std::floor((W - pw) / 2), std::floor((H - ph) / 2));
    s.panel = ImRect(p0, ImVec2(p0.x + pw, p0.y + ph));
    WindowPlate(dl, s.panel, m.line);
    const float th = .085f * H, dw = std::clamp(pw * .33f, .4f * H, .45f * H), lh = .06f * H;
    {  // the title: the tree's sign, "Weapon tree - <class>", the class's icon
        const float cy = p0.y + th * .55f;
        TreeSign(dl, ImVec2(p0.x + .045f * H, cy), .022f * H, GLOW);
        std::string t = std::string(Words("Weapon tree", es)) + "  -  " + c.class_name;
        Text(dl, title, .048f * H, ImVec2(p0.x + .08f * H, cy - .029f * H), GLOW, t.c_str());
        const float xs = .066f * H, xr = p0.x + pw - .03f * H;
        s.close = ImRect(ImVec2(xr - xs, cy - xs / 2), ImVec2(xr, cy + xs / 2));
        Plate(dl, s.close, m.line, false, s.close_held);
        Cross(dl, s.close.GetCenter(), xs * .2f, s.close_held ? BRIGHT : INK, std::max(2.f, xs * .085f));
        const float note_w = s.close.Min.x - .03f * H - (p0.x + .1f * H + Width(title, .048f * H, t.c_str()));
        const char* note = Words("Read only: forge and upgrade in the Smithy.", es);
        if (note_w > Width(font, .025f * H, note))
            Text(dl, font, .025f * H, ImVec2(s.close.Min.x - .03f * H - Width(font, .025f * H, note), cy - .013f * H), FAINT, note);
    }
    s.area = ImRect(ImVec2(p0.x + .025f * H, p0.y + th), ImVec2(p0.x + pw - dw - .045f * H, p0.y + ph - lh - .03f * H));
    s.detail = ImRect(ImVec2(s.area.Max.x + .02f * H, p0.y + th), ImVec2(p0.x + pw - .025f * H, p0.y + ph - .03f * H));
    dl->AddRectFilled(s.area.Min, s.area.Max, IM_COL32(18, 12, 10, 160));
    dl->AddRect(s.area.Min, s.area.Max, Alpha(BRONZE, .8f), 0, std::max(1.f, m.line * .5f));
    if (s.focus < 0 || s.focus >= static_cast<int>(c.view.nodes.size())) s.focus = c.view.selected, s.detail_scroll = 0;
    if (!s.placed) {  // (the Smithy's weapon in sight, with what follows it, as far as the tree allows)
        s.placed = true;
        if (c.view.selected >= 0) {
            const weapon_tree::Node& n = c.view.nodes[c.view.selected];
            bool kids = false;
            for (const weapon_tree::Node& o : c.view.nodes) kids = kids || o.parent == c.view.selected;
            // (whole columns: the leftmost one shown from its start, the Smithy's never cut)
            const float want = m.pad + n.column * m.px + m.nw + (kids ? m.px : 0.f) + m.pad - s.area.GetWidth();
            s.scroll_x = want <= 1 ? 0.f : std::min(std::ceil((want - 1) / m.px) * m.px, n.column * m.px);
            s.scroll_y = m.pad + n.row * m.py + m.nh / 2 - s.area.GetHeight() / 2;
        }
    }
    ClampScroll(m, s.area, c, s);
    // the tree: its links, then its weapons
    dl->PushClipRect(s.area.Min, s.area.Max, true);
    const int n = static_cast<int>(c.view.nodes.size());
    s.nodes.assign(n, ImRect());
    for (int i = 0; i < n; ++i) {
        const weapon_tree::Node& node = c.view.nodes[i];
        if (node.parent < 0) continue;
        const ImVec2 from = NodeAt(m, s.area, s, c.view.nodes[node.parent]), to = NodeAt(m, s.area, s, node);
        const ImVec2 a0(from.x + m.nw, from.y + m.nh / 2), a1(to.x, to.y + m.nh / 2);
        const float mx = a0.x + (m.px - m.nw) / 2;
        const bool next = node.flags & weapon_tree::kNext;
        const ImU32 col = next ? Alpha(BRONZE, .95f) : IM_COL32(214, 176, 104, 255);
        const float t = std::max(2.f, m.line * (next ? .8f : 1.15f));
        dl->AddLine(a0, ImVec2(mx, a0.y), col, t);
        dl->AddLine(ImVec2(mx, a0.y - t * .5f), ImVec2(mx, a1.y + t * .5f), col, t);
        dl->AddLine(ImVec2(mx, a1.y), a1, col, t);
    }
    for (int i = 0; i < n; ++i) {
        const weapon_tree::Node& node = c.view.nodes[i];
        const ImVec2 q = NodeAt(m, s.area, s, node);
        const ImRect r(q, ImVec2(q.x + m.nw, q.y + m.nh));
        s.nodes[i] = r;
        if (!r.Overlaps(s.area)) continue;
        DrawNode(dl, m, r, node, c, H, es);
        if (i == s.focus && (pad || !(node.flags & weapon_tree::kSelected)))  // (the cursor: the game's corner brackets)
            Brackets(dl, ImRect(ImVec2(r.Min.x - m.line * 2, r.Min.y - m.line * 2), ImVec2(r.Max.x + m.line * 2, r.Max.y + m.line * 2)),
                     m.nh * .28f, std::max(2.f, m.line * .9f), BRIGHT);
    }
    dl->PopClipRect();
    {  // (where the tree goes on out of sight)
        const ImVec2 most = ScrollMost(m, s.area, c);
        const float r = .016f * H, in = .016f * H;
        const ImVec2 mid = s.area.GetCenter();
        if (s.scroll_x > 1) EdgeArrow(dl, ImVec2(s.area.Min.x + in, mid.y), r, -1, 0);
        if (s.scroll_x < most.x - 1) EdgeArrow(dl, ImVec2(s.area.Max.x - in, mid.y), r, 1, 0);
        if (s.scroll_y > 1) EdgeArrow(dl, ImVec2(mid.x, s.area.Min.y + in), r, 0, -1);
        if (s.scroll_y < most.y - 1) EdgeArrow(dl, ImVec2(mid.x, s.area.Max.y - in), r, 0, 1);
    }
    Legend(dl, ImRect(ImVec2(s.area.Min.x + .01f * H, s.area.Max.y + .008f * H), ImVec2(s.area.Max.x, p0.y + ph - .022f * H)), H, es);
    // the details of the focus
    Plate(dl, s.detail, m.line, false);
    const float most = std::max(0.f, s.detail_height - (s.detail.GetHeight() - .04f * H));
    s.detail_scroll = std::clamp(s.detail_scroll, 0.f, most);
    dl->PushClipRect(ImVec2(s.detail.Min.x + m.line, s.detail.Min.y + m.line), ImVec2(s.detail.Max.x - m.line, s.detail.Max.y - m.line), true);
    Details(dl, s.detail, c, s, H, m);
    dl->PopClipRect();
    if (most > 0) {  // (taller than its column: a thin bar shows where it is)
        const float bh = s.detail.GetHeight() - .03f * H, k = (s.detail.GetHeight() - .04f * H) / std::max(1.f, s.detail_height);
        const float y0 = s.detail.Min.y + .015f * H + bh * (s.detail_scroll / std::max(1.f, s.detail_height));
        dl->AddRectFilled(ImVec2(s.detail.Max.x - .01f * H, y0), ImVec2(s.detail.Max.x - .005f * H, y0 + bh * k), Alpha(GOLD, .7f));
    }
    // the open's animation: the window grows and fades in
    for (int i = v0; i < dl->VtxBuffer.Size; ++i) {
        ImDrawVert& v = dl->VtxBuffer.Data[i];
        const ImVec2 cc = s.panel.GetCenter();
        const float k = .94f + .06f * a;
        v.pos.x = cc.x + (v.pos.x - cc.x) * k, v.pos.y = cc.y + (v.pos.y - cc.y) * k;
        v.col = Alpha(v.col, a);
    }
}

bool Finger(int kind, float x, float y, const Content& c, State& s, bool* close) {
    const ImVec2 p(x, y);
    *close = false;
    if (kind == kDown) {
        s.finger = true, s.dragged = false, s.finger_at = s.finger_last = p;
        s.close_held = s.close.Contains(p);
        return true;
    }
    if (!s.finger) return true;
    if (kind == kMove) {
        const ImVec2 d(p.x - s.finger_last.x, p.y - s.finger_last.y);
        s.finger_last = p;
        if (std::fabs(p.x - s.finger_at.x) + std::fabs(p.y - s.finger_at.y) > .02f * ImGui::GetIO().DisplaySize.y) s.dragged = true;
        if (s.dragged && s.area.Contains(s.finger_at)) {  // (a drag in the tree moves it)
            const Metrics m = Measure(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
            s.scroll_x -= d.x, s.scroll_y -= d.y;
            ClampScroll(m, s.area, c, s);
        } else if (s.dragged && s.detail.Contains(s.finger_at)) {  // (one in the details scrolls them)
            s.detail_scroll -= d.y;  // (clamped as they are drawn)
        }
        s.close_held = s.close.Contains(p) && s.close.Contains(s.finger_at);
        return true;
    }
    // up: a tap
    s.finger = false;
    const bool was_held = s.close_held;
    s.close_held = false;
    if (s.dragged) return true;
    if (was_held && s.close.Contains(p)) return *close = true, true;
    if (!s.panel.Contains(p) && !s.panel.Contains(s.finger_at)) return *close = true, true;  // (outside: closed)
    if (s.area.Contains(p)) {
        for (int i = 0; i < static_cast<int>(s.nodes.size()); ++i)
            if (s.nodes[i].Contains(p) && s.focus != i) s.focus = i, s.detail_scroll = 0;
    }
    return true;
}

void Move(int dx, int dy, const Content& c, State& s) {
    const int n = static_cast<int>(c.view.nodes.size());
    if (n == 0) return;
    if (s.focus < 0 || s.focus >= n) s.focus = std::max(0, c.view.selected);
    const weapon_tree::Node& f = c.view.nodes[s.focus];
    int best = -1;
    if (dx < 0) {
        best = f.parent;
    } else if (dx > 0) {
        for (int i = 0; i < n && best < 0; ++i)
            if (c.view.nodes[i].parent == s.focus) best = i;
    } else if (dy != 0) {  // (the nearest weapon in the next row up or down, by column)
        float score = 1e9f;
        for (int i = 0; i < n; ++i) {
            const weapon_tree::Node& o = c.view.nodes[i];
            const int dr = o.row - f.row;
            if (dr == 0 || (dr > 0) != (dy > 0)) continue;
            const float sc = std::abs(dr) * 10.f + std::abs(o.column - f.column);
            if (sc < score) score = sc, best = i;
        }
    }
    if (best >= 0) {
        s.focus = best, s.detail_scroll = 0;
        const Metrics m = Measure(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
        Reveal(m, s.area, c, s);
    }
}

}  // namespace weapon_tree_panel
