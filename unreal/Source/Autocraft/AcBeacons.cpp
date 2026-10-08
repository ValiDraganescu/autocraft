#include "AcBeacons.h"

#include "AcHudStyle.h"
#include "AcLog.h"
#include "AcSpace.h"
#include "AcTerrain.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

#include <cmath>

namespace
{
	constexpr double Cm = AcSpace::CmPerCell;
	const TCHAR* const MatAdditive = TEXT("/Game/Materials/M_AcPointerAdd.M_AcPointerAdd");

	/// A small mesh in Unreal space (cm).
	struct FShape
	{
		TArray<FVector3f> Pos, Normal;
		TArray<FVector2f> UV;
		TArray<int32> Index;

		int32 Vertex(const FVector3f& P, const FVector3f& N, const FVector2f& T)
		{
			Pos.Add(P);
			Normal.Add(N);
			UV.Add(T);
			return Pos.Num() - 1;
		}
		void Tri(int32 A, int32 B, int32 C)
		{
			const FVector3f N = Normal[A] + Normal[B] + Normal[C];
			const FVector3f X = FVector3f::CrossProduct(Pos[B] - Pos[A], Pos[C] - Pos[A]);
			if (FVector3f::DotProduct(X, N) > 0) Swap(B, C);
			Index.Append({A, B, C});
		}
		void Quad(int32 A, int32 B, int32 C, int32 D)
		{
			Tri(A, B, C);
			Tri(A, C, D);
		}
	};

	/// `SCNTorus(ringRadius:pipeRadius:)` lying flat.
	FShape Torus(double Ring, double Pipe, int32 RingSegments, int32 PipeSegments)
	{
		FShape S;
		for (int32 I = 0; I <= RingSegments; I++)
		{
			const double U = 2 * PI * I / RingSegments;
			const FVector3f Out(std::cos(U), std::sin(U), 0);
			for (int32 J = 0; J <= PipeSegments; J++)
			{
				const double V = 2 * PI * J / PipeSegments;
				const FVector3f N = Out * float(std::cos(V)) + FVector3f(0, 0, 1) * float(std::sin(V));
				S.Vertex(Out * float(Ring * Cm) + N * float(Pipe * Cm), N, FVector2f(float(I) / RingSegments, float(J) / PipeSegments));
			}
		}
		const int32 Row = PipeSegments + 1;
		for (int32 I = 0; I < RingSegments; I++)
			for (int32 J = 0; J < PipeSegments; J++) S.Quad(I * Row + J, (I + 1) * Row + J, (I + 1) * Row + J + 1, I * Row + J + 1);
		return S;
	}

	/// `SCNCylinder(radius:height:)` standing on z = 0, side and caps
	/// (double-sided in Swift: both faces of the side are drawn).
	FShape Cylinder(double Radius, double Height, int32 Segments)
	{
		FShape S;
		for (const float Side : {1.f, -1.f})
		{
			const int32 Base = S.Pos.Num();
			for (int32 I = 0; I <= Segments; I++)
			{
				const double U = 2 * PI * I / Segments;
				const FVector3f N(std::cos(U), std::sin(U), 0);
				S.Vertex(N * float(Radius * Cm), N * Side, FVector2f(float(I) / Segments, 0));
				S.Vertex(N * float(Radius * Cm) + FVector3f(0, 0, float(Height * Cm)), N * Side, FVector2f(float(I) / Segments, 1));
			}
			for (int32 I = 0; I < Segments; I++) S.Quad(Base + 2 * I, Base + 2 * I + 2, Base + 2 * I + 3, Base + 2 * I + 1);
		}
		for (const float Z : {0.f, 1.f})
		{
			const FVector3f N(0, 0, Z > 0 ? 1.f : -1.f);
			const int32 Mid = S.Vertex(FVector3f(0, 0, float(Z * Height * Cm)), N, FVector2f(0.5f, 0.5f));
			const int32 First = S.Pos.Num();
			for (int32 I = 0; I <= Segments; I++)
			{
				const double U = 2 * PI * I / Segments;
				S.Vertex(FVector3f(float(std::cos(U) * Radius * Cm), float(std::sin(U) * Radius * Cm), float(Z * Height * Cm)), N,
					FVector2f(0.5f, 0.5f));
			}
			for (int32 I = 0; I < Segments; I++) S.Tri(Mid, First + I, First + I + 1);
		}
		return S;
	}

	void Box(FShape& S, const FVector3f& Center, const FVector3f& Half, float Yaw)
	{
		const FVector3f Ax[3] = {FVector3f(std::cos(Yaw), std::sin(Yaw), 0), FVector3f(-std::sin(Yaw), std::cos(Yaw), 0), FVector3f(0, 0, 1)};
		for (int32 A = 0; A < 3; A++)
		{
			for (const float Sign : {-1.f, 1.f})
			{
				const FVector3f N = Ax[A] * Sign;
				const FVector3f U = Ax[(A + 1) % 3], V = Ax[(A + 2) % 3];
				const FVector3f C = Center + N * Half[A];
				const float Hu = Half[(A + 1) % 3], Hv = Half[(A + 2) % 3];
				const int32 I0 = S.Vertex(C - U * Hu - V * Hv, N, FVector2f(0, 0));
				const int32 I1 = S.Vertex(C + U * Hu - V * Hv, N, FVector2f(1, 0));
				const int32 I2 = S.Vertex(C + U * Hu + V * Hv, N, FVector2f(1, 1));
				const int32 I3 = S.Vertex(C - U * Hu + V * Hv, N, FVector2f(0, 1));
				S.Quad(I0, I1, I2, I3);
			}
		}
	}

	/// Four ticks on the ring: boxes 0.5 × 0.08 × 0.14 at radius 2.1, lying
	/// along the radius (`n.eulerAngles.y = -a`), 0.02 up.
	FShape Ticks()
	{
		FShape S;
		for (int32 K = 0; K < 4; K++)
		{
			const double A = K * PI / 2;
			const FVector3f C(float(std::cos(A) * 2.1 * Cm), float(std::sin(A) * 2.1 * Cm), float(0.02 * Cm));
			Box(S, C, FVector3f(float(0.25 * Cm), float(0.07 * Cm), float(0.04 * Cm)), float(A));
		}
		return S;
	}

	UStaticMesh* MakeMesh(UObject* Outer, const FName Name, const FShape& S)
	{
		FMeshDescription MD;
		FStaticMeshAttributes Attr(MD);
		Attr.Register();
		TVertexAttributesRef<FVector3f> Pos = Attr.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Normals = Attr.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
		UVs.SetNumChannels(1);
		const FPolygonGroupID Group = MD.CreatePolygonGroup();
		Attr.GetPolygonGroupMaterialSlotNames()[Group] = FName("Beacon");
		TArray<FVertexInstanceID> Ids;
		for (int32 I = 0; I < S.Pos.Num(); I++)
		{
			const FVertexID V = MD.CreateVertex();
			Pos[V] = S.Pos[I];
			const FVertexInstanceID VI = MD.CreateVertexInstance(V);
			Normals[VI] = S.Normal[I];
			UVs.Set(VI, 0, S.UV[I]);
			Ids.Add(VI);
		}
		for (int32 I = 0; I + 2 < S.Index.Num(); I += 3) MD.CreateTriangle(Group, {Ids[S.Index[I]], Ids[S.Index[I + 1]], Ids[S.Index[I + 2]]});
		UStaticMesh* Mesh = NewObject<UStaticMesh>(Outer, Name, RF_Transient);
		Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, FName("Beacon")));
		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bFastBuild = true;
		Params.bCommitMeshDescription = false;
		Params.bMarkPackageDirty = false;
		Params.bBuildSimpleCollision = false;
		Params.bAllowCpuAccess = false;
		Mesh->BuildFromMeshDescriptions({&MD}, Params);
		return Mesh;
	}

	/// SCNAction ping-pong: from 1 to `Low` over `Half` seconds and back, linear.
	double PingPong(double T, double Half, double From, double To)
	{
		const double Phase = std::fmod(T, 2 * Half) / Half;
		return Phase < 1 ? From + (To - From) * Phase : To + (From - To) * (Phase - 1);
	}
}

AAcBeacons::AAcBeacons()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	SetCanBeDamaged(false);
}

AAcBeacons* AAcBeacons::Find(const UWorld* World)
{
	if (!World) return nullptr;
	TActorIterator<AAcBeacons> It(const_cast<UWorld*>(World));
	return It ? *It : nullptr;
}

AAcBeacons* AAcBeacons::SpawnFor(UWorld* World)
{
	if (!World) return nullptr;
	if (AAcBeacons* Have = Find(World)) return Have;
	FActorSpawnParameters P;
	P.Name = TEXT("AcBeacons");
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<AAcBeacons>(P);
}

double AAcBeacons::Now() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetRealTimeSeconds() : 0.0;
}

void AAcBeacons::BuildMeshes()
{
	if (!Meshes.IsEmpty()) return;
	Meshes.Add(MakeMesh(this, TEXT("SM_AcBeaconBeam"), Cylinder(0.09, 7, 12)));
	Meshes.Add(MakeMesh(this, TEXT("SM_AcBeaconHalo"), Cylinder(0.32, 7, 16)));
	Meshes.Add(MakeMesh(this, TEXT("SM_AcBeaconRing"), Torus(1.5, 0.06, 48, 6)));
	Meshes.Add(MakeMesh(this, TEXT("SM_AcBeaconTicks"), Ticks()));
}

UStaticMeshComponent* AAcBeacons::Part(const FName Name, UStaticMesh* Mesh, USceneComponent* Parent, const FLinearColor& Color,
	const double Opacity, UMaterialInstanceDynamic** OutMaterial)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, Name);
	C->SetStaticMesh(Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	C->SetMobility(EComponentMobility::Movable);
	C->bReceivesDecals = false;
	C->SetAffectDistanceFieldLighting(false);
	C->bAffectDynamicIndirectLighting = false;
	C->bVisibleInRayTracing = false;
	C->bVisibleInReflectionCaptures = false;
	C->bVisibleInRealTimeSkyCaptures = false;
	C->TranslucencySortPriority = 6;  // `renderingOrder = 6`
	C->SetupAttachment(Parent);
	C->RegisterComponent();
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, MatAdditive))
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, this);
		M->SetVectorParameterValue(TEXT("Color"), Color);
		M->SetScalarParameterValue(TEXT("Intensity"), 1.0f);
		M->SetScalarParameterValue(TEXT("Opacity"), float(Opacity));
		M->SetScalarParameterValue(TEXT("Shape"), 0.0f);
		C->SetMaterial(0, M);
		Owned.Add(M);
		if (OutMaterial) *OutMaterial = M;
	}
	else
	{
		UE_LOG(LogAutocraft, Error, TEXT("beacons: %s is missing (run Tools/Editor/make_pointer_materials.py)"), MatAdditive);
	}
	Owned.Add(C);
	return C;
}

AAcBeacons::FBeacon AAcBeacons::Make(const int64 Id, const TOptional<ac::Objective::Kind> Kind)
{
	BuildMeshes();
	const FLinearColor Color = FAcHudStyle::HudToWorld(FAcCommandMapLogic::BeaconColor(Kind));
	const int32 N = Made++;
	FBeacon B;
	B.Kind = Kind;
	B.Born = Now();
	B.Root = NewObject<USceneComponent>(this, FName(*FString::Printf(TEXT("Beacon%d"), N)));
	B.Root->SetupAttachment(Root);
	B.Root->RegisterComponent();
	Owned.Add(B.Root);
	UMaterialInstanceDynamic* BeamMaterial = nullptr;
	B.Beam = Part(FName(*FString::Printf(TEXT("Beacon%dBeam"), N)), Meshes[0], B.Root, Color, 0.55, &BeamMaterial);
	B.BeamMaterial = BeamMaterial;
	B.Halo = Part(FName(*FString::Printf(TEXT("Beacon%dHalo"), N)), Meshes[1], B.Root, Color, 0.12);
	B.Ring = Part(FName(*FString::Printf(TEXT("Beacon%dRing"), N)), Meshes[2], B.Root, Color, 0.9);
	B.Ticks = Part(FName(*FString::Printf(TEXT("Beacon%dTicks"), N)), Meshes[3], B.Root, Color, 0.9);
	return B;
}

void AAcBeacons::Remove(FBeacon& B)
{
	for (USceneComponent* C : {(USceneComponent*)B.Beam, (USceneComponent*)B.Halo, (USceneComponent*)B.Ring, (USceneComponent*)B.Ticks,
		     (USceneComponent*)B.Root})
	{
		if (!C) continue;
		Owned.Remove(C);
		C->DestroyComponent();
	}
	if (B.BeamMaterial) Owned.Remove(B.BeamMaterial);
}

void AAcBeacons::Show(const TArray<FAcBeaconSpec>& List)
{
	TMap<int64, const FAcBeaconSpec*> Want;
	for (const FAcBeaconSpec& S : List) Want.Add(S.Id, &S);
	for (auto It = Beacons.CreateIterator(); It; ++It)
	{
		const FAcBeaconSpec* const* W = Want.Find(It.Key());
		if (!W || (*W)->Kind != It.Value().Kind)
		{
			Remove(It.Value());
			It.RemoveCurrent();
		}
	}
	const AAcTerrain* Terrain = AAcTerrain::Find(GetWorld());
	for (const FAcBeaconSpec& S : List)
	{
		FBeacon* B = Beacons.Find(S.Id);
		if (!B) B = &Beacons.Add(S.Id, Make(S.Id, S.Kind));
		const double Ground = Terrain ? Terrain->FieldHeight(S.At) : 0.0;
		B->Root->SetWorldLocation(AcSpace::ToWorld(S.At, Ground + 0.06));
	}
}

void AAcBeacons::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Beacons.IsEmpty()) return;
	const double T0 = Now();
	for (TPair<int64, FBeacon>& P : Beacons)
	{
		FBeacon& B = P.Value;
		const double T = T0 - B.Born;
		// The beam's opacity 1 → 0.45 → 1 (1.8 s), on its 0.55.
		if (B.BeamMaterial) B.BeamMaterial->SetScalarParameterValue(TEXT("Opacity"), float(0.55 * PingPong(T, 0.9, 1, 0.45)));
		// The ring 1 → 1.25 → 1.
		const double S = PingPong(T, 0.9, 1, 1.25);
		B.Ring->SetRelativeScale3D(FVector(S));
		// The ticks once round in 6 s: SceneKit's +y turn is Unreal's −yaw.
		B.Ticks->SetRelativeRotation(FRotator(0, -std::fmod(T / 6, 1.0) * 360, 0));
	}
}
