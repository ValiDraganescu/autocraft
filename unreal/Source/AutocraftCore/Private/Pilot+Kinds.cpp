// Port of Sources/GameCore/Pilot+Kinds.swift. Declarations in
// Simulation+Kinds.members.h.
//
// What the driven unit does that depends on its kind, past the Prospector's
// work: a tank's turret and anchored mode, a Comet's cliff jumps, a
// Dropship's heal and cargo. The guns (Comet, Firefly, Juggernaut, tank)
// need nothing here: `strike` does their pistols, flame, grenades and
// splash.

#include "Simulation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ac {

// MARK: - Facing

/// Face where the mouse says. A vehicle's hull turns on A/D instead
/// (slowly anchored, not while changing mode), and its turret, if it has
/// one, traverses toward where the mouse looks at its own rate.
void Simulation::pilotFace(Unit& u, const Pilot& p, double dt) const {
    const double h = std::remainder(p.heading, 2 * pi);
    if (!Pilot::steers(u.kind)) {
        u.heading = h;
        return;
    }
    const double turn_ = max(-1.0, min(1.0, p.walk.y));
    u.heading = std::remainder(u.heading + turn_ * Pilot::hullTurnRate(u.kind) * Pilot::pace(u) * dt, 2 * pi);
    if (const auto rate_ = Pilot::turretRate(u.kind)) {
        const double from = u.aim ? *u.aim : u.heading;
        const double diff = std::remainder(h - from, 2 * pi);
        u.aim = std::remainder(from + max(-*rate_ * dt, min(*rate_ * dt, diff)), 2 * pi);
    }
}

// MARK: - Ability

/// What the ability key would do for `u` now; nil for a kind with no
/// ability. A tank anchors or packs up (a press mid-way turns it back);
/// a Dropship loads the unit of its side under its sight (`healSight`)
/// if that one is in load reach and fits, else sets down everyone
/// aboard.
std::optional<PilotAbility> Simulation::pilotAbility(const Unit& u) const {
    // A leveling pick's burst or grenade takes the key of a kind with
    // none.
    if (auto b = keyAbility(u)) return b;
    switch (u.kind) {
    case Unit::Kind::longbow:
        return u.anchored == true ? PilotAbility(PilotAbility::Action::Unanchor{}, "Tank Mode")
                                  : PilotAbility(PilotAbility::Action::Anchor{}, "Anchor Mode");
    case Unit::Kind::scorpion:
        return u.anchored == true ? PilotAbility(PilotAbility::Action::Unanchor{}, "Dig out")
                                  : PilotAbility(PilotAbility::Action::Anchor{}, "Bury");
    case Unit::Kind::atlas: {
        if (const double wait = u.stompReady.value_or(0) - state.time; wait > 0) {
            char why[64];
            std::snprintf(why, sizeof why, "Ready in %.0f s", std::ceil(wait));
            return PilotAbility(std::nullopt, "Quake stomp", std::string(why));
        }
        return PilotAbility(PilotAbility::Action::Stomp{}, "Quake stomp");
    }
    case Unit::Kind::dropship: {
        std::optional<Unit> sighted;
        if (const auto s = healSight(u)) {
            if (const auto i = unitIndex(s->target)) sighted = state.units[*i];
        }
        const bool near = sighted ? distance(sighted->position, u.position) - Rules::radius(sighted->kind)
                                            - Rules::radius(u.kind)
                                        <= pilotLoadReach
                                  : false;
        if (sighted && Rules::boards(sighted->kind) && near
            && slotsUsed(u) + Rules::slots(sighted->kind) <= cargoSlots(u)) {
            return PilotAbility(PilotAbility::Action::Load{sighted->id}, "Load");
        }
        if (u.cargo && !u.cargo->empty()) return PilotAbility(PilotAbility::Action::Unload{}, "Unload All");
        // Empty, so anything in reach would fit.
        return PilotAbility(std::nullopt, "Load", !sighted ? "Look at a unit to load it" : "Too far to load");
    }
    default:
        return std::nullopt;
    }
}

/// The ability key went down: do what `pilotAbility` says.
void Simulation::pilotUse(Unit& u, std::vector<GameEvent>& events) {
    const auto ability = pilotAbility(u);
    if (!ability || !ability->action) return;
    const PilotAbility::Action& a = *ability->action;
    if (a.is<PilotAbility::Action::Anchor>()) {
        u.anchored = true;
    } else if (a.is<PilotAbility::Action::Unanchor>()) {
        u.anchored = false;
    } else if (const auto l = a.as<PilotAbility::Action::Load>()) {
        // `board` writes the cargo into `state`; `u` is the copy that
        // `step` writes back over it.
        if (board(u.id, l->_0)) {
            if (const auto mi = unitIndex(u.id)) u.cargo = state.units[*mi].cargo;
        }
    } else if (a.is<PilotAbility::Action::Unload>()) {
        unload(u);
    } else if (a.is<PilotAbility::Action::Burst>()) {
        startBurst(u);
    } else if (a.is<PilotAbility::Action::Grenade>()) {
        throwGrenade(u, events);
    } else if (a.is<PilotAbility::Action::Stomp>()) {
        quake(u, events);
    }
}

// MARK: - Comet

/// A jumper whose next step is into a cliff face takes off over it, for
/// the first walkable ground ahead (the 0.2-cell scan of `leap`, out to
/// `pilotJumpReach`, farther with Long jump). True when it took off.
bool Simulation::pilotJump(Unit& u, Vec2 step) const {
    if (!nav || !(length(step) > 1e-9)) return false;
    const Vec2 next = u.position + step;
    if (pilotCanStand(u, next) || !nav->cliff(next)) return false;
    const double reach = boost(Stat::JumpReach{}, u).apply(pilotJumpReach);
    const auto land = landing(next, next + normalize(step) * reach);
    if (!land || !pilotCanStand(u, *land)) return false;
    u.jumpFrom = u.position;
    u.jumpTo = *land;
    return true;
}

/// Mid-jump: on toward `jumpTo` at its speed; there, the jump ends (and
/// Booster's rush starts).
void Simulation::pilotFly(Unit& u, double dt) const {
    if (!u.jumpTo) {
        u.jumpFrom = std::nullopt;
        return;
    }
    const Vec2 to = *u.jumpTo;
    const Vec2 d = to - u.position;
    const double len = length(d), step = Rules::speed(u.kind) * rate(Stat::Speed{}, u) * dt;
    if (len <= step) {
        u.position = to;
        u.jumpFrom = std::nullopt;
        u.jumpTo = std::nullopt;
        const auto r = effect(u, [](const Effect& e) -> std::optional<Rush> {
            if (const auto a = e.as<Effect::AfterJump>()) return a->rush;
            return std::nullopt;
        });
        if (r) {
            u.rush = *r;
            u.rushUntil = state.time + r->seconds;
        }
    } else {
        u.position += d / len * step;
    }
    u.stride += min(len, step);
}

// MARK: - Dropship

/// Cargo slots taken aboard a Dropship (`Rules.slots` each, of
/// `Rules.dropshipSlots`).
int64_t Simulation::slotsUsed(const Unit& m) const {
    int64_t n = 0;
    if (!m.cargo) return n;
    for (const int64_t c : *m.cargo) {
        if (const auto i = unitIndex(c)) n += Rules::slots(state.units[*i].kind);
    }
    return n;
}

/// Living units of `m`'s side it could heal (bio units, machines too with
/// Nanite beam) or load (a Scorpion too): out in the open, not aboard, in a
/// Bastion or in a Derrick.
std::vector<Unit> Simulation::patients(const Unit& m) const {
    std::vector<Unit> out;
    for (const Unit& v : state.units) {
        if (v.owner == m.owner && v.id != m.id && (healable(m, v) || Rules::boards(v.kind))
            && v.hp > 0 && v.task != Unit::Task::aboard && v.task != Unit::Task::inBastion
            && v.task != Unit::Task::inDerrick) {
            out.push_back(v);
        }
    }
    return out;
}

/// The Dropship `m`'s beam can heal `v`: a bio unit, or any machine with
/// Nanite beam.
bool Simulation::healable(const Unit& m, const Unit& v) const {
    const bool machines = effect(m, [](const Effect& e) -> std::optional<bool> {
                              if (e.is<Effect::NaniteBeam>()) return true;
                              return std::nullopt;
                          }) == true;
    return v.stats().bio || (machines && Leveling::machines.contains(v.kind));
}

/// Double beam: the most hurt patient in heal reach of `m` other than
/// `id`, if any.
std::optional<int64_t> Simulation::secondPatient(const Unit& m, int64_t id) const {
    std::optional<Unit> best;
    for (const Unit& v : patients(m)) {
        if (!(v.id != id && healable(m, v) && v.hp < maxHP(v)
              && distance(v.position, m.position) - Rules::radius(v.kind) - Rules::radius(m.kind) <= Rules::healRange)) {
            continue;
        }
        if (!best || v.hp / maxHP(v) < best->hp / maxHP(*best)) best = v;
    }
    if (!best) return std::nullopt;
    return best->id;
}

/// A healer's sight: the first patient on its line of sight, or one
/// right under it (centre within its radius of its ground point), whole
/// or hurt. Reach is `Rules.healRange`.
std::optional<PilotSight> Simulation::healSight(const Unit& u) const {
    std::optional<PilotSight> best;
    for (const Unit& v : patients(u)) {
        const bool under = distance(v.position, u.position) <= Rules::radius(u.kind);
        const auto g = under ? std::optional<double>(0) : sightGap(u, v.position, Rules::radius(v.kind));
        if (!g) continue;
        if (best ? *g < best->distance : true) {
            best = PilotSight{.target = v.id, .distance = *g, .range = Rules::healRange,
                              .inRange = *g <= Rules::healRange + 0.25, .friend_ = true};
        }
    }
    return best;
}

/// Heal the patient under the sight, if it is in reach and hurt and
/// there is energy for it.
std::optional<PilotTarget> Simulation::healTarget(const Unit& u) const {
    if (!(u.energy.value_or(0) > 0)) return std::nullopt;
    const auto s = healSight(u);
    if (!s || !s->inRange) return std::nullopt;
    const auto i = unitIndex(s->target);
    if (!i || !healable(u, state.units[*i]) || !(state.units[*i].hp < maxHP(state.units[*i]))) return std::nullopt;
    return PilotTarget(PilotTarget::Heal{s->target});
}

/// One step of a Dropship's beam on unit `id`: `Rules.healRate` a second,
/// a third of an energy point per hit point, never past full (its
/// picks change both). It shows as attacking that unit (the scene draws
/// the beam from that).
void Simulation::heal(Unit& m, int64_t id, double dt) {
    const auto i = unitIndex(id);
    if (!i) return;
    const Unit v = state.units[*i];
    const double perEnergy = boost(Stat::HealEnergy{}, m).apply(3.0);
    const double amount =
        min(min(Rules::healRate * rate(Stat::Heal{}, m) * dt, maxHP(v) - v.hp), m.energy.value_or(0) * perEnergy);
    state.units[*i].hp += amount;
    earn(Leveling::worth(v.kind) * amount / maxHP(v) * Leveling::mendXP, m);
    tally(m, [&](Tally& t) { t.healed += amount; });
    m.energy = m.energy.value_or(0) - amount / perEnergy;
    m.task = Unit::Task::attacking;
    m.target = id;
    // Cruise: it is healing, so it does not cruise for a moment.
    if (piloted(m.id)) {
        const double now = state.time;
        updateLevels([&](PilotRecord& r) { r.kinds[m.kind].healedAt = now; });
    }
}

} // namespace ac
