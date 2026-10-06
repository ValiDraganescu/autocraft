// `UAcBuildingFx`: what the building poses (AcPoseBuildings.h, chunks B6
// and B7) ask for that is not an instance transform, on the game thread,
// at the `Effects` frame stage:
//
// - Lights: the construction weld's flickering flash (`GameScene.makeBuilding`
//   :370, 0.75/0.85/1, reach 2.5) and the lights in the models (a Citadel's
//   or Garrison's door light, the Foundry's weld light, the Spacedock's and
//   Derrick's bay lights), as a small pool of shadowless point lights.
//   SceneKit intensity `I` (1000 = 1.0, the sun 1900) → candela
//   `I · 2.9/1000 · ac.BuildingLightGain` (the daylight's lux scale, at 1 m).
//   The apron pools and beacon lights at night are C6's (`UAcLampPool`).
// - Particles, drawn on the CPU as camera-facing sprites in one ISM
//   (`MI_AcVapour`, as AAcResources draws the well vapour): weld sparks
//   (`Models.weldSparks`: 0.35 s, 2.4 cells/s, a 70° cone, gravity), vent
//   steam (`Models.steamVents`) and a Derrick's MH vapour
//   (`hydrogenVapour`).
// - A Bastion's crew shots: the slit facing the target (`GameScene.shot`
//   :1214) and its room (`sim.bastionCapacity`), written into the
//   Bastion's renderer memory for its pose next frame.
//
// Dev staging for shots (`-AcStageBase`): player 0 gets one of every
// building near its main, working (training with a queue, a crew in the
// Bastion, a Prospector in the Derrick, the Lab's Garrison training), and a
// second row under construction at half, each with its Prospector welding.
// The log names the rows' centres for `-AcCamAt`.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "AcPose.h"

#include "AcBuildingFx.generated.h"

class AActor;
class UInstancedStaticMeshComponent;
class UPointLightComponent;
class UAcSimSubsystem;
class UAcWorldRenderer;
struct FAcFrame;

UCLASS()
class AUTOCRAFT_API UAcBuildingFx : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	struct FCue
	{
		int64 Id = 0;
		FAcPoseCue Cue;
	};
	enum class EKind : uint8
	{
		Spark,
		Steam,
		Vapour,
		Puff
	};
	struct FParticle
	{
		FVector At = FVector::ZeroVector;  // cm
		FVector Velocity = FVector::ZeroVector;  // cm/s
		FVector Accel = FVector::ZeroVector;  // cm/s²
		float Age = 0.f, Life = 1.f, Size = 1.f;
		FLinearColor Colour = FLinearColor::White;
		EKind Kind = EKind::Spark;
	};

	void OnGameStarted(UAcSimSubsystem& Sim);
	void OnFrame(const FAcFrame& Frame);
	void OnCue(int64 Id, const FAcPoseCue& Cue);
	void Bind();
	void Bastions(const FAcFrame& Frame);
	void Emit(const FCue& C, int32 Index, double Dt);
	void Spawn(EKind Kind, const FVector& At);
	void Lights();
	void Draw(double Dt);
	void Stage(UAcSimSubsystem& Sim);

	TArray<FCue> Cues;
	TMap<uint64, double> Carry;
	TArray<FParticle> Particles;
	TArray<FTransform> Xf;
	TArray<float> Data;

	UPROPERTY(Transient)
	TObjectPtr<AActor> Holder;
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Sprites;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> Pool;
	TWeakObjectPtr<UAcWorldRenderer> Renderer;

	FDelegateHandle FrameHandle, StartedHandle, CueHandle;
	TOptional<double> LastTime;
	FRandomStream Random{0x5eed};
	bool bStaged = false;
};
