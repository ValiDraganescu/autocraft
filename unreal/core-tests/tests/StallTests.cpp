// Port of the movement scenarios of Tests/GameCoreTests/StallTests.swift:
// units wedged on a corner, tanks turning beside an obstacle, an army
// settling at its rally, and the watchdog. Each a few seconds of
// simulation from a saved position (Tests/Fixtures), never a game. (The
// economy and army scenarios of that file are the Commander's.)
#include "SimHelpers.h"

#include "Session.h"
#include "WindowMaps.h"

#include <map>
#include <set>

using namespace ac;
using namespace simtest;

namespace {

/// A saved window game on The Ridge (small) from `Tests/Fixtures`.
struct Saved { std::optional<Simulation> sim; MapDefinition map; };

Saved saved(const std::string& name) {
    const std::string file = std::string(AC_REPO_ROOT) + "/unreal/core-tests/fixtures/" + name + ".json";
    std::string error;
    const auto session = SessionStore::load(file, &error);
    if (!session) {
        std::printf("  could not load %s: %s\n", file.c_str(), error.c_str());
        return Saved{std::nullopt, MapDefinition{}};
    }
    MapChoice choice;
    choice.style = MapStyle::ridge;
    choice.size = MapSize::small;
    const MapDefinition m = WindowMaps::build(choice);
    EXPECT_TRUE(session->mapName == m.name);
    return Saved{Simulation(session->state, m), m};
}

/// A saved window game (The Ridge, small) where Blue's last three Prospectors
/// stood wedged on a corner by their Derrick, with 45 ore and
/// 724 MH, and Red had 60 army supply and 5000+ ore banked.
Saved stalemate() { return saved("stalemate"); }

const std::vector<int64_t> blueProspectors{39, 166, 260};

} // namespace

// MARK: - Movement

/// A leg the grid calls clear never passes through a blocked cell, not
/// even across a corner: 4000 random legs among the saved game's
/// buildings and ore, each checked every 0.01.
TEST(stall_clearLegsNeverCrossABlockedCell) {
    auto [sim, m] = stalemate();
    ASSERT_TRUE(sim.has_value());
    const NavGrid& nav = *sim->nav;
    SeededRandom rng(7);
    int64_t checked = 0, bad = 0;
    const Vec2 centres[] = {Vec2(0.5, -0.5), Vec2(-33.6, -1.6), Vec2(22, -1)};
    for (int n = 0; n < 4000; n++) {
        // Swift evaluates arguments left to right; C++ need not.
        const Vec2 centre = centres[static_cast<int64_t>(rng.unit() * 3)];
        const double ax = rng.range(-9, 9);
        const double ay = rng.range(-9, 9);
        const Vec2 a = centre + Vec2(ax, ay);
        const double bx = rng.range(-4, 4);
        const double by = rng.range(-4, 4);
        const Vec2 b = a + Vec2(bx, by);
        if (!(nav.walkable(a) && nav.clear(a, b))) continue;
        checked += 1;
        const auto first = nav.index(a);
        for (int k = 0; k <= 100; k++) {
            const Vec2 p = a + (b - a) * (static_cast<double>(k) / 100);
            const auto i = nav.index(p);
            if (!(i && (i == first || nav.free(*i)))) bad += 1;
        }
    }
    EXPECT_EQ(bad, 0);
    EXPECT_TRUE(checked > 1000);
}

/// The saved game's three Prospectors, wedged on a corner beside their
/// Derrick, get in within a few seconds (they stood still for good).
TEST(stall_wedgedProspectorsReachTheirDerrick) {
    auto [sim, m] = stalemate();
    ASSERT_TRUE(sim.has_value());
    std::set<int64_t> reached;
    for (int k = 0; k < 6 * 30; k++) {
        sim->step(1.0 / 30);
        for (const int64_t id : blueProspectors) {
            const auto u = sim->state.unit(id);
            if (!u) continue;
            if (u->task == Unit::Task::inDerrick || u->hydrogen == true) reached.insert(id);
        }
    }
    // One at a time inside: in 6 s at least two have had their turn.
    EXPECT_TRUE(reached.size() >= 2);
}

/// Two of Blue's Longbows on attack-move, facing the attack point
/// with their next waypoint off to the side, beside a blocked cell: they
/// used to step sideways into it along their heading while turning, be
/// taken back, turn back toward the attack point, and never leave. Now
/// they drive off along their path.
TEST(stall_tanksTurningBesideAnObstacleDriveOff) {
    auto [sim, m] = saved("wedged-tanks");
    ASSERT_TRUE(sim.has_value());
    const std::vector<int64_t> tanks{117, 151};
    std::map<int64_t, Vec2> start;
    for (const auto id : tanks) start[id] = sim->state.unit(id)->position;
    const auto events = run(*sim, 4);
    for (const auto id : tanks) EXPECT_TRUE(distance(sim->state.unit(id)->position, start[id]) > 2); // drove off
    for (const auto& e : events) {
        if (auto s = e.as<GameEvent::Stuck>()) EXPECT_TRUE(s->unit != 117 && s->unit != 151); // without the watchdog's help
    }
}

/// Blue's army at its rally, three tanks anchored among the Rangers: the
/// others walking to their places used to push against the planted
/// tanks (their places overlapped) until the watchdog freed them, again
/// and again. With tanks placed behind the rest, the army reshuffles
/// once (saved with the old places, a Ranger or two get freed while the
/// tanks pack up), is at its places by 12 s, and nobody is stuck again
/// in the 22 s after the reshuffle.
TEST(stall_armySettlesAtItsRallyAroundAnchoredTanks) {
    auto [sim, m] = saved("rally-jam");
    ASSERT_TRUE(sim.has_value());
    sim->commanders = Commander::all(m);
    // At home: under the fog of war neither side sees the other's
    // army to call its attack off, so both hold.
    for (int64_t p = 0; p < static_cast<int64_t>(sim->state.players.size()); p++) {
        sim->state.players[size_t(p)].attack = std::nullopt;
        sim->issue(Command::Stance{p, Stance::hold});
    }
    std::vector<int64_t> late;
    for (int k = 0; k < 30 * 30; k++) {
        for (const auto& e : sim->step(1.0 / 30)) {
            if (auto s = e.as<GameEvent::Stuck>(); k >= 8 * 30 && s && s->owner == 0) late.push_back(s->unit);
        }
        if (!(k == 12 * 30 && !sim->state.players[0].attack)) continue;
        for (const auto& u : sim->state.units) {
            if (!(u.owner == 0 && sim->inArmy(u) && !u.stats().air)) continue;
            const double slack = u.anchor.value_or(0) > 0 ? Simulation::plantedSlack : 0.6;
            const auto slot = sim->slot(u);
            EXPECT_TRUE(slot && distance(u.position, *slot) < slack + 0.01); // at its place
        }
    }
    EXPECT_TRUE(late.empty()); // Blue units stuck at the rally after the reshuffle
}

/// The watchdog: a unit with somewhere to go that makes no headway for
/// `stuckTime` steps to its cell's centre, looks for a new way, and is
/// reported once.
TEST(stall_unitMakingNoHeadwayIsSetFree) {
    auto [sim, m] = stalemate();
    ASSERT_TRUE(sim.has_value());
    const int64_t id = blueProspectors[0];
    size_t i = 0;
    while (i < sim->state.units.size() && sim->state.units[i].id != id) i++;
    ASSERT_TRUE(i < sim->state.units.size());
    sim->state.units[i].goal = Vec2(20, 0);
    sim->state.units[i].waypoints = std::vector<Vec2>{Vec2(20, 0)};
    // (Nothing steps here, so every unit on its way counts as stuck:
    // look at this one.)
    auto mine = [&](const std::vector<GameEvent>& e) {
        std::vector<GameEvent::Stuck> out;
        for (const auto& x : e) if (auto s = x.as<GameEvent::Stuck>(); s && s->unit == id) out.push_back(*s);
        return out;
    };
    std::vector<GameEvent> events;
    sim->unstick(events);
    EXPECT_TRUE(mine(events).empty());
    sim->state.time += Simulation::stuckTime + 0.1;
    sim->unstick(events);
    const Unit u = sim->state.units[i];
    const auto stuck = mine(events);
    EXPECT_EQ(stuck.size(), size_t(1));
    ASSERT_TRUE(!stuck.empty());
    EXPECT_TRUE(stuck[0].kind == Unit::Kind::prospector && stuck[0].owner == 0);
    EXPECT_TRUE(!u.goal.has_value());
    EXPECT_TRUE(!u.waypoints.has_value());
    EXPECT_TRUE(std::optional<Vec2>(u.position) == sim->nav->freeCentre(u.position));
    // Still stuck later: set free again, but not reported twice.
    sim->state.units[i].goal = Vec2(20, 0);
    sim->state.time += Simulation::stuckTime + 0.1;
    events.clear();
    sim->unstick(events);
    sim->state.time += Simulation::stuckTime + 0.1;
    sim->unstick(events);
    EXPECT_TRUE(mine(events).empty());
    EXPECT_TRUE(!sim->state.units[i].goal.has_value()); // set free again
}
