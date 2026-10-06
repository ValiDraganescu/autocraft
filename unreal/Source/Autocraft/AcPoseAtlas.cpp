// The Atlas's pose (docs/new-units.md "Looks", art/models/atlas/parts.txt):
// a heavy, slow walk from the distance walked (hips, knees, ankles and feet
// of two legs, the body bobbing and rolling over the planted one), the torso
// turning on its own to the sim's aim, the cannons pitching at the target and
// kicking back one after the other, the back vents glowing after each volley,
// amber knee lamps pulsing with the steps, the searchlight lit at night, the
// Quake stomp (one leg up, then driven down, timed with `GameEvent::Stomp`:
// the sim stamps `stompedAt`), and a `AtlasStep` cue for every footfall (the
// dust, the footprint and the shake are `UAcEffects::Footfall`).
// The death is AcShatter's `AtlasFallPose`.
#include "AcPoseNewKinds.h"

#include "AcModelCatalog.h"
#include "AcSpace.h"

#include <cmath>

namespace
{
	FQuat RotX(const double A) { return FQuat(FVector(1, 0, 0), A); }
	FQuat RotY(const double A) { return FQuat(FVector(0, 1, 0), A); }
	FQuat RotZ(const double A) { return FQuat(FVector(0, 0, 1), A); }

	void Turn(FAcPose& P, const FAcModelInfo& M, const int32 I, const FQuat& Q)
	{
		if (I == INDEX_NONE) return;
		P.Local[I].SetRotation((Q * M.Parts[I].Rest.GetRotation()).GetNormalized());
	}

	void Glow(FAcPose& P, const int32 I, const double Value)
	{
		if (I != INDEX_NONE) P.Emission[I] = float(Value);
	}

	/// The hip's height over the sole at rest, cm (the model's hip to its
	/// foot): how far a leg swung by `h` drops the hip.
	constexpr double LegCm = 145.0;
	/// The shin's length, cm.
	constexpr double ShinCm = 65.0;
	/// The cannons' trunnions and the ground under the sights, cm over the
	/// model root (`parts.txt`).
	constexpr double CannonZ = 270.0;

	struct FAtlasState : FAcPoseState
	{
		double Walk = 0.0;
		double Pitch = 0.0;
		/// The whole cycles each leg had last frame (a foot came down when it changes).
		TOptional<int32> Whole[2];
	};

	void PoseAtlas(const FAcPoseContext& C, FAcPose& P)
	{
		static const FAcPartIndex Body(TEXT("body")), Torso(TEXT("torso")), Vents(TEXT("vents")),
			Searchlight(TEXT("searchlight"));
		static const FAcPartIndex Cannons[2] = {FAcPartIndex(TEXT("cannons_0")), FAcPartIndex(TEXT("cannons_1"))};
		static const FAcPartIndex Hips[2] = {FAcPartIndex(TEXT("hips_0")), FAcPartIndex(TEXT("hips_1"))};
		static const FAcPartIndex Knees[2] = {FAcPartIndex(TEXT("knees_0")), FAcPartIndex(TEXT("knees_1"))};
		static const FAcPartIndex Lamps[2] = {FAcPartIndex(TEXT("kneeLamps_0")), FAcPartIndex(TEXT("kneeLamps_1"))};
		static const FAcPartIndex Ankles[2] = {FAcPartIndex(TEXT("ankles_0")), FAcPartIndex(TEXT("ankles_1"))};
		static const FAcPartIndex Feet[2] = {FAcPartIndex(TEXT("feet_0")), FAcPartIndex(TEXT("feet_1"))};

		const FAcModelInfo& M = C.Model;
		const ac::Unit& U = *C.Unit;
		P.bAnimated = true;
		const double Time = C.Time;
		FAtlasState& S = C.Memory.State<FAtlasState>();

		// How far it walks: the legs' amplitude fades in and out with it.
		const double Walking = U.walking() ? 1.0 : 0.0;
		// With no time passing (a paused game, a still) it is where it is going.
		S.Walk += (Walking - S.Walk) * (C.Dt > 0.0 ? FMath::Min(1.0, C.Dt * 5.0) : 1.0);

		// The stomp, in game time (the sim stamps it): the stomping leg is
		// the same one every time for an Atlas.
		const int32 StompLeg = int32(U.id & 1);
		const double SinceStomp = U.stompedAt ? C.State.time - *U.stompedAt : -1.0;
		const AcPoseNew::FAtlasStomp Stomp = AcPoseNew::AtlasStomp(SinceStomp);
		const double Gait = S.Walk * (1.0 - Stomp.Weight);

		// The legs: the left at the cycle's phase, the right half a cycle on.
		const double Cycle = U.stride / AcPoseNew::AtlasCycle;
		double Hip[2], Knee[2], Ankle[2], Lift[2];
		double Drop = 0.0;
		int32 Stance = 0;
		for (int32 K = 0; K < 2; ++K)
		{
			const double Phase = Cycle + 0.5 * double(K);
			const AcPoseNew::FAtlasLeg Leg = AcPoseNew::AtlasLeg(Phase);
			Hip[K] = Leg.Hip * Gait;
			Knee[K] = Leg.Knee * Gait;
			Ankle[K] = Leg.Ankle * Gait;
			Lift[K] = Leg.Lift * Gait;
			if (Leg.bStance)
			{
				Drop += LegCm * (1.0 - std::cos(Hip[K])) + 0.5 * ShinCm * (1.0 - std::cos(Knee[K]));
				++Stance;
			}
			// A foot came down: dust, a footprint and a little shake.
			const int32 Whole = int32(std::floor(Phase));
			if (S.Whole[K] && *S.Whole[K] != Whole && S.Walk > 0.5 && Walking > 0.5)
			{
				FAcPoseCue Step;
				Step.What = TEXT("AtlasStep");
				Step.At = P.Placement.TransformPosition(FVector(45.0, K == 0 ? -60.0 : 60.0, 0.0));
				Step.Value = float(K);
				P.Cues.Add(Step);
			}
			S.Whole[K] = Whole;
		}
		if (Stance > 0) Drop /= double(Stance);

		// The stomp takes over: the stomping leg rises and drives down, the
		// other bends to take the weight.
		const double W = Stomp.Weight;
		if (W > 0.0)
		{
			const int32 Other = 1 - StompLeg;
			Hip[StompLeg] += Stomp.Hip;
			Knee[StompLeg] += Stomp.Knee;
			Ankle[StompLeg] += Stomp.Ankle;
			Knee[Other] += Stomp.Support;
			Ankle[Other] -= Stomp.Support;
		}
		Drop += Stomp.Drop;

		// The whole body: down over the planted foot, rolling and leaning
		// into each step, down and forward with the stomp.
		const double Roll = 0.03 * std::sin(UE_DOUBLE_PI * 2.0 * Cycle) * S.Walk;
		const double Lean = 0.025 * S.Walk + Stomp.Pitch;
		if (const int32 I = Body.Get(M); I != INDEX_NONE)
		{
			FVector Pos = M.Parts[I].Rest.GetTranslation();
			Pos.Z -= Drop;
			Pos.Y += 5.0 * std::sin(UE_DOUBLE_PI * 2.0 * Cycle) * S.Walk;
			P.Local[I].SetTranslation(Pos);
			Turn(P, M, I, RotY(Lean) * RotX(Roll));
		}
		for (int32 K = 0; K < 2; ++K)
		{
			Turn(P, M, Hips[K].Get(M), RotY(-Hip[K]));
			Turn(P, M, Knees[K].Get(M), RotY(Knee[K]));
			Turn(P, M, Ankles[K].Get(M), RotY(Ankle[K]));
			// The toe lifts as the foot leaves and plants flat.
			Turn(P, M, Feet[K].Get(M), RotY(-0.22 * Lift[K]));
			// The knee lamps pulse as the foot comes down, and flare with the stomp.
			const double Beat = 0.5 + 0.5 * std::cos(UE_DOUBLE_PI * 2.0 * (Cycle + 0.5 * double(K)));
			double Lamp = 0.7 + 0.9 * Beat * S.Walk;
			if (K == StompLeg) Lamp += 3.0 * Stomp.Flare;
			Glow(P, Lamps[K].Get(M), Lamp);
		}

		// The torso turns on its own to where the sim aims (3 rad/s there),
		// the cannons pitch at the target and kick back.
		const double Yaw = std::remainder((U.aim ? *U.aim : U.heading) - U.heading, 2.0 * UE_DOUBLE_PI);
		Turn(P, M, Torso.Get(M), RotZ(Yaw));
		// The searchlight sweeps a little when the Atlas stands, and burns when it is dark (`C.Dark`, from AcDaylight).
		if (const int32 I = Searchlight.Get(M); I != INDEX_NONE)
		{
			Turn(P, M, I, RotZ(0.2 * std::sin(Time * 0.45 + double(U.id)) * (1.0 - S.Walk)));
			Glow(P, I, AcPoseNew::AtlasSearchlight(C.Dark));
		}

		double Elevation = 0.0;
		TOptional<FVector> Aim;
		const bool bFresh = C.Memory.LastShot && Time - *C.Memory.LastShot < 2.5;
		if (C.bDriven && C.PilotAim)
		{
			Aim = *C.PilotAim;
		}
		else if (U.target)
		{
			if (const std::optional<ac::Unit> T = C.State.unit(*U.target))
				Aim = AcSpace::ToWorld(T->position, C.Ground(T->position) + 0.62);
			else if (const std::optional<ac::Structure> B = C.State.structure(*U.target))
				Aim = AcSpace::ToWorld(B->position, C.Ground(B->position) + 0.9);
		}
		else if (bFresh && C.Memory.LastShotAt)
		{
			const ac::Vec2 At = *C.Memory.LastShotAt;
			Aim = AcSpace::ToWorld(At, C.Ground(At) + 0.62);
		}
		if (Aim)
		{
			const FVector From = AcSpace::ToWorld(U.position, C.Ground(U.position)) + FVector(0, 0, CannonZ);
			const double Along = FVector::Dist2D(From, *Aim);
			Elevation = std::atan2(Aim->Z - From.Z, FMath::Max(Along, 100.0));
		}
		const double Want = FMath::Clamp(Elevation, -0.55, 0.35);
		S.Pitch += C.Dt > 0.0 ? FMath::Clamp(Want - S.Pitch, -3.0 * C.Dt, 3.0 * C.Dt) : Want - S.Pitch;
		const double Since = C.Memory.LastShot ? Time - *C.Memory.LastShot : 1e9;
		for (int32 K = 0; K < 2; ++K)
		{
			const int32 I = Cannons[K].Get(M);
			if (I == INDEX_NONE) continue;
			// The left cannon fires first, the right a beat after.
			const double Kick = AcPoseNew::AtlasRecoil(Since - double(K) * AcPoseNew::AtlasSecondDelay);
			const FQuat Pitch = RotY(-S.Pitch);
			P.Local[I].SetTranslation(M.Parts[I].Rest.GetTranslation()
				+ Pitch.RotateVector(FVector(-Kick * AcPoseNew::AtlasRecoilCm, 0.0, 0.0)));
			Turn(P, M, I, Pitch);
		}
		Glow(P, Vents.Get(M), AcPoseNew::AtlasVentGlow(Since));
	}

	FAcPoseRegistration AtlasReg(ac::UnitKind::atlas, &PoseAtlas);
}
