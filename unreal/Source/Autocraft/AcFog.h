// `AAcFog`: the fog of war on screen (chunk B10, GAME-LAYER.md §2.5), the
// port of `FogOfWar` (Sources/Autocraft/FogOfWar.swift) for the top-down
// view.
//
// - Levels. One float per cell of the core's `ac::Vision` grid (the walking
//   grid's half cells, `Vision::cell` = 0.5), easing at 6/s toward 1 (the
//   local player's team sees the ground now), 0.5 (explored) or 0.3 (never
//   seen) (`FogOfWar.update`). Allies share sight and explored ground in the
//   core (`Simulation::look`), so this reads the local player's own
//   `sights[p]` and `intel[p]`: 8 players in teams need nothing more here.
// - `VisionTexture()`: those levels as a linear BGRA8 texture (all four
//   channels = level), bilinear, clamped, updated every frame. Texel (x, z)
//   covers sim `origin + (x, z)·0.5` (cells) to one half cell further; row
//   0 is the grid's min sim y (= min Unreal Y). Sample it at
//   `uv = (sim − GridOrigin()) / GridSize()` with sim = Unreal XY / 100.
// - The pass, `M_Fog` (`Tools/Editor/make_fog_material.py`): a post-process
//   material on an unbound `UPostProcessComponent` of this actor. Each
//   pixel's ground point from the scene depth, five taps of the texture,
//   the unseen ground greyed, tinted blue and darkened (Swift's shader one
//   to one); it lifts over 4 cells past the map's edge.
// - Hiding: units and buildings follow `UAcSimSubsystem::Shown()` in the
//   renderer (B1): the others' units out of sight are not drawn, their
//   buildings show as last seen. `Unseen()` lists the others' units and
//   buildings not in sight now (Swift's `world.unseen`: no life bar on a
//   building as last seen; while driving, such buildings are not drawn).
//
// For the minimap (D7): `MinimapTexture()` is Swift's `FogOfWar.image()`:
// black with alpha (1 − level)·1.15, straight alpha, refreshed every 0.25 s
// of real time (`GameController.swift:461`); same texel layout as the
// vision texture. Draw it as a Slate image brush over the terrain picture,
// stretched over the rect `GridOrigin()`..`GridOrigin() + GridSize()` (sim
// cells, mapped the way the minimap maps the ground; note the minimap's
// own row 0 may be the far edge). `MinimapRevision()` changes when it was
// refreshed. Or read `Levels()`/`LevelAt()` directly.
//
// Toggles: `ac.Fog 0|1` (console; default 1) turns the pass and the
// minimap's fog off and on, as Swift's `AUTOCRAFT_FOG=off` (also read here,
// with `-AcNoFog`). It changes nothing in the game: the others' units out of
// sight stay hidden. A map with no fog in the core (a session with `fog:
// false`, the playground; `UAcSimSubsystem` builds the sim) has no grid:
// `HasGrid()` false, no pass, levels all 1.
// `SetSuppressed(true)` turns the top-down pass off for the driving views
// (E4 draws its own veil).
//
// Usage: `AAcFog::SpawnFor(World)` once per game world (AAcGameMode).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Types.h"

#include <set>

#include "AcFog.generated.h"

class UAcSimSubsystem;
class UMaterialInstanceDynamic;
class UPostProcessComponent;
class UTexture2D;
struct FAcFrame;

UCLASS()
class AUTOCRAFT_API AAcFog : public AActor
{
	GENERATED_BODY()

public:
	/// Brightness of ground in sight, seen before, and never seen.
	static constexpr float InSight = 1.0f;
	static constexpr float Explored = 0.5f;
	static constexpr float Unexplored = 0.3f;
	/// How fast the light catches up with the sight (per second).
	static constexpr float Rate = 6.0f;
	/// The minimap's fog is redrawn this often (real seconds).
	static constexpr double MinimapEvery = 0.25;

	AAcFog();

	static AAcFog* SpawnFor(UWorld* World);
	static AAcFog* Find(const UWorld* World);

	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/// The map has a fog grid (the core's `Vision`).
	bool HasGrid() const { return Width > 0 && Height > 0; }
	/// The fog shows: a grid, `ac.Fog` on, not suppressed.
	bool IsOn() const;
	/// Turn the top-down pass off (driving) or back on.
	void SetSuppressed(bool bInSuppressed) { bSuppressed = bInSuppressed; }

	/// The grid, in sim cells: its corner at min x, min y, its size, and
	/// its texels.
	ac::Vec2 GridOrigin() const { return Origin; }
	ac::Vec2 GridSize() const;
	int32 GridWidth() const { return Width; }
	int32 GridHeight() const { return Height; }
	/// Each texel's level (row by row from min y), 0.3 … 1.
	TConstArrayView<float> Levels() const { return Level; }
	/// The level under sim point `P` (1 off the grid or without fog).
	float LevelAt(ac::Vec2 P) const;

	/// The levels as a linear BGRA8 texture (see the header comment).
	UTexture2D* VisionTexture() const { return Vision; }
	/// Swift's minimap fog image (see the header comment).
	UTexture2D* MinimapTexture() const { return Minimap; }
	uint32 MinimapRevision() const { return MinimapRev; }

	/// The others' units and buildings the local team does not see now
	/// (`sim.unseen(by:)`), worked out once a frame on first ask.
	const std::set<int64_t>& Unseen();

	/// Move the light to the sight at once (a new game, a staged scene).
	void Snap() { bSnapNext = true; }

private:
	void OnGameStarted(UAcSimSubsystem& Sim);
	void OnFrame(const FAcFrame& Frame);
	void Rebuild(UAcSimSubsystem& Sim);
	void Step(UAcSimSubsystem& Sim, double Dt);
	void UploadVision();
	void UploadMinimap();
	void ApplyPass();

	UPROPERTY(Transient)
	TObjectPtr<UPostProcessComponent> Post;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> Vision;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> Minimap;

	ac::Vec2 Origin;
	int32 Width = 0;
	int32 Height = 0;
	TArray<float> Level;
	TArray<uint8> VisionBytes;
	TArray<uint8> MinimapBytes;
	double SinceMinimap = 0.0;
	uint32 MinimapRev = 0;
	bool bSnapNext = true;
	bool bSuppressed = false;
	bool bWasOn = false;

	std::set<int64_t> UnseenCache;
	uint64 UnseenFrame = MAX_uint64;
	uint64 FrameCount = 0;

	FDelegateHandle GameStartedHandle;
	FDelegateHandle FrameHandle;
};
