// Port of the facts part of Tests/GameCoreTests/TrackingTests.swift
// (docs/leveling.md, "Tracking"): what the sim keeps as the driven unit
// works, in short scenes around one driven unit at blue's main. The
// signature test is in GoldenLevelingTests.cpp; the rows, the store and
// its views (Database, Tracking*) come with the SQLite port.
#include "LevelingScene.h"
#include "Session.h"

using namespace ac;
using namespace leveling;

// MARK: - The facts

/// The driven Ranger's hits count the hp they take off (no overkill),
/// its kill and the building damage; an AI Ranger's count nothing. Hits
/// on it count what its shield took and the hp it lost, and its death.
TEST(Tracking_aDrivenRangerCountsItsDamageItsKillAndWhatItTakes) {
    Scene s = drive(UnitKind::ranger);
    const Unit me = unit(s.sim, s.id);
    const int64_t red = add(s.sim, 990, UnitKind::ranger, s.at + s.out * 4, 1);
    s.sim.hit(red, 9, false, &me);
    s.sim.hit(red, 100, false, &me);
    const Structure base = firstStructure(s.sim, 1, StructureKind::citadel);
    s.sim.hit(base.id, 150, false, &me);
    Tally t = tallyOf(s.sim, UnitKind::ranger);
    EXPECT_NEAR(t.unitDamage, Rules::hp(UnitKind::ranger), 1e-9); // 9, then the 36 left
    EXPECT_EQ(t.kills, int64_t(1));
    EXPECT_NEAR(t.buildingDamage, 150, 1e-9);
    EXPECT_EQ(t.razed, int64_t(0));
    const int64_t blue = add(s.sim, 951, UnitKind::ranger, s.at - s.out * 2);
    const Unit b = unit(s.sim, blue);
    s.sim.hit(base.id, 150, false, &b);
    EXPECT_NEAR(tallyOf(s.sim, UnitKind::ranger).buildingDamage, 150, 1e-9); // an AI Ranger's hit counts nothing
    const int64_t shooter = add(s.sim, 991, UnitKind::ranger, s.at + s.out * 5, 1);
    unitRef(s.sim, s.id).shield = 10;
    const Unit sh = unit(s.sim, shooter);
    s.sim.hit(s.id, 15, false, &sh);
    s.sim.hit(s.id, 100, false, &sh);
    t = tallyOf(s.sim, UnitKind::ranger);
    EXPECT_NEAR(t.shieldAbsorbed, 10, 1e-9);
    EXPECT_NEAR(t.damageTaken, Rules::hp(UnitKind::ranger), 1e-9); // 5, then the 40 left
    EXPECT_EQ(t.deaths, int64_t(0));
    run(s.sim, 1.0 / 30);
    EXPECT_EQ(tallyOf(s.sim, UnitKind::ranger).deaths, int64_t(1));
    EXPECT_TRUE(s.sim.state.players[0].unitsLost == std::optional<int64_t>(1));
    EXPECT_TRUE(s.sim.state.players[1].unitsLost == std::optional<int64_t>(1)); // the red Ranger
}

/// The loads the driven Prospector drops off count, ore and MH apart;
/// once handed back, its loads don't.
TEST(Tracking_aProspectorCountsTheLoadsItDropsOff) {
    auto [sim, prospector, citadel] = driven();
    unitRef(sim, prospector).carrying = 25;
    run(sim, 1.0 / 30);
    unitRef(sim, prospector).carrying = 20;
    unitRef(sim, prospector).hydrogen = true;
    run(sim, 1.0 / 30);
    EXPECT_EQ(tallyOf(sim, UnitKind::prospector).ore, int64_t(25));
    EXPECT_EQ(tallyOf(sim, UnitKind::prospector).hydrogen, int64_t(20));
    sim.take(std::nullopt);
    unitRef(sim, prospector).carrying = 5;
    unitRef(sim, prospector).task = Unit::Task::toBase;
    const int64_t ore = sim.state.ore();
    run(sim, 1);
    EXPECT_EQ(sim.state.ore(), ore + 5); // the AI dropped it off
    EXPECT_EQ(tallyOf(sim, UnitKind::prospector).ore, int64_t(25));
}

/// A building the driven Prospector puts up counts its hp by the share
/// welded; a repair counts the hp mended.
TEST(Tracking_aProspectorCountsWhatItBuildsAndRepairs) {
    auto [sim, prospector, citadel] = driven();
    sim.state.players[0].ore = 500;
    const Vec2 out = openSide(sim, citadel);
    const Vec2 stand = citadel.position + out * (Rules::radius(StructureKind::citadel) + 2.5);
    unitRef(sim, prospector).position = stand;
    setHeading(sim, std::atan2(out.y, out.x));
    const auto spot = sim.snap(StructureKind::habDome, stand + out * (Rules::radius(StructureKind::habDome) + 1.3));
    ASSERT_TRUE(spot.has_value());
    EXPECT_TRUE(!sim.pilotBuild(StructureKind::habDome, *spot).has_value());
    const Structure habDome = sim.state.structures.back();
    run(sim, 2);
    const double welded = Rules::buildTime(StructureKind::habDome) - sim.state.structure(habDome.id)->buildLeft.value_or(0);
    EXPECT_NEAR(welded, 2 * Rules::heroWork, 0.1);
    EXPECT_NEAR(tallyOf(sim, UnitKind::prospector).built,
                Rules::hp(StructureKind::habDome) * welded / Rules::buildTime(StructureKind::habDome), 1e-6);
    EXPECT_EQ(tallyOf(sim, UnitKind::prospector).repaired, 0.0);
    size_t ci = 0;
    for (size_t i = 0; i < sim.state.structures.size(); i++)
        if (sim.state.structures[i].id == citadel.id) ci = i;
    sim.state.structures[ci].hp = 1000;
    unitRef(sim, prospector).task = Unit::Task::idle;
    unitRef(sim, prospector).structure = std::nullopt;
    unitRef(sim, prospector).position = citadel.position + out * (Rules::radius(StructureKind::citadel) + 0.5);
    setHeading(sim, std::atan2(-out.y, -out.x));
    run(sim, 0.05);
    EXPECT_TRUE(sim.pilotTarget(unit(sim, prospector)) == std::optional<PilotTarget>(PilotTarget::Repair{citadel.id}));
    if (sim.pilot) sim.pilot->hold = true;
    run(sim, 1);
    EXPECT_TRUE(sim.state.structures[ci].hp > 1000);
    EXPECT_NEAR(tallyOf(sim, UnitKind::prospector).repaired, sim.state.structures[ci].hp - 1000, 1e-6);
}

/// Seconds count only while a unit of the kind is driven; every take-over
/// counts.
TEST(Tracking_secondsDrivenAndTakeOvers) {
    auto [sim, prospector, citadel] = driven();
    run(sim, 2);
    sim.take(std::nullopt);
    run(sim, 1);
    EXPECT_NEAR(tallyOf(sim, UnitKind::prospector).seconds, 2, 1e-9);
    EXPECT_TRUE(sim.take(prospector));
    run(sim, 0.5);
    EXPECT_NEAR(tallyOf(sim, UnitKind::prospector).seconds, 2.5, 1e-9);
    EXPECT_EQ(tallyOf(sim, UnitKind::prospector).takeovers, int64_t(2));
}

/// A level-up stamps each level crossed with the time, the seconds
/// driven before and every player's standing; a pick stamps its level
/// with the time it was made.
TEST(Tracking_aLevelUpAndAPickAreStamped) {
    auto [sim, prospector, citadel] = driven();
    run(sim, 1);
    const Player before = sim.state.players[0];
    unitRef(sim, prospector).carrying = 200;
    run(sim, 1.0 / 30);
    const double reached = sim.state.time;
    const auto stamps = sim.levels().kinds.at(UnitKind::prospector).stamps;
    ASSERT_TRUE(stamps.size() == 2);
    EXPECT_EQ(stamps[0].level, int64_t(2));
    EXPECT_EQ(stamps[1].level, int64_t(3));
    for (const auto& st : stamps) {
        EXPECT_NEAR(st.at, reached, 1e-9);
        EXPECT_NEAR(st.driven, 1, 1e-9);
        EXPECT_TRUE(!st.picked.has_value());
        ASSERT_TRUE(st.standing.size() == 2);
        EXPECT_EQ(st.standing[0].workers, int64_t(1));
        EXPECT_EQ(st.standing[0].army, int64_t(0));
        EXPECT_EQ(st.standing[0].buildings, int64_t(1));
        EXPECT_EQ(st.standing[0].ore, before.ore); // the load is not in yet
        EXPECT_EQ(st.standing[0].supplyCap, before.supplyCap);
        EXPECT_EQ(st.standing[1].workers, int64_t(1));
    }
    run(sim, 1);
    EXPECT_TRUE(sim.pick(Perk::prospectorLightFrame));
    const double picked = sim.state.time;
    EXPECT_TRUE(sim.levels().kinds.at(UnitKind::prospector).stamps[0].picked == std::optional<double>(picked));
    EXPECT_TRUE(!sim.levels().kinds.at(UnitKind::prospector).stamps[1].picked.has_value());
    add(sim, 950, UnitKind::ranger, unit(sim, prospector).position + Vec2(3, 0));
    EXPECT_EQ(sim.standing(0).army, Rules::cost(UnitKind::ranger) + Rules::hydrogenCost(UnitKind::ranger));
    EXPECT_EQ(sim.standing(0).workers, int64_t(1));
}

/// Burn damage of the driven Firefly counts (in its unit damage too),
/// as do a Pulse mine's hits, a Dropship's healing and its long carries.
TEST(Tracking_burnBlastHealAndCarry) {
    Scene h = drive(UnitKind::firefly);
    const int64_t red = add(h.sim, 990, UnitKind::ranger, h.at + h.out * 3, 1);
    give(h.sim, UnitKind::firefly, {Perk::fireflyBurn});
    h.sim.land(unit(h.sim, h.id), red, *h.sim.target(red));
    const double lit = unit(h.sim, red).hp;
    run(h.sim, 1);
    EXPECT_NEAR(tallyOf(h.sim, UnitKind::firefly).burn, lit - unit(h.sim, red).hp, 1e-9);
    EXPECT_NEAR(tallyOf(h.sim, UnitKind::firefly).burn, 4, 0.2);
    EXPECT_NEAR(tallyOf(h.sim, UnitKind::firefly).unitDamage, Rules::hp(UnitKind::ranger) - unit(h.sim, red).hp, 1e-9);

    Scene r = drive(UnitKind::comet);
    add(r.sim, 990, UnitKind::ranger, r.at + r.out * 4, 1);
    add(r.sim, 991, UnitKind::ranger, r.at + r.out * 4 + r.side() * 1, 1);
    add(r.sim, 992, UnitKind::ranger, r.at + r.out * 4 + r.side() * 3, 1);
    give(r.sim, UnitKind::comet, {Perk::cometPulseMine});
    if (r.sim.pilot) r.sim.pilot->ability = true;
    run(r.sim, 1.0 / 30);
    EXPECT_EQ(tallyOf(r.sim, UnitKind::comet).blastHits, int64_t(2));

    Scene m = drive(UnitKind::dropship);
    const int64_t hurt = add(m.sim, 960, UnitKind::ranger, m.at + m.out * 1);
    unitRef(m.sim, hurt).hp = 20;
    Unit dropship = unit(m.sim, m.id);
    dropship.energy = 100;
    std::vector<GameEvent> events;
    if (m.sim.pilot) m.sim.pilot->hold = true;
    m.sim.pilotAct(dropship, PilotTarget::Heal{hurt}, true, 0.5, events);
    if (m.sim.pilot) m.sim.pilot->hold = false;
    unitRef(m.sim, m.id) = dropship;
    EXPECT_NEAR(unit(m.sim, hurt).hp, 20 + Rules::healRate * 0.5, 1e-9);
    EXPECT_NEAR(tallyOf(m.sim, UnitKind::dropship).healed, Rules::healRate * 0.5, 1e-9);
    const int64_t near = add(m.sim, 961, UnitKind::ranger, m.at + m.side() * 1.5);
    EXPECT_TRUE(m.sim.board(m.id, near));
    EXPECT_TRUE(m.sim.board(m.id, std::nullopt));
    EXPECT_EQ(tallyOf(m.sim, UnitKind::dropship).carried, int64_t(0)); // set down where it boarded
    EXPECT_TRUE(m.sim.board(m.id, near));
    unitRef(m.sim, m.id).position = m.at + m.out * (Leveling::carryDistance + 2);
    EXPECT_TRUE(m.sim.board(m.id, std::nullopt));
    EXPECT_EQ(tallyOf(m.sim, UnitKind::dropship).carried, int64_t(1));
}

/// No one driving: hits, loads and deaths count nothing for anyone.
TEST(Tracking_withNoPilotNothingIsCounted) {
    auto [sim, prospector, citadel] = driven();
    sim.take(std::nullopt);
    const int64_t red = add(sim, 990, UnitKind::ranger, unit(sim, prospector).position + Vec2(3, 0), 1);
    const Unit me = unit(sim, prospector);
    sim.hit(red, 100, false, &me);
    unitRef(sim, prospector).carrying = 5;
    run(sim, 1);
    Tally once;
    once.takeovers = 1;
    EXPECT_TRUE(sim.levels().kinds.at(UnitKind::prospector).tally == once);
    EXPECT_TRUE(sim.state.players[1].unitsLost == std::optional<int64_t>(1)); // units lost count for everyone
}

// MARK: - Saved with the session

/// The tally and the stamps go round the session file; a record from
/// before tracking still decodes.
TEST(Tracking_oldRecordsStillDecode) {
    auto [sim, prospector, citadel] = driven();
    unitRef(sim, prospector).carrying = 70;
    run(sim, 1.0 / 30);
    const auto back = SessionStore::decode<GameState>(SessionStore::encode(sim.state));
    ASSERT_TRUE(back.has_value());
    EXPECT_TRUE(back->players[0].pilot == sim.state.players[0].pilot);
    EXPECT_EQ(back->players[0].pilot->kinds.at(UnitKind::prospector).stamps.size(), size_t(1));
    const auto old = SessionStore::decode<KindRecord>(R"({"xp": 70, "picks": ["prospectorQuickDrill"]})");
    ASSERT_TRUE(old.has_value());
    EXPECT_TRUE(old->tally == Tally());
    EXPECT_TRUE(old->stamps.empty());
    const auto partial = SessionStore::decode<Tally>(R"({"seconds": 12.5, "kills": 3})");
    ASSERT_TRUE(partial.has_value());
    EXPECT_EQ(partial->seconds, 12.5);
    EXPECT_EQ(partial->kills, int64_t(3));
    EXPECT_EQ(partial->ore, int64_t(0));
}
