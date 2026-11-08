// The error screen when the game crashes, as the MHGU port's (its platform/android.c: fault_handler, the report,
// plat_fatal_notice, LogShareActivity): the screen that tells the player the game stopped and offers to share the log
// comes up instead of the app vanishing.
//   - a bad guest memory access: the runtime writes its report to the log and calls the FatalHook installed here
//     before it ends the process (mh4n/memory.h SetFatalHook);
//   - a crash of the host's own (SIGSEGV or SIGBUS outside the guest's memory, SIGABRT, SIGILL, SIGFPE, SIGTRAP): the
//     handlers here write the signal, the address, the thread and a backtrace to the log first.
// Either way the crashing thread only writes a byte to a pipe and waits (both async-signal-safe): a thread of the app's,
// attached to Java beforehand, opens the screen (GameActivity.crashReport: LogShareActivity in its own process, which
// outlives this one), and the process then ends. If that thread can't (Java itself stuck), the process ends after a few
// seconds anyway, as before.
// The game's own end with an error (Runtime::Boot failing) opens the same screen from android_main.cpp.
#pragma once

// android_main.cpp, before the game starts, on the thread SDL gave a JNIEnv (it finds GameActivity there).
void crash_screen_install();
