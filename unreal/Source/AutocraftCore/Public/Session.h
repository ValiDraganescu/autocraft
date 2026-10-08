// Port of Sources/GameCore/Session.swift: a session and its JSON file, in
// exactly the shape Swift's Codable reads and writes (keys sorted, optionals
// left out when nil, a `Vec2` as `[x, y]`, an enum with payloads as
// `{"case":{"_0":...}}`, dates as ISO 8601, infinities as strings).
#pragma once

#include "AutocraftCoreApi.h"

#include "Hearing.h"
#include "ScreenConfig.h"
#include "Types.h"

#include <optional>
#include <string>
#include <string_view>

namespace ac {

/// Foundation's `Date`: seconds since 1970-01-01 00:00:00 UTC. Saved as ISO
/// 8601 in UTC, whole seconds (`2026-09-29T08:00:44Z`).
struct AUTOCRAFTCORE_API Date {
    double since1970 = 0;
    /// The wall clock now.
    static Date now();
    bool operator==(const Date&) const = default;
};

/// A game played on one screen configuration.
struct AUTOCRAFTCORE_API Session {
    std::string id;
    std::string signature;
    Date created;
    std::string mapName;
    int64_t mapVersion = 0;
    GameState state;
    /// Where the player listens from; nil hears the whole map.
    std::optional<Listener> listener;
    /// False when the AI commander is switched off (nil: on).
    std::optional<bool> ai;
    /// The fog of war on or off (nil: on, but off in the playground).
    std::optional<bool> fog;
    /// The game being played: a UUID, new whenever a game starts (a new
    /// session, `restart`, `nextGame`), and when it started, wall clock
    /// (nil: a session saved before tracking; see `fillGame`). The sim
    /// knows neither, so it stays deterministic.
    std::optional<std::string> game;
    std::optional<Date> gameStarted;

    /// A fresh game; the win tally carries over while the map keeps its players.
    void restart(const MapDefinition& map);

    /// The next game after a victory lap (`Simulation.newGame`): a new id,
    /// started now. The caller copies the new game's state in.
    void nextGame();

    /// A session saved before tracking gets a game id, started when the
    /// session was (`created`). Nothing changes for one that has them.
    void fillGame();

    bool operator==(const Session&) const = default;
};

/// A new random UUID as Foundation writes it (`UUID().uuidString`).
std::string newUUID();

/// Session files: one JSON file per screen configuration.
struct AUTOCRAFTCORE_API SessionStore {
    std::string directory;

    explicit SessionStore(std::string directory_) : directory(std::move(directory_)) {}

    /// JSON has no infinity, and a site no one has seen yet was last seen
    /// at -∞ (`Intel.sites`): those go as strings.
    struct Floats { std::string_view inf, minus, nan; };
    static constexpr Floats floats{"inf", "-inf", "nan"};

    /// `decoder.decode(T.self, from:)`: nil when the JSON does not hold a
    /// `T` the way Swift's decoder reads it (`error`, if given, says why).
    /// Instantiated for Session, GameState, MapDefinition, Player, Unit,
    /// Structure, Intel, Directives, PilotRecord, KindRecord, Tally and Listener.
    template <class T> static std::optional<T> decode(std::string_view json, std::string* error = nullptr);
    /// `encoder.encode(_:)`: sorted keys; pretty-printed unless `pretty` is false.
    template <class T> static std::string encode(const T& value, bool pretty = true);

    /// `~/Library/Application Support/Autocraft/sessions` (from `$HOME`).
    static std::string defaultDirectory();

    std::string url(const std::string& id) const;

    struct Opened {
        Session session;
        bool resumed = false;
    };

    /// The saved session for a configuration, or a new one when there is none
    /// or its map has changed since it was saved.
    Opened open(const ScreenConfig& config, const MapDefinition& map) const;

    /// The saved session under `id` (window mode has its own, apart from
    /// the wallpaper's), or a new one on `map`. Either has a game id.
    Opened open(const std::string& id, const std::string& signature, const MapDefinition& map) const;

    /// Start the configuration's world over: a fresh game on the map,
    /// keeping "my location" and the AI setting. (nil: it could not be saved.)
    std::optional<Session> restart(const ScreenConfig& config, const MapDefinition& map) const;

    /// Start `id` over on `map`, keeping "my location", the AI and the fog.
    std::optional<Session> restart(const std::string& id, const std::string& signature, const MapDefinition& map) const;

    /// Write `session` to its file in `directory` (false: it could not).
    bool save(const Session& session) const;

    /// Read a session file at any path (a fixture, say).
    static std::optional<Session> load(const std::string& path, std::string* error = nullptr);
    /// Write a session file at any path, atomically (false: it could not).
    static bool save(const Session& session, const std::string& path);
};

/// The whole of a file, or nil when it cannot be read.
AUTOCRAFTCORE_API std::optional<std::string> readFile(const std::string& path);
/// Write a whole file through a temporary one and a rename.
bool writeFileAtomically(const std::string& path, const std::string& contents);

} // namespace ac
