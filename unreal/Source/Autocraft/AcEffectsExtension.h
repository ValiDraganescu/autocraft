// How later effect chunks plug into `UAcEffects` (AcEffects.h) without
// editing its files (GAME-LAYER.md §4: C2 arcing projectiles and decals, C3
// Niagara bursts, C4/C5 deaths).
//
// An extension is a plain C++ class derived from `FAcEffectsExtension`,
// registered once at load from its own .cpp:
//
//   class FAcProjectiles final : public FAcEffectsExtension { ... };
//   static FAcEffectsExtensionRegistration Reg(TEXT("projectiles"), 100,
//       [] { return TUniquePtr<FAcEffectsExtension>(new FAcProjectiles); });
//
// `UAcEffects` makes one instance of every registered extension per world
// (in `BeginPlay`, ordered by `Order`, lowest first) and calls it on the
// game thread:
//   - `Begin(Effects)` once, after C1's pools exist (make your own pools
//     here with `Effects.MakePool`, read `Effects.Renderer()`),
//   - `Consume(Frame)` once a frame after C1 dispatched the frame's events
//     (C4/C5: `Died`, `Destroyed`; anything else C1 does not draw),
//   - `Update(Time)` once a frame after C1's own update (fly what you own),
//   - `Clear()` on a new game, `End()` before the world goes.
//
// The `Handle*` calls are the effects C1's dispatch asks for but does not
// draw itself. `UAcEffects` offers each request to the extensions in order
// and stops at the first that returns true. If none does, C1 draws a
// stand-in (see each call) so a fight still reads before C2/C3 land.
#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Templates/UniquePtr.h"

class UAcEffects;
struct FAcFrame;
struct FAcPoseCue;

/// The replayed particle bursts of the Swift `Effects` (`Bursts`, :736).
enum class EAcBurst : uint8
{
	/// `blasts` (fireBurst + smokeBurst, pool 20): a slug's or grenade's blast.
	Blast,
	/// `flames` (groundFire + embers, pool 28): flame licks on the ground.
	Flames,
	/// `dust` (dustBurst, pool 12): dust thrown off the ground.
	Dust,
	/// `chips` (crystalChips, pool 12): opal chips off an ore deposit.
	Chips,
	/// `ricochets` (ricochet, pool 40): sparks thrown on along a round's line.
	Ricochet,
};

/// `Bursts.fire(at:size:time:duration:along:)`: positions in cm (UE world).
struct FAcBurstRequest
{
	EAcBurst Kind = EAcBurst::Blast;
	FVector At = FVector::ZeroVector;
	/// Scales the particles; √size scales their speed.
	double Size = 1.0;
	double Time = 0.0;
	/// How long the emitter spawns (Swift default 0.1 s).
	double Duration = 0.1;
	/// Sprays along this direction (unit vector), if set (ricochets).
	TOptional<FVector> Along;
};

/// The rounds of the Peregrine, the Atlas and the Scorpion (AcEffectsNewKinds.cpp):
/// a seeker missile, a siege cannon's shell and a sting.
enum class EAcRound : uint8
{
	Missile,
	AtlasShell,
	Sting,
};

/// A shell or grenade on its way (`Effects.grenade`/`anchorShell`,
/// :374/:390). Positions in cm. The extension flies it from `From` toward
/// `To` (homing on `Track()` while that returns a value) for exactly
/// `Flight` seconds (`Rules.flight`: it lands when the damage does) and then
/// calls `Land(time, where)` once — also when a pool slot is reused before
/// it landed (Swift force-lands the old shell).
struct FAcLaunch
{
	FVector From = FVector::ZeroVector;
	FVector To = FVector::ZeroVector;
	/// The ground height under the target, cm (shock ring, scorch).
	double GroundZ = 0.0;
	double Flight = 0.0;
	double Time = 0.0;
	/// The arc's height, cells (grenade 0.25 + 0.07·len, shell 0.8 + 0.18·len).
	double Arc = 0.0;
	/// Where the target is now (cm), unset once it is gone; may be null.
	TFunction<TOptional<FVector>()> Track;
	/// What happens where it comes down (the burst, flash, ring, scorch).
	/// Written by C1 (`GrenadeLanded`/`ShellLanded`); call it exactly once.
	TFunction<void(double /*Time*/, const FVector& /*At*/)> Land;
};

/// A dark mark on the ground (`Effects.scorch`, :435): cm and radians.
struct FAcScorchRequest
{
	FVector At = FVector::ZeroVector;  // on the ground (Z = ground)
	double Radius = 1.0;               // cm
	double Stretch = 1.0;              // along Yaw
	double Yaw = 0.0;                  // UE yaw, radians (0: along +X)
	double Life = 10.0;
	double Time = 0.0;
	double Darkness = 0.9;
};

/// A coloured decal (`Effects.decal`, :717: blood, a blast's mark).
struct FAcDecalRequest
{
	FVector At = FVector::ZeroVector;  // on the ground
	double Radius = 1.0;               // cm
	FLinearColor Color = FLinearColor::Black;  // sRGB components as Swift's NSColor
	double Life = 10.0;
	double Time = 0.0;
};

/// A hot ring racing out over the ground (`Effects.shockRing`, :447).
struct FAcShockRingRequest
{
	FVector At = FVector::ZeroVector;  // on the ground
	double Radius = 1.0;               // cm
	double Life = 0.3;
	double Time = 0.0;
	double Bright = 1.0;
};

/// Charred chunks thrown out (`Effects.debris`, :542).
struct FAcDebrisRequest
{
	FVector At = FVector::ZeroVector;
	int32 Count = 3;
	double Size = 1.0;     // cells (as Swift)
	double GroundZ = 0.0;  // cm
	double Time = 0.0;
	int32 Seed = 0;
};

class AUTOCRAFT_API FAcEffectsExtension
{
public:
	virtual ~FAcEffectsExtension() = default;

	virtual void Begin(UAcEffects& Effects) {}
	virtual void Consume(const FAcFrame& Frame) {}
	virtual void Update(double Time) {}
	virtual void Clear() {}
	virtual void End() {}

	/// C2: a Juggernaut grenade (pool 28 in Swift).
	virtual bool HandleGrenade(FAcLaunch& Launch) { return false; }
	/// C2: an anchored Longbow's shell (pool 12 in Swift).
	virtual bool HandleAnchorShell(FAcLaunch& Launch) { return false; }
	/// The new kinds' rounds in flight (`UAcEffects::Round`): flown like a
	/// shell, for exactly `Flight` seconds, then `Land` once.
	virtual bool HandleRound(EAcRound Kind, FAcLaunch& Launch) { return false; }
	/// C3: fire, smoke, sparks and a light (`Effects.explosion`, :630).
	/// Size 1 is a unit, about 3 a building.
	virtual bool HandleExplosion(const FVector& At, double Size, double Time) { return false; }
	/// C3: a particle burst.
	virtual bool HandleBurst(const FAcBurstRequest& Burst) { return false; }
	/// C2: decals and rings.
	virtual bool HandleScorch(const FAcScorchRequest& Scorch) { return false; }
	virtual bool HandleDecal(const FAcDecalRequest& Decal) { return false; }
	virtual bool HandleShockRing(const FAcShockRingRequest& Ring) { return false; }
	/// C4/C5: debris.
	virtual bool HandleDebris(const FAcDebrisRequest& Debris) { return false; }
	/// A pose cue C1 does not know (`UAcWorldRenderer::OnCue`).
	virtual bool HandleCue(const FAcPoseCue& Cue) { return false; }
};

using FAcEffectsExtensionFactory = TFunction<TUniquePtr<FAcEffectsExtension>()>;

namespace AcEffectsExtensions
{
	struct FEntry
	{
		const TCHAR* Name = nullptr;
		int32 Order = 0;
		FAcEffectsExtensionFactory Make;
	};
	AUTOCRAFT_API void Register(const TCHAR* Name, int32 Order, FAcEffectsExtensionFactory Make);
	AUTOCRAFT_API const TArray<FEntry>& All();
}

/// File-scope registration (see the top of this file).
struct FAcEffectsExtensionRegistration
{
	FAcEffectsExtensionRegistration(const TCHAR* Name, const int32 Order, FAcEffectsExtensionFactory Make)
	{
		AcEffectsExtensions::Register(Name, Order, MoveTemp(Make));
	}
};
