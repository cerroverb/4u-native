// The cheats of a run (cheat_engine.h): the boot step that loads the title's cheats file and runs the
// cheats that are on at every new game frame, and its options.
//
//   --cheats FILE   the cheats file (Azahar's format); default <--user>/cheats/<title id>.txt, the one
//                   the app's Cheats page imports into and saves. Without --user and --cheats: none.
//   --cheat NAME    turns the cheat named NAME on for this run (repeatable), whatever the file says;
//                   the file is not changed. A name the file does not have ends the run with status 2.
//   --no-cheats     no cheat runs, not even the ones the file marks on.
#include <algorithm>
#include <memory>

#include "cheats/cheat_engine.h"
#include "code_patch/code_patch.h"
#include "mh4n/log.h"
#include "mh4n/options.h"
#include "mh4n/registry.h"
#include "mh4n/runtime.h"
#include "mh4n/service.h"
#include "services/hid/hid_state.h"

namespace mh4n::cheats {

namespace {

constexpr const char* kLog = "Cheats";

MH4N_OPTION("runner", "--cheats", "FILE",
            "the cheats file, Gateway codes in Azahar's format ([Name], *citra_enabled marks one that is on); "
            "default <--user>/cheats/<title id>.txt. The cheats that are on run once per game frame");
MH4N_OPTION("runner", "--cheat", "NAME", "turn the cheat NAME of the cheats file on for this run (repeatable); the file is not changed");
MH4N_OPTION("runner", "--no-cheats", "", "run no cheats, not even the ones the cheats file marks on");

// The game's memory and pad as a cheat sees them.
class RuntimeTarget final : public CheatTarget {
public:
    RuntimeTarget(GuestMemory& memory, std::shared_ptr<hid::Shared> hid) : memory_(memory), hid_(std::move(hid)) {}
    u8 Read8(u32 address) override { return memory_.Read<u8>(address).value_or(0); }
    u16 Read16(u32 address) override { return memory_.Read<u16>(address).value_or(0); }
    u32 Read32(u32 address) override { return memory_.Read<u32>(address).value_or(0); }
    void Write8(u32 address, u8 value) override { memory_.Write(address, value); }
    void Write16(u32 address, u16 value) override { memory_.Write(address, value); }
    void Write32(u32 address, u32 value) override { memory_.Write(address, value); }
    u32 PadState() override { return hid_ ? hid_->Logic().State() : 0; }

private:
    GuestMemory& memory_;
    std::shared_ptr<hid::Shared> hid_;
};

void StartCheats(Runtime& rt) {
    const Options& options = rt.GetOptions();
    const ProcessImage* image = rt.Image();
    std::string path = options.GetString("--cheats");
    if (path.empty() && !rt.UserFolder().empty() && image != nullptr) {
        path = CheatFilePath(rt.UserFolder(), code_patch::ModId(image->program_id));
    }
    auto engine = std::make_shared<CheatEngine>(path);
    const std::size_t loaded = engine->Load();
    if (image != nullptr) {
        for (const SegmentMapping& segment : image->segments) {
            if (segment.name == "text") {
                engine->SetCodeRange(segment.vaddr, segment.vaddr + segment.size);
            }
        }
    }
    for (const std::string& name : options.All("--cheat")) {
        if (!engine->SetEnabled(name, true)) {
            MH4N_LOG(Error, kLog, "--cheat {}: no cheat of that name in {}", name, path.empty() ? "(no file)" : path);
            rt.RequestExit(2, "--cheat names a cheat the cheats file does not have");
            return;
        }
    }
    if (options.Has("--no-cheats")) {
        for (std::size_t i = 0; i < engine->Count(); ++i) {
            engine->SetEnabled(i, false);
        }
    }
    if (!path.empty()) {
        MH4N_LOG(Info, kLog, "{} cheat(s) in {}, {} on", loaded, path, engine->EnabledCount());
    }
    for (const CheatInfo& info : engine->List()) {
        if (info.enabled) {
            MH4N_LOG(Info, kLog, "on: [{}]{}", info.name,
                     info.touches_code ? " (it writes into the game's code: no effect in the port)" : "");
        }
    }

    std::shared_ptr<hid::Shared> hid;
    if (ServiceManager* manager = FindServiceManager(rt)) {
        hid = hid::GetShared(manager->Context());
    }
    auto target = std::make_shared<RuntimeTarget>(rt.Memory(), std::move(hid));
    // (the app's settings may turn one on later: the frame callback is always there, and cheap with none on)
    rt.OnFrame([engine, target](u32, u64) { engine->Run(*target); });
    rt.Own(target);
    SetCurrent(engine);
    rt.OnFinish([] { SetCurrent(nullptr); });
}

MH4N_REGISTER(BootStep, "started.cheats", BootStep{Phase::Started, StartCheats});

}  // namespace

}  // namespace mh4n::cheats
