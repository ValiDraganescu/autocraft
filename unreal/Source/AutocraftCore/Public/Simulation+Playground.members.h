// Members of `Simulation` from Sources/GameCore/Simulation+Playground.swift,
// included inside the class body of `Simulation.h`. The playground
// (`autocraft models`): anything put down by hand, for either side, then
// played with as in a game.

    /// Ore in a deposit put down by hand: a full base patch's.
    static constexpr int64_t placedOre = 1800;

    /// Put down a `kind` for `owner` at `p` (a ground unit on the nearest
    /// free spot), facing `heading`, as if just trained. Its id.
    int64_t placeUnit(Unit::Kind kind, int64_t owner, Vec2 p, double heading);
    /// Why a `kind` can't be put down at `p`: on open ground, clear of
    /// buildings, ore, wells and rocks (a flyer anywhere in play). Nil: it can.
    std::optional<std::string> unitRefusal(Unit::Kind kind, Vec2 p) const;
    /// A finished `kind` for `owner` at `p` (`placementRefusal` says
    /// where it fits; a Lab goes on a building's add-on spot, `placeLab`).
    /// Its id.
    int64_t placeStructure(Structure::Kind kind, int64_t owner, Vec2 p);
    /// The building a Lab put down at `p` would join: one of `owner`'s
    /// finished Garrisons, Foundries or Spacedocks without one, under `p`
    /// or with its add-on spot there.
    std::optional<Structure> labHost(Vec2 p, int64_t owner) const;
    /// Why no Lab can go on `host` (its add-on spot taken), or nil.
    std::optional<std::string> labRefusal(const Structure& host) const;
    /// A finished Lab on `host`'s add-on spot, as the `.addon` order
    /// builds it. Its id, or nil when it can't go there.
    std::optional<int64_t> placeLab(int64_t host);
    /// Why an ore deposit, a well or a doodad of reach `r` can't go down at
    /// `p`: in play, and clear of buildings, ore, wells and rocks. Nil: it can.
    std::optional<std::string> propRefusal(Vec2 p, double r) const;
    /// A full ore deposit at `p`, its long axis turned by `angle`. Its id.
    int64_t placePatch(Vec2 p, double angle);
    /// A full MH well at `p` (after the map's in `GameState.wells`).
    void placeWell(Vec2 p);
    /// A doodad, after the map's own (`GameState.doodads`); a tower is a
    /// watchtower like the map's.
    void placeDoodad(const Doodad& d);
    /// Kill unit or building `id`, as in a fight. Its name, or nil.
    std::optional<std::string> remove(int64_t id);
    /// Take away what stands at `p`, nearest first: a unit or a building
    /// (killed, as in a fight), a doodad or well put down by hand, or an
    /// ore deposit (mined out for good). What went, or nil for nothing.
    std::optional<std::string> removeProp(Vec2 p);
