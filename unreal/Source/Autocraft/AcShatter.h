// `AcShatter`: machines and buildings broken into the parts they are built
// from (chunk C5, GAME-LAYER.md §2.7), the port of `Shatter.swift` (the cut)
// and `Effects+Shatter.swift` (`launch`: a vehicle blown apart; `fell`: a
// building's collapse; `char`: the burnt metal), plus the flyers' fall of
// `Models+Dropship.swift:511` (`shootDown`).
//
// The cut, once per model (Swift cut the dying model on every death):
//   The export merged each part's rigid children into one mesh per
//   material, so the pieces they were built from (a box, a cylinder, a
//   crystal) are found again in the meshes: runs of triangles joined by
//   shared vertices, welded where two vertices sit on the same spot
//   (0.05 cm, Swift's 0.5 mm). Pieces are grouped into `Count` chunks by
//   Swift's rule, on the model's rest pose: the biggest piece (the hull) is
//   the first seed, then each time the piece farthest from every seed; each
//   piece joins its nearest seed. A chunk is drawn as one *sub* per (part,
//   mesh) it touches: that mesh's triangles of the chunk, a runtime static
//   mesh in the part's own frame (or the model's own mesh when the whole
//   mesh fell into one chunk). So at death every sub is placed at its part's
//   world transform of the last drawn pose (turrets turned, legs anchored),
//   and a chunk's motion is one rigid move applied to all its subs.
//   Additive and translucent meshes are left out (Swift's `read`), and so
//   are a kind's stripped parts (flashes, beams, the Firefly's flame and
//   pilot light, the flyers' ground shadow). Parts hidden at rest (tread
//   frames) join the nearest chunk but seed none.
//   The work: the source triangles are read on the game thread
//   (`UStaticMesh::GetMeshDescription`, editor data), cut on a worker, and
//   the new meshes built on the game thread a few per frame
//   (`FAcShatterLibrary::Tick`); `Finish` does the rest at once when a death
//   needs a model that is not ready.
//
// The flights (pure functions of the death, the seed and the time since):
//   `Launch` (Effects.launch), `Fell` (Effects.fell) fill an `FWreck` from
//   the chunks' world boxes at death; `ChunkDelta` is a chunk's rigid move
//   `Sec` seconds later (a world transform taking where it was at death to
//   where it is now), `Glow` the hot spots' intensity, `Opacity` the fade.
//   The maths stays in SceneKit space (cells, +Y up), as Swift's.
#pragma once

#include "CoreMinimal.h"
#include "Tasks/Task.h"
#include "UObject/StrongObjectPtr.h"

#include "SimdMath.h"

struct FAcModelInfo;
struct FAcPose;
class UStaticMesh;
class UMaterialInterface;
class UTexture2D;
struct FMeshDescription;

namespace AcShatter
{
	/// One drawn piece of a chunk: mesh `Mesh` of part `Part`'s triangles
	/// that fell in chunk `Chunk`.
	struct FSub
	{
		int32 Part = INDEX_NONE;
		int32 Mesh = INDEX_NONE;
		int32 Chunk = 0;
		/// The runtime mesh (or the model's own when not cut). Kept alive by
		/// the library.
		UStaticMesh* StaticMesh = nullptr;
		/// Its bounds in the part's frame, cm.
		FBox Box = FBox(ForceInit);
		/// A lamp (constant lighting, an emission texture or a bright
		/// emission colour): it dims to 0.12 instead of charring.
		bool bGlow = false;
		/// Big enough to cast a shadow (≥ 10 cm, Models+Flatten.swift:47).
		bool bShadow = true;
		int32 Triangles = 0;
	};

	/// A model cut into chunks.
	struct FModel
	{
		const FAcModelInfo* Model = nullptr;
		int32 Chunks = 0;
		/// The chunk with the model's largest piece (Swift `hull`).
		int32 HullChunk = 0;
		TArray<FSub> Subs;
		/// Parts never drawn on the wreck.
		TArray<bool> Stripped;
		int32 Pieces = 0;
		double CutMs = 0;
	};

	/// Hot spots on black (`Effects.emberImage`, Effects.swift:507): 128²,
	/// sRGB, repeating. Made once.
	AUTOCRAFT_API UTexture2D* EmberTexture();

	/// The SceneKit maths' little helpers, shared with the extension.
	AUTOCRAFT_API double Smoothstep(double A, double B, double X);
	/// Swift's `Effects.random(seed, k, i)`: 0..<1.
	AUTOCRAFT_API double Random(int64 Seed, int32 K, double I);

	/// A chunk's flight (Swift's `Flight`/`Piece`), SceneKit space, cells.
	struct FChunk
	{
		/// Centre and box size at death (UE: `Mid`, cm).
		FVector P0 = FVector::ZeroVector;
		FVector Extent = FVector::ZeroVector;
		FVector V = FVector::ZeroVector;
		FVector Axis = FVector(0, 1, 0);
		double Spin = 0;
		double Rest = 0, Land = 0, Hop = 0, HopTime = 0;
		bool bHull = false;
		/// Fell: stays standing (the shell), or breaks off at `Release`.
		bool bShell = false;
		double Release = 0;
		/// Burning: 0 no; 1 the hull (launch) or the shell (fell); 2 a loose
		/// part trailing fire.
		uint8 Fire = 0;
		/// Its fire's emitter: `damageFire(radius:)`, particle sizes, and
		/// where on the chunk (its own frame, SceneKit, cells).
		double FireRadius = 0, FireSize = 0.2, SmokeSize = 0.3;
		FVector FireAt = FVector::ZeroVector;
		bool bEmpty = false;
	};

	enum class EKind : uint8
	{
		Launch,
		Fell,
	};

	struct FBlast
	{
		ac::Vec2 At;
		double Delay = 0;
	};

	struct FWreck
	{
		EKind Kind = EKind::Launch;
		/// `size` (launch: 1 is a Firefly).
		double Size = 1;
		double Life = 12;
		TArray<FChunk> Chunks;
		/// Fell: the blasts, the building's height and radius, the ground.
		TArray<FBlast> Blasts;
		double Height = 0, Radius = 0, G0 = 0;
	};

	/// Each chunk's world box at death (cm): the subs' part-local boxes
	/// through their parts' world transforms. `Shown[sub]` false: left out.
	AUTOCRAFT_API void ChunkBoxes(const FModel& Cut, TConstArrayView<FTransform> PartWorld,
		TConstArrayView<bool> PartShown, TArray<FBox>& Out);

	/// `Effects.launch`: the chunks fly from a blast under their middle.
	/// `Ground(x, z)` is the terrain height (cells) under SceneKit (x, z).
	AUTOCRAFT_API FWreck Launch(const FModel& Cut, TConstArrayView<FBox> Boxes, double Size, int64 Seed,
		TFunctionRef<double(double, double)> Ground);

	/// `Effects.fell`: a building's collapse (`At` its centre, `G0` the
	/// ground there, cells).
	AUTOCRAFT_API FWreck Fell(const FModel& Cut, TConstArrayView<FBox> Boxes, ac::Vec2 At, double G0, double Height,
		double Radius, TArray<FBlast> Blasts, int64 Seed, TFunctionRef<double(double, double)> Ground);

	/// Chunk `C`'s move `Sec` seconds after death: world (cm) at death →
	/// world now. A sub's instance = its part's world at death × this.
	AUTOCRAFT_API FTransform ChunkDelta(const FWreck& W, int32 C, double Sec);
	/// Chunk `C`'s fire emitter's world frame (cm) `Sec` seconds after death.
	AUTOCRAFT_API FTransform FireFrame(const FWreck& W, int32 C, double Sec);
	/// The fire's and the smoke's births a second (Swift's tick).
	AUTOCRAFT_API void FireRates(const FWreck& W, int32 C, double Sec, double& Fire, double& Smoke);
	/// The hot spots' emission intensity (`hot` materials).
	AUTOCRAFT_API double Glow(const FWreck& W, double Sec);
	/// 1 → 0 over the last 1.5 s.
	AUTOCRAFT_API double Opacity(const FWreck& W, double Sec);

	/// `shootDown`: how long a flyer falls before it bursts on the ground.
	inline constexpr double FallTime = 1.15;
	/// The flyer's pose `Sec` (0…FallTime) after it was hit, from its last
	/// pose `Last`: spinning on its root, its `body` dropping to 0.35 over
	/// the ground, pitching and rolling. Writes `Out` (locals, placement).
	AUTOCRAFT_API void ShootDownPose(const FAcModelInfo& Model, const FAcPose& Last, int64 Seed, double Sec, FAcPose& Out);

	/// The Atlas's death before the shatter: it drops to its knees (0.9 s),
	/// then topples forward over its feet (a second), and bursts where it lies.
	inline constexpr double AtlasKneelTime = 0.9;
	inline constexpr double AtlasFallTime = 1.9;
	/// The Atlas's pose `Sec` (0…AtlasFallTime) after it was hit, from its
	/// last pose `Last`: the hips and knees fold, the body drops, then the
	/// whole machine turns forward about its toes. Writes `Out`.
	AUTOCRAFT_API void AtlasFallPose(const FAcModelInfo& Model, const FAcPose& Last, double Sec, FAcPose& Out);

	/// Part world transforms (cm) and visibility folded down the tree.
	AUTOCRAFT_API void PartWorlds(const FAcModelInfo& Model, const FAcPose& Pose, TArray<FTransform>& World,
		TArray<bool>& Shown);
}

/// The cut models of one world (owned by the C5 extension, or a sheet).
class AUTOCRAFT_API FAcShatterLibrary
{
public:
	FAcShatterLibrary();
	~FAcShatterLibrary();

	/// Start cutting `Model` into `Count` chunks, leaving out the parts
	/// named in `Strip` (and their children; a name ending in `_` is a
	/// prefix: `flashes_`). No-op if asked before.
	void Request(const FAcModelInfo& Model, int32 Count, TArray<FString> Strip);
	/// The cut model, or null while it is being made.
	const AcShatter::FModel* Find(const FAcModelInfo& Model) const;
	/// The cut model, finished now on this thread if it must be (null if
	/// never requested or it has nothing to draw).
	const AcShatter::FModel* Finish(const FAcModelInfo& Model);
	/// Game thread, once a frame: read sources, collect finished cuts, build
	/// up to `BudgetMs` of meshes.
	void Tick(double BudgetMs);
	bool IsIdle() const;

	/// How a sub is drawn (one material per source material): charred on
	/// `M_AcEmber` (custom data 3 = glow, ≥ 0 charred, < 0 intact), or a
	/// lamp on C4's fading masters (dithered by custom data 2: `bDither`),
	/// or another master as it is (it fades by its emission).
	struct FDraw
	{
		UMaterialInterface* Material = nullptr;
		bool bEmber = false;
		bool bDither = false;
	};
	FDraw MaterialFor(const AcShatter::FModel& Cut, const AcShatter::FSub& Sub, UObject* Outer);

private:
	struct FJob;
	void Read(FJob& J);
	void Build(FJob& J, double UntilSeconds);

	TArray<TUniquePtr<FJob>> Jobs;
	TMap<const FAcModelInfo*, int32> JobOf;
	TArray<TStrongObjectPtr<UObject>> Keep;
	TMap<UMaterialInterface*, FDraw> Materials;
};
