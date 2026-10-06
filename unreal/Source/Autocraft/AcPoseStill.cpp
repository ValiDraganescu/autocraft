#include "AcPoseStill.h"

#include "AcLog.h"
#include "AcModelCatalog.h"
#include "AcModelRow.h"
#include "AcPose.h"
#include "AcShot.h"
#include "AcSpace.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ContentStreaming.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

#include "TerrainField.h"
#include "Types.h"
#include "WindowMaps.h"

namespace
{
	TOptional<ac::UnitKind> KindNamed(const FString& Name)
	{
		for (int32 K = 0; K <= (int32)ac::UnitKind::hailstorm; ++K)
		{
			if (Name == AcPose::ModelBase((ac::UnitKind)K)) return (ac::UnitKind)K;
		}
		return {};
	}

	double Arg(const TCHAR* Key, const double Default)
	{
		double V = Default;
		FParse::Value(FCommandLine::Get(), Key, V);
		return V;
	}
}

AAcPoseStill::AAcPoseStill()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AAcPoseStill::BeginPlay()
{
	Super::BeginPlay();
	FString Spec;
	if (FParse::Value(FCommandLine::Get(), TEXT("AcPoseStill="), Spec)) Build(Spec);
}

void AAcPoseStill::Build(const FString& Spec)
{
	const bool bBusy = Spec.EndsWith(TEXT("-busy"));
	const FString Name = bBusy ? Spec.LeftChop(5) : Spec;
	const TOptional<ac::UnitKind> Kind = KindNamed(Name);
	const FAcModelInfo* Model = FAcModelCatalog::Get().Find(FName(Name + TEXT("_blue")));
	const FAcPoseFn Fn = Kind ? AcPose::Find(*Kind) : nullptr;
	if (!Kind || !Model)
	{
		UE_LOG(LogAutocraft, Error, TEXT("pose still: no unit model %s"), *Name);
		return;
	}

	// The inputs, as the Swift preview passes them (ModelCatalog.swift).
	const bool bHailstorm = *Kind == ac::UnitKind::hailstorm;
	double Time = Arg(TEXT("AcPoseTime="), bHailstorm ? 1.02 : 10.0);
	const double Anchor = Arg(TEXT("AcPoseAnchor="), bBusy ? 1.0 : 0.0);
	const double Aim = Arg(TEXT("AcPoseAim="), bBusy && bHailstorm ? 0.6 : 0.0);
	double ShotAgo = Arg(TEXT("AcPoseShotAgo="), bBusy && bHailstorm ? 0.02 : -1.0);
	double Stride = 0.0;
	const bool bMoving = FParse::Value(FCommandLine::Get(), TEXT("AcPoseStride="), Stride);
	const int32 Frames = FMath::Max(1, int32(Arg(TEXT("AcPoseFrames="), bBusy && bHailstorm ? 12.0 : 1.0)));
	const int32 Team = int32(Arg(TEXT("AcPoseTeam="), 0.0));

	ac::Unit U(1, *Kind, 0, ac::Vec2(0, 0), 0.0, ac::Unit::Task::idle);
	U.stride = Stride;
	U.moving = bMoving;
	U.aim = Aim;
	if (*Kind == ac::UnitKind::longbow)
	{
		U.anchor = Anchor;
		U.anchored = Anchor > 0.5;
	}
	ac::GameState State;
	State.time = Time;
	const ac::MapDefinition Map = ac::WindowMaps::playground();
	const ac::TerrainField Field(Map);
	FAcUnitMemory Memory;
	if (ShotAgo >= 0) Memory.LastShot = Time - ShotAgo;
	FAcPose Pose;
	for (int32 F = 0; F < Frames; ++F)
	{
		FAcPoseContext C{State, &U, nullptr, *Model, Field, Memory, Time, 1.0 / 60.0, false, 0};
		Pose.Reset(*Model);
		if (Fn) Fn(C, Pose);
	}

	// World transforms as the renderer composes them; first at heading 0
	// at the origin (the feet log, as Swift's), then turned for the camera.
	auto Compose = [&](const FTransform& Placement, TArray<FTransform>& World, TArray<bool>& Visible)
	{
		const int32 N = Model->Parts.Num();
		World.SetNum(N);
		Visible = Pose.Visible;
		for (int32 P = 0; P < N; ++P)
		{
			const int32 Parent = Model->Parts[P].Parent;
			World[P] = Pose.Local[P] * (Parent != INDEX_NONE ? World[Parent] : Placement);
			if (Parent != INDEX_NONE) Visible[P] = Visible[P] && Visible[Parent];
		}
	};
	TArray<FTransform> World;
	TArray<bool> Visible;
	const FVector Bob = Pose.Placement.GetTranslation();
	Compose(FTransform(Bob), World, Visible);
	FString Feet;
	for (int32 I = 0; I < 4; ++I)
	{
		if (const int32* Claw = Model->PartIndex.Find(FName(*FString::Printf(TEXT("claws_%d"), I))))
		{
			const FVector Sk = AcSpace::ToSceneKit(World[*Claw].TransformPosition(AcSpace::FromSceneKit(0, -0.6, 0)));
			Feet += FString::Printf(TEXT(" (%.3f %.3f %.3f)"), Sk.X, Sk.Y, Sk.Z);
		}
	}
	UE_LOG(LogAutocraft, Log, TEXT("pose still: %s anchor %.3f stride %.3f time %.2f%s%s"), *Spec, Anchor, Stride, Time,
		Feet.IsEmpty() ? TEXT("") : TEXT(" feet"), *Feet);

	// Swift preview: units turned -(π/2 + 0.6) about SceneKit Y (heading
	// π/2 + 0.6 here).
	Compose(FTransform(AcSpace::QuatFromHeading(UE_DOUBLE_HALF_PI + 0.6), Bob), World, Visible);
	for (int32 P = 0; P < Model->Parts.Num(); ++P)
	{
		const FAcModelPart& Part = Model->Parts[P];
		for (int32 X = 0; X < Part.Meshes.Num(); ++X)
		{
			const FAcModelMesh& Mesh = Part.Meshes[X];
			UStaticMesh* SM = Mesh.LoadMesh();
			if (!SM) continue;
			float Emission = Pose.Emission[P];
			for (const FAcPose::FMeshEmission& Over : Pose.MeshEmission)
			{
				if (Over.Part == P && Over.Material == Mesh.Material) Emission = Over.Value;
			}
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
			C->SetupAttachment(RootComponent);
			C->SetStaticMesh(SM);
			if (UMaterialInterface* Mat = Mesh.LoadMaterial()) C->SetMaterial(0, Mat);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetCustomPrimitiveDataFloat(AcModelData::TeamIndex, float(Team));
			C->SetCustomPrimitiveDataFloat(AcModelData::EmissionScale, Emission);
			C->SetWorldTransform(World[P]);
			C->SetVisibility(Visible[P]);
			C->RegisterComponent();
			Meshes.Add(C);
		}
	}

	// Nothing else in the level: the model row and its floor hidden.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->IsA<AAcModelRow>() || It->ActorHasTag(TEXT("AcFloor"))) It->SetActorHiddenInGame(true);
	}
	// The preview's camera and sun (SceneKit space, cells).
	const double TargetY = bHailstorm ? 0.6 : 0.7;
	const double D = Arg(TEXT("AcPoseDistance="), bHailstorm ? 4.4 : (Anchor > 0 ? 6.8 : 5.6));
	const FVector Target = AcSpace::FromSceneKit(0, TargetY, 0);
	const FVector CamPos = AcSpace::FromSceneKit(0, TargetY + D * 0.45, D);
	const FVector SunPos = AcSpace::FromSceneKit(-4, 8, 6);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Camera = GetWorld()->SpawnActor<ACameraActor>(CamPos, (Target - CamPos).Rotation(), Params);
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		It->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		It->SetActorRotation((Target - SunPos).Rotation());
	}
	if (UAcShotSubsystem* Shot = UAcShotSubsystem::Get(this))
	{
		Shot->Hold();
		bHolding = true;
	}
}

void AAcPoseStill::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Camera) return;
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (PC->GetViewTarget() != Camera) PC->SetViewTarget(Camera);
	}
	// SceneKit's 30° FOV is vertical.
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
		if (Size.X > 0 && Size.Y > 0)
		{
			const double Half = FMath::DegreesToRadians(15.0);
			Camera->GetCameraComponent()->SetFieldOfView(
				float(FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(Half) * Size.X / Size.Y))));
		}
	}
	if (!bHolding) return;
	bool bBusy = false;
#if WITH_EDITOR
	bBusy = (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
		|| FAssetCompilingManager::Get().GetNumRemainingAssets() > 0;
#endif
	IStreamingManager::Get().StreamAllResources(0.0f);
	SettleFrames = bBusy ? 0 : SettleFrames + 1;
	if (SettleFrames > 90)
	{
		if (UAcShotSubsystem* Shot = UAcShotSubsystem::Get(this)) Shot->Release();
		bHolding = false;
	}
}

bool UAcPoseStillSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return FParse::Param(FCommandLine::Get(), TEXT("AcPoseStill")) || FCString::Strifind(FCommandLine::Get(), TEXT("-AcPoseStill="));
}

void UAcPoseStillSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	if (!World.IsGameWorld()) return;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	World.SpawnActor<AAcPoseStill>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
}
