// `AAcPoseStill`: one unit posed by its registered pose function (AcPose.h)
// for given inputs and framed the way the Swift `Autocraft preview` command
// draws it, for side-by-side stills of an animation (chunk B4; any pose
// chunk can use it). The Swift preview calls `GameScene.pose` with chosen
// inputs (`AUTOCRAFT_PREVIEW_ANCHOR`, `_STRIDE`, the "-busy" variants) and
// renders the model alone: front three-quarter (the model turned
// -(π/2 + 0.6) about up), 30° vertical FOV, camera at
// (0, target.y + 0.45 d, d) looking at the target, sun from (-4, 8, 6).
// This does the same with the catalog's meshes as plain components (the
// same transforms and emission the renderer would send as instances).
//
// Run it in the model level (its sky, sun and exposure), with -AcShot:
//   UnrealEditor Autocraft.uproject /Game/Maps/ModelRow -game -RenderOffscreen
//     -ResX=1067 -ResY=600 -windowed -ForceRes -unattended -nosplash -nosound
//     -AcNoSave -AcNoAI -AcPaused -AcPoseStill=longbow -AcPoseAnchor=0.5
//     -AcShot=/abs/out.png
// then crop the centre square (the FOV is vertical, as SceneKit's).
//
//   -AcPoseStill=KIND[-busy]  the unit kind's model (`longbow`, `hailstorm`,
//                       any `AcPose::ModelBase` name); `-busy` as the Swift
//                       preview's (hailstorm: mount swung 0.6 rad, tracking,
//                       a pair just fired, posed 12 frames).
//   -AcPoseAnchor=F     `Unit.anchor` (Longbow, 0…1).
//   -AcPoseStride=F     distance walked; also sets it moving.
//   -AcPoseTime=F       game time (default 10, the Swift longbow preview's).
//   -AcPoseShotAgo=F    it fired F seconds ago (`lastShot`).
//   -AcPoseAim=F        turret/look heading, radians (default 0).
//   -AcPoseFrames=N     pose N times at 1/60 s steps (eased parts).
//   -AcPoseDistance=F   camera distance, cells (default: the preview's).
//   -AcPoseTeam=N       player colour.
// It logs `pose still:` with each Longbow foot's position (the shin's end),
// as the Swift preview prints them with AUTOCRAFT_PREVIEW_STRIDE.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"

#include "AcPoseStill.generated.h"

class ACameraActor;
class UStaticMeshComponent;

UCLASS()
class AUTOCRAFT_API AAcPoseStill : public AActor
{
	GENERATED_BODY()

public:
	AAcPoseStill();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void Build(const FString& Spec);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Meshes;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> Camera;

	bool bHolding = false;
	int32 SettleFrames = 0;
};

/// Spawns an `AAcPoseStill` when the command line asks for one.
UCLASS()
class AUTOCRAFT_API UAcPoseStillSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& World) override;
};
