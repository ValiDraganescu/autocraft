// `AAcDoodads`: the map's doodads and the border rocks (chunk B8,
// GAME-LAYER.md §2.6 "Doodads, border rocks"), the port of
// `GameScene.addDoodad`, `borderRocks` and `syncPlaced` (doodads put down in
// the playground).
//
// Each doodad is the exported `doodad_<kind>_<variant % 4>` (kinds whose
// shape follows the variant) or `doodad_<kind>`: at the field height, sunk
// 0.05, turned by its rotation (SceneKit eulerAngles.y, Unreal yaw -rotation),
// scaled, and mirrored along its own x when `mirrored`. The border rocks are
// rock, boulder and spire doodads where `AAcTerrain::BorderRocks()` puts
// them; they cast no shadow, as in Swift. Everything is a static
// hierarchical ISM per (mesh, material), built once per map: the map's
// doodads and the border rocks never change; the playground's are rebuilt
// when their list changes.
//
// Usage: `AAcDoodads::SpawnFor(World)` once per game world (AAcGameMode).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "AcScenery.h"
#include "Types.h"

#include <vector>

#include "AcDoodads.generated.h"

class UAcSimSubsystem;
struct FAcFrame;
struct FAcModelInfo;

UCLASS()
class AUTOCRAFT_API AAcDoodads : public AActor
{
	GENERATED_BODY()

public:
	AAcDoodads();

	static AAcDoodads* SpawnFor(UWorld* World);
	static AAcDoodads* Find(const UWorld* World);

	/// The exported model for a doodad (nullptr if the export lacks it).
	static const FAcModelInfo* ModelFor(ac::Doodad::Kind Kind, uint32 Variant);
	/// Where a doodad stands in Unreal (`GameScene.addDoodad`).
	static FTransform Placement(const ac::Doodad& D, double GroundHeight);

	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	void OnGameStarted(UAcSimSubsystem& Sim);
	void OnFrame(const FAcFrame& Frame);
	void Rebuild(UAcSimSubsystem& Sim);
	void PlaceAll(FAcSceneryBatches& Batches, const std::vector<ac::Doodad>& List);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	FAcSceneryBatches Map;
	FAcSceneryBatches Border;
	FAcSceneryBatches Placed;
	std::vector<ac::Doodad> PlacedList;
	FDelegateHandle GameStartedHandle, FrameHandle;
};
