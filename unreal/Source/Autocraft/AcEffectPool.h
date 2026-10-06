// `FAcEffectPool`: a fixed ring of instances of one mesh in one material,
// for short-lived effects (the Swift `Effects` pools of SCNNodes: tracers
// 48, flashes 32, mini gun rounds 64, slugs 16; C2's grenades, shells,
// decals). One `UInstancedStaticMeshComponent`; an instance is hidden by
// zero scale, never removed, so a firefight allocates nothing.
//
// Use: `Next()` takes the next slot (the oldest: a ring, as Swift's
// `nextTracer`), `Set(I, Xf)` shows it, `Hide(I)`. `UAcEffects` uploads
// every pool once a frame (dirty range, previous transforms for TSR, as
// `UAcWorldRenderer::Upload`). Transforms are UE world, cm.
//
// `Shape` builds the transform that fits the mesh's bounds to a box of a
// given size (cm) centred on a point and turned by a rotation, so pools can
// use engine meshes whatever their pivot and size: the mesh's local Z is
// the "length" axis (SceneKit cylinders run along their Y; the effects
// point that axis along the shot).
#pragma once

#include "CoreMinimal.h"

class AActor;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

class AUTOCRAFT_API FAcEffectPool
{
public:
	/// Make the component under `Owner` with `Count` hidden instances.
	/// Custom data: 4 floats {team 0, emission 1, fade 0, char 0}.
	void Init(AActor* Owner, UStaticMesh* Mesh, UMaterialInterface* Material, int32 Count, FName Name,
		bool bCastShadow = false);

	int32 Num() const { return Xf.Num(); }
	/// The next slot of the ring (whatever it shows now is taken over).
	int32 Next();
	/// The slot `Next` will take (Swift's `nextFlash`, read for jitter).
	int32 Peek() const { return Cursor; }
	void Set(int32 I, const FTransform& Transform);
	void Hide(int32 I);
	bool IsShown(int32 I) const { return Xf.IsValidIndex(I) && !Xf[I].GetScale3D().IsNearlyZero(); }
	/// Custom data `Slot` (0..3) of instance `I` (e.g. 1: emission/fade).
	void SetCustom(int32 I, int32 Slot, float Value);
	/// Send this frame's changes to the component.
	void Upload();

	/// The transform that puts the mesh's bounds onto a box `SizeCm`
	/// (x, y across, z along) centred at `Center`, turned by `Rotation`.
	FTransform Shape(const FVector& Center, const FQuat& Rotation, const FVector& SizeCm) const;
	/// A rotation taking the mesh's +Z onto `Dir` (a unit vector).
	static FQuat Along(const FVector& Dir);

	UInstancedStaticMeshComponent* Component = nullptr;

private:
	TArray<FTransform> Xf;
	TArray<FTransform> Shown;
	TArray<float> Custom;
	FBox MeshBox = FBox(FVector(-50), FVector(50));
	int32 Cursor = 0;
	int32 DirtyMin = MAX_int32, DirtyMax = -1;
	int32 LastMin = MAX_int32, LastMax = -1;
	int32 CustomMin = MAX_int32, CustomMax = -1;
};
