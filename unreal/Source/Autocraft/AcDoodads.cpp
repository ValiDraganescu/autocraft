#include "AcDoodads.h"

#include "AcLog.h"
#include "AcModelCatalog.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"
#include "AcTerrain.h"

#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	const TCHAR* KindName(const ac::Doodad::Kind K)
	{
		using Kind = ac::Doodad::Kind;
		switch (K)
		{
		case Kind::rock: return TEXT("rock");
		case Kind::boulder: return TEXT("boulder");
		case Kind::rockSpire: return TEXT("rockSpire");
		case Kind::crate: return TEXT("crate");
		case Kind::crateStack: return TEXT("crateStack");
		case Kind::plant: return TEXT("plant");
		case Kind::bush: return TEXT("bush");
		case Kind::debris: return TEXT("debris");
		case Kind::deadTree: return TEXT("deadTree");
		case Kind::tower: return TEXT("tower");
		}
		return TEXT("rock");
	}
}

AAcDoodads::AAcDoodads()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;
}

AAcDoodads* AAcDoodads::Find(const UWorld* World)
{
	if (!World) return nullptr;
	TActorIterator<AAcDoodads> It(const_cast<UWorld*>(World));
	return It ? *It : nullptr;
}

AAcDoodads* AAcDoodads::SpawnFor(UWorld* World)
{
	if (!World) return nullptr;
	AAcDoodads* D = Find(World);
	if (!D)
	{
		FActorSpawnParameters Params;
		Params.Name = TEXT("AcDoodads");
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		D = World->SpawnActor<AAcDoodads>(AAcDoodads::StaticClass(), FTransform::Identity, Params);
	}
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(World))
	{
		if (!D->GameStartedHandle.IsValid()) D->GameStartedHandle = Sim->OnGameStarted.AddUObject(D, &AAcDoodads::OnGameStarted);
		if (!D->FrameHandle.IsValid())
		{
			D->FrameHandle = Sim->AddFrameListener(EAcFrameStage::Renderer, FAcFrameEvent::FDelegate::CreateUObject(D, &AAcDoodads::OnFrame));
		}
		if (Sim->IsRunning()) D->OnGameStarted(*Sim);
	}
	return D;
}

void AAcDoodads::EndPlay(const EEndPlayReason::Type Reason)
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

const FAcModelInfo* AAcDoodads::ModelFor(const ac::Doodad::Kind Kind, const uint32 Variant)
{
	const FAcModelCatalog& Catalog = FAcModelCatalog::Get();
	if (const FAcModelInfo* M = Catalog.Find(FName(*FString::Printf(TEXT("doodad_%s_%u"), KindName(Kind), Variant % 4u)))) return M;
	return Catalog.Find(FName(*FString::Printf(TEXT("doodad_%s"), KindName(Kind))));
}

FTransform AAcDoodads::Placement(const ac::Doodad& D, const double GroundHeight)
{
	// GameScene.addDoodad: sunk 0.05; eulerAngles.y = rotation (Unreal yaw
	// -rotation); a mirrored doodad flips along its own x (SceneKit x is
	// Unreal x), before the turn.
	const double S = D.scale;
	const bool bMirrored = D.mirrored.value_or(false);
	return FTransform(FRotator(0.0, -FMath::RadiansToDegrees(D.rotation), 0.0), AcSpace::ToWorld(D.position, GroundHeight - 0.05),
		FVector(bMirrored ? -S : S, S, S));
}

void AAcDoodads::OnGameStarted(UAcSimSubsystem& Sim)
{
	Rebuild(Sim);
}

void AAcDoodads::PlaceAll(FAcSceneryBatches& Batches, const std::vector<ac::Doodad>& List)
{
	const AAcTerrain* Terrain = AAcTerrain::Find(GetWorld());
	TArray<FAcSceneryModel> Models;
	Models.Reserve(int32(List.size()));
	for (const ac::Doodad& D : List)
	{
		const FAcModelInfo* M = ModelFor(D.kind, D.variant);
		if (!M)
		{
			UE_LOG(LogAutocraft, Warning, TEXT("doodads: no model for %s %u"), KindName(D.kind), D.variant);
			continue;
		}
		Models.Add(Batches.AddModel(*M, Placement(D, Terrain ? Terrain->FieldHeight(D.position) : 0.0)));
	}
	TArray<FAcSceneryModel*> Ptrs;
	for (FAcSceneryModel& M : Models) Ptrs.Add(&M);
	Batches.Flush(Ptrs);
}

void AAcDoodads::Rebuild(UAcSimSubsystem& Sim)
{
	const double Start = FPlatformTime::Seconds();
	Map.Clear();
	Border.Clear();
	Placed.Clear();
	PlacedList.clear();
	FAcSceneryOptions MapOptions;
	Map.Init(this, Root, MapOptions, TEXT("Doodad"));
	FAcSceneryOptions BorderOptions;
	BorderOptions.bCastShadow = false;  // GameScene.borderRocks: castsShadow = false
	Border.Init(this, Root, BorderOptions, TEXT("BorderRock"));
	Placed.Init(this, Root, MapOptions, TEXT("PlacedDoodad"));

	AAcTerrain* Terrain = AAcTerrain::SpawnFor(GetWorld());
	if (!Terrain || !Terrain->IsBuilt())
	{
		UE_LOG(LogAutocraft, Error, TEXT("doodads: no terrain to stand on"));
		return;
	}
	PlaceAll(Map, Sim.Map().doodads);
	{
		// One line a kind: how many, and where the first stands (to frame shots).
		TMap<FString, TPair<int32, ac::Vec2>> Kinds;
		for (const ac::Doodad& D : Sim.Map().doodads)
		{
			TPair<int32, ac::Vec2>& K = Kinds.FindOrAdd(KindName(D.kind), TPair<int32, ac::Vec2>(0, D.position));
			K.Key++;
		}
		for (const TPair<FString, TPair<int32, ac::Vec2>>& K : Kinds)
		{
			UE_LOG(LogAutocraft, Log, TEXT("doodads: %d %s, first at %.1f,%.1f"), K.Value.Key, *K.Key, K.Value.Value.x, K.Value.Value.y);
		}
	}

	TArray<FAcSceneryModel> Rocks;
	for (const FAcBorderRock& R : Terrain->BorderRocks())
	{
		if (const FAcModelInfo* M = ModelFor(R.Kind, R.Variant)) Rocks.Add(Border.AddModel(*M, R.Transform));
	}
	TArray<FAcSceneryModel*> Ptrs;
	for (FAcSceneryModel& M : Rocks) Ptrs.Add(&M);
	Border.Flush(Ptrs);

	if (Sim.State().doodads)
	{
		PlacedList = *Sim.State().doodads;
		PlaceAll(Placed, PlacedList);
	}
	UE_LOG(LogAutocraft, Log, TEXT("doodads: %d map doodads (%d instances, %d meshes), %d border rocks (%d instances), %d placed, in %.0f ms"),
		int32(Sim.Map().doodads.size()), Map.NumInstances(), Map.NumComponents(), Terrain->BorderRocks().Num(), Border.NumInstances(),
		int32(PlacedList.size()), (FPlatformTime::Seconds() - Start) * 1000.0);
}

void AAcDoodads::OnFrame(const FAcFrame& Frame)
{
	// GameScene.syncPlaced: the playground's doodads, drawn again when the
	// list changes.
	if (!Frame.State) return;
	static const std::vector<ac::Doodad> None;
	const std::vector<ac::Doodad>& Now = Frame.State->doodads ? *Frame.State->doodads : None;
	if (Now == PlacedList) return;
	PlacedList = Now;
	Placed.Clear();
	FAcSceneryOptions Options;
	Placed.Init(this, Root, Options, TEXT("PlacedDoodad"));
	PlaceAll(Placed, PlacedList);
}
