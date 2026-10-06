// `AcPilotAim`: what the driven unit's guns and the pilot's eye do with the
// crosshair (GAME-LAYER.md §2.12, chunk E4). Plain math, no actors:
//
// - `Turn` is Swift's `Aim.turn` (Sources/Autocraft/Aim.swift): the turn
//   about a part's pivot that puts its bore (an axis through a muzzle that
//   may sit off the pivot) through a point. `Held` applies it to a pose
//   the way `GameScene.aimParts` does (the driven Comet's pistols, the
//   driven Juggernaut's launchers: AcPoseVehicles.cpp); the driven Ranger's
//   rifle takes the crosshair as its aim point (`rangerAim`,
//   AcPoseInfantry.cpp). The renderer hands the crosshair's point to the
//   driven unit's pose as `FAcPoseContext::PilotAim`
//   (`UAcWorldRenderer::SetPilotAim`, set by `AAcPilotPawn::Steer`).
// - `ScreenOffset` and `GunMarker` are `PilotCamera.screenOffset` and
//   `GameController.gunMarker`: where a vehicle's gun really points, off
//   the view's centre in half the view's height (+y up), for the overlay
//   (E6 draws it: `AAcPilotPawn::GunMarker()`).
// - `Facing` is `pilotFacing`: third person, the unit turns toward what the
//   view's centre is on.
// - `RangeCue` is `GameController.rangeCue`: the driven unit's reach on the
//   ground (drawn by `AAcPilotAids`, AcPilotAids.h).
#pragma once

#include "CoreMinimal.h"

#include "Types.h"

struct FAcModelInfo;
struct FAcPose;
struct FAcPoseContext;
namespace ac
{
	class Simulation;
	struct PilotSight;
}

/// How far the driven unit's weapon (or heal) reaches on the ground
/// (`RangeCue`, RangeRing.swift), cells.
struct FAcRangeCue
{
	enum class ETone : uint8
	{
		Idle,
		Lock,
		Friend
	};
	ac::Vec2 Center;
	/// From the unit's centre to where a target's edge comes into reach.
	double Radius = 0;
	/// Inside this nothing can be hit (an anchored Longbow's); 0 for most.
	double MinRadius = 0;
	/// The unit's own footprint: the disc starts past it.
	double Inner = 0;
	ETone Tone = ETone::Idle;
	/// On its way into or out of anchored mode: drawn fainter.
	bool bPending = false;
};

namespace AcPilotAim
{
	/// `Aim.turn`: the turn (in the part's parent frame) that swings a part
	/// about `Pivot` from orientation `Q`, so its line of fire, along `Axis`
	/// (the part's own frame) through `Muzzle` (the part's own frame, before
	/// `Scale`), runs through `Target` (parent frame). Identity for a target
	/// within reach of the bore's offset. Any one consistent frame and unit.
	AUTOCRAFT_API FQuat Turn(const FVector& Pivot, const FQuat& Q, const FVector& Scale, const FVector& Muzzle,
		const FVector& Axis, const FVector& Target);

	/// A part's world transform (cm) as posed so far: its chain of `Local`s
	/// up to the root, then the placement.
	AUTOCRAFT_API FTransform PartWorld(const FAcModelInfo& Model, const FAcPose& Pose, int32 Part);

	/// `aimParts` for one part about to be applied with SceneKit values
	/// (`PosSk` cells, `QSk`, `ScaleSk`, relative to its parent): the turn to
	/// put before `QSk` (new orientation = turn × QSk) so the bore, along
	/// `AxisSk` through the child part `Muzzle` (at its rest position),
	/// runs through `C.PilotAim`. Identity unless `C` is the driven unit
	/// with a crosshair. The parent must already be posed.
	AUTOCRAFT_API FQuat Held(const FAcPoseContext& C, const FAcPose& Pose, int32 Part, const FVector& PosSk, const FQuat& QSk,
		const FVector& ScaleSk, int32 Muzzle, const FVector& AxisSk);

	/// Where world point `P` shows, off the view's centre in half the view's
	/// height (+x right, +y up), for a camera at `Eye` turned `View` (UE:
	/// looking along +X) with a vertical field of view of `VerticalFov`
	/// degrees; unset behind the camera.
	AUTOCRAFT_API TOptional<FVector2D> ScreenOffset(const FVector& Eye, const FQuat& View, double VerticalFov, const FVector& P);

	/// `gunMarker`: for a kind that steers (a vehicle's gun), where its
	/// weapon's line meets what is on it (or its reach, or what the view's
	/// centre is on), seen from the camera. `LineFrom` is where the line
	/// starts (the chase eye in third person, else the camera); `Aim` the
	/// crosshair's point in third person (unset in first).
	AUTOCRAFT_API TOptional<FVector2D> GunMarker(const ac::Simulation& Sim, const ac::Unit& U, const FVector& Camera,
		const FQuat& View, double VerticalFov, const FVector& LineFrom, const TOptional<FVector>& Aim);

	/// `pilotFacing`: third person (`Aim` set), toward the crosshair's point
	/// once it is more than 1.2 cells off; else the mouse's yaw.
	AUTOCRAFT_API double Facing(const ac::Unit& U, double Yaw, const TOptional<FVector>& Aim);

	/// `rangeCue`: the ring for the driven unit (unset: none).
	AUTOCRAFT_API TOptional<FAcRangeCue> RangeCue(const ac::Simulation& Sim, const ac::Unit& U);
}
