// Units stuck in a base (the user's report, 2026-10-06: "prospectors mining
// in the base and really doing nothing, stuck scorpions, prospectors run to
// other bases instead of coming back"). On Badlands (large), Blue's main,
// as in the user's saved game: Habitat Domes set corner to corner with the
// Citadel and a Garrison walled the ore line in; Prospectors "mined" patches
// of other bases from where they stood; a buried Scorpion in the lane between
// the ore and the Citadel held another one behind it. Isolated scenarios of a
// few seconds, never a game.
#include "SimHelpers.h"

#include "WindowMaps.h"

using namespace ac;
using namespace simtest;

namespace {

const MapDefinition& badlands() {
    static const MapDefinition m = [] {
        MapChoice c;
        c.style = MapStyle::badlands;
        c.size = MapSize::large;
        return WindowMaps::build(c);
    }();
    return m;
}

/// Blue alone on Badlands, its start Citadel and Prospectors only.
GameState blueMain() { return blueOnly(GameState::new_(badlands())); }

/// The buildings that walled the user's ore line in (ids 113, 135, 153,
/// 121, 315, 338, 210, 261 of the save).
void addWall(GameState& s) {
    for (Vec2 p : {Vec2(-66, -10), Vec2(-64, -8), Vec2(-66, -6), Vec2(-66, -24), Vec2(-68, -26)}) {
        addStructure(s, Structure::Kind::habDome, p, 0);
    }
    addStructure(s, Structure::Kind::garrison, Vec2(-65, -19), 0);
    addStructure(s, Structure::Kind::foundry, Vec2(-63, -4), 0);
    addStructure(s, Structure::Kind::lab, Vec2(-60.5, -3.5), 0);
}

/// A spot in the lane between the main's ore and its Citadel.
const Vec2 oreLine(-73.4, -14.8);
/// Open ground outside the main.
const Vec2 outside(-24, 0);

bool joined(const Simulation& sim) { return sim.nav->path(oreLine, outside, 1).has_value(); }

} // namespace

/// The scene is the user's: Blue's start Citadel stands where theirs did, and
/// their buildings cut the ore line off from the rest of the map.
TEST(stuck_theUsersBuildingsWallTheOreLineIn) {
    GameState s = blueMain();
    ASSERT_TRUE(!s.structures.empty());
    EXPECT_TRUE(distance(s.structures[0].position, Vec2(-70, -14.08)) < 0.5);
    {
        Simulation open(s, badlands());
        EXPECT_TRUE(joined(open));
    }
    addWall(s);
    Simulation walled(s, badlands());
    EXPECT_TRUE(!(joined(walled)));
}

/// `NavGrid::cuts` sees the Dome that closes the wall (and not one that
/// leaves a way round).
TEST(stuck_navGridSeesTheDomeThatClosesTheWall) {
    GameState s = blueMain();
    addWall(s);
    std::erase_if(s.structures, [](const Structure& b) { return b.position == Vec2(-66, -10); });
    Simulation sim(s, badlands());
    ASSERT_TRUE(joined(sim));
    EXPECT_TRUE(sim.nav->cuts({{Vec2(-66, -10), Rules::radius(Structure::Kind::habDome)}}));
    // Out on the open plateau east of the base: nothing to cut.
    EXPECT_TRUE(!(sim.nav->cuts({{Vec2(-56, -12), Rules::radius(Structure::Kind::habDome)}})));
}

/// The Commander's Habitat Domes and Garrisons never wall the ore line in:
/// ten more Domes and three Garrisons, each where the Commander puts it,
/// with the user's buildings already standing.
TEST(stuck_commanderBuildingsKeepTheOreLineOpen) {
    GameState s = blueMain();
    addWall(s);
    std::erase_if(s.structures, [](const Structure& b) { return b.position == Vec2(-66, -10); });
    const Structure citadel = s.structures[0];
    Commander c(badlands());
    for (int k = 0; k < 13; k++) {
        Simulation sim(s, badlands());
        ASSERT_TRUE(joined(sim));
        const bool garrison = k % 4 == 3;
        const auto spot = garrison ? c.garrisonSpot(sim.state, citadel, sim.navGrid())
                                   : c.habDomeSpot(sim.state, citadel, sim.navGrid());
        if (!spot) break;
        addStructure(s, garrison ? Structure::Kind::garrison : Structure::Kind::habDome, *spot, 0);
    }
    Simulation sim(s, badlands());
    EXPECT_TRUE(joined(sim));
}

/// In a walled-in ore line, a Prospector sent to a patch of another base
/// does not mine it from where it stands: it finds a patch it can reach
/// and mines that.
TEST(stuck_prospectorNeverMinesAPatchItCannotReach) {
    GameState s = blueMain();
    s.units.clear();
    addWall(s);
    const int64_t worker = addUnit(s, Unit::Kind::prospector, oreLine, 0);
    Simulation sim(s, badlands());
    ASSERT_TRUE(!(joined(sim)));
    const int64_t main = sim.site(sim.state.structures[0]);
    std::optional<int64_t> far;
    for (size_t i = 0; i < sim.state.patches.size(); i++) {
        if (sim.patchBase[i] != main && (!far || distance(sim.state.patches[i].position, oreLine)
                                                     < distance(sim.state.patches[size_t(*far)].position, oreLine))) {
            far = static_cast<int64_t>(i);
        }
    }
    ASSERT_TRUE(far.has_value());
    const int64_t before = sim.state.patches[size_t(*far)].remaining;
    EXPECT_TRUE(sim.issue(Command::Gather{worker, *far}));
    run(sim, 12);
    EXPECT_EQ(sim.state.patches[size_t(*far)].remaining, before);
    const auto u = sim.state.unit(worker);
    ASSERT_TRUE(u.has_value());
    ASSERT_TRUE(u->patch.has_value());
    EXPECT_EQ(sim.patchBase[size_t(*u->patch)], main);
    EXPECT_TRUE(sim.state.players[0].totalMined + u->carrying > 0);
}

/// A Scorpion walks over one buried in the lane between the ore and the
/// Citadel (the user's Scorpions 516 and 538: the one behind stood there for
/// good). Every other patch of the line mined out, as in the save.
TEST(stuck_scorpionWalksOverABuriedOne) {
    auto scene = [](bool passable) {
        GameState s = blueMain();
        s.units.clear();
        for (auto& p : s.patches) {
            if (p.initial < 1000 && distance(p.position, Vec2(-70, -14.08)) < 10) p.remaining = 0;
        }
        const int64_t buried = addUnit(s, Unit::Kind::scorpion, Vec2(-73.70, -10.89), 0);
        for (auto& u : s.units) {
            if (u.id != buried) continue;
            u.anchor = 1; u.anchored = true;
            u.mission = Mission::Raid{u.position};
        }
        const int64_t mover = addUnit(s, Unit::Kind::scorpion, Vec2(-72.95, -10.66), 0);
        Simulation sim(s, badlands());
        sim.buriedPassable = passable;
        EXPECT_TRUE(sim.issue(Command::Mission{mover, Mission::Raid{Vec2(-71.75, -27.75)}}));
        run(sim, 10);
        return sim.state.unit(mover)->position;
    };
    const Vec2 now = scene(true);
    EXPECT_TRUE(now.y < -20);
    const Vec2 old = scene(false);
    EXPECT_TRUE(old.y > -12);
}
