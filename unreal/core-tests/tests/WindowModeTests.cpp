// Port of Tests/GameCoreTests/WindowModeTests.swift: the maps made on the
// ground and the free camera's maths. Nothing here steps a simulation.
// (`testWindowSessionIsApartFromTheWallpapers` is a SessionStore test.)
#include "test.h"

#include "FreeView.h"
#include "NavGrid.h"
#include "Router.h"
#include "TerrainField.h"
#include "Types.h"
#include "WindowMaps.h"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace ac;

std::vector<MapChoice> allChoices() {
    std::vector<MapChoice> out;
    for (MapStyle style : allCases<MapStyle>()) {
        for (MapSize size : allCases<MapSize>()) out.push_back(MapChoice{style, size});
    }
    return out;
}

FreeView view(Vec2 size = Vec2(1280, 800)) {
    return FreeView(GroundRect{-100, -60, 100, 60}, Vec2(0, 0), 40, size);
}

} // namespace

/// Every map is an exact mirror across x = 0: each base has a twin with
/// its patches mirrored, blue's and red's mains are twins, and the
/// walk between two twins is as long as between their partners.
TEST(WindowMode_everyMapIsAMirrorWithFlatBasesFarApart) {
    for (const MapChoice& choice : allChoices()) {
        const MapDefinition m = WindowMaps::build(choice, false);
        const TerrainField field(m);
        EXPECT_TRUE(m.mirrorX == 0.0);
        EXPECT_EQ(m.starts.size(), size_t(2));
        std::vector<std::optional<size_t>> twin;
        for (const BaseSite& b : m.bases) {
            std::optional<size_t> found;
            for (size_t k = 0; k < m.bases.size(); k++) {
                if (distance(m.bases[k].center, Vec2(-b.center.x, b.center.y)) < 1e-6) { found = k; break; }
            }
            twin.push_back(found);
        }
        bool all = true;
        for (const auto& t : twin) all = all && t.has_value();
        EXPECT_TRUE(all); // every base has a twin
        if (!all) continue;
        EXPECT_TRUE(twin[static_cast<size_t>(m.starts[0])] == static_cast<size_t>(m.starts[1])); // the mains are twins
        auto byXZ = [](Vec2 a, Vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); };
        for (size_t i = 0; i < m.bases.size(); i++) {
            const BaseSite& b = m.bases[i];
            const BaseSite& t = m.bases[*twin[i]];
            std::vector<Vec2> mirrored, theirs;
            for (const OreSpec& o : b.ore) mirrored.push_back(Vec2(-o.position.x, o.position.y));
            for (const OreSpec& o : t.ore) theirs.push_back(o.position);
            std::sort(mirrored.begin(), mirrored.end(), byXZ);
            std::sort(theirs.begin(), theirs.end(), byXZ);
            EXPECT_TRUE(mirrored == theirs); // base i's patches mirror
            // Flat ground of the base's own level under the Citadel and its arc.
            const double h = static_cast<double>(b.level) * m.levelHeight;
            for (int k = 0; k < 16; k++) {
                const double a = static_cast<double>(k) / 16 * 2 * pi;
                for (double r : {0.0, 6.0, 11.0}) {
                    const Vec2 p = b.center + Vec2(std::cos(a), std::sin(a)) * r;
                    EXPECT_NEAR(field.height(p), h, 0.3);
                }
            }
            for (size_t j = i + 1; j < m.bases.size(); j++) {
                EXPECT_TRUE(distance(b.center, m.bases[j].center) >= 20);
            }
            EXPECT_TRUE(std::fabs(b.center.x) < m.bounds.width() / 2 - 10);
            EXPECT_TRUE(std::fabs(b.center.y) < m.bounds.depth() / 2 - 10);
        }
    }
}

/// The bases grow with the size, and every one can be reached on foot
/// from blue's main: the same distance either way round between twins.
/// Slow: it builds and paths all 16 maps, so it runs only with
/// AUTOCRAFT_MAP_TESTS=1 (as `make test-maps` does for Swift).
TEST(WindowMode_basesAreReachableAndCountGrowsWithSize) {
    const char* slow = std::getenv("AUTOCRAFT_MAP_TESTS");
    if (!slow || std::string(slow) != "1") {
        std::printf("    skipped: the slow map test (AUTOCRAFT_MAP_TESTS=1)\n");
        return;
    }
    for (MapStyle style : allCases<MapStyle>()) {
        size_t last = 0;
        for (MapSize size : allCases<MapSize>()) {
            const MapChoice choice{style, size};
            const MapDefinition m = WindowMaps::build(choice);
            EXPECT_TRUE(m.bases.size() >= last); // no fewer bases than the smaller size
            last = m.bases.size();
            const NavGrid nav(m, TerrainField(m));
            const Vec2 home = m.bases[static_cast<size_t>(m.starts[0])].center;
            const auto region = nav.region(home);
            EXPECT_TRUE(region.has_value());
            for (const BaseSite& b : m.bases) {
                EXPECT_TRUE(nav.region(b.center) == region); // on the main's ground
                EXPECT_TRUE(nav.path(home, b.center, 0.5).has_value());
            }
            const Router router(m);
            const Vec2 red = m.bases[static_cast<size_t>(m.starts[1])].center;
            for (const BaseSite& b : m.bases) {
                const Vec2 mirror = Vec2(-b.center.x, b.center.y);
                EXPECT_NEAR(router.distance(home, b.center), router.distance(red, mirror), 1e-6);
            }
        }
    }
}

/// The map's doodads are the west half's, mirrored, and none stand on a base.
TEST(WindowMode_doodadsMirrorAndKeepOffBases) {
    for (const MapChoice& choice : {MapChoice{MapStyle::badlands, MapSize::medium}, MapChoice{MapStyle::openField, MapSize::large}}) {
        const MapDefinition m = WindowMaps::build(choice);
        EXPECT_TRUE(m.doodads.size() > 50);
        size_t east = 0, west = 0;
        for (const Doodad& d : m.doodads) {
            if (d.position.x > 0.01) east += 1;
            if (d.position.x < -0.01) west += 1;
        }
        EXPECT_EQ(east, west);
        for (const Doodad& d : m.doodads) {
            for (const BaseSite& b : m.bases) EXPECT_TRUE(distance(d.position, b.center) > 6);
        }
    }
}

// MARK: - Free camera

/// Zooming holds the ground under the cursor still.
TEST(WindowMode_zoomKeepsTheGroundUnderTheCursor) {
    FreeView v = view();
    const Vec2 cursor = Vec2(900.0, 250.0);
    const Vec2 before = v.ground(cursor);
    v.zoom(1.5, cursor);
    EXPECT_NEAR(v.pointsPerCell, 60, 1e-9);
    EXPECT_TRUE(distance(v.ground(cursor), before) < 1e-6);
    v.zoom(1 / 1.5, cursor);
    EXPECT_TRUE(distance(v.ground(cursor), before) < 1e-6);
}

/// Dragging puts what was under the press under the pointer.
TEST(WindowMode_dragMovesTheGroundWithThePointer) {
    FreeView v = view();
    const Vec2 a = Vec2(600.0, 400.0), b = Vec2(700.0, 330.0);
    const Vec2 grabbed = v.ground(a);
    v.drag(a, b);
    EXPECT_TRUE(distance(v.ground(b), grabbed) < 1e-6);
}

/// The view stops at the map's edges: pushed past a corner,
/// the map's side edge meets the view's side at the middle of the open
/// view, its far edge the view's top, and its near edge the top of the
/// HUD's cover. No ground past the edge shows anywhere else.
TEST(WindowMode_viewStopsAtTheMapEdge) {
    FreeView v = view();
    v.resize(v.size, 150);
    const double open = v.size.y - 150;
    const GroundRect b = v.bounds;
    v.center(Vec2(500, -500));
    EXPECT_NEAR(v.ground(Vec2(v.size.x, open / 2)).x, b.maxX, 1e-6);
    EXPECT_NEAR(v.ground(Vec2(v.size.x / 2, 0)).y, b.minZ, 1e-6);
    v.center(Vec2(-500, 500));
    EXPECT_NEAR(v.ground(Vec2(0, open / 2)).x, b.minX, 1e-6);
    EXPECT_NEAR(v.ground(Vec2(v.size.x / 2, open)).y, b.maxZ, 1e-6);
    // Wherever it is pushed, the open view's sides below mid-height, its
    // bottom edge and its top middle are on the map.
    for (Vec2 push : {Vec2(500, -500), Vec2(-500, 500), Vec2(500, 0), Vec2(0, -500), Vec2(-37, 12)}) {
        v.center(push);
        for (Vec2 c : {Vec2(0, open / 2), Vec2(0, open), Vec2(v.size.x, open), Vec2(v.size.x / 2, 0)}) {
            const Vec2 g = v.ground(c);
            EXPECT_TRUE(g.x >= b.minX - 1e-6);
            EXPECT_TRUE(g.x <= b.maxX + 1e-6);
            EXPECT_TRUE(g.y >= b.minZ - 1e-6);
            EXPECT_TRUE(g.y <= b.maxZ + 1e-6);
        }
    }
    // Inside the limits the camera goes where it is told.
    v.center(Vec2(10, -5));
    EXPECT_TRUE(v.target == Vec2(10, -5));
}

/// The scenery past the edge is the map's own ground on and inside the
/// edge, joins it without a step, and rises into highland further out.
TEST(WindowMode_borderJoinsTheMapAndRises) {
    for (const MapChoice& choice : allChoices()) {
        const MapDefinition m = WindowMaps::build(choice, false);
        const TerrainField field(m);
        const GroundRect b = m.bounds;
        for (int k = 0; k < 40; k++) {
            const double t = static_cast<double>(k) / 40;
            const Vec2 edges[4] = {Vec2(b.minX + t * b.width(), b.minZ), Vec2(b.maxX, b.minZ + t * b.depth()),
                                   Vec2(b.minX + t * b.width(), b.maxZ), Vec2(b.minX, b.minZ + t * b.depth())};
            const Vec2 out[4] = {Vec2(0, -1), Vec2(1, 0), Vec2(0, 1), Vec2(-1, 0)};
            for (int i = 0; i < 4; i++) {
                const Vec2 e = edges[i], o = out[i];
                EXPECT_EQ(field.borderHeight(e), field.height(e));
                EXPECT_EQ(field.borderHeight(e - o * 3), field.height(e - o * 3));
                EXPECT_NEAR(field.borderHeight(e + o * 0.5), field.height(e + o * 0.5), 0.05);
                EXPECT_TRUE(field.borderHeight(e + o * 20) > field.height(e + o * 20) + 2);
            }
        }
    }
}

/// Zoom stays between fit and the maximum.
TEST(WindowMode_zoomIsBounded) {
    FreeView v = view();
    v.zoom(100, Vec2(640, 400));
    EXPECT_EQ(v.pointsPerCell, FreeView::maxPointsPerCell);
    v.zoom(1e-6, Vec2(640, 400));
    EXPECT_NEAR(v.pointsPerCell, v.minPointsPerCell(), 1e-9);
}

/// At full zoom out the whole map is in the open view (above the HUD's
/// cover): every map corner lands inside it.
TEST(WindowMode_fitShowsTheWholeMap) {
    for (Vec2 size : {Vec2(1280.0, 800.0), Vec2(700.0, 900.0), Vec2(2400.0, 700.0)}) {
        for (double cover : {0.0, 170.0}) {
            FreeView v = view(size);
            v.resize(size, cover);
            v.fit();
            const GroundRect b = v.bounds;
            for (Vec2 c : {Vec2(b.minX, b.minZ), Vec2(b.maxX, b.minZ), Vec2(b.minX, b.maxZ), Vec2(b.maxX, b.maxZ)}) {
                const Vec2 p = v.canvas(Vec3(c.x, 0, c.y));
                EXPECT_TRUE(p.x >= -1);
                EXPECT_TRUE(p.x <= size.x + 1);
                EXPECT_TRUE(p.y >= -1);
                EXPECT_TRUE(p.y <= size.y - cover + 1);
            }
        }
    }
}

/// A resized window keeps the scale and re-clamps.
TEST(WindowMode_resizeKeepsTheScale) {
    FreeView v = view();
    v.resize(Vec2(900, 600));
    EXPECT_EQ(v.pointsPerCell, 40.0);
    EXPECT_TRUE(v.size == Vec2(900, 600));
    // A picture through a plain pick agrees with the plane at height 0.
    const Vec2 c = Vec2(450.0, 300.0);
    EXPECT_TRUE(distance(v.ground(c), v.target) < 1e-6);
}
