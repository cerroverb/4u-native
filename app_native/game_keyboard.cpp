// The phone's own keyboard for the game's text (game_keyboard.h).
#include "game_keyboard.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <string>
#include <vector>

#include "swkbd_bridge.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "settings_panel.h"
#include "ui.h"

namespace kb = Port::Keyboard;

namespace {

// ---- the text, as the game counts it (UTF-16) and as SDL and ImGui have it (UTF-8)
std::u16string to16(const std::string& s) {
    std::u16string out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        char32_t cp = 0xFFFD;
        int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        if (i + n > s.size()) n = 1;
        if (n == 1) cp = c < 0x80 ? c : 0xFFFD;
        else {
            cp = c & (0x7F >> n);
            for (int k = 1; k < n; k++) cp = (cp << 6) | (s[i + k] & 0x3F);
        }
        i += n;
        if (cp > 0xFFFF) {  // (outside the game's font: a surrogate pair it would draw as two blanks)
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
    return out;
}
std::string to8(const std::u16string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        char32_t cp = s[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size()) cp = 0x10000 + ((cp - 0xD800) << 10) + (s[++i] - 0xDC00);
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) out += static_cast<char>(0xC0 | cp >> 6), out += static_cast<char>(0x80 | (cp & 0x3F));
        else if (cp < 0x10000)
            out += static_cast<char>(0xE0 | cp >> 12), out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)),
                out += static_cast<char>(0x80 | (cp & 0x3F));
        else
            out += static_cast<char>(0xF0 | cp >> 18), out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)),
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)), out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return out;
}

const char* say(const char* en, const char* es) { return settings_spanish() ? es : en; }

// ---- the session: one per keyboard the game opens
struct Session {
    bool active = false;
    std::uint32_t serial = 0;
    kb::Request req;             // what the game asked for
    std::u16string text;         // what the panel shows (and the game's text box gets)
    std::u16string sent;         // the last text handed to the game
    std::string composing;       // the phone keyboard's word in progress (not the game's yet)
    std::uint32_t game_serial = 0;
    bool hidden = false;         // put away for the game's own keys
    Uint64 decided_at = 0;       // OK or Cancel pressed: hidden while the game closes its keyboard
};
Session g;
bool g_input_on = false;  // SDL's text input started by this panel
bool g_showed_bottom = false;  // the touch screen's corner shown for the game's own keys (hidden again after)
int g_w = 0, g_h = 0;

// the places of the panel's parts last frame (window pixels: the fingers'), for the taps
enum Part { NONE, PANEL, FIELD, CLEAR, GAME_KEYS, CANCEL, OK, TAB };
struct Hit {
    Part part;
    ImRect r;
};
std::vector<Hit> g_hits;
Part g_pressed = NONE;
SDL_FingerID g_finger = 0;

SDL_Window* window() {
    if (SDL_Window* w = SDL_GetKeyboardFocus()) return w;
    int n = 0;
    SDL_Window** all = SDL_GetWindows(&n);
    SDL_Window* w = all && n > 0 ? all[0] : nullptr;
    SDL_free(all);
    return w;
}

// The phone's keyboard as the game's keyboard asks: numbers for a number pad, plain text (no suggestions, which
// would hold a word back until it is done) otherwise. `screen`: the keyboard on the screen too (a physical one types
// either way)
void input_on(bool screen) {
    SDL_Window* w = window();
    if (!w) return;
    if (g_input_on) SDL_StopTextInput(w);
    SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, screen ? "1" : "0");
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_TYPE_NUMBER, g.req.numeric ? SDL_TEXTINPUT_TYPE_NUMBER : SDL_TEXTINPUT_TYPE_TEXT);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_CAPITALIZATION_NUMBER, SDL_CAPITALIZE_SENTENCES);
    SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_AUTOCORRECT_BOOLEAN, false);
    SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_MULTILINE_BOOLEAN, false);
    SDL_StartTextInputWithProperties(w, props);
    SDL_DestroyProperties(props);
    SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "1");
    g_input_on = true;
}
void input_off() {
    if (!g_input_on) return;
    if (SDL_Window* w = window()) SDL_StopTextInput(w);
    g_input_on = false;
    g.composing.clear();
}

bool showing() { return g.active && !g.hidden && !g.decided_at; }

// ---- the text's rules: the game's keyboard's (its length, a number pad's digits, its filters)
bool takes(char16_t c, const std::u16string& text) {
    if (c < 0x20 || c == 0x7F || (c >= 0x80 && c < 0xA0)) return false;
    if (g.req.numeric && !(c >= '0' && c <= '9')) return false;
    if ((g.req.filters & 2) && c == '@') return false;
    if ((g.req.filters & 4) && c == '%') return false;
    if ((g.req.filters & 8) && c == '\\') return false;
    if ((g.req.filters & 1) && c >= '0' && c <= '9' &&
        (std::uint32_t)std::count_if(text.begin(), text.end(), [](char16_t d) { return d >= '0' && d <= '9'; }) >= g.req.max_digits)
        return false;
    return true;
}
void type(const std::string& utf8) {
    for (char16_t c : to16(utf8)) {
        if (c >= 0xD800 && c < 0xE000) continue;  // (an emoji and the like: not in the game's font)
        if (g.text.size() >= g.req.max_length || !takes(c, g.text)) continue;
        g.text.push_back(c);
    }
}
bool blank(const std::u16string& t) {
    return std::all_of(t.begin(), t.end(), [](char16_t c) { return c == ' ' || c == 0x3000; });
}
// Whether the game takes the text as it is (its valid-input rule)
bool acceptable(const std::u16string& t) {
    switch (g.req.accept) {
    case 1: return !t.empty();
    case 2: return !t.empty() && !blank(t);
    case 3: return !blank(t);
    case 4: return t.size() == g.req.max_length;
    default: return true;
    }
}

void hand_over() {  // the panel's text into the game's text box
    if (g.text == g.sent) return;
    g.sent = g.text;
    kb::SetText(g.serial, g.text);
}
void ok() {
    if (!g.composing.empty()) type(g.composing), g.composing.clear();
    if (!acceptable(g.text)) return;
    g.sent = g.text;
    kb::Submit(g.serial, g.text);
    g.decided_at = SDL_GetTicks();
    input_off();
}
void cancel() {
    if (g.req.buttons != 1 && g.req.buttons != 2) return;
    kb::Cancel(g.serial);
    g.decided_at = SDL_GetTicks();
    input_off();
}
void bottom_back() {  // the touch screen's corner as the player had it
    if (g_showed_bottom && ui_bottom_wanted()) ui_toggle_bottom();
    g_showed_bottom = false;
}
void put_away() {  // the game's own keys: on the touch screen, shown if it was hidden (the single-screen mod)
    g.hidden = true;
    input_off();
    if (!ui_bottom_wanted()) {
        ui_toggle_bottom();
        g_showed_bottom = true;
    }
}
void bring_back() {
    g.hidden = false;
    bottom_back();
    input_on(true);
}

void begin(const kb::Request& r) {
    g = Session{};
    g.active = true;
    g.serial = r.serial;
    g.req = r;
    g.text = g.sent = r.text;
    g.game_serial = r.text_serial;
    std::fprintf(stderr, "[Info] App: the game's keyboard: the phone's keyboard is up (up to %u characters%s)\n",
                 (unsigned)r.max_length, r.numeric ? ", numbers" : "");
    input_on(true);
}
void end() {
    input_off();
    bottom_back();
    g = Session{};
    g_pressed = NONE;
}

Part part_at(float x, float y) {
    for (auto it = g_hits.rbegin(); it != g_hits.rend(); ++it)
        if (it->r.Contains(ImVec2(x, y))) return it->part;
    return NONE;
}
void tap(Part p) {
    switch (p) {
    case FIELD: input_on(true); break;  // (the phone's keyboard back, if the player put it away)
    case CLEAR: g.text.clear(), g.composing.clear(), hand_over(); break;
    case GAME_KEYS: put_away(); break;
    case CANCEL: cancel(); break;
    case OK: ok(); break;
    case TAB: bring_back(); break;
    default: break;
    }
}

// ---- the look: the settings' Hunter look (settings_panel.cpp), a wine-dark panel with the game's red and green rules
const ImU32 PANEL_INK = IM_COL32(50, 25, 24, 246), PLATE = IM_COL32(28, 20, 15, 240), EDGE = IM_COL32(112, 76, 34, 255),
            RULE = IM_COL32(222, 186, 96, 255), LIT = IM_COL32(92, 62, 22, 255), INK = IM_COL32(244, 232, 200, 255),
            DIM = IM_COL32(204, 186, 160, 255), GLOW = IM_COL32(255, 214, 92, 255), RED = IM_COL32(176, 34, 30, 255),
            GREEN = IM_COL32(80, 142, 54, 255), GOLD = IM_COL32(222, 190, 40, 255), WELL = IM_COL32(18, 12, 10, 240);
ImU32 fade(ImU32 c, float a) {
    return (c & 0x00ffffffu) | ((ImU32)std::clamp(((c >> 24) & 0xff) * a, 0.f, 255.f) << 24);
}
ImFont* title_font() {
    ImGuiIO& io = ImGui::GetIO();
    return io.Fonts->Fonts.Size > 1 ? io.Fonts->Fonts[1] : ImGui::GetFont();
}
void diamond(ImDrawList* dl, ImVec2 c, float d, ImU32 col) {
    ImVec2 q[4] = {ImVec2(c.x, c.y - d), ImVec2(c.x + d, c.y), ImVec2(c.x, c.y + d), ImVec2(c.x - d, c.y)};
    dl->AddConvexPolyFilled(q, 4, col);
}
void text_shadow(ImDrawList* dl, ImFont* f, float size, ImVec2 p, ImU32 col, const char* t) {
    const float o = std::max(1.f, size * .06f);
    dl->AddText(f, size, ImVec2(p.x + o, p.y + o), fade(IM_COL32(0, 0, 0, 170), ((col >> 24) & 0xff) / 255.f), t);
    dl->AddText(f, size, p, col, t);
}
// A finger's button, as the settings' plates: a dark plate, a bronze edge, a gold rule inside, studs on its corners
void plate(ImDrawList* dl, const ImRect& r, float line, bool held, bool enabled, bool main, const char* label, float size) {
    const float a = enabled ? 1.f : .45f, in = line * 1.8f;
    dl->AddRectFilled(ImVec2(r.Min.x, r.Min.y + line), ImVec2(r.Max.x, r.Max.y + line), fade(IM_COL32(0, 0, 0, 110), a));
    dl->AddRectFilled(r.Min, r.Max, fade(held ? LIT : main ? IM_COL32(74, 34, 31, 255) : PLATE, a));
    dl->AddRect(r.Min, r.Max, fade(EDGE, a), 0, line);
    dl->AddRect(ImVec2(r.Min.x + in, r.Min.y + in), ImVec2(r.Max.x - in, r.Max.y - in), fade(RULE, a * (main ? 1.f : .7f)), 0,
                std::max(1.f, line * (main ? .8f : .45f)));
    for (ImVec2 c : {r.Min, ImVec2(r.Max.x, r.Min.y), ImVec2(r.Min.x, r.Max.y), r.Max}) diamond(dl, c, line * 1.5f, fade(RULE, a));
    if (held) dl->AddRect(ImVec2(r.Min.x - line, r.Min.y - line), ImVec2(r.Max.x + line, r.Max.y + line), fade(GLOW, .7f), 0, line);
    const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0, label);
    text_shadow(dl, ImGui::GetFont(), size, ImVec2(r.GetCenter().x - ts.x * .5f, r.GetCenter().y - ts.y * .5f),
                fade(main ? GLOW : INK, a), label);
}
// The game's window frame: a red rule with a green one inside it along the top and the bottom, gold studs on the corners
void frame(ImDrawList* dl, const ImRect& r, float line) {
    dl->AddRectFilled(r.Min, ImVec2(r.Max.x, r.Min.y + line), RED);
    dl->AddRectFilled(ImVec2(r.Min.x, r.Min.y + line), ImVec2(r.Max.x, r.Min.y + line * 1.7f), GREEN);
    dl->AddRectFilled(ImVec2(r.Min.x, r.Max.y - line * 1.7f), ImVec2(r.Max.x, r.Max.y - line), GREEN);
    dl->AddRectFilled(ImVec2(r.Min.x, r.Max.y - line), r.Max, RED);
    const float c = line * 3.5f;
    for (ImVec2 k : {r.Min, ImVec2(r.Max.x, r.Min.y), ImVec2(r.Min.x, r.Max.y), r.Max}) {
        dl->AddRectFilled(ImVec2(k.x - c * .5f, k.y - c * .5f), ImVec2(k.x + c * .5f, k.y + c * .5f), IM_COL32(0, 0, 0, 255));
        dl->AddRect(ImVec2(k.x - c * .5f + 1, k.y - c * .5f + 1), ImVec2(k.x + c * .5f - 1, k.y + c * .5f - 1), GOLD, 0,
                    std::max(1.f, c * .16f));
        dl->AddRectFilled(ImVec2(k.x - c * .14f, k.y - c * .14f), ImVec2(k.x + c * .14f, k.y + c * .14f), GOLD);
    }
}

void draw_tab(ImDrawList* dl, float u) {
    const char* label = say("Phone keyboard", "Teclado del teléfono");
    const float size = 26 * u;
    const float w = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0, label).x + 60 * u;
    const float cx = g_w * .3f;  // (left of the middle, where the FPS counter is)
    ImRect r(ImVec2(cx - w * .5f, 10 * u), ImVec2(cx + w * .5f, 10 * u + 54 * u));
    plate(dl, r, 2.5f * u, g_pressed == TAB, true, true, label, size);
    g_hits.push_back({TAB, r});
}

void draw_panel(ImDrawList* dl, float u) {
    // two rows, short enough to stay above the phone's keyboard on a landscape screen (it takes the lower two thirds):
    // the title, the game's own keys and the count; the text with Cancel and OK beside it
    const float line = 2.5f * u;
    const float pw = std::min((float)g_w - 32 * u, 1200 * u), ph = 152 * u;
    const ImRect panel(ImVec2((g_w - pw) * .5f, 8 * u), ImVec2((g_w + pw) * .5f, 8 * u + ph));
    dl->AddRectFilled(ImVec2(panel.Min.x + 4 * u, panel.Min.y + 6 * u), ImVec2(panel.Max.x + 4 * u, panel.Max.y + 6 * u), IM_COL32(0, 0, 0, 90));
    dl->AddRectFilled(panel.Min, panel.Max, PANEL_INK);
    dl->AddRect(panel.Min, panel.Max, IM_COL32(0, 0, 0, 255), 0, line);
    frame(dl, panel, line * 1.4f);
    g_hits.push_back({PANEL, panel});
    ImFont* f = ImGui::GetFont();
    const float x0 = panel.Min.x + 24 * u, x1 = panel.Max.x - 24 * u;

    // the count of characters as the game counts them, at the right; the game's own keys beside it
    char count[32];
    std::snprintf(count, sizeof count, "%d / %u", (int)to16(to8(g.text) + g.composing).size(), (unsigned)g.req.max_length);
    const ImVec2 cs = f->CalcTextSizeA(24 * u, FLT_MAX, 0, count);
    text_shadow(dl, f, 24 * u, ImVec2(x1 - cs.x, panel.Min.y + 20 * u), DIM, count);
    const char* game_label = say("Game's keys", "Teclas del juego");
    const float gs = 22 * u, gw = f->CalcTextSizeA(gs, FLT_MAX, 0, game_label).x + 44 * u;
    const ImRect gr(ImVec2(x1 - cs.x - 22 * u - gw, panel.Min.y + 12 * u), ImVec2(x1 - cs.x - 22 * u, panel.Min.y + 52 * u));
    plate(dl, gr, line * .8f, g_pressed == GAME_KEYS, true, false, game_label, gs);
    g_hits.push_back({GAME_KEYS, gr});
    // the title: the game's hint, else what it is
    std::string title = g.req.hint.empty() ? say("Text for the game", "Texto para el juego") : to8(g.req.hint);
    if (g.req.numeric && g.req.hint.empty()) title = say("Numbers for the game", "Números para el juego");
    diamond(dl, ImVec2(x0 + 7 * u, panel.Min.y + 33 * u), 6 * u, RULE);
    dl->PushClipRect(ImVec2(x0, panel.Min.y), ImVec2(gr.Min.x - 12 * u, panel.Min.y + 60 * u), true);
    text_shadow(dl, title_font(), 29 * u, ImVec2(x0 + 22 * u, panel.Min.y + 15 * u), GLOW, title.c_str());
    dl->PopClipRect();

    // the game's Cancel and OK, at the right of the second row
    const float by0 = panel.Min.y + 64 * u, by1 = panel.Min.y + 136 * u, bs = 28 * u, gap = 14 * u;
    const char* ok_label = say("OK", "Aceptar");
    const char* cancel_label = say("Cancel", "Cancelar");
    const float okw = std::max(170 * u, f->CalcTextSizeA(bs, FLT_MAX, 0, ok_label).x + 50 * u);
    const float cw = std::max(170 * u, f->CalcTextSizeA(bs, FLT_MAX, 0, cancel_label).x + 50 * u);
    const ImRect okr(ImVec2(x1 - okw, by0), ImVec2(x1, by1));
    const bool can_ok = acceptable(to16(to8(g.text) + g.composing));
    plate(dl, okr, line, g_pressed == OK, can_ok, true, ok_label, bs);
    g_hits.push_back({OK, okr});
    float field_end = okr.Min.x - gap;
    if (g.req.buttons == 1 || g.req.buttons == 2) {
        const ImRect cr(ImVec2(okr.Min.x - gap - cw, by0), ImVec2(okr.Min.x - gap, by1));
        plate(dl, cr, line, g_pressed == CANCEL, true, false, cancel_label, bs);
        g_hits.push_back({CANCEL, cr});
        field_end = cr.Min.x - gap;
    }

    // the text, as the game's text box has it, the cursor at its end
    const ImRect field(ImVec2(x0, by0), ImVec2(field_end, by1));
    dl->AddRectFilled(field.Min, field.Max, WELL);
    dl->AddRect(field.Min, field.Max, EDGE, 0, line);
    dl->AddRect(ImVec2(field.Min.x + line * 1.8f, field.Min.y + line * 1.8f), ImVec2(field.Max.x - line * 1.8f, field.Max.y - line * 1.8f),
                fade(RULE, .5f), 0, std::max(1.f, line * .45f));
    g_hits.push_back({FIELD, field});
    const float size = 38 * u, clear_w = 60 * u;
    const ImRect clear(ImVec2(field.Max.x - clear_w, field.Min.y), field.Max);
    const float tx0 = field.Min.x + 18 * u, tx1 = clear.Min.x - 6 * u, ty = field.GetCenter().y - size * .5f;
    const std::string shown = to8(g.text), comp = g.composing;
    const float w_text = f->CalcTextSizeA(size, FLT_MAX, 0, shown.c_str()).x, w_comp = f->CalcTextSizeA(size, FLT_MAX, 0, comp.c_str()).x;
    const float shift = std::max(0.f, w_text + w_comp + 6 * u - (tx1 - tx0));  // (a long text: its end shows)
    dl->PushClipRect(ImVec2(tx0 - 2 * u, field.Min.y), ImVec2(tx1, field.Max.y), true);
    if (shown.empty() && comp.empty())
        dl->AddText(f, size * .72f, ImVec2(tx0, field.GetCenter().y - size * .36f), fade(DIM, .55f),
                    say("Type with the phone's keyboard", "Escribe con el teclado del teléfono"));
    dl->AddText(f, size, ImVec2(tx0 - shift, ty), INK, shown.c_str());
    float cx = tx0 - shift + w_text;
    if (!comp.empty()) {  // (the phone keyboard's word in progress, underlined)
        dl->AddText(f, size, ImVec2(cx, ty), GLOW, comp.c_str());
        dl->AddLine(ImVec2(cx, ty + size * 1.02f), ImVec2(cx + w_comp, ty + size * 1.02f), GLOW, std::max(1.f, 2 * u));
        cx += w_comp;
    }
    if ((SDL_GetTicks() / 530) % 2 == 0) dl->AddRectFilled(ImVec2(cx + 2 * u, ty + 2 * u), ImVec2(cx + 5 * u, ty + size), GLOW);
    dl->PopClipRect();
    if (!g.text.empty()) {  // (clear: a cross at the field's end)
        const ImVec2 c = clear.GetCenter();
        const float r = 12 * u;
        const ImU32 col = g_pressed == CLEAR ? GLOW : DIM;
        dl->AddCircleFilled(c, r * 1.6f, g_pressed == CLEAR ? LIT : IM_COL32(40, 28, 22, 255));
        dl->AddLine(ImVec2(c.x - r * .6f, c.y - r * .6f), ImVec2(c.x + r * .6f, c.y + r * .6f), col, 3 * u);
        dl->AddLine(ImVec2(c.x - r * .6f, c.y + r * .6f), ImVec2(c.x + r * .6f, c.y - r * .6f), col, 3 * u);
        g_hits.push_back({CLEAR, clear});
    }
}

}  // namespace

bool game_keyboard_event(const SDL_Event& e) {
    if (!g.active || g.decided_at) return false;
    switch (e.type) {
    case SDL_EVENT_FINGER_DOWN: {
        const Part p = part_at(e.tfinger.x * g_w, e.tfinger.y * g_h);
        if (p == NONE) return false;
        g_pressed = p, g_finger = e.tfinger.fingerID;
        return true;
    }
    case SDL_EVENT_FINGER_MOTION:
        return g_pressed != NONE && e.tfinger.fingerID == g_finger;
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED: {
        if (g_pressed == NONE || e.tfinger.fingerID != g_finger) return false;
        const Part was = g_pressed;
        g_pressed = NONE;
        if (e.type == SDL_EVENT_FINGER_UP && part_at(e.tfinger.x * g_w, e.tfinger.y * g_h) == was) tap(was);
        return true;
    }
    default: break;
    }
    if (!showing()) return false;
    switch (e.type) {
    case SDL_EVENT_TEXT_INPUT:
        g.composing.clear();
        type(e.text.text);
        hand_over();
        return true;
    case SDL_EVENT_TEXT_EDITING:
        g.composing = e.edit.text ? e.edit.text : "";
        return true;
    case SDL_EVENT_KEY_DOWN:
        switch (e.key.scancode) {
        case SDL_SCANCODE_BACKSPACE:
            if (!g.text.empty()) g.text.pop_back(), hand_over();
            return true;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
            if (!e.key.repeat) ok();
            return true;
        case SDL_SCANCODE_AC_BACK:
        case SDL_SCANCODE_ESCAPE:
            if (!e.key.repeat) put_away();
            return true;
        case SDL_SCANCODE_VOLUMEUP:
        case SDL_SCANCODE_VOLUMEDOWN:
        case SDL_SCANCODE_MUTE:
            return false;
        default:
            return true;  // (a physical keyboard's letters are text here, not the game's buttons)
        }
    default:
        return false;
    }
}

void game_keyboard_frame(int width, int height, bool covered) {
    g_w = width, g_h = height;
    g_hits.clear();
    const kb::Request r = kb::Current();
    if (!r.open) {
        if (g.active) end();
        return;
    }
    if (!g.active || r.serial != g.serial) begin(r);
    g.req.text = r.text;
    if (r.text_serial != g.game_serial) {  // (the game's own keys typed, or the game changed its text)
        g.game_serial = r.text_serial;
        g.text = g.sent = r.text;  // (the game's own keys typed: Port::Keyboard counts only the game's changes)
    }
    if (g.decided_at) {
        if (SDL_GetTicks() - g.decided_at < 1500) return;  // (the game closes its keyboard)
        g.decided_at = 0;  // (still open: the game turned the text down, and says why on its screen)
        g.hidden = false;
    }
    if (covered) {  // (the settings: the phone's keyboard is theirs)
        input_off();
        g_pressed = NONE;
        return;
    }
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float u = std::min(width, height) / 720.f;
    if (g.hidden) {
        input_off();
        draw_tab(dl, u);
        return;
    }
    // text input on while the panel shows; the phone's keyboard put away by the player (Back) leaves it on for a
    // physical keyboard, and a tap on the text brings it back
    SDL_Window* w = window();
    if (!g_input_on || (w && !SDL_TextInputActive(w))) input_on(!g_input_on);
    draw_panel(dl, u);
}
