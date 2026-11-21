// Tests of the net sync's bookkeeping on the native runtime (platform/netsync_native.h, 1.0.2): the packet's layout (the
// Azahar build's), and when a console waits for a peer: not without reports or game data, yes once it has run more than
// the slack ahead of a peer that stalls, and on again when the peer catches up; a silent peer is left to the game; the
// leads count again after a restart; a peer that waits for this console is not waited for; off, nothing.
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "platform/netsync_native.h"

namespace {

using mh4n::netsync::MacAddress;
using mh4n::netsync::Peers;
using mh4n::netsync::Report;
namespace netsync = mh4n::netsync;

const MacAddress kSelf = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
const MacAddress kPeer = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};

Report At(double clock_ms, const MacAddress& waiting_for = {}) {
    Report r;
    r.version = netsync::kVersion;
    r.clock_ms = clock_ms;
    r.waiting_for = waiting_for;
    return r;
}

// A console and one peer playing together: both reported and the peer sent game data to this one
void Together(Peers& p, Port::NetSyncMode mode = Port::NetSyncMode::Session) {
    p.Configure(mode, 200.0);
    p.SetSelf(kSelf);
    p.OnReport(kPeer, At(1000.0), 0.0);
    p.OnData(kPeer, kSelf);
}

}  // namespace

TEST_CASE("the packet: the Azahar build's 24 bytes, both ways") {
    const std::vector<std::uint8_t> data = netsync::Encode(1234.5, kPeer, netsync::kFlagEndSkip);
    REQUIRE(data.size() == 24);
    CHECK(data[0] == 0x53);  // "SYNC", little-endian 0x434e5953
    CHECK(data[4] == 2);
    const auto r = netsync::Decode(data);
    REQUIRE(r.has_value());
    CHECK(r->clock_ms == doctest::Approx(1234.5));
    CHECK(r->waiting_for == kPeer);
    CHECK(r->flags == netsync::kFlagEndSkip);
    std::vector<std::uint8_t> bad = data;
    bad[0] ^= 1;
    CHECK_FALSE(netsync::Decode(bad).has_value());
    bad = data;
    bad[4] = 9;  // a version from the future
    CHECK_FALSE(netsync::Decode(bad).has_value());
    CHECK_FALSE(netsync::Decode(std::vector<std::uint8_t>(10)).has_value());
}

TEST_CASE("no reports, or no game data addressed to this console: nobody is waited for") {
    Peers p;
    p.Configure(Port::NetSyncMode::Session, 200.0);
    p.SetSelf(kSelf);
    CHECK(p.Compute(0.0, 0.0, false, nullptr).lead == 0.0);
    p.OnReport(kPeer, At(0.0), 0.0);  // (reports, but no game data to us)
    p.Compute(0.0, 0.0, false, nullptr);
    CHECK(p.Compute(5000.0, 0.1, false, nullptr).lead == 0.0);
    CHECK(p.Syncing() == 0);
}

TEST_CASE("a peer that stalls is waited for once this console is past the slack, and not after it catches up") {
    Peers p;
    Together(p);
    std::vector<std::string> log;
    CHECK(p.Compute(0.0, 0.0, false, &log).lead == 0.0);  // (from here they keep together)
    CHECK(p.Syncing() == 1);
    REQUIRE(log.size() == 1);
    CHECK(log[0].find("net sync: with 02:00:00:00:00:02") == 0);
    p.OnReport(kPeer, At(1100.0), 0.1);
    CHECK(p.Compute(100.0, 0.1, false, nullptr).lead == doctest::Approx(0.0));
    // the peer stalls: its clock stands at 1100 while ours runs on
    const auto a = p.Compute(350.0, 0.35, false, nullptr);
    CHECK(a.mac == kPeer);
    CHECK(a.lead == doctest::Approx(250.0));
    CHECK(a.lead > p.Slack());
    // it goes on and catches up
    p.OnReport(kPeer, At(1300.0), 0.6);
    CHECK(p.Compute(350.0, 0.6, false, nullptr).lead == doctest::Approx(50.0));
}

TEST_CASE("a peer silent for 6 s is left to the game") {
    Peers p;
    Together(p);
    std::vector<std::string> log;
    p.Compute(0.0, 0.0, false, &log);
    CHECK(p.Compute(1000.0, 5.9, false, &log).lead == doctest::Approx(1000.0));
    CHECK(p.Compute(1000.0, 6.1, false, &log).lead == 0.0);
    CHECK(p.Syncing() == 0);
    CHECK(log.back().find("no longer with") != std::string::npos);
}

TEST_CASE("a restart counts the leads from zero") {
    Peers p;
    Together(p);
    p.Compute(0.0, 0.0, false, nullptr);
    CHECK(p.Compute(900.0, 1.0, false, nullptr).lead == doctest::Approx(900.0));
    CHECK(p.Compute(900.0, 9.0 - 3.5, true, nullptr).lead == 0.0);
    CHECK(p.Compute(950.0, 5.6, false, nullptr).lead == doctest::Approx(50.0));
}

TEST_CASE("a peer that waits for this console is not waited for past the slack") {
    Peers p;
    Together(p);
    p.Compute(0.0, 0.0, false, nullptr);
    p.OnReport(kPeer, At(1000.0, kSelf), 0.5);
    CHECK(p.Compute(800.0, 0.5, false, nullptr).lead == doctest::Approx(200.0));
    CHECK_FALSE(p.Compute(900.0, 0.6, false, nullptr).lead > p.Slack());
}

TEST_CASE("game data that isn't for this console, and broadcasts, don't make a session") {
    Peers p;
    p.Configure(Port::NetSyncMode::Session, 200.0);
    p.SetSelf(kSelf);
    p.OnReport(kPeer, At(0.0), 0.0);
    p.OnData(kPeer, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
    p.OnData(kPeer, {0x02, 0, 0, 0, 0, 0x03});
    p.Compute(0.0, 0.0, false, nullptr);
    CHECK(p.Compute(1000.0, 0.1, false, nullptr).lead == 0.0);
    // "all": every reporting port console, with or without game data
    Peers all;
    all.Configure(Port::NetSyncMode::All, 200.0);
    all.SetSelf(kSelf);
    all.OnReport(kPeer, At(0.0), 0.0);
    all.Compute(0.0, 0.0, false, nullptr);
    CHECK(all.Compute(1000.0, 0.1, false, nullptr).lead == doctest::Approx(1000.0));
}

TEST_CASE("off: nothing is kept, nothing is waited for") {
    Peers p;
    p.Configure(Port::NetSyncMode::Off, 200.0);
    p.SetSelf(kSelf);
    p.OnReport(kPeer, At(0.0), 0.0);
    p.OnData(kPeer, kSelf);
    p.Compute(0.0, 0.0, false, nullptr);
    CHECK(p.Compute(5000.0, 1.0, false, nullptr).lead == 0.0);
    CHECK(p.Syncing() == 0);
    // its own reports are not a peer's
    Peers q;
    Together(q);
    q.OnReport(kSelf, At(0.0), 0.0);
    q.Compute(0.0, 0.0, false, nullptr);
    CHECK(q.Syncing() == 1);
}
