// `SAcPopups`: "+5" floating up from a deposit (`HUD.applyPopup`,
// Sources/Autocraft/HUD.swift:589; trigger GameController.swift:335): 20 pt,
// light blue with a dark shadow, it rises 34 points over 1.3 s and fades out
// over the last 0.7 s. At most 8 new ones a frame. Only deposits the local
// team sees reach it (the frame's events are fog-gated).
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class AUTOCRAFT_API SAcPopups : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAcPopups) {}
	SLATE_END_ARGS()

	static constexpr double Life = 1.3;
	static constexpr double FadeAfter = 0.6;
	static constexpr float Rise = 34.0f;
	static constexpr int32 MaxPerFrame = 8;

	void Construct(const FArguments& InArgs);
	/// A deposit of `Amount` at `At` (points in this widget, y down); `Now`: real seconds.
	void Add(int64 Amount, FVector2f At, double Now);
	/// Drop the finished ones and start a new frame's count.
	void Update(double Now);
	int32 Num() const { return Live.Num(); }

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(0, 0); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
		FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
	struct FPopup
	{
		int64 Amount;
		FVector2f At;
		double Born;
	};
	TArray<FPopup> Live;
	double Clock = 0.0;
	int32 AddedThisFrame = 0;
};
