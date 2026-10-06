// Port of Sources/GameCore/TerrainField.swift.
#pragma once

#include "SimdMath.h"
#include "Noise.h"
#include "Projection.h"
#include "Types.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace ac {

/// Continuous terrain derived from a map: height and ground-material weights
/// at any ground point. The renderer samples it into a mesh and a splat map;
/// the simulation samples it to stand units on the ground.
struct TerrainField {
    MapDefinition map;
    Noise noise;
    /// Half width of a cliff face, cells.
    double cliffHalf = 0.6;

    /// `(lo: Vec2, hi: Vec2)`.
    struct Bounds { Vec2 lo, hi; };
    /// Each plateau's polygon bounds: past them by `edgeReach` a plateau
    /// covers nothing, so its polygon need not be measured.
    std::vector<Bounds> plateauBounds;
    /// How far past its polygon a plateau's edge reaches: the cliff's half
    /// width plus the most the wobble moves it (fbm is within ±1).
    double edgeReach() const { return cliffHalf + 1.3 + 0.25 + 0.01; }

    explicit TerrainField(const MapDefinition& map_);

    /// Where noise is sampled for `p`: on a mirrored map the east half reads
    /// the west half's noise, so the two halves are one shape.
    Vec2 fold(Vec2 p) const {
        // A square-symmetric map (C++ only) reads the noise of the eighth
        // 0 <= z <= x: every start has the same ground.
        if (map.squareSymmetric == true) {
            const double x = std::fabs(p.x), z = std::fabs(p.y);
            return z > x ? Vec2(z, x) : Vec2(x, z);
        }
        if (!map.mirrorX || !(p.x > *map.mirrorX)) return p;
        const double m = *map.mirrorX;
        return Vec2(2 * m - p.x, p.y);
    }

    /// How far the noise moves every plateau's edge at `p` (the same for
    /// each plateau, so worked out once a point).
    double edgeWobble(Vec2 p) const;

    /// Plateau `k`'s coverage in [0, 1] (1 = on top), with the point's
    /// `wobble`. Exactly 0 well away from it, without measuring its polygon.
    double plateauCoverage(Vec2 p, size_t k, double wobble) const;

    /// `(height: Double, crag: Double)`.
    struct Cliff { double height, crag; };
    /// Height before ramps and base pads: the stacked plateaus plus craggy
    /// noise on the cliff faces.
    Cliff cliffHeight(Vec2 p) const;

    /// `(weight: Double, height: Double)`.
    struct RampBlend { double weight, height; };
    /// Ramp override: blend weight and the ramp's own height.
    RampBlend ramp(Vec2 p) const;

    /// Terrain height at a ground point.
    double height(Vec2 p) const;

    /// How far a ground point lies outside the map's bounds, cells (0 on it).
    double outside(Vec2 p) const;

    /// Ground for the scenery past the map's edge, out of play: the map's
    /// own ground at the edge, rising over a dozen cells into rough highland
    /// and ridges further out, so the view never ends in the void.
    double borderHeight(Vec2 p) const;

    /// Discrete level under a point (for placement), ignoring cliff blending.
    int64_t level(Vec2 p) const;

    /// `SIMD4<Double>`: grass, highland, plating, scorch.
    struct Materials {
        double x = 0, y = 0, z = 0, w = 0;
        bool operator==(const Materials&) const = default;
    };
    /// Ground material weights: grass, highland, plating, scorch. Dirt fills
    /// whatever is left. Cliff rock is chosen by slope in the renderer.
    Materials materials(Vec2 p) const;

    /// The ground point under canvas point `c`: the first place the camera
    /// ray meets the terrain surface, so a click on a cliff top picks the
    /// plateau, not the low ground behind it.
    Vec2 pick(Vec2 c, const CanvasProjection& projection) const;

    /// The same for a ray from `o` along `direction` (a camera that has
    /// moved off the projection's focus); nil when it points at the sky.
    std::optional<Vec2> pick(Vec3 o, Vec3 direction) const;
};

inline double smoothstep(double e0, double e1, double x) {
    const double t = min(max((x - e0) / (e1 - e0), 0.0), 1.0);
    return t * t * (3 - 2 * t);
}

/// Signed distance from `p` to a polygon: negative inside.
double signedDistance(Vec2 p, const std::vector<Vec2>& poly);

} // namespace ac
