// Chunk C5: machines blown apart, flyers shot down, buildings collapsing
// (see AcEffectsShatter.h). Swift: GameScene.died :1367, blowApart :1546,
// destroyed :1561; Models+Dropship.swift shootDown :511; Effects+Shatter.swift.
#include "AcEffectsShatter.h"

#include "AcEffectPool.h"
#include "AcEffects.h"
#include "AcEffectsBursts.h"
#include "AcEffectsExtension.h"
#include "AcLog.h"
#include "AcModelCatalog.h"
#include "AcParticles.h"
#include "AcPose.h"
#include "AcShatter.h"
#include "AcShot.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"
#include "AcWorldRenderer.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include "TerrainField.h"
#include "Types.h"

#include <cmath>

namespace AcEffectsShatter
{
	bool SpecOf(const ac::UnitKind Kind, FSpec& S)
	{
		S = FSpec();
		switch (Kind)
		{
		case ac::UnitKind::prospector:
			S.Model = TEXT("prospector_blue"); S.Chunks = 7; S.Size = 0.7; S.Ring = 16;
			return true;
		case ac::UnitKind::firefly:
			// `strip: [h.flame, h.flare, h.pilot]`.
			S.Model = TEXT("firefly_blue"); S.Chunks = 10; S.Size = 1; S.Ring = 12;
			S.Strip = {TEXT("flame"), TEXT("flare"), TEXT("pilot")};
			return true;
		case ac::UnitKind::longbow:
			// `strip: t.flashes + [t.smoke]`.
			S.Model = TEXT("longbow_blue"); S.Chunks = 14; S.Size = 1.35; S.Ring = 10;
			S.Strip = {TEXT("flashes_"), TEXT("smoke")};
			return true;
		case ac::UnitKind::hailstorm:
			S.Model = TEXT("hailstorm_blue"); S.Chunks = 12; S.Size = 1.2; S.Ring = 10;
			S.Strip = {TEXT("flashes_")};
			return true;
		case ac::UnitKind::dropship:
			// `strip: [m.beam]` + its ground shadow.
			S.Model = TEXT("dropship_blue"); S.Chunks = 12; S.Size = 1.2; S.Ring = 6; S.bFlyer = true;
			S.Strip = {TEXT("beam"), TEXT("shadow")};
			return true;
		case ac::UnitKind::kestrel:
			S.Model = TEXT("kestrel_blue"); S.Chunks = 10; S.Size = 1.0; S.Ring = 8; S.bFlyer = true;
			S.Strip = {TEXT("flashes_"), TEXT("shadow")};
			return true;
		case ac::UnitKind::peregrine:
			// A light dart: shot down whole, then it bursts.
			S.Model = TEXT("peregrine_blue"); S.Chunks = 9; S.Size = 0.9; S.Ring = 8; S.bFlyer = true;
			return true;
		case ac::UnitKind::atlas:
			// Kneels, topples, then bursts: the biggest wreck of the units.
			S.Model = TEXT("atlas_blue"); S.Chunks = 18; S.Size = 2.0; S.Ring = 4; S.bTopple = true;
			return true;
		case ac::UnitKind::scorpion:
			// `strip: [mound]`: the heap of soil it buried in is not a wreck.
			S.Model = TEXT("scorpion_blue"); S.Chunks = 8; S.Size = 0.7; S.Ring = 12;
			S.Strip = {TEXT("mound")};
			return true;
		default:
			return false;
		}
	}

	bool SpecOf(const ac::StructureKind Kind, FSpec& S)
	{
		S = FSpec();
		S.bBuilding = true;
		S.Ring = 3;
		S.Height = AcPose::BuildingHeight(Kind);
		// GameScene.makeBuilding's radius.
		switch (Kind)
		{
		case ac::StructureKind::citadel: S.Radius = 2.5; break;
		case ac::StructureKind::lab:
		case ac::StructureKind::habDome:
		case ac::StructureKind::sentinel: S.Radius = 1; break;
		default: S.Radius = 1.5; break;
		}
		S.Chunks = int32(10 + S.Radius * 7);
		static TMap<int32, FString> Names;
		FString& Name = Names.FindOrAdd(int32(Kind));
		if (Name.IsEmpty()) Name = FString(AcPose::ModelBase(Kind)) + TEXT("_blue");
		S.Model = *Name;
		return true;
	}
}

namespace AcEffectsShatterPrivate
{
	using namespace AcEffectsShatter;
	constexpr double Cm = AcSpace::CmPerCell;
	/// Game seconds a removed object's pose waits for its death event.
	constexpr double KeepFor = 0.25;

	TAutoConsoleVariable<float> CVarStats(TEXT("ac.ShatterStats"), 0.f,
		TEXT("Log a `shatter:` line every N seconds (live wrecks, drawn subs); 0 off."));
	TAutoConsoleVariable<float> CVarBudget(TEXT("ac.ShatterBudgetMs"), 3.f,
		TEXT("Game-thread milliseconds a frame for cutting models into chunks after a game starts."));

	class FAcShatterDeaths final : public FAcEffectsExtension
	{
	public:
		virtual void Begin(UAcEffects& InEffects) override
		{
			Effects = &InEffects;
			for (int32 K = 0; K <= int32(ac::UnitKind::scorpion); ++K)
			{
				FSpec S;
				if (SpecOf(ac::UnitKind(K), S)) MakeRig(S);
			}
			for (int32 K = 0; K <= int32(ac::StructureKind::sentinel); ++K)
			{
				FSpec S;
				if (SpecOf(ac::StructureKind(K), S)) MakeRig(S);
			}
			if (UAcWorldRenderer* R = Effects->Renderer())
			{
				RemovedHandle = R->OnRemoved.AddRaw(this, &FAcShatterDeaths::OnRemoved);
			}
			FParse::Value(FCommandLine::Get(), TEXT("AcShatterStats="), CommandLineStats);
			FString Demo;
			if (FParse::Value(FCommandLine::Get(), TEXT("AcShatterDemo="), Demo))
			{
				FString X, Y;
				if (Demo.Replace(TEXT("\""), TEXT("")).Split(TEXT(","), &X, &Y))
				{
					DemoAt = ac::Vec2(FCString::Atod(*X), FCString::Atod(*Y));
					FParse::Value(FCommandLine::Get(), TEXT("AcShatterEvery="), DemoEvery);
					double Age = -1;
					if (FParse::Value(FCommandLine::Get(), TEXT("AcShatterAge="), Age) && Age >= 0)
					{
						DemoAge = Age;
						if (UAcShotSubsystem* Shot = UAcShotSubsystem::Get(Effects->GetWorld()))
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
			if (Effects)
				if (UAcWorldRenderer* R = Effects->Renderer()) R->OnRemoved.Remove(RemovedHandle);
			if (bHolding)
				if (UAcShotSubsystem* Shot = Effects ? UAcShotSubsystem::Get(Effects->GetWorld()) : nullptr) Shot->Release();
			bHolding = false;
			UE_LOG(LogAutocraft, Log, TEXT("shatter: %d wrecks (%d vehicles, %d flyers, %d buildings)"), Wrecks, Vehicles,
				Flyers, Buildings);
			Kept.Reset();
		}

		virtual void Clear() override
		{
			Kept.Reset();
			for (FRig& Rig : Rigs)
				for (int32 S = 0; S < Rig.Live.Num(); ++S)
					if (Rig.Live[S].bLive) Free(Rig, S);
			LastTime.Reset();
			NextDemo = 1.0;
			DemoFired.Reset();
		}

		virtual void Consume(const FAcFrame& Frame) override
		{
			for (const ac::GameEvent& E : Frame.Events)
			{
				if (const auto* D = E.as<ac::GameEvent::Died>())
				{
					FSpec S;
					if (SpecOf(D->kind, S)) UnitDied(S, D->kind, D->unit, int32(D->owner), D->at, Frame.Time);
				}
				else if (const auto* B = E.as<ac::GameEvent::Destroyed>())
				{
					FSpec S;
					if (SpecOf(B->kind, S)) BuildingDestroyed(S, B->structure, int32(B->owner), B->at, Frame.Time);
				}
			}
			// The rest left sight: dropped once their death could not come.
			for (auto It = Kept.CreateIterator(); It; ++It)
				if (Frame.Time - It->Value.At > KeepFor) It.RemoveCurrent();
		}

		virtual void Update(const double Time) override
		{
			const double T0 = FPlatformTime::Seconds();
			Library.Tick(CVarBudget.GetValueOnGameThread());
			for (FRig& Rig : Rigs)
				if (!Rig.bPools && Rig.Model) MakePools(Rig, false);
			const double Dt = LastTime ? FMath::Clamp(Time - *LastTime, 0.0, 0.1) : 0.0;
			LastTime = Time;
			if (DemoAt) Demo(Time);
			int32 Live = 0, Drawn = 0;
			for (FRig& Rig : Rigs)
			{
				for (int32 S = 0; S < Rig.Live.Num(); ++S)
				{
					if (!Rig.Live[S].bLive) continue;
					Place(Rig, S, Time, Dt);
					if (Rig.Live[S].bLive)
					{
						++Live;
						Drawn += Rig.Pools.Num();
					}
				}
			}
			const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
			StatsMs += Ms;
			StatsMax = FMath::Max(StatsMax, Ms);
			++StatsFrames;
			MaxLive = FMath::Max(MaxLive, Live);
			const float Every = CommandLineStats > 0 ? CommandLineStats : CVarStats.GetValueOnGameThread();
			if (Every > 0)
			{
				const double Now = FPlatformTime::Seconds();
				if (Now >= StatsAt)
				{
					StatsAt = Now + Every;
					UE_LOG(LogAutocraft, Log,
						TEXT("shatter: %d wrecks live (max %d), %d subs drawn, %d made so far, update %.3f ms mean %.3f max, cutting %s"),
						Live, MaxLive, Drawn, Wrecks, StatsFrames ? StatsMs / StatsFrames : 0.0, StatsMax,
						Library.IsIdle() ? TEXT("done") : TEXT("on"));
					StatsMs = StatsMax = 0;
					StatsFrames = 0;
				}
			}
		}

	private:
		struct FKept
		{
			FAcPose Pose;
			double At = 0;
		};
		/// A wreck in a rig's ring.
		struct FLive
		{
			bool bLive = false;
			int64 Id = 0;
			uint64 Key = 0;
			int32 Team = 0;
			/// When it was hit, and (flyers) when it burst on the ground.
			double Start = 0, Burst = 0;
			/// A flyer still falling whole (`shootDown`).
			bool bFalling = false;
			bool bBurst = false;
			FAcPose Last;
			ac::Vec2 At;
			double Y = 0;
			AcShatter::FWreck Wreck;
			TArray<FTransform> PartWorld;
			TArray<bool> PartShown;
			/// Each sub's emission at death.
			TArray<float> Emission;
		};
		struct FRig
		{
			FSpec Spec;
			FString ModelName;
			const FAcModelInfo* Model = nullptr;
			const AcShatter::FModel* Cut = nullptr;
			bool bPools = false;
			TArray<FAcEffectPool*> Pools;
			TArray<FAcShatterLibrary::FDraw> Draw;
			TArray<FLive> Live;
			int32 Cursor = 0;
			int32 LiveCount = 0;
		};

		void MakeRig(const FSpec& S)
		{
			const FAcModelInfo* Model = FAcModelCatalog::Get().Find(FName(S.Model));
			if (!Model)
			{
				UE_LOG(LogAutocraft, Warning, TEXT("shatter: no model %s"), S.Model);
				return;
			}
			FRig& Rig = Rigs.AddDefaulted_GetRef();
			Rig.Spec = S;
			Rig.ModelName = S.Model;
			Rig.Spec.Model = *Rig.ModelName;
			Rig.Model = Model;
			Rig.Live.SetNum(S.Ring);
			Library.Request(*Model, S.Chunks, S.Strip);
		}

		FRig* RigOf(const FAcModelInfo* Model)
		{
			return Rigs.FindByPredicate([Model](const FRig& R) { return R.Model == Model; });
		}

		/// The pools of a model once it is cut (`bNow`: cut it now).
		bool MakePools(FRig& Rig, const bool bNow)
		{
			if (Rig.bPools) return true;
			const AcShatter::FModel* Cut = bNow ? Library.Finish(*Rig.Model) : Library.Find(*Rig.Model);
			if (!Cut) return false;
			Rig.Cut = Cut;
			for (int32 I = 0; I < Cut->Subs.Num(); ++I)
			{
				const AcShatter::FSub& Sub = Cut->Subs[I];
				const FAcShatterLibrary::FDraw D = Library.MaterialFor(*Cut, Sub, Effects);
				const FName Name(*FString::Printf(TEXT("Wreck_%s_%d"), *Rig.ModelName, I));
				FAcEffectPool& Pool = Effects->MakePool(Sub.StaticMesh, D.Material, Rig.Spec.Ring, Name, Sub.bShadow);
				if (Pool.Component) Pool.Component->SetVisibility(false);
				Rig.Pools.Add(&Pool);
				Rig.Draw.Add(D);
			}
			Rig.bPools = true;
			return true;
		}

		void OnRemoved(const int64 Id, const FAcModelInfo& Model, TConstArrayView<FTransform>)
		{
			if (!RigOf(&Model)) return;
			UAcWorldRenderer* R = Effects ? Effects->Renderer() : nullptr;
			if (!R) return;
			const double Now = Effects->Now();
			R->ForEachObject([&](const int64 O, const bool, const FAcModelInfo& M, const FAcPose& Pose, TConstArrayView<FTransform>)
			{
				if (O != Id || &M != &Model) return;
				FKept& K = Kept.Add(Id);
				K.Pose = Pose;
				K.At = Now;
			});
		}

		double Ground(const double X, const double Z) const { return Effects->GroundZ(ac::Vec2(X, Z)) / Cm; }

		/// `GameScene.died` for a C5 kind.
		void UnitDied(const FSpec& S, const ac::UnitKind Kind, const int64 Unit, const int32 Team, const ac::Vec2 At,
			const double Time)
		{
			const FKept* K = Kept.Find(Unit);
			const double Y = Effects->GroundZ(At) / Cm;
			if (Kind == ac::UnitKind::prospector)
			{
				Effects->Explosion(AcSpace::ToWorld(At, Y + 0.6), 0.8, Time);
				FAcDecalRequest Mark;
				Mark.At = AcSpace::ToWorld(At, Y);
				Mark.Radius = 0.6 * Cm;
				Mark.Color = FLinearColor(0.04f, 0.04f, 0.04f, 1.f);
				Mark.Life = 20;
				Mark.Time = Time;
				Effects->Decal(Mark);
			}
			// Aboard a Dropship that went down, in a Derrick, or out of sight.
			if (!K || K->Pose.bHidden) return;
			FRig* Rig = RigOf(FAcModelCatalog::Get().Find(FName(S.Model)));
			if (!Rig) return;
			Start(*Rig, Unit, Team, At, Y, K->Pose, Time);
		}

		/// `GameScene.destroyed`.
		void BuildingDestroyed(const FSpec& S, const int64 Id, const int32 Team, const ac::Vec2 At, const double Time)
		{
			const FKept* K = Kept.Find(Id);
			if (!K) return;
			FRig* Rig = RigOf(FAcModelCatalog::Get().Find(FName(S.Model)));
			if (!Rig) return;
			Start(*Rig, Id, Team, At, Effects->GroundZ(At) / Cm, K->Pose, Time);
		}

		/// A wreck of `Rig`'s model from its last pose `Last`.
		void Start(FRig& Rig, const int64 Id, const int32 Team, const ac::Vec2 At, const double Y, const FAcPose& Last,
			const double Time)
		{
			if (!MakePools(Rig, true) || Last.Local.Num() != Rig.Model->Parts.Num()) return;
			const int32 S = Rig.Cursor;
			Rig.Cursor = (Rig.Cursor + 1) % Rig.Live.Num();
			if (Rig.Live[S].bLive) Free(Rig, S);
			FLive& L = Rig.Live[S];
			L = FLive();
			L.bLive = true;
			L.Id = Id;
			L.Key = (uint64(1) << 40) + uint64(++Serial) * 16;
			L.Team = Team;
			L.Start = Time;
			L.At = At;
			L.Y = Y;
			L.Last = Last;
			if (Rig.LiveCount++ == 0)
				for (FAcEffectPool* P : Rig.Pools)
					if (P->Component) P->Component->SetVisibility(true);
			// Each sub's emission in the last pose (C4's MeshEmission rule).
			const AcShatter::FModel& Cut = *Rig.Cut;
			L.Emission.SetNum(Cut.Subs.Num());
			for (int32 I = 0; I < Cut.Subs.Num(); ++I)
			{
				const AcShatter::FSub& Sub = Cut.Subs[I];
				float E = Last.Emission.IsValidIndex(Sub.Part) ? Last.Emission[Sub.Part] : 1.f;
				const FName Mat = Rig.Model->Parts[Sub.Part].Meshes[Sub.Mesh].Material;
				for (const FAcPose::FMeshEmission& Over : Last.MeshEmission)
					if (Over.Part == Sub.Part && Over.Material == Mat) E = Over.Value;
				L.Emission[I] = E;
			}
			++Wrecks;
			if (Rig.Spec.bBuilding)
			{
				++Buildings;
				Collapse(Rig, L, Time);
			}
			else if (Rig.Spec.bTopple)
			{
				// It kneels and goes over (`AtlasFallPose`), a small blast in
				// the torso as it is hit; it bursts where it lies.
				++Vehicles;
				L.bFalling = true;
				TArray<FTransform> W;
				TArray<bool> Shown;
				AcShatter::PartWorlds(*Rig.Model, Last, W, Shown);
				const int32* Torso = Rig.Model->PartIndex.Find(FName(TEXT("torso")));
				Effects->Explosion(Torso ? W[*Torso].GetLocation() : AcSpace::ToWorld(At, Y + 2), 0.7, Time);
			}
			else if (Rig.Spec.bFlyer)
			{
				++Flyers;
				// `shootDown`: a burst on the hull, then it drops.
				L.bFalling = true;
				TArray<FTransform> W;
				TArray<bool> Shown;
				AcShatter::PartWorlds(*Rig.Model, Last, W, Shown);
				const int32* Body = Rig.Model->PartIndex.Find(FName(TEXT("body")));
				const FVector Hull = Body ? W[*Body].GetLocation() : AcSpace::ToWorld(At, Y + 1);
				Effects->Explosion(Hull, 0.6, Time);
			}
			else
			{
				++Vehicles;
				AcShatter::PartWorlds(*Rig.Model, Last, L.PartWorld, L.PartShown);
				BlowApart(Rig, L, Time);
			}
			Place(Rig, S, Time, 0);
		}

		/// `GameScene.blowApart` + `Effects.launch` from `L.PartWorld`.
		void BlowApart(FRig& Rig, FLive& L, const double Time)
		{
			const double Size = Rig.Spec.Size;
			Effects->Explosion(AcSpace::ToWorld(L.At, L.Y + 0.5), Size, Time);
			FAcDebrisRequest D;
			D.At = AcSpace::ToWorld(L.At, L.Y + 0.45);
			D.Count = int32(3 * Size);
			D.Size = Size * 0.7;
			D.GroundZ = L.Y * Cm;
			D.Time = Time;
			D.Seed = int32(L.Id);
			Effects->Debris(D);
			// The Prospector leaves its dark mark instead.
			if (Rig.Model->Base != FName(TEXT("prospector")))
			{
				FAcScorchRequest Sc;
				Sc.At = AcSpace::ToWorld(L.At, L.Y);
				Sc.Radius = 0.95 * Size * Cm;
				Sc.Life = 22;
				Sc.Time = Time;
				Effects->Scorch(Sc);
			}
			TArray<FBox> Boxes;
			AcShatter::ChunkBoxes(*Rig.Cut, L.PartWorld, L.PartShown, Boxes);
			L.Wreck = AcShatter::Launch(*Rig.Cut, Boxes, Size, L.Id,
				[this](const double X, const double Z) { return Ground(X, Z); });
			L.Burst = Time;
			L.bBurst = true;
			L.bFalling = false;
		}

		/// `GameScene.destroyed` + `Effects.collapse`/`fell`.
		void Collapse(FRig& Rig, FLive& L, const double Time)
		{
			const double R = Rig.Spec.Radius, H = Rig.Spec.Height;
			const ac::Vec2 At = L.At;
			const double Y = L.Y;
			Effects->Explosion(AcSpace::ToWorld(At, Y + H * 0.5), 1.2 + R * 0.7, Time);
			TArray<AcShatter::FBlast> Blasts = {{At, 0.0}};
			for (int32 K = 0; K < 3; ++K)
			{
				const double A = double(K) * 2.1 + double(L.Id);
				const ac::Vec2 Off = ac::Vec2(std::cos(A), std::sin(A)) * (R * 0.6);
				const double Delay = 0.25 + double(K) * 0.4;
				Blasts.Add({At + Off, Delay});
				// More blasts go off as it breaks up.
				TWeakObjectPtr<UAcEffects> Weak(Effects);
				const FVector P = AcSpace::ToWorld(At + Off, Y + H * 0.35);
				const double Size = 0.6 + R * 0.35;
				Effects->After(Delay, Time, [Weak, P, Size, Time, Delay]
				{
					if (UAcEffects* E = Weak.Get()) E->Explosion(P, Size, Time + Delay);
				});
			}
			FAcDecalRequest Mark;
			Mark.At = AcSpace::ToWorld(At, Y);
			Mark.Radius = (R + 1.2) * Cm;
			Mark.Color = FLinearColor(0.03f, 0.03f, 0.03f, 1.f);
			Mark.Life = 45;
			Mark.Time = Time;
			Effects->Decal(Mark);
			AcShatter::PartWorlds(*Rig.Model, L.Last, L.PartWorld, L.PartShown);
			TArray<FBox> Boxes;
			AcShatter::ChunkBoxes(*Rig.Cut, L.PartWorld, L.PartShown, Boxes);
			L.Wreck = AcShatter::Fell(*Rig.Cut, Boxes, At, Y, H, R, MoveTemp(Blasts), L.Id,
				[this](const double X, const double Z) { return Ground(X, Z); });
			L.Burst = Time;
			L.bBurst = true;
		}

		void Free(FRig& Rig, const int32 S)
		{
			Rig.Live[S].bLive = false;
			for (FAcEffectPool* P : Rig.Pools) P->Hide(S);
			if (--Rig.LiveCount == 0)
				for (FAcEffectPool* P : Rig.Pools)
					if (P->Component) P->Component->SetVisibility(false);
		}

		void Place(FRig& Rig, const int32 S, const double Time, const double Dt)
		{
			FLive& L = Rig.Live[S];
			const AcShatter::FModel& Cut = *Rig.Cut;
			FAcParticles* Particles = AcEffectsBursts::ParticlesOf(Effects);
			if (L.bFalling)
			{
				const double Sec = Time - L.Start;
				const bool bTopple = Rig.Spec.bTopple;
				const double FallTime = bTopple ? AcShatter::AtlasFallTime : AcShatter::FallTime;
				auto FallPose = [&](const double At, FAcPose& Out)
				{
					if (bTopple) AcShatter::AtlasFallPose(*Rig.Model, L.Last, At, Out);
					else AcShatter::ShootDownPose(*Rig.Model, L.Last, L.Id, At, Out);
				};
				if (Sec >= FallTime)
				{
					// It bursts on the ground, from where the fall left it.
					FAcPose Final;
					FallPose(FallTime, Final);
					AcShatter::PartWorlds(*Rig.Model, Final, L.PartWorld, L.PartShown);
					BlowApart(Rig, L, L.Start + FallTime);
				}
				else
				{
					FAcPose Now;
					FallPose(FMath::Max(0.0, Sec), Now);
					TArray<FTransform> W;
					TArray<bool> Shown;
					AcShatter::PartWorlds(*Rig.Model, Now, W, Shown);
					for (int32 I = 0; I < Cut.Subs.Num(); ++I)
					{
						const AcShatter::FSub& Sub = Cut.Subs[I];
						FAcEffectPool& Pool = *Rig.Pools[I];
						if (!Shown[Sub.Part])
						{
							Pool.Hide(S);
							continue;
						}
						Pool.Set(S, W[Sub.Part]);
						Pool.SetCustom(S, AcModelData::TeamIndex, float(L.Team));
						Pool.SetCustom(S, AcModelData::EmissionScale, L.Emission[I]);
						Pool.SetCustom(S, AcModelData::Fade, 0.f);
						Pool.SetCustom(S, AcModelData::Char, -1.f);
					}
					// The trail: `damageFire(radius: 0.35)`, fire 70, smoke 30 on the body.
					const int32* Body = Rig.Model->PartIndex.Find(FName(TEXT("body")));
					if (Particles && Body && Dt > 0)
					{
						const FTransform& B = W[*Body];
						FAcEmit E;
						E.ShapeScale = bTopple ? 0.7 : 0.35;
						Particles->Stream(EAcParticle::DamageFire, L.Key + 14, B, 70, Time, Dt, E);
						Particles->Stream(EAcParticle::DamageSmoke, L.Key + 15, B, 30, Time, Dt, E);
					}
					return;
				}
			}
			const AcShatter::FWreck& W = L.Wreck;
			const double Sec = FMath::Max(0.0, Time - L.Burst);
			if (Sec >= W.Life)
			{
				Free(Rig, S);
				return;
			}
			const float Opacity = float(AcShatter::Opacity(W, Sec));
			const float Glow = float(AcShatter::Glow(W, Sec));
			TArray<FTransform, TInlineAllocator<32>> Delta;
			Delta.SetNum(W.Chunks.Num());
			for (int32 C = 0; C < W.Chunks.Num(); ++C) Delta[C] = AcShatter::ChunkDelta(W, C, Sec);
			for (int32 I = 0; I < Cut.Subs.Num(); ++I)
			{
				const AcShatter::FSub& Sub = Cut.Subs[I];
				FAcEffectPool& Pool = *Rig.Pools[I];
				if (!L.PartShown[Sub.Part] || DebugHidden(Cut, Sub))
				{
					Pool.Hide(S);
					continue;
				}
				Pool.Set(S, L.PartWorld[Sub.Part] * Delta[Sub.Chunk]);
				const FAcShatterLibrary::FDraw& D = Rig.Draw[I];
				Pool.SetCustom(S, AcModelData::TeamIndex, float(L.Team));
				Pool.SetCustom(S, AcModelData::Fade, D.bDither ? 1.f - Opacity : 0.f);
				if (D.bEmber)
				{
					// Scorched: its own emission gone, hot spots that cool.
					Pool.SetCustom(S, AcModelData::EmissionScale, 0.f);
					Pool.SetCustom(S, AcModelData::Char, Glow);
				}
				else
				{
					// The lamps go dark.
					const float E = L.Emission[I] * 0.12f;
					Pool.SetCustom(S, AcModelData::EmissionScale, D.bDither ? E : E * Opacity);
				}
			}
			if (!Particles || Dt <= 0) return;
			for (int32 C = 0; C < W.Chunks.Num(); ++C)
			{
				const AcShatter::FChunk& K = W.Chunks[C];
				if (K.Fire == 0 || K.bEmpty) continue;
				double Fire = 0, Smoke = 0;
				AcShatter::FireRates(W, C, Sec, Fire, Smoke);
				if (Fire <= 0 && Smoke <= 0) continue;
				const FTransform F = AcShatter::FireFrame(W, C, Sec);
				// Streams keyed per chunk (a wreck has at most 30 chunks).
				const uint64 Key = (L.Key << 6) ^ uint64(C * 2);
				if (Fire > 0)
				{
					FAcEmit E;
					E.ShapeScale = K.FireRadius;
					E.Size = K.FireSize;
					Particles->Stream(EAcParticle::DamageFire, Key, F, Fire, Time, Dt, E);
				}
				if (Smoke > 0)
				{
					FAcEmit E;
					E.ShapeScale = K.FireRadius;
					E.Size = K.SmokeSize;
					Particles->Stream(EAcParticle::DamageSmoke, Key + 1, F, Smoke, Time, Dt, E);
				}
			}
		}

		/// Env `AC_SHATTER_HIDE=part,part:mesh`: subs not drawn (to find one).
		static bool DebugHidden(const AcShatter::FModel& Cut, const AcShatter::FSub& Sub)
		{
			static const TArray<FString> Hide = []
			{
				TArray<FString> H;
				FPlatformMisc::GetEnvironmentVariable(TEXT("AC_SHATTER_HIDE")).ParseIntoArray(H, TEXT(","));
				return H;
			}();
			if (Hide.IsEmpty()) return false;
			const FString Part = Cut.Model->Parts[Sub.Part].Name.ToString();
			const FString Mesh = FString::Printf(TEXT("%s:%d"), *Part, Sub.Mesh);
			return Hide.Contains(Part) || Hide.Contains(Mesh) || Hide.Contains(FString::Printf(TEXT("chunk%d"), Sub.Chunk));
		}

		/// `-AcShatterDemoOnly=a,b`: only the models whose names start with
		/// one of these (`citadel`, `prospector`), in their usual places.
		static bool DemoWants(const TCHAR* Model)
		{
			static FString Only;
			static bool bRead = false;
			if (!bRead)
			{
				bRead = true;
				FParse::Value(FCommandLine::Get(), TEXT("AcShatterDemoOnly="), Only, false);
				Only.ReplaceInline(TEXT("\""), TEXT(""));
			}
			if (Only.IsEmpty()) return true;
			TArray<FString> Names;
			Only.ParseIntoArray(Names, TEXT(","));
			for (const FString& N : Names)
				if (FString(Model).StartsWith(N)) return true;
			return false;
		}

		/// `-AcShatterDemo="X,Y"`: every C5 kind dies at once in two rows at
		/// sim (X, Y) (vehicles, then buildings 9 cells further), from its
		/// rest pose, 1 s into the game and every `-AcShatterEvery` seconds;
		/// `-AcShatterAge=A` pauses the game A seconds after (and holds
		/// `-AcShot` until then).
		void Demo(const double Time)
		{
			if (DemoPausedAt) return;
			if (DemoFired && DemoAge >= 0 && Time >= *DemoFired + DemoAge)
			{
				DemoPausedAt = Time;
				if (UAcSimSubsystem* Sim = Effects->Sim()) Sim->SetPaused(true);
				if (bHolding)
					if (UAcShotSubsystem* Shot = UAcShotSubsystem::Get(Effects->GetWorld())) Shot->Release();
				bHolding = false;
				UE_LOG(LogAutocraft, Log, TEXT("shatter: demo paused %.3f s after the deaths"), Time - *DemoFired);
				return;
			}
			if (Time < NextDemo || (DemoFired && DemoEvery <= 0)) return;
			NextDemo = Time + (DemoEvery > 0 ? DemoEvery : 1e9);
			DemoFired = Time;
			UAcSimSubsystem* Sim = Effects->Sim();
			if (!Sim || !Sim->IsRunning()) return;
			const ac::TerrainField Field(Sim->Map());
			ac::GameState State;
			State.time = Time;
			int32 Col = 0;
			const ac::UnitKind Units[] = {ac::UnitKind::prospector, ac::UnitKind::firefly, ac::UnitKind::longbow,
				ac::UnitKind::hailstorm, ac::UnitKind::dropship, ac::UnitKind::kestrel};
			for (const ac::UnitKind Kind : Units)
			{
				FSpec Spec;
				SpecOf(Kind, Spec);
				FRig* Rig = RigOf(FAcModelCatalog::Get().Find(FName(Spec.Model)));
				if (!Rig) continue;
				const ac::Vec2 P = *DemoAt + ac::Vec2(3.2 * Col++, 0);
				if (!DemoWants(Spec.Model)) continue;
				const int64 Id = 900001 + Col;
				ac::Unit U(Id, Kind, 1, P, UE_DOUBLE_HALF_PI + 0.6, ac::Unit::Task::idle);
				FAcUnitMemory Memory;
				Memory.LastHeading = U.heading;
				FAcPose Pose;
				FAcPoseContext C{State, &U, nullptr, *Rig->Model, Field, Memory, Time, 0.0, false, 0};
				AcPose::RestUnit(C, Pose);
				if (const FAcPoseFn Fn = AcPose::Find(Kind)) Fn(C, Pose);
				const double Y = Effects->GroundZ(P) / Cm;
				if (Kind == ac::UnitKind::prospector)
				{
					Effects->Explosion(AcSpace::ToWorld(P, Y + 0.6), 0.8, Time);
				}
				Start(*Rig, Id, 1, P, Y, Pose, Time);
			}
			Col = 0;
			for (int32 K = 0; K <= int32(ac::StructureKind::sentinel); ++K)
			{
				FSpec Spec;
				SpecOf(ac::StructureKind(K), Spec);
				FRig* Rig = RigOf(FAcModelCatalog::Get().Find(FName(Spec.Model)));
				if (!Rig) continue;
				const ac::Vec2 P = *DemoAt + ac::Vec2(6.5 * Col++, 9);
				if (!DemoWants(Spec.Model)) continue;
				const int64 Id = 900101 + K;
				ac::Structure B(Id, ac::StructureKind(K), 1, P);
				FAcUnitMemory Memory;
				FAcPose Pose;
				FAcPoseContext C{State, nullptr, &B, *Rig->Model, Field, Memory, Time, 0.0, false, 0};
				AcPose::RestStructure(C, Pose);
				if (const FAcPoseFn Fn = AcPose::Find(ac::StructureKind(K))) Fn(C, Pose);
				Start(*Rig, Id, 1, P, Effects->GroundZ(P) / Cm, Pose, Time);
			}
			UE_LOG(LogAutocraft, Log, TEXT("shatter: demo deaths at %.2f"), Time);
		}

		UAcEffects* Effects = nullptr;
		FAcShatterLibrary Library;
		TArray<FRig> Rigs;
		TMap<int64, FKept> Kept;
		FDelegateHandle RemovedHandle;
		TOptional<double> LastTime;
		uint64 Serial = 0;
		int32 Wrecks = 0, Vehicles = 0, Flyers = 0, Buildings = 0;
		double StatsAt = 0, StatsMs = 0, StatsMax = 0;
		int32 StatsFrames = 0, MaxLive = 0;
		float CommandLineStats = 0;

		TOptional<ac::Vec2> DemoAt;
		double DemoEvery = 0, DemoAge = -1, NextDemo = 1.0;
		TOptional<double> DemoFired, DemoPausedAt;
		bool bHolding = false;
	};

	FAcEffectsExtensionRegistration Registration(TEXT("shatter"), 140,
		[] { return TUniquePtr<FAcEffectsExtension>(new FAcShatterDeaths); });
}
