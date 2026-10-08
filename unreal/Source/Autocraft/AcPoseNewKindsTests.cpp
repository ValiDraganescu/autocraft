// Automation tests for the Peregrine's, the Atlas's and the Scorpion's poses
// (AcPoseNewKinds.h), the Atlas's fall (AcShatter.h) and what a kind
// borrows (AcNewKinds.h). Run headless:
//   UnrealEditor Autocraft.uproject -nullrhi -unattended -nosplash -nosound
//     -ExecCmds="Automation RunTests Autocraft.PoseNew; Quit"
#include "AcEffectsShatter.h"
#include "AcModelCatalog.h"
#include "AcNewKinds.h"
#include "AcPoseNewKinds.h"
#include "AcShatter.h"

#include "Misc/AutomationTest.h"

#include "TerrainField.h"
#include "Types.h"
#include "WindowMaps.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/// A kind's pose on flat ground for the state `Setup` makes: the pose and
	/// the cues it sent. Memory is the caller's, so two frames of one unit can follow.
	struct FPoseScene
	{
		ac::TerrainField Field{ac::WindowMaps::playground()};
		ac::GameState State;
		FAcUnitMemory Memory;
		const FAcModelInfo* Model = nullptr;

		explicit FPoseScene(const TCHAR* ModelName)
		{
			Model = FAcModelCatalog::Get().Find(FName(ModelName));
			State.time = 10.0;
		}

		FAcPose Pose(const ac::Unit& U, const double Dt = 0.0) const
		{
			FAcPose P;
			FAcPoseContext C{State, &U, nullptr, *Model, Field, const_cast<FAcUnitMemory&>(Memory), 10.0, Dt, false, 0};
			AcPose::RestUnit(C, P);
			if (const FAcPoseFn Fn = AcPose::Find(U.kind)) Fn(C, P);
			return P;
		}

		int32 Part(const TCHAR* Name) const
		{
			const int32* I = Model->PartIndex.Find(FName(Name));
			return I ? *I : INDEX_NONE;
		}
	};

	bool Needs(FAutomationTestBase& T, const FAcModelInfo* M, const TCHAR* Model, std::initializer_list<const TCHAR*> Parts)
	{
		if (!M)
		{
			T.AddError(FString::Printf(TEXT("no model %s in the catalog"), Model));
			return false;
		}
		bool bAll = true;
		for (const TCHAR* P : Parts)
		{
			if (!M->PartIndex.Contains(FName(P)))
			{
				T.AddError(FString::Printf(TEXT("%s has no part %s (the pose moves it)"), Model, P));
				bAll = false;
			}
		}
		return bAll;
	}

	/// The roll about +X (positive: the right side up) of a part's rotation.
	double RollOf(const FQuat& Q) { return std::atan2(-Q.RotateVector(FVector(0, 0, 1)).Y, Q.RotateVector(FVector(0, 0, 1)).Z); }
	/// The pitch about +Y (positive: +X goes down).
	double PitchOf(const FQuat& Q) { return std::atan2(-Q.RotateVector(FVector(1, 0, 0)).Z, Q.RotateVector(FVector(1, 0, 0)).X); }
	double YawOf(const FQuat& Q) { return std::atan2(Q.RotateVector(FVector(1, 0, 0)).Y, Q.RotateVector(FVector(1, 0, 0)).X); }
}

// --- Peregrine -----------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcPoseNewBankTest, "Autocraft.PoseNew.Bank", Flags)
bool FAcPoseNewBankTest::RunTest(const FString&)
{
	using namespace AcPoseNew;
	TestEqual(TEXT("level in a straight line"), PeregrineBank(0.0), 0.0, 1e-12);
	TestEqual(TEXT("banks to 60 degrees"), FMath::RadiansToDegrees(PeregrineBank(5.0)), 60.0, 1e-9);
	TestEqual(TEXT("and the same to the left"), PeregrineBank(-5.0), -PeregrineBank(5.0), 1e-12);
	TestTrue(TEXT("harder than the Kestrel's 0.5 rad"), PeregrineBank(PeregrineBankTurn) > 0.5);
	TestTrue(TEXT("a gentle turn banks less"), PeregrineBank(0.5) < PeregrineBank(1.0));
	TestTrue(TEXT("it hovers higher than a Kestrel"), PeregrineHover < 2.4 && PeregrineHover > 1.0);
	TestEqual(TEXT("its hover is the pose's"), AcPose::Hover(ac::UnitKind::peregrine), PeregrineHover, 1e-12);
	TestEqual(TEXT("the beacons blink in turn"), PeregrineBeacon(0.05, 0) > PeregrineBeacon(0.05, 1), true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcPoseNewMissilesTest, "Autocraft.PoseNew.Missiles", Flags)
bool FAcPoseNewMissilesTest::RunTest(const FString&)
{
	using namespace AcPoseNew;
	for (const double Shot : {10.0, 10.001, 11.25, 12.5})
	{
		int32 Gone = 0;
		for (int32 Rail = 0; Rail < 4; ++Rail)
		{
			if (!MissileOnRail(Rail, Shot, 0.1).bShown) ++Gone;
		}
		TestEqual(TEXT("a volley takes two missiles off their rails"), Gone, 2);
		// The pair is one on each wing: rails 0, 1 are the left, 2, 3 the right.
		const int32 Pair = MissilePair(Shot);
		TestTrue(TEXT("one from each wing"), (!MissileOnRail(Pair == 0 ? 0 : 1, Shot, 0.1).bShown) && (!MissileOnRail(Pair == 0 ? 3 : 2, Shot, 0.1).bShown));
		TestFalse(TEXT("still gone just before the reload"), MissileOnRail(Pair == 0 ? 0 : 1, Shot, MissileReload - 0.01).bShown);
		const FMissile Back = MissileOnRail(Pair == 0 ? 0 : 1, Shot, MissileReload + 0.5 * MissileSlide);
		TestTrue(TEXT("sliding back on"), Back.bShown && Back.Scale > 0.0 && Back.Scale < 1.0 && Back.Back > 0.0);
		const FMissile Home = MissileOnRail(Pair == 0 ? 0 : 1, Shot, MissileReload + MissileSlide + 0.01);
		TestTrue(TEXT("on its rail again"), Home.bShown && Home.Scale == 1.0 && Home.Back == 0.0);
	}
	TestTrue(TEXT("the pair alternates with the shot"), MissilePair(10.0) != MissilePair(10.001));
	TestTrue(TEXT("nothing hidden before any shot"), MissileOnRail(0, 10.0, -1.0).bShown);
	return true;
}

// --- Atlas ---------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcPoseNewAtlasGaitTest, "Autocraft.PoseNew.AtlasGait", Flags)
bool FAcPoseNewAtlasGaitTest::RunTest(const FString&)
{
	using namespace AcPoseNew;
	const AcPoseNew::FAtlasLeg Land = AtlasLeg(0.0);
	TestTrue(TEXT("the foot comes down forward of the hip"), Land.Hip > 0.3 && Land.bStance && Land.Lift == 0.0);
	TestEqual(TEXT("the same foot a cycle later"), AtlasLeg(1.0).Hip, Land.Hip, 1e-9);
	double Last = AtlasLeg(0.0).Hip;
	for (double C = 0.01; C < AtlasStance; C += 0.01)
	{
		const AcPoseNew::FAtlasLeg L = AtlasLeg(C);
		TestTrue(TEXT("in stance the hip goes over the foot (it swings back)"), L.Hip <= Last + 1e-9 && L.bStance && L.Lift == 0.0);
		Last = L.Hip;
	}
	double MaxLift = 0.0;
	for (double C = AtlasStance; C < 1.0; C += 0.01)
	{
		const AcPoseNew::FAtlasLeg L = AtlasLeg(C);
		MaxLift = FMath::Max(MaxLift, L.Lift);
		TestFalse(TEXT("the foot is in the air"), L.bStance && C > AtlasStance + 0.02);
	}
	TestTrue(TEXT("the swing lifts the foot"), MaxLift > 0.99);
	// The ankle keeps the sole flat: hip, knee and ankle turns add up to none.
	for (double C = 0.0; C < 1.0; C += 0.05)
	{
		const AcPoseNew::FAtlasLeg L = AtlasLeg(C);
		TestEqual(TEXT("the sole stays flat"), -L.Hip + L.Knee + L.Ankle, 0.0, 1e-9);
	}
	// The two legs, half a cycle apart, are never both in the air.
	for (double C = 0.0; C < 1.0; C += 0.01)
	{
		TestTrue(TEXT("one foot is always down"), AtlasLeg(C).bStance || AtlasLeg(C + 0.5).bStance);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcPoseNewAtlasStompTest, "Autocraft.PoseNew.AtlasStomp", Flags)
bool FAcPoseNewAtlasStompTest::RunTest(const FString&)
{
	using namespace AcPoseNew;
	TestEqual(TEXT("no stomp before the event"), AtlasStomp(-0.1).Weight, 0.0, 1e-12);
	TestEqual(TEXT("no stomp after it settles"), AtlasStomp(AtlasStompTotal + 0.01).Weight, 0.0, 1e-12);
	const FAtlasStomp Up = AtlasStomp(0.2);
	TestTrue(TEXT("the leg is up before the impact"), Up.Hip > 0.5 && Up.Knee > 1.1 && Up.Weight > 0.9);
	TestTrue(TEXT("the body rises on the standing leg"), Up.Drop < 0.0);
	const FAtlasStomp Hit = AtlasStomp(AtlasStompImpact);
	TestTrue(TEXT("the foot is down at the impact"), Hit.Hip < 0.05 && Hit.Knee < 0.1);
	TestTrue(TEXT("the knee lamps flare at the impact"), Hit.Flare > 0.9);
	TestTrue(TEXT("the body drops after it"), AtlasStomp(AtlasStompImpact + 0.04).Drop > 5.0);
	TestTrue(TEXT("it is on its feet again"), FMath::Abs(AtlasStomp(AtlasStompTotal - 0.01).Drop) < 2.0);
	// The drive comes after the beat at the top.
	double DriveTime = 0.0;
	for (double S = 0.2; S <= AtlasStompImpact; S += 0.005)
	{
		if (AtlasStomp(S).Hip < 0.3) { DriveTime = S; break; }
	}
	TestTrue(TEXT("the drive comes after the top"), DriveTime > 0.24 && DriveTime < AtlasStompImpact);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcPoseNewAtlasGunsTest, "Autocraft.PoseNew.AtlasGuns", Flags)
bool FAcPoseNewAtlasGunsTest::RunTest(const FString&)
{
	using namespace AcPoseNew;
	TestEqual(TEXT("no recoil before the shot"), AtlasRecoil(-1.0), 0.0, 1e-12);
	TestTrue(TEXT("the cannon goes back"), AtlasRecoil(0.05) > 0.99);
	TestTrue(TEXT("and comes forward again"), AtlasRecoil(0.6) < 0.1);
	TestTrue(TEXT("the vents glow after a volley"), AtlasVentGlow(0.0) > 3.0 * AtlasVentGlow(-1.0));
	TestTrue(TEXT("and fade"), AtlasVentGlow(0.5) > AtlasVentGlow(2.0) && AtlasVentGlow(2.0) > AtlasVentGlow(10.0));
	TestTrue(TEXT("the searchlight burns at night"), AtlasSearchlight(1.0) > 2.0 && AtlasSearchlight(0.5) > AtlasSearchlight(0.1));
	TestTrue(TEXT("and is dim by day"), AtlasSearchlight(0.0) < 0.2);
	return true;
}

// --- Scorpion ------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcPoseNewScorpionTest, "Autocraft.PoseNew.Scorpion", Flags)
bool FAcPoseNewScorpionTest::RunTest(const FString&)
{
	using namespace AcPoseNew;
	int32 Counts[2] = {0, 0};
	for (int32 L = 0; L < 6; ++L) ++Counts[TripodOf(L)];
	TestTrue(TEXT("three legs in each tripod"), Counts[0] == 3 && Counts[1] == 3);
	// Each tripod is a left front, a right middle and a left rear, or the others.
	TestTrue(TEXT("alternate legs of a side walk together"), TripodOf(0) == TripodOf(2) && TripodOf(0) != TripodOf(1)
		&& TripodOf(4) == TripodOf(0) && TripodOf(3) == TripodOf(1) && TripodOf(5) == TripodOf(1));
	for (double C = 0.0; C < 1.0; C += 0.01)
	{
		TestFalse(TEXT("the tripods never lift together"), ScorpionLeg(C).Lift > 0.0 && ScorpionLeg(C + 0.5).Lift > 0.0);
	}
	TestTrue(TEXT("the foot comes down forward"), ScorpionLeg(0.0).Swing > 0.3);
	TestEqual(TEXT("the body is up when it is not buried"), ScorpionSink(0.0), 0.0, 1e-12);
	TestEqual(TEXT("and down to its back plates when it is"), ScorpionSink(1.0), ScorpionSinkCm, 1e-9);
	double Was = -1.0;
	for (double A = 0.0; A <= 1.0; A += 0.05)
	{
		TestTrue(TEXT("it sinks, never rises, as it digs in"), ScorpionSink(A) >= Was);
		Was = ScorpionSink(A);
	}
	TestTrue(TEXT("the mound is heaped by the time it is down"), ScorpionMound(0.0) == 0.0 && ScorpionMound(1.0) == 1.0);
	TestTrue(TEXT("the tail is laid back in the ground"), ScorpionTailLaid(1.0, 0.0) > 0.99 && ScorpionTailLaid(0.0, 0.0) == 0.0);
	TestTrue(TEXT("and rises for the lock"), ScorpionTailLaid(1.0, 1.0) < 0.01 && ScorpionTailLaid(1.0, 0.5) < ScorpionTailLaid(1.0, 0.1));
	TestTrue(TEXT("it snaps forward at the shot and settles"), ScorpionSnap(0.0) > 0.99 && ScorpionSnap(0.4) < 0.01 && ScorpionSnap(-1.0) == 0.0);
	return true;
}

// --- The poses on the models ---------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcPoseNewPartsTest, "Autocraft.PoseNew.Parts", Flags)
bool FAcPoseNewPartsTest::RunTest(const FString&)
{
	const FAcModelCatalog& Catalog = FAcModelCatalog::Get();
	if (!Catalog.IsLoaded())
	{
		AddError(TEXT("no model catalog"));
		return false;
	}
	// Every part a pose moves is in the model (a misspelt name moves nothing).
	Needs(*this, Catalog.Find(TEXT("peregrine_blue")), TEXT("peregrine_blue"),
		{TEXT("body"), TEXT("flaps_0"), TEXT("flaps_1"), TEXT("fins_0"), TEXT("fins_1"), TEXT("engines_0"), TEXT("engines_1"),
			TEXT("glow"), TEXT("glow_2"), TEXT("missiles_0"), TEXT("missiles_1"), TEXT("missiles_2"), TEXT("missiles_3"),
			TEXT("beacon_0"), TEXT("beacon_1")});
	Needs(*this, Catalog.Find(TEXT("atlas_blue")), TEXT("atlas_blue"),
		{TEXT("body"), TEXT("torso"), TEXT("vents"), TEXT("searchlight"), TEXT("cannons_0"), TEXT("cannons_1"), TEXT("hips_0"),
			TEXT("hips_1"), TEXT("knees_0"), TEXT("knees_1"), TEXT("kneeLamps_0"), TEXT("kneeLamps_1"), TEXT("ankles_0"),
			TEXT("ankles_1"), TEXT("feet_0"), TEXT("feet_1")});
	Needs(*this, Catalog.Find(TEXT("scorpion_blue")), TEXT("scorpion_blue"),
		{TEXT("body"), TEXT("eyes"), TEXT("launcher"), TEXT("tailLamp"), TEXT("mound"), TEXT("plates_0"), TEXT("plates_4"),
			TEXT("legs_0"), TEXT("legs_5"), TEXT("shins_0"), TEXT("shins_5"), TEXT("pincers_0"), TEXT("pincers_1"),
			TEXT("tail_0"), TEXT("tail_1"), TEXT("tail_2"), TEXT("tail_3")});
	for (const ac::UnitKind K : {ac::UnitKind::peregrine, ac::UnitKind::atlas, ac::UnitKind::scorpion})
	{
		TestNotNull(*FString::Printf(TEXT("pose for %s"), AcPose::ModelBase(K)), (void*)AcPose::Find(K));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcPoseNewPeregrineTest, "Autocraft.PoseNew.Peregrine", Flags)
bool FAcPoseNewPeregrineTest::RunTest(const FString&)
{
	FPoseScene Scene(TEXT("peregrine_blue"));
	if (!Scene.Model)
	{
		AddError(TEXT("no peregrine_blue"));
		return false;
	}
	ac::Unit U(7, ac::UnitKind::peregrine, 0, ac::Vec2(20, 20), 0.5, ac::Unit::Task::idle);
	Scene.Memory.LastHeading = U.heading;
	const FAcPose Level = Scene.Pose(U);
	const int32 Body = Scene.Part(TEXT("body"));
	TestEqual(TEXT("level when it does not turn"), RollOf(Level.Local[Body].GetRotation()), 0.0, 0.1);
	TestEqual(TEXT("flying at its hover"), double(Level.Local[Body].GetTranslation().Z), AcPoseNew::PeregrineHover * 100.0, 15.0);

	// A hard right turn: the right wing down, to 60 degrees.
	Scene.Memory.TurnRate = 6.0;
	const FAcPose Right = Scene.Pose(U);
	const double Roll = RollOf(Right.Local[Body].GetRotation());
	TestTrue(TEXT("a right turn drops the right wing, to about 60 degrees"), Roll < -0.95 && Roll > -1.15);
	Scene.Memory.TurnRate = -6.0;
	TestTrue(TEXT("a left turn the left"), RollOf(Scene.Pose(U).Local[Body].GetRotation()) > 0.95);
	// The flaps work against each other, the fins swing, the glow grows with speed.
	const int32 FlapL = Scene.Part(TEXT("flaps_0")), FlapR = Scene.Part(TEXT("flaps_1")), Fin = Scene.Part(TEXT("fins_0"));
	TestTrue(TEXT("the flaps swing the other way each side"),
		PitchOf(Right.Local[FlapL].GetRotation()) * PitchOf(Right.Local[FlapR].GetRotation()) < 0.0);
	TestTrue(TEXT("a fin swings with the turn"), FMath::Abs(YawOf(Right.Local[Fin].GetRotation())) > 0.2);
	Scene.Memory.TurnRate = 0.0;
	U.moving = true;
	FPoseScene Fast(TEXT("peregrine_blue"));
	Fast.Memory.LastHeading = U.heading;
	const int32 Glow = Scene.Part(TEXT("glow"));
	const FAcPose Idle = Scene.Pose(ac::Unit(8, ac::UnitKind::peregrine, 0, ac::Vec2(20, 20), 0.5, ac::Unit::Task::idle));
	const FAcPose Flying = Fast.Pose(U);
	TestTrue(TEXT("the glow is brighter and longer at speed"),
		Flying.Emission[Glow] > Idle.Emission[Glow] + 0.5f && Flying.Local[Glow].GetScale3D().X > Idle.Local[Glow].GetScale3D().X);

	// A volley takes a pair of missiles off.
	Scene.Memory.LastShot = 10.0 - 0.2;
	const FAcPose Fired = Scene.Pose(U);
	int32 Hidden = 0;
	for (int32 K = 0; K < 4; ++K) Hidden += Fired.Visible[Scene.Part(*FString::Printf(TEXT("missiles_%d"), K))] ? 0 : 1;
	TestEqual(TEXT("two missiles are off their rails"), Hidden, 2);
	Scene.Memory.LastShot = 10.0 - 5.0;
	const FAcPose Reloaded = Scene.Pose(U);
	for (int32 K = 0; K < 4; ++K) TestTrue(TEXT("and back"), Reloaded.Visible[Scene.Part(*FString::Printf(TEXT("missiles_%d"), K))]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcPoseNewAtlasTest, "Autocraft.PoseNew.Atlas", Flags)
bool FAcPoseNewAtlasTest::RunTest(const FString&)
{
	FPoseScene Scene(TEXT("atlas_blue"));
	if (!Scene.Model)
	{
		AddError(TEXT("no atlas_blue"));
		return false;
	}
	const int32 Hip0 = Scene.Part(TEXT("hips_0")), Hip1 = Scene.Part(TEXT("hips_1")), Torso = Scene.Part(TEXT("torso")),
		Knee1 = Scene.Part(TEXT("knees_1")), Cannon = Scene.Part(TEXT("cannons_0")), Body = Scene.Part(TEXT("body")),
		Vents = Scene.Part(TEXT("vents"));
	ac::Unit U(8, ac::UnitKind::atlas, 0, ac::Vec2(20, 20), 0.5, ac::Unit::Task::idle);
	Scene.Memory.LastHeading = U.heading;

	// Standing: legs straight.
	const FAcPose Stand = Scene.Pose(U);
	TestEqual(TEXT("legs straight at rest"), PitchOf(Stand.Local[Hip0].GetRotation()), 0.0, 1e-6);

	// Walking: the legs swing opposite ways.
	U.moving = true;
	U.stride = 0.3;
	const FAcPose Walk = Scene.Pose(U);
	const double H0 = PitchOf(Walk.Local[Hip0].GetRotation()), H1 = PitchOf(Walk.Local[Hip1].GetRotation());
	TestTrue(TEXT("the hips swing"), FMath::Abs(H0) > 0.05 && FMath::Abs(H1) > 0.05);
	TestTrue(TEXT("one forward, one back"), H0 * H1 < 0.0);
	TestTrue(TEXT("the body bobs down over the planted foot"), Walk.Local[Body].GetTranslation().Z < Stand.Local[Body].GetTranslation().Z);

	// A foot coming down sends the footfall cue, at the foot, at ground height.
	U.stride = 2.4;
	Scene.Pose(U);
	U.stride = 2.6;
	const FAcPose Step = Scene.Pose(U);
	int32 Steps = 0;
	for (const FAcPoseCue& Cue : Step.Cues)
	{
		if (Cue.What == FName(TEXT("AtlasStep")))
		{
			++Steps;
			TestEqual(TEXT("the print is on the ground"), double(Cue.At.Z), Scene.Field.height(U.position) * 100.0, 1.0);
			TestTrue(TEXT("and under the Atlas"), FVector::Dist2D(Cue.At, FVector(U.position.x * 100, U.position.y * 100, 0)) < 200.0);
		}
	}
	TestEqual(TEXT("a footfall as the cycle comes round"), Steps, 1);

	// The torso turns to where the sim aims, the legs keep the heading.
	ac::Unit Aimed = U;
	Aimed.moving = false;
	Aimed.aim = U.heading + 1.0;
	TestEqual(TEXT("the torso turns 1 rad"), YawOf(Scene.Pose(Aimed).Local[Torso].GetRotation()), 1.0, 1e-6);

	// A shot: the cannon kicks back, the vents glow.
	Scene.Memory.LastShot = 10.0 - 0.05;
	const FAcPose Shot = Scene.Pose(Aimed);
	TestTrue(TEXT("the cannon kicks back along its barrel"),
		Shot.Local[Cannon].GetTranslation().X < Scene.Model->Parts[Cannon].Rest.GetTranslation().X - 20.0);
	TestTrue(TEXT("the vents glow"), Shot.Emission[Vents] > Stand.Emission[Vents] + 2.0f);

	// The stomp: a leg up, then down, timed from the sim's `stompedAt`.
	Scene.Memory.LastShot.Reset();
	ac::Unit Stomping = U;
	Stomping.moving = false;
	Stomping.stompedAt = Scene.State.time - 0.2;
	const FAcPose Raised = Scene.Pose(Stomping);
	const double Raise0 = PitchOf(Raised.Local[Hip0].GetRotation()), Raise1 = PitchOf(Raised.Local[Hip1].GetRotation());
	TestTrue(TEXT("one leg is up (a hip swung forward)"), FMath::Min(Raise0, Raise1) < -0.4);
	Stomping.stompedAt = Scene.State.time - AcPoseNew::AtlasStompImpact;
	const FAcPose Down = Scene.Pose(Stomping);
	TestTrue(TEXT("and down at the impact"), FMath::Abs(PitchOf(Down.Local[Hip0].GetRotation())) < 0.1
		&& FMath::Abs(PitchOf(Down.Local[Hip1].GetRotation())) < 0.1);
	TestTrue(TEXT("the knee flexed up"), PitchOf(Raised.Local[Knee1].GetRotation()) > 0.0 || PitchOf(Raised.Local[Scene.Part(TEXT("knees_0"))].GetRotation()) > 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcPoseNewScorpionPoseTest, "Autocraft.PoseNew.ScorpionPose", Flags)
bool FAcPoseNewScorpionPoseTest::RunTest(const FString&)
{
	FPoseScene Scene(TEXT("scorpion_blue"));
	if (!Scene.Model)
	{
		AddError(TEXT("no scorpion_blue"));
		return false;
	}
	const int32 Body = Scene.Part(TEXT("body")), Mound = Scene.Part(TEXT("mound")), Leg0 = Scene.Part(TEXT("legs_0")),
		Leg1 = Scene.Part(TEXT("legs_1")), Tail1 = Scene.Part(TEXT("tail_1")), Lamp = Scene.Part(TEXT("tailLamp"));
	ac::Unit U(9, ac::UnitKind::scorpion, 0, ac::Vec2(20, 20), 0.5, ac::Unit::Task::idle);
	Scene.Memory.LastHeading = U.heading;
	const FAcPose Stand = Scene.Pose(U);
	TestFalse(TEXT("no mound while it stands"), Stand.Visible[Mound]);

	// The tripod gait: neighbouring legs on opposite groups swing opposite ways.
	ac::Unit Walk = U;
	Walk.moving = true;
	Walk.stride = 0.15;
	const FAcPose Gait = Scene.Pose(Walk);
	const double S0 = YawOf(Gait.Local[Leg0].GetRotation()), S1 = YawOf(Gait.Local[Leg1].GetRotation());
	TestTrue(TEXT("a front and a middle leg swing opposite ways"), FMath::Abs(S0) > 0.05 && S0 * S1 < 0.0);

	// Buried: down in the ground, a mound, the tail laid back.
	ac::Unit Buried = U;
	Buried.anchor = 1.0;
	Buried.anchored = true;
	const FAcPose Down = Scene.Pose(Buried);
	TestEqual(TEXT("the body is down by the sink"), double(Stand.Local[Body].GetTranslation().Z - Down.Local[Body].GetTranslation().Z),
		AcPoseNew::ScorpionSinkCm, 1.0);
	TestTrue(TEXT("a mound of soil shows"), Down.Visible[Mound]);
	TestTrue(TEXT("the tail is laid back (curled the other way)"),
		PitchOf(Down.Local[Tail1].GetRotation()) < PitchOf(Stand.Local[Tail1].GetRotation()) - 0.2);

	// Locked on a victim: the tail rises, the lamp pulses.
	ac::Unit Lock = Buried;
	Scene.State.units.push_back(ac::Unit(99, ac::UnitKind::ranger, 1, ac::Vec2(23, 20), 0.0, ac::Unit::Task::idle));
	Lock.lockTarget = 99;
	Lock.lockFrom = Scene.State.time - 1.0;
	Lock.lockAt = Scene.State.time;
	const FAcPose Aim = Scene.Pose(Lock);
	TestTrue(TEXT("the tail has risen out of the ground for the lock"),
		FMath::Abs(PitchOf(Aim.Local[Tail1].GetRotation()) - PitchOf(Stand.Local[Tail1].GetRotation())) < 0.2);
	// Reloading: the lamp blinks; ready: it burns steady.
	ac::Unit Reload = Buried;
	Reload.cooldown = 10.0;
	float Min = 1e9f, Max = -1e9f;
	for (int32 K = 0; K < 20; ++K)
	{
		FPoseScene Blink(TEXT("scorpion_blue"));
		Blink.Memory.LastHeading = Reload.heading;
		FAcPose P;
		FAcPoseContext C{Blink.State, &Reload, nullptr, *Blink.Model, Blink.Field, Blink.Memory, 10.0 + 0.1 * K, 0.0, false, 0};
		AcPose::RestUnit(C, P);
		AcPose::Find(ac::UnitKind::scorpion)(C, P);
		Min = FMath::Min(Min, P.Emission[Lamp]);
		Max = FMath::Max(Max, P.Emission[Lamp]);
	}
	TestTrue(TEXT("the tail lamp blinks while it reloads"), Max > Min + 0.8f);
	TestTrue(TEXT("and is steady when ready"), Scene.Pose(Buried).Emission[Lamp] > 1.0f);
	return true;
}

// --- Falls, effects specs, borrowed cockpits ----------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcPoseNewDeathTest, "Autocraft.PoseNew.Death", Flags)
bool FAcPoseNewDeathTest::RunTest(const FString&)
{
	AcEffectsShatter::FSpec Peregrine, Atlas, Scorpion;
	TestTrue(TEXT("a Peregrine is shot down and bursts"), AcEffectsShatter::SpecOf(ac::UnitKind::peregrine, Peregrine) && Peregrine.bFlyer);
	TestTrue(TEXT("an Atlas topples and bursts"), AcEffectsShatter::SpecOf(ac::UnitKind::atlas, Atlas) && Atlas.bTopple && !Atlas.bFlyer);
	TestTrue(TEXT("a Scorpion bursts"), AcEffectsShatter::SpecOf(ac::UnitKind::scorpion, Scorpion) && !Scorpion.bFlyer && !Scorpion.bTopple);
	TestTrue(TEXT("the Atlas is the biggest wreck"), Atlas.Size > Scorpion.Size && Atlas.Chunks > Scorpion.Chunks);

	FPoseScene Scene(TEXT("atlas_blue"));
	if (!Scene.Model)
	{
		AddError(TEXT("no atlas_blue"));
		return false;
	}
	ac::Unit U(8, ac::UnitKind::atlas, 0, ac::Vec2(20, 20), 0.5, ac::Unit::Task::idle);
	Scene.Memory.LastHeading = U.heading;
	const FAcPose Last = Scene.Pose(U);
	FAcPose Start, Knees, Over;
	AcShatter::AtlasFallPose(*Scene.Model, Last, 0.0, Start);
	AcShatter::AtlasFallPose(*Scene.Model, Last, AcShatter::AtlasKneelTime, Knees);
	AcShatter::AtlasFallPose(*Scene.Model, Last, AcShatter::AtlasFallTime, Over);
	const int32 Body = Scene.Part(TEXT("body")), Knee = Scene.Part(TEXT("knees_0")), Foot = Scene.Part(TEXT("feet_0"));
	TestTrue(TEXT("it starts as it stood"), Start.Local[Body].Equals(Last.Local[Body], 1e-3));
	TestTrue(TEXT("it drops to its knees (the body 80 cm lower)"),
		Last.Local[Body].GetTranslation().Z - Knees.Local[Body].GetTranslation().Z > 70.0);
	TestTrue(TEXT("its knees fold"), PitchOf(Knees.Local[Knee].GetRotation()) > 1.2);
	TArray<FTransform> W0, W1;
	TArray<bool> S0, S1;
	AcShatter::PartWorlds(*Scene.Model, Last, W0, S0);
	AcShatter::PartWorlds(*Scene.Model, Over, W1, S1);
	const int32 Torso = Scene.Part(TEXT("torso"));
	// Over on its face: the torso has gone forward and down, close to the ground.
	const FVector Fwd = FVector(FMath::Cos(U.heading), FMath::Sin(U.heading), 0.0);
	const double Ahead = FVector::DotProduct(W1[Torso].GetLocation() - W0[Torso].GetLocation(), Fwd);
	TestTrue(TEXT("it falls forward"), Ahead > 100.0);
	TestTrue(TEXT("and low"), W1[Torso].GetLocation().Z - W0[Foot].GetLocation().Z < 130.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcPoseNewBorrowedTest, "Autocraft.PoseNew.Borrowed", Flags)
bool FAcPoseNewBorrowedTest::RunTest(const FString&)
{
	const FAcModelCatalog& Catalog = FAcModelCatalog::Get();
	if (!Catalog.IsLoaded())
	{
		AddError(TEXT("no model catalog"));
		return false;
	}
	using ac::UnitKind;
	for (const UnitKind K : {UnitKind::peregrine, UnitKind::atlas, UnitKind::scorpion})
	{
		const FString Own = FString::Printf(TEXT("cockpit_%s_blue"), AcPose::ModelBase(K));
		const bool bHas = Catalog.Find(FName(*Own)) != nullptr;
		TestEqual(*FString::Printf(TEXT("%s is in the catalog exactly when HasCockpit says"), *Own), AcNewKinds::HasCockpit(K), bHas);
		TestEqual(TEXT("the cockpit is its own or its stand-in's"), (int32)AcNewKinds::BorrowedCockpit(K),
			(int32)(bHas ? K : AcNewKinds::CockpitStandIn(K)));
		TestTrue(TEXT("the stand-in's cockpit is there to borrow"),
			bHas || Catalog.Find(FName(*FString::Printf(TEXT("cockpit_%s_blue"), AcPose::ModelBase(AcNewKinds::CockpitStandIn(K))))));
		TestNotNull(*FString::Printf(TEXT("%s's own model is in the catalog"), AcPose::ModelBase(K)),
			Catalog.Find(FName(*FString::Printf(TEXT("%s_blue"), AcPose::ModelBase(K)))));
	}
	return true;
}

#endif
