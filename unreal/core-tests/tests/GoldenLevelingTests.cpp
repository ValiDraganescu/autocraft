// The leveling catalogue against what the Swift game computes
// (Tests/GameCoreTests/GoldenLevelingTests.swift writes golden/leveling.json):
// the balance text and its signature, every pick's info and effects, the XP
// curve. Plus the pure tests of LevelingTests.swift, LevelingKindsTests.swift
// and TrackingTests.swift (the curve, the catalogue, the signature).
#ifndef JSON_NOEXCEPTION
#define JSON_NOEXCEPTION 1
#endif

#include "Leveling.h"
#include "json.hpp"
#include "test.h"

#include <cctype>
#include <fstream>
#include <map>
#include <sstream>

using namespace ac;

namespace {

nlohmann::json golden() {
    std::ifstream in(std::string(AC_REPO_ROOT) + "/unreal/core-tests/golden/leveling.json");
    std::stringstream text;
    text << in.rdbuf();
    return nlohmann::json::parse(text.str(), nullptr, false);
}

} // namespace

TEST(GoldenLeveling_signatureAndBalance) {
    const auto g = golden();
    ASSERT_TRUE(g.is_object());
    EXPECT_EQ(Leveling::signature(), g["signature"].get<std::string>());
    EXPECT_TRUE(Leveling::balance() == g["balance"].get<std::string>());
}

TEST(GoldenLeveling_everyPerk) {
    const auto g = golden();
    ASSERT_TRUE(g.is_object());
    const auto& perks = g["perks"];
    ASSERT_TRUE(perks.size() == allCases<Perk>().size());
    for (size_t i = 0; i < perks.size(); i++) {
        const Perk p = allCases<Perk>()[i];
        const auto& row = perks[i];
        const PerkInfo& in = info(p);
        EXPECT_EQ(std::string(rawValue(p)), row["perk"].get<std::string>());
        EXPECT_EQ(std::string(rawValue(in.kind)), row["kind"].get<std::string>());
        EXPECT_EQ(in.level, row["level"].get<int64_t>());
        EXPECT_EQ(std::string(rawValue(in.slot)), row["slot"].get<std::string>());
        EXPECT_EQ(in.title, row["title"].get<std::string>());
        EXPECT_EQ(in.effect, row["effect"].get<std::string>());
        EXPECT_EQ(describe(effects(p)), row["effects"].get<std::string>());
    }
}

TEST(GoldenLeveling_xpCurve) {
    const auto g = golden();
    ASSERT_TRUE(g.is_object());
    const auto& reach = g["xpToReach"];
    for (size_t l = 0; l < reach.size(); l++) EXPECT_EQ(Leveling::xp(static_cast<int64_t>(l)), reach[l].get<double>());
    for (const auto& row : g["levels"]) EXPECT_EQ(Leveling::level(row["xp"].get<double>()), row["level"].get<int64_t>());
}

TEST(GoldenLeveling_swiftDescription) {
    EXPECT_EQ(swiftDescription(1), std::string("1.0"));
    EXPECT_EQ(swiftDescription(0.1), std::string("0.1"));
    EXPECT_EQ(swiftDescription(2.0 / 3), std::string("0.6666666666666666"));
    EXPECT_EQ(swiftDescription(10.0 / 7), std::string("1.4285714285714286"));
    EXPECT_EQ(swiftDescription(-0.25), std::string("-0.25"));
    EXPECT_EQ(swiftDescription(2700), std::string("2700.0"));
    EXPECT_EQ(swiftDescription(std::numeric_limits<double>::infinity()), std::string("inf"));
    EXPECT_EQ(swiftDescription(1e-5), std::string("1e-05"));
    EXPECT_EQ(swiftDescription(0.0001), std::string("0.0001"));
    EXPECT_EQ(swiftDescription(1e16), std::string("1e+16"));
    EXPECT_EQ(swiftDescription(1234567890123456.0), std::string("1234567890123456.0"));
}

// MARK: - LevelingTests.swift

TEST(Leveling_theLevelCurveAndItsCap) {
    EXPECT_EQ(Leveling::xp(1), 0.0);
    EXPECT_EQ(Leveling::xp(2), 60.0);
    EXPECT_EQ(Leveling::xp(3), 180.0);
    EXPECT_EQ(Leveling::xp(5), 600.0);
    EXPECT_EQ(Leveling::xp(7), 1260.0);
    EXPECT_EQ(Leveling::xp(10), 2700.0);
    EXPECT_EQ(Leveling::level(0), int64_t(1));
    EXPECT_EQ(Leveling::level(59.9), int64_t(1));
    EXPECT_EQ(Leveling::level(60), int64_t(2));
    EXPECT_EQ(Leveling::level(2699), int64_t(9));
    EXPECT_EQ(Leveling::level(1e9), int64_t(10)); // the cap
}

/// Every kind offers one of two at each level from 2 to 10, the doc's
/// tables in order; every pick does something.
TEST(Leveling_theCatalogueHasTwoPicksAtEveryLevelOfEveryKind) {
    EXPECT_EQ(allCases<Perk>().size(), allCases<UnitKind>().size() * 18);
    for (UnitKind k : allCases<UnitKind>()) {
        for (int64_t l = 2; l <= 10; l++) {
            const auto o = perkOffer(k, l);
            ASSERT_TRUE(o.has_value());
            EXPECT_TRUE(kind(o->first) == k && kind(o->second) == k);
            EXPECT_TRUE(level(o->first) == l && level(o->second) == l);
            EXPECT_TRUE(info(o->first).slot == PerkSlot::one);
            EXPECT_TRUE(info(o->second).slot == PerkSlot::other);
        }
        EXPECT_TRUE(!perkOffer(k, 11).has_value());
    }
    EXPECT_TRUE(perkOffer(UnitKind::prospector, 2)->first == Perk::prospectorQuickDrill);
    EXPECT_TRUE(perkOffer(UnitKind::longbow, 10)->second == Perk::longbowShield);
    EXPECT_EQ(title(Perk::prospectorQuickDrill), std::string("Quick drill"));
    EXPECT_EQ(effect(Perk::prospectorQuickDrill), std::string("Nearby Prospectors drill 10% faster."));
    for (Perk p : allCases<Perk>()) {
        EXPECT_TRUE(!(title(p).empty() || effect(p).empty()));
        EXPECT_TRUE(!effects(p).empty()); // is built
    }
}

/// The record's offers and refusals (`PilotRecord.pending`, `refusal`),
/// set by hand.
TEST(Leveling_theRecordsOffersAndRefusals) {
    PilotRecord r;
    EXPECT_EQ(r.level(UnitKind::prospector), int64_t(1));
    EXPECT_TRUE(r.pending(UnitKind::prospector).empty());
    EXPECT_TRUE(r.refusal(Perk::prospectorQuickDrill) == std::optional<std::string>("No pick waiting"));
    KindRecord k;
    k.xp = Leveling::xp(3);
    r.kinds[UnitKind::prospector] = k;
    const auto waiting = r.pending(UnitKind::prospector);
    ASSERT_TRUE(waiting.size() == 2);
    EXPECT_EQ(waiting[0].level, int64_t(2));
    EXPECT_TRUE((waiting[0].perks() == std::vector<Perk>{Perk::prospectorQuickDrill, Perk::prospectorLightFrame}));
    EXPECT_TRUE(r.refusal(Perk::prospectorRigMaster) == std::optional<std::string>("Not on offer"));
    EXPECT_TRUE(!r.refusal(Perk::prospectorLightFrame).has_value());
    r.kinds[UnitKind::prospector].picks.push_back(Perk::prospectorLightFrame);
    EXPECT_TRUE(r.has(Perk::prospectorLightFrame));
    EXPECT_TRUE(!r.has(Perk::prospectorQuickDrill));
    EXPECT_TRUE(r.refusal(Perk::prospectorPlating) == std::nullopt);
    EXPECT_TRUE((r.driven() == std::vector<UnitKind>{UnitKind::prospector}));
}

/// `Boost`: speed-ups add up, the biggest factor and load count, and no
/// change leaves a number as it was.
TEST(Leveling_boostAddsUp) {
    EXPECT_EQ(Boost::none.apply(7.25), 7.25);
    EXPECT_EQ(Boost::none.apply(int64_t(5)), int64_t(5));
    Boost b;
    b.add(Change::Percent{0.1});
    b.add(Change::Percent{0.5});
    b.add(Change::Times{2});
    b.add(Change::Times{3});
    EXPECT_NEAR(b.rate(), 3 * 1.6, 1e-12);
    Boost load;
    load.add(Change::AtLeast{7});
    load.add(Change::AtLeast{10});
    EXPECT_EQ(load.apply(int64_t(5)), int64_t(10));
    load.add(Change::Plus{2});
    EXPECT_EQ(load.apply(int64_t(5)), int64_t(12));
}

// MARK: - LevelingKindsTests.swift

TEST(LevelingKinds_everyPerkHasEffects) {
    for (Perk p : allCases<Perk>()) EXPECT_TRUE(!effects(p).empty());
}

/// The ability key holds at most one pick a kind: Surge, Pulse mine,
/// Boost and Surge. The tank's key anchors and the Dropship's loads.
TEST(LevelingKinds_onlyOneKeyPickAKindAndNoneForTheTankOrTheDropship) {
    std::map<UnitKind, std::vector<Perk>> keys;
    for (Perk p : allCases<Perk>()) {
        bool key = false;
        for (const Effect& e : effects(p)) key = key || e.is<Effect::Burst>() || e.is<Effect::Grenade>();
        if (key) keys[kind(p)].push_back(p);
    }
    EXPECT_TRUE((keys[UnitKind::ranger] == std::vector<Perk>{Perk::rangerSurge}));
    EXPECT_TRUE((keys[UnitKind::comet] == std::vector<Perk>{Perk::cometPulseMine}));
    EXPECT_TRUE((keys[UnitKind::firefly] == std::vector<Perk>{Perk::fireflyBoost}));
    EXPECT_TRUE((keys[UnitKind::juggernaut] == std::vector<Perk>{Perk::juggernautSurge}));
    EXPECT_TRUE((keys[UnitKind::kestrel] == std::vector<Perk>{Perk::kestrelAfterburn}));
    EXPECT_TRUE(keys.find(UnitKind::hailstorm) == keys.end());
    EXPECT_TRUE(keys.find(UnitKind::dropship) == keys.end());
    EXPECT_TRUE(keys.find(UnitKind::longbow) == keys.end());
    EXPECT_TRUE(keys.find(UnitKind::prospector) == keys.end());
}

// MARK: - TrackingTests.swift

/// The balance signature: FNV-1a over the balance text, 16 hex digits,
/// the same every time.
TEST(Tracking_theSignatureIsStable) {
    EXPECT_EQ(Leveling::fnv1a(""), std::string("cbf29ce484222325"));
    EXPECT_EQ(Leveling::fnv1a("a"), std::string("af63dc4c8601ec8c"));
    EXPECT_EQ(Leveling::signature().size(), size_t(16));
    bool hex = true;
    for (char c : Leveling::signature()) hex = hex && std::isxdigit(static_cast<unsigned char>(c));
    EXPECT_TRUE(hex);
    EXPECT_EQ(Leveling::fnv1a(Leveling::balance()), Leveling::signature());
    EXPECT_EQ(Leveling::balance(), Leveling::balance());
    for (Perk p : allCases<Perk>())
        EXPECT_TRUE(Leveling::balance().find(std::string(rawValue(p)) + "|") != std::string::npos);
    EXPECT_TRUE(Leveling::balance().find("xp 10 2700.0") != std::string::npos);
}

// MARK: - AirTests.swift (the leveling line)

TEST(Leveling_machinesTakeTheNewAircraft) {
    EXPECT_TRUE(Leveling::machines.contains(UnitKind::kestrel) && Leveling::machines.contains(UnitKind::hailstorm));
}
