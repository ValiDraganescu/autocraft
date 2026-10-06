// Members of `class Simulation` from Sources/GameCore/Pilot+Kinds.swift,
// included inside the class body by Simulation.h. Declarations only; the
// bodies are in Private/Pilot+Kinds.cpp.
// (No include guard: it is a fragment of a class body.)
//
// What the driven unit does that depends on its kind, past the Prospector's
// work: a tank's turret and anchored mode, a Comet's cliff jumps, a
// Dropship's heal and cargo. The guns (Comet, Firefly, Juggernaut, tank)
// need nothing here: `strike` does their pistols, flame, grenades and
// splash.

public:
    /// Reach, from a Dropship's ground point to a unit's edge, at which the
    /// ability key takes that unit aboard.
    static constexpr double pilotLoadReach = 1.5;
    /// Farthest a jumper looks past a cliff face for ground to land on.
    static constexpr double pilotJumpReach = 3.0;

    // MARK: - Facing

    /// Face where the mouse says. A vehicle's hull turns on A/D instead
    /// (slowly anchored, not while changing mode), and its turret, if it has
    /// one, traverses toward where the mouse looks at its own rate.
    void pilotFace(Unit& u, const Pilot& p, double dt) const;

    // MARK: - Ability

    /// What the ability key would do for `u` now; nil for a kind with no
    /// ability. A tank anchors or packs up (a press mid-way turns it back);
    /// a Dropship loads the unit of its side under its sight (`healSight`)
    /// if that one is in load reach and fits, else sets down everyone
    /// aboard.
    std::optional<PilotAbility> pilotAbility(const Unit& u) const;

    /// The ability key went down: do what `pilotAbility` says.
    void pilotUse(Unit& u, std::vector<GameEvent>& events);

    // MARK: - Comet

    /// A jumper whose next step is into a cliff face takes off over it, for
    /// the first walkable ground ahead (the 0.2-cell scan of `leap`, out to
    /// `pilotJumpReach`, farther with Long jump). True when it took off.
    bool pilotJump(Unit& u, Vec2 step) const;

    /// Mid-jump: on toward `jumpTo` at its speed; there, the jump ends (and
    /// Booster's rush starts).
    void pilotFly(Unit& u, double dt) const;

    // MARK: - Dropship

    /// Cargo slots taken aboard a Dropship (`Rules.slots` each, of
    /// `Rules.dropshipSlots`).
    int64_t slotsUsed(const Unit& m) const;

    /// Living units of `m`'s side it could heal (bio units, machines too
    /// with Nanite beam) or load (a Scorpion too): out in the open, not
    /// aboard, in a Bastion or in a Derrick.
    std::vector<Unit> patients(const Unit& m) const;
    /// `m`'s beam can heal `v` (a bio unit; any machine with Nanite beam).
    /// A Scorpion is a patient to load only.
    bool healable(const Unit& m, const Unit& v) const;

    /// Double beam: the most hurt patient in heal reach of `m` other than
    /// `id`, if any.
    std::optional<int64_t> secondPatient(const Unit& m, int64_t id) const;

    /// A healer's sight: the first patient on its line of sight, or one
    /// right under it (centre within its radius of its ground point), whole
    /// or hurt. Reach is `Rules.healRange`.
    std::optional<PilotSight> healSight(const Unit& u) const;

    /// Heal the patient under the sight, if it is in reach and hurt and
    /// there is energy for it.
    std::optional<PilotTarget> healTarget(const Unit& u) const;

    /// One step of a Dropship's beam on unit `id`: `Rules.healRate` a second,
    /// a third of an energy point per hit point, never past full (its
    /// picks change both). It shows as attacking that unit (the scene draws
    /// the beam from that).
    void heal(Unit& m, int64_t id, double dt);
