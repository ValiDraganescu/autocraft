// See SAcMusicDeck.h. Swift: Sources/Autocraft/MusicDeck.swift.
#include "SAcMusicDeck.h"

#include "AcConsolePaint.h"
#include "AcHudStyle.h"
#include "AcHudText.h"
#include "AcIcons.h"

#include "Engine/Texture2D.h"
#include "Styling/SlateBrush.h"

namespace
{
	using FPaint = FAcConsolePaint;

	FLinearColor Rgb(double R, double G, double B, double A = 1) { return FAcHudStyle::Srgb(R, G, B, A); }
	FLinearColor Gray(double W, double A = 1) { return FAcHudStyle::Srgb(W, W, W, A); }
	FLinearColor Alpha(const FLinearColor& C, double A) { return FPaint::WithAlpha(C, A); }

	// MusicDeck.upColor, downColor, adColor; Chrome.cyan, Chrome.soft.
	FLinearColor UpColor() { return Rgb(0.4, 1, 0.5); }
	FLinearColor DownColor() { return Rgb(1, 0.42, 0.3); }
	FLinearColor AdColor() { return Rgb(1, 0.76, 0.3); }
	FLinearColor Cyan() { return FAcHudStyle::Cyan(); }
	FLinearColor Soft() { return FAcHudStyle::Soft(); }

	/// The labels of the deck: HUD.font, no shadow, no name highlighting.
	void Label(FPaint& P, const FString& Text, double Size, const FLinearColor& Color, FVector2D At, EAcHAlign Align,
		float Opacity = 1)
	{
		P.Text(Text, Size, Color, At, Align, false, false, Opacity);
	}

	/// Text the Swift station card has drawn in (`MusicPlayer.stationCover`):
	/// centred on the 256-point card, its line's foot at `Y`, scaled by `K`.
	void CardText(FPaint& P, const FAcRect& Box, double K, const FString& Text, double Size, double Y, const FLinearColor& Color,
		double Kern, float Opacity)
	{
		FSlateFontInfo Font = FAcHudStyle::Font((float)(Size * K));
		FAcHudText T(Text, Font, Color);
		T.Accent = FLinearColor::Transparent;
		T.Kern = (float)(Kern * K);
		const double W = T.Measure().X;
		// NSString.draw(at:) puts the line's foot at the point: the baseline
		// is a descender (about 0.2 em) higher.
		P.Text(T, FVector2D(Box.X + (256 * K - W) / 2, Box.Y + (Y + 0.2 * Size) * K), Opacity);
	}
}

void SAcMusicDeck::Construct(const FArguments& InArgs)
{
	OnCommand = InArgs._OnCommand;
	SetVisibility(EVisibility::Visible);
	Cover = MakeShared<FSlateBrush>();
	Cover->DrawAs = ESlateBrushDrawType::Image;
}

// MARK: - State

void SAcMusicDeck::Show(const TOptional<FAcNowPlaying>& InNp, const double InNow)
{
	Np = InNp;
	Now = InNow;
	ShownSecond = -1;
	if (!Np) HoveredCommand.Reset();
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SAcMusicDeck::SetNow(const double InNow)
{
	Now = InNow;
	if (!Np) return;
	// MusicDeck.tick: redrawn when the second changes.
	const int32 Second = (int32)Np->PositionAt(Now);
	if (Second == ShownSecond) return;
	ShownSecond = Second;
	Invalidate(EInvalidateWidgetReason::Paint);
}

FString SAcMusicDeck::Clock(const double Seconds)
{
	const int32 S = FMath::Max(0, (int32)Seconds);
	return FString::Printf(TEXT("%d:%02d"), S / 60, S % 60);
}

FString SAcMusicDeck::SymbolName(const EAcMusicCommand C, const bool bVoted, const bool bPaused)
{
	switch (C)
	{
	case EAcMusicCommand::Down: return bVoted ? TEXT("hand.thumbsdown.fill") : TEXT("hand.thumbsdown");
	case EAcMusicCommand::Up: return bVoted ? TEXT("hand.thumbsup.fill") : TEXT("hand.thumbsup");
	case EAcMusicCommand::Previous: return TEXT("backward.end.fill");
	case EAcMusicCommand::Next: return TEXT("forward.end.fill");
	case EAcMusicCommand::Toggle: return bPaused ? TEXT("play.fill") : TEXT("pause.fill");
	}
	return {};
}

TPair<FString, double> SAcMusicDeck::Fit(const FString& Text, const double Size, const double Room)
{
	const double W = FPaint::TextWidth(Text, Size);
	if (W <= Room) return {Text, Size};
	const double S = Size * FMath::Max(Room / W, 0.7);
	FString Cut = Text, Shown = Text;
	while (FPaint::TextWidth(Shown, S) > Room && Cut.Len() > 1)
	{
		Cut.LeftChopInline(1);
		Shown = Cut.TrimStartAndEnd() + TEXT("…");
	}
	return {Shown, S};
}

TArray<FAcDeckTipLine> SAcMusicDeck::Tip(const EAcMusicCommand C) const
{
	// Swift writes "Key  <media key glyph> (F7)": Barlow has no such glyphs, so the key alone.
	const FLinearColor White = FLinearColor::White, Key = FAcHudStyle::KeyYellow(), Rest = Gray(0.8);
	switch (C)
	{
	case EAcMusicCommand::Down:
		return {{TEXT("Thumbs down"), White, 17}, {TEXT("Key  [   ·   again takes the vote back"), Rest, 13}};
	case EAcMusicCommand::Up:
		return {{TEXT("Thumbs up"), White, 17}, {TEXT("Key  ]   ·   again takes the vote back"), Rest, 13}};
	case EAcMusicCommand::Previous:
		return {{TEXT("Previous track"), White, 17}, {TEXT("Key  F7"), Key, 13},
			{TEXT("After its first 3 s: this track from the start"), Rest, 13}};
	case EAcMusicCommand::Next:
		return {{TEXT("Next track"), White, 17}, {TEXT("Key  F9"), Key, 13}};
	case EAcMusicCommand::Toggle:
		return {{Np && Np->bPaused ? TEXT("Play") : TEXT("Pause"), White, 17}, {TEXT("Key  F8"), Key, 13}};
	}
	return {};
}

// MARK: - Layout (MusicDeck.rebuild)

TArray<TPair<FAcRect, EAcMusicCommand>> SAcMusicDeck::Buttons(const FVector2D InSize, const bool bMusic)
{
	TArray<TPair<FAcRect, EAcMusicCommand>> Out;
	const FAcRect R = FAcRect(0, 0, InSize.X, InSize.Y).Inset(6, 5);
	if (R.W <= 40 || R.H <= 60 || !bMusic) return Out;
	// The buttons along the foot.
	const double Gap = 4;
	const double Side = FMath::Min(24.0, FMath::FloorToDouble((R.W - 4 * Gap) / 5));
	const double Row = 5 * Side + 4 * Gap;
	for (int32 K = 0; K < 5; ++K)
	{
		Out.Add({FAcRect(FMath::RoundHalfFromZero(R.MidX() - Row / 2 + K * (Side + Gap)), R.MinY(), Side, Side), Order[K]});
	}
	return Out;
}

TOptional<EAcMusicCommand> SAcMusicDeck::CommandAt(const FVector2D P) const
{
	for (const TPair<FAcRect, EAcMusicCommand>& B : Buttons(Size, Np.IsSet()))
	{
		if (B.Key.Contains(P)) return B.Value;
	}
	return {};
}

const FSlateBrush* SAcMusicDeck::CoverBrush() const
{
	UTexture2D* T = Np ? Np->Cover.Get() : nullptr;
	if (!T) return nullptr;
	if (Cover->GetResourceObject() != T)
	{
		Cover->SetResourceObject(T);
		Cover->ImageSize = FVector2D(T->GetSizeX(), T->GetSizeY());
	}
	return Cover.Get();
}

int32 SAcMusicDeck::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
	FSlateWindowElementList& Out, const int32 Layer, const FWidgetStyle& Style, const bool bParentEnabled) const
{
	Size = FVector2D(Geometry.GetLocalSize());
	FPaint P(Out, Geometry, Layer, Size.Y);
	const FAcRect R = FAcRect(0, 0, Size.X, Size.Y).Inset(6, 5);
	if (R.W <= 40 || R.H <= 60) return P.NextLayer();
	if (!Np)
	{
		Label(P, TEXT("NO MUSIC"), 13, Alpha(Soft(), 0.6), FVector2D(R.MidX(), R.MidY() - 5), EAcHAlign::Center);
		return P.NextLayer();
	}
	const FAcNowPlaying& N = *Np;

	// The buttons (MusicDeck.button).
	const TArray<TPair<FAcRect, EAcMusicCommand>> Frames = Buttons(Size, true);
	for (const TPair<FAcRect, EAcMusicCommand>& F : Frames)
	{
		const FAcRect& B = F.Key;
		const EAcMusicCommand C = F.Value;
		const bool bVoted = (C == EAcMusicCommand::Up && N.Vote > 0) || (C == EAcMusicCommand::Down && N.Vote < 0);
		const FLinearColor Tint = bVoted ? (C == EAcMusicCommand::Up ? UpColor() : DownColor()) : Cyan();
		P.FillRounded(B, 3, bVoted ? Alpha(Tint, 0.22) : Rgb(0.03, 0.08, 0.13, 0.95));
		P.Stroke(B, 3, Alpha(Tint, bVoted ? 0.95 : 0.5), bVoted ? 1.4 : 1, bVoted ? 1.5 : 0);
		const FSlateBrush* Icon = FAcIcons::Brush(TEXT("sym.") + SymbolName(C, bVoted, N.bPaused));
		if (!Icon) continue;
		const FVector2D IS = Icon->ImageSize;
		const double K = (B.W - 8) / FMath::Max(FMath::Max(IS.X, IS.Y), 1.0);
		const FAcRect At(B.MidX() - IS.X * K / 2, B.MidY() - IS.Y * K / 2, IS.X * K, IS.Y * K);
		P.Image(Icon, At, FPaint::BlendTint(bVoted ? FLinearColor::White : Tint, 1));
	}
	const double Side = Frames.Num() ? Frames[0].Key.W : 24;

	// The time: elapsed, the bar, the length.
	const double Pos = N.PositionAt(Now);
	const double Ty = R.MinY() + Side + 6;
	Label(P, Clock(Pos), 10, Soft(), FVector2D(R.MinX(), Ty), EAcHAlign::Left);
	const FString Total = Clock(N.Length);
	Label(P, Total, 10, Soft(), FVector2D(R.MaxX(), Ty), EAcHAlign::Right);
	const FAcRect Bar(R.MinX() + 26, Ty + 3, FMath::Max(R.W - 26 - FPaint::TextWidth(Total, 10) - 6, 10.0), 3);
	const FAcRect Back = Bar.Inset(-1, -1);
	P.FillRounded(Back, 1, Gray(0, 0.6));
	P.Stroke(Back, 1, Alpha(Cyan(), 0.35), 1);
	const double F = N.Length > 0 ? FMath::Clamp(Pos / N.Length, 0.0, 1.0) : 0.0;
	if (F > 0) P.Fill(FAcRect(Bar.X, Bar.Y, Bar.W * F, Bar.H), Cyan());

	// The title, shrunk to fit and cut short past that.
	const double TitleY = Ty + 15;
	const FLinearColor Color = N.bAd ? AdColor() : FLinearColor::White;
	const TPair<FString, double> Title = Fit(N.bAd ? TEXT("AD · ") + N.Title : N.Title, 13, R.W);
	Label(P, Title.Key, Title.Value, N.bPaused ? FAcHudStyle::Blend(Color, 0.35, Gray(0.5)) : Color,
		FVector2D(R.MidX(), TitleY), EAcHAlign::Center);

	// The cover over the rest.
	const double Top = R.MaxY(), Bottom = TitleY + 15;
	const double A = FMath::Min(Top - Bottom, R.W);
	if (A > 16)
	{
		const FAcRect Box(FMath::RoundHalfFromZero(R.MidX() - A / 2), Bottom + (Top - Bottom - A) / 2, A, A);
		const float Fade = N.bPaused ? 0.45f : 1.f;
		if (const FSlateBrush* Art = CoverBrush())
		{
			P.Image(Art, Box, FLinearColor(1, 1, 1, Fade));
			if (N.bStationCover)
			{
				// The station's card: its letters (drawn into the image in Swift).
				const double K = A / 256;
				CardText(P, Box, K, TEXT("KSTR"), 92, 256 * 0.5, FLinearColor::White, 4, Fade);
				CardText(P, Box, K, TEXT("88.7  STARDUST"), 30, 256 * 0.04, Rgb(1, 0.8, 0.4), 3, Fade);
			}
		}
		P.Stroke(Box, 2, Alpha(Cyan(), 0.5), 1);
		if (N.bPaused) Label(P, TEXT("PAUSED"), 13, FLinearColor::White, FVector2D(Box.MidX(), Box.MidY() - 5), EAcHAlign::Center);
	}
	return P.NextLayer();
}

// MARK: - Pointer

FVector2D SAcMusicDeck::PointOf(const FGeometry& Geometry, const FPointerEvent& Event) const
{
	Size = FVector2D(Geometry.GetLocalSize());
	const FVector2D L = FVector2D(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	return FVector2D(L.X, Size.Y - L.Y);
}

void SAcMusicDeck::Press(const EAcMusicCommand C)
{
	OnCommand.ExecuteIfBound(C);
}

FReply SAcMusicDeck::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	const TOptional<EAcMusicCommand> C = CommandAt(PointOf(Geometry, Event));
	if (!C) return FReply::Unhandled();
	Press(*C);
	return FReply::Handled();
}

FReply SAcMusicDeck::OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event)
{
	return OnMouseButtonDown(Geometry, Event);
}

FReply SAcMusicDeck::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	HoveredCommand = CommandAt(PointOf(Geometry, Event));
	return FReply::Unhandled();
}

void SAcMusicDeck::OnMouseLeave(const FPointerEvent& Event)
{
	HoveredCommand.Reset();
}

FCursorReply SAcMusicDeck::OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const
{
	// GameController.hovers: a music button takes the "select" arrow.
	const FVector2D L = FVector2D(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	const FVector2D S = FVector2D(Geometry.GetLocalSize());
	bool bOver = false;
	for (const TPair<FAcRect, EAcMusicCommand>& B : Buttons(S, Np.IsSet())) bOver |= B.Key.Contains(FVector2D(L.X, S.Y - L.Y));
	return bOver ? FCursorReply::Cursor(EMouseCursor::Hand) : FCursorReply::Unhandled();
}
