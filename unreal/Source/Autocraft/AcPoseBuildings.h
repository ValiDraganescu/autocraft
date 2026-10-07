// The buildings' poses (chunks B6 and B7, GAME-LAYER.md §2.6): what
// `GameScene.animate` (GameScene.swift:446-615) and the per-kind
// `GameScene.animate` extensions (`Models+Foundry.swift`,
// `Models+Spacedock.swift`, `Models+Lab.swift`, `Models+Derrick.swift`)
// do to a building every frame, as `FAcPoseRegistration` pose functions:
//
// - Construction: the body rises (`AcPose::RestStructure`) inside its
//   scaffold (the renderer), every light dark (0.05-0.1), and while its
//   Prospector welds, a weld point hops along the scaffold's top edge by the
//   golden angle (cue `AcWeld`: sparks and a flickering light, drawn by
//   `UAcBuildingFx`).
// - The light language (`.claude/skills/ringshadow-building-visuals`): the
//   red beacon (0.25 s of every 1.2 s once complete), the blue progress
//   lamps (quarters, the next one blinking) and queue lamps (one per item),
//   the yellow "working" glow (steady 1.2 idle, `0.05 + 2.6·b²` on and off
//   while active), the green pips and breathing.
// - Per kind: the Citadel (dish, hangar door strip), Hab Dome, Garrison
//   (shutter), Bastion (crew pips, slits, slit flashes) in
//   AcPoseBuildings.cpp; the Foundry (gantry, weld arm, door leaves),
//   Spacedock (guide lamps, iris doors, radar), Lab (dish, scanner),
//   Derrick (shutter, pistons, valves, throat) and Sentinel (head and pods
//   from `Simulation.stepTurret`'s `aim`) in AcPoseBuildings2.cpp.
//
// Emission: the export keeps each SceneKit material's intensity at build
// time (`emissiveIntensity` in the manifest); custom data 1 scales it, so a
// SceneKit intensity `I` is written as `I / exported`. Glow groups merged
// into a part with other meshes (the Citadel's `ventGlow` in its root) go
// through `FAcPose::MeshEmission`.
//
// Lights, sparks and steam are cues (`FAcPoseCue`), handled on the game
// thread by `UAcBuildingFx` (AcBuildingFx.h): `AcWeld` (Value = the weld
// light's SceneKit intensity), `AcSparks` (Value = sparks a second), a light
// socket's part name (`light_2`; Value = its SceneKit intensity), `AcSteam`
// (vent steam, `Models.steamVents`) and `AcVapour`, `AcPuffs` (a Derrick's MH vapour,
// `hydrogenVapour`), Value = puffs a second.
#pragma once

#include "CoreMinimal.h"

#include "AcModelCatalog.h"
#include "AcPose.h"
#include "AcSpace.h"

#include <cmath>

namespace AcBuildingPose
{
	/// `phase = time + id·0.37`: buildings blink out of step.
	inline double Phase(const FAcPoseContext& C) { return C.Time + double(C.Structure->id) * 0.37; }

	/// The yellow "working" glow while active: `0.05 + 2.6·b²`,
	/// `b = 0.5 + 0.5·sin((phase + offset)·2π/1.3)`.
	inline double Glow(const double Phase, const double Offset)
	{
		const double B = 0.5 + 0.5 * std::sin((Phase + Offset) * 2.0 * UE_DOUBLE_PI / 1.3);
		return 0.05 + 2.6 * B * B;
	}

	/// The red beacon: shown 0.25 s of every 1.2 s once complete.
	inline bool Beacon(const double Time, const bool bLive) { return bLive && std::fmod(Time, 1.2) <= 0.25; }

	inline double Smoothstep(const double A, const double B, const double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	/// A SceneKit node's `eulerAngles` (x pitch, y yaw, z roll; applied roll,
	/// then yaw, then pitch) → its rotation in Unreal.
	inline FQuat SkEuler(const double X, const double Y, const double Z)
	{
		const FQuat Sk = FQuat(FVector::XAxisVector, X) * FQuat(FVector::YAxisVector, Y) * FQuat(FVector::ZAxisVector, Z);
		return AcSpace::QuatFromSceneKit(Sk.X, Sk.Y, Sk.Z, Sk.W);
	}

	/// A part's index by name (INDEX_NONE if the model has none): a plain
	/// lookup in the model's map, safe from the parallel poses (no cache).
	struct FPart
	{
		explicit FPart(const TCHAR* InName) : Name(InName) {}
		int32 Get(const FAcModelInfo& Model) const
		{
			const int32* I = Model.PartIndex.Find(Name);
			return I ? *I : INDEX_NONE;
		}

	private:
		FName Name;
	};

	/// Numbered parts: `Name_0`, `Name_1`, ... as long as they exist (`progressLamps`).
	struct FParts
	{
		FParts(const TCHAR* InName, int32 InCount);
		int32 Num(const FAcModelInfo& Model) const;
		int32 operator()(const FAcModelInfo& Model, int32 K) const;

	private:
		TArray<FPart> Index;
	};

	/// A part's rotation set to SceneKit euler angles (translation and scale
	/// kept), its position set to a SceneKit position (cells), its scale.
	void SetEuler(const FAcModelInfo& Model, FAcPose& P, int32 Part, double X, double Y, double Z);
	void SetPosition(FAcPose& P, int32 Part, double X, double Y, double Z);
	void SetScale(FAcPose& P, int32 Part, double X, double Y, double Z);

	/// A whole part's emission to a SceneKit intensity (`Exported` = the
	/// intensity the export baked in).
	inline void Lamp(FAcPose& P, const int32 Part, const double Intensity, const double Exported)
	{
		if (Part != INDEX_NONE) P.Emission[Part] = float(Intensity / Exported);
	}

	/// Every mesh of material `Material` (the exported name), in any part,
	/// to a SceneKit intensity: a glow group (`ventGlow`).
	void GlowGroup(const FAcModelInfo& Model, FAcPose& P, FName Material, double Intensity, double Exported);

	/// The blue production lamps (`GameScene.productionLamps`, :604):
	/// progress lamps lit in quarters while `bOn`, the next one blinking
	/// (0.3 s of every 0.5 s); queue lamps one per item once complete.
	void ProductionLamps(const FAcModelInfo& Model, FAcPose& P, const FParts& Progress, const FParts& Queue,
		bool bOn, double Progress01, int64 QueueCount, bool bLive, double Phase, double Exported);

	/// A part's world location (cm), with its chain at rest under the
	/// placement (light and particle sockets on the root).
	FVector RestWorld(const FAcModelInfo& Model, const FAcPose& P, int32 Part);

	/// Where the building stands (cm): the placement without the
	/// construction sink.
	FVector Site(const FAcPoseContext& C, const FAcPose& P);

	/// A light socket's cue (its part name; Value = SceneKit intensity), only
	/// when lit.
	void Light(const FAcPoseContext& C, FAcPose& P, int32 Part, double Intensity);
	/// A particle socket's cue (`AcSparks`, `AcSteam`; Value = a second).
	void Emit(const FAcPoseContext& C, FAcPose& P, FName What, int32 Part, double Rate);

	/// The construction weld (`animate` :489-503): while `bWelding`, a cue
	/// `AcWeld` where the weld point is (hopping along the scaffold's top
	/// edge every 0.7 s by the golden angle), Value = the flash light.
	void Weld(const FAcPoseContext& C, FAcPose& P, bool bWelding, double Radius);

	/// The Prospector building `S` is welding it (`builder.task == .building`).
	bool Welding(const FAcPoseContext& C);

	/// What every building does first: the construction weld (`bWelding`),
	/// the beacon, `bAnimated` once complete. Returns `complete`.
	bool CommonPose(const FAcPoseContext& C, FAcPose& P, bool bWelding);

	/// Rate-limited easing per frame, as Swift steps it (`max(-k, min(k, want - now))`
	/// at 60 frames a second): `Step` per 1/60 s.
	inline double Ease(const double Now, const double Want, const double StepPerFrame, const double Dt)
	{
		const double K = StepPerFrame * Dt * 60.0;
		return Now + FMath::Clamp(Want - Now, -K, K);
	}

	/// A building's renderer memory for doors and the like.
	struct FDoorState : FAcPoseState
	{
		TOptional<double> Value;
		double Angle[2] = {0, 0};
	};

	/// A Bastion's slits (`slitShots`) and room, written on the game thread
	/// by `UAcBuildingFx` (a crew's shot is the crew unit's event).
	struct FBastionState : FAcPoseState
	{
		double Shots[5] = {-10, -10, -10, -10, -10};
		int32 Room = 4;
	};
	/// The slits' angles (radians round Y, 0 = +Z the front; `Models.bastion`
	/// :509-531): faces 0, 1, 2, 6, 7 of 8, in that order.
	inline double BastionSlitAngle(const int32 K)
	{
		static const int32 Faces[5] = {0, 1, 2, 6, 7};
		return double(Faces[FMath::Clamp(K, 0, 4)]) * UE_DOUBLE_PI / 4.0;
	}
}
