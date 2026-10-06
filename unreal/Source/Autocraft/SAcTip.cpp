#include "SAcTip.h"

#include "AcHudStyle.h"
#include "AcHudText.h"

#include "Rendering/DrawElements.h"

namespace
{
	constexpr float Pad = 8.0f;
	constexpr float BarH = 4.0f;

	/// `makeTip`'s tone (a little lighter than the world cue's).
	FLinearColor TipTone(const EAcHoverTone Tone)
	{
		switch (Tone)
		{
		case EAcHoverTone::Own: return FAcHudStyle::Srgb(0.35, 1, 0.45);
		case EAcHoverTone::Enemy: return FAcHudStyle::Srgb(1, 0.35, 0.3);
		default: return AAcPointerCues::UplinkColor();
		}
	}

	double Fraction(const FAcHoverTip& T)
	{
		return T.MaxHp > 0 ? FMath::Clamp(T.Hp / T.MaxHp, 0.0, 1.0) : 0.0;
	}

	/// The hp text's colour (and the bar's): the tone toward white when
	/// healthy, then AppKit's system yellow, then system red.
	FLinearColor HpColor(const FAcHoverTip& T)
	{
		const double F = Fraction(T);
		if (F > 0.6) return FAcHudStyle::Blend(TipTone(T.Tone), 0.3, FLinearColor::White);
		if (F > 0.3) return FAcHudStyle::Srgb(1.0, 0.8, 0.0);
		return FAcHudStyle::Srgb(1.0, 0.23, 0.19);
	}

	struct FLabels
	{
		FAcHudText Title, Hp, Detail;
	};

	FLabels Labels(const FAcHoverTip& T)
	{
		FLabels L;
		L.Title = FAcHudText(T.Title.ToUpper(), FAcHudStyle::Font(19), FLinearColor::White, EAcHAlign::Left, EAcVAlign::Top);
		L.Hp = FAcHudText(FString::Printf(TEXT("%d / %d"), (int32)FMath::CeilToDouble(T.Hp), (int32)T.MaxHp), FAcHudStyle::Font(15),
			HpColor(T), EAcHAlign::Right, EAcVAlign::Top);
		L.Hp.Accent = FLinearColor::Transparent;
		L.Detail = FAcHudText(T.Detail, FAcHudStyle::Font(15), TipTone(T.Tone), EAcHAlign::Left, EAcVAlign::Top);
		return L;
	}

	void PaintRect(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, FVector2f At, FVector2f Size, const FLinearColor& C)
	{
		if (Size.X <= 0 || Size.Y <= 0) return;
		FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2D(Size), FSlateLayoutTransform(FVector2D(At))),
			FAcHudStyle::White(), ESlateDrawEffect::None, C);
	}
}

void SAcTip::Construct(const FArguments& InArgs)
{
	SetCanTick(false);
	SetVisibility(EVisibility::HitTestInvisible);
}

void SAcTip::Show(const TOptional<FAcHoverTip>& InTip)
{
	Tip = InTip;
	Invalidate(EInvalidateWidgetReason::Paint);
}

FVector2f SAcTip::Size(const FAcHoverTip& T)
{
	const FLabels L = Labels(T);
	const bool bDetail = !T.Detail.IsEmpty();
	const float W = FMath::Max3(170.0f, L.Title.Measure().X + L.Hp.Measure().X + 26.0f, bDetail ? L.Detail.Measure().X + 2 * Pad : 0.0f);
	const float H = Pad + 19 + 5 + BarH + (bDetail ? 6 + 16 : 0) + Pad;
	return FVector2f(W, H);
}

int32 SAcTip::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	if (!Tip) return Layer;
	const FAcHoverTip& T = *Tip;
	const FLabels L = Labels(T);
	const FVector2f S = Size(T);
	const FVector2f View(Geometry.GetLocalSize());
	const FVector2f At(Geometry.AbsoluteToLocal(FVector2D(T.AbsoluteAt)));
	// Below right of the arrow (its top 20 points above the tip of the
	// arrow), kept on screen.
	FVector2f P(At.X + 70, At.Y - 20);
	if (P.X + S.X > View.X - 8) P.X = At.X - 70 - S.X;
	P.Y = FMath::Min(P.Y, View.Y - 70);

	const FLinearColor Tone = TipTone(T.Tone);
	// The back: dark glass, framed in the tone (square corners: Slate's
	// rounded box drew white on Metal, see SAcVictoryBanner).
	PaintRect(Out, Layer, Geometry, P, S, FAcHudStyle::Srgb(0.02, 0.05, 0.07, 0.9));
	const FLinearColor Frame = Tone.CopyWithNewOpacity(0.8f);
	PaintRect(Out, Layer + 1, Geometry, P, FVector2f(S.X, 1), Frame);
	PaintRect(Out, Layer + 1, Geometry, P + FVector2f(0, S.Y - 1), FVector2f(S.X, 1), Frame);
	PaintRect(Out, Layer + 1, Geometry, P, FVector2f(1, S.Y), Frame);
	PaintRect(Out, Layer + 1, Geometry, P + FVector2f(S.X - 1, 0), FVector2f(1, S.Y), Frame);
	// A lit edge down the left side, the panel accent.
	PaintRect(Out, Layer + 1, Geometry, P + FVector2f(2, 3), FVector2f(2, S.Y - 6), Tone);

	L.Title.Paint(Out, Layer + 2, Geometry, P + FVector2f(Pad + 2, Pad - 1));
	L.Hp.Paint(Out, Layer + 2, Geometry, P + FVector2f(S.X - Pad, Pad));
	const float BarY = Pad + 19 + 5;
	const float Track = S.X - 2 * Pad - 2;
	PaintRect(Out, Layer + 1, Geometry, P + FVector2f(Pad + 2, BarY), FVector2f(Track, BarH), FAcHudStyle::Srgb(0.15, 0.15, 0.15));
	PaintRect(Out, Layer + 2, Geometry, P + FVector2f(Pad + 2, BarY), FVector2f(Track * (float)Fraction(T), BarH), HpColor(T));
	if (!T.Detail.IsEmpty()) L.Detail.Paint(Out, Layer + 2, Geometry, P + FVector2f(Pad + 2, BarY + BarH + 6));
	return Layer + 3;
}
