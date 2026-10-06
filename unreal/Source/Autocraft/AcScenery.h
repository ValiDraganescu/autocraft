// `FAcSceneryBatches`: exported models drawn as instances, one instanced
// static mesh component per (mesh, material), for the things that stand
// still: ore deposits, MH wells, doodads and border rocks (chunk B8,
// GAME-LAYER.md §2.6). Used by `AAcResources` and `AAcDoodads`.
//
// A model's part meshes are in the part's own frame (FAcModelCatalog), so an
// instance's transform is `RestToModel(part) × placement`. Instances are
// queued with `Add…` and created in one go by `Flush` (one AddInstances per
// component, which is what a hierarchical ISM wants). Nothing is ever
// removed one by one: a part is hidden by scaling its instances to zero
// (`SetVisible`), and a whole set is rebuilt with `Clear`.
#pragma once

#include "CoreMinimal.h"
#include "Components/InstancedStaticMeshComponent.h"

struct FAcModelInfo;
class UHierarchicalInstancedStaticMeshComponent;

/// One instance of one (part, material) mesh.
struct FAcSceneryInstance
{
	int32 Batch = INDEX_NONE;
	int32 Index = INDEX_NONE;
	/// Its transform when shown.
	FTransform Transform;
};

/// The instances of one placed model, by part (index into the model's parts).
struct FAcSceneryModel
{
	TArray<TArray<FAcSceneryInstance>> Parts;
	bool IsEmpty() const { return Parts.IsEmpty(); }
};

struct FAcSceneryOptions
{
	/// Hierarchical ISMs (static, many, culled per cluster) or plain ones
	/// (instances updated now and then).
	bool bHierarchical = true;
	EComponentMobility::Type Mobility = EComponentMobility::Static;
	bool bCastShadow = true;
	/// Kept apart from other components when instances are culled by
	/// distance (cm, 0: never).
	int32 CullDistance = 0;
};

class AUTOCRAFT_API FAcSceneryBatches
{
public:
	void Init(AActor* InOwner, USceneComponent* InAttach, const FAcSceneryOptions& InOptions, const TCHAR* InTag);

	/// Queue every mesh of the model at `Placement` (Unreal world), leaving
	/// out parts that start hidden. Indices are known once flushed.
	FAcSceneryModel AddModel(const FAcModelInfo& Model, const FTransform& Placement);
	/// Create the queued instances. Fills `Index` in the models handed out
	/// since the last flush (they must still be alive: pass them back).
	void Flush(TArrayView<FAcSceneryModel*> Models);
	/// Show or hide one part of a placed (and flushed) model.
	void SetVisible(const FAcSceneryModel& Model, int32 Part, bool bVisible);
	/// Push `SetVisible` changes to the renderer (once a frame at most).
	void Commit();

	void Clear();
	int32 NumComponents() const { return Batches.Num(); }
	int32 NumInstances() const;

private:
	struct FBatch
	{
		TObjectPtr<UInstancedStaticMeshComponent> Component;
		TArray<FTransform> Pending;
		int32 FirstPending = 0;
		bool bDirty = false;
	};
	int32 BatchFor(const FString& MeshPath, const FString& MaterialPath, const struct FAcModelMesh& Mesh, bool bMirrored);

	TWeakObjectPtr<AActor> Owner;
	TWeakObjectPtr<USceneComponent> Attach;
	FAcSceneryOptions Options;
	FString Tag;
	TArray<FBatch> Batches;
	TMap<FString, int32> BatchIndex;
};
