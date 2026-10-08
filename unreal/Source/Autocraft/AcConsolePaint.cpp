#include "AcConsolePaint.h"

#include "AcBakedArt.h"
#include "AcHudStyle.h"

#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"

namespace
{
	/// A rounded rectangle's outline in Slate points, closed.
	TArray<FVector2f> RoundedOutline(const FVector2f Min, const FVector2f Max, float Radius)
	{
		Radius = FMath::Clamp(Radius, 0.0f, FMath::Min(Max.X - Min.X, Max.Y - Min.Y) / 2);
		TArray<FVector2f> P;
		if (Radius <= 0.01f)
		{
			P = {Min, FVector2f(Max.X, Min.Y), Max, FVector2f(Min.X, Max.Y), Min};
			return P;
		}
		const int32 Steps = 4;
		const FVector2f Centres[4] = {FVector2f(Max.X - Radius, Min.Y + Radius), FVector2f(Max.X - Radius, Max.Y - Radius),
			FVector2f(Min.X + Radius, Max.Y - Radius), FVector2f(Min.X + Radius, Min.Y + Radius)};
		// Corners clockwise in y-down: top right, bottom right, bottom left, top left.
		const float Start[4] = {-90, 0, 90, 180};
		for (int32 C = 0; C < 4; ++C)
		{
			for (int32 S = 0; S <= Steps; ++S)
			{
				const float A = FMath::DegreesToRadians(Start[C] + 90.0f * S / Steps);
				P.Add(Centres[C] + FVector2f(FMath::Cos(A), FMath::Sin(A)) * Radius);
			}
		}
		const FVector2f First = P[0];
		P.Add(First);
		return P;
	}

	double SrgbOf(float Linear)
	{
		return FAcHudStyle::HudSrgb(Linear);  // the inverse of FAcHudStyle::Srgb
	}
}

void FAcConsolePaint::Fill(const FAcRect& R, const FLinearColor& Color)
{
	if (R.IsEmpty() || Color.A <= 0) return;
	const FVector2D TopLeft(R.X, ViewHeight - R.MaxY());
	FSlateDrawElement::MakeBox(Out, Take(),
		Geometry.ToPaintGeometry(FVector2f(R.W, R.H), FSlateLayoutTransform(FVector2f(TopLeft))), FAcHudStyle::White(),
		ESlateDrawEffect::None, Color);
}

void FAcConsolePaint::FillRounded(const FAcRect& R, const double Radius, const FLinearColor& Color)
{
	if (R.IsEmpty() || Color.A <= 0) return;
	if (Radius <= 0.01 || !FSlateApplication::IsInitialized())
	{
		Fill(R, Color);
		return;
	}
	// A fan from the middle round the rounded outline, in the white brush.
	const FVector2f Min((float)R.X, (float)(ViewHeight - R.MaxY()));
	const FVector2f Max((float)R.MaxX(), (float)(ViewHeight - R.Y));
	const TArray<FVector2f> Outline = RoundedOutline(Min, Max, (float)Radius);
	const FSlateRenderTransform& T = Geometry.GetAccumulatedRenderTransform();
	const FColor C = Color.ToFColor(true);  // Slate packs vertex colours as sRGB
	// The white brush sits in Slate's atlas: sample the middle of its own region.
	const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FAcHudStyle::White());
	const FSlateShaderResourceProxy* Proxy = Handle.GetResourceProxy();
	const FVector2f Uv = Proxy ? Proxy->StartUV + Proxy->SizeUV * 0.5f : FVector2f(0.5f, 0.5f);
	TArray<FSlateVertex> V;
	TArray<SlateIndex> I;
	V.Reserve(Outline.Num() + 1);
	V.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, (Min + Max) / 2, Uv, C));
	for (const FVector2f& P : Outline) V.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, P, Uv, C));
	for (int32 K = 1; K + 1 < V.Num(); ++K)
	{
		I.Add(0);
		I.Add((SlateIndex)K);
		I.Add((SlateIndex)(K + 1));
	}
	FSlateDrawElement::MakeCustomVerts(Out, Take(), Handle, V, I, nullptr, 0, 0);
}

void FAcConsolePaint::Stroke(const FAcRect& R, const double Radius, const FLinearColor& Color, const double Width,
	const double Glow)
{
	if (Color.A <= 0) return;
	const FVector2f Min((float)R.X, (float)(ViewHeight - R.MaxY()));
	const FVector2f Max((float)R.MaxX(), (float)(ViewHeight - R.Y));
	const TArray<FVector2f> Points = RoundedOutline(Min, Max, (float)Radius);
	if (Glow > 0)
	{
		// SpriteKit's glow: a soft halo either side of the line, as three
		// bands (widest first), fitted to `windowshot` at 1600×1000. On a
		// warped face (drawn ≥ 2 pixels a point, e.g. the deck's voted thumb,
		// glowWidth 1.5) the halo reaches 0.73 of the colour 1 point from
		// the line, 0.38 at 2 and 0.07 at 3; drawn straight at 1 pixel a
		// point (the view switch's lit segment) Slate's anti-aliased edge
		// adds about a pixel, so the bands are narrower there.
		const bool bFine = Geometry.Scale >= 1.5f;
		static constexpr double FineReach[3] = {3.0, 1.9, 0.85}, CoarseReach[3] = {2.4, 1.5, 0.65};
		static constexpr float FineStrength[3] = {0.08f, 0.42f, 0.6f}, CoarseStrength[3] = {0.06f, 0.36f, 0.58f};
		const double* Reach = bFine ? FineReach : CoarseReach;
		const float* Strength = bFine ? FineStrength : CoarseStrength;
		for (int32 K = 0; K < 3; ++K)
		{
			FSlateDrawElement::MakeLines(Out, Take(), Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None,
				WithAlpha(Color, Color.A * Strength[K]), true, (float)(Width + 2 * Reach[K] * Glow));
		}
	}
	FSlateDrawElement::MakeLines(Out, Take(), Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, (float)Width);
}

void FAcConsolePaint::Image(const FSlateBrush* Brush, const FAcRect& R, const FLinearColor& Tint)
{
	if (!Brush || R.IsEmpty() || Tint.A <= 0) return;
	const FVector2D TopLeft(R.X, ViewHeight - R.MaxY());
	FSlateDrawElement::MakeBox(Out, Take(), Geometry.ToPaintGeometry(FVector2f(R.W, R.H), FSlateLayoutTransform(FVector2f(TopLeft))),
		Brush, ESlateDrawEffect::None, Tint);
}

void FAcConsolePaint::Baked(const FAcBakedArt& Art, const FAcRect& R, const float Opacity)
{
	const FSlateBrush* Brush = Art.Get();
	if (!Brush || R.IsEmpty() || Opacity <= 0) return;
	const FVector2D TopLeft(R.X, ViewHeight - R.MaxY());
	FSlateDrawElement::MakeBox(Out, Take(), Geometry.ToPaintGeometry(FVector2f(R.W, R.H), FSlateLayoutTransform(FVector2f(TopLeft))),
		Brush, Art.Effects(), Art.Tint(Opacity));
}

void FAcConsolePaint::Text(const FString& InText, const double Size, const FLinearColor& Color, const FVector2D At,
	const EAcHAlign Align, const bool bShadow, const bool bNames, const float Opacity)
{
	if (InText.IsEmpty()) return;
	FAcHudText T(InText, FAcHudStyle::Font((float)Size), Color, Align, EAcVAlign::Baseline);
	T.Accent = bNames ? FAcHudStyle::NameAccent() : FLinearColor::Transparent;
	if (bShadow)
	{
		T.ShadowOffset = FVector2f(1.2f, 1.2f);
		T.ShadowColor = FLinearColor(0, 0, 0, 0.75f);
	}
	T.Paint(Out, Take(), Geometry, FVector2f((float)At.X, (float)(ViewHeight - At.Y)), Opacity);
	++Layer;  // the shadow under it
}

void FAcConsolePaint::Text(const FAcHudText& Label, const FVector2D At, const float Opacity)
{
	Label.Paint(Out, Take(), Geometry, FVector2f((float)At.X, (float)(ViewHeight - At.Y)), Opacity);
	++Layer;
}

double FAcConsolePaint::TextWidth(const FString& InText, const double Size)
{
	return FAcHudText(InText, FAcHudStyle::Font((float)Size), FLinearColor::White).Measure().X;
}

FLinearColor FAcConsolePaint::BlendTint(const FLinearColor& Color, const double Factor, const double Alpha)
{
	// tex·(1−f) + tex·c·f = tex·(1 − f + f·c), on the sRGB components; a
	// product of sRGB values is (nearly) the product of linear ones, so the
	// tint can be converted on its own.
	auto Mix = [Factor](float L) { return 1 - Factor + Factor * SrgbOf(L); };
	return FAcHudStyle::Srgb(Mix(Color.R), Mix(Color.G), Mix(Color.B), Alpha);
}
