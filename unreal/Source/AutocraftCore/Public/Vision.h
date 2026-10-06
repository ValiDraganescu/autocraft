// Port of Sources/GameCore/Vision.swift: `Vision` and `Sight`. `Intel` is in
// `Types.h`, the `Rules` sight radii in `Rules.h` and `Rules.cpp`, and the Simulation's fog members are in
// `Simulation+Vision.members.h`.
#pragma once

#include "SimdMath.h"
#include "Types.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace ac {

struct TerrainField;
struct NavGrid;

/// What one player sees this moment. (Swift's `[Bool]` cell arrays are
/// bytes here, 0 or 1: `std::vector<bool>` packs bits and is slow to stamp.)
struct Sight {
    /// Cells whose ground it sees (row by row, `Vision.index`).
    std::vector<uint8_t> seen;
    /// Cells within some viewer's reach, whatever their level: an air unit
    /// over them is seen.
    std::vector<uint8_t> near;
    /// The others' units and buildings it sees.
    std::set<int64_t> ids;
};

/// Fog of war, for every player alike, the humans' team and the AI's:
/// a player sees only what its units and buildings see, and remembers the
/// rest as it last saw it (`Intel`).
///
/// Sight is a circle round each unit and building (`Rules.vision`), on the
/// walking grid's half cells. A unit on the ground does not see up a cliff:
/// a cell on a higher level than the viewer's is out of sight (`tier`), so
/// high ground hides what stands on it until something gets up there (an
/// air unit sees every level). A ramp's upper half counts as the high
/// ground. A ground unit next to the watchtower sees `Rules.towerSight`
/// round it.
struct Vision {
    /// `NavGrid.cell`.
    static constexpr double cell = 0.5;
    /// The grid: the walking grid's own, so a mirrored map's cells have
    /// mirror-image twins here too.
    Vec2 origin;
    int64_t width = 0;
    int64_t height = 0;
    /// Each cell's level: its ground's height in cliff levels, rounded.
    std::vector<int8_t> tier;
    /// A watchtower: where it stands and the level it sees from.
    struct Tower { Vec2 at; int8_t tier; };
    /// The watchtowers: where they stand and the level they see from.
    std::vector<Tower> towers;

    Vision(const MapDefinition& map, const TerrainField& field, const NavGrid& nav);

    /// A watchtower put down in the playground (`Simulation.placeDoodad`).
    void addTower(Vec2 p);

    std::optional<int64_t> index(Vec2 p) const {
        const int64_t x = static_cast<int64_t>(std::floor((p.x - origin.x) / cell));
        const int64_t z = static_cast<int64_t>(std::floor((p.y - origin.y) / cell));
        if (!(x >= 0 && z >= 0 && x < width && z < height)) return std::nullopt;
        return z * width + x;
    }

    /// Swift's `static func tier(_:_:_:_:_:)` (C++ can't share the name
    /// with the `tier` array).
    static int8_t tierIn(const std::vector<int8_t>& t, Vec2 o, int64_t w, int64_t h, Vec2 p);

    /// The level a viewer at `p` sees from (Swift's `tier(at:)`).
    int8_t tierAt(Vec2 p) const;

    /// Mark what a viewer at `c` sees, `r` round, from level `level` (nil:
    /// in the air, every level): `seen` (and `explored`) where it sees the
    /// ground, `near` wherever it is in reach (an air unit there is seen).
    void stamp(Vec2 c, double r, std::optional<int8_t> level, std::vector<uint8_t>& seen, std::vector<uint8_t>& near,
               std::vector<uint64_t>& explored) const;

    /// What player `p` sees in `state` now; what it sees joins `explored`.
    /// `farther` holds the sight radius of units whose leveling picks
    /// change it (Spotter), by id; `reveal`, how close an enemy must come
    /// to see a buried Scorpion whose picks change it (Deep burrow), by id.
    /// A buried Scorpion that has not stung lately is seen only by enemies
    /// that close and by enemy Sentinels (`spots`).
    Sight sight(int64_t p, const GameState& state, std::vector<uint64_t>& explored,
                const std::map<int64_t, double>& farther = {}, const std::map<int64_t, double>& reveal = {}) const;

    /// A buried Scorpion `u` is out of sight of player `p` (and its allies):
    /// down in the ground and done reloading, with no unit or building of
    /// `p`'s side within `range` of it (edge to edge for a building), and no
    /// finished Sentinel of `p`'s side seeing that far.
    static bool hides(const Unit& u, int64_t p, const GameState& state, double range);

    /// Any cell of a building's square footprint is in `seen`.
    bool sees(const Structure& b, const std::vector<uint8_t>& seen) const;
};

} // namespace ac
