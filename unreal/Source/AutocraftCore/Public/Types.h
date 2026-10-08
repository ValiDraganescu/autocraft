// The game's data types, ported from Sources/GameCore: State.swift,
// MapDefinition.swift, Directives.swift, the record and effect types of
// Leveling.swift (with `Leveling`'s declarations) and `Intel` of Vision.swift.
// They live in one header because C++ needs a type complete before it is
// held by value, and the Swift types hold each other. Logic goes in the file
// named after its Swift file; the small methods of these types are in
// Types.cpp (Leveling's in Leveling.cpp).
#pragma once

#include "AutocraftCoreApi.h"

#include "MapView.h"
#include "SimdMath.h"
#include "Noise.h"
#include "Projection.h"
#include "Rules.h"

#include <array>
#include <compare>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace ac {

// MARK: - Helpers for Swift's enums

/// The raw strings of an enum without payloads, in declaration order
/// (specialised for each enum below).
template <class E> struct EnumInfo;

/// `E.allCases`.
template <class E> constexpr auto allCases() {
    constexpr size_t n = EnumInfo<E>::names.size();
    std::array<E, n> out{};
    for (size_t i = 0; i < n; i++) out[i] = static_cast<E>(i);
    return out;
}
/// `e.rawValue`.
template <class E> std::string_view rawValue(E e) { return EnumInfo<E>::names[static_cast<size_t>(e)]; }
/// `E(rawValue:)`.
template <class E> std::optional<E> parse(std::string_view s) {
    const auto& names = EnumInfo<E>::names;
    for (size_t i = 0; i < names.size(); i++) if (names[i] == s) return static_cast<E>(i);
    return std::nullopt;
}

/// A small `Set` of a payload-less enum, for constant sets that are only
/// asked `contains` (`Leveling.machines`).
template <class E> struct EnumSet {
    uint64_t bits = 0;
    constexpr EnumSet() = default;
    constexpr EnumSet(std::initializer_list<E> cases) { for (E e : cases) bits |= uint64_t(1) << static_cast<unsigned>(e); }
    constexpr bool contains(E e) const { return (bits >> static_cast<unsigned>(e)) & 1; }
    constexpr void insert(E e) { bits |= uint64_t(1) << static_cast<unsigned>(e); }
    constexpr bool operator==(const EnumSet&) const = default;
};

/// `indirect case`: a heap box with value semantics.
template <class T> class Indirect {
public:
    Indirect(T v) : p(std::make_unique<T>(std::move(v))) {}
    Indirect(const Indirect& o) : p(std::make_unique<T>(*o.p)) {}
    Indirect(Indirect&&) noexcept = default;
    Indirect& operator=(const Indirect& o) { p = std::make_unique<T>(*o.p); return *this; }
    Indirect& operator=(Indirect&&) noexcept = default;
    T& operator*() { return *p; }
    const T& operator*() const { return *p; }
    T* operator->() { return p.get(); }
    const T* operator->() const { return p.get(); }
    bool operator==(const Indirect& o) const { return *p == *o.p; }
private:
    std::unique_ptr<T> p;
};

template <class T, class V> struct IsCaseOf : std::false_type {};
template <class T, class... Ts> struct IsCaseOf<T, std::variant<Ts...>> : std::bool_constant<(std::is_same_v<T, Ts> || ...)> {};

/// The body of an enum with payloads: `value` holds one of its cases (small
/// structs named after the Swift cases, in the Swift order); a case converts
/// to the enum (`Mission m = Mission::Raid{p};`); `is<C>()` and `as<C>()`
/// test and unwrap (`if (auto r = m.as<Mission::Raid>()) ... r->at`).
#define AC_CASES(Name, ...)                                                                                    \
    using Value = std::variant<__VA_ARGS__>;                                                                   \
    Value value;                                                                                               \
    Name() = default;                                                                                          \
    template <class C, std::enable_if_t<IsCaseOf<std::decay_t<C>, Value>::value, int> = 0>                     \
    Name(C&& c) : value(std::forward<C>(c)) {}                                                                 \
    template <class C> bool is() const { return std::holds_alternative<C>(value); }                          \
    template <class C> const C* as() const { return std::get_if<C>(&value); }                                \
    template <class C> C* as() { return std::get_if<C>(&value); }                                            \
    size_t index() const { return value.index(); }

// MARK: - Enums the structs share

/// Research done at a Lab (real seconds). Each is passive once done.
enum class Upgrade : uint8_t {
    /// Garrison: Rangers trade the rifle for a mini gun (`UnitStats.minigun`).
    /// First, so the AI researches it before the shield.
    minigun,
    /// Garrison: Rangers +10 hit points.
    aegisShield,
    /// Foundry: Fireflies +5 damage against light units.
    novaIgniters,
    /// Spacedock: Dropships regain energy twice as fast.
    lifelineReactor,
};
/// The building whose Lab researches it.
AUTOCRAFTCORE_API StructureKind at(Upgrade u);
AUTOCRAFTCORE_API int64_t ore(Upgrade u);
AUTOCRAFTCORE_API int64_t hydrogen(Upgrade u);
AUTOCRAFTCORE_API double time(Upgrade u);
AUTOCRAFTCORE_API std::string name(Upgrade u);

/// How a team's Prospectors split between ore and MH.
enum class Harvest : uint8_t {
    /// Every Prospector on ore, and no new Derricks (MH a queued request
    /// needs is still mined).
    ore,
    /// The AI's own split: ore first, MH past the first lines.
    balanced,
    /// Every Derrick full, and Derricks early.
    hydrogen,
};

/// How a team's army plays when no objective takes it.
enum class Stance : uint8_t {
    /// The AI's own play: attack waves when the odds are good.
    auto_,
    /// Smaller waves at worse odds, and it holds on longer.
    aggressive,
    /// No attacks of its own: it defends and waits at the rally.
    hold,
    /// Everything attacks now and never falls back; a base under attack
    /// does not pull squads home.
    allIn,
};
AUTOCRAFTCORE_API std::string title(Stance s);

// MARK: - Effects (Leveling.swift)

/// A class of target a hit can do more against.
enum class TargetClass : uint8_t { armored, light, bio, building };

/// Kinds an aura or an `.around` change reaches.
enum class Filter : uint8_t { machines, bio, all, air };
bool covers(Filter f, UnitKind k);

/// A number a pick changes. Rates are "how fast": 10% faster is
/// `.percent(0.1)`, and a time is divided by the rate.
struct Stat {
    /// Move speed.
    struct Speed { auto operator<=>(const Speed&) const = default; };
    /// Drilling ore, and pumping MH inside a Derrick (rates).
    struct Drill { auto operator<=>(const Drill&) const = default; };
    struct Hydrogen { auto operator<=>(const Hydrogen&) const = default; };
    /// A load, ore or MH (the biggest `.atLeast` counts).
    struct OreCarry { auto operator<=>(const OreCarry&) const = default; };
    struct HydrogenCarry { auto operator<=>(const HydrogenCarry&) const = default; };
    /// Welding a building, and mending one (rates).
    struct Build { auto operator<=>(const Build&) const = default; };
    struct Repair { auto operator<=>(const Repair&) const = default; };
    /// Full hit points (`.plus`) and armour (`.plus`).
    struct Hp { auto operator<=>(const Hp&) const = default; };
    struct Armor { auto operator<=>(const Armor&) const = default; };
    /// A weapon's damage, its rate of fire and its range.
    struct Damage { auto operator<=>(const Damage&) const = default; };
    struct FireRate { auto operator<=>(const FireRate&) const = default; };
    struct Range { auto operator<=>(const Range&) const = default; };
    /// Extra damage against a class of target: `.plus` a hit, `.percent`
    /// of the whole attack (Demolition).
    struct DamageVs { TargetClass targetClass; auto operator<=>(const DamageVs&) const = default; };
    /// Hits ignore the target's armour (any `.plus` above 0).
    struct IgnoresArmor { auto operator<=>(const IgnoresArmor&) const = default; };
    /// Damage it takes (`.percent(-0.1)`: 10% less).
    struct DamageTaken { auto operator<=>(const DamageTaken&) const = default; };
    /// What a building it places costs (`.percent(-0.1)`: 10% less).
    struct BuildingCost { auto operator<=>(const BuildingCost&) const = default; };
    /// How far it sees (`Rules.vision`); its side sees what it sees.
    struct Sight { auto operator<=>(const Sight&) const = default; };
    /// A Firefly's flame: how far it reaches (its range) and how wide it
    /// burns (`Rules.flameWidth`).
    struct FlameReach { auto operator<=>(const FlameReach&) const = default; };
    struct FlameWidth { auto operator<=>(const FlameWidth&) const = default; };
    /// An anchored shell's splash radii (`Rules.splash`).
    struct Splash { auto operator<=>(const Splash&) const = default; };
    /// Anchoring and packing up (a rate; `.times(.infinity)`: at once).
    struct Anchor { auto operator<=>(const Anchor&) const = default; };
    /// Range anchored, on top of `range` (`.plus`).
    struct AnchorRange { auto operator<=>(const AnchorRange&) const = default; };
    /// A Juggernaut's slow: how long it lasts (`Rules.slowTime`), and the
    /// share of speed it takes (half; the biggest `.atLeast` counts).
    struct SlowTime { auto operator<=>(const SlowTime&) const = default; };
    struct Slow { auto operator<=>(const Slow&) const = default; };
    /// A Dropship's healing (a rate), the hit points an energy point heals
    /// (3), and its energy regrowth (a rate).
    struct Heal { auto operator<=>(const Heal&) const = default; };
    struct HealEnergy { auto operator<=>(const HealEnergy&) const = default; };
    struct Energy { auto operator<=>(const Energy&) const = default; };
    /// A Comet's healing out of combat (a rate).
    struct Regen { auto operator<=>(const Regen&) const = default; };
    /// How far a cliff jump looks for ground to land on.
    struct JumpReach { auto operator<=>(const JumpReach&) const = default; };
    /// A Dropship's cargo slots (`Rules.dropshipSlots`).
    struct Cargo { auto operator<=>(const Cargo&) const = default; };
    /// The share of a victim's cost its side gets for a kill (`.plus`).
    struct Bounty { auto operator<=>(const Bounty&) const = default; };
    // The team's economy (scope `.team`).
    /// How fast the team's buildings train a kind (a rate).
    struct Training { UnitKind kind; auto operator<=>(const Training&) const = default; };
    /// How fast a kind of building of the team trains anything (a rate).
    struct Production { StructureKind kind; auto operator<=>(const Production&) const = default; };
    /// What a kind costs the team, ore and MH.
    struct UnitCost { UnitKind kind; auto operator<=>(const UnitCost&) const = default; };
    /// What an upgrade researched at a kind of building costs, and how
    /// fast it is researched (a rate).
    struct UpgradeCost { StructureKind kind; auto operator<=>(const UpgradeCost&) const = default; };
    struct Research { StructureKind kind; auto operator<=>(const Research&) const = default; };
    /// Supply a Hab Dome gives.
    struct SupplyPerHabDome { auto operator<=>(const SupplyPerHabDome&) const = default; };
    /// Rangers a Bastion holds (`Rules.bastionCapacity`).
    struct BastionSize { auto operator<=>(const BastionSize&) const = default; };
    /// The share of a kind's cost the team gets back when it loses one
    /// (`.plus`).
    struct Refund { UnitKind kind; auto operator<=>(const Refund&) const = default; };
    /// An Atlas's Quake stomp: `.percent` of its damage, `.plus` cells of reach.
    struct Stomp { auto operator<=>(const Stomp&) const = default; };
    /// A Scorpion's burying and digging out (a rate).
    struct BuryTime { auto operator<=>(const BuryTime&) const = default; };
    /// How close an enemy must come to see a buried Scorpion (`.plus`, cells,
    /// on top of `Rules::revealRange`).
    struct RevealRange { auto operator<=>(const RevealRange&) const = default; };

    AC_CASES(Stat, Speed, Drill, Hydrogen, OreCarry, HydrogenCarry, Build, Repair, Hp, Armor, Damage, FireRate, Range,
             DamageVs, IgnoresArmor, DamageTaken, BuildingCost, Sight, FlameReach, FlameWidth, Splash, Anchor,
             AnchorRange, SlowTime, Slow, Heal, HealEnergy, Energy, Regen, JumpReach, Cargo, Bounty, Training,
             Production, UnitCost, UpgradeCost, Research, SupplyPerHabDome, BastionSize, Refund, Stomp, BuryTime, RevealRange)
    auto operator<=>(const Stat&) const = default;
};

/// Who a stat change reaches. All of it only while the player drives a
/// unit of the pick's kind, and only on the driven unit's team.
struct Scope {
    /// The driven unit.
    struct Driven { auto operator<=>(const Driven&) const = default; };
    /// Units of the driven kind within `Leveling.radius`, the driven one
    /// included.
    struct Nearby { auto operator<=>(const Nearby&) const = default; };
    /// Every unit of the driven kind on the team.
    struct Every { auto operator<=>(const Every&) const = default; };
    /// The team's units of these kinds within `Leveling.radius` of the
    /// driven one, whatever its kind (Bulwark).
    struct Around { Filter filter; auto operator<=>(const Around&) const = default; };
    /// The team itself: costs, training, supply (`Simulation.teamBoost`).
    struct Team { auto operator<=>(const Team&) const = default; };

    AC_CASES(Scope, Driven, Nearby, Every, Around, Team)
    auto operator<=>(const Scope&) const = default;
};

/// How a pick changes a stat. Speed-ups of the same thing add up; for a
/// load or a factor, the biggest counts.
struct Change {
    /// +x of the base (0.1: 10% more); these add up.
    struct Percent { double x; auto operator<=>(const Percent&) const = default; };
    /// Times x; the biggest counts (the hero's ×2 work, Fast welder's ×3).
    struct Times { double x; auto operator<=>(const Times&) const = default; };
    /// At least x; the biggest counts (a load of 35).
    struct AtLeast { double x; auto operator<=>(const AtLeast&) const = default; };
    /// Plus x; these add up (hp, armour, range).
    struct Plus { double x; auto operator<=>(const Plus&) const = default; };

    AC_CASES(Change, Percent, Times, AtLeast, Plus)
    auto operator<=>(const Change&) const = default;
};

/// The sum of the changes on one stat of one unit:
/// `max(base × times, atLeast) × (1 + percent) + plus`. With no changes
/// it leaves a number exactly as it was.
struct AUTOCRAFTCORE_API Boost {
    double times = 1.0;
    std::optional<double> atLeast;
    double percent = 0.0;
    double plus = 0.0;

    static const Boost none;

    void add(const Change& c);
    double apply(double base) const;
    /// A rate of 1 under the changes (2: twice as fast).
    double rate() const { return apply(1.0); }
    /// A whole number (a cost, a load) under the changes, rounded.
    int64_t apply(int64_t base) const;
    bool operator==(const Boost&) const = default;
};
inline constexpr Boost Boost::none{};

/// What the action does twice as well on every third time.
enum class Action : uint8_t {
    /// An ore trip of the driven Prospector carries double.
    oreTrip,
    /// An attack of the driven unit does double damage (a shot, a volley,
    /// a flame, a grenade, a shell).
    attack,
};

/// A mending aura around the driven unit: friendly units of `filter`
/// within `radius` (the driven one included) mend `hpPerSecond`.
struct Aura {
    double hpPerSecond = 1.0;
    double radius = 6.0; // Leveling::radius
    Filter filter = Filter::all;
    auto operator<=>(const Aura&) const = default;
};

/// The ability key's burst (Surge, Boost): for `seconds`, the driven unit
/// moves and fires faster, for `hpCost` hp, then waits out `cooldown`
/// (counted from the press).
struct Burst {
    double seconds = 0;
    double cooldown = 0;
    /// +x speed and fire rate while it lasts (1: double).
    double speed = 0;
    double fireRate = 0;
    double hpCost = 0;
    auto operator<=>(const Burst&) const = default;
};

/// A shield on the driven unit: `hp` that takes hits first, refilling to
/// full over `refill` seconds once `delay` seconds pass without a hit.
struct Shield {
    double hp = 0;
    double delay = 7.0;
    double refill = 10.0;
    auto operator<=>(const Shield&) const = default;
};

/// The ability key's grenade (Pulse mine): `damage` to every enemy on the
/// ground within `radius` of where it lands, at the crosshair, then a wait
/// of `cooldown`.
struct Grenade {
    double damage = 0;
    double radius = 0;
    double cooldown = 0;
    auto operator<=>(const Grenade&) const = default;
};

/// A few seconds faster: +`speed` and +`fireRate` for `seconds`
/// (Booster after a jump, Combat drop on the units set down).
struct Rush {
    double seconds = 0;
    double speed = 0;
    double fireRate = 0;
    auto operator<=>(const Rush&) const = default;
};

/// A unit on fire (Burn): it takes `perSecond` until `until`, game time,
/// as hits of unit `by`.
struct Burning {
    double until = 0;
    double perSecond = 0;
    int64_t by = 0;
    auto operator<=>(const Burning&) const = default;
};

/// One building block of a pick.
struct Effect {
    /// A stat change for some units (`Simulation.boost`, `teamBoost`).
    struct Stat { ac::Stat stat; ac::Scope scope; ac::Change change; auto operator<=>(const Stat&) const = default; };
    /// Every third time the driven unit does it, it counts double.
    struct EveryThird { Action action; auto operator<=>(const EveryThird&) const = default; };
    struct Aura { ac::Aura aura; auto operator<=>(const Aura&) const = default; };
    /// On the ability key.
    struct Burst { ac::Burst burst; auto operator<=>(const Burst&) const = default; };
    struct Shield { ac::Shield shield; auto operator<=>(const Shield&) const = default; };
    /// The driven unit mends itself `hpPerSecond` after `delay` seconds
    /// without a hit.
    struct SelfMend { double hpPerSecond; double delay; auto operator<=>(const SelfMend&) const = default; };
    /// The driven unit jumps up and down cliffs like a Comet.
    struct CliffJump { auto operator<=>(const CliffJump&) const = default; };
    /// The driven Prospector mends friendly machines: a full repair takes `share`
    /// of the machine's training time.
    struct MachineRepair { double share; auto operator<=>(const MachineRepair&) const = default; };
    // The one-offs.
    /// On the ability key: a grenade at the crosshair (Pulse mine).
    struct Grenade { ac::Grenade grenade; auto operator<=>(const Grenade&) const = default; };
    /// What the driven Firefly's flame hits burns for `seconds`.
    struct Burn { double perSecond; double seconds; auto operator<=>(const Burn&) const = default; };
    /// The driven Juggernaut's grenade also hits enemies within `radius` of
    /// its target for `share` of its damage.
    struct Fragment { double radius; double share; auto operator<=>(const Fragment&) const = default; };
    /// The driven Dropship heals a second patient at once.
    struct DoubleBeam { auto operator<=>(const DoubleBeam&) const = default; };
    /// The driven Dropship heals machines too.
    struct NaniteBeam { auto operator<=>(const NaniteBeam&) const = default; };
    /// Units the driven Dropship sets down rush.
    struct DropRush { Rush rush; auto operator<=>(const DropRush&) const = default; };
    /// The driven Dropship flies +`speed` faster while not healing.
    struct Cruise { double speed; auto operator<=>(const Cruise&) const = default; };
    /// The driven unit rushes after each cliff jump.
    struct AfterJump { Rush rush; auto operator<=>(const AfterJump&) const = default; };
    /// The driven Atlas's cannons also fire at flyers, for `share` of their damage.
    struct HitsAir { double share; auto operator<=>(const HitsAir&) const = default; };
    /// The driven Scorpion's sting also hits a second enemy within `radius` of the first.
    struct SecondTarget { double radius; auto operator<=>(const SecondTarget&) const = default; };
    /// The driven Scorpion's sting also hits buildings, for `share` of its damage.
    struct HitsBuildings { double share; auto operator<=>(const HitsBuildings&) const = default; };

    AC_CASES(Effect, Stat, EveryThird, Aura, Burst, Shield, SelfMend, CliffJump, MachineRepair, Grenade, Burn,
             Fragment, DoubleBeam, NaniteBeam, DropRush, Cruise, AfterJump, HitsAir, SecondTarget, HitsBuildings)
    auto operator<=>(const Effect&) const = default;
};

// MARK: - The catalogue (declared; the tables are in Leveling.cpp)

/// Every pick of every kind: one of two at each level from 2 to 10
/// (docs/leveling.md). `info` has its kind, level, slot, title and effect
/// text; `effects` what it does.
enum class Perk : uint8_t {
    prospectorQuickDrill, prospectorLightFrame, prospectorRigMaster, prospectorPlating, prospectorBigHaul, prospectorFastWelder, prospectorFieldWelder,
    prospectorGroupRepair, prospectorForeman, prospectorSiteBoss, prospectorRichVein, prospectorCuttingTorch, prospectorOvertime, prospectorPrefab, prospectorCliffHop,
    prospectorSupplyChief, prospectorUnionCrew, prospectorShield,
    rangerDrillSergeant, rangerLightKit, rangerCombatDrill, rangerFlakVest, rangerHollowPoints, rangerLongBarrel,
    rangerSurge, rangerFieldMedic, rangerSquadLeader, rangerQuartermaster, rangerBurstFire, rangerArmorPiercing,
    rangerEspritDeCorps, rangerRecruitment, rangerJumpPack, rangerBastionDrill, rangerBandOfBrothers, rangerShield,
    cometPackTactics, cometFleetFoot, cometNanomeds, cometPaddedSuit, cometTwinMagnums, cometLongJump,
    cometPulseMine, cometSpotter, cometRaidParty, cometDemolition, cometDoubleTap, cometLightKiller,
    cometHitAndRun, cometCheapKit, cometBooster, cometBounty, cometDeathSquad, cometShield,
    fireflyHotBurners, fireflyTurbo, fireflyFuelLine, fireflyArmorPlates, fireflyNapalm, fireflyWideNozzle,
    fireflyBurn, fireflyMechanic, fireflyPack, fireflySpread, fireflyFlashpoint, fireflyHotCore, fireflyOverdrive,
    fireflyCheapChassis, fireflyBoost, fireflyScrap, fireflyFirestorm, fireflyShield,
    juggernautShellDrill, juggernautLongStride, juggernautShockRounds, juggernautHeavyPlate, juggernautBreacher,
    juggernautDeepSlow, juggernautSurge, juggernautBulwark, juggernautSiegeBreakers, juggernautBehemoth,
    juggernautHeavyVolley, juggernautFragment, juggernautShockTroops, juggernautFieldKit, juggernautJumpPack,
    juggernautFieldResearch, juggernautWreckingCrew, juggernautShield,
    dropshipTriage, dropshipBoosters, dropshipLifeline, dropshipArmorPlates, dropshipSurgeon, dropshipCargoBay,
    dropshipNaniteBeam, dropshipHealingField, dropshipMedicalTeam, dropshipEfficient, dropshipDoubleBeam,
    dropshipCombatDrop, dropshipFieldHospital, dropshipCheapHull, dropshipCruise, dropshipSpacedockRefit,
    dropshipMedicalCorps, dropshipShield,
    longbowSpotter, longbowTreads, longbowQuickAnchor, longbowReactiveArmor, longbowShapedCharge, longbowLongGun, longbowCrewMechanic,
    longbowMechanic, longbowBattery, longbowHeavyShells, longbowDoubleShot, longbowAPShells, longbowDrilledCrews, longbowCheapHull,
    longbowHullDown, longbowHeavyIndustry, longbowArtilleryPark, longbowShield,
    kestrelWingman, kestrelAfterburners, kestrelKeenOptics, kestrelArmorPlates, kestrelHellfire, kestrelLongRails,
    kestrelFieldRepairs, kestrelMechanic, kestrelSquadron, kestrelWorkerHunter, kestrelDoubleSalvo, kestrelAPRockets,
    kestrelFlightDrill, kestrelCheapAirframe, kestrelAfterburn, kestrelBountyHunter, kestrelStrikeWing, kestrelShield,
    hailstormLoaders, hailstormTracks, hailstormWideBurst, hailstormArmorPlates, hailstormProximityFuse,
    hailstormLongBarrels, hailstormCrewMechanic, hailstormMechanic, hailstormFlakBattery, hailstormSkyWatch,
    hailstormDoubleBurst, hailstormAPFlak, hailstormDrilledGunners, hailstormCheapChassis, hailstormWalkerLegs,
    hailstormHeavyIndustry, hailstormFlakWall, hailstormShield,
    // docs/new-units.md: appended, since a saved game stores the index.
    peregrineWingmen, peregrineThrusters, peregrineLongRangeRadar, peregrineArmorPlates, peregrineSeekerHeads,
    peregrineLongRails, peregrineFieldRepairs, peregrineEscort, peregrinePackHunters, peregrineGunshipKiller,
    peregrineDoubleVolley, peregrineAPMissiles, peregrineFlightSchool, peregrineCheapAirframe, peregrineAfterburn,
    peregrineBountyHunter, peregrineAirSuperiority, peregrineShield,
    atlasAutoloader, atlasServoLegs, atlasRangefinder, atlasArmorPlates, atlasHeavyShells, atlasLongBarrels,
    atlasCrewMechanic, atlasMechanic, atlasBulwark, atlasWideShells, atlasDoubleVolley, atlasAPShells,
    atlasAftershock, atlasCheapFrame, atlasHeavyIndustry, atlasFlakMount, atlasWarMarch, atlasShield,
    scorpionQuickDig, scorpionLightFrame, scorpionSeismicSense, scorpionArmorPlates, scorpionHeavyCharge,
    scorpionLongTail, scorpionFieldRepairs, scorpionDeepBurrow, scorpionQuickReload, scorpionWorkerHunter,
    scorpionShapedCharge, scorpionTwinSting, scorpionMinefieldDrill, scorpionCheapShell, scorpionSapper,
    scorpionBountyHunter, scorpionNest, scorpionShield,
};

/// `Perk.Slot`: which of the two at its level: the table's left column or its right.
enum class PerkSlot : uint8_t { one, other };

/// `Perk.Info`: what the HUD shows of a pick.
struct PerkInfo {
    Perk perk;
    UnitKind kind;
    int64_t level;
    PerkSlot slot;
    /// "Quick drill".
    std::string title;
    /// One plain line: "Nearby Prospectors drill 10% faster."
    std::string effect;
    bool operator==(const PerkInfo&) const = default;
};

// `Perk`'s members (defined in Leveling.cpp, with the catalogue).
AUTOCRAFTCORE_API const PerkInfo& info(Perk p);
AUTOCRAFTCORE_API UnitKind kind(Perk p);
AUTOCRAFTCORE_API int64_t level(Perk p);
AUTOCRAFTCORE_API const std::string& title(Perk p);
AUTOCRAFTCORE_API const std::string& effect(Perk p);
/// What it does (`built`).
AUTOCRAFTCORE_API const std::vector<Effect>& effects(Perk p);
/// `Perk.offer(_:level:)`: the two picks a kind offers at `level` (2…10),
/// the same every game.
AUTOCRAFTCORE_API std::optional<std::pair<Perk, Perk>> perkOffer(UnitKind kind, int64_t level);

/// Pilot leveling (docs/leveling.md): the more the player drives a kind of
/// unit in a game, the better that kind gets, but only while the player
/// drives it. The numbers, the record and the perk catalogue live here; the
/// simulation's side (XP, picks, the effects) is in `Simulation+Leveling`.
/// (Defined in Leveling.cpp.)
struct AUTOCRAFTCORE_API Leveling {
    /// The highest level a kind reaches.
    static constexpr int64_t cap = 10;
    /// XP in all to reach `level`: `30 × L × (L − 1)` (0 for level 1).
    static double xp(int64_t toReach);
    /// The level `xp` in all reaches.
    static int64_t level(double xp);
    /// "Nearby" and the auras reach this far, cells, centre to centre.
    static constexpr double radius = 6.0;
    /// XP for one resource dropped off.
    static constexpr double oreXP = 1.0;
    static constexpr double hydrogenXP = 1.25;
    /// Damage dealt to a building, mending and healing earn this share of
    /// the plain rate.
    static constexpr double buildingXP = 0.5;
    static constexpr double mendXP = 0.5;
    /// A Dropship's passenger dropped this far from where it boarded earns
    /// this share of its cost.
    static constexpr double carryDistance = 10.0;
    static constexpr double carryXP = 0.1;
    /// A Dropship that healed this recently is still healing (Cruise).
    static constexpr double healing = 0.25;

    /// Machines: what a Prospector mends. Bio: what a Dropship heals.
    static constexpr EnumSet<UnitKind> machines{UnitKind::prospector, UnitKind::firefly, UnitKind::longbow,
                                                UnitKind::dropship, UnitKind::kestrel, UnitKind::hailstorm,
                                                UnitKind::peregrine, UnitKind::atlas, UnitKind::scorpion};
    static constexpr EnumSet<UnitKind> bio{UnitKind::ranger, UnitKind::comet, UnitKind::juggernaut};

    /// A unit's worth for XP: its ore plus its MH.
    static double worth(UnitKind k);
    static double worth(StructureKind k);

    /// Level 1: the driven unit is a hero (`Rules.hero*`). Picks build on
    /// top of these.
    static std::optional<Change> hero(const Stat& stat);

    /// The balance as text: every pick (its info and what it does), the
    /// XP curve and the numbers above, and `Rules.hero*`.
    static std::string balance();

    /// A hash of the balance, 16 hex digits: games tracked under one
    /// signature were played with the same picks and numbers. FNV-1a, the
    /// same on every launch (Swift's `Hasher` is seeded per launch).
    static const std::string& signature();

    /// FNV-1a 64 of `text`'s UTF-8, as 16 hex digits.
    static std::string fnv1a(const std::string& text);
};
static_assert(Leveling::radius == 6.0, "Aura's default radius is Leveling::radius");

// MARK: - The record (Leveling.swift)

/// What the player did with one kind in a game, counted where XP is
/// earned and only for the driven unit (docs/leveling.md, "Tracking").
/// Hp counts leave out overkill.
struct Tally {
    /// Seconds driven (while the driven unit is alive), and times taken
    /// over.
    double seconds = 0.0;
    int64_t takeovers = 0;
    /// Hp taken off enemy units and enemy buildings; units killed and
    /// buildings razed.
    double unitDamage = 0.0;
    double buildingDamage = 0.0;
    int64_t kills = 0;
    int64_t razed = 0;
    /// Hp the driven unit lost, and what its pilot shield took before that.
    double damageTaken = 0.0;
    double shieldAbsorbed = 0.0;
    /// Times the driven unit died.
    int64_t deaths = 0;
    /// Ore and MH dropped off.
    int64_t ore = 0;
    int64_t hydrogen = 0;
    /// Buildings put up, in hp: a building's full hp times the share of its
    /// build time welded, so a whole building counts its full hp.
    double built = 0.0;
    /// Hp mended (buildings, and machines with Field welder) and healed.
    double repaired = 0.0;
    double healed = 0.0;
    /// Passengers set down 10 or more cells from where they boarded: the
    /// carries that earn XP.
    int64_t carried = 0;
    /// Burn damage (counted in `unitDamage` too), and the units and
    /// buildings a Pulse mine hit.
    double burn = 0.0;
    int64_t blastHits = 0;
    /// Flyers killed (a share of `kills`).
    int64_t airKills = 0;
    /// Quake stomps used, and the units they hit.
    int64_t stomps = 0;
    int64_t stompHits = 0;
    /// Stings fired, and the units they hit (splash included).
    int64_t strikes = 0;
    int64_t strikeHits = 0;
    /// Seconds buried and unseen by any enemy.
    double hiddenSeconds = 0.0;

    bool operator==(const Tally&) const = default;
};

/// How a player stands at a moment (`Simulation.standing(of:)`).
struct Standing {
    /// What its army cost at list price, ore plus MH: every unit
    /// but Prospectors.
    int64_t army = 0;
    /// Prospectors.
    int64_t workers = 0;
    /// In the bank.
    int64_t ore = 0;
    int64_t hydrogen = 0;
    int64_t supplyUsed = 0;
    int64_t supplyCap = 0;
    /// Finished buildings.
    int64_t buildings = 0;
    /// Ore mined this game.
    int64_t mined = 0;

    /// Two standings added up (the enemies' together).
    friend Standing operator+(const Standing& a, const Standing& b);
    bool operator==(const Standing&) const = default;
};

/// A level reached: when (game time), the seconds driven with the kind
/// before it, every player's standing then, and when its card was picked
/// (nil: not yet).
struct Stamp {
    int64_t level = 0;
    double at = 0;
    double driven = 0;
    std::vector<Standing> standing;
    std::optional<double> picked;
    bool operator==(const Stamp&) const = default;
};

/// One kind's XP and picks this game, and the state of its building
/// blocks (the every-third counter, the ability key's clock).
struct KindRecord {
    double xp = 0.0;
    std::vector<Perk> picks;
    /// Times the every-third action has been done.
    int64_t count = 0;
    /// The burst runs until, and can be used again at (game time).
    std::optional<double> burstUntil;
    std::optional<double> burstReady;
    /// When the driven Dropship last healed (Cruise), game time.
    std::optional<double> healedAt;
    /// What the player did with this kind this game (for tracking).
    Tally tally;
    /// One for each level reached, lowest first (for tracking).
    std::vector<Stamp> stamps;

    int64_t level() const { return Leveling::level(xp); }
    bool operator==(const KindRecord&) const = default;
};

/// The player's leveling this game, kept on the human `Player` (`pilot`)
/// so it is saved with the session; a new game starts without one.
struct AUTOCRAFTCORE_API PilotRecord {
    /// Each kind the player has driven this game.
    std::map<UnitKind, KindRecord> kinds;

    /// One pending choice: the two picks on offer at `level`.
    struct Offer {
        int64_t level;
        Perk one;
        Perk other;
        std::vector<Perk> perks() const { return {one, other}; }
        bool operator==(const Offer&) const = default;
    };

    int64_t level(UnitKind k) const;
    double xp(UnitKind k) const;
    /// The picks made for a kind, lowest level first.
    std::vector<Perk> picks(UnitKind k) const;
    /// (Defined in Leveling.cpp: it needs the catalogue.)
    bool has(Perk perk) const;
    /// The choices waiting for a kind, lowest level first. Only the first
    /// can be picked from now. (Defined in Leveling.cpp.)
    std::vector<Offer> pending(UnitKind k) const;
    /// The kinds driven this game, in `Unit.Kind` order (the command map's
    /// list).
    std::vector<UnitKind> driven() const;
    /// Why `perk` cannot be picked now, or nil: it must be one of the two
    /// on offer at the kind's lowest pending level. (Defined in Leveling.cpp.)
    std::optional<std::string> refusal(Perk perk) const;

    bool operator==(const PilotRecord&) const = default;
};

// MARK: - The map (MapDefinition.swift)

/// Raised ground bounded by cliffs.
struct Plateau {
    /// Cliff edge, ground coordinates (x, z), either winding.
    std::vector<Vec2> polygon;
    /// Terrain level: 0 is low ground, 1 is high ground.
    int64_t level = 0;
    bool operator==(const Plateau&) const = default;
};

/// A slope through a cliff from `low` (on the lower level) to `high`.
struct Ramp {
    Vec2 low;
    Vec2 high;
    double width = 0;
    int64_t lowLevel = 0;
    int64_t highLevel = 0;
    bool operator==(const Ramp&) const = default;
};

/// A soft patch of a ground material painted over the base dirt.
struct GroundPatch {
    enum class Kind : uint8_t { grass, gravel, scorch };
    Kind kind = Kind::grass;
    Vec2 center;
    double radius = 0;
    bool operator==(const GroundPatch&) const = default;
};

struct OreSpec {
    Vec2 position;
    int64_t amount = 0;
    /// Rotation of the patch's long (2-cell) axis around Y, radians.
    double angle = 0;
    bool operator==(const OreSpec&) const = default;
};

/// A place where a Citadel can stand, with its resources.
struct BaseSite {
    Vec2 center;
    int64_t level = 0;
    std::vector<OreSpec> ore;
    std::vector<Vec2> wells;

    /// The standard layout: eight patches in an arc 6.5–7.5 cells from the
    /// base centre around `facing` (radians, 0 = +X, π/2 = +Z), alternating
    /// 1800 and 900, with a well beyond each end of the arc.
    static BaseSite standard(Vec2 center, int64_t level, double facing);
    bool operator==(const BaseSite&) const = default;
};

struct Doodad {
    enum class Kind : uint8_t {
        rock, boulder, rockSpire, crate, crateStack, plant, bush, debris, deadTree, tower
    };
    Kind kind = Kind::rock;
    Vec2 position;
    double rotation = 0;
    double scale = 1;
    /// Seed for the doodad's own shape variation.
    uint32_t variant = 0;
    /// The mirror image of its kind (flipped along its own x before the
    /// turn), for the east half of a mirrored map.
    std::optional<bool> mirrored;
    bool operator==(const Doodad&) const = default;
};

/// Everything static about a map. Ground coordinates are cells.
struct AUTOCRAFTCORE_API MapDefinition {
    std::string name;
    /// Bump when a map's layout changes; old sessions on it are restarted.
    int64_t version = 0;
    uint64_t seed = 0;
    GroundRect bounds;
    /// Height of one cliff level, cells.
    double levelHeight = 0;
    std::vector<Plateau> plateaus;
    std::vector<Ramp> ramps;
    std::vector<GroundPatch> patches;
    std::vector<BaseSite> bases;
    /// Indices into `bases` of the players' start bases: player 0 (blue)
    /// first, then player 1 (red). On a map for more players (C++ only)
    /// they go round the map in order, so neighbouring starts make a team's
    /// ground (`GameState::new_`).
    std::vector<int64_t> starts;
    std::vector<Doodad> doodads;
    /// A map whose east half is its west half mirrored across x = mirrorX
    /// (nil: no symmetry). Terrain noise and the walking grid follow it, so
    /// neither side has a better way anywhere.
    std::optional<double> mirrorX;
    /// C++ only: a map for more players that is its own image under the
    /// square's eight symmetries about (0, 0), quarter turns and mirrors
    /// across the axes and diagonals (`WindowMaps::buildSquare`). Terrain
    /// noise follows it, so every start has the same ground (nil: none).
    std::optional<bool> squareSymmetric;
    /// What the screens show; nil: all of `bounds` is in play. Ground out of
    /// sight is not walkable, so nothing is ever built or fought there.
    std::optional<MapView> view;
    /// The playground (`WindowMaps.playground`): flat ground with nothing
    /// on it, two sides with nothing, and no one ever wins (nil: a map).
    std::optional<bool> playground;

    /// Where the armies meet: halfway between the feet of the ramps (or
    /// between the start bases on a map with one ramp), so neither side's
    /// army stands closer to the fight on a mirrored map.
    Vec2 front() const;

    /// A ground point is in play: the screens show it (`inset` points clear
    /// of their edges) and, on a mirrored map, its mirror image too, so both
    /// halves play on the same ground.
    bool inPlay(Vec2 p, double inset, const std::function<double(Vec2)>& height) const;
    /// The same with Swift's default `inset` of 24.
    bool inPlay(Vec2 p, const std::function<double(Vec2)>& height) const { return inPlay(p, 24, height); }

    bool operator==(const MapDefinition&) const = default;
};

// MARK: - The state (State.swift)

struct AUTOCRAFTCORE_API OreDeposit {
    int64_t id = 0;
    Vec2 position;
    double angle = 0;
    int64_t initial = 0;
    int64_t remaining = 0;
    /// When the patch ran dry (game time); it regrows `Rules.regrowDelay` later.
    std::optional<double> depletedAt;
    /// The Prospector drilling it now. One at a time; others wait or
    /// move to a free patch.
    std::optional<int64_t> miner;
    /// A deposit shows four sizes as it is mined out: 3 full … 0 gone.
    int64_t stage() const;
    bool operator==(const OreDeposit&) const = default;
};

struct AUTOCRAFTCORE_API Structure {
    using Kind = StructureKind;
    int64_t id = 0;
    Kind kind = Kind::citadel;
    /// The player it belongs to (index into `GameState.players`).
    int64_t owner = 0;
    Vec2 position;
    /// Hit points; a building under construction starts at 10% and gains
    /// the rest as it rises.
    double hp = 0;
    /// Construction seconds left; nil once built.
    std::optional<double> buildLeft;
    /// The Prospector building it.
    std::optional<int64_t> builder;
    /// Seconds left on the unit in training; nil when idle.
    std::optional<double> training;
    /// The production queue, the unit in training first (paid, and holding
    /// supply).
    std::optional<std::vector<UnitKind>> line;
    /// Rangers inside a Bastion.
    std::optional<std::vector<int64_t>> crew;
    /// Its Lab (on a Garrison, Foundry or Spacedock), and on a Lab
    /// the building it serves.
    std::optional<int64_t> addon;
    std::optional<int64_t> parent;
    /// A Lab researching: the upgrade and the seconds left.
    std::optional<Upgrade> research;
    std::optional<double> researchLeft;
    /// A building that shoots (a Sentinel): what it is shooting at, seconds
    /// until it can fire again, and where its launcher points (world radians).
    std::optional<int64_t> target;
    std::optional<double> cooldown;
    std::optional<double> aim;

    /// For decoding; Swift has only the init below.
    Structure() = default;
    /// Swift's `owner` defaults to 0.
    Structure(int64_t id_, Kind kind_, int64_t owner_, Vec2 position_, std::optional<double> buildLeft_ = std::nullopt,
              std::optional<int64_t> builder_ = std::nullopt);

    /// Units in the production queue, the one in training included (0…5).
    int64_t queueCount() const;
    /// The unit in training.
    std::optional<UnitKind> inTraining() const;
    /// 0…1 through the unit in training; nil when idle.
    std::optional<double> trainingProgress() const;

    bool complete() const { return !buildLeft.has_value(); }
    /// 0…1 while under construction, 1 when built.
    double progress() const;

    bool operator==(const Structure&) const = default;
};

/// Where a Prospector is headed to build.
struct BuildOrder {
    StructureKind kind = StructureKind::citadel;
    Vec2 position;
    bool operator==(const BuildOrder&) const = default;
};

/// An MH well: a Derrick on it lets Prospectors take its MH.
struct Well {
    Vec2 position;
    int64_t remaining = 0;
    /// The Prospector inside its Derrick now (one at a time).
    std::optional<int64_t> harvester;
    bool operator==(const Well&) const = default;
};

/// A unit's own errand, apart from the army's orders.
struct Mission {
    /// Harass: go to a point (an enemy ore line), hunting workers there.
    struct Raid { Vec2 at; bool operator==(const Raid&) const = default; };
    /// Fly a load of units to a point and drop them there.
    struct Drop { Vec2 at; bool operator==(const Drop&) const = default; };
    /// Walk back to a point without stopping to fight (a raider pulling out).
    struct FallBack { Vec2 at; bool operator==(const FallBack&) const = default; };
    /// Hunt down raiders around a point in its own base.
    struct Hunt { Vec2 at; bool operator==(const Hunt&) const = default; };
    /// In the squad for one of its team's objectives (`Objective`): walk to
    /// the point and fight what is around it.
    struct Assault { int64_t objective; Vec2 at; bool operator==(const Assault&) const = default; };
    /// In the squad guarding a point for an objective (a base to defend, or
    /// the rally while an attack gathers): stand around it and fight what
    /// comes near.
    struct Hold { int64_t objective; Vec2 at; bool operator==(const Hold&) const = default; };
    /// A squad pulled back from its objective: walk to the point without
    /// stopping to fight, then gather again.
    struct Regroup { int64_t objective; Vec2 at; bool operator==(const Regroup&) const = default; };
    /// Escort: follow a friendly unit (`unit`), holding formation beside it and
    /// re-pathing as it moves. A fighter that can hit flyers also engages
    /// the enemy flyers that come within `Rules::escortReach` of the unit it
    /// follows, and returns to formation after. Ends when the unit is gone.
    struct Follow { int64_t unit; bool operator==(const Follow&) const = default; };

    AC_CASES(Mission, Raid, Drop, FallBack, Hunt, Assault, Hold, Regroup, Follow)

    /// The objective whose squad it is in, if any.
    std::optional<int64_t> objective() const;

    bool operator==(const Mission&) const = default;
};

struct AUTOCRAFTCORE_API Unit {
    using Kind = UnitKind;
    enum class Task : uint8_t {
        idle, toPatch, mining, toBase, depositing,
        /// At its patch while another Prospector drills it.
        waiting,
        toBuild, building,
        /// A Ranger walking to its place at the rally point (standing
        /// there is `idle`).
        toRally,
        /// A Ranger walking to the army's attack point, fighting what it
        /// meets on the way.
        attackMove,
        /// A Ranger closing on or shooting `target`.
        attacking,
        /// A Ranger walking to the Bastion `structure`, and inside it.
        toBastion, inBastion,
        /// A Prospector mending the building `structure`.
        repairing,
        /// A Prospector walking to the Derrick `structure`, and inside it.
        toDerrick, inDerrick,
        /// Carried inside a Dropship (`structure` is the Dropship's id).
        aboard,
        /// A Prospector on an errand of its AI's (`mission`): scouting
        /// (`.raid`), or pulled off the ore to fight (`.hunt`).
        errand,
    };
    int64_t id = 0;
    Kind kind = Kind::prospector;
    int64_t owner = 0;
    Vec2 position;
    double hp = 0;
    /// Facing, radians, 0 = +X, π/2 = +Z.
    double heading = 0;
    Task task = Task::idle;
    /// Index into `GameState.patches` of the patch it mines.
    std::optional<int64_t> patch;
    double timer = 0;
    int64_t carrying = 0;
    /// Cumulative distance walked (drives the walk cycle).
    double stride = 0;
    std::optional<BuildOrder> order;
    /// The structure it is building.
    std::optional<int64_t> structure;
    /// Where it is walking and the ramp waypoints still ahead on the way.
    std::optional<Vec2> goal;
    std::optional<std::vector<Vec2>> waypoints;
    /// The unit or structure it shoots at (ids are unique across both).
    std::optional<int64_t> target;
    /// Seconds until it can fire again.
    std::optional<double> cooldown;
    /// True while it is moving (drives the walk cycle); set by the simulation.
    std::optional<bool> moving;
    /// What it carries is MH, not ore.
    std::optional<bool> hydrogen;
    /// When it was last hit (game time); a Comet heals only after a while.
    std::optional<double> hitAt;
    /// Slowed (a Juggernaut grenade) until this game time, to this share of
    /// its speed (nil: half; Deep slow).
    std::optional<double> slowUntil;
    std::optional<double> slowedTo;
    /// A Longbow: 0 in tank mode … 1 anchored, moving between the two
    /// while `anchored` says which way it is going.
    std::optional<double> anchor;
    std::optional<bool> anchored;
    /// A tank's or a Hailstorm's turret heading, or a Firefly's tail's (world radians).
    std::optional<double> aim;
    /// A Comet crossing a cliff: where it took off and where it lands.
    std::optional<Vec2> jumpFrom;
    std::optional<Vec2> jumpTo;
    /// A Dropship's energy and the units aboard it.
    std::optional<double> energy;
    std::optional<std::vector<int64_t>> cargo;
    /// Its own errand (a raid or a drop); nil: it follows the army.
    std::optional<Mission> mission;
    /// A Peregrine's follow order is its own doing (`Simulation.autoEscort`),
    /// not an order it was given: any order replaces it.
    std::optional<bool> autoFollow;
    /// A gun that fires in bursts (a Ranger's mini gun): when its current
    /// burst began (nil: none under way) and when it last fired, game time.
    std::optional<double> burstFrom;
    std::optional<double> firedAt;
    /// A Kestrel's rail gun has its own clock: seconds until it can fire.
    std::optional<double> railCooldown;
    /// Where a shot from something its side could not see came from (see
    /// `Simulation.stepSoldier`).
    std::optional<Vec2> shotFrom;
    /// The driven unit's shield (a leveling pick): it takes hits first.
    std::optional<double> shield;
    /// Hit points its side's leveling picks add now (see
    /// `Simulation.syncBonusHP`).
    std::optional<double> bonusHP;
    /// Aboard a Dropship: where it boarded (a long carry earns the driven
    /// Dropship XP).
    std::optional<Vec2> boardedAt;
    /// A leveling pick's rush (Booster, Combat drop): faster until
    /// `rushUntil`, game time.
    std::optional<Rush> rush;
    std::optional<double> rushUntil;
    /// On fire from the driven Firefly's flame (Burn).
    std::optional<Burning> burning;
    /// A Scorpion locking onto `lockTarget` (since `lockFrom`, last held at
    /// `lockAt`, game time): it stings once the lock has held a second.
    std::optional<int64_t> lockTarget;
    std::optional<double> lockFrom;
    std::optional<double> lockAt;
    /// An Atlas's Quake stomp is ready again at this game time, and last
    /// shook the ground at `stompedAt` (for the scene).
    std::optional<double> stompReady;
    std::optional<double> stompedAt;
    /// A Scorpion last stung at this game time: it is seen by all for
    /// `Rules::strikeReveal` s after (its own clock, apart from the reload).
    std::optional<double> struckAt;

    /// For decoding; Swift has only the init below.
    Unit() = default;
    /// Swift's `owner` defaults to 0.
    Unit(int64_t id_, Kind kind_, int64_t owner_, Vec2 position_, double heading_, Task task_,
         std::optional<int64_t> patch_ = std::nullopt, double timer_ = 0, int64_t carrying_ = 0, double stride_ = 0);

    bool walking() const;
    /// A Scorpion down in the ground (`anchor` is how far it has dug): it can
    /// sting, and it cannot move.
    bool burrowed() const { return kind == Kind::scorpion && anchor.value_or(0) >= 1 && anchored == true; }
    /// A unit held where it is by its mode: a Longbow or a Scorpion burying,
    /// buried or digging out.
    bool planted() const { return (kind == Kind::longbow || kind == Kind::scorpion) && anchor.value_or(0) > 0; }
    const UnitStats& stats() const { return Rules::stats(kind); }
    /// Soldiers: everything that is not a Prospector.
    bool soldier() const { return kind != Kind::prospector; }
    /// Where it looks, world radians: a tank's turret or a Firefly's tail,
    /// else its heading.
    double look() const { return aim ? *aim : heading; }
    /// 0…1 through a cliff jump; nil on the ground.
    std::optional<double> jump() const;
    /// Drilling ore or welding a building: the tool is running.
    bool working() const;

    bool operator==(const Unit&) const = default;
};

// MARK: - Directives (Directives.swift)

/// One thing a team's humans asked its AI to spend on.
struct AUTOCRAFTCORE_API Request {
    struct What {
        struct Unit { UnitKind kind; bool operator==(const Unit&) const = default; };
        /// A building, placed by the AI. A Lab goes on the building
        /// `at` when that one can take it, else on any that can.
        struct Building { StructureKind kind; bool operator==(const Building&) const = default; };
        struct Upgrade { ac::Upgrade upgrade; bool operator==(const Upgrade&) const = default; };

        AC_CASES(What, Unit, Building, Upgrade)
        bool operator==(const What&) const = default;
    };

    int64_t id = 0;
    What what;
    /// Units still to train (1 for a building or an upgrade).
    int64_t count = 1;
    /// Keep training it: the request is never used up.
    bool repeats = false;
    /// The building it was asked at: a unit trains there and a Lab
    /// goes on it when it can.
    std::optional<int64_t> at;
    /// A Citadel's base site (index into the map's bases): it goes
    /// there rather than where the AI would put it.
    std::optional<int64_t> site;

    int64_t ore() const;
    int64_t hydrogen() const;
    /// (Defined with `Simulation.title`, which it reads.)
    std::string title() const;

    bool operator==(const Request&) const = default;
};

/// Where a team's humans send its army. Each objective gets a squad split
/// off the army by the AI, as big as it judges the job needs.
struct Objective {
    enum class Kind : uint8_t {
        /// Attack-move to the point and clear it; once the squad stands
        /// there with nothing of the enemy's near, it becomes `hold`.
        attack,
        /// Guard a base (the point is its Citadel or site): stand
        /// there and fight what comes; kept until taken off.
        defend,
        /// Hold an area taken by an attack: the squad stays at the point
        /// and fights what comes; kept until taken off.
        hold,
        /// Keep a Bastion (`structure`) full of Rangers, ahead of everything
        /// else: Rangers are pulled from the army and squads, and trained
        /// first when there are too few.
        man,
    };
    int64_t id = 0;
    Kind kind = Kind::attack;
    Vec2 at;
    /// The Bastion to man.
    std::optional<int64_t> structure;
    bool operator==(const Objective&) const = default;
};

/// What a team's human players ask of its AI commander:
/// what to spend on next, what to keep in the bank, how to split the Prospectors
/// between ore and MH, how the army plays (`Stance`) and where it
/// goes (`Objective`).
///
/// One per player (`Player.directives`), saved with the game. Any human on
/// the team changes it with a `Command` (`.request`, `.cancelRequest`,
/// `.keep`, `.split`, `.objective`, `.cancelObjective`, `.stance`), the way a
/// player over the network would, and that
/// team's `Commander` reads it once a second. Every team runs the same
/// `Commander`; a team with no humans simply has none.
struct Directives {
    /// What to spend on next, first come first served. The AI saves for
    /// each in turn before its own spending and puts up what each needs
    /// first (a Foundry before a Longbow, a Lab before an upgrade).
    std::vector<Request> queue;
    /// Ore and MH the AI leaves in the bank when it spends for itself.
    /// The queue may spend them: it is the humans' own order.
    int64_t keepOre = 0;
    int64_t keepHydrogen = 0;
    /// How the Prospectors split between ore and MH.
    Harvest harvest = Harvest::balanced;
    /// Where the army goes: attacks and bases to defend, each with a squad
    /// the AI splits off the army for it (see `Commander.squads`).
    std::vector<Objective> objectives;
    /// How the army plays when no objective takes it.
    Stance stance = Stance::auto_;
    /// The next request's or objective's id (one count for both).
    int64_t nextID = 1;

    /// The queued MH not yet in the bank: a team on `.ore` still
    /// pumps MH for what its humans asked for.
    int64_t hydrogenWanted() const;

    bool operator==(const Directives&) const = default;
};

/// Where an objective stands, for the humans: its squad and what it does.
struct ObjectiveStatus {
    int64_t id = 0;
    /// "Attacking", "Gathering 6 / 14 at the rally", "Regrouping".
    std::string text;
    /// The squad's soldiers, and where they are on average (nil: none).
    std::vector<int64_t> units;
    std::optional<Vec2> centre;
    bool operator==(const ObjectiveStatus&) const = default;
};

/// A base site as the humans see it when choosing where to expand.
struct SiteInfo {
    int64_t index = 0;
    Vec2 centre;
    /// The player with a Citadel there (nil: free).
    std::optional<int64_t> owner;
    /// About as far from an enemy start as from its own: the AI takes it
    /// only late.
    bool contested = false;
    /// The site the AI would expand to next.
    bool next = false;
    bool operator==(const SiteInfo&) const = default;
};

/// Where a request stands, for the humans: what the AI is doing about it.
struct RequestStatus {
    int64_t id = 0;
    /// "Saving: 40 ore short", "Foundry first", "Waiting for the Lab".
    std::string text;
    /// 0…1: how much of its cost is in the bank (1 while it waits on
    /// something other than money).
    double funded = 0;
    bool operator==(const RequestStatus&) const = default;
};

// MARK: - Players and the game

/// One player's economy and army orders.
struct Player {
    /// The AI's army styles.
    enum class Style : uint8_t {
        /// Rangers and Juggernauts with Dropships; Labs on half the
        /// Garrisons, a couple of Longbows.
        bio,
        /// Longbows and Fireflies from two Foundries, fewer Garrisons.
        mech,
        /// Bio behind a pack of Fireflies that runs into ore lines.
        harass,
    };

    // Swift's `init(ore: Int = Rules.startingOre, aggression: Double = 0.5)`
    // is these defaults: `Player{.aggression = a}`.
    int64_t ore = Rules::startingOre;
    int64_t hydrogen = 0;
    int64_t supplyUsed = 1;
    int64_t supplyCap = 15;
    int64_t totalMined = 0;
    /// Where the army attack-moves to; nil: it stands at its rally.
    std::optional<Vec2> attack;
    /// True while the army falls back to its rally.
    bool retreating = false;
    /// Game time the last retreat began (nil: none, or an old session).
    std::optional<double> retreatedAt;
    /// True when `attack` is a point in its own base being defended: the
    /// army fights there and does not chase far.
    std::optional<bool> defending;
    /// Rangers waiting at the rally join the fight once this many are
    /// there (nil: `Simulation.reinforceGroup`).
    std::optional<int64_t> reinforce;
    /// Attack waves launched this game.
    int64_t waves = 0;
    /// The AI's temperament this game, 0…1 (drawn per game): high attacks
    /// earlier and with smaller odds, and holds on longer.
    double aggression = 0.5;
    /// 0…1, drawn per game: high expands earlier, with fewer Prospectors out on
    /// the bases it has (nil: 0).
    std::optional<double> greed;
    /// Garrison per base, 1…3, drawn per game (nil: 2).
    std::optional<int64_t> garrisonPerBase;
    /// Comets it keeps raiding with, 1…2, drawn per game (nil: 1).
    std::optional<int64_t> comets;
    /// Index into the map's bases of where it started (random per game;
    /// nil: the map's order).
    std::optional<int64_t> start;
    /// The army it builds, drawn per game (nil: bio).
    std::optional<Style> style;
    /// Fireflies it keeps for runbys on enemy ore lines, drawn per game:
    /// 0…2, or 4…6 for Firefly harass (nil: 0).
    std::optional<int64_t> fireflies;
    /// Whether it drops Rangers and Juggernauts from Dropships into enemy
    /// ore lines, drawn per game (nil: no).
    std::optional<bool> drops;
    /// Upgrades researched this game (nil: none).
    std::optional<std::set<Upgrade>> upgrades;
    /// What the team's humans asked its AI for (nil: nothing; see `orders`).
    std::optional<Directives> directives;
    /// The human's pilot leveling this game: XP and picks per kind (nil:
    /// none yet, or an old session; see `Simulation.levels`).
    std::optional<PilotRecord> pilot;
    /// Units it has lost this game (nil: none yet, or an old session).
    std::optional<int64_t> unitsLost;
    /// The team it plays on (C++ only, PORTING.md "Teams"): players with
    /// the same team are allies, share their sight and win together. Nil:
    /// its own index, every player for itself, as in the Swift game (and
    /// every old save). Set for all players or for none.
    std::optional<int64_t> team;

    /// The team's directives (nil: none given).
    Directives orders() const { return directives ? *directives : Directives(); }

    bool operator==(const Player&) const = default;
};

/// What a player knows of the others: their buildings as it last saw them,
/// their units seen lately, the ground it has ever seen and when it last
/// saw each base site. Saved with the game.
struct Intel {
    struct Seen {
        Unit unit;
        double at = 0;
        bool operator==(const Seen&) const = default;
    };

    /// Buildings of the others, as last seen; one is forgotten once the
    /// ground it stood on is seen without it.
    std::vector<Structure> buildings;
    /// Units of the others, as and when last seen; forgotten once their
    /// spot is seen empty, or after `Rules.memory` seconds.
    std::vector<Seen> units;
    /// Ever seen, one bit per cell (`Vision.index`).
    std::vector<uint64_t> explored;
    /// When each base site's centre was last seen (-inf: never).
    std::vector<double> sites;

    /// Swift's `explored(_ i:)` (C++ cannot name a method like a member).
    bool isExplored(int64_t i) const;

    bool operator==(const Intel&) const = default;
};

/// The whole mutable state of one session.
struct AUTOCRAFTCORE_API GameState {
    std::vector<Player> players;
    double time = 0;
    std::vector<OreDeposit> patches;
    /// Wells, one per map well in map order (nil in old sessions).
    std::optional<std::vector<Well>> wells;
    std::vector<Structure> structures;
    std::vector<Unit> units;
    int64_t nextID = 0;
    /// Set when one player is left standing; the game restarts
    /// `Rules.victoryLap` seconds after `endedAt`.
    std::optional<int64_t> winner;
    std::optional<double> endedAt;
    /// Games won per player on this screen arrangement, kept across restarts.
    std::vector<int64_t> score;
    /// What each player knows of the others under the fog of war (nil
    /// until the simulation first looks; see `Vision`).
    std::optional<std::vector<Intel>> intel;
    /// Doodads put down by hand in the playground, past the map's own
    /// (nil: none; `Simulation.placeDoodad`).
    std::optional<std::vector<Doodad>> doodads;

    /// Player `p`'s team: `Player.team`, else its own index (also for an
    /// index that is no player), so with no teams set `hostile(a, b)` is
    /// `a != b`, exactly the Swift game's test.
    int64_t team(int64_t p) const {
        if (p >= 0 && p < static_cast<int64_t>(players.size())) {
            if (const auto& t = players[static_cast<size_t>(p)].team) return *t;
        }
        return p;
    }
    /// Players `a` and `b` are on one side: the same player, or allies.
    bool allied(int64_t a, int64_t b) const { return team(a) == team(b); }
    /// Players `a` and `b` fight each other.
    bool hostile(int64_t a, int64_t b) const { return team(a) != team(b); }
    /// Every player's team, when any player has one (nil: none has, every
    /// player for itself). What `new_` takes to set the next game up alike.
    std::optional<std::vector<int64_t>> teams() const;

    // Player 0 (blue), whose resource bar the HUD shows.
    int64_t& ore() { return players[0].ore; }
    int64_t ore() const { return players[0].ore; }
    int64_t& hydrogen() { return players[0].hydrogen; }
    int64_t hydrogen() const { return players[0].hydrogen; }
    int64_t supplyUsed() const { return players[0].supplyUsed; }
    int64_t supplyCap() const { return players[0].supplyCap; }
    int64_t totalMined() const { return players[0].totalMined; }

    /// A fresh session on a map: a Citadel and one Prospector on every start
    /// base, one player each, sides drawn at random. `score` carries the
    /// tally over from the last game, and the draw and the AIs' temperaments
    /// are seeded from it.
    /// `round` varies the game when the score has not moved (a drawn game).
    /// (Swift's `new`, a C++ keyword.)
    ///
    /// `teams` (C++ only, PORTING.md "Teams"): one entry per player, its
    /// team; as many players as entries (at most the map's starts), each
    /// team on a run of neighbouring starts, the teams spread evenly round
    /// the map, all drawn from the seed. Nil: one player per start, each
    /// for itself, exactly as the Swift game sets it up.
    static GameState new_(const MapDefinition& map, const std::optional<std::vector<int64_t>>& score = std::nullopt,
                          int64_t round = 0, const std::optional<std::vector<int64_t>>& teams = std::nullopt);

    std::optional<Structure> structure(int64_t id) const;
    std::optional<Unit> unit(int64_t id) const;

    /// Where a game on a map mirrored across `map.mirrorX` stops being its own
    /// mirror image (nil while it still is): the first thing one side has that
    /// the other has no mirrored twin of.
    std::optional<std::string> mirrorBreak(const MapDefinition& map) const;

    /// How player `p` stands: its army's list price (every unit but Prospectors),
    /// its Prospectors, bank, supply, finished buildings and ore mined.
    /// (Leveling.swift; defined in Leveling.cpp.)
    Standing standing(int64_t p) const;

    bool operator==(const GameState&) const = default;
};

// MARK: - Events and commands (State.swift)

/// Something the renderer shows or plays once.
struct GameEvent {
    struct Deposited { int64_t unit; int64_t amount; Vec2 at; bool operator==(const Deposited&) const = default; };
    struct PatchDepleted { int64_t patch; bool operator==(const PatchDepleted&) const = default; };
    struct PatchRegrown { int64_t patch; bool operator==(const PatchRegrown&) const = default; };
    struct Trained { int64_t unit; UnitKind kind; Vec2 at; bool operator==(const Trained&) const = default; };
    struct ConstructionStarted { int64_t structure; StructureKind kind; Vec2 at; bool operator==(const ConstructionStarted&) const = default; };
    struct Constructed { int64_t structure; StructureKind kind; Vec2 at; bool operator==(const Constructed&) const = default; };
    /// A Ranger fired at a unit or building of `victim` (a player).
    struct Shot { int64_t unit; int64_t target; int64_t victim; Vec2 at; bool operator==(const Shot&) const = default; };
    /// The driven unit fired with nothing under its sight; `at` is where
    /// the shot is spent, at its weapon's range.
    struct Missed { int64_t unit; Vec2 at; bool operator==(const Missed&) const = default; };
    /// A shot of `unit` reached `target` (at once, or when its grenade or
    /// shell came down; see `Rules.flight`): its damage is done.
    struct Landed { int64_t unit; int64_t target; bool operator==(const Landed&) const = default; };
    struct Died { int64_t unit; UnitKind kind; int64_t owner; Vec2 at; bool operator==(const Died&) const = default; };
    struct Destroyed { int64_t structure; StructureKind kind; int64_t owner; Vec2 at; bool operator==(const Destroyed&) const = default; };
    /// The last building of every other player is gone.
    struct Victory { int64_t winner; bool operator==(const Victory&) const = default; };
    /// A player finished researching an upgrade.
    struct Researched { Upgrade upgrade; int64_t owner; Vec2 at; bool operator==(const Researched&) const = default; };
    /// A unit on its way somewhere made no headway for a while and was
    /// set free (see `Simulation.unstick`), for the log.
    struct Stuck { int64_t unit; UnitKind kind; int64_t owner; Vec2 at; bool operator==(const Stuck&) const = default; };
    /// A ground unit stood boxed in (by buildings, say) for
    /// `Simulation.trappedTime` and was destroyed (see `Simulation.trapped`);
    /// it also dies like any other, for the log.
    struct Trapped { int64_t unit; UnitKind kind; int64_t owner; Vec2 at; bool operator==(const Trapped&) const = default; };
    /// The player's driving took a kind to `level` (2…10): two picks are
    /// on offer for it now (`PilotRecord.pending`).
    struct LeveledUp { UnitKind kind; int64_t level; bool operator==(const LeveledUp&) const = default; };
    /// The driven unit's grenade (Pulse mine) went off at `at`, hurting
    /// enemies within `radius`.
    struct Blast { int64_t unit; Vec2 at; double radius; bool operator==(const Blast&) const = default; };

    /// An Atlas's Quake stomp shook the ground at `at`, hurting and slowing
    /// the enemies on it within `radius` (the unit's reach, with Aftershock).
    struct Stomp { int64_t unit; Vec2 at; double radius; bool operator==(const Stomp&) const = default; };

    AC_CASES(GameEvent, Deposited, PatchDepleted, PatchRegrown, Trained, ConstructionStarted, Constructed, Shot, Missed,
             Landed, Died, Destroyed, Victory, Researched, Stuck, Trapped, LeveledUp, Blast, Stomp)
    bool operator==(const GameEvent&) const = default;
};

/// An order from the player (the AI commander).
struct Command {
    /// Queue the building's unit: a Prospector at a Citadel, a Ranger at
    /// a Garrison.
    struct Train { int64_t structure; std::optional<UnitKind> kind; bool operator==(const Train&) const = default; };
    /// Send a Prospector to build at a point (a Derrick: on a well). The cost
    /// is paid when ordered.
    struct Build { int64_t worker; StructureKind kind; Vec2 at; bool operator==(const Build&) const = default; };
    /// A Garrison, Foundry or Spacedock builds its Lab.
    struct Addon { int64_t structure; bool operator==(const Addon&) const = default; };
    /// Send a Prospector to pump MH from a Derrick.
    struct Harvest { int64_t worker; int64_t derrick; bool operator==(const Harvest&) const = default; };
    /// Anchor a tank up (true) or pack it up.
    struct Anchor { int64_t unit; bool on; bool operator==(const Anchor&) const = default; };
    /// Give a unit its own errand, or (nil) send it back to the army.
    struct Mission { int64_t unit; std::optional<ac::Mission> mission; bool operator==(const Mission&) const = default; };
    /// A Dropship takes a unit aboard, or drops everyone (unit nil).
    struct Board { int64_t dropship; std::optional<int64_t> unit; bool operator==(const Board&) const = default; };
    /// Send a Prospector to mine a patch (by index).
    struct Gather { int64_t worker; int64_t patch; bool operator==(const Gather&) const = default; };
    /// Send a Prospector to finish a building whose builder was killed.
    struct Resume { int64_t worker; int64_t structure; bool operator==(const Resume&) const = default; };
    /// A Lab researches an upgrade (paid when ordered).
    struct Research { int64_t structure; ac::Upgrade upgrade; bool operator==(const Research&) const = default; };
    /// Send a Ranger into a Bastion.
    struct Load { int64_t unit; int64_t bastion; bool operator==(const Load&) const = default; };
    /// Send a Prospector to repair a building.
    struct Repair { int64_t worker; int64_t structure; bool operator==(const Repair&) const = default; };
    /// A player's army: attack-move to a point, or (nil) gather at the rally.
    struct Attack { int64_t player; std::optional<Vec2> at; bool operator==(const Attack&) const = default; };
    /// A player's army: fight enemies at a point in its own base, and go
    /// no further.
    struct Defend { int64_t player; Vec2 at; int64_t group; bool operator==(const Defend&) const = default; };
    /// A player's army walks back to its rally without stopping to fight.
    struct Retreat { int64_t player; bool operator==(const Retreat&) const = default; };
    /// A human on `player`'s team asks its AI to spend on this next (see
    /// `Directives`). The same thing asked again at the same building adds
    /// to the last request instead of queuing another.
    struct Request {
        int64_t player;
        ac::Request::What what;
        int64_t count = 1;
        bool repeats = false;
        std::optional<int64_t> at;
        std::optional<int64_t> site;
        bool operator==(const Request&) const = default;
    };
    /// Take a request off the team's queue.
    struct CancelRequest { int64_t player; int64_t id; bool operator==(const CancelRequest&) const = default; };
    /// What the team's AI leaves in the bank when it spends for itself.
    struct Keep { int64_t player; int64_t ore; int64_t hydrogen; bool operator==(const Keep&) const = default; };
    /// How the team's Prospectors split between ore and MH.
    struct Split { int64_t player; ac::Harvest harvest; bool operator==(const Split&) const = default; };
    /// A human on `player`'s team gives its AI an objective at a point (see
    /// `Objective`).
    struct Objective { int64_t player; ac::Objective::Kind kind; Vec2 at; bool operator==(const Objective&) const = default; };
    /// Take an objective off; its squad rejoins the army.
    struct CancelObjective { int64_t player; int64_t id; bool operator==(const CancelObjective&) const = default; };
    /// The team's AI turns an objective into another kind (an attack whose
    /// point is taken becomes a hold); its squad stays.
    struct Retask { int64_t player; int64_t id; ac::Objective::Kind kind; bool operator==(const Retask&) const = default; };
    /// How the team's army plays when no objective takes it (`Stance`).
    struct Stance { int64_t player; ac::Stance stance; bool operator==(const Stance&) const = default; };
    /// The human keeps a leveling pick: one of the two on offer at its
    /// kind's lowest pending level (`PilotRecord.pending`).
    struct Pick { int64_t player; Perk perk; bool operator==(const Pick&) const = default; };
    /// The AI's order for a request: when the order goes through, the
    /// request counts one done (and leaves the queue when used up).
    struct Serve { int64_t player; int64_t request; Indirect<ac::Command> command; bool operator==(const Serve&) const = default; };

    AC_CASES(Command, Train, Build, Addon, Harvest, Anchor, Mission, Board, Gather, Resume, Research, Load, Repair,
             Attack, Defend, Retreat, Request, CancelRequest, Keep, Split, Objective, CancelObjective, Retask, Stance,
             Pick, Serve)
    bool operator==(const Command&) const = default;
};

// MARK: - Raw strings

template <> struct EnumInfo<UnitKind> {
    static constexpr std::array<std::string_view, 12> names{
        "prospector", "ranger", "comet", "firefly", "juggernaut", "dropship", "longbow", "kestrel", "hailstorm",
        "peregrine", "atlas", "scorpion"};
};
template <> struct EnumInfo<StructureKind> {
    static constexpr std::array<std::string_view, 9> names{
        "citadel", "habDome", "garrison", "bastion", "derrick", "foundry", "spacedock", "lab", "sentinel"};
};
template <> struct EnumInfo<Unit::Task> {
    static constexpr std::array<std::string_view, 18> names{
        "idle", "toPatch", "mining", "toBase", "depositing", "waiting", "toBuild", "building", "toRally",
        "attackMove", "attacking", "toBastion", "inBastion", "repairing", "toDerrick", "inDerrick", "aboard", "errand"};
};
template <> struct EnumInfo<Upgrade> {
    static constexpr std::array<std::string_view, 4> names{"minigun", "aegisShield", "novaIgniters", "lifelineReactor"};
};
template <> struct EnumInfo<Harvest> {
    static constexpr std::array<std::string_view, 3> names{"ore", "balanced", "hydrogen"};
};
template <> struct EnumInfo<Stance> {
    static constexpr std::array<std::string_view, 4> names{"auto", "aggressive", "hold", "allIn"};
};
template <> struct EnumInfo<Objective::Kind> {
    static constexpr std::array<std::string_view, 4> names{"attack", "defend", "hold", "man"};
};
template <> struct EnumInfo<Player::Style> {
    static constexpr std::array<std::string_view, 3> names{"bio", "mech", "harass"};
};
template <> struct EnumInfo<GroundPatch::Kind> {
    static constexpr std::array<std::string_view, 3> names{"grass", "gravel", "scorch"};
};
template <> struct EnumInfo<Doodad::Kind> {
    static constexpr std::array<std::string_view, 10> names{
        "rock", "boulder", "rockSpire", "crate", "crateStack", "plant", "bush", "debris", "deadTree", "tower"};
};
template <> struct EnumInfo<TargetClass> {
    static constexpr std::array<std::string_view, 4> names{"armored", "light", "bio", "building"};
};
template <> struct EnumInfo<Filter> {
    static constexpr std::array<std::string_view, 4> names{"machines", "bio", "all", "air"};
};
template <> struct EnumInfo<Action> {
    static constexpr std::array<std::string_view, 2> names{"oreTrip", "attack"};
};
template <> struct EnumInfo<PerkSlot> {
    static constexpr std::array<std::string_view, 2> names{"one", "other"};
};
template <> struct EnumInfo<Perk> {
    static constexpr std::array<std::string_view, 216> names{
        "prospectorQuickDrill", "prospectorLightFrame", "prospectorRigMaster", "prospectorPlating", "prospectorBigHaul", "prospectorFastWelder", "prospectorFieldWelder",
        "prospectorGroupRepair", "prospectorForeman", "prospectorSiteBoss", "prospectorRichVein", "prospectorCuttingTorch", "prospectorOvertime", "prospectorPrefab", "prospectorCliffHop",
        "prospectorSupplyChief", "prospectorUnionCrew", "prospectorShield",
        "rangerDrillSergeant", "rangerLightKit", "rangerCombatDrill", "rangerFlakVest", "rangerHollowPoints", "rangerLongBarrel",
        "rangerSurge", "rangerFieldMedic", "rangerSquadLeader", "rangerQuartermaster", "rangerBurstFire", "rangerArmorPiercing",
        "rangerEspritDeCorps", "rangerRecruitment", "rangerJumpPack", "rangerBastionDrill", "rangerBandOfBrothers", "rangerShield",
        "cometPackTactics", "cometFleetFoot", "cometNanomeds", "cometPaddedSuit", "cometTwinMagnums", "cometLongJump",
        "cometPulseMine", "cometSpotter", "cometRaidParty", "cometDemolition", "cometDoubleTap", "cometLightKiller",
        "cometHitAndRun", "cometCheapKit", "cometBooster", "cometBounty", "cometDeathSquad", "cometShield",
        "fireflyHotBurners", "fireflyTurbo", "fireflyFuelLine", "fireflyArmorPlates", "fireflyNapalm", "fireflyWideNozzle",
        "fireflyBurn", "fireflyMechanic", "fireflyPack", "fireflySpread", "fireflyFlashpoint", "fireflyHotCore", "fireflyOverdrive",
        "fireflyCheapChassis", "fireflyBoost", "fireflyScrap", "fireflyFirestorm", "fireflyShield",
        "juggernautShellDrill", "juggernautLongStride", "juggernautShockRounds", "juggernautHeavyPlate", "juggernautBreacher",
        "juggernautDeepSlow", "juggernautSurge", "juggernautBulwark", "juggernautSiegeBreakers", "juggernautBehemoth",
        "juggernautHeavyVolley", "juggernautFragment", "juggernautShockTroops", "juggernautFieldKit", "juggernautJumpPack",
        "juggernautFieldResearch", "juggernautWreckingCrew", "juggernautShield",
        "dropshipTriage", "dropshipBoosters", "dropshipLifeline", "dropshipArmorPlates", "dropshipSurgeon", "dropshipCargoBay",
        "dropshipNaniteBeam", "dropshipHealingField", "dropshipMedicalTeam", "dropshipEfficient", "dropshipDoubleBeam",
        "dropshipCombatDrop", "dropshipFieldHospital", "dropshipCheapHull", "dropshipCruise", "dropshipSpacedockRefit",
        "dropshipMedicalCorps", "dropshipShield",
        "longbowSpotter", "longbowTreads", "longbowQuickAnchor", "longbowReactiveArmor", "longbowShapedCharge", "longbowLongGun", "longbowCrewMechanic",
        "longbowMechanic", "longbowBattery", "longbowHeavyShells", "longbowDoubleShot", "longbowAPShells", "longbowDrilledCrews", "longbowCheapHull",
        "longbowHullDown", "longbowHeavyIndustry", "longbowArtilleryPark", "longbowShield",
        "kestrelWingman", "kestrelAfterburners", "kestrelKeenOptics", "kestrelArmorPlates", "kestrelHellfire", "kestrelLongRails",
        "kestrelFieldRepairs", "kestrelMechanic", "kestrelSquadron", "kestrelWorkerHunter", "kestrelDoubleSalvo", "kestrelAPRockets",
        "kestrelFlightDrill", "kestrelCheapAirframe", "kestrelAfterburn", "kestrelBountyHunter", "kestrelStrikeWing", "kestrelShield",
        "hailstormLoaders", "hailstormTracks", "hailstormWideBurst", "hailstormArmorPlates", "hailstormProximityFuse",
        "hailstormLongBarrels", "hailstormCrewMechanic", "hailstormMechanic", "hailstormFlakBattery", "hailstormSkyWatch",
        "hailstormDoubleBurst", "hailstormAPFlak", "hailstormDrilledGunners", "hailstormCheapChassis", "hailstormWalkerLegs",
        "hailstormHeavyIndustry", "hailstormFlakWall", "hailstormShield",
        "peregrineWingmen", "peregrineThrusters", "peregrineLongRangeRadar", "peregrineArmorPlates", "peregrineSeekerHeads",
        "peregrineLongRails", "peregrineFieldRepairs", "peregrineEscort", "peregrinePackHunters", "peregrineGunshipKiller",
        "peregrineDoubleVolley", "peregrineAPMissiles", "peregrineFlightSchool", "peregrineCheapAirframe", "peregrineAfterburn",
        "peregrineBountyHunter", "peregrineAirSuperiority", "peregrineShield",
        "atlasAutoloader", "atlasServoLegs", "atlasRangefinder", "atlasArmorPlates", "atlasHeavyShells", "atlasLongBarrels",
        "atlasCrewMechanic", "atlasMechanic", "atlasBulwark", "atlasWideShells", "atlasDoubleVolley", "atlasAPShells",
        "atlasAftershock", "atlasCheapFrame", "atlasHeavyIndustry", "atlasFlakMount", "atlasWarMarch", "atlasShield",
        "scorpionQuickDig", "scorpionLightFrame", "scorpionSeismicSense", "scorpionArmorPlates", "scorpionHeavyCharge",
        "scorpionLongTail", "scorpionFieldRepairs", "scorpionDeepBurrow", "scorpionQuickReload", "scorpionWorkerHunter",
        "scorpionShapedCharge", "scorpionTwinSting", "scorpionMinefieldDrill", "scorpionCheapShell", "scorpionSapper",
        "scorpionBountyHunter", "scorpionNest", "scorpionShield"};
};

} // namespace ac
