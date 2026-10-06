#include "AcCommandMapLogic.h"

#include "AcCommandCard.h"
#include "AcConsoleInfo.h"
#include "AcHudStyle.h"

#include "Commander.h"
#include "Rules.h"
#include "Simulation.h"

#include <cmath>
#include <set>

namespace
{
	using SKind = ac::StructureKind;
	using OKind = ac::Objective::Kind;

	FString Utf8(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

	/// The nearest of `Items` to `P` by `Pos`, if any.
	template <typename T, typename F>
	const T* Nearest(const TArray<const T*>& Items, const ac::Vec2 P, F Pos)
	{
		const T* Best = nullptr;
		double D = 0;
		for (const T* I : Items)
		{
			const double E = ac::distance(Pos(*I), P);
			if (!Best || E < D)
			{
				Best = I;
				D = E;
			}
		}
		return Best;
	}
}

FString FAcMapOrder::Title() const
{
	switch (Type)
	{
	case EType::Attack: return TEXT("Attack here");
	case EType::Defend: return TEXT("Defend this base");
	case EType::Man: return TEXT("Man this Bastion");
	case EType::Expand: return FString::Printf(TEXT("Expand here (site %lld)"), (long long)(Site + 1));
	case EType::Cancel: return TEXT("Cancel ") + What;
	}
	return FString();
}

const ac::Commander* FAcCommandMapLogic::CommanderOf(const ac::Simulation& Sim, const int64 Player)
{
	for (const ac::Commander& C : Sim.commanders)
	{
		if (C.player == Player) return &C;
	}
	return nullptr;
}

FString FAcCommandMapLogic::ObjectiveTitle(const ac::Objective& O, const int32 Number)
{
	switch (O.kind)
	{
	case OKind::attack: return FString::Printf(TEXT("Attack %d"), Number);
	case OKind::defend: return FString::Printf(TEXT("Defend %d"), Number);
	case OKind::hold: return FString::Printf(TEXT("Hold %d"), Number);
	case OKind::man: return FString::Printf(TEXT("Man Bastion %d"), Number);
	}
	return FString();
}

FString FAcCommandMapLogic::ObjectiveIcon(const OKind K)
{
	switch (K)
	{
	case OKind::attack: return TEXT("act.strike");
	case OKind::defend:
	case OKind::hold: return TEXT("up.aegisShield");
	case OKind::man: return FAcConsoleInfo::StructureIconName((int32)SKind::bastion);
	}
	return FString();
}

FString FAcCommandMapLogic::StanceTitle(const ac::Stance S)
{
	return Utf8(ac::title(S));
}

FString FAcCommandMapLogic::StanceLine(const ac::Stance S)
{
	switch (S)
	{
	case ac::Stance::auto_: return TEXT("The AI's own play: attack waves when the odds are good.");
	case ac::Stance::aggressive: return TEXT("Smaller waves at worse odds; it holds on longer.");
	case ac::Stance::hold: return TEXT("No attacks of its own: it defends and waits at the rally.");
	case ac::Stance::allIn: return TEXT("Everything attacks now and never falls back.");
	}
	return FString();
}

FLinearColor FAcCommandMapLogic::BeaconColor(const TOptional<OKind> Kind)
{
	if (!Kind) return FAcHudStyle::Srgb(1, 0.76, 0.3);
	switch (*Kind)
	{
	case OKind::attack: return FAcHudStyle::Srgb(1, 0.32, 0.22);
	case OKind::defend: return FAcHudStyle::Srgb(0.35, 0.72, 1);
	case OKind::hold: return FAcHudStyle::Srgb(0.3, 0.92, 0.78);
	case OKind::man: return FAcHudStyle::Srgb(0.78, 0.52, 1);
	}
	return FLinearColor::White;
}

TArray<FAcMapOrder> FAcCommandMapLogic::Orders(const ac::Simulation& Sim, const int64 Player, const ac::Vec2 P)
{
	const ac::GameState& S = Sim.state;
	if (Player < 0 || Player >= (int64)S.players.size()) return {};
	const ac::Directives D = S.players[(size_t)Player].orders();
	auto Near = [P](ac::Vec2 Q, double R) { return ac::distance(P, Q) < R; };
	TArray<FAcMapOrder> Out;
	for (size_t K = 0; K < D.objectives.size(); ++K)
	{
		const ac::Objective& O = D.objectives[K];
		if (Near(O.at, 3)) Out.Add(FAcMapOrder::Cancel(O.id, ObjectiveTitle(O, int32(K + 1)).ToLower()));
	}
	for (const ac::Request& R : D.queue)
	{
		if (!R.site || *R.site < 0 || *R.site >= (int64_t)Sim.sites.size() || !Near(Sim.sites[(size_t)*R.site], 4)) continue;
		Out.Add(FAcMapOrder::Cancel(R.id, FString::Printf(TEXT("expansion to site %lld"), (long long)(*R.site + 1))));
	}
	// The player's own Bastion under the point (the core mans only its own).
	{
		TArray<const ac::Structure*> Bastions;
		for (const ac::Structure& St : S.structures)
		{
			if (St.owner == Player && St.kind == SKind::bastion && Near(St.position, ac::Rules::radius(SKind::bastion) + 1)) Bastions.Add(&St);
		}
		if (const ac::Structure* B = Nearest(Bastions, P, [](const ac::Structure& X) { return X.position; }))
		{
			const bool bManned = std::any_of(D.objectives.begin(), D.objectives.end(),
				[B](const ac::Objective& O) { return O.kind == OKind::man && O.structure == B->id; });
			if (!bManned) Out.Add(FAcMapOrder::Man(B->position));
		}
	}
	// The team's base under it (an ally's too).
	{
		TArray<const ac::Structure*> Bases;
		for (const ac::Structure& St : S.structures)
		{
			if (S.allied(St.owner, Player) && St.kind == SKind::citadel && Near(St.position, 7)) Bases.Add(&St);
		}
		if (const ac::Structure* Base = Nearest(Bases, P, [](const ac::Structure& X) { return X.position; }))
		{
			const bool bGuarded = std::any_of(D.objectives.begin(), D.objectives.end(), [Base](const ac::Objective& O)
			{
				return O.kind == OKind::defend && ac::distance(O.at, Base->position) < 7;
			});
			if (!bGuarded) Out.Add(FAcMapOrder::Defend(Base->position));
		}
	}
	// The free site under it.
	{
		std::set<int64_t> Taken, Queued;
		for (const ac::Structure& St : S.structures)
		{
			if (St.kind == SKind::citadel) Taken.insert(Sim.site(St));
		}
		for (const ac::Request& R : D.queue)
		{
			if (R.site) Queued.insert(*R.site);
		}
		TOptional<int64> Best;
		for (int64 I = 0; I < (int64)Sim.sites.size(); ++I)
		{
			if (!Near(Sim.sites[(size_t)I], 6) || Taken.count(I) || Queued.count(I)) continue;
			if (!Best || ac::distance(Sim.sites[(size_t)I], P) < ac::distance(Sim.sites[(size_t)*Best], P)) Best = I;
		}
		if (Best) Out.Add(FAcMapOrder::Expand(*Best));
	}
	// Attack, snapped onto an enemy building there; never next to our own.
	const bool bOurs = std::any_of(S.structures.begin(), S.structures.end(), [&](const ac::Structure& St)
	{
		return S.allied(St.owner, Player) && Near(St.position, ac::Rules::radius(St.kind) + 6);
	});
	const bool bAimed = std::any_of(D.objectives.begin(), D.objectives.end(),
		[&](const ac::Objective& O) { return (O.kind == OKind::attack || O.kind == OKind::hold) && Near(O.at, 3); });
	if (!bOurs && !bAimed)
	{
		TArray<const ac::Structure*> Targets;
		for (const ac::Structure& St : S.structures)
		{
			if (S.hostile(St.owner, Player) && Near(St.position, ac::Rules::radius(St.kind) + 3)) Targets.Add(&St);
		}
		const ac::Structure* T = Nearest(Targets, P, [](const ac::Structure& X) { return X.position; });
		Out.Add(FAcMapOrder::Attack(T ? T->position : P));
	}
	return Out;
}

ac::Command FAcCommandMapLogic::CancelCommand(const ac::Simulation& Sim, const int64 Player, const int64 Id)
{
	const ac::Directives D = Sim.state.players[(size_t)Player].orders();
	const bool bRequest = std::any_of(D.queue.begin(), D.queue.end(), [Id](const ac::Request& R) { return R.id == Id; });
	if (bRequest) return ac::Command(ac::Command::CancelRequest{Player, Id});
	return ac::Command(ac::Command::CancelObjective{Player, Id});
}

ac::Command FAcCommandMapLogic::CommandFor(const ac::Simulation& Sim, const int64 Player, const FAcMapOrder& O)
{
	switch (O.Type)
	{
	case FAcMapOrder::EType::Attack: return ac::Command(ac::Command::Objective{Player, OKind::attack, O.At});
	case FAcMapOrder::EType::Defend: return ac::Command(ac::Command::Objective{Player, OKind::defend, O.At});
	case FAcMapOrder::EType::Man: return ac::Command(ac::Command::Objective{Player, OKind::man, O.At});
	case FAcMapOrder::EType::Expand:
		return ac::Command(ac::Command::Request{Player, ac::Request::What(ac::Request::What::Building{SKind::citadel}), 1, false,
			std::nullopt, O.Site});
	case FAcMapOrder::EType::Cancel: break;
	}
	return CancelCommand(Sim, Player, O.Id);
}

FAcCommandMapInfo FAcCommandMapLogic::Info(const ac::Simulation& Sim, const ac::Commander& Ai, const int64 Player,
	const std::map<int64_t, ac::ObjectiveStatus>& Status)
{
	const ac::GameState& S = Sim.state;
	const ac::Directives D = S.players[(size_t)Player].orders();
	FAcCommandMapInfo Info;
	std::set<int64_t> Queued;
	for (const ac::Request& R : D.queue)
	{
		if (R.site) Queued.insert(*R.site);
	}
	for (const ac::SiteInfo& Si : Ai.siteInfo(Sim))
	{
		FAcCommandMapInfo::FSite Site;
		Site.Index = Si.index;
		Site.At = Si.centre;
		if (Si.owner)
		{
			Site.Ours = S.allied(*Si.owner, Player);
			Site.Holder = *Si.owner == Player ? FString(TEXT("Yours"))
				: S.players.size() > 2       ? FAcHudStyle::PlayerName(*Si.owner)
				: *Site.Ours                 ? FString(TEXT("Yours"))
				                             : FString(TEXT("Enemy"));
		}
		Site.bContested = Si.contested;
		Site.bNext = Si.next;
		Site.bQueued = !Si.owner && Queued.count(Si.index) > 0;
		Info.Sites.Add(Site);
	}
	// What `Player` knows (the user's choice, unlike Swift, which reads every
	// building of `sim.state`): its own and its allies' buildings, the others'
	// in sight now, and the others' as last seen (`Intel.buildings`, like the
	// minimap and `Simulation::shown`). Without fog (the playground's switch,
	// a session with fog false) every building shows.
	const bool bFog = Sim.vision && S.intel && Player >= 0 && Player < (int64)S.intel->size() && Player < (int64)Sim.sights.size();
	const std::set<int64_t>* Sight = bFog ? &Sim.sights[(size_t)Player].ids : nullptr;
	for (const ac::Structure& St : S.structures)
	{
		if (St.hp <= 0) continue;
		const bool bAlly = S.allied(St.owner, Player);
		if (bFog && !bAlly && !Sight->count(St.id)) continue;
		Info.Buildings.Add({St.kind, St.position, bAlly, St.complete()});
	}
	if (bFog)
	{
		for (const ac::Structure& St : (*S.intel)[(size_t)Player].buildings)
		{
			if (St.hp <= 0 || Sight->count(St.id)) continue;
			Info.Buildings.Add({St.kind, St.position, S.allied(St.owner, Player), St.complete()});
		}
	}
	for (size_t K = 0; K < D.objectives.size(); ++K)
	{
		const ac::Objective& O = D.objectives[K];
		FAcCommandMapInfo::FMarker M;
		M.Id = O.id;
		M.Kind = O.kind;
		M.At = O.at;
		M.Title = ObjectiveTitle(O, int32(K + 1));
		const auto It = Status.find(O.id);
		M.Status = It != Status.end() ? Utf8(It->second.text) : FString(TEXT("Next second"));
		if (It != Status.end() && It->second.centre) M.Squad = *It->second.centre;
		Info.Markers.Add(M);
	}
	std::map<int64_t, ac::RequestStatus> Requests;
	for (const ac::RequestStatus& R : Ai.requestStatus(Sim)) Requests.emplace(R.id, R);
	for (const ac::Request& R : D.queue)
	{
		FAcCommandMapInfo::FRequest Q;
		Q.Id = R.id;
		if (const auto* U = R.what.as<ac::Request::What::Unit>()) Q.Icon = FAcConsoleInfo::IconName((int32)U->kind);
		else if (const auto* B = R.what.as<ac::Request::What::Building>()) Q.Icon = FAcConsoleInfo::StructureIconName((int32)B->kind);
		else if (const auto* Up = R.what.as<ac::Request::What::Upgrade>()) Q.Icon = FAcCommandCard::UpgradeIcon(Up->upgrade);
		Q.Title = Utf8(R.title()) + (R.site ? FString::Printf(TEXT(" at site %lld"), (long long)(*R.site + 1)) : FString());
		Q.Count = R.repeats ? FString(TEXT("∞")) : R.count > 1 ? FString::Printf(TEXT("×%lld"), (long long)R.count) : FString();
		const auto It = Requests.find(R.id);
		Q.Status = It != Requests.end() ? Utf8(It->second.text) : FString(TEXT("Next second"));
		Q.Funded = std::round((It != Requests.end() ? It->second.funded : 0.0) * 20) / 20;
		Info.Queue.Add(Q);
	}
	Info.Stance = D.stance;
	Info.Economy = TEXT("Prospectors: ") + FAcCommandCard::SplitName(D.harvest)
		+ (D.keepOre + D.keepHydrogen > 0
			? FString::Printf(TEXT(" · Keep %lld ore, %lld MH"), (long long)D.keepOre, (long long)D.keepHydrogen)
			: FString());
	return Info;
}

TArray<FAcBeaconSpec> FAcCommandMapLogic::Beacons(const ac::Simulation& Sim, const int64 Player)
{
	TArray<FAcBeaconSpec> Out;
	if (Player < 0 || Player >= (int64)Sim.state.players.size()) return Out;
	const ac::Directives D = Sim.state.players[(size_t)Player].orders();
	for (const ac::Objective& O : D.objectives) Out.Add({O.id, O.kind, O.at});
	for (const ac::Request& R : D.queue)
	{
		if (R.site && *R.site >= 0 && *R.site < (int64_t)Sim.sites.size()) Out.Add({R.id, {}, Sim.sites[(size_t)*R.site]});
	}
	return Out;
}

FString FAcCommandMapLogic::Describe(const ac::Command& C)
{
	if (const auto* X = C.as<ac::Command::Stance>())
		return FString::Printf(TEXT("stance(%lld, %s)"), (long long)X->player, *Utf8(std::string(ac::EnumInfo<ac::Stance>::names[(size_t)X->stance])));
	if (const auto* X = C.as<ac::Command::Objective>())
		return FString::Printf(TEXT("objective(%lld, %s, %.1f,%.1f)"), (long long)X->player,
			*Utf8(std::string(ac::EnumInfo<OKind>::names[(size_t)X->kind])), X->at.x, X->at.y);
	if (const auto* X = C.as<ac::Command::Request>())
		return FString::Printf(TEXT("request(%lld, site %lld)"), (long long)X->player, (long long)X->site.value_or(-1));
	if (const auto* X = C.as<ac::Command::CancelRequest>()) return FString::Printf(TEXT("cancelRequest(%lld, %lld)"), (long long)X->player, (long long)X->id);
	if (const auto* X = C.as<ac::Command::CancelObjective>())
		return FString::Printf(TEXT("cancelObjective(%lld, %lld)"), (long long)X->player, (long long)X->id);
	return TEXT("command");
}
