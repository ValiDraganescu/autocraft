#include "AcLamps.h"

#include "AcDaylight.h"
#include "AcEffects.h"
#include "AcEffectsExtension.h"
#include "AcLog.h"
#include "AcModelCatalog.h"
#include "AcPose.h"
#include "AcPoseInfantry.h"
#include "AcPoseVehicles.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"
#include "AcWorldRenderer.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

namespace
{
	TAutoConsoleVariable<int32> CVarLamps(TEXT("ac.Lamps"), 96,
		TEXT("How many lamps (night lamps, muzzles, blasts, drill and flame lights) get a real light at once; the strongest in view win."));
	TAutoConsoleVariable<float> CVarGain(TEXT("ac.LampGain"), 100.0f,
		TEXT("Scale on every lamp's brightness (SceneKit intensity I -> I * 2.9/1000 * gain)."));
	TAutoConsoleVariable<float> CVarFalloff(TEXT("ac.LampFalloff"), 3.0f,
		TEXT("The lamps' falloff exponent over their reach (3: the Swift night shots' pools)."));
	TAutoConsoleVariable<int32> CVarShadows(TEXT("ac.LampShadows"), 0,
		TEXT("Lamps cast shadows (1; MegaLights is off for the project, turn it and HW ray tracing on first) or not (0, as Swift)."));
	TAutoConsoleVariable<float> CVarBeaconGain(TEXT("ac.BeaconLightGain"), 40.0f,
		TEXT("Extra gain on the red beacon lights (B6), on top of ac.LampGain: the hulls under them are dark metal, which a point light of the Swift intensity only tinted; 40 reddens the Citadel dome as in Swift (top-night-beacon)."));
	TAutoConsoleVariable<float> CVarDrillGain(TEXT("ac.DrillLightGain"), 30.0f,
		TEXT("Extra gain on the drill's weld light: on the near-black ore it needs far more than the lamps to glow gold as Swift's."));
	TAutoConsoleVariable<float> CVarDrillReach(TEXT("ac.DrillLightReach"), 1.8f,
		TEXT("The drill weld light's reach, cells (Swift 1.3; 1.8 with gain 30 gives Swift's top-down glow, day and night: `top`, `top-night`)."));
	TAutoConsoleVariable<float> CVarStats(TEXT("ac.LampStats"), 0.0f,
		TEXT("Log a `lamps:` line every N seconds (0: off)."));

	/// SceneKit intensity 1000 = 1.0 = this many lux at the daylight's exposure.
	constexpr double UnrealPerSceneKit = 2.9 / 1000.0;

	enum ELamp : uint8
	{
		Headlamp,
		Beacon,
		Warn,
		Pilot,
		Gunfire,
		Blast,
	};

	struct FProto
	{
		FLinearColor Colour;
		double Intensity, Reach;
	};

	FLinearColor Lin(double R, double G, double B)
	{
		return FLinearColor::FromSRGBColor(FLinearColor(float(R), float(G), float(B)).ToFColor(false));
	}

	const FProto& Proto(const uint8 Kind)
	{
		// NightLamps.swift:22-27 and `update(dark:)` :63.
		static const FProto Protos[] = {
			{Lin(1, 0.95, 0.86), 90, 6},
			{Lin(1, 0.12, 0.08), 20, 1.4},
			{Lin(1, 0.5, 0.12), 40, 1.5},
			{Lin(0.35, 0.6, 1), 25, 1.4},
			{Lin(1, 0.75, 0.4), 140, 3.2},
			{Lin(1, 0.7, 0.35), 420, 7},
		};
		return Protos[Kind];
	}

	const FLinearColor& PoolColour()
	{
		static const FLinearColor C = Lin(1, 0.82, 0.5);
		return C;
	}

	/// The explosion's light, `Effects.explosion` :700.
	const FLinearColor& BlastColour()
	{
		static const FLinearColor C = Lin(1, 0.62, 0.3);
		return C;
	}

	/// The Prospector's drill weld (Models+ProspectorHull.swift:196) and the
	/// Firefly's flame (Models+Firefly.swift:373).
	const FLinearColor& DrillColour()
	{
		static const FLinearColor C = Lin(1, 0.7, 0.35);
		return C;
	}
	const FLinearColor& FlameColour()
	{
		static const FLinearColor C = Lin(1, 0.55, 0.2);
		return C;
	}

	int32 PartOf(const FAcModelInfo& M, const TCHAR* Name)
	{
		const int32* I = M.PartIndex.Find(FName(Name));
		return I ? *I : INDEX_NONE;
	}

	/// Shown this frame: the part and every parent visible.
	bool Shown(const FAcModelInfo& M, const FAcPose& P, int32 Part)
	{
		if (P.bHidden) return false;
		while (Part != INDEX_NONE)
		{
			if (!P.Visible.IsValidIndex(Part) || !P.Visible[Part]) return false;
			Part = M.Parts[Part].Parent;
		}
		return true;
	}

	/// A glow material's SceneKit intensity as the pose set it (the part's
	/// own emission, or the mesh's when merged with others).
	double GlowOf(const FAcModelInfo& M, const FAcPose& P, const FName Material, const double Exported)
	{
		for (int32 I = 0; I < M.Parts.Num(); ++I)
		{
			for (const FAcModelMesh& Mesh : M.Parts[I].Meshes)
			{
				if (Mesh.Material != Material) continue;
				if (M.Parts[I].Meshes.Num() == 1) return (P.Emission.IsValidIndex(I) ? P.Emission[I] : 1.f) * Exported;
				for (const FAcPose::FMeshEmission& E : P.MeshEmission)
				{
					if (E.Part == I && E.Material == Material) return E.Value * Exported;
				}
				return Exported;
			}
		}
		return 0.0;
	}

	/// Sees explosions for the lamp pool and lets the next extension (C3)
	/// draw them.
	class FAcLampExplosions final : public FAcEffectsExtension
	{
	public:
		virtual void Begin(UAcEffects& InEffects) override { Effects = &InEffects; }
		virtual bool HandleExplosion(const FVector& At, const double Size, const double Time) override
		{
			if (UAcLampPool* Pool = UAcLampPool::Get(Effects.Get())) Pool->Explosion(At, Size, Time);
			return false;
		}

	private:
		TWeakObjectPtr<UAcEffects> Effects;
	};

	FAcEffectsExtensionRegistration LampExtension(TEXT("lamps"), -1000,
		[] { return TUniquePtr<FAcEffectsExtension>(new FAcLampExplosions); });
}

UAcLampPool* UAcLampPool::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UAcLampPool>() : nullptr;
}

bool UAcLampPool::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UAcLampPool::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UAcSimSubsystem>();
}

void UAcLampPool::Deinitialize()
{
	if (UAcSimSubsystem* Sim = GetWorld() ? GetWorld()->GetSubsystem<UAcSimSubsystem>() : nullptr)
	{
		Sim->RemoveFrameListener(EAcFrameStage::Other, FrameHandle);
		Sim->OnGameStarted.Remove(StartedHandle);
	}
	if (UAcWorldRenderer* R = Renderer.Get()) R->OnCue.Remove(CueHandle);
	Super::Deinitialize();
}

void UAcLampPool::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	UAcSimSubsystem* Sim = InWorld.GetSubsystem<UAcSimSubsystem>();
	if (!Sim) return;
	// Last: after the renderer's poses and the effects' explosions.
	FrameHandle = Sim->AddFrameListener(EAcFrameStage::Other, FAcFrameEvent::FDelegate::CreateUObject(this, &UAcLampPool::OnFrame));
	StartedHandle = Sim->OnGameStarted.AddUObject(this, &UAcLampPool::OnGameStarted);
}

void UAcLampPool::OnGameStarted(UAcSimSubsystem&)
{
	Blasts.Reset();
	Drills.Reset();
}

void UAcLampPool::Bind()
{
	if (Renderer.IsValid()) return;
	UAcWorldRenderer* R = UAcWorldRenderer::Get(this);
	if (!R) return;
	Renderer = R;
	CueHandle = R->OnCue.AddUObject(this, &UAcLampPool::OnCue);
}

void UAcLampPool::Explosion(const FVector& At, const double Size, const double Time)
{
	Blasts.Add({At, Size, Time});
}

void UAcLampPool::SetCockpitLamp(const FTransform* InCockpit, const bool bHeadlamp)
{
	if (InCockpit && bHeadlamp) Cockpit = *InCockpit;
	else Cockpit.Reset();
}

void UAcLampPool::OnCue(const int64 Id, const FAcPoseCue& Cue)
{
	if (Cue.What != AcPoseInfantry::CueDrill() || Cue.Value <= 0.f) return;
	FWant W;
	W.At = Cue.At;
	W.Colour = DrillColour();
	W.Intensity = Cue.Value * CVarDrillGain.GetValueOnGameThread();
	W.Reach = CVarDrillReach.GetValueOnGameThread();
	Drills.Add(W);
}

const UAcLampPool::FSockets& UAcLampPool::SocketsOf(const FAcModelInfo& M)
{
	if (const TUniquePtr<FSockets>* Found = Sockets.Find(&M)) return **Found;
	TUniquePtr<FSockets> S = MakeUnique<FSockets>();
	auto Add = [&](const TCHAR* Name, uint8 Kind) {
		const int32 P = PartOf(M, Name);
		if (P != INDEX_NONE) S->Lamps.Add({P, Kind});
		return P != INDEX_NONE;
	};
	auto Numbered = [&](const TCHAR* Name, TArray<int32>& Out) {
		for (int32 K = 0;; ++K)
		{
			const int32 P = PartOf(M, *FString::Printf(TEXT("%s_%d"), Name, K));
			if (P == INDEX_NONE) break;
			Out.Add(P);
		}
	};
	const FString Base = M.Base.ToString();
	const bool bBuilding = M.Category == TEXT("building");
	if (M.Category == TEXT("unit") || bBuilding)
	{
		Add(TEXT("beacon"), Beacon);
		Add(TEXT("flash"), Gunfire);
		Add(TEXT("minigun_flash"), Gunfire);
		TArray<int32> Flashes;
		Numbered(TEXT("flashes"), Flashes);
		for (int32 K = 0; K < Flashes.Num(); ++K)
		{
			// The Longbow's last flash is the anchored gun's: the blast.
			const bool bAnchor = Base == TEXT("longbow") && K == Flashes.Num() - 1;
			S->Lamps.Add({Flashes[K], uint8(bAnchor ? Blast : Gunfire)});
		}
		if (Base == TEXT("longbow"))
		{
			TArray<int32> WarnParts;
			Numbered(TEXT("warnLamps"), WarnParts);
			for (int32 P : WarnParts) S->Lamps.Add({P, uint8(Warn)});
		}
		if (Base == TEXT("firefly"))
		{
			Add(TEXT("pilot"), Pilot);
			S->FlameLight = PartOf(M, TEXT("flameLightNode"));
		}
		if (Base == TEXT("prospector")) Add(TEXT("headlamp"), Headlamp);
	}
	if (bBuilding)
	{
		// NightLamps.fit(_:id:kind:) :116-149: building space, door toward +Z.
		struct FPoolSpec
		{
			const TCHAR* Kind;
			double X, Y, Z, Reach;
			const TCHAR* Glow;
			double Exported;
		};
		static const FPoolSpec Specs[] = {
			{TEXT("citadel"), 0, 2.2, 3.2, 5, TEXT("floodGlow"), 2.0},
			{TEXT("garrison"), 0, 1.5, 2.1, 3.4, TEXT("floodGlow"), 2.0},
			{TEXT("foundry"), 0, 1.4, 2.2, 3.4, TEXT("floodGlow"), 2.0},
			{TEXT("derrick"), 0, 1.6, 2.1, 3.2, TEXT("floodGlow"), 2.0},
			{TEXT("bastion"), 0, 1.3, 1.9, 3, TEXT("slitGlow"), 1.8},
			{TEXT("sentinel"), 0, 0.55, 1.15, 2.4, TEXT("workGlow"), 2.0},
		};
		for (const FPoolSpec& P : Specs)
		{
			if (Base != P.Kind) continue;
			S->bPool = true;
			S->PoolAt = AcSpace::FromSceneKit(P.X, P.Y, P.Z);
			S->PoolReach = P.Reach;
			S->GlowMaterial = FName(P.Glow);
			S->GlowExported = P.Exported;
		}
		if (Base == TEXT("spacedock"))
		{
			Numbered(TEXT("guideLamps"), S->GuideLamps);
			if (S->GuideLamps.Num() > 0)
			{
				// Over the pad, from the middle of its ring of guide lamps.
				FVector Mid = FVector::ZeroVector;
				for (int32 P : S->GuideLamps) Mid += M.RestToModel(P).GetLocation();
				Mid /= double(S->GuideLamps.Num());
				S->bPool = true;
				S->PoolAt = Mid + FVector(0, 0, 70);
				S->PoolReach = 2.6;
			}
		}
	}
	const FSockets& Out = *S;
	Sockets.Add(&M, MoveTemp(S));
	return Out;
}

void UAcLampPool::Gather(const double Time, const double Dark, TArray<FWant>& Want)
{
	UAcWorldRenderer* R = Renderer.Get();
	if (R)
	{
		R->ForEachObject([&](const int64 Id, const bool bUnit, const FAcModelInfo& M, const FAcPose& P,
							 TConstArrayView<FTransform> World) {
			if (P.bHidden || P.bFrozen || World.Num() == 0) return;  // fogged buildings: no lights
			const FSockets& S = SocketsOf(M);
			if (Dark > 0.001)
			{
				for (const FSocket& L : S.Lamps)
				{
					if (!World.IsValidIndex(L.Part) || !Shown(M, P, L.Part)) continue;
					const FProto& Pr = Proto(L.Kind);
					FWant W;
					W.At = World[L.Part].GetLocation();
					W.Colour = Pr.Colour;
					W.Intensity = Pr.Intensity * Dark;
					if (L.Kind == Beacon) W.Intensity *= CVarBeaconGain.GetValueOnGameThread();
					W.Reach = Pr.Reach;
					if (L.Kind == Headlamp)
					{
						W.Toward = World[L.Part].GetRotation().RotateVector(AcSpace::AxesFromSceneKit(1, -0.65, 0)).GetSafeNormal();
						W.Inner = 16;
						W.Outer = 40;
					}
					Want.Add(W);
				}
				if (S.bPool)
				{
					double Glow = 0;
					if (S.GuideLamps.Num() > 0)
					{
						for (int32 G : S.GuideLamps) Glow += P.Emission.IsValidIndex(G) ? P.Emission[G] : 1.0;
						Glow /= double(S.GuideLamps.Num());
					}
					else
					{
						Glow = GlowOf(M, P, S.GlowMaterial, S.GlowExported);
					}
					const double I = Dark * 26 * FMath::Max(0.0, Glow - 0.15);
					if (I > 0.01)
					{
						// The placement: the root's world without its own rest.
						const FTransform Placement = M.Parts[0].Rest.Inverse() * World[0];
						FWant W;
						W.At = Placement.TransformPosition(S.PoolAt);
						W.Colour = PoolColour();
						W.Intensity = I;
						W.Reach = S.PoolReach;
						Want.Add(W);
						if (CVarStats.GetValueOnGameThread() < 0)
							UE_LOG(LogAutocraft, Log, TEXT("lamps: pool %lld %s glow %.2f at %s (root %s)"), Id, *M.Base.ToString(), Glow,
								*W.At.ToString(), *World[0].GetLocation().ToString());
					}
				}
			}
			if (S.FlameLight != INDEX_NONE && World.IsValidIndex(S.FlameLight))
			{
				const FAcUnitMemory* Mem = R->Memory(Id);
				const FAcVehicleFx* Fx = Mem ? AcPoseVehicles::Fx(ac::UnitKind::firefly, *Mem) : nullptr;
				if (Fx && Fx->FlameLight > 0.f)
				{
					FWant W;
					W.At = World[S.FlameLight].GetLocation();
					W.Colour = FlameColour();
					W.Intensity = Fx->FlameLight;
					W.Reach = Fx->FlameLightRange;
					Want.Add(W);
				}
			}
		});
	}
	// The driven Prospector's headlamp on its cockpit: ahead and down.
	if (Cockpit && Dark > 0.001)
	{
		const FProto& Pr = Proto(Headlamp);
		FWant W;
		W.At = Cockpit->TransformPosition(FVector(0, 0, 12));
		W.Toward = Cockpit->GetRotation().RotateVector(FVector(1, 0, -0.4)).GetSafeNormal();
		W.Colour = Pr.Colour;
		W.Intensity = Pr.Intensity * Dark;
		W.Reach = Pr.Reach;
		W.Inner = 16;
		W.Outer = 40;
		W.Score = 1e12;  // always lit
		Want.Add(W);
	}
	Want.Append(Drills);
	Drills.Reset();
	// Explosions: 70·size, gone in 3.2/6 s.
	for (int32 I = Blasts.Num() - 1; I >= 0; --I)
	{
		const FBlast& B = Blasts[I];
		const double Age = Time - B.Time;
		if (Age > 3.2 / 6 + 0.05)
		{
			Blasts.RemoveAtSwap(I);
			continue;
		}
		const double K = Age < 0 ? 0.0 : FMath::Max(0.0, 1 - Age / 3.2 * 6);
		if (K <= 0) continue;
		FWant W;
		W.At = B.At;
		W.Colour = BlastColour();
		W.Intensity = 70 * B.Size * K;
		W.Reach = 3 + 2.5 * B.Size;
		Want.Add(W);
	}
}

void UAcLampPool::OnFrame(const FAcFrame& Frame)
{
	Bind();
	const AAcDaylight* Daylight = AAcDaylight::Find(GetWorld());
	const double Dark = Daylight ? Daylight->GetDark() : 0.0;
	TArray<FWant> Want;
	Want.Reserve(256);
	Gather(Frame.Time, Dark, Want);
	Light(Want);

	const float Every = CVarStats.GetValueOnGameThread();
	const double Now = FPlatformTime::Seconds();
	if (Every > 0 && Now - StatsWall >= Every)
	{
		StatsWall = Now;
		UE_LOG(LogAutocraft, Log, TEXT("lamps: dark %.2f, wanted %d, in view %d, lit %d (budget %d), blasts %d"), Dark, Stats.Wanted,
			Stats.InView, Stats.Lit, CVarLamps.GetValueOnGameThread(), Blasts.Num());
	}
}

ULocalLightComponent* UAcLampPool::Take(const bool bSpot, const int32 Index)
{
	if (!Holder && GetWorld())
	{
		FActorSpawnParameters Params;
		Params.Name = TEXT("AcLamps");
		Params.ObjectFlags |= RF_Transient;
		Holder = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		USceneComponent* Root = NewObject<USceneComponent>(Holder, TEXT("Root"));
		Holder->SetRootComponent(Root);
		Root->RegisterComponent();
	}
	if (!Holder) return nullptr;
	auto Make = [&](UPointLightComponent* L) {
		L->SetMobility(EComponentMobility::Movable);
		L->SetCastShadows(false);
		L->bUseInverseSquaredFalloff = false;
		L->SetIntensityUnits(ELightUnits::Unitless);
		L->SetSourceRadius(4.f);
		L->SetVisibility(false);
		L->SetupAttachment(Holder->GetRootComponent());
		L->RegisterComponent();
		Holder->AddInstanceComponent(L);
	};
	if (bSpot)
	{
		while (Spots.Num() <= Index)
		{
			USpotLightComponent* L = NewObject<USpotLightComponent>(Holder);
			Make(L);
			Spots.Add(L);
		}
		return Spots[Index];
	}
	while (Points.Num() <= Index)
	{
		UPointLightComponent* L = NewObject<UPointLightComponent>(Holder);
		Make(L);
		Points.Add(L);
	}
	return Points[Index];
}

void UAcLampPool::Light(TArray<FWant>& Want)
{
	Stats.Wanted = Want.Num();
	// Cull to the view and score: brightness and size, less toward the edges.
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	int32 SizeX = 0, SizeY = 0;
	if (PC) PC->GetViewportSize(SizeX, SizeY);
	const FVector Eye = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
	const bool bCull = PC && SizeX > 0 && SizeY > 0;
	const FVector2D Mid(SizeX * 0.5, SizeY * 0.5);
	TArray<FWant> Kept;
	Kept.Reserve(Want.Num());
	for (FWant& W : Want)
	{
		if (W.Intensity <= 0.01) continue;
		double Edge = 0;
		if (bCull && W.Score < 1e11)
		{
			const double Near = FVector::Dist(Eye, W.At) / 100.0;
			FVector2D Screen;
			const bool bFront = PC->ProjectWorldLocationToScreen(W.At, Screen, false);
			if (Near > W.Reach + 3)
			{
				if (!bFront) continue;
				// Off screen by more than a third of the view: its reach does
				// not get in.
				const double Mx = SizeX * 0.33, My = SizeY * 0.33;
				if (Screen.X < -Mx || Screen.Y < -My || Screen.X > SizeX + Mx || Screen.Y > SizeY + My) continue;
				Edge = FVector2D::Distance(Screen, Mid) / double(FMath::Max(SizeX, SizeY));
			}
		}
		if (W.Score < 1e11) W.Score = W.Intensity * W.Reach * W.Reach / (1.0 + 4.0 * Edge * Edge);
		Kept.Add(W);
	}
	Stats.InView = Kept.Num();
	Kept.Sort([](const FWant& A, const FWant& B) { return A.Score > B.Score; });
	const int32 N = FMath::Min(Kept.Num(), FMath::Max(0, CVarLamps.GetValueOnGameThread()));
	Stats.Lit = N;

	const float Gain = float(UnrealPerSceneKit * CVarGain.GetValueOnGameThread());
	const float Falloff = FMath::Max(0.5f, CVarFalloff.GetValueOnGameThread());
	const bool bShadows = CVarShadows.GetValueOnGameThread() != 0;
	int32 NP = 0, NS = 0;
	for (int32 I = 0; I < N; ++I)
	{
		const FWant& W = Kept[I];
		const bool bSpot = !W.Toward.IsNearlyZero();
		ULocalLightComponent* L = Take(bSpot, bSpot ? NS++ : NP++);
		if (!L) break;
		if (bSpot)
		{
			USpotLightComponent* S = static_cast<USpotLightComponent*>(L);
			S->SetWorldLocationAndRotation(W.At, W.Toward.Rotation());
			// SceneKit's cone angles are full angles, Unreal's half.
			S->SetInnerConeAngle(float(W.Inner * 0.5));
			S->SetOuterConeAngle(float(W.Outer * 0.5));
		}
		else
		{
			L->SetWorldLocation(W.At);
		}
		L->SetLightColor(W.Colour);
		L->SetIntensity(float(W.Intensity) * Gain);
		L->SetAttenuationRadius(float(W.Reach * 100.0));
		if (UPointLightComponent* P = Cast<UPointLightComponent>(L); P && P->LightFalloffExponent != Falloff)
		{
			P->LightFalloffExponent = Falloff;
			P->MarkRenderStateDirty();
		}
		if (L->CastShadows != bShadows) L->SetCastShadows(bShadows);
		if (!L->IsVisible()) L->SetVisibility(true);
	}
	for (int32 I = NP; I < Points.Num(); ++I)
		if (Points[I]->IsVisible()) Points[I]->SetVisibility(false);
	for (int32 I = NS; I < Spots.Num(); ++I)
		if (Spots[I]->IsVisible()) Spots[I]->SetVisibility(false);
}
