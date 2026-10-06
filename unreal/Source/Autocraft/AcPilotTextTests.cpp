// Automation tests for what the cockpit says (`AcPilotText`, chunk E6: the
// port of `PilotText`, `pilotHelp`, `pilotHint`, `pilotTips`,
// `sightRefusal`). Run headless:
//   UnrealEditor Autocraft.uproject -nullrhi -unattended -nosplash -nosound
//     -ExecCmds="Automation RunTests Autocraft.Pilot.Text; Quit" -TestExit="Automation Test Queue Empty"
#include "AcPilotText.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	ac::Unit Make(ac::UnitKind Kind) { return ac::Unit(1, Kind, 0, ac::Vec2(0, 0), 0, ac::Unit::Task::idle); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcPilotTextTest, "Autocraft.Pilot.Text", Flags)
bool FAcPilotTextTest::RunTest(const FString&)
{
	const ac::Unit Ranger = Make(ac::UnitKind::ranger);
	TestEqual(TEXT("ranger help"), AcPilotText::Help(Ranger),
		FString(TEXT("Tab  next Ranger   ·   V  view   ·   Esc  leave\nHold click  fire at what the sight is on\nWASD  walk   ·   mouse  look")));
	TestEqual(TEXT("ranger tips"), AcPilotText::Tips(Ranger).Num(), 6);
	TestEqual(TEXT("first tip: how it moves"), AcPilotText::Tips(Ranger)[0], FString(TEXT("WASD  walk   ·   mouse  look")));

	const ac::Unit Prospector = Make(ac::UnitKind::prospector);
	TestEqual(TEXT("prospector hint"), AcPilotText::Hint(Prospector), FString(TEXT("Hold click  drill, strike, repair")));
	const TArray<FString> Tips = AcPilotText::Tips(Prospector);
	TestEqual(TEXT("prospector tips"), Tips.Num(), 8);
	TestEqual(TEXT("the rest of its help"), Tips[1],
		FString(TEXT("click  enter a Derrick   ·   R + hold click  repair (a Derrick too)   ·   walk into the Citadel to unload")));
	TestEqual(TEXT("its build line"), Tips[2], FString(TEXT("B  build menu   ·   number  pick   ·   click  place")));
	TestTrue(TEXT("prospector help has the build line"), AcPilotText::Help(Prospector).Contains(TEXT("\nB  build menu")));

	ac::Unit Longbow = Make(ac::UnitKind::longbow);
	TestTrue(TEXT("tank: drive line"), AcPilotText::Help(Longbow).EndsWith(TEXT("aim the turret (the ring shows the gun)")));
	Longbow.anchor = 1;
	Longbow.anchored = true;
	TestTrue(TEXT("anchored: held line"), AcPilotText::Help(Longbow).EndsWith(TEXT("R  tank mode to drive")));
	TestEqual(TEXT("anchored: held tip"), AcPilotText::Tips(Longbow)[0], FString(TEXT("mouse  aim the turret   ·   R  tank mode to drive")));

	FAcSightInfo S;
	S.Hp = 30;
	S.MaxHp = 45;
	S.Distance = 7.5;
	S.Range = 5;
	TestEqual(TEXT("out of range"), AcPilotText::Refusal(Ranger, S).Get(TEXT("")), FString(TEXT("Out of range  7.5 / 5")));
	S.Distance = 3.8;
	S.bInRange = true;
	TestFalse(TEXT("in range: no refusal"), AcPilotText::Refusal(Ranger, S).IsSet());
	S.Distance = 1.4;
	S.MinRange = 2;
	S.bInRange = false;
	TestEqual(TEXT("too close"), AcPilotText::Refusal(Longbow, S).Get(TEXT("")), FString(TEXT("Too close  1.4 / min 2")));
	FAcSightInfo Friend;
	Friend.bFriend = true;
	Friend.Hp = Friend.MaxHp = 45;
	TestEqual(TEXT("friend at full health"), AcPilotText::Refusal(Make(ac::UnitKind::dropship), Friend).Get(TEXT("")),
		FString(TEXT("Full health")));

	TestEqual(TEXT("%g whole"), AcPilotText::FormatG(5), FString(TEXT("5")));
	TestEqual(TEXT("%g half"), AcPilotText::FormatG(6.5), FString(TEXT("6.5")));
	TestTrue(TEXT("cabs"), AcPilotText::HasCab(ac::UnitKind::hailstorm) && !AcPilotText::HasCab(ac::UnitKind::ranger));
	return true;
}

#endif
