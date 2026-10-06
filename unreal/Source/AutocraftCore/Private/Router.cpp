// Port of Sources/GameCore/Router.swift.
#include "Router.h"

#include "TerrainField.h"

#include <array>
#include <deque>
#include <map>
#include <set>

namespace ac {

Router::Router(const MapDefinition& map_) : map(map_) {
    for (const Ramp& r : map.ramps) {
        const Vec2 dir = normalize(r.high - r.low);
        const Vec2 lowEnd = r.low - dir * 1.2, highEnd = r.high + dir * 1.2;
        links.push_back(Link{region(lowEnd, map), region(highEnd, map), lowEnd, highEnd});
    }
}

/// -1 for the low ground, else the index of the topmost plateau under `p`.
int64_t Router::region(Vec2 p, const MapDefinition& map) {
    int64_t best = -1, level = 0;
    for (size_t i = 0; i < map.plateaus.size(); i++) {
        const Plateau& pl = map.plateaus[i];
        if (!(pl.level > level && signedDistance(p, pl.polygon) < 0)) continue;
        best = static_cast<int64_t>(i); level = pl.level;
    }
    return best;
}

/// Waypoints from `from` to `to`, ending at `to`. Direct when both are in
/// one region or no ramp joins their regions.
std::vector<Vec2> Router::route(Vec2 from, Vec2 to) const {
    const int64_t start = region(from), goal = region(to);
    if (!(start != goal)) return {to};
    // Breadth-first over regions; each step goes through one ramp.
    struct Previous { int64_t region; Vec2 via, out; };
    std::map<int64_t, Previous> previous;
    std::deque<int64_t> queue{start};
    std::set<int64_t> seen{start};
    while (!queue.empty()) {
        const int64_t r = queue.front();
        queue.pop_front();
        if (r == goal) break;
        for (const Link& l : links) {
            struct Way { int64_t here, there; Vec2 entry, exit; };
            for (const Way& way : std::array<Way, 2>{Way{l.a, l.b, l.aEnd, l.bEnd}, Way{l.b, l.a, l.bEnd, l.aEnd}}) {
                if (!(way.here == r && !seen.count(way.there))) continue;
                seen.insert(way.there);
                previous[way.there] = Previous{r, way.entry, way.exit};
                queue.push_back(way.there);
            }
        }
    }
    if (!previous.count(goal)) return {to};
    std::vector<Vec2> points{to};
    int64_t r = goal;
    for (auto p = previous.find(r); p != previous.end(); p = previous.find(r)) {
        points.insert(points.begin(), {p->second.via, p->second.out});
        r = p->second.region;
    }
    return points;
}

/// Walking distance along `route`.
double Router::distance(Vec2 from, Vec2 to) const {
    double d = 0.0;
    Vec2 at = from;
    for (Vec2 p : route(from, to)) { d += ac::distance(at, p); at = p; }
    return d;
}

} // namespace ac
