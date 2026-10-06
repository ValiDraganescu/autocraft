// Port of the `extension Simulation` of Sources/GameCore/Pilot.swift: the
// player driving one unit by hand. The types are in Pilot.h, the member
// declarations in Simulation+Pilot.members.h.
//
// Built for any unit kind: walking, facing and the hand-back are the same
// for all; what the action key does depends on the kind (`pilotTarget`,
// `pilotAct`), as does anything the unit is busy with (`pilotBusy`).

#include "Simulation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ac {

// MARK: - Taking over

/// Take the unit `unit` under manual control; nil hands it back to the
/// AI. False when it is not the player's, or not a kind that can be
/// driven yet.
bool Simulation::take(std::optional<int64_t> unit) {
    if (pilot) {
        if (const auto i = unitIndex(pilot->unit)) {
            // Handed back: it finds work, goes home with its load, or rejoins
            // the army by itself.
            Unit u = state.units[*i];
            if (u.kind == Unit::Kind::prospector && u.task != Unit::Task::mining && u.task != Unit::Task::inDerrick) {
                u.task = Unit::Task::idle;
                u.timer = 0;
            }
            u.goal = std::nullopt;
            u.waypoints = std::nullopt;
            // Mid-jump, it lands; a healer stops healing; anyone aboard is
            // set down (the AI only unloads on a drop it planned itself).
            if (u.jumpTo && u.jumpFrom) {
                u.position = *u.jumpTo;
                u.jumpFrom = std::nullopt;
                u.jumpTo = std::nullopt;
            }
            if (u.task == Unit::Task::attacking) {
                u.task = Unit::Task::idle;
                u.target = std::nullopt;
            }
            if (u.cargo && !u.cargo->empty()) unload(u);
            // Its pilot's shield goes with the pilot.
            u.shield = std::nullopt;
            state.units[*i] = u;
        }
    }
    pilot = std::nullopt;
    if (!unit) return true;
    const auto i = unitIndex(*unit);
    if (!i || state.units[*i].owner != Pilot::player || !Pilot::drivable.contains(state.units[*i].kind)
        || !canDrive(state.units[*i])) {
        return false;
    }
    Unit u = state.units[*i];
    // Whatever it was doing stops; a paid building order is refunded.
    if (u.order) {
        state.players[u.owner].ore += Rules::cost(u.order->kind);
        u.order = std::nullopt;
    }
    if (u.task == Unit::Task::building && u.structure) {
        const int64_t sid = *u.structure;
        const auto s = std::find_if(state.structures.begin(), state.structures.end(),
                                    [&](const Structure& st) { return st.id == sid; });
        if (s != state.structures.end() && s->builder == u.id) s->builder = std::nullopt;
    }
    switch (u.task) {
    case Unit::Task::mining:
    case Unit::Task::inDerrick: break;   // busy work carries on under the pilot
    case Unit::Task::building:
    case Unit::Task::repairing:
    case Unit::Task::toBuild:
    case Unit::Task::toBastion:
        u.structure = std::nullopt;
        u.task = Unit::Task::idle;
        break;
    default: u.task = Unit::Task::idle;
    }
    u.goal = std::nullopt;
    u.waypoints = std::nullopt;
    u.target = std::nullopt;
    u.mission = std::nullopt;
    state.units[*i] = u;
    pilot = Pilot(*unit, u.heading);
    // The kind joins this game's leveling record, at level 1 if new,
    // and counts a take-over.
    updateLevels([&](PilotRecord& r) { r.kinds[u.kind].tally.takeovers += 1; });
    return true;
}

/// A unit that is somewhere it cannot be driven from: inside a Bastion or
/// a Dropship, or mid-jump.
bool Simulation::canDrive(const Unit& u) const {
    return u.task != Unit::Task::inBastion && u.task != Unit::Task::aboard && !u.jumpFrom;
}

/// True for the unit the player drives.
bool Simulation::piloted(int64_t id) const { return pilot && pilot->unit == id; }

/// The unit the player drives, if it is alive.
std::optional<Unit> Simulation::pilotUnit() const {
    if (!pilot) return std::nullopt;
    const auto i = unitIndex(pilot->unit);
    if (!i) return std::nullopt;
    return state.units[*i];
}

// MARK: - Every kind

/// One step of the driven unit: its upkeep and the ability key, then
/// busy work (it stands still for it), else it faces where the mouse
/// says and walks where the keys say; then the action key. Anyone
/// aboard rides along.
void Simulation::stepPiloted(Unit& u, double dt, std::vector<GameEvent>& events) {
    if (!pilot) return;
    const Pilot p = *pilot;
    // Swift's `defer { carry(u) }`: every way out below carries.
    struct Carry {
        Simulation& sim_;
        Unit& unit_;
        ~Carry() { sim_.carry(unit_); }
    } const carryOnExit{*this, u};
    u.goal = std::nullopt;
    u.waypoints = std::nullopt;
    if (u.cooldown && *u.cooldown > 0) u.cooldown = max(0.0, *u.cooldown - dt);
    if (u.railCooldown && *u.railCooldown > 0) u.railCooldown = max(0.0, *u.railCooldown - dt);
    // Only a heal sets it attacking, and only for this step.
    if (u.task == Unit::Task::attacking) {
        u.task = Unit::Task::idle;
        u.target = std::nullopt;
    }
    upkeep(u, dt);
    levelUpkeep(u, dt);
    if (p.ability) pilotUse(u, events);
    if (pilotBusy(u, p, dt, events)) return;
    pilotFace(u, p, dt);
    pilotWalk(u, p.walk, dt);
    // Taking off: no shot on the way up either.
    if (u.jumpFrom) return;
    pilotArrive(u, events);
    // A tank anchoring or packing up cannot fire.
    if (!((p.act || p.hold) && settled(u))) return;
    const auto target = pilotTarget(u);
    if (!target) return pilotMiss(u, events);
    pilotAct(u, *target, p.act, dt, events);
}

/// Not between tank and anchored mode.
bool Simulation::settled(const Unit& u) const { return u.anchor ? (*u.anchor == 0 || *u.anchor >= 1) : true; }

/// A unit whose weapon reaches past arm's length (a Ranger's rifle):
/// it aims along its sight and fires with nothing in it, too.
bool Simulation::pilotShoots(const Unit& u) const {
    const UnitStats w = weapon(u);
    return (w.hitsGround || w.hitsAir) && w.range >= 1.5 && u.kind != Unit::Kind::prospector;
}

/// The trigger held with nothing under the sight: a gun still fires,
/// at its weapon's pace, into the distance. Not with an enemy inside its
/// minimum range on the sight: the shell would land on it.
void Simulation::pilotMiss(Unit& u, std::vector<GameEvent>& events) {
    // A Scorpion's sting is not wasted on the air.
    if (u.kind == Unit::Kind::scorpion) { u.lockTarget = std::nullopt; return; }
    if (!(pilotShoots(u) && u.cooldown.value_or(0) <= 0)) return;
    if (const auto s = pilotSight(u); s && s->distance < s->minRange) return;
    reload(u, weapon(u));
    const Vec2 f(std::cos(u.look()), std::sin(u.look()));
    events.push_back(GameEvent::Missed{u.id, u.position + f * weapon(u).range});
}

/// Walk in the unit's own frame; blocked, it slides along whichever axis
/// is still open (a jumper takes off at a cliff face instead). Wheels
/// and treads do not sidestep; an anchored Longbow creeps (`Pilot.pace`).
void Simulation::pilotWalk(Unit& u, Vec2 walk, double dt) {
    const double pace = Pilot::pace(u);
    if (!(pace > 0)) return;
    Vec2 w = Pilot::strafes(u.kind) ? walk : Vec2(walk.x, 0);
    const double len = length(w);
    if (!(len > 0.01)) return;
    if (len > 1) w /= len;
    const double h = u.heading;
    const Vec2 forward(std::cos(h), std::sin(h));
    const Vec2 right(-std::sin(h), std::cos(h));
    // Backing up and sidestepping are slower than going ahead.
    const double ahead_ = w.x < 0 ? 0.6 : 1.0;
    const bool slowed = u.slowUntil.value_or(-1) > state.time;
    const double speed =
        Rules::speed(u.kind) * rate(Stat::Speed{}, u) * pace * (slowed ? u.slowedTo.value_or(0.5) : 1) * dt;
    const Vec2 dir = forward * (w.x * ahead_) + right * (w.y * 0.8);
    const Vec2 d = dir * speed;
    if (pilotJumps(u) && pilotJump(u, d)) return;
    const Vec2 from = u.position;
    for (const Vec2 step : {d, Vec2(d.x, 0), Vec2(0, d.y)}) {
        if (!(length(step) > 1e-9)) continue;
        if (pilotCanStand(u, u.position + step)) {
            u.position += step;
            break;
        }
    }
    u.stride += distance(from, u.position);
}

/// Nothing solid at `p` for the unit: open ground, clear of buildings
/// and live ore deposits (a flyer goes anywhere on the map).
bool Simulation::pilotCanStand(const Unit& u, Vec2 p) const {
    if (u.stats().air) {
        if (!map) return true;
        const GroundRect b = map->bounds;
        return p.x > b.minX && p.x < b.maxX && p.y > b.minZ && p.y < b.maxZ;
    }
    if (nav && (!nav->standable(p) || nav->inRock(p))) return false;
    const double r = Rules::radius(u.kind) * 0.8;
    if (std::any_of(state.structures.begin(), state.structures.end(),
                    [&](const Structure& s) { return NavGrid::insideStructure(p, s, r); })) {
        return false;
    }
    if (std::any_of(state.patches.begin(), state.patches.end(), [&](const OreDeposit& m) {
            return m.remaining > 0 && NavGrid::insidePatch(p, m, r - 0.1);
        })) {
        return false;
    }
    return true;
}

/// Something the unit is busy with that holds it in place (mid-jump,
/// a Prospector drilling, inside a Derrick, welding or mending). True while
/// it is.
bool Simulation::pilotBusy(Unit& u, const Pilot& p, double dt, std::vector<GameEvent>& events) {
    if (u.jumpFrom) {
        pilotFace(u, p, dt);
        pilotFly(u, dt);
        return true;
    }
    switch (u.kind) {
    case Unit::Kind::prospector: return prospectorBusy(u, p, dt, events);
    default: return false;
    }
}

/// Whatever happens by walking somewhere (a Prospector drops its load at a
/// Citadel).
void Simulation::pilotArrive(Unit& u, std::vector<GameEvent>& events) {
    if (u.kind == Unit::Kind::prospector) prospectorDeposit(u, events);
}

/// `p` lies ahead of `u`, within about 70° of where it faces.
bool Simulation::ahead(const Unit& u, Vec2 p) const {
    const Vec2 d = p - u.position;
    const double len = length(d);
    return len < 1e-6 || dot(d / len, Vec2(std::cos(u.look()), std::sin(u.look()))) > 0.35;
}

/// The enemy the unit would strike: the nearest ahead, on the ground,
/// within its weapon's reach (and a little). A gun takes the first one
/// on its line of sight instead (`pilotAimed`).
std::optional<int64_t> Simulation::pilotFoe(const Unit& u) const {
    const UnitStats w = weapon(u);
    if (!(w.hitsGround || w.hitsAir)) return std::nullopt;
    if (pilotShoots(u)) return pilotAimed(u);
    const double reach = w.range + 0.25;
    std::optional<std::pair<int64_t, double>> best;
    for (const Unit& v : state.units) {
        if (!(state.hostile(v.owner, u.owner) && v.hp > 0)) continue;
        const auto t = foe(v.id, u.owner);
        if (!t || !canHit(u, *t) || !ahead(u, t->position)) continue;
        const double g = gap(u, *t);
        if (g <= reach && (best ? g < best->second : true)) best = std::make_pair(v.id, g);
    }
    for (const Structure& s : state.structures) {
        if (!(state.hostile(s.owner, u.owner))) continue;
        const auto t = foe(s.id, u.owner);
        if (!t || !w.hitsGround) continue;
        const double g = max(std::abs(u.position.x - s.position.x), std::abs(u.position.y - s.position.y))
                         - t->radius - Rules::radius(u.kind);
        if (!(g <= reach && (ahead(u, s.position) || g < 0.3))) continue;
        if (best ? g < best->second : true) best = std::make_pair(s.id, g);
    }
    if (!best) return std::nullopt;
    return best->first;
}

/// The first enemy a gun's line of fire meets within its range (the
/// high-ground bonus counted), if the first one on the line is in range.
std::optional<int64_t> Simulation::pilotAimed(const Unit& u) const {
    const auto s = pilotSight(u);
    if (!s || !s->inRange) return std::nullopt;
    return s->target;
}

/// The first enemy on the driven gun's line of fire (where it looks),
/// out to sight range: a unit or building whose edge comes within
/// `pilotAimSlack` of the line. `distance` is edge to edge; `inRange`
/// is true if and only if a shot now would reach it (not inside a
/// anchored tank's minimum range). A Dropship gets its patient instead
/// (`healSight`).
std::optional<PilotSight> Simulation::pilotSight(const Unit& u) const {
    if (u.kind == Unit::Kind::dropship) return healSight(u);
    if (Pilot::strafes(u.kind) && pilot && pilot->unit == u.id && pilot->crosshair) {
        return crosshairSight(u, *pilot->crosshair);
    }
    const UnitStats w = weapon(u);
    if (!(w.hitsGround || w.hitsAir)) return std::nullopt;
    std::optional<PilotSight> best;
    const auto consider = [&](int64_t id, const Target& t, double r) {
        const auto g = sightGap(u, t.position, r);
        if (!g) return;
        const UnitStats x = weapon(u, t.air);
        const double reach = range(x.range, u.position, t.position);
        if (best ? *g < best->distance : true) {
            best = PilotSight{.target = id, .distance = *g, .range = reach, .minRange = x.minRange,
                              .inRange = *g >= x.minRange && *g <= reach + 0.25};
        }
    };
    for (const Unit& v : state.units) {
        if (!(state.hostile(v.owner, u.owner) && v.hp > 0)) continue;
        const auto t = foe(v.id, u.owner);
        if (!t || !canHit(u, *t)) continue;
        consider(v.id, *t, t->radius);
    }
    for (const Structure& s : state.structures) {
        if (!(state.hostile(s.owner, u.owner) && w.hitsGround)) continue;
        const auto t = foe(s.id, u.owner);
        if (!t) continue;
        consider(s.id, *t, t->radius);
    }
    return best;
}

/// A gun on foot's sight: the enemy the crosshair is on, if it can hit
/// it and it is within sight; nothing for a friend, a rock or the sky.
std::optional<PilotSight> Simulation::crosshairSight(const Unit& u, const Pilot::Crosshair& c) const {
    const auto on = c.as<Pilot::Crosshair::On>();
    if (!on) return std::nullopt;
    const int64_t id = on->_0;
    const auto t = foe(id, u.owner);
    if (!t || !state.hostile(t->owner, u.owner) || !canHit(u, *t)) return std::nullopt;
    const UnitStats w = weapon(u, t->air);
    const double g = max(0.0, gap(u, *t));
    if (!(g <= max(Rules::sight, w.range + 1))) return std::nullopt;
    const double reach = range(w.range, u.position, t->position);
    return PilotSight{.target = id, .distance = g, .range = reach, .minRange = w.minRange,
                      .inRange = g >= w.minRange && g <= reach + 0.25};
}

/// Edge to edge, how far along `u`'s line of sight it meets something of
/// radius `r` at `p`: nil when the line passes it by, it is behind, or
/// it is out of sight range.
std::optional<double> Simulation::sightGap(const Unit& u, Vec2 p, double r) const {
    const Vec2 dir(std::cos(u.look()), std::sin(u.look()));
    const Vec2 rel = p - u.position;
    const double along = dot(rel, dir);
    const double off = length(rel - dir * along);
    if (!(along > 0 && off <= r + pilotAimSlack)) return std::nullopt;
    // Its near edge, where the line enters it.
    const double enter = along - std::sqrt(max(r * r - off * off, 0.0));
    const double g = max(0.0, enter - Rules::radius(u.kind));
    // It sees at least as far as its gun reaches (an anchored Longbow's 13).
    if (g <= max(Rules::sight, weapon(u).range + 1)) return g;
    return std::nullopt;
}

/// What one attack of `u` would take off target `id` now.
std::optional<double> Simulation::strikeDamage(const Unit& u, int64_t id) const {
    const auto t = target(id);
    if (!t) return std::nullopt;
    return damage(u, *t);
}

/// What the action key would do for `u` where it stands and faces.
std::optional<PilotTarget> Simulation::pilotTarget(const Unit& u) const {
    if (mending(u)) {
        if (u.task == Unit::Task::mining || u.task == Unit::Task::inDerrick) return std::nullopt;
        return pilotRepair(u);
    }
    if (const auto f = pilotFoe(u)) return PilotTarget(PilotTarget::Enemy{*f});
    switch (u.kind) {
    case Unit::Kind::prospector: return prospectorTarget(u);
    case Unit::Kind::dropship: return healTarget(u);
    default: return std::nullopt;
    }
}

void Simulation::pilotAct(Unit& u, const PilotTarget& target_, bool pressed, double dt,
                          std::vector<GameEvent>& events) {
    if (const auto e = target_.as<PilotTarget::Enemy>()) {
        const int64_t id = e->_0;
        // No turning to it: the unit faces where the mouse says.
        const auto t = target(id);
        if (!t || !(cooldownFor(u, t->air) <= 0)) return;
        // A Scorpion stings only buried, and after a second's lock-on: the
        // fire button is held that long on the target.
        if (u.kind == Unit::Kind::scorpion && !(u.burrowed() && lockOn(u, id, dt))) return;
        reload(u, weapon(u, t->air));
        if (u.kind == Unit::Kind::scorpion) u.lockTarget = std::nullopt;
        strike(u, id, *t);
        events.push_back(GameEvent::Shot{u.id, id, t->owner, t->position});
    } else if (const auto pa = target_.as<PilotTarget::Patch>()) {
        prospectorMine(u, pa->_0);
    } else if (const auto de = target_.as<PilotTarget::Derrick>()) {
        if (pressed && !de->busy) prospectorEnter(u, de->_0);
    } else if (const auto sc = target_.as<PilotTarget::Scaffold>()) {
        if (pressed) prospectorResume(u, sc->_0);
    } else if (const auto re = target_.as<PilotTarget::Repair>()) {
        u.structure = re->_0;
        u.task = Unit::Task::repairing;
    } else if (target_.is<PilotTarget::Full>()) {
    } else if (const auto he = target_.as<PilotTarget::Heal>()) {
        const int64_t id = he->_0;
        if (!(pilot && pilot->hold)) return;
        heal(u, id, dt);
        // Double beam: the most hurt other patient in reach as well.
        const bool doubleBeam =
            effect(u, [](const Effect& x) -> std::optional<bool> {
                if (x.is<Effect::DoubleBeam>()) return true;
                return std::nullopt;
            }) == true;
        if (doubleBeam) {
            if (const auto other = secondPatient(u, id)) {
                heal(u, *other, dt);
                u.target = id;
            }
        }
    }
}

// MARK: - Prospector

std::optional<PilotTarget> Simulation::prospectorTarget(const Unit& u) const {
    if (u.task == Unit::Task::mining || u.task == Unit::Task::inDerrick) return std::nullopt;
    // Its own finished Derrick, on a well with MH left.
    std::optional<Structure> derrick;
    for (const Structure& s : state.structures) {
        if (s.kind == Structure::Kind::derrick && s.owner == u.owner && s.complete()
            && NavGrid::insideStructure(u.position, s, pilotHydrogenReach) && ahead(u, s.position)) {
            const auto wi = wellIndex(s);
            if (wi && (*state.wells)[*wi].remaining > 0) {
                derrick = s;
                break;
            }
        }
    }
    std::optional<int64_t> patch;
    for (int64_t i = 0; i < static_cast<int64_t>(state.patches.size()); ++i) {
        const OreDeposit& p = state.patches[i];
        if (!(p.remaining > 0 && NavGrid::insidePatch(u.position, p, pilotReach) && ahead(u, p.position))) continue;
        if (!patch || distance(p.position, u.position) < distance(state.patches[*patch].position, u.position)) {
            patch = i;
        }
    }
    if (derrick || patch) {
        if (u.carrying > 0) return PilotTarget(PilotTarget::Full{});
        if (derrick) {
            if (const auto gi = wellIndex(*derrick)) {
                const auto inside = (*state.wells)[*gi].harvester;
                return PilotTarget(PilotTarget::Derrick{derrick->id, inside.has_value() && inside != u.id});
            }
        }
        if (!patch) return std::nullopt;
        return PilotTarget(PilotTarget::Patch{*patch});
    }
    if (const auto v = hurtMachine(u)) return PilotTarget(PilotTarget::Repair{*v});
    // Its own buildings: pick up an abandoned one, or mend a hurt one.
    const Structure* own = nullptr;
    for (const Structure& s : state.structures) {
        if (!(s.owner == u.owner && NavGrid::insideStructure(u.position, s, pilotHydrogenReach) && ahead(u, s.position))) {
            continue;
        }
        if (!own || distance(s.position, u.position) < distance(own->position, u.position)) own = &s;
    }
    if (!own || own->kind == Structure::Kind::lab) return std::nullopt;
    const Structure& s = *own;
    if (!s.complete()) {
        bool welded = false;
        if (s.builder) {
            if (const auto b = unitIndex(*s.builder)) {
                const Unit& w = state.units[*b];
                welded = w.task == Unit::Task::building && w.structure == s.id;
            }
        }
        if (welded) return std::nullopt;
        return PilotTarget(PilotTarget::Scaffold{s.id});
    }
    if (s.hp < Rules::hp(s.kind)) return PilotTarget(PilotTarget::Repair{s.id});
    return std::nullopt;
}

/// The driven Prospector with R held (`Pilot.mend`): it only repairs.
bool Simulation::mending(const Unit& u) const {
    return u.kind == Unit::Kind::prospector && pilot && pilot->mend && piloted(u.id);
}

/// What the driven Prospector would repair with R held: a hurt machine
/// (Field welder), else the nearest damaged finished building of its
/// own, close and ahead. Nil: nothing to repair there.
std::optional<PilotTarget> Simulation::pilotRepair(const Unit& u) const {
    if (u.kind != Unit::Kind::prospector) return std::nullopt;
    if (const auto v = hurtMachine(u)) return PilotTarget(PilotTarget::Repair{*v});
    const Structure* best = nullptr;
    for (const Structure& s : state.structures) {
        if (!(s.owner == u.owner && s.complete() && s.kind != Structure::Kind::lab && s.hp < Rules::hp(s.kind)
              && NavGrid::insideStructure(u.position, s, pilotHydrogenReach) && ahead(u, s.position))) {
            continue;
        }
        if (!best || distance(s.position, u.position) < distance(best->position, u.position)) best = &s;
    }
    if (!best) return std::nullopt;
    return PilotTarget(PilotTarget::Repair{best->id});
}

/// Field welder: a hurt machine of its side, close and ahead.
std::optional<int64_t> Simulation::hurtMachine(const Unit& u) const {
    if (!machineRepair(u)) return std::nullopt;
    const Unit* best = nullptr;
    for (const Unit& v : state.units) {
        if (!(v.owner == u.owner && v.id != u.id && v.hp > 0 && v.hp < maxHP(v) && Leveling::machines.contains(v.kind)
              && canDrive(v) && v.task != Unit::Task::inDerrick
              && gap(u, Target{.position = v.position, .radius = Rules::radius(v.kind), .structure = false,
                               .owner = v.owner}) <= pilotHydrogenReach
              && ahead(u, v.position))) {
            continue;
        }
        if (!best || distance(v.position, u.position) < distance(best->position, u.position)) best = &v;
    }
    if (!best) return std::nullopt;
    return best->id;
}

std::optional<int64_t> Simulation::wellIndex(const Structure& s) const {
    if (!state.wells) return std::nullopt;
    for (int64_t i = 0; i < static_cast<int64_t>(state.wells->size()); ++i) {
        if (distance((*state.wells)[i].position, s.position) < 0.5) return i;
    }
    return std::nullopt;
}

/// Drilling (while the key is held), inside a Derrick, welding a
/// building (until the Prospector walks off) or mending one (while held).
/// Walking off a patch, or letting go of the key, loses the drilling.
bool Simulation::prospectorBusy(Unit& u, const Pilot& p, double dt, std::vector<GameEvent>& events) {
    const bool walking = length(p.walk) > 0.1;
    switch (u.task) {
    case Unit::Task::inDerrick: {
        const auto r = u.structure ? state.structure(*u.structure) : std::nullopt;
        const auto gi = r ? wellIndex(*r) : std::nullopt;
        if (!gi) {
            u.task = Unit::Task::idle;
            u.structure = std::nullopt;
            return false;
        }
        u.timer -= dt * rate(Stat::Hydrogen{}, u);
        if (u.timer <= 0) {
            Well& g = (*state.wells)[*gi];
            const int64_t take_ = min(load(u, true), g.remaining);
            g.remaining -= take_;
            g.harvester = std::nullopt;
            u.carrying = take_;
            u.hydrogen = true;
            u.task = Unit::Task::idle;
        }
        return true;
    }
    case Unit::Task::mining: {
        if (!(u.patch && state.patches[*u.patch].remaining > 0)) {
            u.task = Unit::Task::idle;
            return false;
        }
        const int64_t pi_ = *u.patch;
        if (walking || !p.hold) {
            leavePatch(u);
            return false;
        }
        turn(u, state.patches[pi_].position, dt);
        u.timer -= dt * rate(Stat::Drill{}, u);
        if (u.timer <= 0) {
            // Rich vein: every third trip carries double.
            const int64_t base = load(u, false);
            const int64_t times = thirdTime(Action::oreTrip, u) ? 2 : 1;
            const int64_t take_ = min(base * times, state.patches[pi_].remaining);
            state.patches[pi_].remaining -= take_;
            state.patches[pi_].miner = std::nullopt;
            u.carrying = take_;
            u.hydrogen = std::nullopt;
            if (state.patches[pi_].remaining <= 0) {
                state.patches[pi_].depletedAt = state.time;
                events.push_back(GameEvent::PatchDepleted{pi_});
            }
            u.task = Unit::Task::idle;
        }
        return true;
    }
    case Unit::Task::building: {
        std::optional<size_t> i;
        if (u.structure) {
            for (size_t k = 0; k < state.structures.size(); ++k) {
                if (state.structures[k].id == *u.structure) {
                    i = k;
                    break;
                }
            }
        }
        if (!i || state.structures[*i].complete()) {
            u.task = Unit::Task::idle;
            u.structure = std::nullopt;
            return false;
        }
        if (walking) {
            // Walked off: the building waits, half-built, for any Prospector.
            if (state.structures[*i].builder == u.id) state.structures[*i].builder = std::nullopt;
            u.task = Unit::Task::idle;
            u.structure = std::nullopt;
            return false;
        }
        turn(u, state.structures[*i].position, dt);
        return true;
    }
    case Unit::Task::repairing: {
        if (u.structure && unitIndex(*u.structure)) return prospectorMend(u, *u.structure, p, dt);
        std::optional<size_t> i;
        if (u.structure) {
            for (size_t k = 0; k < state.structures.size(); ++k) {
                if (state.structures[k].id == *u.structure) {
                    i = k;
                    break;
                }
            }
        }
        if (!(i && state.structures[*i].hp < Rules::hp(state.structures[*i].kind) && p.hold && !walking)) {
            u.task = Unit::Task::idle;
            u.structure = std::nullopt;
            return false;
        }
        const Structure s = state.structures[*i];
        turn(u, s.position, dt);
        // A full repair takes as long as building it; the driven
        // Prospector mends `Rules.heroWork` times as fast (or as its picks say).
        const double full = Rules::hp(s.kind);
        state.structures[*i].hp = min(full, s.hp + full / Rules::buildTime(s.kind) * rate(Stat::Repair{}, u) * dt);
        const double mended = state.structures[*i].hp - s.hp;
        earn(Leveling::worth(s.kind) * mended / full * Leveling::mendXP, u);
        tally(u, [&](Tally& t) { t.repaired += mended; });
        return true;
    }
    default:
        u.task = Unit::Task::idle;
        return false;
    }
}

/// Field welder: the driven Prospector mends machine `id` while the key is
/// held and it stays close; a full repair takes the pick's share of the
/// machine's training time.
bool Simulation::prospectorMend(Unit& u, int64_t id, const Pilot& p, double dt) {
    const auto share = machineRepair(u);
    const auto j = unitIndex(id);
    if (!(share && j && p.hold && length(p.walk) <= 0.1)) {
        u.task = Unit::Task::idle;
        u.structure = std::nullopt;
        return false;
    }
    const Unit v = state.units[*j];
    const double full = maxHP(v);
    const double reach =
        gap(u, Target{.position = v.position, .radius = Rules::radius(v.kind), .structure = false, .owner = v.owner});
    if (!(v.hp > 0 && v.hp < full && reach <= pilotHydrogenReach + 0.3 && canDrive(v))) {
        u.task = Unit::Task::idle;
        u.structure = std::nullopt;
        return false;
    }
    turn(u, v.position, dt);
    const double mended = min(full - v.hp, full / (Rules::trainTime(v.kind) * *share) * dt);
    state.units[*j].hp += mended;
    earn(Leveling::worth(v.kind) * mended / full * Leveling::mendXP, u);
    tally(u, [&](Tally& t) { t.repaired += mended; });
    return true;
}

/// A load is dropped off by walking up to one's own Citadel.
void Simulation::prospectorDeposit(Unit& u, std::vector<GameEvent>& events) {
    if (!(u.carrying > 0)) return;
    std::optional<Structure> citadel;
    for (const Structure& b : bases(u.owner)) {
        if (NavGrid::insideStructure(u.position, b, pilotDepositReach)) {
            citadel = b;
            break;
        }
    }
    if (!citadel) return;
    earn(static_cast<double>(u.carrying) * (u.hydrogen == true ? Leveling::hydrogenXP : Leveling::oreXP), u);
    const int64_t load_ = u.carrying;
    const bool hydrogen = u.hydrogen == true;
    tally(u, [&](Tally& t) {
        if (hydrogen) {
            t.hydrogen += load_;
        } else {
            t.ore += load_;
        }
    });
    if (u.hydrogen == true) {
        state.players[u.owner].hydrogen += u.carrying;
    } else {
        state.players[u.owner].ore += u.carrying;
        state.players[u.owner].totalMined += u.carrying;
    }
    events.push_back(GameEvent::Deposited{u.id, u.carrying, citadel->position});
    u.carrying = 0;
    u.hydrogen = std::nullopt;
}

void Simulation::prospectorMine(Unit& u, int64_t pi_) {
    // The player goes first: a Prospector drilling here moves along.
    if (const auto other = state.patches[pi_].miner; other && *other != u.id) {
        if (const auto k = unitIndex(*other)) {
            state.units[*k].task = Unit::Task::toPatch;
            state.units[*k].timer = 0;
        }
    }
    state.patches[pi_].miner = u.id;
    u.patch = pi_;
    u.structure = std::nullopt;
    u.task = Unit::Task::mining;
    u.timer = Rules::miningTime;
}

void Simulation::prospectorEnter(Unit& u, int64_t rid) {
    const auto r = state.structure(rid);
    if (!r) return;
    const auto gi = wellIndex(*r);
    if (!gi) return;
    (*state.wells)[*gi].harvester = u.id;
    u.structure = rid;
    u.patch = std::nullopt;
    u.task = Unit::Task::inDerrick;
    u.timer = Rules::hydrogenTime;
}

void Simulation::prospectorResume(Unit& u, int64_t sid) {
    for (Structure& s : state.structures) {
        if (s.id != sid) continue;
        s.builder = u.id;
        u.structure = sid;
        u.task = Unit::Task::building;
        return;
    }
}

// MARK: - Building by hand

/// Why player `owner` cannot build `kind` now (money, supply of tech),
/// or nil.
std::optional<std::string> Simulation::buildRefusal(Structure::Kind kind, int64_t owner) const {
    const Player& p = state.players[owner];
    const Price price = buildCost(kind, owner);
    if (p.ore < price.ore) return "Not enough ore";
    if (p.hydrogen < price.hydrogen) return "Not enough MH";
    if (const auto need = Rules::requires_(kind)) {
        const bool has_ = std::any_of(state.structures.begin(), state.structures.end(), [&](const Structure& s) {
            return s.kind == *need && s.complete() && s.owner == owner;
        });
        if (!has_) return "Needs a " + title(*need);
    }
    return std::nullopt;
}

/// Where a building of `kind` would stand if placed near `p`: on the
/// build grid (whole cells for odd sizes, cell corners for even), and a
/// Derrick on the well nearest `p` (within 4 cells).
std::optional<Vec2> Simulation::snap(Structure::Kind kind, Vec2 p) const {
    if (kind == Structure::Kind::derrick) {
        if (!state.wells) return std::nullopt;
        std::optional<Vec2> best;
        for (const Well& w : *state.wells) {
            if (!(distance(w.position, p) < 4)) continue;
            if (!best || distance(w.position, p) < distance(*best, p)) best = w.position;
        }
        return best;
    }
    const double r = Rules::radius(kind);
    const bool odd = static_cast<int64_t>(rounded(2 * r)) % 2 == 1;
    const auto g = [&](double v) { return odd ? rounded(v - 0.5) + 0.5 : rounded(v); };
    return Vec2(g(p.x), g(p.y));
}

/// Why a `kind` cannot stand at `p` (the ground, or something in the
/// way), or nil when it can.
std::optional<std::string> Simulation::placementRefusal(Structure::Kind kind, Vec2 p) const {
    const double r = Rules::radius(kind);
    if (kind == Structure::Kind::derrick) {
        const bool onWell = state.wells && std::any_of(state.wells->begin(), state.wells->end(), [&](const Well& w) {
            return distance(w.position, p) < 0.5 && w.remaining > 0;
        });
        if (!onWell) return "Must go on an MH well";
        if (std::any_of(state.structures.begin(), state.structures.end(),
                        [&](const Structure& s) { return distance(s.position, p) < 0.5; })) {
            return "The well is taken";
        }
        return std::nullopt;
    }
    // Flat, open ground under the whole footprint.
    double low = std::numeric_limits<double>::infinity(), high = -std::numeric_limits<double>::infinity();
    // Swift's `stride(from:through:by:)` over Doubles: start + i × step,
    // up to and including the end.
    const double from = -r + 0.25, through = r - 0.25;
    for (int64_t iz = 0;; ++iz) {
        const double dz = from + static_cast<double>(iz) * 0.5;
        if (dz > through) break;
        for (int64_t ix = 0;; ++ix) {
            const double dx = from + static_cast<double>(ix) * 0.5;
            if (dx > through) break;
            const Vec2 q = p + Vec2(dx, dz);
            if (nav && (!nav->standable(q) || nav->inRock(q))) return "Can't build there";
            if (field) {
                const double h = field->height(q);
                low = min(low, h);
                high = max(high, h);
            }
        }
    }
    if (high - low > 0.2) return "The ground is not level";
    for (const Structure& s : state.structures) {
        if (std::abs(s.position.x - p.x) < r + Rules::radius(s.kind)
            && std::abs(s.position.y - p.y) < r + Rules::radius(s.kind)) {
            return "Something is in the way";
        }
    }
    // Add-ons need room on the +X side of a production building.
    for (const Structure& s : state.structures) {
        if (!((s.kind == Structure::Kind::garrison || s.kind == Structure::Kind::foundry
               || s.kind == Structure::Kind::spacedock)
              && !s.addon)) {
            continue;
        }
        const Vec2 lab = s.position + Rules::addonOffset;
        if (std::abs(lab.x - p.x) < r + 1 && std::abs(lab.y - p.y) < r + 1) return "Leave room for the Lab";
    }
    for (const OreDeposit& m : state.patches) {
        if (!(m.remaining > 0)) continue;
        // A field is 2x1: its corners and centre against the footprint.
        const double c = std::cos(m.angle), s = std::sin(m.angle);
        constexpr std::array<std::pair<double, double>, 5> corners{
            {{0.0, 0.0}, {1, 0.5}, {1, -0.5}, {-1, 0.5}, {-1, -0.5}}};
        for (const auto& [lx, lz] : corners) {
            const Vec2 q = m.position + Vec2(lx * c + lz * s, -lx * s + lz * c);
            if (std::abs(q.x - p.x) < r + 0.1 && std::abs(q.y - p.y) < r + 0.1) return "Something is in the way";
        }
    }
    if (state.wells) {
        for (const Well& g : *state.wells) {
            if (std::abs(g.position.x - p.x) < r + 1.5 && std::abs(g.position.y - p.y) < r + 1.5) {
                return "Something is in the way";
            }
        }
    }
    if (kind == Structure::Kind::citadel
        && (std::any_of(state.patches.begin(), state.patches.end(),
                        [&](const OreDeposit& m) { return m.remaining > 0 && distance(m.position, p) < 5.5; })
            || (state.wells && std::any_of(state.wells->begin(), state.wells->end(),
                                           [&](const Well& w) { return distance(w.position, p) < 6; })))) {
        return "Too close to resources";
    }
    return std::nullopt;
}

/// The driven Prospector puts up a `kind` at `p`: paid now, and it welds it
/// until done or until it walks off. Returns why not, or nil when it
/// started.
std::optional<std::string> Simulation::pilotBuild(Structure::Kind kind, Vec2 p) {
    const auto i = pilot ? unitIndex(pilot->unit) : std::nullopt;
    if (!i || state.units[*i].kind != Unit::Kind::prospector) return "No Prospector";
    const Unit u = state.units[*i];
    if (auto why = buildRefusal(kind, u.owner)) return why;
    if (auto why = placementRefusal(kind, p)) return why;
    if (NavGrid::insideStructure(u.position, Structure(-1, kind, 0, p), Rules::radius(Unit::Kind::prospector))) {
        return "Step back first";
    }
    if (u.task == Unit::Task::mining || u.task == Unit::Task::waiting) {
        Unit v = u;
        leavePatch(v);
        state.units[*i] = v;
    }
    const Price price = buildCost(kind, u.owner);
    state.players[u.owner].ore -= price.ore;
    state.players[u.owner].hydrogen -= price.hydrogen;
    const Structure s(state.nextID, kind, u.owner, p, Rules::buildTime(kind), u.id);
    state.nextID += 1;
    state.structures.push_back(s);
    state.units[*i].task = Unit::Task::building;
    state.units[*i].structure = s.id;
    refreshNav();
    return std::nullopt;
}

/// A building's name as the game writes it.
std::string Simulation::title(Structure::Kind k) {
    switch (k) {
    case Structure::Kind::citadel: return "Citadel";
    case Structure::Kind::habDome: return "Hab Dome";
    case Structure::Kind::garrison: return "Garrison";
    case Structure::Kind::bastion: return "Bastion";
    case Structure::Kind::derrick: return "Derrick";
    case Structure::Kind::foundry: return "Foundry";
    case Structure::Kind::spacedock: return "Spacedock";
    case Structure::Kind::lab: return "Lab";
    case Structure::Kind::sentinel: return "Sentinel";
    }
    return "";
}

/// A unit's name as the game writes it.
std::string Simulation::title(Unit::Kind k) {
    switch (k) {
    case Unit::Kind::prospector: return "Prospector";
    case Unit::Kind::ranger: return "Ranger";
    case Unit::Kind::comet: return "Comet";
    case Unit::Kind::firefly: return "Firefly";
    case Unit::Kind::juggernaut: return "Juggernaut";
    case Unit::Kind::dropship: return "Dropship";
    case Unit::Kind::longbow: return "Longbow";
    case Unit::Kind::kestrel: return "Kestrel";
    case Unit::Kind::hailstorm: return "Hailstorm";
    case Unit::Kind::peregrine: return "Peregrine";
    case Unit::Kind::atlas: return "Atlas";
    case Unit::Kind::scorpion: return "Scorpion";
    }
    return "";
}

} // namespace ac
