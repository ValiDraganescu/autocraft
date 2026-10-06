// `AcPoseVehicles`: the per-frame poses of the Comet, the Juggernaut and the
// Firefly (chunk B3, GAME-LAYER.md §2.6), registered with
// `FAcPoseRegistration` (AcPose.h). Ports of
//   Comet       `GameScene.pose(_: Models.Comet, …)`      Models+Comet.swift:335
//               (run, jump arc and tuck, pistols, jetpack, wing cases)
//   Juggernaut  `GameScene.pose(_: Models.Juggernaut, …)` Models+Juggernaut.swift:272
//               (stomp, torso twist to the target, launchers, volley kick)
//               with the twist and aim of `GameScene.sync` (GameScene.swift:682)
//   Firefly     `GameScene.pose(_: Models.Firefly, …)`    Models+Firefly.swift:699
//               (wheel roll held to 0.35 lug a frame, tyre smear, steering,
//               suspension, the tail turning like a turret, pilot light, the
//               jet of flame, the splash ball, the flare)
// with the game's default skins (the beetle Comet, the gorilla Juggernaut,
// the scorpion Firefly), as exported.
//
// Materials these poses expect (Tools/Editor/make_flame_materials.py):
// `M_AcFlame` for the Firefly's jet layers (ramp × scrolling noise × facing²),
// `M_AcSprite` (camera-facing, additive) for the splash ball, the flare and
// the Comet's jet halos, `M_AcSmear` (translucent; custom data 1 is its
// OPACITY, not an emission) for the tyre smears, and the hot-lip `heatGlow`
// at glow-orange intensity 1 (the export froze it at 0); the smear material is
// B2's (make_infantry_materials.py). Emission values are
// relative to the exported intensity, as everywhere (1 = as exported).
//
// Particles and the flame's light are not drawn here: each frame's rates are
// left in the unit's memory for the effects chunks, see `AcPoseVehicles::Fx`.
#pragma once

#include "CoreMinimal.h"

#include "Rules.h"

struct FAcUnitMemory;

/// What a B3 unit's emitters and light do this frame (the Swift pose sets
/// these on its `SCNParticleSystem`s and `SCNLight`). Rates are particles a
/// second, speeds cells a second. Emitter parts in the catalog: the Comet's
/// `emitter_1`/`emitter_2` (jets[0]/[1]); the Juggernaut's `muzzles_0`/`_1`;
/// the Firefly's `emitter_1` (exhaust, on the body), `emitter_2` (fire and
/// embers, in the flame), `flameEnd` (smoke) and `flameLightNode` (light).
struct FAcVehicleFx
{
	/// Comet: each nozzle's exhaust (jets[i].birthRate) and its speed.
	float JetRate[2] = {0.f, 0.f};
	float JetVelocity = 1.2f;
	/// Juggernaut: smoke from each bore (110 for 0.14 s after a volley).
	float MuzzleSmokeRate = 0.f;
	/// Firefly: the jet's fire and embers (speeds reach the jet's head in
	/// their lifetimes), smoke off the splash, exhaust from the stacks.
	float FireRate = 0.f, FireVelocity = 0.f;
	float EmberRate = 0.f, EmberVelocity = 0.f;
	float SmokeRate = 0.f;
	float ExhaustRate = 0.f;
	/// Firefly: the flame's omni light (SceneKit intensity, 0 = off) and its
	/// reach (attenuation end), cells; it sits at the `flameLightNode` part.
	float FlameLight = 0.f;
	float FlameLightRange = 3.f;
};

namespace AcPoseVehicles
{
	/// This frame's emitter rates and light of a Comet, Juggernaut or
	/// Firefly (after the renderer's poses ran; game thread); null for other
	/// kinds or before the first pose.
	AUTOCRAFT_API const FAcVehicleFx* Fx(ac::UnitKind Kind, const FAcUnitMemory& Memory);

	/// The Firefly's wheel radii (front, rear) and lugs per tyre: the drawn
	/// roll is held to 0.35 of a lug a frame.
	inline constexpr double FireflyFrontRadius = 0.23;
	inline constexpr double FireflyRearRadius = 0.26;
	inline constexpr int32 FireflyLugs = 16;
	/// How long one burst of the Firefly's flame burns, seconds.
	inline constexpr double FireflyBurn = 0.45;
}
