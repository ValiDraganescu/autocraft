// `AAcResources`: Stardust Ore deposits and Metallic Hydrogen wells (chunk
// B8, GAME-LAYER.md §2.6 "Ore and wells"), the port of `GameScene.addPatch`,
// `addWell`, `applyStage`, the plume switch in `GameScene.sync` and
// `ModelLibrary.oreDeposit`/`oreGlitter`/`well` (Sources/Autocraft).
//
// - Ore: each patch is the exported `ore_<id % 4>` (Swift seeds the shape
//   with the patch id; the export has seeds 0-3), at the field height, turned
//   by `patch.angle`. Its nodules are three parts, `stage1..3`: a nodule
//   `stageK` shows while the patch's stage is ≥ K, the whole deposit hides at
//   stage 0. Plain ISMs (movable), a part hidden by zero scale. The rock wears
//   `M_Opal` (make_resource_materials.py).
// - Wells: the map's wells (base by base, seed `i·10 + j`) and the
//   playground's (seed 1000 + k), the exported `well_0`/`well_3` turned so
//   the rocks round the mound stand where that seed puts them (their shapes
//   are the export's). Static HISMs. The pool wears `M_Mercury`.
// - Particles, drawn on the CPU as camera-facing quads in two ISMs (no
//   Niagara: a few thousand sprites, stateless, cheaper to keep exact):
//   the vapour plume off each well (7/s, 2.8 s; stops while a Derrick
//   stands on the well, and puffs already out finish rising) and the glitter
//   drifting off each deposit that still shows (6/s, 2.6 s), with the Swift
//   emitter shapes, speeds, size and opacity curves.
// - `MPC_AcWorld.Dark` ← `AAcDaylight::GetDark()` every frame, for the
//   opal's night glow and the mercury's dimmer studio sky (any material may
//   read it).
//
// Reads `UAcSimSubsystem::Shown()` (what the local player may know), so a
// patch's stage and a Derrick's plume follow what the team sees.
//
// Usage: `AAcResources::SpawnFor(World)` once per game world (AAcGameMode).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "AcScenery.h"
#include "Types.h"

#include "AcResources.generated.h"

class UAcSimSubsystem;
class UInstancedStaticMeshComponent;
class UMaterialParameterCollection;
struct FAcFrame;

UCLASS()
class AUTOCRAFT_API AAcResources : public AActor
{
	GENERATED_BODY()

public:
	AAcResources();

	static AAcResources* SpawnFor(UWorld* World);
	static AAcResources* Find(const UWorld* World);

	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	int32 NumPatches() const { return Patches.Num(); }
	int32 NumWells() const { return Wells.Num(); }

private:
	struct FPatch
	{
		int64 Id = 0;
		int64 Stage = -1;
		FAcSceneryModel Model;
		/// The parts `stage1..3` (index into the model's parts, or INDEX_NONE).
		int32 StagePart[3] = {INDEX_NONE, INDEX_NONE, INDEX_NONE};
		int32 RootPart = INDEX_NONE;
		/// The glitter emitter's frame (Unreal world).
		FTransform Emitter;
		/// Particle seed.
		uint32 Seed = 0;
	};
	struct FWell
	{
		ac::Vec2 Position;
		FAcSceneryModel Model;
		/// The vapour emitter (Unreal world, at the pool).
		FVector Emitter = FVector::ZeroVector;
		uint32 Seed = 0;
		/// Puffs born in [Open, Closed) rise (game seconds): the plume runs
		/// from when the well was last uncapped until it was capped.
		double Open = -1e9, Closed = 1e18;
		bool bCapped = false;
	};

	void OnGameStarted(UAcSimSubsystem& Sim);
	void OnFrame(const FAcFrame& Frame);
	void Rebuild(UAcSimSubsystem& Sim);
	void AddPatches(const ac::GameState& State, int32 From);
	void AddWell(ac::Vec2 Position, int32 Seed);
	void SyncWells(const ac::GameState& State);
	void ApplyStage(FPatch& Patch, int64 Stage);
	void DrawParticles(double Now);
	UInstancedStaticMeshComponent* MakeSprites(const TCHAR* Name, const TCHAR* MaterialPath);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Vapour;
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Glitter;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> WorldParameters;

	FAcSceneryBatches Ore;
	FAcSceneryBatches WellBatches;
	TArray<FPatch> Patches;
	TArray<FWell> Wells;
	/// Wells from the map (the rest were put down in the playground).
	int32 MapWells = 0;
	/// The map the scenery was built for.
	FString MapName;
	FDelegateHandle GameStartedHandle, FrameHandle;
	double LastDark = -1;
	/// Particle transforms and colours, reused every frame.
	TArray<FTransform> SpriteTransforms;
	TArray<float> SpriteData;
};
