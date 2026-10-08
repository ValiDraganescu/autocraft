// A vector drawing for Slate (the console's art, AcCabArt.h): what Core
// Graphics drew for the Swift game, as Slate geometry, so the art is the
// same on every platform. Coordinates are Swift points, y up, as AcCab.h.
//
// The drawing is recorded once (fills, gradients, strokes, soft shadows,
// all anti-aliased) into one triangle mesh with a colour per vertex, then
// replayed with `FSlateDrawElement::MakeCustomVerts`: straight onto a widget,
// or once into a render target (FAcBakedArt, AcBakedArt.h) when it is drawn
// every frame and changes only with the layout.
//
// Colours are Swift's (`Rgb`, `Gray`: NSColor's calibrated RGB and white),
// turned into sRGB as Core Graphics turned them into the bitmap's device RGB,
// then into Slate's vertex colours as FAcHudStyle::Srgb does. Gradients mix
// those sRGB components, as Core Graphics does.
//
// Shadows are Core Graphics' too: their blur and offset are in pixels (the
// context's scale does not apply to them), a Gaussian of 0.47 × blur.
//
// Shapes are convex polygons, counter-clockwise (y up). A region is a list
// of convex pieces that do not overlap (a rectangle with a hole in it is
// cut into pieces: `Minus`). Clipping, as `CGContextClip` does, is a region
// passed to the call.
#pragma once

#include "CoreMinimal.h"

#include "AcCab.h"

class FSlateWindowElementList;
struct FGeometry;
enum class ESlateDrawEffect : uint8;

/// A convex polygon, counter-clockwise in Swift points (y up).
using FAcPoly = TArray<FVector2D>;

/// Pieces that do not overlap, each convex: where a call may draw.
struct AUTOCRAFT_API FAcRegion
{
	TArray<FAcPoly> Pieces;

	FAcRegion() = default;
	FAcRegion(FAcPoly Piece) { Pieces.Add(MoveTemp(Piece)); }
	FAcRegion(const FAcRect& R);
	bool IsSingle() const { return Pieces.Num() == 1; }
	FBox2D Bounds() const;
	bool Contains(FVector2D P) const;
};

/// `CGGradient`: colours (Swift components) at positions 0…1.
struct AUTOCRAFT_API FAcStops
{
	TArray<FLinearColor> Colors;
	TArray<double> At;

	FAcStops() = default;
	FAcStops(std::initializer_list<FLinearColor> InColors, std::initializer_list<double> InAt);
	/// The colour at `T` (clamped to 0…1).
	FLinearColor Color(double T) const;
};

namespace AcArt
{
	/// Swift's colours, `NSColor(calibratedRed:green:blue:alpha:)` and
	/// `NSColor(white:alpha:)`: generic RGB (gamma 1.8, Apple's primaries),
	/// as sRGB components, as Core Graphics draws them into a device RGB
	/// bitmap (fitted to it: within 1.3 of 255).
	AUTOCRAFT_API FLinearColor Rgb(double R, double G, double B, double A = 1);
	inline FLinearColor Gray(double W, double A = 1) { return Rgb(W, W, W, A); }
	inline FLinearColor WithAlpha(const FLinearColor& C, double A) { return FLinearColor(C.R, C.G, C.B, (float)A); }

	/// Polygons.
	AUTOCRAFT_API FAcPoly RectPoly(const FAcRect& R);
	/// Counter-clockwise, without repeated points.
	AUTOCRAFT_API FAcPoly Clean(FAcPoly P);
	AUTOCRAFT_API double Area(const FAcPoly& P);
	AUTOCRAFT_API FAcPoly Moved(const FAcPoly& P, FVector2D By);
	/// `P` clipped to the convex `Clip` (Sutherland-Hodgman).
	AUTOCRAFT_API FAcPoly Clip(const FAcPoly& P, const FAcPoly& Clip);
	/// `P` clipped to the half-plane left of `A`→`B`.
	AUTOCRAFT_API FAcPoly ClipHalf(const FAcPoly& P, FVector2D A, FVector2D B);
	/// The convex `Outer` without the convex `Hole` (an even-odd clip), in convex pieces.
	AUTOCRAFT_API FAcRegion Minus(const FAcPoly& Outer, const FAcPoly& Hole);
	/// `Region` clipped to the convex `Clip`.
	AUTOCRAFT_API FAcRegion Clip(const FAcRegion& Region, const FAcPoly& Clip);
	/// A convex polygon grown by `D` (shrunk when negative): rounded outside
	/// its corners, mitred inside. The same number of points for every `D`.
	AUTOCRAFT_API FAcPoly Grown(const FAcPoly& P, double D);
	/// A polyline moved `D` to its left (mitred joins), open or closed.
	AUTOCRAFT_API TArray<FVector2D> Offset(const TArray<FVector2D>& Line, double D, bool bClosed);
	/// The parts of a polyline inside `Region`, as unbroken runs.
	AUTOCRAFT_API TArray<TArray<FVector2D>> ClipLine(const TArray<FVector2D>& Line, const FAcRegion& Region);
	/// A quadratic curve as a polyline of `Steps` segments.
	AUTOCRAFT_API TArray<FVector2D> Quad(FVector2D A, FVector2D Control, FVector2D B, int32 Steps);
}

class AUTOCRAFT_API FAcArtList
{
public:
	/// `PixelsPerPoint`: the scale it will be drawn at (anti-aliasing is one pixel wide).
	explicit FAcArtList(double PixelsPerPoint = 2);

	double Scale() const { return PxPerPt; }
	bool IsEmpty() const { return Indices.IsEmpty(); }
	int32 NumVertices() const { return Verts.Num(); }

	// MARK: Shapes at this scale

	/// `CGPath(roundedRect:cornerWidth:cornerHeight:)`.
	FAcPoly RoundRect(const FAcRect& R, double Rw, double Rh) const;
	FAcPoly RoundRect(const FAcRect& R, double Radius) const { return RoundRect(R, Radius, Radius); }
	/// `CGPath(ellipseIn:)`.
	FAcPoly Ellipse(const FAcRect& R) const;

	// MARK: Drawing

	/// A filled shape. `bSmooth`: anti-aliased edges (off for pieces that
	/// meet other pieces, which would show a seam).
	void Fill(const FAcPoly& P, const FLinearColor& Color, bool bSmooth = true);
	void Fill(const FAcRegion& R, const FLinearColor& Color);
	/// `drawLinearGradient(_, start: A, end: B, options: [])` clipped to
	/// `Region`: nothing before `A` or past `B`.
	void Linear(const FAcRegion& Region, const FAcStops& Stops, FVector2D A, FVector2D B);
	/// `drawRadialGradient(_, startCenter: C, startRadius: 0, endCenter: C, endRadius: R, options: [])` clipped to `Region`.
	void Radial(const FAcRegion& Region, const FAcStops& Stops, FVector2D C, double R);
	/// A stroked polyline (butt ends, mitred joins). `bRound`: round ends.
	void Stroke(const TArray<FVector2D>& Line, bool bClosed, const FLinearColor& Color, double Width, bool bRound = false);
	/// The same, only where it is in `Clip`.
	void Stroke(const TArray<FVector2D>& Line, const FAcRegion& Clip, const FLinearColor& Color, double Width, bool bRound = false);

	// MARK: Shadows (`setShadow(offset:blur:color:)` under what follows; offset and blur in pixels)

	/// The shadow a filled shape throws (a Gaussian `Blur` wide, as Core Graphics').
	void Shadow(const FAcPoly& P, FVector2D Offset, double Blur, const FLinearColor& Color);
	/// The shadow a stroke `Width` wide along the closed `P` throws, offset 0
	/// (`Chrome.glowLine`'s glow).
	void StrokeShadow(const FAcPoly& P, double Width, double Blur, const FLinearColor& Color);
	/// The same along an open polyline (no glow past its ends).
	void LineShadow(const TArray<FVector2D>& Line, double Width, double Blur, const FLinearColor& Color);
	/// The shadow of a shape whose top edge is the open polyline `Edge`
	/// (left to right), above that edge: what a fill under `Edge` throws.
	void EdgeShadow(const TArray<FVector2D>& Edge, FVector2D Offset, double Blur, const FLinearColor& Color);
	/// The shadow everything round the convex `Hole` throws into it.
	void InnerShadow(const FAcPoly& Hole, FVector2D Offset, double Blur, const FLinearColor& Color);

	// MARK: Replay

	/// Draws the list with the white brush on one layer: Swift point `P` at
	/// Slate point (P.X + Offset.X, ViewHeight − (P.Y + Offset.Y)) in `Geometry`.
	/// Returns the next free layer.
	int32 Paint(FSlateWindowElementList& Out, const FGeometry& Geometry, int32 Layer, double ViewHeight,
		FVector2D Offset, float Opacity, ESlateDrawEffect Effects) const;

private:
	struct FV
	{
		FVector2D P;
		FLinearColor C;  // Swift components
	};
	using FVPoly = TArray<FV>;

	int32 Add(FVector2D P, const FLinearColor& C);
	void Fan(const FVPoly& P);
	/// Quads between two outlines with the same number of points.
	void Band(const FVPoly& A, const FVPoly& B, bool bClosed);
	static FVPoly ClipV(const FVPoly& P, const FAcPoly& Clip);
	/// `P`'s colour at every point from `Of`.
	template <typename F>
	static FVPoly Shade(const FAcPoly& P, F&& Of);
	/// A soft edge round a closed convex shape: rings at `Ds` (grown by each)
	/// with the colour `Of(D)`, the inside filled at the first.
	template <typename F>
	void Rings(const FAcPoly& P, const TArray<double>& Ds, F&& Of, bool bFillInside);
	/// The same along an open polyline, left of it.
	template <typename F>
	void OpenRings(const TArray<FVector2D>& Line, const TArray<double>& Ds, F&& Of);
	/// A blurred shape too small for rings: the box blur of its bounds.
	void BoxShadow(const FBox2D& Box, double Sigma, const FLinearColor& Color);
	double Feather() const { return 0.5 / PxPerPt; }

	double PxPerPt;
	TArray<FVector2f> Verts;
	TArray<FColor> Colors;
	TArray<uint32> Indices;
};
