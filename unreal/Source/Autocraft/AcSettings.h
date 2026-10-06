// `UAcSettings` (chunk F1, GAME-LAYER.md §2.14): the game's settings, kept
// in `GameUserSettings.ini` (`[/Script/Autocraft.AcSettings]`), the Unreal
// side of Swift's UserDefaults (`windowMap`, `showPerf`, the view) and of the
// new-game choices. It is the engine's game user settings class
// (`DefaultEngine.ini`: `GameUserSettingsClassName`), so the window size,
// fullscreen and frame-rate cap are the base class's.
//
// What reads what:
//   LastMap, LastTeams      F1: the map the game opens on (Swift `windowMap`)
//                           and the new-game dialog's first choice.
//   bNewGameAI, bNewGameFog F1: the dialog's toggles for a new game.
//   bShowPerf               F1 applies it to `ac.PerfPanel` at start and
//                           saves the cvar back at quit (Swift `showPerf`).
//   bThirdPerson            for E3 (Swift's remembered 1st/3rd view).
//   MasterVolume, bMusic, MusicVolume  for C7/C8 (Swift `--volume`, `--music`).
//   SfxVolume, AmbientVolume  the menu's Settings page: the effects (shots,
//                           tools, voices) and the world around (wind, hum).
//   LookSpeed               the menu's Settings page: the driven unit's mouse turn.
//
// Agents' headless runs (`-unattended`) neither read nor write `LastMap`
// (so one test cannot change the map of the next), unless `-AcRemember`.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"

#include "AcSettings.generated.h"

UCLASS(config = GameUserSettings, configdonotcheckdefaults)
class AUTOCRAFT_API UAcSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	/// The settings (the engine's game user settings object when it is this
	/// class, else one of our own, loaded once).
	static UAcSettings& Get();
	/// Write them to `GameUserSettings.ini`.
	static void Store();
	/// False in agents' headless runs (see the header comment).
	static bool Remembers();

	/// The last map played, as `-AcMap` takes it (`badlands-large-8`); empty: none yet.
	UPROPERTY(config)
	FString LastMap;
	/// Its teams (`4v4`, `2v2v2v2`, `ffa`).
	UPROPERTY(config)
	FString LastTeams;
	UPROPERTY(config)
	bool bNewGameAI = true;
	UPROPERTY(config)
	bool bNewGameFog = true;
	UPROPERTY(config)
	bool bShowPerf = true;
	UPROPERTY(config)
	bool bThirdPerson = false;
	UPROPERTY(config)
	float MasterVolume = 1.0f;
	UPROPERTY(config)
	bool bMusic = true;
	UPROPERTY(config)
	float MusicVolume = 1.0f;
	UPROPERTY(config)
	float SfxVolume = 1.0f;
	UPROPERTY(config)
	float AmbientVolume = 1.0f;
	/// Times the base mouse turn while driving (0.25-3).
	UPROPERTY(config)
	float LookSpeed = 1.0f;
};
