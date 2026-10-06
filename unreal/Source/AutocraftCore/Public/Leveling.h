// Port of Sources/GameCore/Leveling.swift. Its types and `Leveling`'s
// declarations are in Types.h (the Swift types hold each other); the
// definitions are in Leveling.cpp.
#pragma once

#include "Types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ac {

/// What something costs: Swift's `(ore: Int, hydrogen: Int)`
/// (`Simulation.unitCost`, `buildCost`, `upgradeCost`).
struct Price {
    int64_t ore = 0;
    int64_t hydrogen = 0;
    bool operator==(const Price&) const = default;
};

/// Swift's `description` of a `Double`: the shortest digits that read back
/// the same, "1.0" for a whole number, "inf", an exponent from 1e16 up and
/// below 1e-4 ("1e-05").
std::string swiftDescription(double v);

/// Swift's `String(describing:)` of the effects, as the balance text has
/// them: "[GameCore.Effect.stat(GameCore.Stat.drill, GameCore.Scope.nearby,
/// GameCore.Change.percent(0.1))]".
std::string describe(const Stat& s);
std::string describe(const Scope& s);
std::string describe(const Change& c);
std::string describe(const Effect& e);
std::string describe(const std::vector<Effect>& effects);

} // namespace ac
