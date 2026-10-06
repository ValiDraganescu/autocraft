// `AAcDeathSheet`: one infantry kind's fall (AcEffectsDeaths.h) at several
// moments, the bodies in a row, for stills beside the Swift game's own
// `GameScene.fall` (chunk C4). The layout, camera and light are
// `AAcPoseSheet`'s (AcPoseVehiclesSheet.h): copy k at sim (x_k, 0),
// x_k = (k − (N−1)/2)·gap, heading π/2 + 0.6, flat ground; the camera
// (SceneKit space) at (0, cy + 0.55·D, D) looking at (0, cy, 0), vertical
// FOV `fov`.
//
// Spec (JSON): { "sheets": [ { "kind": "ranger", "gap": 1.6, "cy": 0.5,
//   "D": 30, "fov": 5, "width": 2400, "height": 600,
//   "frames": [ { "label": "0.2 s", "sec": 0.2, "id": 7 }, … ] } ] }
// Each copy stands idle at game time 10 (its kind's registered pose, as
// the renderer last drew it), then dies: `AcDeaths::Start` and
// `AcDeaths::Pose` `sec` seconds later. Unit `id` picks the side it falls
// to (odd: −1). Bodies are drawn as in the game (the fading materials,
// custom data 1 and 2); no decals or blasts.
//
//   UnrealEditor Autocraft.uproject /Game/Maps/ModelRow -game -RenderOffscreen
//     -ResX=2400 -ResY=600 -windowed -ForceRes -unattended -nosplash -nosound
//     -AcNoSave -AcNoAI -AcPaused -AcDeathSheet=ranger -AcDeathSheetSpec=/abs/deaths.json
//     -AcShot=/abs/out.png
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"

#include "AcEffectsDeathsSheet.generated.h"

class ACameraActor;
class UStaticMeshComponent;

UCLASS()
class AUTOCRAFT_API AAcDeathSheet : public AActor
{
	GENERATED_BODY()

public:
	AAcDeathSheet();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void Build(const FString& Kind, const FString& SpecFile);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Meshes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> Keep;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> Camera;

	double VerticalFov = 30.0;
	double Aspect = 16.0 / 9.0;
	bool bHolding = false;
	int32 SettleFrames = 0;
};

/// Spawns an `AAcDeathSheet` when the command line asks for one.
UCLASS()
class AUTOCRAFT_API UAcDeathSheetSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& World) override;
};
