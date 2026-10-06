// `AcPoseNew`: the poses of the Peregrine, the Atlas and the Scorpion
// (docs/new-units.md "Looks"), and the plain functions they are made of.
//
// Poses (registered with `FAcPoseRegistration`, one file each):
//   AcPosePeregrine.cpp  PosePeregrine
//   AcPoseAtlas.cpp      PoseAtlas
//   AcPoseScorpion.cpp   PoseScorpion
// They move the parts `art/models/<unit>/parts.txt` names, in the Unreal
// frame of the export (x ahead, y right, z up, cm): a part's origin is its
// pivot, so a part turns about its pivot by setting its local rotation.
// Rotations here are written as the part's turn in its parent's frame:
//   RotX(a): a > 0 lifts the right side (a roll to the left),
//   RotY(a): a > 0 sends +X down (a nose down, a foot back),
//   RotZ(a): a > 0 turns +X toward +Y (a yaw to the right).
//
// The functions below hold the numbers: no parts, no model, so the
// automation tests (AcPoseNewKindsTests.cpp) can check the motion itself.
// Cues the poses send (`FAcPoseCue::What`): `AtlasStep` (a foot came down:
// `At` the foot's ground point, cm; `Value` 0 left, 1 right).
#pragma once

#include "CoreMinimal.h"

#include "AcPose.h"

namespace AcPoseNew
{
	// --- Peregrine --------------------------------------------------------------

	/// A Peregrine flies at this height over the ground it follows, cells
	/// (a Kestrel 2.1, a Dropship 2.4): it hunts flyers, so it flies high.
	inline constexpr double PeregrineHover = 1.9;
	/// The hardest bank: 60 degrees (the Kestrel's 29).
	inline constexpr double PeregrineMaxBank = UE_DOUBLE_PI / 3.0;
	/// The smoothed turn rate (rad/s) that banks it all the way.
	inline constexpr double PeregrineBankTurn = 2.2;
	/// Seconds a missile stays off its rail after a volley, and the part of
	/// it spent sliding back on.
	inline constexpr double MissileReload = 1.0;
	inline constexpr double MissileSlide = 0.25;

	/// The roll for a smoothed turn rate: radians, positive with the right
	/// wing down (a turn to the right), at most `PeregrineMaxBank`.
	AUTOCRAFT_API double PeregrineBank(double TurnRate);
	/// Which pair of rails a volley fired at `ShotTime` (the renderer's
	/// `LastShot`) leaves: 0 the outer pair (missiles_0 and missiles_3), 1
	/// the inner pair (missiles_1 and missiles_2).
	AUTOCRAFT_API int32 MissilePair(double ShotTime);
	/// The rail's missile `Rail` (0-3), `Since` seconds after the volley at
	/// `ShotTime` (the volley's pair is gone).
	struct FMissile
	{
		bool bShown = true;
		/// 0-1 while it slides back on after the reload.
		double Scale = 1.0;
		/// How far behind its place it is (cm).
		double Back = 0.0;
	};
	AUTOCRAFT_API FMissile MissileOnRail(int32 Rail, double ShotTime, double Since);
	/// The red wing-tip beacon's emission (a share of its exported glow),
	/// `Side` 0 or 1 blinking in turn, `T` seconds.
	AUTOCRAFT_API double PeregrineBeacon(double T, int32 Side);

	// --- Atlas ------------------------------------------------------------------

	/// Cells walked in one full gait cycle (both legs have stepped once).
	inline constexpr double AtlasCycle = 2.5;
	/// A leg `Cycle` (0-1) of the way round its gait; 0 is the foot coming
	/// down. Angles in radians: `Hip` positive with the foot forward, `Knee`
	/// positive bending the shin back, `Ankle` the foot's counter-turn that
	/// keeps the sole flat; `Lift` 0-1 how high the foot is off the ground.
	struct FAtlasLeg
	{
		double Hip = 0.0, Knee = 0.0, Ankle = 0.0, Lift = 0.0;
		bool bStance = true;
	};
	AUTOCRAFT_API FAtlasLeg AtlasLeg(double Cycle);
	/// The share of a cycle a foot is on the ground.
	inline constexpr double AtlasStance = 0.56;

	/// The Quake stomp, `Since` seconds after the event (`stompedAt`): the
	/// leg rises, then drives down at `AtlasStompImpact`, and the body
	/// settles by `AtlasStompTotal`. The ring, dust and shake of
	/// `UAcEffects::Stomp` wait for the impact.
	inline constexpr double AtlasStompImpact = 0.30;
	inline constexpr double AtlasStompTotal = 0.95;
	struct FAtlasStomp
	{
		/// The raised leg: hip forward, knee flexed, ankle counter-turn.
		double Hip = 0.0, Knee = 0.0, Ankle = 0.0;
		/// The standing leg's knee bends as it takes the weight.
		double Support = 0.0;
		/// The body's drop (cm, positive down) and pitch forward (radians).
		double Drop = 0.0, Pitch = 0.0;
		/// 0-1: how far the stomp has taken over from the walk.
		double Weight = 0.0;
		/// 0-1 for the knee lamps' flare, peaking at the impact.
		double Flare = 0.0;
	};
	AUTOCRAFT_API FAtlasStomp AtlasStomp(double Since);
	/// A cannon's recoil `Since` seconds after its shot, 0-1 (1 at the
	/// deepest); the second cannon fires `AtlasSecondDelay` after the first.
	inline constexpr double AtlasSecondDelay = 0.1;
	inline constexpr double AtlasRecoilCm = 32.0;
	AUTOCRAFT_API double AtlasRecoil(double Since);
	/// The back vents' emission scale `Since` seconds after a volley: hot at
	/// once, fading over about three seconds.
	AUTOCRAFT_API double AtlasVentGlow(double Since);
	/// The searchlight's emission scale for a darkness 0 (day) to 1 (night).
	AUTOCRAFT_API double AtlasSearchlight(double Dark);

	// --- Scorpion ---------------------------------------------------------------

	/// Cells crawled in one full gait cycle.
	inline constexpr double ScorpionCycle = 1.2;
	/// Which tripod leg `Leg` (0-5: left front, middle, rear, then right
	/// front, middle, rear) walks with: the left front, right middle and left
	/// rear (0), or the other three (1).
	AUTOCRAFT_API int32 TripodOf(int32 Leg);
	/// A leg `Cycle` (0-1) round its gait: `Swing` (radians, positive
	/// forward) and `Lift` 0-1; 0 is the foot coming down.
	struct FScorpionLeg
	{
		double Swing = 0.0, Lift = 0.0;
	};
	AUTOCRAFT_API FScorpionLeg ScorpionLeg(double Cycle);
	/// How far the body is below its rest when it is `Anchor` (0-1) of the
	/// way buried, cm: down to its back plates.
	inline constexpr double ScorpionSinkCm = 44.0;
	AUTOCRAFT_API double ScorpionSink(double Anchor);
	/// The mound's size 0-1 for the same `Anchor`: it heaps up as the body
	/// goes down.
	AUTOCRAFT_API double ScorpionMound(double Anchor);
	/// The tail's curl (0 up and over the back as built, 1 laid back flat)
	/// for how far it is buried and how far its lock has come (`Lock` 0-1
	/// of the second): it rises for the lock.
	AUTOCRAFT_API double ScorpionTailLaid(double Anchor, double Lock);
	/// The tail snaps forward `Since` seconds after the sting: 0-1 peak at
	/// once, back to nothing in a quarter of a second.
	AUTOCRAFT_API double ScorpionSnap(double Since);
}
