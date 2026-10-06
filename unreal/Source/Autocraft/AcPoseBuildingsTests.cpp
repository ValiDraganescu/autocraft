// Automation tests for the building poses (AcPoseBuildings.h). Run headless:
//   UnrealEditor Autocraft.uproject -nullrhi -unattended -nosplash -nosound
//     -ExecCmds="Automation RunTests Autocraft.Buildings; Quit"
#include "AcModelCatalog.h"
#include "AcPoseBuildings.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

// `SkEuler` (the poses' SceneKit euler angles) must give the rotation the
// export wrote for the same node (`localTransform`), for every part of every
// exported building: pods, guide lamps, tilted queue lamps.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcBuildingsEulerTest, "Autocraft.Buildings.Euler", Flags)
bool FAcBuildingsEulerTest::RunTest(const FString&)
{
	const FAcModelCatalog& Catalog = FAcModelCatalog::Get();
	if (!Catalog.IsLoaded())
	{
		AddError(TEXT("no model catalog (Content/Models/ModelCatalog.json)"));
		return false;
	}
	int32 Checked = 0;
	for (const FAcModelInfo& M : Catalog.All())
	{
		// Buildings and their scaffolds (a Ranger's arm rests elsewhere than its euler says).
		if (M.Category != TEXT("building") && M.Category != TEXT("construction")) continue;
		for (const FAcModelPart& P : M.Parts)
		{
			if (P.SkEuler.IsNearlyZero()) continue;
			const FQuat Want = P.Rest.GetRotation();
			const FQuat Got = AcBuildingPose::SkEuler(P.SkEuler.X, P.SkEuler.Y, P.SkEuler.Z);
			if (FMath::Abs(Want | Got) < 0.9999)
			{
				AddError(FString::Printf(TEXT("%s/%s: euler (%.4f, %.4f, %.4f) gives %s, the export %s"), *M.Name.ToString(),
					*P.Name.ToString(), P.SkEuler.X, P.SkEuler.Y, P.SkEuler.Z, *Got.ToString(), *Want.ToString()));
				return false;
			}
			++Checked;
		}
	}
	TestTrue(TEXT("some turned parts checked"), Checked > 20);
	return true;
}

// Every building kind has its pose.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcBuildingsRegisteredTest, "Autocraft.Buildings.Registered", Flags)
bool FAcBuildingsRegisteredTest::RunTest(const FString&)
{
	for (int32 K = 0; K < 9; ++K)
	{
		TestNotNull(*FString::Printf(TEXT("pose for %s"), AcPose::ModelBase(ac::StructureKind(K))), (void*)AcPose::Find(ac::StructureKind(K)));
	}
	TestEqual(TEXT("idle glow at b = 0"), AcBuildingPose::Glow(-0.325, 0.0), 0.05, 1e-9);
	TestTrue(TEXT("beacon on at 0.1 s"), AcBuildingPose::Beacon(1.3, true));
	TestFalse(TEXT("beacon off at 0.5 s"), AcBuildingPose::Beacon(1.7, true));
	TestFalse(TEXT("beacon off while built"), AcBuildingPose::Beacon(1.3, false));
	return true;
}

#endif
