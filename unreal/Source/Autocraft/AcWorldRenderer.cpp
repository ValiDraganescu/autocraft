#include "AcWorldRenderer.h"

#include "AcDaylight.h"
#include "AcFog.h"
#include "AcInstancedMesh.h"
#include "AcLog.h"
#include "AcModelCatalog.h"
#include "AcOutline.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"

#include "Async/ParallelFor.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/StaticMesh.h"
#include "ConvexVolume.h"
#include "SceneView.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "RHIStats.h"
#if STATS
#include "Stats/StatsData.h"
#endif

#include "TerrainField.h"

#include <atomic>
#include <cmath>

namespace
{
	TAutoConsoleVariable<int32> CVarHISM(TEXT("ac.RenderHISM"), 0,
		TEXT("Unit and building batches as hierarchical instanced meshes (1) or plain ISMs (0). Read when batches are made."));
	TAutoConsoleVariable<int32> CVarParallel(TEXT("ac.RenderParallel"), 1,
		TEXT("Pose units and buildings on worker threads (1) or on the game thread (0)."));
	TAutoConsoleVariable<int32> CVarNanite(TEXT("ac.RenderNanite"), 0,
		TEXT("Units' Nanite meshes drawn with Nanite (1) or as plain instanced meshes (0, default). In 5.8 on Metal, moving Nanite instances get no motion vectors and TSR smears them; buildings keep Nanite (they barely move). Read when batches are made."));
	TAutoConsoleVariable<int32> CVarUnitDistanceFields(TEXT("ac.RenderUnitDistanceFields"), 0,
		TEXT("Units in the distance-field scene (1: Lumen GI and distance-field shadows see them, and every moving unit dirties the global distance field each frame) or not (0, default, P2: ~1 ms GPU in a fight at 5K; units keep their shadow maps and screen traces). Read when batches are made."));
	TAutoConsoleVariable<int32> CVarShowAll(TEXT("ac.RenderShowAll"), 0,
		TEXT("Dev: draw every player's units and buildings, not only what the local player sees (the perf run's load)."));
	TAutoConsoleVariable<int32> CVarForceAnimated(TEXT("ac.RenderForceAnimated"), 0,
		TEXT("Dev: treat every pose as animated, so every part of every object is sent each frame (the worst case, for measuring)."));
	TAutoConsoleVariable<int32> CVarCull(TEXT("ac.RenderCull"), 1,
		TEXT("P2: units and buildings off screen (past ac.RenderCullMargin) are posed but not sent to their instances (1), or always sent (0)."));
	TAutoConsoleVariable<float> CVarCullMargin(TEXT("ac.RenderCullMargin"), 2500.f,
		TEXT("P2: how far past the view's edges (cm) objects are still sent: their shadows reach in."));
	TAutoConsoleVariable<int32> CVarOutline(TEXT("ac.Outline"), 1,
		TEXT("The outline pass (a buried Scorpion's outline, AcOutline.h): 1 on, 0 off. The pass costs nothing while no object has an outline."));
	TAutoConsoleVariable<int32> CVarCheckPrev(TEXT("ac.RenderCheckPrev"), 0,
		TEXT("Dev: log when an instance's previous transform (its motion vector) was another object's (a slot handed over)."));
	TAutoConsoleVariable<float> CVarStats(TEXT("ac.RenderStats"), 0.f,
		TEXT("Log a `render:` line (objects, instances, sync ms) every N seconds; 0 off."));

	/// Custom data per instance: team, emission, fade, char (AcModelCatalog.h).
	constexpr int32 CustomFloats = AcModelData::Count;

	const FTransform& Gone()
	{
		static const FTransform T(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
		return T;
	}

	/// A part this small casts no shadow (`Models+Flatten.swift:47`: < 0.1 cell).
	constexpr double NoShadowCm = 10.0;

	/// A counter of a shown stat group (`stat rhi`, `stat scenerendering`),
	/// its average over the stats frame; -1 if the group is not shown.
	double StatCounter(const TCHAR* Name)
	{
#if STATS
		const FGameThreadStatsData* D = FLatestGameThreadStatsData::Get().Latest;
		if (!D) return -1;
		const FName Short(Name);
		for (const FActiveStatGroupInfo& G : D->ActiveStatGroups)
		{
			for (const FComplexStatMessage& M : G.CountersAggregate)
			{
				if (M.NameAndInfo.GetShortName() != Short) continue;
				return M.NameAndInfo.GetField<EStatDataType>() == EStatDataType::ST_double
					? M.GetValue_double(EComplexStatField::IncAve)
					: double(M.GetValue_int64(EComplexStatField::IncAve));
			}
		}
#endif
		return -1;
	}

	bool SameTransform(const FTransform& A, const FTransform& B)
	{
		return A.GetTranslation().Equals(B.GetTranslation(), 0.01) && A.GetRotation().Equals(B.GetRotation(), 1e-6)
			&& A.GetScale3D().Equals(B.GetScale3D(), 1e-6);
	}
}

UAcWorldRenderer::UAcWorldRenderer()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UAcWorldRenderer* UAcWorldRenderer::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World) return nullptr;
	for (TActorIterator<AAcWorld> It(World); It; ++It)
	{
		if (It->Renderer) return It->Renderer;
	}
	return nullptr;
}

void UAcWorldRenderer::BeginPlay()
{
	Super::BeginPlay();
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim)
	{
		UE_LOG(LogAutocraft, Error, TEXT("render: no simulation"));
		return;
	}
	FrameHandle = Sim->AddFrameListener(EAcFrameStage::Renderer, FAcFrameEvent::FDelegate::CreateUObject(this, &UAcWorldRenderer::OnFrame));
	StartedHandle = Sim->OnGameStarted.AddUObject(this, &UAcWorldRenderer::OnGameStarted);
	if (Sim->IsRunning()) OnGameStarted(*Sim);
}

void UAcWorldRenderer::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this))
	{
		Sim->RemoveFrameListener(EAcFrameStage::Renderer, FrameHandle);
		Sim->OnGameStarted.Remove(StartedHandle);
	}
	Super::EndPlay(Reason);
}

void UAcWorldRenderer::OnGameStarted(UAcSimSubsystem& Sim)
{
	Clear();
	if (FieldMap != &Sim.Map() || !Field)
	{
		Field = std::make_unique<ac::TerrainField>(Sim.Map());
		FieldMap = &Sim.Map();
	}
	if (!bStaged)
	{
		bStaged = true;
		Stage(Sim);
	}
	if (!FAcModelCatalog::Get().IsLoaded()) UE_LOG(LogAutocraft, Error, TEXT("render: the model catalog is empty"));
}

void UAcWorldRenderer::OnFrame(const FAcFrame& Frame)
{
	Sync(Frame);
}

void UAcWorldRenderer::Clear()
{
	for (TUniquePtr<FObject>& O : Objects) Free(*O);
	Objects.Reset();
	ObjectIndex.Reset();
	for (FBatch& B : Batches)
	{
		if (B.Component)
		{
			B.Component->ClearInstances();
			// ClearInstances (5.8) keeps the previous transforms, so after a
			// new game they no longer match the instances one for one and the
			// engine drops the batch's per-instance motion vectors for good
			// (every moving unit doubled and smeared by TSR). Start them over.
			B.Component->SetHasPerInstancePrevTransforms(false);
			B.Component->SetHasPerInstancePrevTransforms(true);
		}
		B.bWarnedPrev = false;
		B.Xf.Reset();
		B.Shown.Reset();
		B.Owner.Reset();
		B.ShownOwner.Reset();
		B.LastMin = MAX_int32;
		B.LastMax = -1;
		B.DirtyBits.Reset();
		B.LastBits.Reset();
		B.Custom.Reset();
		B.Free.Reset();
		B.InComponent = 0;
		B.DirtyMin = B.CustomMin = MAX_int32;
		B.DirtyMax = B.CustomMax = -1;
	}
	LastSync.Reset();
	bFirstSync = true;
}

const FAcModelInfo* UAcWorldRenderer::ModelOf(const int64 Id) const
{
	const int32* I = ObjectIndex.Find(Id);
	return I ? Objects[*I]->Layout->Model : nullptr;
}

TConstArrayView<FTransform> UAcWorldRenderer::PartWorld(const int64 Id) const
{
	const int32* I = ObjectIndex.Find(Id);
	return I ? TConstArrayView<FTransform>(Objects[*I]->World) : TConstArrayView<FTransform>();
}

FAcUnitMemory* UAcWorldRenderer::Memory(const int64 Id)
{
	const int32* I = ObjectIndex.Find(Id);
	return I ? &Objects[*I]->Memory : nullptr;
}

void UAcWorldRenderer::ForEachObject(TFunctionRef<void(int64 Id, bool bUnit, const FAcModelInfo& Model, const FAcPose& Pose,
	TConstArrayView<FTransform> PartWorld)> Fn) const
{
	for (const TUniquePtr<FObject>& O : Objects)
	{
		if (O->Layout && O->Layout->Model && O->World.Num() == O->Layout->Model->Parts.Num())
		{
			Fn(O->Id, O->bUnit, *O->Layout->Model, O->Pose, O->World);
		}
	}
}

int32 UAcWorldRenderer::BatchFor(const FAcModelInfo& Model, const int32 Part, const int32 Mesh, const uint8 Stencil)
{
	const FAcModelMesh& X = Model.Parts[Part].Meshes[Mesh];
	// One batch per (mesh, material) asset pair: a part's mesh is its own
	// asset, so this is (model, part, material).
	// A stencil value makes its own set of components (custom stencil is per primitive).
	const uint64 Key = (GetTypeHash(X.Mesh.ToString()) | (uint64(GetTypeHash(X.MaterialInstance.ToString())) << 32))
		^ (uint64(Stencil) * 0x9E3779B97F4A7C15ull);
	if (const int32* Found = BatchIndex.Find(Key)) return *Found;

	UStaticMesh* Mesh_ = X.LoadMesh();
	UInstancedStaticMeshComponent* C = nullptr;
	if (Mesh_)
	{
		AActor* Owner = GetOwner();
		C = CVarHISM.GetValueOnGameThread() != 0
			? static_cast<UInstancedStaticMeshComponent*>(NewObject<UHierarchicalInstancedStaticMeshComponent>(Owner, NAME_None, RF_Transient))
			: static_cast<UInstancedStaticMeshComponent*>(NewObject<UAcInstancedMesh>(Owner, NAME_None, RF_Transient));
		C->SetMobility(EComponentMobility::Movable);
		C->SetStaticMesh(Mesh_);
		if (UMaterialInterface* M = X.LoadMaterial()) C->SetMaterial(0, M);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->bDisableCollision = true;
		C->SetCanEverAffectNavigation(false);
		C->SetGenerateOverlapEvents(false);
		C->SetNumCustomDataFloats(CustomFloats);
		// Previous transforms for motion vectors: without them TSR smears
		// everything that moves.
		C->SetHasPerInstancePrevTransforms(true);
		if (Model.Category == TEXT("unit") && CVarNanite.GetValueOnGameThread() == 0) C->SetForceDisableNanite(true);
		if (Model.Category == TEXT("unit") && CVarUnitDistanceFields.GetValueOnGameThread() == 0) C->bAffectDistanceFieldLighting = false;
		const FBox B = Mesh_->GetBoundingBox();
		const double Scale = Model.RestToModel(Part).GetMaximumAxisScale();
		if (B.GetSize().GetMax() * Scale < NoShadowCm) C->SetCastShadow(false);
		if (X.Blend != TEXT("opaque")) C->SetCastShadow(false);
		if (Stencil != AcOutline::StencilNone)
		{
			C->SetRenderCustomDepth(true);
			C->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_Default);
			C->SetCustomDepthStencilValue(Stencil);
		}
		C->SetupAttachment(Owner->GetRootComponent());
		C->RegisterComponent();
		Components.Add(C);
	}
	else
	{
		UE_LOG(LogAutocraft, Warning, TEXT("render: %s has no mesh %s"), *Model.Name.ToString(), *X.Mesh.ToString());
	}
	FBatch& Batch = Batches.AddDefaulted_GetRef();
	Batch.Component = C;
	// The mesh's reach from its pivot (cm, unscaled), for the batch's bounds (P2).
	Batch.Radius = Mesh_ ? Mesh_->GetBounds().Origin.Size() + Mesh_->GetBounds().SphereRadius : 0.0;
	const int32 Index = Batches.Num() - 1;
	BatchIndex.Add(Key, Index);
	return Index;
}

const UAcWorldRenderer::FLayout* UAcWorldRenderer::LayoutFor(const FAcModelInfo* Model, const uint8 Stencil)
{
	if (!Model) return nullptr;
	const TTuple<const FAcModelInfo*, uint8> Key(Model, Stencil);
	if (const TUniquePtr<FLayout>* Found = Layouts.Find(Key)) return Found->Get();
	TUniquePtr<FLayout> L = MakeUnique<FLayout>();
	L->Model = Model;
	L->Stencil = Stencil;
	for (int32 P = 0; P < Model->Parts.Num(); ++P)
	{
		for (int32 M = 0; M < Model->Parts[P].Meshes.Num(); ++M)
		{
			L->Part.Add(P);
			L->Batch.Add(BatchFor(*Model, P, M, Stencil));
		}
	}
	const FLayout* Out = L.Get();
	Layouts.Add(Key, MoveTemp(L));
	return Out;
}

namespace
{
	int32 Take(TArray<float>& Custom, TArray<FTransform>& Xf, TArray<int32>& Free)
	{
		if (Free.Num()) return Free.Pop(EAllowShrinking::No);
		Xf.Add(Gone());
		Custom.AddZeroed(CustomFloats);
		return Xf.Num() - 1;
	}
}

void UAcWorldRenderer::Allocate(FObject& O)
{
	auto Fill = [&](const FLayout& L, TArray<int32>& Slots)
	{
		Slots.SetNumUninitialized(L.Batch.Num());
		for (int32 K = 0; K < L.Batch.Num(); ++K)
		{
			FBatch& B = Batches[L.Batch[K]];
			const int32 S = Take(B.Custom, B.Xf, B.Free);
			Slots[K] = S;
			while (B.Owner.Num() <= S) B.Owner.Add(-1);
			B.Owner[S] = O.Id;
			// A slot taken over (freed this frame, by a unit gone or an
			// outline change) must not hand its last transform on as this
			// object's previous one: it appears, no motion.
			if (S < B.Shown.Num()) B.Shown[S] = Gone();
			float* C = &B.Custom[S * CustomFloats];
			C[AcModelData::TeamIndex] = float(O.Team);
			C[AcModelData::EmissionScale] = 1.f;
			C[AcModelData::Fade] = 0.f;
			C[AcModelData::Char] = 0.f;
			B.DirtyCustom(S);
			B.Xf[S] = Gone();
			B.Dirty(S);
		}
	};
	Fill(*O.Layout, O.Slots);
	if (O.Scaffold) Fill(*O.Scaffold, O.ScaffoldSlots);
}

void UAcWorldRenderer::Free(FObject& O)
{
	auto Release = [&](const FLayout* L, TArray<int32>& Slots)
	{
		if (!L) return;
		for (int32 K = 0; K < Slots.Num(); ++K)
		{
			FBatch& B = Batches[L->Batch[K]];
			B.Xf[Slots[K]] = Gone();
			B.Dirty(Slots[K]);
			B.Free.Add(Slots[K]);
			B.Owner[Slots[K]] = -1;
		}
		Slots.Reset();
	};
	Release(O.Layout, O.Slots);
	Release(O.Scaffold, O.ScaffoldSlots);
}

void UAcWorldRenderer::WriteObject(FObject& O)
{
	const FLayout& L = *O.Layout;
	const bool bHidden = O.Pose.bHidden;
	if (O.bChanged && !(bHidden && O.bDrawnHidden))
	{
		for (int32 K = 0; K < L.Batch.Num(); ++K)
		{
			const int32 P = L.Part[K];
			FBatch& B = Batches[L.Batch[K]];
			const int32 S = O.Slots[K];
			B.Xf[S] = (bHidden || !O.Pose.Visible[P]) ? Gone() : O.World[P];
			B.Dirty(S);
		}
		O.bDrawnHidden = bHidden;
		++Stats.Uploaded;
	}
	// Emission: only when a part's changed.
	if (O.LastEmission.Num() != O.Pose.Emission.Num())
	{
		O.LastEmission.Init(1.f, O.Pose.Emission.Num());
	}
	for (int32 K = 0, M = 0; K < L.Batch.Num(); ++K)
	{
		const int32 P = L.Part[K];
		M = (K > 0 && L.Part[K - 1] == P) ? M + 1 : 0;  // the mesh's index in its part
		float E = O.Pose.Emission[P];
		for (const FAcPose::FMeshEmission& X : O.Pose.MeshEmission)
		{
			if (X.Part == P && L.Model->Parts[P].Meshes[M].Material == X.Material) E = X.Value;
		}
		FBatch& B = Batches[L.Batch[K]];
		float& Slot = B.Custom[O.Slots[K] * CustomFloats + AcModelData::EmissionScale];
		if (Slot != E)
		{
			Slot = E;
			B.DirtyCustom(O.Slots[K]);
		}
	}
	O.LastEmission = O.Pose.Emission;
	// The scaffold: its rest pose at the site, until the building is done.
	if (O.Scaffold && O.bWantScaffold != O.bScaffoldShown)
	{
		const FAcModelInfo& M = *O.Scaffold->Model;
		const FVector Site = O.Pose.Placement.GetTranslation()
			- FVector(0, 0, O.Structure ? AcSpace::ToCm(AcPose::ConstructionSink(*O.Structure)) : 0.0);
		TArray<FTransform, TInlineAllocator<4>> W;
		W.SetNum(M.Parts.Num());
		for (int32 P = 0; P < M.Parts.Num(); ++P)
		{
			const FTransform Parent = M.Parts[P].Parent == INDEX_NONE ? FTransform(Site) : W[M.Parts[P].Parent];
			W[P] = M.Parts[P].Rest * Parent;
		}
		for (int32 K = 0; K < O.Scaffold->Batch.Num(); ++K)
		{
			FBatch& B = Batches[O.Scaffold->Batch[K]];
			const int32 S = O.ScaffoldSlots[K];
			B.Xf[S] = O.bWantScaffold ? W[O.Scaffold->Part[K]] : Gone();
			B.Dirty(S);
		}
		O.bScaffoldShown = O.bWantScaffold;
	}
}

void UAcWorldRenderer::Upload()
{
	TArray<FTransform> NewScratch, PrevScratch;
	const bool bCheckPrev = CVarCheckPrev.GetValueOnGameThread() != 0;
	for (FBatch& B : Batches)
	{
		if (!B.Component) continue;
		bool bAny = false;
		const int32 Old = B.InComponent;
		if (B.Xf.Num() > Old)
		{
			B.Component->AddInstances(TArray<FTransform>(B.Xf.GetData() + Old, B.Xf.Num() - Old), false, false, false);
			B.InComponent = B.Xf.Num();
			B.DirtyCustom(Old);
			B.DirtyCustom(B.Xf.Num() - 1);
			bAny = true;
		}
		// Transforms of the instances already in the component: this frame's
		// changes plus last frame's (their previous transform catches up).
		const int32 Min = FMath::Min(B.DirtyMin, B.LastMin);
		const int32 Max = FMath::Min(FMath::Max(B.DirtyMax, B.LastMax), Old - 1);
		B.LastMin = B.DirtyMin;
		B.LastMax = B.DirtyMax;
		if (Max >= Min || B.Xf.Num() > Old)
		{
			// The batch's bounds from what it shows (P2, AcInstancedMesh.h):
			// translations grown by the mesh's reach at the largest scale.
			if (UAcInstancedMesh* M = Cast<UAcInstancedMesh>(B.Component))
			{
				FVector Lo(UE_BIG_NUMBER), Hi(-UE_BIG_NUMBER);
				double Scale = 0;
				for (const FTransform& T : B.Xf)
				{
					const FVector S = T.GetScale3D();
					if (S.IsNearlyZero()) continue;
					Lo = Lo.ComponentMin(T.GetTranslation());
					Hi = Hi.ComponentMax(T.GetTranslation());
					Scale = FMath::Max(Scale, S.GetAbsMax());
				}
				M->SetBox(Scale > 0 ? FBox(Lo, Hi).ExpandBy(B.Radius * Scale + 1.0) : FBox(ForceInit));
			}
		}
		if (Max >= Min)
		{
			// The runs of slots changed this frame or last (P2): gaps of up to
			// `Join` unchanged slots are sent along rather than split.
			constexpr int32 Join = 16;
			auto Changed = [&B](const int32 I)
			{
				return (I < B.DirtyBits.Num() && B.DirtyBits[I]) || (I < B.LastBits.Num() && B.LastBits[I]);
			};
			auto Send = [&](const int32 From, const int32 To)
			{
				const int32 N = To - From + 1;
				NewScratch.SetNumUninitialized(N, EAllowShrinking::No);
				PrevScratch.SetNumUninitialized(N, EAllowShrinking::No);
				for (int32 I = 0; I < N; ++I)
				{
					const FTransform& Now = B.Xf[From + I];
					const FTransform& Was = B.Shown[From + I];
					NewScratch[I] = Now;
					// Appearing (or vanishing) is a teleport, not a motion.
					const bool bJump = Was.GetScale3D().IsNearlyZero() || Now.GetScale3D().IsNearlyZero();
					PrevScratch[I] = bJump ? Now : Was;
					B.Shown[From + I] = Now;
					if (bCheckPrev)
					{
						// The previous transform must be the same object's.
						const int32 J = From + I;
						while (B.ShownOwner.Num() <= J) B.ShownOwner.Add(-1);
						const int64 Is = B.Owner.IsValidIndex(J) ? B.Owner[J] : -1;
						const int64 WasId = B.ShownOwner[J];
						++Stats.PrevChecked;
						// -1: not known (the check was turned on after the slot was last sent).
						if (!bJump && WasId >= 0 && WasId != Is)
						{
							++Stats.PrevForeign;
							if (PrevForeignLogged++ < 40)
							{
								UE_LOG(LogAutocraft, Warning, TEXT("render: slot %d of %s: previous transform was object %lld's, now object %lld's (moved %.0f cm)"),
									J, B.Component ? *GetNameSafe(B.Component->GetStaticMesh()) : TEXT("?"), (long long)WasId, (long long)Is,
									FVector::Dist(Was.GetTranslation(), Now.GetTranslation()));
							}
						}
						B.ShownOwner[J] = Now.GetScale3D().IsNearlyZero() ? -1 : Is;
					}
				}
				B.Component->BatchUpdateInstancesTransforms(From, NewScratch, PrevScratch, false, false, false);
			};
			int32 RunStart = -1, RunEnd = -1;
			for (int32 I = Min; I <= Max; ++I)
			{
				if (!Changed(I)) continue;
				if (RunStart >= 0 && I - RunEnd > Join)
				{
					Send(RunStart, RunEnd);
					RunStart = -1;
				}
				if (RunStart < 0) RunStart = I;
				RunEnd = I;
			}
			if (RunStart >= 0) Send(RunStart, RunEnd);
			bAny = true;
		}
		// This frame's changes are next frame's catch-up.
		Swap(B.LastBits, B.DirtyBits);
		B.DirtyBits.Init(false, B.LastBits.Num());
		for (int32 I = B.Shown.Num(); I < B.Xf.Num(); ++I)
		{
			B.Shown.Add(B.Xf[I]);
			if (bCheckPrev)
			{
				while (B.ShownOwner.Num() <= I) B.ShownOwner.Add(-1);
				B.ShownOwner[I] = B.Xf[I].GetScale3D().IsNearlyZero() ? -1 : (B.Owner.IsValidIndex(I) ? B.Owner[I] : -1);
			}
		}
		if (B.CustomMax >= B.CustomMin)
		{
			B.Component->SetCustomData(B.CustomMin, B.CustomMax,
				TConstArrayView<float>(B.Custom.GetData() + B.CustomMin * CustomFloats, (B.CustomMax - B.CustomMin + 1) * CustomFloats), false);
			bAny = true;
		}
		// The engine sends per-instance previous transforms only while there
		// is one per instance; otherwise the batch has no motion vectors and
		// TSR ghosts everything in it that moves.
		if (B.Component->PerInstancePrevTransform.Num() != B.Component->PerInstanceSMData.Num() && !B.bWarnedPrev)
		{
			B.bWarnedPrev = true;
			UE_LOG(LogAutocraft, Warning, TEXT("render: %s has %d previous transforms for %d instances: no motion vectors"),
				*GetNameSafe(B.Component->GetStaticMesh()), B.Component->PerInstancePrevTransform.Num(), B.Component->PerInstanceSMData.Num());
		}
		// The instance tracker already marks the render instances dirty: an
		// incremental GPU-scene update. MarkRenderStateDirty would rebuild
		// the proxy every frame (slow, and TSR loses the motion vectors).
		if (bAny) B.Component->MarkRenderInstancesDirty();
		B.DirtyMin = B.CustomMin = MAX_int32;
		B.DirtyMax = B.CustomMax = -1;
	}
}

TOptional<FConvexVolume> UAcWorldRenderer::CullVolume() const
{
	// The view the local player renders (as the engine builds it).
	const UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	ULocalPlayer* LP = PC ? PC->GetLocalPlayer() : nullptr;
	if (!LP || !LP->ViewportClient || !LP->ViewportClient->Viewport) return {};
	FSceneViewProjectionData Data;
	if (!LP->GetProjectionData(LP->ViewportClient->Viewport, Data)) return {};
	FConvexVolume Volume;
	GetViewFrustumBounds(Volume, Data.ComputeViewProjectionMatrix(), false);
	return Volume;
}

void UAcWorldRenderer::Sync(const FAcFrame& Frame)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(AcWorldRenderer_Sync);
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || !Sim->IsRunning() || !Field) return;
	const FAcModelCatalog& Catalog = FAcModelCatalog::Get();
	if (!Catalog.IsLoaded()) return;

	// -AcRenderRebuildAt=N (a test aid): at the Nth frame, rebuild as for a
	// new game (what `OnGameStarted` does after ac.Restart, ac.NewGame or the
	// victory lap), without touching the game.
	static const int32 RebuildAt = []{ int32 N = 0; FParse::Value(FCommandLine::Get(), TEXT("AcRenderRebuildAt="), N); return N; }();
	if (RebuildAt > 0 && FrameNumber + 1 == uint32(RebuildAt))
	{
		UE_LOG(LogAutocraft, Log, TEXT("render: rebuilding as for a new game (-AcRenderRebuildAt=%d)"), RebuildAt);
		OnGameStarted(*Sim);
	}

	const double T0 = FPlatformTime::Seconds();
	Stats = FStats();
	++FrameNumber;
	const ac::GameState& Shown = CVarShowAll.GetValueOnGameThread() != 0 ? Sim->State() : Sim->Shown();
	// The render clock (Swift `world.sync(time: clock)`): runs on while paused.
	const double Time = Frame.Clock;
	const double Dt = FMath::Max(0.0, Time - LastSync.Get(Time));
	LastSync = Time;

	// 1. Who shot (lastShot), before the poses.
	for (const ac::GameEvent& E : Frame.Events)
	{
		if (const auto* S = E.as<ac::GameEvent::Shot>())
		{
			if (FAcUnitMemory* M = Memory(S->unit))
			{
				M->LastShot = Time;
				M->LastTarget = S->target;
				M->LastShotAt = S->at;
			}
		}
		else if (const auto* Miss = E.as<ac::GameEvent::Missed>())
		{
			if (FAcUnitMemory* M = Memory(Miss->unit))
			{
				M->LastShot = Time;
				M->LastTarget.Reset();
				M->LastShotAt = Miss->at;
			}
		}
	}

	// 2. Objects follow what is shown.
	auto Touch = [&](const int64 Id, const bool bUnit, const FAcModelInfo* Model, const FAcModelInfo* Scaffold,
					 const int64 Owner) -> FObject*
	{
		if (const int32* I = ObjectIndex.Find(Id)) return Objects[*I].Get();
		const FLayout* L = LayoutFor(Model);
		if (!L) return nullptr;
		TUniquePtr<FObject> O = MakeUnique<FObject>();
		O->Id = Id;
		O->bUnit = bUnit;
		O->Team = (int32)FMath::Clamp<int64>(Owner, 0, 7);
		O->Layout = L;
		O->Scaffold = LayoutFor(Scaffold);
		O->Memory.FirstSeen = Time;
		if (!bFirstSync) O->Memory.BornAt = Time;
		Allocate(*O);
		ObjectIndex.Add(Id, Objects.Num());
		return Objects.Add_GetRef(MoveTemp(O)).Get();
	};
	auto ModelNamed = [&](const FString& Name) { return Catalog.Find(FName(*Name)); };
	for (const ac::Structure& S : Shown.structures)
	{
		const TCHAR* Base = AcPose::ModelBase(S.kind);
		static TMap<uint8, TPair<const FAcModelInfo*, const FAcModelInfo*>> Cache;
		TPair<const FAcModelInfo*, const FAcModelInfo*>* C = Cache.Find((uint8)S.kind);
		if (!C || !C->Key)
		{
			C = &Cache.Add((uint8)S.kind, {ModelNamed(FString::Printf(TEXT("%s_blue"), Base)),
				ModelNamed(FString::Printf(TEXT("scaffold_%s_blue"), Base))});
		}
		FObject* O = Touch(S.id, false, C->Key, C->Value, S.owner);
		if (!O) continue;
		O->Structure = &S;
		O->Unit = nullptr;
		O->SeenFrame = FrameNumber;
	}
	for (const ac::Unit& U : Shown.units)
	{
		static const FAcModelInfo* Cache[ac::allCases<ac::UnitKind>().size()] = {};
		const FAcModelInfo*& M = Cache[(int32)U.kind];
		if (!M) M = ModelNamed(FString::Printf(TEXT("%s_blue"), AcPose::ModelBase(U.kind)));
		FObject* O = Touch(U.id, true, M, nullptr, U.owner);
		if (!O) continue;
		O->Unit = &U;
		O->Structure = nullptr;
		O->SeenFrame = FrameNumber;
	}
	// Gone: hand over the last pose, free the slots.
	for (int32 I = Objects.Num() - 1; I >= 0; --I)
	{
		FObject& O = *Objects[I];
		if (O.SeenFrame == FrameNumber) continue;
		OnRemoved.Broadcast(O.Id, *O.Layout->Model, O.World);
		Free(O);
		ObjectIndex.Remove(O.Id);
		if (I != Objects.Num() - 1)
		{
			Objects[I] = MoveTemp(Objects.Last());
			ObjectIndex[Objects[I]->Id] = I;
		}
		Objects.Pop(EAllowShrinking::No);
	}
	// Outlines (AcOutline.h): a unit whose outline changed moves its
	// instances to the components that write that stencil value.
	const bool bOutlines = CVarOutline.GetValueOnGameThread() != 0;
	const int64 LocalPlayer = Sim->LocalPlayer();
	for (TUniquePtr<FObject>& OPtr : Objects)
	{
		FObject& O = *OPtr;
		const uint8 Want = (bOutlines && O.bUnit && O.Unit)
			? AcOutline::StencilFor(O.Unit->kind, O.Unit->anchor, O.Unit->owner, Shown.allied(O.Unit->owner, LocalPlayer))
			: AcOutline::StencilNone;
		if (Want == O.Stencil) continue;
		Free(O);
		O.Stencil = Want;
		O.Layout = LayoutFor(O.Layout->Model, Want);
		Allocate(O);
		O.bFresh = true;
		O.bDrawnHidden = false;
		O.bChanged = true;
	}
	const double T1 = FPlatformTime::Seconds();

	// 3. Poses.
	const int64 Local = Sim->LocalPlayer();
	// Driving (E4): the others' buildings out of sight are not drawn.
	// Fogged: the others' buildings out of sight are shown as last seen only: frozen, unlit, inert.
	const std::set<int64_t>* Veiled = nullptr;
	const std::set<int64_t>* Fogged = nullptr;
	if (AAcFog* Fog = AAcFog::Find(GetWorld()))
	{
		Fogged = &Fog->Unseen();
		if (bVeiled) Veiled = Fogged;
	}
	const AAcDaylight* Daylight = AAcDaylight::Find(GetWorld());
	const double Dark = Daylight ? Daylight->GetDark() : 0.0;
	const bool bForceAnimated = CVarForceAnimated.GetValueOnGameThread() != 0;
	std::atomic<int32> Posed{0};
	auto PoseOne = [&](const int32 I)
	{
		FObject& O = *Objects[I];
		const FAcModelInfo& Model = *O.Layout->Model;
		const bool bFrozen = !O.bUnit && Fogged && Fogged->count(O.Id) > 0;
		if (bFrozen && !O.bFrozen) O.FrozenAt = Time;
		O.bFrozen = bFrozen;
		FAcPoseContext C{Shown, O.Unit, O.Structure, Model, *Field, O.Memory, bFrozen ? O.FrozenAt : Time, bFrozen ? 0.0 : Dt,
			O.bUnit && O.Id == DrivenUnit, Local};
		if (C.bDriven && PilotAim) C.PilotAim = &*PilotAim;
		C.Dark = Dark;
		FAcPoseFn Fn;
		if (O.bUnit)
		{
			AcPose::RestUnit(C, O.Pose);
			const ac::Unit& U = *O.Unit;
			// Inside a Bastion, a Dropship or a Derrick; driven in first person.
			O.Pose.bHidden = U.task == ac::Unit::Task::inBastion || U.task == ac::Unit::Task::aboard
				|| U.task == ac::Unit::Task::inDerrick || U.id == HiddenUnit;
			Fn = AcPose::Find(U.kind);
		}
		else
		{
			AcPose::RestStructure(C, O.Pose);
			O.Pose.bHidden = Veiled && Veiled->count(O.Id) > 0;
			O.bWantScaffold = !O.Structure->complete() && !O.Pose.bHidden;
			Fn = AcPose::Find(O.Structure->kind);
		}
		if (Fn && !O.Pose.bHidden) Fn(C, O.Pose);
		if (bFrozen)
		{
			// No lamps, glow, working effects or animation: the shell as last seen.
			O.Pose.bFrozen = true;
			O.Pose.bAnimated = false;
			O.Pose.Cues.Reset();
			for (float& E : O.Pose.Emission) E = 0.f;
			for (FAcPose::FMeshEmission& X : O.Pose.MeshEmission) X.Value = 0.f;
		}

		const bool bMoved = O.bFresh || O.Pose.bAnimated || bForceAnimated || O.Pose.bHidden != O.bDrawnHidden || O.bFrozen != O.bFrozenDrawn
			|| !SameTransform(O.Pose.Placement, O.LastPlacement);
		O.bChanged = bMoved;
		O.bFresh = false;
		O.bFrozenDrawn = O.bFrozen;
		if (!bMoved || O.Pose.bHidden) return;
		O.LastPlacement = O.Pose.Placement;
		const int32 N = Model.Parts.Num();
		O.World.SetNumUninitialized(N);
		for (int32 P = 0; P < N; ++P)
		{
			const int32 Parent = Model.Parts[P].Parent;
			if (Parent != INDEX_NONE)
			{
				O.World[P] = O.Pose.Local[P] * O.World[Parent];
				O.Pose.Visible[P] = O.Pose.Visible[P] && O.Pose.Visible[Parent];
			}
			else
			{
				O.World[P] = O.Pose.Local[P] * O.Pose.Placement;
			}
		}
		Posed.fetch_add(1, std::memory_order_relaxed);
	};
	if (CVarParallel.GetValueOnGameThread() != 0 && Objects.Num() > 64)
	{
		ParallelFor(TEXT("AcWorldRenderer.Pose"), Objects.Num(), 32, PoseOne);
	}
	else
	{
		for (int32 I = 0; I < Objects.Num(); ++I) PoseOne(I);
	}
	const double T2 = FPlatformTime::Seconds();

	// 4. Into the batches, then one upload per batch. Off screen (P2): posed
	// (rays and effects read the parts), not written; written whole, as a
	// jump (no motion blur), once back in view.
	TOptional<FConvexVolume> View;
	if (CVarCull.GetValueOnGameThread() != 0) View = CullVolume();
	const double Margin = CVarCullMargin.GetValueOnGameThread();
	for (TUniquePtr<FObject>& O : Objects)
	{
		const bool bOut = View && !View->IntersectSphere(O->Pose.Placement.GetTranslation(), float((O->bUnit ? 400.0 : 800.0) + Margin));
		if (bOut)
		{
			O->bCulled = true;
			++Stats.Culled;
		}
		else
		{
			if (O->bCulled)
			{
				O->bCulled = false;
				O->bChanged = true;
				O->bDrawnHidden = !O->Pose.bHidden;
				for (int32 K = 0; K < O->Slots.Num(); ++K) Batches[O->Layout->Batch[K]].Shown[O->Slots[K]] = Gone();
			}
			WriteObject(*O);
		}
		for (const FAcPoseCue& Cue : O->Pose.Cues) OnCue.Broadcast(O->Id, Cue);
	}
	const double T3 = FPlatformTime::Seconds();
	Upload();
	const double T4 = FPlatformTime::Seconds();
	bFirstSync = false;

	Stats.Objects = Objects.Num();
	Stats.Batches = Batches.Num();
	for (const FBatch& B : Batches) Stats.Instances += B.Live();
	// The outline pass runs only while an object is drawn with a stencil value.
	for (const TUniquePtr<FObject>& O : Objects) Stats.Outlined += (O->Stencil != 0 && !O->Pose.bHidden) ? 1 : 0;
	if (AAcWorld* World = Cast<AAcWorld>(GetOwner()); World && World->Outline) World->Outline->bEnabled = Stats.Outlined > 0;
	Stats.Posed = Posed.load();
	Stats.PrepareMs = (T1 - T0) * 1000.0;
	Stats.PoseMs = (T2 - T1) * 1000.0;
	Stats.WriteMs = (T3 - T2) * 1000.0;
	Stats.UploadMs = (T4 - T3) * 1000.0;
	if (CVarCheckPrev.GetValueOnGameThread() != 0)
	{
		PrevCheckedSum += Stats.PrevChecked;
		PrevForeignSum += Stats.PrevForeign;
		if (FrameNumber % 60 == 0)
		{
			UE_LOG(LogAutocraft, Log, TEXT("render: prev check: %lld instances sent, %lld with another object's previous transform"),
				(long long)PrevCheckedSum, (long long)PrevForeignSum);
		}
	}

	const float Every = CVarStats.GetValueOnGameThread();
	if (Every > 0.f)
	{
		++Sum.Frames;
		Sum.Prepare += Stats.PrepareMs;
		Sum.Pose += Stats.PoseMs;
		Sum.Write += Stats.WriteMs;
		Sum.Upload += Stats.UploadMs;
		Sum.Max = FMath::Max(Sum.Max, (T4 - T0) * 1000.0);
		Sum.Uploaded += Stats.Uploaded;
		// Last frame's RHI counts (all views and passes: base, shadows, ...).
		Sum.Draws += GNumDrawCallsRHI[0];
		Sum.Prims += GNumPrimitivesDrawnRHI[0];
		const double Now = FPlatformTime::Seconds();
		if (StatsWall == 0.0) StatsWall = Now;
		if (Now - StatsWall >= Every)
		{
			const double F = FMath::Max(1, Sum.Frames);
			UE_LOG(LogAutocraft, Log,
				TEXT("render: %d objects, %d instances in %d batches | sync %.2f ms (max %.2f): prepare %.2f pose %.2f write %.2f upload %.2f | %.0f objects moved a frame | %.0f draws, %.2fM triangles a frame (rhi); stat rhi draws %.0f, mesh draw commands %.0f | %d frames"),
				Stats.Objects, Stats.Instances, Stats.Batches, (Sum.Prepare + Sum.Pose + Sum.Write + Sum.Upload) / F, Sum.Max,
				Sum.Prepare / F, Sum.Pose / F, Sum.Write / F, Sum.Upload / F, Sum.Uploaded / F, Sum.Draws / F, Sum.Prims / F / 1e6, StatCounter(TEXT("STAT_RHIDraws")), StatCounter(TEXT("STAT_MeshDrawCalls")), Sum.Frames);
			Sum = FStatsSum();
			StatsWall = Now;
		}
	}
}

// MARK: - Staging (dev: shots and the perf run)

void UAcWorldRenderer::Stage(UAcSimSubsystem& Sim)
{
	const TCHAR* Cmd = FCommandLine::Get();
	int32 FightSize = 0, Each = 0;
	FParse::Value(Cmd, TEXT("AcStageFight="), FightSize);
	FParse::Value(Cmd, TEXT("AcStage="), Each);
	const bool bBuildings = FParse::Param(Cmd, TEXT("AcStageBuildings"));
	if (FParse::Param(Cmd, TEXT("AcStageIntel")))
	{
		// Dev: the local player has "remembered" every hostile building (hurt a little), none in sight.
		ac::GameState& G = Sim.Simulation().state;
		if (!G.intel) G.intel = std::vector<ac::Intel>();
		G.intel->resize(G.players.size());
		for (const ac::Structure& X : G.structures)
		{
			if (!G.hostile(X.owner, Sim.LocalPlayer())) continue;
			ac::Structure Copy = X;
			Copy.hp = Copy.hp * 0.6;
			(*G.intel)[(size_t)Sim.LocalPlayer()].buildings.push_back(Copy);
			UE_LOG(LogAutocraft, Log, TEXT("stage intel: kind %d at %.1f,%.1f"), (int)X.kind, X.position.x, X.position.y);
		}
	}
	FString BuriedMode;
	FParse::Value(Cmd, TEXT("AcStageBuried="), BuriedMode);
	if (FightSize <= 0 && Each <= 0 && !bBuildings && BuriedMode.IsEmpty()) return;

	ac::Simulation& S = Sim.Simulation();
	ac::GameState& State = S.state;
	const ac::MapDefinition& Map = Sim.Map();
	using ac::Vec2;
	auto Home = [&](const size_t P) -> Vec2
	{
		const int64_t B = P < State.players.size() && State.players[P].start ? *State.players[P].start : Map.starts[P % Map.starts.size()];
		return Map.bases[(size_t)B].center;
	};
	const size_t Players = State.players.size();
	auto Open = [&](const Vec2 P, const double Clear)
	{
		if (S.nav && !S.nav->walkable(P)) return false;
		for (const ac::Structure& X : State.structures)
		{
			if (ac::distance(X.position, P) < ac::Rules::radius(X.kind) + Clear) return false;
		}
		for (const ac::OreDeposit& X : State.patches)
		{
			if (ac::distance(X.position, P) < 1.5 + Clear - 1.0) return false;
		}
		if (State.wells)
		{
			for (const ac::Well& X : *State.wells)
			{
				if (ac::distance(X.position, P) < 2.5 + Clear - 1.0) return false;
			}
		}
		return true;
	};
	auto Army = [](const int32 Size)
	{
		// Bench.stageFight: 16:4:3:2:2:2 per 29.
		const int32 N = (Size + 28) / 29;
		std::vector<ac::UnitKind> A;
		const std::pair<ac::UnitKind, int32> Mix[] = {{ac::UnitKind::ranger, 16}, {ac::UnitKind::juggernaut, 4},
			{ac::UnitKind::firefly, 3}, {ac::UnitKind::comet, 2}, {ac::UnitKind::longbow, 2}, {ac::UnitKind::dropship, 2}};
		for (const auto& [Kind, Count] : Mix)
		{
			for (int32 I = 0; I < Count * N; ++I) A.push_back(Kind);
		}
		A.resize((size_t)Size);
		return A;
	};
	int64 Added = 0;
	auto Put = [&](const ac::UnitKind Kind, const int64 Owner, const Vec2 P, const double Facing, bool& bAnchored)
	{
		ac::Unit U(State.nextID, Kind, Owner, P, Facing, ac::Unit::Task::idle);
		State.nextID += 1;
		if (Kind == ac::UnitKind::dropship)
		{
			U.energy = 120;
			U.cargo = std::vector<int64_t>{};
		}
		if (Kind == ac::UnitKind::longbow)
		{
			U.anchor = bAnchored ? 0.0 : 1.0;
			U.anchored = !bAnchored;
			U.aim = Facing;
			bAnchored = !bAnchored;
		}
		State.units.push_back(U);
		++Added;
	};

	if (!BuriedMode.IsEmpty() && Players >= 2)
	{
		// Dev: two buried Scorpions in front of the local player's main (and, for
		// `owner`, one walking beside them), for the outline pass's shots. `owner`:
		// the local player's own; `enemy`: the first enemy's, given away by a
		// reload in progress (a buried Scorpion shows to its enemies then), so the
		// local player sees them. `-AcCamAtArmy` centres the camera there.
		const bool bEnemy = BuriedMode.Equals(TEXT("enemy"), ESearchCase::IgnoreCase);
		int32 FoeP = 1;
		for (size_t P = 1; P < Players; ++P)
		{
			if (State.hostile(0, (int64_t)P)) { FoeP = (int32)P; break; }
		}
		const Vec2 HomeP = Home(0), Foe = Home((size_t)FoeP);
		const Vec2 Way = ac::normalize(Foe - HomeP), Side(-Way.y, Way.x);
		double Dist = 9.0;
		FParse::Value(Cmd, TEXT("AcStageBuriedAt="), Dist);
		Vec2 Middle = HomeP + Way * Dist;
		for (double D = Dist; D < Dist + 14.0; D += 1.0)
		{
			const Vec2 C = HomeP + Way * D;
			if (Open(C, 1.5) && Open(C + Side * 2.4, 1.5) && Open(C - Side * 2.4, 1.5)) { Middle = C; break; }
		}
		const double Facing = std::atan2(Way.y, Way.x);
		const int64 Owner = bEnemy ? FoeP : 0;
		int32 Count = 0;
		for (const double Offset : {-2.2, 0.0, 2.2})
		{
			// Owner: the middle one walks (no outline) for comparison. Enemy: just the two outer ones.
			const bool bWalking = !bEnemy && Offset == 0.0;
			if (bEnemy && Offset == 0.0) continue;
			ac::Unit U(State.nextID, ac::UnitKind::scorpion, Owner, Middle + Side * Offset, Facing, ac::Unit::Task::idle);
			State.nextID += 1;
			if (!bWalking)
			{
				U.anchor = 1.0;
				U.anchored = true;
				U.aim = Facing;
				if (bEnemy) U.cooldown = 20.0;
			}
			State.units.push_back(U);
			++Count;
		}
		// A game set up by hand: the fog's sight and what is shown know of them at once.
		S.lookNow();
		FightCentre = FVector2D(Middle.x, Middle.y);
		UE_LOG(LogAutocraft, Log, TEXT("render: staged %d Scorpions (%s, owner %lld) at (%.1f, %.1f) (-AcCamAt=%.1f,%.1f)"), Count,
			*BuriedMode, (long long)Owner, Middle.x, Middle.y, Middle.x, Middle.y);
	}

	if (FightSize > 0 && Players >= 2)
	{
		// Bench.stageFight, two armies between the first two mains (with
		// teams: player 0's and its first enemy's).
		int32 FoeP = 1;
		for (size_t P = 1; P < Players; ++P)
		{
			if (State.hostile(0, (int64_t)P)) { FoeP = (int32)P; break; }
		}
		const Vec2 HomeP = Home(0), Foe = Home((size_t)FoeP);
		const Vec2 Way = ac::normalize(Foe - HomeP), Side(-Way.y, Way.x);
		const int32 Row = FightSize > 29 ? 20 : 8;
		const std::vector<ac::UnitKind> A = Army(FightSize);
		auto Places = [&](const Vec2 Middle, const int32 Owner)
		{
			const Vec2 Back = Owner == 0 ? -Way : Way;
			std::vector<Vec2> Out;
			for (int32 K = 0; K < (int32)A.size(); ++K)
			{
				Out.push_back(Middle + Back * (2.5 + double(K / Row) * 1.5) + Side * (double(K % Row) - double(Row - 1) / 2) * 1.3);
			}
			return Out;
		};
		Vec2 Middle = (HomeP + Foe) / 2;
		double Best = -1e300;
		for (int32 Ti = 0; Ti <= 10; ++Ti)
		{
			const double T = 0.25 + Ti * 0.05;
			for (int32 Oi = 0; Oi <= 16; ++Oi)
			{
				const double O = -24.0 + Oi * 3.0;
				const Vec2 C = HomeP + (Foe - HomeP) * T + Side * O;
				int32 Room = 0;
				for (const int32 Owner : {0, FoeP})
				{
					for (const Vec2& P : Places(C, Owner)) Room += Open(P, 1.0) ? 1 : 0;
				}
				const double Score = double(Room) * 10 - FMath::Abs(T - 0.5) * 10 - FMath::Abs(O) * 0.1;
				if (Score > Best)
				{
					Best = Score;
					Middle = C;
				}
			}
		}
		for (const int32 Owner : {0, FoeP})
		{
			bool bAnchored = false;
			const double Facing = Owner == 0 ? std::atan2(Way.y, Way.x) : std::atan2(-Way.y, -Way.x);
			const std::vector<Vec2> P = Places(Middle, Owner);
			for (size_t K = 0; K < P.size(); ++K)
			{
				if (Open(P[K], 1.0)) Put(A[K], Owner, P[K], Facing, bAnchored);
			}
		}
		FightCentre = FVector2D(Middle.x, Middle.y);
		UE_LOG(LogAutocraft, Log, TEXT("render: staged a fight of %d a side at (%.1f, %.1f) (-AcCamAt=%.1f,%.1f)"), FightSize,
			Middle.x, Middle.y, Middle.x, Middle.y);
	}

	Vec2 Centre(0, 0);
	for (size_t P = 0; P < Players; ++P) Centre = Centre + Home(P);
	Centre = Centre / double(FMath::Max<size_t>(Players, 1));

	if (bBuildings)
	{
		// A late game's base around every main: finished, one Garrison still rising.
		const ac::StructureKind Kinds[] = {ac::StructureKind::garrison, ac::StructureKind::garrison, ac::StructureKind::foundry,
			ac::StructureKind::spacedock, ac::StructureKind::habDome, ac::StructureKind::habDome, ac::StructureKind::habDome,
			ac::StructureKind::habDome, ac::StructureKind::bastion, ac::StructureKind::sentinel, ac::StructureKind::sentinel,
			ac::StructureKind::garrison};
		int32 Built = 0;
		for (size_t P = 0; P < Players; ++P)
		{
			const Vec2 H = Home(P);
			const Vec2 In = ac::normalize(Centre - H);
			const double Toward = std::atan2(In.y, In.x);
			int32 K = 0;
			for (int32 Ring = 0; Ring < 4 && K < (int32)UE_ARRAY_COUNT(Kinds); ++Ring)
			{
				const double R = 9.0 + Ring * 3.5;
				for (int32 Step = 0; Step < 24 && K < (int32)UE_ARRAY_COUNT(Kinds); ++Step)
				{
					// Away from the ore arc (it faces off the map's middle, mostly): start toward the middle.
					const double A = Toward + (Step % 2 ? 1 : -1) * ((Step + 1) / 2) * (UE_DOUBLE_TWO_PI / 24);
					const Vec2 Pos = H + Vec2(std::cos(A), std::sin(A)) * R;
					const ac::StructureKind Kind = Kinds[K];
					if (!Open(Pos, ac::Rules::radius(Kind) + 0.5)) continue;
					const bool bRising = K == (int32)UE_ARRAY_COUNT(Kinds) - 1;
					ac::Structure St(State.nextID, Kind, (int64_t)P, Pos,
						bRising ? std::optional<double>(ac::Rules::buildTime(Kind) * 0.5) : std::nullopt);
					State.nextID += 1;
					State.structures.push_back(St);
					++K;
					++Built;
				}
			}
		}
		UE_LOG(LogAutocraft, Log, TEXT("render: staged %d buildings"), Built);
	}

	if (Each > 0)
	{
		// Every player's army in rows of 20 before its main, facing the middle.
		const std::vector<ac::UnitKind> A = Army(Each);
		for (size_t P = 0; P < Players; ++P)
		{
			const Vec2 H = Home(P);
			const Vec2 Way = ac::normalize(Centre - H), Side(-Way.y, Way.x);
			const double Facing = std::atan2(Way.y, Way.x);
			bool bAnchored = false;
			int32 Placed = 0;
			for (int32 K = 0; Placed < Each && K < Each * 4; ++K)
			{
				const int32 Row = 20;
				const Vec2 Pos = H + Way * (16.0 + double(K / Row) * 1.5) + Side * (double(K % Row) - double(Row - 1) / 2) * 1.3;
				if (!Open(Pos, 1.0)) continue;
				Put(A[(size_t)Placed], (int64)P, Pos, Facing, bAnchored);
				++Placed;
			}
		}
		UE_LOG(LogAutocraft, Log, TEXT("render: staged %d units for %d players"), (int32)Added, (int32)Players);
	}
}

// MARK: - AAcWorld

AAcWorld::AAcWorld()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Renderer = CreateDefaultSubobject<UAcWorldRenderer>(TEXT("Renderer"));
	Outline = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Outline"));
	Outline->SetupAttachment(RootComponent);
	Outline->bUnbound = true;
	// After the pilot's veil (priority 1), so its vignette does not dim the line.
	Outline->Priority = 2.0f;
	Outline->bEnabled = false;
}

void AAcWorld::BeginPlay()
{
	Super::BeginPlay();
	// M_AcOutline (Tools/Editor/make_outline_material.py): without it, no outline.
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_AcOutline.M_AcOutline"));
	if (Material) Outline->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, Material));
	else UE_LOG(LogAutocraft, Warning, TEXT("outline: no /Game/Materials/M_AcOutline (run Tools/Editor/make_outline_material.py)"));
}

AAcWorld* AAcWorld::SpawnFor(UWorld* World)
{
	if (!World) return nullptr;
	if (TActorIterator<AAcWorld> It(World); It) return *It;
	FActorSpawnParameters Params;
	Params.Name = TEXT("AcWorld");
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<AAcWorld>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
}
