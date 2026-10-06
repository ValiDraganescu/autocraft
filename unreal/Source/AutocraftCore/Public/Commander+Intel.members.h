// Members of Commander+Intel.swift (bodies in Private/Commander+Intel.cpp).
// Included inside `struct Commander` by Commander.h; the small types Swift
// nests in `Commander` are nested here too.

    /// What the AI has seen of its enemies' army (in supply), from the
    /// units in sight and those it saw within `Rules.memory`: what its
    /// production answers (`counters`).
    struct EnemyMix {
        /// Infantry (Rangers, Comets, Juggernauts).
        int64_t bio = 0;
        /// Armoured ground units (Juggernauts, Longbows).
        int64_t armored = 0;
        /// Light ground units (Rangers, Comets, Fireflies).
        int64_t light = 0;
        /// Fliers.
        int64_t air = 0;
        /// Fliers that shoot (Kestrels).
        int64_t armedAir = 0;
        /// What shoots air: soldiers that can, and 2 a Sentinel.
        int64_t antiAir = 0;
        /// All its soldiers, on the ground and in the air.
        int64_t total = 0;
        /// Atlases (a unit each): nothing it has shoots air.
        int64_t atlases = 0;
        /// The ground share of `total`.
        int64_t ground() const { return total - air; }

        bool operator==(const EnemyMix&) const = default;
    };

    EnemyMix enemyMix(const GameState& s) const;

    /// Teams (C++ only, PORTING.md "Teams"): the enemy team its side goes
    /// for. Of the teams it knows a building of, the one whose starts lie
    /// nearest its own team's starts; every ally works it out from the same
    /// starts, so allies attack the same team. Nil: no enemy building known.
    std::optional<int64_t> focus(const GameState& s) const;
    /// An ally's army on the attack (not defending, not falling back) is
    /// joined at its point by an army of at least this much supply, so
    /// allies fight as one; an army this big with no attack of its own
    /// also defends an ally's base under a real attack (C++ only).
    static constexpr int64_t allyJoin = 6;

    /// How its army mix bends to what it has seen, on top of its style:
    /// - Longbows: anchored, they break any massed ground army; one more
    ///   per 8 enemy ground supply (up to 8 more);
    /// - Juggernauts one to one with Rangers once a third of the enemy's
    ///   ground army is armoured (their grenades' bonus);
    /// - Fireflies against light infantry: one per 4 of its light bio
    ///   supply (up to 8), once the enemy's ground army is mostly light;
    /// - Hailstorms against fliers: one per 3 supply of armed fliers and
    ///   per 6 of Dropships (up to 10);
    /// - Sentinels at every base once armed fliers are about: one, two
    ///   against three Kestrels or more;
    /// - Kestrels while the enemy has little to shoot air with (under 6
    ///   supply of it): two more, and two more for each Atlas seen (up to
    ///   two Atlases: it cannot shoot air);
    /// - Peregrines against armed fliers: about one per armed flyer seen
    ///   (a Kestrel's 3 supply, a Peregrine's 2), up to 8.
    struct Counters {
        int64_t longbows = 0;
        std::optional<int64_t> perJuggernaut;
        int64_t fireflies = 0;
        int64_t hailstorms = 0;
        int64_t sentinels = 0;
        int64_t kestrels = 0;
        int64_t peregrines = 0;

        bool operator==(const Counters&) const = default;
    };

    Counters counters(const EnemyMix& m) const;

    /// Where a new attack goes: the enemy base it can hurt most for the
    /// least, rather than simply the nearest building. Each known enemy
    /// Citadel scores its way there (cells) plus `guardCost` cells for
    /// every Ranger's worth of enemy soldiers and defences around it
    /// (double on ground above the army), so an undefended expansion is
    /// hit before a guarded main a little closer. With no Citadel known, the nearest building; with none,
    /// where it has looked least (`scout`).
    std::optional<Vec2> attackTarget(const Simulation& sim, Vec2 front) const;

    /// How much more enemies on higher ground than the army count, in a
    /// fight and at a base it picks to attack.
    static constexpr double uphill = 2.0;
    /// Cells of way an attack accepts to avoid each Ranger's worth of
    /// guards at a base.
    static constexpr double guardCost = 4.0;
    /// How far from a base its guards stand.
    static constexpr double guardRadius = 14.0;

    /// Where an attacking army strung out on its way (under
    /// `gatherShare` of it within `gatherRadius` of its middle, the point
    /// still `gatherRadius` × 2 off) gathers: on the soldier nearest its
    /// middle, which stands on ground it can reach. Nil when it is
    /// together enough, or nearly there.
    std::optional<Vec2> regroup(const std::vector<Unit>& rangers, Vec2 front, Vec2 a) const;

    /// Most of the army stands within `gatherRadius` of `p`.
    bool gathered(const std::vector<Unit>& rangers, Vec2 p) const;

    static constexpr double gatherRadius = 9.0;
    static constexpr double gatherShare = 0.6;

    /// A spot for a Sentinel at a base: beside its ore line, within its
    /// reach of the line's middle (where raiders strike), never on the
    /// Prospectors' way between the Citadel and a deposit, and not on top
    /// of another Sentinel (`avoid`).
    std::optional<Vec2> sentinelSpot(const GameState& s, const Structure& citadel,
                                     const std::vector<Vec2>& avoid = {}) const;
    /// A forward Sentinel (C++ only, `frontDefence`): 5-11.5 cells out of
    /// the Citadel within 52 degrees of the enemy's air approach, on open
    /// ground in front of the buildings. Nil if none.
    std::optional<Vec2> forwardSentinelSpot(const GameState& s, const Structure& citadel,
                                            const std::vector<Vec2>& avoid = {}) const;
