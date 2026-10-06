// `simbench FIXTURE SECONDS [--army N]`: the C++ twin of the Swift game's
// `Autocraft bench --sim SECONDS [--army N] --fixture FIXTURE`
// (`Bench.simulate` in Sources/Autocraft/Bench.swift). The fixture's
// simulation alone, both AIs on and the fog as in a window game, stepped 60
// times a game second. Prints the step time spread and the slowest steps in
// the Swift format. Built by `make -C unreal/core-tests bench` (-O3).

#include "Commander.h"
#include "Rules.h"
#include "Session.h"
#include "Simulation.h"
#include "WindowMaps.h"
#include "PathAhead.h"

#include <cstring>
#include <memory>
#include <thread>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace ac;

namespace {

[[noreturn]] void fail(const std::string& why) {
    std::fprintf(stderr, "%s\n", why.c_str());
    std::exit(1);
}

double now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/// `Bench.stageFight`: two armies of `size` face to face on open ground
/// between the two mains, placed exactly as the Swift bench places them.
void stageFight(Simulation& sim, const MapDefinition& map, int64_t size) {
    const GameState s = sim.state;
    const Vec2 home = map.bases[s.players[0].start ? *s.players[0].start : map.starts[0]].center;
    const Vec2 foe = map.bases[s.players.size() > 1 ? (s.players[1].start ? *s.players[1].start : map.starts[1]) : map.starts[1]].center;
    const Vec2 way = normalize(foe - home), side = Vec2(-way.y, way.x);
    const int64_t n = (size + 28) / 29, row = size > 29 ? 20 : 8;
    std::vector<Unit::Kind> army;
    const std::pair<Unit::Kind, int64_t> mix[] = {{Unit::Kind::ranger, 16}, {Unit::Kind::juggernaut, 4}, {Unit::Kind::firefly, 3},
                                                  {Unit::Kind::comet, 2},   {Unit::Kind::longbow, 2},     {Unit::Kind::dropship, 2}};
    for (const auto& [kind, count] : mix)
        for (int64_t i = 0; i < count * n; ++i) army.push_back(kind);
    if ((int64_t)army.size() > size) army.resize(size);
    auto places = [&](Vec2 middle, int64_t owner) {
        const Vec2 back = owner == 0 ? -way : way;
        std::vector<Vec2> out;
        for (int64_t k = 0; k < (int64_t)army.size(); ++k)
            out.push_back(middle + back * (2.5 + double(k / row) * 1.5) + side * (double(k % row) - double(row - 1) / 2) * 1.3);
        return out;
    };
    // Open ground: walkable, clear of buildings, ore and wells.
    const std::vector<Well> wells = s.wells ? *s.wells : std::vector<Well>{};
    auto open = [&](Vec2 p) {
        if (sim.nav && !sim.nav->walkable(p)) return false;
        for (const auto& b : s.structures)
            if (distance(b.position, p) < Rules::radius(b.kind) + 1) return false;
        for (const auto& o : s.patches)
            if (distance(o.position, p) < 1.5) return false;
        for (const auto& w : wells)
            if (distance(w.position, p) < 2.5) return false;
        return true;
    };
    // Swift's floating `stride(from:through:by:)`: start + i * step.
    Vec2 middle = (home + foe) / 2;
    double best = -INFINITY;
    for (int ti = 0;; ++ti) {
        const double t = 0.25 + double(ti) * 0.05;
        if (t > 0.75) break;
        for (int oi = 0;; ++oi) {
            const double o = -24.0 + double(oi) * 3;
            if (o > 24) break;
            const Vec2 c = home + (foe - home) * t + side * o;
            int64_t room = 0;
            for (int64_t owner = 0; owner < 2; ++owner)
                for (Vec2 p : places(c, owner)) room += open(p) ? 1 : 0;
            const double score = double(room) * 10 - std::abs(t - 0.5) * 10 - std::abs(o) * 0.1;
            if (score > best) { best = score; middle = c; }
        }
    }
    int64_t driver = -1;
    bool anchored = false;
    for (int64_t owner = 0; owner < 2; ++owner) {
        const double facing = owner == 0 ? std::atan2(way.y, way.x) : std::atan2(-way.y, -way.x);
        const std::vector<Vec2> ps = places(middle, owner);
        for (int64_t k = 0; k < (int64_t)ps.size(); ++k) {
            if (!open(ps[k])) continue;
            const Unit::Kind kind = army[k];
            Unit u(sim.state.nextID, kind, owner, ps[k], facing, Unit::Task::idle);
            sim.state.nextID += 1;
            if (kind == Unit::Kind::dropship) { u.energy = 120; u.cargo = std::vector<int64_t>{}; }
            if (kind == Unit::Kind::longbow) {
                u.anchor = anchored ? 0 : 1; u.anchored = !anchored; u.aim = facing;
                anchored = !anchored;
            }
            if (owner == 0 && kind == Unit::Kind::ranger && k >= 8 && driver < 0) { driver = u.id; u.hp = 10'000; }
            sim.state.units.push_back(u);
        }
        anchored = false;
    }
}

struct Sample { double ms, at; int64_t units; };

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) fail("usage: simbench FIXTURE SECONDS [--army N] [--ahead THREADS] [--hash] [--steps]");
    const std::string path = argv[1];
    const double seconds = std::atof(argv[2]);
    std::optional<int64_t> army;
    int ahead = 0;
    bool hash = false, steps = false;
    int64_t fast = 0;  // --fast [BUDGET]: fast paths (Simulation.h), a budget of searches a step
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--army" && i + 1 < argc) army = std::atoll(argv[++i]);
        else if (a == "--ahead" && i + 1 < argc) ahead = std::atoi(argv[++i]);
        else if (a == "--fast") fast = (i + 1 < argc && argv[i + 1][0] != '-') ? std::atoll(argv[++i]) : 32;
        else if (a == "--hash") hash = true;
        else if (a == "--steps") steps = true;
    }

    std::string error;
    std::optional<Session> session = SessionStore::load(path, &error);
    if (!session) fail("the fixture " + path + " does not load: " + error);
    std::optional<MapChoice> choice;
    for (MapStyle st : allCases<MapStyle>())
        for (MapSize sz : allCases<MapSize>())
            for (int64_t players : {2, 4, 8})
                if (!choice && MapChoice{st, sz, players}.name() == session->mapName) choice = MapChoice{st, sz, players};
    if (!choice) fail("no window map " + session->mapName);
    const MapDefinition map = WindowMaps::build(*choice);
    if (session->mapVersion != map.version)
        std::fprintf(stderr, "the fixture was saved on %s version %lld, now %lld\n", session->mapName.c_str(),
                     (long long)session->mapVersion, (long long)map.version);

    Simulation sim(session->state, map, Commander::all(map), true);
    if (army) {
        // The `army` shots' armies, staged on the fixture.
        stageFight(sim, map, *army);
        sim = Simulation(sim.state, map, Commander::all(map), true);
    }
    if (fast > 0) sim.setFastPaths(true, fast);
    // --ahead N: searches worked out ahead on N threads (PathAhead.h).
    std::unique_ptr<PathAhead> pathAhead;
    if (ahead > 0) pathAhead = std::make_unique<PathAhead>(ahead);
    // --hash: every unit's id, position and hp after every step, folded (FNV-1a).
    uint64_t digest = 1469598103934665603ull;
    auto fold = [&](const void* p, size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        for (size_t k = 0; k < n; k++) { digest ^= b[k]; digest *= 1099511628211ull; }
    };
    std::vector<Sample> times;
    const double start = sim.state.time, t0 = now();
    while (sim.state.time < start + seconds && !sim.state.endedAt) {
        const double t = now();
        const std::vector<GameEvent> events = pathAhead ? pathAhead->step(sim, 1.0 / 60) : sim.step(1.0 / 60);
        const double ms = (now() - t) * 1000;
        if (hash) {
            for (const Unit& u : sim.state.units) {
                fold(&u.id, sizeof u.id); fold(&u.position.x, sizeof(double)); fold(&u.position.y, sizeof(double)); fold(&u.hp, sizeof u.hp);
            }
            const size_t ne = events.size();
            fold(&ne, sizeof ne);
        }
        if (steps && (ms > 20 || std::getenv("ALLSTEPS"))) {
            std::printf("  step at %.3f s: %.1f ms", sim.state.time, ms);
            if (pathAhead) {
                const PathAhead::Stats& st = pathAhead->lastStats();
                std::printf(" | ahead: asked %lld ready %lld waited %lld claimed %lld missed %lld; queued %lld worked %lld%s; copy %.2f probe %.1f ms",
                            (long long)st.asked, (long long)st.ready, (long long)st.waited, (long long)st.claimed, (long long)st.missed,
                            (long long)st.queued, (long long)st.worked, st.gridDiffered ? " GRID DIFFERED" : "", st.copyMs, st.probeMs);
            }
            std::printf("\n");
        }
        times.push_back({ms, sim.state.time, (int64_t)sim.state.units.size()});
    }
    const double wall = now() - t0;
    std::vector<double> sorted;
    for (const auto& s : times) sorted.push_back(s.ms);
    std::sort(sorted.begin(), sorted.end());
    auto at = [&](double q) {
        return sorted.empty() ? 0.0 : sorted[std::min((size_t)(double(sorted.size()) * q), sorted.size() - 1)];
    };
    double sum = 0;
    for (double v : sorted) sum += v;
    std::printf("%s from %.1f min, %zu steps (%.0f game s) in %.2f s: mean %.2f, p50 %.2f, p95 %.2f, p99 %.2f, max %.1f ms; units %lld to %lld\n",
                session->mapName.c_str(), start / 60, times.size(), sim.state.time - start, wall, sum / double(std::max<size_t>(sorted.size(), 1)),
                at(0.5), at(0.95), at(0.99), sorted.empty() ? 0.0 : sorted.back(),
                (long long)(times.empty() ? 0 : times.front().units), (long long)(times.empty() ? 0 : times.back().units));
    std::printf("searches %lld, cells closed %.1f M\n", (long long)NavGrid::searchesRun.load(), double(NavGrid::cellsClosed.load()) / 1e6);
    if (hash) std::printf("hash %016llx\n", (unsigned long long)digest);
    std::vector<Sample> slow = times;
    std::stable_sort(slow.begin(), slow.end(), [](const Sample& a, const Sample& b) { return a.ms > b.ms; });
    for (size_t i = 0; i < std::min<size_t>(8, slow.size()); ++i)
        std::printf("  slow: %.1f ms at %.2f s (%lld units)\n", slow[i].ms, slow[i].at, (long long)slow[i].units);
    return 0;
}
