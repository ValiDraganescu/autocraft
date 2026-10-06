// Port of Sources/GameCore/MusicQueue.swift.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <utility>
#include <vector>

namespace ac {

/// Swift's `RandomNumberGenerator` algorithms on any generator with a
/// `uint64_t next()`, computed as the Swift standard library computes them,
/// so a seeded queue plays the same order as the Swift game's.
namespace swiftrandom {
    /// `multipliedFullWidth(by:)` without a 128-bit type (MSVC has none).
    inline void multipliedFullWidth(uint64_t a, uint64_t b, uint64_t& high, uint64_t& low) {
        const uint64_t aLo = a & 0xffffffffu, aHi = a >> 32, bLo = b & 0xffffffffu, bHi = b >> 32;
        const uint64_t ll = aLo * bLo, lh = aLo * bHi, hl = aHi * bLo, hh = aHi * bHi;
        const uint64_t mid = (ll >> 32) + (lh & 0xffffffffu) + (hl & 0xffffffffu);
        low = (mid << 32) | (ll & 0xffffffffu);
        high = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
    }
    /// `next(upperBound:)` for `UInt64`: Lemire's nearly divisionless method.
    template <class G> uint64_t next(G& g, uint64_t upperBound) {
        uint64_t random = g.next();
        uint64_t high = 0, low = 0;
        multipliedFullWidth(random, upperBound, high, low);
        if (low < upperBound) {
            const uint64_t t = (0 - upperBound) % upperBound;
            while (low < t) {
                random = g.next();
                multipliedFullWidth(random, upperBound, high, low);
            }
        }
        return high;
    }
    /// `Int.random(in: lower..<upper, using:)`.
    template <class G> int64_t random(G& g, int64_t lower, int64_t upper) {
        const uint64_t delta = static_cast<uint64_t>(upper) - static_cast<uint64_t>(lower);
        return static_cast<int64_t>(static_cast<uint64_t>(lower) + next(g, delta));
    }
    /// `shuffle(using:)`.
    template <class T, class G> void shuffle(std::vector<T>& v, G& g) {
        if (v.size() <= 1) return;
        int64_t amount = static_cast<int64_t>(v.size());
        size_t currentIndex = 0;
        while (amount > 1) {
            const int64_t r = random(g, 0, amount);
            amount -= 1;
            std::swap(v[currentIndex], v[currentIndex + static_cast<size_t>(r)]);
            currentIndex += 1;
        }
    }
} // namespace swiftrandom

/// `SystemRandomNumberGenerator`.
struct SystemRandom {
    std::random_device device;
    uint64_t next() { return (static_cast<uint64_t>(device()) << 32) ^ static_cast<uint64_t>(device()); }
};

/// The music player's playlist (docs/music.md): every track in a shuffled
/// order and the one playing. Past the last track it shuffles again,
/// never starting the new round on the track just played. Pure: the audio
/// plays what it says.
template <class Track>
struct MusicQueue {
    /// Read only (Swift's `private(set)`).
    std::vector<Track> order;
    /// The track playing, as an index into `order`.
    int64_t index = 0;

    /// Previous within this many seconds of a track's start goes back a
    /// track; later, it starts the track over.
    static constexpr double replayAfter = 3;

    template <class G> MusicQueue(std::vector<Track> tracks, G& g) : order(std::move(tracks)) {
        swiftrandom::shuffle(order, g);
    }

    explicit MusicQueue(std::vector<Track> tracks) : order(std::move(tracks)) {
        SystemRandom g;
        swiftrandom::shuffle(order, g);
    }

    std::optional<Track> current() const {
        return index >= 0 && index < static_cast<int64_t>(order.size()) ? std::optional<Track>(order[static_cast<size_t>(index)])
                                                                         : std::nullopt;
    }

    /// On to the next track; past the last, a new round.
    template <class G> void next(G& g) {
        if (order.empty()) return;
        if (index + 1 < static_cast<int64_t>(order.size())) { index += 1; return; }
        const Track last = order[static_cast<size_t>(index)];
        swiftrandom::shuffle(order, g);
        if (order.size() > 1 && order[0] == last) {
            std::swap(order[0], order[static_cast<size_t>(swiftrandom::random(g, 1, static_cast<int64_t>(order.size())))]);
        }
        index = 0;
    }

    void next() {
        SystemRandom g;
        next(g);
    }

    /// Previous, `position` seconds into the track playing: back to the
    /// track before it (true), or, past `replayAfter` or on the round's
    /// first track, this one over from the start (false).
    bool previous(double position) {
        if (!(position <= replayAfter && index > 0)) return false;
        index -= 1;
        return true;
    }
};

} // namespace ac
