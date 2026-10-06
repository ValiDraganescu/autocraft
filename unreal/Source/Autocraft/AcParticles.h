// `FAcParticles`: the particle set of the effects (chunk C3, GAME-LAYER.md
// §2.7), the port of the Swift game's `SCNParticleSystem`s: explosions
// (`Effects.explosion`), the `Bursts` (fireBurst + smokeBurst, groundFire +
// embers, dustBurst, crystalChips, ricochet), damage smoke and fire
// (`Models.damageFire`), the vehicles' jets, flames and plumes, a
// Prospector's drill sparks (`weldSparks`) and the shells' trails.
//
// How: each kind is one `UInstancedStaticMeshComponent` of a quad
// (`/Game/Effects/SM_AcParticleQuad`) in `M_AcParticleAdd`: additive, as
// every Swift system (SceneKit's default blend mode; its smoke and dust are
// pale glows, never dark), soft where it meets the ground; made by
// `Tools/Editor/make_particle_materials.py`. A particle is written ONCE, when
// it is born (where, when, its velocity, size and colour, as custom data);
// the vertex shader flies it from there (the SceneKit emitter's damping and
// acceleration in closed form), grows and fades it by the kind's keyframe
// ramps and turns it to the camera. Each kind's instances are a ring: a new
// particle takes the oldest slot. So the CPU pays only for births, and a
// burst is uploaded once, all of it, its later births stamped ahead
// (unborn particles collapse to a point until their time).
//
// Units: every position and transform is Unreal world, cm. The kind table
// keeps the Swift values (cells, SceneKit axes: +Y up) and converts.
// Clock: game time (`state.time`): a paused game freezes the particles.
//
// Other chunks (C2's trails, C5's burning wrecks) use `Burst`/`Stream`.
#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "UObject/StrongObjectPtr.h"

class AActor;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class UWorld;

/// The particle kinds (one SCNParticleSystem each in Swift).
enum class EAcParticle : uint8
{
	// Effects.explosion (Effects.swift:630): fire, smoke, flying sparks.
	ExplosionFire,
	ExplosionSmoke,
	ExplosionSparks,
	// Bursts.blasts (:900, :925): a slug's or grenade's blast.
	BlastFire,
	BlastSmoke,
	// Bursts.flames (:948, :972): flame licks and embers off the ground.
	GroundFire,
	Embers,
	// Bursts.dust (:1045), chips (:993), ricochets (:1020).
	Dust,
	Chips,
	Ricochet,
	// Models.damageFire (Effects.swift:1073): a hurt building, a wreck.
	DamageSmoke,
	DamageFire,
	// Models+Comet.swift:292 jetExhaust.
	CometJet,
	// Models+Juggernaut.swift:237 muzzleSmoke.
	MuzzleSmoke,
	// Models+Firefly.swift:593-680: fire, embers, smoke, exhaust.
	FireflyFire,
	FireflyEmbers,
	FireflySmoke,
	FireflyExhaust,
	// Models+Dropship.swift:328 thrusterPlume (the Kestrel's too).
	Plume,
	// Models.swift:214 weldSparks (a Prospector's drill bit).
	WeldSparks,
	// Effects.swift:812-893: a grenade's smoke trail, an anchor shell's
	// plasma and plume (C2 streams them along the flight).
	GrenadeTrail,
	ShellFire,
	ShellSmoke,
	Count
};

/// Overrides of a kind's values for one emission (Swift sets these on the
/// system before it emits: `Bursts.fire` scales by size, the poses set the
/// birth rate and the speed). Negative: the kind's own value. Cells and
/// seconds, as Swift.
struct FAcEmit
{
	/// Births a second (`birthRate`).
	double Rate = -1;
	/// How long a burst emits (`emissionDuration`, Bursts default 0.1 s).
	double Duration = 0.1;
	double Speed = -1, SpeedVariation = -1;
	double Size = -1, SizeVariation = -1;
	/// Scales the emitter shape (the explosion's sphere grows with size).
	double ShapeScale = 1;

	/// `Bursts.fire(size:)`: size × k, speed × √k, rate × k (variations kept).
	static FAcEmit Scaled(EAcParticle Kind, double K, double Duration = 0.1);
};

class AUTOCRAFT_API FAcParticles
{
public:
	FAcParticles();
	~FAcParticles();

	/// Make the components under `Owner` (game thread). False if the
	/// assets are missing (run make_particle_materials.py): then every call
	/// does nothing.
	bool Init(AActor* Owner);
	bool IsReady() const { return bReady; }

	/// One burst: `Rate × Duration` particles (stochastic rounding) born
	/// from `Time` over `Duration`, from the emitter at `Frame` (its rotation
	/// turns the kind's SceneKit direction and shape).
	void Burst(EAcParticle Kind, const FTransform& Frame, double Time, const FAcEmit& Emit);
	/// A running emitter, called every frame with the game seconds since
	/// the last (`Dt`): `Rate × Dt` births spread over the frame and along
	/// the way the emitter came. `Key` names the emitter (its fraction of a
	/// particle carries over, and where it was last frame); 0: no memory.
	void Stream(EAcParticle Kind, uint64 Key, const FTransform& Frame, double Rate, double Time, double Dt,
		const FAcEmit& Emit = FAcEmit());

	/// The clock the shaders read (game time, between steps) and the
	/// frame's uploads.
	void Update(UWorld* World, double DisplayTime);
	/// Every particle gone (a new game).
	void Clear();

	struct FStats
	{
		int64 Born = 0;
		int32 Uploaded = 0;
		int32 Emitters = 0;
	};
	const FStats& Totals() const { return Stats; }
	/// Particles alive now (by their stamped lives), every kind.
	int32 Alive(double Time) const;

	/// The `Key` of emitter `Slot` of object `Id`.
	static uint64 KeyOf(int64 Id, uint32 Slot) { return (uint64(Id) << 8) ^ uint64(Slot & 0xff); }

	struct FSpec;

private:
	struct FBatch;
	struct FTrack
	{
		FTransform Last;
		double Carry = 0;
		double SeenAt = 0;
	};

	void Spawn(EAcParticle Kind, const FTransform& From, const FTransform& To, double T0, double T1, int32 Count,
		const FAcEmit& Emit);

	TArray<TUniquePtr<FBatch>> Batches;
	TMap<uint64, FTrack> Tracks;
	TStrongObjectPtr<UMaterialParameterCollection> Clock;
	FRandomStream Random{0xc3c3};
	FStats Stats;
	double PruneAt = 0;
	bool bReady = false;
};
