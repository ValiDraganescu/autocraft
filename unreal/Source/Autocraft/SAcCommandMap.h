// `SAcCommandMap`: the command map (M) over the whole view (chunk D8,
// GAME-LAYER.md §2.3), the Slate port of the Swift `CommandMap`
// (Sources/Autocraft/CommandMap.swift): a dimmed backdrop, a chrome plate,
// the whole map large (D7's minimap at scale 1.8, no camera outline) with
// bright corner brackets, the base sites (ours green, enemy red, free dashed
// white, the AI's next glowing amber, an expansion asked for amber), every
// building's icon in its side's colour, the objectives with their squads'
// lines, the stance buttons along the top with the Prospector split and the
// bank floor, and down the right the objectives and then the queue of
// requests (funded bars, ✕ to take one off) and the squads' note; the order
// menu at a clicked point.
//
// Coordinates: everything is laid out as Swift lays it out, in points with
// y up from the view's bottom (`FAcConsolePaint`), over the widget's whole
// geometry (the HUD root's layer, in points). `Hit` answers in the same
// space. The layout is made again on every paint from the last `Show`, so a
// resize needs nothing but a new minimap (the subsystem makes one).
//
// Clicks are the widget's (the minimap child is hit-test invisible): a
// press and release on it reports `OnHit` (`CommandMap.hit`).
#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

#include "AcCab.h"
#include "AcCabArt.h"
#include "AcCommandMapLogic.h"

class SAcMinimap;
class UTexture2D;
struct FSlateBrush;

/// A click on the command map (`CommandMapHit`).
struct FAcCommandMapHit
{
	enum class EType : uint8
	{
		Stance,
		Order,
		/// The ✕ of an objective or a request.
		Cancel,
		/// On the map, here: open the order menu.
		Ground,
		/// On the backdrop, off the panel: close the map.
		Close,
		/// On the panel, on nothing.
		Inside,
		/// A PILOTS row's pick button (chunk E9): keep `Perk`.
		Pick,
	};
	EType Type = EType::Inside;
	ac::Stance Stance = ac::Stance::auto_;
	FAcMapOrder Order;
	int64 Id = 0;
	ac::Vec2 Ground;
	ac::Perk Perk = ac::Perk::prospectorQuickDrill;
};

DECLARE_DELEGATE_OneParam(FAcOnCommandMapHit, const FAcCommandMapHit&);

/// Chunk E9's PILOTS rows, drawn down the right column after the queue
/// (`CommandMap.pilots`): where they start and what they may use. Swift
/// points, y up. `Out` is null while the map is only hit-tested.
struct FAcCommandMapColumn
{
	FSlateWindowElementList* Out = nullptr;
	const FGeometry* Geometry = nullptr;
	/// The next free layer; the painter moves it on.
	int32 Layer = 0;
	/// The view's height (points).
	double ViewHeight = 0;
	/// The column's left edge (`cx`), right edge (`panel.maxX - 16`), and how low it may go.
	double X = 0, Right = 0, Bottom = 0;
	/// Swift's `y`, moved down past the rows.
	double Y = 0;
	TArray<TPair<FAcRect, FAcCommandMapHit>>* Hits = nullptr;
};

class AUTOCRAFT_API SAcCommandMap : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAcCommandMap) {}
		SLATE_EVENT(FAcOnCommandMapHit, OnHit)
	SLATE_END_ARGS()

	static constexpr double Header = 64;
	static constexpr double Column = 330;
	static constexpr double Margin = 24;

	/// The map area that fits a view of `Size` (for sizing the minimap).
	static FVector2D MapArea(FVector2D Size);

	void Construct(const FArguments& InArgs);

	/// The big map (a minimap made for this view's `MapArea`).
	void SetMinimap(const TSharedPtr<SAcMinimap>& Minimap);
	TSharedPtr<SAcMinimap> Minimap() const { return Map; }
	/// What to show.
	void Show(const FAcCommandMapInfo& Info);
	/// The view's size in points as last laid out (zero before).
	FVector2D ViewSize() const { return View; }
	/// Bakes the panel's plate for a view of `ViewSize` points on a worker
	/// thread (Core Graphics' shadow blur takes ~0.5 s at 2 px a point) and
	/// makes its texture once it is done, so opening the map does not stall.
	/// Called each frame while the map is shut; nothing to do once baked.
	void Prebake(FVector2D ViewSize) const;
	/// Chunk E9: draws the PILOTS rows where `FAcCommandMapInfo::PilotsRoom` is kept.
	void SetPilotsPainter(TFunction<void(FAcCommandMapColumn&)> In) { PilotsPainter = MoveTemp(In); }
	/// What is under a point (Swift points, y up) as last laid out.
	TOptional<FAcCommandMapHit> Hit(FVector2D P) const;
	/// A view point (points, y down) as Swift's (y up).
	FVector2D ToSwift(FVector2D ViewPoint) const { return FVector2D(ViewPoint.X, View.Y - ViewPoint.Y); }
	/// The view point (points, y down) over a ground position on the map.
	FVector2D ViewPointOf(ac::Vec2 Ground) const;

	// SWidget
	virtual void Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
		FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override { return FReply::Handled(); }
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		return OnMouseButtonDown(Geometry, Event);
	}

private:
	struct FPainter;
	/// Lay the map out over a view of `Size` and, with a painter, draw it;
	/// fills `Hits` in drawing order (`CommandMap.show`).
	void Build(FVector2D Size, FPainter* P, TArray<TPair<FAcRect, FAcCommandMapHit>>& OutHits) const;
	/// The panel and the map's rectangle (Swift points, y up).
	FAcRect Panel(FVector2D Size) const;
	FAcRect MapRect(FVector2D Size) const;
	/// A map point (Swift points, y up, relative to the map's corner) over a ground position.
	FVector2D MapPoint(ac::Vec2 G) const;
	void EnsurePlate(FVector2D PanelSize) const;

	FAcOnCommandMapHit OnHit;
	TFunction<void(FAcCommandMapColumn&)> PilotsPainter;
	TSharedPtr<SAcMinimap> Map;
	FAcCommandMapInfo Shown;
	FVector2D View = FVector2D::ZeroVector;
	mutable TArray<TPair<FAcRect, FAcCommandMapHit>> Hits;
	bool bPressed = false;

	mutable TStrongObjectPtr<UTexture2D> PlateTexture;
	mutable TSharedPtr<FSlateBrush> PlateBrush;
	mutable FVector2D PlateFor = FVector2D::ZeroVector;
	/// The plate being baked (for a panel of `BakingFor` points).
	mutable TFuture<FAcArtImage> Baking;
	mutable FVector2D BakingFor = FVector2D::ZeroVector;
};
