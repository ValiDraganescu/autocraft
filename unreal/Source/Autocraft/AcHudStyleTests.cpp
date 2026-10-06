// Automation tests for the HUD's colour transfer and letter spacing
// (`FAcHudStyle::Srgb`, `SlateTextureByte`, `FAcHudText::Kern`). Run headless:
//   UnrealEditor Autocraft.uproject -unattended -nosplash -nosound -RenderOffscreen
//     -ExecCmds="Automation RunTests Autocraft.Hud.Style; Quit" -TestExit="Automation Test Queue Empty"
#include "AcHudStyle.h"
#include "AcHudText.h"

#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcHudStyleGammaTest, "Autocraft.Hud.Style.Gamma", Flags)
bool FAcHudStyleGammaTest::RunTest(const FString&)
{
#if PLATFORM_MAC
	// Slate on the Mac writes pow(linear, 1/2.2): an sRGB component must come back as itself.
	for (const double S : {0.02, 0.1, 0.25, 0.5, 0.9})
	{
		const FLinearColor C = FAcHudStyle::Srgb(S, S, S);
		TestEqual(FString::Printf(TEXT("on screen %.2f"), S), FMath::Pow((double)C.R, 1.0 / 2.2), S, 1e-5);
		TestEqual(FString::Printf(TEXT("round trip %.2f"), S), FAcHudStyle::HudSrgb(C.R), S, 1e-5);
	}
	// A texture byte: sRGB-decoded, then pow(1/2.2) on screen, lands on the drawn byte.
	for (const int32 B : {0, 10, 25, 64, 128, 200, 255})
	{
		const double Decoded = FLinearColor::FromSRGBColor(FColor(FAcHudStyle::SlateTextureByte((uint8)B), 0, 0)).R;
		TestTrue(FString::Printf(TEXT("texture byte %d"), B), FMath::Abs(FMath::RoundToInt(255 * FMath::Pow(Decoded, 1.0 / 2.2)) - B) <= 1);  // 8-bit steps in the dark
	}
	// The world colour stays the sRGB curve.
	TestEqual(TEXT("world"), (double)FAcHudStyle::World(0.5, 0.5, 0.5).R, (double)FLinearColor::FromSRGBColor(FColor(128, 0, 0)).R, 2e-3);
	TestEqual(TEXT("hud to world"), (double)FAcHudStyle::HudToWorld(FAcHudStyle::Srgb(0.3, 0.6, 0.9)).G,
		(double)FAcHudStyle::World(0.3, 0.6, 0.9).G, 1e-5);
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcHudStyleKernTest, "Autocraft.Hud.Style.Kern", Flags)
bool FAcHudStyleKernTest::RunTest(const FString&)
{
	if (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
	{
		AddWarning(TEXT("no Slate renderer: kern not measured"));
		return true;
	}
	// FSlateFontInfo::LetterSpacing does nothing for MakeText or the font
	// measure (only shaped text), hence FAcHudText::Kern.
	const TSharedRef<FSlateFontMeasure> M = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	FSlateFontInfo Spaced = FAcHudStyle::Font(10);
	const double Plain = M->Measure(TEXT("FRONTIER"), Spaced).X;
	Spaced.LetterSpacing = 300;
	TestEqual(TEXT("LetterSpacing ignored by the measure"), (double)M->Measure(TEXT("FRONTIER"), Spaced).X, Plain, 0.01);
	FAcHudText T(TEXT("FRONTIER"), FAcHudStyle::Font(10), FLinearColor::White);
	T.Kern = 2;
	// NSAttributedString.size counts the kern after every letter.
	TestEqual(TEXT("kern 2"), (double)T.Measure().X, Plain + 2.0 * 8, 0.01);
	return true;
}

#endif
