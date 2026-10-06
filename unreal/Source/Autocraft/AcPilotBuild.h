// `UAcPilotBuildSubsystem` (chunk E8): a driven Prospector's build menu and
// the building it holds out, the port of `GameController+Command.swift`
// "Building by hand" (`pilotPress`, `closeBuild`, `key`, `ghostSpot`,
// `pilotBuildInfo`) and the ghost of `GameScene+Pointers.swift`.
//
// Keys come through E2's `AAcPilotPawn::OnKey` (installed in front of
// whatever handler was there, which still gets the keys this one does not
// use): B opens and closes the menu, 1-8 pick a building from it
// (`PilotBuild.kinds`; refused with a note when it cannot be afforded), Esc
// closes the menu or drops the building (else the pawn leaves), "place"
// (the act key while `bPlacing`) pays and puts it down
// (`sim.pilotBuild`), or notes why not.
//
// Each frame (`Effects` stage, before E6's overlay at `Hud`): while a unit
// that builds is driven, `bPlacing` on the pawn, `ghostSpot` (radius + 2.2
// ahead, a Derrick 3, snapped by `sim.snap`), E6's
// `UAcPilotOverlaySubsystem::SetBuild` (the console's build card, the
// prompt and its tone) and the hologram (`AAcHologram`): green where it
// fits, red with the reason. Leaving the drive closes it all.
//
// Dev (headless; no desktop input):
//   -AcPilotBuild=menu|build|blocked[,KIND]  with -AcPilot=prospector:
//       staged as `windowshot --pilot build|menu`: the Prospector on its
//       Citadel's open side facing out, ore at least 200; menu: the key B;
//       build: the Garrison held out as Swift's stage does (refused: "Needs
//       a Hab Dome"), or with KIND the keys B and KIND's number (habDome: 1);
//       blocked: as build, facing the Citadel; the game pauses 0.1 s later
//   -AcPilotBuildPlace=S  instead of pausing, press the act key S game
//       seconds after the staging (places it, the scaffold rises)
//   ac.PilotKey KEY       a key as the pawn would offer it (b, 1-8, esc, place)
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "Types.h"

#include <optional>

#include "AcPilotBuild.generated.h"

class AAcPilotPawn;
struct FAcFrame;

UCLASS()
class AUTOCRAFT_API UAcPilotBuildSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UAcPilotBuildSubsystem* Get(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/// A key while driving (`key`): "b", "1"…"9", "esc", "place". True when used.
	bool Key(const FString& K);
	/// Close the menu and drop the building held out (`closeBuild`).
	void Close();

	bool MenuOpen() const { return bMenu; }
	TOptional<ac::StructureKind> Placing() const { return Held; }
	/// Where the building held out would go (`ghostSpot`).
	TOptional<ac::Vec2> Spot(ac::StructureKind Kind) const;

private:
	void OnFrame(const FAcFrame& Frame);
	void Hook(AAcPilotPawn* Pawn);
	void Place();
	void Note(const FString& Text);
	void Stage(AAcPilotPawn& Pawn);

	FDelegateHandle FrameHandle;
	TWeakObjectPtr<AAcPilotPawn> Hooked;
	bool bMenu = false;
	TOptional<ac::StructureKind> Held;

	// Dev staging.
	FString StageMode;
	int32 StageNumber = 2;
	bool bStageKeys = false;
	bool bStaged = false;
	double PauseAt = -1;
	double PlaceAfter = -1;
	double PlaceAt = -1;
};
