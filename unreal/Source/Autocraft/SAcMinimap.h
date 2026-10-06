// `SAcMinimap`: the minimap (chunk D7, GAME-LAYER.md §2.5), the Slate
// counterpart of the Swift `Minimap` (Sources/Autocraft/Minimap.swift): the
// map from above (a terrain picture baked once, `AcMinimapBake::Terrain`),
// the fog of war over it, the ore deposits and Metallic Hydrogen wells, every
// player's buildings (squares, 45 % while building) and units (dots sized by
// bulk, flyers diamonds) in the 8 player colours lifted 35 % toward white,
// the ground the camera shows as a white outline, and while driving a sight
// wedge. A press or a drag on it reports the ground under the pointer
// (`OnGround`; `UAcMinimapSubsystem` moves the RTS camera there).
//
// Coordinates: the widget draws in its own points (`Size`, Swift's
// `minimap.size`, y down, the far edge (min sim y) at the top), scaled to
// whatever its slot gives it (the console scales it as `minimap.setScale`).
// `Scale` is Swift's `s` (marks' sizes and line widths; 1 on the console,
// 1.8 on the command map). The dark frame round it reaches 6·s points out.
//
// It holds no game state of its own: `UAcMinimapSubsystem` feeds every
// minimap it made (`SetMarks`, `SetFootprint`, `SetFog`, `SetSight`) each
// frame. Hundreds of marks a frame are cheap in Slate.
#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/SLeafWidget.h"

#include "Projection.h"
#include "SimdMath.h"

#include <vector>

class UTexture2D;
struct FSlateBrush;

namespace ac
{
	struct GameState;
	struct MapDefinition;
}

DECLARE_DELEGATE_OneParam(FAcOnMinimapGround, ac::Vec2 /* ground under the pointer */);

/// One mark on the minimap, in the minimap's own points (y down).
struct FAcMinimapMark
{
	enum class EShape : uint8 { Square, Disc, Diamond };
	FVector2D At = FVector2D::ZeroVector;
	/// Half the side (square) or the radius (disc, diamond), points.
	double Radius = 1;
	EShape Shape = EShape::Disc;
	FLinearColor Fill = FLinearColor::White;
	/// The outline (`strokeColor`), and its width in points (0: none).
	FLinearColor Stroke = FLinearColor::Transparent;
	double Line = 0;
	/// The node's alpha (a building going up: 0.45).
	float Alpha = 1;
	/// SpriteKit's global z: 0 buildings and resources, 1 ground units, 2 flyers.
	uint8 Z = 0;
};

class AUTOCRAFT_API SAcMinimap : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAcMinimap) : _Scale(1.0), _ShowFootprint(true) {}
		/// Swift's `s`: marks and lines scale with it.
		SLATE_ARGUMENT(double, Scale)
		/// Draw the camera's outline (the command map's minimap does not).
		SLATE_ARGUMENT(bool, ShowFootprint)
		SLATE_EVENT(FAcOnMinimapGround, OnGround)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/// The ground it shows (the map's bounds), its own size in points, and
	/// the terrain picture over exactly that ground.
	void SetMap(const ac::GroundRect& Bounds, FVector2D Size, UTexture2D* Terrain);
	/// The marks from `Shown` (the game as the local team sees it).
	void SetMarks(const ac::GameState& Shown, const ac::MapDefinition& Map);
	/// The camera's ground quad (empty: none, e.g. while driving).
	void SetFootprint(const std::vector<ac::Vec2>& Quad);
	/// The fog picture over the ground from `Origin`, `FogSize` cells (row 0
	/// at min y); null hides it.
	void SetFog(UTexture2D* Fog, ac::Vec2 Origin, ac::Vec2 FogSize);
	/// Driving: the sight wedge from `At` toward `Heading` (world radians, as
	/// on the compass); unset hides it.
	void SetSight(TOptional<TPair<ac::Vec2, double>> Sight);

	const ac::GroundRect& Bounds() const { return GroundBounds; }
	FVector2D Size() const { return MapSize; }
	double Scale() const { return S; }
	/// The minimap point (own points, y down) over a ground position, and back.
	FVector2D Point(ac::Vec2 G) const;
	ac::Vec2 Ground(FVector2D P) const;
	/// A point in this widget's local geometry in the minimap's own points.
	FVector2D ToOwn(const FGeometry& Geometry, FVector2D Local) const;
	/// The ground under a local point no more than `Pad` own points off the
	/// map (clamped onto it), as Swift's `Console.minimapPoint`.
	TOptional<ac::Vec2> GroundAt(const FGeometry& Geometry, FVector2D Local, double Pad = 6) const;

	/// Report a press at a local point as `OnGround` would (scripted clicks).
	bool Press(const FGeometry& Geometry, FVector2D Local);
	/// The geometry it was last painted with (for scripted clicks).
	const FGeometry& LastGeometry() const { return PaintedGeometry; }

	// SWidget
	virtual FVector2D ComputeDesiredSize(float) const override { return MapSize; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
		FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& Event) override;

private:
	struct FImage
	{
		TStrongObjectPtr<UTexture2D> Texture;
		TSharedPtr<FSlateBrush> Brush;
	};
	void MakeImage(FImage& Image, const struct FAcArtImage& Art, const TCHAR* Name);
	void SetBrushTexture(FSlateBrush& Brush, UTexture2D* Texture);
	void PaintMark(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Own, const FAcMinimapMark& M) const;
	/// A player's colour on the minimap (`team(_:)`).
	static FLinearColor Team(int64 Player);

	FAcOnMinimapGround OnGround;
	double S = 1;
	bool bShowFootprint = true;

	ac::GroundRect GroundBounds;
	FVector2D MapSize = FVector2D(220, 170);

	TWeakObjectPtr<UTexture2D> TerrainTexture;
	TSharedPtr<FSlateBrush> TerrainBrush;
	TWeakObjectPtr<UTexture2D> FogTexture;
	TSharedPtr<FSlateBrush> FogBrush;
	ac::Vec2 FogOrigin, FogSize;
	TOptional<TPair<ac::Vec2, double>> Sight;
	FImage SightImage;

	FImage DiscImage, DiamondImage;

	TArray<FAcMinimapMark> Marks;
	TArray<FVector2f> Outline;

	bool bPressed = false;
	mutable FGeometry PaintedGeometry;
};
