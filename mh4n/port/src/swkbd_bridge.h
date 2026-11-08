// The game's own software keyboard, typed on from outside (docs/research/keyboard.md). MH4U does not use the system's
// keyboard applet: it carries its own copy of it (`sSoftwareKeyboard`, the `nSwkbd` classes), drawn on the touch
// screen, for the Guild Card's greeting, the names, the hall's password and the like. The app shows the phone's own
// keyboard instead while the game's is open, and its text goes into the game's text box (where the game's own keys
// keep working, and what they type comes back here); OK and Cancel are the game's own buttons, pressed for the player,
// so the game checks the text as it checks its own keys'.
//
// Frame runs on the emulation thread, once a frame (hooks.cpp's sMhMain::frame, before the game's frame): it reads the
// keyboard's state and carries out what the app asked for since. Everything else may be called from any thread.
// Nothing here logs the text.
#pragma once

#include <cstdint>
#include <string>

struct RCtx;

namespace Port::Keyboard {

/// What the game's keyboard asks for, while it is open.
struct Request {
    bool open = false;
    std::uint32_t serial = 0;       ///< one per keyboard the game opens
    bool numeric = false;           ///< the number pad (the game's keyboard type 2)
    std::uint32_t max_length = 0;   ///< UTF-16 units
    /// The game's buttons: 0 OK alone, 1 Cancel and OK, 2 Cancel, I Forgot and OK, 3 none.
    std::uint32_t buttons = 0;
    /// The text the game takes: 0 anything, 1 not empty, 2 not empty and not blank, 3 not blank, 4 exactly
    /// max_length characters.
    std::uint32_t accept = 0;
    std::uint32_t filters = 0;      ///< 1 at most max_digits digits, 2 no '@', 4 no '%', 8 no '\'
    std::uint32_t max_digits = 0;
    std::u16string hint;            ///< the game's hint for the empty field (often none)
    std::u16string text;            ///< what the game's text box holds now
    std::uint32_t text_serial = 0;  ///< changes when the game changes `text` (its own keys), not for SetText's
};

/// The keyboard open now (a copy).
Request Current();

/// The game's text box takes `text` (cut to the keyboard's length) at the next frame, the cursor at its end. Ignored
/// unless `serial` is the open keyboard's.
void SetText(std::uint32_t serial, const std::u16string& text);
/// `text`, then the game's OK button.
void Submit(std::uint32_t serial, const std::u16string& text);
/// The game's Cancel button (a keyboard with one only).
void Cancel(std::uint32_t serial);

/// hooks.cpp: before each of the game's frames. Returns a line for the log, or "".
std::string Frame(RCtx* ctx);

}  // namespace Port::Keyboard
