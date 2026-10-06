// Automation tests for AcPick (the top-down pick, D4). Run headless:
//   UnrealEditor Autocraft.uproject -nullrhi -unattended -nosplash -nosound
//     -ExecCmds="Automation RunTests Autocraft.Pick; Quit"
#include "AcPick.h"

#include "Misc/AutomationTest.h"
#include "Rules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	ac::Unit MakeUnit(int64 Id, ac::UnitKind Kind, int64 Owner, ac::Vec2 At)
	{
		ac::Unit U;
		U.id = Id;
		U.kind = Kind;
		U.owner = Owner;
		U.position = At;
		U.hp = 10;
		return U;
	}

	ac::Structure MakeBuilding(int64 Id, ac::StructureKind Kind, int64 Owner, ac::Vec2 At)
	{
		ac::Structure S;
		S.id = Id;
		S.kind = Kind;
		S.owner = Owner;
		S.position = At;
		S.hp = 100;
		return S;
	}

	/// Straight down onto (x, z) from 50 cells up.
	ac::FreeView::Ray Down(double X, double Z)
	{
		return {ac::Vec3(X, 50, Z), ac::Vec3(0, -1, 0)};
	}

	FAcPickShapes Flat()
	{
		FAcPickShapes S;
		S.UnitY = [](const ac::Unit&) { return 0.0; };
		S.GroundY = [](ac::Vec2) { return 0.0; };
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcPickSlabTest, "Autocraft.Pick.Slab", Flags)
bool FAcPickSlabTest::RunTest(const FString&)
{
	const ac::Vec3 Lo(-1, 0, -1), Hi(1, 2, 1);
	// From above, straight down: enters the top at t = 48.
	const auto T = AcPick::Hit(ac::Vec3(0, 50, 0), ac::Vec3(0, -1, 0), Lo, Hi);
	TestTrue(TEXT("hits from above"), T.has_value() && FMath::IsNearlyEqual(*T, 48.0));
	// Parallel to an axis and outside the slab: misses.
	TestFalse(TEXT("parallel outside misses"), AcPick::Hit(ac::Vec3(5, 50, 0), ac::Vec3(0, -1, 0), Lo, Hi).has_value());
	// Inside the box: t = 0.
	const auto In = AcPick::Hit(ac::Vec3(0, 1, 0), ac::Vec3(1, 0, 0), Lo, Hi);
	TestTrue(TEXT("inside: 0"), In.has_value() && *In == 0.0);
	// Pointing away: misses (t would be negative).
	TestFalse(TEXT("behind misses"), AcPick::Hit(ac::Vec3(0, 5, 0), ac::Vec3(0, 1, 0), Lo, Hi).has_value());
	// A slanted ray, as the 56° camera casts (direction need not be unit).
	const auto S = AcPick::Hit(ac::Vec3(0, 10, -10), ac::Vec3(0, -1, 1), Lo, Hi);
	TestTrue(TEXT("slanted hits"), S.has_value() && FMath::IsNearlyEqual(*S, 9.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcPickRulesTest, "Autocraft.Pick.Rules", Flags)
bool FAcPickRulesTest::RunTest(const FString&)
{
	ac::GameState State;
	// A Citadel at the origin, a Prospector of player 0 standing against it,
	// an enemy Ranger out in the open, a Ranger in a Bastion.
	State.structures.push_back(MakeBuilding(1, ac::StructureKind::citadel, 0, ac::Vec2(0, 0)));
	const double Edge = ac::Rules::radius(ac::StructureKind::citadel);
	State.units.push_back(MakeUnit(2, ac::UnitKind::prospector, 0, ac::Vec2(Edge, 0)));
	State.units.push_back(MakeUnit(3, ac::UnitKind::ranger, 1, ac::Vec2(20, 0)));
	ac::Unit Inside = MakeUnit(4, ac::UnitKind::ranger, 0, ac::Vec2(-20, 0));
	Inside.task = ac::Unit::Task::inBastion;
	State.units.push_back(Inside);
	const FAcPickShapes Shapes = Flat();

	auto At = [&](double X, double Z) { return AcPick::Pick(Down(X, Z), State, 0, Shapes); };

	const auto B = At(0, 0);
	TestTrue(TEXT("the Citadel's roof"), B && B->Kind == FAcHover::EKind::Building && B->Id == 1);
	// On the Prospector, inside the Citadel's box too: whichever box top is
	// nearer wins, the unit with a head start of 1.5 cells.
	const double UnitTop = FMath::Max(1.0, 2.4 * ac::Rules::radius(ac::UnitKind::prospector));
	const double RoofTop = FMath::Max(1.2, 1.5 * (Edge + 0.15));
	const bool bUnitWins = (50 - UnitTop) - 1.5 < 50 - RoofTop;
	const auto P = At(Edge, 0);
	TestTrue(TEXT("the bias decides unit against building"), P && P->IsUnit() == bUnitWins);
	// A building low enough: the unit wins within 1.5 cells of its roof.
	State.structures[0].kind = ac::StructureKind::sentinel;
	const double SEdge = ac::Rules::radius(ac::StructureKind::sentinel);
	State.units[0].position = ac::Vec2(SEdge, 0);
	const double SRoof = FMath::Max(1.2, 1.5 * (SEdge + 0.15));
	const auto PS = At(SEdge, 0);
	TestTrue(TEXT("a unit beats a roof less than 1.5 above it"), SRoof - UnitTop >= 1.5 || (PS && PS->IsUnit() && PS->Id == 2));
	TestTrue(TEXT("own Prospector is drivable"), PS && (!PS->IsUnit() || PS->bDrivable));
	State.structures[0].kind = ac::StructureKind::citadel;
	State.units[0].position = ac::Vec2(Edge, 0);
	TestTrue(TEXT("Drivable: own Prospector"), AcPick::Drivable(State.units[0], 0));
	TestFalse(TEXT("Drivable: not another player's"), AcPick::Drivable(State.units[0], 1));
	TestFalse(TEXT("Drivable: not from a Bastion"), AcPick::Drivable(State.units[2], 0));
	const auto E = At(20, 0);
	TestTrue(TEXT("the enemy Ranger"), E && E->IsUnit() && E->Id == 3 && !E->bDrivable);
	TestFalse(TEXT("a Ranger in a Bastion is not picked"), At(-20, 0).has_value());
	TestFalse(TEXT("open ground"), At(40, 40).has_value());

	// The E1 hook: a refine that drops every unit leaves the building.
	FAcPickShapes Refined = Shapes;
	Refined.Refine = [](const FAcHover& H, const ac::FreeView::Ray&, double T) -> std::optional<double> {
		if (H.IsUnit()) return std::nullopt;
		return T;
	};
	const auto R = AcPick::Pick(Down(Edge, 0), State, 0, Refined);
	TestTrue(TEXT("refine can reject a unit"), R && !R->IsUnit() && R->Id == 1);
	return true;
}

#endif
