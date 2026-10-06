// Port of Tests/GameCoreTests/LevelingKindsTests.swift: the picks of the
// fighting kinds (docs/leveling.md, "Every kind"): short scenes around one
// driven unit out on blue's open side, the record set by hand. For each
// kind: a pick for the driven one, one that reaches nearby or every unit of
// the kind, a stat hook and a one-off. (The catalogue checks are in
// GoldenLevelingTests.cpp.)
#include "LevelingScene.h"

using namespace ac;
using namespace leveling;

namespace {

std::vector<Perk> allOf(UnitKind kind) {
    std::vector<Perk> out;
    for (Perk p : allCases<Perk>())
        if (ac::kind(p) == kind) out.push_back(p);
    return out;
}

bool hasID(const std::vector<Unit>& units, int64_t id) {
    for (const auto& u : units)
        if (u.id == id) return true;
    return false;
}

} // namespace

/// Every pick of every kind made, and no one driving: every number is
/// as it was.
TEST(LevelingKinds_noPilotLeavesTheNewStatsAlone) {
    Scene s = drive(UnitKind::ranger);
    for (UnitKind kind : allCases<UnitKind>()) give(s.sim, kind, allOf(kind));
    add(s.sim, 950, UnitKind::dropship, s.at + s.out * 2);
    add(s.sim, 951, UnitKind::firefly, s.at + s.out * 3);
    add(s.sim, 952, UnitKind::longbow, s.at + s.out * 4);
    s.sim.take(std::nullopt);
    EXPECT_EQ(s.sim.bastionCapacity(0), Rules::bastionCapacity);
    EXPECT_EQ(s.sim.cargoSlots(unit(s.sim, 950)), Rules::dropshipSlots);
    EXPECT_TRUE(isPrice(s.sim.upgradeCost(Upgrade::aegisShield, 0), 100, 100));
    EXPECT_EQ(s.sim.weapon(unit(s.sim, 951)).range, Rules::stats(UnitKind::firefly).range);
    EXPECT_TRUE(isPrice(s.sim.unitCost(UnitKind::ranger, 0), 50, 0));
    const std::vector<Stat> stats{Stat::Sight{}, Stat::FlameReach{}, Stat::FlameWidth{}, Stat::Splash{}, Stat::Anchor{},
                                  Stat::AnchorRange{}, Stat::SlowTime{}, Stat::Slow{}, Stat::Heal{}, Stat::HealEnergy{},
                                  Stat::Energy{}, Stat::Regen{}, Stat::JumpReach{}, Stat::Cargo{}, Stat::Bounty{},
                                  Stat::Speed{}, Stat::FireRate{}, Stat::DamageTaken{}};
    for (int64_t id : {int64_t(900), int64_t(950), int64_t(951), int64_t(952)})
        for (const Stat& stat : stats) EXPECT_TRUE(s.sim.boost(stat, unit(s.sim, id)) == Boost::none);
    for (const Stat& stat : std::vector<Stat>{Stat::BastionSize{}, Stat::Production{StructureKind::foundry},
                                              Stat::Research{StructureKind::garrison}, Stat::Refund{UnitKind::firefly}})
        EXPECT_TRUE(s.sim.teamBoost(stat, 0) == Boost::none);
}

/// The other key picks: the Firefly's Boost doubles its speed for 2 s,
/// the Juggernaut's Surge costs 20 hp.
TEST(LevelingKinds_boostAndTheJuggernautsSurge) {
    Scene s = drive(UnitKind::firefly);
    give(s.sim, UnitKind::firefly, {Perk::fireflyBoost});
    EXPECT_TRUE(s.sim.pilotAbility(unit(s.sim, s.id)) ==
                std::optional<PilotAbility>(PilotAbility(PilotAbility::Action::Burst{}, "Boost")));
    if (s.sim.pilot) s.sim.pilot->ability = true;
    run(s.sim, 1.0 / 30);
    EXPECT_NEAR(s.sim.rate(Stat::Speed{}, unit(s.sim, s.id)), Rules::heroSpeed * 2, 1e-12);
    run(s.sim, 2);
    EXPECT_NEAR(s.sim.rate(Stat::Speed{}, unit(s.sim, s.id)), Rules::heroSpeed, 1e-12);
    Scene m = drive(UnitKind::juggernaut);
    give(m.sim, UnitKind::juggernaut, {Perk::juggernautSurge});
    if (m.sim.pilot) m.sim.pilot->ability = true;
    run(m.sim, 1.0 / 30);
    EXPECT_EQ(unit(m.sim, m.id).hp, Rules::hp(UnitKind::juggernaut) - 20);
}

/// Jump pack lets a Ranger and a Juggernaut jump cliffs; Long jump looks
/// half as far again for ground; Booster gives 3 s at +50% speed on
/// landing.
TEST(LevelingKinds_jumpPicks) {
    for (const auto& [kind, pick] : std::vector<std::pair<UnitKind, Perk>>{{UnitKind::ranger, Perk::rangerJumpPack},
                                                                          {UnitKind::juggernaut, Perk::juggernautJumpPack}}) {
        Scene j = drive(kind);
        EXPECT_TRUE(!j.sim.pilotJumps(unit(j.sim, j.id)));
        give(j.sim, kind, {pick});
        EXPECT_TRUE(j.sim.pilotJumps(unit(j.sim, j.id)));
    }
    Scene s = drive(UnitKind::comet);
    give(s.sim, UnitKind::comet, {Perk::cometLongJump, Perk::cometBooster});
    EXPECT_NEAR(s.sim.boost(Stat::JumpReach{}, unit(s.sim, s.id)).apply(Simulation::pilotJumpReach), 4.5, 1e-12);
    Unit u = unit(s.sim, s.id);
    u.jumpFrom = u.position;
    u.jumpTo = u.position + s.out * 0.05;
    s.sim.pilotFly(u, 1.0 / 30);
    EXPECT_TRUE(!u.jumpTo.has_value()); // landed
    unitRef(s.sim, s.id) = u;
    EXPECT_NEAR(s.sim.rate(Stat::Speed{}, u), Rules::heroSpeed * 1.5, 1e-12);
    run(s.sim, 3.1);
    EXPECT_NEAR(s.sim.rate(Stat::Speed{}, unit(s.sim, s.id)), Rules::heroSpeed, 1e-12);
}

/// The team's picks: costs, training and production, only while that
/// kind is driven.
TEST(LevelingKinds_theTeamsCostsAndTraining) {
    Scene s = drive(UnitKind::ranger);
    give(s.sim, UnitKind::ranger, {Perk::rangerQuartermaster, Perk::rangerRecruitment});
    give(s.sim, UnitKind::dropship, {Perk::dropshipCheapHull, Perk::dropshipSpacedockRefit});
    EXPECT_TRUE(isPrice(s.sim.unitCost(UnitKind::ranger, 0), 45, 0));
    EXPECT_NEAR(s.sim.teamBoost(Stat::Training{UnitKind::ranger}, 0).rate(), 1.3, 1e-12);
    EXPECT_TRUE(isPrice(s.sim.unitCost(UnitKind::dropship, 0), 100, 100)); // the Dropship's picks rest
    add(s.sim, 950, UnitKind::dropship, s.at + s.out * 2);
    EXPECT_TRUE(s.sim.take(950));
    EXPECT_TRUE(isPrice(s.sim.unitCost(UnitKind::dropship, 0), 90, 90));
    EXPECT_NEAR(s.sim.teamBoost(Stat::Production{StructureKind::spacedock}, 0).rate(), 1.25, 1e-12);
    EXPECT_TRUE(isPrice(s.sim.unitCost(UnitKind::ranger, 0), 50, 0));
}

// MARK: - Ranger

/// Flak vest and Long barrel on the driven Ranger; Drill sergeant
/// nearby, Esprit de corps on every Ranger; Bastion drill for the team.
/// None of it while a Prospector is driven.
TEST(LevelingKinds_rangerPicks) {
    Scene s = drive(UnitKind::ranger);
    const int64_t near = add(s.sim, 950, UnitKind::ranger, s.at + s.out * 3);
    const int64_t far = add(s.sim, 951, UnitKind::ranger, s.at + s.out * 20);
    give(s.sim, UnitKind::ranger, {Perk::rangerDrillSergeant, Perk::rangerFlakVest, Perk::rangerLongBarrel,
                                   Perk::rangerEspritDeCorps, Perk::rangerBastionDrill});
    EXPECT_EQ(s.sim.maxHP(unit(s.sim, s.id)), Rules::hp(UnitKind::ranger) + 20);
    EXPECT_TRUE(s.sim.target(s.id).has_value() && s.sim.target(s.id)->armor == 1);
    EXPECT_EQ(s.sim.maxHP(unit(s.sim, near)), Rules::hp(UnitKind::ranger));
    EXPECT_NEAR(s.sim.weapon(unit(s.sim, s.id)).range, Rules::rangerRange * Rules::heroRange + 2, 1e-12);
    EXPECT_EQ(s.sim.weapon(unit(s.sim, near)).range, Rules::rangerRange);
    EXPECT_NEAR(s.sim.rate(Stat::FireRate{}, unit(s.sim, s.id)), 1.2, 1e-12);
    EXPECT_NEAR(s.sim.rate(Stat::FireRate{}, unit(s.sim, near)), 1.2, 1e-12); // Drill sergeant and Esprit
    EXPECT_NEAR(s.sim.rate(Stat::FireRate{}, unit(s.sim, far)), 1.1, 1e-12); // only Esprit
    EXPECT_EQ(s.sim.bastionCapacity(0), int64_t(6));
    EXPECT_EQ(s.sim.bastionCapacity(1), Rules::bastionCapacity);
    int64_t prospector = -1;
    for (const auto& u : s.sim.state.units)
        if (u.owner == 0 && u.kind == UnitKind::prospector) { prospector = u.id; break; }
    EXPECT_TRUE(s.sim.take(prospector));
    EXPECT_EQ(s.sim.rate(Stat::FireRate{}, unit(s.sim, near)), 1.0);
    EXPECT_EQ(s.sim.maxHP(unit(s.sim, s.id)), Rules::hp(UnitKind::ranger));
    EXPECT_EQ(s.sim.bastionCapacity(0), Rules::bastionCapacity);
}

/// Surge is the Ranger's key: 10 hp for 8 s of +50% fire rate and speed.
TEST(LevelingKinds_rangerSurge) {
    Scene s = drive(UnitKind::ranger);
    give(s.sim, UnitKind::ranger, {Perk::rangerSurge});
    EXPECT_TRUE(s.sim.pilotAbility(unit(s.sim, s.id)) ==
                std::optional<PilotAbility>(PilotAbility(PilotAbility::Action::Burst{}, "Surge")));
    if (s.sim.pilot) s.sim.pilot->ability = true;
    run(s.sim, 1.0 / 30);
    EXPECT_EQ(unit(s.sim, s.id).hp, Rules::hp(UnitKind::ranger) - 10);
    EXPECT_NEAR(s.sim.weapon(unit(s.sim, s.id)).cooldown, Rules::rangerCooldown / 1.5, 1e-12);
}

// MARK: - Comet

/// Padded suit and Light killer on the driven Comet, Pack tactics
/// nearby, Nanomeds for every Comet's healing out of combat.
TEST(LevelingKinds_cometPicks) {
    Scene s = drive(UnitKind::comet);
    const int64_t near = add(s.sim, 950, UnitKind::comet, s.at + s.out * 3);
    const int64_t far = add(s.sim, 951, UnitKind::comet, s.at + s.out * 20);
    unitRef(s.sim, far).hp = 30;
    const int64_t red = add(s.sim, 990, UnitKind::ranger, s.at + s.out * 4, 1);
    give(s.sim, UnitKind::comet, {Perk::cometPackTactics, Perk::cometNanomeds, Perk::cometPaddedSuit, Perk::cometLightKiller});
    EXPECT_EQ(s.sim.maxHP(unit(s.sim, s.id)), Rules::hp(UnitKind::comet) + 25);
    // Two pistols at 4 (+5 light), and Light killer's +5 a pistol.
    EXPECT_NEAR(s.sim.strikeDamage(unit(s.sim, s.id), red).value_or(-1), (2 * 9 + 2 * 5) * Rules::heroDamage, 1e-9);
    EXPECT_NEAR(s.sim.strikeDamage(unit(s.sim, near), red).value_or(-1), 2 * 9, 1e-9);
    EXPECT_NEAR(s.sim.rate(Stat::FireRate{}, unit(s.sim, near)), 1.1, 1e-12);
    EXPECT_EQ(s.sim.rate(Stat::FireRate{}, unit(s.sim, far)), 1.0);
    run(s.sim, 1);
    EXPECT_NEAR(unit(s.sim, far).hp, 30 + Rules::cometRegen * 1.2, 0.1);
}

/// Pulse mine, the Comet's key: 20 to the enemies within 1.5 cells of
/// what the sight is on, then 14 s to wait.
TEST(LevelingKinds_cometPulseMine) {
    Scene s = drive(UnitKind::comet);
    const int64_t on = add(s.sim, 990, UnitKind::ranger, s.at + s.out * 4, 1);
    const int64_t by = add(s.sim, 991, UnitKind::ranger, s.at + s.out * 4 + s.side() * 1, 1);
    const int64_t away = add(s.sim, 992, UnitKind::ranger, s.at + s.out * 4 + s.side() * 3, 1);
    EXPECT_TRUE(!s.sim.pilotAbility(unit(s.sim, s.id)).has_value()); // no pick, no key
    give(s.sim, UnitKind::comet, {Perk::cometPulseMine});
    EXPECT_TRUE(s.sim.pilotAbility(unit(s.sim, s.id)) ==
                std::optional<PilotAbility>(PilotAbility(PilotAbility::Action::Grenade{}, "Pulse mine")));
    if (s.sim.pilot) s.sim.pilot->ability = true;
    const auto events = run(s.sim, 1.0 / 30);
    bool heard = false;
    for (const auto& e : events)
        if (auto b = e.as<GameEvent::Blast>()) heard = heard || (b->unit == s.id && b->radius == 1.5);
    EXPECT_TRUE(heard); // the scene hears of the blast
    EXPECT_EQ(unit(s.sim, on).hp, Rules::hp(UnitKind::ranger) - 20);
    EXPECT_EQ(unit(s.sim, by).hp, Rules::hp(UnitKind::ranger) - 20);
    EXPECT_EQ(unit(s.sim, away).hp, Rules::hp(UnitKind::ranger));
    EXPECT_TRUE(s.sim.pilotAbility(unit(s.sim, s.id))->why == std::optional<std::string>("Ready in 14 s"));
    EXPECT_TRUE(s.sim.levels().xp(UnitKind::comet) > Leveling::xp(10)); // the hits earn XP
}

/// Bounty: a kill of the driven Comet gives the team 10% of what the
/// victim cost; an AI Comet's kill gives nothing.
TEST(LevelingKinds_cometBounty) {
    Scene s = drive(UnitKind::comet);
    const int64_t other = add(s.sim, 950, UnitKind::comet, s.at + s.out * 3);
    const int64_t red = add(s.sim, 990, UnitKind::comet, s.at + s.out * 4, 1);
    const int64_t red2 = add(s.sim, 991, UnitKind::comet, s.at + s.out * 5, 1);
    give(s.sim, UnitKind::comet, {Perk::cometBounty});
    const int64_t ore = s.sim.state.players[0].ore, hydrogen = s.sim.state.players[0].hydrogen;
    Unit me = unit(s.sim, s.id);
    s.sim.hit(red, 30, false, &me);
    EXPECT_EQ(s.sim.state.players[0].ore, ore); // not a kill yet
    me = unit(s.sim, s.id);
    s.sim.hit(red, 100, false, &me);
    me = unit(s.sim, s.id);
    s.sim.hit(red, 100, false, &me);
    EXPECT_EQ(s.sim.state.players[0].ore, ore + 5); // once
    EXPECT_EQ(s.sim.state.players[0].hydrogen, hydrogen + 5);
    const Unit o = unit(s.sim, other);
    s.sim.hit(red2, 100, false, &o);
    EXPECT_EQ(s.sim.state.players[0].ore, ore + 5);
}

/// Spotter: the driven Comet sees 50% farther, and so its side does.
TEST(LevelingKinds_cometSpotter) {
    Scene s = drive(UnitKind::comet);
    const Vec2 spot = s.at + s.out * 12;
    s.sim.lookNow();
    EXPECT_TRUE(!s.sim.sees(0, spot)); // past a Comet's 9
    give(s.sim, UnitKind::comet, {Perk::cometSpotter});
    s.sim.lookNow();
    EXPECT_TRUE(s.sim.sees(0, spot)); // inside 13.5
}

// MARK: - Firefly

/// Armor plates and Hot core on the driven Firefly, Hot burners nearby,
/// Fuel line and Spread for every Firefly's flame, Wide nozzle for its
/// own.
TEST(LevelingKinds_fireflyPicks) {
    Scene s = drive(UnitKind::firefly);
    const int64_t near = add(s.sim, 950, UnitKind::firefly, s.at + s.out * 3);
    const int64_t far = add(s.sim, 951, UnitKind::firefly, s.at + s.out * 20);
    const int64_t red = add(s.sim, 990, UnitKind::juggernaut, s.at + s.out * 4, 1);
    give(s.sim, UnitKind::firefly, {Perk::fireflyHotBurners, Perk::fireflyFuelLine, Perk::fireflyArmorPlates,
                                    Perk::fireflyWideNozzle, Perk::fireflyHotCore});
    EXPECT_EQ(s.sim.maxHP(unit(s.sim, s.id)), Rules::hp(UnitKind::firefly) + 40);
    // A Juggernaut: 1 armour, ignored; bio, +8.
    EXPECT_NEAR(s.sim.strikeDamage(unit(s.sim, s.id), red).value_or(-1), (8 + 8) * Rules::heroDamage, 1e-9);
    EXPECT_NEAR(s.sim.strikeDamage(unit(s.sim, near), red).value_or(-1), 7, 1e-9);
    EXPECT_NEAR(s.sim.rate(Stat::FireRate{}, unit(s.sim, near)), 1.1, 1e-12);
    EXPECT_EQ(s.sim.rate(Stat::FireRate{}, unit(s.sim, far)), 1.0);
    EXPECT_NEAR(s.sim.weapon(unit(s.sim, far)).range, 5 * 1.2, 1e-12); // Fuel line
    EXPECT_NEAR(s.sim.weapon(unit(s.sim, s.id)).range, 5 * 1.2 * Rules::heroRange, 1e-12);
    EXPECT_NEAR(s.sim.boost(Stat::FlameWidth{}, unit(s.sim, s.id)).apply(Rules::flameWidth), 0.6, 1e-12);
    EXPECT_EQ(s.sim.boost(Stat::FlameWidth{}, unit(s.sim, near)).apply(Rules::flameWidth), Rules::flameWidth);
}

/// Burn: what the driven Firefly's flame hits burns for 3 s, 4 a
/// second. Scrap: a Firefly lost refunds a quarter of its cost.
TEST(LevelingKinds_fireflyBurnAndScrap) {
    Scene s = drive(UnitKind::firefly);
    const int64_t red = add(s.sim, 990, UnitKind::ranger, s.at + s.out * 3, 1);
    give(s.sim, UnitKind::firefly, {Perk::fireflyBurn, Perk::fireflyScrap});
    s.sim.land(unit(s.sim, s.id), red, *s.sim.target(red));
    const double after = Rules::hp(UnitKind::ranger) - (8 + 6) * Rules::heroDamage;
    EXPECT_NEAR(unit(s.sim, red).hp, after, 1e-9);
    EXPECT_TRUE(unit(s.sim, red).burning.has_value());
    run(s.sim, 3.2);
    EXPECT_NEAR(unit(s.sim, red).hp, after - 12, 0.3);
    EXPECT_TRUE(!unit(s.sim, red).burning.has_value()); // burnt out
    const int64_t lost = add(s.sim, 951, UnitKind::firefly, s.at + s.out * 4);
    unitRef(s.sim, lost).hp = 0;
    const int64_t ore = s.sim.state.players[0].ore;
    run(s.sim, 1.0 / 30);
    EXPECT_EQ(s.sim.state.players[0].ore, ore + 25);
}

/// A burn outlasts the driving: it burns out though the player has let
/// go of the Firefly (and the Firefly is gone).
TEST(LevelingKinds_burnOutlastsTheDriving) {
    Scene s = drive(UnitKind::firefly);
    const int64_t red = add(s.sim, 990, UnitKind::ranger, s.at + s.out * 3, 1);
    give(s.sim, UnitKind::firefly, {Perk::fireflyBurn});
    s.sim.land(unit(s.sim, s.id), red, *s.sim.target(red));
    const double after = unit(s.sim, red).hp;
    EXPECT_TRUE(s.sim.take(std::nullopt));
    unitRef(s.sim, s.id).hp = 0;
    run(s.sim, 3.2);
    EXPECT_NEAR(unit(s.sim, red).hp, after - 12, 0.3);
    EXPECT_TRUE(!unit(s.sim, red).burning.has_value()); // burnt out
}

// MARK: - Juggernaut

/// Heavy plate and Deep slow on the driven Juggernaut, Shell drill nearby,
/// Concussive for every Juggernaut's slow, Bulwark around it, Field
/// research for the team.
TEST(LevelingKinds_juggernautPicks) {
    Scene s = drive(UnitKind::juggernaut);
    const int64_t near = add(s.sim, 950, UnitKind::juggernaut, s.at + s.out * 3);
    const int64_t far = add(s.sim, 951, UnitKind::juggernaut, s.at + s.out * 20);
    const int64_t guardian = add(s.sim, 960, UnitKind::ranger, s.at + s.out * 2);
    const int64_t alone = add(s.sim, 961, UnitKind::ranger, s.at + s.out * 21);
    const int64_t red = add(s.sim, 990, UnitKind::ranger, s.at + s.out * 22, 1);
    const int64_t red2 = add(s.sim, 991, UnitKind::ranger, s.at + s.out * 4, 1);
    give(s.sim, UnitKind::juggernaut, {Perk::juggernautShellDrill, Perk::juggernautShockRounds, Perk::juggernautHeavyPlate,
                                       Perk::juggernautDeepSlow, Perk::juggernautBulwark, Perk::juggernautFieldResearch});
    EXPECT_EQ(s.sim.maxHP(unit(s.sim, s.id)), Rules::hp(UnitKind::juggernaut) + 55);
    EXPECT_EQ(s.sim.maxHP(unit(s.sim, near)), Rules::hp(UnitKind::juggernaut));
    EXPECT_NEAR(s.sim.rate(Stat::FireRate{}, unit(s.sim, near)), 1.1, 1e-12);
    EXPECT_EQ(s.sim.rate(Stat::FireRate{}, unit(s.sim, far)), 1.0);
    const double now = s.sim.state.time;
    const Unit f = unit(s.sim, far);
    s.sim.hit(red, 1, true, &f);
    EXPECT_NEAR(unit(s.sim, red).slowUntil.value_or(0), now + Rules::slowTime * 1.2, 1e-12); // Concussive
    EXPECT_TRUE(!unit(s.sim, red).slowedTo.has_value()); // half, as ever
    const Unit me = unit(s.sim, s.id);
    s.sim.hit(red2, 1, true, &me);
    EXPECT_NEAR(unit(s.sim, red2).slowedTo.value_or(0), 1.0 / 3, 1e-12); // Deep slow
    EXPECT_NEAR(s.sim.rate(Stat::DamageTaken{}, unit(s.sim, guardian)), 0.9, 1e-12);
    EXPECT_EQ(s.sim.rate(Stat::DamageTaken{}, unit(s.sim, alone)), 1.0);
    EXPECT_TRUE(isPrice(s.sim.upgradeCost(Upgrade::aegisShield, 0), 75, 75));
    EXPECT_TRUE(isPrice(s.sim.upgradeCost(Upgrade::aegisShield, 1), 100, 100));
    EXPECT_TRUE(isPrice(s.sim.upgradeCost(Upgrade::novaIgniters, 0), 150, 150)); // a Foundry upgrade
    EXPECT_NEAR(s.sim.teamBoost(Stat::Research{StructureKind::garrison}, 0).rate(), 4.0 / 3, 1e-12);
}

/// Fragment: the driven Juggernaut's grenade also does half its damage to
/// the enemies within a cell of its target.
TEST(LevelingKinds_juggernautFragment) {
    Scene s = drive(UnitKind::juggernaut);
    const int64_t red = add(s.sim, 990, UnitKind::ranger, s.at + s.out * 4, 1);
    const int64_t by = add(s.sim, 991, UnitKind::ranger, s.at + s.out * 4 + s.side() * 0.8, 1);
    const int64_t away = add(s.sim, 992, UnitKind::ranger, s.at + s.out * 4 + s.side() * 2.5, 1);
    give(s.sim, UnitKind::juggernaut, {Perk::juggernautFragment});
    s.sim.land(unit(s.sim, s.id), red, *s.sim.target(red));
    const double full = 10 * Rules::heroDamage;
    EXPECT_NEAR(unit(s.sim, red).hp, Rules::hp(UnitKind::ranger) - full, 1e-9);
    EXPECT_NEAR(unit(s.sim, by).hp, Rules::hp(UnitKind::ranger) - full / 2, 1e-9);
    EXPECT_EQ(unit(s.sim, away).hp, Rules::hp(UnitKind::ranger));
}

// MARK: - Dropship

/// Armor plates and Cargo bay on the driven Dropship, Triage nearby,
/// Lifeline and Efficient on every Dropship's energy, Nanite beam.
TEST(LevelingKinds_dropshipPicks) {
    Scene s = drive(UnitKind::dropship);
    const int64_t near = add(s.sim, 950, UnitKind::dropship, s.at + s.out * 3);
    const int64_t far = add(s.sim, 951, UnitKind::dropship, s.at + s.out * 20);
    const int64_t patient = add(s.sim, 960, UnitKind::ranger, s.at + s.out * 1);
    const int64_t machine = add(s.sim, 961, UnitKind::firefly, s.at + s.side() * 1.5);
    unitRef(s.sim, patient).hp = 20;
    unitRef(s.sim, machine).hp = 50;
    EXPECT_TRUE(!hasID(s.sim.patients(unit(s.sim, s.id)), machine));
    give(s.sim, UnitKind::dropship, {Perk::dropshipTriage, Perk::dropshipLifeline, Perk::dropshipArmorPlates,
                                     Perk::dropshipCargoBay, Perk::dropshipEfficient, Perk::dropshipNaniteBeam});
    EXPECT_EQ(s.sim.maxHP(unit(s.sim, s.id)), Rules::hp(UnitKind::dropship) + 65);
    EXPECT_EQ(s.sim.cargoSlots(unit(s.sim, s.id)), int64_t(12));
    EXPECT_EQ(s.sim.cargoSlots(unit(s.sim, near)), Rules::dropshipSlots);
    EXPECT_NEAR(s.sim.rate(Stat::Heal{}, unit(s.sim, near)), 1.1, 1e-12);
    EXPECT_EQ(s.sim.rate(Stat::Heal{}, unit(s.sim, far)), 1.0);
    EXPECT_NEAR(s.sim.rate(Stat::Energy{}, unit(s.sim, far)), 1.2, 1e-12);
    EXPECT_TRUE(hasID(s.sim.patients(unit(s.sim, s.id)), machine)); // Nanite beam
    Unit m = unit(s.sim, s.id);
    m.energy = 50;
    s.sim.heal(m, patient, 0.5);
    const double healed = Rules::healRate * 1.1 * 0.5;
    EXPECT_NEAR(unit(s.sim, patient).hp, 20 + healed, 1e-9);
    EXPECT_NEAR(m.energy.value_or(0), 50 - healed / (3 * 10.0 / 7), 1e-9); // 30% less energy
}

/// Double beam heals two at once; Cruise flies 50% faster while not
/// healing; Combat drop's units rush for 4 s.
TEST(LevelingKinds_dropshipDoubleBeamCruiseAndCombatDrop) {
    Scene s = drive(UnitKind::dropship);
    const int64_t a = add(s.sim, 960, UnitKind::ranger, s.at + s.out * 1);
    const int64_t b = add(s.sim, 961, UnitKind::ranger, s.at + s.side() * 1.5);
    const int64_t c = add(s.sim, 962, UnitKind::ranger, s.at - s.side() * 1.5);
    unitRef(s.sim, a).hp = 20;
    unitRef(s.sim, b).hp = 30;
    give(s.sim, UnitKind::dropship, {Perk::dropshipDoubleBeam, Perk::dropshipCombatDrop, Perk::dropshipCruise});
    EXPECT_NEAR(s.sim.rate(Stat::Speed{}, unit(s.sim, s.id)), Rules::heroSpeed * 1.5, 1e-12);
    if (s.sim.pilot) s.sim.pilot->hold = true;
    Unit m = unit(s.sim, s.id);
    m.energy = 100;
    std::vector<GameEvent> events;
    s.sim.pilotAct(m, PilotTarget::Heal{a}, true, 0.5, events);
    EXPECT_NEAR(unit(s.sim, a).hp, 20 + Rules::healRate * 0.5, 1e-9);
    EXPECT_NEAR(unit(s.sim, b).hp, 30 + Rules::healRate * 0.5, 1e-9); // the second beam
    EXPECT_TRUE(m.target == std::optional<int64_t>(a));
    unitRef(s.sim, s.id) = m;
    EXPECT_NEAR(s.sim.rate(Stat::Speed{}, m), Rules::heroSpeed, 1e-12); // healing: no cruise
    if (s.sim.pilot) s.sim.pilot->hold = false;
    EXPECT_TRUE(s.sim.board(s.id, c));
    run(s.sim, 0.3);
    EXPECT_NEAR(s.sim.rate(Stat::Speed{}, unit(s.sim, s.id)), Rules::heroSpeed * 1.5, 1e-12);
    m = unit(s.sim, s.id);
    s.sim.unload(m);
    unitRef(s.sim, s.id) = m;
    EXPECT_NEAR(s.sim.rate(Stat::Speed{}, unit(s.sim, c)), 1.3, 1e-12);
    EXPECT_NEAR(s.sim.rate(Stat::FireRate{}, unit(s.sim, c)), 1.3, 1e-12);
    run(s.sim, 4.1);
    EXPECT_EQ(s.sim.rate(Stat::Speed{}, unit(s.sim, c)), 1.0);
}

// MARK: - Longbow

/// Reactive armor and Long gun on the driven tank, Spotter nearby, Quick
/// anchor and Heavy shells on every tank, Heavy industry for the team.
/// The key still anchors.
TEST(LevelingKinds_longbowPicks) {
    Scene s = drive(UnitKind::longbow);
    const int64_t near = add(s.sim, 950, UnitKind::longbow, s.at + s.out * 3);
    const int64_t far = add(s.sim, 951, UnitKind::longbow, s.at + s.out * 20);
    give(s.sim, UnitKind::longbow, {Perk::longbowSpotter, Perk::longbowQuickAnchor, Perk::longbowReactiveArmor,
                                    Perk::longbowLongGun, Perk::longbowHeavyShells, Perk::longbowHeavyIndustry});
    EXPECT_EQ(s.sim.maxHP(unit(s.sim, s.id)), Rules::hp(UnitKind::longbow) + 80);
    EXPECT_NEAR(s.sim.rate(Stat::FireRate{}, unit(s.sim, near)), 1.1, 1e-12);
    EXPECT_EQ(s.sim.rate(Stat::FireRate{}, unit(s.sim, far)), 1.0);
    EXPECT_NEAR(s.sim.boost(Stat::Splash{}, unit(s.sim, far)).apply(1.0), 1.3, 1e-12);
    EXPECT_NEAR(s.sim.teamBoost(Stat::Production{StructureKind::foundry}, 0).rate(), 1.2, 1e-12);
    EXPECT_TRUE(s.sim.teamBoost(Stat::Production{StructureKind::spacedock}, 0) == Boost::none);
    EXPECT_TRUE(s.sim.pilotAbility(unit(s.sim, s.id)) ==
                std::optional<PilotAbility>(PilotAbility(PilotAbility::Action::Anchor{}, "Anchor Mode")));
    for (int64_t id : {s.id, near}) {
        unitRef(s.sim, id).anchor = 1;
        unitRef(s.sim, id).anchored = true;
    }
    EXPECT_NEAR(s.sim.weapon(unit(s.sim, s.id)).range, UnitStats::anchor.range * Rules::heroRange + 2, 1e-12);
    EXPECT_EQ(s.sim.weapon(unit(s.sim, near)).range, UnitStats::anchor.range);
    // Quick anchor: the far tank anchors in 1/1.2 of the time.
    unitRef(s.sim, far).anchored = true;
    run(s.sim, Rules::anchorTime / 1.2 - 0.1);
    EXPECT_TRUE(unit(s.sim, far).anchor.value_or(0) < 1);
    run(s.sim, 0.2);
    EXPECT_TRUE(unit(s.sim, far).anchor == std::optional<double>(1));
}

/// Hull down anchors in one step; Crew mechanic mends 3 hp a second
/// once 5 s pass without a hit.
TEST(LevelingKinds_longbowHullDownAndCrewMechanic) {
    Scene s = drive(UnitKind::longbow);
    give(s.sim, UnitKind::longbow, {Perk::longbowCrewMechanic, Perk::longbowHullDown});
    unitRef(s.sim, s.id).hp = 100;
    if (s.sim.pilot) s.sim.pilot->ability = true;
    // The key is read after the step's upkeep: anchored at the next.
    run(s.sim, 2.0 / 30);
    EXPECT_TRUE(unit(s.sim, s.id).anchor == std::optional<double>(1)); // at once
    EXPECT_TRUE(s.sim.pilotAbility(unit(s.sim, s.id))->title == "Tank Mode");
    run(s.sim, 1);
    EXPECT_NEAR(unit(s.sim, s.id).hp, 100 + 3 * (1 + 2.0 / 30), 0.05);
    s.sim.hit(s.id, 1);
    run(s.sim, 2);
    EXPECT_NEAR(unit(s.sim, s.id).hp, 100 + 3 * (1 + 2.0 / 30) - 1, 0.05); // not within 5 s of a hit
}
