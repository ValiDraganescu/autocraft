// Port of Sources/GameCore/Commander+Requests.swift.
//
// The team's queue (`Directives`): what its humans asked the AI to spend
// on next. Every team's `Commander` runs this the same way.
#include "Commander.h"

#include <algorithm>
#include <functional>
#include <map>
#include <tuple>

namespace ac {

Commander::Requests Commander::requests(const Simulation& sim, int64_t& money, int64_t& hydrogen, int64_t& supplyFree,
                                        std::set<int64_t>& busy, const std::vector<Command>& prior) const {
    const GameState& s = sim.state;
    // Rangers for the Bastions to man come first (under the objective's
    // id, which no request has).
    std::vector<Request> queue;
    for (const auto& m : manning(sim)) {
        if (!(m.bastion && m.short_ > 0)) { continue; }
        Request r;
        r.id = m.objective.id;
        r.what = Request::What::Unit{Unit::Kind::ranger};
        r.count = m.short_;
        queue.push_back(r);
    }
    for (const auto& r : s.players[static_cast<size_t>(player)].orders().queue) { queue.push_back(r); }
    if (queue.empty()) { return {}; }
    std::vector<Structure> mine, done;
    for (const auto& b : s.structures) { if (b.owner == player) { mine.push_back(b); } }
    for (const auto& b : mine) { if (b.kind == Structure::Kind::citadel && b.complete()) { done.push_back(b); } }
    std::vector<BuildOrder> ordered;
    for (const auto& u : s.units) {
        if (u.kind == Unit::Kind::prospector && u.owner == player && u.order) { ordered.push_back(*u.order); }
    }
    std::vector<Command> out;
    std::vector<RequestStatus> status;
    Requests::Reserved reserved;
    // Production slots and kinds started this second.
    std::map<int64_t, int64_t> queued;
    std::vector<Structure::Kind> started;
    std::set<int64_t> labbed, labsBusy;

    auto complete = [&](Structure::Kind k) {
        std::vector<Structure> r;
        for (const auto& b : mine) { if (b.kind == k && b.complete()) { r.push_back(b); } }
        return r;
    };
    auto planned = [&](Structure::Kind k) {
        return std::any_of(mine.begin(), mine.end(), [&](const Structure& b) { return b.kind == k; })
            || std::any_of(ordered.begin(), ordered.end(), [&](const BuildOrder& o) { return o.kind == k; })
            || std::find(started.begin(), started.end(), k) != started.end();
    };
    auto lab = [&](const Structure& b) -> std::optional<Structure> {
        if (!b.addon) { return std::nullopt; }
        for (const auto& l : mine) { if (l.id == *b.addon) { return l; } }
        return std::nullopt;
    };
    auto spots = [&]() {
        std::vector<std::pair<Vec2, double>> r;
        auto add = [&](const Command& c) {
            const Command o = inner(c);
            if (const auto* b = o.as<Command::Build>()) { r.emplace_back(b->at, Rules::radius(b->kind)); }
        };
        for (const auto& c : prior) { add(c); }
        for (const auto& c : out) { add(c); }
        return r;
    };
    auto order = [](Command c, int64_t ore, int64_t gas, int64_t supply, bool serves, std::optional<std::string> note,
                    std::optional<int64_t> worker = std::nullopt) {
        return Step{Step::Order{std::move(c), ore, gas, supply, serves, std::move(note), worker}};
    };
    auto wait = [](std::string why) { return Step{Step::Wait{std::move(why)}}; };
    const Step drop{Step::Drop{}};

    std::function<Step(Structure::Kind, bool, std::optional<std::string>, std::optional<int64_t>)> place;
    std::function<Step(Structure::Kind)> ensureBuilt;
    std::function<Step(std::optional<int64_t>, std::optional<Structure::Kind>, bool)> addon;

    /// A new `k`, placed by the AI, once what it needs stands.
    place = [&](Structure::Kind k, bool serves, std::optional<std::string> note, std::optional<int64_t> site) -> Step {
        if (k == Structure::Kind::lab) { return addon(std::nullopt, std::nullopt, serves); }
        if (done.empty()) { return wait("Needs a Citadel"); }
        const Structure& citadel = done.front();
        if (auto need = Rules::requires_(k); need && complete(*need).empty()) { return ensureBuilt(*need); }
        std::optional<Vec2> spot;
        switch (k) {
        // A Sentinel (2x2, like a Hab Dome) goes where a Hab Dome would.
        case Structure::Kind::habDome:
            for (const auto& c : done) { if ((spot = habDomeSpot(s, c, sim.navGrid()))) { break; } }
            break;
        case Structure::Kind::sentinel:
            if (frontDefence) {
                // By the ore line on the side the enemy's air comes from.
                std::vector<Vec2> have;
                for (const auto& b : s.structures) { if (b.kind == Structure::Kind::sentinel && b.owner == player) { have.push_back(b.position); } }
                for (const auto& c : done) { if ((spot = sentinelSpot(s, c, have))) { break; } }
            } else {
                for (const auto& c : done) { if ((spot = habDomeSpot(s, c, sim.navGrid()))) { break; } }
            }
            break;
        case Structure::Kind::garrison:
        case Structure::Kind::foundry:
        case Structure::Kind::spacedock:
            for (const auto& c : done) { if ((spot = garrisonSpot(s, c, sim.navGrid()))) { break; } }
            break;
        case Structure::Kind::bastion: {
            const Structure* front = &done.front();
            for (const auto& c : done) {
                if (distance(c.position, map.front()) < distance(front->position, map.front())) { front = &c; }
            }
            spot = bastionSpot(s, sim, *front);
            break;
        }
        case Structure::Kind::derrick: spot = derrickSpot(s, done); break;
        case Structure::Kind::citadel: {
            auto siteOf = [&](Vec2 p) -> int64_t {
                if (sim.sites.empty()) { return 0; }
                int64_t best = 0;
                for (int64_t i = 1; i < static_cast<int64_t>(sim.sites.size()); i++) {
                    if (distance(sim.sites[static_cast<size_t>(i)], p) < distance(sim.sites[static_cast<size_t>(best)], p)) { best = i; }
                }
                return best;
            };
            std::set<int64_t> taken;
            for (const auto& b : s.structures) { if (b.kind == Structure::Kind::citadel) { taken.insert(sim.site(b)); } }
            for (const auto& o : ordered) { if (o.kind == Structure::Kind::citadel) { taken.insert(siteOf(o.position)); } }
            if (site) {
                // The site the humans chose: dropped once it is the
                // team's, held while an enemy has it.
                std::optional<int64_t> owner;
                for (const auto& b : s.structures) {
                    if (b.kind == Structure::Kind::citadel && sim.site(b) == *site) { owner = b.owner; break; }
                }
                if (!owner) {
                    for (const auto& o : ordered) {
                        if (o.kind == Structure::Kind::citadel && siteOf(o.position) == *site) { owner = player; break; }
                    }
                }
                if (owner == player) { return drop; }
                const Vec2 at = sim.sites[static_cast<size_t>(*site)];
                if (owner || std::any_of(s.structures.begin(), s.structures.end(), [&](const Structure& b) {
                        return b.owner != player && distance(b.position, at) < 16;
                    })) {
                    return wait("The enemy holds that site");
                }
                spot = at;
            } else {
                spot = nextSite(s, sim, taken, citadel.position);
            }
            break;
        }
        case Structure::Kind::lab: spot = std::nullopt; break;
        }
        if (!spot) { return wait("No room for a " + Simulation::title(k)); }
        const double r = Rules::radius(k);
        for (const auto& [q, qr] : spots()) {
            if (distance(q, *spot) < qr + r + 0.8) { return wait("Next in line"); }
        }
        const auto w = builder(s, *spot, busy);
        if (!w) { return wait("No Prospector free"); }
        return order(Command::Build{*w, k, *spot}, Rules::cost(k), Rules::hydrogenCost(k), 0, serves, note, *w);
    };

    /// A finished `k` is needed: wait for one on the way, or put one up.
    ensureBuilt = [&](Structure::Kind k) -> Step {
        return planned(k) ? wait("Waiting for the " + Simulation::title(k))
                          : place(k, false, Simulation::title(k) + " first", std::nullopt);
    };

    /// A Lab: on `at` when it takes one, else on any finished
    /// `on` (any Garrison, Foundry or Spacedock when nil) that does.
    addon = [&](std::optional<int64_t> at, std::optional<Structure::Kind> on, bool serves) -> Step {
        std::vector<Structure> hosts, open;
        for (const auto& b : mine) {
            if (labHolders().count(b.kind) && (!on || b.kind == *on) && b.complete()) { hosts.push_back(b); }
        }
        for (const auto& b : hosts) {
            if (!b.addon && !labbed.count(b.id) && labFits(b, s)) { open.push_back(b); }
        }
        auto key = [&](const Structure& b) {
            return std::make_tuple(b.id == at ? 0 : 1, b.training ? 1 : 0, b.id);
        };
        std::stable_sort(open.begin(), open.end(), [&](const Structure& a, const Structure& b) { return key(a) < key(b); });
        if (open.empty()) {
            if (on && hosts.empty()) { return ensureBuilt(*on); }
            return wait(hosts.empty() ? "Needs a Garrison, Foundry or Spacedock" : "No room for a Lab");
        }
        const Structure& b = open.front();
        if (b.training) { return wait("Waiting for the " + Simulation::title(b.kind) + " to finish training"); }
        return order(Command::Addon{b.id}, Rules::cost(Structure::Kind::lab), Rules::hydrogenCost(Structure::Kind::lab), 0,
                     serves, serves ? std::nullopt : std::optional<std::string>("Lab first"));
    };

    auto train = [&](const Request& r, Unit::Kind k) -> Step {
        if (!unlocked.count(k)) { return drop; }
        std::optional<Structure::Kind> host;
        for (const auto h : allCases<Structure::Kind>()) {
            const auto t = Rules::trains(h);
            if (std::find(t.begin(), t.end(), k) != t.end()) { host = h; break; }
        }
        if (!host) { return drop; }
        const auto hosts = complete(*host);
        if (hosts.empty()) { return ensureBuilt(*host); }
        // A building raising its lab trains nothing meanwhile.
        std::vector<Structure> usable;
        for (const auto& b : hosts) {
            const auto l = lab(b);
            if ((!l || l->complete()) && (!Rules::needsLab(k) || l) && !labbed.count(b.id)) { usable.push_back(b); }
        }
        if (usable.empty()) {
            if (std::any_of(hosts.begin(), hosts.end(), [&](const Structure& b) {
                    const auto l = lab(b);
                    return (l && !l->complete()) || labbed.count(b.id) > 0;
                })) {
                return wait("Waiting for the Lab");
            }
            return addon(r.at, host, false);
        }
        // Short queues: one unit ahead at most (none ahead for a
        // standing order), so the units come out of every building.
        const int64_t depth = r.repeats ? 1 : 2;
        std::vector<Structure> free;
        for (const auto& b : usable) {
            const auto it = queued.find(b.id);
            if (b.queueCount() + (it != queued.end() ? it->second : 0) < depth) { free.push_back(b); }
        }
        std::stable_sort(free.begin(), free.end(), [&](const Structure& a, const Structure& b) {
            return std::make_tuple(a.id == r.at ? 0 : 1, a.queueCount(), a.id)
                < std::make_tuple(b.id == r.at ? 0 : 1, b.queueCount(), b.id);
        });
        if (free.empty()) { return wait("Waiting for a free " + Simulation::title(*host)); }
        const Structure& b = free.front();
        return order(Command::Train{b.id, k}, Rules::cost(k), Rules::hydrogenCost(k), Rules::supply(k), true, std::nullopt);
    };

    auto research = [&](const Request& r, Upgrade up) -> Step {
        if (sim.has(player, up)
            || std::any_of(mine.begin(), mine.end(), [&](const Structure& b) { return b.research == up; })) {
            return drop;
        }
        const auto hosts = complete(at(up));
        if (hosts.empty()) { return ensureBuilt(at(up)); }
        std::vector<Structure> labs;
        for (const auto& b : hosts) { if (auto l = lab(b)) { labs.push_back(*l); } }
        for (const auto& l : labs) {
            if (l.complete() && !l.research && !labsBusy.count(l.id)) {
                return order(Command::Research{l.id, up}, ore(up), ac::hydrogen(up), 0, true, std::nullopt);
            }
        }
        if (std::any_of(labs.begin(), labs.end(), [](const Structure& l) { return !l.complete(); })
            || std::any_of(hosts.begin(), hosts.end(), [&](const Structure& b) { return labbed.count(b.id) > 0; })) {
            return wait("Waiting for the Lab");
        }
        if (!labs.empty()) { return wait("Waiting for the Lab to finish"); }
        return addon(r.at, at(up), false);
    };

    auto step = [&](const Request& r) -> Step {
        // MH it cannot get without a Derrick: a Derrick first.
        if (r.hydrogen() > ac::max<int64_t>(hydrogen, 0) && !planned(Structure::Kind::derrick)) {
            return place(Structure::Kind::derrick, false, "Derrick first", std::nullopt);
        }
        if (const auto* u = r.what.as<Request::What::Unit>()) { return train(r, u->kind); }
        if (const auto* g = r.what.as<Request::What::Upgrade>()) { return research(r, g->upgrade); }
        const auto k = r.what.as<Request::What::Building>()->kind;
        if (k == Structure::Kind::lab) { return addon(r.at, std::nullopt, true); }
        return place(k, true, std::nullopt, r.site);
    };

    auto shortOf = [](int64_t m, int64_t g) -> std::string {
        std::vector<std::string> parts;
        if (m > 0) { parts.push_back(std::to_string(m) + " ore"); }
        if (g > 0) { parts.push_back(std::to_string(g) + " MH"); }
        if (parts.empty()) { return "Ordered"; }
        std::string joined;
        for (size_t i = 0; i < parts.size(); i++) { joined += (i > 0 ? ", " : "") + parts[i]; }
        return "Saving: " + joined + " short";
    };
    auto funded = [&](int64_t m, int64_t g) {
        const double a = m > 0 ? static_cast<double>(ac::max<int64_t>(money, 0)) / static_cast<double>(m) : 1;
        const double b = g > 0 ? static_cast<double>(ac::max<int64_t>(hydrogen, 0)) / static_cast<double>(g) : 1;
        return ac::min(1.0, a, b);
    };
    auto hold = [&](int64_t m, int64_t g) {
        money -= m; hydrogen -= g;
        reserved.ore += m; reserved.hydrogen += g;
    };

    for (const auto& r : queue) {
        // A standing order trains as buildings come free; a count goes
        // on until done or until one has to wait. Anything needed
        // first is one order a second.
        std::optional<RequestStatus> note;
        const int64_t rounds = r.repeats ? 4 : ac::max<int64_t>(r.count, 1);
        for (int64_t round = 0; round < rounds; round++) {
            bool issued = false, itself = false;
            const Step next = step(r);
            if (next.value.index() == 2) { // .drop
                out.push_back(Command::CancelRequest{player, r.id});
                note = RequestStatus{r.id, "Done", 1};
            } else if (const auto* w = std::get_if<Step::Wait>(&next.value)) {
                if (round == 0) {
                    note = RequestStatus{r.id, w->why, funded(r.ore(), r.hydrogen())};
                    // A standing order holds nothing while every building
                    // is busy with it.
                    if (!r.repeats) { hold(r.ore(), r.hydrogen()); }
                }
            } else {
                const auto& o = std::get<Step::Order>(next.value);
                const Command& c = o.command;
                const int64_t m = o.ore, g = o.hydrogen;
                if (o.supply > supplyFree) {
                    if (round == 0) {
                        note = RequestStatus{r.id, "Waiting for supply", funded(m, g)};
                        hold(m, g);
                    }
                } else if (money >= m && hydrogen >= g) {
                    out.push_back(o.serves ? Command{Command::Serve{player, r.id, c}} : c);
                    money -= m; hydrogen -= g; supplyFree -= o.supply;
                    if (o.worker) { busy.insert(*o.worker); }
                    if (const auto* b = c.as<Command::Build>()) {
                        started.push_back(b->kind);
                    } else if (const auto* t = c.as<Command::Train>()) {
                        queued[t->structure] += 1;
                    } else if (const auto* a = c.as<Command::Addon>()) {
                        labbed.insert(a->structure);
                    } else if (const auto* l = c.as<Command::Research>()) {
                        labsBusy.insert(l->structure);
                    }
                    if (!note) { note = RequestStatus{r.id, o.note ? *o.note : "Ordered", 1}; }
                    issued = true;
                    itself = o.serves;
                } else if (round == 0) {
                    note = RequestStatus{r.id, (o.note ? *o.note + " · " : std::string()) + shortOf(m > 0 ? m - money : 0, g > 0 ? g - hydrogen : 0),
                                         funded(m, g)};
                    hold(m, g);
                }
            }
            if (!(issued && itself)) { break; }
        }
        if (note) { status.push_back(*note); }
    }
    return {out, status, reserved};
}

std::vector<RequestStatus> Commander::requestStatus(const Simulation& sim) const {
    const Simulation seen = sim.seen(player);
    if (!(player >= 0 && player < static_cast<int64_t>(seen.state.players.size()))) { return {}; }
    const Player& me = seen.state.players[static_cast<size_t>(player)];
    int64_t money = me.ore, hydrogen = me.hydrogen, supply = me.supplyCap - me.supplyUsed;
    std::set<int64_t> busy;
    if (seen.pilot) { busy.insert(seen.pilot->unit); }
    return requests(seen, money, hydrogen, supply, busy).status;
}

Command Commander::inner(const Command& c) {
    if (const auto* sv = c.as<Command::Serve>()) { return inner(*sv->command); }
    return c;
}

std::vector<Command> Commander::oneBuildPerSpot(const std::vector<Command>& orders) {
    std::vector<std::pair<Vec2, double>> spots;
    std::vector<Command> out;
    for (const auto& c : orders) {
        const Command o = inner(c);
        const auto* b = o.as<Command::Build>();
        if (!b) { out.push_back(c); continue; }
        const Vec2 p = b->at;
        const double r = Rules::radius(b->kind);
        if (std::any_of(spots.begin(), spots.end(), [&](const auto& q) { return distance(q.first, p) < q.second + r + 0.8; })) {
            continue;
        }
        spots.emplace_back(p, r);
        out.push_back(c);
    }
    return out;
}

} // namespace ac
