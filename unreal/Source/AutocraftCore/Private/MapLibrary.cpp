// Port of Sources/GameCore/MapLibrary.swift.
#include "MapLibrary.h"

#include "MapView.h"

#include <cmath>
#include <cstdlib>

namespace ac {

MapDefinition MapLibrary::map(const ScreenConfig& config, const CanvasProjection& projection) {
    if (config.shape() == handcraftedShape) {
        return handcrafted(projection, config);
    }
    return generated(config, projection);
}

// MARK: - Authoring helpers

Plateau MapLibrary::Author::poly(const std::vector<std::pair<double, double>>& pts, int64_t level) const {
    Plateau out;
    for (const auto& p : pts) out.polygon.push_back(g(p.first, p.second, level));
    out.level = level;
    return out;
}

// MARK: - Handcrafted

MapDefinition MapLibrary::handcrafted(const CanvasProjection& projection, const ScreenConfig& config) {
    const double H = 2.4;
    const Author a{projection, H};
    // Everything in play is on a screen, and the map is an exact mirror
    // across world x = 0, the centre line of the ultrawide (the camera
    // looks down it), so both halves fit the screens alike. The west
    // half is authored; the east half is its mirror image.
    const double axis = 0.0;
    auto mirror = [axis](Vec2 p) { return Vec2(2 * axis - p.x, p.y); };
    // Patch by patch: the standard arc alternates rows and amounts by
    // index, which a fresh arc facing the other way would swap.
    auto mirrorBase = [&mirror](const BaseSite& b) {
        BaseSite m = b;
        m.center = mirror(b.center);
        m.ore.clear();
        for (const OreSpec& o : b.ore) m.ore.push_back(OreSpec{mirror(o.position), o.amount, -o.angle});
        m.wells.clear();
        for (Vec2 w : b.wells) m.wells.push_back(mirror(w));
        return m;
    };
    // A base on the axis: its west patches, and their mirror images.
    auto symmetric = [&mirror, axis](const BaseSite& b) {
        BaseSite m = b;
        std::vector<OreSpec> west;
        for (const OreSpec& o : b.ore) if (o.position.x < axis) west.push_back(o);
        m.ore = west;
        for (auto it = west.rbegin(); it != west.rend(); ++it) m.ore.push_back(OreSpec{mirror(it->position), it->amount, -it->angle});
        return m;
    };
    // West plateau: high ground over the left of the ultrawide, its cliff
    // running down toward the laptop's top-left corner; authored on the
    // canvas, then moved `shift` cells west to leave the channel room.
    const Vec2 shift = Vec2(-3, 0);
    Plateau main;
    for (Vec2 p : a.poly({{-400, -400}, {1960, -400}, {2090, 260}, {1960, 640}, {2130, 1010},
                          {1930, 1270}, {1720, 1560}, {1500, 2900}, {-400, 2900}}, 1).polygon) {
        main.polygon.push_back(p + shift);
    }
    main.level = 1;
    Plateau east;
    for (auto it = main.polygon.rbegin(); it != main.polygon.rend(); ++it) east.polygon.push_back(mirror(*it));
    east.level = 1;
    const Ramp rampMain{a.g(2380, 1250) + shift, a.g(1880, 1085, 1) + shift, 3.6, 0, 1};
    const Ramp rampEast{mirror(rampMain.low), mirror(rampMain.high), 3.6, 0, 1};

    // Each plateau: a main at the back, a natural by the ramp. The
    // channel between them: a contested base at its far end, and one on
    // the laptop below where the ramps meet.
    const BaseSite mainBase = BaseSite::standard(Vec2(-34, -22), 1, 1.2 * pi);
    const BaseSite natural = BaseSite::standard(Vec2(-25, -7), 1, 1.0 * pi);
    const BaseSite third = mirrorBase(mainBase);
    const BaseSite redNatural = mirrorBase(natural);
    const BaseSite centre = symmetric(BaseSite::standard(Vec2(axis, -18), 0, 1.5 * pi));
    const BaseSite south = symmetric(BaseSite::standard(Vec2(axis, 12), 0, 0.5 * pi));

    // Ground paint on the west, mirrored.
    using K = GroundPatch::Kind;
    std::vector<GroundPatch> patches = {
        GroundPatch{K::grass, a.g(1850, 2400) + shift, 6},
        GroundPatch{K::grass, a.g(400, 1200, 1) + shift, 8},
        GroundPatch{K::grass, a.g(1600, 200, 1) + shift, 6},
        GroundPatch{K::gravel, a.g(2400, 1450) + shift, 6},
        GroundPatch{K::gravel, a.g(200, 300, 1) + shift, 5},
        GroundPatch{K::grass, Vec2(-6, -26), 8},
    };
    {
        std::vector<GroundPatch> mirrored;
        for (const GroundPatch& p : patches) { GroundPatch m = p; m.center = mirror(p.center); mirrored.push_back(m); }
        patches.insert(patches.end(), mirrored.begin(), mirrored.end());
    }
    patches.push_back(GroundPatch{K::scorch, Vec2(axis, a.g(2850, 1200).y), 2.5});
    patches.push_back(GroundPatch{K::grass, Vec2(axis, 18), 7});

    std::vector<Doodad> doodads;
    auto add = [&](Doodad::Kind k, double x, double y, int64_t level, double scale = 1, double rot = 0) {
        doodads.push_back(Doodad{k, a.g(x, y, level) + shift, rot, scale,
                                 static_cast<uint32_t>(static_cast<int64_t>(doodads.size()) * 7919 + 13), std::nullopt});
    };
    using D = Doodad::Kind;
    // Rocks along the cliff feet and a watchtower in the middle.
    add(D::rockSpire, 2230, 520, 0, 1.3);
    add(D::boulder, 2190, 700, 0, 1.1, 0.6);
    add(D::rock, 2140, 1450, 0, 0.9);
    doodads.push_back(Doodad{D::tower, Vec2(axis, a.g(2860, 1200).y), 0, 1,
                             static_cast<uint32_t>(static_cast<int64_t>(doodads.size()) * 7919 + 13), std::nullopt});
    add(D::crateStack, 1500, 900, 1, 1.0, 0.3);
    add(D::crate, 1590, 1000, 1, 1.0, 0.9);
    add(D::deadTree, 2450, 620, 0, 1.0);

    MapDefinition map;
    map.name = "Ashfall Reach";
    map.version = 12;
    map.seed = 0x5eed0a17;
    map.bounds = projection.visibleGround(-1, 2 * H, 5);
    map.levelHeight = H;
    map.plateaus = {main, east};
    map.ramps = {rampMain, rampEast};
    map.patches = patches;
    map.bases = {mainBase, natural, third, redNatural, centre, south};
    map.starts = {0, 2};
    map.doodads = doodads;
    map.mirrorX = axis;
    map.view = MapView(config, projection);
    cliffRocks(map, config, projection);
    scatter(map, 150, config, projection);
    // The east half's rocks are the west half's, mirrored; a doodad on the
    // axis stays as it is.
    std::vector<Doodad> west, onAxis;
    for (const Doodad& d : map.doodads) if (d.position.x < axis - 0.01) west.push_back(d);
    for (const Doodad& d : map.doodads) if (std::fabs(d.position.x - axis) <= 0.01) onAxis.push_back(d);
    std::vector<Doodad> all = west;
    all.insert(all.end(), onAxis.begin(), onAxis.end());
    for (const Doodad& d : west) {
        Doodad m = d;
        m.position = mirror(d.position);
        m.rotation = -d.rotation;
        m.mirrored = true;
        all.push_back(m);
    }
    map.doodads = all;
    return map;
}

// MARK: - Generated

MapDefinition MapLibrary::generated(const ScreenConfig& config, const CanvasProjection& projection) {
    const double H = 2.4;
    const Author a{projection, H};
    SeededRandom rng(fnv1a64(config.signature()));
    const std::optional<ScreenInfo> largest = config.largest();
    if (!largest) {
        std::abort(); // "no screens"
    }
    const ScreenInfo big = *largest;
    const CanvasRect r = big.rect;
    // Main plateau: the left ~40% of the largest screen, running past its
    // top, left and bottom edges so only its east cliff is visible.
    const double cliffX = r.x + r.width * rng.range(0.34, 0.42);
    std::vector<std::pair<double, double>> pts;
    pts.push_back({r.x - 800, r.y - 800});
    {
        const double x = cliffX + rng.range(-40, 60);
        pts.push_back({x, r.y - 800});
    }
    const int64_t steps = 6;
    for (int64_t i = 1; i < steps; i++) {
        const double y = r.y + r.height * static_cast<double>(i) / static_cast<double>(steps);
        const double x = cliffX + rng.range(-120, 120);
        pts.push_back({x, y});
    }
    {
        const double x = cliffX + rng.range(-60, 60);
        pts.push_back({x, r.maxY() + 800});
        pts.push_back({r.x - 800, r.maxY() + 800});
    }
    const Plateau main = a.poly(pts, 1);
    const double rampY = r.y + r.height * rng.range(0.45, 0.7);
    const Ramp ramp{a.g(cliffX + 280, rampY + 60), a.g(cliffX - 200, rampY - 60, 1), 3.6, 0, 1};
    const BaseSite mainBase = BaseSite::standard(a.g(r.x + (cliffX - r.x) * 0.5, r.midY(), 1), 1, 1.15 * pi);
    std::vector<BaseSite> bases = {mainBase};
    // An expansion on every other screen, and one on the far side of the
    // largest screen when it is wide enough.
    for (const ScreenInfo& s : config.screens) {
        if (!(s != big)) continue;
        const Vec2 c = a.g(s.rect.midX(), s.rect.midY());
        bases.push_back(BaseSite::standard(c, 0, rng.range(0, 2 * pi)));
    }
    // Red starts on the far side of the largest screen.
    {
        const Vec2 c = a.g(r.x + r.width * 0.8, r.midY());
        bases.push_back(BaseSite::standard(c, 0, rng.range(1.5 * pi, 2 * pi)));
    }
    std::vector<GroundPatch> patches;
    const int64_t count = static_cast<int64_t>(max(4.0, config.canvasWidth * config.canvasHeight / 1'400'000));
    for (int64_t i = 0; i < count; i++) {
        const double px = rng.range(0, config.canvasWidth);
        const double py = rng.range(0, config.canvasHeight);
        const Vec2 p = Vec2(px, py);
        const GroundPatch::Kind kind = rng.unit() < 0.7 ? GroundPatch::Kind::grass : GroundPatch::Kind::gravel;
        const Vec2 center = a.g(p.x, p.y);
        patches.push_back(GroundPatch{kind, center, rng.range(4, 9)});
    }
    MapDefinition map;
    map.name = "Generated " + config.sessionID().substr(0, 6);
    map.version = 2;
    map.seed = fnv1a64(config.signature());
    map.bounds = projection.visibleGround(-1, 2 * H, 5);
    map.levelHeight = H;
    map.plateaus = {main};
    map.ramps = {ramp};
    map.patches = patches;
    map.bases = bases;
    map.starts = {0, static_cast<int64_t>(bases.size()) - 1};
    map.doodads = {};
    map.view = MapView(config, projection);
    cliffRocks(map, config, projection);
    scatter(map, 170, config, projection);
    return map;
}

// MARK: - Cliff rocks

/// Boulders along the foot and the lip of every cliff,
/// skipping ramps and anything off screen.
void MapLibrary::cliffRocks(MapDefinition& map, const ScreenConfig& config, const CanvasProjection& projection) {
    cliffRocks(map, [&config, &projection](Vec2 p, double h) {
        const Vec2 c = projection.canvas(Vec3(p.x, h, p.y));
        for (const ScreenInfo& s : config.screens) {
            if (s.rect.contains(c)) return true;
        }
        return false;
    });
}

/// The same, for any test of what counts as in sight: a ground point and
/// its height.
void MapLibrary::cliffRocks(MapDefinition& map, const std::function<bool(Vec2, double)>& onScreen) {
    SeededRandom rng(map.seed + 7);
    const TerrainField field(map);
    auto nearRamp = [&map](Vec2 p) {
        for (const Ramp& r : map.ramps) {
            const Vec2 e = r.high - r.low;
            const double t = min(max(dot(p - r.low, e) / dot(e, e), -0.3), 1.3);
            if (distance(p, r.low + e * t) < r.width / 2 + 2.2) return true;
        }
        return false;
    };
    for (size_t pi_ = 0; pi_ < map.plateaus.size(); pi_++) {
        const std::vector<Vec2> poly = map.plateaus[pi_].polygon;
        for (size_t i = 0; i < poly.size(); i++) {
            const Vec2 a = poly[i], b = poly[(i + 1) % poly.size()];
            const double len = distance(a, b);
            double d = rng.range(0, 1.5);
            while (d < len) {
                const Vec2 p0 = a + (b - a) * (d / len);
                d += rng.range(1.4, 3.2);
                // Find the true (noisy) cliff line near this edge point by
                // walking along the edge normal.
                const Vec2 n = normalize(Vec2(-(b - a).y, (b - a).x));
                Vec2 best = p0;
                double bestSlope = 0.0;
                for (int64_t k = -8; k <= 8; k++) {
                    const Vec2 q = p0 + n * (static_cast<double>(k) * 0.3);
                    const double s = std::fabs(field.height(q + n * 0.3) - field.height(q - n * 0.3));
                    if (s > bestSlope) { bestSlope = s; best = q; }
                }
                if (!(bestSlope > 0.5)) continue;
                // Low side is the side with smaller height.
                const Vec2 lowDir = field.height(best + n) < field.height(best - n) ? n : -n;
                const bool foot = rng.unit() < 0.7;
                const double off = foot ? rng.range(0.7, 1.4) : -rng.range(0.6, 1.1);
                const Vec2 p = best + lowDir * off;
                if (nearRamp(p) || !onScreen(p, field.height(p))) continue;
                bool nearBase = false;
                for (const BaseSite& base : map.bases) if (distance(base.center, p) < 9) { nearBase = true; break; }
                if (nearBase) continue;
                const Doodad::Kind kind = rng.unit() < 0.25 ? Doodad::Kind::boulder : Doodad::Kind::rock;
                const double rotation = rng.range(0, 2 * pi);
                const double scale = kind == Doodad::Kind::rock ? rng.range(0.8, 1.6) : rng.range(0.6, 1.0);
                const uint32_t variant = static_cast<uint32_t>(rng.next());
                map.doodads.push_back(Doodad{kind, p, rotation, scale, variant, std::nullopt});
            }
        }
    }
}

// MARK: - Scatter

/// Scatter small doodads over visible ground, away from bases, ore
/// lines, ramps and cliff faces.
void MapLibrary::scatter(MapDefinition& map, int64_t count, const ScreenConfig& config, const CanvasProjection& projection) {
    scatter(map, count, [&config, &projection](SeededRandom& rng, const TerrainField& field) -> std::optional<Vec2> {
        // Pick a canvas point on a real screen, then find its ground.
        const ScreenInfo* screen = rng.randomElement(config.screens);
        if (!screen) return std::nullopt;
        const double cx = rng.range(screen->rect.x, screen->rect.maxX());
        const double cy = rng.range(screen->rect.y, screen->rect.maxY());
        const Vec2 c = Vec2(cx, cy);
        const Vec2 p = projection.ground(c);
        return projection.ground(c, field.height(p));
    });
}

/// The same, drawing each candidate spot from `sample` (nil: no more).
void MapLibrary::scatter(MapDefinition& map, int64_t count,
                         const std::function<std::optional<Vec2>(SeededRandom&, const TerrainField&)>& sample) {
    SeededRandom rng(map.seed + 99);
    const TerrainField field(map);
    int64_t placed = 0, tries = 0;
    while (placed < count && tries < count * 40) {
        tries += 1;
        const std::optional<Vec2> sampled = sample(rng, field);
        if (!sampled) return;
        const Vec2 p = *sampled;
        bool skip = false;
        for (const BaseSite& b : map.bases) if (distance(b.center, p) < 10.5) { skip = true; break; }
        if (skip) continue;
        for (const Ramp& r : map.ramps) if (distance((r.low + r.high) / 2, p) < 5) { skip = true; break; }
        if (skip) continue;
        for (const Doodad& d : map.doodads) if (distance(d.position, p) < 1.6) { skip = true; break; }
        if (skip) continue;
        // Keep off cliff faces.
        const double e = 0.6;
        const double slope = std::fabs(field.height(p + Vec2(e, 0)) - field.height(p - Vec2(e, 0)))
            + std::fabs(field.height(p + Vec2(0, e)) - field.height(p - Vec2(0, e)));
        if (slope > 0.7) continue;
        const bool onGrass = field.materials(p).x > 0.5;
        const double roll = rng.unit();
        using D = Doodad::Kind;
        D kind;
        if (onGrass) kind = roll < 0.55 ? D::plant : roll < 0.85 ? D::bush : D::rock;
        else kind = roll < 0.45 ? D::rock : roll < 0.62 ? D::boulder : roll < 0.8 ? D::bush : roll < 0.9 ? D::debris : D::deadTree;
        const double scale = kind == D::rock ? rng.range(0.35, 0.8) : rng.range(0.6, 1.1);
        const double rotation = rng.range(0, 2 * pi);
        const uint32_t variant = static_cast<uint32_t>(rng.next());
        map.doodads.push_back(Doodad{kind, p, rotation, scale, variant, std::nullopt});
        placed += 1;
    }
}

} // namespace ac
