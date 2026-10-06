// Automation tests for the life bar rules (AcLifeBars.h, LifeBars.swift).
//   UnrealEditor Autocraft.uproject -nullrhi -unattended -nosplash -nosound
//     -ExecCmds="Automation RunTests Autocraft.LifeBars; Quit"
#include "AcLifeBars.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcLifeBarsSegmentsTest, "Autocraft.LifeBars.Segments", Flags)
bool FAcLifeBarsSegmentsTest::RunTest(const FString&)
{
	// LifeBars.segmentHP's own examples: a Ranger's 45 in tens, a Citadel's 1500 in two hundreds.
	TestEqual(TEXT("45 hp"), AcLifeBars::SegmentHP(45), 10.0);
	TestEqual(TEXT("100 hp"), AcLifeBars::SegmentHP(100), 10.0);
	TestEqual(TEXT("101 hp"), AcLifeBars::SegmentHP(101), 25.0);
	TestEqual(TEXT("1500 hp"), AcLifeBars::SegmentHP(1500), 200.0);
	TestEqual(TEXT("5000 hp"), AcLifeBars::SegmentHP(5000), 500.0);
	TestEqual(TEXT("5001 hp"), AcLifeBars::SegmentHP(5001), 1000.0);
	TestEqual(TEXT("Citadel segments"), 1500.0 / AcLifeBars::SegmentHP(1500), 7.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcLifeBarsSizeTest, "Autocraft.LifeBars.Size", Flags)
bool FAcLifeBarsSizeTest::RunTest(const FString&)
{
	TestEqual(TEXT("barScale at the default zoom"), AcLifeBars::BarScale(40), 1.0);
	TestTrue(TEXT("barScale zoomed out"), FMath::IsNearlyEqual(AcLifeBars::BarScale(8), std::pow(5.0, 0.9), 1e-12));
	TestTrue(TEXT("a unit's bar is at least 0.8 wide"), AcLifeBars::Width(ac::UnitKind::ranger) >= 0.8);
	TestTrue(TEXT("Citadel bar 1.7 r"),
		FMath::IsNearlyEqual(AcLifeBars::Width(ac::StructureKind::citadel), ac::Rules::radius(ac::StructureKind::citadel) * 1.7, 1e-12));
	TestEqual(TEXT("Citadel model radius"), AcLifeBars::BuildingRadius(ac::StructureKind::citadel), 2.5);
	TestEqual(TEXT("Lab model radius"), AcLifeBars::BuildingRadius(ac::StructureKind::lab), 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcLifeBarsShieldTest, "Autocraft.LifeBars.Shield", Flags)
bool FAcLifeBarsShieldTest::RunTest(const FString&)
{
	ac::GameState S;
	S.players.resize(2);
	S.players[0].upgrades = std::set<ac::Upgrade>{ac::Upgrade::aegisShield};
	const ac::Unit Mine(1, ac::UnitKind::ranger, 0, ac::Vec2(0, 0), 0.0, ac::Unit::Task::idle);
	const ac::Unit Theirs(2, ac::UnitKind::ranger, 1, ac::Vec2(0, 0), 0.0, ac::Unit::Task::idle);
	TestEqual(TEXT("shielded Ranger"), AcLifeBars::FullHP(S, Mine), ac::Rules::hp(ac::UnitKind::ranger) + 10.0);
	TestEqual(TEXT("plain Ranger"), AcLifeBars::FullHP(S, Theirs), ac::Rules::hp(ac::UnitKind::ranger));
	return true;
}

#endif
