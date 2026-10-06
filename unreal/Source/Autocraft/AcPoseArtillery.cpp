// Poses of the two artillery kinds (chunk B4, GAME-LAYER.md §2.6): the
// Longbow (the rhino tank: turret, the 2.7 s anchor sequence, its legs and
// their anchored walk, the rail gun, recoil, flashes, smoke ring, lamps)
// and the Hailstorm (flak mount, cradle, four barrels firing in pairs,
// radar, wheels, treads).
//
// Ports of, in Sources/Autocraft:
//   GameScene.swift `sync` (the `.tank` and `.hailstorm` cases: the turret
//     drawn right on its target within 0.25 rad, `anchorOf`, `tankHeading`),
//   Models+Longbow.swift `GameScene.pose(_ t: Models.Longbow, ...)`,
//   Models+LongbowRhino.swift `poseRhinoLegs` and `Longbow.Rhino`,
//   Models+Hailstorm.swift `GameScene.pose(_ m: Models.Hailstorm, ...)`.
// Every number below is Swift's. The maths is done in SceneKit space (Y up,
// cells, the node's position/eulerAngles/quaternion) and each part's local
// transform is converted once with AcSpace.h, so the code reads like the
// Swift it came from.
//
// Materials: the lock lights (`lockGlow`) were exported dark (their rest
// intensity is 0); Tools/Editor/make_artillery_materials.py gives them
// glowGreen at intensity 1, so the mesh emission set here is Swift's
// `lockGlow.emission.intensity`. The radar lamp (`radarGlow`) is exported
// at 1.8, so its emission is Swift's intensity / 1.8. The smoke ring's
// material (M_AcSmokeRing, same script) reads the emission slot as its
// opacity (Swift fades the node's `opacity`).
//
// Not here (effects, C1/C2): the rounds, the turret snapped onto the target
// at the instant a shot leaves (`fire`'s `.tank` case), the anchor shell.
#include "AcPose.h"

#include "AcModelCatalog.h"
#include "AcSpace.h"

#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

#include "Types.h"

#include <cmath>

namespace
{
	constexpr double Pi = UE_DOUBLE_PI;

	double Smoothstep(const double A, const double B, const double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	double Remainder2Pi(const double A) { return std::remainder(A, 2.0 * Pi); }

	/// SceneKit `eulerAngles` (x pitch, y yaw, z roll) → the node's
	/// quaternion, in SceneKit space (R = Rz·Ry·Rx, checked against the
	/// export's matrices).
	FQuat SkEuler(const double X, const double Y, const double Z)
	{
		return FQuat(FVector(0, 0, 1), Z) * FQuat(FVector(0, 1, 0), Y) * FQuat(FVector(1, 0, 0), X);
	}
	FQuat SkEuler(const FVector& E) { return SkEuler(E.X, E.Y, E.Z); }
	FQuat SkAxisAngle(const FVector& Axis, const double Angle) { return FQuat(Axis, Angle); }

	/// A part's SceneKit node values, starting at its rest; `Apply` writes
	/// them into the pose as an Unreal local transform. The export merges
	/// empty in-between nodes into the part below them (a Longbow toe's
	/// `spoke`, which places and turns it on the foot), so the part's rest
	/// transform is that frame × the node's own: the node's values go under
	/// `Frame` = (node rest)⁻¹ · rest, identity for most parts.
	struct FSkNode
	{
		FVector Position = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity;
		FVector Scale = FVector::OneVector;
		FTransform Frame = FTransform::Identity;

		FSkNode(const FAcModelInfo& M, const int32 I)
		{
			if (I == INDEX_NONE) return;
			const FAcModelPart& Part = M.Parts[I];
			Position = Part.SkPosition;
			Rotation = SkEuler(Part.SkEuler);
			Scale = Part.SkScale;
			Frame = AcSpace::TransformFromSceneKit(Position, Rotation, Scale).Inverse() * Part.Rest;
		}
		void SetEuler(const double X, const double Y, const double Z) { Rotation = SkEuler(X, Y, Z); }
		void Apply(FAcPose& P, const int32 I) const
		{
			if (I != INDEX_NONE) P.Local[I] = AcSpace::TransformFromSceneKit(Position, Rotation, Scale) * Frame;
		}
	};

	/// The SceneKit rest euler of part I (zero if missing).
	FVector RestEuler(const FAcModelInfo& M, const int32 I)
	{
		return I == INDEX_NONE ? FVector::ZeroVector : M.Parts[I].SkEuler;
	}

	void SetVisible(FAcPose& P, const int32 I, const bool bVisible)
	{
		if (I != INDEX_NONE) P.Visible[I] = bVisible;
	}

	int32 PartOf(const FAcModelInfo& M, const FString& Name)
	{
		const int32* Found = M.PartIndex.Find(FName(*Name));
		return Found ? *Found : INDEX_NONE;
	}

	/// The parts of one model, looked up once per model and never rebuilt
	/// (the poses run in parallel: a single cache overwritten for another
	/// model would change under a thread still reading it).
	template <class T>
	const T& PartsOf(const FAcModelInfo& M)
	{
		static FCriticalSection Lock;
		static TMap<const FAcModelInfo*, TUniquePtr<T>> Cache;
		FScopeLock Scope(&Lock);
		if (const TUniquePtr<T>* Found = Cache.Find(&M)) return **Found;
		return *Cache.Add(&M, MakeUnique<T>(M));
	}

	/// Where the unit (or, with `bStructures`, the building) `Id` stands.
	TOptional<ac::Vec2> PositionOf(const ac::GameState& S, const int64 Id, const bool bStructures)
	{
		for (const ac::Unit& U : S.units)
		{
			if (U.id == Id) return U.position;
		}
		if (bStructures)
		{
			for (const ac::Structure& B : S.structures)
			{
				if (B.id == Id) return B.position;
			}
		}
		return {};
	}

	/// The turret's world heading: `aim`, drawn right on the target once
	/// nearly on it (the simulation fires within 0.2 rad of it).
	double TurretAim(const FAcPoseContext& C, const bool bStructures)
	{
		const ac::Unit& U = *C.Unit;
		double Aim = U.aim ? *U.aim : U.heading;
		if (!C.bDriven && U.target)
		{
			if (const TOptional<ac::Vec2> P = PositionOf(C.State, *U.target, bStructures))
			{
				const double Want = std::atan2(P->y - U.position.y, P->x - U.position.x);
				if (FMath::Abs(Remainder2Pi(Want - Aim)) < 0.25) Aim = Want;
			}
		}
		return Aim;
	}

	/// `Longbow.treadPhases`, `treadPitch`: the tread loop baked at 6
	/// offsets, one shown by distance driven.
	constexpr int32 TreadPhases = 6;
	constexpr double TreadPitch = 0.1;

	int32 TreadShown(const double Stride)
	{
		const double Roll = Stride / TreadPitch;
		return FMath::Min(int32((Roll - std::floor(Roll)) * TreadPhases), TreadPhases - 1);
	}

	// MARK: - Longbow

	/// `Longbow.legSigns` (x, z): front-left, front-right, rear-left, rear-right.
	constexpr double LegSigns[4][2] = {{1, -1}, {1, 1}, {-1, -1}, {-1, 1}};

	/// `Longbow.Rhino` (Models+LongbowRhino.swift).
	namespace Rhino
	{
		constexpr double Lift = 0.22;
		constexpr double StepCycle = 0.6;
		constexpr double FootReach = 16.0 * StepCycle / (2.0 * Pi);
		constexpr double StepReach = StepCycle / 4.0;
		constexpr double ToeTilt0 = -1.45, ToeTilt1 = -0.32;
		/// The rhino's trunnion height and slide run (`longbow()`), gun pitch.
		constexpr double Trunnion0 = 0.38, Trunnion1 = 0.5;
		constexpr double SlideRun0 = 0.0, SlideRun1 = 0.85;
		constexpr double GunPitch = 0.68;

		/// A pod's centre and turn between travel (u 0) and anchored (u 1).
		void Pod(const double Sx, const double Sz, const double U, FVector& OutP, FQuat& OutQ)
		{
			const FVector P0(0.55 * Sx, 0.255, 0.64 * Sz);
			const double A = 0.85;
			const FVector P1(0.42 * Sx + 0.6 * Sx * std::cos(A), 0.78, 0.34 * Sz + 0.6 * Sz * std::sin(A));
			const FQuat Q1 = SkAxisAngle(FVector(0, 1, 0), -Sx * Sz * A) * SkAxisAngle(FVector(0, 0, 1), Sx * 0.17);
			OutP = P0 + (P1 - P0) * U + FVector(0, 0.08 * std::sin(Pi * U), 0);
			OutQ = FQuat::Slerp(FQuat::Identity, Q1, U);
		}
		double ShinSwing(const double Sx, const double U) { return (-Sx * Pi / 2.0) * (1.0 - U) + Sx * (-0.17 + 0.1) * U; }
		FVector Knee(const double Sz) { return FVector(0, 0.02, -0.18 * Sz); }
		FVector Hip(const double Sx, const double Sz) { return FVector(0.42 * Sx, 0.42, 0.34 * Sz); }
		double Gait(const double Stride, const double Heading) { return (Stride + Heading * FootReach) / StepCycle; }
	}

	/// `Longbow.hullDrop(.rhino, anchor:)`: the hull rises on its legs.
	double HullDrop(const double S) { return Rhino::Lift * Smoothstep(0.45, 0.7, S); }

	struct FLongbowParts
	{
		int32 Hull = INDEX_NONE, Turret = INDEX_NONE, Beacon = INDEX_NONE, AnchorGun = INDEX_NONE,
			AnchorSlide = INDEX_NONE, Barrel = INDEX_NONE, Flash = INDEX_NONE, BigFlash = INDEX_NONE, Smoke = INDEX_NONE;
		int32 WarnLamps[4] = {INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE};
		int32 Pods[4], PodBodies[4], Claws[4], Rams[4], Pistons[4];
		int32 Toes[4][3];
		int32 Treads[4][TreadPhases];

		FLongbowParts() = default;
		explicit FLongbowParts(const FAcModelInfo& M)
		{
			Hull = PartOf(M, TEXT("hull"));
			Turret = PartOf(M, TEXT("turret"));
			Beacon = PartOf(M, TEXT("beacon"));
			AnchorGun = PartOf(M, TEXT("anchorGun"));
			AnchorSlide = PartOf(M, TEXT("anchorSlide"));
			Barrel = PartOf(M, TEXT("barrels_0"));
			Flash = PartOf(M, TEXT("flashes_0"));
			BigFlash = PartOf(M, TEXT("flashes_1"));
			Smoke = PartOf(M, TEXT("smoke"));
			for (int32 I = 0; I < 4; ++I)
			{
				WarnLamps[I] = PartOf(M, FString::Printf(TEXT("warnLamps_%d"), I));
				Pods[I] = PartOf(M, FString::Printf(TEXT("pods_%d"), I));
				PodBodies[I] = PartOf(M, FString::Printf(TEXT("podBodies_%d"), I));
				Claws[I] = PartOf(M, FString::Printf(TEXT("claws_%d"), I));
				Rams[I] = PartOf(M, FString::Printf(TEXT("legs_%d"), I));
				Pistons[I] = PartOf(M, FString::Printf(TEXT("pistons_%d"), I));
				for (int32 K = 0; K < 3; ++K) Toes[I][K] = PartOf(M, FString::Printf(TEXT("toes_%d_%d"), I, K));
				for (int32 K = 0; K < TreadPhases; ++K) Treads[I][K] = PartOf(M, FString::Printf(TEXT("treads_%d_%d"), I, K));
			}
		}
	};

	/// `GameScene.poseRhinoLegs`: the pods lift into thighs, the shins swing
	/// down, the toes splay; anchored, it walks in diagonal pairs.
	void PoseRhinoLegs(const FAcModelInfo& M, const FLongbowParts& T, FAcPose& P, const double S, const double Stride,
		const double Heading, const bool bMoving)
	{
		const double Unfold = Smoothstep(0.05, 0.45, S);
		const double Shin = Smoothstep(0.15, 0.5, S);
		const double Splay = Smoothstep(0.3, 0.6, S);
		const double Lift = HullDrop(S);
		const int32 Shown = TreadShown(Stride);
		const double Walk = Smoothstep(0.9, 1.0, S);
		const double Cycle = Rhino::Gait(Stride, Heading);
		for (int32 I = 0; I < 4; ++I)
		{
			const double Sx = LegSigns[I][0], Sz = LegSigns[I][1];
			// Swinging (first half of its cycle): back to front, lifted;
			// planted (second half): front to back on the ground.
			const double Phase = Cycle + (Sx * Sz > 0 ? 0.0 : 0.5);
			const double K = Phase - std::floor(Phase);
			const double Ahead = K < 0.5 ? -1.0 + 2.0 * Smoothstep(0, 1, K / 0.5) : 1.0 - 2.0 * (K - 0.5) / 0.5;
			const double Up = bMoving && K < 0.5 ? Walk * std::sin(Pi * K / 0.5) : 0.0;
			FVector Pod;
			FQuat Q;
			Rhino::Pod(Sx, Sz, Unfold, Pod, Q);
			Pod += FVector(Walk * Ahead * Rhino::StepReach, 0.06 * Up, 0);
			{
				FSkNode N(M, T.Pods[I]);
				N.Position = Pod;
				N.Rotation = Q;
				N.Apply(P, T.Pods[I]);
			}
			{
				FSkNode N(M, T.Claws[I]);
				const FVector E = RestEuler(M, T.Claws[I]);
				N.SetEuler(E.X, E.Y, Rhino::ShinSwing(Sx, Shin) - Sx * 0.35 * Up);
				N.Apply(P, T.Claws[I]);
			}
			const double Tilt = Rhino::ToeTilt0 + (Rhino::ToeTilt1 - Rhino::ToeTilt0) * Splay - 0.4 * Up;
			for (const int32 Toe : T.Toes[I])
			{
				FSkNode N(M, Toe);
				const FVector E = RestEuler(M, Toe);
				N.SetEuler(E.X, E.Y, Tilt);
				N.Apply(P, Toe);
			}
			for (int32 F = 0; F < TreadPhases; ++F) SetVisible(P, T.Treads[I][F], F == Shown);
			// The ram points from its hip (risen with the hull) at the pod's
			// inner face; the piston runs out to meet it.
			const FVector Hip = Rhino::Hip(Sx, Sz) + FVector(0, Lift, 0);
			const FVector D = Pod + Q.RotateVector(Rhino::Knee(Sz)) - Hip;
			const double Len = D.Size();
			{
				FSkNode N(M, T.Rams[I]);
				N.Position = Hip;
				N.Rotation = FQuat::FindBetweenNormals(FVector(1, 0, 0), D / Len);
				N.Apply(P, T.Rams[I]);
			}
			{
				FSkNode N(M, T.Pistons[I]);
				N.Position = FVector(Len, 0, 0);
				N.Apply(P, T.Pistons[I]);
			}
		}
	}

	/// `GameScene.pose(_ t: Models.Longbow, ...)` (rhino style), from the
	/// `.tank` case of `sync`.
	void PoseLongbow(const FAcPoseContext& C, FAcPose& P)
	{
		const ac::Unit& U = *C.Unit;
		const FAcModelInfo& M = C.Model;
		const FLongbowParts& T = PartsOf<FLongbowParts>(M);

		// `sync`: the anchor for the effects, the turn on the spot.
		C.Memory.AnchorOf = U.anchor ? *U.anchor : 0.0;
		const double Aim = TurretAim(C, true);
		const bool bTurning = FMath::Abs(Remainder2Pi(U.heading - C.Memory.TankHeading.Get(U.heading))) > 1e-5;
		C.Memory.TankHeading = U.heading;
		const bool bMoving = U.walking() || bTurning;
		const double Heading = U.heading, Stride = U.stride, Time = C.Time;
		const TOptional<double> Shot = C.Memory.LastShot;

		const double S = FMath::Clamp(U.anchor ? *U.anchor : 0.0, 0.0, 1.0);
		const double Seed = double(U.id % 97) * 1.37;
		const double Since = Shot ? Time - *Shot : 100.0;
		const bool bAnchored = S > 0.5;

		const double Unfold = Smoothstep(0.08, 0.42, S);
		const double Kneel = Smoothstep(0.45, 0.7, S);
		const double Extend = Smoothstep(0.8, 0.94, S);
		const double Lock = Smoothstep(0.92, 1.0, S);

		// Legs and pods.
		PoseRhinoLegs(M, T, P, S, Stride, Heading, bMoving);

		// Hull: suspension bob and pitch while driving, an idle engine
		// tremble, the rise, and the anchor shudder.
		const double Drive = bMoving ? 1.0 - Unfold : 0.0;
		double Bob = Drive * (0.012 * std::sin(Stride * 7.3 + Seed) + 0.006 * std::sin(Stride * 13.1));
		double Pitch = Drive * (0.016 * std::sin(Stride * 3.1 + Seed) + 0.008);
		Bob += (1.0 - Kneel) * 0.0025 * std::sin(Time * 37.0 + Seed);
		Bob += HullDrop(S);
		// Walking anchored: a dip while a pair of feet is off the ground.
		if (bMoving)
		{
			Bob -= Smoothstep(0.9, 1.0, S) * 0.012 * FMath::Abs(std::sin(2.0 * Pi * Rhino::Gait(Stride, Heading)));
		}
		const double Land = S - 0.7;
		if (Land > 0 && Land < 0.1) Bob += 0.014 * std::sin(Land / 0.1 * Pi) * (1.0 - Land / 0.1);
		if (bAnchored && Since >= 0 && Since < 0.8)
		{
			const double D = std::exp(-Since * 6.0);
			Bob += 0.022 * D * std::sin(Since * 70.0);
			Pitch += 0.03 * D * std::sin(Since * 45.0 + 1.0) - 0.025 * D;
		}
		else if (!bAnchored && Since >= 0 && Since < 0.4)
		{
			Pitch -= 0.012 * std::exp(-Since * 10.0);
		}
		{
			FSkNode N(M, T.Hull);
			N.Position.Y = Bob;
			const FVector E = RestEuler(M, T.Hull);
			N.SetEuler(Drive * 0.008 * std::sin(Stride * 2.3 + Seed), E.Y, Pitch);
			N.Apply(P, T.Hull);
		}

		// Turret faces `aim` in the world.
		{
			FSkNode N(M, T.Turret);
			const FVector E = RestEuler(M, T.Turret);
			N.SetEuler(E.X, Heading - Aim, E.Z);
			N.Apply(P, T.Turret);
		}

		// The packed rail: one bore fires both rounds of the ripple.
		{
			double Ages[2] = {bAnchored ? 100.0 : Since, bAnchored ? 100.0 : Since - 0.11};
			const int32 NumAges = bAnchored ? 1 : 2;
			double Kick = 0.0;
			bool bLit = false;
			for (int32 A = 0; A < NumAges; ++A)
			{
				const double Age = Ages[A];
				Kick = FMath::Max(Kick, Age < 0 ? 0.0 : Age < 0.03 ? Age / 0.03 : FMath::Max(0.0, 1.0 - (Age - 0.03) / 0.35));
				bLit = bLit || (Age >= 0 && Age < 0.06);
			}
			FSkNode N(M, T.Barrel);
			N.Position.X = -0.12 * Kick * Kick;
			N.Apply(P, T.Barrel);
			SetVisible(P, T.Flash, bLit);
			FSkNode F(M, T.Flash);
			const FVector E = RestEuler(M, T.Flash);
			F.SetEuler(Ages[0] * 97.0 + 0.0, E.Y, E.Z);
			F.Apply(P, T.Flash);
		}

		// The rail: rises on its trunnion, then pitches up with a small
		// overshoot and settles; the slide extends.
		const double Up = Smoothstep(0.52, 0.66, S);
		const double Tilt = Smoothstep(0.6, 0.88, S);
		const double Settle = Tilt + 0.14 * std::sin(Tilt * Pi) * Tilt;
		double Recoil = 0.0;
		if (bAnchored && Since >= 0)
		{
			Recoil = Since < 0.04 ? Since / 0.04 : FMath::Square(FMath::Max(0.0, 1.0 - (Since - 0.04) / 1.0));
		}
		{
			FSkNode N(M, T.AnchorGun);
			N.Position.Y = Rhino::Trunnion0 + (Rhino::Trunnion1 - Rhino::Trunnion0) * Up;
			const FVector E = RestEuler(M, T.AnchorGun);
			N.SetEuler(E.X, E.Y, -0.03 + (Rhino::GunPitch + 0.03) * Settle + 0.05 * Recoil * (bAnchored ? 1.0 : 0.0));
			N.Apply(P, T.AnchorGun);
		}
		{
			FSkNode N(M, T.AnchorSlide);
			N.Position.X = Rhino::SlideRun0 + (Rhino::SlideRun1 - Rhino::SlideRun0) * Extend - 0.4 * Recoil;
			N.Apply(P, T.AnchorSlide);
		}
		// (The rhino has no clamps.) Rail lights: stutter on as it locks,
		// breathe while anchored, flare as it fires.
		const double Flicker = Lock > 0 && Lock < 1 ? (std::sin(Time * 60.0) > 0 ? 1.0 : 0.3) : 1.0;
		const double LockGlow = Lock * Flicker * (1.3 + 0.3 * std::sin(Time * 2.2 + Seed))
			+ (bAnchored && Since >= 0 ? 1.6 * FMath::Max(0.0, 1.0 - Since / 0.6) : 0.0);
		for (const int32 Part : {T.AnchorGun, T.AnchorSlide})
		{
			if (Part != INDEX_NONE) P.MeshEmission.Add({Part, FName(TEXT("lockGlow")), float(LockGlow)});
		}
		{
			SetVisible(P, T.BigFlash, bAnchored && Since >= 0 && Since < 0.1);
			FSkNode N(M, T.BigFlash);
			const FVector E = RestEuler(M, T.BigFlash);
			N.SetEuler(Since * 53.0, E.Y, E.Z);
			N.Scale = FVector(0.8 + 4.0 * FMath::Max(0.0, Since));
			N.Apply(P, T.BigFlash);
		}
		// Smoke ring: grows, drifts out along the barrel, fades.
		{
			const double RingAge = bAnchored ? Since : 100.0;
			const bool bRing = RingAge >= 0 && RingAge < 1.6;
			SetVisible(P, T.Smoke, bRing);
			if (bRing && T.Smoke != INDEX_NONE)
			{
				const double F = RingAge / 1.6;
				const double Grow = 0.5 + 1.5 * (1.0 - FMath::Pow(1.0 - F, 3.0));
				FSkNode N(M, T.Smoke);
				N.Scale = FVector(Grow, 0.5 + 0.9 * F, Grow);
				N.Position.X = 1.85 + 0.45 * (1.0 - FMath::Square(1.0 - F));
				N.Apply(P, T.Smoke);
				P.Emission[T.Smoke] = float(0.6 * FMath::Pow(1.0 - F, 1.5));  // opacity (M_AcSmokeRing)
			}
		}

		// Warning lamps blink while between modes; the antenna tip blinks.
		const bool bBusy = S > 0.001 && S < 0.999;
		const bool bBlink = std::sin(Time * 9.0 + Seed) > 0;
		for (int32 K = 0; K < 4; ++K) SetVisible(P, T.WarnLamps[K], bBusy && (bBlink != (K % 2 == 0)));
		SetVisible(P, T.Beacon, !(std::fmod(Time + Seed, 1.6) > 0.12));
		P.bAnimated = true;
	}

	// MARK: - Hailstorm

	/// `Models.Hailstorm` constants.
	namespace Flak
	{
		constexpr double Rest = 0.35, Raised = 0.75;
		constexpr double WheelRadius = 0.24, BarrelX = 0.2;
		/// The cradle eases at most 0.05 rad a frame in Swift (60 Hz): 3 rad/s.
		constexpr double CradleRate = 0.05 * 60.0;
		/// `radarGlow`'s exported intensity (glowOrange's).
		constexpr double RadarGlowExported = 1.8;
	}

	struct FHailstormState : FAcPoseState
	{
		TOptional<double> Cradle;
	};

	struct FHailstormParts
	{
		int32 Turret = INDEX_NONE, Cradle = INDEX_NONE, Radar = INDEX_NONE;
		int32 Wheels[2] = {INDEX_NONE, INDEX_NONE};
		int32 Barrels[4], Flashes[4];
		int32 Treads[2][TreadPhases];

		FHailstormParts() = default;
		explicit FHailstormParts(const FAcModelInfo& M)
		{
			Turret = PartOf(M, TEXT("turret"));
			Cradle = PartOf(M, TEXT("cradle"));
			Radar = PartOf(M, TEXT("radar"));
			for (int32 I = 0; I < 2; ++I)
			{
				Wheels[I] = PartOf(M, FString::Printf(TEXT("wheels_%d"), I));
				for (int32 K = 0; K < TreadPhases; ++K) Treads[I][K] = PartOf(M, FString::Printf(TEXT("treads_%d_%d"), I, K));
			}
			for (int32 K = 0; K < 4; ++K)
			{
				Barrels[K] = PartOf(M, FString::Printf(TEXT("barrels_%d"), K));
				Flashes[K] = PartOf(M, FString::Printf(TEXT("flashes_%d"), K));
			}
		}
	};

	/// `GameScene.pose(_ m: Models.Hailstorm, ...)`, from the `.hailstorm`
	/// case of `sync`.
	void PoseHailstorm(const FAcPoseContext& C, FAcPose& P)
	{
		const ac::Unit& U = *C.Unit;
		const FAcModelInfo& M = C.Model;
		const FHailstormParts& H = PartsOf<FHailstormParts>(M);
		FHailstormState& Mem = C.Memory.State<FHailstormState>();

		// `sync`: aim like a tank's turret (only flyers are its targets);
		// it tracks while it has a target or fired in the last 1.5 s.
		const double Aim = TurretAim(C, false);
		const TOptional<double> Shot = C.Memory.LastShot;
		const bool bTracking = U.target.has_value() || (Shot && C.Time - *Shot < 1.5);
		const bool bMoving = U.walking();
		const double Stride = U.stride, Heading = U.heading, Time = C.Time;

		const double T = Time + double(U.id) * 1.37;
		if (bMoving)
		{
			P.Placement.AddToTranslation(FVector(0, 0, AcSpace::ToCm(0.01 * std::sin(Stride * 9.0))));
		}
		{
			FSkNode N(M, H.Turret);
			const FVector E = RestEuler(M, H.Turret);
			N.SetEuler(E.X, Remainder2Pi(Heading - Aim), E.Z);
			N.Apply(P, H.Turret);
		}
		{
			const FVector E = RestEuler(M, H.Cradle);
			const double Want = bTracking ? Flak::Raised : Flak::Rest;
			const double Now = Mem.Cradle.Get(E.Z);
			const double Step = Flak::CradleRate * C.Dt;
			const double Next = Now + FMath::Clamp(Want - Now, -Step, Step);
			Mem.Cradle = Next;
			FSkNode N(M, H.Cradle);
			N.SetEuler(E.X, E.Y, Next);
			N.Apply(P, H.Cradle);
		}
		for (const int32 W : H.Wheels)
		{
			FSkNode N(M, W);
			const FVector E = RestEuler(M, W);
			N.SetEuler(E.X, E.Y, -Stride / Flak::WheelRadius);
			N.Apply(P, W);
		}
		const int32 Shown = TreadShown(Stride);
		for (int32 I = 0; I < 2; ++I)
		{
			for (int32 F = 0; F < TreadPhases; ++F) SetVisible(P, H.Treads[I][F], F == Shown);
		}
		{
			FSkNode N(M, H.Radar);
			const FVector E = RestEuler(M, H.Radar);
			N.SetEuler(E.X, T * (bTracking ? 6.0 : 1.5), E.Z);
			N.Apply(P, H.Radar);
			const double Glow = bTracking ? 1.2 + 1.2 * (0.5 + 0.5 * std::sin(T * 12.0)) : 0.9;
			if (H.Radar != INDEX_NONE)
			{
				P.MeshEmission.Add({H.Radar, FName(TEXT("radarGlow")), float(Glow / Flak::RadarGlowExported)});
			}
		}
		const double Since = Shot ? Time - *Shot : 10.0;
		for (int32 K = 0; K < 4; ++K)
		{
			const double S = Since - double(K / 2) * 0.1;
			const double Kick = S >= 0 && S < 0.12 ? 1.0 - S / 0.12 : 0.0;
			FSkNode N(M, H.Barrels[K]);
			N.Position.X = Flak::BarrelX - 0.08 * Kick;
			N.Apply(P, H.Barrels[K]);
			SetVisible(P, H.Flashes[K], S >= 0 && S < 0.05);
		}
		P.bAnimated = true;
	}

	FAcPoseRegistration LongbowPose(ac::UnitKind::longbow, &PoseLongbow);
	FAcPoseRegistration HailstormPose(ac::UnitKind::hailstorm, &PoseHailstorm);
}
