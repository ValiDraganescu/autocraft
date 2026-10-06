#include "AcCommands.h"

#include "AcConsole.h"
#include "AcLog.h"
#include "AcPointer.h"
#include "AcSimSubsystem.h"
#include "SAcConsole.h"

#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Simulation.h"

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GCardPress(TEXT("ac.CardPress"),
		TEXT("Press the selected building's card button N (1-based, as its hotkey); add shift and/or option."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UAcCommandsSubsystem* C = UAcCommandsSubsystem::Get(World);
			if (!C || Args.IsEmpty()) return;
			FAcCardMods Mods;
			for (int32 I = 1; I < Args.Num(); ++I)
			{
				Mods.bShift |= Args[I].Equals(TEXT("shift"), ESearchCase::IgnoreCase);
				Mods.bOption |= Args[I].Equals(TEXT("option"), ESearchCase::IgnoreCase) || Args[I].Equals(TEXT("alt"), ESearchCase::IgnoreCase);
			}
			C->Press(FCString::Atoi(*Args[0]) - 1, Mods);
		}));

	/// The digit keys in hotkey order (1-9), top row and keypad.
	const FKey& Digit(int32 N, bool bPad)
	{
		static const FKey Row[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven,
			EKeys::Eight, EKeys::Nine};
		static const FKey Pad[] = {EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour, EKeys::NumPadFive,
			EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine};
		return bPad ? Pad[N - 1] : Row[N - 1];
	}
}

UAcCommandsSubsystem* UAcCommandsSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UAcCommandsSubsystem>() : nullptr;
}

bool UAcCommandsSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

double UAcCommandsSubsystem::Now()
{
	return FPlatformTime::Seconds();
}

void UAcCommandsSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	UAcConsoleSubsystem* Console = UAcConsoleSubsystem::Get(this);
	if (!Sim || !Console) return;
	Console->SetInfoSource([this](const UAcSimSubsystem& S, int64 Id) { return MakeInfo(S, Id); });
	FrameHandle = Sim->AddFrameListener(EAcFrameStage::Hud, FAcFrameEvent::FDelegate::CreateUObject(this, &UAcCommandsSubsystem::OnFrame));

	FString List;
	if (FParse::Value(FCommandLine::Get(), TEXT("AcPress="), List))
	{
		TArray<FString> Items;
		List.ParseIntoArray(Items, TEXT("/"));
		for (FString Item : Items)
		{
			FAcCardMods Mods;
			while (Item.EndsWith(TEXT("s")) || Item.EndsWith(TEXT("o")))
			{
				(Item.EndsWith(TEXT("s")) ? Mods.bShift : Mods.bOption) = true;
				Item.LeftChopInline(1);
			}
			if (Item.IsNumeric()) Scripted.Emplace(FCString::Atoi(*Item) - 1, Mods);
		}
	}
}

void UAcCommandsSubsystem::Deinitialize()
{
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this)) Sim->RemoveFrameListener(EAcFrameStage::Hud, FrameHandle);
	if (UAcConsoleSubsystem* Console = UAcConsoleSubsystem::Get(this)) Console->SetInfoSource(nullptr);
	if (TSharedPtr<SAcConsole> W = BoundConsole.Pin()) W->SetOnCardButton(FAcOnCardButton());
	if (UAcPointer* P = BoundPointer.Get()) P->OnSelectionChanged.Remove(SelectionHandle);
	Super::Deinitialize();
}

TOptional<int64> UAcCommandsSubsystem::SelectedId() const
{
	const UAcConsoleSubsystem* Console = UAcConsoleSubsystem::Get(this);
	return Console ? Console->Selected() : TOptional<int64>();
}

TOptional<FAcCardInfo> UAcCommandsSubsystem::MakeInfo(const UAcSimSubsystem& Sim, const int64 StructureId)
{
	const ac::Simulation& S = Sim.Simulation();
	const std::optional<ac::Structure> St = S.state.structure(StructureId);
	if (!St) return {};
	return FAcCommandCard::Card(S, *St, FAcCommandCard::Commanded(S, Sim.LocalPlayer()), Card, Now());
}

void UAcCommandsSubsystem::OnSelectionChanged()
{
	// Swift `select`: the note goes; another building also drops the build
	// menu (`Card` sees the id change).
	Card.Note.Reset();
}

void UAcCommandsSubsystem::Press(const int32 Index, TOptional<FAcCardMods> Mods)
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	const TOptional<int64> Id = SelectedId();
	if (!Sim || !Sim->IsRunning() || !Id) return;
	const ac::Simulation& S = Sim->Simulation();
	const std::optional<ac::Structure> St = S.state.structure(*Id);
	if (!St || St->owner != Sim->LocalPlayer()) return;
	if (Card.For != Id)
	{
		// The card was not made for this building yet: make it now.
		FAcCommandCard::Card(S, *St, FAcCommandCard::Commanded(S, Sim->LocalPlayer()), Card, Now());
	}
	if (!Mods)
	{
		const FModifierKeysState Keys = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetModifierKeys() : FModifierKeysState();
		Mods = FAcCardMods{Keys.IsShiftDown(), Keys.IsAltDown()};
	}
	const FAcCardPress Done = FAcCommandCard::Press(S, *St, Index, *Mods, FAcCommandCard::Commanded(S, Sim->LocalPlayer()),
		Sim->LocalPlayer(), Card, Now(), [Sim](const ac::Command& C) { return Sim->Issue(C); });
	if (!Done.Log.IsEmpty()) UE_LOG(LogAutocraft, Log, TEXT("%s"), *Done.Log);
	if (Card.Note) UE_LOG(LogAutocraft, Verbose, TEXT("card: note \"%s\""), **Card.Note);
	if (Done.bCommander) OnCommanderChanged.Broadcast();
}

void UAcCommandsSubsystem::OnFrame(const FAcFrame& Frame)
{
	// The console widget and the pointer come up after begin play: hook them when they do.
	if (UAcConsoleSubsystem* Console = UAcConsoleSubsystem::Get(this))
	{
		const TSharedPtr<SAcConsole> W = Console->Console();
		if (W && W != BoundConsole.Pin())
		{
			W->SetOnCardButton(FAcOnCardButton::CreateWeakLambda(this, [this](int32 Index) { Press(Index); }));
			BoundConsole = W;
		}
	}
	if (UAcPointer* P = UAcPointer::Get(this); P && P != BoundPointer.Get())
	{
		if (UAcPointer* Old = BoundPointer.Get()) Old->OnSelectionChanged.Remove(SelectionHandle);
		SelectionHandle = P->OnSelectionChanged.AddUObject(this, &UAcCommandsSubsystem::OnSelectionChanged);
		BoundPointer = P;
	}
	if (!Scripted.IsEmpty())
	{
		// Wait until the card for the selection is up, then one press a frame.
		const TOptional<int64> Id = SelectedId();
		if (Id && Card.For == Id && !Card.Actions.IsEmpty())
		{
			const TPair<int32, FAcCardMods> Next = Scripted[0];
			Scripted.RemoveAt(0);
			UE_LOG(LogAutocraft, Log, TEXT("card: scripted press %d%s%s"), Next.Key + 1, Next.Value.bShift ? TEXT(" shift") : TEXT(""),
				Next.Value.bOption ? TEXT(" option") : TEXT(""));
			Press(Next.Key, Next.Value);
		}
	}
	PollKeys();
}

void UAcCommandsSubsystem::PollKeys()
{
	const UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!PC || !Sim || !Sim->IsRunning() || bBlocked || Sim->Simulation().pilot) return;
	const FModifierKeysState Keys = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetModifierKeys() : FModifierKeysState();
	// ⌘1-⌘8 go to a base (the RTS pawn).
	if (Keys.IsCommandDown() || Keys.IsControlDown()) return;
	const TOptional<int64> Id = SelectedId();
	if (!Id) return;
	if (PC->WasInputKeyJustPressed(EKeys::Escape))
	{
		if (UAcPointer* P = UAcPointer::Get(this)) P->Select({});
		if (UAcConsoleSubsystem* Console = UAcConsoleSubsystem::Get(this)) Console->Select({});
		return;
	}
	for (int32 N = 1; N <= 9; ++N)
	{
		if (PC->WasInputKeyJustPressed(Digit(N, false)) || PC->WasInputKeyJustPressed(Digit(N, true)))
		{
			if (N <= Card.Actions.Num()) Press(N - 1, FAcCardMods{Keys.IsShiftDown(), Keys.IsAltDown()});
			return;
		}
	}
}
