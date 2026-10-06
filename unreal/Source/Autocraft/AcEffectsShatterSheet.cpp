#include "AcEffectsShatterSheet.h"

#include "AcEffectsShatter.h"
#include "AcLog.h"
#include "AcModelCatalog.h"
#include "AcModelRow.h"
#include "AcPose.h"
#include "AcShatter.h"
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

namespace AcShatterSheetPrivate
{
	double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const double Default)
	{
		double V = Default;
		return O->TryGetNumberField(Key, V) ? V : Default;
	}
}

AAcShatterSheet::AAcShatterSheet()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AAcShatterSheet::BeginPlay()
{
	Super::BeginPlay();
	FString Kind, Spec;
	if (FParse::Value(FCommandLine::Get(), TEXT("AcShatterSheet="), Kind)
		&& FParse::Value(FCommandLine::Get(), TEXT("AcShatterSheetSpec="), Spec))
	{
		Build(Kind, Spec);
	}
}

void AAcShatterSheet::EndPlay(const EEndPlayReason::Type Reason)
{
	Library.Reset();
	Super::EndPlay(Reason);
}

void AAcShatterSheet::Build(const FString& KindName, const FString& SpecFile)
{
	using namespace AcShatterSheetPrivate;
	AcEffectsShatter::FSpec Spec;
	TOptional<ac::UnitKind> Unit;
	TOptional<ac::StructureKind> Building;
	for (int32 K = 0; K <= int32(ac::UnitKind::scorpion); ++K)
		if (KindName == AcPose::ModelBase(ac::UnitKind(K)) && AcEffectsShatter::SpecOf(ac::UnitKind(K), Spec)) Unit = ac::UnitKind(K);
	for (int32 K = 0; K <= int32(ac::StructureKind::sentinel); ++K)
		if (KindName == AcPose::ModelBase(ac::StructureKind(K)) && AcEffectsShatter::SpecOf(ac::StructureKind(K), Spec))
			Building = ac::StructureKind(K);
	const FAcModelInfo* Model = Spec.Model ? FAcModelCatalog::Get().Find(FName(Spec.Model)) : nullptr;
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!Model || !FFileHelper::LoadFileToString(Text, *SpecFile)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
	{
		UE_LOG(LogAutocraft, Error, TEXT("shatter sheet: no C5 kind %s or no spec %s"), *KindName, *SpecFile);
		return;
	}
	TSharedPtr<FJsonObject> Sheet;
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("sheets")))
		if (V->AsObject()->GetStringField(TEXT("kind")) == KindName) Sheet = V->AsObject();
	if (!Sheet)
	{
		UE_LOG(LogAutocraft, Error, TEXT("shatter sheet: no sheet for %s in %s"), *KindName, *SpecFile);
		return;
	}
	Library = MakeShared<FAcShatterLibrary>();
	Library->Request(*Model, Spec.Chunks, Spec.Strip);
	const AcShatter::FModel* Cut = Library->Finish(*Model);
	if (!Cut)
	{
		UE_LOG(LogAutocraft, Error, TEXT("shatter sheet: %s could not be cut"), *KindName);
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>& Frames = Sheet->GetArrayField(TEXT("frames"));
	const int32 N = Frames.Num();
	const double Gap = Num(Sheet, TEXT("gap"), 4);
	const double Heading = UE_DOUBLE_HALF_PI + 0.6;
	const ac::MapDefinition Map = ac::WindowMaps::playground();
	const ac::TerrainField Field(Map);
	auto Flat = [](double, double) { return 0.0; };

	for (int32 K = 0; K < N; ++K)
	{
		const TSharedPtr<FJsonObject> F = Frames[K]->AsObject();
		const double Sec = Num(F, TEXT("sec"), 0.0);
		const int64 Id = int64(Num(F, TEXT("id"), 7.0));
		const ac::Vec2 At((double(K) - double(N - 1) / 2.0) * Gap, 0.0);
		ac::GameState State;
		State.time = 10.0;
		FAcUnitMemory Memory;
		FAcPose Pose;
		ac::Unit U(Id, Unit ? *Unit : ac::UnitKind::firefly, 0, At, Heading, ac::Unit::Task::idle);
		ac::Structure B(Id, Building ? *Building : ac::StructureKind::citadel, 0, At);
		if (Unit)
		{
			Memory.LastHeading = Heading;
			FAcPoseContext C{State, &U, nullptr, *Model, Field, Memory, 10.0, 0.0, false, 0};
			AcPose::RestUnit(C, Pose);
			if (const FAcPoseFn Fn = AcPose::Find(*Unit)) Fn(C, Pose);
			Pose.Placement.AddToTranslation(FVector(0, 0, -AcSpace::ToCm(Field.height(U.position))));
		}
		else
		{
			FAcPoseContext C{State, nullptr, &B, *Model, Field, Memory, 10.0, 0.0, false, 0};
			AcPose::RestStructure(C, Pose);
			if (const FAcPoseFn Fn = AcPose::Find(*Building)) Fn(C, Pose);
			Pose.Placement.AddToTranslation(FVector(0, 0, -AcSpace::ToCm(Field.height(B.position))));
		}

		// Where each sub is now: whole while a flyer falls, else its chunk's move.
		TArray<FTransform> World;
		TArray<bool> Shown;
		AcShatter::FWreck W;
		bool bWhole = false;
		double Since = Sec;
		const bool bFalls = Spec.bFlyer || Spec.bTopple;
		const double FallTime = Spec.bTopple ? AcShatter::AtlasFallTime : AcShatter::FallTime;
		auto FallPose = [&](const double At, FAcPose& Out)
		{
			if (Spec.bTopple) AcShatter::AtlasFallPose(*Model, Pose, At, Out);
			else AcShatter::ShootDownPose(*Model, Pose, Id, At, Out);
		};
		if (bFalls && Sec < FallTime)
		{
			FAcPose Now;
			FallPose(Sec, Now);
			AcShatter::PartWorlds(*Model, Now, World, Shown);
			bWhole = true;
		}
		else
		{
			if (bFalls)
			{
				FAcPose Final;
				FallPose(FallTime, Final);
				AcShatter::PartWorlds(*Model, Final, World, Shown);
				Since = Sec - FallTime;
			}
			else
			{
				AcShatter::PartWorlds(*Model, Pose, World, Shown);
			}
			TArray<FBox> Boxes;
			AcShatter::ChunkBoxes(*Cut, World, Shown, Boxes);
			if (Spec.bBuilding)
			{
				TArray<AcShatter::FBlast> Blasts = {{At, 0.0}};
				for (int32 J = 0; J < 3; ++J)
				{
					const double A = double(J) * 2.1 + double(Id);
					Blasts.Add({At + ac::Vec2(std::cos(A), std::sin(A)) * (Spec.Radius * 0.6), 0.25 + double(J) * 0.4});
				}
				W = AcShatter::Fell(*Cut, Boxes, At, 0.0, Spec.Height, Spec.Radius, MoveTemp(Blasts), Id, Flat);
				if (K == 0)
					for (int32 C = 0; C < W.Chunks.Num(); ++C)
					{
						const AcShatter::FChunk& Ch = W.Chunks[C];
						UE_LOG(LogAutocraft, Log, TEXT("shatter sheet: chunk %d p0 (%.2f %.2f %.2f) ext (%.2f %.2f %.2f) shell %d release %.2f v (%.2f %.2f %.2f)"),
							C, Ch.P0.X - At.x, Ch.P0.Y, Ch.P0.Z, Ch.Extent.X, Ch.Extent.Y, Ch.Extent.Z, Ch.bShell, Ch.Release, Ch.V.X, Ch.V.Y, Ch.V.Z);
					}
			}
			else
			{
				W = AcShatter::Launch(*Cut, Boxes, Spec.Size, Id, Flat);
			}
		}
		const float Opacity = bWhole ? 1.f : float(AcShatter::Opacity(W, Since));
		const float Glow = bWhole ? -1.f : float(AcShatter::Glow(W, Since));
		for (const AcShatter::FSub& Sub : Cut->Subs)
		{
			if (!Shown[Sub.Part]) continue;
			const FAcShatterLibrary::FDraw D = Library->MaterialFor(*Cut, Sub, this);
			const float E = Pose.Emission.IsValidIndex(Sub.Part) ? Pose.Emission[Sub.Part] : 1.f;
			UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
			Comp->SetupAttachment(RootComponent);
			Comp->SetStaticMesh(Sub.StaticMesh);
			if (D.Material) Comp->SetMaterial(0, D.Material);
			Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Comp->SetCustomPrimitiveDataFloat(AcModelData::TeamIndex, 0.f);
			Comp->SetCustomPrimitiveDataFloat(AcModelData::Fade, D.bDither ? 1.f - Opacity : 0.f);
			if (D.bEmber)
			{
				Comp->SetCustomPrimitiveDataFloat(AcModelData::EmissionScale, bWhole ? E : 0.f);
				Comp->SetCustomPrimitiveDataFloat(AcModelData::Char, Glow);
			}
			else
			{
				const float Dim = bWhole ? E : E * 0.12f;
				Comp->SetCustomPrimitiveDataFloat(AcModelData::EmissionScale, D.bDither ? Dim : Dim * Opacity);
			}
			Comp->SetWorldTransform(bWhole ? World[Sub.Part] : World[Sub.Part] * AcShatter::ChunkDelta(W, Sub.Chunk, Since));
			Comp->SetVisibility(Opacity > 0.f);
			Comp->RegisterComponent();
			Meshes.Add(Comp);
		}
		UE_LOG(LogAutocraft, Log, TEXT("shatter sheet: %s frame %d '%s' sec %.2f opacity %.2f glow %.2f"), *KindName, K,
			*F->GetStringField(TEXT("label")), Sec, Opacity, Glow);
	}

	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		if (It->IsA<AAcModelRow>() || It->ActorHasTag(TEXT("AcFloor"))) It->SetActorHiddenInGame(true);
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

void AAcShatterSheet::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Camera) return;
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		if (PC->GetViewTarget() != Camera) PC->SetViewTarget(Camera);
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

bool UAcShatterSheetSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return FCString::Strifind(FCommandLine::Get(), TEXT("-AcShatterSheet=")) != nullptr;
}

void UAcShatterSheetSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	if (!World.IsGameWorld()) return;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	World.SpawnActor<AAcShatterSheet>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
}
