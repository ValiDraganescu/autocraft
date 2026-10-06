// Chunk C4: the infantry falls, their blood marks, and debris (see
// AcEffectsDeaths.h). Swift: GameScene.died :1367, GameScene.fall
// :1417-1498, Effects.debris :542, Effects.decal :717.
#include "AcEffectsDeaths.h"

#include "AcEffectPool.h"
#include "AcEffects.h"
#include "AcEffectsExtension.h"
#include "AcLog.h"
#include "AcModelCatalog.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"
#include "AcWorldRenderer.h"

#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeLock.h"

#include "Types.h"

#include <cmath>

namespace AcDeathsPrivate
{
	constexpr double Cm = AcSpace::CmPerCell;
	/// The renderer's rule (AcWorldRenderer.cpp): parts under 10 cm cast no
	/// shadow (`Models+Flatten.swift:47`).
	constexpr double NoShadowCm = 10.0;
	const TCHAR* const MHull = TEXT("/Game/Materials/M_Hull.M_Hull");
	const TCHAR* const MEmissive = TEXT("/Game/Materials/M_Emissive.M_Emissive");
	const TCHAR* const MHullFade = TEXT("/Game/Materials/M_AcHullFade.M_AcHullFade");
	const TCHAR* const MEmissiveFade = TEXT("/Game/Materials/M_AcEmissiveFade.M_AcEmissiveFade");
	const TCHAR* const THull = TEXT("/Game/Models/Textures/T_hull.T_hull");

	double Smoothstep(const double A, const double B, const double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	double SrgbToLinear(const double C)
	{
		return C <= 0.04045 ? C / 12.92 : std::pow((C + 0.055) / 1.055, 2.4);
	}

	/// SceneKit `eulerAngles` → its quaternion in SceneKit space
	/// (R = Rz·Ry·Rx, as AcPoseVehicles.cpp checked against the export).
	FQuat SkEuler(const FVector& E)
	{
		return FQuat(FVector(0, 0, 1), E.Z) * FQuat(FVector(0, 1, 0), E.Y) * FQuat(FVector(1, 0, 0), E.X);
	}

	/// A SceneKit quaternion → `eulerAngles` (the inverse of `SkEuler`), as
	/// SceneKit reads them back before one of them is set.
	FVector EulerOf(const FQuat& Sk)
	{
		const FVector X = Sk.RotateVector(FVector(1, 0, 0));
		const FVector Y = Sk.RotateVector(FVector(0, 1, 0));
		const FVector Z = Sk.RotateVector(FVector(0, 0, 1));
		return FVector(std::atan2(Y.Z, Z.Z), std::asin(FMath::Clamp(-X.Z, -1.0, 1.0)), std::atan2(X.Y, X.X));
	}

	/// The export merged nodes without handles into their parents, so a
	/// part's rest can be its own SceneKit transform under vanished nodes:
	/// Rest = Own × Chain (AcPoseVehicles.cpp's `ChainsFor`).
	struct FChains
	{
		TArray<FMatrix> Chain;
		TArray<bool> bIdentity;
	};

	const FChains& ChainsFor(const FAcModelInfo& M)
	{
		static FCriticalSection Lock;
		static TMap<const FAcModelInfo*, TUniquePtr<FChains>> Map;
		FScopeLock Guard(&Lock);
		if (const TUniquePtr<FChains>* Found = Map.Find(&M)) return **Found;
		TUniquePtr<FChains> C = MakeUnique<FChains>();
		for (const FAcModelPart& Part : M.Parts)
		{
			const FTransform Own = AcSpace::TransformFromSceneKit(Part.SkPosition, SkEuler(Part.SkEuler), Part.SkScale);
			const FMatrix Chain = Own.ToMatrixWithScale().Inverse() * Part.Rest.ToMatrixWithScale();
			C->Chain.Add(Chain);
			C->bIdentity.Add(Chain.Equals(FMatrix::Identity, 1e-3));
		}
		const FChains& Out = *C;
		Map.Add(&M, MoveTemp(C));
		return Out;
	}

	/// A part's local from its SceneKit node values.
	FTransform SkLocal(const FAcModelInfo& M, const int32 I, const FVector& Pos, const FVector& Euler, const FVector& Scale)
	{
		const FTransform Own = AcSpace::TransformFromSceneKit(Pos, SkEuler(Euler), Scale);
		const FChains& C = ChainsFor(M);
		return C.bIdentity[I] ? Own : FTransform(Own.ToMatrixWithScale() * C.Chain[I]);
	}

	/// A part's SceneKit position and Euler angles from its local.
	void SkOf(const FAcModelInfo& M, const int32 I, const FTransform& Local, FVector& Pos, FVector& Euler)
	{
		const FChains& C = ChainsFor(M);
		const FTransform Own = C.bIdentity[I] ? Local : FTransform(Local.ToMatrixWithScale() * C.Chain[I].Inverse());
		Pos = AcSpace::ToSceneKit(Own.GetTranslation());
		Euler = EulerOf(AcSpace::QuatToSceneKit(Own.GetRotation()));
	}

	int32 PartNamed(const FAcModelInfo& M, const TCHAR* Name)
	{
		const int32* I = M.PartIndex.Find(FName(Name));
		return I ? *I : INDEX_NONE;
	}

	const TCHAR* ModelName(const AcDeaths::EFall Kind)
	{
		switch (Kind)
		{
		case AcDeaths::EFall::Juggernaut: return TEXT("juggernaut_blue");
		case AcDeaths::EFall::Comet: return TEXT("comet_blue");
		default: return TEXT("ranger_blue");
		}
	}
}

// --- The fall, in closed form -------------------------------------------------

namespace AcDeaths
{
	using namespace AcDeathsPrivate;

	TOptional<EFall> FallOf(const ac::UnitKind Kind)
	{
		switch (Kind)
		{
		case ac::UnitKind::ranger: return EFall::Ranger;
		case ac::UnitKind::juggernaut: return EFall::Juggernaut;
		case ac::UnitKind::comet: return EFall::Comet;
		default: return {};
		}
	}

	FBody Start(const EFall Kind, const int64 Unit, const FAcModelInfo& Model, const FAcPose& Last)
	{
		FBody B;
		B.Kind = Kind;
		B.Model = &Model;
		B.Unit = Unit;
		// `body.position = root.position; body.eulerAngles.y = root.eulerAngles.y`
		// (then the root goes to zero under it).
		B.Body = FTransform(Last.Placement.GetRotation(), (Last.Local[0] * Last.Placement).GetTranslation());
		B.Local = Last.Local;
		B.Visible = Last.Visible;
		B.Emission = Last.Emission;
		B.MeshEmission.Append(Last.MeshEmission.GetData(), Last.MeshEmission.Num());
		B.Side = Unit % 2 == 0 ? 1.0 : -1.0;
		auto Hide = [&](const TCHAR* Name)
		{
			const int32 I = PartNamed(Model, Name);
			if (I != INDEX_NONE) B.Visible[I] = false;
		};
		auto Turn = [&](const TCHAR* Name)
		{
			const int32 I = PartNamed(Model, Name);
			if (I == INDEX_NONE) return;
			FBody::FTurned T;
			T.Part = I;
			T.Scale = Model.Parts[I].SkScale;
			SkOf(Model, I, Last.Local[I], T.Pos, T.Euler);
			B.Turned.Add(T);
		};
		switch (Kind)
		{
		case EFall::Ranger:
			B.Life = 7;
			// `m.flash.isHidden = true`; the Mini gun's flash too (Swift leaves
			// it as it was: a flash frozen on the body for 7 s).
			Hide(TEXT("flash"));
			Hide(TEXT("minigun_flash"));
			Turn(TEXT("gun"));
			break;
		case EFall::Juggernaut:
			B.Life = 8;
			Hide(TEXT("flashes_0"));
			Hide(TEXT("flashes_1"));
			Turn(TEXT("launchers_0"));
			Turn(TEXT("launchers_1"));
			break;
		case EFall::Comet:
			B.Life = 7;
			for (const TCHAR* N : {TEXT("flashes_0"), TEXT("flashes_1"), TEXT("flames_0"), TEXT("flames_1"),
					 TEXT("halos_0"), TEXT("halos_1")})
			{
				Hide(N);
			}
			// `jetGlow.emission.intensity = 0.1`, `eyeGlow … = 0.2`, over the
			// beetle's exported 0.8 and 1.6.
			B.MaterialEmission.Add({FName(TEXT("jetGlow")), 0.1f / 0.8f});
			B.MaterialEmission.Add({FName(TEXT("eyeGlow")), 0.2f / 1.6f});
			break;
		}
		return B;
	}

	double Pose(const FBody& B, const double Sec, TArray<FTransform>& World, TArray<bool>& Visible)
	{
		const FAcModelInfo& M = *B.Model;
		TArray<FTransform> Local = B.Local;
		double RootX = 0, RootZ = 0, RootY = 0, Opacity = 1;
		switch (B.Kind)
		{
		case EFall::Ranger:
		{
			const double Fall = Smoothstep(0, 0.45, Sec);
			RootZ = 1.5 * Fall;
			RootX = 0.25 * Fall * B.Side;
			RootY = 0.12 * Fall - FMath::Max(0.0, Sec - 5) * 0.25;
			Opacity = 1 - Smoothstep(5, 7, Sec);
			for (const FBody::FTurned& T : B.Turned)
			{
				// `m.gun.eulerAngles.z = -0.55 - 0.6 * fall`
				const FVector E(T.Euler.X, T.Euler.Y, -0.55 - 0.6 * Fall);
				Local[T.Part] = SkLocal(M, T.Part, T.Pos, E, T.Scale);
			}
			break;
		}
		case EFall::Juggernaut:
		{
			// Slow to start, fast at the end: the weight goes over.
			const double Fall = FMath::Min(1.0, std::pow(Sec / 0.5, 2.0));
			const double Bounce = Sec > 0.5 ? 0.06 * std::exp(-(Sec - 0.5) * 9) * std::abs(std::sin((Sec - 0.5) * 18)) : 0.0;
			RootZ = 1.42 * Fall;
			RootX = 0.2 * Fall * B.Side;
			RootY = 0.2 * Fall + Bounce - FMath::Max(0.0, Sec - 6) * 0.3;
			Opacity = 1 - Smoothstep(6, 8, Sec);
			// The gorilla's `hang` (Models+JuggernautGorilla.swift:220).
			constexpr double Hang = -0.85;
			for (int32 I = 0; I < B.Turned.Num(); ++I)
			{
				const FBody::FTurned& T = B.Turned[I];
				const FVector E(T.Euler.X, T.Euler.Y, Hang + 1.6 * Fall + 0.1 * double(I));
				Local[T.Part] = SkLocal(M, T.Part, T.Pos, E, T.Scale);
			}
			break;
		}
		case EFall::Comet:
		{
			const double Fall = Smoothstep(0, 0.4, Sec);
			RootZ = -1.45 * Fall;
			RootY = 0.1 * Fall - FMath::Max(0.0, Sec - 5) * 0.25;
			Opacity = 1 - Smoothstep(5, 7, Sec);
			break;
		}
		}
		// The root under the body: at (0, y, 0), turned about its x and z.
		Local[0] = SkLocal(M, 0, FVector(0, RootY, 0), FVector(RootX, 0, RootZ), M.Parts[0].SkScale);

		const int32 N = M.Parts.Num();
		World.SetNum(N);
		Visible = B.Visible;
		for (int32 P = 0; P < N; ++P)
		{
			const int32 Parent = M.Parts[P].Parent;
			if (Parent != INDEX_NONE)
			{
				World[P] = Local[P] * World[Parent];
				Visible[P] = Visible[P] && Visible[Parent];
			}
			else
			{
				World[P] = Local[P] * B.Body;
			}
		}
		return Opacity;
	}

	float MeshEmission(const FBody& B, const int32 Part, const int32 Mesh)
	{
		const FAcModelMesh& X = B.Model->Parts[Part].Meshes[Mesh];
		for (const TPair<FName, float>& Over : B.MaterialEmission)
		{
			if (Over.Key == X.Material) return Over.Value;
		}
		float E = B.Emission.IsValidIndex(Part) ? B.Emission[Part] : 1.f;
		for (const FAcPose::FMeshEmission& Over : B.MeshEmission)
		{
			if (Over.Part == Part && Over.Material == X.Material) E = Over.Value;
		}
		return E;
	}

	UMaterialInterface* HullFade()
	{
		static TWeakObjectPtr<UMaterialInterface> M;
		if (!M.IsValid()) M = LoadObject<UMaterialInterface>(nullptr, MHullFade);
		return M.Get();
	}

	UMaterialInterface* EmissiveFade()
	{
		static TWeakObjectPtr<UMaterialInterface> M;
		if (!M.IsValid()) M = LoadObject<UMaterialInterface>(nullptr, MEmissiveFade);
		return M.Get();
	}

	FFadeMaterial FadeMaterial(UMaterialInterface* Source, UObject* Outer)
	{
		FFadeMaterial Out;
		Out.Material = Source;
		if (!Source) return Out;
		const UMaterial* Base = Source->GetMaterial();
		if (!Base || Source->GetBlendMode() != BLEND_Opaque) return Out;
		const FString Path = Base->GetPathName();
		UMaterialInterface* Fade = Path == MHull ? HullFade() : Path == MEmissive ? EmissiveFade() : nullptr;
		if (!Fade) return Out;
		UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(Fade, Outer);
		if (UMaterialInstance* MI = Cast<UMaterialInstance>(Source)) D->CopyParameterOverrides(MI);
		Out.Material = D;
		Out.bDither = true;
		return Out;
	}
}

// --- The extension --------------------------------------------------------------

namespace AcDeathsPrivate
{
	using namespace AcDeaths;

	class FAcDeaths final : public FAcEffectsExtension
	{
	public:
		virtual void Begin(UAcEffects& InEffects) override
		{
			Effects = &InEffects;
			for (const EFall Kind : {EFall::Ranger, EFall::Juggernaut, EFall::Comet}) MakeRig(Kind);
			MakeDebris();
			bDebrisDemo = FParse::Param(FCommandLine::Get(), TEXT("AcDebrisDemo"));
			if (UAcWorldRenderer* R = Effects->Renderer())
			{
				RemovedHandle = R->OnRemoved.AddRaw(this, &FAcDeaths::OnRemoved);
			}
			if (!HullFade() || !EmissiveFade())
			{
				UE_LOG(LogAutocraft, Warning,
					TEXT("effects deaths: no M_AcHullFade/M_AcEmissiveFade (run Tools/Editor/make_corpse_materials.py): bodies vanish unfaded"));
			}
		}

		virtual void End() override
		{
			if (Effects)
			{
				if (UAcWorldRenderer* R = Effects->Renderer()) R->OnRemoved.Remove(RemovedHandle);
			}
			UE_LOG(LogAutocraft, Log, TEXT("effects deaths: %d falls, %d debris chunks"), Falls, DebrisThrown);
			Kept.Reset();
		}

		virtual void Consume(const FAcFrame& Frame) override
		{
			for (const ac::GameEvent& E : Frame.Events)
			{
				const auto* D = E.as<ac::GameEvent::Died>();
				if (!D) continue;
				const TOptional<EFall> Kind = FallOf(D->kind);
				if (!Kind) continue;
				const FKept* K = Kept.Find(D->unit);
				// Aboard a Dropship that went down, or out of sight: no body.
				if (!K || K->bHidden) continue;
				Fall(*Kind, D->unit, int32(D->owner), D->at, *K, Frame.Time);
			}
			// The rest left sight (or went aboard): nothing to play.
			Kept.Reset();
		}

		virtual void Update(const double Time) override
		{
			if (bDebrisDemo) DebrisDemo(Time);
			for (FRig& Rig : Rigs) UpdateRig(Rig, Time);
			UpdateDebris(Time);
		}

		virtual void Clear() override
		{
			Kept.Reset();
			for (FRig& Rig : Rigs)
			{
				for (int32 S = 0; S < Rig.Bodies.Num(); ++S)
				{
					if (Rig.Bodies[S].bLive) HideBody(Rig, S);
				}
			}
			for (int32 S = 0; S < Chunks.Num(); ++S)
			{
				Chunks[S].bLive = false;
				if (DebrisPool) DebrisPool->Hide(S);
			}
		}

		virtual bool HandleDebris(const FAcDebrisRequest& R) override
		{
			if (!DebrisPool) return false;
			// Effects.debris: the chunks' sizes, throws and spins from the seed.
			const FVector P = AcSpace::ToSceneKit(R.At);
			for (int32 K = 0; K < R.Count; ++K)
			{
				const double Base = double(R.Seed) * 12.9898 + double(K) * 78.233;
				auto Rnd = [Base](const double I) { return 0.5 + 0.5 * std::sin(Base + I * 37.719); };
				FChunk C;
				C.Size = FVector(R.Size * (0.12 + 0.14 * Rnd(1)), R.Size * (0.05 + 0.08 * Rnd(2)), R.Size * (0.1 + 0.12 * Rnd(3)));
				const double A = Rnd(4) * 2 * UE_DOUBLE_PI, Out = 2.2 + 2.5 * Rnd(5);
				C.V = FVector(std::cos(A) * Out, 3.5 + 3 * Rnd(6), std::sin(A) * Out);
				C.Spin = FVector(Rnd(7), Rnd(8), Rnd(9)) * 14 - 7;
				C.P = P;
				C.Ground = R.GroundZ / Cm;
				C.Start = R.Time;
				C.bLive = true;
				const int32 S = DebrisPool->Next();
				Chunks[S] = C;
				++DebrisThrown;
				PlaceChunk(S, R.Time);
			}
			return true;
		}

	private:
		/// The last pose the renderer drew of a unit it just dropped.
		struct FKept
		{
			const FAcModelInfo* Model = nullptr;
			FAcPose Pose;
			bool bHidden = false;
		};
		struct FLying
		{
			FBody Body;
			int32 Team = 0;
			double Start = 0;
			bool bLive = false;
		};
		/// A kind's bodies: one pool per mesh of its model, slot k of every
		/// pool is body k.
		struct FRig
		{
			EFall Kind = EFall::Ranger;
			const FAcModelInfo* Model = nullptr;
			TArray<int32> Part, Mesh;
			TArray<FAcEffectPool*> Pools;
			TArray<bool> Dither;
			TArray<FLying> Bodies;
			int32 Cursor = 0;
			TArray<FTransform> World;
			TArray<bool> Visible;
		};
		struct FChunk
		{
			FVector P = FVector::ZeroVector;   // SceneKit, cells
			FVector V = FVector::ZeroVector;
			FVector Spin = FVector::ZeroVector;
			FVector Size = FVector::OneVector;  // SceneKit box w, h, l, cells
			double Ground = 0;
			double Start = 0;
			bool bLive = false;
		};

		static int32 RingSize(const EFall Kind)
		{
			switch (Kind)
			{
			case EFall::Juggernaut: return 16;
			case EFall::Comet: return 24;
			default: return 40;
			}
		}

		void MakeRig(const EFall Kind)
		{
			const FAcModelInfo* Model = FAcModelCatalog::Get().Find(FName(ModelName(Kind)));
			if (!Model)
			{
				UE_LOG(LogAutocraft, Warning, TEXT("effects deaths: no model %s"), ModelName(Kind));
				return;
			}
			FRig& Rig = Rigs.AddDefaulted_GetRef();
			Rig.Kind = Kind;
			Rig.Model = Model;
			const int32 Ring = RingSize(Kind);
			for (int32 P = 0; P < Model->Parts.Num(); ++P)
			{
				for (int32 K = 0; K < Model->Parts[P].Meshes.Num(); ++K)
				{
					const FAcModelMesh& X = Model->Parts[P].Meshes[K];
					UStaticMesh* Mesh = X.LoadMesh();
					if (!Mesh) continue;
					const FFadeMaterial F = FadeMaterial(X.LoadMaterial(), Effects);
					const double Scale = Model->RestToModel(P).GetMaximumAxisScale();
					const bool bShadow = X.Blend == TEXT("opaque") && Mesh->GetBoundingBox().GetSize().GetMax() * Scale >= NoShadowCm;
					const FName Name(*FString::Printf(TEXT("Body_%s_%d"), *Model->Name.ToString(), Rig.Pools.Num()));
					Rig.Pools.Add(&Effects->MakePool(Mesh, F.Material, Ring, Name, bShadow));
					Rig.Part.Add(P);
					Rig.Mesh.Add(K);
					Rig.Dither.Add(F.bDither);
				}
			}
			Rig.Bodies.SetNum(Ring);
		}

		void MakeDebris()
		{
			UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			UMaterialInterface* Fade = HullFade();
			if (!Cube || !Fade) return;
			// Effects.charMaterial: lib.hull × white 0.14, metal 0.45, rough 0.85.
			UMaterialInstanceDynamic* Char = UMaterialInstanceDynamic::Create(Fade, Effects);
			if (UTexture* Hull = LoadObject<UTexture>(nullptr, THull)) Char->SetTextureParameterValue(TEXT("BaseColorTex"), Hull);
			const float Dark = float(SrgbToLinear(0.14));
			Char->SetVectorParameterValue(TEXT("BaseColorTint"), FLinearColor(Dark, Dark, Dark, 1));
			Char->SetVectorParameterValue(TEXT("EmissiveColor"), FLinearColor::Black);
			Char->SetScalarParameterValue(TEXT("Metallic"), 0.45f);
			Char->SetScalarParameterValue(TEXT("Roughness"), 0.85f);
			DebrisPool = &Effects->MakePool(Cube, Char, DebrisSlots, TEXT("Debris"), true);
			Chunks.SetNum(DebrisSlots);
		}

		void OnRemoved(const int64 Id, const FAcModelInfo& Model, TConstArrayView<FTransform> /*PartWorld*/)
		{
			const bool bOurs = Rigs.ContainsByPredicate([&Model](const FRig& R) { return R.Model == &Model; });
			if (!bOurs) return;
			UAcWorldRenderer* R = Effects ? Effects->Renderer() : nullptr;
			if (!R) return;
			// Still listed while `OnRemoved` runs: its last drawn pose.
			R->ForEachObject([&](const int64 O, const bool bUnit, const FAcModelInfo& M, const FAcPose& Pose,
				TConstArrayView<FTransform>)
			{
				if (O != Id || !bUnit || &M != &Model) return;
				FKept& K = Kept.Add(Id);
				K.Model = &M;
				K.Pose = Pose;
				K.bHidden = Pose.bHidden;
			});
		}

		void Fall(const EFall Kind, const int64 Unit, const int32 Team, const ac::Vec2 At, const FKept& K, const double Time)
		{
			FRig* Rig = Rigs.FindByPredicate([&K](const FRig& R) { return R.Model == K.Model; });
			if (!Rig || K.Pose.Local.Num() != K.Model->Parts.Num()) return;
			const double Y = Effects->GroundZ(At) / Cm;

			// The body takes the oldest slot of its kind's ring.
			const int32 S = Rig->Cursor;
			Rig->Cursor = (Rig->Cursor + 1) % Rig->Bodies.Num();
			FLying& L = Rig->Bodies[S];
			L.Body = Start(Kind, Unit, *K.Model, K.Pose);
			L.Team = Team;
			L.Start = Time;
			L.bLive = true;
			PlaceBody(*Rig, S, Time);

			const double U = double(Unit);
			FAcDecalRequest Blood;
			Blood.Time = Time;
			switch (Kind)
			{
			case EFall::Ranger:
				Blood.At = AcSpace::ToWorld(At + ac::Vec2(std::sin(U), std::cos(U)) * 0.15, Y);
				Blood.Radius = 0.42 * Cm;
				Blood.Color = FLinearColor(0.28f, 0.02f, 0.02f, 1.f);
				Blood.Life = 14;
				break;
			case EFall::Juggernaut:
			{
				Blood.At = AcSpace::ToWorld(At + ac::Vec2(std::sin(U), std::cos(U)) * 0.2, Y);
				Blood.Radius = 0.55 * Cm;
				Blood.Color = FLinearColor(0.28f, 0.02f, 0.02f, 1.f);
				Blood.Life = 16;
				// It lands with a puff of dust behind it (`back` of its heading).
				const double Heading = AcSpace::HeadingFromYaw(L.Body.Body.Rotator().Yaw);
				const ac::Vec2 Back(-std::cos(Heading), -std::sin(Heading));
				const FVector Dust = AcSpace::ToWorld(At + Back * 0.7, Y + 0.05);
				TWeakObjectPtr<UAcEffects> Weak(Effects);
				Effects->After(0.5, Time, [Weak, Dust, Time]
				{
					if (UAcEffects* E = Weak.Get()) E->DustPuff(Dust, 0.45, Time + 0.5);
				});
				break;
			}
			case EFall::Comet:
				// The jetpack blows.
				Effects->Explosion(AcSpace::ToWorld(At, Y + 0.9), 0.45, Time);
				Blood.At = AcSpace::ToWorld(At, Y);
				Blood.Radius = 0.45 * Cm;
				Blood.Color = FLinearColor(0.22f, 0.03f, 0.02f, 1.f);
				Blood.Life = 14;
				break;
			}
			Effects->Decal(Blood);
			++Falls;
		}

		void PlaceBody(FRig& Rig, const int32 S, const double Time)
		{
			FLying& L = Rig.Bodies[S];
			// Swift's tick: age clamped at 0, gone at the end of its life.
			const double Sec = FMath::Max(0.0, Time - L.Start);
			if (Sec >= L.Body.Life)
			{
				HideBody(Rig, S);
				return;
			}
			const float Opacity = float(Pose(L.Body, Sec, Rig.World, Rig.Visible));
			for (int32 K = 0; K < Rig.Pools.Num(); ++K)
			{
				FAcEffectPool& Pool = *Rig.Pools[K];
				const int32 P = Rig.Part[K];
				if (!Rig.Visible[P])
				{
					Pool.Hide(S);
					continue;
				}
				Pool.Set(S, Rig.World[P]);
				const float E = MeshEmission(L.Body, P, Rig.Mesh[K]);
				Pool.SetCustom(S, AcModelData::TeamIndex, float(L.Team));
				Pool.SetCustom(S, AcModelData::EmissionScale, Rig.Dither[K] ? E : E * Opacity);
				Pool.SetCustom(S, AcModelData::Fade, Rig.Dither[K] ? 1.f - Opacity : 0.f);
			}
		}

		void HideBody(FRig& Rig, const int32 S)
		{
			Rig.Bodies[S].bLive = false;
			for (FAcEffectPool* Pool : Rig.Pools) Pool->Hide(S);
		}

		void UpdateRig(FRig& Rig, const double Time)
		{
			for (int32 S = 0; S < Rig.Bodies.Num(); ++S)
			{
				if (Rig.Bodies[S].bLive) PlaceBody(Rig, S, Time);
			}
		}

		void PlaceChunk(const int32 S, const double Time)
		{
			FChunk& C = Chunks[S];
			const double Life = 2.8;
			const double Age = FMath::Max(0.0, (Time - C.Start) / Life);
			if (Age >= 1)
			{
				C.bLive = false;
				DebrisPool->Hide(S);
				return;
			}
			const double Sec = Age * Life;
			// Ballistic until it hits the ground, then a short skid.
			const double Land = (C.V.Y + std::sqrt(C.V.Y * C.V.Y + 2 * 9.8 * FMath::Max(0.0, C.P.Y - C.Ground))) / 9.8;
			const double T = FMath::Min(Sec, Land);
			const double Skid = FMath::Max(0.0, Sec - Land);
			const double Slide = 0.35 * (1 - std::exp(-Skid * 4));
			const double X = C.P.X + C.V.X * (T + Slide), Z = C.P.Z + C.V.Z * (T + Slide);
			const double Y = FMath::Max(C.Ground + 0.03, C.P.Y + C.V.Y * T - 4.9 * T * T);
			const double Spin = T + 0.2 * Slide;
			const FQuat Q = SkEuler(C.Spin * Spin);
			// The engine cube is 1 m (1 cell) a side, centred: scale = size.
			DebrisPool->Set(S, FTransform(AcSpace::QuatFromSceneKit(Q.X, Q.Y, Q.Z, Q.W), AcSpace::FromSceneKit(X, Y, Z),
				AcSpace::AxesFromSceneKit(C.Size.X, C.Size.Y, C.Size.Z)));
			DebrisPool->SetCustom(S, AcModelData::Fade, float(Smoothstep(0.75, 1, Age)));
		}

		void UpdateDebris(const double Time)
		{
			if (!DebrisPool) return;
			for (int32 S = 0; S < Chunks.Num(); ++S)
			{
				if (Chunks[S].bLive) PlaceChunk(S, Time);
			}
		}

		/// `-AcDebrisDemo` (dev): a shell's debris (5 chunks, size 0.6) at the
		/// staged fight's middle (`-AcStageFight`) every 0.4 s, so a still
		/// shows chunks rising, landing and fading.
		void DebrisDemo(const double Time)
		{
			if (Time < NextDemo) return;
			NextDemo = Time + 0.4;
			UAcWorldRenderer* R = Effects->Renderer();
			const TOptional<FVector2D> C = R ? R->StagedFightCentre() : TOptional<FVector2D>();
			if (!C) return;
			const ac::Vec2 P(C->X, C->Y);
			FAcDebrisRequest D;
			D.GroundZ = Effects->GroundZ(P);
			D.At = AcSpace::ToWorld(P, D.GroundZ / Cm + 0.15);
			D.Count = 5;
			D.Size = 0.6;
			D.Time = Time;
			D.Seed = int32(Time * 100);
			HandleDebris(D);
		}

		static constexpr int32 DebrisSlots = 160;
		bool bDebrisDemo = false;
		double NextDemo = 0.0;

		UAcEffects* Effects = nullptr;
		TArray<FRig> Rigs;
		TMap<int64, FKept> Kept;
		FDelegateHandle RemovedHandle;
		FAcEffectPool* DebrisPool = nullptr;
		TArray<FChunk> Chunks;
		int32 Falls = 0;
		int32 DebrisThrown = 0;
	};

	FAcEffectsExtensionRegistration Registration(TEXT("deaths"), 130,
		[] { return TUniquePtr<FAcEffectsExtension>(new FAcDeaths); });
}
