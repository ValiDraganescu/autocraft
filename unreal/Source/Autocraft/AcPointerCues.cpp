#include "AcPointerCues.h"

#include "AcHudStyle.h"
#include "AcLog.h"
#include "AcSpace.h"

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

	/// A small mesh in Unreal space (cm), built into a transient UStaticMesh.
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
		/// A triangle facing along its vertices' normals: Unreal's front face
		/// is the one for which (b−a)×(c−a) points away from the viewer.
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

	/// `SCNTorus(ringRadius: 1, pipeRadius:)` lying flat (the ring in XY).
	FShape Torus(double Pipe, int32 RingSegments, int32 PipeSegments)
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
				S.Vertex((Out * float(Cm) + N * float(Pipe * Cm)), N, FVector2f(float(I) / RingSegments, float(J) / PipeSegments));
			}
		}
		const int32 Row = PipeSegments + 1;
		for (int32 I = 0; I < RingSegments; I++)
			for (int32 J = 0; J < PipeSegments; J++)
				S.Quad(I * Row + J, (I + 1) * Row + J, (I + 1) * Row + J + 1, I * Row + J + 1);
		return S;
	}

	void Box(FShape& S, const FVector3f& Center, const FVector3f& Half)
	{
		static const FVector3f Axes[3] = {FVector3f(1, 0, 0), FVector3f(0, 1, 0), FVector3f(0, 0, 1)};
		for (int32 A = 0; A < 3; A++)
		{
			for (const float Sign : {-1.f, 1.f})
			{
				const FVector3f N = Axes[A] * Sign;
				const FVector3f U = Axes[(A + 1) % 3], V = Axes[(A + 2) % 3];
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

	/// `Pointers.brackets`: four L corners on a unit square (arm 0.4, width
	/// 0.1, 0.04 tall, the corner at 0.85), one mesh.
	FShape MakeBrackets()
	{
		FShape S;
		const double Arm = 0.4, W = 0.1, At = 0.85, H = 0.04;
		for (int32 Q = 0; Q < 4; Q++)
		{
			const double A = Q * PI / 2;
			const double C = std::cos(A), Sn = std::sin(A);
			auto Turn = [&](double X, double Y) { return FVector3f(float((X * C - Y * Sn) * Cm), float((X * Sn + Y * C) * Cm), 0); };
			// Along one edge and along the other; turned by 90° the half
			// sizes swap.
			const bool bOdd = (Q % 2) == 1;
			for (const bool bAlong : {true, false})
			{
				const double Hx = (bAlong ? Arm : W) / 2, Hy = (bAlong ? W : Arm) / 2;
				const FVector3f Center = Turn(bAlong ? At - Arm / 2 : At, bAlong ? At : At - Arm / 2);
				const FVector3f Half = bOdd ? FVector3f(float(Hy * Cm), float(Hx * Cm), float(H / 2 * Cm))
				                            : FVector3f(float(Hx * Cm), float(Hy * Cm), float(H / 2 * Cm));
				Box(S, Center, Half);
			}
		}
		return S;
	}

	/// `SCNCylinder(radius: 1, height: 2.2)`, side only (the caps are
	/// transparent in Swift), standing on z = 0; v = 0 at the bottom.
	FShape Cylinder(int32 Segments, double Height)
	{
		FShape S;
		for (int32 I = 0; I <= Segments; I++)
		{
			const double U = 2 * PI * I / Segments;
			const FVector3f N(std::cos(U), std::sin(U), 0);
			S.Vertex(N * float(Cm), N, FVector2f(float(I) / Segments, 0));
			S.Vertex(N * float(Cm) + FVector3f(0, 0, float(Height * Cm)), N, FVector2f(float(I) / Segments, 1));
		}
		for (int32 I = 0; I < Segments; I++) S.Quad(2 * I, 2 * I + 2, 2 * I + 3, 2 * I + 1);
		return S;
	}

	/// A flat square `Side` cells across, facing up.
	FShape Plane(double Side)
	{
		FShape S;
		const float H = float(Side / 2 * Cm);
		const FVector3f N(0, 0, 1);
		const int32 A = S.Vertex(FVector3f(-H, -H, 0), N, FVector2f(0, 0));
		const int32 B = S.Vertex(FVector3f(H, -H, 0), N, FVector2f(1, 0));
		const int32 C = S.Vertex(FVector3f(H, H, 0), N, FVector2f(1, 1));
		const int32 D = S.Vertex(FVector3f(-H, H, 0), N, FVector2f(0, 1));
		S.Quad(A, B, C, D);
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
		Attr.GetPolygonGroupMaterialSlotNames()[Group] = FName("Cue");
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
		for (int32 I = 0; I + 2 < S.Index.Num(); I += 3)
		{
			MD.CreateTriangle(Group, {Ids[S.Index[I]], Ids[S.Index[I + 1]], Ids[S.Index[I + 2]]});
		}
		UStaticMesh* Mesh = NewObject<UStaticMesh>(Outer, Name, RF_Transient);
		Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, FName("Cue")));
		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bFastBuild = true;
		Params.bCommitMeshDescription = false;
		Params.bMarkPackageDirty = false;
		Params.bBuildSimpleCollision = false;
		Params.bAllowCpuAccess = false;
		Mesh->BuildFromMeshDescriptions({&MD}, Params);
		return Mesh;
	}

	const TCHAR* const MatTranslucent = TEXT("/Game/Materials/M_AcPointer.M_AcPointer");
	const TCHAR* const MatAdditive = TEXT("/Game/Materials/M_AcPointerAdd.M_AcPointerAdd");
	const TCHAR* const MatTop = TEXT("/Game/Materials/M_AcPointerTop.M_AcPointerTop");

	void SetParams(UStaticMeshComponent* C, const FLinearColor& Color, double Intensity, double Opacity, double Shape = -1)
	{
		UMaterialInstanceDynamic* M = C ? Cast<UMaterialInstanceDynamic>(C->GetMaterial(0)) : nullptr;
		if (!M) return;
		M->SetVectorParameterValue(TEXT("Color"), Color);
		M->SetScalarParameterValue(TEXT("Intensity"), float(Intensity));
		M->SetScalarParameterValue(TEXT("Opacity"), float(Opacity));
		if (Shape >= 0) M->SetScalarParameterValue(TEXT("Shape"), float(Shape));
	}

	void Place(USceneComponent* C, const FVector& At, double Yaw, const FVector& Scale)
	{
		C->SetWorldLocationAndRotation(At, FRotator(0, Yaw, 0));
		C->SetWorldScale3D(Scale);
	}
}

AAcPointerCues::AAcPointerCues()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	SetCanBeDamaged(false);
}

AAcPointerCues* AAcPointerCues::Find(const UWorld* World)
{
	if (!World) return nullptr;
	TActorIterator<AAcPointerCues> It(const_cast<UWorld*>(World));
	return It ? *It : nullptr;
}

AAcPointerCues* AAcPointerCues::SpawnFor(UWorld* World)
{
	if (!World) return nullptr;
	if (AAcPointerCues* Have = Find(World)) return Have;
	FActorSpawnParameters P;
	P.Name = TEXT("AcPointerCues");
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAcPointerCues* Cues = World->SpawnActor<AAcPointerCues>(P);
	if (Cues) Cues->Build();
	return Cues;
}

FLinearColor AAcPointerCues::UplinkColor()
{
	return FAcHudStyle::World(0.3, 0.88, 1);
}

FLinearColor AAcPointerCues::ToneColor(const EAcHoverTone Tone)
{
	switch (Tone)
	{
	case EAcHoverTone::Own: return FAcHudStyle::World(0.3, 1, 0.4);
	case EAcHoverTone::Enemy: return FAcHudStyle::World(1, 0.25, 0.2);
	default: return UplinkColor();
	}
}

UStaticMeshComponent* AAcPointerCues::Part(const TCHAR* Name, UStaticMesh* Mesh, const TCHAR* Material, const int32 SortPriority,
	USceneComponent* Parent)
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
	C->TranslucencySortPriority = SortPriority;
	C->SetupAttachment(Parent);
	C->RegisterComponent();
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, Material))
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, this);
		C->SetMaterial(0, M);
		Mids.Add(M);
	}
	else
	{
		UE_LOG(LogAutocraft, Error, TEXT("pointer: %s is missing (run Tools/Editor/make_pointer_materials.py)"), Material);
	}
	C->SetVisibility(false, true);
	return C;
}

void AAcPointerCues::Build()
{
	if (bBuilt) return;
	bBuilt = true;
	UStaticMesh* HoverTorus = Meshes.Add_GetRef(MakeMesh(this, TEXT("SM_AcHoverTorus"), Torus(0.022, 64, 5)));
	UStaticMesh* UplinkTorus = Meshes.Add_GetRef(MakeMesh(this, TEXT("SM_AcUplinkTorus"), Torus(0.045, 64, 6)));
	UStaticMesh* SelectTorus = Meshes.Add_GetRef(MakeMesh(this, TEXT("SM_AcSelectTorus"), Torus(0.035, 64, 6)));
	UStaticMesh* BracketMesh = Meshes.Add_GetRef(MakeMesh(this, TEXT("SM_AcBrackets"), MakeBrackets()));
	UStaticMesh* BeamMesh = Meshes.Add_GetRef(MakeMesh(this, TEXT("SM_AcBeam"), Cylinder(32, 2.2)));
	UStaticMesh* DiscMesh = Meshes.Add_GetRef(MakeMesh(this, TEXT("SM_AcDisc"), Plane(2.1)));

	// Rendering orders of `Pointers`: the hover ring 5, the disc and beam
	// 6, the brackets 20 (over the unit, like a sight locking on).
	HoverRing = Part(TEXT("HoverRing"), HoverTorus, MatTranslucent, 5, Root);
	Uplink = NewObject<USceneComponent>(this, TEXT("Uplink"));
	Uplink->SetupAttachment(Root);
	Uplink->RegisterComponent();
	Disc = Part(TEXT("UplinkDisc"), DiscMesh, MatAdditive, 6, Uplink);
	UplinkRing = Part(TEXT("UplinkRing"), UplinkTorus, MatTranslucent, 7, Uplink);
	Brackets = Part(TEXT("UplinkBrackets"), BracketMesh, MatTop, 20, Uplink);
	Beam = Part(TEXT("UplinkBeam"), BeamMesh, MatAdditive, 6, Uplink);
	SelectRing = Part(TEXT("SelectRing"), SelectTorus, MatTranslucent, 4, Root);

	SetParams(Disc, UplinkColor(), 1, 1, 2);
	SetParams(Beam, UplinkColor(), 1, 0.1, 1);
	SetParams(UplinkRing, UplinkColor(), 1.2, 1, 0);
	SetParams(Brackets, UplinkColor(), 1, 1, 0);
	SetParams(SelectRing, ToneColor(EAcHoverTone::Own), 1.4, 1, 0);
	SetParams(HoverRing, ToneColor(EAcHoverTone::Own), 1, 0.75, 0);
}

void AAcPointerCues::ShowHover(const TOptional<FAcHoverCue>& Cue, const double Time)
{
	if (!bBuilt) Build();
	if (!Cue)
	{
		HoverRing->SetVisibility(false);
		Uplink->SetVisibility(false, true);
		return;
	}
	const FVector Base = AcSpace::ToWorld(Cue->Position, Cue->Y + 0.07);
	const double R = Cue->Radius;
	if (Cue->Tone == EAcHoverTone::Drive)
	{
		HoverRing->SetVisibility(false);
		Uplink->SetVisibility(true, true);
		Uplink->SetWorldLocation(Base);
		const double Breathe = 0.5 + 0.5 * std::sin(Time * 5);
		Place(UplinkRing, Base, 0, FVector(R, R, 1));
		SetParams(UplinkRing, UplinkColor(), 1.2 + 0.8 * Breathe, 1);
		SetParams(Brackets, UplinkColor(), 1 + 0.6 * Breathe, 1);
		// The brackets close in from wide, then hold and turn. SceneKit's
		// yaw −1.1t about +Y is Unreal's +1.1t (AcSpace: yaw = −SceneKit y).
		const double Close = 1 + 0.18 * (0.5 + 0.5 * std::sin(Time * 2.6));
		Place(Brackets, Base, FMath::RadiansToDegrees(Time * 1.1), FVector(R * Close, R * Close, 1));
		Place(Beam, Base, 0, FVector(R * 0.85, R * 0.85, 1));
		Place(Disc, Base, 0, FVector(R, R, 1));
		SetParams(Disc, UplinkColor(), 1, 0.6 + 0.4 * Breathe);
		SetParams(Beam, UplinkColor(), 1, 0.1 + 0.08 * Breathe);
	}
	else
	{
		Uplink->SetVisibility(false, true);
		HoverRing->SetVisibility(true);
		Place(HoverRing, Base, 0, FVector(R, R, 1));
		SetParams(HoverRing, ToneColor(Cue->Tone), 1, 0.75);
	}
}

void AAcPointerCues::ShowSelection(const TOptional<ac::Vec2>& Position, const double GroundY, const double Radius, const double Time)
{
	if (!bBuilt) Build();
	if (!Position)
	{
		SelectRing->SetVisibility(false);
		return;
	}
	const double R = Radius * 1.42 + 0.25;
	SelectRing->SetVisibility(true);
	Place(SelectRing, AcSpace::ToWorld(*Position, GroundY + 0.08), -FMath::RadiansToDegrees(Time * 0.6), FVector(R, R, 1));
}
