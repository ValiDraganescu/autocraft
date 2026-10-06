// C++ only (Swift has the same flaw): a Comet that stands on a cliff face
// it is jumping asks for a new way. The route used to start by stepping it
// out to the nearest free ground (the way it had just come), so at an edge
// it jumped up and down in place: out, new route, back in, out again.
// One Comet, one foe on the high ground, a few seconds; never a game.
#include "SimHelpers.h"
#include "WindowMaps.h"

using namespace ac;
using namespace simtest;

namespace {

const MapDefinition& highlands() {
    static const MapDefinition m = WindowMaps::build(MapChoice{MapStyle::highlands, MapSize::medium});
    return m;
}

// A cliff face beside a rock on Highlands (medium), with a cell of free
// ground to the west of it and the plateau's lip to the east.
const Vec2 start(19.43, -22.42), foe(22.70, -26.33), onFace(21.0, -26.25);

/// Take-offs of a Comet sent after an unseen Ranger up the cliff.
int64_t takeoffs(bool fastPaths) {
    Simulation sim(bare(2, 0, {}, {Unit(1, Unit::Kind::comet, 0, start, 0, Unit::Task::idle),
                                    Unit(2, Unit::Kind::ranger, 1, foe, 0, Unit::Task::idle)}, 3),
                   highlands());
    sim.setFastPaths(fastPaths, 32);
    int64_t n = 0;
    bool was = false;
    for (int k = 0; k < 12 * 30; k++) {
        sim.step(1.0 / 30);
        const auto u = sim.state.unit(1);
        if (!u) break;
        const bool now = u->jumpFrom.has_value();
        if (now && !was) n++;
        was = now;
    }
    return n;
}

} // namespace

TEST(cometEdge_pathFromACliffFaceGoesOnNotBack) {
    const NavGrid& nav = *Simulation(bare(1, 0, {}, {}, 1), highlands()).nav;
    ASSERT_TRUE(nav.cliff(onFace));
    ASSERT_TRUE(!nav.walkable(onFace));
    const auto way = nav.path(onFace, foe, 1, true);
    ASSERT_TRUE(way.has_value());
    ASSERT_TRUE(!way->empty());
    // The first waypoint is no farther from the target than the Comet is.
    EXPECT_TRUE(distance(way->front(), foe) <= distance(onFace, foe) + 1e-9);
}

TEST(cometEdge_noHoppingAtTheEdge) {
    for (const bool fast : {false, true}) {
        const int64_t n = takeoffs(fast);
        if (n > 2) std::printf("  fast=%d: %lld take-offs\n", int(fast), static_cast<long long>(n));
        EXPECT_TRUE(n <= 2);
    }
}
