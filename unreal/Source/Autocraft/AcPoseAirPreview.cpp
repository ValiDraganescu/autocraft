#include "AcPoseAirPreview.h"

#include "AcLog.h"
#include "AcModelCatalog.h"
#include "AcModelRow.h"
#include "AcPoseAir.h"
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

namespace
{
	const TCHAR* Cmd() { return FCommandLine::Get(); }

	double Opt(const TCHAR* Key, const double Default)
	{
		double V = Default;
		FParse::Value(Cmd(), Key, V);
		return V;
	}

	/// ModelCatalog.swift: the preview's camera target (SceneKit) and distance.
	constexpr double TargetY = 2.1;
	double Distance(const bool bDropship) { return bDropship ? 5.0 : 4.6; }
}

bool UAcPoseAirPreviewSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	FString V;
	return FParse::Value(Cmd(), TEXT("AcAirPreview="), V) && Super::ShouldCreateSubsystem(Outer);
}

void UAcPoseAirPreviewSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	if (!World.IsGameWorld()) return;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	World.SpawnActor<AAcPoseAirPreview>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
}

AAcPoseAirPreview::AAcPoseAirPreview()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AAcPoseAirPreview::BeginPlay()
{
	Super::BeginPlay();
	FParse::Value(Cmd(), TEXT("AcAirPreview="), What);
	const bool bDropship = What.StartsWith(TEXT("dropship"));
	Model = FAcModelCatalog::Get().Find(FName(bDropship ? TEXT("dropship_blue") : TEXT("kestrel_blue")));
	if (!Model)
	{
		UE_LOG(LogAutocraft, Error, TEXT("air preview: no model for %s"), *What);
		return;
	}
	// Only the model: the row and the floor go.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->IsA<AAcModelRow>() || It->ActorHasTag(TEXT("AcFloor"))) It->SetActorHiddenInGame(true);
	}
	Build(*Model, (int32)Opt(TEXT("AcAirTeam="), 0.0));

	// Snapshot.swift ModelPreview: the sun at (-4, 8, 6) looking at the
	// target, the camera at (0, target.y + 0.45 d, d), 30° vertical FOV.
	const double D = Distance(bDropship) * Opt(TEXT("AcAirDistance="), 1.0);
	const FVector Target = AcSpace::FromSceneKit(0.0, TargetY, 0.0);
	const FVector CamPos = AcSpace::FromSceneKit(0.0, TargetY + 0.45 * D, D);
	const FVector SunPos = AcSpace::FromSceneKit(-4.0, 8.0, 6.0);
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		It->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		It->SetActorRotation((Target - SunPos).Rotation());
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Camera = GetWorld()->SpawnActor<ACameraActor>(CamPos, (Target - CamPos).Rotation(), Params);
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	if (UAcShotSubsystem* Shot = UAcShotSubsystem::Get(this))
	{
		Shot->Hold();
		bHolding = true;
	}
	Apply();
	UE_LOG(LogAutocraft, Log, TEXT("air preview: %s, camera %s"), *What, *CamPos.ToString());
}

void AAcPoseAirPreview::Build(const FAcModelInfo& M, const int32 Team)
{
	Parts.SetNum(M.Parts.Num());
	PartMeshes.SetNum(M.Parts.Num());
	for (int32 I = 0; I < M.Parts.Num(); ++I)
	{
		const FAcModelPart& P = M.Parts[I];
		USceneComponent* Part = NewObject<USceneComponent>(this, NAME_None, RF_Transient);
		Part->SetupAttachment(P.Parent != INDEX_NONE ? Parts[P.Parent] : RootComponent.Get());
		Part->SetMobility(EComponentMobility::Movable);
		Part->SetRelativeTransform(P.Rest);
		Part->RegisterComponent();
		Parts[I] = Part;
		Built.Add(Part);
		for (const FAcModelMesh& X : P.Meshes)
		{
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
			C->SetupAttachment(Part);
			C->SetMobility(EComponentMobility::Movable);
			if (UStaticMesh* Mesh = X.LoadMesh()) C->SetStaticMesh(Mesh);
			if (UMaterialInterface* Mat = X.LoadMaterial()) C->SetMaterial(0, Mat);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetCustomPrimitiveDataFloat(AcModelData::TeamIndex, float(Team));
			C->SetCustomPrimitiveDataFloat(AcModelData::EmissionScale, 1.f);
			C->RegisterComponent();
			PartMeshes[I].Add(C);
			Built.Add(C);
		}
	}
}

void AAcPoseAirPreview::Apply()
{
	if (!Model) return;
	const FAcModelInfo& M = *Model;
	const bool bDropship = What.StartsWith(TEXT("dropship"));
	const bool bBusy = What.EndsWith(TEXT("-busy"));
	// The preview turns units -(π/2 + 0.6) about SceneKit Y: heading π/2 + 0.6.
	const double Heading = UE_DOUBLE_HALF_PI + 0.6 - Opt(TEXT("AcAirYaw="), 0.0);
	const double Hover = bDropship ? AcPoseAir::DropshipHover : AcPoseAir::KestrelHover;
	double Lift = 0.0;
	if (double Age; FParse::Value(Cmd(), TEXT("AcAirLift="), Age)) Lift = AcPoseAir::LaunchDrop(Age, Hover).Get(0.0);

	Pose.Reset(M);
	Pose.Placement = FTransform(AcSpace::QuatFromHeading(Heading), FVector(0.0, 0.0, -AcSpace::ToCm(Lift)));
	if (bDropship)
	{
		AcPoseAir::FDropship In;
		In.Id = 1;
		In.Time = Opt(TEXT("AcAirTime="), 0.3);
		In.bMoving = Opt(TEXT("AcAirMoving="), 0.0) != 0.0;
		In.Bank = Opt(TEXT("AcAirBank="), 0.0);
		const double Open = Opt(TEXT("AcAirOpen="), bBusy ? 0.6 : -1.0);
		if (Open >= 0.0) In.Unloading = Open;
		// ModelCatalog.swift: healing a point ahead and below, (1.6, 0.8, 0.4)
		// in the root's space.
		if (Opt(TEXT("AcAirHeal="), bBusy ? 1.0 : 0.0) != 0.0)
		{
			In.Healing = (Pose.Local[0] * Pose.Placement).TransformPosition(AcSpace::FromSceneKit(1.6, 0.8, 0.4));
		}
		AcPoseAir::PoseDropship(M, Pose, In);
	}
	else
	{
		AcPoseAir::FKestrel In;
		In.Id = 1;
		In.Time = Opt(TEXT("AcAirTime="), 1.03);
		In.bMoving = Opt(TEXT("AcAirMoving="), bBusy ? 1.0 : 0.0) != 0.0;
		In.Bank = Opt(TEXT("AcAirBank="), bBusy ? 0.4 : 0.0);
		const double Shot = Opt(TEXT("AcAirShot="), bBusy ? 1.0 : -100.0);
		if (Shot > -100.0) In.Shot = Shot;
		AcPoseAir::PoseKestrel(M, Pose, In);
	}

	RootComponent->SetWorldTransform(Pose.Placement);
	TArray<bool> Shown;
	Shown.SetNum(M.Parts.Num());
	for (int32 I = 0; I < M.Parts.Num(); ++I)
	{
		const int32 Parent = M.Parts[I].Parent;
		Shown[I] = Pose.Visible[I] && (Parent == INDEX_NONE || Shown[Parent]);
		Parts[I]->SetRelativeTransform(Pose.Local[I]);
		for (int32 K = 0; K < PartMeshes[I].Num(); ++K)
		{
			float E = Pose.Emission[I];
			for (const FAcPose::FMeshEmission& X : Pose.MeshEmission)
			{
				if (X.Part == I && M.Parts[I].Meshes[K].Material == X.Material) E = X.Value;
			}
			PartMeshes[I][K]->SetVisibility(Shown[I]);
			PartMeshes[I][K]->SetCustomPrimitiveDataFloat(AcModelData::EmissionScale, E);
		}
	}
}

void AAcPoseAirPreview::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Camera) return;
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (PC->GetViewTarget() != Camera) PC->SetViewTarget(Camera);
	}
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
