// The simulation against Swift, bit for bit: the scenarios of
// Tests/GameCoreTests/GoldenSimulationTests.swift, stepped the same way, a
// frame every second compared with golden/simulation.json (units' places,
// headings, hp and tasks, buildings' hp, the banks, the events by case).
#include "SimHelpers.h"
#include "golden.h"

#include "WindowMaps.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <map>

using namespace ac;
using namespace simtest;
using golden::json;

namespace {

std::string bits(double d) {
    uint64_t u;
    std::memcpy(&u, &d, sizeof u);
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016" PRIx64, u);
    return buf;
}

/// Swift's `String(describing:)` of the case, up to its payload.
const char* caseName(const GameEvent& e) {
    static const char* names[] = {"deposited", "patchDepleted", "patchRegrown", "trained", "constructionStarted", "constructed",
                                  "shot", "missed", "landed", "died", "destroyed", "victory", "researched", "stuck", "trapped",
                                  "leveledUp", "blast"};
    return names[e.index()];
}

json frame(const Simulation& sim, const std::vector<GameEvent>& events) {
    std::map<std::string, int64_t> counts;
    for (const auto& e : events) counts[caseName(e)] += 1;
    json units = json::array(), structures = json::array(), players = json::array(), ev = json::object();
    for (const auto& u : sim.state.units) {
        units.push_back(json::array({u.id, std::string(rawValue(u.kind)), std::string(rawValue(u.task)), bits(u.position.x),
                                     bits(u.position.y), bits(u.heading), bits(u.hp)}));
    }
    for (const auto& s : sim.state.structures) structures.push_back(json::array({s.id, std::string(rawValue(s.kind)), bits(s.hp)}));
    for (const auto& p : sim.state.players) players.push_back(json::array({p.ore, p.hydrogen, p.supplyUsed, p.supplyCap, p.totalMined}));
    for (const auto& [k, n] : counts) ev[k] = n;
    return json{{"time", bits(sim.state.time)}, {"units", units}, {"structures", structures}, {"players", players}, {"events", ev}};
}

std::vector<json> record(Simulation& sim, int seconds) {
    std::vector<json> frames{frame(sim, {})};
    for (int s = 0; s < seconds; s++) {
        std::vector<GameEvent> events;
        for (int k = 0; k < 30; k++) {
            auto e = sim.step(1.0 / 30);
            events.insert(events.end(), e.begin(), e.end());
        }
        frames.push_back(frame(sim, events));
    }
    return frames;
}

int64_t add(GameState& s, Unit::Kind kind, Vec2 p, int64_t owner, Unit::Task task = Unit::Task::idle) {
    const int64_t id = s.nextID;
    Unit u(id, kind, owner, p, 0, task);
    if (kind == Unit::Kind::longbow) { u.anchor = 0; u.anchored = false; u.aim = 0; }
    if (kind == Unit::Kind::hailstorm || kind == Unit::Kind::firefly) u.aim = 0;
    if (kind == Unit::Kind::dropship) { u.energy = 50; u.cargo = std::vector<int64_t>{}; }
    s.units.push_back(u);
    s.nextID += 1;
    return id;
}

std::vector<json> duel() {
    std::vector<Unit> units;
    int64_t id = 10;
    for (const double side : {-1.0, 1.0}) {
        const int64_t p = side < 0 ? 0 : 1;
        for (int64_t k = 0; k < 20; k++) {
            const double a = static_cast<double>(k) * 2.39996, r = 0.62 * std::sqrt(static_cast<double>(k));
            units.push_back(Unit(id, Unit::Kind::ranger, p, Vec2(7 * side, 0) + Vec2(-side * std::cos(a), std::sin(a)) * r, 0,
                                 Unit::Task::idle));
            id += 1;
        }
    }
    std::vector<Structure> structures;
    for (int64_t p = 0; p < 2; p++) {
        structures.push_back(Structure(id, Structure::Kind::habDome, p, Vec2(0, p == 0 ? -40 : 40)));
        id += 1;
    }
    Simulation sim(bare(2, 0, structures, units, id));
    sim.issue(Command::Attack{0, Vec2(7, 0)});
    sim.issue(Command::Attack{1, Vec2(-7, 0)});
    return record(sim, 30);
}

std::vector<json> mining() {
    const MapDefinition& m = homeMap();
    GameState state = blueOnly(GameState::new_(m));
    state.players[0].ore = 1000;
    Simulation sim(state, m);
    const Structure citadel = sim.state.structures[0];
    for (int k = 0; k < 3; k++) sim.issue(Command::Train{citadel.id, std::nullopt});
    const Vec2 spot = *Commander(m).habDomeSpot(sim.state, citadel);
    sim.issue(Command::Build{sim.state.units[0].id, Structure::Kind::habDome, spot});
    return record(sim, 60);
}

std::vector<json> skirmish() {
    const MapDefinition& m = homeMap();
    GameState s = GameState::new_(m);
    s.units.clear();
    s.time = 600;
    Vec2 blue, red;
    for (const auto& b : s.structures) if (b.owner == 0) { blue = b.position; break; }
    for (const auto& b : s.structures) if (b.owner == 1) { red = b.position; break; }
    const Vec2 dir = normalize(red - blue), side(-dir.y, dir.x);
    const Vec2 mid = blue + (red - blue) * 0.4;
    using K = Unit::Kind;
    const std::vector<K> ours{K::ranger, K::ranger, K::ranger, K::ranger, K::ranger, K::ranger, K::longbow, K::longbow,
                              K::juggernaut, K::juggernaut, K::dropship, K::comet, K::comet, K::firefly, K::hailstorm};
    for (size_t k = 0; k < ours.size(); k++) {
        const double kd = static_cast<double>(static_cast<int64_t>(k) / 5), km = static_cast<double>(static_cast<int64_t>(k) % 5);
        add(s, ours[k], mid - dir * (2 + kd) + side * (km - 2) * 1.1, 0);
    }
    const std::vector<K> theirs{K::ranger, K::ranger, K::ranger, K::ranger, K::ranger, K::ranger, K::kestrel,
                                K::firefly, K::firefly, K::juggernaut, K::dropship, K::longbow};
    for (size_t k = 0; k < theirs.size(); k++) {
        const double kd = static_cast<double>(static_cast<int64_t>(k) / 4), km = static_cast<double>(static_cast<int64_t>(k) % 4);
        add(s, theirs[k], mid + dir * (9 + kd) + side * (km - 1.5) * 1.2, 1, Unit::Task::attackMove);
    }
    s.players[0].upgrades = std::set<Upgrade>{Upgrade::minigun};
    s.players[1].attack = mid;
    Simulation sim(s, m);
    sim.flierKeepsGoal = false; // the golden's red Kestrel moves like Swift's (one step in eight)
    return record(sim, 30);
}

std::vector<json> playground() {
    const MapDefinition m = WindowMaps::playground();
    Simulation sim(GameState::new_(m), m, false);
    sim.placeStructure(Structure::Kind::citadel, 0, Vec2(-10, 0));
    const int64_t g = sim.placeStructure(Structure::Kind::garrison, 0, Vec2(-10, 8));
    sim.placeLab(g);
    sim.placePatch(Vec2(-17, 0), pi / 2);
    sim.placePatch(Vec2(-17, 3), 0);
    sim.placeWell(Vec2(-5, -6));
    Doodad d;
    d.kind = Doodad::Kind::boulder;
    d.position = Vec2(0, 2);
    d.rotation = 0;
    d.scale = 1;
    d.variant = 3;
    sim.placeDoodad(d);
    for (int k = 0; k < 3; k++) sim.placeUnit(Unit::Kind::prospector, 0, Vec2(-13.5, static_cast<double>(k) - 1), pi);
    for (int k = 0; k < 4; k++) sim.placeUnit(Unit::Kind::ranger, 0, Vec2(-4, static_cast<double>(k)), 0);
    sim.placeUnit(Unit::Kind::longbow, 0, Vec2(-6, -2), 0);
    for (int k = 0; k < 5; k++) sim.placeUnit(Unit::Kind::ranger, 1, Vec2(6, static_cast<double>(k) - 1), pi);
    sim.placeUnit(Unit::Kind::firefly, 1, Vec2(8, 0), pi);
    sim.issue(Command::Attack{1, Vec2(-4, 1)});
    return record(sim, 25);
}

void compare(const char* name, const std::vector<json>& frames) {
    static const json g = golden::load(golden::path("simulation.json"));
    ASSERT_TRUE(!g.is_discarded() && g.contains(name));
    const json& want = g[name];
    EXPECT_EQ(frames.size(), want.size());
    for (size_t i = 0; i < frames.size() && i < want.size(); i++) {
        std::string where;
        const bool ok = golden::same(frames[i], want[i], where, std::string(name) + "/" + std::to_string(i));
        EXPECT_TRUE(ok);
        if (!ok) {
            std::printf("  first difference at second %zu: %s\n", i, where.c_str());
            return;
        }
    }
}

} // namespace

TEST(simulationGolden_duel) { compare("duel", duel()); }
TEST(simulationGolden_mining) { compare("mining", mining()); }
TEST(simulationGolden_skirmish) { compare("skirmish", skirmish()); }
TEST(simulationGolden_playground) { compare("playground", playground()); }
