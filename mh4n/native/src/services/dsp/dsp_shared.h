// Derived from Azahar src/core/hle/service/dsp/dsp_dsp.h at 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6
// (GPL-2.0-or-later: the file says "Licensed under GPLv2 or any later version"): the state DSP_DSP
// keeps for all its sessions (semaphore_event, preset_semaphore, interrupt_zero, interrupt_one, pipes),
// the six interrupt slots (max_number_of_interrupt_events), GetInterruptEvent, HasTooManyEventsRegistered
// and SignalInterrupt.
// Copyright 2014-2025 Citra Emulator Project / Azahar Emulator Project (dsp_dsp.h)
//
// What all the sessions of dsp::DSP share (N1-18), private to mh4n_services_dsp. Azahar has one DSP_DSP
// object, with one DSP behind it, whatever the number of sessions; here the sessions are services of
// their own and this is the part they have in common: the DSP, the events the application registered,
// the timer that runs an audio frame, and the capture.
//
// The DSP (N3-11, `--dsp hle|status`): `hle`, the default, is our HLE DSP (dsp_hle::Dsp, N3-02 and
// N3-03), which reads the sources' samples from the runtime's physical memory (GuestSampleMemory) and
// hands each frame it generates to the runtime's AudioSink; `status` is N1-18's model (audio::DspModel),
// which keeps every status the application sees and outputs silence, for comparison and cheap runs.
// Both answer the service's calls the same way (DspEngine).
//
// Threads (principle 6): everything here belongs to the scheduler thread, the DSP's work and its reads
// of guest memory included. The audio frame runs from the scheduler's timer queue, never from the
// host's clock; each deadline is the previous one plus kAudioFrameTicks, so a late frame is made up for
// and the cadence does not drift.
#pragma once

#include <array>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "audio/dsp_layout.h"
#include "audio/mdsp_writer.h"
#include "dsp_hle/source.h"
#include "mh4n/kernel.h"
#include "mh4n/service.h"

namespace mh4n {

class GuestMemory;

// --capture-dsp: the writer and which audio frames go into it (the boot step opens it).
struct DspCapture {
    audio::MdspWriter writer;
    u32 start_frame = 0;  // the first audio frame at or after this game frame is the first recorded
    u32 frames = 0;       // how many audio frames
    bool started = false;
    u64 origin_tick = 0;  // the game clock when the capture was opened: the records' ticks count from it
    // The audio frame being recorded, from its type-1 record to its type-2 (--dsp hle): its game frame
    // and ticks, which its type-3 and type-2 records carry too.
    bool in_frame = false;
    u32 frame_now = 0;
    u64 ticks_now = 0;
};

// --dsp: the DSP behind the service.
enum class DspMode { Hle, Status };

// The calls the service makes on its DSP: those audio::DspModel and dsp_hle::Dsp both have, and the count
// of AAC decode requests (N3-03's, which the model's adapter counts the same way).
class DspEngine {
public:
    virtual ~DspEngine() = default;
    virtual audio::DspState State() const = 0;
    virtual u16 RecvData(u32 register_number) = 0;
    virtual bool RecvDataIsReady(u32 register_number) const = 0;
    virtual void SetSemaphore(u16 value) = 0;
    virtual std::vector<u8> PipeRead(u32 pipe, std::size_t length) = 0;
    virtual std::size_t PipeReadableSize(u32 pipe) const = 0;
    virtual void PipeWrite(u32 pipe, std::span<const u8> buffer) = 0;
    // One audio frame: true when the DSP was on and generated it.
    virtual bool Tick() = 0;
    virtual u64 AacDecodeRequests() const = 0;
};

// The sources' samples for dsp_hle::Dsp: the bytes of the runtime's physical memory (FCRAM through the
// linear heap, VRAM, DSP RAM: GuestMemory::PhysicalPointer). Which addresses are valid is the source's
// rule (audio::IsPhysicalAddressValid); here a range the memory does not hold whole gives its mapped
// beginning, and the rest reads as zeros. Every read is shown to `observer` (the capture's type-3
// records).
class GuestSampleMemory final : public dsp_hle::SampleMemory {
public:
    explicit GuestSampleMemory(const GuestMemory& memory) : memory_(memory) {}
    std::span<const u8> Read(u32 physical_address, u32 size) override;

    std::function<void(u32 physical_address, u32 size, std::span<const u8> data)> observer;

private:
    const GuestMemory& memory_;
};

class DspShared : public std::enable_shared_from_this<DspShared> {
public:
    DspShared();
    ~DspShared();
    DspShared(const DspShared&) = delete;
    DspShared& operator=(const DspShared&) = delete;

    // The first session makes the DSP on the DSP RAM (the one `--dsp` names: an unknown name ends the
    // run with status 2), the events and the frame timer; later calls do nothing.
    // `at_boot`: called when the module is made with the system, the way Azahar makes its DSP: the frames are
    // then on a grid that starts at the clock's first reading (its first tick one frame after it), which in
    // the deterministic mode is the reference's starting tick, not a multiple of the frame (in real time, the
    // first reading of the host-driven clock: the boot step calls this in every mode).
    void Start(ServiceContext& context, bool at_boot = false);

    DspEngine& Engine() { return *engine_; }
    ServiceContext& Context() { return *context_; }
    DspMode Mode() const { return mode_; }
    // The AAC decode requests the DSP has answered so far (0 before Start).
    u64 AacDecodeRequests() const { return engine_ != nullptr ? engine_->AacDecodeRequests() : 0; }

    // dsp::DSP's events: the semaphore event, and the interrupts the application registered.
    std::shared_ptr<Event> semaphore_event;
    std::shared_ptr<Event> interrupt_zero;
    std::shared_ptr<Event> interrupt_one;
    std::array<std::shared_ptr<Event>, audio::kNumPipes> pipes{};
    u16 preset_semaphore = 0;

    // The slot of an interrupt, or null for a type or pipe that does not exist.
    std::shared_ptr<Event>* InterruptEvent(audio::InterruptType type, audio::DspPipe pipe);
    // Actual service implementation only has 6 'slots' for interrupts.
    static constexpr std::size_t kMaxInterruptEvents = 6;
    bool HasTooManyEventsRegistered() const;
    void SignalInterrupt(audio::InterruptType type, audio::DspPipe pipe);

    // For tests: the audio frames the timer has run, and the deadline of the next one.
    u64 FramesRun() const { return frames_run_; }
    u64 NextDeadline() const { return deadline_; }
    // For tests: called at the start of every audio frame with its deadline and the game clock.
    std::function<void(u64 deadline, u64 now)> frame_observer;

private:
    void ScheduleNext();
    void OnTick();
    void CaptureFrame(const u8* dsp_memory);
    void CaptureSampleRead(u32 physical_address, u32 size, std::span<const u8> data);
    void OutputFrame(std::span<const s16> samples);

    ServiceContext* context_ = nullptr;
    DspMode mode_ = DspMode::Hle;
    std::unique_ptr<GuestSampleMemory> sample_memory_;  // outlives engine_ (declared first)
    std::unique_ptr<DspEngine> engine_;
    std::shared_ptr<DspCapture> capture_;
    u64 deadline_ = 0;  // the tick of the frame that ran last (or is about to), a multiple of the frame
    u64 timer_id_ = 0;
    u64 frames_run_ = 0;
};

}  // namespace mh4n
