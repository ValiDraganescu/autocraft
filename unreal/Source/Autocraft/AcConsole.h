// `UAcConsoleSubsystem` (chunk D2): puts the console (`SAcConsole`) and the
// frame round the view (`SAcViewFrame`) into the HUD root, feeds the console
// the selection every frame at the `Hud` stage, and hands its height
// (`cover`) to the RTS camera (`AAcRtsPawn::SetCover`), as the Swift
// `GameController` does with `Console.cover` and `updateSelection`.
//
// Selection: one of the local player's buildings at a time
// (`GameController.select`, :145). Chunk D4 picks it with the mouse
// (`Select`); it is dropped when the building dies or changes owner.
// The card's info is `FAcConsoleInfo::ForStructure` (title, status, bars,
// queue) until chunk D5 sets `SetInfoSource` with the whole `card()`
// (buttons included) and binds `OnCardButton`.
//
// Command line (shots):
//   -AcSelect=KIND|ID   select the local player's first building of that
//                       kind (citadel, garrison, habdome...) or that id
//   -AcConsoleNote=TEXT a card note held for shots
//   -AcConsoleDemo      a Citadel's buttons on any card (for shots, until D5)
//   -AcNoConsole        no console (the camera keeps the default cover)
//   -AcConsoleStyle=rts|cockpit|cab  the console's look (D3; shots)
//   -AcCabLamps=idle|working|locked  the cab's lamps
// Console: `ac.ConsoleSelect KIND|ID|none`, `ac.ConsoleStyle rts|cockpit|cab
// [idle|working|locked]`, `ac.ConsoleWarp 0|1` (flat faces, for checks).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "AcCab.h"
#include "AcConsoleInfo.h"

#include "AcConsole.generated.h"

class SAcConsole;
class SAcViewFrame;
class UAcSimSubsystem;
struct FAcFrame;

UCLASS()
class AUTOCRAFT_API UAcConsoleSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UAcConsoleSubsystem* Get(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/// The console widget (null until the HUD is up, or in a run with no viewport).
	TSharedPtr<SAcConsole> Console() const { return ConsoleWidget; }

	/// Select one of the local player's buildings (unset: none).
	void Select(TOptional<int64> StructureId);
	TOptional<int64> Selected() const { return SelectedId; }
	/// Select by `-AcSelect` text: a building kind's name or an id.
	void SelectByName(const FString& Text);

	/// Who makes the card's info (D5: `card()` with the buttons). Unset: the
	/// selection's info without buttons. Return unset to show nothing.
	using FInfoSource = TFunction<TOptional<FAcCardInfo>(const UAcSimSubsystem& Sim, int64 StructureId)>;
	void SetInfoSource(FInfoSource Source) { InfoSource = MoveTemp(Source); }

	/// The console's look (`HUD.applyPilot`, chunk D3): `Cockpit` while
	/// driving, `Cab` in a machine with a cab (Prospector, Firefly, Longbow,
	/// Hailstorm: `PilotInfo.cab`) with its lamps, `Rts` back in the top-down
	/// view; `bViewFrame`: the frame round the view shows (the top-down view
	/// and third person; hidden in first person, where the cockpit frames it).
	void SetLook(EAcConsoleStyle Style, EAcCabLamps Lamps = EAcCabLamps::Idle, bool bViewFrame = true);
	EAcConsoleStyle Look() const { return LookStyle; }
	/// "rts", "cockpit", "cab"; "idle", "working", "locked" (flags, console).
	static EAcConsoleStyle StyleNamed(const FString& Name);
	static EAcCabLamps LampsNamed(const FString& Name);

	/// The cockpit's info (E-chunks): while set, the console shows this
	/// instead of the selection.
	void SetPilotInfo(TOptional<FAcCardInfo> Info) { PilotInfo = MoveTemp(Info); }

private:
	void OnFrame(const FAcFrame& Frame);
	void OnGameStarted(UAcSimSubsystem& Sim);
	bool Attach();
	void UpdateMinimapSize(const UAcSimSubsystem& Sim);

	TSharedPtr<SAcConsole> ConsoleWidget;
	TSharedPtr<SAcViewFrame> ViewFrameWidget;
	FDelegateHandle FrameHandle;
	FDelegateHandle StartedHandle;
	TOptional<int64> SelectedId;
	FString PendingSelect;
	FString HeldNote;
	FInfoSource InfoSource;
	TOptional<FAcCardInfo> PilotInfo;
	double LastCover = -1;
	bool bDisabled = false;
	bool bDemo = false;
	EAcConsoleStyle LookStyle = EAcConsoleStyle::Rts;
	EAcCabLamps LookLamps = EAcCabLamps::Idle;
	bool bLookViewFrame = true;
};
