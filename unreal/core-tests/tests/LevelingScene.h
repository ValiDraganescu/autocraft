// Short scenes around one driven unit at blue's main, shared by the
// leveling tests (LevelingTests, LevelingKindsTests, TrackingTests): a small
// window-mode map, the record set by hand.
#pragma once

#include "Leveling.h"
#include "WindowMaps.h"
#include "Simulation.h"
#include "test.h"

#include <cmath>
#include <optional>
#include <variant>
#include <vector>

namespace leveling {

using namespace ac;

inline MapDefinition smallMap() { return WindowMaps::build(MapChoice{MapStyle::openField, MapSize::small}); }

inline Simulation newSim() {
    const MapDefinition map = smallMap();
    return Simulation(GameState::new_(map), map);
}

inline int64_t index(const Simulation& sim, int64_t id) {
    for (size_t i = 0; i < sim.state.units.size(); i++)
        if (sim.state.units[i].id == id) return static_cast<int64_t>(i);
    return -1;
}
inline Unit& unitRef(Simulation& sim, int64_t id) { return sim.state.units[static_cast<size_t>(index(sim, id))]; }
inline Unit unit(const Simulation& sim, int64_t id) { return sim.state.units[static_cast<size_t>(index(sim, id))]; }

inline const Structure& firstStructure(const Simulation& sim, int64_t owner, StructureKind kind) {
    for (const auto& s : sim.state.structures)
        if (s.owner == owner && s.kind == kind) return s;
    return sim.state.structures.front();
}

/// Away from the Citadel's ore line.
inline Vec2 openSide(const Simulation& sim, const Structure& citadel) {
    Vec2 sum = Vec2::zero;
    double n = 0;
    for (const auto& p : sim.state.patches) {
        if (distance(p.position, citadel.position) < 12) {
            sum = sum + p.position;
            n += 1;
        }
    }
    return normalize(citadel.position - sum / n);
}

inline std::vector<GameEvent> run(Simulation& sim, double seconds) {
    std::vector<GameEvent> events;
    const int64_t steps = static_cast<int64_t>(rounded(seconds * 30));
    for (int64_t i = 0; i < steps; i++) {
        auto e = sim.step(1.0 / 30);
        events.insert(events.end(), e.begin(), e.end());
    }
    return events;
}

/// A kind at `level` with these picks (the record set by hand).
inline void give(Simulation& sim, UnitKind kind, int64_t level, const std::vector<Perk>& picks) {
    KindRecord k;
    k.xp = Leveling::xp(level);
    k.picks = picks;
    PilotRecord r = sim.state.players[0].pilot ? *sim.state.players[0].pilot : PilotRecord();
    r.kinds[kind] = k;
    sim.state.players[0].pilot = r;
}
inline void give(Simulation& sim, UnitKind kind, const std::vector<Perk>& picks) { give(sim, kind, 10, picks); }

/// A unit at `p` that stays put: it never thinks.
inline int64_t add(Simulation& sim, int64_t id, UnitKind kind, Vec2 p, int64_t owner = 0) {
    Unit u = sim.freshUnit(kind, id, owner, p, 0);
    u.timer = 1000;
    sim.state.units.push_back(u);
    sim.reindex();
    return id;
}

inline void setHeading(Simulation& sim, double h) {
    if (sim.pilot) sim.pilot->heading = h;
}

/// Blue's first Prospector taken over (it stands at its Citadel's edge).
struct Driven {
    Simulation sim;
    int64_t prospector;
    Structure citadel;
};
inline Driven driven() {
    Simulation sim = newSim();
    int64_t prospector = -1;
    for (const auto& u : sim.state.units)
        if (u.owner == 0 && u.kind == UnitKind::prospector) { prospector = u.id; break; }
    const Structure citadel = firstStructure(sim, 0, StructureKind::citadel);
    EXPECT_TRUE(sim.take(prospector));
    return Driven{sim, prospector, citadel};
}

/// A blue `kind` taken over, out on the Citadel's open side, facing out.
struct Scene {
    Simulation sim;
    int64_t id;
    Vec2 at;
    Vec2 out;
    Vec2 side() const { return Vec2(-out.y, out.x); }
};
inline Scene drive(UnitKind kind) {
    Simulation sim = newSim();
    const Structure citadel = firstStructure(sim, 0, StructureKind::citadel);
    const Vec2 out = openSide(sim, citadel);
    const Vec2 at = citadel.position + out * (Rules::radius(StructureKind::citadel) + 5);
    add(sim, 900, kind, at);
    unitRef(sim, 900).heading = std::atan2(out.y, out.x);
    EXPECT_TRUE(sim.take(900));
    setHeading(sim, std::atan2(out.y, out.x));
    return Scene{sim, 900, at, out};
}

inline Tally tallyOf(const Simulation& sim, UnitKind kind) {
    auto it = sim.levels().kinds.find(kind);
    return it != sim.levels().kinds.end() ? it->second.tally : Tally();
}

inline bool isPrice(const Price& p, int64_t ore, int64_t hydrogen) { return p.ore == ore && p.hydrogen == hydrogen; }

} // namespace leveling
