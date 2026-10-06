// What the console shows (chunk D2): the Swift `CardButton` and `CardInfo`
// (Sources/Autocraft/Console.swift:5-73), and the selection half of
// `GameController.card` (GameController+Command.swift:176): a building's
// title, status, hit points, work in hand and production queue.
//
// The command card's buttons (`card`'s second half, `refusal`, `press`) are
// chunk D5's: it fills `FAcCardInfo::Buttons` (see `FAcConsoleInfo::ForStructure`)
// and hands the info to `UAcConsoleSubsystem::SetInfoSource`. The cockpit's
// fields (cargo, energy, prompt, tips, level) are filled by the pilot chunks.
#pragma once

#include "CoreMinimal.h"

namespace ac
{
	class Simulation;
	struct Structure;
}

/// One button on the command card.
struct FAcCardButton
{
	/// Where it sits on the 5 × 3 grid, row by row from the top left.
	int32 Slot = 0;
	/// The key that presses it, shown in its corner.
	FString Key;
	FString Title;
	/// An icon name (`FAcIcons`); empty: the button shows its title.
	FString Icon;
	int32 Ore = 0;
	int32 Hydrogen = 0;
	/// Seconds to build, train or research.
	TOptional<double> Time;
	bool bEnabled = true;
	/// Why it is greyed out (the tooltip says it).
	TOptional<FString> Why;
	/// Lit up: in the cockpit, the action that applies to what is ahead.
	bool bLit = false;
	/// A line for the tooltip ("Hold click", "Needs a Lab").
	TOptional<FString> Detail;
	/// Pressing it queues it for the team's AI commander: an amber frame.
	bool bQueues = false;

	bool operator==(const FAcCardButton&) const = default;
};

/// The reticle's tone (`PilotInfo.Tone`), for the cockpit's quick info.
enum class EAcTone : uint8 { Idle, Ready, Blocked, Working, Enemy };

/// A kind's level beside the driven unit's name (`LevelInfo`).
struct FAcLevelInfo
{
	int32 Level = 1;
	/// How far to the next level, 0…1.
	double Fraction = 0;
	/// "120 / 300 XP" or "MAX LEVEL".
	FString Text;

	bool operator==(const FAcLevelInfo&) const = default;
};

/// What the console shows: the selected building in the top-down view, or
/// the driven unit in the cockpit.
struct FAcCardInfo
{
	FString Title;
	FString Status;
	double Hp = 0;
	double MaxHp = 1;
	/// The icon for the portrait (`FAcIcons`).
	TOptional<FString> Icon;
	/// The work in hand (construction, training, research, drilling), 0…1.
	TOptional<FString> ProgressLabel;
	TOptional<double> Progress;
	/// The production queue as icon names, the one in training first.
	TArray<FString> Queue;
	TArray<FAcCardButton> Buttons;
	/// Why the last press did nothing, shown a moment over the card.
	TOptional<FString> Note;
	/// The load carried (cockpit): its icon and a line.
	TOptional<FString> CargoIcon;
	TOptional<FString> Cargo;
	/// Energy (cockpit, a Dropship's): now and its most.
	TOptional<double> Energy;
	double MaxEnergy = 0;
	/// The cockpit's quick info and its tone (unset: a dim hint).
	TOptional<FString> Prompt;
	TOptional<EAcTone> Tone;
	/// The cockpit's tips, taking turns along the screen's foot.
	TArray<FString> Tips;
	/// The driven kind's level and XP (cockpit).
	TOptional<FAcLevelInfo> Level;
	// Pick cards (`offer`) are chunk E9's, drawn over the dashboard by it.

	/// What changes the layout (not the bars, numbers and quick info): `CardInfo.layout`.
	FString LayoutKey() const;
};

struct AUTOCRAFT_API FAcConsoleInfo
{
	/// The selection half of `GameController.card(_:)` for one of the local
	/// player's buildings: title, status, hit points, icon, work in hand and
	/// queue. `bCommanded`: the team has an AI commander (the Citadel's
	/// status then says the harvest split and the bank). No buttons (D5).
	static FAcCardInfo ForStructure(const ac::Simulation& Sim, const ac::Structure& St, bool bCommanded);

	/// `Icons.name`: the icon of a unit or building kind.
	static FString IconName(int32 UnitKind);
	static FString StructureIconName(int32 StructureKind);
};
