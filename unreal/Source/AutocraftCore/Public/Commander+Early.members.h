// Members of Commander+Early.swift (bodies in Private/Commander+Early.cpp).
// Included inside `struct Commander` by Commander.h.

    // MARK: - Scouting

    /// One Prospector scouts the enemy starts it has not seen, once its
    /// side has `scoutAt` Prospectors (early, while a rush can still be
    /// seen coming): it walks to the nearest unseen start, then the next,
    /// and goes back to work once it has seen them all, or when it is
    /// hurt. One that dies before it saw them is replaced until
    /// `scoutUntil`. Without fog of war there is nothing to find.
    std::vector<Command> scouts(const Simulation& sim, const std::set<int64_t>& busy) const;

    /// Prospectors it has before one goes scouting.
    static constexpr int64_t scoutAt = 13;
    /// Game time after which a lost scout is not replaced.
    static constexpr double scoutUntil = 240.0;

    // MARK: - Pulling Prospectors

    /// Against a small early attack on a base that its soldiers there
    /// cannot hold, it pulls Prospectors off the ore to fight (two for
    /// every Ranger's worth it is short, from that base), and sends them
    /// back to work once the attackers are gone or held, when an attack
    /// is too big for Prospectors (`militiaMax`), or when one is badly
    /// hurt.
    std::vector<Command> militia(const Simulation& sim, const std::set<int64_t>& busy) const;

    /// Attacks worth more than this (in Rangers) are left to the army.
    static constexpr double militiaMax = 8.0;
    /// How close to a Citadel attackers count as on the base.
    static constexpr double militiaRadius = 12.0;
