// A Prospector never stands idle while there is ore to mine, even with no
// money, a hurt Citadel and its own base's ore gone (the user's report:
// "prospectors stuck, plenty of work, nothing happens"). Isolated
// scenarios of a few Prospectors, never a game.
#include "SimHelpers.h"

using namespace ac;
using namespace simtest;

namespace {

struct Scene {
    GameState state;
    Structure citadel;
    std::vector<int64_t> workers;
};

/// Blue alone with 0 ore, a Citadel at 80% and four Prospectors by it.
Scene scene() {
    Scene sc{blueOnly(GameState::new_(homeMap())), Structure(), {}};
    sc.state.units.clear();
    sc.state.players[0].ore = 0;
    sc.citadel = sc.state.structures[0];
    for (auto& b : sc.state.structures) if (b.id == sc.citadel.id) b.hp = Rules::hp(b.kind) * 0.8;
    for (int k = 0; k < 4; k++) {
        sc.workers.push_back(addUnit(sc.state, Unit::Kind::prospector, sc.citadel.position + Vec2(3 + k * 0.5, 3), 0));
    }
    return sc;
}

int64_t idleCount(const Simulation& sim, const std::vector<int64_t>& ids) {
    int64_t n = 0;
    for (int64_t id : ids) if (auto u = sim.state.unit(id); u && u->task == Unit::Task::idle && u->carrying == 0) n++;
    return n;
}

} // namespace

/// A repair costs nothing and ends by itself; the repairers go back to
/// the ore with no money banked.
TEST(prospectorIdle_repairWithoutOreEndsInMining) {
    Scene sc = scene();
    Simulation sim(sc.state, homeMap());
    for (int64_t id : sc.workers) EXPECT_TRUE(sim.issue(Command::Repair{id, sc.citadel.id}));
    run(sim, 20);
    EXPECT_EQ(sim.state.structure(sc.citadel.id)->hp, Rules::hp(sc.citadel.kind));
    EXPECT_EQ(idleCount(sim, sc.workers), 0);
    EXPECT_TRUE(sim.state.totalMined() > 0);
}

/// Every patch of the Citadel's base mined out and no ore for a new
/// Citadel: the workers mine the nearest field elsewhere, and keep at it.
TEST(prospectorIdle_minedOutBaseSendsThemToTheNearestField) {
    Scene sc = scene();
    Simulation sim(sc.state, homeMap());
    for (size_t i = 0; i < sim.state.patches.size(); i++) {
        if (sim.patchBase[i] == sim.site(sc.citadel)) sim.state.patches[i].remaining = 0;
    }
    int64_t live = 0;
    for (const auto& p : sim.state.patches) if (p.remaining > 0) live++;
    ASSERT_TRUE(live > 0);
    for (int step = 0; step < 12; step++) {
        run(sim, 5);
        if (step >= 1) EXPECT_EQ(idleCount(sim, sc.workers), 0);
    }
    EXPECT_TRUE(sim.state.totalMined() > 0);
}

/// The same with the Commander on (the human's economy runs through it).
TEST(prospectorIdle_minedOutBaseWithTheCommanderOn) {
    Scene sc = scene();
    Simulation sim(sc.state, homeMap(), Commander::all(homeMap()));
    for (size_t i = 0; i < sim.state.patches.size(); i++) {
        if (sim.patchBase[i] == sim.site(sc.citadel)) sim.state.patches[i].remaining = 0;
    }
    run(sim, 10);
    EXPECT_EQ(idleCount(sim, sc.workers), 0);
    EXPECT_TRUE(sim.state.totalMined() > 0);
}
