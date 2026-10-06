// The console's art (chunk D2): the Swift `Cab` and `Chrome` drawing
// (Sources/Autocraft/ConsoleCab.swift, HUDChrome.swift) ported call for call
// to Core Graphics, so the dashboard is the Swift pixels at any window size
// (its layout changes with the width, so it cannot be baked once like D1's
// plates). Mac only (AcCabArtMac.cpp); elsewhere a flat stand-in.
//
// Every image is drawn as Swift draws it (`Chrome.context`: y up, device
// RGB, premultiplied, at `Scale` pixels per point) and returned as straight
// alpha BGRA rows from the top, ready for a `UTexture2D`.
//
// What is drawn here vs. in Slate: Core Graphics text is not ported (the
// centre face's stencil and the deck's station name are Slate text over the
// art, in Barlow Condensed like the rest of the HUD).
#pragma once

#include "CoreMinimal.h"

#include "AcCab.h"

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

/// A piece of a larger drawing: where it goes (Swift points, y up) and its image.
struct FAcArtBand
{
	FAcRect At;
	FAcArtImage Image;
};

/// `Chrome.Cuts`: how far each corner is cut off.
struct FAcCuts
{
	double TL = 0, TR = 0, BR = 0, BL = 0;
};

namespace AcCabArt
{
	/// Face `K` (0 left, 1 centre, 2 right) drawn flat over its rectangle
	/// (`Cab.faceArt`), at 2 px a point. The left face's deck block holds the
	/// music player's bay (`deckBay`) when it is wider than 60 points.
	AUTOCRAFT_API FAcArtImage Face(int32 K, const FAcCabLayout& Layout);
	/// The dashboard's top with no cab frame (`Cab.node(_, frame: false)`):
	/// the band from the bottom to `Top() + 38`, at 2 px a point.
	AUTOCRAFT_API FAcArtImage DashTop(const FAcCabLayout& Layout);
	/// A machine's cab (the `.cab` look, chunk D3; `Cab.node(_, frame: true)`):
	/// the rail and pillars round the window, the lip and seal at the glass,
	/// the lamps' sockets and the dashboard's top, cut into the four bands
	/// round the glass (bottom, top, left, right), at 2 px a point.
	AUTOCRAFT_API TArray<FAcArtBand> Framed(const FAcCabLayout& Layout);
	/// The thin frame round the whole view (`Cab.viewFrame`), at 1.5 px a point.
	AUTOCRAFT_API FAcArtImage ViewFrame(FVector2D Size, double Rail = 12);
	/// A command card button's face (`Chrome.buttonFace`), at 2 px a point.
	AUTOCRAFT_API FAcArtImage ButtonFace(double Side, bool bEnabled);
	/// Scan lines for a hologram (`Chrome.scanlines`), at 1 px a point.
	AUTOCRAFT_API FAcArtImage Scanlines(FVector2D Size);
	/// A plate (`Chrome.plate`), padded 6 points round `Size` for its shadow
	/// and glow, at 2 px a point.
	AUTOCRAFT_API FAcArtImage Plate(FVector2D Size, FAcCuts Cuts, bool bLeds, int32 Seed);
	/// Points round a plate's rectangle that `Plate` adds.
	constexpr double PlatePad = 6;

	/// A transient texture of `Image` (sRGB, clamped, bilinear, no mips).
	AUTOCRAFT_API UTexture2D* ToTexture(const FAcArtImage& Image, const TCHAR* Name);
}
