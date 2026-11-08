// Derived from Azahar src/core/hle/service/ir/ir_user.cpp at 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6
// (GPL-2.0-or-later: it says "GPLv2 or any later version"): IR_USER::PutToReceive (the packet's
// header, its size in one or two bytes, the CRC-8 of boost::crc<8, 0x07, 0, 0, false, false>), and the
// ExtraHID that its constructor makes for device 1.
// Copyright 2015-2025 Citra Emulator Project / Azahar Emulator Project
//
// The Circle Pad Pro behind ir:USER (N3-12, D39): the state's events, the device (N3-08's
// ir_device::CirclePadPro) with its timer on the kernel's timer queue and its input from the runtime's
// InputSource, and the packets it sends into the receive buffer. --circle-pad-pro off leaves the device
// out (N1-16's behaviour: no device connects).
#include <memory>
#include <string>
#include <utility>

#include "mh4n/log.h"
#include "mh4n/options.h"
#include "mh4n/registry.h"
#include "mh4n/runtime.h"
#include "services/ir/ir_state.h"

namespace mh4n::ir {

namespace {

constexpr const char* kLog = "Service.IR";

MH4N_OPTION("runner", "--circle-pad-pro", "on|off",
            "ir:USER's device 1: on (default) = Azahar's emulated Circle Pad Pro, the C-stick, ZL and ZR "
            "of the input source (the game uses it when its Circle Pad Pro option is on); off = no device "
            "connects");

// Azahar's boost::crc<8, 0x07, 0, 0, false, false>: polynomial x^8 + x^2 + x + 1, initial value 0,
// no reflection, no final XOR.
u8 Crc8(std::span<const u8> data) {
    u8 crc = 0;
    for (const u8 byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = static_cast<u8>((crc & 0x80) ? (crc << 1) ^ 0x07 : crc << 1);
        }
    }
    return crc;
}

}  // namespace

// ---- the device's timer --------------------------------------------------------------------------------------

// One event at a time on the kernel's timer queue. A Schedule from inside the callback counts from the
// tick that event was due at, not from the clock, which is Azahar's `msToCycles(period) - cycles_late`:
// a late event does not shift the ones after it (as the HID updates). The input is sampled at that tick
// too, so the input source sees the ticks in the order the queue runs them.
class QueueTimer final : public ir_device::Timer {
public:
    explicit QueueTimer(Kernel& kernel) : kernel_(kernel) {}
    ~QueueTimer() override { Cancel(); }

    void Schedule(u64 delay_ticks, std::function<void()> callback) override {
        Cancel();
        const u64 due = SampleTick() + delay_ticks;
        std::weak_ptr<bool> alive = alive_;
        pending_ = kernel_.ScheduleAt(due, [this, alive, due, callback = std::move(callback)] {
            if (alive.expired()) {
                return;  // the timer is gone (a stale event)
            }
            pending_ = 0;
            const bool was_running = running_;
            const u64 was_due = running_due_;
            running_ = true;
            running_due_ = due;
            callback();
            running_ = was_running;
            running_due_ = was_due;
        });
    }

    void Cancel() override {
        if (pending_ != 0) {
            kernel_.Cancel(pending_);
            pending_ = 0;
        }
    }

    // The tick the event that runs now was due at, or the clock's outside an event.
    u64 SampleTick() { return running_ ? running_due_ : kernel_.NowTicks(); }

private:
    Kernel& kernel_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    u64 pending_ = 0;
    bool running_ = false;
    u64 running_due_ = 0;
};

// ---- the state --------------------------------------------------------------------------------------------------

Shared::Shared() = default;
Shared::~Shared() = default;

void Shared::Ensure(ServiceContext& context) {
    if (initialised_) {
        return;
    }
    initialised_ = true;
    context_ = &context;
    Kernel& kernel = context.GetKernel();
    connection_status_event = kernel.NewEvent(ResetType::OneShot, "IR:ConnectionStatusEvent");
    send_event = kernel.NewEvent(ResetType::OneShot, "IR:SendEvent");
    receive_event = kernel.NewEvent(ResetType::OneShot, "IR:ReceiveEvent");

    if (context.GetOptions().GetString("--circle-pad-pro", "on") == "off") {
        return;
    }
    // extra_hid = std::make_unique<ExtraHID>([this](std::span<const u8> data) { PutToReceive(data); },
    //                                        system.CoreTiming(), system.Movie());
    timer_ = std::make_unique<QueueTimer>(kernel);
    device_ = std::make_unique<ir_device::CirclePadPro>(
        [this](std::span<const u8> payload) { PutToReceive(payload); }, *timer_, [this] {
            InputSource* input = context_->Interfaces().input;
            return input != nullptr ? input->Sample(timer_->SampleTick()) : InputState{};
        });
}

std::shared_ptr<Shared> GetShared(ServiceContext& context) {
    std::shared_ptr<Shared> shared = context.State<Shared>("ir:USER");
    shared->Ensure(context);
    return shared;
}

std::string HexBytes(std::span<const u8> bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    for (const u8 byte : bytes) {
        if (!out.empty()) {
            out.push_back(' ');
        }
        out.push_back(kDigits[byte >> 4]);
        out.push_back(kDigits[byte & 15]);
    }
    return out;
}

std::vector<u8> MakePacket(u8 network_id, std::span<const u8> payload) {
    const std::size_t size = payload.size();
    std::vector<u8> packet;

    // Builds packet header. For the format info:
    // https://www.3dbrew.org/wiki/IRUSER_Shared_Memory#Packet_structure

    // fixed value
    packet.push_back(0xA5);
    // destination network ID
    packet.push_back(network_id);

    // puts the size info: one byte for a size that fits in 6 bits, else two bytes with the second
    // highest bit of the first set (the size's high bits under it, its low 8 bits in the second).
    if (size < 0x40) {
        packet.push_back(static_cast<u8>(size));
    } else if (size < 0x4000) {
        packet.push_back(static_cast<u8>(size >> 8) | 0x40);
        packet.push_back(static_cast<u8>(size));
    } else {
        return {};  // Azahar: ASSERT(false)
    }

    // puts the payload
    packet.insert(packet.end(), payload.begin(), payload.end());

    // calculates CRC and puts to the end
    packet.push_back(Crc8(packet));
    return packet;
}

// Wraps the payload into packet and puts it to the receive buffer
void Shared::PutToReceive(std::span<const u8> payload) {
    MH4N_LOG(Trace, kLog, "PutToReceive: data={}", HexBytes(payload));
    if (!shared_memory || !receive_buffer) {
        return;  // Azahar has no buffer to put it in either (the device is disconnected by Finalize)
    }
    const u8 network_id =
        shared_memory->Size() > kNetworkIdOffset ? shared_memory->GetPointer()[kNetworkIdOffset] : 0;
    const std::vector<u8> packet = MakePacket(network_id, payload);
    if (packet.empty()) {
        MH4N_LOG(Error, kLog, "a packet of {} bytes is too big for the receive buffer's format", payload.size());
        return;
    }
    if (receive_buffer->Put(packet)) {
        full_logged_ = false;
        receive_event->Signal();
    } else if (!full_logged_) {
        // Azahar logs each packet; here once until a packet fits again.
        full_logged_ = true;
        MH4N_LOG(Error, kLog, "receive buffer is full!");
    }
}

namespace {

// A bad --circle-pad-pro ends the run before the program runs (the device itself is made when the game
// first opens ir:USER, or by --capture-hid at boot, as N1-16 made the events).
void CheckOption(Runtime& rt) {
    const std::string mode = rt.GetOptions().GetString("--circle-pad-pro", "on");
    if (mode != "on" && mode != "off") {
        const std::string message = "--circle-pad-pro: '" + mode + "' is not on or off";
        MH4N_LOG(Error, kLog, "{}", message);
        rt.RequestExit(2, message);
    }
}

MH4N_REGISTER(BootStep, "services.ir.options", BootStep{Phase::Services, CheckOption});

}  // namespace

}  // namespace mh4n::ir
