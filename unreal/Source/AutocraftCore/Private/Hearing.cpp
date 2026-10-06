// Port of Sources/GameCore/Hearing.swift.
#include "Hearing.h"

#include <cmath>

namespace ac {

/// Loudness 0…1 of a sound at `p`: full inside a fifth of the range,
/// then a smooth fall to silence at the range.
double Listener::gain(Vec2 p) const {
    if (!local) return 1;
    const double d = length(p - position) / max(range, 0.001);
    const double t = min(max((d - 0.2) / 0.8, 0.0), 1.0);
    const double s = 1 - t * t * (3 - 2 * t);
    return s * s;
}

/// Loudness 0…1 of a sound seen at view point `c` (y down) in a view of
/// `size` whose bottom `cover` points the HUD hides: the top-down
/// window hears what it shows, at any zoom and on any screen shape.
/// Along each axis, full round the middle of the open view, down to
/// −6 dB at its edge (−12 dB in a corner), and silent 30% of the
/// half-view past it.
double Listener::viewGain(Vec2 c, Vec2 size, double cover) {
    const double open = max(size.y - cover, 1.0);
    auto along = [](double v, double half) {
        const double a = std::abs(v - half) / max(half, 0.001);
        auto smooth = [](double t0) { const double t = min(max(t0, 0.0), 1.0); return t * t * (3 - 2 * t); };
        return a <= 1 ? 1 - 0.5 * smooth((a - 0.3) / 0.7) : 0.5 * (1 - smooth((a - 1) / 0.3));
    };
    return along(c.x, max(size.x, 1.0) / 2) * along(c.y, open / 2);
}

/// Stereo pan −1…1 of a sound at `p`, by how far left or right of the
/// listener it is.
double Listener::pan(Vec2 p) const {
    const Vec2 d = p - position;
    // Right of a unit facing `h` is (−sin h, cos h).
    const double side = facing ? d.x * -std::sin(*facing) + d.y * std::cos(*facing) : d.x;
    return min(max(side / max(range * 0.6, 0.001), -1.0), 1.0);
}

} // namespace ac
