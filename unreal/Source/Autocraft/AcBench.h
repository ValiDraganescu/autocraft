// `-AcBench=SHOT` (chunk F3): the Unreal side of `Sources/Autocraft/Bench.swift`.
// One busy moment of a real game, rendered at a fixed size from one camera
// (a "shot"), timing every frame: the GPU's time on it, Unreal's game,
// render and RHI threads, and the game's tick (`UAcSimSubsystem::Tick` and
// its frame listeners, split into the Swift tick parts, AcPerf.h). One shot
// a process, as Swift's children; `unreal/Tools/bench/bench.py` runs them
// all and prints the table.
//
// The moment is `bench/badlands-large.json` (`-AcSession=`), with two armies
// staged face to face between the mains (`FAcBench::StageFight` =
// `Bench.stageFight`, with the driver Swift's pilot shots take over).
//
// Shots (as `Bench.shots`; hour, view, army a side):
//   fight 12 · fight-night 23 · base 12 · wide 12 · pilot 12 ·
//   pilot-night 23 · third 12 · army 12 (200) · army-night 23 (200) ·
//   late 12 (none staged; a late-game fixture, fully zoomed out on Blue's
//   soldiers) · late8 (as late, for an 8-player game the runner stages with
//   B1's `-AcStage=N -AcStageBuildings`).
//
// Run (the runner passes these):
//   -AcBench=SHOT        the shot (also makes the game mode's run mode Bench)
//   -AcBenchFrames=90    timed frames
//   -AcBenchWarmup=20    frames stepped before them (the game moves on, as Swift's)
//   -AcBenchSettle=120   frames before those with the game paused, for
//                        Unreal's own warm-up (shaders, streaming, Lumen,
//                        exposure); Swift has none, so the timed moment is
//                        the same game time in both
//   -AcBenchScale=2      Swift's points-to-pixels scale: the RTS zoom is set
//                        so the view spans the same cells as Swift's
//                        `pointsPerCell` at pixels/scale points
//   -AcBenchOut=PATH     write the result JSON there too
//   -AcBenchPng=DIR      save the last frame as DIR/SHOT.png (after timing)
// The hour comes from `-AcHour=` (the runner sets the shot's), the window
// size from `-ResX/-ResY`; pass `-UseFixedTimeStep -FPS=30` so each frame is
// `tick(1/30)` as Swift's, and `-nosound`.
//
// Prints one `bench-result {json}` log line: the shape of `Bench.Result`
// plus Unreal's threads.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "SimdMath.h"

#include "AcBench.generated.h"

class UAcSimSubsystem;

/// `Bench.Shot`.
struct FAcBenchShot
{
	enum class EView : uint8 { Fight, Base, Wide, Late, Pilot };
	FString Name;
	double Hour = 12;
	EView View = EView::Fight;
	bool bThird = false;
	int32 Army = 29;

	static const TArray<FAcBenchShot>& All();
	static const FAcBenchShot* Find(const FString& Name);
};

struct AUTOCRAFT_API FAcBench
{
	struct FFight
	{
		ac::Vec2 Blue{0, 0}, Red{0, 0}, Middle{0, 0};
		/// Blue's Ranger to drive (second rank, 10,000 hp); -1: none.
		int64 Driver = -1;
	};
	/// `Bench.stageFight`: two armies of `Size` on open ground between the
	/// mains (with teams: player 0's and its first enemy's), Rangers in
	/// front, then Juggernauts, Fireflies, Comets, Longbows (every other
	/// one anchored) and Dropships, 16:4:3:2:2:2, rows of 8 (20 over 29).
	static FFight StageFight(UAcSimSubsystem& Sim, int32 Size);
};

UCLASS()
class AUTOCRAFT_API UAcBenchSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static bool IsBenchRun();

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }

private:
	enum class EPhase : uint8 { Load, Settle, Warmup, Timed, Picture, Count, Done };

	void Stage(UAcSimSubsystem& Sim);
	bool Aim();
	void Record();
	void Finish();
	void Fail(const FString& Why);

	FAcBenchShot Shot;
	FAcBench::FFight Fight;
	EPhase Phase = EPhase::Load;
	int32 Frames = 90, Warmup = 20, Settle = 120;
	double Scale = 1;
	FString OutPath, PngPath;
	int32 InPhase = 0;
	int32 LoadFrames = 0;
	bool bDriving = false;

	// The timed frames.
	double LastWall = 0;
	double GpuStartNs = -1, GpuEndNs = -1;
	double FootprintMB = 0, GraphicsMB = 0, PeakMB = 0;
	TArray<double> Wall, Game, Render, Rhi, Gpu, Ticks;
	double Parts[8] = {};
	double PartsMax[8] = {};
	int32 Draws = -1, Primitives = -1, MeshDraws = -1;
	/// `-AcBenchGpu`: `stat gpu` after the timed frames (each pass's busy ms,
	/// the stats' average) and one `ProfileGPU` tree in the log.
	bool bGpuStats = false;
	TArray<TPair<FString, double>> GpuPasses;
	double PngWaitStarted = 0;
	int64 LastPngSize = -1;
};
