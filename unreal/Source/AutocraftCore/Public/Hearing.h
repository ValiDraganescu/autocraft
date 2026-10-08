// Port of Sources/GameCore/Hearing.swift.
#pragma once

#include "AutocraftCoreApi.h"

#include "SimdMath.h"

#include <array>
#include <optional>
#include <string_view>

namespace ac {

/// "My location" on the map: where the player listens from. With `local`
/// on, sounds fade with ground distance and go silent beyond `range`;
/// with it off, the whole map is heard as before.
struct AUTOCRAFTCORE_API Listener {
    Vec2 position;
    /// Hearing radius in ground cells.
    double range = 16;
    bool local = true;
    /// The way the ears face, world radians (a driven unit's heading); nil:
    /// the camera's, looking up the screen, so east is right.
    std::optional<double> facing;

    /// One of `ranges`: Swift's `(name: String, cells: Double)`.
    struct Range { std::string_view name; double cells; };
    static constexpr std::array<Range, 3> ranges{{{"Near", 8}, {"Medium", 16}, {"Far", 30}}};

    /// Loudness 0…1 of a sound at `p`: full inside a fifth of the range,
    /// then a smooth fall to silence at the range.
    double gain(Vec2 p) const;

    /// Loudness 0…1 of a sound seen at view point `c` (y down) in a view of
    /// `size` whose bottom `cover` points the HUD hides: the top-down
    /// window hears what it shows, at any zoom and on any screen shape.
    /// Along each axis, full round the middle of the open view, down to
    /// −6 dB at its edge (−12 dB in a corner), and silent 30% of the
    /// half-view past it.
    static double viewGain(Vec2 c, Vec2 size, double cover = 0);

    /// Stereo pan −1…1 of a sound at `p`, by how far left or right of the
    /// listener it is.
    double pan(Vec2 p) const;

    bool operator==(const Listener&) const = default;
};

} // namespace ac
