// `AcPose`: how a unit or a building stands this frame (GAME-LAYER.md §2.6,
// §3.2). The pose functions are the Unreal side of the Swift `pose` and
// `animate` functions (`GameScene.swift`, `Models+<Kind>.swift`): plain
// functions of the simulation's state, a little renderer-only memory per id,
// and the game time, that fill each part's local transform, visibility and
// emission. `UAcWorldRenderer` (AcWorldRenderer.h) calls them every frame
// and turns the result into instance transforms; the cockpits (E5) and the
// rays (E1) can call them too.
//
// Writing a pose for a kind (chunks B2-B7), in its own file
// (`AcPose_Ranger.cpp`):
//
//   static void PoseRanger(const FAcPoseContext& C, FAcPose& P)
//   {
//       static const FAcPartIndex Hip(TEXT("hips_0"));           // the name, hashed once
//       P.Local[Hip.Get(C.Model)].SetRotation(...);              // relative to the parent part
//       P.bAnimated = true;                                      // moves with time
//   }
//   static FAcPoseRegistration Ranger(ac::UnitKind::ranger, &PoseRanger);
//
// Before a kind's function runs, the renderer has already put the model
// where the rest pose puts it (`AcPose::RestUnit`/`RestStructure`):
// `Placement` (the model root's parent: the unit's ground point and
// heading, a flyer's smoothed flight height, a building rising out of the
// ground while it is built), every `Local` at the part's rest transform,
// `Visible` from the export's hidden flags, `Emission` 1. A kind's function
// only changes what moves. Kinds without one draw at the rest pose.
//
// Threading: the renderer runs the pose functions of different ids in
// parallel (`ac.RenderParallel`). A pose function reads `C.State` and the
// catalog, and writes only `P` and `C.Memory` (its own id's). Anything
// else (effects, sounds) goes out as a `Cue` that the renderer hands on,
// on the game thread, after the poses (`UAcWorldRenderer::OnCue`).
//
// Units: Placement and Local are Unreal space, cm, like the catalog's rest
// transforms (`FAcModelPart::Rest`). The SceneKit rest values the Swift code
// overwrites are in `FAcModelPart::SkPosition/SkEuler/SkScale`; convert
// with AcSpace.h (`TransformFromSceneKit`, `QuatFromSceneKit`).
#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"

#include "Rules.h"
#include "SimdMath.h"
#include "Types.h"

struct FAcModelInfo;
namespace ac
{
	struct TerrainField;
}

/// A kind's own memory per id (a Prospector's `ForkMotion`, a Ranger's
/// minigun spin, aim easing): derive from this and get it with
/// `FAcUnitMemory::State<T>()`.
struct FAcPoseState
{
	virtual ~FAcPoseState() = default;
};

/// What the renderer remembers per unit or building that the simulation
/// does not (`GameScene`'s per-id dictionaries, :40-80; `forget` :1149).
/// Kept by the renderer from the id's first frame to its last.
struct FAcUnitMemory
{
	/// Game time it was first drawn; a unit that appears mid-game (not at
	/// the first sync after a game starts) also gets `BornAt` (a Dropship
	/// or a Kestrel lifts off its Spacedock's pad: `bornAt`).
	double FirstSeen = 0.0;
	TOptional<double> BornAt;
	/// When it last fired or struck (game time; `lastShot`), at what
	/// (`Shot.target`) and where the shot was spent (`Shot.at`/`Missed.at`).
	/// Set by the renderer from the frame's events before the poses.
	TOptional<double> LastShot;
	TOptional<int64> LastTarget;
	TOptional<ac::Vec2> LastShotAt;
	/// Heading last frame and the smoothed turn rate, rad/s (`turning`).
	TOptional<double> LastHeading;
	double TurnRate = 0.0;
	/// A flyer's smoothed ground height under it, cells (`airY`).
	TOptional<double> AirY;
	/// A Dropship's load last frame and when it last unloaded.
	TOptional<int32> CargoCount;
	TOptional<double> UnloadedAt;
	/// A Firefly's last jet length, cells (`flameLength`, default 3).
	double FlameLength = 3.0;
	/// A Longbow's anchor last frame and a tank's heading last frame.
	double AnchorOf = 0.0;
	TOptional<double> TankHeading;
	/// Where a Juggernaut last fired (`lastAim`).
	TOptional<ac::Vec2> LastAim;

	/// The kind's own state (see `FAcPoseState`). One kind per id, so one T.
	template <class T>
	T& State()
	{
		if (!KindState) KindState = MakeUnique<T>();
		return static_cast<T&>(*KindState);
	}
	TUniquePtr<FAcPoseState> KindState;
};

/// Something a pose wants done that is not a transform: sparks at the
/// drill bit (`effects.crystalBite`), a weld flash. Handed to
/// `UAcWorldRenderer::OnCue` on the game thread after the poses.
struct FAcPoseCue
{
	FName What;
	/// World position, cm.
	FVector At = FVector::ZeroVector;
	float Value = 0.f;
};

/// What a pose function reads.
struct FAcPoseContext
{
	/// The game as the local player sees it (`sim.shown`), this frame.
	const ac::GameState& State;
	/// The unit or the building posed (exactly one is set).
	const ac::Unit* Unit = nullptr;
	const ac::Structure* Structure = nullptr;
	/// Its model (the canonical, blue export; the team is custom data).
	const FAcModelInfo& Model;
	/// The ground, cells (`TerrainField.height`, as `GameScene.groundY`).
	const ac::TerrainField& Field;
	FAcUnitMemory& Memory;
	/// Game time (`state.time`) and game seconds since the last sync.
	double Time = 0.0;
	double Dt = 0.0;
	/// The unit the local player drives (its guns follow the crosshair).
	bool bDriven = false;
	int64 LocalPlayer = 0;
	/// The driven unit only: the crosshair's point (world cm) its held guns
	/// aim at (`GameScene.pilotAim`; E4, AcPilotAim.h). Null otherwise.
	const FVector* PilotAim = nullptr;
	/// How dark it is, 0 by day to 1 by night (`AAcDaylight::GetDark`, which
	/// respects a held `ac.Hour`): read by the renderer on the game thread and
	/// given to every pose, for lamps that burn at night. 0 where nothing sets it.
	double Dark = 0.0;

	/// Ground height under `P`, cells.
	double Ground(ac::Vec2 P) const;
	/// A player's upgrades (empty for no such player).
	bool HasUpgrade(int64 Player, ac::Upgrade Upgrade) const;
};

/// What a pose function fills: the model's parts this frame.
struct FAcPose
{
	/// The model root's parent in the world (cm): the unit's ground point
	/// and heading (UE yaw = +heading), a building's site. Part 0's `Local`
	/// (its rest: the Ranger's 1.15 scale) goes under it.
	FTransform Placement = FTransform::Identity;
	/// Each part's transform relative to its parent part, cm (index = the
	/// catalog's part index; parents come first).
	TArray<FTransform> Local;
	/// Each part shown (a hidden part hides its children, as in SceneKit).
	TArray<bool> Visible;
	/// Each part's emission scale (custom data 1; 1 = as exported). Applies
	/// to every mesh of the part.
	TArray<float> Emission;
	/// The pose moves with time (walk cycles, blinking lamps, spinning
	/// dishes): drawn again every frame. False (the rest pose): drawn again
	/// only when `Placement` changes, so idle armies and finished buildings
	/// upload nothing.
	bool bAnimated = false;
	/// The whole model hidden (aboard, in a Bastion or a Derrick, driven in
	/// first person). Set by the renderer's hide rules; a pose may set it.
	bool bHidden = false;
	/// A remembered enemy building out of sight: its last-known look, frozen, unlit and inert
	/// (no emission, cues, lamps, bars, smoke). Set by the renderer, read by the effects.
	bool bFrozen = false;
	/// Effects and the like, for the game thread.
	TArray<FAcPoseCue, TInlineAllocator<2>> Cues;
	/// Lamps merged into a part with other meshes (a Dropship's claw
	/// beacons in its `body`, the heal beam's core beside its halo): this
	/// (part, material) mesh's emission instead of `Emission[Part]` (B5).
	struct FMeshEmission
	{
		int32 Part = INDEX_NONE;
		FName Material;
		float Value = 1.f;
	};
	TArray<FMeshEmission, TInlineAllocator<2>> MeshEmission;

	/// Everything back to the model's rest pose.
	void Reset(const FAcModelInfo& Model);
};

/// A part's index by name, looked up in the model on every `Get`. No cache:
/// the poses run in parallel and a `static` index is shared by every thread
/// (and by every kind a function serves); a cached (model, index) pair
/// written from two threads gave a part of the wrong model (B6's Hab Dome
/// crash). The lookup is one FName hash probe.
struct AUTOCRAFT_API FAcPartIndex
{
	explicit FAcPartIndex(FName InName) : Name(InName) {}
	/// INDEX_NONE if the model has no such part.
	int32 Get(const FAcModelInfo& Model) const;

private:
	FName Name;
};

using FAcPoseFn = void (*)(const FAcPoseContext& Context, FAcPose& Pose);

namespace AcPose
{
	/// Register a kind's pose (see `FAcPoseRegistration`).
	AUTOCRAFT_API void Register(ac::UnitKind Kind, FAcPoseFn Fn);
	AUTOCRAFT_API void Register(ac::StructureKind Kind, FAcPoseFn Fn);
	/// The kind's pose, or null (draw the rest pose).
	AUTOCRAFT_API FAcPoseFn Find(ac::UnitKind Kind);
	AUTOCRAFT_API FAcPoseFn Find(ac::StructureKind Kind);

	/// The rest pose every model gets first (the fallback): `Reset`, then
	/// the placement. Units: on the ground at `position`, turned by
	/// `heading`; flyers over `FlightBase`; a Comet mid-jump at `FlightY`.
	/// Buildings: on the ground, sunk by `ConstructionSink` while built.
	AUTOCRAFT_API void RestUnit(const FAcPoseContext& Context, FAcPose& Pose);
	AUTOCRAFT_API void RestStructure(const FAcPoseContext& Context, FAcPose& Pose);

	/// Model names: `ranger`, `habdome` (the catalog's `base`).
	AUTOCRAFT_API const TCHAR* ModelBase(ac::UnitKind Kind);
	AUTOCRAFT_API const TCHAR* ModelBase(ac::StructureKind Kind);

	/// How tall a building's body is (`GameScene.makeBuilding`'s `height`),
	/// cells.
	AUTOCRAFT_API double BuildingHeight(ac::StructureKind Kind);
	/// How far a building's body is under the ground while it is built
	/// (`-(1 - progress)·height·0.92`), cells (≤ 0).
	AUTOCRAFT_API double ConstructionSink(const ac::Structure& S);
	/// A flyer's hover over its ground (`Models.hover`): already in the
	/// body's rest transform, so the placement stays at the ground.
	AUTOCRAFT_API double Hover(ac::UnitKind Kind);
	/// The ground a flyer flies over, smoothed (`GameScene.flightBase`:
	/// rises early over a cliff ahead, sinks slowly off one). Updates
	/// `Memory.AirY`; call once a frame per unit (RestUnit does).
	AUTOCRAFT_API double FlightBase(const FAcPoseContext& Context);
	/// A Comet's feet mid-jump (`GameScene.flightY`), else the ground.
	AUTOCRAFT_API double FlightY(const FAcPoseContext& Context);
	/// Smoothed turn rate, rad/s (`GameScene.turning`). Updates the memory:
	/// call once a frame per unit.
	AUTOCRAFT_API double Turning(const FAcPoseContext& Context);
}

/// Registers a kind's pose at load: a file-scope
/// `static FAcPoseRegistration Reg(ac::UnitKind::ranger, &PoseRanger);`.
struct FAcPoseRegistration
{
	FAcPoseRegistration(ac::UnitKind Kind, FAcPoseFn Fn) { AcPose::Register(Kind, Fn); }
	FAcPoseRegistration(ac::StructureKind Kind, FAcPoseFn Fn) { AcPose::Register(Kind, Fn); }
};
