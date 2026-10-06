// The command map's game logic (chunk D8, GAME-LAYER.md §2.3 "Command map"),
// with no Slate: the port of `GameController+Map.swift` (`orders(at:)` :85,
// `updateMap` :35) and the order helpers of `GameController+Command.swift`
// (`updateCommander` :422, `objectiveTitle`, `objectiveIcon`), plus the
// types of `CommandMap.swift` (`MapOrder`, `MapMenu`, `CommandMapInfo`).
//
// Eight players: "the team" is the local player's commander (its orders are
// `players[Player].orders()`); "ours" is any player allied to it (green on
// the map), "enemy" any hostile one (red). A Bastion to man must be the
// player's own (the core refuses an ally's); a base to defend may be an
// ally's; an attack snaps onto a hostile building and is not offered next to
// an allied one.
#pragma once

#include "CoreMinimal.h"

#include "SimdMath.h"
#include "Types.h"

#include <map>

namespace ac
{
	class Simulation;
	struct Commander;
}

/// An order the command map's menu offers at a spot (`MapOrder`).
struct FAcMapOrder
{
	enum class EType : uint8 { Attack, Defend, Man, Expand, Cancel };
	EType Type = EType::Attack;
	/// Attack, defend, man: the point.
	ac::Vec2 At;
	/// Expand: the base site.
	int64 Site = 0;
	/// Cancel: the objective's or request's id, and what it is ("attack 1").
	int64 Id = 0;
	FString What;

	static FAcMapOrder Attack(ac::Vec2 P) { FAcMapOrder O; O.Type = EType::Attack; O.At = P; return O; }
	static FAcMapOrder Defend(ac::Vec2 P) { FAcMapOrder O; O.Type = EType::Defend; O.At = P; return O; }
	static FAcMapOrder Man(ac::Vec2 P) { FAcMapOrder O; O.Type = EType::Man; O.At = P; return O; }
	static FAcMapOrder Expand(int64 I) { FAcMapOrder O; O.Type = EType::Expand; O.Site = I; return O; }
	static FAcMapOrder Cancel(int64 Id, const FString& What) { FAcMapOrder O; O.Type = EType::Cancel; O.Id = Id; O.What = What; return O; }

	/// "Attack here", "Expand here (site 3)", "Cancel attack 1".
	FString Title() const;
	bool operator==(const FAcMapOrder& B) const
	{
		return Type == B.Type && At.x == B.At.x && At.y == B.At.y && Site == B.Site && Id == B.Id && What == B.What;
	}
};

/// The order menu, open at a ground point (`MapMenu`).
struct FAcMapMenu
{
	ac::Vec2 At;
	TArray<FAcMapOrder> Items;
};

/// What the command map shows (`CommandMapInfo`).
struct FAcCommandMapInfo
{
	struct FSite
	{
		int64 Index = 0;
		ac::Vec2 At;
		/// Held by the team or an ally (true), by an enemy (false), or free.
		TOptional<bool> Ours;
		/// Whose, when held: "Yours", or with more than two players the
		/// holder's name ("Gold"), else "Enemy".
		FString Holder;
		bool bContested = false;
		/// The AI's own next expansion.
		bool bNext = false;
		/// A Citadel the humans asked for here.
		bool bQueued = false;
	};
	struct FBuilding
	{
		ac::StructureKind Kind = ac::StructureKind::citadel;
		ac::Vec2 At;
		bool bOurs = false;
		bool bComplete = true;
	};
	struct FMarker
	{
		int64 Id = 0;
		ac::Objective::Kind Kind = ac::Objective::Kind::attack;
		ac::Vec2 At;
		FString Title;
		FString Status;
		/// Where its squad is (unset: no squad).
		TOptional<ac::Vec2> Squad;
	};
	struct FRequest
	{
		int64 Id = 0;
		FString Icon;
		FString Title;
		/// "×3", "∞" (keep making) or "".
		FString Count;
		FString Status;
		/// 0…1 of its cost in the bank, in steps of 0.05.
		double Funded = 0;
	};
	TArray<FSite> Sites;
	TArray<FBuilding> Buildings;
	TArray<FMarker> Markers;
	TArray<FRequest> Queue;
	ac::Stance Stance = ac::Stance::auto_;
	/// "Prospectors: MH · Keep 300 ore, 0 MH".
	FString Economy;
	TOptional<FAcMapMenu> Menu;
	/// The room the pilots' rows take in the right column (E9 draws them;
	/// `CommandMap.pilotsHeight`), 0: none.
	double PilotsRoom = 0;
};

/// One beacon in the world (`showObjectives`): an objective, or (unset kind)
/// a site the humans asked to expand to.
struct FAcBeaconSpec
{
	int64 Id = 0;
	TOptional<ac::Objective::Kind> Kind;
	ac::Vec2 At;
};

struct AUTOCRAFT_API FAcCommandMapLogic
{
	/// The commander playing `Player` (Swift `sim.commanders.first { $0.player == team }`).
	static const ac::Commander* CommanderOf(const ac::Simulation& Sim, int64 Player);

	/// The orders that make sense at a map point (`orders(at:)`): cancel what
	/// is there, man the player's Bastion under it, defend the team's base
	/// under it, expand to the free site under it, or attack it (snapped onto
	/// an enemy building there).
	static TArray<FAcMapOrder> Orders(const ac::Simulation& Sim, int64 Player, ac::Vec2 P);

	/// The command an order gives (cancel: a request's or an objective's).
	static ac::Command CommandFor(const ac::Simulation& Sim, int64 Player, const FAcMapOrder& Order);
	/// Take request or objective `Id` off (`cancel`).
	static ac::Command CancelCommand(const ac::Simulation& Sim, int64 Player, int64 Id);

	/// The map's contents (`updateMap`), with the objectives' status as last
	/// asked of the AI (`objectiveStatus`, refreshed with the beacons).
	static FAcCommandMapInfo Info(const ac::Simulation& Sim, const ac::Commander& Ai, int64 Player,
		const std::map<int64_t, ac::ObjectiveStatus>& Status);

	/// The beacons in the world (`updateCommander`): every objective, and
	/// every request with a site.
	static TArray<FAcBeaconSpec> Beacons(const ac::Simulation& Sim, int64 Player);

	/// "Attack 2", "Defend 1": numbered in the order given.
	static FString ObjectiveTitle(const ac::Objective& O, int32 Number);
	static FString ObjectiveIcon(ac::Objective::Kind K);
	/// The stance's line under the stance buttons (`stanceLine`).
	static FString StanceLine(ac::Stance S);
	static FString StanceTitle(ac::Stance S);
	/// The beacon colour (`GameScene.beaconColor`; unset: an expansion), as Slate linear.
	static FLinearColor BeaconColor(TOptional<ac::Objective::Kind> Kind);
	/// A command for the log ("stance(0, aggressive)").
	static FString Describe(const ac::Command& C);
};
