// Derived from Azahar src/network/packet.h and src/network/packet.cpp at 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6
// (GPL-2.0-or-later: both say "GPLv2 or any later version"): the room protocol's serialization, network byte order
// for the integers, a u32 length before strings and vectors, arrays as their elements.
// Copyright 2017 Citra Emulator Project (packet.h)
// Copyright 2017-2024 Citra Emulator Project / Azahar Emulator Project (packet.cpp)
//
// Departures from Azahar: reading past the end makes the packet invalid and reads zeros (Azahar's IgnoreBytes moves
// past the end without a check, and the reads after it fail the same way); a vector's or string's length is checked
// against what is left before anything is allocated (Azahar resizes the vector to the length it read first).
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "mh4n/types.h"

namespace mh4n::rooms::detail {

class Packet {
public:
    Packet() = default;
    Packet(const void* data, std::size_t size) { Append(data, size); }

    void Append(const void* data, std::size_t size);
    bool Read(void* out, std::size_t size);
    void IgnoreBytes(std::size_t size);

    const u8* Data() const { return data_.data(); }
    std::size_t Size() const { return data_.size(); }
    std::size_t Remaining() const { return read_pos_ < data_.size() ? data_.size() - read_pos_ : 0; }
    bool EndOfPacket() const { return read_pos_ >= data_.size(); }
    explicit operator bool() const { return valid_; }

    Packet& operator<<(bool value) { return *this << static_cast<u8>(value ? 1 : 0); }
    Packet& operator<<(u8 value);
    Packet& operator<<(u16 value);
    Packet& operator<<(u32 value);
    Packet& operator<<(u64 value);
    Packet& operator<<(const std::string& value);
    Packet& operator<<(const char* value) { return *this << std::string(value); }
    template <std::size_t N> Packet& operator<<(const std::array<u8, N>& value) {
        Append(value.data(), N);
        return *this;
    }
    Packet& operator<<(const std::vector<u8>& value);
    Packet& operator<<(const std::vector<std::string>& value);

    Packet& operator>>(bool& value);
    Packet& operator>>(u8& value);
    Packet& operator>>(u16& value);
    Packet& operator>>(u32& value);
    Packet& operator>>(u64& value);
    Packet& operator>>(std::string& value);
    template <std::size_t N> Packet& operator>>(std::array<u8, N>& value) {
        if (!Read(value.data(), N)) {
            value.fill(0);
        }
        return *this;
    }
    Packet& operator>>(std::vector<u8>& value);
    Packet& operator>>(std::vector<std::string>& value);

private:
    bool CheckSize(std::size_t size);

    std::vector<u8> data_;
    std::size_t read_pos_ = 0;
    bool valid_ = true;
};

}  // namespace mh4n::rooms::detail
