// Helpers for the Commander tests of requests, objectives and intel
// (DirectivesTests, ObjectivesTests, IntelTests): the Swift tests' `map()`,
// `blueOnly` and `CommanderTests`' hand-built positions.
#pragma once

#include "test.h"

#include "Commander.h"
#include "MapLibrary.h"
#include "Projection.h"
#include "ScreenConfig.h"
#include "Simulation.h"
#include "Types.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace squadtest {

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
    s.units.erase(std::remove_if(s.units.begin(), s.units.end(), [](const Unit& u) { return u.owner != 0; }), s.units.end());
    s.structures.erase(std::remove_if(s.structures.begin(), s.structures.end(), [](const Structure& b) { return b.owner != 0; }),
                       s.structures.end());
    return s;
}

/// Add a finished building; returns its id.
inline int64_t add(GameState& s, Structure::Kind kind, Vec2 p, int64_t owner = 0) {
    const int64_t id = s.nextID;
    s.structures.push_back(Structure(id, kind, owner, p));
    s.nextID += 1;
    return id;
}

/// Add a unit standing idle; returns its id.
inline int64_t add(GameState& s, Unit::Kind kind, Vec2 p, int64_t owner = 0) {
    const int64_t id = s.nextID;
    Unit u(id, kind, owner, p, 0, Unit::Task::idle);
    if (kind == Unit::Kind::dropship) { u.energy = 100; u.cargo = std::vector<int64_t>{}; }
    if (kind == Unit::Kind::longbow) { u.anchor = 0; u.anchored = false; }
    s.units.push_back(u);
    s.nextID += 1;
    return id;
}

inline Structure* structureRef(GameState& s, int64_t id) {
    for (auto& b : s.structures) { if (b.id == id) { return &b; } }
    return nullptr;
}

inline Unit* unitRef(GameState& s, int64_t id) {
    for (auto& u : s.units) { if (u.id == id) { return &u; } }
    return nullptr;
}

/// Give a building a finished Lab.
inline void addLab(GameState& s, int64_t id) {
    Structure& host = *structureRef(s, id);
    Structure lab(s.nextID, Structure::Kind::lab, host.owner, host.position + Rules::addonOffset);
    lab.parent = id;
    host.addon = lab.id;
    s.structures.push_back(lab);
    s.nextID += 1;
}

/// Blue alone on the handcrafted map, with a Hab Dome, a finished Garrison,
/// both main Derricks, a Comet out, eight Prospectors and money to spend.
inline std::pair<GameState, int64_t> blueTech(const MapDefinition& m) {
    GameState s = blueOnly(GameState::new_(m));
    s.players[0].style = Player::Style::bio;
    s.players[0].fireflies = 0;
    s.players[0].drops = false;
    s.players[0].comets = 1;
    s.players[0].greed = 0;
    const Commander c(m);
    const Structure citadel = s.structures[0];
    add(s, Structure::Kind::habDome, *c.habDomeSpot(s, citadel));
    const int64_t garrison = add(s, Structure::Kind::garrison, *c.garrisonSpot(s, citadel));
    const std::vector<Well> wells = *s.wells;
    for (const auto& g : wells) {
        if (distance(g.position, citadel.position) < 12) { add(s, Structure::Kind::derrick, g.position); }
    }
    for (int k = 0; k < 7; k++) { add(s, Unit::Kind::prospector, citadel.position + Vec2(static_cast<double>(k) - 3, -3.2)); }
    add(s, Unit::Kind::comet, citadel.position + Vec2(0, 8));
    s.players[0].ore = 1000;
    s.players[0].hydrogen = 500;
    return {s, garrison};
}

/// Red's Citadel with four Prospectors on its ore line.
inline Structure redAtWork(GameState& s) {
    const Structure red = *std::find_if(s.structures.begin(), s.structures.end(), [](const Structure& b) {
        return b.owner == 1 && b.kind == Structure::Kind::citadel;
    });
    std::vector<Vec2> line;
    for (const auto& p : s.patches) {
        if (distance(p.position, red.position) < 10) { line.push_back(p.position); }
    }
    for (size_t i = 0; i < line.size() && i < 4; i++) {
        add(s, Unit::Kind::prospector, red.position + (line[i] - red.position) * 0.8, 1);
    }
    return red;
}

/// The first Citadel of `owner`.
inline Vec2 citadelOf(const GameState& s, int64_t owner) {
    for (const auto& b : s.structures) {
        if (b.owner == owner && b.kind == Structure::Kind::citadel) { return b.position; }
    }
    return Vec2::zero;
}

/// The last mission each unit is given (`missions` of ObjectivesTests).
inline std::map<int64_t, std::optional<Mission>> missions(const std::vector<Command>& orders) {
    std::map<int64_t, std::optional<Mission>> out;
    for (const auto& o : orders) {
        if (const auto* m = o.as<Command::Mission>()) { out[m->unit] = m->mission; }
    }
    return out;
}

/// The orders a request's `.serve` carries.
inline std::vector<Command> served(const std::vector<Command>& orders) {
    std::vector<Command> out;
    for (const auto& o : orders) {
        if (const auto* sv = o.as<Command::Serve>()) { out.push_back(*sv->command); }
    }
    return out;
}

inline bool contains(const std::vector<Command>& orders, const Command& c) {
    return std::find(orders.begin(), orders.end(), c) != orders.end();
}

} // namespace squadtest
