// `UAcSimSubsystem`: the game's heart (GAME-LAYER.md §3.1). It owns the
// core simulation (`ac::Simulation`, with one `ac::Commander` per AI player
// inside it) and the saved `ac::Session`, steps it at a fixed 60 Hz, gates
// the events by the local player's fog, and hands every frame to the
// renderer, effects, audio and HUD in that order (`AddFrameListener`).
//
// Command line (all optional):
//   -AcSession=PATH   load a session JSON (a Swift save or a fixture such as
//                     bench/badlands-large.json). Never saved back.
//   -AcMap=NAME       a window game on that map: `badlands-large`,
//                     `highlands-small-8` (style-size[-players]) or the
//                     map's full name. Resumes the saved game of that map
//                     unless -AcNew. Default: `highlands-medium`.
//   -AcTeams=4v4      teams for a new game (`1v1`, `2v2v2v2`, `4v4`, or one
//                     team index per player: `0,0,1,1`). Default: none.
//   -AcNew            start the map's game over instead of resuming it.
//   -AcNoAI           no commanders (as `session.ai = false`).
//   -AcNoSave         never write the session (the autosave is otherwise
//                     every 15 s of real time and on quit, for -AcMap games).
//   -AcRunFor=SECONDS headless check: step that many game seconds as fast as
//                     possible (fixed 1/60 steps, as `simbench`), log the
//                     step times, unit counts and event counts, then quit.
//   -AcPaused         start paused.
//   -AcImport=PATH    import a Swift session JSON as the window game (F1,
//                     AcSaves.h): saved as the Unreal game's own from then on.
//   -AcSaveDir=PATH   keep the sessions there instead (tests).
// With no -AcMap the game opens on the last map played (`UAcSettings`
// LastMap, F1); with none yet, highlands-medium and `NeedsMapChoice()`.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "AcTracker.h"
#include "PathAhead.h"
#include "Session.h"
#include "Simulation.h"
#include "StallWatch.h"
#include "Types.h"
#include "WindowMaps.h"

#include <memory>
#include <optional>
#include <vector>

#include "AcSimSubsystem.generated.h"

/// One rendered frame's worth of simulation: 0 or more fixed steps.
struct FAcFrame
{
	/// The whole truth (every player's units). Read-only for the game layer.
	const ac::GameState* State = nullptr;
	/// What the local player may know of this frame's events: those it
	/// cannot see (enemy shots, deaths, deposits in the fog) are dropped
	/// here, before visuals or sound (`GameController.swift:318-335`).
	std::vector<ac::GameEvent> Events;
	/// Every event of the frame's steps, unfiltered (for the log, the bench).
	std::vector<ac::GameEvent> AllEvents;
	/// Game time after the frame's steps (`state.time`).
	double Time = 0.0;
	/// The render clock (Swift `GameController.clock`): game time plus the
	/// real seconds spent paused, so it keeps running while the game is
	/// paused and equals `Time` + a constant while it runs. Cosmetic
	/// animation (poses: blinking beacons, spinning dishes, walk cycles;
	/// renderer memory stamps) reads this. Held still in shot runs
	/// (`-AcPaused`, `-AcShot=`, `-AcScene=`): Swift's `windowshot` never ticks
	/// (`-AcClockRuns` lets it run there too).
	double Clock = 0.0;
	/// Fixed steps taken this frame (0 when paused or between steps).
	int32 Steps = 0;
	/// Real seconds this frame (clamped to 0.1).
	double RealDelta = 0.0;
	/// How far game time is between this step and the next (0…1), for a
	/// renderer that wants to interpolate. Swift does not: use `Time`.
	double Alpha = 0.0;
	/// The local player (always 0: the human).
	int64 LocalPlayer = 0;
};

/// Who gets the frame, in this order (GAME-LAYER.md §3.1).
enum class EAcFrameStage : uint8
{
	Renderer,
	Effects,
	Audio,
	Hud,
	Other,
	Count
};

DECLARE_MULTICAST_DELEGATE_OneParam(FAcFrameEvent, const FAcFrame& /*Frame*/);
/// A game began: a session was loaded or a new game started (also after a
/// victory lap or a restart). Terrain, renderer and HUD rebuild from
/// `Map()` and `State()` here.
DECLARE_MULTICAST_DELEGATE_OneParam(FAcGameStarted, class UAcSimSubsystem& /*Sim*/);

UCLASS()
class AUTOCRAFT_API UAcSimSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/// The fixed step: 60 per game second.
	static constexpr double StepSeconds = 1.0 / 60.0;
	/// A frame never advances the game more than this (sleep, stalls).
	static constexpr double MaxFrameSeconds = 0.1;
	/// Autosave interval, real seconds (`GameController.swift:482`).
	static constexpr double AutosaveSeconds = 15.0;

	/// The subsystem of `WorldContext`'s world (null outside a game world).
	static UAcSimSubsystem* Get(const UObject* WorldContext);

	// USubsystem / UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/// True once a session is loaded (from OnWorldBeginPlay on).
	bool IsRunning() const { return Sim.has_value(); }

	/// The simulation. Valid while `IsRunning()`.
	ac::Simulation& Simulation() { return *Sim; }
	const ac::Simulation& Simulation() const { return *Sim; }
	/// `Simulation().state`.
	const ac::GameState& State() const { return Sim->state; }
	const ac::MapDefinition& Map() const { return *MapDef; }
	ac::Session& Session() { return *Sess; }
	const ac::Session& Session() const { return *Sess; }
	/// The map the session plays, when it is a window map.
	const std::optional<ac::MapChoice>& Choice() const { return MapChoice; }
	/// The human: always player 0.
	int64 LocalPlayer() const { return 0; }
	double GameTime() const { return Sim ? Sim->state.time : 0.0; }
	/// The render clock (`FAcFrame::Clock`).
	double Clock() const { return GameTime() + PausedSeconds; }

	/// The game as the local player sees it (`sim.shown(team)`): enemy units
	/// in sight, enemy buildings as last seen. Worked out once per frame,
	/// on first ask.
	const ac::GameState& Shown();

	/// The last frame handed out (its `Events` are this frame's).
	const FAcFrame& LastFrame() const { return Frame; }

	/// Give an order now (`sim.issue`). Its events join the next frame's.
	/// False when it is not possible (cost, supply, unknown unit).
	bool Issue(const ac::Command& Command);

	/// Listen to every frame at a stage. Remove with `RemoveFrameListener`.
	FDelegateHandle AddFrameListener(EAcFrameStage Stage, FAcFrameEvent::FDelegate Listener);
	void RemoveFrameListener(EAcFrameStage Stage, FDelegateHandle Handle);
	/// Fired when a game begins (see `FAcGameStarted`); a listener added
	/// after that should read `IsRunning()` and build at once.
	FAcGameStarted OnGameStarted;

	/// Stop or resume stepping (frames still go out, with 0 steps).
	void SetPaused(bool bInPaused) { bPaused = bInPaused; StepsLeft = -1; }
	bool IsPaused() const { return bPaused; }
	/// Run exactly `Steps` more fixed steps (over as many frames as they
	/// take), then pause: staged stills (E2) step as Swift's stage does.
	void StepThenPause(int32 Steps) { bPaused = Steps <= 0; StepsLeft = FMath::Max(0, Steps); Accumulator = 0.0; }
	/// Steps still to run before `StepThenPause` pauses (-1: none asked).
	int32 StepsToPause() const { return bPaused ? 0 : StepsLeft; }

	/// Turn the AI commanders on or off (`session.ai`), saved.
	void SetAI(bool bOn);
	bool IsAIOn() const { return Sim && !Sim->commanders.empty(); }
	/// The fog of war on or off (`session.fog`, saved): the sim is built
	/// again at once on the same state, keeping the commanders (Swift waits
	/// for the next build; the playground rebuilds at once). Fires
	/// `OnGameStarted`.
	void SetFog(bool bOn);
	bool IsFogOn() const { return Sim && Sim->vision.has_value(); }
	/// The session came from -AcSession (a fixture, never saved).
	bool IsFixture() const { return bFixture; }
	/// No map was asked for and none remembered (F1 asks for one).
	bool NeedsMapChoice() const { return bNeedsMapChoice; }
	/// The window game came from a saved session (the home screen's Resume).
	bool WasResumed() const { return bResumed; }
	/// Make a session file (a Swift save, AcSaves::Import) the window game
	/// and save it as the Unreal game's own. False (logged): not imported.
	bool ImportSession(const FString& Path, FString* Error = nullptr);

	/// A new game on `Choice` (with `Teams`, one entry per player), saved
	/// under the window session.
	/// `AI`/`Fog` unset: as the last session had them (Swift keeps them).
	void NewGame(const ac::MapChoice& Choice, const std::optional<std::vector<int64_t>>& Teams = std::nullopt,
		std::optional<bool> AI = std::nullopt, std::optional<bool> Fog = std::nullopt);
	/// The same map over again (`SessionStore.restart`), keeping the teams.
	void Restart();
	/// After a victory lap: the next game on the same map (`sim.newGame`).
	void NextGame();

	/// The playground (F5, `-AcPlayground`): its own session (`playground`,
	/// on `WindowMaps::playground`), no AI, fog off unless asked, eight
	/// sides. `bClear`: start it over empty (Clear, `-AcNew`).
	void OpenPlayground(bool bClear);
	bool IsPlayground() const { return MapDef && MapDef->playground.value_or(false); }
	/// The state was changed by hand between steps (the playground puts
	/// things down): `Shown()` is worked out again.
	void MarkEdited() { ShownCache.reset(); }

	/// Write the session now (on a background task). False: not saved
	/// (a fixture from -AcSession, -AcNoSave, or nothing loaded).
	bool Save();

	/// Write the game as it stands to the tracking database (a copy of its
	/// rows, written on the tracker's thread; docs/leveling.md, "Tracking").
	/// `Save` does, and so do a level-up, a pick, the victory, a new game,
	/// a restart (`bAbandoned`: the running game ends here unfinished) and
	/// quit. Nothing for a fixture, `-AcNoSave` and the playground.
	void Track(bool bAbandoned = false);
	/// The tracker, once a game has been written (nil: nothing tracked).
	FAcTracker* GetTracker() const { return Tracker.get(); }

	/// Where the Unreal game keeps its sessions:
	/// `~/Library/Application Support/Autocraft/Unreal/sessions` (its own,
	/// apart from the Swift game's; `AcSaves::Directory`).
	static FString SessionDirectory();
	/// `badlands-large`, `highlands-small-8` or a full map name → a choice.
	static std::optional<ac::MapChoice> ParseMap(const FString& Text);
	/// `4v4`, `2v2v2v2`, `0,0,1,1` → one team per player.
	static std::optional<std::vector<int64_t>> ParseTeams(const FString& Text);
	/// The case name of an event (`Shot`, `Died`...).
	static const TCHAR* EventName(const ac::GameEvent& Event);

private:
	/// The session file's write (`Save` without the tracking write).
	bool SaveSession();
	void Load();
	bool LoadSessionFile(const FString& Path);
	void OpenWindowGame(const ac::MapChoice& Choice, bool bFresh, const std::optional<std::vector<int64_t>>& Teams,
		std::optional<bool> AI = std::nullopt, std::optional<bool> Fog = std::nullopt);
	void StartSimulation(const std::vector<ac::Commander>* KeepCommanders = nullptr);
	void StepFrame(double RealDelta);
	void Gate(const std::vector<ac::GameEvent>& In, std::vector<ac::GameEvent>& Out) const;
	void LogEvents(const std::vector<ac::GameEvent>& Events);
	void Broadcast();
	void TickRunFor();
	/// One fixed step, its searches worked out ahead on other threads when
	/// `ac.SimPathAhead` says so (ac::PathAhead: the same game, bit for bit).
	std::vector<ac::GameEvent> StepOnce();

	std::optional<ac::Simulation> Sim;
	/// The worker threads of `StepOnce` (made on first use).
	std::unique_ptr<ac::PathAhead> PathAhead;
	std::optional<ac::Session> Sess;
	/// The leveling tracking database's writer, made by the first `Track`.
	std::unique_ptr<FAcTracker> Tracker;
	std::optional<ac::MapDefinition> MapDef;
	std::optional<ac::MapChoice> MapChoice;
	ac::StallWatch Stalls;

	FAcFrame Frame;
	std::optional<ac::GameState> ShownCache;
	/// Events from `Issue` between frames.
	std::vector<ac::GameEvent> Pending;
	FAcFrameEvent Listeners[(int32)EAcFrameStage::Count];

	double Accumulator = 0.0;
	double SinceSave = 0.0;
	/// Real seconds spent paused this game (`FAcFrame::Clock`).
	double PausedSeconds = 0.0;
	/// A shot run: the render clock does not run while paused.
	bool bHoldClock = false;
	bool bPaused = false;
	/// `StepThenPause`: steps left (-1: run on).
	int32 StepsLeft = -1;
	/// The session came from -AcSession (a fixture): never written.
	bool bFixture = false;
	bool bNoSave = false;
	bool bNeedsMapChoice = false;
	bool bResumed = false;

	/// -AcRunFor: the headless check.
	struct FRunFor
	{
		double Seconds = 0.0;
		double StartTime = 0.0;
		double WallStart = 0.0;
		int64 StartUnits = 0;
		std::vector<double> StepMs;
		TMap<FString, int64> EventCounts;
		bool bDone = false;
	};
	std::optional<FRunFor> RunFor;
};
