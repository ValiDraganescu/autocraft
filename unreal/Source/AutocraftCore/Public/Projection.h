// Port of Sources/GameCore/Projection.swift.
#pragma once

#include "SimdMath.h"

#include <limits>

namespace ac {

struct GroundRect;

/// A rectangle in canvas points. The canvas is the bounding box of every
/// screen, origin top-left, Y growing down.
struct CanvasRect {
    double x = 0, y = 0, width = 0, height = 0;
    double midX() const { return x + width / 2; }
    double midY() const { return y + height / 2; }
    double maxX() const { return x + width; }
    double maxY() const { return y + height; }
    bool contains(Vec2 p) const { return p.x >= x && p.x <= maxX() && p.y >= y && p.y <= maxY(); }
    bool operator==(const CanvasRect&) const = default;
};

/// One virtual RTS camera looking at the ground over the whole canvas.
///
/// World units are cells. The ground is the XZ plane, Y is up, +X is
/// canvas right and +Z points toward the viewer (canvas down). The camera sits
/// above +Z, pitched down, and its full frustum covers the whole canvas; every
/// screen renders the sub-frustum of its own canvas rectangle, so the picture
/// is continuous wherever two screens touch.
struct CanvasProjection {
    double canvasWidth = 0;
    double canvasHeight = 0;
    /// Screen points per cell at the focus point (canvas centre, ground level).
    double pointsPerCell = 52;
    /// Camera pitch below the horizon, degrees (about 56).
    double pitchDegrees = 56;
    /// Vertical field of view of the whole canvas, degrees.
    double fovDegrees = 30;

    CanvasProjection() = default;
    CanvasProjection(double canvasWidth_, double canvasHeight_, double pointsPerCell_ = 52,
                     double pitchDegrees_ = 56, double fovDegrees_ = 30)
        : canvasWidth(canvasWidth_), canvasHeight(canvasHeight_), pointsPerCell(pointsPerCell_),
          pitchDegrees(pitchDegrees_), fovDegrees(fovDegrees_) {}

    double pitch() const;
    double tanHalfY() const;
    double tanHalfX() const;

    /// Distance from the camera to the focus point (world origin).
    double distance() const;

    Vec3 cameraPosition() const;
    Vec3 forward() const;
    Vec3 right() const;
    Vec3 up() const;

    /// Ray direction through a canvas point.
    Vec3 ray(Vec2 p) const;

    /// Where the ray through canvas point `p` meets the plane Y = `height`.
    Vec2 ground(Vec2 p, double height = 0) const;

    /// Canvas point of a world position.
    Vec2 canvas(Vec3 w) const;

    /// Ground rectangle (minX, minZ, maxX, maxZ) that the canvas can show for
    /// terrain between `low` and `high`, plus `margin` cells.
    GroundRect visibleGround(double low = -1, double high = 4, double margin = 4) const;

    bool operator==(const CanvasProjection&) const = default;
};

/// Axis-aligned rectangle on the ground (cells).
struct GroundRect {
    double minX = 0, minZ = 0, maxX = 0, maxZ = 0;
    static const GroundRect empty;
    double width() const { return maxX - minX; }
    double depth() const { return maxZ - minZ; }
    void include(Vec2 p);
    GroundRect inset(double d) const { return GroundRect{minX + d, minZ + d, maxX - d, maxZ - d}; }
    bool operator==(const GroundRect&) const = default;
};
inline constexpr GroundRect GroundRect::empty{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
                                              -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};

} // namespace ac
