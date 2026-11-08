// Derived from Azahar src/network/packet.cpp at 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-2.0-or-later: it says
// "GPLv2 or any later version"): the serialization of the room protocol, as the header next to this file says.
// Copyright 2017-2024 Citra Emulator Project / Azahar Emulator Project
#include "packet.h"

#include <cstring>

namespace mh4n::rooms::detail {

void Packet::Append(const void* data, std::size_t size) {
    if (data != nullptr && size > 0) {
        const std::size_t start = data_.size();
        data_.resize(start + size);
        std::memcpy(data_.data() + start, data, size);
    }
}

bool Packet::CheckSize(std::size_t size) {
    valid_ = valid_ && read_pos_ + size <= data_.size();
    return valid_;
}

bool Packet::Read(void* out, std::size_t size) {
    if (size == 0) {
        return valid_;
    }
    if (!CheckSize(size)) {
        return false;
    }
    std::memcpy(out, data_.data() + read_pos_, size);
    read_pos_ += size;
    return true;
}

void Packet::IgnoreBytes(std::size_t size) {
    if (CheckSize(size)) {
        read_pos_ += size;
    }
}

Packet& Packet::operator<<(u8 value) {
    Append(&value, 1);
    return *this;
}

Packet& Packet::operator<<(u16 value) {
    const u8 bytes[2] = {static_cast<u8>(value >> 8), static_cast<u8>(value)};
    Append(bytes, 2);
    return *this;
}

Packet& Packet::operator<<(u32 value) {
    const u8 bytes[4] = {static_cast<u8>(value >> 24), static_cast<u8>(value >> 16), static_cast<u8>(value >> 8),
                         static_cast<u8>(value)};
    Append(bytes, 4);
    return *this;
}

Packet& Packet::operator<<(u64 value) {
    *this << static_cast<u32>(value >> 32);
    return *this << static_cast<u32>(value);
}

Packet& Packet::operator<<(const std::string& value) {
    *this << static_cast<u32>(value.size());
    Append(value.data(), value.size());
    return *this;
}

Packet& Packet::operator<<(const std::vector<u8>& value) {
    *this << static_cast<u32>(value.size());
    Append(value.data(), value.size());
    return *this;
}

Packet& Packet::operator<<(const std::vector<std::string>& value) {
    *this << static_cast<u32>(value.size());
    for (const std::string& s : value) {
        *this << s;
    }
    return *this;
}

Packet& Packet::operator>>(bool& value) {
    u8 byte = 0;
    *this >> byte;
    value = byte != 0;
    return *this;
}

Packet& Packet::operator>>(u8& value) {
    if (!Read(&value, 1)) {
        value = 0;
    }
    return *this;
}

Packet& Packet::operator>>(u16& value) {
    u8 bytes[2] = {};
    Read(bytes, 2);
    value = static_cast<u16>((bytes[0] << 8) | bytes[1]);
    return *this;
}

Packet& Packet::operator>>(u32& value) {
    u8 bytes[4] = {};
    Read(bytes, 4);
    value = (static_cast<u32>(bytes[0]) << 24) | (static_cast<u32>(bytes[1]) << 16) |
            (static_cast<u32>(bytes[2]) << 8) | bytes[3];
    return *this;
}

Packet& Packet::operator>>(u64& value) {
    u32 high = 0;
    u32 low = 0;
    *this >> high >> low;
    value = (static_cast<u64>(high) << 32) | low;
    return *this;
}

Packet& Packet::operator>>(std::string& value) {
    u32 length = 0;
    *this >> length;
    value.clear();
    if (length > 0 && CheckSize(length)) {
        value.assign(reinterpret_cast<const char*>(data_.data() + read_pos_), length);
        read_pos_ += length;
    }
    return *this;
}

Packet& Packet::operator>>(std::vector<u8>& value) {
    u32 length = 0;
    *this >> length;
    value.clear();
    if (length > 0 && CheckSize(length)) {
        value.assign(data_.begin() + static_cast<std::ptrdiff_t>(read_pos_),
                     data_.begin() + static_cast<std::ptrdiff_t>(read_pos_ + length));
        read_pos_ += length;
    }
    return *this;
}

Packet& Packet::operator>>(std::vector<std::string>& value) {
    u32 count = 0;
    *this >> count;
    value.clear();
    // each string takes at least its 4-byte length
    if (count > Remaining() / 4) {
        valid_ = false;
        return *this;
    }
    for (u32 i = 0; i < count && valid_; ++i) {
        std::string s;
        *this >> s;
        value.push_back(std::move(s));
    }
    return *this;
}

}  // namespace mh4n::rooms::detail
