// Controllers and a physical keyboard for the game (controls.cpp), as in the MHGU port's runtime/host_input.c: every
// controller is open and the one a button was last pressed on (or a stick pushed far) is read; the face buttons by
// their place (the 3DS's) or by their letters; the right stick turns the camera by itself, without the game's Circle
// Pad Pro option; START and SELECT together open the settings from any controller; a map of the player's for a
// controller SDL reads wrong (padmap.h). Everything here runs on the app's main thread.
//
// [Controls] of mh4u-app.ini (the settings' Controls tab, read again whenever the file is saved):
//   FaceButtons = position|label   position: A on the right, B at the bottom, as on the 3DS; label: by the letter
//                                  printed on the button (PlayStation's shapes as an Xbox pad's letters at their place)
//   RightStick  = camera|cstick    camera: the port turns the camera by the stick's tilt, smoothly, whether the game's
//                                  Circle Pad Pro option is on or not; cstick: the C-stick, as the game reads it with
//                                  that option on (the game's own turn, in steps)
//   StickSpeed  = 25..300          the camera's speed at full tilt, % (100: about half a turn a second)
//   InvertX, InvertY = on|off      the camera's turn the other way, across or up and down
//   R3          = lockon|L|ZL|ZR|off  the right stick's click: the lock-on (the touch controls', [Controls] LockOn), the
//                                  camera behind the hunter or to the target (L), the target camera (ZL, the port's
//                                  target button), the kick (ZR), nothing
//   Deadzone    = 0..30            the sticks' dead zone, % of their reach (10)
//   Combo1Pad .. Combo15Pad = None|L3|R3|Select|Guide|Misc|Paddle1..Paddle4|Touchpad|A|B|X|Y|L|R|ZL|ZR|Start|Up|Down|
//                            Left|Right|Raw<i>
//                                  a controller's button that presses a combined button's buttons (the touch controls'
//                                  Combo<n>, touch.h touch_combo_buttons) while held, and no longer does what it did
//                                  (its button of the game's; L3 the touch screen or the town's list, R3 the lock-on,
//                                  Select the town's list and START + SELECT, Guide the settings): one with no buttons
//                                  leaves it as it was. None by default. A, B, X and Y by their place on the controller
//                                  (A on the right, B at the bottom, X at the top, Y on the left), whatever FaceButtons
//                                  says; ZL and ZR count as pressed past half their travel (1.0.2: any button). Raw<i>:
//                                  the controller's own button number i, one SDL's map of it has no name for (an
//                                  8BitDo's paddles on a phone, say: the settings' Press a controller button finds it).
//                                  The MHGU port's [Touch] Combo<n>Pad, with Select and Start for its Minus and Plus
//
// The keyboard and the mouse: the keys and mouse buttons of [Keybinds] in mh4u-app.ini (the MHGU port's names and
// format, "O, F, Mouse Right"; at most kBindKeys each). By default W A S D the Circle Pad (Shift held: walking), I J K L
// the camera (as the right stick), U X or the left click X, O F or the right click A, Space or Escape B, R Y or the
// middle click Y, Q L, E R, Tab ZL, T ZR, Enter START, Backspace SELECT, the arrows the D-pad, Ctrl the left stick's click
// (the touch screen, as a controller's), Alt the right stick's (the lock-on, [Controls] R3), the wheel the item bar.
// With a keyboard and a mouse connected (controls_keyboard_mouse) a left click on the game's picture captures the mouse
// (one on the touch screen or a touch control is a tap there, as before): its movement turns the camera (MouseSensitivity
// in [Mouse], 0.08 degrees a count at 1.0, as the MHGU port's hunt camera), its buttons press what [Keybinds] says, and
// Escape lets it go (Escape is B again then). The settings show Keybinds and the mouse's speed only then.
// On the PC (MH4U.exe) also, as the MHGU port's desktop: a mouse button on A presses it only with the weapon out, in a menu
// or a talk of the game's, or out of the towns and quests (the right click attacks; it never talks, gathers or carves by
// mistake); a modifier key (Ctrl, Alt, the Windows key) on a button's keybind presses it only when tapped alone (Alt+Tab,
// Alt+Enter are no lock-on); [Mouse] InvertX / InvertY; and keybinds of the PC's own (BIND_HELP.., pc/pc_input.h).
#pragma once
#include <SDL3/SDL.h>

#include <cstdint>
#include <functional>
#include <string>

// What the controllers and the keyboard hold this turn of the loop (android_main.cpp merges it with the touch controls')
struct ControlsInput {
    std::uint32_t buttons = 0;                // mh4n::kPad* bits
    std::int16_t circle_x = 0, circle_y = 0;  // the Circle Pad, -156..156, y up
    std::int16_t cstick_x = 0, cstick_y = 0;  // the C-stick, same units
    bool zl = false, zr = false;
    float cam_yaw = 0, cam_pitch = 0;  // degrees to turn the camera by (Port::Hooks::AddCameraTurn), this turn's
};

// Once, after ui_init (ImGui reads the controller the game reads): the maps kept in <data_dir>, the controllers there
// are. `set_touch`: the touch screen, for the test script (MH4U_TEST_INPUT, below).
void controls_init(const std::string& data_dir, std::function<void(bool down, int x, int y)> set_touch);
// Every SDL event, before ui_event: true when it was the controls' alone (START with SELECT, or SELECT with START,
// which opened or closed the settings: it goes nowhere else).
bool controls_event(const SDL_Event& e);
// Each turn of the loop. `game_frame`: the game's frame counter (the test script's clock). Nothing while the settings
// are open, and a button held as they close is not the game's until it is let go.
ControlsInput controls_input(std::uint32_t game_frame);
// A controller in the player's hands is connected (the touch controls hide): not a phone's own keys, a keyboard, a
// virtual device or a keypad, unless the player mapped it.
bool controls_real_pad();
// (1.0.2, T10) The first controller that hides the touch controls, by its name ("" none): touch.cpp's notice says which
std::string controls_real_pad_name();
// The last input was a controller's or a key's (not a finger's): the app's plates show its glyphs then
bool controls_last_input_pad();
// The game's A is the bottom face button of the controller in use (Label, on an Xbox or a PlayStation layout): the
// settings' confirm and back follow it.
bool controls_a_is_south();
// The controller the game reads (0: none).
SDL_JoystickID controls_active_pad();
// Aiming with the gyro ([Controls] GyroAim): each turn of the loop, whether a bow or a bowgun aims now (touch.cpp); the
// degrees the aim turns right and up by since the last call (the phone's gyroscope, or the controller's; GyroSensitivity
// times the turn). The sensor is open only while aiming. The same in the MHGU port
bool controls_gyro(bool aiming, float* right_deg, float* up_deg);
// The game's buttons the controller and the keys held at the last controls_input (mh4n::kPad* bits)
std::uint32_t controls_buttons();
constexpr float kGyroSensMin = 0.2f, kGyroSensMax = 3.0f;  // [Controls] GyroSensitivity (1.0 the same angle)

// ---- the keyboard's and the mouse's keybinds ([Keybinds], the MHGU port's names; START and SELECT for its Plus and Minus)
enum Bind {
    BIND_MOVE_UP, BIND_MOVE_DOWN, BIND_MOVE_LEFT, BIND_MOVE_RIGHT, BIND_WALK, BIND_CAM_UP, BIND_CAM_DOWN, BIND_CAM_LEFT,
    BIND_CAM_RIGHT, BIND_A, BIND_B, BIND_X, BIND_Y, BIND_L, BIND_R, BIND_ZL, BIND_ZR, BIND_START, BIND_SELECT,
    BIND_DPAD_UP, BIND_DPAD_DOWN, BIND_DPAD_LEFT, BIND_DPAD_RIGHT, BIND_LSTICK, BIND_RSTICK, BIND_ITEM_LEFT,
    BIND_ITEM_RIGHT, BIND_COMBO1, BIND_COMBO15 = BIND_COMBO1 + 14,
#ifndef __ANDROID__
    // (the PC's own, pc/pc_input.h: the list of the controls, the touch screen's item shortcuts - the quest's item
    // panel's four - and the town's four actions of its touch screen; the chat's key is [Controls] ChatKey, pc_net.h)
    BIND_HELP, BIND_SCREEN_ITEM1, BIND_SCREEN_ITEM4 = BIND_SCREEN_ITEM1 + 3, BIND_TOWN_CARDS, BIND_TOWN_GESTURES,
    BIND_TOWN_FRIENDS, BIND_TOWN_HIRE,
#endif
    BINDS
};
inline constexpr const char* kBindNames[BINDS] = {
    "MoveUp", "MoveDown", "MoveLeft", "MoveRight", "Walk", "CameraUp", "CameraDown", "CameraLeft", "CameraRight", "A", "B",
    "X", "Y", "L", "R", "ZL", "ZR", "Start", "Select", "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "LeftStickClick",
    "RightStickClick", "ItemLeft", "ItemRight", "Combo1Key", "Combo2Key", "Combo3Key", "Combo4Key", "Combo5Key",
    "Combo6Key", "Combo7Key", "Combo8Key", "Combo9Key", "Combo10Key", "Combo11Key", "Combo12Key", "Combo13Key",
    "Combo14Key", "Combo15Key",
#ifndef __ANDROID__
    "ControlsList", "TouchItem1", "TouchItem2", "TouchItem3", "TouchItem4", "GuildCards", "Gestures", "Friends",
    "HuntersForHire",
#endif
};
constexpr int kBindKeys = 3;  // (three: O and F were both A before there were keybinds, and the right click joins them)
constexpr int kKeyMouse = 0x1000, kKeyWheelUp = kKeyMouse + 0x10, kKeyWheelDown = kKeyMouse + 0x11;  // (the MHGU port's)
struct Keybinds {
    int keys[BINDS][kBindKeys] = {};  // an SDL scancode, kKeyMouse + an SDL mouse button, or the wheel's; 0 none
    bool operator==(const Keybinds&) const = default;
};
Keybinds controls_default_binds();
// [Keybinds] as mh4u-app.ini has it now (the defaults for a key it lacks), and the text of one keybind's keys for it
Keybinds controls_read_binds();
std::string controls_keys_text(const int keys[kBindKeys]);
// A key's or a mouse button's name, as the file and the settings show it ("" for one without a name)
std::string controls_key_name(int code);
// A keyboard with letters and a mouse are connected to the phone (GameActivity.keyboardMouse, asked every 2 s and as a
// keyboard or a mouse comes or goes): the settings show Keybinds and the mouse's speed only then
bool controls_keyboard_mouse();
// The settings choosing a key for a keybind: while on, the next key, mouse button (`mouse`: the pointer on none of the
// view's rows) or wheel notch is theirs (controls_take_key: its code; 0 Escape or Back, a cancel; -1 nothing yet)
void controls_capture(bool on, bool mouse);
int controls_take_key();
#ifndef __ANDROID__
// (the PC) the mouse captured for the camera now; let go of it (the HUD editor, a window of the app's over the game)
bool controls_mouse_captured();
void controls_mouse_let_go(const char* why);
// What the player used last: 0 the mouse or a finger, 1 the keyboard, 2 a controller (the menu's guides)
int controls_last_device();
// A key the program took for itself went down (F1, F11, Alt+Enter): a modifier held with it is a shortcut's, no tap
void controls_shortcut_used();
#endif
// [Controls] Combo<n>Pad's values, by their index (the settings' Controller button of a combined button; the first
// eleven as up to 1.0.1.1, the rest 1.0.2's), and how many combined buttons there are. A raw button i of the controller
// (Raw<i>, 0..kComboPadRaws - 1) is the value kComboPadRaw + i
inline constexpr const char* kComboPadKeys[] = {"None",    "L3",      "R3",    "Select", "Guide", "Misc",  "Paddle1",
                                                "Paddle2", "Paddle3", "Paddle4", "Touchpad", "A",   "B",     "X",
                                                "Y",       "L",       "R",     "ZL",     "ZR",    "Start", "Up",
                                                "Down",    "Left",    "Right"};
constexpr int kComboPadRaw = 100, kComboPadRaws = 64;
constexpr int kCombos = 15;  // (1.0.2: fifteen, each profile with its own: touch.cpp; six up to 1.0.1.1)
static_assert(BIND_COMBO15 - BIND_COMBO1 + 1 == kCombos, "a keybind for each combined button");
// A Combo<n>Pad value from its text in the file (0: none, or one not known), and its text
int controls_combo_pad_value(const std::string& text);
std::string controls_combo_pad_key(int value);
// The settings' Press a controller button (1.0.2): while on, the next button pressed on a controller - a named one, a
// trigger past half, or a raw button its map has no name for - is theirs, and the controller doesn't move the menu
// (controls_take_pad: its Combo<n>Pad value; -1 nothing yet)
void controls_pad_capture(bool on);
int controls_take_pad();

// Tests (launch.txt's env=):
//   MH4U_TEST_INPUT=<file>       an input script (the runner's format: f<frame>:<BUTTON>:f<frames>, CIRCLE:x:y,
//                                CSTICK:x:y, TOUCH:x:y) played on the game's frame counter beside the real controls,
//                                which work all the while: the way to a place in the game, then the player's hands.
//                                MH4U_TEST_INPUT_UNTIL=<frame>: its presses from that frame on are left out.
//   MH4U_PAD_DEBUG=1             every controller's press and push in the log.
//   MH4U_TEST_JOYSTICK=1         a joystick SDL has no map for (a virtual one, "Test Joystick"), for the notice that
//                                asks to map it. MH4U_TEST_JOYSTICK=pad: a virtual controller SDL maps ("Test Gamepad",
//                                a controller in the player's hands for the app), whose buttons the input script presses
//                                with f<frame>:PAD:<SDL's name of the button: a, back, leftstick, guide, misc1...>:f<frames>.
