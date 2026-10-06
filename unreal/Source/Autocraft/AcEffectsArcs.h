// Chunk C2 (GAME-LAYER.md §2.7): shells in flight. The Swift `Effects`
// `launch`/`fly` (`Effects.swift:590-627`) with its two pools of
// `Projectile`s (`grenade` 28, `shell` 12, :816/:857).
//
// `FAcArcFlights` is the flight bookkeeping with no drawing (tested by
// `Autocraft.Effects.Arcs.*`): a ring of slots; `Launch` takes the next slot
// and, if a shell still flies in it, lands that one first (at the new
// launch's time, where it was headed: Swift force-lands it); `Fly` homes
// every flight on its `Track`, lands the ones whose time is up exactly once
// (at `Start + Flight`, so it lands when the damage does) and reports where
// the others are. The extension that draws them (cores, halos, and the
// trails through C3) is in AcEffectsArcs.cpp and registers itself with
// `UAcEffects` (AcEffectsExtension.h); nothing else needs to include this.
#pragma once

#include "CoreMinimal.h"

#include "AcEffectsExtension.h"

class AUTOCRAFT_API FAcArcFlights
{
public:
	struct FFlight
	{
		int32 Slot = 0;
		FVector From = FVector::ZeroVector;
		/// Where it comes down: the target's latest tracked point.
		FVector To = FVector::ZeroVector;
		double Start = 0.0, Flight = 0.0;
		/// The arc's height in cm.
		double ArcCm = 0.0;
		TFunction<TOptional<FVector>()> Track;
		TFunction<void(double, const FVector&)> Land;
	};

	explicit FAcArcFlights(int32 InSlots = 1) : Slots(FMath::Max(1, InSlots)) {}

	/// Fly `L` from slot `Next`; a flight still in that slot lands now.
	/// Returns the slot. `L.Arc` is in cells (Swift units), positions cm.
	int32 Launch(FAcLaunch&& L);
	/// Home, land and place: `Place(flight, position, direction)` for each
	/// flight still in the air (direction unit, along its velocity);
	/// `Landed(flight)` before each landing's callback.
	void Fly(double Time, TFunctionRef<void(const FFlight&, const FVector&, const FVector&)> Place,
		TFunctionRef<void(const FFlight&)> Landed);
	/// Drop everything without landing it (a new game).
	void Clear() { Flying.Reset(); }

	int32 Num() const { return Flying.Num(); }
	int32 SlotCount() const { return Slots; }
	const TArray<FFlight>& All() const { return Flying; }

	/// The point a fraction `U` (0…1) of the way, `ArcCm` up at the middle:
	/// `from + (to − from)·u + up·arc·4u(1 − u)`.
	static FVector Position(const FVector& From, const FVector& To, double ArcCm, double U);
	/// The way it heads there (not normalised).
	static FVector Velocity(const FVector& From, const FVector& To, double ArcCm, double U);

private:
	int32 Slots = 1;
	int32 Cursor = 0;
	TArray<FFlight> Flying;
};
