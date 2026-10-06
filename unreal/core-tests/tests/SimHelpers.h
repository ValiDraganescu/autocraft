// Helpers for the simulation's own tests (SimulationTests, FightTests,
// AirTests, PlaygroundTests, StallTests): hand-built positions, a few
// seconds of simulation, and counting events. Never a game.
#pragma once

#include "test.h"

#include "Commander.h"
#include "MapLibrary.h"
#include "Projection.h"
#include "ScreenConfig.h"
#include "Simulation.h"
#include "Types.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace simtest {

using namespace ac;

/// The handcrafted map for the machine it was drawn for (`map()` of
/// GameCoreTests.swift), made once.
inline const MapDefinition& homeMap() {
    static const MapDefinition m = [] {
        const ScreenConfig homeScreens({
            {"1552-41055-1", "Built-in", 0, 0, 1728, 1117},
            {"19501-29870-2", "Odyssey", -1652, 1117, 5120, 1440},
        });
        const CanvasProjection proj(homeScreens.canvasWidth, homeScreens.canvasHeight);
        return MapLibrary::map(homeScreens, proj);
    }();
    return m;
}

/// A new game with only blue on the map.
inline GameState blueOnly(GameState s) {
    std::erase_if(s.units, [](const Unit& u) { return u.owner != 0; });
    std::erase_if(s.structures, [](const Structure& b) { return b.owner != 0; });
    return s;
}

/// Swift's `GameState(players:time:patches:structures:units:nextID:winner:endedAt:score:)`
/// with `n` default players.
inline GameState bare(int64_t players, double time, std::vector<Structure> structures, std::vector<Unit> units, int64_t nextID) {
    GameState s;
    s.players.assign(static_cast<size_t>(players), Player{});
    s.time = time;
    s.structures = std::move(structures);
    s.units = std::move(units);
    s.nextID = nextID;
    s.score.assign(static_cast<size_t>(players), 0);
    return s;
}

/// Add a unit; returns its id. A Longbow in tank mode, turrets along the hull.
inline int64_t addUnit(GameState& s, Unit::Kind kind, Vec2 p, int64_t owner, Unit::Task task = Unit::Task::idle,
                       double heading = 0) {
    const int64_t id = s.nextID;
    Unit u(id, kind, owner, p, heading, task);
    if (kind == Unit::Kind::longbow) { u.anchor = 0; u.anchored = false; u.aim = heading; }
    s.units.push_back(u);
    s.nextID += 1;
    return id;
}

/// Add a finished building; returns its id.
inline int64_t addStructure(GameState& s, Structure::Kind kind, Vec2 p, int64_t owner) {
    const int64_t id = s.nextID;
    s.structures.push_back(Structure(id, kind, owner, p));
    s.nextID += 1;
    return id;
}

/// `seconds` of 1/30 s steps; every event.
inline std::vector<GameEvent> run(Simulation& sim, double seconds) {
    std::vector<GameEvent> events;
    const int64_t n = static_cast<int64_t>(seconds * 30);
    for (int64_t k = 0; k < n; k++) {
        auto e = sim.step(1.0 / 30);
        events.insert(events.end(), e.begin(), e.end());
    }
    return events;
}

/// Shots fired by `id`.
inline int64_t shots(const std::vector<GameEvent>& events, int64_t id) {
    int64_t n = 0;
    for (const auto& e : events) if (auto s = e.as<GameEvent::Shot>(); s && s->unit == id) n++;
    return n;
}

/// Hit points of unit `id` (0 when gone).
inline double hp(const Simulation& sim, int64_t id) {
    auto u = sim.state.unit(id);
    return u ? u->hp : 0;
}

/// Swift's `stride(from:through:by:)` count of steps.
inline int64_t strideCount(double from, double through, double by) {
    return static_cast<int64_t>(std::floor((through - from) / by + 1e-9)) + 1;
}

/// A spot on the main's plateau with open low ground `apart` below it,
/// found the way the Swift tests search; `accept(p, q)` adds a condition.
template <class Accept>
inline std::optional<std::pair<Vec2, Vec2>> cliffPair(const MapDefinition& m, const TerrainField& field, const NavGrid& nav,
                                                      double apart, Accept accept) {
    for (int64_t ri = 0; ri < strideCount(4.0, 30, 0.5); ri++) {
        const double r = 4.0 + static_cast<double>(ri) * 0.5;
        for (int64_t k = 0; k < 32; k++) {
            const double a = static_cast<double>(k) / 32 * 2 * pi;
            const Vec2 p = m.bases[0].center + Vec2(std::cos(a), std::sin(a)) * r;
            if (!(field.level(p) == 1 && nav.walkable(p))) continue;
            for (int64_t j = 0; j < 16; j++) {
                const double b = static_cast<double>(j) / 16 * 2 * pi;
                const Vec2 q = p + Vec2(std::cos(b), std::sin(b)) * apart;
                if (field.level(q) == 0 && nav.walkable(q) && accept(p, q)) return std::pair<Vec2, Vec2>{p, q};
            }
        }
    }
    return std::nullopt;
}

} // namespace simtest
