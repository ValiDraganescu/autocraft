// Port of Sources/GameCore/Simulation.swift.
#include "Simulation.h"

#include "Commander.h"
#include "PathAhead.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ac {

namespace {

/// Swift's `defer`: runs `f` when the scope ends.
template <class F> struct Defer {
    F f;
    explicit Defer(F f_) : f(std::move(f_)) {}
    ~Defer() { f(); }
    Defer(const Defer&) = delete;
    Defer& operator=(const Defer&) = delete;
};

/// Swift's `Sequence.min(by:)`: the first element no other is ordered
/// before.
template <class T, class Less> std::optional<T> minBy(const std::vector<T>& xs, Less less) {
    std::optional<T> best;
    for (const T& x : xs) {
        if (!best || less(x, *best)) best = x;
    }
    return best;
}

template <class T, class P> std::vector<T> filtered(const std::vector<T>& xs, P pred) {
    std::vector<T> out;
    for (const T& x : xs) if (pred(x)) out.push_back(x);
    return out;
}

template <class T, class P> bool any(const std::vector<T>& xs, P pred) {
    for (const T& x : xs) if (pred(x)) return true;
    return false;
}

/// The point of a mission that has one at a point (every case but a
/// follow order has).
Vec2 point(const Mission& m) {
    return std::visit([](const auto& c) -> Vec2 {
        if constexpr (requires { c.at; }) return c.at; else return Vec2::zero;
    }, m.value);
}

} // namespace

// The special members that need `Commander` whole (see Simulation.h).
Simulation::Simulation(const Simulation&) = default;
Simulation::Simulation(Simulation&&) noexcept = default;
Simulation& Simulation::operator=(const Simulation&) = default;
Simulation& Simulation::operator=(Simulation&&) noexcept = default;
Simulation::~Simulation() = default;

Simulation::Simulation(const GameState& state_, const std::optional<MapDefinition>& map_, bool fog)
    : Simulation(state_, map_, std::vector<Commander>{}, fog) {}

/// `fog: false`: no fog of war, everyone sees everything (the
/// playground can switch it off).
Simulation::Simulation(const GameState& state_, const std::optional<MapDefinition>& map_,
                       const std::vector<Commander>& commanders_, bool fog)
    : state(state_), commanders(commanders_), map(map_) {
    const auto n = static_cast<size_t>(ac::max<int64_t>(static_cast<int64_t>(state.players.size()), 1));
    rallySlots.assign(n, {});
    rallyFacing.assign(n, Vec2(0, 1));
    rallyFor.assign(n, {});
    rallyPlaces.assign(n, {});
    if (map) {
        router = Router(*map);
        field = TerrainField(*map);
        nav = NavGrid(*map, *field);
        if (fog) vision = Vision(*map, *field, *nav);
        for (const auto& b : map->bases) sites.push_back(b.center);
        for (const auto& p : state.patches) {
            std::optional<int64_t> best;
            for (int64_t i = 0; i < static_cast<int64_t>(map->bases.size()); i++) {
                if (!best || distance(map->bases[size_t(i)].center, p.position)
                                 < distance(map->bases[size_t(*best)].center, p.position)) {
                    best = i;
                }
            }
            patchBase.push_back(best.value_or(0));
        }
    } else {
        sites = {state.structures.empty() ? Vec2::zero : state.structures.front().position};
        patchBase.assign(state.patches.size(), 0);
    }
    nextThink = state.time;
    refreshSupply();
    refreshNav();
    reindex();
    look();
    // Again at the first step, with whatever was added since.
    nextLook = state.time;
}

void Simulation::setFastPaths(bool on, int64_t budget) {
    fastPaths = on;
    pathBudget = std::max<int64_t>(budget, 1);
    if (nav) nav->fastSearch = on;
}

/// Rebuild the index of units by id (after `state.units` changed).
void Simulation::reindex() {
    unitIndexByID.clear();
    for (size_t i = 0; i < state.units.size(); i++) unitIndexByID[state.units[i].id] = static_cast<int64_t>(i);
}

/// Start the next game on the same map after a victory, keeping the tally.
void Simulation::newGame() {
    if (!map) return;
    const MapDefinition m = *map;
    const std::vector<Commander> cs = commanders;
    const bool fog = vision.has_value();
    const bool fast = fastPaths;
    const int64_t budget = pathBudget;
    *this = Simulation(GameState::new_(m, state.score, 0, state.teams()), m, cs, fog);
    if (fast) setFastPaths(true, budget);
}

/// Follow buildings going up and ore deposits running dry or growing
/// back: units inside something step out, and walking units find a new
/// way.
void Simulation::refreshNav() {
    if (!nav || !nav->setDynamic(state)) return;
    for (size_t i = 0; i < state.units.size(); i++) {
        auto& u = state.units[i];
        if (!(u.task != Unit::Task::inBastion && u.task != Unit::Task::aboard && u.task != Unit::Task::inDerrick
              && !u.stats().air && !u.jumpFrom)) continue;
        bool pushed = false;
        if (NavGrid::solid(u.position, state)) {
            if (auto out = nav->nearestFree(u.position)) { u.position = *out; pushed = true; }
        }
        if (!u.walking()) continue;
        if (!nav->fastSearch) { u.goal = std::nullopt; continue; }
        // Fast paths: only a walker pushed out of something, or whose route
        // (from here along its waypoints to its goal) crosses a cell that
        // went solid, asks for a new way. The rest walk on.
        if (!u.goal) continue;
        bool stale = pushed;
        Vec2 at = u.position;
        if (!stale && u.waypoints) {
            for (const Vec2& w : *u.waypoints) {
                if (nav->crossesChange(at, w)) { stale = true; break; }
                at = w;
            }
        }
        if (!stale && nav->crossesChange(at, *u.goal)) stale = true;
        if (stale) { u.goal = std::nullopt; u.waypoints = std::nullopt; }
    }
}

/// Step by `dt` real seconds; returns what happened for the renderer.
std::vector<GameEvent> Simulation::step(double dt) {
    std::vector<GameEvent> events;
    pathsStarted = 0;
    if (state.time >= nextLook - 1e-9) {
        nextLook = state.time + lookEvery;
        reindex();
        look();
    }
    state.time += dt;
    if (!commanders.empty() && state.time >= nextThink) {
        nextThink = state.time + 1;
        const std::vector<Commander> cs = commanders;
        for (const auto& c : cs) {
            if (!(!state.winner || state.allied(*state.winner, c.player))) continue;
            for (const auto& command : c.orders(*this)) issue(command, events);
        }
    }
    stepStructures(dt, events);
    refreshNav();
    if (state.time >= nextRally) {
        nextRally = state.time + 1;
        for (int64_t p = 0; p < static_cast<int64_t>(state.players.size()); p++) {
            if (!any(state.units, [&](const Unit& u) { return u.soldier() && u.owner == p; })) continue;
            placeRally(p);
            assignPlaces(p);
        }
    }
    reindex();
    atHome.clear();
    for (int64_t p = 0; p < static_cast<int64_t>(state.players.size()); p++) {
        int64_t n = 0;
        for (const auto& u : state.units) {
            if (u.owner == p && u.task == Unit::Task::idle && u.soldier() && !u.mission) n++;
        }
        atHome.push_back(n);
    }
    // Everyone alive at the start of the step acts in it, even if shot
    // down during it: fire is simultaneous, so the order units are
    // stepped in favours no player.
    std::vector<bool> alive;
    alive.reserve(state.units.size());
    for (const auto& u : state.units) alive.push_back(u.hp > 0);
    if (pilot && unitIndexByID.find(pilot->unit) == unitIndexByID.end()) pilot = std::nullopt;
    const size_t count = state.units.size();
    // PathAhead's probe: its grid is final now; it stops when the real step is over.
    if (probing && pathAhead) pathAhead->probeAtUnits();
    for (size_t i = 0; i < count; i++) {
        if (probing && pathAhead && pathAhead->probeCancelled()) break;
        Unit u = state.units[i];
        if (!alive[i]) continue;
        const Vec2 before = u.position;
        const double hp = u.hp;
        stepUnit(u, dt, events);
        u.moving = distance(before, u.position) > 1e-5;
        // Its own regen, plus the damage taken (or heals got) while it
        // acted.
        u.hp += state.units[i].hp - hp;
        state.units[i] = u;
    }
    // PathAhead's probe ends with its units: the searches are all asked.
    if (probing) return events;
    if (pilot) {
        pilot->act = false;
        pilot->ability = false;
    }
    std::vector<InFlight> due;
    std::vector<InFlight> later;
    for (auto& f : inFlight) {
        if (f.at <= state.time + 1e-9) due.push_back(f); else later.push_back(f);
    }
    inFlight = later;
    for (const auto& f : due) land(f.shooter, f.target, f.aim);
    for (const auto& l : landings) events.push_back(GameEvent::Landed{l.unit, l.target});
    landings.clear();
    if (pilot || hpBonusHeld || burnsLeft) stepLeveling(dt);
    separate();
    unstick(events);
    trapped(events);
    bury(events);
    releaseStaleMiners();
    regrow(events);
    refreshNav();
    refreshSupply();
    events.insert(events.end(), levelUps.begin(), levelUps.end());
    levelUps.clear();
    return events;
}

/// Apply an order now. False when it is not possible (cost, supply,
/// unknown unit); nothing changes then.
bool Simulation::issue(const Command& command) {
    std::vector<GameEvent> events;
    return issue(command, events);
}

bool Simulation::issue(const Command& command, std::vector<GameEvent>& events) {
    using Task = Unit::Task;
    // The unit the player drives takes no orders from the AI.
    if (pilot) {
        const int64_t p = pilot->unit;
        if (auto c = command.as<Command::Build>(); c && c->worker == p) return false;
        if (auto c = command.as<Command::Harvest>(); c && c->worker == p) return false;
        if (auto c = command.as<Command::Gather>(); c && c->worker == p) return false;
        if (auto c = command.as<Command::Resume>(); c && c->worker == p) return false;
        if (auto c = command.as<Command::Repair>(); c && c->worker == p) return false;
        if (auto c = command.as<Command::Anchor>(); c && c->unit == p) return false;
        if (auto c = command.as<Command::Mission>(); c && c->unit == p) return false;
        if (auto c = command.as<Command::Board>(); c && (c->dropship == p || c->unit == p)) return false;
        if (auto c = command.as<Command::Load>(); c && c->unit == p) return false;
    }
    auto structureIndex = [&](int64_t id) -> std::optional<size_t> {
        for (size_t i = 0; i < state.structures.size(); i++) if (state.structures[i].id == id) return i;
        return std::nullopt;
    };
    auto unitAt = [&](int64_t id) -> std::optional<size_t> {
        for (size_t i = 0; i < state.units.size(); i++) if (state.units[i].id == id) return i;
        return std::nullopt;
    };
    auto hasPlayer = [&](int64_t p) { return p >= 0 && p < static_cast<int64_t>(state.players.size()); };

    if (command.is<Command::Train>()) {
        const auto* c = command.as<Command::Train>();
        auto i = structureIndex(c->structure);
        if (!i || !state.structures[*i].complete()) return false;
        auto unitKind = c->kind ? c->kind : Rules::produces(state.structures[*i].kind);
        if (!unitKind) return false;
        const auto unit = *unitKind;
        const auto trains = Rules::trains(state.structures[*i].kind);
        if (std::find(trains.begin(), trains.end(), unit) == trains.end()
            || !(state.structures[*i].queueCount() < Rules::maxQueue)) return false;
        const Structure st = state.structures[*i];
        // A building raising its Lab trains nothing meanwhile.
        std::optional<Structure> lab;
        if (st.addon) {
            for (const auto& s : state.structures) if (s.id == *st.addon) { lab = s; break; }
        }
        if (lab && !lab->complete()) return false;
        if (Rules::needsLab(unit) && !(lab && lab->complete())) return false;
        const int64_t p = st.owner;
        const auto price = unitCost(unit, p);
        if (!(state.players[size_t(p)].ore >= price.ore && state.players[size_t(p)].hydrogen >= price.hydrogen
              && state.players[size_t(p)].supplyUsed + Rules::supply(unit) <= state.players[size_t(p)].supplyCap)) return false;
        state.players[size_t(p)].ore -= price.ore;
        state.players[size_t(p)].hydrogen -= price.hydrogen;
        if (!st.training) state.structures[*i].training = Rules::trainTime(unit);
        std::vector<Unit::Kind> line = !st.training ? std::vector<Unit::Kind>{} : st.line.value_or(std::vector<Unit::Kind>{});
        line.push_back(unit);
        state.structures[*i].line = line;
        refreshSupply();
    } else if (command.is<Command::Build>()) {
        const auto* c = command.as<Command::Build>();
        auto i = unitAt(c->worker);
        if (!i || state.units[*i].kind != Unit::Kind::prospector || state.units[*i].task == Task::building
            || state.units[*i].order) return false;
        const int64_t p = state.units[*i].owner;
        const auto kind = c->kind;
        if (!(state.players[size_t(p)].ore >= Rules::cost(kind) && state.players[size_t(p)].hydrogen >= Rules::hydrogenCost(kind)
              && kind != Structure::Kind::lab)) return false;
        if (auto need = Rules::requires_(kind)) {
            if (!any(state.structures, [&](const Structure& s) { return s.kind == *need && s.complete() && s.owner == p; })) {
                return false;
            }
        }
        // A Derrick goes on a well with nothing on it yet.
        if (kind == Structure::Kind::derrick) {
            auto g = well(c->at);
            if (!g || any(state.structures, [&](const Structure& s) { return distance(s.position, g->position) < 0.5; })) {
                return false;
            }
        }
        state.players[size_t(p)].ore -= Rules::cost(kind);
        state.players[size_t(p)].hydrogen -= Rules::hydrogenCost(kind);
        Unit u = state.units[*i];
        leavePatch(u);
        u.order = BuildOrder{kind, c->at};
        u.task = Task::toBuild;
        state.units[*i] = u;
    } else if (command.is<Command::Gather>()) {
        const auto* c = command.as<Command::Gather>();
        const int64_t patch = c->patch;
        if (!(patch >= 0 && patch < static_cast<int64_t>(state.patches.size())) || !(state.patches[size_t(patch)].remaining > 0)) {
            return false;
        }
        auto i = unitAt(c->worker);
        if (!i || state.units[*i].kind != Unit::Kind::prospector || state.units[*i].order
            || state.units[*i].task == Task::building) return false;
        Unit u = state.units[*i];
        leavePatch(u);
        u.patch = patch;
        // Off MH for good (a load it carries is still dropped off first).
        if (u.structure) {
            auto s = state.structure(*u.structure);
            if (s && s->kind == Structure::Kind::derrick) u.structure = std::nullopt;
        }
        u.task = u.carrying > 0 ? Task::toBase : Task::toPatch;
        state.units[*i] = u;
    } else if (command.is<Command::Research>()) {
        const auto* c = command.as<Command::Research>();
        const auto up = c->upgrade;
        auto i = structureIndex(c->structure);
        if (!i || state.structures[*i].kind != Structure::Kind::lab || !state.structures[*i].complete()
            || state.structures[*i].research) return false;
        std::optional<Structure> parent;
        if (auto pid = state.structures[*i].parent) {
            for (const auto& s : state.structures) if (s.id == *pid) { parent = s; break; }
        }
        if (!parent || parent->kind != ac::at(up)) return false;
        const int64_t p = state.structures[*i].owner;
        const auto price = upgradeCost(up, p);
        if (!(!has(p, up) && !any(state.structures, [&](const Structure& s) { return s.owner == p && s.research == up; })
              && state.players[size_t(p)].ore >= price.ore && state.players[size_t(p)].hydrogen >= price.hydrogen)) return false;
        state.players[size_t(p)].ore -= price.ore;
        state.players[size_t(p)].hydrogen -= price.hydrogen;
        state.structures[*i].research = up;
        state.structures[*i].researchLeft = ac::time(up);
    } else if (command.is<Command::Resume>()) {
        const auto* c = command.as<Command::Resume>();
        auto i = unitAt(c->worker);
        if (!i || state.units[*i].kind != Unit::Kind::prospector || state.units[*i].order
            || state.units[*i].task == Task::building) return false;
        auto s = state.structure(c->structure);
        if (!s || s->complete() || s->owner != state.units[*i].owner) return false;
        Unit u = state.units[*i];
        leavePatch(u);
        u.structure = c->structure;
        u.task = Task::toBuild;
        state.units[*i] = u;
    } else if (command.is<Command::Load>()) {
        const auto* c = command.as<Command::Load>();
        auto i = unitAt(c->unit);
        if (!i || state.units[*i].kind != Unit::Kind::ranger) return false;
        auto b = state.structure(c->bastion);
        if (!b || b->kind != Structure::Kind::bastion || !b->complete() || b->owner != state.units[*i].owner
            || !(static_cast<int64_t>(b->crew ? b->crew->size() : 0) < bastionCapacity(b->owner))
            || state.units[*i].task == Task::inBastion || state.units[*i].task == Task::aboard) return false;
        state.units[*i].task = Task::toBastion;
        state.units[*i].structure = c->bastion;
        state.units[*i].target = std::nullopt;
        state.units[*i].goal = std::nullopt;
    } else if (command.is<Command::Repair>()) {
        const auto* c = command.as<Command::Repair>();
        auto i = unitAt(c->worker);
        if (!i || state.units[*i].kind != Unit::Kind::prospector || state.units[*i].order
            || state.units[*i].task == Task::building) return false;
        auto s = state.structure(c->structure);
        if (!s || !s->complete() || s->owner != state.units[*i].owner) return false;
        Unit u = state.units[*i];
        leavePatch(u);
        u.structure = c->structure;
        u.task = Task::repairing;
        u.goal = std::nullopt;
        state.units[*i] = u;
    } else if (command.is<Command::Addon>()) {
        const auto* c = command.as<Command::Addon>();
        const int64_t sid = c->structure;
        auto i = structureIndex(sid);
        if (!i || !state.structures[*i].complete()) return false;
        const auto k = state.structures[*i].kind;
        if (!(k == Structure::Kind::garrison || k == Structure::Kind::foundry || k == Structure::Kind::spacedock)
            || state.structures[*i].addon || state.structures[*i].training) return false;
        const Structure st = state.structures[*i];
        const int64_t p = st.owner;
        if (!(state.players[size_t(p)].ore >= Rules::cost(Structure::Kind::lab)
              && state.players[size_t(p)].hydrogen >= Rules::hydrogenCost(Structure::Kind::lab))) return false;
        const Vec2 at = st.position + Rules::addonOffset;
        if (any(state.structures, [&](const Structure& s) {
                return s.id != sid && std::abs(s.position.x - at.x) < Rules::radius(s.kind) + 1
                    && std::abs(s.position.y - at.y) < Rules::radius(s.kind) + 1;
            })) return false;
        state.players[size_t(p)].ore -= Rules::cost(Structure::Kind::lab);
        state.players[size_t(p)].hydrogen -= Rules::hydrogenCost(Structure::Kind::lab);
        Structure lab(state.nextID, Structure::Kind::lab, p, at, Rules::buildTime(Structure::Kind::lab));
        lab.parent = sid;
        state.nextID += 1;
        state.structures[*i].addon = lab.id;
        state.structures.push_back(lab);
        events.push_back(GameEvent::ConstructionStarted{lab.id, Structure::Kind::lab, at});
    } else if (command.is<Command::Harvest>()) {
        const auto* c = command.as<Command::Harvest>();
        auto i = unitAt(c->worker);
        if (!i || state.units[*i].kind != Unit::Kind::prospector || state.units[*i].order
            || state.units[*i].task == Task::building) return false;
        auto r = state.structure(c->derrick);
        if (!r || r->kind != Structure::Kind::derrick || !r->complete() || r->owner != state.units[*i].owner) return false;
        Unit u = state.units[*i];
        leavePatch(u);
        u.patch = std::nullopt;
        u.structure = c->derrick;
        u.task = u.carrying > 0 ? Task::toBase : Task::toDerrick;
        u.goal = std::nullopt;
        state.units[*i] = u;
    } else if (command.is<Command::Anchor>()) {
        const auto* c = command.as<Command::Anchor>();
        auto i = unitIndex(c->unit);
        if (!i || (state.units[size_t(*i)].kind != Unit::Kind::longbow && state.units[size_t(*i)].kind != Unit::Kind::scorpion)) return false;
        state.units[size_t(*i)].anchored = c->on;
        if (c->on) { state.units[size_t(*i)].goal = std::nullopt; state.units[size_t(*i)].waypoints = std::nullopt; }
    } else if (command.is<Command::Mission>()) {
        const auto* c = command.as<Command::Mission>();
        const auto& m = c->mission;
        if (auto i = unitIndex(c->unit); i && state.units[size_t(*i)].kind == Unit::Kind::prospector) return sendOnErrand(*i, m);
        auto i = unitIndex(c->unit);
        if (!i || !state.units[size_t(*i)].soldier() || state.units[size_t(*i)].task == Task::inBastion
            || state.units[size_t(*i)].task == Task::aboard) return false;
        if (m && m->is<Mission::Follow>() && !canFollow(state.units[size_t(*i)], m->as<Mission::Follow>()->unit)) return false;
        auto& u = state.units[size_t(*i)];
        u.mission = m;
        u.goal = std::nullopt;
        u.target = std::nullopt;
        u.autoFollow = std::nullopt;
        if (!m) u.task = Task::toRally;
    } else if (command.is<Command::Board>()) {
        const auto* c = command.as<Command::Board>();
        return board(c->dropship, c->unit);
    } else if (command.is<Command::Attack>() || command.is<Command::Defend>()) {
        int64_t p = 0;
        std::optional<Vec2> at;
        bool defend = false;
        if (auto a = command.as<Command::Attack>()) {
            p = a->player; at = a->at;
            if (!hasPlayer(p)) return false;
            state.players[size_t(p)].reinforce = std::nullopt;
        } else if (auto d = command.as<Command::Defend>()) {
            p = d->player; at = d->at; defend = true;
            if (!hasPlayer(p)) return false;
            state.players[size_t(p)].reinforce = d->group;
        }
        if (!hasPlayer(p)) return false;
        auto& pl = state.players[size_t(p)];
        if (at && !defend && (!pl.attack || pl.defending == true)) {
            pl.waves += 1;
        }
        pl.defending = defend ? std::optional<bool>(true) : std::nullopt;
        if (at != pl.attack) {
            // New point: everyone walking to the old one heads for the new.
            for (auto& u : state.units) {
                if (u.owner == p && u.task == Task::attackMove && !u.mission) u.goal = std::nullopt;
            }
        }
        pl.attack = at;
        pl.retreating = false;
    } else if (command.is<Command::Retreat>()) {
        const auto* c = command.as<Command::Retreat>();
        const int64_t p = c->player;
        if (!hasPlayer(p)) return false;
        state.players[size_t(p)].attack = std::nullopt;
        state.players[size_t(p)].defending = std::nullopt;
        state.players[size_t(p)].retreating = true;
        state.players[size_t(p)].retreatedAt = state.time;
    } else if (command.is<Command::Request>()) {
        const auto* c = command.as<Command::Request>();
        const int64_t p = c->player;
        if (!hasPlayer(p) || !(c->count > 0)) return false;
        if (auto u = c->what.as<ac::Request::What::Upgrade>(); u && has(p, u->upgrade)) return false;
        if (c->site && !(*c->site >= 0 && *c->site < static_cast<int64_t>(sites.size()))) return false;
        Directives d = state.players[size_t(p)].orders();
        if (!d.queue.empty() && d.queue.back().what == c->what && d.queue.back().at == c->at
            && d.queue.back().site == c->site && !d.queue.back().repeats && !c->repeats && !c->site) {
            d.queue.back().count += c->count;
        } else {
            ac::Request r;
            r.id = d.nextID;
            r.what = c->what;
            r.count = c->count;
            r.repeats = c->repeats;
            r.at = c->at;
            r.site = c->site;
            d.queue.push_back(r);
            d.nextID += 1;
        }
        state.players[size_t(p)].directives = d;
    } else if (command.is<Command::CancelRequest>()) {
        const auto* c = command.as<Command::CancelRequest>();
        const int64_t p = c->player;
        if (!hasPlayer(p)) return false;
        const auto queue = state.players[size_t(p)].orders().queue;
        if (!any(queue, [&](const ac::Request& r) { return r.id == c->id; })) return false;
        if (auto& d = state.players[size_t(p)].directives) {
            std::erase_if(d->queue, [&](const ac::Request& r) { return r.id == c->id; });
        }
    } else if (command.is<Command::Keep>()) {
        const auto* c = command.as<Command::Keep>();
        const int64_t p = c->player;
        if (!hasPlayer(p)) return false;
        Directives d = state.players[size_t(p)].orders();
        d.keepOre = ac::max<int64_t>(0, c->ore);
        d.keepHydrogen = ac::max<int64_t>(0, c->hydrogen);
        state.players[size_t(p)].directives = d;
    } else if (command.is<Command::Split>()) {
        const auto* c = command.as<Command::Split>();
        const int64_t p = c->player;
        if (!hasPlayer(p)) return false;
        Directives d = state.players[size_t(p)].orders();
        d.harvest = c->harvest;
        state.players[size_t(p)].directives = d;
    } else if (command.is<Command::Objective>()) {
        const auto* c = command.as<Command::Objective>();
        const int64_t p = c->player;
        if (!hasPlayer(p)) return false;
        Directives d = state.players[size_t(p)].orders();
        ac::Objective o;
        o.id = d.nextID;
        o.kind = c->kind;
        o.at = c->at;
        if (c->kind == ac::Objective::Kind::man) {
            // The team's Bastion at the point, not manned already.
            const Vec2 at = c->at;
            auto b = minBy(filtered(state.structures, [&](const Structure& s) {
                               return s.owner == p && s.kind == Structure::Kind::bastion
                                   && distance(s.position, at) < Rules::radius(Structure::Kind::bastion) + 1.5;
                           }),
                           [&](const Structure& x, const Structure& y) { return distance(x.position, at) < distance(y.position, at); });
            if (!b || any(d.objectives, [&](const ac::Objective& x) {
                    return x.kind == ac::Objective::Kind::man && x.structure == b->id;
                })) return false;
            o.structure = b->id;
            o.at = b->position;
        }
        d.objectives.push_back(o);
        d.nextID += 1;
        state.players[size_t(p)].directives = d;
    } else if (command.is<Command::Retask>()) {
        const auto* c = command.as<Command::Retask>();
        const int64_t p = c->player;
        if (!hasPlayer(p)) return false;
        const auto objectives = state.players[size_t(p)].orders().objectives;
        std::optional<size_t> k;
        for (size_t j = 0; j < objectives.size(); j++) if (objectives[j].id == c->id) { k = j; break; }
        if (!k) return false;
        if (auto& d = state.players[size_t(p)].directives) d->objectives[*k].kind = c->kind;
    } else if (command.is<Command::CancelObjective>()) {
        const auto* c = command.as<Command::CancelObjective>();
        const int64_t p = c->player;
        if (!hasPlayer(p)) return false;
        const auto objectives = state.players[size_t(p)].orders().objectives;
        if (!any(objectives, [&](const ac::Objective& o) { return o.id == c->id; })) return false;
        if (auto& d = state.players[size_t(p)].directives) {
            std::erase_if(d->objectives, [&](const ac::Objective& o) { return o.id == c->id; });
        }
        // Its squad rejoins the army.
        for (auto& u : state.units) {
            if (!(u.owner == p && u.mission && u.mission->objective() == c->id)) continue;
            u.mission = std::nullopt;
            u.goal = std::nullopt;
            u.target = std::nullopt;
            u.task = Task::toRally;
        }
    } else if (command.is<Command::Stance>()) {
        const auto* c = command.as<Command::Stance>();
        const int64_t p = c->player;
        if (!hasPlayer(p)) return false;
        Directives d = state.players[size_t(p)].orders();
        d.stance = c->stance;
        state.players[size_t(p)].directives = d;
    } else if (command.is<Command::Pick>()) {
        const auto* c = command.as<Command::Pick>();
        return keepPick(c->player, c->perk);
    } else if (command.is<Command::Serve>()) {
        const auto* c = command.as<Command::Serve>();
        if (!issue(*c->command, events)) return false;
        const int64_t p = c->player;
        if (!hasPlayer(p)) return true;
        auto& d = state.players[size_t(p)].directives;
        if (!d) return true;
        std::optional<size_t> k;
        for (size_t j = 0; j < d->queue.size(); j++) if (d->queue[j].id == c->request) { k = j; break; }
        if (!k) return true;
        if (!d->queue[*k].repeats) {
            d->queue[*k].count -= 1;
            if (d->queue[*k].count <= 0) d->queue.erase(d->queue.begin() + static_cast<std::ptrdiff_t>(*k));
        }
    }
    return true;
}

// MARK: - Economy

/// Supply used counts units in training and queued.
void Simulation::refreshSupply() {
    for (int64_t p = 0; p < static_cast<int64_t>(state.players.size()); p++) {
        int64_t cap = 0, used = 0;
        const int64_t habDome = habDomeSupply(p);
        for (const auto& s : state.structures) {
            if (s.owner != p) continue;
            if (s.complete()) cap += s.kind == Structure::Kind::habDome ? habDome : Rules::supply(s.kind);
            if (s.line) {
                for (auto k : *s.line) used += Rules::supply(k);
            } else if (auto k = s.inTraining()) {
                used += Rules::supply(*k);
            }
        }
        for (const auto& u : state.units) if (u.owner == p) used += Rules::supply(u.kind);
        state.players[size_t(p)].supplyCap = ac::min(cap, Rules::maxSupply);
        state.players[size_t(p)].supplyUsed = used;
    }
}

void Simulation::stepStructures(double dt, std::vector<GameEvent>& events) {
    // A Sentinel looks its targets up by id: units may have died and
    // moved since the index was built.
    if (any(state.structures, [](const Structure& s) { return Rules::turret(s.kind).has_value() && s.complete(); })) reindex();
    for (size_t i = 0; i < state.structures.size(); i++) {
        Structure s = state.structures[i];
        if (auto left = s.buildLeft) {
            // A building only rises while its Prospector works on it; a
            // Lab while the building it serves stands.
            std::optional<Unit> builder;
            if (s.kind != Structure::Kind::lab && s.builder) {
                for (const auto& u : state.units) {
                    if (u.id == *s.builder) {
                        if (u.task == Unit::Task::building && u.structure == s.id) builder = u;
                        break;
                    }
                }
            }
            const bool welding = s.kind == Structure::Kind::lab
                ? (s.parent ? any(state.structures, [&](const Structure& o) { return o.id == *s.parent; }) : false)
                : builder.has_value();
            if (welding) {
                // The driven Prospector welds `Rules.heroWork` times as fast,
                // and its picks may speed up the team's Prospectors.
                const double pace = builder ? rate(Stat::Build{}, *builder) : 1;
                // Its XP: the building's worth times the share welded.
                if (builder) {
                    earn(Leveling::worth(s.kind) * ac::min(dt * pace, *left) / Rules::buildTime(s.kind), *builder);
                    const double put = Rules::hp(s.kind) * ac::min(dt * pace, *left) / Rules::buildTime(s.kind);
                    tally(*builder, [&](Tally& t) { t.built += put; });
                }
                s.buildLeft = *left - dt * pace;
                s.hp = ac::min(Rules::hp(s.kind), s.hp + 0.9 * Rules::hp(s.kind) * dt * pace / Rules::buildTime(s.kind));
                if (*s.buildLeft <= 0) {
                    s.buildLeft = std::nullopt;
                    events.push_back(GameEvent::Constructed{s.id, s.kind, s.position});
                }
            }
        } else if (auto w = Rules::turret(s.kind)) {
            stepTurret(s, *w, dt, events);
        } else if (s.research && s.researchLeft) {
            const auto up = *s.research;
            s.researchLeft = *s.researchLeft - dt * teamBoost(Stat::Research{ac::at(up)}, s.owner).rate();
            if (*s.researchLeft <= 0) {
                s.research = std::nullopt;
                s.researchLeft = std::nullopt;
                auto ups = state.players[size_t(s.owner)].upgrades.value_or(std::set<Upgrade>{});
                ups.insert(up);
                state.players[size_t(s.owner)].upgrades = ups;
                if (up == Upgrade::aegisShield) {
                    for (auto& u : state.units) {
                        if (u.owner == s.owner && u.kind == Unit::Kind::ranger) u.hp += 10;
                    }
                }
                events.push_back(GameEvent::Researched{up, s.owner, s.position});
            }
        } else if (s.training && s.inTraining()) {
            const auto kind = *s.inTraining();
            s.training = *s.training - dt * teamBoost(Stat::Training{kind}, s.owner).rate()
                * teamBoost(Stat::Production{s.kind}, s.owner).rate();
            if (*s.training <= 0) {
                // The next queued unit starts at once.
                std::vector<Unit::Kind> line = s.line.value_or(std::vector<Unit::Kind>{kind});
                line.erase(line.begin());
                s.line = line.empty() ? std::nullopt : std::optional<std::vector<Unit::Kind>>(line);
                s.training = line.empty() ? std::nullopt : std::optional<double>(Rules::trainTime(line.front()));
                const Unit u = kind == Unit::Kind::prospector ? spawnProspector(s) : spawnSoldier(kind, s);
                state.units.push_back(u);
                events.push_back(GameEvent::Trained{u.id, u.kind, u.position});
            }
        }
        state.structures[i] = s;
    }
}

/// A finished building with a weapon of its own (a Sentinel): it keeps
/// to the target it has while that stays in reach, else takes the best
/// enemy in reach it can hit (`priority`, then closest); it turns its
/// launcher there and fires when it points at it. Range is from its edge,
/// longer from high ground. Its missiles hit at once.
void Simulation::stepTurret(Structure& s, const UnitStats& w, double dt, std::vector<GameEvent>& events) {
    s.cooldown = ac::max(0.0, s.cooldown.value_or(0) - dt);
    auto reach = [&](const Target& t) -> std::optional<double> {
        if (!(state.hostile(t.owner, s.owner) && (t.air ? w.hitsAir : w.hitsGround))) return std::nullopt;
        const double d = distance(s.position, t.position) - t.radius - Rules::radius(s.kind);
        if (d <= range(w.range, s.position, t.position)) return d;
        return std::nullopt;
    };
    bool keep = false;
    if (s.target) {
        if (auto t = foe(*s.target, s.owner); t && reach(*t)) keep = true;
    }
    if (!keep) {
        struct Best { int64_t id; int64_t p; double d; };
        std::optional<Best> best;
        const double box = w.range + Rules::radius(s.kind) + 3;
        for (const auto& v : state.units) {
            if (!(state.hostile(v.owner, s.owner) && v.hp > 0)) continue;
            if (!(std::abs(v.position.x - s.position.x) < box && std::abs(v.position.y - s.position.y) < box)) continue;
            auto t = foe(v.id, s.owner);
            if (!t) continue;
            auto d = reach(*t);
            if (!d) continue;
            const int64_t p = priority(*t);
            if (!best || p > best->p || (p == best->p && *d < best->d)) best = Best{v.id, p, *d};
        }
        if (w.hitsGround) {
            for (const auto& o : state.structures) {
                if (!(state.hostile(o.owner, s.owner) && o.hp > 0)) continue;
                auto t = foe(o.id, s.owner);
                if (!t) continue;
                auto d = reach(*t);
                if (!d) continue;
                const int64_t p = priority(*t);
                if (!best || p > best->p || (p == best->p && *d < best->d)) best = Best{o.id, p, *d};
            }
        }
        s.target = best ? std::optional<int64_t>(best->id) : std::nullopt;
    }
    if (!s.target) return;
    const int64_t id = *s.target;
    auto t = foe(id, s.owner);
    if (!t) return;
    const double want = std::atan2(t->position.y - s.position.y, t->position.x - s.position.x);
    const double now = s.aim.value_or(pi / 2);
    const double diff = std::remainder(want - now, 2 * pi);
    const double turn_ = turretTurnRate * dt;
    s.aim = std::remainder(now + ac::max(-turn_, ac::min(turn_, diff)), 2 * pi);
    if (!(std::abs(diff) < 0.25 && s.cooldown.value_or(0) <= 0)) return;
    s.cooldown = w.cooldown;
    hit(id, Rules::damage(w, t->armored, t->light, t->armor));
    events.push_back(GameEvent::Shot{s.id, id, t->owner, t->position});
}

/// A new Prospector at the Citadel's edge facing its ore line.
Unit Simulation::spawnProspector(const Structure& citadel) {
    std::vector<size_t> near;
    for (size_t i = 0; i < state.patches.size(); i++) {
        if (distance(state.patches[i].position, citadel.position) < 10) near.push_back(i);
    }
    Vec2 line;
    if (near.empty()) {
        line = citadel.position + Vec2(0, 1);
    } else {
        Vec2 sum = Vec2::zero;
        for (auto i : near) sum = sum + state.patches[i].position;
        line = sum / static_cast<double>(near.size());
    }
    // Spread newcomers a little so they do not stack on one spot.
    const double spread = static_cast<double>(made(citadel.owner) % 5 - 2) * 0.35 * handed(citadel.position);
    const double a = std::atan2(line.y - citadel.position.y, line.x - citadel.position.x) + spread;
    const Vec2 dir(std::cos(a), std::sin(a));
    const Vec2 at = citadel.position + dir * Rules::depositRange;
    std::optional<Vec2> out;
    if (NavGrid::solid(at, state) && nav) out = nav->nearestFree(at);
    Unit u(state.nextID, Unit::Kind::prospector, citadel.owner, out.value_or(at), a, Unit::Task::idle);
    state.nextID += 1;
    return u;
}

/// Units a player has now: varies spawn spots the same way for both
/// players (ids interleave, so they would not).
int64_t Simulation::made(int64_t owner_) const {
    int64_t n = 0;
    for (const auto& u : state.units) n += u.owner == owner_ ? 1 : 0;
    return n;
}

/// -1 on the east half of a mirrored map, where a sideways offset must
/// go the other way to mirror the west's; else 1.
double Simulation::handed(Vec2 p) const {
    if (!map || !map->mirrorX || !(p.x > *map->mirrorX)) return 1;
    return -1;
}

/// A new soldier at its building's door (the +Z side; a ship lifts off
/// the Spacedock's deck), off to the rally.
Unit Simulation::spawnSoldier(Unit::Kind kind, const Structure& b) {
    const double spread = static_cast<double>(made(b.owner) % 3 - 1) * 0.3 * handed(b.position);
    const Vec2 door = b.position + Vec2(spread, Rules::radius(b.kind) + 0.2 + Rules::radius(kind));
    Vec2 at = door;
    if (Rules::stats(kind).air) {
        at = b.position;
    } else if (nav) {
        at = nav->nearestFree(door).value_or(door);
    }
    Unit u = freshUnit(kind, state.nextID, b.owner, at, pi / 2, Unit::Task::toRally);
    state.nextID += 1;
    return u;
}

/// A unit as it comes out of its building: full health (upgrades
/// counted), a Dropship with 50 energy and an empty hold, a tank in tank
/// mode with its turret along its hull.
Unit Simulation::freshUnit(Unit::Kind kind, int64_t id, int64_t owner_, Vec2 position, double heading,
                           Unit::Task task) const {
    Unit u(id, kind, owner_, position, heading, task);
    u.hp = maxHP(u);
    if (kind == Unit::Kind::dropship) { u.energy = 50; u.cargo = std::vector<int64_t>{}; }
    if (kind == Unit::Kind::longbow) { u.anchor = 0; u.anchored = false; u.aim = heading; }
    if (kind == Unit::Kind::scorpion) { u.anchor = 0; u.anchored = false; }
    if (kind == Unit::Kind::firefly || kind == Unit::Kind::hailstorm || kind == Unit::Kind::atlas) u.aim = heading;
    return u;
}

/// Index into `state.units` of the unit with this id.
std::optional<int64_t> Simulation::unitIndex(int64_t id) const {
    if (auto it = unitIndexByID.find(id); it != unitIndexByID.end()) {
        const int64_t i = it->second;
        if (i >= 0 && i < static_cast<int64_t>(state.units.size()) && state.units[size_t(i)].id == id) return i;
    }
    for (size_t i = 0; i < state.units.size(); i++) if (state.units[i].id == id) return static_cast<int64_t>(i);
    return std::nullopt;
}

/// The well at a point (within half a cell).
std::optional<Well> Simulation::well(Vec2 p) const {
    if (!state.wells) return std::nullopt;
    for (const auto& w : *state.wells) if (distance(w.position, p) < 0.5) return w;
    return std::nullopt;
}

// MARK: - Army

/// A player's army gathers in a block in front of its base nearest the
/// map's centre, a few cells out along the way there (toward its ramp),
/// facing out. Places are on flat ground of that level, clear of
/// buildings, resources and doodads. A block 11 wide fills first, front
/// row first and centre out; an army past it (up to the 200 supply cap)
/// spills onto wings 4 wide on either side. A base with enemy soldiers on it is skipped while another is safe,
/// so new Rangers never gather in the middle of the enemy army.
void Simulation::placeRally(int64_t player) {
    std::vector<Structure> own = bases(player);
    const auto safe = filtered(own, [&](const Structure& b) {
        return !any(state.units, [&](const Unit& u) {
            return state.hostile(u.owner, player) && u.soldier() && distance(u.position, b.position) < 16;
        });
    });
    if (!safe.empty()) own = safe;
    std::optional<Structure> home;
    if (own.empty()) {
        for (const auto& s : state.structures) if (s.owner == player) { home = s; break; }
    }
    const Vec2 c = centre();
    auto nearest = minBy(own, [&](const Structure& x, const Structure& y) { return walk(x.position, c) < walk(y.position, c); });
    if (!nearest) nearest = home;
    if (!nearest) return;
    const Structure from = *nearest;
    std::vector<int64_t> key{from.id};
    for (const auto& s : state.structures) if (s.owner == player) key.push_back(s.complete() ? s.id : -s.id);
    if (key == rallyFor[size_t(player)]) return;
    rallyFor[size_t(player)] = key;
    Vec2 rally = from.position + Vec2(0, 7), facing(0, 1);
    if (map) {
        // Walk 8 cells along the route toward the centre.
        Vec2 at = from.position;
        double left = 8.0;
        std::vector<Vec2> way{from.position};
        if (router) {
            const auto r = router->route(from.position, c);
            way.insert(way.end(), r.begin(), r.end());
        } else {
            way.push_back(c);
        }
        for (const Vec2 p : way) {
            const double d = distance(at, p);
            if (!(d > 1e-6)) continue;
            facing = (p - at) / d;
            if (d >= left) { at += facing * left; left = 0; break; }
            left -= d; at = p;
        }
        rally = at;
    }
    const Vec2 right(-facing.y, facing.x);
    std::vector<std::pair<Vec2, double>> candidates;
    for (int64_t row = 0; row < 14; row++) {
        for (int64_t col = -9; col <= 9; col++) {
            const Vec2 p = rally - facing * (static_cast<double>(row) * 0.95) + right * (static_cast<double>(col) * 0.95);
            // Ties go to one side, mirrored on a mirrored map's east.
            const double wing = std::abs(col) > 5 ? 1000.0 : 0;
            candidates.emplace_back(p, wing + static_cast<double>(row) * 20 + static_cast<double>(std::abs(col))
                                           + (static_cast<double>(col) * handed(rally) < 0 ? 0.5 : 0));
        }
    }
    std::optional<int64_t> level;
    double height = 0;
    if (field) { level = field->level(rally); height = field->height(rally); }
    // Only places the army can walk to from its base (not a pocket
    // boxed in by buildings).
    std::optional<int64_t> reach;
    if (nav) reach = nav->region(from.position + facing * (Rules::radius(from.kind) + 1));
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& x, const auto& y) { return x.second < y.second; });
    std::vector<Vec2> slots;
    for (const auto& cand : candidates) {
        const Vec2 p = cand.first;
        if (field && nav) {
            if (!(nav->walkable(p) && field->level(p) == level && std::abs(field->height(p) - height) < 0.4
                  && (!reach || nav->region(p) == reach))) continue;
        }
        if (any(state.structures, [&](const Structure& s) { return distance(s.position, p) < Rules::radius(s.kind) + 0.8; })) continue;
        if (any(state.patches, [&](const OreDeposit& o) { return distance(o.position, p) < 1.6; })) continue;
        slots.push_back(p);
    }
    // New places: everyone is placed afresh.
    if (slots != rallySlots[size_t(player)]) rallyPlaces[size_t(player)].clear();
    rallySlots[size_t(player)] = slots;
    rallyFacing[size_t(player)] = facing;
}

Vec2 Simulation::centre() const { return map ? map->front() : sites[0]; }

/// In the army: a soldier with no errand of its own, out in the field.
/// A raider picks workers first (Comets, Fireflies, a drop, hunters);
/// a squad picks fights like the army.
bool Simulation::raids(const Unit& u) { return u.mission && !u.mission->objective(); }

/// A squad soldier's place around its point: spread on a golden-angle
/// spiral by id, so they do not all crowd one spot.
Vec2 Simulation::spot(const Unit& u, Vec2 p) const {
    const double k = static_cast<double>(u.id % 24);
    const double a = k * 2.39996, r = 0.9 * std::sqrt(k + 0.5);
    const Vec2 q = p + Vec2(std::cos(a), std::sin(a)) * r;
    if (!nav || u.stats().air) return q;
    return nav->walkable(q) ? q : nav->nearestFree(q, 3).value_or(p);
}

/// Where the player's army gathers: the front of its rally (nil: it
/// has none yet).
std::optional<Vec2> Simulation::rallyPoint(int64_t p) const {
    if (!(p >= 0 && p < static_cast<int64_t>(rallySlots.size())) || rallySlots[size_t(p)].empty()) return std::nullopt;
    return rallySlots[size_t(p)].front();
}

bool Simulation::inArmy(const Unit& u) const {
    return u.soldier() && !u.mission && u.task != Unit::Task::inBastion && u.task != Unit::Task::toBastion
        && u.task != Unit::Task::aboard && !(pilot && pilot->unit == u.id);
}

/// A soldier's place in its army: soldiers take places in id order.
int64_t Simulation::rank(const Unit& u) const {
    int64_t n = 0;
    for (const auto& v : state.units) if (v.owner == u.owner && v.id < u.id && inArmy(v)) n++;
    return n;
}

/// The rally place of a soldier (one that joined the army since the
/// last hand-out takes the place its rank gives it).
std::optional<Vec2> Simulation::slot(const Unit& u) const {
    const auto& slots = rallySlots[size_t(u.owner)];
    if (slots.empty()) return std::nullopt;
    if (u.owner >= 0 && u.owner < static_cast<int64_t>(rallyPlaces.size())) {
        const auto& places = rallyPlaces[size_t(u.owner)];
        if (auto it = places.find(u.id); it != places.end() && it->second >= 0
            && it->second < static_cast<int64_t>(slots.size())) return slots[size_t(it->second)];
    }
    return slots[size_t(ac::min(rank(u), static_cast<int64_t>(slots.size()) - 1))];
}

/// Hand out the rally places, once a second. Places are 0.95 apart,
/// one per Ranger; a tank is 1.75 wide. Everyone keeps its place while
/// it is still good, so a soldier joining or dying moves nobody else
/// (planted tanks would pack up and drive across each other). Tanks
/// keep theirs first; the rest keep theirs unless a tank covers it, and
/// newcomers take the front-most free places; a new tank takes the first
/// place behind the rest that is a tank's width from everyone. Tanks
/// never stand on other soldiers' places, where those used to push
/// against them for good.
void Simulation::assignPlaces(int64_t p) {
    const auto slots = rallySlots[size_t(p)];
    if (slots.empty()) { rallyPlaces[size_t(p)].clear(); return; }
    auto army = filtered(state.units, [&](const Unit& u) { return u.owner == p && inArmy(u); });
    std::stable_sort(army.begin(), army.end(), [](const Unit& a, const Unit& b) { return a.id < b.id; });
    const auto old = rallyPlaces[size_t(p)];
    const double wide = Rules::radius(Unit::Kind::longbow);
    std::map<int64_t, int64_t> places;
    std::set<int64_t> used;
    std::vector<Vec2> small, tanks;
    const int64_t n = static_cast<int64_t>(slots.size());
    auto fitsTank = [&](int64_t i) {
        if (used.count(i)) return false;
        for (const Vec2 s : small) if (!(distance(s, slots[size_t(i)]) >= wide + Rules::radius(Unit::Kind::ranger) + 0.1)) return false;
        for (const Vec2 t : tanks) if (!(distance(t, slots[size_t(i)]) >= 2 * wide + 0.1)) return false;
        return true;
    };
    auto fitsSmall = [&](int64_t i) {
        if (used.count(i)) return false;
        for (const Vec2 t : tanks) if (!(distance(t, slots[size_t(i)]) >= wide + Rules::radius(Unit::Kind::ranger) + 0.1)) return false;
        return true;
    };
    auto take = [&](const Unit& u, int64_t i) {
        places[u.id] = i;
        used.insert(i);
        if (u.kind == Unit::Kind::longbow) tanks.push_back(slots[size_t(i)]);
        else if (!u.stats().air) small.push_back(slots[size_t(i)]);
    };
    auto firstFrom = [&](int64_t start, auto fits) -> std::optional<int64_t> {
        for (int64_t i = start; i < n; i++) if (fits(i)) return i;
        return std::nullopt;
    };
    const auto tankers = filtered(army, [](const Unit& u) { return u.kind == Unit::Kind::longbow; });
    const auto rest = filtered(army, [](const Unit& u) { return u.kind != Unit::Kind::longbow; });
    for (const auto& u : tankers) {
        if (auto it = old.find(u.id); it != old.end() && it->second >= 0 && it->second < n && fitsTank(it->second)) take(u, it->second);
    }
    for (const auto& u : rest) {
        if (auto it = old.find(u.id); it != old.end() && it->second >= 0 && it->second < n && fitsSmall(it->second)) take(u, it->second);
    }
    for (const auto& u : rest) {
        if (places.count(u.id)) continue;
        take(u, firstFrom(0, fitsSmall).value_or(n - 1));
    }
    int64_t top = -1;
    for (const auto& [_, i] : places) top = ac::max(top, i);
    const int64_t behind = top + 1;
    for (const auto& u : tankers) {
        if (places.count(u.id)) continue;
        auto i = firstFrom(behind, fitsTank);
        if (!i) i = firstFrom(0, fitsTank);
        take(u, i.value_or(n - 1));
    }
    rallyPlaces[size_t(p)] = places;
}

/// Where a soldier stands around its army's attack point: a sunflower
/// spread, so the army arrives as a blob and not a single file.
Vec2 Simulation::attackSpot(const Unit& u, Vec2 a) const {
    const double k = static_cast<double>(rank(u));
    const double angle = k * 2.39996;
    const Vec2 p = a + Vec2(std::cos(angle), std::sin(angle)) * 0.62 * std::sqrt(k);
    if (!nav || u.stats().air || !(!nav->walkable(p) || nav->inRock(p))) return p;
    return nav->nearestFree(p).value_or(a);
}

std::optional<Simulation::Target> Simulation::target(int64_t id) const {
    if (auto it = unitIndexByID.find(id); it != unitIndexByID.end() && it->second >= 0
        && it->second < static_cast<int64_t>(state.units.size()) && state.units[size_t(it->second)].id == id) {
        const Unit& u = state.units[size_t(it->second)];
        const UnitStats& st = u.stats();
        if (!(u.hp > 0 && u.task != Unit::Task::inBastion && u.task != Unit::Task::aboard && u.task != Unit::Task::inDerrick)) {
            return std::nullopt;
        }
        const double armor = !pilot ? st.armor : st.armor + boost(Stat::Armor{}, u).plus;
        Target t;
        t.position = u.position;
        t.radius = st.radius;
        t.structure = false;
        t.owner = u.owner;
        t.kind = u.kind;
        t.armed = (st.hitsGround || st.hitsAir) && u.kind != Unit::Kind::prospector;
        t.air = st.air;
        t.armored = st.armored;
        t.light = st.light;
        t.armor = armor;
        return t;
    }
    for (const auto& s : state.structures) {
        if (s.id != id) continue;
        if (!(s.hp > 0)) return std::nullopt;
        Target t;
        t.position = s.position;
        t.radius = Rules::radius(s.kind);
        t.structure = true;
        t.owner = s.owner;
        t.armed = s.crew && !s.crew->empty();
        return t;
    }
    return std::nullopt;
}

/// Edge-to-edge distance from a unit to a target.
double Simulation::gap(const Unit& u, const Target& t) const {
    return distance(u.position, t.position) - t.radius - Rules::radius(u.kind);
}

/// A Ranger's range from `from` at `to`: longer by
/// `highGroundRangeBonus` if and only if `from` is on a higher level.
double Simulation::range(Vec2 from, Vec2 to) const { return range(Rules::rangerRange, from, to); }

/// `base` range, plus the high-ground bonus if and only if `from` is on
/// a higher level than `to`.
double Simulation::range(double base, Vec2 from, Vec2 to) const {
    if (!field || !(field->level(from) > field->level(to))) return base;
    return base + Rules::highGroundRangeBonus;
}

/// A unit's weapon now: an anchored Longbow uses its shock cannon, a Ranger
/// its side's mini gun once researched. The driven unit's reaches
/// `Rules.heroRange` times as far; leveling picks change its range and
/// rate of fire (`boost`).
UnitStats Simulation::weapon(const Unit& u, bool air) const {
    UnitStats w = u.kind == Unit::Kind::kestrel && air ? UnitStats::railgun
        : u.kind == Unit::Kind::longbow && u.anchor.value_or(0) >= 1 ? UnitStats::anchor
        : u.kind == Unit::Kind::ranger && has(u.owner, Upgrade::minigun) ? UnitStats::minigun : u.stats();
    // Flak mount: the driven Atlas's cannons also fire at flyers.
    if (u.kind == Unit::Kind::atlas && air && pilot && flakShare(u)) w.hitsAir = true;
    if (pilot) {
        // A Firefly's range is its flame's reach.
        if (u.kind == Unit::Kind::firefly) w.range = boost(Stat::FlameReach{}, u).apply(w.range);
        w.range = boost(Stat::Range{}, u).apply(w.range);
        if (u.kind == Unit::Kind::longbow && u.anchor.value_or(0) >= 1) w.range += boost(Stat::AnchorRange{}, u).plus;
        w.cooldown /= rate(Stat::FireRate{}, u);
    }
    return w;
}

/// `u` just fired weapon `w`: it is ready again after the cooldown. A
/// burst weapon fires for `w.burst` s and then pauses `w.pause` s; one
/// that has been quiet for a pause has reloaded already, and starts a
/// fresh burst. Its rounds keep their beat: one fired a step late (the
/// step's length, 1/30 s against 0.2) leaves the next due on time.
void Simulation::reload(Unit& u, const UnitStats& w) {
    // A Scorpion's sting gives it away to all for `Rules.strikeReveal` s.
    if (u.kind == Unit::Kind::scorpion) u.struckAt = state.time;
    if (w.rail) { u.railCooldown = w.cooldown; return; }
    u.cooldown = w.cooldown;
    if (!(w.burst > 0)) return;
    const double now = state.time;
    if (u.firedAt && now - *u.firedAt >= w.pause) u.burstFrom = std::nullopt;
    double due = now;
    if (u.burstFrom && u.firedAt) {
        const double late = now - (*u.firedAt + w.cooldown);
        if (late > 0 && late < w.cooldown) due = now - late;
    }
    const double from = u.burstFrom.value_or(now);
    u.firedAt = due;
    u.cooldown = w.cooldown - (now - due);
    if (due + w.cooldown - from >= w.burst - 1e-6) {
        // The next round would fall past the burst: pause from its end.
        u.cooldown = from + w.burst - now + w.pause;
        u.burstFrom = std::nullopt;
    } else {
        u.burstFrom = from;
    }
}

/// A player has finished this upgrade.
bool Simulation::has(int64_t player, Upgrade up) const {
    return player >= 0 && player < static_cast<int64_t>(state.players.size())
        && state.players[size_t(player)].upgrades && state.players[size_t(player)].upgrades->count(up) > 0;
}

/// A unit's full hit points, its side's upgrades and leveling picks
/// counted.
double Simulation::maxHP(const Unit& u) const {
    return Rules::hp(u.kind) + (u.kind == Unit::Kind::ranger && has(u.owner, Upgrade::aegisShield) ? 10 : 0)
        + (!pilot ? 0 : boost(Stat::Hp{}, u).plus);
}

/// Seconds until the weapon it uses against `air` targets is ready.
double Simulation::cooldownFor(const Unit& u, bool air) const {
    return (u.kind == Unit::Kind::kestrel && air ? u.railCooldown : u.cooldown).value_or(0);
}

/// It can shoot this kind of target at all (air or ground).
bool Simulation::canHit(const Unit& u, const Target& t) const {
    if (u.kind == Unit::Kind::scorpion && t.structure) return pilot && sapperShare(u).has_value();
    const UnitStats w = weapon(u, t.air);
    return t.air ? w.hitsAir : w.hitsGround;
}

/// Target priority: units that shoot back first, then workers,
/// then buildings. A raider hunts workers first.
int64_t Simulation::priority(const Target& t, bool raider) const {
    if (raider && t.kind == Unit::Kind::prospector) return 4;
    return t.armed ? 3 : t.structure ? 1 : 2;
}

/// The best enemy in sight that it can hit: highest priority; then
/// what it can shoot from where it stands, the one closest to dying
/// first (so soldiers side by side focus their fire and kill, rather
/// than wound many); then the closest. An anchored Longbow skips what
/// is inside its minimum range. `inRange`: only what it can shoot from
/// where it stands.
std::optional<int64_t> Simulation::acquire(const Unit& u, bool inRange) const {
    struct Best { int64_t id; int64_t p; bool now; double hp; double d; };
    std::optional<Best> best;
    // A Scorpion stings only buried, and never chases: only what is in reach.
    if (u.kind == Unit::Kind::scorpion) { if (!u.burrowed()) return std::nullopt; inRange = true; }
    const bool raider = raids(u);
    const UnitStats ground = weapon(u), flier = weapon(u, true);
    const UnitStats w{ground.range >= flier.range ? ground : flier};
    auto consider = [&](int64_t id, const Target& t, double hp) {
        if (!canHit(u, t)) return;
        const UnitStats& x = t.air ? flier : ground;
        const double d = gap(u, t);
        const double shoot = range(x.range, u.position, t.position);
        const double reach = inRange ? shoot : ac::max(Rules::sight, x.range + 1);
        if (!(d <= reach && d >= x.minRange)) return;
        const int64_t p = priority(t, raider);
        const bool now = d <= shoot;
        if (best) {
            const Best& b = *best;
            if (!(p > b.p || (p == b.p && ((now && !b.now) || (now == b.now && (now ? hp < b.hp : d < b.d)))))) return;
        }
        best = Best{id, p, now, hp, d};
    };
    const double reach = ac::max(Rules::sight, w.range + 1) + 2;
    for (const auto& v : state.units) {
        if (!(state.hostile(v.owner, u.owner) && v.hp > 0)) continue;
        if (!(std::abs(v.position.x - u.position.x) < reach && std::abs(v.position.y - u.position.y) < reach)) continue;
        auto t = foe(v.id, u.owner);
        if (!t) continue;
        consider(v.id, *t, v.hp);
    }
    for (const auto& s : state.structures) {
        if (!(state.hostile(s.owner, u.owner) && s.hp > 0)) continue;
        if (!(std::abs(s.position.x - u.position.x) < reach + 3 && std::abs(s.position.y - u.position.y) < reach + 3)) continue;
        auto t = foe(s.id, u.owner);
        if (!t) continue;
        consider(s.id, *t, s.hp);
    }
    if (!best) return std::nullopt;
    return best->id;
}

/// Hit points left on a unit or building (0 when gone).
double Simulation::hpLeft(int64_t id) const {
    if (auto it = unitIndexByID.find(id); it != unitIndexByID.end() && it->second >= 0
        && it->second < static_cast<int64_t>(state.units.size()) && state.units[size_t(it->second)].id == id) {
        return state.units[size_t(it->second)].hp;
    }
    for (const auto& s : state.structures) if (s.id == id) return s.hp;
    return 0;
}

/// Where a soldier fighting `t` steps back to instead of standing
/// still, or nil to stand and fight:
/// - kiting: a ranged soldier between shots backs off from an armed
///   enemy it outranges by a cell and is not slower than, keeping it
///   near the edge of its reach;
/// - a badly hurt bio soldier being shot steps out of the line while
///   healthy friends stand by to take its place (to a Dropship's
///   heal, or a Comet's own).
std::optional<Vec2> Simulation::backOff(const Unit& u, const Target& t, const UnitStats& w, double d, double reach) const {
    // A Longbow's own answer is anchoring (`stepAnchor`).
    if (!(u.stats().speed > 0 && u.kind != Unit::Kind::longbow && u.kind != Unit::Kind::scorpion && w.minRange == 0)) return std::nullopt;
    const bool kite = u.cooldown.value_or(0) > kiteWindow && d < reach - 1 && outranges(u, t, w);
    if (!(kite || wounded(u))) return std::nullopt;
    // Keep the spot it is already backing off to.
    if (u.goal && distance(*u.goal, u.position) > 0.3
        && distance(*u.goal, t.position) > distance(u.position, t.position) + 0.5) return u.goal;
    const Vec2 off = u.position - t.position;
    if (!(length(off) > 0.01)) return std::nullopt;
    const Vec2 p = u.position + normalize(off) * (kite ? 1.5 : 3);
    if (!u.stats().air && nav && (!nav->walkable(p) || nav->inRock(p))) return std::nullopt;
    return p;
}

/// `t` is an armed unit that `u` outranges by a cell and is not
/// slower than, and that can shoot `u` at all: worth kiting.
bool Simulation::outranges(const Unit& u, const Target& t, const UnitStats& w) const {
    if (t.structure || !t.armed || !t.kind) return false;
    const UnitStats& them = Rules::stats(*t.kind);
    return (u.stats().air ? them.hitsAir : them.hitsGround) && them.range + 1 <= w.range
        && them.speed <= u.stats().speed * 1.15;
}

/// A bio soldier under a third of its hit points, shot within the
/// last `woundedQuiet` seconds, out with at least three healthy friends close by.
bool Simulation::wounded(const Unit& u) const {
    if (!(u.stats().bio && u.hp < woundedShare * maxHP(u) && state.time - u.hitAt.value_or(-100) < woundedQuiet
          && !raids(u))) return false;
    int64_t friends = 0;
    for (const auto& v : state.units) {
        if (!(v.owner == u.owner && v.id != u.id && v.soldier() && v.hp > 0.6 * v.stats().hp)) continue;
        if (!(std::abs(v.position.x - u.position.x) < 5 && std::abs(v.position.y - u.position.y) < 5)) continue;
        friends += 1;
        if (friends >= 3) return true;
    }
    return false;
}

/// Soldiers fight whatever enemy comes in sight, attack-move with their
/// army, and otherwise stand at the rally. A retreating army walks home
/// without stopping to shoot. A soldier on an errand raids its point
/// (or falls back) on its own.
void Simulation::stepSoldier(Unit& u, double dt, std::vector<GameEvent>& events) {
    using Task = Unit::Task;
    const UnitStats st = u.stats();
    u.cooldown = ac::max(0.0, u.cooldown.value_or(0) - dt);
    if (u.railCooldown) u.railCooldown = ac::max(0.0, *u.railCooldown - dt);
    u.timer -= dt;
    const bool think = u.timer <= 0;
    if (think) u.timer = 0.25;
    if (u.task == Task::inBastion || u.task == Task::toBastion) { stepInBastion(u, dt, think, events); return; }
    if (u.task == Task::aboard) return;
    if (u.kind == Unit::Kind::longbow && think) stepAnchor(u);
    if (u.kind == Unit::Kind::scorpion && think) stepBury(u);
    upkeep(u, dt);
    if (u.kind == Unit::Kind::atlas && think) stompCrowd(u, events);
    if (u.kind == Unit::Kind::dropship) { stepDropship(u, dt, think); return; }
    if (u.kind == Unit::Kind::peregrine && think) autoEscort(u);
    if (u.mission && u.mission->is<Mission::Follow>() && stepFollow(u, dt, think, events)) return;
    const Player me = state.players[size_t(u.owner)];
    std::optional<Vec2> raid, home;
    if (u.mission) {
        const Mission& m = *u.mission;
        if (auto r = m.as<Mission::Raid>()) raid = r->at;
        else if (auto h = m.as<Mission::Hunt>()) raid = h->at;
        else if (auto a = m.as<Mission::Assault>()) raid = spot(u, a->at);
        else if (auto o = m.as<Mission::Hold>()) raid = spot(u, o->at);
        else if (auto f = m.as<Mission::FallBack>()) home = f->at;
        else if (auto g = m.as<Mission::Regroup>()) home = spot(u, g->at);
    }
    const bool fleeing = home.has_value() || (me.retreating && !u.mission);
    auto postOf = [&]() -> std::optional<Vec2> {
        if (raid) return raid;
        return me.defending == true ? me.attack : !me.attack ? slot(u) : std::nullopt;
    };

    if (fleeing) {
        if (u.task == Task::attacking || u.task == Task::attackMove) { u.task = Task::toRally; u.goal = std::nullopt; }
        // Shoot in passing: at an enemy already in range, never chasing
        // it. With the weapon ready it stops to turn and fire (a tank's
        // turret turns as it drives); between shots it walks on.
        if (think) u.target = acquire(u, true);
        std::optional<Target> t;
        if (u.target) t = foe(*u.target, u.owner);
        if (u.target && t && state.hostile(t->owner, u.owner) && canHit(u, *t)) {
            const int64_t id = *u.target;
            const UnitStats w = weapon(u, t->air);
            const double d = gap(u, *t);
            if (d <= range(w.range, u.position, t->position) && d >= w.minRange && cooldownFor(u, t->air) <= 0) {
                if (fire(u, id, *t, dt)) events.push_back(GameEvent::Shot{u.id, id, t->owner, t->position});
                if (u.kind != Unit::Kind::longbow) return;
            }
        } else {
            u.target = std::nullopt;
        }
    } else if (think) {
        if (auto best = acquire(u)) {
            // Switch when something more wanted comes in sight, and, among
            // the equally wanted, to one it can shoot now that is closer to
            // dying (focus fire).
            std::optional<Target> now;
            if (u.target) now = foe(*u.target, u.owner);
            const bool raider = raids(u);
            std::optional<Target> t;
            if (now && canHit(u, *now)) t = target(*best);
            if (now && canHit(u, *now) && t) {
                const int64_t p = priority(*t, raider), q = priority(*now, raider);
                auto inReach = [&](const Target& x) { return gap(u, x) <= range(weapon(u, x.air).range, u.position, x.position); };
                if (p > q || (p == q && inReach(*t) && (!inReach(*now) || hpLeft(*best) < hpLeft(*u.target)))) u.target = best;
            } else {
                u.target = best;
            }
        }
    }
    if (u.kind == Unit::Kind::scorpion) {
        if (!u.burrowed()) u.target = std::nullopt;
        if (!u.target) u.lockTarget = std::nullopt;
    }
    if (!fleeing && u.target) {
        const int64_t id = *u.target;
        if (auto t = foe(id, u.owner); t && state.hostile(t->owner, u.owner) && canHit(u, *t)) {
            const UnitStats w = weapon(u, t->air);
            const double d = gap(u, *t);
            const double reach = range(w.range, u.position, t->position);
            if (auto away = backOff(u, *t, w, d, reach)) {
                u.task = Task::attacking;
                (void)travel(u, *away, 0.1, dt);
                return;
            }
            if (d <= reach && d >= w.minRange) {
                u.task = Task::attacking;
                u.goal = std::nullopt; u.waypoints = std::nullopt;
                if (fire(u, id, *t, dt)) events.push_back(GameEvent::Shot{u.id, id, t->owner, t->position});
                return;
            }
            // A tank anchoring or anchored cannot chase: it waits for targets.
            const bool planted = u.planted();
            // Chase while it stays near; re-path only when it has moved a
            // cell. Defenders and the rally guard stay close to home.
            const auto post = postOf();
            const bool leashed = post ? distance(u.position, *post) > 14 : false;
            if (d <= Rules::sight + 3 && !leashed && !planted && d >= w.minRange) {
                u.task = Task::attacking;
                const Vec2 to = u.goal ? (distance(*u.goal, t->position) < 1 ? *u.goal : t->position) : t->position;
                (void)travel(u, to, reach + t->radius + st.radius - 0.4, dt);
                return;
            }
        }
        u.target = std::nullopt;
        u.goal = std::nullopt;
        if (u.task == Task::attacking) u.task = !me.attack || u.mission ? Task::toRally : Task::attackMove;
    }
    if (!fleeing && !u.target && u.shotFrom) {
        const Vec2 from = *u.shotFrom;
        // Shot by something its side cannot see: it heads for where the
        // shot came from (up the ramp to a high ground) for a few
        // seconds, unless planted or kept near its point.
        const auto post = postOf();
        const bool leashed = post ? distance(u.position, *post) > 14 : false;
        if (state.time - u.hitAt.value_or(-100) < Rules::unseenChase && !leashed && u.anchor.value_or(0) == 0 && st.speed > 0) {
            u.task = Task::attacking;
            (void)travel(u, from, 1, dt);
            return;
        }
        u.shotFrom = std::nullopt;
        if (u.task == Task::attacking) u.task = !me.attack || u.mission ? Task::toRally : Task::attackMove;
    }
    if (auto p = raid ? raid : home) {
        // On its own errand: to the point, then hold there (a raider
        // hunts what it finds; one falling back waits at home).
        u.task = Task::toRally;
        if (distance(u.position, *p) > 1.5) (void)travel(u, *p, 1, dt);
        else u.task = Task::idle;
        return;
    }
    if (me.attack && joins(u, *me.attack)) {
        const Vec2 a = *me.attack;
        // Attack-move: walk to its place around the army's point,
        // fighting on the way; once there, face the point.
        if (u.task != Task::attackMove) { u.task = Task::attackMove; u.goal = std::nullopt; }
        if (u.goal && distance(*u.goal, a) < 12) {
            (void)travel(u, *u.goal, 0.1, dt);
        } else if (u.goal || think) {
            u.goal = std::nullopt;
            const Vec2 spot_ = attackSpot(u, a);
            if (distance(u.position, spot_) > 0.3) (void)travel(u, spot_, 0.1, dt);
        } else {
            turn(u, a, dt, 3);
        }
        return;
    }
    switch (u.task) {
    case Task::toRally:
    case Task::attackMove:
    case Task::attacking: {
        if (u.task != Task::toRally) { u.task = Task::toRally; u.goal = std::nullopt; }
        // Look up its place a few times a second; walk on toward it meanwhile.
        std::optional<Vec2> to = u.goal;
        if (!to || think) to = slot(u);
        if (!to) return;
        if (travel(u, *to, 0.05, dt)) { u.task = Task::idle; u.timer = 1; }
        break;
    }
    default: {
        // At ease, facing out; check its place now and then. An anchored
        // tank stays planted unless its place has moved well away (the
        // army's ranks shift as soldiers come and go).
        if (u.anchor.value_or(0) == 0) turn(u, u.position + rallyFacing[size_t(u.owner)], dt, 3);
        if (!think) return;
        const double slack = u.anchor.value_or(0) > 0 ? plantedSlack : 0.3;
        if (auto to = slot(u); to && distance(*to, u.position) > slack) u.task = Task::toRally;
        break;
    }
    }
}

/// Turn to the target and shoot when the weapon is ready; true when it
/// fired. A tank and a Hailstorm turn their turret and a Firefly its
/// tail, not the hull; a tank cannot fire while it anchors or packs up.
bool Simulation::fire(Unit& u, int64_t id, const Target& t, double dt) {
    const UnitStats w = weapon(u, t.air);
    bool ready;
    if (u.kind == Unit::Kind::longbow || u.kind == Unit::Kind::firefly || u.kind == Unit::Kind::hailstorm
        || u.kind == Unit::Kind::atlas) {
        const double want = std::atan2(t.position.y - u.position.y, t.position.x - u.position.x);
        const double now = u.aim.value_or(u.heading);
        const double diff = std::remainder(want - now, 2 * pi);
        const double turnRate = u.kind == Unit::Kind::firefly ? 6.0 : u.kind == Unit::Kind::hailstorm ? 5.0
            : u.kind == Unit::Kind::atlas ? Rules::atlasTorsoRate : 4.0;
        u.aim = std::remainder(now + ac::max(-turnRate * dt, ac::min(turnRate * dt, diff)), 2 * pi);
        const double anchor = u.anchor.value_or(0);
        ready = std::abs(diff) < 0.2 && (anchor == 0 || anchor >= 1);
    } else {
        ready = turn(u, t.position, dt, 10);
    }
    // A buried Scorpion locks onto its victim for a second first, from the
    // moment its sting is ready: the victim's chance to step out of reach.
    if (u.kind == Unit::Kind::scorpion) {
        if (!(u.burrowed() && cooldownFor(u, t.air) <= 0)) { u.lockTarget = std::nullopt; return false; }
        ready = lockOn(u, id, dt) && ready;
    }
    if (!(ready && cooldownFor(u, t.air) <= 0)) return false;
    reload(u, w);
    if (u.kind == Unit::Kind::scorpion) u.lockTarget = std::nullopt;
    strike(u, id, t);
    return true;
}

/// One attack lands: its damage on the target, a Juggernaut's slow, a
/// Firefly's flame along its line, an anchored shell's splash.
/// One attack of `u` on `v`: its weapon against the target's traits
/// (`Rules.heroDamage` times that for the driven unit, and its
/// leveling picks: damage, extra against a class, armour ignored).
double Simulation::damage(const Unit& u, const Target& v) const {
    const bool igniter = u.kind == Unit::Kind::firefly && has(u.owner, Upgrade::novaIgniters);
    const UnitStats w = weapon(u, v.air);
    if (!pilot) {
        return Rules::damage(w, v.armored, v.light, v.armor) + (igniter && v.light ? 5 : 0);
    }
    const bool pierce = boost(Stat::IgnoresArmor{}, u).plus > 0;
    double base = Rules::damage(w, v.armored, v.light, pierce ? 0 : v.armor) + (igniter && v.light ? 5 : 0);
    const std::pair<TargetClass, bool> classes[] = {
        {TargetClass::armored, v.armored}, {TargetClass::light, v.light},
        {TargetClass::bio, v.kind ? Leveling::bio.contains(*v.kind) : false}, {TargetClass::building, v.structure}};
    Boost more = boost(Stat::Damage{}, u);
    for (const auto& [c, on] : classes) {
        if (!on) continue;
        const Boost b = boost(Stat::DamageVs{c}, u);
        base += static_cast<double>(w.hits) * b.plus;
        more.percent += b.percent;
    }
    // Flak mount and Sapper: half damage at a class the weapon does not
    // normally reach.
    double share = 1;
    if (u.kind == Unit::Kind::atlas && v.air) share = flakShare(u).value_or(1);
    if (u.kind == Unit::Kind::scorpion && v.structure) share = sapperShare(u).value_or(1);
    return more.apply(base) * share;
}

/// `u` attacks `id` (seen as `t`): it lands now, or once its round has
/// flown (`Rules.flight`).
void Simulation::strike(const Unit& u, int64_t id, const Target& t) {
    const bool anchored = u.kind == Unit::Kind::longbow && u.anchor.value_or(0) >= 1;
    // A rail slug is hitscan.
    const double delay = u.kind == Unit::Kind::kestrel && t.air ? 0 : Rules::flight(u.kind, anchored, distance(u.position, t.position));
    if (u.kind == Unit::Kind::scorpion) tally(u, [](Tally& k) { k.strikes += 1; });
    if (delay > 0) inFlight.push_back(InFlight{state.time + delay, u, id, t}); else land(u, id, t);
}

/// The shooter `u` was out of sight of the victim's side (player `victim`)
/// when it fired. A buried Scorpion gives itself away by the sting, so it is
/// judged as it stood before it (not yet reloading, nothing struck): hidden,
/// or on ground its victim's side does not see.
bool Simulation::shotUnseen(const Unit& u, int64_t victim) const {
    if (u.kind != Unit::Kind::scorpion) return !sees(victim, u.id);
    Unit before = u;
    before.cooldown = 0;
    before.struckAt = std::nullopt;
    return hiddenFrom(before, victim) || !sees(victim, u.position);
}

/// An attack of `u` lands: on `id` if it is still there, and, for a
/// Firefly's flame or an anchored shell, on everyone in its line or splash
/// (around `aim`, where the target stood, once it is gone).
void Simulation::land(const Unit& u, int64_t id, const Target& aim) {
    const bool anchored = u.kind == Unit::Kind::longbow && u.anchor.value_or(0) >= 1;
    const auto now = target(id);
    // Every third attack of the driven unit may count double (a pick).
    const bool twice = now && thirdTime(Action::attack, u);
    auto dmg = [&](const Target& v) { return damage(u, v) * (twice ? 2 : 1); };
    const Target t = now ? *now : aim;
    // Burn: what the driven Firefly's flame hits catches fire.
    std::optional<std::pair<double, double>> burn;
    if (u.kind == Unit::Kind::firefly) {
        burn = effect(u, [](const Effect& e) -> std::optional<std::pair<double, double>> {
            if (auto b = e.as<Effect::Burn>()) return std::pair<double, double>{b->perSecond, b->seconds};
            return std::nullopt;
        });
    }
    auto ignite = [&](int64_t v) {
        if (!burn) return;
        auto i = unitIndex(v);
        if (!i) return;
        state.units[size_t(*i)].burning = Burning{state.time + burn->second, burn->first, u.id};
        burnsLeft = true;
    };
    if (now) {
        hit(id, dmg(t), u.kind == Unit::Kind::juggernaut, &u);
        ignite(id);
        landings.push_back(Landing{u.id, id});
        // Shot from out of its side's sight (from high ground, say):
        // it goes for where the shot came from (see `stepSoldier`).
        if (!t.structure && shotUnseen(u, t.owner)) {
            if (auto i = unitIndex(id)) state.units[size_t(*i)].shotFrom = u.position;
        }
        // Fragment: the driven Juggernaut's grenade splashes the enemies
        // around its target.
        if (u.kind == Unit::Kind::juggernaut) {
            auto frag = effect(u, [](const Effect& e) -> std::optional<std::pair<double, double>> {
                if (auto f = e.as<Effect::Fragment>()) return std::pair<double, double>{f->radius, f->share};
                return std::nullopt;
            });
            if (frag) {
                const auto [r, share] = *frag;
                const std::vector<Unit> units = state.units;
                for (const auto& v : units) {
                    if (!(state.hostile(v.owner, u.owner) && v.id != id && v.hp > 0)) continue;
                    auto vt = target(v.id);
                    if (!vt || vt->air || !(distance(v.position, t.position) - vt->radius <= r)) continue;
                    hit(v.id, dmg(*vt) * share, false, &u);
                }
            }
        }
    }
    if (u.kind == Unit::Kind::firefly) {
        // Along the line the jet left in.
        const Vec2 dir = normalize(aim.position - u.position);
        const double reach = weapon(u).range + Rules::radius(Unit::Kind::firefly) + 0.5;
        const double width = boost(Stat::FlameWidth{}, u).apply(Rules::flameWidth);
        const std::vector<Unit> units = state.units;
        for (const auto& v : units) {
            if (!(state.hostile(v.owner, u.owner) && v.id != id && v.hp > 0)) continue;
            auto vt = target(v.id);
            if (!vt || vt->air) continue;
            const Vec2 rel = v.position - u.position;
            const double along = dot(rel, dir);
            const double off = length(rel - dir * along);
            if (along > 0 && along < reach + vt->radius && off < width + vt->radius) { hit(v.id, dmg(*vt), false, &u); ignite(v.id); }
        }
    }
    if (anchored || u.kind == Unit::Kind::hailstorm || u.kind == Unit::Kind::atlas) {
        splash(u, t.position, id, twice);
    }
    if (u.kind == Unit::Kind::scorpion) {
        // The sting: splash on enemies round the target, ground or air, and
        // with Twin sting a second target near the first.
        int64_t hits = now && !t.structure ? 1 : 0;
        hits += splash(u, t.position, id, twice);
        if (now) {
            const auto twin = effect(u, [](const Effect& e) -> std::optional<double> {
                if (auto s = e.as<Effect::SecondTarget>()) return s->radius;
                return std::nullopt;
            });
            if (twin) {
                std::optional<std::pair<int64_t, Target>> second;
                double best = 0;
                for (const auto& v : state.units) {
                    if (!(state.hostile(v.owner, u.owner) && v.id != id && v.hp > 0)) continue;
                    auto vt = foe(v.id, u.owner);
                    if (!vt) continue;
                    const double d = distance(v.position, t.position) - vt->radius;
                    if (d <= *twin && (!second || d < best)) { second = std::make_pair(v.id, *vt); best = d; }
                }
                if (second) { hit(second->first, dmg(second->second), false, &u); hits += 1; }
            }
        }
        tally(u, [&](Tally& k) { k.strikeHits += hits; });
    }
}

/// An anchored shell's or a flak burst's splash round `at`, past the
/// target `skip` it was fired at: an anchored shell hurts everyone on the
/// ground there (`Rules.splash`), friends too; a Hailstorm's flak only
/// enemy flyers (`Rules.flakSplash`). Splash picks widen both.
int64_t Simulation::splash(const Unit& u, Vec2 at, int64_t skip, bool twice) {
    const bool flak = u.kind == Unit::Kind::hailstorm;
    // An Atlas's shell and a Scorpion's sting also spare their own side;
    // the Atlas's reaches the ground, the sting both.
    const bool atlas = u.kind == Unit::Kind::atlas, sting = u.kind == Unit::Kind::scorpion;
    auto ringsOf = [](const auto& a) { return std::vector<Rules::Ring>(a.begin(), a.end()); };
    const std::vector<Rules::Ring> rings = flak ? ringsOf(Rules::flakSplash) : atlas ? ringsOf(Rules::atlasSplash)
        : sting ? ringsOf(Rules::scorpionSplash) : ringsOf(Rules::splash);
    const double wide = boost(Stat::Splash{}, u).apply(1.0);
    const std::vector<Unit> units = state.units;
    int64_t hits = 0;
    for (const auto& v : units) {
        if (!(v.id != skip && v.hp > 0 && (!(flak || atlas || sting) || state.hostile(v.owner, u.owner)))) continue;
        auto vt = target(v.id);
        if (!vt || (!sting && vt->air != flak)) continue;
        const double d = distance(v.position, at) - vt->radius;
        std::optional<double> f;
        for (const auto& ring : rings) if (d <= ring.radius * wide) { f = ring.factor; break; }
        if (!f) continue;
        hit(v.id, damage(u, *vt) * (twice ? 2 : 1) * *f, false, &u);
        hits += 1;
    }
    return hits;
}

/// What goes on by itself, whoever drives the unit (the AI or the
/// player): a Comet heals out of combat, a Dropship's energy comes back,
/// a tank anchors up or packs up the way `anchored` says.
void Simulation::upkeep(Unit& u, double dt) {
    switch (u.kind) {
    case Unit::Kind::comet:
        if (state.time - u.hitAt.value_or(-100) > Rules::cometRegenDelay) {
            u.hp = ac::min(maxHP(u), u.hp + Rules::cometRegen * rate(Stat::Regen{}, u) * dt);
        }
        break;
    case Unit::Kind::dropship: {
        const double regen = Rules::energyRegen * (has(u.owner, Upgrade::lifelineReactor) ? 2 : 1) * rate(Stat::Energy{}, u);
        u.energy = ac::min(Rules::maxEnergy, u.energy.value_or(0) + regen * dt);
        break;
    }
    case Unit::Kind::longbow: {
        // Hull down's rate is infinite: at once.
        const double pace = rate(Stat::Anchor{}, u);
        const double anchor = u.anchor.value_or(0), step_ = std::isfinite(pace) ? dt / Rules::anchorTime * pace : 1;
        u.anchor = u.anchored == true ? ac::min(1.0, anchor + step_) : ac::max(0.0, anchor - step_);
        break;
    }
    case Unit::Kind::scorpion: {
        const double step_ = dt / Rules::buryTime * rate(Stat::BuryTime{}, u), anchor = u.anchor.value_or(0);
        u.anchor = u.anchored == true ? ac::min(1.0, anchor + step_) : ac::max(0.0, anchor - step_);
        break;
    }
    default: break;
    }
}

/// A Longbow anchors up when there is something to shoot in its
/// anchor range (2–13) and no enemy fighter right on top of it, and at
/// ease at its rally. It packs up after `Rules.anchorIdle` s with nothing
/// to shoot anchored (to move on, or closer), when all it could shoot is
/// inside its minimum range, and when its army retreats. (`upkeep` moves
/// it the way `anchored` says.)
void Simulation::stepAnchor(Unit& u) {
    const Player& me = state.players[size_t(u.owner)];
    const auto [hittable, crowded] = anchorView(u);
    if (hittable) engaged[u.id] = state.time;
    const auto e = engaged.find(u.id);
    const bool idle = state.time - (e != engaged.end() ? e->second : -100) > Rules::anchorIdle;
    const bool regroup = u.mission && u.mission->is<Mission::Regroup>();
    const bool squad = regroup ? false : (u.mission && u.mission->objective().has_value());
    const bool resting = u.task == Unit::Task::idle && ((!u.mission && !me.retreating) || squad);
    if (me.retreating && !u.mission) u.anchored = false;
    else if (regroup) u.anchored = false;
    else if ((hittable && !crowded) || (resting && !crowded)) u.anchored = true;
    else if (idle || (crowded && !hittable)) u.anchored = false;
}

/// A Scorpion buries at ease and digs out to move, the way a Longbow
/// anchors: it stays down while it has something to sting (and while it
/// reloads, fighting), buries where it stands at ease (at its rally, or on
/// a raid or a hold), and digs out when its army pulls back or it has
/// somewhere to go. (`upkeep` moves it the way `anchored` says.)
void Simulation::stepBury(Unit& u) {
    const Player& me = state.players[size_t(u.owner)];
    const bool regroup = u.mission && u.mission->is<Mission::Regroup>();
    if ((me.retreating && !u.mission) || regroup) { u.anchored = false; return; }
    // (A follow order is not given to a Scorpion, but a saved game may hold one.)
    if (u.mission && u.mission->is<Mission::Follow>()) { u.anchored = false; return; }
    if (u.target && u.anchored == true) return;
    // Where it is to stand: its errand's point, else its place at the rally.
    std::optional<Vec2> post;
    if (u.mission) {
        const Mission& m = *u.mission;
        if (auto r = m.as<Mission::Raid>()) post = r->at;
        else if (auto h = m.as<Mission::Hunt>()) post = h->at;
        else if (auto a = m.as<Mission::Assault>()) post = spot(u, a->at);
        else if (auto o = m.as<Mission::Hold>()) post = spot(u, o->at);
        else if (auto f = m.as<Mission::FallBack>()) post = f->at;
    } else {
        post = slot(u);
    }
    const bool marching = !u.mission && me.attack && joins(u, *me.attack);
    const bool atPost = post && distance(u.position, *post) <= plantedSlack + 0.5;
    u.anchored = !marching && (u.task == Unit::Task::idle || atPost);
}

/// A follow order for `u` on unit `id` can be given: `id` is another living
/// unit of `u`'s side or an ally's, no chain of follow orders leads back to
/// `u`, and `u` is a soldier that can go and come (not a Dropship, a
/// Longbow or a Scorpion, which have their own ways).
bool Simulation::canFollow(const Unit& u, int64_t id) const {
    if (u.kind == Unit::Kind::dropship || u.kind == Unit::Kind::longbow || u.kind == Unit::Kind::scorpion) return false;
    int64_t at = id;
    for (int k = 0; k < 16; k++) {
        if (at == u.id) return false;
        const auto i = unitIndex(at);
        if (!i) return false;
        const Unit& v = state.units[size_t(*i)];
        if (!(v.hp > 0 && state.allied(v.owner, u.owner))) return false;
        const Mission::Follow* f = v.mission ? v.mission->as<Mission::Follow>() : nullptr;
        if (!f) return true;
        at = f->unit;
    }
    return false;
}

/// Where `u` holds formation beside the unit `leader` it follows: its
/// followers take places in id order, left and right of the leader's
/// heading in turn, each pair a little farther back.
Vec2 Simulation::formationSpot(const Unit& u, const Unit& leader) const {
    int64_t k = 0;
    for (const Unit& v : state.units) {
        const Mission::Follow* f = v.mission ? v.mission->as<Mission::Follow>() : nullptr;
        if (f && f->unit == leader.id && v.id < u.id && v.hp > 0) k += 1;
    }
    const Vec2 ahead(std::cos(leader.heading), std::sin(leader.heading));
    const Vec2 left(-ahead.y, ahead.x);
    const double side = k % 2 == 0 ? 1 : -1;
    const double out = leader.stats().radius + u.stats().radius + Rules::escortGap;
    return leader.position + left * (side * out) - ahead * (1.2 * static_cast<double>(k / 2));
}

/// The enemy flyer `u` would engage for the unit `leader` it follows: one it
/// sees and can hit, within `Rules.escortReach` of the leader; the armed
/// first, then the weakest, then the closest to `u`.
std::optional<int64_t> Simulation::escortFoe(const Unit& u, const Unit& leader) const {
    std::optional<int64_t> best;
    int64_t bestPriority = 0;
    double bestHp = 0, bestD = 0;
    for (const Unit& v : state.units) {
        if (!(state.hostile(v.owner, u.owner) && v.hp > 0)) continue;
        if (!(distance(leader.position, v.position) <= Rules::escortReach)) continue;
        const auto t = foe(v.id, u.owner);
        if (!(t && t->air && canHit(u, *t))) continue;
        const int64_t p = priority(*t, false);
        const double d = distance(u.position, v.position);
        if (best && !(p > bestPriority || (p == bestPriority && (v.hp < bestHp || (v.hp == bestHp && d < bestD))))) continue;
        best = v.id; bestPriority = p; bestHp = v.hp; bestD = d;
    }
    return best;
}

/// A unit on a follow order (`Mission::Follow`): it holds formation beside
/// the unit it follows, walking or flying on as that one moves (a walker
/// asks for a new way once its place has moved a cell), engages the enemy
/// flyers that come within `Rules.escortReach` of it, and returns to
/// formation when they are gone or out of that reach. False when the
/// unit it followed is gone: the order ends, and it goes back to its army.
bool Simulation::stepFollow(Unit& u, double dt, bool think, std::vector<GameEvent>& events) {
    using Task = Unit::Task;
    const int64_t id = u.mission->as<Mission::Follow>()->unit;
    const auto li = unitIndex(id);
    if (!(li && state.units[size_t(*li)].hp > 0 && state.allied(state.units[size_t(*li)].owner, u.owner))) {
        u.mission = std::nullopt; u.autoFollow = std::nullopt;
        u.target = std::nullopt; u.goal = std::nullopt; u.waypoints = std::nullopt;
        u.task = Task::toRally;
        return false;
    }
    const Unit leader = state.units[size_t(*li)];
    const UnitStats st = u.stats();
    // Keep the flyer while it stays in the leader's reach.
    std::optional<Target> t;
    if (u.target) {
        t = foe(*u.target, u.owner);
        if (!(t && state.hostile(t->owner, u.owner) && t->air && canHit(u, *t)
              && distance(leader.position, t->position) <= Rules::escortReach)) {
            t = std::nullopt;
            u.target = std::nullopt;
        }
    }
    if (!u.target && think) {
        u.target = escortFoe(u, leader);
        if (u.target) t = foe(*u.target, u.owner);
    }
    if (u.target && t) {
        const UnitStats w = weapon(u, true);
        const double d = gap(u, *t);
        const double reach = range(w.range, u.position, t->position);
        u.task = Task::attacking;
        if (d <= reach && d >= w.minRange) {
            u.goal = std::nullopt; u.waypoints = std::nullopt;
            if (fire(u, *u.target, *t, dt)) events.push_back(GameEvent::Shot{u.id, *u.target, t->owner, t->position});
        } else {
            (void)travel(u, t->position, reach + t->radius + st.radius - 0.4, dt);
        }
        return true;
    }
    const Vec2 spot_ = formationSpot(u, leader);
    if (distance(u.position, spot_) > (u.task == Task::toRally ? 0.3 : 0.9)) {
        u.task = Task::toRally;
        const Vec2 to = u.goal && distance(*u.goal, spot_) < 1 ? *u.goal : spot_;
        (void)travel(u, to, 0.3, dt);
    } else {
        u.task = Task::idle;
        u.goal = std::nullopt; u.waypoints = std::nullopt;
        (void)turn(u, u.position + Vec2(std::cos(leader.heading), std::sin(leader.heading)) * 5, dt, 3);
    }
    return true;
}

/// A Peregrine with no order of its own follows the closest friendly flyer
/// (an air unit that is not a Peregrine: a Kestrel, a Dropship, the one the
/// player drives) on a follow order of its own making (`autoFollow`), whoever
/// owns it. With no flyer it stays as it is. It keeps its leader until the
/// leader is gone or another flyer is less than half as far, and then picks
/// the closest again. Any order it is given (a mission, the AI's escort
/// assignment included) replaces it, and it resumes once the Peregrine has
/// none again.
void Simulation::autoEscort(Unit& u) {
    const Mission::Follow* now = u.mission ? u.mission->as<Mission::Follow>() : nullptr;
    if (u.mission && !(now && u.autoFollow == true)) return;
    if (u.task == Unit::Task::aboard || u.task == Unit::Task::inBastion || u.task == Unit::Task::toBastion) return;
    std::optional<int64_t> best;
    double bestD = 0, nowD = 0;
    bool keep = false;
    for (const Unit& v : state.units) {
        if (v.id == u.id || v.kind == Unit::Kind::peregrine || !v.stats().air) continue;
        if (!(v.hp > 0 && v.task != Unit::Task::aboard && canFollow(u, v.id))) continue;
        const double d = distance(u.position, v.position);
        if (now && now->unit == v.id) { keep = true; nowD = d; }
        if (!best || d < bestD) { best = v.id; bestD = d; }
    }
    if (!best || (keep && !(bestD < 0.5 * nowD))) return;
    if (now && now->unit == *best) return;
    u.mission = Mission::Follow{*best};
    u.autoFollow = true;
    u.goal = std::nullopt; u.waypoints = std::nullopt; u.target = std::nullopt;
}

/// A Scorpion has been locked onto `id` this long (a second lets it
/// sting). The lock starts when the sting is ready and the victim in
/// reach, and starts over when the victim changes or the Scorpion lost
/// its aim for a step.
bool Simulation::lockOn(Unit& u, int64_t id, double dt) const {
    const double now = state.time;
    const bool held = u.lockTarget == id && u.lockAt && now - *u.lockAt <= dt * 1.5 + 1e-9;
    if (!held) { u.lockTarget = id; u.lockFrom = now; }
    u.lockAt = now;
    return now - u.lockFrom.value_or(now) >= Rules::scorpionLock - 1e-9;
}

/// Flak mount: the share of its damage the driven Atlas does to flyers.
std::optional<double> Simulation::flakShare(const Unit& u) const {
    return effect(u, [](const Effect& e) -> std::optional<double> {
        if (auto h = e.as<Effect::HitsAir>()) return h->share;
        return std::nullopt;
    });
}

/// Sapper: the share of its damage the driven Scorpion does to buildings.
std::optional<double> Simulation::sapperShare(const Unit& u) const {
    return effect(u, [](const Effect& e) -> std::optional<double> {
        if (auto h = e.as<Effect::HitsBuildings>()) return h->share;
        return std::nullopt;
    });
}

/// The Quake stomp: every enemy on the ground within its reach takes its
/// damage and is slowed (a Juggernaut's slow), then the stomp waits out
/// `Rules.stompCooldown`. Aftershock makes it harder and wider. A
/// `GameEvent::Stomp` tells the scene and the sound where and how wide.
void Simulation::quake(Unit& u, std::vector<GameEvent>& events) {
    const Boost b = boost(Stat::Stomp{}, u);
    const double dmg = Rules::stompDamage * (1 + b.percent), reach = Rules::stompRadius + b.plus;
    int64_t hits = 0;
    const std::vector<Unit> units = state.units;
    for (const Unit& v : units) {
        if (!(state.hostile(v.owner, u.owner) && v.hp > 0)) continue;
        const auto t = target(v.id);
        if (!(t && !t->air && distance(t->position, u.position) - t->radius <= reach)) continue;
        hit(v.id, dmg, true, &u);
        hits += 1;
    }
    u.stompReady = state.time + Rules::stompCooldown;
    u.stompedAt = state.time;
    tally(u, [&](Tally& t) { t.stomps += 1; t.stompHits += hits; });
    events.push_back(GameEvent::Stomp{u.id, u.position, reach});
}

/// An Atlas not driven stomps by itself once its stomp is ready and
/// `Rules.stompCrowd` or more enemies it sees stand on the ground within
/// reach.
void Simulation::stompCrowd(Unit& u, std::vector<GameEvent>& events) {
    if (!(state.time >= u.stompReady.value_or(0))) return;
    const double reach = Rules::stompRadius + boost(Stat::Stomp{}, u).plus;
    int64_t n = 0;
    for (const Unit& v : state.units) {
        if (!(state.hostile(v.owner, u.owner) && v.hp > 0)) continue;
        const auto t = foe(v.id, u.owner);
        if (t && !t->air && distance(t->position, u.position) - t->radius <= reach) n += 1;
    }
    if (n >= Rules::stompCrowd) quake(u, events);
}

/// What a tank at `u` sees: something an anchored Longbow could shoot there
/// (on the ground, 2–13 away edge to edge; buildings only while its army
/// attacks, or a manned Bastion), and enemy fighters right on top of it
/// (inside the minimum range), where only tank mode can shoot.
Simulation::AnchorView Simulation::anchorView(const Unit& u) const {
    const UnitStats& w = UnitStats::anchor;
    bool hittable = false, crowded = false;
    for (const auto& v : state.units) {
        if (!(state.hostile(v.owner, u.owner) && v.hp > 0 && !v.stats().air && v.task != Unit::Task::inBastion
              && v.task != Unit::Task::aboard && v.task != Unit::Task::inDerrick)) continue;
        if (!(std::abs(v.position.x - u.position.x) < w.range + 3 && std::abs(v.position.y - u.position.y) < w.range + 3)) continue;
        auto t = foe(v.id, u.owner);
        if (!t) continue;
        const double d = gap(u, *t);
        if (d <= range(w.range, u.position, t->position) && d >= w.minRange) hittable = true;
        if (d < w.minRange + 0.5 && v.stats().hitsGround) crowded = true;
    }
    if (!hittable) {
        const bool attacking = state.players[size_t(u.owner)].attack.has_value() || (u.mission && u.mission->is<Mission::Assault>());
        hittable = any(state.structures, [&](const Structure& st) {
            if (!(state.hostile(st.owner, u.owner) && (attacking || (st.crew && !st.crew->empty())))) return false;
            auto t = foe(st.id, u.owner);
            if (!t) return false;
            const double d = gap(u, *t);
            return d <= range(w.range, u.position, t->position) && d >= w.minRange;
        });
    }
    return AnchorView{hittable, crowded};
}

/// A Dropship heals the most hurt living thing of its side nearby
/// (infantry only), flies with its army, and carries out drops.
void Simulation::stepDropship(Unit& u, double dt, bool think) {
    Defer carried([&] { carry(u); });
    const Player me = state.players[size_t(u.owner)];
    if (u.mission) {
        if (auto drop = u.mission->as<Mission::Drop>()) {
            const Vec2 p = drop->at;
            // Fly the load in; once over the point, set them down and stay
            // over them to heal.
            u.task = Unit::Task::toRally;
            if (move(u, p, 0.3, dt)) {
                unload(u);
                u.mission = Mission::Raid{p};
            }
            return;
        }
    }
    if (think) {
        // The patient: hurt, of its side, bio, within reach.
        const auto hurt = filtered(state.units, [&](const Unit& v) {
            return v.owner == u.owner && v.stats().bio && v.hp < maxHP(v) && v.hp > 0
                && v.task != Unit::Task::inBastion && v.task != Unit::Task::aboard && v.task != Unit::Task::inDerrick
                && distance(v.position, u.position) < Rules::healRange + 4;
        });
        if (u.energy.value_or(0) >= 1) {
            auto m = minBy(hurt, [&](const Unit& a, const Unit& b) { return a.hp / maxHP(a) < b.hp / maxHP(b); });
            u.target = m ? std::optional<int64_t>(m->id) : std::nullopt;
        } else {
            u.target = std::nullopt;
        }
    }
    if (u.target) {
        const int64_t id = *u.target;
        if (auto i = unitIndex(id); i && state.units[size_t(*i)].hp > 0 && u.energy.value_or(0) > 0) {
            const Unit v = state.units[size_t(*i)];
            if (move(u, v.position, Rules::healRange - 1, dt) || distance(u.position, v.position) < Rules::healRange) {
                heal(u, id, dt);
            }
            return;
        }
    }
    u.target = std::nullopt;
    std::optional<Vec2> point_;
    if (u.mission && (u.mission->is<Mission::Raid>() || u.mission->is<Mission::Assault>() || u.mission->is<Mission::Hold>()
                      || u.mission->is<Mission::Regroup>())) {
        point_ = point(*u.mission);
    } else {
        std::optional<Vec2> spot_;
        if (me.attack && joins(u, *me.attack)) spot_ = attackSpot(u, *me.attack);
        point_ = spot_ ? spot_ : slot(u);
    }
    if (!point_) return;
    const Vec2 to = *point_;
    // Hover over the army, a little behind its front.
    u.task = move(u, to - rallyFacing[size_t(u.owner)] * 1.5, 0.4, dt) ? Unit::Task::idle : Unit::Task::toRally;
}

/// Units aboard a Dropship ride with it.
void Simulation::carry(const Unit& m) {
    if (!m.cargo) return;
    for (const int64_t id : *m.cargo) {
        if (auto i = unitIndex(id)) state.units[size_t(*i)].position = m.position;
    }
}

/// Set everyone aboard down around the Dropship; they raid where it is.
/// The driven Dropship's Combat drop makes them rush.
void Simulation::unload(Unit& m) {
    const auto rush = effect(m, [](const Effect& e) -> std::optional<Rush> {
        if (auto r = e.as<Effect::DropRush>()) return r->rush;
        return std::nullopt;
    });
    const std::vector<int64_t> cargo = m.cargo.value_or(std::vector<int64_t>{});
    for (size_t k = 0; k < cargo.size(); k++) {
        const int64_t id = cargo[k];
        auto i = unitIndex(id);
        if (!i) continue;
        auto& v = state.units[size_t(*i)];
        const double a = static_cast<double>(k) * 2.39996;
        const Vec2 p = m.position + Vec2(std::cos(a), std::sin(a)) * 0.6 * std::sqrt(static_cast<double>(k) + 0.5);
        // A long carry by the driven Dropship earns it XP.
        if (v.boardedAt && distance(*v.boardedAt, m.position) >= Leveling::carryDistance) {
            const double xp = Leveling::worth(v.kind) * Leveling::carryXP;
            earn(xp, m);
            tally(m, [](Tally& t) { t.carried += 1; });
        }
        auto& w = state.units[size_t(*i)];
        w.boardedAt = std::nullopt;
        w.position = nav ? nav->nearestFree(p).value_or(p) : p;
        w.task = Unit::Task::toRally;
        w.structure = std::nullopt;
        w.goal = std::nullopt;
        w.mission = Mission::Raid{m.position};
        if (rush) {
            w.rush = *rush;
            w.rushUntil = state.time + rush->seconds;
        }
    }
    m.cargo = std::vector<int64_t>{};
}

/// A unit within 6 cells boards a Dropship with room for it; with no
/// unit, the Dropship sets everyone down where it is.
bool Simulation::board(int64_t mid, std::optional<int64_t> id) {
    auto mi = unitIndex(mid);
    if (!mi || state.units[size_t(*mi)].kind != Unit::Kind::dropship) return false;
    if (!id) {
        Unit m = state.units[size_t(*mi)];
        unload(m);
        state.units[size_t(*mi)] = m;
        return true;
    }
    auto i = unitIndex(*id);
    if (!i) return false;
    const Unit& v = state.units[size_t(*i)];
    const Unit& m = state.units[size_t(*mi)];
    if (!(v.owner == m.owner && Rules::boards(v.kind) && v.task != Unit::Task::inBastion && v.task != Unit::Task::aboard
          && distance(v.position, m.position) < 6)) return false;
    if (!(slotsUsed(m) + Rules::slots(v.kind) <= cargoSlots(m))) return false;
    auto cargo = state.units[size_t(*mi)].cargo.value_or(std::vector<int64_t>{});
    cargo.push_back(*id);
    state.units[size_t(*mi)].cargo = cargo;
    auto& w = state.units[size_t(*i)];
    w.boardedAt = w.position;
    // A buried Scorpion is lifted aboard.
    if (w.kind == Unit::Kind::scorpion) { w.anchor = 0; w.anchored = false; w.lockTarget = std::nullopt; }
    w.task = Unit::Task::aboard;
    w.structure = mid;
    w.target = std::nullopt;
    w.goal = std::nullopt;
    w.mission = std::nullopt;
    return true;
}

/// Whether a soldier goes to its army's attack point. Soldiers already
/// out go on; the rest wait at the rally and cross the map together, a
/// group of `reinforceGroup` at a time, so they never walk in one by one
/// to die. A fight close to the rally they join straight away.
bool Simulation::joins(const Unit& u, Vec2 a) const {
    if (u.task == Unit::Task::attackMove || u.task == Unit::Task::attacking) return true;
    const int64_t group = state.players[size_t(u.owner)].reinforce.value_or(reinforceGroup);
    if (u.owner >= 0 && u.owner < static_cast<int64_t>(atHome.size()) && atHome[size_t(u.owner)] >= group) return true;
    return distance(u.position, a) < 12;
}

/// A Ranger in a Bastion shoots from it (range measured from the Bastion's
/// edge, one longer) and never moves; one on its way walks in, or goes
/// back to the rally when the Bastion is full or gone.
void Simulation::stepInBastion(Unit& u, double dt, bool think, std::vector<GameEvent>& events) {
    std::optional<size_t> bi;
    if (u.structure) {
        for (size_t k = 0; k < state.structures.size(); k++) if (state.structures[k].id == *u.structure) { bi = k; break; }
    }
    if (!bi) {
        u.task = Unit::Task::toRally; u.structure = std::nullopt; u.goal = std::nullopt; return;
    }
    const Structure b = state.structures[*bi];
    if (u.task == Unit::Task::toBastion) {
        if (!(static_cast<int64_t>(b.crew ? b.crew->size() : 0) < bastionCapacity(b.owner))) {
            u.task = Unit::Task::toRally; u.structure = std::nullopt; return;
        }
        if (travel(u, b.position, Rules::radius(b.kind) + 0.45, dt)) {
            auto crew = b.crew.value_or(std::vector<int64_t>{});
            crew.push_back(u.id);
            state.structures[*bi].crew = crew;
            u.task = Unit::Task::inBastion;
            u.position = b.position;
            u.goal = std::nullopt; u.waypoints = std::nullopt;
        }
        return;
    }
    u.position = b.position;
    const Unit port(u.id, Unit::Kind::ranger, u.owner, b.position, 0, Unit::Task::idle);
    auto inReach = [&](const Target& t) {
        return gap(port, t) <= range(b.position, t.position) + Rules::bastionRangeBonus + Rules::radius(b.kind) - Rules::unitRadius;
    };
    bool keep = false;
    if (u.target) {
        if (auto t = foe(*u.target, u.owner); t && inReach(*t)) keep = true;
    }
    if (!keep) u.target = std::nullopt;
    if (!u.target && think) {
        if (auto best = acquire(port)) {
            if (auto t = target(*best); t && inReach(*t)) u.target = best;
        }
    }
    if (!u.target) return;
    const int64_t id = *u.target;
    auto t = foe(id, u.owner);
    if (!t) return;
    u.heading = std::atan2(t->position.y - b.position.y, t->position.x - b.position.x);
    if (u.cooldown.value_or(0) <= 0) {
        reload(u, weapon(port));
        strike(port, id, *t);
        events.push_back(GameEvent::Shot{u.id, id, t->owner, t->position});
    }
}

/// A shot lands: damage the unit or building with this id (a Juggernaut's
/// grenade also slows a unit). While someone drives: picks that cut the
/// damage taken, the driven unit's shield, XP for the driven `shooter`
/// (the enemy's worth times the share of its hp taken), a kill's
/// Bounty, and a slow its picks make longer or deeper. The driven
/// unit's tally counts the hp it takes off, its kills, and what it
/// takes.
void Simulation::hit(int64_t id, double damage_, bool slow, const Unit* shooter) {
    if (auto it = unitIndexByID.find(id); it != unitIndexByID.end() && it->second >= 0
        && it->second < static_cast<int64_t>(state.units.size()) && state.units[size_t(it->second)].id == id) {
        const size_t i = size_t(it->second);
        double amount = damage_;
        if (pilot) {
            const Unit v = state.units[i];
            const double aimed = amount * rate(Stat::DamageTaken{}, v);
            amount = shieldTakes(static_cast<int64_t>(i), aimed);
            const double lost = ac::min(amount, ac::max(0.0, v.hp)), shielded = aimed - amount;
            const bool kill = v.hp > 0 && amount >= v.hp;
            tally(v, [&](Tally& t) { t.damageTaken += lost; t.shieldAbsorbed += shielded; });
            if (shooter && state.hostile(shooter->owner, v.owner)) {
                const Unit s = *shooter;
                earnHit(s, Leveling::worth(v.kind), ac::min(amount, ac::max(0.0, v.hp)), maxHP(v), false);
                tally(s, [&](Tally& t) { t.unitDamage += lost; if (kill) { t.kills += 1; if (v.stats().air) t.airKills += 1; } });
                if (v.hp > 0 && amount >= v.hp) bounty(s, Rules::cost(v.kind), Rules::hydrogenCost(v.kind));
            }
        }
        state.units[i].hp -= amount;
        state.units[i].hitAt = state.time;
        if (slow) {
            const Boost longer = shooter ? boost(Stat::SlowTime{}, *shooter) : Boost::none;
            const Boost deeper = shooter ? boost(Stat::Slow{}, *shooter) : Boost::none;
            state.units[i].slowUntil = state.time + longer.apply(Rules::slowTime);
            state.units[i].slowedTo = deeper == Boost::none ? std::nullopt : std::optional<double>(1 - deeper.apply(0.5));
        }
        return;
    }
    for (size_t i = 0; i < state.structures.size(); i++) {
        if (state.structures[i].id != id) continue;
        const Structure st = state.structures[i];
        if (pilot && shooter && state.hostile(shooter->owner, st.owner)) {
            const Unit s = *shooter;
            earnHit(s, Leveling::worth(st.kind), ac::min(damage_, ac::max(0.0, st.hp)), Rules::hp(st.kind), true);
            const double taken = ac::min(damage_, ac::max(0.0, st.hp));
            const bool razed = st.hp > 0 && damage_ >= st.hp;
            tally(s, [&](Tally& t) { t.buildingDamage += taken; if (razed) t.razed += 1; });
            if (st.hp > 0 && damage_ >= st.hp) bounty(s, Rules::cost(st.kind), Rules::hydrogenCost(st.kind));
        }
        state.structures[i].hp -= damage_;
        return;
    }
}

/// Ground soldiers never stand inside each other: overlapping pairs step
/// apart onto walkable ground (Prospectors mine through one another;
/// an anchored Longbow stands firm and the others step around it).
void Simulation::separate() {
    std::vector<size_t> army;
    for (size_t k = 0; k < state.units.size(); k++) {
        const Unit& u = state.units[k];
        if (u.soldier() && !u.stats().air && u.task != Unit::Task::inBastion && u.task != Unit::Task::aboard && !u.jumpFrom) {
            army.push_back(k);
        }
    }
    if (!(army.size() > 1)) return;
    // C++ only, the same pushes in the same order: Swift tests every pair;
    // here each unit tests only those in a box about it, from a grid of
    // buckets kept up to date as units are pushed, in the same order (by
    // index in `army`). A unit outside the box is out of reach (the box is
    // wider than any pair's reach), so its test would have failed.
    const size_t count = army.size();
    std::vector<double> radius(count);
    double reachMax = 0;
    for (size_t n = 0; n < count; n++) {
        radius[n] = Rules::radius(state.units[army[n]].kind);
        reachMax = ac::max(reachMax, radius[n]);
    }
    reachMax = 2 * reachMax * 0.85;
    // How far a unit may be pushed in its own turn before its box is gathered again.
    constexpr double slack = 0.5;
    const double half = reachMax + slack + 1e-6;
    const double size = ac::max(half, 1.0);
    double minX = state.units[army[0]].position.x, minY = state.units[army[0]].position.y, maxX = minX, maxY = minY;
    for (size_t k : army) {
        const Vec2 p = state.units[k].position;
        minX = ac::min(minX, p.x); maxX = ac::max(maxX, p.x);
        minY = ac::min(minY, p.y); maxY = ac::max(maxY, p.y);
    }
    const int64_t cols = ac::min<int64_t>(int64_t((maxX - minX) / size) + 1, 4096),
                  rows = ac::min<int64_t>(int64_t((maxY - minY) / size) + 1, 4096);
    auto bucketOf = [&](Vec2 p) {
        const int64_t x = ac::min<int64_t>(ac::max<int64_t>(int64_t(std::floor((p.x - minX) / size)), 0), cols - 1);
        const int64_t y = ac::min<int64_t>(ac::max<int64_t>(int64_t(std::floor((p.y - minY) / size)), 0), rows - 1);
        return y * cols + x;
    };
    std::vector<std::vector<uint32_t>> buckets(size_t(cols * rows));
    std::vector<int64_t> bucket(count);
    for (size_t n = 0; n < count; n++) {
        bucket[n] = bucketOf(state.units[army[n]].position);
        buckets[size_t(bucket[n])].push_back(uint32_t(n));
    }
    auto moved = [&](size_t n) {
        const int64_t b = bucketOf(state.units[army[n]].position);
        if (b == bucket[n]) return;
        auto& from = buckets[size_t(bucket[n])];
        from.erase(std::find(from.begin(), from.end(), uint32_t(n)));
        buckets[size_t(b)].push_back(uint32_t(n));
        bucket[n] = b;
    };
    std::vector<uint32_t> near;
    // Those after `after` in a box about `at`, in order.
    auto gather = [&](Vec2 at, size_t after) {
        near.clear();
        const int64_t b0 = bucketOf(Vec2(at.x - half, at.y - half)), b1 = bucketOf(Vec2(at.x + half, at.y + half));
        for (int64_t y = b0 / cols; y <= b1 / cols; y++) {
            for (int64_t x = b0 % cols; x <= b1 % cols; x++) {
                for (uint32_t m : buckets[size_t(y * cols + x)]) {
                    if (m <= after) continue;
                    const Vec2 q = state.units[army[m]].position;
                    if (std::abs(q.x - at.x) <= half && std::abs(q.y - at.y) <= half) near.push_back(m);
                }
            }
        }
        std::sort(near.begin(), near.end());
    };
    for (size_t n = 0; n < count; n++) {
        const size_t i = army[n];
        Vec2 anchor = state.units[i].position;
        gather(anchor, n);
        for (size_t c = 0; c < near.size(); c++) {
            const size_t m = near[c];
            const size_t j = army[m];
            const double r = (radius[n] + radius[m]) * 0.85;
            const Vec2 d = state.units[j].position - state.units[i].position;
            if (!(std::abs(d.x) < r && std::abs(d.y) < r)) continue;
            const double dist = length(d);
            if (!(dist < r)) continue;
            const Vec2 dir = dist > 1e-6 ? d / dist : Vec2(std::cos(static_cast<double>(i)), std::sin(static_cast<double>(i)));
            const bool fixedI = state.units[i].anchor.value_or(0) > 0, fixedJ = state.units[j].anchor.value_or(0) > 0;
            if (fixedI && fixedJ) continue;
            // Over a buried Scorpion (C++ only, `buriedPassable`).
            if (buriedPassable && (state.units[i].burrowed() || state.units[j].burrowed())) continue;
            const double share = fixedI || fixedJ ? 0.5 : 0.25;
            const Vec2 push = dir * ((r - dist) * share);
            const std::tuple<size_t, double, bool> sides[] = {{i, -1.0, fixedI}, {j, 1.0, fixedJ}};
            for (const auto& [k, sign, fixed] : sides) {
                if (fixed) continue;
                const Vec2 p = state.units[k].position + push * sign;
                if ((nav ? nav->walkable(p) && !nav->inRock(p) : true) && !NavGrid::solid(p, state)) {
                    state.units[k].position = p;
                }
            }
            moved(m);
            const Vec2 now = state.units[i].position;
            if (std::abs(now.x - anchor.x) > slack || std::abs(now.y - anchor.y) > slack) {
                // Pushed far in its own turn: gather again, after `m`.
                anchor = now;
                gather(anchor, m);
                c = size_t(-1);
            }
        }
    }
}

/// The watchdog for units that stop on their way: a ground unit with
/// somewhere to go (a goal or waypoints left) that stays near one spot
/// for `stuckTime` seconds (wedged on a corner, or held by the crowd)
/// steps to the centre of its cell, or the nearest free one, and
/// looks for a new way from there. Anchored tanks, jumping Comets and
/// the driven unit are left alone.
void Simulation::unstick(std::vector<GameEvent>& events) {
    if (!nav) return;
    std::set<int64_t> seen;
    for (size_t i = 0; i < state.units.size(); i++) {
        const Unit u = state.units[i];
        if (!(!u.stats().air && !u.jumpFrom && u.anchor.value_or(0) == 0 && !(pilot && u.id == pilot->unit)
              && u.task != Unit::Task::inBastion && u.task != Unit::Task::aboard && u.task != Unit::Task::inDerrick
              && (u.goal || (u.waypoints && !u.waypoints->empty())))) continue;
        seen.insert(u.id);
        auto h = headway.find(u.id);
        if (h == headway.end() || !(distance(h->second.at, u.position) < stuckRadius)) {
            headway[u.id] = Headway{u.position, state.time};
            continue;
        }
        if (!(state.time - h->second.time >= stuckTime)) continue;
        auto to = nav->freeCentre(u.position);
        if (!to) to = nav->nearestFree(u.position, 3);
        if (to && !nav->inRock(*to)) state.units[i].position = *to;
        state.units[i].goal = std::nullopt;
        state.units[i].waypoints = std::nullopt;
        headway[u.id] = Headway{state.units[i].position, state.time};
        if (freed.find(u.id) == freed.end()) {
            events.push_back(GameEvent::Stuck{u.id, u.kind, u.owner, u.position});
        }
        freed[u.id] = state.units[i].position;
    }
    std::erase_if(headway, [&](const auto& kv) { return !seen.count(kv.first); });
    if (!freed.empty()) {
        std::erase_if(freed, [&](const auto& kv) {
            for (const auto& v : state.units) {
                if (v.id == kv.first) return !(distance(v.position, kv.second) < 2);
            }
            return true;
        });
    }
}

/// Once a second: a ground unit boxed in for `trappedTime` (a tank in a
/// gap between buildings, a Prospector walled in by the building it put up)
/// can never get out, so it is destroyed. Boxed in is `NavGrid.boxedIn`:
/// the ground it walks in is cut off from the open map and smaller than
/// `pocketArea`; a Comet next to a cliff face can jump out. Units in a
/// Bastion, a Dropship or a Derrick, jumping, and the unit a human
/// drives are left alone, and so are Prospectors at work against an ore
/// field or a building (where the nearest open cell may be a gap
/// between patches); they are checked once they walk off.
void Simulation::trapped(std::vector<GameEvent>& events) {
    using Task = Unit::Task;
    if (!nav || !(state.time >= nextTrapCheck)) return;
    nextTrapCheck = state.time + 1;
    std::set<int64_t> seen;
    for (size_t i = 0; i < state.units.size(); i++) {
        const Unit u = state.units[i];
        if (!(u.hp > 0 && !u.stats().air && !u.jumpFrom && !(pilot && u.id == pilot->unit) && u.task != Task::inBastion)) continue;
        if (u.task == Task::aboard || u.task == Task::inDerrick || u.task == Task::building || u.task == Task::repairing
            || u.task == Task::mining || u.task == Task::waiting || u.task == Task::depositing) continue;
        if (!nav->boxedIn(u.position, pocketArea, u.kind == Unit::Kind::comet)) continue;
        seen.insert(u.id);
        auto b = boxedSince.find(u.id);
        const double since = b != boxedSince.end() ? b->second : state.time;
        boxedSince[u.id] = since;
        if (!(state.time - since >= trappedTime)) continue;
        state.units[i].hp = 0;
        events.push_back(GameEvent::Trapped{u.id, u.kind, u.owner, u.position});
    }
    std::erase_if(boxedSince, [&](const auto& kv) { return !seen.count(kv.first); });
}

/// Remove the dead and the destroyed, and end the game when one player
/// is left with buildings.
void Simulation::bury(std::vector<GameEvent>& events) {
    if (any(state.units, [](const Unit& u) { return u.hp <= 0; })) {
        // Everyone aboard a downed Dropship goes down with it.
        {
            const std::vector<Unit> units = state.units;
            for (const auto& m : units) {
                if (!(m.hp <= 0 && m.kind == Unit::Kind::dropship) || !m.cargo) continue;
                for (const int64_t id : *m.cargo) {
                    if (auto i = unitIndex(id)) state.units[size_t(*i)].hp = 0;
                }
            }
        }
        const std::vector<Unit> units = state.units;
        for (const auto& u : units) {
            if (!(u.hp <= 0)) continue;
            if (u.structure && u.task == Unit::Task::inDerrick && state.wells) {
                const auto r = state.structure(*u.structure);
                std::optional<size_t> gi;
                for (size_t g = 0; g < state.wells->size(); g++) {
                    if (r && distance((*state.wells)[g].position, r->position) < 0.5) { gi = g; break; }
                }
                if (gi && (*state.wells)[*gi].harvester == u.id) (*state.wells)[*gi].harvester = std::nullopt;
            }
            if (u.patch && state.patches[size_t(*u.patch)].miner == u.id) state.patches[size_t(*u.patch)].miner = std::nullopt;
            if (pilot) refund(u);
            tally(u, [](Tally& t) { t.deaths += 1; });
            if (u.owner >= 0 && u.owner < static_cast<int64_t>(state.players.size())) {
                state.players[size_t(u.owner)].unitsLost = state.players[size_t(u.owner)].unitsLost.value_or(0) + 1;
            }
            events.push_back(GameEvent::Died{u.id, u.kind, u.owner, u.position});
        }
        std::erase_if(state.units, [](const Unit& u) { return u.hp <= 0; });
    }
    if (!any(state.structures, [](const Structure& s) { return s.hp <= 0; })) return;
    const std::vector<Structure> structures = state.structures;
    for (const auto& s : structures) {
        if (!(s.hp <= 0)) continue;
        // Prospectors inside a fallen Derrick come out; a fallen Lab
        // leaves its building bare.
        for (auto& v : state.units) {
            if (!(v.structure == s.id && v.task != Unit::Task::inBastion)) continue;
            if (v.task == Unit::Task::inDerrick) {
                const Vec2 door = s.position + Vec2(0, Rules::radius(s.kind) + 0.6);
                v.position = nav ? nav->nearestFree(door).value_or(s.position) : s.position;
            }
            if (v.kind == Unit::Kind::prospector) { v.task = Unit::Task::idle; v.structure = std::nullopt; }
        }
        if (state.wells) {
            for (auto& g : *state.wells) {
                if (distance(g.position, s.position) < 0.5) { g.harvester = std::nullopt; break; }
            }
        }
        for (auto& o : state.structures) if (o.addon == s.id) o.addon = std::nullopt;
        // Rangers in a fallen Bastion climb out around it.
        const std::vector<int64_t> crew = s.crew.value_or(std::vector<int64_t>{});
        for (size_t k = 0; k < crew.size(); k++) {
            std::optional<size_t> i;
            for (size_t j = 0; j < state.units.size(); j++) if (state.units[j].id == crew[k]) { i = j; break; }
            if (!i) continue;
            const double a = static_cast<double>(k) * 1.6;
            const Vec2 p = s.position + Vec2(std::cos(a), std::sin(a)) * (Rules::radius(s.kind) + 0.6);
            state.units[*i].position = nav ? nav->nearestFree(p).value_or(p) : p;
            state.units[*i].task = Unit::Task::toRally;
            state.units[*i].structure = std::nullopt;
        }
        events.push_back(GameEvent::Destroyed{s.id, s.kind, s.owner, s.position});
    }
    std::erase_if(state.structures, [](const Structure& s) { return s.hp <= 0; });
    // No one wins in the playground: its sides come and go by hand.
    if (state.winner || state.endedAt || !(state.players.size() > 1) || (map && map->playground == true)) return;
    // The teams with buildings left (C++: with no teams set, each player is
    // its own team and this is Swift's set of owners). Over when one is left.
    std::set<int64_t> standing;
    for (const auto& s : state.structures) standing.insert(state.team(s.owner));
    if (!(standing.size() <= 1)) return;
    state.endedAt = state.time;
    if (!standing.empty()) {
        // Swift's `Set.first`: the one element. The winner is the team's
        // first player still standing; every player of the team scores.
        std::optional<int64_t> w;
        for (const auto& s : state.structures) if (!w || s.owner < *w) w = s.owner;
        state.winner = *w;
        for (int64_t p = 0; p < static_cast<int64_t>(state.score.size()); p++) {
            if (state.allied(p, *w)) state.score[size_t(p)] += 1;
        }
        events.push_back(GameEvent::Victory{*w});
    }
}

/// Dry patches grow back `Rules.regrowDelay` after running out.
void Simulation::regrow(std::vector<GameEvent>& events) {
    for (size_t i = 0; i < state.patches.size(); i++) {
        auto& p = state.patches[i];
        if (!(p.remaining <= 0)) continue;
        if (!p.depletedAt) { p.depletedAt = state.time; continue; }
        if (state.time - *p.depletedAt >= Rules::regrowDelay) {
            p.remaining = p.initial;
            p.depletedAt = std::nullopt;
            p.miner = std::nullopt;
            events.push_back(GameEvent::PatchRegrown{static_cast<int64_t>(i)});
        }
    }
}

/// Clear a patch's miner (or a well's harvester) if that Prospector is no
/// longer drilling it (inside it).
void Simulation::releaseStaleMiners() {
    std::map<int64_t, int64_t> drilling;
    for (const auto& u : state.units) {
        if (u.task == Unit::Task::mining && u.patch) drilling[*u.patch] = u.id;
    }
    for (size_t i = 0; i < state.patches.size(); i++) {
        if (!state.patches[i].miner) continue;
        const int64_t m = *state.patches[i].miner;
        auto it = drilling.find(static_cast<int64_t>(i));
        if (it == drilling.end() || it->second != m) state.patches[i].miner = std::nullopt;
    }
    if (!state.wells) return;
    for (auto& g : *state.wells) {
        if (!g.harvester) continue;
        auto i = unitIndex(*g.harvester);
        if (!i || state.units[size_t(*i)].task != Unit::Task::inDerrick) g.harvester = std::nullopt;
    }
}

void Simulation::leavePatch(Unit& u) {
    if (u.patch && state.patches[size_t(*u.patch)].miner == u.id) state.patches[size_t(*u.patch)].miner = std::nullopt;
    if (u.task == Unit::Task::mining || u.task == Unit::Task::waiting) u.task = Unit::Task::idle;
}

// MARK: - Bases and patches

/// A player's completed Citadels.
std::vector<Structure> Simulation::bases(int64_t player) const {
    return filtered(state.structures, [&](const Structure& s) {
        return s.kind == Structure::Kind::citadel && s.complete() && s.owner == player;
    });
}

std::optional<Structure> Simulation::base(Vec2 p, int64_t player) const {
    return minBy(bases(player), [&](const Structure& a, const Structure& b) { return walk(p, a.position) < walk(p, b.position); });
}

double Simulation::walk(Vec2 a, Vec2 b) const {
    if (router) return router->distance(a, b);
    return distance(a, b);
}

/// Site index a Citadel stands on.
int64_t Simulation::site(const Structure& citadel) const {
    std::optional<int64_t> best;
    for (int64_t i = 0; i < static_cast<int64_t>(sites.size()); i++) {
        if (!best || distance(sites[size_t(i)], citadel.position) < distance(sites[size_t(*best)], citadel.position)) best = i;
    }
    return best.value_or(0);
}

/// Workers assigned to each patch (walking to, drilling, waiting at or
/// carrying back from it).
std::map<int64_t, int64_t> Simulation::assigned() const {
    using Task = Unit::Task;
    std::map<int64_t, int64_t> n;
    for (const auto& u : state.units) {
        if (!u.patch || !(u.task == Task::toPatch || u.task == Task::mining || u.task == Task::waiting
                          || u.task == Task::toBase || u.task == Task::depositing)) continue;
        n[*u.patch] += 1;
    }
    return n;
}

/// The patch a Prospector should mine: spread over the patches of a base with
/// a Citadel, closest first; a patch counts as full at
/// `workersPerPatch`, and only then do workers go to another base.
std::optional<int64_t> Simulation::choosePatch(const Unit& unit) const {
    const auto mine = bases(unit.owner);
    std::set<int64_t> owned;
    for (const auto& b : mine) owned.insert(site(b));
    // Not at a base with enemy soldiers on it, while another is safe.
    std::set<int64_t> unsafe;
    for (const auto& b : mine) {
        if (any(state.units, [&](const Unit& v) { return state.hostile(v.owner, unit.owner) && v.soldier() && distance(v.position, b.position) < 13; })) {
            unsafe.insert(site(b));
        }
    }
    if (!unsafe.empty() && owned.size() > unsafe.size()) {
        for (auto s : unsafe) owned.erase(s);
    }
    const auto n = assigned();
    // Only patches it can walk to (C++ only, `reaches`).
    auto walkable = [&](int64_t i) { return reaches(unit.position, state.patches[size_t(i)].position, patchReach); };
    std::optional<int64_t> best;
    for (int64_t i = 0; i < static_cast<int64_t>(state.patches.size()); i++) {
        if (!(state.patches[size_t(i)].remaining > 0 && owned.count(patchBase[size_t(i)]))) continue;
        if (!walkable(i)) continue;
        if (!best || cost(i, unit, n) < cost(*best, unit, n)) best = i;
    }
    if (best || mine.empty()) return best;
    // Nothing left at its own bases (mined out, no money for a new
    // Citadel): rather than stand idle, mine the nearest field where no
    // enemy Citadel stands and haul it home; the ore pays for the next
    // base. (Divergence from Swift, which left them idle.)
    std::set<int64_t> foes;
    for (const auto& b : state.structures) {
        if (b.kind == Structure::Kind::citadel && state.hostile(b.owner, unit.owner)) foes.insert(site(b));
    }
    for (int64_t i = 0; i < static_cast<int64_t>(state.patches.size()); i++) {
        if (!(state.patches[size_t(i)].remaining > 0 && !foes.count(patchBase[size_t(i)]))) continue;
        if (!walkable(i)) continue;
        if (!best || cost(i, unit, n) < cost(*best, unit, n)) best = i;
    }
    return best;
}

bool Simulation::reaches(Vec2 from, Vec2 to, double reach) const {
    if (!nav) return true;
    const auto start = nav->nearestFree(from, 4);
    if (!start) return true;
    const auto i = nav->index(*start);
    return !i || nav->reachable(*i, to, reach, false);
}

double Simulation::cost(int64_t p, const Unit& unit, const std::map<int64_t, int64_t>& n) const {
    auto it = n.find(p);
    const int64_t k = it != n.end() ? it->second : 0;
    const double crowd = k >= workersPerPatch ? 1000.0 * static_cast<double>(k) : 3.0 * static_cast<double>(k);
    // Rounded, so a tie is a tie on both halves of a mirrored map and
    // goes to the patch listed first (mirrored bases list theirs in
    // mirrored order).
    return crowd + rounded(walk(unit.position, state.patches[size_t(p)].position) * 1e6) / 1e6;
}

// MARK: - Units

void Simulation::stepUnit(Unit& u, double dt, std::vector<GameEvent>& events) {
    using Task = Unit::Task;
    if (pilot && pilot->unit == u.id) { stepPiloted(u, dt, events); return; }
    if (u.soldier()) { stepSoldier(u, dt, events); return; }
    if (u.task == Task::errand) { stepErrand(u, dt, events); return; }
    if (prospectorDefends(u, dt, events)) return;
    auto wellOf = [&](const Structure& r) -> std::optional<size_t> {
        if (!state.wells) return std::nullopt;
        for (size_t g = 0; g < state.wells->size(); g++) if (distance((*state.wells)[g].position, r.position) < 0.5) return g;
        return std::nullopt;
    };
    switch (u.task) {
    case Task::idle: {
        if (u.carrying > 0 && !bases(u.owner).empty()) { u.task = Task::toBase; return; }
        // Look for work once a second, not every frame.
        u.timer -= dt;
        if (!(u.timer <= 0)) return;
        u.timer = 1;
        if (auto p = choosePatch(u)) { u.patch = *p; u.task = Task::toPatch; }
        break;
    }
    case Task::toPatch: {
        if (!u.patch || !(state.patches[size_t(*u.patch)].remaining > 0)) { u.task = Task::idle; return; }
        const int64_t pi_ = *u.patch;
        if (travel(u, state.patches[size_t(pi_)].position, Rules::mineRange, dt)) {
            // The walk ended short of the patch: it cannot get there (a
            // base walled in by its buildings). Not mined from afar: it
            // looks for a patch it can reach (C++ only).
            if (distance(u.position, state.patches[size_t(pi_)].position) > patchArrival) {
                u.patch = std::nullopt; u.task = Task::idle; u.timer = 0;
                return;
            }
            arrive(u, pi_);
        }
        break;
    }
    case Task::waiting: {
        if (!u.patch || !(state.patches[size_t(*u.patch)].remaining > 0)) { u.task = Task::idle; return; }
        const int64_t pi_ = *u.patch;
        // Not at it (a saved game from before `patchArrival`): find another.
        if (distance(u.position, state.patches[size_t(pi_)].position) > patchArrival) {
            u.patch = std::nullopt; u.task = Task::idle; u.timer = 0;
            return;
        }
        turn(u, state.patches[size_t(pi_)].position, dt);
        if (!state.patches[size_t(pi_)].miner) arrive(u, pi_);
        break;
    }
    case Task::mining: {
        if (!u.patch) { u.task = Task::idle; return; }
        const size_t pi_ = size_t(*u.patch);
        turn(u, state.patches[pi_].position, dt);
        u.timer -= dt * rate(Stat::Drill{}, u);
        if (u.timer <= 0) {
            const int64_t take = ac::min(load(u, false), state.patches[pi_].remaining);
            state.patches[pi_].remaining -= take;
            state.patches[pi_].miner = std::nullopt;
            u.carrying = take;
            if (state.patches[pi_].remaining <= 0) {
                state.patches[pi_].depletedAt = state.time;
                events.push_back(GameEvent::PatchDepleted{static_cast<int64_t>(pi_)});
            }
            u.task = Task::toBase;
        }
        break;
    }
    case Task::toBase: {
        // Keep the base chosen when the trip started.
        std::optional<Structure> current;
        if (u.goal) {
            for (const auto& b : bases(u.owner)) if (b.position == *u.goal) { current = b; break; }
        }
        const auto citadel = current ? current : base(u.position, u.owner);
        if (!citadel) { u.task = Task::idle; return; }
        if (travel(u, citadel->position, Rules::depositRange, dt)) {
            auto& pl = state.players[size_t(u.owner)];
            if (u.hydrogen == true) {
                pl.hydrogen += u.carrying;
            } else {
                pl.ore += u.carrying;
                pl.totalMined += u.carrying;
            }
            u.hydrogen = std::nullopt;
            events.push_back(GameEvent::Deposited{u.id, u.carrying, citadel->position});
            u.carrying = 0;
            u.task = Task::depositing;
            u.timer = 0.12;
        }
        break;
    }
    case Task::depositing: {
        u.timer -= dt;
        if (u.timer <= 0) {
            // Back to the same patch or Derrick while it lasts, like a real worker.
            std::optional<Structure> r;
            if (u.structure) r = state.structure(*u.structure);
            if (u.patch && state.patches[size_t(*u.patch)].remaining > 0) u.task = Task::toPatch;
            else if (r && r->kind == Structure::Kind::derrick) u.task = Task::toDerrick;
            else { u.task = Task::idle; u.timer = 0; }
        }
        break;
    }
    case Task::toBuild: {
        if (!u.order) {
            // Sent to finish a building whose builder died.
            std::optional<Structure> s;
            if (u.structure) s = state.structure(*u.structure);
            if (!s || s->complete()) {
                u.structure = std::nullopt; u.task = Task::idle; return;
            }
            const int64_t sid = s->id;
            if (travel(u, s->position, Rules::radius(s->kind) + 0.4, dt)) {
                for (auto& o : state.structures) {
                    if (o.id == sid) { o.builder = u.id; u.task = Task::building; break; }
                }
            }
            return;
        }
        const BuildOrder o = *u.order;
        if (travel(u, o.position, Rules::radius(o.kind) + 0.4, dt)) {
            // Someone built there first (the other player): refund.
            if (any(state.structures, [&](const Structure& s) {
                    return distance(s.position, o.position) < Rules::radius(s.kind) + Rules::radius(o.kind) + 0.2;
                })) {
                state.players[size_t(u.owner)].ore += Rules::cost(o.kind);
                u.order = std::nullopt;
                u.task = Task::idle;
                return;
            }
            const Structure s(state.nextID, o.kind, u.owner, o.position, Rules::buildTime(o.kind), u.id);
            state.nextID += 1;
            state.structures.push_back(s);
            events.push_back(GameEvent::ConstructionStarted{s.id, s.kind, s.position});
            u.order = std::nullopt;
            u.structure = s.id;
            u.task = Task::building;
        }
        break;
    }
    case Task::building: {
        std::optional<Structure> s;
        if (u.structure) s = state.structure(*u.structure);
        if (!s || !s->buildLeft) {
            u.structure = std::nullopt;
            u.task = Task::idle;
            u.timer = 0;
            return;
        }
        turn(u, s->position, dt);
        break;
    }
    case Task::repairing: {
        std::optional<size_t> i;
        if (u.structure) {
            for (size_t k = 0; k < state.structures.size(); k++) if (state.structures[k].id == *u.structure) { i = k; break; }
        }
        if (!i || !(state.structures[*i].hp < Rules::hp(state.structures[*i].kind))) {
            u.structure = std::nullopt; u.task = Task::idle; u.timer = 0; return;
        }
        const Structure s = state.structures[*i];
        if (travel(u, s.position, Rules::radius(s.kind) + 0.5, dt)) {
            turn(u, s.position, dt);
            // A full repair takes as long as building it.
            state.structures[*i].hp = ac::min(Rules::hp(s.kind), s.hp + Rules::hp(s.kind) / Rules::buildTime(s.kind) * dt
                                                                      * rate(Stat::Repair{}, u));
        }
        break;
    }
    case Task::toDerrick: {
        std::optional<Structure> r;
        if (u.structure) r = state.structure(*u.structure);
        std::optional<size_t> gi;
        if (r && r->kind == Structure::Kind::derrick) gi = wellOf(*r);
        if (!r || r->kind != Structure::Kind::derrick || !gi || !((*state.wells)[*gi].remaining > 0)) {
            u.task = Task::idle; u.structure = std::nullopt; return;
        }
        if (travel(u, r->position, Rules::radius(Structure::Kind::derrick) + 0.3, dt)) {
            // One at a time inside; the others wait at the door.
            if (!(*state.wells)[*gi].harvester) {
                (*state.wells)[*gi].harvester = u.id;
                u.task = Task::inDerrick;
                u.timer = Rules::hydrogenTime;
            } else {
                turn(u, r->position, dt);
            }
        }
        break;
    }
    case Task::inDerrick: {
        std::optional<Structure> r;
        if (u.structure) r = state.structure(*u.structure);
        std::optional<size_t> gi;
        if (r) gi = wellOf(*r);
        if (!r || !gi) {
            u.task = Task::idle; u.structure = std::nullopt; return;
        }
        u.timer -= dt * rate(Stat::Hydrogen{}, u);
        if (u.timer <= 0) {
            auto& g = (*state.wells)[*gi];
            const int64_t take = ac::min(load(u, true), g.remaining);
            g.remaining -= take;
            g.harvester = std::nullopt;
            u.carrying = take;
            u.hydrogen = true;
            u.task = Task::toBase;
        }
        break;
    }
    case Task::toRally:
    case Task::attackMove:
    case Task::attacking:
    case Task::toBastion:
    case Task::inBastion:
    case Task::aboard:
    case Task::errand:
        u.task = Task::idle;
        break;
    }
}

/// A Prospector that was just hit fights back: it strikes an enemy on the
/// ground in melee reach, or closes on one within 3.5 cells (workers
/// mob a raider in their ore line). Not from inside a Derrick, nor
/// while it builds. It drops what it was drilling and goes back to work
/// once no one is on it. True while it fights.
bool Simulation::prospectorDefends(Unit& u, double dt, std::vector<GameEvent>& events) {
    using Task = Unit::Task;
    if (u.cooldown && *u.cooldown > 0) u.cooldown = ac::max(0.0, *u.cooldown - dt);
    if (!(state.time - u.hitAt.value_or(-100) < 2 && u.task != Task::inDerrick && u.task != Task::building
          && u.task != Task::toBuild && u.task != Task::toDerrick)) return false;
    if (auto m = meleeFoe(u)) {
        const auto [id, t] = *m;
        if (u.task == Task::mining || u.task == Task::waiting) { leavePatch(u); u.task = Task::toPatch; }
        if (fire(u, id, t, dt)) events.push_back(GameEvent::Shot{u.id, id, t.owner, t.position});
        return true;
    }
    const auto near = minBy(filtered(state.units, [&](const Unit& v) {
                                return state.hostile(v.owner, u.owner) && v.hp > 0 && !v.stats().air && v.task != Task::inBastion
                                    && v.task != Task::aboard && v.task != Task::inDerrick
                                    && distance(v.position, u.position) < 3.5 && sees(u.owner, v.id);
                            }),
                            [&](const Unit& a, const Unit& b) { return distance(a.position, u.position) < distance(b.position, u.position); });
    if (!near) return false;
    const Unit foe_ = *near;
    if (u.task == Task::mining || u.task == Task::waiting) { leavePatch(u); u.task = Task::toPatch; }
    u.goal = std::nullopt; u.waypoints = std::nullopt;
    (void)move(u, foe_.position, Rules::radius(foe_.kind) + Rules::radius(Unit::Kind::prospector) + 0.1, dt);
    return true;
}

/// A Prospector's errand (`Command.mission`): `.raid(p)` scouts, walking
/// to `p` and waiting there; `.hunt(p)` pulls it off the ore to fight
/// the enemies around `p`; nil sends it back to work. A builder, or
/// one in a Derrick, is not sent.
bool Simulation::sendOnErrand(int64_t i, const std::optional<Mission>& m) {
    using Task = Unit::Task;
    Unit u = state.units[size_t(i)];
    if (!(!u.order && u.task != Task::building && u.task != Task::toBuild && u.task != Task::inDerrick && u.task != Task::aboard)) {
        return false;
    }
    if (m && (m->is<Mission::Raid>() || m->is<Mission::Hunt>() || m->is<Mission::FallBack>())) {
        leavePatch(u);
        u.task = Task::errand;
    } else if (!m) {
        if (u.task == Task::errand) {
            // Back from running (C++ only, a FallBack errand): to the patch
            // it worked, while it lasts.
            const bool ran = u.mission && u.mission->is<Mission::FallBack>();
            if (ran && u.patch && state.patches[size_t(*u.patch)].remaining > 0) {
                u.task = u.carrying > 0 ? Task::toBase : Task::toPatch;
            } else {
                u.task = Task::idle; u.timer = 0;
            }
        }
    } else {
        return false;
    }
    u.mission = m;
    u.goal = std::nullopt; u.waypoints = std::nullopt; u.target = std::nullopt;
    state.units[size_t(i)] = u;
    return true;
}

/// A Prospector on an errand: a scout walks to its point and waits;
/// one pulled to fight goes for the nearest enemy on the ground within
/// `Rules.sight` of its point and strikes it in melee, or waits at the
/// point.
void Simulation::stepErrand(Unit& u, double dt, std::vector<GameEvent>& events) {
    if (u.cooldown && *u.cooldown > 0) u.cooldown = ac::max(0.0, *u.cooldown - dt);
    if (u.mission && u.mission->is<Mission::Raid>()) {
        const Vec2 p = u.mission->as<Mission::Raid>()->at;
        if (distance(u.position, p) > 1) (void)travel(u, p, 0.8, dt);
    } else if (u.mission && u.mission->is<Mission::FallBack>()) {
        // Running from a base under attack (C++ only): to the spot, then wait.
        const Vec2 p = u.mission->as<Mission::FallBack>()->at;
        if (distance(u.position, p) > 1.5) (void)travel(u, p, 1.2, dt);
    } else if (u.mission && u.mission->is<Mission::Hunt>()) {
        const Vec2 p = u.mission->as<Mission::Hunt>()->at;
        if (auto m = meleeFoe(u)) {
            const auto [id, t] = *m;
            if (fire(u, id, t, dt)) events.push_back(GameEvent::Shot{u.id, id, t.owner, t.position});
            return;
        }
        const auto foe_ = minBy(filtered(state.units, [&](const Unit& v) {
                                    return state.hostile(v.owner, u.owner) && v.soldier() && v.hp > 0 && !v.stats().air
                                        && v.task != Unit::Task::inBastion && v.task != Unit::Task::aboard
                                        && distance(v.position, p) < Rules::sight;
                                }),
                                [&](const Unit& a, const Unit& b) { return distance(a.position, u.position) < distance(b.position, u.position); });
        if (foe_) {
            (void)travel(u, foe_->position, Rules::radius(foe_->kind) + Rules::radius(Unit::Kind::prospector) + 0.1, dt);
        } else if (distance(u.position, p) > 1.5) {
            (void)travel(u, p, 1, dt);
        }
    } else {
        u.task = Unit::Task::idle; u.mission = std::nullopt;
    }
}

/// The nearest enemy on the ground within a Prospector's melee reach (units
/// first, then buildings).
std::optional<std::pair<int64_t, Simulation::Target>> Simulation::meleeFoe(const Unit& u) const {
    const double reach = Rules::stats(Unit::Kind::prospector).range + 0.15;
    struct Best { int64_t id; Target t; double g; };
    std::optional<Best> best;
    for (const auto& v : state.units) {
        if (!(state.hostile(v.owner, u.owner) && v.hp > 0)) continue;
        auto t = foe(v.id, u.owner);
        if (!t || t->air) continue;
        const double g = gap(u, *t);
        if (g <= reach && (best ? g < best->g : true)) best = Best{v.id, *t, g};
    }
    if (!best) {
        for (const auto& s : state.structures) {
            if (!state.hostile(s.owner, u.owner)) continue;
            auto t = foe(s.id, u.owner);
            if (!t) continue;
            // A building's footprint is square: reach its nearest edge.
            const double g = ac::max(std::abs(u.position.x - s.position.x), std::abs(u.position.y - s.position.y)) - t->radius
                - Rules::radius(u.kind);
            if (g <= reach && (best ? g < best->g : true)) best = Best{s.id, *t, g};
        }
    }
    if (!best) return std::nullopt;
    return std::pair<int64_t, Target>{best->id, best->t};
}

/// At the patch: drill it if free, else take a free neighbour of the same
/// base (the worker "bounce"), else wait for it.
void Simulation::arrive(Unit& u, int64_t pi_) {
    const int64_t id = u.id;
    const auto patches = state.patches;
    auto free = [&](int64_t i) { return !patches[size_t(i)].miner || *patches[size_t(i)].miner == id; };
    if (free(pi_)) {
        state.patches[size_t(pi_)].miner = u.id;
        u.task = Unit::Task::mining;
        u.timer = Rules::miningTime;
        return;
    }
    const auto n = assigned();
    const Vec2 here = state.patches[size_t(pi_)].position;
    std::optional<int64_t> other;
    for (int64_t k = 0; k < static_cast<int64_t>(state.patches.size()); k++) {
        auto it = n.find(k);
        const int64_t taken = it != n.end() ? it->second : 0;
        if (!(k != pi_ && patchBase[size_t(k)] == patchBase[size_t(pi_)] && state.patches[size_t(k)].remaining > 0 && free(k)
              && taken < workersPerPatch && distance(state.patches[size_t(k)].position, here) < 4)) continue;
        if (!other || distance(state.patches[size_t(k)].position, here) < distance(state.patches[size_t(*other)].position, here)) other = k;
    }
    if (other) {
        u.patch = *other;
        u.task = Unit::Task::toPatch;
    } else {
        u.task = Unit::Task::waiting;
    }
}

std::optional<std::vector<Vec2>> Simulation::findPath(Vec2 from, Vec2 target, double stopAt, bool jumps) {
    if (!pathAhead) return nav->path(from, target, stopAt, jumps);
    return probing ? pathAhead->guess(*nav, from, target, stopAt, jumps) : pathAhead->find(*nav, from, target, stopAt, jumps);
}

/// Walk toward `target` along ramps when it is on another level; true on
/// arrival within `stopAt`.
bool Simulation::travel(Unit& u, Vec2 target_, double stopAt, double dt) {
    if (u.stats().air) {
        // Flying: straight there. The goal is kept while it flies, like a
        // walker's: the attack-move and rally branches of `stepSoldier` only
        // walk on between two thinks while a goal is set, and a flier
        // without one moved one step in eight (stop-and-go).
        const bool arrived = move(u, target_, stopAt, dt);
        u.goal = arrived || !flierKeepsGoal ? std::nullopt : std::optional<Vec2>(target_);
        return arrived;
    }
    if (u.planted()) {
        // An anchored Longbow, or a buried Scorpion, cannot drive. Whether it packs up is
        // `stepAnchor`'s call alone: a tank that packed up whenever it
        // had somewhere to go never finished anchoring.
        return false;
    }
    if (u.goal != target_) {
        // Fast paths: past this step's budget the unit keeps walking its old
        // route, or waits where it is, and asks again next step.
        if (fastPaths && pathsStarted >= pathBudget) {
            if (!(u.goal && u.waypoints && !u.waypoints->empty())) return false;
        } else {
        pathsStarted++;
        u.goal = target_;
        // Around obstacles. When there is no way to the target (boxed in
        // by buildings), to the nearest spot it can reach; never through
        // anything. Straight between ramps only without a nav grid.
        if (nav) {
            if (auto path = findPath(u.position, target_, stopAt, u.kind == Unit::Kind::comet)) {
                u.waypoints = *path;
            } else if (auto near = nav->closestReachable(target_, u.position)) {
                if (auto path2 = findPath(u.position, *near, 0.1)) {
                    auto way = *path2;
                    way.push_back(*near);
                    u.waypoints = way;
                } else {
                    u.waypoints = std::vector<Vec2>{};
                }
            } else {
                u.waypoints = std::vector<Vec2>{};
            }
        } else {
            std::vector<Vec2> path = router ? router->route(u.position, target_) : std::vector<Vec2>{target_};
            if (!path.empty()) path.pop_back();
            u.waypoints = path;
        }
        }
    }
    if (u.waypoints && !u.waypoints->empty()) {
        std::vector<Vec2> ahead = *u.waypoints;
        const Vec2 next = ahead.front();
        const Vec2 from = u.position;
        if (move(u, next, 0.15, dt)) {
            ahead.erase(ahead.begin());
            u.waypoints = ahead;
        }
        if (u.kind == Unit::Kind::comet) {
            leap(u, from, next);
        } else if (offCourse(u, from)) {
            // Pushed off its line (by the crowd), the straight leg now
            // cuts into something solid: step back and find a new way.
            u.position = from;
            u.goal = std::nullopt;
        }
        return false;
    }
    // The last leg straight to the target, only when it is in the clear.
    if (nav && distance(u.position, target_) > stopAt + 0.05 && !nav->clear(u.position, target_)
        && (NavGrid::solid(u.position + normalize(target_ - u.position) * 0.3, state) || !nav->walkable(target_))) {
        // Unreachable from here: stop where it stands.
        u.goal = std::nullopt; u.waypoints = std::nullopt;
        return true;
    }
    const Vec2 from = u.position;
    const bool arrived = move(u, target_, stopAt, dt);
    if (u.kind == Unit::Kind::comet) {
        leap(u, from, target_);
    } else if (offCourse(u, from)) {
        u.position = from; u.goal = std::nullopt; u.waypoints = std::nullopt; return false;
    }
    if (arrived) { u.goal = std::nullopt; u.waypoints = std::nullopt; }
    return arrived;
}

/// A ground step from open ground into a blocked cell or a rock.
bool Simulation::offCourse(const Unit& u, Vec2 from) const {
    if (!nav) return false;
    return nav->walkable(from) && !nav->inRock(from) && (!nav->walkable(u.position) || nav->inRock(u.position));
}

/// A Comet stepping onto a cliff face jumps it: it takes off where it
/// was and lands on the first walkable ground ahead; on landing the
/// jump ends.
void Simulation::leap(Unit& u, Vec2 from, Vec2 next) const {
    if (!nav) return;
    if (nav->walkable(u.position)) {
        if (u.jumpFrom) { u.jumpFrom = std::nullopt; u.jumpTo = std::nullopt; }
        return;
    }
    if (!(!u.jumpFrom && nav->cliff(u.position))) return;
    if (!(distance(next, u.position) > 1e-6)) return;
    // Nothing walkable before `next` (or under 0.2 cells to go): it
    // lands on `next`.
    u.jumpFrom = from;
    u.jumpTo = landing(u.position, next).value_or(next);
}

/// The first walkable spot from `p` toward `next`, in 0.2-cell steps
/// (where a jump over a cliff face comes down); nil if there is none
/// before `next`.
std::optional<Vec2> Simulation::landing(Vec2 p, Vec2 next) const {
    if (!nav) return std::nullopt;
    const Vec2 d = next - p;
    const double len = length(d);
    // `1...0` would trap: under 0.2 cells there is no step to check.
    if (!(len >= 0.2)) return std::nullopt;
    const int64_t steps = static_cast<int64_t>(len / 0.2);
    for (int64_t k = 1; k <= steps; k++) {
        const Vec2 q = p + d / len * (static_cast<double>(k) * 0.2);
        if (nav->walkable(q)) return q;
    }
    return std::nullopt;
}

/// Turn toward `target`; true when facing it within a small angle.
bool Simulation::turn(Unit& u, Vec2 target_, double dt, double turnRate) const {
    const Vec2 d = target_ - u.position;
    if (!(length(d) > 1e-6)) return true;
    const double want = std::atan2(d.y, d.x);
    double diff = std::remainder(want - u.heading, 2 * pi);
    const double maxTurn = turnRate * dt;
    diff = ac::max(-maxTurn, ac::min(maxTurn, diff));
    u.heading = std::remainder(u.heading + diff, 2 * pi);
    return std::abs(std::remainder(want - u.heading, 2 * pi)) < 0.6;
}

/// Walk toward `target` until within `stopAt`; true on arrival.
bool Simulation::move(Unit& u, Vec2 target_, double stopAt, double dt) const {
    const Vec2 d = target_ - u.position;
    const double dist = length(d);
    if (dist <= stopAt + 1e-6) return true;
    if (u.planted()) return false;
    const bool facing = turn(u, target_, dt, u.kind == Unit::Kind::longbow ? 5 : u.kind == Unit::Kind::hailstorm ? 6
                                                 : u.kind == Unit::Kind::firefly ? 9
                                                 : u.kind == Unit::Kind::atlas ? Rules::atlasBodyRate : Rules::prospectorTurnRate);
    const bool slowed = u.slowUntil.value_or(-1) > state.time;
    const double speed = Rules::speed(u.kind) * rate(Stat::Speed{}, u) * (facing ? 1 : 0.3) * (slowed ? u.slowedTo.value_or(0.5) : 1);
    const double stepLen = ac::min(speed * dt, dist - stopAt);
    // Facing (within 0.6 rad): along its heading. Still turning, or
    // when a step along its heading would land in something solid:
    // along the straight line to the target, which is clear. A sideways
    // step into a building or a cliff is taken back, and the unit used
    // to turn back and forth on that spot for good.
    Vec2 dir = d / dist;
    if (facing) {
        const Vec2 ahead(std::cos(u.heading), std::sin(u.heading));
        const Vec2 q = u.position + ahead * stepLen;
        if (u.stats().air || (nav ? nav->walkable(q) && !nav->inRock(q) : true)) dir = ahead;
    }
    u.position += dir * stepLen;
    u.stride += stepLen;
    return distance(u.position, target_) <= stopAt + 1e-3;
}

} // namespace ac
