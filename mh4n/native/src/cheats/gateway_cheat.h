// Derived from Azahar src/core/cheats/gateway_cheat.h, src/core/cheats/gateway_cheat.cpp and
// src/core/cheats/cheat_base.h at 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-2.0-or-later: each says
// "GPLv2 or any later version"): the Gateway / Action Replay code types (CheatType), a code line
// (CheatLine) and how it is parsed, a cheat and its text in the cheats file (ToString, LoadFile, the
// `*citra_enabled` marker).
// Copyright 2018-2025 Citra Emulator Project / Azahar Emulator Project (gateway_cheat.h, cheat_base.h)
// Copyright 2018-2026 Citra Emulator Project / Azahar Emulator Project (gateway_cheat.cpp)
//
// Gateway cheat codes (the format of Azahar's and Luma3DS's cheats files):
//
//     [Name]
//     *citra_enabled              (the cheat is on; Azahar writes it, and so does the app)
//     *a comment line
//     12345678 90ABCDEF           (one or more code lines)
//
// Execute runs a cheat's lines once, as Azahar's GatewayCheat::Execute, against a CheatTarget: the
// game's memory and the pad's state. Departures from Azahar:
//  - Azahar invalidates the JIT's cache after each write; the port's code is recompiled ahead of time
//    and never reads its instructions from memory, so a cheat that writes into the game's code does
//    nothing (TouchesCode tells such a cheat apart, for the app to say so);
//  - a patch line (EXXXXXXX) that claims more bytes than its cheat has lines stops at the last line
//    (Azahar reads past the end of its list);
//  - nothing is logged per bad line at load (the count is), and enabling a cheat logs nothing.
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "mh4n/types.h"

namespace mh4n::cheats {

// What a cheat reads and writes: the game's memory (an address that is not mapped reads as 0 and a
// write there is dropped, as Azahar's Memory::Read / Write), and the pad's buttons (HID's PadState,
// what the Joker code DD tests).
class CheatTarget {
public:
    virtual ~CheatTarget() = default;
    virtual u8 Read8(u32 address) = 0;
    virtual u16 Read16(u32 address) = 0;
    virtual u32 Read32(u32 address) = 0;
    virtual void Write8(u32 address, u8 value) = 0;
    virtual void Write16(u32 address, u16 value) = 0;
    virtual void Write32(u32 address, u32 value) = 0;
    virtual u32 PadState() = 0;
};

class GatewayCheat {
public:
    enum class CheatType {
        Null = -0x1,
        Write32 = 0x00,
        Write16 = 0x01,
        Write8 = 0x02,
        GreaterThan32 = 0x03,
        LessThan32 = 0x04,
        EqualTo32 = 0x05,
        NotEqualTo32 = 0x06,
        GreaterThan16WithMask = 0x07,
        LessThan16WithMask = 0x08,
        EqualTo16WithMask = 0x09,
        NotEqualTo16WithMask = 0x0A,
        LoadOffset = 0x0B,
        Loop = 0x0C,
        Terminator = 0xD0,
        LoopExecuteVariant = 0xD1,
        FullTerminator = 0xD2,
        SetOffset = 0xD3,
        AddValue = 0xD4,
        SetValue = 0xD5,
        IncrementiveWrite32 = 0xD6,
        IncrementiveWrite16 = 0xD7,
        IncrementiveWrite8 = 0xD8,
        Load32 = 0xD9,
        Load16 = 0xDA,
        Load8 = 0xDB,
        AddOffset = 0xDC,
        Joker = 0xDD,
        Patch = 0xE,
    };

    struct CheatLine {
        explicit CheatLine(const std::string& line);
        CheatType type = CheatType::Null;
        u32 address = 0;
        u32 value = 0;
        u32 first = 0;
        std::string cheat_line;
        bool valid = true;
    };

    GatewayCheat(std::string name, std::vector<CheatLine> cheat_lines, std::string comments);
    // `code`: the code lines, one per line.
    GatewayCheat(std::string name, const std::string& code, std::string comments);

    // The lines once, in order (any thread, but one target at a time).
    void Execute(CheatTarget& target) const;

    bool IsEnabled() const { return enabled_.load(std::memory_order_relaxed); }
    void SetEnabled(bool enabled) { enabled_.store(enabled, std::memory_order_relaxed); }

    const std::string& GetName() const { return name_; }
    const std::string& GetComments() const { return comments_; }
    // The code lines, each followed by '\n'.
    std::string GetCode() const;
    // How many lines are not codes (not 17 characters, or not hexadecimal).
    std::size_t InvalidLines() const;
    // Does a write of this cheat land in [begin, end) as far as the lines alone tell (an offset loaded
    // from memory is not followed)? The game's code there is recompiled: such a write changes nothing.
    bool TouchesCode(u32 begin, u32 end) const;

    // The cheat as the file has it: [Name], the marker if it is on, the comments, the lines, an empty line.
    std::string ToString() const;

    // The cheats of a file, or of a text in the file's format (`*citra_enabled` turns the cheat it is in
    // on). A file that does not exist or can't be opened has none.
    static std::vector<std::shared_ptr<GatewayCheat>> LoadFile(const std::string& path);
    static std::vector<std::shared_ptr<GatewayCheat>> Parse(const std::string& text);

private:
    std::atomic<bool> enabled_{false};
    const std::string name_;
    std::vector<CheatLine> cheat_lines_;
    const std::string comments_;
};

// The marker of a cheat that is on, in the file.
inline constexpr const char* kEnabledText = "*citra_enabled";

}  // namespace mh4n::cheats
