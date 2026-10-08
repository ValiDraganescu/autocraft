// Port of Sources/GameCore/Commander.swift: the AI that plays one team.
// Its extensions (`Commander+Early.swift`, `+Intel`, `+Objectives`,
// `+Requests`) add their members through the fragments included at the end
// of the struct body; their bodies are in the `.cpp` of the same names.
#pragma once

#include "AutocraftCoreApi.h"

#include "SimdMath.h"
#include "Rules.h"
#include "Router.h"
#include "TerrainField.h"
#include "Types.h"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace ac {

// A Commander reads a Simulation, and the Simulation holds its commanders in
// a vector, which needs Commander complete: `Simulation.h` comes at the end
// of this header, once Commander is.
class Simulation;
struct NavGrid;

/// One AI player: a macro manager and army commander in the style of a
/// classic RTS AI. Once a second it looks at the state and orders its army
/// (see `army`), its raiders and drops, its scout and any Prospectors it
/// pulls against a rush (`Commander+Early.swift`), then, in priority order:
/// 1. a Hab Dome before supply runs out,
/// 2. the first Garrison once a Hab Dome stands (the tech requirement),
///    Bastions at the outer bases, and Derricks as MH is wanted,
/// 3. a Citadel on the next base once the owned bases are nearly
///    saturated or running dry,
/// 4. Prospectors from every Citadel up to saturation, then a Foundry and
///    a Spacedock,
/// 5. more Garrisons once the economy is saturated, and Labs,
/// 6. soldiers from every Garrison, Foundry and Spacedock up to the supply
///    cap its Prospectors leave (`armyCap`), in the mix its `Player.style` asks for
///    bent to answer the enemy it has seen (`counters`),
/// 7. transfers of surplus Prospectors to bases that are short.
/// Placement is checked against the terrain, resources and buildings.
struct AUTOCRAFTCORE_API Commander {
    MapDefinition map;
    TerrainField field;
    Router router;
    /// The player it plays (index into `GameState.players`).
    int64_t player = 0;
    /// Prospectors per ore deposit to aim for (2 for full saturation).
    int64_t workersPerPatch = 2;
    /// Most Prospectors the AI keeps.
    int64_t maxWorkers = 66;
    /// Most army supply the AI keeps (a Ranger 1, a tank 3); the supply
    /// cap leaves it less once its Prospectors are out (`armyCap`).
    int64_t maxArmy = Rules::maxSupply;
    /// Most Garrison: two per base up to this.
    int64_t maxGarrisons = 5;
    /// The units it may build.
    std::set<Unit::Kind> unlocked;

    explicit Commander(const MapDefinition& map_, int64_t player_ = 0, int64_t workersPerPatch_ = 2);

    /// One commander per player on the map.
    static std::vector<Commander> all(const MapDefinition& map_, int64_t workersPerPatch_ = 2);

    std::vector<Command> orders(const Simulation& sim) const;

    // MARK: - Army

    std::vector<Command> army(const Simulation& sim) const;
    /// With no enemy building known, where the army looks for one.
    std::optional<Vec2> scout(const Simulation& sim, Vec2 from) const;
    /// Fighting worth of a group, in Rangers.
    static double strength(const std::vector<Unit>& us);
    /// Army supply it keeps.
    int64_t armyCap(const GameState& s) const;
    /// Supply taken by a group.
    static int64_t armySupply(const std::vector<Unit>& us);
    /// The units in a building's production queue, the one in training first.
    static std::vector<Unit::Kind> queued(const Structure& b);

    /// An ore line not seen for this long is raided as if it were worked.
    static constexpr double lineUnseen = 30.0;
    /// Raiders worth up to this (in Rangers) are hunted by a few, not the army.
    static constexpr double raidSize = 6.0;
    /// Prospectors kept on the ore lines before any go on MH (fewer when the
    /// lines take fewer).
    static constexpr int64_t minersBeforeHydrogen = 10;
    /// MH in the bank past which, with ore short, Prospectors leave MH.
    static constexpr int64_t hydrogenGlut = 500;
    /// Ore an upgrade leaves in the bank.
    static constexpr int64_t researchSpare = 150;
    /// A retreating army turns on the enemies near it once it is worth more
    /// than this many times them; below that they chase it home.
    static constexpr double turnOdds = 2.0;
    /// Longest a retreat keeps the army home, in seconds.
    static constexpr double retreatTime = 20.0;
    /// An army worth this much (in Rangers) and `finishOdds` times what the
    /// enemy has in the field attacks whatever the wave size.
    static constexpr double finishForce = 6.0;
    static constexpr double finishOdds = 4.0;

    // MARK: - Raids

    std::vector<Command> raiders(const Simulation& sim) const;
    /// Where the units that hold a place instead of marching with the army
    /// stand: Scorpions buried two to each of its ore lines (spread either
    /// side of the line's middle) and one at each choke it holds (`chokes`), Peregrines over the base, or over the
    /// Kestrels and Dropships it has out on a raid or a drop.
    std::vector<Command> stations(const Simulation& sim) const;
    /// The chokes this AI holds, where `stations` buries one Scorpion each:
    /// the foot of each ramp on its side of each finished base (the ramp's
    /// end on the base's level and nearer to that base than to any enemy
    /// base, set a little off the slope onto its own ground), then the
    /// points of its `hold` objectives. Open ground only.
    std::vector<Vec2> chokes(const Simulation& sim) const;
    std::optional<Vec2> raidTarget(const GameState& s, const Simulation& sim, double force = 2, bool air = false) const;

    // MARK: - Drops

    std::vector<Command> drops(const Simulation& sim, const std::set<int64_t>& skip = {}) const;
    std::vector<Command> hunters(const Simulation& sim) const;
    /// Where its units were hit lately by a shooter its side could not see
    /// (`Unit::shotFrom`, within `unseenWindow` s), a spot for each cluster.
    /// A unit goes there to find it (a hunt order, in `hunters`) and a
    /// Sentinel is built by the ore line there if none sees the spot
    /// (`spending`).
    std::vector<Vec2> unseenShots(const GameState& s) const;
    static constexpr double unseenWindow = 8.0;
    /// Send the nearest unit of the army to each spot in `spots` that has
    /// none on its way; `skip`: units given another order this second.
    std::vector<Command> seekers(const Simulation& sim, const std::vector<Vec2>& spots, const std::set<int64_t>& skip) const;

    /// Where a player (this one by default) started.
    Vec2 start(const GameState& s, std::optional<int64_t> of = std::nullopt) const;
    /// The building closest to `p` by walking distance.
    std::optional<Structure> nearest(const std::vector<Structure>& structures, Vec2 to) const;
    std::vector<Command> crew(const Simulation& sim, std::set<int64_t>& busy) const;
    std::optional<Vec2> bastionSpot(const GameState& s, const Simulation& sim, const Structure& citadel) const;
    /// C++ only. Defences face the enemy: a Bastion within 60 degrees of
    /// the way to it, on open ground in front of the buildings (none if
    /// there is no such spot); a Sentinel on the side of its ore line the
    /// enemy's fliers come from, and a second one forward at heavy air.
    /// Off, the Swift placement (the goldens compare against it).
    bool frontDefence = true;
    /// No building within 4.5 cells of `p`, none within 9 ahead of it along `dir`.
    bool openFront(Vec2 p, Vec2 dir, const GameState& s) const;
    /// Unit vector from `citadel` to the nearest enemy start (straight line).
    std::optional<Vec2> airDirection(const GameState& s, const Structure& citadel) const;

    // MARK: - Economy

    std::vector<Command> economy(const Simulation& sim, const std::set<int64_t>& taken = {}) const;
    std::vector<Command> spend(const Simulation& sim, std::set<int64_t>& busy) const;
    std::vector<Command> spending(const Simulation& sim, std::set<int64_t>& busy) const;
    std::vector<Command> workers(const Simulation& sim, const std::set<int64_t>& taken) const;
    std::pair<std::vector<Command>, std::set<int64_t>> evacuate(const GameState& s, const Simulation& sim,
                                                                const std::set<int64_t>& busy) const;
    /// C++ only: Prospectors run a short way from a base under attack and
    /// go back to their patch once it is calm (`pullWorkers`). Off, the
    /// Swift game's: they mine at the nearest safe base from then on
    /// (`commanderProspectorsRunFromFireflies`; the goldens pass either way).
    bool pullBack = true;
    static constexpr double fleeDistance = 9, calmRadius = 16, calmTime = 3;
    std::pair<std::vector<Command>, std::set<int64_t>> pullWorkers(const GameState& s, const Simulation& sim,
                                                                   const std::set<int64_t>& busy,
                                                                   const std::vector<Unit>& soldiers) const;
    std::optional<int64_t> builder(const GameState& s, Vec2 spot, const std::set<int64_t>& busy) const;
    std::vector<Command> transfers(const GameState& s, const Simulation& sim, const std::set<int64_t>& busy,
                                   const std::set<int64_t>& avoid = {}) const;
    std::optional<Vec2> nextSite(const GameState& s, const Simulation& sim, const std::set<int64_t>& owned, Vec2 from,
                                 bool contested = true) const;
    std::optional<Vec2> derrickSpot(const GameState& s, const std::vector<Structure>& done) const;
    /// `nav` (C++ only): never a spot that walls ground in (`openSpot`).
    std::optional<Vec2> habDomeSpot(const GameState& s, const Structure& citadel, const NavGrid* nav = nullptr) const;
    std::optional<Vec2> garrisonSpot(const GameState& s, const Structure& citadel, const NavGrid* nav = nullptr) const;
    std::optional<Vec2> openSpot(std::vector<std::pair<Vec2, double>> spots, double half, const GameState& s,
                                 const NavGrid* nav, bool lab = false) const;
    /// C++ only: buildings never cut the ground about them in two
    /// (`NavGrid::cuts`); the user's base had its ore line walled in by
    /// Habitat Domes set corner to corner with the Citadel (2026-10-06).
    /// Off, the Swift placement (the goldens pass either way).
    bool keepOpen = true;

    /// The buildings that take a Lab.
    static const std::set<Structure::Kind>& labHolders();
    static std::vector<Vec2> labSpots(Vec2 p);
    bool labFits(const Structure& b, const GameState& s) const;
    bool boxFree(Vec2 p, double half, const GameState& s) const;
    bool clear(Vec2 p, double r, const GameState& s, const std::vector<Vec2>& wells, int64_t level, double height,
               bool snug = false) const;

    // The extensions' members.
#include "Commander+Early.members.h"
#include "Commander+Intel.members.h"
#include "Commander+Objectives.members.h"
#include "Commander+Requests.members.h"
};

} // namespace ac

// After the struct: the Simulation holds Commanders by value.
#include "Simulation.h"
