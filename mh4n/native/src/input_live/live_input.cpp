#include "input_live/live_input.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "input_script/script_input.h"
#include "mh4n/clock.h"
#include "mh4n/log.h"
#include "mh4n/options.h"
#include "mh4n/pacing_trace.h"
#include "mh4n/registry.h"
#include "mh4n/runtime.h"

namespace mh4n::input_live {

namespace {

constexpr const char* kLog = "Input.Live";

// Live sources by runtime, for Find. An entry goes with its source.
struct Entry {
    LiveInput* source;
    std::weak_ptr<LiveInput> handle;
};
std::mutex g_registry_mutex;
std::map<const Runtime*, Entry>& Registry() {
    static std::map<const Runtime*, Entry> registry;
    return registry;
}

}  // namespace

MH4N_OPTION("runner", "--input-latch", "MODE",
            "live input (--input-source live): frame (default) = the game sees a change only at "
            "its next frame, so the session replays; none = at once");
MH4N_OPTION("runner", "--record-input", "FILE",
            "live input: write what the game saw as a script that --input-file replays (frame "
            "keys; ms of game time before the frame counter is seen), when the run ends");

LiveInput::LiveInput(Runtime* runtime, u64 start_tick, Latch latch)
    : runtime_(runtime), start_tick_(start_tick), latch_(latch) {}

LiveInput::~LiveInput() {
    std::lock_guard lock(g_registry_mutex);
    auto& registry = Registry();
    for (auto it = registry.begin(); it != registry.end();) {
        it = it->second.source == this ? registry.erase(it) : std::next(it);
    }
}

void LiveInput::Set(const InputState& state) {
    std::lock_guard lock(mutex_);
    InputState next = state;
    next.touch_down = pending_.touch_down;
    next.touch_x = pending_.touch_x;
    next.touch_y = pending_.touch_y;
    pending_ = next;
}

// The game frames a touch stays down at least (a quick tap, released before the game read it, is held this long).
// MH4N_TOUCH_HOLD=0 turns it off.
static u32 TouchHoldFrames() {
    static const u32 frames = [] {
        const char* v = std::getenv("MH4N_TOUCH_HOLD");
        return v != nullptr ? static_cast<u32>(std::atoi(v)) : 2u;
    }();
    return frames;
}

void LiveInput::SetTouch(std::optional<TouchPoint> touch) {
    std::lock_guard lock(mutex_);
    if (touch) {
        if (!pending_.touch_down || release_requested_) press_stamped_ = false;  // (a new press: Sample stamps its frame)
        release_requested_ = false;
        pending_.touch_down = true;
        pending_.touch_x = touch->x;
        pending_.touch_y = touch->y;
        return;
    }
    if (pending_.touch_down && TouchHoldFrames() > 0) {  // (released by Sample once the game has seen it)
        release_requested_ = true;
        return;
    }
    pending_.touch_down = false;
    pending_.touch_x = 0;
    pending_.touch_y = 0;
}

InputState LiveInput::Snapshot() const {
    std::lock_guard lock(mutex_);
    return pending_;
}

Stamp LiveInput::StampAt(u64 tick) const {
    Stamp at;
    at.have_frame = frame_seen_;
    at.frame = frame_;
    // The script source's own formula (ScriptInput::Sample), so a recorded time reads back exactly.
    at.ms = static_cast<double>(tick - start_tick_) / (static_cast<double>(kTicksPerSecond) / 1000.0);
    return at;
}

void LiveInput::OnFrame(u32 frame, u64 tick) {
    frame_seen_ = true;
    frame_ = frame;
    if (latch_ == Latch::Frame) {
        const InputState before = latched_;
        latched_ = Snapshot();
        if (!(latched_ == before)) {
            pacing::Mark(pacing::kInputLatch, latched_.buttons);  // (the pacing trace: an input's way to the screen)
        }
        if (recording_) {
            recorder_.Update(latched_, StampAt(tick));
        }
    }
}

InputState LiveInput::Sample(u64 tick) {
    // The frame counter as it is now, as the script source looks at it: a change is latched
    // before this sample is answered.
    if (runtime_ != nullptr) {
        runtime_->PollFrame();
    }
    {  // (a touch's press frame, and a release held until the game has seen the press for TouchHoldFrames frames)
        std::lock_guard lock(mutex_);
        if (pending_.touch_down && !press_stamped_) press_stamped_ = true, press_frame_ = frame_;
        if (release_requested_ && (!frame_seen_ || frame_ - press_frame_ >= TouchHoldFrames())) {
            release_requested_ = false;
            pending_.touch_down = false;
            pending_.touch_x = 0;
            pending_.touch_y = 0;
        }
    }
    const InputState state = (latch_ == Latch::Frame && frame_seen_) ? latched_ : Snapshot();
    if (!(state == sampled_)) {
        sampled_ = state;
        pacing::Mark(pacing::kInputSample, state.buttons);
    }
    if (recording_) {
        recorder_.Update(state, StampAt(tick));
    }
    return state;
}

void LiveInput::StartRecording() {
    recording_ = true;
}

std::string LiveInput::FinishRecording(u64 tick) {
    recording_ = false;
    return recorder_.Finish(StampAt(tick));
}

std::shared_ptr<LiveInput> Find(Runtime& runtime) {
    std::lock_guard lock(g_registry_mutex);
    const auto it = Registry().find(&runtime);
    return it == Registry().end() ? nullptr : it->second.handle.lock();
}

namespace {

// What the runtime owns: it forwards to the shared source, which Find hands out.
class Forwarder final : public InputSource {
public:
    explicit Forwarder(std::shared_ptr<LiveInput> source) : source_(std::move(source)) {}
    InputState Sample(u64 tick) override { return source_->Sample(tick); }

private:
    std::shared_ptr<LiveInput> source_;
};

// The live source of `rt`, made and registered for Find (with --input-latch and --record-input)
std::shared_ptr<LiveInput> MakeLiveSource(Runtime& rt) {
    const Options& options = rt.GetOptions();

    Latch latch = Latch::Frame;
    const std::string mode = options.GetString("--input-latch", "frame");
    if (mode == "none") {
        latch = Latch::None;
    } else if (mode != "frame") {
        const std::string message = "--input-latch: '" + mode + "' is not frame or none";
        MH4N_LOG(Error, kLog, "{}", message);
        rt.RequestExit(2, message);
    }

    // Recorded time 0 is where the game clock stands when the devices are made (as the script source's).
    auto source = std::make_shared<LiveInput>(&rt, rt.Clock().Now(), latch);
    rt.OnFrame([source](u32 frame, u64 tick) { source->OnFrame(frame, tick); });

    if (const std::optional<std::string> path = options.Get("--record-input")) {
        // Fail now, not when the run is over, on a file that cannot be written.
        if (!std::ofstream(*path, std::ios::binary | std::ios::trunc)) {
            const std::string message = "--record-input: cannot write " + *path;
            MH4N_LOG(Error, kLog, "{}", message);
            rt.RequestExit(2, message);
        } else {
            source->StartRecording();
            rt.OnFinish([source, &rt, file = *path] {
                const std::string text = source->FinishRecording(rt.Clock().Now());
                std::ofstream out(file, std::ios::binary | std::ios::trunc);
                out << text;
                out.flush();
                if (!out) {
                    MH4N_LOG(Error, kLog, "--record-input: cannot write {}", file);
                } else {
                    MH4N_LOG(Info, kLog, "recorded input written to {}", file);
                }
            });
        }
    }

    {
        std::lock_guard lock(g_registry_mutex);
        Registry()[&rt] = Entry{source.get(), source};
    }
    return source;
}

std::unique_ptr<InputSource> MakeLiveInput(Runtime& rt) {
    return std::make_unique<Forwarder>(MakeLiveSource(rt));
}

MH4N_REGISTER(InputSourceFactory, "live", InputSourceFactory(MakeLiveInput));

#if MH4N_HAVE_SCHEDULE
// `--input-source mixed`, a test aid: the presses of --input and --input-file, as the script source plays them, and the
// app's live input together. A script takes the game somewhere (a hunt), and then a person, or adb's taps on the app's
// touch controls, plays: the buttons of both, the circle pad, the C-stick and the touch screen from the live input
// while it has them, else from the script.
class Mixed final : public InputSource {
public:
    Mixed(std::unique_ptr<input_script::ScriptInput> script, std::shared_ptr<LiveInput> live)
        : script_(std::move(script)), live_(std::move(live)) {}
    InputState Sample(u64 tick) override {
        InputState s = script_->Sample(tick);
        const InputState l = live_->Sample(tick);
        s.buttons |= l.buttons;
        if (l.circle_x != 0 || l.circle_y != 0) s.circle_x = l.circle_x, s.circle_y = l.circle_y;
        if (l.cstick_x != 0 || l.cstick_y != 0) s.cstick_x = l.cstick_x, s.cstick_y = l.cstick_y;
        if (l.touch_down) s.touch_down = true, s.touch_x = l.touch_x, s.touch_y = l.touch_y;
        s.zl = s.zl || l.zl;
        s.zr = s.zr || l.zr;
        return s;
    }

private:
    std::unique_ptr<input_script::ScriptInput> script_;
    std::shared_ptr<LiveInput> live_;
};

std::unique_ptr<InputSource> MakeMixedInput(Runtime& rt) {
    Schedule::Plan plan;
    std::string error;
    if (!input_script::ParseScripts(rt.GetOptions().All("--input"), rt.GetOptions().All("--input-file"), &plan, &error)) {
        MH4N_LOG(Error, kLog, "{}", error);
        rt.RequestExit(2, error);
        plan = Schedule::Plan{};
    }
    auto script = std::make_unique<input_script::ScriptInput>(std::move(plan), &rt, rt.Clock().Now());
    rt.OnFrame([cell = script->FrameCell()](u32 frame, u64) { *cell = frame; });
    MH4N_LOG(Info, kLog, "mixed input: the script's presses and the live input together");
    return std::make_unique<Mixed>(std::move(script), MakeLiveSource(rt));
}

MH4N_REGISTER(InputSourceFactory, "mixed", InputSourceFactory(MakeMixedInput));
#endif

// Options that only the live source reads, given to another one, would do nothing without a word.
void WarnUnusedOptions(Runtime& rt) {
    const Options& options = rt.GetOptions();
    if ((options.Has("--record-input") || options.Has("--input-latch")) && Find(rt) == nullptr) {
        MH4N_LOG(Warning, kLog, "--record-input and --input-latch only apply to --input-source live");
    }
}

MH4N_REGISTER(BootStep, "input_live.warn", BootStep{Phase::Services, WarnUnusedOptions});

}  // namespace

}  // namespace mh4n::input_live
