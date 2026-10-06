// Port of `Rules` of Sources/GameCore/State.swift and Vision.swift.
#include "Rules.h"


namespace ac {

double Rules::flight(UnitKind kind, bool anchored, double d) {
    switch (kind) {
    case UnitKind::juggernaut: return 0.15 + d / 14;
    case UnitKind::longbow: if (anchored) return 0.3 + d * 0.035; return 0;
    case UnitKind::firefly: return 0.09;
    // A Kestrel's rockets and a Hailstorm's flak shells fly fast.
    case UnitKind::kestrel: return 0.12 + d / 22;
    case UnitKind::hailstorm: return 0.1 + d / 28;
    // A Peregrine's seeker missiles fly; an Atlas's shells hit at once; a
    // Scorpion's sting is a fast bolt.
    case UnitKind::peregrine: return 0.1 + d / 30;
    case UnitKind::scorpion: return 0.2;
    default: return 0;
    }
}

int64_t Rules::cost(StructureKind k) {
    switch (k) {
    case StructureKind::citadel: return 400; case StructureKind::habDome: return 100;
    case StructureKind::garrison: return 150; case StructureKind::bastion: return 100;
    case StructureKind::derrick: return 75; case StructureKind::foundry: case StructureKind::spacedock: return 150;
    case StructureKind::lab: return 50; case StructureKind::sentinel: return 100;
    }
    return 0;
}

int64_t Rules::hydrogenCost(StructureKind k) {
    switch (k) {
    case StructureKind::foundry: case StructureKind::spacedock: return 100;
    case StructureKind::lab: return 25;
    default: return 0;
    }
}

double Rules::buildTime(StructureKind k) {
    switch (k) {
    case StructureKind::citadel: return 71; case StructureKind::habDome: return 21;
    case StructureKind::garrison: return 46; case StructureKind::bastion: return 29;
    case StructureKind::derrick: return 21; case StructureKind::foundry: return 43;
    case StructureKind::spacedock: return 36; case StructureKind::lab: return 18; case StructureKind::sentinel: return 18;
    }
    return 0;
}

int64_t Rules::supply(StructureKind k) {
    switch (k) {
    case StructureKind::citadel: return 15;
    case StructureKind::habDome: return 8;
    default: return 0;
    }
}

/// Half the footprint side: 5x5 Citadel; 3x3 Garrison, Bastion,
/// Derrick, Foundry and Spacedock; 2x2 Hab Dome, Lab and Sentinel.
double Rules::radius(StructureKind k) {
    switch (k) {
    case StructureKind::citadel: return 2.5;
    case StructureKind::habDome: case StructureKind::lab: case StructureKind::sentinel: return 1;
    default: return 1.5;
    }
}

/// A building needs one of these finished first (the tech tree).
std::optional<StructureKind> Rules::requires_(StructureKind k) {
    switch (k) {
    case StructureKind::garrison: return StructureKind::habDome;
    case StructureKind::bastion: case StructureKind::foundry: case StructureKind::sentinel: return StructureKind::garrison;
    case StructureKind::spacedock: return StructureKind::foundry;
    default: return std::nullopt;
    }
}

/// The units a building trains, the ones needing a Lab included.
std::vector<UnitKind> Rules::trains(StructureKind k) {
    switch (k) {
    case StructureKind::citadel: return {UnitKind::prospector};
    case StructureKind::garrison: return {UnitKind::ranger, UnitKind::comet, UnitKind::juggernaut};
    case StructureKind::foundry: return {UnitKind::firefly, UnitKind::longbow, UnitKind::hailstorm, UnitKind::atlas, UnitKind::scorpion};
    case StructureKind::spacedock: return {UnitKind::dropship, UnitKind::kestrel, UnitKind::peregrine};
    default: return {};
    }
}

/// The unit a building trains when none is named (the first it trains).
std::optional<UnitKind> Rules::produces(StructureKind k) {
    auto t = trains(k);
    if (t.empty()) return std::nullopt;
    return t.front();
}

double Rules::hp(StructureKind k) {
    switch (k) {
    case StructureKind::citadel: return 1500; case StructureKind::habDome: return 400;
    case StructureKind::garrison: return 1000; case StructureKind::bastion: return 400;
    case StructureKind::derrick: return 500; case StructureKind::foundry: return 1250;
    case StructureKind::spacedock: return 1300; case StructureKind::lab: return 400; case StructureKind::sentinel: return 250;
    }
    return 0;
}

/// The weapon of a building that shoots by itself (a Sentinel), if any.
std::optional<UnitStats> Rules::turret(StructureKind k) {
    if (k == StructureKind::sentinel) return UnitStats::sentinel;
    return std::nullopt;
}

/// Damage a Ranger shot does after armour: buildings have 1 armour,
/// and a hit always does at least 0.5.
double Rules::damage(bool toStructure) { return max(0.5, rangerDamage - (toStructure ? 1 : 0)); }

/// Damage one attack of `attacker` does to a target with these traits:
/// per hit, the base plus any bonus against its attribute, less its
/// armour (at least 0.5), times the hits.
double Rules::damage(UnitKind attacker, bool anchored, bool armored, bool light, double armor) {
    return damage(anchored ? UnitStats::anchor : stats(attacker), armored, light, armor);
}

/// The same for weapon `a`.
double Rules::damage(const UnitStats& a, bool armored, bool light, double armor) {
    const double bonus = a.bonusVsArmored > 0 && armored ? a.bonusVsArmored : a.bonusVsLight > 0 && light ? a.bonusVsLight : 0;
    return static_cast<double>(a.hits) * max(0.5, a.damage + bonus - armor);
}

/// How far each kind sees (sight radii).
double Rules::vision(UnitKind k) {
    switch (k) {
    case UnitKind::prospector: case UnitKind::scorpion: return 8;
    case UnitKind::ranger: case UnitKind::comet: return 9;
    case UnitKind::juggernaut: case UnitKind::firefly: case UnitKind::kestrel: return 10;
    case UnitKind::longbow: case UnitKind::dropship: case UnitKind::hailstorm: case UnitKind::peregrine: case UnitKind::atlas: return 11;
    }
    return 0;
}

double Rules::vision(StructureKind k) {
    switch (k) {
    case StructureKind::citadel: case StructureKind::sentinel: return 11;
    case StructureKind::bastion: return 10;
    default: return 9;
    }
}

} // namespace ac
