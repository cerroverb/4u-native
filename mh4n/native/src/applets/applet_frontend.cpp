// The applets' frontends (N3-12): mh4n/applet_frontend.h's functions, the hub of each runtime
// (applets/frontend_hub.h), the boot step that hands it to the applets, and --test-keyboard-frontend.
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "applets/frontend_hub.h"
#include "applets/swkbd.h"
#include "mh4n/applet_frontend.h"
#include "mh4n/log.h"
#include "mh4n/options.h"
#include "mh4n/registry.h"
#include "mh4n/runtime.h"
#include "mh4n/service.h"

namespace mh4n {

namespace apt {

namespace {

constexpr const char* kLog = "Applet.Frontend";
constexpr const char* kStateKey = "applet_frontend";

// What the service context holds: the runtime's hub, put there by the boot step.
struct FrontendSlot {
    std::shared_ptr<FrontendHub> hub;
};

// The hubs by runtime. The runtime owns its hub (Runtime::Own), so an entry expires with it.
std::mutex g_hubs_mutex;
std::map<const Runtime*, std::weak_ptr<FrontendHub>>& Hubs() {
    static std::map<const Runtime*, std::weak_ptr<FrontendHub>> hubs;
    return hubs;
}

std::shared_ptr<FrontendHub> LookUp(const Runtime& runtime) {
    std::lock_guard lock(g_hubs_mutex);
    const auto it = Hubs().find(&runtime);
    return it == Hubs().end() ? nullptr : it->second.lock();
}

std::shared_ptr<FrontendHub> MakeOrFind(Runtime& runtime) {
    std::lock_guard lock(g_hubs_mutex);
    std::weak_ptr<FrontendHub>& slot = Hubs()[&runtime];
    std::shared_ptr<FrontendHub> hub = slot.lock();
    if (!hub) {
        hub = std::make_shared<FrontendHub>();
        slot = hub;
        runtime.Own(hub);
    }
    return hub;
}

KeyboardResult ToResult(ValidationError error) {
    switch (error) {
    case ValidationError::None: return KeyboardResult::Ok;
    case ValidationError::ButtonOutOfRange: return KeyboardResult::ButtonOutOfRange;
    case ValidationError::MaxDigitsExceeded: return KeyboardResult::MaxDigitsExceeded;
    case ValidationError::AtSignNotAllowed: return KeyboardResult::AtSignNotAllowed;
    case ValidationError::PercentNotAllowed: return KeyboardResult::PercentNotAllowed;
    case ValidationError::BackslashNotAllowed: return KeyboardResult::BackslashNotAllowed;
    case ValidationError::ProfanityNotAllowed: return KeyboardResult::ProfanityNotAllowed;
    case ValidationError::CallbackFailed: return KeyboardResult::CallbackFailed;
    case ValidationError::FixedLengthRequired: return KeyboardResult::FixedLengthRequired;
    case ValidationError::MaxLengthExceeded: return KeyboardResult::MaxLengthExceeded;
    case ValidationError::BlankInputNotAllowed: return KeyboardResult::BlankInputNotAllowed;
    case ValidationError::EmptyInputNotAllowed: return KeyboardResult::EmptyInputNotAllowed;
    case ValidationError::UnknownConfiguration: return KeyboardResult::UnknownConfiguration;
    }
    return KeyboardResult::UnknownConfiguration;
}

}  // namespace

// ---- the hub ------------------------------------------------------------------------------------------------

void FrontendHub::SetKeyboard(std::shared_ptr<KeyboardFrontend> frontend) {
    std::lock_guard lock(mutex_);
    keyboard_ = std::move(frontend);
}

void FrontendHub::SetError(std::shared_ptr<ErrorFrontend> frontend) {
    std::lock_guard lock(mutex_);
    error_ = std::move(frontend);
}

std::shared_ptr<KeyboardFrontend> FrontendHub::Keyboard() const {
    std::lock_guard lock(mutex_);
    return keyboard_;
}

std::shared_ptr<ErrorFrontend> FrontendHub::Error() const {
    std::lock_guard lock(mutex_);
    return error_;
}

void FrontendHub::Post(KeyboardAnswer answer) {
    std::lock_guard lock(mutex_);
    answer_ = std::move(answer);
}

std::optional<KeyboardAnswer> FrontendHub::Take() {
    std::lock_guard lock(mutex_);
    std::optional<KeyboardAnswer> answer = std::move(answer_);
    answer_.reset();
    return answer;
}

void FrontendHub::Drop() {
    std::lock_guard lock(mutex_);
    answer_.reset();
}

std::shared_ptr<FrontendHub> FindFrontendHub(ServiceContext& context) {
    return context.State<FrontendSlot>(kStateKey)->hub;
}

namespace {

// ---- --test-keyboard-frontend: a stand-in for the app's keyboard -------------------------------------------------

// Answers each keyboard the way the app does: from another host thread, some host time after Execute, with
// ValidateKeyboard first and then FinishKeyboard, pressing the OK button. The Nth keyboard gets the Nth text
// (the last one after that). Its texts are never logged either.
class TestKeyboard final : public KeyboardFrontend {
public:
    TestKeyboard(Runtime& runtime, std::vector<std::u16string> texts)
        : runtime_(runtime), texts_(std::move(texts)) {}
    ~TestKeyboard() override {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void Execute(const KeyboardRequest& request) override {
        const std::u16string text = texts_.empty() ? std::u16string()
                                                   : texts_[std::min(shown_, texts_.size() - 1)];
        ++shown_;
        const u8 button = request.OkButton();
        const KeyboardResult verdict = ValidateKeyboard(request, text, button);
        MH4N_LOG(Info, kLog, "test keyboard frontend: keyboard {} (up to {} units): {} answer in 50 ms",
                 shown_, request.max_text_length,
                 verdict == KeyboardResult::Ok ? "an accepted" : "a turned-down");
        if (worker_.joinable()) {
            worker_.join();
        }
        worker_ = std::thread([this, text, button] {
            std::unique_lock lock(mutex_);
            if (wake_.wait_for(lock, std::chrono::milliseconds(50), [this] { return stopping_; })) {
                return;
            }
            lock.unlock();
            FinishKeyboard(runtime_, text, button);  // the runtime is only the key of its hub here
        });
    }

    void ShowError(const std::u16string& message) override {
        MH4N_LOG(Info, kLog, "test keyboard frontend: the game's check turned the text down ({} units of message)",
                 message.size());
    }

private:
    Runtime& runtime_;
    std::vector<std::u16string> texts_;
    std::size_t shown_ = 0;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_ = false;
};

MH4N_OPTION("runner", "--test-keyboard-frontend", "TEXT",
            "for tests: a stand-in for the app's keyboard frontend (mh4n/applet_frontend.h) that answers each "
            "software keyboard with TEXT and its OK button from another thread, 50 ms of host time after it "
            "opens (repeatable: the Nth keyboard gets the Nth, then the last); replaces the default keyboard");

// The hub of the runtime, if a frontend was set, goes where the applets look for it. The name sorts after
// "services.ipc", which makes the service manager.
void AttachFrontends(Runtime& rt) {
    const std::vector<std::string> texts = rt.GetOptions().All("--test-keyboard-frontend");
    if (!texts.empty()) {
        std::vector<std::u16string> converted;
        for (const std::string& text : texts) {
            converted.push_back(Utf8ToUtf16(text));
        }
        SetKeyboardFrontend(rt, std::make_shared<TestKeyboard>(rt, std::move(converted)));
    }
    std::shared_ptr<FrontendHub> hub = LookUp(rt);
    if (!hub) {
        return;
    }
    ServiceManager* manager = FindServiceManager(rt);
    if (manager == nullptr) {
        MH4N_LOG(Error, kLog, "the applets' frontends need the service manager");
        return;
    }
    manager->Context().State<FrontendSlot>(kStateKey)->hub = hub;
    MH4N_LOG(Info, kLog, "frontends: keyboard {}, error {}", hub->Keyboard() ? "the app's" : "the default",
             hub->Error() ? "the app's" : "the default");
}

MH4N_REGISTER(BootStep, "services.ipc.applets", BootStep{Phase::Services, AttachFrontends});

}  // namespace

}  // namespace apt

// ---- mh4n/applet_frontend.h ------------------------------------------------------------------------------------

KeyboardResult ValidateKeyboard(const KeyboardRequest& request, const std::u16string& text, u8 button) {
    return apt::ToResult(apt::CheckFinalize(apt::ToFrontendConfig(request), text, button));
}

void FinishKeyboard(Runtime& runtime, std::u16string text, u8 button) {
    const std::shared_ptr<apt::FrontendHub> hub = apt::LookUp(runtime);
    if (!hub || !hub->Keyboard()) {
        MH4N_LOG(Warning, apt::kLog, "FinishKeyboard: no keyboard frontend was set; the answer is dropped");
        return;
    }
    hub->Post({std::move(text), button});
}

void SetKeyboardFrontend(Runtime& runtime, std::shared_ptr<KeyboardFrontend> frontend) {
    apt::MakeOrFind(runtime)->SetKeyboard(std::move(frontend));
}

void SetErrorFrontend(Runtime& runtime, std::shared_ptr<ErrorFrontend> frontend) {
    apt::MakeOrFind(runtime)->SetError(std::move(frontend));
}

}  // namespace mh4n
