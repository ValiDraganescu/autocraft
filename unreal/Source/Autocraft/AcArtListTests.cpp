// Automation tests for the console art's geometry (AcArtList.h): the colours
// against Core Graphics' own, and the polygon work the port leans on (the
// even-odd frame body, grown shapes, lines cut by a region). Run headless:
//   UnrealEditor Autocraft.uproject -nullrhi -unattended -nosplash -nosound
//     -ExecCmds="Automation RunTests Autocraft.Hud.Art; Quit" -TestExit="Automation Test Queue Empty"
#include "AcArtList.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	double AreaOf(const FAcRegion& R)
	{
		double A = 0;
		for (const FAcPoly& P : R.Pieces) A += AcArt::Area(P);
		return A;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcArtColourTest, "Autocraft.Hud.Art.Colour", Flags)
bool FAcArtColourTest::RunTest(const FString&)
{
	// What Core Graphics writes into a device RGB bitmap for
	// `CGColor(red:green:blue:alpha:)` (macOS 26, measured 2026-10-08).
	struct FSample { double R, G, B; uint8 Er, Eg, Eb; };
	const FSample Samples[] = {{0.11, 0.16, 0.32, 37, 56, 101}, {0.5, 0.5, 0.5, 146, 146, 146}, {0.2, 0.2, 0.2, 66, 66, 66},
		{0.35, 0.85, 1, 103, 224, 255}, {0.93, 0.7, 0.1, 242, 190, 31}, {0.65, 0.75, 0.92, 180, 204, 239}, {0.05, 0.05, 0.05, 14, 14, 14}};
	for (const FSample& S : Samples)
	{
		const FLinearColor C = AcArt::Rgb(S.R, S.G, S.B);
		const int32 R = FMath::RoundToInt(C.R * 255), G = FMath::RoundToInt(C.G * 255), B = FMath::RoundToInt(C.B * 255);
		TestTrue(FString::Printf(TEXT("(%.2f, %.2f, %.2f) → (%d, %d, %d), Core Graphics (%d, %d, %d)"), S.R, S.G, S.B, R, G, B, S.Er,
			S.Eg, S.Eb), FMath::Abs(R - S.Er) <= 2 && FMath::Abs(G - S.Eg) <= 2 && FMath::Abs(B - S.Eb) <= 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcArtRegionTest, "Autocraft.Hud.Art.Region", Flags)
bool FAcArtRegionTest::RunTest(const FString&)
{
	const FAcPoly Outer = AcArt::RectPoly(FAcRect(0, 0, 10, 10));
	const FAcPoly Hole = AcArt::RectPoly(FAcRect(3, 3, 4, 4));
	const FAcRegion Body = AcArt::Minus(Outer, Hole);
	TestNearlyEqual(TEXT("rectangle minus hole: area"), AreaOf(Body), 84.0, 1e-6);
	for (const FAcPoly& P : Body.Pieces) TestTrue(TEXT("pieces counter-clockwise"), AcArt::Area(P) > 0);
	TestFalse(TEXT("the hole is not in the body"), Body.Contains(FVector2D(5, 5)));
	TestTrue(TEXT("the rim is"), Body.Contains(FVector2D(1, 5)));

	// A hole past the edge (the view frame's opening runs below the view).
	const FAcRegion Open = AcArt::Minus(Outer, AcArt::RectPoly(FAcRect(2, -4, 6, 10)));
	TestNearlyEqual(TEXT("hole past the edge: area"), AreaOf(Open), 100.0 - 36.0, 1e-6);

	// Grown outward rounds the corners; inward mitres them.
	const FAcPoly Round = AcArt::Grown(Outer, 1);
	TestNearlyEqual(TEXT("grown by 1: area 144 − (4 − π)"), AcArt::Area(Round), 144.0 - (4.0 - UE_DOUBLE_PI), 0.15);
	TestNearlyEqual(TEXT("shrunk by 1: area 64"), AcArt::Area(AcArt::Grown(Outer, -1)), 64.0, 1e-6);
	TestEqual(TEXT("the same number of points either way"), Round.Num(), AcArt::Grown(Outer, -1).Num());

	// A line across the body comes out in two runs, the hole cut out.
	const TArray<TArray<FVector2D>> Runs = AcArt::ClipLine({FVector2D(-5, 5), FVector2D(15, 5)}, Body);
	if (TestEqual(TEXT("two runs"), Runs.Num(), 2))
	{
		TestTrue(TEXT("left run 0 → 3"), Runs[0][0].Equals(FVector2D(0, 5), 1e-6) && Runs[0].Last().Equals(FVector2D(3, 5), 1e-6));
		TestTrue(TEXT("right run 7 → 10"), Runs[1][0].Equals(FVector2D(7, 5), 1e-6) && Runs[1].Last().Equals(FVector2D(10, 5), 1e-6));
	}
	return true;
}

#endif
