// Port of Sources/GameCore/MapLibrary.swift.
#pragma once

#include "Noise.h"
#include "Projection.h"
#include "ScreenConfig.h"
#include "SimdMath.h"
#include "TerrainField.h"
#include "Types.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ac {

/// Builds the map for a screen configuration.
///
/// Maps are authored in canvas points, so features land on real screens, and
/// converted to ground cells through the camera projection. The handcrafted
/// map is used when the arrangement matches the one it was drawn for; every
/// other arrangement gets a map from the same template, laid out over its
/// screens with a seed from the arrangement.
struct MapLibrary {
    /// The arrangement the handcrafted map was drawn for: the ultrawide
    /// (5120x1440) above a MacBook's built-in 1728x1117 panel.
    static constexpr const char* handcraftedShape = "0,0,5120x1440|1652,1440,1728x1117";

    static MapDefinition map(const ScreenConfig& config, const CanvasProjection& projection);

    // MARK: - Authoring helpers

    struct Author {
        CanvasProjection projection;
        double levelHeight;
        /// Ground point under a canvas point on the given level.
        Vec2 g(double x, double y, int64_t level = 0) const {
            return projection.ground(Vec2(x, y), static_cast<double>(level) * levelHeight);
        }
        Plateau poly(const std::vector<std::pair<double, double>>& pts, int64_t level) const;
    };

    // MARK: - Handcrafted

    static MapDefinition handcrafted(const CanvasProjection& projection, const ScreenConfig& config);

    // MARK: - Generated

    static MapDefinition generated(const ScreenConfig& config, const CanvasProjection& projection);

    // MARK: - Cliff rocks

    /// Boulders along the foot and the lip of every cliff,
    /// skipping ramps and anything off screen.
    static void cliffRocks(MapDefinition& map, const ScreenConfig& config, const CanvasProjection& projection);

    /// The same, for any test of what counts as in sight: a ground point and
    /// its height.
    static void cliffRocks(MapDefinition& map, const std::function<bool(Vec2, double)>& onScreen);

    // MARK: - Scatter

    /// Scatter small doodads over visible ground, away from bases, ore
    /// lines, ramps and cliff faces.
    static void scatter(MapDefinition& map, int64_t count, const ScreenConfig& config, const CanvasProjection& projection);

    /// The same, drawing each candidate spot from `sample` (nil: no more).
    static void scatter(MapDefinition& map, int64_t count,
                        const std::function<std::optional<Vec2>(SeededRandom&, const TerrainField&)>& sample);
};

} // namespace ac
