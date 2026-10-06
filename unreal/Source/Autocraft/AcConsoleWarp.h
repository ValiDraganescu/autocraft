// The console's warp (chunk D3; GAME-LAYER.md §2.4 "Console"): the Swift
// `Console.warpFaces` (Sources/Autocraft/Console.swift:587). Each of the
// dashboard's three faces is drawn flat (its art and its screen's live
// parts) and then warped into the face as seen, with a 12 × 6 grid as
// Swift's `SKWarpGeometryGrid`.
//
// Here the three faces laid out flat make one strip, the console's width
// by `FAcCabLayout::FaceTop` (Swift points 0…W × 0…FaceTop, the faces side
// by side): `SAcConsoleFaces` paints it (the face art, the selection, the
// card, and the minimap's and music player's widgets as children), and
// `FAcConsoleWarp` renders it into a render target with `FWidgetRenderer`
// every frame, then draws each face's part of it into the face's quad with
// `FSlateDrawElement::MakeCustomVerts`. The target is drawn in gamma space
// and put on screen with `NoGamma`, so it blends as the rest of the HUD
// (and as SpriteKit) does.
//
// Input: the strip is never on screen, so nothing in it is hit-tested by
// Slate; `SAcConsole` maps the pointer back through the face's inverse
// projection (`FAcCabLayout::FaceAt`) and hands mouse events to the minimap
// and music player widgets itself (see SAcConsole.h).
#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

#include "AcCab.h"

class FWidgetRenderer;
class FHittestGrid;
class SVirtualWindow;
class UTextureRenderTarget2D;
struct FSlateBrush;

/// The dashboard's faces laid out flat (never on screen: `FAcConsoleWarp`
/// renders it). `OnPaintFlat` paints under the children, in this widget's
/// geometry (Slate points, y down from the strip's top).
class AUTOCRAFT_API SAcConsoleFaces : public SCompoundWidget
{
public:
	using FPaintFlat = TFunction<int32(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer)>;

	SLATE_BEGIN_ARGS(SAcConsoleFaces) {}
		SLATE_ARGUMENT(FPaintFlat, OnPaintFlat)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetSize(FVector2D InSize) { Size = InSize; }

	virtual FVector2D ComputeDesiredSize(float) const override { return Size; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
		FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
	FPaintFlat PaintFlat;
	FVector2D Size = FVector2D(100, 100);
};

/// Renders the flat strip into a target and draws its faces warped.
class AUTOCRAFT_API FAcConsoleWarp
{
public:
	/// Grid of the warp, as Swift's (`columns`, `rows`).
	static constexpr int32 Columns = 12;
	static constexpr int32 Rows = 6;

	explicit FAcConsoleWarp(const TSharedRef<SWidget>& Strip);
	~FAcConsoleWarp();

	/// Draw the strip (`Points` in Swift points) into the target at
	/// `PixelsPerPoint`. Call it once a frame before `Paint` (from a paint:
	/// the target is drawn on the render thread before the window is).
	void Render(FVector2D Points, float PixelsPerPoint, float DeltaTime);
	/// Each face's part of the strip in its quad as seen, in the console's
	/// geometry (`Layout` in that geometry's points), on one layer.
	void Paint(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FAcCabLayout& Layout) const;
	bool IsReady() const { return Target.IsValid(); }

private:
	TSharedRef<SWidget> Strip;
	TUniquePtr<FWidgetRenderer> Renderer;
	TSharedPtr<SVirtualWindow> Window;
	TUniquePtr<FHittestGrid> Grid;
	TStrongObjectPtr<UTextureRenderTarget2D> Target;
	TSharedPtr<FSlateBrush> Brush;
	FIntPoint Pixels = FIntPoint::ZeroValue;
	FVector2D Points = FVector2D::ZeroVector;
	float Scale = 1;
};
