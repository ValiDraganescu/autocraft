// C++ only: the fast paths option (Simulation::setFastPaths; PORTING.md "Fast
// paths"). It changes results on purpose, so the golden and parity tests
// run with it off; these cover what it must keep: valid routes, units that
// arrive, the same game run to run (also under PathAhead), and the right
// set of units sent to find a new way after the grid changes.
#include "SimHelpers.h"

#include "Commander.h"
#include "PathAhead.h"
#include "Session.h"
#include "WindowMaps.h"

#include <cstring>

using namespace ac;
using namespace simtest;

namespace {

std::optional<Simulation> fastGame(bool fast) {
    const std::string file = std::string(AC_REPO_ROOT) + "/bench/badlands-large.json";
    std::string error;
    const auto session = SessionStore::load(file, &error);
    if (!session) return std::nullopt;
    const MapDefinition m = WindowMaps::build(MapChoice{MapStyle::badlands, MapSize::large});
    Simulation sim(session->state, m, Commander::all(m), true);
    for (Unit& u : sim.state.units) {
        if (!u.soldier() || u.stats().air) continue;
        const int64_t foe = u.owner == 0 ? 1 : 0;
        const int64_t base = sim.state.players[size_t(foe)].start.value_or(m.starts[size_t(foe)]);
        u.mission = Mission{Mission::Raid{m.bases[size_t(base)].center}};
    }
    if (fast) sim.setFastPaths(true, 32);
    return sim;
}

bool same(const Simulation& a, const Simulation& b) {
    if (a.state.units.size() != b.state.units.size() || a.state.time != b.state.time) return false;
    for (size_t i = 0; i < a.state.units.size(); i++) {
        const Unit& u = a.state.units[i];
        const Unit& v = b.state.units[i];
        if (u.id != v.id || std::memcmp(&u.position, &v.position, sizeof u.position) != 0 || std::memcmp(&u.hp, &v.hp, sizeof u.hp) != 0
            || u.task != v.task || u.waypoints != v.waypoints || u.goal != v.goal) return false;
    }
    return true;
}

double length(Vec2 from, const std::vector<Vec2>& way) {
    double total = 0;
    for (Vec2 p : way) { total += distance(from, p); from = p; }
    return total;
}

} // namespace

TEST(fastPaths_routesAreValidAndNotMuchLonger) {
    std::optional<Simulation> game = fastGame(false);
    ASSERT_TRUE(game.has_value());
    const NavGrid& slow = *game->nav;
    NavGrid fast = slow;
    fast.fastSearch = true;
    // Free cells from a fixed sequence.
    uint64_t seed = 12345;
    auto pick = [&] {
        while (true) {
            seed = seed * 6364136223846793005ull + 1442695040888963407ull;
            const int64_t i = static_cast<int64_t>((seed >> 33) % uint64_t(slow.width * slow.height));
            if (slow.free(i) && slow.regions[size_t(i)] == slow.mainRegion) return slow.centre(i);
        }
    };
    int64_t checked = 0;
    double worst = 0;
    for (int k = 0; k < 150; k++) {
        const Vec2 a = pick(), b = pick();
        const auto p0 = slow.path(a, b, 0.1);
        const auto p1 = fast.path(a, b, 0.1);
        EXPECT_TRUE(p0.has_value() == p1.has_value());
        if (!p0 || !p1) continue;
        // Every leg clear, and the last waypoint within reach of the target.
        Vec2 at = a;
        bool legsClear = true;
        for (Vec2 w : *p1) { legsClear = legsClear && fast.clear(at, w); at = w; }
        EXPECT_TRUE(legsClear);
        EXPECT_TRUE(p1->empty() || distance(p1->back(), b) <= 0.1 + NavGrid::cell * 0.75 + 1e-9);
        const double l0 = length(a, *p0), l1 = length(a, *p1);
        worst = std::max(worst, l1 / std::max(l0, 1.0));
        EXPECT_TRUE(l1 <= l0 * (NavGrid::fastWeight + 0.05) + 1.0);
        checked++;
    }
    EXPECT_TRUE(checked > 100);
    std::printf("  (%lld routes, fast at most %.3f times the shortest)\n", (long long)checked, worst);
}

TEST(fastPaths_unitsArriveThroughTheBudget) {
    std::optional<Simulation> game = fastGame(false);
    ASSERT_TRUE(game.has_value());
    Simulation sim = *game;
    sim.setFastPaths(true, 2);
    const NavGrid& nav = *sim.nav;
    // 12 rangers around blue's main, each sent somewhere far across the open map.
    std::vector<Unit> units;
    const Vec2 home = sim.state.structures[0].position;
    for (int k = 0; k < 12; k++) {
        const Vec2 at = nav.nearestFree(home + Vec2(double(k % 4) * 1.5 - 2, double(k / 4) * 1.5 + 4)).value();
        units.push_back(Unit(1000 + k, Unit::Kind::ranger, 0, at, 0, Unit::Task::idle));
    }
    ASSERT_TRUE(units.size() == 12);
    std::vector<Vec2> targets;
    for (const Unit& u : units) {
        // The free open-map cell farthest along a ring, so every route is long.
        Vec2 best = u.position;
        for (int64_t i = 0; i < nav.width * nav.height; i += 997) {
            if (nav.free(i) && nav.regions[size_t(i)] == nav.mainRegion && distance(nav.centre(i), u.position) < 90
                && distance(nav.centre(i), u.position) > distance(best, u.position)) best = nav.centre(i);
        }
        targets.push_back(best);
    }
    int64_t mostStarted = 0;
    int64_t steps = 0;
    std::vector<bool> arrived(units.size(), false);
    for (; steps < 60 * 90; steps++) {
        sim.pathsStarted = 0;
        bool all = true;
        for (size_t k = 0; k < units.size(); k++) {
            if (arrived[k]) continue;
            arrived[k] = sim.travel(units[k], targets[k], 0.1, 1.0 / 30);
            all = all && arrived[k];
        }
        mostStarted = std::max(mostStarted, sim.pathsStarted);
        if (all) break;
    }
    EXPECT_TRUE(mostStarted <= 2);
    EXPECT_TRUE(mostStarted >= 1);
    for (size_t k = 0; k < units.size(); k++) {
        EXPECT_TRUE(arrived[k]);
        EXPECT_TRUE(distance(units[k].position, targets[k]) < 0.5);
    }
}

TEST(fastPaths_sameGameRunToRunAndUnderPathAhead) {
    std::optional<Simulation> a = fastGame(true);
    ASSERT_TRUE(a.has_value());
    Simulation b = *a;
    Simulation c = *a;
    PathAhead paths(3);
    bool alike = true;
    for (int k = 0; k < 4 * 60 && alike; k++) {
        a->pathsStarted = 0;
        const size_t e1 = a->step(1.0 / 60).size();
        const size_t e2 = b.step(1.0 / 60).size();
        const size_t e3 = paths.step(c, 1.0 / 60).size();
        alike = e1 == e2 && e1 == e3 && same(*a, b) && same(*a, c);
    }
    EXPECT_TRUE(alike);
    // Not the Swift way: the fast game really differs from the exact one.
    std::optional<Simulation> exact = fastGame(false);
    ASSERT_TRUE(exact.has_value());
    for (int k = 0; k < 4 * 60; k++) (void)exact->step(1.0 / 60);
    EXPECT_TRUE(!same(*a, *exact));
}

TEST(fastPaths_aBuildingSendsOnlyTheUnitsItCrossesToFindAWay) {
    const MapDefinition& m = homeMap();
    GameState state = blueOnly(GameState::new_(m));
    const Structure citadel = state.structures[0];
    const auto spotOpt = Commander(m).garrisonSpot(state, citadel);
    ASSERT_TRUE(spotOpt.has_value());
    const Vec2 spot = *spotOpt;
    const Vec2 dir = normalize(spot - citadel.position);
    const Vec2 side(-dir.y, dir.x);
    auto add = [&](Vec2 at) {
        const int64_t id = addUnit(state, Unit::Kind::ranger, at, 0, Unit::Task::toRally);
        return size_t(state.units.size() - 1) + 0 * size_t(id);
    };
    // Through the spot; waypoints cross it; a last straight leg crosses it;
    // alongside it; past it; one already waiting with no goal.
    const size_t through = add(spot - dir * 6);
    const size_t leg = add(spot - dir * 6 + side * 0.5);
    const size_t alongside = add(spot - dir * 6 + side * 12);
    const size_t away = add(spot + side * 20);
    const size_t idle = add(spot - dir * 6 - side * 0.5);
    Simulation sim(state, m);
    sim.setFastPaths(true);
    auto route = [&](size_t i, std::vector<Vec2> way, Vec2 goal) {
        sim.state.units[i].waypoints = std::move(way);
        sim.state.units[i].goal = goal;
    };
    route(through, {spot - dir * 3, spot + dir * 3, spot + dir * 8}, spot + dir * 9);
    route(leg, {}, spot + dir * 7);
    route(alongside, {spot - dir * 2 + side * 12, spot + dir * 8 + side * 12}, spot + dir * 9 + side * 12);
    route(away, {spot + side * 30}, spot + side * 31);
    sim.state.units[idle].goal = std::nullopt;
    sim.state.units[idle].waypoints = std::vector<Vec2>{spot + dir * 3};
    const auto before = sim.state.units;
    sim.state.structures.push_back(Structure(sim.state.nextID, Structure::Kind::garrison, 0, spot));
    sim.state.nextID += 1;
    sim.refreshNav();
    EXPECT_TRUE(!sim.state.units[through].goal.has_value());
    EXPECT_TRUE(!sim.state.units[leg].goal.has_value());
    EXPECT_TRUE(sim.state.units[alongside].goal == before[alongside].goal);
    EXPECT_TRUE(sim.state.units[alongside].waypoints == before[alongside].waypoints);
    EXPECT_TRUE(sim.state.units[away].goal == before[away].goal);
    EXPECT_TRUE(sim.state.units[away].waypoints == before[away].waypoints);
    EXPECT_TRUE(sim.state.units[idle].waypoints == before[idle].waypoints);
    // The same change, slow: everyone who walks asks again.
    Simulation slow(state, m);
    for (size_t i : {through, leg, alongside, away}) { slow.state.units[i].goal = before[i].goal; slow.state.units[i].waypoints = before[i].waypoints; }
    slow.state.structures.push_back(Structure(slow.state.nextID, Structure::Kind::garrison, 0, spot));
    slow.refreshNav();
    EXPECT_TRUE(!slow.state.units[alongside].goal.has_value());
    EXPECT_TRUE(!slow.state.units[away].goal.has_value());
    // A walker standing where the building goes up is pushed out and asks again.
    Simulation pushed(state, m);
    pushed.setFastPaths(true);
    pushed.state.units[through].goal = spot + dir * 9;
    pushed.state.units[through].position = spot;
    pushed.state.units[through].waypoints = std::vector<Vec2>{spot + dir * 20};
    pushed.state.structures.push_back(Structure(pushed.state.nextID, Structure::Kind::garrison, 0, spot));
    pushed.refreshNav();
    EXPECT_TRUE(!NavGrid::solid(pushed.state.units[through].position, pushed.state));
    EXPECT_TRUE(!pushed.state.units[through].goal.has_value());
}
