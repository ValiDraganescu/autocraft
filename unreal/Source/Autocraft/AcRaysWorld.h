// `UAcRaysSubsystem`: the rays against the game's own shapes (chunk E1,
// GAME-LAYER.md §2.9): the port of `GameScene.sightHit`, `blocked` and
// `cast` (Sources/Autocraft/GameScene.swift :184/:202,
// GameScene+Rays.swift). Plain math (AcRays.h), no Unreal collision: the
// ground is the terrain mesh's own height grid (`AAcTerrain::Heights()`),
// and every unit, building, ore deposit, well and doodad is the pieces of
// its parts (`rays` in the catalog: each SceneKit geometry node's bounds, or
// its true sphere, cylinder or capsule) where the renderer posed them this
// frame (`UAcWorldRenderer::ForEachObject`). Per-part precision is the rule:
// a shot between a Ranger's legs misses; hidden parts (a Ranger's minigun
// before it is researched, mined-out ore nodules) are not met.
//
// API (Unreal world space, cm; game thread; cast after the renderer's
// `Renderer` frame stage to see this frame's poses, before it for last
// frame's):
//
//   UAcRaysSubsystem* Rays = UAcRaysSubsystem::Get(this);
//   // The crosshair, D4's precise picks: units count, `Skip` passes one id.
//   if (TOptional<FAcRayHit> H = Rays->SightHit(Eye, Eye + Dir * AcSpace::ToCm(60), DrivenId))
//       H->Point; H->T; H->Id;   // Id unset: the ground, ore, a well, a doodad
//   // The chase camera and sound occlusion: ground, buildings, scenery; units don't count.
//   if (TOptional<float> T = Rays->Blocked(Pivot, Camera)) Boom *= *T;
//   // Either, with the choice spelled out:
//   Rays->Cast(A, B, /*bUnits*/ true, /*Skip*/ {});
//
// The segment is A → B; T is in 0…1 along it (the Swift `t`); nothing past
// B is met. What the rays see follows the renderer: what the local player
// sees (`Shown()`), and a unit aboard, in a Bastion or a Derrick, or driven
// in first person (`SetHiddenUnit`) is not there.
//
// Cost: one bounding-sphere test per object, the pieces only of those the
// segment passes near; the ground is marched a quarter grid square (1/16
// cell) at a time. A 60-cell crosshair ray over a late game: well under a
// millisecond.
//
// Console: `ac.Ray X Y Z X Y Z [units 0|1]` (UE cm) logs what a segment
// meets; `ac.RayProbe [N]` casts down onto N units and N buildings and 60
// cells across from each, logging hits and times. `-AcRayProbe=SECONDS`
// runs the probe once the game has run that long (headless checks).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "AcRays.h"

#include <memory>
#include <vector>

#include "AcRaysWorld.generated.h"

struct FAcModelInfo;
class UAcSimSubsystem;

/// What a ray met.
struct FAcRayHit
{
	/// Where, Unreal world space (cm).
	FVector Point = FVector::ZeroVector;
	/// How far along the segment, 0…1.
	float T = 0.f;
	/// The unit or building met; unset: the ground or the scenery.
	TOptional<int64> Id;
};

/// Conversions between Unreal space and the SceneKit space AcRays works in
/// (cells, Y up; AcSpace.h).
namespace AcRaySpace
{
	AUTOCRAFT_API FAcF3 Point(const FVector& UnrealCm);
	AUTOCRAFT_API FVector ToUnreal(FAcF3 SceneKit);
	/// An Unreal transform (a part's world, cm) as the SceneKit matrix of the
	/// same placement: swap Y and Z on both sides, translation in cells.
	AUTOCRAFT_API FAcMat4 Matrix(const FTransform& UnrealCm);
}

/// Every catalog model's ray shape, built on first use.
class AUTOCRAFT_API FAcRayShapes
{
public:
	static const FAcRayShapes& Get();
	/// Null if the model has no pieces (or the catalog has no `rays`: rerun
	/// `export-models` and `import_models.py`).
	const FAcRayShape* Find(const FAcModelInfo* Model) const;
	/// A model's shape from its catalog parts (`rays`, rest transforms).
	static FAcRayShape Build(const FAcModelInfo& Model);
	/// One `rays` entry (the manifest's `RayOut`) → a piece of `Part`.
	static bool ParsePiece(const FJsonValue& Value, int32 Part, FAcRayShape::FPiece& Out);

private:
	TMap<const FAcModelInfo*, TUniquePtr<FAcRayShape>> Shapes;
};

UCLASS()
class AUTOCRAFT_API UAcRaysSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UAcRaysSubsystem* Get(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Deinitialize() override;

	/// `GameScene.sightHit`: what a shooter's ray from A to B meets first, on
	/// the models' parts; `Skip` lets one unit through (the driven one).
	TOptional<FAcRayHit> SightHit(const FVector& A, const FVector& B, TOptional<int64> Skip = {});
	/// `GameScene.blocked`: how far along A → B (0…1) the ground, a building,
	/// an ore deposit, a well or a doodad first stands in the way. Units
	/// don't count.
	TOptional<float> Blocked(const FVector& A, const FVector& B);
	/// `GameScene.cast`: what A → B meets first; units only when `bUnits`.
	TOptional<FAcRayHit> Cast(const FVector& A, const FVector& B, bool bUnits, TOptional<int64> Skip = {});

	/// Ore, wells and doodads as the rays see them (rebuilt when they change).
	int32 NumScenery() const { return Scenery.Num(); }

private:
	struct FScenery
	{
		const FAcRayShape* Shape = nullptr;
		/// Patch id for ore (its stage hides nodules), else unset.
		TOptional<int64> Patch;
		/// Index of the `stage1..3` parts (ore), INDEX_NONE if absent.
		int32 StagePart[3] = {INDEX_NONE, INDEX_NONE, INDEX_NONE};
		const FAcModelInfo* Model = nullptr;
		std::vector<FAcMat4> World;
		std::vector<uint8_t> Visible;
	};

	void SyncScenery(UAcSimSubsystem& Sim);
	void AddScenery(const FAcModelInfo* Model, const FTransform& Placement, TOptional<int64> Patch = {});

	TArray<FScenery> Scenery;
	/// `-AcRayProbe=SECONDS`.
	FDelegateHandle ProbeHandle;
	double ProbeAt = -1;
	/// What the scenery was built from.
	const void* SceneryMap = nullptr;
	size_t SceneryPatches = 0, SceneryWells = 0, SceneryDoodads = 0;
	uint32 SceneryDoodadHash = 0;

	/// Scratch, reused per cast.
	std::vector<FAcMat4> Mats;
	std::vector<uint8_t> Shown;
	std::vector<AcRays::FTarget> Targets;
	std::vector<std::pair<size_t, size_t>> Ranges;
};
