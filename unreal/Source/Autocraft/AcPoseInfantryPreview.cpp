#include "AcPoseInfantryPreview.h"

#include "AcLog.h"
#include "AcModelCatalog.h"
#include "AcModelRow.h"
#include "AcPose.h"
#include "AcPoseInfantry.h"
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

#include <cmath>

namespace
{
	using Task = ac::Unit::Task;

	/// PosePreview.swift `prospector(_:_:)`.
	ac::Unit ProspectorAt(const FString& Scene, const double T)
	{
		ac::Unit U(1, ac::UnitKind::prospector, 0, ac::Vec2(0, 0), 0.0, Task::toBase);
		if (Scene == TEXT("prospector-mine"))
		{
			U.task = Task::mining;
			U.timer = FMath::Max(0.0, 3 - T);
			U.moving = false;
		}
		else
		{
			U.moving = true;
			U.stride = 1.1 * T;
			U.carrying = Scene == TEXT("prospector-walk") ? 0 : 5;
			U.hydrogen = Scene == TEXT("prospector-hydrogen");
		}
		return U;
	}

	/// PosePreview.swift `ranger(_:_:)`: the unit and its last shot.
	ac::Unit RangerAt(const FString& Scene, const double T, TOptional<double>& Shot)
	{
		ac::Unit U(1, ac::UnitKind::ranger, 0, ac::Vec2(0, 0), 0.0, Task::toRally);
		Shot.Reset();
		if (Scene == TEXT("ranger-walk"))
		{
			U.moving = true;
			U.stride = 1.3 * T;
			return U;
		}
		U.task = Task::attacking;
		U.moving = false;
		const double Every = Scene == TEXT("ranger-minigun") ? 0.1 : 0.8;
		if (T >= 0.2) Shot = 0.2 + std::floor((T - 0.2) / Every) * Every;
		return U;
	}
}

AAcPoseInfantryPreview::AAcPoseInfantryPreview()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AAcPoseInfantryPreview::BeginPlay()
{
	Super::BeginPlay();
	FString Scene;
	double At = 0.0;
	FParse::Value(FCommandLine::Get(), TEXT("AcPoseAt="), At);
	if (FParse::Value(FCommandLine::Get(), TEXT("AcPoseInfantry="), Scene)) Build(Scene, At);
}

void AAcPoseInfantryPreview::Build(const FString& Scene, const double At)
{
	const bool bProspector = Scene.StartsWith(TEXT("prospector"));
	if (!bProspector && !Scene.StartsWith(TEXT("ranger")))
	{
		UE_LOG(LogAutocraft, Error, TEXT("pose infantry: no scene %s"), *Scene);
		return;
	}
	const FAcModelInfo* Model = FAcModelCatalog::Get().Find(bProspector ? TEXT("prospector_blue") : TEXT("ranger_blue"));
	if (!Model)
	{
		UE_LOG(LogAutocraft, Error, TEXT("pose infantry: no model"));
		return;
	}
	int32 Team = 0;
	FParse::Value(FCommandLine::Get(), TEXT("AcPoseTeam="), Team);

	// Step from 0 at 60 Hz, as the game poses it each frame.
	const ac::MapDefinition Map = ac::WindowMaps::playground();
	const ac::TerrainField Field(Map);
	const double Ground = Field.height(ac::Vec2(0, 0));
	ac::GameState State;
	FAcUnitMemory Memory;
	FAcPose Pose;
	AcPoseInfantry::FRangerArms Arms;
	Arms.bMinigun = Scene == TEXT("ranger-minigun");
	Arms.bShield = Scene != TEXT("ranger-walk");
	// PosePreview.swift `aim`: ahead and to its right, chest high.
	const FVector Aim = AcSpace::FromSceneKit(3.0, 0.62 + Ground, 0.8);
	const int32 Steps = FMath::Max(0, FMath::RoundToInt(At * 60));
	ac::Unit U;
	for (int32 K = 0; K <= Steps; ++K)
	{
		const double T = double(K) / 60.0;
		State.time = T;
		TOptional<double> Shot;
		if (bProspector)
		{
			U = ProspectorAt(Scene, T);
			Shot = Scene == TEXT("prospector-mine") && T >= 0.8 ? TOptional<double>(0.8) : TOptional<double>();
		}
		else
		{
			U = RangerAt(Scene, T, Shot);
		}
		Memory.LastShot = Shot;
		FAcPoseContext C{State, &U, nullptr, *Model, Field, Memory, T, K == 0 ? 0.0 : 1.0 / 60.0, false, 0};
		AcPose::RestUnit(C, Pose);
		if (bProspector) AcPoseInfantry::PoseProspector(C, Pose);
		else AcPoseInfantry::PoseRanger(C, Pose, U.task == Task::attacking ? TOptional<FVector>(Aim) : TOptional<FVector>(), Arms);
	}

	// Swift preview: the model turned -(π/2 + 0.6) about SceneKit Y (UE yaw
	// +(π/2 + 0.6)), on the ground at 0.
	FTransform Placement = Pose.Placement;
	Placement.AddToTranslation(FVector(0, 0, -AcSpace::ToCm(Ground)));
	Placement.SetRotation(AcSpace::QuatFromHeading(UE_DOUBLE_HALF_PI + 0.6) * Placement.GetRotation());
	const int32 N = Model->Parts.Num();
	TArray<FTransform> World;
	TArray<bool> Visible = Pose.Visible;
	World.SetNum(N);
	for (int32 P = 0; P < N; ++P)
	{
		const int32 Parent = Model->Parts[P].Parent;
		World[P] = Pose.Local[P] * (Parent != INDEX_NONE ? World[Parent] : Placement);
		if (Parent != INDEX_NONE) Visible[P] = Visible[P] && Visible[Parent];
	}

	double Lowest = TNumericLimits<double>::Max();
	for (int32 P = 0; P < N; ++P)
	{
		const FAcModelPart& Part = Model->Parts[P];
		const bool bFoot = Part.Name.ToString().Contains(TEXT("_foot_"));
		for (const FAcModelMesh& Mesh : Part.Meshes)
		{
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
			if (bFoot && Visible[P])
			{
				const FBox B = SM->GetBoundingBox();
				for (int32 Corner = 0; Corner < 8; ++Corner)
				{
					const FVector V((Corner & 1) ? B.Max.X : B.Min.X, (Corner & 2) ? B.Max.Y : B.Min.Y, (Corner & 4) ? B.Max.Z : B.Min.Z);
					Lowest = FMath::Min(Lowest, World[P].TransformPosition(V).Z);
				}
			}
		}
	}
	FString Cues;
	for (const FAcPoseCue& Cue : Pose.Cues) Cues += FString::Printf(TEXT(" %s(%.0f)"), *Cue.What.ToString(), Cue.Value);
	UE_LOG(LogAutocraft, Log, TEXT("pose infantry: %s at %.3f s: lowest sole %.2f cm, rest fold error %.4f%s%s"), *Scene, At,
		Lowest, bProspector ? 0.0 : AcPoseInfantry::RangerFoldError(*Model), Cues.IsEmpty() ? TEXT("") : TEXT(", cues"), *Cues);

	// Nothing else in the level.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->IsA<AAcModelRow>()) It->SetActorHiddenInGame(true);
		if (It->ActorHasTag(TEXT("AcFloor")) && !FParse::Param(FCommandLine::Get(), TEXT("AcPoseFloor")))
		{
			It->SetActorHiddenInGame(true);
		}
	}
	// The preview's camera and sun (ModelCatalog: target (0, 0.8, 0), 4.4 away).
	const double TargetY = 0.8, D = 4.4;
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

void AAcPoseInfantryPreview::Tick(const float DeltaSeconds)
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

bool UAcPoseInfantryPreviewSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return FCString::Strifind(FCommandLine::Get(), TEXT("-AcPoseInfantry=")) != nullptr;
}

void UAcPoseInfantryPreviewSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	if (!World.IsGameWorld()) return;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	World.SpawnActor<AAcPoseInfantryPreview>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
}
