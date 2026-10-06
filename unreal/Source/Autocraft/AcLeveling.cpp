#include "AcLeveling.h"

#include "AcAudioDirector.h"
#include "AcCommandMap.h"
#include "AcConsole.h"
#include "AcConsolePaint.h"
#include "AcHUD.h"
#include "AcHudStyle.h"
#include "AcLog.h"
#include "AcPilotOverlay.h"
#include "AcPilotPawn.h"
#include "AcPilotText.h"
#include "AcSimSubsystem.h"
#include "SAcCommandMap.h"
#include "SAcConsole.h"
#include "SAcLevelUp.h"
#include "SAcRoot.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include "Pilot.h"
#include "Simulation.h"

#include <cmath>

namespace
{
	FString Str(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }
	double Clock() { return FPlatformTime::Seconds(); }

	FAutoConsoleCommandWithWorldAndArgs GEarn(TEXT("ac.Earn"), TEXT("ac.Earn XP: XP for the driven unit (sim.earn)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UAcLevelingSubsystem* L = UAcLevelingSubsystem::Get(World)) L->Earn(Args.Num() > 0 ? FCString::Atod(*Args[0]) : 100);
		}));

	FAutoConsoleCommandWithWorldAndArgs GPick(TEXT("ac.Pick"), TEXT("ac.Pick z|x|PERK: a leveling pick, as the key or by name."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UAcLevelingSubsystem* L = UAcLevelingSubsystem::Get(World);
			if (!L || Args.IsEmpty()) return;
			if (Args[0].Len() == 1) L->PickKey(Args[0].ToLower());
			else if (const TOptional<ac::Perk> P = AcLeveling::ParsePerk(Args[0])) L->Pick(*P);
			else UE_LOG(LogAutocraft, Warning, TEXT("ac.Pick: no perk %s"), *Args[0]);
		}));

	FAutoConsoleCommandWithWorldAndArgs GLevelUp(TEXT("ac.LevelUp"), TEXT("ac.LevelUp [KIND LEVEL]: the level-up banner."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UAcLevelingSubsystem* L = UAcLevelingSubsystem::Get(World);
			if (!L) return;
			const std::optional<ac::UnitKind> K = ac::parse<ac::UnitKind>(Args.Num() > 0 ? TCHAR_TO_UTF8(*Args[0].ToLower()) : "prospector");
			L->LevelUp(K.value_or(ac::UnitKind::prospector), Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 2);
		}));
}

// MARK: - Logic

TOptional<FAcPickOffer> FAcPickOffer::Of(const ac::PilotRecord& R, const ac::UnitKind Kind)
{
	const std::vector<ac::PilotRecord::Offer> Pending = R.pending(Kind);
	if (Pending.empty()) return {};
	FAcPickOffer O;
	O.Kind = Kind;
	O.Level = (int32)Pending.front().level;
	O.Perks = {Pending.front().one, Pending.front().other};
	O.Waiting = (int32)Pending.size();
	return O;
}

EAcPerkReach AcLeveling::Reach(const ac::Perk Perk)
{
	const std::vector<ac::Effect>& Effects = ac::effects(Perk);
	bool bNearby = false, bTeam = false, bAura = false;
	for (const ac::Effect& E : Effects)
	{
		if (const ac::Effect::Stat* S = E.as<ac::Effect::Stat>())
		{
			// Around: friendly units of any kind within 6 cells of the driven one (Escort, Bulwark, War march).
			bNearby |= S->scope.is<ac::Scope::Nearby>() || S->scope.is<ac::Scope::Around>();
			bTeam |= S->scope.is<ac::Scope::Every>() || S->scope.is<ac::Scope::Team>();
		}
		bAura |= E.is<ac::Effect::Aura>();
	}
	if (bNearby) return EAcPerkReach::Nearby;
	if (bTeam || bAura) return EAcPerkReach::Team;
	if (!Effects.empty()) return EAcPerkReach::Driven;
	// No effects built: from its line (`PerkReach.init` fallback).
	const FString T = Effect(Perk).ToLower();
	if (T.StartsWith(TEXT("nearby")) || T.Contains(TEXT(", and nearby")) || T.Contains(TEXT("; nearby"))) return EAcPerkReach::Nearby;
	if (T.Contains(TEXT("team")) || T.StartsWith(TEXT("friendly")) || T.StartsWith(TEXT("garrison"))
		|| (T.StartsWith(TEXT("every ")) && !T.StartsWith(TEXT("every third")))
		|| (T.Contains(TEXT("cost 10% less")) && !T.Contains(TEXT("it places"))))
	{
		return EAcPerkReach::Team;
	}
	return EAcPerkReach::Driven;
}

const TCHAR* AcLeveling::Tag(const EAcPerkReach R)
{
	switch (R)
	{
	case EAcPerkReach::Driven: return TEXT("YOU");
	case EAcPerkReach::Nearby: return TEXT("NEARBY");
	case EAcPerkReach::Team: return TEXT("TEAM");
	}
	return TEXT("");
}

FString AcLeveling::Title(const ac::Perk Perk) { return Str(ac::title(Perk)); }
FString AcLeveling::Effect(const ac::Perk Perk) { return Str(ac::effect(Perk)); }
FString AcLeveling::Name(const ac::Perk Perk) { return Str(std::string(ac::rawValue(Perk))); }

TOptional<ac::Perk> AcLeveling::ParsePerk(const FString& Name)
{
	for (const ac::Perk P : ac::allCases<ac::Perk>())
	{
		if (AcLeveling::Name(P).Equals(Name, ESearchCase::IgnoreCase)) return P;
	}
	return {};
}

TArray<FString> AcLeveling::Wrap(const FString& Text, const double Size, const double Width, const int32 Max)
{
	auto W = [Size](const FString& S) { return FAcConsolePaint::TextWidth(S, Size); };
	TArray<FString> Words;
	Text.ParseIntoArray(Words, TEXT(" "), true);
	TArray<FString> Out;
	FString Line;
	for (const FString& Word : Words)
	{
		const FString Longer = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
		if (Line.IsEmpty() || W(Longer) <= Width) Line = Longer;
		else
		{
			Out.Add(Line);
			Line = Word;
		}
	}
	if (!Line.IsEmpty()) Out.Add(Line);
	if (Out.Num() <= Max) return Out;
	FString Last = Out[Max - 1];
	while (!Last.IsEmpty() && W(Last + TEXT("…")) > Width) Last.LeftChopInline(1);
	Out.SetNum(Max - 1);
	Out.Add(Last + TEXT("…"));
	return Out;
}

TArray<FAcDrivenRow> AcLeveling::Rows(const ac::Simulation& Sim)
{
	const ac::PilotRecord& R = Sim.levels();
	TArray<FAcDrivenRow> Out;
	for (const ac::UnitKind K : R.driven())
	{
		FAcDrivenRow Row;
		Row.Kind = K;
		Row.Level = AcPilotText::Level(Sim, K);
		for (const ac::Perk P : R.picks(K)) Row.Picks.Add(P);
		Row.Offer = FAcPickOffer::Of(R, K);
		Out.Add(MoveTemp(Row));
	}
	return Out;
}

double AcLeveling::PilotsHeight(const TArray<FAcDrivenRow>& Rows)
{
	if (Rows.IsEmpty()) return 0;
	double H = 40;
	for (const FAcDrivenRow& R : Rows) H += 46 + (R.Offer ? PickRoom : 0);
	return H;
}

FString AcLeveling::BannerTitle(const ac::UnitKind Kind, const int32 Level)
{
	return FString::Printf(TEXT("%s  LEVEL %d"), *AcPilotText::Title(Kind).ToUpper(), Level);
}

FString AcLeveling::BannerSub(const int32 Level)
{
	return Level >= ac::Leveling::cap ? TEXT("TOP LEVEL  ·  LAST PICK ON THE DASHBOARD") : TEXT("NEW PICK ON THE DASHBOARD");
}

TArray<ac::UnitKind> AcLeveling::Stage(ac::Simulation& Sim, const FString& Spec, const int32 Picks)
{
	TArray<ac::UnitKind> Kinds;
	TArray<FString> Items;
	Spec.ParseIntoArray(Items, TEXT(","), true);
	ac::PilotRecord R = Sim.levels();
	for (const FString& Item : Items)
	{
		TArray<FString> Parts;
		Item.ParseIntoArray(Parts, TEXT(":"), true);
		if (Parts.IsEmpty()) continue;
		const std::optional<ac::UnitKind> K = ac::parse<ac::UnitKind>(TCHAR_TO_UTF8(*Parts[0].ToLower()));
		if (!K)
		{
			UE_LOG(LogAutocraft, Warning, TEXT("leveling: no kind %s"), *Parts[0]);
			continue;
		}
		const int64 L = FMath::Clamp<int64>(Parts.Num() > 1 ? FCString::Atoi(*Parts[1]) : 2, 1, ac::Leveling::cap);
		const double From = ac::Leveling::xp(L);
		ac::KindRecord Record;
		Record.xp = L < ac::Leveling::cap ? From + (ac::Leveling::xp(L + 1) - From) / 3 : From + 140;
		R.kinds[*K] = Record;
		Kinds.Add(*K);
	}
	Sim.updateLevels([&R](ac::PilotRecord& Slot) { Slot = R; });
	for (const ac::UnitKind K : Kinds)
	{
		for (int32 N = 0; N < Picks; ++N)
		{
			const std::vector<ac::PilotRecord::Offer> P = Sim.levels().pending(K);
			if (P.empty()) break;
			Sim.pick(N % 2 == 0 ? P.front().one : P.front().other);
		}
	}
	FString Line;
	for (const ac::UnitKind K : Kinds)
	{
		Line += FString::Printf(TEXT("%s%s %lld, %d picked, %d waiting"), Line.IsEmpty() ? TEXT("") : TEXT("; "),
			*Str(std::string(ac::rawValue(K))), (long long)Sim.levels().level(K), (int32)Sim.levels().picks(K).size(),
			(int32)Sim.levels().pending(K).size());
	}
	UE_LOG(LogAutocraft, Log, TEXT("levels: %s"), *Line);
	return Kinds;
}

// MARK: - The subsystem

UAcLevelingSubsystem* UAcLevelingSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UAcLevelingSubsystem>() : nullptr;
}

bool UAcLevelingSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UAcLevelingSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	const TCHAR* Cmd = FCommandLine::Get();
	FParse::Value(Cmd, TEXT("AcLevel="), LevelSpec, false);
	FParse::Value(Cmd, TEXT("AcPicks="), LevelPicks);
	bHoldBanner = FParse::Param(Cmd, TEXT("AcLevelUp"));
	FString Earn;
	if (FParse::Value(Cmd, TEXT("AcEarn="), Earn))
	{
		FString Xp, After;
		if (Earn.Split(TEXT("@"), &Xp, &After)) EarnAfter = FCString::Atod(*After);
		else Xp = Earn;
		EarnXp = FCString::Atod(*Xp);
	}
	FParse::Value(Cmd, TEXT("AcPickKey="), PickKeyWanted);
	PickKeyWanted = PickKeyWanted.ToLower();
	FParse::Value(Cmd, TEXT("AcPickClick="), PickClickWanted);
	FParse::Value(Cmd, TEXT("AcMapPickPerk="), MapPickWanted);

	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this))
	{
		FrameHandle = Sim->AddFrameListener(EAcFrameStage::Hud, FAcFrameEvent::FDelegate::CreateUObject(this, &UAcLevelingSubsystem::OnFrame));
	}
	if (UAcCommandMapSubsystem* Map = UAcCommandMapSubsystem::Get(this))
	{
		MapInfoHandle = Map->OnInfo.AddUObject(this, &UAcLevelingSubsystem::OnMapInfo);
		MapPickHandle = Map->OnPick.AddWeakLambda(this, [this](const ac::Perk P) { Pick(P); });
		Map->PilotsPainter = [Weak = TWeakObjectPtr<UAcLevelingSubsystem>(this)](FAcCommandMapColumn& Col)
		{
			if (Weak.IsValid()) Weak->PaintPilots(Col);
		};
	}
}

void UAcLevelingSubsystem::Deinitialize()
{
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this)) Sim->RemoveFrameListener(EAcFrameStage::Hud, FrameHandle);
	if (UAcCommandMapSubsystem* Map = UAcCommandMapSubsystem::Get(this))
	{
		Map->OnInfo.Remove(MapInfoHandle);
		Map->OnPick.Remove(MapPickHandle);
		Map->PilotsPainter = nullptr;
	}
	if (AAcHUD* Hud = AAcHUD::Get(this))
	{
		if (const TSharedPtr<SAcRoot> Root = Hud->Root())
		{
			if (CardsWidget) Root->RemoveLayer(CardsWidget.ToSharedRef());
			if (BannerWidget) Root->RemoveLayer(BannerWidget.ToSharedRef());
		}
	}
	CardsWidget.Reset();
	BannerWidget.Reset();
	Super::Deinitialize();
}

bool UAcLevelingSubsystem::Mount()
{
	if (CardsWidget && BannerWidget) return true;
	AAcHUD* Hud = AAcHUD::Get(this);
	const TSharedPtr<SAcRoot> Root = Hud ? Hud->Root() : nullptr;
	if (!Root) return false;
	Root->AddLayer(CardsLayer)
	[
		SAssignNew(CardsWidget, SAcPickCards)
		.OnPick(FAcOnPickCard::CreateWeakLambda(this, [this](const ac::Perk P) { Pick(P); }))
	];
	Root->AddLayer(BannerLayer)[SAssignNew(BannerWidget, SAcLevelBanner)];
	BannerWidget->SetVisibility(EVisibility::HitTestInvisible);
	return true;
}

void UAcLevelingSubsystem::HookPawn()
{
	AAcPilotPawn* Pawn = AAcPilotPawn::Find(this);
	if (!Pawn || HookedPawn.Get() == Pawn) return;
	HookedPawn = Pawn;
	// Chained: the build menu (E8) may have its own keys and pointer wishes.
	TFunction<bool(const FString&)> PrevKey = Pawn->OnKey;
	TFunction<bool()> PrevPointable = Pawn->Pointable;
	const TWeakObjectPtr<UAcLevelingSubsystem> Weak(this);
	Pawn->OnKey = [Weak, PrevKey](const FString& K)
	{
		if ((K == TEXT("z") || K == TEXT("x")) && Weak.IsValid() && Weak->PickKey(K)) return true;
		return PrevKey ? PrevKey(K) : false;
	};
	Pawn->Pointable = [Weak, PrevPointable]()
	{
		if (Weak.IsValid() && Weak->DrivenOffer().IsSet()) return true;
		return PrevPointable ? PrevPointable() : false;
	};
}

TOptional<FAcPickOffer> UAcLevelingSubsystem::DrivenOffer() const
{
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || !Sim->IsRunning()) return {};
	const std::optional<ac::Unit> U = Sim->Simulation().pilotUnit();
	if (!U) return {};
	return FAcPickOffer::Of(Sim->Simulation().levels(), U->kind);
}

bool UAcLevelingSubsystem::Pick(const ac::Perk Perk)
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || !Sim->IsRunning()) return false;
	const std::optional<std::string> Why = Sim->Simulation().levels().refusal(Perk);
	const bool bOk = Sim->Issue(ac::Command(ac::Command::Pick{Sim->LocalPlayer(), Perk}));
	UE_LOG(LogAutocraft, Log, TEXT("pilot: pick %s %s"), *AcLeveling::Name(Perk),
		bOk ? TEXT("ok") : *(TEXT("refused: ") + (Why ? Str(*Why) : FString(TEXT("?")))));
	if (bOk)
	{
		if (UAcAudioDirector* Audio = UAcAudioDirector::Get(this)) Audio->Rules().Picked();
	}
	else if (Sim->Simulation().pilot)
	{
		if (UAcPilotOverlaySubsystem* O = UAcPilotOverlaySubsystem::Get(this)) O->Note(Why ? Str(*Why) : TEXT("Can't pick that now"));
	}
	if (UAcCommandMapSubsystem* Map = UAcCommandMapSubsystem::Get(this))
	{
		if (Map->IsOpen()) Map->Refresh(true);
	}
	return bOk;
}

bool UAcLevelingSubsystem::PickKey(const FString& Key)
{
	const TOptional<FAcPickOffer> O = DrivenOffer();
	if (!O) return false;
	const int32 Slot = Key.Equals(TEXT("z"), ESearchCase::IgnoreCase) ? 0 : Key.Equals(TEXT("x"), ESearchCase::IgnoreCase) ? 1 : -1;
	if (Slot < 0 || !O->Perks.IsValidIndex(Slot)) return false;
	Pick(O->Perks[Slot]);
	return true;
}

void UAcLevelingSubsystem::LevelUp(const ac::UnitKind Kind, const int32 Level, const bool bHold)
{
	UE_LOG(LogAutocraft, Log, TEXT("pilot: %s level %d"), *Str(std::string(ac::rawValue(Kind))), Level);
	if (!Mount()) return;
	BannerWidget->Show(Kind, Level, Clock(), bHold);
}

bool UAcLevelingSubsystem::Earn(const double Xp)
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || !Sim->IsRunning()) return false;
	const std::optional<ac::Unit> U = Sim->Simulation().pilotUnit();
	if (!U) return false;
	const int64 Before = Sim->Simulation().levels().level(U->kind);
	Sim->Simulation().earn(Xp, *U);
	UE_LOG(LogAutocraft, Log, TEXT("leveling: earn %.0f XP for %s #%lld: level %lld -> %lld"), Xp,
		*Str(std::string(ac::rawValue(U->kind))), (long long)U->id, (long long)Before,
		(long long)Sim->Simulation().levels().level(U->kind));
	return true;
}

void UAcLevelingSubsystem::OnMapInfo(FAcCommandMapInfo& Info)
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	MapRows = Sim && Sim->IsRunning() ? AcLeveling::Rows(Sim->Simulation()) : TArray<FAcDrivenRow>();
	Info.PilotsRoom = AcLeveling::PilotsHeight(MapRows);
}

void UAcLevelingSubsystem::PaintPilots(FAcCommandMapColumn& Col)
{
	TArray<TPair<FAcRect, ac::Perk>> Buttons;
	AcLevelPaint::Pilots(Col, MapRows, Buttons);
	MapViewHeight = Col.ViewHeight;
	MapPicks = MoveTemp(Buttons);
}

void UAcLevelingSubsystem::OnFrame(const FAcFrame& Frame)
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || !Sim->IsRunning()) return;
	if (!Mount()) return;
	HookPawn();
	Script(*Sim);

	// `leveledUp`, once for each level crossed.
	for (const ac::GameEvent& E : Frame.Events)
	{
		if (const ac::GameEvent::LeveledUp* L = E.as<ac::GameEvent::LeveledUp>()) LevelUp(L->kind, (int32)L->level);
	}

	// The cards over the dashboard while driving (`showPicks`: none top-down).
	const UAcConsoleSubsystem* ConsoleSub = UAcConsoleSubsystem::Get(this);
	const TSharedPtr<SAcConsole> Console = ConsoleSub ? ConsoleSub->Console() : nullptr;
	const AAcPilotPawn* Pawn = HookedPawn.Get();
	TOptional<FAcPickOffer> Offer;
	if (Console && Console->Style() != EAcConsoleStyle::Rts && Pawn && Pawn->Driving()) Offer = DrivenOffer();
	if (Console)
	{
		const FLinearColor Accent = Console->Style() == EAcConsoleStyle::Rts ? FAcHudStyle::Srgb(0.45, 0.75, 1) : FAcHudStyle::Srgb(0.35, 0.95, 1);
		CardsWidget->Show(Offer, Console->Layout(), Accent, true);
	}
	else
	{
		CardsWidget->Show({}, FAcCabLayout(), FLinearColor::White, true);
	}
}

void UAcLevelingSubsystem::Script(const UAcSimSubsystem& InSim)
{
	UAcSimSubsystem& Sim = const_cast<UAcSimSubsystem&>(InSim);
	if (!bStaged)
	{
		bStaged = true;
		if (!LevelSpec.IsEmpty()) Leveled = AcLeveling::Stage(Sim.Simulation(), LevelSpec, LevelPicks);
	}
	const std::optional<ac::Unit> U = Sim.Simulation().pilotUnit();
	if (bHoldBanner)
	{
		// `--levelup`: the driven kind's, else the first leveled one's, held.
		TOptional<ac::UnitKind> K;
		if (U) K = U->kind;
		else if (!Leveled.IsEmpty()) K = Leveled[0];
		if (K)
		{
			const int32 L = (int32)Sim.Simulation().levels().level(*K);
			if (!HeldBanner || HeldBanner->Key != *K || HeldBanner->Value != L)
			{
				HeldBanner = TPair<ac::UnitKind, int32>(*K, L);
				BannerWidget->Show(*K, L, Clock(), true);
			}
		}
	}
	if (U && DriveStarted < 0) DriveStarted = Clock();
	if (!U) DriveStarted = -1;
	if (EarnXp > 0 && DriveStarted >= 0 && Clock() - DriveStarted >= EarnAfter)
	{
		Earn(EarnXp);
		EarnXp = 0;
	}
	// Scripted picks, a few frames after the cards are up (they lay out first).
	CardFrames = CardsWidget && CardsWidget->Offer() ? CardFrames + 1 : 0;
	if (CardFrames == 8)
	{
		if (!PickKeyWanted.IsEmpty())
		{
			UE_LOG(LogAutocraft, Log, TEXT("leveling: key %s"), *PickKeyWanted);
			PickKey(PickKeyWanted);
			PickKeyWanted.Reset();
		}
		else if (PickClickWanted > 0)
		{
			if (const TOptional<FAcRect> R = CardsWidget->CardRect(PickClickWanted - 1))
			{
				const FVector2D View = CardsWidget->GetCachedGeometry().GetLocalSize();
				const FVector2D At(R->MidX(), View.Y - R->MidY());
				UE_LOG(LogAutocraft, Log, TEXT("leveling: click card %d at %.0f,%.0f: %s"), PickClickWanted, At.X, At.Y,
					CardsWidget->Click(At) ? TEXT("taken") : TEXT("missed"));
			}
			PickClickWanted = 0;
		}
	}
	const UAcCommandMapSubsystem* Map = UAcCommandMapSubsystem::Get(this);
	MapFrames = Map && Map->IsOpen() && !MapPicks.IsEmpty() ? MapFrames + 1 : 0;
	if (MapPickWanted > 0 && MapFrames == 8)
	{
		if (MapPicks.IsValidIndex(MapPickWanted - 1))
		{
			UAcCommandMapSubsystem* M = UAcCommandMapSubsystem::Get(this);
			const FAcRect R = MapPicks[MapPickWanted - 1].Key;
			// Swift points (y up) to view points (y down), through the map's own hit test.
			const FVector2D At(R.MidX(), MapViewHeight - R.MidY());
			UE_LOG(LogAutocraft, Log, TEXT("leveling: map pick %d (%s) at %.0f,%.0f: %s"), MapPickWanted,
				*AcLeveling::Name(MapPicks[MapPickWanted - 1].Value), At.X, At.Y, M && M->ClickAt(At) ? TEXT("hit") : TEXT("missed"));
		}
		MapPickWanted = 0;
	}
}
