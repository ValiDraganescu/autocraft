// Chunk C2 (GAME-LAYER.md §2.7): marks on the ground. The Swift
// `Effects.scorch` (:435), `decal` (:717) and `shockRing` (:447), and the
// slow marker (`Effects.makeSlowMarker` :464, `GameScene.markSlow`/`showSlow`
// :870-893), as a `UAcEffects` extension (AcEffectsExtension.h).
//
// Every mark is an instance on the engine plane (UV 0-1) in a pool:
//   marks (scorch and decals, 512): M_AcMark, the `lib.smoke` disc in a
//     colour and an alpha (translucent; Swift's `aOne` smoke on a disc);
//   shock rings (64): M_AcGlowMark as a ring (additive; Swift's torus of
//     pipe 0.07 seen from above);
//   slow markers (64): a ring (pipe 0.04) and a soft glow disc (r 1.25) on
//     M_AcGlowMark, three motes (engine spheres, M_Additive), all faded by
//     the marker's opacity.
// Marks fade as Swift's `tick`s; a pool slot reused before its mark ran out
// takes the slot over (the oldest goes).
#include "AcEffects.h"
#include "AcEffectPool.h"
#include "AcEffectsExtension.h"
#include "AcLog.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

#include "Rules.h"
#include "Types.h"

#include <cmath>

namespace AcMarksPrivate
{
	constexpr double Cm = AcSpace::CmPerCell;

	double SrgbToLinear(const double C)
	{
		return C <= 0.04045 ? C / 12.92 : std::pow((C + 0.055) / 1.055, 2.4);
	}

	FLinearColor Lin(const double R, const double G, const double B, const double Intensity = 1.0)
	{
		return FLinearColor(SrgbToLinear(R) * Intensity, SrgbToLinear(G) * Intensity, SrgbToLinear(B) * Intensity, 1.0);
	}

	/// The engine plane's transform for a disc of radius `R` cm (`Stretch`
	/// times longer along `Yaw`) lying flat at `At`.
	FTransform Flat(const FVector& At, const double R, const double Stretch = 1.0, const double Yaw = 0.0)
	{
		return FTransform(FQuat(FVector::UpVector, Yaw), At, FVector(2.0 * R * Stretch / 100.0, 2.0 * R / 100.0, 1.0));
	}

	void SetRgba(FAcEffectPool& P, const int32 I, const FLinearColor& C, const double A)
	{
		P.SetCustom(I, 0, C.R);
		P.SetCustom(I, 1, C.G);
		P.SetCustom(I, 2, C.B);
		P.SetCustom(I, 3, float(A));
	}

	class FAcMarks final : public FAcEffectsExtension
	{
	public:
		virtual void Begin(UAcEffects& InEffects) override
		{
			Effects = &InEffects;
			UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
			UMaterialInterface* Mark = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_AcMark.M_AcMark"));
			UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_AcGlowMark.M_AcGlowMark"));
			if (!Plane || !Mark || !Glow || !Effects->SphereMesh())
			{
				UE_LOG(LogAutocraft, Error, TEXT("effects marks: missing the plane, M_AcMark or M_AcGlowMark (run Tools/Editor/make_mark_materials.py)"));
				return;
			}
			Marks = &Effects->MakePool(Plane, Mark, 512, TEXT("Marks"));
			Rings = &Effects->MakePool(Plane, Glow, 64, TEXT("ShockRings"));
			SlowRings = &Effects->MakePool(Plane, Glow, SlowSlots, TEXT("SlowRings"));
			SlowGlows = &Effects->MakePool(Plane, Glow, SlowSlots, TEXT("SlowGlows"));
			Motes = &Effects->MakePool(Effects->SphereMesh(), Effects->EmissiveMaterial(FLinearColor(0.75, 0.9, 1), 3, true),
				SlowSlots * 3, TEXT("SlowMotes"));
			MarkLife.SetNum(Marks->Num());
			RingLife.SetNum(Rings->Num());
			SlowFree.Reserve(SlowSlots);
			for (int32 I = SlowSlots - 1; I >= 0; --I) SlowFree.Add(I);
			bReady = true;
		}

		virtual bool HandleScorch(const FAcScorchRequest& R) override
		{
			if (!bReady) return false;
			// A dark mark (white 0.03, `constant`) 0.035 over the ground.
			const int32 I = Marks->Next();
			FMark& M = MarkLife[I];
			M = {R.Time, FMath::Max(R.Life, 0.01), R.Darkness, true};
			Marks->Set(I, Flat(R.At + FVector(0, 0, 0.035 * Cm), R.Radius, R.Stretch, R.Yaw));
			SetRgba(*Marks, I, Lin(0.03, 0.03, 0.03), R.Darkness);
			return true;
		}

		virtual bool HandleDecal(const FAcDecalRequest& R) override
		{
			if (!bReady) return false;
			const int32 I = Marks->Next();
			MarkLife[I] = {R.Time, FMath::Max(R.Life, 0.01), R.Color.A, true};
			Marks->Set(I, Flat(R.At + FVector(0, 0, 0.03 * Cm), R.Radius));
			SetRgba(*Marks, I, Lin(R.Color.R, R.Color.G, R.Color.B), R.Color.A);
			return true;
		}

		virtual bool HandleShockRing(const FAcShockRingRequest& R) override
		{
			if (!bReady) return false;
			const int32 I = Rings->Next();
			RingLife[I] = {R.Time, FMath::Max(R.Life, 0.01), R.Bright, true, R.At + FVector(0, 0, 0.1 * Cm), R.Radius};
			PlaceRing(I, R.Time);
			return true;
		}

		virtual void Consume(const FAcFrame& Frame) override
		{
			if (!bReady) return;
			MarkSlow(Frame.Time);
		}

		virtual void Update(const double Time) override
		{
			if (!bReady) return;
			for (int32 I = 0; I < MarkLife.Num(); ++I)
			{
				FMark& M = MarkLife[I];
				if (!M.bLive) continue;
				const double Age = FMath::Max(0.0, (Time - M.Start) / M.Life);
				if (Age >= 1.0)
				{
					M.bLive = false;
					Marks->Hide(I);
					continue;
				}
				// Scorch: darkness·min(1, (1 − age)·3); a decal: min(1, (1 − age)·3).
				Marks->SetCustom(I, 3, float(M.Alpha * FMath::Min(1.0, (1.0 - Age) * 3.0)));
			}
			for (int32 I = 0; I < RingLife.Num(); ++I)
			{
				if (RingLife[I].bLive) PlaceRing(I, Time);
			}
		}

		virtual void Clear() override
		{
			if (!bReady) return;
			for (int32 I = 0; I < MarkLife.Num(); ++I)
			{
				MarkLife[I].bLive = false;
				Marks->Hide(I);
			}
			for (int32 I = 0; I < RingLife.Num(); ++I)
			{
				RingLife[I].bLive = false;
				Rings->Hide(I);
			}
			for (const auto& [Id, Slot] : SlowOf) HideSlow(Slot);
			SlowOf.Reset();
		}

	private:
		struct FMark
		{
			double Start = 0, Life = 1, Alpha = 1;
			bool bLive = false;
		};
		struct FRing
		{
			double Start = 0, Life = 1, Bright = 1;
			bool bLive = false;
			FVector At = FVector::ZeroVector;
			double Radius = 100;
		};

		void PlaceRing(const int32 I, const double Time)
		{
			FRing& R = RingLife[I];
			const double Age = FMath::Clamp((Time - R.Start) / R.Life, 0.0, 1.0);
			if (Time - R.Start >= R.Life)
			{
				R.bLive = false;
				Rings->Hide(I);
				return;
			}
			// Races out to `radius`, fading as it goes.
			const double Rad = R.Radius * (0.15 + 0.85 * (1.0 - std::pow(1.0 - Age, 2.5)));
			const double Fade = R.Bright * (1.0 - Age) * (1.0 - Age);
			constexpr double Pipe = 0.07;
			Rings->Set(I, Flat(R.At, Rad * (1.0 + Pipe)));
			SetRgba(*Rings, I, Lin(1, 0.72, 0.4, 2.4 * Fade), Pipe);
		}

		/// `GameScene.markSlow` for every unit drawn: a marker while a
		/// grenade slows it (not aboard, not in a Bastion), gone after.
		void MarkSlow(const double Time)
		{
			UAcSimSubsystem* Sim = Effects->Sim();
			if (!Sim || !Sim->IsRunning()) return;
			const ac::GameState& Shown = Sim->Shown();
			const double Now = Shown.time;
			TSet<int64> Seen;
			for (const ac::Unit& U : Shown.units)
			{
				if (!U.slowUntil || *U.slowUntil <= Now || U.task == ac::Unit::Task::aboard || U.task == ac::Unit::Task::inBastion)
				{
					continue;
				}
				int32* Slot = SlowOf.Find(U.id);
				if (!Slot)
				{
					if (SlowFree.IsEmpty()) continue;
					Slot = &SlowOf.Add(U.id, SlowFree.Pop());
				}
				Seen.Add(U.id);
				ShowSlow(*Slot, U, *U.slowUntil - Now, Time);
			}
			for (auto It = SlowOf.CreateIterator(); It; ++It)
			{
				if (Seen.Contains(It->Key)) continue;
				HideSlow(It->Value);
				SlowFree.Add(It->Value);
				It.RemoveCurrent();
			}
		}

		/// `GameScene.showSlow`: fades out over the last 0.3 s, pulses, its
		/// motes circle.
		void ShowSlow(const int32 S, const ac::Unit& U, const double Left, const double Time)
		{
			const double R = (ac::Rules::radius(U.kind) + 0.2) * Cm;
			const FVector At = AcSpace::ToWorld(U.position, Effects->GroundZ(U.position) / Cm + 0.06);
			const double Opacity = FMath::Clamp(Left / 0.3, 0.0, 1.0) * (0.75 + 0.25 * std::sin(Time * 11.0));
			// The ring: (0.4, 0.72, 1) × 2, additive, pipe 0.04.
			constexpr double Pipe = 0.04;
			SlowRings->Set(S, Flat(At, R * (1.0 + Pipe)));
			SetRgba(*SlowRings, S, Lin(0.4, 0.72, 1, 2 * Opacity), Pipe);
			// The soft glow: radius 1.25, emission × 0.8 through the smoke.
			SlowGlows->Set(S, Flat(At, 1.25 * R));
			SetRgba(*SlowGlows, S, Lin(0.4, 0.72, 1, 0.8 * Opacity), 0.0);
			// Three motes (r 0.06, under the marker's (r, 1, r) scale) at
			// (cos a, 0.12, sin a), the orbit turned by time·4 (SceneKit's
			// eulerAngles.y: angle a − 4t in the ground plane).
			for (int32 K = 0; K < 3; ++K)
			{
				const double A = K * 2.0 * UE_DOUBLE_PI / 3.0 - Time * 4.0;
				const FVector P = At + FVector(std::cos(A) * R, std::sin(A) * R, 0.12 * Cm);
				const int32 I = S * 3 + K;
				Motes->Set(I, Motes->Shape(P, FQuat::Identity, FVector(0.12 * R, 0.12 * R, 0.12 * Cm)));
				Motes->SetCustom(I, 1, float(Opacity));
			}
		}

		void HideSlow(const int32 S)
		{
			SlowRings->Hide(S);
			SlowGlows->Hide(S);
			for (int32 K = 0; K < 3; ++K) Motes->Hide(S * 3 + K);
		}

		static constexpr int32 SlowSlots = 64;
		UAcEffects* Effects = nullptr;
		bool bReady = false;
		FAcEffectPool* Marks = nullptr;
		FAcEffectPool* Rings = nullptr;
		FAcEffectPool* SlowRings = nullptr;
		FAcEffectPool* SlowGlows = nullptr;
		FAcEffectPool* Motes = nullptr;
		TArray<FMark> MarkLife;
		TArray<FRing> RingLife;
		TMap<int64, int32> SlowOf;
		TArray<int32> SlowFree;
	};

	FAcEffectsExtensionRegistration Registration(TEXT("marks (C2)"), 110,
		[] { return TUniquePtr<FAcEffectsExtension>(new FAcMarks); });
}
