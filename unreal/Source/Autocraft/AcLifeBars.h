// `AAcLifeBars`: a thin bar over each hurt or focused unit and building
// (GAME-LAYER.md §2.6 "Life bars", chunk D6), the port of
// `Sources/Autocraft/LifeBars.swift` and `GameScene.showLifeBars`
// (GameScene.swift:751) with `barScale`/`barFocus`/`barUp` from
// `GameController.swift:414`.
//
// One instanced static mesh of camera-facing quads (the engine plane) with
// `M_LifeBar` (Tools/Editor/make_lifebar_material.py): translucent, unlit,
// depth test off, so every bar of the frame is one draw. The frame, the
// fill (green > 0.6 > yellow > 0.3 > red), the track and the segment ticks
// are drawn by the material from per-instance custom data
// {0 fill, 1 segments, 2 aspect}.
//
// Rules (as Swift):
//   - A bar over anything damaged (hp < max - 0.5), and over the focus (what
//     is selected or under the pointer, `SetFocus`) even at full health;
//     none over the dead, what is not drawn (`sim.shown`), units aboard, in
//     a Bastion or a Derrick, the driven unit (`SetHiddenUnit`), nor the
//     sight's target while driving (`SetPilot`: the sight's readout stands
//     in for it). A Ranger's full hp counts the Aegis Shield (+10).
//   - Width: units max(0.8, 2.2 r), buildings 1.7 r (cells); height 0.2 and
//     0.26. Top-down the bar is ×max(1, 0.7 k) wide and ×k tall, with
//     k = barScale = (40 / pointsPerCell)^0.9, so it keeps its thickness
//     on screen as the view zooms out. Driving: half as wide, 0.6 tall,
//     shrinking with the eye's distance inside 4 cells, gone under 1.5.
//   - Placed over the model's top (a unit kind's model height, a building's
//     body height), lifted along the camera's up by 0.5 r + 0.2 k (units)
//     or 0.6 R + 0.3 k (buildings, R its model radius).
//   - Segments: hp per segment is the least of 10/25/50/100/200/500 that
//     cuts the bar into ≤ 10, else 1000.
//
// It runs at the `Renderer` frame stage after `UAcWorldRenderer` (spawned
// after it, so its listener is added later) and places bars from the
// renderer's `PartWorld` (the model root's world transform).
//
// Console: `ac.LifeBars 0|1` (off/on), `ac.LifeBarsStats N` (a `life bars:`
// line every N s), `ac.LifeBarsAll 1` (dev: a bar over
// everything, full or not: the cost and look of a whole army's bars).
// Command line: `-AcLifeBarsAll` (the same, for shots and perf runs).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Rules.h"
#include "Types.h"

#include "AcLifeBars.generated.h"

class UInstancedStaticMeshComponent;
class UAcSimSubsystem;
class AAcTerrain;
struct FAcFrame;
struct FAcModelInfo;

/// The bar rules as plain functions (`LifeBars` statics), for the tests.
namespace AcLifeBars
{
	/// Hit points per segment (`LifeBars.segmentHP`).
	AUTOCRAFT_API double SegmentHP(double MaxHP);
	/// A bar's width at the default zoom, cells (`LifeBars.width`).
	AUTOCRAFT_API double Width(ac::UnitKind Kind);
	AUTOCRAFT_API double Width(ac::StructureKind Kind);
	/// A building's model radius (`GameScene.Building.radius`), cells.
	AUTOCRAFT_API double BuildingRadius(ac::StructureKind Kind);
	/// `barScale` for a top-down view at this zoom (points per cell).
	AUTOCRAFT_API double BarScale(double PointsPerCell);
	/// A unit's full hp as its bar counts it (the Aegis Shield on Rangers).
	AUTOCRAFT_API double FullHP(const ac::GameState& State, const ac::Unit& U);
}

UCLASS()
class AUTOCRAFT_API AAcLifeBars : public AActor
{
	GENERATED_BODY()

public:
	AAcLifeBars();

	static AAcLifeBars* SpawnFor(UWorld* World);
	static AAcLifeBars* Get(const UObject* WorldContext);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/// What shows its bar even at full health: the selected building and
	/// the hovered unit or building (`barFocus`; D4 sets it every frame).
	void SetFocus(const TSet<int64>& Ids) { Focus = Ids; }
	/// Driving (E2-E6): the eye in the cockpit (world, cm) and what the
	/// sight is on (INDEX_NONE: nothing). `ClearPilot` for the top-down view.
	void SetPilot(const FVector& Eye, int64 SightTarget)
	{
		PilotEye = Eye;
		Sight = SightTarget;
	}
	void ClearPilot()
	{
		PilotEye.Reset();
		Sight = INDEX_NONE;
	}
	/// The unit driven in first person: no bar over it.
	void SetHiddenUnit(int64 Id) { HiddenUnit = Id; }

	/// Bars drawn last frame.
	int32 LastCount() const { return Used; }
	/// Game-thread ms of the last frame's bars (placing and uploading).
	double LastMs() const { return Ms; }

private:
	void OnFrame(const FAcFrame& Frame);
	void OnGameStarted(UAcSimSubsystem& Sim);
	/// A kind's model height over its root, cells (`GameScene.height(_:_:)`).
	double UnitTop(const FAcModelInfo& Model);
	void Upload(int32 Count);

	UPROPERTY(VisibleAnywhere, Category = "Autocraft")
	TObjectPtr<UInstancedStaticMeshComponent> Bars;

	TWeakObjectPtr<AAcTerrain> Terrain;
	TMap<const FAcModelInfo*, double> Tops;
	TArray<FTransform> Xf;
	TArray<float> Data;
	TSet<int64> Focus;
	TOptional<FVector> PilotEye;
	int64 Sight = INDEX_NONE;
	int64 HiddenUnit = INDEX_NONE;
	FDelegateHandle FrameHandle, StartedHandle;
	int32 Used = 0;
	/// Instances that showed a bar last frame (hidden again when unused).
	int32 LastUsed = 0;
	double Ms = 0.0;
	/// `ac.LifeBarsStats`: sums since the last line.
	double StatSince = 0.0, StatMs = 0.0, StatMax = 0.0;
	int32 StatFrames = 0;
};
