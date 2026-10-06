// The minimap's pictures (chunk D7, GAME-LAYER.md §2.5), made in plain C++:
//
// - `Terrain`: `Minimap.terrainImage` (Sources/Autocraft/Minimap.swift:287),
//   the map from above at `PixelsPerCell` (4 in the game): low and high
//   ground, grass and plating mixed in, hill shading lit from the upper
//   left, cliffs dark, ground out of play black. Row 0 is the far edge (min
//   sim y), as the minimap draws it. sRGB bytes, opaque. Also for the new-
//   game dialog's preview (F1, Swift `MapPreview`) and the command map (D8).
// - `Disc`, `Diamond`: white marks with soft edges, tinted when drawn.
// - `SightFan`: `Minimap.sightFan`, the driving view's wedge, pointing up.
//
// Each returns an `FAcArtImage`; `AcCabArt::ToTexture` makes the texture.
#pragma once

#include "CoreMinimal.h"

#include "AcCabArt.h"

namespace ac
{
	struct TerrainField;
	struct GroundRect;
}

namespace AcMinimapBake
{
	AUTOCRAFT_API FAcArtImage Terrain(const ac::TerrainField& Field, const ac::GroundRect& Rect, double PixelsPerCell = 4);
	/// A white disc filling a `Size`-pixel square, edges a pixel soft.
	AUTOCRAFT_API FAcArtImage Disc(int32 Size = 64);
	/// A white diamond (a square on its corner) filling a `Size`-pixel square.
	AUTOCRAFT_API FAcArtImage Diamond(int32 Size = 64);
	/// The sight's fan in points: `Reach` long, `Half` radians either side of
	/// up, its apex `Pad` above the picture's bottom middle, edge lines `Line`
	/// wide; drawn at `Scale` pixels a point. `Points` is the picture's size.
	AUTOCRAFT_API FAcArtImage SightFan(double Reach, double Half, double Pad, double Line, double Scale = 4);
}
