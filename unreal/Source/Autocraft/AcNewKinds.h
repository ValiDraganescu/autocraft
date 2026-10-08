// The Peregrine, the Atlas and the Scorpion (docs/new-units.md) in the game
// layer.
//
// The three kinds have their own models (`peregrine_blue`, `atlas_blue`,
// `scorpion_blue`), cockpits (`cockpit_<kind>_blue`), poses (AcPoseNewKinds.h),
// effects (AcEffectsNewKinds.cpp, the shots in AcEffectsShots.cpp), deaths
// (AcEffectsShatter.cpp), icons and sounds, the spoken lines included (library
// voices: Tools/AudioGen/peregrine.manifest.json, atlas.manifest.json).
//
// `BorrowedCockpit` stays as the fallback for a catalog without a kind's
// cockpit model (`HasCockpit`); with the catalog as it is, every kind drives
// its own.
#pragma once

#include "CoreMinimal.h"

#include "Rules.h"

namespace AcNewKinds
{
	/// A kind docs/new-units.md added (the last three of `ac::UnitKind`).
	inline bool IsNew(const ac::UnitKind Kind) { return (int32)Kind >= (int32)ac::UnitKind::peregrine; }

	/// The neighbour whose cockpit a new kind drives with while the catalog
	/// has none of its own.
	AUTOCRAFT_API ac::UnitKind CockpitStandIn(ac::UnitKind Kind);

	/// The catalog holds the new kind's own cockpit (`cockpit_atlas_blue`).
	AUTOCRAFT_API bool HasCockpit(ac::UnitKind Kind);

	/// The kind whose cockpit, eye and cab a kind drives with: its own once
	/// the catalog has its cockpit model, else the stand-in's.
	AUTOCRAFT_API ac::UnitKind BorrowedCockpit(ac::UnitKind Kind);
}
