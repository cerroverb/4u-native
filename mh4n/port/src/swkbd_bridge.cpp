#include "swkbd_bridge.h"

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <fmt/format.h>

extern "C" {
#include "recomp.h"
}

namespace Port::Keyboard {

namespace {

// USA v1.3.0 (docs/research/keyboard.md). The keyboard, sSoftwareKeyboard (DTI 0x010bd030), made on first use by
// sGameControl (0x002b5e28) and kept for good.
constexpr u32 SKBD_INSTANCE = 0x0104deb4;   // sSoftwareKeyboard*
// its configuration, a copy of the 3DS's SwkbdConfig at +0x1c (the fields up to the hint where the 3DS has them)
constexpr u32 KBD_TYPE = 0x1c;              // u32: 0 normal, 1 QWERTY, 2 number pad, 3 western
constexpr u32 KBD_BUTTONS = 0x20;           // u32: buttons minus one (3 none)
constexpr u32 KBD_VALID_INPUT = 0x24;       // u32
constexpr u32 KBD_FILTERS = 0x34;           // u32
constexpr u32 KBD_MAX_LENGTH = 0x3c;        // u16
constexpr u32 KBD_MAX_DIGITS = 0x40;        // u16
constexpr u32 KBD_HINT = 0xac;              // u16[65]
constexpr u32 KBD_STATE = 0x520;            // u32: 4 idle, 0 ready, 1 open, 2 closing, 3 closed (the answer is in)
constexpr u32 KBD_TEXT_MANAGER = 0x518;     // nSwkbd::TextManager*
// nSwkbd::TextManager
constexpr u32 TM_LINES = 0x4;               // nSwkbd::LineManager*
constexpr u32 TM_LENGTH = 0x8;              // u32: characters in the text box
constexpr u32 TM_BUFFER = 0x10;             // u16*: the text (room for max_length + 0x38)
constexpr u32 TM_CURSOR = 0x14;             // u32
constexpr u32 LM_FONT = 0x24;               // nSwkbd::LineManager: the font that lays the lines out (set once the text
                                            // area is made, a frame or two after the keyboard opens)
// functions
constexpr u32 TM_SET_TEXT = 0x00666d8c;     // TextManager::setText(this, const u16* text, int length): cursor at 0
constexpr u32 LM_FIND_LINE = 0x006654b4;    // LineManager::findLine(this, int position): the cursor's line
constexpr u32 KBD_FINISH = 0x003ae264;      // sSoftwareKeyboard::finish(this, bool decided, int button): what a
                                            // dialog button does (the return code, the text for the game, closing)
constexpr u32 KBD_SPARE = 0x38;             // the text box's room beyond max_length

enum class Kind { SetText, Submit, Cancel };
struct Command {
    Kind kind;
    u32 serial;
    std::u16string text;
};

std::mutex mutex;
Request current;
u32 serials = 0;
std::optional<Command> pending;
u32 frames_open = 0;  // the game's frames since its keyboard opened
std::u16string test_text;  // MH4U_KEYBOARD_TEXT: every keyboard answered with it at once (tests)
bool test_read = false;

std::u16string ReadText(RCtx* ctx, u32 address, u32 count) {
    std::u16string text;
    for (u32 i = 0; i < count; i++) {
        const char16_t c = static_cast<char16_t>(rd16(ctx, address + 2 * i));
        if (c == 0) break;
        text.push_back(c);
    }
    return text;
}

/// Calls a recompiled game function with up to four arguments, the caller's registers kept.
void Call(RCtx* ctx, u32 function, std::initializer_list<u32> args, u32 sp = 0) {
    u32 regs[16];
    std::copy(std::begin(ctx->r), std::end(ctx->r), regs);
    const u32 flags[4] = {ctx->fn, ctx->fz, ctx->fc, ctx->fv};
    const u32 expect_next = ctx->expect_next;
    ctx->expect_next = 0;
    int i = 0;
    for (u32 a : args) ctx->r[i++] = a;
    if (sp) ctx->r[13] = sp;
    ctx->r[14] = function;  // (any address: the call ends when the function returns)
    rc_call(ctx, function);
    std::copy(std::begin(regs), std::end(regs), ctx->r);
    ctx->fn = flags[0];
    ctx->fz = flags[1];
    ctx->fc = flags[2];
    ctx->fv = flags[3];
    ctx->expect_next = expect_next;
}

/// The text box takes `text` as the game's own setText does (its lines laid out again), the cursor at its end.
void PutText(RCtx* ctx, u32 tm, u32 max_length, std::u16string text) {
    const u32 room = std::min<u32>(max_length, 0x200);
    if (text.size() > room) text.resize(room);
    // the text below the game thread's stack, for setText to copy (the stack under sp is free)
    const u32 at = (ctx->r[13] - 2 * (static_cast<u32>(text.size()) + 1) - 0x40) & ~7u;
    for (size_t i = 0; i < text.size(); i++) wr16(ctx, at + 2 * static_cast<u32>(i), text[i]);
    wr16(ctx, at + 2 * static_cast<u32>(text.size()), 0);
    Call(ctx, TM_SET_TEXT, {tm, at, static_cast<u32>(text.size())}, at - 0x40);
    const u32 length = rd32(ctx, tm + TM_LENGTH);
    wr32(ctx, tm + TM_CURSOR, length);
    if (const u32 lines = rd32(ctx, tm + TM_LINES)) Call(ctx, LM_FIND_LINE, {lines, length});
}

/// The return code of the OK and the Cancel buttons for a layout (the 3DS's SwkbdResult: D0_CLICK, D1_CLICK0 and
/// D1_CLICK1, D2_CLICK0 and D2_CLICK2), -1 for none.
int OkCode(u32 buttons) {
    return buttons == 1 ? 2 : buttons == 2 ? 5 : 0;
}
int CancelCode(u32 buttons) {
    return buttons == 1 ? 1 : buttons == 2 ? 3 : -1;
}

}  // namespace

Request Current() {
    std::lock_guard lock(mutex);
    return current;
}

void SetText(std::uint32_t serial, const std::u16string& text) {
    std::lock_guard lock(mutex);
    if (pending && pending->kind != Kind::SetText) return;  // (OK or Cancel already asked for)
    pending = Command{Kind::SetText, serial, text};
}

void Submit(std::uint32_t serial, const std::u16string& text) {
    std::lock_guard lock(mutex);
    pending = Command{Kind::Submit, serial, text};
}

void Cancel(std::uint32_t serial) {
    std::lock_guard lock(mutex);
    pending = Command{Kind::Cancel, serial, {}};
}

std::string Frame(RCtx* ctx) {
    if (!test_read) {
        test_read = true;
        if (const char* t = std::getenv("MH4U_KEYBOARD_TEXT"))
            for (const char* p = t; *p; ++p) test_text.push_back(static_cast<unsigned char>(*p));  // (ASCII)
    }
    const u32 kbd = rd32(ctx, SKBD_INSTANCE);
    const u32 tm = kbd ? rd32(ctx, kbd + KBD_TEXT_MANAGER) : 0;
    const bool open = kbd && tm && rd32(ctx, kbd + KBD_STATE) == 1;
    std::string line;
    std::optional<Command> todo;
    Request now;
    {
        std::lock_guard lock(mutex);
        if (open && !current.open) {  // the game opened its keyboard
            current = Request{};
            current.open = true;
            current.serial = ++serials;
            current.numeric = rd32(ctx, kbd + KBD_TYPE) == 2;
            current.max_length = rd16(ctx, kbd + KBD_MAX_LENGTH);
            current.buttons = rd32(ctx, kbd + KBD_BUTTONS);
            current.accept = rd32(ctx, kbd + KBD_VALID_INPUT);
            current.filters = rd32(ctx, kbd + KBD_FILTERS) & 0xf;
            current.max_digits = rd16(ctx, kbd + KBD_MAX_DIGITS);
            current.hint = ReadText(ctx, kbd + KBD_HINT, 64);
            pending.reset();
            line = fmt::format("keyboard: the game asks for a text (up to {} characters{}, {} button(s), rule {})",
                               current.max_length, current.numeric ? ", numbers" : "",
                               current.buttons < 3 ? current.buttons + 1 : 0, current.accept);
            if (!test_text.empty()) pending = Command{Kind::Submit, current.serial, test_text};
        } else if (!open && current.open) {
            current.open = false;
            pending.reset();
            line = "keyboard: closed";
        }
        frames_open = open ? frames_open + 1 : 0;
        // (not before the text area is up: its lines need the font, and the game puts its first text in on the
        // second frame)
        const u32 lines = open ? rd32(ctx, tm + TM_LINES) : 0;
        const bool ready = frames_open >= 3 && lines && rd32(ctx, lines + LM_FONT);
        if (pending && (!open || pending->serial != current.serial)) pending.reset();
        if (ready && pending) {
            todo = std::move(pending);
            pending.reset();
        }
        now = current;
    }
    if (!open) return line;
    // what the text box holds: a change since the last frame is the game's (its own keys, or its first text), and
    // text_serial tells the app; the texts put in below are the app's own and do not count
    auto box = [&] {
        const u32 length = std::min<u32>(rd32(ctx, tm + TM_LENGTH), now.max_length + KBD_SPARE);
        const u32 buffer = rd32(ctx, tm + TM_BUFFER);
        return buffer ? ReadText(ctx, buffer, length) : std::u16string();
    };
    {
        std::u16string text = box();
        std::lock_guard lock(mutex);
        if (current.serial == now.serial && current.text != text) {
            current.text = std::move(text);
            current.text_serial++;
        }
    }
    if (todo) {
        if (todo->kind != Kind::Cancel) PutText(ctx, tm, now.max_length, todo->text);
        const int code = todo->kind == Kind::Submit ? OkCode(now.buttons) : todo->kind == Kind::Cancel ? CancelCode(now.buttons) : -1;
        if (code >= 0) {
            Call(ctx, KBD_FINISH, {kbd, 1, static_cast<u32>(code)});
            line += std::string(line.empty() ? "" : "; ") +
                    (todo->kind == Kind::Submit ? "keyboard: OK pressed for the player" : "keyboard: Cancel pressed for the player");
        }
        std::u16string text = box();
        std::lock_guard lock(mutex);
        if (current.serial == now.serial) current.text = std::move(text);
    }
    return line;
}

}  // namespace Port::Keyboard
