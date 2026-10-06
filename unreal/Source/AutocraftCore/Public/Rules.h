// Port of `Rules` and `UnitStats` of Sources/GameCore/State.swift, and the
// `Rules` extension of Vision.swift (sight).
#pragma once

#include "SimdMath.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace ac {

/// `Unit.Kind`, declared here so `Rules` can take it; `Types.h` nests it as
/// `Unit::Kind`.
enum class UnitKind : uint8_t {
    prospector, ranger, comet, firefly, juggernaut, dropship, longbow, kestrel, hailstorm,
    // docs/new-units.md: appended, since a saved game stores the index.
    peregrine, atlas, scorpion
};
/// Its place in `allCases`: a case without a payload is stored as
/// its index in declaration order (`UnitTableTests` checks).
constexpr int64_t ordinal(UnitKind k) { return static_cast<int64_t>(k); }

/// `Structure.Kind`, declared here so `Rules` can take it; `Types.h` nests it
/// as `Structure::Kind`.
enum class StructureKind : uint8_t {
    citadel, habDome, garrison, bastion, derrick, foundry, spacedock, lab, sentinel
};

/// One unit type's numbers (in real seconds; ranges edge to edge).
struct UnitStats {
    double hp = 0;
    double armor = 0;
    double speed = 0;
    double radius = 0;
    int64_t supply = 0;
    int64_t ore = 0;
    int64_t hydrogen = 0;
    double trainTime = 0;
    /// Damage per hit, hits per attack, and the extra per hit against
    /// light or armoured targets.
    double damage = 0;
    int64_t hits = 1;
    double bonusVsLight = 0;
    double bonusVsArmored = 0;
    double cooldown = 1;
    double range = 0;
    /// Closest it can shoot (an anchored Longbow cannot fire at its own feet).
    double minRange = 0;
    bool light = false;
    bool armored = false;
    bool bio = false;
    bool air = false;
    bool hitsGround = false;
    bool hitsAir = false;
    /// A weapon that fires in bursts: it fires for `burst` seconds, then
    /// pauses `pause` seconds to reload (0: no bursts).
    double burst = 0;
    double pause = 0;
    /// A rail shot: hitscan, with its own cooldown (`Unit::railCooldown`).
    bool rail = false;

    /// `table` in `Unit.Kind.allCases` order, read by `Unit.Kind.ordinal`:
    /// a dictionary keyed by the kind hashes its name on every look-up, and
    /// the simulation looks up stats hundreds of thousands of times a step.
    /// (Swift's `table` dictionary is this array here.)
    static const std::array<UnitStats, 12> byKind;
    /// A Ranger's mini gun (the Mini gun upgrade): 4 a round, five rounds a
    /// second for 3 s, then a 1 s pause to reload. About 15 damage a second
    /// against the rifle's 10.
    static const UnitStats minigun;
    /// An anchored Longbow's shock cannon: 40 (+30 armoured), splash, 13 range.
    static const UnitStats anchor;
    /// A Sentinel's missile pods: 12 ×2 at the air only, range 7 from its edge.
    static const UnitStats sentinel;
    /// A Kestrel's rail gun, at the air only: one hitscan slug, 34 (+14 armoured)
    /// every 3.2 s, range 8. About 11 (+4) a second against the Hailstorm's 12
    /// (+8) and the Sentinel's 24 a second, but a Kestrel is one gun, not a battery.
    static const UnitStats railgun;

    bool operator==(const UnitStats&) const = default;
};

/// The game's numbers, in real seconds.
struct Rules {
    /// Prospector move speed, cells per second.
    static constexpr double prospectorSpeed = 3.94;
    /// Prospector turn rate, radians per second.
    static constexpr double prospectorTurnRate = 14.0;
    /// Time a Prospector drills a patch before it carries a load away, three
    /// times a brisk 2 s: the wallpaper lingers on the drilling, so trips take
    /// longer and the stockpile grows about half as fast.
    static constexpr double miningTime = 6.0;
    /// Ore per trip.
    static constexpr int64_t carry = 5;
    /// Citadel footprint radius the Prospector stops at when returning.
    static constexpr double depositRange = 3.0;
    /// Distance from a patch centre at which a Prospector mines it.
    static constexpr double mineRange = 1.2;
    static constexpr int64_t startingOre = 50;
    /// The hard supply limit.
    static constexpr int64_t maxSupply = 200;
    /// A dry patch grows back this long after its last load (the
    /// wallpaper never runs out, and regrown fields are worth fighting over).
    static constexpr double regrowDelay = 300.0;

    static constexpr int64_t prospectorCost = 50;
    static constexpr double prospectorTrainTime = 12.0;
    static constexpr int64_t rangerCost = 50;
    static constexpr double rangerTrainTime = 18.0;
    /// Ranger move speed, cells per second.
    static constexpr double rangerSpeed = 3.15;
    /// Production queue length of one building.
    static constexpr int64_t maxQueue = 5;

    // Combat.
    /// Ranger coil rifle: damage per shot, seconds between shots, range
    /// (edge to edge, cells) and how far a Ranger sees to pick targets.
    static constexpr double rangerDamage = 6.0;
    static constexpr double rangerCooldown = 0.61;
    static constexpr double rangerRange = 5.0;
    static constexpr double sight = 9.0;
    /// Unit footprint radius (Rangers and Prospectors), for range checks.
    static constexpr double unitRadius = 0.375;
    /// Rangers a Bastion holds, and the range they gain inside it.
    static constexpr int64_t bastionCapacity = 4;
    static constexpr double bastionRangeBonus = 1.0;
    /// Extra range for a shooter standing on a higher level than its
    /// target (a Ranger on a plateau, or a Bastion on one, firing down).
    static constexpr double highGroundRangeBonus = 1.0;
    /// Seconds a won game stays on screen before a new one starts.
    static constexpr double victoryLap = 30.0;

    /// A Comet heals this many hp a second once it has gone
    /// `cometRegenDelay` seconds without being hit.
    static constexpr double cometRegen = 2.8;
    static constexpr double cometRegenDelay = 5.0;
    /// A Longbow takes this long to anchor up or pack up.
    static constexpr double anchorTime = 2.7;
    /// A tank with nothing to shoot anchored for this long packs up (to move
    /// on, or closer).
    static constexpr double anchorIdle = 2.0;
    /// One ring of a splash: Swift's `(radius: Double, factor: Double)`.
    struct Ring { double radius, factor; };
    /// An anchored shell's splash: full damage out to the first radius, then
    /// half, then a quarter. It hurts everyone there, friends too.
    static constexpr std::array<Ring, 3> splash{{{0.47, 1}, {0.78, 0.5}, {1.25, 0.25}}};
    /// A Hailstorm's flak burst: full damage to the air target it bursts on,
    /// then half to enemy flyers within about a cell of it (edge to centre).
    /// Only enemies: it bursts in the sky, away from its own side.
    static constexpr std::array<Ring, 2> flakSplash{{{0.5, 1}, {1.0, 0.5}}};
    /// Half-width of the line a Firefly's flame burns.
    static constexpr double flameWidth = 0.4;
    /// How long an attack takes to land on a target `distance` away
    /// (centre to centre): a Juggernaut's grenade and an anchored shell fly, a
    /// Firefly's jet licks out; coil rounds, pistols and the 90 mm guns hit
    /// at once. The damage lands then, and the scene flies its rounds for
    /// exactly this long.
    static double flight(UnitKind kind, bool anchored, double d);
    /// An Atlas's cannon shell: full damage out to the first radius, then half.
    /// Only enemies, and only on the ground: the Atlas fights in the front line.
    static constexpr std::array<Ring, 2> atlasSplash{{{0.5, 1}, {1.0, 0.5}}};
    /// A Scorpion's sting: full damage to the target, half to enemies (ground
    /// or air) within 1.25 cells; never its own side.
    static constexpr std::array<Ring, 2> scorpionSplash{{{0.5, 1}, {1.25, 0.5}}};
    /// An Atlas's Quake stomp: damage to every enemy on the ground within
    /// `stompRadius` (edge to the Atlas's centre), which also slows it
    /// (`slowTime`), every `stompCooldown` s. Fired by itself when
    /// `stompCrowd` or more stand that close.
    static constexpr double stompDamage = 30.0;
    static constexpr double stompRadius = 2.0;
    static constexpr double stompCooldown = 20.0;
    static constexpr int64_t stompCrowd = 3;
    /// An Atlas's torso (its cannons) and legs (the whole body) turn this
    /// fast, radians a second.
    static constexpr double atlasTorsoRate = 3.0;
    static constexpr double atlasBodyRate = 1.5;
    /// A Scorpion buries and digs out in this long (`anchorTime` is a Longbow's).
    static constexpr double buryTime = 2.0;
    /// Seconds a buried Scorpion needs to lock onto a victim before it stings.
    static constexpr double scorpionLock = 1.0;
    /// A buried Scorpion is seen by enemy units and buildings this close,
    /// and by an enemy Sentinel's whole sight; it is seen by all for
    /// `strikeReveal` s after a strike (and, as it reloads, throughout).
    static constexpr double revealRange = 2.0;
    static constexpr double strikeReveal = 3.0;
    /// A Juggernaut's grenade slows what it hits to half speed for this long.
    static constexpr double slowTime = 1.07;
    /// Dropship: hp healed a second, reach, and energy (1 per 3 hp healed,
    /// regrowing 0.7875 a second, 200 at most).
    static constexpr double healRate = 12.6;
    static constexpr double healRange = 4.0;
    static constexpr double maxEnergy = 200.0;
    static constexpr double energyRegen = 0.7875;
    /// Cargo slots of a Dropship, and how many each unit takes.
    static constexpr int64_t dropshipSlots = 8;
    /// The unit the player drives is a hero (why else take the
    /// wheel): its weapon hits harder and reaches farther, it moves faster,
    /// a Prospector carries five loads a trip and builds and repairs twice as
    /// fast. Only while driven. These are level 1 of pilot leveling
    /// (`Leveling.hero`); the sim reads them through `Simulation.boost`,
    /// which adds the picks on top.
    static constexpr double heroDamage = 1.5;
    static constexpr double heroRange = 1.25;
    static constexpr double heroSpeed = 1.2;
    static constexpr int64_t heroCarry = 25;
    static constexpr int64_t heroHydrogenCarry = 20;
    static constexpr double heroWork = 2.0;
    /// What a Dropship takes aboard: the infantry (bio units), and the
    /// Scorpion, which the harass AI carries to the enemy's ore lines. An
    /// Atlas would need more slots than any Dropship has.
    static bool boards(UnitKind k) { return stats(k).bio || k == UnitKind::scorpion; }
    /// An Atlas takes more slots than any Dropship has (even with Cargo bay).
    static int64_t slots(UnitKind k) {
        return k == UnitKind::atlas ? 16 : k == UnitKind::juggernaut ? 2 : k == UnitKind::longbow ? 4 : 1;
    }

    static int64_t cost(StructureKind k);
    static int64_t hydrogenCost(StructureKind k);
    static double buildTime(StructureKind k);
    static int64_t supply(StructureKind k);
    /// Half the footprint side: 5x5 Citadel; 3x3 Garrison, Bastion,
    /// Derrick, Foundry and Spacedock; 2x2 Hab Dome, Lab and Sentinel.
    static double radius(StructureKind k);
    /// A building needs one of these finished first (the tech tree).
    /// (`requires` is a C++ keyword.)
    static std::optional<StructureKind> requires_(StructureKind k);
    /// The units a building trains, the ones needing a Lab included.
    static std::vector<UnitKind> trains(StructureKind k);
    /// The unit a building trains when none is named (the first it trains).
    static std::optional<UnitKind> produces(StructureKind k);
    /// Units that need a Lab on their building (an add-on).
    static bool needsLab(UnitKind k) {
        return k == UnitKind::juggernaut || k == UnitKind::longbow || k == UnitKind::kestrel || k == UnitKind::atlas;
    }
    /// Where an add-on stands: on the building's +X side, toward the camera.
    static constexpr Vec2 addonOffset{2.5, 0.5};
    /// MH a well holds, taken 4 per trip, 3 Prospectors per Derrick.
    static constexpr int64_t wellHydrogen = 2250;
    static constexpr int64_t hydrogenCarry = 4;
    static constexpr int64_t workersPerWell = 3;
    /// Seconds a Prospector spends inside a Derrick per trip: 1.4 s slowed
    /// like the ore drilling.
    static constexpr double hydrogenTime = 3.5;

    static const UnitStats& stats(UnitKind k) { return UnitStats::byKind[static_cast<size_t>(ordinal(k))]; }
    static int64_t cost(UnitKind k) { return stats(k).ore; }
    static int64_t hydrogenCost(UnitKind k) { return stats(k).hydrogen; }
    static double trainTime(UnitKind k) { return stats(k).trainTime; }
    static double speed(UnitKind k) { return stats(k).speed; }
    static double hp(UnitKind k) { return stats(k).hp; }
    static int64_t supply(UnitKind k) { return stats(k).supply; }
    static double radius(UnitKind k) { return stats(k).radius; }
    static double hp(StructureKind k);
    /// The weapon of a building that shoots by itself (a Sentinel), if any.
    static std::optional<UnitStats> turret(StructureKind k);
    /// Damage a Ranger shot does after armour: buildings have 1 armour,
    /// and a hit always does at least 0.5.
    static double damage(bool toStructure);
    /// Damage one attack of `attacker` does to a target with these traits:
    /// per hit, the base plus any bonus against its attribute, less its
    /// armour (at least 0.5), times the hits. (Swift's `anchored` defaults
    /// to false.)
    static double damage(UnitKind attacker, bool anchored, bool armored, bool light, double armor);
    /// The same for weapon `a`.
    static double damage(const UnitStats& a, bool armored, bool light, double armor);

    // From Vision.swift.
    /// How far each kind sees (sight radii).
    static double vision(UnitKind k);
    static double vision(StructureKind k);
    /// A ground unit this close to a watchtower's centre holds it, and sees
    /// `towerSight` round the tower (22).
    static constexpr double towerReach = 2.6;
    static constexpr double towerSight = 22.0;
    /// How long a player keeps an enemy unit it has lost sight of in mind.
    static constexpr double memory = 60.0;
    /// How long a unit shot from out of its side's sight goes for where the
    /// shot came from.
    static constexpr double unseenChase = 3.0;
    /// A fighter on a follow order engages the enemy flyers within this
    /// many cells of the unit it follows (centre to centre), and holds its
    /// formation place about `escortGap` cells beside it.
    static constexpr double escortReach = 7.0;
    static constexpr double escortGap = 1.6;
};

inline constexpr std::array<UnitStats, 12> UnitStats::byKind{{
    // Fusion cutter: 5 a hit, in melee reach. It fights only when it
    // has to (hit, or driven by the player).
    /* prospector */ {.hp = 45, .speed = Rules::prospectorSpeed, .radius = 0.375, .supply = 1, .ore = 50, .trainTime = 12,
                      .damage = 5, .cooldown = 1.07, .range = 0.1, .light = true, .bio = true, .hitsGround = true},
    /* ranger */ {.hp = 45, .speed = 3.15, .radius = 0.375, .supply = 1, .ore = 50, .trainTime = 18,
                  .damage = 6, .cooldown = 0.61, .range = 5, .light = true, .bio = true, .hitsGround = true, .hitsAir = true},
    // Twin pistols, 4 (+5 light) each; heals out of combat; jumps cliffs.
    /* comet */ {.hp = 60, .speed = 5.25, .radius = 0.375, .supply = 1, .ore = 50, .hydrogen = 50, .trainTime = 32,
                 .damage = 4, .hits = 2, .bonusVsLight = 5, .cooldown = 0.79, .range = 5, .light = true, .bio = true,
                 .hitsGround = true},
    // Flamethrower, 8 (+6 light) to everything in a line.
    /* firefly */ {.hp = 90, .speed = 5.95, .radius = 0.625, .supply = 2, .ore = 100, .trainTime = 21,
                   .damage = 8, .bonusVsLight = 6, .cooldown = 1.79, .range = 5, .light = true, .hitsGround = true},
    // Breacher grenades, 10 (+10 armoured); each hit slows.
    /* juggernaut */ {.hp = 125, .armor = 1, .speed = 3.15, .radius = 0.5625, .supply = 2, .ore = 100, .hydrogen = 25,
                      .trainTime = 21, .damage = 10, .bonusVsArmored = 10, .cooldown = 1.07, .range = 6, .armored = true,
                      .bio = true, .hitsGround = true},
    /* dropship */ {.hp = 150, .armor = 1, .speed = 3.5, .radius = 0.75, .supply = 2, .ore = 100, .hydrogen = 100,
                    .trainTime = 30, .armored = true, .air = true},
    // Tank mode; `anchor` holds the anchored numbers.
    /* longbow */ {.hp = 175, .armor = 1, .speed = 3.15, .radius = 0.875, .supply = 3, .ore = 150, .hydrogen = 125,
                   .trainTime = 32, .damage = 15, .bonusVsArmored = 10, .cooldown = 1.04, .range = 7, .armored = true,
                   .hitsGround = true},
    // Twin rockets, 12 ×2, at the ground (`hitsGround`), and a rail gun at
    // the air (`UnitStats::railgun`, picked by `Simulation::weapon(u, air)`);
    // it flies over cliffs like the Dropship.
    /* kestrel */ {.hp = 140, .speed = 3.85, .radius = 0.625, .supply = 3, .ore = 150, .hydrogen = 100, .trainTime = 43,
                   .damage = 12, .hits = 2, .cooldown = 1.25, .range = 6, .armored = true, .air = true, .hitsGround = true,
                   .hitsAir = true},
    // Flak, 12 (+8 armoured), at the air only, splashing enemy flyers
    // round its target (`Rules.flakSplash`).
    /* hailstorm */ {.hp = 135, .armor = 1, .speed = 3.15, .radius = 0.75, .supply = 2, .ore = 125, .hydrogen = 50,
                     .trainTime = 30, .damage = 12, .bonusVsArmored = 8, .cooldown = 1.0, .range = 7, .armored = true,
                     .hitsAir = true},
    // Seeker missiles, 10 ×2 (+6 armoured), at the air only; a light flyer.
    /* peregrine */ {.hp = 80, .speed = 5.6, .radius = 0.5, .supply = 2, .ore = 100, .hydrogen = 75, .trainTime = 28,
                     .damage = 10, .hits = 2, .bonusVsArmored = 6, .cooldown = 1.25, .range = 7, .light = true, .air = true,
                     .hitsAir = true},
    // Twin siege cannons, 25 ×2 (+15 armoured), at the ground only, with
    // splash (`Rules::atlasSplash`); the Quake stomp is `Rules::stomp*`.
    /* atlas */ {.hp = 500, .armor = 2, .speed = 2.2, .radius = 1.25, .supply = 6, .ore = 300, .hydrogen = 200,
                 .trainTime = 60, .damage = 25, .hits = 2, .bonusVsArmored = 15, .cooldown = 2.0, .range = 7, .armored = true,
                 .hitsGround = true},
    // Sting, 60 (+30 armoured), ground and air, but only buried: `cooldown`
    // is its reload (it ticks while the Scorpion moves, too).
    /* scorpion */ {.hp = 90, .speed = 3.9, .radius = 0.5, .supply = 2, .ore = 75, .hydrogen = 25, .trainTime = 21,
                    .damage = 60, .bonusVsArmored = 30, .cooldown = 25, .range = 5, .light = true, .hitsGround = true,
                    .hitsAir = true},
}};

inline constexpr UnitStats UnitStats::minigun{.hp = 45, .speed = 3.15, .radius = 0.375, .supply = 1, .ore = 50, .trainTime = 18,
                                              .damage = 4, .cooldown = 0.2, .range = 5, .light = true, .bio = true, .hitsGround = true,
                                              .hitsAir = true, .burst = 3, .pause = 1};
inline constexpr UnitStats UnitStats::anchor{.hp = 175, .armor = 1, .speed = 0, .radius = 0.875, .supply = 3, .ore = 150, .hydrogen = 125,
                                             .trainTime = 32, .damage = 40, .bonusVsArmored = 30, .cooldown = 2.14, .range = 13,
                                             .minRange = 2, .armored = true, .hitsGround = true};
inline constexpr UnitStats UnitStats::railgun{.hp = 140, .speed = 3.85, .radius = 0.625, .supply = 3, .ore = 150, .hydrogen = 100, .trainTime = 43,
                                              .damage = 34, .bonusVsArmored = 14, .cooldown = 3.2, .range = 8, .armored = true, .air = true,
                                              .hitsAir = true, .rail = true};
inline constexpr UnitStats UnitStats::sentinel{.hp = 250, .armor = 1, .speed = 0, .radius = 1, .supply = 0, .ore = 100, .trainTime = 18,
                                               .damage = 12, .hits = 2, .cooldown = 1.0, .range = 7, .armored = true, .hitsAir = true};

} // namespace ac
