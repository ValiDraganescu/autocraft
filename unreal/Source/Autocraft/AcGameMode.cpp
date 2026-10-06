#include "AcGameMode.h"

#include "AcDaylight.h"
#include "AcDoodads.h"
#include "AcEffects.h"
#include "AcFog.h"
#include "AcResources.h"
#include "AcHUD.h"
#include "AcLifeBars.h"
#include "AcLog.h"
#include "AcPilotPawn.h"
#include "AcPlayerController.h"
#include "AcRtsPawn.h"
#include "AcSimSubsystem.h"
#include "AcTerrain.h"
#include "AcWorldRenderer.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

AAcGameMode::AAcGameMode()
{
	// The RTS camera (A3); the pilot pawn (E2) is possessed while driving.
	DefaultPawnClass = AAcRtsPawn::StaticClass();
	// The HUD (D1): Slate over the viewport.
	HUDClass = AAcHUD::StaticClass();
	// The pointer (D4): hover, cursors, clicks, selection; driving (E2).
	PlayerControllerClass = AAcPlayerController::StaticClass();
	bStartPlayersAsSpectators = false;
}

EAcRunMode AAcGameMode::ReadRunMode()
{
	const TCHAR* Cmd = FCommandLine::Get();
	FString Value;
	if (FParse::Value(Cmd, TEXT("AcRunFor="), Value)) return EAcRunMode::RunFor;
	if (FParse::Value(Cmd, TEXT("AcBench="), Value)) return EAcRunMode::Bench;
	if (FParse::Param(Cmd, TEXT("AcPlayground"))) return EAcRunMode::Playground;
	if (FParse::Value(Cmd, TEXT("AcShot="), Value)) return EAcRunMode::Shot;
	return EAcRunMode::Game;
}

void AAcGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	RunMode = ReadRunMode();
	static const TCHAR* const Names[] = {TEXT("game"), TEXT("runfor"), TEXT("shot"), TEXT("playground"), TEXT("bench")};
	UE_LOG(LogAutocraft, Log, TEXT("game mode: %s on %s"), Names[(int32)RunMode], *MapName);
}

void AAcGameMode::StartPlay()
{
	Super::StartPlay();
	// Spawns of the world's actors go here (terrain A2, renderer B1,
	// daylight B9), after the subsystem loaded the session in
	// OnWorldBeginPlay.
	if (const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this); !Sim || !Sim->IsRunning())
	{
		UE_LOG(LogAutocraft, Error, TEXT("no simulation running at StartPlay"));
	}
	// The ground (A2): built from the sim's map, rebuilt on a new map.
	AAcTerrain::SpawnFor(GetWorld());
	// Day and night (B9): sun, moon, sky, fog, the clock.
	AAcDaylight::SpawnFor(GetWorld());
	// Units and buildings as instances (B1).
	AAcWorld::SpawnFor(GetWorld());
	// Tracers, flashes, rounds, slugs (C1), after the renderer: muzzles come from its poses.
	AAcEffectsActor::SpawnFor(GetWorld());
	// Life bars (D6): after the renderer, whose frame listener must run first.
	AAcLifeBars::SpawnFor(GetWorld());
	// Ore, wells, doodads and border rocks (B8).
	AAcResources::SpawnFor(GetWorld());
	AAcDoodads::SpawnFor(GetWorld());
	// The fog of war on screen (B10): the vision texture and the top-down pass.
	AAcFog::SpawnFor(GetWorld());
	// Driving a unit (E2): the pilot pawn waits for a take over.
	AAcPilotPawn::SpawnFor(GetWorld());
}
