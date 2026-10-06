// Port of Tests/GameCoreTests/PilotTests.swift: driving a unit by hand,
// short scenes around one Prospector at its main (and one action each for
// the other kinds, a second or two of sim). Isolated scenarios only.

#include "test.h"

#include "Commander.h"
#include "Hearing.h"
#include "MapLibrary.h"
#include "Pilot.h"
#include "Simulation.h"
#include "WindowMaps.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace {

using namespace ac;

struct Driven {
    Simulation sim;
    int64_t prospector;
    Structure citadel;
};

/// A small window-mode map, blue alone, and blue's first Prospector taken over.
Driven driven() {
    const MapDefinition map = WindowMaps::build(MapChoice{MapStyle::openField, MapSize::small});
    Simulation sim(GameState::new_(map), map);
    const auto pu = std::find_if(sim.state.units.begin(), sim.state.units.end(), [](const Unit& u) {
        return u.owner == 0 && u.kind == Unit::Kind::prospector;
    });
    const auto pc = std::find_if(sim.state.structures.begin(), sim.state.structures.end(), [](const Structure& s) {
        return s.owner == 0 && s.kind == Structure::Kind::citadel;
    });
    const int64_t prospector = pu->id;
    const Structure citadel = *pc;
    EXPECT_TRUE(sim.take(prospector));
    return Driven{sim, prospector, citadel};
}

int64_t indexOf(const Simulation& sim, int64_t id) {
    for (size_t i = 0; i < sim.state.units.size(); ++i) {
        if (sim.state.units[i].id == id) return static_cast<int64_t>(i);
    }
    return -1;
}

Unit unit(const Simulation& sim, int64_t id) { return sim.state.units[static_cast<size_t>(indexOf(sim, id))]; }
Unit& unitRef(Simulation& sim, int64_t id) { return sim.state.units[static_cast<size_t>(indexOf(sim, id))]; }

Structure citadelOf(const Simulation& sim) {
    return *std::find_if(sim.state.structures.begin(), sim.state.structures.end(), [](const Structure& s) {
        return s.owner == 0 && s.kind == Structure::Kind::citadel;
    });
}

/// Stand `id` at a free spot `gap` cells off patch `pi`'s long side, on
/// the side facing its Citadel, looking at the patch.
void standAt(Simulation& sim, int64_t id, int64_t pi_, double gap) {
    const OreDeposit p = sim.state.patches[static_cast<size_t>(pi_)];
    const Structure citadel = citadelOf(sim);
    // The patch's short axis, pointing toward the Citadel.
    Vec2 n(-std::sin(p.angle), std::cos(p.angle));
    if (dot(n, citadel.position - p.position) < 0) n = -n;
    unitRef(sim, id).position = p.position + n * (0.5 + gap);
    sim.pilot->heading = std::atan2(-n.y, -n.x);
}

std::vector<GameEvent> run(Simulation& sim, double seconds, Vec2 walk = Vec2(0, 0)) {
    std::vector<GameEvent> events;
    const int64_t n = static_cast<int64_t>(seconds * 30);
    for (int64_t k = 0; k < n; ++k) {
        if (sim.pilot) sim.pilot->walk = walk;
        const auto e = sim.step(1.0 / 30);
        events.insert(events.end(), e.begin(), e.end());
    }
    return events;
}

int64_t nearestPatch(const Simulation& sim, const Structure& citadel) {
    int64_t best = 0;
    for (int64_t i = 1; i < static_cast<int64_t>(sim.state.patches.size()); ++i) {
        if (distance(sim.state.patches[static_cast<size_t>(i)].position, citadel.position)
            < distance(sim.state.patches[static_cast<size_t>(best)].position, citadel.position)) {
            best = i;
        }
    }
    return best;
}

int64_t nearestWell(const Simulation& sim, const Structure& citadel) {
    const auto& wells = *sim.state.wells;
    int64_t best = 0;
    for (int64_t i = 1; i < static_cast<int64_t>(wells.size()); ++i) {
        if (distance(wells[static_cast<size_t>(i)].position, citadel.position)
            < distance(wells[static_cast<size_t>(best)].position, citadel.position)) {
            best = i;
        }
    }
    return best;
}

/// The Citadel's open side (away from its ore), as a unit vector.
Vec2 openSide(const Simulation& sim, const Structure& citadel) {
    Vec2 sum(0, 0);
    int64_t count = 0;
    for (const OreDeposit& p : sim.state.patches) {
        if (distance(p.position, citadel.position) < 12) {
            sum = sum + p.position;
            count += 1;
        }
    }
    return normalize(citadel.position - sum / static_cast<double>(count));
}

std::optional<Structure> structureByID(const Simulation& sim, int64_t id) { return sim.state.structure(id); }

/// A red Ranger (or `kind`) standing at `p`.
void enemy(Simulation& sim, int64_t id, Vec2 p, Unit::Kind kind = Unit::Kind::ranger) {
    sim.state.units.push_back(Unit(id, kind, 1, p, 0, Unit::Task::idle));
}

/// Shots `me` fired, by target, in these events.
std::vector<int64_t> shots(const std::vector<GameEvent>& events, int64_t me) {
    std::vector<int64_t> out;
    for (const GameEvent& e : events) {
        if (const auto s = e.as<GameEvent::Shot>(); s && s->unit == me) out.push_back(s->target);
    }
    return out;
}

/// Shots `me` fired at nothing in these events.
int64_t misses(const std::vector<GameEvent>& events, int64_t me) {
    int64_t n = 0;
    for (const GameEvent& e : events) {
        if (const auto m = e.as<GameEvent::Missed>(); m && m->unit == me) n += 1;
    }
    return n;
}

struct DrivenUnit {
    Simulation sim;
    int64_t me;
    Vec2 at;
    Vec2 out;
};

/// Blue's own `kind`, driven, out on the Citadel's open side facing away.
DrivenUnit drivenUnit(Unit::Kind kind) {
    Driven d = driven();
    const Vec2 out = openSide(d.sim, d.citadel);
    const Vec2 at = d.citadel.position + out * (Rules::radius(Structure::Kind::citadel) + 4);
    const double h = std::atan2(out.y, out.x);
    d.sim.state.units.push_back(Unit(998, kind, 0, at, h, Unit::Task::idle));
    EXPECT_TRUE(d.sim.take(998));
    d.sim.pilot->heading = h;
    return DrivenUnit{d.sim, 998, at, out};
}

/// Blue's own Ranger, driven, out on the Citadel's open side facing away.
DrivenUnit drivenRanger() { return drivenUnit(Unit::Kind::ranger); }

PilotTarget enemyT(int64_t id) { return PilotTarget::Enemy{id}; }

} // namespace

TEST(PilotTests_onlyThePlayersDrivableUnitsCanBeTaken) {
    auto [sim, prospector, citadel] = driven();
    EXPECT_TRUE(sim.pilot && sim.pilot->unit == prospector);
    const auto red = *std::find_if(sim.state.units.begin(), sim.state.units.end(), [](const Unit& u) { return u.owner == 1; });
    EXPECT_TRUE(!sim.take(red.id));      // not the player's
    EXPECT_TRUE(!sim.take(citadel.id));  // not a unit
    EXPECT_TRUE(!sim.pilot);             // a refused take-over leaves no one driven
    sim.state.units.push_back(Unit(999, Unit::Kind::ranger, 0, citadel.position + Vec2(6, 0), 0, Unit::Task::idle));
    EXPECT_EQ(sim.take(999), Pilot::drivable.contains(Unit::Kind::ranger));
}

/// Forward is where it faces, at Prospector speed; a building stops it and it
/// slides along the wall instead of passing through.
TEST(PilotTests_walksWhereItFacesAndNotThroughBuildings) {
    auto [sim, prospector, citadel] = driven();
    // Out on the open side, away from the ore line.
    const Vec2 out = openSide(sim, citadel);
    const Vec2 start = citadel.position + out * (Rules::radius(Structure::Kind::citadel) + 2);
    unitRef(sim, prospector).position = start;
    const double h = std::atan2(out.y, out.x);
    sim.pilot->heading = h;
    run(sim, 0.5, Vec2(1, 0));
    const Vec2 moved = unit(sim, prospector).position - start;
    EXPECT_NEAR(dot(moved, out), Rules::prospectorSpeed * Rules::heroSpeed * 0.5, 0.15);
    EXPECT_NEAR(dot(moved, Vec2(-out.y, out.x)), 0, 0.05);
    EXPECT_NEAR(unit(sim, prospector).heading, h, 1e-9);
    // Straight into the Citadel for 3 s: it ends at the wall, never inside.
    sim.pilot->heading = std::atan2(-out.y, -out.x);
    for (int k = 0; k < 90; ++k) {
        run(sim, 1.0 / 30, Vec2(1, 0));
        EXPECT_TRUE(!NavGrid::insideStructure(unit(sim, prospector).position, citadel, 0.1));
    }
    EXPECT_TRUE(distance(unit(sim, prospector).position, citadel.position) < Rules::radius(Structure::Kind::citadel) + 0.6);
}

/// Holding the action key at a field drills it; the load comes after
/// the mining time, and walking into the Citadel banks it.
TEST(PilotTests_drillsAFieldAndUnloadsAtTheCitadel) {
    auto [sim, prospector, citadel] = driven();
    const int64_t pi_ = nearestPatch(sim, citadel);
    standAt(sim, prospector, pi_, 0.5);
    run(sim, 0.1);
    EXPECT_EQ(sim.pilotTarget(unit(sim, prospector)), PilotTarget(PilotTarget::Patch{pi_}));
    sim.pilot->act = true;
    sim.pilot->hold = true;
    run(sim, 0.1);
    EXPECT_EQ(unit(sim, prospector).task, Unit::Task::mining);
    EXPECT_EQ(sim.pilot->act, false);  // the key press is used up
    const int64_t before = sim.state.patches[static_cast<size_t>(pi_)].remaining;
    run(sim, Rules::miningTime);
    Unit u = unit(sim, prospector);
    EXPECT_EQ(u.carrying, Rules::heroCarry);  // the driven Prospector carries five loads
    EXPECT_EQ(sim.state.patches[static_cast<size_t>(pi_)].remaining, before - Rules::heroCarry);
    EXPECT_EQ(sim.pilotTarget(u), PilotTarget(PilotTarget::Full{}));  // hands full at the field
    sim.pilot->hold = false;
    // Face the Citadel and walk in.
    const int64_t ore = sim.state.ore();
    const Vec2 d = citadel.position - u.position;
    sim.pilot->heading = std::atan2(d.y, d.x);
    int64_t deposited = 0;
    for (const GameEvent& e : run(sim, 4, Vec2(1, 0))) {
        if (const auto dp = e.as<GameEvent::Deposited>(); dp && dp->unit == prospector) deposited += dp->amount;
    }
    u = unit(sim, prospector);
    EXPECT_EQ(deposited, Rules::heroCarry);
    EXPECT_EQ(sim.state.ore(), ore + Rules::heroCarry);
    EXPECT_EQ(u.carrying, int64_t(0));
}

/// Walking away mid-drill loses the load.
TEST(PilotTests_walkingOffStopsTheDrill) {
    auto [sim, prospector, citadel] = driven();
    const int64_t pi_ = nearestPatch(sim, citadel);
    standAt(sim, prospector, pi_, 0.5);
    sim.pilot->act = true;
    sim.pilot->hold = true;
    run(sim, 2);
    EXPECT_EQ(unit(sim, prospector).task, Unit::Task::mining);
    sim.pilot->heading += pi;
    run(sim, Rules::miningTime, Vec2(1, 0));
    const Unit u = unit(sim, prospector);
    EXPECT_EQ(u.task, Unit::Task::idle);
    EXPECT_EQ(u.carrying, int64_t(0));
    EXPECT_TRUE(!sim.state.patches[static_cast<size_t>(pi_)].miner);
}

/// The player goes first at a field another Prospector is drilling: that one
/// moves along.
TEST(PilotTests_takesAFieldFromAnAIProspector) {
    auto [sim, prospector, citadel] = driven();
    const int64_t pi_ = nearestPatch(sim, citadel);
    sim.state.units.push_back(Unit(998, Unit::Kind::prospector, 0, sim.state.patches[static_cast<size_t>(pi_)].position, 0,
                                   Unit::Task::mining, pi_, 3));
    sim.state.patches[static_cast<size_t>(pi_)].miner = 998;
    standAt(sim, prospector, pi_, 0.5);
    sim.pilot->act = true;
    sim.pilot->hold = true;
    run(sim, 0.2);
    EXPECT_TRUE(sim.state.patches[static_cast<size_t>(pi_)].miner == prospector);
    EXPECT_TRUE(unit(sim, 998).task != Unit::Task::mining);
}

/// Into its own finished Derrick for a load of MH, which it banks at
/// the Citadel like ore.
TEST(PilotTests_takesHydrogenFromADerrick) {
    auto [sim, prospector, citadel] = driven();
    const int64_t g = nearestWell(sim, citadel);
    const Vec2 at = (*sim.state.wells)[static_cast<size_t>(g)].position;
    sim.state.structures.push_back(Structure(900, Structure::Kind::derrick, 0, at));
    const Vec2 toward = normalize(citadel.position - at);
    unitRef(sim, prospector).position = at + toward * (Rules::radius(Structure::Kind::derrick) + 0.6);
    sim.pilot->heading = std::atan2(-toward.y, -toward.x);
    run(sim, 0.1);
    EXPECT_EQ(sim.pilotTarget(unit(sim, prospector)), PilotTarget(PilotTarget::Derrick{900, false}));
    sim.pilot->act = true;
    run(sim, 0.1);
    EXPECT_EQ(unit(sim, prospector).task, Unit::Task::inDerrick);
    run(sim, Rules::hydrogenTime);
    const Unit u = unit(sim, prospector);
    EXPECT_EQ(u.carrying, Rules::heroHydrogenCarry);
    EXPECT_TRUE(u.hydrogen == true);
    EXPECT_EQ((*sim.state.wells)[static_cast<size_t>(g)].remaining, Rules::wellHydrogen - Rules::heroHydrogenCarry);
    sim.pilot->heading = std::atan2(toward.y, toward.x);
    const int64_t hydrogen = sim.state.hydrogen();
    run(sim, 6, Vec2(1, 0));
    EXPECT_EQ(sim.state.hydrogen(), hydrogen + Rules::heroHydrogenCarry);
}

/// At its own damaged Derrick a click goes in for MH; with R held the
/// click repairs it instead, and the repair goes on while the click is
/// held after R is let go.
TEST(PilotTests_rHeldRepairsADamagedDerrick) {
    auto [sim, prospector, citadel] = driven();
    const int64_t g = nearestWell(sim, citadel);
    const Vec2 at = (*sim.state.wells)[static_cast<size_t>(g)].position;
    sim.state.structures.push_back(Structure(900, Structure::Kind::derrick, 0, at));
    const Vec2 toward = normalize(citadel.position - at);
    unitRef(sim, prospector).position = at + toward * (Rules::radius(Structure::Kind::derrick) + 0.6);
    sim.pilot->heading = std::atan2(-toward.y, -toward.x);
    run(sim, 0.1);
    sim.pilot->mend = true;
    EXPECT_TRUE(!sim.pilotTarget(unit(sim, prospector)));  // nothing to repair
    sim.pilot->mend = false;
    size_t ri = 0;
    while (sim.state.structures[ri].id != 900) ++ri;
    const double half = Rules::hp(Structure::Kind::derrick) / 2;
    sim.state.structures[ri].hp = half;
    EXPECT_EQ(sim.pilotTarget(unit(sim, prospector)), PilotTarget(PilotTarget::Derrick{900, false}));
    EXPECT_EQ(sim.pilotRepair(unit(sim, prospector)), PilotTarget(PilotTarget::Repair{900}));
    sim.pilot->mend = true;
    EXPECT_EQ(sim.pilotTarget(unit(sim, prospector)), PilotTarget(PilotTarget::Repair{900}));
    sim.pilot->hold = true;
    run(sim, 0.1);
    EXPECT_EQ(unit(sim, prospector).task, Unit::Task::repairing);
    sim.pilot->mend = false;
    run(sim, 1);
    EXPECT_EQ(unit(sim, prospector).task, Unit::Task::repairing);
    EXPECT_TRUE(sim.state.structures[ri].hp > half);
    EXPECT_TRUE(!(*sim.state.wells)[static_cast<size_t>(g)].harvester);  // it never went in
}

/// The AI gives the driven Prospector no orders; handed back, it goes to work.
TEST(PilotTests_theAILeavesTheDrivenProspectorAloneAndGetsItBack) {
    auto [sim, prospector, citadel] = driven();
    EXPECT_TRUE(!sim.issue(Command::Gather{prospector, 0}));
    EXPECT_TRUE(!sim.issue(Command::Build{prospector, Structure::Kind::habDome, citadel.position + Vec2(6, 6)}));
    const Commander commander(*sim.map, 0);
    for (const Command& c : commander.orders(sim)) {
        std::optional<int64_t> w;
        if (const auto b = c.as<Command::Build>()) w = b->worker;
        if (const auto b = c.as<Command::Gather>()) w = b->worker;
        if (const auto b = c.as<Command::Harvest>()) w = b->worker;
        if (const auto b = c.as<Command::Repair>()) w = b->worker;
        if (const auto b = c.as<Command::Resume>()) w = b->worker;
        if (w) EXPECT_TRUE(*w != prospector);
    }
    // Left standing, it does nothing by itself.
    const Vec2 at = unit(sim, prospector).position;
    run(sim, 3);
    EXPECT_NEAR(distance(unit(sim, prospector).position, at), 0, 1e-9);
    sim.take(std::nullopt);
    EXPECT_TRUE(!sim.pilot);
    run(sim, 3);
    EXPECT_TRUE(unit(sim, prospector).task != Unit::Task::idle);  // back at work
}

/// A driven unit that dies ends the ride.
TEST(PilotTests_pilotEndsWhenTheUnitDies) {
    auto [sim, prospector, citadel] = driven();
    (void)citadel;
    unitRef(sim, prospector).hp = 0;
    run(sim, 0.1);
    run(sim, 0.1);
    EXPECT_TRUE(!sim.pilot);
}

/// Letting go of the key stops the drill; the load is lost.
TEST(PilotTests_lettingGoStopsTheDrill) {
    auto [sim, prospector, citadel] = driven();
    const int64_t pi_ = nearestPatch(sim, citadel);
    standAt(sim, prospector, pi_, 0.5);
    sim.pilot->act = true;
    sim.pilot->hold = true;
    run(sim, 1);
    EXPECT_EQ(unit(sim, prospector).task, Unit::Task::mining);
    sim.pilot->hold = false;
    run(sim, 0.1);
    EXPECT_EQ(unit(sim, prospector).task, Unit::Task::idle);
    EXPECT_TRUE(!sim.state.patches[static_cast<size_t>(pi_)].miner);
    run(sim, Rules::miningTime);
    EXPECT_EQ(unit(sim, prospector).carrying, int64_t(0));
}

/// A tap (no hold) at a field does not drill.
TEST(PilotTests_aTapDoesNotDrill) {
    auto [sim, prospector, citadel] = driven();
    standAt(sim, prospector, nearestPatch(sim, citadel), 0.5);
    sim.pilot->act = true;
    run(sim, 0.2);
    EXPECT_TRUE(unit(sim, prospector).task != Unit::Task::mining);
}

/// Held at an enemy, the driven Prospector strikes it for 5 each time its
/// cutter is ready.
TEST(PilotTests_drivenProspectorStrikesAnEnemy) {
    auto [sim, prospector, citadel] = driven();
    const Vec2 out = openSide(sim, citadel);
    const Vec2 at = citadel.position + out * (Rules::radius(Structure::Kind::citadel) + 4);
    unitRef(sim, prospector).position = at;
    sim.pilot->heading = std::atan2(out.y, out.x);
    const double gap = Rules::radius(Unit::Kind::prospector) + Rules::stats(Unit::Kind::ranger).radius + 0.05;
    sim.state.units.push_back(Unit(990, Unit::Kind::ranger, 1, at + out * gap, 0, Unit::Task::idle));
    run(sim, 0.05);
    EXPECT_EQ(sim.pilotTarget(unit(sim, prospector)), enemyT(990));
    sim.pilot->hold = true;
    int64_t strikes = 0;
    for (const GameEvent& e : run(sim, 1.2)) {
        if (const auto s = e.as<GameEvent::Shot>(); s && s->target == 990 && s->unit == prospector) strikes += 1;
    }
    EXPECT_EQ(strikes, int64_t(2));  // one strike now, one after the 1.07 s cooldown
    EXPECT_NEAR(unit(sim, 990).hp, Rules::hp(Unit::Kind::ranger) - 2 * 5 * Rules::heroDamage, 1e-9);
}

/// A Prospector of the AI's that is hit turns on an enemy in reach.
TEST(PilotTests_aProspectorFightsBackWhenHit) {
    auto [sim, prospector, citadel] = driven();
    (void)prospector;
    sim.take(std::nullopt);
    const Vec2 out = openSide(sim, citadel);
    const Vec2 at = citadel.position + out * (Rules::radius(Structure::Kind::citadel) + 4);
    // Facing its attacker, which is idle and soon walks off to mine.
    sim.state.units.push_back(Unit(991, Unit::Kind::prospector, 0, at, std::atan2(out.y, out.x), Unit::Task::idle));
    const double gap = Rules::radius(Unit::Kind::prospector) * 2 + 0.05;
    sim.state.units.push_back(Unit(992, Unit::Kind::prospector, 1, at + out * gap, 0, Unit::Task::idle));
    unitRef(sim, 991).hitAt = sim.state.time;
    bool struck = false;
    for (const GameEvent& e : run(sim, 0.3)) {
        if (const auto s = e.as<GameEvent::Shot>(); s && s->unit == 991 && s->target == 992) struck = true;
    }
    EXPECT_TRUE(struck);
    EXPECT_NEAR(unit(sim, 992).hp, Rules::hp(Unit::Kind::prospector) - 5, 1e-9);
}

/// Not hit, it does not go looking for a fight.
TEST(PilotTests_aProspectorLeavesEnemiesAloneUnlessHit) {
    auto [sim, prospector, citadel] = driven();
    (void)prospector;
    sim.take(std::nullopt);
    const Vec2 out = openSide(sim, citadel);
    const Vec2 at = citadel.position + out * (Rules::radius(Structure::Kind::citadel) + 4);
    Unit mine(991, Unit::Kind::prospector, 0, at, 0, Unit::Task::idle);
    mine.hitAt = std::nullopt;
    sim.state.units.push_back(mine);
    const double gap = Rules::radius(Unit::Kind::prospector) * 2 + 0.05;
    sim.state.units.push_back(Unit(992, Unit::Kind::prospector, 1, at + out * gap, 0, Unit::Task::idle));
    for (const GameEvent& e : run(sim, 0.3)) {
        const auto s = e.as<GameEvent::Shot>();
        EXPECT_TRUE(!(s && s->unit == 991));  // struck unprovoked
    }
}

/// Held, the driven Ranger's rifle fires at its weapon's pace at the
/// enemy on its line of fire, not at a nearer one off to the side, and
/// the Ranger keeps facing where the mouse says.
TEST(PilotTests_drivenRangerFiresAtWhatItsSightIsOn) {
    auto [sim, me, at, out] = drivenRanger();
    const Vec2 side(-out.y, out.x);
    sim.state.units.push_back(Unit(990, Unit::Kind::ranger, 1, at + out * 4, 0, Unit::Task::idle));
    sim.state.units.push_back(Unit(991, Unit::Kind::ranger, 1, at + side * 2.5, 0, Unit::Task::idle));
    run(sim, 0.05);
    EXPECT_EQ(sim.pilotTarget(unit(sim, me)), enemyT(990));
    sim.pilot->hold = true;
    EXPECT_TRUE(shots(run(sim, 1.0), me) == (std::vector<int64_t>{990, 990}));  // one now, one after the 0.61 s cooldown
    EXPECT_NEAR(unit(sim, 990).hp, Rules::hp(Unit::Kind::ranger) - 2 * Rules::rangerDamage * Rules::heroDamage, 1e-9);
    EXPECT_NEAR(unit(sim, 991).hp, Rules::hp(Unit::Kind::ranger), 1e-9);
    EXPECT_NEAR(unit(sim, me).heading, std::atan2(out.y, out.x), 1e-9);
}

/// The sight sees past the rifle's range: an enemy there is on it but
/// out of range, one in range is in range, and one shot's damage is
/// what the target loses.
TEST(PilotTests_theSightReadsRangeAndDamage) {
    auto [sim, me, at, out] = drivenRanger();
    sim.state.units.push_back(Unit(990, Unit::Kind::ranger, 1, at + out * 8.5, 0, Unit::Task::idle));
    run(sim, 0.05);
    const auto far = sim.pilotSight(unit(sim, me));
    EXPECT_TRUE(far && far->target == 990);
    EXPECT_TRUE(far && !far->inRange);
    EXPECT_NEAR(far ? far->distance : 0, 8.5 - 2 * Rules::radius(Unit::Kind::ranger), 1e-9);
    EXPECT_NEAR(far ? far->range : 0, Rules::rangerRange * Rules::heroRange, 1e-9);  // the driven rifle reaches farther
    unitRef(sim, 990).position = at + out * 4;
    const auto near = sim.pilotSight(unit(sim, me));
    EXPECT_TRUE(near && near->inRange);
    const auto dmg = sim.strikeDamage(unit(sim, me), 990);
    sim.pilot->hold = true;
    run(sim, 0.05);
    EXPECT_NEAR(unit(sim, 990).hp, Rules::hp(Unit::Kind::ranger) - dmg.value_or(0), 1e-9);
}

/// With the view's crosshair, a gun on foot shoots what the crosshair
/// is on and nothing else: an enemy off its line of sight if the
/// crosshair is on it, none with the crosshair clear (an enemy right on
/// the line or not), none on a friend, none out of range, and the sight
/// reads range to the crosshair's enemy.
TEST(PilotTests_crosshairDecidesWhatADrivenGunHits) {
    auto [sim, me, at, out] = drivenRanger();
    const Vec2 side(-out.y, out.x);
    sim.state.units.push_back(Unit(990, Unit::Kind::ranger, 1, at + out * 4, 0, Unit::Task::idle));
    sim.state.units.push_back(Unit(991, Unit::Kind::ranger, 1, at + out * 3 + side * 2, 0, Unit::Task::idle));
    sim.state.units.push_back(Unit(992, Unit::Kind::ranger, 0, at + out * 2 - side * 2, 0, Unit::Task::idle));
    sim.state.units.push_back(Unit(993, Unit::Kind::ranger, 1, at - side * 9, 0, Unit::Task::idle));
    run(sim, 0.05);
    /// The first trigger pull (the gun is ready) on a copy of the scene:
    /// what it hit, and whether it was spent on nothing.
    struct Pull { std::vector<int64_t> hit; int64_t missed; };
    const auto pull = [&](const Pilot::Crosshair& c) {
        Simulation s = sim;
        s.pilot->crosshair = c;
        s.pilot->hold = true;
        const auto events = run(s, 0.1);
        return Pull{shots(events, me), misses(events, me)};
    };
    const Unit u = unit(sim, me);
    EXPECT_TRUE(sim.pilotSight(u) && sim.pilotSight(u)->target == 990);  // no crosshair: along the look
    sim.pilot->crosshair = Pilot::Crosshair(Pilot::Crosshair::On{991});
    const auto sight = sim.pilotSight(u);
    EXPECT_TRUE(sight && sight->target == 991);
    EXPECT_TRUE(sight && sight->inRange);
    const Vec2 p991 = unit(sim, 991).position;
    EXPECT_NEAR(sight ? sight->distance : 0, distance(u.position, p991) - 2 * Rules::radius(Unit::Kind::ranger), 1e-9);
    EXPECT_TRUE(pull(Pilot::Crosshair::On{991}).hit == (std::vector<int64_t>{991}));
    EXPECT_TRUE(pull(Pilot::Crosshair::Clear{}).hit.empty());  // the enemy on the look line is not hit
    EXPECT_EQ(pull(Pilot::Crosshair::Clear{}).missed, int64_t(1));
    EXPECT_TRUE(pull(Pilot::Crosshair::On{992}).hit.empty());  // a friend under the crosshair is not a target
    sim.pilot->crosshair = Pilot::Crosshair(Pilot::Crosshair::On{993});
    EXPECT_TRUE(sim.pilotSight(u) && !sim.pilotSight(u)->inRange);
    EXPECT_TRUE(pull(Pilot::Crosshair::On{993}).hit.empty());  // out of range: spent short
    EXPECT_EQ(pull(Pilot::Crosshair::On{993}).missed, int64_t(1));
}

/// With nothing under the sight (the one enemy past its range), the
/// rifle still fires, at the same pace, and hits nothing.
TEST(PilotTests_drivenRangerFiresAtNothing) {
    auto [sim, me, at, out] = drivenRanger();
    sim.state.units.push_back(Unit(990, Unit::Kind::ranger, 1, at + out * 8.5, 0, Unit::Task::idle));
    EXPECT_TRUE(!sim.pilotTarget(unit(sim, me)));
    sim.pilot->hold = true;
    const auto events = run(sim, 1.0);
    EXPECT_TRUE(shots(events, me).empty());  // hit something
    EXPECT_EQ(misses(events, me), int64_t(2));
    EXPECT_NEAR(unit(sim, 990).hp, Rules::hp(Unit::Kind::ranger), 1e-9);
}

/// The AI sends the driven Ranger nowhere and leaves it out of its
/// army; handed back, it is the AI's again.
TEST(PilotTests_theAILeavesTheDrivenRangerAlone) {
    auto [sim, me, at, out] = drivenRanger();
    (void)at;
    (void)out;
    EXPECT_TRUE(!sim.issue(Command::Mission{me, std::nullopt}));
    EXPECT_TRUE(!sim.inArmy(unit(sim, me)));
    sim.take(std::nullopt);
    EXPECT_TRUE(sim.inArmy(unit(sim, me)));
    EXPECT_TRUE(sim.issue(Command::Mission{me, std::nullopt}));
}

/// A building put up by hand: paid on the spot, welded while the Prospector
/// stays, left standing when it walks off, and picked up again.
TEST(PilotTests_buildsByHandPausesAndResumes) {
    auto [sim, prospector, citadel] = driven();
    sim.state.players[0].ore = 500;
    const Vec2 out = openSide(sim, citadel);
    const Vec2 stand = citadel.position + out * (Rules::radius(Structure::Kind::citadel) + 2.5);
    unitRef(sim, prospector).position = stand;
    sim.pilot->heading = std::atan2(out.y, out.x);
    const auto spot = sim.snap(Structure::Kind::habDome, stand + out * (Rules::radius(Structure::Kind::habDome) + 1.3));
    ASSERT_TRUE(spot.has_value());
    EXPECT_TRUE(!sim.placementRefusal(Structure::Kind::habDome, *spot));
    EXPECT_TRUE(!sim.pilotBuild(Structure::Kind::habDome, *spot));
    EXPECT_EQ(sim.state.players[0].ore, int64_t(400));
    const Structure habDome = sim.state.structures.back();
    EXPECT_EQ(habDome.kind, Structure::Kind::habDome);
    EXPECT_TRUE(habDome.builder == prospector);
    EXPECT_EQ(unit(sim, prospector).task, Unit::Task::building);
    run(sim, 2);
    const double left = *structureByID(sim, habDome.id)->buildLeft;
    // The driven Prospector welds faster.
    EXPECT_NEAR(left, Rules::buildTime(Structure::Kind::habDome) - 2 * Rules::heroWork, 0.1);
    // Walk off: the work stops and the scaffold waits.
    sim.pilot->heading = std::atan2(-out.y, -out.x);
    run(sim, 0.5, Vec2(1, 0));
    EXPECT_EQ(unit(sim, prospector).task, Unit::Task::idle);
    EXPECT_TRUE(!structureByID(sim, habDome.id)->builder);
    run(sim, 1);
    EXPECT_NEAR(*structureByID(sim, habDome.id)->buildLeft, left, 0.2);
    // Back to it (within reach of its edge): a click carries on.
    unitRef(sim, prospector).position = stand + out * 0.5;
    sim.pilot->heading = std::atan2(out.y, out.x);
    run(sim, 0.05);
    EXPECT_EQ(sim.pilotTarget(unit(sim, prospector)), PilotTarget(PilotTarget::Scaffold{habDome.id}));
    sim.pilot->act = true;
    run(sim, 1);
    EXPECT_EQ(unit(sim, prospector).task, Unit::Task::building);
    EXPECT_TRUE(*structureByID(sim, habDome.id)->buildLeft < left - 0.5);
}

/// What a building cannot be put on: money, tech, the ore, another
/// building, off a well.
TEST(PilotTests_placementRefusals) {
    const auto [sim, prospector, citadel] = driven();
    (void)prospector;
    Simulation rich = sim;
    rich.state.players[0].ore = 1000;
    rich.state.players[0].hydrogen = 1000;
    EXPECT_TRUE(rich.buildRefusal(Structure::Kind::foundry, 0) == std::optional<std::string>("Needs a Garrison"));
    rich.state.players[0].ore = 50;
    EXPECT_TRUE(rich.buildRefusal(Structure::Kind::habDome, 0) == std::optional<std::string>("Not enough ore"));
    const Vec2 patch = sim.state.patches[static_cast<size_t>(nearestPatch(sim, citadel))].position;
    EXPECT_TRUE(sim.placementRefusal(Structure::Kind::habDome, *sim.snap(Structure::Kind::habDome, patch))
                == std::optional<std::string>("Something is in the way"));
    EXPECT_TRUE(sim.placementRefusal(Structure::Kind::garrison,
                                     *sim.snap(Structure::Kind::garrison, citadel.position + Vec2(1, 0)))
                == std::optional<std::string>("Something is in the way"));
    EXPECT_TRUE(sim.placementRefusal(Structure::Kind::derrick, citadel.position)
                == std::optional<std::string>("Must go on an MH well"));
    const Vec2 open = citadel.position + openSide(sim, citadel) * (Rules::radius(Structure::Kind::citadel) + 5);
    EXPECT_TRUE(!sim.placementRefusal(Structure::Kind::habDome, *sim.snap(Structure::Kind::habDome, open)));
}

/// Aegis shield at a Garrison' Lab: paid when ordered, and Rangers
/// (those out and those to come) get 55 hit points.
TEST(PilotTests_aegisShieldGivesRangers55) {
    auto [sim, prospector, citadel] = driven();
    (void)prospector;
    sim.take(std::nullopt);
    const Vec2 out = openSide(sim, citadel);
    const Structure garrison(950, Structure::Kind::garrison, 0, citadel.position + out * 9);
    Structure lab(951, Structure::Kind::lab, 0, garrison.position + Rules::addonOffset);
    lab.parent = 950;
    Structure garrison2 = garrison;
    garrison2.addon = 951;
    sim.state.structures.push_back(garrison2);
    sim.state.structures.push_back(lab);
    sim.state.units.push_back(Unit(952, Unit::Kind::ranger, 0, citadel.position + out * 5, 0, Unit::Task::idle));
    sim.state.players[0].ore = 300;
    sim.state.players[0].hydrogen = 300;
    EXPECT_TRUE(!sim.issue(Command::Research{950, Upgrade::aegisShield}));  // the lab researches, not the Garrison
    EXPECT_TRUE(sim.issue(Command::Research{951, Upgrade::aegisShield}));
    EXPECT_TRUE(!sim.issue(Command::Research{951, Upgrade::aegisShield}));  // one at a time
    EXPECT_EQ(sim.state.players[0].ore, int64_t(200));
    EXPECT_EQ(sim.state.players[0].hydrogen, int64_t(200));
    for (Structure& s : sim.state.structures) {
        if (s.id == 951) s.researchLeft = 0.01;
    }
    bool done = false;
    for (const GameEvent& e : run(sim, 0.1)) {
        if (const auto r = e.as<GameEvent::Researched>(); r && r->upgrade == Upgrade::aegisShield && r->owner == 0) done = true;
    }
    EXPECT_TRUE(done);
    EXPECT_TRUE(sim.has(0, Upgrade::aegisShield));
    EXPECT_NEAR(unit(sim, 952).hp, 55, 1e-9);
    EXPECT_EQ(sim.maxHP(unit(sim, 952)), 55.0);
    EXPECT_TRUE(!sim.issue(Command::Research{951, Upgrade::aegisShield}));  // already done
}

/// The Mini gun at a Garrison' Lab: once done, every Ranger on the
/// side (and in its Bastions) fires it, the driven one too.
TEST(PilotTests_minigunResearchArmsRangers) {
    auto [sim, prospector, citadel] = driven();
    (void)prospector;
    sim.take(std::nullopt);
    const Vec2 out = openSide(sim, citadel);
    Structure garrison(950, Structure::Kind::garrison, 0, citadel.position + out * 9);
    Structure lab(951, Structure::Kind::lab, 0, garrison.position + Rules::addonOffset);
    lab.parent = 950;
    garrison.addon = 951;
    sim.state.structures.push_back(garrison);
    sim.state.structures.push_back(lab);
    sim.state.units.push_back(Unit(952, Unit::Kind::ranger, 0, citadel.position + out * 5, 0, Unit::Task::idle));
    sim.state.players[0].ore = 300;
    sim.state.players[0].hydrogen = 300;
    EXPECT_EQ(sim.weapon(unit(sim, 952)).cooldown, Rules::rangerCooldown);
    EXPECT_TRUE(sim.issue(Command::Research{951, Upgrade::minigun}));
    EXPECT_EQ(sim.state.players[0].ore, int64_t(150));
    EXPECT_EQ(sim.state.players[0].hydrogen, int64_t(150));
    for (Structure& s : sim.state.structures) {
        if (s.id == 951) s.researchLeft = 0.01;
    }
    bool done = false;
    for (const GameEvent& e : run(sim, 0.1)) {
        if (const auto r = e.as<GameEvent::Researched>(); r && r->upgrade == Upgrade::minigun && r->owner == 0) done = true;
    }
    EXPECT_TRUE(done);
    const UnitStats w = sim.weapon(unit(sim, 952));
    EXPECT_EQ(w.damage, UnitStats::minigun.damage);
    EXPECT_EQ(w.burst, 3.0);
    EXPECT_EQ(w.pause, 1.0);
    EXPECT_EQ(sim.maxHP(unit(sim, 952)), 45.0);  // no extra hit points
}

/// Driving, a sound on the unit's right pans right whatever way it faces.
TEST(PilotTests_listenerPansByFacing) {
    Listener l;
    l.position = Vec2(0, 0);
    l.range = 10;
    l.facing = pi / 2;  // facing +Z: its right is −X
    EXPECT_TRUE(l.pan(Vec2(3, 0)) < 0);
    EXPECT_TRUE(l.pan(Vec2(-3, 0)) > 0);
    l.facing = std::nullopt;
    EXPECT_TRUE(l.pan(Vec2(3, 0)) > 0);
}

// Driving the other kinds: one action each, a second or two of sim.

// MARK: Comet

namespace {

/// The handcrafted map for `homeScreens` (Tests' `map()`): the arrangement
/// on the machine the handcrafted map was drawn for.
MapDefinition homeMap() {
    const ScreenConfig homeScreens({
        {"1552-41055-1", "Built-in", 0, 0, 1728, 1117},
        {"19501-29870-2", "Odyssey", -1652, 1117, 5120, 1440},
    });
    const CanvasProjection proj(homeScreens.canvasWidth, homeScreens.canvasHeight);
    return MapLibrary::map(homeScreens, proj);
}

struct CometScene {
    Simulation sim;
    Vec2 start;
    Vec2 dir;
};

/// A Comet alone on the home-screens map, at a spot on the high level
/// half a cell short of a cliff face, facing it, with low ground to land
/// on within `pilotJumpReach` past it.
std::optional<CometScene> cometAtACliff() {
    const MapDefinition m = homeMap();
    GameState state;
    state.players = {Player()};
    state.time = 0;
    state.units = {Unit(1, Unit::Kind::comet, 0, Vec2(0, 0), 0, Unit::Task::idle)};
    state.nextID = 2;
    state.score = {0};
    Simulation sim(state, m);
    const NavGrid nav = *sim.nav;
    const TerrainField field = *sim.field;
    for (int64_t ri = 0;; ++ri) {
        const double r = 4.0 + static_cast<double>(ri) * 0.5;
        if (r > 30) break;
        for (int k = 0; k < 32; ++k) {
            const double a = static_cast<double>(k) / 32 * 2 * pi;
            const Vec2 p = m.bases[0].center + Vec2(std::cos(a), std::sin(a)) * r;
            if (!(field.level(p) == 1 && nav.walkable(p))) continue;
            for (int j = 0; j < 16; ++j) {
                const double b = static_cast<double>(j) / 16 * 2 * pi;
                const Vec2 dir(std::cos(b), std::sin(b));
                Vec2 q = p;
                while (nav.walkable(q) && distance(q, p) < 2) q += dir * 0.05;
                if (!(nav.cliff(q) && distance(q, p) > 0.6)) continue;
                const auto land = sim.landing(q, q + dir * Simulation::pilotJumpReach);
                if (!land || field.level(*land) != 0) continue;
                const Vec2 start = q - dir * 0.5;
                sim.state.units[0].position = start;
                sim.state.units[0].heading = b;
                EXPECT_TRUE(sim.take(1));
                sim.pilot->heading = b;
                return CometScene{sim, start, dir};
            }
        }
    }
    return std::nullopt;
}

} // namespace

/// Walking into a cliff face, the Comet takes off, flies over it and
/// lands on the level below; it does not fire mid-jump, the take-off
/// step included, though the trigger is held and the pistols ready.
TEST(PilotTests_cometJumpsACliffFaceItWalksInto) {
    auto scene = cometAtACliff();
    ASSERT_TRUE(scene.has_value());  // no cliff face found
    Simulation& sim = scene->sim;
    bool jumped = false, landed = false;
    int64_t missedOnFoot = 0;
    for (int k = 0; k < 60; ++k) {
        sim.pilot->walk = Vec2(1, 0);
        sim.pilot->hold = true;
        unitRef(sim, 1).cooldown = 0;
        const auto events = sim.step(1.0 / 30);
        const Unit u = unit(sim, 1);
        const int64_t missed = misses(events, 1);
        if (u.jumpFrom) {
            jumped = true;
            EXPECT_EQ(missed, int64_t(0));  // no firing mid-jump
        } else if (jumped) {
            landed = true;
            break;
        } else {
            missedOnFoot += missed;
        }
    }
    EXPECT_TRUE(missedOnFoot > 0);  // on foot, the held trigger fires
    const Unit u = unit(sim, 1);
    EXPECT_TRUE(jumped);
    EXPECT_TRUE(landed);
    EXPECT_EQ(sim.field->level(u.position), int64_t(0));
    EXPECT_TRUE(sim.nav->walkable(u.position));
}

/// Handed back mid-jump, it lands at once.
TEST(PilotTests_cometHandedBackMidJumpLands) {
    auto scene = cometAtACliff();
    ASSERT_TRUE(scene.has_value());  // no cliff face found
    Simulation& sim = scene->sim;
    for (int k = 0; k < 30; ++k) {
        if (!unit(sim, 1).jumpFrom) run(sim, 1.0 / 30, Vec2(1, 0));
    }
    const auto to = unit(sim, 1).jumpTo;
    ASSERT_TRUE(to.has_value());  // it never took off
    EXPECT_EQ(sim.field->level(*to), int64_t(0));
    EXPECT_TRUE(unit(sim, 1).position != *to);  // still in the air
    sim.take(std::nullopt);
    EXPECT_TRUE(!unit(sim, 1).jumpFrom);
    EXPECT_TRUE(!unit(sim, 1).jumpTo);
    EXPECT_TRUE(unit(sim, 1).position == *to);
}

/// Driven, a Comet out of combat heals at its regen rate.
TEST(PilotTests_drivenCometRegenerates) {
    auto [sim, me, at, out] = drivenUnit(Unit::Kind::comet);
    (void)at;
    (void)out;
    unitRef(sim, me).hp = 20;
    unitRef(sim, me).hitAt = sim.state.time - Rules::cometRegenDelay - 1;
    run(sim, 1);
    EXPECT_NEAR(unit(sim, me).hp, 20 + Rules::cometRegen, 0.1);
}

/// Not driven, a Comet out of combat heals at its regen rate too.
TEST(PilotTests_aiCometRegenerates) {
    auto [sim, prospector, citadel] = driven();
    (void)prospector;
    sim.take(std::nullopt);
    Unit r(990, Unit::Kind::comet, 0, citadel.position + openSide(sim, citadel) * 6, 0, Unit::Task::idle);
    r.hp = 20;
    r.hitAt = sim.state.time - Rules::cometRegenDelay - 1;
    sim.state.units.push_back(r);
    run(sim, 1);
    EXPECT_NEAR(unit(sim, 990).hp, 20 + Rules::cometRegen, 0.1);
}

/// A Comet that regenerates in the same step it is shot ends that step
/// with the regen and the damage both counted once, whichever of the
/// two is stepped first; the hit then stops the regen.
TEST(PilotTests_cometShotWhileRegeneratingKeepsBoth) {
    for (const bool cometFirst : {true, false}) {
        auto [sim, me, at, out] = drivenRanger();
        Unit r(990, Unit::Kind::comet, 1, at + out * 3, 0, Unit::Task::idle);
        r.hp = 20;
        r.hitAt = sim.state.time - Rules::cometRegenDelay - 1;
        if (cometFirst) {
            sim.state.units.insert(sim.state.units.begin(), r);
        } else {
            sim.state.units.push_back(r);
        }
        const double dt = 1.0 / 30;
        sim.step(dt);  // trigger up: the sim takes in the new unit
        unitRef(sim, 990).hp = 20;
        const double dmg = *sim.strikeDamage(unit(sim, me), 990);
        sim.pilot->act = true;
        sim.pilot->hold = true;
        EXPECT_TRUE(shots(sim.step(dt), me) == (std::vector<int64_t>{990}));
        // Stepped first, it regenerated before the shot landed; stepped
        // after, the shot had already stopped its regen.
        const double regen = cometFirst ? Rules::cometRegen * dt : 0;
        EXPECT_NEAR(unit(sim, 990).hp, 20 + regen - dmg, 1e-9);
        EXPECT_NEAR(unit(sim, 990).hitAt.value_or(-1), sim.state.time, 1e-9);
        sim.pilot->hold = false;
        const double hp = unit(sim, 990).hp;
        sim.step(dt);
        EXPECT_NEAR(unit(sim, 990).hp, hp, 1e-9);  // no regen right after a hit
    }
}

// MARK: Firefly, Juggernaut

/// The flame burns the two enemies on its line and not the one beside,
/// once the jet has licked out (`Rules.flight`, 0.09 s).
TEST(PilotTests_fireflyFlameHitsTheLineOnly) {
    auto [sim, me, at, out] = drivenUnit(Unit::Kind::firefly);
    const Vec2 side(-out.y, out.x);
    enemy(sim, 990, at + out * 3);
    enemy(sim, 991, at + out * 4.5);
    enemy(sim, 992, at + out * 3.5 + side * 1.5);
    run(sim, 1.0 / 30);
    EXPECT_EQ(sim.pilotTarget(unit(sim, me)), enemyT(990));
    const double dmg = *sim.strikeDamage(unit(sim, me), 990);
    sim.pilot->hold = true;
    EXPECT_TRUE(shots(run(sim, 1.0 / 30), me) == (std::vector<int64_t>{990}));
    EXPECT_NEAR(unit(sim, 990).hp, Rules::hp(Unit::Kind::ranger), 1e-9);  // not yet
    sim.pilot->hold = false;
    run(sim, 0.1);
    EXPECT_NEAR(unit(sim, 990).hp, Rules::hp(Unit::Kind::ranger) - dmg, 1e-9);
    EXPECT_NEAR(unit(sim, 991).hp, Rules::hp(Unit::Kind::ranger) - dmg, 1e-9);
    EXPECT_NEAR(unit(sim, 992).hp, Rules::hp(Unit::Kind::ranger), 1e-9);
}

/// Wheels do not sidestep: D alone turns a Firefly's hull right at
/// 3 rad/s where it stands. The mouse aims its tail like a turret, at
/// 3.5 rad/s, and the hull never follows it.
TEST(PilotTests_fireflyTurnsOnADInPlace) {
    auto [sim, me, at, out] = drivenUnit(Unit::Kind::firefly);
    (void)out;
    const double h = unit(sim, me).heading;
    sim.pilot->heading = h - 1;
    run(sim, 0.2, Vec2(0, 1));
    // At most 0.7 in 0.2 s (the first tick after taking it does not turn it).
    EXPECT_NEAR(std::remainder(unit(sim, me).look() - h, 2 * pi), -0.65, 0.06);  // the tail on its way
    run(sim, 0.3, Vec2(0, 1));
    EXPECT_NEAR(distance(unit(sim, me).position, at), 0, 1e-9);
    EXPECT_NEAR(std::remainder(unit(sim, me).heading - h, 2 * pi), 1.5, 0.05);
    EXPECT_NEAR(std::remainder(unit(sim, me).look() - h, 2 * pi), -1, 1e-9);  // the tail on the mouse
}

/// The flame goes where the tail points, not the hull: the tail
/// turned 90° burns the enemy beside the Firefly and not the one ahead.
TEST(PilotTests_fireflyFlamesWhereItsTailPoints) {
    auto [sim, me, at, out] = drivenUnit(Unit::Kind::firefly);
    const Vec2 side(-out.y, out.x);
    enemy(sim, 990, at + out * 3);
    enemy(sim, 991, at + side * 3);
    sim.pilot->heading = std::atan2(side.y, side.x);
    run(sim, 0.6);
    EXPECT_NEAR(std::remainder(unit(sim, me).heading - std::atan2(out.y, out.x), 2 * pi), 0, 1e-9);
    EXPECT_EQ(sim.pilotTarget(unit(sim, me)), enemyT(991));
    sim.pilot->hold = true;
    EXPECT_TRUE(shots(run(sim, 1.0 / 30), me) == (std::vector<int64_t>{991}));
}

/// A Juggernaut's grenade slows what it hits, when it comes down.
TEST(PilotTests_juggernautGrenadeSlows) {
    auto [sim, me, at, out] = drivenUnit(Unit::Kind::juggernaut);
    enemy(sim, 990, at + out * 4);
    sim.pilot->hold = true;
    EXPECT_TRUE(shots(run(sim, 1.0 / 30), me) == (std::vector<int64_t>{990}));
    const double lands = sim.state.time + Rules::flight(Unit::Kind::juggernaut, false, 4);
    EXPECT_TRUE(!unit(sim, 990).slowUntil);  // in the air
    sim.pilot->hold = false;
    run(sim, 0.5);
    EXPECT_NEAR(unit(sim, 990).slowUntil.value_or(0), lands + Rules::slowTime, 0.05);
}

// MARK: Longbow

/// R anchors it in `anchorTime`; it cannot fire nor move on the way,
/// and R again packs it up. Anchored, it walks: W drives it and A/D turn
/// it at `Pilot.anchoredPace` of its tank-mode pace.
TEST(PilotTests_tankAnchorsOnRAndCreepsAnchored) {
    auto [sim, me, at, out] = drivenUnit(Unit::Kind::longbow);
    enemy(sim, 990, at + out * 9);
    sim.pilot->ability = true;
    run(sim, 1.0 / 30);
    EXPECT_EQ(sim.pilot->ability, false);  // the press is used up
    EXPECT_TRUE(unit(sim, me).anchored == true);
    sim.pilot->hold = true;
    const auto anchoring = run(sim, Rules::anchorTime - 0.2, Vec2(1, 1));
    EXPECT_TRUE(shots(anchoring, me).empty());    // no shot mid-anchor
    EXPECT_EQ(misses(anchoring, me), int64_t(0));  // no miss mid-anchor
    EXPECT_NEAR(distance(unit(sim, me).position, at), 0, 1e-9);  // no move mid-anchor
    EXPECT_NEAR(std::remainder(unit(sim, me).heading - std::atan2(out.y, out.x), 2 * pi), 0, 1e-9);  // no turn mid-anchor
    run(sim, 0.3);
    EXPECT_TRUE(unit(sim, me).anchor == 1.0);
    const auto ab = sim.pilotAbility(unit(sim, me));
    EXPECT_TRUE(ab && ab->action == PilotAbility::Action(PilotAbility::Action::Unanchor{}));
    sim.pilot->hold = false;
    const Vec2 from = unit(sim, me).position;
    const double facing = unit(sim, me).heading;
    run(sim, 2, Vec2(1, 0));
    const double pace = Rules::speed(Unit::Kind::longbow) * Rules::heroSpeed;
    EXPECT_NEAR(distance(unit(sim, me).position, from), 2 * pace * Pilot::anchoredPace, 0.05);  // anchored, it creeps
    run(sim, 1, Vec2(0, 1));
    EXPECT_NEAR(std::abs(std::remainder(unit(sim, me).heading - facing, 2 * pi)), 2 * Pilot::anchoredPace, 0.02);  // and turns slowly
    EXPECT_TRUE(unit(sim, me).anchor == 1.0);  // walking does not pack it up
    sim.pilot->hold = false;
    sim.pilot->ability = true;
    run(sim, Rules::anchorTime + 0.1);
    EXPECT_TRUE(unit(sim, me).anchor == 0.0);
    run(sim, 0.5, Vec2(1, 0));
    EXPECT_TRUE(distance(unit(sim, me).position, at) > 1);
}

/// The numbers against a Hab Dome (400 hp, 1 armour, armoured):
/// 17 shots in tank mode (15 +10 armoured), 6 anchored (40 +30).
TEST(PilotTests_tankShotsToKillAHabDome) {
    const auto count = [](bool anchored) {
        const double d = Rules::damage(Unit::Kind::longbow, anchored, true, false, 1);
        return static_cast<int64_t>(std::ceil(Rules::hp(Structure::Kind::habDome) / d));
    };
    EXPECT_EQ(count(false), int64_t(17));
    EXPECT_EQ(count(true), int64_t(6));
}

namespace {

/// A anchored tank at `at` facing `out`, driven.
DrivenUnit anchoredTank() {
    DrivenUnit t = drivenUnit(Unit::Kind::longbow);
    unitRef(t.sim, t.me).anchor = 1;
    unitRef(t.sim, t.me).anchored = true;
    return t;
}

} // namespace

/// The driven unit is a hero only while driven: its weapon reaches
/// `heroRange` times as far and hits `heroDamage` times as hard;
/// handed back, it is an ordinary tank again.
TEST(PilotTests_theDrivenUnitIsAHeroOnlyWhileDriven) {
    auto [sim, me, at, out] = anchoredTank();
    enemy(sim, 990, at + out * 8, Unit::Kind::juggernaut);
    run(sim, 1.0 / 30);
    const double base = Rules::damage(Unit::Kind::longbow, true, true, false, 1);
    EXPECT_NEAR(sim.weapon(unit(sim, me)).range, UnitStats::anchor.range * Rules::heroRange, 1e-9);
    EXPECT_NEAR(*sim.strikeDamage(unit(sim, me), 990), base * Rules::heroDamage, 1e-9);
    sim.take(std::nullopt);
    EXPECT_NEAR(sim.weapon(unit(sim, me)).range, UnitStats::anchor.range, 1e-9);
    EXPECT_NEAR(*sim.strikeDamage(unit(sim, me), 990), base, 1e-9);
}

/// The driven Prospector mends a building `heroWork` times as fast as normal
/// (a full repair in its build time).
TEST(PilotTests_theDrivenProspectorRepairsFaster) {
    auto [sim, prospector, citadel] = driven();
    const Vec2 out = openSide(sim, citadel);
    size_t ci = 0;
    while (sim.state.structures[ci].id != citadel.id) ++ci;
    sim.state.structures[ci].hp = 1000;
    unitRef(sim, prospector).position = citadel.position + out * (Rules::radius(Structure::Kind::citadel) + 0.5);
    sim.pilot->heading = std::atan2(-out.y, -out.x);
    run(sim, 0.05);
    EXPECT_EQ(sim.pilotTarget(unit(sim, prospector)), PilotTarget(PilotTarget::Repair{citadel.id}));
    sim.pilot->hold = true;
    run(sim, 2);
    const double perSecond = Rules::hp(Structure::Kind::citadel) / Rules::buildTime(Structure::Kind::citadel) * Rules::heroWork;
    EXPECT_NEAR(sim.state.structures[ci].hp, 1000 + 2 * perSecond, perSecond * 0.1);
}

/// Anchored, the sight has a minimum range of 2: inside it an enemy is
/// not in range, and holding the key does not hit it.
TEST(PilotTests_anchoredSightIsNotInRangeInsideTwo) {
    auto [sim, me, at, out] = anchoredTank();
    const double close = 1.5 + Rules::radius(Unit::Kind::longbow) + Rules::radius(Unit::Kind::ranger);
    enemy(sim, 990, at + out * close);
    run(sim, 1.0 / 30);
    const auto s = sim.pilotSight(unit(sim, me));
    EXPECT_TRUE(s && s->target == 990);
    EXPECT_NEAR(s ? s->distance : 0, 1.5, 1e-9);
    EXPECT_TRUE(s && s->minRange == 2);
    EXPECT_TRUE(s && !s->inRange);
    sim.pilot->hold = true;
    const auto held = run(sim, 0.2);
    EXPECT_TRUE(shots(held, me).empty());
    EXPECT_EQ(misses(held, me), int64_t(0));  // no shell onto the enemy too close to hit
    EXPECT_NEAR(unit(sim, me).cooldown.value_or(0), 0, 1e-9);
    EXPECT_NEAR(unit(sim, 990).hp, Rules::hp(Unit::Kind::ranger), 1e-9);
    unitRef(sim, 990).position = at + out * 6;
    const auto s2 = sim.pilotSight(unit(sim, me));
    EXPECT_TRUE(s2 && s2->inRange);
}

/// A anchored shell does nothing while it flies (0.58 s over 8), then
/// splashes the enemy just behind its target, not one well off to the
/// side. (Juggernauts: the driven tank's shell kills a Ranger outright.)
TEST(PilotTests_anchoredShotSplashes) {
    auto [sim, me, at, out] = anchoredTank();
    const Vec2 side(-out.y, out.x);
    enemy(sim, 990, at + out * 8, Unit::Kind::juggernaut);
    enemy(sim, 991, at + out * 8.96, Unit::Kind::juggernaut);
    enemy(sim, 992, at + out * 8 + side * 3, Unit::Kind::juggernaut);
    run(sim, 1.0 / 30);
    const double dmg = *sim.strikeDamage(unit(sim, me), 990);
    sim.pilot->hold = true;
    EXPECT_TRUE(shots(run(sim, 1.0 / 30), me) == (std::vector<int64_t>{990}));
    sim.pilot->hold = false;
    // Held where they were put (the Juggernauts would walk at the tank).
    const std::array<Vec2, 2> spots{at + out * 8, at + out * 8.96};
    const auto wait = [&](double seconds) {
        std::vector<GameEvent> events;
        const int64_t n = static_cast<int64_t>(rounded(seconds * 30));
        for (int64_t k = 0; k < n; ++k) {
            const auto e = sim.step(1.0 / 30);
            events.insert(events.end(), e.begin(), e.end());
            unitRef(sim, 990).position = spots[0];
            unitRef(sim, 991).position = spots[1];
        }
        return events;
    };
    const auto flying = wait(Rules::flight(Unit::Kind::longbow, true, 8) - 0.1);
    EXPECT_NEAR(unit(sim, 990).hp, Rules::hp(Unit::Kind::juggernaut), 1e-9);  // in the air
    const auto landing = wait(0.15);
    EXPECT_TRUE(std::none_of(flying.begin(), flying.end(), [](const GameEvent& e) { return e.is<GameEvent::Landed>(); }));
    EXPECT_TRUE(std::any_of(landing.begin(), landing.end(), [&](const GameEvent& e) {
        const auto l = e.as<GameEvent::Landed>();
        return l && l->unit == me && l->target == 990;
    }));
    EXPECT_NEAR(unit(sim, 990).hp, Rules::hp(Unit::Kind::juggernaut) - dmg, 1e-9);
    EXPECT_NEAR(unit(sim, 991).hp, Rules::hp(Unit::Kind::juggernaut) - dmg, 1e-9);  // 0.40 from it: full splash
    EXPECT_NEAR(unit(sim, 992).hp, Rules::hp(Unit::Kind::juggernaut), 1e-9);
}

/// The turret traverses toward the mouse at 2.4 rad/s and stops
/// there; the hull never follows it. Anchored, A/D turn the hull at
/// `Pilot.anchoredPace` of its rate; in tank mode at 2 rad/s, the turret
/// holding its aim either way.
TEST(PilotTests_tankTurretTraversesAndTheHullTurnsOnAD) {
    auto [sim, me, at, out] = anchoredTank();
    (void)at;
    const double h = std::atan2(out.y, out.x);
    sim.pilot->heading = h + 2;
    run(sim, 0.5, Vec2(0, 1));
    EXPECT_NEAR(std::remainder(unit(sim, me).look() - h, 2 * pi), 1.2, 0.05);
    const double anchored = h + 0.5 * Pilot::hullTurnRate(Unit::Kind::longbow) * Pilot::anchoredPace;
    EXPECT_NEAR(std::remainder(unit(sim, me).heading - anchored, 2 * pi), 0, 1e-9);
    run(sim, 0.5);
    EXPECT_NEAR(std::remainder(unit(sim, me).look() - h, 2 * pi), 2, 1e-9);
    unitRef(sim, me).anchor = 0;
    unitRef(sim, me).anchored = false;
    run(sim, 0.25, Vec2(0, -1));
    EXPECT_NEAR(std::remainder(unit(sim, me).heading - anchored, 2 * pi), -0.5, 0.05);
    EXPECT_NEAR(std::remainder(unit(sim, me).look() - h, 2 * pi), 2, 1e-9);
}

// MARK: Dropship

namespace {

struct DropshipScene {
    Simulation sim;
    int64_t me;
    int64_t ranger;
};

/// A driven Dropship with 50 energy, and a Ranger of its own right under
/// it with `hp`.
DropshipScene dropshipOverRanger(double hp) {
    DrivenUnit d = drivenUnit(Unit::Kind::dropship);
    unitRef(d.sim, d.me).energy = 50;
    Unit m(997, Unit::Kind::ranger, 0, d.at, 0, Unit::Task::idle);
    m.hp = hp;
    d.sim.state.units.push_back(m);
    return DropshipScene{d.sim, d.me, 997};
}

Mission raid(Vec2 at) { return Mission::Raid{at}; }

} // namespace

/// Held, it heals the hurt Ranger under it and spends a third of an
/// energy point per hit point; let go, the beam stops.
TEST(PilotTests_dropshipHealsTheRangerUnderIt) {
    auto [sim, me, ranger] = dropshipOverRanger(20);
    const auto s = sim.pilotSight(unit(sim, me));
    EXPECT_TRUE(s && s->target == ranger);
    EXPECT_TRUE(s && s->friend_);
    EXPECT_TRUE(s && s->inRange);
    EXPECT_EQ(sim.pilotTarget(unit(sim, me)), PilotTarget(PilotTarget::Heal{ranger}));
    sim.pilot->hold = true;
    run(sim, 1);
    const double healed = unit(sim, ranger).hp - 20;
    EXPECT_NEAR(healed, Rules::healRate, 0.5);
    EXPECT_NEAR(unit(sim, me).energy.value_or(0), 50 - healed / 3 + Rules::energyRegen, 0.01);
    EXPECT_EQ(unit(sim, me).task, Unit::Task::attacking);
    EXPECT_TRUE(unit(sim, me).target == ranger);
    sim.pilot->hold = false;
    run(sim, 1.0 / 30);
    EXPECT_EQ(unit(sim, me).task, Unit::Task::idle);
    EXPECT_TRUE(!unit(sim, me).target);
}

/// A Ranger at full health is left alone: no heal, no energy spent.
TEST(PilotTests_dropshipLeavesAFullHealthRangerAlone) {
    auto [sim, me, ranger] = dropshipOverRanger(Rules::hp(Unit::Kind::ranger));
    const auto s = sim.pilotSight(unit(sim, me));
    EXPECT_TRUE(s && s->target == ranger);
    EXPECT_TRUE(!sim.pilotTarget(unit(sim, me)));
    sim.pilot->hold = true;
    run(sim, 1);
    EXPECT_NEAR(unit(sim, me).energy.value_or(0), 50 + Rules::energyRegen, 0.01);
    EXPECT_EQ(unit(sim, me).task, Unit::Task::idle);
}

/// R loads the Ranger below; it rides along; R again sets it down to
/// raid where the Dropship is. The AI cannot board the driven Dropship.
TEST(PilotTests_dropshipLoadsAndUnloadsOnR) {
    auto [sim, me, ranger] = dropshipOverRanger(Rules::hp(Unit::Kind::ranger));
    EXPECT_TRUE(!sim.issue(Command::Board{me, ranger}));
    sim.pilot->ability = true;
    run(sim, 1.0 / 30);
    EXPECT_TRUE(unit(sim, me).cargo == (std::vector<int64_t>{ranger}));
    EXPECT_EQ(unit(sim, ranger).task, Unit::Task::aboard);
    EXPECT_EQ(sim.slotsUsed(unit(sim, me)), int64_t(1));
    const auto ab = sim.pilotAbility(unit(sim, me));
    EXPECT_TRUE(ab && ab->action == PilotAbility::Action(PilotAbility::Action::Unload{}));
    run(sim, 1, Vec2(1, 0));
    const Vec2 over = unit(sim, me).position;
    EXPECT_TRUE(unit(sim, ranger).position == over);
    sim.pilot->ability = true;
    run(sim, 1.0 / 30);
    EXPECT_TRUE(unit(sim, me).cargo == std::vector<int64_t>{});
    EXPECT_TRUE(unit(sim, ranger).task != Unit::Task::aboard);
    EXPECT_TRUE(unit(sim, ranger).mission == raid(over));
}

/// Handed back with a Ranger aboard, the Dropship sets it down where it
/// is; the AI would keep it aboard for good (it unloads only on drops
/// it plans).
TEST(PilotTests_dropshipHandedBackWithCargoSetsItDown) {
    auto [sim, me, ranger] = dropshipOverRanger(Rules::hp(Unit::Kind::ranger));
    sim.pilot->ability = true;
    run(sim, 1.0 / 30);
    EXPECT_TRUE(unit(sim, me).cargo == (std::vector<int64_t>{ranger}));
    run(sim, 1, Vec2(1, 0));
    const Vec2 over = unit(sim, me).position;
    sim.take(std::nullopt);
    EXPECT_TRUE(unit(sim, me).cargo == std::vector<int64_t>{});
    EXPECT_TRUE(unit(sim, ranger).task != Unit::Task::aboard);
    EXPECT_TRUE(unit(sim, ranger).mission == raid(over));
    EXPECT_TRUE(distance(unit(sim, ranger).position, over) < 1.5);
}

/// R loads only the unit under the sight, not one beside or behind.
TEST(PilotTests_dropshipLoadsOnlyWhatItLooksAt) {
    auto [sim, me, at, out] = drivenUnit(Unit::Kind::dropship);
    sim.state.units.push_back(Unit(996, Unit::Kind::ranger, 0, at - out * 1.2, 0, Unit::Task::idle));
    EXPECT_TRUE(!sim.pilotSight(unit(sim, me)));
    EXPECT_TRUE(sim.pilotAbility(unit(sim, me)) == PilotAbility(std::nullopt, "Load", "Look at a unit to load it"));
    sim.pilot->ability = true;
    run(sim, 1.0 / 30);
    EXPECT_TRUE(unit(sim, me).cargo.value_or(std::vector<int64_t>{}).empty());
    EXPECT_TRUE(unit(sim, 996).task != Unit::Task::aboard);
    // Turned to face it, it loads it.
    sim.pilot->heading = std::atan2(-out.y, -out.x);
    run(sim, 1.0 / 30);
    const auto s = sim.pilotSight(unit(sim, me));
    EXPECT_TRUE(s && s->target == 996);
    sim.pilot->ability = true;
    run(sim, 1.0 / 30);
    EXPECT_TRUE(unit(sim, me).cargo == (std::vector<int64_t>{996}));
}

/// The ability card: titles per mode, and why a Dropship cannot load.
TEST(PilotTests_pilotAbilityTitlesAndWhy) {
    const DrivenUnit tankScene = drivenUnit(Unit::Kind::longbow);
    Unit tank = unit(tankScene.sim, tankScene.me);
    EXPECT_TRUE(tankScene.sim.pilotAbility(tank) == PilotAbility(PilotAbility::Action::Anchor{}, "Anchor Mode"));
    tank.anchored = true;
    EXPECT_TRUE(tankScene.sim.pilotAbility(tank) == PilotAbility(PilotAbility::Action::Unanchor{}, "Tank Mode"));
    const DrivenUnit rifle = drivenRanger();
    EXPECT_TRUE(!rifle.sim.pilotAbility(unit(rifle.sim, rifle.me)));  // a Ranger has no ability
    auto [air, dropship, at, out] = drivenUnit(Unit::Kind::dropship);
    (void)at;
    (void)out;
    EXPECT_TRUE(air.pilotAbility(unit(air, dropship)) == PilotAbility(std::nullopt, "Load", "Look at a unit to load it"));
    // A Ranger on the sight line but past load reach.
    const Vec2 ahead(std::cos(unit(air, dropship).look()), std::sin(unit(air, dropship).look()));
    const Vec2 pos = unit(air, dropship).position;
    air.state.units.push_back(Unit(978, Unit::Kind::ranger, 0, pos + ahead * 3.5, 0, Unit::Task::idle));
    const auto s = air.pilotSight(unit(air, dropship));
    EXPECT_TRUE(s && s->target == 978);
    EXPECT_TRUE(air.pilotAbility(unit(air, dropship)) == PilotAbility(std::nullopt, "Load", "Too far to load"));
    std::erase_if(air.state.units, [](const Unit& u) { return u.id == 978; });
    // Full up with four Juggernauts, a Ranger below: it can only unload.
    const Vec2 here = unit(air, dropship).position;
    for (int64_t k = 0; k < 4; ++k) {
        air.state.units.push_back(Unit(980 + k, Unit::Kind::juggernaut, 0, here, 0, Unit::Task::aboard));
    }
    unitRef(air, dropship).cargo = std::vector<int64_t>{980, 981, 982, 983};
    air.state.units.push_back(Unit(979, Unit::Kind::ranger, 0, here, 0, Unit::Task::idle));
    EXPECT_EQ(air.slotsUsed(unit(air, dropship)), Rules::dropshipSlots);
    EXPECT_TRUE(air.pilotAbility(unit(air, dropship)) == PilotAbility(PilotAbility::Action::Unload{}, "Unload All"));
    unitRef(air, dropship).cargo = std::vector<int64_t>{980, 981, 982};
    const auto ab = air.pilotAbility(unit(air, dropship));
    EXPECT_TRUE(ab && ab->action == PilotAbility::Action(PilotAbility::Action::Load{979}));
}
