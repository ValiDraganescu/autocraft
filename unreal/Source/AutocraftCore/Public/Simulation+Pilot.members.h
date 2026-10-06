// Members of `class Simulation` from Sources/GameCore/Pilot.swift (its
// `extension Simulation`), included inside the class body by Simulation.h.
// Declarations only; the bodies are in Private/Pilot.cpp.
// (No include guard: it is a fragment of a class body.)

public:
    /// Reach, from an ore deposit's edge, at which a driven Prospector drills it.
    static constexpr double pilotReach = 0.9;
    /// Reach, from a Derrick's edge, at which it goes in.
    static constexpr double pilotHydrogenReach = 1.1;
    /// Reach, from a Citadel's edge, at which a load is dropped off.
    static constexpr double pilotDepositReach = 0.9;

    // MARK: - Taking over

    /// Take the unit `unit` under manual control; nil hands it back to the
    /// AI. False when it is not the player's, or not a kind that can be
    /// driven yet.
    bool take(std::optional<int64_t> unit);

    /// A unit that is somewhere it cannot be driven from: inside a Bastion or
    /// a Dropship, or mid-jump.
    bool canDrive(const Unit& u) const;

    /// True for the unit the player drives.
    bool piloted(int64_t id) const;

    /// The unit the player drives, if it is alive.
    std::optional<Unit> pilotUnit() const;

    // MARK: - Every kind

    /// One step of the driven unit: its upkeep and the ability key, then
    /// busy work (it stands still for it), else it faces where the mouse
    /// says and walks where the keys say; then the action key. Anyone
    /// aboard rides along.
    void stepPiloted(Unit& u, double dt, std::vector<GameEvent>& events);

    /// Not between tank and anchored mode.
    bool settled(const Unit& u) const;

    /// A unit whose weapon reaches past arm's length (a Ranger's rifle):
    /// it aims along its sight and fires with nothing in it, too.
    bool pilotShoots(const Unit& u) const;

    /// The trigger held with nothing under the sight: a gun still fires,
    /// at its weapon's pace, into the distance. Not with an enemy inside its
    /// minimum range on the sight: the shell would land on it.
    void pilotMiss(Unit& u, std::vector<GameEvent>& events);

    /// Walk in the unit's own frame; blocked, it slides along whichever axis
    /// is still open (a jumper takes off at a cliff face instead). Wheels
    /// and treads do not sidestep; an anchored Longbow creeps (`Pilot.pace`).
    void pilotWalk(Unit& u, Vec2 walk, double dt);

    /// Nothing solid at `p` for the unit: open ground, clear of buildings
    /// and live ore deposits (a flyer goes anywhere on the map).
    bool pilotCanStand(const Unit& u, Vec2 p) const;

    /// Something the unit is busy with that holds it in place (mid-jump,
    /// a Prospector drilling, inside a Derrick, welding or mending). True while
    /// it is.
    bool pilotBusy(Unit& u, const Pilot& p, double dt, std::vector<GameEvent>& events);

    /// Whatever happens by walking somewhere (a Prospector drops its load at a
    /// Citadel).
    void pilotArrive(Unit& u, std::vector<GameEvent>& events);

    /// `p` lies ahead of `u`, within about 70° of where it faces.
    bool ahead(const Unit& u, Vec2 p) const;

    /// The enemy the unit would strike: the nearest ahead, on the ground,
    /// within its weapon's reach (and a little). A gun takes the first one
    /// on its line of sight instead (`pilotAimed`).
    std::optional<int64_t> pilotFoe(const Unit& u) const;

    /// Aim assist for the sight: how far beside the line of fire a target's
    /// edge may be and still be hit.
    static constexpr double pilotAimSlack = 0.3;

    /// The first enemy a gun's line of fire meets within its range (the
    /// high-ground bonus counted), if the first one on the line is in range.
    std::optional<int64_t> pilotAimed(const Unit& u) const;

    /// The first enemy on the driven gun's line of fire (where it looks),
    /// out to sight range: a unit or building whose edge comes within
    /// `pilotAimSlack` of the line. `distance` is edge to edge; `inRange`
    /// is true if and only if a shot now would reach it (not inside a
    /// anchored tank's minimum range). A Dropship gets its patient instead
    /// (`healSight`).
    std::optional<PilotSight> pilotSight(const Unit& u) const;

    /// A gun on foot's sight: the enemy the crosshair is on, if it can hit
    /// it and it is within sight; nothing for a friend, a rock or the sky.
    std::optional<PilotSight> crosshairSight(const Unit& u, const Pilot::Crosshair& c) const;

    /// Edge to edge, how far along `u`'s line of sight it meets something of
    /// radius `r` at `p`: nil when the line passes it by, it is behind, or
    /// it is out of sight range.
    std::optional<double> sightGap(const Unit& u, Vec2 p, double r) const;

    /// What one attack of `u` would take off target `id` now.
    std::optional<double> strikeDamage(const Unit& u, int64_t id) const;

    /// What the action key would do for `u` where it stands and faces.
    std::optional<PilotTarget> pilotTarget(const Unit& u) const;

    void pilotAct(Unit& u, const PilotTarget& target, bool pressed, double dt, std::vector<GameEvent>& events);

    // MARK: - Prospector

    std::optional<PilotTarget> prospectorTarget(const Unit& u) const;

    /// The driven Prospector with R held (`Pilot.mend`): it only repairs.
    bool mending(const Unit& u) const;

    /// What the driven Prospector would repair with R held: a hurt machine
    /// (Field welder), else the nearest damaged finished building of its
    /// own, close and ahead. Nil: nothing to repair there.
    std::optional<PilotTarget> pilotRepair(const Unit& u) const;

    /// Field welder: a hurt machine of its side, close and ahead.
    std::optional<int64_t> hurtMachine(const Unit& u) const;

    std::optional<int64_t> wellIndex(const Structure& s) const;

    /// Drilling (while the key is held), inside a Derrick, welding a
    /// building (until the Prospector walks off) or mending one (while held).
    /// Walking off a patch, or letting go of the key, loses the drilling.
    bool prospectorBusy(Unit& u, const Pilot& p, double dt, std::vector<GameEvent>& events);

    /// Field welder: the driven Prospector mends machine `id` while the key is
    /// held and it stays close; a full repair takes the pick's share of the
    /// machine's training time.
    bool prospectorMend(Unit& u, int64_t id, const Pilot& p, double dt);

    /// A load is dropped off by walking up to one's own Citadel.
    void prospectorDeposit(Unit& u, std::vector<GameEvent>& events);

    void prospectorMine(Unit& u, int64_t pi_);

    void prospectorEnter(Unit& u, int64_t rid);

    void prospectorResume(Unit& u, int64_t sid);

    // MARK: - Building by hand

    /// Why player `owner` cannot build `kind` now (money, supply of tech),
    /// or nil.
    std::optional<std::string> buildRefusal(Structure::Kind kind, int64_t owner) const;

    /// Where a building of `kind` would stand if placed near `p`: on the
    /// build grid (whole cells for odd sizes, cell corners for even), and a
    /// Derrick on the well nearest `p` (within 4 cells).
    std::optional<Vec2> snap(Structure::Kind kind, Vec2 p) const;

    /// Why a `kind` cannot stand at `p` (the ground, or something in the
    /// way), or nil when it can.
    std::optional<std::string> placementRefusal(Structure::Kind kind, Vec2 p) const;

    /// The driven Prospector puts up a `kind` at `p`: paid now, and it welds it
    /// until done or until it walks off. Returns why not, or nil when it
    /// started.
    std::optional<std::string> pilotBuild(Structure::Kind kind, Vec2 p);

    /// A building's name as the game writes it.
    static std::string title(Structure::Kind k);

    /// A unit's name as the game writes it.
    static std::string title(Unit::Kind k);
