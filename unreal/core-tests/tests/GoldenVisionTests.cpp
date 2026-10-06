// The fog of war against the Swift game's (golden/vision.json, written by
// Tests/GameCoreTests/GoldenVisionTests.swift): on the bench fixture
// (bench/badlands-large.json, a late window game on Badlands, large), the fog
// grid's levels and towers, each player's sight (cells seen and in reach,
// ids seen, explored bits) from scratch, with every unit seeing 90 round and
// with a watchtower put down by Blue, and each player's Intel after the
// simulation's first look. All exact.
#include "golden.h"
#include "test.h"

#include "NavGrid.h"
#include "Simulation.h"
#include "TerrainField.h"
#include "Vision.h"
#include "WindowMaps.h"

#include <cinttypes>
#include <cstdio>
#include <limits>

using namespace ac;
using golden::json;

namespace {

std::string hex(const std::vector<uint64_t>& words) {
    std::string out;
    char buf[17];
    for (uint64_t w : words) {
        std::snprintf(buf, sizeof buf, "%016" PRIx64, w);
        out += buf;
    }
    return out;
}

json cells(const std::vector<uint8_t>& b) {
    json out = json::array();
    for (size_t i = 0; i < b.size(); ++i) if (b[i]) out.push_back(static_cast<int64_t>(i));
    return out;
}

json ids(const std::set<int64_t>& s) {
    json out = json::array();
    for (int64_t i : s) out.push_back(i);
    return out;
}

json sightJSON(const Sight& s, const std::vector<uint64_t>& explored) {
    return json{{"seen", cells(s.seen)}, {"near", cells(s.near)}, {"ids", ids(s.ids)}, {"explored", hex(explored)}};
}

/// One part of the golden against ours, with where they differ.
void expectSame(const json& want, const json& got, const char* what) {
    std::string where;
    const bool same = golden::same(want, got, where);
    if (!same) std::printf("  %s differs at %s\n", what, where.substr(0, 300).c_str());
    EXPECT_TRUE(same);
}

} // namespace

TEST(goldenVision_badlandsBench) {
    const json want = golden::load(golden::path("vision.json"));
    ASSERT_TRUE(!want.is_discarded());
    std::string error;
    const auto session = SessionStore::load(golden::repo("bench/badlands-large.json"), &error);
    if (!session) std::printf("  the fixture does not load: %s\n", error.c_str());
    ASSERT_TRUE(session.has_value());
    const MapDefinition m = WindowMaps::build(MapChoice{MapStyle::badlands, MapSize::large});
    EXPECT_EQ(session->mapName, m.name);
    const TerrainField field(m);
    const NavGrid nav(m, field);
    const Vision v(m, field, nav);
    const GameState& state = session->state;

    // The grid.
    EXPECT_EQ(v.origin.x, want["origin"][0].get<double>());
    EXPECT_EQ(v.origin.y, want["origin"][1].get<double>());
    EXPECT_EQ(v.width, want["width"].get<int64_t>());
    EXPECT_EQ(v.height, want["height"].get<int64_t>());
    std::string tier;
    for (int8_t t : v.tier) tier.push_back(static_cast<char>(t + 80));
    const std::string wantTier = want["tier"].get<std::string>();
    int64_t tierMismatches = 0;
    for (size_t i = 0; i < std::min(tier.size(), wantTier.size()); ++i) tierMismatches += tier[i] != wantTier[i];
    EXPECT_EQ(tier.size(), wantTier.size());
    EXPECT_EQ(tierMismatches, int64_t(0));
    json towers = json::array();
    for (const auto& t : v.towers) towers.push_back(json{t.at.x, t.at.y, static_cast<double>(t.tier)});
    expectSame(want["towers"], towers, "towers");

    // Each player's sight from scratch.
    json sights = json::array(), farSights = json::array(), towerSights = json::array();
    std::map<int64_t, double> far;
    for (const auto& u : state.units) far[u.id] = 90;
    Vision towered = v;
    const Vec2 tower(want["tower"][0].get<double>(), want["tower"][1].get<double>());
    towered.addTower(tower);
    for (size_t p = 0; p < state.players.size(); ++p) {
        const int64_t pl = static_cast<int64_t>(p);
        std::vector<uint64_t> explored;
        const Sight s = v.sight(pl, state, explored);
        sights.push_back(sightJSON(s, explored));
        std::vector<uint64_t> explored1;
        const Sight f = v.sight(pl, state, explored1, far);
        farSights.push_back(sightJSON(f, explored1));
        std::vector<uint64_t> explored2;
        const Sight t = towered.sight(pl, state, explored2);
        towerSights.push_back(sightJSON(t, explored2));
    }
    expectSame(want["sights"], sights, "sights");
    expectSame(want["farSights"], farSights, "farSights");
    expectSame(want["towerSights"], towerSights, "towerSights");

    // The simulation's first look, on the fixture's saved intel.
    const Simulation sim(state, m);
    json intel = json::array();
    for (size_t p = 0; p < state.players.size(); ++p) {
        const int64_t pl = static_cast<int64_t>(p);
        const auto k = sim.intel(pl);
        ASSERT_TRUE(k.has_value());
        json buildings = json::array(), units = json::array(), sites = json::array();
        for (const auto& b : k->buildings) buildings.push_back(b.id);
        for (const auto& u : k->units)
            units.push_back(json{{"id", u.unit.id}, {"at", u.at}, {"x", u.unit.position.x}, {"y", u.unit.position.y}});
        for (double t : k->sites) sites.push_back(std::isfinite(t) ? t : -1e300);
        const GameState known = sim.known(pl), shown = sim.shown(pl);
        json knownUnits = json::array(), knownStructures = json::array(), shownUnits = json::array(),
             shownStructures = json::array();
        for (const auto& u : known.units) knownUnits.push_back(u.id);
        for (const auto& b : known.structures) knownStructures.push_back(b.id);
        for (const auto& u : shown.units) shownUnits.push_back(u.id);
        for (const auto& b : shown.structures) shownStructures.push_back(b.id);
        const auto sight = sim.sight(pl);
        ASSERT_TRUE(sight.has_value());
        intel.push_back(json{
            {"buildings", buildings}, {"units", units}, {"explored", hex(k->explored)}, {"sites", sites},
            {"known", json{{"units", knownUnits}, {"structures", knownStructures}}},
            {"shown", json{{"units", shownUnits}, {"structures", shownStructures}}},
            {"unseen", ids(sim.unseen(pl))}, {"ids", ids(sight->ids)},
        });
    }
    expectSame(want["intel"], intel, "intel");
}
