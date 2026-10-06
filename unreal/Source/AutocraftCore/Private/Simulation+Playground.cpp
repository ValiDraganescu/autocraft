// Port of Sources/GameCore/Simulation+Playground.swift.
//
// The playground (`autocraft models`): anything put down by hand, for
// either side, then played with as in a game. A unit joins as if just
// trained, a building as if just finished; ore, wells and doodads as if
// the map had them (the walking grid, the fog and the scene take them in).
#include "Simulation.h"

#include <cmath>
#include <limits>

namespace ac {

/// Put down a `kind` for `owner` at `p` (a ground unit on the nearest
/// free spot), facing `heading`, as if just trained. Its id.
int64_t Simulation::placeUnit(Unit::Kind kind, int64_t owner_, Vec2 p, double heading) {
    Unit u = freshUnit(kind, state.nextID, owner_, p, heading);
    if (!u.stats().air && nav) {
        if (auto free = nav->nearestFree(p)) u.position = *free;
    }
    state.nextID += 1;
    state.units.push_back(u);
    reindex();
    return u.id;
}

/// Why a `kind` can't be put down at `p`: on open ground, clear of
/// buildings, ore, wells and rocks (a flyer anywhere in play). Nil: it can.
std::optional<std::string> Simulation::unitRefusal(Unit::Kind kind, Vec2 p) const {
    if (!nav) return std::nullopt;
    if (!nav->standable(p)) return "Out of play";
    if (!Rules::stats(kind).air && (!nav->walkable(p) || nav->inRock(p))) return "Something is in the way";
    return std::nullopt;
}

/// A finished `kind` for `owner` at `p` (`placementRefusal` says
/// where it fits; a Lab goes on a building's add-on spot, `placeLab`).
/// Its id.
int64_t Simulation::placeStructure(Structure::Kind kind, int64_t owner_, Vec2 p) {
    const Structure s(state.nextID, kind, owner_, p);
    state.nextID += 1;
    state.structures.push_back(s);
    refreshNav();
    return s.id;
}

/// The building a Lab put down at `p` would join: one of `owner`'s
/// finished Garrisons, Foundries or Spacedocks without one, under `p`
/// or with its add-on spot there.
std::optional<Structure> Simulation::labHost(Vec2 p, int64_t owner_) const {
    std::optional<Structure> best;
    for (const auto& s : state.structures) {
        if (!((s.kind == Structure::Kind::garrison || s.kind == Structure::Kind::foundry || s.kind == Structure::Kind::spacedock)
              && s.owner == owner_ && s.complete())) continue;
        if (!(distance(s.position + Rules::addonOffset, p) < 2.5 || NavGrid::insideStructure(p, s, 0))) continue;
        if (!best || distance(s.position + Rules::addonOffset, p) < distance(best->position + Rules::addonOffset, p)) best = s;
    }
    return best;
}

/// Why no Lab can go on `host` (its add-on spot taken), or nil.
std::optional<std::string> Simulation::labRefusal(const Structure& host) const {
    if (host.addon) return "It has a Lab";
    const Vec2 at = host.position + Rules::addonOffset;
    for (const auto& s : state.structures) {
        if (s.id != host.id && std::abs(s.position.x - at.x) < Rules::radius(s.kind) + 1
            && std::abs(s.position.y - at.y) < Rules::radius(s.kind) + 1) {
            return "Something is in the way";
        }
    }
    return std::nullopt;
}

/// A finished Lab on `host`'s add-on spot, as the `.addon` order
/// builds it. Its id, or nil when it can't go there.
std::optional<int64_t> Simulation::placeLab(int64_t host) {
    std::optional<size_t> i;
    for (size_t k = 0; k < state.structures.size(); k++) if (state.structures[k].id == host) { i = k; break; }
    if (!i || labRefusal(state.structures[*i])) return std::nullopt;
    Structure lab(state.nextID, Structure::Kind::lab, state.structures[*i].owner, state.structures[*i].position + Rules::addonOffset);
    lab.parent = host;
    state.nextID += 1;
    state.structures[*i].addon = lab.id;
    state.structures.push_back(lab);
    refreshNav();
    return lab.id;
}

/// Why an ore deposit, a well or a doodad of reach `r` can't go down at
/// `p`: in play, and clear of buildings, ore, wells and rocks. Nil: it can.
std::optional<std::string> Simulation::propRefusal(Vec2 p, double r) const {
    if (!nav) return std::nullopt;
    if (!nav->standable(p)) return "Out of play";
    bool blocked = false;
    for (const auto& s : state.structures) if (NavGrid::insideStructure(p, s, r)) { blocked = true; break; }
    if (!blocked) {
        for (const auto& o : state.patches) if (o.remaining > 0 && NavGrid::insidePatch(p, o, r)) { blocked = true; break; }
    }
    if (!blocked && state.wells) {
        for (const auto& w : *state.wells) if (distance(w.position, p) < 1.5 + r) { blocked = true; break; }
    }
    if (!blocked) {
        for (const auto& rock : nav->rocks) if (distance(rock.first, p) < rock.second + r) { blocked = true; break; }
    }
    if (blocked) return "Something is in the way";
    return std::nullopt;
}

/// A full ore deposit at `p`, its long axis turned by `angle`. Its id.
int64_t Simulation::placePatch(Vec2 p, double angle) {
    const int64_t id = state.nextID;
    state.nextID += 1;
    OreDeposit o;
    o.id = id;
    o.position = p;
    o.angle = angle;
    o.initial = placedOre;
    o.remaining = placedOre;
    state.patches.push_back(o);
    std::optional<int64_t> best;
    if (map) {
        for (int64_t k = 0; k < static_cast<int64_t>(map->bases.size()); k++) {
            if (!best || distance(map->bases[size_t(k)].center, p) < distance(map->bases[size_t(*best)].center, p)) best = k;
        }
    }
    patchBase.push_back(best.value_or(0));
    refreshNav();
    return id;
}

/// A full MH well at `p` (after the map's in `GameState.wells`).
void Simulation::placeWell(Vec2 p) {
    auto wells = state.wells.value_or(std::vector<Well>{});
    Well w;
    w.position = p;
    w.remaining = Rules::wellHydrogen;
    wells.push_back(w);
    state.wells = wells;
    refreshNav();
}

/// A doodad, after the map's own (`GameState.doodads`); a tower is a
/// watchtower like the map's.
void Simulation::placeDoodad(const Doodad& d) {
    auto doodads = state.doodads.value_or(std::vector<Doodad>{});
    doodads.push_back(d);
    state.doodads = doodads;
    if (d.kind == Doodad::Kind::tower && vision) vision->addTower(d.position);
    refreshNav();
}

/// Kill unit or building `id`, as in a fight. Its name, or nil.
std::optional<std::string> Simulation::remove(int64_t id) {
    if (auto i = unitIndex(id)) {
        state.units[size_t(*i)].hp = 0;
        return title(state.units[size_t(*i)].kind);
    }
    for (auto& s : state.structures) {
        if (s.id != id) continue;
        s.hp = 0;
        return title(s.kind);
    }
    return std::nullopt;
}

/// Take away what stands at `p`, nearest first: a unit or a building
/// (killed, as in a fight), a doodad or well put down by hand, or an
/// ore deposit (mined out for good). What went, or nil for nothing.
std::optional<std::string> Simulation::removeProp(Vec2 p) {
    {
        std::optional<size_t> best;
        for (size_t i = 0; i < state.units.size(); i++) {
            const Unit& u = state.units[i];
            if (!(u.hp > 0 && u.task != Unit::Task::aboard && u.task != Unit::Task::inBastion
                  && distance(u.position, p) < u.stats().radius + 0.3)) continue;
            if (!best || distance(u.position, p) < distance(state.units[*best].position, p)) best = i;
        }
        if (best) {
            state.units[*best].hp = 0;
            return title(state.units[*best].kind);
        }
    }
    for (auto& s : state.structures) {
        if (s.hp > 0 && NavGrid::insideStructure(p, s, 0.2)) {
            s.hp = 0;
            return title(s.kind);
        }
    }
    if (state.doodads) {
        auto& doodads = *state.doodads;
        std::optional<size_t> best;
        for (size_t k = 0; k < doodads.size(); k++) {
            bool near = distance(doodads[k].position, p) < 0.8;
            if (!near) {
                for (const auto& c : NavGrid::circles(doodads[k])) if (distance(c.first, p) < c.second + 0.3) { near = true; break; }
            }
            if (!near) continue;
            if (!best || distance(doodads[k].position, p) < distance(doodads[*best].position, p)) best = k;
        }
        if (best) {
            const Doodad d = doodads[*best];
            doodads.erase(doodads.begin() + static_cast<std::ptrdiff_t>(*best));
            if (d.kind == Doodad::Kind::tower && vision) {
                std::erase_if(vision->towers, [&](const Vision::Tower& t) { return distance(t.at, d.position) < 0.01; });
            }
            refreshNav();
            return std::string(rawValue(d.kind));
        }
    }
    const int64_t mapWells = nav ? nav->mapWells : 0;
    if (state.wells) {
        const auto& wells = *state.wells;
        std::optional<size_t> k;
        for (size_t j = static_cast<size_t>(ac::max<int64_t>(mapWells, 0)); j < wells.size(); j++) {
            if (distance(wells[j].position, p) < 1.5) { k = j; break; }
        }
        if (k) {
            for (const auto& s : state.structures) if (distance(s.position, wells[*k].position) < 0.5) return std::nullopt;
            state.wells->erase(state.wells->begin() + static_cast<std::ptrdiff_t>(*k));
            refreshNav();
            return "MH well";
        }
    }
    for (auto& o : state.patches) {
        if (!(o.remaining > 0 && NavGrid::insidePatch(p, o, 0.3))) continue;
        o.remaining = 0;
        // Never grows back (`regrow`).
        o.depletedAt = std::numeric_limits<double>::infinity();
        refreshNav();
        return "Ore deposit";
    }
    return std::nullopt;
}

} // namespace ac
