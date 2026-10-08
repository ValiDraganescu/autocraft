// Port of Sources/GameCore/Noise.swift.
#pragma once

#include "AutocraftCoreApi.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace ac {

/// Deterministic value noise and a small PRNG. Same seed, same map, on every run.
struct AUTOCRAFTCORE_API Noise {
    uint32_t seed;
    explicit Noise(uint64_t seed_) : seed(static_cast<uint32_t>(seed_ ^ (seed_ >> 32))) {}

    double hash(int32_t x, int32_t y) const;

    /// Smooth value noise in [0, 1].
    double value(double x, double y) const;

    /// Fractal noise in roughly [-1, 1].
    double fbm(double x, double y, int64_t octaves = 4) const;
};

/// SplitMix64: a tiny seedable generator for map layout.
///
/// Also Swift's `RandomNumberGenerator` algorithms the game uses with it
/// (`next(upperBound:)`, `Int.random(in:using:)`, `randomElement(using:)`,
/// `shuffle(using:)`), the way the Swift standard library computes them.
struct AUTOCRAFTCORE_API SeededRandom {
    uint64_t state;
    explicit SeededRandom(uint64_t seed) : state(seed) {}
    uint64_t next();
    double unit();
    double range(double a, double b);

    /// `next(upperBound:)` for `UInt64`: Lemire's nearly divisionless method.
    uint64_t next(uint64_t upperBound);
    /// `Int.random(in: lower..<upper, using:)`.
    int64_t random(int64_t lower, int64_t upper);
    /// `randomElement(using:)`: nullptr when empty.
    template <class T> const T* randomElement(const std::vector<T>& v) {
        if (v.empty()) return nullptr;
        return &v[static_cast<size_t>(random(0, static_cast<int64_t>(v.size())))];
    }
    /// `shuffle(using:)`.
    template <class T> void shuffle(std::vector<T>& v) {
        if (v.size() <= 1) return;
        int64_t amount = static_cast<int64_t>(v.size());
        size_t currentIndex = 0;
        while (amount > 1) {
            const int64_t r = random(0, amount);
            amount -= 1;
            std::swap(v[currentIndex], v[currentIndex + static_cast<size_t>(r)]);
            currentIndex += 1;
        }
    }
    /// `shuffled(using:)`.
    template <class T> std::vector<T> shuffled(std::vector<T> v) {
        shuffle(v);
        return v;
    }
};

} // namespace ac
