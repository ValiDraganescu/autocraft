// `AcPoseAir`: the flyers' poses, the Dropship and the Kestrel (chunk B5,
// GAME-LAYER.md §2.6 "Flyers"). Ports of `GameScene.poseDropship`
// (GameScene.swift:832), `liftOff` (:862), the Kestrel's case in
// `GameScene.sync` (:718), and the static `pose` functions of
// `Models+Dropship.swift` (:398) and `Models+Kestrel.swift` (:281).
//
// The registered pose functions (AcPoseAir.cpp) read the state and the
// renderer memory (`AirY` from `AcPose::FlightBase`, the turn rate, `BornAt`
// for the lift off the Spacedock's pad, `CargoCount`/`UnloadedAt` for the
// ramp, `LastShot` for the pods) and call the two functions below, which
// are the Swift static `pose` functions: plain inputs → parts. The model
// preview (`AcPoseAirPreview.h`) and the cockpits can call them directly.
//
// What moves: the body (hover bob, roll into turns from the smoothed turn
// rate / 2.5, nose down in flight, a slow drift at the hover), the engine
// pods (tilt, fans), the exhaust glows (flicker), the beacons (two blinks
// every ~2 s, each ship on its own phase), the Dropship's ramp and bay
// light, its heal beam (a stretched core and halo, three pulses running
// down it, a splash at the patient's chest, the lens), the Kestrel's pods
// (kick back and flash, the right a beat after the left), the ground
// shadows (scale with the bob). Not here: the exhaust plumes (particles,
// effects chunks) and the shadow's opacity (custom data 2 is reserved).
#pragma once

#include "CoreMinimal.h"

#include "AcPose.h"

struct FAcModelInfo;

namespace AcPoseAir
{
	/// `Models.Dropship.hover`, `Models.Kestrel.hover` (cells).
	inline constexpr double DropshipHover = 2.4;
	inline constexpr double KestrelHover = 2.1;
	/// `Models.Dropship.rampOpen` (radians), `Models.Kestrel.podX` (cells).
	inline constexpr double RampOpen = 1.95;
	inline constexpr double PodX = 0.16;

	/// How far below its hover a new flyer still is `Age` seconds after it
	/// appeared in its Spacedock's bay (`GameScene.launchDrop`): out of the
	/// pad in 1.4 s from the spawn point's height; unset once it is up.
	AUTOCRAFT_API TOptional<double> LaunchDrop(double Age, double Hover);
	/// How far open the ramp is `Age` seconds after the Dropship set its
	/// load down (`GameScene.rampOpening`): open, hold, shut within 1.5 s.
	AUTOCRAFT_API TOptional<double> RampOpening(double Age);

	/// The Swift `pose(_ m: Models.Dropship, …)` inputs. The placement
	/// (position, heading, `y`) is the caller's: `Pose.Placement`.
	struct FDropship
	{
		int64 Id = 0;
		double Time = 0.0;
		bool bMoving = false;
		/// Turn rate / 2.5 (clamped to ±1 inside).
		double Bank = 0.0;
		/// The patient's chest, world (cm), while healing.
		TOptional<FVector> Healing;
		/// How far open the ramp is (0-1), after an unload.
		TOptional<double> Unloading;
	};
	/// The Dropship's parts for `In`, over `Pose` already at the rest pose
	/// and its placement.
	AUTOCRAFT_API void PoseDropship(const FAcModelInfo& Model, FAcPose& Pose, const FDropship& In);

	/// The Swift `pose(_ m: Models.Kestrel, …)` inputs.
	struct FKestrel
	{
		int64 Id = 0;
		double Time = 0.0;
		bool bMoving = false;
		double Bank = 0.0;
		/// When it last fired (game time).
		TOptional<double> Shot;
	};
	AUTOCRAFT_API void PoseKestrel(const FAcModelInfo& Model, FAcPose& Pose, const FKestrel& In);

	/// A SceneKit `eulerAngles` (x pitch, y yaw, z roll; applied roll, then
	/// yaw, then pitch) → the same rotation in Unreal.
	AUTOCRAFT_API FQuat QuatFromSceneKitEuler(double X, double Y, double Z);
}
