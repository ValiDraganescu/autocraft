// Chunk C2: grenades and anchor shells in flight (see AcEffectsArcs.h).
//
// Drawn as the Swift `Projectile`s (`Effects.swift:799-898`): a glowing core
// stretched along its path in an additive spark halo (ISM pools: grenades
// 28, shells 12, as Swift), and the trails their particle systems leave,
// streamed through C3's particle set (`AcEffectsBursts::ParticlesOf`,
// kinds `GrenadeTrail`, `ShellFire`, `ShellSmoke` = Effects.swift:825/866/880)
// while the shell flies, from the muzzle to where it lands; the particles
// live on in the world after it landed. No C3: no trails, the rest works.
// What a landing does (flash, blast, ring, scorch...) is C1's `Land`.
#include "AcEffectsArcs.h"

#include "AcEffects.h"
#include "AcEffectPool.h"
#include "AcEffectsBursts.h"
#include "AcLog.h"
#include "AcParticles.h"
#include "AcSpace.h"

#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInterface.h"

#include <cmath>

// --- The flights (no drawing) -------------------------------------------------

FVector FAcArcFlights::Position(const FVector& From, const FVector& To, const double ArcCm, const double U)
{
	return From + (To - From) * U + FVector(0, 0, ArcCm * 4.0 * U * (1.0 - U));
}

FVector FAcArcFlights::Velocity(const FVector& From, const FVector& To, const double ArcCm, const double U)
{
	return (To - From) + FVector(0, 0, ArcCm * 4.0 * (1.0 - 2.0 * U));
}

int32 FAcArcFlights::Launch(FAcLaunch&& L)
{
	const int32 Slot = Cursor;
	Cursor = (Cursor + 1) % Slots;
	// A shell still in the air when its slot comes round lands now.
	if (const int32 I = Flying.IndexOfByPredicate([Slot](const FFlight& F) { return F.Slot == Slot; }); I != INDEX_NONE)
	{
		FFlight Old = MoveTemp(Flying[I]);
		Flying.RemoveAt(I);
		if (Old.Land) Old.Land(L.Time, Old.To);
	}
	FFlight& F = Flying.AddDefaulted_GetRef();
	F.Slot = Slot;
	F.From = L.From;
	F.To = L.To;
	F.Start = L.Time;
	F.Flight = FMath::Max(L.Flight, 0.05);
	F.ArcCm = L.Arc * AcSpace::CmPerCell;
	F.Track = MoveTemp(L.Track);
	F.Land = MoveTemp(L.Land);
	return Slot;
}

void FAcArcFlights::Fly(const double Time, TFunctionRef<void(const FFlight&, const FVector&, const FVector&)> Place,
	TFunctionRef<void(const FFlight&)> Landed)
{
	// As Swift's `fly`: what a landing launches joins after this pass.
	TArray<FFlight> Now = MoveTemp(Flying);
	Flying.Reset();
	TArray<FFlight> Keep;
	Keep.Reserve(Now.Num());
	for (FFlight& F : Now)
	{
		// Homing on the target as it moves (where it was, once it is gone).
		if (F.Track)
		{
			if (const TOptional<FVector> P = F.Track()) F.To = *P;
		}
		const double T = (Time - F.Start) / F.Flight;
		if (T >= 1.0)
		{
			Landed(F);
			if (F.Land) F.Land(F.Start + F.Flight, F.To);
			continue;
		}
		const double U = FMath::Max(0.0, T);
		const FVector V = Velocity(F.From, F.To, F.ArcCm, U);
		Place(F, Position(F.From, F.To, F.ArcCm, U), V.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector));
		Keep.Add(MoveTemp(F));
	}
	Keep.Append(MoveTemp(Flying));
	Flying = MoveTemp(Keep);
}

// --- Drawing ------------------------------------------------------------------

namespace AcArcsPrivate
{
	constexpr double Cm = AcSpace::CmPerCell;

	struct FTrail
	{
		EAcParticle Kind;
		double Rate;  // Swift's birthRate
	};

	/// One kind of shell: its pools, its flights and its trails.
	struct FShellKind
	{
		FAcArcFlights Flights;
		FAcEffectPool* Core = nullptr;
		FAcEffectPool* Halo = nullptr;
		/// Core and halo boxes, cm (x, y across, z along the path).
		FVector CoreSize, HaloSize;
		TArray<FTrail> Trails;
		/// Per slot: the flight's emitter key and when it last emitted.
		TArray<uint64> Key;
		TArray<double> Emitted;
	};

	class FAcArcs final : public FAcEffectsExtension
	{
	public:
		virtual void Begin(UAcEffects& InEffects) override
		{
			Effects = &InEffects;
			UTexture* Spark = LoadObject<UTexture>(nullptr, TEXT("/Game/Models/Textures/T_spark.T_spark"));
			UStaticMesh* Sphere = Effects->SphereMesh();
			if (!Sphere) return;

			// Projectile.grenade: an orange-hot slug (r 0.06, ×2.2 along) in a soft glow (r 0.13).
			Grenades.Flights = FAcArcFlights(28);
			Grenades.Core = &Effects->MakePool(Sphere, Effects->EmissiveMaterial(FLinearColor(1, 0.75, 0.35), 3.4), 28, TEXT("GrenadeCore"));
			Grenades.Halo = &Effects->MakePool(Sphere, Effects->EmissiveMaterial(FLinearColor(1, 0.55, 0.2), 1.3, true, Spark), 28, TEXT("GrenadeHalo"));
			Grenades.CoreSize = FVector(0.12, 0.12, 0.12 * 2.2) * Cm;
			Grenades.HaloSize = FVector(0.26, 0.26, 0.26 * 2.2) * Cm;
			Grenades.Trails = {{EAcParticle::GrenadeTrail, 120}};

			// Projectile.shell: blue-white plasma (r 0.1, ×1.8) in a blue glow (r 0.26).
			Shells.Flights = FAcArcFlights(12);
			Shells.Core = &Effects->MakePool(Sphere, Effects->EmissiveMaterial(FLinearColor(0.82, 0.94, 1), 4.5), 12, TEXT("ShellCore"));
			Shells.Halo = &Effects->MakePool(Sphere, Effects->EmissiveMaterial(FLinearColor(0.3, 0.6, 1), 1.8, true, Spark), 12, TEXT("ShellHalo"));
			Shells.CoreSize = FVector(0.2, 0.2, 0.2 * 1.8) * Cm;
			Shells.HaloSize = FVector(0.52, 0.52, 0.52 * 1.8) * Cm;
			Shells.Trails = {{EAcParticle::ShellFire, 160}, {EAcParticle::ShellSmoke, 60}};

			for (FShellKind* K : {&Grenades, &Shells})
			{
				K->Key.Init(0, K->Flights.SlotCount());
				K->Emitted.Init(0.0, K->Flights.SlotCount());
			}
			bReady = true;
		}

		virtual bool HandleGrenade(FAcLaunch& L) override { return Launch(Grenades, L); }
		virtual bool HandleAnchorShell(FAcLaunch& L) override { return Launch(Shells, L); }

		virtual void Update(const double Time) override
		{
			if (!bReady) return;
			for (FShellKind* K : {&Grenades, &Shells}) Fly(*K, Time);
		}

		virtual void Clear() override
		{
			if (!bReady) return;
			for (FShellKind* K : {&Grenades, &Shells})
			{
				K->Flights.Clear();
				for (int32 I = 0; I < K->Core->Num(); ++I)
				{
					K->Core->Hide(I);
					K->Halo->Hide(I);
				}
			}
		}

	private:
		bool Launch(FShellKind& K, FAcLaunch& L)
		{
			if (!bReady) return false;
			const double Time = L.Time;
			const FVector From = L.From;
			const int32 Slot = K.Flights.Launch(MoveTemp(L));
			// Swift puts the shell at the muzzle at once, its trail on. A
			// new emitter key per flight, so a reused slot's trail does not
			// run back from where the last shell came down.
			const FQuat Q = FAcEffectPool::Along((K.Flights.All().Last().To - From).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector));
			K.Core->Set(Slot, K.Core->Shape(From, Q, K.CoreSize));
			K.Halo->Set(Slot, K.Halo->Shape(From, Q, K.HaloSize));
			K.Key[Slot] = FAcParticles::KeyOf(0x0C2000000 + int64(++Serial), uint32(Slot));
			K.Emitted[Slot] = Time;
			if (FAcParticles* P = AcEffectsBursts::ParticlesOf(Effects))
			{
				// Seen at the muzzle: the first frame's births run from here.
				for (const FTrail& T : K.Trails) P->Stream(T.Kind, K.Key[Slot], FTransform(From), T.Rate, Time, 1e-6);
			}
			return true;
		}

		void Fly(FShellKind& K, const double Time)
		{
			FAcParticles* Particles = AcEffectsBursts::ParticlesOf(Effects);
			K.Flights.Fly(Time,
				[&K, Time, Particles](const FAcArcFlights::FFlight& F, const FVector& P, const FVector& Dir)
				{
					const FQuat Q = FAcEffectPool::Along(Dir);
					K.Core->Set(F.Slot, K.Core->Shape(P, Q, K.CoreSize));
					K.Halo->Set(F.Slot, K.Halo->Shape(P, Q, K.HaloSize));
					Emit(K, F, P, Time, Particles);
				},
				[&K, Particles](const FAcArcFlights::FFlight& F)
				{
					// The trail runs to the landing, then stops.
					Emit(K, F, F.To, F.Start + F.Flight, Particles);
					K.Core->Hide(F.Slot);
					K.Halo->Hide(F.Slot);
				});
		}

		static void Emit(FShellKind& K, const FAcArcFlights::FFlight& F, const FVector& At, const double Until,
			FAcParticles* Particles)
		{
			double& Was = K.Emitted[F.Slot];
			const double Dt = Until - Was;
			if (Particles && Dt > 0)
			{
				for (const FTrail& T : K.Trails) Particles->Stream(T.Kind, K.Key[F.Slot], FTransform(At), T.Rate, Until, Dt);
			}
			Was = FMath::Max(Was, Until);
		}

		UAcEffects* Effects = nullptr;
		bool bReady = false;
		uint32 Serial = 0;
		FShellKind Grenades, Shells;
	};

	FAcEffectsExtensionRegistration Registration(TEXT("arcs (C2)"), 100,
		[] { return TUniquePtr<FAcEffectsExtension>(new FAcArcs); });
}
