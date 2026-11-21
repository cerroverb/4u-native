// port/src/weapon_tree_view.h: which weapons the Smithy's tree shows and where (1.0.2).
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "weapon_tree_view.h"

namespace {

// ids 0..8: 0 none; 1 -> 2, 5; 2 -> 3; 3 -> 4; 5 -> 6; 7 alone; 8 also from 3 and from 6 (two parents: hangs from 3)
weapon_tree::Class Sample() {
    weapon_tree::Class c;
    c.weapons.resize(9);
    const char* names[] = {"", "Root", "A", "A+", "A++", "B", "B+", "Lone", "Two"};
    for (int i = 1; i < 9; ++i) c.weapons[i].real = true, c.weapons[i].name = names[i];
    c.weapons[1].children = {2, 5};
    c.weapons[2].children = {3};
    c.weapons[3].children = {4, 8};
    c.weapons[5].children = {6};
    c.weapons[6].children = {8};
    c.weapons[1].forge = c.weapons[7].forge = true;
    return c;
}

int Find(const weapon_tree::View& v, int id) {
    for (int i = 0; i < static_cast<int>(v.nodes.size()); ++i)
        if (v.nodes[i].id == id) return i;
    return -1;
}

}  // namespace

TEST_CASE("the tree of a weapon the player has: the line before it named, one step on as ?") {
    const weapon_tree::Class c = Sample();
    std::vector<std::uint8_t> owned(9, 0);
    owned[2] = 1;
    const weapon_tree::View v = weapon_tree::Build(c, 2, owned, -1);
    REQUIRE(v.nodes.size() == 4);  // Root, A, A+ (?), B (?)
    CHECK(v.nodes[0].id == 1);
    CHECK(v.nodes[0].flags == weapon_tree::kPast);
    const int a = Find(v, 2);
    REQUIRE(a >= 0);
    CHECK(v.selected == a);
    CHECK(v.nodes[a].flags == (weapon_tree::kOwned | weapon_tree::kSelected));
    CHECK(v.nodes[Find(v, 3)].flags == weapon_tree::kNext);
    CHECK(v.nodes[Find(v, 5)].flags == weapon_tree::kNext);
    CHECK(Find(v, 4) < 0);  // (nothing past a "?")
    CHECK(Find(v, 6) < 0);
    CHECK(Find(v, 7) < 0);  // (another line)
    // the first child on its parent's row, the next one below
    CHECK(v.nodes[a].row == 0);
    CHECK(v.nodes[a].column == 1);
    CHECK(v.nodes[Find(v, 3)].row == 0);
    CHECK(v.nodes[Find(v, 5)].row == 1);
    CHECK(v.columns == 3);
    CHECK(v.rows == 2);
    CHECK(weapon_tree::Text(c, v) == "Root\n  A [box] [smithy]\n    ?\n  ?\n");
}

TEST_CASE("a weapon the player does not have, and the one worn") {
    const weapon_tree::Class c = Sample();
    const std::vector<std::uint8_t> owned(9, 0);
    const weapon_tree::View v = weapon_tree::Build(c, 6, owned, 2);
    // Root, A (worn), A+ (?), B, B+ (the Smithy's), Two (?, from its first parent A+ only: not shown twice)
    CHECK(v.nodes[Find(v, 2)].flags == weapon_tree::kWorn);
    CHECK(v.nodes[Find(v, 5)].flags == weapon_tree::kPast);
    CHECK(v.nodes[Find(v, 6)].flags == weapon_tree::kSelected);
    CHECK(v.nodes[Find(v, 3)].flags == weapon_tree::kNext);
    CHECK(Find(v, 8) < 0);
    CHECK(v.nodes.size() == 5);
}

TEST_CASE("nothing for an id that is no weapon") {
    const weapon_tree::Class c = Sample();
    CHECK(weapon_tree::Build(c, 0, {}, -1).nodes.empty());
    CHECK(weapon_tree::Build(c, 42, {}, -1).nodes.empty());
}
