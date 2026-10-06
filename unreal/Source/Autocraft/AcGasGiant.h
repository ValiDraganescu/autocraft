// `FAcGasGiant`: the gas giant in the sky and its life (GAME-LAYER.md,
// "Gas giant" row). The planet is not geometry: it is drawn by the sky
// dome's material (`M_AcSky`, Tools/Editor/make_sky_material.py) from the
// pixel's direction alone, so it sits at infinity (no parallax with the
// map, the same from the RTS camera and the cockpit), lit by the sun's
// direction (phase and terminator follow the day), under the dome's haze,
// and fading toward the sky by day. This class owns the plan: where it
// hangs, its spin, the storm, and the events, which it feeds to the dome's
// material parameters every frame:
//
// - bands drift with a zonal wind (differential rotation) and a large oval
//   storm wanders along its band (all in the material, from `Time`);
// - meteors burn up along the limb: three independent schedules of one every
//   37, 53 and 71 s on average a streak of 0.8 s (one a ~20 s in all);
// - a comet comes in every 140-340 s (one start per 240 s slot, with up to
//   100 s of jitter): a 3.6 s streak ends in an impact flash on the visible
//   face, then a shock ring, a dark smear that stretches with the winds and
//   fades over 5 minutes. Two scar slots hold the last two.
//
// Everything is a pure function of time and a seed (`-AcGasGiantSeed=N`;
// a random one per run by default).
//
// Console: `ac.GasGiant 0/1` (the whole planet), `ac.GasGiant.Events 0/1`,
// `ac.GasGiant.Gain` (brightness trim).
// Stills: `-AcGasGiantShot` points a camera at it (see AcGasGiant.cpp for
// the options).
#pragma once

#include "CoreMinimal.h"

class AActor;
class UMaterialInstanceDynamic;
class UWorld;

class FAcGasGiant
{
public:
	/// Feed the dome material for this frame. `SunDir` toward the sun
	/// (Unreal axes), `Dark` 0 by day to 1 by night.
	void Update(UWorld* World, AActor* Owner, UMaterialInstanceDynamic* Mat, const FVector& SunDir, double Dark);

	/// Toward the middle of the planet (unit, Unreal axes).
	static FVector Direction();
	/// The angular radius in degrees.
	static double AngularRadiusDeg();
	/// The sun behind the disc: 0 clear, 1 deep in it (for dimming the day's light).
	static double Eclipse(const FVector& SunDir);

private:
	struct FFrame
	{
		FVector P, E1, E2;
		FVector AxisView;
		double Radius = 0;
	};
	void StageShot(UWorld* World, AActor* Owner, const FFrame& F, double Spin, double Now);
	/// The surface point of a disc position (x, y in radii) as latitude and
	/// body longitude at `Spin`, or false if off the disc.
	bool Surface(const FFrame& F, double X, double Y, double Spin, double& Lat, double& Lon) const;

	TWeakObjectPtr<AActor> Camera;
	int32 ShotFrames = 0;
	bool bLogged = false;
	/// Where the newest comet lands on the disc (radii), for the stills.
	FVector2D LastImpact = FVector2D::ZeroVector;
};
