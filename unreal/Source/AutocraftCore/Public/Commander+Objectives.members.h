// Members of Commander+Objectives.swift (bodies in
// Private/Commander+Objectives.cpp). Included inside `struct Commander` by
// Commander.h; the small types Swift nests in `Commander` are nested here too.

    // The team's objectives (`Directives.objectives`): the AI splits its army
    // into a squad per objective, as it judges each needs. Every team's
    // `Commander` runs this the same way.

    /// A squad to defend a base is worth at least this much (in Rangers).
    static constexpr double guardForce = 4.0;
    /// An attack squad is worth at least this much before it sets out.
    static constexpr double assaultForce = 6.0;
    /// A squad falling back is home once this share of it is within
    /// `homeRadius` of the rally.
    static constexpr double regroupShare = 0.7;
    static constexpr double homeRadius = 8.0;

    /// Orders for the squads this second: who is in which, and what each
    /// does (see `plan`).
    std::vector<Command> squads(const Simulation& sim) const;

    /// Where each of the team's objectives stands, for its humans.
    std::vector<ObjectiveStatus> objectiveStatus(const Simulation& sim) const;

    /// What `plan` returns (a tuple in Swift).
    struct Plan {
        std::vector<Command> out;
        std::vector<ObjectiveStatus> status;
    };

    /// Once a second the whole field army (soldiers out of Bastions, not
    /// raiding, hunting or dropping) is shared out again, in this order:
    /// 1. a real attack on one of its bases (more than a raid): the army at
    ///    home keeps 1.5 times the attackers' worth, unless all in;
    /// 2. each base to defend: 1.5 times the enemies near it, at least
    ///    `guardForce`; each area held: the same, at least `assaultForce`;
    /// 3. each attack, in the order given: what defends the point times the
    ///    odds the stance wants, at least `assaultForce`;
    /// 4. the rest to the attacks, each time to the one shortest of what
    ///    it needs; with no attack it stays in the army.
    /// Soldiers stay in their squad while it still needs them, and the
    /// nearest fill what is missing. An attack squad gathers at the rally
    /// until it is worth what it needs (all in: at once), attacks, falls back
    /// when the fight goes against it (never all in) and goes again. Once it
    /// stands at its point with nothing of the enemy's near, the attack
    /// becomes a hold: the squad stays there and fights what comes.
    /// Bastions to man are not squads (see `manning`); their Rangers come
    /// out of this pool before it is shared.
    Plan plan(const Simulation& sim) const;

    /// A Bastion the humans want manned, this second.
    struct Manning {
        Objective objective;
        /// Nil once it is gone (or no longer the team's).
        std::optional<Structure> bastion;
        int64_t inside = 0;
        /// Rangers already walking to it.
        int64_t coming = 0;
        /// Rangers to send now (none until the Bastion is finished).
        std::vector<Unit> send;
        /// Rangers to train for it: what is left of its room after the
        /// Rangers to spare and those already training. (`short` in Swift,
        /// a keyword in C++.)
        int64_t short_ = 0;
    };

    /// The team's Bastions to man, in the order given, and the Rangers for
    /// each: any of the team's Rangers out of Bastions and Dropships, not
    /// driven and on no errand of their own (raid, drop, hunt), the field
    /// army before the squads, the nearest first.
    std::vector<Manning> manning(const Simulation& sim) const;

    /// The map's base sites as the humans see them when choosing where to
    /// expand: who holds each, which are contested, and the AI's own next.
    std::vector<SiteInfo> siteInfo(const Simulation& sim) const;
