// `SAcVictoryBanner`: "BLUE VICTORY" across the view once a game is won
// (Sources/Autocraft/HUD.swift:164-181, :410): a 640×96 dark plate centred
// at 38% from the top, the title 64 pt in the winner's colour (35% toward
// white), fading in over 0.8 s; up through the 30 s victory lap. With teams
// it names the winning team by its lowest player still standing
// (`GameState.winner`): "BLUE TEAM VICTORY".
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

namespace ac { struct GameState; }

class AUTOCRAFT_API SAcVictoryBanner : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAcVictoryBanner) {}
	SLATE_END_ARGS()

	static constexpr double FadeSeconds = 0.8;

	void Construct(const FArguments& InArgs);
	/// `Now`: real seconds (the fade's clock).
	void Update(const ac::GameState& State, double Now);
	/// The banner's title, empty when none.
	const FString& GetTitle() const { return Title; }

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(0, 0); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
		FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
	TOptional<int64> Winner;
	FString Title;
	FLinearColor Color;
	double ShownAt = 0.0;
	double Clock = 0.0;
};
