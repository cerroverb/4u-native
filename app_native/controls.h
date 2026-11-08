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
//
// The keyboard (MHGU's keys where it has them): W A S D the Circle Pad (Shift held: walking), I J K L the camera (as
// the right stick), U X, O or F A, Space B, R Y, Q L, E R, Tab ZL, T ZR, Enter START, Backspace SELECT, the arrows the
// D-pad (and X and Y their own letters, Escape B).
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
// The last input was a controller's or a key's (not a finger's): the app's plates show its glyphs then
bool controls_last_input_pad();
// The game's A is the bottom face button of the controller in use (Label, on an Xbox or a PlayStation layout): the
// settings' confirm and back follow it.
bool controls_a_is_south();
// The controller the game reads (0: none).
SDL_JoystickID controls_active_pad();

// Tests (launch.txt's env=):
//   MH4U_TEST_INPUT=<file>       an input script (the runner's format: f<frame>:<BUTTON>:f<frames>, CIRCLE:x:y,
//                                CSTICK:x:y, TOUCH:x:y) played on the game's frame counter beside the real controls,
//                                which work all the while: the way to a place in the game, then the player's hands.
//                                MH4U_TEST_INPUT_UNTIL=<frame>: its presses from that frame on are left out.
//   MH4U_PAD_DEBUG=1             every controller's press and push in the log.
//   MH4U_TEST_JOYSTICK=1         a joystick SDL has no map for (a virtual one, "Test Joystick"), for the notice that
//                                asks to map it.
