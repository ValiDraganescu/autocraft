// Port of Sources/GameCore/Simulation+Leveling.swift: the members of
// Simulation declared in Simulation+Leveling.members.h.
//
// Pilot leveling in the simulation (docs/leveling.md): XP as the driven
// unit works, picks as orders, and one place (`boost`) that says what the
// picks do to a unit's stat right now. With no one driving, every boost
// is `.none` and the game plays exactly as it did before leveling.
#include "Simulation.h"

#include "Commander.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace ac {

namespace {

bool hasPlayer(const GameState& state, int64_t p) { return p >= 0 && p < static_cast<int64_t>(state.players.size()); }

} // namespace

// MARK: - The record

/// The player's leveling this game (empty before any driving).
const PilotRecord& Simulation::levels() const {
    static const PilotRecord empty;
    if (!hasPlayer(state, Pilot::player)) return empty;
    const auto& r = state.players[static_cast<size_t>(Pilot::player)].pilot;
    return r ? *r : empty;
}

/// The pick is made and the player drives a unit of its kind now: its
/// effects are on.
bool Simulation::active(Perk perk) const {
    const auto d = pilotUnit();
    return d && d->kind == kind(perk) && levels().has(perk);
}

/// The effects of the picks for the kind the player drives now, and
/// the driven unit; nil with no one driving.
std::optional<std::pair<Unit, std::vector<Effect>>> Simulation::drivenEffects() const {
    const auto d = pilotUnit();
    if (!d) return std::nullopt;
    std::vector<Effect> all;
    for (Perk perk : levels().picks(d->kind)) {
        const auto& mine = effects(perk);
        all.insert(all.end(), mine.begin(), mine.end());
    }
    return std::make_pair(*d, all);
}

/// What a pick does in this simulation: its own effects, or the
/// ones a test tries on it (`trialEffects`).
const std::vector<Effect>& Simulation::effects(Perk perk) const {
    auto it = trialEffects.find(perk);
    return it != trialEffects.end() ? it->second : ac::effects(perk);
}

void Simulation::updateLevels(const std::function<void(PilotRecord&)>& body) {
    if (!hasPlayer(state, Pilot::player)) return;
    auto& slot = state.players[static_cast<size_t>(Pilot::player)].pilot;
    PilotRecord r = slot ? *slot : PilotRecord();
    body(r);
    slot = r;
}

// MARK: - Picking

/// Keep `perk` for its kind: one of the two on offer at that kind's
/// lowest pending level. False, and nothing changes, otherwise. The
/// same as `issue(.pick(player: Pilot.player, perk))`.
bool Simulation::pick(Perk perk) { return issue(Command::Pick{Pilot::player, perk}); }

/// `Command.pick`'s check and keep.
bool Simulation::keepPick(int64_t player, Perk perk) {
    if (!(player == Pilot::player && hasPlayer(state, player) && !levels().refusal(perk))) return false;
    const double now = state.time;
    updateLevels([&](PilotRecord& r) {
        KindRecord k = r.kinds[kind(perk)];
        k.picks.push_back(perk);
        // Its level's stamp says when.
        for (size_t i = k.stamps.size(); i-- > 0;) {
            if (k.stamps[i].level == level(perk)) {
                k.stamps[i].picked = now;
                break;
            }
        }
        r.kinds[kind(perk)] = k;
    });
    return true;
}

// MARK: - XP

/// The driven unit `u` did `xp` worth of work. A level crossed raises
/// `.leveledUp` at the end of the step. Nothing for any other unit.
void Simulation::earn(double xp, const Unit& u) {
    if (!(xp > 0 && std::isfinite(xp) && piloted(u.id))) return;
    int64_t before = 1, after = 1;
    updateLevels([&](PilotRecord& r) {
        KindRecord k = r.kinds[u.kind];
        before = k.level();
        k.xp += xp;
        after = k.level();
        r.kinds[u.kind] = k;
    });
    if (after > before) {
        for (int64_t l = before + 1; l <= after; l++) levelUps.push_back(GameEvent::LeveledUp{u.kind, l});
        stamp(u.kind, before + 1, after);
    }
}

/// XP for a hit of `shooter` that took `taken` hp off an enemy worth
/// `worth` with `full` hp: its worth times the share taken.
void Simulation::earnHit(const Unit& shooter, double worth, double taken, double full, bool building) {
    if (!(full > 0)) return;
    earn(worth * taken / full * (building ? Leveling::buildingXP : 1), shooter);
}

// MARK: - Tracking

/// Add to the tally of the kind the driven unit `u` is; nothing for
/// any other unit.
void Simulation::tally(const Unit& u, const std::function<void(Tally&)>& body) {
    if (!piloted(u.id)) return;
    const UnitKind k = u.kind;
    updateLevels([&](PilotRecord& r) { body(r.kinds[k].tally); });
}

/// A stamp for each of `levels`, which `kind` reached now: the time,
/// the seconds driven with it before, and every player's standing.
void Simulation::stamp(UnitKind kind, int64_t from, int64_t to) {
    const double now = state.time;
    std::vector<Standing> all;
    for (int64_t p = 0; p < static_cast<int64_t>(state.players.size()); p++) all.push_back(standing(p));
    updateLevels([&](PilotRecord& r) {
        auto it = r.kinds.find(kind);
        const double driven = it != r.kinds.end() ? it->second.tally.seconds : 0;
        auto& stamps = r.kinds[kind].stamps;
        for (int64_t l = from; l <= to; l++) stamps.push_back(Stamp{l, now, driven, all, std::nullopt});
    });
}

/// How player `p` stands now (`GameState.standing(of:)`).
Standing Simulation::standing(int64_t p) const { return state.standing(p); }

// MARK: - Effects

/// What the picks (and level 1's hero, `Leveling.hero`) do to `stat`
/// on `u` now. Only while the player drives a unit, only on its team,
/// and only the picks of the kind it drives: `.none` otherwise.
Boost Simulation::boost(const Stat& stat, const Unit& u) const {
    if (!pilot) return Boost::none;
    const auto di = unitIndex(pilot->unit);
    if (!di) return Boost::none;
    const Unit& d = state.units[static_cast<size_t>(*di)];
    if (!(d.owner == u.owner)) return Boost::none;
    const bool driven = d.id == u.id;
    Boost b;
    if (driven) {
        if (auto c = Leveling::hero(stat)) b.add(*c);
    }
    if (u.rush && u.rushUntil.value_or(-1) > state.time) {
        const Rush& r = *u.rush;
        if (stat.is<Stat::Speed>()) b.add(Change::Percent{r.speed});
        if (stat.is<Stat::FireRate>()) b.add(Change::Percent{r.fireRate});
    }
    const KindRecord* rec = nullptr;
    if (hasPlayer(state, Pilot::player)) {
        const auto& record = state.players[static_cast<size_t>(Pilot::player)].pilot;
        if (record) {
            auto it = record->kinds.find(d.kind);
            if (it != record->kinds.end()) rec = &it->second;
        }
    }
    if (!rec) return b;
    if (driven) {
        if (auto on = burst(d.kind); on && rec->burstUntil.value_or(-1) > state.time) {
            if (stat.is<Stat::Speed>()) b.add(Change::Percent{on->second.speed});
            if (stat.is<Stat::FireRate>()) b.add(Change::Percent{on->second.fireRate});
        }
    }
    std::optional<bool> near;
    auto close = [&]() -> bool {
        if (near) return *near;
        const bool n = distance(u.position, d.position) <= Leveling::radius;
        near = n;
        return n;
    };
    for (Perk perk : rec->picks) {
        for (const Effect& e : effects(perk)) {
            if (auto s = e.as<Effect::Stat>(); s && s->stat == stat) {
                bool reaches = false;
                if (s->scope.is<Scope::Driven>()) reaches = driven;
                else if (s->scope.is<Scope::Nearby>()) reaches = u.kind == d.kind && (driven || close());
                else if (s->scope.is<Scope::Every>()) reaches = u.kind == d.kind;
                else if (auto a = s->scope.as<Scope::Around>()) reaches = covers(a->filter, u.kind) && (driven || close());
                else if (s->scope.is<Scope::Team>()) reaches = false;
                if (reaches) b.add(s->change);
            } else if (auto c = e.as<Effect::Cruise>();
                       c && driven && stat.is<Stat::Speed>() &&
                       state.time - rec->healedAt.value_or(-std::numeric_limits<double>::infinity()) > Leveling::healing) {
                b.add(Change::Percent{c->speed});
            }
        }
    }
    return b;
}

/// What the picks do to one of the team's economy stats (`Scope.team`:
/// costs, training, supply) for player `owner` now.
Boost Simulation::teamBoost(const Stat& stat, int64_t owner) const {
    const auto d = pilotUnit();
    if (!(d && d->owner == owner)) return Boost::none;
    Boost b;
    for (Perk perk : levels().picks(d->kind)) {
        for (const Effect& e : effects(perk)) {
            if (auto s = e.as<Effect::Stat>(); s && s->scope.is<Scope::Team>() && s->stat == stat) b.add(s->change);
        }
    }
    return b;
}

/// `boost(stat, of: u).rate`: how fast `u` does it (1: the base rate).
double Simulation::rate(const Stat& stat, const Unit& u) const { return boost(stat, u).rate(); }

/// A load `u` takes from a field (or, `hydrogen`, a Derrick) now.
int64_t Simulation::load(const Unit& u, bool hydrogen) const {
    return boost(hydrogen ? Stat(Stat::HydrogenCarry{}) : Stat(Stat::OreCarry{}), u)
        .apply(hydrogen ? Rules::hydrogenCarry : Rules::carry);
}

/// What player `owner` pays to train a `kind` now.
Price Simulation::unitCost(UnitKind kind, int64_t owner) const {
    const Boost b = teamBoost(Stat::UnitCost{kind}, owner);
    return Price{b.apply(Rules::cost(kind)), b.apply(Rules::hydrogenCost(kind))};
}

/// What player `owner` pays for a `kind` its driven Prospector places now
/// (Prefab); the list price for anyone else.
Price Simulation::buildCost(StructureKind kind, int64_t owner) const {
    const auto d = pilotUnit();
    if (!(d && d->owner == owner && d->kind == UnitKind::prospector)) return Price{Rules::cost(kind), Rules::hydrogenCost(kind)};
    const Boost b = boost(Stat::BuildingCost{}, *d);
    return Price{b.apply(Rules::cost(kind)), b.apply(Rules::hydrogenCost(kind))};
}

/// Supply a Hab Dome of player `owner` gives now (Supply chief).
int64_t Simulation::habDomeSupply(int64_t owner) const {
    return teamBoost(Stat::SupplyPerHabDome{}, owner).apply(Rules::supply(StructureKind::habDome));
}

/// Rangers a Bastion of player `owner` holds now (Bastion drill).
int64_t Simulation::bastionCapacity(int64_t owner) const {
    return teamBoost(Stat::BastionSize{}, owner).apply(Rules::bastionCapacity);
}

/// Cargo slots of Dropship `m` now (Cargo bay).
int64_t Simulation::cargoSlots(const Unit& m) const { return boost(Stat::Cargo{}, m).apply(Rules::dropshipSlots); }

/// What player `owner` pays to research `up` now (Field research).
Price Simulation::upgradeCost(Upgrade up, int64_t owner) const {
    const Boost b = teamBoost(Stat::UpgradeCost{at(up)}, owner);
    return Price{b.apply(ac::ore(up)), b.apply(ac::hydrogen(up))};
}

/// The driven unit `u` has this effect on now.
bool Simulation::drives(const Unit& u, const Effect& effect) const {
    if (!piloted(u.id)) return false;
    for (Perk perk : levels().picks(u.kind)) {
        for (const Effect& e : effects(perk)) {
            if (e == effect) return true;
        }
    }
    return false;
}

// MARK: - The team's economy

/// A kill by `shooter`: its side gets its Bounty share of what the
/// victim cost.
void Simulation::bounty(const Unit& shooter, int64_t ore, int64_t hydrogen) {
    const double share = boost(Stat::Bounty{}, shooter).plus;
    if (!(share > 0 && hasPlayer(state, shooter.owner))) return;
    Player& p = state.players[static_cast<size_t>(shooter.owner)];
    p.ore += static_cast<int64_t>(rounded(static_cast<double>(ore) * share));
    p.hydrogen += static_cast<int64_t>(rounded(static_cast<double>(hydrogen) * share));
}

/// The team lost `u`: it gets its Scrap share of what `u` cost back.
void Simulation::refund(const Unit& u) {
    const double share = teamBoost(Stat::Refund{u.kind}, u.owner).plus;
    if (!(share > 0)) return;
    Player& p = state.players[static_cast<size_t>(u.owner)];
    p.ore += static_cast<int64_t>(rounded(static_cast<double>(Rules::cost(u.kind)) * share));
    p.hydrogen += static_cast<int64_t>(rounded(static_cast<double>(Rules::hydrogenCost(u.kind)) * share));
}

// MARK: - Building blocks

/// The driven unit did `action` once more: true on every third time,
/// when its pick doubles it (Rich vein, Burst fire).
bool Simulation::thirdTime(Action action, const Unit& u) {
    if (!drives(u, Effect::EveryThird{action})) return false;
    int64_t n = 0;
    const UnitKind k = u.kind;
    updateLevels([&](PilotRecord& r) {
        r.kinds[k].count += 1;
        n = r.kinds[k].count;
    });
    return n % 3 == 0;
}

/// The burst pick a kind has made, with its numbers.
std::optional<std::pair<Perk, Burst>> Simulation::burst(UnitKind kind) const {
    for (Perk perk : levels().picks(kind)) {
        for (const Effect& e : effects(perk)) {
            if (auto b = e.as<Effect::Burst>()) return std::make_pair(perk, b->burst);
        }
    }
    return std::nullopt;
}

/// The pick a kind has made that takes the ability key (a burst or a
/// grenade), with its effect.
std::optional<std::pair<Perk, Effect>> Simulation::keyPick(UnitKind kind) const {
    for (Perk perk : levels().picks(kind)) {
        for (const Effect& e : effects(perk)) {
            if (e.is<Effect::Burst>() || e.is<Effect::Grenade>()) return std::make_pair(perk, e);
        }
    }
    return std::nullopt;
}

/// The ability key's pick for the driven unit, if its kind has one:
/// ready, or why not. The title is the pick's ("Surge", "Pulse mine").
std::optional<PilotAbility> Simulation::keyAbility(const Unit& u) const {
    if (!piloted(u.id)) return std::nullopt;
    const auto key = keyPick(u.kind);
    if (!key) return std::nullopt;
    const auto& [perk, e] = *key;
    const auto& kinds = levels().kinds;
    auto it = kinds.find(u.kind);
    const KindRecord* rec = it != kinds.end() ? &it->second : nullptr;
    char why[64];
    if (rec && rec->burstUntil && *rec->burstUntil > state.time) {
        std::snprintf(why, sizeof why, "On for %.0f s", *rec->burstUntil - state.time);
        return PilotAbility(std::nullopt, ac::title(perk), std::string(why));
    }
    if (rec && rec->burstReady && *rec->burstReady > state.time) {
        std::snprintf(why, sizeof why, "Ready in %.0f s", *rec->burstReady - state.time);
        return PilotAbility(std::nullopt, ac::title(perk), std::string(why));
    }
    if (auto b = e.as<Effect::Burst>(); b && b->burst.hpCost > 0 && u.hp <= b->burst.hpCost)
        return PilotAbility(std::nullopt, ac::title(perk), std::string("Not enough hp"));
    if (e.is<Effect::Grenade>()) return PilotAbility(PilotAbility::Action::Grenade{}, ac::title(perk));
    return PilotAbility(PilotAbility::Action::Burst{}, ac::title(perk));
}

/// Pulse mine: the driven unit throws its grenade at the crosshair (what
/// its sight is on, or straight ahead), out to its weapon's reach. It
/// hits every enemy on the ground within the grenade's radius, and the
/// scene shows the blast (`.blast`).
void Simulation::throwGrenade(Unit& u, std::vector<GameEvent>& events) {
    const auto key = keyPick(u.kind);
    if (!key) return;
    const auto* grenade = key->second.as<Effect::Grenade>();
    if (!grenade) return;
    const Grenade g = grenade->grenade;
    const double reach = weapon(u).range + Rules::radius(u.kind);
    Vec2 at = u.position + Vec2(std::cos(u.look()), std::sin(u.look())) * reach;
    int64_t hits = 0;
    if (auto s = pilotSight(u)) {
        if (auto t = target(s->target)) {
            const double d = distance(t->position, u.position);
            if (d <= reach + t->radius) at = t->position;
            else if (d > 1e-6) at = u.position + (t->position - u.position) / d * reach;
        }
    }
    // Swift loops over a copy of the units and the buildings.
    const std::vector<Unit> units = state.units;
    for (const Unit& v : units) {
        if (!(state.hostile(v.owner, u.owner) && v.hp > 0)) continue;
        const auto t = target(v.id);
        if (!(t && !t->air && distance(t->position, at) - t->radius <= g.radius)) continue;
        hit(v.id, g.damage, false, &u);
        hits += 1;
    }
    const std::vector<Structure> structures = state.structures;
    for (const Structure& s : structures) {
        if (!(state.hostile(s.owner, u.owner) && s.hp > 0)) continue;
        if (!(distance(s.position, at) - Rules::radius(s.kind) <= g.radius)) continue;
        hit(s.id, g.damage, false, &u);
        hits += 1;
    }
    tally(u, [&](Tally& t) { t.blastHits += hits; });
    events.push_back(GameEvent::Blast{u.id, at, g.radius});
    const double ready = state.time + g.cooldown;
    const UnitKind k = u.kind;
    updateLevels([&](PilotRecord& r) { r.kinds[k].burstReady = ready; });
}

/// Start the driven unit's burst: it pays the hp, and the clock runs.
void Simulation::startBurst(Unit& u) {
    const auto on = burst(u.kind);
    if (!on) return;
    const Burst b = on->second;
    u.hp -= b.hpCost;
    const double now = state.time;
    const UnitKind k = u.kind;
    updateLevels([&](PilotRecord& r) {
        r.kinds[k].burstUntil = now + b.seconds;
        r.kinds[k].burstReady = now + b.cooldown;
    });
}

/// The driven unit jumps cliffs (a Comet, or Cliff hop).
bool Simulation::pilotJumps(const Unit& u) const { return Pilot::jumps(u.kind) || drives(u, Effect::CliffJump{}); }

/// Field welder: the share of a machine's training time a full repair
/// by the driven Prospector `u` takes; nil without it.
std::optional<double> Simulation::machineRepair(const Unit& u) const {
    if (!piloted(u.id)) return std::nullopt;
    for (Perk perk : levels().picks(u.kind)) {
        for (const Effect& e : effects(perk)) {
            if (auto m = e.as<Effect::MachineRepair>()) return m->share;
        }
    }
    return std::nullopt;
}

/// The driven unit's own upkeep from its picks: its shield refills,
/// and it mends itself, once enough time has passed without a hit.
void Simulation::levelUpkeep(Unit& u, double dt) const {
    std::vector<Effect> mine;
    for (Perk perk : levels().picks(u.kind)) {
        const auto& e = effects(perk);
        mine.insert(mine.end(), e.begin(), e.end());
    }
    const double quiet = state.time - u.hitAt.value_or(-std::numeric_limits<double>::infinity());
    std::optional<double> shield;
    for (const Effect& e : mine) {
        if (auto s = e.as<Effect::Shield>()) {
            double now = min(u.shield.value_or(0), s->shield.hp);
            if (quiet >= s->shield.delay) now = min(s->shield.hp, now + s->shield.hp / s->shield.refill * dt);
            shield = now;
        } else if (auto m = e.as<Effect::SelfMend>(); m && quiet >= m->delay) {
            u.hp = min(maxHP(u), u.hp + m->hpPerSecond * dt);
        }
    }
    u.shield = shield;
}

/// A hit of `damage` on the driven unit at `i`: its shield takes what
/// it can; returns what is left for its hp.
double Simulation::shieldTakes(int64_t i, double damage) {
    Unit& u = state.units[static_cast<size_t>(i)];
    if (!(piloted(u.id) && u.shield && *u.shield > 0)) return damage;
    const double s = *u.shield;
    const double taken = min(s, damage);
    u.shield = s - taken;
    return damage - taken;
}

/// Once a step while someone drives, or while anything burns: the
/// seconds driven, every unit's hp kept in line with its hp picks,
/// units on fire burn (to the end, driven or not), then the mending
/// auras around the driven unit.
void Simulation::stepLeveling(double dt) {
    if (auto d = pilotUnit(); d && d->hp > 0) {
        // Buried and unseen by every enemy side.
        bool hiding = d->burrowed();
        for (int64_t p = 0; hiding && p < static_cast<int64_t>(state.players.size()); p++) {
            if (state.hostile(p, d->owner) && sees(p, d->id)) hiding = false;
        }
        tally(*d, [&](Tally& t) { t.seconds += dt; if (hiding) t.hiddenSeconds += dt; });
    }
    syncBonusHP();
    bool burns = false;
    for (size_t i = 0; i < state.units.size(); i++) {
        if (!state.units[i].burning) continue;
        const Burning f = *state.units[i].burning;
        if (!(state.time < f.until && state.units[i].hp > 0)) {
            state.units[i].burning = std::nullopt;
            continue;
        }
        std::optional<Unit> by;
        if (auto bi = unitIndex(f.by)) by = state.units[static_cast<size_t>(*bi)];
        const double hp = state.units[i].hp;
        hit(state.units[i].id, f.perSecond * dt, false, by ? &*by : nullptr);
        if (by) {
            const double burnt = hp - max(0.0, state.units[i].hp);
            tally(*by, [&](Tally& t) { t.burn += burnt; });
        }
        burns = true;
    }
    burnsLeft = burns;
    if (auto de = drivenEffects()) {
        const Unit& d = de->first;
        for (const Effect& e : de->second) {
            const auto* aura = e.as<Effect::Aura>();
            if (!aura) continue;
            const Aura& a = aura->aura;
            for (size_t i = 0; i < state.units.size(); i++) {
                const Unit v = state.units[i];
                if (!(v.owner == d.owner && v.hp > 0 && covers(a.filter, v.kind) && v.task != Unit::Task::aboard &&
                      v.task != Unit::Task::inBastion && v.task != Unit::Task::inDerrick &&
                      distance(v.position, d.position) <= a.radius))
                    continue;
                state.units[i].hp = min(maxHP(v), v.hp + a.hpPerSecond * dt);
            }
        }
    }
}

/// An hp pick coming on raises a unit's hp by as much (as the Aegis
/// shield does); going off, it lowers its hp only to the new full.
void Simulation::syncBonusHP() {
    if (!(pilot || hpBonusHeld)) return;
    bool held = false;
    for (size_t i = 0; i < state.units.size(); i++) {
        const double want = boost(Stat::Hp{}, state.units[i]).plus, had = state.units[i].bonusHP.value_or(0);
        if (want > 0) held = true;
        if (!(want != had)) continue;
        if (want > had) state.units[i].hp += want - had;
        else state.units[i].hp = min(state.units[i].hp, maxHP(state.units[i]));
        state.units[i].bonusHP = want > 0 ? std::optional<double>(want) : std::nullopt;
    }
    hpBonusHeld = held;
}

} // namespace ac
