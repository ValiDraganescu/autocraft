// Port of Sources/GameCore/Vision.swift: `Vision` and the Simulation's fog
// (`Simulation+Vision.members.h`).
#include "Vision.h"

#include "Commander.h"
#include "NavGrid.h"
#include "Rules.h"
#include "Simulation.h"
#include "TerrainField.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <mutex>
#include <numeric>
#include <string>
#include <utility>

namespace ac {

static_assert(Vision::cell == NavGrid::cell, "the fog's grid is the walking grid's");

namespace {

/// Terrain levels per map, shared by every simulation on it.
struct VisionCache {
    std::map<std::string, std::vector<int8_t>> layers;
    std::mutex lock;
    std::optional<std::vector<int8_t>> get(const std::string& k) {
        std::lock_guard<std::mutex> g(lock);
        auto it = layers.find(k);
        if (it == layers.end()) return std::nullopt;
        return it->second;
    }
    void set(const std::string& k, const std::vector<int8_t>& v) {
        std::lock_guard<std::mutex> g(lock);
        layers[k] = v;
    }
};

VisionCache& visionCache() {
    static VisionCache cache;
    return cache;
}

/// `Int8(clamping: Int(v))`.
int8_t clampedInt8(double v) {
    const int64_t i = static_cast<int64_t>(v);
    return static_cast<int8_t>(min<int64_t>(max<int64_t>(i, -128), 127));
}

/// A viewer: where, how far, from what level (nil: the air).
struct Viewer { Vec2 c; double r; std::optional<int8_t> level; };

bool hidden(Unit::Task t) {
    return t == Unit::Task::inBastion || t == Unit::Task::aboard || t == Unit::Task::inDerrick;
}

} // namespace

Vision::Vision(const MapDefinition& map, const TerrainField& field, const NavGrid& nav) {
    origin = nav.origin;
    width = nav.width;
    height = nav.height;
    char key[512];
    const double mirror = map.mirrorX ? *map.mirrorX : std::numeric_limits<double>::quiet_NaN();
    std::snprintf(key, sizeof key, "vision|%s|%lld|%llu|%.17g,%.17g,%.17g,%.17g|%.17g", map.name.c_str(),
                  static_cast<long long>(map.version), static_cast<unsigned long long>(map.seed), map.bounds.minX,
                  map.bounds.minZ, map.bounds.maxX, map.bounds.maxZ, mirror);
    if (auto cached = visionCache().get(key)) {
        tier = *cached;
    } else {
        std::vector<int8_t> t(static_cast<size_t>(width * height), 0);
        const Vec2 o = origin;
        const int64_t w = width, h = height;
        const double lh = map.levelHeight;
        // (Swift spreads the rows over the cores; each cell is its own.)
        for (int64_t z = 0; z < h; ++z) {
            for (int64_t x = 0; x < w; ++x) {
                const Vec2 p = o + Vec2(static_cast<double>(x) + 0.5, static_cast<double>(z) + 0.5) * cell;
                t[static_cast<size_t>(z * w + x)] = clampedInt8(rounded(field.height(p) / lh));
            }
        }
        tier = t;
        visionCache().set(key, t);
    }
    for (const auto& d : map.doodads) {
        if (d.kind == Doodad::Kind::tower) towers.push_back(Tower{d.position, tierIn(tier, origin, width, height, d.position)});
    }
}

/// A watchtower put down in the playground (`Simulation.placeDoodad`).
void Vision::addTower(Vec2 p) {
    towers.push_back(Tower{p, tierIn(tier, origin, width, height, p)});
}

int8_t Vision::tierIn(const std::vector<int8_t>& t, Vec2 o, int64_t w, int64_t h, Vec2 p) {
    const int64_t x = static_cast<int64_t>(std::floor((p.x - o.x) / cell));
    const int64_t z = static_cast<int64_t>(std::floor((p.y - o.y) / cell));
    if (!(x >= 0 && z >= 0 && x < w && z < h)) return 0;
    return t[static_cast<size_t>(z * w + x)];
}

/// The level a viewer at `p` sees from.
int8_t Vision::tierAt(Vec2 p) const {
    const auto i = index(p);
    return i ? tier[static_cast<size_t>(*i)] : 0;
}

/// Mark what a viewer at `c` sees, `r` round, from level `level` (nil:
/// in the air, every level): `seen` (and `explored`) where it sees the
/// ground, `near` wherever it is in reach (an air unit there is seen).
void Vision::stamp(Vec2 c, double r, std::optional<int8_t> level, std::vector<uint8_t>& seen, std::vector<uint8_t>& near,
                   std::vector<uint64_t>& explored) const {
    const int64_t x0 = max<int64_t>(0, static_cast<int64_t>(std::floor((c.x - r - origin.x) / cell)));
    const int64_t x1 = min<int64_t>(width - 1, static_cast<int64_t>(std::floor((c.x + r - origin.x) / cell)));
    const int64_t z0 = max<int64_t>(0, static_cast<int64_t>(std::floor((c.y - r - origin.y) / cell)));
    const int64_t z1 = min<int64_t>(height - 1, static_cast<int64_t>(std::floor((c.y + r - origin.y) / cell)));
    if (!(x0 <= x1 && z0 <= z1)) return;
    const double r2 = r * r;
    const int8_t top = level ? *level : std::numeric_limits<int8_t>::max();
    const int8_t* t = tier.data();
    uint8_t* s = seen.data();
    uint8_t* n = near.data();
    uint64_t* ex = explored.data();
    for (int64_t z = z0; z <= z1; ++z) {
        const double dz = origin.y + (static_cast<double>(z) + 0.5) * cell - c.y;
        const double dz2 = dz * dz;
        if (!(dz2 <= r2)) continue;
        const int64_t row = z * width;
        for (int64_t x = x0; x <= x1; ++x) {
            const double dx = origin.x + (static_cast<double>(x) + 0.5) * cell - c.x;
            if (!(dx * dx + dz2 <= r2)) continue;
            const int64_t i = row + x;
            n[i] = 1;
            if (t[i] <= top && !s[i]) {
                s[i] = 1;
                ex[i >> 6] |= uint64_t(1) << static_cast<uint64_t>(i & 63);
            }
        }
    }
}

/// What player `p` sees in `state` now; what it sees joins `explored`.
/// `farther` holds the sight radius of units whose leveling picks
/// change it (Spotter), by id.
Sight Vision::sight(int64_t p, const GameState& state, std::vector<uint64_t>& explored,
                    const std::map<int64_t, double>& farther, const std::map<int64_t, double>& reveal) const {
    const int64_t cells = width * height;
    Sight s;
    s.seen.assign(static_cast<size_t>(cells), 0);
    s.near.assign(static_cast<size_t>(cells), 0);
    const size_t words = static_cast<size_t>((cells + 63) / 64);
    if (explored.size() != words) explored.assign(words, 0);
    // Every viewer: where, how far, from what level (nil: the air).
    std::vector<Viewer> viewers;
    std::vector<bool> holds(towers.size(), false);
    // Its own and its allies' (C++: teams share their sight).
    for (const auto& b : state.structures) {
        if (!(state.allied(b.owner, p) && b.hp > 0)) continue;
        viewers.push_back(Viewer{b.position, Rules::vision(b.kind), tierAt(b.position)});
    }
    for (const auto& u : state.units) {
        if (!(state.allied(u.owner, p) && u.hp > 0)) continue;
        if (hidden(u.task)) continue;
        const bool air = u.stats().air;
        auto f = farther.find(u.id);
        viewers.push_back(Viewer{u.position, f != farther.end() ? f->second : Rules::vision(u.kind),
                                 air ? std::nullopt : std::optional<int8_t>(tierAt(u.position))});
        if (!air) {
            for (size_t k = 0; k < towers.size(); ++k) {
                if (distance(towers[k].at, u.position) <= Rules::towerReach) holds[k] = true;
            }
        }
    }
    for (size_t k = 0; k < towers.size(); ++k) {
        if (holds[k]) viewers.push_back(Viewer{towers[k].at, Rules::towerSight, towers[k].tier});
    }
    // The widest first; one inside another's circle on its level adds
    // nothing (a Prospector by its Citadel).
    std::vector<size_t> order(viewers.size());
    std::iota(order.begin(), order.end(), size_t(0));
    std::sort(order.begin(), order.end(), [&viewers](size_t a, size_t b) {
        return viewers[a].r > viewers[b].r || (viewers[a].r == viewers[b].r && a < b);
    });
    std::vector<Viewer> drawn;
    for (size_t k : order) {
        const Viewer& v = viewers[k];
        bool inside = false;
        for (const auto& d : drawn) {
            if ((!d.level || d.level == v.level) && distance(d.c, v.c) + v.r <= d.r) { inside = true; break; }
        }
        if (inside) continue;
        drawn.push_back(v);
        stamp(v.c, v.r, v.level, s.seen, s.near, explored);
    }
    // What of the others' it sees.
    for (const auto& u : state.units) {
        if (!(state.hostile(u.owner, p) && u.hp > 0)) continue;
        if (hidden(u.task)) continue;
        if (u.burrowed()) {
            auto r = reveal.find(u.id);
            if (hides(u, p, state, r != reveal.end() ? r->second : Rules::revealRange)) continue;
        }
        const auto i = index(u.position);
        if (!i) continue;
        if (u.stats().air ? s.near[static_cast<size_t>(*i)] : s.seen[static_cast<size_t>(*i)]) s.ids.insert(u.id);
    }
    for (const auto& b : state.structures) {
        if (!(state.hostile(b.owner, p) && b.hp > 0)) continue;
        if (sees(b, s.seen)) s.ids.insert(b.id);
    }
    return s;
}

/// A buried Scorpion `u` is out of sight of player `p` (and its allies).
bool Vision::hides(const Unit& u, int64_t p, const GameState& state, double range) {
    // Visible to all for `Rules.strikeReveal` s after each strike (its own
    // clock), and while it reloads.
    if (u.struckAt && state.time - *u.struckAt < Rules::strikeReveal) return false;
    if (u.cooldown.value_or(0) > 0) return false;
    for (const auto& v : state.units) {
        if (!(state.allied(v.owner, p) && v.hp > 0) || hidden(v.task)) continue;
        if (distance(v.position, u.position) <= range) return false;
    }
    for (const auto& b : state.structures) {
        if (!(state.allied(b.owner, p) && b.hp > 0)) continue;
        const double d = distance(b.position, u.position);
        if (d - Rules::radius(b.kind) <= range) return false;
        // An enemy Sentinel is the detector: its whole sight.
        if (b.kind == Structure::Kind::sentinel && b.complete() && d <= Rules::vision(b.kind)) return false;
    }
    return true;
}

/// Any cell of a building's square footprint is in `seen`.
bool Vision::sees(const Structure& b, const std::vector<uint8_t>& seen) const {
    const double r = Rules::radius(b.kind) - 0.01;
    const int64_t x0 = max<int64_t>(0, static_cast<int64_t>(std::floor((b.position.x - r - origin.x) / cell)));
    const int64_t x1 = min<int64_t>(width - 1, static_cast<int64_t>(std::floor((b.position.x + r - origin.x) / cell)));
    const int64_t z0 = max<int64_t>(0, static_cast<int64_t>(std::floor((b.position.y - r - origin.y) / cell)));
    const int64_t z1 = min<int64_t>(height - 1, static_cast<int64_t>(std::floor((b.position.y + r - origin.y) / cell)));
    if (!(x0 <= x1 && z0 <= z1)) return false;
    for (int64_t z = z0; z <= z1; ++z) {
        for (int64_t x = x0; x <= x1; ++x) {
            if (seen[static_cast<size_t>(z * width + x)]) return true;
        }
    }
    return false;
}

// (`Rules.vision`, the sight radii, are in Rules.cpp.)

// MARK: - The simulation's fog

/// Player `p` sees the unit or building `id` now (its own always; every
/// one on a map without fog).
bool Simulation::sees(int64_t p, int64_t id) const {
    if (!(p >= 0 && p < static_cast<int64_t>(sights.size()))) {
        // No fog here: everything, but a buried Scorpion (it hides on every map).
        if (const auto i = unitIndex(id)) return !hiddenFrom(state.units[static_cast<size_t>(*i)], p);
        return true;
    }
    if (sights[static_cast<size_t>(p)].ids.count(id) > 0) return true;
    const auto o = owner(id);
    return o && state.allied(*o, p);
}

/// Player `p` sees the ground at `at` now (everywhere without fog).
bool Simulation::sees(int64_t p, Vec2 at) const {
    if (!(vision && p >= 0 && p < static_cast<int64_t>(sights.size()))) return true;
    const auto i = vision->index(at);
    return i ? sights[static_cast<size_t>(p)].seen[static_cast<size_t>(*i)] != 0 : false;
}

/// What player `p` knows of the others (nil: no fog on this map).
std::optional<Intel> Simulation::intel(int64_t p) const {
    if (!(vision && state.intel && p >= 0 && p < static_cast<int64_t>(state.intel->size()))) return std::nullopt;
    return (*state.intel)[static_cast<size_t>(p)];
}

/// What player `p` sees this moment (nil: no fog on this map).
std::optional<Sight> Simulation::sight(int64_t p) const {
    if (p >= 0 && p < static_cast<int64_t>(sights.size())) return sights[static_cast<size_t>(p)];
    return std::nullopt;
}

std::optional<int64_t> Simulation::owner(int64_t id) const {
    if (const auto i = unitIndex(id)) return state.units[static_cast<size_t>(*i)].owner;
    for (const auto& s : state.structures) {
        if (s.id == id) return s.owner;
    }
    return std::nullopt;
}

/// A target `owner`'s units may pick: its own, or one of the others'
/// it sees.
std::optional<Simulation::Target> Simulation::foe(int64_t id, int64_t owner_) const {
    const auto t = target(id);
    if (!t) return std::nullopt;
    // A buried Scorpion is no target to a side that cannot see it, on a map
    // without fog too (with fog its sight leaves it out).
    if (!vision && t->kind == Unit::Kind::scorpion && !t->structure) {
        if (const auto i = unitIndex(id); i && hiddenFrom(state.units[static_cast<size_t>(*i)], owner_)) return std::nullopt;
    }
    return state.allied(t->owner, owner_) || !(owner_ >= 0 && owner_ < static_cast<int64_t>(sights.size()))
                   || sights[static_cast<size_t>(owner_)].ids.count(id) > 0
               ? t
               : std::nullopt;
}

/// A buried Scorpion `u` is hidden from player `p`: an enemy of its side's
/// that has no unit or building within `Rules.revealRange` of it (1 cell with
/// Deep burrow, once driven), no Sentinel seeing it and no strike or reload
/// to give it away (`Vision.hides`). The same on every map, with fog or not.
bool Simulation::hiddenFrom(const Unit& u, int64_t p) const {
    if (!(u.kind == Unit::Kind::scorpion && u.burrowed() && u.hp > 0 && state.hostile(u.owner, p))) return false;
    double range = Rules::revealRange;
    if (pilot) {
        const Boost r = boost(Stat::RevealRange{}, u);
        if (r != Boost::none) range = max(0.0, Rules::revealRange + r.plus);
    }
    return Vision::hides(u, p, state, range);
}

/// The buried Scorpions hidden from player `p` now, by id (the fog's sight
/// holds them out already; this is for a map without it).
std::set<int64_t> Simulation::hiddenScorpions(int64_t p) const {
    std::set<int64_t> out;
    for (const Unit& u : state.units) {
        if (u.kind == Unit::Kind::scorpion && hiddenFrom(u, p)) out.insert(u.id);
    }
    return out;
}

/// The game without the buried Scorpions player `p` cannot see (a map
/// without fog: otherwise the whole game).
GameState Simulation::withoutHidden(int64_t p) const {
    GameState s = state;
    const std::set<int64_t> hid = hiddenScorpions(p);
    if (!hid.empty()) std::erase_if(s.units, [&](const Unit& u) { return hid.count(u.id) > 0; });
    return s;
}

/// Look at once, for a game set up by hand (units moved, no step yet).
void Simulation::lookNow() {
    reindex();
    look();
}

/// Work out what every player sees, and update what each knows.
void Simulation::look() {
    if (!vision) { sights.clear(); return; }
    std::vector<Intel> intel_ = state.intel ? *state.intel : std::vector<Intel>{};
    const int64_t cells = vision->width * vision->height;
    if (intel_.size() != state.players.size()) {
        // A new game: each knows where the others start, and that a
        // Citadel stands there (the start locations are known).
        intel_.clear();
        for (size_t p = 0; p < state.players.size(); ++p) {
            Intel k;
            for (const auto& s : state.structures) {
                if (state.hostile(s.owner, static_cast<int64_t>(p)) && s.kind == Structure::Kind::citadel) k.buildings.push_back(s);
            }
            k.explored.assign(static_cast<size_t>((cells + 63) / 64), 0);
            k.sites.assign(sites.size(), -std::numeric_limits<double>::infinity());
            intel_.push_back(k);
        }
    }
    // Spotter: units whose picks let them see farther; Deep burrow: a
    // Scorpion that is seen from closer in only.
    std::map<int64_t, double> farther, reveal;
    if (pilot) {
        for (const auto& u : state.units) {
            const Boost b = boost(Stat::Sight{}, u);
            if (b != Boost::none) farther[u.id] = b.apply(Rules::vision(u.kind));
            if (u.kind == Unit::Kind::scorpion) {
                const Boost r = boost(Stat::RevealRange{}, u);
                if (r != Boost::none) reveal[u.id] = max(0.0, Rules::revealRange + r.plus);
            }
        }
    }
    sights.clear();
    const double now = state.time;
    // Allies see alike (C++, teams): worked out once a team, by its first
    // player; the others take that sight and the ground it has explored.
    std::map<int64_t, size_t> firstOfTeam;
    for (size_t p = 0; p < state.players.size(); ++p) {
        const int64_t pl = static_cast<int64_t>(p);
        Intel k = intel_[p];
        const auto first = firstOfTeam.find(state.team(pl));
        if (first == firstOfTeam.end()) {
            firstOfTeam[state.team(pl)] = p;
            sights.push_back(vision->sight(pl, state, k.explored, farther, reveal));
        } else {
            Sight shared = sights[first->second];
            sights.push_back(std::move(shared));
            const std::vector<uint64_t>& theirs = intel_[first->second].explored;
            if (k.explored.size() != theirs.size()) k.explored.assign(theirs.size(), 0);
            for (size_t w = 0; w < theirs.size(); ++w) k.explored[w] |= theirs[w];
        }
        const Sight& s = sights.back();
        if (k.sites.size() != sites.size()) k.sites.assign(sites.size(), -std::numeric_limits<double>::infinity());
        for (size_t j = 0; j < sites.size(); ++j) {
            const auto i = vision->index(sites[j]);
            if (i && s.seen[static_cast<size_t>(*i)]) k.sites[j] = now;
        }
        // Buildings: the ones in sight as they are now; the rest as
        // last seen, unless their ground is seen empty.
        std::vector<Structure> shownNow;
        for (const auto& b : state.structures) {
            if (state.hostile(b.owner, pl) && s.ids.count(b.id)) shownNow.push_back(b);
        }
        std::set<int64_t> shownIDs;
        for (const auto& b : shownNow) shownIDs.insert(b.id);
        std::vector<Structure> buildings;
        for (const auto& b : k.buildings) {
            if (!shownIDs.count(b.id) && !vision->sees(b, s.seen)) buildings.push_back(b);
        }
        buildings.insert(buildings.end(), shownNow.begin(), shownNow.end());
        // (Swift's `sort` is not stable either; ids are unique.)
        std::sort(buildings.begin(), buildings.end(), [](const Structure& a, const Structure& b) { return a.id < b.id; });
        k.buildings = buildings;
        // Units: the same, and a unit out of sight this long is gone
        // from mind.
        std::vector<Intel::Seen> units;
        for (const auto& m : k.units) {
            if (!(!s.ids.count(m.unit.id) && now - m.at <= Rules::memory)) continue;
            const auto i = vision->index(m.unit.position);
            if (!i) { units.push_back(m); continue; }
            if (!(m.unit.stats().air ? s.near[static_cast<size_t>(*i)] : s.seen[static_cast<size_t>(*i)])) units.push_back(m);
        }
        for (const auto& u : state.units) {
            if (state.hostile(u.owner, pl) && s.ids.count(u.id)) units.push_back(Intel::Seen{u, now});
        }
        std::sort(units.begin(), units.end(), [](const Intel::Seen& a, const Intel::Seen& b) { return a.unit.id < b.unit.id; });
        k.units = units;
        intel_[p] = k;
    }
    state.intel = intel_;
}

/// The game as player `p` knows it: its own units and buildings, the
/// others' it sees, and the rest of theirs as it last saw them. Its AI
/// plays from this (`seen(by:)`), and its humans' screens show it.
GameState Simulation::known(int64_t p) const {
    if (!(vision && state.intel && p >= 0 && p < static_cast<int64_t>(state.intel->size())
          && p < static_cast<int64_t>(sights.size()))) return withoutHidden(p);
    const std::set<int64_t>& ids = sights[static_cast<size_t>(p)].ids;
    const Intel& k = (*state.intel)[static_cast<size_t>(p)];
    GameState s = state;
    s.units.clear();
    for (const auto& u : state.units) {
        if (state.allied(u.owner, p) || ids.count(u.id)) s.units.push_back(u);
    }
    for (const auto& m : k.units) {
        if (!ids.count(m.unit.id)) s.units.push_back(m.unit);
    }
    s.structures.clear();
    for (const auto& b : state.structures) {
        if (state.allied(b.owner, p) || ids.count(b.id)) s.structures.push_back(b);
    }
    for (const auto& b : k.buildings) {
        if (!ids.count(b.id)) s.structures.push_back(b);
    }
    return s;
}

/// The game as player `p`'s humans see it on screen: as `known(by:)`,
/// but only the others' units in sight now (no memory of units, only
/// of buildings).
GameState Simulation::shown(int64_t p) const {
    if (!(vision && state.intel && p >= 0 && p < static_cast<int64_t>(state.intel->size())
          && p < static_cast<int64_t>(sights.size()))) return withoutHidden(p);
    const std::set<int64_t>& ids = sights[static_cast<size_t>(p)].ids;
    const Intel& k = (*state.intel)[static_cast<size_t>(p)];
    GameState s = state;
    s.units.clear();
    for (const auto& u : state.units) {
        if (state.allied(u.owner, p) || ids.count(u.id)) s.units.push_back(u);
    }
    s.structures.clear();
    for (const auto& b : state.structures) {
        if (state.allied(b.owner, p) || ids.count(b.id)) s.structures.push_back(b);
    }
    for (const auto& b : k.buildings) {
        if (!ids.count(b.id)) s.structures.push_back(b);
    }
    return s;
}

/// The others' units and buildings player `p` does not see now (none
/// without fog).
std::set<int64_t> Simulation::unseen(int64_t p) const {
    if (!(vision && p >= 0 && p < static_cast<int64_t>(sights.size()))) return hiddenScorpions(p);
    const std::set<int64_t>& ids = sights[static_cast<size_t>(p)].ids;
    std::set<int64_t> out;
    for (const auto& u : state.units) {
        if (state.hostile(u.owner, p) && !ids.count(u.id)) out.insert(u.id);
    }
    for (const auto& b : state.structures) {
        if (state.hostile(b.owner, p) && !ids.count(b.id)) out.insert(b.id);
    }
    if (state.intel && p < static_cast<int64_t>(state.intel->size())) {
        for (const auto& b : (*state.intel)[static_cast<size_t>(p)].buildings) {
            if (!ids.count(b.id)) out.insert(b.id);
        }
    }
    return out;
}

/// This simulation as player `p` knows the game (`known(by:)`), for its
/// AI to decide from. Orders it gives are applied to the real one.
Simulation Simulation::seen(int64_t p) const {
    if (!vision) {
        if (hiddenScorpions(p).empty()) return *this;
        Simulation v = *this;
        v.state = withoutHidden(p);
        v.reindex();
        return v;
    }
    Simulation v = *this;
    v.state = known(p);
    v.reindex();
    return v;
}

/// When player `p` last saw base site `i` (-inf: never; now on a map
/// without fog).
double Simulation::lastSeen(int64_t i, int64_t p) const {
    if (!(vision && state.intel && p >= 0 && p < static_cast<int64_t>(state.intel->size()))) return state.time;
    const Intel& k = (*state.intel)[static_cast<size_t>(p)];
    if (!(i >= 0 && i < static_cast<int64_t>(k.sites.size()))) return state.time;
    return k.sites[static_cast<size_t>(i)];
}

} // namespace ac
