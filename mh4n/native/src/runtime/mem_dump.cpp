// --dump fN:ADDR:SIZE (repeatable): at game frame N, write SIZE bytes of guest memory from ADDR to
// mem_f<N>_<addr>.bin in --dump-dir (else the --shots folder, else the current one). A research aid,
// as the reference runner's --dump: memory diffed between frames where the game's state differs (a
// menu open and closed, the weapon out and away) tells where the game keeps that state
// (docs/research/game-state.md). Pages that are not mapped are written as zeros.
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "../common/utf8.h"
#include "mh4n/log.h"
#include "mh4n/options.h"
#include "mh4n/runtime.h"

namespace mh4n {

namespace {

constexpr const char* kLog = "Runtime";
constexpr u32 kPage = 0x1000;

MH4N_OPTION("runner", "--dump", "fN:ADDR:SIZE",
            "at game frame N write SIZE bytes of guest memory from ADDR (hex) to mem_f<N>_<addr>.bin, "
            "pages not mapped as zeros (repeatable; a research aid)");
MH4N_OPTION("runner", "--dump-dir", "DIR", "where --dump writes (default: the --shots folder, else the current one)");

struct DumpRequest {
    u32 frame, address, size;
};

bool ParseDump(const std::string& text, DumpRequest& out) {
    if (text.size() < 2 || text[0] != 'f') {
        return false;
    }
    const auto c1 = text.find(':'), c2 = text.find(':', c1 == std::string::npos ? c1 : c1 + 1);
    if (c1 == std::string::npos || c2 == std::string::npos) {
        return false;
    }
    char* end = nullptr;
    out.frame = static_cast<u32>(std::strtoul(text.c_str() + 1, &end, 10));
    out.address = static_cast<u32>(std::strtoul(text.c_str() + c1 + 1, nullptr, 16));
    out.size = static_cast<u32>(std::strtoul(text.c_str() + c2 + 1, nullptr, 0));
    return end == text.c_str() + c1 && out.size > 0;
}

class MemDumper {
public:
    MemDumper(Runtime& rt, std::vector<DumpRequest> requests, std::filesystem::path dir)
        : rt_(rt), requests_(std::move(requests)), dir_(std::move(dir)) {}

    void OnFrame(u32 frame) {
        for (const DumpRequest& r : requests_) {
            if (r.frame == frame) {
                Write(r);
            }
        }
    }

private:
    void Write(const DumpRequest& r) {
        std::vector<u8> bytes(r.size, 0);
        const GuestMemory& memory = rt_.Memory();
        u32 mapped = 0;
        for (u32 off = 0; off < r.size;) {
            const u32 address = r.address + off;
            const u32 chunk = std::min(r.size - off, kPage - address % kPage);
            if (memory.ReadBlock(bytes.data() + off, address, chunk)) {
                mapped += chunk;
            }
            off += chunk;
        }
        const std::string name = fmt::format("mem_f{}_{:08x}.bin", r.frame, r.address);
        std::ofstream out(dir_ / utf8::PathFromUtf8(name), std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        MH4N_LOG(Info, kLog, "dump: {} ({} of {} bytes mapped)", name, mapped, r.size);
    }

    Runtime& rt_;
    std::vector<DumpRequest> requests_;
    std::filesystem::path dir_;
};

void StartDumps(Runtime& rt) {
    const Options& options = rt.GetOptions();
    std::vector<DumpRequest> requests;
    for (const std::string& text : options.All("--dump")) {
        DumpRequest r{};
        if (!ParseDump(text, r)) {
            MH4N_LOG(Error, kLog, "bad --dump {} (fN:ADDR:SIZE)", text);
            rt.RequestExit(2, "bad --dump " + text);
            return;
        }
        requests.push_back(r);
    }
    if (requests.empty()) {
        return;
    }
    std::filesystem::path dir = utf8::PathFromUtf8(options.GetString("--dump-dir", options.GetString("--shots", ".")));
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    auto dumper = std::make_shared<MemDumper>(rt, std::move(requests), dir);
    MemDumper* const raw = dumper.get();
    rt.OnFrame([raw](u32 frame, u64) { raw->OnFrame(frame); });
    rt.Own(dumper);
}

MH4N_REGISTER(BootStep, "runtime.mem-dump", BootStep{Phase::Tracing, StartDumps});

}  // namespace

}  // namespace mh4n
