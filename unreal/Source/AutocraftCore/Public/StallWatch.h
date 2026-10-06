// Port of Sources/GameCore/StallWatch.swift.
#pragma once

#include "Types.h"

#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ac {

/// Notices a player whose game has stopped moving, so a stalled AI turns
/// up in the log as a bug report instead of a quiet, endless game.
/// Looked at once a second of game time (`observe`); each stall is noted
/// once, when it begins, and again only after it has cleared.
/// - bank: 1000+ ore, not spent, for `window` seconds;
/// - broke: Prospectors but no income (no ore or MH gathered) for
///   `window` seconds;
/// - retreat: the army falling back for longer than `window` seconds.
struct StallWatch {
    static constexpr double window = 60.0;
    static constexpr int64_t bank = 1000;

    enum class Kind { bank, broke, retreat };

    StallWatch() = default;

    /// Notes for the stalls that began by now (usually none).
    std::vector<std::string> observe(const GameState& s);

    std::string note(Kind kind, int64_t p, const GameState& s, double seconds) const;

private:
    /// Per player: when each condition began (nil: it does not hold).
    std::vector<std::map<Kind, double>> since;
    /// Per player: conditions already noted this episode.
    std::vector<std::set<Kind>> noted;
    /// Per player: the bank when banking began, and ore mined and the
    /// MH bank when the income stopped (to tell spending and earning from
    /// standing still).
    std::vector<int64_t> bankMark;
    struct IncomeMark { int64_t mined; int64_t hydrogen; };
    std::vector<IncomeMark> incomeMark;
    double next = -std::numeric_limits<double>::infinity();
};

} // namespace ac
