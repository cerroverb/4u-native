// The app's own interface over the game's picture (Dear ImGui on SDL_Renderer): the touch controls in the
// Monster Hunter style (touch.cpp), the touch screen's corner, the settings. android_main.cpp owns the window and the loop.
#pragma once
#include <SDL3/SDL.h>

#include <cstdint>
#include <string>

// Where android_main.cpp draws the game's two screens this frame (window pixels).
struct UiScreens {
    SDL_FRect top{}, bottom{};
    bool bottom_shown = false;
};

// What the touch controls hold this frame, merged with the controller's by android_main.cpp.
struct UiInput {
    std::uint32_t buttons = 0;        // mh4n::kPad* bits (A B SELECT START RIGHT LEFT UP DOWN R L X Y)
    std::int16_t circle_x = 0, circle_y = 0;  // the circle pad, -156..156, y up
    std::int16_t cstick_x = 0, cstick_y = 0;  // the C-stick, same units
    bool zl = false, zr = false;
    float cam_yaw = 0, cam_pitch = 0;  // degrees to turn the camera by (Port::Hooks::AddCameraTurn), taken each frame
};

// What the game is doing, as far as android_main.cpp can tell; the touch controls change with it. Every field has a
// default that works without it (the state android_main.cpp doesn't feed yet).
struct UiGameState {
    // A menu of the game's is open (1: the touch controls fade and the menu pad comes in, its d-pad and L and R tabs;
    // the stick moves the cursor too) or the hunter is being played (0). -1, not known: as it was.
    int menu = -1;
    // The weapon: 1 drawn, 0 sheathed, -1 not known. The thumb past the stick's rim holds R (the dash) unless drawn.
    int weapon_drawn = -1;
    // The weapon in hand: 0 a melee one (or not known), 1 a bow, 2 a bowgun (light or heavy): a layout of their own.
    int ranged = 0;
    // The game's Bow Controls option: 0 Type 1 (X draws), 1 Type 2 (R draws).
    int bow_type = 0;
    // The item bar on the top screen, where a tap uses the item (Y) and a swipe turns the bar (L held with Y or A):
    // 1 drawn at `item_bar` (window pixels), 0 not on the screen, -1 not known (its usual place is assumed: the top
    // screen's bottom-right corner).
    int item_bar_shown = -1;
    SDL_FRect item_bar{};
    int top_width = 400;  // the top screen's picture in the game's pixels (the editor moves the HUD's parts in them)
    int place = -1;       // where the game is: 0 another place (the title, the Arena's equipment), 1 a town, 2 a quest
    int front_end = 0;    // 1 the intro and the title, the choice of a save, the character's creation (no menu pad)
    int action = -1;      // in a quest: the hunter's action (a number of the game's; it changes as one move ends)
    // The parts of the game's hunting HUD (Port::Hooks::HudPartKey's order: the clock, the health and stamina bars, the
    // sharpness, the name plates, the area's map, the target camera, the item bar): the layout editor moves them, and
    // the lock-on's ring goes round the target camera's icon
    struct HudPart {
        int shown = 0;    // drawn now
        int seen = 0;     // drawn at least once since the game started
        SDL_FRect r{};    // its art where it is drawn, or would be (window pixels)
        float dx = 0, dy = 0;  // the player's offset it is drawn with (hud_<key> in mh4u.ini, the top screen's pixels)
    };
    static const int HUD_PARTS = 7;
    HudPart hud[HUD_PARTS];
};

void ui_init(SDL_Window* window, SDL_Renderer* renderer, const std::string& data_dir);
// Every SDL event goes here first: true when the interface took it (a finger on a control, the settings). A finger
// that lands on the touch screen's corner while it shows is never taken: it is the game's.
bool ui_event(const SDL_Event& e, const UiScreens& screens);
// Builds and draws the interface over the game's picture (between the game's screens and SDL_RenderPresent).
// `controller`: a controller is connected (the touch controls hide, as in MHGU, when [Touch] Controls = Auto).
void ui_frame(int width, int height, const UiScreens& screens, bool controller);
// ui_frame's input side alone, nothing drawn: the touch controls take the fingers' events of a loop turn that does not
// present (the window's next buffer would only wait for a vsync).
void ui_update(int width, int height, const UiScreens& screens, bool controller);
// The controls' state for this frame (after ui_frame); the camera turn is handed over once.
UiInput ui_take_input();
// The touch screen's corner: shown or hidden by the interface (a button of its own, the 3DS's two screens; shown at
// first). While it shows, the touch controls it would cover move out from under it, the whole group on that side
// together (above it, down to 60% of their size to fit there, else beside it). Remembered apart for while the touch
// controls show and while they
// are hidden (a controller). Hidden while the layout editor is open.
bool ui_bottom_wanted();
void ui_toggle_bottom();
void ui_set_bottom_default(bool shown);  // touch_set_bottom_default
// What the game is doing (see UiGameState); call it whenever something changes, or every frame.
void ui_set_game_state(const UiGameState& state);
// The settings panel (settings_panel.h) over the game: a tap on the settings button (a long press opens the layout
// editor), the Android Back key or a controller's guide button opens it. While it is open the touch controls are
// neither drawn nor fed, and the panel takes the fingers, the controller's presses and the keys (ui_event).
void ui_open_settings();
bool ui_settings_open();
// The touch controls' layout editor is open (touch.cpp; the game waits meanwhile, as in the settings, playing alone).
bool touch_editing();
// ...and holds the game now: not for a moment of every 100 ms while a part of the game's HUD is dragged (touch.cpp).
bool touch_editor_holds();
void ui_shutdown();
