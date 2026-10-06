// Helpers for the fog of war tests (VisionTests, StallWatchTests): the Swift
// tests' `map()` and hand-built positions.
#pragma once

#include "test.h"

#include "MapLibrary.h"
#include "Projection.h"
#include "ScreenConfig.h"
#include "Simulation.h"
#include "Types.h"

#include <vector>

namespace visiontest {

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

/// Add a unit standing idle; returns its id.
inline int64_t add(GameState& s, Unit::Kind kind, Vec2 p, int64_t owner) {
    const int64_t id = s.nextID;
    s.units.push_back(Unit(id, kind, owner, p, 0, Unit::Task::idle));
    s.nextID += 1;
    return id;
}

/// Add a finished building; returns its id.
inline int64_t add(GameState& s, Structure::Kind kind, Vec2 p, int64_t owner) {
    const int64_t id = s.nextID;
    s.structures.push_back(Structure(id, kind, owner, p));
    s.nextID += 1;
    return id;
}

} // namespace visiontest
