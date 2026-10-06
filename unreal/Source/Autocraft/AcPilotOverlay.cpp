#include "AcPilotOverlay.h"

#include "AcConsole.h"
#include "AcHUD.h"
#include "AcLog.h"
#include "AcPilotPawn.h"
#include "AcSimSubsystem.h"
#include "SAcConsole.h"
#include "SAcPilotOverlay.h"
#include "SAcRoot.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Simulation.h"
#include "Types.h"

namespace
{
	/// `notePilot`: a note stays 1.8 s.
	constexpr double NoteSeconds = 1.8;
	/// Stills: marks shown this long after they land (the ticks at full, the
	/// damage just off the pip).
	constexpr double HeldMarkAge = 0.06;

	FAutoConsoleCommandWithWorldAndArgs GPilotMark(TEXT("ac.PilotMark"),
		TEXT("ac.PilotMark DAMAGE [kill]: a hit mark on the cockpit's reticle, as a landed shot makes."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UAcPilotOverlaySubsystem* O = UAcPilotOverlaySubsystem::Get(World))
			{
				O->Mark(Args.Num() > 0 ? FCString::Atod(*Args[0]) : 6.0, Args.Contains(TEXT("kill")));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GPilotNote(TEXT("ac.PilotNote"), TEXT("ac.PilotNote TEXT: a note over the cockpit's reticle."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UAcPilotOverlaySubsystem* O = UAcPilotOverlaySubsystem::Get(World)) O->Note(FString::Join(Args, TEXT(" ")));
		}));
}

UAcPilotOverlaySubsystem* UAcPilotOverlaySubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UAcPilotOverlaySubsystem>() : nullptr;
}

bool UAcPilotOverlaySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UAcPilotOverlaySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	const TCHAR* Cmd = FCommandLine::Get();
	FString Mark;
	if (FParse::Value(Cmd, TEXT("AcPilotMark="), Mark))
	{
		FAcHitMark M;
		M.Serial = 1;
		M.bKill = Mark.Equals(TEXT("kill"), ESearchCase::IgnoreCase);
		M.Damage = M.bKill ? 0 : FCString::Atod(*Mark);
		HeldMark = M;
	}
	bHeldHurt = FParse::Param(Cmd, TEXT("AcPilotHurt"));
	FParse::Value(Cmd, TEXT("AcPilotNote="), HeldNote);
	FParse::Value(Cmd, TEXT("AcPilotRun="), RunFor);
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this))
	{
		FrameHandle = Sim->AddFrameListener(EAcFrameStage::Hud, FAcFrameEvent::FDelegate::CreateUObject(this, &UAcPilotOverlaySubsystem::OnFrame));
	}
}

void UAcPilotOverlaySubsystem::Deinitialize()
{
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this)) Sim->RemoveFrameListener(EAcFrameStage::Hud, FrameHandle);
	if (AAcPilotPawn* Pawn = NotePawn.Get()) Pawn->OnNote.Remove(NoteHandle);
	Detach();
	Super::Deinitialize();
}

void UAcPilotOverlaySubsystem::Note(const FString& Text)
{
	NoteText = Text;
	NoteUntil = FPlatformTime::Seconds() + NoteSeconds;
}

void UAcPilotOverlaySubsystem::SetBuild(const bool bMenu, const TOptional<ac::StructureKind> Placing, const TOptional<ac::Vec2> Spot)
{
	Inputs.bBuildMenu = bMenu;
	Inputs.Placing = Placing;
	Inputs.PlacingSpot = Spot;
}

void UAcPilotOverlaySubsystem::Mark(const double Damage, const bool bKill)
{
	FAcHitMark M;
	M.Serial = Inputs.Hit ? Inputs.Hit->Serial + 1 : 1;
	M.Damage = Damage;
	M.bKill = bKill;
	Inputs.Hit = M;
}

void UAcPilotOverlaySubsystem::Attach(const bool bPanel)
{
	AAcHUD* Hud = AAcHUD::Get(this);
	const TSharedPtr<SAcRoot> Root = Hud ? Hud->Root() : nullptr;
	if (!Root) return;
	if (!Widget)
	{
		Root->AddLayer(Layer)[SAssignNew(Widget, SAcPilotOverlay).Panel(bPanel)];
	}
	Widget->Reset();
	Widget->SetPanel(bPanel);
	Widget->Hold(HeldMark ? TOptional<double>(HeldMarkAge) : TOptional<double>(), bHeldHurt, !HeldNote.IsEmpty());
	Widget->SetVisibility(EVisibility::HitTestInvisible);
	Hud->SetDriving(true);
}

void UAcPilotOverlaySubsystem::Detach()
{
	if (Widget) Widget->SetVisibility(EVisibility::Collapsed);
	if (AAcHUD* Hud = AAcHUD::Get(this)) Hud->SetDriving(false);
	if (UAcConsoleSubsystem* Console = UAcConsoleSubsystem::Get(this))
	{
		Console->SetPilotInfo({});
		Console->SetLook(EAcConsoleStyle::Rts, EAcCabLamps::Idle, true);
	}
}

void UAcPilotOverlaySubsystem::OnFrame(const FAcFrame& Frame)
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	AAcPilotPawn* Pawn = AAcPilotPawn::Find(this);
	if (!Sim || !Sim->IsRunning() || !Frame.State) return;
	if (Pawn && NotePawn.Get() != Pawn)
	{
		if (AAcPilotPawn* Old = NotePawn.Get()) Old->OnNote.Remove(NoteHandle);
		NoteHandle = Pawn->OnNote.AddUObject(this, &UAcPilotOverlaySubsystem::Note);
		NotePawn = Pawn;
	}
	const ac::Simulation& S = Sim->Simulation();
	const TOptional<FAcPilotFrame> F = Pawn && Pawn->Driving() ? FAcPilotFrame::Of(S) : TOptional<FAcPilotFrame>();
	// -AcPilotPullOut: the camera has left the unit; the HUD goes with it.
	if (!F || F->Unit.id != Pawn->Driven() || Pawn->PulledOut())
	{
		if (DrivenId != INDEX_NONE)
		{
			DrivenId = INDEX_NONE;
			Shown.Reset();
			Inputs = FAcPilotInputs();
			NoteText.Reset();
			Aim.Reset();
			Detach();
		}
		return;
	}
	const ac::Unit& U = F->Unit;
	if (RunUntil >= 0)
	{
		// -AcPilotRun: the staged still runs on a moment, then holds again.
		// The act key held meanwhile (`stagePilot("mining")`).
		const bool bDone = Sim->GameTime() >= RunUntil;
		Sim->SetPaused(bDone);
		Pawn->Press(!bDone);
		if (bDone) RunUntil = -2;
	}
	const UAcConsoleSubsystem* ConsoleSub = UAcConsoleSubsystem::Get(this);
	const bool bConsole = ConsoleSub && ConsoleSub->Console().IsValid();
	if (DrivenId != U.id)
	{
		// A new ride (`takeOver`: `pilotHit = nil`).
		DrivenId = U.id;
		Inputs.Hit.Reset();
		Aim.Reset();
		Attach(!bConsole);
		if (HeldMark) Inputs.Hit = HeldMark;
		if (RunFor > 0 && RunUntil < 0) RunUntil = Sim->GameTime() + RunFor;
		UE_LOG(LogAutocraft, Log, TEXT("pilot overlay: %s #%lld, %s"), *AcPilotText::Title(U.kind), (long long)U.id,
			bConsole ? TEXT("on the console") : TEXT("with its own panel"));
	}

	// `markPilotHit`: the driven unit's landed hits, with the damage the
	// sight's target was worth before the step. A staged still marks it at
	// the shot instead, as Swift's stage does (`stageUnit` calls
	// `markPilotHit` with the shot): a shell or a flame lands after the still.
	const bool bAtShot = Pawn->Staged();
	for (const ac::GameEvent& E : Frame.AllEvents)
	{
		int64 Target = INDEX_NONE;
		if (bAtShot)
		{
			const ac::GameEvent::Shot* Sh = E.as<ac::GameEvent::Shot>();
			if (Sh && Sh->unit == U.id) Target = Sh->target;
		}
		else if (const ac::GameEvent::Landed* L = E.as<ac::GameEvent::Landed>(); L && L->unit == U.id)
		{
			Target = L->target;
		}
		if (Target == INDEX_NONE) continue;
		bool bAlive = false;
		if (const std::optional<ac::Unit> V = S.state.unit(Target)) bAlive = V->hp > 0;
		else if (const std::optional<ac::Structure> B = S.state.structure(Target)) bAlive = B->hp > 0;
		double Damage = 0;
		if (Aim && Aim->Key == Target) Damage = Aim->Value;
		else if (const std::optional<double> D = S.strikeDamage(U, Target)) Damage = *D;
		Mark(Damage, !bAlive);
		// The still is taken a moment later: the mark held fresh, as drawn
		// in Swift's still (made at the shot, drawn the same frame).
		if (bAtShot && Widget) Widget->Hold(TOptional<double>(HeldMarkAge), bHeldHurt, !HeldNote.IsEmpty());
	}
	Aim.Reset();
	if (S.pilotShoots(U) && F->Sight)
	{
		Aim = TPair<int64, double>(F->Sight->target, S.strikeDamage(U, F->Sight->target).value_or(0));
	}

	const double Now = FPlatformTime::Seconds();
	if (!HeldNote.IsEmpty()) Inputs.Note = HeldNote;
	else if (NoteText && NoteUntil > Now) Inputs.Note = NoteText;
	else
	{
		NoteText.Reset();
		Inputs.Note.Reset();
	}
	Inputs.bThirdPerson = Pawn->Eye().bThirdPerson;
	FAcPilotInfo Info = AcPilotText::Info(S, *F, Inputs);
	if (GunMarker) Info.Gun = GunMarker();
	else Info.Gun = Pawn->GunMarker(); // E4's marker, computed by the pawn each frame
	if (Widget) Widget->Show(Info, Now);

	if (UAcConsoleSubsystem* Console = UAcConsoleSubsystem::Get(this))
	{
		// `HUD.applyPilot`: the cab's frame in first person in a cab, the
		// frame round the view only from behind.
		Console->SetLook(Info.bCab ? EAcConsoleStyle::Cab : EAcConsoleStyle::Cockpit, Info.Lamps, Info.bThirdPerson);
		Console->SetPilotInfo(Info.Console);
	}
	Shown = MoveTemp(Info);
}
