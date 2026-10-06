// Port of Sources/GameCore/FreeView.swift.
#include "FreeView.h"

#include "TerrainField.h"

#include <array>
#include <cmath>

namespace ac {

FreeView::FreeView(GroundRect bounds_, std::optional<Vec2> target_, double pointsPerCell_, Vec2 size_)
    : bounds(bounds_),
      target(target_ ? *target_ : Vec2((bounds_.minX + bounds_.maxX) / 2, (bounds_.minZ + bounds_.maxZ) / 2)),
      pointsPerCell(pointsPerCell_), size(size_) {
    clamp();
}

/// The camera for the current view size, centred on the world origin;
/// `target` shifts everything by a ground offset.
CanvasProjection FreeView::projection() const {
    return CanvasProjection(max(size.x, 1.0), max(size.y, 1.0), pointsPerCell);
}

/// Camera position in the world.
Vec3 FreeView::cameraPosition() const { return projection().cameraPosition() + Vec3(target.x, 0, target.y); }

/// Least zoom: the whole map in the open view (above `cover`, perspective
/// included, so the near edge is what limits it), but never past a floor.
double FreeView::minPointsPerCell() const {
    const double open = max(size.y - cover, 1.0);
    const double sinPitch = std::sin(projection().pitchDegrees * pi / 180);
    double ppc = min(size.x / bounds.width(), open / (bounds.depth() * sinPitch));
    const Vec2 mid = Vec2((bounds.minX + bounds.maxX) / 2, (bounds.minZ + bounds.maxZ) / 2);
    const std::array<Vec3, 4> corners = {Vec3(bounds.minX, 0, bounds.minZ), Vec3(bounds.maxX, 0, bounds.minZ),
                                         Vec3(bounds.minX, 0, bounds.maxZ), Vec3(bounds.maxX, 0, bounds.maxZ)};
    for (int i = 0; i < 60; i++) {
        if (!(ppc > 1)) continue;
        FreeView v = *this;
        v.pointsPerCell = ppc;
        v.target = mid;
        v.clamp();
        bool inside = true;
        for (Vec3 corner : corners) {
            const Vec2 c = v.canvas(corner);
            if (!(c.x >= 0 && c.x <= size.x && c.y >= 0 && c.y <= open)) { inside = false; break; }
        }
        if (inside) break;
        ppc *= 0.96;
    }
    return max(1.0, ppc * 0.98);
}

/// View point (y down) of a world position.
Vec2 FreeView::canvas(Vec3 w) const { return projection().canvas(w - Vec3(target.x, 0, target.y)); }

/// The ray through a view point.
FreeView::Ray FreeView::ray(Vec2 c) const { return Ray{cameraPosition(), projection().ray(c)}; }

/// Where the ray through a view point meets the ground plane (height 0).
Vec2 FreeView::ground(Vec2 c) const { return projection().ground(c) + target; }

/// Drag the ground: what was under `from` ends up under `to`.
void FreeView::drag(Vec2 from, Vec2 to) {
    target += ground(from) - ground(to);
    clamp();
}

/// Zoom by `factor` (above 1 closer) with the ground under `c` held still.
void FreeView::zoom(double factor, Vec2 c) {
    const Vec2 before = ground(c);
    pointsPerCell = min(max(pointsPerCell * factor, minPointsPerCell()), maxPointsPerCell);
    target += before - ground(c);
    clamp();
}

/// Show the whole map.
void FreeView::fit() {
    pointsPerCell = minPointsPerCell();
    target = Vec2((bounds.minX + bounds.maxX) / 2, (bounds.minZ + bounds.maxZ) / 2);
    clamp();
}

/// Centre on a ground point.
void FreeView::center(Vec2 p) {
    target = p;
    clamp();
}

/// A new view size (the window was resized) or HUD `cover`; keeps the
/// scale, unless the map would now be smaller than the view.
void FreeView::resize(Vec2 s, std::optional<double> c) {
    size = s;
    if (c) cover = *c;
    pointsPerCell = min(max(pointsPerCell, minPointsPerCell()), maxPointsPerCell);
    clamp();
}

/// Keep the view on the map: the map's side edges stop at
/// the view's sides (at the middle of the open view, so a little ground
/// past the edge shows in the far corners), its far edge at the view's
/// top and its near edge just above `cover`. Along an axis where the map
/// is smaller than the view, the map is centred in it.
void FreeView::clamp() {
    const CanvasProjection p = projection();
    const double open = max(size.y - cover, 1.0);
    // Ground under view points, from the focus. (Swift's `far` and `near`:
    // both are macros in Windows headers.)
    const double side = p.ground(Vec2(0, open / 2)).x;
    const double farEdge = p.ground(Vec2(size.x / 2, 0)).y;
    const double nearEdge = p.ground(Vec2(size.x / 2, open)).y;
    auto fitAxis = [](double v, double lo, double hi) { return lo <= hi ? min(max(v, lo), hi) : (lo + hi) / 2; };
    target = Vec2(fitAxis(target.x, bounds.minX - side, bounds.maxX + side),
                  fitAxis(target.y, bounds.minZ - farEdge, bounds.maxZ - nearEdge));
}

/// Ground the view covers: its four corners on the ground plane.
std::vector<Vec2> FreeView::footprint() const {
    std::vector<Vec2> out;
    for (Vec2 c : {Vec2(0, 0), Vec2(size.x, 0), size, Vec2(0, size.y)}) out.push_back(ground(c));
    return out;
}

} // namespace ac
