// The error screen when the game crashes (crash_screen.h).
#include "crash_screen.h"

#include <SDL3/SDL.h>
#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <unwind.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "mh4n/memory.h"
#include "mh4n/registry.h"
#include "mh4n/runtime.h"

namespace {

int g_pipe[2] = {-1, -1};
std::atomic<bool> g_ready{false};  // the watcher waits on the pipe, with the class and the method at hand
std::atomic<bool> g_shown{false};  // the watcher asked for the screen
std::atomic<int> g_crashing{0};    // a crash is being reported (a second one, meanwhile, does not report again)
jclass g_class = nullptr;          // GameActivity (a global reference)
jmethodID g_method = nullptr;      // its static crashReport()

void Put(const char* text) { (void)!write(2, text, strlen(text)); }

// Async-signal-safe: a byte down the pipe, then up to `seconds` for the watcher to have asked for the screen.
void NotifyAndWait(double seconds) {
    if (!g_ready.load()) return;
    const char byte = 1;
    if (write(g_pipe[1], &byte, 1) != 1) return;
    const timespec step{0, 10 * 1000 * 1000};
    for (int i = 0; i < static_cast<int>(seconds * 100) && !g_shown.load(); ++i) nanosleep(&step, nullptr);
    // (a moment for the activity manager to take the request before this process is gone)
    const timespec settle{0, 300 * 1000 * 1000};
    if (g_shown.load()) nanosleep(&settle, nullptr);
}

// The runtime's FatalHook: its report of the bad guest access is in the log already
void OnGuestFault() {
    if (g_crashing.exchange(1) != 0) return;
    Put("*** MH4U: the game stopped: a bad guest memory access (above). The error screen opens.\n");
    __android_log_write(ANDROID_LOG_ERROR, "MH4U", "the game stopped: a bad guest memory access");
    NotifyAndWait(4);
}

const char* SignalName(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGABRT: return "SIGABRT";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    case SIGTRAP: return "SIGTRAP";
    default: return "signal";
    }
}

struct Backtrace {
    uintptr_t pc[32];
    int n = 0;
};
_Unwind_Reason_Code Step(_Unwind_Context* context, void* arg) {
    auto* bt = static_cast<Backtrace*>(arg);
    const uintptr_t pc = _Unwind_GetIP(context);
    if (pc != 0 && bt->n < 32) bt->pc[bt->n++] = pc;
    return bt->n < 32 ? _URC_NO_REASON : _URC_END_OF_STACK;
}

void PutAddress(const char* what, uintptr_t pc) {
    char line[512];
    Dl_info info{};
    if (pc != 0 && dladdr(reinterpret_cast<void*>(pc), &info) != 0 && info.dli_fname != nullptr) {
        const char* slash = strrchr(info.dli_fname, '/');
        snprintf(line, sizeof line, "    %s %016lx %s+%lx%s%s\n", what, static_cast<unsigned long>(pc), slash ? slash + 1 : info.dli_fname,
                 static_cast<unsigned long>(pc - reinterpret_cast<uintptr_t>(info.dli_fbase)), info.dli_sname ? " " : "",
                 info.dli_sname ? info.dli_sname : "");
    } else {
        snprintf(line, sizeof line, "    %s %016lx\n", what, static_cast<unsigned long>(pc));
    }
    Put(line);
}

struct sigaction g_previous[32];
constexpr int kSignals[] = {SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE, SIGTRAP};

// A crash of the host's own (the runtime's handler passes on the faults outside the guest's memory)
void OnSignal(int sig, siginfo_t* info, void* context) {
    if (g_crashing.exchange(1) == 0) {
        char line[256];
        snprintf(line, sizeof line, "\n*** MH4U: the game stopped: %s (code %d, address %016lx) on thread %d\n", SignalName(sig),
                 info ? info->si_code : 0, info ? static_cast<unsigned long>(reinterpret_cast<uintptr_t>(info->si_addr)) : 0ul,
                 static_cast<int>(gettid()));
        Put(line);
#if defined(__aarch64__)
        if (context != nullptr) {
            const auto* uc = static_cast<const ucontext_t*>(context);
            PutAddress("pc", static_cast<uintptr_t>(uc->uc_mcontext.pc));
            PutAddress("lr", static_cast<uintptr_t>(uc->uc_mcontext.regs[30]));
        }
#endif
        Backtrace bt;
        _Unwind_Backtrace(Step, &bt);
        for (int i = 0; i < bt.n; ++i) PutAddress("#", bt.pc[i]);
        __android_log_print(ANDROID_LOG_ERROR, "MH4U", "the game stopped: %s", SignalName(sig));
        NotifyAndWait(4);
        _exit(128 + sig);
    }
    // a crash while one is reported: as without these handlers
    signal(sig, SIG_DFL);
    raise(sig);
}

// The thread that opens the screen: attached to Java while things are well, it waits on the pipe
void Watch() {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());  // (SDL attaches this thread)
    if (env == nullptr) return;
    g_ready.store(true);
    char byte = 0;
    while (read(g_pipe[0], &byte, 1) != 1) {
    }
    env->CallStaticVoidMethod(g_class, g_method);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    g_shown.store(true);
}

// ---- a test-only trigger: MH4U_TEST_CRASH=guest|host|abort[:seconds] (launch.txt's env=), after 20 s by default
void TestTrigger(mh4n::Runtime& rt) {
    const char* what = std::getenv("MH4U_TEST_CRASH");
    if (what == nullptr || *what == 0) return;
    std::string kind = what;
    int seconds = 20;
    if (const auto colon = kind.find(':'); colon != std::string::npos) {
        seconds = std::atoi(kind.c_str() + colon + 1);
        kind.resize(colon);
    }
    std::fprintf(stderr, "[Warning] App: MH4U_TEST_CRASH: a %s crash in %d s (a test)\n", kind.c_str(), seconds);
    mh4n::u8* guest = rt.Memory().Base();
    std::thread([kind, seconds, guest] {
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        std::fprintf(stderr, "[Warning] App: MH4U_TEST_CRASH: %s now\n", kind.c_str());
        std::fflush(stderr);
        if (kind == "guest") {
            // (the guard past the 4 GiB of the guest's reservation: the unmapped guest pages read as zeros in a release
            // build, the guard always faults, and is reported as guest address 0x10)
            *reinterpret_cast<volatile mh4n::u32*>(guest + 0x100000010ull) = 0xDEAD;
        } else if (kind == "abort") {
            std::abort();
        } else {
            *reinterpret_cast<volatile int*>(static_cast<uintptr_t>(0x8)) = 1;
        }
    }).detach();
}
MH4N_REGISTER(mh4n::BootStep, "zz.android.crash_test", mh4n::BootStep{mh4n::Phase::Started, TestTrigger});

}  // namespace

void crash_screen_install() {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (env == nullptr || activity == nullptr || pipe(g_pipe) != 0) return;
    jclass cls = env->GetObjectClass(activity);
    g_method = env->GetStaticMethodID(cls, "crashReport", "()V");
    if (env->ExceptionCheck()) env->ExceptionClear();
    g_class = static_cast<jclass>(env->NewGlobalRef(cls));
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    if (g_method == nullptr) return;
    std::thread(Watch).detach();
    mh4n::SetFatalHook(OnGuestFault);
    // before the runtime's own (its guest memory is made when the game starts): it hands these the faults that aren't its
    struct sigaction action;
    std::memset(&action, 0, sizeof action);
    action.sa_sigaction = OnSignal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (int sig : kSignals) sigaction(sig, &action, &g_previous[sig]);
}
