// Port of the simulation's tests in Tests/GameCoreTests/GameCoreTests.swift
// (`SimulationTests` and `CombatTests`): mining, training, building,
// walking, a short duel. Each from a hand-built position, never a game.
// (The routes, the mirror, the session store and the projection tests of
// that file belong to Router, MapLibrary, Session and Projection.)
#include "SimHelpers.h"

#include <map>

using namespace ac;
using namespace simtest;

// MARK: - SimulationTests

TEST(simulation_prospectorMinesAndDepositsFivePerTrip) {
    Simulation sim(blueOnly(GameState::new_(homeMap())));
    EXPECT_EQ(sim.state.ore(), 50);
    std::vector<int64_t> deposits;
    for (int k = 0; k < 60 * 30; k++) {
        for (const auto& e : sim.step(1.0 / 30)) {
            if (auto d = e.as<GameEvent::Deposited>()) deposits.push_back(d->amount);
        }
    }
    EXPECT_TRUE(!deposits.empty());
    EXPECT_TRUE(std::all_of(deposits.begin(), deposits.end(), [](int64_t n) { return n == 5; }));
    EXPECT_EQ(sim.state.ore(), 50 + 5 * static_cast<int64_t>(deposits.size()));
    // With 6 s of drilling, a trip to a close patch takes about 8 s:
    // roughly 30-40 ore a minute, and most of the time is drilling.
    EXPECT_TRUE(sim.state.totalMined() > 20);
    EXPECT_TRUE(sim.state.totalMined() < 50);
    int64_t mined = 0;
    for (const auto& p : sim.state.patches) mined += p.initial - p.remaining;
    EXPECT_EQ(mined, sim.state.totalMined() + sim.state.units[0].carrying);
}

TEST(simulation_depletedPatchSendsWorkerToAnother) {
    GameState state = blueOnly(GameState::new_(homeMap()));
    Simulation sim(state);
    for (int k = 0; k < 10 * 30; k++) sim.step(1.0 / 30);
    ASSERT_TRUE(sim.state.units[0].patch.has_value());
    const int64_t first = *sim.state.units[0].patch;
    sim.state.patches[size_t(first)].remaining = 5;
    for (int k = 0; k < 30 * 30; k++) sim.step(1.0 / 30);
    EXPECT_EQ(sim.state.patches[size_t(first)].remaining, 0);
    EXPECT_TRUE(sim.state.units[0].patch != first);
    state = sim.state;
    EXPECT_EQ(state.patches[size_t(first)].stage(), 0);
}

TEST(simulation_trainingCostsSupplyAndTakesTwelveSeconds) {
    Simulation sim(blueOnly(GameState::new_(homeMap())), homeMap());
    const int64_t citadel = sim.state.structures[0].id;
    EXPECT_TRUE(sim.issue(Command::Train{citadel, std::nullopt}));
    EXPECT_EQ(sim.state.ore(), 0);
    EXPECT_EQ(sim.state.supplyUsed(), 2);
    // Not without money.
    EXPECT_TRUE(!sim.issue(Command::Train{citadel, std::nullopt}));
    for (int k = 0; k < 11 * 30; k++) sim.step(1.0 / 30);
    EXPECT_EQ(sim.state.units.size(), size_t(1));
    bool trained = false;
    for (int k = 0; k < 2 * 30; k++) {
        for (const auto& e : sim.step(1.0 / 30)) if (e.is<GameEvent::Trained>()) trained = true;
    }
    EXPECT_TRUE(trained);
    EXPECT_EQ(sim.state.units.size(), size_t(2));
    EXPECT_EQ(sim.state.supplyUsed(), 2);
}

TEST(simulation_queueHoldsFivePaidUpFrontAndTrainsThemInTurn) {
    GameState state = blueOnly(GameState::new_(homeMap()));
    state.ore() = 1000;
    Simulation sim(state, homeMap());
    const int64_t citadel = sim.state.structures[0].id;
    for (int64_t k = 0; k < Rules::maxQueue; k++) EXPECT_TRUE(sim.issue(Command::Train{citadel, std::nullopt}));
    EXPECT_TRUE(!sim.issue(Command::Train{citadel, std::nullopt})); // the queue holds five
    EXPECT_EQ(sim.state.ore(), 1000 - 5 * Rules::prospectorCost);
    EXPECT_EQ(sim.state.supplyUsed(), 1 + 5);
    EXPECT_EQ(sim.state.structures[0].queueCount(), 5);
    for (int64_t k = 0; k < static_cast<int64_t>(Rules::prospectorTrainTime) * 30 + 5; k++) sim.step(1.0 / 30);
    EXPECT_EQ(sim.state.units.size(), size_t(2));
    EXPECT_EQ(sim.state.structures[0].queueCount(), 4);
    ASSERT_TRUE(sim.state.structures[0].trainingProgress().has_value());
    EXPECT_TRUE(*sim.state.structures[0].trainingProgress() < 0.05); // the next one starts at once
    for (int64_t k = 0; k < 4 * static_cast<int64_t>(Rules::prospectorTrainTime) * 30; k++) sim.step(1.0 / 30);
    EXPECT_EQ(sim.state.units.size(), size_t(6));
    EXPECT_EQ(sim.state.structures[0].queueCount(), 0);
    EXPECT_TRUE(!sim.state.structures[0].line.has_value());
    EXPECT_EQ(sim.state.supplyUsed(), 6);
}

TEST(simulation_garrisonNeedsAHabDomeAndRangersRallyInsteadOfMining) {
    const MapDefinition& m = homeMap();
    GameState state = blueOnly(GameState::new_(m));
    state.ore() = 1000;
    const Structure citadel = state.structures[0];
    const auto spotOpt = Commander(m).garrisonSpot(state, citadel);
    ASSERT_TRUE(spotOpt.has_value());
    const Vec2 spot = *spotOpt;
    Simulation sim(state, m);
    const int64_t prospector = sim.state.units[0].id;
    EXPECT_TRUE(!sim.issue(Command::Build{prospector, Structure::Kind::garrison, spot})); // a Hab Dome comes first
    EXPECT_EQ(sim.state.ore(), 1000);

    // With a finished Hab Dome it may.
    state.structures.push_back(Structure(state.nextID, Structure::Kind::habDome, 0, citadel.position + Vec2(0, -7)));
    state.nextID += 1;
    sim = Simulation(state, m);
    EXPECT_TRUE(sim.issue(Command::Build{prospector, Structure::Kind::garrison, spot}));

    // A finished Garrison trains Rangers.
    const int64_t garrison = state.nextID;
    state.structures.push_back(Structure(garrison, Structure::Kind::garrison, 0, spot));
    state.nextID += 1;
    sim = Simulation(state, m);
    EXPECT_TRUE(sim.issue(Command::Train{garrison, std::nullopt}));
    EXPECT_EQ(sim.state.ore(), 1000 - Rules::rangerCost);
    EXPECT_EQ(sim.state.supplyUsed(), 2);
    auto hasRanger = [&] {
        return std::any_of(sim.state.units.begin(), sim.state.units.end(), [](const Unit& u) { return u.kind == Unit::Kind::ranger; });
    };
    for (int k = 0; k < 17 * 30; k++) sim.step(1.0 / 30);
    EXPECT_TRUE(!hasRanger()); // 18 s to train
    // The moment it appears, it stands at the Garrison door.
    int steps = 0;
    while (!hasRanger() && steps < 2 * 30) { sim.step(1.0 / 30); steps += 1; }
    ASSERT_TRUE(hasRanger());
    const Unit ranger = *std::find_if(sim.state.units.begin(), sim.state.units.end(),
                                      [](const Unit& u) { return u.kind == Unit::Kind::ranger; });
    EXPECT_TRUE(distance(ranger.position, spot) < 3); // it walks out of the Garrison door
    EXPECT_TRUE(!sim.issue(Command::Gather{ranger.id, 0}));
    EXPECT_TRUE(!sim.issue(Command::Build{ranger.id, Structure::Kind::habDome, spot}));

    for (int k = 0; k < 40 * 30; k++) sim.step(1.0 / 30);
    const auto now = sim.state.unit(ranger.id);
    ASSERT_TRUE(now.has_value());
    EXPECT_EQ(now->task, Unit::Task::idle);
    EXPECT_TRUE(!now->patch.has_value());
    EXPECT_TRUE(distance(now->position, sim.rallySlots[0][0]) < 0.3);
    EXPECT_TRUE(distance(now->position, citadel.position) > 6); // out in front of the base
}

TEST(simulation_unitsWalkAroundBuildingsAndStepOutOfNewOnes) {
    const MapDefinition& m = homeMap();
    GameState state = blueOnly(GameState::new_(m));
    const Structure citadel = state.structures[0];
    // A Garrison right across the Prospector's way to a spot behind it.
    const auto spotOpt = Commander(m).garrisonSpot(state, citadel);
    ASSERT_TRUE(spotOpt.has_value());
    const Vec2 spot = *spotOpt;
    const Vec2 dir = normalize(spot - citadel.position);
    const Vec2 target = spot + dir * 3.5;
    Unit prospector = state.units[0];
    prospector.position = spot - dir * 3.2;
    prospector.task = Unit::Task::idle;
    state.units[0] = prospector;
    state.structures.push_back(Structure(state.nextID, Structure::Kind::garrison, 0, spot));
    state.nextID += 1;
    Simulation sim(state, m);
    const auto path = sim.nav->path(prospector.position, target, 0.1);
    ASSERT_TRUE(path.has_value());
    double length_ = 0.0;
    Vec2 at = prospector.position;
    for (const Vec2 p : *path) { length_ += distance(at, p); at = p; }
    EXPECT_TRUE(length_ > distance(prospector.position, target) + 0.5); // goes around
    Unit u = sim.state.units[0];
    int steps = 0;
    while (!sim.travel(u, target, 0.1, 1.0 / 30) && steps < 30 * 20) {
        EXPECT_TRUE(!NavGrid::solid(u.position, sim.state)); // never inside the Garrison
        steps += 1;
    }
    EXPECT_TRUE(distance(u.position, target) < 0.2);

    // A building going up on top of a unit pushes it out.
    state.units[0].position = citadel.position + Vec2(0, -6);
    state.structures.push_back(Structure(state.nextID, Structure::Kind::habDome, 0, citadel.position + Vec2(0, -6), 21.0,
                                         std::nullopt));
    sim = Simulation(state, m);
    EXPECT_TRUE(!NavGrid::solid(sim.state.units[0].position, sim.state));
    EXPECT_TRUE(distance(sim.state.units[0].position, citadel.position + Vec2(0, -6)) < 2.5);
}

TEST(simulation_onlyOneProspectorDrillsAPatchAtATime) {
    GameState state = blueOnly(GameState::new_(homeMap()));
    Unit second = state.units[0];
    second.id = state.nextID; state.nextID += 1;
    state.units.push_back(second);
    Simulation sim(state, homeMap());
    sim.workersPerPatch = 8; // no bouncing: both want the same patch
    int64_t worst = 0;
    for (int k = 0; k < 120 * 30; k++) {
        sim.step(1.0 / 30);
        std::map<int64_t, int64_t> drilling;
        for (const auto& u : sim.state.units) if (u.task == Unit::Task::mining) drilling[*u.patch] += 1;
        for (const auto& [_, n] : drilling) worst = std::max(worst, n);
    }
    EXPECT_EQ(worst, 1);
    EXPECT_TRUE(sim.state.totalMined() > 50);
}

TEST(simulation_dryPatchRegrowsFiveMinutesLaterAndOnlyWhenDry) {
    GameState state = blueOnly(GameState::new_(homeMap()));
    state.units.clear();
    state.patches[0].remaining = 0;
    state.patches[1].remaining = 1;
    Simulation sim(state, homeMap());
    std::vector<int64_t> regrown;
    for (int k = 0; k < 299 * 10; k++) {
        for (const auto& e : sim.step(0.1)) if (auto r = e.as<GameEvent::PatchRegrown>()) regrown.push_back(r->patch);
    }
    EXPECT_TRUE(regrown.empty());
    EXPECT_EQ(sim.state.patches[0].remaining, 0);
    for (int k = 0; k < 2 * 10; k++) {
        for (const auto& e : sim.step(0.1)) if (auto r = e.as<GameEvent::PatchRegrown>()) regrown.push_back(r->patch);
    }
    EXPECT_TRUE(regrown == std::vector<int64_t>{0});
    EXPECT_EQ(sim.state.patches[0].remaining, sim.state.patches[0].initial);
    EXPECT_EQ(sim.state.patches[1].remaining, 1);
}

// MARK: - CombatTests

namespace {

/// Two mirrored blobs of `n` Rangers, 14 cells apart on open ground (no
/// map). The left blob is `left`'s, and its units come first in the unit
/// list. `attacker`: the player that attack-moves while the other holds
/// a defend point (nil: both attack). Returns survivors per player.
std::vector<int64_t> duel(int64_t n, int64_t left = 0, std::optional<int64_t> attacker = std::nullopt) {
    std::vector<Unit> units;
    int64_t id = 10;
    for (const double side : {-1.0, 1.0}) {
        const int64_t p = side < 0 ? left : 1 - left;
        for (int64_t k = 0; k < n; k++) {
            const double a = static_cast<double>(k) * 2.39996, r = 0.62 * std::sqrt(static_cast<double>(k));
            units.push_back(Unit(id, Unit::Kind::ranger, p, Vec2(7 * side, 0) + Vec2(-side * std::cos(a), std::sin(a)) * r, 0,
                                 Unit::Task::idle));
            id += 1;
        }
    }
    std::vector<Structure> structures;
    for (int64_t p = 0; p < 2; p++) {
        structures.push_back(Structure(id, Structure::Kind::habDome, p, Vec2(0, p == 0 ? -40 : 40)));
        id += 1;
    }
    Simulation sim(bare(2, 0, structures, units, id));
    for (int64_t p = 0; p < 2; p++) {
        const Vec2 home(p == left ? -7 : 7, 0);
        if (!attacker || *attacker == p) sim.issue(Command::Attack{p, -home});
        else sim.issue(Command::Defend{p, home, 1});
    }
    for (int k = 0; k < 60 * 30; k++) sim.step(1.0 / 30);
    std::vector<int64_t> out;
    for (int64_t p = 0; p < 2; p++) {
        out.push_back(std::count_if(sim.state.units.begin(), sim.state.units.end(), [&](const Unit& u) { return u.owner == p; }));
    }
    return out;
}

} // namespace

/// Swapping which player owns which blob (and whose units are stepped
/// first) only swaps the result: no player has an edge in a fight.
TEST(combat_fightsFavourNoPlayer) {
    for (const std::optional<int64_t> attacker : {std::optional<int64_t>{}, std::optional<int64_t>{0}}) {
        const auto a = duel(20, 0, attacker);
        const auto b = duel(20, 1, attacker ? std::optional<int64_t>(1 - *attacker) : std::nullopt);
        std::printf("  duel attacker %s: blue left [%lld, %lld], red left [%lld, %lld]\n",
                    attacker ? std::to_string(*attacker).c_str() : "both", static_cast<long long>(a[0]),
                    static_cast<long long>(a[1]), static_cast<long long>(b[0]), static_cast<long long>(b[1]));
        EXPECT_TRUE(a == (std::vector<int64_t>{b[1], b[0]}));
    }
    // Mirrored equal armies trading head on: nearly wiped out both.
    const auto even = duel(20);
    EXPECT_TRUE(std::abs(even[0] - even[1]) <= 4);
}

TEST(combat_rangersKillARangerInEightShotsAndBuildingsFall) {
    GameState state = bare(2, 0,
                           {Structure(1, Structure::Kind::habDome, 1, Vec2(10, 0)), Structure(2, Structure::Kind::garrison, 0, Vec2(-30, 0))},
                           {Unit(3, Unit::Kind::ranger, 0, Vec2(0, 0), 0, Unit::Task::idle),
                            Unit(4, Unit::Kind::prospector, 1, Vec2(4, 0), 0, Unit::Task::idle)},
                           5);
    state.units[1].hp = Rules::hp(Unit::Kind::prospector);
    Simulation sim(state);
    sim.issue(Command::Attack{0, Vec2(10, 0)});
    int64_t shots_ = 0;
    std::optional<double> died, destroyed;
    std::optional<int64_t> won;
    for (int k = 0; k < 120 * 30; k++) {
        for (const auto& e : sim.step(1.0 / 30)) {
            if (e.is<GameEvent::Shot>()) shots_ += 1;
            else if (e.is<GameEvent::Died>()) died = sim.state.time;
            else if (e.is<GameEvent::Destroyed>()) destroyed = sim.state.time;
            else if (auto v = e.as<GameEvent::Victory>()) won = v->winner;
        }
    }
    // 45 hp at 6 a shot: 8 shots, 0.61 s apart.
    EXPECT_NEAR(died.value_or(0), 7 * Rules::rangerCooldown, 0.6);
    // Then the Hab Dome: 400 hp at 5 (1 armour) a shot.
    EXPECT_EQ(shots_, 8 + 80);
    EXPECT_TRUE(destroyed.has_value());
    EXPECT_TRUE(won == 0);
    EXPECT_TRUE(sim.state.score == (std::vector<int64_t>{1, 0}));
    EXPECT_TRUE(sim.state.endedAt.has_value());
}

/// A Ranger on a plateau outranges one below by a cell: from 5.5 cells
/// (edge to edge) the high one fires and the low one cannot.
TEST(combat_highGroundGivesACellMoreRange) {
    const MapDefinition& m = homeMap();
    const TerrainField field(m);
    const NavGrid nav(m, field);
    const double gap = 5.5, apart = gap + 2 * Rules::unitRadius;
    const auto pair = cliffPair(m, field, nav, apart, [](Vec2, Vec2) { return true; });
    ASSERT_TRUE(pair.has_value());
    const auto [high, low] = *pair;
    Simulation sim(bare(2, 0, {},
                        {Unit(1, Unit::Kind::ranger, 0, high, 0, Unit::Task::idle), Unit(2, Unit::Kind::ranger, 1, low, 0, Unit::Task::idle)},
                        3),
                   m);
    EXPECT_EQ(sim.range(high, low), Rules::rangerRange + Rules::highGroundRangeBonus);
    EXPECT_EQ(sim.range(low, high), Rules::rangerRange);
    EXPECT_EQ(sim.range(low, low + Vec2(1, 0)), Rules::rangerRange);
    std::optional<int64_t> first;
    for (int k = 0; k < 30; k++) {
        for (const auto& e : sim.step(1.0 / 30)) {
            if (auto s = e.as<GameEvent::Shot>(); s && !first) first = s->unit;
        }
    }
    EXPECT_TRUE(first == 1); // the Ranger above shoots first
}

/// Three Prospectors on a Derrick take turns inside, bring back 4 MH a
/// trip, and the well loses what the player gains.
TEST(combat_prospectorsTakeHydrogenFromADerrickOneAtATime) {
    GameState state = blueOnly(GameState::new_(homeMap()));
    const Structure citadel = *std::find_if(state.structures.begin(), state.structures.end(), [](const Structure& s) {
        return s.kind == Structure::Kind::citadel && s.owner == 0;
    });
    size_t wellAt = 0;
    for (size_t k = 1; k < state.wells->size(); k++) {
        if (distance((*state.wells)[k].position, citadel.position) < distance((*state.wells)[wellAt].position, citadel.position)) wellAt = k;
    }
    state.structures.push_back(Structure(900, Structure::Kind::derrick, 0, (*state.wells)[wellAt].position));
    const int64_t before = (*state.wells)[wellAt].remaining;
    Simulation sim(state);
    std::vector<int64_t> prospectors;
    for (const auto& u : sim.state.units) {
        if (u.kind == Unit::Kind::prospector && u.owner == 0 && prospectors.size() < 3) prospectors.push_back(u.id);
    }
    for (const int64_t id : prospectors) EXPECT_TRUE(sim.issue(Command::Harvest{id, 900}));
    int64_t most = 0;
    for (int k = 0; k < 60 * 30; k++) {
        sim.step(1.0 / 30);
        most = std::max<int64_t>(most, std::count_if(sim.state.units.begin(), sim.state.units.end(),
                                                     [](const Unit& u) { return u.task == Unit::Task::inDerrick; }));
    }
    EXPECT_EQ(most, 1); // one Prospector inside at a time
    EXPECT_TRUE(sim.state.hydrogen() > 20);
    EXPECT_EQ(sim.state.hydrogen() % Rules::hydrogenCarry, 0);
    int64_t carried = 0;
    for (const auto& u : sim.state.units) if (u.hydrogen == true) carried += u.carrying;
    EXPECT_EQ(before - (*sim.state.wells)[wellAt].remaining, sim.state.hydrogen() + carried);
}

/// A Comet on a cliff face with its next point under 0.2 cells away
/// (the range of steps to check is empty) lands on that point.
TEST(combat_leapToAPointRightAheadDoesNotTrap) {
    const MapDefinition& m = homeMap();
    const NavGrid nav(m, TerrainField(m));
    std::optional<Vec2> face;
    const int64_t nx = strideCount(m.bounds.minX, m.bounds.maxX, 0.25), nz = strideCount(m.bounds.minZ, m.bounds.maxZ, 0.25);
    for (int64_t i = 0; i < nx && !face; i++) {
        const double x = m.bounds.minX + static_cast<double>(i) * 0.25;
        for (int64_t j = 0; j < nz; j++) {
            const double z = m.bounds.minZ + static_cast<double>(j) * 0.25;
            if (nav.cliff(Vec2(x, z)) && !nav.walkable(Vec2(x, z))) { face = Vec2(x, z); break; }
        }
    }
    ASSERT_TRUE(face.has_value());
    Simulation sim(bare(1, 0, {}, {Unit(1, Unit::Kind::comet, 0, *face, 0, Unit::Task::idle)}, 2), m);
    Unit u = sim.state.units[0];
    sim.leap(u, *face, *face + Vec2(0.1, 0));
    EXPECT_TRUE(u.jumpTo == *face + Vec2(0.1, 0));
}

/// A Comet jumps down a cliff where a Ranger has to walk round by the
/// ramp.
TEST(combat_cometJumpsACliffARangerWalksAround) {
    const MapDefinition& m = homeMap();
    const TerrainField field(m);
    const NavGrid nav(m, field);
    const auto pair = cliffPair(m, field, nav, 4, [&](Vec2 p, Vec2 q) { return nav.clear(p, q, true); });
    ASSERT_TRUE(pair.has_value());
    const auto [high, low] = *pair;
    struct Trip { double seconds; bool jumped; };
    auto trip = [&](Unit::Kind kind) -> Trip {
        Simulation sim(bare(1, 0, {}, {Unit(1, kind, 0, high, 0, Unit::Task::idle)}, 2), m);
        Unit u = sim.state.units[0];
        bool jumped = false;
        for (int n = 1; n <= 120 * 30; n++) {
            if (sim.travel(u, low, 0.2, 1.0 / 30)) return Trip{static_cast<double>(n) / 30, jumped};
            jumped = jumped || u.jumpFrom.has_value();
        }
        return Trip{INFINITY, jumped};
    };
    const Trip comet = trip(Unit::Kind::comet), ranger = trip(Unit::Kind::ranger);
    EXPECT_TRUE(comet.jumped);
    EXPECT_TRUE(!ranger.jumped);
    EXPECT_TRUE(comet.seconds < 2.5);                  // straight down the cliff
    EXPECT_TRUE(ranger.seconds > 2 * comet.seconds);   // round by the ramp
}

TEST(combat_bastionHoldsFourShootsFartherAndSpillsThemWhenItFalls) {
    const Structure bastion(1, Structure::Kind::bastion, 0, Vec2::zero);
    std::vector<Unit> units;
    for (int64_t k = 0; k < 5; k++) {
        units.push_back(Unit(10 + k, Unit::Kind::ranger, 0, Vec2(-4, static_cast<double>(k) - 2), 0, Unit::Task::idle));
    }
    // An enemy 6.8 cells from the Bastion's edge: beyond a Ranger's 5,
    // inside the Bastion's 6 + its own radius... just out of reach at first.
    units.push_back(Unit(20, Unit::Kind::ranger, 1, Vec2(1.5 + 0.375 + 5.9, 0), pi, Unit::Task::idle));
    Simulation sim(bare(2, 0, {bastion, Structure(2, Structure::Kind::habDome, 1, Vec2(40, 0))}, units, 30));
    for (int64_t u = 10; u < 15; u++) sim.issue(Command::Load{u, 1});
    sim.issue(Command::Defend{1, Vec2(7.775, 0), 1});
    for (int k = 0; k < 8 * 30; k++) sim.step(1.0 / 30);
    ASSERT_TRUE(sim.state.structure(1).has_value());
    EXPECT_EQ(sim.state.structure(1)->crew.value_or(std::vector<int64_t>{}).size(), size_t(4)); // four fit
    EXPECT_EQ(std::count_if(sim.state.units.begin(), sim.state.units.end(), [](const Unit& u) { return u.task == Unit::Task::inBastion; }),
              4);
    EXPECT_TRUE(!sim.state.unit(20).has_value()); // the Bastion outranges a Ranger standing at 5.9
    // Hidden inside: an enemy cannot target them, only the Bastion.
    sim.state.structures[0].hp = 1;
    sim.state.units.push_back(Unit(21, Unit::Kind::ranger, 1, Vec2(4, 0), pi, Unit::Task::idle));
    bool spilled = false;
    for (int k = 0; k < 3 * 30; k++) {
        for (const auto& e : sim.step(1.0 / 30)) {
            if (auto d = e.as<GameEvent::Destroyed>(); d && d->structure == 1) spilled = true;
        }
        if (spilled) break;
    }
    EXPECT_TRUE(spilled);
    EXPECT_EQ(std::count_if(sim.state.units.begin(), sim.state.units.end(),
                            [](const Unit& u) { return u.owner == 0 && u.kind == Unit::Kind::ranger; }),
              5); // all climb out alive
    EXPECT_TRUE(std::all_of(sim.state.units.begin(), sim.state.units.end(), [](const Unit& u) { return u.task != Unit::Task::inBastion; }));
}
