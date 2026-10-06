// Port of Sources/GameCore/WindowMaps.swift.
#pragma once

#include "Noise.h"
#include "SimdMath.h"
#include "Types.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ac {

/// The maps of window mode: made on the ground, not fitted to screens, so a
/// window can zoom and pan over all of one. Every map is an exact mirror
/// across x = 0, like the wallpaper's, so neither side has an edge.
enum class MapStyle : uint8_t { highlands, openField, ridge, badlands };

template <> struct EnumInfo<MapStyle> {
    static constexpr std::array<std::string_view, 4> names{"highlands", "openField", "ridge", "badlands"};
};

std::string title(MapStyle s);
std::string blurb(MapStyle s);

enum class MapSize : uint8_t { small, medium, large, huge };

template <> struct EnumInfo<MapSize> {
    static constexpr std::array<std::string_view, 4> names{"small", "medium", "large", "huge"};
};

std::string title(MapSize s);

/// `(width: Double, depth: Double)`.
struct MapCells { double width, depth; };
/// Ground size, cells.
MapCells cells(MapSize s);

/// `(perSide: Int, axis: Int)`.
struct MapExtras { int64_t perSide, axis; };
/// Bases each side gets beyond its main and natural, and how many
/// contested bases go down the middle.
MapExtras extras(MapSize s);

/// A map picked in the new-game dialog.
struct MapChoice {
    MapStyle style = MapStyle::highlands;
    MapSize size = MapSize::small;
    /// Starts on the map (C++ only): 2, the Swift game's mirrored maps;
    /// 4 or 8, a square map for teams (`WindowMaps::buildSquare`).
    int64_t players = 2;

    std::string name() const;

    bool operator==(const MapChoice&) const = default;
};

struct WindowMaps {
    /// The playground's ground (`autocraft models`): flat and open, a few
    /// patches of grass and gravel, and nothing standing on it. What stands
    /// on it is put down by hand (`Simulation+Playground`).
    static MapDefinition playground();

    /// `decorated: false` leaves out the rocks and plants (the picker's
    /// preview needs only the ground and the bases).
    static MapDefinition build(const MapChoice& choice, bool decorated = true);

    /// A map for 4 or 8 players (C++ only, PORTING.md "Teams"): a square
    /// whose eighth 0 <= z <= x is laid out (mains, naturals, more bases,
    /// cliffs, ramps, paint, rocks) and copied by the square's eight
    /// symmetries, so every start has the same ground. The starts go round
    /// the map in order (a team takes neighbouring ones). Each style keeps
    /// its idea: highlands and badlands put each main on a high pocket with
    /// one ramp toward the middle, the ridge raises the middle with four
    /// ramps, the open field is flat.
    static MapDefinition buildSquare(const MapChoice& choice, bool decorated = true);
    /// Half the side of a square map, cells.
    static double squareHalf(MapSize size, int64_t players);

    /// z positions of the contested bases down the middle, best first.
    static std::vector<double> axisSpots(int64_t n, double hd);

    static void cliffRocksAndScatter(MapDefinition& map, double hw, double hd, double margin);

    /// Short walls of boulders on an open field: chokes to fight around, with
    /// gaps at both ends so no base is ever sealed.
    static void rockWalls(MapDefinition& map, double hw, double hd, SeededRandom& rng);
};

} // namespace ac
