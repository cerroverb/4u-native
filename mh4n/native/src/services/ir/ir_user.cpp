// Derived from Azahar src/core/hle/service/ir/ir_user.cpp and src/core/hle/service/ir/ir_user.h at
// 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-2.0-or-later: both say "GPLv2 or any later
// version"), and from src/core/hle/result.h at the same commit (GPL-2.0-or-later) for the error
// descriptions, modules, summaries and levels: the command table, the commands and their replies.
// Copyright 2015-2025 Citra Emulator Project / Azahar Emulator Project (ir_user.cpp)
// Copyright 2015-2024 Citra Emulator Project / Azahar Emulator Project (ir_user.h)
// Copyright 2014-2024 Citra Emulator Project / Azahar Emulator Project (result.h)
//
// ir:USER (N1-16) with the Circle Pad Pro (N3-12, D10, D39). As in Azahar, RequireConnection(1)
// connects the emulated Circle Pad Pro (ExtraHID; here ir_device::CirclePadPro, N3-08, made in
// ir_device.cpp), SendIrNop hands the game's requests to it, and its answers and status packets go
// into the receive buffer with the receive event (Shared::PutToReceive). With --circle-pad-pro off
// nothing connects, as before N3-12: RequireConnection takes the branch Azahar has for an unknown
// device id, SendIrNop answers "not connected", and the receive buffer stays empty. One departure from
// Azahar apart from that: a command that needs the shared memory before InitializeIrNopShared (Azahar
// dereferences a null pointer) leaves the memory alone and answers as it would after one.
#include <cstring>
#include <memory>

#include "mh4n/ipc.h"
#include "mh4n/log.h"
#include "mh4n/registry.h"
#include "mh4n/service.h"
#include "services/ir/ir_state.h"

namespace mh4n::ir {

namespace {

constexpr const char* kLog = "Service.IR";

// ErrorModule::IR, ErrorSummary::InvalidState/NotFound, ErrorLevel::Status, and the descriptions
// (13 is not in Azahar's enumeration; NoData = 1007).
constexpr Result kResultNotConnected = MakeResult(13, 67, 5, 25);
constexpr Result kResultNoData = MakeResult(1007, 67, 4, 25);
static_assert(kResultNotConnected == 0xC8A10C0D);
static_assert(kResultNoData == 0xC8810FEF);

class IrUser final : public Service {
public:
    explicit IrUser(ServiceContext& context) : Service(context, "ir:USER"), ir_(GetShared(context)) {
        Register(0x0001, "InitializeIrNop");
        Register(0x0002, "FinalizeIrNop", &IrUser::FinalizeIrNop);
        Register(0x0003, "ClearReceiveBuffer");
        Register(0x0004, "ClearSendBuffer");
        Register(0x0005, "WaitConnection");
        Register(0x0006, "RequireConnection", &IrUser::RequireConnection);
        Register(0x0007, "AutoConnection");
        Register(0x0008, "AnyConnection");
        Register(0x0009, "Disconnect", &IrUser::Disconnect);
        Register(0x000A, "GetReceiveEvent", &IrUser::GetReceiveEvent);
        Register(0x000B, "GetSendEvent", &IrUser::GetSendEvent);
        Register(0x000C, "GetConnectionStatusEvent", &IrUser::GetConnectionStatusEvent);
        Register(0x000D, "SendIrNop", &IrUser::SendIrNop);
        Register(0x000E, "SendIrNopLarge");
        Register(0x000F, "ReceiveIrnop");
        Register(0x0010, "ReceiveIrnopLarge");
        Register(0x0011, "GetLatestReceiveErrorResult");
        Register(0x0012, "GetLatestSendErrorResult");
        Register(0x0013, "GetConnectionStatus");
        Register(0x0014, "GetTryingToConnectStatus");
        Register(0x0015, "GetReceiveSizeFreeAndUsed");
        Register(0x0016, "GetSendSizeFreeAndUsed");
        Register(0x0017, "GetConnectionRole");
        Register(0x0018, "InitializeIrNopShared", &IrUser::InitializeIrNopShared);
        Register(0x0019, "ReleaseReceivedData", &IrUser::ReleaseReceivedData);
        Register(0x001A, "SetOwnMachineId");
    }

private:
    // Writes one byte of the header, if the shared memory is there and holds one.
    void SetHeaderByte(u32 offset, u8 value) {
        if (ir_->shared_memory && ir_->shared_memory->Size() >= sizeof(SharedMemoryHeader)) {
            ir_->shared_memory->GetPointer(offset)[0] = value;
        }
    }

    // Inputs: 1: shared_buff_size, 2: recv_buff_size, 3: recv_buff_packet_count, 4: send_buff_size,
    // 5: send_buff_packet_count, 6: baud_rate, 7: translate header, 8: the shared memory
    void InitializeIrNopShared(Request& request) {
        const u32 shared_buff_size = request.Pop<u32>();
        const u32 recv_buff_size = request.Pop<u32>();
        const u32 recv_buff_packet_count = request.Pop<u32>();
        const u32 send_buff_size = request.Pop<u32>();
        const u32 send_buff_packet_count = request.Pop<u32>();
        const u8 baud_rate = request.Pop<u8>();
        std::shared_ptr<SharedMemory> memory = request.PopObject<SharedMemory>();
        if (!memory) {
            MH4N_LOG(Error, kLog, "InitializeIrNopShared: the handle is not a shared memory block");
            request.MakeReply(1, 0).Push(kResultInvalidHandle);
            return;
        }

        memory->SetName("IR_USER: shared memory");
        ir_->shared_memory = memory;
        if (ir_->capture_memory.expired()) {
            ir_->capture_memory = memory;
        }
        ir_->receive_buffer = std::make_unique<BufferManager>(memory, 0x10, 0x20,
                                                              recv_buff_packet_count, recv_buff_size);
        if (memory->Size() >= sizeof(SharedMemoryHeader)) {
            SharedMemoryHeader header{};
            header.initialized = 1;
            std::memcpy(memory->GetPointer(), &header, sizeof(header));
        }

        request.MakeReply(1, 0).Push(kResultSuccess);

        MH4N_LOG(Info, kLog,
                 "InitializeIrNopShared: shared_buff_size={}, recv_buff_size={}, "
                 "recv_buff_packet_count={}, send_buff_size={}, send_buff_packet_count={}, "
                 "baud_rate={}",
                 shared_buff_size, recv_buff_size, recv_buff_packet_count, send_buff_size,
                 send_buff_packet_count, baud_rate);
    }

    // Inputs: 1: device_id (a byte)
    void RequireConnection(Request& request) {
        const u8 device_id = request.Pop<u8>();

        if (device_id == 1 && ir_->Device() != nullptr) {
            // These values are observed on a New 3DS. The meaning of them is unclear.
            // TODO (wwylele): should assign network_id a (random?) number
            SetHeaderByte(kConnectionStatusOffset, 2);
            SetHeaderByte(kConnectionRoleOffset, 2);
            SetHeaderByte(kConnectedOffset, 1);

            ir_->connected_device = true;
            ir_->Device()->OnConnect();
            ir_->connection_status_event->Signal();
        } else {
            // An unknown id, or id 1 with --circle-pad-pro off (then no device exists).
            if (device_id != 1) {
                MH4N_LOG(Warning, kLog, "unknown device id {}. Won't connect.", device_id);
            }
            SetHeaderByte(kConnectionStatusOffset, 1);
            SetHeaderByte(kTryingToConnectStatusOffset, 2);
        }

        request.MakeReply(1, 0).Push(kResultSuccess);
        MH4N_LOG(Info, kLog, "RequireConnection: device_id = {}", device_id);
    }

    void GetReceiveEvent(Request& request) {
        Reply& reply = request.MakeReply(1, 2);
        reply.Push(kResultSuccess);
        reply.PushCopyObjects(ir_->receive_event);
    }

    void GetSendEvent(Request& request) {
        Reply& reply = request.MakeReply(1, 2);
        reply.Push(kResultSuccess);
        reply.PushCopyObjects(ir_->send_event);
    }

    void GetConnectionStatusEvent(Request& request) {
        Reply& reply = request.MakeReply(1, 2);
        reply.Push(kResultSuccess);
        reply.PushCopyObjects(ir_->connection_status_event);
    }

    void Disconnect(Request& request) {
        if (ir_->connected_device) {
            if (ir_->Device() != nullptr) {
                ir_->Device()->OnDisconnect();
            }
            ir_->connected_device = false;
            ir_->connection_status_event->Signal();
        }
        SetHeaderByte(kConnectionStatusOffset, 0);
        SetHeaderByte(kConnectedOffset, 0);
        request.MakeReply(1, 0).Push(kResultSuccess);
    }

    void FinalizeIrNop(Request& request) {
        if (ir_->connected_device) {
            if (ir_->Device() != nullptr) {
                ir_->Device()->OnDisconnect();
            }
            ir_->connected_device = false;
        }
        ir_->shared_memory.reset();
        ir_->receive_buffer.reset();
        request.MakeReply(1, 0).Push(kResultSuccess);
    }

    // Inputs: 1: size, 2: the static buffer's descriptor, 3: its address
    void SendIrNop(Request& request) {
        const u32 size = request.Pop<u32>();
        const StaticBuffer* buffer = request.PopStaticBuffer();
        if (buffer == nullptr || buffer->data.size() != size) {
            MH4N_LOG(Warning, kLog, "SendIrNop: the buffer does not hold the {} bytes announced",
                     size);
        }
        if (buffer != nullptr) {
            MH4N_LOG(Trace, kLog, "SendIrNop: data={}", HexBytes(buffer->data));
        }
        if (ir_->connected_device) {
            if (ir_->Device() != nullptr && buffer != nullptr) {
                ir_->Device()->OnReceive(buffer->data);
            } else if (ir_->Device() != nullptr) {
                ir_->Device()->OnReceive({});  // no buffer: an empty request, which the device logs
            }
            ir_->send_event->Signal();
            request.MakeReply(1, 0).Push(kResultSuccess);
        } else {
            MH4N_LOG(Error, kLog, "not connected");
            request.MakeReply(1, 0).Push(kResultNotConnected);
        }
    }

    // Inputs: 1: count
    void ReleaseReceivedData(Request& request) {
        const u32 count = request.Pop<u32>();
        if (ir_->receive_buffer && ir_->receive_buffer->Release(count)) {
            request.MakeReply(1, 0).Push(kResultSuccess);
        } else {
            MH4N_LOG(Error, kLog, "failed to release {} packets", count);
            request.MakeReply(1, 0).Push(kResultNoData);
        }
    }

    std::shared_ptr<Shared> ir_;
};

// A single session, as Azahar's (ServiceFramework("ir:USER", 1)).
MH4N_REGISTER(ServiceFactory, "ir:USER",
              ServiceFactory([](ServiceContext& context) { return std::make_unique<IrUser>(context); },
                             1));

}  // namespace

}  // namespace mh4n::ir
