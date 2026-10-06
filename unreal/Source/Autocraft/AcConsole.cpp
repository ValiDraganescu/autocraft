#include "AcConsole.h"

#include "AcHUD.h"
#include "AcLog.h"
#include "AcRtsPawn.h"
#include "AcSimSubsystem.h"
#include "SAcConsole.h"
#include "SAcRoot.h"

#include "Commander.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Simulation.h"
#include "Types.h"

namespace
{
	/// `-AcConsoleDemo`: the buttons a Citadel's card shows in the Swift
	/// `windowshot --select citadel` (for shots until D5 makes the real ones).
	TArray<FAcCardButton> DemoButtons()
	{
		auto B = [](int32 Slot, const TCHAR* Key, const TCHAR* Title, const TCHAR* Icon)
		{
			FAcCardButton X;
			X.Slot = Slot;
			X.Key = Key;
			X.Title = Title;
			X.Icon = Icon;
			return X;
		};
		TArray<FAcCardButton> Out = {B(0, TEXT("1"), TEXT("Prospector"), TEXT("prospector")), B(5, TEXT("2"), TEXT("Ore"), TEXT("ore")),
			B(6, TEXT("3"), TEXT("Balanced"), TEXT("")), B(7, TEXT("4"), TEXT("MH"), TEXT("derrick")),
			B(10, TEXT("5"), TEXT("Build"), TEXT("act.build")), B(11, TEXT("6"), TEXT("\u2212100 ore"), TEXT("")),
			B(12, TEXT("7"), TEXT("+100 ore"), TEXT("")), B(13, TEXT("8"), TEXT("\u2212100 MH"), TEXT("")),
			B(14, TEXT("9"), TEXT("+100 MH"), TEXT(""))};
		Out[0].bQueues = true;
		Out[0].Ore = 50;
		Out[0].Time = 12.0;
		Out[0].Why = FString(TEXT("Not enough ore: click to queue it"));
		Out[2].bLit = true;
		return Out;
	}

	FAutoConsoleCommandWithWorldAndArgs GConsoleSelect(TEXT("ac.ConsoleSelect"),
		TEXT("Select one of your buildings for the console: a kind (citadel, garrison...), an id, or none."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UAcConsoleSubsystem* C = UAcConsoleSubsystem::Get(World);
			if (!C) return;
			if (Args.IsEmpty() || Args[0] == TEXT("none")) C->Select({});
			else C->SelectByName(Args[0]);
		}));

	FAutoConsoleCommandWithWorldAndArgs GConsoleStyle(TEXT("ac.ConsoleStyle"),
		TEXT("The console's look: rts, cockpit or cab, and the cab's lamps: idle, working or locked."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UAcConsoleSubsystem* C = UAcConsoleSubsystem::Get(World);
			if (!C || Args.IsEmpty()) return;
			C->SetLook(UAcConsoleSubsystem::StyleNamed(Args[0]),
				UAcConsoleSubsystem::LampsNamed(Args.Num() > 1 ? Args[1] : FString()), Args[0] != TEXT("cab") && Args[0] != TEXT("cockpit"));
		}));

	/// The local player's team has an AI commander (`GameController.commanded`).
	bool Commanded(const ac::Simulation& Sim, int64 Player)
	{
		const ac::GameState& S = Sim.state;
		for (const ac::Commander& C : Sim.commanders)
		{
			if (S.team(C.player) == S.team(Player)) return true;
		}
		return false;
	}
}

UAcConsoleSubsystem* UAcConsoleSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UAcConsoleSubsystem>() : nullptr;
}

bool UAcConsoleSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UAcConsoleSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bDisabled = FParse::Param(FCommandLine::Get(), TEXT("AcNoConsole"));
	bDemo = FParse::Param(FCommandLine::Get(), TEXT("AcConsoleDemo"));
	FParse::Value(FCommandLine::Get(), TEXT("AcSelect="), PendingSelect);
	FParse::Value(FCommandLine::Get(), TEXT("AcConsoleNote="), HeldNote);
	FString StyleName, LampsName;
	if (FParse::Value(FCommandLine::Get(), TEXT("AcConsoleStyle="), StyleName))
	{
		FParse::Value(FCommandLine::Get(), TEXT("AcCabLamps="), LampsName);
		SetLook(StyleNamed(StyleName), LampsNamed(LampsName), StyleNamed(StyleName) == EAcConsoleStyle::Rts);
	}
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || bDisabled) return;
	FrameHandle = Sim->AddFrameListener(EAcFrameStage::Hud, FAcFrameEvent::FDelegate::CreateUObject(this, &UAcConsoleSubsystem::OnFrame));
	StartedHandle = Sim->OnGameStarted.AddUObject(this, &UAcConsoleSubsystem::OnGameStarted);
}

void UAcConsoleSubsystem::Deinitialize()
{
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this))
	{
		Sim->RemoveFrameListener(EAcFrameStage::Hud, FrameHandle);
		Sim->OnGameStarted.Remove(StartedHandle);
	}
	if (AAcHUD* Hud = AAcHUD::Get(this))
	{
		if (TSharedPtr<SAcRoot> Root = Hud->Root())
		{
			if (ConsoleWidget) Root->RemoveLayer(ConsoleWidget.ToSharedRef());
			if (ViewFrameWidget) Root->RemoveLayer(ViewFrameWidget.ToSharedRef());
		}
	}
	ConsoleWidget.Reset();
	ViewFrameWidget.Reset();
	Super::Deinitialize();
}

bool UAcConsoleSubsystem::Attach()
{
	if (ConsoleWidget) return true;
	AAcHUD* Hud = AAcHUD::Get(this);
	const TSharedPtr<SAcRoot> Root = Hud ? Hud->Root() : nullptr;
	if (!Root) return false;
	Root->AddLayer(AcHudLayer::ViewFrame)[SAssignNew(ViewFrameWidget, SAcViewFrame)];
	Root->AddLayer(AcHudLayer::Console)[SAssignNew(ConsoleWidget, SAcConsole)];
	ConsoleWidget->HoldNote(!HeldNote.IsEmpty());
	SetLook(LookStyle, LookLamps, bLookViewFrame);
	if (const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this); Sim && Sim->IsRunning()) UpdateMinimapSize(*Sim);
	UE_LOG(LogAutocraft, Log, TEXT("console: attached"));
	return true;
}

void UAcConsoleSubsystem::UpdateMinimapSize(const UAcSimSubsystem& Sim)
{
	if (!ConsoleWidget) return;
	// GameController.makeMinimap(scale: 1, height: 180) over the map's bounds.
	const ac::GroundRect& B = Sim.Map().bounds;
	ConsoleWidget->SetMinimapSize(FAcCabLayout::MinimapSizeFor(B.width(), B.depth()));
}

void UAcConsoleSubsystem::OnGameStarted(UAcSimSubsystem& Sim)
{
	SelectedId.Reset();
	UpdateMinimapSize(Sim);
}

EAcConsoleStyle UAcConsoleSubsystem::StyleNamed(const FString& Name)
{
	return Name.Equals(TEXT("cab"), ESearchCase::IgnoreCase) ? EAcConsoleStyle::Cab
		: Name.Equals(TEXT("cockpit"), ESearchCase::IgnoreCase) ? EAcConsoleStyle::Cockpit
		: EAcConsoleStyle::Rts;
}

EAcCabLamps UAcConsoleSubsystem::LampsNamed(const FString& Name)
{
	return Name.Equals(TEXT("working"), ESearchCase::IgnoreCase) ? EAcCabLamps::Working
		: Name.Equals(TEXT("locked"), ESearchCase::IgnoreCase) ? EAcCabLamps::Locked
		: EAcCabLamps::Idle;
}

void UAcConsoleSubsystem::SetLook(const EAcConsoleStyle Style, const EAcCabLamps Lamps, const bool bViewFrame)
{
	LookStyle = Style;
	LookLamps = Lamps;
	bLookViewFrame = bViewFrame;
	if (ConsoleWidget)
	{
		ConsoleWidget->SetStyle(Style);
		ConsoleWidget->SetLamps(Lamps);
	}
	if (ViewFrameWidget) ViewFrameWidget->SetVisibility(bViewFrame ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
}

void UAcConsoleSubsystem::Select(const TOptional<int64> StructureId)
{
	SelectedId = StructureId;
}

void UAcConsoleSubsystem::SelectByName(const FString& Text)
{
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || !Sim->IsRunning()) return;
	const ac::GameState& S = Sim->State();
	const int64 Me = Sim->LocalPlayer();
	if (Text.IsNumeric())
	{
		Select(FCString::Atoi64(*Text));
		return;
	}
	for (const ac::Structure& St : S.structures)
	{
		if (St.owner == Me && FAcConsoleInfo::StructureIconName((int32)St.kind).Equals(Text, ESearchCase::IgnoreCase))
		{
			Select(St.id);
			UE_LOG(LogAutocraft, Log, TEXT("console: selected %s %lld"), *Text, (long long)St.id);
			return;
		}
	}
	UE_LOG(LogAutocraft, Warning, TEXT("console: no %s of player %lld to select"), *Text, (long long)Me);
}

void UAcConsoleSubsystem::OnFrame(const FAcFrame& Frame)
{
	if (!Attach() || !Frame.State) return;
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || !Sim->IsRunning()) return;
	if (!PendingSelect.IsEmpty())
	{
		SelectByName(PendingSelect);
		PendingSelect.Reset();
	}

	// GameController.updateSelection: drop a building that died or changed hands.
	TOptional<FAcCardInfo> Info;
	if (PilotInfo)
	{
		Info = PilotInfo;
	}
	else if (SelectedId)
	{
		const std::optional<ac::Structure> St = Frame.State->structure(*SelectedId);
		if (!St || St->owner != Frame.LocalPlayer)
		{
			SelectedId.Reset();
		}
		else if (InfoSource)
		{
			Info = InfoSource(*Sim, *SelectedId);
		}
		else
		{
			Info = FAcConsoleInfo::ForStructure(Sim->Simulation(), *St, Commanded(Sim->Simulation(), Frame.LocalPlayer));
		}
	}
	if (Info && !HeldNote.IsEmpty()) Info->Note = HeldNote;
	if (Info && Info->Buttons.IsEmpty() && bDemo) Info->Buttons = DemoButtons();
	ConsoleWidget->Show(Info, FPlatformTime::Seconds());

	// The camera keeps the map's near edge above the console.
	const double Cover = ConsoleWidget->Cover();
	if (Cover > 0 && Cover != LastCover)
	{
		if (AAcRtsPawn* Pawn = AAcRtsPawn::Get(this))
		{
			Pawn->SetCover(Cover);
			LastCover = Cover;
		}
	}
}
