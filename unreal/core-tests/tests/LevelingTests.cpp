// Port of Tests/GameCoreTests/LevelingTests.swift: pilot leveling
// (docs/leveling.md), short scenes around one driven Prospector at blue's
// main, the record set by hand where the XP does not matter. The curve and
// catalogue tests are in GoldenLevelingTests.cpp.
#include "LevelingScene.h"
#include "Session.h"
#ifndef JSON_NOEXCEPTION
#define JSON_NOEXCEPTION 1
#endif
#include "json.hpp"

using namespace ac;
using namespace leveling;

namespace {

/// Move the driven Prospector out to the Citadel's open side, facing out;
/// returns where it stands and that way.
std::pair<Vec2, Vec2> outside(Simulation& sim, int64_t prospector, const Structure& citadel) {
    const Vec2 out = openSide(sim, citadel);
    const Vec2 at = citadel.position + out * (Rules::radius(StructureKind::citadel) + 4);
    unitRef(sim, prospector).position = at;
    setHeading(sim, std::atan2(out.y, out.x));
    return {at, out};
}

std::vector<GameEvent> leveledUps(const std::vector<GameEvent>& events) {
    std::vector<GameEvent> out;
    for (const auto& e : events)
        if (e.is<GameEvent::LeveledUp>()) out.push_back(e);
    return out;
}

} // namespace

/// 1 XP an ore and 1.25 an MH dropped off by the driven Prospector; a
/// Prospector the AI runs earns nothing.
TEST(Leveling_aDropOffEarnsItsXP) {
    auto [sim, prospector, citadel] = driven();
    unitRef(sim, prospector).carrying = 25;
    run(sim, 1.0 / 30);
    EXPECT_EQ(unit(sim, prospector).carrying, int64_t(0)); // dropped off
    EXPECT_EQ(sim.levels().xp(UnitKind::prospector), 25.0);
    unitRef(sim, prospector).carrying = 20;
    unitRef(sim, prospector).hydrogen = true;
    run(sim, 1.0 / 30);
    EXPECT_EQ(sim.levels().xp(UnitKind::prospector), 50.0);
    sim.take(std::nullopt);
    unitRef(sim, prospector).carrying = 5;
    unitRef(sim, prospector).task = Unit::Task::toBase;
    const int64_t ore = sim.state.ore();
    run(sim, 1);
    EXPECT_EQ(sim.state.ore(), ore + 5); // the AI dropped it off
    EXPECT_EQ(sim.levels().xp(UnitKind::prospector), 50.0);
}

/// Each level crossed raises its event; unpicked offers wait, one per
/// level, lowest first.
TEST(Leveling_levelUpsRaiseEventsAndOffersStack) {
    auto [sim, prospector, citadel] = driven();
    EXPECT_EQ(sim.levels().level(UnitKind::prospector), int64_t(1));
    EXPECT_TRUE((sim.levels().driven() == std::vector<UnitKind>{UnitKind::prospector})); // taking it over records the kind
    unitRef(sim, prospector).carrying = 200;
    const auto ups = leveledUps(run(sim, 1.0 / 30));
    EXPECT_TRUE((ups == std::vector<GameEvent>{GameEvent::LeveledUp{UnitKind::prospector, 2},
                                               GameEvent::LeveledUp{UnitKind::prospector, 3}}));
    auto levelsOf = [&] {
        std::vector<int64_t> out;
        for (const auto& o : sim.levels().pending(UnitKind::prospector)) out.push_back(o.level);
        return out;
    };
    EXPECT_TRUE((levelsOf() == std::vector<int64_t>{2, 3}));
    EXPECT_TRUE((sim.levels().pending(UnitKind::prospector).front().perks() ==
                 std::vector<Perk>{Perk::prospectorQuickDrill, Perk::prospectorLightFrame}));
    unitRef(sim, prospector).carrying = 400;
    run(sim, 1.0 / 30);
    EXPECT_EQ(sim.levels().level(UnitKind::prospector), int64_t(5));
    EXPECT_TRUE((levelsOf() == std::vector<int64_t>{2, 3, 4, 5}));
}

/// A pick must be one of the two at the kind's lowest pending level.
TEST(Leveling_aPickIsTakenOnlyFromTheCurrentOffer) {
    auto [sim, prospector, citadel] = driven();
    EXPECT_TRUE(!sim.pick(Perk::prospectorQuickDrill)); // nothing on offer at level 1
    give(sim, UnitKind::prospector, 3, {});
    EXPECT_TRUE(sim.levels().refusal(Perk::prospectorRigMaster) == std::optional<std::string>("Not on offer"));
    EXPECT_TRUE(!sim.pick(Perk::prospectorRigMaster)); // level 3's, while level 2's waits
    EXPECT_TRUE(!sim.pick(Perk::rangerDrillSergeant)); // no Ranger levels
    EXPECT_TRUE(!sim.issue(Command::Pick{1, Perk::prospectorQuickDrill})); // only the human levels
    EXPECT_TRUE(sim.pick(Perk::prospectorQuickDrill));
    EXPECT_TRUE(!sim.pick(Perk::prospectorLightFrame)); // level 2 is picked
    EXPECT_TRUE(sim.issue(Command::Pick{Pilot::player, Perk::prospectorPlating}));
    EXPECT_TRUE((sim.levels().picks(UnitKind::prospector) ==
                 std::vector<Perk>{Perk::prospectorQuickDrill, Perk::prospectorPlating}));
    EXPECT_TRUE(sim.levels().refusal(Perk::prospectorBigHaul) == std::optional<std::string>("No pick waiting"));
    EXPECT_TRUE(sim.levels().has(Perk::prospectorPlating));
    EXPECT_TRUE(sim.active(Perk::prospectorPlating));
    sim.take(std::nullopt);
    EXPECT_TRUE(!sim.active(Perk::prospectorPlating)); // picked, but no Prospector is driven
}

/// With no one driving, nothing changes; driven with no picks, the
/// Prospector is level 1's hero and no one else changes.
TEST(Leveling_noPilotChangesNothingAndLevelOneIsTheHero) {
    auto [sim, prospector, citadel] = driven();
    const int64_t other = add(sim, 950, UnitKind::prospector, citadel.position + openSide(sim, citadel) * 4);
    EXPECT_EQ(sim.rate(Stat::Speed{}, unit(sim, prospector)), Rules::heroSpeed);
    EXPECT_EQ(sim.load(unit(sim, prospector), false), Rules::heroCarry);
    EXPECT_EQ(sim.load(unit(sim, prospector), true), Rules::heroHydrogenCarry);
    EXPECT_EQ(sim.rate(Stat::Build{}, unit(sim, prospector)), Rules::heroWork);
    EXPECT_TRUE(sim.boost(Stat::Speed{}, unit(sim, other)) == Boost::none);
    give(sim, UnitKind::prospector,
         {Perk::prospectorQuickDrill, Perk::prospectorRigMaster, Perk::prospectorBigHaul, Perk::prospectorFastWelder,
          Perk::prospectorForeman, Perk::prospectorCuttingTorch, Perk::prospectorOvertime, Perk::prospectorSupplyChief,
          Perk::prospectorUnionCrew});
    sim.take(std::nullopt);
    for (int64_t id : {prospector, other}) {
        for (const Stat& stat : std::vector<Stat>{Stat::Speed{}, Stat::Drill{}, Stat::Hydrogen{}, Stat::OreCarry{}, Stat::Build{},
                                                 Stat::Repair{}, Stat::Hp{}, Stat::Damage{}, Stat::Range{}})
            EXPECT_TRUE(sim.boost(stat, unit(sim, id)) == Boost::none);
        EXPECT_EQ(sim.load(unit(sim, id), false), Rules::carry);
        EXPECT_EQ(sim.maxHP(unit(sim, id)), Rules::hp(UnitKind::prospector));
    }
    EXPECT_EQ(sim.habDomeSupply(0), Rules::supply(StructureKind::habDome));
}

/// Plating, Cutting torch and Fast welder work on the driven Prospector,
/// and only while it is driven: handed back, it is a plain Prospector again.
TEST(Leveling_drivenPicksChangeTheirStatOnlyWhileDriven) {
    auto [sim, prospector, citadel] = driven();
    const Vec2 out = openSide(sim, citadel);
    const Vec2 at = citadel.position + out * (Rules::radius(StructureKind::citadel) + 4);
    unitRef(sim, prospector).position = at;
    setHeading(sim, std::atan2(out.y, out.x));
    add(sim, 990, UnitKind::ranger,
        at + out * (Rules::radius(UnitKind::prospector) + Rules::radius(UnitKind::ranger) + 0.05), 1);
    give(sim, UnitKind::prospector, 7,
         {Perk::prospectorQuickDrill, Perk::prospectorPlating, Perk::prospectorFastWelder, Perk::prospectorGroupRepair,
          Perk::prospectorForeman, Perk::prospectorCuttingTorch});
    run(sim, 1.0 / 30);
    EXPECT_EQ(sim.maxHP(unit(sim, prospector)), Rules::hp(UnitKind::prospector) + 20);
    EXPECT_EQ(unit(sim, prospector).hp, Rules::hp(UnitKind::prospector) + 20); // the hp comes with the pick
    EXPECT_EQ(sim.target(prospector) ? sim.target(prospector)->armor : 0.0, 1.0);
    EXPECT_EQ(sim.rate(Stat::Build{}, unit(sim, prospector)), 3.0);
    EXPECT_EQ(sim.rate(Stat::Repair{}, unit(sim, prospector)), 3.0);
    EXPECT_NEAR(sim.strikeDamage(unit(sim, prospector), 990).value_or(-1), 5 * Rules::heroDamage * 2, 1e-9);
    sim.take(std::nullopt);
    run(sim, 1.0 / 30);
    EXPECT_EQ(sim.maxHP(unit(sim, prospector)), Rules::hp(UnitKind::prospector));
    EXPECT_EQ(unit(sim, prospector).hp, Rules::hp(UnitKind::prospector)); // and goes with it
    EXPECT_EQ(sim.target(prospector) ? sim.target(prospector)->armor : -1.0, 0.0);
    EXPECT_EQ(sim.rate(Stat::Build{}, unit(sim, prospector)), 1.0);
    EXPECT_NEAR(sim.strikeDamage(unit(sim, prospector), 990).value_or(-1), 5, 1e-9);
}

/// Team picks reach the Prospectors nearby (within 6 cells) or every
/// Prospector, and only while the player drives a Prospector. Speed-ups
/// add up; for a load, the biggest counts.
TEST(Leveling_teamPicksReachNearbyOrEveryProspectorOnlyWhileDrivingAProspector) {
    auto [sim, prospector, citadel] = driven();
    const Vec2 me = unit(sim, prospector).position, out = openSide(sim, citadel);
    const int64_t near = add(sim, 950, UnitKind::prospector, me + out * 3);
    const int64_t far = add(sim, 951, UnitKind::prospector, me + out * 20);
    give(sim, UnitKind::prospector,
         {Perk::prospectorQuickDrill, Perk::prospectorRigMaster, Perk::prospectorBigHaul, Perk::prospectorFieldWelder,
          Perk::prospectorForeman, Perk::prospectorRichVein, Perk::prospectorOvertime, Perk::prospectorCliffHop,
          Perk::prospectorUnionCrew});
    EXPECT_NEAR(sim.rate(Stat::Drill{}, unit(sim, prospector)), 1.7, 1e-12); // 10% + 50% + 10%
    EXPECT_NEAR(sim.rate(Stat::Drill{}, unit(sim, near)), 1.7, 1e-12);
    EXPECT_NEAR(sim.rate(Stat::Drill{}, unit(sim, far)), 1.1, 1e-12); // only Overtime reaches every Prospector
    EXPECT_NEAR(sim.rate(Stat::Hydrogen{}, unit(sim, far)), 1.2, 1e-12);
    EXPECT_EQ(sim.rate(Stat::Speed{}, unit(sim, near)), 1.0); // Light frame not picked
    EXPECT_EQ(sim.load(unit(sim, prospector), false), int64_t(35)); // the driven Prospector keeps its 35
    EXPECT_EQ(sim.load(unit(sim, prospector), true), int64_t(28));
    EXPECT_EQ(sim.load(unit(sim, near), false), int64_t(10)); // Union crew's 10, not 7 + 5
    EXPECT_EQ(sim.load(unit(sim, near), true), int64_t(8));
    EXPECT_EQ(sim.load(unit(sim, far), false), Rules::carry);
    // Driving a Ranger, the Prospector picks rest.
    add(sim, 998, UnitKind::ranger, me + out * 2);
    EXPECT_TRUE(sim.take(998));
    EXPECT_EQ(sim.rate(Stat::Drill{}, unit(sim, near)), 1.0);
    EXPECT_EQ(sim.rate(Stat::Drill{}, unit(sim, far)), 1.0);
    EXPECT_EQ(sim.load(unit(sim, near), false), Rules::carry);
    EXPECT_EQ(sim.levels().level(UnitKind::prospector), int64_t(10)); // the Prospector level stays where it was
    EXPECT_TRUE((sim.levels().driven() == std::vector<UnitKind>{UnitKind::prospector, UnitKind::ranger}));
}

/// An AI Prospector within 6 cells of the driven one drills faster with
/// Quick drill: its loads come sooner.
TEST(Leveling_aNearbyAIProspectorDrillsFaster) {
    auto [sim, prospector, citadel] = driven();
    give(sim, UnitKind::prospector, 2, {Perk::prospectorQuickDrill});
    const auto [me, out] = outside(sim, prospector, citadel);
    size_t patchIndex = 0;
    for (size_t i = 1; i < sim.state.patches.size(); i++)
        if (distance(sim.state.patches[i].position, me) < distance(sim.state.patches[patchIndex].position, me)) patchIndex = i;
    Unit u = sim.freshUnit(UnitKind::prospector, 950, 0, me + out * 5, 0);
    u.task = Unit::Task::mining;
    u.patch = static_cast<int64_t>(patchIndex);
    u.timer = Rules::miningTime;
    sim.state.units.push_back(u);
    sim.state.patches[patchIndex].miner = 950;
    run(sim, Rules::miningTime / 1.1 - 0.2);
    EXPECT_EQ(unit(sim, 950).carrying, int64_t(0)); // not done yet
    run(sim, 0.3);
    EXPECT_EQ(unit(sim, 950).carrying, Rules::carry); // done in 1/1.1 of the time
}

/// Rich vein: every third ore trip of the driven Prospector carries
/// double.
TEST(Leveling_richVeinDoublesEveryThirdTrip) {
    auto [sim, prospector, citadel] = driven();
    give(sim, UnitKind::prospector, 7,
         {Perk::prospectorQuickDrill, Perk::prospectorRigMaster, Perk::prospectorBigHaul, Perk::prospectorFieldWelder,
          Perk::prospectorForeman, Perk::prospectorRichVein});
    size_t patchIndex = 0;
    for (size_t i = 1; i < sim.state.patches.size(); i++)
        if (distance(sim.state.patches[i].position, citadel.position) < distance(sim.state.patches[patchIndex].position, citadel.position))
            patchIndex = i;
    const auto p = sim.state.patches[patchIndex];
    Vec2 n(-std::sin(p.angle), std::cos(p.angle));
    if (dot(n, citadel.position - p.position) < 0) n = -n;
    unitRef(sim, prospector).position = p.position + n * 1.0;
    setHeading(sim, std::atan2(-n.y, -n.x));
    if (sim.pilot) sim.pilot->hold = true;
    std::vector<int64_t> loads;
    for (int k = 0; k < 3; k++) {
        run(sim, Rules::miningTime / 1.6 + 0.2);
        loads.push_back(unit(sim, prospector).carrying);
        unitRef(sim, prospector).carrying = 0;
    }
    EXPECT_TRUE((loads == std::vector<int64_t>{35, 35, 70}));
}

/// The level and the picks belong to the pilot: they go with it onto
/// another Prospector and leave the one it got out of.
TEST(Leveling_theLevelFollowsThePilotOntoAnotherProspector) {
    auto [sim, prospector, citadel] = driven();
    unitRef(sim, prospector).carrying = 200;
    run(sim, 1.0 / 30);
    EXPECT_TRUE(sim.pick(Perk::prospectorQuickDrill));
    EXPECT_TRUE(sim.pick(Perk::prospectorPlating));
    run(sim, 1.0 / 30);
    EXPECT_EQ(sim.maxHP(unit(sim, prospector)), Rules::hp(UnitKind::prospector) + 20);
    const int64_t other = add(sim, 950, UnitKind::prospector, citadel.position + openSide(sim, citadel) * 5);
    EXPECT_TRUE(sim.take(other));
    run(sim, 1.0 / 30);
    EXPECT_EQ(sim.levels().level(UnitKind::prospector), int64_t(3));
    EXPECT_TRUE(sim.active(Perk::prospectorPlating));
    EXPECT_EQ(sim.maxHP(unit(sim, other)), Rules::hp(UnitKind::prospector) + 20);
    EXPECT_EQ(unit(sim, other).hp, Rules::hp(UnitKind::prospector) + 20);
    EXPECT_EQ(sim.maxHP(unit(sim, prospector)), Rules::hp(UnitKind::prospector)); // the one it got out of is plain
    EXPECT_EQ(unit(sim, prospector).hp, Rules::hp(UnitKind::prospector));
}

/// A new game, after a win or from the session, starts every kind at
/// level 1.
TEST(Leveling_aNewGameStartsAtLevelOne) {
    auto [sim, prospector, citadel] = driven();
    give(sim, UnitKind::prospector, 4, {Perk::prospectorQuickDrill});
    sim.newGame();
    EXPECT_TRUE(sim.levels().kinds.empty());
    EXPECT_EQ(sim.levels().level(UnitKind::prospector), int64_t(1));
    const MapDefinition map = smallMap();
    GameState state = GameState::new_(map);
    state.players[0].pilot = sim.levels();
    state.players[0].pilot->kinds[UnitKind::prospector] = KindRecord();
    Session session;
    session.id = "t";
    session.signature = "t";
    session.created = Date::now();
    session.mapName = map.name;
    session.mapVersion = map.version;
    session.state = state;
    session.restart(map);
    EXPECT_TRUE(!session.state.players[0].pilot.has_value());
}

/// The record is saved with the session, and a session saved before
/// leveling (no record) still loads.
TEST(Leveling_theRecordIsSavedAndOldSessionsStillDecode) {
    auto [sim, prospector, citadel] = driven();
    give(sim, UnitKind::prospector, 3, {Perk::prospectorLightFrame});
    run(sim, 1.0 / 30);
    // As the session store writes it (unseen sites are at -∞).
    const std::string data = SessionStore::encode(sim.state);
    const auto back = SessionStore::decode<GameState>(data);
    ASSERT_TRUE(back.has_value());
    EXPECT_TRUE(back->players[0].pilot == sim.state.players[0].pilot);
    // An old session: no record anywhere.
    auto json = nlohmann::json::parse(data, nullptr, false);
    ASSERT_TRUE(json.is_object());
    for (auto& p : json["players"]) p.erase("pilot");
    const auto old = SessionStore::decode<GameState>(json.dump());
    ASSERT_TRUE(old.has_value());
    EXPECT_TRUE(!old->players[0].pilot.has_value());
    // A record from a later build: missing fields read as defaults, an
    // unknown pick ends the list.
    const auto k = SessionStore::decode<KindRecord>(R"({"xp": 70, "picks": ["prospectorQuickDrill", "gone"]})");
    ASSERT_TRUE(k.has_value());
    EXPECT_EQ(k->xp, 70.0);
    EXPECT_TRUE((k->picks == std::vector<Perk>{Perk::prospectorQuickDrill}));
    EXPECT_EQ(k->count, int64_t(0));
}

/// Damage dealt by the driven unit earns the target's worth times the
/// share of its hp taken (a kill's worth is its cost); a building's
/// half that. No one else's hits count.
TEST(Leveling_damageEarnsTheTargetsWorthTimesTheShareTaken) {
    auto [sim, prospector, citadel] = driven();
    const Unit me = unit(sim, prospector);
    const int64_t red = add(sim, 990, UnitKind::ranger, citadel.position + openSide(sim, citadel) * 6, 1);
    sim.hit(red, 9, false, &me);
    EXPECT_NEAR(sim.levels().xp(UnitKind::prospector), 50.0 * 9 / 45, 1e-9);
    sim.hit(red, 100, false, &me);
    EXPECT_NEAR(sim.levels().xp(UnitKind::prospector), 50, 1e-9); // the kill's worth is its cost, no more
    const Structure base = firstStructure(sim, 1, StructureKind::citadel);
    sim.hit(base.id, 150, false, &me);
    EXPECT_NEAR(sim.levels().xp(UnitKind::prospector), 50 + 400 * 0.1 * 0.5, 1e-9);
    const int64_t blue = add(sim, 951, UnitKind::ranger, citadel.position + openSide(sim, citadel) * 7);
    const Unit b = unit(sim, blue);
    sim.hit(base.id, 150, false, &b);
    EXPECT_NEAR(sim.levels().xp(UnitKind::prospector), 70, 1e-9); // an AI Ranger's hit earns nothing
}

/// Supply chief makes the team's Hab Domes give 10; Prefab takes 10% off
/// what the driven Prospector places. Both only while it drives.
TEST(Leveling_supplyChiefAndPrefab) {
    auto [sim, prospector, citadel] = driven();
    sim.state.structures.push_back(Structure(900, StructureKind::habDome, 0, citadel.position + openSide(sim, citadel) * 8));
    give(sim, UnitKind::prospector, 9,
         {Perk::prospectorQuickDrill, Perk::prospectorRigMaster, Perk::prospectorBigHaul, Perk::prospectorFieldWelder,
          Perk::prospectorForeman, Perk::prospectorRichVein, Perk::prospectorPrefab, Perk::prospectorSupplyChief});
    run(sim, 1.0 / 30);
    EXPECT_EQ(sim.state.supplyCap(), Rules::supply(StructureKind::citadel) + 10);
    EXPECT_TRUE(isPrice(sim.buildCost(StructureKind::garrison, 0), 135, 0));
    EXPECT_TRUE(isPrice(sim.buildCost(StructureKind::foundry, 0), 135, 90));
    EXPECT_TRUE(isPrice(sim.buildCost(StructureKind::garrison, 1), 150, 0)); // not red's
    sim.take(std::nullopt);
    run(sim, 1.0 / 30);
    EXPECT_EQ(sim.state.supplyCap(), Rules::supply(StructureKind::citadel) + Rules::supply(StructureKind::habDome));
    EXPECT_TRUE(isPrice(sim.buildCost(StructureKind::garrison, 0), 150, 0));
}

/// Field welder: the driven Prospector mends a machine of its side, a
/// full repair in half the machine's training time, and earns XP for it.
TEST(Leveling_fieldWelderMendsAMachine) {
    auto [sim, prospector, citadel] = driven();
    const Vec2 out = openSide(sim, citadel);
    const Vec2 at = citadel.position + out * (Rules::radius(StructureKind::citadel) + 4);
    unitRef(sim, prospector).position = at;
    setHeading(sim, std::atan2(out.y, out.x));
    const int64_t firefly = add(sim, 960, UnitKind::firefly,
                                at + out * (Rules::radius(UnitKind::prospector) + Rules::radius(UnitKind::firefly) + 0.3));
    unitRef(sim, firefly).hp = 40;
    run(sim, 1.0 / 30);
    EXPECT_TRUE(!sim.pilotTarget(unit(sim, prospector)).has_value()); // no pick, no repair
    give(sim, UnitKind::prospector, 5,
         {Perk::prospectorQuickDrill, Perk::prospectorRigMaster, Perk::prospectorBigHaul, Perk::prospectorFieldWelder});
    EXPECT_TRUE(sim.pilotTarget(unit(sim, prospector)) == std::optional<PilotTarget>(PilotTarget::Repair{firefly}));
    if (sim.pilot) sim.pilot->hold = true;
    run(sim, 1);
    const double perSecond = Rules::hp(UnitKind::firefly) / (Rules::trainTime(UnitKind::firefly) * 0.5);
    EXPECT_NEAR(unit(sim, firefly).hp, 40 + perSecond, perSecond * 0.1);
    const double mended = unit(sim, firefly).hp - 40;
    EXPECT_NEAR(sim.levels().xp(UnitKind::prospector) - Leveling::xp(5), 100 * mended / 90 * 0.5, 1e-6);
}

/// Group repair: friendly machines within 6 cells of the driven Prospector
/// mend 1 hp a second; farther ones and bio do not.
TEST(Leveling_groupRepairMendsMachinesAround) {
    auto [sim, prospector, citadel] = driven();
    const auto [me, out] = outside(sim, prospector, citadel);
    const int64_t near = add(sim, 960, UnitKind::firefly, me + out * 4);
    const int64_t far = add(sim, 961, UnitKind::firefly, me + out * 12);
    const int64_t bio = add(sim, 962, UnitKind::ranger, me + out * 3);
    for (int64_t id : {near, far, bio}) unitRef(sim, id).hp = 30;
    give(sim, UnitKind::prospector, 5,
         {Perk::prospectorQuickDrill, Perk::prospectorRigMaster, Perk::prospectorBigHaul, Perk::prospectorGroupRepair});
    run(sim, 2);
    EXPECT_NEAR(unit(sim, near).hp, 32, 0.05);
    EXPECT_EQ(unit(sim, far).hp, 30.0);
    EXPECT_EQ(unit(sim, bio).hp, 30.0);
}

/// Shield: it fills over 10 s, takes hits first, and starts refilling
/// 7 s after the last hit; it goes when the pilot gets out.
TEST(Leveling_theShieldTakesHitsFirstAndRefills) {
    auto [sim, prospector, citadel] = driven();
    give(sim, UnitKind::prospector,
         {Perk::prospectorQuickDrill, Perk::prospectorRigMaster, Perk::prospectorBigHaul, Perk::prospectorFieldWelder,
          Perk::prospectorForeman, Perk::prospectorRichVein, Perk::prospectorOvertime, Perk::prospectorCliffHop,
          Perk::prospectorShield});
    run(sim, 10.1);
    EXPECT_NEAR(unit(sim, prospector).shield.value_or(0), 20, 1e-9);
    sim.hit(prospector, 15);
    EXPECT_NEAR(unit(sim, prospector).shield.value_or(0), 5, 1e-9);
    EXPECT_EQ(unit(sim, prospector).hp, Rules::hp(UnitKind::prospector));
    run(sim, 6);
    EXPECT_NEAR(unit(sim, prospector).shield.value_or(0), 5, 1e-9); // no refill inside 7 s
    run(sim, 2);
    EXPECT_NEAR(unit(sim, prospector).shield.value_or(0), 7, 0.1); // 2 a second after that
    sim.hit(prospector, 10);
    EXPECT_NEAR(unit(sim, prospector).hp, Rules::hp(UnitKind::prospector) - 3, 0.1); // what the shield cannot take
    sim.take(std::nullopt);
    EXPECT_TRUE(!unit(sim, prospector).shield.has_value());
}

TEST(Leveling_cliffHopLetsTheDrivenProspectorJump) {
    auto [sim, prospector, citadel] = driven();
    EXPECT_TRUE(!sim.pilotJumps(unit(sim, prospector)));
    give(sim, UnitKind::prospector, 9,
         {Perk::prospectorQuickDrill, Perk::prospectorRigMaster, Perk::prospectorBigHaul, Perk::prospectorFieldWelder,
          Perk::prospectorForeman, Perk::prospectorRichVein, Perk::prospectorOvertime, Perk::prospectorCliffHop});
    EXPECT_TRUE(sim.pilotJumps(unit(sim, prospector)));
    sim.take(std::nullopt);
    EXPECT_TRUE(!sim.pilotJumps(unit(sim, prospector)));
}

/// The ability key's burst (tried on Surge): faster moving and firing
/// for its seconds, for hp, then ready again after its cooldown.
TEST(Leveling_theBurstBlock) {
    auto [sim, prospector, citadel] = driven();
    add(sim, 998, UnitKind::ranger, citadel.position + openSide(sim, citadel) * 6);
    EXPECT_TRUE(sim.take(998));
    sim.trialEffects[Perk::rangerSurge] = {Effect::Burst{Burst{8, 15, 0.5, 0.5, 10}}};
    EXPECT_TRUE(!sim.pilotAbility(unit(sim, 998)).has_value()); // no burst picked
    // Only Surge: the other Ranger picks would change the numbers.
    give(sim, UnitKind::ranger, 5, {Perk::rangerSurge});
    EXPECT_TRUE(sim.pilotAbility(unit(sim, 998)) ==
                std::optional<PilotAbility>(PilotAbility(PilotAbility::Action::Burst{}, "Surge")));
    if (sim.pilot) sim.pilot->ability = true;
    run(sim, 1.0 / 30);
    EXPECT_EQ(unit(sim, 998).hp, Rules::hp(UnitKind::ranger) - 10);
    EXPECT_NEAR(sim.rate(Stat::Speed{}, unit(sim, 998)), Rules::heroSpeed * 1.5, 1e-12);
    EXPECT_NEAR(sim.weapon(unit(sim, 998)).cooldown, Rules::rangerCooldown / 1.5, 1e-12);
    EXPECT_TRUE(!sim.pilotAbility(unit(sim, 998))->action.has_value()); // on already
    run(sim, 8.1);
    EXPECT_NEAR(sim.rate(Stat::Speed{}, unit(sim, 998)), Rules::heroSpeed, 1e-12);
    EXPECT_TRUE(sim.pilotAbility(unit(sim, 998))->why == std::optional<std::string>("Ready in 7 s"));
    run(sim, 7);
    EXPECT_TRUE(sim.pilotAbility(unit(sim, 998))->action ==
                std::optional<PilotAbility::Action>(PilotAbility::Action::Burst{}));
}

/// The other blocks, tried on Ranger picks (`trialEffects`): a mending
/// aura for bio, damage against a class with armour ignored, the team's
/// training and unit cost.
TEST(Leveling_theOtherBuildingBlocks) {
    auto [sim, prospector, citadel] = driven();
    const auto [me, out] = outside(sim, prospector, citadel);
    add(sim, 998, UnitKind::ranger, me + out * 2);
    const int64_t hurt = add(sim, 962, UnitKind::ranger, me + out * 4);
    unitRef(sim, hurt).hp = 30;
    const int64_t red = add(sim, 990, UnitKind::juggernaut, me + out * 7, 1);
    EXPECT_TRUE(sim.take(998));
    sim.trialEffects[Perk::rangerDrillSergeant] = {Effect::Aura{Aura{1.0, Leveling::radius, Filter::bio}}};
    sim.trialEffects[Perk::rangerArmorPiercing] = {Effect::Stat{Stat::IgnoresArmor{}, Scope::Driven{}, Change::Plus{1}},
                                                   Effect::Stat{Stat::DamageVs{TargetClass::armored}, Scope::Driven{}, Change::Plus{5}}};
    sim.trialEffects[Perk::rangerQuartermaster] = {Effect::Stat{Stat::Training{UnitKind::ranger}, Scope::Team{}, Change::Percent{0.3}},
                                                   Effect::Stat{Stat::UnitCost{UnitKind::ranger}, Scope::Team{}, Change::Percent{-0.1}}};
    give(sim, UnitKind::ranger, 7, {Perk::rangerDrillSergeant, Perk::rangerQuartermaster, Perk::rangerArmorPiercing});
    // Juggernaut: 1 armour, armoured; the driven rifle ignores the armour
    // and adds 5.
    EXPECT_NEAR(sim.strikeDamage(unit(sim, 998), red).value_or(-1), (6 + 5) * Rules::heroDamage, 1e-9);
    EXPECT_TRUE(isPrice(sim.unitCost(UnitKind::ranger, 0), 45, 0));
    EXPECT_NEAR(sim.teamBoost(Stat::Training{UnitKind::ranger}, 0).rate(), 1.3, 1e-12);
    EXPECT_TRUE(sim.teamBoost(Stat::Training{UnitKind::ranger}, 1) == Boost::none);
    run(sim, 1);
    EXPECT_NEAR(unit(sim, hurt).hp, 31, 0.05);
}
