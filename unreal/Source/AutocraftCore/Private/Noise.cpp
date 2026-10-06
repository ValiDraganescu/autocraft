// Port of Sources/GameCore/Noise.swift.
#include "Noise.h"

#include <cmath>
#include <limits>

namespace ac {

double Noise::hash(int32_t x, int32_t y) const {
    uint32_t h = static_cast<uint32_t>(x) * 0x27d4eb2du ^ static_cast<uint32_t>(y) * 0x165667b1u ^ seed;
    h = (h ^ (h >> 15)) * 0x2c1b3c6du;
    h = (h ^ (h >> 12)) * 0x297a2d39u;
    h ^= h >> 15;
    return static_cast<double>(h) / static_cast<double>(std::numeric_limits<uint32_t>::max());
}

/// Smooth value noise in [0, 1].
double Noise::value(double x, double y) const {
    const double xf = std::floor(x), yf = std::floor(y);
    const int32_t xi = static_cast<int32_t>(xf), yi = static_cast<int32_t>(yf);
    double tx = x - xf, ty = y - yf;
    tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
    const double a = hash(xi, yi), b = hash(xi + 1, yi), c = hash(xi, yi + 1), d = hash(xi + 1, yi + 1);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

/// Fractal noise in roughly [-1, 1].
double Noise::fbm(double x, double y, int64_t octaves) const {
    double sum = 0.0, amp = 0.5, f = 1.0, norm = 0.0;
    for (int64_t o = 0; o < octaves; o++) {
        sum += (value(x * f + static_cast<double>(o) * 17.3, y * f - static_cast<double>(o) * 9.1) * 2 - 1) * amp;
        norm += amp; amp *= 0.5; f *= 2.03;
    }
    return sum / norm;
}

uint64_t SeededRandom::next() {
    state += 0x9e3779b97f4a7c15ull;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

double SeededRandom::unit() { return static_cast<double>(next() >> 11) / static_cast<double>(1ull << 53); }

double SeededRandom::range(double a, double b) { return a + (b - a) * unit(); }

namespace {
/// `multipliedFullWidth(by:)` without a 128-bit type (MSVC has none).
void multipliedFullWidth(uint64_t a, uint64_t b, uint64_t& high, uint64_t& low) {
    const uint64_t aLo = a & 0xffffffffull, aHi = a >> 32, bLo = b & 0xffffffffull, bHi = b >> 32;
    const uint64_t ll = aLo * bLo, lh = aLo * bHi, hl = aHi * bLo, hh = aHi * bHi;
    const uint64_t mid = (ll >> 32) + (lh & 0xffffffffull) + (hl & 0xffffffffull);
    low = (mid << 32) | (ll & 0xffffffffull);
    high = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
}
} // namespace

uint64_t SeededRandom::next(uint64_t upperBound) {
    // Lemire's "nearly divisionless" method, as the Swift standard library
    // has it (https://arxiv.org/abs/1805.10941).
    uint64_t random = next();
    uint64_t high = 0, low = 0;
    multipliedFullWidth(random, upperBound, high, low);
    if (low < upperBound) {
        const uint64_t t = (0 - upperBound) % upperBound;
        while (low < t) {
            random = next();
            multipliedFullWidth(random, upperBound, high, low);
        }
    }
    return high;
}

int64_t SeededRandom::random(int64_t lower, int64_t upper) {
    const uint64_t delta = static_cast<uint64_t>(upper) - static_cast<uint64_t>(lower);
    return static_cast<int64_t>(static_cast<uint64_t>(lower) + next(delta));
}

} // namespace ac
