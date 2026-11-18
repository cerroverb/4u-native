// The game's addresses in the port's native code (hooks.cpp, swkbd_bridge.cpp), USA v1.3.0's: every one is listed
// in config/port_addresses.txt. A code library made for another version of the game (EUR v1.1.0, or USA v1.0's code
// that Heavenfall's web installer builds on) carries each one's address in that version (tools/game_version.py);
// SetGameAddresses installs that table before the hooks, and each GameAddr then reads as the running version's
// address. Without a table (a USA v1.3.0 library, the Azahar build) the addresses are their own.
// A library carries the addresses config/port_addresses.txt had when it was made, and its id doesn't change with that
// list, so another version's library made by older tools lacks the ones added since (1.0's EUR v1.1.0 libraries: the
// 1.0.1 ones of gather.cpp and onescreen.cpp). Those come from the port's own copy of that version's map
// (game_shifts.h, config/shifts), the same address the tools would have put in the library; one no map has stays
// unknown (GameAddr::Known), and keeps its USA v1.3.0 value, which is not the code of that version.
//
//   GameAddr SMAIN_INSTANCE{0x01057194};   // a USA v1.3.0 address, used as a u32: rd32(ctx, SMAIN_INSTANCE)
//   switch (UsaAddress(vtable)) { case 0x00e6c170: ... }   // back to USA v1.3.0's, for constant labels
#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "game_shifts.h"

namespace Port {

// (usa, here) pairs, sorted by usa; empty for USA v1.3.0
inline std::vector<std::pair<std::uint32_t, std::uint32_t>>& GameAddressTable() {
    static std::vector<std::pair<std::uint32_t, std::uint32_t>> table;
    return table;
}

// The running version's address of a USA v1.3.0 one: the table's, else the address itself.
inline std::uint32_t GameAddress(std::uint32_t usa) {
    const auto& table = GameAddressTable();
    const auto it = std::lower_bound(table.begin(), table.end(), std::make_pair(usa, std::uint32_t{0}));
    return it != table.end() && it->first == usa ? it->second : usa;
}

// The other way: the USA v1.3.0 address of one of the running version's, or the address itself.
inline std::uint32_t UsaAddress(std::uint32_t here) {
    for (const auto& [usa, h] : GameAddressTable()) {
        if (h == here) return usa;
    }
    return here;
}

// A USA v1.3.0 address in the version `key` (config/versions.json's), by the port's copy of its map (game_shifts.h), as
// tools/version_map.py's Shifts.translate gives it: false when the port has no map for that version or the map no
// address for this one.
inline bool ShiftedAddress(std::string_view key, std::uint32_t usa, std::uint32_t* here) {
    // the interval [start, end) of a sorted list that holds `a`, or nullptr
    auto in = [](const GameShift* first, std::uint32_t count, std::uint32_t a) -> const GameShift* {
        const GameShift* last = first + count;
        const GameShift* it =
            std::upper_bound(first, last, a, [](std::uint32_t v, const GameShift& s) { return v < s.start; });
        return it != first && a < (it - 1)->end ? it - 1 : nullptr;
    };
    auto moved = [](std::uint32_t a, const GameShift* s) {
        return static_cast<std::uint32_t>(static_cast<std::int64_t>(a) + s->shift);
    };
    for (const GameShifts& map : kGameShifts) {
        if (key != map.key) continue;
        if (map.data_end > map.data_start && usa >= map.data_start && usa < map.data_end) {
            const GameShift* s = in(map.data, map.data_count, usa);
            if (!s) return false;
            *here = moved(usa, s);
            return true;
        }
        const std::uint32_t a = usa & ~1u;  // (a code address keeps its Thumb bit)
        if (map.text_end > map.text_start && (a < map.text_start || a >= map.text_end)) {
            *here = usa;
            return true;
        }
        const GameShift* s = in(map.text, map.text_count, a);
        if (!s) return false;
        *here = moved(a, s) | (usa & 1u);
        return true;
    }
    return false;
}

class GameAddr {
public:
    // (namespace-scope objects of the port's files: each registers itself, so SetGameAddresses reaches it)
    GameAddr(std::uint32_t usa) : usa_(usa), here_(usa) { Registry().push_back(this); }
    GameAddr(const GameAddr&) = delete;
    GameAddr& operator=(const GameAddr&) = delete;
    operator std::uint32_t() const { return here_; }
    std::uint32_t Usa() const { return usa_; }
    // false when the running version's address of it is not known (SetGameAddresses had none for it), so that the
    // address is USA v1.3.0's, not this version's: nothing may call or read it
    bool Known() const { return known_; }
    // here = the table's address (SetGameAddresses); false when the table has none for it
    bool Translate() {
        here_ = GameAddress(usa_);
        const auto& table = GameAddressTable();
        known_ = table.empty() || usa_ == 0 ||
                 std::binary_search(table.begin(), table.end(), std::make_pair(usa_, here_));
        return known_;
    }
    static std::vector<GameAddr*>& Registry() {
        static std::vector<GameAddr*> all;
        return all;
    }

private:
    std::uint32_t usa_, here_;
    bool known_ = true;
};

// Installs the code library's table (rc_port_addr_usa / rc_port_addr_here, `count` pairs; 0 for USA v1.3.0) and
// translates every GameAddr. With the library's version (`version`, its rc_game_version), a GameAddr its table has
// none for (a library made before the address was listed) takes its address from the port's copy of that version's
// map (ShiftedAddress), and goes into `from_map` as (usa, here). Returns the USA v1.3.0 addresses of the GameAddrs still
// unknown (0 stays 0): an address missing from config/port_addresses.txt, or one no map has; each keeps its USA v1.3.0
// value and Known() says false.
inline std::vector<std::uint32_t> SetGameAddresses(const std::uint32_t* usa, const std::uint32_t* here,
                                                   std::uint32_t count, const char* version = nullptr,
                                                   std::vector<std::pair<std::uint32_t, std::uint32_t>>* from_map = nullptr) {
    auto& table = GameAddressTable();
    table.clear();
    for (std::uint32_t i = 0; i < count; ++i) table.emplace_back(usa[i], here[i]);
    std::sort(table.begin(), table.end());
    if (!table.empty() && version && *version) {
        std::vector<std::pair<std::uint32_t, std::uint32_t>> more;
        for (const GameAddr* a : GameAddr::Registry()) {
            const std::uint32_t u = a->Usa();
            std::uint32_t h = 0;
            const auto it = std::lower_bound(table.begin(), table.end(), std::make_pair(u, std::uint32_t{0}));
            if (u != 0 && (it == table.end() || it->first != u) && ShiftedAddress(version, u, &h)) more.emplace_back(u, h);
        }
        std::sort(more.begin(), more.end());
        more.erase(std::unique(more.begin(), more.end()), more.end());
        table.insert(table.end(), more.begin(), more.end());
        std::sort(table.begin(), table.end());
        if (from_map) *from_map = std::move(more);
    }
    std::vector<std::uint32_t> missing;
    for (GameAddr* a : GameAddr::Registry()) {
        if (!a->Translate()) missing.push_back(a->Usa());
    }
    return missing;
}

}  // namespace Port
