// Port of the playlist tests of MusicTests.swift (the MusicStore ones wait
// for the SQLite port).
#include "test.h"

#include "MusicQueue.h"
#include "Noise.h"

#include <set>
#include <string>

using namespace ac;

/// A round plays every track once; the next round never starts on the
/// track that ended the last.
TEST(musicQueue_everyTrackPlaysOnceARound) {
    SeededRandom g(7);
    for (int k = 0; k < 40; ++k) {
        MusicQueue<std::string> q({"a", "b", "c", "d"}, g);
        std::set<std::string> heard{*q.current()};
        for (int j = 0; j < 3; ++j) { q.next(g); heard.insert(*q.current()); }
        EXPECT_TRUE((heard == std::set<std::string>{"a", "b", "c", "d"}));
        const auto last = q.current();
        q.next(g);
        EXPECT_EQ(q.index, int64_t(0));
        EXPECT_TRUE(q.current() != last);
    }
}

/// Previous goes back a track in the first 3 s, else starts this one
/// over; on the round's first track it always starts over.
TEST(musicQueue_previousStartsOverAfterThreeSeconds) {
    SeededRandom g(1);
    MusicQueue<std::string> q({"a", "b", "c"}, g);
    const auto first = q.current();
    EXPECT_TRUE(!q.previous(1));
    EXPECT_TRUE(q.current() == first);
    q.next(g);
    const auto second = q.current();
    EXPECT_TRUE(!q.previous(10));
    EXPECT_TRUE(q.current() == second);
    EXPECT_TRUE(q.previous(2));
    EXPECT_TRUE(q.current() == first);
}

TEST(musicQueue_anEmptyPlaylistHasNothingToPlay) {
    MusicQueue<std::string> q(std::vector<std::string>{});
    q.next();
    EXPECT_TRUE(!q.current());
    EXPECT_TRUE(!q.previous(0));
}

/// The order a seeded queue plays is Swift's: `[a...h]` shuffled with
/// `SeededRandom(seed: 7)`, then two rounds on.
TEST(musicQueue_seededOrderIsSwifts) {
    SeededRandom g(7);
    MusicQueue<std::string> q({"a", "b", "c", "d", "e", "f", "g", "h"}, g);
    std::string played;
    for (int k = 0; k < 24; ++k) { played += *q.current(); q.next(g); }
    EXPECT_EQ(played, std::string("dbhfaegchbafcdegecgbhadf"));
}
