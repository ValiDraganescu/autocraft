// `AcPick`: what the pointer rests on in the top-down view (GAME-LAYER.md
// §2.3, chunk D4): the port of `GameController.pick(at:)` and `hit`
// (Sources/Autocraft/GameController+Hover.swift:109, :137).
//
// Plain math on the simulation's state, in SceneKit/sim space (x, height,
// z = sim y; cells), with the ray from the core's `ac::FreeView::ray`. No
// Unreal collision: every unit is a box `radius + 0.2` wide and
// `max(1, 2.4·radius)` tall from its body's height, every building a box
// `radius + 0.15` wide and `max(1.2, 1.5·r)` tall; the nearest box the ray
// enters wins, and units get a bias of 1.5 cells so they win a close call
// over the building they stand against. Units in a Bastion, aboard a
// Dropship or in a Derrick are skipped.
//
// Hook for E1 (part rays, `AcRays`): `FAcPickShapes::Refine` may replace a
// box's entry distance with a precise one (the ray against the model's part
// shapes), or reject the candidate (return an unset optional). Unset: the
// boxes alone, as Swift's top-down view does.
#pragma once

#include "CoreMinimal.h"

#include "FreeView.h"
#include "Types.h"

#include <functional>
#include <optional>

/// What the pointer rests on (`Hover`).
struct FAcHover
{
	enum class EKind : uint8 { Unit, Building };
	EKind Kind = EKind::Unit;
	int64 Id = 0;
	/// A unit a click takes the controls of.
	bool bDrivable = false;

	bool IsUnit() const { return Kind == EKind::Unit; }
	bool operator==(const FAcHover& O) const { return Kind == O.Kind && Id == O.Id && bDrivable == O.bDrivable; }
	bool operator!=(const FAcHover& O) const { return !(*this == O); }
};

/// How tall things stand, and the E1 hook.
struct FAcPickShapes
{
	/// The height a unit's body stands at, cells (`GameScene.unitY`).
	std::function<double(const ac::Unit&)> UnitY;
	/// The ground under a point, cells (`GameScene.groundY`).
	std::function<double(ac::Vec2)> GroundY;
	/// E1: refine a box hit (`T` is where the ray enters the box, before
	/// the bias); return the precise distance, or nothing to drop it.
	std::function<std::optional<double>(const FAcHover& What, const ac::FreeView::Ray& Ray, double T)> Refine;
};

namespace AcPick
{
	/// Where the ray `O + t·D` enters the box [Lo, Hi] (t ≥ 0), if it hits it
	/// (the slab test, `GameController.hit`).
	AUTOCRAFT_API std::optional<double> Hit(const ac::Vec3& O, const ac::Vec3& D, const ac::Vec3& Lo, const ac::Vec3& Hi);

	/// The unit can be taken over: `Player`'s, of a drivable kind, out in
	/// the open (`GameController.drivable`).
	AUTOCRAFT_API bool Drivable(const ac::Unit& U, int64 Player);

	/// Whether picking sees the unit at all (not in a Bastion, aboard or in
	/// a Derrick).
	AUTOCRAFT_API bool Pickable(const ac::Unit& U);

	/// The unit or building the ray rests on (`GameController.pick`).
	/// `State` is what the player sees (`sim.shown`).
	AUTOCRAFT_API std::optional<FAcHover> Pick(const ac::FreeView::Ray& Ray, const ac::GameState& State, int64 Player,
		const FAcPickShapes& Shapes);
}
