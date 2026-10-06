// The data types' small methods and tables (Types.h, Rules.h).
#include "test.h"

#include "Leveling.h"
#include "Rules.h"
#include "Types.h"

TEST(types_enums_and_raw_values) {
    EXPECT_EQ(ac::allCases<ac::Perk>().size(), size_t(216));
    EXPECT_EQ(ac::allCases<ac::UnitKind>().size(), size_t(12));
    EXPECT_TRUE(ac::rawValue(ac::Unit::Kind::hailstorm) == "hailstorm");
    EXPECT_TRUE(ac::rawValue(ac::Stance::auto_) == "auto");
    EXPECT_TRUE(ac::parse<ac::Structure::Kind>("habDome") == ac::StructureKind::habDome);
    EXPECT_TRUE(!ac::parse<ac::Structure::Kind>("HabDome"));
    EXPECT_TRUE(ac::parse<ac::Perk>("hailstormShield") == ac::Perk::hailstormShield);
    for (auto p : ac::allCases<ac::Perk>()) EXPECT_TRUE(ac::parse<ac::Perk>(ac::rawValue(p)) == p);
}

TEST(types_rules_tables) {
    EXPECT_EQ(ac::Rules::hp(ac::UnitKind::longbow), 175.0);
    EXPECT_EQ(ac::Rules::cost(ac::UnitKind::kestrel), int64_t(150));
    EXPECT_EQ(ac::Rules::cost(ac::StructureKind::citadel), int64_t(400));
    EXPECT_EQ(ac::Rules::speed(ac::UnitKind::prospector), ac::Rules::prospectorSpeed);
    EXPECT_EQ(ac::Rules::stats(ac::UnitKind::comet).hits, int64_t(2));
    EXPECT_TRUE(ac::Rules::requires_(ac::StructureKind::spacedock) == ac::StructureKind::foundry);
    EXPECT_TRUE(ac::Rules::produces(ac::StructureKind::garrison) == ac::UnitKind::ranger);
    // Breacher grenades against an armoured target with 1 armour: 10 + 10 - 1.
    EXPECT_EQ(ac::Rules::damage(ac::UnitKind::juggernaut, false, true, false, 1), 19.0);
    EXPECT_EQ(ac::Rules::damage(ac::UnitKind::longbow, true, true, false, 1), 69.0);
    EXPECT_EQ(ac::Rules::damage(true), 5.0);
    EXPECT_EQ(ac::Rules::flight(ac::UnitKind::longbow, false, 10), 0.0);
    EXPECT_EQ(ac::Rules::vision(ac::StructureKind::bastion), 10.0);
}

TEST(types_methods) {
    ac::Unit u(7, ac::UnitKind::longbow, 1, ac::Vec2(1, 0), 0, ac::Unit::Task::toRally);
    EXPECT_EQ(u.hp, 175.0);
    EXPECT_TRUE(u.walking() && u.soldier());
    u.moving = false;
    EXPECT_TRUE(!u.walking());
    u.jumpFrom = ac::Vec2(0, 0);
    u.jumpTo = ac::Vec2(4, 0);
    EXPECT_EQ(*u.jump(), 0.25);
    u.task = ac::Unit::Task::repairing;
    EXPECT_TRUE(u.working());
    u.moving = true;
    EXPECT_TRUE(!u.working());
    ac::Structure s(3, ac::StructureKind::garrison, 0, ac::Vec2(), 10.0);
    EXPECT_EQ(s.hp, 100.0);
    EXPECT_TRUE(!s.complete());
    ac::Mission m = ac::Mission::Hold{4, ac::Vec2(1, 1)};
    EXPECT_TRUE(m.objective() == int64_t(4));
    m = ac::Mission::Raid{ac::Vec2(2, 2)};
    EXPECT_TRUE(!m.objective());
    ac::Command c = ac::Command::Serve{0, 3, ac::Command(ac::Command::Train{5, std::nullopt})};
    ac::Command d = c;
    EXPECT_TRUE(c == d);
    ac::Boost b;
    EXPECT_EQ(b.apply(3.5), 3.5);
    b.add(ac::Change::Times{2});
    b.add(ac::Change::Percent{0.5});
    b.add(ac::Change::AtLeast{10});
    b.add(ac::Change::Plus{1});
    EXPECT_EQ(b.apply(3.0), 16.0);
    EXPECT_EQ(b.apply(int64_t(3)), int64_t(16));
    EXPECT_TRUE(ac::Leveling::hero(ac::Stat::OreCarry{}) == ac::Change(ac::Change::AtLeast{25}));
    EXPECT_EQ(ac::Leveling::level(ac::Leveling::xp(4)), int64_t(4));
    EXPECT_TRUE(ac::covers(ac::Filter::machines, ac::UnitKind::dropship) && !ac::covers(ac::Filter::bio, ac::UnitKind::dropship));
    std::map<ac::Stat, int> byStat{{ac::Stat::Training{ac::UnitKind::comet}, 1}, {ac::Stat::Speed{}, 2}};
    EXPECT_EQ(byStat.begin()->second, 2);
    EXPECT_TRUE(ac::max(-0.0, 0.0) == 0.0 && !std::signbit(ac::max(-0.0, 0.0)));
}
