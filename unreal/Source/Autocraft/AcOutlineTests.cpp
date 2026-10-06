// Automation tests for which instances get an outline and in which colour
// (AcOutline.h). Run headless:
//   UnrealEditor Autocraft.uproject -nullrhi -unattended -nosplash -nosound
//     -ExecCmds="Automation RunTests Autocraft.Outline; Quit"
#include "AcOutline.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
	using ac::UnitKind;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcOutlineWhoTest, "Autocraft.Outline.Who", Flags)
bool FAcOutlineWhoTest::RunTest(const FString&)
{
	using namespace AcOutline;
	const std::optional<double> Buried = 1.0, Digging = 0.4, None = std::nullopt, Zero = 0.0;
	TestEqual(TEXT("a buried Scorpion of the local side: its owner's team stencil"), int32(StencilFor(UnitKind::scorpion, Buried, 0, true)), int32(TeamStencil(0)));
	TestEqual(TEXT("an ally's, in the ally's colour"), int32(StencilFor(UnitKind::scorpion, Buried, 3, true)), int32(TeamStencil(3)));
	TestEqual(TEXT("a buried enemy that is shown: red"), int32(StencilFor(UnitKind::scorpion, Buried, 1, false)), int32(StencilFoe));
	TestEqual(TEXT("burying or digging out counts"), int32(StencilFor(UnitKind::scorpion, Digging, 0, true)), int32(TeamStencil(0)));
	TestEqual(TEXT("and for an enemy"), int32(StencilFor(UnitKind::scorpion, Digging, 2, false)), int32(StencilFoe));
	TestEqual(TEXT("a walking Scorpion has none (no anchor)"), int32(StencilFor(UnitKind::scorpion, None, 0, true)), int32(StencilNone));
	TestEqual(TEXT("nor one at anchor 0"), int32(StencilFor(UnitKind::scorpion, Zero, 0, true)), int32(StencilNone));
	TestEqual(TEXT("an anchored Longbow has none"), int32(StencilFor(UnitKind::longbow, Buried, 0, true)), int32(StencilNone));
	TestEqual(TEXT("an enemy Longbow neither"), int32(StencilFor(UnitKind::longbow, Buried, 1, false)), int32(StencilNone));
	TestEqual(TEXT("a Ranger has none"), int32(StencilFor(UnitKind::ranger, std::nullopt, 0, true)), int32(StencilNone));
	TestEqual(TEXT("the owner's index is held in range"), int32(StencilFor(UnitKind::scorpion, Buried, 40, true)), int32(TeamStencil(7)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcOutlineColourTest, "Autocraft.Outline.Colour", Flags)
bool FAcOutlineColourTest::RunTest(const FString&)
{
	using namespace AcOutline;
	TArray<FLinearColor> Paint;
	for (int32 I = 0; I < 8; ++I) Paint.Add(FLinearColor(float(I) / 8.f, 0.5f, 1.f - float(I) / 8.f, 1.f));
	for (int32 T = 0; T < 8; ++T)
	{
		const uint8 S = TeamStencil(T);
		TestEqual(*FString::Printf(TEXT("stencil %d is a team's"), S), int32(ClassOf(S)), int32(EClass::Team));
		TestEqual(TEXT("whose index comes back"), TeamOf(S), T);
		const FLinearColor C = Colour(S, Paint);
		TestEqual(TEXT("in its palette colour (red channel)"), double(C.R), double(Paint[T].R), 1e-6);
		TestEqual(TEXT("(blue channel)"), double(C.B), double(Paint[T].B), 1e-6);
		TestEqual(TEXT("faint"), double(C.A), double(TeamStrength), 1e-6);
	}
	const FLinearColor Foe = Colour(StencilFoe, Paint);
	TestTrue(TEXT("a foe's line is red"), Foe.R > 0.9f && Foe.G < 0.4f && Foe.B < 0.4f);
	TestTrue(TEXT("and firmer than a team's"), Foe.A > TeamStrength);
	TestEqual(TEXT("foe class"), int32(ClassOf(StencilFoe)), int32(EClass::Foe));
	TestEqual(TEXT("no team in a foe"), TeamOf(StencilFoe), -1);
	for (const uint8 S : {uint8(0), uint8(9), uint8(15), uint8(17), uint8(255)})
	{
		TestEqual(*FString::Printf(TEXT("%d draws nothing"), S), int32(ClassOf(S)), int32(EClass::None));
		TestEqual(TEXT("with no strength"), double(Colour(S, Paint).A), 0.0, 1e-6);
	}
	return true;
}

#endif
