// The classic RTS worker pull (`Commander::pullBack`, C++ only): Prospectors run a
// short way from a base with enemy fighters on it and go back to their own
// patches once it is calm (the user's report, 2026-10-06: they ran to another
// base and stayed there). Isolated scenarios of a few seconds, never a game.
#include "SimHelpers.h"

using namespace ac;
using namespace simtest;

namespace {

/// Blue's main and a second Citadel at the nearest other site, its
/// Prospectors at work for a few seconds, and one Red Ranger in the main's
/// ore line. Red has nothing else (no Commander moves it).
struct Raid {
    std::optional<Simulation> sim;
    Structure main;
    int64_t site = 0;
    int64_t ranger = 0;
    std::vector<int64_t> workers;
};

Raid raid(bool pullBack) {
    const MapDefinition& m = homeMap();
    GameState s = blueOnly(GameState::new_(m));
    Raid r;
    r.main = s.structures[0];
    std::optional<Vec2> natural;
    for (const BaseSite& b : m.bases) {
        if (!(distance(b.center, r.main.position) > 5)) continue;
        if (!natural || distance(b.center, r.main.position) < distance(*natural, r.main.position)) natural = b.center;
    }
    addStructure(s, Structure::Kind::citadel, *natural, 0);
    s.units.clear();
    for (int k = 0; k < 6; k++) addUnit(s, Unit::Kind::prospector, r.main.position + Vec2(3 + k * 0.5, 3), 0);
    std::vector<Commander> blue{Commander(m, 0)};
    blue[0].pullBack = pullBack;
    r.sim.emplace(s, m, blue);
    Simulation& sim = *r.sim;
    r.site = sim.site(r.main);
    run(sim, 6);
    for (const Unit& u : sim.state.units) if (u.kind == Unit::Kind::prospector) r.workers.push_back(u.id);
    // The Ranger on the far side of the ore from the Citadel.
    Vec2 ore = Vec2::zero;
    int64_t n = 0;
    for (size_t i = 0; i < sim.state.patches.size(); i++) {
        if (sim.patchBase[i] == r.site) { ore = ore + sim.state.patches[i].position; n++; }
    }
    ore = ore / static_cast<double>(n);
    const Vec2 at = ore + normalize(ore - r.main.position) * 2.5;
    r.ranger = addUnit(sim.state, Unit::Kind::ranger, sim.nav->nearestFree(at).value_or(at), 1);
    return r;
}

int64_t fled(const Simulation& sim, const std::vector<int64_t>& ids) {
    int64_t k = 0;
    for (int64_t id : ids) {
        const auto u = sim.state.unit(id);
        if (u && u->task == Unit::Task::errand && u->mission && u->mission->is<Mission::FallBack>()) k++;
    }
    return k;
}

} // namespace

/// Under attack they run a short way, not to the other base; once the
/// Ranger is gone they go back to the main's patches.
TEST(workerPull_runShortAndComeBack) {
    Raid r = raid(true);
    Simulation& sim = *r.sim;
    ASSERT_TRUE(r.workers.size() >= 4);
    run(sim, 3);
    int64_t alive = 0;
    for (int64_t id : r.workers) {
        const auto u = sim.state.unit(id);
        if (!u) continue;
        alive++;
        EXPECT_TRUE(distance(u->position, r.main.position) < Commander::fleeDistance + 4);
    }
    EXPECT_TRUE(alive > 0);
    EXPECT_EQ(fled(sim, r.workers), alive);
    // The Ranger leaves; the base is calm `calmTime` later.
    std::erase_if(sim.state.units, [&](const Unit& u) { return u.id == r.ranger; });
    run(sim, Commander::calmTime + 2.5);
    EXPECT_EQ(fled(sim, r.workers), int64_t(0));
    int64_t home = 0, working = 0;
    for (int64_t id : r.workers) {
        const auto u = sim.state.unit(id);
        if (!u) continue;
        if (u->patch && sim.patchBase[size_t(*u->patch)] == r.site) home++;
        if (u->task == Unit::Task::toPatch || u->task == Unit::Task::mining || u->task == Unit::Task::waiting
            || u->task == Unit::Task::toBase || u->task == Unit::Task::depositing) working++;
    }
    EXPECT_EQ(home, alive);
    EXPECT_EQ(working, alive);
}

/// While the Ranger stays, they stay away (and no one is sent to mine at
/// the other base).
TEST(workerPull_stayAwayWhileTheEnemyStays) {
    Raid r = raid(true);
    Simulation& sim = *r.sim;
    run(sim, 8);
    int64_t alive = 0;
    for (int64_t id : r.workers) {
        const auto u = sim.state.unit(id);
        if (!u) continue;
        alive++;
        EXPECT_TRUE(u->task == Unit::Task::errand);
        EXPECT_TRUE(!u->patch || sim.patchBase[size_t(*u->patch)] == r.site);
    }
    EXPECT_TRUE(alive > 0);
}

/// Off (the Swift game's): they are sent to mine at the other base.
TEST(workerPull_offSendsThemToTheOtherBase) {
    Raid r = raid(false);
    Simulation& sim = *r.sim;
    run(sim, 3);
    EXPECT_EQ(fled(sim, r.workers), int64_t(0));
    int64_t away = 0;
    for (int64_t id : r.workers) {
        const auto u = sim.state.unit(id);
        if (u && u->patch && sim.patchBase[size_t(*u->patch)] != r.site) away++;
    }
    EXPECT_TRUE(away > 0);
}
