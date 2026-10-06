// The tracking database (docs/leveling.md, "Tracking", "Where it goes"):
// a SQLite file with the tables and views of the leveling's balance
// numbers, and the upsert of a whole game. It speaks SQLite's C API, so
// the Unreal build links the engine's SQLiteCore and core-tests the
// system's SQLite; nothing else in the core knows either.
//
// One store is one connection: not thread-safe. The game keeps its store
// on one writer thread.
#pragma once

#include "Tracking.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct sqlite3;

namespace ac {

class TrackingStore {
public:
    /// This build's version of the tracking tables (`meta` key
    /// `version.tracking`). Each later step adds its own migration in
    /// `TrackingStore::migrate`.
    static constexpr int64_t version = 1;

    /// Opens (creating) the database at `path`, makes the tables and runs
    /// the migrations still due, and makes the views again. Null, with
    /// SQLite's message in `error`, when the file won't open or was made
    /// by a newer build.
    static std::unique_ptr<TrackingStore> open(const std::string& path, std::string* error = nullptr);

    ~TrackingStore();
    TrackingStore(const TrackingStore&) = delete;
    TrackingStore& operator=(const TrackingStore&) = delete;

    /// The random id of this install, made once.
    const std::string& installID() const { return install; }

    /// Upserts the whole game in one transaction: its `games` row, and its
    /// `players`, `kinds` and `levels` rows in place of the ones there.
    /// Writing the same game twice changes nothing. False, with SQLite's
    /// message in `error`, when it could not.
    bool write(const TrackedGame& game, std::string* error = nullptr);

    /// The rows of a query as text (NULL is ""), for tests and tools.
    std::vector<std::vector<std::string>> rows(const std::string& sql, const std::vector<std::string>& values = {},
                                               std::string* error = nullptr);

private:
    TrackingStore() = default;
    bool exec(const std::string& sql, std::string* error);
    bool migrate(std::string* error);

    sqlite3* db = nullptr;
    std::string install;
};

} // namespace ac
