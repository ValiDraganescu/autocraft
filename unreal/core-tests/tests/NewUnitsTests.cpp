// The Peregrine, the Atlas and the Scorpion (docs/new-units.md): isolated
// scenarios of a few seconds each, hand-built positions, never a game. The
// air fights and the Atlas run on a map-less field (no fog); the Scorpion's
// hiding on the handcrafted map, which has one.
#include "LevelingScene.h"
#include "SimHelpers.h"
#include "VisionHelpers.h"

#include "Pilot.h"
#include "Session.h"
#include "Vision.h"

using namespace ac;

namespace {

using simtest::bare;
using simtest::hp;
using simtest::run;
using simtest::shots;

/// Two players, nothing on the field, no map (no fog, straight flight).
GameState empty() {
    GameState s = bare(2, 600, {}, {}, 1);
    s.players[0].ore = 1000;
    s.players[0].hydrogen = 1000;
    return s;
}

/// A unit standing still (it never thinks about walking away); a buried
/// Scorpion is down with a raid order on the spot, so it stays put.
int64_t add(GameState& s, Unit::Kind kind, Vec2 p, int64_t owner, double heading = 0) {
    const int64_t id = s.nextID;
    Unit u(id, kind, owner, p, heading, Unit::Task::idle);
    if (kind == Unit::Kind::longbow) { u.anchor = 0; u.anchored = false; u.aim = heading; }
    if (kind == Unit::Kind::hailstorm || kind == Unit::Kind::firefly || kind == Unit::Kind::atlas) u.aim = heading;
    if (kind == Unit::Kind::scorpion) { u.anchor = 0; u.anchored = false; u.mission = Mission::Raid{p}; }
    if (kind == Unit::Kind::dropship) { u.energy = 0; u.cargo = std::vector<int64_t>{}; }
    s.units.push_back(u);
    s.nextID += 1;
    return id;
}

int64_t addBuried(GameState& s, Vec2 p, int64_t owner) {
    const int64_t id = add(s, Unit::Kind::scorpion, p, owner);
    s.units.back().anchor = 1;
    s.units.back().anchored = true;
    return id;
}

/// A Quake stomp event in `events`: its radius (nil: none). It is a case of
/// its own, never a `Blast` (that is the Pulse mine's).
std::optional<double> stomped(const std::vector<GameEvent>& events) {
    for (const auto& e : events) {
        EXPECT_TRUE(!e.is<GameEvent::Blast>());
        if (auto st = e.as<GameEvent::Stomp>()) return st->radius;
    }
    return std::nullopt;
}

} // namespace

// MARK: - The tables

TEST(newUnits_statsAndRulesMatchTheDesign) {
    const auto& p = Rules::stats(Unit::Kind::peregrine);
    EXPECT_EQ(p.hp, 80.0);
    EXPECT_EQ(p.speed, 5.6);
    EXPECT_EQ(Rules::cost(Unit::Kind::peregrine), int64_t(100));
    EXPECT_EQ(Rules::hydrogenCost(Unit::Kind::peregrine), int64_t(75));
    EXPECT_TRUE(p.air && p.light && !p.armored && p.hitsAir && !p.hitsGround);
    EXPECT_TRUE(Rules::damage(p, true, false, 0) == 32.0); // 10 ×2, +6 against armoured, per hit
    EXPECT_TRUE(Rules::flight(Unit::Kind::peregrine, false, 15) == 0.1 + 15.0 / 30);
    const auto& a = Rules::stats(Unit::Kind::atlas);
    EXPECT_TRUE(a.hp == 500 && a.armor == 2 && a.speed == 2.2 && a.radius == 1.25 && a.supply == 6);
    EXPECT_TRUE(a.hitsGround && !a.hitsAir && a.armored);
    EXPECT_EQ(Rules::damage(a, true, false, 1), 78.0); // 25 ×2 (+15 armoured), less 1 each
    const auto& c = Rules::stats(Unit::Kind::scorpion);
    EXPECT_TRUE(c.hp == 90 && c.speed == 3.9 && c.range == 5 && c.cooldown == 25);
    EXPECT_TRUE(c.hitsGround && c.hitsAir && c.light);
    EXPECT_EQ(Rules::damage(c, true, false, 1), 89.0);
    EXPECT_TRUE(Rules::needsLab(Unit::Kind::atlas) && !Rules::needsLab(Unit::Kind::scorpion)
                && !Rules::needsLab(Unit::Kind::peregrine));
    const auto at = [](StructureKind b, UnitKind k) {
        const auto t = Rules::trains(b);
        return std::find(t.begin(), t.end(), k) != t.end();
    };
    EXPECT_TRUE(at(StructureKind::spacedock, UnitKind::peregrine));
    EXPECT_TRUE(at(StructureKind::foundry, UnitKind::atlas) && at(StructureKind::foundry, UnitKind::scorpion));
    // An Atlas does not fit any Dropship, with or without Cargo bay.
    EXPECT_TRUE(Rules::slots(Unit::Kind::atlas) > 12);
    // Saved games store the index: the new kinds come last.
    EXPECT_EQ(ordinal(UnitKind::hailstorm), int64_t(8));
    EXPECT_EQ(ordinal(UnitKind::peregrine), int64_t(9));
    EXPECT_EQ(ordinal(UnitKind::scorpion), int64_t(11));
    EXPECT_TRUE(Rules::vision(UnitKind::peregrine) == 11 && Rules::vision(UnitKind::scorpion) == 8);
}

// MARK: - Peregrine

/// One Peregrine beats one Kestrel (its missiles do 2 × 16 against the
/// armoured gunship every 1.25 s; the rail gun's bonus does not reach a
/// light flyer) and is left alive.
TEST(newUnits_peregrineBeatsAKestrel) {
    GameState s = empty();
    const int64_t peregrine = add(s, Unit::Kind::peregrine, Vec2::zero, 0);
    const int64_t kestrel = add(s, Unit::Kind::kestrel, Vec2(6, 0), 1, pi);
    Simulation sim(s);
    run(sim, 9);
    EXPECT_TRUE(hp(sim, kestrel) <= 0);
    EXPECT_TRUE(hp(sim, peregrine) > 0);
    EXPECT_TRUE(hp(sim, peregrine) < Rules::hp(Unit::Kind::peregrine)); // the rail gun did land
}

/// The missiles fly: nothing lands at the moment of the salvo, and the
/// damage arrives with `Rules::flight`.
TEST(newUnits_peregrineMissilesFlyAndLandOnArrival) {
    GameState s = empty();
    const int64_t peregrine = add(s, Unit::Kind::peregrine, Vec2::zero, 0);
    const int64_t drop = add(s, Unit::Kind::dropship, Vec2(6, 0), 1);
    Simulation sim(s);
    bool fired = false;
    for (int k = 0; k < 30 && !fired; k++) fired = shots(sim.step(1.0 / 30), peregrine) > 0;
    ASSERT_TRUE(fired);
    EXPECT_EQ(hp(sim, drop), Rules::hp(Unit::Kind::dropship)); // still in the air
    EXPECT_TRUE(!sim.inFlight.empty());
    run(sim, 0.5);
    EXPECT_NEAR(Rules::hp(Unit::Kind::dropship) - hp(sim, drop), Rules::damage(Unit::Kind::peregrine, false, true, false, 1), 1e-9);
}

/// It cannot attack the ground: on attack-move it flies past enemy
/// ground units and buildings and never fires at them.
TEST(newUnits_peregrineIgnoresTheGround) {
    GameState s = empty();
    const int64_t peregrine = add(s, Unit::Kind::peregrine, Vec2::zero, 0);
    const int64_t firefly = add(s, Unit::Kind::firefly, Vec2(4, 0), 1);
    const int64_t dome = simtest::addStructure(s, Structure::Kind::habDome, Vec2(5, 3), 1);
    s.players[0].attack = Vec2(20, 0);
    s.units[0].task = Unit::Task::attackMove;
    Simulation sim(s);
    EXPECT_TRUE(!sim.canHit(*sim.state.unit(peregrine), *sim.target(firefly)));
    EXPECT_TRUE(!sim.canHit(*sim.state.unit(peregrine), *sim.target(dome)));
    const auto events = run(sim, 4);
    EXPECT_EQ(shots(events, peregrine), 0);
    EXPECT_EQ(hp(sim, firefly), Rules::hp(Unit::Kind::firefly));
    EXPECT_TRUE(sim.state.unit(peregrine)->position.x > 10); // it flew on toward the army's point
    const int64_t kestrel = add(sim.state, Unit::Kind::kestrel, Vec2(14, 0), 1);
    sim.reindex();
    EXPECT_TRUE(sim.canHit(*sim.state.unit(peregrine), *sim.target(kestrel)));
}

// MARK: - Atlas

/// An Atlas's cannons cannot reach a flyer, and nothing in its sights
/// stops it from killing a Longbow line.
TEST(newUnits_atlasShootsTheGroundOnly) {
    GameState s = empty();
    const int64_t atlas = add(s, Unit::Kind::atlas, Vec2::zero, 0);
    const int64_t kestrel = add(s, Unit::Kind::kestrel, Vec2(4, 0), 1);
    const int64_t ranger = add(s, Unit::Kind::ranger, Vec2(4, 3), 1);
    Simulation sim(s);
    EXPECT_TRUE(!sim.canHit(*sim.state.unit(atlas), *sim.target(kestrel)));
    EXPECT_TRUE(sim.canHit(*sim.state.unit(atlas), *sim.target(ranger)));
}

/// One salvo into a Longbow line: the one aimed at takes the full 78
/// (25 ×2, +15 armoured, less 1 armour each), the one beside it at half
/// distance takes half, and a Ranger of the Atlas's own side right there is
/// spared.
TEST(newUnits_atlasSplashHurtsEnemiesAndSparesFriends) {
    GameState s = empty();
    const int64_t atlas = add(s, Unit::Kind::atlas, Vec2::zero, 0);
    const int64_t aimed = add(s, Unit::Kind::longbow, Vec2(6, 0), 1, pi);
    const int64_t beside = add(s, Unit::Kind::longbow, Vec2(6, 1.6), 1, pi);
    const int64_t friend_ = add(s, Unit::Kind::prospector, Vec2(6, -1.0), 0);
    Simulation sim(s);
    int64_t fired = 0;
    for (int k = 0; k < 90 && fired == 0; k++) fired += shots(sim.step(1.0 / 30), atlas);
    ASSERT_TRUE(fired > 0);
    run(sim, 0.1);
    const double full = Rules::damage(Unit::Kind::atlas, false, true, false, 1);
    EXPECT_EQ(full, 78.0);
    const double a = Rules::hp(Unit::Kind::longbow) - hp(sim, aimed), b = Rules::hp(Unit::Kind::longbow) - hp(sim, beside);
    EXPECT_NEAR(a, full, 1e-9);
    EXPECT_NEAR(b, full / 2, 1e-9);
    EXPECT_EQ(hp(sim, friend_), Rules::hp(Unit::Kind::prospector));
}

/// The Quake stomp fires by itself on three enemies on the ground within 2
/// cells (30 damage each, slowed), not on two; friends and flyers are spared.
TEST(newUnits_atlasStompsByItselfOnACrowd) {
    {
        GameState s = empty();
        const int64_t atlas = add(s, Unit::Kind::atlas, Vec2::zero, 0);
        add(s, Unit::Kind::ranger, Vec2(2.0, 0.5), 1);
        add(s, Unit::Kind::ranger, Vec2(2.0, -0.5), 1);
        Simulation sim(s);
        EXPECT_TRUE(!stomped(sim.step(1.0 / 30)).has_value());
        EXPECT_TRUE(!sim.state.unit(atlas)->stompReady);
    }
    GameState s = empty();
    const int64_t atlas = add(s, Unit::Kind::atlas, Vec2::zero, 0);
    std::vector<int64_t> crowd;
    for (const Vec2 p : {Vec2(2.0, 0.5), Vec2(2.0, -0.5), Vec2(0.0, 2.3)}) crowd.push_back(add(s, Unit::Kind::ranger, p, 1));
    const int64_t kestrel = add(s, Unit::Kind::kestrel, Vec2(1, 1), 1);
    const int64_t mate = add(s, Unit::Kind::ranger, Vec2(-1.5, 0), 0);
    Simulation sim(s);
    const auto events = sim.step(1.0 / 30);
    EXPECT_NEAR(stomped(events).value_or(-1), Rules::stompRadius, 1e-9); // its own event, with its reach
    const Unit a = *sim.state.unit(atlas);
    ASSERT_TRUE(a.stompReady.has_value());
    EXPECT_NEAR(*a.stompReady - sim.state.time, Rules::stompCooldown, 1e-9);
    for (const int64_t id : crowd) {
        // 30 from the stomp; the Atlas's own cannon may add to it.
        EXPECT_TRUE(hp(sim, id) <= Rules::hp(Unit::Kind::ranger) - Rules::stompDamage + 1e-9);
        EXPECT_TRUE(sim.state.unit(id)->slowUntil.value_or(0) > sim.state.time); // and slowed
    }
    EXPECT_EQ(hp(sim, kestrel), Rules::hp(Unit::Kind::kestrel)); // flyers are above it
    EXPECT_EQ(hp(sim, mate), Rules::hp(Unit::Kind::ranger));
    // Not again until it is ready.
    run(sim, 5);
    EXPECT_EQ(sim.state.unit(atlas)->stompReady.value(), *a.stompReady);
}

/// Driven, the Atlas stomps on the ability key, and Aftershock makes the
/// stomp hit harder and reach 1 cell farther.
TEST(newUnits_drivenAtlasStompsOnTheKeyAndAftershockWidensIt) {
    using namespace leveling;
    Scene s = drive(UnitKind::atlas);
    const int64_t near = add(s.sim, 950, UnitKind::ranger, s.at + s.out * 3.0, 1);   // edge at 2.6 from the Atlas
    const int64_t closer = add(s.sim, 951, UnitKind::ranger, s.at + s.out * 1.5, 1);
    s.sim.hit(near, 0, false, nullptr);
    EXPECT_TRUE(s.sim.pilotAbility(unit(s.sim, s.id)) == std::optional<PilotAbility>(PilotAbility(PilotAbility::Action::Stomp{}, "Quake stomp")));
    s.sim.pilot->ability = true;
    const auto events = s.sim.step(1.0 / 30);
    EXPECT_NEAR(stomped(events).value_or(-1), Rules::stompRadius, 1e-9);
    EXPECT_NEAR(hp(s.sim, closer), Rules::hp(UnitKind::ranger) - Rules::stompDamage, 1e-9);
    EXPECT_EQ(hp(s.sim, near), Rules::hp(UnitKind::ranger)); // out of reach (2.6 > 2)
    const auto again = s.sim.pilotAbility(unit(s.sim, s.id));
    ASSERT_TRUE(again.has_value());
    EXPECT_TRUE(!again->action && again->why.has_value()); // waiting out the 20 s
    EXPECT_EQ(tallyOf(s.sim, UnitKind::atlas).stomps, int64_t(1));
    EXPECT_EQ(tallyOf(s.sim, UnitKind::atlas).stompHits, int64_t(1));

    // With Aftershock: 45 damage out to 3 cells.
    Scene t = drive(UnitKind::atlas);
    give(t.sim, UnitKind::atlas, {Perk::atlasAftershock});
    const int64_t far = add(t.sim, 950, UnitKind::ranger, t.at + t.out * 3.0, 1);
    t.sim.pilot->ability = true;
    EXPECT_NEAR(stomped(t.sim.step(1.0 / 30)).value_or(-1), Rules::stompRadius + 1, 1e-9); // the event carries the wider reach
    EXPECT_NEAR(hp(t.sim, far), Rules::hp(UnitKind::ranger) - Rules::stompDamage * 1.5, 1e-9);
}

/// Flak mount: only the driven Atlas with the pick fires at flyers, at
/// half damage.
TEST(newUnits_flakMountLetsTheDrivenAtlasHitFlyers) {
    using namespace leveling;
    Scene s = drive(UnitKind::atlas);
    const int64_t drop = add(s.sim, 950, UnitKind::dropship, s.at + s.out * 4.0, 1);
    EXPECT_TRUE(!s.sim.canHit(unit(s.sim, s.id), *s.sim.target(drop)));
    give(s.sim, UnitKind::atlas, {Perk::atlasFlakMount});
    EXPECT_TRUE(s.sim.canHit(unit(s.sim, s.id), *s.sim.target(drop)));
    const Unit me = unit(s.sim, s.id);
    const double full = s.sim.damage(me, *s.sim.target(950));
    // The hero's ×1.5 counted: the same attack at a Ranger-sized armoured ground target.
    const Unit probe = me;
    Simulation::Target ground = *s.sim.target(drop);
    ground.air = false;
    EXPECT_NEAR(full, s.sim.damage(probe, ground) * 0.5, 1e-9);
}

// MARK: - Scorpion

/// A Scorpion that is not buried does not sting; buried, it locks on for a
/// second and then stings (60 kills a Ranger) and is out of ammunition for
/// 25 s, bar the reloading it does while it moves.
TEST(newUnits_scorpionStingsOnlyBuriedAfterALockOn) {
    GameState s = empty();
    const int64_t scorpion = add(s, Unit::Kind::scorpion, Vec2::zero, 0);
    const int64_t ranger = add(s, Unit::Kind::ranger, Vec2(4, 0), 1);
    Simulation sim(s);
    // It starts out on the ground and buries itself (2 s), locks on (1 s).
    auto events = run(sim, 2.5);
    EXPECT_EQ(shots(events, scorpion), 0);
    EXPECT_TRUE(sim.state.unit(scorpion)->burrowed());
    EXPECT_EQ(hp(sim, ranger) > 0, true);
    events = run(sim, 2);
    EXPECT_EQ(shots(events, scorpion), 1);
    EXPECT_TRUE(hp(sim, ranger) <= 0);
    EXPECT_NEAR(sim.state.unit(scorpion)->cooldown.value_or(0), Rules::stats(Unit::Kind::scorpion).cooldown - 1.7, 0.6);
}

/// The lock-on second is the victim's chance: a unit that steps out of
/// reach before it ends is safe, and the lock starts over.
TEST(newUnits_scorpionLockOnLetsAVictimEscape) {
    GameState s = empty();
    const int64_t scorpion = addBuried(s, Vec2::zero, 0);
    const int64_t ship = add(s, Unit::Kind::dropship, Vec2(4, 0), 1); // unarmed, and it never chases
    Simulation sim(s);
    run(sim, 0.6);
    EXPECT_EQ(sim.state.unit(scorpion)->lockTarget.value_or(-1), ship);
    sim.state.units[1].position = Vec2(9, 0); // out of reach
    const auto events = run(sim, 2);
    EXPECT_EQ(shots(events, scorpion), 0);
    EXPECT_EQ(hp(sim, ship), Rules::hp(Unit::Kind::dropship));
    EXPECT_TRUE(!sim.state.unit(scorpion)->lockTarget.has_value());
}

/// It hits flyers too, and the sting's splash spares its own side but
/// reaches the enemies beside the target (half, within 1.25 cells).
TEST(newUnits_scorpionHitsTheAirAndItsSplashSparesFriends) {
    GameState s = empty();
    const int64_t scorpion = addBuried(s, Vec2::zero, 0);
    // Dropships: unarmed, so nothing shoots back and the first is the target.
    const int64_t aimed = add(s, Unit::Kind::dropship, Vec2(4, 0), 1);
    const int64_t beside = add(s, Unit::Kind::dropship, Vec2(4, 1.8), 1);
    const int64_t mate = add(s, Unit::Kind::dropship, Vec2(4, -1.8), 0);
    Simulation sim(s);
    int64_t fired = 0;
    for (int k = 0; k < 120 && fired == 0; k++) fired += shots(sim.step(1.0 / 30), scorpion);
    ASSERT_TRUE(fired == 1);
    run(sim, 0.4); // the bolt's flight
    const double full = Rules::damage(Unit::Kind::scorpion, false, true, false, 1);
    EXPECT_EQ(full, 89.0);
    EXPECT_NEAR(Rules::hp(Unit::Kind::dropship) - hp(sim, aimed), full, 1e-9); // a flyer, hit in the air
    EXPECT_NEAR(Rules::hp(Unit::Kind::dropship) - hp(sim, beside), full / 2, 1e-9); // within 1.25 cells
    EXPECT_EQ(hp(sim, mate), Rules::hp(Unit::Kind::dropship));
}

/// A buried Scorpion cannot move: ordered off, it digs out first (2 s),
/// and only then walks.
TEST(newUnits_scorpionCannotMoveWhileBuried) {
    GameState s = empty();
    const int64_t scorpion = addBuried(s, Vec2::zero, 0);
    s.units.back().mission = std::nullopt;
    s.units.back().task = Unit::Task::attackMove;
    s.players[0].attack = Vec2(30, 0);
    Simulation sim(s);
    run(sim, 1.5);
    EXPECT_TRUE(sim.state.unit(scorpion)->position == Vec2::zero);
    EXPECT_TRUE(sim.state.unit(scorpion)->anchor.value_or(0) > 0);
    run(sim, 2);
    EXPECT_TRUE(sim.state.unit(scorpion)->position.x > 1);
    EXPECT_EQ(sim.state.unit(scorpion)->anchor.value_or(1), 0.0);
}

/// Moving does not reset the reload: a Scorpion that stung and then dug out
/// and walked still has most of the 25 s to wait, counted from the sting.
TEST(newUnits_scorpionKeepsItsReloadWhileMoving) {
    GameState s = empty();
    const int64_t scorpion = addBuried(s, Vec2::zero, 0);
    s.units.back().cooldown = 20;
    Simulation sim(s);
    sim.state.units.back().anchored = false;
    sim.state.units.back().mission = std::nullopt;
    sim.state.units.back().task = Unit::Task::attackMove;
    sim.state.players[0].attack = Vec2(30, 0);
    run(sim, 6);
    EXPECT_TRUE(sim.state.unit(scorpion)->position.x > 1); // it has walked
    EXPECT_NEAR(sim.state.unit(scorpion)->cooldown.value_or(0), 14.0, 0.2);
}

// MARK: - Hiding (fog of war)

namespace {

GameState field() {
    GameState s = GameState::new_(visiontest::homeMap());
    s.units.clear();
    s.time = 600;
    return s;
}

Vec2 citadel(const GameState& s, int64_t owner) {
    for (const auto& b : s.structures) if (b.owner == owner) return b.position;
    return Vec2::zero;
}

/// 18 cells from blue's Citadel toward red's: out of both Citadels' sight.
Vec2 spotOf(const GameState& s) {
    const Vec2 blue = citadel(s, 0), red = citadel(s, 1);
    return blue + normalize(red - blue) * 18;
}

} // namespace

/// Buried and idle, a Scorpion is invisible to an enemy Ranger 4 cells off
/// that sees the very ground it stands on, but not to one within 2 cells,
/// and its own side always sees it.
TEST(newUnits_aBuriedScorpionIsHiddenUnlessAnEnemyIsNear) {
    GameState s = field();
    const Vec2 at = spotOf(s);
    const int64_t scorpion = addBuried(s, at, 1);
    const int64_t ranger = add(s, Unit::Kind::ranger, at + Vec2(0, 4), 0);
    Simulation sim(s, visiontest::homeMap());
    sim.lookNow();
    EXPECT_TRUE(sim.sees(0, at + Vec2(0, 1)));  // the ground is seen
    EXPECT_TRUE(!sim.sees(0, scorpion));        // the Scorpion is not
    EXPECT_TRUE(sim.sees(1, scorpion));         // its own side sees it
    EXPECT_TRUE(!sim.foe(scorpion, 0).has_value()); // so no one can pick it as a target
    EXPECT_TRUE(!sim.known(0).unit(scorpion).has_value());
    // An enemy within 2 cells sees it.
    sim.state.units[1].position = at + Vec2(0, 1.9);
    sim.lookNow();
    EXPECT_TRUE(sim.sees(0, scorpion));
    sim.state.units[1].position = at + Vec2(0, 2.4);
    sim.lookNow();
    EXPECT_TRUE(!sim.sees(0, scorpion));
    (void)ranger;
    // So does an enemy building within 2 cells (edge to edge).
    simtest::addStructure(sim.state, Structure::Kind::habDome, at + Vec2(2.9, 0), 0);
    sim.lookNow();
    EXPECT_TRUE(sim.sees(0, scorpion));
}

/// An enemy Sentinel's whole sight (11 cells) sees it; a unit without one
/// does not at that distance; one still being built is no detector.
TEST(newUnits_aSentinelSeesABuriedScorpion) {
    GameState s = field();
    const Vec2 at = spotOf(s);
    const int64_t scorpion = addBuried(s, at, 1);
    add(s, Unit::Kind::ranger, at + Vec2(0, 8.5), 0); // sees the ground there
    const int64_t tower = simtest::addStructure(s, Structure::Kind::sentinel, at + Vec2(0, 9.5), 0);
    Simulation sim(s, visiontest::homeMap());
    sim.lookNow();
    EXPECT_TRUE(sim.sees(0, scorpion));
    sim.state.structures.back().position = at + Vec2(0, 12.5);
    sim.lookNow();
    EXPECT_TRUE(!sim.sees(0, scorpion));
    (void)tower;
}

/// A strike gives it away to all while it reloads, and a buried Scorpion
/// is hidden again once it is ready. Enemies who cannot see it cannot
/// target it; the Ranger in its reach is stung before it knows.
TEST(newUnits_aStrikeRevealsAScorpionWhileItReloads) {
    GameState s = field();
    const Vec2 at = spotOf(s);
    const int64_t scorpion = addBuried(s, at, 1);
    const int64_t bait = add(s, Unit::Kind::ranger, at + Vec2(0, 4), 0);
    Simulation sim(s, visiontest::homeMap());
    sim.lookNow();
    EXPECT_TRUE(!sim.sees(0, scorpion));
    const auto events = run(sim, 0.7);
    EXPECT_EQ(shots(events, bait), 0); // the Ranger never found anything to shoot
    EXPECT_TRUE(!sim.sees(0, scorpion));
    const auto later = run(sim, 1.2); // the lock-on has ended: it stings
    EXPECT_EQ(shots(later, scorpion), 1);
    EXPECT_TRUE(hp(sim, bait) <= 0);
    EXPECT_TRUE(sim.state.unit(scorpion)->cooldown.value_or(0) > 20);
    // Reloading: visible to anyone with sight of the ground.
    const int64_t scout = add(sim.state, Unit::Kind::ranger, at + Vec2(0, 7), 0);
    sim.reindex();
    run(sim, 0.3);
    EXPECT_TRUE(sim.sees(0, scorpion));
    // Ready again (and the strike long past): hidden again.
    sim.state.units[size_t(*sim.unitIndex(scorpion))].cooldown = 0;
    sim.state.units[size_t(*sim.unitIndex(scorpion))].struckAt = sim.state.time - 10;
    run(sim, 0.2);
    EXPECT_TRUE(!sim.sees(0, scorpion) || hp(sim, scout) <= 0);
}

/// The reveal after a strike has a clock of its own: 3 s from the strike,
/// whatever the reload (here cut to nothing), on a map with fog and on one
/// without. After them it hides again; while it reloads it is seen, as
/// long as that lasts.
TEST(newUnits_aStrikeRevealsAScorpionForThreeSecondsOnItsOwnClock) {
    for (const bool fog : {true, false}) {
        GameState s = fog ? field() : empty();
        const Vec2 at = fog ? spotOf(s) : Vec2(30, 0);
        const int64_t scorpion = addBuried(s, at, 1);
        const int64_t bait = add(s, Unit::Kind::dropship, at + Vec2(0, 4), 0);
        Simulation sim = fog ? Simulation(s, visiontest::homeMap()) : Simulation(s);
        sim.lookNow();
        EXPECT_TRUE(!sim.sees(0, scorpion));
        EXPECT_TRUE(!sim.state.unit(scorpion)->struckAt.has_value());
        run(sim, 1.6); // the lock-on, then the sting
        ASSERT_TRUE(sim.state.unit(scorpion)->struckAt.has_value());
        const double struck = *sim.state.unit(scorpion)->struckAt;
        EXPECT_TRUE(hp(sim, bait) < Rules::hp(Unit::Kind::dropship));
        // The reload is cut to nothing: only the 3 s clock holds the reveal.
        sim.state.units[size_t(*sim.unitIndex(scorpion))].cooldown = 0;
        sim.state.units[size_t(*sim.unitIndex(scorpion))].lockTarget = std::nullopt;
        sim.state.units[size_t(*sim.unitIndex(bait))].position = at + Vec2(0, 8); // out of reach, still looking
        sim.reindex();
        run(sim, 0.05);
        sim.lookNow();
        EXPECT_TRUE(sim.state.time - struck < Rules::strikeReveal);
        EXPECT_TRUE(sim.sees(0, scorpion));
        EXPECT_TRUE(sim.foe(scorpion, 0).has_value());
        run(sim, 2.0);
        sim.lookNow();
        EXPECT_TRUE(sim.state.time - struck < Rules::strikeReveal);
        EXPECT_TRUE(sim.sees(0, scorpion));
        run(sim, 1.2);
        sim.lookNow();
        EXPECT_TRUE(sim.state.time - struck > Rules::strikeReveal);
        EXPECT_TRUE(!sim.sees(0, scorpion));
        EXPECT_TRUE(!sim.foe(scorpion, 0).has_value());
    }
}

/// A map without fog hides a buried Scorpion all the same: no foe to the
/// enemy (nothing acquires it), seen again within 2 cells (1 with Deep
/// burrow), by an enemy building, by an enemy Sentinel's whole sight;
/// the owner and its allies always see it, and it is left out of what the
/// enemy's AI plays from and what its screen shows.
TEST(newUnits_aBuriedScorpionIsHiddenOnAMapWithoutFog) {
    GameState s = empty();
    const int64_t scorpion = addBuried(s, Vec2(10, 0), 1);
    const int64_t ranger = add(s, Unit::Kind::ranger, Vec2(10, 4), 0);
    Simulation sim(s);
    EXPECT_TRUE(!sim.vision.has_value());
    EXPECT_TRUE(!sim.sees(0, scorpion));
    EXPECT_TRUE(sim.sees(1, scorpion));
    EXPECT_TRUE(!sim.foe(scorpion, 0).has_value());
    EXPECT_TRUE(sim.foe(scorpion, 1).has_value());
    EXPECT_TRUE(!sim.acquire(*sim.state.unit(ranger)).has_value());
    EXPECT_TRUE(!sim.known(0).unit(scorpion).has_value());
    EXPECT_TRUE(!sim.shown(0).unit(scorpion).has_value());
    EXPECT_TRUE(!sim.seen(0).state.unit(scorpion).has_value());
    EXPECT_TRUE(sim.known(1).unit(scorpion).has_value());
    EXPECT_TRUE(sim.unseen(0).count(scorpion) > 0);
    EXPECT_TRUE(sim.unseen(1).empty());
    // The Ranger is not shot at first: nothing to fire at; the Scorpion stings it.
    const auto events = run(sim, 0.8);
    EXPECT_EQ(shots(events, ranger), 0);
    // Within 2 cells an enemy unit sees it, and so may shoot it.
    sim.state.units[size_t(*sim.unitIndex(ranger))].position = Vec2(10, 1.9);
    sim.reindex();
    EXPECT_TRUE(sim.sees(0, scorpion));
    EXPECT_TRUE(sim.foe(scorpion, 0).has_value());
    EXPECT_TRUE(sim.known(0).unit(scorpion).has_value());
    sim.state.units[size_t(*sim.unitIndex(ranger))].position = Vec2(10, 2.4);
    sim.reindex();
    EXPECT_TRUE(!sim.sees(0, scorpion));
    // An enemy Sentinel sees it from across its whole sight; one still
    // building does not.
    const int64_t tower = simtest::addStructure(sim.state, Structure::Kind::sentinel, Vec2(10, 9), 0);
    sim.reindex();
    EXPECT_TRUE(sim.sees(0, scorpion));
    sim.state.structures.back().position = Vec2(10, 14);
    EXPECT_TRUE(!sim.sees(0, scorpion));
    (void)tower;
}

/// A unit stung by a Scorpion it could not see goes for where the sting came
/// from (`Rules::unseenChase`): the sting reveals the Scorpion, but the
/// victim is judged by what it saw as the sting came, on a map with fog and
/// on one without; a Scorpion it could see (a unit within 2 cells) leaves
/// no such mark.
TEST(newUnits_aVictimOfAnUnseenScorpionGoesForTheSpotItCameFrom) {
    for (const bool fog : {true, false}) {
        GameState s = fog ? field() : empty();
        const Vec2 at = fog ? spotOf(s) : Vec2(30, 0);
        addBuried(s, at, 1);
        const int64_t victim = add(s, Unit::Kind::juggernaut, at + Vec2(0, 4), 0);
        Simulation sim = fog ? Simulation(s, visiontest::homeMap()) : Simulation(s);
        sim.lookNow();
        run(sim, 1.6);
        const Unit v = *sim.state.unit(victim);
        EXPECT_TRUE(v.hp < Rules::hp(Unit::Kind::juggernaut));
        ASSERT_TRUE(v.shotFrom.has_value());
        EXPECT_TRUE(distance(*v.shotFrom, at) < 1e-9);
    }
    // Seen at the time (an enemy unit within 2 cells): no mark.
    GameState s = empty();
    addBuried(s, Vec2(30, 0), 1);
    const int64_t victim = add(s, Unit::Kind::juggernaut, Vec2(30, 1.5), 0);
    Simulation sim(s);
    run(sim, 1.6);
    EXPECT_TRUE(sim.state.unit(victim)->hp < Rules::hp(Unit::Kind::juggernaut));
    EXPECT_TRUE(!sim.state.unit(victim)->shotFrom.has_value());
}

/// Deep burrow, once driven: enemies see it only within 1 cell.
TEST(newUnits_deepBurrowShrinksTheRevealRange) {
    GameState s = field();
    const Vec2 at = spotOf(s);
    // Player 0 drives the Scorpion; player 1 looks for it.
    const int64_t scorpion = addBuried(s, at, 0);
    s.units.back().task = Unit::Task::idle;
    add(s, Unit::Kind::ranger, at + Vec2(0, 1.5), 1);
    Simulation sim(s, visiontest::homeMap());
    EXPECT_TRUE(sim.take(scorpion));
    sim.lookNow();
    EXPECT_TRUE(sim.sees(1, scorpion)); // 1.5 cells: within 2
    leveling::give(sim, UnitKind::scorpion, {Perk::scorpionDeepBurrow});
    sim.lookNow();
    EXPECT_TRUE(!sim.sees(1, scorpion)); // within 1 only
    sim.state.units[1].position = at + Vec2(0, 0.9);
    sim.lookNow();
    EXPECT_TRUE(sim.sees(1, scorpion));
}

/// The driven Scorpion: the ability key buries and digs out, the fire
/// button stings the crosshair's target once it has been held a second;
/// the tally counts strikes and hits, and the seconds it hid.
TEST(newUnits_aDrivenScorpionBuriesAndStings) {
    GameState s = field();
    const Vec2 at = spotOf(s);
    const int64_t scorpion = add(s, Unit::Kind::scorpion, at, 0);
    s.units.back().mission = std::nullopt;
    const int64_t target = add(s, Unit::Kind::ranger, at + Vec2(3.5, 0), 1);
    s.units.back().mission = Mission::Raid{at + Vec2(3.5, 0)}; // it holds its ground
    Simulation sim(s, visiontest::homeMap());
    ASSERT_TRUE(sim.take(scorpion));
    sim.pilot->heading = 0;
    const auto digIn = sim.pilotAbility(*sim.state.unit(scorpion));
    EXPECT_TRUE(digIn == std::optional<PilotAbility>(PilotAbility(PilotAbility::Action::Anchor{}, "Bury")));
    sim.pilot->ability = true;
    run(sim, 0.1);
    sim.pilot->walk = Vec2(1, 0);
    run(sim, 2.2);
    EXPECT_TRUE(sim.state.unit(scorpion)->burrowed());
    EXPECT_TRUE(sim.state.unit(scorpion)->position.x < at.x + 0.5); // it did not walk while it dug in
    EXPECT_TRUE(sim.pilotAbility(*sim.state.unit(scorpion)) ==
                std::optional<PilotAbility>(PilotAbility(PilotAbility::Action::Unanchor{}, "Dig out")));
    // Held fire: 1 s of lock-on, then the sting.
    sim.pilot->walk = Vec2(0, 0);
    sim.pilot->hold = true;
    sim.pilot->crosshair = Pilot::Crosshair::On{target};
    run(sim, 0.8);
    EXPECT_TRUE(hp(sim, target) > 0);
    run(sim, 0.6);
    EXPECT_TRUE(hp(sim, target) <= 0);
    const Tally t = leveling::tallyOf(sim, UnitKind::scorpion);
    EXPECT_EQ(t.strikes, int64_t(1));
    EXPECT_EQ(t.strikeHits, int64_t(1));
    EXPECT_TRUE(t.hiddenSeconds > 1.5);
    EXPECT_EQ(t.kills, int64_t(1));
}

/// Sapper: with the pick, the sting also hits buildings (at half damage);
/// without it a Scorpion never picks one. Twin sting: a second target within
/// 2 cells of the first takes a hit too.
TEST(newUnits_sapperAndTwinStingExtendTheDrivenSting) {
    using namespace leveling;
    Scene s = drive(UnitKind::scorpion);
    unitRef(s.sim, s.id).anchor = 1;
    unitRef(s.sim, s.id).anchored = true;
    const Vec2 spot = s.at + s.out * 4;
    const int64_t dome = 960;
    s.sim.state.structures.push_back(Structure(dome, Structure::Kind::habDome, 1, spot));
    const int64_t a = add(s.sim, 951, UnitKind::ranger, spot + s.side() * 3.0, 1);
    const int64_t b = add(s.sim, 952, UnitKind::ranger, spot + s.side() * 3.0 + s.out * 1.8, 1);
    s.sim.lookNow();
    // No picks: the dome is not a target.
    EXPECT_TRUE(!s.sim.canHit(unit(s.sim, s.id), *s.sim.target(dome)));
    give(s.sim, UnitKind::scorpion, {Perk::scorpionSapper, Perk::scorpionTwinSting});
    EXPECT_TRUE(s.sim.canHit(unit(s.sim, s.id), *s.sim.target(dome)));
    const Unit me = unit(s.sim, s.id);
    Simulation::Target asUnit = *s.sim.target(dome);
    asUnit.structure = false;
    EXPECT_NEAR(*s.sim.strikeDamage(me, dome) * 2, s.sim.damage(me, asUnit), 1e-9);
    // Fire at the dome: it loses half a sting.
    s.sim.pilot->hold = true;
    s.sim.pilot->crosshair = Pilot::Crosshair::On{dome};
    s.sim.pilot->heading = std::atan2(s.out.y, s.out.x);
    leveling::run(s.sim, 1.3);
    const Structure after = *s.sim.state.structure(dome);
    EXPECT_NEAR(Rules::hp(StructureKind::habDome) - after.hp, *s.sim.strikeDamage(me, dome), 1e-9);
    // The next sting (reloaded by hand) at Ranger a: b, 1.8 off, takes the twin.
    unitRef(s.sim, s.id).cooldown = 0;
    s.sim.pilot->crosshair = Pilot::Crosshair::On{a};
    leveling::run(s.sim, 1.3);
    EXPECT_TRUE(hp(s.sim, a) <= 0);
    EXPECT_TRUE(hp(s.sim, b) <= 0); // the twin: a full second hit
    EXPECT_EQ(tallyOf(s.sim, UnitKind::scorpion).strikes, int64_t(2));
}

// MARK: - Leveling and saved games

/// 18 picks of each new kind, the doc's tables in order; the new cases are
/// built for the picks that need them.
TEST(newUnits_theNewKindsHaveTheirEighteenPicks) {
    for (const UnitKind k : {UnitKind::peregrine, UnitKind::atlas, UnitKind::scorpion}) {
        int64_t n = 0;
        for (Perk p : allCases<Perk>()) if (kind(p) == k) n += 1;
        EXPECT_EQ(n, int64_t(18));
    }
    EXPECT_TRUE(perkOffer(UnitKind::peregrine, 2)->first == Perk::peregrineWingmen);
    EXPECT_TRUE(perkOffer(UnitKind::peregrine, 5)->second == Perk::peregrineEscort);
    EXPECT_TRUE(perkOffer(UnitKind::atlas, 8)->first == Perk::atlasAftershock);
    EXPECT_TRUE(perkOffer(UnitKind::atlas, 9)->second == Perk::atlasFlakMount);
    EXPECT_TRUE(perkOffer(UnitKind::scorpion, 5)->second == Perk::scorpionDeepBurrow);
    EXPECT_TRUE(perkOffer(UnitKind::scorpion, 10)->second == Perk::scorpionShield);
    EXPECT_TRUE(effects(Perk::peregrineEscort) ==
                std::vector<Effect>{Effect::Stat{Stat::DamageTaken{}, Scope::Around{Filter::air}, Change::Percent{-0.1}}});
    EXPECT_TRUE(effects(Perk::atlasFlakMount) == std::vector<Effect>{Effect::HitsAir{0.5}});
    EXPECT_TRUE(effects(Perk::scorpionTwinSting) == std::vector<Effect>{Effect::SecondTarget{2.0}});
    EXPECT_TRUE(effects(Perk::scorpionSapper) == std::vector<Effect>{Effect::HitsBuildings{0.5}});
    EXPECT_TRUE(covers(Filter::air, UnitKind::kestrel) && covers(Filter::air, UnitKind::peregrine));
    EXPECT_TRUE(!covers(Filter::air, UnitKind::atlas) && !covers(Filter::air, UnitKind::ranger));
    EXPECT_EQ(Leveling::machines.contains(UnitKind::atlas), true);
}

/// Escort: a driven Peregrine's flyers within 6 cells take 10% less, a
/// ground unit beside it does not; Afterburn is the Peregrine's key.
TEST(newUnits_escortCoversFlyersAndAfterburnTakesTheKey) {
    using namespace leveling;
    Scene s = drive(UnitKind::peregrine);
    EXPECT_TRUE(!s.sim.pilotAbility(unit(s.sim, s.id)).has_value()); // no key until the pick
    give(s.sim, UnitKind::peregrine, {Perk::peregrineEscort, Perk::peregrineAfterburn});
    add(s.sim, 950, UnitKind::kestrel, s.at + s.out * 3);
    add(s.sim, 951, UnitKind::ranger, s.at + s.out * 3.5);
    EXPECT_NEAR(s.sim.rate(Stat::DamageTaken{}, unit(s.sim, 950)), 0.9, 1e-12);
    EXPECT_NEAR(s.sim.rate(Stat::DamageTaken{}, unit(s.sim, 951)), 1.0, 1e-12);
    const auto key = s.sim.pilotAbility(unit(s.sim, s.id));
    ASSERT_TRUE(key.has_value());
    EXPECT_TRUE(key->title == "Afterburn" && key->action.has_value());
}

/// A session with the new kinds, picks, the Scorpion's lock and the Atlas's
/// stomp clock, and every new tally field set, round-trips; an old save's
/// tally without them loads as zeros and is saved without them.
TEST(newUnits_sessionsRoundTripWithTheNewKinds) {
    using namespace leveling;
    Scene s = drive(UnitKind::atlas);
    add(s.sim, 950, UnitKind::peregrine, s.at + s.out * 3);
    add(s.sim, 951, UnitKind::scorpion, s.at + s.out * 6);
    unitRef(s.sim, 951).anchor = 1;
    unitRef(s.sim, 951).anchored = true;
    unitRef(s.sim, 951).lockTarget = 900;
    unitRef(s.sim, 951).lockFrom = 12.5;
    unitRef(s.sim, 951).lockAt = 13.0;
    unitRef(s.sim, 900).stompReady = 33.0;
    unitRef(s.sim, 900).stompedAt = 13.0;
    give(s.sim, UnitKind::atlas, {Perk::atlasAutoloader, Perk::atlasLongBarrels});
    s.sim.updateLevels([](PilotRecord& r) {
        Tally& t = r.kinds[UnitKind::atlas].tally;
        t.airKills = 1; t.stomps = 2; t.stompHits = 5; t.strikes = 3; t.strikeHits = 4; t.hiddenSeconds = 6.5;
    });
    Session session;
    session.id = "new-units";
    session.state = s.sim.state;
    const std::string text = SessionStore::encode(session);
    const auto back = SessionStore::decode<Session>(text);
    ASSERT_TRUE(back.has_value());
    EXPECT_TRUE(back->state.units == s.sim.state.units);
    EXPECT_TRUE(back->state.players[0].pilot == s.sim.state.players[0].pilot);
    EXPECT_TRUE(SessionStore::encode(*back) == text);
    EXPECT_TRUE(text.find("\"peregrine\"") != std::string::npos && text.find("\"scorpion\"") != std::string::npos);
    EXPECT_TRUE(text.find("atlasAutoloader") != std::string::npos);
    // The default tally leaves the new fields out, as an old save has it.
    Session plain;
    plain.id = "plain";
    Simulation sim = newSim();
    plain.state = sim.state;
    EXPECT_TRUE(SessionStore::encode(plain).find("stompHits") == std::string::npos);
}

// MARK: - The AI

namespace {

using Mix = Commander::EnemyMix;

const MapDefinition& homeMap_() { return visiontest::homeMap(); }

int64_t add(GameState& s, Structure::Kind kind, Vec2 p, int64_t owner) { return simtest::addStructure(s, kind, p, owner); }

/// Give a building a finished Lab.
void addLab(GameState& s, int64_t id) {
    size_t i = 0;
    while (s.structures[i].id != id) i++;
    Structure lab(s.nextID, Structure::Kind::lab, s.structures[i].owner, s.structures[i].position + Rules::addonOffset);
    lab.parent = id;
    s.structures[i].addon = lab.id;
    s.structures.push_back(lab);
    s.nextID += 1;
}

/// Blue alone on the handcrafted map with a Hab Dome, a Garrison, a
/// Foundry and both main Derricks, a few Prospectors and money.
struct Base { GameState state; int64_t foundry; Structure citadel; };

Base base(const MapDefinition& m) {
    GameState s = GameState::new_(m);
    std::erase_if(s.units, [](const Unit& u) { return u.owner != 0; });
    std::erase_if(s.structures, [](const Structure& b) { return b.owner != 0; });
    s.players[0].style = Player::Style::bio;
    s.players[0].drops = false;
    s.players[0].greed = 0;
    const Commander c(m);
    const Structure citadel = s.structures[0];
    add(s, Structure::Kind::habDome, *c.habDomeSpot(s, citadel), 0);
    add(s, Structure::Kind::garrison, *c.garrisonSpot(s, citadel), 0);
    const int64_t foundry = add(s, Structure::Kind::foundry, *c.garrisonSpot(s, citadel), 0);
    for (const Well& g : *s.wells) if (distance(g.position, citadel.position) < 12) add(s, Structure::Kind::derrick, g.position, 0);
    for (int k = 0; k < 7; k++) add(s, Unit::Kind::prospector, citadel.position + Vec2(static_cast<double>(k) - 3, -3.2), 0);
    s.players[0].ore = 2000;
    s.players[0].hydrogen = 2000;
    return Base{s, foundry, citadel};
}

std::vector<Command> orders(const MapDefinition& m, const GameState& s) {
    Simulation sim(s, m);
    sim.state.players[0].supplyCap = Rules::maxSupply;
    return Commander(m).orders(sim);
}

bool trainsKind(const std::vector<Command>& o, int64_t building, Unit::Kind kind) {
    for (const Command& c : o) {
        if (auto t = c.as<Command::Train>(); t && t->structure == building && t->kind && *t->kind == kind) return true;
    }
    return false;
}

std::optional<Vec2> raidOf(const std::vector<Command>& o, int64_t unit) {
    for (const Command& c : o) {
        if (auto m = c.as<Command::Mission>(); m && m->unit == unit && m->mission) {
            if (auto r = m->mission->as<Mission::Raid>()) return r->at;
        }
    }
    return std::nullopt;
}

std::optional<int64_t> followOf(const std::vector<Command>& o, int64_t unit) {
    for (const Command& c : o) {
        if (auto m = c.as<Command::Mission>(); m && m->unit == unit && m->mission) {
            if (auto f = m->mission->as<Mission::Follow>()) return f->unit;
        }
    }
    return std::nullopt;
}

Unit& unitById(GameState& s, int64_t id) {
    for (Unit& u : s.units) if (u.id == id) return u;
    return s.units.front();
}

} // namespace

/// Armed enemy flyers bring Peregrines (about one a flyer, up to 8); an
/// Atlas seen brings Kestrels, since nothing it has shoots air.
TEST(newUnits_theAICountersFlyersAndAtlases) {
    const Commander c(homeMap_());
    EXPECT_EQ(c.counters(Mix{.armored = 3, .air = 3, .armedAir = 3, .total = 6}).peregrines, int64_t(1)); // a Kestrel
    EXPECT_EQ(c.counters(Mix{.armored = 9, .air = 9, .armedAir = 9, .total = 18}).peregrines, int64_t(3));
    EXPECT_EQ(c.counters(Mix{.air = 99, .armedAir = 99, .total = 99}).peregrines, int64_t(8)); // capped
    EXPECT_EQ(c.counters(Mix{.air = 4, .total = 4}).peregrines, int64_t(0)); // Dropships do not shoot
    EXPECT_EQ(c.counters(Mix{.antiAir = 10, .total = 10}).kestrels, int64_t(0));
    EXPECT_EQ(c.counters(Mix{.antiAir = 10, .total = 10, .atlases = 1}).kestrels, int64_t(2));
    EXPECT_EQ(c.counters(Mix{.antiAir = 10, .total = 20, .atlases = 3}).kestrels, int64_t(4)); // two Atlases' worth
    GameState s = GameState::new_(homeMap_());
    Vec2 red = Vec2::zero;
    for (const auto& b : s.structures) if (b.owner == 1) red = b.position;
    add(s, Unit::Kind::atlas, red + Vec2(0, 8), 1);
    add(s, Unit::Kind::peregrine, red + Vec2(2, 8), 1);
    const Mix mix = c.enemyMix(s);
    EXPECT_EQ(mix.atlases, int64_t(1));
    EXPECT_TRUE(mix.air == 2 && mix.armedAir == 2 && mix.armored == 6);
}

/// A Spacedock trains a Peregrine once armed flyers are seen, and not before.
TEST(newUnits_aSpacedockTrainsPeregrinesAgainstFlyers) {
    const MapDefinition& m = homeMap_();
    Base b = base(m);
    const Commander c(m);
    const int64_t dock = add(b.state, Structure::Kind::spacedock, *c.garrisonSpot(b.state, b.citadel), 0);
    EXPECT_TRUE(!trainsKind(orders(m, b.state), dock, Unit::Kind::peregrine));
    add(b.state, Unit::Kind::kestrel, b.citadel.position + Vec2(0, 12), 1);
    EXPECT_TRUE(trainsKind(orders(m, b.state), dock, Unit::Kind::peregrine));
}

/// A mech AI with a Lab at its Foundry saves for an Atlas once its army is 40
/// supply, and not before.
TEST(newUnits_aMechFoundryBuildsAnAtlasAtFortySupply) {
    const MapDefinition& m = homeMap_();
    Base b = base(m);
    b.state.players[0].style = Player::Style::mech;
    addLab(b.state, b.foundry);
    for (int k = 0; k < 12; k++) {
        add(b.state, Unit::Kind::longbow, b.citadel.position + Vec2(static_cast<double>(k % 6) * 2 - 5, 8 + 2.0 * (k / 6)), 0);
    }
    EXPECT_TRUE(!trainsKind(orders(m, b.state), b.foundry, Unit::Kind::atlas)); // 36 supply
    for (int k = 0; k < 2; k++) add(b.state, Unit::Kind::longbow, b.citadel.position + Vec2(static_cast<double>(k) * 2 - 1, 12), 0);
    EXPECT_TRUE(trainsKind(orders(m, b.state), b.foundry, Unit::Kind::atlas)); // 42
}

/// Idle Scorpions get posted two to an ore line, each at its own spot;
/// a Peregrine holds over the base while it has nothing to escort.
TEST(newUnits_theAIPostsScorpionsAtItsOreLineAndPeregrinesOverhead) {
    const MapDefinition& m = homeMap_();
    Base b = base(m);
    const Vec2 home = b.citadel.position;
    const int64_t a = add(b.state, Unit::Kind::scorpion, home + Vec2(0, 6), 0);
    const int64_t c2 = add(b.state, Unit::Kind::scorpion, home + Vec2(2, 6), 0);
    const int64_t third = add(b.state, Unit::Kind::scorpion, home + Vec2(4, 6), 0);
    const int64_t p = add(b.state, Unit::Kind::peregrine, home + Vec2(0, 9), 0);
    const int64_t fourth = add(b.state, Unit::Kind::scorpion, home + Vec2(6, 6), 0);
    for (const int64_t id : {a, c2, third, fourth}) unitById(b.state, id).mission = std::nullopt; // no post yet
    const auto o = orders(m, b.state);
    const auto pa = raidOf(o, a), pb = raidOf(o, c2);
    ASSERT_TRUE(pa.has_value() && pb.has_value());
    EXPECT_TRUE(distance(*pa, *pb) > 4); // either side of the line
    Vec2 sum = Vec2::zero;
    int n = 0;
    for (const OreDeposit& d : b.state.patches) if (distance(d.position, home) < 10) { sum = sum + d.position; n += 1; }
    EXPECT_TRUE(distance(*pa, sum / double(n)) < 8 && distance(*pb, sum / double(n)) < 8);
    EXPECT_TRUE(raidOf(o, third).has_value()); // the third goes to the ramp (see the chokes below)
    EXPECT_TRUE(!raidOf(o, fourth).has_value()); // two to a line, one to the choke
    ASSERT_TRUE(raidOf(o, p).has_value()); // nothing to escort: over the main
    EXPECT_TRUE(!followOf(o, p).has_value());
}

/// A Peregrine escorts: on a follow order on a Kestrel (or Dropship), the
/// ones out on a raid or a drop first, a few to each; it never joins the
/// army (a follow order is an order of its own).
TEST(newUnits_theAISendsPeregrinesToEscortItsFlyers) {
    const MapDefinition& m = homeMap_();
    Base b = base(m);
    const Vec2 home = b.citadel.position;
    const int64_t p = add(b.state, Unit::Kind::peregrine, home + Vec2(0, 9), 0);
    const int64_t q = add(b.state, Unit::Kind::peregrine, home + Vec2(2, 9), 0);
    const int64_t idle = add(b.state, Unit::Kind::kestrel, home + Vec2(0, 12), 0);
    // Only an idle Kestrel: it is escorted where it stands.
    auto o = orders(m, b.state);
    EXPECT_EQ(followOf(o, p).value_or(-1), idle);
    EXPECT_EQ(followOf(o, q).value_or(-1), idle);
    // A Kestrel and a Dropship out on a raid and a drop: they come first,
    // one Peregrine each.
    const int64_t raider = add(b.state, Unit::Kind::kestrel, home + Vec2(0, 14), 0);
    unitById(b.state, raider).mission = Mission::Raid{home + Vec2(30, 30)};
    const int64_t ship = add(b.state, Unit::Kind::dropship, home + Vec2(2, 14), 0);
    unitById(b.state, ship).mission = Mission::Drop{home + Vec2(30, 32)};
    o = orders(m, b.state);
    const auto fp = followOf(o, p), fq = followOf(o, q);
    ASSERT_TRUE(fp.has_value() && fq.has_value());
    EXPECT_TRUE(*fp != *fq);
    EXPECT_TRUE((*fp == raider || *fp == ship) && (*fq == raider || *fq == ship));
    // One already on its charge keeps it (no order is repeated).
    unitById(b.state, p).mission = Mission::Follow{raider};
    o = orders(m, b.state);
    EXPECT_TRUE(!followOf(o, p).has_value());
    EXPECT_EQ(followOf(o, q).value_or(-1), ship);
    // And the Commander gives it to Peregrines that are not driven only.
    Simulation sim(b.state, m);
    EXPECT_TRUE(!sim.inArmy(*sim.state.unit(p)));
}

/// Sent to a spot, a Scorpion walks there, and buries itself when it stands.
TEST(newUnits_aPostedScorpionBuriesWhereItStands) {
    GameState s = empty();
    const int64_t scorpion = add(s, Unit::Kind::scorpion, Vec2::zero, 0);
    s.units.back().mission = Mission::Raid{Vec2(10, 0)};
    Simulation sim(s);
    run(sim, 1);
    EXPECT_TRUE(!sim.state.unit(scorpion)->burrowed());
    run(sim, 6);
    const Unit u = *sim.state.unit(scorpion);
    EXPECT_TRUE(distance(u.position, Vec2(10, 0)) < 2);
    EXPECT_TRUE(u.burrowed());
}

// MARK: - Escort (the follow order)

namespace {

/// The command that gives `unit` a follow order on `leader`.
bool follow(Simulation& sim, int64_t unit, int64_t leader) {
    std::vector<GameEvent> events;
    return sim.issue(Command::Mission{unit, Mission::Follow{leader}}, events);
}

} // namespace

/// A Peregrine on a follow order flies on with a Dropship that goes
/// somewhere, and ends beside it (not on top of it), not at the army's
/// rally or where it started.
TEST(newUnits_aFollowerHoldsFormationBesideAMovingLeader) {
    GameState s = empty();
    const int64_t ship = add(s, Unit::Kind::dropship, Vec2::zero, 0);
    s.units.back().mission = Mission::Raid{Vec2(24, 0)};
    const int64_t a = add(s, Unit::Kind::peregrine, Vec2(-6, 3), 0);
    const int64_t b = add(s, Unit::Kind::peregrine, Vec2(-6, -3), 0);
    Simulation sim(s);
    ASSERT_TRUE(follow(sim, a, ship));
    ASSERT_TRUE(follow(sim, b, ship));
    run(sim, 3);
    EXPECT_TRUE(sim.state.unit(ship)->position.x > 5);
    run(sim, 3);
    const Unit leader = *sim.state.unit(ship), ua = *sim.state.unit(a), ub = *sim.state.unit(b);
    EXPECT_TRUE(leader.position.x > 10);
    for (const Unit& u : {ua, ub}) {
        const double d = distance(u.position, leader.position);
        EXPECT_TRUE(d > 1.0 && d < 3.2); // beside it
    }
    // One on each side of the leader's heading.
    const Vec2 left(-std::sin(leader.heading), std::cos(leader.heading));
    EXPECT_TRUE(dot(ua.position - leader.position, left) * dot(ub.position - leader.position, left) < 0);
}

/// A follower engages an enemy flyer that comes within 7 cells of the unit it
/// follows, and goes back to its place once the flyer is down; a flyer
/// farther from the leader than that is left alone, even if the follower
/// could reach it.
TEST(newUnits_anEscortEngagesFlyersNearItsChargeAndReturns) {
    GameState s = empty();
    const int64_t ship = add(s, Unit::Kind::dropship, Vec2::zero, 0);
    const int64_t escort = add(s, Unit::Kind::peregrine, Vec2(0, 2), 0);
    const int64_t near = add(s, Unit::Kind::dropship, Vec2(6.5, 0), 1);
    const int64_t far = add(s, Unit::Kind::dropship, Vec2(-9, 0), 1);
    Simulation sim(s);
    ASSERT_TRUE(follow(sim, escort, ship));
    const auto events = run(sim, 8);
    EXPECT_TRUE(shots(events, escort) > 0);
    EXPECT_TRUE(hp(sim, near) <= 0);
    EXPECT_EQ(hp(sim, far), Rules::hp(Unit::Kind::dropship)); // 9 cells from the charge: not its business
    run(sim, 6);
    const Unit u = *sim.state.unit(escort), leader = *sim.state.unit(ship);
    EXPECT_TRUE(distance(u.position, sim.formationSpot(u, leader)) < 1.0);
    EXPECT_TRUE(u.mission.has_value() && u.mission->is<Mission::Follow>()); // still escorting
    EXPECT_TRUE(!u.target.has_value());
}

/// A follower does not chase a flyer out of its charge's reach: one that
/// flies away is dropped at 7 cells, and the follower is back beside the
/// leader.
TEST(newUnits_anEscortGivesUpAFlyerThatLeavesTheChargesReach) {
    GameState s = empty();
    const int64_t ship = add(s, Unit::Kind::dropship, Vec2::zero, 0);
    const int64_t escort = add(s, Unit::Kind::peregrine, Vec2(0, 2), 0);
    const int64_t runner = add(s, Unit::Kind::dropship, Vec2(6, 0), 1);
    s.units.back().mission = Mission::Raid{Vec2(60, 0)};
    Simulation sim(s);
    ASSERT_TRUE(follow(sim, escort, ship));
    run(sim, 10);
    EXPECT_TRUE(sim.state.unit(runner)->position.x > 20);
    EXPECT_TRUE(distance(sim.state.unit(escort)->position, sim.state.unit(ship)->position) < 3.2);
}

/// The order is refused for a stranger, a dead unit, a unit following
/// itself or a chain back to itself, and ends when the leader dies.
TEST(newUnits_aFollowOrderNeedsAFriendAndEndsWithIt) {
    GameState s = empty();
    const int64_t mine = add(s, Unit::Kind::kestrel, Vec2::zero, 0);
    const int64_t escort = add(s, Unit::Kind::peregrine, Vec2(2, 0), 0);
    const int64_t enemy = add(s, Unit::Kind::kestrel, Vec2(20, 0), 1);
    Simulation sim(s);
    EXPECT_TRUE(!follow(sim, escort, enemy));
    EXPECT_TRUE(!follow(sim, escort, escort));
    EXPECT_TRUE(!follow(sim, escort, 9999));
    ASSERT_TRUE(follow(sim, escort, mine));
    EXPECT_TRUE(!follow(sim, mine, escort)); // a loop
    sim.state.units.erase(sim.state.units.begin() + *sim.unitIndex(mine));
    sim.reindex();
    run(sim, 0.5);
    EXPECT_TRUE(!sim.state.unit(escort)->mission.has_value()); // the order ended with its charge
}

/// A follow order is saved with the game and loads the same.
TEST(newUnits_aFollowOrderSurvivesSaveAndLoad) {
    GameState s = empty();
    const int64_t ship = add(s, Unit::Kind::dropship, Vec2::zero, 0);
    const int64_t escort = add(s, Unit::Kind::peregrine, Vec2(2, 0), 0);
    Simulation sim(s);
    ASSERT_TRUE(follow(sim, escort, ship));
    Session session;
    session.id = "follow";
    session.state = sim.state;
    const std::string text = SessionStore::encode(session);
    EXPECT_TRUE(text.find("\"follow\"") != std::string::npos);
    const auto back = SessionStore::decode<Session>(text);
    ASSERT_TRUE(back.has_value());
    const Unit* u = nullptr;
    for (const Unit& v : back->state.units) if (v.id == escort) u = &v;
    ASSERT_TRUE(u != nullptr);
    ASSERT_TRUE(u->mission.has_value() && u->mission->is<Mission::Follow>());
    EXPECT_EQ(u->mission->as<Mission::Follow>()->unit, ship);
    EXPECT_TRUE(SessionStore::encode(*back) == text);
}

// MARK: - Escort by itself (a Peregrine with no order of its own)

namespace {

/// The unit it follows, if it has a follow order.
std::optional<int64_t> leaderOf(const Simulation& sim, int64_t id) {
    const Unit u = *sim.state.unit(id);
    if (u.mission && u.mission->is<Mission::Follow>()) return u.mission->as<Mission::Follow>()->unit;
    return std::nullopt;
}

} // namespace

/// An idle Peregrine follows the closest friendly flyer that is not a
/// Peregrine, by itself, whoever owns it; enemy flyers, other Peregrines and
/// ground units do not count.
TEST(newUnits_anIdlePeregrineFollowsTheClosestFriendlyFlyer) {
    for (const int64_t owner : {int64_t(0), int64_t(1)}) {
        GameState s = empty();
        const int64_t p = add(s, Unit::Kind::peregrine, Vec2::zero, owner);
        add(s, Unit::Kind::peregrine, Vec2(1, 0), owner);
        add(s, Unit::Kind::kestrel, Vec2(0, 3), 1 - owner); // an enemy's
        add(s, Unit::Kind::ranger, Vec2(0, -2), owner);
        const int64_t far = add(s, Unit::Kind::dropship, Vec2(-20, 0), owner);
        const int64_t close = add(s, Unit::Kind::kestrel, Vec2(10, 0), owner);
        Simulation sim(s);
        EXPECT_TRUE(!leaderOf(sim, p).has_value());
        run(sim, 0.5);
        EXPECT_EQ(leaderOf(sim, p).value_or(-1), close);
        EXPECT_TRUE(sim.state.unit(p)->autoFollow == true);
        // It flies to its place beside the leader.
        run(sim, 3);
        EXPECT_TRUE(distance(sim.state.unit(p)->position, sim.state.unit(close)->position) < 3.2);
        (void)far;
    }
}

/// With no friendly flyer a Peregrine stays as it is; one that flies in
/// later is picked up.
TEST(newUnits_aPeregrineWithNoFlyerToFollowStaysAsItIs) {
    GameState s = empty();
    const int64_t p = add(s, Unit::Kind::peregrine, Vec2::zero, 0);
    add(s, Unit::Kind::kestrel, Vec2(5, 0), 1);
    Simulation sim(s);
    run(sim, 1);
    EXPECT_TRUE(!sim.state.unit(p)->mission.has_value());
    EXPECT_TRUE(!sim.state.unit(p)->autoFollow.has_value());
    const int64_t k = add(sim.state, Unit::Kind::kestrel, Vec2(0, 8), 0);
    sim.reindex();
    run(sim, 0.5);
    EXPECT_EQ(leaderOf(sim, p).value_or(-1), k);
}

/// It keeps its leader until the leader is gone, or another flyer is less
/// than half as far; then it picks the closest.
TEST(newUnits_aPeregrineKeepsItsLeaderUntilAnotherIsHalfAsFar) {
    GameState s = empty();
    const int64_t p = add(s, Unit::Kind::peregrine, Vec2::zero, 0);
    const int64_t a = add(s, Unit::Kind::kestrel, Vec2(10, 0), 0);
    const int64_t b = add(s, Unit::Kind::kestrel, Vec2(0, 30), 0);
    Simulation sim(s);
    run(sim, 0.5);
    ASSERT_TRUE(leaderOf(sim, p).value_or(-1) == a);
    // Back to the start, with `a` 10 away: `b` at 6 is closer but not
    // less than half as far; at 4.9 it is.
    auto put = [&](double bAt) {
        unitById(sim.state, p).position = Vec2::zero;
        unitById(sim.state, p).timer = 0; // thinks at once, from the start line
        unitById(sim.state, a).position = Vec2(10, 0);
        unitById(sim.state, b).position = Vec2(0, bAt);
        run(sim, 0.3);
    };
    put(6);
    EXPECT_EQ(leaderOf(sim, p).value_or(-1), a);
    put(5.5);
    EXPECT_EQ(leaderOf(sim, p).value_or(-1), a);
    put(4.5);
    EXPECT_EQ(leaderOf(sim, p).value_or(-1), b);
    EXPECT_TRUE(sim.state.unit(p)->autoFollow == true);
    // The leader dies: the next closest.
    unitById(sim.state, b).hp = 0;
    sim.state.units.erase(sim.state.units.begin() + *sim.unitIndex(b));
    sim.reindex();
    run(sim, 0.5);
    EXPECT_EQ(leaderOf(sim, p).value_or(-1), a);
}

/// An order of its own wins over the automatic follow, and the automatic
/// follow starts again only once the Peregrine has none.
TEST(newUnits_anExplicitOrderBeatsTheAutomaticFollowUntilThePeregrineIsIdle) {
    GameState s = empty();
    const int64_t p = add(s, Unit::Kind::peregrine, Vec2::zero, 0);
    const int64_t k = add(s, Unit::Kind::kestrel, Vec2(4, 0), 0);
    const int64_t d = add(s, Unit::Kind::dropship, Vec2(30, 0), 0);
    Simulation sim(s);
    std::vector<GameEvent> events;
    run(sim, 0.5);
    ASSERT_TRUE(leaderOf(sim, p).value_or(-1) == k);
    // A raid: it goes, and the Kestrel beside it does not take it back.
    ASSERT_TRUE(sim.issue(Command::Mission{p, Mission::Raid{Vec2(0, -20)}}, events));
    run(sim, 2);
    EXPECT_TRUE(sim.state.unit(p)->mission.has_value() && sim.state.unit(p)->mission->is<Mission::Raid>());
    EXPECT_TRUE(!sim.state.unit(p)->autoFollow.has_value());
    EXPECT_TRUE(sim.state.unit(p)->position.y < -5);
    // Idle again (the order lifted): it follows the closest flyer again.
    ASSERT_TRUE(sim.issue(Command::Mission{p, std::nullopt}, events));
    run(sim, 0.5);
    EXPECT_EQ(leaderOf(sim, p).value_or(-1), k);
    EXPECT_TRUE(sim.state.unit(p)->autoFollow == true);
    // A follow order from outside (the AI's escort assignment) is its
    // own: it keeps the Dropship though the Kestrel is closer.
    ASSERT_TRUE(follow(sim, p, d));
    run(sim, 1);
    EXPECT_EQ(leaderOf(sim, p).value_or(-1), d);
    EXPECT_TRUE(!sim.state.unit(p)->autoFollow.has_value());
    // The one it was told to follow is gone: the order ends, and the
    // automatic follow takes over.
    sim.state.units.erase(sim.state.units.begin() + *sim.unitIndex(d));
    sim.reindex();
    run(sim, 0.5);
    EXPECT_EQ(leaderOf(sim, p).value_or(-1), k);
}

/// The automatic follow is saved with the game.
TEST(newUnits_theAutomaticFollowSurvivesSaveAndLoad) {
    GameState s = empty();
    const int64_t p = add(s, Unit::Kind::peregrine, Vec2::zero, 0);
    add(s, Unit::Kind::kestrel, Vec2(4, 0), 0);
    Simulation sim(s);
    run(sim, 0.5);
    ASSERT_TRUE(sim.state.unit(p)->autoFollow == true);
    Session session;
    session.id = "auto-follow";
    session.state = sim.state;
    const std::string text = SessionStore::encode(session);
    const auto back = SessionStore::decode<Session>(text);
    ASSERT_TRUE(back.has_value());
    for (const Unit& v : back->state.units) if (v.id == p) EXPECT_TRUE(v.autoFollow == true);
    EXPECT_TRUE(SessionStore::encode(*back) == text);
}

// MARK: - Scorpions at the chokes the AI holds

namespace {

/// Where the AI of player 0 posts `count` idle Scorpions (their raid spots).
std::vector<Vec2> posts(const MapDefinition& m, Base& b, int count) {
    std::vector<int64_t> ids;
    for (int k = 0; k < count; k++) {
        ids.push_back(add(b.state, Unit::Kind::scorpion, b.citadel.position + Vec2(static_cast<double>(k), 6), 0));
        unitById(b.state, ids.back()).mission = std::nullopt;
    }
    const auto o = orders(m, b.state);
    std::vector<Vec2> out;
    for (const int64_t id : ids) if (auto r = raidOf(o, id)) out.push_back(*r);
    return out;
}

bool anyNear(const std::vector<Vec2>& v, Vec2 p, double within) {
    for (const Vec2 q : v) if (distance(q, p) < within) return true;
    return false;
}

} // namespace

/// A Scorpion is buried at the foot of the ramp on the AI's side of its
/// base (the end on its level, nearer to it than to the enemy), in addition
/// to the two at the ore line; none at the other end of the ramp, nor at the
/// ramp on the enemy's side.
TEST(newUnits_theAIBuriesAScorpionAtTheFootOfItsRamp) {
    const MapDefinition& m = homeMap_();
    Base b = base(m);
    const Vec2 home = b.citadel.position;
    const int64_t level = m.bases[0].level;
    std::vector<Vec2> ours, theirs, otherEnds;
    for (const Ramp& r : m.ramps) {
        for (const bool low : {true, false}) {
            const Vec2 end = low ? r.low : r.high;
            if ((low ? r.lowLevel : r.highLevel) != level) { otherEnds.push_back(end); continue; }
            (distance(end, home) < distance(end, m.bases[2].center) ? ours : theirs).push_back(end);
        }
    }
    ASSERT_TRUE(ours.size() == 1);
    ASSERT_TRUE(!theirs.empty() && !otherEnds.empty());
    const auto spots = posts(m, b, 6);
    EXPECT_EQ(spots.size(), size_t(3)); // two at the ore line, one at the ramp
    EXPECT_TRUE(anyNear(spots, ours[0], 5));
    for (const Vec2 p : theirs) EXPECT_TRUE(!anyNear(spots, p, 8));
    for (const Vec2 p : otherEnds) EXPECT_TRUE(!anyNear(spots, p, 6)); // not at the far side of the slope
}

/// One is buried at each point its squads hold (a `hold` objective), and
/// not for an objective that is still an attack.
TEST(newUnits_theAIBuriesAScorpionAtItsHoldPoints) {
    const MapDefinition& m = homeMap_();
    Base b = base(m);
    const Vec2 hold = b.citadel.position + Vec2(10, 12);
    const Vec2 attack = b.citadel.position + Vec2(-10, 12);
    Directives d;
    for (const auto& [kind, at] : {std::pair{Objective::Kind::hold, hold}, std::pair{Objective::Kind::attack, attack}}) {
        Objective o;
        o.id = d.nextID;
        o.kind = kind;
        o.at = at;
        d.objectives.push_back(o);
        d.nextID += 1;
    }
    b.state.players[0].directives = d;
    const auto spots = posts(m, b, 8);
    EXPECT_EQ(spots.size(), size_t(4)); // ore line twice, the ramp, the hold point
    EXPECT_TRUE(anyNear(spots, hold, 4));
    EXPECT_TRUE(!anyNear(spots, attack, 8));
}

/// The Foundry builds Scorpions for the chokes too: two to an ore line and
/// one to each choke.
TEST(newUnits_theAIBuildsAScorpionForEachChokeToo) {
    const MapDefinition& m = homeMap_();
    Base b = base(m);
    for (int k = 0; k < 6; k++) add(b.state, Unit::Kind::ranger, b.citadel.position + Vec2(static_cast<double>(k) - 3, 8), 0);
    for (int k = 0; k < 6; k++) add(b.state, Unit::Kind::ranger, b.citadel.position + Vec2(static_cast<double>(k) - 3, 10), 0);
    Simulation sim(b.state, m);
    EXPECT_EQ(Commander(m).chokes(sim).size(), size_t(1));
    for (int k = 0; k < 2; k++) {
        const int64_t id = add(b.state, Unit::Kind::scorpion, b.citadel.position + Vec2(static_cast<double>(k), 6), 0);
        unitById(b.state, id).mission = Mission::Raid{b.citadel.position};
    }
    EXPECT_TRUE(trainsKind(orders(m, b.state), b.foundry, Unit::Kind::scorpion)); // two stand, the third is for the ramp
    const int64_t id = add(b.state, Unit::Kind::scorpion, b.citadel.position + Vec2(3, 6), 0);
    unitById(b.state, id).mission = Mission::Raid{b.citadel.position};
    EXPECT_TRUE(!trainsKind(orders(m, b.state), b.foundry, Unit::Kind::scorpion)); // three
}

/// A follower stays out of the army's push: with the army on attack, it
/// keeps to its charge.
TEST(newUnits_aFollowerNeverJoinsAGroundPush) {
    GameState s = empty();
    const int64_t ship = add(s, Unit::Kind::dropship, Vec2::zero, 0);
    const int64_t escort = add(s, Unit::Kind::peregrine, Vec2(2, 0), 0);
    s.players[0].attack = Vec2(60, 0);
    Simulation sim(s);
    ASSERT_TRUE(follow(sim, escort, ship));
    sim.state.units[size_t(*sim.unitIndex(ship))].task = Unit::Task::idle;
    run(sim, 6);
    EXPECT_TRUE(distance(sim.state.unit(escort)->position, sim.state.unit(ship)->position) < 3.5);
}

// MARK: - Harass: Scorpions by Dropship

/// The harass style takes the Scorpions its army has to spare aboard a
/// Dropship (a Scorpion boards too), flies them to the enemy's ore line, sets
/// them down there and they bury themselves. Other styles with no drops do
/// not.
TEST(newUnits_aHarassAIDropsScorpionsAtTheEnemyOreLine) {
    const MapDefinition& m = homeMap_();
    GameState s = GameState::new_(m);
    s.players[0].style = Player::Style::harass;
    s.players[0].drops = false;
    const Vec2 blue = [&] { for (const auto& b : s.structures) if (b.owner == 0) return b.position; return Vec2::zero; }();
    Vec2 red = Vec2::zero;
    for (const auto& b : s.structures) if (b.owner == 1 && b.kind == Structure::Kind::citadel) red = b.position;
    // Red's Prospectors at work on its ore line.
    int worked = 0;
    const auto patches = s.patches;
    for (const OreDeposit& p : patches) {
        if (!(distance(p.position, red) < 10) || worked++ >= 4) continue;
        add(s, Unit::Kind::prospector, red + (p.position - red) * 0.8, 1);
    }
    const Vec2 at = blue + normalize(m.front() - blue) * 9;
    const int64_t ship = add(s, Unit::Kind::dropship, at, 0);
    s.units.back().energy = 100;
    s.units.back().cargo = std::vector<int64_t>{};
    std::vector<int64_t> stingers;
    for (int k = 0; k < 3; k++) stingers.push_back(add(s, Unit::Kind::scorpion, at + Vec2(0.9 * (k + 1), 1), 0));
    for (const int64_t id : stingers) { unitById(s, id).task = Unit::Task::idle; unitById(s, id).mission = std::nullopt; } // in the army
    const int64_t bystander = add(s, Unit::Kind::ranger, at + Vec2(-1, 1), 0);
    Simulation sim(s, m);
    const auto orders = Commander(m, 0).drops(sim);
    std::vector<int64_t> boarding;
    std::optional<Vec2> drop;
    for (const Command& c : orders) {
        if (auto b = c.as<Command::Board>(); b && b->dropship == ship && b->unit) boarding.push_back(*b->unit);
        if (auto mi = c.as<Command::Mission>(); mi && mi->unit == ship && mi->mission) {
            if (auto d = mi->mission->as<Mission::Drop>()) drop = d->at;
        }
    }
    EXPECT_EQ(boarding.size(), size_t(3)); // every Scorpion it has to spare
    for (const int64_t id : boarding) EXPECT_TRUE(id != bystander);
    ASSERT_TRUE(drop.has_value());
    EXPECT_TRUE(distance(*drop, red) < 10);
    for (const Command& c : orders) EXPECT_TRUE(sim.issue(c));
    EXPECT_EQ(sim.state.unit(ship)->cargo.value_or(std::vector<int64_t>{}).size(), size_t(3));
    for (const int64_t id : stingers) EXPECT_TRUE(sim.state.unit(id)->task == Unit::Task::aboard);
    int steps = 0;
    while (sim.state.unit(stingers[0])->task == Unit::Task::aboard && steps < 120 * 30) { sim.step(1.0 / 30); steps += 1; }
    EXPECT_TRUE(steps < 120 * 30);
    for (const int64_t id : stingers) {
        EXPECT_TRUE(distance(sim.state.unit(id)->position, *drop) < 3);
        EXPECT_TRUE(sim.state.unit(id)->mission && sim.state.unit(id)->mission->is<Mission::Raid>());
    }
    run(sim, 6);
    for (const int64_t id : stingers) EXPECT_TRUE(sim.state.unit(id)->burrowed() || sim.state.unit(id)->hp <= 0);
    // The same army with another style (and no drops) loads none.
    GameState t = s;
    t.players[0].style = Player::Style::bio;
    Simulation other(t, m);
    for (const Command& c : Commander(m, 0).drops(other)) EXPECT_TRUE(!c.is<Command::Board>());
}

// MARK: - Hit by a shooter it cannot see

namespace {

bool buildsKind(const std::vector<Command>& o, Structure::Kind kind) {
    for (const Command& c : o) {
        if (auto b = c.as<Command::Build>(); b && b->kind == kind) return true;
    }
    return false;
}

std::optional<Vec2> huntOf(const std::vector<Command>& o, int64_t unit) {
    for (const Command& c : o) {
        if (auto m = c.as<Command::Mission>(); m && m->unit == unit && m->mission) {
            if (auto h = m->mission->as<Mission::Hunt>()) return h->at;
        }
    }
    return std::nullopt;
}

} // namespace

/// Its Prospector was hit a second ago by something its side could not see,
/// from a spot by the ore line: the nearest soldier of its army goes and
/// hunts around the spot (no other, no Longbow or Scorpion), once, and the
/// order ends with the spot's window.
TEST(newUnits_aUnitGoesToTheSpotAnUnseenShooterHitFrom) {
    const MapDefinition& m = homeMap_();
    Base b = base(m);
    const Vec2 home = b.citadel.position;
    const Vec2 spot = home + Vec2(9, 3);
    const int64_t victim = add(b.state, Unit::Kind::prospector, home + Vec2(7, 3), 0);
    unitById(b.state, victim).shotFrom = spot;
    unitById(b.state, victim).hitAt = b.state.time - 1;
    const int64_t far = add(b.state, Unit::Kind::ranger, home + Vec2(0, -9), 0);
    const int64_t near = add(b.state, Unit::Kind::ranger, home + Vec2(5, 3), 0);
    const int64_t tank = add(b.state, Unit::Kind::longbow, home + Vec2(7, 4), 0);
    const int64_t mine = add(b.state, Unit::Kind::scorpion, home + Vec2(8, 4), 0);
    unitById(b.state, mine).mission = std::nullopt;
    Simulation sim(b.state, m);
    const Commander c(m, 0);
    EXPECT_EQ(c.unseenShots(sim.state).size(), size_t(1));
    auto out = c.hunters(sim);
    ASSERT_TRUE(huntOf(out, near).has_value());
    EXPECT_TRUE(distance(*huntOf(out, near), spot) < 1e-9);
    EXPECT_TRUE(!huntOf(out, far).has_value() && !huntOf(out, tank).has_value() && !huntOf(out, mine).has_value());
    // Given, it is not sent again, nor a second one.
    for (const Command& o : out) EXPECT_TRUE(sim.issue(o));
    out = c.hunters(sim);
    EXPECT_TRUE(!huntOf(out, near).has_value() && !huntOf(out, far).has_value());
    // The hit is old news: the hunt ends.
    sim.state.time += Commander::unseenWindow + 1;
    out = c.hunters(sim);
    bool released = false;
    for (const Command& o : out) {
        if (auto mi = o.as<Command::Mission>(); mi && mi->unit == near && !mi->mission) released = true;
    }
    EXPECT_TRUE(released);
}

/// A shot from a spot its side could not see, by one of its ore lines, brings
/// a Sentinel there (whose whole sight finds a hidden Scorpion), unless one
/// already sees the spot or the hit is old.
TEST(newUnits_anUnseenShooterByAnOreLineBringsASentinel) {
    const MapDefinition& m = homeMap_();
    Base b = base(m);
    const Vec2 home = b.citadel.position;
    Vec2 sum = Vec2::zero;
    int n = 0;
    for (const OreDeposit& d : b.state.patches) if (distance(d.position, home) < 10) { sum = sum + d.position; n += 1; }
    const Vec2 line = sum / double(n);
    const Vec2 spot = home + (line - home) * 1.1;
    const int64_t victim = add(b.state, Unit::Kind::prospector, home + (line - home) * 0.9, 0);
    EXPECT_TRUE(!buildsKind(orders(m, b.state), Structure::Kind::sentinel)); // nothing hit it yet
    unitById(b.state, victim).shotFrom = spot;
    unitById(b.state, victim).hitAt = b.state.time - 1;
    EXPECT_TRUE(buildsKind(orders(m, b.state), Structure::Kind::sentinel));
    // A Sentinel that sees the spot is the answer already.
    GameState seen = b.state;
    add(seen, Structure::Kind::sentinel, spot + Vec2(0, 6), 0);
    EXPECT_TRUE(!buildsKind(orders(m, seen), Structure::Kind::sentinel));
    // The hit was long ago.
    GameState old = b.state;
    unitById(old, victim).hitAt = old.time - 60;
    EXPECT_TRUE(!buildsKind(orders(m, old), Structure::Kind::sentinel));
}

// MARK: - The Atlas's turn rates

/// The torso (the cannons) turns at 3 rad/s and the legs (the whole body) at
/// 1.5 rad/s, by itself: an enemy behind it is aimed at no faster than
/// the torso swings, with the body still; and an order to walk the other way
/// turns the body at 1.5 rad/s.
TEST(newUnits_theAtlasTorsoTurnsAtThreeAndTheBodyAtOneAndAHalf) {
    EXPECT_EQ(Rules::atlasTorsoRate, 3.0);
    EXPECT_EQ(Rules::atlasBodyRate, 1.5);
    {
        // A target behind it, in reach: the torso swings round to it.
        GameState s = empty();
        const int64_t atlas = add(s, Unit::Kind::atlas, Vec2::zero, 0, 0);
        add(s, Unit::Kind::longbow, Vec2(-5, 0.01), 1);
        Simulation sim(s);
        run(sim, 0.5);
        const Unit u = *sim.state.unit(atlas);
        ASSERT_TRUE(u.aim.has_value());
        EXPECT_NEAR(std::abs(std::remainder(*u.aim, 2 * pi)), 1.5, 0.1); // 3 rad/s for half a second
        EXPECT_EQ(u.heading, 0.0);                                    // the body did not move
        run(sim, 0.7);
        EXPECT_TRUE(std::abs(std::remainder(*sim.state.unit(atlas)->aim, 2 * pi)) > pi - 0.2); // all the way round
        EXPECT_EQ(sim.state.unit(atlas)->heading, 0.0);
    }
    {
        // Sent to a point behind it: the body turns at 1.5 rad/s before it
        // can walk at full pace.
        GameState s = empty();
        const int64_t atlas = add(s, Unit::Kind::atlas, Vec2::zero, 0, 0);
        s.units.back().mission = Mission::Raid{Vec2(-20, 0)};
        Simulation sim(s);
        run(sim, 1.0);
        const Unit u = *sim.state.unit(atlas);
        EXPECT_NEAR(std::abs(u.heading), 1.5, 0.1);
        run(sim, 1.5);
        EXPECT_TRUE(std::abs(sim.state.unit(atlas)->heading) > pi - 0.2);
    }
    {
        // Driven: A/D turn the hull at 1.5 rad/s, the mouse swings the torso at 3.
        Unit u(1, Unit::Kind::atlas, 0, Vec2::zero, 0, Unit::Task::idle);
        u.aim = 0.0;
        Pilot p;
        p.heading = pi - 0.01; // looking straight behind
        p.walk = Vec2(0, 1);
        Simulation sim(empty());
        for (int k = 0; k < 15; k++) sim.pilotFace(u, p, 1.0 / 30); // half a second
        EXPECT_NEAR(u.heading, 0.75, 1e-9);
        EXPECT_NEAR(*u.aim, 1.5, 1e-9);
    }
}

// MARK: - A driven Dropship and a Scorpion

/// The Dropship the player drives loads a Scorpion it looks at (the ability
/// key), and does not heal it: it is no patient of the beam (a machine, and no
/// Nanite beam).
TEST(newUnits_aDrivenDropshipLoadsAScorpionButDoesNotHealIt) {
    using namespace leveling;
    Scene s = drive(UnitKind::dropship);
    add(s.sim, 950, UnitKind::scorpion, s.at + s.out * 1.8);
    unitRef(s.sim, 950).hp = 10;
    const auto ability = s.sim.pilotAbility(unit(s.sim, s.id));
    ASSERT_TRUE(ability.has_value() && ability->action.has_value());
    const auto load = ability->action->as<PilotAbility::Action::Load>();
    ASSERT_TRUE(load != nullptr);
    EXPECT_EQ(load->_0, int64_t(950));
    EXPECT_TRUE(!s.sim.healTarget(unit(s.sim, s.id)).has_value()); // no beam on a machine
    EXPECT_TRUE(s.sim.board(s.id, 950));
    EXPECT_TRUE(unit(s.sim, 950).task == Unit::Task::aboard);
    // A Ranger is both.
    Scene t = drive(UnitKind::dropship);
    add(t.sim, 951, UnitKind::ranger, t.at + t.out * 1.8);
    unitRef(t.sim, 951).hp = 10;
    EXPECT_TRUE(t.sim.healTarget(unit(t.sim, t.id)).has_value());
}
