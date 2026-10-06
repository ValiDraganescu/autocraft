// See AcRaysWorld.h.
#include "AcRaysWorld.h"

#include "AcDoodads.h"
#include "AcLog.h"
#include "AcModelCatalog.h"
#include "AcPose.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"
#include "AcTerrain.h"
#include "AcWorldRenderer.h"

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// --- Space ---------------------------------------------------------------------

FAcF3 AcRaySpace::Point(const FVector& W)
{
	const FVector S = AcSpace::ToSceneKit(W);
	return {float(S.X), float(S.Y), float(S.Z)};
}

FVector AcRaySpace::ToUnreal(const FAcF3 S)
{
	return AcSpace::FromSceneKit(S.x, S.y, S.z);
}

FAcMat4 AcRaySpace::Matrix(const FTransform& T)
{
	// Unreal's matrix is row-vector (p' = p·M, row i the image of axis i);
	// as a column-major matrix A(r, c) = M[c][r]. SceneKit's = swap·A·swap
	// (Y↔Z on both sides; the cm cancel), translation swapped and in cells.
	const FMatrix M = T.ToMatrixWithScale();
	static constexpr int Swap[3] = {0, 2, 1};
	FAcMat4 Out;
	for (int C = 0; C < 3; C++)
	{
		for (int R = 0; R < 3; R++) Out(R, C) = float(M.M[Swap[C]][Swap[R]]);
		Out(3, C) = 0;
	}
	for (int R = 0; R < 3; R++) Out(R, 3) = float(M.M[3][Swap[R]] * AcSpace::CellsPerCm);
	Out(3, 3) = 1;
	return Out;
}

// --- Shapes --------------------------------------------------------------------

namespace
{
	bool Floats(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, float* Out, const int32 N)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!O->TryGetArrayField(Field, A) || A->Num() != N) return false;
		for (int32 K = 0; K < N; ++K) Out[K] = float((*A)[K]->AsNumber());
		return true;
	}
}

bool FAcRayShapes::ParsePiece(const FJsonValue& Value, const int32 Part, FAcRayShape::FPiece& Out)
{
	const TSharedPtr<FJsonObject>* O = nullptr;
	if (!Value.TryGetObject(O)) return false;
	float M[16], Lo[3], Hi[3];
	if (!Floats(*O, TEXT("transform"), M, 16) || !Floats(*O, TEXT("lo"), Lo, 3) || !Floats(*O, TEXT("hi"), Hi, 3)) return false;
	Out.Part = Part;
	Out.At = FAcMat4::FromColumns(M);
	Out.Box.Lo = {Lo[0], Lo[1], Lo[2]};
	Out.Box.Hi = {Hi[0], Hi[1], Hi[2]};
	FString Round;
	(*O)->TryGetStringField(TEXT("round"), Round);
	double Radius = 0, Half = 0;
	(*O)->TryGetNumberField(TEXT("radius"), Radius);
	(*O)->TryGetNumberField(TEXT("half"), Half);
	Out.Box.Radius = float(Radius);
	Out.Box.Half = float(Half);
	Out.Box.Round = Round == TEXT("sphere") ? FAcRayBox::ERound::Sphere
		: Round == TEXT("cylinder")         ? FAcRayBox::ERound::Cylinder
		: Round == TEXT("capsule")          ? FAcRayBox::ERound::Capsule
											: FAcRayBox::ERound::None;
	return true;
}

FAcRayShape FAcRayShapes::Build(const FAcModelInfo& Model)
{
	FAcRayShape Shape;
	const int32 N = Model.Parts.Num();
	Shape.Parent.resize(size_t(N));
	std::vector<FAcMat4> Rest(static_cast<size_t>(N));
	for (int32 P = 0; P < N; ++P)
	{
		const FAcModelPart& Part = Model.Parts[P];
		Shape.Parent[size_t(P)] = Part.Parent;
		if (Part.Parent == INDEX_NONE) Shape.Root = P;
		Rest[size_t(P)] = AcRaySpace::Matrix(Part.Rest);
		for (const TSharedPtr<FJsonValue>& V : Part.Rays)
		{
			FAcRayShape::FPiece Piece;
			if (V && ParsePiece(*V, P, Piece) && Piece.Box.Known()) Shape.Pieces.push_back(Piece);
		}
	}
	Shape.Measure(Rest);
	return Shape;
}

const FAcRayShapes& FAcRayShapes::Get()
{
	static const FAcRayShapes Instance = [] {
		FAcRayShapes S;
		int32 Pieces = 0;
		for (const FAcModelInfo& Model : FAcModelCatalog::Get().All())
		{
			FAcRayShape Shape = Build(Model);
			if (Shape.Empty()) continue;
			Pieces += int32(Shape.Pieces.size());
			S.Shapes.Add(&Model, MakeUnique<FAcRayShape>(MoveTemp(Shape)));
		}
		UE_LOG(LogAutocraft, Log, TEXT("rays: %d model shapes, %d pieces"), S.Shapes.Num(), Pieces);
		if (S.Shapes.Num() == 0)
		{
			UE_LOG(LogAutocraft, Warning, TEXT("rays: the catalog has no `rays` (rerun export-models and import_models.py): only the ground is met"));
		}
		return S;
	}();
	return Instance;
}

const FAcRayShape* FAcRayShapes::Find(const FAcModelInfo* Model) const
{
	const TUniquePtr<FAcRayShape>* S = Model ? Shapes.Find(Model) : nullptr;
	return S ? S->Get() : nullptr;
}

// --- The subsystem -------------------------------------------------------------

UAcRaysSubsystem* UAcRaysSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UAcRaysSubsystem>() : nullptr;
}

bool UAcRaysSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = ::Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UAcRaysSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	double Seconds = 0;
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(&World);
	if (!Sim || !FParse::Value(FCommandLine::Get(), TEXT("AcRayProbe="), Seconds)) return;
	ProbeAt = Seconds;
	ProbeHandle = Sim->AddFrameListener(EAcFrameStage::Other, FAcFrameEvent::FDelegate::CreateWeakLambda(this, [this](const FAcFrame&) {
		UAcSimSubsystem* S = UAcSimSubsystem::Get(this);
		if (!S || ProbeAt < 0 || S->GameTime() < ProbeAt) return;
		ProbeAt = -1;
		GEngine->Exec(GetWorld(), TEXT("ac.RayProbe 6"));
	}));
}

void UAcRaysSubsystem::Deinitialize()
{
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this); Sim && ProbeHandle.IsValid()) Sim->RemoveFrameListener(EAcFrameStage::Other, ProbeHandle);
	ProbeHandle.Reset();
	Scenery.Reset();
	SceneryMap = nullptr;
	Super::Deinitialize();
}

void UAcRaysSubsystem::AddScenery(const FAcModelInfo* Model, const FTransform& Placement, const TOptional<int64> Patch)
{
	const FAcRayShape* Shape = FAcRayShapes::Get().Find(Model);
	if (!Shape) return;
	FScenery& S = Scenery.AddDefaulted_GetRef();
	S.Shape = Shape;
	S.Model = Model;
	S.Patch = Patch;
	const int32 N = Model->Parts.Num();
	// Composed as the renderer composes a pose: part local × parent world.
	TArray<FTransform> World;
	World.SetNum(N);
	S.World.resize(size_t(N));
	S.Visible.assign(size_t(N), 1);
	for (int32 P = 0; P < N; ++P)
	{
		const FAcModelPart& Part = Model->Parts[P];
		World[P] = Part.Parent == INDEX_NONE ? Part.Rest * Placement : Part.Rest * World[Part.Parent];
		S.World[size_t(P)] = AcRaySpace::Matrix(World[P]);
		S.Visible[size_t(P)] = !Part.bHidden && (Part.Parent == INDEX_NONE || S.Visible[size_t(Part.Parent)]);
		const FString Name = Part.Name.ToString();
		if (Patch && Name.StartsWith(TEXT("stage")))
		{
			const int32 K = FCString::Atoi(*Name + 5);
			if (K >= 1 && K <= 3) S.StagePart[K - 1] = P;
		}
	}
}

void UAcRaysSubsystem::SyncScenery(UAcSimSubsystem& Sim)
{
	const ac::GameState& State = Sim.Shown();
	const ac::MapDefinition& Map = Sim.Map();
	const size_t Wells = State.wells ? State.wells->size() : 0;
	const size_t Doodads = State.doodads ? State.doodads->size() : 0;
	uint32 Hash = 0;
	if (State.doodads)
	{
		for (const ac::Doodad& D : *State.doodads) Hash = HashCombine(Hash, HashCombine(GetTypeHash(D.position.x), GetTypeHash(D.position.y)));
	}
	if (SceneryMap == &Map && SceneryPatches == State.patches.size() && SceneryWells == Wells && SceneryDoodads == Doodads
		&& SceneryDoodadHash == Hash)
	{
		return;
	}
	SceneryMap = &Map;
	SceneryPatches = State.patches.size();
	SceneryWells = Wells;
	SceneryDoodads = Doodads;
	SceneryDoodadHash = Hash;
	Scenery.Reset();

	const AAcTerrain* Terrain = AAcTerrain::Find(GetWorld());
	const auto Ground = [Terrain](const ac::Vec2 P) { return Terrain ? Terrain->FieldHeight(P) : 0.0; };
	const FAcModelCatalog& Catalog = FAcModelCatalog::Get();
	// GameScene.addPatch: `ore_<id % 4>` at the ground, turned by `angle`
	// (as AAcResources draws it).
	for (const ac::OreDeposit& P : State.patches)
	{
		const FAcModelInfo* Model = Catalog.Find(FName(*FString::Printf(TEXT("ore_%d"), int32(((P.id % 4) + 4) % 4))));
		AddScenery(Model, FTransform(FRotator(0.0, -FMath::RadiansToDegrees(P.angle), 0.0), AcSpace::ToWorld(P.position, Ground(P.position))),
			int64(P.id));
	}
	// GameScene.addWell: the map's (seed i·10 + j), then the playground's
	// (seed 1000 + k), sunk 0.05; the export's well_0/well_3 turned to the seed.
	const auto AddWell = [&](const ac::Vec2 At, const int32 Seed) {
		const int32 Variant = (Seed % 2 == 0) ? 0 : 3;
		AddScenery(Catalog.Find(FName(*FString::Printf(TEXT("well_%d"), Variant))),
			FTransform(FRotator(0.0, FMath::RadiansToDegrees(double(Seed - Variant)), 0.0), AcSpace::ToWorld(At, Ground(At) - 0.05)));
	};
	int32 MapWells = 0;
	for (int32 I = 0; I < int32(Map.bases.size()); ++I)
	{
		for (int32 J = 0; J < int32(Map.bases[size_t(I)].wells.size()); ++J)
		{
			AddWell(Map.bases[size_t(I)].wells[size_t(J)], I * 10 + J);
			MapWells++;
		}
	}
	for (int32 K = MapWells; K < int32(Wells); ++K) AddWell((*State.wells)[size_t(K)].position, 1000 + (K - MapWells));
	// GameScene.addDoodad: the map's and the playground's (border rocks are
	// not met, as in Swift).
	const auto AddDoodad = [&](const ac::Doodad& D) { AddScenery(AAcDoodads::ModelFor(D.kind, D.variant), AAcDoodads::Placement(D, Ground(D.position))); };
	for (const ac::Doodad& D : Map.doodads) AddDoodad(D);
	if (State.doodads)
	{
		for (const ac::Doodad& D : *State.doodads) AddDoodad(D);
	}
}

TOptional<FAcRayHit> UAcRaysSubsystem::SightHit(const FVector& A, const FVector& B, const TOptional<int64> Skip)
{
	return Cast(A, B, true, Skip);
}

TOptional<float> UAcRaysSubsystem::Blocked(const FVector& A, const FVector& B)
{
	const TOptional<FAcRayHit> H = Cast(A, B, false);
	return H ? TOptional<float>(H->T) : TOptional<float>();
}

TOptional<FAcRayHit> UAcRaysSubsystem::Cast(const FVector& A, const FVector& B, const bool bUnits, const TOptional<int64> Skip)
{
	const FAcF3 SA = AcRaySpace::Point(A), SB = AcRaySpace::Point(B);
	const AAcTerrain* Terrain = AAcTerrain::Find(GetWorld());
	const FAcHeightGrid* Ground = Terrain && !Terrain->Heights().empty() ? &Terrain->Heights() : nullptr;

	Mats.clear();
	Shown.clear();
	Targets.clear();
	Ranges.clear();
	std::vector<std::optional<int64_t>> Ids;
	std::vector<const FAcRayShape*> Shapes;

	// Units and buildings where the renderer posed them; only those the
	// segment passes near get their parts converted.
	if (const UAcWorldRenderer* Renderer = UAcWorldRenderer::Get(this))
	{
		const FAcRayShapes& All = FAcRayShapes::Get();
		Renderer->ForEachObject([&](const int64 Id, const bool bUnit, const FAcModelInfo& Model, const FAcPose& Pose,
									TConstArrayView<FTransform> World) {
			if ((bUnit && !bUnits) || Pose.bHidden || (Skip && *Skip == Id)) return;
			const FAcRayShape* Shape = All.Find(&Model);
			if (!Shape || !Shape->Near(AcRaySpace::Matrix(World[Shape->Root]), SA, SB)) return;
			const size_t First = Mats.size();
			const int32 N = World.Num();
			for (int32 P = 0; P < N; ++P)
			{
				Mats.push_back(AcRaySpace::Matrix(World[P]));
				const int32 Up = Model.Parts[P].Parent;
				const bool bVisible = Pose.Visible.IsValidIndex(P) ? Pose.Visible[P] : true;
				Shown.push_back(uint8_t(bVisible && (Up == INDEX_NONE || Shown[First + size_t(Up)])));
			}
			Ranges.emplace_back(First, size_t(N));
			Ids.emplace_back(Id);
			Shapes.push_back(Shape);
		});
	}

	// Ore, wells and doodads.
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this); Sim && Sim->IsRunning())
	{
		SyncScenery(*Sim);
		TMap<int64, int64> Stages;
		for (const ac::OreDeposit& P : Sim->Shown().patches) Stages.Add(int64(P.id), int64(P.stage()));
		for (FScenery& S : Scenery)
		{
			if (!S.Shape->Near(S.World[size_t(S.Shape->Root)], SA, SB)) continue;
			if (S.Patch)
			{
				// GameScene.applyStage: nodule K shows while stage ≥ K; stage 0 hides the deposit.
				const int64 Stage = Stages.FindRef(*S.Patch);
				const int32 N = S.Model->Parts.Num();
				for (int32 P = 0; P < N; ++P)
				{
					const FAcModelPart& Part = S.Model->Parts[P];
					bool bVisible = !Part.bHidden;
					for (int32 K = 0; K < 3; ++K)
					{
						if (S.StagePart[K] == P) bVisible = bVisible && Stage >= K + 1;
					}
					if (Part.Parent == INDEX_NONE) bVisible = bVisible && Stage > 0;
					else bVisible = bVisible && S.Visible[size_t(Part.Parent)];
					S.Visible[size_t(P)] = uint8_t(bVisible);
				}
			}
			Ranges.emplace_back(SIZE_MAX, size_t(&S - Scenery.GetData()));
			Ids.emplace_back(std::nullopt);
			Shapes.push_back(S.Shape);
		}
	}

	// Pointers into the scratch only once it stops growing.
	for (size_t K = 0; K < Ranges.size(); ++K)
	{
		AcRays::FTarget T;
		T.Id = Ids[K];
		T.Shape = Shapes[K];
		if (Ranges[K].first == SIZE_MAX)
		{
			const FScenery& S = Scenery[int32(Ranges[K].second)];
			T.PartWorld = S.World.data();
			T.Visible = S.Visible.data();
		}
		else
		{
			T.PartWorld = Mats.data() + Ranges[K].first;
			T.Visible = Shown.data() + Ranges[K].first;
		}
		Targets.push_back(T);
	}

	const std::optional<AcRays::FHit> Hit = AcRays::Cast(Ground, Targets, SA, SB, std::nullopt);
	if (!Hit) return {};
	FAcRayHit Out;
	Out.T = Hit->T;
	Out.Point = A + (B - A) * double(Hit->T);
	if (Hit->Id) Out.Id = *Hit->Id;
	return Out;
}

// --- Console -------------------------------------------------------------------

static FAutoConsoleCommandWithWorldAndArgs GAcRayCommand(TEXT("ac.Ray"),
	TEXT("ac.Ray AX AY AZ BX BY BZ [units 0|1]: what the segment A→B (Unreal cm) meets first (AcRaysWorld.h)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) {
		UAcRaysSubsystem* Rays = World ? World->GetSubsystem<UAcRaysSubsystem>() : nullptr;
		if (!Rays || Args.Num() < 6)
		{
			UE_LOG(LogAutocraft, Display, TEXT("ac.Ray AX AY AZ BX BY BZ [units 0|1]"));
			return;
		}
		const FVector A(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]), FCString::Atod(*Args[2]));
		const FVector B(FCString::Atod(*Args[3]), FCString::Atod(*Args[4]), FCString::Atod(*Args[5]));
		const bool bUnits = Args.Num() < 7 || FCString::Atoi(*Args[6]) != 0;
		const double Start = FPlatformTime::Seconds();
		const TOptional<FAcRayHit> H = Rays->Cast(A, B, bUnits);
		const double Ms = (FPlatformTime::Seconds() - Start) * 1000.0;
		if (!H)
		{
			UE_LOG(LogAutocraft, Display, TEXT("ray: nothing (%.3f ms, %d scenery)"), Ms, Rays->NumScenery());
		}
		else
		{
			UE_LOG(LogAutocraft, Display, TEXT("ray: t %.5f at %s, %s (%.3f ms)"), H->T, *H->Point.ToString(),
				H->Id ? *FString::Printf(TEXT("id %lld"), *H->Id) : TEXT("ground/scenery"), Ms);
		}
	}));

// `ac.RayProbe [N]`: for N units and N buildings the local player sees, a
// ray straight down from 30 cells over each onto it, and one 60-cell
// crosshair ray across the map from each, logged with what they met and the
// time taken: a live check that the rays see what the renderer draws.
static FAutoConsoleCommandWithWorldAndArgs GAcRayProbeCommand(TEXT("ac.RayProbe"),
	TEXT("ac.RayProbe [N]: rays down onto N units and N buildings, and 60-cell rays across, logged (AcRaysWorld.h)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) {
		UAcRaysSubsystem* Rays = World ? World->GetSubsystem<UAcRaysSubsystem>() : nullptr;
		UAcSimSubsystem* Sim = World ? World->GetSubsystem<UAcSimSubsystem>() : nullptr;
		if (!Rays || !Sim || !Sim->IsRunning()) return;
		const int32 N = Args.Num() > 0 ? FMath::Max(1, FCString::Atoi(*Args[0])) : 5;
		const ac::GameState& State = Sim->Shown();
		const AAcTerrain* Terrain = AAcTerrain::Find(World);
		int32 Self = 0, Tried = 0;
		double Worst = 0, Total = 0;
		const auto Probe = [&](const int64 Id, const ac::Vec2 P, const TCHAR* What) {
			const double G = Terrain ? Terrain->HeightAt(P) : 0.0;
			const FVector A = AcSpace::ToWorld(P, G + 30.0), B = AcSpace::ToWorld(P, G - 1.0);
			double Start = FPlatformTime::Seconds();
			const TOptional<FAcRayHit> Down = Rays->SightHit(A, B);
			const double MsDown = (FPlatformTime::Seconds() - Start) * 1000.0;
			// Across: from 1.2 cells over it, 60 cells toward the map's middle.
			const ac::GroundRect R = Sim->Map().bounds;
			const ac::Vec2 Mid(R.minX + R.width() / 2, R.minZ + R.depth() / 2);
			const double L = ac::length(Mid - P);
			const ac::Vec2 Dir = L > 1e-6 ? (Mid - P) * (1.0 / L) : ac::Vec2(1, 0);
			const FVector C = AcSpace::ToWorld(P, G + 1.2), D = AcSpace::ToWorld(P + Dir * 60.0, G + 1.2);
			Start = FPlatformTime::Seconds();
			const TOptional<FAcRayHit> Across = Rays->SightHit(C, D, Id);
			const double MsAcross = (FPlatformTime::Seconds() - Start) * 1000.0;
			Tried++;
			if (Down && Down->Id == Id) Self++;
			Worst = FMath::Max(Worst, FMath::Max(MsDown, MsAcross));
			Total += MsDown + MsAcross;
			UE_LOG(LogAutocraft, Display, TEXT("ray probe %s %lld at (%.2f, %.2f): down meets %s at height %.2f (%.3f ms); across meets %s at t %.3f (%.3f ms)"),
				What, Id, P.x, P.y, Down ? (Down->Id ? *FString::Printf(TEXT("id %lld"), *Down->Id) : TEXT("ground/scenery")) : TEXT("nothing"),
				Down ? AcSpace::HeightToSim(Down->Point) : 0.0, MsDown,
				Across ? (Across->Id ? *FString::Printf(TEXT("id %lld"), *Across->Id) : TEXT("ground/scenery")) : TEXT("nothing"),
				Across ? Across->T : 1.f, MsAcross);
		};
		int32 Units = 0, Buildings = 0;
		for (const ac::Unit& U : State.units)
		{
			if (Units++ >= N) break;
			Probe(U.id, U.position, TEXT("unit"));
		}
		for (const ac::Structure& S : State.structures)
		{
			if (Buildings++ >= N) break;
			Probe(S.id, S.position, TEXT("building"));
		}
		UE_LOG(LogAutocraft, Display, TEXT("ray probe: %d of %d rays down met their own unit or building; %.3f ms a ray on average, worst %.3f ms; %d scenery"),
			Self, Tried, Tried ? Total / (2 * Tried) : 0.0, Worst, Rays->NumScenery());
	}));
