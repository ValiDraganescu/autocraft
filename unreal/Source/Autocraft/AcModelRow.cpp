#include "AcModelRow.h"

#include "AcLog.h"
#include "AcModelCatalog.h"
#include "AcShot.h"
#include "AcSpace.h"

#include "Camera/CameraActor.h"
#include "Dom/JsonObject.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ContentStreaming.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

namespace
{
	const TCHAR* const RowOrder[] = {
		TEXT("unit"), TEXT("building"), TEXT("construction"), TEXT("cockpit"),
		TEXT("resource"), TEXT("doodad"), TEXT("effect"),
	};

	/// Swift `--compare` turns everything but buildings by this about up.
	bool IsTurnedForCompare(const FAcModelInfo& M)
	{
		return M.Category == TEXT("unit") || M.Category == TEXT("cockpit") || M.Category == TEXT("effect");
	}
}

AAcModelRow::AAcModelRow()
{
	PrimaryActorTick.bCanEverTick = true;
	// Shot runs pass -AcPaused (the sim must not run): keep framing and settling.
	PrimaryActorTick.bTickEvenWhenPaused = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AAcModelRow::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Build();
}

USceneComponent* AAcModelRow::AddModel(const FAcModelInfo& Model, const FVector& Location, const int32 Team)
{
	USceneComponent* Holder = NewObject<USceneComponent>(this, NAME_None, RF_Transient);
	Holder->SetupAttachment(RootComponent);
	Holder->SetRelativeLocation(Location);
	Holder->RegisterComponent();
	Built.Add(Holder);

	TArray<USceneComponent*> PartComponents;
	PartComponents.SetNum(Model.Parts.Num());
	for (int32 I = 0; I < Model.Parts.Num(); ++I)
	{
		const FAcModelPart& P = Model.Parts[I];
		USceneComponent* Part = NewObject<USceneComponent>(this, NAME_None, RF_Transient);
		// Parts are listed parent first.
		Part->SetupAttachment(P.Parent != INDEX_NONE && PartComponents[P.Parent] ? PartComponents[P.Parent] : Holder);
		Part->SetRelativeTransform(P.Rest);
		Part->RegisterComponent();
		PartComponents[I] = Part;
		Built.Add(Part);
		for (const FAcModelMesh& X : P.Meshes)
		{
			UStaticMesh* Mesh = X.LoadMesh();
			if (!Mesh)
			{
				UE_LOG(LogAutocraft, Warning, TEXT("models: %s has no mesh %s"), *Model.Name.ToString(), *X.Mesh.ToString());
				continue;
			}
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
			C->SetupAttachment(Part);
			C->SetStaticMesh(Mesh);
			if (UMaterialInterface* Mat = X.LoadMaterial()) C->SetMaterial(0, Mat);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetCustomPrimitiveDataFloat(AcModelData::TeamIndex, float(Team));
			C->SetCustomPrimitiveDataFloat(AcModelData::EmissionScale, 1.f);
			C->RegisterComponent();
			Built.Add(C);
			PlacedMeshes.Add({C, I, X.Blend});
		}
	}
	if (!bShowHiddenParts)
	{
		for (int32 I = 0; I < Model.Parts.Num(); ++I)
		{
			if (Model.Parts[I].bHidden) PartComponents[I]->SetVisibility(false, true);
		}
	}
	Placed.Add({Model.Name, Holder});
	return Holder;
}

void AAcModelRow::Build()
{
	for (USceneComponent* C : Built)
	{
		if (C) C->DestroyComponent();
	}
	Built.Reset();
	Placed.Reset();
	PlacedMeshes.Reset();

	const FAcModelCatalog& Catalog = FAcModelCatalog::Get();
	TSet<FString> Only;
	{
		TArray<FString> Names;
		Models.ParseIntoArray(Names, TEXT(","));
		for (FString& N : Names) Only.Add(N.TrimStartAndEnd());
	}

	double Y = 0.0;
	auto PlaceRow = [&](const TArray<TPair<const FAcModelInfo*, int32>>& Row)
	{
		if (Row.IsEmpty()) return;
		double X = 0.0, Depth = 0.0;
		for (const TPair<const FAcModelInfo*, int32>& E : Row)
		{
			const FAcModelInfo& M = *E.Key;
			const double S = M.Parts.Num() ? M.Parts[0].Rest.GetScale3D().X : 1.0;
			const FVector Size = M.Bounds.GetSize() * S;
			AddModel(M, FVector(X - M.Bounds.Min.X * S, Y - M.Bounds.Min.Y * S, 0.0), E.Value);
			X += Size.X + Gap;
			Depth = FMath::Max(Depth, Size.Y);
		}
		Y += Depth + Gap * 2.0;
	};

	for (const TCHAR* Category : RowOrder)
	{
		TArray<TPair<const FAcModelInfo*, int32>> Row;
		for (const FAcModelInfo& M : Catalog.All())
		{
			if (M.Category != Category) continue;
			if (!bIncludeRed && M.Team == TEXT("red")) continue;
			if (!Only.IsEmpty() && !Only.Contains(M.Name.ToString())) continue;
			Row.Add({&M, M.ExportTeam()});
		}
		PlaceRow(Row);
	}
	if (bPaletteRow && Only.IsEmpty())
	{
		for (const TCHAR* Name : {TEXT("ranger_blue"), TEXT("citadel_blue")})
		{
			TArray<TPair<const FAcModelInfo*, int32>> Row;
			if (const FAcModelInfo* M = Catalog.Find(Name))
			{
				for (int32 Team = 0; Team < 8; ++Team) Row.Add({M, Team});
			}
			PlaceRow(Row);
		}
	}
}

void AAcModelRow::BeginPlay()
{
	Super::BeginPlay();
	// Placed in a level, the transient parts are not saved: build them here.
	if (Built.IsEmpty()) Build();
	FString Name;
	if (!FParse::Value(FCommandLine::Get(), TEXT("AcModelFocus="), Name)) return;
	int32 Team = -1;
	FParse::Value(FCommandLine::Get(), TEXT("AcModelTeam="), Team);
	if (Name == TEXT("row")) Overview();
	else Focus(FName(Name), Team);
}

void AAcModelRow::Overview()
{
	// The whole row from the front, 40° down, 60° wide.
	const FBox Box = GetComponentsBoundingBox(true);
	const FVector Forward = FRotator(-40.0, 90.0, 0.0).Vector();
	const double Distance = Box.GetSize().X * 0.5 / FMath::Tan(FMath::DegreesToRadians(30.0)) * 1.05;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FocusCamera = GetWorld()->SpawnActor<ACameraActor>(Box.GetCenter() - Forward * Distance, Forward.Rotation(), Params);
	FocusCamera->GetCameraComponent()->SetFieldOfView(60.f);
	FocusCamera->GetCameraComponent()->SetConstraintAspectRatio(false);
	if (UAcShotSubsystem* Shot = UAcShotSubsystem::Get(this))
	{
		Shot->Hold();
		bHolding = true;
	}
	UE_LOG(LogAutocraft, Log, TEXT("models: overview of %d models, bounds %s"), Placed.Num(), *Box.ToString());
}

void AAcModelRow::Focus(const FName Name, int32 Team)
{
	const FAcModelInfo* M = FAcModelCatalog::Get().Find(Name);
	if (!M)
	{
		UE_LOG(LogAutocraft, Error, TEXT("models: no model %s to focus"), *Name.ToString());
		return;
	}
	Models = Name.ToString();
	bPaletteRow = false;
	Build();
	if (Placed.IsEmpty()) return;
	USceneComponent* Holder = Placed[0].Holder;
	if (Team >= 0)
	{
		for (USceneComponent* C : Built)
		{
			if (UPrimitiveComponent* P = Cast<UPrimitiveComponent>(C)) P->SetCustomPrimitiveDataFloat(AcModelData::TeamIndex, float(Team));
		}
	}

	FString View;
	if (FParse::Value(FCommandLine::Get(), TEXT("AcModelView="), View) && !View.IsEmpty())
	{
		FString Paint = TEXT("clay");
		FParse::Value(FCommandLine::Get(), TEXT("AcModelPaint="), Paint);
		ApplyView(*M, View, Paint);
		return;
	}

	// ExportModels.compare: holder yaw -(π/2 + 0.6) about SceneKit Y for
	// units, the camera at (c.x, c.y + 0.55 d, c.z + d) looking at the
	// bounds' centre c, d = 3.6 r, r = max(half the diagonal, 0.3), FOV 30.
	Holder->SetRelativeLocation(FVector::ZeroVector);
	const double TurnSk = IsTurnedForCompare(*M) ? -(UE_DOUBLE_HALF_PI + 0.6) : 0.0;
	Holder->SetRelativeRotation(FRotator(0.0, -FMath::RadiansToDegrees(TurnSk), 0.0));
	const FVector Center = M->Bounds.GetCenter();
	const double Radius = FMath::Max(AcSpace::ToCells(M->Bounds.GetSize().Size()) / 2.0, 0.3);
	double DistanceScale = 1.0;
	FParse::Value(FCommandLine::Get(), TEXT("AcModelDistance="), DistanceScale);
	const double D = Radius * 3.6 * DistanceScale;
	const FVector Sk = AcSpace::ToSceneKit(Center);
	const FVector CamPos = AcSpace::FromSceneKit(Sk.X, Sk.Y + 0.55 * D, Sk.Z + D);
	const FVector SunPos = AcSpace::FromSceneKit(-4.0, 8.0, 6.0);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FocusCamera = GetWorld()->SpawnActor<ACameraActor>(CamPos, (Center - CamPos).Rotation(), Params);
	FocusVerticalFov = 30.0;  // SceneKit's FOV is vertical; Tick turns it horizontal
	FocusCamera->GetCameraComponent()->SetConstraintAspectRatio(false);
	// The compare render has no ground.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(TEXT("AcFloor")) && !FParse::Param(FCommandLine::Get(), TEXT("AcModelFloor"))) It->SetActorHiddenInGame(true);
	}
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		It->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		It->SetActorRotation((Center - SunPos).Rotation());
	}
	if (UAcShotSubsystem* Shot = UAcShotSubsystem::Get(this))
	{
		Shot->Hold();
		bHolding = true;
	}
	UE_LOG(LogAutocraft, Log, TEXT("models: focus %s (team %d), camera %s"), *Name.ToString(), Team, *CamPos.ToString());
}

namespace
{
	/// One panel of the stencil sheet: a model-axes basis in SceneKit space (as stencil.json writes it).
	struct FStencilView
	{
		const TCHAR* Name;       // the `view` key of stencil.json
		FVector ToCamera;        // SceneKit axes: x forward, y up, z the unit's right
		FVector Right, Up;
		bool bOrtho;
	};

	bool FindStencilView(const FString& Key, FStencilView& Out)
	{
		static const FStencilView Views[] = {
			{TEXT("front"), FVector(1, 0, 0), FVector(0, 0, -1), FVector(0, 1, 0), true},
			{TEXT("right"), FVector(0, 0, 1), FVector(1, 0, 0), FVector(0, 1, 0), true},
			{TEXT("back"), FVector(-1, 0, 0), FVector(0, 0, 1), FVector(0, 1, 0), true},
			{TEXT("left"), FVector(0, 0, -1), FVector(-1, 0, 0), FVector(0, 1, 0), true},
			{TEXT("top"), FVector(0, 1, 0), FVector(0, 0, 1), FVector(1, 0, 0), true},
			// The sheet's 3/4 view (the Kestrel's stencil.json numbers).
			{TEXT("threeQuarter"), FVector(0.7027283906936646, 0.38650062680244446, 0.5973191261291504),
				FVector(0.6476484537124634, 0, -0.7619393467903137), FVector(-0.2944900393486023, 0.9222891926765442, -0.2503165304660797), false},
		};
		const FString K = Key.Equals(TEXT("3q"), ESearchCase::IgnoreCase) ? FString(TEXT("threeQuarter")) : Key;
		for (const FStencilView& V : Views)
		{
			if (K.Equals(V.Name, ESearchCase::IgnoreCase)) { Out = V; return true; }
		}
		return false;
	}

	TSharedRef<FJsonValue> JsonVec(const FVector& V)
	{
		return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
			MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z)});
	}
}

void AAcModelRow::ApplyView(const FAcModelInfo& M, const FString& ViewKey, const FString& Paint)
{
	FStencilView V;
	if (!FindStencilView(ViewKey, V))
	{
		UE_LOG(LogAutocraft, Error, TEXT("models: -AcModelView=%s is not front, right, back, left, top or 3q"), *ViewKey);
		return;
	}
	const bool bParts = Paint.Equals(TEXT("parts"), ESearchCase::IgnoreCase);
	USceneComponent* Holder = Placed[0].Holder;
	Holder->SetRelativeLocation(FVector::ZeroVector);
	Holder->SetRelativeRotation(FRotator::ZeroRotator);

	// The paint: one material, a `Color` and a `Flat` (1: unlit, the colour as it is).
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_AcStencil.M_AcStencil"));
	if (!Base) UE_LOG(LogAutocraft, Error, TEXT("models: no /Game/Materials/M_AcStencil (run Tools/Editor/make_stencil_material.py)"));
	for (const FPlacedMesh& P : PlacedMeshes)
	{
		UStaticMeshComponent* C = P.Component.Get();
		if (!C) continue;
		if (P.Blend != TEXT("opaque")) { C->SetVisibility(false); continue; }  // halos and glass stay out of the clay
		if (!Base) continue;
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, this);
		FLinearColor Color(0.30f, 0.30f, 0.30f);
		if (bParts)
		{
			// A flat, saturated colour per part: hue by the golden angle, so neighbours differ.
			const float Hue = FMath::Fmod(float(P.Part) * 137.508f, 360.f);
			Color = FLinearColor::MakeFromHSV8(uint8(Hue / 360.f * 255.f), 200, 230).CopyWithNewOpacity(1.f);
		}
		Mid->SetVectorParameterValue(TEXT("Color"), Color);
		Mid->SetScalarParameterValue(TEXT("Flat"), bParts ? 1.f : 0.f);
		C->SetMaterial(0, Mid);
	}

	// The camera numbers, SceneKit's (cells, Y up), as stencil.json has them.
	const FVector Lo = AcSpace::ToSceneKit(M.Bounds.Min), Hi = AcSpace::ToSceneKit(M.Bounds.Max);
	const FVector SkMin = Lo.ComponentMin(Hi), SkMax = Lo.ComponentMax(Hi);
	const FVector SkCenter = (SkMin + SkMax) * 0.5;
	const double Longest = (SkMax - SkMin).GetMax();
	constexpr double Panel = 512.0;
	double Ppu = Panel / (1.2 * Longest);
	const FVector Center = M.Bounds.GetCenter();
	const FVector ToCam = AcSpace::AxesFromSceneKit(V.ToCamera.X, V.ToCamera.Y, V.ToCamera.Z).GetSafeNormal();
	const FVector Up = AcSpace::AxesFromSceneKit(V.Up.X, V.Up.Y, V.Up.Z).GetSafeNormal();
	const FVector Forward = -ToCam;
	double Distance = 100000.0;
	if (!V.bOrtho)
	{
		// FOV 30: the bounding sphere fills about 0.55 of the half angle; Ppu is the scale at the centre.
		const double R = M.Bounds.GetSize().Size() * 0.5;
		Distance = R / FMath::Sin(FMath::DegreesToRadians(15.0 * 0.55));
		Ppu = Panel * 0.5 / (AcSpace::ToCells(Distance) * FMath::Tan(FMath::DegreesToRadians(15.0)));
	}
	else
	{
		Distance = 1500.0;
	}
	const FVector CamPos = Center + ToCam * Distance;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FocusCamera = GetWorld()->SpawnActor<ACameraActor>(CamPos, FRotationMatrix::MakeFromXZ(Forward, Up).Rotator(), Params);
	UCameraComponent* Cam = FocusCamera->GetCameraComponent();
	Cam->SetConstraintAspectRatio(false);
	if (V.bOrtho)
	{
		Cam->SetProjectionMode(ECameraProjectionMode::Orthographic);
		Cam->SetOrthoWidth(float(Panel / Ppu * AcSpace::CmPerCell));
		Cam->SetOrthoNearClipPlane(1.f);
		Cam->SetOrthoFarClipPlane(float(Distance * 2.0));
		FocusVerticalFov = 0.0;
	}
	else
	{
		FocusVerticalFov = 30.0;
	}
	// No floor, a fixed light from the camera's upper left, and nothing else lit by the row's own sun.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(TEXT("AcFloor"))) It->SetActorHiddenInGame(true);
	}
	// The sky would show at the horizon of a side view: hide it (and its light), light the clay with two
	// directional lights instead (a key, below, and a dim fill from the other side).
	for (TActorIterator<ASkyAtmosphere> It(GetWorld()); It; ++It) It->SetActorHiddenInGame(true);
	for (TActorIterator<ASkyLight> It(GetWorld()); It; ++It) It->SetActorHiddenInGame(true);
	const FVector Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();  // UE: Y = Z x X
	const FVector LightDir = (Forward * 0.55 - Right * 0.45 - Up * 0.75).GetSafeNormal();
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		It->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		It->SetActorRotation(LightDir.Rotation());
	}
	{
		FActorSpawnParameters FillParams;
		FillParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector FillDir = (Forward * 0.5 + Right * 0.6 + Up * 0.45).GetSafeNormal();
		if (ADirectionalLight* Fill = GetWorld()->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FillDir.Rotation(), FillParams))
		{
			Fill->GetLightComponent()->SetMobility(EComponentMobility::Movable);
			Fill->GetLightComponent()->SetIntensity(3.f);
			Fill->GetLightComponent()->SetCastShadows(false);
		}
	}
	if (UAcShotSubsystem* Shot = UAcShotSubsystem::Get(this))
	{
		Shot->Hold();
		bHolding = true;
		// The camera numbers next to the picture.
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("view"), V.Name);
		O->SetStringField(TEXT("model"), M.Base.ToString());
		O->SetStringField(TEXT("paint"), bParts ? TEXT("parts") : TEXT("clay"));
		O->SetBoolField(TEXT("orthographic"), V.bOrtho);
		O->SetNumberField(TEXT("pixelsPerUnit"), Ppu);
		O->SetArrayField(TEXT("center"), JsonVec(SkCenter)->AsArray());
		O->SetArrayField(TEXT("right"), JsonVec(V.Right)->AsArray());
		O->SetArrayField(TEXT("up"), JsonVec(V.Up)->AsArray());
		O->SetArrayField(TEXT("toCamera"), JsonVec(V.ToCamera)->AsArray());
		O->SetArrayField(TEXT("boundsMin"), JsonVec(SkMin)->AsArray());
		O->SetArrayField(TEXT("boundsMax"), JsonVec(SkMax)->AsArray());
		// The row of the bounds' lowest point (the stencil's "ground line"), top left origin.
		O->SetNumberField(TEXT("groundRow"), Panel * 0.5 - (SkMin.Y - SkCenter.Y) * Ppu);
		FString Json;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Json);
		FJsonSerializer::Serialize(O, W);
		const FString Out = FPaths::ChangeExtension(Shot->GetPath(), TEXT("json"));
		FFileHelper::SaveStringToFile(Json, *Out);
	}
	UE_LOG(LogAutocraft, Log, TEXT("models: view %s of %s (%s), ppu %.2f, camera %s"), V.Name, *M.Name.ToString(),
		bParts ? TEXT("parts") : TEXT("clay"), Ppu, *CamPos.ToString());
}

void AAcModelRow::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!FocusCamera) return;
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (PC->GetViewTarget() != FocusCamera) PC->SetViewTarget(FocusCamera);
	}
	// The viewport's size is only known once it draws.
	if (FocusVerticalFov > 0.0 && GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
		if (Size.X > 0 && Size.Y > 0)
		{
			const double Half = FMath::DegreesToRadians(FocusVerticalFov / 2.0);
			FocusCamera->GetCameraComponent()->SetFieldOfView(float(FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(Half) * Size.X / Size.Y))));
		}
	}
	if (!bHolding) return;
	bool bBusy = false;
#if WITH_EDITOR
	// Shaders, and textures and meshes an uncooked run builds on load (they
	// draw as placeholders until done).
	bBusy = (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
		|| FAssetCompilingManager::Get().GetNumRemainingAssets() > 0;
#endif
	IStreamingManager::Get().StreamAllResources(0.0f);
	SettleFrames = bBusy ? 0 : SettleFrames + 1;
	// A few frames after the last shader lands, for TSR and Lumen to settle.
	if (SettleFrames > 90)
	{
		if (UAcShotSubsystem* Shot = UAcShotSubsystem::Get(this)) Shot->Release();
		bHolding = false;
		const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
		FIntPoint Size(0, 0);
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport) Size = GEngine->GameViewport->Viewport->GetSizeXY();
		UE_LOG(LogAutocraft, Log, TEXT("models: shot from %s, fov %.1f, viewport %dx%d"),
			PC && PC->PlayerCameraManager ? *PC->PlayerCameraManager->GetCameraLocation().ToString() : TEXT("?"),
			PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetFOVAngle() : 0.f, Size.X, Size.Y);
	}
}
