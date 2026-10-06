// Port of Sources/GameCore/Commander+Early.swift.
#include "Commander.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ac {

namespace {

const Vec2* raidPoint(const std::optional<Mission>& m) {
    if (!m) { return nullptr; }
    if (const auto* r = m->as<Mission::Raid>()) { return &r->at; }
    return nullptr;
}

const Vec2* huntPoint(const std::optional<Mission>& m) {
    if (!m) { return nullptr; }
    if (const auto* h = m->as<Mission::Hunt>()) { return &h->at; }
    return nullptr;
}

Command missionCommand(int64_t unit, std::optional<Mission> m) { return Command{Command::Mission{unit, std::move(m)}}; }

} // namespace

// MARK: - Scouting

std::vector<Command> Commander::scouts(const Simulation& sim, const std::set<int64_t>& busy) const {
    const GameState& s = sim.state;
    if (!sim.vision) { return {}; }
    std::vector<Unit> mine;
    for (const auto& u : s.units) {
        if (u.owner == player && u.kind == Unit::Kind::prospector) { mine.push_back(u); }
    }
    std::vector<Vec2> unseen;
    for (int64_t i = 0; i < static_cast<int64_t>(s.players.size()); i++) {
        if (!s.hostile(i, player)) { continue; }
        const Vec2 p = start(s, i);
        if (sim.sites.empty()) { continue; }
        int64_t site = 0;
        for (int64_t j = 1; j < static_cast<int64_t>(sim.sites.size()); j++) {
            if (distance(sim.sites[j], p) < distance(sim.sites[site], p)) { site = j; }
        }
        if (sim.lastSeen(site, player) == -std::numeric_limits<double>::infinity()) { unseen.push_back(p); }
    }
    auto scoutIt = std::find_if(mine.begin(), mine.end(), [](const Unit& u) { return raidPoint(u.mission) != nullptr; });
    if (scoutIt != mine.end()) {
        const Unit& scout = *scoutIt;
        std::optional<Vec2> next;
        for (const auto& p : unseen) {
            if (!next || distance(p, scout.position) < distance(*next, scout.position)) { next = p; }
        }
        if (!next || !(scout.hp >= 0.5 * scout.stats().hp)) { return {missionCommand(scout.id, std::nullopt)}; }
        if (const Vec2* p = raidPoint(scout.mission); p && distance(*p, *next) < 1) { return {}; }
        return {missionCommand(scout.id, Mission{Mission::Raid{*next}})};
    }
    if (!(!unseen.empty() && static_cast<int64_t>(mine.size()) >= scoutAt && s.time < scoutUntil)) { return {}; }
    const Vec2 home = start(s);
    std::vector<Unit> free;
    for (const auto& u : mine) {
        if (!busy.count(u.id) && !u.order && u.carrying == 0 && u.hydrogen != std::optional<bool>(true)
            && (u.task == Unit::Task::mining || u.task == Unit::Task::toPatch || u.task == Unit::Task::waiting
                || u.task == Unit::Task::idle)) {
            free.push_back(u);
        }
    }
    std::optional<Vec2> target;
    for (const auto& p : unseen) {
        if (!target || distance(p, home) < distance(*target, home)) { target = p; }
    }
    if (!target) { return {}; }
    const Unit* scout = nullptr;
    for (const auto& u : free) {
        if (scout == nullptr || distance(u.position, *target) < distance(scout->position, *target)) { scout = &u; }
    }
    if (scout == nullptr) { return {}; }
    return {missionCommand(scout->id, Mission{Mission::Raid{*target}})};
}

// MARK: - Pulling Prospectors

std::vector<Command> Commander::militia(const Simulation& sim, const std::set<int64_t>& busy) const {
    const GameState& s = sim.state;
    std::vector<Unit> mine, pulled, attackers, soldiers;
    std::vector<Structure> bases;
    for (const auto& u : s.units) {
        if (u.owner == player && u.kind == Unit::Kind::prospector) { mine.push_back(u); }
    }
    for (const auto& u : mine) { if (huntPoint(u.mission)) { pulled.push_back(u); } }
    for (const auto& b : s.structures) {
        if (b.owner == player && b.kind == Structure::Kind::citadel) { bases.push_back(b); }
    }
    for (const auto& u : s.units) {
        if (s.hostile(u.owner, player) && u.soldier() && !u.stats().air && u.task != Unit::Task::aboard) { attackers.push_back(u); }
    }
    for (const auto& u : s.units) {
        if (u.owner == player && u.soldier() && u.task != Unit::Task::aboard) { soldiers.push_back(u); }
    }
    // The base under the worst attack it could hold with Prospectors.
    struct Worst { Structure base; Vec2 at; double short_; };
    std::optional<Worst> worst;
    for (const auto& b : bases) {
        std::vector<Unit> near;
        for (const auto& a : attackers) { if (distance(a.position, b.position) < militiaRadius) { near.push_back(a); } }
        if (near.empty()) { continue; }
        const double threat = strength(near);
        std::vector<Unit> holding;
        for (const auto& u : soldiers) { if (distance(u.position, b.position) < militiaRadius + 4) { holding.push_back(u); } }
        const double held = strength(holding);
        if (!(threat <= militiaMax && held < threat)) { continue; }
        Vec2 sum = Vec2::zero;
        for (const auto& u : near) { sum = sum + u.position; }
        const Vec2 at = sum / static_cast<double>(near.size());
        if (!worst || threat - held > worst->short_) { worst = Worst{b, at, threat - held}; }
    }
    std::vector<Command> out;
    if (!worst) {
        for (const auto& u : pulled) { out.push_back(missionCommand(u.id, std::nullopt)); }
        return out;
    }
    const int64_t want = static_cast<int64_t>(std::ceil(worst->short_ * 2));
    int64_t kept = 0;
    for (const auto& u : pulled) {
        if (u.hp < 0.4 * u.stats().hp || kept >= want) { out.push_back(missionCommand(u.id, std::nullopt)); continue; }
        kept += 1;
        if (const Vec2* p = huntPoint(u.mission); p && distance(*p, worst->at) > 3) {
            out.push_back(missionCommand(u.id, Mission{Mission::Hunt{worst->at}}));
        }
    }
    std::vector<Unit> free;
    for (const auto& u : mine) {
        const bool working = u.task == Unit::Task::mining || u.task == Unit::Task::toPatch || u.task == Unit::Task::waiting
            || u.task == Unit::Task::idle || u.task == Unit::Task::toBase || u.task == Unit::Task::depositing;
        if (!busy.count(u.id) && !u.mission && !u.order && u.hp >= 0.6 * u.stats().hp && working
            && distance(u.position, worst->base.position) < militiaRadius + 4) {
            free.push_back(u);
        }
    }
    const Vec2 at = worst->at;
    std::stable_sort(free.begin(), free.end(),
                     [&](const Unit& a, const Unit& b) { return distance(a.position, at) < distance(b.position, at); });
    const int64_t room = ac::max<int64_t>(0, want - kept);
    for (int64_t i = 0; i < room && i < static_cast<int64_t>(free.size()); i++) {
        out.push_back(missionCommand(free[i].id, Mission{Mission::Hunt{at}}));
    }
    return out;
}

} // namespace ac
