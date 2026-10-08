#include "AcCommandMap.h"

#include "AcBeacons.h"
#include "AcCommandCard.h"
#include "AcCommands.h"
#include "AcHUD.h"
#include "AcLog.h"
#include "AcMinimap.h"
#include "AcPointer.h"
#include "AcRtsPawn.h"
#include "AcSimSubsystem.h"
#include "SAcCommandMap.h"
#include "SAcMinimap.h"
#include "SAcRoot.h"

#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include "Commander.h"
#include "Rules.h"
#include "Simulation.h"

#include <algorithm>
#include <set>

namespace
{
	UAcCommandMapSubsystem* MapOf(UWorld* World) { return UAcCommandMapSubsystem::Get(World); }

	FAutoConsoleCommandWithWorldAndArgs GCommandMap(TEXT("ac.CommandMap"), TEXT("Toggle the command map, or: open | close."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UAcCommandMapSubsystem* M = MapOf(World);
			if (!M) return;
			if (Args.IsEmpty()) M->Toggle();
			else if (Args[0] == TEXT("open")) M->Open();
			else M->Close();
		}));
	FAutoConsoleCommandWithWorldAndArgs GMapClick(TEXT("ac.MapClick"), TEXT("Click the command map at a view point (points, y down)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UAcCommandMapSubsystem* M = MapOf(World); M && Args.Num() >= 2)
				M->ClickAt(FVector2D(FCString::Atod(*Args[0]), FCString::Atod(*Args[1])));
		}));
	FAutoConsoleCommandWithWorldAndArgs GMapGround(TEXT("ac.MapGround"), TEXT("Click the command map at a ground point X Y (cells)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UAcCommandMapSubsystem* M = MapOf(World); M && Args.Num() >= 2)
				M->ClickGround(ac::Vec2(FCString::Atod(*Args[0]), FCString::Atod(*Args[1])));
		}));
	FAutoConsoleCommandWithWorldAndArgs GMapPick(TEXT("ac.MapPick"), TEXT("Press item N (1-based) of the command map's order menu."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UAcCommandMapSubsystem* M = MapOf(World); M && !Args.IsEmpty()) M->PickMenu(FCString::Atoi(*Args[0]) - 1);
		}));
	FAutoConsoleCommandWithWorldAndArgs GMapKey(TEXT("ac.MapKey"), TEXT("A key on the command map: m, esc, 1-4."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UAcCommandMapSubsystem* M = MapOf(World); M && !Args.IsEmpty())
			{
				if (M->IsOpen()) M->Key(Args[0].ToLower());
				else if (Args[0].ToLower() == TEXT("m")) M->Toggle();
			}
		}));

	bool ParsePair(const FString& Text, ac::Vec2& Out)
	{
		TArray<FString> Parts;
		Text.Replace(TEXT("\""), TEXT("")).ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() < 2) return false;
		Out = ac::Vec2(FCString::Atod(*Parts[0].TrimStartAndEnd()), FCString::Atod(*Parts[1].TrimStartAndEnd()));
		return true;
	}
}

UAcCommandMapSubsystem* UAcCommandMapSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UAcCommandMapSubsystem>() : nullptr;
}

bool UAcCommandMapSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

double UAcCommandMapSubsystem::Clock()
{
	return FPlatformTime::Seconds();
}

void UAcCommandMapSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim) return;
	FrameHandle = Sim->AddFrameListener(EAcFrameStage::Hud, FAcFrameEvent::FDelegate::CreateUObject(this, &UAcCommandMapSubsystem::OnFrame));
	AAcBeacons::SpawnFor(&InWorld);

	const TCHAR* Cmd = FCommandLine::Get();
	bWantOpen = FParse::Param(Cmd, TEXT("AcCommandMap"));
	bWantQueue = FParse::Param(Cmd, TEXT("AcQueue"));
	FString Text;
	if (FParse::Value(Cmd, TEXT("AcOrders="), Text)) WantOrders = Text;
	else if (FParse::Param(Cmd, TEXT("AcOrders"))) WantOrders = FString();
	ac::Vec2 G;
	if (FParse::Value(Cmd, TEXT("AcMapGround="), Text) && ParsePair(Text, G)) WantGround = G;
	FParse::Value(Cmd, TEXT("AcMapPick="), WantPick);
}

void UAcCommandMapSubsystem::Deinitialize()
{
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this)) Sim->RemoveFrameListener(EAcFrameStage::Hud, FrameHandle);
	if (UAcCommandsSubsystem* C = BoundCommands.Get()) C->OnCommanderChanged.Remove(CommanderHandle);
	if (Widget)
	{
		if (const AAcHUD* Hud = AAcHUD::Get(GetWorld()))
		{
			if (const TSharedPtr<SAcRoot> Root = Hud->Root()) Root->RemoveLayer(Widget.ToSharedRef());
		}
	}
	Widget.Reset();
	Super::Deinitialize();
}

// MARK: - Opening and shutting

bool UAcCommandMapSubsystem::Toggle()
{
	if (bOpen)
	{
		Close();
		return true;
	}
	return Open();
}

bool UAcCommandMapSubsystem::Open()
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || !Sim->IsRunning()) return false;
	const ac::Simulation& S = Sim->Simulation();
	// `guard free != nil, commanded, !piloting`.
	if (!FAcCommandCard::Commanded(S, Sim->LocalPlayer()) || S.pilot) return false;
	if (!Mount()) return false;
	if (UAcPointer* P = UAcPointer::Get(this)) P->Select({});
	bOpen = true;
	MapMenu.Reset();
	SetBlocked(true);
	Widget->SetVisibility(EVisibility::Visible);
	UE_LOG(LogAutocraft, Log, TEXT("command map: open"));
	Refresh(true);
	return true;
}

void UAcCommandMapSubsystem::Close()
{
	if (!bOpen) return;
	bOpen = false;
	MapMenu.Reset();
	SetBlocked(false);
	if (Widget) Widget->SetVisibility(EVisibility::Collapsed);
	UE_LOG(LogAutocraft, Log, TEXT("command map: closed"));
}

void UAcCommandMapSubsystem::SetBlocked(const bool bBlocked)
{
	if (UAcPointer* P = UAcPointer::Get(this)) P->SetBlocked(bBlocked);
	if (UAcCommandsSubsystem* C = UAcCommandsSubsystem::Get(this)) C->SetBlocked(bBlocked);
	if (AAcRtsPawn* Pawn = AAcRtsPawn::Get(this)) Pawn->SetInputBlocked(bBlocked);
}

bool UAcCommandMapSubsystem::Mount()
{
	if (Widget) return true;
	AAcHUD* Hud = AAcHUD::Get(this);
	const TSharedPtr<SAcRoot> Root = Hud ? Hud->Root() : nullptr;
	if (!Root) return false;
	Root->AddLayer(AcHudLayer::CommandMap)
	[
		SAssignNew(Widget, SAcCommandMap)
		.OnHit(FAcOnCommandMapHit::CreateWeakLambda(this, [this](const FAcCommandMapHit& H) { OnHit(H); }))
	];
	Widget->SetVisibility(EVisibility::Collapsed);
	Widget->SetPilotsPainter([Weak = TWeakObjectPtr<UAcCommandMapSubsystem>(this)](FAcCommandMapColumn& C)
	{
		if (Weak.IsValid() && Weak->PilotsPainter) Weak->PilotsPainter(C);
	});
	return true;
}

void UAcCommandMapSubsystem::FitMinimap()
{
	if (!Widget) return;
	const FVector2D View = Widget->ViewSize();
	if (View.X <= 0 || View.Y <= 0) return;
	UAcMinimapSubsystem* Minimaps = UAcMinimapSubsystem::Get(this);
	if (!Minimaps || !Minimaps->Terrain()) return;
	const ac::GroundRect& B = Minimaps->Bounds();
	// `makeCommandMinimap(fit: CommandMap.mapArea(size))`.
	const FVector2D Fit = SAcCommandMap::MapArea(View);
	double W = Fit.X, H = W * B.depth() / FMath::Max(B.width(), 1e-6);
	if (H > Fit.Y)
	{
		H = Fit.Y;
		W = H * B.width() / FMath::Max(B.depth(), 1e-6);
	}
	const TSharedPtr<SAcMinimap> Have = Widget->Minimap();
	if (Have && MinimapFor == View && Have->Bounds().width() == B.width() && Have->Bounds().depth() == B.depth()) return;
	MinimapFor = View;
	Widget->SetMinimap(Minimaps->MakeMinimap(FVector2D(W, H), 1.8, false));
	UE_LOG(LogAutocraft, Log, TEXT("command map: %.0fx%.0f map in a %.0fx%.0f view"), W, H, View.X, View.Y);
}

// MARK: - Refreshing

void UAcCommandMapSubsystem::Refresh(const bool bNow)
{
	const double Now = Clock();
	if (!bNow && Now - Shown < 0.25) return;
	Shown = Now;
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	AAcBeacons* Beacons = AAcBeacons::Find(GetWorld());
	if (!Sim || !Sim->IsRunning()) return;
	const ac::Simulation& S = Sim->Simulation();
	const int64 Player = Sim->LocalPlayer();
	const ac::Commander* Ai = Player < (int64)S.state.players.size() ? FAcCommandMapLogic::CommanderOf(S, Player) : nullptr;
	if (!Ai)
	{
		if (Beacons) Beacons->Show({});
		Close();
		return;
	}
	ObjectiveStatus.clear();
	for (const ac::ObjectiveStatus& O : Ai->objectiveStatus(S)) ObjectiveStatus.emplace(O.id, O);
	if (Beacons) Beacons->Show(FAcCommandMapLogic::Beacons(S, Player));
	UpdateMap();
}

void UAcCommandMapSubsystem::UpdateMap()
{
	if (!bOpen) return;
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || !Sim->IsRunning()) return Close();
	const ac::Simulation& S = Sim->Simulation();
	const int64 Player = Sim->LocalPlayer();
	const ac::Commander* Ai = FAcCommandMapLogic::CommanderOf(S, Player);
	if (S.pilot || !Ai) return Close();
	FAcCommandMapInfo Info = FAcCommandMapLogic::Info(S, *Ai, Player, ObjectiveStatus);
	// A menu whose orders no longer hold is made again, and goes once it has nothing left to offer.
	if (MapMenu)
	{
		TArray<FAcMapOrder> Items = FAcCommandMapLogic::Orders(S, Player, MapMenu->At);
		if (Items.IsEmpty()) MapMenu.Reset();
		else MapMenu->Items = MoveTemp(Items);
	}
	Info.Menu = MapMenu;
	OnInfo.Broadcast(Info);
	if (Widget) Widget->Show(Info);
}

// MARK: - Orders

void UAcCommandMapSubsystem::Give(const ac::Command& C)
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim) return;
	const bool bOk = Sim->Issue(C);
	UE_LOG(LogAutocraft, Log, TEXT("commander: %s %s"), *FAcCommandMapLogic::Describe(C), bOk ? TEXT("ok") : TEXT("refused"));
	Refresh(true);
}

void UAcCommandMapSubsystem::CancelId(const int64 Id)
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || !Sim->IsRunning()) return;
	Give(FAcCommandMapLogic::CancelCommand(Sim->Simulation(), Sim->LocalPlayer(), Id));
}

void UAcCommandMapSubsystem::OnHit(const FAcCommandMapHit& Hit)
{
	if (!bOpen) return;
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || !Sim->IsRunning()) return;
	const int64 Player = Sim->LocalPlayer();
	using EType = FAcCommandMapHit::EType;
	switch (Hit.Type)
	{
	case EType::Stance:
		Give(ac::Command(ac::Command::Stance{Player, Hit.Stance}));
		break;
	case EType::Order:
		MapMenu.Reset();
		Give(FAcCommandMapLogic::CommandFor(Sim->Simulation(), Player, Hit.Order));
		break;
	case EType::Cancel:
		MapMenu.Reset();
		CancelId(Hit.Id);
		break;
	case EType::Ground:
		ClickGround(Hit.Ground);
		break;
	case EType::Inside:
		MapMenu.Reset();
		UpdateMap();
		break;
	case EType::Close:
		Close();
		break;
	case EType::Pick:
		OnPick.Broadcast(Hit.Perk);
		break;
	}
}

void UAcCommandMapSubsystem::ClickGround(const ac::Vec2 Ground)
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!bOpen || !Sim || !Sim->IsRunning()) return;
	TArray<FAcMapOrder> Items = FAcCommandMapLogic::Orders(Sim->Simulation(), Sim->LocalPlayer(), Ground);
	FString Titles;
	for (const FAcMapOrder& O : Items) Titles += (Titles.IsEmpty() ? TEXT("") : TEXT(", ")) + O.Title();
	UE_LOG(LogAutocraft, Log, TEXT("map orders at %d,%d: [%s]"), (int32)Ground.x, (int32)Ground.y, *Titles);
	if (Items.IsEmpty()) MapMenu.Reset();
	else MapMenu = FAcMapMenu{Ground, MoveTemp(Items)};
	UpdateMap();
}

bool UAcCommandMapSubsystem::PickMenu(const int32 Index)
{
	if (!bOpen || !MapMenu || !MapMenu->Items.IsValidIndex(Index)) return false;
	FAcCommandMapHit H;
	H.Type = FAcCommandMapHit::EType::Order;
	H.Order = MapMenu->Items[Index];
	OnHit(H);
	return true;
}

bool UAcCommandMapSubsystem::ClickAt(const FVector2D ViewPoint)
{
	if (!bOpen || !Widget) return false;
	const TOptional<FAcCommandMapHit> H = Widget->Hit(Widget->ToSwift(ViewPoint));
	if (!H) return false;
	OnHit(*H);
	return true;
}

bool UAcCommandMapSubsystem::Key(const FString& K)
{
	if (!bOpen) return false;
	if (K == TEXT("m"))
	{
		Close();
	}
	else if (K == TEXT("esc"))
	{
		if (MapMenu)
		{
			MapMenu.Reset();
			UpdateMap();
		}
		else
		{
			Close();
		}
	}
	else
	{
		const int32 N = FCString::Atoi(*K);
		if (N < 1 || N > 4) return false;
		UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
		if (!Sim) return false;
		const ac::Stance All[] = {ac::Stance::auto_, ac::Stance::aggressive, ac::Stance::hold, ac::Stance::allIn};
		Give(ac::Command(ac::Command::Stance{Sim->LocalPlayer(), All[N - 1]}));
	}
	return true;
}

void UAcCommandMapSubsystem::PollKeys()
{
	const UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC) return;
	const FModifierKeysState Mods = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetModifierKeys() : FModifierKeysState();
	if (Mods.IsCommandDown() || Mods.IsControlDown()) return;
	if (PC->WasInputKeyJustPressed(EKeys::M))
	{
		Toggle();
		return;
	}
	if (!bOpen) return;
	if (PC->WasInputKeyJustPressed(EKeys::Escape))
	{
		Key(TEXT("esc"));
		return;
	}
	static const FKey Row[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four};
	static const FKey Pad[] = {EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour};
	for (int32 N = 0; N < 4; ++N)
	{
		if (PC->WasInputKeyJustPressed(Row[N]) || PC->WasInputKeyJustPressed(Pad[N]))
		{
			Key(FString::FromInt(N + 1));
			return;
		}
	}
}

// MARK: - Every frame

void UAcCommandMapSubsystem::OnFrame(const FAcFrame& Frame)
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || !Sim->IsRunning()) return;
	if (UAcCommandsSubsystem* C = UAcCommandsSubsystem::Get(this); C && C != BoundCommands.Get())
	{
		CommanderHandle = C->OnCommanderChanged.AddWeakLambda(this, [this]() { Refresh(true); });
		BoundCommands = C;
	}
	++Frames;
	if (!bStaged && Frames > 2) StageCommandLine(*Sim);
	PollKeys();
	if (bOpen)
	{
		// Driving shuts it (`updateMap`).
		if (Sim->Simulation().pilot) Close();
		// Keep D4/D5 blocked (a new pointer or pawn after a restart).
		else SetBlocked(true);
	}
	if (bOpen) FitMinimap();
	// The panel's plate baked ahead, so the first open shows it finished.
	else if (Mount())
	{
		if (const AAcHUD* Hud = AAcHUD::Get(this); Hud && Hud->Root()) Widget->Prebake(Hud->Root()->PointsSize());
	}
	Refresh(false);
}

void UAcCommandMapSubsystem::StageCommandLine(UAcSimSubsystem& Sim)
{
	bStaged = true;
	if (!bWantOpen && !bWantQueue && !WantOrders && !WantGround) return;
	ac::Simulation& S = Sim.Simulation();
	const int64 Team = Sim.LocalPlayer();
	auto Issue = [&](const ac::Command& C)
	{
		const bool bOk = Sim.Issue(C);
		UE_LOG(LogAutocraft, Log, TEXT("command map: staged %s %s"), *FAcCommandMapLogic::Describe(C), bOk ? TEXT("ok") : TEXT("refused"));
	};
	if (bWantQueue)
	{
		// Swift `windowshot --queue`.
		std::optional<int64_t> Garrison;
		for (const ac::Structure& St : S.state.structures)
		{
			if (St.owner == Team && St.kind == ac::StructureKind::garrison)
			{
				Garrison = St.id;
				break;
			}
		}
		using What = ac::Request::What;
		Issue(ac::Command(ac::Command::Request{Team, What(What::Unit{ac::UnitKind::longbow}), 3, false, std::nullopt, std::nullopt}));
		Issue(ac::Command(ac::Command::Request{Team, What(What::Upgrade{ac::Upgrade::aegisShield}), 1, false, Garrison, std::nullopt}));
		Issue(ac::Command(ac::Command::Request{Team, What(What::Unit{ac::UnitKind::ranger}), 1, true, Garrison, std::nullopt}));
		Issue(ac::Command(ac::Command::Request{Team, What(What::Building{ac::StructureKind::bastion}), 1, false, std::nullopt, std::nullopt}));
		Issue(ac::Command(ac::Command::Keep{Team, 300, 0}));
		Issue(ac::Command(ac::Command::Split{Team, ac::Harvest::hydrogen}));
	}
	TOptional<ac::Vec2> MenuAt;
	if (WantOrders)
	{
		// Swift `windowshot --orders`.
		const ac::Structure* Home = nullptr;
		const ac::Structure* Foe = nullptr;
		for (const ac::Structure& St : S.state.structures)
		{
			if (St.kind != ac::StructureKind::citadel) continue;
			if (!Home && St.owner == Team) Home = &St;
			if (!Foe && S.state.hostile(St.owner, Team)) Foe = &St;
		}
		if (Home && Foe)
		{
			const ac::Vec2 HomeAt = Home->position, FoeAt = Foe->position;
			const int64 FoeOwner = Foe->owner;
			std::set<int64_t> Held;
			for (const ac::Structure& St : S.state.structures)
			{
				if (St.kind == ac::StructureKind::citadel) Held.insert(S.site(St));
			}
			TArray<int64> Free;
			for (int64 I = 0; I < (int64)S.sites.size(); ++I)
			{
				if (!Held.count(I)) Free.Add(I);
			}
			Free.Sort([&](int64 A, int64 B) { return ac::distance(S.sites[(size_t)A], HomeAt) < ac::distance(S.sites[(size_t)B], HomeAt); });
			Issue(ac::Command(ac::Command::Objective{Team, ac::Objective::Kind::attack, FoeAt}));
			Issue(ac::Command(ac::Command::Objective{Team, ac::Objective::Kind::defend, HomeAt}));
			Issue(ac::Command(ac::Command::Stance{Team, ac::Stance::aggressive}));
			if (!Free.IsEmpty())
			{
				Issue(ac::Command(ac::Command::Request{Team, ac::Request::What(ac::Request::What::Building{ac::StructureKind::citadel}), 1, false,
					std::nullopt, Free[0]}));
			}
			const ac::Vec2 Way = ac::normalize(FoeAt - HomeAt);
			const ac::Vec2 BastionAt = HomeAt + Way * 10.0;
			S.state.structures.push_back(ac::Structure(S.state.nextID, ac::StructureKind::bastion, Team, BastionAt));
			S.state.nextID += 1;
			// A few buildings behind each main, for their icons (one still going up).
			const ac::Vec2 Side(-Way.y, Way.x);
			const struct { ac::Vec2 At, Dir; int64 Owner; } Mains[] = {{HomeAt, ac::Vec2(-Way.x, -Way.y), Team}, {FoeAt, Way, FoeOwner}};
			for (const auto& Main : Mains)
			{
				const TPair<ac::StructureKind, ac::Vec2> Kinds[] = {
					{ac::StructureKind::garrison, Main.Dir * 9.0 + Side * 5.0}, {ac::StructureKind::foundry, Main.Dir * 9.0 - Side * 5.0},
					{ac::StructureKind::habDome, Side * 9.0}, {ac::StructureKind::habDome, Side * 11.0 + Main.Dir * 2.0},
					{ac::StructureKind::spacedock, Main.Dir * 15.0}};
				for (int32 K = 0; K < 5; ++K)
				{
					S.state.structures.push_back(ac::Structure(S.state.nextID, Kinds[K].Key, Main.Owner, Main.At + Kinds[K].Value,
						K == 4 ? std::optional<double>(20.0) : std::nullopt));
					S.state.nextID += 1;
				}
			}
			const bool bOnSite = *WantOrders == TEXT("site");
			MenuAt = bOnSite ? (Free.Num() > 1 ? S.sites[(size_t)Free[1]] : FoeAt) : BastionAt;
		}
		else
		{
			UE_LOG(LogAutocraft, Warning, TEXT("command map: -AcOrders needs a Citadel each side"));
		}
		// Staged by hand: the new buildings' sight explores now
		// (`fogSnapshot`: `sim.lookNow()` before the picture).
		S.lookNow();
	}
	if (bWantOpen || WantOrders || WantGround)
	{
		if (!Open())
		{
			UE_LOG(LogAutocraft, Warning, TEXT("command map: could not open (no commander for the team, driving, or no HUD)"));
			return;
		}
	}
	if (MenuAt) ClickGround(*MenuAt);
	if (WantGround) ClickGround(*WantGround);
	if (WantPick > 0) PickMenu(WantPick - 1);
	Refresh(true);
	// `-AcOrders=world`: the orders given, the map shut again (the beacons in the world).
	if ((WantOrders && *WantOrders == TEXT("world")) || FParse::Param(FCommandLine::Get(), TEXT("AcMapShut"))) Close();
}
