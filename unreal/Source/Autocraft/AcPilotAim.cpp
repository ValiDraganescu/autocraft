#include "AcPilotAim.h"

#include "AcModelCatalog.h"
#include "AcPose.h"
#include "AcSpace.h"

#include "Pilot.h"
#include "Rules.h"
#include "Simulation.h"

#include <cmath>

namespace
{
	/// SceneKit `eulerAngles` → the node's quaternion (R = Rz·Ry·Rx), as the
	/// pose code reads the export's rest values.
	FQuat SkEuler(const FVector& E)
	{
		return FQuat(FVector(0, 0, 1), E.Z) * FQuat(FVector(0, 1, 0), E.Y) * FQuat(FVector(1, 0, 0), E.X);
	}
}

FQuat AcPilotAim::Turn(const FVector& Pivot, const FQuat& Q, const FVector& Scale, const FVector& Muzzle, const FVector& Axis,
	const FVector& Target)
{
	const FVector A = Q.RotateVector(Axis).GetSafeNormal();
	const FVector M = Q.RotateVector(Muzzle * Scale);
	// The bore's offset square to its line: what the turn must allow for.
	const FVector Off = M - A * FVector::DotProduct(M, A);
	const FVector To = Target - Pivot;
	if (To.Size() <= 2.0 * Off.Size() + 0.05) return FQuat::Identity;
	FQuat R = FQuat::Identity;
	for (int32 K = 0; K < 4; ++K)
	{
		R = FQuat::FindBetweenNormals(A, (To - R.RotateVector(Off)).GetSafeNormal());
	}
	return R;
}

FTransform AcPilotAim::PartWorld(const FAcModelInfo& Model, const FAcPose& Pose, int32 Part)
{
	FTransform T = FTransform::Identity;
	while (Part != INDEX_NONE)
	{
		T = T * Pose.Local[Part];
		Part = Model.Parts[Part].Parent;
	}
	return T * Pose.Placement;
}

FQuat AcPilotAim::Held(const FAcPoseContext& C, const FAcPose& Pose, const int32 Part, const FVector& PosSk, const FQuat& QSk,
	const FVector& ScaleSk, const int32 Muzzle, const FVector& AxisSk)
{
	const FAcModelInfo& M = C.Model;
	if (!C.bDriven || !C.PilotAim || !M.Parts.IsValidIndex(Part) || !M.Parts.IsValidIndex(Muzzle)) return FQuat::Identity;
	const FAcModelPart& P = M.Parts[Part];
	const FTransform Parent = P.Parent == INDEX_NONE ? Pose.Placement : PartWorld(M, Pose, P.Parent);
	// The node's SceneKit parent may be a node the export merged away (its
	// rest = own × chain): the frame the SceneKit values are in is the chain
	// under the exported parent.
	const FTransform OwnRest = AcSpace::TransformFromSceneKit(P.SkPosition, SkEuler(P.SkEuler), P.SkScale);
	const FMatrix Chain = OwnRest.ToMatrixWithScale().Inverse() * P.Rest.ToMatrixWithScale();
	const FMatrix Frame = Chain * Parent.ToMatrixWithScale();
	const FVector Target = AcSpace::ToSceneKit(Frame.InverseTransformPosition(*C.PilotAim));
	return Turn(PosSk, QSk, ScaleSk, M.Parts[Muzzle].SkPosition, AxisSk, Target);
}

TOptional<FVector2D> AcPilotAim::ScreenOffset(const FVector& Eye, const FQuat& View, const double VerticalFov, const FVector& P)
{
	const FVector L = View.UnrotateVector(P - Eye);
	if (L.X < AcSpace::ToCm(0.05)) return {};
	const double K = std::tan(FMath::DegreesToRadians(VerticalFov) * 0.5);
	return FVector2D(L.Y / L.X / K, L.Z / L.X / K);
}

TOptional<FVector2D> AcPilotAim::GunMarker(const ac::Simulation& Sim, const ac::Unit& U, const FVector& Camera, const FQuat& View,
	const double VerticalFov, const FVector& LineFrom, const TOptional<FVector>& Aim)
{
	if (!ac::Pilot::steers(U.kind)) return {};
	const double Radius = ac::Rules::radius(U.kind);
	const double Reach = Sim.weapon(U).range + Radius;
	const std::optional<ac::PilotSight> Sight = Sim.pilotSight(U);
	double D = Sight ? Sight->distance + Radius : Reach;
	if (Aim)
	{
		D = FMath::Min(D, FMath::Max(2.0, AcSpace::ToCells(FVector2D(Aim->X - LineFrom.X, Aim->Y - LineFrom.Y).Size())));
	}
	const FVector Dir(std::cos(U.look()), std::sin(U.look()), 0.0);
	return ScreenOffset(Camera, View, VerticalFov, LineFrom + Dir * AcSpace::ToCm(D));
}

double AcPilotAim::Facing(const ac::Unit& U, const double Yaw, const TOptional<FVector>& Aim)
{
	if (!Aim) return Yaw;
	const ac::Vec2 D = AcSpace::ToSim(*Aim) - U.position;
	return ac::length(D) > 1.2 ? std::atan2(D.y, D.x) : Yaw;
}

TOptional<FAcRangeCue> AcPilotAim::RangeCue(const ac::Simulation& Sim, const ac::Unit& U)
{
	if (U.task == ac::Unit::Task::inDerrick) return {};
	const double R = ac::Rules::radius(U.kind);
	const std::optional<ac::PilotSight> Sight = Sim.pilotSight(U);
	FAcRangeCue Cue;
	Cue.Center = U.position;
	Cue.Inner = R;
	if (U.kind == ac::UnitKind::dropship)
	{
		Cue.Radius = R + ac::Rules::healRange;
		Cue.Tone = Sight && Sight->friend_ && Sight->inRange ? FAcRangeCue::ETone::Friend : FAcRangeCue::ETone::Idle;
		return Cue;
	}
	if (!Sim.pilotShoots(U)) return {};
	const double Anchor = U.anchor.value_or(0.0);
	const bool bPending = Anchor > 0 && Anchor < 1;
	// The weapon it will have once settled.
	ac::UnitStats W = Sim.weapon(U);
	if (bPending)
	{
		W = U.anchored == true ? ac::UnitStats::anchor : U.stats();
		W.range = Sim.boost(ac::Stat::Range{}, U).apply(W.range);
	}
	const bool bLock = Sight && !Sight->friend_ && Sight->inRange;
	Cue.Radius = R + W.range;
	Cue.MinRadius = W.minRange > 0 ? R + W.minRange : 0.0;
	Cue.Tone = bLock && !bPending ? FAcRangeCue::ETone::Lock : FAcRangeCue::ETone::Idle;
	Cue.bPending = bPending;
	return Cue;
}
