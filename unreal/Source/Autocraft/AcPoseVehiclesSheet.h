// `AAcPoseSheet`: one unit kind posed by its registered pose function at
// several moments, the copies in a row, for side-by-side stills against
// the Swift game's own pose code (chunk B3; any pose chunk can use it).
//
// The sheet spec is a JSON file (B3's: scratchpad `ue/b3/sheets.json`):
//   { "sheets": [ { "kind": "comet", "gap": 1.5, "cy": 1.0, "D": 30,
//       "fov": 6.2, "width": 2400, "height": 640,
//       "frames": [ { "time": 10, "stride": 0.3, "walking": true,
//                     "aiming": false, "since": null, "jump": 0.3,
//                     "twist": 0, "moving": false, "turn": 0, "aim": 0,
//                     "flameLength": 3, "speed": 0, "warm": 0 }, … ] } ] }
// Copy k stands at sim (x_k, 0), x_k = (k − (N−1)/2)·gap, heading
// π/2 + 0.6 (the Swift preview's turn), on flat ground; the camera
// (SceneKit space) at (0, cy + 0.55·D, D) looks at (0, cy, 0) with a
// vertical FOV `fov`; the sun from (-4, 8, 6) as the Swift preview's.
// A frame's inputs become sim state and renderer memory the way the game
// would have them: `walking` → `moving`; `aiming` → task attacking;
// `since` → `lastShot`; a Comet's `jump` → `jumpFrom`/`jumpTo` around it;
// a Juggernaut's `twist` → where it last shot (`lastAim`); a Firefly's
// `aim` → `Unit.aim`, `turn` → the smoothed turn rate (×4), `flameLength`
// → the memory's, `warm` poses first while it drives at `speed` cells a
// frame (the wheel roll and smear carry over).
//
// Run it in the model level with -AcShot (offscreen, no sound):
//   UnrealEditor Autocraft.uproject /Game/Maps/ModelRow -game -RenderOffscreen
//     -ResX=2400 -ResY=640 -windowed -ForceRes -unattended -nosplash -nosound
//     -AcNoSave -AcNoAI -AcPaused -AcPoseSheet=comet -AcPoseSheetSpec=/abs/sheets.json
//     -AcShot=/abs/out.png
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"

#include "AcPoseVehiclesSheet.generated.h"

class ACameraActor;
class UStaticMeshComponent;

UCLASS()
class AUTOCRAFT_API AAcPoseSheet : public AActor
{
	GENERATED_BODY()

public:
	AAcPoseSheet();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void Build(const FString& Kind, const FString& SpecFile);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Meshes;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> Camera;

	double VerticalFov = 30.0;
	double Aspect = 16.0 / 9.0;
	bool bHolding = false;
	int32 SettleFrames = 0;
};

/// Spawns an `AAcPoseSheet` when the command line asks for one.
UCLASS()
class AUTOCRAFT_API UAcPoseSheetSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& World) override;
};
