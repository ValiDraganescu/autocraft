// The tracking database: tables, views and the upsert of a game
// (docs/leveling.md, "Where it goes").
#include "TrackingStore.h"

#include "Leveling.h"

// In the Unreal build the engine's SQLiteCore names its exports
// `SQLITE_API=SQLITECORE_API=DLLIMPORT`; the core includes no engine header,
// so this is DLLIMPORT as the platform headers define it (WindowsPlatform.h,
// ApplePlatform.h, UnixPlatform.h). (core-tests build against the system's
// sqlite3.h, which has no such macro.)
#if defined(SQLITE_API) && !defined(DLLIMPORT)
#if defined(_WIN32)
#define DLLIMPORT __declspec(dllimport)
#else
#define DLLIMPORT __attribute__((visibility("default")))
#endif
#endif
#include <sqlite3.h>

#include <cstdio>

namespace ac {

namespace coding {
std::string formatISO8601(const Date& v);
}

namespace {

// MARK: - Statements

/// A prepared statement whose values are bound in order.
class Stmt {
public:
    Stmt(sqlite3* db, const std::string& sql) : db(db) {
        status = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    }
    ~Stmt() { sqlite3_finalize(stmt); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    bool ok() const { return status == SQLITE_OK; }
    Stmt& text(const std::string& v) {
        sqlite3_bind_text(stmt, ++at, v.c_str(), int(v.size()), SQLITE_TRANSIENT);
        return *this;
    }
    Stmt& integer(int64_t v) {
        sqlite3_bind_int64(stmt, ++at, v);
        return *this;
    }
    Stmt& real(double v) {
        sqlite3_bind_double(stmt, ++at, v);
        return *this;
    }
    Stmt& null() {
        sqlite3_bind_null(stmt, ++at);
        return *this;
    }
    Stmt& real(const std::optional<double>& v) { return v ? real(*v) : null(); }
    Stmt& text(const std::optional<std::string>& v) { return v ? text(*v) : null(); }
    /// The nine columns of a standing, or nulls.
    Stmt& standing(const std::optional<Standing>& s) {
        if (!s) {
            for (int i = 0; i < 8; i++) null();
            return *this;
        }
        return integer(s->army).integer(s->workers).integer(s->ore).integer(s->hydrogen).integer(s->supplyUsed)
            .integer(s->supplyCap).integer(s->buildings).integer(s->mined);
    }
    /// Runs it to the end (no rows wanted).
    bool run(std::string* error) {
        if (!ok()) {
            if (error) *error = sqlite3_errmsg(db);
            return false;
        }
        int rc = sqlite3_step(stmt);
        while (rc == SQLITE_ROW) rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            if (error) *error = sqlite3_errmsg(db);
            return false;
        }
        return true;
    }
    sqlite3_stmt* raw() { return stmt; }

private:
    sqlite3* db;
    sqlite3_stmt* stmt = nullptr;
    int status = SQLITE_OK;
    int at = 0;
};

/// "INSERT INTO table (a, b) VALUES (?, ?)".
std::string insert(const std::string& table, const std::vector<std::string>& columns) {
    std::string cols, marks;
    for (size_t i = 0; i < columns.size(); i++) {
        cols += (i ? ", " : "") + columns[i];
        marks += i ? ", ?" : "?";
    }
    return "INSERT INTO " + table + " (" + cols + ") VALUES (" + marks + ")";
}

/// `prefix_army, prefix_workers, ...` (a standing's columns).
std::vector<std::string> standingColumns(const std::string& prefix) {
    std::vector<std::string> out;
    for (const char* c : {"army", "workers", "ore", "hydrogen", "supply_used", "supply_cap", "buildings", "mined"})
        out.push_back(prefix + c);
    return out;
}

std::string standingDefinitions(const std::string& prefix) {
    std::string out;
    for (const std::string& c : standingColumns(prefix)) out += ", " + c + " INTEGER";
    return out;
}

std::string kindName(UnitKind k) { return std::string(rawValue(k)); }
std::string perkName(Perk p) { return std::string(rawValue(p)); }

// MARK: - The tables

/// Version 1: the tables of docs/leveling.md, "Where it goes". Times are
/// game seconds; dates are ISO 8601 text, UTC.
const char* const tablesV1 = R"sql(
CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS games (
  game TEXT PRIMARY KEY, install TEXT NOT NULL, session TEXT, started TEXT, ended TEXT, length REAL,
  result TEXT NOT NULL, map TEXT, map_version INTEGER, ai INTEGER, signature TEXT NOT NULL, build TEXT, os TEXT);
CREATE TABLE IF NOT EXISTS players (
  game TEXT NOT NULL, player INTEGER NOT NULL, human INTEGER, style TEXT, aggression REAL, greed REAL,
  garrison_per_base INTEGER, comets INTEGER, fireflies INTEGER, drops INTEGER, start_base INTEGER, won INTEGER,
  army INTEGER, workers INTEGER, ore INTEGER, hydrogen INTEGER, supply_used INTEGER, supply_cap INTEGER,
  buildings INTEGER, mined INTEGER, units_lost INTEGER, PRIMARY KEY (game, player));
CREATE TABLE IF NOT EXISTS kinds (
  game TEXT NOT NULL, kind TEXT NOT NULL, seconds REAL, takeovers INTEGER, xp REAL, level INTEGER,
  unit_damage REAL, building_damage REAL, kills INTEGER, razed INTEGER, damage_taken REAL, shield_absorbed REAL,
  deaths INTEGER, ore INTEGER, hydrogen INTEGER, built REAL, repaired REAL, healed REAL, carried INTEGER,
  burn REAL, blast_hits INTEGER, air_kills INTEGER, stomps INTEGER, stomp_hits INTEGER, strikes INTEGER,
  strike_hits INTEGER, hidden_seconds REAL, PRIMARY KEY (game, kind));
)sql";

std::string levelsV1() {
    return "CREATE TABLE IF NOT EXISTS levels (game TEXT NOT NULL, kind TEXT NOT NULL, level INTEGER NOT NULL, "
           "one TEXT NOT NULL, other TEXT NOT NULL, picked TEXT, reached REAL, picked_at REAL, waited REAL, "
           "driven REAL" +
           standingDefinitions("own_") + standingDefinitions("enemy_") + ", PRIMARY KEY (game, kind, level));";
}

// MARK: - The views

/// Views of the numbers, per balance signature (`signature` is a column
/// of each). A win rate counts only games that are over: won, lost or
/// drawn, and a draw is not a win. Made again each time the file opens.
/// (`sqrt` is in the system SQLite the tools read with, not in the
/// engine's build: the views are only ever read from outside.)
const char* const views = R"sql(
DROP VIEW IF EXISTS perk_balance;
DROP VIEW IF EXISTS level_funnel;
DROP VIEW IF EXISTS pick_paths;
DROP VIEW IF EXISTS drive_share;

CREATE VIEW perk_balance AS
WITH cards AS (
  SELECT g.signature, l.game, l.kind, l.level, l.one AS perk, l.other AS other,
         l.picked IS NOT NULL AS took, COALESCE(l.picked = l.one, 0) AS picked, g.result
    FROM levels l JOIN games g USING (game)
  UNION ALL
  SELECT g.signature, l.game, l.kind, l.level, l.other, l.one,
         l.picked IS NOT NULL, COALESCE(l.picked = l.other, 0), g.result
    FROM levels l JOIN games g USING (game)
), per AS (
  SELECT signature, kind, level, perk, other,
         COUNT(*) AS offered, SUM(took) AS took, SUM(picked) AS picked,
         SUM(picked AND result IN ('won', 'lost', 'drawn')) AS games,
         SUM(picked AND result = 'won') AS wins
    FROM cards GROUP BY signature, perk
), rated AS (
  SELECT *, 1.0 * picked / NULLIF(took, 0) AS pick_rate, 1.0 * wins / NULLIF(games, 0) AS win_rate FROM per
)
SELECT a.signature, a.kind, a.level, a.perk, a.offered, a.took, a.picked, a.pick_rate,
       a.games, a.wins, a.win_rate, 1.96 * sqrt(a.win_rate * (1 - a.win_rate) / a.games) AS margin,
       a.other, b.games AS other_games, b.wins AS other_wins, b.win_rate AS other_win_rate,
       a.win_rate - b.win_rate AS gap
  FROM rated a JOIN rated b ON b.signature = a.signature AND b.perk = a.other;

CREATE VIEW level_funnel AS
WITH RECURSIVE n(level) AS (SELECT 2 UNION ALL SELECT level + 1 FROM n WHERE level < 10),
driven AS (
  SELECT g.signature, k.kind, COUNT(*) AS games FROM kinds k JOIN games g USING (game) GROUP BY g.signature, k.kind
), reached AS (
  SELECT g.signature, l.kind, l.level, COUNT(*) AS games FROM levels l JOIN games g USING (game)
   GROUP BY g.signature, l.kind, l.level
)
SELECT d.signature, d.kind, n.level, d.games AS driven, COALESCE(r.games, 0) AS reached,
       1.0 * COALESCE(r.games, 0) / d.games AS share
  FROM driven d CROSS JOIN n
  LEFT JOIN reached r ON r.signature = d.signature AND r.kind = d.kind AND r.level = n.level;

CREATE VIEW pick_paths AS
WITH paths AS (
  SELECT g.signature, l.kind, l.game, g.result,
         group_concat(l.picked, ' > ' ORDER BY l.level) AS path
    FROM levels l JOIN games g USING (game) WHERE l.picked IS NOT NULL GROUP BY l.game, l.kind
)
SELECT signature, kind, path, COUNT(*) AS games,
       SUM(result IN ('won', 'lost', 'drawn')) AS finished, SUM(result = 'won') AS wins,
       1.0 * SUM(result = 'won') / NULLIF(SUM(result IN ('won', 'lost', 'drawn')), 0) AS win_rate
  FROM paths GROUP BY signature, kind, path;

CREATE VIEW drive_share AS
SELECT g.signature, k.kind, SUM(k.seconds) AS seconds,
       SUM(k.seconds) / NULLIF(SUM(SUM(k.seconds)) OVER (PARTITION BY g.signature), 0) AS share
  FROM kinds k JOIN games g USING (game) GROUP BY g.signature, k.kind;
)sql";

} // namespace

// MARK: - Opening

std::unique_ptr<TrackingStore> TrackingStore::open(const std::string& path, std::string* error) {
    std::unique_ptr<TrackingStore> s(new TrackingStore());
    const int rc = sqlite3_open_v2(path.c_str(), &s->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        if (error) *error = s->db ? sqlite3_errmsg(s->db) : "out of memory";
        return nullptr;
    }
    sqlite3_busy_timeout(s->db, 3000);
    if (!s->exec("PRAGMA journal_mode = WAL; PRAGMA synchronous = NORMAL;", error)) return nullptr;
    if (!s->migrate(error)) return nullptr;
    if (!s->exec(views, error)) return nullptr;
    return s;
}

TrackingStore::~TrackingStore() { sqlite3_close(db); }

bool TrackingStore::exec(const std::string& sql, std::string* error) {
    char* message = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &message) != SQLITE_OK) {
        if (error) *error = message ? message : sqlite3_errmsg(db);
        sqlite3_free(message);
        return false;
    }
    return true;
}

/// Each set of tables has its version in `meta`; a file at a lower
/// version runs the steps after it, in one transaction. A file made by a
/// newer build is left alone.
bool TrackingStore::migrate(std::string* error) {
    if (!exec("CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);", error)) return false;
    int64_t have = 0;
    {
        const auto r = rows("SELECT value FROM meta WHERE key = 'version.tracking'", {}, error);
        if (!r.empty()) have = std::atoll(r[0][0].c_str());
    }
    if (have > version) {
        if (error) *error = "tracking tables are version " + std::to_string(have) + ", this build knows " + std::to_string(version);
        return false;
    }
    if (have < version) {
        if (!exec("BEGIN IMMEDIATE", error)) return false;
        bool ok = true;
        // Step 1: the tables. A later step adds an `if (have < 2)` here, with
        // its ALTER TABLE ... ADD COLUMN statements.
        if (have < 1) ok = exec(tablesV1, error) && exec(levelsV1(), error);
        ok = ok && Stmt(db, "INSERT INTO meta (key, value) VALUES ('version.tracking', ?) "
                            "ON CONFLICT(key) DO UPDATE SET value = excluded.value")
                       .text(std::to_string(version))
                       .run(error);
        if (!ok) {
            exec("ROLLBACK", nullptr);
            return false;
        }
        if (!exec("COMMIT", error)) return false;
    }
    const auto id = rows("SELECT value FROM meta WHERE key = 'install'", {}, error);
    if (!id.empty()) {
        install = id[0][0];
    } else {
        install = newUUID();
        if (!Stmt(db, "INSERT INTO meta (key, value) VALUES ('install', ?)").text(install).run(error)) return false;
    }
    return true;
}

std::vector<std::vector<std::string>> TrackingStore::rows(const std::string& sql, const std::vector<std::string>& values,
                                                          std::string* error) {
    std::vector<std::vector<std::string>> out;
    Stmt s(db, sql);
    if (!s.ok()) {
        if (error) *error = sqlite3_errmsg(db);
        return out;
    }
    for (const std::string& v : values) s.text(v);
    int rc = 0;
    while ((rc = sqlite3_step(s.raw())) == SQLITE_ROW) {
        std::vector<std::string> row;
        for (int i = 0; i < sqlite3_column_count(s.raw()); i++) {
            const unsigned char* t = sqlite3_column_text(s.raw(), i);
            row.push_back(t ? reinterpret_cast<const char*>(t) : "");
        }
        out.push_back(std::move(row));
    }
    if (rc != SQLITE_DONE && error) *error = sqlite3_errmsg(db);
    return out;
}

// MARK: - Writing

bool TrackingStore::write(const TrackedGame& g, std::string* error) {
    if (!exec("BEGIN IMMEDIATE", error)) return false;
    bool ok = Stmt(db, "INSERT INTO games (game, install, session, started, ended, length, result, map, map_version, ai, "
                       "signature, build, os) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                       "ON CONFLICT(game) DO UPDATE SET install = excluded.install, session = excluded.session, "
                       "started = excluded.started, ended = excluded.ended, length = excluded.length, "
                       "result = excluded.result, map = excluded.map, map_version = excluded.map_version, "
                       "ai = excluded.ai, signature = excluded.signature, build = excluded.build, os = excluded.os")
                  .text(g.id).text(install).text(g.session).text(coding::formatISO8601(g.started))
                  .text(coding::formatISO8601(g.ended)).real(g.length).text(std::string(TrackedGame::name(g.result)))
                  .text(g.map).integer(g.mapVersion).integer(g.ai ? 1 : 0).text(g.signature).text(g.build).text(g.os)
                  .run(error);
    for (const char* table : {"players", "kinds", "levels"}) {
        if (!ok) break;
        ok = Stmt(db, std::string("DELETE FROM ") + table + " WHERE game = ?").text(g.id).run(error);
    }
    for (size_t i = 0; ok && i < g.players.size(); i++) {
        const TrackedPlayer& p = g.players[i];
        ok = Stmt(db, insert("players", {"game", "player", "human", "style", "aggression", "greed", "garrison_per_base",
                                         "comets", "fireflies", "drops", "start_base", "won", "army", "workers", "ore",
                                         "hydrogen", "supply_used", "supply_cap", "buildings", "mined", "units_lost"}))
                 .text(g.id).integer(p.player).integer(p.human).text(p.style).real(p.aggression).real(p.greed)
                 .integer(p.garrisonPerBase).integer(p.comets).integer(p.fireflies).integer(p.drops)
                 .integer(p.start).integer(p.won)
                 .integer(p.standing.army).integer(p.standing.workers).integer(p.standing.ore)
                 .integer(p.standing.hydrogen).integer(p.standing.supplyUsed).integer(p.standing.supplyCap)
                 .integer(p.standing.buildings).integer(p.standing.mined).integer(p.unitsLost)
                 .run(error);
    }
    for (size_t i = 0; ok && i < g.kinds.size(); i++) {
        const TrackedKind& k = g.kinds[i];
        const Tally& t = k.tally;
        ok = Stmt(db, insert("kinds", {"game", "kind", "seconds", "takeovers", "xp", "level", "unit_damage",
                                       "building_damage", "kills", "razed", "damage_taken", "shield_absorbed", "deaths",
                                       "ore", "hydrogen", "built", "repaired", "healed", "carried", "burn", "blast_hits",
                                       "air_kills", "stomps", "stomp_hits", "strikes", "strike_hits", "hidden_seconds"}))
                 .text(g.id).text(kindName(k.kind)).real(t.seconds).integer(t.takeovers).real(k.xp).integer(k.level)
                 .real(t.unitDamage).real(t.buildingDamage).integer(t.kills).integer(t.razed).real(t.damageTaken)
                 .real(t.shieldAbsorbed).integer(t.deaths).integer(t.ore).integer(t.hydrogen).real(t.built)
                 .real(t.repaired).real(t.healed).integer(t.carried).real(t.burn).integer(t.blastHits)
                 .integer(t.airKills).integer(t.stomps).integer(t.stompHits).integer(t.strikes).integer(t.strikeHits)
                 .real(t.hiddenSeconds)
                 .run(error);
    }
    std::vector<std::string> levelColumns{"game", "kind", "level", "one", "other", "picked", "reached", "picked_at",
                                          "waited", "driven"};
    for (const std::string& c : standingColumns("own_")) levelColumns.push_back(c);
    for (const std::string& c : standingColumns("enemy_")) levelColumns.push_back(c);
    for (size_t i = 0; ok && i < g.levels.size(); i++) {
        const TrackedLevel& l = g.levels[i];
        ok = Stmt(db, insert("levels", levelColumns))
                 .text(g.id).text(kindName(l.kind)).integer(l.level).text(perkName(l.one)).text(perkName(l.other))
                 .text(l.picked ? std::optional<std::string>(perkName(*l.picked)) : std::nullopt)
                 .real(l.reached).real(l.pickedAt).real(l.waited).real(l.driven)
                 .standing(l.own).standing(l.enemy)
                 .run(error);
    }
    if (!ok) {
        exec("ROLLBACK", nullptr);
        return false;
    }
    return exec("COMMIT", error);
}

} // namespace ac
