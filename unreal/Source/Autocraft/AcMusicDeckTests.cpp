// Automation tests for the music deck (chunk D9): its layout and hit tests
// (`MusicDeck.rebuild`, `command(at:)`), the clock and the title's fit.
// Run headless:
//   UnrealEditor Autocraft.uproject -nullrhi -unattended -nosplash -nosound
//     -ExecCmds="Automation RunTests Autocraft.Console.MusicDeck; Quit" -TestExit="Automation Test Queue Empty"
#include "SAcMusicDeck.h"

#include "AcConsolePaint.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcMusicDeckLayoutTest, "Autocraft.Console.MusicDeck.Layout", Flags)
bool FAcMusicDeckLayoutTest::RunTest(const FString&)
{
	// A 150 × 170 screen: inset 6 × 5, buttons 24 wide with 4 between, centred.
	const auto B = SAcMusicDeck::Buttons(FVector2D(150, 170), true);
	if (!TestEqual(TEXT("five buttons"), B.Num(), 5)) return false;
	TestEqual(TEXT("first x"), B[0].Key.X, 7.0);
	TestEqual(TEXT("first y"), B[0].Key.Y, 5.0);
	TestEqual(TEXT("side"), B[0].Key.W, 24.0);
	TestEqual(TEXT("last x"), B[4].Key.X, 119.0);
	TestTrue(TEXT("order"), B[0].Value == EAcMusicCommand::Down && B[1].Value == EAcMusicCommand::Previous
		&& B[2].Value == EAcMusicCommand::Toggle && B[3].Value == EAcMusicCommand::Next && B[4].Value == EAcMusicCommand::Up);
	// A narrow screen: the buttons shrink to fit.
	const auto N = SAcMusicDeck::Buttons(FVector2D(100, 170), true);
	TestEqual(TEXT("narrow side"), N[0].Key.W, 14.0);
	TestTrue(TEXT("no music: no buttons"), SAcMusicDeck::Buttons(FVector2D(150, 170), false).IsEmpty());
	TestTrue(TEXT("too small: no buttons"), SAcMusicDeck::Buttons(FVector2D(150, 60), true).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcMusicDeckTextTest, "Autocraft.Console.MusicDeck.Text", Flags)
bool FAcMusicDeckTextTest::RunTest(const FString&)
{
	TestEqual(TEXT("1:23"), SAcMusicDeck::Clock(83.9), FString(TEXT("1:23")));
	TestEqual(TEXT("3:14"), SAcMusicDeck::Clock(194.6), FString(TEXT("3:14")));
	TestEqual(TEXT("never below 0"), SAcMusicDeck::Clock(-3), FString(TEXT("0:00")));
	TestEqual(TEXT("voted thumb"), SAcMusicDeck::SymbolName(EAcMusicCommand::Up, true, false), FString(TEXT("hand.thumbsup.fill")));
	TestEqual(TEXT("play while paused"), SAcMusicDeck::SymbolName(EAcMusicCommand::Toggle, false, true), FString(TEXT("play.fill")));
	const auto Short = SAcMusicDeck::Fit(TEXT("Aftermath (1)"), 13, 138);
	TestEqual(TEXT("short title kept"), Short.Key, FString(TEXT("Aftermath (1)")));
	TestEqual(TEXT("short title size"), Short.Value, 13.0);
	const FString Long = TEXT("A Very Long Title That Will Never Fit On The Little Deck Screen");
	const auto Cut = SAcMusicDeck::Fit(Long, 13, 138);
	TestTrue(TEXT("long title cut"), Cut.Key.EndsWith(TEXT("…")));
	TestTrue(TEXT("shrunk to 70 % at most"), FMath::IsNearlyEqual(Cut.Value, 13 * 0.7, 1e-6));
	TestTrue(TEXT("fits"), FAcConsolePaint::TextWidth(Cut.Key, Cut.Value) <= 138);
	return true;
}

#endif
