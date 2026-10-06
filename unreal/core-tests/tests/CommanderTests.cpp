// Port of Tests/GameCoreTests/CommanderTests.swift: short, hand-built
// scenarios for the AI's tech, army mix and tactics: one `orders` call (or a
// few seconds of simulation) each, never a game. And the Commander golden
// (`GoldenCommanderTests.swift`).
#include "test.h"
#include "golden.h"

#include "Commander.h"
#include "MapLibrary.h"
#include "ScreenConfig.h"
#include "Session.h"
#include "Simulation.h"
#include "WindowMaps.h"


#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace ac;

namespace {

/// The handcrafted map for the machine it was drawn for (`map()` of the
/// Swift tests).
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

/// A new game with only blue on the map.
GameState blueOnly(GameState s) {
    std::erase_if(s.units, [](const Unit& u) { return u.owner != 0; });
    std::erase_if(s.structures, [](const Structure& b) { return b.owner != 0; });
    return s;
}

/// Add a finished building; returns its id.
int64_t add(GameState& s, Structure::Kind kind, Vec2 p, int64_t owner = 0) {
    const int64_t id = s.nextID;
    s.structures.push_back(Structure(id, kind, owner, p));
    s.nextID += 1;
    return id;
}

/// Add a unit standing idle; returns its id.
int64_t add(GameState& s, Unit::Kind kind, Vec2 p, int64_t owner = 0) {
    const int64_t id = s.nextID;
    Unit u(id, kind, owner, p, 0, Unit::Task::idle);
    if (kind == Unit::Kind::dropship) { u.energy = 100; u.cargo = std::vector<int64_t>{}; }
    if (kind == Unit::Kind::longbow) { u.anchor = 0; u.anchored = false; }
    s.units.push_back(u);
    s.nextID += 1;
    return id;
}

size_t indexOf(const GameState& s, int64_t structureID) {
    for (size_t i = 0; i < s.structures.size(); i++) if (s.structures[i].id == structureID) return i;
    return s.structures.size();
}

/// Give a building a finished Lab.
void addLab(GameState& s, int64_t id) {
    const size_t i = indexOf(s, id);
    Structure lab(s.nextID, Structure::Kind::lab, s.structures[i].owner, s.structures[i].position + Rules::addonOffset);
    lab.parent = id;
    s.structures[i].addon = lab.id;
    s.structures.push_back(lab);
    s.nextID += 1;
}

struct Tech { GameState state; int64_t garrison; };

/// Blue alone on the handcrafted map, with a Hab Dome, a finished Garrison,
/// both main Derricks, a Comet out, eight Prospectors and money to spend.
Tech blueTech(const MapDefinition& m) {
    GameState s = blueOnly(GameState::new_(m));
    s.players[0].style = Player::Style::bio;
    s.players[0].fireflies = 0;
    s.players[0].drops = false;
    s.players[0].comets = 1;
    s.players[0].greed = 0;
    const Commander c(m);
    const Structure citadel = s.structures[0];
    add(s, Structure::Kind::habDome, *c.habDomeSpot(s, citadel));
    const int64_t garrison = add(s, Structure::Kind::garrison, *c.garrisonSpot(s, citadel));
    for (const Well& g : *s.wells) {
        if (distance(g.position, citadel.position) < 12) add(s, Structure::Kind::derrick, g.position);
    }
    for (int k = 0; k < 7; k++) add(s, Unit::Kind::prospector, citadel.position + Vec2(static_cast<double>(k) - 3, -3.2));
    add(s, Unit::Kind::comet, citadel.position + Vec2(0, 8));
    s.players[0].ore = 1000;
    s.players[0].hydrogen = 500;
    return {s, garrison};
}

struct Built { Structure::Kind kind; Vec2 at; };

std::vector<Built> builds(const std::vector<Command>& orders) {
    std::vector<Built> out;
    for (const Command& c : orders) if (auto b = c.as<Command::Build>()) out.push_back({b->kind, b->at});
    return out;
}

bool contains(const std::vector<Command>& orders, const Command& c) {
    return std::find(orders.begin(), orders.end(), c) != orders.end();
}

/// `.train(structure: b, kind: k?)` for any kind.
bool trains(const std::vector<Command>& orders, int64_t b, std::optional<Unit::Kind> kind = std::nullopt) {
    for (const Command& c : orders) {
        if (auto t = c.as<Command::Train>(); t && t->structure == b && t->kind && (!kind || *t->kind == *kind)) return true;
    }
    return false;
}

const Structure& citadelOf(const GameState& s, int64_t owner) {
    for (const Structure& b : s.structures) if (b.owner == owner && b.kind == Structure::Kind::citadel) return b;
    return s.structures.front();
}

/// Red's main with Prospectors at work on its ore line; returns red's
/// Citadel.
Structure redAtWork(GameState& s) {
    const Structure red = citadelOf(s, 1);
    int n = 0;
    const auto patches = s.patches;
    for (const OreDeposit& p : patches) {
        if (!(distance(p.position, red.position) < 10)) continue;
        if (n++ >= 4) break;
        add(s, Unit::Kind::prospector, red.position + (p.position - red.position) * 0.8, 1);
    }
    return red;
}

std::optional<int64_t> missionUnit(const Command& c) {
    if (auto m = c.as<Command::Mission>()) return m->unit;
    return std::nullopt;
}

template <class M> bool missionIs(const Command& c, std::optional<int64_t> unit = std::nullopt) {
    auto m = c.as<Command::Mission>();
    return m && m->mission && m->mission->is<M>() && (!unit || m->unit == *unit);
}

} // namespace

/// With a Garrison up and MH in the bank the AI orders a Foundry, at a
/// spot where its Lab then fits on the base's level.
TEST(commanderFoundryGoesWhereItsLabFits) {
    const MapDefinition& m = homeMap();
    const GameState s = blueTech(m).state;
    const Simulation sim(s, m);
    std::optional<Vec2> at;
    for (const Built& b : builds(Commander(m).orders(sim))) if (b.kind == Structure::Kind::foundry) { at = b.at; break; }
    ASSERT_TRUE(at.has_value());
    GameState t = sim.state;
    const int64_t foundry = add(t, Structure::Kind::foundry, *at);
    Simulation built(t, m);
    EXPECT_TRUE(built.issue(Command::Addon{foundry}));
    std::optional<Structure> lab;
    for (const Structure& b : built.state.structures) if (b.kind == Structure::Kind::lab) { lab = b; break; }
    ASSERT_TRUE(lab.has_value());
    const TerrainField field(m);
    const int64_t level = field.level(t.structures[0].position);
    for (Vec2 q : {Vec2(-1, -1), Vec2(1, -1), Vec2(-1, 1), Vec2(1, 1)}) {
        EXPECT_EQ(field.level(lab->position + q * 0.9), level); // the lab stands on the base's level
    }
}

/// Garrison on the main, as many as fit (three with lab room on both
/// sides; more go to the next base), all keep room for a lab.
TEST(commanderEveryGarrisonSpotLeavesRoomForALab) {
    const MapDefinition& m = homeMap();
    GameState s = blueOnly(GameState::new_(m));
    const Commander c(m);
    const Structure citadel = s.structures[0];
    std::vector<int64_t> ids;
    for (int i = 0; i < 5; i++) {
        auto p = c.garrisonSpot(s, citadel);
        if (!p) break;
        ids.push_back(add(s, Structure::Kind::garrison, *p));
    }
    EXPECT_TRUE(ids.size() >= 3);
    s.players[0].ore = 1000;
    s.players[0].hydrogen = 500;
    Simulation sim(s, m);
    for (int64_t id : ids) EXPECT_TRUE(sim.issue(Command::Addon{id})); // Garrison id takes its lab
}

/// A Foundry with a Lab trains a Longbow; without one it trains
/// Fireflies for runbys first.
TEST(commanderFoundryTrainsTanksWithALabAndFirefliesBefore) {
    const MapDefinition& m = homeMap();
    GameState s = blueTech(m).state;
    const Commander c(m);
    const Vec2 spot = *c.garrisonSpot(s, s.structures[0]);
    const int64_t foundry = add(s, Structure::Kind::foundry, spot);
    addLab(s, foundry);
    Simulation sim(s, m);
    const auto orders = c.orders(sim);
    EXPECT_TRUE(contains(orders, Command::Train{foundry, Unit::Kind::longbow}));
    for (const Command& o : orders) sim.issue(o);
    const auto f = sim.state.structure(foundry);
    ASSERT_TRUE(f.has_value());
    EXPECT_TRUE(f->line == std::optional<std::vector<Unit::Kind>>(std::vector<Unit::Kind>{Unit::Kind::longbow}));

    GameState bare = blueTech(m).state;
    bare.players[0].fireflies = 2;
    const int64_t f2 = add(bare, Structure::Kind::foundry, spot);
    const auto early = c.orders(Simulation(bare, m));
    EXPECT_TRUE(contains(early, Command::Train{f2, Unit::Kind::firefly}));
    EXPECT_TRUE(!contains(early, Command::Addon{f2})); // Fireflies first, then the lab
}

/// An idle Garrison Lab with money in the bank researches the
/// Mini gun first, then the Aegis shield.
TEST(commanderGarrisonLabResearchesMinigunFirst) {
    const MapDefinition& m = homeMap();
    Tech t = blueTech(m);
    GameState& s = t.state;
    addLab(s, t.garrison);
    const int64_t lab = *s.structure(t.garrison)->addon;
    const Commander c(m);
    EXPECT_TRUE(contains(c.orders(Simulation(s, m)), Command::Research{lab, Upgrade::minigun}));
    s.players[0].upgrades = std::set<Upgrade>{Upgrade::minigun};
    EXPECT_TRUE(contains(c.orders(Simulation(s, m)), Command::Research{lab, Upgrade::aegisShield}));
}

/// The army grows until its Prospectors and soldiers fill the 200 supply:
/// with 66 Prospectors out, a Garrison still trains at 70 army supply, and
/// stops at 134.
TEST(commanderArmyGrowsToTheSupplyCap) {
    const MapDefinition& m = homeMap();
    Tech t = blueTech(m);
    GameState& s = t.state;
    const Vec2 citadel = s.structures[0].position;
    auto prospectors = [&] {
        return std::count_if(s.units.begin(), s.units.end(), [](const Unit& u) { return u.kind == Unit::Kind::prospector; });
    };
    while (prospectors() < 66) {
        const int64_t k = static_cast<int64_t>(s.units.size());
        add(s, Unit::Kind::prospector, citadel + Vec2(static_cast<double>(k % 11) - 5, -4 - static_cast<double>(k / 11) * 0.8));
    }
    s.players[0].ore = 5000;
    s.players[0].hydrogen = 5000;
    // Supply as if the Hab Domes for all 200 stood.
    auto soldiers = [&](int n) {
        GameState u = s;
        for (int k = 0; k < n; k++) {
            add(u, Unit::Kind::ranger, citadel + Vec2(static_cast<double>(k % 12) - 5.5, 10 + static_cast<double>(k / 12) * 0.8));
        }
        Simulation sim(u, m);
        sim.state.players[0].supplyCap = Rules::maxSupply;
        return sim;
    };
    auto trainsAny = [&](const Simulation& sim) { return trains(Commander(m).orders(sim), t.garrison); };
    EXPECT_TRUE(trainsAny(soldiers(69))); // 70 army supply (the Comet too) is not the cap
    EXPECT_TRUE(!trainsAny(soldiers(133))); // 134 army supply fills the 200
}

/// What it has seen of the enemy bends its mix: Longbows against a
/// big ground army, Juggernauts one to one against armour, Fireflies
/// against light infantry; nothing against nothing.
TEST(commanderCountersAnswerTheEnemyMix) {
    using Mix = Commander::EnemyMix;
    const Commander c(homeMap());
    EXPECT_TRUE(c.counters(Mix{}) == Commander::Counters{});
    const auto rangers = c.counters(Mix{.bio = 30, .light = 30, .total = 30});
    EXPECT_EQ(rangers.longbows, 3);
    EXPECT_EQ(rangers.fireflies, 7);
    EXPECT_TRUE(!rangers.perJuggernaut.has_value());
    const auto armour = c.counters(Mix{.bio = 8, .armored = 20, .light = 4, .total = 24});
    EXPECT_TRUE(armour.perJuggernaut == std::optional<int64_t>(1));
    EXPECT_EQ(armour.fireflies, 0);
    // Seen units are counted by supply, air apart.
    GameState s = GameState::new_(homeMap());
    const Vec2 red = citadelOf(s, 1).position;
    for (int k = 0; k < 4; k++) add(s, Unit::Kind::ranger, red + Vec2(static_cast<double>(k), 6), 1);
    add(s, Unit::Kind::longbow, red + Vec2(0, 8), 1);
    add(s, Unit::Kind::dropship, red + Vec2(2, 8), 1);
    EXPECT_TRUE(c.enemyMix(s) == (Mix{.bio = 4, .armored = 3, .light = 4, .air = 2, .antiAir = 4, .total = 9}));
    // Fliers: Hailstorms and Sentinels; Kestrels while anti-air is short.
    const auto kestrels = c.counters(Mix{.armored = 6, .air = 6, .armedAir = 6, .total = 12});
    EXPECT_EQ(kestrels.hailstorms, 2);
    EXPECT_EQ(kestrels.sentinels, 1);
    EXPECT_EQ(kestrels.kestrels, 2); // nothing of theirs shoots air
    EXPECT_EQ(c.counters(Mix{.armored = 9, .air = 9, .armedAir = 9, .total = 9}).sentinels, 2);
    EXPECT_EQ(c.counters(Mix{.bio = 10, .light = 10, .antiAir = 10, .total = 10}).kestrels, 0); // Rangers shoot air
    EXPECT_EQ(c.counters(Mix{.air = 4, .total = 4}).hailstorms, 1); // Dropships
    EXPECT_EQ(c.counters(Mix{.air = 4, .total = 4}).sentinels, 0);
}

/// A Kestrel seen: the Foundry trains a Hailstorm and a Sentinel goes
/// up by the ore line, off the Prospectors' way.
TEST(commanderAKestrelSeenBringsAntiAir) {
    const MapDefinition& m = homeMap();
    GameState s = blueTech(m).state;
    const Structure citadel = s.structures[0];
    const Commander c(m);
    const int64_t foundry = add(s, Structure::Kind::foundry, *c.garrisonSpot(s, citadel));
    s.players[0].hydrogen = 2000;
    auto orders = [&](const GameState& t) {
        Simulation sim(t, m);
        sim.state.players[0].supplyCap = Rules::maxSupply;
        return c.orders(sim);
    };
    auto hailstorm = [&](const std::vector<Command>& o) { return trains(o, foundry, Unit::Kind::hailstorm); };
    auto sentinel = [](const std::vector<Command>& o) -> std::optional<Vec2> {
        for (const Built& b : builds(o)) if (b.kind == Structure::Kind::sentinel) return b.at;
        return std::nullopt;
    };
    EXPECT_TRUE(!hailstorm(orders(s)));
    EXPECT_TRUE(!sentinel(orders(s)).has_value());
    add(s, Unit::Kind::kestrel, citadel.position + Vec2(0, 12), 1);
    const auto o = orders(s);
    EXPECT_TRUE(hailstorm(o));
    const auto spot = sentinel(o);
    ASSERT_TRUE(spot.has_value()); // no Sentinel
    Vec2 sum = Vec2::zero;
    int n = 0;
    for (const OreDeposit& p : s.patches) {
        if (distance(p.position, citadel.position) < 10) { sum = sum + p.position; n += 1; }
    }
    const Vec2 mid = sum / static_cast<double>(n);
    EXPECT_TRUE(distance(*spot, mid) <= 6.5); // covers the ore line
}

/// Two fresh Kestrels at the rally fly at an enemy ore line with
/// Prospectors on it; one finds a Hailstorm there and pulls out.
TEST(commanderKestrelsRaidAnOreLineAndFleeAntiAir) {
    const MapDefinition& m = homeMap();
    GameState s = GameState::new_(m);
    const Structure red = redAtWork(s);
    const Structure blue = citadelOf(s, 0);
    const int64_t k1 = add(s, Unit::Kind::kestrel, blue.position + Vec2(0, 7));
    const int64_t k2 = add(s, Unit::Kind::kestrel, blue.position + Vec2(1.5, 7));
    const Commander c(m);
    std::set<int64_t> raiding;
    for (const Command& o : c.raiders(Simulation(s, m))) if (missionIs<Mission::Raid>(o)) raiding.insert(*missionUnit(o));
    EXPECT_TRUE(raiding == (std::set<int64_t>{k1, k2}));
    // Out there, a Hailstorm turns up.
    for (size_t i = 0; i < s.units.size(); i++) {
        if (s.units[i].id == k1 || s.units[i].id == k2) {
            s.units[i].mission = Mission::Raid{red.position};
            s.units[i].position = red.position + Vec2(static_cast<double>(i % 2), 3);
        }
    }
    add(s, Unit::Kind::hailstorm, red.position + Vec2(0, 5), 1);
    add(s, Unit::Kind::hailstorm, red.position + Vec2(1, 5), 1);
    const auto back = c.raiders(Simulation(s, m));
    EXPECT_TRUE(std::any_of(back.begin(), back.end(), [&](const Command& o) { return missionIs<Mission::FallBack>(o, k1); }));
}

/// A lone Kestrel over the base, with twice its worth of Rangers
/// about, is hunted by Rangers, not by Fireflies that cannot shoot it.
TEST(commanderHuntersOfAKestrelShootAir) {
    const MapDefinition& m = homeMap();
    GameState s = GameState::new_(m);
    const Vec2 blue = citadelOf(s, 0).position;
    std::set<int64_t> rangers;
    for (int k = 0; k < 12; k++) {
        rangers.insert(add(s, Unit::Kind::ranger, blue + Vec2(static_cast<double>(k % 6) - 2.5, 9 + static_cast<double>(k / 6))));
    }
    const int64_t firefly = add(s, Unit::Kind::firefly, blue + Vec2(0, 7));
    add(s, Unit::Kind::kestrel, blue + Vec2(4, -3), 1);
    const auto hunts = Commander(m).hunters(Simulation(s, m));
    std::set<int64_t> ids;
    for (const Command& o : hunts) if (missionIs<Mission::Hunt>(o)) ids.insert(*missionUnit(o));
    EXPECT_TRUE(!ids.empty());
    EXPECT_TRUE(std::includes(rangers.begin(), rangers.end(), ids.begin(), ids.end()));
    EXPECT_TRUE(!ids.count(firefly));
}

/// A bio AI with its two Longbows out trains more at its Foundry
/// once it has seen a big infantry army.
TEST(commanderLongbowsAnswerAMassOfInfantry) {
    const MapDefinition& m = homeMap();
    GameState s = blueTech(m).state;
    const Structure citadel = s.structures[0];
    const Commander c(m);
    const int64_t foundry = add(s, Structure::Kind::foundry, *c.garrisonSpot(s, citadel));
    addLab(s, foundry);
    for (int k = 0; k < 2; k++) add(s, Unit::Kind::longbow, citadel.position + Vec2(static_cast<double>(k) * 2, 10));
    s.players[0].hydrogen = 2000;
    auto trainsLongbow = [&](const GameState& t) {
        Simulation sim(t, m);
        sim.state.players[0].supplyCap = Rules::maxSupply;
        return trains(c.orders(sim), foundry, Unit::Kind::longbow);
    };
    EXPECT_TRUE(!trainsLongbow(s)); // two is a bio AI's lot
    // In sight of its Longbows.
    for (int k = 0; k < 24; k++) {
        add(s, Unit::Kind::ranger, citadel.position + Vec2(static_cast<double>(k % 6) - 2.5, 14 + static_cast<double>(k / 6) * 0.8), 1);
    }
    EXPECT_TRUE(trainsLongbow(s)); // 24 Rangers seen
}

/// A new attack goes to an unguarded enemy expansion before the
/// guarded main, even a little farther off; with the main bare, to
/// the main.
TEST(commanderAttackGoesForTheLeastGuardedBase) {
    const MapDefinition& m = homeMap();
    GameState s = GameState::new_(m);
    const Vec2 blue = citadelOf(s, 0).position;
    const Vec2 red = citadelOf(s, 1).position;
    const Commander c(m);
    Simulation sim(s, m);
    // Red's expansion: the free site nearest red's main.
    std::optional<Vec2> found;
    for (Vec2 p : sim.sites) {
        if (!(distance(p, red) > 10 && distance(p, blue) > 10)) continue;
        if (!found || distance(p, red) < distance(*found, red)) found = p;
    }
    ASSERT_TRUE(found.has_value());
    const Vec2 site = *found;
    add(s, Structure::Kind::citadel, site, 1);
    sim = Simulation(s, m);
    const Vec2 front = blue + normalize(red - blue) * 8;
    const Vec2 nearer = c.router.distance(front, red) < c.router.distance(front, site) ? red : site;
    EXPECT_TRUE(c.attackTarget(sim, front) == std::optional<Vec2>(nearer)); // bare: the nearer
    // Guard the nearer one.
    for (int k = 0; k < 12; k++) {
        add(s, Unit::Kind::ranger, nearer + Vec2(static_cast<double>(k % 4) - 1.5, 4 + static_cast<double>(k / 4)), 1);
    }
    sim = Simulation(s, m);
    const Vec2 other = nearer == red ? site : red;
    EXPECT_TRUE(c.attackTarget(sim, front) == std::optional<Vec2>(other)); // guarded: the other one
}

/// With 13 Prospectors one goes to scout the enemy start it has not
/// seen, and goes back to work once it has seen it.
TEST(commanderAProspectorScoutsTheEnemyStart) {
    const MapDefinition& m = homeMap();
    GameState s = GameState::new_(m);
    const Commander c(m);
    const Vec2 home = citadelOf(s, 0).position;
    auto blueProspectors = [](const GameState& t) {
        return std::count_if(t.units.begin(), t.units.end(),
                             [](const Unit& u) { return u.owner == 0 && u.kind == Unit::Kind::prospector; });
    };
    while (blueProspectors(s) < Commander::scoutAt - 1) {
        add(s, Unit::Kind::prospector, home + Vec2(static_cast<double>(s.units.size() % 6) - 2.5, -3.5));
    }
    Simulation sim(s, m);
    sim.step(1.0 / 30);
    auto scoutOrders = [&](const Simulation& t) {
        std::vector<Command> out;
        for (const Command& o : c.orders(t)) if (o.is<Command::Mission>()) out.push_back(o);
        return out;
    };
    EXPECT_TRUE(scoutOrders(sim).empty()); // 12 Prospectors: not yet
    add(sim.state, Unit::Kind::prospector, home + Vec2(0, -4));
    sim = Simulation(sim.state, m);
    sim.step(1.0 / 30);
    const Vec2 red = c.start(sim.state, 1);
    const auto first = scoutOrders(sim);
    ASSERT_TRUE(!first.empty());
    const auto* order = first.front().as<Command::Mission>();
    ASSERT_TRUE(order && order->mission && order->mission->is<Mission::Raid>()); // no scout
    const int64_t id = order->unit;
    const Vec2 to = order->mission->as<Mission::Raid>()->at;
    EXPECT_TRUE(to == red);
    EXPECT_TRUE(sim.issue(Command::Mission{id, Mission::Raid{to}}));
    const Vec2 from = sim.state.unit(id)->position;
    for (int i = 0; i < 60; i++) sim.step(1.0 / 30);
    EXPECT_TRUE(sim.state.unit(id)->task == Unit::Task::errand);
    EXPECT_TRUE(distance(sim.state.unit(id)->position, red) < distance(from, red) - 3); // on its way
    EXPECT_TRUE(scoutOrders(sim).empty()); // on with it
    // There: the start seen, it goes back to work.
    for (Unit& u : sim.state.units) if (u.id == id) u.position = red + Vec2(0, -4);
    sim.lookNow();
    EXPECT_TRUE((scoutOrders(sim) == std::vector<Command>{Command::Mission{id, std::nullopt}}));
    EXPECT_TRUE(sim.issue(Command::Mission{id, std::nullopt}));
    EXPECT_TRUE(sim.state.unit(id)->task == Unit::Task::idle);
}

/// A few enemy Rangers on a base with no soldiers of its own: it
/// pulls Prospectors, who fight them; once they are gone the
/// Prospectors go back to work. A big attack is left to the army.
TEST(commanderProspectorsFightOffASmallRush) {
    const MapDefinition& m = homeMap();
    GameState s = GameState::new_(m);
    const Commander c(m);
    const Vec2 home = citadelOf(s, 0).position;
    for (int k = 0; k < 8; k++) add(s, Unit::Kind::prospector, home + Vec2(static_cast<double>(k) - 3.5, -3.5));
    std::vector<int64_t> red;
    for (int k = 0; k < 2; k++) red.push_back(add(s, Unit::Kind::ranger, home + Vec2(static_cast<double>(k) - 0.5, 5), 1));
    s.players[1].attack = home;
    Simulation sim(s, m);
    const auto pulls = c.militia(sim, {});
    EXPECT_EQ(pulls.size(), size_t(4)); // two per Ranger
    for (const Command& o : pulls) EXPECT_TRUE(sim.issue(o));
    for (int i = 0; i < 90; i++) sim.step(1.0 / 30);
    double hurt = 0;
    bool gone = false;
    for (int64_t id : red) {
        if (auto u = sim.state.unit(id)) hurt += Rules::hp(Unit::Kind::ranger) - u->hp;
        else gone = true;
    }
    EXPECT_TRUE(hurt > 0 || gone); // they fought
    // The Rangers gone: back to work.
    std::erase_if(sim.state.units, [](const Unit& u) { return u.owner == 1; });
    sim = Simulation(sim.state, m);
    const auto back = c.militia(sim, {});
    const auto alive = std::count_if(sim.state.units.begin(), sim.state.units.end(),
                                     [](const Unit& u) { return u.mission && u.mission->is<Mission::Hunt>(); });
    EXPECT_TRUE(alive > 0);
    EXPECT_EQ(static_cast<int64_t>(back.size()), static_cast<int64_t>(alive)); // every one still out
    EXPECT_TRUE(std::all_of(back.begin(), back.end(), [](const Command& o) {
        auto mm = o.as<Command::Mission>();
        return mm && !mm->mission;
    }));
    // Twelve Rangers: too many for Prospectors.
    for (int k = 0; k < 12; k++) {
        add(s, Unit::Kind::ranger, home + Vec2(static_cast<double>(k % 4) - 1.5, 6 + static_cast<double>(k / 4)), 1);
    }
    EXPECT_TRUE(c.militia(Simulation(s, m), {}).empty());
}

/// Once the spots with room for a Lab on both sides are gone, a base
/// still takes more production buildings (room on the Lab's side).
TEST(commanderFullBaseStillFindsGarrisonSpots) {
    const MapDefinition& m = homeMap();
    GameState s = blueOnly(GameState::new_(m));
    const Commander c(m);
    const Structure citadel = citadelOf(s, 0);
    for (int i = 0; i < 2; i++) add(s, Structure::Kind::habDome, *c.habDomeSpot(s, citadel));
    for (int k = 0; k < 5; k++) {
        auto p = c.garrisonSpot(s, citadel);
        ASSERT_TRUE(p.has_value()); // only k Garrisons fit
        addLab(s, add(s, Structure::Kind::garrison, *p));
    }
}

/// Fireflies kept for runbys go for an enemy ore line with workers
/// on it, and a burnt one pulls out.
TEST(commanderFirefliesRunIntoAOreLineAndPullOutWhenHurt) {
    const MapDefinition& m = homeMap();
    GameState s = GameState::new_(m);
    s.players[0].fireflies = 2;
    const Structure red = redAtWork(s);
    const Structure blue = citadelOf(s, 0);
    const int64_t h1 = add(s, Unit::Kind::firefly, blue.position + Vec2(0, 7));
    const int64_t h2 = add(s, Unit::Kind::firefly, blue.position + Vec2(1.5, 7));
    const Commander c(m, 0);
    std::vector<std::pair<int64_t, Vec2>> raids;
    for (const Command& o : c.raiders(Simulation(s, m))) {
        if (missionIs<Mission::Raid>(o)) {
            auto mm = o.as<Command::Mission>();
            raids.push_back({mm->unit, mm->mission->as<Mission::Raid>()->at});
        }
    }
    std::set<int64_t> ids;
    for (const auto& r : raids) ids.insert(r.first);
    EXPECT_TRUE(ids == (std::set<int64_t>{h1, h2}));
    for (const auto& r : raids) EXPECT_TRUE(distance(r.second, red.position) < 10); // into red's ore line
    ASSERT_TRUE(!raids.empty());

    // One alone waits for a second.
    GameState lone = s;
    std::erase_if(lone.units, [&](const Unit& u) { return u.id == h2; });
    EXPECT_TRUE(c.raiders(Simulation(lone, m)).empty());

    // Burnt on the raid: back home.
    for (Unit& u : s.units) {
        if (u.id != h1) continue;
        u.mission = Mission::Raid{raids[0].second};
        u.position = raids[0].second;
        u.hp = 20;
    }
    const auto out = c.raiders(Simulation(s, m));
    EXPECT_TRUE(std::any_of(out.begin(), out.end(), [&](const Command& o) { return missionIs<Mission::FallBack>(o, h1); }));
}

/// A Dropship at the rally takes two Juggernauts and four Rangers aboard,
/// flies them to red's ore line and sets them down raiding.
TEST(commanderDropshipDropsASquadOnAOreLine) {
    const MapDefinition& m = homeMap();
    GameState s = GameState::new_(m);
    s.players[0].drops = true;
    const Structure red = redAtWork(s);
    const Structure blue = citadelOf(s, 0);
    const Vec2 at = blue.position + normalize(m.front() - blue.position) * 9;
    const int64_t dropship = add(s, Unit::Kind::dropship, at);
    std::vector<int64_t> squad;
    for (int k = 0; k < 10; k++) {
        const double a = static_cast<double>(k) * 2.39996;
        const Vec2 p = at + Vec2(std::cos(a), std::sin(a)) * 0.9 * std::sqrt(static_cast<double>(k) + 1);
        squad.push_back(add(s, k < 2 ? Unit::Kind::juggernaut : Unit::Kind::ranger, p));
    }
    Simulation sim(s, m);
    const auto orders = Commander(m, 0).drops(sim);
    std::vector<int64_t> boarding;
    for (const Command& o : orders) {
        if (auto b = o.as<Command::Board>(); b && b->dropship == dropship && b->unit) boarding.push_back(*b->unit);
    }
    EXPECT_EQ(boarding.size(), size_t(6));
    auto boards = [&](int64_t id) { return std::find(boarding.begin(), boarding.end(), id) != boarding.end(); };
    EXPECT_TRUE(boards(squad[0]) && boards(squad[1])); // both Juggernauts go
    std::optional<Vec2> drop;
    for (const Command& o : orders) {
        if (missionIs<Mission::Drop>(o, dropship)) { drop = o.as<Command::Mission>()->mission->as<Mission::Drop>()->at; break; }
    }
    ASSERT_TRUE(drop.has_value()); // no drop ordered
    EXPECT_TRUE(distance(*drop, red.position) < 10);
    for (const Command& o : orders) EXPECT_TRUE(sim.issue(o));
    EXPECT_EQ(sim.state.unit(dropship)->cargo.value_or(std::vector<int64_t>{}).size(), size_t(6));

    int steps = 0;
    auto aboard = [&] {
        return std::any_of(sim.state.units.begin(), sim.state.units.end(), [](const Unit& u) { return u.task == Unit::Task::aboard; });
    };
    while (aboard() && steps < 90 * 30) { sim.step(1.0 / 30); steps += 1; }
    std::vector<Unit> landed;
    for (const Unit& u : sim.state.units) if (boards(u.id)) landed.push_back(u);
    EXPECT_EQ(landed.size(), size_t(6));
    for (const Unit& u : landed) {
        EXPECT_TRUE(distance(u.position, *drop) < 3);
        EXPECT_TRUE(u.mission && u.mission->is<Mission::Raid>()); // dropped units raid
    }
}

/// A dropped squad outnumbered by enemy soldiers boards its Dropship
/// again and is flown home.
TEST(commanderDroppedSquadIsLiftedOutWhenItLoses) {
    const MapDefinition& m = homeMap();
    GameState s = GameState::new_(m);
    const Structure red = redAtWork(s);
    const Structure blue = citadelOf(s, 0);
    const Vec2 p = red.position + Vec2(0, 5);
    const int64_t dropship = add(s, Unit::Kind::dropship, p);
    s.units.back().mission = Mission::Raid{p};
    std::set<int64_t> squad;
    for (int k = 0; k < 4; k++) {
        squad.insert(add(s, Unit::Kind::ranger, p + Vec2(static_cast<double>(k) - 1.5, 0.5)));
        s.units.back().mission = Mission::Raid{p};
    }
    for (int k = 0; k < 5; k++) add(s, Unit::Kind::juggernaut, p + Vec2(static_cast<double>(k) - 2, 4), 1);
    const auto orders = Commander(m, 0).drops(Simulation(s, m));
    std::set<int64_t> boarding;
    for (const Command& o : orders) {
        if (auto b = o.as<Command::Board>(); b && b->dropship == dropship && b->unit) boarding.insert(*b->unit);
    }
    EXPECT_TRUE(boarding == squad);
    std::optional<Vec2> homeward;
    for (const Command& o : orders) {
        if (missionIs<Mission::Drop>(o, dropship)) { homeward = o.as<Command::Mission>()->mission->as<Mission::Drop>()->at; break; }
    }
    ASSERT_TRUE(homeward.has_value()); // the Dropship does not fly them out
    EXPECT_TRUE(distance(*homeward, blue.position) < 15);
}

/// Prospectors leave a base that enemy Fireflies (not only Rangers) are in.
TEST(commanderProspectorsRunFromFireflies) {
    const MapDefinition& m = homeMap();
    GameState s = blueOnly(GameState::new_(m));
    const Structure main = s.structures[0];
    std::optional<Vec2> natural;
    for (const BaseSite& b : m.bases) {
        if (!(distance(b.center, main.position) > 5)) continue;
        if (!natural || distance(b.center, main.position) < distance(*natural, main.position)) natural = b.center;
    }
    add(s, Structure::Kind::citadel, *natural);
    const Simulation probe(s, m);
    const int64_t site = probe.site(main);
    int64_t patch = -1;
    for (size_t i = 0; i < s.patches.size(); i++) if (probe.patchBase[i] == site) { patch = static_cast<int64_t>(i); break; }
    s.units[0].patch = patch;
    s.units[0].task = Unit::Task::toPatch;
    add(s, Unit::Kind::firefly, main.position + Vec2(3, 3), 1);
    const Simulation sim(s, m);
    Commander c(m);
    c.pullBack = false; // the Swift game's evacuation (WorkerPullTests: the C++ pull)
    const auto run = c.evacuate(sim.state, sim, {}).first;
    EXPECT_EQ(run.size(), size_t(1));
    ASSERT_TRUE(!run.empty());
    const auto* g = run.front().as<Command::Gather>();
    ASSERT_TRUE(g != nullptr);
    EXPECT_TRUE(sim.patchBase[static_cast<size_t>(g->patch)] != site);
}

/// The same base on both halves of the mirrored map, with the same
/// temperament: spot finders pick mirrored spots, one `orders` call
/// each builds the same things at mirrored spots, and Labs (on +X
/// on both halves) still count as twins.
TEST(commanderMirroredBasesGetMirroredTechOrders) {
    const MapDefinition& m = homeMap();
    ASSERT_TRUE(m.mirrorX.has_value()); // the handcrafted map is mirrored
    const double axis = *m.mirrorX;
    auto twin = [&](Vec2 p) { return Vec2(2 * axis - p.x, p.y); };
    GameState s = GameState::new_(m);
    for (Player& p : s.players) {
        p.aggression = 0.5; p.greed = 0; p.garrisonPerBase = 2; p.comets = 1;
        p.style = Player::Style::bio; p.fireflies = 0; p.drops = false;
        p.ore = 1000; p.hydrogen = 500;
    }
    std::optional<Structure> west, east;
    for (const Structure& b : s.structures) {
        if (b.kind != Structure::Kind::citadel) continue;
        if (b.position.x < axis && !west) west = b;
        if (b.position.x > axis && !east) east = b;
    }
    ASSERT_TRUE(west && east);
    const Commander cw(m, west->owner), ce(m, east->owner);
    EXPECT_TRUE(distance(twin(*cw.garrisonSpot(s, *west)), *ce.garrisonSpot(s, *east)) < 1e-6);
    auto both = [&](Structure::Kind kind, Vec2 p) {
        const int64_t a = add(s, kind, p, west->owner);
        return std::make_pair(a, add(s, kind, twin(p), east->owner));
    };
    both(Structure::Kind::habDome, *cw.habDomeSpot(s, *west));
    const auto [rw, re] = both(Structure::Kind::garrison, *cw.garrisonSpot(s, *west));
    const auto wells = *s.wells;
    for (const Well& g : wells) if (distance(g.position, west->position) < 12) both(Structure::Kind::derrick, g.position);
    for (int k = 0; k < 7; k++) {
        const Vec2 p = west->position + Vec2(static_cast<double>(k) - 3, -3.2);
        add(s, Unit::Kind::prospector, p, west->owner);
        add(s, Unit::Kind::prospector, twin(p), east->owner);
    }
    EXPECT_TRUE(!s.mirrorBreak(m).has_value());

    const Simulation sim(s, m);
    const auto a = builds(cw.orders(sim)), b = builds(ce.orders(sim));
    EXPECT_TRUE(std::any_of(a.begin(), a.end(), [](const Built& x) { return x.kind == Structure::Kind::foundry; }));
    EXPECT_EQ(a.size(), b.size());
    for (size_t i = 0; i < std::min(a.size(), b.size()); i++) {
        EXPECT_TRUE(a[i].kind == b[i].kind);
        EXPECT_TRUE(distance(twin(a[i].at), b[i].at) < 1e-6); // at mirrored spots
    }

    // Labs sit on +X on both halves, yet pair up by their buildings.
    addLab(s, rw);
    addLab(s, re);
    EXPECT_TRUE(!s.mirrorBreak(m).has_value());
    std::erase_if(s.structures, [&](const Structure& x) { return x.kind == Structure::Kind::lab && x.parent == re; });
    s.structures[indexOf(s, re)].addon = std::nullopt;
    const auto broken = s.mirrorBreak(m);
    EXPECT_TRUE(broken && broken->find("lab") != std::string::npos);
}

// MARK: - The golden (GoldenCommanderTests.swift)

namespace {

std::string num(double x) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.17g", x);
    return buf;
}
std::string vec(std::optional<Vec2> p) { return p ? num(p->x) + "," + num(p->y) : "-"; }
std::string opt(std::optional<int64_t> i) { return i ? std::to_string(*i) : "-"; }

std::string describe(const std::optional<Mission>& m) {
    if (!m) return "-";
    if (auto c = m->as<Mission::Raid>()) return "raid " + vec(c->at);
    if (auto c = m->as<Mission::Drop>()) return "drop " + vec(c->at);
    if (auto c = m->as<Mission::FallBack>()) return "fallBack " + vec(c->at);
    if (auto c = m->as<Mission::Hunt>()) return "hunt " + vec(c->at);
    if (auto c = m->as<Mission::Assault>()) return "assault " + std::to_string(c->objective) + " " + vec(c->at);
    if (auto c = m->as<Mission::Hold>()) return "hold " + std::to_string(c->objective) + " " + vec(c->at);
    if (auto c = m->as<Mission::Regroup>()) return "regroup " + std::to_string(c->objective) + " " + vec(c->at);
    return "?";
}

std::string describe(const Command& cmd) {
    const auto id = [](int64_t i) { return std::to_string(i); };
    if (auto c = cmd.as<Command::Train>()) return "train " + id(c->structure) + " " + (c->kind ? std::string(rawValue(*c->kind)) : "-");
    if (auto c = cmd.as<Command::Build>()) return "build " + id(c->worker) + " " + std::string(rawValue(c->kind)) + " " + vec(c->at);
    if (auto c = cmd.as<Command::Addon>()) return "addon " + id(c->structure);
    if (auto c = cmd.as<Command::Harvest>()) return "harvest " + id(c->worker) + " " + id(c->derrick);
    if (auto c = cmd.as<Command::Anchor>()) return "anchor " + id(c->unit) + " " + (c->on ? "true" : "false");
    if (auto c = cmd.as<Command::Mission>()) return "mission " + id(c->unit) + " " + describe(c->mission);
    if (auto c = cmd.as<Command::Board>()) return "board " + id(c->dropship) + " " + opt(c->unit);
    if (auto c = cmd.as<Command::Gather>()) return "gather " + id(c->worker) + " " + id(c->patch);
    if (auto c = cmd.as<Command::Resume>()) return "resume " + id(c->worker) + " " + id(c->structure);
    if (auto c = cmd.as<Command::Research>()) return "research " + id(c->structure) + " " + std::string(rawValue(c->upgrade));
    if (auto c = cmd.as<Command::Load>()) return "load " + id(c->unit) + " " + id(c->bastion);
    if (auto c = cmd.as<Command::Repair>()) return "repair " + id(c->worker) + " " + id(c->structure);
    if (auto c = cmd.as<Command::Attack>()) return "attack " + id(c->player) + " " + vec(c->at);
    if (auto c = cmd.as<Command::Defend>()) return "defend " + id(c->player) + " " + vec(c->at) + " " + id(c->group);
    if (auto c = cmd.as<Command::Retreat>()) return "retreat " + id(c->player);
    if (auto c = cmd.as<Command::Retask>()) return "retask " + id(c->player) + " " + id(c->id) + " " + std::string(rawValue(c->kind));
    if (auto c = cmd.as<Command::Serve>()) return "serve " + id(c->player) + " " + id(c->request) + " [" + describe(*c->command) + "]";
    return "other";
}

/// One `orders` call per player, and the spot finders at each Citadel,
/// against the golden's `part`.
void compareDump(const GameState& state, const MapDefinition& m, const nlohmann::json& part, const std::string& name) {
    const Simulation sim(state, m);
    const auto& orders = part["orders"];
    EXPECT_EQ(orders.size(), state.players.size());
    for (size_t p = 0; p < state.players.size() && p < orders.size(); p++) {
        std::vector<std::string> mine;
        Commander ai(m, static_cast<int64_t>(p));
        ai.frontDefence = false; // the Swift golden's placement
        for (const Command& c : ai.orders(sim)) mine.push_back(describe(c));
        std::vector<std::string> want = orders[p].get<std::vector<std::string>>();
        EXPECT_EQ(mine.size(), want.size());
        for (size_t i = 0; i < std::min(mine.size(), want.size()); i++) {
            if (mine[i] != want[i]) std::printf("    %s player %zu order %zu: %s vs %s\n", name.c_str(), p, i, mine[i].c_str(), want[i].c_str());
            EXPECT_EQ(mine[i], want[i]);
        }
    }
    size_t k = 0;
    for (const Structure& citadel : state.structures) {
        if (citadel.kind != Structure::Kind::citadel) continue;
        ASSERT_TRUE(k < part["spots"].size());
        const auto& want = part["spots"][k++];
        EXPECT_EQ(want["id"].get<int64_t>(), citadel.id);
        Commander c(m, citadel.owner);
        c.frontDefence = false; // the Swift golden's placement
        EXPECT_EQ(vec(c.habDomeSpot(state, citadel)), want["habDome"].get<std::string>());
        EXPECT_EQ(vec(c.garrisonSpot(state, citadel)), want["garrison"].get<std::string>());
        EXPECT_EQ(vec(c.bastionSpot(state, sim, citadel)), want["bastion"].get<std::string>());
        EXPECT_EQ(vec(c.derrickSpot(state, {citadel})), want["derrick"].get<std::string>());
        EXPECT_EQ(vec(c.nextSite(state, sim, {}, citadel.position, false)), want["next"].get<std::string>());
    }
    EXPECT_EQ(k, part["spots"].size());
}

} // namespace

/// The Swift game's orders and spots on the bench fixture, a fresh game and
/// a teched one (`GoldenCommanderTests.swift`).
TEST(commanderGolden) {
    const auto json = golden::load(golden::path("commander.json"));
    ASSERT_TRUE(!json.is_discarded());
    std::string error;
    const auto session = SessionStore::load(golden::repo("bench/badlands-large.json"), &error);
    ASSERT_TRUE(session.has_value());
    const MapDefinition m = WindowMaps::build(MapChoice{MapStyle::badlands, MapSize::large});
    EXPECT_EQ(session->mapName, m.name);
    compareDump(session->state, m, json["bench"], "bench");

    GameState fresh = GameState::new_(m);
    for (Player& p : fresh.players) { p.ore = 1000; p.hydrogen = 500; }
    compareDump(fresh, m, json["fresh"], "fresh");

    // Every player teched as `blueTech`: a Hab Dome, a Garrison, its main's
    // Derricks, seven more Prospectors and a Comet.
    GameState tech = fresh;
    for (const Structure& citadel : fresh.structures) {
        if (citadel.kind != Structure::Kind::citadel) continue;
        const Commander c(m, citadel.owner);
        const auto dome = c.habDomeSpot(tech, citadel);
        ASSERT_TRUE(dome.has_value());
        add(tech, Structure::Kind::habDome, *dome, citadel.owner);
        const auto garrison = c.garrisonSpot(tech, citadel);
        ASSERT_TRUE(garrison.has_value());
        add(tech, Structure::Kind::garrison, *garrison, citadel.owner);
        const auto wells = *tech.wells;
        for (const Well& g : wells) {
            if (distance(g.position, citadel.position) < 12) add(tech, Structure::Kind::derrick, g.position, citadel.owner);
        }
        for (int k = 0; k < 7; k++) {
            const int64_t id = tech.nextID;
            tech.units.push_back(Unit(id, Unit::Kind::prospector, citadel.owner, citadel.position + Vec2(static_cast<double>(k) - 3, -3.2), 0,
                                      Unit::Task::idle));
            tech.nextID += 1;
        }
        const int64_t id = tech.nextID;
        tech.units.push_back(Unit(id, Unit::Kind::comet, citadel.owner, citadel.position + Vec2(0, 8), 0, Unit::Task::idle));
        tech.nextID += 1;
    }
    compareDump(tech, m, json["tech"], "tech");
}
