// Port of ProjectionTests, ScreenConfigTests and MapTests in
// Tests/GameCoreTests/GameCoreTests.swift, with the terrain and route tests
// of its SimulationTests that step no simulation (the pick, the ramps, the
// mirror).
#include "test.h"

#include "MapLibrary.h"
#include "Projection.h"
#include "Router.h"
#include "ScreenConfig.h"
#include "TerrainField.h"
#include "Types.h"

#include <optional>
#include <vector>

namespace {

using namespace ac;

/// The arrangement on the machine the handcrafted map was drawn for.
const ScreenConfig& homeScreens() {
    static const ScreenConfig c({
        {"1552-41055-1", "Built-in", 0, 0, 1728, 1117},
        {"19501-29870-2", "Odyssey", -1652, 1117, 5120, 1440},
    });
    return c;
}

/// The handcrafted map for `homeScreens`.
const MapDefinition& homeMap() {
    static const MapDefinition m = [] {
        const CanvasProjection proj(homeScreens().canvasWidth, homeScreens().canvasHeight);
        return MapLibrary::map(homeScreens(), proj);
    }();
    return m;
}

} // namespace

// MARK: - ProjectionTests

TEST(Projection_canvasGroundRoundTrip) {
    const CanvasProjection p(5120, 2557);
    for (Vec2 c : {Vec2(0.0, 0.0), Vec2(5120.0, 2557.0), Vec2(1234.0, 987.0)}) {
        for (double h : {0.0, 2.4}) {
            const Vec2 g = p.ground(c, h);
            const Vec2 back = p.canvas(Vec3(g.x, h, g.y));
            EXPECT_NEAR(back.x, c.x, 1e-6);
            EXPECT_NEAR(back.y, c.y, 1e-6);
        }
    }
}

TEST(Projection_canvasCentreIsWorldOriginAtStatedScale) {
    const CanvasProjection p(5120, 2557, 50);
    const Vec2 c = p.canvas(Vec3::zero);
    EXPECT_NEAR(c.x, 2560, 1e-6);
    EXPECT_NEAR(c.y, 2557.0 / 2, 1e-6);
    // One cell to the right at the focus is pointsPerCell points.
    EXPECT_NEAR(p.canvas(Vec3(1, 0, 0)).x - c.x, 50, 1e-6);
}

// MARK: - ScreenConfigTests

TEST(ScreenConfig_flipsToCanvasAndMatchesHandcraftedShape) {
    EXPECT_EQ(homeScreens().canvasWidth, 5120.0);
    EXPECT_EQ(homeScreens().canvasHeight, 2557.0);
    EXPECT_EQ(homeScreens().shape(), std::string(MapLibrary::handcraftedShape));
}

TEST(ScreenConfig_sessionIDDependsOnArrangementNotOrder) {
    const ScreenConfig reordered({
        {"19501-29870-2", "Odyssey", -1652, 1117, 5120, 1440},
        {"1552-41055-1", "Built-in", 0, 0, 1728, 1117},
    });
    EXPECT_EQ(reordered.sessionID(), homeScreens().sessionID());
    const ScreenConfig laptopOnly({{"1552-41055-1", "Built-in", 0, 0, 1728, 1117}});
    EXPECT_TRUE(laptopOnly.sessionID() != homeScreens().sessionID());
}

// MARK: - MapTests

TEST(Map_handcraftedMapIsDeterministicAndBasesAreFlat) {
    const CanvasProjection proj(homeScreens().canvasWidth, homeScreens().canvasHeight);
    const MapDefinition a = MapLibrary::map(homeScreens(), proj);
    const MapDefinition b = MapLibrary::map(homeScreens(), proj);
    EXPECT_TRUE(a == b);
    EXPECT_EQ(a.name, std::string("Ashfall Reach"));
    const TerrainField field(a);
    for (const BaseSite& base : a.bases) {
        const double h = field.height(base.center);
        EXPECT_NEAR(h, static_cast<double>(base.level) * a.levelHeight, 0.05);
        EXPECT_EQ(field.level(base.center), base.level);
        for (const OreSpec& m : base.ore) EXPECT_EQ(field.level(m.position), base.level);
    }
}

TEST(Map_otherArrangementsGetAGeneratedMap) {
    const ScreenConfig laptopOnly({{"x", "Built-in", 0, 0, 1728, 1117}});
    const CanvasProjection proj(laptopOnly.canvasWidth, laptopOnly.canvasHeight);
    const MapDefinition map = MapLibrary::map(laptopOnly, proj);
    EXPECT_TRUE(map.name.rfind("Generated", 0) == 0);
    EXPECT_EQ(map.bases[static_cast<size_t>(map.starts[0])].ore.size(), size_t(8));
    // The start base is on screen.
    const Vec2 c = proj.canvas(Vec3(map.bases[0].center.x, 2.4, map.bases[0].center.y));
    EXPECT_TRUE(laptopOnly.screens[0].rect.contains(c));
}

// MARK: - From SimulationTests: the terrain and routes

TEST(Map_pickRoundTripsThroughTheTerrain) {
    const MapDefinition& m = homeMap();
    const TerrainField field(m);
    const CanvasProjection proj(homeScreens().canvasWidth, homeScreens().canvasHeight);
    double worst = 0.0;
    for (Vec2 c : {Vec2(980.0, 700), Vec2(2330, 1960), Vec2(4480, 640), Vec2(300, 1200), Vec2(2600, 200)}) {
        const Vec2 g = field.pick(c, proj);
        const Vec2 back = proj.canvas(Vec3(g.x, field.height(g), g.y));
        worst = max(worst, length(back - c));
    }
    // Within a point of where the click was.
    EXPECT_TRUE(worst < 1);
}

TEST(Map_routesBetweenLevelsGoThroughRamps) {
    const MapDefinition& m = homeMap();
    const Router router(m);
    const Vec2 main = m.bases[0].center, natural = m.bases[1].center, third = m.bases[2].center;
    const Vec2 channel = m.bases[4].center;
    EXPECT_EQ(router.route(main, main + Vec2(3, 0)).size(), size_t(1));
    // The natural shares the main's plateau: straight there.
    EXPECT_EQ(router.route(main, natural).size(), size_t(1));
    // Main (high) to the channel (low): down the main ramp.
    const std::vector<Vec2> down = router.route(main, channel);
    EXPECT_EQ(down.size(), size_t(3));
    EXPECT_TRUE(distance(down[0], m.ramps[0].high) < 2);
    // Main to the other main: down one ramp, across, up the other.
    EXPECT_EQ(router.route(main, third).size(), size_t(5));
}

/// The map is fair from either start: every base has a mirror twin,
/// and walking between twins is as far.
TEST(Map_mirroredMapIsTheSameFromEitherStart) {
    const MapDefinition& m = homeMap();
    ASSERT_TRUE(m.mirrorX.has_value());
    const double axis = *m.mirrorX;
    auto twin = [axis](Vec2 p) { return Vec2(2 * axis - p.x, p.y); };
    // Every base has a twin, and walking between twins is as far.
    const Router router(m);
    std::vector<std::optional<size_t>> twinOf;
    for (const BaseSite& b : m.bases) {
        std::optional<size_t> found;
        for (size_t k = 0; k < m.bases.size(); k++) {
            if (distance(m.bases[k].center, twin(b.center)) < 1e-6) { found = k; break; }
        }
        twinOf.push_back(found);
    }
    for (const auto& t : twinOf) ASSERT_TRUE(t.has_value());
    for (size_t i = 0; i < m.bases.size(); i++) {
        for (size_t j = 0; j < m.bases.size(); j++) {
            EXPECT_NEAR(router.distance(m.bases[i].center, m.bases[j].center),
                        router.distance(m.bases[*twinOf[i]].center, m.bases[*twinOf[j]].center), 1e-6);
        }
    }
}
