// The mod list (native/src/mods): which folders of load/mods are this game's mods, their order and state from
// load_order.txt, what each holds, the code mods applied in order and the RomFS layers. Invented files.
#include <doctest/doctest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "mods/mod_list.h"

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

using namespace mh4n;
namespace stdfs = std::filesystem;

namespace {

constexpr u64 kTitle = 0x0004000000126300ull;

struct TempDir {
    stdfs::path path;
    TempDir() {
        static std::atomic<int> counter{0};
#if defined(_WIN32)
        const int pid = _getpid();
#else
        const int pid = static_cast<int>(getpid());
#endif
        path = stdfs::temp_directory_path() / ("mh4n_mods_test_" + std::to_string(pid) + "_" + std::to_string(counter++));
        stdfs::remove_all(path);
        stdfs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        stdfs::remove_all(path, ec);
    }
    void Write(const std::string& rel, const std::vector<u8>& data) const {
        const stdfs::path p = path / rel;
        stdfs::create_directories(p.parent_path());
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    void Text(const std::string& rel, const std::string& text) const { Write(rel, std::vector<u8>(text.begin(), text.end())); }
    void Dir(const std::string& rel) const { stdfs::create_directories(path / rel); }
    std::string Folder() const { return mods::Folder(path.string()); }
};

// An IPS patch that writes `data` at `offset`.
std::vector<u8> Ips(u32 offset, const std::vector<u8>& data) {
    std::vector<u8> out = {'P', 'A', 'T', 'C', 'H'};
    out.insert(out.end(), {u8(offset >> 16), u8(offset >> 8), u8(offset), u8(data.size() >> 8), u8(data.size())});
    out.insert(out.end(), data.begin(), data.end());
    out.insert(out.end(), {'E', 'O', 'F'});
    return out;
}

std::vector<std::string> Names(const std::vector<mods::Mod>& list) {
    std::vector<std::string> out;
    for (const auto& m : list) out.push_back(m.name + (m.on ? "" : " (off)"));
    return out;
}

}  // namespace

TEST_CASE("every folder of load/mods is a mod, by name; another title's, hidden ones and files are not") {
    TempDir user;
    user.Dir("load/mods/Zeta/romfs");
    user.Dir("load/mods/alpha/exefs");
    user.Write("load/mods/alpha/exefs/code.ips", Ips(0, {1}));
    user.Dir("load/mods/0004000000126300/romfs_ext");
    user.Dir("load/mods/0004000000155400/romfs");  // (another game's)
    user.Dir("load/mods/_parked/romfs");
    user.Dir("load/mods/.new/romfs");
    user.Text("load/mods/notes.txt", "x");
    const auto list = mods::List(user.Folder(), kTitle);
    CHECK(Names(list) == std::vector<std::string>{"0004000000126300", "Zeta", "alpha"});
    CHECK(list[0].romfs);
    CHECK_FALSE(list[0].code);
    CHECK(list[1].romfs);
    CHECK_FALSE(list[2].romfs);
    CHECK(list[2].code);
    // the update's program id finds the base game's folder (GetModId)
    CHECK(mods::List(user.Folder(), 0x0004000E00126300ull).size() == 3);
    CHECK(mods::List((user.path / "nowhere").string() + "/", kTitle).empty());
}

TEST_CASE("load_order.txt: its order, '# off:' for the ones off, unlisted folders after it, names without a folder dropped") {
    TempDir user;
    for (const char* name : {"A", "B", "C", "D"}) user.Dir(std::string("load/mods/") + name + "/romfs");
    user.Text("load/mods/load_order.txt", "# a comment\nC\n# off: A\n\nGone\nC\n");
    user.Text("load/mods/.from_game_folder", "D\t123\n");
    const auto list = mods::List(user.Folder(), kTitle);
    CHECK(Names(list) == std::vector<std::string>{"C", "A (off)", "B", "D"});
    CHECK(list[3].from_game_folder);
    CHECK_FALSE(list[0].from_game_folder);
    CHECK(Names(mods::Enabled(user.Folder(), kTitle)) == std::vector<std::string>{"C", "B", "D"});

    auto changed = list;
    std::swap(changed[0], changed[3]);
    changed[1].on = true;
    changed[2].on = false;
    CHECK(mods::SaveOrder(user.Folder(), changed));
    CHECK(Names(mods::List(user.Folder(), kTitle)) == std::vector<std::string>{"D", "A", "B (off)", "C"});
}

TEST_CASE("the code mods that are on apply in load order; RomFS layers follow the same order") {
    TempDir user;
    user.Write("load/mods/first/exefs/code.ips", Ips(2, {0xAA, 0xBB}));
    user.Write("load/mods/second/code.ips", Ips(3, {0xCC}));
    user.Write("load/mods/third/exefs/code.ips", Ips(0, {0xEE}));
    user.Dir("load/mods/second/romfs");
    user.Dir("load/mods/fourth/romfs_ext");
    user.Text("load/mods/load_order.txt", "first\nsecond\n# off: third\nfourth\n");
    const auto list = mods::List(user.Folder(), kTitle);
    std::vector<u8> code = {0, 1, 2, 3, 4, 5};
    const code_patch::Outcome outcome = mods::ApplyCode(list, code, 1);
    REQUIRE(outcome.ok);
    CHECK(outcome.applied == std::vector<std::string>{"first/exefs/code.ips", "second/code.ips"});
    REQUIRE(code.size() == 6 + 0x1000);  // (the bss page, once)
    CHECK(code[0] == 0);
    CHECK(code[2] == 0xAA);
    CHECK(code[3] == 0xCC);  // (the second patch over the first's byte)
    const auto layers = mods::RomFsLayers(list);
    REQUIRE(layers.size() == 2);
    CHECK(layers[0].romfs == list[1].path + "romfs");
    CHECK(layers[1].romfs_ext == list[3].path + "romfs_ext");

    // a patch that does not fit names its mod
    user.Write("load/mods/third/exefs/code.ips", Ips(0x7FFF00, {1}));  // (past the code's end)
    auto all = mods::List(user.Folder(), kTitle);
    for (auto& m : all) m.on = true;
    std::vector<u8> again = {0, 1, 2, 3, 4, 5};
    const code_patch::Outcome bad = mods::ApplyCode(all, again, 0);
    CHECK_FALSE(bad.ok);
    CHECK(bad.error == "mods: third/exefs/code.ips: patch error");
}
