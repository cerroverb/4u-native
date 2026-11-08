// Derived from Azahar src/core/hle/service/ir/ir_user.cpp and src/core/hle/service/ir/ir_user.h at
// 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-2.0-or-later: both say "GPLv2 or any later
// version"): the header of the ir:USER shared memory, the buffer manager of the receive buffer
// (BufferInfo, PacketInfo, Put, Release), the state the service keeps between sessions, and the packet
// PutToReceive makes of a device's payload.
// Copyright 2015-2025 Citra Emulator Project / Azahar Emulator Project (ir_user.cpp)
// Copyright 2015-2024 Citra Emulator Project / Azahar Emulator Project (ir_user.h)
//
// The state of ir:USER (N1-16), kept in the service context under the key "ir:USER" because the
// service has a single session at a time but its events and shared memory outlive a session (Azahar's
// IR_USER object is one for the whole run). Included as "services/ir/ir_state.h" (the capture of
// the HID module reads the shared memory from it). From N3-12 it holds the Circle Pad Pro too
// (ir_device::CirclePadPro), whose timer is the kernel's timer queue and whose input is the runtime's
// InputSource, sampled at the tick each status packet is due (scheduler thread).
#pragma once

#include <memory>
#include <span>
#include <string>
#include <vector>

#include "ir_device/circle_pad_pro.h"
#include "mh4n/kernel.h"
#include "mh4n/service.h"
#include "mh4n/types.h"

namespace mh4n::ir {

// "This is a header that will present in the ir:USER shared memory if it is initialized with
// InitializeIrNopShared service function."
struct SharedMemoryHeader {
    u32 latest_receive_error_result;
    u32 latest_send_error_result;
    u8 connection_status;
    u8 trying_to_connect_status;
    u8 connection_role;
    u8 machine_id;
    u8 connected;
    u8 network_id;
    u8 initialized;
    u8 unknown;
};
static_assert(sizeof(SharedMemoryHeader) == 16);
inline constexpr u32 kConnectionStatusOffset = 8;
inline constexpr u32 kTryingToConnectStatusOffset = 9;
inline constexpr u32 kConnectionRoleOffset = 10;
inline constexpr u32 kConnectedOffset = 12;
inline constexpr u32 kNetworkIdOffset = 13;
inline constexpr u32 kInitializedOffset = 14;

// The receive buffer: BufferInfo (the queue's indices and count) at `info_offset`, a circular queue
// of PacketInfo {offset, size} at `buffer_offset`, and after it the circular data buffer.
//
// Azahar's manager trusts the sizes the game gives it. This one checks every access against the
// size of the shared memory (an access outside it fails the operation) and does not divide by a
// zero packet count.
class BufferManager {
public:
    BufferManager(std::shared_ptr<SharedMemory> shared_memory, u32 info_offset, u32 buffer_offset,
                  u32 max_packet_count, u32 buffer_size);

    // Puts a packet at the head of the buffer. False if the queue or the data buffer is full.
    bool Put(std::span<const u8> packet);
    // Releases packets from the tail of the buffer. False if fewer are queued.
    bool Release(u32 count);

    struct BufferInfo {
        u32 begin_index;
        u32 end_index;
        u32 packet_count;
        u32 unknown;
    };
    static_assert(sizeof(BufferInfo) == 16);
    const BufferInfo& Info() const { return info_; }

private:
    struct PacketInfo {
        u32 offset;
        u32 size;
    };
    static_assert(sizeof(PacketInfo) == 8);

    u8* Pointer(u64 offset, u64 size) const;
    bool GetPacketInfo(u32 index, PacketInfo* out) const;
    bool SetPacketInfo(u32 index, const PacketInfo& packet_info) const;
    u8* DataPointer(u64 offset, u64 size) const;
    void UpdateBufferInfo() const;

    BufferInfo info_{0, 0, 0, 0};
    std::shared_ptr<SharedMemory> shared_memory_;
    u32 info_offset_;
    u32 buffer_offset_;
    u32 max_packet_count_;
    u32 max_data_size_;
};

class QueueTimer;  // ir_device.cpp: the device's Timer on the kernel's timer queue

class Shared {
public:
    Shared();
    ~Shared();
    void Ensure(ServiceContext& context);

    std::shared_ptr<Event> connection_status_event;
    std::shared_ptr<Event> send_event;
    std::shared_ptr<Event> receive_event;
    // The block the game gave InitializeIrNopShared (empty before, and after FinalizeIrNop).
    std::shared_ptr<SharedMemory> shared_memory;
    std::unique_ptr<BufferManager> receive_buffer;
    // The block --capture-hid records: the first one an InitializeIrNopShared brought, and the block of
    // a later one once the first is gone. Held weakly, as the N0-35 reference runner's capture holds it:
    // a strong reference kept the block mapped at its address after the game's Finalize and CloseHandle,
    // so the game's next CreateMemoryBlock at that address failed with 0xE0A01BF5 and the game died at
    // boot (a save with the Circle Pad Pro option on, G8).
    std::weak_ptr<SharedMemory> capture_memory;
    // Device 1 is connected (RequireConnection(1) with the Circle Pad Pro on, until Disconnect or
    // FinalizeIrNop).
    bool connected_device = false;

    // Azahar's emulated Circle Pad Pro (ExtraHID, N3-08), the device of id 1; null with
    // --circle-pad-pro off (then no device connects, as before N3-12).
    ir_device::CirclePadPro* Device() const { return device_.get(); }

    // Azahar's IR_USER::PutToReceive: wraps a payload of the device into a packet of the receive
    // buffer (0xA5, the network id of the header, the size in one or two bytes, the payload, a CRC-8)
    // and signals the receive event. A full buffer drops the packet (logged once until a packet fits).
    void PutToReceive(std::span<const u8> payload);

private:
    bool initialised_ = false;
    ServiceContext* context_ = nullptr;
    bool full_logged_ = false;
    // In this order: the device is destroyed first and cancels its event on the timer.
    std::unique_ptr<QueueTimer> timer_;
    std::unique_ptr<ir_device::CirclePadPro> device_;
};

// The packet PutToReceive puts in the buffer for `payload` (tests): the header, the payload, the CRC-8 of
// boost::crc<8, 0x07, 0, 0, false, false> over all of it. Empty if the payload has 0x4000 bytes or more
// (Azahar asserts).
std::vector<u8> MakePacket(u8 network_id, std::span<const u8> payload);

// "a5 01 ...": bytes for a Trace line (Azahar's LOG_TRACE of the payloads).
std::string HexBytes(std::span<const u8> bytes);

std::shared_ptr<Shared> GetShared(ServiceContext& context);

}  // namespace mh4n::ir
