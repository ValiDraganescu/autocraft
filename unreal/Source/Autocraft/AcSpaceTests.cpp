// Automation tests for AcSpace.h: the round trips, the yaw sign and the
// SceneKit rotation mapping. Run headless:
//   UnrealEditor Autocraft.uproject -nullrhi -unattended -nosplash
//     -ExecCmds="Automation RunTests Autocraft.Space; Quit"
#include "AcSpace.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	bool Near(const FVector& A, const FVector& B, double Tolerance = 1e-9)
	{
		return A.Equals(B, Tolerance);
	}

	/// A few awkward values, positive, negative and fractional.
	const double Samples[] = {0.0, 1.0, -1.0, 0.5, 12.375, -37.25, 271.0, 1e-3};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcSpaceRoundTripTest, "Autocraft.Space.RoundTrip", Flags)
bool FAcSpaceRoundTripTest::RunTest(const FString&)
{
	for (double X : Samples)
	{
		for (double Y : Samples)
		{
			for (double H : Samples)
			{
				const ac::Vec2 P(X, Y);
				const FVector W = AcSpace::ToWorld(P, H);
				TestEqual(TEXT("one cell is 100 cm (x)"), W.X, X * 100.0);
				TestEqual(TEXT("sim y is Unreal Y"), W.Y, Y * 100.0);
				TestEqual(TEXT("height is Unreal Z"), W.Z, H * 100.0);
				const ac::Vec2 Back = AcSpace::ToSim(W);
				TestTrue(TEXT("ToSim(ToWorld(p)) == p"), FMath::IsNearlyEqual(Back.x, X, 1e-12) && FMath::IsNearlyEqual(Back.y, Y, 1e-12));
				TestTrue(TEXT("height round trip"), FMath::IsNearlyEqual(AcSpace::HeightToSim(W), H, 1e-12));
				// SceneKit (x, y, z) = sim (x, h, y).
				TestTrue(TEXT("SceneKit point = sim point"), Near(AcSpace::FromSceneKit(X, H, Y), W));
				TestTrue(TEXT("ToSceneKit(FromSceneKit(v)) == v"), Near(AcSpace::ToSceneKit(AcSpace::FromSceneKit(X, Y, H)), FVector(X, Y, H), 1e-12));
			}
		}
	}
	TestEqual(TEXT("ToCm"), AcSpace::ToCm(2.5), 250.0);
	TestEqual(TEXT("ToCells"), AcSpace::ToCells(250.0), 2.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcSpaceYawTest, "Autocraft.Space.Yaw", Flags)
bool FAcSpaceYawTest::RunTest(const FString&)
{
	for (int32 I = -12; I <= 12; ++I)
	{
		const double Heading = I * (UE_DOUBLE_PI / 6.0) + 0.1;
		// The sim's facing direction on the ground.
		const FVector Facing = AcSpace::DirectionToWorld(ac::Vec2(std::cos(Heading), std::sin(Heading)));
		// A model facing +X, turned by the heading, faces it.
		TestTrue(TEXT("QuatFromHeading turns +X to the heading"), Near(AcSpace::QuatFromHeading(Heading).RotateVector(FVector::ForwardVector), Facing, 1e-12));
		TestTrue(TEXT("RotatorFromHeading turns +X to the heading"), Near(AcSpace::RotatorFromHeading(Heading).Vector(), Facing, 1e-9));
		TestTrue(TEXT("yaw = +heading in degrees"), FMath::IsNearlyEqual(AcSpace::YawFromHeading(Heading), FMath::RadiansToDegrees(Heading), 1e-12));
		TestTrue(TEXT("heading round trip"), FMath::IsNearlyEqual(AcSpace::HeadingFromYaw(AcSpace::YawFromHeading(Heading)), Heading, 1e-12));
		// SceneKit turns the node with eulerAngles.y = -heading: a rotation
		// about SceneKit Y by -heading. Mapped over, it is the same turn.
		const double A = -Heading;
		const FQuat SkYaw(0.0, std::sin(A / 2), 0.0, std::cos(A / 2));
		const FQuat Mapped = AcSpace::QuatFromSceneKit(SkYaw.X, SkYaw.Y, SkYaw.Z, SkYaw.W);
		TestTrue(TEXT("SceneKit eulerAngles.y = -heading maps to QuatFromHeading"),
			Mapped.Equals(AcSpace::QuatFromHeading(Heading), 1e-12) || Mapped.Equals(-AcSpace::QuatFromHeading(Heading), 1e-12));
	}
	// Quarter turns, spelled out: heading π/2 is sim +y, Unreal +Y.
	TestTrue(TEXT("heading pi/2 faces +Y"), Near(AcSpace::QuatFromHeading(UE_DOUBLE_HALF_PI).RotateVector(FVector::ForwardVector), FVector(0, 1, 0), 1e-12));
	TestTrue(TEXT("yaw 90 faces +Y"), Near(FRotator(0, 90, 0).Vector(), FVector(0, 1, 0), 1e-9));
	// A building faces SceneKit +Z: Unreal +Y.
	TestTrue(TEXT("SceneKit +Z is Unreal +Y"), Near(AcSpace::AxesFromSceneKit(0, 0, 1), FVector(0, 1, 0)));
	TestTrue(TEXT("SceneKit +Y (up) is Unreal +Z"), Near(AcSpace::AxesFromSceneKit(0, 1, 0), FVector(0, 0, 1)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcSpaceSceneKitRotationTest, "Autocraft.Space.SceneKitRotation", Flags)
bool FAcSpaceSceneKitRotationTest::RunTest(const FString&)
{
	// For any SceneKit rotation q and point v: mapping q·v gives the mapped
	// rotation applied to the mapped point (the reflection keeps the look).
	FRandomStream Rng(1234);
	for (int32 I = 0; I < 200; ++I)
	{
		FQuat Sk(Rng.FRandRange(-1, 1), Rng.FRandRange(-1, 1), Rng.FRandRange(-1, 1), Rng.FRandRange(-1, 1));
		Sk.Normalize();
		const FVector V(Rng.FRandRange(-5, 5), Rng.FRandRange(-5, 5), Rng.FRandRange(-5, 5));
		// SceneKit's math is the standard quaternion rotation, as FQuat's.
		const FVector SkTurned = Sk.RotateVector(V);
		const FQuat Ue = AcSpace::QuatFromSceneKit(Sk.X, Sk.Y, Sk.Z, Sk.W);
		const FVector A = AcSpace::FromSceneKit(SkTurned.X, SkTurned.Y, SkTurned.Z);
		const FVector B = Ue.RotateVector(AcSpace::FromSceneKit(V.X, V.Y, V.Z));
		TestTrue(FString::Printf(TEXT("rotation %d maps"), I), Near(A, B, 1e-6));
		TestTrue(TEXT("quaternion round trip"), AcSpace::QuatToSceneKit(Ue).Equals(Sk, 1e-12));
		// A whole transform: scale (uniform here, as SceneKit models use), turn, move.
		const FVector SkPos(Rng.FRandRange(-9, 9), Rng.FRandRange(0, 3), Rng.FRandRange(-9, 9));
		const double S = Rng.FRandRange(0.5, 2.0);
		const FTransform T = AcSpace::TransformFromSceneKit(SkPos, Sk, FVector(S));
		const FVector SkWorld = SkPos + Sk.RotateVector(V * S);
		TestTrue(TEXT("transform maps"), Near(T.TransformPosition(AcSpace::FromSceneKit(V.X, V.Y, V.Z)), AcSpace::FromSceneKit(SkWorld.X, SkWorld.Y, SkWorld.Z), 1e-4));
	}
	return true;
}

#endif
