#include "AcCommandCard.h"

#include "Commander.h"
#include "Pilot.h"
#include "Rules.h"
#include "Simulation.h"

namespace
{
	using EType = FAcCardAction::EType;
	using SKind = ac::StructureKind;

	FString Utf8(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

	constexpr ac::Upgrade AllUpgrades[] = {ac::Upgrade::minigun, ac::Upgrade::aegisShield, ac::Upgrade::novaIgniters,
		ac::Upgrade::lifelineReactor};
	constexpr ac::Harvest AllHarvests[] = {ac::Harvest::ore, ac::Harvest::balanced, ac::Harvest::hydrogen};

	/// The bank buttons in slot order (11-14).
	const FAcCardAction KeepOrder[] = {FAcCardAction::Keep(-100, 0), FAcCardAction::Keep(100, 0), FAcCardAction::Keep(0, -100),
		FAcCardAction::Keep(0, 100)};

	/// The building whose units and upgrades the card shows (a Lab shows its parent's).
	std::optional<ac::Structure> HostOf(const ac::GameState& S, const ac::Structure& St)
	{
		if (St.kind != SKind::lab) return St;
		return St.parent ? S.structure(*St.parent) : std::nullopt;
	}

	/// The Lab doing the research for `St` (itself, or its add-on).
	std::optional<ac::Structure> LabOf(const ac::GameState& S, const ac::Structure& St)
	{
		if (St.kind == SKind::lab) return St;
		return St.addon ? S.structure(*St.addon) : std::nullopt;
	}

	const ac::Player* PlayerOf(const ac::GameState& S, int64 P)
	{
		return P >= 0 && P < (int64)S.players.size() ? &S.players[(size_t)P] : nullptr;
	}

	FString UnitName(ac::UnitKind K) { return Utf8(std::string(ac::EnumInfo<ac::UnitKind>::names[(size_t)K])); }
	FString StructureName(ac::StructureKind K) { return Utf8(std::string(ac::EnumInfo<ac::StructureKind>::names[(size_t)K])); }
}

// MARK: - Actions

bool FAcCardAction::operator==(const FAcCardAction& O) const
{
	if (Type != O.Type) return false;
	switch (Type)
	{
	case EType::Train: return Unit == O.Unit;
	case EType::Research: return Upgrade == O.Upgrade;
	case EType::Build: return Building == O.Building;
	case EType::Split: return Harvest == O.Harvest;
	case EType::Keep: return KeepOre == O.KeepOre && KeepHydrogen == O.KeepHydrogen;
	default: return true;
	}
}

FString FAcCardAction::Describe() const
{
	switch (Type)
	{
	case EType::Train: return FString::Printf(TEXT("train(%s)"), *UnitName(Unit));
	case EType::Addon: return TEXT("addon");
	case EType::Research: return FString::Printf(TEXT("research(%s)"), *Utf8(std::string(ac::EnumInfo<ac::Upgrade>::names[(size_t)Upgrade])));
	case EType::BuildMenu: return TEXT("buildMenu");
	case EType::Build: return FString::Printf(TEXT("build(%s)"), *StructureName(Building));
	case EType::Back: return TEXT("back");
	case EType::Split: return FString::Printf(TEXT("split(%s)"), *Utf8(std::string(ac::EnumInfo<ac::Harvest>::names[(size_t)Harvest])));
	case EType::Keep: return FString::Printf(TEXT("keep(ore: %d, hydrogen: %d)"), KeepOre, KeepHydrogen);
	}
	return TEXT("?");
}

bool FAcCommandCard::Commanded(const ac::Simulation& Sim, const int64 Player)
{
	for (const ac::Commander& C : Sim.commanders)
	{
		if (C.player == Player) return true;
	}
	return false;
}

TArray<FAcCardAction> FAcCommandCard::Actions(const ac::Simulation& Sim, const ac::Structure& St, const bool bCommanded,
	const bool bBuildMenu)
{
	const ac::GameState& S = Sim.state;
	TArray<FAcCardAction> Out;
	if (const std::optional<ac::Structure> Host = HostOf(S, St))
	{
		for (const ac::UnitKind K : ac::Rules::trains(Host->kind)) Out.Add(FAcCardAction::Train(K));
		if ((Host->kind == SKind::garrison || Host->kind == SKind::foundry || Host->kind == SKind::spacedock) && !Host->addon)
		{
			Out.Add(FAcCardAction::Addon());
		}
		for (const ac::Upgrade Up : AllUpgrades)
		{
			if (ac::at(Up) == Host->kind && !Sim.has(St.owner, Up)) Out.Add(FAcCardAction::Research(Up));
		}
	}
	if (St.kind == SKind::lab) Out.RemoveAll([](const FAcCardAction& A) { return A.Type != EType::Research; });
	if (St.kind == SKind::citadel && St.complete() && bCommanded)
	{
		// The team's economy, the same from every Citadel.
		if (bBuildMenu)
		{
			Out.Reset();
			for (const SKind K : ac::PilotBuild::kinds) Out.Add(FAcCardAction::Build(K));
			Out.Add(FAcCardAction::Back());
		}
		else
		{
			for (const ac::Harvest H : AllHarvests) Out.Add(FAcCardAction::Split(H));
			Out.Add(FAcCardAction::BuildMenu());
			for (const FAcCardAction& K : KeepOrder) Out.Add(K);
		}
	}
	return Out;
}

// MARK: - Refusal

TOptional<FAcCardRefusal> FAcCommandCard::Refusal(const ac::Simulation& Sim, const FAcCardAction& A, const ac::Structure& St)
{
	auto No = [](const TCHAR* Why, bool bQueues) { return TOptional<FAcCardRefusal>(FAcCardRefusal{Why, bQueues}); };
	const ac::GameState& S = Sim.state;
	switch (A.Type)
	{
	case EType::BuildMenu:
	case EType::Back:
	case EType::Split:
	case EType::Keep: return {};
	case EType::Build: return No(TEXT("Placed by the AI commander"), true);
	default: break;
	}
	const std::optional<ac::Structure> Host = HostOf(S, St);
	if (!Host) return No(TEXT("No building"), false);
	const ac::Player* P = PlayerOf(S, Host->owner);
	if (!P) return No(TEXT("No building"), false);
	if (!Host->complete()) return No(TEXT("Still under construction"), true);
	const std::optional<ac::Structure> Lab = LabOf(S, *Host);
	auto Money = [&](int64 Ore, int64 Hydrogen) -> TOptional<FAcCardRefusal>
	{
		if (P->ore < Ore) return No(TEXT("Not enough ore"), true);
		if (P->hydrogen < Hydrogen) return No(TEXT("Not enough MH"), true);
		return {};
	};
	switch (A.Type)
	{
	case EType::Train:
	{
		if (Host->queueCount() >= ac::Rules::maxQueue) return No(TEXT("The queue is full"), true);
		if (Lab && !Lab->complete()) return No(TEXT("Wait for the Lab"), true);
		if (ac::Rules::needsLab(A.Unit) && !(Lab && Lab->complete())) return No(TEXT("Needs a Lab"), true);
		if (TOptional<FAcCardRefusal> Why = Money(ac::Rules::cost(A.Unit), ac::Rules::hydrogenCost(A.Unit))) return Why;
		if (P->supplyUsed + ac::Rules::supply(A.Unit) > P->supplyCap) return No(TEXT("Not enough supply: build a Hab Dome"), true);
		break;
	}
	case EType::Addon:
	{
		if (Host->addon) return No(TEXT("Already has a Lab"), false);
		if (Host->training) return No(TEXT("Busy training"), true);
		if (TOptional<FAcCardRefusal> Why = Money(ac::Rules::cost(SKind::lab), ac::Rules::hydrogenCost(SKind::lab))) return Why;
		break;
	}
	case EType::Research:
	{
		if (Sim.has(Host->owner, A.Upgrade)) return No(TEXT("Already researched"), false);
		for (const ac::Structure& O : S.structures)
		{
			if (O.owner == Host->owner && O.research == A.Upgrade) return No(TEXT("Already researching"), false);
		}
		if (!Lab || !Lab->complete()) return No(TEXT("Needs a Lab"), true);
		if (Lab->research) return No(TEXT("Already researching"), true);
		if (TOptional<FAcCardRefusal> Why = Money(ac::ore(A.Upgrade), ac::hydrogen(A.Upgrade))) return Why;
		break;
	}
	default: break;
	}
	return {};
}

// MARK: - Labels

FString FAcCommandCard::Title(const FAcCardAction& A)
{
	switch (A.Type)
	{
	case EType::Train: return Utf8(ac::Simulation::title(A.Unit));
	case EType::Addon: return TEXT("Lab");
	case EType::Research: return Utf8(ac::name(A.Upgrade));
	case EType::BuildMenu: return TEXT("Build");
	case EType::Split: return SplitName(A.Harvest);
	case EType::Keep:
		// "+100 ore", "−100 MH" (a real minus sign, as Swift).
		return A.KeepOre != 0 ? FString::Printf(TEXT("%s100 ore"), A.KeepOre > 0 ? TEXT("+") : TEXT("−"))
							  : FString::Printf(TEXT("%s100 MH"), A.KeepHydrogen > 0 ? TEXT("+") : TEXT("−"));
	case EType::Build: return Utf8(ac::Simulation::title(A.Building));
	case EType::Back: return TEXT("Back");
	}
	return FString();
}

int32 FAcCommandCard::OreCost(const FAcCardAction& A)
{
	switch (A.Type)
	{
	case EType::Train: return (int32)ac::Rules::cost(A.Unit);
	case EType::Addon: return (int32)ac::Rules::cost(SKind::lab);
	case EType::Research: return (int32)ac::ore(A.Upgrade);
	case EType::Build: return (int32)ac::Rules::cost(A.Building);
	default: return 0;
	}
}

int32 FAcCommandCard::HydrogenCost(const FAcCardAction& A)
{
	switch (A.Type)
	{
	case EType::Train: return (int32)ac::Rules::hydrogenCost(A.Unit);
	case EType::Addon: return (int32)ac::Rules::hydrogenCost(SKind::lab);
	case EType::Research: return (int32)ac::hydrogen(A.Upgrade);
	case EType::Build: return (int32)ac::Rules::hydrogenCost(A.Building);
	default: return 0;
	}
}

TOptional<ac::Request::What> FAcCommandCard::Request(const FAcCardAction& A)
{
	switch (A.Type)
	{
	case EType::Train: return ac::Request::What(ac::Request::What::Unit{A.Unit});
	case EType::Addon: return ac::Request::What(ac::Request::What::Building{SKind::lab});
	case EType::Research: return ac::Request::What(ac::Request::What::Upgrade{A.Upgrade});
	case EType::Build: return ac::Request::What(ac::Request::What::Building{A.Building});
	default: return {};
	}
}

FString FAcCommandCard::Effect(const ac::Upgrade Up)
{
	switch (Up)
	{
	case ac::Upgrade::minigun: return TEXT("Rangers fire mini guns: 3 s bursts, about half again the damage");
	case ac::Upgrade::aegisShield: return TEXT("Rangers +10 hit points");
	case ac::Upgrade::novaIgniters: return TEXT("Fireflies +5 damage against light units");
	case ac::Upgrade::lifelineReactor: return TEXT("Dropships regain energy twice as fast");
	}
	return FString();
}

FString FAcCommandCard::SplitName(const ac::Harvest H)
{
	switch (H)
	{
	case ac::Harvest::ore: return TEXT("Ore");
	case ac::Harvest::balanced: return TEXT("Balanced");
	default: return TEXT("MH");
	}
}

FString FAcCommandCard::SplitDetail(const ac::Harvest H)
{
	switch (H)
	{
	case ac::Harvest::ore: return TEXT("Every Prospector on ore and no new Derricks (MH your queue needs is still pumped)");
	case ac::Harvest::balanced: return TEXT("The AI's own split: ore first, MH past the first lines");
	default: return TEXT("Every Derrick full, and Derricks early");
	}
}

FString FAcCommandCard::UpgradeIcon(const ac::Upgrade Up)
{
	return TEXT("up.") + Utf8(std::string(ac::EnumInfo<ac::Upgrade>::names[(size_t)Up]));
}

// MARK: - The card

TArray<FAcCardButton> FAcCommandCard::Buttons(const ac::Simulation& Sim, const ac::Structure& St, const TArray<FAcCardAction>& Actions,
	const bool bCommanded)
{
	const ac::GameState& S = Sim.state;
	const ac::Player* Owner = PlayerOf(S, St.owner);
	const ac::Directives D = Owner ? Owner->orders() : ac::Directives();
	// The grid: units along the top row, upgrades on the middle one, the add-on bottom left.
	int32 NextUnit = 0;
	int32 NextUp = St.kind == SKind::lab ? 0 : 5;
	TArray<FAcCardButton> Out;
	for (int32 I = 0; I < Actions.Num(); ++I)
	{
		const FAcCardAction& A = Actions[I];
		const TOptional<FAcCardRefusal> No = Refusal(Sim, A, St);
		// A button the team's AI can see to (money, supply, a lab still to
		// come) stays pressable: the press queues it.
		const bool bQueues = No && No->bQueues && bCommanded;
		FAcCardButton B;
		B.Key = FString::FromInt(I + 1);
		B.Title = Title(A);
		B.Ore = OreCost(A);
		B.Hydrogen = HydrogenCost(A);
		B.bEnabled = !No || bQueues;
		if (No) B.Why = No->Why + (bQueues ? TEXT(": click to queue it") : TEXT(""));
		B.bQueues = bQueues;
		switch (A.Type)
		{
		case EType::Train:
			B.Slot = NextUnit++;
			B.Icon = FAcConsoleInfo::IconName((int32)A.Unit);
			B.Time = ac::Rules::trainTime(A.Unit);
			if (ac::Rules::needsLab(A.Unit)) B.Detail = FString(TEXT("Needs a Lab"));
			break;
		case EType::Addon:
			B.Slot = 10;
			B.Icon = FAcConsoleInfo::StructureIconName((int32)SKind::lab);
			B.Time = ac::Rules::buildTime(SKind::lab);
			B.Detail = FString(TEXT("Unlocks advanced units and research"));
			break;
		case EType::Research:
			B.Slot = NextUp++;
			B.Icon = UpgradeIcon(A.Upgrade);
			B.Time = ac::time(A.Upgrade);
			B.Detail = Effect(A.Upgrade);
			break;
		case EType::BuildMenu:
			B.Slot = 10;
			B.Icon = TEXT("act.build");
			B.Detail = FString(TEXT("Ask the AI commander for a building; it picks the spot"));
			break;
		case EType::Build:
		{
			B.Slot = I;
			B.Icon = FAcConsoleInfo::StructureIconName((int32)A.Building);
			B.Time = ac::Rules::buildTime(A.Building);
			FString Detail = TEXT("The AI commander places it");
			if (const std::optional<SKind> Needs = ac::Rules::requires_(A.Building))
			{
				Detail += TEXT(", after a ") + Utf8(ac::Simulation::title(*Needs));
			}
			B.Detail = Detail;
			B.bQueues = true;
			B.bEnabled = true;
			B.Why.Reset();
			break;
		}
		case EType::Back:
			B.Slot = 14;
			B.Icon = TEXT("act.back");
			break;
		case EType::Split:
		{
			int32 Index = 0;
			for (int32 H = 0; H < (int32)UE_ARRAY_COUNT(AllHarvests); ++H)
			{
				if (AllHarvests[H] == A.Harvest) Index = H;
			}
			B.Slot = 5 + Index;
			B.Icon = A.Harvest == ac::Harvest::ore ? TEXT("ore") : A.Harvest == ac::Harvest::hydrogen ? TEXT("derrick") : TEXT("");
			B.bLit = D.harvest == A.Harvest;
			B.Detail = SplitDetail(A.Harvest);
			break;
		}
		case EType::Keep:
		{
			int32 Index = 0;
			for (int32 K = 0; K < (int32)UE_ARRAY_COUNT(KeepOrder); ++K)
			{
				if (KeepOrder[K] == A) Index = K;
			}
			B.Slot = 11 + Index;
			B.Detail = FString::Printf(
				TEXT("What the AI leaves in the bank when it spends for itself (your queue may spend it). Now: %lld ore, %lld MH"),
				(long long)D.keepOre, (long long)D.keepHydrogen);
			break;
		}
		}
		Out.Add(MoveTemp(B));
	}
	return Out;
}

FAcCardInfo FAcCommandCard::Card(const ac::Simulation& Sim, const ac::Structure& St, const bool bCommanded, FAcCardState& State,
	const double Now)
{
	FAcCardInfo Info = FAcConsoleInfo::ForStructure(Sim, St, bCommanded);
	if (State.For != TOptional<int64>(St.id))
	{
		// `select`: another building drops the build menu and the note.
		State.bBuildMenu = false;
		State.Note.Reset();
		State.For = St.id;
	}
	State.Actions = Actions(Sim, St, bCommanded, State.bBuildMenu);
	Info.Buttons = Buttons(Sim, St, State.Actions, bCommanded);
	if (State.Note && State.NoteUntil > Now) Info.Note = State.Note;
	else State.Note.Reset();
	return Info;
}

// MARK: - Press

FAcCardPress FAcCommandCard::Press(const ac::Simulation& Sim, const ac::Structure& St, const int32 Index, const FAcCardMods Mods,
	const bool bCommanded, const int64 Player, FAcCardState& State, const double Now, TFunctionRef<bool(const ac::Command&)> Issue)
{
	FAcCardPress Out;
	if (!State.Actions.IsValidIndex(Index) || State.For != TOptional<int64>(St.id)) return Out;
	const FAcCardAction A = State.Actions[Index];
	auto Note = [&](const FString& Text)
	{
		State.Note = Text;
		State.NoteUntil = Now + 2;
	};
	Out.bChanged = true;
	switch (A.Type)
	{
	case EType::BuildMenu: State.bBuildMenu = true; return Out;
	case EType::Back: State.bBuildMenu = false; return Out;
	case EType::Split:
		Issue(ac::Command(ac::Command::Split{Player, A.Harvest}));
		Out.Log = TEXT("card: Prospector split ") + Utf8(std::string(ac::EnumInfo<ac::Harvest>::names[(size_t)A.Harvest]));
		Out.bCommander = true;
		return Out;
	case EType::Keep:
	{
		const ac::Player* P = PlayerOf(Sim.state, Player);
		const ac::Directives D = P ? P->orders() : ac::Directives();
		const int64 Ore = FMath::Clamp<int64>(D.keepOre + A.KeepOre, 0, 5000);
		const int64 Hydrogen = FMath::Clamp<int64>(D.keepHydrogen + A.KeepHydrogen, 0, 5000);
		Issue(ac::Command(ac::Command::Keep{Player, Ore, Hydrogen}));
		// Swift logs the bank as it stands after the order.
		const ac::Directives After = P ? P->orders() : ac::Directives();
		Out.Log = FString::Printf(TEXT("card: keep %lld ore, %lld MH"), (long long)After.keepOre, (long long)After.keepHydrogen);
		Out.bCommander = true;
		return Out;
	}
	default: break;
	}
	const int64 Host = St.kind == SKind::lab ? St.parent.value_or(St.id) : St.id;
	const TOptional<FAcCardRefusal> No = Refusal(Sim, A, St);
	const FString Where = FString::Printf(TEXT("%s #%lld %s"), *Utf8(ac::Simulation::title(St.kind)), (long long)St.id, *A.Describe());
	const TOptional<ac::Request::What> What = Request(A);
	if (bCommanded && What && ((No && No->bQueues) || Mods.bShift || Mods.bOption || (!No && A.Type == EType::Build)))
	{
		const bool bRepeats = Mods.bOption && What->is<ac::Request::What::Unit>();
		const int64 Count = Mods.bShift && !bRepeats ? 5 : 1;
		const bool bOk = Issue(ac::Command(ac::Command::Request{Player, *What, Count, bRepeats, Host, std::nullopt}));
		Note(bOk ? TEXT("Queued for the commander: ") + Title(A) + (bRepeats ? TEXT(" (keep making)") : Count > 1 ? FString::Printf(TEXT(" ×%lld"), (long long)Count) : FString())
				 : FString(TEXT("Can't queue that")));
		Out.Log = FString::Printf(TEXT("card: %s queued %s"), *Where, bOk ? TEXT("ok") : TEXT("refused"));
		if (A.Type == EType::Build) State.bBuildMenu = false;
		Out.bCommander = true;
		return Out;
	}
	if (No)
	{
		Note(No->Why);
		return Out;
	}
	bool bOk = false;
	switch (A.Type)
	{
	case EType::Train: bOk = Issue(ac::Command(ac::Command::Train{Host, A.Unit})); break;
	case EType::Addon: bOk = Issue(ac::Command(ac::Command::Addon{Host})); break;
	case EType::Research:
	{
		const int64 LabId = St.kind == SKind::lab ? St.id : St.addon.value_or(-1);
		bOk = Issue(ac::Command(ac::Command::Research{LabId, A.Upgrade}));
		break;
	}
	default: break;
	}
	if (!bOk) Note(TEXT("Can't do that now"));
	Out.Log = FString::Printf(TEXT("card: %s %s"), *Where, bOk ? TEXT("ok") : TEXT("refused"));
	return Out;
}
