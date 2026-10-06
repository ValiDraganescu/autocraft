// The Peregrine's pose (docs/new-units.md "Looks", art/models/peregrine/
// parts.txt): the dart banks harder than any other flyer (up to 60 degrees),
// its flaps swing against the bank, its fins swing with the turn, the engine
// glow grows and brightens with speed, a missile leaves its rail with each
// volley and slides back on after the reload, the wing-tip beacons blink.
#include "AcPoseNewKinds.h"

#include "AcModelCatalog.h"
#include "AcPoseAir.h"
#include "AcSpace.h"

#include <cmath>

namespace
{
	FQuat RotX(const double A) { return FQuat(FVector(1, 0, 0), A); }
	FQuat RotY(const double A) { return FQuat(FVector(0, 1, 0), A); }
	FQuat RotZ(const double A) { return FQuat(FVector(0, 0, 1), A); }

	/// A part turned by `Q` in its parent's frame, about its own pivot.
	void Turn(FAcPose& P, const FAcModelInfo& M, const int32 I, const FQuat& Q)
	{
		if (I == INDEX_NONE) return;
		P.Local[I].SetRotation((Q * M.Parts[I].Rest.GetRotation()).GetNormalized());
	}

	void Glow(FAcPose& P, const int32 I, const double Value)
	{
		if (I != INDEX_NONE) P.Emission[I] = float(Value);
	}

	/// Where it was last frame and how fast it flies, smoothed (0 at rest,
	/// 1 at the speed it was built for).
	struct FPeregrineState : FAcPoseState
	{
		TOptional<ac::Vec2> Last;
		double Pace = 0.0;
	};

	void PosePeregrine(const FAcPoseContext& C, FAcPose& P)
	{
		static const FAcPartIndex Body(TEXT("body"));
		static const FAcPartIndex Flaps[2] = {FAcPartIndex(TEXT("flaps_0")), FAcPartIndex(TEXT("flaps_1"))};
		static const FAcPartIndex Fins[2] = {FAcPartIndex(TEXT("fins_0")), FAcPartIndex(TEXT("fins_1"))};
		static const FAcPartIndex Engines[2] = {FAcPartIndex(TEXT("engines_0")), FAcPartIndex(TEXT("engines_1"))};
		static const FAcPartIndex Glows[2] = {FAcPartIndex(TEXT("glow")), FAcPartIndex(TEXT("glow_2"))};
		static const FAcPartIndex Missiles[4] = {FAcPartIndex(TEXT("missiles_0")), FAcPartIndex(TEXT("missiles_1")),
			FAcPartIndex(TEXT("missiles_2")), FAcPartIndex(TEXT("missiles_3"))};
		static const FAcPartIndex Beacons[2] = {FAcPartIndex(TEXT("beacon_0")), FAcPartIndex(TEXT("beacon_1"))};

		const FAcModelInfo& M = C.Model;
		const ac::Unit& U = *C.Unit;
		P.bAnimated = true;
		const double Time = C.Time;
		const double T = Time + double(U.id) * 1.91;

		FPeregrineState& S = C.Memory.State<FPeregrineState>();
		if (S.Last && C.Dt > 1e-4)
		{
			const double Speed = ac::distance(*S.Last, U.position) / C.Dt;
			const double Top = FMath::Max(U.stats().speed, 0.1);
			S.Pace += (FMath::Clamp(Speed / Top, 0.0, 1.0) - S.Pace) * FMath::Min(1.0, C.Dt * 3.0);
		}
		else if (!S.Last)
		{
			// First sight: it is flying if it is moving.
			S.Pace = U.walking() ? 1.0 : 0.0;
		}
		S.Last = U.position;
		const double Pace = S.Pace;

		const double Turning = AcPose::Turning(C);
		const double Bank = AcPoseNew::PeregrineBank(Turning);
		// -1 to 1, a hard turn to the right at 1.
		const double B = Bank / AcPoseNew::PeregrineMaxBank;

		// Just out of its Spacedock it still rises to its hover.
		double Lift = 0.0;
		if (C.Memory.BornAt)
		{
			if (const TOptional<double> Drop = AcPoseAir::LaunchDrop(C.Time - *C.Memory.BornAt, AcPoseNew::PeregrineHover))
			{
				Lift = *Drop;
			}
			else
			{
				C.Memory.BornAt.Reset();
			}
		}
		P.Placement.AddToTranslation(FVector(0.0, 0.0, -AcSpace::ToCm(Lift)));

		// The airframe: hover with a quick bob, rolled into the turn (right
		// wing down for a right turn), nose down in flight, nose up a little
		// with the g of a hard bank.
		const double Bob = 0.045 * std::sin(T * 1.7) + 0.018 * std::sin(T * 3.1 + 1.0);
		const int32 BodyI = Body.Get(M);
		if (BodyI != INDEX_NONE)
		{
			FVector Pos = M.Parts[BodyI].Rest.GetTranslation();
			Pos.Z = AcSpace::ToCm(AcPoseNew::PeregrineHover + Bob);
			P.Local[BodyI].SetTranslation(Pos);
			const double Idle = (1.0 - Pace) * 0.03 * std::sin(T * 1.1);
			const double Pitch = 0.12 * Pace - 0.05 * std::abs(B) - Idle;
			const double Sway = (1.0 - Pace) * 0.02 * std::sin(T * 0.8);
			Turn(P, M, BodyI, RotZ(Sway) * RotY(Pitch) * RotX(-Bank + Idle));
		}

		// The flaps work against the bank (the left one's edge goes down in
		// a right turn), the fins swing with the turn.
		for (int32 K = 0; K < 2; ++K)
		{
			const double Side = K == 0 ? -1.0 : 1.0;
			Turn(P, M, Flaps[K].Get(M), RotY(Side * 0.5 * B + 0.03 * std::sin(T * 2.3 + Side)));
			Turn(P, M, Fins[K].Get(M), RotZ(-0.35 * B));
			// The nacelles shake with the thrust and swing a little with the turn.
			const double Shake = 0.025 * Pace * std::sin(Time * 47.0 + Side * 2.0) * std::sin(Time * 13.0);
			Turn(P, M, Engines[K].Get(M), RotZ(-0.1 * B) * RotY(Shake));
			// The glow: a longer, hotter jet the faster it flies.
			const int32 G = Glows[K].Get(M);
			const double Flicker = 0.1 * std::sin(Time * 37.0 + Side * 3.0) * std::sin(Time * 13.0);
			Glow(P, G, 0.7 + 1.5 * Pace + Flicker);
			if (G != INDEX_NONE)
			{
				const FVector Rest = M.Parts[G].Rest.GetScale3D();
				const double Thick = 0.85 + 0.25 * Pace;
				P.Local[G].SetScale3D(Rest * FVector(0.7 + 0.9 * Pace, Thick, Thick));
			}
			Glow(P, Beacons[K].Get(M), AcPoseNew::PeregrineBeacon(T, K));
		}

		// A volley takes a pair of missiles off their rails; they come back
		// one second later, sliding on from behind.
		if (C.Memory.LastShot)
		{
			const double Shot = *C.Memory.LastShot;
			for (int32 K = 0; K < 4; ++K)
			{
				const int32 I = Missiles[K].Get(M);
				if (I == INDEX_NONE) continue;
				const AcPoseNew::FMissile Rail = AcPoseNew::MissileOnRail(K, Shot, Time - Shot);
				P.Visible[I] = Rail.bShown;
				if (Rail.Scale < 1.0)
				{
					P.Local[I].SetScale3D(M.Parts[I].Rest.GetScale3D() * FMath::Max(Rail.Scale, 1e-3));
					P.Local[I].AddToTranslation(FVector(-Rail.Back, 0.0, 0.0));
				}
			}
		}
	}

	FAcPoseRegistration PeregrineReg(ac::UnitKind::peregrine, &PosePeregrine);
}
