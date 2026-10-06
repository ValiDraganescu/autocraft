// Chunk C4 (GAME-LAYER.md §2.7): deaths 1. The infantry falls of
// `GameScene.died` (GameScene.swift:1367) and `GameScene.fall` for the
// Ranger (:1451), the Juggernaut (:1417) and the Comet (:1476), their
// blood marks, and `Effects.debris` (Effects.swift:542), as a `UAcEffects`
// extension (AcEffectsExtension.h, registered in AcEffectsDeaths.cpp).
//
// How a death plays:
//   1. At the `Renderer` stage the renderer drops the unit and fires
//      `OnRemoved`; for a Ranger, Juggernaut or Comet the extension keeps
//      the last pose the renderer drew (`ForEachObject`: part locals,
//      visibility, emission, placement). `OnRemoved` also fires when an
//      enemy merely leaves sight, so nothing happens yet.
//   2. At the `Effects` stage a fog-gated `Died` event for that id turns
//      the kept pose into a body (`AcDeaths::Start`); kept poses with no
//      `Died` are dropped. A unit hidden when it died (aboard a Dropship,
//      in a Bastion) leaves no body, as Swift's `root.isHidden`.
//   3. The body is drawn in its own instance pools (one per mesh of the
//      model, `MakePool`, a ring of bodies per kind; the oldest is taken
//      over when the ring is full) and posed every update by
//      `AcDeaths::Pose`: the Swift tick in closed form (topple, lie, sink,
//      fade). Opaque M_Hull/M_Emissive parts are drawn on
//      `M_AcHullFade`/`M_AcEmissiveFade` (Tools/Editor/make_corpse_materials.py:
//      masked, a dither of 1 − custom data 2) with the model instance's
//      parameters copied; the other parts (additive glows, the smear) fade
//      by their emission.
//   4. The blood decal goes through `UAcEffects::Decal` (C2's marks); the
//      Comet's jetpack blast through `Explosion` (C3); the Juggernaut's
//      landing dust through `DustPuff` after 0.5 s.
//
// Not here: the Prospector (Swift blows it apart: `blowApart`, chunk C5,
// with its blast and dark mark) and every vehicle, flyer and building (C5).
//
// Debris (`HandleDebris`): charred boxes (engine cube on M_AcHullFade with
// `lib.hull` × 0.14, metal 0.45, rough 0.85) thrown from a point, ballistic
// to the ground, one skid, faded over the last quarter of 2.8 s; a ring of
// 160 chunks.
//
// Dev: `-AcDeathSheet=KIND -AcDeathSheetSpec=JSON` (AcEffectsDeathsSheet.h)
// draws one kind's fall at several moments in a row, for stills beside the
// Swift game's own `GameScene.fall`. `-AcDebrisDemo` with `-AcStageFight=N`
// throws a shell's debris at the fight's middle every 0.4 s.
#pragma once

#include "CoreMinimal.h"

#include "AcPose.h"

struct FAcModelInfo;

namespace AcDeaths
{
	/// The infantry that falls (`GameScene.died`'s `fall` cases).
	enum class EFall : uint8
	{
		Ranger,
		Juggernaut,
		Comet,
	};
	AUTOCRAFT_API TOptional<EFall> FallOf(ac::UnitKind Kind);

	/// A body as it falls: the last pose the renderer drew, the parts the
	/// fall turns, and where it lies.
	struct FBody
	{
		EFall Kind = EFall::Ranger;
		const FAcModelInfo* Model = nullptr;
		int64 Unit = 0;
		/// The Swift `body` node: the root's world position, the unit's
		/// heading (Unreal, cm).
		FTransform Body = FTransform::Identity;
		/// The last pose's part locals, visibility and emission, with the
		/// flashes, flames and halos hidden (Swift hides them as it falls).
		TArray<FTransform> Local;
		TArray<bool> Visible;
		TArray<float> Emission;
		TArray<FAcPose::FMeshEmission> MeshEmission;
		/// Material emissions overriding the pose's (the Comet's dimmed
		/// `jetGlow` and `eyeGlow`), relative to the export's.
		TArray<TPair<FName, float>> MaterialEmission;
		/// Parts the fall turns (the Ranger's `gun`, the Juggernaut's
		/// launchers) and their SceneKit position and Euler angles at death.
		struct FTurned
		{
			int32 Part = INDEX_NONE;
			FVector Pos = FVector::ZeroVector;
			FVector Euler = FVector::ZeroVector;
			FVector Scale = FVector::OneVector;
		};
		TArray<FTurned> Turned;
		/// +1 or −1: the side it falls to (`unit % 2`).
		double Side = 1.0;
		/// Seconds the body stays (Swift `life`).
		double Life = 7.0;
	};

	/// The body of a unit of `Kind` (id `Unit`) whose last drawn pose was
	/// `Last` (its `Placement` and `Local`/`Visible`/`Emission`).
	AUTOCRAFT_API FBody Start(EFall Kind, int64 Unit, const FAcModelInfo& Model, const FAcPose& Last);

	/// The body `Sec` seconds after it died: each part's world transform
	/// and visibility (folded down the tree), and the whole body's opacity
	/// (Swift's `root.opacity`).
	AUTOCRAFT_API double Pose(const FBody& Body, double Sec, TArray<FTransform>& World, TArray<bool>& Visible);

	/// Mesh `Mesh` of part `Part`'s emission scale on the body.
	AUTOCRAFT_API float MeshEmission(const FBody& Body, int32 Part, int32 Mesh);

	/// The fading masters (make_corpse_materials.py), or null.
	AUTOCRAFT_API UMaterialInterface* HullFade();
	AUTOCRAFT_API UMaterialInterface* EmissiveFade();

	/// How a model mesh is drawn while it fades: a dynamic instance of the
	/// fading master with the model material's values (fade by custom data
	/// 2: `bDither`), or the model's own material (fade by scaling the
	/// emission, custom data 1).
	struct FFadeMaterial
	{
		UMaterialInterface* Material = nullptr;
		bool bDither = false;
	};
	AUTOCRAFT_API FFadeMaterial FadeMaterial(UMaterialInterface* Source, UObject* Outer);
}
