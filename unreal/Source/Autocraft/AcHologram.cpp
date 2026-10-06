#include "AcHologram.h"

#include "AcHudStyle.h"
#include "AcLog.h"
#include "AcModelCatalog.h"
#include "AcSpace.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

#include <cmath>

namespace
{
	const TCHAR* const GlassPath = TEXT("/Game/Materials/M_Hologram.M_Hologram");
	const TCHAR* const PlatePath = TEXT("/Game/Materials/M_AcPointerAdd.M_AcPointerAdd");
	const TCHAR* const CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");

	/// Swift's `.renderingOrder` 10 (the tint; the depth copy, 9, is the
	/// custom depth pass here).
	constexpr int32 SortPriority = 10;

	void Quiet(UStaticMeshComponent* C)
	{
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetMobility(EComponentMobility::Movable);
		C->bReceivesDecals = false;
		C->SetAffectDistanceFieldLighting(false);
		C->bAffectDynamicIndirectLighting = false;
		C->bVisibleInRayTracing = false;
		C->bVisibleInReflectionCaptures = false;
		C->bVisibleInRealTimeSkyCaptures = false;
		C->bDisallowNanite = true;
	}
}

AAcHologram::AAcHologram()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Body = CreateDefaultSubobject<USceneComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Plate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Plate"));
	Plate->SetupAttachment(Root);
	SetCanBeDamaged(false);
}

AAcHologram* AAcHologram::Find(const UWorld* World)
{
	if (!World) return nullptr;
	TActorIterator<AAcHologram> It(const_cast<UWorld*>(World));
	return It ? *It : nullptr;
}

AAcHologram* AAcHologram::SpawnFor(UWorld* World)
{
	if (!World) return nullptr;
	if (AAcHologram* Have = Find(World)) return Have;
	FActorSpawnParameters P;
	P.Name = TEXT("AcHologram");
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAcHologram* H = World->SpawnActor<AAcHologram>(P);
	if (!H) return nullptr;
	Quiet(H->Plate);
	H->Plate->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, CubePath));
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, PlatePath))
	{
		H->PlateMaterial = UMaterialInstanceDynamic::Create(Base, H);
		H->PlateMaterial->SetScalarParameterValue(TEXT("Intensity"), 1.f);
		// `plateMaterial.transparency = 0.3`, added on.
		H->PlateMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.3f);
		H->PlateMaterial->SetScalarParameterValue(TEXT("Shape"), 0.f);
		H->Plate->SetMaterial(0, H->PlateMaterial);
	}
	else
	{
		UE_LOG(LogAutocraft, Error, TEXT("hologram: %s is missing (run Tools/Editor/make_pointer_materials.py)"), PlatePath);
	}
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, GlassPath))
	{
		H->Glass = UMaterialInstanceDynamic::Create(Base, H);
	}
	else
	{
		UE_LOG(LogAutocraft, Error, TEXT("hologram: %s is missing (run Tools/Editor/make_hologram_material.py)"), GlassPath);
	}
	H->Hide();
	return H;
}

void AAcHologram::Build(const FName Model)
{
	for (UStaticMeshComponent* C : Parts)
	{
		if (C) C->DestroyComponent();
	}
	Parts.Reset();
	Key = Model;
	const FAcModelInfo* Info = FAcModelCatalog::Get().Find(Model);
	if (!Info)
	{
		UE_LOG(LogAutocraft, Warning, TEXT("hologram: no model %s"), *Model.ToString());
		return;
	}
	// `model.clone()` with every node shown, no lights, no particles, no
	// shadow, one material.
	for (int32 P = 0; P < Info->Parts.Num(); ++P)
	{
		const FAcModelPart& Part = Info->Parts[P];
		const FTransform Rest = Info->RestToModel(P);
		for (const FAcModelMesh& M : Part.Meshes)
		{
			UStaticMesh* Mesh = M.LoadMesh();
			if (!Mesh) continue;
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
			Quiet(C);
			C->SetStaticMesh(Mesh);
			C->SetupAttachment(Body);
			C->SetRelativeTransform(Rest);
			C->TranslucencySortPriority = SortPriority;
			C->SetRenderCustomDepth(true);
			for (int32 S = 0; S < C->GetNumMaterials(); ++S) C->SetMaterial(S, Glass);
			C->RegisterComponent();
			Parts.Add(C);
		}
	}
	UE_LOG(LogAutocraft, Log, TEXT("hologram: %s, %d meshes"), *Model.ToString(), Parts.Num());
}

bool AAcHologram::Show(const FName Model, const FVector& At, const double Yaw, const double Radius, const bool bOk,
	const double Time)
{
	if (Key != Model) Build(Model);
	if (Parts.Num() == 0)
	{
		Hide();
		return false;
	}
	const double Cm = AcSpace::CmPerCell;
	Body->SetWorldLocationAndRotation(At + FVector(0, 0, 0.02 * Cm), FRotator(0, Yaw, 0));
	const FLinearColor C = bOk ? FAcHudStyle::World(0.25, 1, 0.45) : FAcHudStyle::World(1, 0.22, 0.15);
	const double Pulse = 0.85 + 0.15 * std::sin(Time * 5);
	if (Glass)
	{
		Glass->SetVectorParameterValue(TEXT("Color"), C);
		Glass->SetScalarParameterValue(TEXT("Pulse"), float(Pulse));
	}
	if (PlateMaterial) PlateMaterial->SetVectorParameterValue(TEXT("Color"), C);
	// `SCNBox(1, 0.02, 1)` scaled (2r, 1, 2r) at the ground + 0.04: the
	// engine cube is 1 m a side, centred.
	Plate->SetWorldLocationAndRotation(At + FVector(0, 0, 0.04 * Cm), FRotator::ZeroRotator);
	Plate->SetWorldScale3D(FVector(2 * Radius, 2 * Radius, 0.02));
	if (!bShown)
	{
		bShown = true;
		Body->SetVisibility(true, true);
		Plate->SetVisibility(true);
	}
	return true;
}

void AAcHologram::Hide()
{
	bShown = false;
	Body->SetVisibility(false, true);
	Plate->SetVisibility(false);
}
