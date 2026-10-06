// Port of Tests/GameCoreTests/FightTests.swift: short fights, a retreating
// army that meets a straggler, Longbows choosing their mode, the mini gun,
// micro and regrouping. A few seconds of simulation or one `army` call
// each, from a hand-built position, never a game.
#include "SimHelpers.h"

using namespace ac;
using namespace simtest;

namespace {

/// Both Citadels, nothing else; `dir` points from Blue's toward Red's.
struct Field { GameState s; Vec2 blue, red, dir; };

Field field() {
    GameState s = GameState::new_(homeMap());
    s.units.clear();
    s.time = 600;
    Vec2 blue, red;
    for (const auto& b : s.structures) if (b.owner == 0) { blue = b.position; break; }
    for (const auto& b : s.structures) if (b.owner == 1) { red = b.position; break; }
    return Field{s, blue, red, normalize(red - blue)};
}

int64_t add(GameState& s, Unit::Kind kind, Vec2 p, int64_t owner, Unit::Task task = Unit::Task::idle) {
    return addUnit(s, kind, p, owner, task);
}
int64_t add(GameState& s, Structure::Kind kind, Vec2 p, int64_t owner) { return addStructure(s, kind, p, owner); }

} // namespace

// MARK: - Retreat

/// Ten retreating Rangers walking home past a lone enemy Ranger shoot it
/// in passing, and keep walking home rather than chase it.
TEST(fight_retreatingSoldiersShootInPassing) {
    auto [s, blue, red, dir] = field();
    const Vec2 side(-dir.y, dir.x);
    // Red's Rangers halfway across, the blue Ranger just beside their way home.
    const Vec2 mid = (blue + red) / 2;
    std::vector<int64_t> reds;
    for (int64_t k = 0; k < 10; k++) {
        reds.push_back(add(s, Unit::Kind::ranger,
                           mid + side * (static_cast<double>(k % 5) * 0.8 - 1.6) + dir * static_cast<double>(k / 5) * 0.8, 1,
                           Unit::Task::toRally));
    }
    const int64_t lone = add(s, Unit::Kind::ranger, mid + side * 4.5 + dir * 2, 0);
    s.players[1].retreating = true;
    s.players[1].retreatedAt = s.time;
    Simulation sim(s, homeMap());
    const double home = distance(mid, red);
    const auto events = run(sim, 3);
    int64_t fired = 0;
    for (auto id : reds) fired += shots(events, id);
    EXPECT_TRUE(fired > 0); // they shot at it
    EXPECT_TRUE(hp(sim, lone) < Rules::hp(Unit::Kind::ranger)); // and hit it
    std::vector<Unit> left;
    for (auto id : reds) if (auto u = sim.state.unit(id)) left.push_back(*u);
    ASSERT_TRUE(!left.empty());
    double gain = 0;
    for (const auto& u : left) gain += home - distance(u.position, red);
    gain /= static_cast<double>(left.size());
    EXPECT_TRUE(gain > 2); // they kept walking home
    for (const auto& u : left) EXPECT_TRUE(u.task == Unit::Task::toRally || u.task == Unit::Task::idle); // no chasing
}

/// A retreating army with only a straggler near it turns on it: an
/// attack at the straggler.
TEST(fight_retreatTurnsOnAWeakPursuer) {
    auto [s, blue, red, dir] = field();
    const Vec2 mid = (blue + red) / 2;
    for (int64_t k = 0; k < 10; k++) {
        add(s, Unit::Kind::ranger, mid + Vec2(static_cast<double>(k % 5) * 0.8, static_cast<double>(k / 5) * 0.8), 1, Unit::Task::toRally);
    }
    const Vec2 straggler = mid - dir * 5;
    add(s, Unit::Kind::ranger, straggler, 0);
    s.players[1].retreating = true;
    s.players[1].retreatedAt = s.time - 1;
    const auto orders = Commander(homeMap(), 1).army(Simulation(s, homeMap()));
    EXPECT_TRUE(orders == std::vector<Command>{Command::Attack{1, straggler}});
}

/// Pursuers worth half the army or more keep it falling back, even past
/// `retreatTime`.
TEST(fight_retreatHoldsWhileRealPursuersChase) {
    auto [s, blue, red, dir] = field();
    const Vec2 mid = (blue + red) / 2;
    for (int64_t k = 0; k < 10; k++) {
        add(s, Unit::Kind::ranger, mid + Vec2(static_cast<double>(k % 5) * 0.8, static_cast<double>(k / 5) * 0.8), 1, Unit::Task::toRally);
    }
    for (int64_t k = 0; k < 6; k++) add(s, Unit::Kind::ranger, mid - dir * 6 + Vec2(static_cast<double>(k) * 0.8, 0), 0);
    s.players[1].retreating = true;
    s.players[1].retreatedAt = s.time - Commander::retreatTime - 5;
    EXPECT_TRUE(Commander(homeMap(), 1).army(Simulation(s, homeMap())).empty());
}

// MARK: - Longbows

/// A tank on attack-move with an enemy building 10 cells ahead (beyond
/// tank range, inside anchor range) anchors up and shells it. It used to
/// start anchoring, pack up to walk on, and start again, never firing.
TEST(fight_tankAnchorsAndFiresBeyondTankRange) {
    auto [s, blue, red, dir] = field();
    const Vec2 at = blue + dir * 9;
    const int64_t tank = add(s, Unit::Kind::longbow, at, 0, Unit::Task::attackMove);
    const int64_t target = add(s, Structure::Kind::garrison,
                               at + dir * (10 + Rules::radius(Unit::Kind::longbow) + Rules::radius(Structure::Kind::garrison)), 1);
    s.players[0].attack = s.structure(target)->position;
    Simulation sim(s, homeMap());
    const auto events = run(sim, 6);
    EXPECT_TRUE(shots(events, tank) > 0); // it fired
    EXPECT_TRUE(sim.state.unit(tank)->anchor == 1.0); // anchored
    EXPECT_TRUE(sim.state.structure(target)->hp < Rules::hp(Structure::Kind::garrison));
}

/// An anchored tank with an enemy Ranger right on top of it (inside its
/// minimum range) and nothing else about packs up and fights in tank
/// mode.
TEST(fight_anchoredTankPacksUpWhenOnlyCrowded) {
    auto [s, blue, red, dir] = field();
    const Vec2 at = blue + dir * 9;
    const int64_t tank = add(s, Unit::Kind::longbow, at, 0);
    s.units.back().anchor = 1;
    s.units.back().anchored = true;
    const double gap = Rules::radius(Unit::Kind::longbow) + Rules::radius(Unit::Kind::ranger) + 0.8;
    const int64_t ranger = add(s, Unit::Kind::ranger, at + dir * gap, 1);
    Simulation sim(s, homeMap());
    const auto events = run(sim, 4.5);
    EXPECT_TRUE(sim.state.unit(tank)->anchor == 0.0); // packed up
    EXPECT_TRUE(shots(events, tank) > 0); // then fired in tank mode
    EXPECT_TRUE(hp(sim, ranger) < Rules::hp(Unit::Kind::ranger));
}

/// A tank anchored out of range of everything, its army attacking a
/// building 16 cells off that a Ranger of its side spots (a tank sees
/// 11, less than its anchor range), packs up, drives closer, anchors
/// again and shells it.
TEST(fight_anchoredTankOutOfRangeMovesCloser) {
    auto [s, blue, red, dir] = field();
    const Vec2 at = blue + dir * 6;
    const int64_t tank = add(s, Unit::Kind::longbow, at, 0, Unit::Task::attackMove);
    s.units.back().anchor = 1;
    s.units.back().anchored = true;
    const int64_t target = add(s, Structure::Kind::garrison,
                               at + dir * (16 + Rules::radius(Unit::Kind::longbow) + Rules::radius(Structure::Kind::garrison)), 1);
    add(s, Unit::Kind::ranger, s.structure(target)->position + dir * 4, 0, Unit::Task::attackMove);
    s.players[0].attack = s.structure(target)->position;
    Simulation sim(s, homeMap());
    const auto events = run(sim, 12);
    EXPECT_TRUE(shots(events, tank) > 0); // it moved up and fired
    EXPECT_TRUE(sim.state.structure(target)->hp < Rules::hp(Structure::Kind::garrison));
}

// MARK: - Mini gun

namespace {

/// A Ranger 3 cells from an enemy Hab Dome that cannot fall (`minigun`:
/// its side has the upgrade); the shot times over `seconds`, and the
/// damage done.
struct Volley { std::vector<double> times; double damage; };

Volley volley(bool minigun, double seconds) {
    auto [s, blue, red, dir] = field();
    const Vec2 at = blue + dir * 9;
    const int64_t ranger = add(s, Unit::Kind::ranger, at, 0);
    const int64_t habDome = add(s, Structure::Kind::habDome,
                                at + dir * (3 + Rules::radius(Unit::Kind::ranger) + Rules::radius(Structure::Kind::habDome)), 1);
    s.structures.back().hp = 1e5;
    if (minigun) s.players[0].upgrades = std::set<Upgrade>{Upgrade::minigun};
    Simulation sim(s, homeMap());
    std::vector<double> times;
    for (int64_t k = 0; k < static_cast<int64_t>(seconds * 30); k++) {
        for (const auto& e : sim.step(1.0 / 30)) {
            if (auto sh = e.as<GameEvent::Shot>(); sh && sh->unit == ranger) times.push_back(sim.state.time);
        }
    }
    return Volley{times, 1e5 - sim.state.structure(habDome)->hp};
}

} // namespace

/// The mini gun fires five rounds a second for 3 s, then pauses 1 s to
/// reload, again and again.
TEST(fight_minigunFiresThreeSecondBurstsThenPauses) {
    const auto times = volley(true, 10).times;
    std::vector<double> gaps;
    for (size_t k = 1; k < times.size(); k++) gaps.push_back(times[k] - times[k - 1]);
    std::vector<size_t> pauses;
    for (size_t k = 0; k < gaps.size(); k++) if (gaps[k] > 0.5) pauses.push_back(k);
    ASSERT_TRUE(pauses.size() >= 2); // two reloads in 10 s
    for (const double g : gaps) if (g <= 0.5) EXPECT_NEAR(g, 0.2, 0.034); // five rounds a second
    for (const size_t k : pauses) EXPECT_NEAR(gaps[k], 1.2, 0.07); // a 1 s pause after the burst
    // A whole burst: from the round after the first pause to the next pause.
    const double burst = times[pauses[1]] - times[pauses[0] + 1];
    EXPECT_NEAR(burst + 0.2, 3, 0.07); // 3 s of fire
}

/// Over 12 s the mini gun does well over the rifle's damage, on armour too.
TEST(fight_minigunOutdamagesTheRifle) {
    const double rifle = volley(false, 12).damage;
    const double minigun = volley(true, 12).damage;
    EXPECT_TRUE(rifle > 0);
    EXPECT_TRUE(minigun > rifle * 1.25);
}

// MARK: - Micro

/// Rangers facing two enemies in range all pick the hurt one (focus
/// fire); with none in range, the closest.
TEST(fight_focusFireOnTheWeakest) {
    auto [s, blue, red, dir] = field();
    const Vec2 side(-dir.y, dir.x);
    const Vec2 at = blue + dir * 10;
    std::vector<int64_t> ours;
    for (int64_t k = 0; k < 3; k++) ours.push_back(add(s, Unit::Kind::ranger, at + side * (static_cast<double>(k) - 1) * 0.8, 0));
    const int64_t healthy = add(s, Unit::Kind::ranger, at + dir * 4 - side, 1);
    const int64_t weak = add(s, Unit::Kind::ranger, at + dir * 4.5 + side, 1);
    s.units.back().hp = 15;
    Simulation sim(s, homeMap());
    for (auto u : ours) EXPECT_TRUE(sim.acquire(*sim.state.unit(u)) == weak); // all on the weak one
    // Both out of range: the closer first.
    const int64_t far = add(sim.state, Unit::Kind::ranger, at - dir * 2, 0);
    sim = Simulation(sim.state, homeMap());
    EXPECT_TRUE(sim.acquire(*sim.state.unit(far)) == healthy);
}

/// A Juggernaut (range 6) kites a Ranger (range 5): it backs away
/// between shots and keeps firing.
TEST(fight_outrangingSoldierKites) {
    auto [s, blue, red, dir] = field();
    const Vec2 at = blue + dir * 10;
    const int64_t jug = add(s, Unit::Kind::juggernaut, at, 0);
    add(s, Unit::Kind::ranger, at + dir * 6, 1);
    s.units.back().hp = 1e4;
    s.players[1].attack = at;
    Simulation sim(s, homeMap());
    const auto events = run(sim, 4);
    const Unit j = *sim.state.unit(jug);
    EXPECT_TRUE(shots(events, jug) >= 3); // it kept firing
    EXPECT_TRUE(dot(at - j.position, dir) > 1.5); // it backed away from the Ranger
}

/// Rangers do not kite Rangers (same range): they stand and fire.
TEST(fight_evenRangeStandsAndFights) {
    auto [s, blue, red, dir] = field();
    const Vec2 at = blue + dir * 10;
    const int64_t ours = add(s, Unit::Kind::ranger, at, 0);
    add(s, Unit::Kind::ranger, at + dir * 4.5, 1);
    s.units.back().hp = 1e4;
    s.players[1].attack = at;
    Simulation sim(s, homeMap());
    run(sim, 2);
    EXPECT_TRUE(distance(sim.state.unit(ours)->position, at) < 0.5);
}

/// A badly hurt Ranger being shot, with healthy friends beside it,
/// steps out of the line.
TEST(fight_woundedSoldierStepsBack) {
    auto [s, blue, red, dir] = field();
    const Vec2 side(-dir.y, dir.x);
    const Vec2 at = blue + dir * 10;
    const int64_t hurt = add(s, Unit::Kind::ranger, at, 0);
    s.units.back().hp = 12;
    for (int64_t k = 0; k < 4; k++) add(s, Unit::Kind::ranger, at + side * (static_cast<double>(k) - 1.5) * 0.9 - dir * 0.8, 0);
    add(s, Unit::Kind::juggernaut, at + dir * 6.5, 1);
    s.units.back().hp = 1e4;
    s.players[1].attack = at;
    Simulation sim(s, homeMap());
    // Its grenade is in the air most of a second; then the Ranger
    // backs off toward home while its friends close in.
    double back = 0.0;
    std::optional<Vec2> hitAt;
    for (int k = 0; k < 72; k++) {
        sim.step(1.0 / 30);
        const auto u = sim.state.unit(hurt);
        if (!u) break;
        if (!hitAt && u->hp < 12) hitAt = u->position;
        if (hitAt) back = ac::max(back, dot(*hitAt - u->position, dir));
    }
    EXPECT_TRUE(back > 1); // it stepped back from the Juggernaut
}

// MARK: - Regroup

/// An attack strung out on its way (half the army well behind) with
/// no fight on gathers on its middle first, waits there until most of
/// it is in, then goes on to the enemy base.
TEST(fight_strungOutAttackRegroupsFirst) {
    auto [s, blue, red, dir] = field();
    const Vec2 side(-dir.y, dir.x);
    const double far = distance(blue, red);
    for (int64_t k = 0; k < 12; k++) {
        const double along = k < 6 ? 0.2 * far : 0.55 * far;
        add(s, Unit::Kind::ranger, blue + dir * (along + static_cast<double>(k % 2)) + side * static_cast<double>(k % 3), 0,
            Unit::Task::attackMove);
    }
    s.players[0].attack = red;
    const Commander c(homeMap());
    const auto orders = c.army(Simulation(s, homeMap()));
    ASSERT_TRUE(!orders.empty());
    const auto* first = orders.front().as<Command::Attack>();
    ASSERT_TRUE(first && first->player == 0 && first->at.has_value()); // a regroup
    const Vec2 gather = *first->at;
    EXPECT_TRUE(distance(gather, red) < far - 5); // on the way
    EXPECT_TRUE(std::any_of(s.units.begin(), s.units.end(), [&](const Unit& u) { return u.position == gather; })); // on one of its soldiers
    // Waiting at the gathering point while strung out.
    s.players[0].attack = gather;
    EXPECT_TRUE(c.army(Simulation(s, homeMap())).empty()); // waits for the rest
    // All in: on to the base.
    for (size_t i = 0; i < s.units.size(); i++) {
        s.units[i].position = gather + side * static_cast<double>(i % 4) * 0.8 + dir * static_cast<double>(i / 4) * 0.8;
    }
    EXPECT_TRUE(c.army(Simulation(s, homeMap())) == std::vector<Command>{Command::Attack{0, red}});
}

// MARK: - High ground

/// An attacking army meeting enemies a little weaker than its retreat
/// threshold: on the level it fights on, but the same enemies on
/// higher ground send it back.
TEST(fight_armyWillNotFightUphill) {
    auto [s, blue, red, dir] = field();
    const MapDefinition& m = homeMap();
    const TerrainField terrain(m);
    const Simulation nav(s, m);
    // A low spot with higher ground 4–6 cells off, both open.
    std::optional<std::pair<Vec2, Vec2>> pair;
    for (int64_t xi = 0; xi < strideCount(-40.0, 40, 2) && !pair; xi++) {
        const double x = -40.0 + static_cast<double>(xi) * 2;
        for (int64_t zi = 0; zi < strideCount(-40.0, 40, 2) && !pair; zi++) {
            const double z = -40.0 + static_cast<double>(zi) * 2;
            const Vec2 low(x, z);
            if (!(nav.nav && nav.nav->walkable(low))) continue;
            // `stride(from: 0, to: 6.28, by: 0.4)`.
            for (int64_t ai = 0; static_cast<double>(ai) * 0.4 < 6.28; ai++) {
                const double a = static_cast<double>(ai) * 0.4;
                const Vec2 high = low + Vec2(std::cos(a), std::sin(a)) * 5;
                if (nav.nav->walkable(high) && terrain.level(high) > terrain.level(low)) { pair = std::pair<Vec2, Vec2>{low, high}; break; }
            }
        }
    }
    ASSERT_TRUE(pair.has_value()); // a slope on the map
    const auto [low, high] = *pair;
    for (int64_t k = 0; k < 10; k++) {
        add(s, Unit::Kind::ranger, low + Vec2(static_cast<double>(k % 5) - 2, static_cast<double>(k / 5)) * 0.8, 0, Unit::Task::attackMove);
    }
    s.players[0].attack = high;
    s.players[0].aggression = 0;
    const Commander c(m);
    auto retreats = [&](Vec2 p) {
        GameState t = s;
        for (int64_t k = 0; k < 7; k++) add(t, Unit::Kind::ranger, p + Vec2(static_cast<double>(k % 4) - 1.5, static_cast<double>(k / 4)) * 0.7, 1);
        return c.army(Simulation(t, m)) == std::vector<Command>{Command::Retreat{0}};
    };
    EXPECT_TRUE(!retreats(low + (low - high) * 0.6)); // level ground: fights
    EXPECT_TRUE(retreats(high)); // uphill: falls back
}
