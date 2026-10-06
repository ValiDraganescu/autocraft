// The army under All in must not gather mid-attack: the gather point is
// the soldier at the army's middle, behind the runners, so each gathering
// order sent the front ones back (two steps on, two steps back). Swift has
// the same dithering; C++ gates it on the stance (PORTING.md).
#include "test.h"

#include "Commander.h"
#include "MapLibrary.h"
#include "ScreenConfig.h"
#include "Simulation.h"

#include <vector>

using namespace ac;

namespace {

/// An attack on red's main under way (`a`), blue's 20 Rangers strung out
/// on the way with no enemy soldier about and none firing.
std::vector<Command> armyOrders(Stance stance) {
    const ScreenConfig screens({
        {"1552-41055-1", "Built-in", 0, 0, 1728, 1117},
        {"19501-29870-2", "Odyssey", -1652, 1117, 5120, 1440},
    });
    const CanvasProjection proj(screens.canvasWidth, screens.canvasHeight);
    const MapDefinition m = MapLibrary::map(screens, proj);
    GameState s = GameState::new_(m);
    std::erase_if(s.units, [](const Unit& u) { return u.owner != 0 || !u.soldier(); });
    Vec2 blue = Vec2::zero, red = Vec2::zero;
    for (const auto& b : s.structures) {
        if (b.kind == Structure::Kind::citadel) { (b.owner == 0 ? blue : red) = b.position; }
    }
    s.units.clear();
    const Vec2 dir = normalize(red - blue);
    for (int k = 0; k < 20; k++) {
        Unit u(s.nextID, Unit::Kind::ranger, 0, blue + dir * (14.0 + 3.5 * k), 0, Unit::Task::attackMove);
        s.units.push_back(u);
        s.nextID += 1;
    }
    Directives d;
    d.stance = stance;
    s.players[0].directives = d;
    s.players[0].attack = red;
    s.players[0].retreating = false;
    const Simulation sim(s, m);
    return Commander(m).army(sim);
}

} // namespace

TEST(allInNeverGathersMidAttack) {
    EXPECT_TRUE(armyOrders(Stance::allIn).empty());
}

TEST(autoStanceStillGathersAStrungOutArmy) {
    EXPECT_TRUE(!armyOrders(Stance::auto_).empty());
}
