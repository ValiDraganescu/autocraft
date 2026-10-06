#include "AcPoseInfantry.h"

#include "AcModelCatalog.h"
#include "AcSpace.h"

#include "Misc/ScopeLock.h"

#include <cmath>

namespace
{
	using ac::UnitKind;
	using Task = ac::Unit::Task;

	constexpr double Pi = UE_DOUBLE_PI;

	double Smoothstep(const double A, const double B, const double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	// --- SceneKit-frame helpers -------------------------------------------

	/// A rotation about a SceneKit axis (a SceneKit quaternion held in an FQuat).
	FQuat SkAxis(const double X, const double Y, const double Z, const double Angle)
	{
		return FQuat(FVector(X, Y, Z), Angle);
	}
	FQuat SkX(const double A) { return SkAxis(1, 0, 0, A); }
	FQuat SkY(const double A) { return SkAxis(0, 1, 0, A); }
	FQuat SkZ(const double A) { return SkAxis(0, 0, 1, A); }

	FQuat ToUE(const FQuat& Sk) { return AcSpace::QuatFromSceneKit(Sk.X, Sk.Y, Sk.Z, Sk.W); }

	/// A SceneKit node transform → the part's Unreal local transform.
	FTransform Sk(const FVector& Pos, const FQuat& Rot, const FVector& Scale)
	{
		return AcSpace::TransformFromSceneKit(Pos, Rot, Scale);
	}

	/// The part's rest position and scale, turned by `Rot` (SceneKit).
	FTransform SkTurned(const FAcModelPart& Part, const FQuat& Rot)
	{
		return Sk(Part.SkPosition, Rot, Part.SkScale);
	}

	/// Turning by `Angle` about SceneKit's Z axis through `Pivot` (cells),
	/// as an Unreal transform applied after a part's own (`RangerShin.bend`'s
	/// `about`).
	FTransform AboutZ(const FVector& PivotSk, const double Angle)
	{
		const FQuat Q = ToUE(SkZ(Angle));
		const FVector P = AcSpace::FromSceneKit(PivotSk.X, PivotSk.Y, PivotSk.Z);
		return FTransform(Q, P - Q.RotateVector(P));
	}

	/// A part's transform relative to the placement this frame (the chain of
	/// local transforms up to the root, then the placement).
	FTransform PartWorld(const FAcModelInfo& M, const FAcPose& P, int32 I)
	{
		FTransform T = FTransform::Identity;
		while (I != INDEX_NONE)
		{
			T = T * P.Local[I];
			I = M.Parts[I].Parent;
		}
		return T * P.Placement;
	}

	// --- Legs ---------------------------------------------------------------

	/// One model's legs: the hips (`legs[i]`) and the shin parts that turn
	/// about the knee (`lower`) and about the knee and the ankle (`foot`).
	struct FLegs
	{
		int32 Hip[2] = {INDEX_NONE, INDEX_NONE};
		TArray<int32> Lower[2];
		TArray<int32> Foot[2];
	};

	const FLegs& LegsOf(const FAcModelInfo& M)
	{
		static FCriticalSection Lock;
		static TMap<const FAcModelInfo*, TUniquePtr<FLegs>> Cache;
		FScopeLock Guard(&Lock);
		if (const TUniquePtr<FLegs>* Found = Cache.Find(&M)) return **Found;
		TUniquePtr<FLegs> L = MakeUnique<FLegs>();
		for (int32 Side = 0; Side < 2; ++Side)
		{
			if (const int32* H = M.PartIndex.Find(FName(*FString::Printf(TEXT("legs_%d"), Side)))) L->Hip[Side] = *H;
			// The export names them in the order Swift built them.
			for (int32 K = 0;; ++K)
			{
				const int32* I = M.PartIndex.Find(FName(*FString::Printf(TEXT("shins_%d_lower_%d_node"), Side, K)));
				if (!I) break;
				L->Lower[Side].Add(*I);
			}
			for (int32 K = 0;; ++K)
			{
				const int32* I = M.PartIndex.Find(FName(*FString::Printf(TEXT("shins_%d_foot_%d_node"), Side, K)));
				if (!I) break;
				L->Foot[Side].Add(*I);
			}
		}
		return *Cache.Add(&M, MoveTemp(L));
	}

	/// `RangerShin`: its knee and ankle pivots in the hip's frame (cells).
	struct FShinPivots
	{
		FVector Knee, Ankle;
	};

	/// `RangerShin.bend`: the shin parts turned about the knee (`-Knee`), the
	/// foot's about the ankle (`Ankle`) as well, from their rest transforms.
	void Bend(const FAcModelInfo& M, FAcPose& P, const FLegs& L, const int32 Side, const FShinPivots& S, const double Knee,
		const double Ankle)
	{
		const FTransform AtKnee = AboutZ(S.Knee, -Knee);
		for (const int32 I : L.Lower[Side]) P.Local[I] = M.Parts[I].Rest * AtKnee;
		const FTransform AtAnkle = AboutZ(S.Ankle, Ankle) * AtKnee;
		for (const int32 I : L.Foot[Side]) P.Local[I] = M.Parts[I].Rest * AtAnkle;
	}

	/// `GameScene.soleHeights`: how high a bent leg's sole points (heel and
	/// toe, in the hip's frame: x ahead, y up) sit under the hip, turned
	/// through the ankle, the knee and the hip. Appended to `Out`.
	void SoleHeights(const FVector2D (&Ends)[2], const FShinPivots& S, const double Hip, const double Knee,
		const double Ankle, TArray<double, TInlineAllocator<4>>& Out)
	{
		auto Turn = [](const FVector2D P, const FVector& C, const double A)
		{
			const FVector2D O(C.X, C.Y), D = P - O;
			return O + FVector2D(D.X * std::cos(A) - D.Y * std::sin(A), D.X * std::sin(A) + D.Y * std::cos(A));
		};
		for (const FVector2D& E : Ends)
		{
			const FVector2D Q = Turn(Turn(E, S.Ankle, Ankle), S.Knee, -Knee);
			Out.Add(Q.X * std::sin(Hip) + Q.Y * std::cos(Hip));
		}
	}

	double Lowest(const TArray<double, TInlineAllocator<4>>& V, const double Fallback)
	{
		if (V.IsEmpty()) return Fallback;
		double M = V[0];
		for (const double X : V) M = FMath::Min(M, X);
		return M;
	}

	// --- Prospector ---------------------------------------------------------

	/// `Models.Prospector` (Models+Prospector.swift, Models+ProspectorHull.swift).
	constexpr double DrillArmX = 0.1;
	const FVector2D ProspectorSoles[2] = {FVector2D(-0.14, -0.565), FVector2D(0.26, -0.565)};
	const FShinPivots ProspectorShin{FVector(0.04, -0.26, 0), FVector(0.035, -0.48, 0)};

	/// `ProspectorFork` (Models+Fork.swift).
	constexpr double ForkShut = -0.03, ForkSpread = 0.52;
	const FVector ForkHold(0.27, 0.01, 0);
	constexpr double ForkHeadX = 0.31, ForkExtend = 0.2;
	constexpr double ForkPickup = 1.3;

	/// `ForkMotion`: where a Prospector's fork is in its round.
	struct FProspectorState : FAcPoseState
	{
		bool bInit = false;
		/// 0 shut, 1 spread round a load, up to 1.35 reaching for a nodule.
		double Open = 0, Load = 0, Slide = 0, Reach = 0;
		bool bHydrogen = false;
		/// Picking up: the drill stops for it. `bBit`: the frame it bites.
		bool bPicking = false, bBit = false;
		TOptional<double> Last;
		TOptional<double> Phase;

		void Start(const ac::Unit& U)
		{
			bInit = true;
			Open = U.carrying > 0 ? 1.0 : 0.0;
			Load = Open;
			bHydrogen = U.hydrogen.value_or(false);
		}

		/// `ForkMotion.step`.
		void Step(const ac::Unit& U, const double Time)
		{
			const double Dt = FMath::Max(0.0, FMath::Min(0.1, Time - Last.Get(Time)));
			Last = Time;
			bBit = false;
			if (U.task == Task::mining && U.carrying == 0 && U.timer < ForkPickup)
			{
				const double S = 1.0 - FMath::Max(0.0, U.timer) / ForkPickup;
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

	// --- Ranger -------------------------------------------------------------

	/// `Models.Ranger` (Models+Ranger.swift).
	const FVector2D RangerSoles[2] = {FVector2D(-0.09, -0.462), FVector2D(0.21, -0.462)};
	const FShinPivots RangerShin{FVector(0.035, -0.215, 0), FVector(0.02, -0.41, 0)};
	constexpr double GunX = 0.03;
	constexpr double MinigunSpin = 30.0;

	/// The torso node (no geometry; folded into `head`'s and `leftArm`'s
	/// rest transforms by the export): at (0, 0.52, 0), leaning −0.12 about Z.
	const FTransform& TorsoLean()
	{
		static const FTransform T = Sk(FVector(0, 0.52, 0), SkZ(-0.12), FVector::OneVector);
		return T;
	}
	const FQuat& TorsoLeanSk()
	{
		static const FQuat Q = SkZ(-0.12);
		return Q;
	}
	/// `guardPose`'s constants (SceneKit quaternions, positions).
	const FQuat& ArmRest() { static const FQuat Q = SkZ(-0.45); return Q; }
	const FQuat& ArmGuard() { static const FQuat Q = SkY(-1.0) * SkZ(0.12); return Q; }
	const FQuat& FaceRest() { static const FQuat Q = SkY(Pi / 2); return Q; }
	const FQuat& FaceGuard() { static const FQuat Q = SkY(0.2); return Q; }
	const FVector ShieldRest(0.13, 0.01, -0.07), ShieldGuard(0.3, 0.02, 0.02);

	/// What a Ranger remembers: `RangerAim`, `RangerGuard`, `MinigunSpin`,
	/// and the gun pivot's x last frame (`gunAngles` reads it before the
	/// recoil of this frame is set).
	struct FRangerState : FAcPoseState
	{
		double AimW = 0;
		TOptional<double> AimLast;
		double Guard = 0;
		TOptional<double> GuardLast;
		double SpinAngle = 0, SpinRate = 0, Heat = 0;
		TOptional<double> SpinLast;
		double GunPosX = GunX;
	};

	/// A part looked up by name each time: `FAcPartIndex`'s one-model cache
	/// is written from the parallel poses (see GAME-LAYER.md, B6's note).
	struct FPart
	{
		FName Name;
		FPart(const TCHAR* InName) : Name(InName) {}
		int32 Get(const FAcModelInfo& M) const
		{
			const int32* I = M.PartIndex.Find(Name);
			return I ? *I : INDEX_NONE;
		}
	};

	/// The Ranger's parts.
	struct FRangerParts
	{
		FPart Body{TEXT("body")}, Head{TEXT("head")}, LeftArm{TEXT("leftArm")}, Shield{TEXT("shield")},
			Gun{TEXT("gun")}, Rifle{TEXT("rifle")}, Flash{TEXT("flash")}, Minigun{TEXT("minigun_root")},
			Barrels{TEXT("minigun_barrels")}, Blur{TEXT("minigun_blur")}, MinigunFlash{TEXT("minigun_flash")};
	};
	const FRangerParts& RangerParts()
	{
		static const FRangerParts Parts;
		return Parts;
	}

	struct FProspectorParts
	{
		FPart Body{TEXT("body")}, DrillArm{TEXT("drillArm")}, Drill{TEXT("drill")}, ForkArm{TEXT("fork_arm")},
			ForkRoot{TEXT("fork_root")}, Tine0{TEXT("fork_tines_0")}, Tine1{TEXT("fork_tines_1")}, Ore{TEXT("fork_ore")},
			Hydrogen{TEXT("fork_hydrogen")}, Sparks{TEXT("sparkEmitter")};
	};
	const FProspectorParts& ProspectorParts()
	{
		static const FProspectorParts Parts;
		return Parts;
	}

	/// `GameScene.gunAngles`: the gun arm's yaw (about the body's up, + to
	/// its left) and pitch (+ up) that put the bore's line through `Aim`,
	/// its offset from the elbow counted; unset at arm's length. Reads the
	/// body as posed this frame. Yaw within ±0.9 rad, pitch ±1.
	TOptional<FVector2D> GunAngles(const FAcModelInfo& M, const FAcPose& P, const int32 Body, const FVector& GunPosSk,
		const FVector& BoreSk, const FVector& Aim)
	{
		const FTransform BodyWorld = PartWorld(M, P, Body);
		const FVector D = AcSpace::ToSceneKit(BodyWorld.InverseTransformPosition(Aim)) - GunPosSk;
		const double H = FVector2D(D.X, D.Z).Size();
		if (H <= 0.4) return {};
		const double Yaw = std::atan2(-D.Z, D.X) + std::asin(FMath::Clamp(BoreSk.Z / H, -0.9, 0.9));
		const double R = FVector2D(H, D.Y).Size();
		const double Pitch = std::atan2(D.Y, H) - std::asin(FMath::Clamp(BoreSk.Y / R, -0.9, 0.9));
		return FVector2D(FMath::Clamp(std::remainder(Yaw, 2 * Pi), -0.9, 0.9), FMath::Clamp(Pitch, -1.0, 1.0));
	}

	void PoseRangerKind(const FAcPoseContext& C, FAcPose& P)
	{
		const ac::Unit& U = *C.Unit;
		AcPoseInfantry::FRangerArms Arms;
		Arms.bMinigun = C.HasUpgrade(U.owner, ac::Upgrade::minigun);
		Arms.bShield = C.HasUpgrade(U.owner, ac::Upgrade::aegisShield);
		AcPoseInfantry::PoseRanger(C, P, AcPoseInfantry::RangerAim(C), Arms);
	}

	FAcPoseRegistration RegisterProspector(UnitKind::prospector, &AcPoseInfantry::PoseProspector);
	FAcPoseRegistration RegisterRanger(UnitKind::ranger, &PoseRangerKind);
}

namespace AcPoseInfantry
{
	FName CueDrill()
	{
		static const FName N(TEXT("AcDrill"));
		return N;
	}

	FName CueCrystalBite()
	{
		static const FName N(TEXT("AcCrystalBite"));
		return N;
	}

	double ChestHeight(const ac::UnitKind Kind) { return Kind == UnitKind::juggernaut ? 0.85 : 0.62; }

	double BuildingRadius(const ac::StructureKind Kind)
	{
		switch (Kind)
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
		return 1.5;
	}

	double RangerFoldError(const FAcModelInfo& M)
	{
		auto Error = [](const FTransform& A, const FTransform& B)
		{
			const FMatrix X = A.ToMatrixWithScale(), Y = B.ToMatrixWithScale();
			double E = 0;
			for (int32 R = 0; R < 4; ++R)
			{
				for (int32 K = 0; K < 4; ++K) E = FMath::Max(E, FMath::Abs(X.M[R][K] - Y.M[R][K]) * (R == 3 ? 1.0 : 100.0));
			}
			return E;
		};
		const FRangerParts& Parts = RangerParts();
		double E = 0;
		if (const int32 I = Parts.Head.Get(M); I != INDEX_NONE)
		{
			E = FMath::Max(E, Error(SkTurned(M.Parts[I], FQuat::Identity) * TorsoLean(), M.Parts[I].Rest));
		}
		if (const int32 I = Parts.LeftArm.Get(M); I != INDEX_NONE)
		{
			E = FMath::Max(E, Error(SkTurned(M.Parts[I], ArmRest()) * TorsoLean(), M.Parts[I].Rest));
		}
		if (const int32 I = Parts.Shield.Get(M); I != INDEX_NONE)
		{
			const FQuat Face = (TorsoLeanSk() * ArmRest()).Inverse() * FaceRest();
			E = FMath::Max(E, Error(Sk(ShieldRest, Face, M.Parts[I].SkScale), M.Parts[I].Rest));
		}
		return E;
	}

	void PoseProspector(const FAcPoseContext& C, FAcPose& P)
	{
		const ac::Unit& U = *C.Unit;
		const FAcModelInfo& M = C.Model;
		const FProspectorParts& Parts = ProspectorParts();
		const FLegs& Legs = LegsOf(M);
		const double Time = C.Time;
		const double Id = double(U.id);
		const bool bWalking = U.walking();
		const double Phase = U.stride * 3.2;
		P.bAnimated = true;

		// A short, heavy stride: each knee bends as its leg swings through,
		// the boots stay near level, and the hull sinks so the lower boot
		// stays on the ground.
		TArray<double, TInlineAllocator<4>> Soles;
		for (int32 I = 0; I < 2; ++I)
		{
			const double Side = I == 0 ? 1.0 : -1.0;
			const double Hip = bWalking ? Side * 0.45 * std::sin(Phase) : 0.0;
			const double Knee = bWalking ? 0.1 + 1.1 * std::pow(FMath::Max(0.0, Side * std::cos(Phase + 0.2)), 1.2) : 0.0;
			const double Ankle = -(Hip - Knee) * 0.8;
			if (Legs.Hip[I] != INDEX_NONE) P.Local[Legs.Hip[I]] = SkTurned(M.Parts[Legs.Hip[I]], SkZ(Hip));
			Bend(M, P, Legs, I, ProspectorShin, Knee, Ankle);
			SoleHeights(ProspectorSoles, ProspectorShin, Hip, Knee, Ankle, Soles);
		}
		const double Rest = ProspectorSoles[0].Y;
		const double Idle = bWalking ? 0.0 : std::sin(Time * 2.1 + Id) * 0.006;
		const double Sink = (Rest - Lowest(Soles, Rest)) * M.Parts[0].SkScale.Y;
		P.Placement.AddToTranslation(FVector(0, 0, AcSpace::ToCm(Sink + Idle)));

		// The fork's round.
		FProspectorState& F = C.Memory.State<FProspectorState>();
		if (!F.bInit) F.Start(U);
		F.Step(U, Time);
		{
			// `ProspectorFork.pose`: eased up to 1 (spread round a load); past 1
			// wider still, reaching for a nodule; a little overshoot as the
			// tines snap shut.
			const double E = F.Open < 1 ? Smoothstep(0, 1, F.Open) : F.Open;
			const double Snap = F.Open < 0.25 ? 0.05 * std::sin(F.Open / 0.25 * Pi) : 0.0;
			const double A = ForkShut + (ForkSpread - ForkShut) * E - Snap;
			const int32 Tines[2] = {Parts.Tine0.Get(M), Parts.Tine1.Get(M)};
			for (int32 I = 0; I < 2; ++I)
			{
				const double Side = I == 0 ? -1.0 : 1.0;
				if (Tines[I] != INDEX_NONE) P.Local[Tines[I]] = SkTurned(M.Parts[Tines[I]], SkY(-Side * A));
			}
			const int32 Ore = Parts.Ore.Get(M), Hydrogen = Parts.Hydrogen.Get(M);
			const int32 Load = F.bHydrogen ? Hydrogen : Ore;
			const int32 Other = F.bHydrogen ? Ore : Hydrogen;
			if (Other != INDEX_NONE) P.Visible[Other] = false;
			if (Load != INDEX_NONE)
			{
				P.Visible[Load] = F.Load >= 0.02;
				P.Local[Load] = Sk(FVector(ForkHold.X + F.Slide, ForkHold.Y, ForkHold.Z), FQuat::Identity, FVector(F.Load));
			}
			const int32 Arm = Parts.ForkArm.Get(M), Root = Parts.ForkRoot.Get(M);
			if (Arm != INDEX_NONE && Root != INDEX_NONE)
			{
				// `fork.rest` is the arm's built orientation; `reached` points it
				// into the pile.
				static const FQuat Reached = ToUE(FQuat::FindBetweenNormals(FVector(1, 0, 0),
					FVector(0.8, -0.3, 0.2).GetSafeNormal()));
				const double R = Smoothstep(0, 1, F.Reach);
				const FTransform& ArmRest = M.Parts[Arm].Rest;
				P.Local[Arm] = FTransform(FQuat::Slerp(ArmRest.GetRotation(), Reached, R), ArmRest.GetTranslation(),
					ArmRest.GetScale3D());
				const FAcModelPart& RootPart = M.Parts[Root];
				P.Local[Root] = Sk(FVector(ForkHeadX + ForkExtend * R, RootPart.SkPosition.Y, RootPart.SkPosition.Z),
					FQuat::Identity, RootPart.SkScale);
			}
		}

		// The drill stops while the fork picks a nodule off the pile.
		const bool bMining = U.working() && !F.bPicking;
		double DrillTurn = bMining ? Time * 25 : 0.0;
		// Drilling: the arm pushes into the rock in slow strokes and the whole
		// body shudders with the bit.
		double Push = bMining ? 0.06 * (0.5 + 0.5 * std::sin(Time * 2.4 + Id)) : 0.0;
		// A cutter strike: the arm jabs out and snaps back.
		if (C.Memory.LastShot && Time - *C.Memory.LastShot < 0.3)
		{
			const double K = (Time - *C.Memory.LastShot) / 0.3;
			Push = 0.16 * std::sin(K * Pi);
			DrillTurn = Time * 30;
		}
		const int32 Drill = Parts.Drill.Get(M), DrillArm = Parts.DrillArm.Get(M), Body = Parts.Body.Get(M);
		if (Drill != INDEX_NONE)
		{
			P.Local[Drill] = SkTurned(M.Parts[Drill], SkX(DrillTurn));
			// The bit's spiral glows gold as it bites (`drillGlow`).
			static const FName DrillGlow(TEXT("drillGlow"));
			P.MeshEmission.Add({Drill, DrillGlow, bMining ? float(1.6 + 0.8 * std::sin(Time * 19 + Id)) : 0.f});
		}
		if (DrillArm != INDEX_NONE)
		{
			const FAcModelPart& A = M.Parts[DrillArm];
			P.Local[DrillArm] = Sk(FVector(DrillArmX + Push, A.SkPosition.Y, A.SkPosition.Z), FQuat::Identity, A.SkScale);
		}
		if (Body != INDEX_NONE)
		{
			// Pitched with the stride; picking up, it leans in after the fork.
			const double Pitch = bWalking ? std::sin(U.stride * 3.2) * 0.04 : 0.0;
			const double Roll = bMining ? -0.1 - Push * 0.8 + std::sin(Time * 31) * 0.012 : -0.16 * Smoothstep(0, 1, F.Reach);
			P.Local[Body] = SkTurned(M.Parts[Body], SkZ(Roll) * SkX(Pitch));
		}

		// Sparks and the welder's flicker at the bit; chips as the fork bites.
		if (bMining)
		{
			if (const int32 S = Parts.Sparks.Get(M); S != INDEX_NONE)
			{
				const float Weld = float(22 + 14 * std::sin(Time * 47) * std::sin(Time * 13));
				P.Cues.Add({CueDrill(), PartWorld(M, P, S).GetTranslation(), Weld});
			}
		}
		if (F.bBit)
		{
			const int32 Ore = Parts.Ore.Get(M);
			if (Ore != INDEX_NONE) P.Cues.Add({CueCrystalBite(), PartWorld(M, P, Ore).GetTranslation(), 1.f});
		}
	}

	TOptional<FVector> RangerAim(const FAcPoseContext& C)
	{
		// The driven one: on what the crosshair is on (E4, `pilotAim`).
		if (C.bDriven && C.PilotAim) return *C.PilotAim;
		const ac::Unit& U = *C.Unit;
		const FAcUnitMemory& Mem = C.Memory;
		const bool bRecent = Mem.LastShot && C.Time - *Mem.LastShot < 1.5;
		if (U.task != Task::attacking && !bRecent) return {};
		auto BuildingFace = [&C, &U](const ac::Structure& S)
		{
			const ac::Vec2 D = U.position - S.position;
			const ac::Vec2 Face = S.position + D * (1.0 / FMath::Max(ac::length(D), 1e-6)) * BuildingRadius(S.kind) * 0.85;
			return AcSpace::ToWorld(Face, C.Ground(Face) + 0.5 + 0.25 * AcPose::BuildingHeight(S.kind));
		};
		if (U.target)
		{
			const int64 Id = *U.target;
			for (const ac::Unit& V : C.State.units)
			{
				if (V.id != Id) continue;
				// A flyer's hull, hovering over its ground; else the chest.
				if (ac::Rules::stats(V.kind).air)
				{
					return AcSpace::ToWorld(V.position, C.Ground(V.position) + AcPose::Hover(V.kind));
				}
				return AcSpace::ToWorld(V.position, C.Ground(V.position) + ChestHeight(V.kind));
			}
			for (const ac::Structure& S : C.State.structures)
			{
				if (S.id == Id) return BuildingFace(S);
			}
		}
		if (!bRecent || !Mem.LastShotAt) return {};
		// Where it last shot (Swift keeps the round's end point, `lastTo`).
		const ac::Vec2 At = *Mem.LastShotAt;
		if (Mem.LastTarget)
		{
			for (const ac::Structure& S : C.State.structures)
			{
				if (S.id == *Mem.LastTarget) return BuildingFace(S);
			}
		}
		return AcSpace::ToWorld(At, C.Ground(At) + 0.62);
	}

	void PoseRanger(const FAcPoseContext& C, FAcPose& P, const TOptional<FVector>& Aim, const FRangerArms Arms)
	{
		const ac::Unit& U = *C.Unit;
		const FAcModelInfo& M = C.Model;
		const FRangerParts& Parts = RangerParts();
		const FLegs& Legs = LegsOf(M);
		FRangerState& R = C.Memory.State<FRangerState>();
		const double Time = C.Time;
		const bool bWalking = U.walking();
		const bool bAiming = U.task == Task::attacking && !bWalking;
		const double T = Time + double(U.id) * 1.73;
		const double Phase = U.stride * 3.8;
		P.bAnimated = true;

		// The weapon (`arm(minigun:)`) and the shield (`wear(shield:)`).
		const int32 Rifle = Parts.Rifle.Get(M), Minigun = Parts.Minigun.Get(M), Flash = Parts.Flash.Get(M),
			MinigunFlash = Parts.MinigunFlash.Get(M), Shield = Parts.Shield.Get(M), Blur = Parts.Blur.Get(M);
		if (Rifle != INDEX_NONE) P.Visible[Rifle] = !Arms.bMinigun;
		if (Minigun != INDEX_NONE) P.Visible[Minigun] = Arms.bMinigun;
		if (Shield != INDEX_NONE) P.Visible[Shield] = Arms.bShield;

		// Left leg (legs[0]) forward while sin(phase) > 0. A leg swings
		// through while it moves forward; its knee bends most early in the
		// swing and a little under the weight.
		TArray<double, TInlineAllocator<4>> Soles;
		for (int32 I = 0; I < 2; ++I)
		{
			const double Side = I == 0 ? 1.0 : -1.0;
			const double Hip = bWalking ? Side * 0.5 * std::sin(Phase) : 0.0;
			const double Knee = bWalking ? 0.08 + 0.8 * std::pow(FMath::Max(0.0, Side * std::cos(Phase + 0.4)), 1.5) : 0.0;
			// The foot kept near level: against the hip and the knee.
			const double Ankle = -(Hip - Knee) * 0.8;
			if (Legs.Hip[I] != INDEX_NONE) P.Local[Legs.Hip[I]] = SkTurned(M.Parts[Legs.Hip[I]], SkZ(Hip));
			Bend(M, P, Legs, I, RangerShin, Knee, Ankle);
			SoleHeights(RangerSoles, RangerShin, Hip, Knee, Ankle, Soles);
		}
		// Lower the whole Ranger by how far the lower sole rose (the hips sit
		// in the root's frame, scaled with it).
		const double Rest = RangerSoles[0].Y;
		const double Sink = (Rest - Lowest(Soles, Rest)) * M.Parts[0].SkScale.Y;
		P.Placement.AddToTranslation(FVector(0, 0, AcSpace::ToCm(Sink)));

		const int32 Body = Parts.Body.Get(M);
		if (Body != INDEX_NONE)
		{
			const double Y = bWalking ? 0.0 : 0.006 * std::sin(T * 1.8);
			const double Pitch = bWalking ? std::sin(U.stride * 3.8) * 0.05 : 0.03 * std::sin(T * 0.31);
			P.Local[Body] = Sk(FVector(0, Y, 0), SkX(Pitch), M.Parts[Body].SkScale);
		}

		// Scan: a few seconds of every ~45 the rifle comes up and sweeps.
		// Aiming: rifle level and steady, kicking back with each shot.
		const double Scan = bAiming ? 1.0 : bWalking ? 0.6 : Smoothstep(0.75, 0.9, std::sin(T * 0.14));
		// Walking, the rifle arm sways a little with the stride.
		double Pitch = -0.55 + 0.55 * Scan + (bWalking ? 0.06 * std::sin(Phase) : 0.0);
		double Yaw = bAiming ? 0.0 : Scan * 0.25 * std::sin(T * 0.9);
		double Look = bWalking || bAiming ? 0.0 : 0.5 * std::sin(T * 0.4) * (0.4 + 0.6 * Scan);
		const double Wdt = FMath::Min(0.1, FMath::Max(0.0, Time - R.AimLast.Get(Time)));
		R.AimLast = Time;
		const int32 Gun = Parts.Gun.Get(M);
		TOptional<FVector2D> On;
		if (Aim && Gun != INDEX_NONE && Body != INDEX_NONE)
		{
			const FAcModelPart& G = M.Parts[Gun];
			// The bore: the muzzle in the gun arm's frame.
			FVector Bore = FVector::ZeroVector;
			if (Arms.bMinigun && Minigun != INDEX_NONE && MinigunFlash != INDEX_NONE)
			{
				const FAcModelPart& Mg = M.Parts[Minigun];
				Bore = Mg.SkPosition + Mg.SkScale * M.Parts[MinigunFlash].SkPosition;
			}
			else if (Flash != INDEX_NONE)
			{
				Bore = M.Parts[Flash].SkPosition;
			}
			On = GunAngles(M, P, Body, FVector(R.GunPosX, G.SkPosition.Y, G.SkPosition.Z), Bore, *Aim);
		}
		R.AimW = On ? FMath::Min(1.0, R.AimW + Wdt / 0.15) : FMath::Max(0.0, R.AimW - Wdt / 0.4);
		if (On || R.AimW > 0)
		{
			// Eased from the rest pose onto the aim (and back off it).
			const FVector2D Target = On ? *On : FVector2D(Yaw, Pitch);
			const double E = R.AimW * R.AimW * (3 - 2 * R.AimW);
			Pitch += (Target.Y - Pitch) * E;
			Yaw += (Target.X - Yaw) * E;
			Look += (0.6 * Target.X - Look) * E;
		}
		const int32 Head = Parts.Head.Get(M);
		if (Head != INDEX_NONE) P.Local[Head] = SkTurned(M.Parts[Head], SkY(Look)) * TorsoLean();

		// Recoil and flash, or the Mini gun's spin.
		const double Since = C.Memory.LastShot ? Time - *C.Memory.LastShot : 10.0;
		double GunPosX = GunX;
		if (!Arms.bMinigun)
		{
			if (Flash != INDEX_NONE)
			{
				P.Visible[Flash] = Since <= 0.05;
				P.Local[Flash] = SkTurned(M.Parts[Flash], SkX(Since * 97));
			}
			GunPosX = GunX - 0.035 * FMath::Max(0.0, 1 - Since / 0.12);
		}
		else
		{
			// `spinMinigun`: up to speed in about 0.15 s, run down over a
			// second, hot after 3 s of fire, cool in 2.5 s.
			const bool bFiring = Since < 0.25;
			const double Dt = FMath::Min(0.1, FMath::Max(0.0, Time - R.SpinLast.Get(Time)));
			R.SpinLast = Time;
			R.SpinRate = bFiring ? R.SpinRate + (MinigunSpin - R.SpinRate) * FMath::Min(1.0, Dt * 14)
				: R.SpinRate * std::exp(-Dt * 2.6);
			// Six barrels look alike every 60°: the drawn turn is held to 0.35
			// of that a frame (a clear forward spin at any frame rate).
			const double Step = 0.35 * Pi / 3;
			R.SpinAngle = std::remainder(R.SpinAngle + FMath::Min(R.SpinRate * Dt, Step), 2 * Pi);
			R.Heat = bFiring ? FMath::Min(1.0, R.Heat + Dt / 3) : FMath::Max(0.0, R.Heat - Dt / 2.5);
			if (const int32 Barrels = Parts.Barrels.Get(M); Barrels != INDEX_NONE)
			{
				P.Local[Barrels] = SkTurned(M.Parts[Barrels], SkX(R.SpinAngle));
				// The tips glow hotter the longer it fires (`minigun_heat`).
				static const FName Heat(TEXT("minigun_heat"));
				P.MeshEmission.Add({Barrels, Heat, float(2.6 * R.Heat * R.Heat)});
			}
			// The smear over the barrels shows the rest of the speed: its
			// opacity is custom data 1 (M_AcSmear).
			if (Blur != INDEX_NONE)
			{
				const double Smear = 0.6 * Smoothstep(8, MinigunSpin, R.SpinRate);
				P.Visible[Blur] = Smear >= 0.02;
				P.Emission[Blur] = float(Smear);
			}
			const bool bShooting = bFiring && R.SpinRate > MinigunSpin * 0.5;
			if (MinigunFlash != INDEX_NONE)
			{
				const FAcModelPart& F = M.Parts[MinigunFlash];
				const double K = 0.8 + 0.4 * std::abs(std::sin(Time * 57));
				P.Visible[MinigunFlash] = Since <= 0.06;
				P.Local[MinigunFlash] = Sk(F.SkPosition, SkX(Time * 131), F.SkScale * K);
			}
			GunPosX = GunX + (bShooting ? -0.012 + 0.006 * std::sin(Time * 173) : 0.0);
		}
		if (Gun != INDEX_NONE)
		{
			// Pitched, then turned: the order `GunAngles` solves for.
			const FAcModelPart& G = M.Parts[Gun];
			P.Local[Gun] = Sk(FVector(GunPosX, G.SkPosition.Y, G.SkPosition.Z), SkY(Yaw) * SkZ(Pitch), G.SkScale);
		}
		R.GunPosX = GunPosX;

		// The shield comes up to block in 0.25 s as the fight starts and goes
		// down in 0.5 s after; with no shield the forearm stays down.
		const double Gdt = FMath::Min(0.1, FMath::Max(0.0, Time - R.GuardLast.Get(Time)));
		R.GuardLast = Time;
		const bool bBlock = bAiming && Arms.bShield;
		R.Guard = bBlock ? FMath::Min(1.0, R.Guard + Gdt / 0.25) : FMath::Max(0.0, R.Guard - Gdt / 0.5);
		// `guardPose`: the shield arm swings back as its leg (the left) goes
		// forward.
		const double Swing = bWalking ? -0.22 * std::sin(Phase) * (1 - R.Guard) : 0.0;
		const double E = R.Guard * R.Guard * (3 - 2 * R.Guard);
		const FQuat ArmQ = SkZ(Swing) * FQuat::Slerp(ArmRest(), ArmGuard(), E);
		if (const int32 LeftArm = Parts.LeftArm.Get(M); LeftArm != INDEX_NONE)
		{
			P.Local[LeftArm] = SkTurned(M.Parts[LeftArm], ArmQ) * TorsoLean();
		}
		if (Shield != INDEX_NONE)
		{
			const FQuat Face = (TorsoLeanSk() * ArmQ).Inverse() * FQuat::Slerp(FaceRest(), FaceGuard(), E);
			P.Local[Shield] = Sk(FMath::Lerp(ShieldRest, ShieldGuard, E), Face, M.Parts[Shield].SkScale);
		}
	}
}
