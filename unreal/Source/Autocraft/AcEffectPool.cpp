#include "AcEffectPool.h"
#include "AcInstancedMesh.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"

namespace
{
	constexpr int32 CustomFloats = 4;
	const FTransform Hidden(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
}

void FAcEffectPool::Init(AActor* Owner, UStaticMesh* Mesh, UMaterialInterface* Material, const int32 Count,
	const FName Name, const bool bCastShadow)
{
	check(Owner && Mesh && Count > 0);
	UInstancedStaticMeshComponent* C = NewObject<UAcInstancedMesh>(Owner, Name, RF_Transient);  // cheap bounds (P2)
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(Mesh);
	if (Material) C->SetMaterial(0, Material);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->bDisableCollision = true;
	C->SetCanEverAffectNavigation(false);
	C->SetGenerateOverlapEvents(false);
	C->SetCastShadow(bCastShadow);
	C->SetNumCustomDataFloats(CustomFloats);
	C->SetHasPerInstancePrevTransforms(true);
	C->SetForceDisableNanite(true);
	// The instances jump all over the map: bounds from the instances would
	// be recomputed every frame; never cull the pool as a whole instead.
	C->SetCullDistances(0, 0);
	C->bNeverDistanceCull = true;
	if (USceneComponent* Root = Owner->GetRootComponent()) C->SetupAttachment(Root);
	C->RegisterComponent();
	Component = C;
	MeshBox = Mesh->GetBoundingBox();

	Xf.Init(Hidden, Count);
	Shown.Init(Hidden, Count);
	Custom.SetNumZeroed(Count * CustomFloats);
	for (int32 I = 0; I < Count; ++I) Custom[I * CustomFloats + 1] = 1.f;
	C->AddInstances(Xf, false, false, false);
	C->SetCustomData(0, Count - 1, Custom, false);
	Cursor = 0;
}

int32 FAcEffectPool::Next()
{
	const int32 I = Cursor;
	Cursor = (Cursor + 1) % FMath::Max(1, Xf.Num());
	return I;
}

void FAcEffectPool::Set(const int32 I, const FTransform& Transform)
{
	if (!Xf.IsValidIndex(I)) return;
	Xf[I] = Transform;
	DirtyMin = FMath::Min(DirtyMin, I);
	DirtyMax = FMath::Max(DirtyMax, I);
}

void FAcEffectPool::Hide(const int32 I)
{
	if (!Xf.IsValidIndex(I) || Xf[I].GetScale3D().IsNearlyZero()) return;
	Set(I, Hidden);
}

void FAcEffectPool::SetCustom(const int32 I, const int32 Slot, const float Value)
{
	if (!Xf.IsValidIndex(I) || Slot < 0 || Slot >= CustomFloats) return;
	float& V = Custom[I * CustomFloats + Slot];
	if (V == Value) return;
	V = Value;
	CustomMin = FMath::Min(CustomMin, I);
	CustomMax = FMath::Max(CustomMax, I);
}

void FAcEffectPool::Upload()
{
	if (!Component) return;
	bool bAny = false;
	// This frame's changes plus last frame's (their previous transform
	// catches up, so nothing keeps a stale velocity).
	const int32 Min = FMath::Min(DirtyMin, LastMin);
	const int32 Max = FMath::Max(DirtyMax, LastMax);
	LastMin = DirtyMin;
	LastMax = DirtyMax;
	if (Max >= Min)
	{
		const int32 N = Max - Min + 1;
		TArray<FTransform> Now, Was;
		Now.SetNumUninitialized(N);
		Was.SetNumUninitialized(N);
		for (int32 K = 0; K < N; ++K)
		{
			const FTransform& A = Xf[Min + K];
			const FTransform& B = Shown[Min + K];
			Now[K] = A;
			// Appearing, vanishing or jumping to a new shot is a teleport.
			const bool bTeleport = A.GetScale3D().IsNearlyZero() || B.GetScale3D().IsNearlyZero()
				|| FVector::DistSquared(A.GetLocation(), B.GetLocation()) > FMath::Square(200.0);
			Was[K] = bTeleport ? A : B;
			Shown[Min + K] = A;
		}
		Component->BatchUpdateInstancesTransforms(Min, Now, Was, false, false, false);
		bAny = true;
	}
	if (CustomMax >= CustomMin)
	{
		Component->SetCustomData(CustomMin, CustomMax,
			TConstArrayView<float>(Custom.GetData() + CustomMin * CustomFloats, (CustomMax - CustomMin + 1) * CustomFloats), false);
		bAny = true;
	}
	if (bAny) Component->MarkRenderInstancesDirty();
	DirtyMin = CustomMin = MAX_int32;
	DirtyMax = CustomMax = -1;
}

FTransform FAcEffectPool::Shape(const FVector& Center, const FQuat& Rotation, const FVector& SizeCm) const
{
	const FVector Size = MeshBox.GetSize();
	const FVector Scale(Size.X > 0 ? SizeCm.X / Size.X : 0, Size.Y > 0 ? SizeCm.Y / Size.Y : 0,
		Size.Z > 0 ? SizeCm.Z / Size.Z : 0);
	// The mesh's own centre goes to `Center`.
	const FVector Offset = Rotation.RotateVector(Scale * MeshBox.GetCenter());
	return FTransform(Rotation, Center - Offset, Scale);
}

FQuat FAcEffectPool::Along(const FVector& Dir)
{
	return FQuat::FindBetweenNormals(FVector::UpVector, Dir.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector));
}
