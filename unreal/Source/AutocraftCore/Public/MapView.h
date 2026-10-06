// Port of Sources/GameCore/MapView.swift.
#pragma once

#include "Projection.h"
#include "ScreenConfig.h"

#include <vector>

namespace ac {

/// What the screens show of the ground: every screen's canvas rectangle,
/// minus what covers it (menu bar and Dock, the resource bar and the
/// minimap). A map fitted to it keeps every base, unit and building in
/// sight.
struct MapView {
    CanvasProjection projection;
    std::vector<CanvasRect> screens;
    std::vector<CanvasRect> covered;

    /// HUD panels drawn over the ground, in points; the app lays them out
    /// within these.
    struct Reserve { double width, height; };
    static constexpr Reserve barReserve{590.0, 48.0};
    static constexpr Reserve minimapReserve{470.0, 275.0};

    MapView() = default;
    MapView(const ScreenConfig& config, const CanvasProjection& projection_);

    /// A ground point at `height` is in sight: it and the points `inset`
    /// points around it are on some screen (so an edge where two screens
    /// meet is no edge) and under nothing drawn over the ground.
    bool shows(Vec2 p, double height, double inset = 0) const;

    bool operator==(const MapView&) const = default;
};

} // namespace ac
