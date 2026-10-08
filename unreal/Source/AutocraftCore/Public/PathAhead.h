// Paths worked out ahead, on other threads (C++ only; PORTING.md "Paths
// ahead"). A step that sends many units somewhere new (the AIs' orders, a
// building going up) spends nearly all its time in `NavGrid::path`, one
// search after another. `PathAhead::step(sim, dt)` runs the same step and
// gives the same result, bit for bit, but first starts a *probe*: a copy of
// the simulation stepping on another thread with every search left out (a
// unit that wants a path is told to walk straight). The probe runs ahead of
// the real step and queues each search it would have made; worker threads
// work them out on the probe's grid. When the real step asks for a path, it
// takes the probe's answer when the question (start, target, stop distance,
// jumps) is the same to the bit and the grid the same, waits for it when a
// worker is on it, and otherwise searches itself.
//
// Why the result is the same: a search reads only the grid (`passable`,
// `ground`, `dynamic`, `cliffs`, `regions`) and its question. The probe's
// grid is a copy of the real one, put through the same `refreshNav` from the
// same state; the real step checks that the two are equal before it takes
// any answer, and the probe stops after its units (before the grid changes
// again). Where the probe goes astray (its units walk straight, so later
// questions can differ), it only loses answers; the real step searches those
// itself.
#pragma once

#include "AutocraftCoreApi.h"

#include "SimdMath.h"
#include "NavGrid.h"
#include "Types.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ac {

class Simulation;

class AUTOCRAFTCORE_API PathAhead {
public:
    /// `threads` workers for the searches (at least 1), plus one for the probe.
    explicit PathAhead(int threads);
    ~PathAhead();
    PathAhead(const PathAhead&) = delete;
    PathAhead& operator=(const PathAhead&) = delete;

    /// `sim.step(dt)`, with its searches worked out ahead.
    std::vector<GameEvent> step(Simulation& sim, double dt);

    /// The last step's searches.
    struct Stats {
        /// Searches the real step asked for (past `pathBegin`'s cheap answers).
        int64_t asked = 0;
        /// Answered by a worker, ready or waited for.
        int64_t ready = 0, waited = 0;
        /// Searched by the real step: never queued, or queued and not yet taken.
        int64_t missed = 0, claimed = 0;
        /// Queued by the probe; worked out by the workers.
        int64_t queued = 0, worked = 0;
        /// The grids differed: no answer was taken.
        bool gridDiffered = false;
        /// Last step's probe was still out: this one went without.
        bool skipped = false;
        double copyMs = 0, probeMs = 0;
    };
    const Stats& lastStats() const { return stats; }

    // Called from `Simulation` (travel, step) during `step`.
    /// The real step's search.
    std::optional<std::vector<Vec2>> find(const NavGrid& nav, Vec2 from, Vec2 target, double stopAt, bool jumps);
    /// The probe's: queue the search, answer with a straight walk.
    std::optional<std::vector<Vec2>> guess(const NavGrid& nav, Vec2 from, Vec2 target, double stopAt, bool jumps);
    /// The probe reached its units: its grid is final for the step.
    void probeAtUnits();
    /// The real step is over: the probe stops (between units).
    bool probeCancelled() const { return stopProbe.load(std::memory_order_relaxed); }

private:
    struct Key {
        uint64_t fx, fy, tx, ty, stop;
        bool jumps;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash {
        size_t operator()(const Key& k) const;
    };
    enum class State : uint8_t { queued, running, done };
    struct Entry {
        Key key;
        NavGrid::PathBegin begin;
        Vec2 from, target;
        double stopAt;
        State state = State::queued;
        std::optional<std::vector<Vec2>> result;
    };
    static Key keyOf(Vec2 from, Vec2 target, double stopAt, bool jumps);
    /// One step's probe and searches. Kept alive by the workers still on
    /// one of its searches when the next step begins (no step waits for
    /// searches it no longer needs).
    struct Round {
        std::shared_ptr<Simulation> probe;
        const NavGrid* realNav = nullptr;
        double dt = 0;
        bool active = true, atUnits = false, over = false;
        bool gridChecked = false, gridSame = false;
        std::deque<Entry> entries;
        std::unordered_map<Key, Entry*, KeyHash> table;
    };

    void work();
    void runProbe();

    std::mutex lock;
    std::condition_variable wake;   // workers: a search queued, a probe to run, or quit
    std::condition_variable done;   // the real step: an answer ready, the probe at its units or over
    std::vector<std::thread> workers;
    std::thread probeThread;
    bool quit = false;
    /// Set when a step ends: its probe, if still out, stops between units.
    std::atomic<bool> stopProbe{false};

    // Under `lock`.
    std::shared_ptr<Round> current;     // the step under way
    std::shared_ptr<Round> probing;     // the probe thread's (null: idle)
    std::vector<std::shared_ptr<Round>> retired;  // done with; freed by the probe thread
    /// Probes kept to be assigned into (game thread only).
    std::vector<std::shared_ptr<Simulation>> spares;
    bool probeWanted = false;
    std::deque<std::pair<std::shared_ptr<Round>, Entry*>> queue;
    Stats stats;
};

} // namespace ac
