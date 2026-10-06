// Automation tests for the HUD's name highlighting (`FAcNames`, the port of
// Sources/Autocraft/NameHighlight.swift). Run headless:
//   UnrealEditor Autocraft.uproject -nullrhi -unattended -nosplash -nosound
//     -ExecCmds="Automation RunTests Autocraft.Hud; Quit" -TestExit="Automation Test Queue Empty"
#include "AcHudText.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/// The names `Text` picks out, joined by "|".
	FString Marked(const FString& Text)
	{
		TArray<FString> Out;
		for (const FAcTextRun& R : FAcNames::Ranges(Text)) Out.Add(Text.Mid(R.Start, R.Len));
		return FString::Join(Out, TEXT("|"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcHudNamesTest, "Autocraft.Hud.Names", Flags)
bool FAcHudNamesTest::RunTest(const FString&)
{
	TestEqual(TEXT("plural y"), FAcNames::Plural(TEXT("Firefly")), FString(TEXT("Fireflies")));
	TestEqual(TEXT("plural vowel y"), FAcNames::Plural(TEXT("Key")), FString(TEXT("Keys")));
	TestEqual(TEXT("plural"), FAcNames::Plural(TEXT("Ranger")), FString(TEXT("Rangers")));

	TestEqual(TEXT("singular and plural"), Marked(TEXT("Train a Ranger, then 5 Fireflies")), FString(TEXT("Ranger|Fireflies")));
	TestEqual(TEXT("capitals"), Marked(TEXT("INSIDE THE DERRICK")), FString(TEXT("DERRICK")));
	TestEqual(TEXT("two words, longest first"), Marked(TEXT("Hab Domes add supply")), FString(TEXT("Hab Domes")));
	TestEqual(TEXT("possessive"), Marked(TEXT("the Citadel's dish")), FString(TEXT("Citadel's")));
	TestEqual(TEXT("curly possessive"), Marked(TEXT("the Lab’s queue")), FString(TEXT("Lab’s")));
	TestEqual(TEXT("lower case is the word"), Marked(TEXT("a comet over the lab")), FString());
	TestEqual(TEXT("whole words only"), Marked(TEXT("Labrador Rangerz Comets")), FString(TEXT("Comets")));
	TestEqual(TEXT("mixed case is not a form"), Marked(TEXT("RaNGER")), FString());

	const FString S = TEXT("Drive the Longbow now");
	const TArray<FAcTextRun> Runs = FAcNames::Runs(S);
	TestEqual(TEXT("runs"), Runs.Num(), 3);
	if (Runs.Num() == 3)
	{
		TestTrue(TEXT("middle run is the name"), Runs[1].bName && S.Mid(Runs[1].Start, Runs[1].Len) == TEXT("Longbow"));
		TestEqual(TEXT("runs cover the text"), Runs[2].Start + Runs[2].Len, S.Len());
	}
	TestEqual(TEXT("no names: one plain run"), FAcNames::Runs(TEXT("ore 40")).Num(), 1);
	return true;
}

#endif
