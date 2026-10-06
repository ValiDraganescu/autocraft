#include "AcPilotChase.h"

#include "AcLog.h"
#include "AcModelCatalog.h"
#include "AcPilotCamera.h"
#include "AcRays.h"
#include "AcRaysWorld.h"
#include "AcSpace.h"
#include "AcTerrain.h"
#include "AcWorldRenderer.h"
#include "AcPose.h"
#include "AcSimSubsystem.h"

#include "Engine/World.h"

#include <cmath>
#include <vector>

float FAcPilotChase::Reach(const FAcModelInfo& Model, const TConstArrayView<bool> Visible, const TConstArrayView<FTransform> Local)
{
	// Game thread only (the pilot's camera).
	static TMap<const FAcModelInfo*, float> Measured;
	if (const float* Known = Measured.Find(&Model)) return *Known;

	const int32 N = Model.Parts.Num();
	int32 Root = 0;
	for (int32 P = 0; P < N; ++P)
	{
		if (Model.Parts[P].Parent == INDEX_NONE) { Root = P; break; }
	}
	const double RootScale = N > 0 ? Model.Parts[Root].Rest.GetScale3D().X : 1.0;
	float Far = 0.f;
	int32 FarPart = INDEX_NONE;
	if (const FAcRayShape* Shape = FAcRayShapes::Get().Find(&Model); Shape && !Shape->Empty())
	{
		// Each part in the root's space (the root's own transform left out,
		// as Swift walks from the root node), hidden parts and theirs skipped.
		std::vector<FAcMat4> ToRoot(static_cast<size_t>(N));
		std::vector<uint8_t> Shown(static_cast<size_t>(N), 1);
		for (int32 P = 0; P < N; ++P)
		{
			const int32 Up = Model.Parts[P].Parent;
			const FAcMat4 Rest = AcRaySpace::Matrix(Local.IsValidIndex(P) ? Local[P] : Model.Parts[P].Rest);
			if (P == Root || Up == INDEX_NONE) ToRoot[size_t(P)] = FAcMat4();
			else if (Up == Root) ToRoot[size_t(P)] = Rest;
			else ToRoot[size_t(P)] = ToRoot[size_t(Up)] * Rest;
			const bool bOwn = Visible.IsValidIndex(P) ? Visible[P] : !Model.Parts[P].bHidden;
			Shown[size_t(P)] = (P == Root || bOwn) && (Up == INDEX_NONE || Shown[size_t(Up)]);
		}
		for (const FAcRayShape::FPiece& Piece : Shape->Pieces)
		{
			if (!Shown[size_t(Piece.Part)]) continue;
			const FAcMat4 M = ToRoot[size_t(Piece.Part)] * Piece.At;
			for (int C = 0; C < 8; ++C)
			{
				const FAcF3 Corner{(C & 1) == 0 ? Piece.Box.Lo.x : Piece.Box.Hi.x, (C & 2) == 0 ? Piece.Box.Lo.y : Piece.Box.Hi.y,
					(C & 4) == 0 ? Piece.Box.Lo.z : Piece.Box.Hi.z};
				const FAcF3 W = M.Point(Corner);
				const float Dxz = std::sqrt(W.x * W.x + W.z * W.z);
				if (Dxz > Far) FarPart = Piece.Part;
				Far = FMath::Max(Far, Dxz);
			}
		}
	}
	else if (Model.Bounds.IsValid)
	{
		// No ray pieces: the rest bounds' corners across the ground.
		for (int C = 0; C < 4; ++C)
		{
			const double X = (C & 1) ? Model.Bounds.Max.X : Model.Bounds.Min.X;
			const double Y = (C & 2) ? Model.Bounds.Max.Y : Model.Bounds.Min.Y;
			Far = FMath::Max(Far, float(AcSpace::ToCells(FVector2D(X, Y).Size())));
		}
	}
	const float Reach = Far > 0.f ? Far * float(RootScale) : 1.f;
	Measured.Add(&Model, Reach);
	UE_LOG(LogAutocraft, Log, TEXT("pilot: %s reaches %.2f cells (third-person boom; farthest: %s)"), *Model.Name.ToString(), Reach,
		Model.Parts.IsValidIndex(FarPart) ? *Model.Parts[FarPart].Name.ToString() : TEXT("-"));
	return Reach;
}

float FAcPilotChase::AtEase(UWorld* World, const ac::Unit& U, const FAcModelInfo& Model)
{
	// Swift measures the live node the first time it chases a kind, which is
	// the unit as it stood before the take-over: at ease, not aiming (a
	// Ranger's rifle held up across the ground would reach 1 cell, its body
	// 0.6), the parts its owner's upgrades hide hidden. Pose it so once.
	static TSet<const FAcModelInfo*> Posed;
	if (Posed.Contains(&Model)) return Reach(Model);
	Posed.Add(&Model);
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(World);
	const AAcTerrain* Terrain = AAcTerrain::Find(World);
	if (!Sim || !Sim->IsRunning() || !Terrain) return Reach(Model);
	ac::Unit V = U;
	V.task = ac::Unit::Task::idle;
	V.target.reset();
	V.aim.reset();
	V.moving = false;
	FAcUnitMemory Memory;
	Memory.FirstSeen = Sim->State().time;
	FAcPoseContext C{Sim->State(), &V, nullptr, Model, Terrain->GetField(), Memory, Sim->State().time, 0.0, false, Sim->LocalPlayer()};
	FAcPose Pose;
	AcPose::RestUnit(C, Pose);
	if (const FAcPoseFn Fn = AcPose::Find(V.kind)) Fn(C, Pose);
	return Reach(Model, Pose.Visible, Pose.Local);
}

void FAcPilotChase::Reset()
{
	PivotZ.Reset();
	ChaseEye.Reset();
}

void FAcPilotChase::Follow(UWorld* World, const FTransform& Root, const ac::Unit& U, const FAcModelInfo* Model, const double Pitch,
	const double Yaw, const double RealNow, FVector& OutLocation, FQuat& OutRotation)
{
	// The unit's eye on its model (as the first-person eye, the turret's
	// turn counted, no lean and no bob): where its shots start.
	const FQuat Turret(FVector(0, 1, 0), -std::remainder(U.look() - U.heading, 2.0 * UE_DOUBLE_PI));
	const FVector P = FAcPilotCamera::Pivot(U.kind);
	const FVector E = P + Turret.RotateVector(FAcPilotCamera::Eye(U.kind) - P);
	const FVector Eye = Root.TransformPosition(AcSpace::FromSceneKit(E.X, E.Y, E.Z));

	const double R = Model ? AtEase(World, U, *Model) : 1.0;
	const FVector Ahead(std::cos(Yaw), std::sin(Yaw), 0.0);
	// The unit's right across the ground (heading grows toward it).
	const FVector Right(-std::sin(Yaw), std::cos(Yaw), 0.0);
	const FVector Look = Ahead * std::cos(Pitch) + FVector(0, 0, std::sin(Pitch));
	float Lift = 0.f, Length = 1.f;
	FAcPilotCamera::Boom(U.kind, Lift, Length);
	FVector Pivot = Eye + FVector(0, 0, AcSpace::ToCm(0.1 + 0.15 * R + Lift)) + Right * AcSpace::ToCm(0.4 + 0.45 * R);
	// The pivot rides the ground's bumps eased, as a machine's eye does.
	if (PivotZ && RealNow - PivotZ->Value < 0.5)
	{
		Pivot.Z = PivotZ->Key + (Pivot.Z - PivotZ->Key) * FMath::Min(1.0, (RealNow - PivotZ->Value) * 9.0);
	}
	PivotZ = TPair<double, double>(Pivot.Z, RealNow);
	FVector At = Pivot - Look * AcSpace::ToCm((2.6 + 3.3 * R) * Length);
	if (UAcRaysSubsystem* Rays = UAcRaysSubsystem::Get(World))
	{
		// The boom comes in where a building or a rock is behind the unit...
		if (const TOptional<float> T = Rays->Blocked(Pivot, At)) At = Pivot + (At - Pivot) * FMath::Max(0.1, double(*T) - 0.08);
		// ...nor between the camera and the unit's body.
		const FVector Body = (Eye + Root.GetTranslation()) / 2;
		if (const TOptional<float> T = Rays->Blocked(At, Body); T && *T < 0.9f)
		{
			At = At + (Pivot - At) * FMath::Min(0.9, double(*T) + 0.08);
		}
	}
	if (const AAcTerrain* Terrain = AAcTerrain::Find(World))
	{
		At.Z = FMath::Max(At.Z, AcSpace::ToCm(Terrain->FieldHeight(AcSpace::ToSim(At)) + 0.3));
	}
	ChaseEye = Eye;
	OutLocation = At;
	OutRotation = FRotationMatrix::MakeFromXZ(Look, FVector::UpVector).ToQuat();
}
