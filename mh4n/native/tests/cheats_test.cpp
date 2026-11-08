// Derived from Azahar src/core/cheats/gateway_cheat.cpp at 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6
// (GPL-2.0-or-later: it says "GPLv2 or any later version"): the meaning of each code type, from its comments
// in Execute, and the cheats file's format, which the cases are written against.
// Copyright 2018-2026 Citra Emulator Project / Azahar Emulator Project
//
// The cheat engine (native/src/cheats): Gateway codes run against a memory of their own, the cheats file
// as Azahar reads and writes it, and the engine's list as the app changes it. The codes are written here
// from the code types' comments in gateway_cheat.cpp, on invented addresses.
#include <doctest/doctest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "cheats/cheat_engine.h"
#include "cheats/gateway_cheat.h"

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

using namespace mh4n;
using namespace mh4n::cheats;
namespace stdfs = std::filesystem;

namespace {

// Bytes by address; nothing is "unmapped" but the addresses at or above 0xF0000000 (they read 0 and drop writes).
class FakeTarget final : public CheatTarget {
public:
    std::map<u32, u8> bytes;
    u32 pad = 0;
    int writes = 0;
    u8 Read8(u32 a) override { return a >= 0xF0000000u ? 0 : bytes[a]; }
    u16 Read16(u32 a) override { return static_cast<u16>(Read8(a) | Read8(a + 1) << 8); }
    u32 Read32(u32 a) override { return Read16(a) | static_cast<u32>(Read16(a + 2)) << 16; }
    void Write8(u32 a, u8 v) override {
        ++writes;
        if (a < 0xF0000000u) bytes[a] = v;
    }
    void Write16(u32 a, u16 v) override {
        Write8(a, static_cast<u8>(v));
        Write8(a + 1, static_cast<u8>(v >> 8));
        writes -= 1;
    }
    void Write32(u32 a, u32 v) override {
        Write16(a, static_cast<u16>(v));
        Write16(a + 2, static_cast<u16>(v >> 16));
        writes -= 1;
    }
    u32 PadState() override { return pad; }
};

std::shared_ptr<GatewayCheat> One(const std::string& text) {
    auto cheats = GatewayCheat::Parse(text);
    REQUIRE(cheats.size() == 1);
    return cheats[0];
}

struct TempDir {
    stdfs::path path;
    TempDir() {
        static std::atomic<int> counter{0};
#if defined(_WIN32)
        const int pid = _getpid();
#else
        const int pid = static_cast<int>(getpid());
#endif
        path = stdfs::temp_directory_path() / ("mh4n_cheats_test_" + std::to_string(pid) + "_" + std::to_string(counter++));
        stdfs::remove_all(path);
        stdfs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        stdfs::remove_all(path, ec);
    }
    std::string Write(const std::string& rel, const std::string& text) const {
        const stdfs::path p = path / rel;
        stdfs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
        return p.string();
    }
    std::string Read(const std::string& rel) const {
        std::ifstream in(path / rel, std::ios::binary);
        std::stringstream s;
        s << in.rdbuf();
        return s.str();
    }
};

}  // namespace

TEST_CASE("a cheats file: names, comments, the enabled marker, lines that are no codes") {
    const auto cheats = GatewayCheat::Parse(
        "[Max money]\n"
        "*citra_enabled\n"
        "*Sets the money\n"
        "  00100000 0001869F  \n"
        "\n"
        "[Two lines]\r\n"
        "*first\r\n"
        "*second\r\n"
        "10100004 00001234\r\n"
        "not a code\r\n"
        "[No lines]\n"
        "*nothing here\n");
    REQUIRE(cheats.size() == 2);  // (a cheat without lines is dropped, as in Azahar)
    CHECK(cheats[0]->GetName() == "Max money");
    CHECK(cheats[0]->IsEnabled());
    CHECK(cheats[0]->GetComments() == "Sets the money\n");
    CHECK(cheats[0]->GetCode() == "00100000 0001869F\n");
    CHECK(cheats[0]->InvalidLines() == 0);
    CHECK(cheats[1]->GetName() == "Two lines");
    CHECK_FALSE(cheats[1]->IsEnabled());
    CHECK(cheats[1]->GetComments() == "first\nsecond\n");
    CHECK(cheats[1]->InvalidLines() == 1);
}

TEST_CASE("ToString writes the file's text back, with the marker only when on") {
    auto cheat = One("[A]\n*a note\n00100000 00000001\n");
    CHECK(cheat->ToString() == "[A]\n*a note\n00100000 00000001\n\n");
    cheat->SetEnabled(true);
    CHECK(cheat->ToString() == "[A]\n*citra_enabled\n*a note\n00100000 00000001\n\n");
    const auto again = GatewayCheat::Parse(cheat->ToString());
    REQUIRE(again.size() == 1);
    CHECK(again[0]->IsEnabled());
    CHECK(again[0]->GetCode() == cheat->GetCode());
}

TEST_CASE("writes of 32, 16 and 8 bits, and nothing written when the value is already there") {
    FakeTarget t;
    One("[w]\n00100000 11223344\n10100008 0000BEEF\n2010000C 000000AA\n")->Execute(t);
    CHECK(t.Read32(0x00100000) == 0x11223344);
    CHECK(t.Read16(0x00100008) == 0xBEEF);
    CHECK(t.Read8(0x0010000C) == 0xAA);
    const int before = t.writes;
    One("[w]\n00100000 11223344\n")->Execute(t);
    CHECK(t.writes == before);
}

TEST_CASE("conditions run the block up to D0 only when they hold") {
    FakeTarget t;
    t.Write32(0x00200000, 5);
    // 5XXXXXXX: if YYYYYYYY == word -> taken
    One("[c]\n50200000 00000005\n00200010 00000001\nD0000000 00000000\n00200014 00000002\n")->Execute(t);
    CHECK(t.Read32(0x00200010) == 1);
    CHECK(t.Read32(0x00200014) == 2);
    // 6XXXXXXX: if YYYYYYYY != word -> not taken; the line after D0 still runs
    One("[c]\n60200000 00000005\n00200018 00000003\nD0000000 00000000\n0020001C 00000004\n")->Execute(t);
    CHECK(t.Read32(0x00200018) == 0);
    CHECK(t.Read32(0x0020001C) == 4);
    // 3XXXXXXX: if YYYYYYYY > word (unsigned); nested ifs skipped as a whole
    One("[c]\n30200000 00000004\n50200000 00000005\n00200020 00000009\nD0000000 00000000\nD0000000 00000000\n"
        "00200024 00000007\n")
        ->Execute(t);
    CHECK(t.Read32(0x00200020) == 0);
    CHECK(t.Read32(0x00200024) == 7);
    // 9XXXXXXX ZZZZYYYY: YYYY == (~ZZZZ & half)
    t.Write16(0x00200030, 0x12FF);
    One("[c]\n9020003000FF1200\n")->Execute(t);  // (not 17 characters: not a code)
    One("[c]\n90200030 00FF1200\n00200034 00000001\nD2000000 00000000\n")->Execute(t);
    CHECK(t.Read32(0x00200034) == 1);
}

TEST_CASE("offsets: B loads one from memory, D3 sets it, DC adds, D2 clears it") {
    FakeTarget t;
    t.Write32(0x00300000, 0x08000000);  // a pointer
    One("[p]\nB0300000 00000000\n00000010 0000002A\nD2000000 00000000\n00000004 00000001\n")->Execute(t);
    CHECK(t.Read32(0x08000010) == 0x2A);
    CHECK(t.Read32(0x00000004) == 1);  // (after D2 the offset is 0 again)
    One("[o]\nD3000000 08000100\nDC000000 00000010\n00000000 00000007\n")->Execute(t);
    CHECK(t.Read32(0x08000110) == 7);
}

TEST_CASE("the register: D5 sets, D4 adds, D6 to D8 write it and step the offset, D9 to DB load it") {
    FakeTarget t;
    One("[r]\nD5000000 00000100\nD4000000 00000023\nD3000000 00400000\nD6000000 00000000\nD7000000 00000000\n"
        "D8000000 00000000\n")
        ->Execute(t);
    CHECK(t.Read32(0x00400000) == 0x123);
    CHECK(t.Read16(0x00400004) == 0x123);
    CHECK(t.Read8(0x00400006) == 0x23);
    t.Write32(0x00500000, 0xCAFEBABE);
    One("[l]\nDA000000 00500000\nD6000000 00500010\n")->Execute(t);
    CHECK(t.Read32(0x00500010) == 0xBABE);
}

TEST_CASE("the Joker code runs its block only while the buttons are held") {
    FakeTarget t;
    const auto cheat = One("[j]\nDD000000 00000300\n00600000 00000001\nD0000000 00000000\n");
    t.pad = 0x200;  // L alone
    cheat->Execute(t);
    CHECK(t.Read32(0x00600000) == 0);
    t.pad = 0x301;  // L, R and A
    cheat->Execute(t);
    CHECK(t.Read32(0x00600000) == 1);
}

TEST_CASE("a loop repeats its block, a patch copies the bytes of its lines") {
    FakeTarget t;
    One("[loop]\nD3000000 00700000\nD5000000 00000009\nC0000000 00000003\nD6000000 00000000\nD1000000 00000000\n")
        ->Execute(t);
    for (u32 i = 0; i < 4; ++i) CHECK(t.Read32(0x00700000 + 4 * i) == 9);
    One("[patch]\nE0800000 0000000A\n11223344 55667788\n0000AABB 00000000\n")->Execute(t);
    CHECK(t.Read32(0x00800000) == 0x11223344);
    CHECK(t.Read32(0x00800004) == 0x55667788);
    CHECK(t.Read16(0x00800008) == 0xAABB);
    CHECK(t.Read8(0x0080000A) == 0);
    // a patch longer than its lines stops at the last one
    One("[short]\nE0900000 00000100\n01020304 05060708\n")->Execute(t);
    CHECK(t.Read32(0x00900004) == 0x05060708);
}

TEST_CASE("TouchesCode: writes into a range, followed through set offsets, not through loaded ones") {
    CHECK(One("[a]\n00100000 00000000\n")->TouchesCode(0x00100000, 0x00200000));
    CHECK_FALSE(One("[a]\n00300000 00000000\n")->TouchesCode(0x00100000, 0x00200000));
    CHECK(One("[a]\nD3000000 00100000\n00000010 00000000\n")->TouchesCode(0x00100000, 0x00200000));
    CHECK_FALSE(One("[a]\nB0300000 00000000\n00000010 00000000\n")->TouchesCode(0x00100000, 0x00200000));
    CHECK(One("[a]\nE01FFFFE 00000004\n00000000 00000000\n")->TouchesCode(0x00100000, 0x00200000));
}

TEST_CASE("the engine: load, turn on, run, save; import replaces by name and leaves everything it brings off") {
    TempDir dir;
    const std::string file = CheatFilePath(dir.path.string(), 0x0004000000126300ull);
    CHECK(file.size() > 0);
    CHECK(file.substr(file.size() - 27) == "cheats/0004000000126300.txt");
    dir.Write("cheats/0004000000126300.txt", "[One]\n00100000 00000001\n\n[Two]\n*citra_enabled\n00100004 00000002\n");
    CheatEngine engine(file);
    CHECK(engine.Load() == 2);
    CHECK(engine.EnabledCount() == 1);
    FakeTarget t;
    engine.Run(t);
    CHECK(t.Read32(0x00100000) == 0);
    CHECK(t.Read32(0x00100004) == 2);
    const u64 revision = engine.Revision();
    CHECK(engine.SetEnabled(std::size_t{0}, true));
    CHECK(engine.Revision() != revision);
    engine.Run(t);
    CHECK(t.Read32(0x00100000) == 1);

    const std::string other = dir.Write("download/more.txt", "[Two]\n*citra_enabled\n00100004 00000003\n[Three]\n00100008 00000004\n");
    CHECK(engine.Import(other) == 2);
    const auto list = engine.List();
    REQUIRE(list.size() == 3);
    CHECK(list[0].name == "One");
    CHECK(list[0].enabled);
    CHECK(list[1].name == "Two");
    CHECK_FALSE(list[1].enabled);
    CHECK(list[1].code == "00100004 00000003\n");
    CHECK(list[2].name == "Three");
    CHECK_FALSE(list[2].enabled);
    std::string error;
    CHECK(engine.Import(dir.Write("download/empty.txt", "nothing\n"), &error) == -1);
    CHECK(error == "no_cheats");
    CHECK(engine.Import((dir.path / "missing.txt").string(), &error) == -1);
    CHECK(error == "unreadable");

    CHECK(engine.Remove(2));
    CHECK_FALSE(engine.Remove(5));
    CHECK(engine.Save());
    CHECK(dir.Read("cheats/0004000000126300.txt") == "[One]\n*citra_enabled\n00100000 00000001\n\n[Two]\n00100004 00000003\n\n");
    CheatEngine again(file);
    CHECK(again.Load() == 2);
    CHECK(again.EnabledCount() == 1);
    CHECK(again.SetEnabled(std::string("Two"), true));
    CHECK_FALSE(again.SetEnabled(std::string("Nope"), true));
}
