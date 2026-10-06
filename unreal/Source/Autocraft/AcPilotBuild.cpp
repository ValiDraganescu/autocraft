#include "AcPilotBuild.h"

#include "AcHologram.h"
#include "AcLog.h"
#include "AcPilotOverlay.h"
#include "AcPilotPawn.h"
#include "AcPilotText.h"
#include "AcPose.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"
#include "AcTerrain.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Pilot.h"
#include "Rules.h"
#include "Simulation.h"

#include <cmath>
#include <string>

namespace
{
	FString Utf8(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

	FAutoConsoleCommandWithWorldAndArgs GPilotKey(TEXT("ac.PilotKey"),
		TEXT("ac.PilotKey KEY: a key to the driven unit's build menu (b, 1-8, esc, place)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UAcPilotBuildSubsystem* B = UAcPilotBuildSubsystem::Get(World);
			if (!B || Args.Num() == 0) return;
			const bool bUsed = B->Key(Args[0].ToLower());
			UE_LOG(LogAutocraft, Log, TEXT("pilot build: key %s %s"), *Args[0], bUsed ? TEXT("used") : TEXT("not used"));
		}));
}

UAcPilotBuildSubsystem* UAcPilotBuildSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UAcPilotBuildSubsystem>() : nullptr;
}

bool UAcPilotBuildSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UAcPilotBuildSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	const TCHAR* Cmd = FCommandLine::Get();
	FString Mode;
	if (FParse::Value(Cmd, TEXT("AcPilotBuild="), Mode, false))
	{
		FString Kind;
		if (!Mode.Split(TEXT(","), &StageMode, &Kind)) StageMode = Mode;
		StageMode = StageMode.ToLower();
		if (!Kind.IsEmpty())
		{
			const std::optional<ac::StructureKind> K = ac::parse<ac::StructureKind>(TCHAR_TO_UTF8(*Kind));
			for (int32 I = 0; K && I < (int32)ac::PilotBuild::kinds.size(); ++I)
			{
				if (ac::PilotBuild::kinds[I] == *K) { StageNumber = I + 1; bStageKeys = true; }
			}
		}
	}
	FParse::Value(Cmd, TEXT("AcPilotBuildPlace="), PlaceAfter);
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this))
	{
		FrameHandle = Sim->AddFrameListener(EAcFrameStage::Effects,
			FAcFrameEvent::FDelegate::CreateUObject(this, &UAcPilotBuildSubsystem::OnFrame));
	}
}

void UAcPilotBuildSubsystem::Deinitialize()
{
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this)) Sim->RemoveFrameListener(EAcFrameStage::Effects, FrameHandle);
	Super::Deinitialize();
}

void UAcPilotBuildSubsystem::Hook(AAcPilotPawn* Pawn)
{
	if (!Pawn || Hooked.Get() == Pawn) return;
	Hooked = Pawn;
	// In front of whatever was there (E9's picks): it still gets what this
	// does not use.
	TFunction<bool(const FString&)> Before = MoveTemp(Pawn->OnKey);
	TWeakObjectPtr<UAcPilotBuildSubsystem> Self(this);
	Pawn->OnKey = [Self, Before](const FString& K) -> bool
	{
		if (UAcPilotBuildSubsystem* B = Self.Get(); B && B->Key(K)) return true;
		return Before ? Before(K) : false;
	};
}

void UAcPilotBuildSubsystem::Note(const FString& Text)
{
	if (UAcPilotOverlaySubsystem* O = UAcPilotOverlaySubsystem::Get(this)) O->Note(Text);
}

void UAcPilotBuildSubsystem::Close()
{
	bMenu = false;
	Held.Reset();
	if (AAcHologram* H = AAcHologram::Find(GetWorld())) H->Hide();
	if (AAcPilotPawn* Pawn = Hooked.Get()) Pawn->bPlacing = false;
}

TOptional<ac::Vec2> UAcPilotBuildSubsystem::Spot(const ac::StructureKind Kind) const
{
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || !Sim->IsRunning()) return {};
	const ac::Simulation& S = Sim->Simulation();
	const std::optional<ac::Unit> U = S.pilotUnit();
	if (!U) return {};
	const ac::Vec2 Ahead(std::cos(U->heading), std::sin(U->heading));
	const double Reach = Kind == ac::StructureKind::derrick ? 3.0 : ac::Rules::radius(Kind) + 2.2;
	const std::optional<ac::Vec2> P = S.snap(Kind, U->position + Ahead * Reach);
	return P ? TOptional<ac::Vec2>(*P) : TOptional<ac::Vec2>();
}

bool UAcPilotBuildSubsystem::Key(const FString& K)
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || !Sim->IsRunning()) return false;
	ac::Simulation& S = Sim->Simulation();
	const std::optional<ac::Unit> U = S.pilotUnit();
	if (!U || !ac::PilotBuild::builds(U->kind)) return false;
	if (K == TEXT("place"))
	{
		// `pilotPress` with a building held out.
		if (!Held) return false;
		Place();
		return true;
	}
	if (K == TEXT("esc"))
	{
		if (!bMenu && !Held) return false;
		Close();
		return true;
	}
	if (K == TEXT("b"))
	{
		const bool bOpen = !bMenu;
		Close();
		bMenu = bOpen;
		return true;
	}
	if (!bMenu || !K.IsNumeric()) return false;
	const int32 N = FCString::Atoi(*K);
	if (N < 1 || N > (int32)ac::PilotBuild::kinds.size()) return false;
	const ac::StructureKind Kind = ac::PilotBuild::kinds[N - 1];
	if (const std::optional<std::string> Why = S.buildRefusal(Kind, ac::Pilot::player))
	{
		Note(Utf8(*Why));
		return true;
	}
	bMenu = false;
	Held = Kind;
	return true;
}

void UAcPilotBuildSubsystem::Place()
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Held || !Sim || !Sim->IsRunning()) return;
	const ac::StructureKind Kind = *Held;
	const TOptional<ac::Vec2> P = Spot(Kind);
	if (!P) { Note(TEXT("No well here")); return; }
	if (const std::optional<std::string> Why = Sim->Simulation().pilotBuild(Kind, *P))
	{
		Note(Utf8(*Why));
		return;
	}
	UE_LOG(LogAutocraft, Log, TEXT("pilot: building %s at %d,%d"), AcPose::ModelBase(Kind), int32(P->x), int32(P->y));
	Close();
}

void UAcPilotBuildSubsystem::OnFrame(const FAcFrame& Frame)
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || !Sim->IsRunning()) return;
	AAcPilotPawn* Pawn = AAcPilotPawn::Find(this);
	Hook(Pawn);
	ac::Simulation& S = Sim->Simulation();
	const std::optional<ac::Unit> U = Pawn && Pawn->Driving() ? S.pilotUnit() : std::nullopt;
	if (!U || U->id != Pawn->Driven() || !ac::PilotBuild::builds(U->kind))
	{
		// `leavePilot` closes the build (and the drive ending with the unit).
		if (bMenu || Held) Close();
		if (Pawn) Pawn->bPlacing = false;
		return;
	}

	if (!StageMode.IsEmpty() && !bStaged) Stage(*Pawn);
	if (PauseAt >= 0 && Sim->GameTime() >= PauseAt)
	{
		Sim->SetPaused(true);
		PauseAt = -1;
	}
	if (PlaceAt >= 0 && Sim->GameTime() >= PlaceAt)
	{
		PlaceAt = -1;
		UE_LOG(LogAutocraft, Log, TEXT("pilot build: pressing act (placing %s)"), Held ? TEXT("yes") : TEXT("no"));
		Pawn->Press(true);
		Pawn->Press(false);
	}

	Pawn->bPlacing = Held.IsSet();
	// `pilotBuildInfo`: the spot, the ghost; E6 makes the prompt and the card.
	TOptional<ac::Vec2> At;
	if (Held) At = Spot(*Held);
	if (UAcPilotOverlaySubsystem* O = UAcPilotOverlaySubsystem::Get(this)) O->SetBuild(bMenu, Held, At);
	if (!Held || !At)
	{
		if (AAcHologram* H = AAcHologram::Find(GetWorld())) H->Hide();
		return;
	}
	std::optional<std::string> Why = S.buildRefusal(*Held, ac::Pilot::player);
	if (!Why) Why = S.placementRefusal(*Held, *At);
	const AAcTerrain* Terrain = AAcTerrain::Find(GetWorld());
	const double Y = Terrain ? Terrain->FieldHeight(*At) : 0.0;
	if (AAcHologram* H = AAcHologram::SpawnFor(GetWorld()))
	{
		H->Show(FName(FString::Printf(TEXT("%s_blue"), AcPose::ModelBase(*Held))), AcSpace::ToWorld(*At, Y), 0.0,
			ac::Rules::radius(*Held), !Why, Sim->GameTime());
	}
}

void UAcPilotBuildSubsystem::Stage(AAcPilotPawn& Pawn)
{
	// As `stagePilot` "build"/"menu" (windowshot --pilot build|menu).
	bStaged = true;
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	ac::Simulation& S = Sim->Simulation();
	ac::GameState& St = S.state;
	const ac::Structure* Citadel = nullptr;
	for (const ac::Structure& B : St.structures)
	{
		if (B.owner == ac::Pilot::player && B.kind == ac::StructureKind::citadel) { Citadel = &B; break; }
	}
	const ac::OreDeposit* Patch = nullptr;
	for (const ac::OreDeposit& P : St.patches)
	{
		if (Citadel && P.remaining > 0
			&& (!Patch || ac::distance(P.position, Citadel->position) < ac::distance(Patch->position, Citadel->position)))
		{
			Patch = &P;
		}
	}
	ac::Unit* Me = nullptr;
	for (ac::Unit& X : St.units)
	{
		if (X.id == Pawn.Driven()) Me = &X;
	}
	if (!Citadel || !Patch || !Me)
	{
		UE_LOG(LogAutocraft, Error, TEXT("pilot build: nothing to stage"));
		return;
	}
	const ac::Vec2 Home = Citadel->position;
	const ac::Vec2 Away = ac::normalize(Patch->position - Home);
	Me->position = Home - Away * (ac::Rules::radius(ac::StructureKind::citadel) + 3);
	Me->task = ac::Unit::Task::idle;
	const ac::Vec2 Face = StageMode == TEXT("blocked") ? Home : Home - Away * 20;
	const ac::Vec2 D = Face - Me->position;
	const double Yaw = std::atan2(D.y, D.x);
	Me->heading = Yaw;
	if (S.pilot) S.pilot->heading = Yaw;
	Pawn.Look(std::remainder(Yaw - Pawn.Yaw(), 2.0 * PI) / 0.0032, 0);
	St.players[ac::Pilot::player].ore = std::max<int64_t>(St.players[ac::Pilot::player].ore, 200);
	S.lookNow();
	// "menu": B. "build" with no KIND: the Garrison held out as Swift's stage
	// sets it (no refusal checked: "Needs a Hab Dome"). With a KIND: the keys
	// B and its number, as the player presses them.
	if (StageMode == TEXT("menu")) Key(TEXT("b"));
	else if (!bStageKeys) Held = ac::StructureKind::garrison;
	else { Key(TEXT("b")); Key(FString::FromInt(StageNumber)); }
	UE_LOG(LogAutocraft, Log, TEXT("pilot build: staged %s at %.2f,%.2f heading %.3f, menu %d, holding %s"), *StageMode,
		Me->position.x, Me->position.y, Yaw, bMenu ? 1 : 0, Held ? AcPose::ModelBase(*Held) : TEXT("-"));
	// Swift's stage steps 3 × 1/30 s.
	if (PlaceAfter >= 0) PlaceAt = Sim->GameTime() + PlaceAfter;
	else PauseAt = Sim->GameTime() + 3.0 / 30.0;
}
