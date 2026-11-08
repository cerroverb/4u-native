// Derived from Azahar src/core/hle/service/ir/ir_user.cpp and src/core/hle/service/ir/extra_hid.cpp at
// 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-2.0-or-later: both say "GPLv2 or any later version"):
// the expected answers (RequireConnection's header bytes for device 1, the packet PutToReceive makes, the
// calibration answer and the status packet of ExtraHID) are the ones those sources give; the CRC-8 values
// were computed independently (boost::crc<8, 0x07, 0, 0, false, false>, Python).
// Copyright 2015-2025 Citra Emulator Project / Azahar Emulator Project (ir_user.cpp)
// Copyright 2017-2024 Citra Emulator Project / Azahar Emulator Project (extra_hid.cpp)
//
// Tests of N3-12's ir:USER: the Circle Pad Pro (ir_device::CirclePadPro, N3-08) connected through the
// service as device 1, driven through the SVC layer as the game does: the connection and its event, the
// requests of SendIrNop reaching the device, its packets in the receive buffer with the receive event, the
// polling period on the game clock and the input sampled at each packet's tick, Disconnect and Finalize,
// --circle-pad-pro off, and the shared block that a Finalize lets go of. Everything is synthetic.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "ir_device/circle_pad_pro.h"
#include "mh4n/clock.h"
#include "mh4n/ipc.h"
#include "mh4n/log.h"
#include "mh4n/options.h"
#include "mh4n/runtime.h"
#include "mh4n/service.h"
#include "services/ir/ir_state.h"

using namespace mh4n;

// ---- the packet (no runtime needed) ---------------------------------------------------------------------------

TEST_CASE("the receive buffer's packet: 0xA5, the network id, the size in one or two bytes, the CRC-8") {
    const std::vector<u8> status = {0x10, 0x00, 0x08, 0x80, 0xFF, 0x00};
    CHECK(ir::MakePacket(0, status) ==
          std::vector<u8>{0xA5, 0x00, 0x06, 0x10, 0x00, 0x08, 0x80, 0xFF, 0x00, 0x9D});
    const std::vector<u8> pushed = {0x10, 0xFF, 0x0F, 0x40, 0xDF, 0x00};
    CHECK(ir::MakePacket(7, pushed) ==
          std::vector<u8>{0xA5, 0x07, 0x06, 0x10, 0xFF, 0x0F, 0x40, 0xDF, 0x00, 0x9C});
    // 0x45 bytes (the whole calibration answer): the extended size, 0x40 | high bits, then the low byte
    std::vector<u8> calibration = {0x11, 0x00, 0x00, 0x40, 0x00};
    calibration.insert(calibration.end(), ir_device::kCalibrationData.begin(), ir_device::kCalibrationData.end());
    const std::vector<u8> packet = ir::MakePacket(0, calibration);
    REQUIRE(packet.size() == 4 + calibration.size() + 1);
    CHECK(packet[0] == 0xA5);
    CHECK(packet[2] == 0x40);
    CHECK(packet[3] == 0x45);
    CHECK(packet.back() == 0x67);
    // the largest size the format holds, and one past it (Azahar asserts)
    CHECK(ir::MakePacket(0, std::vector<u8>(0x3FFF, 1)).size() == 0x3FFF + 5);
    CHECK(ir::MakePacket(0, std::vector<u8>(0x4000, 1)).empty());
    CHECK(ir::HexBytes(status) == "10 00 08 80 ff 00");
}

TEST_CASE("--circle-pad-pro is an option of the runner") {
    const Options options = ParseOptions("runner", {"--circle-pad-pro", "off"});
    REQUIRE_MESSAGE(options.ok(), options.error());
    CHECK(options.GetString("--circle-pad-pro") == "off");
    CHECK(Usage("runner").find("--circle-pad-pro on|off") != std::string::npos);
}

#if !__has_include("recomp.h")
TEST_CASE("ir_cpp_test's service cases need recomp.h (the repository around native/)" * doctest::skip()) {}
#else

#include "../src/cpu/recomp_c.h"  // last: it defines macros

namespace {

namespace fs = std::filesystem;

constexpr u32 kText = 0x00100000;

void Put16(std::vector<u8>& b, std::size_t at, u32 v) {
    b[at] = static_cast<u8>(v), b[at + 1] = static_cast<u8>(v >> 8);
}
void Put32(std::vector<u8>& b, std::size_t at, u32 v) {
    Put16(b, at, v & 0xFFFF), Put16(b, at + 2, v >> 16);
}

std::vector<u8> MakeElf() {
    std::vector<u8> b(52 + 32, 0);
    b[0] = 0x7F, b[1] = 'E', b[2] = 'L', b[3] = 'F', b[4] = 1, b[5] = 1, b[6] = 1;
    Put16(b, 16, 2), Put16(b, 18, 40), Put32(b, 20, 1), Put32(b, 24, kText);
    Put32(b, 28, 52), Put16(b, 40, 52), Put16(b, 42, 32), Put16(b, 44, 1);
    Put32(b, 52, 1);  // PT_LOAD
    Put32(b, 52 + 4, 84), Put32(b, 52 + 8, kText), Put32(b, 52 + 12, kText);
    Put32(b, 52 + 16, 0x40), Put32(b, 52 + 20, 0x40), Put32(b, 52 + 24, 5), Put32(b, 52 + 28, 0x1000);
    for (u32 i = 0; i < 0x40; ++i) {
        b.push_back(static_cast<u8>(i));
    }
    return b;
}

class TempDir {
public:
    TempDir() {
        static std::atomic<int> counter{0};
        path_ = fs::temp_directory_path() /
                ("mh4n_ir_cpp_test_" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
                 std::to_string(counter++));
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }
    std::string WriteElf() const {
        const std::string path = (path_ / "program.elf").string();
        const std::vector<u8> bytes = MakeElf();
        std::ofstream(path, std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return path;
    }

private:
    fs::path path_;
};

struct Fixture {
    std::unique_ptr<Runtime> runtime;
    std::vector<std::string> logged;
    Fixture() {
        RuntimeConfig config;
        config.time = std::make_unique<ManualTimeSource>();
        runtime = std::make_unique<Runtime>(std::move(config));
        SetLogFilter(LogFilter(LogLevel::Info));
        SetLogSink([this](const LogRecord& r) { logged.push_back(std::string(r.message)); });
    }
    ~Fixture() {
        runtime.reset();
        SetLogSink({});
        SetLogFilter(LogFilter(LogLevel::Info));
    }
    std::size_t Count(const std::string& needle) const {
        return static_cast<std::size_t>(std::count_if(logged.begin(), logged.end(), [&](const std::string& l) {
            return l.find(needle) != std::string::npos;
        }));
    }
};

int BootWith(Fixture& f, const TempDir& dir, std::vector<std::string> extra, std::function<void()> body) {
    std::vector<std::string> args = {dir.WriteElf()};
    args.insert(args.end(), extra.begin(), extra.end());
    const Options options = ParseOptions("runner", args);
    REQUIRE_MESSAGE(options.ok(), options.error());
    f.runtime->SetMainThreadEntry([body = std::move(body)](GuestThread&) { body(); });
    return f.runtime->Boot(options);
}

struct Regs {
    std::array<u32, 16> r{};
};

Regs CallSvc(Runtime& rt, u32 number, std::initializer_list<u32> in) {
    RCtx ctx{};
    int i = 0;
    for (const u32 value : in) {
        ctx.r[i++] = value;
    }
    ctx.r[14] = kText + 0x20;
    rt.Svc().CallSvc(ctx, number, kText + 0x11);
    Regs out;
    for (int k = 0; k < 16; ++k) {
        out.r[static_cast<std::size_t>(k)] = ctx.r[k];
    }
    return out;
}

void ExitProcess(Runtime& rt) {
    CallSvc(rt, 0x03, {});
}
void SleepNs(Runtime& rt, u64 ns) {
    CallSvc(rt, 0x0A, {static_cast<u32>(ns), static_cast<u32>(ns >> 32)});
}
void SleepToTick(Runtime& rt, u64 tick) {
    const u64 now = rt.Clock().Now();
    if (tick > now) {
        SleepNs(rt, TicksToNsCeil(tick - now));
    }
}

constexpr Result kTimeout = 0x09401BFE;
constexpr Result kNotConnected = 0xC8A10C0D;
constexpr u32 kPeriodMs = 20;
constexpr u64 kPeriodTicks = kTicksPerSecond * kPeriodMs / 1000;  // msToCycles(20)
static_assert(kPeriodTicks == 5362237);

struct Guest {
    Runtime& rt;
    explicit Guest(Runtime& runtime) : rt(runtime) { FindServiceManager(rt)->SetReplyDelayNs(0); }

    u32 Cmd() const { return CommandBufferAddress(*rt.MainThread()); }
    u32 Scratch(u32 offset = 0) const { return rt.MainThread()->stack_top - 0x3000 + offset; }
    std::vector<u32> Request(Handle session, const std::vector<u32>& words, std::size_t reply_words) {
        REQUIRE(rt.Memory().WriteBlock(Cmd(), words.data(), static_cast<u32>(words.size() * 4)));
        CallSvc(rt, 0x32, {session});
        std::vector<u32> reply(reply_words);
        REQUIRE(rt.Memory().ReadBlock(reply.data(), Cmd(), static_cast<u32>(reply_words * 4)));
        return reply;
    }
    Result Wait(Handle handle, s64 ns) {
        return CallSvc(rt, 0x24, {handle, static_cast<u32>(ns), static_cast<u32>(static_cast<u64>(ns) >> 32)}).r[0];
    }
    Result Close(Handle handle) { return CallSvc(rt, 0x23, {handle}).r[0]; }
    Handle Open(const std::string& name) {
        const std::vector<u8> text = {'s', 'r', 'v', ':', 0};
        REQUIRE(rt.Memory().WriteBlock(Scratch(0x2000), text.data(), static_cast<u32>(text.size())));
        const Regs port = CallSvc(rt, 0x2D, {0, Scratch(0x2000)});
        REQUIRE(port.r[0] == kResultSuccess);
        std::array<u32, 2> words{};
        std::memcpy(words.data(), name.data(), std::min<std::size_t>(name.size(), 8));
        const std::vector<u32> reply =
            Request(port.r[1], {ipc::MakeHeader(0x0005, 4, 0), words[0], words[1], static_cast<u32>(name.size()), 0}, 4);
        return reply[1] == kResultSuccess ? reply[3] : 0;
    }
};

// One ir:USER session over a 0x1000-byte block of the game's: 8 packets, 0x400 bytes of receive buffer.
struct Ir {
    Guest& g;
    Handle session = 0;
    std::shared_ptr<SharedMemory> block;
    Handle block_handle = 0;
    Handle receive_event = 0, connection_event = 0;

    explicit Ir(Guest& guest) : g(guest) {
        session = g.Open("ir:USER");
        REQUIRE(session != 0);
        REQUIRE(g.rt.GetKernel().NewSharedMemory(&block, nullptr, 0x1000, MemoryPermission::ReadWrite,
                                                 MemoryPermission::ReadWrite, 0, FcramRegion::Base,
                                                 "test") == kResultSuccess);
        REQUIRE(g.rt.GetKernel().AddHandle(&block_handle, block) == kResultSuccess);
    }
    Result Initialize() {
        return g.Request(session, {ipc::MakeHeader(0x0018, 6, 2), 0x1000, 0x400, 8, 0x400, 8, 3,
                                   ipc::CopyHandleDesc(1), block_handle}, 2)[1];
    }
    Handle Event(u16 command) {
        const std::vector<u32> reply = g.Request(session, {ipc::MakeHeader(command, 0, 0)}, 4);
        REQUIRE(reply[1] == kResultSuccess);
        return reply[3];
    }
    Result Require(u8 device) { return g.Request(session, {ipc::MakeHeader(0x0006, 1, 0), device}, 2)[1]; }
    Result Send(const std::vector<u8>& bytes) {
        const u32 data = g.Scratch(0x100);
        REQUIRE(g.rt.Memory().WriteBlock(data, bytes.data(), static_cast<u32>(bytes.size())));
        return g.Request(session, {ipc::MakeHeader(0x000D, 1, 2), static_cast<u32>(bytes.size()),
                                   ipc::StaticBufferDesc(bytes.size(), 2), data}, 2)[1];
    }
    Result Release(u32 count) { return g.Request(session, {ipc::MakeHeader(0x0019, 1, 0), count}, 2)[1]; }
    Result Disconnect() { return g.Request(session, {ipc::MakeHeader(0x0009, 0, 0)}, 2)[1]; }
    Result Finalize() { return g.Request(session, {ipc::MakeHeader(0x0002, 0, 0)}, 2)[1]; }

    std::vector<u8> Header() const { return {block->GetPointer(), block->GetPointer() + 16}; }
    // The receive buffer's BufferInfo: begin, end, count.
    std::array<u32, 3> Info() const {
        std::array<u32, 3> info{};
        std::memcpy(info.data(), block->GetPointer(0x10), 12);
        return info;
    }
    // The packet at the head of the queue (begin index), as the game reads it: {offset, size} at
    // 0x20 + 8 * index, the data after the 8 packet infos.
    std::vector<u8> Oldest() const {
        const u32 index = Info()[0];
        u32 offset = 0, size = 0;
        std::memcpy(&offset, block->GetPointer(0x20 + 8 * index), 4);
        std::memcpy(&size, block->GetPointer(0x20 + 8 * index + 4), 4);
        const u32 data = 0x20 + 8 * 8;
        const u32 data_size = 0x400 - 8 * 8;
        std::vector<u8> out;
        for (u32 i = 0; i < size; ++i) {
            out.push_back(block->GetPointer(data)[(offset + i) % data_size]);
        }
        return out;
    }
};

// An input source that records the ticks it is asked about and answers `answer(tick)`.
class Recorder final : public InputSource {
public:
    explicit Recorder(std::function<InputState(u64)> answer = {}) : answer_(std::move(answer)) {}
    InputState Sample(u64 tick) override {
        ticks.push_back(tick);
        return answer_ ? answer_(tick) : InputState{};
    }
    std::vector<u64> ticks;

private:
    std::function<InputState(u64)> answer_;
};

bool Sampled(const std::vector<u64>& ticks, u64 tick) {
    return std::find(ticks.begin(), ticks.end(), tick) != ticks.end();
}

std::vector<u8> Status(const InputState& input) {
    const auto status = ir_device::BuildStatus(input);
    return {status.begin(), status.end()};
}

}  // namespace

TEST_CASE("RequireConnection(1) connects the Circle Pad Pro: the header, the connection event, the "
          "calibration answer and the status packets in the receive buffer") {
    Fixture f;
    TempDir dir;
    std::vector<u8> header_after_require;
    Result init = 1, require = 1, wait_connection = 1, wait_connection_again = 0, send_calibration = 1;
    Result wait_receive = 1, wait_receive_early = 0, send_polling = 1;
    std::array<u32, 3> info_after_calibration{}, info_after_status{};
    std::vector<u8> calibration_packet, first_status, second_status;
    u64 polling_tick = 0;
    std::vector<u64> sampled;
    const int status = BootWith(f, dir, {}, [&] {
        Guest g(*f.runtime);
        auto source = std::make_unique<Recorder>([](u64) {
            InputState s;
            s.cstick_x = 156;
            s.cstick_y = -78;
            s.zl = true;
            s.buttons = kPadA;  // not the device's: it reports only the C-stick, ZL, ZR and R
            return s;
        });
        Recorder* recorder = source.get();
        f.runtime->SetInputSource(std::move(source));
        Ir ir(g);
        std::memset(ir.block->GetPointer(), 0, 0x1000);
        ir.block->GetPointer()[ir::kNetworkIdOffset] = 0;
        init = ir.Initialize();
        ir.receive_event = ir.Event(0x000A);
        ir.connection_event = ir.Event(0x000C);
        require = ir.Require(1);
        header_after_require = ir.Header();
        wait_connection = g.Wait(ir.connection_event, 0);
        wait_connection_again = g.Wait(ir.connection_event, 0);  // one-shot: signalled once
        // ReadCalibrationData(offset 0, size 0x40): answered at once
        send_calibration = ir.Send({0x02, 0x64, 0x00, 0x00, 0x40, 0x00});
        wait_receive = g.Wait(ir.receive_event, 0);
        info_after_calibration = ir.Info();
        calibration_packet = ir.Oldest();
        REQUIRE(ir.Release(1) == kResultSuccess);
        // ConfigureHIDPolling(20 ms): the first status one period later, then one a period
        polling_tick = f.runtime->Clock().Now();
        send_polling = ir.Send({0x01, kPeriodMs, 0x87});
        SleepToTick(*f.runtime, polling_tick + kPeriodTicks - 1000);
        wait_receive_early = g.Wait(ir.receive_event, 0);
        SleepToTick(*f.runtime, polling_tick + kPeriodTicks + 1000);
        first_status = ir.Oldest();
        REQUIRE(ir.Release(1) == kResultSuccess);
        SleepToTick(*f.runtime, polling_tick + 3 * kPeriodTicks + 1000);
        info_after_status = ir.Info();
        second_status = ir.Oldest();
        sampled = recorder->ticks;
        ExitProcess(*f.runtime);
    });
    CHECK(status == 0);
    CHECK(init == kResultSuccess);
    CHECK(require == kResultSuccess);
    // connection_status 2, connection_role 2, connected 1 ("observed on a New 3DS"), initialized 1
    REQUIRE(header_after_require.size() == 16);
    CHECK(header_after_require[8] == 2);
    CHECK(header_after_require[9] == 0);
    CHECK(header_after_require[10] == 2);
    CHECK(header_after_require[12] == 1);
    CHECK(header_after_require[14] == 1);
    CHECK(wait_connection == kResultSuccess);
    CHECK(wait_connection_again == kTimeout);
    CHECK(send_calibration == kResultSuccess);
    CHECK(wait_receive == kResultSuccess);
    CHECK(info_after_calibration[2] == 1);
    // the calibration answer, wrapped: A5, network 0, extended size 0x45, the answer, its CRC-8
    std::vector<u8> answer = {0x11, 0x00, 0x00, 0x40, 0x00};
    answer.insert(answer.end(), ir_device::kCalibrationData.begin(), ir_device::kCalibrationData.end());
    CHECK(calibration_packet == ir::MakePacket(0, answer));
    CHECK(calibration_packet.back() == 0x67);
    CHECK(send_polling == kResultSuccess);
    CHECK(wait_receive_early == kTimeout);  // nothing before the period is over
    const std::vector<u8> pushed = {0x10, 0xFF, 0x0F, 0x40, 0xDF, 0x00};
    InputState input;
    input.cstick_x = 156, input.cstick_y = -78, input.zl = true;
    CHECK(Status(input) == pushed);
    CHECK(first_status == ir::MakePacket(0, pushed));
    CHECK(second_status == ir::MakePacket(0, pushed));
    // the packets at 2 and 3 periods are both queued (the game has not released them)
    CHECK(info_after_status[2] == 2);
    // the input was sampled at each packet's due tick, a whole period apart (the source also sees HID's
    // own pad updates)
    for (u64 k = 1; k <= 3; ++k) {
        CHECK(Sampled(sampled, polling_tick + k * kPeriodTicks));
    }
}

TEST_CASE("a new polling configuration restarts the period; Disconnect and Finalize stop the packets") {
    Fixture f;
    TempDir dir;
    std::vector<u64> sampled_before_disconnect;
    u32 queued_before = 0, queued_after_disconnect = 0, queued_after_finalize = 0;
    Result wait_disconnect = 1, send_after = 0, wait_after = 0;
    u64 first_config = 0, second_config = 0;
    std::array<u32, 3> info_after_finalize_cycle{};
    const int status = BootWith(f, dir, {}, [&] {
        Guest g(*f.runtime);
        auto source = std::make_unique<Recorder>();
        Recorder* recorder = source.get();
        f.runtime->SetInputSource(std::move(source));
        Ir ir(g);
        REQUIRE(ir.Initialize() == kResultSuccess);
        ir.receive_event = ir.Event(0x000A);
        ir.connection_event = ir.Event(0x000C);
        REQUIRE(ir.Require(1) == kResultSuccess);
        REQUIRE(g.Wait(ir.connection_event, 0) == kResultSuccess);
        first_config = f.runtime->Clock().Now();
        REQUIRE(ir.Send({0x01, kPeriodMs, 0x87}) == kResultSuccess);
        // half a period later the game configures again (MH4U does so after each packet): the pending
        // packet is dropped and the next one comes a whole period after this request
        SleepToTick(*f.runtime, first_config + kPeriodTicks / 2);
        second_config = f.runtime->Clock().Now();
        REQUIRE(ir.Send({0x01, kPeriodMs, 0x87}) == kResultSuccess);
        SleepToTick(*f.runtime, second_config + 2 * kPeriodTicks + 1000);
        sampled_before_disconnect = recorder->ticks;
        REQUIRE(ir.Disconnect() == kResultSuccess);
        wait_disconnect = g.Wait(ir.connection_event, 0);
        queued_before = ir.Info()[2];
        SleepNs(*f.runtime, 200'000'000);
        queued_after_disconnect = ir.Info()[2];
        send_after = ir.Send({0x01, kPeriodMs, 0x87});
        // connect again, poll, then Finalize: no packet after it, and the buffer is gone with it
        REQUIRE(ir.Require(1) == kResultSuccess);
        REQUIRE(ir.Send({0x01, kPeriodMs, 0x87}) == kResultSuccess);
        REQUIRE(ir.Finalize() == kResultSuccess);
        info_after_finalize_cycle = ir.Info();
        g.Wait(ir.receive_event, 0);  // (a packet of the connection before Finalize may have signalled it)
        SleepNs(*f.runtime, 200'000'000);
        wait_after = g.Wait(ir.receive_event, 0);
        queued_after_finalize = ir.Info()[2];
        ExitProcess(*f.runtime);
    });
    CHECK(status == 0);
    CHECK_FALSE(Sampled(sampled_before_disconnect, first_config + kPeriodTicks));  // dropped by the second
    CHECK(Sampled(sampled_before_disconnect, second_config + kPeriodTicks));
    CHECK(Sampled(sampled_before_disconnect, second_config + 2 * kPeriodTicks));
    CHECK(wait_disconnect == kResultSuccess);  // the connection event on Disconnect
    CHECK(queued_before == 2);
    CHECK(queued_after_disconnect == queued_before);
    CHECK(queued_after_finalize == info_after_finalize_cycle[2]);
    CHECK(send_after == kNotConnected);
    CHECK(wait_after == kTimeout);
}

TEST_CASE("Finalize lets go of the game's block, so the game can make a new block at its address") {
    Fixture f;
    TempDir dir;
    bool expired_after_finalize = false, alive_before = false;
    Result close = 1;
    const int status = BootWith(f, dir, {}, [&] {
        Guest g(*f.runtime);
        std::weak_ptr<SharedMemory> weak;
        {
            Ir ir(g);
            weak = ir.block;
            REQUIRE(ir.Initialize() == kResultSuccess);
            REQUIRE(ir.Require(1) == kResultSuccess);
            REQUIRE(ir.Send({0x01, 8, 0x87}) == kResultSuccess);
            SleepNs(*f.runtime, 50'000'000);
            REQUIRE(ir.Disconnect() == kResultSuccess);
            REQUIRE(ir.Finalize() == kResultSuccess);
            close = g.Close(ir.block_handle);
            ir.block.reset();
            alive_before = true;
        }
        // nothing in the service (the capture's reference included) keeps it alive
        expired_after_finalize = weak.expired();
        ExitProcess(*f.runtime);
    });
    CHECK(status == 0);
    CHECK(alive_before);
    CHECK(close == kResultSuccess);
    CHECK(expired_after_finalize);
}

TEST_CASE("--circle-pad-pro off: device 1 does not connect, as before N3-12") {
    Fixture f;
    TempDir dir;
    std::vector<u8> header;
    Result send = 0;
    u32 queued = 1;
    const int status = BootWith(f, dir, {"--circle-pad-pro", "off"}, [&] {
        Guest g(*f.runtime);
        Ir ir(g);
        REQUIRE(ir.Initialize() == kResultSuccess);
        REQUIRE(ir.Require(1) == kResultSuccess);
        header = ir.Header();
        send = ir.Send({0x01, 8, 0x87});
        SleepNs(*f.runtime, 100'000'000);
        queued = ir.Info()[2];
        ExitProcess(*f.runtime);
    });
    CHECK(status == 0);
    REQUIRE(header.size() == 16);
    CHECK(header[8] == 1);   // connection_status: trying
    CHECK(header[9] == 2);   // trying_to_connect_status
    CHECK(header[12] == 0);  // not connected
    CHECK(send == kNotConnected);
    CHECK(queued == 0);
}

TEST_CASE("a --circle-pad-pro that is neither on nor off ends the run with status 2 before it starts") {
    Fixture f;
    TempDir dir;
    bool ran = false;
    const int status = BootWith(f, dir, {"--circle-pad-pro", "yes"}, [&] {
        ran = true;
        ExitProcess(*f.runtime);
    });
    CHECK(status == 2);
    CHECK_FALSE(ran);
}

TEST_CASE("a full receive buffer drops the packets and says so once until one fits again") {
    Fixture f;
    TempDir dir;
    std::array<u32, 3> info_full{};
    std::size_t logged_full = 0, logged_after = 0;
    const int status = BootWith(f, dir, {}, [&] {
        Guest g(*f.runtime);
        Ir ir(g);
        REQUIRE(ir.Initialize() == kResultSuccess);
        REQUIRE(ir.Require(1) == kResultSuccess);
        REQUIRE(ir.Send({0x01, 8, 0x87}) == kResultSuccess);
        SleepNs(*f.runtime, 200'000'000);  // 25 packets for a queue of 8
        info_full = ir.Info();
        logged_full = f.Count("receive buffer is full!");
        REQUIRE(ir.Release(8) == kResultSuccess);
        SleepNs(*f.runtime, 200'000'000);
        logged_after = f.Count("receive buffer is full!");
        ExitProcess(*f.runtime);
    });
    CHECK(status == 0);
    CHECK(info_full[2] == 8);
    CHECK(logged_full == 1);
    CHECK(logged_after == 2);
}

#endif  // recomp.h
