#include "AcBench.h"

#include "AcLog.h"
#include "AcPerf.h"
#include "AcPerfSystem.h"
#include "AcPilotPawn.h"
#include "AcRtsPawn.h"
#include "AcSimSubsystem.h"
#include "AcWorldRenderer.h"

#include "Components/LocalLightComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RHIStats.h"
#include "Stats/StatsData.h"
#include "RenderCore.h"
#include "RHI.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"

#include "NavGrid.h"
#include "Rules.h"
#include "Simulation.h"
#include "Types.h"

#include <cmath>
#include <vector>

namespace
{
	/// A counter of a shown stat group (`stat rhi`), its average over the
	/// stats frame; -1 if the group is not shown (as AcWorldRenderer's).
	double StatCounter(const TCHAR* Name)
	{
#if STATS
		const FGameThreadStatsData* D = FLatestGameThreadStatsData::Get().Latest;
		if (!D) return -1;
		const FName Short(Name);
		for (const FActiveStatGroupInfo& G : D->ActiveStatGroups)
		{
			for (const FComplexStatMessage& M : G.CountersAggregate)
			{
				if (M.NameAndInfo.GetShortName() != Short) continue;
				return M.NameAndInfo.GetField<EStatDataType>() == EStatDataType::ST_double
					? M.GetValue_double(EComplexStatField::IncAve)
					: double(M.GetValue_int64(EComplexStatField::IncAve));
			}
		}
#endif
		return -1;
	}

	/// `stat gpu`'s passes on the first graphics queue: busy ms a frame (the
	/// stats' average), largest first.
	TArray<TPair<FString, double>> GpuStatPasses()
	{
		TArray<TPair<FString, double>> Out;
#if STATS
		const FGameThreadStatsData* D = FLatestGameThreadStatsData::Get().Latest;
		if (!D) return Out;
		for (int32 G = 0; G < D->ActiveStatGroups.Num(); ++G)
		{
			if (!D->GroupNames.IsValidIndex(G) || D->GroupNames[G].ToString() != TEXT("STATGROUP_GPU0_Graphics0")) continue;
			for (const FComplexStatMessage& M : D->ActiveStatGroups[G].GpuStatsAggregate)
			{
				// The type is the short name's number: 0 busy, 1 wait, 2 idle (StatsRender2.cpp).
				if (M.GetShortName().GetNumber() != 0) continue;
				if (M.NameAndInfo.GetField<EStatDataType>() != EStatDataType::ST_double) continue;
				Out.Emplace(M.GetDescription(), M.GetValue_double(EComplexStatField::IncAve));
			}
		}
		Out.Sort([](const TPair<FString, double>& A, const TPair<FString, double>& B) { return A.Value > B.Value; });
#endif
		return Out;
	}
}

// MARK: - Shots

const TArray<FAcBenchShot>& FAcBenchShot::All()
{
	using V = FAcBenchShot::EView;
	static const TArray<FAcBenchShot> Shots = [&]
	{
		auto Make = [](const TCHAR* Name, double Hour, V View, int32 Army = 29, bool bThird = false)
		{
			FAcBenchShot S;
			S.Name = Name;
			S.Hour = Hour;
			S.View = View;
			S.Army = Army;
			S.bThird = bThird;
			return S;
		};
		return TArray<FAcBenchShot>{
			Make(TEXT("fight"), 12, V::Fight),
			Make(TEXT("fight-night"), 23, V::Fight),
			Make(TEXT("base"), 12, V::Base),
			Make(TEXT("wide"), 12, V::Wide),
			Make(TEXT("pilot"), 12, V::Pilot),
			Make(TEXT("pilot-night"), 23, V::Pilot),
			Make(TEXT("third"), 12, V::Pilot, 29, true),
			Make(TEXT("army"), 12, V::Fight, 200),
			Make(TEXT("army-night"), 23, V::Fight, 200),
			Make(TEXT("late"), 12, V::Late, 0),
			// Unreal only: an 8-player late game (the runner stages it with
			// B1's -AcStage/-AcStageBuildings on an 8-player map).
			Make(TEXT("late8"), 12, V::Late, 0),
			// P2: the same game at the RTS camera's own zoom over Blue's main
			// (most of the 1,450 units off screen: culling's case).
			Make(TEXT("late8-base"), 12, V::Base, 0),
		};
	}();
	return Shots;
}

const FAcBenchShot* FAcBenchShot::Find(const FString& Name)
{
	return All().FindByPredicate([&](const FAcBenchShot& S) { return S.Name == Name; });
}

// MARK: - Staging (Bench.stageFight)

FAcBench::FFight FAcBench::StageFight(UAcSimSubsystem& Sim, const int32 Size)
{
	using ac::Vec2;
	ac::Simulation& S = Sim.Simulation();
	ac::GameState& State = S.state;
	const ac::MapDefinition& Map = Sim.Map();
	FFight Out;
	if (Size <= 0 || State.players.size() < 2) return Out;
	auto Home = [&](const size_t P) -> Vec2
	{
		const int64_t B = State.players[P].start ? *State.players[P].start : Map.starts[P % Map.starts.size()];
		return Map.bases[(size_t)B].center;
	};
	// Player 1 in Swift; with teams, player 0's first enemy.
	size_t FoeP = 1;
	for (size_t P = 1; P < State.players.size(); ++P)
	{
		if (State.hostile(0, (int64_t)P)) { FoeP = P; break; }
	}
	const Vec2 HomeP = Home(0), Foe = Home(FoeP);
	const Vec2 Way = ac::normalize(Foe - HomeP), Side(-Way.y, Way.x);
	const int32 N = (Size + 28) / 29, Row = Size > 29 ? 20 : 8;
	std::vector<ac::UnitKind> Army;
	const std::pair<ac::UnitKind, int32> Mix[] = {{ac::UnitKind::ranger, 16}, {ac::UnitKind::juggernaut, 4},
		{ac::UnitKind::firefly, 3}, {ac::UnitKind::comet, 2}, {ac::UnitKind::longbow, 2}, {ac::UnitKind::dropship, 2}};
	for (const auto& [Kind, Count] : Mix)
	{
		for (int32 I = 0; I < Count * N; ++I) Army.push_back(Kind);
	}
	Army.resize((size_t)Size);

	auto Places = [&](const Vec2 Middle, const int32 Owner)
	{
		const Vec2 Back = Owner == 0 ? -Way : Way;
		std::vector<Vec2> P;
		P.reserve(Army.size());
		for (int32 K = 0; K < (int32)Army.size(); ++K)
		{
			P.push_back(Middle + Back * (2.5 + double(K / Row) * 1.5) + Side * (double(K % Row) - double(Row - 1) / 2) * 1.3);
		}
		return P;
	};
	// Open ground: walkable, clear of buildings, ore and wells.
	auto Open = [&](const Vec2 P)
	{
		if (S.nav && !S.nav->walkable(P)) return false;
		for (const ac::Structure& X : State.structures)
		{
			if (ac::distance(X.position, P) < ac::Rules::radius(X.kind) + 1) return false;
		}
		for (const ac::OreDeposit& X : State.patches)
		{
			if (ac::distance(X.position, P) < 1.5) return false;
		}
		if (State.wells)
		{
			for (const ac::Well& X : *State.wells)
			{
				if (ac::distance(X.position, P) < 2.5) return false;
			}
		}
		return true;
	};
	// Between the mains (the middle half of the way, a little to either
	// side), the ground where most places are open; the nearest the halfway
	// point of equals.
	Vec2 Middle = (HomeP + Foe) / 2;
	double Best = -1e300;
	for (int32 Ti = 0; Ti <= 10; ++Ti)
	{
		const double T = 0.25 + Ti * 0.05;
		for (int32 Oi = 0; Oi <= 16; ++Oi)
		{
			const double O = -24.0 + Oi * 3.0;
			const Vec2 C = HomeP + (Foe - HomeP) * T + Side * O;
			int32 Room = 0;
			for (const int32 Owner : {0, 1})
			{
				for (const Vec2& P : Places(C, Owner)) Room += Open(P) ? 1 : 0;
			}
			const double Score = double(Room) * 10 - FMath::Abs(T - 0.5) * 10 - FMath::Abs(O) * 0.1;
			if (Score > Best)
			{
				Best = Score;
				Middle = C;
			}
		}
	}
	int32 Added = 0;
	for (const int32 Side01 : {0, 1})
	{
		const int64 Owner = Side01 == 0 ? 0 : (int64)FoeP;
		bool bAnchored = false;
		const double Facing = Side01 == 0 ? std::atan2(Way.y, Way.x) : std::atan2(-Way.y, -Way.x);
		const std::vector<Vec2> P = Places(Middle, Side01);
		for (size_t K = 0; K < P.size(); ++K)
		{
			if (!Open(P[K])) continue;
			const ac::UnitKind Kind = Army[K];
			ac::Unit U(State.nextID, Kind, Owner, P[K], Facing, ac::Unit::Task::idle);
			State.nextID += 1;
			if (Kind == ac::UnitKind::dropship)
			{
				U.energy = 120;
				U.cargo = std::vector<int64_t>{};
			}
			if (Kind == ac::UnitKind::longbow)
			{
				U.anchor = bAnchored ? 0.0 : 1.0;
				U.anchored = !bAnchored;
				U.aim = Facing;
				bAnchored = !bAnchored;
			}
			if (Side01 == 0 && Kind == ac::UnitKind::ranger && K >= 8 && Out.Driver < 0)
			{
				Out.Driver = U.id;
				U.hp = 10'000;
			}
			State.units.push_back(U);
			++Added;
		}
	}
	Sim.MarkEdited();
	Out.Middle = Middle;
	Out.Blue = Middle - Way * 4;
	Out.Red = Middle + Way * 4;
	UE_LOG(LogAutocraft, Log, TEXT("bench: staged %d units (%d a side asked) at %.1f,%.1f; driver #%lld"), Added, Size, Middle.x,
		Middle.y, (long long)Out.Driver);
	return Out;
}

// MARK: - The subsystem

bool UAcBenchSubsystem::IsBenchRun()
{
	FString Value;
	return FParse::Value(FCommandLine::Get(), TEXT("AcBench="), Value);
}

bool UAcBenchSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return IsBenchRun() && Super::ShouldCreateSubsystem(Outer);
}

bool UAcBenchSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game;
}

TStatId UAcBenchSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAcBenchSubsystem, STATGROUP_Tickables);
}

void UAcBenchSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const TCHAR* Cmd = FCommandLine::Get();
	FString Name;
	FParse::Value(Cmd, TEXT("AcBench="), Name);
	if (const FAcBenchShot* Found = FAcBenchShot::Find(Name))
	{
		Shot = *Found;
	}
	else
	{
		TArray<FString> Names;
		for (const FAcBenchShot& S : FAcBenchShot::All()) Names.Add(S.Name);
		Fail(FString::Printf(TEXT("no shot %s; shots: %s"), *Name, *FString::Join(Names, TEXT(", "))));
		return;
	}
	FParse::Value(Cmd, TEXT("AcBenchFrames="), Frames);
	FParse::Value(Cmd, TEXT("AcBenchWarmup="), Warmup);
	FParse::Value(Cmd, TEXT("AcBenchSettle="), Settle);
	FParse::Value(Cmd, TEXT("AcBenchScale="), Scale);
	FParse::Value(Cmd, TEXT("AcBenchOut="), OutPath);
	bGpuStats = FParse::Param(Cmd, TEXT("AcBenchGpu"));
	FString PngDir;
	if (FParse::Value(Cmd, TEXT("AcBenchPng="), PngDir))
	{
		if (FPaths::IsRelative(PngDir)) PngDir = FPaths::ConvertRelativePathToFull(FPaths::LaunchDir(), PngDir);
		PngPath = FPaths::Combine(PngDir, Shot.Name + TEXT(".png"));
		IFileManager::Get().MakeDirectory(*PngDir, true);
		IFileManager::Get().Delete(*PngPath, false, true, true);
	}
	Frames = FMath::Max(Frames, 1);
	Warmup = FMath::Max(Warmup, 0);
	Settle = FMath::Max(Settle, 0);
	Scale = FMath::Max(Scale, 0.25);
	float Held = -1;
	if (!FParse::Value(Cmd, TEXT("AcHour="), Held))
	{
		UE_LOG(LogAutocraft, Warning, TEXT("bench: no -AcHour; the shot's hour is %.0f"), Shot.Hour);
	}
	UE_LOG(LogAutocraft, Log, TEXT("bench: shot %s, %d frames after %d to warm up (%d paused to settle), scale %.0f"), *Shot.Name, Frames,
		Warmup, Settle, Scale);
}

void UAcBenchSubsystem::Fail(const FString& Why)
{
	UE_LOG(LogAutocraft, Error, TEXT("bench: %s"), *Why);
	Phase = EPhase::Done;
	FPlatformMisc::RequestExitWithStatus(false, 1, TEXT("AcBench"));
}

void UAcBenchSubsystem::Stage(UAcSimSubsystem& Sim)
{
	Fight = FAcBench::StageFight(Sim, Shot.Army);
	if (Shot.View == FAcBenchShot::EView::Pilot && Fight.Driver < 0) Fail(TEXT("no Ranger to drive"));
	// Held still while Unreal settles; the game moves on in the warm-up.
	Sim.SetPaused(true);
}

bool UAcBenchSubsystem::Aim()
{
	AAcRtsPawn* Pawn = AAcRtsPawn::Get(this);
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Pawn || !Pawn->View() || !Sim) return false;
	const ac::FreeView& View = *Pawn->View();
	const FVector2D Size = Pawn->ViewSize();
	if (Size.X <= 1) return false;
	// Swift's points are pixels / scale; ours are Slate's. The same cells
	// across: ppc × (our width / Swift's width).
	FVector2D Pixels(0, 0);
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(Pixels);
	const double SwiftWidth = Pixels.X > 1 ? Pixels.X / Scale : Size.X;
	const double Per = Size.X / SwiftWidth;
	const ac::GameState& State = Sim->State();
	const ac::MapDefinition& Map = Sim->Map();
	const int64_t Start = State.players[0].start ? *State.players[0].start : Map.starts[0];
	const ac::Vec2 Home = Map.bases[(size_t)Start].center;
	double Want = ac::FreeView::defaultPointsPerCell * Per;
	ac::Vec2 At = Fight.Middle;
	switch (Shot.View)
	{
	case FAcBenchShot::EView::Fight:
	case FAcBenchShot::EView::Pilot:
		break;
	case FAcBenchShot::EView::Base:
		At = Home;
		break;
	case FAcBenchShot::EView::Wide:
		Want = 0;
		break;
	case FAcBenchShot::EView::Late:
	{
		Want = 0;
		ac::Vec2 Sum(0, 0);
		int32 Count = 0;
		for (const ac::Unit& U : State.units)
		{
			if (U.owner != 0 || !U.soldier()) continue;
			Sum = Sum + U.position;
			++Count;
		}
		At = Count ? Sum / double(Count) : Home;
		break;
	}
	}
	// 0: the least zoom (the whole map), as `minPointsPerCell`.
	const double Target = Want > 0 ? Want : View.minPointsPerCell();
	if (View.pointsPerCell > 0 && FMath::Abs(View.pointsPerCell - Target) > 1e-6) Pawn->ZoomBy(Target / View.pointsPerCell);
	Pawn->CenterOn(At);
	return true;
}

void UAcBenchSubsystem::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	switch (Phase)
	{
	case EPhase::Load:
	{
		// The game loaded, the camera measured its view.
		const AAcRtsPawn* Pawn = AAcRtsPawn::Get(this);
		if (++LoadFrames > 3000) return Fail(TEXT("the game never came up"));
		if (!Sim || !Sim->IsRunning() || !Pawn || !Pawn->View() || Pawn->ViewSize().X <= 1 || LoadFrames < 3) return;
		Stage(*Sim);
		Phase = EPhase::Settle;
		InPhase = 0;
		return;
	}
	case EPhase::Settle:
	{
		++InPhase;
		if (Shot.View != FAcBenchShot::EView::Pilot) Aim();
		else if (InPhase == 1) Aim();
		else if (!bDriving && InPhase >= 3)
		{
			// The staged units have their instances now (a frame after staging).
			AAcPilotPawn* Pilot = AAcPilotPawn::Find(this);
			if (!Pilot || !Pilot->TakeOver(Fight.Driver)) return Fail(FString::Printf(TEXT("could not drive #%lld"), (long long)Fight.Driver));
			bDriving = true;
		}
		if (InPhase >= Settle && (Shot.View != FAcBenchShot::EView::Pilot || bDriving))
		{
			if (Sim) Sim->SetPaused(false);
			Phase = EPhase::Warmup;
			InPhase = 0;
		}
		return;
	}
	case EPhase::Warmup:
	{
		if (++InPhase < Warmup) return;
		// The driver's count comes a little late: read after a pause, as Swift's `settledCount`.
		FPlatformProcess::Sleep(0.1f);
		GpuStartNs = AcPerfSystem::AppGpuNanoseconds();
		LastWall = FPlatformTime::Seconds();
		Phase = EPhase::Timed;
		InPhase = 0;
		return;
	}
	case EPhase::Timed:
	{
		const double Now = FPlatformTime::Seconds();
		Wall.Add((Now - LastWall) * 1000.0);
		LastWall = Now;
		Game.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
		Render.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
		Rhi.Add(FPlatformTime::ToMilliseconds(GRHIThreadTime));
		Gpu.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)));
		const FAcPerf& Perf = FAcPerf::Get();
		Ticks.Add(Perf.LastTickMs());
		for (int32 I = 0; I < (int32)EAcTickPart::Count; ++I)
		{
			Parts[I] += Perf.LastTickParts()[I] / Frames;
			PartsMax[I] = FMath::Max(PartsMax[I], Perf.LastTickParts()[I]);
		}
		if (++InPhase < Frames) return;
		FPlatformProcess::Sleep(0.1f);
		GpuEndNs = AcPerfSystem::AppGpuNanoseconds();
		AcPerfSystem::Memory(FootprintMB, GraphicsMB, PeakMB);
		InPhase = 0;
		if (PngPath.IsEmpty())
		{
			Phase = EPhase::Count;
			return;
		}
		// The last frame as the game shows it (before the stats overlay).
		FScreenshotRequest::RequestScreenshot(PngPath, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		PngWaitStarted = FPlatformTime::Seconds();
		Phase = EPhase::Picture;
		return;
	}
	case EPhase::Picture:
	{
		const int64 Bytes = IFileManager::Get().FileSize(*PngPath);
		if ((Bytes > 0 && Bytes == LastPngSize) || FPlatformTime::Seconds() - PngWaitStarted > 30)
		{
			UE_LOG(LogAutocraft, Log, TEXT("bench: saved %s (%lld bytes)"), *PngPath, (long long)Bytes);
			Phase = EPhase::Count;
			return;
		}
		LastPngSize = Bytes;
		return;
	}
	case EPhase::Count:
		// The RHI counts draws only while its stats run: turned on after
		// the timed frames (and the picture), read some frames later.
		if (InPhase++ == 0)
		{
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
			{
				PC->ConsoleCommand(TEXT("stat rhi"));
				PC->ConsoleCommand(TEXT("stat scenerendering"));
				if (bGpuStats) PC->ConsoleCommand(TEXT("stat gpu"));
			}
		}
		if (bGpuStats && InPhase == 30)
		{
			// One frame's tree to the log; never the profiler window.
			if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ProfileGPU.ShowUI"))) V->Set(false);
			if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ProfileGPU.UnicodeOutput"))) V->Set(false);
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) PC->ConsoleCommand(TEXT("ProfileGPU"));
		}
		Draws = FMath::Max3(Draws, GNumDrawCallsRHI[0], (int32)StatCounter(TEXT("STAT_RHIDraws")));
		Primitives = FMath::Max3(Primitives, GNumPrimitivesDrawnRHI[0], (int32)StatCounter(TEXT("STAT_RHIPrimitives")));
		MeshDraws = FMath::Max(MeshDraws, (int32)StatCounter(TEXT("STAT_MeshDrawCalls")));
		if (InPhase < (bGpuStats ? 70 : 40)) return;
		if (bGpuStats) GpuPasses = GpuStatPasses();
		Record();
		Finish();
		return;
	default:
		return;
	}
}

void UAcBenchSubsystem::Record()
{
	auto P = [](TArray<double> V, const double Q)
	{
		V.Sort();
		return FAcPerf::Percentile(V, Q);
	};
	auto Max = [](const TArray<double>& V)
	{
		double M = 0;
		for (const double X : V) M = FMath::Max(M, X);
		return M;
	};
	auto Mean = [](const TArray<double>& V)
	{
		double S = 0;
		for (const double X : V) S += X;
		return V.Num() ? S / V.Num() : 0.0;
	};
	// The driver's count over the timed frames; without it, the GPU's frame time p50.
	const double GpuBusy = GpuStartNs >= 0 && GpuEndNs >= GpuStartNs ? (GpuEndNs - GpuStartNs) / 1e6 / Frames : P(Gpu, 0.5);
	int32 Lights = 0, Lit = 0;
	for (TObjectIterator<ULocalLightComponent> It; It; ++It)
	{
		const ULocalLightComponent* L = *It;
		if (L->GetWorld() != GetWorld() || !L->IsRegistered() || !L->IsVisible()) continue;
		++Lights;
		if (L->Intensity > 0) ++Lit;
	}
	const UAcWorldRenderer* Renderer = UAcWorldRenderer::Get(this);
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	FVector2D Pixels(0, 0);
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(Pixels);

	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetStringField(TEXT("shot"), Shot.Name);
	J->SetNumberField(TEXT("frames"), Frames);
	J->SetNumberField(TEXT("width"), Pixels.X);
	J->SetNumberField(TEXT("height"), Pixels.Y);
	J->SetNumberField(TEXT("gpuBusy"), GpuBusy);
	J->SetNumberField(TEXT("gpuWallP50"), P(Gpu, 0.5));
	J->SetNumberField(TEXT("gpuWallP95"), P(Gpu, 0.95));
	J->SetNumberField(TEXT("gpuWallMax"), Max(Gpu));
	J->SetNumberField(TEXT("frameP50"), P(Wall, 0.5));
	J->SetNumberField(TEXT("frameP95"), P(Wall, 0.95));
	J->SetNumberField(TEXT("frameMax"), Max(Wall));
	J->SetNumberField(TEXT("fps"), 1000.0 / FMath::Max(Mean(Wall), 1e-3));
	J->SetNumberField(TEXT("gameP50"), P(Game, 0.5));
	J->SetNumberField(TEXT("gameP95"), P(Game, 0.95));
	J->SetNumberField(TEXT("renderP50"), P(Render, 0.5));
	J->SetNumberField(TEXT("renderP95"), P(Render, 0.95));
	J->SetNumberField(TEXT("rhiP50"), P(Rhi, 0.5));
	J->SetNumberField(TEXT("rhiP95"), P(Rhi, 0.95));
	J->SetNumberField(TEXT("tickP50"), P(Ticks, 0.5));
	J->SetNumberField(TEXT("tickP95"), P(Ticks, 0.95));
	J->SetNumberField(TEXT("tickMax"), Max(Ticks));
	J->SetNumberField(TEXT("footprintMB"), FootprintMB);
	J->SetNumberField(TEXT("graphicsMB"), GraphicsMB);
	J->SetNumberField(TEXT("peakMB"), PeakMB);
	J->SetNumberField(TEXT("draws"), Draws);
	J->SetNumberField(TEXT("triangles"), Primitives);
	J->SetNumberField(TEXT("meshDraws"), MeshDraws);
	J->SetNumberField(TEXT("objects"), Renderer ? Renderer->LastStats().Objects : 0);
	J->SetNumberField(TEXT("instances"), Renderer ? Renderer->LastStats().Instances : 0);
	J->SetNumberField(TEXT("units"), Sim && Sim->IsRunning() ? (double)Sim->State().units.size() : 0);
	J->SetNumberField(TEXT("structures"), Sim && Sim->IsRunning() ? (double)Sim->State().structures.size() : 0);
	J->SetNumberField(TEXT("players"), Sim && Sim->IsRunning() ? (double)Sim->State().players.size() : 0);
	J->SetNumberField(TEXT("lights"), Lights);
	J->SetNumberField(TEXT("litLights"), Lit);
	J->SetNumberField(TEXT("load"), AcPerfSystem::LoadAverage());
	TArray<TSharedPtr<FJsonValue>> PartRows;
	for (int32 I = 0; I < (int32)EAcTickPart::Count; ++I)
	{
		TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("name"), FAcPerf::PartName((EAcTickPart)I));
		Row->SetNumberField(TEXT("ms"), Parts[I]);
		Row->SetNumberField(TEXT("max"), PartsMax[I]);
		PartRows.Add(MakeShared<FJsonValueObject>(Row));
	}
	J->SetArrayField(TEXT("parts"), PartRows);
	if (bGpuStats)
	{
		TSharedRef<FJsonObject> Passes = MakeShared<FJsonObject>();
		for (const TPair<FString, double>& Pass : GpuPasses) Passes->SetNumberField(Pass.Key, Pass.Value);
		J->SetObjectField(TEXT("gpuPasses"), Passes);
	}
	FString Text;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
	FJsonSerializer::Serialize(J, Writer);
	UE_LOG(LogAutocraft, Display, TEXT("bench-result %s"), *Text);
	if (!OutPath.IsEmpty()) FFileHelper::SaveStringToFile(Text, *OutPath);
	if (Shot.View == FAcBenchShot::EView::Pilot)
	{
		const AAcPilotPawn* Pilot = AAcPilotPawn::Find(this);
		if (!Pilot || !Pilot->Driving()) UE_LOG(LogAutocraft, Error, TEXT("bench: the driven unit was lost"));
	}
}

void UAcBenchSubsystem::Finish()
{
	Phase = EPhase::Done;
	FPlatformMisc::RequestExit(false, TEXT("AcBench"));
}
