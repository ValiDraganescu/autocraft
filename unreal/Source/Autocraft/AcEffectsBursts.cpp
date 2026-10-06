// The C3 effects extension (GAME-LAYER.md §2.7, chunk C3): explosions and
// particle bursts for `UAcEffects` (AcEffectsExtension.h), the particles the
// poses ask for every frame (Comet jets, Juggernaut muzzle smoke, the
// Firefly's fire, embers, smoke and exhaust, Dropship and Kestrel plumes, a
// Prospector's drill sparks), and a hurt building's smoke and fire
// (`GameScene.animate`: smoke below two thirds of its hit points, fire below
// a third). All drawn by `FAcParticles` (AcParticles.h).
//
// Order 300: an extension that only watches explosions (C6's flash light)
// sits before it and returns false.
//
// Dev: `-AcParticlesDemo="X,Y"` fires a row of every burst at sim point
// (X, Y) one second into the game (explosions of size 1 and 3, a blast,
// a flame line, a dust puff, opal chips, mini gun ricochets), then keeps a
// burning building's smoke and fire and an engine plume on a circle going;
// `-AcParticlesAge=A` pauses the game A seconds after they went off (and
// holds `-AcShot` until then), for stills at a given age;
// `-AcParticlesEvery=P` repeats the row every P seconds instead.
// `-AcParticlesHighlightSat=S` overrides the grading's highlight saturation
// (a look at how it whitens a fire's core).
// `ac.ParticleStats N` logs births, uploads and live particles every N s.
#include "AcEffectsBursts.h"

#include "AcEffects.h"
#include "AcEffectsExtension.h"
#include "AcLog.h"
#include "AcModelCatalog.h"
#include "AcParticles.h"
#include "AcPose.h"
#include "AcPoseVehicles.h"
#include "AcShot.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"
#include "AcWorldRenderer.h"

#include "Components/PostProcessComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include "Rules.h"

#include <cmath>

namespace
{
	constexpr double Cm = AcSpace::CmPerCell;

	TAutoConsoleVariable<float> CVarParticleStats(TEXT("ac.ParticleStats"), 0.f,
		TEXT("Log a `particles:` line (born, uploaded, alive) every N seconds; 0 off."));

	const FName CueDrill(TEXT("AcDrill")), CueCrystalBite(TEXT("AcCrystalBite"));
	const FName PartEmitter1(TEXT("emitter_1")), PartEmitter2(TEXT("emitter_2"));
	const FName PartMuzzle0(TEXT("muzzles_0")), PartMuzzle1(TEXT("muzzles_1")), PartFlameEnd(TEXT("flameEnd"));
	const FName KindComet(TEXT("comet")), KindJuggernaut(TEXT("juggernaut")), KindFirefly(TEXT("firefly"));
	const FName KindDropship(TEXT("dropship")), KindKestrel(TEXT("kestrel"));

	class FAcBursts;
	TMap<const UAcEffects*, FAcBursts*>& Live()
	{
		static TMap<const UAcEffects*, FAcBursts*> M;
		return M;
	}

	class FAcBursts final : public FAcEffectsExtension
	{
	public:
		virtual ~FAcBursts() override
		{
			for (auto It = Live().CreateIterator(); It; ++It)
				if (It.Value() == this) It.RemoveCurrent();
		}

		FAcParticles* Set() { return Particles.IsReady() ? &Particles : nullptr; }

		virtual void Begin(UAcEffects& InEffects) override
		{
			Effects = &InEffects;
			Live().Add(&InEffects, this);
			Particles.Init(InEffects.GetOwner());
			// Dev: how the highlights' saturation changes a fire's white core.
			if (float Sat = 1; FParse::Value(FCommandLine::Get(), TEXT("AcParticlesHighlightSat="), Sat))
			{
				UPostProcessComponent* P = NewObject<UPostProcessComponent>(InEffects.GetOwner(), TEXT("AcParticlesGrading"));
				P->bUnbound = true;
				P->Priority = 10;
				P->Settings.bOverride_ColorSaturationHighlights = true;
				P->Settings.ColorSaturationHighlights = FVector4(Sat, Sat, Sat, 1);
				P->RegisterComponent();
				UE_LOG(LogAutocraft, Log, TEXT("particles: highlight saturation %.2f"), Sat);
			}
			FString Demo;
			if (FParse::Value(FCommandLine::Get(), TEXT("AcParticlesDemo="), Demo))
			{
				FString X, Y;
				if (Demo.Replace(TEXT("\""), TEXT("")).Split(TEXT(","), &X, &Y))
				{
					DemoAt = ac::Vec2(FCString::Atod(*X), FCString::Atod(*Y));
					FParse::Value(FCommandLine::Get(), TEXT("AcParticlesEvery="), DemoEvery);
					double Age = -1;
					if (FParse::Value(FCommandLine::Get(), TEXT("AcParticlesAge="), Age) && Age >= 0)
					{
						DemoAge = Age;
						if (UAcShotSubsystem* Shot = UAcShotSubsystem::Get(InEffects.GetWorld()))
						{
							Shot->Hold();
							bHolding = true;
						}
					}
				}
			}
		}

		virtual void End() override
		{
			if (bHolding)
				if (UAcShotSubsystem* Shot = Effects ? UAcShotSubsystem::Get(Effects->GetWorld()) : nullptr) Shot->Release();
			bHolding = false;
			const FAcParticles::FStats& S = Particles.Totals();
			UE_LOG(LogAutocraft, Log, TEXT("particles: totals born %lld"), S.Born);
		}

		virtual void Clear() override
		{
			Particles.Clear();
			LastTime.Reset();
			NextDemo = 1.0;
		}

		virtual bool HandleExplosion(const FVector& At, const double Size, const double Time) override
		{
			if (!Particles.IsReady()) return false;
			// Effects.explosion (Effects.swift:630): every value of the fire
			// and smoke scales with size; the sparks' speed with √size. Its
			// systems do not loop, and a non-looping SCNParticleSystem emits
			// `birthRate` particles in all over its `emissionDuration`
			// (counted on SceneKit renders), not birthRate a second.
			const FTransform F(At);
			auto Over = [](const double Count, const double Duration) { return Count / FMath::Max(Duration, 1e-3); };
			FAcEmit Fire;
			Fire.Duration = 0.12 * Size;
			Fire.Rate = Over(260 * Size, Fire.Duration);
			Fire.Speed = 2.2 * Size;
			Fire.SpeedVariation = 1.4 * Size;
			Fire.Size = 0.28 * Size;
			Fire.SizeVariation = 0.12 * Size;
			Fire.ShapeScale = Size;
			Particles.Burst(EAcParticle::ExplosionFire, F, Time, Fire);
			FAcEmit Smoke;
			Smoke.Duration = 0.5 * Size;
			Smoke.Rate = Over(40 * Size, Smoke.Duration);
			Smoke.Speed = 0.9 * Size;
			Smoke.Size = 0.35 * Size;
			Smoke.ShapeScale = Size;
			Particles.Burst(EAcParticle::ExplosionSmoke, F, Time, Smoke);
			FAcEmit Sparks;
			Sparks.Duration = 0.08;
			Sparks.Rate = Over(300 * Size, Sparks.Duration);
			Sparks.Speed = 4.5 * FMath::Sqrt(FMath::Max(Size, 0.0));
			Particles.Burst(EAcParticle::ExplosionSparks, F, Time, Sparks);
			return true;
		}

		virtual bool HandleBurst(const FAcBurstRequest& R) override
		{
			if (!Particles.IsReady()) return false;
			FTransform F(R.At);
			if (R.Along) F.SetRotation(FQuat::FindBetweenNormals(FVector::UpVector, R.Along->GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector)));
			auto Fire = [&](EAcParticle K) { Particles.Burst(K, F, R.Time, FAcEmit::Scaled(K, R.Size, R.Duration)); };
			switch (R.Kind)
			{
			case EAcBurst::Blast: Fire(EAcParticle::BlastFire); Fire(EAcParticle::BlastSmoke); break;
			case EAcBurst::Flames: Fire(EAcParticle::GroundFire); Fire(EAcParticle::Embers); break;
			case EAcBurst::Dust: Fire(EAcParticle::Dust); break;
			case EAcBurst::Chips: Fire(EAcParticle::Chips); break;
			case EAcBurst::Ricochet: Fire(EAcParticle::Ricochet); break;
			default: return false;
			}
			return true;
		}

		virtual bool HandleCue(const FAcPoseCue& Cue) override
		{
			if (!Particles.IsReady() || !Effects) return false;
			if (Cue.What == CueDrill)
			{
				// GameScene: `prospector.sparks.birthRate = mining ? 90 : 0`,
				// the emitter at the drill bit (the cue's point), +Y up.
				Particles.Stream(EAcParticle::WeldSparks, 0, FTransform(Cue.At), 90, Now, FrameDt);
				return true;
			}
			if (Cue.What == CueCrystalBite)
			{
				Effects->CrystalBite(Cue.At, Now);
				return true;
			}
			return false;
		}

		virtual void Consume(const FAcFrame& Frame) override
		{
			Now = Frame.Time;
			FrameDt = LastTime ? FMath::Max(0.0, Frame.Time - *LastTime) : 0.0;
			LastTime = Frame.Time;
			Alpha = Frame.Alpha;
			if (!Particles.IsReady() || !Effects || !Frame.State) return;
			if (DemoAt) Demo(Frame.Time);
			if (FrameDt <= 0) return;
			Emitters(*Frame.State, Frame.Time, FrameDt);
		}

		virtual void Update(const double Time) override
		{
			UWorld* World = Effects ? Effects->GetWorld() : nullptr;
			double Display = Time + FMath::Clamp(Alpha, 0.0, 1.0) * UAcSimSubsystem::StepSeconds;
			if (DemoPausedAt) Display = *DemoPausedAt;
			Particles.Update(World, Display);
			if (const float Every = CVarParticleStats.GetValueOnGameThread(); Every > 0)
			{
				const double Wall = FPlatformTime::Seconds();
				if (Wall - StatsWall >= Every)
				{
					StatsWall = Wall;
					const FAcParticles::FStats& S = Particles.Totals();
					UE_LOG(LogAutocraft, Log, TEXT("particles: born %lld, uploaded %d this frame, alive %d, emitters %d"), S.Born,
						S.Uploaded, Particles.Alive(Time), S.Emitters);
				}
			}
		}

	private:
		/// The running emitters of this frame: the units' jets and plumes
		/// (at their exported `emitter_k`/`muzzles_k`/`flameEnd` parts) and
		/// the hurt buildings.
		void Emitters(const ac::GameState& State, const double Time, const double Dt)
		{
			UAcWorldRenderer* R = Effects->Renderer();
			if (!R) return;
			Units.Reset();
			for (const ac::Unit& U : State.units) Units.Add(U.id, &U);
			Structures.Reset();
			for (const ac::Structure& S : State.structures) Structures.Add(S.id, &S);

			R->ForEachObject([&](const int64 Id, const bool bUnit, const FAcModelInfo& Model, const FAcPose& Pose,
								 TConstArrayView<FTransform> W)
			{
				if (Pose.bHidden || Pose.bFrozen) return;
				auto Part = [&](const FName Name) -> const FTransform*
				{
					const int32* I = Model.PartIndex.Find(Name);
					return I && W.IsValidIndex(*I) ? &W[*I] : nullptr;
				};
				if (!bUnit)
				{
					const ac::Structure* const* S = Structures.Find(Id);
					if (S) Damage(Id, **S, Pose, Time, Dt);
					return;
				}
				const FName Kind = Model.Base;
				if (Kind == KindDropship || Kind == KindKestrel)
				{
					// The Swift poses: thrusters' birth rate and speed by `moving`.
					const ac::Unit* const* U = Units.Find(Id);
					const bool bMoving = U && (*U)->walking();
					const bool bDrop = Kind == KindDropship;
					FAcEmit E;
					E.Speed = bDrop ? (bMoving ? 2.4 : 1.3) : (bMoving ? 2.2 : 1.1);
					const double Rate = bDrop ? (bMoving ? 80 : 32) : (bMoving ? 70 : 26);
					if (const FTransform* P = Part(PartEmitter1)) Particles.Stream(EAcParticle::Plume, FAcParticles::KeyOf(Id, 1), *P, Rate, Time, Dt, E);
					if (const FTransform* P = Part(PartEmitter2)) Particles.Stream(EAcParticle::Plume, FAcParticles::KeyOf(Id, 2), *P, Rate, Time, Dt, E);
					return;
				}
				const bool bComet = Kind == KindComet, bJuggernaut = Kind == KindJuggernaut, bFirefly = Kind == KindFirefly;
				if (!bComet && !bJuggernaut && !bFirefly) return;
				const ac::Unit* const* U = Units.Find(Id);
				FAcUnitMemory* Memory = R->Memory(Id);
				if (!U || !Memory) return;
				const FAcVehicleFx* Fx = AcPoseVehicles::Fx((*U)->kind, *Memory);
				if (!Fx) return;
				if (bComet)
				{
					FAcEmit E;
					E.Speed = Fx->JetVelocity;
					if (const FTransform* P = Part(PartEmitter1)) Particles.Stream(EAcParticle::CometJet, FAcParticles::KeyOf(Id, 1), *P, Fx->JetRate[0], Time, Dt, E);
					if (const FTransform* P = Part(PartEmitter2)) Particles.Stream(EAcParticle::CometJet, FAcParticles::KeyOf(Id, 2), *P, Fx->JetRate[1], Time, Dt, E);
				}
				else if (bJuggernaut)
				{
					if (Fx->MuzzleSmokeRate <= 0) return;
					if (const FTransform* P = Part(PartMuzzle0)) Particles.Stream(EAcParticle::MuzzleSmoke, FAcParticles::KeyOf(Id, 1), *P, Fx->MuzzleSmokeRate, Time, Dt);
					if (const FTransform* P = Part(PartMuzzle1)) Particles.Stream(EAcParticle::MuzzleSmoke, FAcParticles::KeyOf(Id, 2), *P, Fx->MuzzleSmokeRate, Time, Dt);
				}
				else
				{
					// emitter_2 is the jet's (under `flame`), emitter_1 the stacks'.
					if (const FTransform* P = Part(PartEmitter2))
					{
						if (Fx->FireRate > 0)
						{
							FAcEmit E;
							E.Speed = Fx->FireVelocity;
							Particles.Stream(EAcParticle::FireflyFire, FAcParticles::KeyOf(Id, 1), *P, Fx->FireRate, Time, Dt, E);
						}
						if (Fx->EmberRate > 0)
						{
							FAcEmit E;
							E.Speed = Fx->EmberVelocity;
							Particles.Stream(EAcParticle::FireflyEmbers, FAcParticles::KeyOf(Id, 2), *P, Fx->EmberRate, Time, Dt, E);
						}
					}
					if (Fx->SmokeRate > 0)
						if (const FTransform* P = Part(PartFlameEnd))
							Particles.Stream(EAcParticle::FireflySmoke, FAcParticles::KeyOf(Id, 3), FTransform(P->GetRotation(), P->GetLocation()),
								Fx->SmokeRate, Time, Dt);
					if (const FTransform* P = Part(PartEmitter1))
						Particles.Stream(EAcParticle::FireflyExhaust, FAcParticles::KeyOf(Id, 4), *P, Fx->ExhaustRate, Time, Dt);
				}
			});
		}

		/// `GameScene.animate`: a finished building's damage smoke and fire,
		/// from a box of its radius at 0.75 of its height.
		void Damage(const int64 Id, const ac::Structure& S, const FAcPose& Pose, const double Time, const double Dt)
		{
			if (!S.complete()) return;
			const double Hurt = 1 - S.hp / ac::Rules::hp(S.kind);
			if (Hurt <= 0.33) return;
			const double Radius = ac::Rules::radius(S.kind);
			const double Height = AcPose::BuildingHeight(S.kind);
			const FTransform& Place = Pose.Placement;
			const FTransform Frame(Place.GetRotation(), Place.GetLocation() + Place.GetRotation().RotateVector(FVector(0, 0, Height * 0.75 * Cm)));
			FAcEmit Smoke;
			Smoke.ShapeScale = Radius;
			Particles.Stream(EAcParticle::DamageSmoke, FAcParticles::KeyOf(Id, 1), Frame, 3 + 10 * Hurt, Time, Dt, Smoke);
			if (Hurt > 0.66)
			{
				FAcEmit Fire;
				Fire.ShapeScale = Radius;
				Particles.Stream(EAcParticle::DamageFire, FAcParticles::KeyOf(Id, 2), Frame, 20 + 40 * Hurt, Time, Dt, Fire);
			}
		}

		/// `-AcParticlesDemo`: a row of every burst.
		void Demo(const double Time)
		{
			if (DemoPausedAt) return;
			if (DemoFired && DemoAge >= 0 && Time >= *DemoFired + DemoAge)
			{
				// Freeze here: the sim pauses, the particles' clock stops at
				// exactly the age asked for, and the shot may be taken.
				DemoPausedAt = *DemoFired + DemoAge;
				if (UAcSimSubsystem* Sim = Effects->Sim()) Sim->SetPaused(true);
				if (bHolding)
					if (UAcShotSubsystem* Shot = UAcShotSubsystem::Get(Effects->GetWorld())) Shot->Release();
				bHolding = false;
				UE_LOG(LogAutocraft, Log, TEXT("particles: demo paused at age %.3f (game time %.3f)"), DemoAge, *DemoPausedAt);
				return;
			}
			if (DemoFired && FrameDt > 0)
			{
				// Running emitters too: a burning building (hurt 0.9, radius
				// 1.5, 3 tall) and an engine plume flying a circle.
				const ac::Vec2 B(DemoAt->x - 3, DemoAt->y + 4.5);
				const FTransform Roof(AcSpace::ToWorld(B, Effects->GroundZ(B) / Cm + 3 * 0.75));
				FAcEmit Box;
				Box.ShapeScale = 1.5;
				Particles.Stream(EAcParticle::DamageSmoke, FAcParticles::KeyOf(-7, 1), Roof, 3 + 10 * 0.9, Time, FrameDt, Box);
				Particles.Stream(EAcParticle::DamageFire, FAcParticles::KeyOf(-7, 2), Roof, 20 + 40 * 0.9, Time, FrameDt, Box);
				const double A = Time * 2.0;
				const ac::Vec2 E(DemoAt->x + 6 + 2 * std::cos(A), DemoAt->y + 4.5 + 2 * std::sin(A));
				// Facing along the circle; the plume blows out of its back (-X).
				const FQuat Q(FVector::UpVector, A + UE_PI / 2);
				FAcEmit Fast;
				Fast.Speed = 2.4;
				Particles.Stream(EAcParticle::Plume, FAcParticles::KeyOf(-7, 3), FTransform(Q, AcSpace::ToWorld(E, Effects->GroundZ(E) / Cm + 1.5)),
					80, Time, FrameDt, Fast);
			}
			if (Time < NextDemo || (DemoFired && DemoEvery <= 0)) return;
			NextDemo = Time + (DemoEvery > 0 ? DemoEvery : 1e9);
			DemoFired = Time;
			const ac::Vec2 C = *DemoAt;
			auto At = [&](const double Dx, const double Dy, const double Up)
			{
				const ac::Vec2 P(C.x + Dx, C.y + Dy);
				return AcSpace::ToWorld(P, Effects->GroundZ(P) / Cm + Up);
			};
			Effects->Explosion(At(-6, 0, 0.3), 1, Time);
			Effects->Explosion(At(-1, 0, 0.5), 3, Time);
			FAcBurstRequest B;
			B.Kind = EAcBurst::Blast;
			B.At = At(3.5, 0, 0.2);
			B.Size = 0.85;
			B.Time = Time;
			Effects->Burst(B);
			Effects->FlameLine(ac::Vec2(C.x + 5, C.y - 1.5), ac::Vec2(C.x + 9, C.y - 1.5), Time);
			Effects->DustPuff(At(6, 2, 0.1), 1.4, Time);
			Effects->CrystalBite(At(9, 2, 0.4), Time);
			FAcBurstRequest Ric;
			Ric.Kind = EAcBurst::Ricochet;
			Ric.At = At(9, -1.5, 0.5);
			Ric.Time = Time;
			Ric.Duration = 0.05;
			Ric.Along = FVector(1, 0.3, 0.2).GetSafeNormal();
			Effects->Burst(Ric);
			UE_LOG(LogAutocraft, Log, TEXT("particles: demo fired at game time %.3f"), Time);
		}

		UAcEffects* Effects = nullptr;
		FAcParticles Particles;
		TMap<int64, const ac::Unit*> Units;
		TMap<int64, const ac::Structure*> Structures;
		TOptional<double> LastTime;
		double Now = 0, FrameDt = 0, Alpha = 0;
		double StatsWall = 0;

		TOptional<ac::Vec2> DemoAt;
		double DemoEvery = 0, DemoAge = -1, NextDemo = 1.0;
		TOptional<double> DemoFired, DemoPausedAt;
		bool bHolding = false;
	};

	FAcEffectsExtensionRegistration Registration(TEXT("bursts"), 300,
		[] { return TUniquePtr<FAcEffectsExtension>(new FAcBursts); });
}

FAcParticles* AcEffectsBursts::ParticlesOf(const UAcEffects* Effects)
{
	FAcBursts* const* B = Live().Find(Effects);
	return B ? (*B)->Set() : nullptr;
}
