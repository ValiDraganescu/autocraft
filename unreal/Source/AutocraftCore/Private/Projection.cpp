// Port of Sources/GameCore/Projection.swift.
#include "Projection.h"

#include <algorithm>
#include <cmath>

namespace ac {

double CanvasProjection::pitch() const { return pitchDegrees * pi / 180; }
double CanvasProjection::tanHalfY() const { return std::tan(fovDegrees * pi / 360); }
double CanvasProjection::tanHalfX() const { return tanHalfY() * canvasWidth / canvasHeight; }

/// Distance from the camera to the focus point (world origin).
double CanvasProjection::distance() const { return (canvasHeight / pointsPerCell / 2) / tanHalfY(); }

Vec3 CanvasProjection::cameraPosition() const {
    return Vec3(0, std::sin(pitch()), std::cos(pitch())) * distance();
}
Vec3 CanvasProjection::forward() const { return Vec3(0, -std::sin(pitch()), -std::cos(pitch())); }
Vec3 CanvasProjection::right() const { return Vec3(1, 0, 0); }
Vec3 CanvasProjection::up() const { return Vec3(0, std::cos(pitch()), -std::sin(pitch())); }

/// Ray direction through a canvas point.
Vec3 CanvasProjection::ray(Vec2 p) const {
    const double nx = p.x / canvasWidth * 2 - 1;
    const double ny = 1 - p.y / canvasHeight * 2;
    return forward() + right() * (nx * tanHalfX()) + up() * (ny * tanHalfY());
}

/// Where the ray through canvas point `p` meets the plane Y = `height`.
Vec2 CanvasProjection::ground(Vec2 p, double height) const {
    const Vec3 o = cameraPosition(), d = ray(p);
    const double t = (height - o.y) / d.y;
    const Vec3 hit = o + d * t;
    return Vec2(hit.x, hit.z);
}

/// Canvas point of a world position.
Vec2 CanvasProjection::canvas(Vec3 w) const {
    const Vec3 v = w - cameraPosition();
    const double z = dot(v, forward());
    const double nx = dot(v, right()) / (z * tanHalfX());
    const double ny = dot(v, up()) / (z * tanHalfY());
    return Vec2((nx + 1) / 2 * canvasWidth, (1 - ny) / 2 * canvasHeight);
}

/// Ground rectangle (minX, minZ, maxX, maxZ) that the canvas can show for
/// terrain between `low` and `high`, plus `margin` cells.
GroundRect CanvasProjection::visibleGround(double low, double high, double margin) const {
    GroundRect r = GroundRect::empty;
    for (double h : {low, high}) {
        for (Vec2 c : {Vec2(0.0, 0.0), Vec2(canvasWidth, 0), Vec2(0, canvasHeight), Vec2(canvasWidth, canvasHeight)}) {
            r.include(ground(c, h));
        }
    }
    return r.inset(-margin);
}

void GroundRect::include(Vec2 p) {
    minX = std::min(minX, p.x); minZ = std::min(minZ, p.y);
    maxX = std::max(maxX, p.x); maxZ = std::max(maxZ, p.y);
}

} // namespace ac
