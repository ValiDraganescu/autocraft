#include "AcPlayground.h"

#include "AcConsole.h"
#include "AcDoodads.h"
#include "AcHUD.h"
#include "AcHologram.h"
#include "AcHudStyle.h"
#include "AcLog.h"
#include "AcModelCatalog.h"
#include "AcPilotPawn.h"
#include "AcPointer.h"
#include "AcPose.h"
#include "AcRtsPawn.h"
#include "AcSettings.h"
#include "AcShot.h"
#include "AcShotScenes.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"
#include "AcTerrain.h"
#include "SAcConsole.h"
#include "SAcPalette.h"
#include "SAcRoot.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"

#include "FreeView.h"
#include "NavGrid.h"
#include "Rules.h"
#include "Simulation.h"

namespace
{
	FString Text(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }
	FString Text(const std::string_view S) { return FString(int32(S.size()), UTF8_TO_TCHAR(std::string(S).c_str())); }

	constexpr double Pi = UE_DOUBLE_PI;

	/// `ModelCatalog.titles` (the rest: the name with a capital).
	FString CatalogTitle(const FString& Name)
	{
		static const TMap<FString, FString> Titles = {
			{TEXT("citadel"), TEXT("Citadel")}, {TEXT("garrison"), TEXT("Garrison")}, {TEXT("habdome"), TEXT("Hab Dome")},
			{TEXT("bastion"), TEXT("Bastion")}, {TEXT("prospector"), TEXT("Prospector")}, {TEXT("ranger"), TEXT("Ranger")},
			{TEXT("comet"), TEXT("Comet")}, {TEXT("derrick"), TEXT("Derrick")}, {TEXT("juggernaut"), TEXT("Juggernaut")},
			{TEXT("firefly"), TEXT("Firefly")}, {TEXT("longbow"), TEXT("Longbow")}, {TEXT("dropship"), TEXT("Dropship")},
			{TEXT("foundry"), TEXT("Foundry")}, {TEXT("spacedock"), TEXT("Spacedock")}, {TEXT("lab"), TEXT("Lab")},
			{TEXT("kestrel"), TEXT("Kestrel")}, {TEXT("hailstorm"), TEXT("Hailstorm")}, {TEXT("sentinel"), TEXT("Sentinel")},
			{TEXT("ore"), TEXT("Ore deposit")}, {TEXT("well"), TEXT("MH well")}, {TEXT("rockSpire"), TEXT("Rock spire")},
			{TEXT("crateStack"), TEXT("Crate stack")}, {TEXT("deadTree"), TEXT("Dead tree")},
		};
		if (const FString* T = Titles.Find(Name)) return *T;
		return Name.Left(1).ToUpper() + Name.Mid(1);
	}

	double RandomTurn() { return FMath::FRandRange(0.0, 2 * Pi); }
	uint32 RandomVariant() { return uint32(FMath::RandRange(1, MAX_int32)); }

	bool ParsePair(const FString& S, double& X, double& Y)
	{
		FString A, B;
		if (!S.Split(TEXT(","), &A, &B)) return false;
		X = FCString::Atod(*A);
		Y = FCString::Atod(*B);
		return true;
	}

	const TCHAR* ConfigSection = TEXT("/Script/Autocraft.AcPlayground");
}

// MARK: - Items

FAcPaletteItem FAcPaletteItem::OfStructure(const ac::StructureKind K)
{
	FAcPaletteItem I;
	I.Type = EType::Structure;
	I.Structure = K;
	return I;
}

FAcPaletteItem FAcPaletteItem::OfUnit(const ac::UnitKind K)
{
	FAcPaletteItem I;
	I.Type = EType::Unit;
	I.Unit = K;
	return I;
}

FAcPaletteItem FAcPaletteItem::OfDoodad(const ac::Doodad::Kind K)
{
	FAcPaletteItem I;
	I.Type = EType::Doodad;
	I.Doodad = K;
	return I;
}

FAcPaletteItem FAcPaletteItem::OfType(const EType T)
{
	FAcPaletteItem I;
	I.Type = T;
	return I;
}

bool FAcPaletteItem::operator==(const FAcPaletteItem& O) const
{
	if (Type != O.Type) return false;
	switch (Type)
	{
	case EType::Structure: return Structure == O.Structure;
	case EType::Unit: return Unit == O.Unit;
	case EType::Doodad: return Doodad == O.Doodad;
	default: return true;
	}
}

FString FAcPaletteItem::CatalogName() const
{
	switch (Type)
	{
	case EType::Structure: return Text(ac::rawValue(Structure)).ToLower();
	case EType::Unit: return Text(ac::rawValue(Unit));
	case EType::Ore: return TEXT("ore");
	case EType::Well: return TEXT("well");
	case EType::Doodad: return Text(ac::rawValue(Doodad));
	}
	return FString();
}

FString FAcPaletteItem::Title() const { return CatalogTitle(CatalogName()); }

const TArray<TPair<FString, TArray<FAcPaletteItem>>>& FAcPaletteItem::Groups()
{
	static const TArray<TPair<FString, TArray<FAcPaletteItem>>> G = [] {
		TArray<TPair<FString, TArray<FAcPaletteItem>>> Out;
		TArray<FAcPaletteItem> Buildings, Units, Doodads;
		for (const ac::StructureKind K : ac::allCases<ac::StructureKind>()) Buildings.Add(OfStructure(K));
		for (const ac::UnitKind K : ac::allCases<ac::UnitKind>()) Units.Add(OfUnit(K));
		for (const ac::Doodad::Kind K : ac::allCases<ac::Doodad::Kind>()) Doodads.Add(OfDoodad(K));
		Out.Emplace(TEXT("Buildings"), Buildings);
		Out.Emplace(TEXT("Units"), Units);
		Out.Emplace(TEXT("Resources"), TArray<FAcPaletteItem>{OfType(EType::Ore), OfType(EType::Well)});
		Out.Emplace(TEXT("Doodads"), Doodads);
		return Out;
	}();
	return G;
}

TOptional<FAcPaletteItem> FAcPaletteItem::Named(const FString& Name)
{
	for (const auto& [Group, Items] : Groups())
		for (const FAcPaletteItem& I : Items)
			if (I.CatalogName().Equals(Name, ESearchCase::IgnoreCase)) return I;
	return {};
}

// MARK: - Keys

namespace
{
	class FAcPlaygroundKeys final : public IInputProcessor
	{
	public:
		explicit FAcPlaygroundKeys(UAcPlaygroundSubsystem* In) : Playground(In) {}
		virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
		virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
		{
			UAcPlaygroundSubsystem* P = Playground.Get();
			if (!P || Event.IsRepeat()) return false;
			const FKey K = Event.GetKey();
			const bool bCommand = Event.IsCommandDown();
			if (P->IsAskingClear())
			{
				if (K == EKeys::Enter) P->ClearNow();
				else if (K == EKeys::Escape) P->CancelClear();
				return true;
			}
			if (bCommand && !Event.IsAltDown())
			{
				// The playground's menu: ⌘N clears it, ⌘⇧D drives; F1's ⌘R
				// (restart a map) has no meaning here.
				if (K == EKeys::N && !Event.IsShiftDown()) { P->AskClear(); return true; }
				if (K == EKeys::D && Event.IsShiftDown()) { P->TogglePilot(); return true; }
				if (K == EKeys::R && !Event.IsShiftDown()) return true;
				return false;
			}
			if (P->Piloting() || Event.IsControlDown() || Event.IsAltDown()) return false;
			if (K == EKeys::Escape && P->Held()) { P->Drop(); return true; }
			if (K == EKeys::R && P->Held()) { P->Turn(); return true; }
			if (K == EKeys::BackSpace || K == EKeys::Delete)
			{
				FVector2D C;
				const AAcRtsPawn* Pawn = AAcRtsPawn::Get(P);
				if (Pawn && Pawn->CursorPoint(C)) P->RemoveAt(C);
				return true;
			}
			return false;
		}
		virtual const TCHAR* GetDebugName() const override { return TEXT("AcPlaygroundKeys"); }

	private:
		TWeakObjectPtr<UAcPlaygroundSubsystem> Playground;
	};

	UAcPlaygroundSubsystem* Of(UWorld* World) { return World ? World->GetSubsystem<UAcPlaygroundSubsystem>() : nullptr; }

	FAutoConsoleCommandWithWorldAndArgs CmdPalette(TEXT("ac.Palette"),
		TEXT("Playground: hold a palette item (ac.Palette citadel|ranger|ore|well|rockSpire...; ac.Palette none lets go)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) {
			UAcPlaygroundSubsystem* P = Of(World);
			if (!P) return;
			const TOptional<FAcPaletteItem> I = Args.Num() > 0 ? FAcPaletteItem::Named(Args[0]) : TOptional<FAcPaletteItem>();
			if (Args.Num() > 0 && !I && Args[0] != TEXT("none")) UE_LOG(LogAutocraft, Warning, TEXT("playground: no palette item %s"), *Args[0]);
			P->Hold(I);
		}));
	FAutoConsoleCommandWithWorldAndArgs CmdSide(TEXT("ac.PaletteSide"), TEXT("Playground: the side things go to (0..7)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) {
			if (UAcPlaygroundSubsystem* P = Of(World); P && Args.Num() > 0) P->SetTeam(FCString::Atoi(*Args[0]));
		}));
	FAutoConsoleCommandWithWorldAndArgs CmdPut(TEXT("ac.PlaygroundPut"),
		TEXT("Playground: put NAME down at ground X Y through the palette and a click: ac.PlaygroundPut ranger 3 -2 [SIDE [TURN]]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) {
			UAcPlaygroundSubsystem* P = Of(World);
			if (!P || Args.Num() < 3) return;
			P->Put(Args[0], ac::Vec2(FCString::Atod(*Args[1]), FCString::Atod(*Args[2])),
				Args.Num() > 3 ? TOptional<int32>(FCString::Atoi(*Args[3])) : TOptional<int32>(),
				Args.Num() > 4 ? TOptional<double>(FCString::Atod(*Args[4])) : TOptional<double>());
			P->Hold({});
		}));
	FAutoConsoleCommandWithWorldAndArgs CmdDelete(TEXT("ac.PlaygroundDelete"),
		TEXT("Playground: Delete with the pointer over ground X Y."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) {
			UAcPlaygroundSubsystem* P = Of(World);
			if (!P || Args.Num() < 2) return;
			if (const TOptional<FVector2D> C = P->ViewPointOf(ac::Vec2(FCString::Atod(*Args[0]), FCString::Atod(*Args[1])))) P->RemoveAt(*C);
		}));
	FAutoConsoleCommandWithWorldAndArgs CmdFog(TEXT("ac.PlaygroundFog"), TEXT("Playground: fog of war on (1) or off (0)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) {
			if (UAcPlaygroundSubsystem* P = Of(World)) P->SetFog(Args.Num() > 0 ? Args[0] != TEXT("0") : !P->IsFogOn());
		}));
	FAutoConsoleCommandWithWorldAndArgs CmdUnlimited(TEXT("ac.PlaygroundUnlimited"),
		TEXT("Playground: every side's ore and MH topped up (1) or not (0)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) {
			if (UAcPlaygroundSubsystem* P = Of(World)) P->SetUnlimited(Args.Num() > 0 ? Args[0] != TEXT("0") : !P->IsUnlimited());
		}));
	FAutoConsoleCommandWithWorldAndArgs CmdClear(TEXT("ac.PlaygroundClear"), TEXT("Playground: take everything away (no question)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World) {
			if (UAcPlaygroundSubsystem* P = Of(World)) P->ClearNow();
		}));
}

// MARK: - The subsystem

UAcPlaygroundSubsystem* UAcPlaygroundSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UAcPlaygroundSubsystem>() : nullptr;
}

bool UAcPlaygroundSubsystem::IsPlaygroundRun() { return FParse::Param(FCommandLine::Get(), TEXT("AcPlayground")); }

bool UAcPlaygroundSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer) && IsPlaygroundRun();
}

bool UAcPlaygroundSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UAcPlaygroundSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UAcPlaygroundSubsystem, STATGROUP_Tickables); }

void UAcPlaygroundSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UAcSimSubsystem* Sim = Collection.InitializeDependency<UAcSimSubsystem>();
	if (Sim)
	{
		StartedHandle = Sim->OnGameStarted.AddUObject(this, &UAcPlaygroundSubsystem::OnGameStarted);
		FrameHandle = Sim->AddFrameListener(EAcFrameStage::Hud, FAcFrameEvent::FDelegate::CreateUObject(this, &UAcPlaygroundSubsystem::OnFrame));
	}
	bActive = true;
	LoadSettings();
	const TCHAR* Cmd = FCommandLine::Get();
	FString S;
	if (FParse::Value(Cmd, TEXT("AcPlaygroundSide="), S)) PaletteTeam = FMath::Clamp(FCString::Atoi(*S), 0, Sides - 1);
	if (FSlateApplication::IsInitialized())
	{
		// First, ahead of F1's ⌘N/⌘R.
		Keys = MakeShared<FAcPlaygroundKeys>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(Keys, 0);
	}
	UE_LOG(LogAutocraft, Log, TEXT("playground: on (unlimited %s)"), bUnlimited ? TEXT("on") : TEXT("off"));
}

void UAcPlaygroundSubsystem::Deinitialize()
{
	if (UAcSimSubsystem* Sim = GetWorld() ? GetWorld()->GetSubsystem<UAcSimSubsystem>() : nullptr)
	{
		Sim->OnGameStarted.Remove(StartedHandle);
		Sim->RemoveFrameListener(EAcFrameStage::Hud, FrameHandle);
	}
	if (Keys && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(Keys);
	Keys.Reset();
	if (const AAcHUD* Hud = AAcHUD::Get(this); Hud && Hud->Root())
	{
		if (List) Hud->Root()->RemoveLayer(List.ToSharedRef());
		if (Status) Hud->Root()->RemoveLayer(Status.ToSharedRef());
		if (Confirm) Hud->Root()->RemoveLayer(Confirm.ToSharedRef());
	}
	List.Reset();
	Status.Reset();
	Confirm.Reset();
	Super::Deinitialize();
}

void UAcPlaygroundSubsystem::LoadSettings()
{
	// Swift's `playgroundUnlimited` default (on); kept only by remembering runs.
	if (!UAcSettings::Remembers() || FParse::Param(FCommandLine::Get(), TEXT("AcPlaygroundTest")) || !GConfig) return;
	bool b = true;
	if (GConfig->GetBool(ConfigSection, TEXT("bUnlimited"), b, GGameUserSettingsIni)) bUnlimited = b;
}

void UAcPlaygroundSubsystem::SaveSettings() const
{
	// The self-test flips unlimited off and on: never into the player's settings.
	if (!UAcSettings::Remembers() || FParse::Param(FCommandLine::Get(), TEXT("AcPlaygroundTest")) || !GConfig) return;
	GConfig->SetBool(ConfigSection, TEXT("bUnlimited"), bUnlimited, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void UAcPlaygroundSubsystem::OnGameStarted(UAcSimSubsystem&)
{
	// The sim was built again (fog, Clear): what was held stays held.
	TopUp();
}

bool UAcPlaygroundSubsystem::Hook()
{
	if (bHooked) return true;
	UAcPointer* Pointer = UAcPointer::Get(this);
	const AAcHUD* Hud = AAcHUD::Get(this);
	const TSharedPtr<SAcRoot> Root = Hud ? Hud->Root() : nullptr;
	if (!Pointer || !Root) return false;
	bHooked = true;
	const TWeakObjectPtr<UAcPlaygroundSubsystem> Weak(this);
	// A click while the palette holds something puts it down (before the
	// world's drive/select); over the console it is the console's.
	Pointer->ClickHandlers.Insert([Weak](const FVector2D P) {
		UAcPlaygroundSubsystem* S = Weak.Get();
		if (!S || !S->Held() || S->Piloting() || S->OverHud(P)) return false;
		S->PlaceAt(P);
		return true;
	}, 0);
	// Its hologram, no cue (`updateHover`).
	Pointer->HoverSuppressed = [Weak]() { return Weak.IsValid() && Weak->Held().IsSet() && !Weak->Piloting(); };
	Ghost = AAcHologram::Find(GetWorld());
	if (!Ghost.IsValid()) Ghost = AAcHologram::SpawnFor(GetWorld());

	Root->AddLayer(AcHudLayer::Console + 5).HAlign(HAlign_Left).VAlign(VAlign_Fill)
		.Padding(TAttribute<FMargin>::CreateWeakLambda(this, [this]() { return FMargin(0, 0, 0, ColumnInset()); }))
	[
		SAssignNew(List, SAcPalette).Playground(this)
	];
	// The status line under the scoreboard's row, taking no clicks.
	Root->AddLayer(AcHudLayer::Console + 6).HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(FMargin(SAcPalette::Width + 8, 60, 0, 0))
	[
		SAssignNew(Status, SAcPaletteStatus).Playground(this).Visibility(EVisibility::HitTestInvisible)
	];
	return true;
}

void UAcPlaygroundSubsystem::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bActive) return;
	if (!Hook()) return;
	// Driving lets go of the palette (`dropPalette`).
	if (Palette && Piloting()) Drop();
	// The top-down view starts right of the column, as Swift's stage sits
	// beside its list. Driving keeps the whole window: the cockpit's
	// overlay is laid out on it, so the column goes while driving (it
	// would cover the cockpit's left).
	if (List)
	{
		const EVisibility Want = Piloting() ? EVisibility::Collapsed : EVisibility::Visible;
		if (List->GetVisibility() != Want) List->SetVisibility(Want);
	}
	if (UWorld* World = GetWorld())
	{
		// Not `AAcRtsPawn::Get`: while driving the controller holds the pilot.
		for (TActorIterator<AAcRtsPawn> It(World); It; ++It) It->SetViewInset(Piloting() ? 0.0 : double(SAcPalette::Width));
	}
	// A right click lets go too.
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController(); PC && Palette && PC->WasInputKeyJustPressed(EKeys::RightMouseButton)) Drop();
	}
}

void UAcPlaygroundSubsystem::OnFrame(const FAcFrame& Frame)
{
	if (!bActive) return;
	++Frames;
	// Before the next step (Swift tops up just before `sim.step`).
	TopUp();
	const double Time = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	if (!bStaged && bHooked && Frames >= 3 && AAcRtsPawn::Get(this) && AAcRtsPawn::Get(this)->View()) Staged();
	UpdateGhost(Time);
}

void UAcPlaygroundSubsystem::TopUp()
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!bUnlimited || !Sim || !Sim->IsRunning()) return;
	// Not in a shot: `windowshot --playground` lays out, warms and draws
	// without a tick, so its stock is what the game left (65 ore, not 5000).
	if (UAcShotSubsystem::IsShotRun()) return;
	for (ac::Player& P : Sim->Simulation().state.players)
	{
		P.ore = FMath::Max<int64>(P.ore, Stock);
		P.hydrogen = FMath::Max<int64>(P.hydrogen, Stock);
	}
}

bool UAcPlaygroundSubsystem::Piloting() const
{
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	return Sim && Sim->IsRunning() && Sim->Simulation().pilot.has_value();
}

// MARK: - Holding

void UAcPlaygroundSubsystem::Hold(const TOptional<FAcPaletteItem> Item)
{
	Palette = Item;
	PaletteNote.Reset();
	PaletteTurn = Item && Item->Type == FAcPaletteItem::EType::Unit ? Pi / 2 : RandomTurn();
	PaletteVariant = RandomVariant();
	if (Item)
	{
		if (UAcPointer* Pointer = UAcPointer::Get(this)) Pointer->Select({});
	}
	UE_LOG(LogAutocraft, Log, TEXT("playground: holding %s"), Item ? *Item->Title() : TEXT("nothing"));
}

void UAcPlaygroundSubsystem::PickRow(const FAcPaletteItem& Item)
{
	if (!Palette || *Palette != Item)
	{
		if (Piloting())
		{
			if (AAcPilotPawn* Pilot = AAcPilotPawn::Find(this)) Pilot->Leave();
		}
		Hold(Item);
	}
	else
	{
		Hold({});
	}
}

void UAcPlaygroundSubsystem::Drop()
{
	if (!Palette) return;
	Hold({});
}

void UAcPlaygroundSubsystem::Turn()
{
	if (!Palette) return;
	PaletteTurn += Pi / 4;
}

void UAcPlaygroundSubsystem::SetTeam(const int32 Team)
{
	const int32 Was = PaletteTeam;
	PaletteTeam = FMath::Clamp(Team, 0, Sides - 1);
	if (Was != PaletteTeam) UE_LOG(LogAutocraft, Log, TEXT("playground: side %s"), *FAcHudStyle::PlayerName(PaletteTeam));
}

// MARK: - Where it goes

TOptional<ac::Vec2> UAcPlaygroundSubsystem::PickGround(const FVector2D ViewPoint) const
{
	const AAcRtsPawn* Pawn = AAcRtsPawn::Get(this);
	const ac::FreeView* View = Pawn ? Pawn->View() : nullptr;
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!View) return {};
	const ac::Vec2 C(ViewPoint.X, ViewPoint.Y);
	const ac::FreeView::Ray R = View->ray(C);
	if (Sim && Sim->IsRunning() && Sim->Simulation().field)
	{
		if (const std::optional<ac::Vec2> G = Sim->Simulation().field->pick(R.origin, R.direction)) return *G;
	}
	return View->ground(C);
}

TOptional<FVector2D> UAcPlaygroundSubsystem::ViewPointOf(const ac::Vec2 Ground) const
{
	const AAcRtsPawn* Pawn = AAcRtsPawn::Get(this);
	const ac::FreeView* View = Pawn ? Pawn->View() : nullptr;
	if (!View) return {};
	const AAcTerrain* Terrain = AAcTerrain::Find(GetWorld());
	const double Y = Terrain ? Terrain->FieldHeight(Ground) : 0.0;
	const ac::Vec2 C = View->canvas(ac::Vec3(Ground.x, Y, Ground.y));
	return FVector2D(C.x, C.y);
}

UAcPlaygroundSubsystem::FSpot UAcPlaygroundSubsystem::SpotFor(const FAcPaletteItem& Item, const ac::Vec2 G) const
{
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || !Sim->IsRunning()) return {G, std::string("No game")};
	const ac::Simulation& S = Sim->Simulation();
	switch (Item.Type)
	{
	case FAcPaletteItem::EType::Structure:
		if (Item.Structure == ac::StructureKind::lab)
		{
			const std::optional<ac::Structure> Host = S.labHost(G, PaletteTeam);
			if (!Host) return {G, std::string("A Lab goes on the side of a Garrison, Foundry or Spacedock")};
			return {Host->position + ac::Rules::addonOffset, S.labRefusal(*Host)};
		}
		if (const std::optional<ac::Vec2> At = S.snap(Item.Structure, G)) return {*At, S.placementRefusal(Item.Structure, *At)};
		return {G, std::string("A Derrick goes on an MH well")};
	case FAcPaletteItem::EType::Unit: return {G, S.unitRefusal(Item.Unit, G)};
	case FAcPaletteItem::EType::Ore: return {G, S.propRefusal(G, 1.1)};
	case FAcPaletteItem::EType::Well: return {G, S.propRefusal(G, 1.6)};
	case FAcPaletteItem::EType::Doodad:
	{
		const ac::Doodad D{.kind = Item.Doodad, .position = G, .rotation = PaletteTurn, .scale = 1, .variant = PaletteVariant};
		double Reach = 0.0;
		bool bAny = false;
		for (const auto& C : ac::NavGrid::circles(D))
		{
			Reach = FMath::Max(Reach, ac::distance(C.first, G) + C.second);
			bAny = true;
		}
		return {G, S.propRefusal(G, bAny ? Reach : 0.3)};
	}
	}
	return {G, std::nullopt};
}

float UAcPlaygroundSubsystem::ColumnInset() const
{
	const UAcConsoleSubsystem* Console = UAcConsoleSubsystem::Get(this);
	const TSharedPtr<SAcConsole> W = Console ? Console->Console() : nullptr;
	if (!W || !W->GetVisibility().IsVisible()) return 0;
	const FAcCabLayout& L = W->Layout();
	return float(FMath::Max(L.DashTop(0), L.DashTop(SAcPalette::Width)) + 6);
}

bool UAcPlaygroundSubsystem::OverHud(const FVector2D ViewPoint) const
{
	// View points start right of the column (`SetViewInset`); the HUD's are the window's.
	const AAcRtsPawn* Pawn = AAcRtsPawn::Get(this);
	const FVector2D P = ViewPoint + FVector2D(Pawn ? Pawn->GetViewInset() : 0.0, 0);
	if (P.X < SAcPalette::Width && (!List || P.Y < List->GetCachedGeometry().GetLocalSize().Y)) return true;
	if (const UAcConsoleSubsystem* Console = UAcConsoleSubsystem::Get(this))
	{
		if (const TSharedPtr<SAcConsole> W = Console->Console(); W && W->Covers(P)) return true;
	}
	return false;
}

bool UAcPlaygroundSubsystem::PointerPoint(FVector2D& Out) const
{
	if (HeldAt)
	{
		const TOptional<FVector2D> C = ViewPointOf(*HeldAt);
		if (!C) return false;
		Out = *C;
		return true;
	}
	const AAcRtsPawn* Pawn = AAcRtsPawn::Get(this);
	return Pawn && Pawn->CursorPoint(Out);
}

void UAcPlaygroundSubsystem::UpdateGhost(const double Time)
{
	AAcHologram* G = Ghost.Get();
	if (!G) return;
	FVector2D C;
	TOptional<ac::Vec2> Ground;
	if (Palette && !Piloting() && !IsAskingClear() && PointerPoint(C) && !OverHud(C)) Ground = PickGround(C);
	if (!Ground)
	{
		if (bGhostShown) G->Hide();
		bGhostShown = false;
		return;
	}
	bGhostShown = true;
	const FAcPaletteItem& Item = *Palette;
	const FSpot Spot = SpotFor(Item, *Ground);
	PaletteNote = Spot.Why ? Text(*Spot.Why) : FString();
	const AAcTerrain* Terrain = AAcTerrain::Find(GetWorld());
	const double GY = Terrain ? Terrain->FieldHeight(Spot.At) : 0.0;
	// The ground point (the hologram lifts itself 0.02).
	const FVector At = AcSpace::ToWorld(Spot.At, GY);
	const bool bOk = !Spot.Why;
	// SceneKit eulerAngles.y = a is Unreal yaw −a.
	auto Yaw = [](double SkAngle) { return -FMath::RadiansToDegrees(SkAngle); };
	switch (Item.Type)
	{
	case FAcPaletteItem::EType::Structure:
		G->Show(FName(Item.CatalogName() + TEXT("_blue")), At, 0.0, ac::Rules::radius(Item.Structure), bOk, Time);
		break;
	case FAcPaletteItem::EType::Unit:
		G->Show(FName(FString(AcPose::ModelBase(Item.Unit)) + TEXT("_blue")), At, Yaw(-PaletteTurn),
			ac::Rules::stats(Item.Unit).radius, bOk, Time);
		break;
	case FAcPaletteItem::EType::Ore: G->Show(TEXT("ore_3"), At, Yaw(PaletteTurn), 1.0, bOk, Time); break;
	case FAcPaletteItem::EType::Well: G->Show(TEXT("well_3"), At, 0.0, 1.5, bOk, Time); break;
	case FAcPaletteItem::EType::Doodad:
	{
		const FAcModelInfo* M = AAcDoodads::ModelFor(Item.Doodad, PaletteVariant);
		G->Show(M ? M->Name : NAME_None, At, Yaw(PaletteTurn), 0.6, bOk, Time);
		break;
	}
	}
}

// MARK: - Putting down and taking away

bool UAcPlaygroundSubsystem::PlaceAt(const FVector2D ViewPoint)
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Palette || !Sim || !Sim->IsRunning()) return false;
	const TOptional<ac::Vec2> G = PickGround(ViewPoint);
	if (!G) return false;
	const FAcPaletteItem Item = *Palette;
	const FSpot Spot = SpotFor(Item, *G);
	if (Spot.Why)
	{
		// Swift beeps here; the Unreal game stays silent (no system sound).
		PaletteNote = Text(*Spot.Why);
		UE_LOG(LogAutocraft, Log, TEXT("playground: %s can't go at %.1f, %.1f: %s"), *Item.Title(), Spot.At.x, Spot.At.y, *PaletteNote);
		return true;
	}
	ac::Simulation& S = Sim->Simulation();
	switch (Item.Type)
	{
	case FAcPaletteItem::EType::Structure:
		if (Item.Structure == ac::StructureKind::lab)
		{
			if (const std::optional<ac::Structure> Host = S.labHost(Spot.At, PaletteTeam)) S.placeLab(Host->id);
		}
		else
		{
			S.placeStructure(Item.Structure, PaletteTeam, Spot.At);
		}
		break;
	case FAcPaletteItem::EType::Unit: S.placeUnit(Item.Unit, PaletteTeam, Spot.At, PaletteTurn); break;
	case FAcPaletteItem::EType::Ore: S.placePatch(Spot.At, PaletteTurn); break;
	case FAcPaletteItem::EType::Well: S.placeWell(Spot.At); break;
	case FAcPaletteItem::EType::Doodad:
		S.placeDoodad(ac::Doodad{.kind = Item.Doodad, .position = Spot.At, .rotation = PaletteTurn, .scale = 1, .variant = PaletteVariant});
		break;
	}
	S.lookNow();
	Sim->MarkEdited();
	UE_LOG(LogAutocraft, Log, TEXT("playground: %s%s at %.1f, %.1f"), *Item.Title(),
		Item.Owned() ? *(TEXT(" for ") + FAcHudStyle::PlayerName(PaletteTeam)) : TEXT(""), Spot.At.x, Spot.At.y);
	// The next one looks different (a unit keeps facing the camera).
	if (Item.Type != FAcPaletteItem::EType::Unit) PaletteTurn = RandomTurn();
	PaletteVariant = RandomVariant();
	PaletteNote.Reset();
	return true;
}

bool UAcPlaygroundSubsystem::RemoveAt(const FVector2D ViewPoint)
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || !Sim->IsRunning() || Piloting()) return false;
	ac::Simulation& S = Sim->Simulation();
	// What the pointer is on (picked as for a click), else the ground there.
	std::optional<std::string> What;
	if (const UAcPointer* Pointer = UAcPointer::Get(this))
	{
		if (const TOptional<FAcHover> H = Pointer->PickAt(ViewPoint)) What = S.remove(H->Id);
	}
	if (!What)
	{
		if (const TOptional<ac::Vec2> G = PickGround(ViewPoint)) What = S.removeProp(*G);
	}
	if (!What)
	{
		UE_LOG(LogAutocraft, Log, TEXT("playground: nothing to remove at %d,%d"), FMath::RoundToInt(ViewPoint.X), FMath::RoundToInt(ViewPoint.Y));
		return false;
	}
	UE_LOG(LogAutocraft, Log, TEXT("playground: removed %s"), *Text(*What));
	S.lookNow();
	Sim->MarkEdited();
	return true;
}

// MARK: - Toggles

void UAcPlaygroundSubsystem::SetUnlimited(const bool bOn)
{
	bUnlimited = bOn;
	UE_LOG(LogAutocraft, Log, TEXT("playground: unlimited ore and MH %s"), bOn ? TEXT("on") : TEXT("off"));
	SaveSettings();
	TopUp();
}

bool UAcPlaygroundSubsystem::IsFogOn() const
{
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	return Sim && Sim->IsFogOn();
}

void UAcPlaygroundSubsystem::SetFog(const bool bOn)
{
	// `fogChanged`: the session's fog, the sim built again on the same state.
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this)) Sim->SetFog(bOn);
}

void UAcPlaygroundSubsystem::AskClear()
{
	if (Confirm) return;
	const AAcHUD* Hud = AAcHUD::Get(this);
	const TSharedPtr<SAcRoot> Root = Hud ? Hud->Root() : nullptr;
	if (!Root) return;
	Root->AddLayer(AcHudLayer::CommandMap + 15)
	[
		SAssignNew(Confirm, SAcPaletteConfirm).Playground(this)
	];
	if (UAcPointer* Pointer = UAcPointer::Get(this)) Pointer->SetBlocked(true);
}

void UAcPlaygroundSubsystem::CancelClear()
{
	if (!Confirm) return;
	if (const AAcHUD* Hud = AAcHUD::Get(this); Hud && Hud->Root()) Hud->Root()->RemoveLayer(Confirm.ToSharedRef());
	Confirm.Reset();
	if (UAcPointer* Pointer = UAcPointer::Get(this)) Pointer->SetBlocked(false);
}

void UAcPlaygroundSubsystem::ClearNow()
{
	CancelClear();
	if (Piloting())
	{
		if (AAcPilotPawn* Pilot = AAcPilotPawn::Find(this)) Pilot->Leave();
	}
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim) return;
	UE_LOG(LogAutocraft, Log, TEXT("playground: cleared"));
	// `SessionStore.restart` on the playground, then the game opened again.
	Sim->OpenPlayground(true);
}

void UAcPlaygroundSubsystem::TogglePilot()
{
	AAcPilotPawn* Pilot = AAcPilotPawn::Find(this);
	if (!Pilot) return;
	if (Pilot->Driving())
	{
		Pilot->Leave();
		return;
	}
	const AAcRtsPawn* Pawn = AAcRtsPawn::Get(this);
	const ac::FreeView* View = Pawn ? Pawn->View() : nullptr;
	Drop();
	Pilot->DriveNear(View ? View->target : ac::Vec2(0, 0));
}

FString UAcPlaygroundSubsystem::StatusText() const
{
	if (Piloting()) return TEXT("Driving: Esc or F to step out · V: first or third person · Tab: the next unit");
	if (Palette)
	{
		const FString Side = Palette->Owned() ? TEXT(" for ") + FAcHudStyle::PlayerName(PaletteTeam) : FString();
		FString T = Palette->Title() + Side + TEXT(": click to put it down · R: turn it · Esc or right click: done");
		if (!PaletteNote.IsEmpty()) T += TEXT("  —  ") + PaletteNote;
		return T;
	}
	return TEXT("Click a unit to drive it, a building for its card · Delete: take away what is under the pointer · drag: pan · scroll or pinch: zoom");
}

// MARK: - Scripted

bool UAcPlaygroundSubsystem::Put(const FString& Name, const ac::Vec2 At, const TOptional<int32> Side, const TOptional<double> InTurn)
{
	const TOptional<FAcPaletteItem> Item = FAcPaletteItem::Named(Name);
	TOptional<FVector2D> C = ViewPointOf(At);
	// Under the palette or the console: bring it into view first, as a
	// person would pan there.
	if (C && OverHud(*C))
	{
		if (AAcRtsPawn* Pawn = AAcRtsPawn::Get(this)) Pawn->CenterOn(At);
		C = ViewPointOf(At);
	}
	UAcPointer* Pointer = UAcPointer::Get(this);
	if (!Item || !C || !Pointer || !List)
	{
		UE_LOG(LogAutocraft, Warning, TEXT("playground: can't put %s"), *Name);
		return false;
	}
	if (Side) SetTeam(*Side);
	// The row clicked as the mouse would (held already: it stays held).
	if (!Palette || *Palette != *Item) List->ClickItem(*Item);
	if (InTurn) PaletteTurn = *InTurn;
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	const size_t Before = Sim->State().units.size() + Sim->State().structures.size() + Sim->State().patches.size()
		+ Sim->State().wells.value_or(std::vector<ac::Well>{}).size() + Sim->State().doodads.value_or(std::vector<ac::Doodad>{}).size();
	// A click there, through the pointer's routing.
	Pointer->Click(*C);
	const size_t After = Sim->State().units.size() + Sim->State().structures.size() + Sim->State().patches.size()
		+ Sim->State().wells.value_or(std::vector<ac::Well>{}).size() + Sim->State().doodads.value_or(std::vector<ac::Doodad>{}).size();
	if (After == Before)
	{
		UE_LOG(LogAutocraft, Warning, TEXT("could not put %s at %.1f, %.1f: %s"), *Item->Title(), At.x, At.y,
			PaletteNote.IsEmpty() ? TEXT("not placed") : *PaletteNote);
		return false;
	}
	return true;
}

void UAcPlaygroundSubsystem::LayOut()
{
	// `WindowSnapshot.layPlayground`: blue's base on the west with ore, a
	// well under a Derrick and its army, red's Garrison and squad on the
	// east, and scenery between. (x, z) are sim (x, y).
	if (AAcRtsPawn* Pawn = AAcRtsPawn::Get(this)) Pawn->CenterOn(ac::Vec2(0, 0));
	auto P = [this](const TCHAR* N, double X, double Z, int32 Team = 0, TOptional<double> Turn = {}) {
		Put(N, ac::Vec2(X, Z), Team, Turn);
	};
	P(TEXT("citadel"), -15, 0);
	const double OreZ[] = {-4.5, -1.5, 1.5, 4.5};
	for (int32 K = 0; K < 4; ++K) P(TEXT("ore"), -23 - double(K % 2), OreZ[K], 0, Pi / 2);
	P(TEXT("well"), -17, 9);
	P(TEXT("derrick"), -17, 9);
	P(TEXT("habdome"), -15, -9);
	P(TEXT("garrison"), -8, 7);
	P(TEXT("lab"), -5, 7);
	for (double Z : {-2.0, 0.0, 2.0}) P(TEXT("prospector"), -19, Z);
	for (double Z : {-3.0, -1.0, 1.0, 3.0}) P(TEXT("ranger"), -4, Z, 0, 0.0);
	P(TEXT("juggernaut"), -6, -4, 0, 0.0);
	P(TEXT("longbow"), -7, 1, 0, 0.0);
	P(TEXT("dropship"), -9, -6, 0, 0.0);
	P(TEXT("sentinel"), -11, -10);
	P(TEXT("hailstorm"), -9, 3, 0, 0.0);
	P(TEXT("kestrel"), -12, 3, 0, 0.0);
	P(TEXT("garrison"), 16, -3, 1);
	for (double Z : {-2.0, 0.0, 2.0}) P(TEXT("ranger"), 9, Z, 1, Pi);
	P(TEXT("comet"), 10, 4, 1, Pi);
	P(TEXT("firefly"), 11, -5, 1, Pi);
	P(TEXT("kestrel"), 13, 1, 1, Pi);
	P(TEXT("tower"), 2, 8);
	P(TEXT("rockSpire"), 1, -8);
	P(TEXT("crateStack"), -1, 3);
	P(TEXT("boulder"), 4, -3);
	P(TEXT("deadTree"), 6, 10);
	for (const ac::Vec2 B : {ac::Vec2(-2, -5), ac::Vec2(3, 5), ac::Vec2(7, -9)}) P(TEXT("bush"), B.x, B.y);
	SetTeam(0);
	Hold({});
	if (AAcRtsPawn* Pawn = AAcRtsPawn::Get(this)) Pawn->CenterOn(ac::Vec2(0, 0));
	const ac::GameState& S = UAcSimSubsystem::Get(this)->State();
	UE_LOG(LogAutocraft, Log, TEXT("playground: %d buildings, %d units, %d ore, %d wells, %d doodads"), (int32)S.structures.size(),
		(int32)S.units.size(), (int32)S.patches.size(), (int32)S.wells.value_or(std::vector<ac::Well>{}).size(),
		(int32)S.doodads.value_or(std::vector<ac::Doodad>{}).size());
}

void UAcPlaygroundSubsystem::Staged()
{
	bStaged = true;
	const TCHAR* Cmd = FCommandLine::Get();
	FString S;
	if (FParse::Param(Cmd, TEXT("AcPlaygroundLayout")))
	{
		LayOut();
		// `windowshot --playground`: laid out, then warmed (-AcWarm).
		if (UAcShotWarm* Warm = GetWorld() ? GetWorld()->GetSubsystem<UAcShotWarm>() : nullptr) Warm->Warm();
		// The shot's camera over its point again (`windowshot --at`).
		double X, Y;
		if (FParse::Value(Cmd, TEXT("AcCamAt="), S, false) && ParsePair(S.TrimQuotes(), X, Y))
		{
			if (AAcRtsPawn* Pawn = AAcRtsPawn::Get(this)) Pawn->CenterOn(ac::Vec2(X, Y));
		}
	}
	if (FParse::Value(Cmd, TEXT("AcPlaygroundHold="), S))
	{
		double X = 0, Y = -2;
		FString At;
		if (FParse::Value(Cmd, TEXT("AcPlaygroundAt="), At, false)) ParsePair(At.TrimQuotes(), X, Y);
		const TOptional<FAcPaletteItem> Item = FAcPaletteItem::Named(S);
		if (Item && List)
		{
			List->ClickItem(*Item);
			HeldAt = ac::Vec2(X, Y);
			const FSpot Spot = SpotFor(*Item, *HeldAt);
			UE_LOG(LogAutocraft, Log, TEXT("playground: holding %s at %.1f, %.1f: %s"), *Item->Title(), X, Y,
				Spot.Why ? *Text(*Spot.Why) : TEXT("fits"));
		}
		else
		{
			UE_LOG(LogAutocraft, Warning, TEXT("playground: no palette item %s"), *S);
		}
	}
	if (FParse::Param(Cmd, TEXT("AcPlaygroundTest"))) RunTest();
}

void UAcPlaygroundSubsystem::RunTest()
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	int32 Failures = 0;
	auto Check = [&Failures](bool bOk, const FString& What) {
		UE_LOG(LogAutocraft, Log, TEXT("playgroundtest: %s %s"), *What, bOk ? TEXT("ok") : TEXT("FAILED"));
		if (!bOk) ++Failures;
	};
	auto Count = [Sim](auto Pred) {
		int32 N = 0;
		for (const ac::Unit& U : Sim->State().units) N += Pred(U) ? 1 : 0;
		return N;
	};
	const ac::GameState& S0 = Sim->State();
	Check(S0.players.size() == 8, FString::Printf(TEXT("eight sides (%d)"), (int32)S0.players.size()));
	Check(!Sim->IsAIOn(), TEXT("no AI"));
	// Every kind goes down from the palette, for side 5.
	const int32 Units0 = (int32)Sim->State().units.size();
	int32 Placed = 0;
	double X = -30;
	for (const ac::UnitKind K : ac::allCases<ac::UnitKind>())
	{
		Placed += Put(Text(ac::rawValue(K)), ac::Vec2(X, -18), 5, Pi / 2) ? 1 : 0;
		X += 3;
	}
	Check(Placed == (int32)ac::allCases<ac::UnitKind>().size() && (int32)Sim->State().units.size() == Units0 + Placed,
		FString::Printf(TEXT("every unit kind put down (%d)"), Placed));
	Check(Count([](const ac::Unit& U) { return U.owner == 5; }) == Placed, TEXT("for Teal (side 5)"));
	// A building over another is refused, with the reason.
	const int32 Buildings0 = (int32)Sim->State().structures.size();
	const bool bPut = Put(TEXT("citadel"), ac::Vec2(-15, 0.5), 0);
	Check(!bPut && (int32)Sim->State().structures.size() == Buildings0 && !PaletteNote.IsEmpty(),
		FString::Printf(TEXT("refused on top of the Citadel (\"%s\")"), *PaletteNote));
	// A Lab needs a host; one on red's Garrison goes on its add-on spot.
	Check(Put(TEXT("lab"), ac::Vec2(18.5, -2.5), 1), TEXT("red Lab on red's Garrison"));
	Hold({});
	// Delete: a unit under the pointer (killed as in a fight), a bush, an ore deposit.
	const ac::Unit* Ranger = nullptr;
	for (const ac::Unit& U : Sim->State().units)
		if (U.owner == 1 && U.kind == ac::UnitKind::ranger) Ranger = &U;
	if (Ranger)
	{
		const int64 Id = Ranger->id;
		const TOptional<FVector2D> C = ViewPointOf(Ranger->position);
		RemoveAt(*C);
		bool bDead = false;
		for (const ac::Unit& U : Sim->State().units) bDead |= U.id == Id && U.hp <= 0;
		Check(bDead, TEXT("Delete kills the red Ranger under the pointer"));
	}
	const int32 Doodads0 = (int32)Sim->State().doodads.value_or(std::vector<ac::Doodad>{}).size();
	RemoveAt(*ViewPointOf(ac::Vec2(7, -9)));
	Check((int32)Sim->State().doodads.value_or(std::vector<ac::Doodad>{}).size() == Doodads0 - 1, TEXT("Delete takes a bush away"));
	RemoveAt(*ViewPointOf(ac::Vec2(-23, -4.5)));
	bool bMined = false;
	for (const ac::OreDeposit& P : Sim->State().patches) bMined |= P.remaining == 0 && ac::distance(P.position, ac::Vec2(-23, -4.5)) < 0.1;
	Check(bMined, TEXT("Delete mines an ore deposit out"));
	// Unlimited: a side's stock comes back.
	Sim->Simulation().state.players[3].ore = 0;
	TopUp();
	Check(Sim->State().players[3].ore == Stock, TEXT("unlimited tops up"));
	SetUnlimited(false);
	Sim->Simulation().state.players[3].ore = 7;
	TopUp();
	Check(Sim->State().players[3].ore == 7, TEXT("limited leaves it"));
	SetUnlimited(true);
	// Fog: the sim built again on the same state.
	const int32 Units1 = (int32)Sim->State().units.size();
	SetFog(true);
	Check(IsFogOn() && (int32)Sim->State().units.size() == Units1 && Sim->State().players.size() == 8, TEXT("fog on keeps the playground"));
	SetFog(false);
	Check(!IsFogOn(), TEXT("fog off"));
	// Clear: nothing left, eight sides again.
	ClearNow();
	Check(Sim->State().units.empty() && Sim->State().structures.empty() && Sim->State().patches.empty() && Sim->State().players.size() == 8,
		TEXT("clear empties it"));
	UE_LOG(LogAutocraft, Log, TEXT("playgroundtest: done, %d failed"), Failures);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0, TEXT("AcPlaygroundTest"));
}
