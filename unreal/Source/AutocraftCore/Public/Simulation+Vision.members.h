// Members of Simulation from the Vision extension (Sources/GameCore/Vision.swift,
// "The simulation's fog"); the bodies are in Vision.cpp. Included inside
// `class Simulation`.

    /// Player `p` sees the unit or building `id` now (its own always; every
    /// one on a map without fog).
    bool sees(int64_t p, int64_t id) const;
    /// Player `p` sees the ground at `at` now (everywhere without fog).
    bool sees(int64_t p, Vec2 at) const;
    /// What player `p` knows of the others (nil: no fog on this map).
    std::optional<Intel> intel(int64_t p) const;
    /// What player `p` sees this moment (nil: no fog on this map).
    std::optional<Sight> sight(int64_t p) const;
    std::optional<int64_t> owner(int64_t id) const;
    /// A target `owner`'s units may pick: its own, or one of the others'
    /// it sees.
    std::optional<Target> foe(int64_t id, int64_t owner_) const;
    /// A buried Scorpion is out of sight of player `p`, on every map (see
    /// `Vision.hides`); the ones that are, and the game without them.
    bool hiddenFrom(const Unit& u, int64_t p) const;
    std::set<int64_t> hiddenScorpions(int64_t p) const;
    GameState withoutHidden(int64_t p) const;
    /// Look at once, for a game set up by hand (units moved, no step yet).
    void lookNow();
    /// Work out what every player sees, and update what each knows.
    void look();
    /// The game as player `p` knows it: its own units and buildings, the
    /// others' it sees, and the rest of theirs as it last saw them. Its AI
    /// plays from this (`seen(by:)`), and its humans' screens show it.
    GameState known(int64_t p) const;
    /// The game as player `p`'s humans see it on screen: as `known(by:)`,
    /// but only the others' units in sight now (no memory of units, only
    /// of buildings).
    GameState shown(int64_t p) const;
    /// The others' units and buildings player `p` does not see now (none
    /// without fog).
    std::set<int64_t> unseen(int64_t p) const;
    /// This simulation as player `p` knows the game (`known(by:)`), for its
    /// AI to decide from. Orders it gives are applied to the real one.
    Simulation seen(int64_t p) const;
    /// When player `p` last saw base site `i` (-inf: never; now on a map
    /// without fog).
    double lastSeen(int64_t i, int64_t p) const;
