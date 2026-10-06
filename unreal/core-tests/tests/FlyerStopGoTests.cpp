// A flier under an army order (attack-move) or walking to its place at the
// rally used to move on one step in eight: `travel` never sets `goal` for a
// flier, so the "walk on toward it meanwhile" branches (which only run while
// a goal is set) did nothing between two thinks (4 a second) but turn. The
// Swift game has the same flaw. Each test is a few seconds of a hand-built
// scene, never a game.
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
    if (kind == Unit::Kind::hailstorm || kind == Unit::Kind::firefly) u.aim = heading;
    if (kind == Unit::Kind::dropship) { u.energy = 0; u.cargo = std::vector<int64_t>{}; }
    s.units.push_back(u);
    s.nextID += 1;
    return id;
}

struct Gait { int64_t zeros = 0; int64_t longestStall = 0; double covered = 0; };

/// Walk `seconds` of steps and measure the unit's per-step displacement.
Gait gait(Simulation& sim, int64_t id, double seconds) {
    Gait g;
    int64_t stall = 0;
    for (int64_t k = 0; k < int64_t(seconds * 30); k++) {
        const Vec2 before = sim.state.unit(id)->position;
        sim.step(1.0 / 30);
        const double d = distance(before, sim.state.unit(id)->position);
        g.covered += d;
        if (d < 1e-9) { g.zeros++; stall++; g.longestStall = std::max(g.longestStall, stall); } else stall = 0;
    }
    return g;
}

const Unit::Kind fliers[] = {Unit::Kind::kestrel, Unit::Kind::firefly, Unit::Kind::hailstorm};

} // namespace

/// Attack-move across open ground, nothing to shoot, every flier and fast
/// paths on and off: no stop-and-go.
TEST(flyer_attackMoveNeverStopsAndGoes) {
    for (Unit::Kind kind : fliers) {
        for (bool fast : {false, true}) {
            GameState s = arena();
            const int64_t id = put(s, kind, Vec2(10, 10), 0, 0);
            s.units.back().task = Unit::Task::attackMove;
            s.players[0].attack = Vec2(120, 10);
            Simulation sim(s);
            sim.setFastPaths(fast, 32);
            sim.step(1.0 / 30);
            const Gait g = gait(sim, id, 6);
            if (getenv("AC_GAIT")) printf("  attack-move %d fast=%d zero=%lld longest=%lld covered=%.1f\n", int(kind), int(fast),
                                          (long long)g.zeros, (long long)g.longestStall, g.covered);
            EXPECT_TRUE(g.longestStall <= 1);
            EXPECT_TRUE(g.covered > 6 * Rules::speed(kind) * 0.7);
        }
    }
}

/// The same with enemies it cannot shoot in sight (a flier-blind ground
/// unit for a Hailstorm's rail-less case is not needed: a distant enemy
/// Prospector and a Dropship only), and an Arbiter-free field.
TEST(flyer_attackMoveWithFoesNearbyStillFlies) {
    GameState s = arena();
    const int64_t k = put(s, Unit::Kind::kestrel, Vec2(10, 10), 0, 0);
    s.units.back().task = Unit::Task::attackMove;
    put(s, Unit::Kind::juggernaut, Vec2(60, 40), 1, pi);
    s.players[0].attack = Vec2(120, 10);
    Simulation sim(s);
    sim.step(1.0 / 30);
    const Gait g = gait(sim, k, 4);
    EXPECT_TRUE(g.longestStall <= 1);
}

/// Walking to its place at the rally moves every step too.
TEST(flyer_toRallyNeverStopsAndGoes) {
    for (Unit::Kind kind : fliers) {
        GameState s = arena();
        addStructure(s, Structure::Kind::citadel, Vec2(70, 10), 0);
        const int64_t id = put(s, kind, Vec2(10, 10), 0, 0);
        s.units.back().task = Unit::Task::toRally;
        Simulation sim(s);
        sim.step(1.0 / 30);
        const Gait g = gait(sim, id, 4);
        if (getenv("AC_GAIT")) printf("  rally %d zero=%lld longest=%lld covered=%.1f\n", int(kind), (long long)g.zeros,
                                      (long long)g.longestStall, g.covered);
        EXPECT_TRUE(g.longestStall <= 1);
    }
}
