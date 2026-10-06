// Port of the stall log test of StallTests.swift (the rest of StallTests
// is the Simulation's and the Commander's).
#include "VisionHelpers.h"

#include "StallWatch.h"

#include <string>

using namespace ac;
using visiontest::homeMap;

namespace {
bool hasPrefix(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }
bool anyHasPrefix(const std::vector<std::string>& notes, const std::string& p) {
    for (const auto& n : notes) if (hasPrefix(n, p)) return true;
    return false;
}
} // namespace

/// A bank that sits unspent for a minute is noted once, and again
/// only after it was spent; so is a player with Prospectors and no income.
TEST(stallWatch_notesABankAndNoIncomeOnce) {
    GameState s = GameState::new_(homeMap());
    s.players[1].ore = 1500;
    s.players[1].totalMined = 3000;
    s.players[0].totalMined = 100;
    StallWatch watch;
    std::vector<std::string> notes;
    for (int t = 0; t <= 70; ++t) {
        s.time = static_cast<double>(t);
        s.players[0].totalMined += 5; // Blue keeps mining.
        for (auto& n : watch.observe(s)) notes.push_back(n);
    }
    EXPECT_EQ(notes.size(), size_t(2));
    EXPECT_TRUE(anyHasPrefix(notes, "stall: Red has banked 1500 ore unspent for 60 s"));
    EXPECT_TRUE(anyHasPrefix(notes, "stall: Red has gathered nothing for 60 s"));
    // Spending clears it; banking another minute notes it again.
    notes.clear();
    s.players[1].ore = 1200;
    s.players[1].totalMined += 5;
    for (int t = 71; t <= 135; ++t) {
        s.time = static_cast<double>(t);
        s.players[0].totalMined += 5;
        s.players[1].totalMined += 5;
        for (auto& n : watch.observe(s)) notes.push_back(n);
    }
    EXPECT_EQ(notes.size(), size_t(1));
    EXPECT_TRUE(!notes.empty() && hasPrefix(notes[0], "stall: Red has banked 1200"));
}
