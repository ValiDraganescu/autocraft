// The console's art (chunk D2): the Swift `Cab` and `Chrome` drawing
// (Sources/Autocraft/ConsoleCab.swift, HUDChrome.swift), ported call for
// call from Core Graphics to Slate geometry (FAcArtList, AcArtList.h), so
// the dashboard is the Swift drawing at any window size (its layout changes
// with the width, so it cannot be baked once like D1's plates) and the same
// on every platform.
//
// Each piece is drawn in Swift points (y up, as AcCab.h) and baked into a
// render target when the layout changes (FAcBakedArt, AcBakedArt.h), at the
// scale Swift drew it: 2 pixels a point (the view frame 1.5, the scan lines
// 1). The random wear keeps Swift's order of draws (`SplitMix`), so the
// scratches stand where Swift's do.
//
// What is drawn here vs. in Slate: Core Graphics text is not ported (the
// centre face's stencil and the deck's station name are Slate text over the
// art, in Barlow Condensed like the rest of the HUD).
#pragma once

#include "CoreMinimal.h"

#include "AcCab.h"

class FAcBakedArt;
class UTexture2D;

/// One drawn image: `Width` × `Height` pixels, rows from the top, straight
/// alpha. `Points`: the size it stands for in points.
struct FAcArtImage
{
	int32 Width = 0;
	int32 Height = 0;
	TArray<FColor> Pixels;
	FVector2D Points = FVector2D::ZeroVector;

	bool IsValid() const { return Width > 0 && Height > 0 && Pixels.Num() == Width * Height; }
};

/// `Chrome.Cuts`: how far each corner is cut off.
struct FAcCuts
{
	double TL = 0, TR = 0, BR = 0, BL = 0;
};

namespace AcCabArt
{
	/// Face `K` (0 left, 1 centre, 2 right) drawn flat over its rectangle
	/// (`Cab.faceArt`). The left face's deck block holds the music player's
	/// bay (`deckBay`) when it is wider than 60 points.
	AUTOCRAFT_API void BakeFace(FAcBakedArt& Art, int32 K, const FAcCabLayout& Layout);
	/// The dashboard's top with no cab frame (`Cab.node(_, frame: false)`):
	/// the band from the bottom to `Top() + 38`.
	AUTOCRAFT_API void BakeDashTop(FAcBakedArt& Art, const FAcCabLayout& Layout);
	/// A machine's cab (the `.cab` look, chunk D3; `Cab.node(_, frame: true)`):
	/// the rail and pillars round the window, the lip and seal at the glass,
	/// the lamps' sockets and the dashboard's top, in the four bands round
	/// the glass (bottom, top, left, right), each where it goes.
	AUTOCRAFT_API void BakeFramed(TArray<TPair<FAcRect, FAcBakedArt>>& Bands, const FAcCabLayout& Layout);
	/// The thin frame round the whole view (`Cab.viewFrame`).
	AUTOCRAFT_API void BakeViewFrame(FAcBakedArt& Art, FVector2D Size, double Rail = 12);
	/// A command card button's face (`Chrome.buttonFace`).
	AUTOCRAFT_API void BakeButtonFace(FAcBakedArt& Art, double Side, bool bEnabled);
	/// Scan lines for a hologram (`Chrome.scanlines`).
	AUTOCRAFT_API void BakeScanlines(FAcBakedArt& Art, FVector2D Size);
	/// A plate (`Chrome.plate`), padded 6 points round `Size` for its shadow
	/// and glow: draw it over the plate's rectangle grown by `PlatePad`.
	AUTOCRAFT_API void BakePlate(FAcBakedArt& Art, FVector2D Size, FAcCuts Cuts, bool bLeds, int32 Seed);
	/// Points round a plate's rectangle that `Plate` adds.
	constexpr double PlatePad = 6;

	/// A transient texture of `Image` (sRGB, clamped, bilinear, no mips).
	AUTOCRAFT_API UTexture2D* ToTexture(const FAcArtImage& Image, const TCHAR* Name);
}
