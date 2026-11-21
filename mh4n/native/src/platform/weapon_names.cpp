// The weapons' names for the weapon tree, from the game's RomFS (weapon_names.h).
#include "platform/weapon_names.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

#include "mh4n/log.h"
#include "mh4n/registry.h"
#include "mh4n/runtime.h"
#include "platform/monster_names.h"
#include "services/fs/game_romfs.h"
#include "weapon_tree.h"

namespace mh4n::platform::weapon_names {

namespace {

constexpr const char* kLog = "Frontend";
// each class's message file, in equipment type order (7 ... 20)
constexpr const char* kFiles[] = {"LswordName", "SwordName", "HammerName", "LanceName", "LightName", "HeavyName",
                                  "Lsword2Name", "AxeName", "Lance2Name", "BowName", "WswordName", "Hammer2Name",
                                  "RodName", "GaxeName"};
constexpr int kFirstType = 7, kClasses = 14;
// the weapons' cells of the icon sheet's large icons, in weapon_tree_panel.h's WeaponClass order (the game's table at
// 0x00fb811b, by equipment type 7 ... 20: 55 57 59 61 63 65 56 67 62 66 58 60 68 69)
constexpr int kIconCells[kClasses] = {55, 56, 57, 58, 59, 60, 61, 62, 63, 65, 66, 67, 68, 69};
constexpr int kIconSide = 96;  // a cell as the app gets it (the sheet's 24 pixels, each made 4 x 4)

std::mutex g_mutex;
std::map<int, std::vector<std::vector<std::string>>> g_names;  // (a language read; empty while being read, or none)
std::map<int, std::vector<std::string>> g_items;               // (the items' names, by id)
std::map<int, std::vector<std::uint8_t>> g_icons;              // (the classes' icons, kIconSide a side, side by side)
std::map<int, bool> g_started;

std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\n' || s.back() == '\r')) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    return s.substr(i);
}

// The weapons' icons of the game's icon sheet <language>\lyt\common\texture\cmn_icon_GSM_NOMIP (a TEX: +8 the
// levels in bits 0-5, the width in 6-18 and the height in 19-31, +12 the format in bits 8-15, 0x10 the console's IA4;
// one level, its texels the last width x height bytes): its large icons from y 208, 24 pixels a cell, ten a row, which
// the game tints by rarity. Each weapon's cell, made kIconSide a side with whole pixels, side by side, RGBA.
bool ReadIcons(const fs::GameRomFsFile& arc, const char* lang, std::vector<std::uint8_t>* out) {
    std::vector<unsigned char> t;
    if (!monster_names::ArcFile(arc, std::string(lang) + "\\lyt\\common\\texture\\cmn_icon_GSM_NOMIP", &t) || t.size() < 20 ||
        std::memcmp(t.data(), "TEX", 4) != 0)
        return false;
    auto le32 = [&](std::size_t at) { return std::uint32_t{t[at]} | t[at + 1] << 8 | t[at + 2] << 16 | std::uint32_t{t[at + 3]} << 24; };
    const std::uint32_t b = le32(8), c = le32(12);
    const std::uint32_t w = (b >> 6) & 0x1fff, h = (b >> 19) & 0x1fff;
    if ((b & 0x3f) != 1 || ((c >> 8) & 0xff) != 0x10 || w != 256 || h != 512 || t.size() < 16 + std::size_t{w} * h) return false;
    // (IA4: a byte a texel, its intensity in the high four bits and its alpha in the low four; 8 x 8 tiles in rows, the
    // texels of a tile in Morton order)
    const unsigned char* texels = t.data() + t.size() - std::size_t{w} * h;
    auto texel = [&](std::uint32_t x, std::uint32_t y, std::uint8_t* out) {
        std::uint32_t m = 0;
        for (int bit = 0; bit < 3; ++bit) m |= ((x >> bit) & 1) << (2 * bit) | ((y >> bit) & 1) << (2 * bit + 1);
        const unsigned char v = texels[((y / 8) * (w / 8) + x / 8) * 64 + m];
        out[0] = out[1] = out[2] = static_cast<std::uint8_t>((v >> 4) * 17), out[3] = static_cast<std::uint8_t>((v & 15) * 17);
    };
    constexpr int cell = 24, top = 208, k = kIconSide / cell;
    out->assign(std::size_t{kIconSide} * kIconSide * kClasses * 4, 0);
    for (int j = 0; j < kClasses; ++j) {
        const int cx = kIconCells[j] % 10 * cell, cy = top + kIconCells[j] / 10 * cell;
        for (int y = 0; y < kIconSide; ++y)
            for (int x = 0; x < kIconSide; ++x)
                texel(cx + x / k, cy + y / k, &(*out)[(std::size_t(y) * kIconSide * kClasses + j * kIconSide + x) * 4]);
    }
    return true;
}

// The items' names (the recipes' materials), one text an id: <language>\msg\itemName_<language>
bool ReadItems(const fs::GameRomFsFile& arc, const char* lang, std::vector<std::string>* out) {
    std::vector<unsigned char> lmd;
    if (!monster_names::ArcFile(arc, std::string(lang) + "\\msg\\itemName_" + lang, &lmd)) return false;
    if (!monster_names::LmdTexts(lmd, out)) return false;
    for (std::string& s : *out) s = Trim(s);
    return true;
}

}  // namespace

bool Read(int language, std::vector<std::vector<std::string>>* names, std::string* why) {
    const char* lang = monster_names::Folder(language);
    if (!lang) return *why = "no texts in that language", false;
    const std::string arc_path = std::string(lang) + "/data/core_common.arc";
    fs::GameRomFsFile arc;
    if (!fs::OpenGameRomFsFile(arc_path, &arc, why)) return false;
    const char* where = arc.update ? "the update's RomFS" : "the game's RomFS";
    names->assign(kClasses, {});
    for (int c = 0; c < kClasses; ++c) {
        const std::string file = std::string(lang) + "\\msg\\" + kFiles[c] + "_" + lang;
        std::vector<unsigned char> lmd;
        if (!monster_names::ArcFile(arc, file, &lmd)) return *why = arc_path + " of " + where + " has no " + file, false;
        if (!monster_names::LmdTexts(lmd, &(*names)[c])) return *why = file + " of " + where + " can't be read", false;
        for (std::string& s : (*names)[c]) s = Trim(s);
    }
    *why = arc_path + " of " + where;
    return true;
}

void Prepare(int language) {
    if (!monster_names::Folder(language)) return;
    {
        std::lock_guard lock(g_mutex);
        if (g_started[language]) return;
        g_started[language] = true;
    }
    std::thread([language]() {
        std::vector<std::vector<std::string>> names;
        std::vector<std::string> items;
        std::vector<std::uint8_t> icons;
        std::string why;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = Read(language, &names, &why);
        if (ok) {  // (the recipes' items and the classes' icons, from the same file)
            const char* lang = monster_names::Folder(language);
            fs::GameRomFsFile arc;
            std::string why2;
            if (fs::OpenGameRomFsFile(std::string(lang) + "/data/core_common.arc", &arc, &why2)) {
                if (!ReadItems(arc, lang, &items)) items.clear();
                if (!ReadIcons(arc, lang, &icons)) icons.clear();
            }
        }
        if (!ok && why == "no game runs") {  // (too early: again on a later call)
            std::lock_guard lock(g_mutex);
            g_started.erase(language);
            return;
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (ok) {
            std::size_t count = 0;
            for (const auto& c : names) count += c.size();
            MH4N_LOG(Info, kLog, "weapon names: {} in {}, from {} ({:.1f} ms): {} ... {}; {} items, {}", count,
                     monster_names::Folder(language), why, ms, names[0].size() > 1 ? names[0][1] : std::string(),
                     names.back().empty() ? std::string() : names.back().back(), items.size(),
                     icons.empty() ? "no icons (drawn ones)" : "the game's icons");
        } else {
            MH4N_LOG(Warning, kLog, "weapon names: none in {} ({})", monster_names::Folder(language), why);
        }
        std::lock_guard lock(g_mutex);
        g_names[language] = std::move(names);
        g_items[language] = std::move(items);
        g_icons[language] = std::move(icons);
    }).detach();
}

std::string Item(int language, int id) {
    std::lock_guard lock(g_mutex);
    const auto it = g_items.find(language);
    if (it == g_items.end() || id <= 0 || id >= static_cast<int>(it->second.size())) return {};
    return it->second[id];
}

bool Icons(int language, std::vector<std::uint8_t>* rgba, int* cell) {
    std::lock_guard lock(g_mutex);
    const auto it = g_icons.find(language);
    if (it == g_icons.end() || it->second.empty()) return false;
    *rgba = it->second;
    *cell = kIconSide;
    return true;
}

std::string Name(int language, int type, int id) {
    std::lock_guard lock(g_mutex);
    const auto it = g_names.find(language);
    const int c = type - kFirstType;
    if (it == g_names.end() || c < 0 || c >= static_cast<int>(it->second.size())) return {};
    const auto& list = it->second[c];
    if (id <= 0 || id >= static_cast<int>(list.size())) return {};
    return list[id];
}

namespace {

// Phase `started`: the weapon tree (port/src/weapon_tree.cpp) takes its names from here; it asks for a language's when
// the Smithy first opens.
void StartedStep(Runtime&) {
    Port::WeaponTree::SetNames(&Name, &Prepare, &Item, &Icons);
}

MH4N_REGISTER(BootStep, "platform.weapon_names", BootStep{Phase::Started, StartedStep});

}  // namespace

}  // namespace mh4n::platform::weapon_names
