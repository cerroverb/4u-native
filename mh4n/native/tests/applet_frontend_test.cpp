// Derived from Azahar src/core/frontend/applets/swkbd.cpp, src/core/hle/applets/swkbd.cpp and
// src/core/hle/applets/erreula.cpp at 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-2.0-or-later: each of
// them says "GPLv2 or any later version"): the expected answers (the validation rules, the return codes of
// the keyboard, the update tick that finishes it when the frontend's data is ready, the error applet that
// closes inside StartLibraryApplet) are the ones those sources give.
// Copyright 2018 Citra Emulator Project (frontend/applets/swkbd.cpp)
// Copyright 2015 Citra Emulator Project (hle/applets/swkbd.cpp)
// Copyright 2016-2025 Citra Emulator Project / Azahar Emulator Project (hle/applets/erreula.cpp)
//
// Tests of N3-12's applet frontends (mh4n/applet_frontend.h): ValidateKeyboard's rules; a keyboard
// frontend given the request on StartLibraryApplet, answered through FinishKeyboard from another thread
// and finishing at the next update tick; a text the checks turn down; the callback filter's message; the
// Error frontend called while the applet still closes inside StartLibraryApplet; no frontend, as before;
// and no log line holding the keyboard's text, hint or initial text, or the error's text. The game side is
// driven through the SVC layer as apt_test does; the configuration is built byte by byte at 3dbrew's
// offsets. Everything is synthetic.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "applets/applet.h"
#include "applets/swkbd.h"
#include "mh4n/applet_frontend.h"
#include "mh4n/clock.h"
#include "mh4n/ipc.h"
#include "mh4n/log.h"
#include "mh4n/options.h"
#include "mh4n/runtime.h"
#include "mh4n/service.h"

using namespace mh4n;

// ---- ValidateKeyboard (pure) ----------------------------------------------------------------------------------

namespace {

KeyboardRequest MakeRequest(KeyboardButtons buttons = KeyboardButtons::Dual, u16 max = 10,
                        KeyboardAccept accept = KeyboardAccept::NotEmptyAndNotBlank) {
    KeyboardRequest request;
    request.buttons = buttons;
    request.max_text_length = max;
    request.accept = accept;
    return request;
}

}  // namespace

TEST_CASE("ValidateKeyboard: Azahar's rules, checked only when the button is the OK one") {
    KeyboardRequest r = MakeRequest();
    CHECK(r.OkButton() == 1);
    CHECK(ValidateKeyboard(r, u"Hunter", 1) == KeyboardResult::Ok);
    // empty counts as blank first (Azahar's std::all_of over no characters is true)
    CHECK(ValidateKeyboard(r, u"", 1) == KeyboardResult::BlankInputNotAllowed);
    CHECK(ValidateKeyboard(r, u"   ", 1) == KeyboardResult::BlankInputNotAllowed);
    CHECK(ValidateKeyboard(r, u"Hunter12345", 1) == KeyboardResult::MaxLengthExceeded);  // 11 units
    CHECK(ValidateKeyboard(r, u"ハンター", 1) == KeyboardResult::Ok);  // UTF-16 units, not bytes
    // Cancel (not the OK button): no check at all, as Finalize
    CHECK(ValidateKeyboard(r, u"", 0) == KeyboardResult::Ok);
    CHECK(ValidateKeyboard(r, std::u16string(40, u'x'), 0) == KeyboardResult::Ok);

    r = MakeRequest(KeyboardButtons::Single, 4, KeyboardAccept::FixedLength);
    CHECK(r.OkButton() == 0);
    CHECK(ValidateKeyboard(r, u"abcd", 0) == KeyboardResult::Ok);
    CHECK(ValidateKeyboard(r, u"abc", 0) == KeyboardResult::FixedLengthRequired);
    r = MakeRequest(KeyboardButtons::Triple, 8, KeyboardAccept::NotBlank);
    CHECK(r.OkButton() == 2);
    // Azahar's comment says an empty input is fine here; its code finds it blank, and that is what counts
    CHECK(ValidateKeyboard(r, u"", 2) == KeyboardResult::BlankInputNotAllowed);
    CHECK(ValidateKeyboard(r, u" \t", 2) == KeyboardResult::BlankInputNotAllowed);
    r = MakeRequest(KeyboardButtons::Single, 8, KeyboardAccept::NotEmpty);
    CHECK(ValidateKeyboard(r, u"", 0) == KeyboardResult::EmptyInputNotAllowed);
    CHECK(ValidateKeyboard(r, u" ", 0) == KeyboardResult::Ok);

    r = MakeRequest(KeyboardButtons::Single, 16, KeyboardAccept::Anything);
    r.filters.digits = true;
    r.max_digits = 2;
    CHECK(ValidateKeyboard(r, u"a1b2", 0) == KeyboardResult::Ok);
    CHECK(ValidateKeyboard(r, u"a1b2c3", 0) == KeyboardResult::MaxDigitsExceeded);
    r.filters = {};
    r.filters.at = true;
    CHECK(ValidateKeyboard(r, u"me@here", 0) == KeyboardResult::AtSignNotAllowed);
    r.filters = {};
    r.filters.percent = true;
    CHECK(ValidateKeyboard(r, u"100%", 0) == KeyboardResult::PercentNotAllowed);
    r.filters = {};
    r.filters.backslash = true;
    CHECK(ValidateKeyboard(r, u"a\\b", 0) == KeyboardResult::BackslashNotAllowed);
    r.filters = {};
    r.filters.profanity = true;  // asked for, not checked (Azahar's TODO)
    CHECK(ValidateKeyboard(r, u"anything", 0) == KeyboardResult::Ok);

    // the button's range (a button past the OK one is not the OK one: no check, as Finalize)
    r = MakeRequest(KeyboardButtons::Single, 8, KeyboardAccept::Anything);
    CHECK(ValidateKeyboard(r, u"x", 3) == KeyboardResult::Ok);
    // configuration values the game should not send
    r = MakeRequest(static_cast<KeyboardButtons>(1), 8, static_cast<KeyboardAccept>(9));
    CHECK(ValidateKeyboard(r, u"x", 1) == KeyboardResult::UnknownConfiguration);
    r = MakeRequest(KeyboardButtons::None, 8, KeyboardAccept::NotEmpty);
    CHECK(r.OkButton() == 0);
    CHECK(ValidateKeyboard(r, u"", 0) == KeyboardResult::Ok);  // None: 0 is not the OK button (3)
}

TEST_CASE("--test-keyboard-frontend is an option of the runner and may be given several times") {
    const Options options =
        ParseOptions("runner", {"--test-keyboard-frontend", "One", "--test-keyboard-frontend", "Two"});
    REQUIRE_MESSAGE(options.ok(), options.error());
    CHECK(options.All("--test-keyboard-frontend") == std::vector<std::string>{"One", "Two"});
}

#if !__has_include("recomp.h")
TEST_CASE("applet_frontend_test's service cases need recomp.h (the repository around native/)" * doctest::skip()) {}
#else

#include "../src/cpu/recomp_c.h"  // last: it defines macros

namespace {

namespace fs = std::filesystem;

constexpr u32 kText = 0x00100000;

void Put16(std::vector<u8>& b, std::size_t at, u32 v) {
    b[at] = static_cast<u8>(v), b[at + 1] = static_cast<u8>(v >> 8);
}
void Put32(std::vector<u8>& b, std::size_t at, u32 v) {
    Put16(b, at, v & 0xFFFF), Put16(b, at + 2, v >> 16);
}
u32 Get16(const std::vector<u8>& b, std::size_t at) {
    return b[at] | b[at + 1] << 8;
}
u32 Get32(const std::vector<u8>& b, std::size_t at) {
    return Get16(b, at) | Get16(b, at + 2) << 16;
}
void PutText(std::vector<u8>& b, std::size_t at, const std::u16string& text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        Put16(b, at + 2 * i, text[i]);
    }
}

std::vector<u8> MakeElf() {
    std::vector<u8> b(52 + 32, 0);
    b[0] = 0x7F, b[1] = 'E', b[2] = 'L', b[3] = 'F', b[4] = 1, b[5] = 1, b[6] = 1;
    Put16(b, 16, 2), Put16(b, 18, 40), Put32(b, 20, 1), Put32(b, 24, kText);
    Put32(b, 28, 52), Put16(b, 40, 52), Put16(b, 42, 32), Put16(b, 44, 1);
    Put32(b, 52, 1);  // PT_LOAD
    Put32(b, 52 + 4, 84), Put32(b, 52 + 8, kText), Put32(b, 52 + 12, kText);
    Put32(b, 52 + 16, 0x40), Put32(b, 52 + 20, 0x40), Put32(b, 52 + 24, 5), Put32(b, 52 + 28, 0x1000);
    for (u32 i = 0; i < 0x40; ++i) {
        b.push_back(static_cast<u8>(i));
    }
    return b;
}

class TempDir {
public:
    TempDir() {
        static std::atomic<int> counter{0};
        path_ = fs::temp_directory_path() /
                ("mh4n_applet_frontend_test_" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
                 std::to_string(counter++));
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }
    std::string User() const { return (path_ / "user").string(); }
    std::string WriteElf() const {
        const std::string path = (path_ / "program.elf").string();
        const std::vector<u8> bytes = MakeElf();
        std::ofstream(path, std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return path;
    }

private:
    fs::path path_;
};

// Every level is seen, Trace included: the privacy checks look at all of them.
struct Fixture {
    std::unique_ptr<Runtime> runtime;
    std::vector<std::string> logged;
    std::mutex mutex;
    Fixture() {
        RuntimeConfig config;
        config.time = std::make_unique<ManualTimeSource>();
        runtime = std::make_unique<Runtime>(std::move(config));
        SetLogFilter(LogFilter(LogLevel::Trace));
        SetLogSink([this](const LogRecord& r) {
            std::lock_guard lock(mutex);
            logged.push_back(std::string(r.message));
        });
    }
    ~Fixture() {
        runtime.reset();
        SetLogSink({});
        SetLogFilter(LogFilter(LogLevel::Info));
    }
    std::size_t Count(const std::string& needle) {
        std::lock_guard lock(mutex);
        return static_cast<std::size_t>(std::count_if(logged.begin(), logged.end(), [&](const std::string& l) {
            return l.find(needle) != std::string::npos;
        }));
    }
};

int BootWith(Fixture& f, const TempDir& dir, std::vector<std::string> extra, std::function<void()> body) {
    std::vector<std::string> args = {dir.WriteElf(), "--user", dir.User(), "--ms", "20000"};
    args.insert(args.end(), extra.begin(), extra.end());
    const Options options = ParseOptions("runner", args);
    REQUIRE_MESSAGE(options.ok(), options.error());
    f.runtime->SetMainThreadEntry([body = std::move(body)](GuestThread&) { body(); });
    return f.runtime->Boot(options);
}

struct Regs {
    std::array<u32, 16> r{};
};

Regs CallSvc(Runtime& rt, u32 number, std::initializer_list<u32> in) {
    RCtx ctx{};
    int i = 0;
    for (const u32 value : in) {
        ctx.r[i++] = value;
    }
    ctx.r[14] = kText + 0x20;
    rt.Svc().CallSvc(ctx, number, kText + 0x11);
    Regs out;
    for (int k = 0; k < 16; ++k) {
        out.r[static_cast<std::size_t>(k)] = ctx.r[k];
    }
    return out;
}

void ExitProcess(Runtime& rt) {
    CallSvc(rt, 0x03, {});
}
void SleepNs(Runtime& rt, u64 ns) {
    CallSvc(rt, 0x0A, {static_cast<u32>(ns), static_cast<u32>(ns >> 32)});
}
void SleepToTick(Runtime& rt, u64 tick) {
    const u64 now = rt.Clock().Now();
    if (tick > now) {
        SleepNs(rt, TicksToNs(tick - now));
    }
}

constexpr Result kNoData = 0xC8A0CFEF;
constexpr u32 kApplication = 0x300;
constexpr u32 kKeyboard = 0x401;
constexpr u32 kError = 0x206;
constexpr u32 kRequest = 2, kMessage = 5, kWakeupByExit = 0xA;
constexpr u64 kTick = apt::kAppletUpdateIntervalTicks;

struct Guest {
    Runtime& rt;
    explicit Guest(Runtime& runtime) : rt(runtime) { FindServiceManager(rt)->SetReplyDelayNs(0); }

    u32 Cmd() const { return CommandBufferAddress(*rt.MainThread()); }
    u32 Scratch(u32 offset = 0) const { return rt.MainThread()->stack_top - 0x3000 + offset; }
    void PutWords(u32 address, const std::vector<u32>& words) {
        REQUIRE(rt.Memory().WriteBlock(address, words.data(), static_cast<u32>(words.size() * 4)));
    }
    void PutBytes(u32 address, const std::vector<u8>& bytes) {
        if (!bytes.empty()) {
            REQUIRE(rt.Memory().WriteBlock(address, bytes.data(), static_cast<u32>(bytes.size())));
        }
    }
    std::vector<u8> GetBytes(u32 address, std::size_t count) {
        std::vector<u8> bytes(count);
        if (count != 0) {
            REQUIRE(rt.Memory().ReadBlock(bytes.data(), address, static_cast<u32>(count)));
        }
        return bytes;
    }
    std::vector<u32> Request(Handle session, const std::vector<u32>& words, std::size_t reply_words) {
        PutWords(Cmd(), words);
        CallSvc(rt, 0x32, {session});
        std::vector<u32> reply(reply_words);
        REQUIRE(rt.Memory().ReadBlock(reply.data(), Cmd(), static_cast<u32>(reply_words * 4)));
        return reply;
    }
    Handle Service(const std::string& name) {
        PutBytes(Scratch(0x2000), std::vector<u8>{'s', 'r', 'v', ':', 0});
        const Regs port = CallSvc(rt, 0x2D, {0, Scratch(0x2000)});
        REQUIRE(port.r[0] == kResultSuccess);
        std::array<u32, 2> words{};
        std::memcpy(words.data(), name.data(), std::min<std::size_t>(name.size(), 8));
        const std::vector<u32> reply = Request(
            port.r[1], {ipc::MakeHeader(0x0005, 4, 0), words[0], words[1], static_cast<u32>(name.size()), 1}, 4);
        REQUIRE(reply[1] == kResultSuccess);
        return reply[3];
    }
};

struct Param {
    Result result = 0;
    u32 sender = 0, signal = 0, size = 0;
    std::vector<u8> data;
};

struct Apt {
    Guest& g;
    Handle h;

    Result Simple(u16 command, std::vector<u32> params) {
        std::vector<u32> words = {ipc::MakeHeader(command, static_cast<u32>(params.size()), 0)};
        words.insert(words.end(), params.begin(), params.end());
        return g.Request(h, words, 2)[1];
    }
    Param Receive(u32 size) {
        const u32 buffer = g.Scratch(0x400);
        g.PutBytes(buffer, std::vector<u8>(size, 0xEE));
        g.PutWords(g.Cmd() + 0x100, {ipc::StaticBufferDesc(size, 0), buffer});
        const std::vector<u32> raw = g.Request(h, {ipc::MakeHeader(0x000D, 2, 0), kApplication, size}, 9);
        Param p;
        p.result = raw[1];
        if (raw[0] == ipc::MakeHeader(0x000D, 4, 4)) {
            p.sender = raw[2], p.signal = raw[3], p.size = raw[4];
            const std::vector<u8> all = g.GetBytes(buffer, size);
            p.data.assign(all.begin(), all.begin() + std::min<u32>(p.size, size));
        }
        return p;
    }
    Result Send(u32 destination, u32 signal, const std::vector<u8>& data) {
        const u32 buffer = g.Scratch(0x800);
        g.PutBytes(buffer, data);
        return g.Request(h, {ipc::MakeHeader(0x000C, 4, 4), kApplication, destination, signal,
                             static_cast<u32>(data.size()), ipc::CopyHandleDesc(1), 0,
                             ipc::StaticBufferDesc(data.size(), 0), buffer}, 2)[1];
    }
    void StartApplication() {
        REQUIRE(g.Request(h, {ipc::MakeHeader(0x0001, 1, 0), 0}, 6)[1] == kResultSuccess);
        REQUIRE(g.Request(h, {ipc::MakeHeader(0x0002, 2, 0), kApplication, 0}, 5)[1] == kResultSuccess);
        REQUIRE(Simple(0x0003, {0}) == kResultSuccess);
        REQUIRE(Receive(0x10).result == kResultSuccess);  // the Wakeup
    }
    Result StartLibraryApplet(u32 applet, Handle object, const std::vector<u8>& data) {
        const u32 buffer = g.Scratch(0xC00);
        g.PutBytes(buffer, data);
        return g.Request(h, {ipc::MakeHeader(0x001E, 2, 4), applet, static_cast<u32>(data.size()),
                             ipc::CopyHandleDesc(1), object, ipc::StaticBufferDesc(data.size(), 0), buffer},
                         2)[1];
    }
    // PrepareToStartLibraryApplet, the Request and its Response, StartLibraryApplet: the tick the applet was
    // made at (its update ticks are that plus whole periods) and the result of the start.
    std::pair<u64, Result> Launch(u32 applet, Handle object, const std::vector<u8>& data) {
        const u64 made = g.rt.Clock().Now();
        REQUIRE(Simple(0x0018, {applet}) == kResultSuccess);
        std::vector<u8> info(0x20, 0);
        Put32(info, 0, 0x8000);
        REQUIRE(Send(applet, kRequest, info) == kResultSuccess);
        REQUIRE(Receive(0x20).result == kResultSuccess);
        return {made, StartLibraryApplet(applet, object, data)};
    }
};

// The keyboard's configuration at 3dbrew's offsets.
namespace off {
constexpr std::size_t kType = 0x00, kButtons = 0x04, kValidInput = 0x08, kPassword = 0x0C, kFilterFlags = 0x18;
constexpr std::size_t kMaxTextLength = 0x20, kMaxDigits = 0x24, kButtonText = 0x26, kHint = 0x90;
constexpr std::size_t kMultiline = 0x113, kInitialTextOffset = 0x120, kSharedMemorySize = 0x130;
constexpr std::size_t kReturnCode = 0x138, kTextOffset = 0x144, kTextLength = 0x148;
constexpr std::size_t kCallbackResult = 0x14C, kCallbackMessage = 0x150;
}  // namespace off

std::vector<u8> MakeConfig(u32 buttons, u32 max_text, u32 valid_input, u32 filter_flags) {
    std::vector<u8> c(0x400, 0);
    Put32(c, off::kButtons, buttons);
    Put32(c, off::kValidInput, valid_input);
    Put32(c, off::kFilterFlags, filter_flags);
    Put16(c, off::kMaxTextLength, max_text);
    Put16(c, off::kMaxDigits, 2);
    Put32(c, off::kSharedMemorySize, 0x2000);
    Put32(c, off::kInitialTextOffset, 0xFFFFFFFF);
    return c;
}

// A block of the application's heap for the text, filled with a pattern (made once per run).
constexpr u32 kHeap = 0x08000000;
struct TextBlock {
    u32 address = 0;
    Handle handle = 0;
};
TextBlock MakeTextBlock(Guest& g) {
    REQUIRE(CallSvc(g.rt, 0x01, {3, kHeap, 0, 0x10000, 3}).r[0] == kResultSuccess);
    TextBlock block;
    block.address = kHeap;
    g.PutBytes(block.address, std::vector<u8>(0x2000, 0x5A));
    const Regs out = CallSvc(g.rt, 0x1E, {3, block.address, 0x2000, 3});
    REQUIRE(out.r[0] == kResultSuccess);
    block.handle = out.r[1];
    return block;
}
std::u16string ReadText(Guest& g, u32 address) {
    const std::vector<u8> bytes = g.GetBytes(address, 128);
    std::u16string text;
    for (std::size_t i = 0; i < 64 && Get16(bytes, 2 * i) != 0; ++i) {
        text.push_back(static_cast<char16_t>(Get16(bytes, 2 * i)));
    }
    return text;
}

// The next update tick of an applet made at `made`, after the clock's reading.
u64 NextUpdate(Runtime& rt, u64 made) {
    const u64 now = rt.Clock().Now();
    return made + ((now - made) / kTick + 1) * kTick;
}

// A frontend that records what it is shown.
class Recording final : public KeyboardFrontend {
public:
    void Execute(const KeyboardRequest& request) override {
        std::lock_guard lock(mutex_);
        requests.push_back(request);
    }
    void ShowError(const std::u16string& message) override {
        std::lock_guard lock(mutex_);
        errors.push_back(message);
    }
    std::size_t Executed() {
        std::lock_guard lock(mutex_);
        return requests.size();
    }
    std::vector<KeyboardRequest> requests;
    std::vector<std::u16string> errors;

private:
    std::mutex mutex_;
};

class Notices final : public ErrorFrontend {
public:
    void Show(u32 error_code, const std::u16string& text) override { shown.emplace_back(error_code, text); }
    std::vector<std::pair<u32, std::u16string>> shown;
};

// FinishKeyboard from another host thread, as the app's UI thread calls it.
void FinishFromAnotherThread(Runtime& rt, std::u16string text, u8 button) {
    std::thread ui([&rt, text = std::move(text), button] { FinishKeyboard(rt, text, button); });
    ui.join();
}

}  // namespace

TEST_CASE("the keyboard frontend gets the request at StartLibraryApplet, and FinishKeyboard's answer from "
          "another thread finishes the applet at the next update tick") {
    Fixture f;
    TempDir dir;
    auto frontend = std::make_shared<Recording>();
    SetKeyboardFrontend(*f.runtime, frontend);
    Result start = 1;
    std::size_t executed_at_start = 0;
    Param before_answer, before_tick, closing;
    std::u16string text;
    const int status = BootWith(f, dir, {}, [&] {
        Guest g(*f.runtime);
        Apt apt{g, g.Service("APT:U")};
        apt.StartApplication();
        const TextBlock block = MakeTextBlock(g);
        // the initial text where the configuration says, in the text block
        std::vector<u8> initial(0x20, 0);
        PutText(initial, 0, u"Prefill");
        g.PutBytes(block.address + 0x100, initial);
        std::vector<u8> config = MakeConfig(1, 10, 2, apt::kFilterAt);
        Put32(config, off::kType, 2);
        Put32(config, off::kPassword, 1);
        config[off::kMultiline] = 1;
        PutText(config, off::kButtonText, u"Back");
        PutText(config, off::kButtonText + 2 * 2 * 17, u"Done");
        PutText(config, off::kHint, u"SecretHint");
        Put32(config, off::kInitialTextOffset, 0x100);
        const auto launched = apt.Launch(kKeyboard, block.handle, config);
        start = launched.second;
        executed_at_start = frontend->Executed();
        // no answer: no closing, however many ticks pass
        SleepNs(*f.runtime, TicksToNs(3 * kTick));
        before_answer = apt.Receive(0x400);
        FinishFromAnotherThread(*f.runtime, u"Zinogre", 1);
        const u64 next = NextUpdate(*f.runtime, launched.first);
        SleepToTick(*f.runtime, next - 100);
        before_tick = apt.Receive(0x400);
        SleepToTick(*f.runtime, next + 100);
        closing = apt.Receive(0x400);
        text = ReadText(g, block.address);
        ExitProcess(*f.runtime);
    });
    CHECK(status == 0);
    CHECK(start == kResultSuccess);
    CHECK(executed_at_start == 1);
    REQUIRE(frontend->requests.size() == 1);
    const KeyboardRequest& r = frontend->requests[0];
    CHECK(r.type == KeyboardType::Numpad);
    CHECK(r.buttons == KeyboardButtons::Dual);
    CHECK(r.OkButton() == 1);
    CHECK(r.button_text[0] == u"Back");
    CHECK(r.button_text[1].empty());
    CHECK(r.button_text[2] == u"Done");
    CHECK(r.hint == u"SecretHint");
    CHECK(r.initial_text == u"Prefill");
    CHECK(r.max_text_length == 10);
    CHECK(r.accept == KeyboardAccept::NotEmptyAndNotBlank);
    CHECK(r.password == KeyboardPassword::Hide);
    CHECK(r.multiline);
    CHECK(r.filters.at);
    CHECK_FALSE(r.filters.digits);
    CHECK(r.max_digits == 2);
    CHECK(before_answer.result == kNoData);
    CHECK(before_tick.result == kNoData);
    CHECK(closing.result == kResultSuccess);
    CHECK(closing.sender == kKeyboard);
    CHECK(closing.signal == kWakeupByExit);
    REQUIRE(closing.data.size() == 0x400);
    CHECK(static_cast<s32>(Get32(closing.data, off::kReturnCode)) == 2);  // D1Click1: OK of two
    CHECK(Get16(closing.data, off::kTextLength) == 7);
    CHECK(Get32(closing.data, off::kTextOffset) == 0);
    CHECK(text == u"Zinogre");
    // rule 5: neither the text nor the hint nor the initial text in any log line, at any level
    CHECK(f.Count("Zinogre") == 0);
    CHECK(f.Count("SecretHint") == 0);
    CHECK(f.Count("Prefill") == 0);
    CHECK(f.Count("the app's keyboard is shown") == 1);
}

TEST_CASE("a text the checks turn down is dropped and the frontend is asked again; Cancel needs no check") {
    Fixture f;
    TempDir dir;
    auto frontend = std::make_shared<Recording>();
    SetKeyboardFrontend(*f.runtime, frontend);
    Param after_rejected, closing, second_closing;
    std::size_t executed_after_rejected = 0;
    std::u16string text;
    const int status = BootWith(f, dir, {}, [&] {
        Guest g(*f.runtime);
        Apt apt{g, g.Service("APT:U")};
        apt.StartApplication();
        const TextBlock block = MakeTextBlock(g);
        const std::vector<u8> config = MakeConfig(1, 10, 2, apt::kFilterAt);
        const u64 made = apt.Launch(kKeyboard, block.handle, config).first;
        FinishFromAnotherThread(*f.runtime, u"Me@Turned", 1);  // the @ filter
        SleepToTick(*f.runtime, NextUpdate(*f.runtime, made) + 100);
        after_rejected = apt.Receive(0x400);
        executed_after_rejected = frontend->Executed();
        // the same request again; this time an accepted text
        FinishFromAnotherThread(*f.runtime, u"Fine", 1);
        SleepToTick(*f.runtime, NextUpdate(*f.runtime, made) + 100);
        closing = apt.Receive(0x400);
        text = ReadText(g, block.address);
        // a second keyboard, cancelled: the left button, no check, empty text
        const u64 made2 = apt.Launch(kKeyboard, block.handle, config).first;
        FinishFromAnotherThread(*f.runtime, u"", 0);
        SleepToTick(*f.runtime, NextUpdate(*f.runtime, made2) + 100);
        second_closing = apt.Receive(0x400);
        ExitProcess(*f.runtime);
    });
    CHECK(status == 0);
    CHECK(after_rejected.result == kNoData);
    CHECK(executed_after_rejected == 2);
    REQUIRE(frontend->requests.size() == 3);
    CHECK(frontend->requests[1].max_text_length == frontend->requests[0].max_text_length);
    CHECK(closing.result == kResultSuccess);
    CHECK(closing.signal == kWakeupByExit);
    CHECK(text == u"Fine");
    CHECK(second_closing.result == kResultSuccess);
    REQUIRE(second_closing.data.size() == 0x400);
    CHECK(static_cast<s32>(Get32(second_closing.data, off::kReturnCode)) == 1);  // D1Click0: Cancel
    CHECK(Get16(second_closing.data, off::kTextLength) == 0);
    CHECK(f.Count("it is asked again") == 1);
    CHECK(f.Count("Me@Turned") == 0);
    CHECK(f.Count("Fine") == 0);
}

TEST_CASE("an answer given while no keyboard is open does not answer the next one") {
    Fixture f;
    TempDir dir;
    auto frontend = std::make_shared<Recording>();
    SetKeyboardFrontend(*f.runtime, frontend);
    Param stale;
    const int status = BootWith(f, dir, {}, [&] {
        Guest g(*f.runtime);
        Apt apt{g, g.Service("APT:U")};
        apt.StartApplication();
        const TextBlock block = MakeTextBlock(g);
        FinishFromAnotherThread(*f.runtime, u"Early", 0);
        apt.Launch(kKeyboard, block.handle, MakeConfig(0, 10, 0, 0));
        SleepNs(*f.runtime, TicksToNs(3 * kTick));
        stale = apt.Receive(0x400);
        ExitProcess(*f.runtime);
    });
    CHECK(status == 0);
    CHECK(frontend->Executed() == 1);
    CHECK(stale.result == kNoData);
}

TEST_CASE("the callback filter with a frontend: the game's message goes to ShowError, and Continue asks "
          "the frontend again") {
    Fixture f;
    TempDir dir;
    auto frontend = std::make_shared<Recording>();
    SetKeyboardFrontend(*f.runtime, frontend);
    Param message, second_message, closing;
    std::size_t executed_after_continue = 0;
    const int status = BootWith(f, dir, {}, [&] {
        Guest g(*f.runtime);
        Apt apt{g, g.Service("APT:U")};
        apt.StartApplication();
        const TextBlock block = MakeTextBlock(g);
        const u64 made = apt.Launch(kKeyboard, block.handle, MakeConfig(0, 16, 0, apt::kFilterCallback)).first;
        FinishFromAnotherThread(*f.runtime, u"Abc", 0);
        SleepToTick(*f.runtime, NextUpdate(*f.runtime, made) + 100);
        message = apt.Receive(0x400);
        std::vector<u8> verdict = message.data;
        REQUIRE(verdict.size() == 0x400);
        Put32(verdict, off::kCallbackResult, 2);  // Continue
        PutText(verdict, off::kCallbackMessage, u"CallbackSecret");
        REQUIRE(apt.Send(kKeyboard, kMessage, verdict) == kResultSuccess);
        executed_after_continue = frontend->Executed();
        FinishFromAnotherThread(*f.runtime, u"Def", 0);
        SleepToTick(*f.runtime, NextUpdate(*f.runtime, made) + 100);
        second_message = apt.Receive(0x400);
        verdict = second_message.data;
        REQUIRE(verdict.size() == 0x400);
        Put32(verdict, off::kCallbackResult, 0);  // OK
        REQUIRE(apt.Send(kKeyboard, kMessage, verdict) == kResultSuccess);
        closing = apt.Receive(0x400);
        ExitProcess(*f.runtime);
    });
    CHECK(status == 0);
    CHECK(message.signal == kMessage);
    CHECK(Get16(message.data, off::kTextLength) == 3);
    CHECK(executed_after_continue == 2);
    REQUIRE(frontend->errors.size() == 1);
    CHECK(frontend->errors[0] == u"CallbackSecret");
    CHECK(second_message.signal == kMessage);
    CHECK(Get16(second_message.data, off::kTextLength) == 3);
    CHECK(closing.result == kResultSuccess);
    CHECK(closing.signal == kWakeupByExit);
    CHECK(f.Count("CallbackSecret") == 0);
}

TEST_CASE("the Error frontend is shown the code and the message while the applet still closes inside "
          "StartLibraryApplet; the EULA types are not shown") {
    for (const u8 type : {u8{0}, u8{1}, u8{5}}) {
        INFO("display type " << int(type));
        Fixture f;
        TempDir dir;
        auto notices = std::make_shared<Notices>();
        SetErrorFrontend(*f.runtime, notices);
        std::vector<u8> param(0xF80, 0);
        param[0] = type;
        Put32(param, 0x004, 0xFFFFF5E9);
        PutText(param, 0x00C, u"NetSecret");
        u64 before_start = 0, after_start = 1;
        std::size_t shown_inside = 0;
        Param closing;
        Result start = 1;
        const int status = BootWith(f, dir, {}, [&] {
            Guest g(*f.runtime);
            Apt apt{g, g.Service("APT:U")};
            apt.StartApplication();
            REQUIRE(apt.Simple(0x0018, {kError}) == kResultSuccess);
            std::vector<u8> info(0x20, 0);
            Put32(info, 0, 0x8000);
            REQUIRE(apt.Send(kError, kRequest, info) == kResultSuccess);
            REQUIRE(apt.Receive(0x20).result == kResultSuccess);
            before_start = f.runtime->Clock().Now();
            start = apt.StartLibraryApplet(kError, 0, param);
            after_start = f.runtime->Clock().Now();
            shown_inside = notices->shown.size();
            closing = apt.Receive(0xF80);
            ExitProcess(*f.runtime);
        });
        CHECK(status == 0);
        CHECK(start == kResultSuccess);
        CHECK(after_start == before_start);  // closed inside the command, at no update tick
        CHECK(closing.result == kResultSuccess);
        CHECK(closing.signal == kWakeupByExit);
        if (type == 5) {
            CHECK(shown_inside == 0);
        } else {
            REQUIRE(shown_inside == 1);
            CHECK(notices->shown[0].first == 0xFFFFF5E9);
            CHECK(notices->shown[0].second == u"NetSecret");
        }
        CHECK(f.Count("NetSecret") == 0);
    }
}

TEST_CASE("with no keyboard frontend the default keyboard answers as before, and FinishKeyboard does nothing") {
    Fixture f;
    TempDir dir;
    SetErrorFrontend(*f.runtime, std::make_shared<Notices>());  // an error frontend alone changes no keyboard
    Param closing;
    std::u16string text;
    const int status = BootWith(f, dir, {"--swkbd-text", "Default"}, [&] {
        Guest g(*f.runtime);
        Apt apt{g, g.Service("APT:U")};
        apt.StartApplication();
        const TextBlock block = MakeTextBlock(g);
        const u64 made = apt.Launch(kKeyboard, block.handle, MakeConfig(0, 10, 0, 0)).first;
        FinishFromAnotherThread(*f.runtime, u"Ignored", 0);
        SleepToTick(*f.runtime, made + kTick + 100);
        closing = apt.Receive(0x400);
        text = ReadText(g, block.address);
        ExitProcess(*f.runtime);
    });
    CHECK(status == 0);
    CHECK(closing.result == kResultSuccess);
    CHECK(closing.signal == kWakeupByExit);
    CHECK(text == u"Default");
    CHECK(f.Count("no keyboard frontend was set") == 1);
}

TEST_CASE("--test-keyboard-frontend answers each keyboard from another thread through FinishKeyboard") {
    Fixture f;
    TempDir dir;
    Param closing;
    std::u16string text;
    const int status = BootWith(f, dir, {"--test-keyboard-frontend", "Zinogre"}, [&] {
        Guest g(*f.runtime);
        Apt apt{g, g.Service("APT:U")};
        apt.StartApplication();
        const TextBlock block = MakeTextBlock(g);
        const u64 made = apt.Launch(kKeyboard, block.handle, MakeConfig(1, 10, 2, 0)).first;
        // the stand-in answers 50 ms of host time after Execute; the game clock only moves when the guest
        // sleeps here
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        SleepToTick(*f.runtime, NextUpdate(*f.runtime, made) + 100);
        closing = apt.Receive(0x400);
        text = ReadText(g, block.address);
        ExitProcess(*f.runtime);
    });
    CHECK(status == 0);
    CHECK(closing.result == kResultSuccess);
    CHECK(closing.signal == kWakeupByExit);
    REQUIRE(closing.data.size() == 0x400);
    CHECK(static_cast<s32>(Get32(closing.data, off::kReturnCode)) == 2);
    CHECK(text == u"Zinogre");
    CHECK(f.Count("Zinogre") == 0);
}

#endif  // recomp.h
