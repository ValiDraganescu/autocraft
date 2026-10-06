#include "AcFog.h"

#include "AcLog.h"
#include "AcSimSubsystem.h"

#include "Components/PostProcessComponent.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RenderingThread.h"

namespace
{
	TAutoConsoleVariable<int32> CVarFog(TEXT("ac.Fog"), 1,
		TEXT("The fog of war on screen (the top-down pass and the minimap's fog): 1 on, 0 off. The game is unchanged: "
			 "the others' units out of sight stay hidden (Swift AUTOCRAFT_FOG=off)."));

	const TCHAR* const FogMaterialPath = TEXT("/Game/Fog/M_Fog.M_Fog");

	/// Off from the start: `AUTOCRAFT_FOG=off` or `-AcNoFog`.
	bool StartsOff()
	{
		return FPlatformMisc::GetEnvironmentVariable(TEXT("AUTOCRAFT_FOG")) == TEXT("off")
			|| FParse::Param(FCommandLine::Get(), TEXT("AcNoFog"));
	}

	UTexture2D* MakeTexture(const int32 W, const int32 H, const TCHAR* Name)
	{
		UTexture2D* T = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8, FName(Name));
		if (!T) return nullptr;
		T->SRGB = false;
		T->Filter = TF_Bilinear;
		T->AddressX = TA_Clamp;
		T->AddressY = TA_Clamp;
		T->CompressionSettings = TC_VectorDisplacementmap;
		T->NeverStream = true;
		T->UpdateResource();
		return T;
	}

	/// Send a whole BGRA8 picture to `T` (copied: the render thread frees it).
	void Upload(UTexture2D* T, const TArray<uint8>& Bytes, const int32 W, const int32 H)
	{
		if (!T || Bytes.Num() != W * H * 4) return;
		uint8* Copy = static_cast<uint8*>(FMemory::Malloc(Bytes.Num()));
		FMemory::Memcpy(Copy, Bytes.GetData(), Bytes.Num());
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, W, H);
		T->UpdateTextureRegions(0, 1, Region, W * 4, 4, Copy,
			[](uint8* Data, const FUpdateTextureRegion2D* Regions) {
				FMemory::Free(Data);
				delete Regions;
			});
	}
}

AAcFog::AAcFog()
{
	PrimaryActorTick.bCanEverTick = false;
	Post = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Fog"));
	Post->bUnbound = true;
	Post->Priority = 0.0f;
	Post->bEnabled = false;
	RootComponent = Post;
}

AAcFog* AAcFog::Find(const UWorld* World)
{
	if (!World) return nullptr;
	TActorIterator<AAcFog> It(const_cast<UWorld*>(World));
	return It ? *It : nullptr;
}

AAcFog* AAcFog::SpawnFor(UWorld* World)
{
	if (!World) return nullptr;
	AAcFog* F = Find(World);
	if (!F)
	{
		FActorSpawnParameters Params;
		Params.Name = TEXT("AcFog");
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		F = World->SpawnActor<AAcFog>(AAcFog::StaticClass(), FTransform::Identity, Params);
		if (StartsOff()) CVarFog->Set(0, ECVF_SetByCommandline);
	}
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(World))
	{
		if (!F->GameStartedHandle.IsValid()) F->GameStartedHandle = Sim->OnGameStarted.AddUObject(F, &AAcFog::OnGameStarted);
		if (!F->FrameHandle.IsValid())
		{
			F->FrameHandle = Sim->AddFrameListener(EAcFrameStage::Renderer, FAcFrameEvent::FDelegate::CreateUObject(F, &AAcFog::OnFrame));
		}
		if (Sim->IsRunning()) F->OnGameStarted(*Sim);
	}
	return F;
}

void AAcFog::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this))
	{
		if (GameStartedHandle.IsValid()) Sim->OnGameStarted.Remove(GameStartedHandle);
		if (FrameHandle.IsValid()) Sim->RemoveFrameListener(EAcFrameStage::Renderer, FrameHandle);
	}
	GameStartedHandle.Reset();
	FrameHandle.Reset();
	Super::EndPlay(Reason);
}

bool AAcFog::IsOn() const
{
	return HasGrid() && CVarFog.GetValueOnGameThread() != 0;
}

ac::Vec2 AAcFog::GridSize() const
{
	return ac::Vec2(double(Width) * ac::Vision::cell, double(Height) * ac::Vision::cell);
}

float AAcFog::LevelAt(const ac::Vec2 P) const
{
	if (!IsOn()) return InSight;
	const int64 X = (int64)FMath::FloorToDouble((P.x - Origin.x) / ac::Vision::cell);
	const int64 Z = (int64)FMath::FloorToDouble((P.y - Origin.y) / ac::Vision::cell);
	if (X < 0 || Z < 0 || X >= Width || Z >= Height) return InSight;
	return Level[Z * Width + X];
}

const std::set<int64_t>& AAcFog::Unseen()
{
	if (UnseenFrame != FrameCount)
	{
		UnseenFrame = FrameCount;
		UnseenCache.clear();
		if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this); Sim && Sim->IsRunning())
		{
			UnseenCache = Sim->Simulation().unseen(Sim->LocalPlayer());
		}
	}
	return UnseenCache;
}

void AAcFog::OnGameStarted(UAcSimSubsystem& Sim)
{
	Rebuild(Sim);
	bSnapNext = true;
	Step(Sim, 0.0);
}

void AAcFog::Rebuild(UAcSimSubsystem& Sim)
{
	const auto& V = Sim.Simulation().vision;
	const int32 W = V ? int32(V->width) : 0, H = V ? int32(V->height) : 0;
	if (V) Origin = V->origin;
	if (W == Width && H == Height && (Vision || W == 0)) return;
	Width = W;
	Height = H;
	Level.Init(Unexplored, W * H);
	VisionBytes.Init(0, W * H * 4);
	MinimapBytes.Init(0, W * H * 4);
	Vision = nullptr;
	Minimap = nullptr;
	if (W == 0)
	{
		UE_LOG(LogAutocraft, Log, TEXT("fog: none on this map"));
		ApplyPass();
		return;
	}
	Vision = MakeTexture(W, H, TEXT("AcFogVision"));
	Minimap = MakeTexture(W, H, TEXT("AcFogMinimap"));
	if (!Material)
	{
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, FogMaterialPath))
		{
			Material = UMaterialInstanceDynamic::Create(Base, this, TEXT("FogMaterial"));
			Post->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, Material));
		}
		else
		{
			UE_LOG(LogAutocraft, Warning, TEXT("fog: no %s (run Tools/Editor/make_fog_material.py)"), FogMaterialPath);
		}
	}
	if (Material)
	{
		Material->SetTextureParameterValue(TEXT("Fog"), Vision);
		const ac::Vec2 S = GridSize();
		Material->SetVectorParameterValue(TEXT("Rect"), FLinearColor(float(Origin.x), float(Origin.y), float(1.0 / S.x), float(1.0 / S.y)));
		Material->SetVectorParameterValue(TEXT("Texel"), FLinearColor(1.0f / float(W), 1.0f / float(H), 0.0f, 0.0f));
	}
	UE_LOG(LogAutocraft, Log, TEXT("fog: %dx%d half cells from (%.1f, %.1f)"), W, H, Origin.x, Origin.y);
	ApplyPass();
}

void AAcFog::ApplyPass()
{
	const bool bOn = IsOn();
	Post->bEnabled = bOn && !bSuppressed && Material != nullptr;
	if (bOn != bWasOn)
	{
		bWasOn = bOn;
		bSnapNext = true;
		UE_LOG(LogAutocraft, Log, TEXT("fog: %s"), bOn ? TEXT("on") : TEXT("off"));
	}
}

void AAcFog::OnFrame(const FAcFrame& Frame)
{
	++FrameCount;
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || !Sim->IsRunning()) return;
	// The playground can build its sim again with or without fog.
	const auto& V = Sim->Simulation().vision;
	if ((V ? int32(V->width) : 0) != Width || (V ? int32(V->height) : 0) != Height) Rebuild(*Sim);
	ApplyPass();
	Step(*Sim, Frame.RealDelta);
}

void AAcFog::Step(UAcSimSubsystem& Sim, const double Dt)
{
	if (!HasGrid()) return;
	const ac::Simulation& S = Sim.Simulation();
	const int64 P = Sim.LocalPlayer();
	const int32 N = Width * Height;
	const bool bOn = IsOn();
	const ac::Sight* Sight = P >= 0 && P < (int64)S.sights.size() ? &S.sights[(size_t)P] : nullptr;
	const ac::Intel* Intel = S.state.intel && P >= 0 && P < (int64)S.state.intel->size() ? &(*S.state.intel)[(size_t)P] : nullptr;
	if (Sight && (int32)Sight->seen.size() != N) Sight = nullptr;
	const float K = bSnapNext ? 1.0f : FMath::Min(1.0f, Rate * float(Dt));
	bSnapNext = false;
	const uint64_t* Bits = Intel && (int64)Intel->explored.size() * 64 >= N ? Intel->explored.data() : nullptr;
	float* L = Level.GetData();
	uint8* Out = VisionBytes.GetData();
	for (int32 I = 0; I < N; ++I)
	{
		// `seen.map { $0[i] } ?? true`: no sight yet is all in sight.
		const bool bSeen = !bOn || !Sight || Sight->seen[(size_t)I] != 0;
		const float Want = bSeen ? InSight : (Bits && (Bits[I >> 6] >> (I & 63) & 1) ? Explored : Unexplored);
		float Value = L[I];
		Value += (Want - Value) * K;
		L[I] = Value;
		const uint8 B = uint8(FMath::Clamp(Value * 255.0f + 0.5f, 0.0f, 255.0f));
		uint8* Px = Out + I * 4;
		Px[0] = B;
		Px[1] = B;
		Px[2] = B;
		Px[3] = B;
	}
	UploadVision();
	SinceMinimap += Dt;
	if (K >= 1.0f || SinceMinimap >= MinimapEvery)
	{
		SinceMinimap = 0.0;
		UploadMinimap();
	}
}

void AAcFog::UploadVision()
{
	Upload(Vision, VisionBytes, Width, Height);
}

void AAcFog::UploadMinimap()
{
	// `FogOfWar.image()`: black, alpha (1 − level)·1.15. Straight alpha.
	const int32 N = Width * Height;
	for (int32 I = 0; I < N; ++I)
	{
		uint8* Px = MinimapBytes.GetData() + I * 4;
		Px[0] = 0;
		Px[1] = 0;
		Px[2] = 0;
		Px[3] = uint8(FMath::Clamp((1.0f - Level[I]) * 1.15f * 255.0f, 0.0f, 255.0f));
	}
	Upload(Minimap, MinimapBytes, Width, Height);
	++MinimapRev;
}
