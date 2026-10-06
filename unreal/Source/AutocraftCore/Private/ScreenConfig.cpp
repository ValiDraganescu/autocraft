// Port of Sources/GameCore/ScreenConfig.swift.
#include "ScreenConfig.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>

namespace ac {

ScreenConfig::ScreenConfig(const std::vector<AppKitScreen>& appKitScreens,
                           const std::optional<std::vector<ScreenInsets>>& insets) {
    double minX = 0, maxX = 1, minY = 0, maxY = 1;
    if (!appKitScreens.empty()) {
        minX = appKitScreens[0].x; maxX = appKitScreens[0].x + appKitScreens[0].width;
        minY = appKitScreens[0].y; maxY = appKitScreens[0].y + appKitScreens[0].height;
        for (const auto& s : appKitScreens) {
            minX = std::min(minX, s.x);
            maxX = std::max(maxX, s.x + s.width);
            minY = std::min(minY, s.y);
            maxY = std::max(maxY, s.y + s.height);
        }
    }
    canvasWidth = maxX - minX;
    canvasHeight = maxY - minY;
    screens.clear();
    for (size_t i = 0; i < appKitScreens.size(); i++) {
        const auto& s = appKitScreens[i];
        ScreenInfo info;
        info.identity = s.identity;
        info.name = s.name;
        info.rect = CanvasRect{s.x - minX, maxY - (s.y + s.height), s.width, s.height};
        if (insets && i < insets->size()) info.insets = (*insets)[i];
        screens.push_back(info);
    }
    std::stable_sort(screens.begin(), screens.end(), [](const ScreenInfo& a, const ScreenInfo& b) {
        return a.rect.y < b.rect.y || (a.rect.y == b.rect.y && a.rect.x < b.rect.x);
    });
}

namespace {
std::string intText(double v) { return std::to_string(static_cast<int64_t>(v)); }
} // namespace

/// Canonical text of the arrangement.
std::string ScreenConfig::signature() const {
    std::string out;
    for (size_t i = 0; i < screens.size(); i++) {
        const auto& s = screens[i];
        if (i > 0) out += "|";
        out += s.identity + "@" + intText(s.rect.x) + "," + intText(s.rect.y) + "," + intText(s.rect.width) + "x" +
               intText(s.rect.height);
    }
    return out;
}

/// Short stable id of the arrangement, used as the session file name.
std::string ScreenConfig::sessionID() const {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%016" PRIx64, fnv1a64(signature()));
    return buf;
}

/// The arrangement's shape: sizes and positions without panel identities.
/// Handcrafted maps match on this.
std::string ScreenConfig::shape() const {
    std::string out;
    for (size_t i = 0; i < screens.size(); i++) {
        const auto& r = screens[i].rect;
        if (i > 0) out += "|";
        out += intText(r.x) + "," + intText(r.y) + "," + intText(r.width) + "x" + intText(r.height);
    }
    return out;
}

/// The biggest screen by area (the first of equals): it holds the minimap.
std::optional<ScreenInfo> ScreenConfig::largest() const {
    std::optional<ScreenInfo> best;
    for (const auto& s : screens) {
        if (!best) { best = s; continue; }
        if (s.rect.width * s.rect.height > best->rect.width * best->rect.height) best = s;
    }
    return best;
}

/// The screen that holds the canvas's top-right: it holds the resource bar.
std::optional<ScreenInfo> ScreenConfig::barScreen() const {
    if (screens.empty()) return std::nullopt;
    auto it = std::min_element(screens.begin(), screens.end(), [](const ScreenInfo& a, const ScreenInfo& b) {
        return a.rect.y < b.rect.y || (a.rect.y == b.rect.y && -a.rect.maxX() < -b.rect.maxX());
    });
    return *it;
}

uint64_t fnv1a64(const std::string& s) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char b : s) h = (h ^ static_cast<uint64_t>(b)) * 0x100000001b3ull;
    return h;
}

} // namespace ac
