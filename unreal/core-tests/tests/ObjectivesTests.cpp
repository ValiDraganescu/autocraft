// Port of Tests/GameCoreTests/ObjectivesTests.swift: the humans' objectives
// and stance for their team's army: one `orders` call (or a few seconds of
// simulation) from a hand-built position each, never a game.
#include "SquadHelpers.h"

#include <tuple>

using namespace ac;
using namespace squadtest;

namespace {

/// Blue alone with `rangers` Rangers beside its Citadel; red has
/// a lone Hab Dome at two far base sites. Returns the state and the
/// two sites' centres.
std::tuple<GameState, std::vector<Vec2>> scene(int rangers) {
    const MapDefinition& m = homeMap();
    GameState s = blueOnly(GameState::new_(m));
    const Vec2 citadel = s.structures[0].position;
    std::vector<Vec2> far;
    for (const auto& b : m.bases) { far.push_back(b.center); }
    std::stable_sort(far.begin(), far.end(),
                     [&](Vec2 a, Vec2 b) { return distance(a, citadel) > distance(b, citadel); });
    const std::vector<Vec2> targets(far.begin(), far.begin() + 2);
    for (const auto& t : targets) { add(s, Structure::Kind::habDome, t, 1); }
    for (int k = 0; k < rangers; k++) {
        add(s, Unit::Kind::ranger, citadel + Vec2(static_cast<double>(k % 6) - 2.5, 5 + static_cast<double>(k / 6)));
    }
    return {s, targets};
}

int64_t give(GameState& s, Objective::Kind kind, Vec2 p) {
    Directives d = s.players[0].orders();
    const int64_t id = d.nextID;
    Objective o;
    o.id = id;
    o.kind = kind;
    o.at = p;
    d.objectives.push_back(o);
    d.nextID += 1;
    s.players[0].directives = d;
    return id;
}

template <class C> bool isCase(const std::optional<Mission>& m, int64_t objective) {
    if (!m) { return false; }
    const auto* c = m->as<C>();
    return c && c->objective == objective;
}

template <class C> int64_t countCase(const std::map<int64_t, std::optional<Mission>>& sent, int64_t objective) {
    int64_t n = 0;
    for (const auto& [id, m] : sent) { if (isCase<C>(m, objective)) { n++; } }
    return n;
}

template <class C> bool allCase(const std::map<int64_t, std::optional<Mission>>& sent, int64_t objective) {
    return countCase<C>(sent, objective) == static_cast<int64_t>(sent.size());
}

bool startsWith(const std::string& s, const std::string& prefix) { return s.rfind(prefix, 0) == 0; }

bool attacks(const Commander& c, const GameState& s) {
    for (const auto& o : c.army(Simulation(s, homeMap()))) {
        if (const auto* a = o.as<Command::Attack>(); a && a->player == 0 && a->at) { return true; }
    }
    return false;
}

} // namespace

/// Two attacks at once: the army splits, each squad worth at least
/// what its point needs, all of it sent.
TEST(objectivesTwoAttacksSplitTheArmy) {
    auto [s, t] = scene(20);
    const MapDefinition& m = homeMap();
    const int64_t a = give(s, Objective::Kind::attack, t[0]), b = give(s, Objective::Kind::attack, t[1]);
    const Simulation sim(s, m);
    const auto sent = missions(Commander(m).orders(sim));
    const int64_t toA = countCase<Mission::Assault>(sent, a);
    const int64_t toB = countCase<Mission::Assault>(sent, b);
    EXPECT_TRUE(toA >= static_cast<int64_t>(Commander::assaultForce));
    EXPECT_TRUE(toB >= static_cast<int64_t>(Commander::assaultForce));
    EXPECT_EQ(toA + toB, 20);
    const auto status = Commander(m).objectiveStatus(sim);
    ASSERT_TRUE(status.size() == 2);
    EXPECT_EQ(status[0].id, a);
    EXPECT_EQ(status[1].id, b);
    for (const auto& st : status) { EXPECT_TRUE(startsWith(st.text, "Attacking")); }
}

/// Too few for the job: the squad gathers at the rally; all in, it
/// goes at once.
TEST(objectivesSmallSquadGathersUnlessAllIn) {
    auto [s, t] = scene(3);
    const MapDefinition& m = homeMap();
    const int64_t a = give(s, Objective::Kind::attack, t[0]);
    const Commander c(m);
    Simulation sim(s, m);
    EXPECT_TRUE(allCase<Mission::Hold>(missions(c.orders(sim)), a));
    EXPECT_EQ(std::string(c.objectiveStatus(sim).front().text), std::string("Gathering 3 / 6 at the rally"));
    sim.issue(Command::Stance{0, Stance::allIn});
    EXPECT_TRUE(allCase<Mission::Assault>(missions(c.orders(sim)), a));
}

/// A base to defend gets a guard; with no attack given, the rest stay
/// in the army.
TEST(objectivesDefendObjectiveGuardsTheBase) {
    auto [s, t] = scene(10);
    (void)t;
    const MapDefinition& m = homeMap();
    const Vec2 citadel = s.structures[0].position;
    const int64_t d = give(s, Objective::Kind::defend, citadel);
    const auto sent = missions(Commander(m).orders(Simulation(s, m)));
    int64_t guards = 0;
    for (const auto& [id, mi] : sent) {
        if (mi && mi->as<Mission::Hold>() && *mi == Mission(Mission::Hold{d, citadel})) { guards++; }
    }
    EXPECT_EQ(guards, static_cast<int64_t>(Commander::guardForce));
    EXPECT_EQ(static_cast<int64_t>(sent.size()), guards); // the rest keep no mission
}

/// A real attack on the base pulls an attack squad home; all in, it
/// does not.
TEST(objectivesAttackOnTheBasePullsSquadsHome) {
    auto [s, t] = scene(12);
    const MapDefinition& m = homeMap();
    const int64_t a = give(s, Objective::Kind::attack, t[0]);
    for (auto& u : s.units) {
        if (u.kind == Unit::Kind::ranger) { u.mission = Mission::Assault{a, t[0]}; }
    }
    const Vec2 citadel = s.structures[0].position;
    for (int k = 0; k < 10; k++) { add(s, Unit::Kind::ranger, citadel + Vec2(static_cast<double>(k) - 4.5, -6), 1); }
    const Commander c(m);
    const auto home = missions(c.orders(Simulation(s, m)));
    EXPECT_EQ(static_cast<int64_t>(home.size()), 12);
    for (const auto& [id, mi] : home) { EXPECT_TRUE(!mi); } // back in the army
    s.players[0].directives->stance = Stance::allIn;
    const auto allIn = missions(c.squads(Simulation(s, m)));
    EXPECT_TRUE(allIn.empty()); // they keep attacking
}

/// An attack squad outfought at its point falls back to regroup; all
/// in, it fights on.
TEST(objectivesOutfoughtSquadFallsBack) {
    auto [s, t] = scene(0);
    const MapDefinition& m = homeMap();
    const int64_t a = give(s, Objective::Kind::attack, t[0]);
    for (int k = 0; k < 8; k++) {
        const int64_t id = add(s, Unit::Kind::ranger, t[0] + Vec2(static_cast<double>(k) - 3.5, 4));
        unitRef(s, id)->mission = Mission::Assault{a, t[0]};
    }
    for (int k = 0; k < 16; k++) {
        add(s, Unit::Kind::ranger, t[0] + Vec2(static_cast<double>(k % 8) - 3.5, -2 - static_cast<double>(k / 8)), 1);
    }
    const Commander c(m);
    const Simulation sim(s, m);
    EXPECT_TRUE(allCase<Mission::Regroup>(missions(c.squads(sim)), a));
    EXPECT_EQ(std::string(c.objectiveStatus(sim).front().text), std::string("Falling back to regroup"));
    s.players[0].directives->stance = Stance::allIn;
    EXPECT_TRUE(missions(c.squads(Simulation(s, m))).empty());
}

/// At its point with nothing of the enemy's left: the attack becomes a
/// hold and the squad stays there, a second later too.
TEST(objectivesTakenPointIsHeld) {
    auto [s, t] = scene(0);
    const MapDefinition& m = homeMap();
    s.structures.erase(std::remove_if(s.structures.begin(), s.structures.end(), [](const Structure& b) { return b.owner == 1; }),
                       s.structures.end());
    const int64_t a = give(s, Objective::Kind::attack, t[0]);
    for (int k = 0; k < 6; k++) {
        const int64_t id = add(s, Unit::Kind::ranger, t[0] + Vec2(static_cast<double>(k) - 2.5, 2));
        unitRef(s, id)->mission = Mission::Assault{a, t[0]};
    }
    Simulation sim(s, m);
    const Commander c(m);
    const auto orders = c.squads(sim);
    EXPECT_TRUE(contains(orders, Command::Retask{0, a, Objective::Kind::hold}));
    for (const auto& o : orders) { sim.issue(o); }
    const auto objectives = sim.state.players[0].orders().objectives;
    ASSERT_TRUE(objectives.size() == 1);
    EXPECT_TRUE(objectives[0].kind == Objective::Kind::hold);
    for (const auto& u : sim.state.units) {
        if (u.kind == Unit::Kind::ranger) { EXPECT_TRUE(u.mission == std::optional<Mission>(Mission::Hold{a, t[0]})); } // the squad stays
    }
    EXPECT_TRUE(c.squads(sim).empty()); // and keeps holding
    EXPECT_EQ(std::string(c.objectiveStatus(sim).front().text), std::string("Holding the area (6)"));
}

/// A Bastion to man takes Rangers out of a squad, ahead of it, and the
/// AI fills nothing else with them.
TEST(objectivesManningPullsRangersFromSquads) {
    auto [s, t] = scene(8);
    const MapDefinition& m = homeMap();
    const int64_t a = give(s, Objective::Kind::attack, t[0]);
    for (auto& u : s.units) {
        if (u.kind == Unit::Kind::ranger) { u.mission = Mission::Hold{a, t[0]}; }
    }
    const int64_t bastion = add(s, Structure::Kind::bastion, s.structures[0].position + Vec2(-8, 0));
    Simulation sim(s, m);
    const Vec2 at = s.structure(bastion)->position;
    EXPECT_TRUE(sim.issue(Command::Objective{0, Objective::Kind::man, at + Vec2(1, 0)}));
    EXPECT_TRUE(sim.state.players[0].orders().objectives.back().structure == std::optional<int64_t>(bastion));
    EXPECT_TRUE(!sim.issue(Command::Objective{0, Objective::Kind::man, at})); // manned already
    const Commander c(m);
    for (const auto& o : c.orders(sim)) { sim.issue(o); }
    int64_t going = 0;
    for (const auto& u : sim.state.units) {
        if (u.task == Unit::Task::toBastion && u.structure == bastion) {
            going++;
            EXPECT_TRUE(!u.mission);
        }
    }
    EXPECT_EQ(going, Rules::bastionCapacity);
    EXPECT_EQ(std::string(c.objectiveStatus(sim).back().text), std::string("Filling 0 / 4 · 4 on the way"));
    for (int i = 0; i < 20 * 30; i++) { sim.step(1.0 / 30); }
    const auto crew = sim.state.structure(bastion)->crew;
    EXPECT_EQ(crew ? static_cast<int64_t>(crew->size()) : -1, Rules::bastionCapacity);
    EXPECT_EQ(std::string(c.objectiveStatus(sim).back().text), std::string("Manned 4 / 4"));
}

/// With too few Rangers, the Bastion's are trained first, ahead of the
/// queue; a lost Bastion drops its objective.
TEST(objectivesManningTrainsRangers) {
    const MapDefinition& m = homeMap();
    auto [s, garrison] = blueTech(m);
    const int64_t bastion = add(s, Structure::Kind::bastion, s.structures[0].position + Vec2(-8, 0));
    add(s, Unit::Kind::ranger, s.structures[0].position + Vec2(0, 6));
    Simulation sim(s, m);
    sim.issue(Command::Request{0, Request::What::Unit{Unit::Kind::comet}, 5, false, std::nullopt, std::nullopt});
    EXPECT_TRUE(sim.issue(Command::Objective{0, Objective::Kind::man, s.structure(bastion)->position}));
    const int64_t man = sim.state.players[0].orders().objectives[0].id;
    const Commander c(m);
    const auto orders = c.orders(sim);
    const Command want = Command::Serve{0, man, Command(Command::Train{garrison, Unit::Kind::ranger})};
    EXPECT_EQ(std::count(orders.begin(), orders.end(), want), 2); // two Rangers queued for it, the Garrison' depth
    EXPECT_EQ(std::string(c.objectiveStatus(sim).front().text), std::string("Filling 0 / 4 · 1 on the way · training 3"));
    sim.state.structures.erase(std::remove_if(sim.state.structures.begin(), sim.state.structures.end(),
                                              [&](const Structure& b) { return b.id == bastion; }),
                               sim.state.structures.end());
    EXPECT_TRUE(contains(c.squads(sim), Command::CancelObjective{0, man}));
}

/// A squad soldier walks to its point.
TEST(objectivesSquadWalksToItsPoint) {
    auto [s, t] = scene(1);
    (void)t;
    const MapDefinition& m = homeMap();
    Unit& u = *std::find_if(s.units.begin(), s.units.end(), [](const Unit& x) { return x.kind == Unit::Kind::ranger; });
    const int64_t id = u.id;
    const Vec2 to = u.position + Vec2(0, 10);
    u.mission = Mission::Assault{1, to};
    Simulation sim(s, m);
    const double before = distance(sim.state.unit(id)->position, to);
    for (int i = 0; i < 2 * 30; i++) { sim.step(1.0 / 30); }
    EXPECT_TRUE(distance(sim.state.unit(id)->position, to) < before - 4);
}

/// A Citadel asked for at a chosen site goes there, not where
/// the AI would expand.
TEST(objectivesExpansionGoesToTheChosenSite) {
    const MapDefinition& m = homeMap();
    auto [s, garrison] = blueTech(m);
    (void)garrison;
    s.players[0].ore = 1000;
    Simulation sim(s, m);
    const Commander c(m);
    const auto info = c.siteInfo(sim);
    EXPECT_EQ(std::count_if(info.begin(), info.end(), [](const SiteInfo& i) { return i.next; }), 1);
    const auto pick = std::find_if(info.begin(), info.end(), [](const SiteInfo& i) { return !i.owner && !i.next; });
    ASSERT_TRUE(pick != info.end()); // another free site
    sim.issue(Command::Request{0, Request::What::Building{Structure::Kind::citadel}, 1, false, std::nullopt, pick->index});
    std::vector<Vec2> builds;
    for (const auto& o : c.orders(sim)) {
        if (const auto* sv = o.as<Command::Serve>()) {
            if (const auto* b = sv->command->as<Command::Build>(); b && b->kind == Structure::Kind::citadel) { builds.push_back(b->at); }
        }
    }
    ASSERT_TRUE(builds.size() == 1);
    EXPECT_TRUE(builds[0] == pick->centre);
}

/// Hold never sends the army out on its own, even maxed; all in sends
/// a small one.
TEST(objectivesStanceHoldAndAllIn) {
    auto [s, t] = scene(4);
    (void)t;
    const Commander c(homeMap());
    EXPECT_TRUE(!attacks(c, s)); // four Rangers wait for a wave
    s.players[0].directives = Directives();
    s.players[0].directives->stance = Stance::allIn;
    EXPECT_TRUE(attacks(c, s));
    s.players[0].directives->stance = Stance::hold;
    const Vec2 citadel = s.structures[0].position;
    for (int k = 0; k < 60; k++) {
        add(s, Unit::Kind::ranger, citadel + Vec2(static_cast<double>(k % 10) - 4.5, 8 + static_cast<double>(k / 10)));
    }
    EXPECT_TRUE(!attacks(c, s)); // a maxed army holds too
}

/// Hold called while the army is out attacking: it falls back and walks
/// home, and does not turn on a straggler on the way.
TEST(objectivesHoldCallsAnAttackHome) {
    auto [s, t] = scene(0);
    const MapDefinition& m = homeMap();
    for (int k = 0; k < 8; k++) {
        add(s, Unit::Kind::ranger, t[0] + Vec2(static_cast<double>(k % 4) - 1.5, 4 + static_cast<double>(k / 4)));
        s.units.back().task = Unit::Task::attackMove;
    }
    s.players[0].attack = t[0];
    s.players[0].directives = Directives();
    const Commander c(m);
    EXPECT_TRUE(c.army(Simulation(s, m)).empty()); // auto: on with the attack
    s.players[0].directives->stance = Stance::hold;
    Simulation sim(s, m);
    const auto orders = c.army(sim);
    ASSERT_TRUE(orders.size() == 1);
    EXPECT_TRUE(orders[0] == Command(Command::Retreat{0}));

    const Vec2 home = s.structures[0].position;
    auto away = [&](const GameState& g) {
        double sum = 0;
        int64_t n = 0;
        for (const auto& u : g.units) {
            if (u.owner == 0 && u.kind == Unit::Kind::ranger) { sum = sum + distance(u.position, home); n++; }
        }
        return sum / static_cast<double>(n);
    };
    const double before = away(sim.state);
    for (const auto& o : orders) { EXPECT_TRUE(sim.issue(o)); }
    for (int i = 0; i < 3 * 30; i++) { sim.step(1.0 / 30); }
    EXPECT_TRUE(away(sim.state) < before - 4); // the army walks home

    Vec2 sum = Vec2::zero;
    for (const auto& u : sim.state.units) {
        if (u.owner == 0 && u.kind == Unit::Kind::ranger) { sum = sum + u.position; }
    }
    const Vec2 straggler = sum / 8 + Vec2(0, 5);
    GameState chased = sim.state;
    add(chased, Unit::Kind::ranger, straggler, 1);
    auto turns = [&](const GameState& g) {
        for (const auto& o : c.army(Simulation(g, m))) {
            if (const auto* a = o.as<Command::Attack>(); a && a->player == 0 && a->at) { return true; }
        }
        return false;
    };
    EXPECT_TRUE(!turns(chased)); // no turning on a straggler
    chased.players[0].directives->stance = Stance::auto_;
    EXPECT_TRUE(turns(chased)); // auto turns on it
}

/// On hold no raider goes out and those out come home: a raiding Comet
/// falls back, Fireflies ready for a runby stay, a drop on its way turns
/// back, and a squad down on an ore line is lifted out.
TEST(objectivesHoldCallsRaidersHome) {
    const MapDefinition& m = homeMap();
    GameState s = GameState::new_(m);
    s.players[0].fireflies = 2;
    s.players[0].directives = Directives();
    s.players[0].directives->stance = Stance::hold;
    const Structure red = redAtWork(s);
    const Vec2 blue = citadelOf(s, 0);
    GameState down = s;
    add(s, Unit::Kind::firefly, blue + Vec2(0, 7));
    add(s, Unit::Kind::firefly, blue + Vec2(1.5, 7));
    const Vec2 line = red.position + Vec2(0, 5);
    const int64_t comet = add(s, Unit::Kind::comet, line);
    s.units.back().mission = Mission::Raid{line};
    const int64_t dropship = add(s, Unit::Kind::dropship, (blue + red.position) / 2);
    s.units.back().mission = Mission::Drop{line};
    const Commander c(m);
    const Simulation sim(s, m);
    auto both = c.raiders(sim);
    const auto dropped = c.drops(sim);
    both.insert(both.end(), dropped.begin(), dropped.end());
    const auto sent = missions(both);
    EXPECT_EQ(static_cast<int64_t>(sent.size()), 2);
    const auto cometIt = sent.find(comet);
    ASSERT_TRUE(cometIt != sent.end() && cometIt->second && cometIt->second->is<Mission::FallBack>()); // the Comet raids on
    const auto dropIt = sent.find(dropship);
    ASSERT_TRUE(dropIt != sent.end() && dropIt->second && dropIt->second->is<Mission::Drop>()); // the drop flies on
    EXPECT_TRUE(distance(dropIt->second->as<Mission::Drop>()->at, blue) < 15);

    const int64_t ride = add(down, Unit::Kind::dropship, line);
    down.units.back().mission = Mission::Raid{line};
    std::set<int64_t> squad;
    for (int k = 0; k < 4; k++) {
        squad.insert(add(down, Unit::Kind::ranger, line + Vec2(static_cast<double>(k) - 1.5, 0.5)));
        down.units.back().mission = Mission::Raid{line};
    }
    std::set<int64_t> lifted;
    for (const auto& o : c.drops(Simulation(down, m))) {
        if (const auto* b = o.as<Command::Board>(); b && b->dropship == ride && b->unit) { lifted.insert(*b->unit); }
    }
    EXPECT_TRUE(lifted == squad);
}
