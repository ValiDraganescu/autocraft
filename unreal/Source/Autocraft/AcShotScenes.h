// F2: named reference scenes, the Unreal side of the Swift game's
// `windowshot` scenes (top-down, `--select`, `--pilot ...`, `--orders`...).
//
//   -AcScene=NAME   expands to the scene's own flags from
//                   `Tools/shots/scenes.json` (the same table the compare
//                   script `Tools/shots/compare.py` reads for both games),
//                   appended to the command line before the map loads, so
//                   every chunk's flag (-AcPilot, -AcSelect, -AcOrders...)
//                   reads it as if typed. Flags typed on the command line
//                   come first and win where a chunk reads the first match.
//                   `-AcScene=list` logs every scene. The window size is not
//                   a scene flag (the engine reads -ResX/-ResY before this
//                   runs): pass `-windowed -ForceRes -ResX=W -ResY=H`.
//   -AcWarm=S       before the first frame, step the game S seconds at 1/30
//                   as `GameController.warm(seconds:)` does (windowshot's
//                   `--warm`, 10 by default there). With -AcPaused the game
//                   then holds still for the picture, as Swift's still does.
//
// Typical: UnrealEditor Autocraft.uproject /Game/Maps/Battlefield -game
//   -RenderOffscreen -windowed -ForceRes -ResX=1600 -ResY=1000 -unattended
//   -nosplash -nosound -AcScene=pilot-ranger -AcShot=/abs/out.png
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/WorldSubsystem.h"

#include "AcShotScenes.generated.h"

/// One scene of `Tools/shots/scenes.json`, as far as Unreal needs it.
struct AUTOCRAFT_API FAcShotScene
{
	FString Name;
	/// The flags it adds (defaults + map, zoom, at, warm, still + its own).
	TArray<FString> Flags;
	/// False: the scene has no Unreal staging yet (`"ue": null`).
	bool bStaged = true;
	int32 Width = 1600;
	int32 Height = 1000;
};

namespace AcShotScenes
{
	/// `<project>/Tools/shots/scenes.json`.
	AUTOCRAFT_API FString TablePath();
	/// Every scene of the table (empty, logged, if it cannot be read).
	AUTOCRAFT_API TArray<FAcShotScene> Load(FString* Error = nullptr);
}

/// Expands -AcScene=NAME into the command line (runs in
/// `UGameInstance::Init`, before the first world exists).
UCLASS()
class AUTOCRAFT_API UAcShotScenes : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
};

/// -AcWarm=S: steps the game before the first frame.
UCLASS()
class AUTOCRAFT_API UAcShotWarm : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	/// Step the game -AcWarm seconds (once). With -AcPlaygroundLayout the
	/// playground calls it after laying out (`windowshot`: lay, then warm).
	void Warm();

private:
	FDelegateHandle StartedHandle;
	bool bWarmed = false;
};
