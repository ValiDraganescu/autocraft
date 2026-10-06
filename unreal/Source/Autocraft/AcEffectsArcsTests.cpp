// Automation tests for the shells in flight (AcEffectsArcs.h, Effects.swift
// `launch`/`fly`).
//   UnrealEditor Autocraft.uproject -nullrhi -unattended -nosplash -nosound
//     -ExecCmds="Automation RunTests Autocraft.Effects.Arcs; Quit"
#include "AcEffectsArcs.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	struct FLanding
	{
		double Time;
		FVector At;
		int32 Tag;
	};

	FAcLaunch Make(const FVector& From, const FVector& To, const double Time, const double Flight, const int32 Tag,
		TArray<FLanding>& Log)
	{
		FAcLaunch L;
		L.From = From;
		L.To = To;
		L.Time = Time;
		L.Flight = Flight;
		L.Arc = 1.0;
		L.Land = [&Log, Tag](const double T, const FVector& At) { Log.Add({T, At, Tag}); };
		return L;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcArcsPathTest, "Autocraft.Effects.Arcs.Path", Flags)
bool FAcArcsPathTest::RunTest(const FString&)
{
	const FVector A(0, 0, 50), B(1000, 0, 50);
	TestTrue(TEXT("starts at the muzzle"), FAcArcFlights::Position(A, B, 100, 0).Equals(A));
	TestTrue(TEXT("ends at the target"), FAcArcFlights::Position(A, B, 100, 1).Equals(B));
	TestTrue(TEXT("arc height at the middle"), FAcArcFlights::Position(A, B, 100, 0.5).Equals(FVector(500, 0, 150)));
	TestTrue(TEXT("climbs at first"), FAcArcFlights::Velocity(A, B, 100, 0).Z > 0);
	TestTrue(TEXT("level at the top"), FMath::IsNearlyZero(FAcArcFlights::Velocity(A, B, 100, 0.5).Z));
	TestTrue(TEXT("falls at the end"), FAcArcFlights::Velocity(A, B, 100, 1).Z < 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcArcsTimingTest, "Autocraft.Effects.Arcs.Timing", Flags)
bool FAcArcsTimingTest::RunTest(const FString&)
{
	// It lands once, at start + flight (when the damage lands), not at the
	// frame that noticed.
	TArray<FLanding> Log;
	FAcArcFlights F(4);
	F.Launch(Make(FVector::ZeroVector, FVector(600, 0, 0), 10.0, 0.5, 1, Log));
	int32 Placed = 0;
	auto Place = [&Placed](const FAcArcFlights::FFlight&, const FVector&, const FVector&) { ++Placed; };
	auto Landed = [](const FAcArcFlights::FFlight&) {};
	F.Fly(10.2, Place, Landed);
	TestEqual(TEXT("in the air"), Log.Num(), 0);
	TestEqual(TEXT("placed"), Placed, 1);
	F.Fly(10.53, Place, Landed);
	TestEqual(TEXT("landed once"), Log.Num(), 1);
	TestEqual(TEXT("at start + flight"), Log[0].Time, 10.5);
	F.Fly(10.6, Place, Landed);
	TestEqual(TEXT("not twice"), Log.Num(), 1);
	TestEqual(TEXT("nothing flies"), F.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcArcsHomingTest, "Autocraft.Effects.Arcs.Homing", Flags)
bool FAcArcsHomingTest::RunTest(const FString&)
{
	// It follows the target while `Track` answers, and comes down where it
	// last saw it once it is gone.
	TArray<FLanding> Log;
	FAcArcFlights F(2);
	FAcLaunch L = Make(FVector::ZeroVector, FVector(600, 0, 0), 0.0, 1.0, 1, Log);
	TOptional<FVector> Target = FVector(600, 0, 0);
	L.Track = [&Target] { return Target; };
	F.Launch(MoveTemp(L));
	auto Place = [](const FAcArcFlights::FFlight&, const FVector&, const FVector&) {};
	auto Landed = [](const FAcArcFlights::FFlight&) {};
	Target = FVector(600, 300, 0);
	F.Fly(0.5, Place, Landed);
	TestTrue(TEXT("homes"), F.All()[0].To.Equals(FVector(600, 300, 0)));
	Target.Reset();
	F.Fly(1.0, Place, Landed);
	TestEqual(TEXT("landed"), Log.Num(), 1);
	TestTrue(TEXT("where it last saw it"), Log[0].At.Equals(FVector(600, 300, 0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcArcsReuseTest, "Autocraft.Effects.Arcs.Reuse", Flags)
bool FAcArcsReuseTest::RunTest(const FString&)
{
	// Two slots: the third launch takes the first's slot and lands it now.
	TArray<FLanding> Log;
	FAcArcFlights F(2);
	TestEqual(TEXT("slot 0"), F.Launch(Make(FVector::ZeroVector, FVector(100, 0, 0), 0.0, 5.0, 1, Log)), 0);
	TestEqual(TEXT("slot 1"), F.Launch(Make(FVector::ZeroVector, FVector(200, 0, 0), 0.1, 5.0, 2, Log)), 1);
	TestEqual(TEXT("slot 0 again"), F.Launch(Make(FVector::ZeroVector, FVector(300, 0, 0), 0.2, 5.0, 3, Log)), 0);
	TestEqual(TEXT("the old one landed"), Log.Num(), 1);
	TestEqual(TEXT("it was the first"), Log[0].Tag, 1);
	TestEqual(TEXT("at the new launch's time"), Log[0].Time, 0.2);
	TestTrue(TEXT("where it was headed"), Log[0].At.Equals(FVector(100, 0, 0)));
	TestEqual(TEXT("two fly"), F.Num(), 2);
	F.Clear();
	TestEqual(TEXT("cleared without landing"), Log.Num(), 1);
	return true;
}

#endif
