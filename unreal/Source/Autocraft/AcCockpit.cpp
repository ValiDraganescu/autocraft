#include "AcCockpit.h"

#include "AcCockpitKinds.h"
#include "AcEffects.h"
#include "AcEffectsBursts.h"
#include "AcLog.h"
#include "AcModelCatalog.h"
#include "AcNewKinds.h"
#include "AcParticles.h"
#include "AcPilotPawn.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"
#include "AcWorldRenderer.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include "Pilot.h"
#include "Rules.h"
#include "Simulation.h"
#include "TerrainField.h"

namespace
{
	TAutoConsoleVariable<int32> CVarCockpit(TEXT("ac.Cockpit"), 1, TEXT("Draw the first-person cockpits (E5)."));
	TAutoConsoleVariable<float> CVarScale(TEXT("ac.CockpitScale"), 0.5f,
		TEXT("First-person scale of the cockpits toward the eye (1: as Swift, may cut into walls; smaller keeps them "
		     "clear of the world). The nearest rim sits 10 cm out, the near plane at 4 cm: keep it above 0.4."));
	TAutoConsoleVariable<float> CVarWorldLightGain(TEXT("ac.CockpitWorldLightGain"), 1500.f,
		TEXT("Gain on the cockpit lights' world twins (where the light really is: the drill's weld light on the crystal)."));
	TAutoConsoleVariable<float> CVarLightGain(TEXT("ac.CockpitLightGain"), 1500.f,
		TEXT("A cockpit light's SceneKit intensity × 2.9/1000 × this, unitless (ac.LampGain's scale; set by eye "
		     "against Swift's lit gauntlets and mask rim: a hand's-width light needs far more than a night lamp)."));

	/// Command-line overrides for stills.
	struct FStills
	{
		TOptional<double> Since, Jump, Anchor;
		bool bMinigun = false, bShield = false, bHeal = false, bMining = false;
		FStills()
		{
			const TCHAR* Cmd = FCommandLine::Get();
			double V = 0;
			if (FParse::Value(Cmd, TEXT("AcCockpitSince="), V)) Since = V;
			if (FParse::Value(Cmd, TEXT("AcCockpitJump="), V)) Jump = V;
			if (FParse::Value(Cmd, TEXT("AcCockpitAnchor="), V)) Anchor = V;
			FString Arms;
			if (FParse::Value(Cmd, TEXT("AcCockpitArms="), Arms, false))
			{
				bMinigun = Arms.Contains(TEXT("minigun"));
				bShield = Arms.Contains(TEXT("shield"));
			}
			bHeal = FParse::Param(Cmd, TEXT("AcCockpitHeal"));
			bMining = FParse::Param(Cmd, TEXT("AcCockpitMining"));
		}
	};
	const FStills& Stills()
	{
		static const FStills S;
		return S;
	}

	/// Unreal mount frame (x ahead, y right, z up) from the cockpit's
	/// (SceneKit camera axes through AcSpace: x right, −y ahead, z up).
	const FRotator CockpitToMount(0, 90, 0);
}

void FAcCockpitPose::Fold()
{
	const int32 N = Local.Num();
	Root.SetNum(N);
	for (int32 I = 0; I < N; ++I)
	{
		const int32 Parent = Model->Parts[I].Parent;
		Root[I] = Parent == INDEX_NONE ? Local[I] : Local[I] * Root[Parent];
		if (Parent != INDEX_NONE && !Visible[Parent]) Visible[I] = false;
	}
}

UAcCockpitSubsystem* UAcCockpitSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UAcCockpitSubsystem>() : nullptr;
}

bool UAcCockpitSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UAcCockpitSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(&World))
	{
		// After the pilot pawn's `Hud`-stage follow: the camera is placed.
		FrameHandle = Sim->AddFrameListener(EAcFrameStage::Other,
			FAcFrameEvent::FDelegate::CreateUObject(this, &UAcCockpitSubsystem::OnFrame));
	}
}

void UAcCockpitSubsystem::Deinitialize()
{
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(GetWorld()))
	{
		if (FrameHandle.IsValid()) Sim->RemoveFrameListener(EAcFrameStage::Other, FrameHandle);
	}
	Clear();
	Super::Deinitialize();
}

void UAcCockpitSubsystem::Clear()
{
	for (UStaticMeshComponent* C : CompRefs)
	{
		if (IsValid(C)) C->DestroyComponent();
	}
	for (UPointLightComponent* L : Lights)
	{
		if (IsValid(L)) L->DestroyComponent();
	}
	if (IsValid(Frame)) Frame->DestroyComponent();
	CompRefs.Reset();
	Comps.Reset();
	Lights.Reset();
	Frame = nullptr;
	Kind.Reset();
	CockpitModel = nullptr;
	UnitId = INDEX_NONE;
	State.reset();
	MuzzlesWorld.Reset();
	Posed = FAcCockpitPose();
}

void UAcCockpitSubsystem::Build(const ac::UnitKind InKind, const int64 Owner)
{
	Clear();
	AAcPilotPawn* Pawn = AAcPilotPawn::Find(GetWorld());
	if (!Pawn || !Pawn->CockpitMount()) return;
	// A new kind without a cockpit model of its own rides in its stand-in's (AcNewKinds.h).
	const FName Name(*FString::Printf(TEXT("cockpit_%s_blue"), AcPose::ModelBase(AcNewKinds::BorrowedCockpit(InKind))));
	const FAcModelInfo* Model = FAcModelCatalog::Get().Find(Name);
	if (!Model)
	{
		UE_LOG(LogAutocraft, Warning, TEXT("cockpit: no model %s in the catalog"), *Name.ToString());
		return;
	}
	Kind = InKind;
	CockpitModel = Model;
	bLoggedMuzzles = false;
	State = AcCockpitKinds::Make(InKind, *Model);

	// Lamp materials: the Dropship's lit energy bars (lib.emissive), the
	// cargo lamps (glowOrange, as the Prospector cockpit's), the Kestrel's
	// spent rocket lamps (the Dropship cockpit's `unlit`).
	AcCockpitKinds::FLamps Lamps;
	if (UAcEffects* Fx = UAcEffects::Get(this))
	{
		Lamps.EnergyLit = Fx->EmissiveMaterial(FLinearColor(0.45f, 1.f, 0.5f), 1.6);
	}
	const FAcModelCatalog& Catalog = FAcModelCatalog::Get();
	if (const FAcModelMesh* M = Catalog.FindMesh(TEXT("cockpit_prospector_blue"), TEXT("fork_root"), TEXT("glowOrange")))
	{
		Lamps.CargoLit = M->LoadMaterial();
	}
	if (const FAcModelMesh* M = Catalog.FindMesh(TEXT("cockpit_dropship_blue"), TEXT("energy_0"), TEXT("unlit")))
	{
		Lamps.Unlit = M->LoadMaterial();
	}
	AcCockpitKinds::SetLamps(*State, Lamps);

	Frame = NewObject<USceneComponent>(Pawn, TEXT("CockpitFrame"));
	Frame->SetupAttachment(Pawn->CockpitMount());
	Frame->SetRelativeRotation(CockpitToMount);
	Frame->RegisterComponent();

	for (int32 P = 0; P < Model->Parts.Num(); ++P)
	{
		const FAcModelPart& Part = Model->Parts[P];
		for (int32 K = 0; K < Part.Meshes.Num(); ++K)
		{
			const FAcModelMesh& Mesh = Part.Meshes[K];
			UStaticMesh* SM = Mesh.LoadMesh();
			if (!SM) continue;
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Pawn);
			C->SetupAttachment(Frame);
			C->SetMobility(EComponentMobility::Movable);
			C->SetStaticMesh(SM);
			UMaterialInterface* Mat = Mesh.LoadMaterial();
			if (Mat) C->SetMaterial(0, Mat);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetGenerateOverlapEvents(false);
			C->SetCastShadow(false);
			C->SetForceDisableNanite(true);
			C->bReceivesDecals = false;
			C->bNeverDistanceCull = true;
			C->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
			C->SetCustomPrimitiveDataFloat(AcModelData::TeamIndex, float(FMath::Clamp<int64>(Owner, 0, 7)));
			C->SetCustomPrimitiveDataFloat(AcModelData::EmissionScale, 1.f);
			C->SetCustomPrimitiveDataFloat(AcModelData::Fade, 0.f);
			C->SetCustomPrimitiveDataFloat(AcModelData::Char, 0.f);
			C->RegisterComponent();
			FMeshComp MC;
			MC.Part = P;
			MC.Mesh = K;
			MC.Component = C;
			MC.Material = Mat;
			Comps.Add(MC);
			CompRefs.Add(C);
		}
	}
	UE_LOG(LogAutocraft, Log, TEXT("cockpit: %s, %d meshes"), *Name.ToString(), Comps.Num());
}

FAcCockpitCues UAcCockpitSubsystem::Cues(const AAcPilotPawn& Pawn, const ac::Unit& U, const FAcFrame& F)
{
	FAcCockpitCues C;
	// The render clock, as the renderer's poses and `LastShot` (B1/B6).
	C.Time = F.Clock;
	C.Dt = LastTime >= 0 ? FMath::Clamp(F.Clock - LastTime, 0.0, 0.1) : 0.0;
	LastDt = C.Dt;
	LastTime = F.Clock;
	const FAcPilotCamera& Eye = const_cast<AAcPilotPawn&>(Pawn).Eye();
	C.Turn = Eye.Turn();
	C.Speed = Eye.Speed();
	C.Pitch = Eye.Pitch;
	UAcWorldRenderer* R = UAcWorldRenderer::Get(this);
	if (const FAcUnitMemory* M = R ? R->Memory(U.id) : nullptr) C.Shot = M->LastShot;
	if (Stills().Since) C.Shot = F.Clock - *Stills().Since;
	C.Since = C.Shot ? F.Clock - *C.Shot : 100.0;
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	const ac::Simulation& S = const_cast<UAcSimSubsystem*>(Sim)->Simulation();
	C.Slots = int32(S.slotsUsed(U));
	C.Room = int32(S.cargoSlots(U));
	C.bMinigun = U.kind == ac::UnitKind::ranger && (S.has(U.owner, ac::Upgrade::minigun) || Stills().bMinigun);
	C.bShield = U.kind == ac::UnitKind::ranger && (S.has(U.owner, ac::Upgrade::aegisShield) || Stills().bShield);
	if (Pawn.Crosshair()) C.Aim = Pawn.Crosshair()->Point;
	// A Dropship's patient: what it heals (`crown`: 0.85 of its height).
	if (U.kind == ac::UnitKind::dropship)
	{
		TOptional<int64> Patient;
		if (U.task == ac::Unit::Task::attacking && U.target) Patient = *U.target;
		if (!Patient && Stills().bHeal && Pawn.Crosshair() && Pawn.Crosshair()->Target != INDEX_NONE)
		{
			Patient = Pawn.Crosshair()->Target;
		}
		if (Patient && Field)
		{
			for (const ac::Unit& V : S.state.units)
			{
				if (V.id != *Patient) continue;
				double Top = 0.9;
				if (const FAcModelInfo* Model = R ? R->ModelOf(V.id) : nullptr)
				{
					Top = AcSpace::ToCells(Model->Bounds.Max.Z) * (Model->Parts.Num() ? Model->Parts[0].Rest.GetScale3D().Z : 1.0);
				}
				C.BeamTo = AcSpace::ToWorld(V.position, Field->height(V.position) + 0.85 * Top);
			}
		}
	}
	if (const UGameViewportClient* VC = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		FVector2D Size;
		VC->GetViewportSize(Size);
		if (Size.Y > 0) C.Aspect = Size.X / Size.Y;
	}
	return C;
}

void UAcCockpitSubsystem::OnFrame(const FAcFrame& F)
{
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	AAcPilotPawn* Pawn = AAcPilotPawn::Find(GetWorld());
	if (!Sim || !Sim->IsRunning() || !Pawn || !Pawn->Driving())
	{
		if (Kind) Clear();
		return;
	}
	std::optional<ac::Unit> Found = Sim->Simulation().pilotUnit();
	if (!Found || Found->id != Pawn->Driven())
	{
		if (Kind) Clear();
		return;
	}
	ac::Unit U = *Found;
	// As the view follows it: a turret's look is the mouse's.
	if (ac::Pilot::steers(U.kind) && ac::Pilot::turretRate(U.kind) && !Pawn->Eye().bThirdPerson) U.aim = Pawn->Yaw();
	if (Stills().Anchor && U.kind == ac::UnitKind::longbow) U.anchor = *Stills().Anchor;
	if (!Kind || *Kind != U.kind || UnitId != U.id)
	{
		Build(U.kind, U.owner);
		UnitId = U.id;
		LastTime = -1.0;
		if (!Kind) return;
	}
	if (FieldMap != &Sim->Map() || !Field)
	{
		Field = std::make_unique<ac::TerrainField>(Sim->Map());
		FieldMap = &Sim->Map();
	}

	const FAcCockpitCues Cue = Cues(*Pawn, U, F);
	if (!State || !CockpitModel) return;
	const FAcModelInfo& Model = *CockpitModel;

	// The rest pose, then the kind's.
	FAcCockpitPose& P = Posed;
	P.Model = &Model;
	P.Local.SetNum(Model.Parts.Num());
	P.Visible.SetNum(Model.Parts.Num());
	P.Emission.SetNum(Model.Parts.Num());
	for (int32 I = 0; I < Model.Parts.Num(); ++I)
	{
		P.Local[I] = Model.Parts[I].Rest;
		P.Visible[I] = !Model.Parts[I].bHidden;
		P.Emission[I] = Model.Parts[I].Opacity < 1.f ? Model.Parts[I].Opacity : 1.f;
	}
	P.MeshEmission.Reset();
	P.Swaps.Reset();
	P.Lights.Reset();
	P.Emitters.Reset();
	P.Muzzles.Reset();
	P.Bite.Reset();
	P.Lean = FAcViewLean();
	P.bHidden = CVarCockpit.GetValueOnGameThread() == 0 || Pawn->Eye().bThirdPerson || Pawn->InDive() || U.task == ac::Unit::Task::inDerrick;

	const FTransform FrameWorld = Frame ? Frame->GetComponentTransform() : FTransform::Identity;
	UAcWorldRenderer* R = UAcWorldRenderer::Get(this);
	FAcCockpitContext C{U, Cue, Sim->Simulation(), *Field, R ? R->Memory(U.id) : nullptr};
	if (Cue.Aim) C.AimRoot = FrameWorld.InverseTransformPosition(*Cue.Aim);
	if (Cue.BeamTo) C.BeamRoot = FrameWorld.InverseTransformPosition(*Cue.BeamTo);
	C.Jump = Stills().Jump ? TOptional<double>(*Stills().Jump) : (U.jump() ? TOptional<double>(*U.jump()) : TOptional<double>());
	C.bMining = Stills().bMining;
	AcCockpitKinds::Pose(*State, C, P);

	// `fitVisor`: the helmet's rim widened on a view wider than 16:10.
	if (const int32* Visor = Model.PartIndex.Find(TEXT("visor")))
	{
		FTransform& L = P.Local[*Visor];
		const FVector S = Model.Parts[*Visor].Rest.GetScale3D();
		L.SetScale3D(FVector(S.X * FMath::Max(1.0, Cue.Aspect / 1.6), S.Y, S.Z));
	}
	P.Fold();
	Apply(*Pawn);

	// The view's lean, applied by the next frame's follow.
	Pawn->Eye().SetLean(P.Lean, U.stats().bio, F.Time);

	// The muzzles into the effects (the rounds leave from the cockpit's guns).
	MuzzlesWorld.Reset();
	if (!P.bHidden)
	{
		for (const int32 I : P.Muzzles)
		{
			if (I != INDEX_NONE) MuzzlesWorld.Add(FrameWorld.TransformPosition(P.Root[I].GetTranslation()));
		}
		if (!bLoggedMuzzles && MuzzlesWorld.Num())
		{
			bLoggedMuzzles = true;
			const FVector Eye = Frame ? Frame->GetComponentLocation() : FVector::ZeroVector;
			UE_LOG(LogAutocraft, Log, TEXT("cockpit: %d muzzles, the first %.0f cm from the eye"), MuzzlesWorld.Num(),
				FVector::Dist(Eye, MuzzlesWorld[0]));
		}
		if (UAcEffects* Fx = UAcEffects::Get(this))
		{
			FAcPilotFire Fire = Fx->Pilot();
			Fire.Muzzles = MuzzlesWorld;
			Fx->SetPilot(Fire);
			// A crystal bite at the fork's load.
			if (P.Bite) Fx->CrystalBite(FrameWorld.TransformPosition(*P.Bite), F.Time);
		}
	}
}

void UAcCockpitSubsystem::Apply(AAcPilotPawn& Pawn)
{
	const FAcCockpitPose& P = Posed;
	const float Scale = FMath::Clamp(CVarScale.GetValueOnGameThread(), 0.05f, 1.f);
	if (UCameraComponent* Cam = Pawn.Camera())
	{
		// The cockpit drawn with the scene's field of view, pulled toward the eye.
		Cam->SetEnableFirstPersonFieldOfView(true);
		Cam->SetFirstPersonFieldOfView(Cam->FieldOfView);
		Cam->SetEnableFirstPersonScale(Scale < 1.f);
		Cam->SetFirstPersonScale(Scale);
	}
	for (FMeshComp& MC : Comps)
	{
		UStaticMeshComponent* C = MC.Component;
		if (!IsValid(C)) continue;
		const bool bShow = !P.bHidden && P.Visible[MC.Part];
		if (bShow != MC.bShown)
		{
			C->SetVisibility(bShow);
			MC.bShown = bShow;
		}
		if (!bShow) continue;
		C->SetRelativeTransform(P.Root[MC.Part]);
		float E = P.Emission[MC.Part];
		const FName Mat = P.Model->Parts[MC.Part].Meshes[MC.Mesh].Material;
		for (const FAcPose::FMeshEmission& ME : P.MeshEmission)
		{
			if (ME.Part == MC.Part && ME.Material == Mat) E = ME.Value;
		}
		if (E != MC.LastEmission)
		{
			C->SetCustomPrimitiveDataFloat(AcModelData::EmissionScale, E);
			MC.LastEmission = E;
		}
		UMaterialInterface* Want = MC.Material;
		for (const FAcCockpitPose::FMaterialSwap& Sw : P.Swaps)
		{
			if (Sw.Part == MC.Part) Want = Sw.Material;
		}
		if (C->GetMaterial(0) != Want) C->SetMaterial(0, Want);
	}

	// Lights in the scaled space the cockpit is drawn in.
	const float Gain = CVarLightGain.GetValueOnGameThread() * 2.9f / 1000.f;
	// Each light twice when the cockpit is pulled in: once in the scaled
	// space (the cockpit's own parts), once where it really is, for the world
	// (the drill's weld light on the struck crystal, Swift's `weldLight`).
	const int32 Lit = P.bHidden ? 0 : P.Lights.Num();
	const bool bTwins = Scale < 1.f;
	const int32 Want = bTwins ? Lit * 2 : Lit;
	// The world twins: `ac.CockpitWorldLightGain` (the struck crystal lit gold, as Swift).
	const float WorldGain = CVarWorldLightGain.GetValueOnGameThread() * 2.9f / 1000.f;
	while (Lights.Num() < Want && Frame)
	{
		UPointLightComponent* L = NewObject<UPointLightComponent>(Frame->GetOwner());
		L->SetupAttachment(Frame);
		L->SetMobility(EComponentMobility::Movable);
		L->SetIntensityUnits(ELightUnits::Unitless);
		L->SetUseInverseSquaredFalloff(false);
		L->SetLightFalloffExponent(3.f);
		L->SetCastShadows(false);
		L->SetSourceRadius(0.5f);
		L->RegisterComponent();
		Lights.Add(L);
	}
	for (int32 K = 0; K < Lights.Num(); ++K)
	{
		UPointLightComponent* L = Lights[K];
		if (!IsValid(L)) continue;
		const bool bOn = K < Want;
		L->SetVisibility(bOn);
		if (!bOn) continue;
		const bool bWorld = K >= Lit;
		const FAcCockpitPose::FLight& W = P.Lights[bWorld ? K - Lit : K];
		const float At = bWorld ? 1.f : Scale;
		L->SetRelativeLocation(W.At * At);
		L->SetLightColor(W.Color);
		L->SetIntensity(W.Intensity * (bWorld ? WorldGain : Gain));
		L->SetAttenuationRadius(float(AcSpace::ToCm(W.Reach)) * At);
	}

	// Particles at the cockpit's emitters, in the same scaled space.
	if (!P.bHidden && P.Emitters.Num() && Frame)
	{
		UAcEffects* Fx = UAcEffects::Get(this);
		if (FAcParticles* Particles = Fx ? AcEffectsBursts::ParticlesOf(Fx) : nullptr)
		{
			const FTransform FrameWorld = Frame->GetComponentTransform();
			for (int32 K = 0; K < P.Emitters.Num(); ++K)
			{
				const FAcCockpitPose::FEmitter& E = P.Emitters[K];
				FTransform At = E.Frame;
				At.SetTranslation(At.GetTranslation() * Scale);
				At.SetScale3D(FVector::OneVector);
				FAcEmit Emit;
				if (E.Size > 0) Emit.Size = E.Size * Scale;
				if (E.Speed > 0) Emit.Speed = E.Speed * Scale;
				Particles->Stream(EAcParticle(E.Kind), FAcParticles::KeyOf(UnitId, 200 + K), At * FrameWorld, E.Rate,
					LastTime, LastDt, Emit);
			}
		}
	}
}
