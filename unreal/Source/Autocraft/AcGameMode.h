// `AAcGameMode`: the game's mode (GAME-LAYER.md §3.2), the global default
// (`Config/DefaultEngine.ini`). It reads which kind of run this is from the
// command line and spawns the world's actors when play starts.
//
// Later chunks add their spawns in `StartPlay` (terrain, renderer,
// daylight) or listen to `UAcSimSubsystem::OnGameStarted`, and set the
// pawn and controller classes in the constructor (A3, D4, E2).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "AcGameMode.generated.h"

/// What kind of run this is.
enum class EAcRunMode : uint8
{
	/// The window game (a saved session or a new one on -AcMap).
	Game,
	/// -AcRunFor=SECONDS: step the sim headless, log, quit.
	RunFor,
	/// -AcShot=PATH: render some frames, save a PNG, quit.
	Shot,
	/// -AcPlayground (F5).
	Playground,
	/// -AcBench=... (F3).
	Bench,
};

UCLASS()
class AUTOCRAFT_API AAcGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AAcGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;

	/// From the command line (`-AcRunFor`, `-AcShot`, `-AcPlayground`, `-AcBench`).
	static EAcRunMode ReadRunMode();
	EAcRunMode GetRunMode() const { return RunMode; }

private:
	EAcRunMode RunMode = EAcRunMode::Game;
};
