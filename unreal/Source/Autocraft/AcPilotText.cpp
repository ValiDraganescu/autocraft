#include "AcPilotText.h"

#include "AcNewKinds.h"
#include "Leveling.h"
#include "Rules.h"

#include <cmath>

namespace
{
	FString Utf8(const std::string& S) { return UTF8_TO_TCHAR(S.c_str()); }

	const TCHAR* const Dot = TEXT("   ·   ");

	/// The move line, or the held one while the machine is held in place.
	FString MoveLine(const ac::Unit& U, const FAcPilotText& T)
	{
		return U.anchor.value_or(0) > 0 ? T.Held.Get(T.Move) : T.Move;
	}

	FString BuildLine() { return TEXT("B  build menu   ·   number  pick   ·   click  place"); }

	/// `transit`: a move under way that holds the unit, for the work ring.
	struct FTransit
	{
		FString Label;
		double Progress;
		TOptional<FString> Prompt;
	};
	TOptional<FTransit> Transit(const ac::Unit& U)
	{
		if (const std::optional<double> J = U.jump()) return FTransit{TEXT("JET PACK"), *J, {}};
		if (!U.anchor || *U.anchor <= 0 || *U.anchor >= 1) return {};
		const double S = *U.anchor;
		if (U.kind == ac::UnitKind::scorpion)
		{
			return U.anchored == true ? FTransit{TEXT("BURYING"), S, FString(TEXT("R  cancel"))}
									  : FTransit{TEXT("DIGGING OUT"), 1 - S, FString(TEXT("R  cancel"))};
		}
		return U.anchored == true ? FTransit{TEXT("ANCHORING"), S, FString(TEXT("R  cancel"))}
								  : FTransit{TEXT("PACKING UP"), 1 - S, FString(TEXT("R  cancel"))};
	}

	double Dist(ac::Vec2 A, ac::Vec2 B) { return std::hypot(A.x - B.x, A.y - B.y); }
}

FAcPilotText FAcPilotText::Of(const ac::UnitKind Kind)
{
	const FString Walk = TEXT("WASD  walk   ·   mouse  look"), Fire = TEXT("act.strike");
	auto Act = [](const TCHAR* Title, const FString& Icon, const TCHAR* Detail) { return FAct{Title, Icon, Detail}; };
	FAcPilotText T;
	switch (Kind)
	{
	case ac::UnitKind::prospector:
		T.Aimed = TEXT("Hold click  strike");
		T.Help = TEXT("Hold click  drill, strike, repair   ·   click  enter a Derrick   ·   R + hold click  repair (a Derrick too)")
			TEXT("   ·   walk into the Citadel to unload");
		T.Move = Walk;
		break;
	case ac::UnitKind::ranger:
		T.Act = Act(TEXT("Fire"), Fire, TEXT("Hold click: fires at what the sight is on"));
		T.Aimed = TEXT("Hold click  fire");
		T.Help = TEXT("Hold click  fire at what the sight is on");
		T.Move = Walk;
		break;
	case ac::UnitKind::comet:
		T.Act = Act(TEXT("Fire"), Fire, TEXT("Hold click: both pistols fire at what the sight is on"));
		T.Aimed = TEXT("Hold click  fire");
		T.Help = TEXT("Hold click  fire both pistols   ·   walk into a cliff  jet pack over it");
		T.Move = Walk;
		break;
	case ac::UnitKind::firefly:
		T.Act = Act(TEXT("Flame"), Fire, TEXT("Hold click: burns everything on the line ahead"));
		T.Aimed = TEXT("Hold click  flame");
		T.Help = TEXT("Hold click  flame everything on the line (where the tail points: the ring)");
		T.Move = TEXT("W S  drive   ·   A D  turn the hull   ·   mouse  aim the tail (the ring shows the flame)");
		break;
	case ac::UnitKind::juggernaut:
		T.Act = Act(TEXT("Grenades"), Fire, TEXT("Hold click: Breacher grenades; what they hit is slowed"));
		T.Aimed = TEXT("Hold click  fire grenades");
		T.Help = TEXT("Hold click  fire grenades (they slow)");
		T.Move = Walk;
		break;
	case ac::UnitKind::longbow:
		T.Act = Act(TEXT("Fire"), Fire, TEXT("Hold click: plasma blobs from the packed railgun; anchored, a shell that splashes"));
		T.Aimed = TEXT("Hold click  fire");
		T.Help = TEXT("Hold click  fire   ·   R  anchored mode: range 13, nothing under 2, walks slowly");
		T.Move = TEXT("W S  drive   ·   A D  turn the hull   ·   mouse  aim the turret (the ring shows the gun)");
		T.Held = FString(TEXT("mouse  aim the turret   ·   R  tank mode to drive"));
		break;
	case ac::UnitKind::dropship:
		T.Act = Act(TEXT("Heal"), TEXT("act.heal"), TEXT("Hold click: heals the unit under the sight, using energy"));
		T.Aimed = TEXT("Hold click  heal");
		T.Help = TEXT("Hold click  heal what the sight is on   ·   R  load the unit below, or unload all");
		T.Move = TEXT("WASD  fly   ·   mouse  look");
		break;
	case ac::UnitKind::kestrel:
		T.Act = Act(TEXT("Rockets / Rail"), Fire, TEXT("Hold click: rockets at a ground target under the sight, a rail gun slug at a flyer"));
		T.Aimed = TEXT("Hold click  rockets / rail gun");
		T.Help = TEXT("Hold click  rockets at the ground, rail gun at flyers   ·   flies over cliffs");
		T.Move = TEXT("WASD  fly   ·   mouse  look");
		break;
	case ac::UnitKind::hailstorm:
		T.Act = Act(TEXT("Flak"), Fire, TEXT("Hold click: flak at the flyer the sight is on; it bursts round it"));
		T.Aimed = TEXT("Hold click  fire flak");
		T.Help = TEXT("Hold click  fire flak at flyers (air only; the burst hurts flyers round it)");
		T.Move = TEXT("W S  drive   ·   A D  turn the hull   ·   mouse  aim the mount (the ring shows the guns)");
		break;
	case ac::UnitKind::peregrine:
		T.Act = Act(TEXT("Missiles"), Fire, TEXT("Hold click: seeker missiles at the flyer the sight is on (air only)"));
		T.Aimed = TEXT("Hold click  fire missiles");
		T.Help = TEXT("Hold click  fire missiles at flyers (they fly: a fast target can outrun a salvo)   ·   flies over cliffs");
		T.Move = TEXT("WASD  fly   ·   mouse  look");
		break;
	case ac::UnitKind::atlas:
		T.Act = Act(TEXT("Cannons"), Fire, TEXT("Hold click: twin siege cannons at the ground; the shell splashes enemies"));
		T.Aimed = TEXT("Hold click  fire cannons");
		T.Help = TEXT("Hold click  fire the cannons (ground only, enemies round the target take splash)   ·   R  Quake stomp: 30 damage and a slow within 2 cells");
		T.Move = TEXT("W S  walk   ·   A D  turn the legs   ·   mouse  aim the torso (the ring shows the cannons)");
		break;
	case ac::UnitKind::scorpion:
		T.Act = Act(TEXT("Sting"), Fire, TEXT("Hold click, buried: locks on for a second, then one heavy sting; 25 s to reload"));
		T.Aimed = TEXT("Hold click  sting (buried)");
		T.Help = TEXT("R  bury / dig out (2 s each)   ·   buried, hold click  lock on and sting what the sight is on");
		T.Move = Walk;
		T.Held = FString(TEXT("hold click  sting   ·   R  dig out to move"));
		break;
	default:
		T.Aimed = TEXT("Hold click  fire");
		T.Help = TEXT("Hold click  fire at what the sight is on");
		T.Move = Walk;
		break;
	}
	return T;
}

TOptional<FAcPilotFrame> FAcPilotFrame::Of(const ac::Simulation& Sim)
{
	const std::optional<ac::Unit> U = Sim.pilotUnit();
	if (!U) return {};
	FAcPilotFrame F;
	F.Unit = *U;
	F.Target = Sim.pilotTarget(*U);
	F.Sight = Sim.pilotSight(*U);
	F.Ability = Sim.pilotAbility(*U);
	if (U->task == ac::Unit::Task::attacking && U->target) F.Patient = Sim.state.unit(*U->target);
	return F;
}

bool AcPilotText::HasCab(const ac::UnitKind Driven)
{
	// A new kind without a cockpit of its own rides in its stand-in's (AcNewKinds.h).
	const ac::UnitKind Kind = AcNewKinds::BorrowedCockpit(Driven);
	return Kind == ac::UnitKind::prospector || Kind == ac::UnitKind::firefly || Kind == ac::UnitKind::longbow
		|| Kind == ac::UnitKind::hailstorm;
}

FString AcPilotText::Title(const ac::UnitKind Kind) { return Utf8(ac::Simulation::title(Kind)); }
FString AcPilotText::Title(const ac::StructureKind Kind) { return Utf8(ac::Simulation::title(Kind)); }

FString AcPilotText::FormatG(const double V)
{
	// %g with 6 significant digits, as Foundation's String(format:).
	char Buf[64];
	std::snprintf(Buf, sizeof(Buf), "%g", V);
	return UTF8_TO_TCHAR(Buf);
}

FString AcPilotText::Help(const ac::Unit& U)
{
	const FAcPilotText T = FAcPilotText::Of(U.kind);
	TArray<FString> Lines = {FString::Printf(TEXT("Tab  next %s   ·   V  view   ·   Esc  leave"), *Title(U.kind)), T.Help,
		ac::PilotBuild::builds(U.kind) ? BuildLine() : FString(), MoveLine(U, T)};
	Lines.RemoveAll([](const FString& L) { return L.IsEmpty(); });
	return FString::Join(Lines, TEXT("\n"));
}

FString AcPilotText::Hint(const ac::Unit& U)
{
	const FString Help = FAcPilotText::Of(U.kind).Help;
	const int32 At = Help.Find(Dot);
	return At == INDEX_NONE ? Help : Help.Left(At);
}

TArray<FString> AcPilotText::Tips(const ac::Unit& U)
{
	const FAcPilotText T = FAcPilotText::Of(U.kind);
	TArray<FString> Parts;
	T.Help.ParseIntoArray(Parts, Dot, false);
	if (Parts.Num() > 0) Parts.RemoveAt(0);
	TArray<FString> Tips = {MoveLine(U, T), FString::Join(Parts, Dot), ac::PilotBuild::builds(U.kind) ? BuildLine() : FString(),
		FString::Printf(TEXT("V  first or third person   ·   Tab  next %s"), *Title(U.kind)),
		TEXT("Esc or F  leave   ·   Space or E  same as a click"), TEXT("Option-scroll  hearing range"),
		TEXT("F7 F8 F9  music back, pause, next   ·   [ or ]  vote the track down or up"),
		TEXT("Z or X  pick a level-up card   ·   hold Option  free the pointer to click one")};
	Tips.RemoveAll([](const FString& L) { return L.IsEmpty(); });
	return Tips;
}

TOptional<FAcSightInfo> AcPilotText::Sight(const ac::Simulation& Sim, const std::optional<ac::PilotSight>& S)
{
	if (!S) return {};
	const FString Side = S->friend_ ? TEXT("") : TEXT("Enemy ");
	FAcSightInfo I;
	I.Distance = S->distance;
	I.Range = S->range;
	I.MinRange = S->minRange;
	I.bInRange = S->inRange;
	I.bFriend = S->friend_;
	if (const std::optional<ac::Unit> V = Sim.state.unit(S->target))
	{
		const bool bSlowed = V->slowUntil.value_or(0) > Sim.state.time;
		I.Title = Side + Title(V->kind) + (bSlowed ? TEXT(" (slowed)") : TEXT(""));
		I.Hp = FMath::Max(V->hp, 0.0);
		I.MaxHp = Sim.maxHP(*V);
		return I;
	}
	const std::optional<ac::Structure> B = Sim.state.structure(S->target);
	if (!B) return {};
	I.Title = Side + Title(B->kind);
	I.Hp = FMath::Max(B->hp, 0.0);
	I.MaxHp = ac::Rules::hp(B->kind);
	return I;
}

TOptional<FString> AcPilotText::Refusal(const ac::Unit& U, const FAcSightInfo& S)
{
	if (S.bFriend)
	{
		if (S.Hp >= S.MaxHp) return FString(TEXT("Full health"));
		if (U.energy.value_or(0) <= 0) return FString(TEXT("Out of energy"));
		if (S.bInRange) return {};
		return FString::Printf(TEXT("Out of reach  %.1f / %.0f"), S.Distance, S.Range);
	}
	if (S.Distance < S.MinRange) return FString::Printf(TEXT("Too close  %.1f / min %s"), S.Distance, *FormatG(S.MinRange));
	if (S.bInRange) return {};
	return FString::Printf(TEXT("Out of range  %.1f / %.0f"), S.Distance, S.Range);
}

FAcPilotInfo AcPilotText::Info(const ac::Simulation& Sim, const FAcPilotFrame& F, const FAcPilotInputs& In)
{
	const ac::GameState& S = Sim.state;
	const ac::Unit& U = F.Unit;
	const FAcPilotText Text = FAcPilotText::Of(U.kind);
	FAcPilotInfo Info;
	Info.Name = Title(U.kind).ToUpper();
	Info.Hp = FMath::Max(U.hp, 0.0);
	Info.MaxHp = Sim.maxHP(U);
	Info.Cargo = U.carrying;
	Info.bHydrogen = U.hydrogen == true;
	Info.Heading = U.look();
	Info.Position = U.position;
	Info.Help = Help(U);
	using ETask = ac::Unit::Task;
	if (U.task == ETask::mining)
	{
		Info.Progress = 1 - FMath::Max(U.timer, 0.0) / ac::Rules::miningTime;
		Info.ProgressLabel = TEXT("DRILLING");
		Info.Tone = EAcTone::Working;
		Info.Prompt = FString(TEXT("Let go or walk away to stop"));
	}
	else if (U.task == ETask::inDerrick)
	{
		Info.Progress = 1 - FMath::Max(U.timer, 0.0) / ac::Rules::hydrogenTime;
		Info.ProgressLabel = TEXT("PUMPING MH");
		Info.Tone = EAcTone::Working;
		Info.Inside = FString(TEXT("INSIDE THE DERRICK"));
	}
	else if (const TOptional<FTransit> T = Transit(U))
	{
		Info.Progress = T->Progress;
		Info.ProgressLabel = T->Label;
		Info.Tone = EAcTone::Working;
		Info.Prompt = T->Prompt;
	}
	else
	{
		const std::optional<ac::PilotTarget>& Target = F.Target;
		if (!Target)
		{
			if (Sim.pilot && Sim.pilot->mend && U.kind == ac::UnitKind::prospector) Info.Prompt = FString(TEXT("Nothing to repair ahead"));
			else if (U.carrying > 0) Info.Prompt = FString(TEXT("Walk into the Citadel to unload"));
		}
		else if (Target->is<ac::PilotTarget::Enemy>())
		{
			Info.Prompt = U.anchor.value_or(0) >= 1 ? FString(TEXT("Hold click  fire shell")) : Text.Aimed;
			Info.Tone = EAcTone::Enemy;
		}
		else if (Target->is<ac::PilotTarget::Heal>())
		{
			Info.Prompt = Text.Aimed;
			Info.Tone = EAcTone::Ready;
		}
		else if (Target->is<ac::PilotTarget::Patch>())
		{
			Info.Prompt = FString(TEXT("Hold click  drill ore"));
			Info.Tone = EAcTone::Ready;
		}
		else if (Target->is<ac::PilotTarget::Scaffold>())
		{
			Info.Prompt = FString(TEXT("Click  carry on building"));
			Info.Tone = EAcTone::Ready;
		}
		else if (Target->is<ac::PilotTarget::Repair>())
		{
			Info.Prompt = FString(TEXT("Hold click  repair"));
			Info.Tone = EAcTone::Ready;
		}
		else if (const ac::PilotTarget::Derrick* D = Target->as<ac::PilotTarget::Derrick>())
		{
			Info.Prompt = FString(D->busy ? TEXT("Derrick in use, wait") : TEXT("Click  enter the Derrick"));
			Info.Tone = D->busy ? EAcTone::Blocked : EAcTone::Ready;
		}
		else if (Target->is<ac::PilotTarget::Full>())
		{
			Info.Prompt = FString(TEXT("Hands full: unload at the Citadel"));
			Info.Tone = EAcTone::Blocked;
		}
		// A damaged building behind what the click goes to (its Derrick,
		// ore): R takes the click for the repair.
		if (Target && (Target->is<ac::PilotTarget::Derrick>() || Target->is<ac::PilotTarget::Patch>() || Target->is<ac::PilotTarget::Full>())
			&& Sim.pilotRepair(U) && Info.Prompt)
		{
			Info.Prompt = *Info.Prompt + TEXT("   ·   R + hold click  repair");
		}
	}
	Info.Sight = Sight(Sim, F.Sight);
	if (!Info.Prompt && Info.Sight)
	{
		if (const TOptional<FString> Why = Refusal(U, *Info.Sight))
		{
			Info.Prompt = Why;
			Info.Tone = EAcTone::Blocked;
		}
	}
	Info.Hit = In.Hit;

	// `pilotBuildInfo`: the note, and the prompt for a building held out
	// (the ghost itself is E8's).
	Info.Note = In.Note;
	if (In.Placing)
	{
		if (!In.PlacingSpot)
		{
			Info.Prompt = FString(TEXT("Face an MH well"));
			Info.Tone = EAcTone::Blocked;
		}
		else
		{
			std::optional<std::string> Why = Sim.buildRefusal(*In.Placing, ac::Pilot::player);
			if (!Why) Why = Sim.placementRefusal(*In.Placing, *In.PlacingSpot);
			Info.Prompt = Why ? Utf8(*Why) : FString::Printf(TEXT("Click  build %s   ·   Esc  cancel"), *Title(*In.Placing));
			Info.Tone = Why ? EAcTone::Blocked : EAcTone::Ready;
		}
	}
	Info.bThirdPerson = In.bThirdPerson;
	Info.bCab = HasCab(U.kind) && !Info.bThirdPerson;
	Info.Lamps = Info.Tone == EAcTone::Working ? EAcCabLamps::Working
		: U.anchor.value_or(0) >= 1		   ? EAcCabLamps::Locked
											   : EAcCabLamps::Idle;
	Info.Console = Console(Sim, F, Info, In);

	// The compass points home with a load, else to the nearest field.
	auto Edge = [&U](const ac::Vec2 P, const double R) { return FMath::Max(0.0, Dist(P, U.position) - R); };
	const ac::Structure* Home = nullptr;
	if (U.kind == ac::UnitKind::prospector && U.carrying > 0)
	{
		for (const ac::Structure& B : S.structures)
		{
			if (B.kind == ac::StructureKind::citadel && B.owner == U.owner && B.complete()
				&& (!Home || Dist(B.position, U.position) < Dist(Home->position, U.position)))
			{
				Home = &B;
			}
		}
	}
	if (Home)
	{
		Info.Goal = FAcPilotGoal{TEXT("CITADEL"), Home->position, Edge(Home->position, ac::Rules::radius(ac::StructureKind::citadel))};
	}
	else if (U.kind == ac::UnitKind::prospector && U.task != ETask::mining && U.task != ETask::inDerrick)
	{
		const ac::OreDeposit* Near = nullptr;
		for (const ac::OreDeposit& P : S.patches)
		{
			if (P.remaining > 0 && (!Near || Dist(P.position, U.position) < Dist(Near->position, U.position))) Near = &P;
		}
		if (Near && Edge(Near->position, 1) > 1.5) Info.Goal = FAcPilotGoal{TEXT("ORE"), Near->position, Edge(Near->position, 1)};
	}
	return Info;
}

FAcLevelInfo AcPilotText::Level(const ac::Simulation& Sim, const ac::UnitKind Kind)
{
	const ac::PilotRecord& R = Sim.levels();
	FAcLevelInfo L;
	L.Level = (int32)R.level(Kind);
	const double Xp = R.xp(Kind);
	const double From = ac::Leveling::xp(L.Level);
	if (L.Level < ac::Leveling::cap)
	{
		const double To = ac::Leveling::xp(L.Level + 1);
		L.Fraction = FMath::Clamp((Xp - From) / FMath::Max(To - From, 1.0), 0.0, 1.0);
		L.Text = FString::Printf(TEXT("%lld / %lld XP"), (long long)std::floor(Xp), (long long)To);
	}
	else
	{
		L.Fraction = 1;
		L.Text = TEXT("MAX LEVEL");
	}
	return L;
}

FString AcPilotText::Status(const FAcPilotFrame& F)
{
	const ac::Unit& U = F.Unit;
	using ETask = ac::Unit::Task;
	switch (U.task)
	{
	case ETask::mining: return TEXT("Drilling");
	case ETask::inDerrick: return TEXT("Inside the Derrick");
	case ETask::building: return TEXT("Building");
	case ETask::repairing: return TEXT("Repairing");
	default: break;
	}
	if (U.jumpFrom) return TEXT("Jet pack");
	if (U.anchor && *U.anchor > 0)
	{
		return U.anchored == true ? (*U.anchor >= 1 ? TEXT("Anchored") : TEXT("Anchoring")) : TEXT("Packing up");
	}
	if (F.Patient) return TEXT("Healing");
	return U.carrying > 0 ? TEXT("Carrying a load home") : TEXT("Ready");
}

FAcCardInfo AcPilotText::Console(const ac::Simulation& Sim, const FAcPilotFrame& F, const FAcPilotInfo& P,
	const FAcPilotInputs& In)
{
	const ac::Unit& U = F.Unit;
	FAcCardInfo Info;
	Info.Title = Title(U.kind);
	Info.Status = Status(F);
	Info.Hp = FMath::Max(U.hp, 0.0);
	Info.MaxHp = Sim.maxHP(U);
	Info.Icon = FAcConsoleInfo::IconName((int32)U.kind);
	Info.Progress = P.Progress;
	if (P.Progress) Info.ProgressLabel = P.ProgressLabel;
	if (ac::PilotBuild::builds(U.kind))
	{
		const bool bMh = U.hydrogen == true;
		Info.Cargo = U.carrying > 0 ? FString::Printf(TEXT("%lld %s"), (long long)U.carrying, bMh ? TEXT("MH") : TEXT("ore"))
									: FString(TEXT("empty"));
		if (U.carrying > 0) Info.CargoIcon = FString(bMh ? TEXT("well") : TEXT("ore"));
	}
	if (U.cargo)
	{
		Info.Cargo = FString::Printf(TEXT("%lld / %lld"), (long long)Sim.slotsUsed(U), (long long)Sim.cargoSlots(U));
		if (!U.cargo->empty())
		{
			if (const std::optional<ac::Unit> First = Sim.state.unit(U.cargo->front()))
			{
				Info.CargoIcon = FAcConsoleInfo::IconName((int32)First->kind);
			}
		}
	}
	if (U.energy)
	{
		Info.Energy = *U.energy;
		Info.MaxEnergy = ac::Rules::maxEnergy;
	}
	Info.Note = P.Note;
	Info.Prompt = P.Prompt ? *P.Prompt : Hint(U);
	if (P.Prompt) Info.Tone = P.Tone;
	Info.Tips = Tips(U);
	Info.Level = Level(Sim, U.kind);

	if (!ac::PilotBuild::builds(U.kind))
	{
		// `soldierCard`: its act, lit while it applies to what the sight is
		// on, and its ability on R, greyed with the reason.
		if (const TOptional<FAcPilotText::FAct> Act = FAcPilotText::Of(U.kind).Act)
		{
			const FString Range = Sim.pilotShoots(U) ? TEXT(", range ") + FormatG(Sim.weapon(U).range) : FString();
			FAcCardButton B;
			B.Slot = 0;
			B.Key = TEXT("hold");
			B.Title = Act->Title;
			B.Icon = Act->Icon;
			B.bLit = F.Target.has_value();
			B.Detail = Act->Detail + Range;
			Info.Buttons.Add(B);
		}
		if (F.Ability)
		{
			// No icon: the button shows its title ("Anchor Mode", "Unload All").
			FAcCardButton B;
			B.Slot = 1;
			B.Key = TEXT("R");
			B.Title = Utf8(F.Ability->title);
			B.bEnabled = F.Ability->action.has_value();
			if (F.Ability->why) B.Why = Utf8(*F.Ability->why);
			Info.Buttons.Add(B);
		}
		return Info;
	}
	if (In.bBuildMenu || In.Placing)
	{
		int32 I = 0;
		for (const ac::StructureKind K : ac::PilotBuild::kinds)
		{
			const std::optional<std::string> Why = Sim.buildRefusal(K, ac::Pilot::player);
			FAcCardButton B;
			B.Slot = I;
			B.Key = FString::FromInt(I + 1);
			B.Title = Title(K);
			B.Icon = FAcConsoleInfo::StructureIconName((int32)K);
			B.Ore = (int32)ac::Rules::cost(K);
			B.Hydrogen = (int32)ac::Rules::hydrogenCost(K);
			B.Time = ac::Rules::buildTime(K);
			B.bEnabled = !Why;
			if (Why) B.Why = Utf8(*Why);
			B.bLit = In.Placing == K;
			Info.Buttons.Add(B);
			++I;
		}
		FAcCardButton Back;
		Back.Slot = 14;
		Back.Key = TEXT("Esc");
		Back.Title = TEXT("Back");
		Back.Icon = TEXT("act.back");
		Info.Buttons.Add(Back);
		return Info;
	}
	const std::optional<ac::PilotTarget>& T = F.Target;
	using ETask = ac::Unit::Task;
	auto Button = [](int32 Slot, const TCHAR* Key, const TCHAR* Title, const TCHAR* Icon, bool bLit, const TCHAR* Detail)
	{
		FAcCardButton B;
		B.Slot = Slot;
		B.Key = Key;
		B.Title = Title;
		B.Icon = Icon;
		B.bLit = bLit;
		B.Detail = FString(Detail);
		return B;
	};
	Info.Buttons = {
		Button(0, TEXT("hold"), TEXT("Drill ore"), TEXT("ore"), (T && T->is<ac::PilotTarget::Patch>()) || U.task == ETask::mining,
			TEXT("Hold click at an ore deposit")),
		Button(1, TEXT("click"), TEXT("Pump MH"), TEXT("derrick"), (T && T->is<ac::PilotTarget::Derrick>()) || U.task == ETask::inDerrick,
			TEXT("Click at your Derrick")),
		Button(2, TEXT("hold"), TEXT("Repair"), TEXT("act.repair"), (T && T->is<ac::PilotTarget::Repair>()) || U.task == ETask::repairing,
			TEXT("Hold click at a damaged building (hold R too at a Derrick)")),
		Button(3, TEXT("hold"), TEXT("Strike"), TEXT("act.strike"), T && T->is<ac::PilotTarget::Enemy>(),
			TEXT("Hold click at an enemy: 5 damage")),
		Button(10, TEXT("B"), TEXT("Build"), TEXT("act.build"), (T && T->is<ac::PilotTarget::Scaffold>()) || U.task == ETask::building,
			TEXT("Open the buildings")),
	};
	return Info;
}
