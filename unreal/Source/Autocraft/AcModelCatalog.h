// `FAcModelCatalog`: every model exported from the Swift game, as Unreal
// assets (GAME-LAYER.md §3.2, chunk A4). It reads
// `Content/Models/ModelCatalog.json`, which `Tools/Editor/import_models.py`
// writes from the export's `manifest.json` plus where each mesh and material
// instance went.
//
// A model is a tree of rigid parts. Each part has a rest transform relative
// to its parent part (the SceneKit node's, converted with AcSpace) and holds
// one static mesh per material, already in the part's own frame and in
// Unreal space (Y-up → Z-up and cells → cm are done at import). So a part's
// world transform is `PartLocal × Parent × … × Root × Placement`, and a pose
// function only replaces the local transforms of the parts it moves.
//
// The blue and red exports share their meshes and material instances: the
// team colour is per-instance custom data (slot 0, `MPC_AcTeams`), so the
// red model's entries point at the blue assets (`FAcModelMesh::Team` says
// which team channels the material reads).
//
// Custom data slots every model material reads (ISM per-instance custom
// data, or custom primitive data on a plain component):
//   0 team index (0..7), 1 emission scale (1 = as exported),
//   2 reserved: fade, 3 reserved: char.
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UObject/SoftObjectPath.h"

class UStaticMesh;
class UMaterialInterface;

namespace AcModelData
{
	inline constexpr int32 TeamIndex = 0;
	inline constexpr int32 EmissionScale = 1;
	inline constexpr int32 Fade = 2;
	inline constexpr int32 Char = 3;
	inline constexpr int32 Count = 4;
}

/// What a material takes from the team (bit flags).
enum class EAcTeamChannel : uint8
{
	None = 0,
	/// The tint is the team colour (the `team` hull trim).
	Tint = 1 << 0,
	/// The base texture's blue paint is recoloured (SkinTextures.painted).
	PaintBase = 1 << 1,
	/// The emission texture's blue paint is recoloured.
	PaintEmissive = 1 << 2,
	/// The emission colour is the team's lamp colour.
	Glow = 1 << 3,
};
ENUM_CLASS_FLAGS(EAcTeamChannel);

/// One (part, material) static mesh.
struct FAcModelMesh
{
	/// The exported material name (this model's, so `ranger_red_basecolor`
	/// on the red Ranger).
	FName Material;
	/// The USD prim (`<part>__<material>`).
	FName Prim;
	FSoftObjectPath Mesh;
	FSoftObjectPath MaterialInstance;
	/// `opaque`, `additive` or `translucent`.
	FName Blend;
	EAcTeamChannel Team = EAcTeamChannel::None;
	int32 Triangles = 0;

	UStaticMesh* LoadMesh() const;
	UMaterialInterface* LoadMaterial() const;
};

/// One rigid part.
struct FAcModelPart
{
	FName Name;
	/// The USD prim path (`/ranger_blue/body/head`).
	FString Path;
	/// Index of the parent part in `FAcModelInfo::Parts`; INDEX_NONE for the root.
	int32 Parent = INDEX_NONE;
	/// Swift struct paths that reach this part (`legs[0]`, `fork.tines[1]`).
	TArray<FString> Handles;
	FString SceneKitName;
	/// Rest transform relative to the parent part, in Unreal space (cm).
	FTransform Rest;
	/// The SceneKit rest values pose code overwrites (cells, radians:
	/// x pitch, y yaw, z roll, applied roll, yaw, pitch).
	FVector SkPosition = FVector::ZeroVector;
	FVector SkEuler = FVector::ZeroVector;
	FVector SkScale = FVector::OneVector;
	/// Starts hidden (muzzle flashes, spare tread frames...).
	bool bHidden = false;
	/// The node's opacity (smears, blur sleeves); 1 if unset.
	float Opacity = 1.f;
	TArray<FAcModelMesh> Meshes;
	/// What moves it (`what`, `channels`, `functions`), a light socket, a
	/// particle socket: straight from the manifest, or null.
	TSharedPtr<FJsonObject> Driver;
	TSharedPtr<FJsonObject> Light;
	TArray<TSharedPtr<FJsonValue>> Particles;
	/// The solid pieces the rays test (`rays` in the manifest: a `RayBox`
	/// per SceneKit geometry node, in this part's frame); read by AcRaysWorld.
	TArray<TSharedPtr<FJsonValue>> Rays;
};

/// One exported model (`ranger_blue`, `citadel_red`, `ore_0`, `doodad_rock_2`).
struct FAcModelInfo
{
	FName Name;
	/// Without the team suffix (`ranger`).
	FName Base;
	/// `blue`, `red` or empty.
	FString Team;
	/// unit, building, construction, cockpit, resource, doodad, effect.
	FString Category;
	FString Facing;
	/// The rest pose with the hidden parts left out, Unreal space (cm),
	/// in the model's root frame before the root's own transform.
	FBox Bounds = FBox(ForceInit);
	int32 Triangles = 0;
	TArray<FString> Notes;
	TArray<FAcModelPart> Parts;
	TMap<FName, int32> PartIndex;

	const FAcModelPart* FindPart(FName Part) const;
	/// The part's rest transform relative to the model root's parent (the
	/// placement): its chain of rest transforms, the root's included.
	FTransform RestToModel(int32 PartIndex) const;
	/// The team index the export was made for (blue 0, red 1, else 0).
	int32 ExportTeam() const;
};

class AUTOCRAFT_API FAcModelCatalog
{
public:
	/// The catalog loaded from `Content/Models/ModelCatalog.json` (on first use).
	static const FAcModelCatalog& Get();
	static FString DefaultPath();

	/// Load (or reload) from a file. False, with the reason logged, on failure.
	bool Load(const FString& File);
	bool IsLoaded() const { return Models.Num() > 0; }

	const FAcModelInfo* Find(FName Model) const;
	const FAcModelPart* FindPart(FName Model, FName Part) const;
	const FAcModelMesh* FindMesh(FName Model, FName Part, FName Material) const;
	const TArray<FAcModelInfo>& All() const { return Models; }

	/// A SceneKit node transform as the manifest writes it (16 floats,
	/// SCNMatrix4 order: rows are the X, Y, Z axes and the translation, in
	/// cells) → an Unreal transform (cm). See AcSpace.h.
	static FTransform TransformFromSceneKitMatrix(const TArray<double>& M);

private:
	TArray<FAcModelInfo> Models;
	TMap<FName, int32> Index;
};
