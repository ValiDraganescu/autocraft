// The effects of the Peregrine, the Atlas and the Scorpion
// (docs/new-units.md "Looks"):
//
//   * Peregrine: seeker missiles (a small hot core in a pale-blue glow, a smoke
//     trail, flying exactly `Rules::flight` seconds, homing as they go), and
//     the vapour trails off its wing tips in hard turns and at top speed.
//   * Atlas: the twin siege cannons' shells (hit at once, drawn crossing the
//     short way) and their splash burst; the footfall's dust, footprint and
//     shake (`UAcEffects::Footfall`, AcEffectsShots.cpp).
//   * Scorpion: the sting (a fast bolt, 0.2 s) and its burst, and the red
//     laser line from the launcher to its victim through the lock-on second.
//
// The shots are `UAcEffects::Round` (asked for by `Fire`, AcEffectsShots.cpp),
// flown here as an extension like C2's shells (AcEffectsArcs.cpp); what a
// landing does is `UAcEffects::RoundLanded`, below.
#include "AcEffects.h"

#include "AcEffectPool.h"
#include "AcEffectsArcs.h"
#include "AcEffectsBursts.h"
#include "AcEffectsExtension.h"
#include "AcModelCatalog.h"
#include "AcParticles.h"
#include "AcPose.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"
#include "AcWorldRenderer.h"

#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInterface.h"

#include "Rules.h"
#include "Simulation.h"
#include "Types.h"

#include <cmath>

namespace
{
	constexpr double Cm = AcSpace::CmPerCell;

	double Smoothstep(const double A, const double B, const double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	/// How a round looks: its core and halo (boxes, cm: x, y across, z along)
	/// and the trail it leaves.
	struct FRoundLook
	{
		FLinearColor Core, Halo;
		double CoreGlow, HaloGlow;
		FVector CoreSize, HaloSize;
		int32 Slots;
		bool bTrail;
		double TrailRate;
	};

	FRoundLook LookOf(const EAcRound Kind)
	{
		switch (Kind)
		{
		case EAcRound::Missile:
			// A cold-blue seeker, r 0.04 and 4 long, in a pale glow, trailing smoke.
			return {FLinearColor(0.82f, 0.94f, 1.f), FLinearColor(0.35f, 0.65f, 1.f), 4.5, 1.6,
				FVector(0.08, 0.08, 0.3) * Cm, FVector(0.2, 0.2, 0.34) * Cm, 24, true, 110};
		case EAcRound::AtlasShell:
			// A heavy hot slug: white-orange, bigger than a grenade.
			return {FLinearColor(1.f, 0.82f, 0.5f), FLinearColor(1.f, 0.5f, 0.15f), 4.0, 1.5,
				FVector(0.2, 0.2, 0.46) * Cm, FVector(0.5, 0.5, 0.7) * Cm, 12, true, 90};
		case EAcRound::Sting:
			// A thin red-hot bolt.
			return {FLinearColor(1.f, 0.55f, 0.45f), FLinearColor(1.f, 0.12f, 0.08f), 5.0, 2.0,
				FVector(0.07, 0.07, 0.9) * Cm, FVector(0.22, 0.22, 1.1) * Cm, 8, false, 0};
		}
		return {};
	}
}

// --- What a landing does -------------------------------------------------------

void UAcEffects::Round(const EAcRound Kind, const FVector& From, const FVector& To, const double Ground, const double Flight,
	const double Time, TFunction<TOptional<FVector>()> Track)
{
	FAcLaunch L;
	L.From = From;
	L.To = To;
	L.GroundZ = Ground;
	L.Flight = Flight;
	L.Time = Time;
	// A seeker curves a little off the rail; the shells and the bolt run flat.
	L.Arc = Kind == EAcRound::Missile ? 0.12 + 0.012 * FVector::Dist(From, To) / Cm : 0.02;
	L.Track = MoveTemp(Track);
	TWeakObjectPtr<UAcEffects> Weak(this);
	L.Land = [Weak, Kind, Ground](const double T, const FVector& B)
	{
		if (UAcEffects* E = Weak.Get()) E->RoundLanded(Kind, T, B, Ground);
	};
	++Count.Shells;
	for (TUniquePtr<FAcEffectsExtension>& X : Extensions)
	{
		if (X->HandleRound(Kind, L)) return;
	}
	// Stand-in without the extension: it comes down where the target is when
	// the damage lands.
	++Count.Unhandled;
	After(L.Flight, L.Time, [L = MoveTemp(L)]()
	{
		const TOptional<FVector> Now = L.Track ? L.Track() : TOptional<FVector>();
		if (L.Land) L.Land(L.Time + L.Flight, Now.Get(L.To));
	});
}

void UAcEffects::RoundLanded(const EAcRound Kind, const double T, const FVector& B, const double Ground)
{
	const bool bGround = B.Z - Ground < 1.2 * Cm;
	switch (Kind)
	{
	case EAcRound::Missile:
	{
		// A small burst in the air, as a Kestrel's rocket.
		Flash(B, 2.0, T, 0.08);
		Explosion(B, 0.3, T);
		break;
	}
	case EAcRound::AtlasShell:
	{
		// The shell's burst and its splash: a ring out to the second radius
		// of `Rules::atlasSplash`, dust thrown up, a mark scorched.
		const double Splash = ac::Rules::atlasSplash.back().radius;
		Flash(B, 3.6, T, 0.1);
		Explosion(B, 0.55, T);
		if (bGround)
		{
			FAcShockRingRequest Ring;
			Ring.At = FVector(B.X, B.Y, Ground);
			Ring.Radius = Splash * Cm;
			Ring.Life = 0.35;
			Ring.Time = T;
			Ring.Bright = 0.8;
			ShockRing(Ring);
			DustPuff(FVector(B.X, B.Y, Ground + 0.1 * Cm), 0.8, T);
			FAcScorchRequest S;
			S.At = FVector(B.X, B.Y, Ground);
			S.Radius = 0.6 * Cm;
			S.Life = 12;
			S.Time = T;
			Scorch(S);
		}
		break;
	}
	case EAcRound::Sting:
	{
		// The sting's burst, out to the second radius of `Rules::scorpionSplash`.
		const double Splash = ac::Rules::scorpionSplash.back().radius;
		Flash(B, 4.0, T, 0.12);
		Explosion(B, 0.75, T);
		if (bGround)
		{
			FAcShockRingRequest Ring;
			Ring.At = FVector(B.X, B.Y, Ground);
			Ring.Radius = Splash * Cm;
			Ring.Life = 0.3;
			Ring.Time = T;
			Ring.Bright = 0.9;
			ShockRing(Ring);
			DustPuff(FVector(B.X, B.Y, Ground + 0.1 * Cm), 0.9, T);
		}
		break;
	}
	}
}

// --- Flying them ---------------------------------------------------------------

namespace AcNewKindsFxPrivate
{
	/// One kind of round: its pools, its flights and its trail.
	struct FRounds
	{
		FRoundLook Look;
		FAcArcFlights Flights;
		FAcEffectPool* Core = nullptr;
		FAcEffectPool* Halo = nullptr;
		TArray<uint64> Key;
		TArray<double> Emitted;
	};

	class FAcNewKindsFx final : public FAcEffectsExtension
	{
	public:
		virtual void Begin(UAcEffects& InEffects) override
		{
			Effects = &InEffects;
			UTexture* Spark = LoadObject<UTexture>(nullptr, TEXT("/Game/Models/Textures/T_spark.T_spark"));
			UStaticMesh* Sphere = Effects->SphereMesh();
			UStaticMesh* Cylinder = Effects->CylinderMesh();
			if (!Sphere || !Cylinder) return;
			for (int32 K = 0; K < 3; ++K)
			{
				FRounds& R = Rounds[K];
				R.Look = LookOf(EAcRound(K));
				R.Flights = FAcArcFlights(R.Look.Slots);
				const FName Name(*FString::Printf(TEXT("NewKindRound%d"), K));
				R.Core = &Effects->MakePool(Sphere, Effects->EmissiveMaterial(R.Look.Core, R.Look.CoreGlow), R.Look.Slots,
					FName(*(Name.ToString() + TEXT("Core"))));
				R.Halo = &Effects->MakePool(Sphere, Effects->EmissiveMaterial(R.Look.Halo, R.Look.HaloGlow, true, Spark),
					R.Look.Slots, FName(*(Name.ToString() + TEXT("Halo"))));
				R.Key.Init(0, R.Look.Slots);
				R.Emitted.Init(0.0, R.Look.Slots);
			}
			// The Scorpion's laser: a thin red beam, one for each lock.
			Laser = &Effects->MakePool(Cylinder, Effects->EmissiveMaterial(FLinearColor(1.f, 0.1f, 0.06f), 3.2, true, Spark),
				LaserSlots, TEXT("ScorpionLaser"));
			bReady = true;
		}

		virtual bool HandleRound(const EAcRound Kind, FAcLaunch& L) override
		{
			if (!bReady) return false;
			FRounds& R = Rounds[int32(Kind)];
			const double Time = L.Time;
			const FVector From = L.From;
			const int32 Slot = R.Flights.Launch(MoveTemp(L));
			const FQuat Q = FAcEffectPool::Along((R.Flights.All().Last().To - From).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector));
			R.Core->Set(Slot, R.Core->Shape(From, Q, R.Look.CoreSize));
			R.Halo->Set(Slot, R.Halo->Shape(From, Q, R.Look.HaloSize));
			R.Key[Slot] = FAcParticles::KeyOf(0x0C7000000 + int64(++Serial), uint32(Slot));
			R.Emitted[Slot] = Time;
			if (FAcParticles* P = AcEffectsBursts::ParticlesOf(Effects); P && R.Look.bTrail)
			{
				P->Stream(EAcParticle::GrenadeTrail, R.Key[Slot], FTransform(From), R.Look.TrailRate, Time, 1e-6);
			}
			return true;
		}

		virtual void Consume(const FAcFrame& Frame) override
		{
			if (!bReady || !Frame.State) return;
			const double Dt = LastTime ? FMath::Clamp(Frame.Time - *LastTime, 0.0, 0.1) : 0.0;
			LastTime = Frame.Time;
			UAcWorldRenderer* Renderer = Effects->Renderer();
			if (!Renderer) return;
			FAcParticles* Particles = AcEffectsBursts::ParticlesOf(Effects);
			int32 Lasers = 0;
			TSet<int64> Seen;
			for (const ac::Unit& U : Frame.State->units)
			{
				if (U.kind == ac::UnitKind::peregrine) Vapour(*Renderer, Particles, U, Frame.Time, Dt, Seen);
				else if (U.kind == ac::UnitKind::scorpion) LockBeam(*Renderer, Frame, U, Lasers);
			}
			for (int32 I = Lasers; I < LaserSlots; ++I) Laser->Hide(I);
			// Forget the Peregrines that are gone.
			for (auto It = LastPosition.CreateIterator(); It; ++It)
			{
				if (!Seen.Contains(It.Key())) It.RemoveCurrent();
			}
		}

		virtual void Update(const double Time) override
		{
			if (!bReady) return;
			FAcParticles* Particles = AcEffectsBursts::ParticlesOf(Effects);
			for (FRounds& R : Rounds)
			{
				R.Flights.Fly(Time,
					[&R, Time, Particles](const FAcArcFlights::FFlight& F, const FVector& P, const FVector& Dir)
					{
						const FQuat Q = FAcEffectPool::Along(Dir);
						R.Core->Set(F.Slot, R.Core->Shape(P, Q, R.Look.CoreSize));
						R.Halo->Set(F.Slot, R.Halo->Shape(P, Q, R.Look.HaloSize));
						Emit(R, F, P, Time, Particles);
					},
					[&R, Particles](const FAcArcFlights::FFlight& F)
					{
						Emit(R, F, F.To, F.Start + F.Flight, Particles);
						R.Core->Hide(F.Slot);
						R.Halo->Hide(F.Slot);
					});
			}
		}

		virtual void Clear() override
		{
			if (!bReady) return;
			for (FRounds& R : Rounds)
			{
				R.Flights.Clear();
				for (int32 I = 0; I < R.Core->Num(); ++I)
				{
					R.Core->Hide(I);
					R.Halo->Hide(I);
				}
			}
			for (int32 I = 0; I < LaserSlots; ++I) Laser->Hide(I);
			LastPosition.Reset();
			LastTime.Reset();
		}

	private:
		static void Emit(FRounds& R, const FAcArcFlights::FFlight& F, const FVector& At, const double Until, FAcParticles* Particles)
		{
			double& Was = R.Emitted[F.Slot];
			const double Dt = Until - Was;
			if (Particles && R.Look.bTrail && Dt > 0)
			{
				Particles->Stream(EAcParticle::GrenadeTrail, R.Key[F.Slot], FTransform(At), R.Look.TrailRate, Until, Dt);
			}
			Was = FMath::Max(Was, Until);
		}

		/// The vapour off a Peregrine's wing tips (its red beacons): in a hard
		/// turn, and at top speed.
		void Vapour(UAcWorldRenderer& Renderer, FAcParticles* Particles, const ac::Unit& U, const double Time, const double Dt,
			TSet<int64>& Seen)
		{
			Seen.Add(U.id);
			double Speed = 0.0;
			if (const ac::Vec2* Last = LastPosition.Find(U.id); Last && Dt > 1e-4) Speed = ac::distance(*Last, U.position) / Dt;
			LastPosition.Add(U.id, U.position);
			if (!Particles || Dt <= 0.0) return;
			const FAcUnitMemory* Mem = Renderer.Memory(U.id);
			const FAcModelInfo* Model = Renderer.ModelOf(U.id);
			if (!Mem || !Model) return;
			const double Turn = std::abs(Mem->TurnRate);
			const double Top = FMath::Max(U.stats().speed, 0.1);
			const double Rate = 70.0 * Smoothstep(1.2, 2.2, Turn) + 45.0 * Smoothstep(0.88, 0.97, Speed / Top);
			if (Rate <= 0.0) return;
			const TConstArrayView<FTransform> W = Renderer.PartWorld(U.id);
			for (int32 K = 0; K < 2; ++K)
			{
				const int32* I = Model->PartIndex.Find(FName(*FString::Printf(TEXT("beacon_%d"), K)));
				if (!I || !W.IsValidIndex(*I)) continue;
				FAcEmit E;
				E.Size = 0.1;
				Particles->Stream(EAcParticle::ShellSmoke, FAcParticles::KeyOf(U.id, 0x40u + uint32(K)), FTransform(W[*I].GetLocation()),
					Rate, Time, Dt, E);
			}
		}

		/// The Scorpion's red line from its launcher to its victim, from the
		/// moment the lock starts, thin at first and bright as it holds. The
		/// victim sees it coming (the lock's second is its chance); so does
		/// everyone who sees the Scorpion.
		void LockBeam(UAcWorldRenderer& Renderer, const FAcFrame& Frame, const ac::Unit& U, int32& Count)
		{
			if (!(U.lockTarget && U.lockFrom && U.lockAt) || Frame.Time - *U.lockAt >= 0.25 || Count >= LaserSlots) return;
			const std::optional<ac::Unit> V = Frame.State->unit(*U.lockTarget);
			if (!V) return;
			const bool bDrawn = Renderer.ModelOf(U.id) != nullptr;
			if (!bDrawn && V->owner != Frame.LocalPlayer) return;
			FVector From = AcSpace::ToWorld(U.position, Effects->GroundZ(U.position) / Cm + 1.2);
			if (bDrawn) Effects->PartLocation(U.id, TEXT("launcher"), From);
			const double Chest = V->stats().air ? AcPose::Hover(V->kind) : 0.62;
			FVector To = AcSpace::ToWorld(V->position, Effects->GroundZ(V->position) / Cm + Chest);
			if (bDrawn)
			{
				// Where the launcher's meshes end.
				FVector Tip;
				if (Effects->PartMuzzle(U.id, TEXT("launcher"), Tip)) From = Tip;
			}
			const FVector D = To - From;
			const double Len = D.Size();
			if (Len < 0.1 * Cm) return;
			const double Held = FMath::Clamp((Frame.Time - *U.lockFrom) / 1.0, 0.0, 1.0);
			const double Flicker = 0.85 + 0.15 * std::sin(Frame.Time * 61.0 + double(U.id));
			const double Width = (0.012 + 0.03 * Held) * Flicker * Cm;
			Laser->Set(Count, Laser->Shape(From + D * 0.5, FAcEffectPool::Along(D / Len), FVector(Width, Width, Len)));
			++Count;
		}

		static constexpr int32 LaserSlots = 12;
		UAcEffects* Effects = nullptr;
		bool bReady = false;
		uint32 Serial = 0;
		FRounds Rounds[3];
		FAcEffectPool* Laser = nullptr;
		TOptional<double> LastTime;
		TMap<int64, ac::Vec2> LastPosition;
	};

	FAcEffectsExtensionRegistration Registration(TEXT("new kinds"), 105,
		[] { return TUniquePtr<FAcEffectsExtension>(new FAcNewKindsFx); });
}
