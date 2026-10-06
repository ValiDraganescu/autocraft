// A Kestrel has two weapons: twin rockets at the ground and a rail gun at
// the air, each with its own cooldown. Not in the Swift game (where the
// Kestrel is rockets at the ground only). Each test is a few seconds of a
// hand-built skirmish, never a game.
#include "SimHelpers.h"

using namespace ac;
using namespace simtest;

namespace {

GameState arena() {
    GameState s = bare(2, 600, {}, {}, 1);
    s.players[0].ore = 1000;
    s.players[0].hydrogen = 1000;
    return s;
}

int64_t put(GameState& s, Unit::Kind kind, Vec2 p, int64_t owner, double heading = 0) {
    const int64_t id = s.nextID;
    Unit u(id, kind, owner, p, heading, Unit::Task::idle);
    if (kind == Unit::Kind::dropship) { u.energy = 0; u.cargo = std::vector<int64_t>{}; }
    s.units.push_back(u);
    s.nextID += 1;
    return id;
}

} // namespace

/// Against a unit on the ground a Kestrel picks its rockets: 12 x2 volleys,
/// no rail slug, and the rail clock stays untouched.
TEST(kestrel_picksRocketsAgainstGround) {
    GameState s = arena();
    const int64_t kestrel = put(s, Unit::Kind::kestrel, Vec2::zero, 0);
    const int64_t foe = put(s, Unit::Kind::juggernaut, Vec2(4, 0), 1, pi);
    Simulation sim(s);
    const Unit k = *sim.state.unit(kestrel);
    const UnitStats w = sim.weapon(k, sim.target(foe)->air);
    EXPECT_TRUE(!w.rail);
    EXPECT_EQ(w.hits, 2);
    EXPECT_EQ(w.damage, 12.0);
    EXPECT_TRUE(sim.canHit(k, *sim.target(foe)));
    const auto events = run(sim, 1.0);
    EXPECT_TRUE(shots(events, kestrel) >= 1);
    EXPECT_TRUE(hp(sim, foe) < Rules::hp(Unit::Kind::juggernaut));
    EXPECT_TRUE(!sim.state.unit(kestrel)->railCooldown.has_value()); // never touched the rail gun
}

/// Against a flier it picks the rail gun: one hitscan slug that lands in
/// the step it is fired, 34 (+14 armoured) less the target's armour.
TEST(kestrel_picksRailAgainstAir) {
    GameState s = arena();
    const int64_t kestrel = put(s, Unit::Kind::kestrel, Vec2::zero, 0);
    const int64_t flier = put(s, Unit::Kind::dropship, Vec2(7, 0), 1);
    Simulation sim(s);
    const Unit k = *sim.state.unit(kestrel);
    EXPECT_TRUE(sim.weapon(k, true).rail);
    EXPECT_TRUE(sim.weapon(k, true).range > sim.weapon(k, false).range); // outranges the flak
    EXPECT_TRUE(sim.canHit(k, *sim.target(flier)));
    int64_t fired = 0;
    for (int n = 0; n < 60 && fired == 0; n++) fired += shots(sim.step(1.0 / 30), kestrel);
    EXPECT_EQ(fired, 1);
    // Hitscan: the damage is in already, with no flight time to wait for.
    const double slug = Rules::damage(UnitStats::railgun, true, false, 1);
    EXPECT_EQ(slug, 47.0);
    EXPECT_NEAR(Rules::hp(Unit::Kind::dropship) - hp(sim, flier), slug, 1e-9);
    EXPECT_TRUE(sim.state.unit(kestrel)->railCooldown.value_or(0) > 2.9);
    EXPECT_EQ(sim.state.unit(kestrel)->cooldown.value_or(0), 0.0); // the rockets did not fire
}

/// Each weapon keeps its own cooldown: a rocket volley does not delay the
/// rail gun, a slug does not delay the rockets, and each is ready after its
/// own time.
TEST(kestrel_weaponsKeepTheirOwnCooldowns) {
    GameState s = arena();
    const int64_t kestrel = put(s, Unit::Kind::kestrel, Vec2::zero, 0);
    const int64_t ground = put(s, Unit::Kind::juggernaut, Vec2(4, 0), 1, pi);
    const int64_t air = put(s, Unit::Kind::dropship, Vec2(7, 0), 1);
    Simulation sim(s);
    Unit& k = sim.state.units[0];
    EXPECT_TRUE(sim.fire(k, air, *sim.target(air), 1.0 / 30));
    EXPECT_TRUE(sim.fire(k, ground, *sim.target(ground), 1.0 / 30)); // the slug did not block it
    EXPECT_NEAR(k.railCooldown.value(), UnitStats::railgun.cooldown, 1e-9);
    EXPECT_NEAR(k.cooldown.value(), Rules::stats(Unit::Kind::kestrel).cooldown, 1e-9);
    EXPECT_TRUE(!sim.fire(k, air, *sim.target(air), 1.0 / 30));      // rail waits
    EXPECT_TRUE(!sim.fire(k, ground, *sim.target(ground), 1.0 / 30)); // rockets wait
    // After the rockets' time only the rockets are ready again.
    sim.state.units.erase(std::remove_if(sim.state.units.begin(), sim.state.units.end(),
                                         [&](const Unit& u) { return u.id == air || u.id == ground; }), sim.state.units.end());
    run(sim, 1.3);
    EXPECT_EQ(sim.cooldownFor(*sim.state.unit(kestrel), false), 0.0);
    EXPECT_TRUE(sim.cooldownFor(*sim.state.unit(kestrel), true) > 1.5);
    run(sim, 2.0);
    EXPECT_EQ(sim.cooldownFor(*sim.state.unit(kestrel), true), 0.0);
}
