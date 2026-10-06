// The per-kind cockpit poses of `UAcCockpitSubsystem` (AcCockpit.h, chunk
// E5): `PilotCamera.animate` and the `pose` of each `Cockpit` case
// (Models+Cockpit*.swift), in SceneKit's camera frame (cells, x right, y up,
// −Z ahead), written into the cockpit model's parts as Unreal transforms.
#pragma once

#include "CoreMinimal.h"

#include "AcCockpit.h"

#include "Simulation.h"

#include <memory>

class UAcSimSubsystem;
class UAcWorldRenderer;
class UAcEffects;

/// What a kind's pose reads besides its own memory.
struct FAcCockpitContext
{
	/// The driven unit as the view follows it (a turret's `aim` = the look).
	const ac::Unit& Unit;
	const FAcCockpitCues& Cues;
	const ac::Simulation& Sim;
	const ac::TerrainField& Field;
	/// The renderer memory of the driven unit (its last shot, flame), or null.
	const FAcUnitMemory* Memory = nullptr;
	/// The crosshair's point in the cockpit's root frame (cm), and the heal
	/// beam's end; unset if none.
	TOptional<FVector> AimRoot;
	TOptional<FVector> BeamRoot;
	/// A Comet's jump (0…1) as the stills may force it.
	TOptional<double> Jump;
	bool bMining = false;
};

namespace AcCockpitKinds
{
	/// A new ride's memory for the kind (null: no cockpit model for it).
	std::shared_ptr<UAcCockpitSubsystem::FState> Make(ac::UnitKind Kind, const FAcModelInfo& Cockpit);
	/// The frame's pose: `P` starts at the rest pose (Local, Visible,
	/// Emission); the kind moves its parts, says how the view leans and
	/// which parts are the muzzles.
	void Pose(UAcCockpitSubsystem::FState& S, const FAcCockpitContext& C, FAcCockpitPose& P);
	/// The Dropship's and the Kestrel's lamp materials, set by the subsystem.
	struct FLamps
	{
		UMaterialInterface* EnergyLit = nullptr;
		UMaterialInterface* CargoLit = nullptr;
		UMaterialInterface* Unlit = nullptr;
	};
	void SetLamps(UAcCockpitSubsystem::FState& S, const FLamps& Lamps);
}
