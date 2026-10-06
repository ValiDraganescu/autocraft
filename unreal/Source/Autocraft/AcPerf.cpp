#include "AcPerf.h"

#include "AcHUD.h"
#include "AcLog.h"
#include "AcPerfSystem.h"
#include "AcSimSubsystem.h"
#include "SAcPerfPanel.h"
#include "SAcResourceBar.h"
#include "SAcRoot.h"

#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RHIStats.h"
#include "RenderTimer.h"
#include "UnrealClient.h"

static TAutoConsoleVariable<int32> CVarAcPerfPanel(
	TEXT("ac.PerfPanel"), 1,
	TEXT("Show the perf panel under the resource bar (⌘D toggles; PerfPanel.swift)."));
static TAutoConsoleVariable<int32> CVarAcPerfTargetFps(
	TEXT("ac.PerfTargetFps"), 0,
	TEXT("The FPS the perf line and panel measure against. 0: t.MaxFPS, else the fixed frame rate, else the display's."));

static FAutoConsoleCommand GAcTogglePerf(
	TEXT("ac.TogglePerf"), TEXT("Show or hide the perf panel (⌘D)."),
	FConsoleCommandDelegate::CreateLambda([] { UAcPerfSubsystem::SetPanelShown(CVarAcPerfPanel.GetValueOnGameThread() == 0); }));

// MARK: - FAcPerf

FAcPerf& FAcPerf::Get()
{
	static FAcPerf Perf;
	return Perf;
}

FAcPerf::FAcPerf()
{
	LastWall = FPlatformTime::Seconds();
	LastCpu = AcPerfSystem::CpuSeconds();
	LastGpuNs = AcPerfSystem::AppGpuNanoseconds();
}

const TCHAR* FAcPerf::PartName(const EAcTickPart Part)
{
	static const TCHAR* const Names[] = {TEXT("sim"), TEXT("events"), TEXT("scene"), TEXT("hud"), TEXT("audio"), TEXT("save")};
	static_assert(UE_ARRAY_COUNT(Names) == (int32)EAcTickPart::Count);
	return Names[FMath::Clamp((int32)Part, 0, (int32)EAcTickPart::Count - 1)];
}

const TCHAR* FAcPerf::TraceName(const EAcTickPart Part)
{
	static const TCHAR* const Names[] = {TEXT("Ac.Sim"), TEXT("Ac.Events"), TEXT("Ac.Scene"), TEXT("Ac.Hud"), TEXT("Ac.Audio"), TEXT("Ac.Save")};
	static_assert(UE_ARRAY_COUNT(Names) == (int32)EAcTickPart::Count);
	return Names[FMath::Clamp((int32)Part, 0, (int32)EAcTickPart::Count - 1)];
}

void FAcPerf::StartTick()
{
	TickStarted = FPlatformTime::Seconds();
	bInTick = true;
}

void FAcPerf::Add(const EAcTickPart Part, const double Ms)
{
	TickParts[(int32)Part] += Ms;
}

void FAcPerf::EndTick()
{
	if (!bInTick) return;
	bInTick = false;
	LastTick = (FPlatformTime::Seconds() - TickStarted) * 1000.0;
	Ticks.Add(LastTick);
	for (int32 I = 0; I < (int32)EAcTickPart::Count; ++I)
	{
		LastParts[I] = TickParts[I];
		LogParts[I].Ms += TickParts[I];
		LogParts[I].Max = FMath::Max(LogParts[I].Max, TickParts[I]);
		TickParts[I] = 0;
	}
}

void FAcPerf::FThreadTimes::Add(const double (&Ms)[4])
{
	for (int32 I = 0; I < 4; ++I)
	{
		Sum[I] += Ms[I];
		Max[I] = FMath::Max(Max[I], Ms[I]);
	}
	Frames += 1;
}

void FAcPerf::Frame(const double Now)
{
	Stamps.Add(Now);
	// Keep a little over a second.
	int32 Old = 0;
	while (Old < Stamps.Num() && Now - Stamps[Old] > 1.5) ++Old;
	if (Old > 0) Stamps.RemoveAt(0, Old, EAllowShrinking::No);
	FrameCount += 1;
	// The engine's own: the last frame's game, render and RHI thread time
	// and the GPU's (Metal command buffers' time).
	const double Ms[4] = {
		FPlatformTime::ToMilliseconds(GGameThreadTime),
		FPlatformTime::ToMilliseconds(GRenderThreadTime),
		FPlatformTime::ToMilliseconds(GRHIThreadTime),
		FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)),
	};
	// A reading over 10 s is not a frame (the counters start from garbage
	// while the first frames load; 32-bit cycles wrap at 179 s).
	if (Ms[0] < 10'000 && Ms[1] < 10'000 && Ms[2] < 10'000 && Ms[3] < 10'000)
	{
		SecondThreads.Add(Ms);
		LogThreads.Add(Ms);
	}
}

double FAcPerf::Percentile(const TArray<double>& Sorted, const double Q)
{
	if (Sorted.IsEmpty()) return 0;
	return Sorted[FMath::Min((int32)FMath::RoundToDouble((Sorted.Num() - 1) * Q), Sorted.Num() - 1)];
}

FAcPerfSample FAcPerf::Sample(const double Now, const FString& ViewName, const FString& ViewPixels, const int32 TargetFps)
{
	const double Wall = FMath::Max(Now - LastWall, 1e-3);
	LastWall = Now;
	FAcPerfSample S;
	S.TargetFps = TargetFps;
	S.ViewName = ViewName;
	S.ViewPixels = ViewPixels;

	TArray<double> Intervals;
	int32 Recent = 0;
	for (int32 I = 0; I < Stamps.Num(); ++I)
	{
		if (Now - Stamps[I] > 1.0) continue;
		Recent += 1;
		if (I > 0 && Now - Stamps[I - 1] <= 1.0) Intervals.Add((Stamps[I] - Stamps[I - 1]) * 1000.0);
	}
	Intervals.Sort();
	S.Fps = Recent;
	S.FrameP50 = Intervals.IsEmpty() ? 0 : Intervals[Intervals.Num() / 2];
	S.FrameMax = Intervals.IsEmpty() ? 0 : Intervals.Last();

	const double Cpu = AcPerfSystem::CpuSeconds();
	S.Cpu = (Cpu - LastCpu) / Wall * 100.0;
	LastCpu = Cpu;

	const int64 Frames = FMath::Max<int64>(FrameCount - LastFrameCount, 0);
	LastFrameCount = FrameCount;
	S.GameThreadMs = SecondThreads.Mean(0);
	S.RenderThreadMs = SecondThreads.Mean(1);
	S.RhiThreadMs = SecondThreads.Mean(2);
	const double GpuNs = AcPerfSystem::AppGpuNanoseconds();
	double AppGpuFrameMs = -1;
	if (GpuNs >= 0)
	{
		if (LastGpuNs >= 0 && GpuNs >= LastGpuNs)
		{
			const double D = GpuNs - LastGpuNs;
			S.GpuApp = FMath::Min(D / 1e9 / Wall * 100.0, 100.0);
			if (Frames > 0) AppGpuFrameMs = D / 1e6 / Frames;
			LogGpuNs += D;
		}
		LastGpuNs = GpuNs;
	}
	// Unreal's GPU frame time where the RHI has it, else the driver's
	// per-process accounting spread over the frames (as Swift).
	S.GpuFrameMs = SecondThreads.Mean(3) > 0 ? SecondThreads.Mean(3) : AppGpuFrameMs;
	S.GpuChip = AcPerfSystem::ChipGpuUtilization();
	SecondThreads = FThreadTimes();

	TArray<double> Sorted = Ticks;
	Sorted.Sort();
	double Sum = 0;
	for (const double T : Ticks) Sum += T;
	S.TickMs = Ticks.IsEmpty() ? 0 : Sum / Ticks.Num();
	S.TickP95 = Percentile(Sorted, 0.95);
	S.TickMax = Sorted.IsEmpty() ? 0 : Sorted.Last();
	LogTicks.Append(Ticks);
	Ticks.Reset();

	double Peak = 0;
	AcPerfSystem::Memory(S.MemoryMB, S.GraphicsMB, Peak);
	S.DrawCalls = GNumDrawCallsRHI[0];
	S.Primitives = GNumPrimitivesDrawnRHI[0];

	LogFrames += Frames;
	LogWall += Wall;
	LogLowFps = FMath::Min(LogLowFps, S.Fps);

	bGpuIsApp = S.GpuApp >= 0;
	const double Budget = 1000.0 / FMath::Max(TargetFps, 1);
	Fps.Add(S.Fps);
	Gpu.Add(bGpuIsApp ? S.GpuApp : (S.GpuFrameMs > 0 ? S.GpuFrameMs / Budget * 100.0 : 0.0));
	if (Fps.Num() > HistoryLength) Fps.RemoveAt(0);
	if (Gpu.Num() > HistoryLength) Gpu.RemoveAt(0);
	Last = S;
	return S;
}

FString FAcPerf::Summary(const FAcPerfSample& S, const double GameTime)
{
	TArray<double> T = LogTicks;
	T.Sort();
	const double Count = FMath::Max(T.Num(), 1);
	const double Budget = 1000.0 / FMath::Max(S.TargetFps, 1);
	int32 Slow = 0;
	for (const double X : T) Slow += X > Budget ? 1 : 0;
	const double MeanFps = LogWall > 0 ? LogFrames / LogWall : S.Fps;
	const double LowFps = LogLowFps < TNumericLimits<double>::Max() ? LogLowFps : S.Fps;
	const double GpuFrame = LogThreads.Mean(3) > 0 ? LogThreads.Mean(3) : (LogFrames > 0 && LogGpuNs > 0 ? LogGpuNs / 1e6 / LogFrames : -1);
	const FString GpuText = GpuFrame >= 0 ? FString::Printf(TEXT("%.1f"), GpuFrame) : TEXT("n/a");
	const FString Speed = LogGameTime >= 0 && GameTime >= LogGameTime && LogWall > 0
		? FString::Printf(TEXT("%.2f"), (GameTime - LogGameTime) / LogWall) : TEXT("n/a");
	FString Parts;
	for (int32 I = 0; I < (int32)EAcTickPart::Count; ++I)
	{
		if (I) Parts += TEXT(" ");
		Parts += FString::Printf(TEXT("%s %.1f/%.0f"), PartName((EAcTickPart)I), LogParts[I].Ms / Count, LogParts[I].Max);
	}
	double Footprint = 0, Graphics = 0, Peak = 0;
	AcPerfSystem::Memory(Footprint, Graphics, Peak);
	const FString Line = FString::Printf(
		TEXT("perf: fps %.0f/%d (low %.0f) frame p50 %.1f max %.1f ms | gpu %s ms/frame, app %.0f%% chip %.0f%%")
		TEXT(" | threads game %.1f/%.0f render %.1f/%.0f rhi %.1f/%.0f gpu %.1f/%.0f ms")
		TEXT(" | tick p50 %.2f p95 %.2f max %.1f ms | speed %s, %.0f ticks/s, %d slow | %s ms")
		TEXT(" | cpu %.0f%%, load %.1f | mem %.0f MB, graphics %.0f, peak %.0f | %s %s %.0f"),
		MeanFps, S.TargetFps, LowFps, S.FrameP50, S.FrameMax, *GpuText, S.GpuApp, S.GpuChip,
		LogThreads.Mean(0), LogThreads.Max[0], LogThreads.Mean(1), LogThreads.Max[1], LogThreads.Mean(2), LogThreads.Max[2],
		LogThreads.Mean(3), LogThreads.Max[3],
		Percentile(T, 0.5), Percentile(T, 0.95), T.IsEmpty() ? 0.0 : T.Last(), *Speed, LogWall > 0 ? T.Num() / LogWall : 0.0, Slow,
		*Parts, S.Cpu, AcPerfSystem::LoadAverage(), Footprint, Graphics, Peak, *S.ViewName, *S.ViewPixels, S.Fps);
	LogFrames = 0;
	LogWall = 0;
	LogGpuNs = 0;
	LogLowFps = TNumericLimits<double>::Max();
	LogTicks.Reset();
	for (FPart& P : LogParts) P = FPart();
	LogThreads = FThreadTimes();
	LogGameTime = GameTime;
	return Line;
}

FString FAcPerf::MachineSummary()
{
	return AcPerfSystem::Machine();
}

// MARK: - Scopes

FAcPerfScope::FAcPerfScope(const EAcTickPart InPart)
	: Part(InPart), Start(FPlatformTime::Seconds()), Activity(FAcPerf::PartName(InPart))
{
}

FAcPerfScope::~FAcPerfScope()
{
	FAcPerf::Get().Add(Part, (FPlatformTime::Seconds() - Start) * 1000.0);
}

// MARK: - UAcPerfSubsystem

namespace
{
	/// ⌘D: the perf panel (`GameController.togglePerf`).
	class FAcPerfKeys final : public IInputProcessor
	{
	public:
		virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
		virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
		{
			if (Event.GetKey() != EKeys::D || !Event.IsCommandDown() || Event.IsRepeat()) return false;
			UAcPerfSubsystem::SetPanelShown(CVarAcPerfPanel.GetValueOnGameThread() == 0);
			return true;
		}
		virtual const TCHAR* GetDebugName() const override { return TEXT("AcPerfKeys"); }
	};
}

void UAcPerfSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	AcWatchdog::Start();
	static bool bLoggedMachine = false;
	if (!bLoggedMachine)
	{
		bLoggedMachine = true;
		UE_LOG(LogAutocraft, Log, TEXT("machine: %s, RHI %s"), *FAcPerf::MachineSummary(), GDynamicRHI ? GDynamicRHI->GetName() : TEXT("none"));
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("AcNoPerfPanel"))) CVarAcPerfPanel->Set(0, ECVF_SetByCommandline);
	// -AcPerfFor counts from the first game world (a map load makes another).
	static const double FirstStart = FPlatformTime::Seconds();
	static bool bLoggedFor = false;
	Started = FirstStart;
	NextSample = FPlatformTime::Seconds() + 1.0;
	SamplesToWarm = 2;
	WarmSamples = 0;
	double For = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("AcPerfFor="), For) && For > 0)
	{
		QuitAt = Started + For;
		if (!bLoggedFor) UE_LOG(LogAutocraft, Log, TEXT("perf: running %.0f s, then quitting (-AcPerfFor)"), For);
		bLoggedFor = true;
	}
	if (FSlateApplication::IsInitialized())
	{
		Keys = MakeShared<FAcPerfKeys>();
		FSlateApplication::Get().RegisterInputPreProcessor(Keys);
	}
}

void UAcPerfSubsystem::Deinitialize()
{
	if (Keys && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(Keys);
	Keys.Reset();
	if (Panel)
	{
		if (const AAcHUD* Hud = AAcHUD::Get(GetWorld()))
		{
			if (const TSharedPtr<SAcRoot> Root = Hud->Root()) Root->RemoveLayer(Panel.ToSharedRef());
		}
		Panel.Reset();
	}
	Super::Deinitialize();
}

bool UAcPerfSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UAcPerfSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAcPerfSubsystem, STATGROUP_Tickables);
}

void UAcPerfSubsystem::SetPanelShown(const bool bShown)
{
	CVarAcPerfPanel->Set(bShown ? 1 : 0, ECVF_SetByConsole);
	UE_LOG(LogAutocraft, Log, TEXT("perf: panel %s"), bShown ? TEXT("on") : TEXT("off"));
}

void UAcPerfSubsystem::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);
	TRACE_CPUPROFILER_EVENT_SCOPE(Ac.Perf);
	const double Now = FPlatformTime::Seconds();
	FAcPerf::Get().Frame(Now);
	if (!Panel) AttachPanel();
	if (Now >= NextSample)
	{
		NextSample = FMath::Max(NextSample + 1.0, Now + 0.5);
		TakeSample(Now);
	}
	if (QuitAt > 0 && Now >= QuitAt)
	{
		QuitAt = 0;
		UE_LOG(LogAutocraft, Log, TEXT("perf: %.0f s done, %d stalls, quitting (-AcPerfFor)"), Now - Started, AcWatchdog::StallCount());
		FPlatformMisc::RequestExit(false, TEXT("AcPerfFor"));
	}
}

void UAcPerfSubsystem::TakeSample(const double Now)
{
	FAcPerf& Perf = FAcPerf::Get();
	const FAcPerfSample S = Perf.Sample(Now, TEXT("window"), ViewPixels(), TargetFps());
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	const double GameTime = Sim && Sim->IsRunning() ? Sim->GameTime() : -1.0;
	if (SamplesToWarm > 0)
	{
		// The first seconds of a world carry its loading (multi-second
		// frames, the render thread's backlog): start the line after them.
		--SamplesToWarm;
		// Still loading (a frame over 250 ms): wait for two calm seconds, up to 20.
		if (S.FrameMax > 250.0 && ++WarmSamples < 20) SamplesToWarm = 2;
		Perf.Summary(S, GameTime);
		return;
	}
	if (--SamplesToLog <= 0)
	{
		SamplesToLog = 10;
		UE_LOG(LogAutocraft, Log, TEXT("%s"), *Perf.Summary(S, GameTime));
	}
}

void UAcPerfSubsystem::AttachPanel()
{
	const AAcHUD* Hud = AAcHUD::Get(GetWorld());
	const TSharedPtr<SAcRoot> Root = Hud ? Hud->Root() : nullptr;
	if (!Root) return;
	// HUD.attachPerf: under the resource bar, 6 points down, as wide.
	Root->AddLayer(AcHudLayer::Bar)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		.Padding(0, 10 + SAcResourceBar::Height + 6, 14, 0)
	[
		SAssignNew(Panel, SAcPerfPanel)
		.Visibility_Lambda([] { return CVarAcPerfPanel.GetValueOnGameThread() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
	];
}

FString UAcPerfSubsystem::ViewPixels() const
{
	const UGameViewportClient* Client = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	const FViewport* Viewport = Client ? Client->Viewport : nullptr;
	if (!Viewport) return TEXT("none");
	const FIntPoint Size = Viewport->GetSizeXY();
	return FString::Printf(TEXT("%dx%d"), Size.X, Size.Y);
}

int32 UAcPerfSubsystem::TargetFps() const
{
	if (const int32 Ours = CVarAcPerfTargetFps.GetValueOnGameThread(); Ours > 0) return Ours;
	if (const IConsoleVariable* Max = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
	{
		if (const float F = Max->GetFloat(); F > 0) return FMath::RoundToInt(F);
	}
	if (GEngine && GEngine->bUseFixedFrameRate && GEngine->FixedFrameRate > 0) return FMath::RoundToInt(GEngine->FixedFrameRate);
	return FMath::Max(30, FPlatformMisc::GetMaxRefreshRate());
}
