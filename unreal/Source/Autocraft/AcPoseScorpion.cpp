// The Scorpion's pose (docs/new-units.md "Looks", art/models/scorpion/
// parts.txt): a six-legged tripod gait (three legs at a time), the tail
// curling up to aim and snapping forward to fire, the back plates shifting
// as it buries; burying: the pincers and legs dig, the body sinks over two
// seconds (the sim's `anchor`), a mound of soil heaps up; the sting: the tail
// rises out of the ground for the lock-on second and turns on its victim, the
// tail lamp blinks slowly while the sting reloads and burns steady when it is
// ready, the eyes burn brighter while it hunts.
//
// Not here: the owner's faint team-coloured outline and the enemy's red one
// round a buried Scorpion (the renderer's outline pass, AcOutline.h: it moves
// the instances to components that write a stencil value), the laser line
// from the launcher during the lock (AcEffectsNewKinds.cpp, from the same
// sim fields).
#include "AcPoseNewKinds.h"

#include "AcModelCatalog.h"
#include "AcSpace.h"

#include <cmath>

namespace
{
	FQuat RotX(const double A) { return FQuat(FVector(1, 0, 0), A); }
	FQuat RotY(const double A) { return FQuat(FVector(0, 1, 0), A); }
	FQuat RotZ(const double A) { return FQuat(FVector(0, 0, 1), A); }

	double Smoothstep(const double A, const double B, const double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	void Turn(FAcPose& P, const FAcModelInfo& M, const int32 I, const FQuat& Q)
	{
		if (I == INDEX_NONE) return;
		P.Local[I].SetRotation((Q * M.Parts[I].Rest.GetRotation()).GetNormalized());
	}

	void Glow(FAcPose& P, const int32 I, const double Value)
	{
		if (I != INDEX_NONE) P.Emission[I] = float(Value);
	}

	struct FScorpionState : FAcPoseState
	{
		double Walk = 0.0;
		/// The tail's turn toward its victim, radians off the heading.
		double TailYaw = 0.0, TailPitch = 0.0;
	};

	void PoseScorpion(const FAcPoseContext& C, FAcPose& P)
	{
		static const FAcPartIndex Body(TEXT("body")), Eyes(TEXT("eyes")), Launcher(TEXT("launcher")),
			TailLamp(TEXT("tailLamp")), Mound(TEXT("mound"));
		static const FAcPartIndex Plates[5] = {FAcPartIndex(TEXT("plates_0")), FAcPartIndex(TEXT("plates_1")),
			FAcPartIndex(TEXT("plates_2")), FAcPartIndex(TEXT("plates_3")), FAcPartIndex(TEXT("plates_4"))};
		static const FAcPartIndex Legs[6] = {FAcPartIndex(TEXT("legs_0")), FAcPartIndex(TEXT("legs_1")),
			FAcPartIndex(TEXT("legs_2")), FAcPartIndex(TEXT("legs_3")), FAcPartIndex(TEXT("legs_4")),
			FAcPartIndex(TEXT("legs_5"))};
		static const FAcPartIndex Shins[6] = {FAcPartIndex(TEXT("shins_0")), FAcPartIndex(TEXT("shins_1")),
			FAcPartIndex(TEXT("shins_2")), FAcPartIndex(TEXT("shins_3")), FAcPartIndex(TEXT("shins_4")),
			FAcPartIndex(TEXT("shins_5"))};
		static const FAcPartIndex Pincers[2] = {FAcPartIndex(TEXT("pincers_0")), FAcPartIndex(TEXT("pincers_1"))};
		static const FAcPartIndex Tail[4] = {FAcPartIndex(TEXT("tail_0")), FAcPartIndex(TEXT("tail_1")),
			FAcPartIndex(TEXT("tail_2")), FAcPartIndex(TEXT("tail_3"))};

		const FAcModelInfo& M = C.Model;
		const ac::Unit& U = *C.Unit;
		const double Time = C.Time;
		const double T = Time + double(U.id) * 1.37;
		FScorpionState& S = C.Memory.State<FScorpionState>();

		const double Anchor = FMath::Clamp(U.anchor.value_or(0.0), 0.0, 1.0);
		const bool bDigging = Anchor > 0.0 && Anchor < 1.0;
		const double Walking = (U.walking() && Anchor <= 0.0) ? 1.0 : 0.0;
		// With no time passing (a paused game, a still) it is where it is going.
		const double Ease = C.Dt > 0.0 ? FMath::Min(1.0, C.Dt * 8.0) : 1.0;
		S.Walk += (Walking - S.Walk) * (C.Dt > 0.0 ? FMath::Min(1.0, C.Dt * 6.0) : 1.0);
		P.bAnimated = true;

		// The lock-on: a second held on a victim (game time, from the sim).
		const double Now = C.State.time;
		double Lock = 0.0;
		TOptional<FVector> Aim;
		if (U.lockTarget && U.lockFrom && U.lockAt && Now - *U.lockAt < 0.25)
		{
			Lock = FMath::Clamp((Now - *U.lockFrom) / 1.0, 0.0, 1.0);
			if (const std::optional<ac::Unit> V = C.State.unit(*U.lockTarget))
			{
				const double Chest = V->stats().air ? AcPose::Hover(V->kind) : 0.62;
				Aim = AcSpace::ToWorld(V->position, C.Ground(V->position) + Chest);
			}
		}

		// The sink, the heave of the digging and the walk's bob.
		const double Cycle = U.stride / AcPoseNew::ScorpionCycle;
		const double Dig = bDigging ? 1.0 : 0.0;
		if (const int32 I = Body.Get(M); I != INDEX_NONE)
		{
			FVector Pos = M.Parts[I].Rest.GetTranslation();
			Pos.Z += -AcPoseNew::ScorpionSink(Anchor) + 2.5 * std::abs(std::sin(UE_DOUBLE_PI * 2.0 * Cycle)) * S.Walk
				+ 1.5 * std::sin(Time * 17.0) * Dig;
			P.Local[I].SetTranslation(Pos);
			Turn(P, M, I, RotX(0.02 * std::sin(UE_DOUBLE_PI * 2.0 * Cycle) * S.Walk + 0.025 * std::sin(Time * 13.0) * Dig));
		}

		// The legs: two tripods; digging, all six scrabble.
		for (int32 L = 0; L < 6; ++L)
		{
			const double Side = L < 3 ? 1.0 : -1.0;  // the left legs reach out to -Y
			const AcPoseNew::FScorpionLeg Leg =
				AcPoseNew::ScorpionLeg(Cycle + 0.5 * double(AcPoseNew::TripodOf(L)));
			double Swing = Leg.Swing * S.Walk;
			double Lift = Leg.Lift * S.Walk;
			if (Dig > 0.0)
			{
				const double Beat = std::sin(Time * 15.0 + double(L) * 1.9);
				Swing += 0.38 * Beat;
				Lift += 0.5 * (0.5 + 0.5 * std::sin(Time * 15.0 + double(L) * 1.9 + 1.2));
			}
			Turn(P, M, Legs[L].Get(M), RotZ(Side * Swing) * RotX(-Side * 0.3 * Lift));
			Turn(P, M, Shins[L].Get(M), RotX(-Side * 0.55 * Lift));
		}

		// The pincers: they dig, they open as it hunts, they work as it walks.
		const double Open = 0.12 + 0.1 * std::sin(T * 3.0) * (1.0 - Dig) + 0.45 * Lock
			+ 0.4 * (0.5 + 0.5 * std::sin(Time * 11.0)) * Dig;
		for (int32 K = 0; K < 2; ++K) Turn(P, M, Pincers[K].Get(M), RotZ((K == 0 ? -1.0 : 1.0) * Open));

		// The back plates ripple as it buries or digs out, one after another,
		// and rock a little with the gait.
		for (int32 K = 0; K < 5; ++K)
		{
			const double Ripple = std::sin(UE_DOUBLE_PI * FMath::Clamp(Anchor * 1.5 - 0.1 * double(K), 0.0, 1.0)) * Dig;
			const double Rock = 0.04 * std::sin(UE_DOUBLE_PI * 4.0 * Cycle + 0.7 * double(K)) * S.Walk;
			if (const int32 I = Plates[K].Get(M); I != INDEX_NONE)
			{
				Turn(P, M, I, RotY(-0.5 * Ripple + Rock));
				P.Local[I].AddToTranslation(FVector(-4.0 * Ripple, 0.0, 3.0 * Ripple));
			}
		}

		// The mound: heaped up as it goes down, gone as it comes out.
		if (const int32 I = Mound.Get(M); I != INDEX_NONE)
		{
			const double Heap = AcPoseNew::ScorpionMound(Anchor);
			P.Visible[I] = Heap > 0.01;
			const double Heave = 1.0 + 0.06 * std::sin(Time * 17.0) * Dig;
			P.Local[I].SetScale3D(M.Parts[I].Rest.GetScale3D()
				* FVector((0.45 + 0.35 * Heap) * Heave, (0.45 + 0.35 * Heap) * Heave, FMath::Max(Heap, 0.02)));
		}

		// The tail: laid back flat in the ground, rising for the lock, curling
		// up to aim, and snapping forward at the shot.
		const double Laid = AcPoseNew::ScorpionTailLaid(Anchor, Lock);
		const double Snap = C.Memory.LastShot ? AcPoseNew::ScorpionSnap(Time - *C.Memory.LastShot) : 0.0;
		double YawWant = 0.0, PitchWant = 0.0;
		if (Aim)
		{
			const FVector At = AcSpace::ToWorld(U.position, C.Ground(U.position)) + FVector(0, 0, 120);
			YawWant = FMath::Clamp(std::remainder(std::atan2(Aim->Y - At.Y, Aim->X - At.X) - U.heading, 2.0 * UE_DOUBLE_PI), -1.2, 1.2);
			PitchWant = FMath::Clamp(std::atan2(Aim->Z - At.Z, FMath::Max(FVector::Dist2D(At, *Aim), 100.0)), -0.7, 0.9);
		}
		S.TailYaw += (YawWant - S.TailYaw) * Ease;
		S.TailPitch += (PitchWant - S.TailPitch) * Ease;
		// A walking Scorpion's tail sways; a ready one raises it a little higher.
		const double Sway = 0.06 * std::sin(T * 1.6) * (1.0 - Laid);
		static constexpr double Lay[4] = {-1.6, -0.5, -0.45, -0.3};
		static constexpr double Curl[4] = {0.0, 0.08, 0.1, 0.12};
		for (int32 K = 0; K < 4; ++K)
		{
			double A = Lay[K] * Laid + Curl[K] * Lock + 0.3 * Snap * double(K > 0) + Sway * double(K > 0);
			FQuat Q = RotY(A);
			if (K == 0) Q = RotZ(S.TailYaw) * Q;
			Turn(P, M, Tail[K].Get(M), Q);
		}
		Turn(P, M, Launcher.Get(M), RotY(-0.9 * S.TailPitch * Lock - 0.2 * Laid + 0.3 * Snap));

		// The tail lamp: slow blinks while it reloads (faster with the lock),
		// steady when ready. The eyes: brighter as it hunts.
		const bool bReloading = U.cooldown.value_or(0.0) > 0.0;
		double Lamp = 1.4;
		if (bReloading) Lamp = 0.15 + 1.4 * Smoothstep(0.3, 0.7, 0.5 + 0.5 * std::sin(UE_DOUBLE_PI * 0.8 * T));
		else if (Lock > 0.0) Lamp = 1.4 + 1.6 * (0.5 + 0.5 * std::sin(Time * (8.0 + 14.0 * Lock)));
		Glow(P, TailLamp.Get(M), Lamp);
		const bool bHunting = Lock > 0.0 || U.task == ac::Unit::Task::attacking;
		Glow(P, Eyes.Get(M), (bHunting ? 1.9 : 0.8) * (0.5 + 0.5 * (1.0 - Anchor)) + 1.0 * Lock * Anchor);
	}

	FAcPoseRegistration ScorpionReg(ac::UnitKind::scorpion, &PoseScorpion);
}
