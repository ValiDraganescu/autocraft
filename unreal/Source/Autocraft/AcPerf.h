// `FAcPerf`: the perf monitor (GAME-LAYER.md §2.14 "Perf monitor", chunk
// D10), the Unreal side of `PerfMonitor.swift`.
//
// - **Tick laps.** The game layer's work per frame (`UAcSimSubsystem::Tick`,
//   its listeners included) is a *tick*, split into the Swift parts: sim,
//   events, scene, hud, audio, save. Time a part with `AC_PERF_SCOPE(Part)`:
//   it adds its duration to the part, opens an Unreal Insights scope
//   (`Ac.Sim`, `Ac.Scene`...) and labels the watchdog's activity. The sim
//   subsystem wraps its stages: Renderer → scene, Effects and Other →
//   events, Audio → audio, Hud → hud.
// - **Unreal's frame.** Every frame it also reads the engine's own times:
//   game thread, render thread, RHI thread and GPU (`GGameThreadTime`,
//   `GRenderThreadTime`, `GRHIThreadTime`, `RHIGetGPUFrameCycles`), draw
//   calls and primitives.
// - **Samples** once a second (FPS of the last second, frame p50 and max,
//   GPU, CPU, memory, ticks), five minutes of FPS and GPU for the panel, and
//   every 10 s the `perf:` line in `~/Library/Logs/Autocraft/unreal.log` in
//   the Swift line's shape, with the Unreal threads added after the GPU:
//     perf: fps 60/60 (low 58) frame p50 16.7 max 18.0 ms | gpu 6.1 ms/frame,
//     app 37% chip 40% | threads game 3.2/5.0 render 4.1/6.3 rhi 2.0/3.1
//     gpu 6.1/7.9 ms | tick p50 0.90 p95 1.40 max 3.1 ms | speed 1.00,
//     60 ticks/s, 0 slow | sim 0.2/1 events 0.0/0 scene 0.6/2 hud 0.1/0
//     audio 0.0/0 save 0.0/0 ms | cpu 140%, load 3.2 | mem 2400 MB,
//     graphics 900, peak 2500 | window 5120x1378 60
//   (one line; mean/max per tick for the parts and the threads).
//
// `UAcPerfSubsystem` drives it in game worlds: samples, logs, starts the
// stall watchdog (`AcWatchdog.h`) and hangs `SAcPerfPanel` under the
// resource bar once the HUD is up. Console: `ac.PerfPanel 0|1`,
// `ac.TogglePerf` (also ⌘D), `ac.PerfTargetFps N`. Command line:
// `-AcNoPerfPanel`, `-AcPerfFor=SECONDS` (log, then quit after that many
// real seconds: the headless perf run).
#pragma once

#include "CoreMinimal.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Subsystems/WorldSubsystem.h"

#include "AcWatchdog.h"

#include "AcPerf.generated.h"

class SAcPerfPanel;
class IInputProcessor;

/// The parts of a tick (`TickPart` in PerfMonitor.swift), in its order.
enum class EAcTickPart : uint8
{
	/// The simulation's steps (and, later, the camera and the pilot).
	Sim,
	/// The step's events: the fog gate, effects, log lines.
	Events,
	/// The world brought up to the state: units, buildings, bars.
	Scene,
	/// The HUD.
	Hud,
	/// Sound.
	Audio,
	/// The session saved (every 15 s).
	Save,
	Count
};

/// A reading of the last second (`PerfSample`).
struct FAcPerfSample
{
	/// Frames in the last second, and the target.
	double Fps = 0;
	int32 TargetFps = 60;
	/// Frame interval over the last second, ms.
	double FrameP50 = 0, FrameMax = 0;
	/// GPU time per frame (Unreal's GPU frame time), ms; < 0: none.
	double GpuFrameMs = -1;
	/// Share of the GPU this process used (0…100), and the whole chip's; < 0: unknown.
	double GpuApp = -1, GpuChip = -1;
	/// Unreal's threads per frame, mean of the second, ms.
	double GameThreadMs = 0, RenderThreadMs = 0, RhiThreadMs = 0;
	/// This process's CPU (100 = one core).
	double Cpu = 0;
	/// Memory footprint and its graphics part, MB.
	double MemoryMB = 0, GraphicsMB = 0;
	/// Ticks of the last second: mean, 95th percentile, worst, ms.
	double TickMs = 0, TickP95 = 0, TickMax = 0;
	/// Draw calls and primitives of a recent frame.
	int32 DrawCalls = 0, Primitives = 0;
	/// "window", its size in pixels ("5120x1378").
	FString ViewName, ViewPixels;
};

class AUTOCRAFT_API FAcPerf
{
public:
	static FAcPerf& Get();

	/// Samples kept for the graphs: five minutes at one a second.
	static constexpr int32 HistoryLength = 300;

	/// "sim", "events"...
	static const TCHAR* PartName(EAcTickPart Part);
	/// The Insights scope name: "Ac.Sim"...
	static const TCHAR* TraceName(EAcTickPart Part);

	// MARK: - The tick (game thread)

	void StartTick();
	void EndTick();
	/// `Ms` more for `Part` in the tick under way.
	void Add(EAcTickPart Part, double Ms);
	/// The last tick's parts, ms (the bench's).
	const double* LastTickParts() const { return LastParts; }
	/// The last whole tick, ms (the bench's).
	double LastTickMs() const { return LastTick; }

	// MARK: - Frames and samples (game thread)

	/// One engine frame ended at `Now` (seconds): reads the engine's times.
	void Frame(double Now);
	/// The reading of the last second; pushes the graph histories.
	FAcPerfSample Sample(double Now, const FString& ViewName, const FString& ViewPixels, int32 TargetFps);
	/// The `perf:` line of everything since the last one; starts the next.
	/// `GameTime`: the game's clock (< 0: none, no speed).
	FString Summary(const FAcPerfSample& Last, double GameTime);

	const FAcPerfSample& LastSample() const { return Last; }
	const TArray<double>& FpsHistory() const { return Fps; }
	const TArray<double>& GpuHistory() const { return Gpu; }
	/// True when the GPU graph is the app's share (else GPU ms as % of the frame budget).
	bool GpuIsAppShare() const { return bGpuIsApp; }

	/// The value `Q` (0…1) of the way up sorted `Values` (0 when empty).
	static double Percentile(const TArray<double>& Sorted, double Q);

	/// "Apple M4 Max (Mac16,5), GPU 40 cores, CPU 12P+4E, 128 GB, macOS 26.3".
	static FString MachineSummary();

private:
	FAcPerf();

	struct FPart
	{
		double Ms = 0, Max = 0;
	};
	/// Per-frame engine readings, mean and worst.
	struct FThreadTimes
	{
		double Sum[4] = {0, 0, 0, 0};
		double Max[4] = {0, 0, 0, 0};
		int32 Frames = 0;
		void Add(const double (&Ms)[4]);
		double Mean(int32 I) const { return Frames ? Sum[I] / Frames : 0; }
	};

	// The tick under way.
	double TickStarted = 0;
	bool bInTick = false;
	double TickParts[(int32)EAcTickPart::Count] = {};
	double LastParts[(int32)EAcTickPart::Count] = {};
	double LastTick = 0;

	// The last second.
	TArray<double> Stamps;
	TArray<double> Ticks;
	FThreadTimes SecondThreads;
	double LastWall = 0;
	double LastCpu = 0;
	int64 LastFrameCount = 0;
	int64 FrameCount = 0;
	double LastGpuNs = -1;

	// Since the last summary.
	int64 LogFrames = 0;
	double LogWall = 0, LogLowFps = TNumericLimits<double>::Max(), LogGpuNs = 0;
	TArray<double> LogTicks;
	FPart LogParts[(int32)EAcTickPart::Count];
	FThreadTimes LogThreads;
	double LogGameTime = -1;

	FAcPerfSample Last;
	TArray<double> Fps, Gpu;
	bool bGpuIsApp = false;
};

/// Times a part of the tick for the perf line, Insights and the watchdog.
struct AUTOCRAFT_API FAcPerfScope
{
	explicit FAcPerfScope(EAcTickPart InPart);
	~FAcPerfScope();
	FAcPerfScope(const FAcPerfScope&) = delete;
	FAcPerfScope& operator=(const FAcPerfScope&) = delete;

private:
	EAcTickPart Part;
	double Start;
	FAcActivity Activity;
};

/// The whole tick (`startTick` … `endTick`).
struct AUTOCRAFT_API FAcPerfTick
{
	FAcPerfTick() { FAcPerf::Get().StartTick(); }
	~FAcPerfTick() { FAcPerf::Get().EndTick(); }
};

/// Time the rest of the enclosing block as `Part` (an `EAcTickPart`).
#define AC_PERF_SCOPE(Part) \
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(FAcPerf::TraceName(Part)); \
	FAcPerfScope UE_JOIN(AcPerfScope_, __LINE__)(Part)

/// Drives `FAcPerf` in a game world: the per-frame readings, the samples,
/// the perf line, the watchdog and the panel.
UCLASS()
class AUTOCRAFT_API UAcPerfSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }

	/// Show or hide the panel (`ac.PerfPanel`).
	static void SetPanelShown(bool bShown);

private:
	void AttachPanel();
	void TakeSample(double Now);
	FString ViewPixels() const;
	int32 TargetFps() const;

	TSharedPtr<SAcPerfPanel> Panel;
	TSharedPtr<IInputProcessor> Keys;
	double NextSample = 0;
	int32 SamplesToLog = 10;
	/// Samples left before the first line counts (the world's loading).
	int32 SamplesToWarm = 2;
	int32 WarmSamples = 0;
	double QuitAt = 0;
	double Started = 0;
};
