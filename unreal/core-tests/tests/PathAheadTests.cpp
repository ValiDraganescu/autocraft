// C++ only: `PathAhead` (paths worked out ahead on other threads) gives the
// same game as `Simulation::step`, bit for bit. A few seconds of a saved
// game (the bench fixture) with the AIs on, and every soldier of both sides
// sent across the map, so most steps make searches.
#include "SimHelpers.h"

#include "Commander.h"
#include "PathAhead.h"
#include "Session.h"
#include "WindowMaps.h"

#include <cstring>

using namespace ac;

namespace {

std::optional<Simulation> benchGame() {
    const std::string file = std::string(AC_REPO_ROOT) + "/bench/badlands-large.json";
    std::string error;
    const auto session = SessionStore::load(file, &error);
    if (!session) {
        std::printf("  could not load %s: %s\n", file.c_str(), error.c_str());
        return std::nullopt;
    }
    const MapDefinition m = WindowMaps::build(MapChoice{MapStyle::badlands, MapSize::large});
    Simulation sim(session->state, m, Commander::all(m), true);
    // Every soldier to the other side's main.
    for (Unit& u : sim.state.units) {
        if (!u.soldier() || u.stats().air) continue;
        const int64_t foe = u.owner == 0 ? 1 : 0;
        const int64_t base = sim.state.players[size_t(foe)].start.value_or(m.starts[size_t(foe)]);
        u.mission = Mission{Mission::Raid{m.bases[size_t(base)].center}};
    }
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

} // namespace

TEST(pathAheadStepsTheSameGame) {
    std::optional<Simulation> plain = benchGame();
    ASSERT_TRUE(plain.has_value());
    Simulation ahead = *plain;
    PathAhead paths(3);
    int64_t answered = 0;
    bool alike = true;
    for (int k = 0; k < 4 * 60 && alike; k++) {
        const std::vector<GameEvent> e1 = plain->step(1.0 / 60);
        const std::vector<GameEvent> e2 = paths.step(ahead, 1.0 / 60);
        answered += paths.lastStats().ready + paths.lastStats().waited;
        alike = e1.size() == e2.size() && same(*plain, ahead);
    }
    EXPECT_TRUE(alike);
    // The searches did come from the workers.
    EXPECT_TRUE(answered > 20);
}
