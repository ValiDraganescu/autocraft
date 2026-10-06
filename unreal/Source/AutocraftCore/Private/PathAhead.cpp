// C++ only: see PathAhead.h.
#include "PathAhead.h"

#include "Commander.h"
#include "Simulation.h"

#include <chrono>
#include <cstring>

namespace ac {

namespace {

double nowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

uint64_t bits(double v) {
    uint64_t b;
    std::memcpy(&b, &v, sizeof b);
    return b;
}

/// Everything a search reads is the same in both grids.
bool sameGrid(const NavGrid& a, const NavGrid& b) {
    return a.width == b.width && a.height == b.height && bits(a.cell) == bits(b.cell) && bits(a.origin.x) == bits(b.origin.x)
        && bits(a.origin.y) == bits(b.origin.y) && a.mirrorX == b.mirrorX && a.ground == b.ground && a.cliffs == b.cliffs
        && a.dynamic == b.dynamic && a.passable == b.passable;
}

} // namespace

size_t PathAhead::KeyHash::operator()(const Key& k) const {
    uint64_t h = 1469598103934665603ull;
    for (uint64_t v : {k.fx, k.fy, k.tx, k.ty, k.stop, uint64_t(k.jumps)}) {
        h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    }
    return size_t(h);
}

PathAhead::Key PathAhead::keyOf(Vec2 from, Vec2 target, double stopAt, bool jumps) {
    return Key{bits(from.x), bits(from.y), bits(target.x), bits(target.y), bits(stopAt), jumps};
}

PathAhead::PathAhead(int threads) {
    for (int i = 0; i < std::max(1, threads); i++) workers.emplace_back([this] { work(); });
    probeThread = std::thread([this] { runProbe(); });
}

PathAhead::~PathAhead() {
    {
        std::lock_guard<std::mutex> g(lock);
        quit = true;
        queue.clear();
    }
    stopProbe.store(true);
    wake.notify_all();
    for (auto& t : workers) t.join();
    probeThread.join();
}

std::vector<GameEvent> PathAhead::step(Simulation& sim, double dt) {
    if (!sim.nav) return sim.step(dt);
    const double t0 = nowMs();
    {
        std::lock_guard<std::mutex> g(lock);
        stats = Stats{};
        // Last step's probe still out (it stops between units): this step
        // goes without.
        if (probing) {
            stats.skipped = true;
            current = nullptr;
        }
    }
    if (stats.skipped) return sim.step(dt);
    auto round = std::make_shared<Round>();
    // The commanders only matter on a step where they think (once a game
    // second): otherwise the probe goes without (they are big to copy).
    // The same test as `step`'s.
    const bool thinks = !sim.commanders.empty() && sim.state.time + dt >= sim.nextThink;
    std::vector<Commander> commanders;
    if (!thinks) std::swap(commanders, sim.commanders);
    // Into a probe of an earlier step no one holds any more: assigning
    // reuses its arrays (a fresh copy allocates them all, and freeing one
    // costs as much).
    for (const std::shared_ptr<Simulation>& spare : spares) {
        if (spare.use_count() == 1) { round->probe = spare; break; }
    }
    if (round->probe) {
        *round->probe = sim;
    } else {
        round->probe = std::make_shared<Simulation>(sim);
        if (spares.size() < 3) spares.push_back(round->probe);
    }
    if (!thinks) std::swap(commanders, sim.commanders);
    round->probe->pathAhead = this;
    round->probe->probing = true;
    round->realNav = &*sim.nav;
    round->dt = dt;
    stats.copyMs = nowMs() - t0;
    {
        std::lock_guard<std::mutex> g(lock);
        current = round;
        probing = round;
        probeWanted = true;
        stopProbe.store(false);
        // What last step still had queued is not needed.
        queue.clear();
    }
    wake.notify_all();

    PathAhead* before = sim.pathAhead;
    sim.pathAhead = this;
    std::vector<GameEvent> events = sim.step(dt);
    sim.pathAhead = before;

    {
        std::lock_guard<std::mutex> g(lock);
        round->active = false;
        // Freed on the probe thread (a whole simulation: not on this one's time).
        retired.push_back(std::move(current));
        round = nullptr;
        queue.clear();
    }
    stopProbe.store(true);
    wake.notify_all();
    return events;
}

void PathAhead::runProbe() {
    for (;;) {
        std::shared_ptr<Round> round;
        {
            std::unique_lock<std::mutex> g(lock);
            wake.wait(g, [&] { return quit || probeWanted || !retired.empty(); });
            if (quit) return;
            if (!retired.empty()) {
                std::vector<std::shared_ptr<Round>> old = std::move(retired);
                retired.clear();
                g.unlock();
                old.clear();
                g.lock();
                if (!probeWanted) continue;
            }
            probeWanted = false;
            round = probing;
        }
        const double t0 = nowMs();
        (void)round->probe->step(round->dt);
        {
            std::lock_guard<std::mutex> g(lock);
            round->over = true;
            probing = nullptr;
            stats.probeMs = nowMs() - t0;
        }
        done.notify_all();
    }
}

void PathAhead::probeAtUnits() {
    {
        std::lock_guard<std::mutex> g(lock);
        if (probing) probing->atUnits = true;
    }
    done.notify_all();
}

void PathAhead::work() {
    for (;;) {
        std::shared_ptr<Round> round;
        Entry* e = nullptr;
        {
            std::unique_lock<std::mutex> g(lock);
            wake.wait(g, [&] { return quit || !queue.empty(); });
            if (quit) return;
            round = std::move(queue.front().first);
            e = queue.front().second;
            queue.pop_front();
            if (e->state != State::queued || !round->active) continue;
            e->state = State::running;
        }
        // The probe's grid: final while it is on its units, and the probe
        // lives as long as `round`.
        std::optional<std::vector<Vec2>> r = round->probe->nav->pathSearch(e->begin, e->from, e->target, e->stopAt, e->key.jumps);
        {
            std::lock_guard<std::mutex> g(lock);
            e->result = std::move(r);
            e->state = State::done;
            if (round->active) stats.worked += 1;
        }
        done.notify_all();
    }
}

std::optional<std::vector<Vec2>> PathAhead::guess(const NavGrid& nav, Vec2 from, Vec2 target, double stopAt, bool jumps) {
    NavGrid::PathBegin b = nav.pathBegin(from, target, stopAt, jumps);
    if (b.done) return std::move(b.result);
    bool queued = false;
    {
        std::lock_guard<std::mutex> g(lock);
        Round* round = probing.get();
        if (round && round->active && &nav == &*round->probe->nav) {
            const Key k = keyOf(from, target, stopAt, jumps);
            if (round->table.find(k) == round->table.end()) {
                Entry& e = round->entries.emplace_back();
                e.key = k;
                e.begin = std::move(b);
                e.from = from;
                e.target = target;
                e.stopAt = stopAt;
                round->table.emplace(k, &e);
                queue.emplace_back(probing, &e);
                stats.queued += 1;
                queued = true;
            }
        }
    }
    if (queued) wake.notify_one();
    // Walk straight meanwhile: the probe only finds out the questions.
    return std::vector<Vec2>{target};
}

std::optional<std::vector<Vec2>> PathAhead::find(const NavGrid& nav, Vec2 from, Vec2 target, double stopAt, bool jumps) {
    NavGrid::PathBegin b = nav.pathBegin(from, target, stopAt, jumps);
    if (b.done) return std::move(b.result);
    std::unique_lock<std::mutex> g(lock);
    const std::shared_ptr<Round> round = current;
    if (!round || &nav != round->realNav) {
        g.unlock();
        return nav.pathSearch(b, from, target, stopAt, jumps);
    }
    stats.asked += 1;
    if (!round->gridChecked) {
        // The probe's grid is final once it reaches its units.
        done.wait(g, [&] { return round->atUnits || round->over; });
        round->gridChecked = true;
        round->gridSame = round->atUnits && round->probe->nav && sameGrid(*round->probe->nav, nav);
        stats.gridDiffered = !round->gridSame;
    }
    auto it = round->gridSame ? round->table.find(keyOf(from, target, stopAt, jumps)) : round->table.end();
    if (it == round->table.end()) {
        stats.missed += 1;
        g.unlock();
        return nav.pathSearch(b, from, target, stopAt, jumps);
    }
    Entry& e = *it->second;
    if (e.state == State::queued) {
        // Not taken yet: search here, on the real grid (the same).
        e.state = State::running;
        stats.claimed += 1;
        g.unlock();
        std::optional<std::vector<Vec2>> r = nav.pathSearch(b, from, target, stopAt, jumps);
        g.lock();
        e.result = r;
        e.state = State::done;
        g.unlock();
        done.notify_all();
        return r;
    }
    if (e.state == State::running) {
        stats.waited += 1;
        done.wait(g, [&] { return e.state == State::done; });
    } else {
        stats.ready += 1;
    }
    return e.result;
}

} // namespace ac
