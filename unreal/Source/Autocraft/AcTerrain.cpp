// Port of `TerrainBuilder` (Sources/Autocraft/Terrain.swift) and
// `GameScene.borderRocks` (GameScene.swift). See AcTerrain.h.
#include "AcTerrain.h"

#include "AcLog.h"
#include "AcShot.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"

#include "FreeView.h"
#include "Noise.h"
#include "WindowMaps.h"

#include "Async/ParallelFor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ContentStreaming.h"
#include "Containers/Ticker.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Misc/CommandLine.h"
#include "UObject/UObjectIterator.h"
#include "Misc/Parse.h"
#include "ShaderCompiler.h"
#include "StaticMeshAttributes.h"

#include <cmath>

namespace
{
	/// A height field over grid lines `Xs` × `Zs` (cells), as the Swift code
	/// holds it: heights row by row (j outer), with normals from central
	/// differences (one-sided at the ends).
	struct FGridLines
	{
		std::vector<double> Xs, Zs;
		std::vector<float> H;
		int64 Nx = 0, Nz = 0;

		float At(int64 I, int64 J) const
		{
			I = FMath::Clamp<int64>(I, 0, Nx - 1);
			J = FMath::Clamp<int64>(J, 0, Nz - 1);
			return H[size_t(J * Nx + I)];
		}

		/// Swift's normal `normalize(-dx, 1, -dz)` in SceneKit axes is
		/// `(-dh/dx, -dh/dy, 1)` in Unreal's (the height scale is the same
		/// in both: cells and cm alike).
		FVector3f Normal(int64 I, int64 J, bool bCentral) const
		{
			const int64 I0 = bCentral ? I - 1 : FMath::Max<int64>(I - 1, 0), I1 = bCentral ? I + 1 : FMath::Min<int64>(I + 1, Nx - 1);
			const int64 J0 = bCentral ? J - 1 : FMath::Max<int64>(J - 1, 0), J1 = bCentral ? J + 1 : FMath::Min<int64>(J + 1, Nz - 1);
			// The map's grid: central differences over 2·step with the
			// heights clamped at the edges (`H(i ± 1, j)` in Swift).
			const double Dx = bCentral ? Xs[1] - Xs[0] : 0, Dz = bCentral ? Zs[1] - Zs[0] : 0;
			const float SlopeX = bCentral ? float((At(I1, J) - At(I0, J)) / (2 * Dx))
			                              : float((At(I1, J) - At(I0, J)) / float(Xs[size_t(I1)] - Xs[size_t(I0)]));
			const float SlopeY = bCentral ? float((At(I, J1) - At(I, J0)) / (2 * Dz))
			                              : float((At(I, J1) - At(I, J0)) / float(Zs[size_t(J1)] - Zs[size_t(J0)]));
			return FVector3f(-SlopeX, -SlopeY, 1.0f).GetSafeNormal();
		}
	};

	/// A mesh over vertices [I0, I1] × [J0, J1] of `F`; `Skip(i, j)` leaves
	/// out the square whose low corner is (i, j). Null when nothing is left.
	TUniquePtr<FMeshDescription> MakeDescription(const FGridLines& F, int64 I0, int64 I1, int64 J0, int64 J1, bool bCentral,
	                                             bool bUVs, TFunctionRef<bool(int64, int64)> Skip)
	{
		const int64 W = I1 - I0 + 1, D = J1 - J0 + 1;
		int64 Quads = 0;
		for (int64 J = J0; J < J1; J++)
			for (int64 I = I0; I < I1; I++)
				if (!Skip(I, J)) Quads++;
		if (Quads == 0) return nullptr;

		TUniquePtr<FMeshDescription> MD = MakeUnique<FMeshDescription>();
		FStaticMeshAttributes Attr(*MD);
		Attr.Register();
		TVertexAttributesRef<FVector3f> Pos = Attr.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Normals = Attr.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector3f> Tangents = Attr.GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<float> Signs = Attr.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
		UVs.SetNumChannels(1);
		MD->ReserveNewVertices(int32(W * D));
		MD->ReserveNewVertexInstances(int32(W * D));
		MD->ReserveNewTriangles(int32(Quads * 2));
		MD->ReserveNewPolygons(int32(Quads * 2));
		MD->ReserveNewEdges(int32(Quads * 3 + W + D));
		const FPolygonGroupID Group = MD->CreatePolygonGroup();
		Attr.GetPolygonGroupMaterialSlotNames()[Group] = FName("Ground");

		// One vertex instance per vertex: the ground is smooth everywhere.
		TArray<FVertexInstanceID> Ids;
		Ids.SetNumUninitialized(int32(W * D));
		for (int64 J = J0; J <= J1; J++)
		{
			for (int64 I = I0; I <= I1; I++)
			{
				const FVertexID V = MD->CreateVertex();
				Pos[V] = FVector3f(float(F.Xs[size_t(I)] * AcSpace::CmPerCell), float(F.Zs[size_t(J)] * AcSpace::CmPerCell),
				                   F.At(I, J) * float(AcSpace::CmPerCell));
				const FVertexInstanceID VI = MD->CreateVertexInstance(V);
				const FVector3f N = F.Normal(I, J, bCentral);
				// Tangent along +X (the textures' U), bitangent +Y (their V):
				// Cross(N, T) is +Y for flat ground, so the sign is +1.
				const FVector3f T = (FVector3f(1, 0, 0) - N * N.X).GetSafeNormal();
				Normals[VI] = N;
				Tangents[VI] = T;
				Signs[VI] = 1.0f;
				UVs.Set(VI, 0, bUVs ? FVector2f(float(double(I) / double(F.Nx - 1)), float(double(J) / double(F.Nz - 1)))
				                    : FVector2f::ZeroVector);
				Ids[int32((J - J0) * W + (I - I0))] = VI;
			}
		}
		// Swift's `[a, c, b1, b1, c, d]`: the diagonal runs from (i+1, j) to
		// (i, j+1) (`FAcHeightGrid::height` relies on it). SceneKit → Unreal
		// swaps two axes, and Unreal's other handedness swaps them back, so
		// the same order faces up.
		for (int64 J = J0; J < J1; J++)
		{
			for (int64 I = I0; I < I1; I++)
			{
				if (Skip(I, J)) continue;
				const FVertexInstanceID A = Ids[int32((J - J0) * W + (I - I0))], B1 = Ids[int32((J - J0) * W + (I + 1 - I0))];
				const FVertexInstanceID C = Ids[int32((J + 1 - J0) * W + (I - I0))], Dd = Ids[int32((J + 1 - J0) * W + (I + 1 - I0))];
				MD->CreateTriangle(Group, {A, C, B1});
				MD->CreateTriangle(Group, {B1, C, Dd});
			}
		}
		return MD;
	}

	UStaticMesh* MakeMesh(UObject* Outer, const FName Name, const FMeshDescription& MD, UMaterialInterface* Material)
	{
		UStaticMesh* Mesh = NewObject<UStaticMesh>(Outer, Name, RF_Transient);
		Mesh->GetStaticMaterials().Add(FStaticMaterial(Material, FName("Ground"), FName("Ground")));
		Mesh->bSupportRayTracing = true;
		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bFastBuild = true;
		Params.bCommitMeshDescription = false;
		Params.bMarkPackageDirty = false;
		Params.bBuildSimpleCollision = false;
		Params.bAllowCpuAccess = false;
		Mesh->BuildFromMeshDescriptions({&MD}, Params);
		return Mesh;
	}

	/// `FAcHeightGrid::ParallelRows` on Unreal's task graph.
	void ParallelRows(int64_t N, const std::function<void(int64_t)>& Body)
	{
		ParallelFor(int32(N), [&Body](int32 J) { Body(J); });
	}

	TAutoConsoleVariable<int32> CVarBorderShadow(
		TEXT("ac.TerrainBorderShadow"), 0,
		TEXT("1: the border scenery casts shadows (Swift: never). Takes effect on the next build."));
}

double AAcTerrain::BorderWidth(const ac::GroundRect& B)
{
	return FMath::Max(60.0, 0.45 * FMath::Max(B.width(), B.depth()));
}

AAcTerrain::AAcTerrain()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;
	TerrainMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Terrain/M_Terrain.M_Terrain")));
}

AAcTerrain* AAcTerrain::Find(const UWorld* World)
{
	if (!World) return nullptr;
	TActorIterator<AAcTerrain> It(const_cast<UWorld*>(World));
	return It ? *It : nullptr;
}

AAcTerrain* AAcTerrain::SpawnFor(UWorld* World)
{
	if (!World) return nullptr;
	AAcTerrain* Terrain = Find(World);
	if (!Terrain)
	{
		FActorSpawnParameters Params;
		Params.Name = TEXT("AcTerrain");
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Terrain = World->SpawnActor<AAcTerrain>(AAcTerrain::StaticClass(), FTransform::Identity, Params);
	}
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(World))
	{
		if (!Terrain->GameStartedHandle.IsValid())
		{
			Terrain->GameStartedHandle = Sim->OnGameStarted.AddUObject(Terrain, &AAcTerrain::OnGameStarted);
		}
		if (Sim->IsRunning()) Terrain->OnGameStarted(*Sim);
	}
	Terrain->StageViewIfAsked();
	return Terrain;
}

void AAcTerrain::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this); Sim && GameStartedHandle.IsValid())
	{
		Sim->OnGameStarted.Remove(GameStartedHandle);
	}
	GameStartedHandle.Reset();
	Super::EndPlay(Reason);
}

double AAcTerrain::GroundZ(const FVector& World) const
{
	return Grid.empty() ? 0.0 : AcSpace::ToCm(Grid.height(AcSpace::ToSim(World)));
}

void AAcTerrain::OnGameStarted(UAcSimSubsystem& Sim)
{
	Build(Sim.Map());
}

void AAcTerrain::Clear()
{
	for (UStaticMeshComponent* C : Pieces)
	{
		if (C) C->DestroyComponent();
	}
	Pieces.Reset();
	Rocks.Reset();
	Grid = FAcHeightGrid();
	Field.reset();
	Map.reset();
}

void AAcTerrain::Build(const ac::MapDefinition& InMap)
{
	if (Map && *Map == InMap) return;
	const double Start = FPlatformTime::Seconds();
	Clear();
	Map = InMap;
	Field = std::make_unique<ac::TerrainField>(*Map);
	Grid = FAcHeightGrid(*Field, Density, ParallelRows);

	UMaterialInterface* Base = TerrainMaterial.LoadSynchronous();
	if (!Base)
	{
		UE_LOG(LogAutocraft, Error, TEXT("terrain: %s is missing (run Tools/Editor/make_terrain_material.py)"),
		       *TerrainMaterial.ToString());
	}
	Material = UMaterialInstanceDynamic::Create(Base, this, TEXT("TerrainMaterial"));
	BuildSplat();
	const ac::GroundRect B = Map->bounds;
	if (Material)
	{
		Material->SetTextureParameterValue(TEXT("Splat"), Splat);
		Material->SetVectorParameterValue(TEXT("Bounds"), FLinearColor(float(B.minX), float(B.minZ), float(B.width()), float(B.depth())));
		const FLinearColor H = Haze();
		Material->SetVectorParameterValue(TEXT("Haze"), FLinearColor(H.R, H.G, H.B, float(BorderWidth(B))));
	}
	BuildGround();
	BuildBorder();
	PlaceBorderRocks();
	UE_LOG(LogAutocraft, Log, TEXT("terrain: %s, %lld x %lld vertices, %d pieces, %d border rocks, built in %.0f ms"),
	       UTF8_TO_TCHAR(Map->name.c_str()), (long long)Grid.nx, (long long)Grid.nz, Pieces.Num(), Rocks.Num(),
	       (FPlatformTime::Seconds() - Start) * 1000.0);
}

UStaticMeshComponent* AAcTerrain::AddMeshComponent(UStaticMesh* Mesh, const TCHAR* Name, const bool bCastShadow)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, FName(Name));
	C->SetMobility(EComponentMobility::Static);
	C->SetStaticMesh(Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCanEverAffectNavigation(false);
	C->SetCastShadow(bCastShadow);
	C->bUseAsOccluder = true;
	C->SetupAttachment(Root);
	C->RegisterComponent();
	AddInstanceComponent(C);
	Pieces.Add(C);
	return C;
}

void AAcTerrain::BuildGround()
{
	// The grid the Swift mesh uses, from `Grid`'s heights.
	FGridLines F;
	F.Nx = Grid.nx;
	F.Nz = Grid.nz;
	F.Xs.resize(size_t(F.Nx));
	F.Zs.resize(size_t(F.Nz));
	for (int64 I = 0; I < F.Nx; I++) F.Xs[size_t(I)] = Grid.minX + double(I) * Grid.step;
	for (int64 J = 0; J < F.Nz; J++) F.Zs[size_t(J)] = Grid.minZ + double(J) * Grid.step;
	F.H = Grid.heights;

	// Tiles of TileCells × TileCells cells, sharing their edge vertices
	// (same heights and normals on both sides, so no seam).
	const int64 Per = int64(TileCells) * Density;
	const int64 TilesX = FMath::DivideAndRoundUp<int64>(F.Nx - 1, Per), TilesZ = FMath::DivideAndRoundUp<int64>(F.Nz - 1, Per);
	TArray<TUniquePtr<FMeshDescription>> Descriptions;
	Descriptions.SetNum(int32(TilesX * TilesZ));
	ParallelFor(Descriptions.Num(), [&](const int32 K) {
		const int64 TX = K % TilesX, TZ = K / TilesX;
		const int64 I0 = TX * Per, J0 = TZ * Per;
		const int64 I1 = FMath::Min(I0 + Per, F.Nx - 1), J1 = FMath::Min(J0 + Per, F.Nz - 1);
		Descriptions[K] = MakeDescription(F, I0, I1, J0, J1, /*bCentral*/ true, /*bUVs*/ true, [](int64, int64) { return false; });
	});
	for (int32 K = 0; K < Descriptions.Num(); K++)
	{
		if (!Descriptions[K]) continue;
		const FString Name = FString::Printf(TEXT("Ground_%d_%d"), int32(K % TilesX), int32(K / TilesX));
		UStaticMesh* Mesh = MakeMesh(this, FName(*(TEXT("SM_") + Name)), *Descriptions[K], Material);
		AddMeshComponent(Mesh, *Name, /*bCastShadow*/ true);
	}
}

void AAcTerrain::BuildBorder()
{
	const ac::GroundRect B = Map->bounds;
	const double Width = BorderWidth(B);
	// Grid lines along one axis: 0.5 cells near the edges, wider out.
	const auto Lines = [Width](const double Lo, const double Hi) {
		const auto Step = [](const double D) { return FMath::Min(0.5 + D * 0.12, 4.0); };
		std::vector<double> Out;
		double D = Width;
		while (D > 0.01)
		{
			Out.push_back(Lo - D);
			D -= Step(D);
		}
		for (double X = Lo; X < Lo + 2; X += 0.5) Out.push_back(X);
		for (double X = Lo + 2; X < Hi - 2; X += 1) Out.push_back(X);
		for (double X = Hi - 2; X <= Hi; X += 0.5) Out.push_back(X);
		D = 0.5;
		while (D <= Width + 1e-9)
		{
			Out.push_back(Hi + D);
			D += Step(D);
		}
		return Out;
	};
	FGridLines F;
	F.Xs = Lines(B.minX, B.maxX);
	F.Zs = Lines(B.minZ, B.maxZ);
	F.Nx = int64(F.Xs.size());
	F.Nz = int64(F.Zs.size());
	F.H.assign(size_t(F.Nx * F.Nz), 0.0f);
	ParallelFor(int32(F.Nz), [&](const int32 J) {
		for (int64 I = 0; I < F.Nx; I++)
		{
			const ac::Vec2 P(F.Xs[size_t(I)], F.Zs[size_t(J)]);
			// Under the map's own mesh: a touch low, out of sight.
			const double Sink = Field->outside(P) > 0 ? 0.0 : 0.08;
			F.H[size_t(J * F.Nx + I)] = float(Field->borderHeight(P) - Sink);
		}
	});
	// Cells wholly a cell inside the map are the map mesh's: skipped.
	const ac::GroundRect Inner = B.inset(1);
	const auto Skip = [&](const int64 I, const int64 J) {
		return F.Xs[size_t(I)] >= Inner.minX && F.Xs[size_t(I + 1)] <= Inner.maxX && F.Zs[size_t(J)] >= Inner.minZ &&
		       F.Zs[size_t(J + 1)] <= Inner.maxZ;
	};
	// Four pieces (one per side band), so each is culled on its own.
	const auto Index = [](const std::vector<double>& V, const double X) {
		return int64(std::lower_bound(V.begin(), V.end(), X) - V.begin());
	};
	const int64 IA = FMath::Clamp<int64>(Index(F.Xs, Inner.minX), 1, F.Nx - 1);
	const int64 IB = FMath::Clamp<int64>(Index(F.Xs, Inner.maxX), IA, F.Nx - 1);
	struct FBand { int64 I0, I1, J0, J1; const TCHAR* Name; };
	const int64 JA = FMath::Clamp<int64>(Index(F.Zs, Inner.minZ), 1, F.Nz - 1);
	const int64 JB = FMath::Clamp<int64>(Index(F.Zs, Inner.maxZ), JA, F.Nz - 1);
	const FBand Bands[] = {
		{0, F.Nx - 1, 0, JA, TEXT("Border_N")},
		{0, F.Nx - 1, JB, F.Nz - 1, TEXT("Border_S")},
		{0, IA, JA, JB, TEXT("Border_W")},
		{IB, F.Nx - 1, JA, JB, TEXT("Border_E")},
	};
	const bool bShadow = CVarBorderShadow.GetValueOnGameThread() != 0;
	for (const FBand& Band : Bands)
	{
		TUniquePtr<FMeshDescription> MD = MakeDescription(F, Band.I0, Band.I1, Band.J0, Band.J1, /*bCentral*/ false, /*bUVs*/ false, Skip);
		if (!MD) continue;
		UStaticMesh* Mesh = MakeMesh(this, FName(*(FString(TEXT("SM_")) + Band.Name)), *MD, Material);
		AddMeshComponent(Mesh, Band.Name, bShadow);
	}
}

void AAcTerrain::BuildSplat()
{
	// RGBA, 4 texels per cell: grass, highland, plating, scorch. Row 0 is
	// minY (the mesh's v = 0). Linear (not sRGB), uncompressed, no mips.
	const ac::GroundRect B = Map->bounds;
	const double PerCell = SplatPerCell;
	const int32 W = int32(B.width() * PerCell), H = int32(B.depth() * PerCell);
	TArray<uint8> Bytes;
	Bytes.SetNumZeroed(W * H * 4);
	const auto Byte = [](const double V) { return uint8(FMath::Clamp(V, 0.0, 1.0) * 255); };
	ParallelFor(H, [&](const int32 Y) {
		const double Z = B.minZ + (double(Y) + 0.5) / PerCell;
		for (int32 X = 0; X < W; X++)
		{
			const ac::TerrainField::Materials M = Field->materials(ac::Vec2(B.minX + (double(X) + 0.5) / PerCell, Z));
			uint8* Px = &Bytes[(Y * W + X) * 4];
			// BGRA in memory.
			Px[0] = Byte(M.z);
			Px[1] = Byte(M.y);
			Px[2] = Byte(M.x);
			Px[3] = Byte(M.w);
		}
	});
	Splat = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8, TEXT("TerrainSplat"));
	Splat->SRGB = false;
	Splat->Filter = TF_Bilinear;
	Splat->AddressX = TA_Clamp;
	Splat->AddressY = TA_Clamp;
	Splat->CompressionSettings = TC_VectorDisplacementmap;
	Splat->NeverStream = true;
	FTexture2DMipMap& Mip = Splat->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Bytes.GetData(), Bytes.Num());
	Mip.BulkData.Unlock();
	Splat->UpdateResource();
}

void AAcTerrain::PlaceBorderRocks()
{
	// `GameScene.borderRocks`: boulders and spires strewn over the border
	// scenery, thickest near the map's edge.
	const ac::GroundRect B = Map->bounds;
	const double Reach = 28.0;
	ac::SeededRandom Rng(Map->seed + 0xB0D3);
	using Kind = ac::Doodad::Kind;
	const Kind Kinds[] = {Kind::rock, Kind::rock, Kind::boulder, Kind::rockSpire};
	const int64 Count = int64((B.width() + B.depth()) * 2 * Reach / 110);
	int64 Placed = 0;
	for (int64 K = 0; K < Count * 20 && Placed < Count; K++)
	{
		// Swift evaluates the two coordinates left to right.
		const double X = Rng.range(B.minX - Reach, B.maxX + Reach);
		const double Y = Rng.range(B.minZ - Reach, B.maxZ + Reach);
		const ac::Vec2 P(X, Y);
		const double D = Field->outside(P);
		if (!(D > 1.5) || !(D < Reach) || !(Rng.unit() < 1 - 0.6 * D / Reach)) continue;
		const Kind Which = Kinds[FMath::Min(int32(Rng.unit() * 4), 3)];
		const double Scale = Rng.range(0.7, 1.5) * (1 + 0.5 * D / Reach);
		const double Turn = Rng.range(0, 2 * ac::pi);
		FAcBorderRock Rock;
		Rock.Kind = Which;
		Rock.Variant = uint32(K);
		// SceneKit `eulerAngles.y = a` is Unreal yaw -a (AcSpace.h).
		Rock.Transform = FTransform(FRotator(0.0, -FMath::RadiansToDegrees(Turn), 0.0),
		                            AcSpace::ToWorld(P, Field->borderHeight(P) - 0.15 * Scale), FVector(Scale));
		Rocks.Add(Rock);
		Placed++;
	}
}

// -AcTerrainView: the Swift game's default top-down framing and a stand-in
// sun, for terrain renders before the camera (A3) and daylight (B9) exist.
namespace
{
	/// A stand-in noon sun and sky, when nothing else (B9's AAcDaylight)
	/// lit the level by the first frame.
	void StandInSun(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		bool bSun = false;
		for (TObjectIterator<UDirectionalLightComponent> It; It; ++It)
		{
			if (It->GetWorld() == World && It->IsRegistered()) bSun = true;
		}
		if (!bSun)
		{
			// Noon (`Daylight.sun(hour: 12)`), toward the sun in SceneKit axes.
			const FVector Toward = AcSpace::AxesFromSceneKit(-60, 110, -55).GetSafeNormal();
			ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(FVector::ZeroVector, (-Toward).Rotation(), Params);
			UDirectionalLightComponent* L = Cast<UDirectionalLightComponent>(Sun->GetLightComponent());
			L->SetMobility(EComponentMobility::Movable);
			L->SetIntensity(5.0f);
			L->SetLightColor(FLinearColor(1.0f, 0.93f, 0.82f));
			L->SetAtmosphereSunLight(true);
			ASkyAtmosphere* Atmosphere = World->SpawnActor<ASkyAtmosphere>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
			(void)Atmosphere;
			ASkyLight* Sky = World->SpawnActor<ASkyLight>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
			USkyLightComponent* S = Sky->GetLightComponent();
			S->SetMobility(EComponentMobility::Movable);
			S->bRealTimeCapture = true;
			S->SetIntensity(1.0f);
			S->RecaptureSky();
		}

	}

	void StageTerrainView(UWorld* World, AAcTerrain* Terrain, const ac::MapDefinition& Map)
	{
		const TCHAR* Cmd = FCommandLine::Get();
		int32 W = 1600, H = 1000;
		FParse::Value(Cmd, TEXT("ResX="), W);
		FParse::Value(Cmd, TEXT("ResY="), H);
		double Zoom = 0, Cover = 0;
		FParse::Value(Cmd, TEXT("AcTerrainView="), Zoom);
		FParse::Value(Cmd, TEXT("AcTerrainCover="), Cover);

		// Over blue's main base (`GameController.swift:167`).
		const ac::Vec2 Main = Map.starts.empty() ? Map.front() : Map.bases[size_t(Map.starts[0])].center;
		ac::FreeView View(Map.bounds, Main);
		View.resize(ac::Vec2(W, H), Cover);
		if (Zoom > 0)
		{
			View.pointsPerCell = FMath::Clamp(Zoom, View.minPointsPerCell(), ac::FreeView::maxPointsPerCell);
			View.clamp();
		}
		// -AcTerrainAt=X,Y: centre there (as `windowshot --at`).
		FString At;
		if (FParse::Value(Cmd, TEXT("AcTerrainAt="), At, /*bShouldStopOnSeparator*/ false))
		{
			FString Xs, Ys;
			if (At.Split(TEXT(","), &Xs, &Ys)) View.center(ac::Vec2(FCString::Atod(*Xs), FCString::Atod(*Ys)));
		}
		const ac::Vec3 Eye = View.cameraPosition();
		const double Pitch = View.projection().pitchDegrees, Fov = View.projection().fovDegrees;

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACameraActor* Camera = World->SpawnActor<ACameraActor>(
			AcSpace::FromSceneKit(Eye.x, Eye.y, Eye.z), FRotator(-Pitch, -90.0, 0.0), Params);
		UCameraComponent* Cam = Camera->GetCameraComponent();
		Cam->SetConstraintAspectRatio(false);
		// The aspect too: with MaintainYFOV Unreal derives the vertical FOV
		// from the horizontal one through the camera's own aspect ratio.
		Cam->SetAspectRatio(float(W) / float(H));
		// Unreal's FOV is horizontal: the same vertical 30°.
		Cam->SetFieldOfView(float(FMath::RadiansToDegrees(2 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(Fov / 2)) * double(W) / double(H)))));
		Cam->PostProcessSettings.bOverride_AutoExposureMethod = true;
		Cam->PostProcessSettings.AutoExposureMethod = AEM_Manual;
		Cam->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		Cam->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
		Cam->PostProcessSettings.bOverride_AutoExposureBias = true;
		Cam->PostProcessSettings.AutoExposureBias = 0.0f;
		UE_LOG(LogAutocraft, Log, TEXT("terrain view: target %.1f,%.1f at %.1f points per cell, eye %s"),
		       View.target.x, View.target.y, View.pointsPerCell, *Camera->GetActorLocation().ToString());

		// Into the view at once and again once play is going (the camera
		// manager may still pick the controller's own view first).
		const TWeakObjectPtr<ACameraActor> WeakCamera(Camera);
		const TWeakObjectPtr<UWorld> WeakWorld(World);
		UAcShotSubsystem* Shot = UAcShotSubsystem::Get(World);
		if (Shot) Shot->Hold();
		const TWeakObjectPtr<UAcShotSubsystem> WeakShot(Shot);
		int32 Frames = 0;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakCamera, WeakWorld, WeakShot, Frames](float) mutable {
			if (!WeakWorld.IsValid() || !WeakCamera.IsValid()) return false;
			if (APlayerController* PC = WeakWorld->GetFirstPlayerController())
			{
				if (PC->GetViewTarget() != WeakCamera.Get()) PC->SetViewTarget(WeakCamera.Get());
			}
			if (++Frames == 2) StandInSun(WeakWorld.Get());
			// Hold the shot until the shaders are compiled and the ground
			// textures are in.
			const bool bCompiling = GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0;
			if (bCompiling || Frames < 20)
			{
				return true;
			}
			IStreamingManager::Get().StreamAllResources(1.0f);
			if (WeakShot.IsValid()) WeakShot->Release();
			return false;
		}));
	}
}

void AAcTerrain::StageViewIfAsked()
{
	double Zoom = 0;
	const TCHAR* Cmd = FCommandLine::Get();
	if (bViewStaged || !IsBuilt()) return;
	if (!FParse::Param(Cmd, TEXT("AcTerrainView")) && !FParse::Value(Cmd, TEXT("AcTerrainView="), Zoom)) return;
	bViewStaged = true;
	StageTerrainView(GetWorld(), this, *Map);
}
