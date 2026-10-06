// `AAcShatterSheet`: one C5 kind's death (AcEffectsShatter.h) at several
// moments in a row, for stills beside the Swift game's own
// `blowApart`/`shootDown`/`collapse` (chunk C5). The layout, camera and
// light are C4's `AAcDeathSheet` (AcEffectsDeathsSheet.h): copy k at sim
// (x_k, 0), x_k = (k − (N−1)/2)·gap, heading π/2 + 0.6, flat ground at 0;
// the camera (SceneKit space) at (0, cy + 0.55·D, D) looking at (0, cy, 0).
//
// Spec (JSON): { "sheets": [ { "kind": "longbow" | "citadel" | …, "gap": 4,
//   "cy": 0.5, "D": 40, "fov": 8, "width": 2800, "height": 560,
//   "frames": [ { "label": "0.5 s", "sec": 0.5, "id": 7 }, … ] } ] }
// Each copy stands at its rest pose (its kind's registered pose at game
// time 10), dies, and is drawn `sec` seconds later through the game's own
// cut (`FAcShatterLibrary`) and flights (`AcShatter::Launch`/`Fell`/
// `ShootDownPose`) on the game's materials. No particles, explosions or
// marks. `id` seeds the throw.
//
//   UnrealEditor Autocraft.uproject /Game/Maps/ModelRow -game -RenderOffscreen
//     -ResX=2800 -ResY=560 -windowed -ForceRes -unattended -nosplash -nosound
//     -AcNoSave -AcNoAI -AcPaused -AcShatterSheet=longbow -AcShatterSheetSpec=/abs/c5.json
//     -AcShot=/abs/out.png
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"

#include "AcEffectsShatterSheet.generated.h"

class ACameraActor;
class UStaticMeshComponent;
class FAcShatterLibrary;

UCLASS()
class AUTOCRAFT_API AAcShatterSheet : public AActor
{
	GENERATED_BODY()

public:
	AAcShatterSheet();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	void Build(const FString& Kind, const FString& SpecFile);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Meshes;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> Camera;

	TSharedPtr<FAcShatterLibrary> Library;
	double VerticalFov = 30.0;
	double Aspect = 16.0 / 9.0;
	bool bHolding = false;
	int32 SettleFrames = 0;
};

/// Spawns an `AAcShatterSheet` when the command line asks for one.
UCLASS()
class AUTOCRAFT_API UAcShatterSheetSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& World) override;
};
