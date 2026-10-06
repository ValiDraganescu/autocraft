// Port of Sources/GameCore/Commander+Objectives.swift.
//
// The team's objectives (`Directives.objectives`): the AI splits its army
// into a squad per objective, as it judges each needs. Every team's
// `Commander` runs this the same way.
#include "Commander.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace ac {

namespace {

/// `u.mission?.objective`.
std::optional<int64_t> objectiveOf(const Unit& u) { return u.mission ? u.mission->objective() : std::nullopt; }

/// `u.id != sim.pilot?.unit`.
bool driven(const Simulation& sim, const Unit& u) { return sim.pilot && sim.pilot->unit == u.id; }

} // namespace

std::vector<Command> Commander::squads(const Simulation& sim) const { return plan(sim).out; }

std::vector<ObjectiveStatus> Commander::objectiveStatus(const Simulation& sim) const {
    return plan(sim.seen(player)).status;
}

Commander::Plan Commander::plan(const Simulation& sim) const {
    const GameState& s = sim.state;
    if (!(player >= 0 && player < static_cast<int64_t>(s.players.size()))) { return {}; }
    const Player& me = s.players[static_cast<size_t>(player)];
    const Directives d = me.orders();
    std::set<int64_t> live;
    for (const auto& o : d.objectives) { live.insert(o.id); }
    std::vector<Unit> units;
    for (const auto& u : s.units) { if (u.owner == player) { units.push_back(u); } }
    std::vector<Command> out;
    // A squad whose objective is gone rejoins the army.
    for (const auto& u : units) {
        if (auto o = objectiveOf(u); o && !live.count(*o)) { out.push_back(Command::Mission{u.id, std::nullopt}); }
    }
    if (d.objectives.empty()) { return {out, {}}; }

    const Stance stance = d.stance;
    std::vector<Unit> pool;
    for (const auto& u : units) {
        const auto o = objectiveOf(u);
        if (u.soldier() && u.hp > 0 && u.task != Unit::Task::inBastion && u.task != Unit::Task::toBastion
            && u.task != Unit::Task::aboard && !driven(sim, u) && (!u.mission || (o && live.count(*o)))) {
            pool.push_back(u);
        }
    }
    std::stable_sort(pool.begin(), pool.end(), [](const Unit& a, const Unit& b) { return a.id < b.id; });
    std::vector<Unit> enemies;
    for (const auto& u : s.units) {
        if (s.hostile(u.owner, player) && u.soldier() && u.task != Unit::Task::aboard && u.hp > 0) { enemies.push_back(u); }
    }
    std::vector<Structure> theirs, mine;
    for (const auto& b : s.structures) {
        if (s.hostile(b.owner, player)) { theirs.push_back(b); }
        if (b.owner == player) { mine.push_back(b); }
    }
    const std::optional<Vec2> rallied = sim.rallyPoint(player);
    const Vec2 rally = rallied ? *rallied : start(s);
    const double aggression = stance == Stance::aggressive ? ac::min(1.0, me.aggression + 0.4) : me.aggression;
    const double odds = 1.3 - 0.5 * aggression;
    auto worth = [](const std::vector<Unit>& us) { return strength(us); };
    /// Enemy soldiers near `p`, and the Rangers in their Bastions there.
    auto threat = [&](Vec2 p, double r) {
        std::vector<Unit> near;
        for (const auto& e : enemies) { if (distance(e.position, p) < r) { near.push_back(e); } }
        double crews = 0;
        for (const auto& b : theirs) {
            if (b.kind == Structure::Kind::bastion && distance(b.position, p) < r) {
                crews = crews + static_cast<double>(b.crew ? b.crew->size() : 0);
            }
        }
        return worth(near) + crews;
    };

    // The groups, in the order they are filled.
    struct Group {
        std::optional<int64_t> key;
        Vec2 at;
        double need;
        std::optional<Objective::Kind> kind;
    };
    std::vector<Group> groups;
    if (stance != Stance::allIn) {
        std::vector<Unit> intruders;
        for (const auto& e : enemies) {
            if (std::any_of(mine.begin(), mine.end(), [&](const Structure& b) {
                    return distance(b.position, e.position) < Rules::radius(b.kind) + 12;
                })) {
                intruders.push_back(e);
            }
        }
        const double t = worth(intruders);
        const Vec2 home = start(s);
        if (t > raidSize && !intruders.empty()) {
            const Unit* first = &intruders[0];
            for (const auto& u : intruders) {
                if (distance(u.position, home) < distance(first->position, home)) { first = &u; }
            }
            groups.push_back(Group{std::nullopt, first->position, 1.5 * t, std::nullopt});
        }
    }
    for (const auto& o : d.objectives) {
        if (!(o.kind == Objective::Kind::defend || o.kind == Objective::Kind::hold)) { continue; }
        const double least = o.kind == Objective::Kind::defend ? guardForce : assaultForce;
        groups.push_back(Group{o.id, o.at, ac::max(least, 1.5 * threat(o.at, 16)), o.kind});
    }
    for (const auto& o : d.objectives) {
        if (o.kind != Objective::Kind::attack) { continue; }
        groups.push_back(Group{o.id, o.at, ac::max(assaultForce, odds * threat(o.at, 14)), Objective::Kind::attack});
    }

    // Share out the soldiers.
    std::vector<std::vector<Unit>> members(groups.size());
    std::vector<double> have(groups.size(), 0.0);
    std::set<int64_t> taken;
    for (size_t g = 0; g < groups.size(); g++) {
        const Group& group = groups[g];
        std::vector<Unit> order;
        for (const auto& u : pool) { if (!taken.count(u.id)) { order.push_back(u); } }
        std::stable_sort(order.begin(), order.end(), [&](const Unit& a, const Unit& b) {
            const auto ka = std::make_tuple(objectiveOf(a) == group.key ? 0 : 1, distance(a.position, group.at), a.id);
            const auto kb = std::make_tuple(objectiveOf(b) == group.key ? 0 : 1, distance(b.position, group.at), b.id);
            return ka < kb;
        });
        for (const auto& u : order) {
            if (!(have[g] < group.need)) { break; }
            members[g].push_back(u);
            have[g] += worth({u});
            taken.insert(u.id);
        }
    }
    std::vector<size_t> attacks;
    for (size_t g = 0; g < groups.size(); g++) {
        if (groups[g].kind == Objective::Kind::attack) { attacks.push_back(g); }
    }
    if (!attacks.empty()) {
        for (const auto& u : pool) {
            if (taken.count(u.id)) { continue; }
            // Its own squad if it is in one, else the one shortest of
            // what it needs.
            const auto own = objectiveOf(u);
            std::optional<size_t> pick;
            for (size_t a : attacks) {
                if (groups[a].key == own) { pick = a; break; }
            }
            if (!pick) {
                size_t best = attacks[0];
                for (size_t a : attacks) {
                    if (have[a] / groups[a].need < have[best] / groups[best].need) { best = a; }
                }
                pick = best;
            }
            const size_t g = *pick;
            members[g].push_back(u);
            have[g] += worth({u});
            taken.insert(u.id);
        }
    }

    // What each group does.
    std::vector<ObjectiveStatus> status;
    auto send = [&](const std::vector<Unit>& us, const std::optional<Mission>& m) {
        for (const auto& u : us) {
            if (u.mission != m) { out.push_back(Command::Mission{u.id, m}); }
        }
    };
    auto centre = [](const std::vector<Unit>& us) -> std::optional<Vec2> {
        if (us.empty()) { return std::nullopt; }
        Vec2 sum = Vec2::zero;
        for (const auto& u : us) { sum = sum + u.position; }
        return sum / static_cast<double>(us.size());
    };
    // The army at home: everyone in it goes back to the army.
    for (size_t g = 0; g < groups.size(); g++) {
        if (!groups[g].key) { send(members[g], std::nullopt); }
    }
    // Soldiers in no group (no attack to send them to) rejoin the army.
    {
        std::vector<Unit> loose;
        for (const auto& u : pool) { if (!taken.count(u.id) && u.mission) { loose.push_back(u); } }
        send(loose, std::nullopt);
    }

    for (size_t g = 0; g < groups.size(); g++) {
        const Group& group = groups[g];
        if (!group.key) { continue; }
        const int64_t id = *group.key;
        const std::vector<Unit>& squad = members[g];
        std::vector<int64_t> ids;
        for (const auto& u : squad) { ids.push_back(u.id); }
        const int64_t need = static_cast<int64_t>(std::ceil(group.need)), got = static_cast<int64_t>(std::round(have[g]));
        const std::string squadCount = std::to_string(squad.size());
        if (group.kind == Objective::Kind::defend || group.kind == Objective::Kind::hold) {
            send(squad, Mission{Mission::Hold{id, group.at}});
            const std::string doing = group.kind == Objective::Kind::defend ? "Guarding" : "Holding the area";
            const std::string text = squad.empty() ? "No soldiers to spare"
                : have[g] < group.need ? doing + ", short: " + std::to_string(got) + " / " + std::to_string(need)
                                       : doing + " (" + squadCount + ")";
            status.push_back(ObjectiveStatus{id, text, ids, centre(squad)});
            continue;
        }
        std::vector<Unit> assaulting, regrouping;
        for (const auto& u : squad) {
            if (u.mission && u.mission->is<Mission::Assault>()) { assaulting.push_back(u); }
            if (u.mission && u.mission->is<Mission::Regroup>()) { regrouping.push_back(u); }
        }
        // Done: there, and nothing of the enemy's near the point.
        const bool there = std::any_of(assaulting.begin(), assaulting.end(),
                                       [&](const Unit& u) { return distance(u.position, group.at) < 6; });
        const bool clear_ = threat(group.at, 10) == 0
            && !std::any_of(theirs.begin(), theirs.end(), [&](const Structure& b) {
                   return distance(b.position, group.at) < Rules::radius(b.kind) + 8;
               });
        if (there && clear_) {
            // Taken: the squad stays and holds it.
            out.push_back(Command::Retask{player, id, Objective::Kind::hold});
            send(squad, Mission{Mission::Hold{id, group.at}});
            status.push_back(ObjectiveStatus{id, "Taken: holding the area (" + squadCount + ")", ids, centre(squad)});
            continue;
        }
        if (stance != Stance::allIn && !regrouping.empty()) {
            int64_t home = 0;
            for (const auto& u : regrouping) { if (distance(u.position, rally) < homeRadius) { home++; } }
            if (static_cast<double>(home) < regroupShare * static_cast<double>(regrouping.size())) {
                send(squad, Mission{Mission::Regroup{id, rally}});
                status.push_back(ObjectiveStatus{id, "Falling back to regroup", ids, centre(squad)});
                continue;
            }
        }
        if (!assaulting.empty() && stance != Stance::allIn) {
            if (const auto c = centre(assaulting)) {
                // The fight around the squad.
                std::vector<Unit> near, foesNear;
                for (const auto& u : squad) { if (distance(u.position, *c) < 10) { near.push_back(u); } }
                for (const auto& e : enemies) { if (distance(e.position, *c) < 12) { foesNear.push_back(e); } }
                const double ours = worth(near);
                const double foes = worth(foesNear);
                if (foes > ours * (1.25 + 0.6 * aggression)) {
                    send(squad, Mission{Mission::Regroup{id, rally}});
                    status.push_back(ObjectiveStatus{id, "Falling back to regroup", ids, c});
                    continue;
                }
            }
        }
        if (!assaulting.empty() || have[g] >= group.need || (stance == Stance::allIn && !squad.empty())) {
            send(squad, Mission{Mission::Assault{id, group.at}});
            status.push_back(ObjectiveStatus{id, "Attacking (" + squadCount + ")", ids, centre(squad)});
        } else {
            send(squad, Mission{Mission::Hold{id, rally}});
            const std::string text = squad.empty()
                ? "Waiting for soldiers"
                : "Gathering " + std::to_string(got) + " / " + std::to_string(need) + " at the rally";
            status.push_back(ObjectiveStatus{id, text, ids, centre(squad)});
        }
    }
    for (const auto& m : manning(sim)) {
        const int64_t id = m.objective.id;
        if (!m.bastion) {
            out.push_back(Command::CancelObjective{player, id});
            status.push_back(ObjectiveStatus{id, "Bastion lost", {}, std::nullopt});
            continue;
        }
        const Structure& b = *m.bastion;
        const int64_t cap = Rules::bastionCapacity;
        std::string text;
        if (!b.complete()) {
            text = "Waiting for the Bastion to finish";
        } else if (m.inside >= cap) {
            text = "Manned " + std::to_string(cap) + " / " + std::to_string(cap);
        } else {
            text = "Filling " + std::to_string(m.inside) + " / " + std::to_string(cap);
            const int64_t coming = m.coming + static_cast<int64_t>(m.send.size());
            if (coming > 0) { text += " · " + std::to_string(coming) + " on the way"; }
        }
        if (m.short_ > 0) { text += " · training " + std::to_string(m.short_); }
        std::vector<Unit> walking;
        for (const auto& u : s.units) {
            if (u.task == Unit::Task::toBastion && u.structure == b.id) { walking.push_back(u); }
        }
        walking.insert(walking.end(), m.send.begin(), m.send.end());
        std::vector<int64_t> crewAndWalking = b.crew ? *b.crew : std::vector<int64_t>{};
        for (const auto& u : walking) { crewAndWalking.push_back(u.id); }
        status.push_back(ObjectiveStatus{id, text, crewAndWalking,
                                         walking.empty() ? std::nullopt : centre(walking)});
    }
    // In the order the humans gave them.
    std::map<int64_t, int64_t> order;
    for (size_t i = 0; i < d.objectives.size(); i++) { order[d.objectives[i].id] = static_cast<int64_t>(i); }
    auto rank = [&](int64_t id) {
        const auto it = order.find(id);
        return it != order.end() ? it->second : 0;
    };
    std::stable_sort(status.begin(), status.end(),
                     [&](const ObjectiveStatus& a, const ObjectiveStatus& b) { return rank(a.id) < rank(b.id); });
    return {out, status};
}

std::vector<Commander::Manning> Commander::manning(const Simulation& sim) const {
    const GameState& s = sim.state;
    if (!(player >= 0 && player < static_cast<int64_t>(s.players.size()))) { return {}; }
    std::vector<Objective> jobs;
    for (const auto& o : s.players[static_cast<size_t>(player)].orders().objectives) {
        if (o.kind == Objective::Kind::man) { jobs.push_back(o); }
    }
    if (jobs.empty()) { return {}; }
    std::vector<Unit> spare;
    for (const auto& u : s.units) {
        if (u.owner == player && u.kind == Unit::Kind::ranger && u.hp > 0 && u.task != Unit::Task::inBastion
            && u.task != Unit::Task::toBastion && u.task != Unit::Task::aboard && !driven(sim, u)
            && (!u.mission || objectiveOf(u))) {
            spare.push_back(u);
        }
    }
    int64_t training = 0;
    for (const auto& b : s.structures) {
        if (b.owner != player) { continue; }
        const auto line = queued(b);
        training = training + std::count(line.begin(), line.end(), Unit::Kind::ranger);
    }
    std::vector<Manning> result;
    for (const auto& o : jobs) {
        Manning m{o, std::nullopt, 0, 0, {}, 0};
        const std::optional<Structure> found = o.structure ? s.structure(*o.structure) : std::nullopt;
        if (!(found && found->owner == player && found->kind == Structure::Kind::bastion)) {
            result.push_back(m);
            continue;
        }
        const Structure& b = *found;
        m.bastion = b;
        m.inside = b.crew ? static_cast<int64_t>(b.crew->size()) : 0;
        m.coming = 0;
        for (const auto& u : s.units) {
            if (u.task == Unit::Task::toBastion && u.structure == b.id) { m.coming++; }
        }
        const int64_t room = ac::max<int64_t>(0, Rules::bastionCapacity - m.inside - m.coming);
        std::vector<Unit> sorted = spare;
        std::stable_sort(sorted.begin(), sorted.end(), [&](const Unit& a, const Unit& c) {
            return std::make_tuple(a.mission ? 1 : 0, distance(a.position, b.position), a.id)
                < std::make_tuple(c.mission ? 1 : 0, distance(c.position, b.position), c.id);
        });
        const std::vector<Unit> pick(sorted.begin(), sorted.begin() + ac::min<int64_t>(room, static_cast<int64_t>(sorted.size())));
        if (b.complete()) {
            m.send = pick;
            std::set<int64_t> ids;
            for (const auto& u : pick) { ids.insert(u.id); }
            spare.erase(std::remove_if(spare.begin(), spare.end(), [&](const Unit& u) { return ids.count(u.id) > 0; }),
                        spare.end());
        }
        const int64_t picked = static_cast<int64_t>(pick.size());
        const int64_t fromTraining = ac::min(training, room - picked);
        training -= fromTraining;
        m.short_ = room - picked - fromTraining;
        result.push_back(m);
    }
    return result;
}

std::vector<SiteInfo> Commander::siteInfo(const Simulation& sim) const {
    const Simulation seen = sim.seen(player);
    const GameState& s = seen.state;
    if (!(player >= 0 && player < static_cast<int64_t>(s.players.size()))) { return {}; }
    std::vector<Structure> citadels;
    for (const auto& b : s.structures) { if (b.kind == Structure::Kind::citadel) { citadels.push_back(b); } }
    const Vec2 home = start(s);
    std::vector<Vec2> foes;
    for (int64_t i = 0; i < static_cast<int64_t>(s.players.size()); i++) {
        if (s.hostile(i, player)) { foes.push_back(start(s, i)); }
    }
    std::set<int64_t> taken;
    for (const auto& c : citadels) { taken.insert(seen.site(c)); }
    std::optional<Vec2> next;
    for (const auto& c : citadels) {
        if (c.owner == player && c.complete()) {
            next = nextSite(s, seen, taken, c.position, false);
            break;
        }
    }
    std::vector<SiteInfo> out;
    for (int64_t i = 0; i < static_cast<int64_t>(seen.sites.size()); i++) {
        const Vec2 p = seen.sites[static_cast<size_t>(i)];
        std::optional<int64_t> owner;
        for (const auto& c : citadels) {
            if (seen.site(c) == i) { owner = c.owner; break; }
        }
        const double mine = router.distance(home, p);
        const bool contested = std::any_of(foes.begin(), foes.end(), [&](Vec2 f) { return router.distance(f, p) <= 1.5 * mine; });
        out.push_back(SiteInfo{i, p, owner, contested, next ? distance(*next, p) < 0.5 : false});
    }
    return out;
}

} // namespace ac
