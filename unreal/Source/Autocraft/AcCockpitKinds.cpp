#include "AcCockpitKinds.h"

#include "HAL/IConsoleManager.h"

#include "AcModelCatalog.h"
#include "AcNewKinds.h"
#include "AcParticles.h"
#include "AcPilotCamera.h"
#include "AcPoseNewKinds.h"
#include "AcPoseVehicles.h"
#include "AcSpace.h"

#include "Pilot.h"
#include "Rules.h"
#include "TerrainField.h"

#include <cmath>

namespace
{
	TAutoConsoleVariable<float> CVarFlameLight(TEXT("ac.CockpitFlameLight"), 0.15f,
		TEXT("The driven Firefly's flame light on its own hull and the near ground, a share of Swift's flameLight (C6/E5)."));

	using ac::UnitKind;
	using Task = ac::Unit::Task;
	constexpr double Pi = UE_DOUBLE_PI;

	double Smoothstep(const double A, const double B, const double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}
	double Rem2Pi(const double A) { return std::remainder(A, 2 * Pi); }

	// --- SceneKit frame ------------------------------------------------------
	// Quaternions below are SceneKit's (simd_quatf algebra held in an FQuat:
	// the same products and rotations), positions are cells.

	const FVector SkXAxis(1, 0, 0), SkYAxis(0, 1, 0), SkZAxis(0, 0, 1);
	FQuat SkAxis(const FVector& Axis, const double Angle) { return FQuat(Axis, Angle); }
	/// SceneKit `eulerAngles` (as the pose chunks convert them).
	FQuat SkEuler(const FVector& E)
	{
		return FQuat(SkZAxis, E.Z) * FQuat(SkYAxis, E.Y) * FQuat(SkXAxis, E.X);
	}
	/// Model frame (+X ahead, +Y up, +Z right) to the camera's (−Z ahead).
	const FQuat& ModelToCamera()
	{
		static const FQuat Q = SkAxis(SkYAxis, Pi / 2);
		return Q;
	}

	/// A part's SceneKit node values, from its rest; `Put` writes them as the
	/// part's Unreal local transform (under the frame of any node the export
	/// merged into it: `Frame` = (node rest)⁻¹ · rest).
	struct FNode
	{
		FVector Pos = FVector::ZeroVector;
		FQuat Rot = FQuat::Identity;
		FVector Scale = FVector::OneVector;
		FVector Euler = FVector::ZeroVector;
		FTransform Frame = FTransform::Identity;
		bool bValid = false;

		FNode() = default;
		FNode(const FAcModelInfo& M, const int32 I)
		{
			if (I == INDEX_NONE) return;
			const FAcModelPart& Part = M.Parts[I];
			Pos = Part.SkPosition;
			Euler = Part.SkEuler;
			Rot = SkEuler(Euler);
			Scale = Part.SkScale;
			bValid = true;
			// The rest orientation from the rest transform, not the euler
			// angles: SceneKit reports degenerate angles at a yaw of ±90°
			// (a Juggernaut's launcher), where its roll-yaw-pitch order locks.
			const FVector RestPos = AcSpace::ToSceneKit(Part.Rest.GetTranslation());
			const FVector RestScale = Part.Rest.GetScale3D();
			if (RestPos.Equals(Pos, 1e-4) && FVector(RestScale.X, RestScale.Z, RestScale.Y).Equals(Scale, 1e-4))
			{
				Rot = AcSpace::QuatToSceneKit(Part.Rest.GetRotation());
				return;
			}
			// A node the export merged into this part: its values go under that frame.
			Frame = AcSpace::TransformFromSceneKit(Pos, Rot, Scale).Inverse() * Part.Rest;
			if (Frame.Equals(FTransform::Identity, 1e-4)) Frame = FTransform::Identity;
		}
		void SetEuler(const FVector& E)
		{
			Euler = E;
			Rot = SkEuler(E);
		}
		FTransform Own() const { return AcSpace::TransformFromSceneKit(Pos, Rot, Scale); }
		/// A point of this node's frame in its parent's (cells).
		FVector ToParent(const FVector& P) const { return Pos + Rot.RotateVector(P * Scale); }
	};

	void Put(FAcCockpitPose& P, const int32 I, const FNode& N)
	{
		if (I != INDEX_NONE && N.bValid) P.Local[I] = N.Own() * N.Frame;
	}
	void SetVisible(FAcCockpitPose& P, const int32 I, const bool b)
	{
		if (I != INDEX_NONE) P.Visible[I] = b;
	}
	void SetEmission(FAcCockpitPose& P, const int32 I, const double E)
	{
		if (I != INDEX_NONE) P.Emission[I] = float(E);
	}

	int32 PartOf(const FAcModelInfo& M, const TCHAR* Name)
	{
		const int32* I = M.PartIndex.Find(FName(Name));
		return I ? *I : INDEX_NONE;
	}

	/// A part's transform to the cockpit root (Unreal, cm), this frame.
	FTransform ToRoot(const FAcModelInfo& M, const FAcCockpitPose& P, int32 I)
	{
		FTransform T = FTransform::Identity;
		while (I != INDEX_NONE)
		{
			T = T * P.Local[I];
			I = M.Parts[I].Parent;
		}
		return T;
	}

	/// A root point (cm) in part `I`'s own SceneKit frame (cells): Swift's
	/// `node.simdConvertPosition(p, from: nil)` with the eye at the root.
	FVector InPart(const FAcModelInfo& M, const FAcCockpitPose& P, const int32 I, const FVector& RootCm)
	{
		const FTransform T = ToRoot(M, P, I);
		return AcSpace::ToSceneKit(T.InverseTransformPosition(RootCm));
	}

	/// `Aim.inRig`: the aim point in `rig`'s frame, no nearer than 1.5 cells.
	TOptional<FVector> InRig(const FAcModelInfo& M, const FAcCockpitPose& P, const int32 Rig, const TOptional<FVector>& AimRoot)
	{
		if (!AimRoot || Rig == INDEX_NONE) return {};
		const FVector T = InPart(M, P, Rig, *AimRoot);
		const double Len = T.Size();
		return Len < 1.5 ? T / FMath::Max(Len, 1e-3) * 1.5 : T;
	}

	/// `Aim.turn`: the turn (parent frame) that swings a part about its
	/// pivot so its line of fire (along `Axis` through `Muzzle`, its own
	/// frame before `Scale`) runs through `Target`.
	FQuat AimTurn(const FVector& Pivot, const FQuat& Q, const FVector& Scale, const FVector& Muzzle, const FVector& Axis,
		const FVector& Target)
	{
		const FVector A = Q.RotateVector(Axis).GetSafeNormal();
		const FVector Mz = Q.RotateVector(Muzzle * Scale);
		const FVector Off = Mz - A * FVector::DotProduct(Mz, A);
		const FVector To = Target - Pivot;
		if (To.Size() <= 2 * Off.Size() + 0.05) return FQuat::Identity;
		FQuat R = FQuat::Identity;
		for (int32 K = 0; K < 4; ++K) R = FQuat::FindBetweenNormals(A, (To - R.RotateVector(Off)).GetSafeNormal());
		return R;
	}

	/// 1 at the moment of a shot, falling to 0 over `Length` seconds.
	double Recoil(const double Since, const double Length)
	{
		return Since >= 0 && Since < Length ? 1 - Since / Length : 0.0;
	}

	/// `HeldPart`: a gun on an arm, its rest and its line of fire.
	struct FHeld
	{
		int32 Part = INDEX_NONE;
		FNode Rest;
		FVector Muzzle = FVector::ZeroVector;
		FVector Axis = FVector(0, 0, -1);

		/// Its line of fire through `Target` (rig frame), pushed back by
		/// `Back`, moved by `Offset`, turned about the camera's X by `Up`.
		void Pose(FAcCockpitPose& P, const double Back, const double Up, const FVector& Offset,
			const TOptional<FVector>& Target) const
		{
			if (Part == INDEX_NONE) return;
			const FQuat Turn = Target ? AimTurn(Rest.Pos, Rest.Rot, Rest.Scale, Muzzle, Axis, *Target) : FQuat::Identity;
			FNode N = Rest;
			N.Pos = Rest.Pos + Offset + FVector(0, 0, Back);
			N.Rot = SkAxis(SkXAxis, Up) * Turn * Rest.Rot;
			Put(P, Part, N);
		}
	};

	FAcCockpitPose::FLight Light(const FVector& AtCm, const FLinearColor& Color, const double Intensity, const double Reach)
	{
		FAcCockpitPose::FLight L;
		L.At = AtCm;
		L.Color = Color;
		L.Intensity = float(Intensity);
		L.Reach = float(Reach);
		return L;
	}

	/// `ForkMotion` (Models+Fork.swift), as B2 ports it for the model.
	struct FForkMotion
	{
		static constexpr double Pickup = 1.3;
		bool bInit = false;
		double Open = 0, Load = 0, Slide = 0, Reach = 0;
		bool bHydrogen = false, bPicking = false, bBit = false;
		TOptional<double> Last, Phase;

		void Step(const ac::Unit& U, const double Time)
		{
			if (!bInit)
			{
				bInit = true;
				Open = U.carrying > 0 ? 1.0 : 0.0;
				Load = Open;
				bHydrogen = U.hydrogen.value_or(false);
			}
			const double Dt = FMath::Max(0.0, FMath::Min(0.1, Time - Last.Get(Time)));
			Last = Time;
			bBit = false;
			if (U.task == Task::mining && U.carrying == 0 && U.timer < Pickup)
			{
				const double S = 1.0 - FMath::Max(0.0, U.timer) / Pickup;
				if (Phase.Get(0.0) < 0.32 && S >= 0.32) bBit = true;
				Phase = S;
				bPicking = true;
				bHydrogen = false;
				Slide = 0;
				Reach = Smoothstep(0, 0.3, S) * (1 - Smoothstep(0.55, 1, S));
				if (S > 0.42 && S < 0.55) Reach -= 0.07 * std::sin((S - 0.42) / 0.13 * 2 * Pi);
				Open = S < 0.42 ? 1.35 * Smoothstep(0.02, 0.28, S)
					: 1.35 - 0.47 * Smoothstep(0.42, 0.5, S) + 0.12 * Smoothstep(0.5, 0.65, S);
				Load = Smoothstep(0.3, 0.36, S);
				return;
			}
			Phase.Reset();
			bPicking = false;
			Reach = FMath::Max(0.0, Reach - Dt / 0.4);
			if (U.carrying > 0)
			{
				bHydrogen = U.hydrogen.value_or(false);
				Slide = 0;
				Open = Open > 1 ? FMath::Max(1.0, Open - Dt / 0.3) : FMath::Min(1.0, Open + Dt / 0.3);
				Load = FMath::Max(Load, Smoothstep(0, 0.8, Open));
			}
			else
			{
				Open = FMath::Max(0.0, FMath::Min(1.0, Open) - Dt / 0.45);
				Load = FMath::Min(Load, Smoothstep(0.35, 1, Open));
				Slide = (1 - Open) * 0.22;
			}
		}
	};

	/// `Models.MinigunSpin`.
	struct FSpin
	{
		double Angle = 0, Rate = 0, Heat = 0;
		TOptional<double> Last;
	};
	constexpr double MinigunSpin = 30.0;

	/// An orientation turning `Front` (own frame) toward `Dir`, its +Y as
	/// near world +Y as it goes (`simdLook(at:up:localFront:)`).
	FQuat LookAlong(const FVector& Dir, const FVector& Front)
	{
		const FVector F = Dir.GetSafeNormal();
		// Basis for local front = +X: x = F, z = F × up, y = z × x.
		const FVector Z = FVector::CrossProduct(F, SkYAxis).GetSafeNormal();
		const FVector Y = FVector::CrossProduct(Z, F);
		const FQuat XFront(FMatrix(FPlane(F, 0), FPlane(Y, 0), FPlane(Z, 0), FPlane(0, 0, 0, 1)));
		// Then the local front onto +X.
		return XFront * FQuat::FindBetweenNormals(Front.GetSafeNormal(), SkXAxis);
	}
}

// MARK: - The memory of a ride

struct UAcCockpitSubsystem::FState
{
	UnitKind Kind = UnitKind::ranger;
	const FAcModelInfo* Model = nullptr;
	int32 Rig = INDEX_NONE, Level = INDEX_NONE;

	// Vehicles that carry the unit's own model (Firefly, Longbow, Hailstorm,
	// and the new kinds' own cockpits: Peregrine, Atlas, Scorpion).
	const FAcModelInfo* UnitModel = nullptr;
	FAcPoseFn UnitPose = nullptr;
	int32 ModelRoot = INDEX_NONE;
	/// Cockpit part → unit part (INDEX_NONE: the cockpit's own), and the
	/// offset of a part the cockpit hung elsewhere: Local = unit local × Adjust.
	TArray<int32> UnitPart;
	TArray<FMatrix> Adjust;
	TArray<bool> bAdjust;
	FAcUnitMemory UnitMemory;
	FAcPose UnitPosed;

	// Per kind.
	FForkMotion Fork;
	FQuat ForkRest = FQuat::Identity, ForkReached = FQuat::Identity;
	double DrillTurn = 0;
	FSpin Spin;
	TArray<FHeld, TInlineAllocator<2>> Held;
	AcCockpitKinds::FLamps Lamps;
};

std::shared_ptr<UAcCockpitSubsystem::FState> AcCockpitKinds::Make(const UnitKind Driven, const FAcModelInfo& Cockpit)
{
	// A new kind without a cockpit of its own rides in its stand-in's (AcNewKinds.h).
	const UnitKind Kind = AcNewKinds::BorrowedCockpit(Driven);
	auto S = std::make_shared<UAcCockpitSubsystem::FState>();
	S->Kind = Kind;
	S->Model = &Cockpit;
	const FAcModelInfo& M = Cockpit;
	S->Rig = PartOf(M, TEXT("rig"));
	S->Level = PartOf(M, TEXT("level"));

	switch (Kind)
	{
	case UnitKind::prospector:
	{
		// `fork.rest` is the arm as built; `reached` points it at the middle
		// of the view, 1.35 ahead (its local front +X).
		const FNode Arm(M, PartOf(M, TEXT("fork_arm")));
		S->ForkRest = Arm.Rot;
		S->ForkReached = LookAlong(FVector(0, -0.02, -1.35) - Arm.Pos, SkXAxis);
		break;
	}
	case UnitKind::comet:
	case UnitKind::juggernaut:
	{
		const bool bComet = Kind == UnitKind::comet;
		for (int32 K = 0; K < 2; ++K)
		{
			FHeld H;
			H.Part = PartOf(M, *(bComet ? FString::Printf(TEXT("guns_%d_node"), K) : FString::Printf(TEXT("launchers_%d_node"), K)));
			H.Rest = FNode(M, H.Part);
			const int32 Mz = PartOf(M, *(bComet ? FString::Printf(TEXT("flashes_%d"), K) : FString::Printf(TEXT("muzzles_%d"), K)));
			if (Mz != INDEX_NONE) H.Muzzle = M.Parts[Mz].SkPosition;
			H.Axis = bComet ? FVector(0, -1, 0) : FVector(1, 0, 0);
			S->Held.Add(H);
		}
		break;
	}
	case UnitKind::firefly:
	case UnitKind::longbow:
	case UnitKind::hailstorm:
	case UnitKind::peregrine:
	case UnitKind::atlas:
	case UnitKind::scorpion:
	{
		const FName UnitName(*FString::Printf(TEXT("%s_blue"), AcPose::ModelBase(Kind)));
		S->UnitModel = FAcModelCatalog::Get().Find(UnitName);
		S->UnitPose = AcPose::Find(Kind);
		S->ModelRoot = PartOf(M, TEXT("model_root"));
		S->UnitPart.Init(INDEX_NONE, M.Parts.Num());
		S->Adjust.Init(FMatrix::Identity, M.Parts.Num());
		S->bAdjust.Init(false, M.Parts.Num());
		if (S->UnitModel)
		{
			const FAcModelInfo& UM = *S->UnitModel;
			for (int32 I = 0; I < M.Parts.Num(); ++I)
			{
				const FString Name = M.Parts[I].Name.ToString();
				if (!Name.StartsWith(TEXT("model_"))) continue;
				const int32 J = I == S->ModelRoot ? 0 : PartOf(UM, *Name.RightChop(6));
				S->UnitPart[I] = J;
				if (J == INDEX_NONE || I == S->ModelRoot) continue;
				const FTransform& Ru = UM.Parts[J].Rest;
				const FTransform& Rc = M.Parts[I].Rest;
				if (!Ru.Equals(Rc, 1e-3))
				{
					S->Adjust[I] = Ru.ToMatrixWithScale().Inverse() * Rc.ToMatrixWithScale();
					S->bAdjust[I] = true;
				}
			}
		}
		break;
	}
	default:
		break;
	}
	return S;
}

void AcCockpitKinds::SetLamps(UAcCockpitSubsystem::FState& S, const FLamps& Lamps) { S.Lamps = Lamps; }

namespace
{
	using FState = UAcCockpitSubsystem::FState;

	/// The unit's own model posed by its kind's pose function, on the
	/// cockpit's `model_*` parts (Firefly, Longbow, Hailstorm). `Root`: the
	/// model root's SceneKit placement in `level`'s frame. Returns the
	/// height (cells) the pose added to its placement (a bob).
	double PoseUnitModel(FState& S, const FAcCockpitContext& C, FAcCockpitPose& P, const FNode& Root)
	{
		if (!S.UnitModel) return 0.0;
		const FAcModelInfo& UM = *S.UnitModel;
		const FAcModelInfo& M = *S.Model;
		// The renderer's memory of the driven unit: its shots, its flame.
		if (C.Memory)
		{
			S.UnitMemory.LastShot = C.Memory->LastShot;
			S.UnitMemory.LastTarget = C.Memory->LastTarget;
			S.UnitMemory.LastShotAt = C.Memory->LastShotAt;
			S.UnitMemory.FlameLength = C.Memory->FlameLength;
		}
		if (C.Cues.Shot) S.UnitMemory.LastShot = C.Cues.Shot;
		FAcPoseContext Ctx{C.Sim.state, &C.Unit, nullptr, UM, C.Field, S.UnitMemory, C.Cues.Time, C.Cues.Dt, true, 0};
		FAcPose& U = S.UnitPosed;
		AcPose::RestUnit(Ctx, U);
		const double BaseZ = U.Placement.GetTranslation().Z;
		if (S.UnitPose) S.UnitPose(Ctx, U);
		const double Bob = AcSpace::ToCells(U.Placement.GetTranslation().Z - BaseZ);

		for (int32 I = 0; I < M.Parts.Num(); ++I)
		{
			const int32 J = S.UnitPart[I];
			if (J == INDEX_NONE || !U.Local.IsValidIndex(J)) continue;
			P.Visible[I] = U.Visible[J];
			P.Emission[I] = U.Emission[J];
			if (I == S.ModelRoot) continue;
			P.Local[I] = S.bAdjust[I] ? FTransform(U.Local[J].ToMatrixWithScale() * S.Adjust[I]) : U.Local[J];
		}
		for (const FAcPose::FMeshEmission& E : U.MeshEmission)
		{
			for (int32 I = 0; I < M.Parts.Num(); ++I)
			{
				if (S.UnitPart[I] != E.Part) continue;
				// The cockpit's copies of the model's materials are named `model_<name>`.
				P.MeshEmission.Add({I, E.Material, E.Value});
				P.MeshEmission.Add({I, FName(*(TEXT("model_") + E.Material.ToString())), E.Value});
			}
		}
		Put(P, S.ModelRoot, Root);
		return Bob;
	}

	/// Part `I`'s local transform taken a share `K` of the way from none to
	/// what the pose set (`position.y *= k`, `eulerAngles.x/z *= k`).
	void Damp(FAcCockpitPose& P, const int32 I, const double K)
	{
		if (I == INDEX_NONE) return;
		FTransform& L = P.Local[I];
		FVector T = L.GetTranslation();
		T.Z *= K;
		L.SetTranslation(T);
		L.SetRotation(FQuat::Slerp(FQuat::Identity, L.GetRotation(), K).GetNormalized());
	}

	/// The model root's place in `level`'s frame: the hull turned under the
	/// eye by the turret's angle `D` to it, about the turret's pivot.
	FNode ModelRootNode(const FState& S, const ac::UnitKind Kind, const double D)
	{
		FNode Root(*S.Model, S.ModelRoot);
		const FVector Eye = FAcPilotCamera::Eye(Kind);
		// The Firefly turns about the eye itself (its pivot is its eye).
		const FVector Pivot = FAcPilotCamera::Pivot(Kind);
		const FQuat Turn = SkAxis(SkYAxis, D);
		const FVector Origin = ModelToCamera().RotateVector(Pivot - Eye + Turn.RotateVector(-Pivot));
		Root.Pos = Origin;
		// heading −(π/2 + d) → eulerAngles.y = π/2 + d.
		Root.SetEuler(FVector(0, Pi / 2 + D, 0));
		return Root;
	}

	void Unpitch(const FState& S, const FAcCockpitContext& C, FAcCockpitPose& P)
	{
		if (S.Level == INDEX_NONE) return;
		FNode L(*S.Model, S.Level);
		L.Rot = SkAxis(SkXAxis, -C.Cues.Pitch);
		Put(P, S.Level, L);
	}

	void Muzzles(const FAcModelInfo& M, FAcCockpitPose& P, std::initializer_list<const TCHAR*> Names)
	{
		for (const TCHAR* N : Names) P.Muzzles.Add(PartOf(M, N));
	}

	// --- Prospector (PilotCamera.animate) ---------------------------------------

	void PoseProspector(FState& S, const FAcCockpitContext& C, FAcCockpitPose& P)
	{
		const FAcModelInfo& M = *S.Model;
		const ac::Unit& U = C.Unit;
		const double Time = C.Cues.Time;
		const bool bWalking = U.moving.value_or(false);
		const double Sway = bWalking ? std::sin(U.stride * 3.2) : 0.0;

		// The fork's round (`ProspectorFork.pose` on the cockpit's fork).
		FForkMotion& F = S.Fork;
		F.Step(U, Time);
		{
			const double E = F.Open < 1 ? Smoothstep(0, 1, F.Open) : F.Open;
			const double Snap = F.Open < 0.25 ? 0.05 * std::sin(F.Open / 0.25 * Pi) : 0.0;
			const double A = -0.03 + (0.52 + 0.03) * E - Snap;
			for (int32 I = 0; I < 2; ++I)
			{
				const int32 T = PartOf(M, I == 0 ? TEXT("fork_tines_0") : TEXT("fork_tines_1"));
				FNode N(M, T);
				const double Side = I == 0 ? -1.0 : 1.0;
				N.SetEuler(FVector(N.Euler.X, -Side * A, N.Euler.Z));
				Put(P, T, N);
			}
			const int32 Ore = PartOf(M, TEXT("fork_ore")), Hydrogen = PartOf(M, TEXT("fork_hydrogen"));
			const int32 Load = F.bHydrogen ? Hydrogen : Ore;
			SetVisible(P, F.bHydrogen ? Ore : Hydrogen, false);
			if (Load != INDEX_NONE)
			{
				SetVisible(P, Load, F.Load >= 0.02);
				FNode N(M, Load);
				N.Pos = FVector(0.27 + F.Slide, 0.01, 0);
				N.Scale = FVector(F.Load);
				Put(P, Load, N);
			}
			const int32 Arm = PartOf(M, TEXT("fork_arm")), Root = PartOf(M, TEXT("fork_root"));
			const double R = Smoothstep(0, 1, F.Reach);
			FNode A2(M, Arm);
			A2.Rot = FQuat::Slerp(S.ForkRest, S.ForkReached, R);
			Put(P, Arm, A2);
			FNode RN(M, Root);
			RN.Pos.X = 0.32 + 0.4 * R;
			Put(P, Root, RN);
			if (F.bBit && Ore != INDEX_NONE) P.Bite = ToRoot(M, P, Ore).GetTranslation();
		}

		// The drill stops while the fork picks a nodule off the pile.
		const bool bMining = (U.task == Task::mining || C.bMining) && !F.bPicking;
		const bool bWelding = U.task == Task::building || U.task == Task::repairing;
		const double Since = C.Cues.Shot ? Time - *C.Cues.Shot : 1.0;
		const double Jab = Since >= 0 && Since < 0.3 ? 1 - Since / 0.3 : 0.0;

		FNode RigN(M, S.Rig);
		RigN.Pos = FVector(Sway * 0.012, -std::abs(Sway) * 0.018, 0);
		RigN.SetEuler(FVector(bMining ? std::sin(Time * 23) * 0.003 : 0.0, 0,
			bMining ? std::sin(Time * 31) * 0.004 + std::sin(Time * 17) * 0.003 : Sway * 0.006));
		Put(P, S.Rig, RigN);

		const int32 DrillArm = PartOf(M, TEXT("drillArm")), Drill = PartOf(M, TEXT("drill"));
		FNode DA(M, DrillArm);
		DA.Pos.Z = -0.62 - 0.14 * Jab;
		Put(P, DrillArm, DA);
		if (bMining) S.DrillTurn = Time * 26;
		FNode DN(M, Drill);
		DN.SetEuler(FVector(DN.Euler.X, DN.Euler.Y, S.DrillTurn));
		DN.Pos.Z = -0.13 - (bMining ? 0.05 * (0.5 + 0.5 * std::sin(Time * 2.4)) : 0.0);
		Put(P, Drill, DN);

		// Sparks and the weld light at the bit's emitter.
		const double Rate = bMining || bWelding ? 140.0 : Jab > 0 ? 400.0 : 0.0;
		const int32 Emitter = PartOf(M, TEXT("light_1"));
		const FTransform At = Emitter != INDEX_NONE ? ToRoot(M, P, Emitter)
			: FTransform(AcSpace::FromSceneKit(0, 0, -0.62)) * ToRoot(M, P, DrillArm);
		if (Rate > 0)
		{
			FAcCockpitPose::FEmitter E;
			E.Kind = uint8(EAcParticle::WeldSparks);
			E.Frame = At;
			E.Rate = Rate;
			E.Size = 0.018;
			E.Speed = 1.6;
			P.Emitters.Add(E);
		}
		if (bMining || bWelding || Jab > 0)
		{
			P.Lights.Add(Light(At.GetTranslation(), FLinearColor(1, 0.7, 0.35),
				30 + 20 * std::sin(Time * 47) * std::sin(Time * 13), 2.2));
		}
	}

	// --- Ranger -----------------------------------------------------------------

	void PoseRanger(FState& S, const FAcCockpitContext& C, FAcCockpitPose& P)
	{
		const FAcModelInfo& M = *S.Model;
		const ac::Unit& U = C.Unit;
		const double Time = C.Cues.Time;
		const bool bWalking = U.moving.value_or(false);
		const double Sway = bWalking ? std::sin(U.stride * 3.2) : 0.0;
		const double Since = C.Cues.Shot ? Time - *C.Cues.Shot : 1.0;

		const int32 Gun = PartOf(M, TEXT("gun")), Rifle = PartOf(M, TEXT("rifle")),
			Minigun = PartOf(M, TEXT("minigun_root")), Barrels = PartOf(M, TEXT("minigun_barrels")),
			Blur = PartOf(M, TEXT("minigun_blur")), MuzzleI = PartOf(M, TEXT("muzzle")), Flash = PartOf(M, TEXT("flash")),
			Shield = PartOf(M, TEXT("shield"));
		const bool bMini = C.Cues.bMinigun;
		SetVisible(P, Minigun, bMini);
		SetVisible(P, Rifle, !bMini);
		SetVisible(P, Shield, C.Cues.bShield);
		// The muzzle moves to the shown gun's.
		FNode Mz(M, MuzzleI);
		if (bMini && Minigun != INDEX_NONE) Mz.Pos = FNode(M, Minigun).ToParent(FVector(0.65, 0.005, 0));
		Put(P, MuzzleI, Mz);

		double Kick = Recoil(Since, 0.12);
		double Tip = 0.07 * Kick;
		if (bMini)
		{
			// `GameScene.spinMinigun`.
			const bool bFiring = Since >= 0 && Since < 0.25;
			FSpin& R = S.Spin;
			const double Dt = FMath::Min(0.1, FMath::Max(0.0, Time - R.Last.Get(Time)));
			R.Last = Time;
			R.Rate = bFiring ? R.Rate + (MinigunSpin - R.Rate) * FMath::Min(1.0, Dt * 14) : R.Rate * std::exp(-Dt * 2.6);
			R.Angle = std::remainder(R.Angle + FMath::Min(R.Rate * Dt, 0.35 * Pi / 3), 2 * Pi);
			R.Heat = bFiring ? FMath::Min(1.0, R.Heat + Dt / 3) : FMath::Max(0.0, R.Heat - Dt / 2.5);
			FNode B(M, Barrels);
			B.SetEuler(FVector(R.Angle, B.Euler.Y, B.Euler.Z));
			Put(P, Barrels, B);
			// The tips glow (minigun_heat lit at 1 by make_infantry_materials.py),
			// the smear's opacity is custom data 1 (M_AcSmear).
			if (Barrels != INDEX_NONE) P.MeshEmission.Add({Barrels, FName(TEXT("minigun_heat")), float(2.6 * R.Heat * R.Heat)});
			const double Smear = 0.6 * Smoothstep(8, MinigunSpin, R.Rate);
			SetVisible(P, Blur, Smear >= 0.02);
			SetEmission(P, Blur, Smear);
			const bool bShooting = bFiring && R.Rate > MinigunSpin * 0.5;
			Kick = bShooting ? 0.25 + 0.2 * std::sin(Time * 173) : 0.0;
			Tip = bShooting ? 0.006 * std::sin(Time * 131) : 0.0;
		}

		FNode RigN(M, S.Rig);
		RigN.Pos = FVector(Sway * 0.014, -std::abs(Sway) * 0.016, 0);
		RigN.SetEuler(FVector(RigN.Euler.X, RigN.Euler.Y, Sway * 0.008));
		Put(P, S.Rig, RigN);

		// Its line of fire through what the crosshair is on.
		const FNode G(M, Gun);
		const TOptional<FVector> Target = InRig(M, P, S.Rig, C.AimRoot);
		const FQuat Aim = Target ? AimTurn(G.Pos, G.Rot, G.Scale, Mz.Pos, FVector(0, 0, -1), *Target) : FQuat::Identity;
		const FQuat Held = Aim * G.Rot;
		FNode GN = G;
		GN.Pos = G.Pos + Held.RotateVector(FVector(0, 0.004, 0.05)) * Kick;
		GN.Rot = Held * SkAxis(SkXAxis, Tip);
		Put(P, Gun, GN);

		const bool bLit = Since >= 0 && Since < 0.06;
		SetVisible(P, Flash, bLit);
		FNode FN(M, Flash);
		FN.SetEuler(FVector(FN.Euler.X, FN.Euler.Y, Time * 97));
		Put(P, Flash, FN);
		if (bLit && MuzzleI != INDEX_NONE)
		{
			P.Lights.Add(Light(ToRoot(M, P, MuzzleI).GetTranslation(), FLinearColor(1, 0.8, 0.45), 24, 0.4));
		}
		P.Muzzles.Add(MuzzleI);
	}

	// --- Comet --------------------------------------------------------------------

	void PoseComet(FState& S, const FAcCockpitContext& C, FAcCockpitPose& P)
	{
		const FAcModelInfo& M = *S.Model;
		const ac::Unit& U = C.Unit;
		const double Time = C.Cues.Time, Since = C.Cues.Since;
		const TOptional<double> Jump = C.Jump;
		// The pistols swing a little with the stride (the mask stays put).
		const bool bWalking = U.moving.value_or(false) && !Jump;
		const double Sway = bWalking ? std::sin(U.stride * 4.6) : 0.0;
		FNode RigN(M, S.Rig);
		RigN.Pos = FVector(Sway * 0.005, -Sway * Sway * 0.006, 0);
		RigN.SetEuler(FVector(RigN.Euler.X, RigN.Euler.Y, Sway * 0.003));
		Put(P, S.Rig, RigN);
		// Through a jump the pistols dip and the jetpack roars below.
		const TOptional<double> J = Jump ? TOptional<double>(FMath::Clamp(*Jump, 0.0, 1.0)) : TOptional<double>();
		const double Dip = J ? std::sin(Pi * *J) : 0.0;
		const TOptional<FVector> Target = InRig(M, P, S.Rig, C.AimRoot);
		for (int32 K = 0; K < S.Held.Num(); ++K)
		{
			const double Sk = Since - double(K) * 0.06;
			const double Kick = Recoil(Sk, 0.13);
			S.Held[K].Pose(P, 0.045 * Kick, 0.16 * Kick - 0.08 * Dip, FVector(0, -0.015 * Dip, 0), Target);
			const int32 Fl = PartOf(M, K == 0 ? TEXT("flashes_0") : TEXT("flashes_1"));
			SetVisible(P, Fl, Sk >= 0 && Sk < 0.05);
			FNode FN(M, Fl);
			FN.SetEuler(FVector(FN.Euler.X, Sk * 97, FN.Euler.Z));
			Put(P, Fl, FN);
		}
		Muzzles(M, P, {TEXT("flashes_0"), TEXT("flashes_1")});
		if (Since >= 0 && Since < 0.11 && P.Muzzles[0] != INDEX_NONE)
		{
			P.Lights.Add(Light(ToRoot(M, P, P.Muzzles[0]).GetTranslation(), FLinearColor(1, 0.75, 0.4), 24, 0.14));
		}
		const int32 Jet = PartOf(M, TEXT("jet"));
		SetVisible(P, Jet, J.IsSet());
		if (!J) return;
		const double Flicker = 0.5 + 0.5 * std::sin(Time * 23) * std::sin(Time * 7.3);
		const double Thrust = 0.75 + 0.25 * std::abs(std::cos(Pi * *J)) + 0.08 * Flicker;
		for (int32 K = 0; K < 3; ++K)
		{
			const int32 H = PartOf(M, *FString::Printf(TEXT("jetHalos_%d"), K));
			FNode N(M, H);
			N.Scale = FVector(0.8 + 0.35 * Thrust + 0.08 * std::sin(Time * 31 + double(K) * 2));
			Put(P, H, N);
			// The halo's opacity: additive, so it scales the glow.
			SetEmission(P, H, FMath::Min(1.0, Thrust));
		}
		if (const int32 L = PartOf(M, TEXT("light_2")); L != INDEX_NONE)
		{
			P.Lights.Add(Light(ToRoot(M, P, L).GetTranslation(), FLinearColor(1, 0.55, 0.2), 30 * Thrust, 0.6));
		}
		// The jetpack shudders the frame.
		P.Lean.Shake = FVector2f(float(0.004 * Thrust * std::sin(Time * 47)), float(0.003 * Thrust * std::sin(Time * 39)));
	}

	// --- Juggernaut ----------------------------------------------------------------

	void PoseJuggernaut(FState& S, const FAcCockpitContext& C, FAcCockpitPose& P)
	{
		const FAcModelInfo& M = *S.Model;
		const ac::Unit& U = C.Unit;
		const double Since = C.Cues.Since;
		const bool bWalking = U.moving.value_or(false);
		// A heavy stomp: a drop onto each foot.
		const double Ph = U.stride * 2.7;
		const double Stomp = bWalking ? std::abs(std::cos(Ph)) : 1.0;
		const double Sway = bWalking ? std::sin(Ph) : 0.0;
		FNode RigN(M, S.Rig);
		RigN.Pos = FVector(Sway * 0.016, (Stomp - 1) * 0.02, 0);
		RigN.SetEuler(FVector(RigN.Euler.X, RigN.Euler.Y, Sway * 0.01));
		Put(P, S.Rig, RigN);
		const TOptional<FVector> Target = InRig(M, P, S.Rig, C.AimRoot);
		Muzzles(M, P, {TEXT("muzzles_0"), TEXT("muzzles_1")});
		for (int32 K = 0; K < S.Held.Num(); ++K)
		{
			// Right, then the left 0.07 s later.
			const double Sk = Since - double(K) * 0.07;
			const bool bFresh = Sk >= 0 && Sk < 1;
			const double Kick = bFresh ? std::exp(-Sk * 11) : 0.0;
			const double Settle = bFresh ? std::exp(-Sk * 5) * std::sin(Sk * 20) : 0.0;
			S.Held[K].Pose(P, 0.09 * Kick, 0.22 * Kick + 0.02 * Settle, FVector::ZeroVector, Target);
			const int32 Fl = PartOf(M, K == 0 ? TEXT("flashes_0") : TEXT("flashes_1"));
			SetVisible(P, Fl, Sk >= 0 && Sk < 0.08);
			FNode FN(M, Fl);
			FN.Scale = FVector(0.8 + FMath::Max(0.0, Sk) * 8);
			FN.SetEuler(FVector(Sk * 97, FN.Euler.Y, FN.Euler.Z));
			Put(P, Fl, FN);
			if (Sk >= 0 && Sk < 0.14 && P.Muzzles[K] != INDEX_NONE)
			{
				FAcCockpitPose::FEmitter E;
				E.Kind = uint8(EAcParticle::MuzzleSmoke);
				E.Frame = ToRoot(M, P, P.Muzzles[K]);
				E.Rate = 110;
				P.Emitters.Add(E);
			}
		}
		if (Since >= 0 && Since < 0.12 && P.Muzzles[0] != INDEX_NONE)
		{
			P.Lights.Add(Light(ToRoot(M, P, P.Muzzles[0]).GetTranslation(), FLinearColor(1, 0.75, 0.4), 30, 0.2));
		}
		// Each footfall jolts the view; the volley rocks it back.
		const double Jolt = bWalking ? std::exp(-8 * (1 - std::abs(std::cos(Ph)))) : 0.0;
		const double Rock = Since >= 0 && Since < 0.6 ? std::exp(-Since * 9) : 0.0;
		P.Lean.Lift = float(-0.012 * Jolt);
		P.Lean.Shake = FVector2f(float(0.004 * Jolt + 0.02 * Rock), 0.f);
	}

	// --- Firefly -----------------------------------------------------------------

	void PoseFirefly(FState& S, const FAcCockpitContext& C, FAcCockpitPose& P)
	{
		const FAcModelInfo& M = *S.Model;
		const ac::Unit& U = C.Unit;
		Unpitch(S, C, P);
		const bool bMoving = U.moving.value_or(false);
		const double Pace = FMath::Min(1.0, C.Cues.Speed / FMath::Max(U.stats().speed, 0.1));
		const double Steer = FMath::Clamp(C.Cues.Turn / 4, -1.0, 1.0);
		// The hull turned under the view by the tail's angle to it, about the
		// eye (in the seat at the cockpit's origin).
		const double D = Rem2Pi(U.look() - U.heading);
		PoseUnitModel(S, C, P, ModelRootNode(S, UnitKind::firefly, D));
		// The chassis' bob is under the eye: a third of it.
		Damp(P, PartOf(M, TEXT("model_body")), 0.3);
		// From the seat the jet is slimmer and dimmer, with no nozzle flare.
		SetVisible(P, PartOf(M, TEXT("model_flare")), false);
		static const TCHAR* const Flames[] = {TEXT("model_flameLayers_0"), TEXT("model_flameLayers_1"),
			TEXT("model_flameLayers_2"), TEXT("model_flameEnd")};
		for (int32 K = 0; K < 4; ++K)
		{
			const int32 I = PartOf(M, Flames[K]);
			if (I == INDEX_NONE) continue;
			P.Emission[I] *= 0.55f;
			for (FAcPose::FMeshEmission& E : P.MeshEmission)
			{
				if (E.Part == I) E.Value *= 0.55f;
			}
			const FVector Sc = P.Local[I].GetScale3D();
			P.Local[I].SetScale3D(K < 3 ? FVector(Sc.X, Sc.Y * 0.5, Sc.Z * 0.5) : FVector(Sc.X * 0.5));
		}
		Muzzles(M, P, {TEXT("model_nozzle")});
		// The flame's light, at a third (it washed out the sight from here).
		if (const FAcVehicleFx* Fx = AcPoseVehicles::Fx(UnitKind::firefly, S.UnitMemory); Fx && Fx->FlameLight > 0)
		{
			if (const int32 L = PartOf(M, TEXT("model_flameLightNode")); L != INDEX_NONE && P.Visible[L])
			{
				// Swift's sRGB colour in linear light, as C6's flame lamp: deep
				// orange, so the hull and the ground warm instead of going white.
				static const FLinearColor Flame = FLinearColor::FromSRGBColor(FColor(255, 140, 51));
				P.Lights.Add(Light(ToRoot(M, P, L).GetTranslation(), Flame,
					CVarFlameLight.GetValueOnGameThread() * Fx->FlameLight, Fx->FlameLightRange));
			}
		}
		// A gentle swell that grows with speed; a kick as the flame lights.
		const double Since = C.Cues.Since;
		const double Burn = AcPoseVehicles::FireflyBurn;
		const double Bounce = Pace * 0.006 * std::sin(U.stride * 1.7);
		const bool bBurning = Since >= 0 && Since < Burn;
		const double Kick = bBurning ? Smoothstep(0, 0.05, Since) * (1 - Smoothstep(0.1, Burn, Since)) : 0.0;
		P.Lean.Roll = float(Steer * (bMoving ? 0.035 : 0.01));
		P.Lean.Lift = float(Bounce);
		P.Lean.Shake = FVector2f(float(0.012 * Kick), 0.f);
	}

	// --- Longbow -----------------------------------------------------------------

	void PoseLongbow(FState& S, const FAcCockpitContext& C, FAcCockpitPose& P)
	{
		const FAcModelInfo& M = *S.Model;
		const ac::Unit& U = C.Unit;
		const double Time = C.Cues.Time, Since = C.Cues.Since;
		Unpitch(S, C, P);
		const double An = FMath::Clamp(U.anchor.value_or(0.0), 0.0, 1.0);
		// `Longbow.hullDrop(.rhino)`: the hull rises on its legs.
		const double Drop = 0.22 * Smoothstep(0.45, 0.7, An);
		const double D = Rem2Pi(U.look() - U.heading);
		FNode Root = ModelRootNode(S, UnitKind::longbow, D);
		Root.Pos.Y -= Drop;
		PoseUnitModel(S, C, P, Root);
		SetVisible(P, PartOf(M, TEXT("model_turret")), false);
		// The cannon stays put and near level: its rise and most of its
		// anchored pitch taken off (`trunnion.1` = 0.5, pitch × 0.15).
		if (const int32 G = PartOf(M, TEXT("model_anchorGun")); G != INDEX_NONE && S.UnitPart[G] != INDEX_NONE)
		{
			FTransform L = S.UnitPosed.Local[S.UnitPart[G]];
			FVector T = L.GetTranslation();
			T.Z = AcSpace::ToCm(0.5);
			L.SetTranslation(T);
			FQuat Swing, Twist;
			L.GetRotation().ToSwingTwist(FVector(0, 1, 0), Swing, Twist);
			FVector Axis;
			float Angle;
			Twist.ToAxisAndAngle(Axis, Angle);
			L.SetRotation(Swing * FQuat(Axis, Angle * 0.15));
			P.Local[G] = S.bAdjust[G] ? FTransform(L.ToMatrixWithScale() * S.Adjust[G]) : L;
		}
		if (U.moving.value_or(false) && An < 0.001) Damp(P, PartOf(M, TEXT("model_hull")), 0.3);
		Muzzles(M, P, {TEXT("model_tankMuzzles_0"), TEXT("model_tankMuzzles_0"), TEXT("model_anchorMuzzle")});
		const bool bBusy = An > 0.001 && An < 0.999;
		FVector2f Shake(0, 0);
		if (bBusy) Shake += FVector2f(float(std::sin(Time * 41)), float(std::sin(Time * 29 + 1))) * 0.005f;
		const double Land = An - 0.7;
		double Lift = Drop;
		if (Land > 0 && Land < 0.1) Lift += 0.02 * std::sin(Land / 0.1 * Pi);
		if (Since >= 0 && Since < 0.8)
		{
			const double Decay = std::exp(-Since * (An > 0.5 ? 6 : 12));
			const double Size = An > 0.5 ? 0.03 : 0.008;
			Shake += FVector2f(float(Size * Decay * (1 + std::sin(Since * 45))), float(Size * 0.4 * Decay * std::sin(Since * 70)));
		}
		P.Lean.Lift = float(Lift);
		P.Lean.Shake = Shake;
	}

	// --- Hailstorm ----------------------------------------------------------------

	void PoseHailstorm(FState& S, const FAcCockpitContext& C, FAcCockpitPose& P)
	{
		const FAcModelInfo& M = *S.Model;
		const ac::Unit& U = C.Unit;
		Unpitch(S, C, P);
		const double D = Rem2Pi(U.look() - U.heading);
		FNode Root = ModelRootNode(S, UnitKind::hailstorm, D);
		const double Base = Root.Pos.Y;
		const double Bob = PoseUnitModel(S, C, P, Root);
		// The suspension's bob is right under the seat: a third of it.
		if (U.moving.value_or(false) && Bob != 0)
		{
			Root.Pos.Y = Base + 0.3 * Bob;
			Put(P, S.ModelRoot, Root);
		}
		// The cradle hung on `gun`, its own raise held at 0.
		const int32 Cradle = PartOf(M, TEXT("model_cradle"));
		FNode CN(M, Cradle);
		CN.Pos = FVector::ZeroVector;
		CN.SetEuler(FVector(CN.Euler.X, CN.Euler.Y, 0));
		Put(P, Cradle, CN);
		// `gun` rises with the view but never dips below the mount's level.
		const int32 Gun = PartOf(M, TEXT("gun"));
		FNode GN(M, Gun);
		const FQuat Hold = SkAxis(SkXAxis, FMath::Max(0.0, -C.Cues.Pitch));
		GN.Pos = Hold.RotateVector(GN.Pos);
		GN.Rot = Hold * GN.Rot;
		Put(P, Gun, GN);
		Muzzles(M, P, {TEXT("model_muzzles_0"), TEXT("model_muzzles_1"), TEXT("model_muzzles_2"), TEXT("model_muzzles_3")});
		// Each pair's shot shakes the mount.
		FVector2f Shake(0, 0);
		for (int32 K = 0; K < 2; ++K)
		{
			const double Sk = C.Cues.Since - double(K) * 0.1;
			if (Sk >= 0 && Sk < 0.25)
			{
				const double Decay = std::exp(-Sk * 16);
				Shake += FVector2f(float(0.006 * Decay * std::sin(Sk * 70)), float(0.003 * Decay * std::sin(Sk * 95)));
			}
		}
		P.Lean.Shake = Shake;
	}


	// --- Peregrine, Atlas, Scorpion (the cockpits `cockpit_<kind>_blue`) -----------
	// Each carries the unit's own model on `model_<part>` copies, posed by the
	// kind's pose function as the Longbow's and the Hailstorm's do (the parts a
	// cockpit leaves out are simply not there), under the eye at `rig`. The
	// muzzles are the cockpit's own `muzzles_0`, `muzzles_1` (`muzzle` for the
	// Scorpion), else the model's rails, barrels and launcher.

	/// The cockpit's muzzle part `Own`, else `Model`'s copy of the unit part.
	void MuzzlesOr(const FAcModelInfo& M, FAcCockpitPose& P, std::initializer_list<std::pair<const TCHAR*, const TCHAR*>> Names)
	{
		for (const std::pair<const TCHAR*, const TCHAR*>& N : Names)
		{
			const int32 Own = PartOf(M, N.first);
			P.Muzzles.Add(Own != INDEX_NONE ? Own : PartOf(M, N.second));
		}
	}

	void PosePeregrineCockpit(FState& S, const FAcCockpitContext& C, FAcCockpitPose& P)
	{
		const FAcModelInfo& M = *S.Model;
		const ac::Unit& U = C.Unit;
		const double T = C.Cues.Time, Since = C.Cues.Since;
		Unpitch(S, C, P);
		// The nose, wings and fins under the canopy, at the hover.
		PoseUnitModel(S, C, P, ModelRootNode(S, UnitKind::peregrine, 0.0));
		MuzzlesOr(M, P, {{TEXT("muzzles_0"), TEXT("model_missiles_0")}, {TEXT("muzzles_1"), TEXT("model_missiles_3")}});
		// A harder bank than the Kestrel's (the airframe rolls to 60 degrees).
		const double Bank = AcPoseNew::PeregrineBank(C.Cues.Turn) / AcPoseNew::PeregrineMaxBank;
		FVector2f Shake(U.moving.value_or(false) ? 0.f : float(0.005 * std::sin(T * 1.1)), 0.f);
		if (Since >= 0 && Since < 0.3)
		{
			const double Decay = std::exp(-Since * 14);
			Shake += FVector2f(float(0.005 * Decay * std::sin(Since * 60)), float(0.0025 * Decay * std::sin(Since * 85)));
		}
		P.Lean.Roll = float(-0.45 * Bank);
		P.Lean.Lift = float(0.05 * std::sin(T * 1.7) + 0.02 * std::sin(T * 3.1 + 1));
		P.Lean.Shake = Shake;
	}

	void PoseAtlasCockpit(FState& S, const FAcCockpitContext& C, FAcCockpitPose& P)
	{
		const FAcModelInfo& M = *S.Model;
		const ac::Unit& U = C.Unit;
		const double Since = C.Cues.Since;
		Unpitch(S, C, P);
		// The torso turns under the view by its angle to the legs, about its axis.
		const double D = Rem2Pi(U.look() - U.heading);
		FNode Root = ModelRootNode(S, UnitKind::atlas, D);
		const double Base = Root.Pos.Y;
		const double Bob = PoseUnitModel(S, C, P, Root);
		// The body's bob and its stomp's drop are right under the seat: part of them.
		if (Bob != 0)
		{
			Root.Pos.Y = Base + 0.5 * Bob;
			Put(P, S.ModelRoot, Root);
		}
		MuzzlesOr(M, P, {{TEXT("muzzles_0"), TEXT("model_cannons_0")}, {TEXT("muzzles_1"), TEXT("model_cannons_1")}});
		// The barrels pitch at the aim, but from the slit they stay in view: a third of
		// their pitch (the way the Longbow's cannon keeps near level), the recoil whole.
		for (const TCHAR* Name : {TEXT("model_cannons_0"), TEXT("model_cannons_1")})
		{
			const int32 I = PartOf(M, Name);
			if (I == INDEX_NONE || !P.Local.IsValidIndex(I)) continue;
			P.Local[I].SetRotation(FQuat::Slerp(M.Parts[I].Rest.GetRotation(), P.Local[I].GetRotation(), 0.3));
		}
		// Each footfall jolts the view, the volley rocks it back, the stomp drops it.
		const bool bWalking = U.moving.value_or(false);
		const double Cycle = U.stride / AcPoseNew::AtlasCycle;
		const double Jolt = bWalking ? std::exp(-10.0 * (1.0 - std::abs(std::cos(Pi * Cycle)))) : 0.0;
		double Rock = 0.0;
		for (int32 K = 0; K < 2; ++K)
		{
			const double Sk = Since - double(K) * AcPoseNew::AtlasSecondDelay;
			if (Sk >= 0 && Sk < 0.8) Rock += std::exp(-Sk * 8) * (K == 0 ? 0.02 : 0.015);
		}
		const double Stomp = U.stompedAt ? C.Sim.state.time - *U.stompedAt : -1.0;
		const double After = Stomp - AcPoseNew::AtlasStompImpact;
		const double Quake = After >= 0 && After < 1.0 ? std::exp(-After * 6) : 0.0;
		P.Lean.Lift = float(-0.025 * Jolt - 0.008 * Quake * std::cos(After * 13));
		P.Lean.Shake = FVector2f(float(0.006 * Jolt + Rock + 0.03 * Quake * std::sin(After * 31)), float(0.01 * Quake * std::sin(After * 47)));
		P.Lean.Roll = float(0.012 * std::sin(Pi * 2.0 * Cycle) * (bWalking ? 1.0 : 0.0));
	}

	void PoseScorpionCockpit(FState& S, const FAcCockpitContext& C, FAcCockpitPose& P)
	{
		const FAcModelInfo& M = *S.Model;
		const ac::Unit& U = C.Unit;
		const double Anchor = FMath::Clamp(U.anchor.value_or(0.0), 0.0, 1.0);
		Unpitch(S, C, P);
		PoseUnitModel(S, C, P, ModelRootNode(S, UnitKind::scorpion, 0.0));
		MuzzlesOr(M, P, {{TEXT("muzzle"), TEXT("model_launcher")}});
		// The crawl's sway; down with the body as it buries, up as it digs out.
		const bool bWalking = U.moving.value_or(false) && Anchor <= 0.0;
		const double Cycle = U.stride / AcPoseNew::ScorpionCycle;
		const double Time = C.Cues.Time;
		P.Lean.Lift = float(-AcSpace::ToCells(AcPoseNew::ScorpionSink(Anchor)) + (bWalking ? 0.012 * std::abs(std::sin(Pi * 2.0 * Cycle)) : 0.0));
		P.Lean.Roll = float(bWalking ? 0.02 * std::sin(Pi * 2.0 * Cycle) : 0.0);
		const double Dig = Anchor > 0.0 && Anchor < 1.0 ? 1.0 : 0.0;
		P.Lean.Shake = FVector2f(float(0.004 * Dig * std::sin(Time * 17)), float(0.003 * Dig * std::sin(Time * 23)));
		// The sting's snap kicks the view.
		if (C.Cues.Since >= 0 && C.Cues.Since < 0.4)
		{
			P.Lean.Shake += FVector2f(float(0.012 * std::exp(-C.Cues.Since * 12)), 0.f);
		}
	}

	// --- Dropship ------------------------------------------------------------------

	void PoseDropship(FState& S, const FAcCockpitContext& C, FAcCockpitPose& P)
	{
		const FAcModelInfo& M = *S.Model;
		const ac::Unit& U = C.Unit;
		const double T = C.Cues.Time;
		const double E = U.energy.value_or(0.0) / ac::Rules::maxEnergy;
		const int32 Bars = int32(std::ceil(E * 10.0));
		for (int32 K = 0; K < 10; ++K)
		{
			const int32 I = PartOf(M, *FString::Printf(TEXT("energy_%d"), K));
			if (I != INDEX_NONE && K < Bars && S.Lamps.EnergyLit) P.Swaps.Add({I, S.Lamps.EnergyLit});
		}
		for (int32 K = 0; K < 12; ++K)
		{
			const int32 I = PartOf(M, *FString::Printf(TEXT("cargo_%d"), K));
			if (I == INDEX_NONE) continue;
			SetVisible(P, I, K < C.Cues.Room);
			if (K < C.Cues.Slots && S.Lamps.CargoLit) P.Swaps.Add({I, S.Lamps.CargoLit});
		}
		if (S.Rig != INDEX_NONE)
		{
			P.MeshEmission.Add({S.Rig, FName(TEXT("screens")), float((0.9 + 0.12 * std::sin(T * 3.1) * std::sin(T * 7.7)) / 0.9)});
		}
		// The heal beam (`poseBeam`), thinner than the model's.
		const int32 Emitter = PartOf(M, TEXT("emitter")), Beam = PartOf(M, TEXT("beam"));
		constexpr double LensExported = 0.7, CoreExported = 2.2;
		bool bBeam = false;
		if (C.BeamRoot && Emitter != INDEX_NONE && Beam != INDEX_NONE)
		{
			const FVector O = FNode(M, Emitter).Pos;
			const FVector Dv = InPart(M, P, S.Rig, *C.BeamRoot) - O;
			const double Len = Dv.Size();
			if (Len > 0.05)
			{
				bBeam = true;
				FNode BN(M, Beam);
				BN.Pos = O;
				BN.Rot = FQuat::FindBetweenNormals(SkYAxis, Dv / Len);
				Put(P, Beam, BN);
				const double Pulse = 0.5 + 0.5 * std::sin(T * 9);
				constexpr double Thin = 0.3;
				const double W = (0.85 + 0.3 * Pulse) * Thin;
				const int32 Shaft = PartOf(M, TEXT("shaft"));
				FNode SN(M, Shaft);
				SN.Scale = FVector(W, Len, W);
				Put(P, Shaft, SN);
				if (Shaft != INDEX_NONE)
				{
					P.MeshEmission.Add({Shaft, FName(TEXT("glow_bcfbbe")), float((1.6 + 1.2 * Pulse) / CoreExported)});
				}
				for (int32 K = 0; K < 3; ++K)
				{
					const int32 I = PartOf(M, *FString::Printf(TEXT("pulse%d"), K));
					const double Ph = std::fmod(T * 1.4 + double(K) / 3, 1.0);
					FNode N(M, I);
					N.Pos = FVector(0, Ph * Len, 0);
					const double Sc = std::sin(Ph * Pi) * (Thin + (1 - Thin) * Ph);
					N.Scale = FVector(Sc, 1.8 * Sc, Sc);
					Put(P, I, N);
				}
				const int32 Splash = PartOf(M, TEXT("splash"));
				FNode SpN(M, Splash);
				SpN.Pos = FVector(0, Len, 0);
				SpN.Scale = FVector(0.5 + 0.2 * Pulse);
				SpN.SetEuler(FVector(SpN.Euler.X, T * 2, SpN.Euler.Z));
				Put(P, Splash, SpN);
				SetEmission(P, Emitter, (1.2 + 0.8 * Pulse) / LensExported);
			}
		}
		SetVisible(P, Beam, bBeam);
		if (!bBeam) SetEmission(P, Emitter, 0.8 / LensExported);
		Muzzles(M, P, {TEXT("emitter")});
		// A slow bob at the hover; a bank into turns.
		const double Bank = FMath::Clamp(C.Cues.Turn / 2.5, -1.0, 1.0);
		P.Lean.Roll = float(-0.22 * Bank);
		P.Lean.Lift = float(0.05 * std::sin(T * 1.3) + 0.02 * std::sin(T * 2.7 + 1));
		P.Lean.Shake = FVector2f(U.moving.value_or(false) ? 0.f : float(0.006 * std::sin(T * 0.9)), 0.f);
	}

	// --- Kestrel -----------------------------------------------------------------

	void PoseKestrel(FState& S, const FAcCockpitContext& C, FAcCockpitPose& P)
	{
		const FAcModelInfo& M = *S.Model;
		const ac::Unit& U = C.Unit;
		const double T = C.Cues.Time, Since = C.Cues.Since;
		const int32 Sweep = PartOf(M, TEXT("sweep"));
		FNode SN(M, Sweep);
		SN.SetEuler(FVector(SN.Euler.X, SN.Euler.Y, -T * 2.6));
		Put(P, Sweep, SN);
		// A pod's lamps go out as it fires and light again one by one.
		for (int32 K = 0; K < 2; ++K)
		{
			const double Sk = Since - double(K) * 0.08;
			const int32 Back = Sk >= 0 && Sk < 0.6 ? int32(4.0 * Sk / 0.6) : 4;
			for (int32 I = 0; I < 4; ++I)
			{
				const int32 L = PartOf(M, *FString::Printf(TEXT("rockets_%d_%d"), K, I));
				if (L != INDEX_NONE && I >= Back && S.Lamps.Unlit) P.Swaps.Add({L, S.Lamps.Unlit});
			}
		}
		if (S.Rig != INDEX_NONE)
		{
			P.MeshEmission.Add({S.Rig, FName(TEXT("screens")), float((0.9 + 0.1 * std::sin(T * 3.3) * std::sin(T * 8.1)) / 0.9)});
			P.MeshEmission.Add({S.Rig, FName(TEXT("hud")), float((0.8 + 0.08 * std::sin(T * 5.7)) / 0.8)});
		}
		Muzzles(M, P, {TEXT("muzzles_0"), TEXT("muzzles_1")});
		const double Bank = FMath::Clamp(C.Cues.Turn / 2.5, -1.0, 1.0);
		FVector2f Shake(U.moving.value_or(false) ? 0.f : float(0.005 * std::sin(T * 1.1)), 0.f);
		if (Since >= 0 && Since < 0.3)
		{
			const double Decay = std::exp(-Since * 14);
			Shake += FVector2f(float(0.006 * Decay * std::sin(Since * 60)), float(0.003 * Decay * std::sin(Since * 85)));
		}
		P.Lean.Roll = float(-0.26 * Bank);
		P.Lean.Lift = float(0.05 * std::sin(T * 1.7) + 0.02 * std::sin(T * 3.1 + 1));
		P.Lean.Shake = Shake;
	}
}

void AcCockpitKinds::Pose(UAcCockpitSubsystem::FState& S, const FAcCockpitContext& C, FAcCockpitPose& P)
{
	switch (S.Kind)
	{
	case UnitKind::prospector: PoseProspector(S, C, P); break;
	case UnitKind::ranger: PoseRanger(S, C, P); break;
	case UnitKind::comet: PoseComet(S, C, P); break;
	case UnitKind::juggernaut: PoseJuggernaut(S, C, P); break;
	case UnitKind::firefly: PoseFirefly(S, C, P); break;
	case UnitKind::longbow: PoseLongbow(S, C, P); break;
	case UnitKind::dropship: PoseDropship(S, C, P); break;
	case UnitKind::kestrel: PoseKestrel(S, C, P); break;
	case UnitKind::hailstorm: PoseHailstorm(S, C, P); break;
	case UnitKind::peregrine: PosePeregrineCockpit(S, C, P); break;
	case UnitKind::atlas: PoseAtlasCockpit(S, C, P); break;
	case UnitKind::scorpion: PoseScorpionCockpit(S, C, P); break;
	}
}
