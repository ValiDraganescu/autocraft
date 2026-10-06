// Port of Sources/GameCore/ScreenConfig.swift.
#pragma once

#include "Projection.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ac {

struct ScreenInsets {
    double top = 0, bottom = 0, left = 0, right = 0;
    bool operator==(const ScreenInsets&) const = default;
};

/// One physical display, already mapped into canvas coordinates.
struct ScreenInfo {
    /// Stable identity of the panel: vendor, model and serial number.
    std::string identity;
    std::string name;
    /// The screen's rectangle on the canvas (points, top-left origin).
    CanvasRect rect;
    /// Top and bottom and side strips the system draws over (menu bar,
    /// Dock), in points from each edge; nil: none. Not part of the session's
    /// identity: the Dock moving does not start a new game.
    std::optional<ScreenInsets> insets;
    bool operator==(const ScreenInfo&) const = default;
};

/// The display arrangement a game session is bound to.
///
/// Two configurations are the same session exactly when they list the same
/// panels at the same canvas rectangles. Plugging in, unplugging or moving a
/// display in System Settings gives a new configuration and a new session;
/// returning to an earlier arrangement resumes its session.
struct ScreenConfig {
    std::vector<ScreenInfo> screens;
    double canvasWidth = 0;
    double canvasHeight = 0;

    /// One of `init(appKitScreens:)`'s tuples.
    struct AppKitScreen {
        std::string identity, name;
        double x = 0, y = 0, width = 0, height = 0;
    };

    ScreenConfig() = default;
    /// `frames` are in AppKit space (Y up, origin at the primary screen's
    /// bottom-left); they are flipped into canvas space here.
    ScreenConfig(const std::vector<AppKitScreen>& appKitScreens,
                 const std::optional<std::vector<ScreenInsets>>& insets = std::nullopt);
    ScreenConfig(std::vector<ScreenInfo> screens_, double canvasWidth_, double canvasHeight_)
        : screens(std::move(screens_)), canvasWidth(canvasWidth_), canvasHeight(canvasHeight_) {}

    /// Canonical text of the arrangement.
    std::string signature() const;

    /// Short stable id of the arrangement, used as the session file name.
    std::string sessionID() const;

    /// The arrangement's shape: sizes and positions without panel identities.
    /// Handcrafted maps match on this.
    std::string shape() const;

    /// The biggest screen by area (the first of equals): it holds the minimap.
    std::optional<ScreenInfo> largest() const;

    /// The screen that holds the canvas's top-right: it holds the resource bar.
    std::optional<ScreenInfo> barScreen() const;

    bool operator==(const ScreenConfig&) const = default;
};

uint64_t fnv1a64(const std::string& s);

} // namespace ac
