#include "AcEffects.h"

#include "AcLog.h"
#include "AcPose.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"
#include "AcWorldRenderer.h"

#include "Async/Async.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include "Rules.h"
#include "TerrainField.h"

#include <cmath>

namespace
{
	TAutoConsoleVariable<float> CVarEffectsStats(TEXT("ac.EffectsStats"), 0.f,
		TEXT("Log an `effects:` line (shots, pools, timed, pending) every N seconds; 0 off."));

	constexpr double Cm = AcSpace::CmPerCell;
	/// Cells a second a mini gun tracer flies, and its streak's length
	/// (`Effects.roundSpeed`, `roundLength`).
	constexpr double RoundSpeed = 40.0;
	constexpr double RoundLength = 0.6;
	/// Cells a second a plasma blob flies (`Effects.plasmaSpeed`).
	constexpr double PlasmaSpeed = 28.0;

	const TCHAR* const MEmissive = TEXT("/Game/Materials/M_Emissive.M_Emissive");
	const TCHAR* const MAdditive = TEXT("/Game/Materials/M_Additive.M_Additive");
	const TCHAR* const TSpark = TEXT("/Game/Models/Textures/T_spark.T_spark");

	double SrgbToLinear(const double C)
	{
		return C <= 0.04045 ? C / 12.92 : std::pow((C + 0.055) / 1.055, 2.4);
	}

	struct FExtensionRegistry
	{
		TArray<AcEffectsExtensions::FEntry> Entries;
	};
	FExtensionRegistry& Registry()
	{
		static FExtensionRegistry R;
		return R;
	}
}

namespace AcEffectsExtensions
{
	void Register(const TCHAR* Name, const int32 Order, FAcEffectsExtensionFactory Make)
	{
		Registry().Entries.Add({Name, Order, MoveTemp(Make)});
	}
	const TArray<FEntry>& All() { return Registry().Entries; }
}

// --- Setup -------------------------------------------------------------------

UAcEffects::UAcEffects()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UAcEffects* UAcEffects::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World) return nullptr;
	for (TActorIterator<AAcEffectsActor> It(World); It; ++It)
	{
		if (It->Effects) return It->Effects;
	}
	return nullptr;
}

void UAcEffects::BeginPlay()
{
	Super::BeginPlay();
	bLogShots = FParse::Param(FCommandLine::Get(), TEXT("AcEffectsLog"));
	if (FString Demo; FParse::Value(FCommandLine::Get(), TEXT("AcEffectsDemo="), Demo))
	{
		FString X, Y;
		if (Demo.Replace(TEXT("\""), TEXT("")).Split(TEXT(","), &X, &Y)) DemoAt = ac::Vec2(FCString::Atod(*X), FCString::Atod(*Y));
	}
	Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!Cylinder || !Sphere)
	{
		UE_LOG(LogAutocraft, Error, TEXT("effects: no engine basic shapes; nothing drawn"));
		return;
	}
	MakePools();

	// The extensions (C2, C3, ...), lowest order first.
	TArray<AcEffectsExtensions::FEntry> Entries = AcEffectsExtensions::All();
	Entries.StableSort([](const auto& A, const auto& B) { return A.Order < B.Order; });
	for (const auto& E : Entries)
	{
		if (TUniquePtr<FAcEffectsExtension> X = E.Make())
		{
			X->Begin(*this);
			Extensions.Add(MoveTemp(X));
			UE_LOG(LogAutocraft, Log, TEXT("effects: extension %s"), E.Name);
		}
	}

	if (UAcSimSubsystem* S = UAcSimSubsystem::Get(this))
	{
		FrameHandle = S->AddFrameListener(EAcFrameStage::Effects, FAcFrameEvent::FDelegate::CreateUObject(this, &UAcEffects::OnFrame));
		StartedHandle = S->OnGameStarted.AddUObject(this, &UAcEffects::OnGameStarted);
		if (S->IsRunning()) OnGameStarted(*S);
	}
	if (UAcWorldRenderer* R = Renderer())
	{
		CueHandle = R->OnCue.AddUObject(this, &UAcEffects::OnCue);
	}
	else
	{
		UE_LOG(LogAutocraft, Warning, TEXT("effects: no world renderer (spawn AAcWorld first): no muzzles, no cues"));
	}
}

void UAcEffects::EndPlay(const EEndPlayReason::Type Reason)
{
	for (TUniquePtr<FAcEffectsExtension>& X : Extensions) X->End();
	Extensions.Reset();
	if (UAcSimSubsystem* S = UAcSimSubsystem::Get(this))
	{
		S->RemoveFrameListener(EAcFrameStage::Effects, FrameHandle);
		S->OnGameStarted.Remove(StartedHandle);
	}
	if (UAcWorldRenderer* R = Renderer()) R->OnCue.Remove(CueHandle);
	UE_LOG(LogAutocraft, Log,
		TEXT("effects: totals shots %d, tracers %d, rounds %d, slugs %d, flashes %d, grenades %d, shells %d, bursts %d, explosions %d, unhandled %d"),
		Count.Shots, Count.Tracers, Count.Rounds, Count.Slugs, Count.Flashes, Count.Grenades, Count.Shells, Count.Bursts,
		Count.Explosions, Count.Unhandled);
	++Generation;
	Super::EndPlay(Reason);
}

UMaterialInstanceDynamic* UAcEffects::EmissiveMaterial(const FLinearColor& SrgbColor, const double Intensity,
	const bool bAdditive, UTexture* Texture)
{
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, bAdditive ? MAdditive : MEmissive);
	if (!Parent)
	{
		UE_LOG(LogAutocraft, Error, TEXT("effects: no %s (run Tools/Editor/make_materials.py)"), bAdditive ? MAdditive : MEmissive);
		return nullptr;
	}
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Parent, this);
	// MaterialLibrary.emissive: black diffuse, the colour as emission ×
	// intensity (linear, as ExportModels.linear folds it in).
	M->SetVectorParameterValue(TEXT("BaseColorTint"), FLinearColor::Black);
	const FLinearColor E(SrgbToLinear(SrgbColor.R) * Intensity, SrgbToLinear(SrgbColor.G) * Intensity,
		SrgbToLinear(SrgbColor.B) * Intensity, 1.f);
	M->SetVectorParameterValue(TEXT("EmissiveColor"), E);
	if (Texture) M->SetTextureParameterValue(TEXT("EmissiveTex"), Texture);
	Keep.Add(M);
	return M;
}

FAcEffectPool& UAcEffects::MakePool(UStaticMesh* Mesh, UMaterialInterface* Material, const int32 N, const FName Name,
	const bool bCastShadow)
{
	TUniquePtr<FAcEffectPool>& P = Pools.Add_GetRef(MakeUnique<FAcEffectPool>());
	P->Init(GetOwner(), Mesh, Material, N, Name, bCastShadow);
	Keep.Add(P->Component);
	return *P;
}

void UAcEffects::MakePools()
{
	// Effects.init (Effects.swift:57-144): the pools and their colours.
	UMaterialInterface* TracerM = EmissiveMaterial(FLinearColor(1, 0.85, 0.5), 2.2);
	UMaterialInterface* FlashM = EmissiveMaterial(FLinearColor(1, 0.8, 0.45), 2.6);
	UMaterialInterface* RoundM = EmissiveMaterial(FLinearColor(1, 0.66, 0.3), 3.2);
	UMaterialInterface* CoreM = EmissiveMaterial(FLinearColor(0.8, 0.93, 1), 4);
	UTexture* Spark = LoadObject<UTexture>(nullptr, TSpark);
	UMaterialInterface* HaloM = EmissiveMaterial(FLinearColor(0.3, 0.6, 1), 1.8, true, Spark);

	TracerPool = &MakePool(Cylinder, TracerM, 48, TEXT("AcTracers"));
	FlashPool = &MakePool(Sphere, FlashM, 32, TEXT("AcFlashes"));
	RoundPool = &MakePool(Cylinder, RoundM, 64, TEXT("AcRounds"));
	SlugCorePool = &MakePool(Sphere, CoreM, 16, TEXT("AcSlugs"));
	SlugHaloPool = &MakePool(Sphere, HaloM, 16, TEXT("AcSlugHalos"));
	TracerUntil.Init(0.0, TracerPool->Num());
	FlashUntil.Init(0.0, FlashPool->Num());
	Rounds.SetNum(RoundPool->Num());
	Slugs.SetNum(SlugCorePool->Num());
}

UAcWorldRenderer* UAcEffects::Renderer() const { return UAcWorldRenderer::Get(this); }
UAcSimSubsystem* UAcEffects::Sim() const { return UAcSimSubsystem::Get(this); }

double UAcEffects::GroundZ(const ac::Vec2 P) const
{
	return Field ? Field->height(P) * Cm : 0.0;
}

void UAcEffects::OnGameStarted(UAcSimSubsystem& S)
{
	Clear();
	if (FieldMap != &S.Map() || !Field)
	{
		Field = std::make_unique<ac::TerrainField>(S.Map());
		FieldMap = &S.Map();
	}
}

void UAcEffects::Clear()
{
	for (TUniquePtr<FAcEffectPool>& P : Pools)
	{
		for (int32 I = 0; I < P->Num(); ++I) P->Hide(I);
	}
	for (FFlight& F : Rounds) F.bLive = false;
	for (FFlight& F : Slugs) F.bLive = false;
	for (FTimed& T : Timed)
	{
		if (T.End) T.End();
	}
	Timed.Reset();
	Pending.Reset();
	{
		FScopeLock Lock(&DoneLock);
		Done.Reset();
	}
	++Generation;
	SlitShotTimes.Reset();
	Stomps.Reset();
	for (TUniquePtr<FAcEffectsExtension>& X : Extensions) X->Clear();
	for (TUniquePtr<FAcEffectPool>& P : Pools) P->Upload();
}

// --- The clock ---------------------------------------------------------------

void UAcEffects::Add(const double Life, const double Time, TFunction<void(double)> Tick, TFunction<void()> End)
{
	if (Tick) Tick(0.0);
	Timed.Add({Time, FMath::Max(Life, 1e-6), MoveTemp(Tick), MoveTemp(End)});
}

namespace
{
	struct FPendingLess
	{
		template <class T>
		bool operator()(const T& A, const T& B) const { return A.At < B.At || (A.At == B.At && A.Seq < B.Seq); }
	};
}

void UAcEffects::After(const double Delay, const double Time, TFunction<void()> Action)
{
	Pending.HeapPush({Time + Delay, PendingSeq++, MoveTemp(Action)}, FPendingLess());
}

void UAcEffects::OffThread(TFunction<TFunction<void()>()> Work)
{
	TWeakObjectPtr<UAcEffects> Weak(this);
	const int32 Gen = Generation;
	Async(EAsyncExecution::ThreadPool, [Weak, Gen, Work = MoveTemp(Work)]()
	{
		TFunction<void()> Then = Work();
		// Back on the game thread to queue it (the component may be gone).
		AsyncTask(ENamedThreads::GameThread, [Weak, Gen, Then = MoveTemp(Then)]() mutable
		{
			UAcEffects* E = Weak.Get();
			if (!E || E->Generation != Gen || !Then) return;
			FScopeLock Lock(&E->DoneLock);
			E->Done.Add(MoveTemp(Then));
		});
	});
}

// --- Frames ------------------------------------------------------------------

void UAcEffects::OnFrame(const FAcFrame& F)
{
	Consume(F);
}

void UAcEffects::Consume(const FAcFrame& F)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(AcEffects_Consume);
	if (!TracerPool) return;
	Frame = &F;
	for (const ac::GameEvent& E : F.Events)
	{
		if (const auto* S = E.as<ac::GameEvent::Shot>())
		{
			Shot(S->unit, S->target, S->at, F.Time);
		}
		else if (const auto* M = E.as<ac::GameEvent::Missed>())
		{
			// The sim says where the round is spent on the ground; the pilot
			// knows where the sight meets it (GameController.pilotMissed).
			const FVector To = M->unit == PilotFire.DrivenUnit && PilotFire.SightPoint
				? *PilotFire.SightPoint
				: AcSpace::ToWorld(M->at, GroundZ(M->at) / Cm);
			Missed(M->unit, To, F.Time);
		}
		else if (const auto* B = E.as<ac::GameEvent::Blast>())
		{
			Blast(B->at, B->radius, F.Time);
		}
		else if (const auto* St = E.as<ac::GameEvent::Stomp>())
		{
			Stomp(St->at, St->radius, F.Time);
		}
	}
	for (TUniquePtr<FAcEffectsExtension>& X : Extensions) X->Consume(F);
	Frame = nullptr;
	Update(F.Time);
}

void UAcEffects::Update(const double Time)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(AcEffects_Update);
	if (!TracerPool) return;
	NowTime = Time;
	Stomps.RemoveAll([Time](const FStompMark& S) { return Time - S.Time > 1.0; });
	for (int32 I = 0; I < TracerUntil.Num(); ++I)
	{
		if (TracerPool->IsShown(I) && Time >= TracerUntil[I]) TracerPool->Hide(I);
	}
	for (int32 I = 0; I < FlashUntil.Num(); ++I)
	{
		if (FlashPool->IsShown(I) && Time >= FlashUntil[I]) FlashPool->Hide(I);
	}
	FlyRounds(Time);
	FlySlugs(Time);
	// Delayed actions that came due, in the order they were asked for.
	if (Pending.Num() > 0 && Pending.HeapTop().At <= Time)
	{
		TArray<FPending> Due;
		while (Pending.Num() > 0 && Pending.HeapTop().At <= Time)
		{
			FPending P;
			Pending.HeapPop(P, FPendingLess(), EAllowShrinking::No);
			Due.Add(MoveTemp(P));
		}
		Due.Sort([](const FPending& A, const FPending& B) { return A.Seq < B.Seq; });
		for (FPending& P : Due) P.Run();
	}
	TArray<TFunction<void()>> Finished;
	{
		FScopeLock Lock(&DoneLock);
		Finished = MoveTemp(Done);
		Done.Reset();
	}
	for (TFunction<void()>& F : Finished) F();
	for (TUniquePtr<FAcEffectsExtension>& X : Extensions) X->Update(Time);
	if (Timed.Num() > 0)
	{
		// A tick may start new effects; they join after this pass.
		TArray<FTimed> Running = MoveTemp(Timed);
		Timed.Reset();
		TArray<FTimed> KeepTimed;
		KeepTimed.Reserve(Running.Num());
		for (FTimed& E : Running)
		{
			const double Age = (Time - E.Start) / E.Life;
			if (Age >= 1)
			{
				if (E.End) E.End();
				continue;
			}
			if (E.Tick) E.Tick(FMath::Max(Age, 0.0));
			KeepTimed.Add(MoveTemp(E));
		}
		KeepTimed.Append(MoveTemp(Timed));
		Timed = MoveTemp(KeepTimed);
	}
	if (DemoAt) Demo(Time);
	for (TUniquePtr<FAcEffectPool>& P : Pools) P->Upload();

	if (const float Every = CVarEffectsStats.GetValueOnGameThread(); Every > 0)
	{
		const double Wall = FPlatformTime::Seconds();
		if (Wall - StatsWall >= Every)
		{
			StatsWall = Wall;
			int32 Live = 0;
			for (TUniquePtr<FAcEffectPool>& P : Pools)
				for (int32 I = 0; I < P->Num(); ++I) Live += P->IsShown(I) ? 1 : 0;
			UE_LOG(LogAutocraft, Log, TEXT("effects: shots %d, tracers %d, rounds %d, slugs %d, flashes %d, unhandled %d; live %d, timed %d, pending %d"),
				Count.Shots, Count.Tracers, Count.Rounds, Count.Slugs, Count.Flashes, Count.Unhandled, Live, Timed.Num(), Pending.Num());
		}
	}
}

void UAcEffects::OnCue(int64 Id, const FAcPoseCue& Cue)
{
	// Cues the poses send (B2-B7), in game time now (the poses ran this frame).
	const UAcSimSubsystem* S = Sim();
	const double Time = S ? S->GameTime() : NowTime;
	static const FName CrystalBiteName(TEXT("CrystalBite")), CutterSparkName(TEXT("CutterSpark")),
		DustPuffName(TEXT("DustPuff")), FlashName(TEXT("Flash")), AtlasStepName(TEXT("AtlasStep"));
	if (Cue.What == CrystalBiteName) CrystalBite(Cue.At, Time);
	else if (Cue.What == CutterSparkName) CutterSpark(Cue.At, Time);
	else if (Cue.What == DustPuffName) DustPuff(Cue.At, Cue.Value > 0 ? Cue.Value : 1.0, Time);
	else if (Cue.What == FlashName) Flash(Cue.At, Cue.Value > 0 ? Cue.Value : 1.0, Time);
	else if (Cue.What == AtlasStepName) Footfall(Cue.At, Time, int32(Cue.Value));
	else
	{
		for (TUniquePtr<FAcEffectsExtension>& X : Extensions)
			if (X->HandleCue(Cue)) return;
	}
}

// --- Pools: tracers, flashes, rounds, slugs ----------------------------------

void UAcEffects::Tracer(const FVector& A, const FVector& B, const double Time)
{
	if (!TracerPool) return;
	const FVector D = B - A;
	const double Len = D.Size();
	if (Len <= 0.05 * Cm) return;
	const int32 I = TracerPool->Next();
	TracerUntil[I] = Time + 0.06;
	// Only the front half of the way, so it reads as a round in flight.
	const FVector Mid = A + D * 0.6;
	TracerPool->Set(I, TracerPool->Shape(Mid, FAcEffectPool::Along(D / Len), FVector(0.032 * Cm, 0.032 * Cm, Len * 0.55)));
	++Count.Tracers;
	Flash(B, 0.7 + 0.6 * double((FlashPool->Peek() * 7919) % 10) / 10, Time);
}

void UAcEffects::RailShot(const FVector& A, const FVector& B, const double Time)
{
	if (!TracerPool) return;
	const FVector D = B - A;
	const double Len = D.Size();
	if (Len <= 0.05 * Cm) return;
	const int32 I = TracerPool->Next();
	TracerUntil[I] = Time + 0.22;
	TracerPool->Set(I, TracerPool->Shape(A + D * 0.5, FAcEffectPool::Along(D / Len), FVector(0.05 * Cm, 0.05 * Cm, Len)));
	++Count.Tracers;
	Flash(A, 2.4, Time, 0.12);
	Flash(B, 3.2, Time, 0.14);
	Explosion(B, 0.25, Time);
}

void UAcEffects::Flash(const FVector& At, const double K, const double Time, const double Life)
{
	if (!FlashPool) return;
	const int32 I = FlashPool->Next();
	FlashUntil[I] = Time + Life;
	const double D = 2 * 0.09 * Cm * K;
	FlashPool->Set(I, FlashPool->Shape(At, FQuat::Identity, FVector(D)));
	++Count.Flashes;
}

void UAcEffects::MinigunRound(const FVector& From, const FVector& To, const double Time)
{
	if (!RoundPool) return;
	const double Len = FVector::Dist(From, To);
	if (Len <= 0.05 * Cm) return;
	const int32 I = RoundPool->Next();
	Rounds[I] = {From, To, Time, (Len / Cm) / RoundSpeed, true};
	PlaceRound(I, Time);
	++Count.Rounds;
}

void UAcEffects::PlaceRound(const int32 I, const double Time)
{
	// The streak's head where the round is now, its tail no further back
	// than the muzzle (cells, as Swift).
	const FFlight& R = Rounds[I];
	const FVector D = R.To - R.From;
	const double Len = D.Size() / Cm;
	const double Head = FMath::Min(Len, FMath::Max(0.0, Time - R.Start) * RoundSpeed + RoundLength * 0.5);
	const double Tail = FMath::Max(0.0, Head - RoundLength);
	const FVector Dir = D / (Len * Cm);
	const FVector Mid = R.From + Dir * ((Head + Tail) / 2 * Cm);
	RoundPool->Set(I, RoundPool->Shape(Mid, FAcEffectPool::Along(Dir),
		FVector(0.044 * Cm, 0.044 * Cm, FMath::Max(Head - Tail, 0.05) * Cm)));
}

void UAcEffects::FlyRounds(const double Time)
{
	for (int32 I = 0; I < Rounds.Num(); ++I)
	{
		FFlight& R = Rounds[I];
		if (!R.bLive) continue;
		if (Time >= R.Start + R.Flight)
		{
			R.bLive = false;
			RoundPool->Hide(I);
			Flash(R.To, 0.55, Time, 0.05);
			FAcBurstRequest B;
			B.Kind = EAcBurst::Ricochet;
			B.At = R.To;
			B.Size = 1;
			B.Time = Time;
			B.Duration = 0.05;
			B.Along = (R.To - R.From).GetSafeNormal();
			Burst(B);
		}
		else
		{
			PlaceRound(I, Time);
		}
	}
}

void UAcEffects::Slug(const FVector& From, const FVector& To, const double Time)
{
	if (!SlugCorePool) return;
	const double Len = FVector::Dist(From, To);
	if (Len <= 0.05 * Cm) return;
	const int32 I = SlugCorePool->Next();
	SlugHaloPool->Next();
	Slugs[I] = {From, To, Time, (Len / Cm) / PlasmaSpeed, true};
	const FQuat Q = FAcEffectPool::Along((To - From) / Len);
	// A core sphere r 0.065 stretched 2.4 along the way, a halo r 0.17
	// (× 0.7 along, under the core's stretch).
	SlugCorePool->Set(I, SlugCorePool->Shape(From, Q, FVector(0.13 * Cm, 0.13 * Cm, 0.13 * 2.4 * Cm)));
	SlugHaloPool->Set(I, SlugHaloPool->Shape(From, Q, FVector(0.34 * Cm, 0.34 * Cm, 0.34 * 2.4 * 0.7 * Cm)));
	++Count.Slugs;
}

void UAcEffects::FlySlugs(const double Time)
{
	for (int32 I = 0; I < Slugs.Num(); ++I)
	{
		FFlight& R = Slugs[I];
		if (!R.bLive) continue;
		if (Time >= R.Start + R.Flight)
		{
			R.bLive = false;
			SlugCorePool->Hide(I);
			SlugHaloPool->Hide(I);
			Flash(R.To, 1.8, Time, 0.09);
			FAcBurstRequest B;
			B.Kind = EAcBurst::Blast;
			B.At = R.To;
			B.Size = 0.7;
			B.Time = Time;
			Burst(B);
		}
		else
		{
			const FVector P = R.From + (R.To - R.From) * (FMath::Max(0.0, Time - R.Start) / R.Flight);
			const FQuat Q = FAcEffectPool::Along((R.To - R.From).GetSafeNormal());
			SlugCorePool->Set(I, SlugCorePool->Shape(P, Q, FVector(0.13 * Cm, 0.13 * Cm, 0.13 * 2.4 * Cm)));
			SlugHaloPool->Set(I, SlugHaloPool->Shape(P, Q, FVector(0.34 * Cm, 0.34 * Cm, 0.34 * 2.4 * 0.7 * Cm)));
		}
	}
}

// --- Requests for the extensions ---------------------------------------------

void UAcEffects::Launch(FAcLaunch& L, const bool bShell)
{
	for (TUniquePtr<FAcEffectsExtension>& X : Extensions)
	{
		if (bShell ? X->HandleAnchorShell(L) : X->HandleGrenade(L)) return;
	}
	// Stand-in until C2 flies it: it comes down where the target is when
	// the damage lands.
	++Count.Unhandled;
	After(L.Flight, L.Time, [L = MoveTemp(L)]()
	{
		const TOptional<FVector> Now = L.Track ? L.Track() : TOptional<FVector>();
		if (L.Land) L.Land(L.Time + L.Flight, Now.Get(L.To));
	});
}

void UAcEffects::Grenade(const FVector& From, const FVector& To, const double Ground, const double Flight,
	const double Time, TFunction<TOptional<FVector>()> Track)
{
	FAcLaunch L;
	L.From = From;
	L.To = To;
	L.GroundZ = Ground;
	L.Flight = Flight;
	L.Time = Time;
	L.Arc = 0.25 + 0.07 * FVector::Dist(From, To) / Cm;
	L.Track = MoveTemp(Track);
	TWeakObjectPtr<UAcEffects> Weak(this);
	L.Land = [Weak, Ground](const double T, const FVector& B)
	{
		if (UAcEffects* E = Weak.Get()) E->GrenadeLanded(T, B, Ground);
	};
	++Count.Grenades;
	Launch(L, false);
}

void UAcEffects::GrenadeLanded(const double T, const FVector& B, const double Ground)
{
	// A concussive blast: fire, smoke, a flash and a quick shock ring.
	Flash(B, 2.6, T, 0.1);
	FAcBurstRequest R;
	R.Kind = EAcBurst::Blast;
	R.At = B;
	R.Size = 0.85;
	R.Time = T;
	Burst(R);
	FAcShockRingRequest Ring;
	Ring.At = FVector(B.X, B.Y, Ground);
	Ring.Radius = 0.8 * Cm;
	Ring.Life = 0.3;
	Ring.Time = T;
	Ring.Bright = 0.7;
	ShockRing(Ring);
}

void UAcEffects::AnchorShell(const FVector& From, const FVector& To, const double Ground, const double Flight,
	const double Time, TFunction<TOptional<FVector>()> Track)
{
	FAcLaunch L;
	L.From = From;
	L.To = To;
	L.GroundZ = Ground;
	L.Flight = Flight;
	L.Time = Time;
	// Ground distance only (SceneKit x and z).
	L.Arc = 0.8 + FVector::Dist2D(From, To) / Cm * 0.18;
	L.Track = MoveTemp(Track);
	TWeakObjectPtr<UAcEffects> Weak(this);
	L.Land = [Weak, Ground](const double T, const FVector& B)
	{
		if (UAcEffects* E = Weak.Get()) E->ShellLanded(T, B, Ground);
	};
	++Count.Shells;
	Launch(L, true);
}

void UAcEffects::ShellLanded(const double T, const FVector& B, const double Ground)
{
	// A big blast with a shock ring over the splash, a dust cloud rolling
	// out low, debris and a scorch mark.
	Explosion(B, 1.25, T);
	Flash(B, 5, T, 0.1);
	FAcShockRingRequest Ring;
	Ring.At = FVector(B.X, B.Y, Ground);
	Ring.Radius = (ac::Rules::splash.back().radius + 0.2) * Cm;
	Ring.Life = 0.45;
	Ring.Time = T;
	Ring.Bright = 1;
	ShockRing(Ring);
	FAcBurstRequest Dust;
	Dust.Kind = EAcBurst::Dust;
	Dust.At = FVector(B.X, B.Y, Ground + 0.1 * Cm);
	Dust.Size = 1.4;
	Dust.Time = T;
	Dust.Duration = 0.12;
	Burst(Dust);
	FAcDebrisRequest D;
	D.At = FVector(B.X, B.Y, Ground + 0.15 * Cm);
	D.Count = 5;
	D.Size = 0.6;
	D.GroundZ = Ground;
	D.Time = T;
	D.Seed = int32(T * 100);
	Debris(D);
	FAcScorchRequest S;
	S.At = FVector(B.X, B.Y, Ground);
	S.Radius = 1.15 * Cm;
	S.Life = 14;
	S.Time = T;
	Scorch(S);
}

void UAcEffects::Explosion(const FVector& At, const double Size, const double Time)
{
	++Count.Explosions;
	for (TUniquePtr<FAcEffectsExtension>& X : Extensions)
		if (X->HandleExplosion(At, Size, Time)) return;
	// Stand-in until C3: a bright flash the size of the blast.
	++Count.Unhandled;
	Flash(At, 3 * Size, Time, 0.12);
}

void UAcEffects::Burst(const FAcBurstRequest& R)
{
	++Count.Bursts;
	for (TUniquePtr<FAcEffectsExtension>& X : Extensions)
		if (X->HandleBurst(R)) return;
	++Count.Unhandled;
}

void UAcEffects::DustPuff(const FVector& At, const double Size, const double Time)
{
	FAcBurstRequest R;
	R.Kind = EAcBurst::Dust;
	R.At = At;
	R.Size = Size;
	R.Time = Time;
	R.Duration = 0.1;
	Burst(R);
}

void UAcEffects::CrystalBite(const FVector& At, const double Time)
{
	FAcBurstRequest R;
	R.Kind = EAcBurst::Chips;
	R.At = At;
	R.Size = 1;
	R.Time = Time;
	R.Duration = 0.08;
	Burst(R);
}

void UAcEffects::FlameLine(const ac::Vec2 A, const ac::Vec2 B, const double Time)
{
	// Effects.flameLine (:420): flame licks along the line, the last one
	// biggest, and a scorch streak.
	const ac::Vec2 D = B - A;
	const double Len = ac::length(D);
	if (Len <= 0.3) return;
	for (int32 K = 0; K < 3; ++K)
	{
		const ac::Vec2 P = A + D * (0.45 + 0.27 * K);
		FAcBurstRequest R;
		R.Kind = EAcBurst::Flames;
		R.At = AcSpace::ToWorld(P, GroundZ(P) / Cm + 0.05);
		R.Size = 0.75 + 0.25 * K;
		R.Time = Time;
		R.Duration = 0.3;
		Burst(R);
	}
	const ac::Vec2 Mid = A + D * 0.7;
	FAcScorchRequest S;
	S.At = AcSpace::ToWorld(Mid, GroundZ(Mid) / Cm);
	S.Radius = 0.4 * Cm;
	S.Stretch = FMath::Max(1.0, Len * 0.35 / 0.4);
	// SceneKit's eulerAngles.y = -atan2(d.y, d.x) is UE yaw +atan2.
	S.Yaw = std::atan2(D.y, D.x);
	S.Life = 6;
	S.Time = Time;
	S.Darkness = 0.75;
	Scorch(S);
}

void UAcEffects::Scorch(const FAcScorchRequest& R)
{
	for (TUniquePtr<FAcEffectsExtension>& X : Extensions)
		if (X->HandleScorch(R)) return;
	++Count.Unhandled;
}

void UAcEffects::Decal(const FAcDecalRequest& R)
{
	for (TUniquePtr<FAcEffectsExtension>& X : Extensions)
		if (X->HandleDecal(R)) return;
	++Count.Unhandled;
}

void UAcEffects::ShockRing(const FAcShockRingRequest& R)
{
	for (TUniquePtr<FAcEffectsExtension>& X : Extensions)
		if (X->HandleShockRing(R)) return;
	++Count.Unhandled;
}

void UAcEffects::Debris(const FAcDebrisRequest& R)
{
	for (TUniquePtr<FAcEffectsExtension>& X : Extensions)
		if (X->HandleDebris(R)) return;
	++Count.Unhandled;
}

void UAcEffects::Demo(const double Time)
{
	// Rows 1 cell apart, shooting along +x over 6 cells at chest height:
	// tracers and flashes every frame (they live a few frames), rounds and
	// slugs every 0.1 s, a grenade every 0.5 s, so a still at any frame
	// shows each in flight.
	const ac::Vec2 C = *DemoAt;
	auto At = [this, C](const double Dx, const double Dy, const double H)
	{
		const ac::Vec2 P(C.x + Dx, C.y + Dy);
		return AcSpace::ToWorld(P, GroundZ(P) / Cm + H);
	};
	Tracer(At(-3, -2, 0.6), At(3, -2, 0.62), Time);
	Flash(At(-1, 1, 0.6), 1, Time, 0.02);
	Flash(At(1, 1, 0.6), 2.6, Time, 0.02);
	if (Time < NextDemo) return;
	NextDemo = Time + 0.1;
	MinigunRound(At(-3, -1, 0.6), At(3, -1, 0.62), Time);
	Slug(At(-3, 0, 0.6), At(3, 0, 0.62), Time);
	if (std::fmod(Time, 0.5) < 0.1)
		Grenade(At(-3, 2, 0.9), At(3, 2, 0.62), GroundZ(ac::Vec2(C.x + 3, C.y + 2)), 0.15 + 6.0 / 14, Time);
}

// --- The actor ---------------------------------------------------------------

AAcEffectsActor::AAcEffectsActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Effects = CreateDefaultSubobject<UAcEffects>(TEXT("Effects"));
}

AAcEffectsActor* AAcEffectsActor::SpawnFor(UWorld* World)
{
	if (!World) return nullptr;
	if (TActorIterator<AAcEffectsActor> It(World); It) return *It;
	FActorSpawnParameters Params;
	Params.Name = TEXT("AcEffects");
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<AAcEffectsActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
}
