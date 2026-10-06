// The tracking record (docs/leveling.md, "Tracking"): a game's rows as plain
// values, and the pure maths of the two numbers per pick.
#include "Tracking.h"

#include "Leveling.h"
#include "Pilot.h"

#include <algorithm>
#include <cmath>

namespace ac {

namespace coding {
std::string formatISO8601(const Date& v);
}

std::string_view TrackedGame::name(Result r) {
    switch (r) {
    case Result::won: return "won";
    case Result::lost: return "lost";
    case Result::drawn: return "drawn";
    case Result::abandoned: return "abandoned";
    case Result::open: return "open";
    }
    return "open";
}

namespace {

/// The level rows of one kind: one for each level from 2 to the higher of
/// the level its XP reaches and the highest card taken, stamped when the
/// kind's record has the stamp.
void levelRows(UnitKind kind, const KindRecord& rec, std::vector<TrackedLevel>& out) {
    int64_t top = rec.level();
    for (Perk p : rec.picks) top = std::max(top, level(p));
    std::optional<double> previousPick;
    for (int64_t l = 2; l <= top; l++) {
        const auto offer = perkOffer(kind, l);
        if (!offer) continue;
        TrackedLevel row;
        row.kind = kind;
        row.level = l;
        row.one = offer->first;
        row.other = offer->second;
        for (Perk p : rec.picks) if (level(p) == l) row.picked = p;
        const Stamp* stamp = nullptr;
        for (const Stamp& s : rec.stamps) if (s.level == l) stamp = &s;
        if (stamp) {
            row.reached = stamp->at;
            row.driven = stamp->driven;
            row.pickedAt = row.picked ? stamp->picked : std::nullopt;
            if (!stamp->standing.empty()) {
                row.own = stamp->standing.front();
                Standing others;
                for (size_t i = 1; i < stamp->standing.size(); i++) others = others + stamp->standing[i];
                row.enemy = others;
            }
            if (row.pickedAt) {
                const double inLine = previousPick ? std::max(stamp->at, *previousPick) : stamp->at;
                row.waited = std::max(0.0, *row.pickedAt - inLine);
            }
        }
        previousPick = row.pickedAt;
        out.push_back(row);
    }
}

} // namespace

TrackedGame TrackedGame::make(const Session& session, const GameState& state, const std::string& build,
                              const std::string& os, Date now, bool abandoned) {
    TrackedGame g;
    g.id = session.game.value_or(session.id);
    g.session = session.id;
    g.started = session.gameStarted.value_or(session.created);
    g.map = session.mapName;
    g.mapVersion = session.mapVersion;
    g.ai = session.ai.value_or(true);
    g.signature = Leveling::signature();
    g.build = build;
    g.os = os;

    const bool over = state.endedAt.has_value();
    if (over) {
        g.result = state.winner ? (state.team(*state.winner) == state.team(Pilot::player) ? Result::won : Result::lost)
                                : Result::drawn;
        g.length = *state.endedAt;
        g.ended = Date{now.since1970 - (state.time - *state.endedAt)};
    } else {
        g.result = abandoned ? Result::abandoned : Result::open;
        g.length = state.time;
        g.ended = now;
    }

    for (size_t i = 0; i < state.players.size(); i++) {
        const Player& p = state.players[i];
        const auto index = static_cast<int64_t>(i);
        TrackedPlayer t;
        t.player = index;
        t.human = index == Pilot::player;
        t.style = std::string(rawValue(p.style.value_or(Player::Style::bio)));
        t.aggression = p.aggression;
        t.greed = p.greed.value_or(0);
        t.garrisonPerBase = p.garrisonPerBase.value_or(2);
        t.comets = p.comets.value_or(1);
        t.fireflies = p.fireflies.value_or(0);
        t.drops = p.drops.value_or(false);
        t.start = p.start.value_or(index);
        t.won = over && state.winner && state.team(index) == state.team(*state.winner);
        t.standing = state.standing(index);
        t.unitsLost = p.unitsLost.value_or(0);
        g.players.push_back(t);
    }

    if (!state.players.empty() && state.players[static_cast<size_t>(Pilot::player)].pilot) {
        const PilotRecord& pilot = *state.players[static_cast<size_t>(Pilot::player)].pilot;
        for (UnitKind k : pilot.driven()) {
            const KindRecord& rec = pilot.kinds.at(k);
            TrackedKind t;
            t.kind = k;
            t.xp = rec.xp;
            t.level = rec.level();
            t.tally = rec.tally;
            g.kinds.push_back(t);
            levelRows(k, rec, g.levels);
        }
    }
    return g;
}

// MARK: - The two numbers

namespace worth {

double pickRate(int64_t picked, int64_t took) { return took > 0 ? double(picked) / double(took) : 0.0; }

double winRate(int64_t wins, int64_t games) { return games > 0 ? double(wins) / double(games) : 0.0; }

double margin(double rate, int64_t games) {
    return games > 0 ? 1.96 * std::sqrt(rate * (1 - rate) / double(games)) : 0.0;
}

double gap(double rate, double otherRate) { return rate - otherRate; }

} // namespace worth

} // namespace ac
