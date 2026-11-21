// The weapon tree's view (1.0.2): which weapons of a class the Smithy's tree shows and where, from the game's tables
// and the player's equipment. The same file in both ports (MH4U's mh4n port/src, MHGU's runtime/); each fills
// WeaponTreeClass from its game and draws the result its own way (the apps' weapon tree panels).
//
// The tree of a weapon is its line: from the root it is forged at, every weapon that upgrades from it, and theirs.
// Shown are the weapons the player has (in the box or worn), the one the Smithy shows, the ones before them in their
// line (named: the way there), and, one step on from each of those, the ones they upgrade into, as "?" (unnamed): the
// tree reveals nothing beyond the next step. Read-only: nothing here changes the game.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace weapon_tree {

// One material of a recipe: the game's item (its id in the port's tables), its name in the game's language and how
// many the recipe takes
struct Material {
    int item = 0;
    std::string name;
    int count = 0;
};

// What making a weapon takes, as the game's tables give it (an empty one: none known)
struct Recipe {
    std::vector<Material> items;
    int zenny = 0;        // 0: not in the tables
    std::string points;   // (MHGU) the material category the game also asks for, and its value: "Ore 4"
    int level = 0;        // (MHGU) the level it makes (an upgrade to a weapon's next level)
    bool Empty() const { return items.empty() && zenny == 0 && points.empty(); }
};

struct Weapon {
    bool real = false;                  // a weapon of the game's (an empty or unused id: false)
    std::string name;                   // in the game's language (UTF-8; empty while not read)
    int rarity = 0;                     // as the game shows it (1 on)
    int attack = 0;                     // as the game shows it
    int affinity = 0;                   // %
    int element = 0, element_value = 0;  // kElement*: the element or status and its value as shown (0: none)
    int element2 = 0, element2_value = 0;  // a second one (dual blades with two elements; else 0)
    bool element_hidden = false;        // needs Awaken
    int slots = 0;
    bool forge = false;                 // forged from scratch (a create recipe)
    std::vector<int> children;          // the weapons it upgrades into
    // (MHGU: a weapon is a line of levels) the level shown (the one had, worn or in the Smithy; 0 none), its last
    // level, and the level of the weapon before it that upgrades into it; 0 where a port has no levels (MH4U)
    int level = 0, levels = 0, from_level = 0;
    int defense = 0;                    // the defense it adds
    // its sharpness as the game shows it: where each colour ends (red, orange, yellow, green, blue, white, purple; in the
    // tables' units, from 0), and how far the skill that adds sharpness takes it (0: not in the tables)
    std::vector<int> sharpness;
    int sharpness_plus = 0;
    // forged from scratch (the game's Create), upgraded from the weapon before it, and (MHGU) its next level; empty
    // where none
    Recipe create, upgrade, next;
};

enum Element { kElementNone, kFire, kWater, kThunder, kIce, kDragon, kPoison, kParalysis, kSleep, kBlast };

struct Class {
    std::vector<Weapon> weapons;  // by id
    int sharpness_full = 0;       // a sharpness bar's whole length in the tables' units (0: no bars)
};

enum NodeFlags : unsigned {
    kOwned = 1,      // a copy in the box
    kWorn = 2,       // the one worn
    kSelected = 4,   // the one the Smithy shows
    kNext = 8,       // one step on from a known one: shown as "?"
    kPast = 16,      // before a known one in its line: named, not had
};

struct Node {
    int id = 0;
    int parent = -1;  // index in View::nodes of the weapon it upgrades from (-1: the root)
    int column = 0, row = 0;
    unsigned flags = 0;
};

struct View {
    std::vector<Node> nodes;  // the root first, then in depth-first order (each parent before its children)
    int selected = -1;        // index of the Smithy's weapon
    int columns = 0, rows = 0;
};

// The tree of weapon `selected` of class `c`, with `owned` (by id: 1 a copy in the box) and `worn` (the id worn, -1
// none of the class). Empty when `selected` is not a real weapon of the class.
inline View Build(const Class& c, int selected, const std::vector<std::uint8_t>& owned, int worn) {
    View v;
    const int n = static_cast<int>(c.weapons.size());
    if (selected < 0 || selected >= n || !c.weapons[selected].real) return v;
    // each weapon's parent: the first real one (lowest id) that upgrades into it
    std::vector<int> parent(n, -1);
    for (int p = 0; p < n; ++p) {
        if (!c.weapons[p].real) continue;
        for (int k : c.weapons[p].children)
            if (k > 0 && k < n && k != p && parent[k] < 0) parent[k] = p;
    }
    int root = selected;
    for (int guard = 0; parent[root] >= 0 && guard < n; ++guard) root = parent[root];
    // known: had (box, worn) and the Smithy's; shown named: those and every weapon before them in their line
    std::vector<unsigned> flag(n, 0);
    std::vector<char> in_line(n, 0), named(n, 0);
    std::vector<int> stack{root};
    while (!stack.empty()) {  // (the line: the root and everything that upgrades from it)
        const int i = stack.back();
        stack.pop_back();
        if (in_line[i]) continue;
        in_line[i] = 1;
        for (int k : c.weapons[i].children)
            if (k > 0 && k < n && c.weapons[k].real && parent[k] == i) stack.push_back(k);
    }
    for (int i = 0; i < n; ++i) {
        if (!in_line[i]) continue;
        if (i < static_cast<int>(owned.size()) && owned[i]) flag[i] |= kOwned;
        if (i == worn) flag[i] |= kWorn;
        if (i == selected) flag[i] |= kSelected;
        if (flag[i]) {
            for (int a = i, guard = 0; a >= 0 && guard <= n; a = parent[a], ++guard) named[a] = 1;
        }
    }
    for (int i = 0; i < n; ++i)
        if (named[i] && !(flag[i] & (kOwned | kWorn | kSelected))) flag[i] |= kPast;
    // the nodes, depth first; a "?" one step on from each named one
    std::vector<int> index(n, -1);
    int next_row = 0;
    struct Item { int id, parent_node, column; };
    std::vector<Item> todo{{root, -1, 0}};
    while (!todo.empty()) {
        const Item it = todo.back();
        todo.pop_back();
        Node node;
        node.id = it.id;
        node.parent = it.parent_node;
        node.column = it.column;
        node.flags = named[it.id] ? flag[it.id] : kNext;
        // (a first child comes right after its parent, on its row; any other child starts a row of its own)
        if (it.parent_node >= 0 && it.parent_node == static_cast<int>(v.nodes.size()) - 1)
            node.row = v.nodes[it.parent_node].row;
        else
            node.row = next_row++;
        index[it.id] = static_cast<int>(v.nodes.size());
        if (it.id == selected) v.selected = index[it.id];
        v.nodes.push_back(node);
        v.columns = std::max(v.columns, it.column + 1);
        if (!named[it.id]) continue;  // (nothing past a "?")
        const std::vector<int>& kids = c.weapons[it.id].children;
        for (auto k = kids.rbegin(); k != kids.rend(); ++k) {  // (reversed: the first child comes out first)
            if (*k > 0 && *k < n && c.weapons[*k].real && parent[*k] == it.id && index[*k] < 0)
                todo.push_back({*k, index[it.id], it.column + 1});
        }
    }
    v.rows = next_row;
    return v;
}

// The tree as text, one weapon a line, indented by its column (the runner's trace and the tests): "Iron Sword [box]",
// "? ", "Buster Sword [smithy]"...
inline std::string Text(const Class& c, const View& v) {
    std::string out;
    for (const Node& node : v.nodes) {
        out.append(2 * node.column, ' ');
        const Weapon& w = c.weapons[node.id];
        if (node.flags & kNext) {
            out += "?";
        } else {
            out += w.name.empty() ? "#" + std::to_string(node.id) : w.name;
            if (node.flags & kOwned) out += " [box]";
            if (node.flags & kWorn) out += " [worn]";
            if (node.flags & kSelected) out += " [smithy]";
        }
        out += "\n";
    }
    return out;
}

}  // namespace weapon_tree
