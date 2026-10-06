// See AcMusicDeck.h.
#include "AcMusicDeck.h"

#include "AcCab.h"
#include "AcHudStyle.h"
#include "AcConsole.h"
#include "AcLog.h"
#include "AcMp3Loader.h"
#include "AcSimSubsystem.h"
#include "SAcConsole.h"
#include "SAcMusicDeck.h"

#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundWave.h"

namespace
{
	const TCHAR* NameOf(const EAcMusicCommand C)
	{
		switch (C)
		{
		case EAcMusicCommand::Down: return TEXT("down");
		case EAcMusicCommand::Previous: return TEXT("previous");
		case EAcMusicCommand::Toggle: return TEXT("toggle");
		case EAcMusicCommand::Next: return TEXT("next");
		case EAcMusicCommand::Up: return TEXT("up");
		}
		return TEXT("?");
	}
}

UAcMusicDeckSubsystem* UAcMusicDeckSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UAcMusicDeckSubsystem>() : nullptr;
}

bool UAcMusicDeckSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

TOptional<EAcMusicCommand> UAcMusicDeckSubsystem::CommandNamed(const FString& Name)
{
	const FString N = Name.ToLower();
	if (N == TEXT("down")) return EAcMusicCommand::Down;
	if (N == TEXT("previous") || N == TEXT("prev")) return EAcMusicCommand::Previous;
	if (N == TEXT("toggle") || N == TEXT("pause")) return EAcMusicCommand::Toggle;
	if (N == TEXT("next")) return EAcMusicCommand::Next;
	if (N == TEXT("up")) return EAcMusicCommand::Up;
	return {};
}

void UAcMusicDeckSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	const TCHAR* Cmd = FCommandLine::Get();
	bSample = FParse::Value(Cmd, TEXT("AcMusicSample="), SampleMode) || FParse::Param(Cmd, TEXT("AcMusicSample"));
	FString Name;
	if (FParse::Value(Cmd, TEXT("AcDeckHover="), Name)) HeldHover = CommandNamed(Name);
	if (FParse::Value(Cmd, TEXT("AcDeckClick="), Name)) PendingClick = CommandNamed(Name);
	if (bSample && SampleMode != TEXT("off")) SampleNp = Sample(SampleMode);

	SAssignNew(DeckWidget, SAcMusicDeck)
		.OnCommand(FAcOnMusicCommand::CreateUObject(this, &UAcMusicDeckSubsystem::OnCommand));
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this))
	{
		FrameHandle = Sim->AddFrameListener(EAcFrameStage::Hud, FAcFrameEvent::FDelegate::CreateUObject(this, &UAcMusicDeckSubsystem::OnFrame));
	}
	Command = IConsoleManager::Get().RegisterConsoleCommand(TEXT("ac.MusicDeck"),
		TEXT("The music deck: ac.MusicDeck [click|hover down|previous|toggle|next|up]"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			const TOptional<EAcMusicCommand> C = Args.Num() > 1 ? CommandNamed(Args[1]) : TOptional<EAcMusicCommand>();
			if (Args.Num() > 1 && Args[0] == TEXT("click") && C) Click(*C);
			else if (Args.Num() > 0 && Args[0] == TEXT("hover")) HeldHover = C;
			const TOptional<FAcNowPlaying>& Np = DeckWidget->Shown();
			UE_LOG(LogAutocraft, Log, TEXT("deck: %s"), Np ? *FString::Printf(TEXT("\"%s\" %s/%s%s%s vote %d"), *Np->Title,
				*SAcMusicDeck::Clock(Np->PositionAt(Np->At)), *SAcMusicDeck::Clock(Np->Length), Np->bPaused ? TEXT(" paused") : TEXT(""),
				Np->bAd ? TEXT(" (ad)") : TEXT(""), Np->Vote) : TEXT("no music"));
		}),
		ECVF_Default);
}

void UAcMusicDeckSubsystem::Deinitialize()
{
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this)) Sim->RemoveFrameListener(EAcFrameStage::Hud, FrameHandle);
	if (const TSharedPtr<SAcConsole> Console = MountedOn.Pin())
	{
		Console->SetDeck(nullptr);
		Console->SetDeckTip({});
	}
	if (Command) IConsoleManager::Get().UnregisterConsoleObject(Command);
	Command = nullptr;
	DeckWidget.Reset();
	Super::Deinitialize();
}

void UAcMusicDeckSubsystem::Mount()
{
	const UAcConsoleSubsystem* Consoles = UAcConsoleSubsystem::Get(this);
	const TSharedPtr<SAcConsole> Console = Consoles ? Consoles->Console() : nullptr;
	if (!Console || !DeckWidget || MountedOn.Pin() == Console) return;
	Console->SetDeck(DeckWidget);
	MountedOn = Console;
	UE_LOG(LogAutocraft, Log, TEXT("deck: on the console%s"), bSample ? *FString::Printf(TEXT(" (sample %s)"),
		SampleMode.IsEmpty() ? TEXT("playing") : *SampleMode) : TEXT(""));
}

void UAcMusicDeckSubsystem::OnFrame(const FAcFrame& Frame)
{
	Mount();
	if (!DeckWidget) return;
	if (bSample)
	{
		if (bRefresh) DeckWidget->Show(SampleNp, SampleNp ? SampleNp->At : 0);
		bRefresh = false;
	}
	else if (const UAcMusicPlayer* Player = UAcMusicPlayer::Get(this))
	{
		const double Now = Player->Now();
		// The player bumps its revision at every change the deck shows; a
		// press with no voice (a silent run) may not, so look again after one.
		if (bRefresh || Player->Revision() != ShownRevision || Now - ShownAt >= 1)
		{
			bRefresh = false;
			ShownRevision = Player->Revision();
			ShownAt = Now;
			DeckWidget->Show(Player->NowPlaying(), Now);
		}
		DeckWidget->SetNow(Now);
	}
	else if (bRefresh)
	{
		bRefresh = false;
		DeckWidget->Show({}, 0);
	}

	if (PendingClick && MountedOn.IsValid() && Click(*PendingClick)) PendingClick.Reset();
	if (HeldHover) DeckWidget->SetHovered(HeldHover);

	// The tooltip over the dashboard (Console.showMusicTip).
	const TOptional<EAcMusicCommand> Over = DeckWidget->Hovered();
	const bool bPaused = DeckWidget->Shown() && DeckWidget->Shown()->bPaused;
	if (const TSharedPtr<SAcConsole> Console = MountedOn.Pin())
	{
		if (Over != TipShown || (Over == EAcMusicCommand::Toggle && bPaused != bTipShown))
		{
			TipShown = Over;
			bTipShown = bPaused;
			Console->SetDeckTip(Over ? DeckWidget->Tip(*Over) : TArray<FAcDeckTipLine>());
		}
	}
}

void UAcMusicDeckSubsystem::OnCommand(const EAcMusicCommand C)
{
	bRefresh = true;
	if (bSample)
	{
		UE_LOG(LogAutocraft, Log, TEXT("deck: pressed %s (sample: not played)"), NameOf(C));
		return;
	}
	UE_LOG(LogAutocraft, Log, TEXT("deck: pressed %s"), NameOf(C));
	if (UAcMusicPlayer* Player = UAcMusicPlayer::Get(this)) Player->Press(C);
}

bool UAcMusicDeckSubsystem::Click(const EAcMusicCommand C)
{
	const TSharedPtr<SAcConsole> Console = MountedOn.Pin();
	if (!Console || !DeckWidget || Console->Layout().Blocks[0].W <= 60) return false;
	const FAcRect Screen = FAcCabLayout::DeckScreen(Console->Layout().Blocks[0]);
	const FVector2D Size(Screen.W, Screen.H);
	for (const TPair<FAcRect, EAcMusicCommand>& B : SAcMusicDeck::Buttons(Size, DeckWidget->Shown().IsSet()))
	{
		if (B.Value != C) continue;
		// The click at the button's middle, in the deck's own box (y down).
		const FVector2D At(B.Key.MidX(), Size.Y - B.Key.MidY());
		const FGeometry Box = FGeometry::MakeRoot(FVector2f(Size), FSlateLayoutTransform());
		const FPointerEvent Event(0, At, At, TSet<FKey>({EKeys::LeftMouseButton}), EKeys::LeftMouseButton, 0, FModifierKeysState());
		const bool bHandled = DeckWidget->OnMouseButtonDown(Box, Event).IsEventHandled();
		UE_LOG(LogAutocraft, Log, TEXT("deck: click %s at %.0f,%.0f of %.0fx%.0f: %s"), NameOf(C), At.X, At.Y, Size.X, Size.Y,
			bHandled ? TEXT("handled") : TEXT("missed"));
		return true;
	}
	return false;
}

TOptional<FAcNowPlaying> UAcMusicDeckSubsystem::Sample(const FString& Mode)
{
	FAcNowPlaying Np;
	if (Mode == TEXT("ad"))
	{
		// The station's first ad (Sounds.json), 0:04 in, on the station's card.
		FString Text;
		TSharedPtr<FJsonObject> Root;
		const TSharedPtr<FJsonObject>* Ads = nullptr;
		if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Audio/Sounds.json")))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root
			|| !Root->TryGetObjectField(TEXT("ads"), Ads) || (*Ads)->Values.IsEmpty())
		{
			return {};
		}
		TArray<FString> Ids;
		for (const auto& Pair : (*Ads)->Values) Ids.Add(FString(Pair.Key));
		Ids.Sort();
		const TSharedPtr<FJsonObject> Ad = (*Ads)->GetObjectField(Ids[0]);
		const USoundWave* Wave = LoadObject<USoundWave>(nullptr, *Ad->GetStringField(TEXT("asset")));
		SampleCover = UAcMusicPlayer::MakeStationCover(this);
		Np.Track = Ids[0];
		Np.Title = Ad->GetStringField(TEXT("title"));
		Np.Cover = SampleCover;
		Np.bStationCover = true;
		Np.Length = Wave ? Wave->Duration : 0;
		Np.Position = 4;
		Np.bAd = true;
		return Np;
	}
	// The first own track, its tag's title and cover, 1:23 in, voted up.
	const TArray<FString> Own = UAcMp3Loader::FindMp3s(UAcMusicPlayer::OwnMusicFolder());
	if (Own.IsEmpty()) return {};
	const FString& Path = Own[0];
	Np.Track = FPaths::GetBaseFilename(Path);
	FAcTrackTags Tags;
	const bool bTags = UAcMusicPlayer::ReadTags(Path, Tags);
	Np.Title = bTags && !Tags.Title.IsEmpty() ? Tags.Title : UAcMusicPlayer::TitleFromId(Np.Track);
	if (bTags && Tags.Picture.Num()) SampleCover = FAcHudStyle::ImportHudTexture(Tags.Picture);
	Np.Cover = SampleCover;
	FAcMp3Info Info;
	Np.Length = UAcMp3Loader::Probe(Path, Info) ? Info.Duration : 210;
	Np.Position = 83;
	Np.bPaused = Mode == TEXT("paused");
	Np.Vote = Mode == TEXT("down") ? -1 : 1;
	return Np;
}
