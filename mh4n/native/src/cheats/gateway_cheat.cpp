// Derived from Azahar src/core/cheats/gateway_cheat.cpp and src/core/cheats/gateway_cheat.h at
// 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-2.0-or-later: both say "GPLv2 or any later version"):
// the execution state (State), each code type's operation (WriteOp, CompOp, LoadOffsetOp, LoopOp,
// TerminateOp, LoopExecuteVariantOp, FullTerminateOp, SetOffsetOp, AddValueOp, SetValueOp,
// IncrementiveWriteOp, LoadOp, AddOffsetOp, JokerOp, PatchOp), CheatLine's parsing, Execute, ToString
// and LoadFile; and Common::StripSpaces and Common::SplitString as src/common/string_util.cpp has them
// (GPL-2.0-or-later).
// Copyright 2018-2026 Citra Emulator Project / Azahar Emulator Project (gateway_cheat.cpp)
// Copyright 2018-2025 Citra Emulator Project / Azahar Emulator Project (gateway_cheat.h)
// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project (string_util.cpp)
#include "cheats/gateway_cheat.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <type_traits>

#include "../common/utf8.h"

namespace mh4n::cheats {

namespace {

struct State {
    u32 reg = 0;
    u32 offset = 0;
    u32 if_flag = 0;
    u32 loop_count = 0;
    std::size_t loop_back_line = 0;
    std::size_t current_line_nr = 0;
    bool loop_flag = false;
};

using CheatLine = GatewayCheat::CheatLine;
using CheatType = GatewayCheat::CheatType;

template <typename T>
T ReadT(CheatTarget& target, u32 address) {
    if constexpr (sizeof(T) == 1) {
        return target.Read8(address);
    } else if constexpr (sizeof(T) == 2) {
        return target.Read16(address);
    } else {
        return target.Read32(address);
    }
}

template <typename T>
void WriteT(CheatTarget& target, u32 address, T value) {
    if constexpr (sizeof(T) == 1) {
        target.Write8(address, value);
    } else if constexpr (sizeof(T) == 2) {
        target.Write16(address, value);
    } else {
        target.Write32(address, value);
    }
}

template <typename T>
void WriteOp(const CheatLine& line, const State& state, CheatTarget& target) {
    const u32 addr = line.address + state.offset;
    const T val = ReadT<T>(target, addr);
    if (val != static_cast<T>(line.value)) {
        WriteT<T>(target, addr, static_cast<T>(line.value));
    }
}

template <typename T, typename CompareFunc>
void CompOp(const CheatLine& line, State& state, CheatTarget& target, CompareFunc comp) {
    const u32 addr = line.address + state.offset;
    const T val = ReadT<T>(target, addr);
    if (!comp(val)) {
        state.if_flag++;
    }
}

void LoadOffsetOp(CheatTarget& target, const CheatLine& line, State& state) {
    const u32 addr = line.address + state.offset;
    state.offset = target.Read32(addr);
}

void LoopOp(const CheatLine& line, State& state) {
    state.loop_flag = state.loop_count < line.value;
    state.loop_count++;
    state.loop_back_line = state.current_line_nr;
}

void TerminateOp(State& state) {
    if (state.if_flag > 0) {
        state.if_flag--;
    }
}

void LoopExecuteVariantOp(State& state) {
    if (state.loop_flag) {
        state.current_line_nr = state.loop_back_line - 1;
    } else {
        state.loop_count = 0;
    }
}

void FullTerminateOp(State& state) {
    if (state.loop_flag) {
        state.current_line_nr = state.loop_back_line - 1;
    } else {
        state.offset = 0;
        state.reg = 0;
        state.loop_count = 0;
        state.if_flag = 0;
        state.loop_flag = false;
    }
}

template <typename T>
void IncrementiveWriteOp(const CheatLine& line, State& state, CheatTarget& target) {
    const u32 addr = line.value + state.offset;
    const T val = ReadT<T>(target, addr);
    if (val != static_cast<T>(state.reg)) {
        WriteT<T>(target, addr, static_cast<T>(state.reg));
    }
    state.offset += sizeof(T);
}

template <typename T>
void LoadOp(const CheatLine& line, State& state, CheatTarget& target) {
    const u32 addr = line.value + state.offset;
    state.reg = ReadT<T>(target, addr);
}

void JokerOp(const CheatLine& line, State& state, CheatTarget& target) {
    const u32 pad_state = target.PadState();
    const bool pressed = (pad_state & line.value) == line.value;
    if (!pressed) {
        state.if_flag++;
    }
}

// EXXXXXXX YYYYYYYY: YYYYYYYY bytes from the next lines (their two words in turn) to [XXXXXXXX + offset].
void PatchOp(const CheatLine& line, State& state, CheatTarget& target, const std::vector<CheatLine>& cheat_lines) {
    if (state.if_flag > 0) {
        // Skip over the additional patch lines
        state.current_line_nr += static_cast<std::size_t>(std::ceil(line.value / 8.0));
        return;
    }
    u32 num_bytes = line.value;
    u32 addr = line.address + state.offset;

    bool first = true;
    u32 bit_offset = 0;
    if (num_bytes > 0) {
        state.current_line_nr++;  // skip over the current code
    }
    // (departure: a patch longer than its lines stops at the last one)
    const auto at = [&](std::size_t nr) -> const CheatLine* { return nr < cheat_lines.size() ? &cheat_lines[nr] : nullptr; };
    while (num_bytes >= 4) {
        const CheatLine* l = at(state.current_line_nr);
        if (l == nullptr) {
            return;
        }
        const u32 tmp = first ? l->first : l->value;
        if (!first && num_bytes > 4) {
            state.current_line_nr++;
        }
        first = !first;
        target.Write32(addr, tmp);
        addr += 4;
        num_bytes -= 4;
    }
    while (num_bytes > 0) {
        const CheatLine* l = at(state.current_line_nr);
        if (l == nullptr) {
            return;
        }
        const u32 tmp = (first ? l->first : l->value) >> bit_offset;
        target.Write8(addr, static_cast<u8>(tmp));
        addr += 1;
        num_bytes -= 1;
        bit_offset += 8;
    }
}

// Common::StripSpaces: without the spaces, tabs, carriage returns and line feeds at either end.
std::string StripSpaces(const std::string& str) {
    const std::size_t s = str.find_first_not_of(" \t\r\n");
    if (s == std::string::npos) {
        return "";
    }
    const std::size_t e = str.find_last_not_of(" \t\r\n");
    return str.substr(s, e - s + 1);
}

// Common::SplitString: the empty piece after the last delimiter is dropped.
std::vector<std::string> SplitString(const std::string& str, char delim) {
    std::istringstream iss(str);
    std::vector<std::string> output(1);
    while (std::getline(iss, *output.rbegin(), delim)) {
        output.emplace_back();
    }
    output.pop_back();
    return output;
}

}  // namespace

GatewayCheat::CheatLine::CheatLine(const std::string& line) {
    constexpr std::size_t cheat_length = 17;
    if (line.length() != cheat_length) {
        type = CheatType::Null;
        cheat_line = line;
        valid = false;
        return;
    }
    try {
        const std::string type_temp = line.substr(0, 1);
        // 0xD types have extra subtype value, i.e. 0xDA
        std::string sub_type_temp;
        if (type_temp == "D" || type_temp == "d") {
            sub_type_temp = line.substr(1, 1);
        }
        type = static_cast<CheatType>(std::stoi(type_temp + sub_type_temp, nullptr, 16));
        first = static_cast<u32>(std::stoul(line.substr(0, 8), nullptr, 16));
        address = first & 0x0FFFFFFF;
        value = static_cast<u32>(std::stoul(line.substr(9, 8), nullptr, 16));
        cheat_line = line;
    } catch (const std::logic_error&) {
        type = CheatType::Null;
        cheat_line = line;
        valid = false;
    }
}

GatewayCheat::GatewayCheat(std::string name, std::vector<CheatLine> cheat_lines, std::string comments)
    : name_(std::move(name)), cheat_lines_(std::move(cheat_lines)), comments_(std::move(comments)) {}

GatewayCheat::GatewayCheat(std::string name, const std::string& code, std::string comments)
    : name_(std::move(name)), comments_(std::move(comments)) {
    for (const std::string& line : SplitString(code, '\n')) {
        if (!line.empty()) {
            cheat_lines_.emplace_back(line);
        }
    }
}

void GatewayCheat::Execute(CheatTarget& target) const {
    State state;
    for (state.current_line_nr = 0; state.current_line_nr < cheat_lines_.size(); state.current_line_nr++) {
        const CheatLine& line = cheat_lines_[state.current_line_nr];
        if (state.if_flag > 0) {
            switch (line.type) {
            case CheatType::GreaterThan32:
            case CheatType::LessThan32:
            case CheatType::EqualTo32:
            case CheatType::NotEqualTo32:
            case CheatType::GreaterThan16WithMask:
            case CheatType::LessThan16WithMask:
            case CheatType::EqualTo16WithMask:
            case CheatType::NotEqualTo16WithMask:
            case CheatType::Joker:
                // Increment the if_flag to handle the end if correctly
                state.if_flag++;
                break;
            case CheatType::Patch:
                // Skip the additional patch lines
                PatchOp(line, state, target, cheat_lines_);
                break;
            case CheatType::Terminator:
                // D0000000 00000000 - ENDIF
                TerminateOp(state);
                break;
            case CheatType::FullTerminator:
                // D2000000 00000000 - END; offset = 0; reg = 0;
                FullTerminateOp(state);
                break;
            default:
                break;
            }
            // Do not execute any other op code
            continue;
        }
        switch (line.type) {
        case CheatType::Null:
            break;
        case CheatType::Write32:
            // 0XXXXXXX YYYYYYYY - word[XXXXXXX+offset] = YYYYYYYY
            WriteOp<u32>(line, state, target);
            break;
        case CheatType::Write16:
            // 1XXXXXXX 0000YYYY - half[XXXXXXX+offset] = YYYY
            WriteOp<u16>(line, state, target);
            break;
        case CheatType::Write8:
            // 2XXXXXXX 000000YY - byte[XXXXXXX+offset] = YY
            WriteOp<u8>(line, state, target);
            break;
        case CheatType::GreaterThan32:
            // 3XXXXXXX YYYYYYYY - execute next block IF YYYYYYYY > word[XXXXXXX]   ;unsigned
            CompOp<u32>(line, state, target, [&line](u32 val) { return line.value > val; });
            break;
        case CheatType::LessThan32:
            // 4XXXXXXX YYYYYYYY - execute next block IF YYYYYYYY < word[XXXXXXX]   ;unsigned
            CompOp<u32>(line, state, target, [&line](u32 val) { return line.value < val; });
            break;
        case CheatType::EqualTo32:
            // 5XXXXXXX YYYYYYYY - execute next block IF YYYYYYYY == word[XXXXXXX]   ;unsigned
            CompOp<u32>(line, state, target, [&line](u32 val) { return line.value == val; });
            break;
        case CheatType::NotEqualTo32:
            // 6XXXXXXX YYYYYYYY - execute next block IF YYYYYYYY != word[XXXXXXX]   ;unsigned
            CompOp<u32>(line, state, target, [&line](u32 val) { return line.value != val; });
            break;
        case CheatType::GreaterThan16WithMask:
            // 7XXXXXXX ZZZZYYYY - execute next block IF YYYY > ((not ZZZZ) AND half[XXXXXXX])
            CompOp<u16>(line, state, target, [&line](u16 val) {
                return static_cast<u16>(line.value) > (static_cast<u16>(~line.value >> 16) & val);
            });
            break;
        case CheatType::LessThan16WithMask:
            // 8XXXXXXX ZZZZYYYY - execute next block IF YYYY < ((not ZZZZ) AND half[XXXXXXX])
            CompOp<u16>(line, state, target, [&line](u16 val) {
                return static_cast<u16>(line.value) < (static_cast<u16>(~line.value >> 16) & val);
            });
            break;
        case CheatType::EqualTo16WithMask:
            // 9XXXXXXX ZZZZYYYY - execute next block IF YYYY = ((not ZZZZ) AND half[XXXXXXX])
            CompOp<u16>(line, state, target, [&line](u16 val) {
                return static_cast<u16>(line.value) == (static_cast<u16>(~line.value >> 16) & val);
            });
            break;
        case CheatType::NotEqualTo16WithMask:
            // AXXXXXXX ZZZZYYYY - execute next block IF YYYY <> ((not ZZZZ) AND half[XXXXXXX])
            CompOp<u16>(line, state, target, [&line](u16 val) {
                return static_cast<u16>(line.value) != (static_cast<u16>(~line.value >> 16) & val);
            });
            break;
        case CheatType::LoadOffset:
            // BXXXXXXX 00000000 - offset = word[XXXXXXX+offset]
            LoadOffsetOp(target, line, state);
            break;
        case CheatType::Loop:
            // C0000000 YYYYYYYY - LOOP next block YYYYYYYY times
            LoopOp(line, state);
            break;
        case CheatType::Terminator:
            // D0000000 00000000 - END IF
            TerminateOp(state);
            break;
        case CheatType::LoopExecuteVariant:
            // D1000000 00000000 - END LOOP
            LoopExecuteVariantOp(state);
            break;
        case CheatType::FullTerminator:
            // D2000000 00000000 - NEXT & Flush
            FullTerminateOp(state);
            break;
        case CheatType::SetOffset:
            // D3000000 XXXXXXXX - Sets the offset to XXXXXXXX
            state.offset = line.value;
            break;
        case CheatType::AddValue:
            // D4000000 XXXXXXXX - reg += XXXXXXXX
            state.reg += line.value;
            break;
        case CheatType::SetValue:
            // D5000000 XXXXXXXX - reg = XXXXXXXX
            state.reg = line.value;
            break;
        case CheatType::IncrementiveWrite32:
            // D6000000 XXXXXXXX - (32bit) [XXXXXXXX+offset] = reg ; offset += 4
            IncrementiveWriteOp<u32>(line, state, target);
            break;
        case CheatType::IncrementiveWrite16:
            // D7000000 XXXXXXXX - (16bit) [XXXXXXXX+offset] = reg & 0xffff ; offset += 2
            IncrementiveWriteOp<u16>(line, state, target);
            break;
        case CheatType::IncrementiveWrite8:
            // D8000000 XXXXXXXX - (8bit) [XXXXXXXX+offset] = reg & 0xff ; offset++
            IncrementiveWriteOp<u8>(line, state, target);
            break;
        case CheatType::Load32:
            // D9000000 XXXXXXXX - reg = [XXXXXXXX+offset]
            LoadOp<u32>(line, state, target);
            break;
        case CheatType::Load16:
            // DA000000 XXXXXXXX - reg = [XXXXXXXX+offset] & 0xFFFF
            LoadOp<u16>(line, state, target);
            break;
        case CheatType::Load8:
            // DB000000 XXXXXXXX - reg = [XXXXXXXX+offset] & 0xFF
            LoadOp<u8>(line, state, target);
            break;
        case CheatType::AddOffset:
            // DC000000 XXXXXXXX - offset + XXXXXXXX
            state.offset += line.value;
            break;
        case CheatType::Joker:
            // DD000000 XXXXXXXX - if KEYPAD has value XXXXXXXX execute next block
            JokerOp(line, state, target);
            break;
        case CheatType::Patch:
            // EXXXXXXX YYYYYYYY - copies YYYYYYYY bytes from (current code location + 8) to [XXXXXXXX + offset]
            PatchOp(line, state, target, cheat_lines_);
            break;
        }
    }
}

std::string GatewayCheat::GetCode() const {
    std::string result;
    for (const CheatLine& line : cheat_lines_) {
        result += line.cheat_line + '\n';
    }
    return result;
}

std::size_t GatewayCheat::InvalidLines() const {
    return static_cast<std::size_t>(
        std::count_if(cheat_lines_.begin(), cheat_lines_.end(), [](const CheatLine& l) { return !l.valid; }));
}

bool GatewayCheat::TouchesCode(u32 begin, u32 end) const {
    // The offset as the lines set it; unknown after one loaded from memory (B, the Patch's own offset).
    std::optional<u32> offset = 0;
    const auto lands = [&](u32 address, u32 size) {
        if (!offset) {
            return false;
        }
        const u64 at = static_cast<u64>(address) + *offset;
        return at < end && at + size > begin;
    };
    for (std::size_t i = 0; i < cheat_lines_.size(); ++i) {
        const CheatLine& line = cheat_lines_[i];
        switch (line.type) {
        case CheatType::Write32:
            if (lands(line.address, 4)) return true;
            break;
        case CheatType::Write16:
            if (lands(line.address, 2)) return true;
            break;
        case CheatType::Write8:
            if (lands(line.address, 1)) return true;
            break;
        case CheatType::Patch:
            if (lands(line.address, std::max<u32>(line.value, 1))) return true;
            i += static_cast<std::size_t>(std::ceil(line.value / 8.0));  // (its data lines are no codes)
            break;
        case CheatType::IncrementiveWrite32:
        case CheatType::IncrementiveWrite16:
        case CheatType::IncrementiveWrite8: {
            const u32 size = line.type == CheatType::IncrementiveWrite32 ? 4 : line.type == CheatType::IncrementiveWrite16 ? 2 : 1;
            if (lands(line.value, size)) return true;
            if (offset) *offset += size;
            break;
        }
        case CheatType::LoadOffset:
            offset.reset();
            break;
        case CheatType::SetOffset:
            offset = line.value;
            break;
        case CheatType::AddOffset:
            if (offset) *offset += line.value;
            break;
        case CheatType::FullTerminator:
            offset = 0;
            break;
        default:
            break;
        }
    }
    return false;
}

std::string GatewayCheat::ToString() const {
    std::string result;
    result += '[' + name_ + "]\n";
    if (IsEnabled()) {
        result += kEnabledText;
        result += '\n';
    }
    for (const std::string& comment_line : SplitString(comments_, '\n')) {
        result += "*" + comment_line + '\n';
    }
    result += GetCode() + '\n';
    return result;
}

std::vector<std::shared_ptr<GatewayCheat>> GatewayCheat::Parse(const std::string& text) {
    std::vector<std::shared_ptr<GatewayCheat>> cheats;
    std::istringstream file(text);
    std::string comments;
    std::vector<CheatLine> cheat_lines;
    std::string name;
    bool enabled = false;

    std::string line;
    while (std::getline(file, line)) {
        line.erase(std::remove(line.begin(), line.end(), '\0'), line.end());
        line = StripSpaces(line);  // remove spaces at front and end
        if (line.length() >= 2 && line.front() == '[') {
            if (!cheat_lines.empty()) {
                cheats.push_back(std::make_shared<GatewayCheat>(name, cheat_lines, comments));
                cheats.back()->SetEnabled(enabled);
                enabled = false;
            }
            name = line.substr(1, line.length() - 2);
            cheat_lines.clear();
            comments.erase();
        } else if (!line.empty() && line.front() == '*') {
            if (line == kEnabledText) {
                enabled = true;
            } else {
                comments += line.substr(1, line.length() - 1) + '\n';
            }
        } else if (!line.empty()) {
            cheat_lines.emplace_back(line);
        }
    }
    if (!cheat_lines.empty()) {
        cheats.push_back(std::make_shared<GatewayCheat>(name, cheat_lines, comments));
        cheats.back()->SetEnabled(enabled);
    }
    return cheats;
}

std::vector<std::shared_ptr<GatewayCheat>> GatewayCheat::LoadFile(const std::string& path) {
    std::ifstream file(utf8::PathFromUtf8(path), std::ios::binary);
    if (!file) {
        return {};
    }
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return Parse(text);
}

}  // namespace mh4n::cheats
