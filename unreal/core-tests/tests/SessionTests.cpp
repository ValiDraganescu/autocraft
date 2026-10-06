// Saved games: every Swift fixture loads and saves back to the same JSON,
// a session with every field set round-trips, GameState::new_ and
// BaseSite::standard match Swift (golden/states.json), and the tolerant
// decoders give Swift's defaults.
#include "golden.h"
#include "test.h"

#include "Leveling.h"
#include "Session.h"

#include <filesystem>

using golden::json;

namespace {

/// Every session JSON among the fixtures: bench/*.json, unreal/core-tests/fixtures/*.json.
std::vector<std::string> fixtures() {
    std::vector<std::string> out;
    for (const char* dir : {"bench", "unreal/core-tests/fixtures"}) {
        std::error_code ec;
        std::filesystem::directory_iterator it(golden::repo(dir), ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            const auto p = it->path();
            if (p.extension() != ".json") continue;
            const json j = golden::load(p.string());
            if (j.is_object() && j.contains("state") && j.contains("mapName")) out.push_back(p.string());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

/// Load `file` as a session, save it, and compare the two JSON trees.
void roundTrip(const std::string& file) {
    std::string error;
    const auto s = ac::SessionStore::load(file, &error);
    if (!s) std::printf("  %s: %s\n", file.c_str(), error.c_str());
    ASSERT_TRUE(s.has_value());
    const json in = golden::load(file);
    const std::string text = ac::SessionStore::encode(*s);
    const json back = json::parse(text, nullptr, false);
    ASSERT_TRUE(!back.is_discarded());
    std::string where;
    const bool equal = golden::same(in, back, where);
    if (!equal) std::printf("  %s differs at %s\n", file.c_str(), where.c_str());
    EXPECT_TRUE(equal);
    // And what was saved loads back to the same session.
    const auto again = ac::SessionStore::decode<ac::Session>(text);
    ASSERT_TRUE(again.has_value());
    EXPECT_TRUE(ac::SessionStore::encode(*again) == text);
}

} // namespace

TEST(session_fixtures_round_trip) {
    const auto files = fixtures();
    EXPECT_TRUE(files.size() >= 4);
    for (const auto& f : files) roundTrip(f);
}

TEST(session_with_every_field_round_trips) {
    roundTrip(golden::path("session-full.json"));
    const auto s = ac::SessionStore::load(golden::path("session-full.json"));
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->created.since1970, 1790000000.0);
    EXPECT_EQ(s->gameStarted->since1970, 1790000100.0);
    EXPECT_EQ(s->listener->facing.value_or(0), 1.25);
    const auto& intel = (*s->state.intel)[0];
    EXPECT_TRUE(std::isinf(intel.sites[0]) && intel.sites[0] < 0);
    EXPECT_TRUE(std::isinf(intel.sites[2]) && intel.sites[2] > 0);
    EXPECT_EQ(intel.explored[1], UINT64_MAX);
    EXPECT_TRUE(intel.isExplored(64) && !intel.isExplored(0) && !intel.isExplored(-1) && !intel.isExplored(1 << 20));
    const auto& p = s->state.players[0];
    EXPECT_EQ(p.upgrades->size(), size_t(3));
    EXPECT_TRUE(p.directives->queue[1].what.is<ac::Request::What::Building>());
    EXPECT_EQ(p.directives->stance, ac::Stance::allIn);
    EXPECT_EQ(p.directives->hydrogenWanted(), int64_t(3 * 0 + 100 + 100));
    EXPECT_EQ(p.pilot->kinds.at(ac::UnitKind::ranger).picks.size(), size_t(1));
    EXPECT_EQ(p.pilot->level(ac::UnitKind::ranger), int64_t(2));
    EXPECT_EQ(p.pilot->driven().size(), size_t(2));
    int64_t missions = 0;
    for (const auto& u : s->state.units) if (u.mission) missions += 1;
    EXPECT_EQ(missions, int64_t(7));
    const auto& j = s->state.units.back();
    EXPECT_TRUE(std::isnan(*j.slowedTo));
    EXPECT_EQ(j.burning->by, int64_t(12));
    // The Garrison in the save: two in its line, the Ranger in training.
    const auto& g = s->state.structures.back();
    EXPECT_EQ(g.queueCount(), int64_t(2));
    EXPECT_TRUE(g.inTraining() == ac::UnitKind::ranger);
    EXPECT_EQ(*g.trainingProgress(), 1 - 4.5 / 18);
    EXPECT_EQ(g.progress(), 1 - 12.5 / 46);
}

TEST(game_state_new_matches_swift) {
    const json g = golden::load(golden::path("states.json"));
    ASSERT_TRUE(!g.is_discarded());
    const auto map = ac::SessionStore::decode<ac::MapDefinition>(g["map"].dump());
    ASSERT_TRUE(map.has_value());
    // The map's bases are BaseSite::standard's.
    ASSERT_TRUE(g["bases"].size() == map->bases.size());
    for (size_t i = 0; i < map->bases.size(); i++) {
        const auto& a = g["bases"][i];
        const auto site = ac::BaseSite::standard(ac::Vec2(a["center"][0].get<double>(), a["center"][1].get<double>()),
                                                 a["level"].get<int64_t>(), a["facing"].get<double>());
        EXPECT_TRUE(site == map->bases[i]);
    }
    const ac::Vec2 front = map->front();
    EXPECT_EQ(front.x, g["front"][0].get<double>());
    EXPECT_EQ(front.y, g["front"][1].get<double>());
    std::string where;
    auto compare = [&](const ac::GameState& s, const char* key) {
        const json mine = json::parse(ac::SessionStore::encode(s), nullptr, false);
        const bool equal = golden::same(g[key], mine, where);
        if (!equal) std::printf("  %s differs at %s\n", key, where.c_str());
        EXPECT_TRUE(equal);
    };
    const auto fresh = ac::GameState::new_(*map);
    compare(fresh, "fresh");
    compare(ac::GameState::new_(*map, std::vector<int64_t>{2, 1}, 1), "scored");
    auto ground = *map;
    ground.playground = true;
    compare(ac::GameState::new_(ground), "playground");
    for (size_t p = 0; p < 2; p++) {
        const auto want = g["standing"][p];
        const ac::Standing s = fresh.standing(static_cast<int64_t>(p));
        EXPECT_EQ(s.workers, want["workers"].get<int64_t>());
        EXPECT_EQ(s.buildings, want["buildings"].get<int64_t>());
        EXPECT_EQ(s.ore, want["ore"].get<int64_t>());
        EXPECT_EQ(s.supplyCap, want["supplyCap"].get<int64_t>());
    }
    const auto broken = fresh.mirrorBreak(*map);
    EXPECT_EQ(broken.has_value(), g.contains("mirrorBreak"));
    // The map itself saves back as Swift wrote it.
    const json mapBack = json::parse(ac::SessionStore::encode(*map), nullptr, false);
    EXPECT_TRUE(golden::same(g["map"], mapBack, where));
}

TEST(decoders_give_swift_defaults) {
    // A record from a later build: missing fields read as defaults, an
    // unknown pick ends the list.
    const auto k = ac::SessionStore::decode<ac::KindRecord>(
        R"({"xp": 70, "picks": ["prospectorQuickDrill", "gone", "prospectorPlating"]})");
    ASSERT_TRUE(k.has_value());
    EXPECT_EQ(k->xp, 70.0);
    EXPECT_EQ(k->picks.size(), size_t(1));
    EXPECT_TRUE(k->picks[0] == ac::Perk::prospectorQuickDrill);
    EXPECT_EQ(k->count, int64_t(0));
    EXPECT_TRUE(k->tally == ac::Tally());
    // Directives a saved game does not have yet take their defaults.
    const auto d = ac::SessionStore::decode<ac::Directives>(R"({"keepOre": 75})");
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->keepOre, int64_t(75));
    EXPECT_TRUE(d->harvest == ac::Harvest::balanced);
    EXPECT_TRUE(d->stance == ac::Stance::auto_);
    EXPECT_EQ(d->nextID, int64_t(1));
    const auto t = ac::SessionStore::decode<ac::Tally>(R"({"kills": 3, "burn": 1.5})");
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->kills, int64_t(3));
    EXPECT_EQ(t->burn, 1.5);
    EXPECT_EQ(t->seconds, 0.0);
    // A field Swift requires is required: a unit with no hp does not load.
    std::string error;
    const auto u = ac::SessionStore::decode<ac::Unit>(
        R"({"id":1,"kind":"ranger","owner":0,"position":[0,0],"heading":0,"task":"idle","timer":0,"carrying":0,"stride":0})",
        &error);
    EXPECT_TRUE(!u.has_value());
    EXPECT_TRUE(error.find("hp") != std::string::npos);
    // An unknown kind, or a mission with two cases, does not load either.
    EXPECT_TRUE(!ac::SessionStore::decode<ac::Unit>(
        R"({"id":1,"kind":"titan","owner":0,"position":[0,0],"hp":1,"heading":0,"task":"idle","timer":0,"carrying":0,"stride":0})"));
    EXPECT_TRUE(!ac::SessionStore::decode<ac::Unit>(
        R"({"id":1,"kind":"ranger","owner":0,"position":[0,0],"hp":1,"heading":0,"task":"idle","timer":0,"carrying":0,"stride":0,
            "mission":{"raid":{"_0":[1,2]},"drop":{"_0":[1,2]}}})"));
    // Not JSON at all.
    EXPECT_TRUE(!ac::SessionStore::decode<ac::Session>("{not json"));
}

TEST(session_store_saves_and_opens) {
    const std::string dir = golden::repo("unreal/core-tests/build/sessions");
    const ac::SessionStore store(dir);
    const auto map = ac::SessionStore::decode<ac::MapDefinition>(golden::load(golden::path("states.json"))["map"].dump());
    ASSERT_TRUE(map.has_value());
    auto first = store.open("store-test", "sig", *map);
    EXPECT_TRUE(first.session.game.has_value());
    first.session.listener = ac::Listener{.position = ac::Vec2(1, 2)};
    first.session.ai = false;
    ASSERT_TRUE(store.save(first.session));
    const auto again = store.open("store-test", "sig", *map);
    EXPECT_TRUE(again.resumed);
    // Dates are saved to the second, as Swift's `.iso8601` does.
    EXPECT_TRUE(ac::SessionStore::encode(again.session) == ac::SessionStore::encode(first.session));
    EXPECT_TRUE(again.session.state == first.session.state);
    // A map whose version moved on starts over, keeping the settings.
    auto moved = *map;
    moved.version += 1;
    const auto fresh = store.open("store-test", "sig", moved);
    EXPECT_TRUE(!fresh.resumed);
    EXPECT_TRUE(fresh.session.listener == first.session.listener);
    EXPECT_TRUE(fresh.session.ai == std::optional<bool>(false));
}
