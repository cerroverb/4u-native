// Where the applets meet the app's frontends (N3-12; the API is mh4n/applet_frontend.h). One hub per
// runtime: SetKeyboardFrontend and SetErrorFrontend make it (the runtime owns it), the boot step
// "services.ipc.applets" puts it into the service context under "applet_frontend", and the applets find it
// there when they start. FinishKeyboard posts the player's answer into it from any thread; the keyboard
// applet takes it on the scheduler thread at its update tick.
#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "mh4n/applet_frontend.h"
#include "mh4n/service.h"

namespace mh4n::apt {

struct KeyboardAnswer {
    std::u16string text;
    u8 button = 0;
};

class FrontendHub {
public:
    void SetKeyboard(std::shared_ptr<KeyboardFrontend> frontend);
    void SetError(std::shared_ptr<ErrorFrontend> frontend);
    std::shared_ptr<KeyboardFrontend> Keyboard() const;
    std::shared_ptr<ErrorFrontend> Error() const;

    // Any thread: the answer replaces one that was not taken yet.
    void Post(KeyboardAnswer answer);
    // Scheduler thread: the answer, once.
    std::optional<KeyboardAnswer> Take();
    // Scheduler thread, when a keyboard is shown: an answer posted before it is not for it.
    void Drop();

private:
    mutable std::mutex mutex_;
    std::shared_ptr<KeyboardFrontend> keyboard_;
    std::shared_ptr<ErrorFrontend> error_;
    std::optional<KeyboardAnswer> answer_;
};

// The hub of the runtime the context belongs to, or null (no frontend was set: the applets behave as
// Azahar's default ones).
std::shared_ptr<FrontendHub> FindFrontendHub(ServiceContext& context);

}  // namespace mh4n::apt
