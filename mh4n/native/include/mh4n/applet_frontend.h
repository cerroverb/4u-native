// Derived from Azahar src/core/frontend/applets/swkbd.h at 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6
// (GPL-2.0-or-later: it says "GPLv2 or any later version"): what a keyboard frontend is given
// (Frontend::KeyboardConfig: the button layouts, the accepted inputs and the filters), the reasons a text
// is turned down (Frontend::ValidationError) and the OK button's number for each layout (the order of
// ButtonConfig, which SoftwareKeyboard::Finalize compares with the pressed button).
// Copyright 2018 Citra Emulator Project
//
// The frontends of the library applets (N3-12, D43, D64): the app shows the player's own keyboard when the
// game opens the software keyboard, and a notice when it starts the Error applet. Without a frontend the
// applets answer as Azahar's default ones do (the runner and the golden sessions set none):
//
//   mh4n::Runtime runtime;
//   mh4n::SetKeyboardFrontend(runtime, std::make_shared<MyKeyboard>());  // before Boot
//   mh4n::SetErrorFrontend(runtime, std::make_shared<MyNotices>());       // before Boot
//   runtime.Boot(options);
//
//   // MyKeyboard::Execute (scheduler thread): copy the request, hand it to the UI thread, return.
//   // The UI thread, when the player presses a button:
//   if (mh4n::ValidateKeyboard(request, text, button) == mh4n::KeyboardResult::Ok) {
//       mh4n::FinishKeyboard(runtime, text, button);  // the game gets it at the applet's next update
//   } else { /* tell the player why, keep the keyboard up */ }
//
// The keyboard's flow is Azahar's (src/core/hle/applets/swkbd.cpp): StartLibraryApplet calls Execute; the
// applet checks for the frontend's answer at each of its update ticks (every 4,468,352 ticks of game
// time, about 16.7 ms) and finishes at the first one after FinishKeyboard, writing the text and the
// button's return code for the game. A text the checks turn down (FinishKeyboard without ValidateKeyboard,
// or a different request) is dropped with a warning and Execute is called again with the same request, so
// the game is never left waiting with no keyboard on screen. When the game filters the text itself (the
// callback filter) and turns it down, ShowError gets the game's message and, if the game asks for another
// text, Execute is called again. The Error applet still closes inside StartLibraryApplet (D43): Show is
// called from inside that command and must not wait for the player.
//
// Privacy (rule 5 of N3): nothing in the runtime logs the keyboard's text, its hint or initial text, the
// callback's message, or the Error applet's text, at any level. A frontend must not either.
//
// Threads: SetKeyboardFrontend and SetErrorFrontend before Boot, on the thread that calls Boot.
// KeyboardFrontend::Execute and ShowError, and ErrorFrontend::Show, run on the scheduler thread (the one
// that runs Boot): copy what they need and return at once. ValidateKeyboard and FinishKeyboard may be
// called from any thread.
#pragma once

#include <array>
#include <memory>
#include <string>

#include "mh4n/types.h"

namespace mh4n {

class Runtime;

// ---- the software keyboard ------------------------------------------------------------------------------

// The buttons the game shows. Each layout's value is the number of its rightmost button, the OK one; the
// buttons are numbered from the left: Single {OK = 0}, Dual {Cancel = 0, OK = 1}, Triple {Cancel = 0,
// I Forgot = 1, OK = 2}. With None the game shows no button: answer with 0 (no check is made, as Azahar's
// default keyboard). A value that is none of these is a game's mistake: no text is accepted then
// (KeyboardResult::UnknownConfiguration).
enum class KeyboardButtons : u32 { Single = 0, Dual = 1, Triple = 2, None = 3 };

// What kinds of text the game accepts (the valid-input rule).
enum class KeyboardAccept : u32 {
    Anything = 0,
    NotEmpty = 1,
    NotEmptyAndNotBlank = 2,  // neither empty nor only white space (an empty text is reported as blank)
    NotBlank = 3,             // not only white space; Azahar's check finds an empty text blank too

    FixedLength = 4,          // exactly max_text_length UTF-16 units
};

// The kind of keyboard the game asks for (SwkbdType): the app may show a number pad for Numpad.
enum class KeyboardType : u32 { Normal = 0, Qwerty = 1, Numpad = 2, Western = 3 };

// Whether the text is hidden while it is typed (SwkbdPasswordMode).
enum class KeyboardPassword : u32 { None = 0, Hide = 1, HideDelay = 2 };

// A plain copy of what the game's keyboard configuration asks for. The texts are UTF-16, as the 3DS has
// them; an empty button text means the default one ("Cancel", "I Forgot", "Ok").
struct KeyboardRequest {
    KeyboardType type = KeyboardType::Normal;
    KeyboardButtons buttons = KeyboardButtons::Single;
    // By position, whatever the layout: [0] the left button (Cancel), [1] the middle one (I Forgot),
    // [2] the right one (OK). A Dual layout uses [0] and [2].
    std::array<std::u16string, 3> button_text;
    std::u16string hint;          // shown in the empty field
    std::u16string initial_text;  // what the field starts with (empty if the game gives none)
    u16 max_text_length = 0;      // in UTF-16 units, the 3DS's measure
    KeyboardAccept accept = KeyboardAccept::Anything;
    KeyboardPassword password = KeyboardPassword::None;
    bool multiline = false;
    struct Filters {
        bool digits = false;     // at most max_digits digits
        bool at = false;         // no '@'
        bool percent = false;    // no '%'
        bool backslash = false;  // no '\'
        bool profanity = false;  // asked for, but not checked (Azahar does not either)
        bool callback = false;   // the game checks the text itself after FinishKeyboard
    };
    Filters filters;
    u16 max_digits = 0;

    // The number of the OK button: the layout's value (Single 0, Dual 1, Triple 2), and 0 for None.
    u8 OkButton() const { return buttons == KeyboardButtons::None ? 0 : static_cast<u8>(buttons); }
};

// Why a text is turned down (Ok: it is not). The order is Azahar's ValidationError, then
// UnknownConfiguration (a configuration value the game sent that is none of the known ones).
enum class KeyboardResult : u8 {
    Ok,
    ButtonOutOfRange,
    MaxDigitsExceeded,
    AtSignNotAllowed,
    PercentNotAllowed,
    BackslashNotAllowed,
    ProfanityNotAllowed,
    CallbackFailed,
    FixedLengthRequired,
    MaxLengthExceeded,
    BlankInputNotAllowed,
    EmptyInputNotAllowed,
    UnknownConfiguration,
};

class KeyboardFrontend {
public:
    virtual ~KeyboardFrontend() = default;
    // The game opened the keyboard (or asks for another text): show it. Scheduler thread; return at once.
    virtual void Execute(const KeyboardRequest& request) = 0;
    // The game's own check (the callback filter) turned the last text down with this message. Execute
    // follows if the game wants another text; otherwise the keyboard is closed. Scheduler thread.
    virtual void ShowError(const std::u16string& message) { static_cast<void>(message); }
};

// Azahar's checks of a text (SoftwareKeyboard::Finalize: the filters, the length and the valid-input
// rule, then the button), made only when `button` is the OK one, exactly as the applet makes them when
// FinishKeyboard's text arrives (the same code as the runtime's default keyboard). Pure; any thread.
KeyboardResult ValidateKeyboard(const KeyboardRequest& request, const std::u16string& text, u8 button);

// The player's answer to the keyboard that is open: posted, so callable from any thread; the applet takes
// it at its next update tick (an answer given while no keyboard is open is dropped when the next one
// opens). Does nothing if no keyboard frontend was set.
void FinishKeyboard(Runtime& runtime, std::u16string text, u8 button);

// ---- the Error applet --------------------------------------------------------------------------------------

class ErrorFrontend {
public:
    virtual ~ErrorFrontend() = default;
    // The game started the Error applet with an error code and a message (UTF-16, possibly empty). Called
    // for the error display types, not for the EULA ones (2 to 5, which are agreed to at once). The applet
    // closes when this returns (D43): copy and return. Scheduler thread.
    virtual void Show(u32 error_code, const std::u16string& text) = 0;
};

// ---- setting them --------------------------------------------------------------------------------------------

// Before Boot. An empty pointer removes the frontend (the default applet answers again).
void SetKeyboardFrontend(Runtime& runtime, std::shared_ptr<KeyboardFrontend> frontend);
void SetErrorFrontend(Runtime& runtime, std::shared_ptr<ErrorFrontend> frontend);

}  // namespace mh4n
