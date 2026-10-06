// Port of VisionTests.swift: the fog of war, the same for every player:
// what each side sees, what it remembers, and that its units and its AI act
// only on that. Hand-built positions and a few seconds of simulation each,
// never a game.
#include "VisionHelpers.h"

#include "NavGrid.h"
#include "TerrainField.h"
#include "Vision.h"

#include <algorithm>
#include <cmath>

using namespace ac;
using visiontest::add;
using visiontest::homeMap;

namespace {

/// Both Citadels, nothing else, at minute 10.
GameState field() {
    GameState s = GameState::new_(homeMap());
    s.units.clear();
    s.time = 600;
    return s;
}

std::vector<GameEvent> run(Simulation& sim, double seconds) {
    std::vector<GameEvent> events;
    for (int k = 0; k < static_cast<int>(seconds * 30); ++k) {
        for (auto& e : sim.step(1.0 / 30)) events.push_back(e);
    }
    return events;
}

Vec2 citadel(const GameState& s, int64_t owner) {
    for (const auto& b : s.structures) if (b.owner == owner) return b.position;
    return Vec2::zero;
}

struct Cliff { Vec2 low, high; bool found; };

/// A walkable spot under a cliff and one on top of it, `gap` apart
/// across the cliff, well away from any ramp and both Citadels.
Cliff cliff(const MapDefinition& m, double gap = 6) {
    const Simulation sim(GameState::new_(m), m);
    const NavGrid& nav = *sim.nav;
    const Vision& v = *sim.vision;
    for (const auto& plateau : m.plateaus) {
        if (!(plateau.level > 0)) continue;
        const auto& poly = plateau.polygon;
        for (size_t i = 0; i < poly.size(); ++i) {
            const Vec2 a = poly[i], b = poly[(i + 1) % poly.size()];
            for (int j = 0; j <= 6; ++j) {
                const double t = 0.2 + static_cast<double>(j) * 0.1;
                const Vec2 e = a + (b - a) * t;
                Vec2 n = normalize(Vec2(-(b - a).y, (b - a).x));
                if (signedDistance(e + n, poly) < 0) n = -n;
                const Vec2 low = e + n * gap / 2, high = e - n * gap / 2;
                if (!(nav.walkable(low) && nav.walkable(high))) continue;
                const auto li = v.index(low), hi = v.index(high);
                if (!(li && hi && v.tier[static_cast<size_t>(*hi)] > v.tier[static_cast<size_t>(*li)])) continue;
                if (!std::all_of(m.ramps.begin(), m.ramps.end(),
                                 [&](const Ramp& r) { return distance(r.low, e) > 8 && distance(r.high, e) > 8; })) continue;
                if (!std::all_of(sim.state.structures.begin(), sim.state.structures.end(),
                                 [&](const Structure& s) { return distance(s.position, e) > 14; })) continue;
                return Cliff{low, high, true};
            }
        }
    }
    return Cliff{Vec2::zero, Vec2::zero, false};
}

std::vector<int64_t> shooters(const std::vector<GameEvent>& events) {
    std::vector<int64_t> out;
    for (const auto& e : events) if (const auto* s = e.as<GameEvent::Shot>()) out.push_back(s->unit);
    return out;
}

bool contains(const std::vector<int64_t>& v, int64_t x) { return std::find(v.begin(), v.end(), x) != v.end(); }

} // namespace

/// High ground hides what stands on it: a Ranger below the cliff does
/// not see an enemy Ranger 6 cells off on top, and never shoots it;
/// the one on top sees and shoots the one below.
TEST(vision_highGroundHidesWhatIsOnIt) {
    GameState s = field();
    const MapDefinition& m = homeMap();
    const Cliff c = cliff(m);
    ASSERT_TRUE(c.found);
    const int64_t below = add(s, Unit::Kind::ranger, c.low, 0);
    const int64_t above = add(s, Unit::Kind::ranger, c.high, 1);
    Simulation sim(s, m);
    const auto events = run(sim, 1.5);
    EXPECT_TRUE(!sim.sees(0, above)); // the cliff hides the Ranger on top
    EXPECT_TRUE(sim.sees(1, below));  // the Ranger on top sees down
    const auto shots = shooters(events);
    EXPECT_TRUE(!contains(shots, below)); // nothing to shoot at that it can see
    EXPECT_TRUE(contains(shots, above));
}

/// The Ranger below, out with its army (attacking somewhere else) and
/// shot by what it cannot see, goes for where the shots come from
/// instead of walking on.
TEST(vision_shotFromTheFogItGoesForTheShooter) {
    GameState s = field();
    const MapDefinition& m = homeMap();
    const Cliff c = cliff(m);
    ASSERT_TRUE(c.found);
    const int64_t below = add(s, Unit::Kind::ranger, c.low, 0);
    s.units.back().task = Unit::Task::attackMove;
    s.players[0].attack = c.low + normalize(c.low - c.high) * 30;
    add(s, Unit::Kind::ranger, c.high, 1);
    Simulation sim(s, m);
    run(sim, 1.5);
    const auto u = sim.state.unit(below);
    ASSERT_TRUE(u.has_value());
    EXPECT_TRUE(u->shotFrom == c.high); // it knows where the shots came from
    EXPECT_TRUE(u->task == Unit::Task::attacking); // and heads there
}

/// The air sees every level: a Dropship over the low ground sees the
/// Ranger on top of the cliff.
TEST(vision_theAirSeesUpCliffs) {
    GameState s = field();
    const MapDefinition& m = homeMap();
    const Cliff c = cliff(m);
    ASSERT_TRUE(c.found);
    add(s, Unit::Kind::dropship, c.low, 0);
    const int64_t above = add(s, Unit::Kind::ranger, c.high, 1);
    Simulation sim(s, m);
    run(sim, 0.2);
    EXPECT_TRUE(sim.sees(0, above));
}

/// Out of sight is unknown: a red Ranger 20 cells from anything blue is
/// not in blue's view of the game, nor its AI's; blue sees its own and
/// knows only red's start Citadel.
TEST(vision_whatASideDoesNotSeeItDoesNotKnow) {
    GameState s = field();
    const MapDefinition& m = homeMap();
    const Vec2 blue = citadel(s, 0), red = citadel(s, 1);
    const int64_t lone = add(s, Unit::Kind::ranger, red + normalize(blue - red) * 6, 1);
    Simulation sim(s, m);
    run(sim, 0.2);
    const GameState known = sim.known(0);
    EXPECT_TRUE(!known.unit(lone).has_value());
    EXPECT_TRUE(!sim.seen(0).state.unit(lone).has_value()); // nor its AI's
    std::vector<Structure::Kind> red_;
    for (const auto& b : known.structures) if (b.owner == 1) red_.push_back(b.kind);
    EXPECT_TRUE(red_ == std::vector<Structure::Kind>{Structure::Kind::citadel}); // only where red starts
    EXPECT_TRUE(sim.known(1).unit(lone).has_value()); // red knows its own
}

/// A building seen is remembered as it was: out of sight it stays in
/// the side's view (destroyed meanwhile, too) until its ground is seen
/// again; then it is forgotten.
TEST(vision_buildingsAreRememberedUntilSeenGone) {
    GameState s = field();
    const MapDefinition& m = homeMap();
    const Vec2 blue = citadel(s, 0), red = citadel(s, 1);
    const Vec2 at = blue + normalize(red - blue) * 18;
    const int64_t habDome = add(s, Structure::Kind::habDome, at, 1);
    const int64_t scout = add(s, Unit::Kind::ranger, at + Vec2(0, 4), 0);
    Simulation sim(s, m);
    run(sim, 0.2);
    EXPECT_TRUE(sim.sees(0, habDome));
    // The scout walks away; the Hab Dome dies out of sight.
    auto& us = sim.state.units;
    us.erase(std::remove_if(us.begin(), us.end(), [&](const Unit& u) { return u.id == scout; }), us.end());
    run(sim, 0.2);
    EXPECT_TRUE(!sim.sees(0, habDome));
    auto& bs = sim.state.structures;
    bs.erase(std::remove_if(bs.begin(), bs.end(), [&](const Structure& b) { return b.id == habDome; }), bs.end());
    run(sim, 0.2);
    EXPECT_TRUE(sim.known(0).structure(habDome).has_value()); // remembered as last seen
    EXPECT_TRUE(sim.unseen(0).count(habDome) > 0);
    // Back to look: gone.
    add(sim.state, Unit::Kind::ranger, at + Vec2(0, 4), 0);
    run(sim, 0.2);
    EXPECT_TRUE(!sim.known(0).structure(habDome).has_value()); // seen empty, forgotten
}

/// A unit lost from sight is kept in mind for `Rules.memory` seconds,
/// where it was last seen, unless that spot is seen empty.
TEST(vision_unitsAreRememberedForAWhile) {
    GameState s = field();
    const MapDefinition& m = homeMap();
    const Vec2 blue = citadel(s, 0), red = citadel(s, 1);
    const Vec2 at = blue + normalize(red - blue) * 18;
    const int64_t foe = add(s, Unit::Kind::ranger, at, 1);
    const int64_t scout = add(s, Unit::Kind::ranger, at + Vec2(0, 3), 0);
    Simulation sim(s, m);
    run(sim, 0.2);
    auto& us = sim.state.units;
    us.erase(std::remove_if(us.begin(), us.end(), [&](const Unit& u) { return u.id == scout; }), us.end());
    run(sim, 0.2);
    const auto remembered = sim.known(0).unit(foe);
    EXPECT_TRUE(remembered && remembered->position == at); // in mind where last seen
    EXPECT_TRUE(!sim.shown(0).unit(foe).has_value()); // but not shown on screen
    sim.state.time += Rules::memory + 1;
    run(sim, 0.2);
    EXPECT_TRUE(!sim.known(0).unit(foe).has_value()); // forgotten after a while
}

/// A ground unit at the watchtower sees far round it.
TEST(vision_watchtowerSeesFar) {
    GameState s = field();
    const MapDefinition& m = homeMap();
    const Doodad* towerDoodad = nullptr;
    for (const auto& d : m.doodads) if (d.kind == Doodad::Kind::tower) { towerDoodad = &d; break; }
    ASSERT_TRUE(towerDoodad != nullptr);
    const Vec2 tower = towerDoodad->position;
    const Simulation sim0(s, m);
    ASSERT_TRUE(sim0.nav.has_value());
    const NavGrid& nav = *sim0.nav;
    std::optional<Vec2> spot;
    for (int k = 0; k < 21; ++k) {
        const double a = static_cast<double>(k) * 0.3;
        const Vec2 p = tower + Vec2(std::cos(a), std::sin(a)) * 2.2;
        if (nav.walkable(p)) { spot = p; break; }
    }
    ASSERT_TRUE(spot.has_value());
    std::optional<Vec2> far;
    for (const Vec2 d : {Vec2(15, 0), Vec2(-15, 0), Vec2(0, 15), Vec2(0, -15)}) {
        const Vec2 p = tower + d;
        if (nav.walkable(p) && sim0.vision->tierAt(p) <= sim0.vision->tierAt(tower)) { far = p; break; }
    }
    ASSERT_TRUE(far.has_value());
    add(s, Unit::Kind::ranger, *spot, 0);
    const int64_t foe = add(s, Unit::Kind::ranger, *far, 1);
    Simulation sim(s, m);
    run(sim, 0.2);
    EXPECT_TRUE(sim.sees(0, foe)); // the tower's sight reaches 15 cells off
}

/// On the mirrored map each side's start looks the same: blue's sight
/// is red's mirrored, cell for cell.
TEST(vision_sightIsAMirror) {
    const MapDefinition& m = homeMap();
    ASSERT_TRUE(m.mirrorX.has_value());
    const double axis = *m.mirrorX;
    const Simulation sim(GameState::new_(m), m);
    ASSERT_TRUE(sim.vision.has_value());
    const Vision& v = *sim.vision;
    const auto sa = sim.sight(0), sb = sim.sight(1);
    ASSERT_TRUE(sa && sb);
    const auto& a = sa->seen;
    const auto& b = sb->seen;
    int64_t mismatches = 0, seen = 0;
    for (int64_t z = 0; z < v.height; ++z) {
        for (int64_t x = 0; x < v.width; ++x) {
            if (!a[static_cast<size_t>(z * v.width + x)]) continue;
            seen += 1;
            const Vec2 p = v.origin + Vec2(static_cast<double>(x) + 0.5, static_cast<double>(z) + 0.5) * Vision::cell;
            const auto j = v.index(Vec2(2 * axis - p.x, p.y));
            if (!j) { mismatches += 1; continue; }
            if (!b[static_cast<size_t>(*j)]) mismatches += 1;
        }
    }
    EXPECT_TRUE(seen > 100);
    EXPECT_EQ(mismatches, int64_t(0));
    EXPECT_EQ(std::count(a.begin(), a.end(), uint8_t(1)), std::count(b.begin(), b.end(), uint8_t(1)));
}
