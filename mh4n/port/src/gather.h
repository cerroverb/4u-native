// Gather everything in one press (1.0.1, the owner's idea; MHGU for Android has the same option): one gather at a
// gathering point, or one carve, gives everything the point had left at once, through the game's own steps for each
// one (gather.cpp). Off unless the player turns it on (gather_all); off, the game gathers one item a press, as always.
// It doesn't change the code library: it reads the game's memory around each frame and calls the game's functions.
#pragma once

#include <cstdint>
#include <string>

struct RCtx;
struct RCodeInfo;

namespace Port::Gather {

/// The options (mh4u.ini, the app's menu, the runner's --set): gather_all (on/off, off unless set) and gather_trace
/// (tests: a line for each item the rest of a point gives). The runner's --set takes gather_test too (tests only: see
/// gather.cpp). False for another key. On the emulation thread, or before the game starts.
bool SetOption(const std::string& key, const std::string& value);
bool On();

/// A new boot: the options back to off and nothing remembered of the last game (hooks.cpp's Install, before the
/// options are read).
void Reset();
/// hooks.cpp's Install, after Reset and once the game's addresses are this version's (game_addr.h): whether the code
/// library has every game function the module calls, at its address in the library's version. If not, the option
/// does nothing until the next boot, and the line returned says why for the log; "" when it has them.
std::string Check(const RCodeInfo& info);
/// Where the module's log lines go (hooks.cpp's log).
void SetLog(void (*log)(const std::string& line));

/// hooks.cpp's sMhMain::frame, before the game's frame: what the local hunter's gather is compared with.
void Before(RCtx* ctx);
/// hooks.cpp's sMhMain::frame, after the game's frame: when the game's own gather gave an item into the pouch this
/// frame, the rest of the point.
void After(RCtx* ctx);

}  // namespace Port::Gather
