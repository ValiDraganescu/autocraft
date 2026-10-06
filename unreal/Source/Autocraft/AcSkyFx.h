// `FAcSkyFx`: the sky's life besides the gas giant (GAME-LAYER.md, "Sky:
// ships, comet, meteors" row). Like the planet it is not geometry: the sky
// dome's material (`M_AcSky`, Tools/Editor/make_sky_material.py,
// `SKYFX_HLSL`) draws it from the pixel's direction, at infinity, under the
// horizon haze. This class owns the plan and feeds the material every frame:
//
// - Ships: every 60-180 s (one start per 120 s slot, up to 60 s of jitter) a
//   small craft either lifts off from the planet's limb and climbs across the
//   sky to set below the opposite horizon, or comes up from there and
//   descends onto the planet, shrinking toward it. 20-40 s on a great-circle
//   arc lifted overhead; a bright hull, a warm engine glow, a faint contrail.
//   Visible by day too (engine glow fainter).
// - The comet: a long-lived one, with a glowing coma, a curved dust tail and a
//   straighter blue ion tail pointing away from the sun (so it swings with the
//   day), drifting slowly over the sky.
// - The meteor shower: radiating from the zenith, a few a minute, bursts of
//   25 s every ~5 min, and a rare bright fireball (~every 8 min).
//
// The comet and the meteors (and the gas giant's limb meteors) are hidden by
// day: `FAcSkyFx::Night` is 0 with the sun up, 1 from the end of dusk.
//
// Everything is a pure function of the clock and the seed (`-AcGasGiantSeed=N`).
// Console: `ac.Sky.Ships`, `ac.Sky.Comet`, `ac.Sky.Meteors` (0/1).
// Stills (with -AcShot; with -AcSkyShot a camera 2 m over the map middle):
//   -AcSkyShot -AcSkyAim=ship|comet|zenith|planet|horizon  -AcSkyFov=F
//   -AcSkyPitch=DEG -AcSkyYaw=DEG   (override the aim)
//   -AcSkyT=SECONDS        freeze the clock
//   -AcSkyShip=takeoff|landing -AcSkyShipAt=U   force a ship at U (0-1) of its flight
//   -AcSkyMeteors=burst    force the burst;  -AcSkyFireball[=U]  force the fireball (U 0-1.6)
#pragma once

#include "CoreMinimal.h"

class AActor;
class UMaterialInstanceDynamic;
class UWorld;

class FAcSkyFx
{
public:
	void Update(UWorld* World, AActor* Owner, UMaterialInstanceDynamic* Mat, const FVector& SunDir, double Dark);

	/// 0 by day, 1 at night, through dusk and dawn (from `Dark`).
	static double Night(double Dark);

private:
	struct FShip
	{
		bool bOn = false;
		FVector Pos = FVector::ZeroVector, Tail = FVector::ZeroVector;
		double Size = 0, Engine = 0, Fade = 0;
	};
	FShip ShipAt(double Now, uint32 Seed) const;
	FVector CometAt(double Now) const;
	void StageShot(UWorld* World, AActor* Owner, const FVector& ShipPos, const FVector& CometPos);

	TWeakObjectPtr<AActor> Camera;
	int32 ShotFrames = 0;
	bool bLogged = false;
};
