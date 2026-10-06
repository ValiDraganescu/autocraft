#include "SAcPopups.h"

#include "AcHudStyle.h"
#include "AcHudText.h"

void SAcPopups::Construct(const FArguments& InArgs)
{
	SetCanTick(false);
	SetVisibility(EVisibility::HitTestInvisible);
}

void SAcPopups::Add(const int64 Amount, const FVector2f At, const double Now)
{
	if (AddedThisFrame >= MaxPerFrame) return;
	++AddedThisFrame;
	Live.Add({Amount, At, Now});
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SAcPopups::Update(const double Now)
{
	Clock = Now;
	AddedThisFrame = 0;
	Live.RemoveAll([Now](const FPopup& P) { return Now - P.Born >= Life; });
	if (!Live.IsEmpty()) Invalidate(EInvalidateWidgetReason::Paint);
}

int32 SAcPopups::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
	FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const FSlateFontInfo Font = FAcHudStyle::Font(20);
	for (const FPopup& P : Live)
	{
		const double Age = FMath::Max(0.0, Clock - P.Born);
		const float Alpha = Age < FadeAfter ? 1.0f : (float)FMath::Clamp(1.0 - (Age - FadeAfter) / (Life - FadeAfter), 0.0, 1.0);
		const float Y = P.At.Y - Rise * (float)FMath::Min(Age / Life, 1.0);
		FAcHudText T(FString::Printf(TEXT("+%lld"), (long long)P.Amount), Font, FAcHudStyle::Deposit(), EAcHAlign::Center,
			EAcVAlign::Baseline);
		T.Accent = FLinearColor::Transparent;
		T.ShadowOffset = FVector2f(1.5f, 1.5f);
		T.ShadowColor = FLinearColor(0, 0, 0, 0.7f);
		T.Paint(Out, Layer, Geometry, FVector2f(P.At.X, Y), Alpha);
	}
	return Layer + 2;
}
