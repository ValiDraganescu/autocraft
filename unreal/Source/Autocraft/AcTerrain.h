// `AAcTerrain`: the ground (GAME-LAYER.md §2.6 "Terrain", chunk A2), the
// port of `TerrainBuilder` (Sources/Autocraft/Terrain.swift).
//
// - The map's ground: a height field from `ac::TerrainField` at 4 vertices
//   per cell, central-difference normals, each square split along the
//   diagonal from (i+1, j) to (i, j+1), as in Swift. `Heights()` is that
//   same grid (`FAcHeightGrid`, engine-free), so picking and the pilot's
//   rays meet exactly the triangles that are drawn.
// - The border: the scenery past the map's edge, out of play, 60+ cells
//   wide, finer near the edge, a touch (0.08) lower under the map's own
//   mesh, casting no shadow (`TerrainBuilder.border`).
// - The splat map: RGBA at 4 texels per cell, grass, highland, plating,
//   scorch (`TerrainField.materials`); dirt fills the rest. `M_Terrain`
//   (made by Tools/Editor/make_terrain_material.py) blends the ground
//   textures by it, puts rock on cliffs by slope and fades the border into
//   the haze.
// - Border rocks: where `GameScene.borderRocks` strews them
//   (`BorderRocks()`); the doodad chunk (B8) draws them with its doodad
//   meshes.
//
// Mesh path: the ground is a set of `UStaticMesh`es built at runtime from
// `FMeshDescription`s by the fast build (`BuildFromMeshDescriptions`,
// bFastBuild), 32 × 32-cell tiles. The map never changes during a game, so
// a static mesh is the right kind: the static draw path (no per-frame
// mesh work), per-tile culling, cached virtual shadow map pages, and a ray
// tracing representation for hardware Lumen. The fast build is the one path
// that works in a packaged game too; ProceduralMeshComponent and
// DynamicMeshComponent redraw dynamically and are meant for meshes that
// change. Nanite and mesh distance fields need the editor's builders, so the
// ground has neither (it is low-poly enough not to need Nanite; software
// Lumen sees it through screen traces).
//
// Usage: `AAcTerrain::SpawnFor(World)` once per game world (AAcGameMode):
// it builds from `UAcSimSubsystem::Map()` and rebuilds when a game on
// another map starts. Or `Build(Map)` directly.
//
// `-AcTerrainView[=POINTS_PER_CELL]` (with `-AcShot`): frames the Swift
// game's default top-down view (over blue's main base, 40 points per cell,
// pitch 56°, vertical FOV 30°, `ac::FreeView`) and, when the level has no
// sun yet, puts up a stand-in noon sun and sky. For terrain renders until
// the RTS camera (A3) and daylight (B9) exist. `-AcTerrainCover=PTS` is the
// console's cover (about 233 at 1600 × 1000), `-AcTerrainAt=X,Y` centres
// the view on a ground point (as `windowshot --at`).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "AcHeightGrid.h"
#include "TerrainField.h"
#include "Types.h"

#include <memory>
#include <optional>
#include <vector>

#include "AcTerrain.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture2D;
class UAcSimSubsystem;

/// A rock strewn over the border scenery (`GameScene.borderRocks`).
struct FAcBorderRock
{
	ac::Doodad::Kind Kind = ac::Doodad::Kind::rock;
	/// The doodad's shape seed (`Doodad.variant`); the export has `variant % 4`.
	uint32 Variant = 0;
	/// Unreal world transform: on `borderHeight`, sunk 0.15 · scale, turned
	/// and scaled as in Swift.
	FTransform Transform;
};

UCLASS()
class AUTOCRAFT_API AAcTerrain : public AActor
{
	GENERATED_BODY()

public:
	/// Mesh vertices per cell (`TerrainBuilder.density`).
	static constexpr int32 Density = FAcHeightGrid::density;
	/// Cells per side of a mesh tile.
	static constexpr int32 TileCells = 32;
	/// Splat texels per cell.
	static constexpr int32 SplatPerCell = 4;
	/// The haze the border fades into (`TerrainBuilder.haze`), sRGB-ish as
	/// Swift writes it.
	static FLinearColor Haze() { return FLinearColor(0.27f, 0.22f, 0.19f, 1.0f); }
	/// How far the border reaches past the map, cells (`borderWidth`).
	static double BorderWidth(const ac::GroundRect& B);

	AAcTerrain();

	/// The terrain of the world (spawned on first ask, built from the sim's
	/// map and rebuilt when a game on another map starts).
	static AAcTerrain* SpawnFor(UWorld* World);
	static AAcTerrain* Find(const UWorld* World);

	/// Build everything for `Map` (a no-op when it is already built for it).
	void Build(const ac::MapDefinition& Map);
	bool IsBuilt() const { return Field != nullptr; }

	/// The field (heights, materials, border). Valid once built.
	const ac::TerrainField& GetField() const { return *Field; }
	/// The drawn mesh's height grid, shared with the rays (E1) and picking.
	const FAcHeightGrid& Heights() const { return Grid; }
	/// The drawn ground's height (cells) under a sim ground point: the mesh
	/// triangle on the map, `borderHeight` past it.
	double HeightAt(ac::Vec2 P) const { return Grid.height(P); }
	/// The same for an Unreal location: the ground's Z (cm) under it.
	double GroundZ(const FVector& World) const;
	/// The field's own smooth height (cells), as the sim stands units on it
	/// (`GameScene.groundY`): use this to place things, `HeightAt` to hit
	/// what is drawn.
	double FieldHeight(ac::Vec2 P) const { return Field ? Field->height(P) : 0.0; }

	/// Where the border rocks go (`BorderRocks`); drawn by B8.
	const TArray<FAcBorderRock>& BorderRocks() const { return Rocks; }

	/// The ground material (a `M_Terrain` instance): B9 sets `Haze`, the
	/// fog chunk may read `Bounds`.
	UMaterialInstanceDynamic* GetMaterial() const { return Material; }
	UTexture2D* GetSplat() const { return Splat; }

	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	void OnGameStarted(UAcSimSubsystem& Sim);
	void BuildGround();
	void BuildBorder();
	void BuildSplat();
	void PlaceBorderRocks();
	UStaticMeshComponent* AddMeshComponent(UStaticMesh* Mesh, const TCHAR* Name, bool bCastShadow);
	void Clear();
	/// -AcTerrainView: frame the Swift default view (see the file comment).
	void StageViewIfAsked();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Pieces;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> Splat;

	/// The material the ground draws with (made by make_terrain_material.py).
	UPROPERTY(EditAnywhere, Category = "Terrain")
	TSoftObjectPtr<UMaterialInterface> TerrainMaterial;

	std::optional<ac::MapDefinition> Map;
	std::unique_ptr<ac::TerrainField> Field;
	FAcHeightGrid Grid;
	TArray<FAcBorderRock> Rocks;
	FDelegateHandle GameStartedHandle;
	bool bViewStaged = false;
};
