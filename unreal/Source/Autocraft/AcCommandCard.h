// The command card's logic (chunk D5; GAME-LAYER.md §2.3 "Command card"):
// the port of `card` (the buttons half), `refusal`, `label`, `request` and
// `press` of Sources/Autocraft/GameController+Command.swift (:176-:410).
// No widgets and no UObjects: it reads `ac::Simulation` and gives out
// `ac::Command`s through a function, so the automation tests
// (`Autocraft.Card.*`, AcCommandCardTests.cpp) drive it on hand-built states.
//
// The console (D2, `SAcConsole`) draws the `FAcCardInfo` this makes; the
// wiring (selection, clicks, hotkeys, modifier keys) is `UAcCommandsSubsystem`
// in AcCommands.h.
#pragma once

#include "CoreMinimal.h"

#include "AcConsoleInfo.h"

#include "Types.h"

namespace ac
{
	class Simulation;
}

/// What a card button does (Swift `CardAction`).
struct FAcCardAction
{
	enum class EType : uint8
	{
		Train,
		/// A Garrison, Foundry or Spacedock builds its Lab.
		Addon,
		Research,
		/// The Citadel's Build button: opens the buildings the team's AI can put up.
		BuildMenu,
		/// One of those buildings (a request; the AI places it).
		Build,
		Back,
		/// The team's Prospector split.
		Split,
		/// The team's bank floor, ±100 ore or MH.
		Keep,
	};

	EType Type = EType::Back;
	ac::UnitKind Unit = ac::UnitKind::prospector;
	ac::StructureKind Building = ac::StructureKind::citadel;
	ac::Upgrade Upgrade = ac::Upgrade::minigun;
	ac::Harvest Harvest = ac::Harvest::balanced;
	int32 KeepOre = 0;
	int32 KeepHydrogen = 0;

	static FAcCardAction Train(ac::UnitKind K) { FAcCardAction A; A.Type = EType::Train; A.Unit = K; return A; }
	static FAcCardAction Addon() { FAcCardAction A; A.Type = EType::Addon; return A; }
	static FAcCardAction Research(ac::Upgrade U) { FAcCardAction A; A.Type = EType::Research; A.Upgrade = U; return A; }
	static FAcCardAction BuildMenu() { FAcCardAction A; A.Type = EType::BuildMenu; return A; }
	static FAcCardAction Build(ac::StructureKind K) { FAcCardAction A; A.Type = EType::Build; A.Building = K; return A; }
	static FAcCardAction Back() { FAcCardAction A; A.Type = EType::Back; return A; }
	static FAcCardAction Split(ac::Harvest H) { FAcCardAction A; A.Type = EType::Split; A.Harvest = H; return A; }
	static FAcCardAction Keep(int32 Ore, int32 Hydrogen) { FAcCardAction A; A.Type = EType::Keep; A.KeepOre = Ore; A.KeepHydrogen = Hydrogen; return A; }

	bool operator==(const FAcCardAction& O) const;
	/// For the log, as Swift prints a `CardAction` ("train(ranger)").
	FString Describe() const;
};

/// Why a button would do nothing now, and whether the team's AI can see to
/// it if asked (`queues`: money, supply, a Lab still to come, a busy building).
struct FAcCardRefusal
{
	FString Why;
	bool bQueues = false;
};

/// The modifier keys at the press (Swift reads `NSEvent.modifierFlags`).
struct FAcCardMods
{
	/// Shift: queue five.
	bool bShift = false;
	/// Option (Alt): a standing order to keep making the unit.
	bool bOption = false;
};

/// The card's own state between frames (the Swift `GameController` fields
/// `cardBuild`, `cardNote`, `cardActions`), one per local player.
struct FAcCardState
{
	/// The Citadel's card shows the build menu.
	bool bBuildMenu = false;
	/// The note over the card and until when it shows (the caller's clock).
	TOptional<FString> Note;
	double NoteUntil = 0;
	/// The actions of the card last shown, in button order (hotkey n = index n-1).
	TArray<FAcCardAction> Actions;
	/// The building they were made for.
	TOptional<int64> For;
};

/// What a press did.
struct FAcCardPress
{
	/// The card changed (the caller shows it again at once).
	bool bChanged = false;
	/// The team's orders changed: refresh the command map and beacons now
	/// (Swift `updateCommander(now: true)`).
	bool bCommander = false;
	/// The line for the log ("card: Garrison #12 train(ranger) ok"), empty if none.
	FString Log;
};

struct AUTOCRAFT_API FAcCommandCard
{
	/// The actions a building's card holds (`card`'s action list), in order.
	/// `bCommanded`: the player has an AI commander.
	static TArray<FAcCardAction> Actions(const ac::Simulation& Sim, const ac::Structure& St, bool bCommanded, bool bBuildMenu);

	/// `refusal`: why the action would do nothing now (unset: it would).
	static TOptional<FAcCardRefusal> Refusal(const ac::Simulation& Sim, const FAcCardAction& A, const ac::Structure& St);

	/// `label`: the button's title and its ore and MH price.
	static FString Title(const FAcCardAction& A);
	static int32 OreCost(const FAcCardAction& A);
	static int32 HydrogenCost(const FAcCardAction& A);

	/// `request`: what the action asks the team's AI for (unset: nothing).
	static TOptional<ac::Request::What> Request(const FAcCardAction& A);

	/// The buttons for `Actions` (`card`'s second half).
	static TArray<FAcCardButton> Buttons(const ac::Simulation& Sim, const ac::Structure& St, const TArray<FAcCardAction>& Actions,
		bool bCommanded);

	/// The whole `card(st)`: the selection's info (`FAcConsoleInfo::ForStructure`)
	/// plus the buttons, with the note while it lasts (`Now` on the same clock
	/// as `FAcCardState::NoteUntil`). Keeps the actions in `State`.
	static FAcCardInfo Card(const ac::Simulation& Sim, const ac::Structure& St, bool bCommanded, FAcCardState& State, double Now);

	/// `press(i)`: button `Index` of the card last made by `Card` for the
	/// building `St`. Orders go out through `Issue` (the sim's `issue`, true
	/// when it took). `Player` is the local player (the team's orders).
	static FAcCardPress Press(const ac::Simulation& Sim, const ac::Structure& St, int32 Index, FAcCardMods Mods, bool bCommanded,
		int64 Player, FAcCardState& State, double Now, TFunctionRef<bool(const ac::Command&)> Issue);

	/// `GameController.effect`: what an upgrade does, for its tooltip.
	static FString Effect(ac::Upgrade Up);
	/// `splitName`, `splitDetail`.
	static FString SplitName(ac::Harvest H);
	static FString SplitDetail(ac::Harvest H);
	/// `Icons.name` of an upgrade ("up.minigun").
	static FString UpgradeIcon(ac::Upgrade Up);
	/// The Swift `commanded`: `Player` has an AI commander.
	static bool Commanded(const ac::Simulation& Sim, int64 Player);
};
