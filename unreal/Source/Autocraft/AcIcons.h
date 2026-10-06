// The console's icons (chunk D2; GAME-LAYER.md §2.4 "Icons"): the Swift
// `Icons` pictures, exported once as PNGs (`unreal/Tools/hud/bake_icons.sh`)
// and imported to /Game/UI/Icons/T_Icon_<name> (`Tools/Editor/import_icons.py`).
// Names are the Swift ones: a model's catalog name ("citadel", "ranger",
// "ore"), "up.<upgrade>" or "act.<action>" (build, repair, strike, back).
#pragma once

#include "CoreMinimal.h"

struct FSlateBrush;

struct AUTOCRAFT_API FAcIcons
{
	/// The icon's brush (192 px), or null when there is no such icon.
	static const FSlateBrush* Brush(const FString& Name);
};
