#include "SAcVictoryBanner.h"

#include "AcHudStyle.h"
#include "AcHudText.h"

#include "HAL/IConsoleManager.h"

#include "Types.h"

static TAutoConsoleVariable<int32> CVarAcHudWinner(
	TEXT("ac.HudWinner"), -1,
	TEXT("Show the victory banner for this player as if it had won (-1: the game's own winner). For shots."));

void SAcVictoryBanner::Construct(const FArguments& InArgs)
{
	SetCanTick(false);
	SetVisibility(EVisibility::HitTestInvisible);
}

void SAcVictoryBanner::Update(const ac::GameState& State, const double Now)
{
	Clock = Now;
	TOptional<int64> W = State.winner ? TOptional<int64>(*State.winner) : TOptional<int64>();
	if (const int32 Forced = CVarAcHudWinner.GetValueOnGameThread(); Forced >= 0) W = Forced;
	if (W != Winner)
	{
		Winner = W;
		ShownAt = Now;
		if (W)
		{
			bool bTeam = false;
			for (int64 P = 0; P < (int64)State.players.size(); ++P)
			{
				if (P != *W && State.allied(P, *W)) bTeam = true;
			}
			Title = FAcHudStyle::PlayerName(*W).ToUpper() + (bTeam ? TEXT(" TEAM VICTORY") : TEXT(" VICTORY"));
			Color = FAcHudStyle::PlayerTextColor(*W, 0.35);
		}
		else
		{
			Title.Reset();
		}
	}
	if (Winner) Invalidate(EInvalidateWidgetReason::Paint);
}

int32 SAcVictoryBanner::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
	FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	if (!Winner || Title.IsEmpty()) return Layer;
	const float Alpha = (float)FMath::Clamp((Clock - ShownAt) / FadeSeconds, 0.0, 1.0);
	if (Alpha <= 0) return Layer;
	const FVector2f Size(Geometry.GetLocalSize());
	// Centred at 62% up (SpriteKit, y up): 38% down.
	const FVector2f C(Size.X / 2, Size.Y * 0.38f);
	FAcHudText T(Title, FAcHudStyle::Font(64), Color, EAcHAlign::Center, EAcVAlign::Center);
	T.Accent = FLinearColor::Transparent;
	const float W = FMath::Max(640.0f, T.Measure().X + 80), H = 96;
	// A dark plate with a faint white rim (Swift: a 6-point rounded rect;
	// Slate's rounded-box shader drew it white on Metal, so square here).
	const FSlateBrush* White = FAcHudStyle::White();
	const FVector2f TL(C.X - W / 2, C.Y - H / 2);
	FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(FVector2D(W, H), FSlateLayoutTransform(FVector2D(TL))),
		White, ESlateDrawEffect::None, FAcHudStyle::Srgb(0.02, 0.04, 0.07, 0.78 * Alpha));
	const float E = 1.5f;
	const FLinearColor Rim(1, 1, 1, 0.35f * Alpha);
	const FVector4f Edges[] = {FVector4f(0, 0, W, E), FVector4f(0, H - E, W, E), FVector4f(0, E, E, H - 2 * E), FVector4f(W - E, E, E, H - 2 * E)};
	for (const FVector4f& R : Edges)
	{
		FSlateDrawElement::MakeBox(Out, Layer + 1,
			Geometry.ToPaintGeometry(FVector2D(R.Z, R.W), FSlateLayoutTransform(FVector2D(TL.X + R.X, TL.Y + R.Y))), White,
			ESlateDrawEffect::None, Rim);
	}
	T.Paint(Out, Layer + 2, Geometry, FVector2f(C.X, C.Y + 2), Alpha);
	return Layer + 4;
}
