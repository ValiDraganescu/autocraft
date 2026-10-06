#include "AcPointer.h"

#include "AcConsole.h"
#include "AcHUD.h"
#include "AcHudStyle.h"
#include "AcLifeBars.h"
#include "AcLog.h"
#include "AcPose.h"
#include "AcRtsPawn.h"
#include "AcSimSubsystem.h"
#include "AcTerrain.h"
#include "AcWorldRenderer.h"
#include "SAcConsole.h"
#include "SAcRoot.h"
#include "SAcTip.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

#include "Pilot.h"
#include "Rules.h"
#include "Simulation.h"

namespace
{
	FString Title(const std::string& S) { return UTF8_TO_TCHAR(S.c_str()); }
	/// With more than two players, whose another player's thing is (" · Teal").
	FString Whose(const ac::GameState& S, const int64 Me, const int64 Owner)
	{
		if (Owner == Me || S.players.size() <= 2) return FString();
		return TEXT(" \u00B7 ") + FAcHudStyle::PlayerName(Owner);
	}

	const TCHAR* CursorName(const EAcCursor K)
	{
		switch (K)
		{
		case EAcCursor::Select: return TEXT("select");
		case EAcCursor::Enemy: return TEXT("enemy");
		case EAcCursor::Drive: return TEXT("drive");
		default: return TEXT("normal");
		}
	}

	EMouseCursor::Type CursorShape(const EAcCursor K)
	{
		switch (K)
		{
		case EAcCursor::Select: return EMouseCursor::Hand;
		case EAcCursor::Enemy: return EMouseCursor::Crosshairs;
		case EAcCursor::Drive: return EMouseCursor::EyeDropper;
		default: return EMouseCursor::Default;
		}
	}

	FString Describe(const TOptional<FAcHover>& H)
	{
		if (!H) return TEXT("nothing");
		if (H->IsUnit()) return FString::Printf(TEXT("unit(%lld, drivable: %s)"), (long long)H->Id, H->bDrivable ? TEXT("true") : TEXT("false"));
		return FString::Printf(TEXT("building(%lld)"), (long long)H->Id);
	}

	bool ParsePoint(const FString& Text, FVector2D& Out)
	{
		FString X, Y;
		if (!Text.Split(TEXT(","), &X, &Y)) return false;
		Out = FVector2D(FCString::Atod(*X), FCString::Atod(*Y));
		return true;
	}

	/// Slate widgets that are the world itself, or plain containers over it
	/// (the game viewport's own layers, the HUD root): not a HUD area.
	bool IsWorldWidget(const SWidget& W)
	{
		static const TSet<FName> World = {
			TEXT("SViewport"), TEXT("SGameLayerManager"), TEXT("SPlayerLayer"), TEXT("SOverlay"), TEXT("SConstraintCanvas"),
			TEXT("SCanvas"), TEXT("SDPIScaler"), TEXT("SBox"), TEXT("SVerticalBox"), TEXT("SHorizontalBox"), TEXT("SWindow"),
			TEXT("SAcRoot"), TEXT("SAcTip"), TEXT("SAcPopups"), TEXT("SVirtualWindow"), TEXT("SBorder")};
		return World.Contains(W.GetType());
	}

	UAcPointer* PointerOf(UWorld* World) { return UAcPointer::Get(World); }

	FAutoConsoleCommandWithWorldAndArgs CmdPointAt(TEXT("ac.PointAt"),
		TEXT("Hold the pointer at a view point: ac.PointAt X Y (points, y down); ac.PointAt off gives it back to the mouse."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) {
			UAcPointer* P = PointerOf(World);
			if (!P) return;
			if (Args.Num() >= 2) P->SetSimulatedPoint(FVector2D(FCString::Atod(*Args[0]), FCString::Atod(*Args[1])));
			else P->SetSimulatedPoint({});
		}));
	FAutoConsoleCommandWithWorldAndArgs CmdClick(TEXT("ac.Click"),
		TEXT("Click at a view point (ac.Click X Y) or where the pointer is (ac.Click), through the click routing."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) {
			UAcPointer* P = PointerOf(World);
			if (!P) return;
			if (Args.Num() >= 2) P->Click(FVector2D(FCString::Atod(*Args[0]), FCString::Atod(*Args[1])));
			else if (APlayerController* PC = World->GetFirstPlayerController())
			{
				if (const AAcRtsPawn* Pawn = Cast<AAcRtsPawn>(PC->GetPawn()))
				{
					FVector2D C;
					if (Pawn->CursorPoint(C)) P->Click(C);
				}
			}
		}));
	FAutoConsoleCommandWithWorldAndArgs CmdHover(TEXT("ac.Hover"),
		TEXT("Rest the pointer on the first prospector, citadel, enemy:ranger... and follow it (ac.Hover off: stop)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) {
			if (UAcPointer* P = PointerOf(World)) P->HoverOn(Args.Num() > 0 && Args[0] != TEXT("off") ? Args[0] : FString());
		}));
	FAutoConsoleCommandWithWorldAndArgs CmdSelect(TEXT("ac.Select"),
		TEXT("Select the player's first building of a kind (ac.Select citadel; ac.Select none)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) {
			UAcPointer* P = PointerOf(World);
			if (!P) return;
			P->Select(Args.Num() > 0 ? P->FirstOwnBuilding(Args[0]) : TOptional<int64>());
		}));
}

UAcPointer::UAcPointer()
{
	PrimaryComponentTick.bCanEverTick = true;
}

UAcPointer* UAcPointer::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC ? PC->FindComponentByClass<UAcPointer>() : nullptr;
}

void UAcPointer::BeginPlay()
{
	Super::BeginPlay();
	const TWeakObjectPtr<UAcPointer> Self(this);
	PickShapes.UnitY = [Self](const ac::Unit& U) { return Self.IsValid() ? Self->UnitY(U) : 0.0; };
	PickShapes.GroundY = [Self](ac::Vec2 P) { return Self.IsValid() ? Self->GroundY(P) : 0.0; };

	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this))
	{
		FrameHandle = Sim->AddFrameListener(EAcFrameStage::Hud, FAcFrameEvent::FDelegate::CreateUObject(this, &UAcPointer::OnFrame));
		StartedHandle = Sim->OnGameStarted.AddUObject(this, &UAcPointer::OnGameStarted);
	}
	Cues = AAcPointerCues::SpawnFor(GetWorld());
	InstallCursors();

	const TCHAR* Cmd = FCommandLine::Get();
	FString Text;
	FVector2D At;
	if (FParse::Value(Cmd, TEXT("AcPointAt="), Text, /*bShouldStopOnSeparator*/ false) && ParsePoint(Text, At)) Simulated = At;
}

void UAcPointer::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this))
	{
		Sim->RemoveFrameListener(EAcFrameStage::Hud, FrameHandle);
		Sim->OnGameStarted.Remove(StartedHandle);
	}
	if (Tip)
	{
		if (const AAcHUD* Hud = AAcHUD::Get(this); Hud && Hud->Root()) Hud->Root()->RemoveLayer(Tip.ToSharedRef());
		Tip.Reset();
	}
	Super::EndPlay(Reason);
}

void UAcPointer::OnGameStarted(UAcSimSubsystem&)
{
	Select({});
	Hover.Reset();
	FollowId.Reset();
}

// MARK: - Where the pointer is

void UAcPointer::SetSimulatedPoint(TOptional<FVector2D> ViewPoint)
{
	Simulated = ViewPoint;
	FollowName.Reset();
	FollowId.Reset();
}

bool UAcPointer::PointerPoint(FVector2D& Out) const
{
	if (Simulated)
	{
		Out = *Simulated;
		return true;
	}
	const AAcRtsPawn* Pawn = AAcRtsPawn::Get(this);
	return Pawn && Pawn->CursorPoint(Out);
}

bool UAcPointer::AbsolutePoint(const FVector2D ViewPoint, FVector2D& Out) const
{
	UGameViewportClient* Client = GEngine ? GEngine->GameViewport : nullptr;
	const TSharedPtr<SViewport> Widget = Client ? Client->GetGameViewportWidget() : nullptr;
	if (!Widget) return false;
	// View points start right of an inset (the playground's column).
	const AAcRtsPawn* Pawn = AAcRtsPawn::Get(this);
	Out = FVector2D(Widget->GetCachedGeometry().LocalToAbsolute(ViewPoint + FVector2D(Pawn ? Pawn->GetViewInset() : 0.0, 0)));
	return true;
}

bool UAcPointer::Covered(const FVector2D ViewPoint) const
{
	if (HudCovers && HudCovers(ViewPoint)) return true;
	FVector2D Abs;
	if (!FSlateApplication::IsInitialized() || !AbsolutePoint(ViewPoint, Abs)) return false;
	FSlateApplication& App = FSlateApplication::Get();
	const FWidgetPath Path = App.LocateWindowUnderMouse(Abs, App.GetInteractiveTopLevelWindows(), /*bIgnoreEnabledStatus*/ true);
	if (!Path.IsValid()) return false;
	// Like Slate's routing of a click: from the topmost widget down to the
	// game viewport, the first one that would take it covers the world.
	// The console (D2) fills the view and takes only clicks on its dashboard
	// (`SAcConsole::Covers`); containers pass them on.
	for (int32 I = Path.Widgets.Num() - 1; I >= 0; --I)
	{
		const FArrangedWidget& A = Path.Widgets[I];
		const SWidget& W = A.Widget.Get();
		if (W.GetType() == TEXT("SViewport")) return false;
		if (IsWorldWidget(W)) continue;
		if (W.GetType() == TEXT("SAcConsole"))
		{
			if (static_cast<const SAcConsole&>(W).Covers(FVector2D(A.Geometry.AbsoluteToLocal(Abs)))) return true;
			continue;
		}
		if (Simulated)
		{
			static FName LastType;
			if (W.GetType() != LastType)
			{
				UE_LOG(LogAutocraft, Log, TEXT("pointer: %d,%d is on the HUD (%s)"), FMath::RoundToInt(ViewPoint.X), FMath::RoundToInt(ViewPoint.Y),
					*W.GetTypeAsString());
			}
			LastType = W.GetType();
		}
		return true;
	}
	return false;
}

// MARK: - Heights (`GameScene.unitY`, `groundY`)

double UAcPointer::GroundY(const ac::Vec2 P) const
{
	const AAcTerrain* Terrain = AAcTerrain::Find(GetWorld());
	return Terrain ? Terrain->FieldHeight(P) : 0.0;
}

double UAcPointer::UnitY(const ac::Unit& U) const
{
	if (!U.stats().air) return GroundY(U.position);
	double Base = GroundY(U.position);
	if (UAcWorldRenderer* Renderer = UAcWorldRenderer::Get(this))
	{
		if (const FAcUnitMemory* M = Renderer->Memory(U.id); M && M->AirY) Base = *M->AirY;
	}
	return Base + AcPose::Hover(U.kind);
}

// MARK: - Picking and hover

TOptional<FAcHover> UAcPointer::PickAt(const FVector2D ViewPoint) const
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	const AAcRtsPawn* Pawn = AAcRtsPawn::Get(this);
	const ac::FreeView* View = Pawn ? Pawn->View() : nullptr;
	if (!Sim || !Sim->IsRunning() || !View) return {};
	const ac::FreeView::Ray Ray = View->ray(ac::Vec2(ViewPoint.X, ViewPoint.Y));
	const std::optional<FAcHover> H = AcPick::Pick(Ray, Sim->Shown(), Sim->LocalPlayer(), PickShapes);
	return H ? TOptional<FAcHover>(*H) : TOptional<FAcHover>();
}

bool UAcPointer::Piloting() const
{
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	return Sim && Sim->IsRunning() && Sim->Simulation().pilot.has_value();
}

void UAcPointer::OnFrame(const FAcFrame& Frame)
{
	++Frames;
	const double Time = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	if (!bStaged && Frames >= 3 && AAcRtsPawn::Get(this) && AAcRtsPawn::Get(this)->View()) StageCommandLine();
	UpdateSelection(Time);
	UpdateHover(Time);
	// Bars show for what is selected or under the pointer, even at full
	// health (`GameController.swift:416`, `barFocus`).
	if (AAcLifeBars* Bars = AAcLifeBars::Get(this))
	{
		TSet<int64> Focus;
		if (Selection) Focus.Add(*Selection);
		if (Hover) Focus.Add(Hover->Id);
		Bars->SetFocus(Focus);
	}
}

bool UAcPointer::HoverOn(const FString& Name)
{
	FollowName = Name.ToLower();
	FollowId.Reset();
	if (FollowName.IsEmpty())
	{
		Simulated.Reset();
		return false;
	}
	const bool bFound = Follow();
	if (!bFound) UE_LOG(LogAutocraft, Warning, TEXT("pointer: nothing to hover: %s"), *Name);
	return bFound;
}

bool UAcPointer::Follow()
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	const AAcRtsPawn* Pawn = AAcRtsPawn::Get(this);
	const ac::FreeView* View = Pawn ? Pawn->View() : nullptr;
	if (!Sim || !Sim->IsRunning() || !View) return false;
	const ac::GameState& S = Sim->Shown();
	const int64 Me = Sim->LocalPlayer();
	const bool bEnemy = FollowName.StartsWith(TEXT("enemy:"));
	const bool bAlly = FollowName.StartsWith(TEXT("ally:"));
	const std::string Kind = TCHAR_TO_UTF8(*(bEnemy ? FollowName.Mid(6) : bAlly ? FollowName.Mid(5) : FollowName));
	auto Owned = [&](int64 O) { return bEnemy ? S.hostile(Me, O) : bAlly ? (O != Me && S.allied(Me, O)) : O == Me; };
	TOptional<ac::Vec3> Spot;
	// The one followed while it lives, else the first of its kind.
	for (int32 Pass = 0; Pass < 2 && !Spot; Pass++)
	{
		const TOptional<int64> Want = Pass == 0 ? FollowId : TOptional<int64>();
		if (Pass == 0 && !Want) continue;
		if (const std::optional<ac::UnitKind> K = ac::parse<ac::UnitKind>(Kind))
		{
			for (const ac::Unit& U : S.units)
			{
				if (U.kind != *K || !Owned(U.owner) || !AcPick::Pickable(U)) continue;
				if (Want && U.id != *Want) continue;
				FollowId = U.id;
				Spot = ac::Vec3(U.position.x, UnitY(U) + 0.5, U.position.y);
				break;
			}
		}
		else if (const std::optional<ac::StructureKind> B = ac::parse<ac::StructureKind>(Kind))
		{
			for (const ac::Structure& St : S.structures)
			{
				if (St.kind != *B || !Owned(St.owner)) continue;
				if (Want && St.id != *Want) continue;
				FollowId = St.id;
				Spot = ac::Vec3(St.position.x, GroundY(St.position) + ac::Rules::radius(St.kind), St.position.y);
				break;
			}
		}
	}
	if (!Spot) return false;
	const ac::Vec2 C = View->canvas(*Spot);
	Simulated = FVector2D(C.x, C.y);
	return true;
}

void UAcPointer::UpdateHover(const double Time)
{
	FVector2D C;
	// Follow it as it walks (the Swift shot points once; a live run moves).
	if (!FollowName.IsEmpty()) Follow();
	if (Piloting() || bBlocked || !PointerPoint(C) || (HoverSuppressed && HoverSuppressed()) || Covered(C))
	{
		ShowHover({}, FVector2D::ZeroVector, Time);
		ApplyCursor(EAcCursor::Normal);
		return;
	}
	const TOptional<FAcHover> H = PickAt(C);
	if (!H && Simulated && LastLog != TEXT("none"))
	{
		UE_LOG(LogAutocraft, Log, TEXT("pointer: nothing at %d,%d"), FMath::RoundToInt(C.X), FMath::RoundToInt(C.Y));
		LastLog = TEXT("none");
	}
	ShowHover(H, C, Time);
	ApplyCursor(CursorFor(H));
	const FString Line = FString::Printf(TEXT("hovered at %d,%d: %s, cursor %s"), FMath::RoundToInt(C.X), FMath::RoundToInt(C.Y), *Describe(H),
		CursorName(CursorKind));
	// Scripted pointing logs what it rests on (as `windowshot --hover`
	// prints), once per change.
	const FString What = Describe(H) + CursorName(CursorKind);
	if (Simulated && What != LastLog)
	{
		UE_LOG(LogAutocraft, Log, TEXT("pointer: %s"), *Line);
		LastLog = What;
	}
}

void UAcPointer::ShowHover(const TOptional<FAcHover>& H, const FVector2D At, const double Time)
{
	Hover = H;
	AAcPointerCues* C = Cues.Get();
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	EnsureTip();
	if (!H || !Sim || !Sim->IsRunning())
	{
		if (C) C->ShowHover({}, Time);
		if (Tip) Tip->Show({});
		return;
	}
	const ac::GameState& S = Sim->Shown();
	const int64 Me = Sim->LocalPlayer();
	FAcHoverCue Cue;
	FAcHoverTip T;
	FVector2D Abs;
	if (AbsolutePoint(At, Abs)) T.AbsoluteAt = FVector2f(Abs);
	if (H->IsUnit())
	{
		const ac::Unit* U = nullptr;
		for (const ac::Unit& X : S.units)
			if (X.id == H->Id) U = &X;
		if (!U)
		{
			ShowHover({}, At, Time);
			return;
		}
		const bool bMine = U->owner == Me;
		const bool bAlly = !bMine && S.allied(Me, U->owner);
		Cue.Tone = H->bDrivable ? EAcHoverTone::Drive : (bMine || bAlly) ? EAcHoverTone::Own : EAcHoverTone::Enemy;
		// The models stand wider than their collision radius.
		Cue.Position = U->position;
		Cue.Y = UnitY(*U);
		Cue.Radius = ac::Rules::radius(U->kind) * 1.5 + 0.4;
		Cue.bUnit = true;
		T.Title = (bMine ? TEXT("") : bAlly ? TEXT("Allied ") : TEXT("Enemy ")) + Title(ac::Simulation::title(U->kind)) + Whose(S, Me, U->owner);
		if (H->bDrivable)
		{
			T.Detail = FString::Printf(TEXT("Click to take the controls (%s person, V)"), bThirdPerson ? TEXT("third") : TEXT("first"));
		}
		T.Hp = FMath::Max(U->hp, 0.0);
		T.MaxHp = Sim->Simulation().maxHP(*U);
	}
	else
	{
		const std::optional<ac::Structure> St = S.structure(H->Id);
		if (!St)
		{
			ShowHover({}, At, Time);
			return;
		}
		const bool bMine = St->owner == Me;
		const bool bAlly = !bMine && S.allied(Me, St->owner);
		Cue.Tone = (bMine || bAlly) ? EAcHoverTone::Own : EAcHoverTone::Enemy;
		Cue.Position = St->position;
		Cue.Y = GroundY(St->position);
		Cue.Radius = ac::Rules::radius(St->kind) * 1.42 + 0.4;
		Cue.bUnit = false;
		T.Title = (bMine ? TEXT("") : bAlly ? TEXT("Allied ") : TEXT("Enemy ")) + Title(ac::Simulation::title(St->kind)) + Whose(S, Me, St->owner);
		if (bMine && !(Selection && *Selection == St->id)) T.Detail = TEXT("Click to select");
		T.Hp = FMath::Max(St->hp, 0.0);
		T.MaxHp = ac::Rules::hp(St->kind);
	}
	T.Tone = Cue.Tone;
	if (C) C->ShowHover(Cue, Time);
	if (Tip) Tip->Show(T);
}

EAcCursor UAcPointer::CursorFor(const TOptional<FAcHover>& H) const
{
	if (!H) return EAcCursor::Normal;
	if (H->IsUnit() && H->bDrivable) return EAcCursor::Drive;
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || !Sim->IsRunning()) return EAcCursor::Normal;
	const ac::GameState& S = Sim->Shown();
	const int64 Me = Sim->LocalPlayer();
	TOptional<int64> Owner;
	if (H->IsUnit())
	{
		for (const ac::Unit& U : S.units)
			if (U.id == H->Id) Owner = U.owner;
	}
	else if (const std::optional<ac::Structure> St = S.structure(H->Id))
	{
		Owner = St->owner;
	}
	if (!Owner) return EAcCursor::Normal;
	if (*Owner == Me) return EAcCursor::Select;
	// An ally's: nothing to click, but no enemy either.
	return S.hostile(Me, *Owner) ? EAcCursor::Enemy : EAcCursor::Normal;
}

void UAcPointer::ApplyCursor(const EAcCursor Kind)
{
	CursorKind = Kind;
	if (APlayerController* PC = Cast<APlayerController>(GetOwner())) PC->CurrentMouseCursor = CursorShape(Kind);
}

void UAcPointer::InstallCursors()
{
	UGameViewportClient* Client = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (!Client || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetPlatformCursor()) return;
	int32 Installed = 0;
	for (const EAcCursor K : {EAcCursor::Normal, EAcCursor::Select, EAcCursor::Enemy, EAcCursor::Drive})
	{
		const FString Rel = FString::Printf(TEXT("UI/Cursors/Cursor_%s"), CursorName(K));
		if (!FPaths::FileExists(FPaths::ProjectContentDir() / Rel + TEXT(".tiff"))) continue;
		// Hot spot (3, 2) of 32 points; the Mac cursor rounds `h·(size − 1)`.
		if (Client->SetHardwareCursor(CursorShape(K), FName(*Rel), FVector2D(3.0 / 31.0, 2.0 / 31.0))) ++Installed;
	}
	if (Installed < 4)
	{
		UE_LOG(LogAutocraft, Warning, TEXT("pointer: %d of 4 cursors installed (run sh Tools/cursors/bake_cursors.sh)"), Installed);
	}
}

void UAcPointer::EnsureTip()
{
	if (Tip) return;
	const AAcHUD* Hud = AAcHUD::Get(this);
	const TSharedPtr<SAcRoot> Root = Hud ? Hud->Root() : nullptr;
	if (!Root) return;
	Root->AddLayer(AcHudLayer::Tip)
	[
		SAssignNew(Tip, SAcTip)
	];
}

// MARK: - Selection

void UAcPointer::Select(const TOptional<int64> Id)
{
	const bool bChanged = Id.IsSet() != Selection.IsSet() || (Id && *Id != *Selection);
	Selection = Id;
	UpdateSelection(GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0);
	if (bChanged)
	{
		UE_LOG(LogAutocraft, Log, TEXT("pointer: selected %s"), Selection ? *FString::Printf(TEXT("#%lld"), (long long)*Selection) : TEXT("nothing"));
		// The console (D2) shows the selected building's panel and card.
		if (UAcConsoleSubsystem* Console = UAcConsoleSubsystem::Get(this)) Console->Select(Selection);
		OnSelectionChanged.Broadcast();
	}
}

void UAcPointer::UpdateSelection(const double Time)
{
	AAcPointerCues* C = Cues.Get();
	if (!Selection)
	{
		if (C) C->ShowSelection({}, 0, 0, Time);
		return;
	}
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	const std::optional<ac::Structure> St = Sim && Sim->IsRunning() ? Sim->State().structure(*Selection) : std::nullopt;
	// A building that is gone, or changed hands, or a drive started: let go.
	if (!St || St->owner != Sim->LocalPlayer() || Piloting())
	{
		Select({});
		return;
	}
	if (C) C->ShowSelection(St->position, GroundY(St->position), ac::Rules::radius(St->kind), Time);
}

TOptional<int64> UAcPointer::FirstOwnBuilding(const FString& Kind) const
{
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	const std::optional<ac::StructureKind> K = ac::parse<ac::StructureKind>(std::string(TCHAR_TO_UTF8(*Kind.ToLower())));
	if (!Sim || !Sim->IsRunning() || !K) return {};
	for (const ac::Structure& S : Sim->State().structures)
		if (S.owner == Sim->LocalPlayer() && S.kind == *K) return S.id;
	return {};
}

TOptional<FVector2D> UAcPointer::RoofPoint(const int64 Id) const
{
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	const AAcRtsPawn* Pawn = AAcRtsPawn::Get(this);
	const ac::FreeView* View = Pawn ? Pawn->View() : nullptr;
	const std::optional<ac::Structure> S = Sim && Sim->IsRunning() ? Sim->State().structure(Id) : std::nullopt;
	if (!S || !View) return {};
	const ac::Vec2 C = View->canvas(ac::Vec3(S->position.x, GroundY(S->position) + ac::Rules::radius(S->kind) * 1.2, S->position.y));
	return FVector2D(C.x, C.y);
}

// MARK: - Clicks

void UAcPointer::Click(const FVector2D ViewPoint)
{
	if (bBlocked) return;
	for (const TFunction<bool(FVector2D)>& Handler : ClickHandlers)
	{
		if (Handler && Handler(ViewPoint)) return;
	}
	if (Covered(ViewPoint)) return;
	const TOptional<FAcHover> H = PickAt(ViewPoint);
	UE_LOG(LogAutocraft, Log, TEXT("pointer: click at %d,%d on %s"), FMath::RoundToInt(ViewPoint.X), FMath::RoundToInt(ViewPoint.Y), *Describe(H));
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (H && H->IsUnit() && H->bDrivable)
	{
		if (OnTakeOver.IsBound()) OnTakeOver.Execute(H->Id);
		else UE_LOG(LogAutocraft, Log, TEXT("pointer: drive #%lld (no pilot pawn yet: E2)"), (long long)H->Id);
		return;
	}
	if (H && !H->IsUnit() && Sim)
	{
		const std::optional<ac::Structure> St = Sim->State().structure(H->Id);
		if (St && St->owner == Sim->LocalPlayer())
		{
			Select(H->Id);
			return;
		}
	}
	// Swift also moves the listener here, but only off the window game
	// (`placeListener` returns at once when the free camera is on).
	Select({});
}

void UAcPointer::DriveAt(const FVector2D ViewPoint)
{
	const TOptional<FAcHover> H = PickAt(ViewPoint);
	if (!H || !H->IsUnit() || !H->bDrivable) return;
	if (OnTakeOver.IsBound()) OnTakeOver.Execute(H->Id);
	else UE_LOG(LogAutocraft, Log, TEXT("pointer: drive #%lld (no pilot pawn yet: E2)"), (long long)H->Id);
}

void UAcPointer::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	PollInput();
}

void UAcPointer::PollInput()
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC || Piloting())
	{
		bPressed = bDragged = false;
		return;
	}
	FVector2D C;
	if (PC->WasInputKeyJustPressed(EKeys::LeftMouseButton) && PointerPoint(C))
	{
		bPressed = true;
		bDragged = false;
		PressAt = C;
	}
	if (bPressed && !bDragged && PointerPoint(C) && FVector2D::Distance(C, PressAt) >= AAcRtsPawn::DragThreshold) bDragged = true;
	if (bPressed && PC->WasInputKeyJustReleased(EKeys::LeftMouseButton))
	{
		bPressed = false;
		if (!bDragged && PointerPoint(C)) Click(C);
	}
	// F over the view: drive what is under the pointer (`drive(at:)`).
	const FModifierKeysState Mods = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetModifierKeys() : FModifierKeysState();
	if (!bBlocked && PC->WasInputKeyJustPressed(EKeys::F) && !Mods.IsCommandDown() && !Mods.IsControlDown() && PointerPoint(C) && !Covered(C))
	{
		DriveAt(C);
	}
}

// MARK: - Scripted pointing

void UAcPointer::StageCommandLine()
{
	bStaged = true;
	const TCHAR* Cmd = FCommandLine::Get();
	FString Value;
	if (FParse::Value(Cmd, TEXT("AcSelect="), Value))
	{
		const TOptional<int64> Id = FirstOwnBuilding(Value);
		if (!Id) UE_LOG(LogAutocraft, Warning, TEXT("pointer: no %s to select"), *Value);
		Select(Id);
	}
	if (FParse::Value(Cmd, TEXT("AcClick="), Value))
	{
		const TOptional<int64> Id = FirstOwnBuilding(Value);
		const TOptional<FVector2D> At = Id ? RoofPoint(*Id) : TOptional<FVector2D>();
		if (At)
		{
			// A click on its roof, through the same path as the mouse.
			Click(*At);
			UE_LOG(LogAutocraft, Log, TEXT("pointer: clicked %s roof at %d,%d: selected %s"), *Value, FMath::RoundToInt(At->X),
				FMath::RoundToInt(At->Y), Selection ? *FString::Printf(TEXT("#%lld"), (long long)*Selection) : TEXT("nothing"));
		}
		else
		{
			UE_LOG(LogAutocraft, Warning, TEXT("pointer: no %s to click"), *Value);
		}
	}
	if (FParse::Value(Cmd, TEXT("AcHover="), Value)) HoverOn(Value);
}
