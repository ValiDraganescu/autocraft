// `UAcLampPool`: the lights of the world that are not the sun and the moon
// (GAME-LAYER.md §2.8, chunk C6), the port of `NightLamps`
// (Sources/Autocraft/NightLamps.swift) plus the other moving lights of the
// Swift game:
//
// - Night lamps, `dark` (AAcDaylight::GetDark) times their Swift intensity,
//   following their lamp parts as the Swift lights follow their nodes (a
//   hidden part, a unit in the fog, aboard or in a Bastion takes its light
//   with it):
//     headlamp   spot  (1, 0.95, 0.86)  90  reach 6    a Prospector's `headlamp`
//                                                     part, ahead and down
//     beacon     omni  (1, 0.12, 0.08)  20  reach 1.4  every `beacon` part
//     warn       omni  (1, 0.5, 0.12)   40  reach 1.5  a Longbow's `warnLamps_k`
//     pilot      omni  (0.35, 0.6, 1)   25  reach 1.4  a Firefly's `pilot`
//     gunfire    omni  (1, 0.75, 0.4)  140  reach 3.2  every muzzle flash part
//                                                     (`flash`, `minigun_flash`,
//                                                     `flashes_k`), Bastion and
//                                                     Sentinel slits included
//     blast      omni  (1, 0.7, 0.35)  420  reach 7    a Longbow's anchored shot
//                                                     (its last `flashes_k`)
//     apron pool omni  (1, 0.82, 0.5)  26·max(0, glow − 0.15), in front of the
//                                     door (on the pad for a Spacedock), glow =
//                                     the building's yellow working lamps as
//                                     its pose set them (floodGlow, slitGlow,
//                                     workGlow, the guide lamps)
// - Lights by day and night: an explosion's flash (`Effects.explosion`
//   :700, 70·size fading out in 0.53 s, reach 3 + 2.5·size), seen through
//   the effects extension API (any extension handling explosions still
//   does), a Prospector's drill weld light (cue `AcDrill`, AcPoseInfantry.h)
//   and a Firefly's flame light (`AcPoseVehicles::Fx`).
//
// Budget: every frame the wanted lights are culled to the view (a light
// whose reach is off screen is dropped) and the `ac.Lamps` (96) strongest
// for their size and distance to the middle of the view get a real light
// from a pool of point and spot light components (no shadows unless
// `ac.LampShadows 1`; MegaLights makes many cheap). Intensity: a SceneKit
// light `I` (1000 = 1.0, the daylight's scale) is `I · 2.9/1000 ·
// ac.LampGain` (100) unitless, with Unreal's reach falloff (exponent
// `ac.LampFalloff`, 3), not inverse square: both calibrated by eye against
// Swift `bench --shot base,fight` at 23:00 (SceneKit's local lights are far
// brighter than its sun scale suggests). `ac.LampStats N` logs a `lamps:`
// line every N s; negative also logs every apron pool.
//
// The construction weld and the building's own door, bay and weld lights
// stay in `UAcBuildingFx` (its own pool, `ac.BuildingLights`).
//
// Driving (E chunks): the driven Prospector's model is hidden, so its
// headlamp goes; `SetCockpitLamp` hangs it on the cockpit as Swift's
// `NightLamps.fit(cockpit:kind:)` does.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "AcLamps.generated.h"

class AActor;
class ULocalLightComponent;
class UPointLightComponent;
class USpotLightComponent;
class UAcSimSubsystem;
class UAcWorldRenderer;
struct FAcFrame;
struct FAcModelInfo;
struct FAcPoseCue;

UCLASS()
class AUTOCRAFT_API UAcLampPool : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UAcLampPool* Get(const UObject* WorldContext);

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/// An explosion's flash (`Effects.explosion`): `At` cm, size 1 a unit.
	void Explosion(const FVector& At, double Size, double Time);

	/// The driven unit's cockpit (its camera's world transform, X ahead,
	/// Z up) carries the headlamp of a `bHeadlamp` kind (the Prospector);
	/// null: none.
	void SetCockpitLamp(const FTransform* Cockpit, bool bHeadlamp = true);

	struct FStats
	{
		int32 Wanted = 0, InView = 0, Lit = 0;
	};
	const FStats& LastStats() const { return Stats; }

	/// One light this frame (cm; colour linear; Swift intensity and reach).
	struct FWant
	{
		FVector At = FVector::ZeroVector;
		FVector Toward = FVector::ZeroVector;  // a spot's axis; zero for an omni
		FLinearColor Colour = FLinearColor::White;
		double Intensity = 0;  // SceneKit units
		double Reach = 1;      // cells
		double Inner = 0, Outer = 0;  // a spot's cone, full angles (SceneKit), degrees
		double Score = 0;
	};

private:
	/// A model's lamp sockets, found once per model by part name.
	struct FSocket
	{
		int32 Part = INDEX_NONE;
		uint8 Kind = 0;
	};
	struct FSockets
	{
		TArray<FSocket> Lamps;
		/// The apron pool: its point in the building's root (SceneKit, cells),
		/// reach, and where its glow comes from.
		bool bPool = false;
		FVector PoolAt = FVector::ZeroVector;
		double PoolReach = 3;
		FName GlowMaterial;
		double GlowExported = 2.0;
		TArray<int32> GuideLamps;
		int32 FlameLight = INDEX_NONE;
	};
	struct FBlast
	{
		FVector At;
		double Size = 1, Time = 0;
	};

	void OnGameStarted(UAcSimSubsystem& Sim);
	void OnFrame(const FAcFrame& Frame);
	void OnCue(int64 Id, const FAcPoseCue& Cue);
	void Bind();
	const FSockets& SocketsOf(const FAcModelInfo& Model);
	void Gather(double Time, double Dark, TArray<FWant>& Want);
	void Light(TArray<FWant>& Want);
	ULocalLightComponent* Take(bool bSpot, int32 Index);

	TMap<const FAcModelInfo*, TUniquePtr<FSockets>> Sockets;
	TArray<FBlast> Blasts;
	TArray<FWant> Drills;
	TOptional<FTransform> Cockpit;

	UPROPERTY(Transient)
	TObjectPtr<AActor> Holder;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> Points;
	UPROPERTY(Transient)
	TArray<TObjectPtr<USpotLightComponent>> Spots;
	TWeakObjectPtr<UAcWorldRenderer> Renderer;

	FDelegateHandle FrameHandle, StartedHandle, CueHandle;
	FStats Stats;
	double StatsWall = 0;
};
