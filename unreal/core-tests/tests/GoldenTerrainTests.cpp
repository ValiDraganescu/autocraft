// The terrain, maps, walking grid, routes and free camera against
// golden/terrain.json and golden/nav.json, written by
// Tests/GameCoreTests/GoldenTerrainTests.swift. Doubles are compared bit for
// bit (the golden holds their bit patterns in hex).
#include "test.h"

#ifndef JSON_NOEXCEPTION
#define JSON_NOEXCEPTION 1
#endif
#include "json.hpp"

#include "FreeView.h"
#include "MapLibrary.h"
#include "NavGrid.h"
#include "Router.h"
#include "ScreenConfig.h"
#include "TerrainField.h"
#include "Types.h"
#include "WindowMaps.h"

#include <bit>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

using json = nlohmann::json;
using namespace ac;

json loadGolden(const std::string& name) {
    std::ifstream in(std::string(AC_REPO_ROOT) + "/unreal/core-tests/golden/" + name);
    std::stringstream text;
    text << in.rdbuf();
    return json::parse(text.str(), nullptr, false);
}

const json& terrainGolden() {
    static const json j = loadGolden("terrain.json");
    return j;
}
const json& navGolden() {
    static const json j = loadGolden("nav.json");
    return j;
}

double hexDouble(const json& s) { return std::bit_cast<double>(std::strtoull(s.get<std::string>().c_str(), nullptr, 16)); }
Vec2 hexVec(const json& a) { return Vec2(hexDouble(a[0]), hexDouble(a[1])); }
bool sameBits(double a, double b) { return std::bit_cast<uint64_t>(a) == std::bit_cast<uint64_t>(b); }
bool sameBits(Vec2 a, Vec2 b) { return sameBits(a.x, b.x) && sameBits(a.y, b.y); }
std::string show(Vec2 v) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "(%.17g, %.17g)", v.x, v.y);
    return buf;
}

/// Mismatches of one kind: counted, the first few printed.
struct Mismatches {
    const char* what;
    int64_t checked = 0, failed = 0;
    void add(bool ok, const std::string& detail) {
        checked += 1;
        if (ok) return;
        failed += 1;
        if (failed <= 6) std::printf("    %s: %s\n", what, detail.c_str());
    }
};

// MARK: - Building the maps as the golden did

MapDefinition buildMap(const json& b) {
    const std::string kind = b["kind"];
    if (kind == "window") {
        return WindowMaps::build(MapChoice{*parse<MapStyle>(b["style"].get<std::string>()), *parse<MapSize>(b["size"].get<std::string>())});
    }
    if (kind == "playground") return WindowMaps::playground();
    std::vector<ScreenConfig::AppKitScreen> screens;
    for (const json& s : b["screens"]) {
        screens.push_back({s[0].get<std::string>(), s[1].get<std::string>(), hexDouble(s[2]), hexDouble(s[3]), hexDouble(s[4]),
                           hexDouble(s[5])});
    }
    std::optional<std::vector<ScreenInsets>> insets;
    if (b.contains("insets")) {
        insets = std::vector<ScreenInsets>{};
        for (const json& i : b["insets"]) {
            insets->push_back(ScreenInsets{i[0].get<double>(), i[1].get<double>(), i[2].get<double>(), i[3].get<double>()});
        }
    }
    const ScreenConfig config(screens, insets);
    const CanvasProjection proj(config.canvasWidth, config.canvasHeight);
    return MapLibrary::map(config, proj);
}

struct Built {
    std::string label;
    MapDefinition map;
    std::unique_ptr<TerrainField> field;
    std::unique_ptr<NavGrid> nav;
};

/// Every golden map, its terrain and its walking grid, built once.
const std::vector<Built>& built() {
    static const std::vector<Built> all = [] {
        std::vector<Built> out;
        for (const json& g : terrainGolden()["maps"]) {
            Built b;
            b.label = g["label"];
            b.map = buildMap(g["build"]);
            b.field = std::make_unique<TerrainField>(b.map);
            b.nav = std::make_unique<NavGrid>(b.map, *b.field);
            out.push_back(std::move(b));
        }
        return out;
    }();
    return all;
}

// MARK: - The map, field by field (Swift's Codable shape)

Vec2 jsonVec(const json& a) { return Vec2(a[0].get<double>(), a[1].get<double>()); }

void compareRect(Mismatches& t, const std::string& at, const CanvasRect& r, const json& j) {
    t.add(r.x == j["x"].get<double>() && r.y == j["y"].get<double>() && r.width == j["width"].get<double>() &&
              r.height == j["height"].get<double>(), at);
}

void compareMap(Mismatches& t, const std::string& label, const MapDefinition& m, const json& j) {
    t.add(m.name == j["name"].get<std::string>(), label + " name " + m.name);
    t.add(m.version == j["version"].get<int64_t>(), label + " version");
    t.add(m.seed == j["seed"].get<uint64_t>(), label + " seed");
    const json& b = j["bounds"];
    t.add(m.bounds.minX == b["minX"].get<double>() && m.bounds.minZ == b["minZ"].get<double>() &&
              m.bounds.maxX == b["maxX"].get<double>() && m.bounds.maxZ == b["maxZ"].get<double>(), label + " bounds");
    t.add(m.levelHeight == j["levelHeight"].get<double>(), label + " levelHeight");
    t.add(m.plateaus.size() == j["plateaus"].size(), label + " plateau count");
    for (size_t i = 0; i < std::min(m.plateaus.size(), j["plateaus"].size()); i++) {
        const json& p = j["plateaus"][i];
        bool same = m.plateaus[i].level == p["level"].get<int64_t>() && m.plateaus[i].polygon.size() == p["polygon"].size();
        for (size_t k = 0; same && k < m.plateaus[i].polygon.size(); k++) same = m.plateaus[i].polygon[k] == jsonVec(p["polygon"][k]);
        t.add(same, label + " plateau " + std::to_string(i));
    }
    t.add(m.ramps.size() == j["ramps"].size(), label + " ramp count");
    for (size_t i = 0; i < std::min(m.ramps.size(), j["ramps"].size()); i++) {
        const json& r = j["ramps"][i];
        const Ramp& c = m.ramps[i];
        t.add(c.low == jsonVec(r["low"]) && c.high == jsonVec(r["high"]) && c.width == r["width"].get<double>() &&
                  c.lowLevel == r["lowLevel"].get<int64_t>() && c.highLevel == r["highLevel"].get<int64_t>(),
              label + " ramp " + std::to_string(i));
    }
    t.add(m.patches.size() == j["patches"].size(), label + " patch count");
    for (size_t i = 0; i < std::min(m.patches.size(), j["patches"].size()); i++) {
        const json& p = j["patches"][i];
        const GroundPatch& c = m.patches[i];
        t.add(rawValue(c.kind) == p["kind"].get<std::string>() && c.center == jsonVec(p["center"]) && c.radius == p["radius"].get<double>(),
              label + " patch " + std::to_string(i));
    }
    t.add(m.bases.size() == j["bases"].size(), label + " base count");
    for (size_t i = 0; i < std::min(m.bases.size(), j["bases"].size()); i++) {
        const json& p = j["bases"][i];
        const BaseSite& c = m.bases[i];
        bool same = c.center == jsonVec(p["center"]) && c.level == p["level"].get<int64_t>() && c.ore.size() == p["ore"].size() &&
                    c.wells.size() == p["wells"].size();
        for (size_t k = 0; same && k < c.ore.size(); k++) {
            const json& o = p["ore"][k];
            same = c.ore[k].position == jsonVec(o["position"]) && c.ore[k].amount == o["amount"].get<int64_t>() &&
                   c.ore[k].angle == o["angle"].get<double>();
        }
        for (size_t k = 0; same && k < c.wells.size(); k++) same = c.wells[k] == jsonVec(p["wells"][k]);
        t.add(same, label + " base " + std::to_string(i) + " at " + show(c.center));
    }
    t.add(m.starts == j["starts"].get<std::vector<int64_t>>(), label + " starts");
    t.add(m.doodads.size() == j["doodads"].size(),
          label + " doodad count " + std::to_string(m.doodads.size()) + " vs " + std::to_string(j["doodads"].size()));
    for (size_t i = 0; i < std::min(m.doodads.size(), j["doodads"].size()); i++) {
        const json& p = j["doodads"][i];
        const Doodad& c = m.doodads[i];
        const std::optional<bool> mirrored = p.contains("mirrored") ? std::optional<bool>(p["mirrored"].get<bool>()) : std::nullopt;
        t.add(rawValue(c.kind) == p["kind"].get<std::string>() && c.position == jsonVec(p["position"]) &&
                  c.rotation == p["rotation"].get<double>() && c.scale == p["scale"].get<double>() &&
                  c.variant == p["variant"].get<uint32_t>() && c.mirrored == mirrored,
              label + " doodad " + std::to_string(i) + " at " + show(c.position));
    }
    const std::optional<double> mirrorX = j.contains("mirrorX") ? std::optional<double>(j["mirrorX"].get<double>()) : std::nullopt;
    t.add(m.mirrorX == mirrorX, label + " mirrorX");
    const std::optional<bool> playground = j.contains("playground") ? std::optional<bool>(j["playground"].get<bool>()) : std::nullopt;
    t.add(m.playground == playground, label + " playground");
    t.add(m.view.has_value() == j.contains("view"), label + " view");
    if (m.view && j.contains("view")) {
        const json& v = j["view"];
        const CanvasProjection& p = m.view->projection;
        const json& jp = v["projection"];
        t.add(p.canvasWidth == jp["canvasWidth"].get<double>() && p.canvasHeight == jp["canvasHeight"].get<double>() &&
                  p.pointsPerCell == jp["pointsPerCell"].get<double>() && p.pitchDegrees == jp["pitchDegrees"].get<double>() &&
                  p.fovDegrees == jp["fovDegrees"].get<double>(), label + " view projection");
        t.add(m.view->screens.size() == v["screens"].size() && m.view->covered.size() == v["covered"].size(), label + " view rects");
        for (size_t i = 0; i < std::min(m.view->screens.size(), v["screens"].size()); i++)
            compareRect(t, label + " view screen " + std::to_string(i), m.view->screens[i], v["screens"][i]);
        for (size_t i = 0; i < std::min(m.view->covered.size(), v["covered"].size()); i++)
            compareRect(t, label + " view covered " + std::to_string(i), m.view->covered[i], v["covered"][i]);
    }
}

// MARK: - Grid layers

std::vector<int64_t> rle(const json& a) {
    std::vector<int64_t> out;
    for (size_t i = 0; i + 1 < a.size(); i += 2) {
        const int64_t v = a[i].get<int64_t>(), n = a[i + 1].get<int64_t>();
        for (int64_t k = 0; k < n; k++) out.push_back(v);
    }
    return out;
}

template <class T> bool sameLayer(const std::vector<T>& mine, const json& golden, std::string& where) {
    const std::vector<int64_t> want = rle(golden);
    if (mine.size() != want.size()) { where = "size " + std::to_string(mine.size()) + " vs " + std::to_string(want.size()); return false; }
    for (size_t i = 0; i < mine.size(); i++) {
        if (static_cast<int64_t>(mine[i]) != want[i]) {
            where = "cell " + std::to_string(i) + ": " + std::to_string(static_cast<int64_t>(mine[i])) + " vs " + std::to_string(want[i]);
            return false;
        }
    }
    return true;
}

void compareLayer(Mismatches& t, const std::string& label, const NavGrid& nav, const json& l) {
    std::string where;
    t.add(sameLayer(nav.regions, l["regions"], where), label + " regions " + where);
    t.add(sameLayer(nav.jumpRegions, l["jumpRegions"], where), label + " jumpRegions " + where);
    t.add(nav.regionCells == l["regionCells"].get<std::vector<int64_t>>(), label + " regionCells");
    std::vector<uint8_t> cliff;
    for (const json& b : l["regionCliff"]) cliff.push_back(b.get<bool>() ? 1 : 0);
    t.add(nav.regionCliff == cliff, label + " regionCliff");
    t.add(nav.mainRegion == l["mainRegion"].get<int32_t>(), label + " mainRegion");
}

// MARK: - Queries

void comparePath(Mismatches& t, const std::string& label, const NavGrid& nav, const json& q) {
    const Vec2 from = hexVec(q["from"]), to = hexVec(q["to"]);
    const double stopAt = hexDouble(q["stopAt"]);
    const bool jumps = q["jumps"];
    // The one deliberate difference (CometEdgeTests): a jumper standing on a
    // cliff face starts its route there; Swift stepped it out to free ground.
    if (jumps && nav.cliff(from) && !nav.walkable(from)) { t.add(true, label + " path from a cliff face (C++ differs on purpose)"); return; }
    const auto mine = nav.path(from, to, stopAt, jumps);
    bool same = mine.has_value() == q.contains("path");
    std::string detail = label + " path " + show(from) + " -> " + show(to) + (jumps ? " (jumps)" : "");
    if (same && mine) {
        const json& want = q["path"];
        same = mine->size() == want.size();
        for (size_t i = 0; same && i < mine->size(); i++) same = sameBits((*mine)[i], hexVec(want[i]));
        detail += ": " + std::to_string(mine->size()) + " vs " + std::to_string(want.size()) + " points";
    } else {
        detail += mine ? ": found, Swift found none" : ": none, Swift found one";
    }
    t.add(same, detail);
}

void compareOptionalVec(Mismatches& t, const std::string& detail, const std::optional<Vec2>& mine, const json& q, const char* key) {
    bool same = mine.has_value() == q.contains(key);
    if (same && mine) same = sameBits(*mine, hexVec(q[key]));
    t.add(same, detail + (mine ? " got " + show(*mine) : " got nil"));
}

void compareRegion(Mismatches& t, const std::string& label, const NavGrid& nav, const json& q) {
    const Vec2 p = hexVec(q["p"]);
    const auto region = nav.region(p);
    const std::optional<int32_t> want = q.contains("region") ? std::optional<int32_t>(q["region"].get<int32_t>()) : std::nullopt;
    t.add(region == want, label + " region at " + show(p));
    t.add(nav.walkable(p) == q["walkable"].get<bool>(), label + " walkable at " + show(p));
    t.add(nav.standable(p) == q["standable"].get<bool>(), label + " standable at " + show(p));
    t.add(nav.inRock(p) == q["inRock"].get<bool>(), label + " inRock at " + show(p));
    t.add(nav.cliff(p) == q["cliff"].get<bool>(), label + " cliff at " + show(p));
    compareOptionalVec(t, label + " freeCentre at " + show(p), nav.freeCentre(p), q, "freeCentre");
    t.add(nav.boxedIn(p, 40) == q["boxedIn"].get<bool>(), label + " boxedIn at " + show(p));
    t.add(nav.boxedIn(p, 40, true) == q["boxedInJumps"].get<bool>(), label + " boxedIn (jumps) at " + show(p));
}

} // namespace

TEST(GoldenTerrain_mapsMatchSwift) {
    ASSERT_TRUE(!terrainGolden().is_discarded());
    Mismatches t{"map"};
    const json& maps = terrainGolden()["maps"];
    ASSERT_TRUE(maps.size() == built().size());
    for (size_t i = 0; i < maps.size(); i++) compareMap(t, built()[i].label, built()[i].map, maps[i]["map"]);
    std::printf("    %lld map fields checked\n", static_cast<long long>(t.checked));
    EXPECT_EQ(t.failed, 0);
}

TEST(GoldenTerrain_heightsAndMaterialsMatchSwift) {
    ASSERT_TRUE(!terrainGolden().is_discarded());
    Mismatches t{"terrain"};
    const json& maps = terrainGolden()["maps"];
    for (size_t i = 0; i < maps.size(); i++) {
        const json& s = maps[i]["samples"];
        const TerrainField& field = *built()[i].field;
        const std::string& label = built()[i].label;
        for (size_t k = 0; k < s["points"].size(); k++) {
            const Vec2 p = hexVec(s["points"][k]);
            t.add(sameBits(field.height(p), hexDouble(s["height"][k])), label + " height at " + show(p));
            t.add(sameBits(field.borderHeight(p), hexDouble(s["border"][k])), label + " borderHeight at " + show(p));
            t.add(sameBits(field.outside(p), hexDouble(s["outside"][k])), label + " outside at " + show(p));
            t.add(field.level(p) == s["level"][k].get<int64_t>(), label + " level at " + show(p));
            const TerrainField::Materials m = field.materials(p);
            const json& w = s["materials"][k];
            t.add(sameBits(m.x, hexDouble(w[0])) && sameBits(m.y, hexDouble(w[1])) && sameBits(m.z, hexDouble(w[2])) &&
                      sameBits(m.w, hexDouble(w[3])), label + " materials at " + show(p));
        }
        if (maps[i].contains("picks")) {
            const ScreenConfig config({{"1552-41055-1", "Built-in", 0, 0, 1728, 1117}, {"19501-29870-2", "Odyssey", -1652, 1117, 5120, 1440}});
            const CanvasProjection proj(config.canvasWidth, config.canvasHeight);
            for (const json& pk : maps[i]["picks"]) {
                const Vec2 c = hexVec(pk[0]);
                t.add(sameBits(field.pick(c, proj), hexVec(pk[1])), label + " pick at " + show(c));
            }
        }
    }
    std::printf("    %lld terrain samples checked\n", static_cast<long long>(t.checked));
    EXPECT_EQ(t.failed, 0);
}

TEST(GoldenTerrain_freeViewMatchesSwift) {
    ASSERT_TRUE(!terrainGolden().is_discarded());
    Mismatches t{"view"};
    const std::vector<GroundRect> bounds = {GroundRect{-100, -60, 100, 60}, GroundRect{-48, -32, 48, 32}, GroundRect{-136, -88, 136, 88}};
    const json& views = terrainGolden()["views"];
    ASSERT_TRUE(views.size() == 6);
    for (size_t n = 0; n < views.size(); n++) {
        const json& g = views[n];
        const Vec2 size = hexVec(g["size"]);
        const double cover = hexDouble(g["cover"]);
        FreeView v(bounds[n / 2], Vec2(3, -2), 40, size);
        std::vector<std::vector<double>> steps;
        auto note = [&] { steps.push_back({v.target.x, v.target.y, v.pointsPerCell}); };
        note();
        v.resize(size, cover); note();
        v.zoom(1.5, Vec2(900, 250)); note();
        v.drag(Vec2(600, 400), Vec2(700, 330)); note();
        v.center(Vec2(500, -500)); note();
        v.center(Vec2(-37, 12)); note();
        v.zoom(1e-6, Vec2(100, 100)); note();
        v.zoom(100, Vec2(640, 400)); note();
        v.fit(); note();
        v.resize(Vec2(size.x * 0.75, size.y * 1.1)); note();
        ASSERT_TRUE(steps.size() == g["steps"].size());
        for (size_t k = 0; k < steps.size(); k++) {
            const json& w = g["steps"][k];
            t.add(sameBits(steps[k][0], hexDouble(w[0])) && sameBits(steps[k][1], hexDouble(w[1])) && sameBits(steps[k][2], hexDouble(w[2])),
                  "view " + std::to_string(n) + " step " + std::to_string(k));
        }
        const std::vector<Vec2> fp = v.footprint();
        for (size_t k = 0; k < 4; k++) t.add(sameBits(fp[k], hexVec(g["footprint"][k])), "view " + std::to_string(n) + " footprint");
        const Vec3 cam = v.cameraPosition();
        t.add(sameBits(cam.x, hexDouble(g["footprint"][4][0])) && sameBits(cam.y, hexDouble(g["footprint"][5][0])) &&
                  sameBits(cam.z, hexDouble(g["footprint"][6][0])), "view " + std::to_string(n) + " camera");
        t.add(sameBits(v.distance(), hexDouble(g["footprint"][7][0])), "view " + std::to_string(n) + " distance");
        t.add(sameBits(v.visibleCells(), hexDouble(g["footprint"][8][0])), "view " + std::to_string(n) + " visibleCells");
        t.add(sameBits(v.minPointsPerCell(), hexDouble(g["minPointsPerCell"])), "view " + std::to_string(n) + " minPointsPerCell");
    }
    EXPECT_EQ(t.failed, 0);
}

TEST(GoldenNav_gridAndRegionsMatchSwift) {
    ASSERT_TRUE(!navGolden().is_discarded());
    Mismatches t{"grid"};
    const json& maps = navGolden()["maps"];
    ASSERT_TRUE(maps.size() == built().size());
    for (size_t i = 0; i < maps.size(); i++) {
        const json& g = maps[i];
        const NavGrid& nav = *built()[i].nav;
        const std::string& label = built()[i].label;
        t.add(sameBits(nav.origin, hexVec(g["origin"])), label + " origin");
        t.add(nav.width == g["width"].get<int64_t>() && nav.height == g["height"].get<int64_t>(), label + " size");
        std::vector<int64_t> cells(nav.ground.size());
        for (size_t k = 0; k < cells.size(); k++) cells[k] = nav.ground[k] ? 1 : nav.cliffs[k] ? 2 : 0;
        std::string where;
        t.add(sameLayer(cells, g["cells"], where), label + " cells " + where);
        t.add(nav.mapRocks == g["mapRocks"].get<int64_t>() && nav.mapWells == g["mapWells"].get<int64_t>(), label + " rocks and wells");
        compareLayer(t, label, nav, g["layer"]);
    }
    EXPECT_EQ(t.failed, 0);
}

TEST(GoldenNav_pathsAndQueriesMatchSwift) {
    ASSERT_TRUE(!navGolden().is_discarded());
    Mismatches paths{"path"}, points{"query"}, routes{"route"};
    const json& maps = navGolden()["maps"];
    ASSERT_TRUE(maps.size() == built().size());
    int64_t none = 0, jumps = 0;
    for (size_t i = 0; i < maps.size(); i++) {
        const json& g = maps[i];
        const NavGrid& nav = *built()[i].nav;
        const std::string& label = built()[i].label;
        for (const json& q : g["paths"]) {
            comparePath(paths, label, nav, q);
            if (!q.contains("path")) none += 1;
            if (q["jumps"].get<bool>()) jumps += 1;
        }
        for (const json& q : g["nearest"]) {
            const Vec2 p = hexVec(q["p"]);
            compareOptionalVec(points, label + " nearestFree " + show(p), nav.nearestFree(p, hexDouble(q["limit"])), q, "result");
        }
        for (const json& q : g["closest"]) {
            const Vec2 target = hexVec(q["target"]), from = hexVec(q["from"]);
            compareOptionalVec(points, label + " closestReachable " + show(target), nav.closestReachable(target, from), q, "result");
        }
        for (const json& q : g["clear"]) {
            const Vec2 a = hexVec(q["a"]), b = hexVec(q["b"]);
            points.add(nav.clear(a, b, q["jumps"].get<bool>()) == q["result"].get<bool>(), label + " clear " + show(a) + " -> " + show(b));
        }
        for (const json& q : g["regions"]) compareRegion(points, label, nav, q);
        const Router router(built()[i].map);
        for (const json& q : g["routes"]) {
            const Vec2 from = hexVec(q["from"]), to = hexVec(q["to"]);
            const std::vector<Vec2> r = router.route(from, to);
            bool same = r.size() == q["route"].size();
            for (size_t k = 0; same && k < r.size(); k++) same = sameBits(r[k], hexVec(q["route"][k]));
            routes.add(same, label + " route " + show(from) + " -> " + show(to));
            routes.add(sameBits(router.distance(from, to), hexDouble(q["distance"])), label + " route distance");
            routes.add(router.region(from) == q["regionFrom"].get<int64_t>() && router.region(to) == q["regionTo"].get<int64_t>(),
                       label + " route regions");
        }
    }
    std::printf("    %lld paths (%lld with no way, %lld as a Comet), %lld queries, %lld route checks\n",
                static_cast<long long>(paths.checked), static_cast<long long>(none), static_cast<long long>(jumps),
                static_cast<long long>(points.checked), static_cast<long long>(routes.checked));
    EXPECT_EQ(paths.failed, 0);
    EXPECT_EQ(points.failed, 0);
    EXPECT_EQ(routes.failed, 0);
}

TEST(GoldenNav_buildingsOreAndPlacedThingsMatchSwift) {
    ASSERT_TRUE(!navGolden().is_discarded());
    Mismatches t{"dynamic"};
    const json& maps = navGolden()["maps"];
    ASSERT_TRUE(maps.size() == built().size());
    int64_t scenarios = 0;
    for (size_t i = 0; i < maps.size(); i++) {
        if (!maps[i].contains("dynamic")) continue;
        scenarios += 1;
        const json& d = maps[i]["dynamic"];
        const std::string& label = built()[i].label;
        GameState s;
        for (const json& st : d["structures"]) {
            s.structures.push_back(Structure(st["id"].get<int64_t>(), *parse<StructureKind>(st["kind"].get<std::string>()), 0,
                                             hexVec(st["position"])));
        }
        for (const json& p : d["patches"]) {
            OreDeposit o;
            o.id = p["id"];
            o.position = hexVec(p["position"]);
            o.angle = hexDouble(p["angle"]);
            o.initial = p["amount"];
            o.remaining = p["remaining"];
            s.patches.push_back(o);
        }
        if (!d["doodads"].empty()) {
            s.doodads = std::vector<Doodad>{};
            for (const json& p : d["doodads"]) {
                Doodad dd;
                dd.kind = *parse<Doodad::Kind>(p["kind"].get<std::string>());
                dd.position = jsonVec(p["position"]);
                dd.rotation = p["rotation"];
                dd.scale = p["scale"];
                dd.variant = p["variant"];
                if (p.contains("mirrored")) dd.mirrored = p["mirrored"].get<bool>();
                s.doodads->push_back(dd);
            }
        }
        s.wells = std::vector<Well>{};
        for (const json& w : d["wells"]) s.wells->push_back(Well{hexVec(w), 100, std::nullopt});
        NavGrid nav = *built()[i].nav;
        t.add(nav.setDynamic(s) == d["changed"].get<bool>(), label + " setDynamic");
        std::string where;
        t.add(sameLayer(nav.dynamic, d["dynamic"], where), label + " dynamic " + where);
        compareLayer(t, label, nav, d["layer"]);
        for (const json& q : d["paths"]) comparePath(t, label, nav, q);
        for (const json& q : d["regions"]) compareRegion(t, label, nav, q);
        for (size_t k = 0; k < d["solid"].size(); k++) {
            const Vec2 p = hexVec(d["solid"][k]);
            t.add(NavGrid::solid(p, s) == d["solidResult"][k].get<bool>(), label + " solid at " + show(p));
        }
        // Setting the same layout again changes nothing.
        t.add(!nav.setDynamic(s), label + " setDynamic again");
    }
    std::printf("    %lld scenarios, %lld checks\n", static_cast<long long>(scenarios), static_cast<long long>(t.checked));
    EXPECT_TRUE(scenarios >= 4);
    EXPECT_EQ(t.failed, 0);
}
