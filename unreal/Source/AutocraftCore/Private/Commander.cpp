// Port of Sources/GameCore/Commander.swift.
#include "Commander.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ac {

namespace {

template <class T, class F>
std::vector<T> filtered(const std::vector<T>& v, F f) {
    std::vector<T> out;
    for (const T& x : v) if (f(x)) out.push_back(x);
    return out;
}

template <class T, class F>
bool any(const std::vector<T>& v, F f) {
    for (const T& x : v) if (f(x)) return true;
    return false;
}

template <class T, class F>
int64_t countIf(const std::vector<T>& v, F f) {
    int64_t n = 0;
    for (const T& x : v) if (f(x)) n += 1;
    return n;
}

/// Swift's `min(by:)`: the first of the least.
template <class T, class Less>
std::optional<T> minBy(const std::vector<T>& v, Less less) {
    std::optional<T> best;
    for (const T& x : v) if (!best || less(x, *best)) best = x;
    return best;
}

template <class T>
std::vector<T> operator+(std::vector<T> a, const std::vector<T>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

template <class T>
std::vector<T> prefix(const std::vector<T>& v, int64_t n) {
    if (n < 0) n = 0;
    return std::vector<T>(v.begin(), v.begin() + ac::min<int64_t>(n, static_cast<int64_t>(v.size())));
}

template <class T, class Less>
std::vector<T> sorted(std::vector<T> v, Less less) {
    std::stable_sort(v.begin(), v.end(), less);
    return v;
}

/// `xs.map(\.position).reduce(Vec2.zero, +) / Double(xs.count)`.
template <class T>
Vec2 centreOf(const std::vector<T>& xs) {
    Vec2 sum = Vec2::zero;
    for (const T& x : xs) sum = sum + x.position;
    return sum / static_cast<double>(xs.size());
}

std::vector<Vec2> positions(const std::vector<Structure>& v) {
    std::vector<Vec2> out;
    for (const Structure& x : v) out.push_back(x.position);
    return out;
}

/// `n[k] ?? 0`.
int64_t lookup(const std::map<int64_t, int64_t>& n, int64_t k) {
    auto it = n.find(k);
    return it == n.end() ? 0 : it->second;
}

bool isRaid(const std::optional<Mission>& m) { return m && std::holds_alternative<Mission::Raid>(m->value); }
bool isHunt(const std::optional<Mission>& m) { return m && std::holds_alternative<Mission::Hunt>(m->value); }

/// The commands and missions it gives (Swift's enum cases).
namespace make {
Command attack(int64_t player, std::optional<Vec2> at) { return Command::Attack{player, at}; }
Command defend(int64_t player, Vec2 at, int64_t group) { return Command::Defend{player, at, group}; }
Command retreat(int64_t player) { return Command::Retreat{player}; }
Command mission(int64_t unit, std::optional<Mission> m) { return Command::Mission{unit, std::move(m)}; }
Command board(int64_t dropship, std::optional<int64_t> unit) { return Command::Board{dropship, unit}; }
Command load(int64_t unit, int64_t bastion) { return Command::Load{unit, bastion}; }
Command repair(int64_t worker, int64_t structure) { return Command::Repair{worker, structure}; }
Command resume(int64_t worker, int64_t structure) { return Command::Resume{worker, structure}; }
Command build(int64_t worker, Structure::Kind kind, Vec2 at) { return Command::Build{worker, kind, at}; }
Command train(int64_t structure, std::optional<Unit::Kind> kind) { return Command::Train{structure, kind}; }
Command gather(int64_t worker, int64_t patch) { return Command::Gather{worker, patch}; }
Command harvest(int64_t worker, int64_t derrick) { return Command::Harvest{worker, derrick}; }
Command addon(int64_t structure) { return Command::Addon{structure}; }
Command research(int64_t structure, Upgrade up) { return Command::Research{structure, up}; }
Mission raid(Vec2 at) { return Mission::Raid{at}; }
Mission fallBack(Vec2 at) { return Mission::FallBack{at}; }
Mission drop(Vec2 at) { return Mission::Drop{at}; }
Mission hunt(Vec2 at) { return Mission::Hunt{at}; }
Mission follow(int64_t unit) { return Mission::Follow{unit}; }
} // namespace make

} // namespace

Commander::Commander(const MapDefinition& map_, int64_t player_, int64_t workersPerPatch_)
    : map(map_), field(map_), router(map_), player(player_), workersPerPatch(workersPerPatch_) {
    for (Unit::Kind k : allCases<Unit::Kind>()) unlocked.insert(k);
}

std::vector<Commander> Commander::all(const MapDefinition& map_, int64_t workersPerPatch_) {
    std::vector<Commander> out;
    for (size_t i = 0; i < map_.starts.size(); i++) out.emplace_back(map_, static_cast<int64_t>(i), workersPerPatch_);
    return out;
}

std::vector<Command> Commander::orders(const Simulation& whole) const {
    // (C++: a map's start with no player on it, in a team game for fewer.)
    if (!(player >= 0 && player < static_cast<int64_t>(whole.state.players.size()))) return {};
    // It knows only what its side sees and has seen (the fog of war).
    const Simulation sim = whole.seen(player);
    if (!(player >= 0 && player < static_cast<int64_t>(sim.state.players.size()))) return {};
    // The Prospector the player drives is not the AI's to send anywhere.
    std::set<int64_t> busy;
    if (sim.pilot) busy.insert(sim.pilot->unit);
    // Prospectors scouting or pulled to fight are on their errand.
    const std::vector<Command> early = scouts(sim, busy) + militia(sim, busy);
    for (const Unit& u : sim.state.units) if (u.owner == player && u.task == Unit::Task::errand) busy.insert(u.id);
    for (const Command& c : early) {
        if (auto m = std::get_if<Command::Mission>(&c.value); m && m->mission) busy.insert(m->unit);
    }
    const std::vector<Command> hold = crew(sim, busy);
    std::set<int64_t> loading;
    for (const Command& c : hold) {
        if (auto l = std::get_if<Command::Load>(&c.value)) loading.insert(l->unit);
    }
    // Units posted this second are not loaded or sent anywhere else.
    const std::vector<Command> posts = stations(sim);
    for (const Command& c : posts) {
        if (auto m = std::get_if<Command::Mission>(&c.value); m && m->mission) loading.insert(m->unit);
    }
    return army(sim) + squads(sim) + raiders(sim) + posts + drops(sim, loading) + hunters(sim) + hold + early
        + economy(sim, busy);
}

// MARK: - Army

/// Army play, all with the whole army:
/// - defend: enemies near its buildings pull the army home to fight;
/// - attack in waves: once the army is big enough (a first wave of
///   8–16 supply by temperament, four more each wave) and not
///   outnumbered, attack-move to the enemy building nearest to it;
/// - retreat when the fight near the army turns against it, and hold
///   at the rally until most of the army is back;
/// - after a win, hunt down what is left.
///
/// The team's `Stance` bends it: aggressive plays as if more aggressive;
/// hold never attacks on its own (nor with an attack objective given:
/// the squads attack) and calls an attack under way back to the rally;
/// all in attacks at once with what it has, never
/// falls back and does not turn home for an attack on a base.
std::vector<Command> Commander::army(const Simulation& sim) const {
    const GameState& s = sim.state;
    Player me = s.players[player];
    const Stance stance = me.orders().stance;
    if (stance == Stance::aggressive) me.aggression = ac::min(1.0, me.aggression + 0.4);
    const bool ownWaves = stance != Stance::hold
        && !any(me.orders().objectives, [](const Objective& o) { return o.kind == Objective::Kind::attack; });
    // The field army: soldiers with no errand of their own (Rangers in
    // or bound for a Bastion stay there).
    const auto rangers = filtered(s.units, [&](const Unit& u) { return u.owner == player && sim.inArmy(u); });
    const auto enemies = filtered(s.units, [&](const Unit& u) { return s.hostile(u.owner, player) && u.task != Unit::Task::aboard; });
    const auto theirs = filtered(s.structures, [&](const Structure& b) { return s.hostile(b.owner, player); });
    const auto mine = filtered(s.structures, [&](const Structure& b) { return b.owner == player; });
    if (rangers.empty()) {
        if (me.attack || me.retreating) return {make::attack(player, std::nullopt)};
        return {};
    }
    // The front: where the Rangers out fighting are (all of them when
    // none are).
    const auto out = filtered(rangers, [](const Unit& u) {
        return u.task == Unit::Task::attackMove || u.task == Unit::Task::attacking;
    });
    const auto& group = out.empty() ? rangers : out;
    const Vec2 front = centreOf(group);
    auto attack = [&](Vec2 p) -> std::vector<Command> {
        if (me.attack && distance(*me.attack, p) < 3 && !me.retreating) return {};
        return {make::attack(player, p)};
    };

    if (s.winner && s.allied(*s.winner, player)) {
        auto prey = minBy(enemies, [&](const Unit& a, const Unit& b) {
            return distance(a.position, front) < distance(b.position, front);
        });
        if (!prey) return {};
        return attack(prey->position);
    }

    // Defend.
    const Vec2 home = start(s);
    const auto intruders = filtered(enemies, [&](const Unit& e) {
        return e.soldier() && any(mine, [&](const Structure& b) {
            return distance(b.position, e.position) < Rules::radius(b.kind) + 12;
        });
    });
    // A few raiders are the hunters' job (see `hunters`); the army
    // turns home only for a real attack.
    const bool raid = strength(intruders) <= raidSize && strength(rangers) >= 2 * strength(intruders);
    if (!raid && stance != Stance::allIn) {
        if (auto first = minBy(intruders, [&](const Unit& a, const Unit& b) {
                return distance(a.position, home) < distance(b.position, home);
            })) {
            // Reinforcements go in as they come while the whole army is the
            // stronger; otherwise they gather until they match most of it.
            const double threat = strength(intruders);
            const int64_t size = strength(rangers) >= threat
                ? 1 : ac::max<int64_t>(2, static_cast<int64_t>(std::ceil(threat * 0.8)));
            if (me.attack && distance(*me.attack, first->position) < 3 && !me.retreating && me.reinforce == size) return {};
            return {make::defend(player, first->position, size)};
        }
    }
    // Teams (C++ only): an ally's base under a real attack (more than a
    // raid) is defended too, by an army of at least `allyJoin` that has no
    // attack of its own under way, not on hold or all in. It goes home
    // with the rest once that base is clear (below).
    if (stance != Stance::hold && stance != Stance::allIn && !me.retreating && (!me.attack || me.defending == true)
        && armySupply(rangers) >= allyJoin) {
        const auto theirBases = filtered(s.structures, [&](const Structure& b) {
            return b.owner != player && s.allied(b.owner, player);
        });
        const auto raiders = theirBases.empty() ? std::vector<Unit>{} : filtered(enemies, [&](const Unit& e) {
            return e.soldier() && any(theirBases, [&](const Structure& b) {
                return distance(b.position, e.position) < Rules::radius(b.kind) + 12;
            });
        });
        const double threat = strength(raiders);
        if (threat > raidSize) {
            if (auto first = minBy(raiders, [&](const Unit& a, const Unit& b) {
                    return distance(a.position, home) < distance(b.position, home);
                })) {
                const int64_t size = strength(rangers) >= threat
                    ? 1 : ac::max<int64_t>(2, static_cast<int64_t>(std::ceil(threat * 0.8)));
                if (me.attack && distance(*me.attack, first->position) < 3 && me.reinforce == size) return {};
                return {make::defend(player, first->position, size)};
            }
        }
    }
    // Base clear again: the defenders go home.
    if (me.defending == true) return {make::attack(player, std::nullopt)};

    const auto enemyArmy = filtered(enemies, [](const Unit& u) { return u.soldier() && !u.mission; });
    const int64_t all = armySupply(filtered(s.units, [&](const Unit& u) { return u.soldier() && u.owner == player; }));
    const int64_t cap = armyCap(s);
    const bool maxed = all >= cap - 4;
    // The fight around the army: its soldiers near the front, and the
    // enemy's.
    const double ours = strength(filtered(rangers, [&](const Unit& u) { return distance(u.position, front) < 10; }));
    const auto near = filtered(enemyArmy, [&](const Unit& u) { return distance(u.position, front) < 12; });
    const double foes = strength(near);
    // Enemies holding higher ground (up a ramp, on a cliff) outrange
    // and outsee the army coming up: fought uphill they count
    // `uphill` times, so it waits below rather than climb into them.
    const int64_t level = field.level(front);
    const double held = foes + (uphill - 1) * strength(filtered(near, [&](const Unit& u) { return field.level(u.position) > level; }));

    if (me.attack) {
        const Vec2 a = *me.attack;
        // Hold: an attack under way when the stance came is called off,
        // and the army falls back to the rally.
        if (stance == Stance::hold) return {make::retreat(player)};
        // Fall back when the fight around the army goes badly (worse
        // uphill).
        if (stance != Stance::allIn && held > ours * (1.25 + 0.6 * me.aggression)) {
            return {make::retreat(player)};
        }
        // Strung out on the way with no fight on: gather first, on the
        // soldier at the middle of the army (`regroup`). Only those
        // out count (reinforcements at the rally cross on their own).
        // A point with no building of theirs near it is a gathering
        // point already: no new one until the army is in.
        const bool fighting = any(group, [](const Unit& u) { return u.task == Unit::Task::attacking; });
        const bool inBase = any(theirs, [&](const Structure& b) { return distance(b.position, a) < guardRadius; });
        // All in never gathers: the gather point is behind the front
        // runners, so each gathering sent them back (two steps on, two
        // steps back).
        if (foes == 0 && !fighting && inBase && stance != Stance::allIn) {
            if (auto gather = regroup(out, front, a)) return attack(*gather);
        }
        // Point cleared (no building and no enemy soldiers left at it):
        // on to the next building in the base, or, out in the open (a
        // gathering point), to the best base (`attackTarget`) once most
        // of the army is there.
        const bool standing = any(theirs, [&](const Structure& b) { return distance(b.position, a) < Rules::radius(b.kind) + 1; })
            || any(enemyArmy, [&](const Unit& u) { return distance(u.position, a) < 8; });
        if (!standing) {
            if (!inBase && foes == 0 && !fighting && !gathered(out, a)) return {};
            std::optional<Vec2> next;
            if (inBase) {
                auto n = nearest(theirs, front);
                next = n ? std::optional<Vec2>(n->position) : scout(sim, front);
            } else {
                next = attackTarget(sim, front);
            }
            if (next) return attack(*next);
        }
        return {};
    }
    if (me.retreating) {
        // The fight turned: the enemies near it are now clearly the
        // weaker (a few pursuers, a straggler): turn on them (not on
        // hold, which would call the turn off again).
        if (stance != Stance::hold && foes > 0 && foes * turnOdds < ours) {
            if (auto prey = minBy(near, [&](const Unit& x, const Unit& y) {
                    return distance(x.position, front) < distance(y.position, front);
                })) {
                return {make::attack(player, prey->position)};
            }
        }
        // Over once most of the army is back and no pursuers worth
        // turning from are on it. Some never make it back (cut off, or
        // held up), so it is also over after `retreatTime`, or as soon
        // as the army is maxed out: a retreat must never keep the army
        // home for good.
        const auto back = filtered(rangers, [&](const Unit& u) {
            return u.task == Unit::Task::idle || (u.task == Unit::Task::toRally && distance(u.position, front) < 6);
        });
        const bool chased = foes > 0;
        const bool longTime = s.time - me.retreatedAt.value_or(-std::numeric_limits<double>::infinity()) >= retreatTime;
        const bool over = static_cast<double>(back.size()) >= 0.8 * static_cast<double>(rangers.size()) || longTime || maxed;
        if (over && !chased) return {make::attack(player, std::nullopt)};
        return {};
    }

    // Teams (C++ only): an ally's army on the attack is joined at its point
    // by an army big enough, so allies fight as one.
    if (ownWaves && armySupply(rangers) >= allyJoin) {
        for (int64_t i = 0; i < static_cast<int64_t>(s.players.size()); i++) {
            if (i == player || !s.allied(i, player)) continue;
            const Player& ally = s.players[static_cast<size_t>(i)];
            if (ally.attack && !ally.retreating && ally.defending != true) return attack(*ally.attack);
        }
    }

    // Attack when the wave is ready and the odds are good enough, at
    // the base it can hurt most for the least (`attackTarget`).
    // A maxed-out army goes regardless, and so does one that far
    // outweighs what the enemy has left (finish them). Rangers held in
    // Bastions count toward the cap but not toward the wave.
    const int64_t wave = ac::min(static_cast<int64_t>(16 - 8 * me.aggression) + 4 * me.waves,
                                  cap - 4 - (all - armySupply(rangers)));
    const double odds = strength(rangers) / ac::max(strength(enemyArmy), 1.0);
    const bool finish = strength(rangers) >= finishForce && odds >= finishOdds;
    const bool ready = ownWaves && ((armySupply(rangers) >= wave && odds >= 1.3 - 0.5 * me.aggression) || maxed || finish
                                    || stance == Stance::allIn);
    if (ready) {
        if (auto target = attackTarget(sim, front)) return attack(*target);
    }
    return {};
}

/// With no enemy building known, where the army looks for one: the
/// base site its side has gone longest without seeing (never seen
/// first, the nearest of those), none of its own.
std::optional<Vec2> Commander::scout(const Simulation& sim, Vec2 from) const {
    const GameState& s = sim.state;
    std::set<int64_t> mine;
    for (const Structure& b : s.structures) {
        if (b.owner == player && b.kind == Structure::Kind::citadel) mine.insert(sim.site(b));
    }
    std::vector<int64_t> free;
    for (int64_t i = 0; i < static_cast<int64_t>(sim.sites.size()); i++) if (!mine.count(i)) free.push_back(i);
    auto best = minBy(free, [&](int64_t a, int64_t b) {
        const double ta = sim.lastSeen(a, player), tb = sim.lastSeen(b, player);
        if (ta != tb) return ta < tb;
        return router.distance(from, sim.sites[a]) < router.distance(from, sim.sites[b]);
    });
    if (!best) return std::nullopt;
    return sim.sites[*best];
}

/// Fighting worth of a group, in Rangers: each unit's share of its hit
/// points times what it costs (a Ranger 1, a tank 5.5).
double Commander::strength(const std::vector<Unit>& us) {
    double sum = 0;
    for (const Unit& u : us) sum = sum + u.hp / u.stats().hp * static_cast<double>(u.stats().ore + u.stats().hydrogen) / 50;
    return sum;
}

/// Army supply it keeps: what the supply cap (200) leaves
/// over its Prospectors, those in training included, up to `maxArmy`.
int64_t Commander::armyCap(const GameState& s) const {
    int64_t prospectors = countIf(s.units, [&](const Unit& u) { return u.owner == player && u.kind == Unit::Kind::prospector; });
    int64_t queuedProspectors = 0;
    for (const Structure& b : s.structures) {
        if (b.owner == player && b.kind == Structure::Kind::citadel) queuedProspectors += b.queueCount();
    }
    prospectors += queuedProspectors;
    return ac::min(maxArmy, Rules::maxSupply - prospectors);
}

/// Supply taken by a group.
int64_t Commander::armySupply(const std::vector<Unit>& us) {
    int64_t n = 0;
    for (const Unit& u : us) n += Rules::supply(u.kind);
    return n;
}

/// The units in a building's production queue, the one in training first.
std::vector<Unit::Kind> Commander::queued(const Structure& b) {
    if (b.line) return *b.line;
    if (auto k = b.inTraining()) return {*k};
    return {};
}

// MARK: - Raids

/// Raiders go for the enemy ore line with the most workers and the
/// fewest guards, and pull out when hurt or when soldiers close in:
/// - Comets (cliffs are no obstacle to them) go again once healed;
/// - Fireflies run in as a pack of up to `Player.fireflies`, once two (or
///   all it keeps) stand ready at the rally, and come home when the line
///   is empty or guarded. They do not heal: home again, they rejoin the
///   army, and only the ones still in good shape run in again;
/// - Kestrels fly in the same way at the line with the least anti-air.
/// On hold none go, and those out come home.
std::vector<Command> Commander::raiders(const Simulation& sim) const {
    const GameState& s = sim.state;
    const Player& me = s.players[player];
    const Vec2 home = start(s);
    const bool holding = me.orders().stance == Stance::hold;
    const auto mine = filtered(s.units, [&](const Unit& u) { return u.owner == player; });
    std::vector<Command> out;
    auto raiding = [&](Unit::Kind kind) {
        return filtered(mine, [&](const Unit& u) { return u.kind == kind && isRaid(u.mission); });
    };
    /// Enemy soldiers or a manned Bastion close by, more than the raiders
    /// around `r` can take on.
    auto threatened = [&](const Unit& r, const std::vector<Unit>& pack) {
        const bool air = r.stats().air;
        const auto danger = filtered(s.units, [&](const Unit& u) {
            return s.hostile(u.owner, player) && u.soldier() && (air ? u.stats().hitsAir : u.stats().hitsGround)
                && u.task != Unit::Task::aboard && distance(u.position, r.position) < 8;
        });
        const bool bastion = any(s.structures, [&](const Structure& b) {
            return s.hostile(b.owner, player) && distance(b.position, r.position) < 9
                && (!(b.crew.value_or(std::vector<int64_t>{})).empty() || (air && b.kind == Structure::Kind::sentinel));
        });
        const auto near = filtered(pack, [&](const Unit& u) { return u.id != r.id && distance(u.position, r.position) < 6; })
            + std::vector<Unit>{r};
        return bastion || strength(danger) > 1.6 * strength(near);
    };

    const auto comets = filtered(mine, [](const Unit& u) { return u.kind == Unit::Kind::comet; });
    if (!comets.empty()) {
        const auto pack = raiding(Unit::Kind::comet);
        const std::optional<Vec2> target = holding ? std::nullopt : raidTarget(s, sim, strength(comets));
        for (const Unit& r : comets) {
            const double health = r.hp / r.stats().hp;
            const Mission::Raid* raid = r.mission ? std::get_if<Mission::Raid>(&r.mission->value) : nullptr;
            const bool fallingBack = r.mission && std::holds_alternative<Mission::FallBack>(r.mission->value);
            if (fallingBack) {
                if (health > 0.95 && target) out.push_back(make::mission(r.id, make::raid(*target)));
            } else if (raid) {
                if (health < 0.45 || threatened(r, pack) || holding) {
                    out.push_back(make::mission(r.id, make::fallBack(home)));
                } else if (target && distance(*target, raid->at) > 3) {
                    out.push_back(make::mission(r.id, make::raid(*target)));
                }
            } else {
                if (health > 0.9 && target) out.push_back(make::mission(r.id, make::raid(*target)));
                else if (health <= 0.9) out.push_back(make::mission(r.id, make::fallBack(home)));
            }
        }
    }

    // Kestrels: the fresh ones fly in together (two at least, or all
    // it has) at the ore line with the fewest anti-air guards, and come
    // home hurt, when anti-air closes in, or when the line is no
    // longer worth it; home again they rejoin the army.
    const auto kestrels = filtered(mine, [](const Unit& u) { return u.kind == Unit::Kind::kestrel; });
    if (!kestrels.empty()) {
        const auto pack = raiding(Unit::Kind::kestrel);
        const auto fresh = filtered(kestrels, [&](const Unit& u) {
            return sim.inArmy(u) && u.hp / u.stats().hp >= 0.7 && (u.task == Unit::Task::idle || u.task == Unit::Task::toRally);
        });
        const std::optional<Vec2> target = holding ? std::nullopt : raidTarget(s, sim, strength(pack + fresh), true);
        for (const Unit& k : kestrels) {
            if (!k.mission) continue;
            if (auto raid = std::get_if<Mission::Raid>(&k.mission->value)) {
                if (k.hp / k.stats().hp < 0.45 || !target || threatened(k, pack)) {
                    out.push_back(make::mission(k.id, make::fallBack(home)));
                } else if (target && distance(*target, raid->at) > 3) {
                    out.push_back(make::mission(k.id, make::raid(*target)));
                }
            } else if (auto back = std::get_if<Mission::FallBack>(&k.mission->value)) {
                if (distance(k.position, back->at) < 6) out.push_back(make::mission(k.id, std::nullopt));
            }
        }
        if (target && me.defending != true && !fresh.empty()
            && (!pack.empty() || static_cast<int64_t>(fresh.size()) >= ac::min<int64_t>(static_cast<int64_t>(kestrels.size()), 2))) {
            for (const Unit& u : fresh) out.push_back(make::mission(u.id, make::raid(*target)));
        }
    }

    const auto fireflies = filtered(mine, [](const Unit& u) { return u.kind == Unit::Kind::firefly; });
    const int64_t keep = me.fireflies.value_or(0);
    if (!(keep > 0 && !fireflies.empty())) return out;
    const auto pack = raiding(Unit::Kind::firefly);
    const auto fresh = sorted(filtered(fireflies, [&](const Unit& u) {
        return sim.inArmy(u) && u.hp / u.stats().hp >= 0.7 && (u.task == Unit::Task::idle || u.task == Unit::Task::toRally);
    }), [](const Unit& a, const Unit& b) { return a.id < b.id; });
    const auto joining = prefix(fresh, ac::max<int64_t>(0, keep - static_cast<int64_t>(pack.size())));
    const std::optional<Vec2> target = holding ? std::nullopt : raidTarget(s, sim, strength(pack + joining));
    for (const Unit& h : fireflies) {
        if (!h.mission) continue;
        if (auto raid = std::get_if<Mission::Raid>(&h.mission->value)) {
            if (h.hp / h.stats().hp < 0.4 || !target || threatened(h, pack)) {
                out.push_back(make::mission(h.id, make::fallBack(home)));
            } else if (target && distance(*target, raid->at) > 3) {
                out.push_back(make::mission(h.id, make::raid(*target)));
            }
        } else if (auto back = std::get_if<Mission::FallBack>(&h.mission->value)) {
            if (distance(h.position, back->at) < 6) out.push_back(make::mission(h.id, std::nullopt));
        }
    }
    // A runby: fresh Fireflies from the rally join the pack out there, or
    // set off together once enough stand ready.
    if (target && me.defending != true && !joining.empty()
        && (!pack.empty() || static_cast<int64_t>(joining.size()) >= ac::min<int64_t>(keep, 2))) {
        for (const Unit& u : joining) out.push_back(make::mission(u.id, make::raid(*target)));
    }
    return out;
}

/// The enemy ore line most worth raiding: most Prospectors, fewest soldiers
/// and no manned Bastion; nil when none is safe enough for raiders worth
/// `force` (in Rangers). For fliers (`air`) the guards are what shoots
/// air, and a Sentinel near the line rules it out.
std::optional<Vec2> Commander::raidTarget(const GameState& s, const Simulation& sim, double force, bool air) const {
    std::optional<std::pair<Vec2, double>> best;
    for (const Structure& citadel : s.structures) {
        if (!(s.hostile(citadel.owner, player) && citadel.kind == Structure::Kind::citadel && citadel.complete())) continue;
        const int64_t site = sim.site(citadel);
        std::vector<Vec2> line;
        for (size_t i = 0; i < s.patches.size(); i++) {
            if (sim.patchBase[i] == site && s.patches[i].remaining > 0) line.push_back(s.patches[i].position);
        }
        if (line.empty()) continue;
        Vec2 sum = Vec2::zero;
        for (Vec2 p : line) sum = sum + p;
        const Vec2 mean = sum / static_cast<double>(line.size());
        const Vec2 at = citadel.position + (mean - citadel.position) * 0.75;
        if (any(s.structures, [&](const Structure& b) {
                return s.hostile(b.owner, player) && distance(b.position, at) < 10
                    && (!(b.crew.value_or(std::vector<int64_t>{})).empty() || (air && b.kind == Structure::Kind::sentinel));
            })) {
            continue;
        }
        int64_t workers = countIf(s.units, [&](const Unit& u) {
            return s.hostile(u.owner, player) && u.kind == Unit::Kind::prospector && distance(u.position, at) < 9;
        });
        // A line its side has not looked at lately: worth a look, as if
        // it were worked.
        if (workers == 0 && s.time - sim.lastSeen(site, player) > lineUnseen) workers = 6;
        const double guards = strength(filtered(s.units, [&](const Unit& u) {
            return s.hostile(u.owner, player) && u.soldier() && (air ? u.stats().hitsAir : u.stats().hitsGround)
                && distance(u.position, at) < 12;
        }));
        if (!(workers > 0 && guards < ac::max(2.0, 0.6 * force))) continue;
        const double score = static_cast<double>(workers) - 3 * guards - 0.001 * distance(at, map.front());
        if (!best || score > best->second) best = std::make_pair(at, score);
    }
    if (!best) return std::nullopt;
    return best->first;
}

/// Scorpions and Peregrines stand where they are put, with an order on the
/// spot (so they stay out of the army, which they would only march
/// with in the way: a Peregrine reaches the point first and alone, and a
/// Scorpion cannot move once buried):
/// - Scorpions bury two to each finished base's ore line, either side of
///   the line's middle, where raiders strike (an enemy that comes near
///   is stung; a Sentinel or a unit within 2 cells finds them), and one at
///   each choke the AI holds (`chokes`);
/// - Peregrines escort the AI's own Dropships and Kestrels, each on a
///   follow order (`Mission::Follow`): the ones out on a raid or a drop
///   first, else any, a few to each. With nothing to escort they stay over
///   the main. They never join a ground push.
/// Neither needs more than an order a second.
std::vector<Command> Commander::stations(const Simulation& sim) const {
    const GameState& s = sim.state;
    const Vec2 home = start(s);
    std::vector<Command> out;
    const auto mine = filtered(s.units, [&](const Unit& u) {
        return u.owner == player && !sim.piloted(u.id) && u.hp > 0 && u.task != Unit::Task::aboard;
    });

    // Peregrines: each escorts a Dropship or a Kestrel, else holds over home.
    const auto peregrines = sorted(filtered(mine, [](const Unit& u) { return u.kind == Unit::Kind::peregrine; }),
                                   [](const Unit& a, const Unit& b) { return a.id < b.id; });
    if (!peregrines.empty()) {
        const auto charges = sorted(filtered(mine, [](const Unit& u) {
            return u.kind == Unit::Kind::kestrel || u.kind == Unit::Kind::dropship;
        }), [](const Unit& a, const Unit& b) { return a.id < b.id; });
        // The ones out on an errand are escorted first.
        const auto out_ = filtered(charges, [](const Unit& u) {
            return u.mission && (u.mission->is<Mission::Raid>() || u.mission->is<Mission::Drop>());
        });
        const auto& pool = out_.empty() ? charges : out_;
        std::map<int64_t, int64_t> followers;
        for (const Unit& c : pool) followers[c.id] = 0;
        std::vector<const Unit*> loose;
        for (const Unit& p : peregrines) {
            const Mission::Follow* f = p.mission ? std::get_if<Mission::Follow>(&p.mission->value) : nullptr;
            if (f && followers.count(f->unit)) followers[f->unit] += 1; // keeps its charge
            else loose.push_back(&p);
        }
        for (const Unit* p : loose) {
            if (pool.empty()) {
                const Mission::Raid* raid = p->mission ? std::get_if<Mission::Raid>(&p->mission->value) : nullptr;
                if (!raid || distance(raid->at, home) > 3) out.push_back(make::mission(p->id, make::raid(home)));
                continue;
            }
            const Unit* best = &pool.front();
            for (const Unit& c : pool) if (followers[c.id] < followers[best->id]) best = &c;
            followers[best->id] += 1;
            out.push_back(make::mission(p->id, make::follow(best->id)));
        }
    }

    // Scorpions: two buried at each ore line, one at each choke.
    const auto scorpions = filtered(mine, [](const Unit& u) { return u.kind == Unit::Kind::scorpion; });
    if (scorpions.empty()) return out;
    std::vector<Vec2> spots;
    for (const Structure& citadel : s.structures) {
        if (!(citadel.owner == player && citadel.kind == Structure::Kind::citadel && citadel.complete())) continue;
        const int64_t site = sim.site(citadel);
        std::vector<Vec2> line;
        for (size_t i = 0; i < s.patches.size(); i++) {
            if (sim.patchBase[i] == site && s.patches[i].remaining > 0) line.push_back(s.patches[i].position);
        }
        if (line.empty()) continue;
        Vec2 sum = Vec2::zero;
        for (Vec2 p : line) sum = sum + p;
        const Vec2 mean = sum / static_cast<double>(line.size());
        const Vec2 at = citadel.position + (mean - citadel.position) * 0.75;
        const Vec2 along = normalize(mean - citadel.position);
        const Vec2 across(-along.y, along.x);
        // Either side of the line, on the first open ground along it.
        for (const double side : {2.5, -2.5}) {
            for (const double shift : {0.0, -1.5, 1.5, -3.0, 3.0}) {
                const Vec2 p = at + across * side + along * shift;
                if (sim.nav && !(sim.nav->walkable(p) && !sim.nav->inRock(p))) continue;
                spots.push_back(p);
                break;
            }
        }
    }
    for (const Vec2 p : chokes(sim)) spots.push_back(p);
    std::set<int64_t> taken;
    for (const Unit& u : scorpions) {
        const Mission::Raid* raid = u.mission ? std::get_if<Mission::Raid>(&u.mission->value) : nullptr;
        if (!raid) continue;
        // Already posted at one of the spots (or at an old spot that is
        // still within reach of it): keep it.
        for (size_t k = 0; k < spots.size(); k++) {
            if (!taken.count(static_cast<int64_t>(k)) && distance(spots[k], raid->at) < 3) { taken.insert(static_cast<int64_t>(k)); break; }
        }
    }
    for (const Unit& u : scorpions) {
        if (u.mission || !sim.inArmy(u)) continue;
        std::optional<size_t> best;
        for (size_t k = 0; k < spots.size(); k++) {
            if (taken.count(static_cast<int64_t>(k))) continue;
            if (!best || distance(spots[k], u.position) < distance(spots[*best], u.position)) best = k;
        }
        if (!best) break;
        taken.insert(static_cast<int64_t>(*best));
        out.push_back(make::mission(u.id, make::raid(spots[*best])));
    }
    return out;
}

std::vector<Vec2> Commander::chokes(const Simulation& sim) const {
    const GameState& s = sim.state;
    std::vector<Vec2> out;
    auto open = [&](Vec2 p) { return !sim.nav || (sim.nav->walkable(p) && !sim.nav->inRock(p)); };
    // A spot at `p`, or the first open one a little to either side of it.
    auto place = [&](Vec2 p, Vec2 across) -> std::optional<Vec2> {
        for (const double side : {0.0, 1.5, -1.5, 3.0, -3.0}) {
            if (open(p + across * side)) return p + across * side;
        }
        return std::nullopt;
    };
    auto add = [&](std::optional<Vec2> p) {
        if (!p) return;
        for (const Vec2 q : out) if (distance(q, *p) < 3) return;
        out.push_back(*p);
    };
    // The enemy's bases: its start sites and the Citadels it has built.
    std::vector<Vec2> foes;
    for (int64_t p = 0; p < static_cast<int64_t>(s.players.size()); p++) {
        if (p != player && s.hostile(p, player)) foes.push_back(start(s, p));
    }
    for (const Structure& b : s.structures) {
        if (b.kind == Structure::Kind::citadel && s.hostile(b.owner, player)) foes.push_back(b.position);
    }
    for (const Structure& citadel : s.structures) {
        if (!(citadel.owner == player && citadel.kind == Structure::Kind::citadel && citadel.complete())) continue;
        const int64_t site = sim.site(citadel);
        if (site >= static_cast<int64_t>(map.bases.size())) continue;
        const int64_t level = map.bases[static_cast<size_t>(site)].level;
        for (const Ramp& r : map.ramps) {
            // The end on its side: on the base's level, and nearer to this
            // base than to any of the enemy's.
            for (const bool low : {true, false}) {
                const Vec2 end = low ? r.low : r.high, other = low ? r.high : r.low;
                if ((low ? r.lowLevel : r.highLevel) != level) continue;
                const double d = distance(end, citadel.position);
                bool ours = true;
                for (const Vec2 f : foes) if (distance(end, f) <= d) ours = false;
                if (!ours) continue;
                const Vec2 away = normalize(end - other);
                add(place(end + away * 2, Vec2(-away.y, away.x)));
            }
        }
    }
    for (const Objective& o : s.players[static_cast<size_t>(player)].orders().objectives) {
        if (o.kind == Objective::Kind::hold) add(place(o.at, Vec2(1, 0)));
    }
    return out;
}

// MARK: - Drops

/// Dropship drops, when its temperament does them. With the army at
/// home, a Dropship at the rally with energy to heal takes up to 8 slots
/// of Rangers and Juggernauts standing near it (two Juggernauts at most)
/// and flies them into the enemy ore line most worth raiding, where
/// they raid. When the fight there turns against the squad, the Dropship
/// lifts out whoever is next to it and flies them to the rally; the
/// rest walk home. Home again, squad and Dropship rejoin the army.
/// On hold none go, a drop on its way turns back, and a squad out there
/// is pulled out.
/// `skip`: units given other orders this second.
std::vector<Command> Commander::drops(const Simulation& sim, const std::set<int64_t>& skip) const {
    const GameState& s = sim.state;
    const Player& me = s.players[player];
    const bool holding = me.orders().stance == Stance::hold;
    const Vec2 home = start(s);
    const Vec2 rally = player >= 0 && player < static_cast<int64_t>(sim.rallySlots.size())
        ? (sim.rallySlots[player].empty() ? home : sim.rallySlots[player].front()) : home;
    const auto mine = filtered(s.units, [&](const Unit& u) { return u.owner == player; });
    const auto dropships = filtered(mine, [](const Unit& u) { return u.kind == Unit::Kind::dropship; });
    auto errand = [](const Unit& u) -> std::optional<Vec2> {
        if (!u.mission) return std::nullopt;
        if (auto r = std::get_if<Mission::Raid>(&u.mission->value)) return r->at;
        if (auto f = std::get_if<Mission::FallBack>(&u.mission->value)) return f->at;
        return std::nullopt;
    };
    auto homeward = [&](Vec2 p) { return distance(p, rally) < 8 || distance(p, home) < 8; };
    auto bio = [](const Unit& u) { return u.kind == Unit::Kind::ranger || u.kind == Unit::Kind::juggernaut; };
    std::vector<Command> out;

    // Home again: back to the army.
    const auto squad = filtered(mine, [&](const Unit& u) { return bio(u) && errand(u).has_value(); });
    for (const Unit& u : squad + dropships) {
        auto p = errand(u);
        if (!(p && homeward(*p) && distance(u.position, *p) < 6)) continue;
        out.push_back(make::mission(u.id, std::nullopt));
    }
    if (holding) {
        for (const Unit& m : dropships) {
            if (!m.mission) continue;
            if (auto d = std::get_if<Mission::Drop>(&m.mission->value); d && !homeward(d->at)) {
                out.push_back(make::mission(m.id, make::drop(rally)));
            }
        }
    }
    // Out there: pull the squad out when the fight goes against it.
    auto raidingAway = [&](const Unit& u) {
        if (!u.mission) return false;
        if (auto r = std::get_if<Mission::Raid>(&u.mission->value)) return !homeward(r->at);
        return false;
    };
    const auto away = filtered(squad, raidingAway);
    const auto rides = filtered(dropships, raidingAway);
    if (!away.empty()) {
        const Vec2 centre = centreOf(away);
        const auto danger = filtered(s.units, [&](const Unit& u) {
            return s.hostile(u.owner, player) && u.soldier() && u.stats().hitsGround && u.task != Unit::Task::aboard
                && distance(u.position, centre) < 10;
        });
        double hpSum = 0, hpFull = 0;
        for (const Unit& u : away) hpSum = hpSum + u.hp;
        for (const Unit& u : away) hpFull = hpFull + u.stats().hp;
        const double health = hpSum / hpFull;
        if (!(strength(danger) > 1.2 * strength(away) || health < 0.35 || holding)) return out;
        std::optional<Unit> ride;
        for (const Unit& m : rides) {
            if (distance(m.position, centre) < 8 && m.cargo.value_or(std::vector<int64_t>{}).empty()) { ride = m; break; }
        }
        int64_t room = Rules::dropshipSlots;
        const auto order = sorted(away, [&](const Unit& a, const Unit& b) {
            const double da = ride ? distance(a.position, ride->position) : 0;
            const double db = ride ? distance(b.position, ride->position) : 0;
            return da != db ? da < db : a.id < b.id;
        });
        for (const Unit& u : order) {
            if (ride && distance(u.position, ride->position) < 6 && Rules::slots(u.kind) <= room) {
                out.push_back(make::board(ride->id, u.id));
                room -= Rules::slots(u.kind);
            } else {
                out.push_back(make::mission(u.id, make::fallBack(home)));
            }
        }
        if (ride) out.push_back(make::mission(ride->id, make::drop(rally)));
        for (const Unit& m : rides) {
            if (ride && m.id == ride->id) continue;
            out.push_back(make::mission(m.id, std::nullopt));
        }
        return out;
    }
    // A Dropship whose squad is gone rejoins the army.
    for (const Unit& m : rides) out.push_back(make::mission(m.id, std::nullopt));

    // A new drop. The harass style also carries Scorpions out, two or more
    // that the army has to spare (not posted at its own ore lines), to bury
    // them at the enemy's ore line, with its Rangers and Juggernauts when
    // it drops those.
    const auto stingers = me.style.value_or(Player::Style::bio) == Player::Style::harass
        ? filtered(mine, [&](const Unit& u) {
              return u.kind == Unit::Kind::scorpion && sim.inArmy(u) && u.hp > 0 && !skip.count(u.id)
                  && (u.task == Unit::Task::idle || u.task == Unit::Task::toRally);
          })
        : std::vector<Unit>{};
    const bool stinging = stingers.size() >= 2;
    if (!((me.drops == true || stinging) && !holding && !me.attack && !me.retreating && me.defending != true && squad.empty()
          && !any(dropships, [](const Unit& u) { return u.mission.has_value(); }))) {
        return out;
    }
    const auto ship = minBy(filtered(dropships, [&](const Unit& u) {
        return sim.inArmy(u) && u.cargo.value_or(std::vector<int64_t>{}).empty() && u.energy.value_or(0) >= 50
            && u.task == Unit::Task::idle;
    }), [](const Unit& a, const Unit& b) { return a.id < b.id; });
    if (!ship) return out;
    const Unit& m = *ship;
    const auto ready = filtered(mine, [&](const Unit& u) { return bio(u) && sim.inArmy(u); });
    const bool bioDrop = me.drops == true && armySupply(ready) >= 12;
    if (!(bioDrop || stinging)) return out;
    auto byDistance = [&](const Unit& a, const Unit& b) {
        const double da = distance(a.position, m.position), db = distance(b.position, m.position);
        return da != db ? da < db : a.id < b.id;
    };
    const auto near = sorted(filtered(bioDrop ? ready : std::vector<Unit>{}, [&](const Unit& u) {
        return u.task == Unit::Task::idle && !skip.count(u.id) && distance(u.position, m.position) < 6;
    }), byDistance);
    const auto sting = sorted(filtered(stingers, [&](const Unit& u) { return distance(u.position, m.position) < 6; }), byDistance);
    int64_t room = Rules::dropshipSlots;
    std::vector<Unit> load;
    for (const Unit& u : prefix(filtered(near, [](const Unit& x) { return x.kind == Unit::Kind::juggernaut; }), 2)
             + filtered(near, [](const Unit& x) { return x.kind == Unit::Kind::ranger; })) {
        if (!(Rules::slots(u.kind) <= room)) continue;
        load.push_back(u);
        room -= Rules::slots(u.kind);
    }
    // Scorpions fill what is left (up to four).
    int64_t stung = 0;
    for (const Unit& u : sting) {
        if (!(stung < 4 && Rules::slots(u.kind) <= room)) continue;
        load.push_back(u);
        room -= Rules::slots(u.kind);
        stung += 1;
    }
    // Full with infantry (as before), or Scorpions to take (two at least).
    if (!(room <= 2 || stung >= 2)) return out;
    if (!bioDrop && stung < 2) return out;
    const auto t = raidTarget(s, sim, strength(load + std::vector<Unit>{m}));
    if (!t) return out;
    for (const Unit& u : load) out.push_back(make::board(m.id, u.id));
    out.push_back(make::mission(m.id, make::drop(*t)));
    return out;
}

/// A few raiders in its base: the nearest army units, worth twice as
/// much, go and hunt them; once they are gone the hunters rejoin.
std::vector<Command> Commander::hunters(const Simulation& sim) const {
    const GameState& s = sim.state;
    const auto mine = filtered(s.structures, [&](const Structure& b) { return b.owner == player; });
    // Armed raiders: a Dropship alone is the army's business when it
    // unloads, a Kestrel is hunted by what shoots air.
    const auto raiders = filtered(s.units, [&](const Unit& e) {
        return s.hostile(e.owner, player) && e.soldier() && e.task != Unit::Task::aboard && (!e.stats().air || e.stats().hitsGround)
            && any(mine, [&](const Structure& b) { return distance(b.position, e.position) < Rules::radius(b.kind) + 12; });
    });
    // Where a shooter its side could not see hit its units lately: a hunt
    // there is the seekers' (below), not a raider's.
    const std::vector<Vec2> spots = unseenShots(s);
    auto seeking = [&](const Unit& u) {
        const Mission::Hunt* h = u.mission ? std::get_if<Mission::Hunt>(&u.mission->value) : nullptr;
        return h && any(spots, [&](Vec2 p) { return distance(p, h->at) < 3.5; });
    };
    // Soldiers only: Prospectors pulled to fight are `militia`'s.
    const auto hunting = filtered(s.units, [&](const Unit& u) {
        return u.owner == player && u.soldier() && isHunt(u.mission) && !seeking(u);
    });
    const bool air = any(raiders, [](const Unit& u) { return u.stats().air; });
    const auto army = filtered(s.units, [&](const Unit& u) {
        return u.owner == player && sim.inArmy(u) && (air ? u.stats().hitsAir : u.stats().hitsGround)
            && u.kind != Unit::Kind::longbow;
    });
    const auto first = minBy(raiders, [&](const Unit& a, const Unit& b) {
        return distance(a.position, start(s)) < distance(b.position, start(s));
    });
    // (A hunt that sought a spot is a raider's once the spot's window is over.)
    if (!(first && strength(raiders) <= raidSize && strength(army) + strength(hunting) >= 2 * strength(raiders))) {
        std::vector<Command> back;
        std::set<int64_t> skip;
        for (const Unit& h : hunting) {
            back.push_back(make::mission(h.id, std::nullopt));
            skip.insert(h.id);
        }
        return back + seekers(sim, spots, skip);
    }
    std::vector<Command> out;
    for (const Unit& h : hunting) {
        if (auto hunt = std::get_if<Mission::Hunt>(&h.mission->value); hunt && distance(hunt->at, first->position) > 3) {
            out.push_back(make::mission(h.id, make::hunt(first->position)));
        }
    }
    double force = strength(hunting);
    for (const Unit& u : sorted(army, [&](const Unit& a, const Unit& b) {
             return distance(a.position, first->position) < distance(b.position, first->position);
         })) {
        if (!(force < 2 * strength(raiders))) break;
        out.push_back(make::mission(u.id, make::hunt(first->position)));
        force += strength({u});
    }
    std::set<int64_t> skip;
    for (const Command& c : out) {
        if (auto m = std::get_if<Command::Mission>(&c.value)) skip.insert(m->unit);
    }
    return out + seekers(sim, spots, skip);
}

/// Where its units were hit lately by a shooter its side could not see
/// (`shotFrom`), one spot to each cluster of 3 cells.
std::vector<Vec2> Commander::unseenShots(const GameState& s) const {
    std::vector<Vec2> out;
    for (const Unit& u : s.units) {
        if (!(u.owner == player && u.hp > 0 && u.shotFrom && s.time - u.hitAt.value_or(-100) < unseenWindow)) continue;
        if (any(out, [&](Vec2 p) { return distance(p, *u.shotFrom) < 3; })) continue;
        out.push_back(*u.shotFrom);
    }
    return out;
}

/// A unit to each spot a shooter hit from without being seen: the nearest
/// soldier of its army that can walk or fly there (not a Longbow, a
/// Scorpion or a Dropship) goes and hunts around it; within 2 cells it sees
/// a hidden Scorpion. Two spots at a time at most.
std::vector<Command> Commander::seekers(const Simulation& sim, const std::vector<Vec2>& spots, const std::set<int64_t>& skip) const {
    const GameState& s = sim.state;
    std::vector<Command> out;
    std::set<int64_t> sent = skip;
    int64_t n = 0;
    for (const Vec2 spot : spots) {
        if (n >= 2) break;
        n += 1;
        const bool covered = any(s.units, [&](const Unit& u) {
            const Mission::Hunt* h = u.mission ? std::get_if<Mission::Hunt>(&u.mission->value) : nullptr;
            return u.owner == player && u.hp > 0 && h && distance(h->at, spot) < 3.5;
        });
        if (covered) continue;
        const auto pick = minBy(filtered(s.units, [&](const Unit& u) {
            return u.owner == player && u.hp > 0 && sim.inArmy(u) && !sent.count(u.id) && !sim.piloted(u.id)
                && u.kind != Unit::Kind::longbow && u.kind != Unit::Kind::scorpion && u.kind != Unit::Kind::dropship
                && u.stats().speed > 0 && !(u.planted());
        }), [&](const Unit& a, const Unit& b) {
            const double da = distance(a.position, spot), db = distance(b.position, spot);
            return da != db ? da < db : a.id < b.id;
        });
        if (!pick) continue;
        sent.insert(pick->id);
        out.push_back(make::mission(pick->id, make::hunt(spot)));
    }
    return out;
}

/// Where a player (this one by default) started.
Vec2 Commander::start(const GameState& s, std::optional<int64_t> of) const {
    const int64_t p = of.value_or(player);
    return map.bases[s.players[p].start.value_or(map.starts[p])].center;
}

/// The building closest to `p` by walking distance.
std::optional<Structure> Commander::nearest(const std::vector<Structure>& structures, Vec2 to) const {
    return minBy(structures, [&](const Structure& a, const Structure& b) {
        return router.distance(to, a.position) < router.distance(to, b.position);
    });
}

/// Fill Bastions from the Rangers standing at the rally, and send Prospectors
/// from the nearest base to repair a Bastion under fire (two per Bastion).
std::vector<Command> Commander::crew(const Simulation& sim, std::set<int64_t>& busy) const {
    const GameState& s = sim.state;
    std::vector<Command> out;
    // The Bastions the humans want manned first, with any Ranger to
    // spare (out of its squad if need be).
    std::map<int64_t, int64_t> sent;
    std::set<int64_t> taken;
    for (const Manning& m : manning(sim)) {
        if (!m.bastion) continue;
        const Structure& b = *m.bastion;
        for (const Unit& u : m.send) {
            if (u.mission) out.push_back(make::mission(u.id, std::nullopt));
            out.push_back(make::load(u.id, b.id));
            taken.insert(u.id);
        }
        sent[b.id] += static_cast<int64_t>(m.send.size());
    }
    const auto bastions = filtered(s.structures, [&](const Structure& b) {
        return b.kind == Structure::Kind::bastion && b.owner == player && b.complete();
    });
    if (bastions.empty()) return out;
    auto idle = filtered(s.units, [&](const Unit& u) {
        return u.kind == Unit::Kind::ranger && u.owner == player && u.task == Unit::Task::idle && !taken.count(u.id);
    });
    for (const Structure& b : bastions) {
        const int64_t bound = countIf(s.units, [&](const Unit& u) { return u.task == Unit::Task::toBastion && u.structure == b.id; })
            + sent[b.id];
        int64_t room = Rules::bastionCapacity - (b.crew ? static_cast<int64_t>(b.crew->size()) : 0) - bound;
        while (room > 0) {
            std::optional<size_t> k;
            for (size_t i = 0; i < idle.size(); i++) {
                if (!k || distance(idle[i].position, b.position) < distance(idle[*k].position, b.position)) k = i;
            }
            if (!k) break;
            out.push_back(make::load(idle[*k].id, b.id));
            idle.erase(idle.begin() + static_cast<std::ptrdiff_t>(*k));
            room -= 1;
        }
        const bool hurt = b.hp < Rules::hp(Structure::Kind::bastion) * 0.9;
        const bool menace = any(s.units, [&](const Unit& u) {
            return s.hostile(u.owner, player) && u.soldier() && distance(u.position, b.position) < 12;
        });
        const int64_t fixing = countIf(s.units, [&](const Unit& u) { return u.task == Unit::Task::repairing && u.structure == b.id; });
        if (hurt && (menace || b.hp < Rules::hp(Structure::Kind::bastion) * 0.5) && fixing < 2) {
            const auto prospectors = sorted(filtered(s.units, [&](const Unit& u) {
                return u.owner == player && u.kind == Unit::Kind::prospector && !busy.count(u.id) && !u.order
                    && (u.task == Unit::Task::mining || u.task == Unit::Task::toPatch || u.task == Unit::Task::waiting
                        || u.task == Unit::Task::idle);
            }), [&](const Unit& a, const Unit& c) { return distance(a.position, b.position) < distance(c.position, b.position); });
            for (const Unit& u : prefix(prospectors, 2 - fixing)) {
                if (!(distance(u.position, b.position) < 30)) continue;
                out.push_back(make::repair(u.id, b.id));
                busy.insert(u.id);
            }
        }
    }
    return out;
}

/// A spot for a Bastion at a base that is not a start base: 4–7 cells
/// out from the Citadel toward the enemy's start, on its level.
std::optional<Vec2> Commander::bastionSpot(const GameState& s, const Simulation& /*sim*/, const Structure& citadel) const {
    std::vector<Vec2> enemy;
    for (int64_t i = 0; i < static_cast<int64_t>(s.players.size()); i++) if (s.hostile(i, player)) enemy.push_back(start(s, i));
    if (enemy.empty()) return std::nullopt;
    // The nearest enemy start (C++: with more players; the one with two).
    const Vec2 foe = *minBy(enemy, [&](Vec2 a, Vec2 b) {
        return router.distance(citadel.position, a) < router.distance(citadel.position, b);
    });
    const std::vector<Vec2> way = router.route(citadel.position, foe);
    if (way.empty() || !(distance(way.front(), citadel.position) > 1e-3)) return std::nullopt;
    const Vec2 dir = normalize(way.front() - citadel.position);
    const auto base = minBy(map.bases, [&](const auto& a, const auto& b) {
        return distance(a.center, citadel.position) < distance(b.center, citadel.position);
    });
    const std::vector<Vec2> wells = base ? base->wells : std::vector<Vec2>{};
    const int64_t level = field.level(citadel.position);
    const double height = field.height(citadel.position);
    std::optional<std::pair<Vec2, double>> best;
    // Straight toward the enemy if it is clear; else the nearest clear
    // angle (an ore line may lie that way). Angles are swept evenly
    // both ways, so mirrored bases pick mirrored spots; a tie goes to
    // the spot nearer the front.
    // With `frontDefence`: only within 60 degrees of the enemy, on open
    // ground in front of the buildings (`openFront`); none, none.
    std::vector<double> angles{0};
    for (int k = 1; k <= (frontDefence ? 7 : 20); k++) {
        angles.push_back(-0.15 * static_cast<double>(k));
        angles.push_back(0.15 * static_cast<double>(k));
    }
    for (int i = 0; 4.5 + 0.5 * i <= (frontDefence ? 9.0 : 8.0); i++) {
        const double r = 4.5 + 0.5 * i;
        for (double a : angles) {
            const Vec2 d(dir.x * std::cos(a) - dir.y * std::sin(a), dir.x * std::sin(a) + dir.y * std::cos(a));
            const Vec2 p = citadel.position + d * r;
            if (!clear(p, 1.5, s, wells, level, height)) continue;
            if (frontDefence && !openFront(p, d, s)) continue;
            const double score = std::abs(a) * 3 + std::abs(r - 5.5) + 0.001 * distance(p, map.front());
            if (!best || score < best->second) best = std::make_pair(p, score);
        }
    }
    if (!best) return std::nullopt;
    return best->first;
}

/// Open ground for a defence at `p` facing `dir`: no building within 4.5
/// cells (not wedged between two), and none of the buildings within 9
/// cells lies ahead of it (it stands in front of them, not behind).
bool Commander::openFront(Vec2 p, Vec2 dir, const GameState& s) const {
    for (const Structure& b : s.structures) {
        const double d = distance(b.position, p);
        if (d < 4.5) return false;
        if (d < 9 && dot(b.position - p, dir) > 1.0) return false;
    }
    return true;
}

/// Where the nearest enemy starts, as the crow flies: the way its fliers
/// come in. Nil with no enemy.
std::optional<Vec2> Commander::airDirection(const GameState& s, const Structure& citadel) const {
    std::optional<Vec2> best;
    for (int64_t i = 0; i < static_cast<int64_t>(s.players.size()); i++) {
        if (!s.hostile(i, player)) continue;
        const Vec2 a = start(s, i);
        if (!best || distance(a, citadel.position) < distance(*best, citadel.position)) best = a;
    }
    if (!best || !(distance(*best, citadel.position) > 1e-3)) return std::nullopt;
    return normalize(*best - citadel.position);
}

// MARK: - Economy

/// Spending, then the Prospectors' jobs. The Prospectors are placed every second,
/// whatever the spending is saving up for.
std::vector<Command> Commander::economy(const Simulation& sim, const std::set<int64_t>& taken) const {
    std::set<int64_t> busy = taken;
    const auto spent = spend(sim, busy);
    return spent + workers(sim, busy);
}

/// What to build, train and research with the money there is (steps
/// 1–6, the team's requests and the upgrades), with only one building
/// ordered on any one spot.
std::vector<Command> Commander::spend(const Simulation& sim, std::set<int64_t>& busy) const {
    return oneBuildPerSpot(spending(sim, busy));
}

/// `spend` before the spot check; it returns early while saving for
/// something.
std::vector<Command> Commander::spending(const Simulation& sim, std::set<int64_t>& busy) const {
    using SK = Structure::Kind;
    using UK = Unit::Kind;
    const GameState& s = sim.state;
    const Player& me = s.players[player];
    const Player::Style style = me.style.value_or(Player::Style::bio);
    int64_t money = me.ore;
    int64_t hydrogen = me.hydrogen;
    std::vector<Command> out;

    const auto mine = filtered(s.structures, [&](const Structure& b) { return b.owner == player; });
    const auto citadels = filtered(mine, [](const Structure& b) { return b.kind == SK::citadel; });
    const auto done = filtered(citadels, [](const Structure& b) { return b.complete(); });
    const auto myProspectors = filtered(s.units, [&](const Unit& u) { return u.kind == UK::prospector && u.owner == player; });
    std::vector<BuildOrder> ordered;
    for (const Unit& u : myProspectors) if (u.order) ordered.push_back(*u.order);
    // Finish buildings whose Prospector was killed (a Lab rises by itself).
    for (const Structure& st : mine) {
        if (!(!st.complete() && st.kind != SK::lab)) continue;
        const bool welded = any(myProspectors, [&](const Unit& u) { return u.structure == st.id; });
        if (!welded) {
            if (auto w = builder(s, st.position, busy)) {
                out.push_back(make::resume(*w, st.id));
                busy.insert(*w);
            }
        }
    }
    if (done.empty()) {
        // Lost every Citadel: build one again on the start base,
        // or the nearest free site.
        std::set<int64_t> taken;
        for (const Structure& b : s.structures) if (b.kind == SK::citadel) taken.insert(sim.site(b));
        if (any(citadels, [](const Structure& b) { return !b.complete(); })
            || any(ordered, [](const BuildOrder& o) { return o.kind == SK::citadel; })
            || !(money >= Rules::cost(SK::citadel))) {
            return out;
        }
        const auto site = nextSite(s, sim, taken, start(s));
        if (!site) return out;
        const auto w = builder(s, *site, busy);
        if (!w) return out;
        out.push_back(make::build(*w, SK::citadel, *site));
        return out;
    }
    const auto rising = filtered(mine, [](const Structure& b) { return !b.complete(); });
    const auto garrison = filtered(mine, [](const Structure& b) { return b.kind == SK::garrison; });
    const auto garrisonDone = filtered(garrison, [](const Structure& b) { return b.complete(); });
    const auto foundriesDone = filtered(mine, [](const Structure& b) { return b.kind == SK::foundry && b.complete(); });
    const auto spacedocksDone = filtered(mine, [](const Structure& b) { return b.kind == SK::spacedock && b.complete(); });
    const auto producers = spacedocksDone + foundriesDone + garrisonDone;
    auto planned = [&](SK k) -> int64_t {
        return countIf(mine, [&](const Structure& b) { return b.kind == k; })
            + countIf(ordered, [&](const BuildOrder& o) { return o.kind == k; });
    };
    // Supply that lands in time: Hab Domes on the way, and a rising Command
    // Center only if the supply left lasts until it is done (every
    // Citadel spends one per 12 s, every Garrison one per 18 s,
    // every Foundry three per 32 s, every Spacedock two per 30 s).
    const double rate = static_cast<double>(done.size()) / Rules::prospectorTrainTime
        + static_cast<double>(garrisonDone.size()) / Rules::rangerTrainTime
        + static_cast<double>(static_cast<int64_t>(foundriesDone.size()) * Rules::supply(UK::longbow)) / Rules::trainTime(UK::longbow)
        + static_cast<double>(static_cast<int64_t>(spacedocksDone.size()) * Rules::supply(UK::dropship)) / Rules::trainTime(UK::dropship);
    const int64_t habDomeSupply = countIf(rising, [](const Structure& b) { return b.kind == SK::habDome; }) * Rules::supply(SK::habDome)
        + countIf(ordered, [](const BuildOrder& o) { return o.kind == SK::habDome; }) * Rules::supply(SK::habDome);
    const double spare = static_cast<double>(me.supplyCap + habDomeSupply - me.supplyUsed);
    int64_t risingSupply = 0;
    for (const Structure& b : rising) {
        if (b.kind == SK::citadel && b.buildLeft.value_or(0) * rate < spare) risingSupply += Rules::supply(b.kind);
    }
    const int64_t pendingSupply = habDomeSupply + risingSupply;
    const bool expanding = any(rising, [](const Structure& b) { return b.kind == SK::citadel; })
        || any(ordered, [](const BuildOrder& o) { return o.kind == SK::citadel; });

    // Patches of its bases with a Citadel (built or rising), and
    // the sites anyone has taken.
    auto siteOf = [&](Vec2 p) -> int64_t {
        std::optional<int64_t> best;
        for (int64_t i = 0; i < static_cast<int64_t>(sim.sites.size()); i++) {
            if (!best || distance(sim.sites[i], p) < distance(sim.sites[*best], p)) best = i;
        }
        return best.value_or(0);
    };
    std::set<int64_t> ownedSites;
    for (const Structure& b : citadels) ownedSites.insert(sim.site(b));
    for (const BuildOrder& o : ordered) if (o.kind == SK::citadel) ownedSites.insert(siteOf(o.position));
    std::set<int64_t> takenSites = ownedSites;
    for (const Structure& b : s.structures) if (b.kind == SK::citadel) takenSites.insert(sim.site(b));
    std::vector<int64_t> live;
    for (int64_t i = 0; i < static_cast<int64_t>(s.patches.size()); i++) {
        if (s.patches[i].remaining > 0 && ownedSites.count(sim.patchBase[i])) live.push_back(i);
    }
    const auto derricks = filtered(mine, [](const Structure& b) { return b.kind == SK::derrick; });
    const int64_t target = ac::min(static_cast<int64_t>(live.size()) * workersPerPatch
                                        + Rules::workersPerWell * static_cast<int64_t>(derricks.size()), maxWorkers);
    int64_t doneQueued = 0;
    for (const Structure& b : done) doneQueued += b.queueCount();
    const int64_t workers = static_cast<int64_t>(myProspectors.size()) + doneQueued;
    const int64_t garrisonPlanned = planned(SK::garrison);
    // Its soldiers and those in production, by kind and in supply.
    std::map<UK, int64_t> have;
    for (const Unit& u : s.units) if (u.owner == player && u.soldier()) have[u.kind] += 1;
    for (const Structure& b : producers) for (UK k : queued(b)) have[k] += 1;
    int64_t inProduction = 0;
    for (const Structure& b : producers) {
        int64_t n = 0;
        for (UK k : queued(b)) n += Rules::supply(k);
        inProduction += n;
    }
    const int64_t army = armySupply(filtered(s.units, [&](const Unit& u) { return u.soldier() && u.owner == player; })) + inProduction;
    const bool habDomeDone = any(mine, [](const Structure& b) { return b.kind == SK::habDome && b.complete(); });
    // Its style's mix, bent to answer what it has seen (`counters`).
    const Counters answer = counters(enemyMix(s));

    // 4. Prospectors up to saturation (a few extra while a base is on the way).
    // It keeps one Prospector queued ahead once it can afford to,
    // and queues deeper while ore piles up. Run at step 4, and
    // earlier while a request saves (1b).
    int64_t supplyFree = me.supplyCap - me.supplyUsed;
    int64_t count = workers;
    auto trainProspectors = [&]() {
        for (const Structure& citadel : done) {
            const int64_t depth = money >= 400 ? 3 : money >= 150 ? 2 : 1;
            if (!(citadel.queueCount() < depth)) continue;
            if (!(count < target + (expanding ? 8 : 0) && count < maxWorkers && money >= Rules::prospectorCost && supplyFree > 0)) break;
            out.push_back(make::train(citadel.id, std::nullopt));
            money -= Rules::prospectorCost;
            supplyFree -= 1;
            count += 1;
        }
    };

    // 1. Supply: a Hab Dome takes ~25 s to walk to and build; production
    // uses supply at `rate` meanwhile.
    const int64_t headroom = me.supplyCap + pendingSupply - me.supplyUsed;
    const int64_t habDomesBuilding = countIf(rising, [](const Structure& b) { return b.kind == SK::habDome; })
        + countIf(ordered, [](const BuildOrder& o) { return o.kind == SK::habDome; });
    const bool producing = workers < target || (!producers.empty() && army < armyCap(s));
    const int64_t usage = 2 * static_cast<int64_t>(done.size()) + 2 * static_cast<int64_t>(garrisonDone.size())
        + 3 * static_cast<int64_t>(foundriesDone.size()) + 2 * static_cast<int64_t>(spacedocksDone.size());
    if (me.supplyCap + pendingSupply < Rules::maxSupply && (producing || headroom <= 0) && headroom <= 1 + usage
        && habDomesBuilding < ac::max<int64_t>(1, static_cast<int64_t>(done.size() + producers.size()) / 2)) {
        if (money >= Rules::cost(SK::habDome)) {
            std::optional<Vec2> spot;
            for (const Structure& c : done) if ((spot = habDomeSpot(s, c, sim.navGrid()))) break;
            if (spot) {
                if (auto w = builder(s, *spot, busy)) {
                    out.push_back(make::build(*w, SK::habDome, *spot));
                    money -= Rules::cost(SK::habDome);
                    busy.insert(*w);
                }
            }
        } else {
            return out; // Save for it before spending on anything else.
        }
    }

    // 1b. What the team's humans asked for (`Directives`), ahead of the
    // AI's own spending: each request is ordered, or its cost is held.
    // While one saves, the Prospectors keep coming out of what it holds
    // (constant worker production). Then the bank floor: the AI spends
    // for itself only what is over it (what the requests hold counts).
    const Directives wishes = me.orders();
    const auto asked = requests(sim, money, hydrogen, supplyFree, busy, out);
    out = out + asked.out;
    const int64_t keepOre = ac::max<int64_t>(0, wishes.keepOre - asked.reserved.ore);
    const int64_t keepHydrogen = ac::max<int64_t>(0, wishes.keepHydrogen - asked.reserved.hydrogen);
    if (asked.reserved.ore > 0) {
        money += asked.reserved.ore - wishes.keepOre;
        trainProspectors();
        money -= asked.reserved.ore - wishes.keepOre;
    }
    money -= keepOre;
    hydrogen -= keepHydrogen;

    // 2. The first Garrison as soon as a Hab Dome stands.
    if (garrisonPlanned == 0 && habDomeDone) {
        if (money >= Rules::cost(SK::garrison)) {
            std::optional<Vec2> spot;
            for (const Structure& c : done) if ((spot = garrisonSpot(s, c, sim.navGrid()))) break;
            if (spot) {
                if (auto w = builder(s, *spot, busy)) {
                    out.push_back(make::build(*w, SK::garrison, *spot));
                    money -= Rules::cost(SK::garrison);
                    busy.insert(*w);
                }
            }
        } else {
            return out; // Saving for it.
        }
    }

    // 2b. A Bastion at every base but the start base, once a Garrison
    // stands, facing the enemy (a wall at the natural).
    if (!garrisonDone.empty()) {
        std::vector<Vec2> bastions = positions(filtered(mine, [](const Structure& b) { return b.kind == SK::bastion; }));
        for (const BuildOrder& o : ordered) if (o.kind == SK::bastion) bastions.push_back(o.position);
        const Vec2 startAt = start(s);
        for (const Structure& citadel : done) {
            if (!(distance(citadel.position, startAt) > 3
                  && !any(bastions, [&](Vec2 b) { return distance(b, citadel.position) < 10; }))) {
                continue;
            }
            // No spot in front (`frontDefence`): skip this base, try the next.
            if (frontDefence && !bastionSpot(s, sim, citadel)) continue;
            if (!(money >= Rules::cost(SK::bastion))) return out; // Saving for it.
            if (auto spot = bastionSpot(s, sim, citadel)) {
                if (auto w = builder(s, *spot, busy)) {
                    out.push_back(make::build(*w, SK::bastion, *spot));
                    money -= Rules::cost(SK::bastion);
                    busy.insert(*w);
                }
            }
            break;
        }
    }

    // 2b'. Sentinels against fliers it has seen (`counters`): at every
    // finished base, by its ore line.
    if (!garrisonDone.empty() && answer.sentinels > 0) {
        std::vector<Vec2> sentinels = positions(filtered(mine, [](const Structure& b) { return b.kind == SK::sentinel; }));
        for (const BuildOrder& o : ordered) if (o.kind == SK::sentinel) sentinels.push_back(o.position);
        for (const Structure& citadel : done) {
            if (!(countIf(sentinels, [&](Vec2 p) { return distance(p, citadel.position) < 12; }) < answer.sentinels)) continue;
            if (!(money >= Rules::cost(SK::sentinel))) return out; // Saving for it.
            // At heavy air (two wanted) the second goes forward, toward the
            // enemy; the first covers the ore line.
            std::optional<Vec2> spot;
            if (frontDefence && answer.sentinels >= 2
                && countIf(sentinels, [&](Vec2 p) { return distance(p, citadel.position) < 12; }) >= 1) {
                spot = forwardSentinelSpot(s, citadel, sentinels);
            }
            if (!spot) spot = sentinelSpot(s, citadel, sentinels);
            if (spot) {
                if (auto w = builder(s, *spot, busy)) {
                    out.push_back(make::build(*w, SK::sentinel, *spot));
                    money -= Rules::cost(SK::sentinel);
                    busy.insert(*w);
                }
            }
            break;
        }
    }

    // 2b''. A Sentinel where a shooter its side could not see hit its units
    // (a buried Scorpion, say): at the ore line of the base it hit, unless
    // one already sees the spot. Its whole sight finds what hides; a unit
    // goes there as well (`seekers`).
    if (!garrisonDone.empty()) {
        std::vector<Vec2> sentinels = positions(filtered(mine, [](const Structure& b) { return b.kind == SK::sentinel; }));
        for (const BuildOrder& o : ordered) if (o.kind == SK::sentinel) sentinels.push_back(o.position);
        for (const Vec2 spot : unseenShots(s)) {
            if (any(sentinels, [&](Vec2 p) { return distance(p, spot) <= Rules::vision(SK::sentinel); })) continue;
            const auto citadel = minBy(filtered(done, [&](const Structure& c) { return distance(c.position, spot) < 16; }),
                                       [&](const Structure& a, const Structure& b) {
                                           return distance(a.position, spot) < distance(b.position, spot);
                                       });
            if (!citadel || countIf(sentinels, [&](Vec2 p) { return distance(p, citadel->position) < 12; }) >= 3) continue;
            if (!(money >= Rules::cost(SK::sentinel))) return out; // Saving for it.
            if (auto at = sentinelSpot(s, *citadel, sentinels)) {
                if (auto w = builder(s, *at, busy)) {
                    out.push_back(make::build(*w, SK::sentinel, *at));
                    money -= Rules::cost(SK::sentinel);
                    busy.insert(*w);
                }
            }
            break;
        }
    }

    // 2c. Derricks as MH is wanted: one once a Garrison is on the
    // way, a second once it stands (the Foundry is next), and two at
    // every finished base once the Spacedock is on the way, the later
    // ones only while the ore lines are nearly full.
    const bool mech = unlocked.count(UK::firefly) || unlocked.count(UK::longbow) || unlocked.count(UK::hailstorm);
    bool hydrogenUnits = false;
    for (UK k : unlocked) if (Rules::hydrogenCost(k) > 0) { hydrogenUnits = true; break; }
    const int64_t derricksPlanned = static_cast<int64_t>(derricks.size())
        + countIf(ordered, [](const BuildOrder& o) { return o.kind == SK::derrick; });
    const int64_t own = !hydrogenUnits || garrisonPlanned == 0 ? 0
        : !mech || (garrisonDone.empty() && planned(SK::foundry) == 0) ? 1
        : planned(SK::spacedock) == 0 ? 2 : 2 * static_cast<int64_t>(done.size());
    // The humans' split: no new Derricks on ore, two a base
    // early on MH.
    const int64_t hydrogenWanted = wishes.harvest == Harvest::ore ? 0
        : wishes.harvest == Harvest::hydrogen && garrisonPlanned > 0 ? 2 * static_cast<int64_t>(done.size()) : own;
    if (derricksPlanned < hydrogenWanted && (derricksPlanned < 2 || workers >= target - 4 || wishes.harvest == Harvest::hydrogen)) {
        if (derricksPlanned < 2 && money < Rules::cost(SK::derrick)) return out; // Saving for it.
        if (money >= Rules::cost(SK::derrick)) {
            if (auto spot = derrickSpot(s, done)) {
                if (auto w = builder(s, *spot, busy)) {
                    out.push_back(make::build(*w, SK::derrick, *spot));
                    money -= Rules::cost(SK::derrick);
                    busy.insert(*w);
                }
            }
        }
    }

    // 3. Expand when the bases are nearly saturated or drying up.
    int64_t left = 0;
    for (int64_t i : live) left += s.patches[i].remaining;
    if (!expanding && (workers >= target - 2 - static_cast<int64_t>(8 * me.greed.value_or(0)) || left < 3000)) {
        if (auto site = nextSite(s, sim, takenSites, done[0].position, money >= 1500 || left < 3000)) {
            if (money >= Rules::cost(SK::citadel)) {
                if (auto w = builder(s, *site, busy)) {
                    out.push_back(make::build(*w, SK::citadel, *site));
                    money -= Rules::cost(SK::citadel);
                    busy.insert(*w);
                }
            } else if (workers >= target) {
                return out; // Saving for it.
            }
        }
    }

    // 4. Prospectors (see `trainProspectors`).
    trainProspectors();

    // 4b. Tech: a Foundry once a Garrison stands (a second for mech, or
    // to answer a big ground army, on two bases), then a Spacedock once
    // a Foundry stands. MH is kept for it once the Comets are out, ore
    // once the MH is there.
    // One new production building a second, so two never take one spot.
    bool placing = false;
    const bool cometsOut = !unlocked.count(UK::comet) || have[UK::comet] >= me.comets.value_or(1);
    const int64_t foundries = !mech || garrisonDone.empty() ? 0
        : (style == Player::Style::mech || answer.longbows >= 4) && done.size() >= 2 ? 2 : 1;
    const int64_t spacedocks = foundriesDone.empty() || (!unlocked.count(UK::dropship) && !unlocked.count(UK::kestrel)) ? 0 : 1;
    const std::pair<SK, int64_t> techs[] = {{SK::foundry, foundries}, {SK::spacedock, spacedocks}};
    for (const auto& [kind, want] : techs) {
        if (!(!placing && planned(kind) < want)) continue;
        std::optional<Vec2> spot;
        for (const Structure& c : done) if ((spot = garrisonSpot(s, c, sim.navGrid()))) break;
        if (!spot) continue;
        if (hydrogen < Rules::hydrogenCost(kind)) {
            if (cometsOut) hydrogen -= Rules::hydrogenCost(kind);
        } else if (std::optional<int64_t> w; money >= Rules::cost(kind) && (w = builder(s, *spot, busy))) {
            out.push_back(make::build(*w, kind, *spot));
            busy.insert(*w);
            placing = true;
            money -= Rules::cost(kind);
            hydrogen -= Rules::hydrogenCost(kind);
        } else {
            money -= Rules::cost(kind); // Saving for it.
            hydrogen -= Rules::hydrogenCost(kind);
        }
    }

    // 5. More Garrisons, `garrisonPerBase` per base (one for mech), once the
    // Prospectors are nearly all out or ore pile up.
    const int64_t perBase = style == Player::Style::mech ? 1 : me.garrisonPerBase.value_or(2);
    if (!placing && garrisonPlanned > 0 && garrisonPlanned < ac::min(perBase * static_cast<int64_t>(done.size()), maxGarrisons)
        && ((workers >= target - 4 && !expanding) || money >= 500)
        && money >= Rules::cost(SK::garrison)) {
        std::optional<Vec2> spot;
        for (const Structure& c : done) if ((spot = garrisonSpot(s, c, sim.navGrid()))) break;
        if (spot) {
            if (auto w = builder(s, *spot, busy)) {
                out.push_back(make::build(*w, SK::garrison, *spot));
                money -= Rules::cost(SK::garrison);
                busy.insert(*w);
            }
        }
    }

    // 5b. Labs: on a Foundry once its early Fireflies are out, and
    // on Garrison (half of them for bio, one otherwise) once a Foundry
    // is on the way. The building finishes what it trains and then
    // waits for the lab.
    std::set<int64_t> clearing;
    auto lab = [&](const Structure& b) {
        if (!(hydrogen >= Rules::hydrogenCost(SK::lab) && labFits(b, s))) return;
        clearing.insert(b.id);
        if (b.training) return;
        if (money >= Rules::cost(SK::lab)) out.push_back(make::addon(b.id));
        money -= Rules::cost(SK::lab); // Paid, or saved for.
        hydrogen -= Rules::hydrogenCost(SK::lab);
    };
    const int64_t fireflies = unlocked.count(UK::firefly) ? me.fireflies.value_or(0) : 0;
    for (const Structure& f : foundriesDone) {
        if (!(!f.addon && unlocked.count(UK::longbow) && have[UK::firefly] >= fireflies
              && have[UK::hailstorm] >= answer.hailstorms)) {
            continue;
        }
        lab(f);
    }
    // Kestrels: a few for harass and mech, more while the enemy is
    // short of anti-air; they need the Spacedock's Lab.
    const int64_t kestrels = !unlocked.count(UK::kestrel) ? 0
        : (style == Player::Style::harass ? 3 : style == Player::Style::mech ? 2 : 0) + answer.kestrels;
    if (kestrels > 0) {
        std::optional<Structure> dock;
        for (const Structure& b : spacedocksDone) if (!b.addon) { dock = b; break; }
        if (dock && have[UK::dropship] >= ac::min<int64_t>(1, me.drops == true ? 1 : 0)) lab(*dock);
    }
    const int64_t labsWanted = style == Player::Style::bio ? (static_cast<int64_t>(garrisonDone.size()) + 1) / 2 : 1;
    if (unlocked.count(UK::juggernaut) && planned(SK::foundry) > 0
        && countIf(garrison, [](const Structure& b) { return b.addon.has_value(); }) < labsWanted) {
        if (auto b = minBy(filtered(garrisonDone, [&](const Structure& g) { return !g.addon && labFits(g, s); }),
                           [](const Structure& x, const Structure& y) { return x.id < y.id; })) {
            lab(*b);
        }
    }

    // 6. Soldiers with what is left, a deeper queue when ore pile
    // up. The Spacedock and Foundry get the MH first: Dropships (about
    // one per 8 bio supply), Longbows once a Foundry has its lab
    // (Fireflies before that, for runbys); then the Garrison: its
    // Comets (they raid), Juggernauts mixed in where there is a lab,
    // Rangers otherwise. How many Longbows, Fireflies, Juggernauts,
    // Hailstorms (first at the Foundry) and Kestrels (at the
    // Spacedock, after its Dropships) also answers the enemy army it
    // has seen (`counters`).
    int64_t soldiers = army;
    const int64_t cap = armyCap(s);
    auto train = [&](const Structure& b, UK kind) {
        if (!(unlocked.count(kind) && soldiers < cap && money >= Rules::cost(kind) && hydrogen >= Rules::hydrogenCost(kind)
              && supplyFree >= Rules::supply(kind))) {
            return;
        }
        out.push_back(make::train(b.id, kind));
        money -= Rules::cost(kind);
        hydrogen -= Rules::hydrogenCost(kind);
        supplyFree -= Rules::supply(kind);
        soldiers += Rules::supply(kind);
        have[kind] += 1;
    };
    const int64_t bioSupply = armySupply(filtered(s.units, [&](const Unit& u) {
        return u.owner == player && (u.kind == UK::ranger || u.kind == UK::juggernaut);
    }));
    const int64_t dropships = ac::min<int64_t>(4, ac::max<int64_t>(me.drops == true ? 1 : 0, bioSupply / 8));
    const int64_t tanks = (style == Player::Style::mech ? 12 : 2) + answer.longbows;
    const int64_t perJuggernaut = answer.perJuggernaut.value_or(style == Player::Style::bio ? 2 : 3);
    // Atlases: one per 40 supply of army for mech (up to 3), else one once
    // the army is big and the bank high. Scorpions: two at each base, one at each choke.
    const int64_t atlases = style == Player::Style::mech ? ac::min<int64_t>(3, soldiers / 40)
        : money >= 800 && soldiers >= 60 ? 1 : 0;
    // Harass: two more to carry out in a Dropship.
    const int64_t scorpions = 2 * static_cast<int64_t>(done.size()) + static_cast<int64_t>(chokes(sim).size()) + (style == Player::Style::harass ? 2 : 0);
    for (const Structure& b : producers) {
        if (clearing.count(b.id)) continue;
        if (!(b.queueCount() < (money >= 400 ? 2 : 1))) continue;
        std::optional<Structure> addon;
        if (b.addon) {
            for (const Structure& x : s.structures) if (x.id == *b.addon) { addon = x; break; }
        }
        if (addon && !addon->complete()) continue; // Raising its lab.
        switch (b.kind) {
        case SK::spacedock:
            if (have[UK::dropship] < dropships) train(b, UK::dropship);
            else if (unlocked.count(UK::peregrine) && have[UK::peregrine] < answer.peregrines) train(b, UK::peregrine);
            else if (addon && have[UK::kestrel] < kestrels) train(b, UK::kestrel);
            break;
        case SK::foundry:
            if (unlocked.count(UK::hailstorm) && have[UK::hailstorm] < answer.hailstorms) {
                train(b, UK::hailstorm);
            } else if (addon && unlocked.count(UK::atlas) && have[UK::atlas] < atlases) {
                // The capstone: mech saves for it (one per 40 supply of army, up to
                // 3); the others buy one late, when the bank is high.
                if (money >= Rules::cost(UK::atlas) && hydrogen >= Rules::hydrogenCost(UK::atlas)) train(b, UK::atlas);
                else if (style == Player::Style::mech) {
                    money -= Rules::cost(UK::atlas); // Saving for it.
                    hydrogen -= Rules::hydrogenCost(UK::atlas);
                }
            } else if (unlocked.count(UK::scorpion) && have[UK::scorpion] < scorpions && soldiers >= 12) {
                train(b, UK::scorpion);
            } else if (addon && unlocked.count(UK::longbow) && have[UK::longbow] < tanks) {
                if (hydrogen >= Rules::hydrogenCost(UK::longbow)) train(b, UK::longbow);
                else hydrogen -= Rules::hydrogenCost(UK::longbow); // Saving MH for it.
            } else if (have[UK::firefly] < fireflies + (addon && style == Player::Style::mech ? 4 : 0) + answer.fireflies) {
                train(b, UK::firefly);
            }
            break;
        default: {
            UK kind = UK::ranger;
            if (unlocked.count(UK::comet) && have[UK::comet] < me.comets.value_or(1) && hydrogen >= Rules::hydrogenCost(UK::comet)) {
                kind = UK::comet;
            } else if (addon && unlocked.count(UK::juggernaut) && hydrogen >= Rules::hydrogenCost(UK::juggernaut)
                       && have[UK::juggernaut] * perJuggernaut < have[UK::ranger]) {
                kind = UK::juggernaut;
            }
            train(b, kind);
            break;
        }
        }
    }

    // 6c. Upgrades at idle Labs, with money to spare after the
    // army (a banked AI with a maxed army spends it here).
    std::set<Upgrade> researching;
    for (const Structure& b : mine) if (b.research) researching.insert(*b.research);
    for (const Structure& l : mine) {
        if (!(l.kind == SK::lab && l.complete() && !l.research)) continue;
        if (!l.parent) continue;
        std::optional<Structure> parent;
        for (const Structure& x : mine) if (x.id == *l.parent) { parent = x; break; }
        if (!parent) continue;
        std::optional<Upgrade> up;
        for (Upgrade u : allCases<Upgrade>()) {
            if (ac::at(u) == parent->kind && !sim.has(player, u) && !researching.count(u)) { up = u; break; }
        }
        if (!up || !(money >= ac::ore(*up) + researchSpare && hydrogen >= ac::hydrogen(*up))) continue;
        out.push_back(make::research(l.id, *up));
        researching.insert(*up);
        money -= ac::ore(*up);
        hydrogen -= ac::hydrogen(*up);
    }

    return out;
}

/// The Prospectors' jobs: MH or ore (6b), and (7) away from a base
/// under attack, or from over-full bases to short ones.
std::vector<Command> Commander::workers(const Simulation& sim, const std::set<int64_t>& taken) const {
    const GameState& s = sim.state;
    const Player& me = s.players[player];
    std::set<int64_t> busy = taken;
    std::vector<Command> out;
    const auto mine = filtered(s.structures, [&](const Structure& b) { return b.owner == player; });
    const auto myProspectors = filtered(s.units, [&](const Unit& u) { return u.kind == Unit::Kind::prospector && u.owner == player; });
    const auto derricks = filtered(mine, [](const Structure& b) { return b.kind == Structure::Kind::derrick; });
    // Live patches of its bases with a Citadel.
    std::set<int64_t> owned;
    for (const Structure& b : mine) if (b.kind == Structure::Kind::citadel) owned.insert(sim.site(b));
    std::vector<int64_t> live;
    for (int64_t i = 0; i < static_cast<int64_t>(s.patches.size()); i++) {
        if (s.patches[i].remaining > 0 && owned.count(sim.patchBase[i])) live.push_back(i);
    }

    // 6b. MH: ore come first. Prospectors go on MH (three per Derrick
    // with MH left) only beyond the first `minersBeforeHydrogen` on the
    // ore lines, and none while MH piles up and ore do not;
    // the ones over that come off MH and mine.
    const auto hydrogenProspectors = filtered(myProspectors, [&](const Unit& u) {
        return !u.order && u.task != Unit::Task::building && u.task != Unit::Task::toBuild
            && u.structure && any(derricks, [&](const Structure& d) { return d.id == *u.structure; })
            && (u.task == Unit::Task::toDerrick || u.task == Unit::Task::inDerrick || u.hydrogen == true);
    });
    const int64_t miners = countIf(myProspectors, [](const Unit& u) {
        return u.patch && u.hydrogen != true && u.task != Unit::Task::toDerrick && u.task != Unit::Task::inDerrick;
    });
    // The humans' split (`Harvest`); on ore, MH a request still
    // needs is mined as usual.
    const Directives wishes = me.orders();
    const Harvest harvest = wishes.harvest == Harvest::ore && wishes.hydrogenWanted() > me.hydrogen ? Harvest::balanced : wishes.harvest;
    const bool glut = harvest == Harvest::balanced && me.hydrogen >= hydrogenGlut && me.hydrogen > 4 * ac::max<int64_t>(me.ore, 50);
    const int64_t hydrogenCount = static_cast<int64_t>(hydrogenProspectors.size());
    const int64_t hydrogenRoom = harvest == Harvest::ore || glut || live.empty() ? 0
        : harvest == Harvest::hydrogen ? Rules::workersPerWell * static_cast<int64_t>(derricks.size())
        : ac::max<int64_t>(0, miners + hydrogenCount
                                   - ac::min<int64_t>(minersBeforeHydrogen, static_cast<int64_t>(live.size()) * workersPerPatch));
    if (hydrogenCount > hydrogenRoom && !live.empty()) {
        // Off MH: the ones on their way in, or carrying MH out (they
        // drop it first), to the patches with the fewest miners.
        const auto n = sim.assigned();
        auto patches = sorted(live, [&](int64_t a, int64_t b) { return lookup(n, a) < lookup(n, b); });
        const auto off = sorted(filtered(hydrogenProspectors, [&](const Unit& u) {
            return !busy.count(u.id) && u.task != Unit::Task::inDerrick;
        }), [](const Unit& a, const Unit& b) {
            return std::make_tuple(a.task == Unit::Task::toDerrick ? 0 : 1, a.id)
                < std::make_tuple(b.task == Unit::Task::toDerrick ? 0 : 1, b.id);
        });
        for (const Unit& u : prefix(off, hydrogenCount - hydrogenRoom)) {
            std::optional<size_t> k;
            auto cost = [&](size_t i) {
                return distance(s.patches[patches[i]].position, u.position) + 4 * static_cast<double>(lookup(n, patches[i]));
            };
            for (size_t i = 0; i < patches.size(); i++) if (!k || cost(i) < cost(*k)) k = i;
            if (!k) break;
            out.push_back(make::gather(u.id, patches[*k]));
            busy.insert(u.id);
            const int64_t moved = patches[*k];
            patches.erase(patches.begin() + static_cast<std::ptrdiff_t>(*k));
            patches.push_back(moved); // Spread them out.
        }
    } else {
        int64_t room = hydrogenRoom - hydrogenCount;
        for (const Structure& r : derricks) {
            if (!(r.complete() && room > 0)) continue;
            bool flowing = false;
            if (s.wells) {
                for (const Well& g : *s.wells) {
                    if (distance(g.position, r.position) < 0.5 && g.remaining > 0) { flowing = true; break; }
                }
            }
            if (!flowing) continue;
            const int64_t on = countIf(hydrogenProspectors, [&](const Unit& u) { return u.structure == r.id; });
            if (!(on < Rules::workersPerWell)) continue;
            const auto free = sorted(filtered(myProspectors, [&](const Unit& u) {
                return !busy.count(u.id) && !u.order && u.carrying == 0
                    && (u.task == Unit::Task::toPatch || u.task == Unit::Task::idle)
                    && sim.reaches(u.position, r.position, Rules::radius(Structure::Kind::derrick) + 2.0);
            }), [&](const Unit& a, const Unit& b) { return distance(a.position, r.position) < distance(b.position, r.position); });
            for (const Unit& u : prefix(free, ac::min(Rules::workersPerWell - on, room))) {
                out.push_back(make::harvest(u.id, r.id));
                busy.insert(u.id);
                room -= 1;
            }
        }
    }

    // 7. Prospectors run from a base under attack; otherwise surplus Prospectors move
    // from over-full bases to short ones.
    const auto [run, threatened] = evacuate(s, sim, busy);
    out = out + (run.empty() ? transfers(s, sim, busy, threatened) : run);
    return out;
}

/// Bases with enemy ground fighters close by, and orders sending their
/// Prospectors to mine at the nearest safe base (none when there is no safe
/// base).
std::pair<std::vector<Command>, std::set<int64_t>> Commander::evacuate(const GameState& s, const Simulation& sim,
                                                                       const std::set<int64_t>& busy) const {
    const auto soldiers = filtered(s.units, [&](const Unit& u) {
        return s.hostile(u.owner, player) && u.soldier() && u.stats().hitsGround && u.task != Unit::Task::aboard;
    });
    if (pullBack) return pullWorkers(s, sim, busy, soldiers);
    if (soldiers.empty()) return {{}, {}};
    const auto bases = sim.bases(player);
    const auto danger = filtered(bases, [&](const Structure& b) {
        return any(soldiers, [&](const Unit& u) { return distance(u.position, b.position) < 13; });
    });
    if (danger.empty()) return {{}, {}};
    std::set<int64_t> threatened;
    for (const Structure& b : danger) threatened.insert(sim.site(b));
    const auto safe = filtered(bases, [&](const Structure& b) { return !threatened.count(sim.site(b)); });
    const auto refuge = minBy(safe, [&](const Structure& a, const Structure& b) {
        return router.distance(danger[0].position, a.position) < router.distance(danger[0].position, b.position);
    });
    if (!refuge) return {{}, threatened};
    const int64_t site = sim.site(*refuge);
    const auto n = sim.assigned();
    std::vector<int64_t> patchList;
    for (int64_t i = 0; i < static_cast<int64_t>(s.patches.size()); i++) {
        if (sim.patchBase[i] == site && s.patches[i].remaining > 0) patchList.push_back(i);
    }
    const auto patches = sorted(patchList, [&](int64_t a, int64_t b) { return lookup(n, a) < lookup(n, b); });
    if (patches.empty()) return {{}, threatened};
    std::vector<Command> out;
    int64_t k = 0;
    for (const Unit& u : s.units) {
        if (!(u.owner == player && u.kind == Unit::Kind::prospector && !busy.count(u.id) && !u.order
              && u.task != Unit::Task::building && u.task != Unit::Task::toBuild)) {
            continue;
        }
        if (!(u.patch && threatened.count(sim.patchBase[*u.patch]))) continue;
        out.push_back(make::gather(u.id, patches[static_cast<size_t>(k % static_cast<int64_t>(patches.size()))]));
        k += 1;
    }
    return {out, threatened};
}

/// C++ only (`pullBack`), the classic RTS worker pull: the Prospectors of a base with
/// enemy fighters on it run a short way, to a spot `fleeDistance` behind
/// their Citadel away from the enemy (an errand, `Mission::FallBack`), and
/// go back to their own patch once the base is calm: no enemy fighter within
/// `calmRadius` of it and none of the side's units by it hit for
/// `calmTime` seconds. They used to mine at another base from then on (the
/// user's report, 2026-10-06).
std::pair<std::vector<Command>, std::set<int64_t>> Commander::pullWorkers(const GameState& s, const Simulation& sim,
                                                                          const std::set<int64_t>& busy,
                                                                          const std::vector<Unit>& soldiers) const {
    const auto bases = sim.bases(player);
    std::vector<Command> out;
    std::set<int64_t> threatened;
    std::map<int64_t, Vec2> refuge; // Site -> where its workers run to.
    for (const Structure& b : bases) {
        const auto near = filtered(soldiers, [&](const Unit& u) { return distance(u.position, b.position) < 13; });
        if (near.empty()) continue;
        const int64_t site = sim.site(b);
        threatened.insert(site);
        Vec2 centroid = Vec2::zero;
        for (const Unit& u : near) centroid = centroid + u.position;
        centroid = centroid / static_cast<double>(near.size());
        Vec2 away = b.position - centroid;
        if (!(length(away) > 0.5)) {
            // On top of the Citadel: toward the nearest other base, else +Z.
            const auto other = minBy(filtered(bases, [&](const Structure& o) { return o.id != b.id; }),
                                     [&](const Structure& x, const Structure& y) {
                                         return distance(x.position, b.position) < distance(y.position, b.position);
                                     });
            away = other ? other->position - b.position : Vec2(0, 1);
        }
        Vec2 to = b.position + normalize(away) * fleeDistance;
        if (sim.nav) {
            if (auto q = sim.nav->closestReachable(to, b.position)) to = *q;
        }
        refuge[site] = to;
    }
    auto siteOf = [&](const Unit& u) -> std::optional<int64_t> {
        if (u.patch && *u.patch >= 0 && *u.patch < static_cast<int64_t>(sim.patchBase.size())) return sim.patchBase[size_t(*u.patch)];
        return std::nullopt;
    };
    auto calm = [&](int64_t site) {
        const auto home = minBy(filtered(bases, [&](const Structure& b) { return sim.site(b) == site; }),
                                [](const Structure& a, const Structure& b) { return a.id < b.id; });
        if (!home) return true;
        if (any(soldiers, [&](const Unit& u) { return distance(u.position, home->position) < calmRadius; })) return false;
        return !any(s.units, [&](const Unit& u) {
            return u.owner == player && distance(u.position, home->position) < 13 && u.hitAt
                && s.time - *u.hitAt < calmTime;
        });
    };
    for (const Unit& u : s.units) {
        // (Every errand is in `busy`; a run is this function's own.)
        const bool fled = u.task == Unit::Task::errand && u.mission && u.mission->is<Mission::FallBack>();
        if (!(u.owner == player && u.kind == Unit::Kind::prospector && (fled || !busy.count(u.id)) && !u.order
              && u.task != Unit::Task::building && u.task != Unit::Task::toBuild && u.task != Unit::Task::inDerrick)) {
            continue;
        }
        const auto site = siteOf(u);
        if (fled) {
            if (!site || !threatened.count(*site)) {
                // Back to its patch once the base is calm.
                if (!site || calm(*site)) out.push_back(make::mission(u.id, std::nullopt));
                continue;
            }
            // Still under attack: run on if the enemy came round.
            const Vec2 to = refuge[*site];
            if (distance(u.mission->as<Mission::FallBack>()->at, to) > 4) out.push_back(make::mission(u.id, make::fallBack(to)));
            continue;
        }
        if (u.task == Unit::Task::errand || !site || !threatened.count(*site)) continue;
        out.push_back(make::mission(u.id, make::fallBack(refuge[*site])));
    }
    return {out, threatened};
}

// MARK: - Workers

/// The Prospector to send building: not carrying, not building, closest.
std::optional<int64_t> Commander::builder(const GameState& s, Vec2 spot, const std::set<int64_t>& busy) const {
    const auto free = filtered(s.units, [&](const Unit& u) {
        return u.kind == Unit::Kind::prospector && u.owner == player && !busy.count(u.id) && !u.order
            && u.task != Unit::Task::building && u.task != Unit::Task::toBuild && u.task != Unit::Task::inDerrick
            && u.task != Unit::Task::toDerrick && u.hydrogen != true;
    });
    auto rank = [&](const Unit& u) {
        const double d = distance(u.position, spot);
        // Prefer Prospectors that just dropped their load or are walking out empty.
        return d + (u.carrying > 0 ? 30 : 0) + (u.task == Unit::Task::mining ? 15 : 0);
    };
    const auto best = minBy(free, [&](const Unit& a, const Unit& b) { return rank(a) < rank(b); });
    if (!best) return std::nullopt;
    return best->id;
}

std::vector<Command> Commander::transfers(const GameState& s, const Simulation& sim, const std::set<int64_t>& busy,
                                          const std::set<int64_t>& avoid) const {
    const auto n = sim.assigned();
    std::set<int64_t> owned;
    for (const Structure& b : sim.bases(player)) {
        const int64_t site = sim.site(b);
        if (!avoid.count(site)) owned.insert(site);
    }
    struct Base { int64_t have; int64_t want; std::vector<int64_t> open; };
    std::map<int64_t, Base> perBase;
    for (int64_t site : owned) {
        std::vector<int64_t> patches;
        for (int64_t i = 0; i < static_cast<int64_t>(s.patches.size()); i++) {
            if (sim.patchBase[i] == site && s.patches[i].remaining > 0) patches.push_back(i);
        }
        int64_t have = 0;
        for (int64_t p : patches) have += lookup(n, p);
        const auto open = sorted(filtered(patches, [&](int64_t p) { return lookup(n, p) < workersPerPatch; }),
                                 [&](int64_t a, int64_t b) { return lookup(n, a) < lookup(n, b); });
        perBase[site] = Base{have, static_cast<int64_t>(patches.size()) * workersPerPatch, open};
    }
    std::vector<Command> out;
    for (const auto& [from, b] : perBase) {
        if (!(b.have > b.want)) continue;
        int64_t surplus = b.have - b.want;
        const auto movers = filtered(s.units, [&](const Unit& u) {
            return u.owner == player && !busy.count(u.id) && !u.order && u.carrying == 0 && u.task == Unit::Task::toPatch
                && u.patch && sim.patchBase[*u.patch] == from;
        });
        size_t m = 0;
        for (const auto& [to, t] : perBase) {
            if (!(to != from && t.have < t.want)) continue;
            auto open = t.open;
            int64_t room = t.want - t.have;
            while (surplus > 0 && room > 0 && !open.empty() && m < movers.size()) {
                const Unit& u = movers[m++];
                // Not to a base it cannot walk to (C++ only; a base walled
                // in by its buildings kept sending its workers off).
                if (!sim.reaches(u.position, s.patches[size_t(open.front())].position, Simulation::patchReach)) continue;
                out.push_back(make::gather(u.id, open.front()));
                open.erase(open.begin());
                surplus -= 1; room -= 1;
            }
        }
    }
    return prefix(out, 4);
}

// MARK: - Placement

/// The closest free base site by walking distance, away from enemy
/// buildings.
/// The nearest free base site. Contested sites (about as far from an
/// enemy start as from this player's) only when `contested`: an early
/// race for them hands the game to whoever gets there first.
std::optional<Vec2> Commander::nextSite(const GameState& s, const Simulation& sim, const std::set<int64_t>& owned, Vec2 from,
                                        bool contested) const {
    const Vec2 home = start(s);
    std::vector<Vec2> foes;
    for (int64_t i = 0; i < static_cast<int64_t>(s.players.size()); i++) if (s.hostile(i, player)) foes.push_back(start(s, i));
    std::vector<Vec2> free;
    for (int64_t i = 0; i < static_cast<int64_t>(sim.sites.size()); i++) {
        bool hasOre = false;
        for (size_t p = 0; p < s.patches.size(); p++) {
            if (sim.patchBase[p] == i && s.patches[p].remaining > 0) { hasOre = true; break; }
        }
        if (owned.count(i) || !hasOre
            || any(s.structures, [&](const Structure& b) { return b.owner != player && distance(b.position, sim.sites[i]) < 16; })) {
            continue;
        }
        if (!contested) {
            const double mineDistance = router.distance(home, sim.sites[i]);
            bool far = true;
            for (Vec2 f : foes) if (!(router.distance(f, sim.sites[i]) > 1.5 * mineDistance)) { far = false; break; }
            if (!far) continue;
        }
        free.push_back(sim.sites[i]);
    }
    return minBy(free, [&](Vec2 a, Vec2 b) { return router.distance(from, a) < router.distance(from, b); });
}

/// A free well at one of its finished bases, the start base first (a
/// tie goes to the one nearer the front, the same way on both halves of
/// a mirrored map).
std::optional<Vec2> Commander::derrickSpot(const GameState& s, const std::vector<Structure>& done) const {
    const Vec2 home = start(s);
    const std::vector<Well> all = s.wells.value_or(std::vector<Well>{});
    const auto wells = filtered(all, [&](const Well& g) {
        return g.remaining > 0 && any(done, [&](const Structure& b) { return distance(b.position, g.position) < 12; })
            && !any(s.structures, [&](const Structure& b) { return distance(b.position, g.position) < 0.5; })
            && !any(s.units, [&](const Unit& u) { return u.order && distance(u.order->position, g.position) < 0.5; });
    });
    auto rank = [&](const Well& g) { return distance(g.position, home) + 0.001 * distance(g.position, map.front()); };
    const auto best = minBy(wells, [&](const Well& a, const Well& b) { return rank(a) < rank(b); });
    if (!best) return std::nullopt;
    return best->position;
}

/// A 2x2 spot behind a Citadel, clear of its ore line, other
/// buildings, resources, doodads and ramps, on flat ground of its level.
std::optional<Vec2> Commander::habDomeSpot(const GameState& s, const Structure& citadel, const NavGrid* nav) const {
    std::vector<Vec2> lineP;
    for (const auto& m : s.patches) if (distance(m.position, citadel.position) < 10) lineP.push_back(m.position);
    Vec2 line(0, 1);
    if (!lineP.empty()) {
        Vec2 sum = Vec2::zero;
        for (Vec2 p : lineP) sum = sum + p;
        line = normalize(sum / static_cast<double>(lineP.size()) - citadel.position);
    }
    const auto base = minBy(map.bases, [&](const auto& a, const auto& b) {
        return distance(a.center, citadel.position) < distance(b.center, citadel.position);
    });
    const std::vector<Vec2> wells = base ? base->wells : std::vector<Vec2>{};
    const int64_t level = field.level(citadel.position);
    const double height = field.height(citadel.position);
    const Vec2 origin(rounded(citadel.position.x), rounded(citadel.position.y));
    // Within 12 cells first; once production buildings have taken
    // those, out to 18 and on the 1-cell grid, still off the ore line.
    const std::pair<int, int> passes[] = {{12, 2}, {18, 1}};
    for (const auto& [reach, step] : passes) {
        std::vector<std::pair<Vec2, double>> spots;
        for (int dx = -reach; dx <= reach; dx += step) {
            for (int dz = -reach; dz <= reach; dz += step) {
                const Vec2 p = origin + Vec2(static_cast<double>(dx), static_cast<double>(dz));
                const double d = distance(p, citadel.position);
                if (!(d >= 5 && dot(normalize(p - citadel.position), line) < 0.1)) continue;
                if (!clear(p, 1, s, wells, level, height)) continue;
                // Closest to the Citadel, a little toward its back.
                // A tie goes to the spot farther from the front, the same
                // way on both sides of a mirrored map.
                const double score = d - 2 * dot(normalize(p - citadel.position), -line) - 0.001 * distance(p, map.front());
                spots.emplace_back(p, score);
            }
        }
        if (auto best = openSpot(spots, Rules::radius(Structure::Kind::habDome), s, nav)) return best;
    }
    return std::nullopt;
}

/// The best-scoring of `spots` (the first of equals, as a scan keeping the
/// lowest score) where a building of half side `half` (and its Lab, with
/// `lab`) leaves the ground about it joined (`NavGrid::cuts`, C++ only,
/// `keepOpen`).
std::optional<Vec2> Commander::openSpot(std::vector<std::pair<Vec2, double>> spots, double half, const GameState& s,
                                        const NavGrid* nav, bool lab) const {
    std::stable_sort(spots.begin(), spots.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    if (!(keepOpen && nav)) {
        if (spots.empty()) return std::nullopt;
        return spots.front().first;
    }
    std::vector<NavGrid::Box> pending;
    for (const Unit& u : s.units) {
        if (u.order) pending.emplace_back(u.order->position, Rules::radius(u.order->kind));
    }
    for (const auto& [p, score] : spots) {
        std::vector<NavGrid::Box> boxes{{p, half}};
        if (lab) boxes.emplace_back(p + Rules::addonOffset, 1.0);
        if (!nav->cuts(boxes, pending)) return p;
    }
    return std::nullopt;
}

/// A 3x3 spot for a Garrison, Foundry or Spacedock beside or behind a
/// Citadel, out of its ore line, with room in front of its
/// door (+Z) for units to walk out and room for a Lab. The lab
/// goes on the +X side on both halves of a mirrored map, so
/// the spot needs room on both sides while there are such spots: then
/// mirrored bases still pick mirrored spots. After that, room on the
/// +X side is enough.
std::optional<Vec2> Commander::garrisonSpot(const GameState& s, const Structure& citadel, const NavGrid* nav) const {
    std::vector<Vec2> lineP;
    for (const auto& m : s.patches) if (distance(m.position, citadel.position) < 10) lineP.push_back(m.position);
    Vec2 line(0, 1);
    if (!lineP.empty()) {
        Vec2 sum = Vec2::zero;
        for (Vec2 p : lineP) sum = sum + p;
        line = normalize(sum / static_cast<double>(lineP.size()) - citadel.position);
    }
    const auto base = minBy(map.bases, [&](const auto& a, const auto& b) {
        return distance(a.center, citadel.position) < distance(b.center, citadel.position);
    });
    const std::vector<Vec2> wells = base ? base->wells : std::vector<Vec2>{};
    const int64_t level = field.level(citadel.position);
    const double height = field.height(citadel.position);
    const Vec2 origin(rounded(citadel.position.x), rounded(citadel.position.y));
    // Room for the Lab on both sides first (mirrored bases pick
    // mirrored spots); once a base has none of those left, room on the
    // side the Lab goes is enough, so a full base still gets more.
    for (bool mirrored : {true, false}) {
        std::vector<std::pair<Vec2, double>> spots;
        for (int dx = -18; dx <= 18; dx += 1) {
            for (int dz = -18; dz <= 18; dz += 1) {
                const Vec2 p = origin + Vec2(static_cast<double>(dx), static_cast<double>(dz));
                const double d = distance(p, citadel.position);
                if (!(d >= 6 && dot(normalize(p - citadel.position), line) < 0)) continue;
                const std::vector<Vec2> labs = mirrored ? labSpots(p) : std::vector<Vec2>{p + Rules::addonOffset};
                if (!clear(p, 1.5, s, wells, level, height)) continue;
                if (!clear(p + Vec2(0, 2.6), 0.8, s, wells, level, height)) continue;
                bool labsFit = true;
                for (Vec2 q : labs) {
                    if (!(clear(q, 1, s, wells, level, height, true) && boxFree(q, 1, s))) { labsFit = false; break; }
                }
                if (!labsFit) continue;
                const double score = d - 1.5 * dot(normalize(p - citadel.position), -line) - 0.001 * distance(p, map.front());
                spots.emplace_back(p, score);
            }
        }
        if (auto best = openSpot(spots, Rules::radius(Structure::Kind::garrison), s, nav, true)) return best;
    }
    return std::nullopt;
}

/// Buildings that take a Lab.
const std::set<Structure::Kind>& Commander::labHolders() {
    static const std::set<Structure::Kind> holders{Structure::Kind::garrison, Structure::Kind::foundry, Structure::Kind::spacedock};
    return holders;
}

/// Where the Lab of a building at `p` goes (`Rules.addonOffset`),
/// and its mirror image on the other side.
std::vector<Vec2> Commander::labSpots(Vec2 p) {
    const Vec2 o = Rules::addonOffset;
    return {p + o, p + Vec2(-o.x, o.y)};
}

/// The simulation's own test for a Lab: no building overlaps its
/// 2x2 footprint.
bool Commander::labFits(const Structure& b, const GameState& s) const {
    const Vec2 at = b.position + Rules::addonOffset;
    return !any(s.structures, [&](const Structure& x) {
        return x.id != b.id && std::abs(x.position.x - at.x) < Rules::radius(x.kind) + 1
            && std::abs(x.position.y - at.y) < Rules::radius(x.kind) + 1;
    });
}

/// No building, or building on the way, overlaps the square of half
/// side `half` at `p`, with a little room to spare.
bool Commander::boxFree(Vec2 p, double half, const GameState& s) const {
    auto apart = [&](Vec2 q, double r) { return std::abs(q.x - p.x) >= r + half + 0.2 || std::abs(q.y - p.y) >= r + half + 0.2; };
    for (const Structure& b : s.structures) if (!apart(b.position, Rules::radius(b.kind))) return false;
    for (const Unit& u : s.units) if (u.order && !apart(u.order->position, Rules::radius(u.order->kind))) return false;
    return true;
}

/// Room for a building of footprint radius `r` at `p`: on flat ground of
/// the base's level, well inside the screens, and clear of buildings
/// (and the Lab spots beside Garrisons, Foundries and Spacedocks),
/// resources, doodads and ramps.
bool Commander::clear(Vec2 p, double r, const GameState& s, const std::vector<Vec2>& wells, int64_t level, double height,
                      bool snug) const {
    const auto b = map.bounds;
    if (!(p.x - r > b.minX + 2 && p.x + r < b.maxX - 2 && p.y - r > b.minZ + 2 && p.y + r < b.maxZ - 2)) return false;
    for (Vec2 q : {Vec2(-r, -r), Vec2(r, -r), Vec2(-r, r), Vec2(r, r), Vec2::zero}) {
        const Vec2 c = p + q * 1.2;
        if (!(field.level(c) == level && std::abs(field.height(c) - height) < 0.3)) return false;
        // Buildings stand tall: keep them well inside the screens.
        if (!map.inPlay(c, 40, [this](Vec2 x) { return field.height(x); })) return false;
    }
    for (const Structure& st : s.structures) if (distance(st.position, p) < Rules::radius(st.kind) + r + 0.8) return false;
    for (const Unit& u : s.units) {
        if (u.order && distance(u.order->position, p) < Rules::radius(u.order->kind) + r + 0.8) return false;
    }
    std::vector<Vec2> holders;
    for (const Structure& st : s.structures) if (labHolders().count(st.kind)) holders.push_back(st.position);
    for (const Unit& u : s.units) if (u.order && labHolders().count(u.order->kind)) holders.push_back(u.order->position);
    for (Vec2 h : holders) {
        for (Vec2 q : labSpots(h)) {
            if (std::abs(q.x - p.x) < r + 1.2 && std::abs(q.y - p.y) < r + 1.2) return false;
        }
    }
    // A Lab hugs its building (`snug`) and needs less room to walk
    // around it.
    const double gap = snug ? 1.5 : 2.5;
    for (const auto& m : s.patches) if (distance(m.position, p) < r + gap) return false;
    for (Vec2 g : wells) if (distance(g, p) < r + gap) return false;
    for (const auto& d : map.doodads) if (distance(d.position, p) < r + (snug ? 0.3 : 1) + d.scale) return false;
    for (const auto& rp : map.ramps) {
        // Keep ramps and their approaches open.
        const Vec2 ab = rp.high - rp.low;
        const double t = ac::min(ac::max(dot(p - rp.low, ab) / dot(ab, ab), -0.3), 1.3);
        if (distance(p, rp.low + ab * t) < rp.width / 2 + r + 2) return false;
    }
    return true;
}

} // namespace ac
