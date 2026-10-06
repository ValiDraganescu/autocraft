#include "AcSimSubsystem.h"

#include "AcLog.h"
#include "AcPerf.h"
#include "AcSaves.h"
#include "AcSettings.h"

#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

#include "Commander.h"
#include "Rules.h"

#include <algorithm>
#include <string>

namespace
{
	TAutoConsoleVariable<int32> CVarSimPathAhead(TEXT("ac.SimPathAhead"), 2,
		TEXT("A step's path searches worked out ahead on worker threads (ac::PathAhead, the same game bit for bit): 0 off, 1 on, 2 on from 300 units (default)."));

	TAutoConsoleVariable<int32> CVarSimFastPaths(TEXT("ac.SimFastPaths"), 1,
		TEXT("Faster route finding that changes battles a little from the Swift game (ac::Simulation::setFastPaths): after a grid change only the walkers whose route it crosses re-path, a step starts at most ac.SimPathBudget searches (the rest keep their old route or wait), A* uses a weighted octile heuristic and a 4-ary heap. 1 on (default), 0 off = Swift's results bit for bit."));
	TAutoConsoleVariable<int32> CVarSimPathBudget(TEXT("ac.SimPathBudget"), 32,
		TEXT("With ac.SimFastPaths: searches a step may start."));

	std::string Utf8(const FString& S)
	{
		return std::string(TCHAR_TO_UTF8(*S));
	}

	FString Text(const std::string& S)
	{
		return FString(UTF8_TO_TCHAR(S.c_str()));
	}

	/// One session write at a time (they share the file's `.tmp`); the last
	/// write at quit waits for the one in flight.
	FCriticalSection SaveLock;

	/// `Simulation`'s unit by id, through its index (no copy).
	const ac::Unit* FindUnit(const ac::Simulation& Sim, int64_t Id)
	{
		const auto It = Sim.unitIndexByID.find(Id);
		if (It != Sim.unitIndexByID.end() && It->second >= 0 && It->second < (int64_t)Sim.state.units.size()
			&& Sim.state.units[It->second].id == Id)
		{
			return &Sim.state.units[It->second];
		}
		for (const ac::Unit& U : Sim.state.units)
		{
			if (U.id == Id) return &U;
		}
		return nullptr;
	}
}

// MARK: - Subsystem

UAcSimSubsystem* UAcSimSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UAcSimSubsystem>() : nullptr;
}

bool UAcSimSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UAcSimSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const TCHAR* Cmd = FCommandLine::Get();
	bNoSave = FParse::Param(Cmd, TEXT("AcNoSave"));
	bPaused = FParse::Param(Cmd, TEXT("AcPaused"));
	{
		FString Arg;
		bHoldClock = bPaused || FParse::Value(Cmd, TEXT("AcShot="), Arg) || FParse::Value(Cmd, TEXT("AcScene="), Arg);
		// Dev: a still whose render clock runs on, as the live game's pause.
		if (FParse::Param(Cmd, TEXT("AcClockRuns"))) bHoldClock = false;
	}
	double Seconds = 0.0;
	if (FParse::Value(Cmd, TEXT("AcRunFor="), Seconds) && Seconds > 0)
	{
		RunFor.emplace();
		RunFor->Seconds = Seconds;
		bNoSave = true;
	}
}

void UAcSimSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	Load();
}

void UAcSimSubsystem::Deinitialize()
{
	if (Sim && !bFixture && !bNoSave)
	{
		// The last write, on this thread: the process may be going away.
		Sess->state = Sim->state;
		FScopeLock Lock(&SaveLock);
		if (!ac::SessionStore(Utf8(SessionDirectory())).save(*Sess))
		{
			UE_LOG(LogAutocraft, Warning, TEXT("save failed: %s"), *SessionDirectory());
		}
		// Quit: the game as it stands, open (it resumes next launch), and wait
		// for the tracker to write it.
		Track();
	}
	Tracker.reset();
	Sim.reset();
	Super::Deinitialize();
}

TStatId UAcSimSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAcSimSubsystem, STATGROUP_Tickables);
}

// MARK: - Loading

FString UAcSimSubsystem::SessionDirectory()
{
	return AcSaves::Directory();
}

std::optional<ac::MapChoice> UAcSimSubsystem::ParseMap(const FString& In)
{
	const FString Wanted = In.TrimStartAndEnd();
	for (int64_t Players : {2, 4, 8})
	{
		for (ac::MapStyle Style : ac::allCases<ac::MapStyle>())
		{
			for (ac::MapSize Size : ac::allCases<ac::MapSize>())
			{
				const ac::MapChoice C{Style, Size, Players};
				FString Short = FString::Printf(TEXT("%s-%s"), *Text(std::string(ac::rawValue(Style))), *Text(std::string(ac::rawValue(Size))));
				if (Players > 2) Short += FString::Printf(TEXT("-%lld"), (long long)Players);
				if (Wanted.Equals(Short, ESearchCase::IgnoreCase) || Wanted.Equals(Text(C.name()), ESearchCase::IgnoreCase))
				{
					return C;
				}
			}
		}
	}
	return std::nullopt;
}

std::optional<std::vector<int64_t>> UAcSimSubsystem::ParseTeams(const FString& In)
{
	std::vector<int64_t> Teams;
	TArray<FString> Parts;
	if (In.Contains(TEXT("v")))
	{
		In.ToLower().ParseIntoArray(Parts, TEXT("v"));
		for (int32 T = 0; T < Parts.Num(); ++T)
		{
			const int32 N = FCString::Atoi(*Parts[T]);
			if (N <= 0) return std::nullopt;
			for (int32 K = 0; K < N; ++K) Teams.push_back(T);
		}
	}
	else
	{
		In.ParseIntoArray(Parts, TEXT(","));
		for (const FString& P : Parts)
		{
			if (!P.IsNumeric()) return std::nullopt;
			Teams.push_back(FCString::Atoi64(*P));
		}
	}
	if (Teams.size() < 2) return std::nullopt;
	return Teams;
}

void UAcSimSubsystem::Load()
{
	const TCHAR* Cmd = FCommandLine::Get();
	FString Path;
	if (FParse::Value(Cmd, TEXT("AcImport="), Path))
	{
		if (ImportSession(FPaths::ConvertRelativePathToFull(FPaths::LaunchDir(), Path))) return;
		UE_LOG(LogAutocraft, Error, TEXT("-AcImport=%s does not import; starting a window game instead"), *Path);
	}
	// The playground (F5): its own session on its own ground.
	if (FParse::Param(Cmd, TEXT("AcPlayground")))
	{
		OpenPlayground(FParse::Param(Cmd, TEXT("AcNew")));
		return;
	}
	if (FParse::Value(Cmd, TEXT("AcSession="), Path))
	{
		if (FPaths::IsRelative(Path))
		{
			// Relative to the repo root (one above the project), as the
			// Swift tools take `bench/...`; else to where it was launched.
			const FString FromRepo = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), Path));
			Path = FPaths::FileExists(FromRepo) ? FromRepo : FPaths::ConvertRelativePathToFull(FPaths::LaunchDir(), Path);
		}
		if (LoadSessionFile(Path)) return;
		UE_LOG(LogAutocraft, Error, TEXT("-AcSession=%s does not load; starting a window game instead"), *Path);
	}
	FString MapText = TEXT("highlands-medium");
	if (!FParse::Value(Cmd, TEXT("AcMap="), MapText))
	{
		// The last map played (Swift `windowMap`), else ask (F1).
		const FString Last = UAcSettings::Remembers() ? UAcSettings::Get().LastMap : FString();
		if (!Last.IsEmpty() && ParseMap(Last)) MapText = Last;
		else bNeedsMapChoice = UAcSettings::Remembers();
	}
	std::optional<ac::MapChoice> Choice = ParseMap(MapText);
	if (!Choice)
	{
		UE_LOG(LogAutocraft, Error, TEXT("-AcMap=%s: no such map (style-size[-players], e.g. badlands-large, highlands-small-8)"), *MapText);
		Choice = ac::MapChoice{ac::MapStyle::highlands, ac::MapSize::medium, 2};
	}
	std::optional<std::vector<int64_t>> Teams;
	FString TeamText;
	if (FParse::Value(Cmd, TEXT("AcTeams="), TeamText, /*bShouldStopOnSeparator*/ false))
	{
		Teams = ParseTeams(TeamText);
		if (!Teams) UE_LOG(LogAutocraft, Error, TEXT("-AcTeams=%s: not teams (4v4, 2v2v2v2, 0,0,1,1)"), *TeamText);
	}
	OpenWindowGame(*Choice, FParse::Param(Cmd, TEXT("AcNew")) || Teams.has_value(), Teams);
}

bool UAcSimSubsystem::LoadSessionFile(const FString& Path)
{
	std::string Error;
	std::optional<ac::Session> Loaded = ac::SessionStore::load(Utf8(Path), &Error);
	if (!Loaded)
	{
		UE_LOG(LogAutocraft, Error, TEXT("session %s: %s"), *Path, *Text(Error));
		return false;
	}
	// The map by name, as `simbench` finds it.
	std::optional<ac::MapChoice> Choice;
	for (int64_t Players : {2, 4, 8})
		for (ac::MapStyle Style : ac::allCases<ac::MapStyle>())
			for (ac::MapSize Size : ac::allCases<ac::MapSize>())
				if (!Choice && ac::MapChoice{Style, Size, Players}.name() == Loaded->mapName) Choice = ac::MapChoice{Style, Size, Players};
	if (Choice)
	{
		MapDef = Choice->players > 2 ? ac::WindowMaps::buildSquare(*Choice) : ac::WindowMaps::build(*Choice);
	}
	else if (Loaded->mapName == ac::WindowMaps::playground().name)
	{
		MapDef = ac::WindowMaps::playground();
	}
	else
	{
		UE_LOG(LogAutocraft, Error, TEXT("session %s: no window map \"%s\""), *Path, *Text(Loaded->mapName));
		return false;
	}
	if (Loaded->mapVersion != MapDef->version)
	{
		UE_LOG(LogAutocraft, Warning, TEXT("the session was saved on %s version %lld, now %lld"), *Text(Loaded->mapName),
			(long long)Loaded->mapVersion, (long long)MapDef->version);
	}
	Loaded->fillGame();
	MapChoice = Choice;
	Sess = MoveTemp(*Loaded);
	bFixture = true;
	UE_LOG(LogAutocraft, Log, TEXT("loaded session %s (%s) on \"%s\" at %.1f min, %d units, %d buildings"), *Text(Sess->id), *Path,
		*Text(MapDef->name), Sess->state.time / 60, (int32)Sess->state.units.size(), (int32)Sess->state.structures.size());
	StartSimulation();
	return true;
}

void UAcSimSubsystem::OpenWindowGame(const ac::MapChoice& Choice, const bool bFresh, const std::optional<std::vector<int64_t>>& Teams,
	const std::optional<bool> AI, const std::optional<bool> Fog)
{
	MapDef = Choice.players > 2 ? ac::WindowMaps::buildSquare(Choice) : ac::WindowMaps::build(Choice);
	MapChoice = Choice;
	bFixture = false;
	const ac::SessionStore Store(Utf8(SessionDirectory()));
	IFileManager::Get().MakeDirectory(*SessionDirectory(), true);
	const std::string Signature = "window|" + Choice.name();
	ac::SessionStore::Opened Opened = Store.open("window", Signature, *MapDef);
	if (bFresh)
	{
		Opened.session.restart(*MapDef);
		if (Teams)
		{
			Opened.session.state = ac::GameState::new_(*MapDef, std::nullopt, 0, Teams);
		}
		Opened.resumed = false;
	}
	if (AI) Opened.session.ai = *AI ? std::nullopt : std::optional<bool>(false);
	if (Fog) Opened.session.fog = *Fog;
	bResumed = Opened.resumed;
	Sess = MoveTemp(Opened.session);
	UE_LOG(LogAutocraft, Log, TEXT("%s session %s on \"%s\", %d players, ore %lld"), Opened.resumed ? TEXT("resumed") : TEXT("new"),
		*Text(Sess->id), *Text(MapDef->name), (int32)Sess->state.players.size(), (long long)Sess->state.ore());
	StartSimulation();
	if (!bNoSave) Save();
}

void UAcSimSubsystem::OpenPlayground(const bool bClear)
{
	// `GameController.init(playground: true)`: the session `playground`,
	// signature `playground`; Clear is `SessionStore.restart` on it.
	// Clear keeps the fog as it was (the bar's switch), whatever is on disk.
	const std::optional<bool> KeepFog = Sess && MapDef && MapDef->playground.value_or(false) ? Sess->fog : std::nullopt;
	MapDef = ac::WindowMaps::playground();
	MapChoice.reset();
	bFixture = false;
	const ac::SessionStore Store(Utf8(SessionDirectory()));
	IFileManager::Get().MakeDirectory(*SessionDirectory(), true);
	ac::SessionStore::Opened Opened = [&] {
		// After any write under way (Clear reads back what was just saved).
		FScopeLock Lock(&SaveLock);
		return Store.open("playground", "playground", *MapDef);
	}();
	if (bClear)
	{
		Opened.session.restart(*MapDef);
		Opened.resumed = false;
		if (KeepFog) Opened.session.fog = KeepFog;
	}
	// Eight sides to put things down for (the core makes two): each its
	// own team, all at war.
	ac::GameState& S = Opened.session.state;
	while (S.players.size() < 8) S.players.push_back(ac::Player());
	if (S.score.size() < S.players.size()) S.score.resize(S.players.size(), 0);
	Sess = MoveTemp(Opened.session);
	UE_LOG(LogAutocraft, Log, TEXT("%s playground: %d units, %d buildings, fog %s"), Opened.resumed ? TEXT("resumed") : TEXT("new"),
		(int32)Sess->state.units.size(), (int32)Sess->state.structures.size(), Sess->fog.value_or(false) ? TEXT("on") : TEXT("off"));
	StartSimulation();
	if (!bNoSave) Save();
}

void UAcSimSubsystem::StartSimulation(const std::vector<ac::Commander>* KeepCommanders)
{
	const bool bPlayground = MapDef->playground.value_or(false);
	const bool bAI = Sess->ai.value_or(true) && !bPlayground && !FParse::Param(FCommandLine::Get(), TEXT("AcNoAI"));
	const bool bFog = Sess->fog.value_or(!bPlayground);
	Sim.reset();
	Sim.emplace(Sess->state, *MapDef, KeepCommanders ? *KeepCommanders : bAI ? ac::Commander::all(*MapDef) : std::vector<ac::Commander>{}, bFog);
	Stalls = ac::StallWatch();
	Accumulator = 0.0;
	SinceSave = 0.0;
	ShownCache.reset();
	Pending.clear();
	Frame = FAcFrame();
	Frame.State = &Sim->state;
	Frame.Time = Sim->state.time;
	PausedSeconds = 0.0;
	Frame.Clock = Frame.Time;
	if (RunFor)
	{
		RunFor->StartTime = Sim->state.time;
		RunFor->StartUnits = (int64)Sim->state.units.size();
		RunFor->WallStart = FPlatformTime::Seconds();
	}
	OnGameStarted.Broadcast(*this);
}

// MARK: - Game flow

void UAcSimSubsystem::SetAI(const bool bOn)
{
	if (!Sim) return;
	// Swift `setAI`: nil when on.
	Sess->ai = bOn ? std::nullopt : std::optional<bool>(false);
	Sim->commanders = bOn ? ac::Commander::all(*MapDef) : std::vector<ac::Commander>{};
	UE_LOG(LogAutocraft, Log, TEXT("ai: %s"), bOn ? TEXT("on") : TEXT("off"));
	Save();
}

void UAcSimSubsystem::SetFog(const bool bOn)
{
	if (!Sim) return;
	Sess->fog = bOn;
	UE_LOG(LogAutocraft, Log, TEXT("fog: %s"), bOn ? TEXT("on") : TEXT("off"));
	if (IsFogOn() != bOn)
	{
		// The same game, built again with or without vision.
		Sess->state = Sim->state;
		const std::vector<ac::Commander> Keep = Sim->commanders;
		StartSimulation(&Keep);
	}
	Save();
}

bool UAcSimSubsystem::ImportSession(const FString& Path, FString* Error)
{
	FString Why;
	std::optional<AcSaves::FImported> In = AcSaves::Import(Path, Why);
	if (!In)
	{
		UE_LOG(LogAutocraft, Error, TEXT("import %s: %s"), *Path, *Why);
		if (Error) *Error = Why;
		return false;
	}
	if (Sim && !bFixture)
	{
		UE_LOG(LogAutocraft, Log, TEXT("game %s abandoned at %.1f min (import)"), *Text(Sess->game.value_or("")), Sim->state.time / 60);
		Track(true);
	}
	MapDef = AcSaves::Build(In->Choice);
	MapChoice = In->Choice;
	bFixture = false;
	Sess = MoveTemp(In->Session);
	UE_LOG(LogAutocraft, Log, TEXT("imported %s as session %s on \"%s\" at %.1f min, %d players, %d units, %d buildings"), *Path,
		*Text(Sess->id), *Text(MapDef->name), Sess->state.time / 60, (int32)Sess->state.players.size(),
		(int32)Sess->state.units.size(), (int32)Sess->state.structures.size());
	IFileManager::Get().MakeDirectory(*SessionDirectory(), true);
	StartSimulation();
	if (!bNoSave) Save();
	return true;
}

void UAcSimSubsystem::NewGame(const ac::MapChoice& Choice, const std::optional<std::vector<int64_t>>& Teams,
	const std::optional<bool> AI, const std::optional<bool> Fog)
{
	if (Sim && !bFixture)
	{
		// The game running now ends here, unfinished (Swift `track(abandoned:)`).
		UE_LOG(LogAutocraft, Log, TEXT("game %s abandoned at %.1f min"), *Text(Sess->game.value_or("")), Sim->state.time / 60);
		Track(true);
		SaveSession();
	}
	OpenWindowGame(Choice, true, Teams, AI, Fog);
}

void UAcSimSubsystem::Restart()
{
	if (!Sim) return;
	Sess->state = Sim->state;
	UE_LOG(LogAutocraft, Log, TEXT("game %s abandoned at %.1f min"), *Text(Sess->game.value_or("")), Sim->state.time / 60);
	Track(true);
	Sess->restart(*MapDef);
	UE_LOG(LogAutocraft, Log, TEXT("restart on \"%s\""), *Text(MapDef->name));
	StartSimulation();
	Save();
}

void UAcSimSubsystem::NextGame()
{
	if (!Sim) return;
	// The game that ended is written (its result) before the next replaces it.
	Track();
	Sim->newGame();
	Sess->nextGame();
	Sess->state = Sim->state;
	FString Score;
	for (const int64_t S : Sim->state.score) Score += (Score.IsEmpty() ? TEXT("") : TEXT(":")) + FString::Printf(TEXT("%lld"), (long long)S);
	UE_LOG(LogAutocraft, Log, TEXT("new game (score %s)"), *Score);
	Save();
	// The renderer and HUD rebuild as for any new game; the sim carries on.
	ShownCache.reset();
	OnGameStarted.Broadcast(*this);
}

bool UAcSimSubsystem::Save()
{
	if (!Sim || bFixture || bNoSave) return false;
	Track();
	return SaveSession();
}

void UAcSimSubsystem::Track(const bool bAbandoned)
{
	// Real window games only: no fixture, no -AcNoSave (every hidden shot and
	// test run; -AcRunFor sets it), no playground.
	if (!Sim || !Sess || bFixture || bNoSave || RunFor || IsPlayground()) return;
	if (!Tracker) Tracker = std::make_unique<FAcTracker>(AcSaves::TrackingFile());
	// An old session gets its game id before its rows are keyed by it.
	if (!Sess->game) Sess->fillGame();
	Tracker->Write(ac::TrackedGame::make(*Sess, Sim->state, Tracker->Build(), Tracker->OS(), ac::Date::now(), bAbandoned));
}

bool UAcSimSubsystem::SaveSession()
{
	if (!Sim || bFixture || bNoSave) return false;
	Sess->state = Sim->state;
	SinceSave = 0.0;
	// Encoding and writing off the game thread, on a copy.
	Async(EAsyncExecution::ThreadPool, [Copy = *Sess, Dir = Utf8(SessionDirectory())]() {
		FScopeLock Lock(&SaveLock);
		if (!ac::SessionStore(Dir).save(Copy))
		{
			UE_LOG(LogAutocraft, Warning, TEXT("save failed: %s"), UTF8_TO_TCHAR(Dir.c_str()));
		}
	});
	return true;
}

// MARK: - Stepping

bool UAcSimSubsystem::Issue(const ac::Command& Command)
{
	if (!Sim) return false;
	const bool bDone = Sim->issue(Command, Pending);
	// A pick that took stamps its level: the tracking database hears of it now.
	if (bDone && Command.is<ac::Command::Pick>()) Track();
	return bDone;
}

const ac::GameState& UAcSimSubsystem::Shown()
{
	if (!ShownCache) ShownCache = Sim->shown(LocalPlayer());
	return *ShownCache;
}

FDelegateHandle UAcSimSubsystem::AddFrameListener(const EAcFrameStage Stage, FAcFrameEvent::FDelegate Listener)
{
	return Listeners[(int32)Stage].Add(MoveTemp(Listener));
}

void UAcSimSubsystem::RemoveFrameListener(const EAcFrameStage Stage, const FDelegateHandle Handle)
{
	Listeners[(int32)Stage].Remove(Handle);
}

void UAcSimSubsystem::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!Sim) return;
	FAcPerfTick PerfTick;  // the tick and its parts (AcPerf.h)
	if (RunFor)
	{
		TickRunFor();
		return;
	}
	StepFrame((double)DeltaTime);
	if (!bFixture && !bNoSave)
	{
		SinceSave += Frame.RealDelta;
		if (SinceSave >= AutosaveSeconds)
		{
			AC_PERF_SCOPE(EAcTickPart::Save);
			Save();
		}
	}
}

void UAcSimSubsystem::StepFrame(const double RealDelta)
{
	// Never fast-forward through sleep or a stall (`GameController.tick`).
	const double Dt = FMath::Clamp(RealDelta, 0.0, MaxFrameSeconds);
	Frame.RealDelta = Dt;
	Frame.Steps = 0;
	Frame.AllEvents = MoveTemp(Pending);
	Pending.clear();
	if (!bPaused)
	{
		AC_PERF_SCOPE(EAcTickPart::Sim);
		Accumulator += Dt;
		while (Accumulator >= StepSeconds)
		{
			Accumulator -= StepSeconds;
			std::vector<ac::GameEvent> Events = StepOnce();
			Frame.AllEvents.insert(Frame.AllEvents.end(), std::make_move_iterator(Events.begin()), std::make_move_iterator(Events.end()));
			Frame.Steps += 1;
			if (!MapDef->playground.value_or(false))
			{
				for (const std::string& Note : Stalls.observe(Sim->state)) UE_LOG(LogAutocraft, Log, TEXT("%s"), *Text(Note));
			}
			if (StepsLeft > 0 && --StepsLeft == 0)
			{
				// `StepThenPause`: the steps asked for are done.
				bPaused = true;
				StepsLeft = -1;
				Accumulator = 0.0;
				break;
			}
		}
	}
	Frame.Alpha = Accumulator / StepSeconds;
	Frame.Time = Sim->state.time;
	// The render clock runs on while paused (Swift's `clock += dt`).
	if (bPaused && Frame.Steps == 0 && !bHoldClock) PausedSeconds += Dt;
	Frame.Clock = Frame.Time + PausedSeconds;
	Frame.State = &Sim->state;
	{
		AC_PERF_SCOPE(EAcTickPart::Events);
		Frame.Events.clear();
		Gate(Frame.AllEvents, Frame.Events);
		LogEvents(Frame.AllEvents);
	}
	if (Frame.Steps > 0) ShownCache.reset();
	// A level-up (the level's stamp) and the victory (the result) go to the
	// tracking database as they happen, not at the next autosave.
	for (const ac::GameEvent& E : Frame.AllEvents)
	{
		if (E.is<ac::GameEvent::LeveledUp>() || E.is<ac::GameEvent::Victory>())
		{
			Track();
			break;
		}
	}
	Broadcast();
	// The victory lap, then the next game on the same map.
	if (Sim->state.endedAt && Sim->state.time - *Sim->state.endedAt >= ac::Rules::victoryLap)
	{
		NextGame();
	}
}

void UAcSimSubsystem::Broadcast()
{
	// Each stage is a part of the tick (AcPerf.h): renderer → scene,
	// effects and other → events, audio, hud.
	static constexpr EAcTickPart Parts[] = {EAcTickPart::Scene, EAcTickPart::Events, EAcTickPart::Audio, EAcTickPart::Hud, EAcTickPart::Events};
	static_assert(UE_ARRAY_COUNT(Parts) == (int32)EAcFrameStage::Count, "a frame stage has no perf part");
	for (int32 I = 0; I < (int32)EAcFrameStage::Count; ++I)
	{
		AC_PERF_SCOPE(Parts[I]);
		Listeners[I].Broadcast(Frame);
	}
}

std::vector<ac::GameEvent> UAcSimSubsystem::StepOnce()
{
	const bool bFast = CVarSimFastPaths.GetValueOnGameThread() != 0;
	const int64 Budget = FMath::Max(CVarSimPathBudget.GetValueOnGameThread(), 1);
	if (Sim->fastPaths != bFast || (bFast && Sim->pathBudget != Budget)) Sim->setFastPaths(bFast, Budget);
	// 0 off, 1 on, 2 (default) on once the game has many units: below that
	// a step makes few searches and the probe's copy costs more than it saves.
	const int32 Mode = CVarSimPathAhead.GetValueOnGameThread();
	const bool bAhead = Mode == 1 || (Mode == 2 && Sim->state.units.size() >= 300);
	if (!bAhead || !Sim->nav) return Sim->step(StepSeconds);
	if (!PathAhead)
	{
		const int32 Threads = FMath::Clamp(FPlatformMisc::NumberOfCores() - 4, 2, 10);
		PathAhead = std::make_unique<ac::PathAhead>(Threads);
		UE_LOG(LogAutocraft, Log, TEXT("sim: paths worked out ahead on %d threads"), Threads);
	}
	const double T0 = FPlatformTime::Seconds();
	std::vector<ac::GameEvent> Events = PathAhead->step(*Sim, StepSeconds);
	const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
	if (Ms > 50.0)
	{
		const ac::PathAhead::Stats& St = PathAhead->lastStats();
		UE_LOG(LogAutocraft, Log, TEXT("sim: step at %.2f s took %.0f ms: %lld searches (%lld ready, %lld waited for, %lld here), probe %.0f ms%s"),
			Sim->state.time, Ms, (long long)St.asked, (long long)St.ready, (long long)St.waited, (long long)(St.claimed + St.missed),
			St.probeMs, St.gridDiffered ? TEXT(", grids differed") : St.skipped ? TEXT(", no probe") : TEXT(""));
	}
	return Events;
}

void UAcSimSubsystem::Gate(const std::vector<ac::GameEvent>& In, std::vector<ac::GameEvent>& Out) const
{
	const int64_t Team = LocalPlayer();
	const ac::Simulation& S = *Sim;
	const std::optional<ac::Sight> Sight = S.sight(Team);
	auto SeenAt = [&](ac::Vec2 P) { return S.sees(Team, P); };
	auto SeenId = [&](int64_t Id) { return Sight ? Sight->ids.count(Id) > 0 : true; };
	Out.reserve(In.size());
	for (const ac::GameEvent& E : In)
	{
		bool bKeep = true;
		if (const auto* D = E.as<ac::GameEvent::Deposited>())
		{
			bKeep = SeenAt(D->at);
		}
		else if (const auto* T = E.as<ac::GameEvent::Trained>())
		{
			bKeep = SeenAt(T->at);
		}
		else if (const auto* Sh = E.as<ac::GameEvent::Shot>())
		{
			const ac::Unit* Shooter = FindUnit(S, Sh->unit);
			bKeep = SeenAt(Sh->at) || SeenId(Sh->unit) || SeenId(Sh->target)
				|| (Shooter && (S.state.allied(Shooter->owner, Team) || SeenAt(Shooter->position)));
		}
		else if (const auto* Di = E.as<ac::GameEvent::Died>())
		{
			bKeep = S.state.allied(Di->owner, Team) || SeenId(Di->unit) || SeenAt(Di->at);
		}
		else if (const auto* De = E.as<ac::GameEvent::Destroyed>())
		{
			bKeep = S.state.allied(De->owner, Team) || SeenId(De->structure);
		}
		else if (const auto* St = E.as<ac::GameEvent::Stomp>())
		{
			const ac::Unit* Stomper = FindUnit(S, St->unit);
			bKeep = SeenAt(St->at) || (Stomper && S.state.allied(Stomper->owner, Team));
		}
		if (bKeep) Out.push_back(E);
	}
}

const TCHAR* UAcSimSubsystem::EventName(const ac::GameEvent& Event)
{
	static const TCHAR* const Names[] = {TEXT("Deposited"), TEXT("PatchDepleted"), TEXT("PatchRegrown"), TEXT("Trained"),
		TEXT("ConstructionStarted"), TEXT("Constructed"), TEXT("Shot"), TEXT("Missed"), TEXT("Landed"), TEXT("Died"),
		TEXT("Destroyed"), TEXT("Victory"), TEXT("Researched"), TEXT("Stuck"), TEXT("Trapped"), TEXT("LeveledUp"), TEXT("Blast"), TEXT("Stomp")};
	static_assert(UE_ARRAY_COUNT(Names) == std::variant_size_v<decltype(ac::GameEvent::value)>, "a GameEvent case has no name");
	return Names[Event.value.index()];
}

void UAcSimSubsystem::LogEvents(const std::vector<ac::GameEvent>& Events)
{
	const ac::GameState& S = Sim->state;
	for (const ac::GameEvent& E : Events)
	{
		if (RunFor) RunFor->EventCounts.FindOrAdd(EventName(E)) += 1;
		if (const auto* V = E.as<ac::GameEvent::Victory>())
		{
			UE_LOG(LogAutocraft, Log, TEXT("victory: player %lld at %.1f min"), (long long)V->winner, S.time / 60);
		}
		else if (const auto* C = E.as<ac::GameEvent::Constructed>())
		{
			UE_LOG(LogAutocraft, Verbose, TEXT("built %s #%lld at %.1f min"), *Text(std::string(ac::rawValue(C->kind))), (long long)C->structure, S.time / 60);
		}
		else if (const auto* D = E.as<ac::GameEvent::Destroyed>())
		{
			UE_LOG(LogAutocraft, Log, TEXT("war: player %lld's %s #%lld destroyed at %.1f min"), (long long)D->owner,
				*Text(std::string(ac::rawValue(D->kind))), (long long)D->structure, S.time / 60);
		}
		else if (const auto* T = E.as<ac::GameEvent::Trapped>())
		{
			UE_LOG(LogAutocraft, Log, TEXT("trapped: %s #%lld of player %lld at (%.1f, %.1f)"), *Text(std::string(ac::rawValue(T->kind))),
				(long long)T->unit, (long long)T->owner, T->at.x, T->at.y);
		}
	}
}

// MARK: - -AcRunFor

void UAcSimSubsystem::TickRunFor()
{
	FRunFor& R = *RunFor;
	if (R.bDone) return;
	ac::Simulation& S = *Sim;
	// As many fixed steps as fit in about 50 ms, so the engine keeps ticking.
	const double Budget = FPlatformTime::Seconds() + 0.05;
	Frame.AllEvents = MoveTemp(Pending);
	Pending.clear();
	Frame.Steps = 0;
	while (S.state.time < R.StartTime + R.Seconds && !S.state.endedAt && FPlatformTime::Seconds() < Budget)
	{
		const double T0 = FPlatformTime::Seconds();
		std::vector<ac::GameEvent> Events = StepOnce();
		R.StepMs.push_back((FPlatformTime::Seconds() - T0) * 1000.0);
		Frame.AllEvents.insert(Frame.AllEvents.end(), Events.begin(), Events.end());
		Frame.Steps += 1;
	}
	Frame.Time = S.state.time;
	Frame.Clock = Frame.Time + PausedSeconds;
	Frame.State = &S.state;
	Frame.Events.clear();
	Gate(Frame.AllEvents, Frame.Events);
	LogEvents(Frame.AllEvents);
	ShownCache.reset();
	Broadcast();
	if (S.state.time < R.StartTime + R.Seconds && !S.state.endedAt) return;

	R.bDone = true;
	const double Wall = FPlatformTime::Seconds() - R.WallStart;
	std::vector<double> Sorted = R.StepMs;
	std::sort(Sorted.begin(), Sorted.end());
	auto At = [&](double Q) { return Sorted.empty() ? 0.0 : Sorted[std::min((size_t)(double(Sorted.size()) * Q), Sorted.size() - 1)]; };
	double Sum = 0;
	for (double V : Sorted) Sum += V;
	// The `simbench` line, so the two compare side by side.
	UE_LOG(LogAutocraft, Display,
		TEXT("runfor: %s from %.1f min, %d steps (%.0f game s) in %.2f s: mean %.2f, p50 %.2f, p95 %.2f, p99 %.2f, max %.1f ms; units %lld to %lld"),
		*Text(MapDef->name), R.StartTime / 60, (int32)R.StepMs.size(), S.state.time - R.StartTime, Wall,
		Sum / double(std::max<size_t>(Sorted.size(), 1)), At(0.5), At(0.95), At(0.99), Sorted.empty() ? 0.0 : Sorted.back(),
		(long long)R.StartUnits, (long long)S.state.units.size());
	R.EventCounts.KeySort([](const FString& A, const FString& B) { return A < B; });
	FString Counts;
	for (const TPair<FString, int64>& P : R.EventCounts) Counts += FString::Printf(TEXT(" %s=%lld"), *P.Key, (long long)P.Value);
	UE_LOG(LogAutocraft, Display, TEXT("runfor: events%s"), *Counts);
	FString PerPlayer;
	for (int64_t P = 0; P < (int64_t)S.state.players.size(); ++P)
	{
		int64 Units = 0, Buildings = 0;
		for (const ac::Unit& U : S.state.units) Units += U.owner == P;
		for (const ac::Structure& B : S.state.structures) Buildings += B.owner == P;
		PerPlayer += FString::Printf(TEXT(" p%lld=%lld/%lld"), (long long)P, (long long)Units, (long long)Buildings);
	}
	UE_LOG(LogAutocraft, Display, TEXT("runfor: units/buildings per player%s"), *PerPlayer);
	FPlatformMisc::RequestExit(false, TEXT("AcRunFor"));
}
