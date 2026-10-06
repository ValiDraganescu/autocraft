#include "AcPilotAids.h"

#include "AcDaylight.h"
#include "AcFog.h"
#include "AcLog.h"
#include "AcSpace.h"
#include "AcTerrain.h"

#include "Components/PostProcessComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

#include <cmath>

namespace
{
	TAutoConsoleVariable<int32> CVarPilotVeil(TEXT("ac.PilotVeil"), 1,
		TEXT("The fog veil round a driven unit (the far ground lost in the sky): 1 on (on a map with fog), 0 off."));
	TAutoConsoleVariable<float> CVarRingGain(TEXT("ac.RangeRingGain"), 1.0f,
		TEXT("Brightness of the driven unit's range ring (1: the Swift colours)."));

	const TCHAR* const RingMaterialPath = TEXT("/Game/Pilot/M_AcRangeRing.M_AcRangeRing");
	const TCHAR* const VeilMaterialPath = TEXT("/Game/Pilot/M_AcPilotVeil.M_AcPilotVeil");

	enum ESection : int32
	{
		Disc = 0,
		Edge = 1,
		MinEdge = 2
	};

	/// `RangeRing`'s colours (calibrated sRGB).
	const FVector3f Cyan(0.3f, 0.88f, 1.f), Red(1.f, 0.3f, 0.18f), Green(0.3f, 1.f, 0.45f);

	float ToLinear(const float C)
	{
		return C <= 0.04045f ? C / 12.92f : std::pow((C + 0.055f) / 1.055f, 2.4f);
	}

	/// `color.blended(withFraction: 1 − K, of: .black)` (the blend in the
	/// colour's own space), then to linear light.
	FLinearColor Dimmed(const FVector3f& C, const float K)
	{
		return FLinearColor(ToLinear(C.X * K), ToLinear(C.Y * K), ToLinear(C.Z * K), 1.f);
	}

	struct FMesh
	{
		TArray<FVector> Pos;
		TArray<int32> Index;
		TArray<FLinearColor> Color;
	};
}

AAcPilotAids::AAcPilotAids()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);
	Ring = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("RangeRing"));
	RootComponent = Ring;
	Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ring->SetCastShadow(false);
	Ring->SetGenerateOverlapEvents(false);
	Ring->bUseAsyncCooking = false;
	Ring->SetVisibility(false);
	Ring->bAffectDistanceFieldLighting = false;
	Ring->bAffectDynamicIndirectLighting = false;
	Ring->bVisibleInRayTracing = false;

	Post = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Veil"));
	Post->SetupAttachment(Ring);
	Post->bUnbound = true;
	Post->Priority = 1.0f;
	Post->bEnabled = false;
}

AAcPilotAids* AAcPilotAids::Find(const UWorld* World)
{
	if (!World) return nullptr;
	TActorIterator<AAcPilotAids> It(const_cast<UWorld*>(World));
	return It ? *It : nullptr;
}

AAcPilotAids* AAcPilotAids::SpawnFor(UWorld* World)
{
	if (!World) return nullptr;
	if (AAcPilotAids* Existing = Find(World)) return Existing;
	FActorSpawnParameters Params;
	Params.Name = TEXT("AcPilotAids");
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<AAcPilotAids>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
}

void AAcPilotAids::EnsureMaterials()
{
	if (bMaterials) return;
	bMaterials = true;
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, RingMaterialPath))
	{
		DiscMaterial = UMaterialInstanceDynamic::Create(Base, this, TEXT("RingDisc"));
		EdgeMaterial = UMaterialInstanceDynamic::Create(Base, this, TEXT("RingEdge"));
		MinMaterial = UMaterialInstanceDynamic::Create(Base, this, TEXT("RingMin"));
	}
	else
	{
		UE_LOG(LogAutocraft, Warning, TEXT("pilot: no %s (run Tools/Editor/make_pilot_materials.py)"), RingMaterialPath);
	}
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, VeilMaterialPath))
	{
		VeilMaterial = UMaterialInstanceDynamic::Create(Base, this, TEXT("VeilMaterial"));
		Post->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, VeilMaterial));
	}
	else
	{
		UE_LOG(LogAutocraft, Warning, TEXT("pilot: no %s (run Tools/Editor/make_pilot_materials.py)"), VeilMaterialPath);
	}
}

void AAcPilotAids::Update(const TOptional<FAcRangeCue>& Cue, const FVector& Body)
{
	EnsureMaterials();
	Veil(true, Body);
	if (!Cue)
	{
		Ring->SetVisibility(false);
		return;
	}
	const bool bMoved = !Built || ac::distance(Built->Center, Cue->Center) > 0.04 || std::abs(Built->Radius - Cue->Radius) > 0.01
		|| std::abs(Built->MinRadius - Cue->MinRadius) > 0.01 || std::abs(Built->Inner - Cue->Inner) > 0.01;
	if (bMoved) Rebuild(*Cue);
	Built->Tone = Cue->Tone;
	Built->bPending = Cue->bPending;
	Paint(*Cue);
	Ring->SetVisibility(true);
}

void AAcPilotAids::Hide()
{
	Ring->SetVisibility(false);
	Built.Reset();
	Veil(false, FVector::ZeroVector);
}

void AAcPilotAids::Veil(const bool bOn, const FVector& Body)
{
	const AAcFog* Fog = AAcFog::Find(GetWorld());
	const bool bShow = bOn && VeilMaterial && CVarPilotVeil.GetValueOnGameThread() != 0 && Fog && Fog->HasGrid();
	Post->bEnabled = bShow;
	if (!bShow) return;
	const AAcDaylight* Day = AAcDaylight::Find(GetWorld());
	const double Reach = Day ? Day->GetSkyTones().Reach : 110.0;
	VeilMaterial->SetVectorParameterValue(TEXT("Center"),
		FLinearColor(float(Body.X), float(Body.Y), float(Body.Z), float(AcSpace::ToCm(Reach))));
	// The haze rising from the horizon, as on the sky dome (B9).
	if (Day) VeilMaterial->SetVectorParameterValue(TEXT("Haze"), Day->GetHazeRise());
}

void AAcPilotAids::Paint(const FAcRangeCue& Cue)
{
	if (!DiscMaterial) return;
	const FVector3f C = Cue.Tone == FAcRangeCue::ETone::Idle ? Cyan : Cue.Tone == FAcRangeCue::ETone::Lock ? Red : Green;
	const float Dim = Cue.bPending ? 0.45f : 1.f;
	const float Lit = Cue.Tone == FAcRangeCue::ETone::Idle ? 0.6f : 1.f;
	const float Gain = CVarRingGain.GetValueOnGameThread();
	DiscMaterial->SetVectorParameterValue(TEXT("Color"), Dimmed(C, 0.55f * Lit * Dim) * Gain);
	EdgeMaterial->SetVectorParameterValue(TEXT("Color"), Dimmed(C, 0.8f * Lit * Dim) * Gain);
	MinMaterial->SetVectorParameterValue(TEXT("Color"), Dimmed(Red, 0.6f * Dim) * Gain);
	Ring->SetMeshSectionVisible(MinEdge, bMinShown && Cue.MinRadius > Cue.Inner);
}

void AAcPilotAids::Rebuild(const FAcRangeCue& Cue)
{
	Built = Cue;
	const AAcTerrain* Terrain = AAcTerrain::Find(GetWorld());
	auto Vertex = [Terrain](const ac::Vec2 P)
	{
		return AcSpace::ToWorld(P, (Terrain ? Terrain->FieldHeight(P) : 0.0) + 0.08);
	};
	const int32 Segments = FMath::Clamp(int32(Cue.Radius * 2 * UE_DOUBLE_PI / 0.35), 48, 160);

	// Rings from just past the unit out to the edge, draped over the
	// ground; the light grows toward the edge, so the middle stays clear
	// and the reach reads as a soft rim.
	FMesh D;
	{
		const double From = Cue.Inner + 0.25;
		const int32 Rings = FMath::Max(2, int32(std::ceil((Cue.Radius - From) / 0.6)) + 1);
		for (int32 K = 0; K < Rings; ++K)
		{
			const double F = double(K) / double(Rings - 1);
			const double R = From + (Cue.Radius - From) * F;
			const float Glow = K == 0 ? 0.f : float(0.05 + 0.12 * F * F * F);
			for (int32 I = 0; I <= Segments; ++I)
			{
				const double A = double(I) / double(Segments) * 2 * UE_DOUBLE_PI;
				D.Pos.Add(Vertex(Cue.Center + ac::Vec2(std::cos(A), std::sin(A)) * R));
				D.Color.Add(FLinearColor(Glow, Glow, Glow, 1.f));
			}
		}
		const int32 Row = Segments + 1;
		for (int32 K = 0; K < Rings - 1; ++K)
		{
			for (int32 I = 0; I < Segments; ++I)
			{
				const int32 A = K * Row + I, B = A + Row;
				D.Index.Append({A, B, A + 1, A + 1, B, B + 1});
			}
		}
	}
	// A thin band at `Radius`, draped over the ground.
	auto Ribbon = [&](const double Radius, const double Width)
	{
		FMesh M;
		for (int32 I = 0; I <= Segments; ++I)
		{
			const double A = double(I) / double(Segments) * 2 * UE_DOUBLE_PI;
			const ac::Vec2 Dir(std::cos(A), std::sin(A));
			for (const double R : {Radius - Width / 2, Radius + Width / 2})
			{
				M.Pos.Add(Vertex(Cue.Center + Dir * R));
				M.Color.Add(FLinearColor::White);
			}
			if (I < Segments)
			{
				const int32 K = I * 2;
				M.Index.Append({K, K + 1, K + 2, K + 1, K + 3, K + 2});
			}
		}
		return M;
	};
	const FMesh E = Ribbon(Cue.Radius, 0.08);
	auto Make = [this](const int32 Section, const FMesh& M, UMaterialInterface* Material)
	{
		Ring->CreateMeshSection_LinearColor(Section, M.Pos, M.Index, {}, {}, M.Color, {}, false);
		if (Material) Ring->SetMaterial(Section, Material);
	};
	Make(Disc, D, DiscMaterial);
	Make(Edge, E, EdgeMaterial);
	bMinShown = Cue.MinRadius > Cue.Inner;
	if (bMinShown) Make(MinEdge, Ribbon(Cue.MinRadius, 0.06), MinMaterial);
	else if (Ring->GetNumSections() > MinEdge) Ring->ClearMeshSection(MinEdge);
}
