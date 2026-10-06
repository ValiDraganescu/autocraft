// Port of Tests/GameCoreTests/PlaygroundTests.swift: the playground
// (`Simulation+Playground`), flat, empty ground with two sides, and what
// is put down by hand joining the game as the map's own.
#include "SimHelpers.h"

#include "WindowMaps.h"

using namespace ac;
using namespace simtest;

namespace {

Simulation fresh() {
    const MapDefinition m = WindowMaps::playground();
    return Simulation(GameState::new_(m), m, false);
}

} // namespace

/// It starts with two sides and nothing on the ground, level everywhere;
/// one side alone with buildings is no win.
TEST(playground_startsEmptyWithTwoSidesAndNoOneWins) {
    Simulation sim = fresh();
    EXPECT_EQ(sim.state.players.size(), size_t(2));
    EXPECT_TRUE(sim.state.score == (std::vector<int64_t>{0, 0}));
    EXPECT_TRUE(sim.state.structures.empty() && sim.state.units.empty() && sim.state.patches.empty());
    const TerrainField field(WindowMaps::playground());
    EXPECT_EQ(field.height(Vec2(-20, 11)), 0.0);
    EXPECT_EQ(field.height(Vec2(30, -17)), 0.0);
    const int64_t citadel = sim.placeStructure(Structure::Kind::citadel, 0, Vec2(-10, 0));
    const int64_t garrison = sim.placeStructure(Structure::Kind::garrison, 1, Vec2(10, 0));
    EXPECT_TRUE(!sim.remove(424242).has_value());
    EXPECT_TRUE(sim.remove(garrison) == std::optional<std::string>("Garrison"));
    for (int k = 0; k < 30; k++) sim.step(1.0 / 30);
    EXPECT_TRUE(!sim.state.structure(garrison).has_value());
    EXPECT_TRUE(sim.state.structure(citadel).has_value());
    EXPECT_TRUE(!sim.state.endedAt.has_value());
    EXPECT_TRUE(!sim.state.winner.has_value());
}

/// A Prospector put down by a Citadel mines ore put down after the
/// game began.
TEST(playground_placedOreIsMinedIntoAPlacedCitadel) {
    Simulation sim = fresh();
    sim.placeStructure(Structure::Kind::citadel, 0, Vec2(-10, 0));
    EXPECT_TRUE(sim.propRefusal(Vec2(-10, 0), 1.1) == std::optional<std::string>("Something is in the way"));
    EXPECT_TRUE(!sim.propRefusal(Vec2(-17, 0), 1.1).has_value());
    sim.placePatch(Vec2(-17, 0), pi / 2);
    EXPECT_TRUE(!sim.unitRefusal(Unit::Kind::prospector, Vec2(-13.5, 0)).has_value());
    const int64_t worker = sim.placeUnit(Unit::Kind::prospector, 0, Vec2(-13.5, 0), pi);
    const int64_t start = sim.state.players[0].ore;
    for (int k = 0; k < 30 * 30; k++) sim.step(1.0 / 30);
    EXPECT_TRUE(sim.state.players[0].ore > start); // the Prospector brought ore home
    EXPECT_TRUE(sim.state.patches[0].remaining < Simulation::placedOre);
    EXPECT_TRUE(sim.state.unit(worker).has_value());
}

/// Doodads and wells put down block the way like the map's, and go
/// again when taken away; a Derrick goes on a placed well.
TEST(playground_placedDoodadsAndWellsBlockTheWay) {
    Simulation sim = fresh();
    const Vec2 rock(5, 5), well(-5, -5);
    EXPECT_TRUE(sim.nav->walkable(rock));
    Doodad d;
    d.kind = Doodad::Kind::boulder;
    d.position = rock;
    d.rotation = 0;
    d.scale = 1;
    d.variant = 3;
    sim.placeDoodad(d);
    EXPECT_TRUE(!sim.nav->walkable(rock));
    EXPECT_TRUE(sim.nav->inRock(rock));
    EXPECT_TRUE(sim.unitRefusal(Unit::Kind::ranger, rock) == std::optional<std::string>("Something is in the way"));
    EXPECT_TRUE(!sim.unitRefusal(Unit::Kind::dropship, rock).has_value()); // a flyer goes over it
    sim.placeWell(well);
    EXPECT_TRUE(!sim.nav->walkable(well));
    EXPECT_TRUE(sim.snap(Structure::Kind::derrick, well + Vec2(1, 0)) == well);
    EXPECT_TRUE(!sim.placementRefusal(Structure::Kind::derrick, well).has_value());
    EXPECT_TRUE(sim.placementRefusal(Structure::Kind::citadel, well + Vec2(5, 0)) == std::optional<std::string>("Too close to resources"));
    EXPECT_TRUE(sim.removeProp(rock) == std::optional<std::string>("boulder"));
    EXPECT_TRUE(sim.nav->walkable(rock));
    EXPECT_TRUE(!sim.nav->inRock(rock));
    sim.placeStructure(Structure::Kind::derrick, 0, well);
    EXPECT_TRUE(sim.removeProp(well + Vec2(1.2, 0)) == std::optional<std::string>("Derrick")); // the Derrick goes first
    EXPECT_TRUE(!sim.removeProp(well + Vec2(1.2, 0)).has_value()); // a well under a Derrick stays
    for (int k = 0; k < 3; k++) sim.step(1.0 / 30);
    EXPECT_TRUE(sim.removeProp(well + Vec2(1.2, 0)) == std::optional<std::string>("MH well")); // then it goes
    EXPECT_TRUE(sim.nav->walkable(well));
}

/// A Lab goes on its building's add-on spot, once.
TEST(playground_labGoesOnItsHost) {
    Simulation sim = fresh();
    const int64_t g = sim.placeStructure(Structure::Kind::garrison, 0, Vec2(0, 0));
    const Vec2 spot = Vec2(0, 0) + Rules::addonOffset;
    const auto host = sim.labHost(spot, 0);
    EXPECT_TRUE(host && host->id == g);
    EXPECT_TRUE(!sim.labHost(spot, 1).has_value()); // not the other side's
    const auto lab = sim.placeLab(g);
    ASSERT_TRUE(lab.has_value());
    EXPECT_TRUE(sim.state.structure(g)->addon == lab);
    EXPECT_TRUE(sim.state.structure(*lab)->parent == g);
    EXPECT_TRUE(sim.labRefusal(*sim.state.structure(g)) == std::optional<std::string>("It has a Lab"));
    EXPECT_TRUE(!sim.placeLab(g).has_value());
}
