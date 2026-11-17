// Hunting aids of the port (1.0.1), all off unless the player turns them on: the large monsters always on the
// game's maps (monsters_on_map), the damage of each hit as a number over the monster (damage_numbers) and each large
// monster's health as a bar (monster_health_bar), both drawn by the app over the top screen. None changes the code
// library: they read (and the first writes) the game's memory once a frame (hunt_aids.cpp).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct RCtx;

namespace Port::HuntAids {

/// One damage number to draw: where on the top screen's picture (x from its left edge, y from its top, fractions of
/// it), the damage, and when it was dealt (the steady clock's ns, std::chrono::steady_clock): the app raises and fades
/// it over kNumberSeconds from then.
struct Number {
    float x = 0.0f, y = 0.0f;
    bool shown = false;       // the place is in front of the camera and on the picture (else not drawn now)
    int value = 0;
    std::int64_t born_ns = 0;
    std::uint32_t id = 0;     // a number of its own (the app's tests, and a steady jitter)
};
constexpr double kNumberSeconds = 1.0;

/// The damage numbers to draw now (those dealt in the last kNumberSeconds), as the last game frame placed them.
/// Empty with damage_numbers off. Safe from any thread.
std::vector<Number> Numbers();

/// One large monster's health for the app's bar (monster_health_bar): health and its maximum as the game keeps them,
/// whether it is in the hunter's area of the map, its row of the game's monster table, and a number of its own (its
/// unit).
struct Health {
    int health = 0, max = 0;
    bool here = false;
    std::uint8_t kind = 0;
    std::uint32_t id = 0;
};

/// The large monsters of the quest that are alive, in the game's order, as the last game frame read them. Empty out of
/// quests and with monster_health_bar off. Safe from any thread.
std::vector<Health> Healths();

/// The language the game shows its texts in (with monster_health_bar on, from the title on: the app's bars name each
/// monster in it): its own number, the row of its table of language folders (0x0105722c: 0 jpn, 1 eng, 2 fre, 3 spa,
/// 4 ger, 5 ita, ...), as sGameControl keeps it (+0x11, the byte the game makes its <language>\... paths with,
/// 0x002ffeb4). -1 while unknown. Safe from any thread.
int GameLanguage();

/// The options (mh4u.ini, the app's menu, the runner's --set): monsters_on_map, damage_numbers, monster_health_bar
/// (on/off, off unless set) and hunt_trace (tests: a line a second with each large monster's place, health and paint,
/// and one per number and per bar each frame). False for another key. On the emulation thread, or before the game
/// starts.
bool SetOption(const std::string& key, const std::string& value);
bool MonstersOnMap();
bool DamageNumbersOn();
bool HealthBarOn();

/// A new boot: the options back to off and nothing remembered of the last game (hooks.cpp's Install, before the
/// options are read).
void Reset();
/// Where the module's log lines go (hooks.cpp's log).
void SetLog(void (*log)(const std::string& line));

/// hooks.cpp's sMhMain::frame, after the game's frame: both aids' step.
void Frame(RCtx* ctx);

/// hooks.cpp's getProjMatrix of a top-screen camera, after the port's own changes: the projection `matrix` (16 floats,
/// the game's row-vector layout) that `camera` draws with this frame.
void Projection(RCtx* ctx, std::uint32_t camera, std::uint32_t matrix);

}  // namespace Port::HuntAids
