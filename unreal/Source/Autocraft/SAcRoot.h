// `SAcRoot`: the HUD's root widget (GAME-LAYER.md §3.2), the Slate
// counterpart of the Swift `HUD` scene (Sources/Autocraft/HUD.swift).
// `AAcHUD` puts it over the game viewport and feeds it every frame.
//
// Everything inside lays out in Swift points: the root scales by
// `PixelsPerPoint` (2 on a Retina Mac) and undoes the engine's own DPI
// curve, so 340 points of resource bar are 680 pixels whatever the window.
//
// Children (D1): the resource bar (top right), the scoreboard left of it,
// the victory banner, deposit pop-ups. Later chunks add theirs with
// `AddLayer` (console D2, tip D4, perf panel D10, command map D8, pilot
// overlay E6, pick cards E9...).
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"

class SAcResourceBar;
class SAcScoreboard;
class SAcVictoryBanner;
class SAcPopups;
struct FAcFrame;
namespace ac { struct GameState; }

/// Z orders of the root's layers (higher on top). Leave gaps for later chunks.
namespace AcHudLayer
{
	constexpr int32 ViewFrame = 0;
	constexpr int32 Popups = 10;
	constexpr int32 Console = 20;
	constexpr int32 Bar = 30;
	constexpr int32 Banner = 40;
	constexpr int32 CommandMap = 60;
	constexpr int32 Tip = 90;
}

class AUTOCRAFT_API SAcRoot : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAcRoot) : _PixelsPerPoint(2.0f), _EngineScale(1.0f) {}
		/// Pixels per Swift point (the screen's backing scale).
		SLATE_ATTRIBUTE(float, PixelsPerPoint)
		/// The scale the engine already applies to viewport widgets (its DPI
		/// curve), divided out.
		SLATE_ATTRIBUTE(float, EngineScale)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/// One frame: the local player's stock, the scoreboard, the banner, the
	/// pop-ups' clock. `Now`: real seconds.
	void Update(const ac::GameState& State, int64 LocalPlayer, double RealDelta, double Now);
	/// A deposit pop-up at `At` (points, y down, in the root).
	void Popup(int64 Amount, FVector2f At, double Now);
	/// A new game: the counters show the new stock at once.
	void Reset();
	/// While driving, only the resources stay up (`HUD.applyPilot`).
	void SetDriving(bool bDriving);

	/// Add a widget over the view at `ZOrder` (`AcHudLayer`), laid out in points.
	SOverlay::FScopedWidgetSlotArguments AddLayer(int32 ZOrder);
	void RemoveLayer(const TSharedRef<SWidget>& Widget);

	float PixelsPerPoint() const { return PixelsPerPointAttr.Get(); }
	/// The layers' size in points as last laid out (zero before).
	FVector2D PointsSize() const { return Layers ? FVector2D(Layers->GetCachedGeometry().GetLocalSize()) : FVector2D::ZeroVector; }

	TSharedPtr<SAcResourceBar> Bar;
	TSharedPtr<SAcScoreboard> Scoreboard;
	TSharedPtr<SAcVictoryBanner> Banner;
	TSharedPtr<SAcPopups> Popups;

private:
	TAttribute<float> PixelsPerPointAttr;
	TAttribute<float> EngineScaleAttr;
	TSharedPtr<SOverlay> Layers;
};
