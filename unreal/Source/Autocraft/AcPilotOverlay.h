// `UAcPilotOverlaySubsystem` (chunk E6): the cockpit's HUD while a unit is
// driven, the port of `HUD.applyPilot` (Sources/Autocraft/HUD.swift:430)
// with the controller's side of it (`pilotInfo`, `markPilotHit`,
// `notePilot`; GameController+Pilot.swift, GameController+Command.swift).
//
// Each frame at the `Hud` stage, while `AAcPilotPawn` drives a unit:
//   - `AcPilotText::Info` makes the `FAcPilotInfo` (prompt and tone, work,
//     sight, goal, note, hit mark; the gun marker from E4's `GunMarker`);
//   - `SAcPilotOverlay` (layer 35, over the console) draws it;
//   - the console (`UAcConsoleSubsystem`, D2/D3) gets the cockpit or cab
//     look (`SetLook`: the cab's lamps; the view frame only in third person)
//     and the driven unit's card (`SetPilotInfo`: portrait, status, level
//     badge and XP bar, cargo, energy, quick info and tips, its buttons);
//   - the scoreboard goes while driving (`AAcHUD::SetDriving`).
// On leaving, all of it goes back to the top-down look.
//
// Hit marks: a `Landed` event of the driven unit (`markPilotHit`), the
// damage as the sight's target was worth before the step (`pilotAim`).
// Notes: `AAcPilotPawn::OnNote` and `Note()` (1.8 s, `notePilot`).
//
// Hooks: E4 sets `GunMarker`; E8 calls `SetBuild` (menu open, building held
// out and its spot) and `Note`; E9 reads `LastInfo()`.
//
// Dev (shots and checks; held for stills):
//   -AcPilotMark=DAMAGE|kill   a hit mark, held just after it lands
//   -AcPilotHurt               the hurt flash, held
//   -AcPilotNote=TEXT          a note over the reticle, held
//   -AcPilotRun=S              a staged still (-AcPilotStage, paused) runs on
//                              for S game seconds first with the act key
//                              held (a drill under way, as `--pilot mining`)
//   (-AcNoConsole: the overlay's own panel, prompt and keys)
//   ac.PilotMark DAMAGE [kill], ac.PilotNote TEXT (console)
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "AcPilotText.h"

#include "AcPilotOverlay.generated.h"

class SAcPilotOverlay;
class UAcSimSubsystem;
struct FAcFrame;

UCLASS()
class AUTOCRAFT_API UAcPilotOverlaySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/// The overlay's layer in `SAcRoot` (over the console, 20, and the bar, 30).
	static constexpr int32 Layer = 35;

	static UAcPilotOverlaySubsystem* Get(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/// A short note over the reticle and on the dashboard (`notePilot`).
	void Note(const FString& Text);
	/// E8: the build menu is open, a building is held out (`placing`) and
	/// where it would go (`ghostSpot`; unset: no spot, "Face an MH well").
	void SetBuild(bool bMenu, TOptional<ac::StructureKind> Placing, TOptional<ac::Vec2> Spot);
	/// E4: the gun marker, off the view's centre in half the view's height
	/// (+y up); unset: none.
	TFunction<TOptional<FVector2D>()> GunMarker;
	/// The cockpit as last shown (unset while not driving).
	const TOptional<FAcPilotInfo>& LastInfo() const { return Shown; }
	TSharedPtr<SAcPilotOverlay> Overlay() const { return Widget; }

	/// Dev: a hit mark as `markPilotHit` makes one.
	void Mark(double Damage, bool bKill);

private:
	void OnFrame(const FAcFrame& Frame);
	void Attach(bool bPanel);
	void Detach();

	TSharedPtr<SAcPilotOverlay> Widget;
	FDelegateHandle FrameHandle;
	FDelegateHandle NoteHandle;
	TWeakObjectPtr<class AAcPilotPawn> NotePawn;
	int64 DrivenId = INDEX_NONE;
	TOptional<FAcPilotInfo> Shown;

	FAcPilotInputs Inputs;
	TOptional<FString> NoteText;
	double NoteUntil = 0;
	/// `pilotAim`: the sight's target and one attack's damage on it, before the step.
	TOptional<TPair<int64, double>> Aim;

	// Dev holds.
	TOptional<FAcHitMark> HeldMark;
	bool bHeldHurt = false;
	FString HeldNote;
	double RunFor = 0;
	double RunUntil = -1;
};
