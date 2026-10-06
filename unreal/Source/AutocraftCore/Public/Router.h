// Port of Sources/GameCore/Router.swift.
#pragma once

#include "SimdMath.h"
#include "Types.h"

#include <cstdint>
#include <vector>

namespace ac {

/// Walking routes between the map's regions (the low ground and each
/// plateau): straight within a region, through ramps between them.
struct Router {
    MapDefinition map;
    /// `(a: Int, b: Int, aEnd: Vec2, bEnd: Vec2)`.
    struct Link {
        int64_t a, b;
        Vec2 aEnd, bEnd;
    };
    /// Per ramp: its two regions and a point just past each end.
    std::vector<Link> links;

    explicit Router(const MapDefinition& map_);

    /// -1 for the low ground, else the index of the topmost plateau under `p`.
    static int64_t region(Vec2 p, const MapDefinition& map);

    int64_t region(Vec2 p) const { return region(p, map); }

    /// Waypoints from `from` to `to`, ending at `to`. Direct when both are in
    /// one region or no ramp joins their regions.
    std::vector<Vec2> route(Vec2 from, Vec2 to) const;

    /// Walking distance along `route`.
    double distance(Vec2 from, Vec2 to) const;
};

} // namespace ac
