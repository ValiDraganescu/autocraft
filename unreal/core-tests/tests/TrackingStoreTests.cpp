// The tracking record and its database (docs/leveling.md, "Tracking"): the
// rows a game makes from a hand-built state, the maths of the two numbers
// per pick, the store on a temporary database (upsert, the same game
// twice, the views on rows written by hand, migration). Hand-built states
// only; no game is played.
#include "LevelingScene.h"
#include "Session.h"
#include "Tracking.h"
#include "TrackingStore.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>

using namespace ac;
using namespace leveling;

namespace {

/// A database file in the temp folder that removes its files when done.
struct TempDB {
    std::string path;
    explicit TempDB(const char* name) {
        path = (std::filesystem::temp_directory_path() / ("autocraft-" + std::string(name) + "-" + std::to_string(getpid()) + ".sqlite")).string();
        wipe();
    }
    ~TempDB() { wipe(); }
    void wipe() const {
        for (const char* suffix : {"", "-wal", "-shm"}) std::filesystem::remove(path + suffix);
    }
};

Standing standing(int64_t seed) {
    Standing s;
    s.army = seed * 10 + 1;
    s.workers = seed * 10 + 2;
    s.ore = seed * 10 + 3;
    s.hydrogen = seed * 10 + 4;
    s.supplyUsed = seed * 10 + 5;
    s.supplyCap = seed * 10 + 6;
    s.buildings = seed * 10 + 7;
    s.mined = seed * 10 + 8;
    return s;
}

/// A tally with every field set to a different number.
Tally fullTally() {
    Tally t;
    t.seconds = 61.5; t.takeovers = 2; t.unitDamage = 300.25; t.buildingDamage = 40.5; t.kills = 5; t.razed = 1;
    t.damageTaken = 70.5; t.shieldAbsorbed = 12.5; t.deaths = 1; t.ore = 75; t.hydrogen = 40; t.built = 220.5;
    t.repaired = 18.5; t.healed = 9.5; t.carried = 3; t.burn = 24.5; t.blastHits = 4; t.airKills = 6; t.stomps = 7;
    t.stompHits = 8; t.strikes = 9; t.strikeHits = 10; t.hiddenSeconds = 11.5;
    return t;
}

/// A session for `state`, with a game id and a start.
Session sessionFor(const GameState& state) {
    Session s;
    s.id = "window";
    s.created = Date{1'000'000};
    s.mapName = "Highlands · Small";
    s.mapVersion = 3;
    s.state = state;
    s.game = "GAME-1";
    s.gameStarted = Date{1'000'100};
    return s;
}

/// The Ranger's levels 2 and 3 taken, level 4 reached and not taken; the
/// Peregrine driven with nothing earned.
GameState handBuilt() {
    GameState state = GameState::new_(smallMap());
    PilotRecord r;
    KindRecord ranger;
    ranger.xp = Leveling::xp(4) + 5;
    ranger.picks = {Perk::rangerDrillSergeant, Perk::rangerFlakVest};
    ranger.tally = fullTally();
    ranger.stamps = {Stamp{2, 100, 40, {standing(1), standing(2), standing(3)}, 130},
                     Stamp{3, 200, 90, {standing(4), standing(5), standing(6)}, 260},
                     Stamp{4, 300, 140, {standing(7), standing(8), standing(9)}, std::nullopt}};
    r.kinds[UnitKind::ranger] = ranger;
    r.kinds[UnitKind::peregrine] = KindRecord();
    state.players[0].pilot = r;
    state.time = 400;
    return state;
}

TrackedGame hand(const std::string& id, TrackedGame::Result result, std::optional<Perk> two, std::optional<Perk> three,
                 const std::string& signature = "SIG") {
    TrackedGame g;
    g.id = id;
    g.session = "window";
    g.started = Date{1000};
    g.ended = Date{2000};
    g.length = 600;
    g.result = result;
    g.map = "Highlands · Small";
    g.mapVersion = 3;
    g.signature = signature;
    g.build = "2026-10-06T08:00:00Z";
    g.os = "macOS 26";
    TrackedPlayer human;
    human.human = true;
    TrackedPlayer enemy;
    enemy.player = 1;
    g.players = {human, enemy};
    TrackedKind k;
    k.kind = UnitKind::ranger;
    k.tally.seconds = 100;
    g.kinds = {k};
    TrackedLevel l2;
    l2.kind = UnitKind::ranger;
    l2.level = 2;
    l2.one = Perk::rangerDrillSergeant;
    l2.other = Perk::rangerLightKit;
    l2.picked = two;
    g.levels = {l2};
    if (two) {
        TrackedLevel l3 = l2;
        l3.level = 3;
        l3.one = Perk::rangerCombatDrill;
        l3.other = Perk::rangerFlakVest;
        l3.picked = three;
        g.levels.push_back(l3);
    }
    return g;
}

std::string cell(TrackingStore& s, const std::string& sql, const std::vector<std::string>& values = {}) {
    std::string error;
    const auto rows = s.rows(sql, values, &error);
    if (!error.empty()) std::printf("  sql: %s\n", error.c_str());
    return rows.empty() ? "<none>" : rows[0][0];
}

} // namespace

// MARK: - The record

/// A game as it stands: the draw, the standing, the kinds with every tally
/// field, and a row for each level reached, with the stamps' times and
/// standings (the human's, and the others' added up).
TEST(TrackingStore_aGameMakesItsRows) {
    GameState state = handBuilt();
    // The game draws every AI's temperament; player 0 is set to an old
    // session's nils, which stand for the defaults.
    Player& old = state.players[0];
    old.greed = std::nullopt;
    old.garrisonPerBase = std::nullopt;
    old.comets = std::nullopt;
    old.fireflies = std::nullopt;
    old.drops = std::nullopt;
    old.start = std::nullopt;
    old.style = std::nullopt;
    state.players[1].unitsLost = 7;
    state.players[1].style = Player::Style::mech;
    state.players[1].greed = 0.75;
    state.players[1].garrisonPerBase = 3;
    state.players[1].comets = 2;
    state.players[1].fireflies = 5;
    state.players[1].drops = true;
    state.players[1].start = 1;
    const Session session = sessionFor(state);
    const TrackedGame g = TrackedGame::make(session, state, "build-1", "macOS 26", Date{5000});
    EXPECT_EQ(g.id, std::string("GAME-1"));
    EXPECT_EQ(g.session, std::string("window"));
    EXPECT_EQ(g.started.since1970, 1'000'100.0);
    EXPECT_EQ(g.map, std::string("Highlands · Small"));
    EXPECT_EQ(g.mapVersion, int64_t(3));
    EXPECT_TRUE(g.ai);
    EXPECT_EQ(g.signature, Leveling::signature());
    EXPECT_EQ(g.build, std::string("build-1"));
    EXPECT_EQ(g.os, std::string("macOS 26"));
    EXPECT_TRUE(g.result == TrackedGame::Result::open);
    EXPECT_EQ(g.length, 400.0);
    EXPECT_EQ(g.ended.since1970, 5000.0);

    ASSERT_TRUE(g.players.size() == 2);
    EXPECT_TRUE(g.players[0].human);
    EXPECT_TRUE(!g.players[1].human);
    EXPECT_EQ(g.players[0].style, std::string("bio")); // nils stand for their defaults
    EXPECT_EQ(g.players[0].garrisonPerBase, int64_t(2));
    EXPECT_EQ(g.players[0].comets, int64_t(1));
    EXPECT_EQ(g.players[0].fireflies, int64_t(0));
    EXPECT_TRUE(!g.players[0].drops);
    EXPECT_EQ(g.players[0].greed, 0.0);
    EXPECT_EQ(g.players[0].start, int64_t(0));
    EXPECT_EQ(g.players[1].style, std::string("mech"));
    EXPECT_EQ(g.players[1].greed, 0.75);
    EXPECT_EQ(g.players[1].garrisonPerBase, int64_t(3));
    EXPECT_EQ(g.players[1].comets, int64_t(2));
    EXPECT_EQ(g.players[1].fireflies, int64_t(5));
    EXPECT_TRUE(g.players[1].drops);
    EXPECT_EQ(g.players[1].start, int64_t(1));
    EXPECT_EQ(g.players[1].unitsLost, int64_t(7));
    EXPECT_TRUE(g.players[0].standing == state.standing(0));

    ASSERT_TRUE(g.kinds.size() == 2);
    EXPECT_TRUE(g.kinds[0].kind == UnitKind::ranger); // Unit.Kind order
    EXPECT_EQ(g.kinds[0].level, int64_t(4));
    EXPECT_EQ(g.kinds[0].xp, Leveling::xp(4) + 5);
    EXPECT_TRUE(g.kinds[0].tally == fullTally());
    EXPECT_TRUE(g.kinds[1].kind == UnitKind::peregrine);
    EXPECT_EQ(g.kinds[1].level, int64_t(1));

    ASSERT_TRUE(g.levels.size() == 3); // the Peregrine has none
    const TrackedLevel& two = g.levels[0];
    EXPECT_EQ(two.level, int64_t(2));
    EXPECT_TRUE(two.one == Perk::rangerDrillSergeant && two.other == Perk::rangerLightKit);
    EXPECT_TRUE(two.picked == std::optional<Perk>(Perk::rangerDrillSergeant));
    EXPECT_TRUE(two.reached == std::optional<double>(100));
    EXPECT_TRUE(two.pickedAt == std::optional<double>(130));
    EXPECT_TRUE(two.waited == std::optional<double>(30));
    EXPECT_TRUE(two.driven == std::optional<double>(40));
    EXPECT_TRUE(two.own == std::optional<Standing>(standing(1)));
    EXPECT_TRUE(two.enemy == std::optional<Standing>(standing(2) + standing(3)));
    // Level 3 was reached at 200 and picked at 260: first in line from 200.
    EXPECT_TRUE(g.levels[1].waited == std::optional<double>(60));
    // Level 4 was reached and not taken.
    EXPECT_EQ(g.levels[2].level, int64_t(4));
    EXPECT_TRUE(!g.levels[2].picked.has_value());
    EXPECT_TRUE(!g.levels[2].pickedAt.has_value());
    EXPECT_TRUE(!g.levels[2].waited.has_value());
    EXPECT_TRUE(g.levels[2].reached == std::optional<double>(300));
}

/// A wait counts from the later of the level being reached and the card
/// below being taken: a card that came up while another stood first in line.
TEST(TrackingStore_aWaitCountsFromWhenTheCardWasFirstInLine) {
    GameState state = GameState::new_(smallMap());
    KindRecord k;
    k.xp = Leveling::xp(3);
    k.picks = {Perk::rangerLightKit, Perk::rangerCombatDrill};
    k.stamps = {Stamp{2, 100, 10, {standing(1), standing(2)}, 150}, Stamp{3, 120, 20, {standing(1), standing(2)}, 190}};
    PilotRecord r;
    r.kinds[UnitKind::ranger] = k;
    state.players[0].pilot = r;
    const TrackedGame g = TrackedGame::make(sessionFor(state), state, "b", "o", Date{0});
    ASSERT_TRUE(g.levels.size() == 2);
    EXPECT_TRUE(g.levels[0].waited == std::optional<double>(50));
    EXPECT_TRUE(g.levels[1].waited == std::optional<double>(40)); // from 150, not from 120
}

/// A level with no stamp (set by hand, or an old session) has a row with
/// no times, driving or standings; a pick above the XP's level still counts.
TEST(TrackingStore_aLevelWithNoStampHasNoTimesOrStandings) {
    GameState state = GameState::new_(smallMap());
    KindRecord k;
    k.xp = Leveling::xp(2);
    k.picks = {Perk::rangerDrillSergeant};
    PilotRecord r;
    r.kinds[UnitKind::ranger] = k;
    state.players[0].pilot = r;
    const TrackedGame g = TrackedGame::make(sessionFor(state), state, "b", "o", Date{0});
    ASSERT_TRUE(g.levels.size() == 1);
    const TrackedLevel& l = g.levels[0];
    EXPECT_TRUE(l.picked == std::optional<Perk>(Perk::rangerDrillSergeant));
    EXPECT_TRUE(!l.reached && !l.pickedAt && !l.waited && !l.driven && !l.own && !l.enemy);
}

/// A game nobody drove has players and no kinds or levels; a session with
/// no AI says so.
TEST(TrackingStore_aGameNobodyDroveHasNoKindsOrLevels) {
    const GameState state = GameState::new_(smallMap());
    Session s = sessionFor(state);
    s.ai = false;
    const TrackedGame g = TrackedGame::make(s, state, "b", "o", Date{0});
    EXPECT_TRUE(g.kinds.empty() && g.levels.empty());
    EXPECT_TRUE(!g.ai);
    EXPECT_TRUE(g.players.size() == 2);
}

/// The result is the human team's: won or lost from the winner, drawn with
/// none, abandoned only when the caller says so and the game was not over;
/// the end is the write's time less the game seconds since it ended.
TEST(TrackingStore_theResultIsTheHumanTeamsAndTheEndIsWhenItEnded) {
    GameState state = GameState::new_(smallMap());
    state.time = 700;
    state.endedAt = 600;
    state.winner = 0;
    TrackedGame g = TrackedGame::make(sessionFor(state), state, "b", "o", Date{10'000});
    EXPECT_TRUE(g.result == TrackedGame::Result::won);
    EXPECT_TRUE(g.players[0].won && !g.players[1].won);
    EXPECT_EQ(g.length, 600.0);
    EXPECT_EQ(g.ended.since1970, 9900.0);
    // Writing it again later (the same end) is the caller's `now`; the end moves
    // only with the clock, as docs/leveling.md says.
    g = TrackedGame::make(sessionFor(state), state, "b", "o", Date{10'000}, true);
    EXPECT_TRUE(g.result == TrackedGame::Result::won); // abandoned only when it was not over

    state.winner = 1;
    g = TrackedGame::make(sessionFor(state), state, "b", "o", Date{10'000});
    EXPECT_TRUE(g.result == TrackedGame::Result::lost);
    EXPECT_TRUE(!g.players[0].won && g.players[1].won);

    state.winner = std::nullopt;
    g = TrackedGame::make(sessionFor(state), state, "b", "o", Date{10'000});
    EXPECT_TRUE(g.result == TrackedGame::Result::drawn);
    EXPECT_TRUE(!g.players[0].won && !g.players[1].won);

    state.endedAt = std::nullopt;
    g = TrackedGame::make(sessionFor(state), state, "b", "o", Date{10'000}, true);
    EXPECT_TRUE(g.result == TrackedGame::Result::abandoned);
    EXPECT_EQ(g.length, 700.0);
    EXPECT_EQ(g.ended.since1970, 10'000.0);
}

/// Allies of the human win and lose with it (teams).
TEST(TrackingStore_alliesShareTheHumanTeamsResult) {
    GameState state = GameState::new_(smallMap());
    state.players.push_back(Player());
    state.players.push_back(Player());
    state.players[0].team = 0;
    state.players[1].team = 1;
    state.players[2].team = 0;
    state.players[3].team = 1;
    state.endedAt = 10;
    state.time = 12;
    state.winner = 2;
    const TrackedGame g = TrackedGame::make(sessionFor(state), state, "b", "o", Date{0});
    EXPECT_TRUE(g.result == TrackedGame::Result::won);
    EXPECT_TRUE(g.players[0].won && !g.players[1].won && g.players[2].won && !g.players[3].won);
}

/// The balance signature is `Leveling::signature()`: 16 hex digits.
TEST(TrackingStore_theSignatureIsTheBalances) {
    const GameState state = GameState::new_(smallMap());
    const TrackedGame g = TrackedGame::make(sessionFor(state), state, "b", "o", Date{0});
    EXPECT_EQ(g.signature, Leveling::signature());
    EXPECT_EQ(g.signature.size(), size_t(16));
}

// MARK: - The worth of a pick

TEST(TrackingStore_theTwoNumbersOfAPick) {
    EXPECT_EQ(worth::pickRate(3, 5), 0.6);
    EXPECT_EQ(worth::pickRate(0, 0), 0.0);
    EXPECT_EQ(worth::winRate(2, 3), 2.0 / 3);
    EXPECT_EQ(worth::winRate(0, 0), 0.0);
    // 1.96 × √(p(1 − p)/n): p = 0.6, n = 100 is about 9.6 points.
    EXPECT_NEAR(worth::margin(0.6, 100), 0.0960, 0.0001);
    EXPECT_EQ(worth::margin(0.5, 0), 0.0);
    EXPECT_EQ(worth::margin(1.0, 10), 0.0);
    EXPECT_NEAR(worth::gap(0.6, 0.75), -0.15, 1e-12);
}

// MARK: - The database

/// A game written and read back, every column; the install id.
TEST(TrackingStore_aGameIsWrittenWithEveryColumn) {
    TempDB file("rows");
    std::string error;
    auto store = TrackingStore::open(file.path, &error);
    ASSERT_TRUE(store != nullptr);
    EXPECT_EQ(store->installID().size(), size_t(36));
    GameState state = handBuilt();
    state.players[1].style = Player::Style::harass;
    const TrackedGame g = TrackedGame::make(sessionFor(state), state, "build-1", "macOS 26", Date{1'700'000'000});
    EXPECT_TRUE(store->write(g, &error));

    EXPECT_EQ(cell(*store, "SELECT game || '|' || install || '|' || session || '|' || started || '|' || ended || '|' || length || '|' || result || '|' || map || '|' || map_version || '|' || ai || '|' || build || '|' || os FROM games"),
              "GAME-1|" + store->installID() + "|window|1970-01-12T13:48:20Z|2023-11-14T22:13:20Z|400.0|open|Highlands · Small|3|1|build-1|macOS 26");
    EXPECT_EQ(cell(*store, "SELECT signature FROM games"), Leveling::signature());
    EXPECT_EQ(cell(*store, "SELECT COUNT(*) FROM players"), "2");
    EXPECT_EQ(cell(*store, "SELECT style FROM players WHERE player = 1"), "harass");
    EXPECT_EQ(cell(*store, "SELECT human FROM players WHERE player = 0"), "1");
    EXPECT_EQ(cell(*store, "SELECT army FROM players WHERE player = 0"), std::to_string(state.standing(0).army));
    EXPECT_EQ(cell(*store, "SELECT supply_cap FROM players WHERE player = 0"), std::to_string(state.players[0].supplyCap));

    // Every Tally field, in its column.
    EXPECT_EQ(cell(*store, "SELECT seconds || ' ' || takeovers || ' ' || unit_damage || ' ' || building_damage || ' ' || kills || ' ' || razed || ' ' || damage_taken || ' ' || shield_absorbed || ' ' || deaths || ' ' || ore || ' ' || hydrogen || ' ' || built || ' ' || repaired || ' ' || healed || ' ' || carried || ' ' || burn || ' ' || blast_hits FROM kinds WHERE kind = 'ranger'"),
              "61.5 2 300.25 40.5 5 1 70.5 12.5 1 75 40 220.5 18.5 9.5 3 24.5 4");
    EXPECT_EQ(cell(*store, "SELECT air_kills || ' ' || stomps || ' ' || stomp_hits || ' ' || strikes || ' ' || strike_hits || ' ' || hidden_seconds || ' ' || level || ' ' || xp FROM kinds WHERE kind = 'ranger'"),
              "6 7 8 9 10 11.5 4 " + std::to_string(int64_t(Leveling::xp(4) + 5)) + ".0");
    EXPECT_EQ(cell(*store, "SELECT COUNT(*) FROM kinds"), "2");

    EXPECT_EQ(cell(*store, "SELECT one || ' ' || other || ' ' || picked || ' ' || reached || ' ' || picked_at || ' ' || waited || ' ' || driven FROM levels WHERE level = 2"),
              "rangerDrillSergeant rangerLightKit rangerDrillSergeant 100.0 130.0 30.0 40.0");
    EXPECT_EQ(cell(*store, "SELECT own_army || ' ' || own_mined || ' ' || enemy_army || ' ' || enemy_workers || ' ' || enemy_mined FROM levels WHERE level = 2"),
              "11 18 " + std::to_string(21 + 31) + " " + std::to_string(22 + 32) + " " + std::to_string(28 + 38));
    EXPECT_EQ(cell(*store, "SELECT COUNT(*) FROM levels WHERE picked IS NULL"), "1");
    EXPECT_EQ(cell(*store, "SELECT own_army IS NULL FROM levels WHERE level = 4"), "0");
}

/// Writing the same game twice changes nothing; a later write replaces the
/// game's rows (the result, and rows that are gone), and leaves other games.
TEST(TrackingStore_theSameGameTwiceChangesNothingAndALaterWriteReplacesIt) {
    TempDB file("upsert");
    auto store = TrackingStore::open(file.path);
    ASSERT_TRUE(store != nullptr);
    const TrackedGame a = hand("A", TrackedGame::Result::open, Perk::rangerDrillSergeant, Perk::rangerCombatDrill);
    const TrackedGame b = hand("B", TrackedGame::Result::won, Perk::rangerLightKit, std::nullopt);
    EXPECT_TRUE(store->write(a));
    EXPECT_TRUE(store->write(b));
    const auto before = store->rows("SELECT * FROM levels ORDER BY game, level");
    const auto games = store->rows("SELECT * FROM games ORDER BY game");
    EXPECT_TRUE(store->write(a));
    EXPECT_TRUE(store->rows("SELECT * FROM levels ORDER BY game, level") == before);
    EXPECT_TRUE(store->rows("SELECT * FROM games ORDER BY game") == games);
    EXPECT_EQ(cell(*store, "SELECT COUNT(*) FROM levels"), "4");

    TrackedGame later = a;
    later.result = TrackedGame::Result::lost;
    later.levels.pop_back(); // a row that is gone
    later.kinds[0].tally.kills = 9;
    EXPECT_TRUE(store->write(later));
    EXPECT_EQ(cell(*store, "SELECT result FROM games WHERE game = 'A'"), "lost");
    EXPECT_EQ(cell(*store, "SELECT COUNT(*) FROM levels WHERE game = 'A'"), "1");
    EXPECT_EQ(cell(*store, "SELECT kills FROM kinds WHERE game = 'A'"), "9");
    EXPECT_EQ(cell(*store, "SELECT COUNT(*) FROM games"), "2");
    EXPECT_EQ(cell(*store, "SELECT COUNT(*) FROM levels WHERE game = 'B'"), "2");
}

/// The file is in WAL mode, and a reopened file keeps its games and its
/// install id.
TEST(TrackingStore_theFileIsWalAndKeepsItsInstallID) {
    TempDB file("reopen");
    std::string id;
    {
        auto store = TrackingStore::open(file.path);
        ASSERT_TRUE(store != nullptr);
        EXPECT_EQ(cell(*store, "PRAGMA journal_mode"), "wal");
        id = store->installID();
        EXPECT_TRUE(store->write(hand("A", TrackedGame::Result::won, Perk::rangerDrillSergeant, std::nullopt)));
    }
    auto store = TrackingStore::open(file.path);
    ASSERT_TRUE(store != nullptr);
    EXPECT_EQ(store->installID(), id);
    EXPECT_EQ(cell(*store, "SELECT COUNT(*) FROM games"), "1");
    EXPECT_EQ(cell(*store, "SELECT value FROM meta WHERE key = 'version.tracking'"), std::to_string(TrackingStore::version));
    EXPECT_TRUE(store->write(hand("A", TrackedGame::Result::won, Perk::rangerDrillSergeant, std::nullopt)));
}

/// A file below this build's version gets the tables made again (here: two
/// gone and no version row, another set's `meta` rows kept); a file made by
/// a newer build is not opened, and is left as it was.
TEST(TrackingStore_migrationMakesTheTablesAndRefusesANewerFile) {
    TempDB file("migrate");
    std::string id;
    {
        auto store = TrackingStore::open(file.path);
        ASSERT_TRUE(store != nullptr);
        id = store->installID();
        store->rows("DROP TABLE levels");
        store->rows("DROP TABLE kinds");
        store->rows("DELETE FROM meta WHERE key = 'version.tracking'");
        store->rows("INSERT INTO meta (key, value) VALUES ('version.perf', '4')");
    }
    auto again = TrackingStore::open(file.path);
    ASSERT_TRUE(again != nullptr);
    EXPECT_EQ(cell(*again, "SELECT value FROM meta WHERE key = 'version.tracking'"), "1");
    EXPECT_EQ(cell(*again, "SELECT value FROM meta WHERE key = 'version.perf'"), "4"); // another set's rows are left alone
    EXPECT_EQ(again->installID(), id);
    EXPECT_TRUE(again->write(hand("A", TrackedGame::Result::won, Perk::rangerDrillSergeant, std::nullopt))); // the tables are back
    EXPECT_EQ(cell(*again, "SELECT COUNT(*) FROM levels"), "2");
    again->rows("UPDATE meta SET value = '99' WHERE key = 'version.tracking'");
    again.reset();
    std::string error;
    EXPECT_TRUE(TrackingStore::open(file.path, &error) == nullptr);
    EXPECT_TRUE(error.find("99") != std::string::npos);
}

/// A file that is no database does not open, and says why.
TEST(TrackingStore_aFileThatIsNoDatabaseDoesNotOpen) {
    TempDB file("junk");
    FILE* f = std::fopen(file.path.c_str(), "w");
    ASSERT_TRUE(f != nullptr);
    std::fputs("this is not a sqlite database, just a long enough line of text to fail the header check", f);
    std::fclose(f);
    std::string error;
    EXPECT_TRUE(TrackingStore::open(file.path, &error) == nullptr);
    EXPECT_TRUE(!error.empty());
}

// MARK: - The views, on rows written by hand

/// `perk_balance` on six games: two cards of level 2, picked and won by
/// hand, so the counts and the rates are known. An old balance's game is
/// a separate set of rows.
TEST(TrackingStore_perkBalanceWorksOutThePickAndWinRates) {
    TempDB file("balance");
    auto store = TrackingStore::open(file.path);
    ASSERT_TRUE(store != nullptr);
    const Perk drill = Perk::rangerDrillSergeant, kit = Perk::rangerLightKit;
    for (const TrackedGame& g : {
             hand("A", TrackedGame::Result::won, drill, Perk::rangerCombatDrill),
             hand("B", TrackedGame::Result::lost, drill, Perk::rangerCombatDrill),
             hand("C", TrackedGame::Result::won, drill, Perk::rangerFlakVest),
             hand("D", TrackedGame::Result::won, kit, Perk::rangerFlakVest),
             hand("E", TrackedGame::Result::open, kit, std::nullopt),
             hand("F", TrackedGame::Result::won, std::nullopt, std::nullopt), // reached level 2, took nothing
             hand("G", TrackedGame::Result::drawn, kit, std::nullopt),
             hand("OLD", TrackedGame::Result::won, kit, std::nullopt, "OLD")})
        EXPECT_TRUE(store->write(g));

    auto row = [&](const char* perk) {
        return store->rows("SELECT offered, took, picked, pick_rate, games, wins, win_rate, margin, other_games, other_wins, other_win_rate, gap, other "
                           "FROM perk_balance WHERE signature = 'SIG' AND perk = ?", {perk});
    };
    const auto d = row("rangerDrillSergeant");
    ASSERT_TRUE(d.size() == 1);
    EXPECT_EQ(d[0][0], "7"); // on offer in every game of the signature that reached level 2
    EXPECT_EQ(d[0][1], "6"); // all but F took a card
    EXPECT_EQ(d[0][2], "3");
    EXPECT_EQ(d[0][4], "3"); // finished games it was picked in
    EXPECT_EQ(d[0][5], "2");
    EXPECT_NEAR(std::atof(d[0][3].c_str()), worth::pickRate(3, 6), 1e-12);
    EXPECT_NEAR(std::atof(d[0][6].c_str()), worth::winRate(2, 3), 1e-12);
    EXPECT_NEAR(std::atof(d[0][7].c_str()), worth::margin(worth::winRate(2, 3), 3), 1e-12);
    EXPECT_EQ(d[0][8], "2"); // the other card: D and G finished (E is open)
    EXPECT_EQ(d[0][9], "1"); // a draw is not a win
    EXPECT_NEAR(std::atof(d[0][10].c_str()), 0.5, 1e-12);
    EXPECT_NEAR(std::atof(d[0][11].c_str()), worth::gap(worth::winRate(2, 3), 0.5), 1e-12);
    EXPECT_EQ(d[0][12], "rangerLightKit");

    const auto k = row("rangerLightKit");
    ASSERT_TRUE(k.size() == 1);
    EXPECT_EQ(k[0][2], "3"); // D, E, G (OLD is another signature)
    EXPECT_EQ(k[0][4], "2");
    EXPECT_EQ(k[0][5], "1");
    EXPECT_NEAR(std::atof(k[0][6].c_str()), 0.5, 1e-12);
    EXPECT_NEAR(std::atof(k[0][11].c_str()), worth::gap(0.5, worth::winRate(2, 3)), 1e-12);

    // The other signature stands on its own.
    EXPECT_EQ(cell(*store, "SELECT picked || ' ' || games || ' ' || wins FROM perk_balance WHERE signature = 'OLD' AND perk = 'rangerLightKit'"), "1 1 1");
    // Level 3: compared inside its own pair.
    EXPECT_EQ(cell(*store, "SELECT picked || ' ' || games || ' ' || wins FROM perk_balance WHERE signature = 'SIG' AND perk = 'rangerFlakVest'"), "2 2 2");
    EXPECT_EQ(cell(*store, "SELECT picked || ' ' || games || ' ' || wins || ' ' || took FROM perk_balance WHERE signature = 'SIG' AND perk = 'rangerCombatDrill'"), "2 2 1 4");
}

/// `level_funnel`: the share of the games a kind was driven in that
/// reached each level, zero where none did.
TEST(TrackingStore_levelFunnelCountsTheGamesThatReachedEachLevel) {
    TempDB file("funnel");
    auto store = TrackingStore::open(file.path);
    ASSERT_TRUE(store != nullptr);
    for (const TrackedGame& g : {hand("A", TrackedGame::Result::won, Perk::rangerDrillSergeant, Perk::rangerCombatDrill),
                                 hand("B", TrackedGame::Result::lost, Perk::rangerDrillSergeant, std::nullopt),
                                 hand("C", TrackedGame::Result::won, std::nullopt, std::nullopt),
                                 hand("D", TrackedGame::Result::won, Perk::rangerLightKit, std::nullopt)})
        EXPECT_TRUE(store->write(g));
    auto at = [&](int level) {
        return cell(*store, "SELECT driven || ' ' || reached || ' ' || share FROM level_funnel WHERE signature = 'SIG' AND kind = 'ranger' AND level = " + std::to_string(level));
    };
    EXPECT_EQ(at(2), "4 4 1.0");
    EXPECT_EQ(at(3), "4 3 0.75"); // A, B and D picked at level 2, so they reached 3; C took nothing
}

/// `pick_paths`: each game's picks in level order, the games that took the
/// run and its win rate (finished games only).
TEST(TrackingStore_pickPathsListEachRunOfPicks) {
    TempDB file("paths");
    auto store = TrackingStore::open(file.path);
    ASSERT_TRUE(store != nullptr);
    for (const TrackedGame& g : {hand("A", TrackedGame::Result::won, Perk::rangerDrillSergeant, Perk::rangerCombatDrill),
                                 hand("B", TrackedGame::Result::lost, Perk::rangerDrillSergeant, Perk::rangerCombatDrill),
                                 hand("C", TrackedGame::Result::open, Perk::rangerDrillSergeant, Perk::rangerCombatDrill),
                                 hand("D", TrackedGame::Result::won, Perk::rangerLightKit, Perk::rangerFlakVest)})
        EXPECT_TRUE(store->write(g));
    EXPECT_EQ(cell(*store, "SELECT games || ' ' || finished || ' ' || wins || ' ' || win_rate FROM pick_paths WHERE path = 'rangerDrillSergeant > rangerCombatDrill'"), "3 2 1 0.5");
    EXPECT_EQ(cell(*store, "SELECT games || ' ' || finished || ' ' || wins || ' ' || win_rate FROM pick_paths WHERE path = 'rangerLightKit > rangerFlakVest'"), "1 1 1 1.0");
    EXPECT_EQ(cell(*store, "SELECT COUNT(*) FROM pick_paths"), "2");
}

/// `drive_share`: each kind's share of the seconds driven.
TEST(TrackingStore_driveShareSplitsTheSecondsDriven) {
    TempDB file("share");
    auto store = TrackingStore::open(file.path);
    ASSERT_TRUE(store != nullptr);
    TrackedGame a = hand("A", TrackedGame::Result::won, std::nullopt, std::nullopt);
    TrackedKind prospector;
    prospector.kind = UnitKind::prospector;
    prospector.tally.seconds = 300;
    a.kinds.push_back(prospector); // ranger 100, prospector 300
    TrackedGame b = hand("B", TrackedGame::Result::lost, std::nullopt, std::nullopt);
    b.kinds[0].tally.seconds = 200; // ranger 200
    EXPECT_TRUE(store->write(a));
    EXPECT_TRUE(store->write(b));
    EXPECT_EQ(cell(*store, "SELECT seconds || ' ' || share FROM drive_share WHERE kind = 'ranger'"), "300.0 0.5");
    EXPECT_EQ(cell(*store, "SELECT seconds || ' ' || share FROM drive_share WHERE kind = 'prospector'"), "300.0 0.5");
}

/// The views are made again each time the file opens: one a build left
/// behind with another body is replaced.
TEST(TrackingStore_theViewsAreMadeAgainOnOpen) {
    TempDB file("views");
    {
        auto store = TrackingStore::open(file.path);
        ASSERT_TRUE(store != nullptr);
        store->rows("DROP VIEW drive_share");
        store->rows("CREATE VIEW drive_share AS SELECT 1 AS stale");
    }
    auto store = TrackingStore::open(file.path);
    ASSERT_TRUE(store != nullptr);
    EXPECT_EQ(cell(*store, "SELECT COUNT(*) FROM pragma_table_info('drive_share') WHERE name = 'share'"), "1");
}
