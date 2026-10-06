#include "AcGameFlow.h"
#include "AcMusicPlayer.h"

#include "AcCommandMap.h"
#include "AcCommands.h"
#include "AcGameMode.h"
#include "AcHUD.h"
#include "AcLog.h"
#include "AcPilotPawn.h"
#include "AcPointer.h"
#include "AcRtsPawn.h"
#include "AcSaves.h"
#include "AcSettings.h"
#include "AcShot.h"
#include "AcSimSubsystem.h"
#include "SAcRoot.h"

#include "Engine/Console.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

#include "Commander.h"
#include "Rules.h"

namespace
{
	/// Over the command map (60), under the tip (90).
	constexpr int32 NewGameLayer = 80;
	/// The home screen and pause menu: under the new-game dialog it opens.
	constexpr int32 MenuLayer = 75;

	FString Text(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

	FString WindowFile() { return FPaths::Combine(AcSaves::Directory(), TEXT("window.json")); }

	std::optional<ac::Session> ReadSession(const FString& Path)
	{
		return ac::SessionStore::load(std::string(TCHAR_TO_UTF8(*Path)));
	}

	/// ⌘N, ⌘R (the menu's keys), and Return/Escape for the dialog when it
	/// has not got the keyboard.
	class FAcFlowKeys final : public IInputProcessor
	{
	public:
		explicit FAcFlowKeys(UAcGameFlowSubsystem* InFlow) : Flow(InFlow) {}
		virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
		virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
		{
			UAcGameFlowSubsystem* F = Flow.Get();
			if (!F || Event.IsRepeat()) return false;
			const FKey K = Event.GetKey();
			if (TSharedPtr<SAcNewGame> D = F->NewGameDialog())
			{
				if (K == EKeys::Enter) { D->Start(); return true; }
				if (K == EKeys::Escape) { D->Cancel(); return true; }
				// The game's own keys wait while the dialog is up.
				return !Event.IsCommandDown();
			}
			if (TSharedPtr<SAcMenu> M = F->MenuWidget())
			{
				if (K == EKeys::Up || K == EKeys::W) M->Move(-1);
				else if (K == EKeys::Down || K == EKeys::S || K == EKeys::Tab) M->Move(1);
				else if (K == EKeys::Left || K == EKeys::A) M->Adjust(-1);
				else if (K == EKeys::Right || K == EKeys::D) M->Adjust(1);
				else if (K == EKeys::Enter || K == EKeys::SpaceBar) M->PickHighlighted();
				else if (K == EKeys::Escape && M->Back()) {}
				else if (K == EKeys::Escape && M->Mode() == EAcMenuMode::Pause) F->CloseMenu();
				else if (K == EKeys::N && Event.IsCommandDown()) F->OpenNewGame();
				// Nothing else reaches the game while the menu is up (⌘Q still quits).
				return !(Event.IsCommandDown() && K == EKeys::Q);
			}
			if (K == EKeys::Escape && !Event.IsCommandDown() && F->WantsPauseOnEscape())
			{
				F->OpenMenu(EAcMenuMode::Pause);
				return true;
			}
			if (!Event.IsCommandDown() || Event.IsShiftDown() || Event.IsAltDown()) return false;
			if (K == EKeys::N) { F->OpenNewGame(); return true; }
			if (K == EKeys::R) { F->Restart(); return true; }
			return false;
		}
		virtual const TCHAR* GetDebugName() const override { return TEXT("AcFlowKeys"); }

	private:
		TWeakObjectPtr<UAcGameFlowSubsystem> Flow;
	};

	UAcGameFlowSubsystem* FlowOf(UWorld* World) { return World ? World->GetSubsystem<UAcGameFlowSubsystem>() : nullptr; }

	FAutoConsoleCommandWithWorldAndArgs CmdNewGame(TEXT("ac.NewGame"),
		TEXT("ac.NewGame: the new-game dialog. ac.NewGame MAP [TEAMS]: start that game now (badlands-large-8 4v4)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) {
			UAcGameFlowSubsystem* F = FlowOf(World);
			if (!F) return;
			if (Args.Num() == 0)
			{
				F->OpenNewGame();
				return;
			}
			const std::optional<ac::MapChoice> Map = UAcSimSubsystem::ParseMap(Args[0]);
			if (!Map)
			{
				UE_LOG(LogAutocraft, Warning, TEXT("ac.NewGame: no map %s"), *Args[0]);
				return;
			}
			FAcNewGameChoice C = F->CurrentChoice();
			C.Map = *Map;
			C.Teams = Args.Num() > 1 ? Args[1] : SAcNewGame::TeamOptions(Map->players)[0];
			F->StartNewGame(C);
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdNewGameDialog(TEXT("ac.NewGameDialog"),
		TEXT("ac.NewGameDialog [MAP [TEAMS]]: open the new-game dialog on that choice; ac.NewGameDialog close."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) {
			UAcGameFlowSubsystem* F = FlowOf(World);
			if (!F) return;
			if (Args.Num() > 0 && Args[0] == TEXT("close"))
			{
				F->CloseNewGame();
				return;
			}
			TOptional<FAcNewGameChoice> C;
			if (Args.Num() > 0)
			{
				if (const std::optional<ac::MapChoice> Map = UAcSimSubsystem::ParseMap(Args[0]))
				{
					FAcNewGameChoice N = F->CurrentChoice();
					N.Map = *Map;
					N.Teams = Args.Num() > 1 ? Args[1] : SAcNewGame::TeamOptions(Map->players)[0];
					C = N;
				}
			}
			if (F->IsNewGameOpen() && C) F->NewGameDialog()->SetChoice(*C);
			else F->OpenNewGame(TEXT("Pick a map and how big it is."), C);
		}));

	FAutoConsoleCommandWithWorld CmdRestart(TEXT("ac.Restart"), TEXT("The same map from the start (⌘R)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) {
			if (UAcGameFlowSubsystem* F = FlowOf(World)) F->Restart();
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdAI(TEXT("ac.AI"), TEXT("ac.AI [0|1]: the AI commanders off/on (toggles with no value); saved."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) {
			UAcSimSubsystem* Sim = UAcSimSubsystem::Get(World);
			if (!Sim || !Sim->IsRunning()) return;
			Sim->SetAI(Args.Num() > 0 ? FCString::Atoi(*Args[0]) != 0 : !Sim->IsAIOn());
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdSessionFog(TEXT("ac.SessionFog"),
		TEXT("ac.SessionFog [0|1]: the fog of war in the game (session.fog: what the player sees and hears), off/on; saved."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) {
			UAcSimSubsystem* Sim = UAcSimSubsystem::Get(World);
			if (!Sim || !Sim->IsRunning()) return;
			Sim->SetFog(Args.Num() > 0 ? FCString::Atoi(*Args[0]) != 0 : !Sim->IsFogOn());
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdImport(TEXT("ac.Import"),
		TEXT("ac.Import [PATH]: play a Swift session JSON here (default: the Swift game's window.json)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) {
			if (UAcGameFlowSubsystem* F = FlowOf(World)) F->Import(Args.Num() > 0 ? FString::Join(Args, TEXT(" ")) : AcSaves::SwiftWindowFile());
		}));

	FAutoConsoleCommandWithWorld CmdSave(TEXT("ac.Save"), TEXT("Save the session now (in the background)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) {
			if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(World))
			{
				UE_LOG(LogAutocraft, Log, TEXT("save: %s"), Sim->Save() ? *WindowFile() : TEXT("not saved (a fixture or -AcNoSave)"));
			}
		}));
}

// MARK: - Subsystem

UAcGameFlowSubsystem* UAcGameFlowSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UAcGameFlowSubsystem>() : nullptr;
}

bool UAcGameFlowSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UAcGameFlowSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAcGameFlowSubsystem, STATGROUP_Tickables);
}

void UAcGameFlowSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const TCHAR* Cmd = FCommandLine::Get();
	// Listen before the sim loads its session (in its OnWorldBeginPlay).
	UAcSimSubsystem* Sim = Collection.InitializeDependency<UAcSimSubsystem>();
	if (Sim) StartedHandle = Sim->OnGameStarted.AddUObject(this, &UAcGameFlowSubsystem::OnGameStarted);

	FString Steps;
	if (FParse::Value(Cmd, TEXT("AcFlowTest="), Steps, false))
	{
		Test.Emplace();
		Steps.ParseIntoArray(Test->Steps, TEXT(","));
		UE_LOG(LogAutocraft, Log, TEXT("flowtest: %s in %s"), *Steps, *AcSaves::Directory());
		// What is on disk before the game opens, and what is imported.
		Test->Before = ReadSession(WindowFile());
		FString ImportPath;
		if (FParse::Value(Cmd, TEXT("AcImport="), ImportPath))
		{
			Test->Expected = ReadSession(FPaths::ConvertRelativePathToFull(FPaths::LaunchDir(), ImportPath));
		}
	}
	// The perf panel as last left (Swift `showPerf`); the command line wins.
	static bool bAppliedPerf = false;
	if (!bAppliedPerf && UAcSettings::Remembers())
	{
		bAppliedPerf = true;
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("ac.PerfPanel")))
		{
			V->Set(UAcSettings::Get().bShowPerf ? 1 : 0, ECVF_SetByGameSetting);
		}
	}
	if (FSlateApplication::IsInitialized())
	{
		Keys = MakeShared<FAcFlowKeys>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(Keys);
	}
}

void UAcGameFlowSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	const TCHAR* Cmd = FCommandLine::Get();
	FString Wanted;
	if (FParse::Value(Cmd, TEXT("AcNewGameDialog="), Wanted, false) || FParse::Param(Cmd, TEXT("AcNewGameDialog")))
	{
		bOpenWhenReady = true;
		OpenMessage = TEXT("Pick a map and how big it is.");
		TArray<FString> Parts;
		Wanted.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() > 0)
		{
			if (const std::optional<ac::MapChoice> Map = UAcSimSubsystem::ParseMap(Parts[0]))
			{
				FAcNewGameChoice C = CurrentChoice();
				C.Map = *Map;
				C.Teams = Parts.Num() > 1 ? Parts[1] : SAcNewGame::TeamOptions(Map->players)[0];
				OpenChoice = C;
			}
		}
		if (UAcShotSubsystem* Shot = UAcShotSubsystem::Get(this))
		{
			Shot->Hold();
			bShotHeld = true;
		}
	}
	else if (const AAcGameMode* RunMode = InWorld.GetAuthGameMode<AAcGameMode>(); !Test && !FParse::Param(Cmd, TEXT("AcNoHome"))
		&& (FParse::Param(Cmd, TEXT("AcHome")) || (RunMode && RunMode->GetRunMode() == EAcRunMode::Game && !FParse::Param(Cmd, TEXT("unattended")))))
	{
		// The launcher: the home screen over the paused game (-AcHome for
		// headless shots; -AcNoHome straight into the game).
		bHomeWhenReady = true;
	}
	else if (const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this); Sim && Sim->NeedsMapChoice())
	{
		const AAcGameMode* Mode = InWorld.GetAuthGameMode<AAcGameMode>();
		if (!Mode || Mode->GetRunMode() == EAcRunMode::Game)
		{
			// Swift `startWindowMode`: no map remembered, so ask.
			bOpenWhenReady = true;
			OpenMessage = TEXT("Pick a map and how big it is.");
		}
	}
}

void UAcGameFlowSubsystem::Deinitialize()
{
	if (Keys && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(Keys);
	Keys.Reset();
	CloseNewGame();
	CloseMenu();
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this)) Sim->OnGameStarted.Remove(StartedHandle);
	if (UAcSettings::Remembers())
	{
		if (const IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("ac.PerfPanel")))
		{
			UAcSettings::Get().bShowPerf = V->GetInt() != 0;
		}
		UAcSettings::Store();
	}
	Super::Deinitialize();
}

void UAcGameFlowSubsystem::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bOpenWhenReady && OpenNewGame(OpenMessage, OpenChoice)) bOpenWhenReady = false;
	if (bHomeWhenReady)
	{
		const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
		if (Sim && Sim->IsRunning())
		{
			bHasGame = bHasGame || Sim->WasResumed();
			if (OpenMenu(EAcMenuMode::Home))
			{
				bHomeWhenReady = false;
				if (FParse::Param(FCommandLine::Get(), TEXT("AcPauseMenu"))) Menu->SetMode(EAcMenuMode::Pause);
				// -AcSettingsMenu: its Settings page (stills).
				if (FParse::Param(FCommandLine::Get(), TEXT("AcSettingsMenu")))
				{
					Menu->OpenSettings();
				}
			}
		}
	}
	if (Menu && Menu->Mode() == EAcMenuMode::Home)
	{
		// The home screen's song line (the music player ticks while paused).
		const UAcMusicPlayer* Music = GetWorld() && GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UAcMusicPlayer>() : nullptr;
		const TOptional<FAcNowPlaying> Np = Music ? Music->NowPlaying() : TOptional<FAcNowPlaying>();
		Menu->SetNowPlaying(Np && !Np->bAd ? Np->Title : FString());
	}
	if (bShotHeld && Dialog && Dialog->PreviewReady())
	{
		bShotHeld = false;
		if (UAcShotSubsystem* Shot = UAcShotSubsystem::Get(this)) Shot->Release();
	}
	if (Test) TickFlowTest();
}

// MARK: - The game

FAcNewGameChoice UAcGameFlowSubsystem::CurrentChoice() const
{
	FAcNewGameChoice C;
	const UAcSettings& S = UAcSettings::Get();
	C.bAI = S.bNewGameAI;
	C.bFog = S.bNewGameFog;
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (Sim && Sim->IsRunning() && Sim->Choice())
	{
		C.Map = *Sim->Choice();
		C.Teams = AcSaves::TeamsText(Sim->State().teams());
		if (C.Map.players == 2) C.Teams = TEXT("1v1");
		C.bAI = Sim->IsAIOn();
		C.bFog = Sim->IsFogOn();
	}
	else if (const std::optional<ac::MapChoice> Last = UAcSimSubsystem::ParseMap(S.LastMap))
	{
		C.Map = *Last;
		C.Teams = S.LastTeams;
	}
	if (!SAcNewGame::TeamOptions(C.Map.players).Contains(C.Teams)) C.Teams = SAcNewGame::TeamOptions(C.Map.players)[0];
	return C;
}

void UAcGameFlowSubsystem::OnGameStarted(UAcSimSubsystem& Sim)
{
	Remember(Sim);
	if (Test) Test->GamesStarted += 1;
}

void UAcGameFlowSubsystem::Remember(const UAcSimSubsystem& Sim)
{
	if (!UAcSettings::Remembers() || Sim.IsFixture() || !Sim.Choice()) return;
	UAcSettings& S = UAcSettings::Get();
	const FString Map = AcSaves::ShortName(*Sim.Choice());
	const FString Teams = AcSaves::TeamsText(Sim.State().teams());
	if (S.LastMap == Map && S.LastTeams == Teams) return;
	S.LastMap = Map;
	S.LastTeams = Teams;
	UAcSettings::Store();
}

void UAcGameFlowSubsystem::LeaveDriving()
{
	if (AAcPilotPawn* Pilot = AAcPilotPawn::Find(this); Pilot && Pilot->Driving()) Pilot->Leave();
}

void UAcGameFlowSubsystem::StartNewGame(const FAcNewGameChoice& Choice)
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim) return;
	CloseNewGame();
	LeaveDriving();
	UE_LOG(LogAutocraft, Log, TEXT("new game: %s, %s, AI %s, fog %s"), *AcSaves::Title(Choice.Map), *SAcNewGame::TeamTitle(Choice.Teams),
		Choice.bAI ? TEXT("on") : TEXT("off"), Choice.bFog ? TEXT("on") : TEXT("off"));
	UAcSettings& S = UAcSettings::Get();
	S.bNewGameAI = Choice.bAI;
	S.bNewGameFog = Choice.bFog;
	Sim->NewGame(Choice.Map, Choice.Map.players > 2 ? AcSaves::ParseTeams(Choice.Teams) : std::nullopt, Choice.bAI, Choice.bFog);
	if (UAcSettings::Remembers()) UAcSettings::Store();
	bHasGame = true;
	if (Menu)
	{
		// From the home screen: into the new game, running.
		bPausedBefore = false;
		CloseMenu();
	}
}

void UAcGameFlowSubsystem::Restart()
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || !Sim->IsRunning()) return;
	CloseNewGame();
	LeaveDriving();
	Sim->Restart();
}

bool UAcGameFlowSubsystem::Import(const FString& Path)
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim) return false;
	LeaveDriving();
	FString Error;
	if (!Sim->ImportSession(Path, &Error))
	{
		UE_LOG(LogAutocraft, Warning, TEXT("import: %s"), *Error);
		return false;
	}
	CloseNewGame();
	bHasGame = true;
	if (Menu)
	{
		bPausedBefore = false;
		CloseMenu();
	}
	return true;
}

// MARK: - The dialog

bool UAcGameFlowSubsystem::OpenNewGame(const FString& Message, TOptional<FAcNewGameChoice> Initial)
{
	if (Dialog)
	{
		if (Initial) Dialog->SetChoice(*Initial);
		return true;
	}
	const AAcHUD* Hud = AAcHUD::Get(this);
	const TSharedPtr<SAcRoot> Root = Hud ? Hud->Root() : nullptr;
	if (!Root) return false;
	FString ImportNote;
	const FString Swift = AcSaves::SwiftWindowFile();
	if (IFileManager::Get().FileExists(*Swift))
	{
		const FDateTime When = IFileManager::Get().GetTimeStamp(*Swift);
		ImportNote = FString::Printf(TEXT("Swift game: %s, saved %s"), *Swift.Replace(*FPaths::Combine(FPlatformProcess::UserHomeDir(), TEXT("")), TEXT("~/")),
			*When.ToString(TEXT("%Y-%m-%d %H:%M")));
	}
	TWeakObjectPtr<UAcGameFlowSubsystem> Weak(this);
	Root->AddLayer(NewGameLayer)
	[
		SAssignNew(Dialog, SAcNewGame)
		.Initial(Initial ? *Initial : CurrentChoice())
		.Message(Message)
		.ImportNote(ImportNote)
		.OnStart_Lambda([Weak](const FAcNewGameChoice& C) { if (Weak.IsValid()) Weak->StartNewGame(C); })
		.OnCancel_Lambda([Weak]() { if (Weak.IsValid()) Weak->CloseNewGame(); })
		.OnImport_Lambda([Weak]() { if (Weak.IsValid()) Weak->Import(AcSaves::SwiftWindowFile()); })
	];
	if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetKeyboardFocus(Dialog, EFocusCause::SetDirectly);
	// Modal: the game waits (an alert stops Swift's timer), nothing else takes input.
	BeginModal();
	UE_LOG(LogAutocraft, Log, TEXT("new game: dialog open"));
	return true;
}

void UAcGameFlowSubsystem::CloseNewGame()
{
	if (!Dialog) return;
	if (const AAcHUD* Hud = AAcHUD::Get(this))
	{
		if (const TSharedPtr<SAcRoot> Root = Hud->Root()) Root->RemoveLayer(Dialog.ToSharedRef());
	}
	Dialog.Reset();
	EndModal();
}

void UAcGameFlowSubsystem::BeginModal()
{
	if (ModalDepth++ > 0) return;
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this))
	{
		bPausedBefore = Sim->IsPaused();
		Sim->SetPaused(true);
	}
	SetBlocked(true);
}

void UAcGameFlowSubsystem::EndModal()
{
	if (ModalDepth == 0 || --ModalDepth > 0) return;
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this)) Sim->SetPaused(bPausedBefore);
	SetBlocked(false);
	if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocusToGameViewport();
}

// MARK: - The launcher (home screen, pause menu)

bool UAcGameFlowSubsystem::OpenMenu(const EAcMenuMode Mode)
{
	if (Menu)
	{
		Menu->SetMode(Mode);
		RefreshMenuGame();
		return true;
	}
	const AAcHUD* Hud = AAcHUD::Get(this);
	const TSharedPtr<SAcRoot> Root = Hud ? Hud->Root() : nullptr;
	if (!Root) return false;
	if (Mode == EAcMenuMode::Home) LeaveDriving();
	TWeakObjectPtr<UAcGameFlowSubsystem> Weak(this);
	Root->AddLayer(MenuLayer)
	[
		SAssignNew(Menu, SAcMenu)
		.Mode(Mode)
		.OnPick_Lambda([Weak](EAcMenuAction A) { if (Weak.IsValid()) Weak->OnMenuPick(A); })
	];
	RefreshMenuGame();
	BeginModal();
	UE_LOG(LogAutocraft, Log, TEXT("menu: %s"), Mode == EAcMenuMode::Home ? TEXT("home") : TEXT("paused"));
	return true;
}

void UAcGameFlowSubsystem::CloseMenu()
{
	if (!Menu) return;
	if (const AAcHUD* Hud = AAcHUD::Get(this))
	{
		if (const TSharedPtr<SAcRoot> Root = Hud->Root()) Root->RemoveLayer(Menu.ToSharedRef());
	}
	Menu.Reset();
	EndModal();
	UE_LOG(LogAutocraft, Log, TEXT("menu: closed"));
}

void UAcGameFlowSubsystem::RefreshMenuGame()
{
	if (!Menu) return;
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!bHasGame || !Sim || !Sim->IsRunning() || !Sim->Choice())
	{
		Menu->SetGame(FString(), FString());
		return;
	}
	const FAcNewGameChoice C = CurrentChoice();
	const int32 Seconds = (int32)Sim->GameTime();
	const FString Played = Seconds >= 3600 ? FString::Printf(TEXT("%d:%02d:%02d"), Seconds / 3600, Seconds / 60 % 60, Seconds % 60)
	                                       : FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60);
	Menu->SetGame(AcSaves::Title(*Sim->Choice()),
		FString::Printf(TEXT("%s  ·  %s played  ·  AI %s  ·  fog %s"), *SAcNewGame::TeamTitle(C.Teams), *Played, C.bAI ? TEXT("on") : TEXT("off"),
			C.bFog ? TEXT("on") : TEXT("off")));
}

void UAcGameFlowSubsystem::OnMenuPick(const EAcMenuAction Action)
{
	switch (Action)
	{
	case EAcMenuAction::Resume:
		CloseMenu();
		break;
	case EAcMenuAction::NewGame:
		OpenNewGame();
		break;
	case EAcMenuAction::QuitToHome:
		if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this)) Sim->Save();
		bHasGame = true;
		OpenMenu(EAcMenuMode::Home);
		break;
	case EAcMenuAction::Settings:
		// The widget's own page.
		break;
	case EAcMenuAction::QuitApp:
		// The sim saves on the way out (Deinitialize).
		FPlatformMisc::RequestExit(false, TEXT("AcMenu"));
		break;
	}
}

bool UAcGameFlowSubsystem::WantsPauseOnEscape() const
{
	const UWorld* World = GetWorld();
	const AAcGameMode* Mode = World ? World->GetAuthGameMode<AAcGameMode>() : nullptr;
	if (!Mode || Mode->GetRunMode() != EAcRunMode::Game || Dialog || Menu) return false;
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || !Sim->IsRunning()) return false;
	// Escape's other jobs come first: shut the map, drop the selection, leave the unit.
	if (const UAcCommandMapSubsystem* Map = UAcCommandMapSubsystem::Get(this); Map && Map->IsOpen()) return false;
	if (const UAcPointer* Pointer = UAcPointer::Get(this); Pointer && Pointer->Selected().IsSet()) return false;
	if (const AAcPilotPawn* Pilot = AAcPilotPawn::Find(this); Pilot && Pilot->Driving()) return false;
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->ViewportConsole && GEngine->GameViewport->ViewportConsole->ConsoleActive())
	{
		return false;
	}
	return true;
}

void UAcGameFlowSubsystem::SetBlocked(const bool bBlocked)
{
	if (UAcPointer* Pointer = UAcPointer::Get(this)) Pointer->SetBlocked(bBlocked);
	if (AAcRtsPawn* Pawn = AAcRtsPawn::Get(this)) Pawn->SetInputBlocked(bBlocked);
	if (UAcCommandsSubsystem* Commands = UAcCommandsSubsystem::Get(this)) Commands->SetBlocked(bBlocked);
}

// MARK: - -AcFlowTest

void UAcGameFlowSubsystem::Check(const bool bOk, const FString& What)
{
	const FString Step = Test->Steps.IsValidIndex(Test->Step) ? Test->Steps[Test->Step] : TEXT("?");
	UE_LOG(LogAutocraft, Display, TEXT("flowtest: %s: %s %s"), *Step, *What, bOk ? TEXT("ok") : TEXT("FAILED"));
	if (!bOk) Test->Failures += 1;
}

void UAcGameFlowSubsystem::NextStep()
{
	Test->Step += 1;
	Test->Phase = 0;
	Test->PhaseAt = FPlatformTime::Seconds();
}

void UAcGameFlowSubsystem::TickFlowTest()
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || !Sim->IsRunning()) return;
	FFlowTest& T = *Test;
	const double Now = FPlatformTime::Seconds();
	if (T.PhaseAt == 0) T.PhaseAt = Now;
	if (T.Step >= T.Steps.Num())
	{
		if (T.Step == T.Steps.Num())
		{
			UE_LOG(LogAutocraft, Display, TEXT("flowtest: done, %d failed"), T.Failures);
			T.Step += 1;
			FPlatformMisc::RequestExit(false, TEXT("AcFlowTest"));
		}
		return;
	}
	const FString Step = T.Steps[T.Step].TrimStartAndEnd();
	const ac::GameState& S = Sim->State();
	auto Since = [&] { return Now - T.PhaseAt; };
	auto Advance = [&] { T.Phase += 1; T.PhaseAt = Now; T.GameAt = S.time; };
	auto SavedEqual = [&](const ac::Session& Want, const FString& What) {
		// The file holds exactly the session's JSON, and reads back to it
		// (dates are whole seconds in JSON, so compare the encodings).
		const std::optional<std::string> File = ac::readFile(std::string(TCHAR_TO_UTF8(*WindowFile())));
		const std::optional<ac::Session> Disk = ReadSession(WindowFile());
		const std::string A = ac::SessionStore::encode(Want);
		const bool bBytes = File && *File == A;
		const bool bBack = Disk && ac::SessionStore::encode(*Disk) == A && Disk->state == Want.state;
		Check(bBytes && bBack, FString::Printf(TEXT("%s: %s (%d bytes, game %s at %.2f s, %d units, %d buildings)"), *What,
			bBytes && bBack ? TEXT("the file is the session's JSON and reads back to the same state")
			: bBytes ? TEXT("the file is the session's JSON but reads back different") : TEXT("the file differs"), File ? (int32)File->size() : 0,
			*Text(Want.game.value_or("")), Want.state.time, (int32)Want.state.units.size(), (int32)Want.state.structures.size()));
	};

	if (Step == TEXT("save"))
	{
		// A few seconds of play, a save, the file read back.
		if (T.Phase == 0) { Advance(); return; }
		if (T.Phase == 1 && S.time - T.GameAt >= 3)
		{
			Check(Sim->Save(), TEXT("Save() queued on a worker"));
			T.Expected = Sim->Session();
			Advance();
			return;
		}
		if (T.Phase == 2 && Since() >= 1.0)
		{
			SavedEqual(*T.Expected, TEXT("save → load"));
			NextStep();
		}
		return;
	}
	if (Step == TEXT("resume"))
	{
		// The window game on disk before launch is the one being played.
		const ac::Session& Now2 = Sim->Session();
		Check(T.Before.has_value(), TEXT("a saved window game was on disk"));
		if (T.Before)
		{
			Check(Now2.game == T.Before->game && Now2.mapName == T.Before->mapName,
				FString::Printf(TEXT("resumed game %s on %s"), *Text(Now2.game.value_or("")), *Text(Now2.mapName)));
			// What is played is the file's game as a fresh sim takes it in
			// (building a sim refreshes intel and re-plans walking units'
			// paths, so compare against that, not the raw file).
			const ac::Simulation Fresh(T.Before->state, Sim->Map(), {}, Sim->IsFogOn());
			const std::string Was = ac::SessionStore::encode(Fresh.state), Is = ac::SessionStore::encode(S);
			Check(Was == Is && S.time == T.Before->state.time,
				FString::Printf(TEXT("its state as saved, through the sim (time %.2f s, %d units, %d bytes)"), T.Before->state.time,
				(int32)T.Before->state.units.size(), (int32)Was.size()));
			if (Was != Is)
			{
				size_t At = 0;
				while (At < Was.size() && At < Is.size() && Was[At] == Is[At]) ++At;
				const size_t From = At > 200 ? At - 200 : 0;
				UE_LOG(LogAutocraft, Display, TEXT("flowtest: resume: differs at %d: was ...%s... is ...%s..."), (int32)At,
					*Text(Was.substr(From, 300)), *Text(Is.substr(From, 300)));
			}
			Check(S.time >= T.Before->state.time, FString::Printf(TEXT("play goes on from %.2f s (now %.2f s)"), T.Before->state.time, S.time));
		}
		NextStep();
		return;
	}
	if (Step == TEXT("import"))
	{
		// -AcImport=PATH: the Swift game as the window game, then saved as ours.
		if (T.Phase == 0)
		{
			const ac::Session& Sess = Sim->Session();
			Check(T.Expected.has_value(), TEXT("the import file reads"));
			if (T.Expected)
			{
				Check(Sess.id == AcSaves::WindowId && Sess.signature == AcSaves::Signature(*Sim->Choice()),
					FString::Printf(TEXT("imported as \"%s\" (%s)"), *Text(Sess.id), *Text(Sess.signature)));
				// The file's game as a fresh sim takes it in (see resume).
				const ac::Simulation Fresh(T.Expected->state, Sim->Map(), {}, Sim->IsFogOn());
				Check(ac::SessionStore::encode(Fresh.state) == ac::SessionStore::encode(S) && Sess.game == T.Expected->game
						&& Sess.mapName == T.Expected->mapName && S.time == T.Expected->state.time,
					FString::Printf(TEXT("the same game: %s on %s at %.1f min, %d units, %d buildings, score %d:%d"),
						*Text(Sess.game.value_or("")), *Text(Sess.mapName), Sess.state.time / 60, (int32)Sess.state.units.size(),
						(int32)Sess.state.structures.size(), Sess.state.score.size() > 0 ? (int32)Sess.state.score[0] : -1,
						Sess.state.score.size() > 1 ? (int32)Sess.state.score[1] : -1));
				Check(Sim->IsAIOn() == T.Expected->ai.value_or(true), TEXT("its AI setting"));
			}
			Advance();
			return;
		}
		if (T.Phase == 1 && S.time - T.GameAt >= 3)
		{
			Check(S.time > T.Expected->state.time, FString::Printf(TEXT("plays on (%.2f → %.2f s)"), T.Expected->state.time, S.time));
			Sim->Save();
			T.Expected = Sim->Session();
			Advance();
			return;
		}
		if (T.Phase == 2 && Since() >= 1.0)
		{
			SavedEqual(*T.Expected, TEXT("saved as the Unreal game's"));
			NextStep();
		}
		return;
	}
	if (Step == TEXT("autosave"))
	{
		// Every 15 s of real time, without being asked.
		if (T.Phase == 0)
		{
			T.FileTime = IFileManager::Get().GetTimeStamp(*WindowFile());
			Advance();
			return;
		}
		const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*WindowFile());
		if (Stamp > T.FileTime)
		{
			Check(Since() <= 16.5, FString::Printf(TEXT("autosaved after %.1f s"), Since()));
			NextStep();
		}
		else if (Since() > 20)
		{
			Check(false, TEXT("no autosave within 20 s"));
			NextStep();
		}
		return;
	}
	if (Step == TEXT("fog"))
	{
		if (T.Phase == 0) { Advance(); return; }
		if (T.Phase == 1 && S.time - T.GameAt >= 1)
		{
			const int32 Total = (int32)S.units.size() + (int32)S.structures.size();
			Sim->SetFog(false);
			const ac::GameState& Shown = Sim->Shown();
			Check(!Sim->IsFogOn() && Sim->Session().fog == false, TEXT("session fog off: no vision in the core"));
			Check((int32)Shown.units.size() + (int32)Shown.structures.size() == Total,
				FString::Printf(TEXT("everything shown (%d of %d)"), (int32)(Shown.units.size() + Shown.structures.size()), Total));
			Check(T.GamesStarted >= 1 && Sim->State().time >= T.GameAt, TEXT("the same game goes on"));
			Advance();
			return;
		}
		if (T.Phase == 2 && S.time - T.GameAt >= 1)
		{
			Sim->SetFog(true);
			const ac::GameState& Shown = Sim->Shown();
			const int32 Total = (int32)S.units.size() + (int32)S.structures.size();
			Check(Sim->IsFogOn() && Sim->Session().fog == true, TEXT("session fog on again"));
			Check((int32)Shown.units.size() + (int32)Shown.structures.size() < Total,
				FString::Printf(TEXT("the others hidden again (%d of %d shown)"), (int32)(Shown.units.size() + Shown.structures.size()), Total));
			NextStep();
		}
		return;
	}
	if (Step == TEXT("menu"))
	{
		// The launcher through the real key path: Escape pauses, Escape
		// resumes, Quit to home saves, New Game from home starts a game.
		auto Press = [&](const FKey& K) {
			if (!Keys || !FSlateApplication::IsInitialized()) return false;
			return Keys->HandleKeyDownEvent(FSlateApplication::Get(), FKeyEvent(K, FModifierKeysState(), 0, false, 0, 0));
		};
		if (T.Phase == 0)
		{
			if (UAcPointer* Pointer = UAcPointer::Get(this)) Pointer->Select({});
			CloseMenu();
			Check(!Sim->IsPaused() && Press(EKeys::Escape) && Menu && Menu->Mode() == EAcMenuMode::Pause && Sim->IsPaused(),
				TEXT("Escape opens the pause menu and the game waits"));
			Check(Press(EKeys::Escape) && !Menu && !Sim->IsPaused(), TEXT("Escape again resumes"));
			Press(EKeys::Escape);
			Press(EKeys::Down);  // Resume, Settings
			Press(EKeys::Enter);
			{
				UAcSettings& Set = UAcSettings::Get();
				const float Was = Set.SfxVolume;
				Press(EKeys::Down);  // Master, Music, Effects
				Press(EKeys::Down);
				Press(EKeys::Left);
				const float Lower = Set.SfxVolume;
				Press(EKeys::Right);
				Check(Menu && Menu->InSettings() && Lower < Was && FMath::IsNearlyEqual(Set.SfxVolume, Was, 0.051f),
					TEXT("Settings: Left/Right move the effects volume"));
				Set.SfxVolume = Was;
				Check(Press(EKeys::Escape) && Menu && !Menu->InSettings() && Menu->Mode() == EAcMenuMode::Pause,
					TEXT("Escape leaves the settings for the pause menu"));
			}
			Press(EKeys::Down);  // back on Settings: Quit to home
			T.FileTime = IFileManager::Get().GetTimeStamp(*WindowFile());
			Check(Press(EKeys::Enter) && Menu && Menu->Mode() == EAcMenuMode::Home && Sim->IsPaused(), TEXT("Quit to home: the home screen, paused"));
			Advance();
			return;
		}
		if (T.Phase == 1 && Since() >= 1.0)
		{
			SavedEqual(Sim->Session(), TEXT("quit to home saved the game"));
			const double At = S.time;
			Check(Sim->IsPaused() && FMath::IsNearlyEqual(At, T.GameAt), TEXT("the game waits behind the home screen"));
			Check(!Press(EKeys::Escape) || Menu, TEXT("Escape keeps the home screen"));
			Menu->Move(1);  // Resume, New Game
			Menu->PickHighlighted();
			Check(Dialog.IsValid() && Menu.IsValid(), TEXT("New Game opens the dialog over home"));
			Press(EKeys::Escape);
			Check(!Dialog && Menu && Sim->IsPaused(), TEXT("Cancel goes back to home"));
			Menu->SetMode(EAcMenuMode::Home);
			Menu->Move(1);
			Menu->PickHighlighted();
			T.GameId = Text(Sim->Session().game.value_or(""));
			Press(EKeys::Enter);
			Check(!Dialog && !Menu && !Sim->IsPaused() && Text(Sim->Session().game.value_or("")) != T.GameId,
				TEXT("Start: a new game, running, menus gone"));
			Advance();
			return;
		}
		if (T.Phase == 2 && S.time - T.GameAt >= 1)
		{
			Check(true, TEXT("the new game runs"));
			NextStep();
		}
		return;
	}
	if (Step == TEXT("newgame"))
	{
		if (T.Phase == 0)
		{
			FAcNewGameChoice C;
			C.Map = ac::MapChoice{ac::MapStyle::badlands, ac::MapSize::small, 4};
			C.Teams = TEXT("2v2");
			C.bAI = true;
			C.bFog = true;
			T.GameId = Text(Sim->Session().game.value_or(""));
			StartNewGame(C);
			const ac::GameState& N = Sim->State();
			Check(Sim->Map().name == AcSaves::Build(C.Map, false).name && N.players.size() == 4,
				FString::Printf(TEXT("new game on %s, %d players"), *Text(Sim->Map().name), (int32)N.players.size()));
			Check(AcSaves::TeamsText(N.teams()) == TEXT("2v2") && N.allied(0, 1) && N.hostile(0, 2),
				FString::Printf(TEXT("teams %s"), *AcSaves::TeamsText(N.teams())));
			Check(Text(Sim->Session().game.value_or("")) != T.GameId && N.time == 0, TEXT("a new game id, time 0"));
			Advance();
			return;
		}
		if (T.Phase == 1 && S.time - T.GameAt >= 2)
		{
			T.GameId = Text(Sim->Session().game.value_or(""));
			const std::string Map = Sim->Map().name;
			Restart();
			const ac::GameState& N = Sim->State();
			Check(Sim->Map().name == Map && AcSaves::TeamsText(N.teams()) == TEXT("2v2") && N.players.size() == 4,
				TEXT("restart: same map, teams kept"));
			Check(Text(Sim->Session().game.value_or("")) != T.GameId && N.time == 0, TEXT("restart: a new game id, time 0"));
			Advance();
			return;
		}
		if (T.Phase == 2 && Since() >= 1.0)
		{
			SavedEqual(Sim->Session(), TEXT("the new game is on disk"));
			NextStep();
		}
		return;
	}
	if (Step == TEXT("victory"))
	{
		// Stage the end (no game played out): player 0 won half a second
		// before the lap ends, so the next game follows at once.
		if (T.Phase == 0)
		{
			ac::GameState& W = Sim->Simulation().state;
			W.winner = 0;
			W.endedAt = W.time - ac::Rules::victoryLap + 0.5;
			T.GameId = Text(Sim->Session().game.value_or(""));
			T.GamesStarted = 0;
			Advance();
			return;
		}
		if (T.Phase == 1 && T.GamesStarted > 0)
		{
			const ac::GameState& N = Sim->State();
			Check(Text(Sim->Session().game.value_or("")) != T.GameId && !N.endedAt && !N.winner && N.time < 1.0,
				FString::Printf(TEXT("victory lap → next game %s at %.2f s"), *Text(Sim->Session().game.value_or("")), N.time));
			NextStep();
		}
		else if (T.Phase == 1 && Since() > 5)
		{
			Check(false, TEXT("no next game after the victory lap"));
			NextStep();
		}
		return;
	}
	Check(false, FString::Printf(TEXT("unknown step %s"), *Step));
	NextStep();
}
