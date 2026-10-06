// `SAcResourceBar`: ore, Metallic Hydrogen and supply on a 340×42 plate, top
// right (`HUD.buildBar`, Sources/Autocraft/HUD.swift:91). The ore count
// climbs toward the stockpile by max(1, Δ/3) per update, 60 times a second,
// so a deposit reads as a running counter (HUD.swift:398-403).
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class AUTOCRAFT_API SAcResourceBar : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAcResourceBar) {}
	SLATE_END_ARGS()

	/// The plate's size in points.
	static constexpr float Width = 340.0f;
	static constexpr float Height = 42.0f;

	void Construct(const FArguments& InArgs);

	/// The local player's stock. `RealDelta` (seconds) paces the ore count-up.
	void Update(int64 Ore, int64 Hydrogen, int64 SupplyUsed, int64 SupplyCap, double RealDelta);
	/// Show the stock at once (a new game).
	void Reset() { ShownOre = -1; }

	int64 GetShownOre() const { return ShownOre; }

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Width, Height); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
		FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
	int64 ShownOre = -1;
	int64 TargetOre = 0;
	int64 Hydrogen = 0;
	int64 SupplyUsed = 0;
	int64 SupplyCap = 0;
	/// Real time not yet spent on count-up steps.
	double Pending = 0.0;
};
