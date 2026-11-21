// MH4U for PC: the PC's own controls (pc_input.h).
#include "pc_input.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <span>
#include <unordered_map>
#include <vector>

#include "app_settings.h"
#include "controls.h"
#include "hooks.h"
#include "pc_net.h"
#include "imgui.h"
#include "quick_items.h"
#include "settings_panel.h"
#include "town.h"
#include "ui.h"

namespace {

bool g_help = false;
Uint64 g_tap_until = 0;  // a key's tap on the touch screen: held until then (SDL_GetTicks), at g_tap_x, g_tap_y
int g_tap_x = 0, g_tap_y = 0;
constexpr Uint64 kTapMs = 150;  // (some of the game's frames, at any frame rate it holds)

const char* L(const char* en, const char* es) { return settings_spanish() ? es : en; }

void help_set(bool on, const char* why) {
    if (on == g_help) return;
    g_help = on;
    std::fprintf(stderr, "[input] the list of the controls %s (%s)\n", on ? "shown" : "hidden", why);
}

// ---- the keys' names as the list shows them: the keyboard's own words, the mouse's in the player's language, one
// name for the left and the right Shift, Ctrl or Alt
std::string key_label(int code) {
    switch (code) {
    case kKeyMouse + SDL_BUTTON_LEFT: return L("Left click", "Clic izquierdo");
    case kKeyMouse + SDL_BUTTON_RIGHT: return L("Right click", "Clic derecho");
    case kKeyMouse + SDL_BUTTON_MIDDLE: return L("Middle click", "Clic central");
    case kKeyMouse + SDL_BUTTON_X1: return L("Mouse button 4", "Botón 4 del ratón");
    case kKeyMouse + SDL_BUTTON_X2: return L("Mouse button 5", "Botón 5 del ratón");
    case kKeyWheelUp: return L("Wheel up", "Rueda arriba");
    case kKeyWheelDown: return L("Wheel down", "Rueda abajo");
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_RSHIFT: return "Shift";
    case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_RCTRL: return "Ctrl";
    case SDL_SCANCODE_LALT:
    case SDL_SCANCODE_RALT: return "Alt";
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: return "Enter";
    case SDL_SCANCODE_ESCAPE: return "Esc";
    case SDL_SCANCODE_SPACE: return L("Space", "Espacio");
    case SDL_SCANCODE_BACKSPACE: return L("Backspace", "Retroceso");
    case SDL_SCANCODE_UP: return "\xE2\x86\x91";
    case SDL_SCANCODE_DOWN: return "\xE2\x86\x93";
    case SDL_SCANCODE_LEFT: return "\xE2\x86\x90";
    case SDL_SCANCODE_RIGHT: return "\xE2\x86\x92";
    default: return controls_key_name(code);
    }
}
// A keybind's keys ("" none), each name once
std::string keys_of(const Keybinds& b, int bind, const char* sep = ", ") {
    std::vector<std::string> names;
    for (int k = 0; k < kBindKeys; k++) {
        const int code = b.keys[bind][k];
        if (!code) continue;
        std::string n = key_label(code);
        if (code == kKeyMouse + SDL_BUTTON_RIGHT && bind == BIND_A) n += L(" (weapon out)", " (arma afuera)");
        if (!n.empty() && std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
    }
    std::string t;
    for (const std::string& n : names) t += (t.empty() ? "" : sep) + n;
    return t;
}
// A stick's four keybinds as one: "W A S D" (each one's first key), "" when none has a key
std::string four_of(const Keybinds& b, int up, int left, int down, int right) {
    if (b.keys[up][0] == SDL_SCANCODE_UP && b.keys[left][0] == SDL_SCANCODE_LEFT && b.keys[down][0] == SDL_SCANCODE_DOWN &&
        b.keys[right][0] == SDL_SCANCODE_RIGHT)
        return L("The arrows", "Las flechas");
    std::string t;
    bool any = false;
    for (int bind : {up, left, down, right}) {
        const std::string k = b.keys[bind][0] ? key_label(b.keys[bind][0]) : "-";
        any |= b.keys[bind][0] != 0;
        t += (t.empty() ? "" : " ") + k;
    }
    return any ? t : "";
}

// ---- the list of the controls
struct Line {
    int kind = 0;  // 0 a row (label, keys), 1 a section's title, 2 a note across the column
    std::string label, keys;
};
std::vector<Line> keyboard_lines() {
    const Keybinds b = controls_read_binds();
    std::vector<Line> v;
    auto row = [&v](const std::string& label, const std::string& keys) {
        if (!keys.empty()) v.push_back({0, label, keys});
    };
    v.push_back({1, L("Keyboard and mouse", "Teclado y ratón"), ""});
    row(L("Move", "Moverse"), four_of(b, BIND_MOVE_UP, BIND_MOVE_LEFT, BIND_MOVE_DOWN, BIND_MOVE_RIGHT));
    row(L("Walk", "Caminar"), keys_of(b, BIND_WALK));
    {
        std::string cam = L("Mouse", "Ratón");
        const std::string four = four_of(b, BIND_CAM_UP, BIND_CAM_LEFT, BIND_CAM_DOWN, BIND_CAM_RIGHT);
        if (!four.empty()) cam += ", " + four;
        row(L("Camera", "Cámara"), cam);
    }
    row(L("X: draw, attack", "X: desenfundar, atacar"), keys_of(b, BIND_X));
    row(L("A: talk, gather, attack", "A: hablar, recolectar, atacar"), keys_of(b, BIND_A));
    row(L("B: evade", "B: esquivar"), keys_of(b, BIND_B));
    row(L("Y: sheathe, use an item", "Y: enfundar, usar un objeto"), keys_of(b, BIND_Y));
    row(L("R: run, guard", "R: correr, cubrirse"), keys_of(b, BIND_R));
    row(L("L: camera behind the hunter", "L: cámara detrás del cazador"), keys_of(b, BIND_L));
    row(L("ZL: target camera", "ZL: cámara de objetivo"), keys_of(b, BIND_ZL));
    row(L("ZR: kick", "ZR: patada"), keys_of(b, BIND_ZR));
    row(L("START: the game's menu", "START: el menú del juego"), keys_of(b, BIND_START));
    row(L("SELECT: town actions", "SELECT: acciones del pueblo"), keys_of(b, BIND_SELECT));
    row(L("D-pad", "Cruceta"), four_of(b, BIND_DPAD_UP, BIND_DPAD_LEFT, BIND_DPAD_DOWN, BIND_DPAD_RIGHT));
    {
        std::string bar = keys_of(b, BIND_ITEM_LEFT, " / ");
        const std::string right = keys_of(b, BIND_ITEM_RIGHT, " / ");
        if (bar == L("Wheel up", "Rueda arriba") && right == L("Wheel down", "Rueda abajo")) bar = L("Wheel", "Rueda");
        else if (!right.empty()) bar += (bar.empty() ? "" : " | ") + right;
        row(L("Item bar", "Barra de objetos"), bar);
    }
    const std::string r3 = app_setting("Controls", "R3", "lockon");
    row(!SDL_strcasecmp(r3.c_str(), "lockon") ? L("Lock-on", "Fijar al monstruo") : L("Right stick click", "Clic del stick derecho"),
        keys_of(b, BIND_RSTICK));
    // (1.0.2: in the Smithy, with a weapon shown, the right stick's click opens its weapon tree: weapon_tree_ui.h)
    row(L("Weapon tree (in the Smithy)", "Árbol de armas (en la herrería)"), keys_of(b, BIND_RSTICK));
    row(L("Touch screen: show, hide", "Pantalla táctil: mostrar, ocultar"), keys_of(b, BIND_LSTICK));
    {
        std::string items;
        for (int i = BIND_SCREEN_ITEM1; i <= BIND_SCREEN_ITEM4; i++)
            if (b.keys[i][0]) items += (items.empty() ? "" : " ") + key_label(b.keys[i][0]);
        row(L("Item shortcuts (touch screen)", "Atajos de objetos (pantalla táctil)"), items);
    }
    row(L("Guild Cards", "Tarjetas de gremio"), keys_of(b, BIND_TOWN_CARDS));
    row(L("Gestures", "Gestos"), keys_of(b, BIND_TOWN_GESTURES));
    row(L("Friends", "Amigos"), keys_of(b, BIND_TOWN_FRIENDS));
    row(L("Hunters for Hire", "Cazadores a sueldo"), keys_of(b, BIND_TOWN_HIRE));
    for (int k = 0; k < kCombos; k++) {
        const std::string keys = keys_of(b, BIND_COMBO1 + k);
        if (keys.empty()) continue;
        char t[64];
        std::snprintf(t, sizeof t, L("Combined button %d", "Botón combinado %d"), k + 1);
        row(t, keys);
    }
    {  // (the chat's key: [Controls] ChatKey, pc_net.h)
        const std::string chat = app_setting("Controls", "ChatKey", "/");
        if (!chat.empty()) row(L("Chat", "Chat"), pc_chat_key_label(chat, settings_spanish()));
    }
    row(L("Settings", "Opciones"), "F1");
    row(L("Full screen", "Pantalla completa"), "F11, Alt+Enter");
    row(L("This list", "Esta lista"), keys_of(b, BIND_HELP));
    return v;
}
std::vector<Line> other_lines() {
    std::vector<Line> v;
    auto row = [&v](const std::string& label, const std::string& keys) { v.push_back({0, label, keys}); };
    v.push_back({1, L("Controller", "Mando"), ""});
    row(L("Settings", "Opciones"), "START + SELECT");
    row(L("Camera", "Cámara"), L("Right stick", "Stick derecho"));
    const std::string r3 = app_setting("Controls", "R3", "lockon");
    if (!SDL_strcasecmp(r3.c_str(), "lockon")) row(L("Lock-on", "Fijar al monstruo"), L("Right stick click", "Clic del stick derecho"));
    row(L("Weapon tree (in the Smithy)", "Árbol de armas (en la herrería)"), L("Right stick click", "Clic del stick derecho"));
    row(L("Touch screen: show, hide", "Pantalla táctil: mostrar, ocultar"), L("Left stick click", "Clic del stick izquierdo"));
    row(L("Town actions", "Acciones del pueblo"), "SELECT");
    row(L("The game's A", "La A del juego"), !SDL_strcasecmp(app_setting("Controls", "FaceButtons", "position").c_str(), "label")
                                                 ? L("The button marked A", "El botón marcado A")
                                                 : L("The right button, as on the 3DS", "El botón de la derecha, como en la 3DS"));
    v.push_back({2, L("A controller the game reads wrong: Settings, Controls, Controller, Map a controller.",
                      "Un mando que el juego lee mal: Opciones, Controles, Mando, Mapear un mando."), ""});
    v.push_back({1, L("The mouse", "El ratón"), ""});
    row(L("A click on the game", "Un clic en el juego"), L("the mouse turns the camera", "el ratón gira la cámara"));
    row(L("Esc", "Esc"), L("lets the mouse go", "suelta el ratón"));
    row(L("A click on the touch screen", "Un clic en la pantalla táctil"), L("a tap there", "un toque ahí"));
    row(L("A click on the quick items", "Un clic en los objetos rápidos"), L("uses that item", "usa ese objeto"));
    v.push_back({1, L("Your own", "A tu gusto"), ""});
    v.push_back({2, L("Any key: Settings, Controls, Keyboard and mouse, Keybinds.",
                      "Cualquier tecla: Opciones, Controles, Teclado y ratón, Asignación de teclas."), ""});
    v.push_back({2, L("The HUD: Settings, Controls, Customize controls and HUD: drag a part, the wheel sizes it.",
                      "El HUD: Opciones, Controles, Personalizar controles y HUD: arrastra una parte, la rueda cambia su tamaño."),
                 ""});
    return v;
}

const ImU32 kPlate = IM_COL32(50, 25, 24, 246), kBand = IM_COL32(74, 34, 31, 255), kEdge = IM_COL32(112, 76, 34, 255),
            kGold = IM_COL32(222, 186, 96, 255), kGlow = IM_COL32(255, 214, 92, 255), kInk = IM_COL32(244, 232, 200, 255),
            kDim = IM_COL32(204, 186, 160, 255), kRed = IM_COL32(176, 34, 30, 255), kGreen = IM_COL32(80, 142, 54, 255);

// One column's lines from (x, y), `w` wide: their height (nothing drawn with dl null)
float column(ImDrawList* dl, ImFont* font, float fs, float x, float y, float w, const std::vector<Line>& lines) {
    const float row_h = fs * 1.34f, label_w = w * .52f;
    float cy = y;
    for (const Line& l : lines) {
        if (l.kind == 1) {
            if (cy > y) cy += fs * .45f;
            if (dl) {
                dl->AddText(font, fs * 1.08f, ImVec2(x, cy), kGlow, l.label.c_str());
                const float ty = cy + fs * 1.25f;
                dl->AddLine(ImVec2(x, ty), ImVec2(x + w, ty), (kGold & 0x00ffffffu) | 0x90000000u, std::max(1.f, fs * .06f));
            }
            cy += fs * 1.5f;
            continue;
        }
        if (l.kind == 2) {
            const float h = font->CalcTextSizeA(fs * .92f, FLT_MAX, w, l.label.c_str()).y;
            if (dl) dl->AddText(font, fs * .92f, ImVec2(x, cy), kDim, l.label.c_str(), nullptr, w);
            cy += h + fs * .34f;
            continue;
        }
        if (dl) {
            dl->AddText(font, fs, ImVec2(x, cy), kDim, l.label.c_str(), nullptr, label_w - fs * .4f);
            dl->AddText(font, fs, ImVec2(x + label_w, cy), kInk, l.keys.c_str(), nullptr, w - label_w);
        }
        const float lh = std::max(font->CalcTextSizeA(fs, FLT_MAX, label_w - fs * .4f, l.label.c_str()).y,
                                  font->CalcTextSizeA(fs, FLT_MAX, w - label_w, l.keys.c_str()).y);
        cy += std::max(row_h, lh + fs * .34f);
    }
    return cy - y;
}

void draw_help() {
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    if (W <= 0 || H <= 0) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    ImFont* title = io.Fonts->Fonts.Size > 1 ? io.Fonts->Fonts[1] : font;
    const std::vector<Line> left = keyboard_lines(), right = other_lines();
    const float pw = std::min(W * .94f, H * 1.9f), pad = H * .025f, gap = pad * 1.6f;
    const float cw = (pw - pad * 2 - gap) * .5f;
    // (the letters' size: the screen's, smaller until both columns fit in it)
    float fs = std::clamp(H * .024f, 11.f, 30.f), band = 0, body = 0;
    for (int tries = 0; tries < 12; tries++) {
        band = fs * 2.6f;
        body = std::max(column(nullptr, font, fs, 0, 0, cw, left), column(nullptr, font, fs, 0, 0, cw, right));
        if (band + body + fs * 2.6f + pad * 2 <= H * .96f) break;
        fs *= .92f;
    }
    const float ph = band + pad + body + fs * 2.2f + pad;
    const float x0 = (W - pw) * .5f, y0 = std::max(0.f, (H - ph) * .5f);
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 0, 150));
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + pw, y0 + ph), kPlate);
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + pw, y0 + band), kBand);
    const float line = std::max(2.f, H * .0035f);
    dl->AddRectFilled(ImVec2(x0, y0 + band), ImVec2(x0 + pw, y0 + band + line), kRed);
    dl->AddRectFilled(ImVec2(x0, y0 + band + line), ImVec2(x0 + pw, y0 + band + line * 1.7f), kGreen);
    dl->AddRect(ImVec2(x0, y0), ImVec2(x0 + pw, y0 + ph), kEdge, 0, line);
    for (ImVec2 c : {ImVec2(x0, y0), ImVec2(x0 + pw, y0), ImVec2(x0, y0 + ph), ImVec2(x0 + pw, y0 + ph)}) {
        const float d = line * 2.2f;
        ImVec2 q[4] = {ImVec2(c.x, c.y - d), ImVec2(c.x + d, c.y), ImVec2(c.x, c.y + d), ImVec2(c.x - d, c.y)};
        dl->AddConvexPolyFilled(q, 4, kGold);
    }
    {
        const char* t = L("Controls", "Controles");
        const float ts = fs * 1.6f;
        const ImVec2 sz = title->CalcTextSizeA(ts, FLT_MAX, 0, t);
        dl->AddText(title, ts, ImVec2(x0 + (pw - sz.x) * .5f + 1, y0 + (band - sz.y) * .5f + 1), IM_COL32(0, 0, 0, 170), t);
        dl->AddText(title, ts, ImVec2(x0 + (pw - sz.x) * .5f, y0 + (band - sz.y) * .5f), kGlow, t);
    }
    const float by = y0 + band + pad;
    column(dl, font, fs, x0 + pad, by, cw, left);
    dl->AddLine(ImVec2(x0 + pad + cw + gap * .5f, by), ImVec2(x0 + pad + cw + gap * .5f, by + body), (kEdge & 0x00ffffffu) | 0xa0000000u,
                std::max(1.f, line * .5f));
    column(dl, font, fs, x0 + pad + cw + gap, by, cw, right);
    {
        const char* t = L("F2, Esc or a click closes this list. The game waits meanwhile.",
                          "F2, Esc o un clic cierra esta lista. El juego espera mientras tanto.");
        const ImVec2 sz = font->CalcTextSizeA(fs * .9f, FLT_MAX, 0, t);
        dl->AddText(font, fs * .9f, ImVec2(x0 + (pw - sz.x) * .5f, y0 + ph - pad * .6f - sz.y), kDim, t);
    }
}

}  // namespace

void pc_bind_pressed(int bind) {
    if (bind == BIND_HELP) {
        help_set(!g_help, "its key");
        return;
    }
    if (bind >= BIND_SCREEN_ITEM1 && bind <= BIND_SCREEN_ITEM4) {
        const Port::Hooks::GameState g = Port::Hooks::CurrentGameState();
        const int i = bind - BIND_SCREEN_ITEM1, cells = g.touch_items_wide ? 4 : 2;
        if (g.touch_items_slot < 0 || i >= cells) {
            std::fprintf(stderr, "[input] item shortcut %d: no item shortcuts panel on the touch screen now\n", i + 1);
            return;
        }
        float crop[4];
        quick_items_crop(g.touch_items_slot, g.touch_items_wide != 0, crop);
        g_tap_x = std::clamp(static_cast<int>((crop[0] + (i + .5f) / cells * (crop[2] - crop[0])) * 320), 0, 319);
        g_tap_y = std::clamp(static_cast<int>((crop[1] + crop[3]) * .5f * 240), 0, 239);
        g_tap_until = SDL_GetTicks() + kTapMs;
        std::fprintf(stderr, "[input] item shortcut %d: a tap at %d, %d (panel slot %d%s)\n", i + 1, g_tap_x, g_tap_y,
                     g.touch_items_slot, g.touch_items_wide ? ", wide" : "");
        return;
    }
    if (bind >= BIND_TOWN_CARDS && bind <= BIND_TOWN_HIRE) {
        const int a = bind - BIND_TOWN_CARDS;
        if (!town_pick_now(a)) std::fprintf(stderr, "[input] the town's action %d: not now (not in a town, or a menu is up)\n", a);
    }
}

bool pc_input_event(const SDL_Event& e) {
    if (!g_help) return false;
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
        if (e.key.scancode == SDL_SCANCODE_ESCAPE && !e.key.repeat) help_set(false, "Esc");
        return true;  // (the game and the touch controls get no key meanwhile; its own key hides it: controls.cpp)
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_FINGER_DOWN:
        help_set(false, "a click");
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if (e.gbutton.button != SDL_GAMEPAD_BUTTON_GUIDE) help_set(false, "a controller's button");
        return true;
    default:
        return false;
    }
}

void pc_input_frame(int, int) {
    // (the first starts: where the settings and this list are, for a few seconds)
    static bool said = false;
    if (!said) {
        said = true;
        const int seen = app_setting_int("Interface", "PcHintsShown", 0);
        if (seen < 3) {
            app_setting_set_int("Interface", "PcHintsShown", seen + 1);
            app_settings_save();
            settings_panel_notice("F1: the settings. F2: the list of the controls. A click on the game: the mouse turns the camera (Esc lets it go).",
                                  "F1: las opciones. F2: la lista de los controles. Un clic en el juego: el ratón gira la cámara (Esc lo suelta).",
                                  10);
        }
    }
    if (g_help && ui_settings_open()) help_set(false, "the settings");
    if (g_help) draw_help();
}

bool pc_help_open() { return g_help; }

void pc_help_show() { help_set(true, "the settings"); }

int pc_key_touch(int* x, int* y) {
    if (SDL_GetTicks() >= g_tap_until) return 0;
    *x = g_tap_x, *y = g_tap_y;
    return 1;
}

// ---- the controller's buttons and the keyboard's keys, drawn (pc_input.h)
int pc_icon_look(int setting) {
    if (setting > 0) return std::min(setting, 4);
    switch (SDL_GetGamepadTypeForID(controls_active_pad())) {
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5: return 2;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return 3;
    default: return 1;
    }
}

float pc_face_glyph(ImDrawList* dl, ImFont* font, ImVec2 p, float size, int button, int look, const SettingsIcons::Ink& ink) {
    if (look == 4) {
        SettingsIcons::draw(dl, font, p, size, button, ink);
        return size;
    }
    // (its place on the controller in use: 0 south, 1 east, 2 west, 3 north - by place A on the right as on the 3DS, by
    // label where the letters put it)
    static const int by_place[4] = {1, 0, 3, 2}, by_label[4] = {0, 1, 2, 3};
    const int place = (controls_a_is_south() ? by_label : by_place)[button & 3];
    const float line = size > 24 ? 2.0f : 1.5f;
    const ImVec2 c(p.x + size * .5f, p.y + size * .5f);
    dl->AddCircleFilled(c, size * .48f, ink.plate);
    dl->AddCircle(c, size * .48f, ink.rim, 0, line);
    dl->AddCircle(c, size * .48f - line * 1.6f, (ink.rule & 0x00ffffffu) | 0x80000000u, 0, line * .6f);
    if (look == 2) {  // (PlayStation: its shapes, in the rule's gold)
        const float r = size * .2f, t = std::max(1.5f, line * .9f);
        if (place == 0) {
            dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ink.rule, t);
            dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), ink.rule, t);
        } else if (place == 1) {
            dl->AddCircle(c, r, ink.rule, 0, t);
        } else if (place == 2) {
            dl->AddRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ink.rule, 0, t);
        } else {
            dl->AddTriangle(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y + r * .8f), ImVec2(c.x - r, c.y + r * .8f), ink.rule, t);
        }
        return size;
    }
    static const char* const xbox[4] = {"A", "B", "X", "Y"}, *const bayx[4] = {"B", "A", "Y", "X"};
    const char* name = (look == 3 ? bayx : xbox)[place];
    const float text = size * .62f;
    const ImVec2 ts = font->CalcTextSizeA(text, FLT_MAX, 0, name);
    dl->AddText(font, text, ImVec2(c.x - ts.x * .5f + 1, c.y - ts.y * .5f + 1), IM_COL32(0, 0, 0, 170), name);
    dl->AddText(font, text, ImVec2(c.x - ts.x * .5f, c.y - ts.y * .5f), ink.letter, name);
    return size;
}

float pc_key_cap(ImDrawList* dl, ImFont* font, ImVec2 p, float size, const char* name, const SettingsIcons::Ink& ink) {
    const float text = size * .56f, line = size > 24 ? 2.0f : 1.5f;
    const ImVec2 ts = font->CalcTextSizeA(text, FLT_MAX, 0, name);
    const float w = std::max(size, ts.x + size * .55f);
    if (dl) {
        const ImVec2 a(p.x, p.y + size * .08f), b(p.x + w, p.y + size * .92f);
        dl->AddRectFilled(ImVec2(a.x, a.y + line), ImVec2(b.x, b.y + line), IM_COL32(0, 0, 0, 110), size * .14f);
        dl->AddRectFilled(a, b, ink.plate, size * .14f);
        dl->AddRect(a, b, ink.rim, size * .14f, line);
        dl->AddText(font, text, ImVec2((a.x + b.x - ts.x) * .5f, (a.y + b.y - ts.y) * .5f), ink.letter, name);
    }
    return w;
}

bool pc_glyph(ImDrawList* dl, ImFont* font, ImVec2 c, float size, int glyph, const SettingsIcons::Ink& ink) {
    if (controls_last_device() == 2 && controls_real_pad()) {  // (a controller: its own look, the 3DS's letters as before)
        static unsigned read = ~0u;
        static int setting = 0;
        if (read != app_settings_version()) {
            read = app_settings_version();
            static const char* const keys[] = {"auto", "xbox", "playstation", "bayx", "game"};
            const std::string v = app_setting("Controls", "ButtonIcons", "auto");
            setting = 0;
            for (int i = 0; i < 5; i++)
                if (!SDL_strcasecmp(v.c_str(), keys[i])) setting = i;
        }
        const int look = pc_icon_look(setting);
        if (look == 4 || glyph > SettingsIcons::Y) return false;
        pc_face_glyph(dl, font, ImVec2(c.x - size * .5f, c.y - size * .5f), size, glyph, look, ink);
        return true;
    }
    // (the keyboard or the mouse: the first key of that button's keybind)
    const Keybinds b = controls_read_binds();
    std::string name;
    if (glyph == SettingsIcons::Dpad) {
        name = b.keys[BIND_DPAD_UP][0] == SDL_SCANCODE_UP ? "\xE2\x86\x91\xE2\x86\x93" : key_label(b.keys[BIND_MOVE_UP][0]);
    } else {
        static const int binds[] = {BIND_A, BIND_B, BIND_X, BIND_Y, BIND_L, BIND_R};
        const int bind = binds[std::clamp(glyph, 0, 5)];
        // (a key's, not the mouse's - the mouse's A is the weapon's -, the shortest name, the later of two as short: F of
        // O and F, Esc of Space and Esc, as they fit the glyph's place)
        for (int k = 0; k < kBindKeys; k++)
            if (b.keys[bind][k] > 0 && b.keys[bind][k] < SDL_SCANCODE_COUNT) {
                const std::string n = key_label(b.keys[bind][k]);
                if (name.empty() || n.size() <= name.size()) name = n;
            }
    }
    if (name.empty()) return false;
    float w = pc_key_cap(nullptr, font, ImVec2(0, 0), size, name.c_str(), ink);
    if (w > size * 1.4f) size *= size * 1.4f / w, w = pc_key_cap(nullptr, font, ImVec2(0, 0), size, name.c_str(), ink);
    pc_key_cap(dl, font, ImVec2(c.x - w * .5f, c.y - size * .5f), size, name.c_str(), ink);
    return true;
}

// ---- the settings' words for a PC (pc_input.h): the phone said as the PC, its Wi-Fi as the network, a tap as a click
namespace {
struct Swap {
    const char *from, *to;
};
// (the longest first: "the phone's keyboard" before "the phone's" before "phone"), each language's own: a Spanish line
// takes the Spanish ones ("esta Wi-Fi" is "esta red", never the English "Wi-Fi" -> "network" inside it)
const Swap kSwapsEs[] = {
    {"teclado del teléfono", "teclado"}, {"Teclado del teléfono", "Teclado"}, {"en este teléfono", "en esta PC"},
    {"este teléfono", "esta PC"}, {"Este teléfono", "Esta PC"}, {"tu teléfono", "tu PC"}, {"Tu teléfono", "Tu PC"},
    {"Del teléfono", "Del sistema"}, {"del teléfono", "de la PC"}, {"al teléfono", "a la PC"}, {"El teléfono", "La PC"},
    {"el teléfono", "la PC"}, {"un teléfono", "una PC"}, {"Un teléfono", "Una PC"}, {"teléfonos", "PC"},
    {"teléfono", "PC"}, {"esta Wi-Fi", "esta red"}, {"la Wi-Fi", "la red"}, {"ninguna Wi-Fi", "ninguna red"},
    {"misma Wi-Fi", "misma red"}, {"Wi-Fi", "red"}, {"Un toque", "Un clic"}, {"un toque", "un clic"},
    {"toques", "clics"}, {"toque", "clic"}, {"Toca ", "Haz clic en "}, {" toca ", " haz clic en "},
};
const Swap kSwapsEn[] = {
    {"the phone's own keyboard", "the keyboard"}, {"the phone's keyboard", "the keyboard"}, {"phone's keyboard", "keyboard"},
    {"Phone keyboard", "Keyboard"}, {"on this phone", "on this PC"}, {"this phone's", "this PC's"}, {"This phone", "This PC"},
    {"this phone", "this PC"}, {"your phone's", "your PC's"}, {"your phone", "your PC"}, {"Your phone", "Your PC"},
    {"The phone's", "The PC's"}, {"the phone's", "the PC's"}, {"The phone", "The PC"}, {"the phone", "the PC"},
    {"a phone's", "a PC's"}, {"A phone", "A PC"}, {"a phone", "a PC"}, {"weakest phones", "weakest PCs"},
    {"weak phones", "weak PCs"}, {"phones", "PCs"}, {"Phone's", "System's"}, {"phone's", "PC's"}, {"phone", "PC"},
    {"Phone", "PC"}, {"this Wi-Fi", "this network"}, {"the host's Wi-Fi", "the host's network"}, {"no Wi-Fi", "no network"},
    {"same Wi-Fi", "same network"}, {"Wi-Fi", "network"}, {"A tap", "A click"}, {"a tap", "a click"}, {"Tapping", "Clicking"},
    {"tapping", "clicking"}, {"Tapped", "Clicked"}, {"tapped", "clicked"}, {"Tap ", "Click "}, {"Taps", "Clicks"},
    {"taps", "clicks"}, {" tap ", " click "}, {" tap.", " click."}, {" tap,", " click,"}, {" tap:", " click:"},
    {" tap)", " click)"},
};
bool has_words(const char* t) {
    static const char* const kTriggers[] = {"phone", "Phone", "Wi-Fi", "tap", "Tap", "teléfono", "toque", "Toca", " toca "};
    for (const char* k : kTriggers)
        if (std::strstr(t, k) != nullptr) return true;
    return false;
}
std::string swapped(std::string s, bool es) {
    for (const Swap& w : es ? std::span<const Swap>(kSwapsEs) : std::span<const Swap>(kSwapsEn)) {
        const size_t n = std::strlen(w.from);
        for (size_t at = s.find(w.from); at != std::string::npos; at = s.find(w.from, at)) {
            // (a whole word's start: "tap" in "laptop" or "stapled" stays)
            const bool word_start = at == 0 || !std::isalpha(static_cast<unsigned char>(s[at - 1])) || w.from[0] == ' ';
            if (!word_start) {
                at += n;
                continue;
            }
            s.replace(at, n, w.to);
            at += std::strlen(w.to);
        }
    }
    return s;
}
}  // namespace

const char* pc_words(const char* text) {
    if (text == nullptr || !has_words(text)) return text;
    static std::unordered_map<std::string, std::string> kept;  // (the PC's text of each: kept, its pointer stays good)
    const bool es = settings_spanish();
    const std::string key = (es ? "es:" : "en:") + std::string(text);
    auto it = kept.find(key);
    if (it != kept.end()) return it->second.c_str();
    if (kept.size() > 4096) return text;  // (a text built each frame: left as it is rather than kept without end)
    return kept.emplace(key, swapped(text, es)).first->second.c_str();
}

std::string pc_words(const std::string& text) { return has_words(text.c_str()) ? swapped(text, settings_spanish()) : text; }
