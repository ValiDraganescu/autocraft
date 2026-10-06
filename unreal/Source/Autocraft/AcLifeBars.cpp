#include "AcLifeBars.h"
#include "AcFog.h"
#include "AcInstancedMesh.h"

#include "AcLog.h"
#include "AcModelCatalog.h"
#include "AcPose.h"
#include "AcRtsPawn.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"
#include "AcTerrain.h"
#include "AcWorldRenderer.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FreeView.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include <cmath>

namespace
{
	TAutoConsoleVariable<int32> CVarLifeBars(TEXT("ac.LifeBars"), 1, TEXT("Life bars over hurt and focused units and buildings (1) or none (0)."));
	TAutoConsoleVariable<int32> CVarLifeBarsAll(TEXT("ac.LifeBarsAll"), 0,
		TEXT("Dev: a life bar over every unit and building, full or not (the look and cost of a whole army's bars)."));

	TAutoConsoleVariable<float> CVarLifeBarsStats(TEXT("ac.LifeBarsStats"), 0.f,
		TEXT("Log a `life bars:` line (bars, mean and max game-thread ms) every N seconds; 0 off."));

	const TCHAR* const QuadPath = TEXT("/Engine/BasicShapes/Plane.Plane");
	const TCHAR* const MaterialPath = TEXT("/Game/Materials/M_LifeBar.M_LifeBar");
	/// Custom data per bar: fill, segments, aspect, spare.
	constexpr int32 Floats = 4;

	FTransform Gone() { return FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector); }
}

// --- the rules ---------------------------------------------------------------

namespace AcLifeBars
{
	double SegmentHP(const double MaxHP)
	{
		for (const double S : {10.0, 25.0, 50.0, 100.0, 200.0, 500.0})
		{
			if (MaxHP / S <= 10) return S;
		}
		return 1000.0;
	}

	double Width(const ac::UnitKind Kind) { return FMath::Max(0.8, ac::Rules::radius(Kind) * 2.2); }
	double Width(const ac::StructureKind Kind) { return ac::Rules::radius(Kind) * 1.7; }

	double BuildingRadius(const ac::StructureKind Kind)
	{
		// GameScene.makeBuilding (GameScene.swift:376-420).
		switch (Kind)
		{
		case ac::StructureKind::citadel: return 2.5;
		case ac::StructureKind::lab:
		case ac::StructureKind::habDome:
		case ac::StructureKind::sentinel: return 1.0;
		default: return 1.5;
		}
	}

	double BarScale(const double PointsPerCell)
	{
		return std::pow(ac::FreeView::defaultPointsPerCell / PointsPerCell, 0.9);
	}

	double FullHP(const ac::GameState& State, const ac::Unit& U)
	{
		bool bShield = false;
		if (U.kind == ac::UnitKind::ranger && U.owner >= 0 && U.owner < int64(State.players.size()))
		{
			const auto& Up = State.players[size_t(U.owner)].upgrades;
			bShield = Up && Up->count(ac::Upgrade::aegisShield) > 0;
		}
		return ac::Rules::hp(U.kind) + (bShield ? 10.0 : 0.0);
	}
}

// --- the actor ---------------------------------------------------------------

AAcLifeBars::AAcLifeBars()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Bars = CreateDefaultSubobject<UAcInstancedMesh>(TEXT("Bars"));  // cheap bounds (P2)
	Bars->SetupAttachment(RootComponent);
	Bars->SetMobility(EComponentMobility::Movable);
	Bars->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bars->SetCanEverAffectNavigation(false);
	Bars->SetCastShadow(false);
	Bars->bAffectDistanceFieldLighting = false;
	Bars->bVisibleInRayTracing = false;
	Bars->bVisibleInReflectionCaptures = false;
	Bars->bVisibleInRealTimeSkyCaptures = false;
	Bars->SetReceivesDecals(false);
	// After the world's own translucency (SceneKit renderingOrder 3000).
	Bars->SetTranslucentSortPriority(1000);
	Bars->SetNumCustomDataFloats(Floats);
}

AAcLifeBars* AAcLifeBars::SpawnFor(UWorld* World)
{
	if (!World) return nullptr;
	if (TActorIterator<AAcLifeBars> It(World); It) return *It;
	FActorSpawnParameters Params;
	Params.Name = TEXT("AcLifeBars");
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<AAcLifeBars>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
}

AAcLifeBars* AAcLifeBars::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World) return nullptr;
	TActorIterator<AAcLifeBars> It(World);
	return It ? *It : nullptr;
}

void AAcLifeBars::BeginPlay()
{
	Super::BeginPlay();
	Bars->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, QuadPath));
	if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, MaterialPath)) Bars->SetMaterial(0, M);
	else UE_LOG(LogAutocraft, Warning, TEXT("life bars: no %s (run Tools/Editor/make_lifebar_material.py)"), MaterialPath);
	if (FParse::Param(FCommandLine::Get(), TEXT("AcLifeBarsAll"))) CVarLifeBarsAll->Set(1, ECVF_SetByCommandline);

	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim)
	{
		UE_LOG(LogAutocraft, Error, TEXT("life bars: no simulation"));
		return;
	}
	// Spawned after AAcWorld: this listener runs after the renderer's sync.
	FrameHandle = Sim->AddFrameListener(EAcFrameStage::Renderer, FAcFrameEvent::FDelegate::CreateUObject(this, &AAcLifeBars::OnFrame));
	StartedHandle = Sim->OnGameStarted.AddUObject(this, &AAcLifeBars::OnGameStarted);
}

void AAcLifeBars::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this))
	{
		if (FrameHandle.IsValid()) Sim->RemoveFrameListener(EAcFrameStage::Renderer, FrameHandle);
		if (StartedHandle.IsValid()) Sim->OnGameStarted.Remove(StartedHandle);
	}
	FrameHandle.Reset();
	StartedHandle.Reset();
	Super::EndPlay(Reason);
}

void AAcLifeBars::OnGameStarted(UAcSimSubsystem&)
{
	Focus.Reset();
	ClearPilot();
	HiddenUnit = INDEX_NONE;
}

double AAcLifeBars::UnitTop(const FAcModelInfo& Model)
{
	if (const double* T = Tops.Find(&Model)) return *T;
	// `GameScene.top(of:)` × the root's scale: the highest point of the
	// shown parts in the root's own frame (the export's bounds), scaled.
	const double Scale = Model.Parts.Num() > 0 ? Model.Parts[0].Rest.GetScale3D().Z : 1.0;
	const double T = Model.Bounds.IsValid ? AcSpace::ToCells(Model.Bounds.Max.Z) * Scale : 1.0;
	Tops.Add(&Model, T);
	return T;
}

void AAcLifeBars::OnFrame(const FAcFrame&)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(AcLifeBars);
	const double Start = FPlatformTime::Seconds();
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	UAcWorldRenderer* Renderer = UAcWorldRenderer::Get(this);
	Used = 0;
	if (!Sim || !Sim->IsRunning() || !Renderer || CVarLifeBars.GetValueOnGameThread() == 0)
	{
		Upload(0);
		return;
	}
	if (!Terrain.IsValid())
	{
		TActorIterator<AAcTerrain> It(GetWorld());
		if (It) Terrain = *It;
	}

	// The eye: its up on screen (bars lift along it) and its turn (bars face it).
	FRotator Eye(-56.0, 0.0, 0.0);
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
	{
		Eye = PC->PlayerCameraManager->GetCameraRotation();
	}
	const FRotationMatrix EyeAxes(Eye);
	const FVector Forward = EyeAxes.GetUnitAxis(EAxis::X);
	const FVector Right = EyeAxes.GetUnitAxis(EAxis::Y);
	const FVector Up = EyeAxes.GetUnitAxis(EAxis::Z);
	// The engine plane: +Z its face, X its u (left to right). Its Y then
	// runs down the screen with v (top to bottom).
	const FQuat Facing = FRotationMatrix::MakeFromZX(-Forward, Right).ToQuat();

	// `barScale` (GameController.swift:414): 1 while driving.
	double K = 1.0;
	if (!PilotEye.IsSet())
	{
		const AAcRtsPawn* Pawn = AAcRtsPawn::Get(this);
		const ac::FreeView* View = Pawn ? Pawn->View() : nullptr;
		K = AcLifeBars::BarScale(View && View->pointsPerCell > 0 ? View->pointsPerCell : 52.0);
	}
	const bool bDriving = PilotEye.IsSet();
	const double Wide = bDriving ? 0.5 : FMath::Max(1.0, 0.7 * K);
	const double Tall = bDriving ? 0.6 : K;
	const bool bAll = CVarLifeBarsAll.GetValueOnGameThread() != 0;

	Xf.Reset();
	Data.Reset();
	auto Show = [&](const int64 Id, const FVector& Over, const double Height, const double Lift, const double Width,
	                const double BarHeight, const double Frac, const double MaxHP)
	{
		if (Id == Sight) return;
		const FVector At = Over + FVector(0, 0, AcSpace::ToCm(Height)) + Up * AcSpace::ToCm(Lift);
		// Close to the eye a bar would fill the view: it shrinks inside 4
		// cells and goes under 1.5.
		double Near = 1.0;
		if (bDriving)
		{
			const double D = AcSpace::ToCells(FVector::Dist(At, *PilotEye));
			if (D < 1.5) return;
			Near = FMath::Min(1.0, D / 4.0);
		}
		const double W = Width * Wide * Near, H = BarHeight * Tall * Near;
		// The plane is 1 m (one cell) square.
		Xf.Add(FTransform(Facing, At, FVector(W, H, 1.0)));
		Data.Add(float(FMath::Clamp(Frac, 0.0, 1.0)));
		Data.Add(float(MaxHP / AcLifeBars::SegmentHP(MaxHP)));
		Data.Add(float(W / H));
		Data.Add(0.f);
	};

	// What the renderer draws (`ac.RenderShowAll`: everything, fog ignored).
	static IConsoleVariable* ShowAll = IConsoleManager::Get().FindConsoleVariable(TEXT("ac.RenderShowAll"));
	const ac::GameState& Shown = ShowAll && ShowAll->GetInt() != 0 ? Sim->State() : Sim->Shown();
	// Buildings out of sight (remembered intel) show no bar: only their last look.
	const AAcFog* Fog = AAcFog::Find(GetWorld());
	const std::set<int64_t>* Fogged = Fog ? &const_cast<AAcFog*>(Fog)->Unseen() : nullptr;
	for (const ac::Structure& S : Shown.structures)
	{
		if (S.hp <= 0) continue;
		if (Fogged && Fogged->count(S.id)) continue;
		const double Full = ac::Rules::hp(S.kind);
		if (!(S.hp < Full - 0.5 || Focus.Contains(S.id) || bAll)) continue;
		if (!Renderer->ModelOf(S.id)) continue;
		const double Ground = Terrain.IsValid() ? Terrain->FieldHeight(S.position) : 0.0;
		const double R = AcLifeBars::BuildingRadius(S.kind);
		Show(S.id, AcSpace::ToWorld(S.position, Ground), AcPose::BuildingHeight(S.kind), R * 0.6 + 0.3 * K,
		     AcLifeBars::Width(S.kind), 0.26, S.hp / Full, Full);
	}
	for (const ac::Unit& U : Shown.units)
	{
		if (U.hp <= 0 || U.id == HiddenUnit) continue;
		// Hidden as the renderer hides them (aboard, in a Bastion or a Derrick).
		if (U.task == ac::Unit::Task::inBastion || U.task == ac::Unit::Task::aboard || U.task == ac::Unit::Task::inDerrick) continue;
		const double Full = AcLifeBars::FullHP(Shown, U);
		if (!(U.hp < Full - 0.5 || Focus.Contains(U.id) || bAll)) continue;
		const FAcModelInfo* Model = Renderer->ModelOf(U.id);
		const TConstArrayView<FTransform> World = Renderer->PartWorld(U.id);
		if (!Model || World.Num() == 0) continue;
		const double R = ac::Rules::radius(U.kind);
		Show(U.id, World[0].GetLocation(), UnitTop(*Model), R * 0.5 + 0.2 * K, AcLifeBars::Width(U.kind), 0.2, U.hp / Full, Full);
	}
	Used = Xf.Num();
	Upload(Used);
	Ms = (FPlatformTime::Seconds() - Start) * 1000.0;

	if (const float Every = CVarLifeBarsStats.GetValueOnGameThread(); Every > 0.f)
	{
		++StatFrames;
		StatMs += Ms;
		StatMax = FMath::Max(StatMax, Ms);
		const double Now = FPlatformTime::Seconds();
		if (StatSince == 0.0) StatSince = Now;
		if (Now - StatSince >= Every)
		{
			UE_LOG(LogAutocraft, Log, TEXT("life bars: %d bars (%d units, %d buildings shown), %.3f ms mean, %.3f max over %d frames"),
			       Used, int32(Shown.units.size()), int32(Shown.structures.size()), StatMs / StatFrames, StatMax, StatFrames);
			StatSince = Now;
			StatFrames = 0;
			StatMs = StatMax = 0.0;
		}
	}
}

void AAcLifeBars::Upload(const int32 Count)
{
	if (!Bars) return;
	// Grow the instance count when needed, never shrink: spare instances
	// are scaled to nothing.
	const int32 Have = Bars->GetInstanceCount();
	if (Have < Count)
	{
		TArray<FTransform> More;
		More.Init(Gone(), Count - Have);
		Bars->AddInstances(More, false, false, false);
	}
	// What changes this frame: this frame's bars and last frame's spares.
	const int32 Span = FMath::Max(Count, LastUsed);
	LastUsed = Count;
	if (Span == 0) return;
	while (Xf.Num() < Span) Xf.Add(Gone());
	Xf.SetNum(Span);
	Data.SetNumZeroed(Span * Floats);
	// An overlay needs no motion vectors (no previous transforms kept).
	Bars->BatchUpdateInstancesTransforms(0, Xf, false, false, true);
	Bars->SetCustomData(0, Span - 1, TConstArrayView<float>(Data.GetData(), Span * Floats), false);
	Bars->MarkRenderInstancesDirty();
}
