// Members of Simulation from Simulation+Leveling.swift, included inside the
// class body (Simulation.h); the bodies are in Simulation+Leveling.cpp.
//
// Pilot leveling in the simulation (docs/leveling.md): XP as the driven
// unit works, picks as orders, and one place (`boost`) that says what the
// picks do to a unit's stat right now. With no one driving, every boost
// is `.none` and the game plays exactly as it did before leveling.

    // MARK: - The record

    /// The player's leveling this game (empty before any driving). (A
    /// reference, not Swift's copy: `boost` reads it many times a step.)
    const PilotRecord& levels() const;

    /// The pick is made and the player drives a unit of its kind now: its
    /// effects are on.
    bool active(Perk perk) const;

    /// The effects of the picks for the kind the player drives now, and
    /// the driven unit; nil with no one driving.
    std::optional<std::pair<Unit, std::vector<Effect>>> drivenEffects() const;

    /// What a pick does in this simulation: its own effects, or the
    /// ones a test tries on it (`trialEffects`). (Swift's `effects(of:)`.)
    const std::vector<Effect>& effects(Perk perk) const;

    void updateLevels(const std::function<void(PilotRecord&)>& body);

    // MARK: - Picking

    /// Keep `perk` for its kind: one of the two on offer at that kind's
    /// lowest pending level. False, and nothing changes, otherwise. The
    /// same as `issue(.pick(player: Pilot.player, perk))`.
    bool pick(Perk perk);

    /// `Command.pick`'s check and keep.
    bool keepPick(int64_t player, Perk perk);

    // MARK: - XP

    /// The driven unit `u` did `xp` worth of work. A level crossed raises
    /// `.leveledUp` at the end of the step. Nothing for any other unit.
    void earn(double xp, const Unit& u);

    /// XP for a hit of `shooter` that took `taken` hp off an enemy worth
    /// `worth` with `full` hp: its worth times the share taken.
    void earnHit(const Unit& shooter, double worth, double taken, double full, bool building);

    // MARK: - Tracking

    /// Add to the tally of the kind the driven unit `u` is; nothing for
    /// any other unit.
    void tally(const Unit& u, const std::function<void(Tally&)>& body);

    /// A stamp for each of `levels` (`from...to`), which `kind` reached now:
    /// the time, the seconds driven with it before, and every player's
    /// standing.
    void stamp(UnitKind kind, int64_t from, int64_t to);

    /// How player `p` stands now (`GameState.standing(of:)`).
    Standing standing(int64_t p) const;

    // MARK: - Effects

    /// What the picks (and level 1's hero, `Leveling.hero`) do to `stat`
    /// on `u` now. Only while the player drives a unit, only on its team,
    /// and only the picks of the kind it drives: `.none` otherwise.
    Boost boost(const Stat& stat, const Unit& u) const;

    /// What the picks do to one of the team's economy stats (`Scope.team`:
    /// costs, training, supply) for player `owner` now.
    Boost teamBoost(const Stat& stat, int64_t owner) const;

    /// `boost(stat, of: u).rate`: how fast `u` does it (1: the base rate).
    double rate(const Stat& stat, const Unit& u) const;

    /// A load `u` takes from a field (or, `hydrogen`, a Derrick) now.
    int64_t load(const Unit& u, bool hydrogen) const;

    /// What player `owner` pays to train a `kind` now.
    Price unitCost(UnitKind kind, int64_t owner) const;

    /// What player `owner` pays for a `kind` its driven Prospector places now
    /// (Prefab); the list price for anyone else.
    Price buildCost(StructureKind kind, int64_t owner) const;

    /// Supply a Hab Dome of player `owner` gives now (Supply chief).
    int64_t habDomeSupply(int64_t owner) const;

    /// Rangers a Bastion of player `owner` holds now (Bastion drill).
    int64_t bastionCapacity(int64_t owner) const;

    /// Cargo slots of Dropship `m` now (Cargo bay).
    int64_t cargoSlots(const Unit& m) const;

    /// What player `owner` pays to research `up` now (Field research).
    Price upgradeCost(Upgrade up, int64_t owner) const;

    /// The driven unit `u` has this effect on now.
    bool drives(const Unit& u, const Effect& effect) const;

    /// The first of the driven unit `u`'s effects that `match` takes (a
    /// one-off's numbers); nil when it is not driven or has none. `match`
    /// returns a `std::optional`.
    template <class Match>
    auto effect(const Unit& u, Match match) const -> decltype(match(std::declval<const Effect&>())) {
        if (!piloted(u.id)) return std::nullopt;
        for (Perk perk : levels().picks(u.kind)) {
            for (const Effect& e : effects(perk)) {
                if (auto t = match(e)) return t;
            }
        }
        return std::nullopt;
    }

    // MARK: - The team's economy

    /// A kill by `shooter`: its side gets its Bounty share of what the
    /// victim cost.
    void bounty(const Unit& shooter, int64_t ore, int64_t hydrogen);

    /// The team lost `u`: it gets its Scrap share of what `u` cost back.
    void refund(const Unit& u);

    // MARK: - Building blocks

    /// The driven unit did `action` once more: true on every third time,
    /// when its pick doubles it (Rich vein, Burst fire).
    bool thirdTime(Action action, const Unit& u);

    /// The burst pick a kind has made, with its numbers.
    std::optional<std::pair<Perk, Burst>> burst(UnitKind kind) const;

    /// The pick a kind has made that takes the ability key (a burst or a
    /// grenade), with its effect.
    std::optional<std::pair<Perk, Effect>> keyPick(UnitKind kind) const;

    /// The ability key's pick for the driven unit, if its kind has one:
    /// ready, or why not. The title is the pick's ("Surge", "Pulse mine").
    std::optional<PilotAbility> keyAbility(const Unit& u) const;

    /// Pulse mine: the driven unit throws its grenade at the crosshair (what
    /// its sight is on, or straight ahead), out to its weapon's reach. It
    /// hits every enemy on the ground within the grenade's radius, and the
    /// scene shows the blast (`.blast`).
    void throwGrenade(Unit& u, std::vector<GameEvent>& events);

    /// Start the driven unit's burst: it pays the hp, and the clock runs.
    void startBurst(Unit& u);

    /// The driven unit jumps cliffs (a Comet, or Cliff hop).
    bool pilotJumps(const Unit& u) const;

    /// Field welder: the share of a machine's training time a full repair
    /// by the driven Prospector `u` takes; nil without it.
    std::optional<double> machineRepair(const Unit& u) const;

    /// The driven unit's own upkeep from its picks: its shield refills,
    /// and it mends itself, once enough time has passed without a hit.
    void levelUpkeep(Unit& u, double dt) const;

    /// A hit of `damage` on the driven unit at `i`: its shield takes what
    /// it can; returns what is left for its hp.
    double shieldTakes(int64_t i, double damage);

    /// Once a step while someone drives, or while anything burns: the
    /// seconds driven, every unit's hp kept in line with its hp picks,
    /// units on fire burn (to the end, driven or not), then the mending
    /// auras around the driven unit.
    void stepLeveling(double dt);

    /// An hp pick coming on raises a unit's hp by as much (as the Aegis
    /// shield does); going off, it lowers its hp only to the new full.
    void syncBonusHP();
