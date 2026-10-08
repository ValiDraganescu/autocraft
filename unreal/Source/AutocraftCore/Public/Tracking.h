// The leveling tracking record (docs/leveling.md, "Tracking"): the rows a
// game writes to the tracking database, built from a session and its state
// as plain values, and the pure maths of the two numbers per pick. No
// SQLite here: `TrackingStore.h` writes the rows.
#pragma once

#include "AutocraftCoreApi.h"

#include "Session.h"
#include "Types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ac {

/// One player in a game: whether it is the human team, its AI's draw (the
/// nearest thing to a difficulty setting; the nils are written as the
/// defaults they stand for), whether it won, and its standing at the last
/// write and the units it lost.
struct TrackedPlayer {
    int64_t player = 0;
    bool human = false;
    /// "bio", "mech" or "harass".
    std::string style = "bio";
    double aggression = 0.5;
    double greed = 0;
    int64_t garrisonPerBase = 2;
    int64_t comets = 1;
    int64_t fireflies = 0;
    bool drops = false;
    /// Index into the map's bases (nil in the state: the player's own index).
    int64_t start = 0;
    bool won = false;
    Standing standing;
    int64_t unitsLost = 0;
    bool operator==(const TrackedPlayer&) const = default;
};

/// One kind the human drove this game: seconds driven, XP, the level
/// reached and everything the driver did (`Tally`).
struct TrackedKind {
    UnitKind kind = UnitKind::prospector;
    double xp = 0;
    int64_t level = 1;
    Tally tally;
    bool operator==(const TrackedKind&) const = default;
};

/// One level reached by a kind, with a row even when no card was taken.
/// The times, the driving and the standings are nil for a level with no
/// stamp (one set by hand, or reached in a session saved before tracking).
struct TrackedLevel {
    UnitKind kind = UnitKind::prospector;
    int64_t level = 2;
    Perk one = Perk::prospectorQuickDrill;
    Perk other = Perk::prospectorLightFrame;
    /// The card taken, nil before the game ended or when none was.
    std::optional<Perk> picked;
    /// Game seconds: when the level was reached, and when the card was taken.
    std::optional<double> reached;
    std::optional<double> pickedAt;
    /// How long the card stood first in line: from the later of the level
    /// being reached and the lower level's card being taken, to its taking.
    std::optional<double> waited;
    /// Seconds driven with the kind up to the level-up.
    std::optional<double> driven;
    /// The standing at the level-up: the human's, and the other players'
    /// added up.
    std::optional<Standing> own;
    std::optional<Standing> enemy;
    bool operator==(const TrackedLevel&) const = default;
};

/// A game as it stands, to be written whole.
struct AUTOCRAFTCORE_API TrackedGame {
    enum class Result : uint8_t { won, lost, drawn, abandoned, open };

    std::string id;
    std::string session;
    Date started;
    Date ended;
    /// Game seconds: to the end for a game that is over, else so far.
    double length = 0;
    Result result = Result::open;
    std::string map;
    int64_t mapVersion = 0;
    /// Whether the team's AI was on.
    bool ai = true;
    /// `Leveling::signature()`.
    std::string signature;
    /// The app binary's date, and the macOS version.
    std::string build;
    std::string os;
    std::vector<TrackedPlayer> players;
    std::vector<TrackedKind> kinds;
    std::vector<TrackedLevel> levels;

    /// The rows of `session`'s game as `state` has it, at wall-clock `now`.
    /// The result is won or lost from `state.winner` for the human's team
    /// (`Pilot::player`), drawn when it ended with no winner, abandoned
    /// when the caller says so and it was not over, open otherwise. The
    /// end is `now` for a game still going, and for one that is over `now`
    /// less the game seconds since it ended.
    static TrackedGame make(const Session& session, const GameState& state, const std::string& build,
                            const std::string& os, Date now, bool abandoned = false);

    static std::string_view name(Result r);
    bool operator==(const TrackedGame&) const = default;
};

/// The two numbers per pick (docs/leveling.md, "The two numbers"), as the
/// `perk_balance` view works them out.
namespace worth {
/// Of the games that reached the level with the kind, the share that took
/// this card rather than the other: picked over took (0 with no game).
double pickRate(int64_t picked, int64_t took);
/// The share of the finished games it was picked in that were won.
double winRate(int64_t wins, int64_t games);
/// A 95% margin on a win rate: `1.96 × √(p(1 − p)/n)` (0 with no game).
double margin(double rate, int64_t games);
/// The gap between a pick's win rate and the other card's.
double gap(double rate, double otherRate);
} // namespace worth

} // namespace ac
