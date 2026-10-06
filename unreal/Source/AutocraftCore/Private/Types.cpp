// The small methods of the data types of Types.h: State.swift,
// MapDefinition.swift, Directives.swift and `Intel` of Vision.swift.
#include "Types.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace ac {

// MARK: - Upgrade, Stance

/// The building whose Lab researches it.
StructureKind at(Upgrade u) {
    switch (u) {
    case Upgrade::minigun: case Upgrade::aegisShield: return StructureKind::garrison;
    case Upgrade::novaIgniters: return StructureKind::foundry;
    case Upgrade::lifelineReactor: return StructureKind::spacedock;
    }
    return StructureKind::garrison;
}
int64_t ore(Upgrade u) {
    switch (u) {
    case Upgrade::minigun: return 150; case Upgrade::aegisShield: return 100;
    case Upgrade::novaIgniters: return 150; case Upgrade::lifelineReactor: return 100;
    }
    return 0;
}
int64_t hydrogen(Upgrade u) {
    switch (u) {
    case Upgrade::minigun: return 150; case Upgrade::aegisShield: return 100;
    case Upgrade::novaIgniters: return 150; case Upgrade::lifelineReactor: return 100;
    }
    return 0;
}
double time(Upgrade u) {
    switch (u) {
    case Upgrade::minigun: return 100; case Upgrade::aegisShield: return 79;
    case Upgrade::novaIgniters: return 79; case Upgrade::lifelineReactor: return 50;
    }
    return 0;
}
std::string name(Upgrade u) {
    switch (u) {
    case Upgrade::minigun: return "Mini gun";
    case Upgrade::aegisShield: return "Aegis shield";
    case Upgrade::novaIgniters: return "Nova igniters";
    case Upgrade::lifelineReactor: return "Lifeline reactor";
    }
    return "";
}

std::string title(Stance s) {
    switch (s) {
    case Stance::auto_: return "Auto";
    case Stance::aggressive: return "Aggressive";
    case Stance::hold: return "Hold";
    case Stance::allIn: return "All in";
    }
    return "";
}

// MARK: - MapDefinition.swift

/// The standard layout: eight patches in an arc 6.5–7.5 cells from the
/// base centre around `facing` (radians, 0 = +X, π/2 = +Z), alternating
/// 1800 and 900, with a well beyond each end of the arc.
BaseSite BaseSite::standard(Vec2 center, int64_t level, double facing) {
    std::vector<OreSpec> ore;
    const int64_t count = 8;
    const double spread = 1.95; // radians covered by the arc
    for (int64_t i = 0; i < count; i++) {
        const double t = static_cast<double>(i) / static_cast<double>(count - 1) - 0.5;
        const double a = facing + t * spread;
        // Inner and outer rows alternate, like a real ore line.
        const double r = i % 2 == 0 ? 7.2 : 6.6;
        const Vec2 p = center + Vec2(std::cos(a), std::sin(a)) * r;
        ore.push_back(OreSpec{p, i % 2 == 0 ? 1800 : 900, -(a + pi / 2)});
    }
    const double g1 = facing - spread / 2 - 0.62, g2 = facing + spread / 2 + 0.62;
    std::vector<Vec2> wells{center + Vec2(std::cos(g1), std::sin(g1)) * 7.0,
                            center + Vec2(std::cos(g2), std::sin(g2)) * 7.0};
    return BaseSite{center, level, ore, wells};
}

/// Where the armies meet: halfway between the feet of the ramps (or
/// between the start bases on a map with one ramp), so neither side's
/// army stands closer to the fight on a mirrored map.
Vec2 MapDefinition::front() const {
    if (ramps.size() > 1) {
        Vec2 sum = Vec2::zero;
        for (const auto& r : ramps) sum = sum + r.low;
        return sum / static_cast<double>(ramps.size());
    }
    if (starts.size() > 1) {
        Vec2 sum = Vec2::zero;
        for (int64_t s : starts) sum = sum + bases[static_cast<size_t>(s)].center;
        return sum / static_cast<double>(starts.size());
    }
    return Vec2((bounds.minX + bounds.maxX) / 2, (bounds.minZ + bounds.maxZ) / 2);
}

/// A ground point is in play: the screens show it (`inset` points clear
/// of their edges) and, on a mirrored map, its mirror image too, so both
/// halves play on the same ground.
bool MapDefinition::inPlay(Vec2 p, double inset, const std::function<double(Vec2)>& height) const {
    if (!view) return true;
    if (!view->shows(p, height(p), inset)) return false;
    if (!mirrorX) return true;
    const double m = *mirrorX;
    const Vec2 q(2 * m - p.x, p.y);
    return view->shows(q, height(q), inset);
}

// MARK: - State.swift

/// A deposit shows four sizes as it is mined out: 3 full … 0 gone.
int64_t OreDeposit::stage() const {
    if (remaining <= 0) return 0;
    const double f = static_cast<double>(remaining) / static_cast<double>(initial);
    return f > 0.66 ? 3 : f > 0.33 ? 2 : 1;
}

Structure::Structure(int64_t id_, Kind kind_, int64_t owner_, Vec2 position_, std::optional<double> buildLeft_,
                     std::optional<int64_t> builder_)
    : id(id_), kind(kind_), owner(owner_), position(position_), buildLeft(buildLeft_), builder(builder_) {
    hp = Rules::hp(kind) * (!buildLeft ? 1 : 0.1);
}

/// Units in the production queue, the one in training included (0…5).
int64_t Structure::queueCount() const {
    if (line) return static_cast<int64_t>(line->size());
    return training ? 1 : 0;
}

/// The unit in training.
std::optional<UnitKind> Structure::inTraining() const {
    if (!training) return std::nullopt;
    if (line && !line->empty()) return line->front();
    return Rules::produces(kind);
}

/// 0…1 through the unit in training; nil when idle.
std::optional<double> Structure::trainingProgress() const {
    if (!training) return std::nullopt;
    const double left = *training;
    const auto unit = inTraining();
    if (!unit) return std::nullopt;
    return 1 - left / Rules::trainTime(*unit);
}

/// 0…1 while under construction, 1 when built.
double Structure::progress() const {
    if (!buildLeft) return 1;
    return 1 - *buildLeft / Rules::buildTime(kind);
}

/// The objective whose squad it is in, if any.
std::optional<int64_t> Mission::objective() const {
    if (auto c = as<Assault>()) return c->objective;
    if (auto c = as<Hold>()) return c->objective;
    if (auto c = as<Regroup>()) return c->objective;
    return std::nullopt;
}

Unit::Unit(int64_t id_, Kind kind_, int64_t owner_, Vec2 position_, double heading_, Task task_,
           std::optional<int64_t> patch_, double timer_, int64_t carrying_, double stride_)
    : id(id_), kind(kind_), owner(owner_), position(position_), hp(Rules::hp(kind_)), heading(heading_), task(task_),
      patch(patch_), timer(timer_), carrying(carrying_), stride(stride_) {}

bool Unit::walking() const {
    if (moving) return *moving;
    return task == Task::toPatch || task == Task::toBase || task == Task::toBuild || task == Task::toRally ||
           task == Task::attackMove;
}

/// 0…1 through a cliff jump; nil on the ground.
std::optional<double> Unit::jump() const {
    if (!jumpFrom || !jumpTo) return std::nullopt;
    const Vec2 a = *jumpFrom, b = *jumpTo;
    const double total = distance(a, b);
    if (total > 1e-6) return min(1.0, distance(a, position) / total);
    return std::nullopt;
}

/// Drilling ore or welding a building: the tool is running.
bool Unit::working() const {
    return task == Task::mining || task == Task::building || (task == Task::repairing && moving != true);
}

/// A fresh session on a map: a Citadel and one Prospector on every start
/// base, one player each, sides drawn at random. `score` carries the
/// tally over from the last game, and the draw and the AIs' temperaments
/// are seeded from it.
/// `round` varies the game when the score has not moved (a drawn game).
namespace {

/// Teams (C++ only, PORTING.md "Teams"): the start of each player, from the
/// map's starts (`ring`, in order round the map) and each player's team.
/// Each team takes a run of neighbouring starts and the teams are spread
/// evenly round the ring (4v4 on eight starts: two halves; 2v2v2v2: four
/// quarters; 1v1: opposite starts). Where the runs begin, which team gets
/// which and the order inside a team are drawn from `rng`.
std::vector<int64_t> deal(const std::vector<int64_t>& ring, const std::vector<int64_t>& teams, SeededRandom& rng) {
    const int64_t n = static_cast<int64_t>(ring.size());
    std::map<int64_t, std::vector<int64_t>> members;
    for (size_t p = 0; p < teams.size(); p++) members[teams[p]].push_back(static_cast<int64_t>(p));
    std::vector<std::vector<int64_t>> groups;
    size_t widest = 0;
    for (auto& [team, ps] : members) {
        groups.push_back(ps);
        widest = std::max(widest, ps.size());
    }
    const int64_t stride = n / static_cast<int64_t>(groups.size());
    const int64_t offset = rng.random(0, n);
    rng.shuffle(groups);
    for (auto& g : groups) rng.shuffle(g);
    std::vector<int64_t> out(teams.size(), 0);
    int64_t run = 0;
    for (size_t k = 0; k < groups.size(); k++) {
        // Teams too big to spread evenly simply follow one another.
        const int64_t first = static_cast<int64_t>(widest) <= stride ? static_cast<int64_t>(k) * stride : run;
        for (size_t j = 0; j < groups[k].size(); j++) {
            out[static_cast<size_t>(groups[k][j])] = ring[static_cast<size_t>((offset + first + static_cast<int64_t>(j)) % n)];
        }
        run += static_cast<int64_t>(groups[k].size());
    }
    return out;
}

} // namespace

std::optional<std::vector<int64_t>> GameState::teams() const {
    bool some = false;
    for (const auto& p : players) some = some || p.team.has_value();
    if (!some) return std::nullopt;
    std::vector<int64_t> out;
    for (size_t p = 0; p < players.size(); p++) out.push_back(team(static_cast<int64_t>(p)));
    return out;
}

GameState GameState::new_(const MapDefinition& map, const std::optional<std::vector<int64_t>>& score_, int64_t round,
                          const std::optional<std::vector<int64_t>>& teams_) {
    int64_t id = 1;
    std::vector<OreDeposit> patches;
    for (const auto& base : map.bases) {
        for (const auto& m : base.ore) {
            patches.push_back(OreDeposit{.id = id, .position = m.position, .angle = m.angle,
                                         .initial = m.amount, .remaining = m.amount});
            id += 1;
        }
    }
    // Teams play on a map's starts, never in the playground.
    const bool teamed = teams_ && !teams_->empty() && teams_->size() <= map.starts.size() && map.playground != true;
    // The playground has no start bases but still two sides.
    const size_t sides = map.playground == true ? 2 : teamed ? teams_->size() : map.starts.size();
    const std::vector<int64_t> score = score_ ? *score_ : std::vector<int64_t>(sides, 0);
    int64_t total = 0;
    for (int64_t s : score) total += s;
    SeededRandom rng(map.seed + static_cast<uint64_t>(total + round * 1000) * 7919);
    std::vector<Player> players;
    std::vector<Structure> structures;
    std::vector<Unit> units;
    std::vector<int64_t> starts = map.starts;
    if (starts.size() > 2) {
        // A map for more players (C++ only): each team on its own ground.
        std::vector<int64_t> each;
        for (size_t p = 0; p < starts.size(); p++) each.push_back(static_cast<int64_t>(p));
        starts = deal(map.starts, teamed ? *teams_ : each, rng);
    } else if (starts.size() == 2 && rng.unit() < 0.5) {
        std::swap(starts[0], starts[1]);
    }
    if (teamed) starts.resize(teams_->size());
    for (size_t owner = 0; owner < starts.size(); owner++) {
        const int64_t b = starts[owner];
        const BaseSite& start = map.bases[static_cast<size_t>(b)];
        structures.push_back(Structure(id, StructureKind::citadel, static_cast<int64_t>(owner), start.center)); id += 1;
        // The Prospector starts at the Citadel's edge nearest the ore line.
        Vec2 sum = Vec2::zero;
        for (const auto& o : start.ore) sum = sum + o.position;
        const Vec2 oreCenter = sum / static_cast<double>(max(static_cast<int64_t>(start.ore.size()), int64_t(1)));
        const Vec2 toward = normalize(oreCenter - start.center);
        units.push_back(Unit(id, UnitKind::prospector, static_cast<int64_t>(owner),
                             start.center + toward * Rules::depositRange, std::atan2(toward.y, toward.x),
                             Unit::Task::idle));
        id += 1;
        Player p{.aggression = rng.unit()};
        p.greed = rng.unit();
        p.garrisonPerBase = 1 + min(static_cast<int64_t>(rng.unit() * 3), int64_t(2));
        p.comets = 1 + min(static_cast<int64_t>(rng.unit() * 2), int64_t(1));
        p.start = b;
        players.push_back(p);
    }
    if (map.playground == true) players = {Player(), Player()};
    if (teamed) {
        for (size_t p = 0; p < players.size(); p++) players[p].team = (*teams_)[p];
    }
    // Tactics, drawn after the rest so the older draws stay as they were.
    for (auto& player : players) {
        const double r = rng.unit();
        const Player::Style style = r < 0.5 ? Player::Style::bio : r < 0.75 ? Player::Style::mech : Player::Style::harass;
        const int64_t pack = min(static_cast<int64_t>(rng.unit() * 3), int64_t(2));
        player.style = style;
        player.fireflies = style == Player::Style::harass ? 4 + pack : pack;
        player.drops = rng.unit() < (style == Player::Style::bio ? 0.7 : 0.4);
    }
    GameState state{.players = players, .time = 0, .patches = patches, .structures = structures, .units = units,
                    .nextID = id, .winner = std::nullopt, .endedAt = std::nullopt, .score = score};
    std::vector<Well> wells;
    for (const auto& base : map.bases)
        for (const Vec2& w : base.wells) wells.push_back(Well{.position = w, .remaining = Rules::wellHydrogen});
    state.wells = wells;
    return state;
}

std::optional<Structure> GameState::structure(int64_t id) const {
    for (const auto& s : structures) if (s.id == id) return s;
    return std::nullopt;
}

std::optional<Unit> GameState::unit(int64_t id) const {
    for (const auto& u : units) if (u.id == id) return u;
    return std::nullopt;
}

namespace {
template <class... Args> std::string format(const char* f, Args... args) {
    char buf[512];
    std::snprintf(buf, sizeof buf, f, args...);
    return buf;
}
} // namespace

/// Where a game on a map mirrored across `map.mirrorX` stops being its own
/// mirror image (nil while it still is): the first thing one side has that
/// the other has no mirrored twin of.
std::optional<std::string> GameState::mirrorBreak(const MapDefinition& map) const {
    const GameState& s = *this;
    if (!map.mirrorX) return std::nullopt;
    const double axis = 2 * *map.mirrorX;
    if (s.players.size() != 2) return std::nullopt;
    const int64_t west = s.players[0].start == map.starts[0] ? 0 : 1;
    auto twin = [axis](Vec2 p) { return Vec2(axis - p.x, p.y); };
    auto twinOf = [&s, &twin](const Structure& st) -> const Structure* {
        for (const auto& o : s.structures)
            if (o.owner != st.owner && o.kind == st.kind && distance(o.position, twin(st.position)) < 0.3) return &o;
        return nullptr;
    };
    auto byID = [&s](int64_t id) -> const Structure* {
        for (const auto& o : s.structures) if (o.id == id) return &o;
        return nullptr;
    };
    std::vector<const Structure*> ordered;
    for (const auto& st : s.structures) if (st.owner == west) ordered.push_back(&st);
    for (const auto& st : s.structures) if (st.owner != west) ordered.push_back(&st);
    for (const Structure* stp : ordered) {
        const Structure& st = *stp;
        const char* side = st.owner == west ? "west" : "east";
        if (st.kind == StructureKind::lab) {
            // A Lab stands on its building's +X side on both halves,
            // so it is not at the mirror point: its twin is
            // the lab of its building's twin.
            const Structure* parent = st.parent ? byID(*st.parent) : nullptr;
            if (parent) {
                const Structure* other = twinOf(*parent);
                const Structure* lab = other && other->addon ? byID(*other->addon) : nullptr;
                if (lab && lab->complete() == st.complete()) continue;
            }
            return format("%s lab at %.1f,%.1f has no twin (its building's twin has no lab)", side,
                          st.position.x, st.position.y);
        }
        if (!twinOf(st)) {
            const Structure* nearest = nullptr;
            for (const auto& o : s.structures) {
                if (!(o.owner != st.owner && o.kind == st.kind)) continue;
                if (!nearest || distance(o.position, twin(st.position)) < distance(nearest->position, twin(st.position)))
                    nearest = &o;
            }
            return format("%s %s at %.1f,%.1f has no twin; nearest %.1f,%.1f", side,
                          std::string(rawValue(st.kind)).c_str(), st.position.x, st.position.y,
                          nearest ? nearest->position.x : std::nan(""), nearest ? nearest->position.y : std::nan(""));
        }
    }
    for (const auto& u : s.units) {
        const char* side = u.owner == west ? "west" : "east";
        bool found = false;
        for (const auto& o : s.units)
            if (o.owner != u.owner && o.kind == u.kind && distance(o.position, twin(u.position)) < 1.0) { found = true; break; }
        if (!found) {
            const Unit* nearest = nullptr;
            for (const auto& o : s.units) {
                if (!(o.owner != u.owner && o.kind == u.kind)) continue;
                if (!nearest || distance(o.position, twin(u.position)) < distance(nearest->position, twin(u.position)))
                    nearest = &o;
            }
            auto goal = [&s](const Unit* v) -> std::string {
                if (!v || !v->patch) return "-";
                for (const auto& pp : s.patches)
                    if (pp.id == *v->patch) return format("patch %.1f,%.1f", pp.position.x, pp.position.y);
                return "-";
            };
            return format("%s %s %s at %.1f,%.1f (%s) has no twin; nearest %s at %.1f,%.1f (%s)", side,
                          std::string(rawValue(u.kind)).c_str(), std::string(rawValue(u.task)).c_str(),
                          u.position.x, u.position.y, goal(&u).c_str(),
                          nearest ? std::string(rawValue(nearest->task)).c_str() : "-",
                          nearest ? nearest->position.x : 0.0, nearest ? nearest->position.y : 0.0, goal(nearest).c_str());
        }
    }
    return std::nullopt;
}

// MARK: - Directives.swift

int64_t Request::ore() const {
    if (auto c = what.as<What::Unit>()) return Rules::cost(c->kind);
    if (auto c = what.as<What::Building>()) return Rules::cost(c->kind);
    if (auto c = what.as<What::Upgrade>()) return ac::ore(c->upgrade);
    return 0;
}

int64_t Request::hydrogen() const {
    if (auto c = what.as<What::Unit>()) return Rules::hydrogenCost(c->kind);
    if (auto c = what.as<What::Building>()) return Rules::hydrogenCost(c->kind);
    if (auto c = what.as<What::Upgrade>()) return ac::hydrogen(c->upgrade);
    return 0;
}

/// The queued MH not yet in the bank: a team on `.ore` still
/// pumps MH for what its humans asked for.
int64_t Directives::hydrogenWanted() const {
    int64_t sum = 0;
    for (const auto& r : queue) sum = sum + r.hydrogen() * max(int64_t(1), r.repeats ? int64_t(1) : r.count);
    return sum;
}

// MARK: - Vision.swift

bool Intel::isExplored(int64_t i) const {
    const int64_t word = i >> 6;
    return word >= 0 && word < static_cast<int64_t>(explored.size()) &&
           (explored[static_cast<size_t>(word)] & (uint64_t(1) << static_cast<uint64_t>(i & 63))) != 0;
}

} // namespace ac
