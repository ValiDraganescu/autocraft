// The AI's defences face the enemy (`Commander::frontDefence`): Bastions on
// open ground in front of the base, toward the enemy, never wedged between
// buildings; Sentinels by the ore line on the side the enemy's air comes
// from, and a forward one at heavy air. Hand-built scenes, one `orders` call
// each, never a game.
#include "test.h"

#include "Commander.h"
#include "MapLibrary.h"
#include "ScreenConfig.h"
#include "Simulation.h"

#include <cmath>
#include <vector>

using namespace ac;

namespace {

const MapDefinition& homeMap() {
    static const MapDefinition m = [] {
        const ScreenConfig screens({
            {"1552-41055-1", "Built-in", 0, 0, 1728, 1117},
            {"19501-29870-2", "Odyssey", -1652, 1117, 5120, 1440},
        });
        const CanvasProjection proj(screens.canvasWidth, screens.canvasHeight);
        return MapLibrary::map(screens, proj);
    }();
    return m;
}

int64_t put(GameState& s, Structure::Kind kind, Vec2 p, int64_t owner) {
    const int64_t id = s.nextID;
    s.structures.push_back(Structure(id, kind, owner, p));
    s.nextID += 1;
    return id;
}

/// The angle between two unit vectors, radians.
double angleBetween(Vec2 a, Vec2 b) { return std::acos(std::max(-1.0, std::min(1.0, dot(a, b)))); }

/// Nothing within 4.5 of `p`; no building within 9 of it ahead along `dir`.
bool inFrontOfBuildings(const GameState& s, Vec2 p, Vec2 dir) {
    for (const Structure& b : s.structures) {
        if (distance(b.position, p) < 4.5) return false;
        if (distance(b.position, p) < 9 && dot(b.position - p, dir) > 1.0) return false;
    }
    return true;
}

/// A fresh two-player game with each side's Hab Dome and Garrison set up
/// like a mid-game base.
GameState basesUp(const MapDefinition& m) {
    GameState s = GameState::new_(m);
    for (int64_t p = 0; p < 2; p++) {
        const Commander c(m, p);
        const Structure citadel = *std::find_if(s.structures.begin(), s.structures.end(), [&](const Structure& b) {
            return b.kind == Structure::Kind::citadel && b.owner == p;
        });
        if (auto d = c.habDomeSpot(s, citadel)) put(s, Structure::Kind::habDome, *d, p);
        if (auto g = c.garrisonSpot(s, citadel)) put(s, Structure::Kind::garrison, *g, p);
    }
    return s;
}

} // namespace

/// A Bastion stands within 60 degrees of the way to the enemy, in front of
/// the buildings, not between them, for each side of the map.
TEST(aiDefence_bastionFacesTheEnemyOnOpenGround) {
    const MapDefinition& m = homeMap();
    const GameState s = basesUp(m);
    const Simulation sim(s, m);
    int found = 0;
    for (int64_t p = 0; p < 2; p++) {
        const Commander c(m, p);
        ASSERT_TRUE(c.frontDefence);
        for (const Structure& citadel : s.structures) {
            if (citadel.kind != Structure::Kind::citadel || citadel.owner != p) continue;
            const auto spot = c.bastionSpot(s, sim, citadel);
            if (!spot) continue;
            found++;
            const auto way = c.router.route(citadel.position, c.start(s, 1 - p));
            ASSERT_TRUE(!way.empty());
            const Vec2 dir = normalize(way.front() - citadel.position);
            EXPECT_TRUE(angleBetween(normalize(*spot - citadel.position), dir) <= 1.06);
            EXPECT_TRUE(inFrontOfBuildings(s, *spot, normalize(*spot - citadel.position)));
            EXPECT_TRUE(dot(*spot - citadel.position, dir) > 0);
        }
    }
    EXPECT_TRUE(found >= 1);
}

/// Buildings piled on the enemy side of the Citadel leave no front: no
/// Bastion is placed rather than one behind or between them.
TEST(aiDefence_noOpenFrontNoBastion) {
    const MapDefinition& m = homeMap();
    GameState s = basesUp(m);
    const Simulation sim0(s, m);
    const Commander c(m, 0);
    const Structure citadel = *std::find_if(s.structures.begin(), s.structures.end(), [](const Structure& b) {
        return b.kind == Structure::Kind::citadel && b.owner == 0;
    });
    const auto way = c.router.route(citadel.position, c.start(s, 1));
    const Vec2 dir = normalize(way.front() - citadel.position);
    // A wall of buildings across the whole front, 3 to 7 cells out.
    const Vec2 side(-dir.y, dir.x);
    for (double r : {3.5, 6.0}) {
        for (int k = -4; k <= 4; k++) put(s, Structure::Kind::habDome, citadel.position + dir * r + side * (2.5 * k), 0);
    }
    const Simulation sim(s, m);
    const auto spot = c.bastionSpot(s, sim, citadel);
    if (spot) EXPECT_TRUE(inFrontOfBuildings(s, *spot, normalize(*spot - citadel.position)));
    EXPECT_TRUE(!spot.has_value() || dot(*spot - citadel.position, dir) > 0);
}

/// The Sentinel stays within reach of the ore line's middle, and sits on the
/// side of it toward the enemy more than the old placement did.
TEST(aiDefence_sentinelCoversOreLineOnTheEnemySide) {
    const MapDefinition& m = homeMap();
    const GameState s = basesUp(m);
    for (int64_t p = 0; p < 2; p++) {
        Commander c(m, p);
        Commander old(m, p);
        old.frontDefence = false;
        for (const Structure& citadel : s.structures) {
            if (citadel.kind != Structure::Kind::citadel || citadel.owner != p) continue;
            const auto air = c.airDirection(s, citadel);
            ASSERT_TRUE(air.has_value());
            EXPECT_TRUE(distance(*air, normalize(c.start(s, 1 - p) - citadel.position)) < 1e-9);
            Vec2 sum = Vec2::zero;
            int n = 0;
            for (const OreDeposit& d : s.patches) {
                if (distance(d.position, citadel.position) < 10) { sum = sum + d.position; n++; }
            }
            if (n == 0) continue;
            const Vec2 mid = sum / static_cast<double>(n);
            const auto now = c.sentinelSpot(s, citadel);
            const auto was = old.sentinelSpot(s, citadel);
            ASSERT_TRUE(now.has_value() && was.has_value());
            EXPECT_TRUE(distance(*now, mid) <= 6.5);
            EXPECT_TRUE(dot(*now - mid, *air) >= dot(*was - mid, *air) - 1e-9);
        }
    }
}

/// A forward Sentinel stands 5 to 11.5 cells out toward the enemy, within 52
/// degrees of its air approach, in front of the buildings.
TEST(aiDefence_forwardSentinelFacesTheEnemyAir) {
    const MapDefinition& m = homeMap();
    const GameState s = basesUp(m);
    int found = 0;
    for (int64_t p = 0; p < 2; p++) {
        const Commander c(m, p);
        for (const Structure& citadel : s.structures) {
            if (citadel.kind != Structure::Kind::citadel || citadel.owner != p) continue;
            const auto spot = c.forwardSentinelSpot(s, citadel);
            if (!spot) continue;
            found++;
            const Vec2 air = *c.airDirection(s, citadel);
            const double r = distance(*spot, citadel.position);
            EXPECT_TRUE(r >= 4.99 && r <= 11.51);
            EXPECT_TRUE(angleBetween(normalize(*spot - citadel.position), air) <= 0.91);
            EXPECT_TRUE(inFrontOfBuildings(s, *spot, normalize(*spot - citadel.position)));
        }
    }
    EXPECT_TRUE(found >= 1);
}

/// Heavy enemy air (9 supply of Kestrels): the AI orders its ore-line
/// Sentinel first, then a second, forward one.
TEST(aiDefence_heavyAirGetsASecondForwardSentinel) {
    const MapDefinition& m = homeMap();
    GameState s = basesUp(m);
    const Commander c(m, 0);
    const Structure citadel = *std::find_if(s.structures.begin(), s.structures.end(), [](const Structure& b) {
        return b.kind == Structure::Kind::citadel && b.owner == 0;
    });
    s.players[0].ore = 3000;
    s.players[0].hydrogen = 1000;
    for (int k = 0; k < 9; k++) {
        const int64_t id = s.nextID++;
        s.units.push_back(Unit(id, Unit::Kind::kestrel, 1, citadel.position + Vec2(0, 12), 0, Unit::Task::idle));
    }
    auto sentinelOrder = [&](const GameState& t) -> std::optional<Vec2> {
        Simulation sim(t, m);
        sim.state.players[0].supplyCap = Rules::maxSupply;
        for (const Command& cmd : c.orders(sim)) {
            if (auto b = cmd.as<Command::Build>(); b && b->kind == Structure::Kind::sentinel) return b->at;
        }
        return std::nullopt;
    };
    const auto first = sentinelOrder(s);
    ASSERT_TRUE(first.has_value());
    put(s, Structure::Kind::sentinel, *first, 0);
    const auto second = sentinelOrder(s);
    ASSERT_TRUE(second.has_value());
    EXPECT_TRUE(distance(*second, *first) >= 5);
    const auto fwd = c.forwardSentinelSpot(s, citadel, {*first});
    if (fwd) { EXPECT_TRUE(distance(*second, *fwd) < 1e-9); }
}
