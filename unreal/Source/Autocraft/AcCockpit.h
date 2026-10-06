// `UAcCockpitSubsystem`: the first-person view models of the nine drivable
// kinds (GAME-LAYER.md §2.12, chunk E5), the port of `Models+Cockpit.swift`
// (Ranger, Prospector), `Models+Cockpit2.swift` (Comet, Firefly, Juggernaut,
// Longbow, Dropship; `ViewLean`, `CockpitFrame`, `CockpitCues`, `HeldPart`),
// `Models+Cockpit3.swift` (Kestrel, Hailstorm), `Aim.swift` and
// `PilotCamera.animate`/`fitVisor`.
//
// The models are the export's `cockpit_<kind>_blue` (A4; one static mesh per
// part and material, the team as custom data 0, emission as custom data 1).
// While a unit is driven in first person they hang off the pilot pawn's
// `CockpitMount()` as plain static mesh components with
// `FirstPersonPrimitiveType = FirstPerson`: drawn only for the eye, with no
// shadow, no Nanite (moving Nanite smears under TSR on Metal), their own
// field of view (= the scene's: Swift draws them with the same camera) and
// pulled toward the eye by `ac.CockpitScale` (0.5) so they never cut into a
// cliff or a wall (the camera's near plane, 4 cm, still clears the nearest
// rim at that scale). Lights and particles that belong to the cockpit (a
// muzzle's flash light, the drill's weld light and sparks, a launcher's
// smoke) are placed in that scaled space; the muzzles handed to the effects
// are the unscaled points (on the same line of sight from the eye, out where
// the rounds really leave).
//
// Rig and level parts (the Swift frames): every cockpit is laid out in the
// camera's frame (SceneKit: x right, y up, −Z ahead). Parts under `rig` turn
// with the view (and sway with the stride); a vehicle's `level` part has the
// view's pitch taken off again (`unpitch`), so the machine under it (the
// Firefly's scorpion, the Longbow's hull, the Hailstorm's mount) stays level
// while the eye looks up and down. Those three carry the unit's own model
// (`model_<part>`), posed by the unit's registered pose function (B3/B4) on
// a copy of the driven unit, then turned under the eye by the turret's
// angle to the hull as Swift does; their parts reparented by the cockpit
// (the Longbow's cannon, the Hailstorm's cradle) keep that offset.
//
// Each frame, at the `Other` frame stage (after the pawn placed the camera
// at `Hud`): the pose, the components, the cockpit's lights and particles,
// `Eye().SetLean` (applied by the next frame's `Follow`, as Swift), and the
// muzzles (world cm, in `Trooper.muzzles` order) into `UAcEffects::SetPilot`.
// Hidden in third person (E3), inside a Derrick, and in the take-over dive.
//
// Console and command line:
//   ac.Cockpit 0|1           draw the cockpits (1)
//   ac.CockpitScale S        first-person scale toward the eye (0.5; 1 = Swift)
//   ac.CockpitLightGain G    a cockpit light's SceneKit intensity × 2.9/1000 × G
//                            unitless (1500, by eye against Swift)
//   -AcCockpitSince=S        pose as if the unit fired S seconds ago (stills)
//   -AcCockpitArms=minigun,shield  a Ranger's side has them (stills)
//   -AcCockpitJump=J         a Comet mid-jump at J (0…1) (stills)
//   -AcCockpitAnchor=S       a Longbow's anchor at S (0…1) (stills)
//   -AcCockpitHeal           a Dropship's beam on the unit under the sight
//   -AcCockpitMining         a Prospector's drill running (stills)
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "AcPilotCamera.h"
#include "AcPose.h"

#include <memory>

#include "AcCockpit.generated.h"

class UAcSimSubsystem;
class UMaterialInterface;
class UPointLightComponent;
class UStaticMeshComponent;
class USceneComponent;
class AAcPilotPawn;
struct FAcFrame;
struct FAcModelInfo;
namespace ac
{
	struct TerrainField;
}

/// What the scene and the simulation tell a cockpit each frame besides the
/// unit (`CockpitCues` + `CockpitFrame`).
struct FAcCockpitCues
{
	double Time = 0.0;
	double Dt = 0.0;
	/// When the unit last fired (game time) and the seconds since (100: never).
	TOptional<double> Shot;
	double Since = 100.0;
	/// Turn rate (rad/s) and speed (cells/s), smoothed (`FAcPilotCamera::Track`).
	double Turn = 0.0, Speed = 0.0;
	/// The view's pitch (`unpitch` takes it off).
	double Pitch = 0.0;
	/// The world point (cm) a Dropship's heal beam reaches.
	TOptional<FVector> BeamTo;
	int32 Slots = 0;
	int32 Room = 8;
	/// How far the last flame reached (cells).
	TOptional<double> Flame;
	bool bMinigun = false;
	bool bShield = false;
	/// The world point (cm) under the crosshair.
	TOptional<FVector> Aim;
	/// The view's width over its height (`fitVisor`).
	double Aspect = 1.6;
};

/// One cockpit's parts this frame: the cockpit-model counterpart of
/// `FAcPose`, plus what is not a transform.
struct FAcCockpitPose
{
	const FAcModelInfo* Model = nullptr;
	/// Part → parent part, Unreal space (cm), the cockpit's own frame
	/// (SceneKit camera axes converted by AcSpace: x right, y back, z up).
	TArray<FTransform> Local;
	TArray<bool> Visible;
	TArray<float> Emission;
	TArray<FAcPose::FMeshEmission, TInlineAllocator<8>> MeshEmission;
	/// A (part, material) mesh drawn with another material (a lamp lit).
	struct FMaterialSwap
	{
		int32 Part = INDEX_NONE;
		UMaterialInterface* Material = nullptr;
	};
	TArray<FMaterialSwap, TInlineAllocator<24>> Swaps;
	/// Lights of the cockpit (cockpit frame, cm; SceneKit intensity and reach in cells).
	struct FLight
	{
		FVector At = FVector::ZeroVector;
		FLinearColor Color = FLinearColor::White;
		float Intensity = 0.f;
		float Reach = 1.f;
	};
	TArray<FLight, TInlineAllocator<2>> Lights;
	/// A running particle emitter (cockpit frame; the kind's SceneKit
	/// direction turned by the frame's rotation).
	struct FEmitter
	{
		uint8 Kind = 0;
		FTransform Frame;
		double Rate = 0.0;
		double Size = -1.0, Speed = -1.0;
	};
	TArray<FEmitter, TInlineAllocator<2>> Emitters;
	/// A crystal bite at this cockpit point this frame (cm).
	TOptional<FVector> Bite;
	/// The view's lean (`ViewLean`).
	FAcViewLean Lean;
	/// The muzzle parts, in `Trooper.muzzles` order.
	TArray<int32, TInlineAllocator<4>> Muzzles;
	bool bHidden = false;

	/// Each part's transform to the cockpit root (cm), from `Local`.
	TArray<FTransform> Root;
	void Fold();
};

UCLASS()
class AUTOCRAFT_API UAcCockpitSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UAcCockpitSubsystem* Get(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Deinitialize() override;

	/// The cockpit model's parts this frame (null while none is shown).
	const FAcCockpitPose* Pose() const { return Kind ? &Posed : nullptr; }
	/// The muzzles this frame (world cm), as handed to the effects.
	const TArray<FVector>& Muzzles() const { return MuzzlesWorld; }

	/// The per-kind poses (AcCockpitKinds.cpp). Opaque per-ride memory.
	struct FState;

private:
	void OnFrame(const FAcFrame& Frame);
	void Build(ac::UnitKind Kind, int64 Owner);
	void Clear();
	void Apply(AAcPilotPawn& Pawn);
	FAcCockpitCues Cues(const AAcPilotPawn& Pawn, const ac::Unit& U, const FAcFrame& Frame);

	FDelegateHandle FrameHandle;
	TOptional<ac::UnitKind> Kind;
	int64 UnitId = INDEX_NONE;
	FAcCockpitPose Posed;
	std::shared_ptr<FState> State;
	TArray<FVector> MuzzlesWorld;
	double LastTime = -1.0;
	double LastDt = 0.0;
	bool bLoggedMuzzles = false;
	const FAcModelInfo* CockpitModel = nullptr;

	struct FMeshComp
	{
		int32 Part = INDEX_NONE;
		int32 Mesh = INDEX_NONE;
		TObjectPtr<UStaticMeshComponent> Component;
		UMaterialInterface* Material = nullptr;
		float LastEmission = -1.f;
		bool bShown = true;
	};
	TArray<FMeshComp> Comps;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> CompRefs;
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> Frame;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> Lights;

	const void* FieldMap = nullptr;
	std::unique_ptr<ac::TerrainField> Field;
};
