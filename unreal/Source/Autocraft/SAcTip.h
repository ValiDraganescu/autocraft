// `SAcTip`: the tip beside the pointer (GAME-LAYER.md §2.3, chunk D4), the
// port of `HUD.applyTip` and `makeTip` (Sources/Autocraft/HUD.swift:502):
// the name in capitals ("Enemy " before another side's), hp / max, a 4-point
// health bar, and what a click does, framed in the side's colour with a lit
// edge down the left. Placed 70 points right of the pointer with its top 20
// points above it, flipped to the left when it would leave the view, and
// never lower than 70 points from the bottom.
//
// It fills its layer (`AcHudLayer::Tip`) and is hit-test invisible; the
// pointer arrives in absolute (Slate desktop) coordinates, so it lands right
// whatever the HUD's point scale.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

#include "AcPointerCues.h"

/// `HoverTip`.
struct FAcHoverTip
{
	FString Title;
	/// What a click does, if anything.
	FString Detail;
	double Hp = 0.0;
	double MaxHp = 0.0;
	EAcHoverTone Tone = EAcHoverTone::Own;
	/// The pointer, absolute (desktop) Slate coordinates.
	FVector2f AbsoluteAt = FVector2f::ZeroVector;
};

class AUTOCRAFT_API SAcTip : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAcTip) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	/// Show `Tip` (unset hides it).
	void Show(const TOptional<FAcHoverTip>& Tip);
	bool IsShown() const { return Tip.IsSet(); }

	/// The tip's size in points for `Tip` (`makeTip`'s w and h).
	static FVector2f Size(const FAcHoverTip& Tip);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(0, 0); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
		FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
	TOptional<FAcHoverTip> Tip;
};
