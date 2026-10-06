// `UAcCommandsSubsystem` (chunk D5): the command card wired into the game.
// The logic is `FAcCommandCard` (AcCommandCard.h, engine-free); this:
//   - gives the console (D2) the whole `card()` through
//     `UAcConsoleSubsystem::SetInfoSource` and hears its buttons
//     (`SAcConsole::SetOnCardButton`);
//   - follows the selection (D4's `UAcPointer::OnSelectionChanged`, or the
//     console's own `-AcSelect`/`ac.ConsoleSelect`): a new building drops
//     the build menu and the note, as Swift `select`;
//   - presses with the modifier keys held at that moment (Shift: queue five,
//     Option: keep making), as Swift reads `NSEvent.modifierFlags`;
//   - hotkeys (Swift `key`): 1-9 press the card's action n, Esc lets go of
//     the selection; not while driving, with ⌘/Ctrl, or while blocked
//     (the command map, D8: `SetBlocked`).
//
// For D8 (command map): `OnCommanderChanged` fires after a press changed the
// team's orders (Swift `updateCommander(now: true)`).
//
// Scripted presses (headless shots and tests; no desktop automation):
//   -AcPress=LIST   presses once the card is up, one a frame: 1-based keys
//                   separated by '/', each with an optional `s` (Shift) or
//                   `o` (Option): `-AcPress=5/1s` = Build, then the first
//                   building shifted.
//   ac.CardPress N [shift] [option]
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "AcCommandCard.h"

#include "AcCommands.generated.h"

class UAcSimSubsystem;
struct FAcFrame;

DECLARE_MULTICAST_DELEGATE(FAcCommanderChanged);

UCLASS()
class AUTOCRAFT_API UAcCommandsSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UAcCommandsSubsystem* Get(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/// Press button `Index` (0-based, into the card's buttons) of the
	/// selected building's card. Unset `Mods`: the keys held now.
	void Press(int32 Index, TOptional<FAcCardMods> Mods = {});
	/// The card's state (actions shown, build menu, note).
	const FAcCardState& State() const { return Card; }
	/// The command map is open: no hotkeys.
	void SetBlocked(bool bInBlocked) { bBlocked = bInBlocked; }

	FAcCommanderChanged OnCommanderChanged;

private:
	void OnFrame(const FAcFrame& Frame);
	TOptional<FAcCardInfo> MakeInfo(const UAcSimSubsystem& Sim, int64 StructureId);
	void OnSelectionChanged();
	void PollKeys();
	TOptional<int64> SelectedId() const;
	static double Now();

	FAcCardState Card;
	FDelegateHandle FrameHandle;
	FDelegateHandle SelectionHandle;
	TWeakPtr<class SAcConsole> BoundConsole;
	TWeakObjectPtr<class UAcPointer> BoundPointer;
	bool bBlocked = false;

	/// -AcPress: the presses still to make.
	TArray<TPair<int32, FAcCardMods>> Scripted;
};
