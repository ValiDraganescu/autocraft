// `AAcPoseInfantryPreview`: the Prospector or the Ranger played through a
// scripted scene by its pose (AcPoseInfantry.h) and drawn as it stands at a
// given time, framed as the Swift `Autocraft preview` draws it, for
// side-by-side stills against `Sources/Autocraft/PosePreview.swift` (chunk B2).
// Both step the unit from time 0 at 60 Hz with the same inputs:
//
//   prospector-walk, prospector-carry, prospector-hydrogen  walking home
//   prospector-mine   drilling; the run ends at 3 s (the fork's pickup in
//                     the last 1.3 s); a cutter strike at 0.8 s
//   ranger-walk       walking
//   ranger-shoot      rifle and shield, attacking a point ahead-right, a
//                     shot every 0.8 s from 0.2 s
//   ranger-minigun    Mini gun and shield, a shot every 0.1 s from 0.2 s
//
// Run it in the model level, with -AcShot:
//   UnrealEditor Autocraft.uproject /Game/Maps/ModelRow -game -RenderOffscreen
//     -ResX=1600 -ResY=900 -windowed -ForceRes -unattended -nosplash -nosound
//     -AcNoSave -AcNoAI -AcPaused -AcPoseInfantry=ranger-shoot -AcPoseAt=0.21
//     -AcShot=/abs/out.png
// then crop the centre square (SceneKit's 30° FOV is vertical). Swift:
//   AUTOCRAFT_PREVIEW_POSE=ranger-shoot AUTOCRAFT_PREVIEW_AT=0.21 \
//     .build/release/Autocraft preview ranger out.png
// `-AcPoseTeam=N` draws it in player N's colour; `-AcPoseFloor` keeps the
// level's floor. It logs `pose infantry:` with the lowest sole's height
// (0 when the sink keeps it on the ground) and how far the composed rest
// of the torso-folded parts is from the export's.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"

#include "AcPoseInfantryPreview.generated.h"

class ACameraActor;
class UStaticMeshComponent;

UCLASS()
class AUTOCRAFT_API AAcPoseInfantryPreview : public AActor
{
	GENERATED_BODY()

public:
	AAcPoseInfantryPreview();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void Build(const FString& Scene, double At);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Meshes;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> Camera;

	bool bHolding = false;
	int32 SettleFrames = 0;
};

/// Spawns an `AAcPoseInfantryPreview` for `-AcPoseInfantry=SCENE`.
UCLASS()
class AUTOCRAFT_API UAcPoseInfantryPreviewSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& World) override;
};
