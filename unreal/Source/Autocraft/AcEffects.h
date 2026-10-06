// `UAcEffects`: the short-lived combat effects (chunk C1, GAME-LAYER.md
// §2.7), the port of the Swift `Effects` class (`Effects.swift`) and of
// `GameScene.shot`/`missed`/`fire` (`GameScene.swift:1164-1353`, plus
// `tracers`, `grenades`, `Longbow.tankRounds`, `Firefly.flameLine`).
//
// Each frame, at the `Effects` stage of `UAcSimSubsystem` (after the
// renderer posed every unit, so muzzles are where this frame draws them):
//   1. `Consume(Frame)`: the frame's fog-gated `Shot`, `Missed` and `Blast`
//      events become effects (who fires what, from which muzzle: see
//      AcEffectsShots.cpp); then each extension's `Consume`.
//   2. `Update(Time)`: in Swift's order: spent tracers and flashes hide,
//      rounds and slugs fly (and spark where they land), delayed actions
//      come due, work finished off the game thread is finished, the
//      extensions update, the timed effects tick; then every pool uploads.
//
// What C1 draws itself, in ISM pools (`FAcEffectPool`), unlit emissive
// (`M_Emissive`/`M_Additive` instances made at runtime, Swift colours):
//   tracers (48), impact flashes (32), mini gun rounds (64), plasma slugs
//   (16, a core and an additive halo).
// What it asks for and leaves to the extensions (AcEffectsExtension.h):
//   grenades and anchor shells (C2; stand-in: the landing at `flight`),
//   explosions and particle bursts (C3; stand-in: a flash), scorch marks,
//   decals, shock rings (C2), debris (C4/C5). See `Handle*` below.
//
// The clock is game time (`state.time`): every `Time` argument is game
// seconds, as Swift's `clock`.
//
// Console: `ac.EffectsStats N` (log an `effects:` line every N s; 0 off).
// Command line: `-AcEffectsLog` logs each shot dispatched (dev);
// `-AcEffectsDemo="X,Y"` fires every C1 effect in rows at sim point
// (X, Y), all the time (dev: a still of tracer, mini gun round, slug,
// flash, grenade).
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Templates/Function.h"

#include "AcEffectPool.h"
#include "AcEffectsExtension.h"
#include "SimdMath.h"

#include <memory>

#include "AcEffects.generated.h"

class UAcSimSubsystem;
class UAcWorldRenderer;
class UMaterialInstanceDynamic;
struct FAcFrame;
struct FAcModelInfo;
struct FAcPoseCue;
namespace ac
{
	struct GameState;
	struct TerrainField;
	struct Unit;
}

/// What the pilot (chunk E) tells the effects about the driven unit
/// (`GameScene.hiddenUnit`, `drivenUnit`, `pilotMuzzles`, `pilotAim`,
/// `pilotAimOn`). Set it each frame with `UAcEffects::SetPilot`.
struct FAcPilotFire
{
	/// Driven in first person (its model hidden): rounds leave `Muzzles`.
	int64 HiddenUnit = INDEX_NONE;
	/// Driven (first or third person).
	int64 DrivenUnit = INDEX_NONE;
	/// The cockpit's gun muzzles, world cm, in the model's muzzle order.
	TArray<FVector> Muzzles;
	/// Where the crosshair is on `AimOn` (a unit or building id), world cm.
	TOptional<FVector> Aim;
	int64 AimOn = INDEX_NONE;
	/// Where the sight meets the ground for a shot with nothing under it
	/// (`sightPoint(reach:)`), world cm; if unset, a miss lands on the
	/// ground at the event's `at`.
	TOptional<FVector> SightPoint;
};

UCLASS()
class AUTOCRAFT_API UAcEffects : public UActorComponent
{
	GENERATED_BODY()

public:
	UAcEffects();

	/// The effects of `WorldContext`'s world (null if none was spawned).
	static UAcEffects* Get(const UObject* WorldContext);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/// A frame's events into effects, then `Update(Frame.Time)`.
	void Consume(const FAcFrame& Frame);
	/// Hide spent pool instances and advance everything to `Time`.
	void Update(double Time);
	/// Everything off (a new game).
	void Clear();

	// --- The clock (`Effects.add`, `after`, `offThread`) ---------------------

	/// Run `Tick(age)` every update for `Life` seconds from `Time` (age
	/// 0…1, clamped at 0), then `End()` once. Ticks once at once with age 0
	/// (as Swift's `add`). Effects a tick starts join after this pass.
	void Add(double Life, double Time, TFunction<void(double /*Age*/)> Tick, TFunction<void()> End = nullptr);
	/// Run `Action` once, `Delay` seconds after `Time` (game time).
	void After(double Delay, double Time, TFunction<void()> Action);
	/// `Work` on a background thread, then `Then` on the game thread at the
	/// next update.
	void OffThread(TFunction<TFunction<void()>()> Work);
	/// The latest time `Update` was given.
	double Now() const { return NowTime; }

	// --- What C1 draws (cm, UE world) -----------------------------------------

	/// A coil-rifle round: a streak over the front of the way for 0.06 s and
	/// a spark where it lands (`Effects.tracer`, :242).
	void Tracer(const FVector& From, const FVector& To, double Time);
	/// A mini gun round flying at 40 cells/s, then a spark and ricochets
	/// (`Effects.minigunRound`, :262).
	void MinigunRound(const FVector& From, const FVector& To, double Time);
	/// A packed-rail plasma blob at 28 cells/s, then a spark and a small
	/// blast (`Effects.slug`, :338).
	void Slug(const FVector& From, const FVector& To, double Time);
	/// A Kestrel's rail gun: a thin bright beam the whole way from the muzzle
	/// for 0.22 s (hitscan), a flare at the muzzle and a burst where it hits.
	void RailShot(const FVector& From, const FVector& To, double Time);
	/// An impact spark, `Life` seconds (`Effects.flash`, :320). Scale 1 is
	/// a sphere of radius 0.09 cells.
	void Flash(const FVector& At, double Scale, double Time, double Life = 0.07);
	/// A Prospector's cutter biting (`Effects.cutterSpark`, :315).
	void CutterSpark(const FVector& At, double Time) { Flash(At, 0.9, Time, 0.12); }

	// --- What C1 asks the extensions for (stand-ins if none answers) ---------

	/// A grenade (`Effects.grenade`): flies (C2), then bursts (flash 2.6,
	/// blast 0.85, shock ring 0.8). Stand-in: the burst after `Flight`.
	void Grenade(const FVector& From, const FVector& To, double GroundZ, double Flight, double Time,
		TFunction<TOptional<FVector>()> Track = nullptr);
	/// An anchored Longbow's shell (`Effects.anchorShell`): flies (C2),
	/// then explosion, flash 5, ring, dust, debris, scorch.
	void AnchorShell(const FVector& From, const FVector& To, double GroundZ, double Flight, double Time,
		TFunction<TOptional<FVector>()> Track = nullptr);
	/// `Effects.explosion` (C3). Stand-in: a flash of 3·size.
	void Explosion(const FVector& At, double Size, double Time);
	/// `Bursts.fire` (C3). Stand-in: none (a blast: a small flash).
	void Burst(const FAcBurstRequest& Request);
	/// `Effects.dustPuff`.
	void DustPuff(const FVector& At, double Size, double Time);
	/// `Effects.crystalBite`.
	void CrystalBite(const FVector& At, double Time);
	/// `Effects.flameLine`: three flame licks along the line and a scorch
	/// streak (sim points, cells; the ground from the map).
	void FlameLine(ac::Vec2 From, ac::Vec2 To, double Time);
	void Scorch(const FAcScorchRequest& Request);
	void Decal(const FAcDecalRequest& Request);
	void ShockRing(const FAcShockRingRequest& Request);
	void Debris(const FAcDebrisRequest& Request);

	// --- Shots (AcEffectsShots.cpp) ------------------------------------------

	/// `GameScene.shot`: `Unit` fired at `Target` over ground point `At`.
	void Shot(int64 Unit, int64 Target, ac::Vec2 At, double Time);
	/// `GameScene.missed`: the driven unit's round spent at `To` (cm).
	void Missed(int64 Unit, const FVector& To, double Time);
	/// `GameScene.blast`: a driven unit's Pulse mine went off.
	void Blast(ac::Vec2 At, double Radius, double Time);
	/// An Atlas's Quake stomp (`GameEvent::Stomp`): a ring of dust and two
	/// shock rings spreading out to `Radius` cells, a mark on the ground,
	/// and a shake for cameras close to it (`StompShake`).
	void Stomp(ac::Vec2 At, double Radius, double Time);
	/// An Atlas's foot came down (the `AtlasStep` cue; `Side` 0 left, 1 right):
	/// a puff of dust, a footprint that fades, and a small shake for cameras
	/// within a few cells (`StompShake`'s, a fifth of a stomp's).
	void Footfall(const FVector& At, double Time, int32 Side);
	/// A round of the new kinds (a Peregrine's missile, an Atlas's shell, a
	/// Scorpion's sting) from `From` to `To` (cm), flying `Flight` seconds, homing
	/// on `Track`; what happens where it comes down is `RoundLanded`.
	void Round(EAcRound Kind, const FVector& From, const FVector& To, double Ground, double Flight, double Time,
		TFunction<TOptional<FVector>()> Track = nullptr);
	/// How hard the ground shakes under a camera looking at sim point `From`
	/// now, -1…1: the stomps of the last 0.6 s within `Reach` cells of it,
	/// fading with distance and age. 0 when none is near. The caller sets
	/// `Reach` (the pilot's eye: a stomp near the driven unit; the top-down
	/// view: only when zoomed in close).
	double StompShake(ac::Vec2 From, double Reach) const;

	/// The pilot's view of the driven unit (chunk E; default: none).
	void SetPilot(const FAcPilotFire& Pilot) { PilotFire = Pilot; }
	const FAcPilotFire& Pilot() const { return PilotFire; }

	/// When each firing slit of a Bastion last fired (game time, one per
	/// slit in the model's `flashes` order; -10 never), for its pose (B6):
	/// `GameScene.slitShots`. Null if it never fired. Written only at the
	/// Effects stage, so poses may read it.
	const TArray<double>* SlitShots(int64 Bastion) const { return SlitShotTimes.Find(Bastion); }

	// --- For extensions --------------------------------------------------------

	/// Where a drawn part is now (its pivot), cm, in the world; false if the
	/// unit or the part is not drawn.
	bool PartLocation(int64 Id, FName Part, FVector& Out) const;
	/// A part's muzzle: its pivot, and the way its meshes reach forward (a
	/// cannon's barrel, a launcher), cm, in the world.
	bool PartMuzzle(int64 Id, FName Part, FVector& Out) const;
	/// A pool owned and uploaded by the effects (C2: grenades, decals...).
	FAcEffectPool& MakePool(UStaticMesh* Mesh, UMaterialInterface* Material, int32 Count, FName Name,
		bool bCastShadow = false);
	/// An unlit emissive material (`MaterialLibrary.emissive`): `Color` in
	/// sRGB components as Swift's NSColor, times `Intensity`; `bAdditive`
	/// with `Texture` (an alpha sprite such as `T_spark`) for glows.
	UMaterialInstanceDynamic* EmissiveMaterial(const FLinearColor& SrgbColor, double Intensity, bool bAdditive = false,
		UTexture* Texture = nullptr);
	UAcWorldRenderer* Renderer() const;
	UAcSimSubsystem* Sim() const;
	/// Ground height under a sim point, cm (`GameScene.groundY` × 100).
	double GroundZ(ac::Vec2 P) const;
	/// The engine's unit shapes, for pools.
	UStaticMesh* CylinderMesh() const { return Cylinder; }
	UStaticMesh* SphereMesh() const { return Sphere; }

	struct FStats
	{
		int32 Shots = 0, Tracers = 0, Rounds = 0, Slugs = 0, Flashes = 0, Grenades = 0, Shells = 0, Bursts = 0,
			Explosions = 0, Unhandled = 0, Timed = 0, Pending = 0;
	};
	const FStats& Totals() const { return Count; }

private:
	struct FTimed
	{
		double Start = 0, Life = 1;
		TFunction<void(double)> Tick;
		TFunction<void()> End;
	};
	struct FPending
	{
		double At = 0;
		uint64 Seq = 0;
		TFunction<void()> Run;
	};
	/// A round or a slug in flight.
	struct FFlight
	{
		FVector From = FVector::ZeroVector, To = FVector::ZeroVector;
		double Start = 0, Flight = 0;
		bool bLive = false;
	};

	void OnGameStarted(UAcSimSubsystem& Sim);
	void OnFrame(const FAcFrame& Frame);
	void OnCue(int64 Id, const FAcPoseCue& Cue);
	void MakePools();
	void FlyRounds(double Time);
	void FlySlugs(double Time);
	void PlaceRound(int32 I, double Time);
	void GrenadeLanded(double Time, const FVector& At, double GroundZ);
	void ShellLanded(double Time, const FVector& At, double GroundZ);
	void Launch(FAcLaunch& L, bool bShell);
	void RoundLanded(EAcRound Kind, double Time, const FVector& At, double GroundZ);
	/// The stomp's ring, dust, mark and shake, at the foot's impact.
	void StompImpact(ac::Vec2 At, double Radius, double Time);

	// Shot helpers (AcEffectsShots.cpp).
	struct FShooter;
	void Fire(const FShooter& S, const FVector& To, ac::Vec2 At, double Height, double Time,
		TFunction<TOptional<FVector>()> Track);
	TOptional<FVector> AirBody(int64 Id) const;

	TArray<TUniquePtr<FAcEffectPool>> Pools;
	FAcEffectPool* TracerPool = nullptr;
	FAcEffectPool* FlashPool = nullptr;
	FAcEffectPool* RoundPool = nullptr;
	FAcEffectPool* SlugCorePool = nullptr;
	FAcEffectPool* SlugHaloPool = nullptr;
	TArray<double> TracerUntil, FlashUntil;
	TArray<FFlight> Rounds, Slugs;

	TArray<FTimed> Timed;
	TArray<FPending> Pending;  // a min-heap on (At, Seq)
	uint64 PendingSeq = 0;
	FCriticalSection DoneLock;
	TArray<TFunction<void()>> Done;
	/// Bumped on Clear: background work of an older game is dropped.
	int32 Generation = 0;
	double NowTime = 0.0;
	/// The Quake stomps of the last second (`StompShake`).
	/// `Weight`: a stomp's 1, a footfall's less; `Cap`: how far it is felt, cells.
	struct FStompMark { ac::Vec2 At; double Time = 0.0; double Weight = 1.0; double Cap = 1e9; };
	TArray<FStompMark> Stomps;

	TArray<TUniquePtr<FAcEffectsExtension>> Extensions;
	TMap<int64, TArray<double>> SlitShotTimes;
	FAcPilotFire PilotFire;

	std::unique_ptr<ac::TerrainField> Field;
	const void* FieldMap = nullptr;
	/// The frame being consumed (valid during Consume).
	const FAcFrame* Frame = nullptr;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> Keep;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> Cylinder;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> Sphere;

	FDelegateHandle FrameHandle, StartedHandle, CueHandle;
	FStats Count;
	double StatsWall = 0.0;
	bool bLogShots = false;
	TOptional<ac::Vec2> DemoAt;
	double NextDemo = 0.0;
	void Demo(double Time);
};

/// The actor holding the effects (spawned by the game mode).
UCLASS()
class AUTOCRAFT_API AAcEffectsActor : public AActor
{
	GENERATED_BODY()

public:
	AAcEffectsActor();
	static AAcEffectsActor* SpawnFor(UWorld* World);

	UPROPERTY(VisibleAnywhere, Category = "Autocraft")
	TObjectPtr<UAcEffects> Effects;
};
