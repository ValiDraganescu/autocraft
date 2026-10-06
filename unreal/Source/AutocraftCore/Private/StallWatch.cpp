// Port of Sources/GameCore/StallWatch.swift.
#include "StallWatch.h"

#include "Commander.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace ac {

/// Notes for the stalls that began by now (usually none).
std::vector<std::string> StallWatch::observe(const GameState& s) {
    if (!(s.time >= next)) return {};
    next = s.time + 1;
    const size_t n = s.players.size();
    if (since.size() != n) {
        since.assign(n, {});
        noted.assign(n, {});
        bankMark.assign(n, 0);
        incomeMark.assign(n, IncomeMark{0, 0});
    }
    std::vector<std::string> out;
    for (size_t p = 0; p < n; ++p) {
        const Player& me = s.players[p];
        // Banking: 1000+ ore, never below the bank it began with.
        if (!since[p].count(Kind::bank)) bankMark[p] = me.ore;
        const bool banking = me.ore >= bank && me.ore >= bankMark[p];
        // Broke: Prospectors, and no ore mined and no MH come in since it began.
        if (!since[p].count(Kind::broke)) incomeMark[p] = IncomeMark{me.totalMined, me.hydrogen};
        const bool prospecting = std::any_of(s.units.begin(), s.units.end(), [p](const Unit& u) {
            return u.owner == static_cast<int64_t>(p) && u.kind == Unit::Kind::prospector;
        });
        const bool broke = prospecting && me.totalMined == incomeMark[p].mined && me.hydrogen <= incomeMark[p].hydrogen;
        incomeMark[p].hydrogen = min(incomeMark[p].hydrogen, me.hydrogen);
        const std::pair<Kind, bool> conditions[] = {{Kind::bank, banking}, {Kind::broke, broke}, {Kind::retreat, me.retreating}};
        for (const auto& [kind, on] : conditions) {
            if (!on) { since[p].erase(kind); noted[p].erase(kind); continue; }
            auto it = since[p].find(kind);
            const double t0 = it != since[p].end() ? it->second : s.time;
            since[p][kind] = t0;
            if (!(s.time - t0 >= window && !noted[p].count(kind))) continue;
            noted[p].insert(kind);
            out.push_back(note(kind, static_cast<int64_t>(p), s, s.time - t0));
        }
    }
    return out;
}

std::string StallWatch::note(Kind kind, int64_t p, const GameState& s, double seconds) const {
    const Player& me = s.players[static_cast<size_t>(p)];
    std::vector<Unit> prospectors;
    for (const auto& u : s.units) {
        if (u.owner == p && u.kind == Unit::Kind::prospector) prospectors.push_back(u);
    }
    int64_t hydrogen = 0;
    for (const auto& u : prospectors) {
        if (u.task == Unit::Task::toDerrick || u.task == Unit::Task::inDerrick || u.hydrogen == true) hydrogen += 1;
    }
    std::vector<Unit> soldiers;
    for (const auto& u : s.units) {
        if (u.owner == p && u.soldier()) soldiers.push_back(u);
    }
    const int64_t army = Commander::armySupply(soldiers);
    const char* state = me.retreating ? "retreating" : me.defending == true ? "defending"
        : me.attack ? "attacking" : "at home";
    std::string what;
    switch (kind) {
    case Kind::bank: what = "has banked " + std::to_string(me.ore) + " ore unspent"; break;
    case Kind::broke: what = "has gathered nothing"; break;
    case Kind::retreat: what = "has been retreating"; break;
    }
    const std::string who = p == 0 ? "Blue" : p == 1 ? "Red" : "Player " + std::to_string(p);
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "stall: %s %s for %.0f s at %.1f min (%lld Prospectors, %lld on MH; army %lld supply, %s; ore %lld, MH %lld, supply %lld/%lld)",
                  who.c_str(), what.c_str(), seconds, s.time / 60, static_cast<long long>(prospectors.size()),
                  static_cast<long long>(hydrogen), static_cast<long long>(army), state, static_cast<long long>(me.ore),
                  static_cast<long long>(me.hydrogen), static_cast<long long>(me.supplyUsed), static_cast<long long>(me.supplyCap));
    return buf;
}

} // namespace ac
