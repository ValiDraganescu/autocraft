// Who fires what, from which muzzle: the port of `GameScene.shot`,
// `missed`, `fire` and `blast` (GameScene.swift:1164-1365), with
// `GameScene.tracers`/`grenades` (:1503/:1515), `Longbow.tankRounds`
// (Models+Longbow.swift:681) and `Firefly.flameLine`
// (Models+Firefly.swift:850). Swift numbers are cells; × `Cm` here. A
// SceneKit offset (dx, dy up, dz) is UE (dx, dz, dy).
//
// Muzzles are the exported parts the Swift `Trooper.muzzles` lists (the
// manifest's handles), read from `UAcWorldRenderer::PartWorld` as this
// frame posed them; a round fired a beat later (a second barrel, a pod)
// reads them again then, as Swift's `worldPosition` closures do.
#include "AcEffects.h"
#include "AcAudioDirector.h"

#include "AcLog.h"
#include "AcModelCatalog.h"
#include "AcNewKinds.h"
#include "AcPose.h"
#include "AcPoseNewKinds.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"
#include "AcWorldRenderer.h"

#include "Rules.h"
#include "Simulation.h"
#include "Types.h"

#include "Engine/StaticMesh.h"

#include <cmath>

namespace
{
	constexpr double Cm = AcSpace::CmPerCell;

	/// `GameScene.Building.radius` (`makeBuilding`, GameScene.swift:370-420).
	double BuildingRadius(const ac::StructureKind K)
	{
		switch (K)
		{
		case ac::StructureKind::citadel: return 2.5;
		case ac::StructureKind::garrison:
		case ac::StructureKind::bastion:
		case ac::StructureKind::derrick:
		case ac::StructureKind::foundry:
		case ac::StructureKind::spacedock: return 1.5;
		case ac::StructureKind::lab:
		case ac::StructureKind::habDome:
		case ac::StructureKind::sentinel: return 1.0;
		}
		return 1.0;
	}

	/// `Trooper.muzzles` (GameScene.swift:1646) as exported part names. The
	/// Rhino Longbow's two prongs share one muzzle (`[muzzle, muzzle]`).
	TArray<FName> MuzzleParts(const ac::UnitKind K, const bool bMinigun)
	{
		switch (K)
		{
		case ac::UnitKind::ranger: return {bMinigun ? FName(TEXT("minigun_flash")) : FName(TEXT("flash"))};
		case ac::UnitKind::comet: return {TEXT("flashes_0"), TEXT("flashes_1")};
		case ac::UnitKind::juggernaut: return {TEXT("muzzles_0"), TEXT("muzzles_1")};
		case ac::UnitKind::firefly: return {TEXT("nozzle")};
		case ac::UnitKind::longbow: return {TEXT("tankMuzzles_0"), TEXT("tankMuzzles_0"), TEXT("anchorMuzzle")};
		case ac::UnitKind::dropship: return {TEXT("beamOrigin")};
		case ac::UnitKind::kestrel: return {TEXT("muzzles_0"), TEXT("muzzles_1")};
		case ac::UnitKind::hailstorm: return {TEXT("muzzles_0"), TEXT("muzzles_1"), TEXT("muzzles_2"), TEXT("muzzles_3")};
		// The four rails (a volley leaves a pair of them), the two cannons, the launcher.
		case ac::UnitKind::peregrine: return {TEXT("missiles_0"), TEXT("missiles_1"), TEXT("missiles_2"), TEXT("missiles_3")};
		case ac::UnitKind::atlas: return {TEXT("cannons_0"), TEXT("cannons_1")};
		case ac::UnitKind::scorpion: return {TEXT("launcher")};
		case ac::UnitKind::prospector: return {};
		}
		return {};
	}

	/// A Bastion's firing slits (`Models.bastion`, Models.swift:505-540):
	/// the front five of eight faces, i = 0, 1, 2, 6, 7, at a = i·π/4
	/// around SceneKit Y (0 = +Z, the front), in the `flashes` order.
	constexpr double SlitAngles[5] = {0, UE_DOUBLE_PI / 4, UE_DOUBLE_PI / 2, 6 * UE_DOUBLE_PI / 4, 7 * UE_DOUBLE_PI / 4};

	/// Which prong leads a tank's shot at `Shot` (`Longbow.leadBarrel`).
	int32 LeadBarrel(const double Shot) { return int32(std::llround(Shot * 1000)) & 1; }

	/// A Ranger whose side has the Mini gun fires it (`Ranger.arm(minigun:)`).
	bool HasMinigun(const ac::GameState& State, const ac::Unit& U)
	{
		if (U.kind != ac::UnitKind::ranger || U.owner < 0 || U.owner >= (int64)State.players.size()) return false;
		const auto& Ups = State.players[(size_t)U.owner].upgrades;
		return Ups && Ups->count(ac::Upgrade::minigun) > 0;
	}

	const ac::GameState* StateOf(const FAcFrame* Frame, const UAcSimSubsystem* Sim)
	{
		if (Frame && Frame->State) return Frame->State;
		return Sim ? &Sim->State() : nullptr;
	}
}

/// The shooter as `GameScene.fire` sees it.
struct UAcEffects::FShooter
{
	int64 Unit = 0;
	ac::UnitKind Kind = ac::UnitKind::ranger;
	/// The drawn root's ground point (`trooper.root.position`), or a
	/// Bastion's centre for its crew.
	ac::Vec2 Position;
	/// Muzzles at the shot (world cm), in `Trooper.muzzles` order.
	TArray<FVector> Muzzles;
	/// The parts to read them from again later (empty: keep `Muzzles`, the
	/// cockpit's guns or a Bastion slit).
	TArray<FName> Parts;
	bool bHidden = false;
	bool bDriven = false;
	bool bMinigun = false;
	double Anchor = 0.0;
	/// A Kestrel's target is a flyer: its rail gun, not its rockets.
	bool bAirTarget = false;
	/// The muzzles are where the part's meshes end (a new kind's barrels, rails
	/// and launcher), not its pivot.
	bool bReach = false;
};

bool UAcEffects::PartLocation(const int64 Id, const FName Part, FVector& Out) const
{
	const UAcWorldRenderer* R = Renderer();
	const FAcModelInfo* M = R ? R->ModelOf(Id) : nullptr;
	if (!M) return false;
	const int32* I = M->PartIndex.Find(Part);
	const TConstArrayView<FTransform> W = R->PartWorld(Id);
	if (!I || !W.IsValidIndex(*I)) return false;
	Out = W[*I].GetLocation();
	return true;
}

bool UAcEffects::PartMuzzle(const int64 Id, const FName Part, FVector& Out) const
{
	const UAcWorldRenderer* R = Renderer();
	const FAcModelInfo* M = R ? R->ModelOf(Id) : nullptr;
	if (!M) return false;
	const int32* I = M->PartIndex.Find(Part);
	const TConstArrayView<FTransform> W = R->PartWorld(Id);
	if (!I || !W.IsValidIndex(*I)) return false;
	// How far the part's meshes reach along its +X (the barrel's length, the
	// rail's missile), read once from the meshes' bounds.
	static TMap<TPair<const FAcModelInfo*, int32>, double> Reach;
	double& Cm1 = Reach.FindOrAdd({M, *I}, -1.0);
	if (Cm1 < 0.0)
	{
		Cm1 = 0.0;
		for (const FAcModelMesh& Mesh : M->Parts[*I].Meshes)
		{
			if (const UStaticMesh* SM = Mesh.LoadMesh()) Cm1 = FMath::Max(Cm1, double(SM->GetBoundingBox().Max.X));
		}
	}
	const FTransform& T = W[*I];
	Out = T.GetLocation() + T.GetRotation().RotateVector(FVector(Cm1 * T.GetScale3D().X, 0.0, 0.0));
	return true;
}

TOptional<FVector> UAcEffects::AirBody(const int64 Id) const
{
	// `Trooper.airBody`: a Dropship's or a Kestrel's hull, over its root.
	const ac::GameState* State = StateOf(Frame, Sim());
	if (!State) return {};
	const std::optional<ac::Unit> U = State->unit(Id);
	if (!U || !ac::Rules::stats(U->kind).air) return {};
	FVector P;
	if (PartLocation(Id, TEXT("body"), P)) return P;
	return {};
}

void UAcEffects::Shot(const int64 Unit, const int64 Target, const ac::Vec2 At, const double Time)
{
	++Count.Shots;
	UAcWorldRenderer* R = Renderer();
	const ac::GameState* State = StateOf(Frame, Sim());
	if (!R || !State || !R->ModelOf(Unit)) return;  // not drawn: no rounds (`troops[unit]`)
	if (bLogShots)
	{
		UE_LOG(LogAutocraft, Log, TEXT("effects: shot #%lld -> #%lld at %.1f,%.1f t %.2f"), Unit, Target, At.x, At.y, Time);
	}

	const std::optional<ac::Unit> U = State->unit(Unit);
	if (!U)
	{
		const std::optional<ac::Structure> St = State->structure(Unit);
		if (St && St->kind == ac::StructureKind::sentinel)
		{
			// A Sentinel's missiles, a pair from each pod in turn, at the
			// flyer's hull (they hit at once: fast streaks and a burst).
			SlitShotTimes.FindOrAdd(Unit) = {Time};
			const FVector To = AirBody(Target).Get(AcSpace::ToWorld(At, GroundZ(At) / Cm + 2));
			for (int32 K = 0; K < 4; ++K)
			{
				FVector From;
				if (!PartLocation(Unit, FName(*FString::Printf(TEXT("muzzles_%d"), K)), From)) continue;
				TWeakObjectPtr<UAcEffects> Weak(this);
				After(K * 0.06, Time, [Weak, From, To, Time, K]()
				{
					if (UAcEffects* E = Weak.Get()) E->Slug(From, To, Time + K * 0.06);
				});
			}
		}
		return;
	}
	if (U->kind == ac::UnitKind::prospector)
	{
		// A Prospector's cutter: a spark on the target's near side.
		const double Y = GroundZ(At) / Cm + (State->structure(Target) ? 0.9 : 0.6);
		CutterSpark(AcSpace::ToWorld(At, Y), Time);
		return;
	}

	FShooter S;
	S.Unit = Unit;
	S.Kind = U->kind;
	S.bHidden = Unit == PilotFire.HiddenUnit;
	S.bDriven = Unit == PilotFire.DrivenUnit;
	S.Anchor = U->anchor.value_or(0.0);
	if (const FAcUnitMemory* Mem = R->Memory(Unit)) S.Anchor = FMath::Max(S.Anchor, Mem->AnchorOf);
	S.bMinigun = HasMinigun(*State, *U);
	S.bReach = AcNewKinds::IsNew(U->kind);
	if (const std::optional<ac::Unit> TA = State->unit(Target)) S.bAirTarget = ac::Rules::stats(TA->kind).air;
	const TConstArrayView<FTransform> W = R->PartWorld(Unit);
	S.Position = W.Num() > 0 ? AcSpace::ToSim(W[0].GetLocation()) : U->position;
	// The driven unit's rounds leave its cockpit's guns, not the hidden model's.
	if (S.bHidden && PilotFire.Muzzles.Num() > 0)
	{
		S.Muzzles = PilotFire.Muzzles;
	}
	else
	{
		S.Parts = MuzzleParts(U->kind, S.bMinigun);
		for (const FName& P : S.Parts)
		{
			FVector L;
			const bool bFound = S.bReach ? PartMuzzle(Unit, P, L) : PartLocation(Unit, P, L);
			S.Muzzles.Add(bFound ? L : AcSpace::ToWorld(S.Position, GroundZ(S.Position) / Cm + 0.6));
		}
	}
	// A Ranger in a Bastion fires from the slit facing the target.
	if (U->task == ac::Unit::Task::inBastion && U->structure)
	{
		const int64 Bid = *U->structure;
		const std::optional<ac::Structure> B = State->structure(Bid);
		const TConstArrayView<FTransform> BW = R->PartWorld(Bid);
		if (B && B->kind == ac::StructureKind::bastion && BW.Num() > 0)
		{
			const ac::Vec2 C = AcSpace::ToSim(BW[0].GetLocation());
			const double Want = std::atan2(At.x - C.x, At.y - C.y);
			int32 K = 0;
			for (int32 I = 1; I < 5; ++I)
			{
				if (std::abs(std::remainder(SlitAngles[I] - Want, 2 * UE_DOUBLE_PI))
					< std::abs(std::remainder(SlitAngles[K] - Want, 2 * UE_DOUBLE_PI)))
					K = I;
			}
			TArray<double>& Shots = SlitShotTimes.FindOrAdd(Bid);
			if (Shots.Num() != 5) Shots.Init(-10.0, 5);
			Shots[K] = Time;
			// The slit's flash: the slit at 1.2 out and 0.9 up, the flash
			// 0.14 further out along the face's normal.
			const double A = SlitAngles[K];
			const FVector Local = AcSpace::FromSceneKit(std::sin(A) * 1.34, 0.9, std::cos(A) * 1.34);
			S.Muzzles = {BW[0].TransformPosition(Local)};
			S.Parts.Reset();
			S.Position = C;
		}
	}

	const double Ground = GroundZ(At);
	FVector To;
	double Height = 0.0;
	const std::optional<ac::Structure> TB = State->structure(Target);
	if (TB)
	{
		const double Radius = BuildingRadius(TB->kind);
		const ac::Vec2 D = S.Position - At;
		const double Len = FMath::Max(ac::length(D), 1e-6);
		const ac::Vec2 Jitter = ac::Vec2(std::sin(Time * 71 + double(Unit)), std::cos(Time * 53 + double(Unit))) * 0.5;
		const ac::Vec2 P = At + D / Len * Radius * 0.85 + Jitter;
		Height = AcPose::BuildingHeight(TB->kind);
		To = AcSpace::ToWorld(P, Ground / Cm + 0.5 + 0.5 * Height * (0.5 + 0.5 * std::sin(Time * 37 + double(Unit))));
	}
	else if (const TOptional<FVector> Air = AirBody(Target))
	{
		To = *Air;
	}
	else
	{
		To = AcSpace::ToWorld(At, Ground / Cm + 0.62);
	}
	// The driven gun's round lands where its crosshair is on the target.
	if (S.bDriven && Target == PilotFire.AimOn && PilotFire.Aim) To = *PilotFire.Aim;

	// Grenades and shells home on a unit as it moves, as the damage does.
	TFunction<TOptional<FVector>()> Track;
	if (!TB && R->ModelOf(Target))
	{
		const std::optional<ac::Unit> TU = State->unit(Target);
		const bool bAir = TU && ac::Rules::stats(TU->kind).air;
		const TConstArrayView<FTransform> TW = R->PartWorld(Target);
		const double Lift = TW.Num() > 0 ? To.Z - TW[0].GetLocation().Z : 0.0;
		TWeakObjectPtr<UAcEffects> Weak(this);
		Track = [Weak, Target, Lift, bAir]() -> TOptional<FVector>
		{
			const UAcEffects* E = Weak.Get();
			const UAcWorldRenderer* WR = E ? E->Renderer() : nullptr;
			if (!WR || !WR->ModelOf(Target)) return {};
			if (bAir)
			{
				FVector P;
				return E->PartLocation(Target, TEXT("body"), P) ? TOptional<FVector>(P) : TOptional<FVector>();
			}
			const TConstArrayView<FTransform> Now = WR->PartWorld(Target);
			if (Now.Num() == 0) return {};
			return Now[0].GetLocation() + FVector(0, 0, Lift);
		};
	}
	Fire(S, To, At, Height, Time, MoveTemp(Track));
}

void UAcEffects::Missed(const int64 Unit, const FVector& To, const double Time)
{
	UAcWorldRenderer* R = Renderer();
	const ac::GameState* State = StateOf(Frame, Sim());
	if (!R || !State || !R->ModelOf(Unit)) return;
	const std::optional<ac::Unit> U = State->unit(Unit);
	if (!U) return;
	FShooter S;
	S.Unit = Unit;
	S.Kind = U->kind;
	S.bHidden = Unit == PilotFire.HiddenUnit;
	S.bDriven = Unit == PilotFire.DrivenUnit;
	S.Anchor = U->anchor.value_or(0.0);
	if (const FAcUnitMemory* Mem = R->Memory(Unit)) S.Anchor = FMath::Max(S.Anchor, Mem->AnchorOf);
	S.bMinigun = HasMinigun(*State, *U);
	S.bReach = AcNewKinds::IsNew(U->kind);
	const TConstArrayView<FTransform> W = R->PartWorld(Unit);
	S.Position = W.Num() > 0 ? AcSpace::ToSim(W[0].GetLocation()) : U->position;
	if (S.bHidden && PilotFire.Muzzles.Num() > 0)
	{
		S.Muzzles = PilotFire.Muzzles;
	}
	else
	{
		S.Parts = MuzzleParts(U->kind, S.bMinigun);
		for (const FName& P : S.Parts)
		{
			FVector L;
			const bool bFound = S.bReach ? PartMuzzle(Unit, P, L) : PartLocation(Unit, P, L);
			S.Muzzles.Add(bFound ? L : AcSpace::ToWorld(S.Position, GroundZ(S.Position) / Cm + 0.6));
		}
	}
	Fire(S, To, AcSpace::ToSim(To), 0.0, Time, nullptr);
}

void UAcEffects::Blast(const ac::Vec2 At, const double Radius, const double Time)
{
	// A driven unit's Pulse mine: a burst as wide as what it hurts, and a mark.
	const double Y = GroundZ(At) / Cm;
	Explosion(AcSpace::ToWorld(At, Y + 0.3), Radius * 0.6, Time);
	FAcDecalRequest D;
	D.At = AcSpace::ToWorld(At, Y);
	D.Radius = Radius * 0.6 * Cm;
	D.Color = FLinearColor(0.04, 0.04, 0.04);
	D.Life = 12;
	D.Time = Time;
	Decal(D);
}

void UAcEffects::Stomp(const ac::Vec2 At, const double Radius, const double Time)
{
	// The Atlas raises a leg first (AcPoseAtlas.cpp: the stomp is timed from
	// this event); the ring, the dust and the shake wait for the foot to come
	// down.
	TWeakObjectPtr<UAcEffects> Weak(this);
	After(AcPoseNew::AtlasStompImpact, Time, [Weak, At, Radius, Time]()
	{
		if (UAcEffects* E = Weak.Get()) E->StompImpact(At, Radius, Time + AcPoseNew::AtlasStompImpact);
	});
}

void UAcEffects::StompImpact(const ac::Vec2 At, const double Radius, const double Time)
{
	const double Y = GroundZ(At) / Cm;
	// A ring of dust thrown up at the edge of the stomp, as the shock reaches it.
	constexpr int32 Puffs = 12;
	for (int32 K = 0; K < Puffs; ++K)
	{
		const double A = double(K) / Puffs * 2.0 * UE_DOUBLE_PI;
		const ac::Vec2 P = At + ac::Vec2(std::cos(A), std::sin(A)) * (Radius * 0.85);
		DustPuff(AcSpace::ToWorld(P, GroundZ(P) / Cm + 0.1), 1.0, Time + 0.1);
	}
	DustPuff(AcSpace::ToWorld(At, Y + 0.1), 1.6, Time);
	// Two shock rings racing out to the reach, the second slower and fainter.
	FAcShockRingRequest Ring;
	Ring.At = AcSpace::ToWorld(At, Y);
	Ring.Radius = Radius * Cm;
	Ring.Life = 0.45;
	Ring.Time = Time;
	Ring.Bright = 1.0;
	ShockRing(Ring);
	Ring.Life = 0.75;
	Ring.Time = Time + 0.08;
	Ring.Bright = 0.55;
	ShockRing(Ring);
	// The foot's mark.
	FAcDecalRequest D;
	D.At = AcSpace::ToWorld(At, Y);
	D.Radius = Radius * 0.45 * Cm;
	D.Color = FLinearColor(0.05, 0.045, 0.04);
	D.Life = 10;
	D.Time = Time;
	Decal(D);
	Stomps.Add({At, Time});
}

void UAcEffects::Footfall(const FVector& At, const double Time, const int32 Side)
{
	const ac::Vec2 Sim = AcSpace::ToSim(At);
	const double Y = GroundZ(Sim) / Cm;
	// The foot's thud (the sound drops it when no director is playing).
	if (UAcAudioDirector* Audio = UAcAudioDirector::Get(this)) Audio->Rules().Footfall(Sim);
	// The foot's dust, and its print, which fades.
	DustPuff(AcSpace::ToWorld(Sim, Y + 0.08), 0.5, Time);
	FAcDecalRequest D;
	D.At = AcSpace::ToWorld(Sim, Y);
	D.Radius = 0.4 * Cm;
	D.Color = FLinearColor(0.06f, 0.055f, 0.05f, 1.f);
	D.Life = 12;
	D.Time = Time;
	Decal(D);
	// A fifth of a stomp's shake, felt within eight cells.
	Stomps.Add({Sim, Time, 0.2, 8.0});
}

double UAcEffects::StompShake(const ac::Vec2 From, const double Reach) const
{
	constexpr double Seconds = 0.6;
	double K = 0.0;
	for (const FStompMark& S : Stomps)
	{
		const double Age = NowTime - S.Time;
		const double D = ac::distance(From, S.At);
		const double Felt = FMath::Min(Reach, S.Cap);
		if (!(Age >= 0.0 && Age < Seconds && D < Felt)) continue;
		const double Fall = 1.0 - D / Felt, Decay = 1.0 - Age / Seconds;
		K += S.Weight * Fall * Decay * Decay * std::sin(Age * 55.0 + 0.5);
	}
	return FMath::Clamp(K, -1.0, 1.0);
}

void UAcEffects::Fire(const FShooter& S, const FVector& To, const ac::Vec2 At, const double Height, const double Time,
	TFunction<TOptional<FVector>()> Track)
{
	if (S.Muzzles.Num() == 0 && S.Kind != ac::UnitKind::firefly && S.Kind != ac::UnitKind::dropship) return;
	UAcWorldRenderer* R = Renderer();
	const double Ground = GroundZ(At);
	const bool bAnchored = S.Anchor > 0.5;
	const double Flight = ac::Rules::flight(S.Kind, bAnchored, ac::distance(S.Position, At));
	TWeakObjectPtr<UAcEffects> Weak(this);

	// Where muzzle k is now: read again from the pose (a round fired a beat
	// later leaves where the barrel is then). A Longbow's turret is turned
	// onto the target first, as Swift sets it the instant it fires, in case
	// its pose has not (a no-op once B4's pose snaps the turret).
	const FShooter Shooter = S;
	auto MuzzleNow = [Weak, Shooter, At](int32 K) -> FVector
	{
		const int32 N = Shooter.Muzzles.Num();
		if (N == 0) return FVector::ZeroVector;
		K = FMath::Clamp(K, 0, N - 1);
		FVector P = Shooter.Muzzles[K];
		const UAcEffects* E = Weak.Get();
		if (!E || Shooter.Parts.Num() != N) return P;
		if (Shooter.bReach) E->PartMuzzle(Shooter.Unit, Shooter.Parts[K], P);
		else E->PartLocation(Shooter.Unit, Shooter.Parts[K], P);
		if (Shooter.Kind == ac::UnitKind::longbow && !Shooter.bHidden)
		{
			const UAcWorldRenderer* WR = E->Renderer();
			const FAcModelInfo* M = WR ? WR->ModelOf(Shooter.Unit) : nullptr;
			const int32* TI = M ? M->PartIndex.Find(TEXT("turret")) : nullptr;
			const TConstArrayView<FTransform> W = WR ? WR->PartWorld(Shooter.Unit) : TConstArrayView<FTransform>();
			if (TI && W.IsValidIndex(*TI) && W.Num() > 0)
			{
				const FTransform& Turret = W[*TI];
				const FVector Fwd = Turret.GetRotation().RotateVector(FVector::ForwardVector);
				const ac::Vec2 Root = AcSpace::ToSim(W[0].GetLocation());
				const double Want = std::atan2(At.y - Root.y, At.x - Root.x);
				const double Have = std::atan2(Fwd.Y, Fwd.X);
				const double Turn = std::remainder(Want - Have, 2 * UE_DOUBLE_PI);
				if (std::abs(Turn) > 1e-3)
				{
					const FVector Pivot = Turret.GetLocation();
					P = Pivot + FQuat(FVector::UpVector, Turn).RotateVector(P - Pivot);
				}
			}
		}
		return P;
	};
	// A driven gun's shell or grenade shows from a little way down the line:
	// at its cockpit bore, a hand's width from the eye, its glow would fill
	// the view.
	const double Clear = S.bHidden ? 0.8 * Cm : 0.0;
	auto Round = [MuzzleNow, To, Clear](const int32 K) -> FVector
	{
		const FVector A = MuzzleNow(K);
		const FVector D = To - A;
		return A + D * FMath::Min(Clear / FMath::Max(D.Size(), 1e-3 * Cm), 0.5);
	};

	switch (S.Kind)
	{
	case ac::UnitKind::ranger:
	case ac::UnitKind::comet:
		if (S.Kind == ac::UnitKind::ranger && S.bMinigun)
		{
			MinigunRound(Round(0), To, Time);
			break;
		}
		{
			// A tracer from the first muzzle; a Comet's second pistol fires a
			// beat later, a little off the mark (`GameScene.tracers`).
			Tracer(S.Muzzles[0], To, Time);
			if (S.Muzzles.Num() > 1)
			{
				const FVector Off(To.X + 0.12 * Cm * std::sin(Time * 41), To.Y + 0.12 * Cm * std::cos(Time * 29), To.Z - 0.05 * Cm);
				Tracer(S.Muzzles[1], Off, Time + 0.06);
			}
		}
		break;
	case ac::UnitKind::juggernaut:
	{
		// The launchers' aim this instant is the pose's (B3: LastShotAt is
		// stamped before the poses run); remember where it fired.
		if (FAcUnitMemory* Mem = R ? R->Memory(S.Unit) : nullptr) Mem->LastAim = At;
		// A grenade from the right launcher, the left's a beat later a
		// little apart (`GameScene.grenades`).
		Grenade(Round(0), To, Ground, Flight, Time, Track);
		const FVector Off(To.X + 0.18 * Cm * std::sin(Time * 31), To.Y + 0.18 * Cm * std::cos(Time * 23), To.Z + 0.05 * Cm);
		TFunction<TOptional<FVector>()> Second;
		if (Track)
		{
			const FVector Delta = Off - To;
			Second = [Track, Delta]() -> TOptional<FVector>
			{
				const TOptional<FVector> P = Track();
				return P ? TOptional<FVector>(*P + Delta) : TOptional<FVector>();
			};
		}
		After(0.07, Time, [Weak, Round, Off, Ground, Flight, Time, Second]()
		{
			if (UAcEffects* E = Weak.Get()) E->Grenade(Round(1), Off, Ground, Flight, Time + 0.07, Second);
		});
		break;
	}
	case ac::UnitKind::firefly:
	{
		// The jet runs where the tail points, at the target, as far as the
		// target (or its full reach); the ground catches as the head of the
		// jet lands (`Firefly.flameLine`, +0.09 s).
		double Reach = ac::Rules::stats(ac::UnitKind::firefly).range * (S.bDriven ? ac::Rules::heroRange : 1.0);
		if (const UAcSimSubsystem* Sm = Sim())
		{
			if (const std::optional<ac::Unit> U = Sm->State().unit(S.Unit)) Reach = Sm->Simulation().weapon(*U).range;
		}
		const double Len = FMath::Min(ac::distance(S.Position, At), Reach + ac::Rules::radius(ac::UnitKind::firefly));
		// Read by the Firefly's pose next frame (Swift sets it before the
		// frame's sync; here the poses ran already).
		if (FAcUnitMemory* Mem = R ? R->Memory(S.Unit) : nullptr) Mem->FlameLength = Len;
		const double Heading = std::atan2(At.y - S.Position.y, At.x - S.Position.x);
		const ac::Vec2 Dir(std::cos(Heading), std::sin(Heading));
		const ac::Vec2 A = S.Position + Dir * 0.6, B = S.Position + Dir * Len;
		After(0.09, Time, [Weak, A, B, Time]()
		{
			if (UAcEffects* E = Weak.Get()) E->FlameLine(A, B, Time + 0.09);
		});
		break;
	}
	case ac::UnitKind::longbow:
	{
		// It fires only in tank mode or fully anchored (`Longbow.tankRounds`).
		const FVector Land = Height > 0 ? To : AcSpace::ToWorld(At, Ground / Cm + 0.15);
		const FVector Dust = AcSpace::ToWorld(S.Position, GroundZ(S.Position) / Cm + 0.1);
		if (bAnchored)
		{
			// A shell high over the field onto `Land`, homing on the target.
			const double Lift = Land.Z - To.Z;
			TFunction<TOptional<FVector>()> Aim;
			if (Track)
			{
				Aim = [Track, Lift]() -> TOptional<FVector>
				{
					const TOptional<FVector> P = Track();
					return P ? TOptional<FVector>(*P + FVector(0, 0, Lift)) : TOptional<FVector>();
				};
			}
			AnchorShell(Round(2), Land, Ground, Flight, Time, MoveTemp(Aim));
			DustPuff(Dust, 0.9, Time);
		}
		else
		{
			// A slug from the lead prong, the other's 0.11 s later a little off.
			const int32 Lead = LeadBarrel(Time);
			Slug(Round(Lead), To, Time);
			const FVector Off(To.X + 0.15 * Cm * std::sin(Time * 17), To.Y + 0.15 * Cm * std::cos(Time * 13), To.Z - 0.08 * Cm);
			After(0.11, Time, [Weak, Round, Off, Lead, Time]()
			{
				if (UAcEffects* E = Weak.Get()) E->Slug(Round(1 - Lead), Off, Time + 0.11);
			});
		}
		break;
	}
	case ac::UnitKind::kestrel:
		if (S.bAirTarget)
		{
			// The rail gun: one beam, from the pod whose turn it is.
			RailShot(Round(LeadBarrel(Time)), To, Time);
			break;
		}
		// A rocket from each pod, the right a beat after the left; each
		// bursts where it lands.
		for (int32 K = 0; K < 2; ++K)
		{
			const FVector Off(To.X + 0.12 * Cm * K * std::sin(Time * 23), To.Y + 0.12 * Cm * K * std::cos(Time * 19), To.Z);
			After(K * 0.08, Time, [Weak, Round, Off, K, Time, Flight]()
			{
				UAcEffects* E = Weak.Get();
				if (!E) return;
				const double T0 = Time + K * 0.08;
				E->Slug(Round(K), Off, T0);
				E->After(Flight, T0, [Weak, Off, T0, Flight]()
				{
					if (UAcEffects* E2 = Weak.Get()) E2->Explosion(Off, 0.3, T0 + Flight);
				});
			});
		}
		break;
	case ac::UnitKind::hailstorm:
		// Two pairs of flak rounds that burst in the air round the flyer.
		for (int32 K = 0; K < 2; ++K)
		{
			After(K * 0.1, Time, [Weak, Round, To, K, Time, Flight]()
			{
				UAcEffects* E = Weak.Get();
				if (!E) return;
				const double T0 = Time + K * 0.1;
				E->Slug(Round(2 * K), To, T0);
				E->Slug(Round(2 * K + 1), To, T0);
				const FVector Burst(To.X + 0.3 * Cm * std::sin(T0 * 37), To.Y + 0.3 * Cm * std::cos(T0 * 31),
					To.Z + 0.2 * Cm * std::cos(T0 * 29));
				E->After(Flight, T0, [Weak, Burst, T0, Flight]()
				{
					if (UAcEffects* E2 = Weak.Get()) E2->Explosion(Burst, 0.35, T0 + Flight);
				});
			});
		}
		break;
	case ac::UnitKind::peregrine:
	{
		// Two seeker missiles, one off each wing's rails (the pair the pose
		// took off them: the same parity of its `LastShot`), the right a beat
		// after the left; each flies as long as the damage takes and bursts
		// where it lands.
		const FAcUnitMemory* Mem = R ? R->Memory(S.Unit) : nullptr;
		const int32 Pair = AcPoseNew::MissilePair(Mem && Mem->LastShot ? *Mem->LastShot : Time);
		const int32 Rail[2] = {Pair == 0 ? 0 : 1, Pair == 0 ? 3 : 2};
		for (int32 K = 0; K < 2; ++K)
		{
			const FVector Off(To.X + 0.1 * Cm * K * std::sin(Time * 23), To.Y + 0.1 * Cm * K * std::cos(Time * 19), To.Z);
			// The cockpit has one muzzle a wing, the hull four rails.
			const int32 Index = S.Muzzles.Num() == 4 ? Rail[K] : FMath::Min(K, FMath::Max(S.Muzzles.Num() - 1, 0));
			After(K * 0.07, Time, [Weak, Round, Off, Index, K, Time, Flight, Ground, Track]()
			{
				if (UAcEffects* E = Weak.Get()) E->Round(EAcRound::Missile, Round(Index), Off, Ground, Flight, Time + K * 0.07, Track);
			});
		}
		break;
	}
	case ac::UnitKind::atlas:
	{
		// Twin siege cannons: the shells leave at once and hit at once
		// (`Rules::flight` is 0): they are seen crossing the short way in a
		// few frames, the second cannon a beat after the first, and burst
		// with their splash.
		const double Way = FVector::Dist(Round(0), To) / Cm;
		const double Visible = FMath::Clamp(Way / 45.0, 0.05, 0.25);
		for (int32 K = 0; K < 2; ++K)
		{
			const FVector Off(To.X + 0.2 * Cm * K * std::sin(Time * 29), To.Y + 0.2 * Cm * K * std::cos(Time * 17), To.Z);
			const int32 Index = FMath::Min(K, FMath::Max(S.Muzzles.Num() - 1, 0));
			After(K * AcPoseNew::AtlasSecondDelay, Time, [Weak, Round, Off, Index, K, Time, Visible, Ground, Track]()
			{
				if (UAcEffects* E = Weak.Get()) E->Round(EAcRound::AtlasShell, Round(Index), Off, Ground, Visible, Time + K * AcPoseNew::AtlasSecondDelay, Track);
			});
		}
		break;
	}
	case ac::UnitKind::scorpion:
		// The sting: one fast bolt from the launcher (0.2 s), a burst where it lands.
		this->Round(EAcRound::Sting, Round(0), To, Ground, FMath::Max(Flight, 0.05), Time, Track);
		break;
	case ac::UnitKind::dropship:
	case ac::UnitKind::prospector:
		break;
	}
}
