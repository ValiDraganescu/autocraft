// Port of Tests/GameCoreTests/DirectivesTests.swift: the humans' orders to
// their team's AI (`Directives`): one `orders` call or a few seconds of
// simulation from a hand-built position each, never a game.
// (`testBoxedInTankIsDestroyed` is the simulation's: NavGrid and Simulation.)
#include "SquadHelpers.h"

using namespace ac;
using namespace squadtest;

namespace {

int64_t requestID(const Simulation& sim, int64_t player = 0) {
    return sim.state.players[static_cast<size_t>(player)].orders().queue.front().id;
}

Command request(int64_t player, Request::What what, int64_t count = 1, std::optional<int64_t> at = std::nullopt) {
    return Command::Request{player, what, count, false, at, std::nullopt};
}

} // namespace

// MARK: - The queue

/// The same unit asked for again at the same building adds to the
/// last request; a cancel takes it off; an upgrade the team has is
/// refused.
TEST(directivesRequestsMergeAndCancel) {
    const MapDefinition& m = homeMap();
    auto [s, garrison] = blueTech(m);
    Simulation sim(s, m);
    EXPECT_TRUE(sim.issue(request(0, Request::What::Unit{Unit::Kind::ranger}, 1, garrison)));
    EXPECT_TRUE(sim.issue(request(0, Request::What::Unit{Unit::Kind::ranger}, 2, garrison)));
    EXPECT_TRUE(sim.issue(request(0, Request::What::Building{Structure::Kind::bastion})));
    const auto q = sim.state.players[0].orders().queue;
    ASSERT_TRUE(q.size() == 2);
    EXPECT_EQ(q[0].count, 3);
    EXPECT_EQ(q[1].count, 1);
    EXPECT_TRUE(sim.issue(Command::CancelRequest{0, q[0].id}));
    const auto left = sim.state.players[0].orders().queue;
    ASSERT_TRUE(left.size() == 1);
    EXPECT_TRUE(left[0].what == Request::What(Request::What::Building{Structure::Kind::bastion}));
    EXPECT_TRUE(sim.state.players[1].orders().queue.empty()); // each team has its own queue
    sim.state.players[0].upgrades = std::set<Upgrade>{Upgrade::minigun};
    EXPECT_TRUE(!sim.issue(request(0, Request::What::Upgrade{Upgrade::minigun})));
}

/// A queued upgrade it cannot pay for yet holds the bank: no Ranger
/// out of the money it saves. Once it can pay, the lab researches it
/// and the request leaves the queue.
TEST(directivesQueuedUpgradeHoldsTheBankThenResearches) {
    const MapDefinition& m = homeMap();
    auto [s, garrison] = blueTech(m);
    addLab(s, garrison);
    const int64_t lab = *s.structure(garrison)->addon;
    s.players[0].ore = 90;
    s.players[0].hydrogen = 100;
    Simulation sim(s, m);
    sim.issue(request(0, Request::What::Upgrade{Upgrade::aegisShield}));
    const Commander c(m);
    const auto saving = c.orders(sim);
    EXPECT_TRUE(std::none_of(saving.begin(), saving.end(), [&](const Command& o) {
        const auto* t = o.as<Command::Train>();
        return t && t->structure == garrison;
    })); // no Ranger
    const auto status = c.requestStatus(sim);
    ASSERT_TRUE(!status.empty());
    EXPECT_EQ(std::string(status.front().text), std::string("Saving: 10 ore short"));

    sim.state.players[0].ore = 100;
    const auto orders = c.orders(sim);
    const auto paid = served(orders);
    ASSERT_TRUE(paid.size() == 1);
    EXPECT_TRUE(paid[0] == Command(Command::Research{lab, Upgrade::aegisShield}));
    for (const auto& o : orders) { sim.issue(o); }
    EXPECT_TRUE(sim.state.players[0].orders().queue.empty());
    EXPECT_TRUE(sim.state.structure(lab)->research == std::optional<Upgrade>(Upgrade::aegisShield));
}

/// A queued Longbow with only a Garrison: a Foundry first (one,
/// though the AI wanted one too), then its Lab, then the tank.
TEST(directivesQueuedTankPutsUpWhatItNeedsFirst) {
    const MapDefinition& m = homeMap();
    auto [s, garrison] = blueTech(m);
    (void)garrison;
    const Commander c(m);
    Simulation sim(s, m);
    sim.issue(request(0, Request::What::Unit{Unit::Kind::longbow}));
    EXPECT_EQ(std::string(c.requestStatus(sim).front().text), std::string("Foundry first"));
    int64_t foundries = 0;
    for (const auto& o : c.orders(sim)) {
        const Command inner = Commander::inner(o);
        if (const auto* b = inner.as<Command::Build>(); b && b->kind == Structure::Kind::foundry) { foundries++; }
    }
    EXPECT_EQ(foundries, 1);

    s = sim.state;
    const int64_t foundry = add(s, Structure::Kind::foundry, *c.garrisonSpot(s, s.structures[0]));
    sim = Simulation(s, m);
    EXPECT_EQ(std::string(c.requestStatus(sim).front().text), std::string("Lab first"));
    EXPECT_TRUE(contains(c.orders(sim), Command::Addon{foundry}));

    addLab(s, foundry);
    sim = Simulation(s, m);
    const int64_t id = requestID(sim);
    const auto orders = c.orders(sim);
    EXPECT_TRUE(contains(orders, Command::Serve{0, id, Command(Command::Train{foundry, Unit::Kind::longbow})}));
    for (const auto& o : orders) { sim.issue(o); }
    EXPECT_TRUE(sim.state.players[0].orders().queue.empty());
}

/// A building request: the AI picks the spot and sends a Prospector.
TEST(directivesQueuedBuildingIsPlacedByTheAI) {
    const MapDefinition& m = homeMap();
    auto [s, garrison] = blueTech(m);
    (void)garrison;
    Simulation sim(s, m);
    sim.issue(request(0, Request::What::Building{Structure::Kind::garrison}));
    const int64_t id = requestID(sim);
    const auto orders = Commander(m).orders(sim);
    bool found = false;
    for (const auto& o : orders) {
        const auto* sv = o.as<Command::Serve>();
        if (!(sv && sv->player == 0 && sv->request == id)) { continue; }
        const auto* b = sv->command->as<Command::Build>();
        found = b && b->kind == Structure::Kind::garrison;
        break;
    }
    EXPECT_TRUE(found); // a Garrison for the request
}

// MARK: - Bank and split

/// With a floor over the whole bank the AI spends nothing for itself.
TEST(directivesBankFloorHoldsTheAIsOwnSpending) {
    const MapDefinition& m = homeMap();
    auto [s, garrison] = blueTech(m);
    (void)garrison;
    Simulation sim(s, m);
    sim.issue(Command::Keep{0, 1000, 500});
    int64_t paid = 0;
    for (const auto& o : Commander(m).orders(sim)) {
        if (o.is<Command::Train>() || o.is<Command::Addon>() || o.is<Command::Research>()) { paid++; }
        if (const auto* b = o.as<Command::Build>(); b && b->kind != Structure::Kind::habDome) { paid++; }
    }
    EXPECT_EQ(paid, 0);
}

/// Seven Prospectors, Derricks standing: on the AI's own split they all
/// mine ore; on MH they fill the Derricks.
TEST(directivesHydrogenSplitFillsTheDerricks) {
    const MapDefinition& m = homeMap();
    auto [s, garrison] = blueTech(m);
    (void)garrison;
    const Commander c(m);
    auto toDerrick = [&](const GameState& t) {
        int64_t n = 0;
        for (const auto& o : c.workers(Simulation(t, m), {})) { if (o.is<Command::Harvest>()) { n++; } }
        return n;
    };
    EXPECT_EQ(toDerrick(s), 0);
    s.players[0].directives = Directives();
    s.players[0].directives->harvest = Harvest::hydrogen;
    EXPECT_TRUE(toDerrick(s) > 0);
    s.players[0].directives->harvest = Harvest::ore;
    EXPECT_EQ(toDerrick(s), 0);
}
