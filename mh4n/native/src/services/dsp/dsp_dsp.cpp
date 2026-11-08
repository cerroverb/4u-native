// Derived from Azahar src/core/hle/service/dsp/dsp_dsp.cpp, src/core/hle/service/dsp/dsp_dsp.h,
// src/audio_core/hle/hle.cpp and src/core/hle/result.h at 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6
// (GPL-2.0-or-later: each of them says "Licensed under GPLv2 or any later version"): the commands of
// dsp::DSP, their command ids, parameters, replies and names (the table of FunctionInfo), the
// translation of a DSP address (ConvertProcessAddressFromDspDram), the changes the application makes
// to the pipes (WriteProcessPipe: the bytes the game leaves at random in a message are cleared),
// the six interrupt slots and the result for too many events, the interrupts the DSP raises
// (SignalInterrupt, the audio tick that raises the audio pipe's) and the audio frame's timer
// (AudioTickCallback).
// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project (dsp_dsp.cpp)
// Copyright 2014-2025 Citra Emulator Project / Azahar Emulator Project (dsp_dsp.h)
// Copyright 2017-2026 Citra Emulator Project / Azahar Emulator Project (hle.cpp)
// Copyright 2014-2024 Citra Emulator Project / Azahar Emulator Project (result.h)
//
// The dsp::DSP service (N1-18): the application's way to the DSP. The DSP behind it (N3-11, `--dsp`)
// is our HLE DSP (dsp_hle::Dsp, the default `hle`), whose frames go to the runtime's AudioSink, or
// N1-18's model of mh4n_audio (`status`: no sound, a frame of silence to the sink). It advances once
// every audio frame, 1,310,720 ticks, from the timer queue of the scheduler: the application's sound
// thread waits for the interrupt of the audio pipe and then writes the next region, so the cadence is
// part of what the game can observe.
//
// Departures from Azahar:
//  - A channel, an interrupt number or a register that Azahar asserts on, a message too short for its
//    pipe, and a ReadPipe for more than the pipe holds (which "hangs the hardware"), are logged and
//    answered: the register as register 0, the others with an error result (RegisterInterruptEvents:
//    a descriptor 1005 of the DSP module) or an empty buffer. A LoadComponent whose size is larger
//    than its buffer reads only what the buffer holds. A LoadComponent without a valid mapped-buffer
//    descriptor answers kResultInvalidBufferDescriptor (Azahar asserts).
//  - FlushDataCache and InvalidateDCache answer success when the process handle is not a process
//    (Azahar dereferences it in its log line).
//  - Reply::Push of a u8, a u16 or a bool (RecvData, RecvDataIsReady, GetPipeReadableSize,
//    ReadPipeIfPossible, GetHeadphoneStatus) writes only those bytes, and the other bytes of the reply
//    word are the request's bytes at the same index, as in Azahar (D53, mh4n/ipc.h); that is not a
//    departure, the framework does it for every service.
//  - The firmware is identified in the log by the CRC-32 of its bytes (Azahar logs a CityHash).
//  - GetHeadphoneStatus answers "not inserted" (Azahar's setting simulate_headphones_plugged is off).
//  - All sessions share one DSP, as Azahar's single DSP_DSP object does, and so do their events.
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <vector>

#include "audio/dsp_layout.h"
#include "audio/dsp_model.h"
#include "dsp_hle/dsp.h"
#include "dsp_shared.h"
#include "mh4n/assert.h"
#include "mh4n/crc32.h"
#include "mh4n/ipc.h"
#include "mh4n/log.h"
#include "mh4n/memory.h"
#include "mh4n/options.h"
#include "mh4n/registry.h"
#include "mh4n/runtime.h"
#include "mh4n/service.h"

namespace mh4n {

namespace {

constexpr const char* kLog = "Service.DSP";

MH4N_OPTION("runner", "--dsp", "hle|status",
            "the DSP behind dsp::DSP: hle (the default) decodes and mixes the game's sound and sends it to "
            "--audio; status is N1-18's model, every status the game sees and no sound");
MH4N_OPTION("runner", "--reverb", "on|off",
            "--dsp hle: the effect sends (the game's reverb, on auxiliary bus 0) mixed in (on, the default), or bypassed "
            "(off: the sound is dry); an app changes it while the game runs (dsp_hle::SetReverb)");

// ErrorModule::DSP, and the pieces of the results below (Azahar's result.h).
constexpr u32 kModuleDsp = 41;
constexpr u32 kDescriptionInvalidResultValue = 1023;
constexpr u32 kDescriptionInvalidEnumValue = 1005;
constexpr u32 kSummaryOutOfResource = 3;
constexpr u32 kSummaryInvalidArgument = 7;
constexpr u32 kLevelStatus = 25;
constexpr u32 kLevelPermanent = 27;
// "Ran out of space to register interrupts".
constexpr Result kResultTooManyInterruptEvents =
    MakeResult(kDescriptionInvalidResultValue, kModuleDsp, kSummaryOutOfResource, kLevelStatus);
static_assert(kResultTooManyInterruptEvents == 0xC860A7FF);
// Where Azahar asserts on an interrupt or channel that does not exist.
constexpr Result kResultInvalidInterrupt =
    MakeResult(kDescriptionInvalidEnumValue, kModuleDsp, kSummaryInvalidArgument, kLevelPermanent);

// The offset ConvertProcessAddressFromDspDram adds to the DSP RAM's address in the application's space
// (kDspRamVaddr, memory.h) for a DSP word address.
constexpr u32 kDspDramOffset = 0x40000;

// The DSP of either kind behind the calls of DspEngine.
template <class Dsp>
class EngineOf final : public DspEngine {
public:
    explicit EngineOf(std::unique_ptr<Dsp> dsp) : dsp_(std::move(dsp)) {}

    audio::DspState State() const override { return dsp_->State(); }
    u16 RecvData(u32 register_number) override { return dsp_->RecvData(register_number); }
    bool RecvDataIsReady(u32 register_number) const override {
        return dsp_->RecvDataIsReady(register_number);
    }
    void SetSemaphore(u16 value) override { dsp_->SetSemaphore(value); }
    std::vector<u8> PipeRead(u32 pipe, std::size_t length) override { return dsp_->PipeRead(pipe, length); }
    std::size_t PipeReadableSize(u32 pipe) const override { return dsp_->PipeReadableSize(pipe); }
    void PipeWrite(u32 pipe, std::span<const u8> buffer) override {
        if constexpr (!kCounts) {
            // The model answers every AAC decode request with a failure, as dsp_hle::Dsp does; count
            // the same requests that Dsp counts (a message of the right size, codec and command).
            if (pipe == static_cast<u32>(audio::DspPipe::Binary) && buffer.size() == audio::kBinaryMessageSize) {
                u16 codec, command;
                std::memcpy(&codec, buffer.data(), 2);
                std::memcpy(&command, buffer.data() + 2, 2);
                if (codec == audio::kCodecDecodeAac && command == audio::kCommandEncodeDecode) {
                    ++aac_decode_requests_;
                }
            }
        }
        dsp_->PipeWrite(pipe, buffer);
    }
    bool Tick() override { return dsp_->Tick(); }
    u64 AacDecodeRequests() const override {
        if constexpr (kCounts) {
            return dsp_->AacDecodeRequests();
        } else {
            return aac_decode_requests_;
        }
    }

private:
    static constexpr bool kCounts = requires(const Dsp& dsp) { dsp.AacDecodeRequests(); };
    std::unique_ptr<Dsp> dsp_;
    u64 aac_decode_requests_ = 0;
};

}  // namespace

// ---- the sources' samples ----------------------------------------------------------------------------------

std::span<const u8> GuestSampleMemory::Read(u32 physical_address, u32 size) {
    std::span<const u8> bytes;
    if (size != 0) {
        if (const u8* const whole = memory_.PhysicalPointer(physical_address, size)) {
            bytes = {whole, size};
        } else if (const u8* const start = memory_.PhysicalPointer(physical_address, 0)) {
            // Not all of it: the pages from the first on that are mapped and follow each other (the
            // rest of the range reads as zeros, as the source does with a short span).
            const u64 end = static_cast<u64>(physical_address) + size;
            u64 at = physical_address;
            while (at < end) {
                const u64 next = std::min<u64>((at | 0xFFF) + 1, end);
                const u8* const piece = memory_.PhysicalPointer(static_cast<u32>(at), static_cast<u32>(next - at));
                if (piece != start + (at - physical_address)) {
                    break;
                }
                at = next;
            }
            bytes = {start, static_cast<std::size_t>(at - physical_address)};
        }
    }
    if (observer) {
        observer(physical_address, size, bytes);
    }
    return bytes;
}

// ---- what the sessions share -------------------------------------------------------------------------------

DspShared::DspShared() = default;

DspShared::~DspShared() {
    if (timer_id_ != 0 && context_ != nullptr) {
        context_->GetKernel().Cancel(timer_id_);
    }
}

void DspShared::Start(ServiceContext& context, bool at_boot) {
    if (engine_ != nullptr) {
        return;
    }
    u8* const memory = context.Memory().PhysicalPointer(kDspRamPaddr, kDspRamSize);
    MH4N_ASSERT(memory != nullptr, "the DSP RAM is not mapped");
    context_ = &context;
    capture_ = context.State<DspCapture>("dsp::capture");

    const std::string mode = context.GetOptions().GetString("--dsp", "hle");
    if (mode == "status") {
        mode_ = DspMode::Status;
    } else {
        mode_ = DspMode::Hle;
        if (mode != "hle") {
            MH4N_LOG(Error, kLog, "bad --dsp: {} (want hle or status)", mode);
            context.RequestExit(2, "bad --dsp: " + mode + " (want hle or status)");
        }
    }
    const auto interrupt = [this](audio::InterruptType type, audio::DspPipe pipe) { SignalInterrupt(type, pipe); };
    const auto frame_start = [this](const u8* dsp_memory) { CaptureFrame(dsp_memory); };
    if (mode_ == DspMode::Hle) {
        auto dsp = std::make_unique<dsp_hle::Dsp>(memory);
        sample_memory_ = std::make_unique<GuestSampleMemory>(context.Memory());
        sample_memory_->observer = [this](u32 address, u32 size, std::span<const u8> data) {
            CaptureSampleRead(address, size, data);
        };
        dsp->SetSampleMemory(sample_memory_.get());
        dsp->SetInterruptHandler(interrupt);
        dsp->SetFrameStartHook(frame_start);
        dsp->SetFrameOutput([this](std::span<const s16> samples) { OutputFrame(samples); });
        engine_ = std::make_unique<EngineOf<dsp_hle::Dsp>>(std::move(dsp));
    } else {
        auto model = std::make_unique<audio::DspModel>(memory);
        model->SetInterruptHandler(interrupt);
        model->SetFrameStartHook(frame_start);
        engine_ = std::make_unique<EngineOf<audio::DspModel>>(std::move(model));
    }
    MH4N_LOG(Info, kLog, "the DSP is {}", mode_ == DspMode::Hle ? "hle" : "status");
    if (context.GetOptions().Has("--reverb")) {  // (process-wide: dsp_hle::SetReverb, which an app also changes live)
        const std::string reverb = context.GetOptions().GetString("--reverb", "on");
        dsp_hle::SetReverb(!(reverb == "off" || reverb == "0" || reverb == "false" || reverb == "no"));
        MH4N_LOG(Info, kLog, "reverb {}", dsp_hle::Reverb() ? "on" : "off: the effect sends are bypassed");
    }

    semaphore_event = context.GetKernel().NewEvent(ResetType::OneShot, "DSP_DSP::semaphore_event");
    semaphore_event->SetHleNotifier([this] { engine_->SetSemaphore(preset_semaphore); });

    // The frames are on a grid from tick 0, as Azahar's are (its first tick is one frame after the
    // start of the core timing): a DSP that is first used later runs on the same grid.
    deadline_ = at_boot ? context.GetKernel().NowTicks()
                        : context.GetKernel().NowTicks() / audio::kAudioFrameTicks * audio::kAudioFrameTicks;
    ScheduleNext();
}

void DspShared::ScheduleNext() {
    // From the previous deadline, not from now: a frame that runs late is made up for (Azahar
    // reschedules with `audio_frame_ticks - cycles_late`).
    deadline_ += audio::kAudioFrameTicks;
    timer_id_ = context_->GetKernel().ScheduleAt(
        deadline_, [weak = weak_from_this()] {
            if (const std::shared_ptr<DspShared> self = weak.lock()) {
                self->OnTick();
            }
        });
}

void DspShared::OnTick() {
    timer_id_ = 0;
    ++frames_run_;
    if (frame_observer) {
        frame_observer(deadline_, context_->GetKernel().NowTicks());
    }
    if (engine_->Tick()) {
        if (mode_ == DspMode::Status) {
            // The frame goes to the sink: silence, since the model mixes nothing. (The HLE DSP hands its
            // frame to OutputFrame itself, inside Tick.)
            static const std::array<s16, 2 * audio::kSamplesPerFrame> silence{};
            if (AudioSink* const sink = context_->Interfaces().audio) {
                sink->Queue(silence.data(), audio::kSamplesPerFrame);
            }
        }
        // TODO(merry) in Azahar: "Signal all the other interrupts as appropriate."
        SignalInterrupt(audio::InterruptType::Pipe, audio::DspPipe::Audio);
    }
    ScheduleNext();
}

void DspShared::OutputFrame(std::span<const s16> samples) {
    DspCapture& capture = *capture_;
    if (capture.in_frame) {
        capture.in_frame = false;
        capture.writer.AddPcm(capture.frame_now, capture.ticks_now, samples.data());
        if (capture.writer.Frames() >= capture.frames) {
            capture.writer.Close();  // the hooks stay set and do nothing from now on
        }
    }
    if (AudioSink* const sink = context_->Interfaces().audio) {
        sink->Queue(samples.data(), samples.size() / 2);
    }
}

void DspShared::CaptureFrame(const u8* dsp_memory) {
    DspCapture& capture = *capture_;
    capture.in_frame = false;
    if (!capture.writer.IsOpen()) {
        return;
    }
    // The game's frame counter, read now; a program that does not map it has no frames.
    u32 frame = 0;
    if (const auto value = context_->Memory().Read<u32>(kFrameCounterAddress)) {
        frame = *value;
    }
    if (!capture.started && frame < capture.start_frame) {
        return;
    }
    capture.started = true;
    const u64 ticks = context_->GetKernel().NowTicks() - capture.origin_tick;
    if (!capture.writer.AddRegions(frame, ticks, dsp_memory + audio::kRegionOffset[0],
                                   dsp_memory + audio::kRegionOffset[1], audio::kRegionSize)) {
        return;
    }
    if (mode_ == DspMode::Hle) {
        // The frame's sample reads (type 3) and its output (type 2) follow, with the same frame and
        // ticks; the capture closes after the output of its last frame.
        capture.in_frame = true;
        capture.frame_now = frame;
        capture.ticks_now = ticks;
    } else if (capture.writer.Frames() >= capture.frames) {
        capture.writer.Close();  // the hook stays set and does nothing from now on
    }
}

void DspShared::CaptureSampleRead(u32 physical_address, u32 size, std::span<const u8> data) {
    DspCapture& capture = *capture_;
    if (capture.in_frame) {
        capture.writer.AddSampleRead(capture.frame_now, capture.ticks_now, physical_address, size, data);
    }
}

std::shared_ptr<Event>* DspShared::InterruptEvent(audio::InterruptType type, audio::DspPipe pipe) {
    switch (type) {
    case audio::InterruptType::Zero:
        return &interrupt_zero;
    case audio::InterruptType::One:
        return &interrupt_one;
    case audio::InterruptType::Pipe: {
        const std::size_t index = static_cast<std::size_t>(pipe);
        return index < pipes.size() ? &pipes[index] : nullptr;
    }
    default:
        return nullptr;
    }
}

bool DspShared::HasTooManyEventsRegistered() const {
    std::size_t number = static_cast<std::size_t>(
        std::count_if(pipes.begin(), pipes.end(), [](const auto& event) { return event != nullptr; }));
    if (interrupt_zero != nullptr) {
        ++number;
    }
    if (interrupt_one != nullptr) {
        ++number;
    }
    return number >= kMaxInterruptEvents;
}

void DspShared::SignalInterrupt(audio::InterruptType type, audio::DspPipe pipe) {
    MH4N_LOG(Trace, kLog, "called, type={}, pipe={}", static_cast<u32>(type), static_cast<u32>(pipe));
    if (std::shared_ptr<Event>* const slot = InterruptEvent(type, pipe)) {
        if (*slot) {
            (*slot)->Signal();
        }
    }
}

// ---- the service -----------------------------------------------------------------------------------------------

namespace {

class DspDsp final : public Service {
public:
    explicit DspDsp(ServiceContext& context)
        : Service(context, "dsp::DSP"), shared_(context.State<DspShared>("dsp::DSP")) {
        shared_->Start(context);
        Register(0x0001, "RecvData", &DspDsp::RecvData);
        Register(0x0002, "RecvDataIsReady", &DspDsp::RecvDataIsReady);
        Register(0x0003, "SendData");
        Register(0x0004, "SendDataIsEmpty");
        Register(0x0005, "SendFifoEx");
        Register(0x0006, "RecvFifoEx");
        Register(0x0007, "SetSemaphore", &DspDsp::SetSemaphore);
        Register(0x0008, "GetSemaphore");
        Register(0x0009, "ClearSemaphore");
        Register(0x000A, "MaskSemaphore");
        Register(0x000B, "CheckSemaphoreRequest");
        Register(0x000C, "ConvertProcessAddressFromDspDram",
                 &DspDsp::ConvertProcessAddressFromDspDram);
        Register(0x000D, "WriteProcessPipe", &DspDsp::WriteProcessPipe);
        Register(0x000E, "ReadPipe", &DspDsp::ReadPipe);
        Register(0x000F, "GetPipeReadableSize", &DspDsp::GetPipeReadableSize);
        Register(0x0010, "ReadPipeIfPossible", &DspDsp::ReadPipeIfPossible);
        Register(0x0011, "LoadComponent", &DspDsp::LoadComponent);
        Register(0x0012, "UnloadComponent", &DspDsp::UnloadComponent);
        Register(0x0013, "FlushDataCache", &DspDsp::FlushDataCache);
        Register(0x0014, "InvalidateDCache", &DspDsp::InvalidateDataCache);
        Register(0x0015, "RegisterInterruptEvents", &DspDsp::RegisterInterruptEvents);
        Register(0x0016, "GetSemaphoreEventHandle", &DspDsp::GetSemaphoreEventHandle);
        Register(0x0017, "SetSemaphoreMask", &DspDsp::SetSemaphoreMask);
        Register(0x0018, "GetPhysicalAddress");
        Register(0x0019, "GetVirtualAddress");
        Register(0x001A, "SetIirFilterI2S1_cmd1");
        Register(0x001B, "SetIirFilterI2S1_cmd2");
        Register(0x001C, "SetIirFilterEQ");
        Register(0x001D, "ReadMultiEx_SPI2");
        Register(0x001E, "WriteMultiEx_SPI2");
        Register(0x001F, "GetHeadphoneStatus", &DspDsp::GetHeadphoneStatus);
        Register(0x0020, "ForceHeadphoneOut", &DspDsp::ForceHeadphoneOut);
        Register(0x0021, "GetIsDspOccupied");
    }

private:
    DspEngine& Dsp() { return shared_->Engine(); }

    // Inputs:  0: 0x00010040, 1: the register number
    // Outputs: 0: 0x00010080, 1: result, 2: u16, the value in the register
    void RecvData(Request& request) {
        const u32 register_number = request.Pop<u32>();
        Reply& reply = request.MakeReply(2, 0);
        reply.Push(kResultSuccess);
        reply.Push<u16>(Dsp().RecvData(register_number));
        MH4N_LOG(Debug, kLog, "register_number={}", register_number);
    }

    // Inputs:  0: 0x00020040, 1: the register number
    // Outputs: 0: 0x00020080, 1: result, 2: 0 not ready, 1 ready
    void RecvDataIsReady(Request& request) {
        const u32 register_number = request.Pop<u32>();
        Reply& reply = request.MakeReply(2, 0);
        reply.Push(kResultSuccess);
        reply.Push(Dsp().RecvDataIsReady(register_number));
        MH4N_LOG(Debug, kLog, "register_number={}", register_number);
    }

    // Inputs:  0: 0x00070040, 1: u16, the semaphore value
    void SetSemaphore(Request& request) {
        const u16 semaphore_value = request.Pop<u16>();
        Dsp().SetSemaphore(semaphore_value);
        request.MakeReply(1, 0).Push(kResultSuccess);
        MH4N_LOG(Info, kLog, "called, semaphore_value={:04X}", semaphore_value);
    }

    // Inputs:  0: 0x000C0040, 1: the address (a word address in the DSP's DRAM)
    // Outputs: 0: 0x000C0080, 1: result, 2: the address in the application's space
    void ConvertProcessAddressFromDspDram(Request& request) {
        const u32 address = request.Pop<u32>();
        Reply& reply = request.MakeReply(2, 0);
        reply.Push(kResultSuccess);
        // "There is a per-region offset missing in this calculation (that seems to be always zero)."
        reply.Push<u32>((address << 1) + (kDspRamVaddr + kDspDramOffset));
        MH4N_LOG(Debug, kLog, "address=0x{:08X}", address);
    }

    // Inputs:  0: 0x000D0082, 1: the channel, 2: the size, 3: the static buffer descriptor, 4: its address
    void WriteProcessPipe(Request& request) {
        const u32 channel = request.Pop<u32>();
        const u32 size = request.Pop<u32>();
        const StaticBuffer* const message = request.PopStaticBuffer();
        std::vector<u8> buffer;
        if (message != nullptr) {
            buffer = message->data;
        }

        // This behaviour was confirmed by RE. The likely reason for this is that games tend to pass in
        // garbage at these bytes because they read random bytes off the stack.
        bool usable = true;
        switch (static_cast<audio::DspPipe>(channel)) {
        case audio::DspPipe::Audio:
            if (buffer.size() >= 4) {
                buffer[2] = 0;
                buffer[3] = 0;
            } else {
                usable = false;  // Azahar asserts
            }
            break;
        case audio::DspPipe::Binary:
            if (buffer.size() >= 8) {
                buffer[4] = 1;
                buffer[5] = 0;
                buffer[6] = 0;
                buffer[7] = 0;
            } else {
                usable = false;  // Azahar asserts
            }
            break;
        default:
            MH4N_LOG(Error, kLog, "Unknown pipe {}", channel);
            break;
        }
        if (usable) {
            Dsp().PipeWrite(channel, buffer);
        } else {
            MH4N_LOG(Error, kLog, "pipe {}: a message of {} bytes is too short; it is ignored",
                     channel, buffer.size());
        }

        request.MakeReply(1, 0).Push(kResultSuccess);
        MH4N_LOG(Debug, kLog, "channel={}, size=0x{:X}, buffer_size={:X}", channel, size,
                 buffer.size());
    }

    // Inputs:  0: 0x000E00C0, 1: the channel, 2: the peer, 3: the size
    // Outputs: 0: 0x000E0042, 1: result, 2: the static buffer descriptor, 3: its address
    void ReadPipe(Request& request) {
        const u32 channel = request.Pop<u32>();
        const u32 peer = request.Pop<u32>();
        const u16 size = request.Pop<u16>();
        const std::size_t readable = Dsp().PipeReadableSize(channel);
        std::vector<u8> pipe_buffer;
        if (readable >= size) {
            pipe_buffer = Dsp().PipeRead(channel, size);
        } else {
            // "No more data is in pipe. Hardware hangs in this case; Should never happen." Azahar
            // aborts here.
            MH4N_LOG(Error, kLog, "ReadPipe of {} bytes from pipe {} with {} readable", size, channel,
                     readable);
        }
        Reply& reply = request.MakeReply(1, 2);
        reply.Push(kResultSuccess);
        reply.PushStaticBuffer(std::move(pipe_buffer), 0);
        MH4N_LOG(Debug, kLog, "channel={}, peer={}, size=0x{:04X}, pipe_readable_size=0x{:04X}", channel,
                 peer, size, readable);
    }

    // Inputs:  0: 0x000F0080, 1: the channel, 2: the peer
    // Outputs: 0: 0x000F0080, 1: result, 2: u16, the readable size
    void GetPipeReadableSize(Request& request) {
        const u32 channel = request.Pop<u32>();
        const u32 peer = request.Pop<u32>();
        const u16 readable = static_cast<u16>(Dsp().PipeReadableSize(channel));
        Reply& reply = request.MakeReply(2, 0);
        reply.Push(kResultSuccess);
        reply.Push<u16>(readable);
        MH4N_LOG(Debug, kLog, "channel={}, peer={}, return pipe_readable_size=0x{:04X}", channel, peer,
                 readable);
    }

    // Inputs:  0: 0x001000C0, 1: the channel, 2: the peer, 3: the size
    // Outputs: 0: 0x00100082, 1: result, 2: u16, the bytes read, 3: the static buffer descriptor,
    //          4: its address
    void ReadPipeIfPossible(Request& request) {
        const u32 channel = request.Pop<u32>();
        const u32 peer = request.Pop<u32>();
        const u16 size = request.Pop<u16>();
        const u16 readable = static_cast<u16>(Dsp().PipeReadableSize(channel));
        std::vector<u8> pipe_buffer;
        if (readable >= size) {
            pipe_buffer = Dsp().PipeRead(channel, size);
        }
        Reply& reply = request.MakeReply(2, 2);
        reply.Push(kResultSuccess);
        reply.Push<u16>(static_cast<u16>(pipe_buffer.size()));
        reply.PushStaticBuffer(std::move(pipe_buffer), 0);
        MH4N_LOG(Debug, kLog, "channel={}, peer={}, size=0x{:04X}, pipe_readable_size=0x{:04X}", channel,
                 peer, size, readable);
    }

    // Inputs:  0: 0x001100C2, 1: the size, 2: u16 program mask, u16 data mask, 3: the mapped buffer
    //          descriptor, 4: its address
    // Outputs: 0: 0x00110082, 1: result, 2: true, 3: the mapped buffer descriptor, 4: its id
    void LoadComponent(Request& request) {
        const u32 size = request.Pop<u32>();
        const u16 prog_mask = request.Pop<u16>();
        const u16 data_mask = request.Pop<u16>();
        const MappedBuffer* const buffer = request.PopMappedBuffer();
        if (buffer == nullptr) {
            request.MakeReply(1, 0).Push(kResultInvalidBufferDescriptor);
            return;
        }
        Reply& reply = request.MakeReply(2, 2);
        reply.Push(kResultSuccess);
        reply.Push(true);
        reply.PushMappedBuffer(*buffer);

        // The firmware is only identified: the HLE needs no DSP program.
        const u32 readable = std::min(size, buffer->Size());
        std::vector<u8> component_data(readable);
        if (readable != 0 && buffer->Read(component_data.data(), 0, readable)) {
            MH4N_LOG(Info, kLog, "Firmware crc32: {:#010x}",
                     Crc32(component_data.data(), component_data.size()));
        }
        MH4N_LOG(Info, kLog, "called size=0x{:X}, prog_mask=0x{:04X}, data_mask=0x{:04X}", size,
                 prog_mask, data_mask);
    }

    // Inputs:  0: 0x00120000
    void UnloadComponent(Request& request) {
        request.MakeReply(1, 0).Push(kResultSuccess);
        MH4N_LOG(Info, kLog, "called");
    }

    // Inputs:  0: 0x00130082, 1: the address, 2: the size, 3: the copy handle descriptor, 4: the process
    void FlushDataCache(Request& request) {
        const u32 address = request.Pop<u32>();
        const u32 size = request.Pop<u32>();
        const auto process = request.PopObject<Process>();
        request.MakeReply(1, 0).Push(kResultSuccess);
        MH4N_LOG(Trace, kLog, "called address=0x{:08X}, size=0x{:X}, process={}", address, size,
                 process ? process->process_id : 0);
    }

    // Inputs:  0: 0x00140082, as FlushDataCache
    void InvalidateDataCache(Request& request) {
        const u32 address = request.Pop<u32>();
        const u32 size = request.Pop<u32>();
        const auto process = request.PopObject<Process>();
        request.MakeReply(1, 0).Push(kResultSuccess);
        MH4N_LOG(Trace, kLog, "called address=0x{:08X}, size=0x{:X}, process={}", address, size,
                 process ? process->process_id : 0);
    }

    // Inputs:  0: 0x00150082, 1: the interrupt, 2: the channel, 3: the copy handle descriptor, 4: the
    //          event (0 unregisters)
    void RegisterInterruptEvents(Request& request) {
        const u32 interrupt = request.Pop<u32>();
        const u32 channel = request.Pop<u32>();
        const auto event = request.PopObject<Event>();

        if (interrupt >= static_cast<u32>(audio::InterruptType::Count) || channel >= audio::kNumPipes) {
            // Azahar asserts.
            MH4N_LOG(Error, kLog, "Invalid type or pipe: interrupt = {}, channel = {}", interrupt,
                     channel);
            request.MakeReply(1, 0).Push(kResultInvalidInterrupt);
            return;
        }
        const auto type = static_cast<audio::InterruptType>(interrupt);
        const auto pipe = static_cast<audio::DspPipe>(channel);
        std::shared_ptr<Event>& slot = *shared_->InterruptEvent(type, pipe);

        Reply& reply = request.MakeReply(1, 0);
        if (event) {  // register the interrupt event
            if (shared_->HasTooManyEventsRegistered()) {
                MH4N_LOG(Info, kLog,
                         "Ran out of space to register interrupts (Attempted to register "
                         "interrupt={}, channel={}, event={})",
                         interrupt, channel, event->Name());
                reply.Push(kResultTooManyInterruptEvents);
                return;
            }
            slot = event;
            MH4N_LOG(Info, kLog, "Registered interrupt={}, channel={}, event={}", interrupt, channel,
                     event->Name());
        } else {  // otherwise unregister it
            slot = nullptr;
            MH4N_LOG(Info, kLog, "Unregistered interrupt={}, channel={}", interrupt, channel);
        }
        reply.Push(kResultSuccess);
    }

    // Outputs: 0: 0x00160042, 1: result, 2: the translation descriptor, 3: the semaphore event
    void GetSemaphoreEventHandle(Request& request) {
        Reply& reply = request.MakeReply(1, 2);
        reply.Push(kResultSuccess);
        reply.PushCopyObjects(shared_->semaphore_event);
        MH4N_LOG(Warning, kLog, "(STUBBED) called");
    }

    // Inputs:  0: 0x00170040, 1: u16, the mask
    void SetSemaphoreMask(Request& request) {
        shared_->preset_semaphore = request.Pop<u16>();
        request.MakeReply(1, 0).Push(kResultSuccess);
        MH4N_LOG(Warning, kLog, "(STUBBED) called mask=0x{:04X}", shared_->preset_semaphore);
    }

    // Outputs: 0: 0x001F0080, 1: result, 2: u8, 0 not inserted, 1 inserted
    void GetHeadphoneStatus(Request& request) {
        Reply& reply = request.MakeReply(2, 0);
        reply.Push(kResultSuccess);
        reply.Push<u8>(0);
    }

    // Inputs:  0: 0x00200040, 1: u8, 0 don't force, 1 force
    void ForceHeadphoneOut(Request& request) {
        const u8 force = request.Pop<u8>();
        request.MakeReply(1, 0).Push(kResultSuccess);
        MH4N_LOG(Debug, kLog, "(STUBBED) called, force={}", force);
    }

    std::shared_ptr<DspShared> shared_;
};

// Ten sessions at most (Azahar's DefaultMaxSessions).
MH4N_REGISTER(ServiceFactory, "dsp::DSP",
              ServiceFactory([](ServiceContext& context) { return std::make_unique<DspDsp>(context); },
                             kDefaultMaxSessions));

// Azahar's HLE DSP is made with the system and its frame event runs from the first tick of the core
// timing, whether or not the game ever opens dsp::DSP: the frames are on the grid from tick 0 and each is
// an event of the timeline (a slice of the deterministic mode ends at it, the reference's trace shows it).
// So the DSP starts here, at boot, and the service finds it going. The name sorts after "services.ipc",
// which makes the service manager the state lives in.
//
// This is so in every mode, real time included, as it is in Azahar: the frame event runs from boot. Until the
// game turns the DSP on (both DSPs start Off, and Tick() then does nothing and queues nothing) it is one timer
// event per frame that does nothing; after that the frames go to the audio sink as they did when the service was
// first opened. What changes in real time is where the grid of frames starts: at the clock's first reading, which
// is not a multiple of the frame (in the deterministic mode that reading is the reference's starting tick).
//
// At the end of the run one Info line says how many AAC decodes the game asked for (N3-11: the HLE DSP answers
// each with a failure, and a count above 0 means the game needs a decoder).
void StartDsp(Runtime& rt) {
    ServiceManager* manager = FindServiceManager(rt);
    if (manager == nullptr) {
        MH4N_LOG(Error, kLog, "the DSP needs the service manager");
        return;
    }
    const std::shared_ptr<DspShared> shared = manager->Context().State<DspShared>("dsp::DSP");
    shared->Start(manager->Context(), /*at_boot=*/true);
    rt.OnFinish([weak = std::weak_ptr<DspShared>(shared)] {
        const std::shared_ptr<DspShared> self = weak.lock();
        MH4N_LOG(Info, kLog, "dsp: {} AAC requests", self != nullptr ? self->AacDecodeRequests() : 0);
    });
}

MH4N_REGISTER(BootStep, "services.ipc.dsp", BootStep{Phase::Services, StartDsp});

}  // namespace

}  // namespace mh4n
