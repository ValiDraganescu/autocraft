// Port of the types of Sources/GameCore/Pilot.swift: `Pilot`, `PilotSight`,
// `PilotTarget`, `PilotAbility`, `PilotBuild`. Its `extension Simulation`
// is declared in `Simulation+Pilot.members.h` and defined in `Pilot.cpp`.
#pragma once

#include "SimdMath.h"
#include "Rules.h"
#include "Types.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace ac {

/// The player driving one unit by hand (window mode's first-person view).
/// The AI leaves that unit alone; the rest of the game plays on around it.
///
/// Built for any unit kind: walking, facing and the hand-back are the same
/// for all; what the action key does depends on the kind (`pilotTarget`,
/// `pilotAct`), as does anything the unit is busy with (`pilotBusy`).
/// `Pilot.drivable` lists the kinds that can be driven so far.
struct Pilot {
    /// What the player's crosshair is on (`crosshair`).
    struct Crosshair {
        struct Clear {
            bool operator==(const Clear&) const = default;
        };
        struct On {
            int64_t _0;
            bool operator==(const On&) const = default;
        };
        AC_CASES(Crosshair, Clear, On)
        bool operator==(const Crosshair&) const = default;
    };

    int64_t unit = 0;
    /// Wanted walk in the unit's own frame: x forward, y to its right, each
    /// −1…1.
    Vec2 walk{0, 0};
    /// Where it faces, world radians (0 = +X, π/2 = +Z), from the mouse.
    double heading = 0;
    /// The action key went down since the last step (enter a Derrick,
    /// pick up a paused building, place one).
    bool act = false;
    /// The action key is held (drill, strike, repair: they go on only while
    /// it is).
    bool hold = false;
    /// The ability key (R) went down since the last step: the kind's
    /// `pilotAbility`, if it has one.
    bool ability = false;
    /// The ability key (R) is held. A driven Prospector then only repairs
    /// (`pilotRepair`): a damaged Derrick, ore ahead or a full load no
    /// longer take the action key from it.
    bool mend = false;
    /// What the player's crosshair is on, as the view measures it each
    /// frame (the simulation has no camera): a unit's or a building's model,
    /// or nothing (the ground, a rock, the sky). A gun on foot (Ranger,
    /// Comet, Juggernaut) shoots exactly that, as in a shooter: no aim
    /// assist, and height counts. Nil: the sight runs along `look` across
    /// the ground with `pilotAimSlack` (a vehicle, or no view).
    std::optional<Crosshair> crosshair;

    Pilot() = default;
    Pilot(int64_t unit_, double heading_) : unit(unit_), heading(heading_) {}

    bool operator==(const Pilot&) const = default;

    /// Kinds the player can take over: every kind.
    static constexpr EnumSet<UnitKind> drivable{UnitKind::prospector, UnitKind::ranger, UnitKind::comet,
                                                 UnitKind::firefly, UnitKind::juggernaut, UnitKind::dropship,
                                                 UnitKind::longbow, UnitKind::kestrel, UnitKind::hailstorm,
                                                 UnitKind::peregrine, UnitKind::atlas, UnitKind::scorpion};
    /// The player whose units can be driven (the one the HUD shows).
    static constexpr int64_t player = 0;

    /// A vehicle, driven as in World of Tanks: W/S drive it, A/D turn its
    /// hull (it does not step sideways), and the mouse aims: the turret (a
    /// tank's, a Firefly's tail) traverses after the look (`turretRate`).
    static bool steers(UnitKind k) {
        return k == UnitKind::firefly || k == UnitKind::longbow || k == UnitKind::hailstorm || k == UnitKind::atlas;
    }
    /// It can step sideways (wheels and treads cannot).
    static bool strafes(UnitKind k) { return !steers(k); }
    /// How fast A/D turn a vehicle's hull, radians a second.
    static double hullTurnRate(UnitKind k) {
        return k == UnitKind::firefly ? 3.0 : k == UnitKind::atlas ? Rules::atlasBodyRate : 2.0;
    }
    /// An anchored Longbow walks on its legs at this share of its pace, driving
    /// and turning.
    static constexpr double anchoredPace = 0.15;
    /// The share of its pace a unit moves at: a tank's full in tank mode,
    /// `anchoredPace` anchored, none while it changes mode.
    static double pace(const Unit& u) {
        if (!u.anchor) return 1;
        const double s = *u.anchor;
        // A buried Scorpion cannot move at all.
        if (u.kind == UnitKind::scorpion) return s == 0 ? 1 : 0;
        return s == 0 ? 1 : s >= 1 ? anchoredPace : 0;
    }
    /// How fast a turret traverses toward where the player looks, radians
    /// a second; nil without a turret.
    static std::optional<double> turretRate(UnitKind k) {
        switch (k) {
        case UnitKind::longbow: return 2.4;
        // The scorpion's tail: lighter than a tank's turret.
        case UnitKind::firefly: return 3.5;
        // The flak mount: built to track fast flyers.
        case UnitKind::hailstorm: return 3.2;
        // The Atlas's torso: its cannons swing twice as fast as its legs turn.
        case UnitKind::atlas: return Rules::atlasTorsoRate;
        default: return std::nullopt;
        }
    }
    /// Walking into a cliff face, it jumps it.
    static bool jumps(UnitKind k) { return k == UnitKind::comet; }
};

/// The first enemy on a driven gun's line of fire (`pilotSight`).
/// For a healer, the friendly unit it would heal instead (`friend`).
struct PilotSight {
    int64_t target = 0;
    /// Edge to edge, and the gun's reach at it (high ground counted; a
    /// healer's `Rules.healRange`).
    double distance = 0;
    double range = 0;
    /// Closest it can shoot (an anchored Longbow's 2); 0 for most.
    double minRange = 0;
    /// Between `minRange` and `range`: a shot (or the beam) now would reach.
    bool inRange = false;
    /// A unit of its own side (a Dropship's patient), not an enemy.
    /// (`friend` is a C++ keyword.)
    bool friend_ = false;

    bool operator==(const PilotSight&) const = default;
};

/// What the action key would do for a driven unit right now.
struct PilotTarget {
    /// Strike or shoot this enemy unit or building (in reach, ahead; for a
    /// gun, under the sight). Every armed kind.
    struct Enemy {
        int64_t _0;
        bool operator==(const Enemy&) const = default;
    };
    /// Prospector: drill this patch (index into `patches`) while the key is held.
    /// A Prospector drilling it already moves along.
    struct Patch {
        int64_t _0;
        bool operator==(const Patch&) const = default;
    };
    /// Prospector: go into this Derrick for a load of MH; `busy` while another
    /// Prospector is inside.
    struct Derrick {
        int64_t _0;
        bool busy;
        bool operator==(const Derrick&) const = default;
    };
    /// Prospector: its hands are full; take the load to a Citadel first.
    struct Full {
        bool operator==(const Full&) const = default;
    };
    /// Prospector: pick up this unfinished building of its own that no Prospector is
    /// welding.
    struct Scaffold {
        int64_t _0;
        bool operator==(const Scaffold&) const = default;
    };
    /// Prospector: mend this damaged building of its own while the key is held
    /// (with the Field welder pick, a damaged machine of its side: ids are
    /// unique across units and buildings).
    struct Repair {
        int64_t _0;
        bool operator==(const Repair&) const = default;
    };
    /// Dropship: heal this hurt unit of its own side while the key is held,
    /// using energy.
    struct Heal {
        int64_t _0;
        bool operator==(const Heal&) const = default;
    };

    AC_CASES(PilotTarget, Enemy, Patch, Derrick, Full, Scaffold, Repair, Heal)
    bool operator==(const PilotTarget&) const = default;
};

/// What the ability key (R) does for a driven unit right now. Only kinds
/// with an ability have one (`Simulation.pilotAbility`).
struct PilotAbility {
    struct Action {
        /// Longbow: anchor up, or pack up (`Rules.anchorTime` either way;
        /// it cannot move until it is back in tank mode). A Scorpion buries
        /// and digs out the same way (`Rules.buryTime`).
        struct Anchor {
            bool operator==(const Anchor&) const = default;
        };
        struct Unanchor {
            bool operator==(const Unanchor&) const = default;
        };
        /// Dropship: take this unit aboard.
        struct Load {
            int64_t _0;
            bool operator==(const Load&) const = default;
        };
        /// Dropship: set everyone aboard down where it is.
        struct Unload {
            bool operator==(const Unload&) const = default;
        };
        /// A leveling pick's burst (Surge, Boost): faster for a few
        /// seconds (`Burst`). `title` is the pick's.
        struct Burst {
            bool operator==(const Burst&) const = default;
        };
        /// A leveling pick's grenade (Pulse mine) at the crosshair
        /// (`Grenade`). `title` is the pick's.
        struct Grenade {
            bool operator==(const Grenade&) const = default;
        };

        /// Atlas: the Quake stomp (`Rules.stompDamage` round it).
        struct Stomp {
            bool operator==(const Stomp&) const = default;
        };

        AC_CASES(Action, Anchor, Unanchor, Load, Unload, Burst, Grenade, Stomp)
        bool operator==(const Action&) const = default;
    };
    /// Nil: nothing to do now, and `why` says so.
    std::optional<Action> action;
    /// For the command card: "Anchor Mode", "Tank Mode", "Load", "Unload All".
    std::string title;
    std::optional<std::string> why;

    PilotAbility() = default;
    PilotAbility(std::optional<Action> action_, std::string title_, std::optional<std::string> why_ = std::nullopt)
        : action(std::move(action_)), title(std::move(title_)), why(std::move(why_)) {}

    bool operator==(const PilotAbility&) const = default;
};

/// Buildings a Prospector can put up by hand, in the build menu's order.
struct PilotBuild {
    static constexpr std::array<StructureKind, 8> kinds{
        StructureKind::habDome, StructureKind::garrison, StructureKind::derrick, StructureKind::bastion,
        StructureKind::citadel, StructureKind::foundry, StructureKind::spacedock, StructureKind::sentinel};
    /// The driven kinds that can put buildings up.
    static bool builds(UnitKind k) { return k == UnitKind::prospector; }
};

} // namespace ac
