// Port of Tests/GameCoreTests/AirTests.swift: air attack and anti-air, the
// Kestrel gunship, the Hailstorm flak vehicle and the Sentinel tower. Each
// test is a few seconds of a hand-built skirmish, never a game.
#include "SimHelpers.h"

#include "Pilot.h"

using namespace ac;
using namespace simtest;

namespace {

/// Two players, nothing on the field, no map (no fog, straight flight).
GameState empty() {
    GameState s = bare(2, 600, {}, {}, 1);
    s.players[0].ore = 1000;
    s.players[0].hydrogen = 1000;
    return s;
}

int64_t add(GameState& s, Unit::Kind kind, Vec2 p, int64_t owner, double heading = 0) {
    const int64_t id = s.nextID;
    Unit u(id, kind, owner, p, heading, Unit::Task::idle);
    if (kind == Unit::Kind::longbow) { u.anchor = 0; u.anchored = false; u.aim = heading; }
    if (kind == Unit::Kind::hailstorm || kind == Unit::Kind::firefly) u.aim = heading;
    if (kind == Unit::Kind::dropship) { u.energy = 0; u.cargo = std::vector<int64_t>{}; }
    s.units.push_back(u);
    s.nextID += 1;
    return id;
}

int64_t add(GameState& s, Structure::Kind kind, Vec2 p, int64_t owner) { return addStructure(s, kind, p, owner); }

} // namespace

// MARK: - Kestrel

/// A Kestrel's rockets kill two Prospectors in a few volleys; the
/// Prospectors cannot cut at a flyer.
TEST(air_kestrelKillsProspectors) {
    GameState s = empty();
    const int64_t kestrel = add(s, Unit::Kind::kestrel, Vec2::zero, 0);
    const int64_t a = add(s, Unit::Kind::prospector, Vec2(3, 0), 1);
    const int64_t b = add(s, Unit::Kind::prospector, Vec2(3, 1.5), 1);
    Simulation sim(s);
    const auto events = run(sim, 6);
    EXPECT_TRUE(shots(events, kestrel) >= 4); // two volleys each
    EXPECT_TRUE(hp(sim, a) <= 0);
    EXPECT_TRUE(hp(sim, b) <= 0);
    EXPECT_EQ(hp(sim, kestrel), Rules::hp(Unit::Kind::kestrel)); // untouched
}

/// Fireflies, Longbows (anchored too), Juggernauts and Comets cannot hit
/// a Kestrel; Rangers, Hailstorms and Sentinels can.
TEST(air_onlyAntiAirHitsAKestrel) {
    for (const auto kind : {Unit::Kind::firefly, Unit::Kind::longbow, Unit::Kind::juggernaut, Unit::Kind::comet}) {
        GameState s = empty();
        const int64_t gun = add(s, kind, Vec2::zero, 0);
        if (kind == Unit::Kind::longbow) { s.units[0].anchor = 1; s.units[0].anchored = true; }
        const int64_t kestrel = add(s, Unit::Kind::kestrel, Vec2(4, 0), 1, pi);
        Simulation sim(s);
        EXPECT_TRUE(!sim.canHit(*sim.state.unit(gun), *sim.target(kestrel)));
        const auto events = run(sim, 1);
        EXPECT_EQ(shots(events, gun), 0); // holds its fire
        EXPECT_EQ(hp(sim, kestrel), Rules::hp(Unit::Kind::kestrel)); // cannot hurt it
    }
    for (const auto kind : {Unit::Kind::ranger, Unit::Kind::hailstorm}) {
        GameState s = empty();
        const int64_t gun = add(s, kind, Vec2::zero, 0);
        const int64_t kestrel = add(s, Unit::Kind::kestrel, Vec2(4, 0), 1, pi);
        Simulation sim(s);
        EXPECT_TRUE(sim.canHit(*sim.state.unit(gun), *sim.target(kestrel)));
        const auto events = run(sim, 1.5);
        EXPECT_TRUE(shots(events, gun) > 0); // fires
        EXPECT_TRUE(hp(sim, kestrel) < Rules::hp(Unit::Kind::kestrel)); // hurts it
    }
    GameState s = empty();
    const int64_t tower = add(s, Structure::Kind::sentinel, Vec2::zero, 0);
    const int64_t kestrel = add(s, Unit::Kind::kestrel, Vec2(5, 0), 1, pi);
    Simulation sim(s);
    const auto events = run(sim, 1.5);
    EXPECT_TRUE(shots(events, tower) > 0); // the Sentinel fires
    EXPECT_TRUE(hp(sim, kestrel) < Rules::hp(Unit::Kind::kestrel)); // and hurts it
}

// MARK: - Hailstorm

/// A Hailstorm never fires at the ground, even with an enemy Ranger
/// shooting it point-blank.
TEST(air_hailstormCannotShootGround) {
    GameState s = empty();
    const int64_t flak = add(s, Unit::Kind::hailstorm, Vec2::zero, 0);
    const int64_t ranger = add(s, Unit::Kind::ranger, Vec2(3, 0), 1);
    Simulation sim(s);
    EXPECT_TRUE(!sim.canHit(*sim.state.unit(flak), *sim.target(ranger)));
    const auto events = run(sim, 3);
    EXPECT_EQ(shots(events, flak), 0);
    EXPECT_EQ(hp(sim, ranger), Rules::hp(Unit::Kind::ranger));
    EXPECT_TRUE(hp(sim, flak) < Rules::hp(Unit::Kind::hailstorm)); // the Ranger did shoot it
}

/// One flak burst hurts the Dropship it is fired at in full (12 + 8
/// armoured, less 1 armour) and an enemy Dropship beside it by half;
/// its own Dropship beside them is spared.
TEST(air_hailstormFlakSplashesEnemyFlyers) {
    GameState s = empty();
    const int64_t flak = add(s, Unit::Kind::hailstorm, Vec2::zero, 0);
    const int64_t aimed = add(s, Unit::Kind::dropship, Vec2(5, 0), 1);
    const int64_t beside = add(s, Unit::Kind::dropship, Vec2(5, 1.3), 1);
    const int64_t friend_ = add(s, Unit::Kind::dropship, Vec2(5, -1.0), 0);
    Simulation sim(s);
    int64_t fired = 0;
    for (int k = 0; k < 60 && fired == 0; k++) fired += shots(sim.step(1.0 / 30), flak);
    EXPECT_EQ(fired, 1);
    run(sim, 0.4); // the shell's flight
    const double full = Rules::damage(Unit::Kind::hailstorm, false, true, false, 1);
    EXPECT_EQ(full, 19.0);
    std::vector<double> hit{Rules::hp(Unit::Kind::dropship) - hp(sim, aimed), Rules::hp(Unit::Kind::dropship) - hp(sim, beside)};
    std::sort(hit.begin(), hit.end());
    EXPECT_NEAR(hit[1], full, 1e-9);     // the one it aimed at
    EXPECT_NEAR(hit[0], full / 2, 1e-9); // the one beside it
    EXPECT_EQ(hp(sim, friend_), Rules::hp(Unit::Kind::dropship)); // its own side is spared
}

// MARK: - Sentinel

/// A Prospector can put a Sentinel up only once a Garrison of its side
/// stands.
TEST(air_sentinelNeedsAGarrison) {
    GameState s = empty();
    const int64_t worker = add(s, Unit::Kind::prospector, Vec2::zero, 0);
    Simulation sim(s);
    EXPECT_TRUE(sim.buildRefusal(Structure::Kind::sentinel, 0).has_value());
    EXPECT_TRUE(!sim.issue(Command::Build{worker, Structure::Kind::sentinel, Vec2(4, 0)}));
    sim.state.structures.push_back(Structure(500, Structure::Kind::garrison, 0, Vec2(-8, 0)));
    EXPECT_TRUE(!sim.buildRefusal(Structure::Kind::sentinel, 0).has_value());
    const int64_t ore = sim.state.ore();
    EXPECT_TRUE(sim.issue(Command::Build{worker, Structure::Kind::sentinel, Vec2(4, 0)}));
    EXPECT_EQ(sim.state.ore(), ore - Rules::cost(Structure::Kind::sentinel));
    EXPECT_TRUE(Rules::requires_(Structure::Kind::sentinel) == Structure::Kind::garrison);
}

/// A Sentinel shoots the Dropship over it and never the Ranger at its
/// foot; one still going up shoots nothing.
TEST(air_sentinelShootsAirOnly) {
    GameState s = empty();
    const int64_t tower = add(s, Structure::Kind::sentinel, Vec2::zero, 0);
    const int64_t ranger = add(s, Unit::Kind::ranger, Vec2(0, 2.5), 1);
    const int64_t dropship = add(s, Unit::Kind::dropship, Vec2(5, 0), 1);
    Simulation sim(s);
    const auto events = run(sim, 3);
    EXPECT_TRUE(shots(events, tower) > 0);
    EXPECT_EQ(hp(sim, ranger), Rules::hp(Unit::Kind::ranger)); // not the Ranger
    EXPECT_TRUE(hp(sim, dropship) < Rules::hp(Unit::Kind::dropship)); // the Dropship
    EXPECT_TRUE(sim.state.structure(tower)->hp < Rules::hp(Structure::Kind::sentinel)); // the Ranger shot back

    GameState u = empty();
    const int64_t rising = u.nextID;
    u.structures.push_back(Structure(rising, Structure::Kind::sentinel, 0, Vec2::zero, 10.0));
    u.nextID += 1;
    const int64_t target = add(u, Unit::Kind::dropship, Vec2(5, 0), 1);
    Simulation sim2(u);
    EXPECT_EQ(shots(run(sim2, 2), rising), 0); // under construction
    EXPECT_EQ(hp(sim2, target), Rules::hp(Unit::Kind::dropship));
}

// MARK: - Flight

/// A Kestrel flies straight down a cliff a Ranger has to walk round by
/// the ramp, like a Dropship.
TEST(air_kestrelFliesOverACliff) {
    const MapDefinition& m = homeMap();
    const TerrainField field(m);
    const NavGrid nav(m, field);
    const auto pair = cliffPair(m, field, nav, 4, [&](Vec2 p, Vec2 q) { return !nav.clear(p, q); });
    ASSERT_TRUE(pair.has_value()); // a cliff edge
    const auto [high, low] = *pair;
    struct Trip { double seconds; bool overCliff; };
    auto trip = [&](Unit::Kind kind) -> Trip {
        Simulation sim(bare(1, 0, {}, {Unit(1, kind, 0, high, 0, Unit::Task::idle)}, 2), m);
        Unit u = sim.state.units[0];
        bool over = false;
        for (int n = 1; n <= 120 * 30; n++) {
            if (sim.travel(u, low, 0.2, 1.0 / 30)) return Trip{static_cast<double>(n) / 30, over};
            over = over || !nav.walkable(u.position);
        }
        return Trip{INFINITY, over};
    };
    const Trip kestrel = trip(Unit::Kind::kestrel), ranger = trip(Unit::Kind::ranger);
    EXPECT_TRUE(kestrel.overCliff); // over the cliff face
    EXPECT_TRUE(!ranger.overCliff);
    EXPECT_TRUE(kestrel.seconds < 4 / Rules::speed(Unit::Kind::kestrel) + 0.5); // straight there
    EXPECT_TRUE(ranger.seconds > 2 * kestrel.seconds); // round by the ramp
}

// MARK: - The tables

/// Where they come from and what they need.
TEST(air_whereTheyAreBuilt) {
    auto contains = [](const auto& xs, auto x) { return std::find(xs.begin(), xs.end(), x) != xs.end(); };
    EXPECT_TRUE(contains(Rules::trains(Structure::Kind::spacedock), Unit::Kind::kestrel));
    EXPECT_TRUE(Rules::needsLab(Unit::Kind::kestrel));
    EXPECT_TRUE(contains(Rules::trains(Structure::Kind::foundry), Unit::Kind::hailstorm));
    EXPECT_TRUE(!Rules::needsLab(Unit::Kind::hailstorm));
    EXPECT_TRUE(contains(PilotBuild::kinds, Structure::Kind::sentinel));
    const auto& k = Rules::stats(Unit::Kind::kestrel);
    const auto& h = Rules::stats(Unit::Kind::hailstorm);
    EXPECT_TRUE(k.air && k.hitsGround && k.hitsAir); // rockets at the ground, a rail gun at the air (differs from Swift)
    EXPECT_TRUE(h.hitsAir && !h.hitsGround && !h.air);
    EXPECT_TRUE(Leveling::machines.contains(Unit::Kind::kestrel) && Leveling::machines.contains(Unit::Kind::hailstorm));
}
