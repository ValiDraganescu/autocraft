// Noise and SeededRandom against the Swift game's numbers (golden/noise.json,
// from GoldenDumpTests.testNoise): bit for bit.
#include "golden.h"
#include "test.h"

#include "Noise.h"

using golden::json;

TEST(noise_matches_swift) {
    const json g = golden::load(golden::path("noise.json"));
    ASSERT_TRUE(!g.is_discarded());
    ASSERT_TRUE(g["noise"].size() == 6);
    for (const auto& c : g["noise"]) {
        const ac::Noise n(c["seed"].get<uint64_t>());
        const auto& points = c["points"];
        ASSERT_TRUE(points.size() == c["value"].size());
        for (size_t i = 0; i < points.size(); i++) {
            const double x = points[i][0].get<double>(), y = points[i][1].get<double>();
            EXPECT_EQ(n.value(x, y), c["value"][i].get<double>());
            EXPECT_EQ(n.fbm(x, y), c["fbm"][i].get<double>());
            EXPECT_EQ(n.fbm(x * 0.05, y * 0.05, 2), c["fbm2"][i].get<double>());
            EXPECT_EQ(n.fbm(x * 0.013, y * 0.013, 6), c["fbm6"][i].get<double>());
        }
    }
}

TEST(seeded_random_matches_swift) {
    const json g = golden::load(golden::path("noise.json"));
    ASSERT_TRUE(!g.is_discarded());
    ASSERT_TRUE(g["random"].size() == 6);
    for (const auto& c : g["random"]) {
        ac::SeededRandom r(c["seed"].get<uint64_t>());
        for (const auto& v : c["next"]) EXPECT_EQ(r.next(), v.get<uint64_t>());
        for (const auto& v : c["unit"]) EXPECT_EQ(r.unit(), v.get<double>());
        for (const auto& v : c["range"]) EXPECT_EQ(r.range(-3.5, 12.25), v.get<double>());
        const auto& bounds = c["bounds"];
        size_t k = 0;
        for (int round = 0; round < 8; round++)
            for (const auto& b : bounds) EXPECT_EQ(r.next(b.get<uint64_t>()), c["upper"][k++].get<uint64_t>());
        for (const auto& v : c["ints"]) EXPECT_EQ(r.random(-5, 17), v.get<int64_t>());
        for (int64_t n = 0; n <= 12; n++) {
            std::vector<int64_t> a;
            for (int64_t i = 0; i < n; i++) a.push_back(i);
            const auto s = r.shuffled(a);
            const auto& want = c["shuffled"][static_cast<size_t>(n)];
            ASSERT_TRUE(want.size() == s.size());
            for (size_t i = 0; i < s.size(); i++) EXPECT_EQ(s[i], want[i].get<int64_t>());
        }
        const std::vector<int64_t> pool{10, 11, 12, 13, 14, 15, 16};
        for (const auto& v : c["elements"]) {
            const int64_t* e = r.randomElement(pool);
            ASSERT_TRUE(e != nullptr);
            EXPECT_EQ(*e, v.get<int64_t>());
        }
    }
}
