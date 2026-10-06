// The squads golden (`Tests/GameCoreTests/GoldenSquadsTests.swift`,
// golden/squads.json): the Commander extensions' calls (Early, Intel,
// Objectives, Requests) for every player on the bench fixture and a fresh,
// teched game on Badlands large, with objectives and a queue given by hand.
// One call each, never a game played.
#include "test.h"
#include "golden.h"

#include "Commander.h"
#include "Session.h"
#include "Simulation.h"
#include "WindowMaps.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace ac;

namespace {

std::string num(double x) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.17g", x);
    return buf;
}
std::string vec(std::optional<Vec2> p) { return p ? num(p->x) + "," + num(p->y) : "-"; }
std::string opt(std::optional<int64_t> i) { return i ? std::to_string(*i) : "-"; }
std::string str(std::string_view s) { return std::string(s); }

std::string describe(const std::optional<Mission>& m) {
    if (!m) { return "-"; }
    if (auto c = m->as<Mission::Raid>()) { return "raid " + vec(c->at); }
    if (auto c = m->as<Mission::Drop>()) { return "drop " + vec(c->at); }
    if (auto c = m->as<Mission::FallBack>()) { return "fallBack " + vec(c->at); }
    if (auto c = m->as<Mission::Hunt>()) { return "hunt " + vec(c->at); }
    if (auto c = m->as<Mission::Assault>()) { return "assault " + std::to_string(c->objective) + " " + vec(c->at); }
    if (auto c = m->as<Mission::Hold>()) { return "hold " + std::to_string(c->objective) + " " + vec(c->at); }
    if (auto c = m->as<Mission::Regroup>()) { return "regroup " + std::to_string(c->objective) + " " + vec(c->at); }
    return "?";
}

std::string describe(const Command& c) {
    if (auto o = c.as<Command::Train>()) { return "train " + std::to_string(o->structure) + " " + (o->kind ? str(rawValue(*o->kind)) : "-"); }
    if (auto o = c.as<Command::Build>()) { return "build " + std::to_string(o->worker) + " " + str(rawValue(o->kind)) + " " + vec(o->at); }
    if (auto o = c.as<Command::Addon>()) { return "addon " + std::to_string(o->structure); }
    if (auto o = c.as<Command::Mission>()) { return "mission " + std::to_string(o->unit) + " " + describe(o->mission); }
    if (auto o = c.as<Command::Research>()) { return "research " + std::to_string(o->structure) + " " + str(rawValue(o->upgrade)); }
    if (auto o = c.as<Command::Retask>()) {
        return "retask " + std::to_string(o->player) + " " + std::to_string(o->id) + " " + str(rawValue(o->kind));
    }
    if (auto o = c.as<Command::CancelRequest>()) { return "cancelRequest " + std::to_string(o->player) + " " + std::to_string(o->id); }
    if (auto o = c.as<Command::CancelObjective>()) { return "cancelObjective " + std::to_string(o->player) + " " + std::to_string(o->id); }
    if (auto o = c.as<Command::Serve>()) {
        return "serve " + std::to_string(o->player) + " " + std::to_string(o->request) + " [" + describe(*o->command) + "]";
    }
    return "other";
}

std::vector<std::string> describeAll(const std::vector<Command>& cs) {
    std::vector<std::string> out;
    for (const auto& c : cs) { out.push_back(describe(c)); }
    return out;
}

std::string joined(const std::vector<int64_t>& ids) {
    std::string s;
    for (size_t i = 0; i < ids.size(); i++) { s += (i > 0 ? "," : "") + std::to_string(ids[i]); }
    return s;
}

/// Every player's view, as `dump` of the Swift golden.
golden::json dump(const GameState& state, const MapDefinition& m) {
    const Simulation sim(state, m);
    golden::json out = golden::json::array();
    for (int64_t p = 0; p < static_cast<int64_t>(state.players.size()); p++) {
        Commander c(m, p);
        c.frontDefence = false; // the Swift golden's placement
        const Simulation seen = sim.seen(p);
        const auto mix = c.enemyMix(seen.state);
        const auto k = c.counters(mix);
        const Vec2 front = c.start(state, p);
        const Player& me = state.players[static_cast<size_t>(p)];
        int64_t money = me.ore, hydrogen = me.hydrogen, supply = me.supplyCap - me.supplyUsed;
        std::set<int64_t> busy;
        const auto r = c.requests(seen, money, hydrogen, supply, busy);
        const auto plan = c.plan(seen);
        std::vector<std::string> sentinels;
        for (const auto& b : state.structures) {
            if (b.owner == p && b.kind == Structure::Kind::citadel) { sentinels.push_back(vec(c.sentinelSpot(state, b))); }
        }
        std::vector<Unit> mine;
        for (const auto& u : state.units) { if (u.owner == p && u.soldier()) { mine.push_back(u); } }
        std::vector<std::string> objectives, manning, sites, status;
        for (const auto& o : plan.status) {
            objectives.push_back(std::to_string(o.id) + " " + o.text + " [" + joined(o.units) + "] " + vec(o.centre));
        }
        for (const auto& mn : c.manning(seen)) {
            std::vector<int64_t> send;
            for (const auto& u : mn.send) { send.push_back(u.id); }
            manning.push_back(std::to_string(mn.objective.id) + " " + opt(mn.bastion ? std::optional<int64_t>(mn.bastion->id) : std::nullopt)
                              + " " + std::to_string(mn.inside) + " " + std::to_string(mn.coming) + " [" + joined(send) + "] "
                              + std::to_string(mn.short_));
        }
        for (const auto& si : c.siteInfo(sim)) {
            sites.push_back(std::to_string(si.index) + " " + vec(si.centre) + " " + opt(si.owner) + " "
                            + (si.contested ? "true" : "false") + " " + (si.next ? "true" : "false"));
        }
        for (const auto& st : r.status) { status.push_back(std::to_string(st.id) + " " + st.text + " " + num(st.funded)); }
        std::vector<Command> twice = r.out;
        twice.insert(twice.end(), r.out.begin(), r.out.end());
        golden::json j;
        j["scouts"] = describeAll(c.scouts(seen, {}));
        j["militia"] = describeAll(c.militia(seen, {}));
        j["mix"] = {mix.bio, mix.armored, mix.light, mix.air, mix.armedAir, mix.antiAir, mix.total};
        j["counters"] = {k.longbows, k.perJuggernaut ? *k.perJuggernaut : -1, k.fireflies, k.hailstorms, k.sentinels, k.kestrels};
        j["attackTarget"] = vec(c.attackTarget(seen, front));
        j["regroup"] = vec(c.regroup(mine, front, m.front()));
        j["gathered"] = c.gathered(mine, front);
        j["sentinels"] = sentinels;
        j["plan"] = describeAll(plan.out);
        j["objectives"] = objectives;
        j["manning"] = manning;
        j["sites"] = sites;
        j["requests"] = describeAll(r.out);
        j["requestStatus"] = status;
        j["reserved"] = {r.reserved.ore, r.reserved.hydrogen};
        j["left"] = {money, hydrogen, supply};
        j["oneBuildPerSpot"] = describeAll(Commander::oneBuildPerSpot(twice));
        out.push_back(j);
    }
    return out;
}

/// The humans' orders given by hand, as `direct` of the Swift golden.
void direct(GameState& s) {
    const int64_t n = static_cast<int64_t>(s.players.size());
    auto citadel = [&](int64_t owner) {
        for (const auto& b : s.structures) {
            if (b.owner == owner && b.kind == Structure::Kind::citadel) { return b.position; }
        }
        return Vec2::zero;
    };
    for (int64_t p = 0; p < n; p++) {
        const Vec2 home = citadel(p);
        const Vec2 foe = citadel((p + 1) % n);
        Directives d;
        auto objective = [&](Objective::Kind k, Vec2 at, std::optional<int64_t> structure = std::nullopt) {
            Objective o;
            o.id = d.nextID;
            o.kind = k;
            o.at = at;
            o.structure = structure;
            d.objectives.push_back(o);
            d.nextID += 1;
        };
        auto request = [&](Request::What w, int64_t count = 1, bool repeats = false, std::optional<int64_t> site = std::nullopt) {
            Request r;
            r.id = d.nextID;
            r.what = w;
            r.count = count;
            r.repeats = repeats;
            r.site = site;
            d.queue.push_back(r);
            d.nextID += 1;
        };
        objective(Objective::Kind::attack, foe);
        objective(Objective::Kind::defend, home);
        objective(Objective::Kind::attack, (home + foe) / 2);
        for (const auto& b : s.structures) {
            if (b.owner == p && b.kind == Structure::Kind::bastion) { objective(Objective::Kind::man, b.position, b.id); break; }
        }
        request(Request::What::Unit{Unit::Kind::longbow});
        request(Request::What::Upgrade{Upgrade::minigun});
        request(Request::What::Unit{Unit::Kind::ranger}, 3);
        request(Request::What::Building{Structure::Kind::garrison});
        request(Request::What::Building{Structure::Kind::lab});
        request(Request::What::Building{Structure::Kind::citadel}, 1, false, p);
        request(Request::What::Unit{Unit::Kind::comet}, 1, true);
        request(Request::What::Building{Structure::Kind::sentinel});
        s.players[static_cast<size_t>(p)].directives = d;
    }
}

void compare(const golden::json& got, const golden::json& want, const std::string& label) {
    std::string where;
    const bool same = golden::same(got, want, where, label);
    EXPECT_TRUE(same);
    if (!same) { std::printf("    %s\n", where.c_str()); }
}

} // namespace

TEST(squadsGolden) {
    const auto json = golden::load(golden::path("squads.json"));
    ASSERT_TRUE(!json.is_discarded());
    const auto session = SessionStore::load(golden::repo("bench/badlands-large.json"));
    ASSERT_TRUE(session.has_value());
    const MapDefinition m = WindowMaps::build(MapChoice{MapStyle::badlands, MapSize::large});
    GameState bench = session->state;
    direct(bench);
    compare(dump(bench, m), json["bench"], "bench");

    GameState fresh = GameState::new_(m);
    for (Player& p : fresh.players) { p.ore = 1000; p.hydrogen = 500; }
    compare(dump(fresh, m), json["fresh"], "fresh");

    GameState tech = fresh;
    for (const Structure& citadel : fresh.structures) {
        if (citadel.kind != Structure::Kind::citadel) { continue; }
        Commander c(m, citadel.owner);
        c.frontDefence = false; // the Swift golden's placement
        auto addB = [&](Structure::Kind k, Vec2 p) {
            tech.structures.push_back(Structure(tech.nextID, k, citadel.owner, p));
            tech.nextID += 1;
        };
        auto addU = [&](Unit::Kind k, Vec2 p, std::optional<int64_t> owner = std::nullopt) {
            tech.units.push_back(Unit(tech.nextID, k, owner ? *owner : citadel.owner, p, 0, Unit::Task::idle));
            tech.nextID += 1;
        };
        const auto dome = c.habDomeSpot(tech, citadel);
        ASSERT_TRUE(dome.has_value());
        addB(Structure::Kind::habDome, *dome);
        const auto garrison = c.garrisonSpot(tech, citadel);
        ASSERT_TRUE(garrison.has_value());
        addB(Structure::Kind::garrison, *garrison);
        const auto wells = *tech.wells;
        for (const Well& g : wells) {
            if (distance(g.position, citadel.position) < 12) { addB(Structure::Kind::derrick, g.position); }
        }
        for (int k = 0; k < 13; k++) {
            addU(Unit::Kind::prospector, citadel.position + Vec2(static_cast<double>(k % 7) - 3, -3.2 - static_cast<double>(k / 7)));
        }
        addU(Unit::Kind::comet, citadel.position + Vec2(0, 8));
        addB(Structure::Kind::bastion, citadel.position + Vec2(-8, 0));
        for (int k = 0; k < 5; k++) { addU(Unit::Kind::ranger, citadel.position + Vec2(static_cast<double>(k) - 2, 6)); }
        if (citadel.owner == 0) {
            for (int k = 0; k < 7; k++) { addU(Unit::Kind::ranger, citadel.position + Vec2(static_cast<double>(k) - 3, 5), 1); }
        }
    }
    direct(tech);
    compare(dump(tech, m), json["tech"], "tech");
}
