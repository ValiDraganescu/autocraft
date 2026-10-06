// `AcOutline`: the outline pass (GAME-LAYER.md "Outline pass"), a post-process
// material (`/Game/Materials/M_AcOutline`, Tools/Editor/make_outline_material.py)
// that draws a thin line round every pixel group whose custom stencil value
// is set, in a colour chosen by the value. Reusable: anything that renders
// custom depth with a stencil value from the table below gets an outline.
//
// Stencil values (0 = not outlined):
//   1..8   `StencilTeamBase + team`: the colour of player `team` (`PaintN` of
//          `MPC_AcTeams`, read in the material), drawn faint (a friend's);
//   16     `StencilFoe`: red, drawn firm (an enemy's).
// Add a value by giving it a colour in the material (and a case in
// `ClassOf`/`Name`).
//
// Who writes the stencil: `UAcWorldRenderer`. Custom stencil is per
// primitive, not per instance (checked in UE 5.8: `UInstancedStaticMeshComponent`
// has none), so the renderer keeps a second set of instanced components per
// stencil value and moves an object's instances to the set its `StencilFor`
// names (`FLayout` keyed by (model, stencil)). The pass itself is off (the
// post-process component disabled: no draw, no stencil fetch) while no
// object has a stencil value.
//
// The buried Scorpion (docs/new-units.md "Looks"): to the side that owns it
// (the local player and its allies) the mound and the faint outline of its
// owner's colour; to an enemy that sees it (`Simulation::shown` leaves it
// out unless revealed) the mound and a red outline.
//
// Needs `r.CustomDepth=3` (DefaultEngine.ini: the stencil is written only
// then). `ac.Outline 0` turns the pass off.
#pragma once

#include "CoreMinimal.h"

#include "Types.h"

namespace AcOutline
{
	inline constexpr uint8 StencilNone = 0;
	inline constexpr uint8 StencilTeamBase = 1;
	inline constexpr uint8 StencilFoe = 16;

	enum class EClass : uint8
	{
		None,
		/// A friend's: faint, in a team's colour.
		Team,
		/// A foe's: firm, red.
		Foe
	};

	AUTOCRAFT_API EClass ClassOf(uint8 Stencil);
	/// The team (0-7) whose colour a `Team` stencil draws; -1 for the others.
	AUTOCRAFT_API int32 TeamOf(uint8 Stencil);
	/// The stencil value of team `Team`'s outline.
	AUTOCRAFT_API uint8 TeamStencil(int64 Team);

	/// The outline a unit gets, as the stencil value (`StencilNone`: none).
	/// Only a Scorpion that is buried, or burying or digging out
	/// (`Anchor` above 0), has one: the local player's side sees its own in
	/// the owner's colour, an enemy that is shown one sees it red. `bAllied`:
	/// the unit's owner is allied with the local player (itself included).
	AUTOCRAFT_API uint8 StencilFor(ac::UnitKind Kind, const std::optional<double>& Anchor, int64 Owner, bool bAllied);

	/// The line's colour for a stencil value, as drawn (sRGB, alpha its
	/// strength), `TeamPaint` the eight team colours (`PaintN`). The material
	/// does the same; this is for tests and tools.
	AUTOCRAFT_API FLinearColor Colour(uint8 Stencil, TConstArrayView<FLinearColor> TeamPaint);
	inline constexpr float TeamStrength = 0.55f;
	inline constexpr float FoeStrength = 0.95f;
}
