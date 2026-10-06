#include "AcPoseVehiclesSheet.h"

#include "AcLog.h"
#include "AcModelCatalog.h"
#include "AcModelRow.h"
#include "AcPose.h"
#include "AcShot.h"
#include "AcSpace.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ContentStreaming.h"
#include "Dom/JsonObject.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
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
		for (int32 K = 0; K <= (int32)ac::UnitKind::scorpion; ++K)
		{
			if (Name == AcPose::ModelBase((ac::UnitKind)K)) return (ac::UnitKind)K;
		}
		return {};
	}

	double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const double Default)
	{
		double V = Default;
		return O->TryGetNumberField(Key, V) ? V : Default;
	}

	bool Flag(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		bool B = false;
		return O->TryGetBoolField(Key, B) && B;
	}

	TOptional<double> Maybe(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		double V = 0;
		if (O->TryGetNumberField(Key, V)) return V;
		return {};
	}
}

AAcPoseSheet::AAcPoseSheet()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AAcPoseSheet::BeginPlay()
{
	Super::BeginPlay();
	FString Kind, Spec;
	if (FParse::Value(FCommandLine::Get(), TEXT("AcPoseSheet="), Kind)
		&& FParse::Value(FCommandLine::Get(), TEXT("AcPoseSheetSpec="), Spec))
	{
		Build(Kind, Spec);
	}
}

void AAcPoseSheet::Build(const FString& KindName, const FString& SpecFile)
{
	const TOptional<ac::UnitKind> Kind = KindNamed(KindName);
	const FAcModelInfo* Model = FAcModelCatalog::Get().Find(FName(KindName + TEXT("_blue")));
	const FAcPoseFn Fn = Kind ? AcPose::Find(*Kind) : nullptr;
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!Kind || !Model || !FFileHelper::LoadFileToString(Text, *SpecFile)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
	{
		UE_LOG(LogAutocraft, Error, TEXT("pose sheet: no model %s or no spec %s"), *KindName, *SpecFile);
		return;
	}
	TSharedPtr<FJsonObject> Sheet;
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("sheets")))
	{
		if (V->AsObject()->GetStringField(TEXT("kind")) == KindName) Sheet = V->AsObject();
	}
	if (!Sheet)
	{
		UE_LOG(LogAutocraft, Error, TEXT("pose sheet: no sheet for %s in %s"), *KindName, *SpecFile);
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>& Frames = Sheet->GetArrayField(TEXT("frames"));
	const int32 N = Frames.Num();
	const double Gap = Num(Sheet, TEXT("gap"), 1.5);
	const double Heading = UE_DOUBLE_HALF_PI + 0.6;
	const ac::Vec2 Dir(std::cos(Heading), std::sin(Heading));
	const ac::MapDefinition Map = ac::WindowMaps::playground();
	const ac::TerrainField Field(Map);

	for (int32 K = 0; K < N; ++K)
	{
		const TSharedPtr<FJsonObject> F = Frames[K]->AsObject();
		const double Time = Num(F, TEXT("time"), 10.0);
		const ac::Vec2 At((double(K) - double(N - 1) / 2.0) * Gap, 0.0);
		const TOptional<double> Since = Maybe(F, TEXT("since"));
		const int32 Warm = int32(Num(F, TEXT("warm"), 0.0));
		const double Speed = Num(F, TEXT("speed"), 0.0);
		const bool bFirefly = *Kind == ac::UnitKind::firefly;

		ac::Unit U(7, *Kind, 0, At, Heading, ac::Unit::Task::idle);
		U.stride = Num(F, TEXT("stride"), 0.0);
		U.moving = bFirefly ? Flag(F, TEXT("moving")) : Flag(F, TEXT("walking"));
		if (Flag(F, TEXT("aiming")))
		{
			U.task = ac::Unit::Task::attacking;
			U.moving = false;
		}
		if (const TOptional<double> J = Maybe(F, TEXT("jump")))
		{
			// A 2-cell leap along the heading, `jump` of the way over.
			U.jumpFrom = At - Dir * (*J * 2.0);
			U.jumpTo = *U.jumpFrom + Dir * 2.0;
		}
		if (bFirefly || *Kind == ac::UnitKind::atlas) U.aim = Heading + Num(F, TEXT("aim"), 0.0);
		// The new kinds: a Scorpion `anchor`ed (0-1 buried, `anchored` says
		// which way it goes), its `cooldown` running, its `lock` held that many
		// seconds on a target `lockAt` cells ahead; an Atlas `stomp`ed that many
		// seconds ago.
		if (const TOptional<double> A = Maybe(F, TEXT("anchor")))
		{
			U.anchor = *A;
			U.anchored = F->HasField(TEXT("anchored")) ? Flag(F, TEXT("anchored")) : true;
		}
		if (const TOptional<double> Cd = Maybe(F, TEXT("cooldown"))) U.cooldown = *Cd;
		if (const TOptional<double> St = Maybe(F, TEXT("stomp"))) U.stompedAt = Time - *St;

		ac::GameState State;
		State.time = Time;
		if (const TOptional<double> Lock = Maybe(F, TEXT("lock")))
		{
			// A Ranger of the other side that far ahead, locked onto.
			const double Away = Num(F, TEXT("lockAt"), 3.0);
			State.units.push_back(ac::Unit(99, ac::UnitKind::ranger, 1, At + Dir * Away, Heading + UE_DOUBLE_PI, ac::Unit::Task::idle));
			U.lockTarget = 99;
			U.lockFrom = Time - *Lock;
			U.lockAt = Time;
			U.task = ac::Unit::Task::attacking;
			U.target = 99;
		}
		FAcUnitMemory Memory;
		if (Since) Memory.LastShot = Time - *Since;
		if (*Kind == ac::UnitKind::juggernaut)
		{
			const double Twist = Num(F, TEXT("twist"), 0.0);
			if (Twist != 0.0 || Flag(F, TEXT("aiming")))
			{
				Memory.LastShotAt = At + ac::Vec2(std::cos(Heading + Twist), std::sin(Heading + Twist)) * 5.0;
			}
		}
		Memory.FlameLength = Num(F, TEXT("flameLength"), 3.0);
		// The turn rate held (no time passes between the sheet's poses).
		Memory.TurnRate = 4.0 * Num(F, TEXT("turn"), 0.0);
		Memory.LastHeading = Heading;

		FAcPose Pose;
		auto PoseAt = [&](const int32 Step)
		{
			// Warm-up steps drive up to the copy's place.
			if (bFirefly)
			{
				U.stride = 5.0 + double(Step) * Speed;
				U.position = At - Dir * (Speed * double(Warm + 1 - Step));
			}
			FAcPoseContext C{State, &U, nullptr, *Model, Field, Memory, Time, 0.0, false, 0};
			AcPose::RestUnit(C, Pose);
			if (Fn) Fn(C, Pose);
		};
		for (int32 Step = 1; Step <= (bFirefly ? Warm + 1 : 1); ++Step) PoseAt(Step);

		// On flat ground: the placement less the playground's height here.
		FTransform Placement = Pose.Placement;
		Placement.AddToTranslation(FVector(0, 0, -AcSpace::ToCm(Field.height(U.position))));
		TArray<FTransform> World;
		TArray<bool> Visible = Pose.Visible;
		World.SetNum(Model->Parts.Num());
		for (int32 P = 0; P < Model->Parts.Num(); ++P)
		{
			const int32 Parent = Model->Parts[P].Parent;
			World[P] = Pose.Local[P] * (Parent != INDEX_NONE ? World[Parent] : Placement);
			if (Parent != INDEX_NONE) Visible[P] = Visible[P] && Visible[Parent];
		}
		for (int32 P = 0; P < Model->Parts.Num(); ++P)
		{
			const FAcModelPart& Part = Model->Parts[P];
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
				C->SetCustomPrimitiveDataFloat(AcModelData::TeamIndex, 0.f);
				C->SetCustomPrimitiveDataFloat(AcModelData::EmissionScale, Emission);
				C->SetWorldTransform(World[P]);
				C->SetVisibility(Visible[P]);
				C->RegisterComponent();
				Meshes.Add(C);
			}
		}
		UE_LOG(LogAutocraft, Log, TEXT("pose sheet: %s frame %d '%s' placed at %s"), *KindName, K,
			*F->GetStringField(TEXT("label")), *Placement.GetTranslation().ToString());
	}

	// Nothing else in the level: the model row and its floor hidden.
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
	// The project keeps the vertical FOV (it derives it from this aspect).
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

void AAcPoseSheet::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Camera) return;
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (PC->GetViewTarget() != Camera) PC->SetViewTarget(Camera);
		if (SettleFrames == 30 && PC->PlayerCameraManager)
		{
			UE_LOG(LogAutocraft, Log, TEXT("pose sheet: view %s fov %.2f at %s (camera fov %.2f)"),
				*GetNameSafe(PC->GetViewTarget()), PC->PlayerCameraManager->GetFOVAngle(),
				*PC->PlayerCameraManager->GetCameraLocation().ToString(), Camera->GetCameraComponent()->FieldOfView);
		}
	}
	// SceneKit's FOV is vertical; the sheet's own aspect (the viewport's
	// reported size can differ from the shot's under -RenderOffscreen).
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

bool UAcPoseSheetSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return FCString::Strifind(FCommandLine::Get(), TEXT("-AcPoseSheet=")) != nullptr;
}

void UAcPoseSheetSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	if (!World.IsGameWorld()) return;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	World.SpawnActor<AAcPoseSheet>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
}
