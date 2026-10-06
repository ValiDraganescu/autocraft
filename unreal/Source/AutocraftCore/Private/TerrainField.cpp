// Port of Sources/GameCore/TerrainField.swift.
#include "TerrainField.h"

#include <cmath>
#include <limits>

namespace ac {

TerrainField::TerrainField(const MapDefinition& map_) : map(map_), noise(map_.seed) {
    const double inf = std::numeric_limits<double>::infinity();
    plateauBounds.reserve(map.plateaus.size());
    for (const Plateau& p : map.plateaus) {
        Vec2 lo = Vec2::repeating(inf), hi = Vec2::repeating(-inf);
        for (Vec2 q : p.polygon) lo = min(lo, q);
        for (Vec2 q : p.polygon) hi = max(hi, q);
        plateauBounds.push_back(Bounds{lo, hi});
    }
}

/// How far the noise moves every plateau's edge at `p` (the same for
/// each plateau, so worked out once a point).
double TerrainField::edgeWobble(Vec2 p) const {
    const Vec2 q = fold(p);
    return noise.fbm(q.x * 0.16, q.y * 0.16) * 1.3 + noise.fbm(q.x * 0.7, q.y * 0.7) * 0.25;
}

/// Plateau `k`'s coverage in [0, 1] (1 = on top), with the point's
/// `wobble`. Exactly 0 well away from it, without measuring its polygon.
double TerrainField::plateauCoverage(Vec2 p, size_t k, double wobble) const {
    const Bounds& b = plateauBounds[k];
    const double out = length(max(max(b.lo - p, p - b.hi), Vec2::zero));
    if (!(out < edgeReach())) return 0;
    const double sd = signedDistance(p, map.plateaus[k].polygon) + wobble;
    return smoothstep(cliffHalf, -cliffHalf, sd);
}

/// Height before ramps and base pads: the stacked plateaus plus craggy
/// noise on the cliff faces.
TerrainField::Cliff TerrainField::cliffHeight(Vec2 p) const {
    double h = 0.0, crag = 0.0;
    const double wobble = edgeWobble(p);
    for (size_t k = 0; k < map.plateaus.size(); k++) {
        const Plateau& plateau = map.plateaus[k];
        const double t = plateauCoverage(p, k, wobble);
        h = max(h, static_cast<double>(plateau.level) * map.levelHeight * t);
        crag = max(crag, 4 * t * (1 - t));
    }
    return Cliff{h, crag};
}

/// Ramp override: blend weight and the ramp's own height.
TerrainField::RampBlend TerrainField::ramp(Vec2 p) const {
    RampBlend best{0.0, 0.0};
    for (const Ramp& r : map.ramps) {
        const Vec2 axis = r.high - r.low;
        const double len = length(axis);
        if (!(len > 0)) continue;
        const Vec2 dir = axis / len;
        const Vec2 perp = Vec2(-dir.y, dir.x);
        const double u = dot(p - r.low, dir) / len;
        const double v = std::fabs(dot(p - r.low, perp)) / (r.width / 2);
        const double w = (1 - smoothstep(0.8, 1.2, v)) * smoothstep(-0.35, -0.02, u) * (1 - smoothstep(1.02, 1.35, u));
        if (w > best.weight) {
            const double hl = static_cast<double>(r.lowLevel) * map.levelHeight,
                         hh = static_cast<double>(r.highLevel) * map.levelHeight;
            const double s = min(max(u, 0.0), 1.0);
            best = RampBlend{w, hl + (hh - hl) * (s * s * (3 - 2 * s))};
        }
    }
    return best;
}

/// Terrain height at a ground point.
double TerrainField::height(Vec2 p) const {
    // The playground is level everywhere: anything goes down anywhere.
    if (map.playground == true) return 0;
    const Cliff c = cliffHeight(p);
    const double base = c.height, crag = c.crag;
    const Vec2 q = fold(p);
    double h = base + crag * noise.fbm(q.x * 0.9 + 3.1, q.y * 0.9) * 0.45;
    const RampBlend r = ramp(p);
    if (r.weight > 0) h = h + (r.height - h) * r.weight;
    // Gentle rolling ground everywhere, flattened under base sites.
    double roll = noise.fbm(q.x * 0.07, q.y * 0.07) * 0.22 + noise.fbm(q.x * 0.45, q.y * 0.45) * 0.05;
    for (const BaseSite& b : map.bases) {
        const double d = distance(p, b.center);
        const double flat = 1 - smoothstep(5.0, 8.5, d);
        if (flat > 0) {
            const double target = static_cast<double>(b.level) * map.levelHeight;
            h = h + (target - h) * flat * (1 - crag);
            roll *= 1 - flat;
        }
    }
    return h + roll;
}

/// How far a ground point lies outside the map's bounds, cells (0 on it).
double TerrainField::outside(Vec2 p) const {
    const GroundRect& b = map.bounds;
    return length(max(max(Vec2(b.minX, b.minZ) - p, p - Vec2(b.maxX, b.maxZ)), Vec2::zero));
}

/// Ground for the scenery past the map's edge, out of play: the map's
/// own ground at the edge, rising over a dozen cells into rough highland
/// and ridges further out, so the view never ends in the void.
double TerrainField::borderHeight(Vec2 p) const {
    const double d = outside(p);
    const double h = height(p);
    if (!(d > 0)) return h;
    const Vec2 q = fold(p);
    const double hills = smoothstep(0.5, 8, d) * (2.8 + 2.2 * noise.fbm(q.x * 0.06 + 17, q.y * 0.06 - 5));
    const double ridges = smoothstep(6, 26, d) * (4 + 5 * max(noise.fbm(q.x * 0.03 - 9, q.y * 0.03 + 23), -0.4));
    const double crags = smoothstep(1.5, 8, d) * (noise.fbm(q.x * 0.3 + 5, q.y * 0.3 + 2) * 1.3
                                                  + noise.fbm(q.x * 0.9 - 3, q.y * 0.9 + 8) * 0.35);
    return h + hills + ridges + crags;
}

/// Discrete level under a point (for placement), ignoring cliff blending.
int64_t TerrainField::level(Vec2 p) const {
    int64_t l = 0;
    for (size_t k = 0; k < map.plateaus.size(); k++) {
        const Plateau& plateau = map.plateaus[k];
        // Off its polygon's box (with room for rounding) a point is outside
        // it, so its signed distance is not negative: skip measuring it.
        if (k < plateauBounds.size()) {
            const Bounds& b = plateauBounds[k];
            if (p.x < b.lo.x - 1e-6 || p.x > b.hi.x + 1e-6 || p.y < b.lo.y - 1e-6 || p.y > b.hi.y + 1e-6) continue;
        }
        if (!(signedDistance(p, plateau.polygon) < 0)) continue;
        l = std::max(l, plateau.level);
    }
    return l;
}

/// Ground material weights: grass, highland, plating, scorch. Dirt fills
/// whatever is left. Cliff rock is chosen by slope in the renderer.
TerrainField::Materials TerrainField::materials(Vec2 p) const {
    double grass = 0.0, highland = 0.0, plating = 0.0, scorch = 0.0;
    const double wobble = edgeWobble(p);
    for (size_t k = 0; k < map.plateaus.size(); k++) {
        if (!(map.plateaus[k].level > 0)) continue;
        highland = max(highland, plateauCoverage(p, k, wobble));
    }
    const Vec2 q = fold(p);
    highland *= 0.55 + 0.45 * smoothstep(-0.3, 0.3, noise.fbm(q.x * 0.11 + 40, q.y * 0.11));
    for (const GroundPatch& patch : map.patches) {
        const double d = distance(p, patch.center);
        const double edge = noise.fbm(q.x * 0.35 + 11, q.y * 0.35 - 7) * patch.radius * 0.45;
        const double w = smoothstep(patch.radius, patch.radius * 0.55, d + edge);
        switch (patch.kind) {
        case GroundPatch::Kind::grass: grass = max(grass, w); break;
        case GroundPatch::Kind::gravel: highland = max(highland, w * 0.8); break;
        case GroundPatch::Kind::scorch: scorch = max(scorch, w); break;
        }
    }
    // Grass thins out on cliff faces and ramps.
    const RampBlend r = ramp(p);
    grass *= 1 - r.weight * 0.7;
    // Foundation plate under each player's first Citadel.
    for (int64_t b : map.starts) {
        if (!(b >= 0 && b < static_cast<int64_t>(map.bases.size()))) continue;
        const Vec2 d = p - map.bases[static_cast<size_t>(b)].center;
        if (!(std::fabs(d.x) < 8) || !(std::fabs(d.y) < 8)) continue;
        // Rounded square, 7 cells across.
        const Vec2 sq = Vec2(std::fabs(d.x), std::fabs(d.y)) - Vec2(3.0, 3.0);
        const double sdf = length(max(sq, Vec2::zero)) + min(max(sq.x, sq.y), 0.0) - 0.5;
        plating = max(plating, smoothstep(0.12, -0.12, sdf));
        // Dust around the plate from constant traffic.
        scorch = max(scorch, smoothstep(6.5, 3.6, length(d)) * 0.35 * (1 - plating));
    }
    return Materials{grass, highland, plating, scorch};
}

/// Signed distance from `p` to a polygon: negative inside.
double signedDistance(Vec2 p, const std::vector<Vec2>& poly) {
    if (!(poly.size() > 2)) return std::numeric_limits<double>::infinity();
    double d = length_squared(p - poly[0]);
    bool inside = false;
    size_t j = poly.size() - 1;
    for (size_t i = 0; i < poly.size(); i++) {
        const Vec2 a = poly[i], b = poly[j];
        const Vec2 e = b - a, w = p - a;
        const double t = min(max(dot(w, e) / dot(e, e), 0.0), 1.0);
        d = min(d, length_squared(w - e * t));
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) {
            inside = !inside;
        }
        j = i;
    }
    return inside ? -std::sqrt(d) : std::sqrt(d);
}

/// The ground point under canvas point `c`: the first place the camera
/// ray meets the terrain surface, so a click on a cliff top picks the
/// plateau, not the low ground behind it.
Vec2 TerrainField::pick(Vec2 c, const CanvasProjection& projection) const {
    if (auto g = pick(projection.cameraPosition(), projection.ray(c))) return *g;
    return projection.ground(c);
}

/// The same for a ray from `o` along `direction` (a camera that has
/// moved off the projection's focus); nil when it points at the sky.
std::optional<Vec2> TerrainField::pick(Vec3 o, Vec3 direction) const {
    const Vec3 d = normalize(direction);
    const double top = map.levelHeight * 3;
    if (!(d.y < 0)) return std::nullopt;
    // Start where the ray drops below anything the terrain can reach.
    double t = max((top - o.y) / d.y, 0.0);
    const double step = 0.1;
    auto above = [&](double at) {
        const Vec3 q = o + d * at;
        return q.y > height(Vec2(q.x, q.z));
    };
    const double limit = (-2 * top - o.y) / d.y;
    while (t < limit && above(t + step)) t += step;
    double lo = t, hi = t + step;
    for (int i = 0; i < 20; i++) {
        const double mid = (lo + hi) / 2;
        if (above(mid)) lo = mid; else hi = mid;
    }
    const Vec3 q = o + d * hi;
    return Vec2(q.x, q.z);
}

} // namespace ac
