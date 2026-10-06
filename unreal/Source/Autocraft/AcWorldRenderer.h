// `UAcWorldRenderer`: every unit and building drawn as instances
// (GAME-LAYER.md §2.6, §3.2-3.3, chunk B1). The Swift game gives each unit
// its own node tree (`GameScene.addUnit`/`addBuilding`/`sync`); here every
// (model, part, material) of the catalog is one instanced static mesh
// component (a *batch*), shared by every player (the team is per-instance
// custom data 0), and each unit or building owns one instance in each batch
// of its model.
//
// Each frame, at the `Renderer` stage of `UAcSimSubsystem`'s frame:
//   1. The frame's `Shot`/`Missed` events stamp `FAcUnitMemory::LastShot`.
//   2. Objects follow `Shown()` (`sim.shown(0)`: enemy units in sight,
//      enemy buildings as last seen): new ids get instance slots (a batch's
//      free list first, so nothing is removed or re-added each frame), ids
//      gone free theirs (`OnRemoved` fires first, with the last pose, for
//      the death effects).
//   3. Poses, in parallel (`ac.RenderParallel`): the rest pose
//      (`AcPose::RestUnit`/`RestStructure`), the hide rules, then the kind's
//      pose function if one is registered (AcPose.h). Part world =
//      part local × parent world × … × placement.
//   4. Changed instances are written into each batch's transform array and
//      sent once per batch (`BatchUpdateInstancesTransforms` over the dirty
//      range, render state marked dirty once); emission changes go to
//      custom data 1 the same way. A pose that does not move with time
//      (`bAnimated` false) and whose placement did not change is not sent.
//
// Hide rules (`GameScene.sync` :640-740): a unit aboard a Dropship, in a
// Bastion or in a Derrick, and the unit driven in first person
// (`SetHiddenUnit`), keep their slots at scale 0. Buildings under
// construction rise out of the ground (`AcPose::ConstructionSink`) inside
// their scaffold model (`scaffold_<kind>`), shown until complete.
//
// Outlines (AcOutline.h): an object whose `AcOutline::StencilFor` is set (a
// buried Scorpion) draws from a second set of components per stencil value
// (custom depth + stencil; the stencil is per primitive, not per instance):
// its slots move to that set when the value changes, and the outline pass
// (the world actor's post-process component) is enabled only while one
// object has a value. `ac.Outline 0` turns it off.
//
// Console: `ac.RenderNanite 0|1` (units' Nanite meshes as Nanite; off by
// default: moving Nanite instances smear under TSR, see the cvar), `ac.RenderHISM 0|1` (batches as HISM instead of ISM; read when
// batches are made, so restart the game), `ac.RenderParallel 0|1`, `ac.RenderShowAll 1` (dev: draw
// every player's objects, fog ignored),
// `ac.RenderStats N` (log a `render:` line every N seconds; 0 off).
// Command line (dev staging for shots and the perf run):
//   -AcStageFight=N  Bench.stageFight: two armies of N face to face
//                    between the first two players' mains (bench default 29).
//   -AcStage=N       N units for every player, in rows in front of its main
//                    facing the map's middle (the 8-player late-game load).
//   -AcStageBuildings  also a late game's buildings around every main
//                    (finished: Garrisons, Foundry, Spacedock, Labs, Hab
//                    Domes, Bastion, Sentinels).
//   -AcStageBuried=owner|enemy  two buried Scorpions before the local
//                    player's main (owner: its own, one more walking;
//                    enemy: the first enemy's, shown by a reload), for the
//                    outline's shots; `-AcStageBuriedAt=D` how far out
//                    (default 9 cells); `-AcCamAtArmy` centres on them.
//   -AcCamAtArmy     with -AcStageFight or -AcStageBuried: centre the camera on the fight
//                    (as the bench's `fight`/`wide` shots).
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"

#include "AcPose.h"

#include <memory>

#include "AcWorldRenderer.generated.h"

class UInstancedStaticMeshComponent;
class UPostProcessComponent;
class UAcSimSubsystem;
struct FAcFrame;
struct FAcModelInfo;

DECLARE_MULTICAST_DELEGATE_ThreeParams(FAcObjectRemoved, int64 /*Id*/, const FAcModelInfo& /*Model*/,
	TConstArrayView<FTransform> /*PartWorld*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FAcPoseCueEvent, int64 /*Id*/, const FAcPoseCue& /*Cue*/);

UCLASS()
class AUTOCRAFT_API UAcWorldRenderer : public UActorComponent
{
	GENERATED_BODY()

public:
	UAcWorldRenderer();

	/// The renderer of `WorldContext`'s world (null if none was spawned).
	static UAcWorldRenderer* Get(const UObject* WorldContext);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/// Bring the instances up to this frame (the `Renderer` stage).
	void Sync(const FAcFrame& Frame);
	/// Drop every object and instance (a new game, a new map).
	void Clear();

	/// The unit driven in first person: its model is hidden (INDEX_NONE: none).
	void SetHiddenUnit(int64 Id) { HiddenUnit = Id; }
	/// The unit driven (third person too): its pose aims at the crosshair.
	void SetDrivenUnit(int64 Id) { DrivenUnit = Id; }
	/// The crosshair's point (world cm): the driven unit's held guns aim at
	/// it (`FAcPoseContext::PilotAim`; E4). Unset: none.
	void SetPilotAim(const TOptional<FVector>& Aim) { PilotAim = Aim; }
	/// Driving: the others' buildings out of sight (`AAcFog::Unseen`) are not
	/// drawn at all, not even as last seen (`GameScene.veiled`; E4).
	void SetVeiled(bool bInVeiled) { bVeiled = bInVeiled; }

	/// An id's model and its parts' world transforms this frame (cm), for
	/// the rays, the muzzles of the effects, life bars. Null if not drawn.
	const FAcModelInfo* ModelOf(int64 Id) const;
	TConstArrayView<FTransform> PartWorld(int64 Id) const;
	/// An id's renderer memory (null if not drawn).
	FAcUnitMemory* Memory(int64 Id);
	/// `-AcStageFight`'s middle (cells), once staged: `-AcCamAtArmy` centres
	/// the RTS camera there (`AAcRtsPawn::ApplyCommandLine`).
	TOptional<FVector2D> StagedFightCentre() const { return FightCentre; }
	/// Every drawn unit and building, for the rays (E1, AcRaysWorld.h): its
	/// id, whether it is a unit, its model, this frame's pose (`Visible` not
	/// yet folded down to the children; `bHidden` whole) and its parts' world
	/// transforms (stale while `Pose.bHidden`). Game thread, outside `Sync`.
	void ForEachObject(TFunctionRef<void(int64 Id, bool bUnit, const FAcModelInfo& Model, const FAcPose& Pose,
		TConstArrayView<FTransform> PartWorld)> Fn) const;

	/// Fired before an id's instances are freed (it died, left sight, ...).
	FAcObjectRemoved OnRemoved;
	/// Cues from the poses (sparks, flashes), on the game thread.
	FAcPoseCueEvent OnCue;

	struct FStats
	{
		int32 Objects = 0, Batches = 0, Instances = 0, Posed = 0, Uploaded = 0, Culled = 0, Outlined = 0;
		double PrepareMs = 0, PoseMs = 0, WriteMs = 0, UploadMs = 0;
		/// ac.RenderCheckPrev: instances sent, and those whose previous transform was another object's.
		int32 PrevChecked = 0, PrevForeign = 0;
	};
	const FStats& LastStats() const { return Stats; }

private:
	struct FBatch
	{
		UInstancedStaticMeshComponent* Component = nullptr;
		/// What each instance should show this frame, and what it showed last
		/// frame (sent as the previous transforms, for TSR's motion vectors).
		TArray<FTransform> Xf;
		TArray<FTransform> Shown;
		/// ac.RenderCheckPrev (a dev check): the object each slot belongs to
		/// now (-1: free), and the one whose transform `Shown` holds.
		TArray<int64> Owner, ShownOwner;
		/// Logged once: its previous transforms stopped matching its instances.
		bool bWarnedPrev = false;
		TArray<float> Custom;
		TArray<int32> Free;
		int32 InComponent = 0;
		/// The mesh's reach from its pivot, cm (its bounds, P2).
		double Radius = 0;
		/// Last frame's uploaded range: sent again so what stopped moving
		/// gets its previous transform caught up (no stale velocity).
		int32 LastMin = MAX_int32, LastMax = -1;
		int32 DirtyMin = MAX_int32, DirtyMax = -1;
		int32 CustomMin = MAX_int32, CustomMax = -1;
		/// Which slots changed this frame and last (P2: only their runs are
		/// sent, so units culled between them cost nothing).
		TBitArray<> DirtyBits, LastBits;

		void Dirty(const int32 I)
		{
			DirtyMin = FMath::Min(DirtyMin, I);
			DirtyMax = FMath::Max(DirtyMax, I);
			if (DirtyBits.Num() <= I) DirtyBits.Add(false, I + 1 - DirtyBits.Num());
			DirtyBits[I] = true;
		}
		void DirtyCustom(const int32 I)
		{
			CustomMin = FMath::Min(CustomMin, I);
			CustomMax = FMath::Max(CustomMax, I);
		}
		int32 Live() const { return Xf.Num() - Free.Num(); }
	};

	/// A model's meshes flattened: mesh k is part `Part[k]`, drawn by batch `Batch[k]`.
	struct FLayout
	{
		const FAcModelInfo* Model = nullptr;
		/// The custom stencil value its components write (0: none, AcOutline.h).
		uint8 Stencil = 0;
		TArray<int32> Part;
		TArray<int32> Batch;	};

	struct FObject
	{
		int64 Id = 0;
		bool bUnit = true;
		int32 Team = 0;
		const FLayout* Layout = nullptr;
		/// A building's scaffold (shown while it is built).
		const FLayout* Scaffold = nullptr;
		TArray<int32> Slots;
		TArray<int32> ScaffoldSlots;
		FAcUnitMemory Memory;
		FAcPose Pose;
		TArray<FTransform> World;
		TArray<float> LastEmission;
		FTransform LastPlacement;
		/// This frame's state (valid during Sync).
		const ac::Unit* Unit = nullptr;
		const ac::Structure* Structure = nullptr;
		uint32 SeenFrame = 0;
		/// The outline's stencil value this object draws with (its layout's).
		uint8 Stencil = 0;
		bool bFresh = true;
		bool bChanged = true;
		bool bDrawnHidden = false;
		bool bScaffoldShown = false;
		bool bWantScaffold = false;
		/// Off screen (P2, `ac.RenderCull`): posed, but nothing written.
		bool bCulled = false;
		/// A remembered enemy building out of sight (fog): posed at the moment it left sight, unlit.
		bool bFrozen = false, bFrozenDrawn = false;
		double FrozenAt = 0.0;
	};


	void OnGameStarted(UAcSimSubsystem& Sim);
	void OnFrame(const FAcFrame& Frame);
	const FLayout* LayoutFor(const FAcModelInfo* Model, uint8 Stencil = 0);
	int32 BatchFor(const FAcModelInfo& Model, int32 Part, int32 Mesh, uint8 Stencil = 0);
	void Allocate(FObject& O);
	void Free(FObject& O);
	void WriteObject(FObject& O);
	void Upload();
	/// The local player's view (P2 culling); unset: none to cull by.
	TOptional<struct FConvexVolume> CullVolume() const;
	void Stage(UAcSimSubsystem& Sim);

	TArray<FBatch> Batches;
	TMap<uint64, int32> BatchIndex;
	TMap<TTuple<const FAcModelInfo*, uint8>, TUniquePtr<FLayout>> Layouts;
	TArray<TUniquePtr<FObject>> Objects;
	TMap<int64, int32> ObjectIndex;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Components;

	/// The ground for the poses: the sim's map's own field.
	std::unique_ptr<ac::TerrainField> Field;
	const void* FieldMap = nullptr;

	FDelegateHandle FrameHandle, StartedHandle;
	int64 HiddenUnit = INDEX_NONE;
	int64 DrivenUnit = INDEX_NONE;
	TOptional<FVector> PilotAim;
	bool bVeiled = false;
	TOptional<double> LastSync;
	uint32 FrameNumber = 0;
	/// ac.RenderCheckPrev: warnings logged so far (the first 40 are).
	int32 PrevForeignLogged = 0;
	int64 PrevCheckedSum = 0, PrevForeignSum = 0;
	bool bFirstSync = true;
	bool bStaged = false;
	TOptional<FVector2D> FightCentre;
	FStats Stats;
	double StatsWall = 0.0;
	struct FStatsSum
	{
		int32 Frames = 0;
		double Prepare = 0, Pose = 0, Write = 0, Upload = 0, Max = 0;
		int64 Uploaded = 0;
		double Draws = 0, Prims = 0;
	} Sum;
};

/// The actor holding the world's renderers (spawned by the game mode).
UCLASS()
class AUTOCRAFT_API AAcWorld : public AActor
{
	GENERATED_BODY()

public:
	AAcWorld();
	static AAcWorld* SpawnFor(UWorld* World);
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Autocraft")
	TObjectPtr<UAcWorldRenderer> Renderer;
	/// The outline pass (AcOutline.h): unbound, on while an object has a stencil value.
	UPROPERTY(VisibleAnywhere, Category = "Autocraft")
	TObjectPtr<UPostProcessComponent> Outline;
};
