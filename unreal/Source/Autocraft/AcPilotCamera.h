// `FAcPilotCamera`: the first-person eye of a driven unit (GAME-LAYER.md
// §2.12, chunk E2), the port of `PilotCamera` (Sources/Autocraft/
// PilotCamera.swift): where the eye sits on each kind (`Eye`), what it turns
// about with a turret (`Pivot`), the pitch a ride starts at and its limits,
// the Comet's run bob taken off, a machine's eye eased over the ground's
// bumps (9/s, real time), and the cockpit's `ViewLean` (roll and lift eased
// at 5/s for machines, shake sharp). Plain math: `AAcPilotPawn` feeds it the
// model root's world transform and puts its camera where `Follow` says.
//
// The Swift math runs in SceneKit axes (Y up, the model facing +X, its right
// side +Z, a camera looking down −Z); the result is turned into Unreal axes
// once at the end (AcSpace.h). Kind-neutral: a new drivable kind needs a
// case in `Eye` (and `Pivot` if it has a turret), as in Swift.
//
// For later chunks: E3 (third person) adds `chase` behind `bThirdPerson`
// (`Boom` and `ChaseYaw` are here already); E5 (cockpits) calls `SetLean`
// each frame with what its pose returns, and reads `Turn`/`Speed`.
#pragma once

#include "CoreMinimal.h"

#include "Types.h"

/// How the view leans with the machine (`ViewLean`, Models+Cockpit2.swift).
struct FAcViewLean
{
	/// Roll about the line of sight, radians (+: the left side down).
	float Roll = 0.f;
	/// Eye height change, cells.
	float Lift = 0.f;
	/// Small turns of the view (pitch, yaw), radians: shake and jolts.
	FVector2f Shake = FVector2f::ZeroVector;
};

class AUTOCRAFT_API FAcPilotCamera
{
public:
	/// The Swift camera: 62° vertical, near 0.04 cells, far 260 cells.
	static constexpr double FovDegrees = 62.0;
	static constexpr double NearCells = 0.04;
	static constexpr double FarCells = 260.0;
	/// The horizontal field of view (degrees) that gives `FovDegrees`
	/// vertically on a view `Aspect` wide.
	static double HorizontalFov(double Aspect);

	/// Where the eye sits on a unit, in its model's frame (SceneKit axes,
	/// cells, before the model's own scale; `PilotCamera.eye`).
	static FVector Eye(ac::UnitKind Kind);
	/// What the eye turns about with `look − heading` (`PilotCamera.pivot`).
	static FVector Pivot(ac::UnitKind Kind);
	/// How far the walk lifts the model now (cells), taken off the eye.
	static double Bob(const ac::Unit& U);
	/// The pitch a ride starts at.
	static double StartPitch(ac::UnitKind Kind);
	/// How far the mouse may pitch the view.
	static void PitchLimits(ac::UnitKind Kind, double& OutLow, double& OutHigh);
	/// Third person (E3): how much higher and longer the boom is.
	static void Boom(ac::UnitKind Kind, float& OutLift, float& OutLength);

	/// Look up (+) or down (−), radians.
	double Pitch = -0.22;
	/// Third person (E3): where the camera looks across the ground.
	double ChaseYaw = 0.0;
	bool bThirdPerson = false;

	/// A new ride (`mount`): no lean, nothing eased yet.
	void Reset();

	/// The eye on the unit (`follow`): `Root` is the model root's world
	/// transform (UE cm: the unit's placement × its root part's rest),
	/// `U` the unit with `look` as the view should follow it, `RealNow`
	/// real seconds (the suspension's easing). Out: the camera's location
	/// and rotation (UE: looking along +X).
	void Follow(const FTransform& Root, const ac::Unit& U, double RealNow, FVector& OutLocation, FQuat& OutRotation);

	/// The cockpit's lean this frame (E5), eased for a machine (`ease`).
	void SetLean(const FAcViewLean& InLean, bool bBio, double Time);
	const FAcViewLean& Lean() const { return CurrentLean; }

	/// How fast the driven unit turns (rad/s) and moves (cells/s), smoothed
	/// (`track`); call once a frame with the game time.
	void Track(const ac::Unit& U, double Time);
	double Turn() const { return TurnRate; }
	double Speed() const { return SpeedRate; }

private:
	FAcViewLean CurrentLean;
	TOptional<TPair<FAcViewLean, double>> Eased;
	TOptional<TPair<double, double>> EyeZ;
	struct FLast
	{
		int64 Id = 0;
		double Heading = 0, Stride = 0, Time = 0;
	};
	TOptional<FLast> Last;
	double TurnRate = 0.0, SpeedRate = 0.0;
};
