// `UAcMinimapSubsystem` (chunk D7, GAME-LAYER.md §2.5): the minimap's game
// side, as the Swift `GameController` drives `Minimap` (GameController.swift
// `minimapImage` :120, `makeMinimap` :575, the fog every 0.25 s :461,
// `moveFreeCamera` :754, `jumpFromMinimap` :775).
//
// - At each game start it bakes the terrain picture over the map's bounds at
//   4 pixels a cell (`AcMinimapBake::Terrain`, Badlands · Large ~0.1 s) and
//   hands it to every minimap it made.
// - It makes the console's minimap (`makeMinimap(scale: 1, height: 180)`)
//   and mounts it with `SAcConsole::SetMinimap` (D2), again whenever the
//   console widget is remade.
// - Every frame at the `Hud` stage: the marks from `Shown()` (the game as
//   the local team sees it), the RTS camera's ground quad (`FreeView::
//   footprint`; none while the RTS pawn is not the one in use, i.e. while
//   driving), and B10's minimap fog (`AAcFog::MinimapTexture`, none with
//   `ac.Fog 0` or on a map without fog).
// - A press or drag on the minimap centres the RTS camera on that ground
//   (`AAcRtsPawn::CenterOn`).
//
// For other chunks:
//   - D8 (command map): `MakeMinimap(Size, 1.8, false)` gives another
//     minimap fed the same way (no camera outline); `Terrain()` is the
//     baked picture, `Bounds()` the ground it covers.
//   - E (driving): `SetSight({{At, Heading}})` shows the sight wedge on
//     every minimap, `SetSight({})` hides it.
//   - F1 (new game preview): `AcMinimapBake::Terrain` on any map.
//   - D3 (warped faces): the console's minimap is an ordinary child widget;
//     the warp must route mouse input to it through the face's inverse.
//
// Command line and console (shots and tests): `-AcMinimapClick="FX,FY"` /
// `ac.MinimapClick FX FY` presses the console's minimap at that fraction of
// its width and height (0,0 top left = the far west corner) through the
// same path as the mouse; `-AcMinimapSight="X,Y,HEADING"` shows the wedge.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/StrongObjectPtr.h"

#include "Projection.h"
#include "SimdMath.h"

#include "AcMinimap.generated.h"

class SAcConsole;
class SAcMinimap;
class UAcSimSubsystem;
class UTexture2D;
struct FAcFrame;

UCLASS()
class AUTOCRAFT_API UAcMinimapSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/// Pixels a cell in the terrain picture (`pixelsPerCell: 4`).
	static constexpr double PixelsPerCell = 4.0;

	static UAcMinimapSubsystem* Get(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/// The console's minimap (null before the first game starts).
	TSharedPtr<SAcMinimap> Minimap() const { return Main; }
	/// Another minimap over the same map, fed every frame like the console's.
	TSharedRef<SAcMinimap> MakeMinimap(FVector2D Size, double Scale, bool bShowFootprint);
	/// The terrain picture and the ground it covers.
	UTexture2D* Terrain() const { return TerrainTexture.Get(); }
	const ac::GroundRect& Bounds() const { return MapBounds; }

	/// Driving: the sight wedge on every minimap (unset: hidden).
	void SetSight(TOptional<TPair<ac::Vec2, double>> Sight);

	/// Press the console's minimap at a fraction of its size (0..1, top left
	/// first), as a mouse press there would. False when it is not drawn yet.
	bool PressAt(FVector2D Fraction);

private:
	void OnGameStarted(UAcSimSubsystem& Sim);
	void OnFrame(const FAcFrame& Frame);
	void Build(const UAcSimSubsystem& Sim);
	void Mount();
	void JumpTo(ac::Vec2 Ground);

	TSharedPtr<SAcMinimap> Main;
	TArray<TWeakPtr<SAcMinimap>> Others;
	TWeakPtr<SAcConsole> MountedOn;
	TStrongObjectPtr<UTexture2D> TerrainTexture;
	ac::GroundRect MapBounds;
	TOptional<TPair<ac::Vec2, double>> SightNow;
	FDelegateHandle FrameHandle;
	FDelegateHandle StartedHandle;
	TOptional<FVector2D> PendingPress;
	int32 PressWait = 0;
};
