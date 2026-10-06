#include "AcEffectsDeathsSheet.h"

#include "AcEffectsDeaths.h"
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
#include "Dom/JsonObject.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

#include "TerrainField.h"
#include "Types.h"
#include "WindowMaps.h"

namespace AcDeathSheetPrivate
{
	double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const double Default)
	{
		double V = Default;
		return O->TryGetNumberField(Key, V) ? V : Default;
	}

	TOptional<ac::UnitKind> KindNamed(const FString& Name)
	{
		for (int32 K = 0; K <= (int32)ac::UnitKind::hailstorm; ++K)
		{
			if (Name == AcPose::ModelBase((ac::UnitKind)K)) return (ac::UnitKind)K;
		}
		return {};
	}
}

AAcDeathSheet::AAcDeathSheet()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AAcDeathSheet::BeginPlay()
{
	Super::BeginPlay();
	FString Kind, Spec;
	if (FParse::Value(FCommandLine::Get(), TEXT("AcDeathSheet="), Kind)
		&& FParse::Value(FCommandLine::Get(), TEXT("AcDeathSheetSpec="), Spec))
	{
		Build(Kind, Spec);
	}
}

void AAcDeathSheet::Build(const FString& KindName, const FString& SpecFile)
{
	using namespace AcDeathSheetPrivate;
	const TOptional<ac::UnitKind> Kind = KindNamed(KindName);
	const TOptional<AcDeaths::EFall> Fall = Kind ? AcDeaths::FallOf(*Kind) : TOptional<AcDeaths::EFall>();
	const FAcModelInfo* Model = FAcModelCatalog::Get().Find(FName(KindName + TEXT("_blue")));
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!Fall || !Model || !FFileHelper::LoadFileToString(Text, *SpecFile)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
	{
		UE_LOG(LogAutocraft, Error, TEXT("death sheet: no falling kind %s or no spec %s"), *KindName, *SpecFile);
		return;
	}
	TSharedPtr<FJsonObject> Sheet;
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("sheets")))
	{
		if (V->AsObject()->GetStringField(TEXT("kind")) == KindName) Sheet = V->AsObject();
	}
	if (!Sheet)
	{
		UE_LOG(LogAutocraft, Error, TEXT("death sheet: no sheet for %s in %s"), *KindName, *SpecFile);
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>& Frames = Sheet->GetArrayField(TEXT("frames"));
	const int32 N = Frames.Num();
	const double Gap = Num(Sheet, TEXT("gap"), 1.5);
	const double Heading = UE_DOUBLE_HALF_PI + 0.6;
	const ac::MapDefinition Map = ac::WindowMaps::playground();
	const ac::TerrainField Field(Map);
	const FAcPoseFn Fn = AcPose::Find(*Kind);

	for (int32 K = 0; K < N; ++K)
	{
		const TSharedPtr<FJsonObject> F = Frames[K]->AsObject();
		const double Sec = Num(F, TEXT("sec"), 0.0);
		const int64 Id = int64(Num(F, TEXT("id"), 7.0));
		const ac::Vec2 At((double(K) - double(N - 1) / 2.0) * Gap, 0.0);
		ac::Unit U(Id, *Kind, 0, At, Heading, ac::Unit::Task::idle);
		ac::GameState State;
		State.time = 10.0;
		FAcUnitMemory Memory;
		Memory.LastHeading = Heading;
		FAcPose Pose;
		FAcPoseContext C{State, &U, nullptr, *Model, Field, Memory, 10.0, 0.0, false, 0};
		AcPose::RestUnit(C, Pose);
		if (Fn) Fn(C, Pose);
		// On flat ground: the placement less the playground's height here.
		Pose.Placement.AddToTranslation(FVector(0, 0, -AcSpace::ToCm(Field.height(U.position))));

		const AcDeaths::FBody Body = AcDeaths::Start(*Fall, Id, *Model, Pose);
		TArray<FTransform> World;
		TArray<bool> Visible;
		const float Opacity = float(AcDeaths::Pose(Body, Sec, World, Visible));
		for (int32 P = 0; P < Model->Parts.Num(); ++P)
		{
			const FAcModelPart& Part = Model->Parts[P];
			for (int32 Mi = 0; Mi < Part.Meshes.Num(); ++Mi)
			{
				const FAcModelMesh& Mesh = Part.Meshes[Mi];
				UStaticMesh* SM = Mesh.LoadMesh();
				if (!SM) continue;
				const AcDeaths::FFadeMaterial Mat = AcDeaths::FadeMaterial(Mesh.LoadMaterial(), this);
				Keep.Add(Mat.Material);
				const float E = AcDeaths::MeshEmission(Body, P, Mi);
				UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
				Comp->SetupAttachment(RootComponent);
				Comp->SetStaticMesh(SM);
				if (Mat.Material) Comp->SetMaterial(0, Mat.Material);
				Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Comp->SetCustomPrimitiveDataFloat(AcModelData::TeamIndex, 0.f);
				Comp->SetCustomPrimitiveDataFloat(AcModelData::EmissionScale, Mat.bDither ? E : E * Opacity);
				Comp->SetCustomPrimitiveDataFloat(AcModelData::Fade, Mat.bDither ? 1.f - Opacity : 0.f);
				Comp->SetWorldTransform(World[P]);
				Comp->SetVisibility(Visible[P] && Opacity > 0.f);
				Comp->RegisterComponent();
				Meshes.Add(Comp);
			}
		}
		UE_LOG(LogAutocraft, Log, TEXT("death sheet: %s frame %d '%s' sec %.2f opacity %.2f"), *KindName, K,
			*F->GetStringField(TEXT("label")), Sec, Opacity);
	}

	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->IsA<AAcModelRow>() || It->ActorHasTag(TEXT("AcFloor"))) It->SetActorHiddenInGame(true);
	}
	const double Cy = Num(Sheet, TEXT("cy"), 0.7), D = Num(Sheet, TEXT("D"), 30.0);
	VerticalFov = Num(Sheet, TEXT("fov"), 6.0);
	Aspect = Num(Sheet, TEXT("width"), 16.0) / Num(Sheet, TEXT("height"), 9.0);
	const FVector Target = AcSpace::FromSceneKit(0, Cy, 0);
	const FVector CamPos = AcSpace::FromSceneKit(0, Cy + 0.55 * D, D);
	const FVector SunPos = AcSpace::FromSceneKit(-4, 8, 6);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Camera = GetWorld()->SpawnActor<ACameraActor>(CamPos, (Target - CamPos).Rotation(), Params);
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	Camera->GetCameraComponent()->SetAspectRatio(float(Aspect));
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

void AAcDeathSheet::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Camera) return;
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (PC->GetViewTarget() != Camera) PC->SetViewTarget(Camera);
	}
	{
		const double Half = FMath::DegreesToRadians(VerticalFov / 2.0);
		Camera->GetCameraComponent()->SetFieldOfView(
			float(FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(Half) * Aspect))));
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

bool UAcDeathSheetSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return FCString::Strifind(FCommandLine::Get(), TEXT("-AcDeathSheet=")) != nullptr;
}

void UAcDeathSheetSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	if (!World.IsGameWorld()) return;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	World.SpawnActor<AAcDeathSheet>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
}
