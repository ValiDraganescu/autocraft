// Port of Sources/GameCore/WindowMaps.swift.
#include "WindowMaps.h"

#include "MapLibrary.h"
#include "ScreenConfig.h"
#include "TerrainField.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ac {

std::string title(MapStyle s) {
    switch (s) {
    case MapStyle::highlands: return "Highlands";
    case MapStyle::openField: return "Open Field";
    case MapStyle::ridge: return "The Ridge";
    case MapStyle::badlands: return "Badlands";
    }
    return "";
}

std::string blurb(MapStyle s) {
    switch (s) {
    case MapStyle::highlands: return "Each side holds a high plateau with its main and natural, one ramp down to a low corridor with contested bases between them.";
    case MapStyle::openField: return "Flat and open, with no cliffs. Mains on the far edges, short rock walls, and big fights in the middle.";
    case MapStyle::ridge: return "A high ridge runs down the middle with two bases on top and a ramp at each end. The mains sit on open low ground either side.";
    case MapStyle::badlands: return "Each main is a small high pocket with one ramp. Everything else is low ground strewn with rocks and spread-out bases.";
    }
    return "";
}

std::string title(MapSize s) {
    switch (s) {
    case MapSize::small: return "Small";
    case MapSize::medium: return "Medium";
    case MapSize::large: return "Large";
    case MapSize::huge: return "Huge";
    }
    return "";
}

/// Ground size, cells.
MapCells cells(MapSize s) {
    switch (s) {
    case MapSize::small: return {96, 64};
    case MapSize::medium: return {144, 96};
    case MapSize::large: return {200, 128};
    case MapSize::huge: return {272, 176};
    }
    return {96, 64};
}

/// Bases each side gets beyond its main and natural, and how many
/// contested bases go down the middle.
MapExtras extras(MapSize s) {
    switch (s) {
    case MapSize::small: return {0, 1};
    case MapSize::medium: return {1, 2};
    case MapSize::large: return {2, 3};
    case MapSize::huge: return {3, 3};
    }
    return {0, 1};
}

/// "Style · Size" (a middle dot between); "Style · Size · 8 players" for a
/// square map (C++ only).
std::string MapChoice::name() const {
    const std::string two = title(style) + " \xC2\xB7 " + title(size);
    return players > 2 ? two + " \xC2\xB7 " + std::to_string(players) + " players" : two;
}

/// The playground's ground (`autocraft models`): flat and open, a few
/// patches of grass and gravel, and nothing standing on it. What stands
/// on it is put down by hand (`Simulation+Playground`).
MapDefinition WindowMaps::playground() {
    const double hw = 36.0, hd = 24.0;
    const uint64_t seed = fnv1a64("playground");
    SeededRandom rng(seed);
    MapDefinition map;
    map.name = "Playground";
    map.version = 1;
    map.seed = seed;
    map.bounds = GroundRect{-hw, -hd, hw, hd};
    map.levelHeight = 2.4;
    map.playground = true;
    for (int i = 0; i < 10; i++) {
        const GroundPatch::Kind kind = rng.unit() < 0.65 ? GroundPatch::Kind::grass : GroundPatch::Kind::gravel;
        const double x = rng.range(-hw, hw);
        const double z = rng.range(-hd, hd);
        const double radius = rng.range(4, 8);
        map.patches.push_back(GroundPatch{kind, Vec2(x, z), radius});
    }
    return map;
}

/// `decorated: false` leaves out the rocks and plants (the picker's
/// preview needs only the ground and the bases).
MapDefinition WindowMaps::build(const MapChoice& choice, bool decorated) {
    if (choice.players > 2) return buildSquare(choice, decorated);
    const MapCells size = cells(choice.size);
    const double W = size.width, D = size.depth;
    const double hw = W / 2, hd = D / 2, H = 2.4;
    const uint64_t seed = fnv1a64("window|" + std::string(rawValue(choice.style)) + "|" + std::string(rawValue(choice.size)));
    SeededRandom rng(seed);
    const double axis = 0.0;
    auto mirror = [axis](Vec2 p) { return Vec2(2 * axis - p.x, p.y); };
    auto mirrorBase = [&mirror](const BaseSite& b) {
        BaseSite m = b;
        m.center = mirror(b.center);
        m.ore.clear();
        for (const OreSpec& o : b.ore) m.ore.push_back(OreSpec{mirror(o.position), o.amount, -o.angle});
        m.wells.clear();
        for (Vec2 w : b.wells) m.wells.push_back(mirror(w));
        return m;
    };
    // A base on the axis keeps its west patches and gains their mirror images.
    auto symmetric = [&mirror, axis](const BaseSite& b) {
        BaseSite m = b;
        std::vector<OreSpec> west;
        for (const OreSpec& o : b.ore) if (o.position.x < axis) west.push_back(o);
        m.ore = west;
        for (auto it = west.rbegin(); it != west.rend(); ++it) m.ore.push_back(OreSpec{mirror(it->position), it->amount, -it->angle});
        return m;
    };
    auto mirrorPolygon = [&mirror](const std::vector<Vec2>& poly) {
        std::vector<Vec2> out;
        for (auto it = poly.rbegin(); it != poly.rend(); ++it) out.push_back(mirror(*it));
        return out;
    };

    std::vector<Plateau> plateaus;
    std::vector<Ramp> ramps;
    /// West bases in order (the first is blue's main), and the axis bases.
    struct WestSpot { Vec2 center; double facing; };
    std::vector<WestSpot> west;
    std::vector<double> onAxis;
    const double beyond = 30.0;
    switch (choice.style) {
    case MapStyle::highlands: {
        // A west plateau to the left of a low corridor; its cliff wobbles.
        const double cx = -0.3 * hw;
        auto cliffX = [cx](double, double jitter) { return cx + jitter; };
        std::vector<Vec2> poly = {Vec2(-hw - beyond, -hd - beyond), Vec2(cx, -hd - beyond)};
        const int64_t steps = 8;
        for (int64_t i = 0; i <= steps; i++) {
            const double z = -hd + D * static_cast<double>(i) / static_cast<double>(steps);
            // The ramp's stretch of cliff stays straight.
            const bool straight = std::fabs(z - 0.02 * hd) < 8;
            const double jitter = straight ? 0 : rng.range(-2.5, 2.5);
            poly.push_back(Vec2(cliffX(z, jitter), z));
        }
        poly.push_back(Vec2(cx, hd + beyond));
        poly.push_back(Vec2(-hw - beyond, hd + beyond));
        const Plateau plateau{poly, 1};
        plateaus = {plateau, Plateau{mirrorPolygon(plateau.polygon), 1}};
        const Ramp ramp{Vec2(cx + 4.5, 0.02 * hd), Vec2(cx - 4.5, 0.02 * hd), 3.6, 0, 1};
        ramps = {ramp, Ramp{mirror(ramp.low), mirror(ramp.high), 3.6, 0, 1}};
        west = {{Vec2(-0.72 * hw, -0.30 * hd), 1.2 * pi}, {Vec2(-0.55 * hw, 0.46 * hd), 0.95 * pi}};
        onAxis = axisSpots(extras(choice.size).axis, hd);
        break;
    }
    case MapStyle::openField:
        west = {{Vec2(-0.72 * hw, -0.06 * hd), 1.0 * pi}, {Vec2(-0.5 * hw, 0.5 * hd), 0.8 * pi}};
        onAxis = axisSpots(extras(choice.size).axis, hd);
        break;
    case MapStyle::ridge: {
        // A ridge down the axis: two bases on top, a ramp at each end.
        const double rw = max(15.0, 0.24 * hw);
        const double ends = 0.78 * hd;
        const std::vector<Vec2> poly = {Vec2(-0.7 * rw, -ends), Vec2(0.7 * rw, -ends), Vec2(rw, -0.55 * hd), Vec2(rw, 0.55 * hd),
                                        Vec2(0.7 * rw, ends), Vec2(-0.7 * rw, ends), Vec2(-rw, 0.55 * hd), Vec2(-rw, -0.55 * hd)};
        plateaus = {Plateau{poly, 1}};
        ramps = {Ramp{Vec2(0, -ends - 4.5), Vec2(0, -ends + 4.5), 3.6, 0, 1},
                 Ramp{Vec2(0, ends + 4.5), Vec2(0, ends - 4.5), 3.6, 0, 1}};
        west = {{Vec2(-0.7 * hw, -0.05 * hd), 1.0 * pi}, {Vec2(-0.5 * hw, 0.48 * hd), 0.8 * pi}};
        onAxis = choice.size == MapSize::small ? std::vector<double>{0} : std::vector<double>{-0.3 * hd, 0.3 * hd};
        break;
    }
    case MapStyle::badlands: {
        // Each main on its own small high pocket, one ramp facing the middle.
        const Vec2 pocket = Vec2(-0.7 * hw, -0.22 * hd);
        const double r = 17.0;
        std::vector<Vec2> poly;
        const int64_t n = 12;
        for (int64_t i = 0; i < n; i++) {
            const double a = static_cast<double>(i) / static_cast<double>(n) * 2 * pi;
            // Exactly `r` toward the ramp (a = 0), a little ragged elsewhere.
            const double k = i == 0 || i == 1 || i == n - 1 ? 1.0 : rng.range(0.9, 1.08);
            poly.push_back(pocket + Vec2(std::cos(a), std::sin(a)) * r * k);
        }
        const Plateau plateau{poly, 1};
        plateaus = {plateau, Plateau{mirrorPolygon(plateau.polygon), 1}};
        const Ramp ramp{pocket + Vec2(r + 4.5, 0), pocket + Vec2(r - 4.5, 0), 3.6, 0, 1};
        ramps = {ramp, Ramp{mirror(ramp.low), mirror(ramp.high), 3.6, 0, 1}};
        west = {{pocket, 1.05 * pi}, {Vec2(-0.5 * hw, 0.5 * hd), 0.85 * pi}};
        onAxis = axisSpots(extras(choice.size).axis, hd);
        break;
    }
    }

    MapDefinition map;
    map.name = choice.name();
    map.version = 1;
    map.seed = seed;
    map.bounds = GroundRect{-hw, -hd, hw, hd};
    map.levelHeight = H;
    map.plateaus = plateaus;
    map.ramps = ramps;
    map.mirrorX = axis;
    const TerrainField field(map);
    const double margin = 14.0;

    /// A Citadel and its ore arc fit: flat ground of one level
    /// within the arc's reach, clear of ramps, inside the map, and far
    /// enough from every base already placed.
    auto fits = [&](Vec2 c, const std::vector<Vec2>& others) -> std::optional<int64_t> {
        if (!(std::fabs(c.x) < hw - margin) || !(std::fabs(c.y) < hd - margin)) return std::nullopt;
        for (Vec2 o : others) if (!(distance(o, c) >= 22)) return std::nullopt;
        const double h0 = field.height(c);
        const int64_t level = static_cast<int64_t>(rounded(h0 / H));
        if (!(std::fabs(h0 - static_cast<double>(level) * H) < 0.2)) return std::nullopt;
        for (int64_t k = 0; k < 16; k++) {
            const double a = static_cast<double>(k) / 16 * 2 * pi;
            for (double r : {6.0, 11.0}) {
                const Vec2 q = c + Vec2(std::cos(a), std::sin(a)) * r;
                if (!(std::fabs(field.height(q) - h0) < 0.25)) return std::nullopt;
            }
        }
        for (const Ramp& r : ramps) {
            const Vec2 e = r.high - r.low;
            const double t = min(max(dot(c - r.low, e) / dot(e, e), -0.4), 1.4);
            if (distance(c, r.low + e * t) < 14) return std::nullopt;
        }
        return level;
    };

    std::vector<BaseSite> westSites;
    std::vector<Vec2> placed;
    // The level the natural expansion is on: the main's plateau on the
    // highlands, low ground elsewhere.
    const int64_t naturalLevel = choice.style == MapStyle::highlands ? 1 : 0;
    for (size_t i = 0; i < west.size(); i++) {
        const Vec2 c = west[i].center;
        const double facing = west[i].facing;
        if (i == 0) {
            // The main is placed as authored; the map tests check it.
            const auto fit = fits(c, placed);
            const int64_t level = fit ? *fit : static_cast<int64_t>(rounded(field.height(c) / H));
            westSites.push_back(BaseSite::standard(c, level, facing));
            placed.push_back(c);
            placed.push_back(mirror(c));
            continue;
        }
        // The natural: the authored spot, else the nearest ground where one fits.
        std::vector<Vec2> candidates = {c};
        for (double r = 2.0; r <= 24.0; r += 2.0) {
            for (int64_t k = 0; k < 16; k++) {
                const double a = static_cast<double>(k) / 16 * 2 * pi;
                candidates.push_back(c + Vec2(std::cos(a), std::sin(a)) * r);
            }
        }
        for (Vec2 q : candidates) {
            const auto level = fits(q, placed);
            if (!level || !(*level == naturalLevel) || !(distance(q, mirror(q)) >= 28)) continue;
            westSites.push_back(BaseSite::standard(q, *level, facing));
            placed.push_back(q);
            placed.push_back(mirror(q));
            break;
        }
    }
    std::vector<BaseSite> axisSites;
    for (double z : onAxis) {
        const Vec2 c = Vec2(axis, z);
        const auto level = fits(c, placed);
        if (!level) continue;
        axisSites.push_back(symmetric(BaseSite::standard(c, *level, z > 0 ? 0.5 * pi : 1.5 * pi)));
        placed.push_back(c);
    }
    // More bases each side, wherever one fits, spread by a seeded shuffle.
    int64_t extra = extras(choice.size).perSide;
    int64_t tries = 0;
    while (extra > 0 && tries < 600) {
        tries += 1;
        const double x = rng.range(-hw + margin + 4, -14);
        const double y = rng.range(-hd + margin, hd - margin);
        const Vec2 c = Vec2(x, y);
        const auto level = fits(c, placed);
        if (!level || !(distance(c, mirror(c)) >= 28)) continue;
        // The ridge's top is for the axis bases; the rest go on the low ground.
        if (choice.style == MapStyle::ridge && *level > 0) continue;
        const double facing = std::atan2(c.y, c.x) + rng.range(-0.3, 0.3);
        westSites.push_back(BaseSite::standard(c, *level, facing));
        placed.push_back(c);
        placed.push_back(mirror(c));
        extra -= 1;
    }
    map.bases = westSites;
    for (const BaseSite& b : westSites) map.bases.push_back(mirrorBase(b));
    map.bases.insert(map.bases.end(), axisSites.begin(), axisSites.end());
    map.starts = {0, static_cast<int64_t>(westSites.size())};

    // Ground paint on the west, mirrored, and a scorch mark on the axis.
    std::vector<GroundPatch> patches;
    const int64_t paint = static_cast<int64_t>(max(6.0, W * D / 700));
    for (int64_t i = 0; i < paint; i++) {
        const double px = rng.range(-hw, -3);
        const double pz = rng.range(-hd, hd);
        const Vec2 p = Vec2(px, pz);
        const GroundPatch::Kind kind = rng.unit() < 0.7 ? GroundPatch::Kind::grass : GroundPatch::Kind::gravel;
        patches.push_back(GroundPatch{kind, p, rng.range(4, 9)});
    }
    {
        std::vector<GroundPatch> mirrored;
        for (const GroundPatch& p : patches) { GroundPatch m = p; m.center = mirror(p.center); mirrored.push_back(m); }
        patches.insert(patches.end(), mirrored.begin(), mirrored.end());
    }
    patches.push_back(GroundPatch{GroundPatch::Kind::scorch, Vec2(axis, 0), 2.5});
    map.patches = patches;

    if (!decorated) return map;
    // Rocks along the cliffs and scattered over the ground, then the
    // east half is the west half's mirror image.
    cliffRocksAndScatter(map, hw, hd, margin);
    if (choice.style == MapStyle::openField) rockWalls(map, hw, hd, rng);
    std::vector<Doodad> westDoodads, onAxisDoodads;
    for (const Doodad& d : map.doodads) if (d.position.x < axis - 0.01) westDoodads.push_back(d);
    for (const Doodad& d : map.doodads) if (std::fabs(d.position.x - axis) <= 0.01) onAxisDoodads.push_back(d);
    std::vector<Doodad> all = westDoodads;
    all.insert(all.end(), onAxisDoodads.begin(), onAxisDoodads.end());
    for (const Doodad& d : westDoodads) {
        Doodad m = d;
        m.position = mirror(d.position);
        m.rotation = -d.rotation;
        m.mirrored = true;
        all.push_back(m);
    }
    map.doodads = all;
    return map;
}

// MARK: - Square maps for teams (C++ only, PORTING.md "Teams")

namespace {

/// One of the square's eight symmetries about (0, 0): a mirror across
/// x = 0 when `flip`, then `quarters` quarter turns (from +x toward +z).
/// Both are exact in floating point, so images are exact.
struct Sym { int quarters; bool flip; };

const Sym* syms() {
    static const Sym all[8] = {{0, false}, {1, false}, {2, false}, {3, false}, {3, true}, {0, true}, {1, true}, {2, true}};
    return all;
}

Vec2 apply(const Sym& t, Vec2 p) {
    Vec2 q = t.flip ? Vec2(-p.x, p.y) : p;
    for (int i = 0; i < t.quarters; i++) q = Vec2(-q.y, q.x);
    return q;
}

/// A turn of a thing laid down turned by `rotation` (a doodad, an ore
/// patch: the scene turns it by -rotation in x, z), seen through `t`.
double turned(const Sym& t, double rotation) {
    return (t.flip ? -rotation : rotation) - static_cast<double>(t.quarters) * pi / 2;
}

/// The symmetries that take `anchor` to distinct places, one for each
/// (eight inside the eighth 0 < z < x, four on its edges, one at the middle).
std::vector<Sym> distinct(Vec2 anchor) {
    std::vector<Sym> out;
    std::vector<Vec2> seen;
    for (int k = 0; k < 8; k++) {
        const Sym& t = syms()[k];
        const Vec2 q = apply(t, anchor);
        bool again = false;
        for (Vec2 v : seen) if (distance(v, q) < 1e-6) { again = true; break; }
        if (again) continue;
        seen.push_back(q);
        out.push_back(t);
    }
    return out;
}

BaseSite image(const Sym& t, const BaseSite& b) {
    BaseSite m = b;
    m.center = apply(t, b.center);
    m.ore.clear();
    for (const OreSpec& o : b.ore) m.ore.push_back(OreSpec{apply(t, o.position), o.amount, turned(t, o.angle)});
    m.wells.clear();
    for (Vec2 w : b.wells) m.wells.push_back(apply(t, w));
    return m;
}

/// A base on a mirror line of the square (an axis or a diagonal) keeps
/// its patches and well on one side of the line and gains their mirror
/// images, so it is its own mirror image.
BaseSite selfMirrored(const BaseSite& b) {
    if (!(length(b.center) > 1e-6)) return b;
    std::optional<Sym> mirror;
    for (int k = 4; k < 8; k++) {
        if (distance(apply(syms()[k], b.center), b.center) < 1e-6) { mirror = syms()[k]; break; }
    }
    if (!mirror) return b;
    const Vec2 u = normalize(b.center);
    auto side = [u](Vec2 p) { return u.x * p.y - u.y * p.x; };
    BaseSite m = b;
    std::vector<OreSpec> kept;
    for (const OreSpec& o : b.ore) if (side(o.position) < 0) kept.push_back(o);
    m.ore = kept;
    for (auto it = kept.rbegin(); it != kept.rend(); ++it) {
        m.ore.push_back(OreSpec{apply(*mirror, it->position), it->amount, turned(*mirror, it->angle)});
    }
    std::vector<Vec2> wells;
    for (Vec2 w : b.wells) if (side(w) < 0) wells.push_back(w);
    m.wells = wells;
    for (Vec2 w : wells) m.wells.push_back(apply(*mirror, w));
    return m;
}

double angleOf(Vec2 p) { return std::atan2(p.y, p.x); }

} // namespace

/// Half the side of a square map, cells: room for every player's main,
/// natural and more bases at the size.
double WindowMaps::squareHalf(MapSize size, int64_t players) {
    const bool eight = players > 4;
    switch (size) {
    case MapSize::small: return eight ? 112 : 80;
    case MapSize::medium: return eight ? 128 : 96;
    case MapSize::large: return eight ? 144 : 112;
    case MapSize::huge: return eight ? 160 : 128;
    }
    return eight ? 112 : 80;
}

MapDefinition WindowMaps::buildSquare(const MapChoice& choice, bool decorated) {
    const int64_t players = choice.players > 4 ? 8 : 4;
    const double h = squareHalf(choice.size, players), H = 2.4;
    const uint64_t seed = fnv1a64("window|" + std::string(rawValue(choice.style)) + "|" + std::string(rawValue(choice.size)) +
                                  "|" + std::to_string(players));
    SeededRandom rng(seed);
    const double margin = 14.0;
    // The main of the eighth 0 <= z <= x: on its diagonal edge with four
    // players (its four turns are the mains), inside it with eight (its
    // eight images).
    const Vec2 main = players == 8 ? Vec2(std::cos(pi / 8), std::sin(pi / 8)) * (0.78 * h) : Vec2(0.57 * h, 0.57 * h);
    const Vec2 inward = normalize(-main);
    const bool pockets = choice.style == MapStyle::highlands || choice.style == MapStyle::badlands;
    const double pocketR = choice.style == MapStyle::highlands ? 19.0 : 16.0;

    std::vector<Plateau> plateaus;
    std::vector<Ramp> ramps;
    if (pockets) {
        // Each main on a small high pocket, one ramp toward the middle.
        std::vector<Vec2> poly;
        const int64_t n = 12;
        const double a0 = angleOf(inward);
        for (int64_t i = 0; i < n; i++) {
            const double a = a0 + static_cast<double>(i) / static_cast<double>(n) * 2 * pi;
            const double ragged = choice.style == MapStyle::badlands ? 1.1 : 1.04;
            const double k = i == 0 || i == 1 || i == n - 1 ? 1.0 : rng.range(0.9, ragged);
            poly.push_back(main + Vec2(std::cos(a), std::sin(a)) * pocketR * k);
        }
        const Ramp ramp{main + inward * (pocketR + 4.5), main + inward * (pocketR - 4.5), 3.6, 0, 1};
        for (const Sym& t : distinct(main)) {
            std::vector<Vec2> q;
            for (Vec2 p : poly) q.push_back(apply(t, p));
            plateaus.push_back(Plateau{q, 1});
            ramps.push_back(Ramp{apply(t, ramp.low), apply(t, ramp.high), ramp.width, 0, 1});
        }
    } else if (choice.style == MapStyle::ridge) {
        // The middle raised: an octagon, a ramp up each diagonal.
        const double r = max(16.0, 0.24 * h);
        const Vec2 corner = Vec2(std::cos(pi / 8), std::sin(pi / 8)) * r;
        std::vector<Vec2> poly;
        for (const Sym& t : distinct(corner)) poly.push_back(apply(t, corner));
        std::sort(poly.begin(), poly.end(), [](Vec2 a, Vec2 b) { return angleOf(a) < angleOf(b); });
        plateaus.push_back(Plateau{poly, 1});
        const double inner = r * std::cos(pi / 8);
        const Vec2 d = Vec2(1, 1) / std::sqrt(2.0);
        const Ramp ramp{d * (inner + 4.5), d * (inner - 4.5), 3.6, 0, 1};
        for (const Sym& t : distinct(ramp.low)) ramps.push_back(Ramp{apply(t, ramp.low), apply(t, ramp.high), 3.6, 0, 1});
    }

    MapDefinition map;
    map.name = MapChoice{choice.style, choice.size, players}.name();
    map.version = 1;
    map.seed = seed;
    map.bounds = GroundRect{-h, -h, h, h};
    map.levelHeight = H;
    map.plateaus = plateaus;
    map.ramps = ramps;
    map.squareSymmetric = true;
    const TerrainField field(map);

    /// As `build`'s: flat ground of one level under the Citadel and its
    /// arc, clear of ramps, inside the map, far enough from every base.
    auto fits = [&](Vec2 c, const std::vector<Vec2>& others) -> std::optional<int64_t> {
        if (!(std::fabs(c.x) < h - margin) || !(std::fabs(c.y) < h - margin)) return std::nullopt;
        for (Vec2 o : others) if (!(distance(o, c) >= 22)) return std::nullopt;
        const double h0 = field.height(c);
        const int64_t level = static_cast<int64_t>(rounded(h0 / H));
        if (!(std::fabs(h0 - static_cast<double>(level) * H) < 0.2)) return std::nullopt;
        for (int64_t k = 0; k < 16; k++) {
            const double a = static_cast<double>(k) / 16 * 2 * pi;
            for (double r : {6.0, 11.0}) {
                const Vec2 q = c + Vec2(std::cos(a), std::sin(a)) * r;
                if (!(std::fabs(field.height(q) - h0) < 0.25)) return std::nullopt;
            }
        }
        for (const Ramp& r : ramps) {
            const Vec2 e = r.high - r.low;
            const double t = min(max(dot(c - r.low, e) / dot(e, e), -0.4), 1.4);
            if (distance(c, r.low + e * t) < 14) return std::nullopt;
        }
        return level;
    };
    /// Its images are one place, or far enough apart for a base each.
    auto apart = [](Vec2 c) {
        const std::vector<Sym> ts = distinct(c);
        for (size_t i = 0; i < ts.size(); i++) {
            for (size_t j = i + 1; j < ts.size(); j++) {
                if (!(distance(apply(ts[i], c), apply(ts[j], c)) >= 28)) return false;
            }
        }
        return true;
    };
    std::vector<BaseSite> bases;
    std::vector<Vec2> placed;
    auto put = [&](const BaseSite& b) {
        const BaseSite s0 = selfMirrored(b);
        for (const Sym& t : distinct(s0.center)) {
            bases.push_back(image(t, s0));
            placed.push_back(apply(t, s0.center));
        }
    };

    // The mains, facing away from the middle, in order round the map.
    {
        const auto fit = fits(main, placed);
        const int64_t level = fit ? *fit : static_cast<int64_t>(rounded(field.height(main) / H));
        put(BaseSite::standard(main, level, angleOf(main)));
        std::sort(bases.begin(), bases.end(), [](const BaseSite& a, const BaseSite& b) { return angleOf(a.center) < angleOf(b.center); });
        for (int64_t i = 0; i < static_cast<int64_t>(bases.size()); i++) map.starts.push_back(i);
    }
    // The natural: on low ground just out of the main's way toward the
    // middle, else the nearest ground where one fits.
    {
        const Vec2 c = main + inward * (pockets ? pocketR + 13 : 24.0);
        std::vector<Vec2> candidates = {c};
        for (double r = 2.0; r <= 32.0; r += 2.0) {
            for (int64_t k = 0; k < 16; k++) {
                const double a = static_cast<double>(k) / 16 * 2 * pi;
                candidates.push_back(c + Vec2(std::cos(a), std::sin(a)) * r);
            }
        }
        for (Vec2 q : candidates) {
            const auto level = fits(q, placed);
            if (!level || *level != 0 || !(q.y >= -1e-9 && q.y <= q.x + 1e-9) || !apart(q)) continue;
            put(BaseSite::standard(q, *level, angleOf(q)));
            break;
        }
    }
    // A contested base on each axis, between neighbouring mains.
    for (double r = 0.3 * h; r <= 0.62 * h; r += 2.0) {
        const Vec2 c = Vec2(r, 0);
        const auto level = fits(c, placed);
        if (!level) continue;
        put(BaseSite::standard(c, *level, 0));
        break;
    }
    // More bases, wherever one fits in the eighth, by a seeded draw: none on
    // a small map, one more with each size up.
    int64_t extra = static_cast<int64_t>(choice.size);
    int64_t tries = 0;
    while (extra > 0 && tries < 800) {
        tries += 1;
        const double x = rng.range(margin + 4, h - margin);
        const double z = rng.range(0, x);
        const Vec2 c = Vec2(x, z);
        const auto level = fits(c, placed);
        if (!level || *level > 0 || !apart(c)) continue;
        put(BaseSite::standard(c, *level, angleOf(c) + rng.range(-0.3, 0.3)));
        extra -= 1;
    }
    map.bases = bases;

    // Ground paint in the eighth, copied, and a scorch mark in the middle.
    const int64_t paint = static_cast<int64_t>(max(6.0, 4 * h * h / 700)) / 8 + 1;
    for (int64_t i = 0; i < paint; i++) {
        const double px = rng.range(3, h);
        const double pz = rng.range(0, px);
        const GroundPatch::Kind kind = rng.unit() < 0.7 ? GroundPatch::Kind::grass : GroundPatch::Kind::gravel;
        const GroundPatch p{kind, Vec2(px, pz), rng.range(4, 9)};
        for (const Sym& t : distinct(p.center)) { GroundPatch m = p; m.center = apply(t, p.center); map.patches.push_back(m); }
    }
    map.patches.push_back(GroundPatch{GroundPatch::Kind::scorch, Vec2::zero, 3.5});

    if (!decorated) return map;
    // Rocks along the cliffs and scattered over the eighth; the rest of
    // the map is its images.
    MapLibrary::cliffRocks(map, [h](Vec2 p, double) { return std::fabs(p.x) < h - 4 && std::fabs(p.y) < h - 4; });
    const int64_t count = static_cast<int64_t>(h * h / 2 / 45);
    MapLibrary::scatter(map, count, [h](SeededRandom& r, const TerrainField&) -> std::optional<Vec2> {
        const double x = r.range(4, h - 4);
        return Vec2(x, r.range(0, x));
    });
    std::vector<Doodad> all;
    for (const Doodad& d : map.doodads) {
        if (!(d.position.y > 0.01 && d.position.y < d.position.x - 0.01)) continue;
        for (int k = 0; k < 8; k++) {
            const Sym& t = syms()[k];
            Doodad m = d;
            m.position = apply(t, d.position);
            m.rotation = turned(t, d.rotation);
            m.mirrored = (d.mirrored == true) != t.flip ? std::optional<bool>(true) : std::nullopt;
            all.push_back(m);
        }
    }
    map.doodads = all;
    return map;
}

/// z positions of the contested bases down the middle, best first.
std::vector<double> WindowMaps::axisSpots(int64_t n, double hd) {
    switch (n) {
    case 1: return {0.55 * hd};
    case 2: return {-0.6 * hd, 0.6 * hd};
    default: return {-0.68 * hd, 0.68 * hd, 0};
    }
}

void WindowMaps::cliffRocksAndScatter(MapDefinition& map, double hw, double hd, double /*margin*/) {
    MapLibrary::cliffRocks(map, [hw, hd](Vec2 p, double) { return std::fabs(p.x) < hw - 4 && std::fabs(p.y) < hd - 4; });
    // Doodads from the west half only; the caller mirrors them.
    const int64_t count = static_cast<int64_t>(hw * hd * 2 / 45);
    MapLibrary::scatter(map, count, [hw, hd](SeededRandom& rng, const TerrainField&) -> std::optional<Vec2> {
        const double x = rng.range(-hw + 4, 0);
        const double z = rng.range(-hd + 4, hd - 4);
        return Vec2(x, z);
    });
}

/// Short walls of boulders on an open field: chokes to fight around, with
/// gaps at both ends so no base is ever sealed.
void WindowMaps::rockWalls(MapDefinition& map, double hw, double hd, SeededRandom& rng) {
    struct Wall { double x, z, len; };
    for (const Wall& wall : {Wall{-0.3 * hw, -0.32 * hd, 0.2 * hd}, Wall{-0.3 * hw, 0.3 * hd, 0.2 * hd}, Wall{-0.1 * hw, 0.0, 0.16 * hd}}) {
        double d = -wall.len / 2;
        while (d < wall.len / 2) {
            const double x = wall.x + rng.range(-0.3, 0.3);
            const Vec2 p = Vec2(x, wall.z + d);
            bool nearBase = false;
            for (const BaseSite& b : map.bases) if (distance(b.center, p) < 12) { nearBase = true; break; }
            if (!nearBase) {
                const double rotation = rng.range(0, 2 * pi);
                const double scale = rng.range(0.9, 1.2);
                const uint32_t variant = static_cast<uint32_t>(rng.next());
                map.doodads.push_back(Doodad{Doodad::Kind::boulder, p, rotation, scale, variant, std::nullopt});
            }
            d += 1.6;
        }
    }
}

} // namespace ac
