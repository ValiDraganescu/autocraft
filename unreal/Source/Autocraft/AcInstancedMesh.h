// `UAcInstancedMesh` (chunk P2): an instanced static mesh with cheap bounds.
// A plain ISM works out its bounds from every instance each time its
// instances move: a box turned by each instance's matrix, twice a frame
// (`UpdateBounds`, then `GetLocalBounds` when the scene takes the update).
// With 1,500 units that was ~4 ms of the game thread a frame in the world
// renderer alone, and as much again in the life bars, the ore and the
// effects. Here:
// - `SetBox`: the owner says where the instances are (the world renderer,
//   which walks the transforms it sends anyway);
// - otherwise the box of the instances' origins, grown by the mesh's reach
//   at the largest scale (a min/max per instance, no box turned).
// Use it wherever an ISM's instances move every frame.
#pragma once

#include "CoreMinimal.h"
#include "Components/InstancedStaticMeshComponent.h"

#include "AcInstancedMesh.generated.h"

UCLASS()
class AUTOCRAFT_API UAcInstancedMesh : public UInstancedStaticMeshComponent
{
	GENERATED_BODY()

public:
	/// The box (world cm) every instance lies in from now on; invalid: work
	/// it out. Read the next time the bounds are updated (the engine does
	/// when the instance data is sent).
	void SetBox(const FBox& WorldBox);

	virtual FBoxSphereBounds CalcBounds(const FTransform& BoundTransform) const override;

private:
	/// The box told, in the component's space.
	FBox Box{ForceInit};
	bool bTold = false;
};
