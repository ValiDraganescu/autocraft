// Port of Sources/GameCore/MapView.swift.
#include "MapView.h"

namespace ac {

MapView::MapView(const ScreenConfig& config, const CanvasProjection& projection_) : projection(projection_) {
    for (const auto& s : config.screens) screens.push_back(s.rect);
    std::vector<CanvasRect> out;
    for (const auto& s : config.screens) {
        if (!s.insets) continue;
        const ScreenInsets& i = *s.insets;
        const CanvasRect r = s.rect;
        if (i.top > 0) out.push_back(CanvasRect{r.x, r.y, r.width, i.top});
        if (i.bottom > 0) out.push_back(CanvasRect{r.x, r.maxY() - i.bottom, r.width, i.bottom});
        if (i.left > 0) out.push_back(CanvasRect{r.x, r.y, i.left, r.height});
        if (i.right > 0) out.push_back(CanvasRect{r.maxX() - i.right, r.y, i.right, r.height});
    }
    if (auto s = config.barScreen()) {
        const double top = s->insets ? s->insets->top : 0;
        out.push_back(CanvasRect{s->rect.maxX() - barReserve.width, s->rect.y,
                                 barReserve.width, top + barReserve.height});
    }
    if (auto s = config.largest()) {
        const double bottom = s->insets ? s->insets->bottom : 0;
        out.push_back(CanvasRect{s->rect.maxX() - minimapReserve.width,
                                 s->rect.maxY() - bottom - minimapReserve.height,
                                 minimapReserve.width, bottom + minimapReserve.height});
    }
    covered = out;
}

/// A ground point at `height` is in sight: it and the points `inset`
/// points around it are on some screen (so an edge where two screens
/// meet is no edge) and under nothing drawn over the ground.
bool MapView::shows(Vec2 p, double height, double inset) const {
    const Vec2 c = projection.canvas(Vec3(p.x, height, p.y));
    for (Vec2 d : {Vec2(0, 0), Vec2(inset, 0), Vec2(-inset, 0), Vec2(0, inset), Vec2(0, -inset)}) {
        const Vec2 q = c + d;
        bool onScreen = false;
        for (const auto& r : screens) if (r.contains(q)) { onScreen = true; break; }
        bool underHUD = false;
        for (const auto& r : covered) if (r.contains(q)) { underHUD = true; break; }
        if (!onScreen || underHUD) return false;
    }
    return true;
}

} // namespace ac
