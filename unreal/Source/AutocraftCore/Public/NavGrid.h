// Port of Sources/GameCore/NavGrid.swift.
#pragma once

#include "SimdMath.h"
#include "TerrainField.h"
#include "Types.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/// A search's hot helpers, inlined even where the compiler would not.
#if defined(__clang__) || defined(__GNUC__)
#define AC_ALWAYS_INLINE __attribute__((always_inline)) inline
#else
#define AC_ALWAYS_INLINE inline
#endif

namespace ac {

/// `a < b` as 0 or 1 without a branch. Which of a heap node's children is
/// smaller is a coin toss the CPU cannot learn, and clang turns the plain
/// comparison back into a branch; on arm64 `fcmp` + `cset mi` is exactly
/// `<` (false when either is NaN).
inline size_t lessBit(float a, float b) {
#if defined(__aarch64__) && (defined(__clang__) || defined(__GNUC__))
    uint64_t r;
    __asm__("fcmp %s1, %s2\n\tcset %x0, mi" : "=r"(r) : "w"(a), "w"(b) : "cc");
    return static_cast<size_t>(r);
#else
    return static_cast<size_t>(a < b);
#endif
}

/// Binary min-heap of (priority, cell) for A*. Sifts move a hole rather
/// than swapping, which leaves every item where swaps would: equal
/// priorities still come out in Swift's order.
struct Heap {
    struct Item { float first; int32_t second; };
    std::vector<Item> items;
    void clear() { items.clear(); }
    bool empty() const { return items.empty(); }
    AC_ALWAYS_INLINE void push(float p, int64_t i) {
        size_t c = items.size();
        items.push_back(Item{p, static_cast<int32_t>(i)});
        Item* a = items.data();
        while (c > 0) {
            const size_t parent = (c - 1) / 2;
            if (a[parent].first <= p) break;
            a[c] = a[parent];
            c = parent;
        }
        a[c] = Item{p, static_cast<int32_t>(i)};
    }
    /// The smallest item's cell; the heap must not be empty.
    int64_t popCell() {
        Item* a = items.data();
        const int32_t top = a[0].second;
        const Item last = items.back();
        items.pop_back();
        const size_t n = items.size();
        if (n > 0) {
            size_t c = 0;
            while (true) {
                const size_t l = 2 * c + 1;
                if (l >= n) break;
                const size_t r = l + 1 < n ? l + 1 : l;
                const size_t m = l + lessBit(a[r].first, a[l].first);
                if (!(a[m].first < last.first)) break;
                a[c] = a[m];
                c = m;
            }
            a[c] = last;
        }
        return top;
    }
    std::optional<std::pair<float, int64_t>> pop() {
        if (items.empty()) return std::nullopt;
        const float p = items[0].first;
        return std::pair<float, int64_t>{p, popCell()};
    }
};

/// 4-ary min-heap, the same interface as `Heap` (fast searches: shallower, and
/// the four children of a node sit in one cache line). Ties come out in
/// another order than `Heap`'s.
struct Heap4 {
    using Item = Heap::Item;
    std::vector<Item> items;
    void clear() { items.clear(); }
    bool empty() const { return items.empty(); }
    AC_ALWAYS_INLINE void push(float p, int64_t i) {
        size_t c = items.size();
        items.push_back(Item{p, static_cast<int32_t>(i)});
        Item* a = items.data();
        while (c > 0) {
            const size_t parent = (c - 1) / 4;
            if (a[parent].first <= p) break;
            a[c] = a[parent];
            c = parent;
        }
        a[c] = Item{p, static_cast<int32_t>(i)};
    }
    int64_t popCell() {
        Item* a = items.data();
        const int32_t top = a[0].second;
        const Item last = items.back();
        items.pop_back();
        const size_t n = items.size();
        if (n > 0) {
            size_t c = 0;
            while (true) {
                const size_t f = 4 * c + 1;
                if (f >= n) break;
                size_t m = f;
                const size_t end = f + 4 < n ? f + 4 : n;
                for (size_t k = f + 1; k < end; k++) if (a[k].first < a[m].first) m = k;
                if (!(a[m].first < last.first)) break;
                a[c] = a[m];
                c = m;
            }
            a[c] = last;
        }
        return top;
    }
};

/// Where units can walk, on a half-cell grid, and the paths between points.
/// Cliffs, doodads, wells, ore deposits and buildings are solid. Every
/// obstacle is grown by a unit's radius, so a path through free
/// cells keeps a unit's body clear of it. Buildings and ore deposits change
/// during a game (`setDynamic`), as do the doodads and wells put down in
/// the playground; the rest is fixed per map.
struct NavGrid {
    static constexpr double cell = 0.5;
    /// A unit's radius (the Prospector's and the Ranger's: 0.375), a little less so
    /// units may squeeze through a gap exactly one unit wide.
    static constexpr double clearance = 0.35;
    /// Steeper ground than this (height per cell) is a cliff face. Ramps rise
    /// about 0.4 per cell, cliff faces 1.5 and more.
    static constexpr double maxSlope = 0.9;

    /// A solid circle: centre and radius (Swift's `(Vec2, Double)`).
    using Circle = std::pair<Vec2, double>;

    Vec2 origin;
    /// A mirrored map's axis: searches from its east half scan x the other
    /// way, so equal choices fall the same way on both halves.
    std::optional<double> mirrorX;
    int64_t width = 0;
    int64_t height = 0;
    /// Walkable before buildings and ore: ground, not cliff or doodad.
    /// (Swift's `[Bool]`: one byte a cell.)
    std::vector<uint8_t> ground;
    /// Cliff faces a Comet can jump: too steep to walk, but clear of
    /// doodads and wells and in play.
    std::vector<uint8_t> cliffs;
    /// The doodads' solid circles, for exact checks inside a free cell:
    /// the map's, then those put down in the playground (`setDynamic`).
    std::vector<Circle> rocks;
    int64_t mapRocks = 0;
    /// The map's own wells, which `ground` already keeps clear of: those
    /// past them in `GameState.wells` were put down in the playground.
    int64_t mapWells = 0;
    /// Covered by a building or a live ore deposit (grown by clearance).
    std::vector<uint8_t> dynamic;
    /// Ids of what `dynamic` was built from, to rebuild only on change.
    std::vector<int64_t> dynamicKey;
    /// Connected walkable areas: cells with the same number reach each other
    /// (-1: blocked). Rebuilt with `dynamic`.
    std::vector<int32_t> regions;
    /// Each region's size in cells, and whether it borders a cliff face a
    /// Comet can jump. Rebuilt with `regions`.
    std::vector<int64_t> regionCells;
    std::vector<uint8_t> regionCliff;
    /// The same for a Comet, which also crosses the cliff faces it can
    /// jump (`cliffs`). Rebuilt with `regions`.
    std::vector<int32_t> jumpRegions;
    /// The largest region: the open map.
    int32_t mainRegion = -1;
    /// Per cell: 1 free (`free`), 2 a cliff face a Comet can jump (not
    /// covered). Rebuilt with `dynamic`; what a search tests per step.
    std::vector<uint8_t> passable;
    /// `rocks` by square buckets of `rockBucket` (from `origin`), for
    /// `inRock`: bucket `b` holds `rockList[rockStart[b] ..< rockStart[b + 1]]`,
    /// every rock whose circle's box reaches it. Rebuilt with `rocks`.
    static constexpr double rockBucket = 2.0;
    int64_t rockColumns = 0, rockRows = 0;
    std::vector<int32_t> rockStart, rockList;
    size_t rocksIndexed = 0;

    /// C++ only, fast paths (PORTING.md "Fast paths"; off = Swift's results
    /// bit for bit). On: searches use a weighted octile heuristic and a 4-ary
    /// heap (other tie order, paths up to `fastWeight` times the shortest), and `setDynamic` keeps what it newly blocked.
    bool fastSearch = false;
    /// Fast mode weighs the heuristic: a path costs at most this times the
    /// shortest (before straightening). Weight 1 closes 10x more cells on the
    /// cliff-ridden maps (late8's first step: 34 M cells), because the
    /// search fills every dead-end basin before it tries the way around.
    static constexpr float fastWeight = 1.15f;
    /// Fast mode: the cells `setDynamic`'s last change newly blocked, and
    /// them grown by one cell as a mask (`changedBox`: lo x, lo z, hi x, hi z).
    std::vector<int64_t> newlyBlocked;
    std::vector<uint8_t> changedMask;
    std::array<int64_t, 4> changedBox{0, 0, -1, -1};
    /// Totals over every search (any thread): searches run and cells they
    /// closed, for the bench.
    static std::atomic<int64_t> searchesRun, cellsClosed;
    /// A straight walk from `a` to `b` passes through a cell `setDynamic`
    /// newly blocked (or next to one): the route is stale.
    bool crossesChange(Vec2 a, Vec2 b) const;

    NavGrid(const MapDefinition& map, const TerrainField& field);

    // MARK: - Cells

    std::optional<int64_t> index(Vec2 p) const {
        const int64_t x = static_cast<int64_t>(std::floor((p.x - origin.x) / cell)),
                      z = static_cast<int64_t>(std::floor((p.y - origin.y) / cell));
        if (!(x >= 0 && z >= 0 && x < width && z < height)) return std::nullopt;
        return z * width + x;
    }

    Vec2 centre(int64_t i) const {
        return origin + Vec2(static_cast<double>(i % width) + 0.5, static_cast<double>(i / width) + 0.5) * cell;
    }

    bool free(int64_t i) const { return ground[static_cast<size_t>(i)] && !dynamic[static_cast<size_t>(i)]; }

    /// The centre of the free cell `p` is in, clear of rocks (nil: that
    /// cell is blocked). Paths from a cell centre never clip a corner.
    std::optional<Vec2> freeCentre(Vec2 p) const;

    /// A unit can stand at `p` without touching anything.
    bool walkable(Vec2 p) const;

    /// Open ground at `p`, cliffs and the map edge excluded, whatever
    /// buildings and fields stand on it (a unit stepping by hand checks those
    /// itself, closer than the grid's cells).
    bool standable(Vec2 p) const;

    /// `p` is inside a doodad's rock (cells are coarser than rocks' edges).
    bool inRock(Vec2 p) const;

    /// `p` is on a cliff face a Comet can jump (and no building covers it).
    bool cliff(Vec2 p) const;

    // MARK: - Obstacles

    /// Solid circles of a doodad (offset in its own frame, radius), before
    /// its rotation and scale. Plants, bushes and flat debris are walkable.
    static std::vector<Circle> footprint(Doodad::Kind k);

    /// World circles a doodad blocks. The scene turns it by `rotation` about
    /// +Y: local (x, z) goes to (x·cos + z·sin, −x·sin + z·cos).
    static std::vector<Circle> circles(const Doodad& d);

    /// An ore deposit is 2x1, its long axis turned by `angle` like the scene
    /// turns its model.
    static bool insidePatch(Vec2 p, const OreDeposit& m, double grow);

    static bool insideStructure(Vec2 p, const Structure& st, double grow);

    /// Anything solid exactly at `p` (not grown): a building or a live
    /// ore deposit, or a doodad put down in the playground. Units found
    /// here are pushed out.
    static bool solid(Vec2 p, const GameState& s);

    /// `(ground: [Bool], cliffs: [Bool])`.
    struct Layers { std::vector<uint8_t> ground, cliffs; };
    static Layers buildGround(const MapDefinition& map, const TerrainField& field, Vec2 origin_, int64_t width_,
                              int64_t height_);

    /// Rebuild the building and ore layer when either changed (or a
    /// doodad or well was put down in the playground). True when it did
    /// (paths through the old layout are stale).
    bool setDynamic(const GameState& s);

    /// Flood-fill the free cells into regions. Four-way, which matches A*'s
    /// reach (it steps diagonally only past two open sides).
    void labelRegions();

    /// Some cell within `reach` of `target` that a search from cell `start`
    /// can step on is in its region. The search moves as the regions
    /// connect (a diagonal step needs both cells beside it open), so
    /// without one it would fail, after flooding the whole region.
    bool reachable(int64_t start, Vec2 target, double reach, bool jumps) const;

    /// A ground unit at `p` is boxed in: the ground it walks in (see
    /// `region`) is cut off from the open map and smaller than `pocket`
    /// square cells, so a gap between buildings, not a walled-off base.
    /// `jumps` (a Comet): not when that ground borders a cliff face.
    bool boxedIn(Vec2 p, double pocket, bool jumps = false) const;

    /// A square footprint: centre and half side (a building's `Rules::radius`).
    using Box = std::pair<Vec2, double>;
    /// C++ only: a building over `boxes` would cut the ground about it in
    /// two. Free cells beside it that are joined now, within `window` cells
    /// of it, would no longer be (four-way, as `labelRegions`); `pending`
    /// (buildings ordered, not yet begun) count as standing both times.
    /// A Commander never builds there: its Habitat Domes, set corner to
    /// corner with the Citadel, walled in a base's ore line (the user's
    /// report, 2026-10-06). A way round farther out than `window` is not
    /// seen, so the answer errs on the side of a cut.
    bool cuts(const std::vector<Box>& boxes, const std::vector<Box>& pending = {}, double window = 12) const;

    /// The region a unit at `p` walks in (that of the nearest free spot).
    std::optional<int32_t> region(Vec2 p) const;

    /// The free spot nearest `target` that a unit at `from` can walk to.
    std::optional<Vec2> closestReachable(Vec2 target, Vec2 from) const;

    // MARK: - Paths

    /// The nearest free spot to `p`, searching outward up to `limit` cells.
    std::optional<Vec2> nearestFree(Vec2 p, double limit = 8) const;

    /// Up to 8 neighbours with their step lengths (Swift's `[(Int, Double)]`).
    struct Neighbours {
        std::array<std::pair<int64_t, double>, 8> items;
        size_t count = 0;
        const std::pair<int64_t, double>* begin() const { return items.data(); }
        const std::pair<int64_t, double>* end() const { return items.data() + count; }
    };
    /// 8 neighbours with their step lengths; diagonals only when both sides
    /// are open (no cutting corners).
    Neighbours neighbours(int64_t i, bool flip = false) const;

    /// Waypoints from `from` to a free spot within `stopAt` of `target`,
    /// around every obstacle, straightened; nil when there is no way. The
    /// last waypoint is where the unit stops short of the target.
    /// With `jumps` (a Comet) it may cross cliff faces, at a little extra
    /// cost so it still takes the plain way when that is as short.
    std::optional<std::vector<Vec2>> path(Vec2 from, Vec2 target, double stopAt, bool jumps = false) const;

    /// `path` in two halves (C++ only), so the search can run on another
    /// thread (`PathAhead`): `pathBegin` makes the cheap checks and either
    /// answers (`done`) or says where the search starts; `pathSearch` is the
    /// A* and the straightening. `path` is the one, then the other.
    struct PathBegin {
        bool done = false;
        std::optional<std::vector<Vec2>> result;
        int64_t start = 0;
        /// A step out of something the unit stands in, first.
        std::vector<Vec2> prefix;
    };
    PathBegin pathBegin(Vec2 from, Vec2 target, double stopAt, bool jumps = false) const;
    std::optional<std::vector<Vec2>> pathSearch(const PathBegin& begin, Vec2 from, Vec2 target, double stopAt,
                                                bool jumps = false) const;

    using Step = std::pair<int64_t, int64_t>;
    static constexpr std::array<Step, 8> steps{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}}};
    static constexpr std::array<Step, 8> stepsEast{{{-1, 0}, {1, 0}, {0, 1}, {0, -1}, {-1, 1}, {-1, -1}, {1, 1}, {1, -1}}};

    /// On the east half of a mirrored map.
    bool east(Vec2 p) const { return mirrorX ? p.x > *mirrorX : false; }

    /// A straight walk from `a` to `b` stays on free cells (the cell `a` is
    /// in is allowed, so a unit can leave a tight spot).
    /// Every cell the segment passes through is checked (a grid walk, not
    /// samples): a leg that clips the corner of a blocked cell is not clear,
    /// or a unit walking it steps into that corner and is sent back, again
    /// and again. Where it crosses a cell corner exactly, both cells beside
    /// the corner must be open.
    bool clear(Vec2 a, Vec2 b, bool jumps = false) const;

    // MARK: - Cache

    /// Ground layers per map, so every simulation and test on a map shares
    /// the one sampling of the terrain.
    struct Cache {
        std::optional<std::vector<uint8_t>> get(const std::string& k);
        void set(const std::string& k, const std::vector<uint8_t>& v);
    };
    static Cache& cache();

    /// A search's working arrays, kept between searches: allocating and
    /// clearing a whole grid's worth for each one cost more than most
    /// searches. A cell's cost and where it was reached from count only
    /// when its `mark` is the search's stamp (or one more: closed).
    struct Scratch {
        /// One cell's state, side by side for the cache.
        struct Cell { uint32_t mark; float g; int32_t came; };
        std::vector<Cell> cells;
        Heap heap;
        Heap4 heap4;
        /// Ready for a search over `n` cells: its stamp.
        uint32_t begin(int64_t n);
    private:
        uint32_t stamp = 0;
    };

    /// Scratch for each search running at once (simulations on other
    /// threads, tests).
    struct ScratchPool {
        std::unique_ptr<Scratch> take();
        void give(std::unique_ptr<Scratch> s);
    };
    static ScratchPool& scratch();

private:
    template <class Inside> void mark(Vec2 lo, Vec2 hi, Inside inside);
    /// Rebuild `passable` from `ground`, `cliffs` and `dynamic`.
    void indexPassable();
    /// Rebuild the rock buckets from `rocks`.
    void indexRocks();
};

} // namespace ac
