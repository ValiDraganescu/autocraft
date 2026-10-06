// Port of Sources/GameCore/NavGrid.swift.
#include "NavGrid.h"

#include "Rules.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <utility>

namespace ac {

namespace {

/// Exact text of a double, for the cache key.
std::string hexText(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%a", v);
    return buf;
}

std::string describe(const CanvasRect& r) {
    return hexText(r.x) + "," + hexText(r.y) + "," + hexText(r.width) + "," + hexText(r.height);
}

/// Swift's `String(describing: map.view)`: anything that tells two views apart.
std::string describe(const std::optional<MapView>& view) {
    if (!view) return "nil";
    const CanvasProjection& p = view->projection;
    std::string out = hexText(p.canvasWidth) + "," + hexText(p.canvasHeight) + "," + hexText(p.pointsPerCell) + "," +
                      hexText(p.pitchDegrees) + "," + hexText(p.fovDegrees) + "|";
    for (const CanvasRect& r : view->screens) out += describe(r) + ";";
    out += "|";
    for (const CanvasRect& r : view->covered) out += describe(r) + ";";
    return out;
}

} // namespace

NavGrid::NavGrid(const MapDefinition& map, const TerrainField& field) {
    const GroundRect& b = map.bounds;
    const std::string key = map.name + "|" + std::to_string(map.version) + "|" + std::to_string(map.seed) + "|" +
                            hexText(b.minX) + "," + hexText(b.minZ) + "," + hexText(b.maxX) + "," + hexText(b.maxZ) +
                            "|" + hexText(map.mirrorX ? *map.mirrorX : std::nan("")) + "|" + describe(map.view) +
                            (map.squareSymmetric == true ? "|square" : "");
    // On a mirrored map the axis falls on a cell edge, so every cell has
    // a mirror-image twin.
    const double x0 = map.mirrorX ? *map.mirrorX - std::ceil((*map.mirrorX - b.minX) / cell) * cell : b.minX;
    origin = Vec2(x0, b.minZ);
    mirrorX = map.mirrorX;
    width = static_cast<int64_t>((b.maxX - x0) / cell);
    height = static_cast<int64_t>((b.maxZ - b.minZ) / cell);
    auto cached = cache().get(key);
    auto c = cache().get(key + "|cliffs");
    if (cached && c) {
        ground = std::move(*cached);
        cliffs = std::move(*c);
    } else {
        Layers built = buildGround(map, field, origin, width, height);
        ground = std::move(built.ground);
        cliffs = std::move(built.cliffs);
        cache().set(key, ground);
        cache().set(key + "|cliffs", cliffs);
    }
    for (const Doodad& d : map.doodads) {
        for (const Circle& k : circles(d)) rocks.push_back(k);
    }
    mapRocks = static_cast<int64_t>(rocks.size());
    mapWells = 0;
    for (const BaseSite& s : map.bases) mapWells += static_cast<int64_t>(s.wells.size());
    dynamic = std::vector<uint8_t>(static_cast<size_t>(width * height), 0);
    indexPassable();
    indexRocks();
    labelRegions();
}

// MARK: - Cells

/// The centre of the free cell `p` is in, clear of rocks (nil: that
/// cell is blocked). Paths from a cell centre never clip a corner.
std::optional<Vec2> NavGrid::freeCentre(Vec2 p) const {
    const auto i = index(p);
    if (!i || !free(*i) || inRock(centre(*i))) return std::nullopt;
    return centre(*i);
}

/// A unit can stand at `p` without touching anything.
bool NavGrid::walkable(Vec2 p) const {
    const auto i = index(p);
    return i ? free(*i) : false;
}

/// Open ground at `p`, cliffs and the map edge excluded, whatever
/// buildings and fields stand on it (a unit stepping by hand checks those
/// itself, closer than the grid's cells).
bool NavGrid::standable(Vec2 p) const {
    const auto i = index(p);
    return i ? ground[static_cast<size_t>(*i)] != 0 : false;
}

/// `p` is inside a doodad's rock (cells are coarser than rocks' edges).
bool NavGrid::inRock(Vec2 p) const {
    // Only the rocks whose box reaches `p`'s bucket can hold it; the test
    // per rock is the same, so the answer is too.
    if (rocksIndexed == rocks.size()) {
        const double bx = std::floor((p.x - origin.x) / rockBucket), bz = std::floor((p.y - origin.y) / rockBucket);
        if (bx >= 0 && bz >= 0 && bx < static_cast<double>(rockColumns) && bz < static_cast<double>(rockRows)) {
            const size_t b = static_cast<size_t>(static_cast<int64_t>(bz) * rockColumns + static_cast<int64_t>(bx));
            const Circle* rs = rocks.data();
            for (int32_t k = rockStart[b], end = rockStart[b + 1]; k < end; k++) {
                const Circle& r = rs[rockList[static_cast<size_t>(k)]];
                if (distance(r.first, p) < r.second) return true;
            }
            return false;
        }
    }
    for (const Circle& r : rocks) {
        if (distance(r.first, p) < r.second) return true;
    }
    return false;
}

/// Rebuild the rock buckets from `rocks`.
void NavGrid::indexRocks() {
    rockColumns = static_cast<int64_t>(std::ceil(static_cast<double>(width) * cell / rockBucket));
    rockRows = static_cast<int64_t>(std::ceil(static_cast<double>(height) * cell / rockBucket));
    const size_t buckets = static_cast<size_t>(rockColumns * rockRows);
    // A rock's box, a little grown so rounding at a bucket's edge never
    // leaves it out.
    const double pad = 0.01;
    auto span = [&](const Circle& r, int64_t& x0, int64_t& x1, int64_t& z0, int64_t& z1) {
        const double g = r.second + pad;
        x0 = std::max<int64_t>(0, static_cast<int64_t>(std::floor((r.first.x - g - origin.x) / rockBucket)));
        x1 = std::min<int64_t>(rockColumns - 1, static_cast<int64_t>(std::floor((r.first.x + g - origin.x) / rockBucket)));
        z0 = std::max<int64_t>(0, static_cast<int64_t>(std::floor((r.first.y - g - origin.y) / rockBucket)));
        z1 = std::min<int64_t>(rockRows - 1, static_cast<int64_t>(std::floor((r.first.y + g - origin.y) / rockBucket)));
    };
    std::vector<int32_t> counts(buckets + 1, 0);
    for (const Circle& r : rocks) {
        int64_t x0, x1, z0, z1;
        span(r, x0, x1, z0, z1);
        for (int64_t z = z0; z <= z1; z++)
            for (int64_t x = x0; x <= x1; x++) counts[static_cast<size_t>(z * rockColumns + x)]++;
    }
    rockStart.assign(buckets + 1, 0);
    for (size_t b = 0; b < buckets; b++) rockStart[b + 1] = rockStart[b] + counts[b];
    rockList.assign(static_cast<size_t>(rockStart[buckets]), 0);
    std::vector<int32_t> fill(rockStart.begin(), rockStart.end() - 1);
    for (size_t k = 0; k < rocks.size(); k++) {
        int64_t x0, x1, z0, z1;
        span(rocks[k], x0, x1, z0, z1);
        for (int64_t z = z0; z <= z1; z++)
            for (int64_t x = x0; x <= x1; x++) rockList[static_cast<size_t>(fill[static_cast<size_t>(z * rockColumns + x)]++)] = static_cast<int32_t>(k);
    }
    rocksIndexed = rocks.size();
}

/// Rebuild `passable` from `ground`, `cliffs` and `dynamic`.
void NavGrid::indexPassable() {
    const size_t n = static_cast<size_t>(width * height);
    passable.assign(n, 0);
    for (size_t i = 0; i < n; i++) {
        if (dynamic[i]) continue;
        passable[i] = ground[i] ? 1 : (cliffs[i] ? 2 : 0);
    }
}

/// `p` is on a cliff face a Comet can jump (and no building covers it).
bool NavGrid::cliff(Vec2 p) const {
    const auto i = index(p);
    return i ? cliffs[static_cast<size_t>(*i)] && !dynamic[static_cast<size_t>(*i)] : false;
}

// MARK: - Obstacles

/// Solid circles of a doodad (offset in its own frame, radius), before
/// its rotation and scale. Plants, bushes and flat debris are walkable.
std::vector<NavGrid::Circle> NavGrid::footprint(Doodad::Kind k) {
    switch (k) {
    case Doodad::Kind::rock: return {{Vec2(0, 0), 0.55}};
    case Doodad::Kind::boulder: return {{Vec2(0, 0), 1.15}, {Vec2(1.0, 0.6), 0.45}};
    case Doodad::Kind::rockSpire: return {{Vec2(0, 0), 0.8}, {Vec2(0.9, 0.4), 0.55}, {Vec2(-0.7, 0.7), 0.45}};
    case Doodad::Kind::crate: return {{Vec2(0, 0), 0.42}};
    case Doodad::Kind::crateStack: return {{Vec2(0, 0), 0.42}, {Vec2(0.78, 0.1), 0.42}};
    case Doodad::Kind::deadTree: return {{Vec2(0, 0), 0.22}};
    case Doodad::Kind::tower: return {{Vec2(0, 0), 1.3}};
    case Doodad::Kind::plant: case Doodad::Kind::bush: case Doodad::Kind::debris: return {};
    }
    return {};
}

/// World circles a doodad blocks. The scene turns it by `rotation` about
/// +Y: local (x, z) goes to (x·cos + z·sin, −x·sin + z·cos).
std::vector<NavGrid::Circle> NavGrid::circles(const Doodad& d) {
    const double c = std::cos(d.rotation), s = std::sin(d.rotation), f = d.mirrored == true ? -1.0 : 1.0;
    std::vector<Circle> out;
    for (const Circle& fp : footprint(d.kind)) {
        const Vec2 o = fp.first;
        const double r = fp.second;
        out.push_back({d.position + Vec2(f * o.x * c + o.y * s, -f * o.x * s + o.y * c) * d.scale, r * d.scale});
    }
    return out;
}

/// An ore deposit is 2x1, its long axis turned by `angle` like the scene
/// turns its model.
bool NavGrid::insidePatch(Vec2 p, const OreDeposit& m, double grow) {
    const Vec2 d = p - m.position;
    // Far off the patch: no need to turn into its frame (C++ only, the same
    // answer). Inside, |d| = |(lx, lz)| <= sqrt(a² + b²) <= a + b.
    const double far = (1 + grow) + (0.5 + grow) + 1e-9;
    if (std::fabs(d.x) > far || std::fabs(d.y) > far) return false;
    const double c = std::cos(m.angle), s = std::sin(m.angle);
    // Into the patch's frame (inverse of the scene's rotation).
    const double lx = d.x * c - d.y * s, lz = d.x * s + d.y * c;
    return std::fabs(lx) <= 1 + grow && std::fabs(lz) <= 0.5 + grow;
}

bool NavGrid::insideStructure(Vec2 p, const Structure& st, double grow) {
    const double r = Rules::radius(st.kind) + grow;
    return std::fabs(p.x - st.position.x) <= r && std::fabs(p.y - st.position.y) <= r;
}

/// Anything solid exactly at `p` (not grown): a building or a live
/// ore deposit, or a doodad put down in the playground. Units found
/// here are pushed out.
bool NavGrid::solid(Vec2 p, const GameState& s) {
    for (const Structure& st : s.structures) {
        if (insideStructure(p, st, -0.05)) return true;
    }
    for (const OreDeposit& m : s.patches) {
        if (m.remaining > 0 && insidePatch(p, m, -0.05)) return true;
    }
    if (s.doodads) {
        for (const Doodad& d : *s.doodads) {
            for (const Circle& k : circles(d)) {
                if (distance(k.first, p) < k.second) return true;
            }
        }
    }
    return false;
}

NavGrid::Layers NavGrid::buildGround(const MapDefinition& map, const TerrainField& field, Vec2 origin_, int64_t width_,
                                     int64_t height_) {
    std::vector<uint8_t> out(static_cast<size_t>(width_ * height_), 0);
    std::vector<uint8_t> cliff(static_cast<size_t>(width_ * height_), 0);
    const double c = clearance, e = cell / 2;
    std::vector<Circle> doodads;
    for (const Doodad& d : map.doodads) {
        for (const Circle& k : circles(d)) doodads.push_back(k);
    }
    std::vector<Vec2> wells;
    for (const BaseSite& b : map.bases) wells.insert(wells.end(), b.wells.begin(), b.wells.end());
    uint8_t* ptr = out.data();
    uint8_t* cptr = cliff.data();
    const std::function<double(Vec2)> heightAt = [&field](Vec2 q) { return field.height(q); };
    auto row = [&](int64_t z) {
        for (int64_t x = 0; x < width_; x++) {
            const Vec2 p = origin_ + Vec2(static_cast<double>(x) + 0.5, static_cast<double>(z) + 0.5) * cell;
            // Keep off the map edge.
            if (!(x > 2 && z > 2 && x < width_ - 3 && z < height_ - 3)) continue;
            // Cliff: steep somewhere within the unit's reach.
            bool steep = false;
            for (Vec2 q : {p, p + Vec2(c, 0), p - Vec2(c, 0), p + Vec2(0, c), p - Vec2(0, c)}) {
                if (steep) continue;
                const double dx = (field.height(q + Vec2(e, 0)) - field.height(q - Vec2(e, 0))) / cell;
                const double dz = (field.height(q + Vec2(0, e)) - field.height(q - Vec2(0, e))) / cell;
                steep = std::sqrt(dx * dx + dz * dz) > maxSlope;
            }
            bool blocked = false;
            for (const Circle& k : doodads) {
                if (distance(k.first, p) < k.second + c) { blocked = true; break; }
            }
            if (blocked) continue;
            for (Vec2 w : wells) {
                if (distance(w, p) < 1.5 + c) { blocked = true; break; }
            }
            if (blocked) continue;
            // Out of sight is out of play.
            if (!map.inPlay(p, heightAt)) continue;
            if (steep) cptr[z * width_ + x] = 1; else ptr[z * width_ + x] = 1;
        }
    };
    // Rows in parallel (Swift's `DispatchQueue.concurrentPerform`): each
    // writes only its own cells.
    const int64_t threads = std::max<int64_t>(1, std::min<int64_t>(static_cast<int64_t>(std::thread::hardware_concurrency()), height_));
    std::vector<std::thread> pool;
    for (int64_t t = 1; t < threads; t++) {
        pool.emplace_back([&row, t, threads, height_] {
            for (int64_t z = t; z < height_; z += threads) row(z);
        });
    }
    for (int64_t z = 0; z < height_; z += threads) row(z);
    for (std::thread& th : pool) th.join();
    return Layers{std::move(out), std::move(cliff)};
}

/// Rebuild the building and ore layer when either changed (or a
/// doodad or well was put down in the playground). True when it did
/// (paths through the old layout are stale).
bool NavGrid::setDynamic(const GameState& s) {
    static const std::vector<Doodad> none;
    const std::vector<Doodad>& placed = s.doodads ? *s.doodads : none;
    std::vector<Vec2> wells;
    if (s.wells) {
        for (size_t i = static_cast<size_t>(std::min<int64_t>(mapWells, static_cast<int64_t>(s.wells->size())));
             i < s.wells->size(); i++) {
            wells.push_back((*s.wells)[i].position);
        }
    }
    std::vector<int64_t> key;
    for (const Structure& st : s.structures) key.push_back(st.id);
    key.push_back(-1);
    for (size_t i = 0; i < s.patches.size(); i++) {
        if (s.patches[i].remaining > 0) key.push_back(static_cast<int64_t>(i));
    }
    key.push_back(-2);
    key.push_back(static_cast<int64_t>(placed.size()));
    key.push_back(static_cast<int64_t>(wells.size()));
    if (key == dynamicKey) return false;
    dynamicKey = key;
    std::vector<uint8_t> before;
    if (fastSearch) before = std::move(dynamic);
    dynamic = std::vector<uint8_t>(static_cast<size_t>(width * height), 0);
    const double c = clearance;
    // Placed doodads and wells, grown like the map's in `buildGround`.
    std::vector<Circle> grown;
    for (const Doodad& d : placed) {
        for (const Circle& k : circles(d)) grown.push_back(k);
    }
    for (Vec2 w : wells) grown.push_back({w, 1.5});
    rocks.resize(static_cast<size_t>(std::min<int64_t>(mapRocks, static_cast<int64_t>(rocks.size()))));
    for (const Doodad& d : placed) {
        for (const Circle& k : circles(d)) rocks.push_back(k);
    }
    for (const Circle& k : grown) {
        const Vec2 o = k.first;
        const double g = k.second + c;
        mark(o - Vec2(g, g), o + Vec2(g, g), [o, g](Vec2 q) { return distance(q, o) < g; });
    }
    for (const Structure& st : s.structures) {
        const double r = Rules::radius(st.kind) + c;
        mark(st.position - Vec2(r, r), st.position + Vec2(r, r), [&st, c](Vec2 q) { return insideStructure(q, st, c); });
    }
    for (const OreDeposit& m : s.patches) {
        if (!(m.remaining > 0)) continue;
        const double r = 1 + c;
        mark(m.position - Vec2(r, r), m.position + Vec2(r, r), [&m, c](Vec2 q) { return insidePatch(q, m, c); });
    }
    if (fastSearch) {
        newlyBlocked.clear();
        changedBox = {width, height, -1, -1};
        const size_t n = dynamic.size();
        for (size_t i = 0; i < n; i++) {
            if (!dynamic[i] || (i < before.size() && before[i])) continue;
            newlyBlocked.push_back(static_cast<int64_t>(i));
            const int64_t x = static_cast<int64_t>(i) % width, z = static_cast<int64_t>(i) / width;
            changedBox = {std::min(changedBox[0], x - 1), std::min(changedBox[1], z - 1), std::max(changedBox[2], x + 1),
                          std::max(changedBox[3], z + 1)};
        }
        changedMask.assign(n, 0);
        for (int64_t i : newlyBlocked) {
            const int64_t x = i % width, z = i / width;
            for (int64_t dz = -1; dz <= 1; dz++) {
                for (int64_t dx = -1; dx <= 1; dx++) {
                    if (x + dx >= 0 && x + dx < width && z + dz >= 0 && z + dz < height) changedMask[static_cast<size_t>(i + dz * width + dx)] = 1;
                }
            }
        }
    }
    indexPassable();
    indexRocks();
    labelRegions();
    return true;
}

/// Flood-fill the free cells into regions. Four-way, which matches A*'s
/// reach (it steps diagonally only past two open sides).
void NavGrid::labelRegions() {
    const int64_t n = width * height;
    std::vector<int32_t> out(static_cast<size_t>(n), -1);
    int32_t next = 0;
    std::vector<int64_t> stack;
    std::vector<int64_t> cells;
    std::vector<uint8_t> cliff;
    for (int64_t s = 0; s < n; s++) {
        if (!(out[static_cast<size_t>(s)] < 0 && free(s))) continue;
        out[static_cast<size_t>(s)] = next;
        stack.push_back(s);
        int64_t count = 0;
        bool jumpable = false;
        while (!stack.empty()) {
            const int64_t i = stack.back();
            stack.pop_back();
            count += 1;
            const int64_t x = i % width;
            for (int64_t j : {i - 1, i + 1, i - width, i + width}) {
                if (!(j >= 0 && j < n && std::llabs(j % width - x) <= 1)) continue;
                if (cliffs[static_cast<size_t>(j)] && !dynamic[static_cast<size_t>(j)]) jumpable = true;
                if (!(out[static_cast<size_t>(j)] < 0 && free(j))) continue;
                out[static_cast<size_t>(j)] = next;
                stack.push_back(j);
            }
        }
        cells.push_back(count);
        cliff.push_back(jumpable ? 1 : 0);
        next += 1;
    }
    regions = std::move(out);
    regionCells = cells;
    regionCliff = cliff;
    // `cells.indices.max { cells[$0] < cells[$1] }`: the first of the largest.
    mainRegion = -1;
    for (size_t i = 0; i < cells.size(); i++) {
        if (mainRegion < 0 || cells[static_cast<size_t>(mainRegion)] < cells[i]) mainRegion = static_cast<int32_t>(i);
    }
    // A Comet's: free cells and the cliff faces between them.
    std::vector<int32_t> jump(static_cast<size_t>(n), -1);
    next = 0;
    auto open = [this](int64_t k) { return free(k) || (cliffs[static_cast<size_t>(k)] && !dynamic[static_cast<size_t>(k)]); };
    for (int64_t s = 0; s < n; s++) {
        if (!(jump[static_cast<size_t>(s)] < 0 && open(s))) continue;
        jump[static_cast<size_t>(s)] = next;
        stack.push_back(s);
        while (!stack.empty()) {
            const int64_t i = stack.back();
            stack.pop_back();
            const int64_t x = i % width;
            for (int64_t j : {i - 1, i + 1, i - width, i + width}) {
                if (!(j >= 0 && j < n && std::llabs(j % width - x) <= 1 && jump[static_cast<size_t>(j)] < 0 && open(j))) continue;
                jump[static_cast<size_t>(j)] = next;
                stack.push_back(j);
            }
        }
        next += 1;
    }
    jumpRegions = std::move(jump);
}

/// Some cell within `reach` of `target` that a search from cell `start`
/// can step on is in its region. The search moves as the regions
/// connect (a diagonal step needs both cells beside it open), so
/// without one it would fail, after flooding the whole region.
bool NavGrid::reachable(int64_t start, Vec2 target, double reach, bool jumps) const {
    const std::vector<int32_t>& labels = jumps ? jumpRegions : regions;
    if (!(static_cast<int64_t>(labels.size()) == width * height)) return true;
    const int32_t want = labels[static_cast<size_t>(start)];
    if (!(want >= 0)) return true;
    const int64_t x0 = std::max<int64_t>(0, static_cast<int64_t>(std::floor((target.x - reach - origin.x) / cell)));
    const int64_t x1 = std::min<int64_t>(width - 1, static_cast<int64_t>(std::floor((target.x + reach - origin.x) / cell)));
    const int64_t z0 = std::max<int64_t>(0, static_cast<int64_t>(std::floor((target.y - reach - origin.y) / cell)));
    const int64_t z1 = std::min<int64_t>(height - 1, static_cast<int64_t>(std::floor((target.y + reach - origin.y) / cell)));
    if (!(x0 <= x1 && z0 <= z1)) return false;
    for (int64_t z = z0; z <= z1; z++) {
        for (int64_t x = x0; x <= x1; x++) {
            const int64_t i = z * width + x;
            if (labels[static_cast<size_t>(i)] == want && distance(centre(i), target) <= reach) return true;
        }
    }
    return false;
}

/// A ground unit at `p` is boxed in: the ground it walks in (see
/// `region`) is cut off from the open map and smaller than `pocket`
/// square cells, so a gap between buildings, not a walled-off base.
/// `jumps` (a Comet): not when that ground borders a cliff face.
bool NavGrid::boxedIn(Vec2 p, double pocket, bool jumps) const {
    if (!(mainRegion >= 0)) return false;
    const auto r = region(p);
    if (!r || !(*r >= 0)) return true;
    if (!(*r != mainRegion)) return false;
    if (jumps && regionCliff[static_cast<size_t>(*r)]) return false;
    return static_cast<double>(regionCells[static_cast<size_t>(*r)]) * cell * cell < pocket;
}

bool NavGrid::cuts(const std::vector<Box>& boxes, const std::vector<Box>& pending, double window) const {
    if (boxes.empty()) return false;
    const double c = clearance;
    double minX = boxes[0].first.x, maxX = minX, minY = boxes[0].first.y, maxY = minY, reach = 0;
    for (const auto& [o, h] : boxes) {
        minX = std::min(minX, o.x - h); maxX = std::max(maxX, o.x + h);
        minY = std::min(minY, o.y - h); maxY = std::max(maxY, o.y + h);
    }
    reach = c + window;
    const int64_t x0 = std::max<int64_t>(0, static_cast<int64_t>(std::floor((minX - reach - origin.x) / cell)));
    const int64_t x1 = std::min<int64_t>(width - 1, static_cast<int64_t>(std::floor((maxX + reach - origin.x) / cell)));
    const int64_t z0 = std::max<int64_t>(0, static_cast<int64_t>(std::floor((minY - reach - origin.y) / cell)));
    const int64_t z1 = std::min<int64_t>(height - 1, static_cast<int64_t>(std::floor((maxY + reach - origin.y) / cell)));
    if (!(x0 <= x1 && z0 <= z1)) return false;
    const int64_t w = x1 - x0 + 1, h = z1 - z0 + 1;
    auto under = [&](Vec2 q, const std::vector<Box>& bs, double grow) {
        for (const auto& [o, half] : bs) {
            if (std::fabs(q.x - o.x) <= half + grow && std::fabs(q.y - o.y) <= half + grow) return true;
        }
        return false;
    };
    // Open before and after; ring: open after, right beside the new boxes.
    std::vector<uint8_t> before(static_cast<size_t>(w * h)), after(before.size()), ring(before.size());
    for (int64_t z = 0; z < h; z++) {
        for (int64_t x = 0; x < w; x++) {
            const int64_t i = (z0 + z) * width + (x0 + x);
            const size_t k = static_cast<size_t>(z * w + x);
            const Vec2 q = centre(i);
            before[k] = free(i) && !under(q, pending, c);
            after[k] = before[k] && !under(q, boxes, c);
            ring[k] = after[k] && under(q, boxes, c + 1.0);
        }
    }
    auto label = [&](const std::vector<uint8_t>& open) {
        std::vector<int32_t> l(open.size(), -1);
        std::vector<int64_t> stack;
        int32_t next = 0;
        for (size_t s0 = 0; s0 < open.size(); s0++) {
            if (!open[s0] || l[s0] >= 0) continue;
            l[s0] = next;
            stack.assign(1, static_cast<int64_t>(s0));
            while (!stack.empty()) {
                const int64_t k = stack.back();
                stack.pop_back();
                const int64_t x = k % w, z = k / w;
                const int64_t ns[4][2] = {{x + 1, z}, {x - 1, z}, {x, z + 1}, {x, z - 1}};
                for (const auto& n : ns) {
                    if (!(n[0] >= 0 && n[0] < w && n[1] >= 0 && n[1] < h)) continue;
                    const size_t m = static_cast<size_t>(n[1] * w + n[0]);
                    if (open[m] && l[m] < 0) { l[m] = next; stack.push_back(static_cast<int64_t>(m)); }
                }
            }
            next++;
        }
        return l;
    };
    const auto lb = label(before), la = label(after);
    std::map<int32_t, int32_t> joined;
    for (size_t k = 0; k < ring.size(); k++) {
        if (!ring[k]) continue;
        const auto [it, fresh] = joined.emplace(lb[k], la[k]);
        if (!fresh && it->second != la[k]) return true;
    }
    return false;
}

/// The region a unit at `p` walks in (that of the nearest free spot).
std::optional<int32_t> NavGrid::region(Vec2 p) const {
    const auto q = nearestFree(p, 4);
    if (!q) return std::nullopt;
    const auto i = index(*q);
    if (!i) return std::nullopt;
    return regions[static_cast<size_t>(*i)];
}

/// The free spot nearest `target` that a unit at `from` can walk to.
std::optional<Vec2> NavGrid::closestReachable(Vec2 target, Vec2 from) const {
    const auto want = region(from);
    const auto start = index(target);
    if (!want || !start) return std::nullopt;
    const bool flip = east(target);
    if (regions[static_cast<size_t>(*start)] == *want) return target;
    // Seen cells marked with a search stamp in pooled scratch (C++ only;
    // Swift's `Set` visits the same cells in the same order).
    std::unique_ptr<Scratch> scratchArrays = scratch().take();
    Scratch& sc = *scratchArrays;
    const uint32_t stamp = sc.begin(width * height);
    Scratch::Cell* cs = sc.cells.data();
    struct Give {
        std::unique_ptr<Scratch>& s;
        ~Give() { scratch().give(std::move(s)); }
    } give{scratchArrays};
    cs[*start].mark = stamp;
    std::vector<int64_t> frontier{*start};
    for (int step = 0; step < 40; step++) {
        std::vector<int64_t> next;
        std::optional<std::pair<int64_t, double>> best;
        for (int64_t i : frontier) {
            for (const auto& n : neighbours(i, flip)) {
                if (cs[n.first].mark == stamp) continue;
                cs[n.first].mark = stamp;
                if (regions[static_cast<size_t>(n.first)] == *want) {
                    const double d = distance(centre(n.first), target);
                    if (!best || d < best->second) best = std::make_pair(n.first, d);
                }
                next.push_back(n.first);
            }
        }
        if (best) return centre(best->first);
        frontier = std::move(next);
    }
    return std::nullopt;
}

template <class Inside> void NavGrid::mark(Vec2 lo, Vec2 hi, Inside inside) {
    const int64_t x0 = std::max<int64_t>(0, static_cast<int64_t>(std::floor((lo.x - origin.x) / cell))),
                  x1 = std::min<int64_t>(width - 1, static_cast<int64_t>(std::floor((hi.x - origin.x) / cell)));
    const int64_t z0 = std::max<int64_t>(0, static_cast<int64_t>(std::floor((lo.y - origin.y) / cell))),
                  z1 = std::min<int64_t>(height - 1, static_cast<int64_t>(std::floor((hi.y - origin.y) / cell)));
    if (!(x0 <= x1 && z0 <= z1)) return;
    for (int64_t z = z0; z <= z1; z++) {
        for (int64_t x = x0; x <= x1; x++) {
            if (inside(centre(z * width + x))) dynamic[static_cast<size_t>(z * width + x)] = 1;
        }
    }
}

// MARK: - Paths

/// The nearest free spot to `p`, searching outward up to `limit` cells.
std::optional<Vec2> NavGrid::nearestFree(Vec2 p, double limit) const {
    const auto start = index(p);
    if (!start) return std::nullopt;
    if (free(*start)) return p;
    std::unordered_set<int64_t> seen{*start};
    std::vector<int64_t> frontier{*start};
    const int64_t steps_ = static_cast<int64_t>(limit / cell);
    const bool flip = east(p);
    for (int64_t step = 0; step < steps_; step++) {
        std::vector<int64_t> next;
        std::optional<std::pair<int64_t, double>> best;
        for (int64_t i : frontier) {
            for (const auto& n : neighbours(i, flip)) {
                if (seen.count(n.first)) continue;
                seen.insert(n.first);
                if (free(n.first)) {
                    const double d = distance(centre(n.first), p);
                    if (!best || d < best->second) best = std::make_pair(n.first, d);
                }
                next.push_back(n.first);
            }
        }
        if (best) return centre(best->first);
        frontier = std::move(next);
    }
    return std::nullopt;
}

/// 8 neighbours with their step lengths; diagonals only when both sides
/// are open (no cutting corners).
NavGrid::Neighbours NavGrid::neighbours(int64_t i, bool flip) const {
    const int64_t x = i % width, z = i / width;
    Neighbours out;
    for (const Step& s : flip ? stepsEast : steps) {
        const int64_t dx = s.first, dz = s.second;
        const int64_t nx = x + dx, nz = z + dz;
        if (!(nx >= 0 && nz >= 0 && nx < width && nz < height)) continue;
        if (dx != 0 && dz != 0) {
            out.items[out.count++] = {nz * width + nx, 1.41421356};
        } else {
            out.items[out.count++] = {nz * width + nx, 1};
        }
    }
    return out;
}

/// Waypoints from `from` to a free spot within `stopAt` of `target`,
/// around every obstacle, straightened; nil when there is no way. The
/// last waypoint is where the unit stops short of the target.
/// With `jumps` (a Comet) it may cross cliff faces, at a little extra
/// cost so it still takes the plain way when that is as short.
std::optional<std::vector<Vec2>> NavGrid::path(Vec2 from, Vec2 target, double stopAt, bool jumps) const {
    PathBegin b = pathBegin(from, target, stopAt, jumps);
    if (b.done) return std::move(b.result);
    return pathSearch(b, from, target, stopAt, jumps);
}

NavGrid::PathBegin NavGrid::pathBegin(Vec2 from, Vec2 target, double stopAt, bool jumps) const {
    PathBegin b;
    auto answer = [&b](std::optional<std::vector<Vec2>> r) {
        b.done = true;
        b.result = std::move(r);
        return std::move(b);
    };
    const auto startIndex = index(from);
    if (!startIndex) return answer(std::nullopt);
    b.start = *startIndex;
    // A jumper on a cliff face it may cross starts right there. Stepping it
    // out to the nearest free ground first sent a Comet mid-jump back the
    // way it came at every re-path, and it hopped up and down at the edge.
    const bool onFace = jumps && cliffs[static_cast<size_t>(b.start)] && !dynamic[static_cast<size_t>(b.start)];
    if (!free(b.start) && !onFace) {
        // Standing in something (a building went up around it): step out first.
        const auto out = nearestFree(from, 4);
        if (!out) return answer(std::nullopt);
        const auto i = index(*out);
        if (!i) return answer(std::nullopt);
        b.prefix = {*out};
        b.start = *i;
    }
    const double reach = stopAt + cell * 0.75;
    auto goalTest = [this, target, reach](int64_t i) { return distance(centre(i), target) <= reach; };
    if (goalTest(b.start)) return answer(b.prefix);
    if (!reachable(b.start, target, reach, jumps)) return answer(std::nullopt);
    return b;
}

std::optional<std::vector<Vec2>> NavGrid::pathSearch(const PathBegin& b, Vec2 from, Vec2 target, double stopAt, bool jumps) const {
    const int64_t start = b.start;
    const std::vector<Vec2>& prefix = b.prefix;
    const double reach = stopAt + cell * 0.75;

    // On raw buffers, with nothing captured: a closure over `self`
    // copies the grid's arrays in and out (retain, release) on every
    // call, which was most of a search's time. The working arrays are
    // kept between searches (`Scratch`).
    const int64_t n = width * height, w = width, hgt = height;
    std::unique_ptr<Scratch> scratchArrays = scratch().take();
    Scratch& sc = *scratchArrays;
    const uint32_t stamp = sc.begin(n);
    const double ox = origin.x, oz = origin.y;
    // From cell (x, z)'s centre to the target, as `distance(centre(i), target)`.
    auto distanceAt = [ox, oz, target](int64_t x, int64_t z) {
        return distance(Vec2(ox, oz) + Vec2(static_cast<double>(x) + 0.5, static_cast<double>(z) + 0.5) * cell, target);
    };
    std::vector<int64_t> cells;
    // `passable`: 1 free, 2 a cliff face a Comet may cross.
    const uint8_t* pass = passable.data();
    const uint8_t openMask = jumps ? 3 : 1;
    Scratch::Cell* cs = sc.cells.data();
    // `mark`: this search's stamp once a cell has a cost, one more once closed.
    const uint32_t closedMark = stamp + 1;
    const float diagonal = 1.0f * 1.41421356f, diagonalCliff = 1.3f * 1.41421356f;
    // One search per scan order, each with its 8 steps unrolled.
    // Fast: weighted (`fastWeight`) octile heuristic (the target's cell coordinates, and how much of
    // the distance the goal's disk takes off: octile / euclid is at most
    // 1.0824) and the 4-ary heap.
    const float tcx = static_cast<float>((target.x - ox) / cell), tcz = static_cast<float>((target.y - oz) / cell);
    const float discount = static_cast<float>(1.0825 * reach / cell);
    auto search = [&](auto eastward, auto fast) -> int64_t {
        constexpr const std::array<Step, 8>& dirs = decltype(eastward)::value ? stepsEast : steps;
        auto& heap = [&]() -> auto& { if constexpr (decltype(fast)::value) return sc.heap4; else return sc.heap; }();
        auto h = [&](int64_t x, int64_t z) {
            if constexpr (decltype(fast)::value) {
                const float fx = std::fabs(static_cast<float>(x) + 0.5f - tcx), fz = std::fabs(static_cast<float>(z) + 0.5f - tcz);
                const float o = std::max(fx, fz) + 0.41421356f * std::min(fx, fz) - discount;
                return o > 0 ? o * fastWeight : 0.0f;
            } else {
                return static_cast<float>(max(0.0, distanceAt(x, z) - stopAt) / cell);
            }
        };
        cs[start].g = 0;
        cs[start].mark = stamp;
        heap.push(h(start % w, start / w), start);
        int64_t closed = 0;
        struct Count { int64_t& n; ~Count() { NavGrid::searchesRun.fetch_add(1, std::memory_order_relaxed); NavGrid::cellsClosed.fetch_add(n, std::memory_order_relaxed); } } count{closed};
        while (!heap.empty()) {
            const int64_t i = heap.popCell();
            if (cs[i].mark == closedMark) continue;
            cs[i].mark = closedMark;
            closed++;
            const int64_t x = i % w, z = i / w;
            if (distanceAt(x, z) <= reach) {
                int64_t k = i;
                while (k != start) { cells.push_back(k); k = cs[k].came; }
                return i;
            }
            const float gi = cs[i].g;
            // Free cells and cliff faces keep three cells off the map's
            // edge, so only a start cell on the edge needs the bounds test.
            const bool inner = x > 0 && z > 0 && x < w - 1 && z < hgt - 1;
            auto visit = [&](auto d) {
                constexpr int64_t dx = dirs[decltype(d)::value].first, dz = dirs[decltype(d)::value].second;
                const int64_t nx = x + dx, nz = z + dz;
                if (!inner && !(nx >= 0 && nz >= 0 && nx < w && nz < hgt)) return;
                const int64_t j = i + dz * w + dx;
                const uint8_t pj = pass[j];
                if (!(pj & openMask)) return;
                Scratch::Cell& cj = cs[j];
                if (cj.mark == closedMark) return;
                float stepCost;
                if constexpr (dx != 0 && dz != 0) {
                    if (!(pass[i + dx] & openMask) || !(pass[i + dz * w] & openMask)) return;
                    stepCost = pj == 1 ? diagonal : diagonalCliff;
                } else {
                    stepCost = pj == 1 ? 1.0f : 1.3f;
                }
                const float t = gi + stepCost;
                if (cj.mark < stamp || t < cj.g) {
                    cj.g = t;
                    cj.came = static_cast<int32_t>(i);
                    cj.mark = stamp;
                    heap.push(t + h(nx, nz), j);
                }
            };
            [&]<size_t... d>(std::index_sequence<d...>) {
                (visit(std::integral_constant<size_t, d>{}), ...);
            }(std::make_index_sequence<8>{});
        }
        return -1;
    };
    const int64_t found = fastSearch ? (east(from) ? search(std::true_type{}, std::true_type{}) : search(std::false_type{}, std::true_type{}))
                                     : (east(from) ? search(std::true_type{}, std::false_type{}) : search(std::false_type{}, std::false_type{}));
    scratch().give(std::move(scratchArrays));
    if (!(found >= 0)) return std::nullopt;
    std::reverse(cells.begin(), cells.end());
    // Straighten: from each corner, jump to the farthest cell in sight.
    std::vector<Vec2> points;
    Vec2 at = prefix.empty() ? from : prefix.back();
    size_t k = 0;
    while (k < cells.size()) {
        size_t far = k;
        for (size_t m = cells.size() - 1; m > k; m--) {
            if (clear(at, centre(cells[m]), jumps)) { far = m; break; }
        }
        at = centre(cells[far]);
        points.push_back(at);
        k = far + 1;
    }
    std::vector<Vec2> result = prefix;
    result.insert(result.end(), points.begin(), points.end());
    return result;
}

/// A straight walk from `a` to `b` stays on free cells (the cell `a` is
/// in is allowed, so a unit can leave a tight spot).
/// Every cell the segment passes through is checked (a grid walk, not
/// samples): a leg that clips the corner of a blocked cell is not clear,
/// or a unit walking it steps into that corner and is sent back, again
/// and again. Where it crosses a cell corner exactly, both cells beside
/// the corner must be open.
bool NavGrid::clear(Vec2 a, Vec2 b, bool jumps) const {
    const auto firstIndex = index(a);
    if (!firstIndex || !index(b)) return false;
    const int64_t first = *firstIndex;
    auto open = [this, first, jumps](int64_t x, int64_t z) {
        if (!(x >= 0 && z >= 0 && x < width && z < height)) return false;
        const int64_t i = z * width + x;
        return i == first || free(i) || (jumps && cliffs[static_cast<size_t>(i)] && !dynamic[static_cast<size_t>(i)]);
    };
    const Vec2 pa = (a - origin) / cell, d = (b - a) / cell;
    int64_t x = static_cast<int64_t>(std::floor(pa.x)), z = static_cast<int64_t>(std::floor(pa.y));
    const int64_t sx = d.x > 0 ? 1 : -1, sz = d.y > 0 ? 1 : -1;
    const double inf = std::numeric_limits<double>::infinity();
    // Fraction of the segment per cell, and to the first cell edge, per axis.
    const double tdx = std::fabs(d.x) > 1e-12 ? 1 / std::fabs(d.x) : inf;
    const double tdz = std::fabs(d.y) > 1e-12 ? 1 / std::fabs(d.y) : inf;
    double tx = std::fabs(d.x) > 1e-12 ? (d.x > 0 ? static_cast<double>(x + 1) - pa.x : pa.x - static_cast<double>(x)) * tdx : inf;
    double tz = std::fabs(d.y) > 1e-12 ? (d.y > 0 ? static_cast<double>(z + 1) - pa.y : pa.y - static_cast<double>(z)) * tdz : inf;
    while (min(tx, tz) < 1) {
        if (std::fabs(tx - tz) < 1e-9) {
            if (!open(x + sx, z) || !open(x, z + sz)) return false;
            x += sx; z += sz; tx += tdx; tz += tdz;
        } else if (tx < tz) {
            x += sx; tx += tdx;
        } else {
            z += sz; tz += tdz;
        }
        if (!open(x, z)) return false;
    }
    return true;
}

std::atomic<int64_t> NavGrid::searchesRun{0}, NavGrid::cellsClosed{0};

bool NavGrid::crossesChange(Vec2 a, Vec2 b) const {
    if (newlyBlocked.empty() || changedMask.empty()) return false;
    const Vec2 pa = (a - origin) / cell, pb = (b - origin) / cell;
    // Outside the changed cells' box (grown by one): no need to walk it.
    const double lox = static_cast<double>(changedBox[0]), loz = static_cast<double>(changedBox[1]);
    const double hix = static_cast<double>(changedBox[2] + 1), hiz = static_cast<double>(changedBox[3] + 1);
    if (std::max(pa.x, pb.x) < lox || std::min(pa.x, pb.x) > hix || std::max(pa.y, pb.y) < loz || std::min(pa.y, pb.y) > hiz) return false;
    auto hit = [this](int64_t x, int64_t z) {
        return x >= 0 && z >= 0 && x < width && z < height && changedMask[static_cast<size_t>(z * width + x)];
    };
    const Vec2 d = pb - pa;
    int64_t x = static_cast<int64_t>(std::floor(pa.x)), z = static_cast<int64_t>(std::floor(pa.y));
    if (hit(x, z)) return true;
    const int64_t sx = d.x > 0 ? 1 : -1, sz = d.y > 0 ? 1 : -1;
    const double inf = std::numeric_limits<double>::infinity();
    const double tdx = std::fabs(d.x) > 1e-12 ? 1 / std::fabs(d.x) : inf;
    const double tdz = std::fabs(d.y) > 1e-12 ? 1 / std::fabs(d.y) : inf;
    double tx = std::fabs(d.x) > 1e-12 ? (d.x > 0 ? static_cast<double>(x + 1) - pa.x : pa.x - static_cast<double>(x)) * tdx : inf;
    double tz = std::fabs(d.y) > 1e-12 ? (d.y > 0 ? static_cast<double>(z + 1) - pa.y : pa.y - static_cast<double>(z)) * tdz : inf;
    while (min(tx, tz) < 1) {
        if (tx < tz) { x += sx; tx += tdx; } else { z += sz; tz += tdz; }
        if (hit(x, z)) return true;
    }
    return false;
}

// MARK: - Cache

namespace {
struct CacheStore {
    std::mutex lock;
    std::map<std::string, std::vector<uint8_t>> layers;
};
CacheStore& cacheStore() {
    static CacheStore store;
    return store;
}
struct PoolStore {
    std::mutex lock;
    std::vector<std::unique_ptr<NavGrid::Scratch>> idle;
};
PoolStore& poolStore() {
    static PoolStore store;
    return store;
}
} // namespace

std::optional<std::vector<uint8_t>> NavGrid::Cache::get(const std::string& k) {
    CacheStore& s = cacheStore();
    std::lock_guard<std::mutex> guard(s.lock);
    auto it = s.layers.find(k);
    if (it == s.layers.end()) return std::nullopt;
    return it->second;
}

void NavGrid::Cache::set(const std::string& k, const std::vector<uint8_t>& v) {
    CacheStore& s = cacheStore();
    std::lock_guard<std::mutex> guard(s.lock);
    s.layers[k] = v;
}

NavGrid::Cache& NavGrid::cache() {
    static Cache c;
    return c;
}

/// Ready for a search over `n` cells: its stamp.
uint32_t NavGrid::Scratch::begin(int64_t n) {
    if (static_cast<int64_t>(cells.size()) != n || stamp >= std::numeric_limits<uint32_t>::max() - 4) {
        cells = std::vector<Cell>(static_cast<size_t>(n), Cell{0, 0, -1});
        stamp = 0;
    }
    stamp += 2;
    heap.clear();
    heap4.clear();
    return stamp;
}

std::unique_ptr<NavGrid::Scratch> NavGrid::ScratchPool::take() {
    PoolStore& s = poolStore();
    std::lock_guard<std::mutex> guard(s.lock);
    if (s.idle.empty()) return std::make_unique<Scratch>();
    std::unique_ptr<Scratch> out = std::move(s.idle.back());
    s.idle.pop_back();
    return out;
}

void NavGrid::ScratchPool::give(std::unique_ptr<Scratch> sc) {
    PoolStore& s = poolStore();
    std::lock_guard<std::mutex> guard(s.lock);
    s.idle.push_back(std::move(sc));
}

NavGrid::ScratchPool& NavGrid::scratch() {
    static ScratchPool p;
    return p;
}

} // namespace ac
