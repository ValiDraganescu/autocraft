// Automation tests for the rays (AcRays.h, AcRaysWorld.h) against what the
// Swift ray code computes: core-tests/golden/rays.json, written by
//   AUTOCRAFT_RAYS_GOLDEN=$PWD/unreal/core-tests/golden/rays.json \
//     .build/release/Autocraft export-models <scratch dir>
// (Sources/Autocraft/ExportModels+Rays.swift). Run headless:
//   UnrealEditor Autocraft.uproject -nullrhi -nosound -unattended -nosplash
//     -ExecCmds="Automation RunTests Autocraft.Rays; Quit"
//
// - Boxes: `RayBox.hit` on every SceneKit primitive (box, sphere, cylinder,
//   tube, capsule, cone, torus, pyramid, plane) under random transforms,
//   mirrored ones included.
// - Ground: `HeightGrid.height` and `crossing` on Highlands · Medium.
// - Models: `RayShape.hit` on every exported unit, building, ore deposit,
//   well and doodad at its rest pose, placed somewhere; the Unreal side goes
//   the runtime's way (catalog matrices → FTransforms, composed as the
//   renderer composes a pose, back to SceneKit matrices).
#include "AcModelCatalog.h"
#include "AcRays.h"
#include "AcRaysWorld.h"

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "TerrainField.h"
#include "WindowMaps.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/// How close a `t` must be to Swift's. Float math in another order (the
	/// inverse, dot products) moves it by a few ulps of the coordinates.
	constexpr float TTolerance = 2e-4f;

	TSharedPtr<FJsonObject> Golden()
	{
		static TSharedPtr<FJsonObject> Root = [] {
			TSharedPtr<FJsonObject> J;
			FString Text;
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("core-tests/golden/rays.json"));
			if (FFileHelper::LoadFileToString(Text, *Path))
			{
				FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J);
			}
			return J;
		}();
		return Root;
	}

	FAcF3 F3(const TSharedPtr<FJsonValue>& V)
	{
		const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
		return {float(A[0]->AsNumber()), float(A[1]->AsNumber()), float(A[2]->AsNumber())};
	}

	std::optional<float> OptT(const TSharedPtr<FJsonValue>& V)
	{
		if (!V.IsValid() || V->IsNull()) return std::nullopt;
		return float(V->AsNumber());
	}

	TArray<double> Doubles(const TArray<TSharedPtr<FJsonValue>>& A)
	{
		TArray<double> Out;
		for (const TSharedPtr<FJsonValue>& V : A) Out.Add(V->AsNumber());
		return Out;
	}

	/// Compares one ray's result; counts and describes the mismatches.
	struct FTally
	{
		int32 Cases = 0, Hits = 0, Mismatches = 0;
		float Worst = 0;
		TArray<FString> First;

		void Check(const std::optional<float> Ours, const std::optional<float> Swift, const FString& What)
		{
			Cases++;
			if (Swift) Hits++;
			const bool bSame = Ours.has_value() == Swift.has_value() && (!Ours || FMath::Abs(*Ours - *Swift) <= TTolerance);
			if (Ours && Swift) Worst = FMath::Max(Worst, FMath::Abs(*Ours - *Swift));
			if (bSame) return;
			Mismatches++;
			if (First.Num() < 8)
			{
				First.Add(FString::Printf(TEXT("%s: ours %s, Swift %s"), *What, Ours ? *FString::Printf(TEXT("%.7f"), *Ours) : TEXT("miss"),
					Swift ? *FString::Printf(TEXT("%.7f"), *Swift) : TEXT("miss")));
			}
		}

		void Report(FAutomationTestBase& Test, const TCHAR* Name) const
		{
			Test.AddInfo(FString::Printf(TEXT("%s: %d rays (%d hit in Swift), %d mismatches, worst |dt| %.2g"), Name, Cases, Hits, Mismatches, Worst));
			for (const FString& S : First) Test.AddError(S);
			Test.TestTrue(FString::Printf(TEXT("%s: every ray as in Swift"), Name), Mismatches == 0 && Cases > 0);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcRaysBoxesTest, "Autocraft.Rays.Boxes", Flags)
bool FAcRaysBoxesTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> G = Golden();
	if (!TestTrue(TEXT("golden rays.json loads"), G.IsValid())) return false;
	FTally Tally;
	for (const TSharedPtr<FJsonValue>& CaseV : G->GetArrayField(TEXT("boxes")))
	{
		const TSharedPtr<FJsonObject> C = CaseV->AsObject();
		FAcRayShape::FPiece Piece;
		if (!TestTrue(TEXT("shape parses"), FAcRayShapes::ParsePiece(*C->GetField<EJson::Object>(TEXT("shape")), 0, Piece))) continue;
		const TArray<TSharedPtr<FJsonValue>>& A = C->GetArrayField(TEXT("a"));
		const TArray<TSharedPtr<FJsonValue>>& B = C->GetArrayField(TEXT("b"));
		const TArray<TSharedPtr<FJsonValue>>& T = C->GetArrayField(TEXT("t"));
		for (int32 K = 0; K < T.Num(); ++K)
		{
			Tally.Check(Piece.Box.Hit(Piece.At, F3(A[K]), F3(B[K])), OptT(T[K]), C->GetStringField(TEXT("geometry")) + FString::Printf(TEXT(" ray %d"), K));
		}
	}
	Tally.Report(*this, TEXT("primitives"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcRaysGroundTest, "Autocraft.Rays.Ground", Flags)
bool FAcRaysGroundTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> G = Golden();
	if (!TestTrue(TEXT("golden rays.json loads"), G.IsValid())) return false;
	const TSharedPtr<FJsonObject> J = G->GetObjectField(TEXT("ground"));
	TestEqual(TEXT("the golden's map"), J->GetStringField(TEXT("style")) + TEXT(" ") + J->GetStringField(TEXT("size")), FString(TEXT("highlands medium")));
	const ac::TerrainField Field(ac::WindowMaps::build(ac::MapChoice{ac::MapStyle::highlands, ac::MapSize::medium}));
	const FAcHeightGrid Grid(Field, int(J->GetNumberField(TEXT("density"))));

	const TArray<TSharedPtr<FJsonValue>>& Points = J->GetArrayField(TEXT("points"));
	const TArray<TSharedPtr<FJsonValue>>& Heights = J->GetArrayField(TEXT("heights"));
	int32 Off = 0;
	double Worst = 0;
	for (int32 K = 0; K < Points.Num(); ++K)
	{
		const TArray<double> P = Doubles(Points[K]->AsArray());
		const double D = FMath::Abs(Grid.height(ac::Vec2(P[0], P[1])) - Heights[K]->AsNumber());
		Worst = FMath::Max(Worst, D);
		if (D > 1e-6 && Off++ < 5) AddError(FString::Printf(TEXT("height at (%.4f, %.4f): off by %g"), P[0], P[1], D));
	}
	AddInfo(FString::Printf(TEXT("heights: %d points, worst %g"), Points.Num(), Worst));
	TestEqual(TEXT("every height as in Swift"), Off, 0);

	FTally Tally;
	const TArray<TSharedPtr<FJsonValue>>& A = J->GetArrayField(TEXT("a"));
	const TArray<TSharedPtr<FJsonValue>>& B = J->GetArrayField(TEXT("b"));
	const TArray<TSharedPtr<FJsonValue>>& T = J->GetArrayField(TEXT("t"));
	for (int32 K = 0; K < T.Num(); ++K)
	{
		Tally.Check(Grid.crossing(F3(A[K]), F3(B[K])), OptT(T[K]), FString::Printf(TEXT("ground ray %d"), K));
	}
	Tally.Report(*this, TEXT("ground crossings"));
	// The same through the cast, which adds nothing but the ground here.
	const std::optional<AcRays::FHit> H = AcRays::Cast(&Grid, {}, F3(A[0]), F3(B[0]));
	TestTrue(TEXT("Cast with no targets is the ground's crossing"), H.has_value() == OptT(T[0]).has_value() && (!H || !H->Id));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcRaysModelsTest, "Autocraft.Rays.Models", Flags)
bool FAcRaysModelsTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> G = Golden();
	if (!TestTrue(TEXT("golden rays.json loads"), G.IsValid())) return false;
	FTally Tally;
	int32 Models = 0;
	for (const TSharedPtr<FJsonValue>& CaseV : G->GetArrayField(TEXT("models")))
	{
		const TSharedPtr<FJsonObject> C = CaseV->AsObject();
		const FString Name = C->GetStringField(TEXT("model"));
		// The model as the catalog holds it.
		FAcModelInfo Model;
		Model.Name = FName(*Name);
		for (const TSharedPtr<FJsonValue>& PV : C->GetArrayField(TEXT("parts")))
		{
			const TSharedPtr<FJsonObject> PO = PV->AsObject();
			FAcModelPart& Part = Model.Parts.AddDefaulted_GetRef();
			Part.Name = FName(*PO->GetStringField(TEXT("name")));
			double Parent = -1;
			Part.Parent = PO->TryGetNumberField(TEXT("parent"), Parent) ? int32(Parent) : INDEX_NONE;
			Part.Rest = FAcModelCatalog::TransformFromSceneKitMatrix(Doubles(PO->GetArrayField(TEXT("localTransform"))));
			Part.bHidden = PO->GetBoolField(TEXT("hidden"));
			const TArray<TSharedPtr<FJsonValue>>* Rays = nullptr;
			if (PO->TryGetArrayField(TEXT("rays"), Rays)) Part.Rays = *Rays;
		}
		const FAcRayShape Shape = FAcRayShapes::Build(Model);
		if (!TestFalse(*FString::Printf(TEXT("%s has pieces"), *Name), Shape.Empty())) continue;
		Models++;
		// Posed as UAcWorldRenderer poses: local × parent world, the root under the placement.
		const FTransform Placement = FAcModelCatalog::TransformFromSceneKitMatrix(Doubles(C->GetArrayField(TEXT("placement"))));
		const int32 N = Model.Parts.Num();
		TArray<FTransform> World;
		World.SetNum(N);
		std::vector<FAcMat4> Mats(static_cast<size_t>(N));
		std::vector<uint8_t> Visible(static_cast<size_t>(N));
		for (int32 P = 0; P < N; ++P)
		{
			const FAcModelPart& Part = Model.Parts[P];
			World[P] = Part.Parent == INDEX_NONE ? Part.Rest * Placement : Part.Rest * World[Part.Parent];
			Mats[size_t(P)] = AcRaySpace::Matrix(World[P]);
			Visible[size_t(P)] = uint8_t(!Part.bHidden && (Part.Parent == INDEX_NONE || Visible[size_t(Part.Parent)]));
		}
		const TArray<TSharedPtr<FJsonValue>>& A = C->GetArrayField(TEXT("a"));
		const TArray<TSharedPtr<FJsonValue>>& B = C->GetArrayField(TEXT("b"));
		const TArray<TSharedPtr<FJsonValue>>& T = C->GetArrayField(TEXT("t"));
		for (int32 K = 0; K < T.Num(); ++K)
		{
			Tally.Check(Shape.Hit(Mats.data(), Visible.data(), F3(A[K]), F3(B[K]), 1.0f), OptT(T[K]), FString::Printf(TEXT("%s ray %d"), *Name, K));
		}
	}
	AddInfo(FString::Printf(TEXT("%d models"), Models));
	Tally.Report(*this, TEXT("models"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcRaysCastTest, "Autocraft.Rays.Cast", Flags)
bool FAcRaysCastTest::RunTest(const FString&)
{
	// Two unit cubes on the x axis at 2 and 5, ids 7 and 9; scenery at 8.
	FAcRayShape Cube;
	Cube.Parent = {-1};
	FAcRayShape::FPiece Piece;
	Piece.Box.Lo = {-0.5f, -0.5f, -0.5f};
	Piece.Box.Hi = {0.5f, 0.5f, 0.5f};
	Cube.Pieces.push_back(Piece);
	Cube.Measure({FAcMat4()});
	TestTrue(TEXT("reach: the corner ×1.25"), FMath::IsNearlyEqual(Cube.Reach, FMath::Sqrt(0.75f) * 1.25f, 1e-5f));
	const auto At = [](const float X) {
		FAcMat4 M;
		M(0, 3) = X;
		return M;
	};
	const FAcMat4 M7 = At(2), M9 = At(5), M8 = At(8);
	const uint8_t Shown = 1, Hidden = 0;
	std::vector<AcRays::FTarget> Targets = {{9, &Cube, &M9, &Shown}, {7, &Cube, &M7, &Shown}, {std::nullopt, &Cube, &M8, &Shown}};
	const FAcF3 A{0, 0, 0}, B{10, 0, 0};
	std::optional<AcRays::FHit> H = AcRays::Cast(nullptr, Targets, A, B);
	TestTrue(TEXT("the nearest is met first, whatever the order"), H && H->Id == 7 && FMath::IsNearlyEqual(H->T, 0.15f, 1e-6f));
	H = AcRays::Cast(nullptr, Targets, A, B, 7);
	TestTrue(TEXT("Skip lets one id through"), H && H->Id == 9 && FMath::IsNearlyEqual(H->T, 0.45f, 1e-6f));
	Targets[0].Visible = &Hidden;
	H = AcRays::Cast(nullptr, Targets, A, B, 7);
	TestTrue(TEXT("a hidden root is not met; the scenery is, with no id"), H && !H->Id && FMath::IsNearlyEqual(H->T, 0.75f, 1e-6f));
	H = AcRays::Cast(nullptr, Targets, {0, 2, 0}, {10, 2, 0});
	TestFalse(TEXT("a segment passing over misses"), H.has_value());
	H = AcRays::Cast(nullptr, Targets, {2, 0, 0}, {3, 0, 0});
	TestFalse(TEXT("a segment starting inside passes"), H.has_value());
	H = AcRays::Cast(nullptr, Targets, A, {1, 0, 0});
	TestFalse(TEXT("nothing past B"), H.has_value());

	// The SceneKit ↔ Unreal conversion: a part 1 cell up Unreal Z sits at SceneKit y 1.
	const FAcMat4 Up = AcRaySpace::Matrix(FTransform(FRotator(0, 90, 0), FVector(100, 200, 300), FVector(1, 2, 3)));
	const FAcF3 O = Up.Point({0, 0, 0});
	TestTrue(TEXT("translation: UE (1, 2, 3) m → SceneKit (1, 3, 2)"), FMath::IsNearlyEqual(O.x, 1.f) && FMath::IsNearlyEqual(O.y, 3.f) && FMath::IsNearlyEqual(O.z, 2.f));
	const FAcF3 Q = AcRaySpace::Point(FVector(100, 200, 300));
	const FVector Back = AcRaySpace::ToUnreal(Up.Point({0.01f, 0.02f, 0.03f}));
	// UE local (1, 3, 2) cm (= SceneKit (0.01, 0.02, 0.03)) through the transform.
	const FVector Expected = FTransform(FRotator(0, 90, 0), FVector(100, 200, 300), FVector(1, 2, 3)).TransformPosition(FVector(1, 3, 2));
	TestTrue(TEXT("Matrix(T) agrees with T.TransformPosition"), Back.Equals(Expected, 1e-3));
	TestTrue(TEXT("Point: UE cm → SceneKit cells"), FMath::IsNearlyEqual(Q.x, 1.f) && FMath::IsNearlyEqual(Q.y, 3.f) && FMath::IsNearlyEqual(Q.z, 2.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcRaysCatalogTest, "Autocraft.Rays.Catalog", Flags)
bool FAcRaysCatalogTest::RunTest(const FString&)
{
	const FAcModelCatalog& Catalog = FAcModelCatalog::Get();
	if (!TestTrue(TEXT("the model catalog loads"), Catalog.IsLoaded())) return false;
	const TCHAR* Needed[] = {TEXT("prospector_blue"), TEXT("ranger_blue"), TEXT("comet_blue"), TEXT("juggernaut_blue"), TEXT("firefly_blue"),
		TEXT("longbow_blue"), TEXT("dropship_blue"), TEXT("kestrel_blue"), TEXT("hailstorm_blue"), TEXT("citadel_blue"), TEXT("garrison_blue"),
		TEXT("habdome_blue"), TEXT("bastion_blue"), TEXT("derrick_blue"), TEXT("foundry_blue"), TEXT("spacedock_blue"), TEXT("lab_blue"),
		TEXT("sentinel_blue"), TEXT("ore_0"), TEXT("well_0"), TEXT("well_3"), TEXT("doodad_rock_0"), TEXT("doodad_tower")};
	for (const TCHAR* Name : Needed)
	{
		const FAcModelInfo* Model = Catalog.Find(FName(Name));
		const FAcRayShape* Shape = FAcRayShapes::Get().Find(Model);
		if (TestNotNull(*FString::Printf(TEXT("%s has a ray shape (catalog `rays`)"), Name), Shape))
		{
			TestTrue(*FString::Printf(TEXT("%s reaches out"), Name), Shape->Reach > 0.1f);
		}
	}
	// A Ranger at the origin facing +X: a shot across its chest hits it,
	// one across between its legs, low, misses (the rule the rays exist for).
	const FAcModelInfo* Ranger = Catalog.Find(TEXT("ranger_blue"));
	const FAcRayShape* Shape = FAcRayShapes::Get().Find(Ranger);
	if (!Ranger || !Shape) return false;
	const int32 N = Ranger->Parts.Num();
	TArray<FTransform> World;
	World.SetNum(N);
	std::vector<FAcMat4> Mats(static_cast<size_t>(N));
	std::vector<uint8_t> Visible(static_cast<size_t>(N));
	for (int32 P = 0; P < N; ++P)
	{
		const FAcModelPart& Part = Ranger->Parts[P];
		World[P] = Part.Parent == INDEX_NONE ? Part.Rest : Part.Rest * World[Part.Parent];
		Mats[size_t(P)] = AcRaySpace::Matrix(World[P]);
		Visible[size_t(P)] = uint8_t(!Part.bHidden && (Part.Parent == INDEX_NONE || Visible[size_t(Part.Parent)]));
	}
	// Side to side (SceneKit z) at chest height.
	const std::optional<float> Chest = Shape->Hit(Mats.data(), Visible.data(), {0.0f, 0.9f, -3.0f}, {0.0f, 0.9f, 3.0f}, 1.0f);
	TestTrue(TEXT("a shot across the chest hits"), Chest.has_value());
	// Front to back through the middle, low: between the legs it passes.
	int32 Gaps = 0;
	float Highest = 0;
	for (float Y = 0.05f; Y <= 0.8f; Y += 0.05f)
	{
		if (!Shape->Hit(Mats.data(), Visible.data(), {-3.0f, Y, 0.0f}, {3.0f, Y, 0.0f}, 1.0f))
		{
			Gaps++;
			Highest = Y;
		}
	}
	AddInfo(FString::Printf(TEXT("Ranger: %d of 16 low front-to-back shots pass between the legs (highest at y %.2f)"), Gaps, Highest));
	TestTrue(TEXT("a shot between a Ranger's legs misses"), Gaps > 0);
	return true;
}

#endif
