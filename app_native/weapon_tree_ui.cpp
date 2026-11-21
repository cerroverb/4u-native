// The Smithy's weapon tree over the game (weapon_tree_ui.h): the app's side of mh4n's Port::WeaponTree.
#include "weapon_tree_ui.h"

#include <algorithm>
#include <cstdio>

#include "controls.h"
#include "imgui.h"
#include "mh4n/host.h"
#include "settings_panel.h"
#include "touch.h"
#include "weapon_tree.h"
#include "weapon_tree_panel.h"

namespace {

constexpr float ANIM_MS = 130.f;
// the classes' names in the app's language (weapon_tree_panel::Words), by equipment type - 7
const char* const CLASS_NAMES[] = {"Great Sword", "Sword and Shield", "Hammer", "Lance", "Light Bowgun", "Heavy Bowgun",
                                   "Long Sword", "Switch Axe", "Gunlance", "Bow", "Dual Blades", "Hunting Horn",
                                   "Insect Glaive", "Charge Blade"};

// the classes in the panel's order (its icons'), by equipment type - 7
const int PANEL_CLASS[] = {weapon_tree_panel::kGreatSword, weapon_tree_panel::kSwordAndShield, weapon_tree_panel::kHammer,
                           weapon_tree_panel::kLance, weapon_tree_panel::kLightBowgun, weapon_tree_panel::kHeavyBowgun,
                           weapon_tree_panel::kLongSword, weapon_tree_panel::kSwitchAxe, weapon_tree_panel::kGunlance,
                           weapon_tree_panel::kBow, weapon_tree_panel::kDualBlades, weapon_tree_panel::kHuntingHorn,
                           weapon_tree_panel::kInsectGlaive, weapon_tree_panel::kChargeBlade};

bool g_open = false;
weapon_tree_panel::Icons g_icons;    // (the game's icons, from the player's RomFS: mh4n's weapon_names)
int g_icons_language = -1;
Uint64 g_in = 0, g_out = 0;          // (when it opened, when it closed: the animation)
std::uint32_t g_serial = 0;          // (the Smithy's weapon it shows)
Uint64 g_names_at = 0;               // (the names read again while some were missing)
weapon_tree_panel::Content g_content;
weapon_tree_panel::State g_state;
float g_w = 0, g_h = 0;
SDL_FingerID g_finger = 0;
bool g_finger_on = false;
std::uint32_t g_prev_held = 0;
int g_repeat_dx = 0, g_repeat_dy = 0;
Uint64 g_repeat_at = 0;

float ease(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return 1 - (1 - t) * (1 - t);
}

bool missing_names(const weapon_tree_panel::Content& c) {
    for (const auto& n : c.view.nodes)
        if (!(n.flags & weapon_tree::kNext) && c.cls.weapons[n.id].name.empty()) return true;
    return false;
}

// The tree of the Smithy's weapon now; false when it shows none
bool load() {
    weapon_tree::Class cls;
    weapon_tree::View view;
    int type = 0;
    if (!Port::WeaponTree::Tree(&cls, &view, &type)) return false;
    const int focus = g_state.focus;
    const bool same = g_open && Port::WeaponTree::Current().serial == g_serial;
    g_content.cls = std::move(cls);
    g_content.view = std::move(view);
    g_content.spanish = settings_spanish();
    const int c = type - Port::WeaponTree::kFirstType;
    g_content.class_name = c >= 0 && c < 14 ? weapon_tree_panel::Words(CLASS_NAMES[c], g_content.spanish) : "";
    g_content.weapon_class = c >= 0 && c < 14 ? PANEL_CLASS[c] : -1;
    const int language = Port::WeaponTree::Current().language;
    if (language != g_icons_language) {  // (once a language: the game's icons, or the panel's drawn ones)
        weapon_tree_panel::Icons ic;
        if (Port::WeaponTree::Icons(language, &ic.rgba, &ic.cell)) {
            ic.serial = g_icons.serial + 1;
            g_icons = std::move(ic);
            g_icons_language = language;
        }
    }
    g_content.icons = &g_icons;
    if (!same) g_state = weapon_tree_panel::State{};
    else g_state.focus = focus;
    g_serial = Port::WeaponTree::Current().serial;
    return true;
}

}  // namespace

bool weapon_tree_available() {
    return !g_open && Port::WeaponTree::On() && Port::WeaponTree::Current().shown;
}

void weapon_tree_open(bool by_pad) {
    if (g_open || !Port::WeaponTree::Current().shown || !load()) return;
    g_open = true, g_in = SDL_GetTicks(), g_out = 0, g_finger_on = false;
    g_prev_held = 0;
    std::fprintf(stderr, "[Info] Weapon tree: opened (%s), %d weapons shown\n", by_pad ? "the controller" : "the button",
                 (int)g_content.view.nodes.size());
}

void weapon_tree_close() {
    if (!g_open) return;
    g_open = false, g_out = SDL_GetTicks(), g_finger_on = false;
    std::fprintf(stderr, "[Info] Weapon tree: closed\n");
}

bool weapon_tree_is_open() {
    return g_open;
}

bool weapon_tree_back() {
    if (!g_open) return false;
    weapon_tree_close();
    return true;
}

bool weapon_tree_takes_pad() {
    return g_open;
}

void weapon_tree_pad(std::uint32_t held, std::uint32_t pressed, float sx, float sy) {
    if (!g_open) return;
    const std::uint32_t edge = (held & ~g_prev_held) | pressed;
    g_prev_held = held;
    if (edge & mh4n::kPadB) return weapon_tree_close();
    const int dx = (held & mh4n::kPadLeft) || sx < -.55f ? -1 : (held & mh4n::kPadRight) || sx > .55f ? 1 : 0;
    const int dy = (held & mh4n::kPadUp) || sy > .55f ? -1 : (held & mh4n::kPadDown) || sy < -.55f ? 1 : 0;
    const Uint64 now = SDL_GetTicks();
    if ((dx || dy) && (dx != g_repeat_dx || dy != g_repeat_dy || now >= g_repeat_at)) {
        g_repeat_at = now + (dx != g_repeat_dx || dy != g_repeat_dy ? 380 : 140);
        weapon_tree_panel::Move(dx, dy != 0 && dx == 0 ? dy : 0, g_content, g_state);
    }
    g_repeat_dx = dx, g_repeat_dy = dy;
}

bool weapon_tree_event(const SDL_Event& e) {
    if (!g_open || g_w <= 0) return false;
    const bool down = e.type == SDL_EVENT_FINGER_DOWN,
               up = e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED;
    if (!down && !up && e.type != SDL_EVENT_FINGER_MOTION) return true;  // (the panel is over everything)
    if (down) {
        if (g_finger_on) return true;
        g_finger_on = true, g_finger = e.tfinger.fingerID;
    } else if (!g_finger_on || e.tfinger.fingerID != g_finger) {
        return true;
    }
    if (up) g_finger_on = false;
    bool close = false;
    weapon_tree_panel::Finger(down ? weapon_tree_panel::kDown : up ? weapon_tree_panel::kUp : weapon_tree_panel::kMove,
                              e.tfinger.x * g_w, e.tfinger.y * g_h, g_content, g_state, &close);
    if (down) touch_haptic_tap();
    if (close) weapon_tree_close();
    return true;
}

void weapon_tree_frame(int w, int h, bool controller) {
    (void)controller;
    g_w = (float)w, g_h = (float)h;
    if (w <= 0 || h <= 0) return;
    const Uint64 now = SDL_GetTicks();
    if (g_open) {
        const Port::WeaponTree::Smithy s = Port::WeaponTree::Current();
        if (!s.shown && now - g_in > 300) {  // (the Smithy closed under it, or its list went)
            weapon_tree_close();
        } else if (s.serial != g_serial || (missing_names(g_content) && now - g_names_at > 250)) {
            g_names_at = now;
            load();  // (the cursor moved in the Smithy's list, or the names came)
        }
    }
    float a;
    if (g_open) a = ease((now - g_in) / ANIM_MS);
    else if (g_out && now - g_out < ANIM_MS) a = 1 - ease((now - g_out) / ANIM_MS);
    else return;
    weapon_tree_panel::Draw(ImGui::GetForegroundDrawList(), (float)w, (float)h, a, controls_last_input_pad(), g_content,
                            g_state);
}
