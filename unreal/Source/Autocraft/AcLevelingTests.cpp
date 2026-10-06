// Automation tests for the leveling UI's logic (chunk E9, AcLeveling.h: the
// port of `PickOffer`, `PerkReach`, `PickCards.wrap`, `pilotsHeight` and the
// banner's lines). Run headless:
//   UnrealEditor Autocraft.uproject -nullrhi -unattended -nosplash -nosound
//     -ExecCmds="Automation RunTests Autocraft.Leveling; Quit" -TestExit="Automation Test Queue Empty"
#include "AcLeveling.h"
#include "AcPilotText.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/// `windowshot --level KIND:L`: the XP for level `L` and a third of the way on.
	ac::KindRecord At(int64 L)
	{
		ac::KindRecord R;
		const double From = ac::Leveling::xp(L);
		R.xp = From + (ac::Leveling::xp(L + 1) - From) / 3;
		return R;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcLevelingOfferTest, "Autocraft.Leveling.Offer", Flags)
bool FAcLevelingOfferTest::RunTest(const FString&)
{
	ac::PilotRecord R;
	TestFalse(TEXT("nothing driven: no offer"), FAcPickOffer::Of(R, ac::UnitKind::prospector).IsSet());
	R.kinds[ac::UnitKind::prospector] = At(1);
	TestFalse(TEXT("level 1: nothing waits"), FAcPickOffer::Of(R, ac::UnitKind::prospector).IsSet());
	R.kinds[ac::UnitKind::prospector] = At(4);
	const TOptional<FAcPickOffer> O = FAcPickOffer::Of(R, ac::UnitKind::prospector);
	if (!TestTrue(TEXT("level 4: an offer"), O.IsSet())) return false;
	TestEqual(TEXT("lowest level first"), O->Level, 2);
	TestEqual(TEXT("three wait"), O->Waiting, 3);
	TestTrue(TEXT("level 2's pair"), O->Perks == TArray<ac::Perk>{ac::Perk::prospectorQuickDrill, ac::Perk::prospectorLightFrame});
	R.kinds[ac::UnitKind::prospector].picks.push_back(ac::Perk::prospectorQuickDrill);
	const TOptional<FAcPickOffer> Next = FAcPickOffer::Of(R, ac::UnitKind::prospector);
	TestEqual(TEXT("a pick moves it on"), Next ? Next->Level : 0, 3);
	TestEqual(TEXT("keys"), FString(FAcPickOffer::Key(0)) + FAcPickOffer::Key(1), FString(TEXT("ZX")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcLevelingReachTest, "Autocraft.Leveling.Reach", Flags)
bool FAcLevelingReachTest::RunTest(const FString&)
{
	// The marks in `windowshot --pilot ore --level prospector:4` and `--command-map`.
	TestEqual(TEXT("Quick drill: nearby"), (int32)AcLeveling::Reach(ac::Perk::prospectorQuickDrill), (int32)EAcPerkReach::Nearby);
	TestEqual(TEXT("Light frame: team"), (int32)AcLeveling::Reach(ac::Perk::prospectorLightFrame), (int32)EAcPerkReach::Team);
	TestEqual(TEXT("Rig master: team"), (int32)AcLeveling::Reach(ac::Perk::prospectorRigMaster), (int32)EAcPerkReach::Team);
	TestEqual(TEXT("Plating: you"), (int32)AcLeveling::Reach(ac::Perk::prospectorPlating), (int32)EAcPerkReach::Driven);
	TestEqual(TEXT("Big haul: nearby"), (int32)AcLeveling::Reach(ac::Perk::prospectorBigHaul), (int32)EAcPerkReach::Nearby);
	TestEqual(TEXT("Fast welder: you"), (int32)AcLeveling::Reach(ac::Perk::prospectorFastWelder), (int32)EAcPerkReach::Driven);
	TestEqual(TEXT("Combat drill: team"), (int32)AcLeveling::Reach(ac::Perk::rangerCombatDrill), (int32)EAcPerkReach::Team);
	TestEqual(TEXT("Flak vest: you"), (int32)AcLeveling::Reach(ac::Perk::rangerFlakVest), (int32)EAcPerkReach::Driven);
	// The new kinds' picks: what reaches the units around is nearby, not the driven one's alone.
	TestEqual(TEXT("Wingmen: nearby"), (int32)AcLeveling::Reach(ac::Perk::peregrineWingmen), (int32)EAcPerkReach::Nearby);
	TestEqual(TEXT("Thrusters: team"), (int32)AcLeveling::Reach(ac::Perk::peregrineThrusters), (int32)EAcPerkReach::Team);
	TestEqual(TEXT("Escort: nearby"), (int32)AcLeveling::Reach(ac::Perk::peregrineEscort), (int32)EAcPerkReach::Nearby);
	TestEqual(TEXT("Bulwark: nearby"), (int32)AcLeveling::Reach(ac::Perk::atlasBulwark), (int32)EAcPerkReach::Nearby);
	TestEqual(TEXT("Juggernaut's Bulwark: nearby"), (int32)AcLeveling::Reach(ac::Perk::juggernautBulwark), (int32)EAcPerkReach::Nearby);
	TestEqual(TEXT("War march: nearby"), (int32)AcLeveling::Reach(ac::Perk::atlasWarMarch), (int32)EAcPerkReach::Nearby);
	TestEqual(TEXT("Heavy industry: team"), (int32)AcLeveling::Reach(ac::Perk::atlasHeavyIndustry), (int32)EAcPerkReach::Team);
	TestEqual(TEXT("Flak mount: you"), (int32)AcLeveling::Reach(ac::Perk::atlasFlakMount), (int32)EAcPerkReach::Driven);
	TestEqual(TEXT("Deep burrow: you"), (int32)AcLeveling::Reach(ac::Perk::scorpionDeepBurrow), (int32)EAcPerkReach::Driven);
	TestEqual(TEXT("Nest: nearby"), (int32)AcLeveling::Reach(ac::Perk::scorpionNest), (int32)EAcPerkReach::Nearby);
	TestEqual(TEXT("Quick dig: team"), (int32)AcLeveling::Reach(ac::Perk::scorpionQuickDig), (int32)EAcPerkReach::Team);
	TestEqual(TEXT("tag"), FString(AcLeveling::Tag(EAcPerkReach::Nearby)), FString(TEXT("NEARBY")));
	TestEqual(TEXT("name"), AcLeveling::Name(ac::Perk::rangerSurge), FString(TEXT("rangerSurge")));
	TestTrue(TEXT("parse"), AcLeveling::ParsePerk(TEXT("RANGERSURGE")) == TOptional<ac::Perk>(ac::Perk::rangerSurge));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcLevelingNewKindsTest, "Autocraft.Leveling.NewKinds", Flags)
bool FAcLevelingNewKindsTest::RunTest(const FString&)
{
	// The Peregrine, the Atlas and the Scorpion: two picks at each level from 2 to 10, each with a
	// title and a line that fits its card (a 245-point text well, three lines at size 12, no ellipsis).
	for (const ac::UnitKind Kind : {ac::UnitKind::peregrine, ac::UnitKind::atlas, ac::UnitKind::scorpion})
	{
		ac::PilotRecord R;
		R.kinds[Kind] = At(10);
		int32 Offers = 0;
		for (int32 Pick = 0; Pick < 9; ++Pick)
		{
			const TOptional<FAcPickOffer> O = FAcPickOffer::Of(R, Kind);
			if (!TestTrue(FString::Printf(TEXT("%s: an offer at pick %d"), *AcPilotText::Title(Kind), Pick), O.IsSet())) return false;
			TestEqual(TEXT("level"), O->Level, Pick + 2);
			TestEqual(TEXT("two perks"), O->Perks.Num(), 2);
			for (const ac::Perk P : O->Perks)
			{
				TestTrue(FString::Printf(TEXT("%s has a title"), *AcLeveling::Name(P)), !AcLeveling::Title(P).IsEmpty());
				const TArray<FString> Lines = AcLeveling::Wrap(AcLeveling::Effect(P), 12, 245, 3);
				TestTrue(FString::Printf(TEXT("%s fits its card"), *AcLeveling::Name(P)), Lines.Num() >= 1 && !Lines.Last().EndsWith(TEXT("…")));
				TestTrue(FString::Printf(TEXT("%s is of its kind"), *AcLeveling::Name(P)), ac::info(P).kind == Kind);
			}
			R.kinds[Kind].picks.push_back(O->Perks[0]);
			Offers += 1;
		}
		TestEqual(TEXT("nine offers"), Offers, 9);
		TestFalse(TEXT("then none"), FAcPickOffer::Of(R, Kind).IsSet());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcLevelingLinesTest, "Autocraft.Leveling.Lines", Flags)
bool FAcLevelingLinesTest::RunTest(const FString&)
{
	TestEqual(TEXT("banner"), AcLeveling::BannerTitle(ac::UnitKind::prospector, 4), FString(TEXT("PROSPECTOR  LEVEL 4")));
	TestEqual(TEXT("banner sub"), AcLeveling::BannerSub(4), FString(TEXT("NEW PICK ON THE DASHBOARD")));
	TestEqual(TEXT("banner sub at the cap"), AcLeveling::BannerSub(10), FString(TEXT("TOP LEVEL  ·  LAST PICK ON THE DASHBOARD")));

	// `pilotsHeight`: 40 + 46 a kind + the pick room under one with a choice.
	TArray<FAcDrivenRow> Rows;
	TestEqual(TEXT("no pilots, no room"), AcLeveling::PilotsHeight(Rows), 0.0);
	FAcDrivenRow A, B;
	A.Offer = FAcPickOffer();
	Rows = {A, B};
	TestEqual(TEXT("two kinds, one choice"), AcLeveling::PilotsHeight(Rows), 40.0 + 46 + 98 + 46);

	// `PickCards.wrap`: at most `Max` lines, the last cut with "…".
	const FString Long = TEXT("Carries 35 ore or 28 MH a trip; nearby Prospectors carry 7 ore or 6 MH, up from 5 and 4.");
	const TArray<FString> Three = AcLeveling::Wrap(Long, 12, 120, 3);
	TestEqual(TEXT("three lines"), Three.Num(), 3);
	TestTrue(TEXT("cut"), Three.Last().EndsWith(TEXT("…")));
	const TArray<FString> One = AcLeveling::Wrap(TEXT("Plating"), 12, 200, 1);
	TestEqual(TEXT("short: as is"), One.Num() == 1 ? One[0] : FString(), FString(TEXT("Plating")));
	return true;
}

#endif
