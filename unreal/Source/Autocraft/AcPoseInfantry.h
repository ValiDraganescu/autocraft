// `AcPoseInfantry`: how a Prospector and a Ranger stand each frame (chunk B2,
// GAME-LAYER.md §2.6). Ports of the Swift `GameScene.pose` for both
// (`GameScene.swift:901`, `:1015`), `soleHeights` (:960), `gunAngles`
// (:1091), `spinMinigun` (:1108), `rangerAim` (:991), `RangerShin.bend` and
// `Ranger.guardPose` (`Models+Ranger.swift`), and `ProspectorFork.pose` /
// `ForkMotion` (`Models+Fork.swift`). Registered with the renderer through
// `FAcPoseRegistration` (AcPose.h) in AcPoseInfantry.cpp.
//
// The math runs in SceneKit's frame (cells, Y up, the Swift constants as
// they are) and each moved part's local transform is converted once with
// AcSpace.h. Two parts of the Ranger's export (`head`, `leftArm`) have the
// torso folded into their rest transform (the torso node has no geometry of
// its own): their poses are composed with `TorsoLean` the same way.
//
// Materials: `Tools/Editor/make_infantry_materials.py` lights the drill
// bit's and the Mini gun tips' instances (exported dark) and puts the smear
// sleeve on `M_AcSmear` (opacity from custom data 1); the poses set them
// through `MeshEmission`/`Emission`.
// Not done here: the driven Ranger's gun on the crosshair (`pilotAim`,
// E2/E5); a flyer target's hull height is estimated from its ground and its
// hover (Swift reads the posed Dropship/Kestrel body).
// Cues (`FAcPoseCue`, for the effects and lamp chunks):
// - `AcDrill` every frame a Prospector drills: `At` the spark emitter
//   (the drill's tip), `Value` the weld light's intensity (SceneKit units,
//   22 ± 14, 1.3-cell reach); sparks at 90 a second while it lasts.
// - `AcCrystalBite` the frame the fork bites a nodule off (`effects.crystalBite`
//   at the fork's load).
#pragma once

#include "CoreMinimal.h"

#include "AcPose.h"

namespace AcPoseInfantry
{
	/// The cue names (see above).
	AUTOCRAFT_API FName CueDrill();
	AUTOCRAFT_API FName CueCrystalBite();

	/// GameScene.pose (Prospector): walk, idle, drill, the cutter strike
	/// (`Memory.LastShot`), the fork's round (`ForkMotion`, in the memory).
	AUTOCRAFT_API void PoseProspector(const FAcPoseContext& C, FAcPose& P);

	/// What a Ranger carries: the Mini gun in place of the rifle, the Aegis
	/// shield on its forearm (its side's upgrades).
	struct FRangerArms
	{
		bool bMinigun = false;
		bool bShield = false;
	};
	/// GameScene.pose (Ranger), its gun on `Aim` (Unreal space, cm, the
	/// placement's frame: the world in the game) or at ease.
	AUTOCRAFT_API void PoseRanger(const FAcPoseContext& C, FAcPose& P, const TOptional<FVector>& Aim, FRangerArms Arms);
	/// GameScene.rangerAim: its target's chest (a building's near face, a
	/// flyer's hull) while it fights or for 1.5 s after a shot, else where
	/// it last shot; unset at ease.
	AUTOCRAFT_API TOptional<FVector> RangerAim(const FAcPoseContext& C);

	/// How far (cm, the worst translation or rotation-matrix entry × 100) the
	/// Ranger's composed rest transforms of `head`, `leftArm` and `shield`
	/// are from the export's: a check of the torso fold and the axes.
	AUTOCRAFT_API double RangerFoldError(const FAcModelInfo& Ranger);

	/// GameScene.chestHeight: where rounds meet a unit, cells over its feet.
	AUTOCRAFT_API double ChestHeight(ac::UnitKind Kind);
	/// `Building.radius` (GameScene.makeBuilding), cells.
	AUTOCRAFT_API double BuildingRadius(ac::StructureKind Kind);
}
