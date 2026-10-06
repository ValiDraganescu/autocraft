#include "AcBuildingFx.h"
#include "AcInstancedMesh.h"

#include "AcLog.h"
#include "AcModelCatalog.h"
#include "AcPoseBuildings.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"
#include "AcWorldRenderer.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include "Rules.h"
#include "Simulation.h"
#include "Types.h"

#include <cmath>

namespace
{
	TAutoConsoleVariable<float> CVarLightGain(TEXT("ac.BuildingLightGain"), 500.f,
		TEXT("Scale on the buildings' weld and door lights (unitless = SceneKit intensity · 2.9/1000 · this; 100 = the lamps' ac.LampGain)."));
	TAutoConsoleVariable<int32> CVarMaxLights(TEXT("ac.BuildingLights"), 24,
		TEXT("Most building lights (welds, door and bay lights) at once; the brightest win."));

	const TCHAR* const SpriteMeshPath = TEXT("/Engine/BasicShapes/Plane.Plane");
	const TCHAR* const SpriteMaterialPath = TEXT("/Game/Materials/MI_AcVapour.MI_AcVapour");
	constexpr int32 MaxParticles = 6000;
	/// SceneKit light intensity → Unreal unitless, as UAcLampPool (AcLamps.cpp):
	/// no inverse square, SceneKit-like falloff over the reach (`ac.LampFalloff`).
	constexpr double CandelaPerSceneKit = 2.9 / 1000.0;

	const FName WeldCue(TEXT("AcWeld")), SparksCue(TEXT("AcSparks")), SteamCue(TEXT("AcSteam")), VapourCue(TEXT("AcVapour")),
		PuffsCue(TEXT("AcPuffs"));

	/// `SIMD4<Float>(1.0, 0.66, 0.19, 1.0)` (the manifest's light colour).
	FLinearColor ParseColour(const FString& S, const FLinearColor Fallback)
	{
		int32 Open = INDEX_NONE, Close = INDEX_NONE;
		if (!S.FindChar(TEXT('('), Open) || !S.FindLastChar(TEXT(')'), Close) || Close <= Open) return Fallback;
		TArray<FString> Parts;
		S.Mid(Open + 1, Close - Open - 1).ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() < 3) return Fallback;
		return FLinearColor(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]));
	}

	/// A random direction within `HalfAngle` (degrees) of `Axis`.
	FVector Cone(FRandomStream& R, const FVector& Axis, const double HalfAngle)
	{
		const double CosMax = std::cos(FMath::DegreesToRadians(HalfAngle));
		const double Z = R.FRandRange(CosMax, 1.0);
		const double Phi = R.FRandRange(0.0, UE_DOUBLE_TWO_PI);
		const double S = std::sqrt(FMath::Max(0.0, 1.0 - Z * Z));
		const FVector Local(S * std::cos(Phi), S * std::sin(Phi), Z);
		return FQuat::FindBetweenNormals(FVector::ZAxisVector, Axis.GetSafeNormal()).RotateVector(Local);
	}

	/// A three-key curve (`CAKeyframeAnimation` values at key times 0, Mid, 1).
	double Keys(const double T, const double A, const double B, const double C, const double Mid)
	{
		return T < Mid ? FMath::Lerp(A, B, T / Mid) : FMath::Lerp(B, C, (T - Mid) / (1.0 - Mid));
	}
}

bool UAcBuildingFx::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UAcBuildingFx::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UAcSimSubsystem>();
}

void UAcBuildingFx::Deinitialize()
{
	if (UAcSimSubsystem* Sim = GetWorld() ? GetWorld()->GetSubsystem<UAcSimSubsystem>() : nullptr)
	{
		Sim->RemoveFrameListener(EAcFrameStage::Effects, FrameHandle);
		Sim->OnGameStarted.Remove(StartedHandle);
	}
	if (UAcWorldRenderer* R = Renderer.Get()) R->OnCue.Remove(CueHandle);
	Super::Deinitialize();
}

void UAcBuildingFx::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	UAcSimSubsystem* Sim = InWorld.GetSubsystem<UAcSimSubsystem>();
	if (!Sim) return;
	FrameHandle = Sim->AddFrameListener(EAcFrameStage::Effects, FAcFrameEvent::FDelegate::CreateUObject(this, &UAcBuildingFx::OnFrame));
	StartedHandle = Sim->OnGameStarted.AddUObject(this, &UAcBuildingFx::OnGameStarted);
	if (Sim->IsRunning()) OnGameStarted(*Sim);
}

void UAcBuildingFx::OnGameStarted(UAcSimSubsystem& Sim)
{
	Particles.Reset();
	Carry.Reset();
	Cues.Reset();
	LastTime.Reset();
	if (!bStaged)
	{
		bStaged = true;
		Stage(Sim);
	}
}

void UAcBuildingFx::Bind()
{
	if (Renderer.IsValid()) return;
	UAcWorldRenderer* R = UAcWorldRenderer::Get(this);
	if (!R) return;
	Renderer = R;
	CueHandle = R->OnCue.AddUObject(this, &UAcBuildingFx::OnCue);
}

void UAcBuildingFx::OnCue(const int64 Id, const FAcPoseCue& Cue)
{
	const UAcWorldRenderer* R = Renderer.Get();
	const FAcModelInfo* M = R ? R->ModelOf(Id) : nullptr;
	if (!M || M->Category != TEXT("building")) return;
	Cues.Add({Id, Cue});
}

void UAcBuildingFx::OnFrame(const FAcFrame& Frame)
{
	Bind();
	// The render clock, as the poses (B1): sparks and steam run on while paused.
	const double Dt = FMath::Clamp(Frame.Clock - LastTime.Get(Frame.Clock), 0.0, 0.1);
	LastTime = Frame.Clock;
	Bastions(Frame);
	// Particles from this frame's cues (one key per building, cue kind and order).
	TMap<int64, int32> Seen;
	for (const FCue& C : Cues)
	{
		int32& N = Seen.FindOrAdd(C.Id);
		Emit(C, N++, Dt);
	}
	Lights();
	Draw(Dt);
	Cues.Reset();
}

// MARK: - Bastion slits

void UAcBuildingFx::Bastions(const FAcFrame& Frame)
{
	UAcWorldRenderer* R = Renderer.Get();
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!R || !Sim || !Sim->IsRunning()) return;
	const ac::GameState& Shown = Sim->Shown();
	TMap<int64, const ac::Structure*> CrewOf;
	for (const ac::Structure& S : Shown.structures)
	{
		if (S.kind != ac::StructureKind::bastion) continue;
		if (FAcUnitMemory* M = R->Memory(S.id))
		{
			M->State<AcBuildingPose::FBastionState>().Room = int32(Sim->Simulation().bastionCapacity(S.owner));
		}
		if (S.crew)
		{
			for (const int64_t Id : *S.crew) CrewOf.Add(Id, &S);
		}
	}
	if (CrewOf.IsEmpty()) return;
	for (const ac::GameEvent& E : Frame.Events)
	{
		const auto* Shot = E.as<ac::GameEvent::Shot>();
		if (!Shot) continue;
		const ac::Structure* const* B = CrewOf.Find(Shot->unit);
		if (!B) continue;
		FAcUnitMemory* M = R->Memory((*B)->id);
		if (!M) continue;
		// From the slit facing the target.
		const ac::Vec2 C = (*B)->position;
		const double Want = std::atan2(Shot->at.x - C.x, Shot->at.y - C.y);
		int32 Best = 0;
		for (int32 K = 1; K < 5; ++K)
		{
			if (FMath::Abs(std::remainder(AcBuildingPose::BastionSlitAngle(K) - Want, UE_DOUBLE_TWO_PI))
				< FMath::Abs(std::remainder(AcBuildingPose::BastionSlitAngle(Best) - Want, UE_DOUBLE_TWO_PI)))
			{
				Best = K;
			}
		}
		M->State<AcBuildingPose::FBastionState>().Shots[Best] = Frame.Clock;
	}
}

// MARK: - Particles

void UAcBuildingFx::Emit(const FCue& C, const int32 Index, const double Dt)
{
	const FName What = C.Cue.What;
	EKind Kind;
	double Rate = C.Cue.Value;
	if (What == WeldCue)
	{
		Kind = EKind::Spark;
		Rate = 110.0;
	}
	else if (What == SparksCue) Kind = EKind::Spark;
	else if (What == SteamCue) Kind = EKind::Steam;
	else if (What == VapourCue) Kind = EKind::Vapour;
	else if (What == PuffsCue) Kind = EKind::Puff;
	else return;
	const uint64 Key = (uint64(C.Id) << 8) ^ uint64(Index);
	double& N = Carry.FindOrAdd(Key);
	N += Rate * Dt;
	// No pile-up after a stall: at most a tenth of a second's worth.
	N = FMath::Min(N, Rate * 0.1 + 1.0);
	while (N >= 1.0 && Particles.Num() < MaxParticles)
	{
		N -= 1.0;
		Spawn(Kind, C.Cue.At);
	}
}

void UAcBuildingFx::Spawn(const EKind Kind, const FVector& At)
{
	FParticle& P = Particles.AddDefaulted_GetRef();
	P.Kind = Kind;
	auto Var = [&](const double V, const double Range) { return V + Random.FRandRange(-0.5, 0.5) * Range; };
	switch (Kind)
	{
	case EKind::Spark:
		// Models.weldSparks: 0.35 s ±0.1, 2.4 ±0.6 cells/s in a 70° cone, gravity plus -6.
		P.At = At;
		P.Velocity = Cone(Random, FVector::ZAxisVector, 70.0) * Var(2.4, 1.2) * 100.0;
		P.Accel = FVector(0, 0, -(9.8 + 6.0) * 100.0);
		P.Life = float(Var(0.35, 0.2));
		P.Size = float(Var(0.035, 0.02));
		P.Colour = FLinearColor(1.f, 0.75f, 0.35f, 1.f) * 3.f;
		break;
	case EKind::Steam:
		// Models.steamVents: 1.8 s, 0.7 ±0.15 cells/s, 14° round (0.15, 1, 0).
		P.At = At + FVector(Random.FRandRange(-20.0, 20.0), Random.FRandRange(-12.0, 12.0), 0);
		P.Velocity = Cone(Random, AcSpace::AxesFromSceneKit(0.15, 1, 0), 14.0) * Var(0.7, 0.3) * 100.0;
		P.Life = 1.8f;
		P.Size = 0.22f;
		P.Colour = FLinearColor(0.85f, 0.85f, 0.85f, 0.22f);
		break;
	case EKind::Vapour:
	case EKind::Puff:
	{
		// Models.hydrogenVapour: radius 0.3 size 0.3 (steam), radius 0.05 size 0.14 (puffs).
		const bool bPuff = Kind == EKind::Puff;
		const double Radius = bPuff ? 5.0 : 30.0;
		P.At = At + Cone(Random, FVector::ZAxisVector, 180.0) * Random.FRandRange(0.0, Radius);
		P.Velocity = Cone(Random, AcSpace::AxesFromSceneKit(0.1, 1, 0.05), 16.0) * Var(bPuff ? 0.9 : 0.75, 0.3) * 100.0;
		P.Accel = AcSpace::AxesFromSceneKit(0.12, 0.1, 0) * 100.0;
		P.Life = float(Var(bPuff ? 1.5 : 2.2, 0.6));
		P.Size = float(Var(bPuff ? 0.14 : 0.3, (bPuff ? 0.14 : 0.3) * 0.3));
		P.Colour = FLinearColor(0.84f, 0.92f, 1.f, 0.2f);
		break;
	}
	}
	P.Life = FMath::Max(P.Life, 0.05f);
}

void UAcBuildingFx::Draw(const double Dt)
{
	UWorld* World = GetWorld();
	if (!World) return;
	if (!Sprites)
	{
		if (Particles.IsEmpty()) return;
		if (!Holder)
		{
			FActorSpawnParameters Params;
			Params.Name = TEXT("AcBuildingFx");
			Params.ObjectFlags |= RF_Transient;
			Holder = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
			USceneComponent* Root = NewObject<USceneComponent>(Holder, TEXT("Root"));
			Holder->SetRootComponent(Root);
			Root->RegisterComponent();
		}
		Sprites = NewObject<UAcInstancedMesh>(Holder, TEXT("Sprites"));  // cheap bounds (P2)
		Sprites->SetMobility(EComponentMobility::Movable);
		Sprites->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, SpriteMeshPath));
		if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, SpriteMaterialPath)) Sprites->SetMaterial(0, M);
		else UE_LOG(LogAutocraft, Warning, TEXT("buildfx: no %s (run Tools/Editor/make_resource_materials.py)"), SpriteMaterialPath);
		Sprites->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Sprites->SetCanEverAffectNavigation(false);
		Sprites->SetCastShadow(false);
		Sprites->bAffectDistanceFieldLighting = false;
		Sprites->SetNumCustomDataFloats(4);
		Sprites->SetupAttachment(Holder->GetRootComponent());
		Sprites->RegisterComponent();
		Holder->AddInstanceComponent(Sprites);
	}

	FVector Forward = FVector::ForwardVector, Right = FVector::RightVector;
	if (const APlayerController* PC = World->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
	{
		const FRotator R = PC->PlayerCameraManager->GetCameraRotation();
		Forward = R.Vector();
		Right = FRotationMatrix(R).GetUnitAxis(EAxis::Y);
	}
	const FQuat Facing = FRotationMatrix::MakeFromZX(-Forward, Right).ToQuat();

	Xf.Reset();
	Data.Reset();
	for (int32 I = Particles.Num() - 1; I >= 0; --I)
	{
		FParticle& P = Particles[I];
		P.Age += float(Dt);
		if (P.Age >= P.Life)
		{
			Particles.RemoveAtSwap(I, EAllowShrinking::No);
			continue;
		}
		P.Velocity += P.Accel * Dt;
		P.At += P.Velocity * Dt;
		const double T = P.Age / P.Life;
		if (P.Kind == EKind::Spark)
		{
			// A streak along the motion as the eye sees it (stretch 0.05 per cell/s).
			const FVector Along = (P.Velocity - Forward * FVector::DotProduct(P.Velocity, Forward));
			const double Speed = P.Velocity.Size() / 100.0;
			const FQuat Q = Along.SizeSquared() > 1.0 ? FRotationMatrix::MakeFromZX(-Forward, Along).ToQuat() : Facing;
			Xf.Add(FTransform(Q, P.At, FVector(P.Size + 0.05 * Speed, P.Size, 1)));
			const float Fade = float(1.0 - T);
			Data.Append({P.Colour.R, P.Colour.G, P.Colour.B, Fade});
		}
		else
		{
			const bool bSteam = P.Kind == EKind::Steam;
			const double Grow = bSteam ? Keys(T, 0.3, 1.2, 2.2, 0.5) : Keys(T, 0.35, 1.3, 2.6, 0.45);
			const double Alpha = bSteam ? Keys(T, 0.0, 0.5, 0.0, 0.25) : Keys(T, 0.0, 0.45, 0.0, 0.2);
			Xf.Add(FTransform(Facing, P.At, FVector(P.Size * Grow)));
			Data.Append({P.Colour.R, P.Colour.G, P.Colour.B, float(P.Colour.A * Alpha)});
		}
	}
	const int32 Count = Xf.Num();
	const int32 Have = Sprites->GetInstanceCount();
	if (Have < Count)
	{
		TArray<FTransform> More;
		More.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), Count - Have);
		Sprites->AddInstances(More, false, true);
	}
	const int32 Total = Sprites->GetInstanceCount();
	if (Total == 0) return;
	for (int32 I = Count; I < Total; ++I) Xf.Add(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector));
	Sprites->BatchUpdateInstancesTransforms(0, Xf, true, false, true);
	for (int32 I = 0; I < Count; ++I) Sprites->SetCustomData(I, TArrayView<const float>(&Data[I * 4], 4), false);
	Sprites->MarkRenderStateDirty();
}

// MARK: - Lights

void UAcBuildingFx::Lights()
{
	struct FWant
	{
		FVector At;
		FLinearColor Colour;
		double Intensity;
		double Reach;  // cells
	};
	TArray<FWant, TInlineAllocator<32>> Want;
	const UAcWorldRenderer* R = Renderer.Get();
	for (const FCue& C : Cues)
	{
		if (C.Cue.What == WeldCue)
		{
			Want.Add({C.Cue.At, FLinearColor(0.75f, 0.85f, 1.f), C.Cue.Value, 2.5});
			continue;
		}
		const FAcModelInfo* M = R ? R->ModelOf(C.Id) : nullptr;
		const FAcModelPart* Part = M ? M->FindPart(C.Cue.What) : nullptr;
		if (!Part || !Part->Light.IsValid()) continue;
		FString Colour, End;
		Part->Light->TryGetStringField(TEXT("color"), Colour);
		Part->Light->TryGetStringField(TEXT("attenuationEnd"), End);
		const double Reach = End.IsEmpty() ? 2.5 : FCString::Atod(*End);
		Want.Add({C.Cue.At, ParseColour(Colour, FLinearColor(1.f, 0.66f, 0.19f)), C.Cue.Value, Reach});
	}
	Want.Sort([](const FWant& A, const FWant& B) { return A.Intensity > B.Intensity; });
	const int32 N = FMath::Min(Want.Num(), FMath::Max(0, CVarMaxLights.GetValueOnGameThread()));
	if (N > Pool.Num() && !Holder && GetWorld())
	{
		FActorSpawnParameters Params;
		Params.Name = TEXT("AcBuildingFx");
		Params.ObjectFlags |= RF_Transient;
		Holder = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		USceneComponent* Root = NewObject<USceneComponent>(Holder, TEXT("Root"));
		Holder->SetRootComponent(Root);
		Root->RegisterComponent();
	}
	const IConsoleVariable* FalloffVar = IConsoleManager::Get().FindConsoleVariable(TEXT("ac.LampFalloff"));
	const float Falloff = FMath::Max(0.5f, FalloffVar ? FalloffVar->GetFloat() : 3.f);
	while (Pool.Num() < N && Holder)
	{
		UPointLightComponent* L = NewObject<UPointLightComponent>(Holder);
		L->SetMobility(EComponentMobility::Movable);
		L->SetCastShadows(false);
		L->SetIntensityUnits(ELightUnits::Unitless);
		L->bUseInverseSquaredFalloff = false;
		L->LightFalloffExponent = Falloff;
		L->SetSourceRadius(2.f);
		L->SetupAttachment(Holder->GetRootComponent());
		L->RegisterComponent();
		Holder->AddInstanceComponent(L);
		Pool.Add(L);
	}
	const double Gain = CandelaPerSceneKit * CVarLightGain.GetValueOnGameThread();
	for (int32 I = 0; I < Pool.Num(); ++I)
	{
		UPointLightComponent* L = Pool[I];
		if (I >= N)
		{
			if (L->IsVisible()) L->SetVisibility(false);
			continue;
		}
		const FWant& W = Want[I];
		L->SetWorldLocation(W.At);
		L->SetLightColor(W.Colour);
		L->SetIntensity(float(W.Intensity * Gain));
		L->SetAttenuationRadius(float(W.Reach * 100.0));
		if (!L->IsVisible()) L->SetVisibility(true);
	}
}

// MARK: - Staging (dev: shots)

void UAcBuildingFx::Stage(UAcSimSubsystem& Sim)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("AcStageBase"))) return;
	ac::Simulation& S = Sim.Simulation();
	ac::GameState& State = S.state;
	const ac::MapDefinition& Map = Sim.Map();
	if (State.players.empty() || Map.bases.empty()) return;
	using ac::Vec2;
	const int64 Start = State.players[0].start ? *State.players[0].start : Map.starts[0];
	const Vec2 Home = Map.bases[(size_t)Start].center;
	Vec2 Mid(0, 0);
	for (const ac::BaseSite& B : Map.bases) Mid = Mid + B.center;
	Mid = Mid / double(Map.bases.size());
	// Rows run along x (across the top-down camera's screen), stepping toward the middle.
	const Vec2 Way = ac::normalize(Mid - Home), Side(1, 0);

	auto AddUnit = [&](const ac::UnitKind Kind, const Vec2 P, const ac::Unit::Task Task, const int64 Structure) -> int64
	{
		ac::Unit U(State.nextID, Kind, 0, P, 0, Task);
		U.structure = Structure;
		State.nextID += 1;
		State.units.push_back(U);
		return U.id;
	};
	auto AddBuilding = [&](const ac::StructureKind Kind, const Vec2 P, const bool bBuilt) -> ac::Structure&
	{
		ac::Structure St(State.nextID, Kind, 0, P, bBuilt ? std::nullopt : std::optional<double>(ac::Rules::buildTime(Kind) * 0.5));
		State.nextID += 1;
		if (!bBuilt) St.hp = ac::Rules::hp(Kind) * 0.55;
		State.structures.push_back(St);
		ac::Structure& Out = State.structures.back();
		if (!bBuilt)
		{
			const Vec2 At = P + Vec2(0.6, ac::Rules::radius(Kind) + 0.6);
			Out.builder = AddUnit(ac::UnitKind::prospector, At, ac::Unit::Task::building, Out.id);
		}
		return Out;
	};
	auto Train = [&](ac::Structure& St)
	{
		const std::vector<ac::UnitKind> T = ac::Rules::trains(St.kind);
		if (T.empty()) return;
		St.line = std::vector<ac::UnitKind>{T[0], T[0], T[0]};
		St.training = ac::Rules::trainTime(T[0]) * 0.45;
	};
	auto Row = [&](const TArray<ac::StructureKind>& Kinds, const double Out, const bool bBuilt)
	{
		double Width = 0;
		for (const ac::StructureKind K : Kinds) Width += 2 * ac::Rules::radius(K) + 1.5 + (K == ac::StructureKind::garrison ? 2.5 : 0.0);
		double X = -Width / 2;
		// Out: cells along y (the screen's up and down), both rows 20 cells toward the middle.
		const Vec2 Centre = Home + Way * 20.0 + Vec2(0, Out);
		TArray<int64> Ids;
		for (const ac::StructureKind K : Kinds)
		{
			const double R = ac::Rules::radius(K);
			X += R + 0.75;
			Ids.Add(AddBuilding(K, Centre + Side * X, bBuilt).id);
			X += R + 0.75 + (K == ac::StructureKind::garrison ? 2.5 : 0.0);  // its Lab
		}
		UE_LOG(LogAutocraft, Log, TEXT("buildfx: staged a row of %d %s at (%.1f, %.1f)"), Kinds.Num(),
			bBuilt ? TEXT("working buildings") : TEXT("buildings under construction"), Centre.x, Centre.y);
		return Ids;
	};
	auto Find = [&](const int64 Id) -> ac::Structure*
	{
		for (ac::Structure& X : State.structures)
		{
			if (X.id == Id) return &X;
		}
		return nullptr;
	};
	auto AddLab = [&](const int64 ParentId, const bool bBuilt)
	{
		ac::Structure* Parent = Find(ParentId);
		if (!Parent) return;
		const Vec2 P = Parent->position + ac::Rules::addonOffset;
		ac::Structure& Lab = AddBuilding(ac::StructureKind::lab, P, bBuilt);
		Lab.parent = ParentId;
		Find(ParentId)->addon = Lab.id;
	};

	using K = ac::StructureKind;
	// The main Citadel trains Prospectors.
	for (ac::Structure& X : State.structures)
	{
		if (X.owner == 0 && X.kind == K::citadel) Train(X);
	}
	const TArray<int64> Working = Row({K::habDome, K::garrison, K::bastion, K::foundry, K::spacedock, K::sentinel}, -5.0, true);
	for (const int64 Id : Working)
	{
		ac::Structure* X = Find(Id);
		if (!X) continue;
		if (X->kind == K::garrison || X->kind == K::foundry || X->kind == K::spacedock) Train(*X);
		if (X->kind == K::bastion)
		{
			std::vector<int64_t> Crew;
			for (int32 I = 0; I < 3; ++I) Crew.push_back(AddUnit(ac::UnitKind::ranger, X->position, ac::Unit::Task::inBastion, Id));
			Find(Id)->crew = Crew;
		}
	}
	AddLab(Working[1], true);
	const TArray<int64> Rising =
		Row({K::citadel, K::habDome, K::garrison, K::bastion, K::foundry, K::spacedock, K::sentinel}, 5.0, false);
	AddLab(Rising[2], false);
	// Derricks on the two wells nearest the main: one pumping, one rising.
	if (State.wells)
	{
		std::vector<ac::Well>& Wells = *State.wells;
		TArray<int32> Order;
		for (int32 I = 0; I < (int32)Wells.size(); ++I) Order.Add(I);
		Order.Sort([&](const int32 A, const int32 B)
			{ return ac::distance(Wells[A].position, Home) < ac::distance(Wells[B].position, Home); });
		for (int32 N = 0; N < Order.Num() && N < 2; ++N)
		{
			const Vec2 P = Wells[Order[N]].position;
			ac::Structure& D = AddBuilding(K::derrick, P, N == 0);
			const int64 DerrickId = D.id;
			if (N == 0)
			{
				const int64 Who = AddUnit(ac::UnitKind::prospector, P, ac::Unit::Task::inDerrick, DerrickId);
				(*State.wells)[Order[N]].harvester = Who;
			}
			UE_LOG(LogAutocraft, Log, TEXT("buildfx: staged a %s Derrick at (%.1f, %.1f)"), N == 0 ? TEXT("pumping") : TEXT("rising"),
				P.x, P.y);
		}
	}
	UE_LOG(LogAutocraft, Log, TEXT("buildfx: staged player 0's base at (%.1f, %.1f), toward (%.2f, %.2f)"), Home.x, Home.y, Way.x, Way.y);
}
