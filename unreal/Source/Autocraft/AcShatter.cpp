// Chunk C5: the cut (Shatter.swift) and the flights (Effects+Shatter.swift).
// See AcShatter.h.
#include "AcShatter.h"

#include "AcEffectsDeaths.h"
#include "AcLog.h"
#include "AcModelCatalog.h"
#include "AcPose.h"
#include "AcSpace.h"

#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "Misc/CommandLine.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"

#include "Noise.h"

#include <cmath>

namespace AcShatterPrivate
{
	constexpr double Cm = AcSpace::CmPerCell;
	/// Swift's weld: 0.5 mm cells (here cm).
	constexpr double WeldCm = 0.05;
	constexpr double NoShadowCm = 10.0;
	const TCHAR* const MHull = TEXT("/Game/Materials/M_Hull.M_Hull");
	const TCHAR* const MEmissive = TEXT("/Game/Materials/M_Emissive.M_Emissive");
	const TCHAR* const MEmber = TEXT("/Game/Materials/M_AcEmber.M_AcEmber");

	TAutoConsoleVariable<int32> CVarFromCache(TEXT("ac.ShatterFromCache"), 0,
		TEXT("Cut wrecks from the baked triangle cache (Content/Shatter), as a cooked game does, even where the meshes' source data is at hand."));

	/// The source triangles of one model's opaque (part, mesh)s, baked by an
	/// editor run (`GetMeshDescription` is editor data) to
	/// `Content/Shatter/<model>.bin` for a cooked game, which stages that
	/// folder (DefaultGame.ini). Rewritten whenever the editor's meshes
	/// differ from it.
	struct FCacheEntry
	{
		int32 Part = INDEX_NONE, Mesh = INDEX_NONE;
		FString Path;
		TArray<FVector3f> P, N, T;
		TArray<float> S;
		TArray<FVector2f> UV;
		TArray<int32> Tri;

		friend FArchive& operator<<(FArchive& Ar, FCacheEntry& E)
		{
			Ar << E.Part << E.Mesh << E.Path;
			E.P.BulkSerialize(Ar);
			E.N.BulkSerialize(Ar);
			E.T.BulkSerialize(Ar);
			E.S.BulkSerialize(Ar);
			E.UV.BulkSerialize(Ar);
			E.Tri.BulkSerialize(Ar);
			return Ar;
		}
	};
	constexpr uint32 CacheMagic = 0x48534341; // 'ACSH'
	constexpr int32 CacheVersion = 2;

	FString CachePath(const FAcModelInfo& M)
	{
		return FPaths::ProjectContentDir() / TEXT("Shatter") / (M.Name.ToString() + TEXT(".bin"));
	}

	bool LoadCache(const FAcModelInfo& M, TArray<FCacheEntry>& Out)
	{
		TArray<uint8> File;
		if (!FFileHelper::LoadFileToArray(File, *CachePath(M), FILEREAD_Silent)) return false;
		FMemoryReader Head(File);
		uint32 Magic = 0;
		int32 Version = 0, Size = 0;
		Head << Magic << Version << Size;
		if (Head.IsError() || Magic != CacheMagic || Version != CacheVersion || Size <= 0) return false;
		const int32 At = int32(Head.Tell());
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(Size);
		if (!FCompression::UncompressMemory(NAME_Zlib, Bytes.GetData(), Size, File.GetData() + At, File.Num() - At)) return false;
		FMemoryReader Ar(Bytes);
		Ar << Out;
		return !Ar.IsError();
	}

	bool SaveCache(const FAcModelInfo& M, TArray<FCacheEntry>& Entries)
	{
		TArray<uint8> Bytes;
		FMemoryWriter Ar(Bytes);
		Ar << Entries;
		// Zlib: about a ninth of the raw triangles.
		int32 Packed = FCompression::CompressMemoryBound(NAME_Zlib, Bytes.Num());
		TArray<uint8> File;
		FMemoryWriter Head(File);
		uint32 Magic = CacheMagic;
		int32 Version = CacheVersion, Size = Bytes.Num();
		Head << Magic << Version << Size;
		const int32 At = File.Num();
		File.SetNumUninitialized(At + Packed);
		if (!FCompression::CompressMemory(NAME_Zlib, File.GetData() + At, Packed, Bytes.GetData(), Bytes.Num())) return false;
		File.SetNum(At + Packed);
		const FString Path = CachePath(M);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		return FFileHelper::SaveArrayToFile(File, *Path);
	}

	/// SceneKit `eulerAngles` → quaternion in SceneKit space (R = Rz·Ry·Rx).
	FQuat SkEuler(const FVector& E)
	{
		return FQuat(FVector(0, 0, 1), E.Z) * FQuat(FVector(0, 1, 0), E.Y) * FQuat(FVector(1, 0, 0), E.X);
	}

	FVector EulerOf(const FQuat& Sk)
	{
		const FVector X = Sk.RotateVector(FVector(1, 0, 0));
		const FVector Y = Sk.RotateVector(FVector(0, 1, 0));
		const FVector Z = Sk.RotateVector(FVector(0, 0, 1));
		return FVector(std::atan2(Y.Z, Z.Z), std::asin(FMath::Clamp(-X.Z, -1.0, 1.0)), std::atan2(X.Y, X.X));
	}

	/// A part's rest = its own SceneKit transform × the frames the export
	/// merged away (as AcEffectsDeaths.cpp / AcPoseVehicles.cpp).
	FMatrix ChainOf(const FAcModelInfo& M, const int32 I)
	{
		const FAcModelPart& Part = M.Parts[I];
		const FTransform Own = AcSpace::TransformFromSceneKit(Part.SkPosition, SkEuler(Part.SkEuler), Part.SkScale);
		return Own.ToMatrixWithScale().Inverse() * Part.Rest.ToMatrixWithScale();
	}

	FTransform SkLocal(const FAcModelInfo& M, const int32 I, const FVector& Pos, const FVector& Euler, const FVector& Scale)
	{
		const FTransform Own = AcSpace::TransformFromSceneKit(Pos, SkEuler(Euler), Scale);
		const FMatrix Chain = ChainOf(M, I);
		return Chain.Equals(FMatrix::Identity, 1e-3) ? Own : FTransform(Own.ToMatrixWithScale() * Chain);
	}

	void SkOf(const FAcModelInfo& M, const int32 I, const FTransform& Local, FVector& Pos, FVector& Euler)
	{
		const FMatrix Chain = ChainOf(M, I);
		const FTransform Own = Chain.Equals(FMatrix::Identity, 1e-3) ? Local : FTransform(Local.ToMatrixWithScale() * Chain.Inverse());
		Pos = AcSpace::ToSceneKit(Own.GetTranslation());
		Euler = EulerOf(AcSpace::QuatToSceneKit(Own.GetRotation()));
	}

	/// A UE-world box → SceneKit centre and size (cells).
	void SkBox(const FBox& B, FVector& Centre, FVector& Extent)
	{
		Centre = AcSpace::ToSceneKit(B.GetCenter());
		const FVector S = B.GetSize() / Cm;
		Extent = FVector(S.X, S.Z, S.Y);
	}

	double Bulk(const FVector& E) { return E.X * E.Y * E.Z; }
	double MaxOf(const FVector& E) { return FMath::Max3(E.X, E.Y, E.Z); }
	double MinOf(const FVector& E) { return FMath::Min3(E.X, E.Y, E.Z); }

	/// Swift: `simd_normalize(SIMD3<Float>(r5 - 0.5, (r6 - 0.5)·0.6, r7 - 0.5) + 1e-3)`.
	FVector AxisOf(const int64 Seed, const int32 K)
	{
		const FVector A(AcShatter::Random(Seed, K, 5) - 0.5 + 1e-3, (AcShatter::Random(Seed, K, 6) - 0.5) * 0.6 + 1e-3,
			AcShatter::Random(Seed, K, 7) - 0.5 + 1e-3);
		return A.GetSafeNormal(UE_SMALL_NUMBER, FVector(0, 1, 0));
	}

	/// Union-find with path halving.
	int32 FindRoot(TArray<int32>& Up, int32 I)
	{
		while (Up[I] != I)
		{
			Up[I] = Up[Up[I]];
			I = Up[I];
		}
		return I;
	}
}

// --- Small helpers ---------------------------------------------------------------

double AcShatter::Smoothstep(const double A, const double B, const double X)
{
	const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
	return T * T * (3.0 - 2.0 * T);
}

double AcShatter::Random(const int64 Seed, const int32 K, const double I)
{
	const double A = double(Seed) * 12.9898 + double(K) * 78.233 + I * 37.719;
	const double X = std::sin(A) * 43758.5453;
	return X - std::floor(X);
}

UTexture2D* AcShatter::EmberTexture()
{
	static TStrongObjectPtr<UTexture2D> Tex;
	if (Tex.IsValid()) return Tex.Get();
	constexpr int32 N = 128;
	TArray<uint8> Px;
	Px.SetNumUninitialized(N * N * 4);
	const ac::Noise Noise(77);
	for (int32 Y = 0; Y < N; ++Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			const double V = Noise.fbm(double(X) * 0.07, double(Y) * 0.07, 4);
			const double E = Smoothstep(0.02, 0.32, V);
			const int32 I = (Y * N + X) * 4;
			// BGRA; Swift's RGBA bytes: r 255e, g 255e(0.3+0.35e), b 255e·0.06.
			Px[I + 2] = uint8(255 * E);
			Px[I + 1] = uint8(255 * E * (0.3 + 0.35 * E));
			Px[I + 0] = uint8(255 * E * 0.06);
			Px[I + 3] = 255;
		}
	}
	UTexture2D* T = UTexture2D::CreateTransient(N, N, PF_B8G8R8A8, TEXT("T_AcEmber"), TConstArrayView64<uint8>(Px.GetData(), Px.Num()));
	if (!T) return nullptr;
	T->SRGB = true;
	T->AddressX = TA_Wrap;
	T->AddressY = TA_Wrap;
	T->UpdateResource();
	Tex.Reset(T);
	return T;
}

// --- Poses -------------------------------------------------------------------------

void AcShatter::PartWorlds(const FAcModelInfo& M, const FAcPose& Pose, TArray<FTransform>& World, TArray<bool>& Shown)
{
	const int32 N = M.Parts.Num();
	World.SetNum(N);
	Shown.SetNum(N);
	for (int32 P = 0; P < N; ++P)
	{
		const int32 Parent = M.Parts[P].Parent;
		const FTransform& L = Pose.Local.IsValidIndex(P) ? Pose.Local[P] : M.Parts[P].Rest;
		const bool bVis = !Pose.bHidden && (!Pose.Visible.IsValidIndex(P) || Pose.Visible[P]);
		if (Parent != INDEX_NONE)
		{
			World[P] = L * World[Parent];
			Shown[P] = bVis && Shown[Parent];
		}
		else
		{
			World[P] = L * Pose.Placement;
			Shown[P] = bVis;
		}
	}
}

void AcShatter::ShootDownPose(const FAcModelInfo& M, const FAcPose& Last, const int64 Seed, const double Sec, FAcPose& Out)
{
	using namespace AcShatterPrivate;
	Out.Placement = Last.Placement;
	Out.Local = Last.Local;
	Out.Visible = Last.Visible;
	Out.Emission = Last.Emission;
	Out.MeshEmission = Last.MeshEmission;
	Out.bHidden = false;
	const double A = FMath::Clamp(Sec / FallTime, 0.0, 1.0);
	const double Side = Seed % 2 == 0 ? 1.0 : -1.0;
	// The holder keeps the root's place and heading; the root spins in it
	// (SceneKit eulerAngles.y = side·(2.5a + 3a²); UE yaw is −y).
	const double Spin = Side * (2.5 * A + 3 * A * A);
	if (Out.Local.Num() > 0) Out.Local[0] = Out.Local[0] * FTransform(FQuat(FVector::UpVector, -Spin));
	const int32* Body = M.PartIndex.Find(FName(TEXT("body")));
	if (!Body || !Last.Local.IsValidIndex(*Body)) return;
	FVector Pos, Euler;
	SkOf(M, *Body, Last.Local[*Body], Pos, Euler);
	const double Start = Pos.Y;
	Pos.Y = Start - (Start - 0.35) * A * A;
	Euler.X += Side * 0.5 * A;
	Euler.Z -= 0.45 * A;
	Out.Local[*Body] = SkLocal(M, *Body, Pos, Euler, M.Parts[*Body].SkScale);
}

void AcShatter::AtlasFallPose(const FAcModelInfo& M, const FAcPose& Last, const double Sec, FAcPose& Out)
{
	Out.Placement = Last.Placement;
	Out.Local = Last.Local;
	Out.Visible = Last.Visible;
	Out.Emission = Last.Emission;
	Out.MeshEmission = Last.MeshEmission;
	Out.bHidden = false;
	auto Smooth = [](const double A, const double B, const double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	};
	auto Part = [&M](const TCHAR* Name) { const int32* I = M.PartIndex.Find(FName(Name)); return I ? *I : INDEX_NONE; };
	auto Turn = [&](const int32 I, const FQuat& Q)
	{
		if (I != INDEX_NONE && Out.Local.IsValidIndex(I)) Out.Local[I].SetRotation((Q * Last.Local[I].GetRotation()).GetNormalized());
	};
	// Down on its knees: the thighs swing forward, the shins fold back under,
	// the body comes down with them (the leg is 1.45 m, folded to a third).
	const double Kneel = Smooth(0.0, AtlasKneelTime, Sec);
	for (int32 K = 0; K < 2; ++K)
	{
		const double Side = K == 0 ? 0.0 : 0.06;  // the legs do not fold quite together
		const double Hip = 1.0 * Kneel + Side * Kneel, Knee = 1.75 * Kneel;
		Turn(Part(*FString::Printf(TEXT("hips_%d"), K)), FQuat(FVector(0, 1, 0), -Hip));
		Turn(Part(*FString::Printf(TEXT("knees_%d"), K)), FQuat(FVector(0, 1, 0), Knee));
		Turn(Part(*FString::Printf(TEXT("ankles_%d"), K)), FQuat(FVector(0, 1, 0), Hip - Knee));
	}
	if (const int32 B = Part(TEXT("body")); B != INDEX_NONE)
	{
		FVector Pos = Last.Local[B].GetTranslation();
		Pos.Z -= 82.0 * Kneel;
		Out.Local[B].SetTranslation(Pos);
		Turn(B, FQuat(FVector(0, 1, 0), 0.12 * Kneel));
	}
	// The cannons droop and the torso sags, then the whole machine goes over
	// its toes: slow to start, fast at the end, the weight going.
	const double Fall = Smooth(AtlasKneelTime, AtlasFallTime, Sec);
	Turn(Part(TEXT("torso")), FQuat(FVector(0, 1, 0), 0.25 * Fall));
	if (Out.Local.Num() > 0)
	{
		const FQuat Q(FVector(0, 1, 0), 1.5 * Fall * Fall);
		const FVector Toes(55.0, 0.0, 0.0);
		Out.Local[0] = Last.Local[0] * FTransform(Q, Toes - Q.RotateVector(Toes));
	}
}

// --- Flights -----------------------------------------------------------------------

void AcShatter::ChunkBoxes(const FModel& Cut, TConstArrayView<FTransform> PartWorld, TConstArrayView<bool> PartShown,
	TArray<FBox>& Out)
{
	Out.Init(FBox(ForceInit), Cut.Chunks);
	for (const FSub& S : Cut.Subs)
	{
		if (!PartShown.IsValidIndex(S.Part) || !PartShown[S.Part] || !S.Box.IsValid) continue;
		Out[S.Chunk] += S.Box.TransformBy(PartWorld[S.Part]);
	}
}

AcShatter::FWreck AcShatter::Launch(const FModel& Cut, TConstArrayView<FBox> Boxes, const double Size, const int64 Seed,
	TFunctionRef<double(double, double)> Ground)
{
	using namespace AcShatterPrivate;
	FWreck W;
	W.Kind = EKind::Launch;
	W.Size = Size;
	W.Life = 12;
	W.Chunks.SetNum(Cut.Chunks);
	auto R = [Seed](const int32 K, const double I) { return Random(Seed, K, I); };
	FVector Mid = FVector::ZeroVector;
	int32 Live = 0;
	for (int32 K = 0; K < Cut.Chunks; ++K)
	{
		FChunk& C = W.Chunks[K];
		if (!Boxes.IsValidIndex(K) || !Boxes[K].IsValid)
		{
			C.bEmpty = true;
			continue;
		}
		SkBox(Boxes[K], C.P0, C.Extent);
		C.bHull = K == Cut.HullChunk;
		Mid += C.P0;
		++Live;
	}
	if (Live == 0) return W;
	Mid /= double(Live);
	const FVector Blast(Mid.X, Mid.Y - 0.3 * Size, Mid.Z);
	// The two biggest after the hull trail fire.
	TArray<int32> Big;
	for (int32 K = 0; K < Cut.Chunks; ++K)
		if (!W.Chunks[K].bEmpty && !W.Chunks[K].bHull) Big.Add(K);
	Big.StableSort([&W](const int32 A, const int32 B) { return Bulk(W.Chunks[A].Extent) > Bulk(W.Chunks[B].Extent); });
	Big.SetNum(FMath::Min(2, Big.Num()));

	const double Root = std::sqrt(Size);
	for (int32 K = 0; K < Cut.Chunks; ++K)
	{
		FChunk& C = W.Chunks[K];
		if (C.bEmpty) continue;
		const FVector& P0 = C.P0;
		FVector2D Out(P0.X - Blast.X, P0.Z - Blast.Z);
		if (Out.Size() < 0.05)
		{
			const double A = R(K, 1) * 2 * UE_DOUBLE_PI;
			Out = FVector2D(std::cos(A), std::sin(A));
		}
		Out.Normalize();
		// Heavier parts fly slower and spin less.
		const double Light = 1 / (0.55 + 1.6 * MaxOf(C.Extent) / Size);
		if (C.bHull)
		{
			C.V = FVector(Out.X * 0.3, 2.3 * Root, Out.Y * 0.3);
			C.Spin = 0.9;
		}
		else
		{
			const double Speed = (1.6 + 2.2 * R(K, 2)) * Root * Light;
			C.V = FVector(Out.X * Speed, (3 + 3.2 * R(K, 3)) * Root * FMath::Min(1.2, Light + 0.3), Out.Y * Speed);
			C.Spin = (5 + 9 * R(K, 4)) * Light;
		}
		C.Axis = AxisOf(Seed, K);
		const double G0 = Ground(P0.X, P0.Z);
		// The hull keeps its height over the ground; a loose part lies low.
		const double Lift = C.bHull ? P0.Y - G0 : 0.35 * MinOf(C.Extent) + 0.02;
		double Rest = G0 + Lift, Land = 0;
		for (int32 I = 0; I < 2; ++I)
		{
			Land = (C.V.Y + std::sqrt(C.V.Y * C.V.Y + 19.6 * FMath::Max(0.0, P0.Y - Rest))) / 9.8;
			Rest = Ground(P0.X + C.V.X * Land, P0.Z + C.V.Z * Land) + Lift;
		}
		C.Rest = Rest;
		C.Land = Land;
		C.Hop = FMath::Min(2.0, 0.28 * std::abs(C.V.Y - 9.8 * Land));
		C.HopTime = 2 * C.Hop / 9.8;
		if (C.bHull || Big.Contains(K))
		{
			C.Fire = 1;
			C.FireRadius = C.bHull ? 0.45 * Size : 0.15 * Size;
			C.FireAt = FVector(0, C.Extent.Y * 0.3, 0);
			C.FireSize = (C.bHull ? 0.3 : 0.18) * Size;
			C.SmokeSize = (C.bHull ? 0.4 : 0.25) * Size;
		}
	}
	return W;
}

AcShatter::FWreck AcShatter::Fell(const FModel& Cut, TConstArrayView<FBox> Boxes, const ac::Vec2 At, const double G0,
	const double Height, const double Radius, TArray<FBlast> Blasts, const int64 Seed,
	TFunctionRef<double(double, double)> Ground)
{
	using namespace AcShatterPrivate;
	FWreck W;
	W.Kind = EKind::Fell;
	W.Life = 20;
	W.Height = Height;
	W.Radius = Radius;
	W.G0 = G0;
	W.Blasts = MoveTemp(Blasts);
	W.Chunks.SetNum(Cut.Chunks);
	auto R = [Seed](const int32 K, const double I) { return Random(Seed, K, I); };
	double Total = 0;
	for (int32 K = 0; K < Cut.Chunks; ++K)
	{
		FChunk& C = W.Chunks[K];
		if (!Boxes.IsValidIndex(K) || !Boxes[K].IsValid)
		{
			C.bEmpty = true;
			continue;
		}
		SkBox(Boxes[K], C.P0, C.Extent);
		Total += Bulk(C.Extent);
	}
	const auto Dist = [](const ac::Vec2 A, const ac::Vec2 B) { return std::hypot(A.x - B.x, A.y - B.y); };
	for (int32 K = 0; K < Cut.Chunks; ++K)
	{
		FChunk& C = W.Chunks[K];
		if (C.bEmpty) continue;
		const FVector& P0 = C.P0;
		const double Bottom = P0.Y - C.Extent.Y / 2;
		const double Wide = FMath::Max(C.Extent.X, C.Extent.Z);
		// On the ground and big: the shell.
		C.bShell = Bottom < G0 + 0.15 * Height && (Bulk(C.Extent) > 0.05 * Total || Wide > 0.9 * Radius);
		const ac::Vec2 Flat(P0.X, P0.Z);
		// The nearest blast breaks it off; the first (in the middle) takes
		// everything near the middle.
		int32 B = 0;
		for (int32 I = 1; I < W.Blasts.Num(); ++I)
			if (Dist(W.Blasts[I].At, Flat) < Dist(W.Blasts[B].At, Flat)) B = I;
		if (Dist(Flat, At) < 0.35 * Radius) B = 0;
		C.Release = W.Blasts.IsEmpty() ? 0 : W.Blasts[B].Delay + 0.05 * R(K, 8);
		if (C.bShell) continue;
		ac::Vec2 Out = Flat - (W.Blasts.IsEmpty() ? At : W.Blasts[B].At);
		if (Dist(Out, ac::Vec2(0, 0)) < 0.05) Out = Flat - At;
		if (Dist(Out, ac::Vec2(0, 0)) < 0.05)
		{
			const double A = R(K, 1) * 2 * UE_DOUBLE_PI;
			Out = ac::Vec2(std::cos(A), std::sin(A));
		}
		const double Len = Dist(Out, ac::Vec2(0, 0));
		Out = ac::Vec2(Out.x / Len, Out.y / Len);
		const double Light = 1 / (0.6 + 1.2 * MaxOf(C.Extent) / Radius);
		const double Up = (1.8 + 2.4 * R(K, 3)) * FMath::Min(1.3, Light + 0.4);
		// Land a little way past the walls, not on the roof.
		const double FallT = (Up + std::sqrt(Up * Up + 19.6 * FMath::Max(0.0, P0.Y - G0))) / 9.8;
		const double Clear = FMath::Max(0.0, Radius * (1.05 + 0.5 * R(K, 2)) - Dist(Flat, At));
		const double Speed = FMath::Max((0.8 + 1.8 * R(K, 2)) * Light, Clear / FMath::Max(FallT, 0.2));
		C.V = FVector(Out.x * Speed, Up, Out.y * Speed);
		C.Spin = (2 + 6 * R(K, 4)) * Light;
		C.Axis = AxisOf(Seed, K);
		const double Lift = 0.35 * MinOf(C.Extent) + 0.02;
		double Rest = G0 + Lift, Land = 0;
		for (int32 I = 0; I < 2; ++I)
		{
			Land = (Up + std::sqrt(Up * Up + 19.6 * FMath::Max(0.0, P0.Y - Rest))) / 9.8;
			Rest = Ground(P0.X + C.V.X * Land, P0.Z + C.V.Z * Land) + Lift;
		}
		C.Rest = Rest;
		C.Land = Land;
		C.Hop = FMath::Min(1.6, 0.25 * std::abs(Up - 9.8 * Land));
		C.HopTime = 2 * C.Hop / 9.8;
	}
	// Fire: in the biggest part of the shell, and trailing from the three
	// biggest loose parts.
	TArray<int32> ByBulk;
	for (int32 K = 0; K < Cut.Chunks; ++K)
		if (!W.Chunks[K].bEmpty) ByBulk.Add(K);
	ByBulk.StableSort([&W](const int32 A, const int32 B) { return Bulk(W.Chunks[A].Extent) > Bulk(W.Chunks[B].Extent); });
	int32 S = INDEX_NONE;
	for (const int32 K : ByBulk)
		if (W.Chunks[K].bShell)
		{
			S = K;
			break;
		}
	if (S == INDEX_NONE && ByBulk.Num() > 0) S = ByBulk[0];
	if (S != INDEX_NONE)
	{
		FChunk& C = W.Chunks[S];
		C.Fire = 1;
		C.FireRadius = Radius * 0.8;
		C.FireAt = FVector(0, C.Extent.Y * 0.35, 0);
		C.FireSize = 0.35 + 0.1 * Radius;
		C.SmokeSize = 0.45 + 0.15 * Radius;
	}
	int32 Loose = 0;
	for (const int32 K : ByBulk)
	{
		if (W.Chunks[K].bShell || Loose >= 3) continue;
		FChunk& C = W.Chunks[K];
		// `pieces[s].fire` may already be this one (no shell at all): the
		// loose fire replaces it there, as in Swift.
		C.Fire = 2;
		C.FireRadius = 0.2;
		C.FireAt = FVector::ZeroVector;
		C.FireSize = 0.2;
		C.SmokeSize = 0.3;
		++Loose;
	}
	return W;
}

namespace AcShatterPrivate
{
	/// A chunk's SceneKit position and rotation `Sec` after death.
	void Move(const AcShatter::FWreck& W, const AcShatter::FChunk& C, const double Sec, FVector& Pos, FQuat& Rot)
	{
		using namespace AcShatter;
		auto Fly = [&](const double T, const double Sink)
		{
			// In the air, then one bounce, then a skid to a stop.
			const double T1 = FMath::Min(T, C.Land);
			const double T2 = FMath::Clamp(T - C.Land, 0.0, C.HopTime);
			const double T3 = FMath::Max(0.0, T - C.Land - C.HopTime);
			const double Slide = 0.3 * T2 + 0.12 * (1 - std::exp(-T3 * 4));
			const double Y = T < C.Land ? C.P0.Y + C.V.Y * T1 - 4.9 * T1 * T1 : C.Rest + C.Hop * T2 - 4.9 * T2 * T2;
			Pos = FVector(C.P0.X + C.V.X * (T1 + Slide), FMath::Max(Y, C.Rest - 0.001) - Sink, C.P0.Z + C.V.Z * (T1 + Slide));
			Rot = FQuat(C.Axis, C.Spin * (T1 + 0.4 * T2));
		};
		if (W.Kind == EKind::Launch)
		{
			Fly(Sec, Smoothstep(9, W.Life, Sec) * 0.6 * W.Size);
			return;
		}
		const double Away = Smoothstep(16, W.Life, Sec);
		if (C.bShell || Sec < C.Release)
		{
			// Standing: shaken by every blast, then slumping into the ground.
			double Shake = 0;
			for (const FBlast& B : W.Blasts)
				if (Sec >= B.Delay) Shake += 0.07 * std::exp(-(Sec - B.Delay) * 9);
			const double Down = C.bShell ? Smoothstep(0.3, 3.2, Sec) * W.Height * 0.7 + Away * W.Height * 0.4 : 0.0;
			Pos = FVector(C.P0.X + Shake * std::sin(Sec * 61 + C.P0.X), C.P0.Y - Down, C.P0.Z + Shake * std::cos(Sec * 53 + C.P0.Z));
			Rot = FQuat::Identity;
			return;
		}
		Fly(Sec - C.Release, Away * 0.8);
	}
}

FTransform AcShatter::ChunkDelta(const FWreck& W, const int32 K, const double Sec)
{
	using namespace AcShatterPrivate;
	if (!W.Chunks.IsValidIndex(K)) return FTransform::Identity;
	const FChunk& C = W.Chunks[K];
	FVector Pos;
	FQuat Rot;
	Move(W, C, FMath::Max(0.0, Sec), Pos, Rot);
	const FQuat Q = AcSpace::QuatFromSceneKit(Rot.X, Rot.Y, Rot.Z, Rot.W);
	const FVector Mid = AcSpace::FromSceneKit(C.P0.X, C.P0.Y, C.P0.Z);
	return FTransform(Q, AcSpace::FromSceneKit(Pos.X, Pos.Y, Pos.Z) - Q.RotateVector(Mid));
}

FTransform AcShatter::FireFrame(const FWreck& W, const int32 K, const double Sec)
{
	const FTransform D = ChunkDelta(W, K, Sec);
	const FChunk& C = W.Chunks[K];
	const FVector At = C.P0 + C.FireAt;
	return FTransform(D.GetRotation(), D.TransformPosition(AcSpace::FromSceneKit(At.X, At.Y, At.Z)));
}

void AcShatter::FireRates(const FWreck& W, const int32 K, const double Sec, double& Fire, double& Smoke)
{
	Fire = Smoke = 0;
	const FChunk& C = W.Chunks[K];
	if (C.Fire == 0 || C.bEmpty) return;
	if (W.Kind == EKind::Launch)
	{
		if (C.bHull)
		{
			Fire = Sec < 6 ? 40 * W.Size * (1 - Sec / 7) : 0;
			Smoke = Sec < 9.5 ? 9 * W.Size : 0;
		}
		else
		{
			Fire = Sec < 2 ? 70 : 0;
			Smoke = Sec < 4 ? 22 : 0;
		}
		return;
	}
	if (C.Fire == 1)
	{
		Fire = Sec < 9 ? 90 * (1 - Sec / 10) : 0;
		Smoke = Sec < 16 ? 18 * (1 - Sec / 20) : 0;
	}
	else
	{
		const double T = Sec - C.Release;
		Fire = T > 0 && T < 2.5 ? 60 : 0;
		Smoke = T > 0 && T < 5 ? 18 : 0;
	}
}

double AcShatter::Glow(const FWreck& W, const double Sec)
{
	const double Flicker = 0.85 + 0.15 * std::sin(Sec * 23) * std::sin(Sec * 7);
	return W.Kind == EKind::Launch ? 1.3 * Flicker * std::pow(FMath::Max(0.0, 1 - Sec / 5.5), 1.5)
	                               : 1.4 * Flicker * std::pow(FMath::Max(0.0, 1 - Sec / 9), 1.5);
}

double AcShatter::Opacity(const FWreck& W, const double Sec)
{
	return 1 - Smoothstep(W.Life - 1.5, W.Life, Sec);
}

// --- The cut -----------------------------------------------------------------------

struct FAcShatterLibrary::FJob
{
	const FAcModelInfo* Model = nullptr;
	int32 Count = 8;
	TArray<FString> Strip;
	enum class EStage : uint8 { Read, Cutting, Building, Done } Stage = EStage::Read;

	/// One (part, mesh) source: its triangles as vertex instances.
	struct FSrc
	{
		int32 Part = INDEX_NONE, Mesh = INDEX_NONE;
		UStaticMesh* Asset = nullptr;
		/// Seeds a chunk (shown at rest); else only joins one.
		bool bSeed = true;
		/// No source data (cooked): the whole mesh is one piece.
		bool bWhole = false;
		FBox AssetBox = FBox(ForceInit);
		TArray<FVector3f> P, N, T;
		TArray<float> S;
		TArray<FVector2f> UV;
		TArray<int32> Tri;
	};
	TArray<FSrc> Srcs;
	TArray<FTransform> RestToModel;

	struct FOut
	{
		int32 Src = 0, Chunk = 0;
		bool bWhole = false;
		TArray<int32> Tris;
		TUniquePtr<FMeshDescription> MD;
		FBox Box = FBox(ForceInit);
	};
	TArray<FOut> Outs;
	int32 Chunks = 0, HullChunk = 0, Pieces = 0;
	int32 NextBuild = 0;
	UE::Tasks::TTask<void> Task;
	double StartedAt = 0;
	AcShatter::FModel Result;

	void Cut();
};

void FAcShatterLibrary::FJob::Cut()
{
	using namespace AcShatterPrivate;
	// Every piece: its part, bounds (part frame), model-space centre,
	// volume, and its triangles in each of the part's meshes. Swift welds
	// within one flattened geometry, which is a part with all its materials
	// (the export's one mesh per material), so the weld runs across a
	// part's meshes: a box whose caps are another material stays one piece.
	struct FPiece
	{
		int32 Part;
		FBox Box;
		FVector Centre;
		double Volume;
		bool bSeed;
		/// (source, its triangles in this piece, their box in the part frame).
		TArray<int32> Src;
		TArray<TArray<int32>> Tris;
		TArray<FBox> SrcBox;
	};
	TArray<FPiece> Ps;
	auto Add = [&](FPiece&& P)
	{
		const FBox M = P.Box.TransformBy(RestToModel[P.Part]);
		P.Centre = M.GetCenter();
		const FVector E = M.GetSize().ComponentMax(FVector(2.0));
		P.Volume = E.X * E.Y * E.Z;
		Ps.Add(MoveTemp(P));
	};
	TArray<int32> Order;
	for (int32 Si = 0; Si < Srcs.Num(); ++Si) Order.Add(Si);
	Order.StableSort([this](const int32 A, const int32 B) { return Srcs[A].Part < Srcs[B].Part; });
	for (int32 K = 0; K < Order.Num();)
	{
		// This part's sources [K, End).
		int32 End = K;
		while (End < Order.Num() && Srcs[Order[End]].Part == Srcs[Order[K]].Part) ++End;
		const int32 Part = Srcs[Order[K]].Part;
		const bool bSeed = Srcs[Order[K]].bSeed;
		TArray<int32> Base;
		int32 NV = 0;
		for (int32 I = K; I < End; ++I)
		{
			const FSrc& S = Srcs[Order[I]];
			Base.Add(NV);
			if (S.bWhole)
			{
				FPiece W{Part, S.AssetBox, FVector::ZeroVector, 0, bSeed};
				W.Src.Add(Order[I]);
				W.Tris.AddDefaulted();
				W.SrcBox.Add(S.AssetBox);
				Add(MoveTemp(W));
				continue;
			}
			NV += S.P.Num();
		}
		TArray<int32> Up;
		Up.SetNumUninitialized(NV);
		for (int32 I = 0; I < NV; ++I) Up[I] = I;
		auto Join = [&Up](const int32 A, const int32 B)
		{
			const int32 Ra = FindRoot(Up, A), Rb = FindRoot(Up, B);
			if (Ra != Rb) Up[Ra] = Rb;
		};
		// Vertices on the same spot are one (a cylinder keeps its caps).
		TMap<uint64, int32> Spot;
		Spot.Reserve(NV);
		for (int32 I = K; I < End; ++I)
		{
			const FSrc& S = Srcs[Order[I]];
			if (S.bWhole) continue;
			const int32 B0 = Base[I - K];
			for (int32 V = 0; V < S.P.Num(); ++V)
			{
				const FVector3f& Q = S.P[V];
				const uint64 X = uint64(int64(FMath::RoundToDouble(Q.X / WeldCm))) & 0x1FFFFF;
				const uint64 Y = uint64(int64(FMath::RoundToDouble(Q.Y / WeldCm))) & 0x1FFFFF;
				const uint64 Z = uint64(int64(FMath::RoundToDouble(Q.Z / WeldCm))) & 0x1FFFFF;
				const uint64 Key = X << 42 | Y << 21 | Z;
				if (const int32* J = Spot.Find(Key)) Join(B0 + V, *J);
				else Spot.Add(Key, B0 + V);
			}
			for (int32 T = 0; T + 2 < S.Tri.Num(); T += 3)
			{
				Join(B0 + S.Tri[T], B0 + S.Tri[T + 1]);
				Join(B0 + S.Tri[T], B0 + S.Tri[T + 2]);
			}
		}
		TMap<int32, int32> PieceOf;
		TArray<FPiece> Made;
		for (int32 I = K; I < End; ++I)
		{
			const FSrc& S = Srcs[Order[I]];
			if (S.bWhole) continue;
			const int32 B0 = Base[I - K];
			for (int32 T = 0; T + 2 < S.Tri.Num(); T += 3)
			{
				const int32 Root = FindRoot(Up, B0 + S.Tri[T]);
				int32* J = PieceOf.Find(Root);
				if (!J)
				{
					J = &PieceOf.Add(Root, Made.Num());
					Made.Add(FPiece{Part, FBox(ForceInit), FVector::ZeroVector, 0, bSeed});
				}
				FPiece& P = Made[*J];
				int32 Slot = P.Src.Find(Order[I]);
				if (Slot == INDEX_NONE)
				{
					Slot = P.Src.Add(Order[I]);
					P.Tris.AddDefaulted();
					P.SrcBox.Add(FBox(ForceInit));
				}
				for (int32 V = 0; V < 3; ++V)
				{
					const FVector Q(S.P[S.Tri[T + V]]);
					P.Box += Q;
					P.SrcBox[Slot] += Q;
				}
				P.Tris[Slot].Add(T / 3);
			}
		}
		for (FPiece& P : Made) Add(MoveTemp(P));
		K = End;
	}
	Pieces = Ps.Num();
	if (Ps.IsEmpty()) return;
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("AC_SHATTER_DEBUG")).Len() > 0)
	{
		for (int32 I = 0; I < Ps.Num(); ++I)
		{
			const FBox M = Ps[I].Box.TransformBy(RestToModel[Ps[I].Part]);
			int32 NT = 0;
			for (const TArray<int32>& T : Ps[I].Tris) NT += T.Num();
			UE_LOG(LogAutocraft, Log, TEXT("shatter piece %d srcs %d part %d tris %d min (%.2f %.2f %.2f) size (%.2f %.2f %.2f)"), I, Ps[I].Src.Num(),
				Ps[I].Part, NT, M.Min.X / 100, M.Min.Y / 100, M.Min.Z / 100, M.GetSize().X / 100, M.GetSize().Y / 100, M.GetSize().Z / 100);
		}
	}

	// Seeds: the largest part, then each time the part farthest from every
	// seed so far; every part joins its nearest seed.
	int32 Hull = INDEX_NONE;
	for (int32 I = 0; I < Ps.Num(); ++I)
		if (Ps[I].bSeed && (Hull == INDEX_NONE || Ps[I].Volume > Ps[Hull].Volume)) Hull = I;
	if (Hull == INDEX_NONE) Hull = 0;
	TArray<int32> Seeds = {Hull};
	TArray<double> Far;
	Far.SetNumUninitialized(Ps.Num());
	for (int32 J = 0; J < Ps.Num(); ++J) Far[J] = FVector::DistSquared(Ps[J].Centre, Ps[Hull].Centre);
	while (Seeds.Num() < Count)
	{
		int32 Best = INDEX_NONE;
		for (int32 J = 0; J < Ps.Num(); ++J)
			if (Ps[J].bSeed && (Best == INDEX_NONE || Far[J] > Far[Best])) Best = J;
		// 1e-4 cells² in cm².
		if (Best == INDEX_NONE || Far[Best] <= 1.0) break;
		Seeds.Add(Best);
		for (int32 J = 0; J < Ps.Num(); ++J) Far[J] = FMath::Min(Far[J], FVector::DistSquared(Ps[J].Centre, Ps[Best].Centre));
	}
	TArray<int32> GroupOf;
	GroupOf.SetNumUninitialized(Ps.Num());
	TArray<int32> Members;
	Members.Init(0, Seeds.Num());
	for (int32 J = 0; J < Ps.Num(); ++J)
	{
		int32 Best = 0;
		double D = TNumericLimits<double>::Max();
		for (int32 S = 0; S < Seeds.Num(); ++S)
		{
			const double E = FVector::DistSquared(Ps[J].Centre, Ps[Seeds[S]].Centre);
			if (E < D)
			{
				D = E;
				Best = S;
			}
		}
		GroupOf[J] = Best;
		++Members[Best];
	}
	// Every seed has itself, so no group is empty.
	Chunks = Seeds.Num();
	HullChunk = 0;

	// The subs: per source, per chunk.
	TMap<uint64, int32> OutOf;
	TArray<int32> ChunksOfSrc;
	ChunksOfSrc.Init(0, Srcs.Num());
	for (int32 J = 0; J < Ps.Num(); ++J)
	{
		for (int32 K = 0; K < Ps[J].Src.Num(); ++K)
		{
			const int32 Src = Ps[J].Src[K];
			const uint64 Key = uint64(Src) << 32 | uint32(GroupOf[J]);
			int32* O = OutOf.Find(Key);
			if (!O)
			{
				O = &OutOf.Add(Key, Outs.Num());
				FOut& New = Outs.AddDefaulted_GetRef();
				New.Src = Src;
				New.Chunk = GroupOf[J];
				++ChunksOfSrc[Src];
			}
			Outs[*O].Tris.Append(Ps[J].Tris[K]);
			Outs[*O].Box += Ps[J].SrcBox[K];
		}
	}
	// A mesh wholly in one chunk is drawn as itself; the rest are cut into
	// new meshes (vertex instances kept, tangents MikkT as the import's).
	for (FOut& O : Outs)
	{
		const FSrc& S = Srcs[O.Src];
		if (S.bWhole || ChunksOfSrc[O.Src] == 1)
		{
			O.bWhole = true;
			O.Box = S.bWhole ? S.AssetBox : O.Box;
			continue;
		}
		O.MD = MakeUnique<FMeshDescription>();
		FMeshDescription& MD = *O.MD;
		FStaticMeshAttributes Attr(MD);
		Attr.Register();
		TVertexAttributesRef<FVector3f> Pos = Attr.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Normals = Attr.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector3f> Tangents = Attr.GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<float> Signs = Attr.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
		UVs.SetNumChannels(1);
		const FPolygonGroupID Group = MD.CreatePolygonGroup();
		Attr.GetPolygonGroupMaterialSlotNames()[Group] = FName(TEXT("Mat"));
		TMap<int32, FVertexInstanceID> Made;
		Made.Reserve(O.Tris.Num() * 3);
		MD.ReserveNewVertices(O.Tris.Num() * 3);
		MD.ReserveNewVertexInstances(O.Tris.Num() * 3);
		MD.ReserveNewTriangles(O.Tris.Num());
		MD.ReserveNewPolygons(O.Tris.Num());
		bool bTangents = true;
		for (const int32 T : O.Tris)
		{
			FVertexInstanceID Ids[3];
			for (int32 V = 0; V < 3; ++V)
			{
				const int32 I = S.Tri[T * 3 + V];
				if (const FVertexInstanceID* Known = Made.Find(I))
				{
					Ids[V] = *Known;
					continue;
				}
				const FVertexID Vx = MD.CreateVertex();
				Pos[Vx] = S.P[I];
				const FVertexInstanceID VI = MD.CreateVertexInstance(Vx);
				Normals[VI] = S.N.IsValidIndex(I) ? S.N[I] : FVector3f(0, 0, 1);
				const FVector3f Tg = S.T.IsValidIndex(I) ? S.T[I] : FVector3f::ZeroVector;
				if (Tg.IsNearlyZero()) bTangents = false;
				Tangents[VI] = Tg;
				Signs[VI] = S.S.IsValidIndex(I) ? S.S[I] : 1.f;
				UVs.Set(VI, 0, S.UV.IsValidIndex(I) ? S.UV[I] : FVector2f::ZeroVector);
				Made.Add(I, VI);
				Ids[V] = VI;
			}
			MD.CreateTriangle(Group, {Ids[0], Ids[1], Ids[2]});
		}
		if (!bTangents) FStaticMeshOperations::ComputeMikktTangents(MD, true);
	}
}

FAcShatterLibrary::FAcShatterLibrary() = default;

FAcShatterLibrary::~FAcShatterLibrary()
{
	for (TUniquePtr<FJob>& J : Jobs)
		if (J->Stage == FJob::EStage::Cutting) J->Task.Wait();
}

void FAcShatterLibrary::Request(const FAcModelInfo& Model, const int32 Count, TArray<FString> Strip)
{
	if (JobOf.Contains(&Model)) return;
	TUniquePtr<FJob> J = MakeUnique<FJob>();
	J->Model = &Model;
	J->Count = FMath::Max(1, Count);
	J->Strip = MoveTemp(Strip);
	JobOf.Add(&Model, Jobs.Num());
	Jobs.Add(MoveTemp(J));
}

const AcShatter::FModel* FAcShatterLibrary::Find(const FAcModelInfo& Model) const
{
	const int32* I = JobOf.Find(&Model);
	if (!I) return nullptr;
	const FJob& J = *Jobs[*I];
	return J.Stage == FJob::EStage::Done && J.Result.Chunks > 0 ? &J.Result : nullptr;
}

bool FAcShatterLibrary::IsIdle() const
{
	for (const TUniquePtr<FJob>& J : Jobs)
		if (J->Stage != FJob::EStage::Done) return false;
	return true;
}

void FAcShatterLibrary::Read(FJob& J)
{
	const FAcModelInfo& M = *J.Model;
	J.StartedAt = FPlatformTime::Seconds();
	const int32 N = M.Parts.Num();
	J.Result.Model = &M;
	J.Result.Stripped.Init(false, N);
	TArray<bool> Shown;
	Shown.Init(true, N);
	J.RestToModel.SetNum(N);
	for (int32 P = 0; P < N; ++P)
	{
		const FAcModelPart& Part = M.Parts[P];
		J.RestToModel[P] = M.RestToModel(P);
		const FString Name = Part.Name.ToString();
		bool bStrip = Part.Parent != INDEX_NONE && J.Result.Stripped[Part.Parent];
		for (const FString& S : J.Strip) bStrip |= S.EndsWith(TEXT("_")) ? Name.StartsWith(S) : Name == S;
		J.Result.Stripped[P] = bStrip;
		Shown[P] = !Part.bHidden && (Part.Parent == INDEX_NONE || Shown[Part.Parent]);
	}
	using namespace AcShatterPrivate;
	const bool bForceCache = CVarFromCache.GetValueOnGameThread() != 0 || FParse::Param(FCommandLine::Get(), TEXT("AcShatterFromCache"));
	bool bCacheRead = false, bCacheLoaded = false;
	TArray<FCacheEntry> Cache, Fresh;
	int32 FromCache = 0, Whole = 0;
	for (int32 P = 0; P < N; ++P)
	{
		if (J.Result.Stripped[P]) continue;
		const FAcModelPart& Part = M.Parts[P];
		for (int32 K = 0; K < Part.Meshes.Num(); ++K)
		{
			const FAcModelMesh& X = Part.Meshes[K];
			// Glow sprites, smears, decals: not solid (Swift's `read`).
			if (X.Blend != TEXT("opaque")) continue;
			UStaticMesh* Mesh = X.LoadMesh();
			if (!Mesh) continue;
			FJob::FSrc& S = J.Srcs.AddDefaulted_GetRef();
			S.Part = P;
			S.Mesh = K;
			S.Asset = Mesh;
			S.bSeed = Shown[P];
			S.AssetBox = Mesh->GetBoundingBox();
			const FString MeshPath = X.Mesh.ToString();
			const FMeshDescription* MD = nullptr;
#if WITH_EDITORONLY_DATA
			if (!bForceCache) MD = Mesh->GetMeshDescription(0);
#endif
			if (!MD || MD->Triangles().Num() == 0)
			{
				// No source data (a cooked game): the baked triangles.
				if (!bCacheRead)
				{
					bCacheRead = true;
					bCacheLoaded = LoadCache(M, Cache);
				}
				const FCacheEntry* E = Cache.FindByPredicate([&](const FCacheEntry& C)
					{ return C.Part == P && C.Mesh == K && C.Path == MeshPath; });
				if (!E || E->Tri.IsEmpty())
				{
					S.bWhole = true;
					++Whole;
					continue;
				}
				S.P = E->P;
				S.N = E->N;
				S.T = E->T;
				S.S = E->S;
				S.UV = E->UV;
				S.Tri = E->Tri;
				++FromCache;
				continue;
			}
			FStaticMeshConstAttributes A(*MD);
			const TVertexAttributesConstRef<FVector3f> Pos = A.GetVertexPositions();
			const TVertexInstanceAttributesConstRef<FVector3f> Nrm = A.GetVertexInstanceNormals();
			const TVertexInstanceAttributesConstRef<FVector3f> Tan = A.GetVertexInstanceTangents();
			const TVertexInstanceAttributesConstRef<float> Sgn = A.GetVertexInstanceBinormalSigns();
			const TVertexInstanceAttributesConstRef<FVector2f> UV = A.GetVertexInstanceUVs();
			const bool bUV = UV.IsValid() && UV.GetNumChannels() > 0;
			TArray<int32> Index;
			Index.Init(INDEX_NONE, MD->VertexInstances().GetArraySize());
			const int32 NVI = MD->VertexInstances().Num();
			S.P.Reserve(NVI);
			S.N.Reserve(NVI);
			S.T.Reserve(NVI);
			S.S.Reserve(NVI);
			S.UV.Reserve(NVI);
			for (const FVertexInstanceID VI : MD->VertexInstances().GetElementIDs())
			{
				Index[VI.GetValue()] = S.P.Num();
				S.P.Add(Pos[MD->GetVertexInstanceVertex(VI)]);
				S.N.Add(Nrm.IsValid() ? Nrm[VI] : FVector3f(0, 0, 1));
				S.T.Add(Tan.IsValid() ? Tan[VI] : FVector3f::ZeroVector);
				S.S.Add(Sgn.IsValid() ? Sgn[VI] : 1.f);
				S.UV.Add(bUV ? UV.Get(VI, 0) : FVector2f::ZeroVector);
			}
			S.Tri.Reserve(MD->Triangles().Num() * 3);
			for (const FTriangleID T : MD->Triangles().GetElementIDs())
			{
				const TArrayView<const FVertexInstanceID> Vs = MD->GetTriangleVertexInstances(T);
				for (int32 V = 0; V < 3; ++V) S.Tri.Add(Index[Vs[V].GetValue()]);
			}
			FCacheEntry& E = Fresh.AddDefaulted_GetRef();
			E.Part = P;
			E.Mesh = K;
			E.Path = MeshPath;
			E.P = S.P;
			E.N = S.N;
			E.T = S.T;
			E.S = S.S;
			E.UV = S.UV;
			E.Tri = S.Tri;
		}
	}
	// An editor run keeps the baked triangles in step with its meshes.
	if (!Fresh.IsEmpty())
	{
		TArray<FCacheEntry> Old;
		bool bSame = LoadCache(M, Old) && Old.Num() == Fresh.Num();
		for (int32 I = 0; bSame && I < Fresh.Num(); ++I)
		{
			const FCacheEntry& A = Fresh[I];
			const FCacheEntry& B = Old[I];
			bSame = A.Part == B.Part && A.Mesh == B.Mesh && A.Path == B.Path && A.Tri == B.Tri && A.P == B.P && A.UV == B.UV;
		}
		if (!bSame)
		{
			const bool bSaved = SaveCache(M, Fresh);
			UE_LOG(LogAutocraft, Log, TEXT("shatter: %s %s %s (%d meshes)"), bSaved ? TEXT("baked") : TEXT("could not bake"),
				*M.Name.ToString(), *CachePath(M), Fresh.Num());
		}
	}
	if (FromCache > 0 || Whole > 0)
	{
		UE_LOG(LogAutocraft, Log, TEXT("shatter: %s read %d meshes from the baked cache%s, %d whole"), *M.Name.ToString(), FromCache,
			bCacheLoaded ? TEXT("") : TEXT(" (missing: run the game once in the editor)"), Whole);
	}
	J.Stage = FJob::EStage::Cutting;
	FJob* Raw = &J;
	J.Task = UE::Tasks::Launch(TEXT("AcShatterCut"), [Raw] { Raw->Cut(); });
}

void FAcShatterLibrary::Build(FJob& J, const double UntilSeconds)
{
	using namespace AcShatterPrivate;
	const FAcModelInfo& M = *J.Model;
	if (J.NextBuild == 0)
	{
		J.Result.Chunks = J.Chunks;
		J.Result.HullChunk = J.HullChunk;
		J.Result.Pieces = J.Pieces;
	}
	while (J.NextBuild < J.Outs.Num())
	{
		if (UntilSeconds > 0 && FPlatformTime::Seconds() > UntilSeconds) return;
		FJob::FOut& O = J.Outs[J.NextBuild++];
		const FJob::FSrc& S = J.Srcs[O.Src];
		AcShatter::FSub Sub;
		Sub.Part = S.Part;
		Sub.Mesh = S.Mesh;
		Sub.Chunk = O.Chunk;
		Sub.Triangles = O.bWhole ? S.Tri.Num() / 3 : O.Tris.Num();
		if (O.bWhole)
		{
			Sub.StaticMesh = S.Asset;
			Sub.Box = O.Box;
		}
		else
		{
			UMaterialInterface* Mat = M.Parts[S.Part].Meshes[S.Mesh].LoadMaterial();
			UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage(), NAME_None, RF_Transient);
			Mesh->GetStaticMaterials().Add(FStaticMaterial(Mat, FName(TEXT("Mat")), FName(TEXT("Mat"))));
			UStaticMesh::FBuildMeshDescriptionsParams Params;
			Params.bFastBuild = true;
			Params.bCommitMeshDescription = false;
			Params.bMarkPackageDirty = false;
			Params.bBuildSimpleCollision = false;
			Params.bAllowCpuAccess = false;
			Mesh->BuildFromMeshDescriptions({O.MD.Get()}, Params);
			O.MD.Reset();
			Keep.Emplace(Mesh);
			Sub.StaticMesh = Mesh;
			Sub.Box = O.Box;
		}
		const double Scale = J.RestToModel[S.Part].GetMaximumAxisScale();
		Sub.bShadow = Sub.Box.IsValid && Sub.Box.GetSize().GetMax() * Scale >= NoShadowCm;
		J.Result.Subs.Add(Sub);
	}
	// The lamps: whatever glows dims instead of charring (`Effects.char`).
	for (AcShatter::FSub& Sub : J.Result.Subs)
	{
		UMaterialInterface* Src = M.Parts[Sub.Part].Meshes[Sub.Mesh].LoadMaterial();
		const UMaterial* Base = Src ? Src->GetMaterial() : nullptr;
		const FString Path = Base ? Base->GetPathName() : FString();
		if (Path == MEmissive)
		{
			Sub.bGlow = true;
		}
		else if (Path == MHull)
		{
			UTexture* Tex = nullptr;
			FLinearColor E = FLinearColor::Black;
			Src->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("EmissiveTex")), Tex);
			Src->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("EmissiveColor")), E);
			const bool bTex = Tex && !Tex->GetName().Contains(TEXT("WhiteSquareTexture"));
			// NSColor brightness 0.05 (sRGB) ≈ 0.004 linear.
			Sub.bGlow = bTex || FMath::Max3(E.R, E.G, E.B) > 0.004f;
		}
		else
		{
			// Opal, mercury, glass: drawn as they are (dim with the lamps).
			Sub.bGlow = true;
		}
	}
	J.Srcs.Reset();
	J.Outs.Reset();
	J.Result.CutMs = (FPlatformTime::Seconds() - J.StartedAt) * 1000.0;
	J.Stage = FJob::EStage::Done;
	int32 Cut = 0;
	for (const AcShatter::FSub& Sub : J.Result.Subs) Cut += Sub.StaticMesh != M.Parts[Sub.Part].Meshes[Sub.Mesh].LoadMesh();
	UE_LOG(LogAutocraft, Log, TEXT("shatter: %s cut into %d chunks (%d pieces, %d subs, %d new meshes) in %.0f ms"),
		*M.Name.ToString(), J.Result.Chunks, J.Result.Pieces, J.Result.Subs.Num(), Cut, J.Result.CutMs);
}

void FAcShatterLibrary::Tick(const double BudgetMs)
{
	const double Until = FPlatformTime::Seconds() + BudgetMs / 1000.0;
	for (TUniquePtr<FJob>& J : Jobs)
	{
		if (FPlatformTime::Seconds() > Until) return;
		switch (J->Stage)
		{
		case FJob::EStage::Read:
			Read(*J);
			break;
		case FJob::EStage::Cutting:
			if (J->Task.IsCompleted())
			{
				J->Stage = FJob::EStage::Building;
				Build(*J, Until);
			}
			break;
		case FJob::EStage::Building:
			Build(*J, Until);
			break;
		default:
			break;
		}
	}
}

const AcShatter::FModel* FAcShatterLibrary::Finish(const FAcModelInfo& Model)
{
	const int32* I = JobOf.Find(&Model);
	if (!I) return nullptr;
	FJob& J = *Jobs[*I];
	if (J.Stage == FJob::EStage::Read) Read(J);
	if (J.Stage == FJob::EStage::Cutting)
	{
		J.Task.Wait();
		J.Stage = FJob::EStage::Building;
	}
	if (J.Stage == FJob::EStage::Building) Build(J, 0);
	return J.Result.Chunks > 0 ? &J.Result : nullptr;
}

FAcShatterLibrary::FDraw FAcShatterLibrary::MaterialFor(const AcShatter::FModel& Cut, const AcShatter::FSub& Sub, UObject* Outer)
{
	using namespace AcShatterPrivate;
	UMaterialInterface* Src = Cut.Model->Parts[Sub.Part].Meshes[Sub.Mesh].LoadMaterial();
	if (!Src) return {};
	if (const FDraw* Known = Materials.Find(Src)) return *Known;
	FDraw D;
	static TWeakObjectPtr<UMaterialInterface> Ember;
	if (!Ember.IsValid()) Ember = LoadObject<UMaterialInterface>(nullptr, MEmber);
	if (!Sub.bGlow && Ember.IsValid())
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Ember.Get(), Outer);
		if (UMaterialInstance* MI = Cast<UMaterialInstance>(Src)) Mid->CopyParameterOverrides(MI);
		if (UTexture2D* Tex = AcShatter::EmberTexture()) Mid->SetTextureParameterValue(TEXT("EmberTex"), Tex);
		// `Effects.char` sets SceneKit's `multiply` to 0.6 grey, replacing the
		// hull materials' tint (hullDark, team: `hullMaterial`'s multiply), so
		// a wreck's painted metal is the bare hull texture × 0.6. A textured
		// material's BaseColorTint is that multiply; a plain one's is its
		// diffuse colour, which stays.
		UTexture* BaseTex = nullptr;
		Src->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("BaseColorTex")), BaseTex);
		const bool bMultiply = BaseTex && !BaseTex->GetName().Contains(TEXT("WhiteSquareTexture"));
		Mid->SetScalarParameterValue(TEXT("CharUntint"), bMultiply ? 1.f : 0.f);
		D.Material = Mid;
		D.bEmber = true;
		D.bDither = true;
		Keep.Emplace(Mid);
	}
	else
	{
		const AcDeaths::FFadeMaterial F = AcDeaths::FadeMaterial(Src, Outer);
		D.Material = F.Material;
		D.bDither = F.bDither;
		if (F.Material != Src) Keep.Emplace(F.Material);
		if (!Sub.bGlow && !Ember.IsValid())
		{
			UE_LOG(LogAutocraft, Warning, TEXT("shatter: no M_AcEmber (run Tools/Editor/make_ember_material.py): wrecks are not charred"));
		}
	}
	Materials.Add(Src, D);
	return D;
}
