// Port of Sources/GameCore/Commander+Intel.swift.
#include "Commander.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace ac {

Commander::EnemyMix Commander::enemyMix(const GameState& s) const {
    EnemyMix m;
    for (const auto& b : s.structures) {
        if (s.hostile(b.owner, player) && b.kind == Structure::Kind::sentinel) { m.antiAir += 2; }
    }
    for (const auto& u : s.units) {
        if (!(s.hostile(u.owner, player) && u.soldier())) { continue; }
        const UnitStats st = u.stats();
        const int64_t n = Rules::supply(u.kind);
        m.total += n;
        if (u.kind == Unit::Kind::atlas) { m.atlases += 1; }
        if (st.hitsAir) { m.antiAir += n; }
        if (st.air) {
            m.air += n;
            if (st.hitsGround || st.hitsAir) { m.armedAir += n; }
            continue;
        }
        if (st.bio) { m.bio += n; }
        if (st.armored) { m.armored += n; }
        if (st.light) { m.light += n; }
    }
    return m;
}

Commander::Counters Commander::counters(const EnemyMix& m) const {
    Counters c;
    c.longbows = ac::min<int64_t>(8, m.ground() / 8);
    if (m.ground() >= 6 && 3 * m.armored >= m.ground()) { c.perJuggernaut = 1; }
    if (m.ground() >= 8 && 2 * m.light > m.ground()) { c.fireflies = ac::min<int64_t>(8, ac::min(m.light, m.bio) / 4); }
    const int64_t fliers = 2 * m.armedAir + (m.air - m.armedAir);
    if (fliers > 0) { c.hailstorms = ac::min<int64_t>(10, (fliers + 5) / 6); }
    if (m.armedAir > 0) { c.sentinels = m.armedAir >= 9 ? 2 : 1; }
    if (m.ground() > 0 && m.antiAir < 6) { c.kestrels = 2; }
    // Nothing an Atlas has shoots air: Kestrels answer it.
    c.kestrels += 2 * ac::min<int64_t>(2, m.atlases);
    // About one Peregrine per armed flyer (a Kestrel is 3 supply, a Peregrine 2).
    if (m.armedAir > 0) { c.peregrines = ac::min<int64_t>(8, (m.armedAir + 2) / 3); }
    return c;
}

std::optional<int64_t> Commander::focus(const GameState& s) const {
    std::set<int64_t> known;
    for (const auto& b : s.structures) { if (s.hostile(b.owner, player)) { known.insert(s.team(b.owner)); } }
    if (known.size() <= 1) { return known.empty() ? std::nullopt : std::optional<int64_t>(*known.begin()); }
    const int64_t n = static_cast<int64_t>(s.players.size());
    std::optional<std::pair<int64_t, double>> best;
    for (int64_t t : known) {
        // How far apart the two teams start: the sum over every pair.
        double apart = 0;
        for (int64_t i = 0; i < n; i++) {
            if (!s.allied(i, player)) { continue; }
            for (int64_t j = 0; j < n; j++) {
                if (s.team(j) == t) { apart += distance(start(s, i), start(s, j)); }
            }
        }
        if (!best || apart < best->second) { best = std::make_pair(t, apart); }
    }
    return best->first;
}

std::optional<Vec2> Commander::attackTarget(const Simulation& sim, Vec2 front) const {
    const GameState& s = sim.state;
    // The team its side goes for (C++, teams): with one enemy, all of theirs.
    const std::optional<int64_t> aim = focus(s);
    std::vector<Structure> theirs;
    for (const auto& b : s.structures) {
        if (s.hostile(b.owner, player) && (!aim || s.team(b.owner) == *aim)) { theirs.push_back(b); }
    }
    std::vector<Unit> soldiers;
    for (const auto& u : s.units) { if (s.hostile(u.owner, player) && u.soldier()) { soldiers.push_back(u); } }
    std::vector<Structure> bases;
    for (const auto& b : theirs) { if (b.kind == Structure::Kind::citadel) { bases.push_back(b); } }
    if (bases.empty()) {
        if (auto n = nearest(theirs, front)) { return n->position; }
        return scout(sim, front);
    }
    auto guarded = [&](Vec2 p) -> double {
        std::vector<Unit> near;
        for (const auto& u : soldiers) { if (distance(u.position, p) < guardRadius) { near.push_back(u); } }
        double crews = 0;
        for (const auto& b : theirs) {
            if (b.kind == Structure::Kind::bastion && distance(b.position, p) < guardRadius) {
                crews = crews + static_cast<double>(b.crew ? b.crew->size() : 0) * 1.5;
            }
        }
        const double guards = strength(near) + crews;
        // A base above the army is fought uphill (`uphill`).
        return field.level(p) > field.level(front) ? uphill * guards : guards;
    };
    auto score = [&](const Structure& b) {
        return router.distance(front, b.position) + guardCost * guarded(b.position);
    };
    // `min(by:)`: the first of the smallest.
    size_t best = 0;
    double bestScore = score(bases[0]);
    for (size_t i = 1; i < bases.size(); i++) {
        const double sc = score(bases[i]);
        if (sc < bestScore) { best = i; bestScore = sc; }
    }
    return bases[best].position;
}

std::optional<Vec2> Commander::regroup(const std::vector<Unit>& rangers, Vec2 front, Vec2 a) const {
    if (!(rangers.size() >= 4 && distance(front, a) > 2 * gatherRadius)) { return std::nullopt; }
    int64_t close = 0;
    for (const auto& u : rangers) { if (distance(u.position, front) < gatherRadius) { close++; } }
    if (!(static_cast<double>(close) < gatherShare * static_cast<double>(rangers.size()))) { return std::nullopt; }
    const Unit* best = &rangers[0];
    for (const auto& u : rangers) {
        if (distance(u.position, front) < distance(best->position, front)) { best = &u; }
    }
    return best->position;
}

bool Commander::gathered(const std::vector<Unit>& rangers, Vec2 p) const {
    int64_t close = 0;
    for (const auto& u : rangers) { if (distance(u.position, p) < gatherRadius) { close++; } }
    return static_cast<double>(close) >= gatherShare * static_cast<double>(rangers.size());
}

std::optional<Vec2> Commander::sentinelSpot(const GameState& s, const Structure& citadel,
                                            const std::vector<Vec2>& avoid) const {
    std::vector<Vec2> line;
    for (const auto& m : s.patches) {
        if (distance(m.position, citadel.position) < 10) { line.push_back(m.position); }
    }
    const std::optional<Vec2> air = frontDefence ? airDirection(s, citadel) : std::nullopt;
    if (line.empty()) { return habDomeSpot(s, citadel); }
    Vec2 sum = Vec2::zero;
    for (const auto& q : line) { sum = sum + q; }
    const Vec2 mid = sum / static_cast<double>(line.size());
    const BaseSite* base = nullptr;
    for (const auto& b : map.bases) {
        if (base == nullptr || distance(b.center, citadel.position) < distance(base->center, citadel.position)) { base = &b; }
    }
    const std::vector<Vec2> wells = base != nullptr ? base->wells : std::vector<Vec2>{};
    const int64_t level = field.level(citadel.position);
    const double height = field.height(citadel.position);
    auto onTheWay = [&](Vec2 p) {
        for (const auto& q : line) {
            const Vec2 ab = q - citadel.position;
            const double t = ac::max(0.0, ac::min(1.0, dot(p - citadel.position, ab) / length_squared(ab)));
            if (distance(p, citadel.position + ab * t) < 1.8) { return true; }
        }
        return false;
    };
    const Vec2 origin(rounded(mid.x), rounded(mid.y));
    std::optional<std::pair<Vec2, double>> best;
    for (int dx = -8; dx <= 8; dx++) {
        for (int dz = -8; dz <= 8; dz++) {
            const Vec2 p = origin + Vec2(static_cast<double>(dx), static_cast<double>(dz));
            const double d = distance(p, mid);
            if (!(d <= 6)) { continue; }
            if (!(distance(p, citadel.position) >= Rules::radius(Structure::Kind::citadel) + 2)) { continue; }
            if (onTheWay(p)) { continue; }
            if (std::any_of(avoid.begin(), avoid.end(), [&](Vec2 a) { return distance(a, p) < 5; })) { continue; }
            if (!clear(p, 1, s, wells, level, height)) { continue; }
            // Along the ore line, toward the enemy's air approach.
            const double score = air ? d - 0.8 * dot(p - mid, *air) - 0.001 * distance(p, map.front())
                                     : d - 0.001 * distance(p, map.front());
            if (!best || score < best->second) { best = std::make_pair(p, score); }
        }
    }
    if (best) { return best->first; }
    return habDomeSpot(s, citadel);
}

std::optional<Vec2> Commander::forwardSentinelSpot(const GameState& s, const Structure& citadel,
                                                   const std::vector<Vec2>& avoid) const {
    const auto air = airDirection(s, citadel);
    if (!air) { return std::nullopt; }
    const BaseSite* base = nullptr;
    for (const auto& b : map.bases) {
        if (base == nullptr || distance(b.center, citadel.position) < distance(base->center, citadel.position)) { base = &b; }
    }
    const std::vector<Vec2> wells = base != nullptr ? base->wells : std::vector<Vec2>{};
    const int64_t level = field.level(citadel.position);
    const double height = field.height(citadel.position);
    std::optional<std::pair<Vec2, double>> best;
    for (int k = 0; k <= 6; k++) {
        for (int sign : {1, -1}) {
            if (k == 0 && sign < 0) { continue; }
            const double a = 0.15 * k * sign;
            const Vec2 d((*air).x * std::cos(a) - (*air).y * std::sin(a), (*air).x * std::sin(a) + (*air).y * std::cos(a));
            for (double r = 5; r <= 11.5; r += 0.5) {
                const Vec2 p = citadel.position + d * r;
                if (std::any_of(avoid.begin(), avoid.end(), [&](Vec2 q) { return distance(q, p) < 5; })) { continue; }
                if (!clear(p, 1, s, wells, level, height)) { continue; }
                if (!openFront(p, d, s)) { continue; }
                const double score = std::abs(a) * 3 + std::abs(r - 7);
                if (!best || score < best->second) { best = std::make_pair(p, score); }
            }
        }
    }
    if (best) { return best->first; }
    return std::nullopt;
}

} // namespace ac
