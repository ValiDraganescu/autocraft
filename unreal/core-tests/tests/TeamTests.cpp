// Teams and eight players (C++ only, PORTING.md "Teams"): allies hold fire
// and share their sight, a team wins once every enemy team is gone, the
// square maps for 4 and 8 players, the AI's choice of the enemy team, and
// saves. Hand-built positions and single AI calls, never a game; the timing
// check steps one minute from the start.
#include "SimHelpers.h"

#include "NavGrid.h"
#include "Router.h"
#include "Session.h"
#include "TerrainField.h"
#include "WindowMaps.h"

#include <chrono>
#include <cstdlib>
#include <set>
#include <string>

using namespace ac;
using namespace simtest;

namespace {

/// A square map, made once per choice (building one and its walking grid
/// takes a while).
const MapDefinition& square(MapStyle style, int64_t players, MapSize size = MapSize::small) {
    static std::map<std::string, MapDefinition> made;
    const MapChoice choice{style, size, players};
    auto it = made.find(choice.name());
    if (it == made.end()) it = made.emplace(choice.name(), WindowMaps::build(choice)).first;
    return it->second;
}

std::vector<int64_t> fourVersusFour() { return {0, 0, 0, 0, 1, 1, 1, 1}; }

/// The image of `p` under each of the square's eight symmetries.
std::vector<Vec2> images(Vec2 p) {
    return {p, Vec2(-p.y, p.x), Vec2(-p.x, -p.y), Vec2(p.y, -p.x), Vec2(p.y, p.x), Vec2(-p.x, p.y), Vec2(-p.y, -p.x), Vec2(p.x, -p.y)};
}

Vec2 startOf(const GameState& s, const MapDefinition& m, int64_t p) {
    return m.bases[static_cast<size_t>(*s.players[static_cast<size_t>(p)].start)].center;
}

} // namespace

// MARK: - Hostility

/// With no teams set, every player is its own team: `hostile` is `!=`.
TEST(teams_noTeamsIsEveryPlayerForItself) {
    GameState s = bare(3, 0, {}, {}, 1);
    for (int64_t a = -1; a < 4; a++) {
        for (int64_t b = -1; b < 4; b++) EXPECT_EQ(s.hostile(a, b), a != b);
    }
    EXPECT_TRUE(!s.teams().has_value());
    s.players[0].team = 0;
    s.players[1].team = 0;
    s.players[2].team = 1;
    EXPECT_TRUE(s.allied(0, 1));
    EXPECT_TRUE(s.hostile(1, 2));
    EXPECT_TRUE(s.teams() == std::vector<int64_t>({0, 0, 1}));
}

/// Allies side by side never shoot each other; the enemy next to them is
/// shot at once.
TEST(teams_alliesDoNotShootEachOther) {
    GameState s = bare(4, 0, {}, {}, 1);
    const std::vector<int64_t> teams{0, 0, 1, 1};
    for (size_t p = 0; p < 4; p++) s.players[p].team = teams[p];
    // A Citadel each far away, so no one has won.
    for (int64_t p = 0; p < 4; p++) addStructure(s, Structure::Kind::citadel, Vec2(-60.0 + 40.0 * static_cast<double>(p), 40), p);
    const int64_t a = addUnit(s, Unit::Kind::ranger, Vec2(0, 0), 0);
    const int64_t b = addUnit(s, Unit::Kind::ranger, Vec2(1.5, 0), 1);
    const int64_t c = addUnit(s, Unit::Kind::juggernaut, Vec2(0, 1.5), 1);
    Simulation sim(s);
    auto events = run(sim, 3);
    EXPECT_EQ(shots(events, a), int64_t(0));
    EXPECT_EQ(shots(events, b), int64_t(0));
    EXPECT_EQ(shots(events, c), int64_t(0));
    EXPECT_EQ(hp(sim, a), Rules::hp(Unit::Kind::ranger));
    EXPECT_EQ(hp(sim, b), Rules::hp(Unit::Kind::ranger));
    EXPECT_TRUE(!sim.state.winner.has_value());
    // An enemy walks up between them: both allies shoot it.
    GameState t = sim.state;
    const Vec2 between = (t.unit(a)->position + t.unit(b)->position) / 2;
    const int64_t e = addUnit(t, Unit::Kind::juggernaut, between + Vec2(0.5, 0.5), 2);
    Simulation fight(t);
    events = run(fight, 2);
    EXPECT_TRUE(shots(events, a) > 0);
    EXPECT_TRUE(shots(events, b) > 0);
    EXPECT_TRUE(hp(fight, e) < Rules::hp(Unit::Kind::juggernaut));
    for (const auto& ev : events) {
        if (auto shot = ev.as<GameEvent::Shot>(); shot && shot->unit != e) EXPECT_EQ(shot->victim, int64_t(2));
    }
}

/// A unit of an ally sees for the whole team: what it sees, its allies see
/// and know; with no teams the same enemy stays in the fog.
TEST(teams_alliedVisionIsShared) {
    const MapDefinition& m = square(MapStyle::openField, 4);
    for (bool teamed : {true, false}) {
        GameState s = GameState::new_(m, std::nullopt, 0, teamed ? std::optional(std::vector<int64_t>{0, 0, 1, 1}) : std::nullopt);
        s.units.clear();
        // Player 1's scout out in the middle with an enemy beside it, far
        // from anything of player 0's.
        const Vec2 spot = Vec2(4, -6);
        const int64_t scout = addUnit(s, Unit::Kind::ranger, spot, 1);
        const int64_t enemy = addUnit(s, Unit::Kind::ranger, spot + Vec2(3, 0), 2);
        for (const auto& b : s.structures) {
            if (b.owner == 0) ASSERT_TRUE(distance(b.position, spot) > 30);
        }
        Simulation sim(s, m);
        sim.lookNow();
        EXPECT_EQ(sim.sees(1, enemy), true);
        EXPECT_EQ(sim.sees(0, enemy), teamed);
        EXPECT_EQ(sim.sees(0, spot + Vec2(3, 0)), teamed);
        // Allies' own things are always in sight, never in the intel of the others.
        EXPECT_EQ(sim.sees(0, scout), teamed);
        const Intel k = *sim.intel(0);
        bool knows = false;
        for (const auto& seen : k.units) knows = knows || seen.unit.id == enemy;
        EXPECT_EQ(knows, teamed);
        bool allyInIntel = false;
        for (const auto& seen : k.units) allyInIntel = allyInIntel || seen.unit.id == scout;
        EXPECT_EQ(allyInIntel, false);
        // Player 0's AI plays from what the team sees.
        const GameState known = sim.known(0);
        EXPECT_EQ(known.unit(enemy).has_value(), teamed);
        EXPECT_EQ(known.unit(scout).has_value(), teamed);
        // The enemy beside the scout sees it, and so does its ally (3) with teams.
        EXPECT_EQ(sim.sees(2, scout), true);
        EXPECT_EQ(sim.sees(3, scout), teamed);
    }
}

// MARK: - Victory

/// Three teams: losing one team, or one player of a team, ends nothing; the
/// game ends when one team is left, and every player of it scores.
TEST(teams_aTeamWinsOnlyWhenEveryEnemyTeamIsGone) {
    GameState s = bare(6, 0, {}, {}, 1);
    const std::vector<int64_t> teams{0, 0, 1, 1, 2, 2};
    for (size_t p = 0; p < 6; p++) s.players[p].team = teams[p];
    std::vector<int64_t> citadel;
    for (int64_t p = 0; p < 6; p++) {
        citadel.push_back(addStructure(s, Structure::Kind::citadel, Vec2(-80.0 + 30.0 * static_cast<double>(p), 0), p));
    }
    Simulation sim(s);
    auto raze = [&](int64_t p) {
        for (auto& b : sim.state.structures) if (b.id == citadel[static_cast<size_t>(p)]) b.hp = 0;
        return run(sim, 0.2);
    };
    auto victories = [](const std::vector<GameEvent>& events) {
        int64_t n = 0;
        for (const auto& e : events) n += e.is<GameEvent::Victory>() ? 1 : 0;
        return n;
    };
    // Team 2 is wiped out: teams 0 and 1 still stand.
    EXPECT_EQ(victories(raze(4)), int64_t(0));
    EXPECT_EQ(victories(raze(5)), int64_t(0));
    EXPECT_TRUE(!sim.state.winner && !sim.state.endedAt);
    // Player 0 and player 2 fall, their allies stand.
    EXPECT_EQ(victories(raze(0)), int64_t(0));
    EXPECT_EQ(victories(raze(2)), int64_t(0));
    EXPECT_TRUE(!sim.state.winner && !sim.state.endedAt);
    // The last of team 1: team 0 wins, through player 1, its last one standing.
    const auto events = raze(3);
    EXPECT_EQ(victories(events), int64_t(1));
    EXPECT_TRUE(sim.state.winner == int64_t(1));
    EXPECT_TRUE(sim.state.score == std::vector<int64_t>({1, 1, 0, 0, 0, 0}));
}

// MARK: - Maps

/// Every square map: the starts its players need, every base the image of
/// another under the square's symmetries, flat and far apart, and a game of
/// teams on it with a Citadel on each start, every base reachable on foot
/// from every start. All sizes with AUTOCRAFT_MAP_TESTS=1, small otherwise.
TEST(teams_squareMapsAreFairAndEveryBaseReachable) {
    const char* slow = std::getenv("AUTOCRAFT_MAP_TESTS");
    const bool all = slow && std::string(slow) == "1";
    for (int64_t players : {4, 8}) {
        for (MapStyle style : allCases<MapStyle>()) {
            for (MapSize size : allCases<MapSize>()) {
                if (!all && size != MapSize::small) continue;
                const MapDefinition& m = square(style, players, size);
                const TerrainField field(m);
                EXPECT_EQ(m.starts.size(), size_t(players));
                EXPECT_TRUE(m.squareSymmetric == true);
                EXPECT_TRUE(!m.mirrorX.has_value());
                EXPECT_TRUE(m.bases.size() >= size_t(2 * players));
                // Every base has its images, and the starts are images of each other.
                for (const BaseSite& b : m.bases) {
                    for (Vec2 q : images(b.center)) {
                        bool found = false;
                        for (const BaseSite& o : m.bases) found = found || distance(o.center, q) < 1e-6;
                        EXPECT_TRUE(found);
                    }
                    EXPECT_EQ(b.ore.size(), size_t(8));
                    EXPECT_EQ(b.wells.size(), size_t(2));
                    const double h = static_cast<double>(b.level) * m.levelHeight;
                    for (int k = 0; k < 16; k++) {
                        const double a = static_cast<double>(k) / 16 * 2 * pi;
                        for (double r : {0.0, 6.0, 11.0}) EXPECT_NEAR(field.height(b.center + Vec2(std::cos(a), std::sin(a)) * r), h, 0.3);
                    }
                }
                for (size_t i = 0; i < m.bases.size(); i++) {
                    for (size_t j = i + 1; j < m.bases.size(); j++) EXPECT_TRUE(distance(m.bases[i].center, m.bases[j].center) >= 20);
                }
                const double r0 = length(m.bases[size_t(m.starts[0])].center);
                for (int64_t st : m.starts) EXPECT_NEAR(length(m.bases[size_t(st)].center), r0, 1e-9);
                // The terrain is the same at a point and at its images.
                for (Vec2 p : {Vec2(17.3, 5.1), Vec2(40.2, 33.7), m.bases[size_t(m.starts[0])].center + Vec2(3, 1)}) {
                    for (Vec2 q : images(p)) EXPECT_EQ(field.height(q), field.height(p));
                }
                // A game of teams: each start once, every base reachable from each.
                std::vector<int64_t> teams;
                for (int64_t p = 0; p < players; p++) teams.push_back(p < players / 2 ? 0 : 1);
                const GameState s = GameState::new_(m, std::nullopt, 0, teams);
                EXPECT_EQ(s.players.size(), size_t(players));
                std::set<int64_t> used;
                for (const auto& p : s.players) used.insert(*p.start);
                EXPECT_EQ(used.size(), size_t(players));
                int64_t citadels = 0;
                for (const auto& b : s.structures) citadels += b.kind == Structure::Kind::citadel ? 1 : 0;
                EXPECT_EQ(citadels, players);
                const NavGrid nav(m, field);
                const auto region = nav.region(startOf(s, m, 0));
                EXPECT_TRUE(region.has_value());
                for (const BaseSite& b : m.bases) EXPECT_TRUE(nav.region(b.center) == region);
                for (int64_t p = 0; p < players; p++) {
                    for (int64_t q = 0; q < players; q++) {
                        if (p != q) EXPECT_TRUE(nav.path(startOf(s, m, p), startOf(s, m, q), 0.5).has_value());
                    }
                }
                for (const BaseSite& b : m.bases) EXPECT_TRUE(nav.path(startOf(s, m, 0), b.center, 0.5).has_value());
            }
        }
    }
}

/// Teams start on neighbouring starts round the map: 4v4 on eight starts
/// takes two halves, 2v2v2v2 four quarters, 1v1 opposite starts; the deal
/// is the seed's, and the same seed deals the same.
TEST(teams_teamsStartTogether) {
    const MapDefinition& m = square(MapStyle::openField, 8);
    auto ringIndex = [&](const GameState& s, int64_t p) {
        for (size_t i = 0; i < m.starts.size(); i++) if (m.starts[i] == *s.players[static_cast<size_t>(p)].start) return int64_t(i);
        return int64_t(-1);
    };
    // Starts in a run round the ring of 8: the team's own in a row.
    auto contiguous = [&](const GameState& s, const std::vector<int64_t>& teams, int64_t team) {
        std::vector<bool> taken(8, false);
        int64_t size = 0;
        for (size_t p = 0; p < teams.size(); p++) if (teams[p] == team) { taken[size_t(ringIndex(s, int64_t(p)))] = true; size++; }
        int64_t runs = 0;
        for (int64_t i = 0; i < 8; i++) runs += taken[size_t(i)] && !taken[size_t((i + 7) % 8)] ? 1 : 0;
        return size == 8 || runs == 1;
    };
    for (int64_t round = 0; round < 6; round++) {
        const auto halves = fourVersusFour();
        const GameState a = GameState::new_(m, std::nullopt, round, halves);
        EXPECT_TRUE(contiguous(a, halves, 0) && contiguous(a, halves, 1));
        EXPECT_TRUE(a == GameState::new_(m, std::nullopt, round, halves));
        const std::vector<int64_t> pairs{0, 0, 1, 1, 2, 2, 3, 3};
        const GameState b = GameState::new_(m, std::nullopt, round, pairs);
        for (int64_t t = 0; t < 4; t++) EXPECT_TRUE(contiguous(b, pairs, t));
        const GameState duel = GameState::new_(m, std::nullopt, round, std::vector<int64_t>{0, 1});
        EXPECT_EQ(duel.players.size(), size_t(2));
        EXPECT_EQ((ringIndex(duel, 0) - ringIndex(duel, 1) + 8) % 8, int64_t(4));
        for (size_t p = 0; p < 8; p++) EXPECT_TRUE(a.players[p].team == halves[p]);
    }
    // Free for all on the square map: every start taken, no teams set.
    const GameState ffa = GameState::new_(m);
    EXPECT_EQ(ffa.players.size(), size_t(8));
    EXPECT_TRUE(!ffa.teams().has_value());
}

// MARK: - The AI

/// The AI attacks a base of an enemy team, never an ally's; both players of
/// a team pick the same enemy team; and an ally's attack is joined.
TEST(teams_aiAttacksAnEnemyTeamsBase) {
    const MapDefinition& m = square(MapStyle::openField, 8);
    const std::vector<int64_t> pairs{0, 0, 1, 1, 2, 2, 3, 3};
    GameState s = GameState::new_(m, std::nullopt, 0, pairs);
    s.units.clear();
    s.time = 300;
    // Twelve Rangers of player 0 at home: a wave ready to go.
    const Vec2 home = startOf(s, m, 0);
    const Vec2 out = home + normalize(-home) * 12;
    for (int64_t k = 0; k < 14; k++) {
        addUnit(s, Unit::Kind::ranger, out + Vec2(static_cast<double>(k % 4), static_cast<double>(k / 4)), 0);
    }
    // Ten Rangers of player 4 at home: odds a wave of 14 takes, seven do not.
    const Vec2 far = startOf(s, m, 4);
    for (int64_t k = 0; k < 10; k++) {
        addUnit(s, Unit::Kind::ranger, far + normalize(-far) * 10 + Vec2(static_cast<double>(k % 4), static_cast<double>(k / 4)), 4);
    }
    s.players[0].aggression = 1;
    const Simulation sim(s, m, false);
    const Commander c0(m, 0), c1(m, 1);
    const auto aim0 = c0.focus(sim.state), aim1 = c1.focus(sim.state);
    ASSERT_TRUE(aim0.has_value());
    EXPECT_TRUE(aim0 == aim1);
    EXPECT_TRUE(*aim0 != s.team(0));
    const auto target = c0.attackTarget(sim, out);
    ASSERT_TRUE(target.has_value());
    std::optional<int64_t> owner;
    for (const auto& b : s.structures) if (distance(b.position, *target) < 1e-9) owner = b.owner;
    ASSERT_TRUE(owner.has_value());
    EXPECT_TRUE(s.hostile(*owner, 0));
    EXPECT_EQ(s.team(*owner), *aim0);
    // The army goes for that team's base.
    bool attacks = false;
    for (const Command& cmd : c0.army(sim)) {
        if (auto a = cmd.as<Command::Attack>(); a && a->player == 0 && a->at) {
            attacks = true;
            std::optional<int64_t> near;
            for (const auto& b : s.structures) if (distance(b.position, *a->at) < 12) near = b.owner;
            EXPECT_TRUE(near.has_value() && s.hostile(*near, 0));
        }
    }
    EXPECT_TRUE(attacks);
    // Player 1, with an army too small for a wave of its own, joins player
    // 0's attack at its point.
    GameState j = s;
    std::erase_if(j.units, [](const Unit& u) { return u.owner == 0; });
    const Vec2 home1 = startOf(j, m, 1);
    const Vec2 out1 = home1 + normalize(-home1) * 12;
    for (int64_t k = 0; k < 7; k++) addUnit(j, Unit::Kind::ranger, out1 + Vec2(static_cast<double>(k % 4), static_cast<double>(k / 4)), 1);
    j.players[0].attack = *target;
    j.players[1].aggression = 0;
    const Simulation joined(j, m, false);
    bool joins = false;
    for (const Command& cmd : c1.army(joined)) {
        if (auto a = cmd.as<Command::Attack>(); a && a->player == 1 && a->at && distance(*a->at, *target) < 1e-9) joins = true;
    }
    EXPECT_TRUE(joins);
    // Without the ally on the attack it waits for its wave.
    j.players[0].attack = std::nullopt;
    const Simulation alone(j, m, false);
    bool goes = false;
    for (const Command& cmd : c1.army(alone)) {
        if (auto a = cmd.as<Command::Attack>(); a && a->at) goes = true;
    }
    EXPECT_TRUE(!goes);
}

/// An ally's base under a real attack is defended by an army big enough
/// (`allyJoin`) with no attack of its own; not a raid, not on hold, not
/// with no teams; and the defenders go home once that base is clear.
TEST(teams_aiDefendsAnAllysBase) {
    const MapDefinition& m = square(MapStyle::openField, 8);
    const std::vector<int64_t> pairs{0, 0, 1, 1, 2, 2, 3, 3};
    GameState s = GameState::new_(m, std::nullopt, 0, pairs);
    s.units.clear();
    s.time = 300;
    // Player 1's army at home: eight Rangers.
    const Vec2 home1 = startOf(s, m, 1);
    const Vec2 out1 = home1 + normalize(-home1) * 12;
    for (int64_t k = 0; k < 8; k++) addUnit(s, Unit::Kind::ranger, out1 + Vec2(static_cast<double>(k % 4), static_cast<double>(k / 4)), 1);
    // Ten Rangers of player 4 (another team) in its ally player 0's base.
    const Vec2 home0 = startOf(s, m, 0);
    auto raid = [&](GameState& g, int64_t count, int64_t owner) {
        for (int64_t k = 0; k < count; k++) {
            addUnit(g, Unit::Kind::ranger, home0 + normalize(-home0) * 6 + Vec2(static_cast<double>(k % 4), static_cast<double>(k / 4)), owner);
        }
    };
    const Commander c1(m, 1);
    auto defends = [&](const GameState& g) -> std::optional<Vec2> {
        const Simulation sim(g, m, false);
        for (const Command& cmd : c1.army(sim)) {
            if (auto d = cmd.as<Command::Defend>(); d && d->player == 1) return d->at;
        }
        return std::nullopt;
    };
    GameState a = s;
    raid(a, 10, 4);
    const auto at = defends(a);
    ASSERT_TRUE(at.has_value());
    EXPECT_TRUE(distance(*at, home0) < 12);
    // A raid of three is the ally's own business.
    GameState r = s;
    raid(r, 3, 4);
    EXPECT_TRUE(!defends(r).has_value());
    // On hold its army stays home.
    GameState h = a;
    Directives d;
    d.stance = Stance::hold;
    h.players[1].directives = d;
    EXPECT_TRUE(!defends(h).has_value());
    // An army under `allyJoin` stays home too.
    GameState small = a;
    std::erase_if(small.units, [](const Unit& u) { return u.owner == 1; });
    for (int64_t k = 0; k < 3; k++) addUnit(small, Unit::Kind::ranger, out1 + Vec2(static_cast<double>(k), 0), 1);
    EXPECT_TRUE(!defends(small).has_value());
    // With no teams player 0 is no ally: nothing to defend.
    GameState f = a;
    for (auto& p : f.players) p.team = std::nullopt;
    EXPECT_TRUE(!defends(f).has_value());
    // The base clear again, the defenders go home.
    GameState c = s;
    c.players[1].attack = *at;
    c.players[1].defending = true;
    const Simulation clear(c, m, false);
    bool home = false;
    for (const Command& cmd : c1.army(clear)) {
        if (auto x = cmd.as<Command::Attack>(); x && x->player == 1 && !x->at) home = true;
    }
    EXPECT_TRUE(home);
}

// MARK: - Saves

/// A game of teams on a square map saves and loads back the same, map
/// included; a save without teams loads with none.
TEST(teams_savesWithTeamsRoundTrip) {
    const MapDefinition& m = square(MapStyle::highlands, 8);
    const GameState s = GameState::new_(m, std::vector<int64_t>{1, 1, 1, 1, 0, 0, 0, 0}, 0, fourVersusFour());
    const std::string text = SessionStore::encode(s);
    EXPECT_TRUE(text.find("\"team\"") != std::string::npos);
    const auto back = SessionStore::decode<GameState>(text);
    ASSERT_TRUE(back.has_value());
    EXPECT_TRUE(*back == s);
    EXPECT_TRUE(back->teams() == fourVersusFour());
    EXPECT_EQ(SessionStore::encode(*back), text);
    const auto map = SessionStore::decode<MapDefinition>(SessionStore::encode(m));
    ASSERT_TRUE(map.has_value());
    EXPECT_TRUE(*map == m);
    // No teams: nothing written, nothing read.
    const GameState plain = GameState::new_(m);
    const std::string none = SessionStore::encode(plain);
    EXPECT_TRUE(none.find("\"team\"") == std::string::npos);
    const auto plainBack = SessionStore::decode<GameState>(none);
    ASSERT_TRUE(plainBack.has_value());
    EXPECT_TRUE(!plainBack->teams().has_value());
    // The next game keeps the teams and the tally.
    Simulation sim(s, m, std::vector<Commander>{}, false);
    sim.newGame();
    EXPECT_TRUE(sim.state.teams() == fourVersusFour());
    EXPECT_TRUE(sim.state.score == s.score);
}

// MARK: - Timing

/// Not a game: one minute from the start of a 4v4 on the medium eight-player
/// map, every AI on and the fog as in a window game, stepped at 60 Hz. Prints
/// the time a step takes.
TEST(teams_fourVersusFourMinuteTiming) {
    const MapDefinition& m = square(MapStyle::highlands, 8, MapSize::medium);
    Simulation sim(GameState::new_(m, std::nullopt, 0, fourVersusFour()), m, Commander::all(m));
    const int64_t steps = 60 * 60;
    double slowest = 0;
    const auto begin = std::chrono::steady_clock::now();
    for (int64_t k = 0; k < steps; k++) {
        const auto a = std::chrono::steady_clock::now();
        (void)sim.step(1.0 / 60);
        slowest = std::max(slowest, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count());
    }
    const double total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    std::printf("    4v4, %s, 60 s at 60 Hz: %.3f ms/step on average, slowest %.2f ms (%lld units at the end)\n", m.name.c_str(),
                total / static_cast<double>(steps), slowest, static_cast<long long>(sim.state.units.size()));
    EXPECT_TRUE(sim.state.time > 59.9);
    EXPECT_TRUE(!sim.state.winner.has_value());
}
