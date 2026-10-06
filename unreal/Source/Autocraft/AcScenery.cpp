#include "AcScenery.h"

#include "AcLog.h"
#include "AcModelCatalog.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"

void FAcSceneryBatches::Init(AActor* InOwner, USceneComponent* InAttach, const FAcSceneryOptions& InOptions, const TCHAR* InTag)
{
	Owner = InOwner;
	Attach = InAttach;
	Options = InOptions;
	Tag = InTag;
}

int32 FAcSceneryBatches::BatchFor(const FString& MeshPath, const FString& MaterialPath, const FAcModelMesh& Mesh, const bool bMirrored)
{
	const FString Key = MeshPath + TEXT("|") + MaterialPath + (bMirrored ? TEXT("|mirrored") : TEXT(""));
	if (const int32* Found = BatchIndex.Find(Key)) return *Found;
	AActor* Actor = Owner.Get();
	UStaticMesh* StaticMesh = Mesh.LoadMesh();
	if (!Actor || !StaticMesh)
	{
		UE_LOG(LogAutocraft, Warning, TEXT("scenery: no mesh %s"), *MeshPath);
		BatchIndex.Add(Key, INDEX_NONE);
		return INDEX_NONE;
	}
	const FName Name = MakeUniqueObjectName(Actor, UInstancedStaticMeshComponent::StaticClass(),
		FName(*FString::Printf(TEXT("%s_%s"), *Tag, *StaticMesh->GetName())));
	UInstancedStaticMeshComponent* C = Options.bHierarchical
		? NewObject<UHierarchicalInstancedStaticMeshComponent>(Actor, Name)
		: NewObject<UInstancedStaticMeshComponent>(Actor, Name);
	C->SetMobility(Options.Mobility);
	// An instance with a negative scale (a mirrored doodad) has a negative
	// determinant, which the engine only reads off the component's own
	// transform: the faces would cull the wrong way round (rocks drawn as
	// hollow shells). Mirrored instances get their own component, culled
	// the other way.
	C->bReverseCulling = bMirrored;
	C->SetStaticMesh(StaticMesh);
	if (UMaterialInterface* Material = Mesh.LoadMaterial()) C->SetMaterial(0, Material);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCanEverAffectNavigation(false);
	C->SetCastShadow(Options.bCastShadow);
	// Per-instance custom data as the model materials read it: team 0 (scenery
	// has none), emission scale 1. Left out, the emission reads 0.
	C->SetNumCustomDataFloats(2);
	if (Options.CullDistance > 0) C->SetCullDistances(0, Options.CullDistance);
	if (USceneComponent* Parent = Attach.Get()) C->SetupAttachment(Parent);
	C->RegisterComponent();
	Actor->AddInstanceComponent(C);
	const int32 Index = Batches.Num();
	FBatch& B = Batches.AddDefaulted_GetRef();
	B.Component = C;
	BatchIndex.Add(Key, Index);
	return Index;
}

FAcSceneryModel FAcSceneryBatches::AddModel(const FAcModelInfo& Model, const FTransform& Placement)
{
	FAcSceneryModel Out;
	Out.Parts.SetNum(Model.Parts.Num());
	for (int32 I = 0; I < Model.Parts.Num(); ++I)
	{
		const FAcModelPart& P = Model.Parts[I];
		if (P.bHidden || P.Meshes.IsEmpty()) continue;
		const FTransform T = Model.RestToModel(I) * Placement;
		const bool bMirrored = T.GetDeterminant() < 0.0;
		for (const FAcModelMesh& M : P.Meshes)
		{
			const int32 B = BatchFor(M.Mesh.ToString(), M.MaterialInstance.ToString(), M, bMirrored);
			if (B == INDEX_NONE) continue;
			FAcSceneryInstance& X = Out.Parts[I].AddDefaulted_GetRef();
			X.Batch = B;
			// Provisional: the slot in the batch's queue; Flush turns it
			// into the component's instance index.
			X.Index = Batches[B].Pending.Add(T);
			X.Transform = T;
		}
	}
	return Out;
}

void FAcSceneryBatches::Flush(TArrayView<FAcSceneryModel*> Models)
{
	for (FBatch& B : Batches)
	{
		B.FirstPending = B.Component ? B.Component->GetInstanceCount() : 0;
	}
	for (FAcSceneryModel* Model : Models)
	{
		if (!Model) continue;
		for (TArray<FAcSceneryInstance>& Part : Model->Parts)
		{
			for (FAcSceneryInstance& X : Part) X.Index += Batches[X.Batch].FirstPending;
		}
	}
	for (FBatch& B : Batches)
	{
		if (B.Pending.IsEmpty() || !B.Component) continue;
		const int32 First = B.Component->GetInstanceCount();
		B.Component->AddInstances(B.Pending, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ true);
		static const float Data[2] = {0.f, 1.f};
		for (int32 I = First; I < B.Component->GetInstanceCount(); ++I)
		{
			B.Component->SetCustomData(I, TArrayView<const float>(Data, 2), false);
		}
		B.Component->MarkRenderStateDirty();
		B.Pending.Reset();
	}
}

void FAcSceneryBatches::SetVisible(const FAcSceneryModel& Model, const int32 Part, const bool bVisible)
{
	if (!Model.Parts.IsValidIndex(Part)) return;
	for (const FAcSceneryInstance& X : Model.Parts[Part])
	{
		FBatch& B = Batches[X.Batch];
		if (!B.Component || !B.Component->IsValidInstance(X.Index)) continue;
		FTransform T = X.Transform;
		if (!bVisible) T.SetScale3D(FVector::ZeroVector);
		B.Component->UpdateInstanceTransform(X.Index, T, /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ false, /*bTeleport*/ true);
		B.bDirty = true;
	}
}

void FAcSceneryBatches::Commit()
{
	for (FBatch& B : Batches)
	{
		if (!B.bDirty) continue;
		B.bDirty = false;
		if (B.Component) B.Component->MarkRenderStateDirty();
	}
}

void FAcSceneryBatches::Clear()
{
	for (FBatch& B : Batches)
	{
		if (B.Component) B.Component->DestroyComponent();
	}
	Batches.Reset();
	BatchIndex.Reset();
}

int32 FAcSceneryBatches::NumInstances() const
{
	int32 N = 0;
	for (const FBatch& B : Batches)
	{
		if (B.Component) N += B.Component->GetInstanceCount();
	}
	return N;
}
