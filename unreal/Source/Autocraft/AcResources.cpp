#include "AcResources.h"
#include "AcInstancedMesh.h"

#include "AcDaylight.h"
#include "AcLog.h"
#include "AcModelCatalog.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"
#include "AcTerrain.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"

namespace
{
	const TCHAR* const WorldParametersPath = TEXT("/Game/Materials/MPC_AcWorld.MPC_AcWorld");
	const TCHAR* const VapourMaterialPath = TEXT("/Game/Materials/MI_AcVapour.MI_AcVapour");
	const TCHAR* const GlintMaterialPath = TEXT("/Game/Materials/MI_AcGlint.MI_AcGlint");
	const TCHAR* const SpriteMeshPath = TEXT("/Engine/BasicShapes/Plane.Plane");

	/// A stateless random number in [0, 1) for particle `K` of emitter `Seed`,
	/// stream `S`.
	double Rand(const uint32 Seed, const int64 K, const uint32 S)
	{
		uint64 X = uint64(Seed) * 0x9E3779B97F4A7C15ull ^ uint64(K) * 0xBF58476D1CE4E5B9ull ^ uint64(S + 1) * 0x94D049BB133111EBull;
		X ^= X >> 30;
		X *= 0xBF58476D1CE4E5B9ull;
		X ^= X >> 27;
		X *= 0x94D049BB133111EBull;
		X ^= X >> 31;
		return double(X >> 11) * (1.0 / 9007199254740992.0);
	}

	/// A keyframed curve (CAKeyframeAnimation with linear steps).
	double Keys(const double U, std::initializer_list<double> Values, std::initializer_list<double> Times)
	{
		const double* V = Values.begin();
		const double* T = Times.begin();
		const int32 N = int32(Values.size());
		if (U <= T[0]) return V[0];
		for (int32 I = 1; I < N; ++I)
		{
			if (U <= T[I]) return FMath::Lerp(V[I - 1], V[I], (U - T[I - 1]) / FMath::Max(T[I] - T[I - 1], 1e-6));
		}
		return V[N - 1];
	}

	/// A unit direction within `HalfAngle` (radians) of up, SceneKit-style
	/// spread, in Unreal axes.
	FVector Spread(const double HalfAngle, const double A, const double B)
	{
		const double Theta = HalfAngle * FMath::Sqrt(A);
		const double Phi = B * UE_DOUBLE_TWO_PI;
		return FVector(FMath::Sin(Theta) * FMath::Cos(Phi), FMath::Sin(Theta) * FMath::Sin(Phi), FMath::Cos(Theta));
	}

	/// The export's ore deposit for a patch (Swift seeds it with the id).
	FName OreModel(const int64 Id) { return FName(*FString::Printf(TEXT("ore_%d"), int32(((Id % 4) + 4) % 4))); }
}

AAcResources::AAcResources()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	// Static, so the wells' static HISMs may hang off it.
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;
}

AAcResources* AAcResources::Find(const UWorld* World)
{
	if (!World) return nullptr;
	TActorIterator<AAcResources> It(const_cast<UWorld*>(World));
	return It ? *It : nullptr;
}

AAcResources* AAcResources::SpawnFor(UWorld* World)
{
	if (!World) return nullptr;
	AAcResources* R = Find(World);
	if (!R)
	{
		FActorSpawnParameters Params;
		Params.Name = TEXT("AcResources");
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		R = World->SpawnActor<AAcResources>(AAcResources::StaticClass(), FTransform::Identity, Params);
	}
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(World))
	{
		if (!R->GameStartedHandle.IsValid()) R->GameStartedHandle = Sim->OnGameStarted.AddUObject(R, &AAcResources::OnGameStarted);
		if (!R->FrameHandle.IsValid())
		{
			R->FrameHandle = Sim->AddFrameListener(EAcFrameStage::Renderer, FAcFrameEvent::FDelegate::CreateUObject(R, &AAcResources::OnFrame));
		}
		if (Sim->IsRunning()) R->OnGameStarted(*Sim);
	}
	return R;
}

void AAcResources::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this))
	{
		if (GameStartedHandle.IsValid()) Sim->OnGameStarted.Remove(GameStartedHandle);
		if (FrameHandle.IsValid()) Sim->RemoveFrameListener(EAcFrameStage::Renderer, FrameHandle);
	}
	GameStartedHandle.Reset();
	FrameHandle.Reset();
	Super::EndPlay(Reason);
}

UInstancedStaticMeshComponent* AAcResources::MakeSprites(const TCHAR* Name, const TCHAR* MaterialPath)
{
	UInstancedStaticMeshComponent* C = NewObject<UAcInstancedMesh>(this, FName(Name));  // cheap bounds (P2)
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, SpriteMeshPath));
	if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, MaterialPath)) C->SetMaterial(0, M);
	else UE_LOG(LogAutocraft, Warning, TEXT("resources: no %s (run Tools/Editor/make_resource_materials.py)"), MaterialPath);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCanEverAffectNavigation(false);
	C->SetCastShadow(false);
	C->bAffectDistanceFieldLighting = false;
	C->SetNumCustomDataFloats(4);
	C->SetupAttachment(Root);
	C->RegisterComponent();
	AddInstanceComponent(C);
	return C;
}

void AAcResources::OnGameStarted(UAcSimSubsystem& Sim)
{
	Rebuild(Sim);
}

void AAcResources::Rebuild(UAcSimSubsystem& Sim)
{
	const double Start = FPlatformTime::Seconds();
	Ore.Clear();
	WellBatches.Clear();
	Patches.Reset();
	Wells.Reset();
	MapWells = 0;
	if (!Vapour) Vapour = MakeSprites(TEXT("Vapour"), VapourMaterialPath);
	if (!Glitter) Glitter = MakeSprites(TEXT("Glitter"), GlintMaterialPath);
	if (!WorldParameters) WorldParameters = LoadObject<UMaterialParameterCollection>(nullptr, WorldParametersPath);

	FAcSceneryOptions OreOptions;
	OreOptions.bHierarchical = false;  // stages change: plain ISMs
	OreOptions.Mobility = EComponentMobility::Movable;
	Ore.Init(this, Root, OreOptions, TEXT("Ore"));
	FAcSceneryOptions WellOptions;
	WellBatches.Init(this, Root, WellOptions, TEXT("Well"));

	// The terrain must be built first: it owns the field heights.
	AAcTerrain* Terrain = AAcTerrain::SpawnFor(GetWorld());
	if (!Terrain || !Terrain->IsBuilt())
	{
		UE_LOG(LogAutocraft, Error, TEXT("resources: no terrain to stand on"));
		return;
	}
	const ac::MapDefinition& Map = Sim.Map();
	MapName = UTF8_TO_TCHAR(Map.name.c_str());
	AddPatches(Sim.Shown(), 0);
	for (int32 I = 0; I < int32(Map.bases.size()); ++I)
	{
		for (int32 J = 0; J < int32(Map.bases[I].wells.size()); ++J) AddWell(Map.bases[I].wells[J], I * 10 + J);
	}
	MapWells = Wells.Num();
	SyncWells(Sim.Shown());
	TArray<FAcSceneryModel*> WellModels;
	for (FWell& W : Wells) WellModels.Add(&W.Model);
	WellBatches.Flush(WellModels);
	UE_LOG(LogAutocraft, Log, TEXT("resources: %d ore patches (%d instances), %d wells (%d instances) in %.0f ms"),
		Patches.Num(), Ore.NumInstances(), Wells.Num(), WellBatches.NumInstances(), (FPlatformTime::Seconds() - Start) * 1000.0);
}

void AAcResources::AddPatches(const ac::GameState& State, const int32 From)
{
	const AAcTerrain* Terrain = AAcTerrain::Find(GetWorld());
	const FAcModelCatalog& Catalog = FAcModelCatalog::Get();
	const int32 First = Patches.Num();
	for (int32 K = From; K < int32(State.patches.size()); ++K)
	{
		const ac::OreDeposit& P = State.patches[size_t(K)];
		const FAcModelInfo* Model = Catalog.Find(OreModel(P.id));
		if (!Model)
		{
			UE_LOG(LogAutocraft, Warning, TEXT("resources: no model %s"), *OreModel(P.id).ToString());
			continue;
		}
		// GameScene.addPatch: at the field height, eulerAngles.y = angle
		// (Unreal yaw -angle, AcSpace.h).
		const FTransform Placement(FRotator(0.0, -FMath::RadiansToDegrees(P.angle), 0.0),
			AcSpace::ToWorld(P.position, Terrain ? Terrain->FieldHeight(P.position) : 0.0));
		FPatch& Patch = Patches.AddDefaulted_GetRef();
		Patch.Id = P.id;
		Patch.Seed = uint32(P.id) * 7919u + 17u;
		Patch.Model = Ore.AddModel(*Model, Placement);
		for (int32 I = 0; I < Model->Parts.Num(); ++I)
		{
			const FString Name = Model->Parts[I].Name.ToString();
			if (Model->Parts[I].Parent == INDEX_NONE) Patch.RootPart = I;
			else if (Name.StartsWith(TEXT("stage")))
			{
				const int32 K3 = FCString::Atoi(*Name + 5);
				if (K3 >= 1 && K3 <= 3) Patch.StagePart[K3 - 1] = I;
			}
		}
		// ModelLibrary.oreGlitter: a 1.7 × 0.4 × 0.5 box 0.3 up the deposit.
		Patch.Emitter = FTransform(FVector(0.0, 0.0, AcSpace::ToCm(0.3))) * Placement;
	}
	TArray<FAcSceneryModel*> Models;
	for (int32 I = First; I < Patches.Num(); ++I) Models.Add(&Patches[I].Model);
	Ore.Flush(Models);
	for (int32 I = First; I < Patches.Num(); ++I)
	{
		const ac::OreDeposit* P = nullptr;
		for (const ac::OreDeposit& X : State.patches)
		{
			if (X.id == Patches[I].Id) P = &X;
		}
		ApplyStage(Patches[I], P ? P->stage() : 3);
	}
	Ore.Commit();
}

void AAcResources::AddWell(const ac::Vec2 Position, const int32 Seed)
{
	const AAcTerrain* Terrain = AAcTerrain::Find(GetWorld());
	// The export has seeds 0 and 3; the seed turns the ring of rocks round
	// the mound (`a = i/7·2π + seed`), so turn the nearer export by the rest.
	const int32 Variant = (Seed % 2 == 0) ? 0 : 3;
	const FAcModelInfo* Model = FAcModelCatalog::Get().Find(FName(*FString::Printf(TEXT("well_%d"), Variant)));
	if (!Model)
	{
		UE_LOG(LogAutocraft, Warning, TEXT("resources: no model well_%d"), Variant);
		return;
	}
	// GameScene.addWell: sunk 0.05 into the ground.
	const FVector At = AcSpace::ToWorld(Position, (Terrain ? Terrain->FieldHeight(Position) : 0.0) - 0.05);
	const FTransform Placement(FRotator(0.0, FMath::RadiansToDegrees(double(Seed - Variant)), 0.0), At);
	FWell& W = Wells.AddDefaulted_GetRef();
	W.Position = Position;
	W.Seed = uint32(Seed) * 104729u + 3u;
	W.Model = WellBatches.AddModel(*Model, Placement);
	W.Emitter = At + FVector(0.0, 0.0, AcSpace::ToCm(0.62));
}

void AAcResources::SyncWells(const ac::GameState& State)
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	for (FWell& W : Wells)
	{
		// GameScene.sync: the Derrick's own steam replaces the well's plume.
		bool bCapped = false;
		for (const ac::Structure& S : State.structures)
		{
			if (S.kind == ac::StructureKind::derrick && ac::distance(S.position, W.Position) < 0.5)
			{
				bCapped = true;
				break;
			}
		}
		if (bCapped == W.bCapped) continue;
		W.bCapped = bCapped;
		if (bCapped) W.Closed = Now;
		else
		{
			W.Open = Now;
			W.Closed = 1e18;
		}
	}
}

void AAcResources::ApplyStage(FPatch& Patch, const int64 Stage)
{
	if (Patch.Stage == Stage) return;
	Patch.Stage = Stage;
	// GameScene.applyStage: the deposit hides at 0; nodule K while stage < K.
	if (Patch.RootPart != INDEX_NONE) Ore.SetVisible(Patch.Model, Patch.RootPart, Stage > 0);
	for (int32 K = 1; K <= 3; ++K)
	{
		if (Patch.StagePart[K - 1] != INDEX_NONE) Ore.SetVisible(Patch.Model, Patch.StagePart[K - 1], Stage >= K && Stage > 0);
	}
}

void AAcResources::OnFrame(const FAcFrame& Frame)
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || !Sim->IsRunning()) return;
	const ac::GameState& State = Sim->Shown();

	// Ore and wells put down in the playground since the last frame.
	if (int32(State.patches.size()) > Patches.Num()) AddPatches(State, Patches.Num());
	if (State.wells && int32(State.wells->size()) > Wells.Num())
	{
		TArray<FAcSceneryModel*> Added;
		const int32 First = Wells.Num();
		for (int32 K = Wells.Num(); K < int32(State.wells->size()); ++K) AddWell((*State.wells)[size_t(K)].position, 1000 + (K - MapWells));
		for (int32 I = First; I < Wells.Num(); ++I) Added.Add(&Wells[I].Model);
		WellBatches.Flush(Added);
	}

	// Patch stages.
	if (int32(State.patches.size()) == Patches.Num())
	{
		for (int32 I = 0; I < Patches.Num(); ++I)
		{
			const ac::OreDeposit& P = State.patches[size_t(I)];
			if (P.id == Patches[I].Id) ApplyStage(Patches[I], P.stage());
		}
	}
	else
	{
		for (FPatch& Patch : Patches)
		{
			for (const ac::OreDeposit& P : State.patches)
			{
				if (P.id == Patch.Id) ApplyStage(Patch, P.stage());
			}
		}
	}
	Ore.Commit();
	SyncWells(State);

	// How dark it is, for every material that reads MPC_AcWorld.
	if (WorldParameters)
	{
		const AAcDaylight* Daylight = AAcDaylight::Find(GetWorld());
		const double Dark = Daylight ? Daylight->GetDark() : 0.0;
		if (Dark != LastDark)
		{
			LastDark = Dark;
			UKismetMaterialLibrary::SetScalarParameterValue(this, WorldParameters, TEXT("Dark"), float(Dark));
		}
	}
	DrawParticles(GetWorld()->GetTimeSeconds());
}

void AAcResources::DrawParticles(const double Now)
{
	if (!Vapour || !Glitter) return;
	FVector Forward = FVector::ForwardVector, Right = FVector::RightVector;
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
	{
		const FRotator R = PC->PlayerCameraManager->GetCameraRotation();
		Forward = R.Vector();
		Right = FRotationMatrix(R).GetUnitAxis(EAxis::Y);
	}
	// The engine plane faces +Z: turn it to face the eye.
	const FQuat Facing = FRotationMatrix::MakeFromZX(-Forward, Right).ToQuat();

	auto Upload = [&](UInstancedStaticMeshComponent* C, const int32 Count)
	{
		// Keep the instance count (never shrink); hide the spare ones.
		const int32 Have = C->GetInstanceCount();
		if (Have < Count)
		{
			TArray<FTransform> More;
			More.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), Count - Have);
			C->AddInstances(More, false, true);
		}
		const int32 Total = C->GetInstanceCount();
		while (SpriteTransforms.Num() < Total) SpriteTransforms.Add(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector));
		for (int32 I = Count; I < Total; ++I) SpriteTransforms[I].SetScale3D(FVector::ZeroVector);
		SpriteTransforms.SetNum(Total);
		C->BatchUpdateInstancesTransforms(0, SpriteTransforms, true, false, true);
		for (int32 I = 0; I < Count; ++I) C->SetCustomData(I, TArrayView<const float>(&SpriteData[I * 4], 4), false);
		C->MarkRenderStateDirty();
	};

	// Vapour: ModelLibrary.well's `hydrogen` system.
	{
		SpriteTransforms.Reset();
		SpriteData.Reset();
		const double Rate = 7.0, Life = 2.8, LifeVar = 0.8;
		const double MaxLife = Life + LifeVar * 0.5;
		for (const FWell& W : Wells)
		{
			const int64 K1 = int64(FMath::FloorToDouble(Now * Rate));
			const int64 K0 = int64(FMath::FloorToDouble((Now - MaxLife) * Rate));
			for (int64 K = K0; K <= K1; ++K)
			{
				const double Born = double(K) / Rate;
				if (Born < W.Open || Born >= W.Closed) continue;
				const double L = Life + LifeVar * (Rand(W.Seed, K, 0) - 0.5);
				const double Age = Now - Born;
				if (Age < 0.0 || Age > L) continue;
				const double U = Age / L;
				// A disc of radius 0.42 at the pool; up within 24° at
				// 0.45 ± 0.1; acceleration (0.03, 0.06) cells/s².
				const double Rr = 0.42 * FMath::Sqrt(Rand(W.Seed, K, 1)), Ra = Rand(W.Seed, K, 2) * UE_DOUBLE_TWO_PI;
				const FVector P0 = W.Emitter + FVector(Rr * FMath::Cos(Ra), Rr * FMath::Sin(Ra), 0.0) * 100.0;
				const double Speed = 0.45 + 0.2 * (Rand(W.Seed, K, 3) - 0.5);
				const FVector V = Spread(FMath::DegreesToRadians(12.0), Rand(W.Seed, K, 4), Rand(W.Seed, K, 5)) * Speed;
				const FVector A(0.03, 0.0, 0.06);
				const FVector P = P0 + (V * Age + 0.5 * A * Age * Age) * 100.0;
				const double Size = (0.36 + 0.12 * (Rand(W.Seed, K, 6) - 0.5)) * Keys(U, {0.4, 1.3, 2.6}, {0.0, 0.45, 1.0});
				const double Alpha = 0.3 * Keys(U, {0.0, 0.4, 0.0}, {0.0, 0.2, 1.0});
				const FQuat Roll(-Forward, Rand(W.Seed, K, 7) * UE_DOUBLE_TWO_PI);
				SpriteTransforms.Add(FTransform(Roll * Facing, P, FVector(Size)));
				SpriteData.Append({0.82f, 0.9f, 1.0f, float(Alpha)});
			}
		}
		Upload(Vapour, SpriteTransforms.Num());
	}

	// Glitter: ModelLibrary.oreGlitter, off every deposit that still shows.
	{
		SpriteTransforms.Reset();
		SpriteData.Reset();
		const double Rate = 6.0, Life = 2.6, LifeVar = 0.9;
		const double MaxLife = Life + LifeVar * 0.5;
		for (const FPatch& Patch : Patches)
		{
			if (Patch.Stage <= 0) continue;
			const int64 K1 = int64(FMath::FloorToDouble(Now * Rate));
			const int64 K0 = int64(FMath::FloorToDouble((Now - MaxLife) * Rate));
			for (int64 K = K0; K <= K1; ++K)
			{
				const double Born = double(K) / Rate;
				const double L = Life + LifeVar * (Rand(Patch.Seed, K, 0) - 0.5);
				const double Age = Now - Born;
				if (Age < 0.0 || Age > L) continue;
				const double U = Age / L;
				// A 1.7 × 0.4 × 0.5 box (SceneKit x, y, z) in the deposit's frame.
				const FVector Local = AcSpace::FromSceneKit(1.7 * (Rand(Patch.Seed, K, 1) - 0.5), 0.4 * (Rand(Patch.Seed, K, 2) - 0.5),
					0.5 * (Rand(Patch.Seed, K, 3) - 0.5));
				const FVector P0 = Patch.Emitter.TransformPosition(Local);
				const double Speed = 0.14 + 0.08 * (Rand(Patch.Seed, K, 4) - 0.5);
				const FVector V = Spread(FMath::DegreesToRadians(15.0), Rand(Patch.Seed, K, 5), Rand(Patch.Seed, K, 6)) * Speed;
				const FVector P = P0 + (V * Age + 0.5 * FVector(0.0, 0.0, 0.04) * Age * Age) * 100.0;
				const double Size = 0.05 + 0.02 * (Rand(Patch.Seed, K, 7) - 0.5);
				// Hue anywhere (0.5 ± 0.5), saturation 0.4, intensity 3.
				const FLinearColor C = FLinearColor(float(Rand(Patch.Seed, K, 8) * 360.0), 0.4f, 1.0f).HSVToLinearRGB();
				const double Alpha = Keys(U, {0.0, 1.0, 0.25, 0.9, 0.15, 0.0}, {0.0, 0.12, 0.35, 0.55, 0.8, 1.0});
				const FQuat Roll(-Forward, Rand(Patch.Seed, K, 9) * UE_DOUBLE_HALF_PI);
				SpriteTransforms.Add(FTransform(Roll * Facing, P, FVector(Size)));
				SpriteData.Append({C.R * 3.0f, C.G * 3.0f, C.B * 3.0f, float(Alpha)});
			}
		}
		Upload(Glitter, SpriteTransforms.Num());
	}
}
