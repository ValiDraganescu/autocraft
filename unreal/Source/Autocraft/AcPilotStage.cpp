// The staged first-person stills (`windowshot --pilot SCENE`), chunk E2:
// the port of `GameController+PilotStage.swift` (`stagePilot`, `stageUnit`,
// `newUnit`, `nearestCliff`) for `AAcPilotPawn`'s `-AcPilot=KIND
// -AcPilotStage [-AcPilotVariant=V]`.
//
// Swift sets the scene up and steps the game by hand (1/30 s steps) before
// the picture. Here the set-up is the same, then the game runs exactly as
// many fixed steps (`UAcSimSubsystem::StepThenPause`, 1/60 s) and holds:
//   Settle  the set-up's step (Swift's one 1/30 step; a Prospector's three),
//           then a gun on foot's four aim rounds onto its target's chest
//           (E2's `ScriptAimTarget`, in `Follow`), 1/30 s each as Swift;
//   Act     "fire", "miss", "heal": the act key held. A gun steps one fixed
//           step at a time until its round leaves (a Shot or Missed event of
//           the driven unit, seen in `OnFrame`), so the tracer is drawn at
//           its start from the cockpit's muzzle, as Swift's
//           `world.shot(time: clock)`; a healer holds it 20 steps (Swift's
//           ten 1/30 steps) for the beam to take hold;
//   Done    the game holds; the shot is taken `-AcPilotFor` seconds later.
// Effects run on game time, so the held tracer stays where it left.
//
// The staged target holds its fire (cooldown 1 s): a shot of its own in
// the staging steps would stay on screen frozen as a tracer (Swift's first
// shot of the target lands unseen: its Ranger reads 39/45, this one 45/45).
#include "AcPilotPawn.h"

#include "AcLog.h"
#include "AcSimSubsystem.h"
#include "AcTerrain.h"
#include "AcWorldRenderer.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include "NavGrid.h"
#include "Pilot.h"
#include "Rules.h"
#include "Simulation.h"

#include <cmath>
#include <utility>

namespace
{
	/// The shortest cliff jump near `P`: the last walkable spot before a
	/// cliff face, and the first walkable one past it (`nearestCliff`, as
	/// `leap` scans).
	std::optional<std::pair<ac::Vec2, ac::Vec2>> NearestCliff(const ac::Simulation& S, const ac::Vec2 P)
	{
		if (!S.nav) return std::nullopt;
		const ac::NavGrid& Nav = *S.nav;
		std::optional<std::pair<ac::Vec2, ac::Vec2>> Best;
		double BestLen = 0;
		for (int32 K = 0; K < 64; ++K)
		{
			const double A = double(K) / 64 * 2 * UE_DOUBLE_PI;
			const ac::Vec2 D(std::cos(A), std::sin(A));
			ac::Vec2 From = P;
			double T = 0;
			while (T < 30 && Nav.walkable(From + D * 0.2)) { From = From + D * 0.2; T += 0.2; }
			// Across the blocked cells to the next walkable one: a jump if a
			// cliff face is among them (not a rock or the map's edge).
			ac::Vec2 To = From + D * 0.2;
			double Len = 0.2;
			bool bCliff = false;
			while (Len < 4 && !Nav.walkable(To)) { bCliff = bCliff || Nav.cliff(To); To = To + D * 0.2; Len += 0.2; }
			if (T >= 30 || !bCliff || !Nav.walkable(To) || (Best && T + Len >= BestLen)) continue;
			Best = std::make_pair(From, To);
			BestLen = T + Len;
		}
		return Best;
	}

	/// A unit as GameCore trains one, the next id taken (`newUnit`).
	ac::Unit NewUnit(ac::Simulation& S, const ac::UnitKind Kind, const int64 Owner, const ac::Vec2 At, const double Heading)
	{
		ac::Unit U = S.freshUnit(Kind, S.state.nextID, Owner, At, Heading);
		S.state.nextID += 1;
		return U;
	}

	ac::Unit* UnitById(ac::GameState& St, const int64 Id)
	{
		for (ac::Unit& U : St.units)
		{
			if (U.id == Id) return &U;
		}
		return nullptr;
	}
}

bool AAcPilotPawn::StageFor(const ac::UnitKind Kind)
{
	// As `GameController.stagePilot`/`stageUnit` (windowshot --pilot SCENE).
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	ac::Simulation& S = Sim->Simulation();
	const ac::Structure* Citadel = nullptr;
	for (const ac::Structure& B : S.state.structures)
	{
		if (B.owner == ac::Pilot::player && B.kind == ac::StructureKind::citadel) { Citadel = &B; break; }
	}
	if (!Citadel) return false;
	const ac::Vec2 Home = Citadel->position;
	Stage = EStage::None;
	bStageActs = bStageHeals = bStageFired = false;
	StageFrames = StageActSteps = StageAimSteps = 0;
	StageDoneAt = -1.0;
	StagedIds.Reset();
	StageLoad = 0;
	StageJump.Reset();
	StageCross.Reset();
	ScriptAimTarget.Reset();
	ScriptAimRounds = 0;
	if (!(Kind == ac::UnitKind::prospector ? StageProspector(S, Home) : StageUnit(S, Kind, Home))) return false;

	auto SetHeading = [&](const int64 Id, const double H)
	{
		if (ac::Unit* U = UnitById(S.state, Id)) { U->heading = H; U->aim.reset(); }
	};
	double Yaw = 0, Pitch = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("AcPilotYaw="), Yaw)) { PilotYaw = Yaw; SetHeading(DrivenId, Yaw); ScriptAimRounds = 0; }
	if (FParse::Value(FCommandLine::Get(), TEXT("AcPilotPitch="), Pitch)) { Cam.Pitch = Pitch; ScriptAimRounds = 0; }
	if (!bScriptStage)
	{
		ScriptAimRounds = 0;
		return true;
	}
	// A still: Swift's stage steps 1/30 s (a Prospector's 0.1 s) after the
	// set-up; the renderer's `Shown()` follows only a step.
	Stage = EStage::Settle;
	Sim->StepThenPause(Kind == ac::UnitKind::prospector ? 6 : 2);
	UE_LOG(LogAutocraft, Log, TEXT("pilot: staged %s%s%s (%s)"), UTF8_TO_TCHAR(std::string(ac::rawValue(Kind)).c_str()),
		StageVariant.IsEmpty() ? TEXT("") : TEXT("-"), *StageVariant,
		bStageActs ? (bStageHeals ? TEXT("heals") : TEXT("fires")) : TEXT("holds"));
	return true;
}

bool AAcPilotPawn::StageProspector(ac::Simulation& S, const ac::Vec2 Home)
{
	// The player's first Prospector by the ore field nearest its Citadel
	// ("ore", "mining"), walking home with a load ("citadel"), on the open
	// side facing the enemy's Citadel ("enemy"), or off to one side
	// facing the field ("wide").
	ac::GameState& St = S.state;
	const ac::OreDeposit* Patch = nullptr;
	for (const ac::OreDeposit& P : St.patches)
	{
		if (P.remaining > 0 && (!Patch || ac::distance(P.position, Home) < ac::distance(Patch->position, Home))) Patch = &P;
	}
	ac::Unit* Me = nullptr;
	for (ac::Unit& U : St.units)
	{
		if (U.owner == ac::Pilot::player && U.kind == ac::UnitKind::prospector) { Me = &U; break; }
	}
	if (!Patch || !Me) return false;
	const ac::Vec2 Field = Patch->position;
	const ac::Vec2 Away = ac::normalize(Field - Home);
	std::optional<ac::Vec2> Face;
	if (StageVariant == TEXT("citadel"))
	{
		Me->position = Home - Away * 7.5;
		// The load goes aboard once the cockpit has posed (`StageTick`):
		// Swift's `takeOver` poses the fork before `carrying = 5`, and its
		// render clock stands still after, so its fork stays shut and empty.
		Me->carrying = 0;
		StageLoad = 5;
		Face = Home;
	}
	else if (StageVariant == TEXT("enemy"))
	{
		Me->position = Home - Away * (ac::Rules::radius(ac::StructureKind::citadel) + 3);
		for (const ac::Structure& B : St.structures)
		{
			if (B.owner != ac::Pilot::player && B.kind == ac::StructureKind::citadel) { Face = B.position; break; }
		}
	}
	else if (StageVariant == TEXT("wide"))
	{
		Me->position = Home - Away * 6 + ac::Vec2(Away.y, -Away.x) * 3;
		Face = Field;
	}
	else if (StageVariant == TEXT("line"))
	{
		// Off to the side of the field's near edge, looking along it: the ore
		// line and the miners between it and the Citadel in view.
		double Back = 2.5, Aside = 7.0;
		FParse::Value(FCommandLine::Get(), TEXT("AcPilotLineBack="), Back);
		FParse::Value(FCommandLine::Get(), TEXT("AcPilotLineSide="), Aside);
		Me->position = Field - Away * Back + ac::Vec2(Away.y, -Away.x) * Aside;
		Face = Field - Away * Back;
	}
	else
	{
		// At a field's edge, facing it ("ore"; "mining" drills: E6's -AcPilotRun).
		Me->position = Field - Away * 1.35;
		Face = Field;
	}
	// -AcPilotMiners=N (a test aid): N more of the player's Prospectors set to
	// mine the patches nearest the Citadel, walking in and out of the mining
	// glow (the ghosting of moving units, GAME-LAYER.md Progress).
	if (int32 Miners = 0; FParse::Value(FCommandLine::Get(), TEXT("AcPilotMiners="), Miners) && Miners > 0)
	{
		std::vector<int64> Near;
		for (int64 I = 0; I < (int64)St.patches.size(); ++I)
		{
			if (St.patches[(size_t)I].remaining > 0 && ac::distance(St.patches[(size_t)I].position, Field) < 6.0) Near.push_back(I);
		}
		const ac::Vec2 Side(Away.y, -Away.x);
		for (int32 K = 0; K < Miners && !Near.empty(); ++K)
		{
			const ac::Vec2 At = Field - Away * (2.5 + (K % 3) * 1.2) + Side * (double(K / 3) - double(Miners / 3) * 0.5) * 1.6;
			ac::Unit W = NewUnit(S, ac::UnitKind::prospector, ac::Pilot::player, At, std::atan2(Away.y, Away.x));
			W.task = ac::Unit::Task::toPatch;
			W.patch = Near[(size_t)K % Near.size()];
			St.units.push_back(W);
		}
		// The push may have moved the units: find the driven one again.
		Me = nullptr;
		for (ac::Unit& U : St.units)
		{
			if (U.owner == ac::Pilot::player && U.kind == ac::UnitKind::prospector) { Me = &U; break; }
		}
	}
	Me->task = ac::Unit::Task::idle;
	const int64 Id = Me->id;
	double Yaw = Me->heading;
	if (Face)
	{
		const ac::Vec2 D = *Face - Me->position;
		Yaw = std::atan2(D.y, D.x);
	}
	Me->heading = Yaw;
	// -AcPilotWalker (a test aid): a second Prospector of the player's walks
	// across the view to the ore, to look at how moving units draw (motion
	// vectors, TSR).
	if (FParse::Param(FCommandLine::Get(), TEXT("AcPilotWalker")))
	{
		const ac::Vec2 Fwd(std::cos(Yaw), std::sin(Yaw));
		const ac::Vec2 Left(-Fwd.y, Fwd.x);
		double Side = 6.0, Ahead = 4.0;
		FParse::Value(FCommandLine::Get(), TEXT("AcPilotWalkerSide="), Side);
		FParse::Value(FCommandLine::Get(), TEXT("AcPilotWalkerAhead="), Ahead);
		ac::Unit W = NewUnit(S, ac::UnitKind::prospector, ac::Pilot::player, Me->position + Fwd * Ahead - Left * Side, Yaw);
		W.task = ac::Unit::Task::toPatch;
		W.patch = int64(Patch - St.patches.data());
		St.units.push_back(W);
		Me = nullptr;
	}
	if (!TakeOver(Id)) return false;
	PilotYaw = Yaw;
	S.pilot->heading = Yaw;
	S.lookNow();
	return true;
}

bool AAcPilotPawn::StageUnit(ac::Simulation& S, const ac::UnitKind Kind, const ac::Vec2 Home)
{
	// A new unit of the player's on its Citadel's open side, facing out, a
	// Ranger ahead in reach: an enemy for a gun, a hurt one of its own for
	// a healer; an enemy Kestrel for a gun that hits only flyers.
	ac::GameState& St = S.state;
	// "minigun" among "+"-joined variants: the player's side has the Mini gun.
	TArray<FString> Mods;
	StageVariant.ParseIntoArray(Mods, TEXT("+"));
	if (Mods.Remove(TEXT("minigun")) > 0)
	{
		ac::Player& P = St.players[ac::Pilot::player];
		if (!P.upgrades) P.upgrades.emplace();
		P.upgrades->insert(ac::Upgrade::minigun);
	}
	const FString Variant = Mods.Num() > 0 ? Mods[0] : FString();

	ac::Vec2 Sum(0, 0);
	int32 N = 0;
	for (const ac::OreDeposit& P : St.patches)
	{
		if (ac::distance(P.position, Home) < 12) { Sum = Sum + P.position; ++N; }
	}
	if (N == 0) return false;
	ac::Vec2 Out = ac::normalize(Home - Sum / double(N));
	ac::Vec2 At = Home + Out * (ac::Rules::radius(ac::StructureKind::citadel) + 3);
	ac::Unit Me = NewUnit(S, Kind, ac::Pilot::player, At, std::atan2(Out.y, Out.x));
	const bool bHeals = !S.pilotShoots(Me);
	// A gun that hits only flyers (a Hailstorm's flak) gets an enemy Kestrel.
	// A Kestrel's "air" variant: its rail gun's flyer ahead.
	const bool bFlyer = !bHeals && (!S.weapon(Me).hitsGround || Variant == TEXT("air"));
	std::optional<std::pair<ac::Vec2, ac::Vec2>> Jump;
	if (Variant == TEXT("anchored") || Variant == TEXT("close")) { Me.anchor = 1.0; Me.anchored = true; }
	else if (Variant == TEXT("anchoring")) { Me.anchor = 0.45; Me.anchored = true; }
	else if (Variant == TEXT("jump"))
	{
		// Taken over at the edge (not mid-jump: `take` refuses that), then
		// put half way across.
		Jump = NearestCliff(S, At);
		if (!Jump) return false;
		Out = ac::normalize(Jump->second - Jump->first);
		At = Jump->first + (Jump->second - Jump->first) * 0.45;
		Me.position = Jump->first;
		Me.heading = std::atan2(Out.y, Out.x);
	}
	const ac::Vec2 Side(-Out.y, Out.x);
	/// A Ranger (the player's, else the enemy's) at `P`, facing back; it
	/// holds its fire through the staging steps.
	auto Ranger = [&](const bool bMine, const ac::Vec2 P, const TFunction<void(ac::Unit&)>& Set = nullptr) -> int64
	{
		ac::Unit V = NewUnit(S, bFlyer && !bMine ? ac::UnitKind::kestrel : ac::UnitKind::ranger, bMine ? ac::Pilot::player : 1, P,
			std::atan2(-Out.y, -Out.x));
		V.cooldown = 1.0;
		if (Set) Set(V);
		St.units.push_back(V);
		StagedIds.Add(V.id);
		return V.id;
	};
	// Who stands ahead: the target of "fire".
	std::optional<int64> Ahead;
	if (Variant == TEXT("cargo"))
	{
		auto Aboard = [](ac::Unit& V) { V.task = ac::Unit::Task::aboard; };
		const int64 A = Ranger(true, At, Aboard), B = Ranger(true, At, Aboard);
		Me.cargo = std::vector<int64_t>{A, B};
		Ahead = Ranger(true, At + Out * 0.4);
	}
	else if (Variant != TEXT("jump"))
	{
		// Edge to edge: out of reach by 2 (inside sight) for "far" and a
		// gun's "miss", 1.4 for "close" (inside an anchored Longbow's
		// minimum range of 2); else centre to centre, in reach (a healer's
		// patient under its sight as it looks down).
		const double Reach = bHeals ? ac::Rules::healRange : S.weapon(Me).range;
		const double Radii = ac::Rules::radius(Kind) + ac::Rules::radius(bFlyer ? ac::UnitKind::kestrel : ac::UnitKind::ranger);
		const bool bAway = Variant == TEXT("far") || (Variant == TEXT("miss") && !bHeals);
		const double D = bAway ? FMath::Min(Reach + 2, ac::Rules::sight - 0.3) + Radii
			: Variant == TEXT("close") ? 1.4 + Radii : bHeals ? 3.5 : 4.5;
		const bool bHurt = bHeals && Variant != TEXT("miss");
		Ahead = Ranger(bHeals, At + Out * D + Side * (bHeals ? 0.2 : 0.4), [bHurt](ac::Unit& V) { if (bHurt) V.hp = 18; });
	}
	const int64 MeId = Me.id;
	St.units.push_back(Me);
	// Not the driven unit: Swift's is hidden and not posed again after
	// its first sync, so its eye stays at the lift-off's start (as E2's).
	// Set up by hand: the sight sees the new places now.
	S.lookNow();
	if (!TakeOver(MeId)) return false;
	if (Jump)
	{
		// Put half way across in `StageTick`, once the view has stood at
		// the edge (Swift's `takeOver` places its camera there, and the
		// stage's crosshair is cast from it).
		StageJump = TPair<ac::Vec2, ac::Vec2>(Jump->first, Jump->second);
		StageJumpAt = At;
	}
	const double Yaw = std::atan2(Out.y, Out.x);
	PilotYaw = Yaw;
	S.pilot->heading = Yaw;
	if (ac::Unit* U = UnitById(St, MeId)) { U->heading = Yaw; U->aim.reset(); }
	// A gun on foot turns its view onto the target's chest first (its shots
	// go where the crosshair is): `Follow` does it a few rounds.
	if (!bHeals && ac::Pilot::strafes(Kind) && Ahead)
	{
		ScriptAimTarget = *Ahead;
		ScriptAimRounds = 4;
		// Swift steps the game 1/30 s each aim round (the target may walk).
		StageAimSteps = 4;
	}
	bStageActs = Variant == TEXT("fire") || Variant == TEXT("miss") || Variant == TEXT("heal") || Variant == TEXT("air");
	bStageHeals = bHeals;
	return true;
}

void AAcPilotPawn::StageLifted()
{
	UAcWorldRenderer* R = UAcWorldRenderer::Get(this);
	if (!R) return;
	for (const int64 Id : StagedIds)
	{
		if (FAcUnitMemory* M = R->Memory(Id)) M->BornAt.Reset();
	}
}

bool AAcPilotPawn::StageTick(UAcSimSubsystem& Sim)
{
	if (Stage != EStage::None) StageLifted();
	switch (Stage)
	{
	case EStage::None:
		return true;
	case EStage::Settle:
		// The set-up's steps done and the aim rounds turned (they run in
		// `Follow`, a frame each), a frame more for the crosshair to follow.
		if (!Sim.IsPaused()) return false;
		if (StageLoad > 0)
		{
			// "citadel": the load aboard now, the game held (the fork's
			// clock stands still, as Swift's render clock).
			for (ac::Unit& U : Sim.Simulation().state.units)
			{
				if (U.id == DrivenId) U.carrying = StageLoad;
			}
			StageLoad = 0;
		}
		if (StageJump)
		{
			// "jump": the crosshair as cast from the edge (kept), then half
			// way across, and Swift's 1/30 s step.
			StageCross = Cross;
			if (ac::Unit* U = UnitById(Sim.Simulation().state, DrivenId))
			{
				U->position = StageJumpAt;
				U->jumpFrom = StageJump->Key;
				U->jumpTo = StageJump->Value;
			}
			StageJump.Reset();
			Sim.StepThenPause(2);
			return false;
		}
		if (StageAimSteps > 0)
		{
			// An aim round: 1/30 s on, the view kept on the target meanwhile.
			--StageAimSteps;
			if (ScriptAimTarget) ScriptAimRounds = FMath::Max(ScriptAimRounds, 2);
			Sim.StepThenPause(2);
			return false;
		}
		if (ScriptAimRounds > 0 || ++StageFrames < 3) return false;
		if (!bStageActs)
		{
			Stage = EStage::Done;
			StageDoneAt = ScriptStart;
			// -AcPilotWalker: the game runs on from here (the shot waits -AcPilotFor).
			if (FParse::Param(FCommandLine::Get(), TEXT("AcPilotWalker")) || FParse::Param(FCommandLine::Get(), TEXT("AcPilotLive"))) Sim.SetPaused(false);
			return true;
		}
		Stage = EStage::Act;
		bHold = true;
		StageActSteps = bStageHeals ? 20 : 1;
		Sim.StepThenPause(StageActSteps);
		UE_LOG(LogAutocraft, Log, TEXT("pilot: stage acts (%s), crosshair %lld"), bStageHeals ? TEXT("held 20 steps") : TEXT("a step at a time"),
			Cross ? (long long)Cross->Target : -2ll);
		return false;
	case EStage::Act:
		if (!Sim.IsPaused()) return false;
		if (!bStageHeals && !bStageFired && StageActSteps < 12)
		{
			// Not yet: one step more (a gun still cooling, a flyer's lock).
			++StageActSteps;
			Sim.StepThenPause(1);
			return false;
		}
		UE_LOG(LogAutocraft, Log, TEXT("pilot: stage %s after %d steps"),
			bStageHeals ? TEXT("healed") : bStageFired ? TEXT("fired") : TEXT("did not fire"), StageActSteps);
		for (const ac::Unit& U : Sim.Simulation().state.units)
		{
			if (U.id > DrivenId - 4 && U.id != DrivenId)
			{
				UE_LOG(LogAutocraft, Log, TEXT("pilot: stage #%lld at %.2f,%.2f heading %.3f hp %.0f task %d"), (long long)U.id, U.position.x,
					U.position.y, U.heading, U.hp, int32(U.task));
			}
		}
		Stage = EStage::Done;
		StageDoneAt = ScriptStart;
		return true;
	case EStage::Done:
		return true;
	}
	return true;
}

bool AAcPilotPawn::StageAt(const ac::UnitKind Kind, const FString& Where)
{
	// Put down already (before a dive), else now; then taken over.
	if (PlacedId == INDEX_NONE) PlacedId = PlaceAt(Kind, Where);
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	ac::Simulation& S = Sim->Simulation();
	// Back where it was put and how it faced (an idle unit gets nudged
	// while the top-down view runs before a dive).
	if (ac::Unit* U = UnitById(S.state, PlacedId)) { U->position = PlacedAt; U->heading = PlacedHeading; U->aim.reset(); }
	if (PlacedId == INDEX_NONE || !TakeOver(PlacedId)) return false;
	PilotYaw = PlacedHeading;
	if (S.pilot) S.pilot->heading = PlacedHeading;
	return true;
}

int64 AAcPilotPawn::PlaceAt(const ac::UnitKind Kind, const FString& Where)
{
	// -AcPilotAt: a fresh unit of the player's at a staged place (a video's
	// opening); -AcPilotVariant=anchored puts a Longbow down anchored.
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	ac::Simulation& S = Sim->Simulation();
	ac::GameState& St = S.state;
	double Heading = 0;
	FParse::Value(FCommandLine::Get(), TEXT("AcPilotYaw="), Heading);
	ac::Unit Me = NewUnit(S, Kind, ac::Pilot::player, ac::Vec2(0, 0), Heading);
	if (Where == TEXT("enemy") || Where == TEXT("slope"))
	{
		// The nearest enemy Citadel to the player's own.
		ac::Vec2 Home(0, 0);
		for (const ac::Structure& B : St.structures)
		{
			if (B.owner == ac::Pilot::player && B.kind == ac::StructureKind::citadel) { Home = B.position; break; }
		}
		const ac::Structure* Foe = nullptr;
		for (const ac::Structure& B : St.structures)
		{
			if (B.kind == ac::StructureKind::citadel && St.hostile(B.owner, ac::Pilot::player)
				&& (!Foe || ac::distance(B.position, Home) < ac::distance(Foe->position, Home))) Foe = &B;
		}
		if (!Foe || !S.field) return INDEX_NONE;
		// The approach whose ground falls the most toward the Citadel over
		// its first 6 cells, open all the way to 12 cells out (an anchored
		// Longbow's reach).
		const ac::Vec2 C = Foe->position;
		double Best = -1e9;
		std::optional<ac::Vec2> BestAt;
		for (int32 K = 0; K < 96; ++K)
		{
			const double A = double(K) / 96 * 2 * UE_DOUBLE_PI;
			const ac::Vec2 Out(std::cos(A), std::sin(A));
			for (double R = 20; R <= 26; R += 1)
			{
				const ac::Vec2 P = C + Out * R;
				bool bOpen = true;
				for (double T = 0; T <= R - 12 && bOpen; T += 0.25) bOpen = S.pilotCanStand(Me, P - Out * T);
				if (!bOpen) continue;
				const double Drop = S.field->height(P) - S.field->height(P - Out * 6);
				if (Drop > Best) { Best = Drop; BestAt = P; }
			}
		}
		ac::Vec2 In = BestAt ? ac::normalize(C - *BestAt) : ac::Vec2(1, 0);
		if (Where == TEXT("slope") && S.map)
		{
			// The player's own ramp (its top nearest the player's Citadel),
			// the way out toward the enemy: on its top, a cell and a half
			// back from where it starts down, facing down it.
			const ac::Ramp* Ramp = nullptr;
			for (const ac::Ramp& R : S.map->ramps)
			{
				if (!Ramp || ac::distance(R.high, Home) < ac::distance(Ramp->high, Home)) Ramp = &R;
			}
			if (!Ramp) return INDEX_NONE;
			In = ac::normalize(Ramp->low - Ramp->high);
			BestAt = Ramp->high - In * 1.5;
			Best = S.field->height(*BestAt) - S.field->height(*BestAt + In * 6);
			UE_LOG(LogAutocraft, Log, TEXT("pilot: -AcPilotAt=slope: %d ramps; this one %.2f,%.2f (level %lld) down to %.2f,%.2f (level %lld), %.1f wide, levels %.2f apart"),
				int32(S.map->ramps.size()), Ramp->high.x, Ramp->high.y, (long long)Ramp->highLevel, Ramp->low.x, Ramp->low.y,
				(long long)Ramp->lowLevel, Ramp->width, S.map->levelHeight);
		}
		if (!BestAt) return INDEX_NONE;
		Me.position = *BestAt;
		Heading = std::atan2(In.y, In.x);
		UE_LOG(LogAutocraft, Log, TEXT("pilot: -AcPilotAt=%s: at %.2f,%.2f heading %.3f, the ground falls %.2f over 6 cells ahead; the Citadel at %.2f,%.2f (player %lld), %.1f cells off"),
			*Where, Me.position.x, Me.position.y, Heading, Best, C.x, C.y, (long long)Foe->owner, ac::distance(Me.position, C));
	}
	else
	{
		FString X, Y;
		if (!Where.Split(TEXT(","), &X, &Y)) return INDEX_NONE;
		Me.position = ac::Vec2(FCString::Atod(*X), FCString::Atod(*Y));
	}
	Me.heading = Heading;
	if (StageVariant == TEXT("anchored") && Me.anchor) { Me.anchor = 1.0; Me.anchored = true; }
	PlacedAt = Me.position;
	PlacedHeading = Heading;
	St.units.push_back(Me);
	S.lookNow();
	return Me.id;
}
