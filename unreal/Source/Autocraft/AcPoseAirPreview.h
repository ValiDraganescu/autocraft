// A close-up of a posed Dropship or Kestrel, framed and lit like the Swift
// `autocraft preview <model>[-busy]` (Snapshot.swift `ModelPreview`,
// ModelCatalog.swift's "dropship"/"kestrel" poses), to check the B5 poses
// (AcPoseAir.h) still by still against Swift.
//
// Run in the ModelRow level (it has the sun, sky and exposure; the row is
// hidden):
//   UnrealEditor Autocraft.uproject /Game/Maps/ModelRow -game -RenderOffscreen
//     -nosound -ResX=1600 -ResY=900 -AcShot=OUT.png
//     -AcAirPreview=dropship|dropship-busy|kestrel|kestrel-busy
// Crop the centre 900×900 to lay it beside the Swift 900×900 preview
// (the camera keeps SceneKit's 30° vertical FOV).
// Overrides (otherwise Swift's preview values): -AcAirTime=T (game time),
// -AcAirMoving=0|1, -AcAirBank=B, -AcAirOpen=F (ramp), -AcAirHeal=0|1,
// -AcAirShot=T (a Kestrel's last shot), -AcAirLift=AGE (seconds out of the
// Spacedock's pad), -AcAirTeam=N, -AcAirYaw=R (turn further, as
// AUTOCRAFT_PREVIEW_YAW), -AcAirDistance=F (camera F times as far).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"

#include "AcPose.h"

#include "AcPoseAirPreview.generated.h"

class ACameraActor;
class USceneComponent;
class UStaticMeshComponent;
struct FAcModelInfo;

UCLASS(NotPlaceable, Transient)
class AUTOCRAFT_API AAcPoseAirPreview : public AActor
{
	GENERATED_BODY()

public:
	AAcPoseAirPreview();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void Build(const FAcModelInfo& Model, int32 Team);
	void Apply();

	FString What;
	const FAcModelInfo* Model = nullptr;
	FAcPose Pose;
	TArray<USceneComponent*> Parts;
	TArray<TArray<UStaticMeshComponent*>> PartMeshes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Built;
	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> Camera;

	bool bHolding = false;
	int32 SettleFrames = 0;
};

/// Spawns the preview actor in a game world when `-AcAirPreview=` is given.
UCLASS()
class AUTOCRAFT_API UAcPoseAirPreviewSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& World) override;
};
