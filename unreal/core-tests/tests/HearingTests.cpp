// Port of the Listener tests (GameCoreTests.swift, PilotTests.swift):
// "my location" fades and pans sounds, the window hears what it shows.
#include "test.h"

#include "Hearing.h"

using namespace ac;

TEST(hearing_listenerFadesWithDistance) {
    const Listener l(Vec2(10, 10), 10);
    EXPECT_EQ(l.gain(Vec2(11, 10)), 1.0);
    EXPECT_TRUE(l.gain(Vec2(15, 10)) > l.gain(Vec2(18, 10)));
    EXPECT_TRUE(l.gain(Vec2(18, 10)) > 0);
    EXPECT_EQ(l.gain(Vec2(20.1, 10)), 0.0);
    EXPECT_TRUE(l.pan(Vec2(5, 10)) < 0);
    EXPECT_TRUE(l.pan(Vec2(15, 10)) > 0);
    Listener whole = l;
    whole.local = false;
    EXPECT_EQ(whole.gain(Vec2(100, 100)), 1.0);
}

TEST(hearing_theWindowHearsWhatItShows) {
    // A 32:9 screen with the HUD over its bottom 100 points.
    const Vec2 size(5120, 1440);
    const double cover = 100.0, open = size.y - cover;
    auto gain = [&](double x, double y) { return Listener::viewGain(Vec2(x, y), size, cover); };
    EXPECT_EQ(gain(2560, open / 2), 1.0);
    EXPECT_EQ(gain(2560 + 600, open / 2 + 150), 1.0);
    // At the far left and right edges: a little quieter, clearly heard.
    EXPECT_NEAR(gain(0, open / 2), 0.5, 0.001);
    EXPECT_NEAR(gain(5120, open / 2), 0.5, 0.001);
    EXPECT_TRUE(gain(4500, open / 2) > gain(5000, open / 2));
    EXPECT_NEAR(gain(0, 0), 0.25, 0.001);
    // Past the edges it fades out; under the HUD it counts as past the edge.
    EXPECT_TRUE(gain(5600, open / 2) > 0);
    EXPECT_EQ(gain(5120 + 0.31 * 2560, open / 2), 0.0);
    EXPECT_TRUE(gain(2560, open + 90) < 0.5);
}

/// Driving, a sound on the unit's right pans right whatever way it faces.
TEST(hearing_listenerPansByFacing) {
    Listener l(Vec2(0, 0), 10);
    l.facing = pi / 2; // facing +Z: its right is −X
    EXPECT_TRUE(l.pan(Vec2(3, 0)) < 0);
    EXPECT_TRUE(l.pan(Vec2(-3, 0)) > 0);
    l.facing = std::nullopt;
    EXPECT_TRUE(l.pan(Vec2(3, 0)) > 0);
}
