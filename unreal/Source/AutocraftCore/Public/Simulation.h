// Port of Sources/GameCore/Simulation.swift: advances a GameState.
// Its extensions (`Simulation+Leveling.swift`, `Pilot.swift`,
// `Pilot+Kinds.swift`, `Vision.swift`, `Simulation+Playground.swift`) add
// their members through the fragments included at the end of the class
// body; their bodies are in the `.cpp` of the same names.
#pragma once

#include "Leveling.h"
#include "SimdMath.h"
#include "NavGrid.h"
#include "Pilot.h"
#include "Router.h"
#include "Rules.h"
#include "TerrainField.h"
#include "Types.h"
#include "Vision.h"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ac {

// `Commander.h` includes this header (a Commander reads a Simulation); the
// Simulation holds its commanders in a vector, which takes an incomplete
// type: the special members that need it whole are in `Simulation.cpp`.
struct Commander;
class PathAhead;

/// Advances a GameState. Pure: the same steps give the same state.
class Simulation {
public:
    GameState state;
    /// The AI players, one per player it plays; asked for orders once a
    /// second of game time. Empty: nobody gives orders (Prospectors keep mining).
    std::vector<Commander> commanders;
    /// Workers a patch takes before others look elsewhere.
    int64_t workersPerPatch = 2;
    std::optional<Router> router;
    std::optional<TerrainField> field;
    /// Walkable ground and paths around cliffs, doodads, resources and
    /// buildings (nil without a map: units walk straight).
    std::optional<NavGrid> nav;
    const NavGrid* navGrid() const { return nav ? &*nav : nullptr; }
    /// The fog of war's grid (nil without a map: everyone sees everything),
    /// what each player sees now, and when it is next worked out.
    std::optional<Vision> vision;
    std::vector<Sight> sights;
    double nextLook = 0.0;
    std::optional<MapDefinition> map;
    /// Base site index of each patch (all 0 without a map; a patch put
    /// down in the playground gets its own, `placePatch`).
    std::vector<int64_t> patchBase;
    /// Base site centres (the start Citadel alone without a map).
    std::vector<Vec2> sites;
    double nextThink = 0.0;
    /// Where each player's army stands: places in a block, front row first,
    /// and the way it faces. Worked out once a second.
    std::vector<std::vector<Vec2>> rallySlots;
    std::vector<Vec2> rallyFacing;
    double nextRally = 0.0;
    /// The buildings each rally was placed for; it moves only when they change.
    std::vector<std::vector<int64_t>> rallyFor;
    /// Each army unit's place at the rally (an index into `rallySlots`),
    /// handed out once a second (see `assignPlaces`).
    std::vector<std::map<int64_t, int64_t>> rallyPlaces;
    /// Index into `state.units` by unit id, rebuilt every step. (Swift's
    /// `unitIndex`; renamed here, C++ can't share the name with the
    /// method `unitIndex(_:)`.)
    std::unordered_map<int64_t, int64_t> unitIndexByID;
    /// Rangers of each player standing at their rally, counted every step.
    std::vector<int64_t> atHome;
    /// Reinforcements cross the map to a fight only in groups this big.
    static constexpr int64_t reinforceGroup = 5;
    /// When each Longbow last had something to shoot anchored.
    std::map<int64_t, double> engaged;

    /// What a target is, for range, damage and priority: where it stands,
    /// its footprint radius, whether it is a building, whose it is, and its
    /// armour and attributes.
    struct Target {
        Vec2 position; double radius = 0; bool structure = false; int64_t owner = 0; std::optional<Unit::Kind> kind;
        /// A Bastion with Rangers in it shoots back like a unit.
        bool armed = false;
        bool air = false;
        bool armored = true;
        bool light = false;
        double armor = 1.0;
    };

    /// An attack on its way: it lands at `at`.
    struct InFlight { double at; Unit shooter; int64_t target; Target aim; };
    /// Attacks on their way (grenades, shells, flame): they land at `at`.
    std::vector<InFlight> inFlight;
    /// An attack that landed: who fired and at what.
    struct Landing { int64_t unit; int64_t target; };
    /// Attacks that landed since the step began, for `.landed` events.
    std::vector<Landing> landings;
    /// The Prospector the player drives by hand (see `take`); nil: the AI has them all.
    std::optional<Pilot> pilot;
    /// Level-ups earned since the step began, for `.leveledUp` events.
    std::vector<GameEvent> levelUps;
    /// Some unit may hold hp from a leveling pick (see `syncBonusHP`);
    /// true at first, for a session saved mid-drive.
    bool hpBonusHeld = true;
    /// Some unit is on fire (Burn): it burns out even once no one drives.
    bool burnsLeft = false;
    /// Effects tried on a pick in place of its own (`effects(of:)`): a
    /// test can try a building block on a pick not built yet.
    std::map<Perk, std::vector<Effect>> trialEffects;
    /// Where a ground unit last made headway, and when.
    struct Headway { Vec2 at; double time; };
    /// Where each ground unit on its way somewhere last made headway, and
    /// when (see `unstick`).
    std::map<int64_t, Headway> headway;
    /// Units set free by `unstick`, and where: one log line per episode,
    /// until the unit has moved well away from there.
    std::map<int64_t, Vec2> freed;
    /// A unit on its way somewhere that stays within `stuckRadius` of one
    /// spot for `stuckTime` seconds is stuck.
    static constexpr double stuckTime = 2.5;
    static constexpr double stuckRadius = 0.4;
    /// When each boxed-in ground unit was first seen boxed in (see `trapped`).
    std::map<int64_t, double> boxedSince;
    double nextTrapCheck = 0.0;
    /// A ground unit boxed in this long is destroyed.
    static constexpr double trappedTime = 5.0;
    /// Ground cut off from the open map and smaller than this (square
    /// cells) is a pocket a unit cannot leave; larger is a walled-off area.
    static constexpr double pocketArea = 100.0;

    /// `fog: false`: no fog of war, everyone sees everything (the
    /// playground can switch it off).
    explicit Simulation(const GameState& state_, const std::optional<MapDefinition>& map_ = std::nullopt, bool fog = true);
    /// With AI players (Swift's `commanders:`; an overload, as a default
    /// `{}` would need `Commander` whole here).
    Simulation(const GameState& state_, const std::optional<MapDefinition>& map_,
               const std::vector<Commander>& commanders_, bool fog = true);
    Simulation(const Simulation&);
    Simulation(Simulation&&) noexcept;
    Simulation& operator=(const Simulation&);
    Simulation& operator=(Simulation&&) noexcept;
    ~Simulation();

    /// How often the fog is worked out, in game seconds.
    static constexpr double lookEvery = 0.1;

    /// Rebuild the index of units by id (after `state.units` changed).
    void reindex();
    /// Start the next game on the same map after a victory, keeping the tally.
    void newGame();
    /// Follow buildings going up and ore deposits running dry or growing
    /// back: units inside something step out, and walking units find a new
    /// way.
    void refreshNav();
    /// Step by `dt` real seconds; returns what happened for the renderer.
    std::vector<GameEvent> step(double dt);

    /// C++ only (PathAhead.h): while `PathAhead::step` runs this step (`pathAhead`), its
    /// searches come from there; `probing`: this is its probe, a copy that
    /// only finds out which searches the step makes. Null/false otherwise.
    PathAhead* pathAhead = nullptr;
    bool probing = false;

    /// C++ only, fast paths (PORTING.md "Fast paths"): results differ from
    /// the Swift game's, which is why it is an option (off in the golden and
    /// parity tests, on in the game). On: (1) a grid change (`refreshNav`)
    /// re-paths only the walkers whose route crosses a cell it newly
    /// blocked; (2) at most `pathBudget` units start a search in a step, the
    /// rest keep walking their old path or wait, and ask again next step;
    /// (3) searches use an octile heuristic and a 4-ary heap. Set it with
    /// `setFastPaths` (it also tells the grid).
    bool fastPaths = false;
    int64_t pathBudget = 32;
    /// Searches started this step.
    int64_t pathsStarted = 0;
    void setFastPaths(bool on, int64_t budget = 32);
    /// C++ only: a flier keeps its goal while it flies, so under an army
    /// attack-move it flies every step (the Swift game's fliers move one step
    /// in eight, between two thinks; PORTING.md). False only in the skirmish
    /// golden test, which holds a Kestrel in an attack-move.
    bool flierKeepsGoal = true;
    /// C++ only: a buried Scorpion is under the ground, so other ground units
    /// walk over it (`separate` leaves it out, as RTS games do burrowed units).
    /// One buried in the lane between an ore field and its Citadel held a
    /// Scorpion behind it for good (the user's report, 2026-10-06). Off:
    /// the Swift game's pushes (StuckUnitsTests checks both; the goldens
    /// pass either way).
    bool buriedPassable = true;
    /// Apply an order now. False when it is not possible (cost, supply,
    /// unknown unit); nothing changes then.
    bool issue(const Command& command);
    bool issue(const Command& command, std::vector<GameEvent>& events);

    // MARK: - Economy

    /// Supply used counts units in training and queued.
    void refreshSupply();
    void stepStructures(double dt, std::vector<GameEvent>& events);
    /// A finished building with a weapon of its own (a Sentinel).
    void stepTurret(Structure& s, const UnitStats& w, double dt, std::vector<GameEvent>& events);
    /// How fast a Sentinel's launcher swings round, radians a second.
    static constexpr double turretTurnRate = 6.0;
    /// A new Prospector at the Citadel's edge facing its ore line.
    Unit spawnProspector(const Structure& citadel);
    /// Units a player has now: varies spawn spots the same way for both
    /// players (ids interleave, so they would not).
    int64_t made(int64_t owner) const;
    /// -1 on the east half of a mirrored map, where a sideways offset must
    /// go the other way to mirror the west's; else 1.
    double handed(Vec2 p) const;
    /// A new soldier at its building's door (the +Z side; a ship lifts off
    /// the Spacedock's deck), off to the rally.
    Unit spawnSoldier(Unit::Kind kind, const Structure& b);
    /// A unit as it comes out of its building: full health (upgrades
    /// counted), a Dropship with 50 energy and an empty hold, a tank in tank
    /// mode with its turret along its hull.
    Unit freshUnit(Unit::Kind kind, int64_t id, int64_t owner, Vec2 position, double heading,
                   Unit::Task task = Unit::Task::idle) const;
    /// Index into `state.units` of the unit with this id.
    std::optional<int64_t> unitIndex(int64_t id) const;
    /// The well at a point (within half a cell).
    std::optional<Well> well(Vec2 p) const;

    // MARK: - Army

    /// A player's army gathers in a block in front of its base nearest the
    /// map's centre (see the Swift for the whole story).
    void placeRally(int64_t player);
    Vec2 centre() const;
    /// In the army: a soldier with no errand of its own, out in the field.
    /// A raider picks workers first (Comets, Fireflies, a drop, hunters);
    /// a squad picks fights like the army.
    static bool raids(const Unit& u);
    /// A squad soldier's place around its point: spread on a golden-angle
    /// spiral by id, so they do not all crowd one spot.
    Vec2 spot(const Unit& u, Vec2 p) const;
    /// Where the player's army gathers: the front of its rally (nil: it
    /// has none yet).
    std::optional<Vec2> rallyPoint(int64_t p) const;
    bool inArmy(const Unit& u) const;
    /// A soldier's place in its army: soldiers take places in id order.
    int64_t rank(const Unit& u) const;
    /// The rally place of a soldier (one that joined the army since the
    /// last hand-out takes the place its rank gives it).
    std::optional<Vec2> slot(const Unit& u) const;
    /// An anchored Longbow at the rally stays planted until its place is this far
    /// off.
    static constexpr double plantedSlack = 2.0;
    /// Hand out the rally places, once a second.
    void assignPlaces(int64_t p);
    /// Where a soldier stands around its army's attack point: a sunflower
    /// spread, so the army arrives as a blob and not a single file.
    Vec2 attackSpot(const Unit& u, Vec2 a) const;

    std::optional<Target> target(int64_t id) const;
    /// Edge-to-edge distance from a unit to a target.
    double gap(const Unit& u, const Target& t) const;
    /// A Ranger's range from `from` at `to`: longer by
    /// `highGroundRangeBonus` if and only if `from` is on a higher level.
    double range(Vec2 from, Vec2 to) const;
    /// `base` range, plus the high-ground bonus if and only if `from` is on
    /// a higher level than `to`.
    double range(double base, Vec2 from, Vec2 to) const;
    /// A unit's weapon now; `air`: the one it uses against a flier (a Kestrel's
    /// rail gun; every other unit has one weapon).
    UnitStats weapon(const Unit& u, bool air = false) const;
    /// Seconds until the weapon it uses against `air` targets is ready.
    double cooldownFor(const Unit& u, bool air) const;
    /// `u` just fired weapon `w`: it is ready again after the cooldown.
    void reload(Unit& u, const UnitStats& w);
    /// A player has finished this upgrade.
    bool has(int64_t player, Upgrade up) const;
    /// A unit's full hit points, its side's upgrades and leveling picks
    /// counted.
    double maxHP(const Unit& u) const;
    /// It can shoot this kind of target at all (air or ground).
    bool canHit(const Unit& u, const Target& t) const;
    /// Target priority: units that shoot back first, then workers,
    /// then buildings. A raider hunts workers first.
    int64_t priority(const Target& t, bool raider = false) const;
    /// The best enemy in sight that it can hit.
    std::optional<int64_t> acquire(const Unit& u, bool inRange = false) const;
    /// Hit points left on a unit or building (0 when gone).
    double hpLeft(int64_t id) const;
    /// Where a soldier fighting `t` steps back to instead of standing
    /// still, or nil to stand and fight.
    std::optional<Vec2> backOff(const Unit& u, const Target& t, const UnitStats& w, double d, double reach) const;
    /// `t` is an armed unit that `u` outranges by a cell and is not
    /// slower than, and that can shoot `u` at all: worth kiting.
    bool outranges(const Unit& u, const Target& t, const UnitStats& w) const;
    /// A bio soldier under a third of its hit points, shot within the
    /// last `woundedQuiet` seconds, out with at least three healthy friends close by.
    bool wounded(const Unit& u) const;
    /// Seconds of cooldown left above which a kiting soldier steps back.
    static constexpr double kiteWindow = 0.3;
    /// Share of its hit points below which a bio soldier being shot
    /// steps out of the line.
    static constexpr double woundedShare = 0.33;
    /// Seconds after its last hit that a wounded soldier keeps stepping back.
    static constexpr double woundedQuiet = 1.5;
    void stepSoldier(Unit& u, double dt, std::vector<GameEvent>& events);
    /// Turn to the target and shoot when the weapon is ready; true when it
    /// fired.
    bool fire(Unit& u, int64_t id, const Target& t, double dt);
    /// One attack of `u` on `v`.
    double damage(const Unit& u, const Target& v) const;
    /// `u` attacks `id` (seen as `t`): it lands now, or once its round has
    /// flown (`Rules.flight`).
    void strike(const Unit& u, int64_t id, const Target& t);
    /// The shooter `u` was unseen by the victim's side `victim` when it fired.
    bool shotUnseen(const Unit& u, int64_t victim) const;
    /// An attack of `u` lands.
    void land(const Unit& u, int64_t id, const Target& aim);
    /// An anchored shell's or a flak burst's splash round `at`.
    /// Returns the units it hit.
    int64_t splash(const Unit& u, Vec2 at, int64_t skip, bool twice);
    /// What goes on by itself, whoever drives the unit.
    void upkeep(Unit& u, double dt);
    /// A Longbow anchors up or packs up.
    void stepAnchor(Unit& u);
    /// A Scorpion buries or digs out.
    void stepBury(Unit& u);
    /// A follow order (`Mission::Follow`): whether `u` may follow unit `id`,
    /// its place in the formation around it, the flyer it would engage for
    /// it, and one step of it. `stepFollow` is false once the order is over.
    bool canFollow(const Unit& u, int64_t id) const;
    Vec2 formationSpot(const Unit& u, const Unit& leader) const;
    std::optional<int64_t> escortFoe(const Unit& u, const Unit& leader) const;
    bool stepFollow(Unit& u, double dt, bool think, std::vector<GameEvent>& events);
    /// An idle Peregrine (no order of its own) takes a follow order on the
    /// closest friendly flyer by itself, and keeps it (see `Simulation.cpp`).
    void autoEscort(Unit& u);
    /// A buried Scorpion has held its lock on `id` for a second (see `Rules.scorpionLock`).
    bool lockOn(Unit& u, int64_t id, double dt) const;
    /// The shares of the Flak mount and Sapper picks, for the driven unit.
    std::optional<double> flakShare(const Unit& u) const;
    std::optional<double> sapperShare(const Unit& u) const;
    /// An Atlas's Quake stomp, and the AI's trigger for it.
    void quake(Unit& u, std::vector<GameEvent>& events);
    void stompCrowd(Unit& u, std::vector<GameEvent>& events);
    /// What a tank at `u` sees.
    struct AnchorView { bool hittable; bool crowded; };
    AnchorView anchorView(const Unit& u) const;
    /// A Dropship heals the most hurt living thing of its side nearby
    /// (infantry only), flies with its army, and carries out drops.
    void stepDropship(Unit& u, double dt, bool think);
    /// Units aboard a Dropship ride with it.
    void carry(const Unit& m);
    /// Set everyone aboard down around the Dropship; they raid where it is.
    void unload(Unit& m);
    /// A unit within 6 cells boards a Dropship with room for it; with no
    /// unit, the Dropship sets everyone down where it is.
    bool board(int64_t mid, std::optional<int64_t> id);
    /// Whether a soldier goes to its army's attack point.
    bool joins(const Unit& u, Vec2 a) const;
    /// A Ranger in a Bastion shoots from it.
    void stepInBastion(Unit& u, double dt, bool think, std::vector<GameEvent>& events);
    /// A shot lands: damage the unit or building with this id. (`by`:
    /// Swift's `by shooter: Unit?`.)
    void hit(int64_t id, double damage, bool slow = false, const Unit* by = nullptr);
    /// Ground soldiers never stand inside each other.
    void separate();
    /// The watchdog for units that stop on their way.
    void unstick(std::vector<GameEvent>& events);
    /// Once a second: a ground unit boxed in for `trappedTime` is destroyed.
    void trapped(std::vector<GameEvent>& events);
    /// Remove the dead and the destroyed, and end the game when one player
    /// is left with buildings.
    void bury(std::vector<GameEvent>& events);
    /// Dry patches grow back `Rules.regrowDelay` after running out.
    void regrow(std::vector<GameEvent>& events);
    /// Clear a patch's miner (or a well's harvester) if that Prospector is no
    /// longer drilling it (inside it).
    void releaseStaleMiners();
    void leavePatch(Unit& u);

    // MARK: - Bases and patches

    /// A player's completed Citadels.
    std::vector<Structure> bases(int64_t player) const;
    std::optional<Structure> base(Vec2 p, int64_t player) const;
    double walk(Vec2 a, Vec2 b) const;
    /// Site index a Citadel stands on.
    int64_t site(const Structure& citadel) const;
    /// Workers assigned to each patch.
    std::map<int64_t, int64_t> assigned() const;
    /// The patch a Prospector should mine (C++ only: one it can walk to).
    std::optional<int64_t> choosePatch(const Unit& unit) const;
    /// C++ only: a ground unit at `from` can walk to within `reach` of `to`
    /// (the nav grid's regions; true without a grid). A base walled in by
    /// its own buildings left Prospectors "mining" patches of other bases
    /// they could not reach, from where they stood (the user's report,
    /// 2026-10-06): their walk ended at once and counted as arrival.
    bool reaches(Vec2 from, Vec2 to, double reach) const;
    /// How close a Prospector must get to its patch for the walk to count as
    /// arrival (C++ only); farther, the patch was out of reach.
    static constexpr double patchArrival = Rules::mineRange + 1.0;
    /// The reach `reaches` asks of a patch (its cells are blocked).
    static constexpr double patchReach = Rules::mineRange + 0.6;
    double cost(int64_t p, const Unit& unit, const std::map<int64_t, int64_t>& n) const;

    // MARK: - Units

    void stepUnit(Unit& u, double dt, std::vector<GameEvent>& events);
    /// A Prospector that was just hit fights back. True while it fights.
    bool prospectorDefends(Unit& u, double dt, std::vector<GameEvent>& events);
    /// A Prospector's errand (`Command.mission`).
    bool sendOnErrand(int64_t i, const std::optional<Mission>& m);
    /// A Prospector on an errand.
    void stepErrand(Unit& u, double dt, std::vector<GameEvent>& events);
    /// The nearest enemy on the ground within a Prospector's melee reach (units
    /// first, then buildings).
    std::optional<std::pair<int64_t, Target>> meleeFoe(const Unit& u) const;
    /// At the patch: drill it if free, else take a free neighbour of the same
    /// base (the worker "bounce"), else wait for it.
    void arrive(Unit& u, int64_t pi);
    /// Walk toward `target` along ramps when it is on another level; true on
    /// arrival within `stopAt`.
    bool travel(Unit& u, Vec2 target, double stopAt, double dt);
    /// `nav->path`, or through `pathAhead` while it runs the step.
    std::optional<std::vector<Vec2>> findPath(Vec2 from, Vec2 target, double stopAt, bool jumps = false);
    /// A ground step from open ground into a blocked cell or a rock.
    bool offCourse(const Unit& u, Vec2 from) const;
    /// A Comet stepping onto a cliff face jumps it.
    void leap(Unit& u, Vec2 from, Vec2 next) const;
    /// The first walkable spot from `p` toward `next`, in 0.2-cell steps.
    std::optional<Vec2> landing(Vec2 p, Vec2 next) const;
    /// Turn toward `target`; true when facing it within a small angle.
    bool turn(Unit& u, Vec2 target, double dt, double rate = Rules::prospectorTurnRate) const;
    /// Walk toward `target` until within `stopAt`; true on arrival.
    bool move(Unit& u, Vec2 target, double stopAt, double dt) const;

    // The extensions' members.
#include "Simulation+Leveling.members.h"
#include "Simulation+Pilot.members.h"
#include "Simulation+Kinds.members.h"
#include "Simulation+Vision.members.h"
#include "Simulation+Playground.members.h"
};

} // namespace ac
