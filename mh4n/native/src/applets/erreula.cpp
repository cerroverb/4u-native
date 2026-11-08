// Derived from Azahar src/core/hle/applets/erreula.cpp at 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6
// (GPL-2.0-or-later: it says "GPLv2 or any later version"): ErrEula::ReceiveParameterImpl, Start and
// Finalize.
// Copyright 2016-2025 Citra Emulator Project / Azahar Emulator Project
#include "applets/erreula.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "applets/frontend_hub.h"
#include "mh4n/log.h"

namespace mh4n::apt {

namespace {
constexpr const char* kLog = "Service.APT";

// The EULA display types of libctru's errorType (ERROR_EULA, ERROR_TYPE_EULA_FIRST_BOOT,
// ERROR_TYPE_EULA_DRAW_ONLY, ERROR_TYPE_AGREE), as the low byte Azahar reads: agreed to, not shown.
bool IsEulaType(u8 type) {
    return type >= 2 && type <= 5;
}
}  // namespace

Result ErrEula::ReceiveParameterImpl(const MessageParameter& parameter) {
    return AnswerFramebufferRequest(context_, parameter, framebuffer_memory_, "ErrEula");
}

Result ErrEula::Start(const MessageParameter& parameter) {
    // An empty parameter has no data pointer to copy from (memcpy with a null source is undefined).
    if (!parameter.buffer.empty()) {
        std::memcpy(param_.data(), parameter.buffer.data(),
                    std::min(parameter.buffer.size(), param_.size()));
    }

    // Do something here, like showing error codes, or prompting for EULA agreement (Azahar's note).
    // The player "agrees" to an EULA; an error goes to the app's frontend if there is one (N3-12), which
    // shows it without holding the game, and is otherwise "read".
    const u8 type = param_[errlayout::kType];
    if (type == static_cast<u8>(ErrEulaDisplayType::Agree)) {
        const u32 agreed = 1;
        std::memcpy(param_.data() + errlayout::kResult, &agreed, sizeof(agreed));
    }
    const std::shared_ptr<FrontendHub> hub = FindFrontendHub(context_);
    const std::shared_ptr<ErrorFrontend> frontend = hub ? hub->Error() : nullptr;
    const bool shown = frontend != nullptr && !IsEulaType(type);
    if (shown) {
        u32 error_code = 0;
        std::memcpy(&error_code, param_.data() + errlayout::kErrorCode, sizeof(error_code));
        std::u16string text;
        for (std::size_t i = 0; i < kErrEulaMessageSize; ++i) {
            const std::size_t at = errlayout::kMessage + 2 * i;
            const char16_t unit = static_cast<char16_t>(param_[at] | (param_[at + 1] << 8));
            if (unit == 0) {
                break;
            }
            text.push_back(unit);
        }
        frontend->Show(error_code, text);  // the text is never logged (rule 5)
    }
    MH4N_LOG(Info, kLog, "the error/EULA applet {:03X} (display type {}) is closed at once{}",
             static_cast<u32>(id_), static_cast<u32>(type), shown ? ", shown by the app" : "");

    // Let the application know that we're closing.
    Finalize();
    return kResultSuccess;
}

Result ErrEula::Finalize() {
    CloseApplet(nullptr, std::vector<u8>(param_.begin(), param_.end()));
    return kResultSuccess;
}

}  // namespace mh4n::apt
