// The Peregrine, the Atlas and the Scorpion (docs/new-units.md) in the game
// layer: what is still borrowed from a neighbour.
//
// The three kinds have their own models (`peregrine_blue`, `atlas_blue`,
// `scorpion_blue`), poses (AcPoseNewKinds.h), effects (AcEffectsNewKinds.cpp,
// the shots in AcEffectsShots.cpp), deaths (AcEffectsShatter.cpp), icons and
// sounds. One thing is not made yet:
//
//   Peregrine -> Kestrel      Atlas -> Juggernaut      (the Scorpion is its own)
//
//   * `StandIn`: the spoken report-in and death lines of the Peregrine pilot
//     and the Atlas crew. Their voices need ElevenLabs Voice Design, and the
//     account has no free custom voice slot (Tools/AudioGen/peregrine.manifest.json,
//     atlas.manifest.json). Drop a kind when its `v<kind>` and `<kind>death`
//     lines land.
//   * `BorrowedCockpit`: the first-person cockpit, the eye and the HUD cab,
//     only while the catalog has no `cockpit_<kind>_blue` of the kind's own
//     (`HasCockpit`). When the model lands the check turns true and
//     AcCockpitKinds' own pose for the kind takes over; nothing else changes.
//
// `grep -rn AcNewKinds unreal/Source/Autocraft` lists every call site; drop a
// kind from `StandIn` when its voice lands.
#pragma once

#include "CoreMinimal.h"

#include "Rules.h"

namespace AcNewKinds
{
	/// A kind docs/new-units.md added (the last three of `ac::UnitKind`).
	inline bool IsNew(const ac::UnitKind Kind) { return (int32)Kind >= (int32)ac::UnitKind::peregrine; }

	/// The neighbour whose spoken lines (`v<kind>`, `<kind>death`) a new kind
	/// uses (the kind itself for the others and for the Scorpion, whose
	/// "voice" is robot chirps of its own).
	AUTOCRAFT_API ac::UnitKind StandIn(ac::UnitKind Kind);

	/// The neighbour whose cockpit a new kind drives with while the catalog
	/// has none of its own.
	AUTOCRAFT_API ac::UnitKind CockpitStandIn(ac::UnitKind Kind);

	/// The catalog holds the new kind's own cockpit (`cockpit_atlas_blue`).
	AUTOCRAFT_API bool HasCockpit(ac::UnitKind Kind);

	/// The kind whose cockpit, eye and cab a kind drives with: its own once
	/// the catalog has its cockpit model, else the stand-in's.
	AUTOCRAFT_API ac::UnitKind BorrowedCockpit(ac::UnitKind Kind);
}
