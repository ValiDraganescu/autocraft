#include "SAcResourceBar.h"

#include "AcHudStyle.h"
#include "AcHudText.h"

void SAcResourceBar::Construct(const FArguments& InArgs)
{
	SetCanTick(false);
	SetVisibility(EVisibility::HitTestInvisible);
}

void SAcResourceBar::Update(const int64 Ore, const int64 InHydrogen, const int64 InSupplyUsed, const int64 InSupplyCap,
	const double RealDelta)
{
	TargetOre = Ore;
	Hydrogen = InHydrogen;
	SupplyUsed = InSupplyUsed;
	SupplyCap = InSupplyCap;
	if (ShownOre < 0)
	{
		ShownOre = Ore;
		Pending = 0.0;
	}
	else
	{
		// Swift steps once per HUD update at 60 Hz: the same here, whatever
		// the frame rate (at most 6 steps a frame).
		Pending = FMath::Min(Pending + RealDelta, 0.1);
		while (Pending >= 1.0 / 60.0 && ShownOre != TargetOre)
		{
			Pending -= 1.0 / 60.0;
			ShownOre = ShownOre + FMath::Max<int64>(1, (TargetOre - ShownOre) / 3);
			ShownOre = FMath::Min(ShownOre, TargetOre);
		}
		if (ShownOre == TargetOre) Pending = 0.0;
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 SAcResourceBar::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
	FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	FAcHudStyle::PaintPlate(Out, Layer, Geometry, EAcPlate::Bar, FSlateRect(0, 0, Width, Height));

	// HUD.buildBar: icons centred at x 28, 136, 238; labels from x 44, 152,
	// 256, centred 1 point under the middle.
	const float Cy = Height / 2;
	auto Icon = [&](const FSlateBrush* Brush, float X)
	{
		FSlateDrawElement::MakeBox(Out, Layer + 1,
			Geometry.ToPaintGeometry(FVector2D(24, 24), FSlateLayoutTransform(FVector2D(X - 12, Cy - 12))), Brush);
	};
	Icon(FAcHudStyle::OreIcon(), 28);
	Icon(FAcHudStyle::HydrogenIcon(), 136);
	Icon(FAcHudStyle::SupplyIcon(), 238);

	const FSlateFontInfo Font = FAcHudStyle::Font(24);
	auto Label = [&](const FString& Text, float X)
	{
		FAcHudText(Text, Font, FAcHudStyle::Text(), EAcHAlign::Left, EAcVAlign::Center)
			.Paint(Out, Layer + 1, Geometry, FVector2f(X, Cy + 1));
	};
	Label(FString::Printf(TEXT("%lld"), (long long)FMath::Max<int64>(ShownOre, 0)), 44);
	Label(FString::Printf(TEXT("%lld"), (long long)Hydrogen), 152);
	Label(FString::Printf(TEXT("%lld/%lld"), (long long)SupplyUsed, (long long)SupplyCap), 256);
	return Layer + 3;
}
